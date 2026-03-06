#ifdef BOARD_POCKETWROOM

#include "screenshot_server.h"
#include <ESPAsyncWebServer.h>
#include <lvgl.h>
#include "display/lv_display_private.h"
#include <ArduinoLog.h>
#include <cstring>
#include <esp_heap_caps.h>

// ── Screen geometry (logical, after rotation) ────────────────────────────────
static constexpr int SCR_W       = 320;
static constexpr int SCR_H       = 170;
static constexpr int BPP         = 2;       // RGB565
static constexpr int ROW_BYTES   = SCR_W * BPP;
static constexpr int PIXEL_BYTES = ROW_BYTES * SCR_H;  // 108,800
static constexpr int HDR_SIZE    = 14 + 40 + 12;       // BMP file+info+masks
static constexpr int BMP_SIZE    = HDR_SIZE + PIXEL_BYTES;

// ── State ────────────────────────────────────────────────────────────────────
static AsyncWebServer* s_server      = nullptr;
static uint8_t* s_ss_pixels         = nullptr;   // PSRAM shadow framebuffer
static lv_display_flush_cb_t s_orig_flush_cb = nullptr;

// ── BMP helpers ──────────────────────────────────────────────────────────────

static void put_le16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put_le32(uint8_t* p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void build_bmp_header(uint8_t* hdr)
{
    memset(hdr, 0, HDR_SIZE);
    hdr[0] = 'B'; hdr[1] = 'M';
    put_le32(hdr + 2,  BMP_SIZE);
    put_le32(hdr + 10, HDR_SIZE);
    put_le32(hdr + 14, 40);
    put_le32(hdr + 18, SCR_W);
    put_le32(hdr + 22, SCR_H);
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 16);
    put_le32(hdr + 30, 3);              // BI_BITFIELDS
    put_le32(hdr + 34, PIXEL_BYTES);
    put_le32(hdr + 54, 0xF800);         // red mask
    put_le32(hdr + 58, 0x07E0);         // green mask
    put_le32(hdr + 62, 0x001F);         // blue mask
}

// ── Flush hook ───────────────────────────────────────────────────────────────
// Continuously mirrors every flushed area into a PSRAM shadow framebuffer.
// No extra rendering pass needed — the buffer always reflects the screen.

static void screenshot_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    if (s_ss_pixels) {
        int32_t w = lv_area_get_width(area);
        int32_t h = lv_area_get_height(area);
        for (int32_t y = 0; y < h; y++) {
            int32_t dst_y = area->y1 + y;
            int32_t dst_x = area->x1;
            if (dst_y >= 0 && dst_y < SCR_H && dst_x >= 0) {
                int32_t copy_w = (dst_x + w > SCR_W) ? (SCR_W - dst_x) : w;
                if (copy_w > 0) {
                    memcpy(s_ss_pixels + dst_y * ROW_BYTES + dst_x * BPP,
                           px_map + y * w * BPP,
                           copy_w * BPP);
                }
            }
        }
    }

    // Chain to original flush.
    if (s_orig_flush_cb) {
        s_orig_flush_cb(disp, area, px_map);
    }
}

// ── No-op update (kept for API compatibility) ────────────────────────────────

void screenshot_server_update()
{
    // Shadow buffer is updated continuously via flush hook — nothing to do here.
}

// ── HTTP handler ─────────────────────────────────────────────────────────────

// Callback-based response to avoid AsyncResponseStream's internal buffer allocation.
static size_t screenshot_fill_cb(uint8_t* buffer, size_t maxLen, size_t index)
{
    // index = byte offset into the virtual BMP file.
    if (index >= (size_t)BMP_SIZE) return 0;

    size_t written = 0;

    // Write BMP header bytes if index is within header range.
    if (index < HDR_SIZE) {
        uint8_t hdr[HDR_SIZE];
        build_bmp_header(hdr);
        size_t hdr_remaining = HDR_SIZE - index;
        size_t hdr_chunk = (hdr_remaining < maxLen) ? hdr_remaining : maxLen;
        memcpy(buffer, hdr + index, hdr_chunk);
        written += hdr_chunk;
        index += hdr_chunk;
    }

    // Write pixel data (bottom-up row order).
    while (written < maxLen && index < (size_t)BMP_SIZE) {
        size_t pixel_offset = index - HDR_SIZE;
        // Which row in BMP bottom-up order?
        int bmp_row = pixel_offset / ROW_BYTES;
        int col_offset = pixel_offset % ROW_BYTES;
        // BMP row 0 = screen row (SCR_H-1)
        int screen_row = SCR_H - 1 - bmp_row;

        size_t row_remaining = ROW_BYTES - col_offset;
        size_t buf_remaining = maxLen - written;
        size_t chunk = (row_remaining < buf_remaining) ? row_remaining : buf_remaining;

        memcpy(buffer + written,
               s_ss_pixels + screen_row * ROW_BYTES + col_offset,
               chunk);
        written += chunk;
        index += chunk;
    }

    return written;
}

static void handle_screenshot(AsyncWebServerRequest* req)
{
    if (!s_ss_pixels) {
        req->send(503, "text/plain", "Screenshot buffer not allocated");
        return;
    }

    AsyncWebServerResponse* resp = req->beginResponse("image/bmp", BMP_SIZE, screenshot_fill_cb);
    resp->addHeader("Cache-Control", "no-cache");
    req->send(resp);
}

// ── Public API ───────────────────────────────────────────────────────────────

void screenshot_server_start()
{
    if (s_server) return;

    if (!s_ss_pixels) {
        s_ss_pixels = (uint8_t*)heap_caps_malloc(PIXEL_BYTES, MALLOC_CAP_SPIRAM);
        if (!s_ss_pixels) {
            Log.warningln("Screenshot: PSRAM malloc(%d) failed", PIXEL_BYTES);
            return;
        }
        memset(s_ss_pixels, 0, PIXEL_BYTES);
        Log.noticeln("Screenshot: allocated %d bytes in PSRAM", PIXEL_BYTES);
    }

    // Hook the display flush callback to mirror pixels into shadow buffer.
    lv_display_t* disp = lv_display_get_default();
    if (disp && !s_orig_flush_cb) {
        s_orig_flush_cb = disp->flush_cb;
        lv_display_set_flush_cb(disp, screenshot_flush_cb);
        Log.noticeln("Screenshot: flush callback hooked");
    }

    s_server = new AsyncWebServer(80);

    s_server->on("/screenshot.bmp", HTTP_GET,
                 [](AsyncWebServerRequest* r) { handle_screenshot(r); });

    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "<h2>Morserino-32 NG</h2>"
                 "<p><a href='/screenshot.bmp'>Screenshot (BMP)</a></p>"
                 "<p>Free heap: %u, PSRAM free: %u</p>",
                 esp_get_free_heap_size(),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        r->send(200, "text/html", buf);
    });

    s_server->begin();
    Log.noticeln("Screenshot server started on port 80");
}

void screenshot_server_stop()
{
    if (s_server) {
        s_server->end();
        delete s_server;
        s_server = nullptr;
    }

    lv_display_t* disp = lv_display_get_default();
    if (disp && s_orig_flush_cb) {
        lv_display_set_flush_cb(disp, s_orig_flush_cb);
        s_orig_flush_cb = nullptr;
    }
}

#endif // BOARD_POCKETWROOM
