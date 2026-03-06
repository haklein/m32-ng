#pragma once

// Adopted from m5core2-cwtrainer/lib/IambicKeyer/SymbolPlayer — no functional changes.
// millis is injected via constructor; no Arduino dependency.

#include "common.h"
#include <cstdint>

enum PlayState
{
    PLAY_STATE_UNSET = 0,
    PLAY_STATE_STOPPED,
    PLAY_STATE_DOT_ON,
    PLAY_STATE_DOT_OFF,
    PLAY_STATE_DASH_ON,
    PLAY_STATE_DASH_OFF,
    PLAY_STATE_STRAIGHT_ON,
    PLAY_STATE_STRAIGHT_OFF
};

typedef void (*play_state_changed_fun_ptr)(PlayState);

class SymbolPlayer
{
public:
    SymbolPlayer(unsigned long duration_unit,
                 play_state_changed_fun_ptr state_changed_cb,
                 millis_fun_ptr millis_cb);

    bool ready();
    void playDot();
    void playDash();
    void tick();
    void setDurationUnit(unsigned long duration_unit);
    void setReleaseCompensation(unsigned long ms);

    // Enhanced Curtis B: returns true if current element is past the given
    // threshold percentage, or if not currently in an ON state.
    bool isPastElementThreshold(uint8_t dit_pct, uint8_t dah_pct);

    // True when an element is actively sounding (DOT_ON or DASH_ON).
    bool isSounding() const;

protected:
    PlayState getPlayState();
    unsigned long getPlayStateAge();
    PlayState nextPlayState();
    bool playStateFinished();
    unsigned long getPlayStateDuration();
    void setPlayState(PlayState state);

private:
    millis_fun_ptr millis_cb;
    play_state_changed_fun_ptr state_changed_cb;
    PlayState play_state = PLAY_STATE_UNSET;
    unsigned long last_state_change = 0;
    unsigned long duration_unit;
    unsigned long release_comp_ms = 0;
};
