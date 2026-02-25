// src/app_ui.hpp — Shared UI logic for pocketwroom and simulator.
//
// Include requirements (must be defined by the platform TU before this header):
//   static constexpr lv_coord_t SCREEN_W;   // logical display width
//   static constexpr lv_coord_t SCREEN_H;   // logical display height
//   static unsigned long app_millis();       // monotonic millisecond counter
//
// Platform TU must also set s_audio and s_keys before calling app_ui_init().

#pragma once

#include <algorithm>
#include <string>
#include <random>

#include <lvgl.h>

#include "i_audio_output.h"
#include "i_key_input.h"
#include "paddle_ctl.h"
#include "iambic_keyer.h"
#include "morse_decoder.h"
#include "morse_trainer.h"
#include "text_generators.h"
#include "cw_textfield.hpp"
#include "screen_stack.hpp"
#include "status_bar.hpp"
#include "cw_chatbot.h"
#include "lv_font_intel.h"

static constexpr lv_coord_t CONTENT_Y = StatusBar::HEIGHT + 2;

// ── Global settings ────────────────────────────────────────────────────────
struct AppSettings {
    int      wpm          = 15;
    bool     mode_a       = false;   // false = Iambic B
    uint16_t freq_hz      = 700;
    uint8_t  volume       = 7;
    // Content
    bool     cont_words   = true;    // English words
    bool     cont_abbrevs = false;   // Ham radio abbreviations
    bool     cont_calls   = false;   // Synthetic callsigns
    bool     cont_chars   = false;   // Random character groups
    uint8_t  chars_group       = 0;   // 0=Alpha, 1=Alpha+Num, 2=All CW
    uint8_t  koch_lesson       = 0;   // 0=off, 1..N=first N Koch chars
    uint8_t  echo_max_repeats  = 3;   // 0=unlimited, else max failures before reveal
    uint8_t  chatbot_qso_depth = 1;  // 0=MINIMAL, 1=STANDARD, 2=RAGCHEW
    uint8_t  text_font_size    = 0;  // 0=Normal (20px), 1=Large (28px)
};
static AppSettings s_settings;

static const lv_font_t* cw_text_font()
{
    return s_settings.text_font_size == 0 ? &lv_font_intel_20 : &lv_font_intel_28;
}

// ── Active CW mode ─────────────────────────────────────────────────────────
enum class ActiveMode { NONE, KEYER, GENERATOR, ECHO, CHATBOT };
static ActiveMode s_active_mode = ActiveMode::NONE;

// ── HAL pointers (assigned by platform before app_ui_init) ────────────────
static IAudioOutput* s_audio = nullptr;
static IKeyInput*    s_keys  = nullptr;

// ── CW engine (keyer + echo modes) ────────────────────────────────────────
static PaddleCtl*    s_paddle      = nullptr;
static IambicKeyer*  s_keyer       = nullptr;
static MorseDecoder* s_decoder     = nullptr;
static uint32_t      s_straight_t0 = 0;

// ── Trainer (generator + echo modes) ──────────────────────────────────────
static MorseTrainer*   s_trainer    = nullptr;
static TextGenerators* s_gen        = nullptr;
static std::mt19937    s_rng;
static bool            s_gen_paused = false;

// ── Word-space timer for CW keyer ─────────────────────────────────────────
// Tracks last element END (not character decode) so the 7-dit gap is measured
// from the right point regardless of how long the next character takes to play.
static unsigned long s_keyer_last_element_end_t = 0;
static bool          s_keyer_word_pending        = false;

// ── UI widgets updated from engine callbacks ───────────────────────────────
static CWTextField* s_keyer_tf  = nullptr;
static CWTextField* s_gen_tf    = nullptr;
static StatusBar*   s_active_sb = nullptr;

// ── Echo trainer widgets (set/cleared with echo screen) ───────────────────
static lv_obj_t*   s_echo_target_lbl = nullptr;
static lv_obj_t*   s_echo_rcvd_lbl   = nullptr;
static lv_obj_t*   s_echo_result_lbl = nullptr;
static std::string s_echo_typed;
static std::string s_pending_gen_phrase;

