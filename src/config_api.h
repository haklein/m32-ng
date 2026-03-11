#pragma once

// Config API — JSON settings access for the web server.
// Implemented in app_ui.hpp (where AppSettings lives).

#include <cstdint>
#include <cstddef>

// Field type for web UI rendering
enum class FieldType : uint8_t {
    INT,        // integer spinbox
    BOOL,       // checkbox
    ENUM,       // dropdown (options in pipe-separated string)
    STRING,     // text input
};

// Describes one setting field for JSON serialization and web UI generation.
struct FieldMeta {
    const char* key;        // JSON key
    const char* label;      // human-readable label
    FieldType   type;
    int         min_val;    // for INT
    int         max_val;    // for INT
    int         step;       // for INT (0 = 1)
    const char* options;    // for ENUM: "Option A|Option B|Option C"
    const char* group;      // settings group: "General", "Content", "Keyer", etc.
};

// Get the field metadata table.  Returns count.
int config_get_field_meta(const FieldMeta** out);

// Serialize current settings to JSON string (caller must free with free()).
// Returns nullptr on allocation failure.
char* config_settings_to_json();

// Apply a JSON object to current settings.  Only keys present are updated.
// Returns true if any setting changed.
bool config_settings_from_json(const char* json, size_t len);

// Slots (snapshots) — up to 8 named slots.
static constexpr int CONFIG_MAX_SLOTS = 8;

// List existing slot names.  Writes up to max_slots names into names[].
// Each name is null-terminated, max 16 chars.  Returns count found.
int config_list_slots(char names[][17], int max_slots);

// Save current settings to a named slot.
bool config_save_slot(const char* name);

// Load a named slot into current settings (applies + saves).
bool config_load_slot(const char* name);

// Delete a named slot.
bool config_delete_slot(const char* name);

// Battery info snapshot for web API.
struct BatteryInfo {
    int     percent;         // 0–100, or -1 if unavailable
    int     raw_mv;          // uncalibrated battery voltage
    int     compensated_mv;  // calibrated battery voltage
    float   comp_factor;     // compensation factor (1.0 = uncalibrated)
    bool    charging;
};
BatteryInfo config_get_battery_info();

// ── Mode control & status for web API ──────────────────────────────────────

// Current device status snapshot.
struct StatusInfo {
    const char* mode;        // "none","keyer","generator","echo","chatbot",
                             // "internet_cw","invaders","decoder"
    bool  paused;            // true if generator/echo is paused
    int   wpm;               // current WPM setting
    int   decoder_signal;    // 0–100, decoder mode signal level
    int   decoder_wpm;       // estimated WPM in decoder mode
};
StatusInfo config_get_status();

// Request mode switch (deferred to main loop).
// mode: "keyer","generator","echo","chatbot","decoder","home"
// Returns true if the request was queued.
bool config_request_mode(const char* mode);

// Toggle pause/resume in generator or echo mode.
// Returns true if toggled.
bool config_toggle_pause();

// Get accumulated decoded/generated text since last call.
// Caller must free() the returned string.  Returns nullptr if empty.
char* config_get_text();

// Get accumulated text without clearing.  Caller must free().
char* config_peek_text();

// Clear accumulated text buffer.
void config_clear_text();

// Queue text to be played as Morse.  Works in Keyer and Generator modes.
// The text is split into words and fed to the MorseTrainer one word at a time.
// Returns true if the request was queued (mode supports sending).
bool config_send_text(const char* text);
