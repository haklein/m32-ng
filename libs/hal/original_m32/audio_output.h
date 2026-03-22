#pragma once

#ifdef BOARD_ORIGINAL_M32

// Original Morserino-32 (Heltec V2) audio output: PWM tone via LEDC channels.
//
// Two-stage modulation:
//   LF channel (CONFIG_LF_PIN, GPIO 23): audible tone via ledcWriteTone()
//   HF channel (CONFIG_HF_PIN, GPIO 22): 32 kHz PWM for volume control
//   Line-out  (CONFIG_LINEOUT_PIN, GPIO 17): same tone as LF channel
//
// Volume is controlled by the HF channel duty cycle (10-bit, 0-1023).
// Soft start/stop ramps prevent audio clicks.

#include "../interfaces/i_audio_output.h"

class OriginalM32AudioOutput : public IAudioOutput
{
public:
    OriginalM32AudioOutput() = default;
    ~OriginalM32AudioOutput() override = default;

    void begin() override;
    void tone_on(uint16_t frequency_hz) override;
    void tone_off() override;
    void set_volume(uint8_t level) override;
    void set_adsr(float, float, float, float) override {} // no-op for PWM
    void suspend() override;
    void play_effect(SoundEffect effect) override;

private:
    // Pin-based LEDC API — pins defined via build flags
    static constexpr uint16_t VOL_TABLE[20] = {
        0, 1, 2, 4, 6, 9, 14, 21, 31, 45,
        70, 100, 140, 200, 280, 390, 512, 680, 840, 1023
    };

    uint16_t current_freq_ = 700;
    uint8_t  current_vol_  = 14;
    bool     tone_active_  = false;

    void volume_ramp_up();
    void volume_ramp_down();
};

#endif // BOARD_ORIGINAL_M32
