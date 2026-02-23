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

#ifndef SMOKE_TEST
#include "paddle_ctl.h"
#include "iambic_keyer.h"
#include "morse_decoder.h"
#include "morse_trainer.h"
#include "text_generators.h"
#include "cw_textfield.hpp"
#include "screen_stack.hpp"
#include "status_bar.hpp"
#endif // !SMOKE_TEST
#endif // BOARD_POCKETWROOM

// ── Common display helpers ─────────────────────────────────────────────────────

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
// Must be called once before any lv_* calls.
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

// ══════════════════════════════════════════════════════════════════════════════
#ifdef SMOKE_TEST
// ── Smoke Test ────────────────────────────────────────────────────────────────
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

// ══════════════════════════════════════════════════════════════════════════════
#else // Full multi-screen UI
// ── Full UI ───────────────────────────────────────────────────────────────────
//
// Screens: Main Menu → [CW Keyer | CW Generator | Echo Trainer | Settings]
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
static constexpr lv_coord_t SCREEN_W  = TFT_HEIGHT;           // 320
static constexpr lv_coord_t SCREEN_H  = TFT_WIDTH;            // 170
static constexpr lv_coord_t CONTENT_Y = StatusBar::HEIGHT + 2; // 22

static unsigned long hw_millis() { return (unsigned long)millis(); }

// ── Global settings ───────────────────────────────────────────────────────────
struct AppSettings {
    int      wpm     = 15;
    bool     mode_a  = false;   // false = Iambic B
    uint16_t freq_hz = 700;
    uint8_t  volume  = 7;
};
static AppSettings s_settings;

// ── Active CW mode ────────────────────────────────────────────────────────────
enum class ActiveMode { NONE, KEYER, GENERATOR, ECHO };
static ActiveMode s_active_mode = ActiveMode::NONE;

// ── Hardware ──────────────────────────────────────────────────────────────────
static PocketAudioOutput* s_audio = nullptr;
static PocketKeyInput*    s_keys  = nullptr;

// ── CW engine (keyer + echo modes) ───────────────────────────────────────────
static PaddleCtl*    s_paddle      = nullptr;
static IambicKeyer*  s_keyer       = nullptr;
static MorseDecoder* s_decoder     = nullptr;
static uint32_t      s_straight_t0 = 0;

// ── Trainer (generator + echo modes) ─────────────────────────────────────────
static MorseTrainer*   s_trainer    = nullptr;
static TextGenerators* s_gen        = nullptr;
static std::mt19937    s_rng;
static bool            s_gen_paused = false;

// ── UI widgets updated from engine callbacks ──────────────────────────────────
static CWTextField* s_keyer_tf  = nullptr;
static CWTextField* s_gen_tf    = nullptr;
static StatusBar*   s_active_sb = nullptr;

// ── Echo trainer widgets (set/cleared with echo screen) ──────────────────────
static lv_obj_t*   s_echo_target_lbl = nullptr;   // phrase being played
static lv_obj_t*   s_echo_rcvd_lbl   = nullptr;   // chars received so far
static lv_obj_t*   s_echo_result_lbl = nullptr;   // "OK" / "ERR"
static std::string s_echo_typed;                   // mirrors trainer's received_phrase_

// ── Screen stack & navigation ─────────────────────────────────────────────────
static ScreenStack s_stack;

// LVGL encoder indev
static lv_indev_t* s_enc_indev        = nullptr;
static int32_t     s_enc_diff         = 0;
static int         s_enc_press_frames = 0;

static lv_group_t* s_menu_group     = nullptr;
static lv_group_t* s_settings_group = nullptr;

// ── Forward declarations ──────────────────────────────────────────────────────
static lv_obj_t* build_main_menu();
static lv_obj_t* build_keyer_screen();
static lv_obj_t* build_generator_screen();
static lv_obj_t* build_echo_screen();
static lv_obj_t* build_settings_screen();
static void      apply_settings();
static void      route(KeyEvent ev);

// ── Encoder indev read callback ───────────────────────────────────────────────
static void enc_read_cb(lv_indev_t*, lv_indev_data_t* d)
{
    d->enc_diff = s_enc_diff;
    s_enc_diff  = 0;
    if (s_enc_press_frames > 0) {
        d->state = LV_INDEV_STATE_PR;
        --s_enc_press_frames;
    } else {
        d->state = LV_INDEV_STATE_REL;
    }
}

