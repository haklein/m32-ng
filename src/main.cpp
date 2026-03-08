// M32 NG — Morserino-32 Next Generation
//
// src/main.cpp: pocketwroom (ESP32-S3 WROOM) entry point.
//
// Two build modes (select via PlatformIO env):
//   SMOKE_TEST  (env:pocketwroom_smoke): exercises display, audio, and all
//               key/paddle/encoder/touch inputs.  Events logged to Serial.
//   default     (env:pocketwroom): full multi-screen LVGL UI with CW keyer,
//               CW generator, echo trainer, and settings screen.
//
// Flash and open Serial at 115200.

#include <main.h>
#include <Arduino.h>
#include <ArduinoLog.h>
#include <algorithm>
#include <string>
#include <random>

#include <lvgl.h>
#include <LovyanGFX.hpp>

#ifdef BOARD_POCKETWROOM
#include <audio_output.h>   // PocketAudioOutput
#include <key_input.h>      // PocketKeyInput, KeyEvent
#include <storage_nvs.h>    // PocketStorage (NVS)
#include <battery.h>        // PocketBattery
#include <network_esp32.h>  // Esp32Network (WiFi HAL)
#include <esp_sleep.h>      // deep sleep + GPIO wakeup
#include <WiFi.h>           // MAC address for AP SSID
#include <SPIFFS.h>         // sound effects filesystem
#include "screenshot_server.h"
#endif

// ── Common display helpers ─────────────────────────────────────────────────

extern "C" uint32_t tick_get_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

#ifdef BOARD_POCKETWROOM
static LGFX gfx;
#endif

static lv_display_t* s_lv_display = nullptr;

static const unsigned int lvBufferSize =
    TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8);
static uint8_t lvBuffer[lvBufferSize];

static void my_disp_flush(lv_display_t* display, const lv_area_t* area,
                           unsigned char* px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
#ifdef BOARD_POCKETWROOM
    if (gfx.getStartCount()) gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    gfx.pushPixelsDMA((uint16_t*)px_map, w * h, true);
#endif
    lv_display_flush_ready(display);
}

static void M32Pocket_hal_init()
{
#ifndef VEXT_ON_VALUE
#define VEXT_ON_VALUE LOW
#endif
#ifdef PIN_VEXT
    pinMode(PIN_VEXT, OUTPUT);
    digitalWrite(PIN_VEXT, VEXT_ON_VALUE);
#endif
}

