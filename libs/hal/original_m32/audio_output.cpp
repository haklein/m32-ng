#ifdef BOARD_ORIGINAL_M32

#include "audio_output.h"
#include <Arduino.h>

// Channel-based LEDC API (ESP32 Arduino Core).
// Silence = duty 0 on tone channels (not frequency 0).

static constexpr uint8_t TONE_CH    = 0;
static constexpr uint8_t VOLUME_CH  = 1;
static constexpr uint8_t LINEOUT_CH = 2;

void OriginalM32AudioOutput::begin()
{
    // Volume channel: 32 kHz, 10-bit resolution — starts muted
    ledcSetup(VOLUME_CH, 32000, 10);
    ledcAttachPin(CONFIG_HF_PIN, VOLUME_CH);
    ledcWrite(VOLUME_CH, 0);

    // Tone channels: set up at nominal freq, duty 0 = silent
    ledcSetup(TONE_CH, 1000, 10);
    ledcAttachPin(CONFIG_LF_PIN, TONE_CH);
    ledcWrite(TONE_CH, 0);

    ledcSetup(LINEOUT_CH, 1000, 10);
    ledcAttachPin(CONFIG_LINEOUT_PIN, LINEOUT_CH);
    ledcWrite(LINEOUT_CH, 0);
}

void OriginalM32AudioOutput::tone_on(uint16_t frequency_hz)
{
    current_freq_ = frequency_hz;
    // ledcWriteTone sets frequency AND 50% duty
    ledcWriteTone(TONE_CH, frequency_hz);
    ledcWriteTone(LINEOUT_CH, frequency_hz);
    if (!tone_active_) {
        tone_active_ = true;
        volume_ramp_up();
    }
}

void OriginalM32AudioOutput::tone_off()
{
    if (tone_active_) {
        tone_active_ = false;
        volume_ramp_down();
    }
    // Silence: set duty to 0 (not frequency to 0)
    ledcWrite(TONE_CH, 0);
    ledcWrite(LINEOUT_CH, 0);
}

void OriginalM32AudioOutput::set_volume(uint8_t level)
{
    if (level > 19) level = 19;
    current_vol_ = level;
    if (tone_active_) {
        ledcWrite(VOLUME_CH, VOL_TABLE[level]);
    }
}

void OriginalM32AudioOutput::suspend()
{
    tone_off();
    ledcDetachPin(CONFIG_LF_PIN);
    ledcDetachPin(CONFIG_HF_PIN);
    ledcDetachPin(CONFIG_LINEOUT_PIN);
}

void OriginalM32AudioOutput::play_effect(SoundEffect effect)
{
    auto beep = [this](uint16_t hz, uint32_t ms) {
        tone_on(hz);
        delay(ms);
        tone_off();
        delay(50);
    };
    if (effect == SoundEffect::SUCCESS) {
        beep(440, 100);
        beep(587, 100);
    } else {
        beep(311, 150);
        beep(330, 150);
    }
}

void OriginalM32AudioOutput::volume_ramp_up()
{
    uint16_t target = VOL_TABLE[current_vol_];
    for (uint16_t v = 0; v <= target; v += (target / 8 + 1)) {
        ledcWrite(VOLUME_CH, v > target ? target : v);
        delayMicroseconds(200);
    }
    ledcWrite(VOLUME_CH, target);
}

void OriginalM32AudioOutput::volume_ramp_down()
{
    uint16_t start = VOL_TABLE[current_vol_];
    for (int v = (int)start; v >= 0; v -= (start / 8 + 1)) {
        ledcWrite(VOLUME_CH, v < 0 ? 0 : (uint16_t)v);
        delayMicroseconds(200);
    }
    ledcWrite(VOLUME_CH, 0);
}

constexpr uint16_t OriginalM32AudioOutput::VOL_TABLE[20];

#endif // BOARD_ORIGINAL_M32
