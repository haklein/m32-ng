#pragma once

#ifdef BOARD_POCKETWROOM

#include <lvgl.h>

// Render a QR code encoding `text` onto a new LVGL canvas widget.
// max_px: maximum side length in pixels (canvas will be square).
// Only one QR canvas may exist at a time; call qr_canvas_destroy() first.
lv_obj_t* qr_canvas_create(lv_obj_t* parent, const char* text,
                            int32_t max_px);

// Free the internal draw buffer.  Call before the canvas is deleted.
void qr_canvas_destroy();

#endif // BOARD_POCKETWROOM