// Initialises hardware, LVGL, and the LVGL display driver.
// Call after load_settings() so screen_flip is already known — avoids a
// double setRotation() that some panels don't handle cleanly.
static void display_init(bool flip)
{
    M32Pocket_hal_init();
    Log.verboseln("Display init (flip=%d)", flip);
#ifdef BOARD_POCKETWROOM
    gfx.begin();
    gfx.setRotation(flip ? 1 : 3);
#endif
    lv_init();
    lv_tick_set_cb(tick_get_cb);

    s_lv_display = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_rotation(s_lv_display,
        flip ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    lv_display_set_color_format(s_lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_lv_display, my_disp_flush);
    lv_display_set_buffers(s_lv_display, lvBuffer, nullptr, lvBufferSize,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

// ══════════════════════════════════════════════════════════════════════════
#ifdef SMOKE_TEST
// ── Smoke Test ────────────────────────────────────────────────────────────
// Exercises: display (ST7789 via LovyanGFX + LVGL), audio (TLV320 + I2S
// sidetone), all key/paddle/encoder/touch inputs (PocketKeyInput).
// A startup VVV (. . . -) plays at 700 Hz / 18 WPM to verify audio.

#ifdef BOARD_POCKETWROOM

static PocketAudioOutput* s_audio = nullptr;
static PocketKeyInput*    s_keys  = nullptr;

static constexpr uint32_t DOT_MS  = 67;       // 18 WPM
static constexpr uint32_t DASH_MS = 201;      // 3 × dot
static constexpr uint16_t TONE_HZ = 700;

static void cw_dit()
{
    s_audio->tone_on(TONE_HZ); delay(DOT_MS);
    s_audio->tone_off();        delay(DOT_MS);
}

static void cw_dah()
{
    s_audio->tone_on(TONE_HZ); delay(DASH_MS);
    s_audio->tone_off();        delay(DOT_MS);
}

static void cw_charspace() { delay(DOT_MS * 2); }
static void play_V() { cw_dit(); cw_dit(); cw_dit(); cw_dah(); }

static const char* key_event_name(KeyEvent ev)
{
    switch (ev) {
        case KeyEvent::PADDLE_DIT_DOWN:      return "DIT_DOWN";
        case KeyEvent::PADDLE_DIT_UP:        return "DIT_UP";
        case KeyEvent::PADDLE_DAH_DOWN:      return "DAH_DOWN";
        case KeyEvent::PADDLE_DAH_UP:        return "DAH_UP";
        case KeyEvent::STRAIGHT_DOWN:        return "STRAIGHT_DOWN";
        case KeyEvent::STRAIGHT_UP:          return "STRAIGHT_UP";
        case KeyEvent::ENCODER_CW:           return "ENC_CW";
        case KeyEvent::ENCODER_CCW:          return "ENC_CCW";
        case KeyEvent::BUTTON_ENCODER_SHORT: return "ENC_SHORT";
        case KeyEvent::BUTTON_ENCODER_LONG:  return "ENC_LONG";
        case KeyEvent::BUTTON_AUX_SHORT:     return "AUX_SHORT";
        case KeyEvent::BUTTON_AUX_LONG:      return "AUX_LONG";
        case KeyEvent::TOUCH_LEFT_DOWN:      return "TOUCH_L_DOWN";
        case KeyEvent::TOUCH_LEFT_UP:        return "TOUCH_L_UP";
        case KeyEvent::TOUCH_RIGHT_DOWN:     return "TOUCH_R_DOWN";
        case KeyEvent::TOUCH_RIGHT_UP:       return "TOUCH_R_UP";
        default:                             return "?";
    }
}

#endif // BOARD_POCKETWROOM

static lv_obj_t* status_label = nullptr;

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.verboseln("M32 NG — Smoke Test Startup");

    display_init(false);

    status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "M32 Smoke Test\nReady.");
    lv_obj_center(status_label);
    lv_timer_handler();

#ifdef BOARD_POCKETWROOM
    Log.verboseln("Audio init");
    s_audio = new PocketAudioOutput();
    s_audio->begin();
    s_audio->set_volume(14);
    s_audio->set_adsr(0.007f, 0.0f, 1.0f, 0.007f);

    Log.verboseln("Playing VVV");
    play_V(); cw_charspace();
    play_V(); cw_charspace();
    play_V();

    auto sample_idle = [](int pin) -> uint32_t {
        uint32_t sum = 0;
        for (int i = 0; i < 8; i++) { sum += touchRead(pin); delay(10); }
        return sum / 8;
    };
    uint32_t touch_l_idle = sample_idle(PIN_TOUCH_LEFT);
    uint32_t touch_r_idle = sample_idle(PIN_TOUCH_RIGHT);
    Log.verboseln("Touch calibrated: L_idle=%d R_idle=%d",
                  touch_l_idle, touch_r_idle);

    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput(touch_l_idle, touch_r_idle);

    Log.verboseln("Ready — press paddles / encoder / buttons / touch");
#endif
}

void loop()
{
#ifdef BOARD_POCKETWROOM
    KeyEvent ev;
    bool updated = false;
    while (s_keys->poll(ev)) {
        const char* name = key_event_name(ev);
        Log.verboseln("KEY: %s", name);
        lv_label_set_text(status_label, name);
        lv_obj_center(status_label);
        updated = true;
    }
    (void)updated;
#endif
    lv_timer_handler();
    delay(5);
}

// ══════════════════════════════════════════════════════════════════════════
#else // Full multi-screen UI
// ── Full UI ───────────────────────────────────────────────────────────────
//
// Controls:
//   Enc CW/CCW      — scroll menu / ±1 WPM in keyer|generator|echo
//   Enc short       — select / edit control in settings
//   Enc long        — back to previous screen
//   Aux short       — pause/resume in generator
//   Aux long        — return to main menu
//   Paddle L/Touch L — DIT    Paddle R/Touch R — DAH
//   Straight key    — straight key (keyer + echo screens)

#ifdef BOARD_POCKETWROOM

// After LV_DISPLAY_ROTATION_90 the logical viewport is TFT_HEIGHT × TFT_WIDTH.
static constexpr lv_coord_t SCREEN_W = TFT_HEIGHT;   // 320
static constexpr lv_coord_t SCREEN_H = TFT_WIDTH;    // 170

static unsigned long app_millis() { return (unsigned long)millis(); }

#include "app_ui.hpp"

// ── setup ─────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(2000);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.verboseln("M32 NG — Startup");

    // ── Settings persistence (load BEFORE display so rotation is set once) ──
    s_storage = new PocketStorage();
    load_settings();
    Log.verboseln("Settings: v=%d wpm=%d flip=%d",
                  s_settings.version, s_settings.wpm, s_settings.screen_flip);

    display_init(s_settings.screen_flip);
    gfx.setBrightness(s_settings.brightness);

    s_on_screen_flip = [](bool flip) {
        gfx.setRotation(flip ? 1 : 3);
        lv_display_set_rotation(s_lv_display,
            flip ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    };

    // ── Deep sleep callback ────────────────────────────────────────────────
    s_enter_deep_sleep = []() {
        Log.verboseln("Entering deep sleep");
        s_audio->tone_off();
        s_audio->suspend();                          // power down codec + I2S
        digitalWrite(PIN_VEXT, !VEXT_ON_VALUE);      // cut peripheral power
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);  // LOW=pressed
        esp_deep_sleep_start();                      // does not return
    };

    // Splash screen — logo + version, shown during peripheral init
    unsigned long splash_start = millis();
    lv_obj_t* splash_scr = lv_screen_active();
    lv_obj_set_style_bg_color(splash_scr, lv_color_hex(0x000000), 0);

    // Logo (RGB565, white on black)
    #include "logo.h"
    lv_obj_t* logo = lv_image_create(splash_scr);
    lv_image_set_src(logo, &logo_img);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -20);

    // Version + status label
    lv_obj_t* splash_lbl = lv_label_create(splash_scr);
    lv_obj_set_style_text_color(splash_lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(splash_lbl, &lv_font_montserrat_20, 0);
#ifdef GIT_VERSION
    lv_label_set_text(splash_lbl, GIT_VERSION);
#else
    lv_label_set_text(splash_lbl, "");
#endif
    lv_obj_align(splash_lbl, LV_ALIGN_CENTER, 0, 50);
    lv_timer_handler();

    // ── SPIFFS (sound effects, future file storage) ─────────────────────
    if (!SPIFFS.begin(true))
        Log.warningln("SPIFFS mount failed");

    // ── Audio ─────────────────────────────────────────────────────────────
    Log.verboseln("Audio init");
    s_audio = new PocketAudioOutput();
    s_audio->begin();
    // Volume and ADSR set via apply_settings() after engine init.

    // ── Touch calibration ─────────────────────────────────────────────────
    auto sample_idle = [](int pin) -> uint32_t {
        uint32_t sum = 0;
        for (int i = 0; i < 8; i++) { sum += touchRead(pin); delay(10); }
        return sum / 8;
    };
    uint32_t touch_l_idle = sample_idle(PIN_TOUCH_LEFT);
    uint32_t touch_r_idle = sample_idle(PIN_TOUCH_RIGHT);
    Log.verboseln("Touch: L_idle=%d R_idle=%d on=+%d off=+%d",
                  touch_l_idle, touch_r_idle, 4000, 2500);

    // ── Key input ─────────────────────────────────────────────────────────
    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput(touch_l_idle, touch_r_idle);

    // ── Straight key — paddle jack in straight mode (ext_key_iambic=false) ─
    // PADDLE_DIT_DOWN/UP events set s_straight_key_state when ext_key_iambic
    // is false.  StraightKeyer's noise blanker debounces on top.
    // PIN_KEYER (GPIO 41) is the MOSFET output — configured in PocketKeyInput.
    s_read_straight_key = []() -> bool { return s_straight_key_state; };

    // ── Battery ADC + charger status ─────────────────────────────────────
    static PocketBattery s_battery;
    s_battery.begin();
    s_read_battery_percent = []() -> uint8_t { return s_battery.percent(); };
    s_is_charging          = []() -> bool    { return s_battery.charging(); };
    s_set_brightness       = [](uint8_t level) { gfx.setBrightness(level); };

    // ── WiFi HAL + AP SSID ────────────────────────────────────────────────
    s_network = new Esp32Network();
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Morserino-%02X%02X",
                 mac[4], mac[5]);
        Log.verboseln("AP SSID: %s", s_ap_ssid);
    }
    // Show version for at least 3 seconds before WiFi status replaces it
    {
        unsigned long ver_elapsed = millis() - splash_start;
        if (ver_elapsed < 3000) delay(3000 - ver_elapsed);
    }
    // Try connecting with saved credentials
    {
        char wifi_ssid[33] = {};
        char wifi_pass[65] = {};
        if (load_wifi_creds(wifi_ssid, sizeof(wifi_ssid),
                            wifi_pass, sizeof(wifi_pass))) {
            Log.noticeln("WiFi: connecting to '%s'", wifi_ssid);
            lv_label_set_text(splash_lbl, "Connecting WiFi...");
            lv_timer_handler();
            bool ok = s_network->wifi_connect(wifi_ssid, wifi_pass, 8000);
            Log.noticeln("WiFi: %s", ok ? "connected" : "failed");
            if (ok) screenshot_server_start();
        }
    }

    // ── Ensure splash shows for at least 4 seconds ─────────────────────
    unsigned long elapsed = millis() - splash_start;
    if (elapsed < 4000) delay(4000 - elapsed);

    // ── CW engine + UI ────────────────────────────────────────────────────
    app_ui_init(esp_random());

    Log.verboseln("Ready");
}

// ── loop ──────────────────────────────────────────────────────────────────
// CW task consumes PocketKeyInput queue directly and forwards non-CW events
// (encoder, buttons) to s_ui_event_queue.  UI loop drains that queue.
void loop()
{
    KeyEvent ev;
    while (xQueueReceive(s_ui_event_queue, &ev, 0) == pdTRUE) {
        route_ui(ev);
    }
    app_ui_tick();
    delay(1);
}

#else // !BOARD_POCKETWROOM

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    display_init(false);
    lv_obj_t* lbl = lv_label_create(lv_screen_active());
    lv_label_set_text(lbl, "M32 NG\n(board not supported)");
    lv_obj_center(lbl);
    lv_timer_handler();
}

void loop()
{
    lv_timer_handler();
    delay(5);
}

#endif // BOARD_POCKETWROOM

#endif // SMOKE_TEST / Full UI
