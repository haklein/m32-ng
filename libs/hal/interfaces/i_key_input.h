#pragma once

#include <cstdint>

enum class KeyEvent {
    PADDLE_DIT_DOWN,
    PADDLE_DIT_UP,
    PADDLE_DAH_DOWN,
    PADDLE_DAH_UP,
    STRAIGHT_DOWN,
    STRAIGHT_UP,
    ENCODER_CW,
    ENCODER_CCW,
    BUTTON_ENCODER_SHORT,
    BUTTON_ENCODER_LONG,
    BUTTON_AUX_SHORT,
    BUTTON_AUX_LONG,
    TOUCH_LEFT_DOWN,
    TOUCH_LEFT_UP,
    TOUCH_RIGHT_DOWN,
    TOUCH_RIGHT_UP,
};

// Source of physical key/paddle/encoder/button input.
// Implementations deliver debounced, edge-triggered events.
// On targets without an encoder (e.g. M5Core2), ENCODER_* events are never emitted.
class IKeyInput
{
public:
    virtual ~IKeyInput() = default;

    // Returns true and sets `out` if an event is waiting; non-blocking.
    virtual bool poll(KeyEvent& out) = 0;

    // Blocks until an event arrives or timeout_ms elapses.
    // Returns false on timeout.
    virtual bool wait(KeyEvent& out, uint32_t timeout_ms) = 0;
};