// ── Chatbot widgets (set/cleared with chatbot screen) ─────────────────────
static CWChatbot*   s_chatbot                = nullptr;
static std::string  s_chatbot_pending_phrase;
static bool         s_chatbot_tx_active      = false;
static CWTextField* s_chatbot_bot_tf         = nullptr;
static CWTextField* s_chatbot_oper_tf        = nullptr;
static lv_obj_t*    s_chatbot_state_lbl      = nullptr;

// ── Screen stack & LVGL encoder indev ─────────────────────────────────────
static ScreenStack s_stack;
static lv_indev_t* s_enc_indev        = nullptr;
static int32_t     s_enc_diff         = 0;
static int         s_enc_press_frames = 0;
static lv_group_t* s_menu_group     = nullptr;
static lv_group_t* s_settings_group = nullptr;
static lv_group_t* s_content_group  = nullptr;

// ── Forward declarations ───────────────────────────────────────────────────
static lv_obj_t* build_main_menu();
static lv_obj_t* build_keyer_screen();
static lv_obj_t* build_generator_screen();
static lv_obj_t* build_echo_screen();
static lv_obj_t* build_chatbot_screen();
static lv_obj_t* build_settings_screen();
static lv_obj_t* build_content_screen();
static void      apply_settings();
static std::string content_phrase();
static void      route(KeyEvent ev);

// ── LVGL encoder indev callback ────────────────────────────────────────────
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

// ── CW engine callbacks ────────────────────────────────────────────────────
static void on_play_state(PlayState state)
{
    switch (state) {
        case PLAY_STATE_DOT_ON:
        case PLAY_STATE_DASH_ON:
            s_audio->tone_on(s_settings.freq_hz);
            s_decoder->set_transmitting(true);
            s_keyer_last_element_end_t = (unsigned long)app_millis();
            break;
        case PLAY_STATE_DOT_OFF:
            s_audio->tone_off();
            s_decoder->append_dot();
            s_decoder->set_transmitting(false);
            s_keyer_last_element_end_t = (unsigned long)app_millis();
            break;
        case PLAY_STATE_DASH_OFF:
            s_audio->tone_off();
            s_decoder->append_dash();
            s_decoder->set_transmitting(false);
            s_keyer_last_element_end_t = (unsigned long)app_millis();
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
        if (!s_keyer_tf || letter == " ") return;
        s_keyer_tf->add_string(letter);
        s_keyer_word_pending = true;
    } else if (s_active_mode == ActiveMode::ECHO) {
        s_trainer->symbol_received(letter);
        if (s_echo_rcvd_lbl) {
            if      (letter == "<err>") { /* <HH> resets whole word */ s_echo_typed.clear(); }
            else if (letter != " ")     { s_echo_typed += letter; }
            lv_label_set_text(s_echo_rcvd_lbl, s_echo_typed.c_str());
        }
    } else if (s_active_mode == ActiveMode::CHATBOT) {
        if (s_chatbot) s_chatbot->symbol_received(letter);
        if (s_chatbot_oper_tf && letter != " ") {
            s_chatbot_oper_tf->add_string(letter);
        }
        s_keyer_word_pending = true;
    }
}

// ── Propagate settings to all engine objects ───────────────────────────────
static void apply_settings()
{
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_keyer->setSpeedWPM((unsigned long)s_settings.wpm);
    s_keyer->setModeA(s_settings.mode_a);
    s_decoder->set_decode_threshold(dit_ms * 2);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_trainer->set_max_echo_repeats(s_settings.echo_max_repeats);
    if (s_chatbot) {
        s_chatbot->set_speed_wpm(s_settings.wpm);
        static const QSODepth depths[] = {
            QSODepth::MINIMAL, QSODepth::STANDARD, QSODepth::RAGCHEW };
        s_chatbot->set_qso_depth(depths[std::min((int)s_settings.chatbot_qso_depth, 2)]);
    }
    s_audio->set_volume(s_settings.volume);
    if (s_active_sb) s_active_sb->set_wpm(s_settings.wpm);
}

