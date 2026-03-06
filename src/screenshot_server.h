#pragma once

#ifdef BOARD_POCKETWROOM

// Lightweight HTTP server for LVGL screenshot capture.
// Serves /screenshot.bmp — a 320x170 RGB565 BMP of the active screen.
//
// Call screenshot_server_start() after WiFi connects.
// Call screenshot_server_update() every iteration of the LVGL task
// (it only does work when a screenshot has been requested via HTTP).

void screenshot_server_start();
void screenshot_server_stop();
void screenshot_server_update();   // call from LVGL task context

#endif
