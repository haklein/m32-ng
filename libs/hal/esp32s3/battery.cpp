#ifdef BOARD_POCKETWROOM

#include "battery.h"
#include <Arduino.h>
#include <algorithm>

void PocketBattery::begin()
{
    adcAttachPin(CONFIG_BATMEAS_PIN);
    analogReadResolution(12);
    analogSetPinAttenuation(CONFIG_BATMEAS_PIN, ADC_6db);

    pinMode(CONFIG_MCP_PG_PIN, INPUT_PULLUP);
    pinMode(CONFIG_MCP_STAT1_PIN, INPUT_PULLUP);
    pinMode(CONFIG_MCP_STAT2_PIN, INPUT_PULLUP);
}

uint8_t PocketBattery::percent()
{
    // Average 10 ADC samples for stability
    float mv = 0;
    for (int i = 0; i < 10; ++i)
        mv += 1750.0f * (analogRead(CONFIG_BATMEAS_PIN) / 4095.0f);
    mv /= 10.0f;

    int bat_mv = (int)(mv * 3.43f);  // voltage divider ratio (from M32 codebase)
    int pct = (bat_mv - 3400) * 100 / (4200 - 3400);  // 3.4V=0%, 4.2V=100%
    return (uint8_t)std::max(0, std::min(100, pct));
}

bool PocketBattery::charging()
{
    uint8_t state = (digitalRead(CONFIG_MCP_STAT1_PIN) << 2)
                  | (digitalRead(CONFIG_MCP_STAT2_PIN) << 1)
                  | digitalRead(CONFIG_MCP_PG_PIN);
    return state == 2;  // MCP73871: STAT1=L STAT2=H PG=L → charging
}

#endif // BOARD_POCKETWROOM
