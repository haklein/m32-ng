#include "screen_stack.hpp"

void ScreenStack::push(lv_obj_t* screen, Cb on_enter, Cb on_leave)
{
    if (!screens_.empty() && screens_.back().on_leave)
        screens_.back().on_leave();

    screens_.push_back({screen, std::move(on_enter), std::move(on_leave)});

    if (screens_.back().on_enter)
        screens_.back().on_enter();

    lv_scr_load(screen);
}

void ScreenStack::pop()
{
    if (screens_.size() <= 1)
        return;

    if (screens_.back().on_leave)
        screens_.back().on_leave();

    lv_obj_t* dying = screens_.back().screen;
    screens_.pop_back();

    if (screens_.back().on_enter)
        screens_.back().on_enter();

    lv_scr_load(screens_.back().screen);
    lv_obj_del(dying);
}

void ScreenStack::pop_all()
{
    while (screens_.size() > 1)
        pop();
}

lv_obj_t* ScreenStack::top() const
{
    return screens_.empty() ? nullptr : screens_.back().screen;
}