// ── Content phrase generator ───────────────────────────────────────────────
// Returns the next training phrase according to the current content settings.
static std::string content_phrase()
{
    // Koch mode: character groups using the first N Koch chars
    if (s_settings.koch_lesson > 0) {
        int n = std::min((int)s_settings.koch_lesson,
                         (int)(sizeof(KOCH_ORDER) - 1));
        return s_gen->random_chars_from_set(std::string(KOCH_ORDER, n), 5);
    }
    // Collect enabled content types
    int types[4]; int nt = 0;
    if (s_settings.cont_words)   types[nt++] = 0;
    if (s_settings.cont_abbrevs) types[nt++] = 1;
    if (s_settings.cont_calls)   types[nt++] = 2;
    if (s_settings.cont_chars)   types[nt++] = 3;
    if (nt == 0) return s_gen->random_word();   // fallback: always something
    int choice = types[std::uniform_int_distribution<int>(0, nt - 1)(s_rng)];
    static const RandomOption char_opts[] = { OPT_ALPHA, OPT_ALNUM, OPT_ALL };
    switch (choice) {
        case 0: return s_gen->random_word();
        case 1: return s_gen->random_abbrev();
        case 2: return s_gen->random_callsign();
        case 3: return s_gen->random_chars(
                    5, char_opts[std::min((int)s_settings.chars_group, 2)]);
    }
    return s_gen->random_word();
}

// ── Screen: Main Menu ──────────────────────────────────────────────────────
static lv_obj_t* build_main_menu()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Morserino-32-NG");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93 = scroll    e = select    Esc = quit");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 26);
#endif

    lv_obj_t* list = lv_list_create(scr);
#ifdef NATIVE_BUILD
    lv_obj_set_size(list, SCREEN_W - 60, SCREEN_H - 100);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);
#else
    lv_obj_set_size(list, SCREEN_W - 40, SCREEN_H - 44);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 10);
#endif

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
                s_active_mode              = ActiveMode::NONE;
                s_keyer_word_pending       = false;
                s_keyer_last_element_end_t = 0;
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
                s_pending_gen_phrase.clear();
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
        } else if (idx == 3) {
            ns = build_chatbot_screen();
            enter_cb = []() {
                s_active_mode = ActiveMode::CHATBOT;
                s_chatbot = new CWChatbot(
                    // send_cb: queue phrase for MorseTrainer to play
                    [](const std::string& text) {
                        s_chatbot_pending_phrase = text;
                        s_chatbot_tx_active = true;
                        if (s_chatbot_bot_tf)
                            s_chatbot_bot_tf->add_string(text + " ");
                        s_trainer->set_state(MorseTrainer::TrainerState::Player);
                        s_trainer->set_playing();
                    },
                    // speed_cb: update WPM
                    [](int wpm) {
                        s_settings.wpm = wpm;
                        apply_settings();
                    },
                    // event_cb
                    [](QSOEvent) {},
                    app_millis
                );
                s_chatbot->set_operator_call("W1TEST");
                s_chatbot->set_speed_wpm(s_settings.wpm);
                static const QSODepth depths[] = {
                    QSODepth::MINIMAL, QSODepth::STANDARD, QSODepth::RAGCHEW };
                s_chatbot->set_qso_depth(
                    depths[std::min((int)s_settings.chatbot_qso_depth, 2)]);
                s_chatbot->set_rng_seed((unsigned int)app_millis());
                s_chatbot->start();
                lv_indev_set_group(s_enc_indev, nullptr);
            };
            leave_cb = []() {
                s_active_mode = ActiveMode::NONE;
                s_trainer->set_idle();
                s_audio->tone_off();
                delete s_chatbot; s_chatbot = nullptr;
                s_chatbot_pending_phrase.clear();
                s_chatbot_tx_active = false;
                delete s_chatbot_bot_tf;  s_chatbot_bot_tf  = nullptr;
                delete s_chatbot_oper_tf; s_chatbot_oper_tf = nullptr;
                s_chatbot_state_lbl = nullptr;
                delete s_active_sb; s_active_sb = nullptr;
            };
        } else if (idx == 4) {
            ns = build_content_screen();
            enter_cb = []() { lv_indev_set_group(s_enc_indev, s_content_group); };
            leave_cb = []() { delete s_active_sb; s_active_sb = nullptr; };
        } else {
            ns = build_settings_screen();
            enter_cb = []() { lv_indev_set_group(s_enc_indev, s_settings_group); };
            leave_cb = []() { delete s_active_sb; s_active_sb = nullptr; };
        }
        s_stack.push(ns, std::move(enter_cb), std::move(leave_cb));
    };

    static const struct { const char* icon; const char* label; } items[] = {
        { LV_SYMBOL_AUDIO,    "CW Keyer"      },
        { LV_SYMBOL_PLAY,     "CW Generator"  },
        { LV_SYMBOL_LOOP,     "Echo Trainer"  },
        { LV_SYMBOL_CALL,     "QSO Chatbot"   },
        { LV_SYMBOL_LIST,     "Content"       },
        { LV_SYMBOL_SETTINGS, "Settings"      },
    };
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* btn = lv_list_add_button(list, items[i].icon, items[i].label);
        lv_group_add_obj(s_menu_group, btn);
        lv_obj_add_event_cb(btn, push_screen, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    return scr;
}

