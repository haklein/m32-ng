#include "cw_invaders.h"
#include <algorithm>
#include <cstring>

// Koch method character order (Morserino standard).
const char CwInvaders::KOCH_ORDER[] =
    "KMRSUAPTLOWINJEF0YV,G5/Q9ZH38B?427C1D6X";

// ── Animation helpers ────────────────────────────────────────────────────────

void CwInvaders::anim_x_cb(void* obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t*)obj, (lv_coord_t)v);
}

// Called when scroll animation finishes — invader reached left edge.
void CwInvaders::anim_done_cb(lv_anim_t* a)
{
    auto* inv = (Invader*)lv_anim_get_user_data(a);
    if (inv->dying) return;  // already handled
    // Signal: mark with a red flash, then the main tick() will remove it
    lv_obj_set_style_text_color(inv->label, lv_color_hex(0xFF0000), 0);
    inv->dying = true;
}

// Timer callback to delete a killed invader's label after a brief flash.
void CwInvaders::kill_timer_cb(lv_timer_t* t)
{
    auto* inv = (Invader*)lv_timer_get_user_data(t);
    if (inv->label) {
        lv_obj_del(inv->label);
        inv->label = nullptr;
    }
    lv_timer_del(t);
}

// ── Construction ─────────────────────────────────────────────────────────────

CwInvaders::CwInvaders(lv_obj_t* game_area, const lv_font_t* font,
                       std::mt19937& rng, MillisFn millis_fn)
    : game_area_(game_area)
    , font_(font)
    , rng_(rng)
    , millis_fn_(millis_fn)
{
}

CwInvaders::~CwInvaders()
{
    stop();
}

// ── Game control ─────────────────────────────────────────────────────────────

void CwInvaders::start()
{
    stop();
    score_      = 0;
    lives_      = 3;
    level_      = 1;
    streak_     = 0;
    game_over_  = false;
    paused_     = false;
    koch_unlocked_ = 2;
    spawn_interval_ms_ = 2500;
    scroll_duration_ms_ = 8000;
    last_spawn_t_ = millis_fn_();
}

void CwInvaders::stop()
{
    for (auto* inv : invaders_) {
        if (inv->label) {
            lv_anim_del(inv->label, anim_x_cb);
            lv_obj_del(inv->label);
        }
        delete inv;
    }
    invaders_.clear();
}

// ── Main tick ────────────────────────────────────────────────────────────────

void CwInvaders::set_paused(bool p)
{
    if (p == paused_) return;
    paused_ = p;
    // Pause/resume all active animations
    for (auto* inv : invaders_) {
        if (inv->label && !inv->dying) {
            if (p)
                lv_anim_del(inv->label, anim_x_cb);
            else {
                // Restart animation from current position
                lv_coord_t cur_x = lv_obj_get_x(inv->label);
                lv_coord_t area_w = lv_obj_get_content_width(game_area_);
                int remaining = (int)scroll_duration_ms_ * (cur_x + 30) / (area_w + 30);
                if (remaining < 100) remaining = 100;
                lv_anim_init(&inv->anim);
                lv_anim_set_var(&inv->anim, inv->label);
                lv_anim_set_exec_cb(&inv->anim, anim_x_cb);
                lv_anim_set_values(&inv->anim, cur_x, -30);
                lv_anim_set_duration(&inv->anim, remaining);
                lv_anim_set_user_data(&inv->anim, inv);
                lv_anim_set_completed_cb(&inv->anim, anim_done_cb);
                lv_anim_set_path_cb(&inv->anim, lv_anim_path_linear);
                lv_anim_start(&inv->anim);
            }
        }
    }
    if (!p) last_spawn_t_ = millis_fn_();  // reset spawn timer on unpause
}

void CwInvaders::tick()
{
    if (game_over_ || paused_) return;

    unsigned long now = millis_fn_();

    // Spawn new invaders on timer
    if ((now - last_spawn_t_) >= (unsigned long)spawn_interval_ms_) {
        spawn_invader();
        last_spawn_t_ = now;
    }

    // Check for invaders that reached the left edge (dying from anim_done)
    for (auto it = invaders_.begin(); it != invaders_.end(); ) {
        Invader* inv = *it;
        if (inv->dying && inv->label) {
            if (!inv->hit) {
                lives_--;
                streak_ = 0;
            }
            // Brief flash then delete
            lv_timer_create(kill_timer_cb, 200, inv);
            lv_anim_del(inv->label, anim_x_cb);
            it = invaders_.erase(it);
            if (lives_ <= 0) {
                game_over_ = true;
                // Clean up remaining invaders
                for (auto* rem : invaders_) {
                    if (rem->label) {
                        lv_anim_del(rem->label, anim_x_cb);
                        lv_obj_del(rem->label);
                    }
                    delete rem;
                }
                invaders_.clear();
                return;
            }
        } else if (!inv->label) {
            // Already cleaned up
            delete inv;
            it = invaders_.erase(it);
        } else {
            ++it;
        }
    }
}

