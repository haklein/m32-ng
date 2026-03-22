#include <Arduino.h>
#include <LovyanGFX.hpp>

#ifdef BOARD_POCKETWROOM
#include <display/ST7789.h>
#elif defined(BOARD_ORIGINAL_M32)
#include <display/SSD1306.h>
#endif

