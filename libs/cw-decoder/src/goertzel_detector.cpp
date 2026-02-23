#include "goertzel_detector.h"
#include <cmath>

// Adapted from m5core2-cwtrainer/lib/Goertzel / Morserino-32 MorseDecoder.
// Algorithmic content preserved exactly; analogRead() replaced with an
// explicit sample buffer so this file has no platform dependency.

void GoertzelDetector::setup(float target_freq_hz,
                             float sample_rate_hz,
                             bool  narrow_bandwidth)
{
    // Narrow bandwidth uses 608 samples (~175 Hz BW); wide uses 152 (~700 Hz).
    // Values mirror the original: 152/608 at 106 kHz ADC for 698 Hz tone.
    // At lower sample rates (e.g. 8 kHz I2S) the caller should pass an
    // appropriate goertzel_n; here we scale proportionally.
    const float base_sample_rate = 106000.0f;
    const float scale = sample_rate_hz / base_sample_rate;
    goertzel_n_ = narrow_bandwidth
        ? static_cast<int>(608.0f * scale + 0.5f)
        : static_cast<int>(152.0f * scale + 0.5f);
    if (goertzel_n_ < 16) goertzel_n_ = 16;  // sanity floor

    magnitude_limit_low_ = narrow_bandwidth ? 160000.0f : 40000.0f;
    magnitude_limit_ = magnitude_limit_low_;

    int k = static_cast<int>(0.5f + (goertzel_n_ * target_freq_hz) / sample_rate_hz);
    float omega = (2.0f * static_cast<float>(M_PI) * k) / goertzel_n_;
    sine_   = sinf(omega);
    cosine_ = cosf(omega);
    coeff_  = 2.0f * cosine_;
}

bool GoertzelDetector::process_block(const int16_t* samples, size_t count)
{
    float Q1 = 0.0f;
    float Q2 = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float Q0 = coeff_ * Q1 - Q2 + static_cast<float>(samples[i]);
        Q2 = Q1;
        Q1 = Q0;
    }

    float magnitude_sq = Q1 * Q1 + Q2 * Q2 - Q1 * Q2 * coeff_;
    float magnitude = sqrtf(magnitude_sq);

    // Adaptive threshold: track a moving average of recent peak magnitudes.
    if (magnitude > magnitude_limit_low_) {
        magnitude_limit_ += (magnitude - magnitude_limit_) / 6.0f;
    }
    if (magnitude_limit_ < magnitude_limit_low_) {
        magnitude_limit_ = magnitude_limit_low_;
    }

    return magnitude > magnitude_limit_ * 0.6f;
}
