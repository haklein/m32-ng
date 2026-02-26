#pragma once

// Adapted from m5core2-cwtrainer/lib/IambicKeyer/StraightKeyer.
// Changes: removed Arduino dependency; GPIO read and millis injected via
// constructor function pointers.

#include <cstdint>
#include "common.h"
#include "symbol_player.h"

typedef bool (*read_key_fun_ptr)();  // returns true when key is pressed (active)

// Decodes a straight key into dot/dash signals with adaptive speed detection.
// Call decode() on every iteration of the main task loop.
// Fires state_changed_cb with PLAY_STATE_DOT_ON/DASH_ON/STRAIGHT_ON/OFF events.
class StraightKeyer
{
public:
    StraightKeyer(play_state_changed_fun_ptr state_changed_cb,
                  read_key_fun_ptr read_key_cb,
                  millis_fun_ptr millis_cb);

    // Returns true if the filtered key state changed since last call.
    bool checkInput();

    // Drive the decoder state machine; call every loop iteration.
    void decode();

    // Current adaptive dit length estimate (ms) — for external decode threshold.
    unsigned long get_dit_avg() const { return dit_avg; }

private:
    play_state_changed_fun_ptr state_changed_cb;
    read_key_fun_ptr read_key_cb;
    millis_fun_ptr millis_cb;

    enum DecoderState { LOW_, HIGH_, INTERELEMENT_, INTERCHAR_ };
    DecoderState decoder_state = LOW_;

    int nbtime = 1;                // noise blanker threshold in ms

    unsigned long start_time_high = 0;
    unsigned long high_duration = 0;
    unsigned long last_start_time = 0;
    bool realstate = false;
    bool realstate_before = false;
    bool filtered_state = false;
    bool filtered_state_before = false;
    long start_time_low = 0;
    long low_duration = 0;

    unsigned long dit_avg = 60;   // adaptive dit length estimate in ms
    unsigned long dah_avg = 180;  // adaptive dah length estimate in ms
    uint8_t d_wpm = 15;

    void on_rising();
    void on_falling();
    void recalculate_dit(unsigned long duration);
    void recalculate_dah(unsigned long duration);
};
