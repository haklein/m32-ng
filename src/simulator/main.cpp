// Morserino-32-NG — Desktop Simulator
//
// Full interactive Morse trainer running on Linux/macOS/Windows.
// Build and run:  pio run -e simulator -t upload
//
// Display : LVGL 9 with SDL2 backend (LV_USE_SDL=1), 480×320 window
// Audio   : NativeAudioOutputAlsa (ALSA PCM, Blackman-Harris envelope)
// Input   : NativeKeyInputSdl (SDL keyboard) + NativeKeyInputMidi (MIDI adapter)
// CW      : PaddleCtl → IambicKeyer → MorseDecoder → CWTextField
//
// Key mapping is documented in §2.4 of the Implementation FSD.
// MIDI note mapping: MIDI_NOTE_DIT / MIDI_NOTE_DAH / MIDI_NOTE_STRAIGHT below.

#define SDL_MAIN_HANDLED   // prevent SDL from hijacking main() on Windows

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <chrono>
#include <cstdio>

#include "audio_output_alsa.h"
#include "key_input_sdl.h"
#include "key_input_midi.h"

#include "paddle_ctl.h"
#include "iambic_keyer.h"
#include "morse_decoder.h"
#include "cw_textfield.hpp"

// ── MIDI note mapping ────────────────────────────────────────────────────────
static constexpr int MIDI_NOTE_DIT      = 60;   // C4
static constexpr int MIDI_NOTE_DAH      = 62;   // D4
static constexpr int MIDI_NOTE_STRAIGHT = 61;   // C#4 (change if needed)

// ── CW speed ─────────────────────────────────────────────────────────────────
static constexpr unsigned long CW_WPM          = 15;
static constexpr unsigned long DIT_MS          = 1200 / CW_WPM;     // 80 ms
static constexpr unsigned long DECODE_THRESHOLD = DIT_MS * 3;        // 240 ms

// ── millis injected into CW engine classes ───────────────────────────────────
static unsigned long sim_millis()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

// ── Forward declarations ─────────────────────────────────────────────────────
static const char* key_event_name(KeyEvent ev);
static void        build_ui(lv_obj_t* screen);

// ── Globals ───────────────────────────────────────────────────────────────────
static NativeAudioOutputAlsa* s_audio    = nullptr;
static NativeKeyInputSdl*     s_keys     = nullptr;
static NativeKeyInputMidi*    s_midi     = nullptr;

static PaddleCtl*    s_paddle   = nullptr;
static IambicKeyer*  s_keyer    = nullptr;
static MorseDecoder* s_decoder  = nullptr;
static CWTextField*  s_tf       = nullptr;

static uint32_t      s_straight_t0 = 0;  // timestamp of STRAIGHT_DOWN
static volatile bool s_quit = false;

// ── SDL quit watcher ─────────────────────────────────────────────────────────
static int quit_watcher(void* /*unused*/, SDL_Event* ev)
{
    if (ev->type == SDL_QUIT) s_quit = true;
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) s_quit = true;
    return 0;
}

// ── CW engine callbacks ───────────────────────────────────────────────────────

