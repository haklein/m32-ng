// Morserino-32-NG — Desktop Simulator
//
// Screens:
//   Main Menu → [CW Keyer | CW Generator | Settings]
//
// Key mapping (SDL keyboard):
//   Space=DIT  Enter=DAH  /=Straight
//   ↑/↓  = Encoder CW/CCW  (scroll menu  |  +/-1 WPM in keyer/generator)
//   e    = Encoder short   (select / enter edit mode in settings)
//   E    = Encoder long    (back to previous screen)
//   a    = Aux short       (pause/resume in generator)
//   A    = Aux long        (home)
//   Esc  = quit

#define SDL_MAIN_HANDLED

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#include "audio_output_alsa.h"
#include "key_input_sdl.h"
#include "key_input_midi.h"

#include "paddle_ctl.h"
#include "iambic_keyer.h"
#include "morse_decoder.h"
#include "morse_trainer.h"
#include "text_generators.h"

#include "cw_textfield.hpp"
#include "screen_stack.hpp"
#include "status_bar.hpp"

// ── MIDI note mapping ─────────────────────────────────────────────────────────
static constexpr int MIDI_NOTE_DIT      = 60;
static constexpr int MIDI_NOTE_DAH      = 62;
static constexpr int MIDI_NOTE_STRAIGHT = 61;

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr lv_coord_t SCREEN_W  = LV_SDL_HOR_RES;
static constexpr lv_coord_t SCREEN_H  = LV_SDL_VER_RES;
static constexpr lv_coord_t CONTENT_Y = StatusBar::HEIGHT + 2;

// ── millis injected into CW engine ────────────────────────────────────────────
static unsigned long sim_millis()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

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
static NativeAudioOutputAlsa* s_audio = nullptr;
static NativeKeyInputSdl*     s_keys  = nullptr;
static NativeKeyInputMidi*    s_midi  = nullptr;

// ── CW engine (keyer mode) ────────────────────────────────────────────────────
static PaddleCtl*    s_paddle     = nullptr;
static IambicKeyer*  s_keyer      = nullptr;
static MorseDecoder* s_decoder    = nullptr;
static uint32_t      s_straight_t0 = 0;

// ── Trainer (generator mode) ──────────────────────────────────────────────────
static MorseTrainer*   s_trainer    = nullptr;
static TextGenerators* s_gen        = nullptr;
static std::mt19937    s_rng;
static bool            s_gen_paused = false;

// ── UI widgets updated from engine callbacks ──────────────────────────────────
static CWTextField* s_keyer_tf  = nullptr;  // set/cleared with keyer screen
static CWTextField* s_gen_tf    = nullptr;  // set/cleared with generator screen
static StatusBar*   s_active_sb = nullptr;  // status bar of the currently visible screen

// ── Echo trainer widgets (set/cleared with echo screen) ──────────────────────
static lv_obj_t*   s_echo_target_lbl = nullptr;   // phrase being played
static lv_obj_t*   s_echo_rcvd_lbl   = nullptr;   // chars received so far
static lv_obj_t*   s_echo_result_lbl = nullptr;   // "OK" / "ERR"
static std::string s_echo_typed;                   // mirrors trainer's received_phrase_

// ── Screen stack & navigation ─────────────────────────────────────────────────
static ScreenStack   s_stack;
static volatile bool s_quit = false;

// LVGL encoder indev
static lv_indev_t* s_enc_indev        = nullptr;
static int32_t     s_enc_diff         = 0;
static int         s_enc_press_frames = 0;

// Per-screen encoder groups (created inside each screen builder)
static lv_group_t* s_menu_group     = nullptr;
static lv_group_t* s_settings_group = nullptr;

// ── Forward declarations ──────────────────────────────────────────────────────
static lv_obj_t* build_main_menu();
static lv_obj_t* build_keyer_screen();
static lv_obj_t* build_generator_screen();
static lv_obj_t* build_echo_screen();
static lv_obj_t* build_settings_screen();
static void      apply_settings();

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

// ── SDL quit watcher ──────────────────────────────────────────────────────────
static int quit_watcher(void*, SDL_Event* ev)
{
    if (ev->type == SDL_QUIT) s_quit = true;
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) s_quit = true;
    return 0;
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
    // freq_hz is picked up on the next tone_on() call
    if (s_active_sb) s_active_sb->set_wpm(s_settings.wpm);
}

