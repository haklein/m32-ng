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
    // Position after mode icon — will be placed dynamically
    lv_obj_align_to(wpm_label_, mode_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    wifi_label_ = lv_label_create(bar_);
    lv_label_set_text(wifi_label_, "");
    lv_obj_set_style_text_color(wifi_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(wifi_label_, f, 0);

    bat_label_ = lv_label_create(bar_);
    lv_label_set_text(bat_label_, "");
    lv_obj_set_style_text_color(bat_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(bat_label_, f, 0);
    lv_obj_align(bat_label_, LV_ALIGN_RIGHT_MID, -4, 0);

    // wifi goes left of battery
    lv_obj_align_to(wifi_label_, bat_label_, LV_ALIGN_OUT_LEFT_MID, -4, 0);
}

void StatusBar::set_mode(const char* icon)
{
    lv_label_set_text(mode_label_, icon);
    // Re-chain wpm label after mode icon
    lv_obj_align_to(wpm_label_, mode_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
}

void StatusBar::set_wpm(int wpm, int farnsworth, int effective_wpm)
{
    char buf[32];
    if (effective_wpm > 0 && effective_wpm != wpm)
        snprintf(buf, sizeof(buf), "%d/%dW", effective_wpm, wpm);
    else if (farnsworth > 0 && farnsworth < wpm)
        snprintf(buf, sizeof(buf), "%d(%d)W", wpm, farnsworth);
    else
        snprintf(buf, sizeof(buf), "%d WPM", wpm);
    lv_label_set_text(wpm_label_, buf);
    lv_obj_align_to(wpm_label_, mode_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
}

void StatusBar::set_volume(int vol)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "Vol %d", vol);
    lv_label_set_text(wpm_label_, buf);
    lv_obj_align_to(wpm_label_, mode_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
}

void StatusBar::set_scroll()
{
    lv_label_set_text(wpm_label_, LV_SYMBOL_UP LV_SYMBOL_DOWN " Scroll");
    lv_obj_align_to(wpm_label_, mode_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
}

void StatusBar::set_wifi(bool connected, bool ap_mode)
{
    if (connected) {
        lv_label_set_text(wifi_label_, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x00FF00), 0); // green
    } else if (ap_mode) {
        lv_label_set_text(wifi_label_, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0xFFAA00), 0); // orange
    } else {
        lv_label_set_text(wifi_label_, "");
    }
    // Re-align wifi left of battery
    lv_obj_align_to(wifi_label_, bat_label_, LV_ALIGN_OUT_LEFT_MID, -4, 0);
}

void StatusBar::set_battery(uint8_t percent, bool charging)
{
    if (percent == 255) {
        lv_label_set_text(bat_label_, "");
        return;
    }
    if (charging) {
        lv_label_set_text(bat_label_, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(bat_label_, lv_color_hex(0x00FF00), 0);
    } else {
        const char* sym;
        lv_color_t col;
        if (percent >= 80) {
            sym = LV_SYMBOL_BATTERY_FULL;
            col = lv_color_hex(0x00FF00);
        } else if (percent >= 60) {
            sym = LV_SYMBOL_BATTERY_3;
            col = lv_color_hex(0xAAFF00);
        } else if (percent >= 40) {
            sym = LV_SYMBOL_BATTERY_2;
            col = lv_color_hex(0xFFFF00);
        } else if (percent >= 20) {
            sym = LV_SYMBOL_BATTERY_1;
            col = lv_color_hex(0xFF8800);
        } else {
            sym = LV_SYMBOL_BATTERY_EMPTY;
            col = lv_color_hex(0xFF0000);
        }
        lv_label_set_text(bat_label_, sym);
        lv_obj_set_style_text_color(bat_label_, col, 0);
    }
    lv_obj_align(bat_label_, LV_ALIGN_RIGHT_MID, -4, 0);
    // Re-align wifi left of battery
    lv_obj_align_to(wifi_label_, bat_label_, LV_ALIGN_OUT_LEFT_MID, -4, 0);
}
