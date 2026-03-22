#ifdef BOARD_ORIGINAL_M32

#include "key_input.h"
#include <Arduino.h>
#include <ArduinoLog.h>
#include <Preferences.h>

// ── Board version detection ──────────────────────────────────────────────────
// V3: older Heltec, battery on GPIO 13, ext paddle pins swapped (L=33, R=32).
// V4: newer Heltec, battery on GPIO 37, ext paddle L=32, R=33.
// Detection: read ADC on GPIO 13.  If voltage is high (>1023), it's connected
// to battery → V3.  If low, GPIO 13 is unused → V4.
// Result is cached in NVS so detection only runs once.

uint8_t detect_board_version()
{
    Preferences pref;
    pref.begin("morserino", false);
    uint8_t ver = pref.getUChar("boardVersion", 0);
    if (ver == 0) {
        const int probe_pin = CONFIG_BATMEAS_PIN_V3;  // GPIO 13
        analogSetAttenuation(ADC_0db);
        adcAttachPin(probe_pin);
        analogSetClockDiv(128);
        analogSetPinAttenuation(probe_pin, ADC_11db);
        if (analogRead(probe_pin) > 1023) {
            ver = 3;
        } else {
            ver = 4;
        }
        pref.putUChar("boardVersion", ver);
        Log.noticeln("Board version detected: V%d (saved to NVS)", ver);
    } else {
        Log.noticeln("Board version from NVS: V%d", ver);
    }
    pref.end();
    return ver;
}

// ── Key input ────────────────────────────────────────────────────────────────

