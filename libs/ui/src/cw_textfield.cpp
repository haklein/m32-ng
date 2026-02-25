#include "cw_textfield.hpp"
#include "lv_font_intel.h"

CWTextField::CWTextField(lv_obj_t* parent, const lv_font_t* font)
{
    // Scrollable container — the outer widget callers position/size.
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    // Spangroup grows vertically with content; container scrolls.
    spangroup_ = lv_spangroup_create(container_);
    lv_obj_set_size(spangroup_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(spangroup_, font ? font : &lv_font_intel_20, 0);
    lv_spangroup_set_align(spangroup_, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_set_overflow(spangroup_, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_mode(spangroup_, LV_SPAN_MODE_BREAK);
    current_span_ = lv_spangroup_new_span(spangroup_);
}

void CWTextField::scroll_to_bottom()
{
    lv_obj_scroll_to_y(container_, lv_obj_get_scroll_y_max(container_), LV_ANIM_OFF);
}

void CWTextField::add_char(char c)
{
    current_word_ += c;
    lv_span_set_text(current_span_, current_word_.c_str());
    lv_spangroup_refresh(spangroup_);
    scroll_to_bottom();
}

void CWTextField::add_string(const std::string& s)
{
    current_word_ += s;
    lv_span_set_text(current_span_, current_word_.c_str());
    lv_spangroup_refresh(spangroup_);
    scroll_to_bottom();
}

void CWTextField::next_word()
{
    add_char('\n');
    current_span_ = lv_spangroup_new_span(spangroup_);
    current_word_.clear();
}

void CWTextField::current_word_ok()
{
    lv_style_set_text_color(lv_span_get_style(current_span_),
                            lv_palette_main(LV_PALETTE_GREEN));
    lv_spangroup_refresh(spangroup_);
    next_word();
}

void CWTextField::current_word_err()
{
    lv_style_set_text_color(lv_span_get_style(current_span_),
                            lv_palette_main(LV_PALETTE_RED));
    lv_spangroup_refresh(spangroup_);
    next_word();
}
