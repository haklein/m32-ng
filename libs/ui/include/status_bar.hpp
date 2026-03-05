#pragma once

#include <cstdint>
#include <lvgl.h>

// Thin bar pinned to the top of a screen.
//
// Height adapts to the supplied font (default: LVGL default font → 20 px).

class StatusBar
{
public:
    // parent is typically the screen object.
    // font: if non-null, applied to all labels and used to compute bar height.
    explicit StatusBar(lv_obj_t* parent, const lv_font_t* font = nullptr);

    void set_mode(const char* icon);
    void set_wpm(int wpm, int farnsworth = 0, int effective_wpm = 0);
    void set_volume(int vol);
    void set_scroll();
    void set_battery(uint8_t percent, bool charging);
    void set_wifi(bool connected, bool ap_mode = false);

    lv_obj_t* obj() const { return bar_; }
    lv_coord_t height() const { return height_; }

private:
    lv_obj_t*  bar_;
    lv_obj_t*  mode_label_;
    lv_obj_t*  wpm_label_;
    lv_obj_t*  wifi_label_;
    lv_obj_t*  bat_label_;
    lv_coord_t height_;
};
