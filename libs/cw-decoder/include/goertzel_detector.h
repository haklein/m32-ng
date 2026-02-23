#pragma once

// Adapted from m5core2-cwtrainer/lib/Goertzel (originally from Morserino-32,
// algorithm by Hjalmar Skovholm Hansen OZ1JHM).
// Changes: removed Arduino / analogRead dependency; caller feeds pre-sampled
// buffers via process_block().  All algorithm and adaptive threshold logic
// preserved exactly.

#include <cstdint>
#include <cstddef>

// Detects a fixed-frequency tone in a block of PCM samples using the
// Goertzel algorithm with an adaptive magnitude threshold.
//
// Usage:
//   GoertzelDetector det;
//   det.setup(target_hz, sample_rate_hz, bandwidth_narrow);
//   // In audio callback / task:
//   bool tone_present = det.process_block(samples, count);
class GoertzelDetector
{
public:
    GoertzelDetector() = default;

    // Precompute filter coefficients.
    // target_freq_hz   : CW tone frequency to detect (e.g. 698 Hz)
    // sample_rate_hz   : ADC sample rate (e.g. 8000 Hz; original ESP32 ADC ~106000)
    // narrow_bandwidth : true = ~175 Hz BW (608 samples), false = ~700 Hz (152 samples)
    void setup(float target_freq_hz, float sample_rate_hz, bool narrow_bandwidth = false);

    // Process one block of 16-bit signed PCM samples.
    // Returns true if the target tone is detected above the adaptive threshold.
    // block_size must equal the goertzel_n configured in setup().
    bool process_block(const int16_t* samples, size_t count);

    // Current adaptive magnitude limit (useful for calibration / display).
    float magnitude_limit() const { return magnitude_limit_; }

private:
    float coeff_ = 0.0f;
    float sine_ = 0.0f;
    float cosine_ = 0.0f;
    int   goertzel_n_ = 152;
    float magnitude_limit_ = 0.0f;
    float magnitude_limit_low_ = 0.0f;
};