OriginalM32KeyInput::OriginalM32KeyInput(uint32_t touch_l_idle,
                                         uint32_t touch_r_idle,
                                         uint8_t board_version)
{
    // ESP32 touch: lower = touched.  Compute thresholds below idle.
    touch_l_on_  = (uint32_t)(touch_l_idle * SENS_ON);
    touch_l_off_ = (uint32_t)(touch_l_idle * SENS_OFF);
    touch_r_on_  = (uint32_t)(touch_r_idle * SENS_ON);
    touch_r_off_ = (uint32_t)(touch_r_idle * SENS_OFF);

    Log.noticeln("Touch thresholds: L idle=%d on<%d off>%d  R idle=%d on<%d off>%d",
                 touch_l_idle, touch_l_on_, touch_l_off_,
                 touch_r_idle, touch_r_on_, touch_r_off_);

    // External paddle pins depend on board version
    if (board_version == 3) {
        paddle_left_pin_  = PIN_PADDLE_RIGHT_V4;  // V3: swapped
        paddle_right_pin_ = PIN_PADDLE_LEFT_V4;
    } else {
        paddle_left_pin_  = PIN_PADDLE_LEFT_V4;   // V4: normal
        paddle_right_pin_ = PIN_PADDLE_RIGHT_V4;
    }
    Log.noticeln("External paddle: dit=GPIO%d dah=GPIO%d (board V%d)",
                 paddle_left_pin_, paddle_right_pin_, board_version);

    event_queue_ = xQueueCreate(QUEUE_DEPTH, sizeof(KeyEvent));

    // Keyer output
    pinMode(PIN_KEYER, OUTPUT);
    digitalWrite(PIN_KEYER, LOW);

    // External paddle pins — board has external pull-ups
    pinMode(paddle_left_pin_, INPUT);
    pinMode(paddle_right_pin_, INPUT);

    // Buttons (touch pins NOT set with pinMode — touch peripheral manages them)
    pinMode(PIN_ROT_BTN, INPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // Encoder (PCNT-based)
    encoder_.attachHalfQuad(PIN_ROT_DT, PIN_ROT_CLK);
    encoder_.setCount(0);

    // Start polling task on core 0
    xTaskCreatePinnedToCore(poll_task_entry, "keys", 2048, this, 5,
                            &poll_task_handle_, 0);
}

OriginalM32KeyInput::~OriginalM32KeyInput()
{
    if (poll_task_handle_) vTaskDelete(poll_task_handle_);
    if (event_queue_) vQueueDelete(event_queue_);
}

bool OriginalM32KeyInput::poll(KeyEvent& out)
{
    return xQueueReceive(event_queue_, &out, 0) == pdTRUE;
}

bool OriginalM32KeyInput::wait(KeyEvent& out, uint32_t timeout_ms)
{
    return xQueueReceive(event_queue_, &out,
                         pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void OriginalM32KeyInput::push(KeyEvent ev)
{
    xQueueSend(event_queue_, &ev, 0);
}

void OriginalM32KeyInput::push_release(KeyEvent ev)
{
    xQueueSendToFront(event_queue_, &ev, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
}

void OriginalM32KeyInput::update_button(ButtonState& s, bool now_pressed,
                                        uint32_t now_ms,
                                        KeyEvent short_ev, KeyEvent long_ev)
{
    // Debounce: require DEBOUNCE_READS consecutive matching reads
    if (now_pressed != s.pressed) {
        if (now_pressed == s.pending) {
            if (++s.stable_cnt >= DEBOUNCE_READS) {
                // State change confirmed
                s.pressed = now_pressed;
                s.stable_cnt = 0;
                if (s.pressed) {
                    s.press_ms = now_ms;
                    s.long_done = false;
                } else {
                    if (!s.long_done) push(short_ev);
                }
            }
        } else {
            s.pending = now_pressed;
            s.stable_cnt = 1;
        }
    } else {
        s.stable_cnt = 0;
        s.pending = s.pressed;
    }
    // Long press detection
    if (s.pressed && !s.long_done &&
        (now_ms - s.press_ms) >= LONG_PRESS_MS) {
        s.long_done = true;
        push(long_ev);
    }
}

void OriginalM32KeyInput::poll_task_body()
{
    ButtonState enc_btn, aux_btn;
    bool touch_l_down = false, touch_r_down = false;
    uint32_t touch_l_start = 0, touch_r_start = 0;
    bool paddle_l_down = false, paddle_r_down = false;

    while (true) {
        uint32_t now = (uint32_t)millis();

        // ── Touch pads (ESP32: lower touchRead = touched) ────────────────
        uint32_t tl = touchRead(PIN_TOUCH_LEFT);
        uint32_t tr = touchRead(PIN_TOUCH_RIGHT);

        if (!touch_l_down && tl < touch_l_on_) {
            touch_l_down = true;
            touch_l_start = now;
            push(KeyEvent::TOUCH_LEFT_DOWN);
        } else if (touch_l_down && tl > touch_l_off_) {
            touch_l_down = false;
            push_release(KeyEvent::TOUCH_LEFT_UP);
        } else if (touch_l_down && (now - touch_l_start) >= MAX_TOUCH_HOLD_MS) {
            touch_l_down = false;
            push_release(KeyEvent::TOUCH_LEFT_UP);
        }

        if (!touch_r_down && tr < touch_r_on_) {
            touch_r_down = true;
            touch_r_start = now;
            push(KeyEvent::TOUCH_RIGHT_DOWN);
        } else if (touch_r_down && tr > touch_r_off_) {
            touch_r_down = false;
            push_release(KeyEvent::TOUCH_RIGHT_UP);
        } else if (touch_r_down && (now - touch_r_start) >= MAX_TOUCH_HOLD_MS) {
            touch_r_down = false;
            push_release(KeyEvent::TOUCH_RIGHT_UP);
        }

        // ── External paddle jack (separate GPIO pins, active LOW) ────────
        bool pl = (digitalRead(paddle_left_pin_) == LOW);
        if (pl && !paddle_l_down) {
            paddle_l_down = true;
            push(KeyEvent::PADDLE_DIT_DOWN);
        } else if (!pl && paddle_l_down) {
            paddle_l_down = false;
            push_release(KeyEvent::PADDLE_DIT_UP);
        }

        bool pr = (digitalRead(paddle_right_pin_) == LOW);
        if (pr && !paddle_r_down) {
            paddle_r_down = true;
            push(KeyEvent::PADDLE_DAH_DOWN);
        } else if (!pr && paddle_r_down) {
            paddle_r_down = false;
            push_release(KeyEvent::PADDLE_DAH_UP);
        }

        // ── Encoder ──────────────────────────────────────────────────────
        int32_t cnt = (int32_t)encoder_.getCount();
        if (cnt >= STEPS_PER_DETENT) {
            encoder_.setCount(0);
            push(KeyEvent::ENCODER_CW);
        } else if (cnt <= -STEPS_PER_DETENT) {
            encoder_.setCount(0);
            push(KeyEvent::ENCODER_CCW);
        }

        // ── Buttons ──────────────────────────────────────────────────────
        update_button(enc_btn, digitalRead(PIN_ROT_BTN) == HIGH, now,
                      KeyEvent::BUTTON_ENCODER_SHORT, KeyEvent::BUTTON_ENCODER_LONG);
        update_button(aux_btn, digitalRead(PIN_BUTTON) == LOW, now,
                      KeyEvent::BUTTON_AUX_SHORT, KeyEvent::BUTTON_AUX_LONG);

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void OriginalM32KeyInput::poll_task_entry(void* arg)
{
    static_cast<OriginalM32KeyInput*>(arg)->poll_task_body();
}

#endif // BOARD_ORIGINAL_M32
