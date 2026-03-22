#pragma once

#ifdef BOARD_EMBEDDED

// HTTP server: config API, mode control, screenshots, plain-text UI.
//
// Call web_server_start() after WiFi connects.
// Call web_server_update() every iteration of the LVGL task.

void web_server_start();
void web_server_stop();
void web_server_update();   // call from LVGL task context

#endif
