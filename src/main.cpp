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
#endif

// ── Common display helpers ─────────────────────────────────────────────────

extern "C" uint32_t tick_get_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

#ifdef BOARD_POCKETWROOM
static LGFX gfx;
#endif

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
static void display_init()
{
    M32Pocket_hal_init();
    Log.verboseln("Display init");
#ifdef BOARD_POCKETWROOM
    gfx.begin();
    gfx.setRotation(3);
#endif
    lv_init();
    lv_tick_set_cb(tick_get_cb);

    static lv_display_t* lvDisplay = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_rotation(lvDisplay, LV_DISPLAY_ROTATION_90);
    lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvDisplay, my_disp_flush);
    lv_display_set_buffers(lvDisplay, lvBuffer, nullptr, lvBufferSize,
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

    display_init();

    status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "M32 Smoke Test\nReady.");
    lv_obj_center(status_label);
    lv_timer_handler();

#ifdef BOARD_POCKETWROOM
    Log.verboseln("Audio init");
    s_audio = new PocketAudioOutput();
    s_audio->begin();
    s_audio->set_volume(7);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    Log.verboseln("Playing VVV");
    play_V(); cw_charspace();
    play_V(); cw_charspace();
    play_V();

    auto sample_idle = [](int pin) -> uint32_t {
        uint32_t mn = UINT32_MAX;
        for (int i = 0; i < 5; i++) { mn = std::min(mn, (uint32_t)touchRead(pin)); delay(10); }
        return mn;
    };
    uint32_t touch_l_idle = sample_idle(PIN_TOUCH_LEFT);
    uint32_t touch_r_idle = sample_idle(PIN_TOUCH_RIGHT);
    uint32_t touch_threshold = std::max(touch_l_idle, touch_r_idle) + 3000;
    Log.verboseln("Touch calibrated: L_idle=%d R_idle=%d threshold=%d",
                  touch_l_idle, touch_r_idle, touch_threshold);

    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput(touch_threshold);

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

    display_init();

    // Splash while peripherals init
    lv_obj_t* splash = lv_label_create(lv_screen_active());
    lv_label_set_text(splash, "M32 NG");
    lv_obj_center(splash);
    lv_timer_handler();

    // ── Settings persistence ─────────────────────────────────────────────
    s_storage = new PocketStorage();
    load_settings();

    // ── Audio ─────────────────────────────────────────────────────────────
    Log.verboseln("Audio init");
    s_audio = new PocketAudioOutput();
    s_audio->begin();
    s_audio->set_volume(s_settings.volume);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    // ── Touch calibration ─────────────────────────────────────────────────
    auto sample_idle = [](int pin) -> uint32_t {
        uint32_t mn = UINT32_MAX;
        for (int i = 0; i < 5; i++) { mn = std::min(mn, (uint32_t)touchRead(pin)); delay(10); }
        return mn;
    };
    uint32_t touch_l_idle = sample_idle(PIN_TOUCH_LEFT);
    uint32_t touch_r_idle = sample_idle(PIN_TOUCH_RIGHT);
    uint32_t touch_threshold = std::max(touch_l_idle, touch_r_idle) + 3000;
    Log.verboseln("Touch: L=%d R=%d threshold=%d",
                  touch_l_idle, touch_r_idle, touch_threshold);

    // ── Key input ─────────────────────────────────────────────────────────
    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput(touch_threshold);

    // ── CW engine + UI ────────────────────────────────────────────────────
    app_ui_init(esp_random());

    Log.verboseln("Ready");
}

// ── loop ──────────────────────────────────────────────────────────────────
void loop()
{
    KeyEvent ev;
    while (s_keys->poll(ev)) route(ev);
    app_ui_tick();
    delay(5);
}

#else // !BOARD_POCKETWROOM

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    display_init();
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
