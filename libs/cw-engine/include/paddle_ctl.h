#pragma once

// Debounces raw dot/dash lever events and reports combined LeverState changes
// to the iambic keyer.
//
// Each lever is debounced independently so a brief dit squeeze during a dah
// is never masked by a near-simultaneous dah release (the root cause of
// missed elements in the original combined-state debounce).
//
// Call setDotPushed() / setDashPushed() from your ISR or polling loop;
// call tick() from the main task loop.

#include "iambic_keyer.h"

typedef void (*lever_state_changed_fun_ptr)(LeverState);

class PaddleCtl
{
public:
    PaddleCtl(unsigned long debounce_ms,
              lever_state_changed_fun_ptr state_changed_cb,
              millis_fun_ptr millis_cb);

    void tick();
    void setDotPushed(bool pushed);
    void setDashPushed(bool pushed);

private:
    millis_fun_ptr millis_cb_;
    unsigned long  debounce_ms_;
    lever_state_changed_fun_ptr state_changed_cb_;

    // Per-lever independent debounce state.
    struct Lever {
        bool          raw     = false;   // latest input
        bool          stable  = false;   // debounced output
        unsigned long changed = 0;       // timestamp of last raw change
    };

    Lever dit_;
    Lever dah_;
    LeverState current_ = LEVER_UNSET;

    static LeverState combine(bool dit, bool dah, LeverState prev);
};
