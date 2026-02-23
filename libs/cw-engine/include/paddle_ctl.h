#pragma once

// Adopted from m5core2-cwtrainer/lib/IambicKeyer/PaddleCtl — no functional changes.
// GPIO reads are the caller's responsibility; no Arduino dependency.

#include "iambic_keyer.h"

enum LeverEvent
{
    LEVER_EVENT_DOT_OFF = 0,
    LEVER_EVENT_DOT_ON,
    LEVER_EVENT_DASH_OFF,
    LEVER_EVENT_DASH_ON
};

typedef void (*lever_state_changed_fun_ptr)(LeverState);

// Debounces raw dot/dash GPIO events and reports LeverState changes.
// Call setDotPushed() / setDashPushed() from your ISR or polling loop;
// call tick() from the main task loop.
class PaddleCtl
{
public:
    PaddleCtl(unsigned long state_change_threshold_ms,
              lever_state_changed_fun_ptr state_changed_cb,
              millis_fun_ptr millis_cb);

    void tick();
    void setDotPushed(bool pushed);
    void setDashPushed(bool pushed);

private:
    void onLeverEvent(LeverEvent lever_event);
    LeverState getNextLeverState(LeverEvent lever_event);
    bool nextStateReady();

    millis_fun_ptr millis_cb;
    unsigned long state_change_threshold;
    lever_state_changed_fun_ptr state_changed_cb;
    LeverState current_lever_state = LEVER_UNSET;
    LeverState next_lever_state = LEVER_UNSET;
    unsigned long next_lever_state_changed = 0;
};
