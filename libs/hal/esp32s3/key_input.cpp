#ifdef BOARD_POCKETWROOM

#include "key_input.h"
#include <Arduino.h>

// ── Global instance pointer used by the static ISRs ───────────────────────────
static PocketKeyInput* s_instance = nullptr;

// ── GPIO ISRs (IRAM-resident; minimal work — push event and yield) ────────────

static void IRAM_ATTR isr_paddle_left()
{
    KeyEvent ev = (digitalRead(PIN_PADDLE_LEFT) == LOW)
                  ? KeyEvent::PADDLE_DIT_DOWN : KeyEvent::PADDLE_DIT_UP;
    s_instance->push_from_isr(ev);
}

static void IRAM_ATTR isr_paddle_right()
{
    KeyEvent ev = (digitalRead(PIN_PADDLE_RIGHT) == LOW)
                  ? KeyEvent::PADDLE_DAH_DOWN : KeyEvent::PADDLE_DAH_UP;
    s_instance->push_from_isr(ev);
}

// ── PocketKeyInput ─────────────────────────────────────────────────────────────

PocketKeyInput::PocketKeyInput(uint32_t touch_l_idle, uint32_t touch_r_idle)
    : touch_l_on_(touch_l_idle + HYST_ON),   touch_l_off_(touch_l_idle + HYST_OFF)
    , touch_r_on_(touch_r_idle + HYST_ON),   touch_r_off_(touch_r_idle + HYST_OFF)
{
    s_instance  = this;
    event_queue_ = xQueueCreate(QUEUE_DEPTH, sizeof(KeyEvent));

    // Configure paddle pins — active low, ISR on any edge.
    pinMode(PIN_PADDLE_LEFT,  INPUT_PULLUP);
    pinMode(PIN_PADDLE_RIGHT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_PADDLE_LEFT),  isr_paddle_left,  CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_PADDLE_RIGHT), isr_paddle_right, CHANGE);

    // PIN_KEYER is the MOSFET output for keying an external transmitter.
    pinMode(PIN_KEYER, OUTPUT);
    digitalWrite(PIN_KEYER, LOW);

    // Encoder button and aux button are polled (short/long press detection).
    pinMode(PIN_ROT_BTN, INPUT_PULLUP);
    pinMode(PIN_BUTTON,  INPUT_PULLUP);

    // Encoder — half-quadrature (two edges per detent).
    encoder_.attachHalfQuad(PIN_ROT_DT, PIN_ROT_CLK);
    encoder_.clearCount();

    // Polling task on core 0 (audio task runs on core 1).
    xTaskCreatePinnedToCore(poll_task_entry, "key_poll",
                            2048, this, 5, &poll_task_handle_, 0);
}

PocketKeyInput::~PocketKeyInput()
{
    if (poll_task_handle_) {
        vTaskDelete(poll_task_handle_);
        poll_task_handle_ = nullptr;
    }
    detachInterrupt(digitalPinToInterrupt(PIN_PADDLE_LEFT));
    detachInterrupt(digitalPinToInterrupt(PIN_PADDLE_RIGHT));
    digitalWrite(PIN_KEYER, LOW);
    if (event_queue_) vQueueDelete(event_queue_);
    s_instance = nullptr;
}

// ── IKeyInput ──────────────────────────────────────────────────────────────────

bool PocketKeyInput::poll(KeyEvent& out)
{
    return xQueueReceive(event_queue_, &out, 0) == pdTRUE;
}

bool PocketKeyInput::wait(KeyEvent& out, uint32_t timeout_ms)
{
    return xQueueReceive(event_queue_, &out,
                         pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void IRAM_ATTR PocketKeyInput::push_from_isr(KeyEvent ev)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(event_queue_, &ev, &woken);
    portYIELD_FROM_ISR(woken);
}

void PocketKeyInput::push(KeyEvent ev)
{
    xQueueSend(event_queue_, &ev, 0);   // drop if full (task context, non-blocking)
}

void PocketKeyInput::update_button(ButtonState& s, bool now_pressed,
                                    uint32_t now_ms,
                                    KeyEvent short_ev, KeyEvent long_ev)
{
    if (now_pressed && !s.pressed) {
        // Falling edge — record press start.
        s.pressed  = true;
        s.press_ms = now_ms;
        s.long_done = false;
    } else if (now_pressed && !s.long_done) {
        // Held — fire long event once threshold is crossed.
        if ((now_ms - s.press_ms) >= LONG_PRESS_MS) {
            push(long_ev);
            s.long_done = true;
        }
    } else if (!now_pressed && s.pressed) {
        // Rising edge — fire short event if long press was not already fired.
        if (!s.long_done) push(short_ev);
        s.pressed = false;
    }
}

// ── Polling task ──────────────────────────────────────────────────────────────

void PocketKeyInput::poll_task_entry(void* arg)
{
    static_cast<PocketKeyInput*>(arg)->poll_task_body();
}

void PocketKeyInput::poll_task_body()
{
    int64_t  last_enc_count   = 0;
    bool     touch_left_prev  = false;
    bool     touch_right_prev = false;
    ButtonState enc_btn, aux_btn;

    while (true) {
        uint32_t now = millis();

        // ── Rotary encoder ────────────────────────────────────────────────────
        int64_t count = encoder_.getCount();
        int64_t diff  = count - last_enc_count;
        while (diff >= STEPS_PER_DETENT) {
            push(KeyEvent::ENCODER_CW);
            last_enc_count += STEPS_PER_DETENT;
            diff           -= STEPS_PER_DETENT;
        }
        while (diff <= -STEPS_PER_DETENT) {
            push(KeyEvent::ENCODER_CCW);
            last_enc_count -= STEPS_PER_DETENT;
            diff           += STEPS_PER_DETENT;
        }

        // ── Encoder button ────────────────────────────────────────────────────
        update_button(enc_btn, digitalRead(PIN_ROT_BTN) == LOW, now,
                      KeyEvent::BUTTON_ENCODER_SHORT, KeyEvent::BUTTON_ENCODER_LONG);

        // ── Aux button ────────────────────────────────────────────────────────
        update_button(aux_btn, digitalRead(PIN_BUTTON) == LOW, now,
                      KeyEvent::BUTTON_AUX_SHORT, KeyEvent::BUTTON_AUX_LONG);

        // ── Touch left ────────────────────────────────────────────────────────
        // Per-strip hysteresis: press requires > ON threshold, release requires
        // < OFF threshold.  Eliminates edge flickering with zero latency cost.
        {
            uint32_t val = touchRead(PIN_TOUCH_LEFT);
            bool touch_l = touch_left_prev
                ? (val >= touch_l_off_)   // stay pressed until below OFF
                : (val > touch_l_on_);    // require above ON to press
            if (touch_l != touch_left_prev) {
                push(touch_l ? KeyEvent::TOUCH_LEFT_DOWN : KeyEvent::TOUCH_LEFT_UP);
                touch_left_prev = touch_l;
            }
        }

        // ── Touch right ───────────────────────────────────────────────────────
        {
            uint32_t val = touchRead(PIN_TOUCH_RIGHT);
            bool touch_r = touch_right_prev
                ? (val >= touch_r_off_)
                : (val > touch_r_on_);
            if (touch_r != touch_right_prev) {
                push(touch_r ? KeyEvent::TOUCH_RIGHT_DOWN : KeyEvent::TOUCH_RIGHT_UP);
                touch_right_prev = touch_r;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

#endif // BOARD_POCKETWROOM