// ── Screen: CW Keyer ──────────────────────────────────────────────────────
static lv_obj_t* build_keyer_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("CW Keyer");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight  "
        "\xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);
    lv_coord_t tf_y = CONTENT_Y + 24;
#else
    lv_coord_t tf_y = CONTENT_Y + 2;
#endif

    s_keyer_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_keyer_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_keyer_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

    return scr;
}

// ── Screen: CW Generator ──────────────────────────────────────────────────
static lv_obj_t* build_generator_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("CW Generator");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "a=pause/resume  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);
    lv_coord_t tf_y = CONTENT_Y + 24;
#else
    lv_coord_t tf_y = CONTENT_Y + 2;
#endif

    s_gen_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_gen_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_gen_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

    return scr;
}

// ── Screen: Echo Trainer ──────────────────────────────────────────────────
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
    lv_label_set_text(s_echo_target_lbl, "?");
    lv_obj_set_style_text_font(s_echo_target_lbl, cw_text_font(), 0);
    lv_obj_set_pos(s_echo_target_lbl, 60, ROW1_Y + 6);
    lv_obj_set_width(s_echo_target_lbl, SCREEN_W - 68);

    lv_obj_t* l2 = lv_label_create(scr);
    lv_label_set_text(l2, "Rcvd:");
    lv_obj_set_pos(l2, 8, ROW2_Y + 8);

    s_echo_rcvd_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_rcvd_lbl, "");
    lv_obj_set_style_text_font(s_echo_rcvd_lbl, cw_text_font(), 0);
    lv_obj_set_pos(s_echo_rcvd_lbl, 60, ROW2_Y + 6);
    lv_obj_set_width(s_echo_rcvd_lbl, SCREEN_W - 68);

    s_echo_result_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_result_lbl, "");
    lv_obj_align(s_echo_result_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Space=DIT  Enter=DAH  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 8, -22);
#endif

    return scr;
}

// ── Screen: QSO Chatbot ──────────────────────────────────────────────────
static lv_obj_t* build_chatbot_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("QSO Chatbot");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight  "
        "\xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, CONTENT_Y + 2);
    lv_coord_t base_y = CONTENT_Y + 24;
#else
    lv_coord_t base_y = CONTENT_Y + 2;
#endif

    // Bot label + text field (upper half)
    lv_obj_t* bot_lbl = lv_label_create(scr);
    lv_label_set_text(bot_lbl, "Bot:");
    lv_obj_set_pos(bot_lbl, 4, base_y);

    lv_coord_t mid_y = (SCREEN_H - base_y) / 2 + base_y;
    s_chatbot_bot_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_chatbot_bot_tf->obj(), 4, base_y + 16);
    lv_obj_set_size(s_chatbot_bot_tf->obj(), SCREEN_W - 8, mid_y - base_y - 22);

    // Operator label + text field (lower half)
    lv_obj_t* op_lbl = lv_label_create(scr);
    lv_label_set_text(op_lbl, "You:");
    lv_obj_set_pos(op_lbl, 4, mid_y);

    s_chatbot_oper_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_chatbot_oper_tf->obj(), 4, mid_y + 16);
    lv_obj_set_size(s_chatbot_oper_tf->obj(), SCREEN_W - 8, SCREEN_H - mid_y - 40);

    // State label at bottom
    s_chatbot_state_lbl = lv_label_create(scr);
    lv_label_set_text(s_chatbot_state_lbl, "[IDLE]");
    lv_obj_align(s_chatbot_state_lbl, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    return scr;
}

