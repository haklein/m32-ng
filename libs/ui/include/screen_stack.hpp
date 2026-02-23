#pragma once

#include <lvgl.h>
#include <functional>
#include <vector>

// Simple LVGL screen stack.
//
// push(screen)  — load screen immediately; old screen stays in the stack
// pop()         — reload the previous screen; delete the top one
// pop_all()     — pop all the way back to the root screen
//
// Screens are created by the caller (lv_obj_create(nullptr)) and owned by the
// stack after push().  When a screen is popped it is deleted with lv_obj_del().
//
// Optional on_enter / on_leave callbacks fire synchronously before loading.

class ScreenStack
{
public:
    using Cb = std::function<void()>;

    // Push a new screen.  on_enter fires before lv_scr_load; on_leave fires
    // on the current top before it is replaced.
    void push(lv_obj_t* screen, Cb on_enter = {}, Cb on_leave = {});

    // Pop the top screen and return to the one below.
    // No-op if only one screen is on the stack.
    void pop();

    // Pop all screens down to the root (index 0).
    void pop_all();

    lv_obj_t* top() const;
    bool      empty() const { return screens_.empty(); }
    size_t    size() const  { return screens_.size(); }

private:
    struct Entry {
        lv_obj_t* screen;
        Cb        on_enter;
        Cb        on_leave;
    };

    std::vector<Entry> screens_;
};
