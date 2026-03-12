#pragma once

#ifdef BOARD_POCKETWROOM

// Thin serial JSON bridge — exposes the same API as the web server over USB CDC.
// Line protocol: "METHOD /path [json-body]\n" → JSON response + "\n"
// Non-command lines (e.g. ArduinoLog output) are ignored.

void serial_bridge_init();
void serial_bridge_poll();   // call from main loop

#endif
