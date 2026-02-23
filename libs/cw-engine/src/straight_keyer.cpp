#include "straight_keyer.h"
#include <algorithm>
#include <cmath>

// Adapted from m5core2-cwtrainer/lib/IambicKeyer/StraightKeyer.
// Algorithmic content preserved exactly; Arduino-specific calls replaced
// with injected function pointers.

StraightKeyer::StraightKeyer(play_state_changed_fun_ptr state_changed_cb,
                             read_key_fun_ptr read_key_cb,
                             millis_fun_ptr millis_cb)
    : state_changed_cb(state_changed_cb)
    , read_key_cb(read_key_cb)
    , millis_cb(millis_cb)
{}

bool StraightKeyer::checkInput()
{
    bool keystate = read_key_cb();
    realstate = keystate;

    if (realstate != realstate_before) {
        last_start_time = millis_cb();
    }

    if ((millis_cb() - last_start_time) > static_cast<unsigned long>(nbtime)) {
        if (realstate != filtered_state) {
            filtered_state = realstate;
        }
    }
    realstate_before = realstate;

    if (filtered_state == filtered_state_before) {
        return false;
    }
    filtered_state_before = filtered_state;
    return true;
}

void StraightKeyer::decode()
{
    float lacktime;
    int wpm;

    switch (decoder_state) {
    case INTERELEMENT_:
        if (checkInput()) {
            on_rising();
            decoder_state = HIGH_;
        } else {
            low_duration = millis_cb() - start_time_low;
            lacktime = 0.8f;
            if (d_wpm > 35)      lacktime = 2.4f;
            else if (d_wpm > 30) lacktime = 2.3f;
            if (low_duration > static_cast<long>(lacktime * dit_avg)) {
                state_changed_cb(PLAY_STATE_STOPPED);
                wpm = (d_wpm + static_cast<int>(7200 / (dah_avg + 3 * dit_avg))) / 2;
                if (d_wpm != static_cast<uint8_t>(wpm)) {
                    d_wpm = static_cast<uint8_t>(wpm);
                }
                decoder_state = INTERCHAR_;
            }
        }
        break;

    case INTERCHAR_:
        if (checkInput()) {
            on_rising();
            decoder_state = HIGH_;
        } else {
            low_duration = millis_cb() - start_time_low;
            lacktime = 3.0f;
            if (d_wpm > 35)      lacktime = 6.0f;
            else if (d_wpm > 30) lacktime = 5.5f;
            if (low_duration > static_cast<long>(lacktime * dit_avg)) {
                state_changed_cb(PLAY_STATE_STOPPED);
                decoder_state = LOW_;
            }
        }
        break;

    case LOW_:
        if (checkInput()) {
            on_rising();
            decoder_state = HIGH_;
        }
        break;

    case HIGH_:
        if (checkInput()) {
            on_falling();
            decoder_state = INTERELEMENT_;
        }
        break;
    }
}

void StraightKeyer::on_rising()
{
    unsigned long now = millis_cb();
    low_duration = now - start_time_low;
    start_time_high = now;

    state_changed_cb(PLAY_STATE_STRAIGHT_ON);

    if (low_duration < static_cast<long>(dit_avg * 1.4f)) {
        recalculate_dit(low_duration);
    }
}

void StraightKeyer::on_falling()
{
    unsigned long now = millis_cb();
    unsigned long threshold = static_cast<unsigned long>(dit_avg * sqrt(static_cast<double>(dah_avg) / dit_avg));

    high_duration = now - start_time_high;
    start_time_low = now;

    if (high_duration > (dit_avg / 2) && high_duration < (dah_avg * 5 / 2)) {
        if (high_duration < threshold) {
            state_changed_cb(PLAY_STATE_DOT_ON);
            recalculate_dit(high_duration);
        } else {
            state_changed_cb(PLAY_STATE_DASH_ON);
            recalculate_dah(high_duration);
        }
    }
    state_changed_cb(PLAY_STATE_STRAIGHT_OFF);
}

void StraightKeyer::recalculate_dit(unsigned long duration)
{
    dit_avg = (4 * dit_avg + duration) / 5;
    // Noise blanker: clamp between 7 and 20 ms
    nbtime = static_cast<int>(std::min(std::max(dit_avg / 5, 7UL), 20UL));
}

void StraightKeyer::recalculate_dah(unsigned long duration)
{
    if (duration > 2 * dah_avg) {
        // Very rapid decrease in speed — adjust faster
        dah_avg = (dah_avg + 2 * duration) / 3;
        dit_avg = dit_avg / 2 + dah_avg / 6;
    } else {
        dah_avg = (3 * dit_avg + dah_avg + duration) / 3;
    }
}