// ── CW engine callbacks ───────────────────────────────────────────────────────
static void on_play_state(PlayState state)
{
    switch (state) {
        case PLAY_STATE_DOT_ON:
        case PLAY_STATE_DASH_ON:
            s_audio->tone_on(s_settings.freq_hz);
            s_decoder->set_transmitting(true);
            break;
        case PLAY_STATE_DOT_OFF:
            s_audio->tone_off();
            s_decoder->append_dot();
            s_decoder->set_transmitting(false);
            break;
        case PLAY_STATE_DASH_OFF:
            s_audio->tone_off();
            s_decoder->append_dash();
            s_decoder->set_transmitting(false);
            break;
        default:
            break;
    }
}

static void on_lever_state(LeverState state)
{
    s_keyer->setLeverState(state);
}

static void on_letter_decoded(const std::string& letter)
{
    if (s_active_mode == ActiveMode::KEYER) {
        if (!s_keyer_tf) return;
        if (letter == " ") s_keyer_tf->next_word();
        else               s_keyer_tf->add_string(letter);
    } else if (s_active_mode == ActiveMode::ECHO) {
        s_trainer->symbol_received(letter);
        if (s_echo_rcvd_lbl) {
            if      (letter == "<err>") { if (!s_echo_typed.empty()) s_echo_typed.pop_back(); }
            else if (letter != " ")     { s_echo_typed += letter; }
            lv_label_set_text(s_echo_rcvd_lbl, s_echo_typed.c_str());
        }
    }
}

// ── Propagate settings to all engine objects ──────────────────────────────────
static void apply_settings()
{
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_keyer->setSpeedWPM((unsigned long)s_settings.wpm);
    s_keyer->setModeA(s_settings.mode_a);
    s_decoder->set_decode_threshold(dit_ms * 3);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_audio->set_volume(s_settings.volume);
    if (s_active_sb) s_active_sb->set_wpm(s_settings.wpm);
}

// ── Screen: Main Menu ─────────────────────────────────────────────────────────
static lv_obj_t* build_main_menu()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Morserino-32-NG");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, SCREEN_W - 40, SCREEN_H - 44);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 10);

    if (s_menu_group) lv_group_del(s_menu_group);
    s_menu_group = lv_group_create();

    static const auto push_screen = [](lv_event_t* e) {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        lv_obj_t*       ns       = nullptr;
        ScreenStack::Cb enter_cb = {};
        ScreenStack::Cb leave_cb = {};

        if (idx == 0) {
            ns = build_keyer_screen();
            enter_cb = []() {
                s_active_mode = ActiveMode::KEYER;
                lv_indev_set_group(s_enc_indev, nullptr);
            };
            leave_cb = []() {
                s_active_mode = ActiveMode::NONE;
                delete s_keyer_tf; s_keyer_tf = nullptr;
                delete s_active_sb; s_active_sb = nullptr;
            };
        } else if (idx == 1) {
            ns = build_generator_screen();
            enter_cb = []() {
                s_active_mode = ActiveMode::GENERATOR;
                s_gen_paused  = false;
                s_trainer->set_state(MorseTrainer::TrainerState::Player);
                s_trainer->set_playing();
                lv_indev_set_group(s_enc_indev, nullptr);
            };
            leave_cb = []() {
                s_active_mode = ActiveMode::NONE;
                s_trainer->set_idle();
                s_audio->tone_off();
                delete s_gen_tf; s_gen_tf = nullptr;
                delete s_active_sb; s_active_sb = nullptr;
            };
        } else if (idx == 2) {
            ns = build_echo_screen();
            enter_cb = []() {
                s_active_mode = ActiveMode::ECHO;
                s_trainer->set_state(MorseTrainer::TrainerState::Echo);
                s_trainer->set_playing();
                lv_indev_set_group(s_enc_indev, nullptr);
            };
            leave_cb = []() {
                s_active_mode = ActiveMode::NONE;
                s_trainer->set_idle();
                s_trainer->set_state(MorseTrainer::TrainerState::Player);
                s_audio->tone_off();
                s_echo_target_lbl = nullptr;
                s_echo_rcvd_lbl   = nullptr;
                s_echo_result_lbl = nullptr;
                delete s_active_sb; s_active_sb = nullptr;
            };
        } else {
            ns = build_settings_screen();
            enter_cb = []() {
                lv_indev_set_group(s_enc_indev, s_settings_group);
            };
            leave_cb = []() {
                delete s_active_sb; s_active_sb = nullptr;
            };
        }
        s_stack.push(ns, std::move(enter_cb), std::move(leave_cb));
    };

    static const struct { const char* icon; const char* label; } items[] = {
        { LV_SYMBOL_AUDIO,    "CW Keyer"      },
        { LV_SYMBOL_PLAY,     "CW Generator"  },
        { LV_SYMBOL_LOOP,     "Echo Trainer"  },
        { LV_SYMBOL_SETTINGS, "Settings"      },
    };
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* btn = lv_list_add_button(list, items[i].icon, items[i].label);
        lv_group_add_obj(s_menu_group, btn);
        lv_obj_add_event_cb(btn, push_screen, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    return scr;
}

