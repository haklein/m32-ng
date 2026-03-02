#include "status_bar.hpp"
#include <cstdio>

static constexpr lv_coord_t PAD = 2;

StatusBar::StatusBar(lv_obj_t* parent, const lv_font_t* font)
{
    const lv_font_t* f = font ? font : lv_font_get_default();
    height_ = lv_font_get_line_height(f) + PAD * 2;

    bar_ = lv_obj_create(parent);
    lv_obj_set_size(bar_, LV_PCT(100), height_);
    lv_obj_set_pos(bar_, 0, 0);
    lv_obj_set_style_pad_all(bar_, PAD, 0);
    lv_obj_set_style_radius(bar_, 0, 0);
    lv_obj_set_style_border_width(bar_, 0, 0);
    lv_obj_set_style_bg_color(bar_, lv_palette_darken(LV_PALETTE_BLUE_GREY, 3), 0);
    lv_obj_clear_flag(bar_, LV_OBJ_FLAG_SCROLLABLE);

    mode_label_ = lv_label_create(bar_);
    lv_label_set_text(mode_label_, "");
    lv_obj_set_style_text_color(mode_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(mode_label_, f, 0);
    lv_obj_align(mode_label_, LV_ALIGN_LEFT_MID, 4, 0);

    wpm_label_ = lv_label_create(bar_);
    lv_label_set_text(wpm_label_, "");
    lv_obj_set_style_text_color(wpm_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(wpm_label_, f, 0);
    // Reserve space for battery label ("100%" ≈ 3× font height + margin).
    lv_coord_t bat_reserve = lv_font_get_line_height(f) * 3 + 8;
    lv_obj_align(wpm_label_, LV_ALIGN_RIGHT_MID, -bat_reserve, 0);

    bat_label_ = lv_label_create(bar_);
    lv_label_set_text(bat_label_, "");
    lv_obj_set_style_text_color(bat_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(bat_label_, f, 0);
    lv_obj_align(bat_label_, LV_ALIGN_RIGHT_MID, -4, 0);
}

void StatusBar::set_mode(const char* name)
{
    lv_label_set_text(mode_label_, name);
}

void StatusBar::set_wpm(int wpm)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d WPM", wpm);
    lv_label_set_text(wpm_label_, buf);
}

void StatusBar::set_volume(int vol)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "Vol %d", vol);
    lv_label_set_text(wpm_label_, buf);
}

void StatusBar::set_scroll()
{
    lv_label_set_text(wpm_label_, LV_SYMBOL_UP LV_SYMBOL_DOWN " Scroll");
}

void StatusBar::set_battery(uint8_t percent, bool charging)
{
    if (percent == 255) {
        lv_label_set_text(bat_label_, "");
        return;
    }
    char buf[8];
    if (charging)
        snprintf(buf, sizeof(buf), "CHG");
    else
        snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(bat_label_, buf);
}
