#pragma once

#include <cstdint>

// Display abstraction used by the LVGL integration layer.
//
// Pocket target: ST7789 170x320 via LovyanGFX (haklein/DisplayWrapper).
// M5Core2 target: ILI9342C 320x240 via M5Unified / LovyanGFX.
//
// Application code uses LVGL APIs directly.  This interface exists so the
// LVGL flush callback and touch read callback can be wired without knowing
// which platform is running.
class IDisplay
{
public:
    virtual ~IDisplay() = default;

    // Initialise display hardware and backlight.
    virtual void begin() = 0;

    // Backlight brightness 0–255.
    virtual void set_brightness(uint8_t level) = 0;

    // Enter display sleep (backlight off, panel sleep command).
    virtual void sleep() = 0;

    // Wake display from sleep.
    virtual void wake() = 0;

    virtual uint16_t width()     const = 0;
    virtual uint16_t height()    const = 0;
    virtual bool     has_touch() const = 0;

    // Called by the LVGL flush callback.
    // Implementations must call lv_display_flush_ready() when done.
    virtual void flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       const uint16_t* color_map) = 0;

    // Called by the LVGL touch read callback.
    // Returns true and sets x/y if a touch point is active.
    virtual bool get_touch(int16_t& x, int16_t& y) = 0;
};
