#pragma once

#ifdef BOARD_POCKETWROOM

// Pocket (ESP32-S3) audio output: I2S sine tone via haklein/cw-i2s-sidetone,
// codec init/control via haklein/tlv320aic31xx.
//
// Init sequence (critical — BCLK must run before PLL enable):
//   1. Wire.begin() on codec SDA/SCL
//   2. Hardware-reset codec via RST pin
//   3. Configure codec registers (PLL params, word length) — PLL not yet on
//   4. sidetone_.begin() → I2S starts → BCLK begins
//   5. Enable codec PLL (locks on BCLK), then NDAC/MDAC, then DAC + outputs
//
// PLL target for 48 kHz with BCLK = 1.536 MHz as source:
//   P=1, R=4, J=16, D=0  →  PLL_CLK = 98.304 MHz
//   NDAC=4, MDAC=4, DOSR=128

#include "../interfaces/i_audio_output.h"
#include <I2S_Sidetone.hpp>
#include <tlv320aic31xx_codec.h>
#include <Wire.h>

class PocketAudioOutput : public IAudioOutput
{
public:
    PocketAudioOutput();
    ~PocketAudioOutput() override = default;

    void begin() override;
    void tone_on(uint16_t frequency_hz) override;
    void tone_off() override;
    void set_volume(uint8_t level) override;
    void set_adsr(float attack_s, float decay_s,
                  float sustain_level, float release_s) override;
    void suspend() override;
    void poll() override;
    void play_effect(SoundEffect effect) override;

    void enable_adc() override;
    void disable_adc() override;
    size_t read_audio(int16_t* buf, size_t max_samples) override;

private:
    I2S_Sidetone   sidetone_;
    TLV320AIC31xx  codec_;
    uint16_t       current_freq_ = 700;

    void codec_init_clocking();
    void codec_enable_outputs();
    void handle_headset_event();
};

#endif // BOARD_POCKETWROOM