// ── Spawning ─────────────────────────────────────────────────────────────────

void CwInvaders::spawn_invader()
{
    // Pick a random Koch character
    std::uniform_int_distribution<int> dist(0, koch_unlocked_ - 1);
    char ch = KOCH_ORDER[dist(rng_)];

    // Random Y within game area
    lv_coord_t area_h = lv_obj_get_content_height(game_area_);
    lv_coord_t area_w = lv_obj_get_content_width(game_area_);
    int font_h = lv_font_get_line_height(font_);
    int max_y = area_h - font_h - 4;
    if (max_y < 4) max_y = 4;
    std::uniform_int_distribution<int> y_dist(4, max_y);

    auto* inv = new Invader{};
    inv->ch = ch;

    // Create label
    inv->label = lv_label_create(game_area_);
    char txt[2] = { ch, '\0' };
    lv_label_set_text(inv->label, txt);
    lv_obj_set_style_text_font(inv->label, font_, 0);
    lv_obj_set_style_text_color(inv->label, lv_color_hex(0xFFFFFF), 0);

    lv_coord_t start_x = area_w;
    lv_coord_t y = (lv_coord_t)y_dist(rng_);
    lv_obj_set_pos(inv->label, start_x, y);

    // Animate X from right edge to off-screen left
    lv_anim_init(&inv->anim);
    lv_anim_set_var(&inv->anim, inv->label);
    lv_anim_set_exec_cb(&inv->anim, anim_x_cb);
    lv_anim_set_values(&inv->anim, start_x, -30);
    lv_anim_set_duration(&inv->anim, scroll_duration_ms_);
    lv_anim_set_user_data(&inv->anim, inv);
    lv_anim_set_completed_cb(&inv->anim, anim_done_cb);
    lv_anim_set_path_cb(&inv->anim, lv_anim_path_linear);
    lv_anim_start(&inv->anim);

    invaders_.push_back(inv);
}

// ── Matching ─────────────────────────────────────────────────────────────────

bool CwInvaders::try_match(const std::string& letter)
{
    if (game_over_ || letter.empty()) return false;

    char input_ch = letter[0];
    if (input_ch >= 'a' && input_ch <= 'z')
        input_ch = input_ch - 'a' + 'A';

    // Find the leftmost (closest to danger) matching invader
    Invader* best = nullptr;
    lv_coord_t best_x = 9999;

    for (auto* inv : invaders_) {
        if (inv->dying || !inv->label) continue;
        if (inv->ch == input_ch) {
            lv_coord_t x = lv_obj_get_x(inv->label);
            if (x < best_x) {
                best_x = x;
                best = inv;
            }
        }
    }

    if (!best) {
        streak_ = 0;
        return false;
    }

    // Hit! Flash green and schedule deletion
    score_ += 10 + streak_;
    streak_++;
    best->dying = true;
    best->hit   = true;
    lv_anim_del(best->label, anim_x_cb);
    lv_obj_set_style_text_color(best->label, lv_color_hex(0x00FF00), 0);
    lv_timer_create(kill_timer_cb, 150, best);

    // Koch advance: correctly copying the highest unlocked character levels up
    if (koch_unlocked_ < KOCH_SIZE &&
        input_ch == KOCH_ORDER[koch_unlocked_ - 1]) {
        advance_level();
    }

    return true;
}

// ── Leveling ─────────────────────────────────────────────────────────────────

void CwInvaders::advance_level()
{
    level_++;
    if (koch_unlocked_ < KOCH_SIZE)
        koch_unlocked_++;
    if (spawn_interval_ms_ > 800)
        spawn_interval_ms_ -= 150;
    if (scroll_duration_ms_ > 3000)
        scroll_duration_ms_ -= 400;
}

// ── Cleanup helper ───────────────────────────────────────────────────────────

void CwInvaders::remove_invader(Invader* inv)
{
    if (inv->label) {
        lv_anim_del(inv->label, anim_x_cb);
        lv_obj_del(inv->label);
        inv->label = nullptr;
    }
}
