#pragma once

#ifdef BOARD_POCKETWROOM

// Pocket (ESP32-S3) battery measurement via ADC + MCP73871 charger status.
//
// Voltage divider: R14=1M / R15=470K  →  multiplier ~3.43
// ADC: GPIO CONFIG_BATMEAS_PIN, 12-bit, 6 dB attenuation (~1750 mV max)
// Battery range: single-cell LiIon, 4.2 V (full) – 3.4 V (cutoff)
//
// MCP73871 power-path state from three GPIO pins:
//   state = (STAT1 << 2) | (STAT2 << 1) | PG
//   2 = Charging, 4 = Charge complete, 3 = On battery, 6 = No battery

#include <cstdint>

class PocketBattery
{
public:
    void begin();

    // 0–100 percentage, clamped.
    uint8_t percent();

    // True while MCP73871 reports charging (state == 2).
    bool charging();
};

#endif // BOARD_POCKETWROOM