// ── Screen: CW Keyer ──────────────────────────────────────────────────────────
static lv_obj_t* build_keyer_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("CW Keyer");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

    s_keyer_tf = new CWTextField(scr);
    lv_obj_set_pos(s_keyer_tf->obj(), 4, CONTENT_Y + 2);
    lv_obj_set_size(s_keyer_tf->obj(), SCREEN_W - 8, SCREEN_H - CONTENT_Y - 6);

    return scr;
}

// ── Screen: CW Generator ──────────────────────────────────────────────────────
static lv_obj_t* build_generator_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("CW Generator");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

    s_gen_tf = new CWTextField(scr);
    lv_obj_set_pos(s_gen_tf->obj(), 4, CONTENT_Y + 2);
    lv_obj_set_size(s_gen_tf->obj(), SCREEN_W - 8, SCREEN_H - CONTENT_Y - 6);

    return scr;
}

// ── Screen: Echo Trainer ──────────────────────────────────────────────────────
static lv_obj_t* build_echo_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("Echo Trainer");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

    // Divide remaining height into thirds: Sent / Rcvd / Result
    const lv_coord_t ROW_H  = (SCREEN_H - CONTENT_Y) / 3;  // ~49 px
    const lv_coord_t ROW1_Y = CONTENT_Y + 4;
    const lv_coord_t ROW2_Y = CONTENT_Y + ROW_H + 4;

    lv_obj_t* l1 = lv_label_create(scr);
    lv_label_set_text(l1, "Sent:");
    lv_obj_set_pos(l1, 8, ROW1_Y + 8);

    s_echo_target_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_target_lbl, "...");
    lv_obj_set_pos(s_echo_target_lbl, 60, ROW1_Y + 6);
    lv_obj_set_width(s_echo_target_lbl, SCREEN_W - 68);

    lv_obj_t* l2 = lv_label_create(scr);
    lv_label_set_text(l2, "Rcvd:");
    lv_obj_set_pos(l2, 8, ROW2_Y + 8);

    s_echo_rcvd_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_rcvd_lbl, "");
    lv_obj_set_pos(s_echo_rcvd_lbl, 60, ROW2_Y + 6);
    lv_obj_set_width(s_echo_rcvd_lbl, SCREEN_W - 68);

    s_echo_result_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_result_lbl, "");
    lv_obj_align(s_echo_result_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

    return scr;
}

