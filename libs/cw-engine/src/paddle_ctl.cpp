#include "paddle_ctl.h"

PaddleCtl::PaddleCtl(unsigned long debounce_ms,
                     lever_state_changed_fun_ptr state_changed_cb,
                     millis_fun_ptr millis_cb)
    : millis_cb_(millis_cb)
    , debounce_ms_(debounce_ms)
    , state_changed_cb_(state_changed_cb)
{}

void PaddleCtl::setDotPushed(bool pushed)
{
    if (pushed != dit_.raw) {
        dit_.raw     = pushed;
        dit_.changed = millis_cb_();
    }
}

void PaddleCtl::setDashPushed(bool pushed)
{
    if (pushed != dah_.raw) {
        dah_.raw     = pushed;
        dah_.changed = millis_cb_();
    }
}

LeverState PaddleCtl::combine(bool dit, bool dah, LeverState prev)
{
    if (dit && dah) {
        // Preserve squeeze direction from the lever that was already active.
        if (prev == LEVER_DOT)      return LEVER_DOT_DASH;
        if (prev == LEVER_DASH)     return LEVER_DASH_DOT;
        // Already in a squeeze — keep same direction.
        if (prev == LEVER_DOT_DASH || prev == LEVER_DASH_DOT)
            return prev;
        // Both pressed from idle — default (shouldn't happen in practice).
        return LEVER_DOT_DASH;
    }
    if (dit) return LEVER_DOT;
    if (dah) return LEVER_DASH;
    return LEVER_UNSET;
}

void PaddleCtl::tick()
{
    unsigned long now = millis_cb_();
    bool changed = false;

    // Debounce dit independently.
    if (dit_.raw != dit_.stable && (now - dit_.changed) > debounce_ms_) {
        dit_.stable = dit_.raw;
        changed = true;
    }
    // Debounce dah independently.
    if (dah_.raw != dah_.stable && (now - dah_.changed) > debounce_ms_) {
        dah_.stable = dah_.raw;
        changed = true;
    }

    if (changed) {
        LeverState next = combine(dit_.stable, dah_.stable, current_);
        if (next != current_) {
            current_ = next;
            state_changed_cb_(next);
        }
    }
}
