#ifdef NATIVE_BUILD

#include "key_input_sdl.h"

#include <chrono>

NativeKeyInputSdl::NativeKeyInputSdl()
{
    SDL_AddEventWatch(sdl_event_watcher, this);
}

NativeKeyInputSdl::~NativeKeyInputSdl()
{
    SDL_DelEventWatch(sdl_event_watcher, this);
}

bool NativeKeyInputSdl::poll(KeyEvent& out)
{
    std::lock_guard<std::mutex> lk(queue_mutex_);
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop();
    return true;
}

bool NativeKeyInputSdl::wait(KeyEvent& out, uint32_t timeout_ms)
{
    std::unique_lock<std::mutex> lk(queue_mutex_);
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
    while (queue_.empty()) {
        if (queue_cv_.wait_until(lk, deadline) == std::cv_status::timeout)
            return false;
    }
    out = queue_.front();
    queue_.pop();
    return true;
}

// static
int NativeKeyInputSdl::sdl_event_watcher(void* userdata, SDL_Event* event)
{
    static_cast<NativeKeyInputSdl*>(userdata)->handle_sdl_event(event);
    return 0;   // keep the event in the queue for LVGL / others
}

void NativeKeyInputSdl::handle_sdl_event(const SDL_Event* event)
{
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) return;

    const bool down = (event->type == SDL_KEYDOWN);
    const SDL_Keycode sym = event->key.keysym.sym;

    switch (sym) {
        case SDLK_SPACE:
            push(down ? KeyEvent::TOUCH_LEFT_DOWN : KeyEvent::TOUCH_LEFT_UP);
            break;
        case SDLK_RETURN:
            push(down ? KeyEvent::TOUCH_RIGHT_DOWN : KeyEvent::TOUCH_RIGHT_UP);
            break;
        case SDLK_SLASH:
            push(down ? KeyEvent::STRAIGHT_DOWN : KeyEvent::STRAIGHT_UP);
            break;
        case SDLK_UP:
            if (down) push(KeyEvent::ENCODER_CW);
            break;
        case SDLK_DOWN:
            if (down) push(KeyEvent::ENCODER_CCW);
            break;
        case SDLK_e:
            if (down) {
                // Shift+E = long press; plain E = short press
                if (event->key.keysym.mod & KMOD_SHIFT)
                    push(KeyEvent::BUTTON_ENCODER_LONG);
                else
                    push(KeyEvent::BUTTON_ENCODER_SHORT);
            }
            break;
        case SDLK_a:
            if (down) {
                // Shift+A = long press; plain A = short press
                if (event->key.keysym.mod & KMOD_SHIFT)
                    push(KeyEvent::BUTTON_AUX_LONG);
                else
                    push(KeyEvent::BUTTON_AUX_SHORT);
            }
            break;
        default:
            break;
    }
}

void NativeKeyInputSdl::push(KeyEvent ev)
{
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        queue_.push(ev);
    }
    queue_cv_.notify_one();
}

#endif // NATIVE_BUILD
