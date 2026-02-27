#pragma once

#ifdef BOARD_POCKETWROOM

// Pocket (ESP32-S3) key/paddle/encoder input.
//
// Paddle LEFT (PIN_PADDLE_LEFT, dit) and RIGHT (PIN_PADDLE_RIGHT, dah),
// straight key (PIN_KEYER), are driven by GPIO-change ISRs for minimal
// latency — critical for CW timing accuracy.
//
// Rotary encoder position is read from ESP32Encoder (hardware PCNT).
// Encoder button (PIN_ROT_BTN) and aux button (PIN_BUTTON) are polled at
// ~500 Hz with software debounce; short (<500 ms) vs long press is detected
// at release time.
//
// Touch strips (PIN_TOUCH_LEFT / PIN_TOUCH_RIGHT) are polled at 500 Hz via
// touchRead() with per-strip hysteresis thresholds for zero-latency-cost
// debouncing.  At 35 WPM a dit is 34 ms; 2 ms poll gives ≤ 6 % jitter.
//
// All events land in a FreeRTOS queue (depth 32) consumed by poll() / wait().

#include "../interfaces/i_key_input.h"
#include <ESP32Encoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class PocketKeyInput : public IKeyInput
{
public:
    // Per-strip idle values from boot calibration.  Hysteresis thresholds are
    // computed internally:  ON = idle + HYST_ON, OFF = idle + HYST_OFF.
    PocketKeyInput(uint32_t touch_l_idle, uint32_t touch_r_idle);
    ~PocketKeyInput() override;

    bool poll(KeyEvent& out) override;
    bool wait(KeyEvent& out, uint32_t timeout_ms) override;

    // Called from GPIO ISRs — must be IRAM-safe.
    void IRAM_ATTR push_from_isr(KeyEvent ev);

private:
    static constexpr int      QUEUE_DEPTH     = 32;
    static constexpr int      POLL_INTERVAL_MS = 2;   // 500 Hz — critical for CW timing
    static constexpr uint32_t LONG_PRESS_MS   = 500;
    static constexpr int      STEPS_PER_DETENT = 2;   // half-quad encoder
    static constexpr uint32_t HYST_ON         = 4000;  // press threshold above idle
    static constexpr uint32_t HYST_OFF        = 2000;  // release threshold above idle

    struct ButtonState {
        bool     pressed   = false;
        uint32_t press_ms  = 0;
        bool     long_done = false;
    };

    void push(KeyEvent ev);
    void update_button(ButtonState& s, bool now_pressed, uint32_t now_ms,
                       KeyEvent short_ev, KeyEvent long_ev);
    void poll_task_body();
    static void poll_task_entry(void* arg);

    QueueHandle_t  event_queue_;
    TaskHandle_t   poll_task_handle_ = nullptr;
    ESP32Encoder   encoder_;
    uint32_t       touch_l_on_;    // per-strip press threshold
    uint32_t       touch_l_off_;   // per-strip release threshold
    uint32_t       touch_r_on_;
    uint32_t       touch_r_off_;
};

#endif // BOARD_POCKETWROOM
