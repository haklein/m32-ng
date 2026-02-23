// TODO: SMOKE TEST — replace with proper HAL wiring + full LVGL UI refactor
//
// Exercises: display (ST7789 via LovyanGFX + LVGL), audio (TLV320 + I2S
// sidetone), and all key/paddle/encoder/touch inputs (PocketKeyInput).
//
// Flash env:pocketwroom; open Serial at 115200.
// Press paddles, encoder, buttons, or touch strips — events are logged.
// A startup VVV (. . . -) plays at 700 Hz / 18 WPM to verify audio.

#include <main.h>
#include <Arduino.h>
#include <ArduinoLog.h>

#include <lvgl.h>
#include <LovyanGFX.hpp>

#ifdef BOARD_POCKETWROOM
#include <audio_output.h>   // PocketAudioOutput
#include <key_input.h>      // PocketKeyInput, KeyEvent
#endif

// ── CW helpers ────────────────────────────────────────────────────────────────

#ifdef BOARD_POCKETWROOM

static PocketAudioOutput* s_audio = nullptr;
static PocketKeyInput*    s_keys  = nullptr;

static constexpr uint32_t DOT_MS  = 67;       // 18 WPM
static constexpr uint32_t DASH_MS = 201;      // 3 × dot
static constexpr uint16_t TONE_HZ = 700;

static void cw_dit()
{
    s_audio->tone_on(TONE_HZ); delay(DOT_MS);
    s_audio->tone_off();        delay(DOT_MS);   // inter-element gap
}

static void cw_dah()
{
    s_audio->tone_on(TONE_HZ); delay(DASH_MS);
    s_audio->tone_off();        delay(DOT_MS);   // inter-element gap
}

// Extra pause to reach 3-dot inter-character gap (1 dot already added after last element).
static void cw_charspace() { delay(DOT_MS * 2); }

// V = . . . -
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

// ── Display / LVGL ────────────────────────────────────────────────────────────

extern "C" uint32_t tick_get_cb(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

#ifdef BOARD_POCKETWROOM
static LGFX gfx;
#endif

static const unsigned int lvBufferSize =
    TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8);
static uint8_t lvBuffer[lvBufferSize];

static lv_obj_t* status_label = nullptr;

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

// ── setup ─────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.verboseln("M32 NG — Smoke Test Startup");

    M32Pocket_hal_init();

    // ── Display ───────────────────────────────────────────────────────────────
    Log.verboseln("Display init");
#ifdef BOARD_POCKETWROOM
    gfx.begin();
    gfx.setRotation(3);
#endif

    lv_init();
    lv_tick_set_cb(tick_get_cb);

    static auto* lvDisplay = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_rotation(lvDisplay, LV_DISPLAY_ROTATION_90);
    lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvDisplay, my_disp_flush);
    lv_display_set_buffers(lvDisplay, lvBuffer, nullptr, lvBufferSize,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "M32 Smoke Test\nReady.");
    lv_obj_center(status_label);
    lv_timer_handler();   // render initial screen before audio init

    // ── Audio + key input (Pocket only) ───────────────────────────────────────
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

    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput();

    Log.verboseln("Ready — press paddles / encoder / buttons / touch");
#endif
}

// ── loop ──────────────────────────────────────────────────────────────────────

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
