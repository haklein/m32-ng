#pragma once

// Adopted from m5core2-cwtrainer/lib/IambicKeyer/IambicKeyer — no functional changes.
// millis is injected via constructor; no Arduino dependency.

#include "common.h"
#include "symbol_player.h"

enum LeverState
{
    LEVER_UNSET = 0,
    LEVER_DOT,
    LEVER_DASH,
    LEVER_DOT_DASH,
    LEVER_DASH_DOT,
};

enum KeyerState
{
    KEYER_STATE_UNSET = 0,
    KEYER_STATE_STOPPED,
    KEYER_STATE_DOT,
    KEYER_STATE_DASH,
    KEYER_STATE_ALTERNATING_DOT,
    KEYER_STATE_ALTERNATING_DASH
};

class IambicKeyer
{
public:
    IambicKeyer(unsigned long duration_unit,
                play_state_changed_fun_ptr state_changed_cb,
                millis_fun_ptr millis_cb,
                bool mode_a);

    void tick();
    void setLeverState(LeverState lever_state);
    void setDurationUnit(unsigned long duration_unit);
    void setSpeedWPM(unsigned long speed_wpm);
    void setModeA(bool mode_a);
    bool getModeA();
    void setReleaseCompensation(unsigned long ms);
    void setCurtisBThreshold(uint8_t dit_pct, uint8_t dah_pct);

    KeyerState nextKeyerState();
    bool isPlayStateExpired();
    void switchPlayStateIfReady();

private:
    SymbolPlayer symbol_player;
    KeyerState keyer_state = KEYER_STATE_UNSET;
    millis_fun_ptr millis_cb;
    LeverState lever_state = LEVER_UNSET;
    LeverState prev_lever_state = LEVER_UNSET;
    bool lever_upgrade = false;
    bool mode_a = false;
    uint8_t curtisb_dit_pct_ = 0;  // 0 = original Curtis B
    uint8_t curtisb_dah_pct_ = 0;
};
