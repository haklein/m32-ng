#pragma once

#ifdef BOARD_POCKETWROOM

// Pocket (ESP32-S3) battery measurement via ADC + MCP73871 charger status.
//
// Voltage divider: R14=1M / R15=470K  →  nominal multiplier ~3.43
// ADC: GPIO CONFIG_BATMEAS_PIN, 12-bit, 6 dB attenuation (~1750 mV max)
// Battery range: single-cell LiIon, 4.2 V (full) – 3.4 V (cutoff)
//
// Auto-calibration: when the MCP73871 signals "charge complete" (state 4),
// the battery voltage is known to be 4200 mV.  A compensation factor is
// computed from the raw ADC reading and persisted in NVS.
//
// MCP73871 power-path state from three GPIO pins:
//   state = (STAT1 << 2) | (STAT2 << 1) | PG
//   2 = Charging, 4 = Charge complete, 3 = On battery, 6 = No battery

#include <cstdint>

class PocketBattery
{
public:
    void begin();

    // 0–100 percentage (using compensated voltage), clamped.
    uint8_t percent();

    // Raw battery voltage (nominal divider ratio, no compensation).
    int raw_mv();

    // Compensated battery voltage (calibrated via charge-complete).
    int compensated_mv();

    // True while MCP73871 reports charging (state == 2).
    bool charging();

    // MCP73871 state code (for diagnostics).
    uint8_t charger_state();

    // Current compensation factor (1.0 = no compensation).
    float comp_factor() const { return comp_factor_; }

    // Poll charger state and auto-calibrate if charge complete.
    // Call periodically (e.g. from battery poll loop).
    void poll_calibration();

private:
    float read_raw_mv();
    float comp_factor_ = 1.0f;
    bool  calibrated_  = false;
};

#endif // BOARD_POCKETWROOM
