#ifdef BOARD_POCKETWROOM

#include "qr_canvas.h"
#include <qrcode.h>
#include <cstring>
#include <new>

static uint8_t* s_canvas_buf = nullptr;

lv_obj_t* qr_canvas_create(lv_obj_t* parent, const char* text,
                            int32_t max_px)
{
    // Version 4 QR code handles up to ~50 alphanumeric chars.
    static constexpr int QR_VERSION = 4;
    static constexpr int QR_BUF_SIZE = 140;
    static uint8_t qr_data[QR_BUF_SIZE];

    QRCode qr;
    qrcode_initText(&qr, qr_data, QR_VERSION, ECC_LOW, text);

    const int modules   = qr.size;   // e.g. 33 for version 4
    const int module_px = max_px / modules;
    if (module_px < 1) return nullptr;

    const int32_t side = module_px * modules;

    // L8 buffer: 1 byte per pixel, write directly (no layer API needed).
    // Use side as stride — must match what LVGL computes for the canvas.
    const uint32_t buf_sz = (uint32_t)side * side;
    delete[] s_canvas_buf;
    s_canvas_buf = new(std::nothrow) uint8_t[buf_sz];
    if (!s_canvas_buf) return nullptr;

    // Fill white (0xFF in L8)
    memset(s_canvas_buf, 0xFF, buf_sz);

    // Draw black modules directly into the pixel buffer
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (qrcode_getModule(&qr, x, y)) {
                for (int py = 0; py < module_px; ++py) {
                    uint8_t* row = s_canvas_buf +
                                   (y * module_px + py) * side +
                                   x * module_px;
                    memset(row, 0x00, module_px);
                }
            }
        }
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, s_canvas_buf, side, side,
                         LV_COLOR_FORMAT_L8);
    return canvas;
}

void qr_canvas_destroy()
{
    delete[] s_canvas_buf;
    s_canvas_buf = nullptr;
}

#endif // BOARD_POCKETWROOM