// ── Screen: Settings ──────────────────────────────────────────────────────────
static lv_obj_t* build_settings_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("Settings");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

    if (s_settings_group) lv_group_del(s_settings_group);
    s_settings_group = lv_group_create();

    // Layout tuned for 320×170: 4 rows of 34 px each fit below the status bar.
    const lv_coord_t LBL_X   = 8;
    const lv_coord_t CTL_X   = SCREEN_W / 2 + 20;  // 180
    const lv_coord_t ROW_H   = 34;
    const lv_coord_t START_Y = CONTENT_Y + 4;       // 26

    auto make_label = [&](const char* text, int row) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, text);
        lv_obj_set_pos(lbl, LBL_X, START_Y + row * ROW_H + 10);
    };

    // WPM
    make_label("Speed (WPM)", 0);
    lv_obj_t* wpm_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(wpm_spn, 5, 40);
    lv_spinbox_set_digit_count(wpm_spn, 2);
    lv_spinbox_set_value(wpm_spn, s_settings.wpm);
    lv_obj_set_width(wpm_spn, 100);
    lv_obj_set_pos(wpm_spn, CTL_X, START_Y + 0 * ROW_H + 2);
    lv_group_add_obj(s_settings_group, wpm_spn);
    lv_obj_add_event_cb(wpm_spn, [](lv_event_t* e) {
        s_settings.wpm = (int)lv_spinbox_get_value(lv_event_get_target_obj(e));
        apply_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Keyer mode
    make_label("Keyer Mode", 1);
    lv_obj_t* mode_dd = lv_dropdown_create(scr);
    lv_dropdown_set_options(mode_dd, "Iambic A\nIambic B");
    lv_dropdown_set_selected(mode_dd, s_settings.mode_a ? 0u : 1u);
    lv_obj_set_width(mode_dd, 140);
    lv_obj_set_pos(mode_dd, CTL_X, START_Y + 1 * ROW_H + 2);
    lv_group_add_obj(s_settings_group, mode_dd);
    lv_obj_add_event_cb(mode_dd, [](lv_event_t* e) {
        s_settings.mode_a = (lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 0u);
        apply_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Frequency
    make_label("Freq (Hz)", 2);
    lv_obj_t* freq_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(freq_spn, 400, 900);
    lv_spinbox_set_digit_count(freq_spn, 3);
    lv_spinbox_set_step(freq_spn, 10);
    lv_spinbox_set_value(freq_spn, s_settings.freq_hz);
    lv_obj_set_width(freq_spn, 120);
    lv_obj_set_pos(freq_spn, CTL_X, START_Y + 2 * ROW_H + 2);
    lv_group_add_obj(s_settings_group, freq_spn);
    lv_obj_add_event_cb(freq_spn, [](lv_event_t* e) {
        s_settings.freq_hz = (uint16_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Volume
    make_label("Volume (0-10)", 3);
    lv_obj_t* vol_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(vol_spn, 0, 10);
    lv_spinbox_set_digit_count(vol_spn, 1);
    lv_spinbox_set_value(vol_spn, s_settings.volume);
    lv_obj_set_width(vol_spn, 80);
    lv_obj_set_pos(vol_spn, CTL_X, START_Y + 3 * ROW_H + 2);
    lv_group_add_obj(s_settings_group, vol_spn);
    lv_obj_add_event_cb(vol_spn, [](lv_event_t* e) {
        s_settings.volume = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
        s_audio->set_volume(s_settings.volume);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    return scr;
}

// ── Key event router ──────────────────────────────────────────────────────────
static void route(KeyEvent ev)
{
    switch (ev) {
        case KeyEvent::ENCODER_CW:
            s_enc_diff++;
            if (s_active_mode == ActiveMode::KEYER  ||
                s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO) {
                s_settings.wpm = std::min(s_settings.wpm + 1, 40);
                apply_settings();
            }
            break;
        case KeyEvent::ENCODER_CCW:
            s_enc_diff--;
            if (s_active_mode == ActiveMode::KEYER  ||
                s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO) {
                s_settings.wpm = std::max(s_settings.wpm - 1, 5);
                apply_settings();
            }
            break;

        case KeyEvent::BUTTON_ENCODER_SHORT:
            s_enc_press_frames = 2;
            break;
        case KeyEvent::BUTTON_ENCODER_LONG:
            s_stack.pop();
            if (s_stack.size() == 1)
                lv_indev_set_group(s_enc_indev, s_menu_group);
            break;

        case KeyEvent::BUTTON_AUX_SHORT:
            if (s_active_mode == ActiveMode::GENERATOR) {
                s_gen_paused = !s_gen_paused;
                if (s_gen_paused) s_trainer->set_idle();
                else              s_trainer->set_playing();
            }
            break;
        case KeyEvent::BUTTON_AUX_LONG:
            s_stack.pop_all();
            lv_indev_set_group(s_enc_indev, s_menu_group);
            break;

        // Paddle and touch strips both drive the iambic paddle.
        // In ECHO mode, any DOWN event also tames the receive timeout.
        case KeyEvent::PADDLE_DIT_DOWN:
        case KeyEvent::TOUCH_LEFT_DOWN:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO)  s_paddle->setDotPushed(true);
            if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::PADDLE_DIT_UP:
        case KeyEvent::TOUCH_LEFT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO)  s_paddle->setDotPushed(false);
            break;
        case KeyEvent::PADDLE_DAH_DOWN:
        case KeyEvent::TOUCH_RIGHT_DOWN:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO)  s_paddle->setDashPushed(true);
            if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::PADDLE_DAH_UP:
        case KeyEvent::TOUCH_RIGHT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO)  s_paddle->setDashPushed(false);
            break;

        case KeyEvent::STRAIGHT_DOWN:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO) {
                s_straight_t0 = (uint32_t)hw_millis();
                s_audio->tone_on(s_settings.freq_hz);
                s_decoder->set_transmitting(true);
                if (s_active_mode == ActiveMode::ECHO)
                    s_trainer->tame_echo_timeout();
            }
            break;
        case KeyEvent::STRAIGHT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO) {
                uint32_t dur = (uint32_t)hw_millis() - s_straight_t0;
                unsigned long dit = 1200u / (unsigned long)s_settings.wpm;
                s_audio->tone_off();
                s_decoder->set_transmitting(false);
                if (dur < dit * 2) s_decoder->append_dot();
                else               s_decoder->append_dash();
            }
            break;

        default:
            break;
    }
}

// ── setup ─────────────────────────────────────────────────────────────────────

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

    // ── Audio ─────────────────────────────────────────────────────────────────
    Log.verboseln("Audio init");
    s_audio = new PocketAudioOutput();
    s_audio->begin();
    s_audio->set_volume(s_settings.volume);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    // ── Touch calibration ─────────────────────────────────────────────────────
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

    // ── Key input ─────────────────────────────────────────────────────────────
    Log.verboseln("Key input init");
    s_keys = new PocketKeyInput(touch_threshold);

    // ── CW engine ─────────────────────────────────────────────────────────────
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_decoder = new MorseDecoder(dit_ms * 3, on_letter_decoded, hw_millis);
    s_keyer   = new IambicKeyer(dit_ms, on_play_state, hw_millis, s_settings.mode_a);
    s_paddle  = new PaddleCtl(/*debounce_ms=*/2, on_lever_state, hw_millis);

    // ── Text generator + trainer ──────────────────────────────────────────────
    s_rng.seed(esp_random());
    s_gen = new TextGenerators(s_rng);
    s_trainer = new MorseTrainer(
        [](bool on) {
            if (on) s_audio->tone_on(s_settings.freq_hz);
            else    s_audio->tone_off();
        },
        []() -> std::string {
            std::string phrase = s_gen->random_word();
            if (s_gen_tf)          s_gen_tf->add_string(phrase + " ");
            if (s_echo_target_lbl) {
                s_echo_typed.clear();
                if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
                if (s_echo_result_lbl) lv_label_set_text(s_echo_result_lbl, "");
                lv_label_set_text(s_echo_target_lbl, phrase.c_str());
            }
            return phrase;
        },
        hw_millis);
    s_trainer->set_state(MorseTrainer::TrainerState::Player);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_trainer->set_echo_result_fn([](const std::string& /*phrase*/, bool success) {
        s_echo_typed.clear();
        if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
        if (s_echo_result_lbl) {
            lv_label_set_text(s_echo_result_lbl, success ? "OK" : "ERR");
            lv_obj_set_style_text_color(s_echo_result_lbl,
                success ? lv_palette_main(LV_PALETTE_GREEN)
                        : lv_palette_main(LV_PALETTE_RED), 0);
        }
    });

    // ── Encoder indev ─────────────────────────────────────────────────────────
    s_enc_indev = lv_indev_create();
    lv_indev_set_type(s_enc_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_enc_indev, enc_read_cb);

    // ── Push main menu ────────────────────────────────────────────────────────
    s_stack.push(build_main_menu(),
        []() { lv_indev_set_group(s_enc_indev, s_menu_group); },
        {});

    Log.verboseln("Ready");
}

// ── loop ──────────────────────────────────────────────────────────────────────

void loop()
{
    KeyEvent ev;
    while (s_keys->poll(ev)) route(ev);

    if (s_active_mode == ActiveMode::KEYER ||
        s_active_mode == ActiveMode::ECHO) {
        s_paddle->tick();
        s_keyer->tick();
        s_decoder->tick();
    }
    if (s_active_mode == ActiveMode::GENERATOR ||
        s_active_mode == ActiveMode::ECHO) {
        s_trainer->tick();
    }

    lv_timer_handler();
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
