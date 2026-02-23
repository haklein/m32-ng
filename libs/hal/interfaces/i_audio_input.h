#pragma once

#include <cstdint>
#include <functional>

// Audio input for CW decoder (Goertzel tone detection).
//
// Pocket target: I2S DATA_IN from TLV320AIC3100 ADC (mic/line input).
// M5Core2 target: not available on base hardware — implementation returns
//                 signal_level() = 0 and never fires the callback.
class IAudioInput
{
public:
    virtual ~IAudioInput() = default;

    // Initialise and start sampling at the given rate (e.g. 8000 Hz).
    virtual void begin(uint32_t sample_rate_hz) = 0;

    // Current normalised signal power 0–100 (snapshot, not block-averaged).
    virtual uint8_t signal_level() = 0;

    // Callback fired whenever carrier detect state transitions.
    // Implementations call this from their internal audio task.
    using DetectCallback = std::function<void(bool carrier_present)>;
    virtual void set_detect_callback(DetectCallback cb) = 0;
};
