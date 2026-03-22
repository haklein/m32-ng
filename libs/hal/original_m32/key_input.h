#pragma once

#ifdef BOARD_ORIGINAL_M32

// Original Morserino-32 (Heltec V2) key/paddle/encoder input.
//
// Touch pins: T2/GPIO 2 (LEFT), T5/GPIO 12 (RIGHT)
// On ESP32 (non-S3), touchRead() returns LOWER values when touched.
//
// External paddle jack: GPIO 32/33 (V4: L=32 R=33, V3: swapped).
// Board version detected at runtime via NVS + ADC on GPIO 13.
//
// Encoder: GPIO 38 (CLK), GPIO 39 (DT) via ESP32Encoder (PCNT hardware)
// Encoder button: GPIO 37 — active HIGH (reversed from typical)
// Aux button: GPIO 0 — active LOW
// Keyer output: GPIO 25

#include "../interfaces/i_key_input.h"
#include <ESP32Encoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Detect original M32 board version (V3 vs V4).
// Reads NVS first; if not set, probes GPIO 13 ADC to distinguish.
// V3: older Heltec module, battery on GPIO 13, ext paddle L/R swapped.
// V4: newer Heltec module, battery on GPIO 37, ext paddle L=32 R=33.
uint8_t detect_board_version();

class OriginalM32KeyInput : public IKeyInput
{
public:
    OriginalM32KeyInput(uint32_t touch_l_idle, uint32_t touch_r_idle,
                        uint8_t board_version);
    ~OriginalM32KeyInput() override;

    bool poll(KeyEvent& out) override;
    bool wait(KeyEvent& out, uint32_t timeout_ms) override;

private:
    static constexpr int      QUEUE_DEPTH      = 32;
    static constexpr int      POLL_INTERVAL_MS = 1;
    static constexpr uint32_t LONG_PRESS_MS    = 500;
    static constexpr int      STEPS_PER_DETENT = 2;
    static constexpr uint32_t MAX_TOUCH_HOLD_MS = 3000;

    // ESP32 touch: lower value = touched.
    static constexpr float SENS_ON  = 0.45f;  // press:   value < idle * 0.45
    static constexpr float SENS_OFF = 0.55f;  // release: value > idle * 0.55

    static constexpr uint8_t DEBOUNCE_READS = 5;  // consecutive matching reads

    struct ButtonState {
        bool     pressed      = false;
        uint32_t press_ms     = 0;
        bool     long_done    = false;
        uint8_t  stable_cnt   = 0;     // consecutive reads matching pending state
        bool     pending      = false;  // state we're transitioning toward
    };

    void push(KeyEvent ev);
    void push_release(KeyEvent ev);
    void update_button(ButtonState& s, bool now_pressed, uint32_t now_ms,
                       KeyEvent short_ev, KeyEvent long_ev);
    void poll_task_body();
    static void poll_task_entry(void* arg);

    QueueHandle_t  event_queue_;
    TaskHandle_t   poll_task_handle_ = nullptr;
    ESP32Encoder   encoder_;
    uint32_t       touch_l_on_, touch_l_off_;
    uint32_t       touch_r_on_, touch_r_off_;
    uint8_t        paddle_left_pin_;   // GPIO for dit (board-version dependent)
    uint8_t        paddle_right_pin_;  // GPIO for dah
};

#endif // BOARD_ORIGINAL_M32
