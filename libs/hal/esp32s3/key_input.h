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
// ~100 Hz with software debounce; short (<500 ms) vs long press is detected
// at release time.
//
// Touch strips (PIN_TOUCH_LEFT / PIN_TOUCH_RIGHT) are polled via touchRead().
//
// All events land in a FreeRTOS queue (depth 32) consumed by poll() / wait().

#include "../interfaces/i_key_input.h"
#include <ESP32Encoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class PocketKeyInput : public IKeyInput
{
public:
    // touch_threshold: raw touchRead() value above which a strip counts as pressed.
    // On ESP32-S3 the touch sensor returns HIGHER values when touched.
    // Typical idle: 38000–42000; typical touched: 46000–52000; default 44000.
    explicit PocketKeyInput(uint32_t touch_threshold = 44000);
    ~PocketKeyInput() override;

    bool poll(KeyEvent& out) override;
    bool wait(KeyEvent& out, uint32_t timeout_ms) override;

    // Called from GPIO ISRs — must be IRAM-safe.
    void IRAM_ATTR push_from_isr(KeyEvent ev);

private:
    static constexpr int    QUEUE_DEPTH     = 32;
    static constexpr int    POLL_INTERVAL_MS = 10;  // encoder + button poll rate
    static constexpr uint32_t LONG_PRESS_MS = 500;
    static constexpr int    STEPS_PER_DETENT = 2;   // half-quad encoder

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
    uint32_t       touch_threshold_;
};

#endif // BOARD_POCKETWROOM
