#pragma once

#include <cstdint>
#include <cstddef>

// Sound effects for training feedback (echo trainer OK/ERR).
enum class SoundEffect : uint8_t { SUCCESS, ERROR };

// Sidetone and keyed-output audio abstraction.
//
// Pocket target: I2S -> TLV320AIC3100 codec (haklein/cw-i2s-sidetone +
//                haklein/tlv320aic31xx), speaker/headphone auto-switching.
// M5Core2 target: I2S -> internal Class-D amp via AXP192 SPK_EN.
class IAudioOutput
{
public:
    virtual ~IAudioOutput() = default;

    // Initialise hardware; must be called once before any other method.
    // On Pocket: codec I2C setup must precede I2S start (BCLK feeds codec PLL).
    virtual void begin() = 0;

    // Start tone at given frequency in Hz (ADSR attack applied).
    virtual void tone_on(uint16_t frequency_hz) = 0;

    // Stop tone (ADSR release applied).
    virtual void tone_off() = 0;

    // Set output volume; 0 = mute, 20 = maximum.
    virtual void set_volume(uint8_t level) = 0;

    // Configure ADSR envelope for the sidetone sine wave.
    virtual void set_adsr(float attack_s, float decay_s,
                          float sustain_level, float release_s) = 0;

    // Power down audio hardware before deep sleep.
    virtual void suspend() = 0;

    // Poll for asynchronous hardware events (e.g. headphone plug/unplug).
    // Call from the main loop.  Default is a no-op.
    virtual void poll() {}

    // Play a short sound effect (e.g. echo trainer OK/ERR).
    // Pocket: tries SPIFFS MP3, falls back to tone sequence.
    // Native: plays tone sequence via ALSA.
    // Default is a no-op for platforms without audio effects.
    virtual void play_effect(SoundEffect /*effect*/) {}

    // ── Audio input (ADC) for CW decoder ────────────────────────────────────
    // Enable codec ADC path with MIC PGA, AGC, and input routing.
    virtual void enable_adc() {}

    // Disable codec ADC path and power down input stages.
    virtual void disable_adc() {}

    // Read I2S RX samples (interleaved stereo, 16-bit signed).
    // Returns number of samples actually read (may be less than max_samples).
    virtual size_t read_audio(int16_t* buf, size_t max_samples) { (void)buf; return 0; }
};
