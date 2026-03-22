#pragma once

#ifdef BOARD_ORIGINAL_M32

// Original Morserino-32 (Heltec V2) battery measurement.
//
// V3 board: battery ADC on GPIO 13
// V4 board: battery ADC on GPIO 37
// Board version detected at runtime.

#include <cstdint>

class OriginalM32Battery
{
public:
    void set_board_version(uint8_t ver) { board_version_ = ver; }
    void begin();

    uint8_t percent();
    int raw_mv();
    int compensated_mv();
    bool charging();
    uint8_t charger_state();
    float comp_factor() const { return 1.0f; }
    void poll_calibration() {}

private:
    int read_raw_mv();
    uint8_t board_version_ = 4;
    uint8_t bat_pin_ = CONFIG_BATMEAS_PIN_V4;
};

#endif // BOARD_ORIGINAL_M32
