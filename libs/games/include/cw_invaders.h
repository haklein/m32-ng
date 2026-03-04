#pragma once

// CW Invaders — Morse training game.
// Characters scroll across the screen; player morses them to destroy.
// Koch progression: starts with 2 chars, adds one per level.

#include <lvgl.h>
#include <string>
#include <vector>
#include <random>
#include <cstdint>

class CwInvaders {
public:
    using MillisFn = unsigned long(*)();

    CwInvaders(lv_obj_t* game_area, const lv_font_t* font,
               std::mt19937& rng, MillisFn millis_fn);
    ~CwInvaders();

    void start();
    void tick();
    bool try_match(const std::string& letter);  // true if hit
    void stop();
    void set_paused(bool p);
    bool paused() const { return paused_; }

    int  score()     const { return score_; }
    int  lives()     const { return lives_; }
    int  level()     const { return level_; }
    int  streak()    const { return streak_; }
    bool game_over() const { return game_over_; }

private:
    struct Invader {
        lv_obj_t*   label;
        char        ch;           // the character to match
        lv_anim_t   anim;
        bool        dying = false;
        bool        hit   = false;  // true = killed by player, false = reached edge
    };

    lv_obj_t*       game_area_;
    const lv_font_t* font_;
    std::mt19937&   rng_;
    MillisFn        millis_fn_;

    std::vector<Invader*> invaders_;
    int  score_      = 0;
    int  lives_      = 3;
    int  level_      = 1;
    int  streak_     = 0;
    bool game_over_  = false;
    bool paused_     = false;

    // Koch progression
    static const char KOCH_ORDER[];
    static constexpr int KOCH_SIZE = 40;
    int koch_unlocked_ = 2;

    // Timing
    unsigned long last_spawn_t_    = 0;
    int spawn_interval_ms_         = 2500;
    int scroll_duration_ms_        = 8000;  // time to cross the screen

    void spawn_invader();
    void advance_level();
    void remove_invader(Invader* inv);
    void kill_invader(Invader* inv, bool hit);

    static void anim_x_cb(void* obj, int32_t v);
    static void anim_done_cb(lv_anim_t* a);
    static void kill_timer_cb(lv_timer_t* t);
};
