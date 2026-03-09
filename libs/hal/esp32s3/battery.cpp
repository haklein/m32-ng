#ifdef BOARD_POCKETWROOM

#include "battery.h"
#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>

static const char* NVS_NS  = "battery";
static const char* NVS_KEY = "comp";

void PocketBattery::begin()
{
    adcAttachPin(CONFIG_BATMEAS_PIN);
    analogReadResolution(12);
    analogSetPinAttenuation(CONFIG_BATMEAS_PIN, ADC_6db);

    pinMode(CONFIG_MCP_PG_PIN, INPUT_PULLUP);
    pinMode(CONFIG_MCP_STAT1_PIN, INPUT_PULLUP);
    pinMode(CONFIG_MCP_STAT2_PIN, INPUT_PULLUP);

    // Load saved compensation factor from NVS
    Preferences prefs;
    if (prefs.begin(NVS_NS, true)) {
        float stored = prefs.getFloat(NVS_KEY, 0.0f);
        prefs.end();
        if (stored > 0.5f && stored < 2.0f) {
            comp_factor_ = stored;
            calibrated_ = true;
        }
    }
}

float PocketBattery::read_raw_mv()
{
    // Average 10 ADC samples for stability
    float mv = 0;
    for (int i = 0; i < 10; ++i)
        mv += 1750.0f * (analogRead(CONFIG_BATMEAS_PIN) / 4095.0f);
    mv /= 10.0f;
    return mv * 3.43f;  // nominal voltage divider ratio
}

int PocketBattery::raw_mv()
{
    return (int)read_raw_mv();
}

int PocketBattery::compensated_mv()
{
    return (int)(read_raw_mv() * comp_factor_);
}

uint8_t PocketBattery::percent()
{
    int mv = compensated_mv();
    int pct = (mv - 3400) * 100 / (4200 - 3400);
    return (uint8_t)std::max(0, std::min(100, pct));
}

bool PocketBattery::charging()
{
    return charger_state() == 2;
}

uint8_t PocketBattery::charger_state()
{
    return (digitalRead(CONFIG_MCP_STAT1_PIN) << 2)
         | (digitalRead(CONFIG_MCP_STAT2_PIN) << 1)
         | digitalRead(CONFIG_MCP_PG_PIN);
}

void PocketBattery::poll_calibration()
{
    // State 4 = charge complete → battery is at 4200 mV
    if (charger_state() != 4) return;

    float raw = read_raw_mv();
    if (raw < 3000.0f) return;  // bogus reading, ignore

    float new_factor = 4200.0f / raw;
    // Sanity: factor should be close to 1.0 (within ±30%)
    if (new_factor < 0.7f || new_factor > 1.3f) return;

    comp_factor_ = new_factor;
    calibrated_ = true;

    // Persist to NVS
    Preferences prefs;
    if (prefs.begin(NVS_NS, false)) {
        prefs.putFloat(NVS_KEY, comp_factor_);
        prefs.end();
    }
}

#endif // BOARD_POCKETWROOM
