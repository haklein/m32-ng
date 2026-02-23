#pragma once

#ifdef NATIVE_BUILD

// Native HAL: key/paddle input driven by SDL2 keyboard events.
//
// Observes SDL events via SDL_AddEventWatch without consuming them, so LVGL's
// own SDL driver continues to receive all events (mouse, window, etc.).
//
// Key mapping (see also §2.4 of Implementation FSD):
//
//   Space        -> PADDLE_DIT_DOWN / PADDLE_DIT_UP
//   Return       -> PADDLE_DAH_DOWN / PADDLE_DAH_UP
//   /            -> STRAIGHT_DOWN   / STRAIGHT_UP
//   Arrow Up     -> ENCODER_CW
//   Arrow Down   -> ENCODER_CCW
//   e            -> BUTTON_ENCODER_SHORT
//   E (shift+e)  -> BUTTON_ENCODER_LONG
//   a            -> BUTTON_AUX_SHORT
//   A (shift+a)  -> BUTTON_AUX_LONG
//
// poll() and wait() are thread-safe; the SDL watcher pushes events from the
// thread that calls lv_timer_handler().

#include "../interfaces/i_key_input.h"

#include <SDL2/SDL.h>
#include <condition_variable>
#include <mutex>
#include <queue>

class NativeKeyInputSdl : public IKeyInput
{
public:
    NativeKeyInputSdl();
    ~NativeKeyInputSdl() override;

    // Returns true and fills `out` if a keyer event is pending; non-blocking.
    bool poll(KeyEvent& out) override;

    // Blocks until a keyer event arrives or timeout_ms elapses.
    bool wait(KeyEvent& out, uint32_t timeout_ms) override;

private:
    // SDL event watcher callback — called by SDL_PollEvent without consuming
    // the event, so LVGL's SDL driver still processes it normally.
    static int sdl_event_watcher(void* userdata, SDL_Event* event);
    void handle_sdl_event(const SDL_Event* event);
    void push(KeyEvent ev);

    std::queue<KeyEvent>    queue_;
    std::mutex              queue_mutex_;
    std::condition_variable queue_cv_;
};

#endif // NATIVE_BUILD
