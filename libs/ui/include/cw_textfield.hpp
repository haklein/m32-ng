#pragma once

#include <string>
#include <lvgl.h>

// CW trainer text display widget built on lv_spangroup.
//
// Words are accumulated character-by-character in real time via add_char() /
// add_string().  Calling current_word_ok() or current_word_err() commits the
// current word with green or red color respectively and advances to the next
// word.  next_word() is also public for cases where no colour feedback is
// needed.
//
// The outer container fills its parent by default.  Call
//   lv_obj_set_size(field.obj(), w, h);
// after construction to constrain it to a specific area.  The spangroup inside
// grows with content and the container scrolls automatically.
//
// Note: each word creates one lv_span.  For long sessions the spangroup will
// accumulate spans; consider calling lv_spangroup_delete_span() on old spans
// to bound memory usage once span deletion is needed.

class CWTextField
{
public:
    explicit CWTextField(lv_obj_t* parent, const lv_font_t* font = nullptr);

    // Append a single character to the current word (updates display immediately).
    void add_char(char c);

    // Append a string to the current word (updates display immediately).
    void add_string(const std::string& s);

    // Mark the current word correct (green), advance to next word.
    void current_word_ok();

    // Mark the current word incorrect (red), advance to next word.
    void current_word_err();

    // Start a new word without applying colour feedback.
    void next_word();

    // Return the outer scrollable container for size / position adjustments.
    lv_obj_t* obj() const { return container_; }

    // Enable/disable auto-scroll on new content.  When disabled, the view
    // stays put so the user can scroll freely.  Re-enabling snaps to bottom.
    void set_auto_scroll(bool enable);

private:
    void scroll_to_bottom();

    lv_obj_t*   container_;
    lv_obj_t*   spangroup_;
    lv_span_t*  current_span_;
    std::string current_word_;
    bool        auto_scroll_ = true;
};