// Fired by IambicKeyer when a CW element starts or ends.
static void on_play_state(PlayState state)
{
    switch (state) {
        case PLAY_STATE_DOT_ON:
        case PLAY_STATE_DASH_ON:
            s_audio->tone_on(700);
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

// Fired by PaddleCtl when the combined lever state changes.
static void on_lever_state(LeverState state)
{
    s_keyer->setLeverState(state);
}

// Fired by MorseDecoder when a character has been decoded.
static void on_letter_decoded(const std::string& letter)
{
    if (!s_tf) return;
    if (letter == " ") {
        s_tf->next_word();
    } else {
        s_tf->add_string(letter);
    }
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
    s_audio->set_volume(7);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    // CW engine — order matters: decoder before keyer before paddle
    s_decoder = new MorseDecoder(DECODE_THRESHOLD, on_letter_decoded, sim_millis);
    s_keyer   = new IambicKeyer(DIT_MS, on_play_state, sim_millis, /*mode_a=*/false);
    s_paddle  = new PaddleCtl(/*debounce_ms=*/2, on_lever_state, sim_millis);

    // Key input
    s_keys = new NativeKeyInputSdl();
    s_midi = new NativeKeyInputMidi(MIDI_NOTE_DIT, MIDI_NOTE_DAH,
                                    MIDI_NOTE_STRAIGHT, /*auto_connect=*/true);
    SDL_AddEventWatch(quit_watcher, nullptr);

    // UI (creates s_tf)
    build_ui(lv_screen_active());

    // ── Main loop ─────────────────────────────────────────────────────────────
    Uint32 last_tick = SDL_GetTicks();

    while (!s_quit) {
        SDL_Delay(5);
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();

        // Route key events to the CW pipeline
        auto route = [](KeyEvent ev) {
            switch (ev) {
                case KeyEvent::PADDLE_DIT_DOWN:  s_paddle->setDotPushed(true);  break;
                case KeyEvent::PADDLE_DIT_UP:    s_paddle->setDotPushed(false); break;
                case KeyEvent::PADDLE_DAH_DOWN:  s_paddle->setDashPushed(true); break;
                case KeyEvent::PADDLE_DAH_UP:    s_paddle->setDashPushed(false);break;
                case KeyEvent::STRAIGHT_DOWN:
                    s_straight_t0 = (uint32_t)sim_millis();
                    s_audio->tone_on(700);
                    s_decoder->set_transmitting(true);
                    break;
                case KeyEvent::STRAIGHT_UP: {
                    uint32_t dur = (uint32_t)sim_millis() - s_straight_t0;
                    s_audio->tone_off();
                    s_decoder->set_transmitting(false);
                    if (dur < DIT_MS * 2) s_decoder->append_dot();
                    else                  s_decoder->append_dash();
                    break;
                }
                default:
                    break;
            }
        };

        KeyEvent ev;
        while (s_keys->poll(ev)) route(ev);
        while (s_midi->poll(ev)) route(ev);

        // Tick CW engine state machines
        s_paddle->tick();
        s_keyer->tick();
        s_decoder->tick();
    }

    SDL_DelEventWatch(quit_watcher, nullptr);
    delete s_keys;
    delete s_midi;
    delete s_paddle;
    delete s_keyer;
    delete s_decoder;
    delete s_audio;
    lv_deinit();
    SDL_Quit();
    return 0;
}

// ── UI ────────────────────────────────────────────────────────────────────────
static void build_ui(lv_obj_t* screen)
{
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Morserino-32-NG Simulator");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* hint = lv_label_create(screen);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight\n"
        "Up/Down=Encoder  e=EncBtn  a=AuxBtn  Esc=quit");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);

    // CW text field — leave 70px at top for title+hint
    s_tf = new CWTextField(screen);
    lv_obj_set_pos(s_tf->obj(), 4, 72);
    lv_obj_set_size(s_tf->obj(), LV_SDL_HOR_RES - 8, LV_SDL_VER_RES - 76);
}

// ── Key event name (debug) ────────────────────────────────────────────────────
static const char* key_event_name(KeyEvent ev)
{
    switch (ev) {
        case KeyEvent::PADDLE_DIT_DOWN:       return "DIT DOWN";
        case KeyEvent::PADDLE_DIT_UP:         return "DIT UP";
        case KeyEvent::PADDLE_DAH_DOWN:       return "DAH DOWN";
        case KeyEvent::PADDLE_DAH_UP:         return "DAH UP";
        case KeyEvent::STRAIGHT_DOWN:         return "STRAIGHT DOWN";
        case KeyEvent::STRAIGHT_UP:           return "STRAIGHT UP";
        case KeyEvent::ENCODER_CW:            return "ENC CW";
        case KeyEvent::ENCODER_CCW:           return "ENC CCW";
        case KeyEvent::BUTTON_ENCODER_SHORT:  return "ENC SHORT";
        case KeyEvent::BUTTON_ENCODER_LONG:   return "ENC LONG";
        case KeyEvent::BUTTON_AUX_SHORT:      return "AUX SHORT";
        case KeyEvent::BUTTON_AUX_LONG:       return "AUX LONG";
        default:                              return "OTHER";
    }
}