// ── Screen: Main Menu ─────────────────────────────────────────────────────────
static lv_obj_t* build_main_menu()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Morserino-32-NG");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93 = scroll    e = select    Esc = quit");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, SCREEN_W - 60, SCREEN_H - 100);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);

    if (s_menu_group) lv_group_del(s_menu_group);
    s_menu_group = lv_group_create();

    // Non-capturing lambda — safe to convert to lv_event_cb_t function pointer
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

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight  "
        "\xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);

    lv_coord_t tf_y = CONTENT_Y + 24;
    s_keyer_tf = new CWTextField(scr);
    lv_obj_set_pos(s_keyer_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_keyer_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

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

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "a=pause/resume  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);

    lv_coord_t tf_y = CONTENT_Y + 24;
    s_gen_tf = new CWTextField(scr);
    lv_obj_set_pos(s_gen_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_gen_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

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

    const lv_coord_t ROW_H  = (SCREEN_H - CONTENT_Y) / 3;
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

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Space=DIT  Enter=DAH  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 8, -22);

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

    const lv_coord_t LBL_X  = 16;
    const lv_coord_t CTL_X  = SCREEN_W / 2 + 20;
    const lv_coord_t ROW_H  = 50;
    const lv_coord_t START_Y = CONTENT_Y + 10;

    auto make_label = [&](const char* text, int row) {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, text);
        lv_obj_set_pos(lbl, LBL_X, START_Y + row * ROW_H + 14);
    };

    // WPM
    make_label("Speed (WPM)", 0);
    lv_obj_t* wpm_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(wpm_spn, 5, 40);
    lv_spinbox_set_digit_count(wpm_spn, 2);
    lv_spinbox_set_value(wpm_spn, s_settings.wpm);
    lv_obj_set_width(wpm_spn, 100);
    lv_obj_set_pos(wpm_spn, CTL_X, START_Y + 0 * ROW_H + 6);
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
    lv_obj_set_width(mode_dd, 150);
    lv_obj_set_pos(mode_dd, CTL_X, START_Y + 1 * ROW_H + 6);
    lv_group_add_obj(s_settings_group, mode_dd);
    lv_obj_add_event_cb(mode_dd, [](lv_event_t* e) {
        s_settings.mode_a = (lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 0u);
        apply_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Frequency
    make_label("Frequency (Hz)", 2);
    lv_obj_t* freq_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(freq_spn, 400, 900);
    lv_spinbox_set_digit_count(freq_spn, 3);
    lv_spinbox_set_step(freq_spn, 10);
    lv_spinbox_set_value(freq_spn, s_settings.freq_hz);
    lv_obj_set_width(freq_spn, 120);
    lv_obj_set_pos(freq_spn, CTL_X, START_Y + 2 * ROW_H + 6);
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
    lv_obj_set_pos(vol_spn, CTL_X, START_Y + 3 * ROW_H + 6);
    lv_group_add_obj(s_settings_group, vol_spn);
    lv_obj_add_event_cb(vol_spn, [](lv_event_t* e) {
        s_settings.volume = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
        s_audio->set_volume(s_settings.volume);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "\xe2\x86\x91/\xe2\x86\x93=navigate    e=edit value    E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    return scr;
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(void)
{
    SDL_SetMainReady();

    lv_init();
    lv_display_t* disp  = lv_sdl_window_create(LV_SDL_HOR_RES, LV_SDL_VER_RES);
    lv_indev_t*   mouse = lv_sdl_mouse_create();
    lv_indev_t*   wheel = lv_sdl_mousewheel_create();
    (void)disp; (void)mouse; (void)wheel;

    // Audio
    s_audio = new NativeAudioOutputAlsa("default");
    s_audio->begin();
    s_audio->set_volume(s_settings.volume);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    // CW engine
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_decoder = new MorseDecoder(dit_ms * 3, on_letter_decoded, sim_millis);
    s_keyer   = new IambicKeyer(dit_ms, on_play_state, sim_millis, s_settings.mode_a);
    s_paddle  = new PaddleCtl(/*debounce_ms=*/2, on_lever_state, sim_millis);

    // Text generator + trainer
    s_rng.seed(std::random_device{}());
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
        sim_millis);
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

    // Encoder indev
    s_enc_indev = lv_indev_create();
    lv_indev_set_type(s_enc_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_enc_indev, enc_read_cb);

    // Key input
    s_keys = new NativeKeyInputSdl();
    s_midi = new NativeKeyInputMidi(MIDI_NOTE_DIT, MIDI_NOTE_DAH,
                                    MIDI_NOTE_STRAIGHT, /*auto_connect=*/true);
    SDL_AddEventWatch(quit_watcher, nullptr);

    // Push initial screen
    s_stack.push(build_main_menu(),
        []() { lv_indev_set_group(s_enc_indev, s_menu_group); },
        {});

    // ── Main loop ─────────────────────────────────────────────────────────────
    Uint32 last_tick = SDL_GetTicks();

    while (!s_quit) {
        SDL_Delay(5);
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();

        auto route = [](KeyEvent ev) {
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

                case KeyEvent::PADDLE_DIT_DOWN:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO)  s_paddle->setDotPushed(true);
                    if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
                    break;
                case KeyEvent::PADDLE_DIT_UP:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO)  s_paddle->setDotPushed(false);
                    break;
                case KeyEvent::PADDLE_DAH_DOWN:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO)  s_paddle->setDashPushed(true);
                    if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
                    break;
                case KeyEvent::PADDLE_DAH_UP:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO)  s_paddle->setDashPushed(false);
                    break;

                case KeyEvent::STRAIGHT_DOWN:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO) {
                        s_straight_t0 = (uint32_t)sim_millis();
                        s_audio->tone_on(s_settings.freq_hz);
                        s_decoder->set_transmitting(true);
                        if (s_active_mode == ActiveMode::ECHO)
                            s_trainer->tame_echo_timeout();
                    }
                    break;
                case KeyEvent::STRAIGHT_UP:
                    if (s_active_mode == ActiveMode::KEYER ||
                        s_active_mode == ActiveMode::ECHO) {
                        uint32_t dur = (uint32_t)sim_millis() - s_straight_t0;
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
        };

        KeyEvent ev;
        while (s_keys->poll(ev)) route(ev);
        while (s_midi->poll(ev)) route(ev);

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
    }

    SDL_DelEventWatch(quit_watcher, nullptr);
    delete s_keys;
    delete s_midi;
    delete s_trainer;
    delete s_gen;
    delete s_paddle;
    delete s_keyer;
    delete s_decoder;
    delete s_audio;
    lv_deinit();
    SDL_Quit();
    return 0;
}
