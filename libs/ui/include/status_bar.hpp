#pragma once

#include <lvgl.h>

// Thin 20 px bar pinned to the top of a screen.
//
// Create one per screen (or share a single bar and reparent it — simpler to
// just create one per screen since screens are short-lived).
//
// Shows:  <mode-name>          WPM: <nn>

class StatusBar
{
public:
    static constexpr lv_coord_t HEIGHT = 20;

    // parent is typically the screen object.
    explicit StatusBar(lv_obj_t* parent);

    void set_mode(const char* name);
    void set_wpm(int wpm);
    void set_volume(int vol);

    lv_obj_t* obj() const { return bar_; }

private:
    lv_obj_t* bar_;
    lv_obj_t* mode_label_;
    lv_obj_t* wpm_label_;
};
