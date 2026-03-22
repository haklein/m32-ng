#ifdef BOARD_ORIGINAL_M32

#include "battery.h"
#include <Arduino.h>
#include <ArduinoLog.h>

static constexpr float VOLT_CALIBRATE = 4.33f;

void OriginalM32Battery::begin()
{
    bat_pin_ = (board_version_ == 3) ? CONFIG_BATMEAS_PIN_V3
                                     : CONFIG_BATMEAS_PIN_V4;
    Log.noticeln("Battery ADC: GPIO%d (board V%d)", bat_pin_, board_version_);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
}

int OriginalM32Battery::read_raw_mv()
{
    int raw = analogRead(bat_pin_);
    // On V4, bat_pin_ (37) is shared with encoder button.
    // Re-init as digital input after ADC read so button polling works.
    if (bat_pin_ == PIN_ROT_BTN)
        pinMode(bat_pin_, INPUT);
    return (int)(raw / 4095.0f * 3300.0f * VOLT_CALIBRATE / 3.3f);
}

int OriginalM32Battery::raw_mv()
{
    int sum = 0;
    for (int i = 0; i < 10; i++) sum += read_raw_mv();
    return sum / 10;
}

int OriginalM32Battery::compensated_mv()
{
    return raw_mv();
}

uint8_t OriginalM32Battery::percent()
{
    int mv = compensated_mv();
    if (mv >= 4200) return 100;
    if (mv <= 3400) return 0;
    return (uint8_t)((mv - 3400) * 100 / 800);
}

bool OriginalM32Battery::charging()
{
    return false;
}

uint8_t OriginalM32Battery::charger_state()
{
    return 3;
}

#endif // BOARD_ORIGINAL_M32
