// Morserino-32-NG — Desktop Simulator
//
// Key mapping (SDL keyboard):
//   Space=DIT  Enter=DAH  /=Straight
//   ↑/↓  = Encoder CW/CCW  (scroll menu  |  +/-1 WPM in keyer/generator/echo)
//   e    = Encoder short   (select / enter edit mode in settings)
//   E    = Encoder long    (back to previous screen)
//   a    = Aux short       (pause/resume in generator)
//   A    = Aux long        (home)
//   Esc  = quit

#define SDL_MAIN_HANDLED

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <chrono>
#include <random>

#include "audio_output_alsa.h"
#include "key_input_sdl.h"
#include "key_input_midi.h"

// ── MIDI note mapping ─────────────────────────────────────────────────────
static constexpr int MIDI_NOTE_DIT      = 60;
static constexpr int MIDI_NOTE_DAH      = 62;
static constexpr int MIDI_NOTE_STRAIGHT = 61;

// ── Layout constants (passed to app_ui.hpp) ───────────────────────────────
static constexpr lv_coord_t SCREEN_W = LV_SDL_HOR_RES;
static constexpr lv_coord_t SCREEN_H = LV_SDL_VER_RES;

static unsigned long app_millis()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

#include "../app_ui.hpp"

// ── Simulator-only state ──────────────────────────────────────────────────
static NativeKeyInputMidi* s_midi = nullptr;
static volatile bool       s_quit = false;

static int quit_watcher(void*, SDL_Event* ev)
{
    if (ev->type == SDL_QUIT) s_quit = true;
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) s_quit = true;
    return 0;
}

// ── Entry point ───────────────────────────────────────────────────────────
int main(void)
{
    SDL_SetMainReady();
    lv_init();
    lv_display_t* disp  = lv_sdl_window_create(LV_SDL_HOR_RES, LV_SDL_VER_RES);
    lv_indev_t*   mouse = lv_sdl_mouse_create();
    lv_indev_t*   wheel = lv_sdl_mousewheel_create();
    (void)disp; (void)mouse; (void)wheel;

    s_audio = new NativeAudioOutputAlsa("default");
    s_audio->begin();
    s_audio->set_volume(s_settings.volume);
    s_audio->set_adsr(0.005f, 0.0f, 1.0f, 0.005f);

    s_keys = new NativeKeyInputSdl();
    s_midi = new NativeKeyInputMidi(MIDI_NOTE_DIT, MIDI_NOTE_DAH,
                                    MIDI_NOTE_STRAIGHT, /*auto_connect=*/true);
    SDL_AddEventWatch(quit_watcher, nullptr);

    // StraightKeyer polls via s_straight_key_state (set from route() events).
    s_read_straight_key = []() -> bool { return s_straight_key_state; };

    app_ui_init(std::random_device{}());

    Uint32 last_tick = SDL_GetTicks();
    while (!s_quit) {
        SDL_Delay(1);
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;

        KeyEvent ev;
        while (s_keys->poll(ev)) route(ev);
        while (s_midi->poll(ev)) route(ev);

        app_ui_tick();
    }

    SDL_DelEventWatch(quit_watcher, nullptr);
    delete s_keys;
    delete s_midi;
    delete s_trainer;
    delete s_gen;
    delete s_paddle;
    delete s_keyer;
    delete s_straight_keyer;
    delete s_decoder;
    delete s_audio;
    lv_deinit();
    SDL_Quit();
    return 0;
}
