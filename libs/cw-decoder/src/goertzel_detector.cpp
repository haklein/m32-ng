#include "goertzel_detector.h"
#include <cmath>

// Adapted from Morserino-32 v6 goertzel.cpp (I2S path).
// Threshold values and adaptive smoothing matched to the original.

namespace cw {

void GoertzelDetector::setup(float target_freq_hz,
                             float sample_rate_hz,
                             bool  narrow_bandwidth)
{
    // Original Morserino I2S mode at 44.1 kHz:
    //   narrow BW (175 Hz) → N=252,  magnitudelimit_low = 15000
    //   wide   BW (700 Hz) → N=63,   magnitudelimit_low = 3800
    // Scale N proportionally to actual sample rate.
    const float base_i2s_rate = 44100.0f;
    const float scale = sample_rate_hz / base_i2s_rate;
    goertzel_n_ = narrow_bandwidth
        ? static_cast<int>(252.0f * scale + 0.5f)
        : static_cast<int>(63.0f * scale + 0.5f);
    if (goertzel_n_ < 16) goertzel_n_ = 16;

    magnitude_limit_low_ = narrow_bandwidth ? 15000.0f : 3800.0f;
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
    last_magnitude_ = magnitude;

    // Adaptive threshold — matched to original Morserino v6 goertzel.cpp:
    //   magnitudelimit = magnitudelimit * 0.95 + (magnitude - magnitudelimit) / 4
    // Only updates when signal exceeds the noise floor.
    if (magnitude > magnitude_limit_low_) {
        magnitude_limit_ = magnitude_limit_ * 0.95f
                         + (magnitude - magnitude_limit_) / 4.0f;
    }
    if (magnitude_limit_ < magnitude_limit_low_) {
        magnitude_limit_ = magnitude_limit_low_;
    }

    return magnitude > magnitude_limit_ * 0.6f;
}

} // namespace cw