// ── Screen: Settings ──────────────────────────────────────────────────────
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

    const bool compact = (SCREEN_H <= 200);
    const lv_coord_t PAD = compact ? 4 : 8;

    // Scrollable container below the status bar
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_pos(cont, 0, CONTENT_Y);
    lv_obj_set_size(cont, SCREEN_W, SCREEN_H - CONTENT_Y);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, PAD, 0);
    lv_obj_set_style_pad_all(cont, PAD, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    // Helper: create a label + control row
    auto make_row = [&](const char* label_text) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(cont);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label_text);
        return row;
    };

    // WPM
    {
        lv_obj_t* row = make_row("Speed (WPM)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 5, 40);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.wpm);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.wpm = (int)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Keyer mode
    {
        lv_obj_t* row = make_row("Keyer Mode");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Iambic A\nIambic B");
        lv_dropdown_set_selected(dd, s_settings.mode_a ? 0u : 1u);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.mode_a = (lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 0u);
            apply_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Frequency
    {
        lv_obj_t* row = make_row("Freq (Hz)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 400, 900);
        lv_spinbox_set_digit_count(spn, 3);
        lv_spinbox_set_step(spn, 10);
        lv_spinbox_set_value(spn, s_settings.freq_hz);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.freq_hz = (uint16_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Volume
    {
        lv_obj_t* row = make_row("Volume (0-10)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 10);
        lv_spinbox_set_digit_count(spn, 1);
        lv_spinbox_set_value(spn, s_settings.volume);
        lv_obj_set_width(spn, 80);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.volume = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            s_audio->set_volume(s_settings.volume);
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Text Size
    {
        lv_obj_t* row = make_row("Text Size");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Normal\nLarge");
        lv_dropdown_set_selected(dd, s_settings.text_font_size);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.text_font_size =
                (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Echo max repeats
    {
        lv_obj_t* row = make_row("Echo rpt (0=\xe2\x88\x9e)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 9);
        lv_spinbox_set_digit_count(spn, 1);
        lv_spinbox_set_value(spn, s_settings.echo_max_repeats);
        lv_obj_set_width(spn, 80);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.echo_max_repeats =
                (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // QSO depth
    {
        lv_obj_t* row = make_row("QSO Depth");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Minimal\nStandard\nRagchew");
        lv_dropdown_set_selected(dd, s_settings.chatbot_qso_depth);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.chatbot_qso_depth =
                (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
            apply_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93=navigate    e=edit value    E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
#endif

    return scr;
}

// ── Screen: Content ───────────────────────────────────────────────────────
static lv_obj_t* build_content_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr);
    sb->set_mode("Content");
    sb->set_wpm(s_settings.wpm);
    s_active_sb = sb;

    if (s_content_group) lv_group_del(s_content_group);
    s_content_group = lv_group_create();

    const bool compact       = (SCREEN_H <= 200);
    const lv_coord_t ROW_H   = compact ? 36 : 50;
    const lv_coord_t START_Y = CONTENT_Y + (compact ? 4 : 10);
    const lv_coord_t COL2_X  = compact ? SCREEN_W / 2 : SCREEN_W / 2 + 20;
    const lv_coord_t LBL_X   = compact ? 8 : 16;
    const lv_coord_t LBL_OFF = compact ? 8 : 14;
    const lv_coord_t CTL_OFF = compact ? 4 : 8;

    auto make_cb = [&](const char* text, bool checked,
                       lv_coord_t x, lv_coord_t y) -> lv_obj_t* {
        lv_obj_t* cb = lv_checkbox_create(scr);
        lv_checkbox_set_text(cb, text);
        if (checked) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_set_pos(cb, x, y);
        lv_group_add_obj(s_content_group, cb);
        return cb;
    };

    // Row 0: Words | Abbrevs
    lv_obj_t* cb_words = make_cb("Words",     s_settings.cont_words,
                                 LBL_X, START_Y + 0 * ROW_H);
    lv_obj_t* cb_abbr  = make_cb("Abbrevs",   s_settings.cont_abbrevs,
                                 COL2_X, START_Y + 0 * ROW_H);
    // Row 1: Callsigns | Chars
    lv_obj_t* cb_calls = make_cb("Callsigns", s_settings.cont_calls,
                                 LBL_X, START_Y + 1 * ROW_H);
    lv_obj_t* cb_chars = make_cb("Chars",     s_settings.cont_chars,
                                 COL2_X, START_Y + 1 * ROW_H);

    lv_obj_add_event_cb(cb_words, [](lv_event_t* e) {
        s_settings.cont_words = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_abbr, [](lv_event_t* e) {
        s_settings.cont_abbrevs = (lv_obj_get_state(lv_event_get_target_obj(e))
                                   & LV_STATE_CHECKED) != 0;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_calls, [](lv_event_t* e) {
        s_settings.cont_calls = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_chars, [](lv_event_t* e) {
        s_settings.cont_chars = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Row 2: Chars group dropdown  |  Koch spinbox
    const lv_coord_t R2_Y = START_Y + 2 * ROW_H;

    lv_obj_t* cg_lbl = lv_label_create(scr);
    lv_label_set_text(cg_lbl, "Chars:");
    lv_obj_set_pos(cg_lbl, LBL_X, R2_Y + LBL_OFF);

    lv_obj_t* cg_dd = lv_dropdown_create(scr);
    lv_dropdown_set_options(cg_dd, "Alpha\nAlpha+Num\nAll CW");
    lv_dropdown_set_selected(cg_dd, s_settings.chars_group);
    lv_obj_set_width(cg_dd, compact ? 110 : 140);
    lv_obj_set_pos(cg_dd, LBL_X + (compact ? 48 : 60), R2_Y + CTL_OFF);
    lv_group_add_obj(s_content_group, cg_dd);
    lv_obj_add_event_cb(cg_dd, [](lv_event_t* e) {
        s_settings.chars_group = (uint8_t)lv_dropdown_get_selected(
            lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* kl_lbl = lv_label_create(scr);
    lv_label_set_text(kl_lbl, compact ? "Koch:" : "Koch (0=off):");
    lv_obj_set_pos(kl_lbl, COL2_X, R2_Y + LBL_OFF);

    lv_obj_t* koch_spn = lv_spinbox_create(scr);
    lv_spinbox_set_range(koch_spn, 0, (int32_t)(sizeof(KOCH_ORDER) - 1));
    lv_spinbox_set_digit_count(koch_spn, 2);
    lv_spinbox_set_value(koch_spn, s_settings.koch_lesson);
    lv_obj_set_width(koch_spn, compact ? 80 : 100);
    lv_obj_set_pos(koch_spn, COL2_X + (compact ? 50 : 110), R2_Y + CTL_OFF);
    lv_group_add_obj(s_content_group, koch_spn);
    lv_obj_add_event_cb(koch_spn, [](lv_event_t* e) {
        s_settings.koch_lesson = (uint8_t)lv_spinbox_get_value(
            lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93=navigate    e=toggle/edit    E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
#endif

    return scr;
}

// ── Key event router ───────────────────────────────────────────────────────
static void route(KeyEvent ev)
{
    switch (ev) {
        case KeyEvent::ENCODER_CW:
            s_enc_diff++;
            if (s_active_mode == ActiveMode::KEYER  ||
                s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_settings.wpm = std::min(s_settings.wpm + 1, 40);
                apply_settings();
            }
            break;
        case KeyEvent::ENCODER_CCW:
            s_enc_diff--;
            if (s_active_mode == ActiveMode::KEYER  ||
                s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_settings.wpm = std::max(s_settings.wpm - 1, 5);
                apply_settings();
            }
            break;

        case KeyEvent::BUTTON_ENCODER_SHORT:
            if (s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_gen_paused = !s_gen_paused;
                if (s_gen_paused) s_trainer->set_idle();
                else              s_trainer->set_playing();
            } else {
                s_enc_press_frames = 2;
            }
            break;
        case KeyEvent::BUTTON_ENCODER_LONG:
            s_stack.pop();
            if (s_stack.size() == 1)
                lv_indev_set_group(s_enc_indev, s_menu_group);
            break;

        case KeyEvent::BUTTON_AUX_SHORT:
            if (s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
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
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT)  s_paddle->setDotPushed(true);
            if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::PADDLE_DIT_UP:
        case KeyEvent::TOUCH_LEFT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT)  s_paddle->setDotPushed(false);
            break;
        case KeyEvent::PADDLE_DAH_DOWN:
        case KeyEvent::TOUCH_RIGHT_DOWN:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT)  s_paddle->setDashPushed(true);
            if (s_active_mode == ActiveMode::ECHO)  s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::PADDLE_DAH_UP:
        case KeyEvent::TOUCH_RIGHT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT)  s_paddle->setDashPushed(false);
            break;

        case KeyEvent::STRAIGHT_DOWN:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_straight_t0 = (uint32_t)app_millis();
                s_audio->tone_on(s_settings.freq_hz);
                s_decoder->set_transmitting(true);
                s_keyer_last_element_end_t = (unsigned long)app_millis();
                if (s_active_mode == ActiveMode::ECHO)
                    s_trainer->tame_echo_timeout();
            }
            break;
        case KeyEvent::STRAIGHT_UP:
            if (s_active_mode == ActiveMode::KEYER ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                uint32_t dur = (uint32_t)app_millis() - s_straight_t0;
                unsigned long dit = 1200u / (unsigned long)s_settings.wpm;
                s_audio->tone_off();
                s_decoder->set_transmitting(false);
                if (dur < dit * 2) s_decoder->append_dot();
                else               s_decoder->append_dash();
                if (s_active_mode == ActiveMode::KEYER ||
                    s_active_mode == ActiveMode::CHATBOT)
                    s_keyer_last_element_end_t = (unsigned long)app_millis();
            }
            break;

        default:
            break;
    }
}

// ── Shared CW engine + trainer + LVGL init ────────────────────────────────
// Call after s_audio and s_keys are assigned.
static void app_ui_init(uint32_t rng_seed)
{
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_rng.seed(rng_seed);
    s_gen     = new TextGenerators(s_rng);
    s_decoder = new MorseDecoder(dit_ms * 2, on_letter_decoded, app_millis);
    s_keyer   = new IambicKeyer(dit_ms, on_play_state, app_millis, s_settings.mode_a);
    s_paddle  = new PaddleCtl(/*debounce_ms=*/2, on_lever_state, app_millis);

    s_trainer = new MorseTrainer(
        [](bool on) {
            if (on) s_audio->tone_on(s_settings.freq_hz);
            else    s_audio->tone_off();
        },
        []() -> std::string {
            // Chatbot: return pending phrase, or empty to go Idle.
            // Signal tx-done here (before trainer goes Idle) to avoid
            // the 3-second ADVANCE_PHRASE_DELAY latency.
            if (s_active_mode == ActiveMode::CHATBOT) {
                if (!s_chatbot_pending_phrase.empty()) {
                    std::string p = s_chatbot_pending_phrase;
                    s_chatbot_pending_phrase.clear();
                    return p;
                }
                s_chatbot_tx_active = false;
                return std::string();
            }
            std::string phrase = content_phrase();
            // Generator: deferred — show the just-finished word, queue the new one
            if (s_gen_tf) {
                if (!s_pending_gen_phrase.empty())
                    s_gen_tf->add_string(s_pending_gen_phrase + " ");
                s_pending_gen_phrase = phrase;
            }
            // Echo: reset for new round; hide target until user echoes it back
            if (s_echo_target_lbl) {
                s_echo_typed.clear();
                if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
                if (s_echo_result_lbl) lv_label_set_text(s_echo_result_lbl, "");
                lv_label_set_text(s_echo_target_lbl, "?");
            }
            return phrase;
        },
        app_millis);
    s_trainer->set_state(MorseTrainer::TrainerState::Player);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_trainer->set_echo_result_fn([](const std::string& phrase, bool success) {
        // Only reveal on success; ERR keeps "?" so the word can be replayed blind
        if (success && s_echo_target_lbl)
            lv_label_set_text(s_echo_target_lbl, phrase.c_str());
        // Clear received display so the next round starts clean
        s_echo_typed.clear();
        if (s_echo_rcvd_lbl) lv_label_set_text(s_echo_rcvd_lbl, "");
        if (s_echo_result_lbl) {
            lv_label_set_text(s_echo_result_lbl, success ? "OK" : "ERR");
            lv_obj_set_style_text_color(s_echo_result_lbl,
                success ? lv_palette_main(LV_PALETTE_GREEN)
                        : lv_palette_main(LV_PALETTE_RED), 0);
        }
    });
    s_trainer->set_echo_reveal_fn([](const std::string& phrase) {
        // Max repeats exhausted — show correct phrase and "MISS"
        if (s_echo_target_lbl) lv_label_set_text(s_echo_target_lbl, phrase.c_str());
        s_echo_typed.clear();
        if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
        if (s_echo_result_lbl) {
            lv_label_set_text(s_echo_result_lbl, "MISS");
            lv_obj_set_style_text_color(s_echo_result_lbl,
                lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
    });

    s_enc_indev = lv_indev_create();
    lv_indev_set_type(s_enc_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_enc_indev, enc_read_cb);

    s_stack.push(build_main_menu(),
        []() {
            lv_indev_set_group(s_enc_indev, s_menu_group);
            // Apply focus-key state so the first item is highlighted
            // immediately.  Normally LVGL only sets this during encoder
            // indev processing, so without actual input the highlight
            // would be missing on the first frame.
            lv_obj_t* f = lv_group_get_focused(s_menu_group);
            if (f) lv_obj_add_state(f, LV_STATE_FOCUS_KEY);
        },
        {});
}

// ── CW engine + LVGL tick dispatch ────────────────────────────────────────
// Call every loop iteration after polling and routing key events.
static void app_ui_tick()
{
    if (s_active_mode == ActiveMode::KEYER ||
        s_active_mode == ActiveMode::ECHO ||
        s_active_mode == ActiveMode::CHATBOT) {
        s_paddle->tick();
        s_keyer->tick();
        s_decoder->tick();
    }
    // Word-space: 7 dit after the last element ended (standard CW word space).
    // Measuring from element end (not character decode) ensures the timer can't
    // fire during the playback of a longer next character (e.g. a dah).
    if (s_keyer_word_pending) {
        unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
        if ((unsigned long)app_millis() - s_keyer_last_element_end_t > 7 * dit_ms) {
            if (s_active_mode == ActiveMode::KEYER && s_keyer_tf)
                s_keyer_tf->add_string(" ");
            if (s_active_mode == ActiveMode::CHATBOT && s_chatbot) {
                s_chatbot->symbol_received(" ");
                if (s_chatbot_oper_tf) s_chatbot_oper_tf->add_string(" ");
            }
            s_keyer_word_pending = false;
        }
    }
    if (s_active_mode == ActiveMode::GENERATOR ||
        s_active_mode == ActiveMode::ECHO) {
        s_trainer->tick();
    }
    // Chatbot: tick trainer (for CW playback) + chatbot state machine
    if (s_active_mode == ActiveMode::CHATBOT) {
        s_trainer->tick();
        if (s_chatbot) {
            // Detect transmission complete: phrase_cb returned empty
            // (sets s_chatbot_tx_active = false).  Must fire BEFORE chatbot
            // tick so the chatbot sees transmitting_ = false immediately.
            if (!s_chatbot_tx_active && s_chatbot->is_transmitting()) {
                s_chatbot->transmission_complete();
            }
            s_chatbot->tick();
            // Update state label
            if (s_chatbot_state_lbl) {
                static const char* STATE_NAMES[] = {
                    "IDLE", "BOT CQ", "WAIT CQ", "ANS CQ",
                    "EXCHANGE", "WAIT EXCH", "TOPICS", "CLOSING"
                };
                int si = (int)s_chatbot->state();
                char buf[48];
                snprintf(buf, sizeof(buf), "[%s] %s",
                         STATE_NAMES[si], s_chatbot->bot_callsign().c_str());
                lv_label_set_text(s_chatbot_state_lbl, buf);
            }
        }
    }
    lv_timer_handler();
}
