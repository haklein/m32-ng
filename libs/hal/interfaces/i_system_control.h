#pragma once

#include <cstdint>

// System-level control: sleep, restart, time, power, entropy.
//
// Pocket target:  ESP32-S3 deep sleep; MCP73871 charger monitoring via ADC/GPIO.
// M5Core2 target: AXP192 PMIC via M5Unified; light sleep with touch wakeup.
// Native target:  uptime_ms() from steady_clock; deep_sleep() is a no-op.
class ISystemControl
{
public:
    virtual ~ISystemControl() = default;

    // Enter deep sleep.  wakeup_gpio_mask is a bitmask of GPIO numbers that
    // should act as wakeup sources (platform-specific interpretation).
    virtual void deep_sleep(uint32_t wakeup_gpio_mask) = 0;

    // Reboot the device.
    virtual void restart() = 0;

    // Monotonic millisecond counter since boot.
    virtual uint32_t uptime_ms() = 0;

    // Battery state-of-charge 0–100; returns 255 if not measurable.
    virtual uint8_t battery_percent() = 0;

    // True while USB / charger power is connected.
    virtual bool is_charging() = 0;

    // Control an external power rail (e.g. VEXT on Pocket, SPK_EN on Core2).
    virtual void set_power_rail(bool on) = 0;

    // Hardware entropy source for seeding the application RNG.
    // Returns a 32-bit value derived from hardware (ADC noise, boot time, etc.).
    virtual uint32_t entropy_seed() = 0;
};
