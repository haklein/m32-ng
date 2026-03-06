// src/app_ui.hpp — Shared UI logic for pocketwroom and simulator.
//
// Include requirements (must be defined by the platform TU before this header):
//   static constexpr lv_coord_t SCREEN_W;   // logical display width
//   static constexpr lv_coord_t SCREEN_H;   // logical display height
//   static unsigned long app_millis();       // monotonic millisecond counter
//
// Platform TU must also set s_audio and s_keys before calling app_ui_init().

#pragma once

#include <algorithm>
#include <string>
#include <random>

#include <lvgl.h>

#include "i_audio_output.h"
#include "i_key_input.h"
#include "i_storage.h"
#include "i_network.h"
#include "paddle_ctl.h"
#include "iambic_keyer.h"
#include "straight_keyer.h"
#include "morse_decoder.h"
#include "morse_trainer.h"
#include "text_generators.h"
#include "cw_textfield.hpp"
#include "screen_stack.hpp"
#include "status_bar.hpp"
#include "cw_chatbot.h"
#include "lv_font_intel.h"
#include "timing_buffer.h"
#include "cwcom_codec.h"
#include "mopp_codec.h"
#include "rx_cw_player.h"
#include "cw_invaders.h"
#ifdef BOARD_POCKETWROOM
#include "config_api.h"
#include <ArduinoJson.h>
#endif

// content_y() removed — use content_y() below (depends on font setting).

// ── Global settings ────────────────────────────────────────────────────────
struct AppSettings {
    static constexpr uint8_t VERSION = 15;
    uint8_t  version      = VERSION;  // NVS blob migration marker
    int      wpm          = 20;
    uint8_t  farnsworth   = 0;       // effective WPM (0=off, must be < wpm)
    bool     mode_a       = false;   // false = Iambic B
    uint16_t freq_hz      = 700;
    uint8_t  volume       = 14;     // 0-20; codec analog volume (2 dB/step)
    // Content
    bool     cont_words   = true;    // English words
    bool     cont_abbrevs = false;   // Ham radio abbreviations
    bool     cont_calls   = false;   // Synthetic callsigns
    bool     cont_chars   = false;   // Random character groups
    bool     cont_qso     = false;   // QSO phrase templates
    uint8_t  chars_group       = 0;   // 0=Alpha, 1=Alpha+Num, 2=All CW
    uint8_t  koch_lesson       = 0;   // 0=off, 1..N=first N Koch chars
    uint8_t  koch_order        = 0;   // KochOrder enum (0=LCWO, 1=Morserino, 2=CWAc, 3=LICW)
    uint8_t  echo_max_repeats  = 3;   // 0=unlimited, else max failures before reveal
    uint8_t  chatbot_qso_depth = 1;  // 0=MINIMAL, 1=STANDARD, 2=RAGCHEW
    uint8_t  text_font_size    = 0;  // 0=Normal (20px), 1=Large (28px)
    // Key input (VERSION 2)
    bool     ext_key_iambic = false; // false=Straight, true=Iambic (A/B from mode_a)
    bool     paddle_swap    = false; // swap dit/dah on touch paddles
    bool     ext_key_swap   = false; // swap dit/dah on ext key (iambic mode only)
    bool     screen_flip    = false; // upside-down rotation (lefty mode)
    uint8_t  word_max_length = 0; // 0=any, 2-15 max chars per word/abbrev/call
    // Sleep (VERSION 5)
    uint8_t  sleep_timeout_min = 5; // 0=disabled, 1-60 minutes
    // Quick start (VERSION 8)
    bool     quick_start  = false;  // auto-enter last mode on boot
    uint8_t  last_mode    = 0;      // 0=Keyer, 1=Generator, 2=Echo, 3=Chatbot
    // VERSION 9
    bool     adaptive_speed = true; // adjust WPM on echo success/failure
    uint8_t  qso_max_words  = 0;   // 0=unlimited, else max words per QSO phrase
    // VERSION 11
    uint8_t  adsr_ms = 7;         // ADSR attack+release (ms), range 1-15
    // VERSION 12
    uint8_t  curtisb_dit_pct = 75; // Curtis B dit timing % (original Morserino default)
    uint8_t  curtisb_dah_pct = 45; // Curtis B dah timing % (original Morserino default)
    // VERSION 13 — Internet CW
    uint8_t  inet_proto     = 0;    // 0=CWCom, 1=MOPP
    uint16_t cwcom_wire     = 111;  // CWCom wire/channel number
    char     callsign[16]   = {};   // station callsign (empty = anonymous)
    // VERSION 14 — session limit
    uint8_t  session_size   = 0;    // 0=unlimited, 1-99 phrases per session
    // VERSION 15 — display brightness
    uint8_t  brightness     = 255;  // backlight 0–255 (persisted)
};
static AppSettings s_settings;

static const lv_font_t* cw_text_font()
{
    return s_settings.text_font_size == 0 ? &lv_font_intel_20 : &lv_font_intel_28;
}

static const lv_font_t* ui_font()
{
    return s_settings.text_font_size == 0 ? &lv_font_intel_14 : &lv_font_intel_20;
}

// Menu font: montserrat (includes LVGL symbol glyphs for icons).
static const lv_font_t* menu_font()
{
    return s_settings.text_font_size == 0
        ? &lv_font_montserrat_14 : &lv_font_montserrat_20;
}

static lv_coord_t content_y()
{
    return lv_font_get_line_height(menu_font()) + 4 + 2;  // font height + 2*pad + gap
}

// ── Active CW mode ─────────────────────────────────────────────────────────
enum class ActiveMode { NONE, KEYER, GENERATOR, ECHO, CHATBOT, INTERNET_CW, INVADERS };
static ActiveMode s_active_mode = ActiveMode::NONE;

// ── HAL pointers (assigned by platform before app_ui_init) ────────────────
static IAudioOutput* s_audio   = nullptr;
static IKeyInput*    s_keys    = nullptr;
static IStorage*     s_storage = nullptr;  // nullptr on simulator (no persistence)
static INetwork*     s_network = nullptr;  // nullptr on simulator

// ── Battery HAL callbacks (set by platform main.cpp) ─────────────────────
static uint8_t (*s_read_battery_percent)() = nullptr;  // 0–100, or 255 if n/a
static bool    (*s_is_charging)()          = nullptr;
static void    (*s_set_brightness)(uint8_t) = nullptr; // backlight 0–255

// ── Dedicated CW task (pocketwroom only) ─────────────────────────────────
// Moves PaddleCtl/IambicKeyer/StraightKeyer/MorseDecoder to a high-priority
// FreeRTOS task on Core 1, isolating CW timing from LVGL render spikes.
#ifdef BOARD_POCKETWROOM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct DecodedSymbol {
    char text[12];   // longest prosign: "<err>" = 5 + NUL
};

static QueueHandle_t s_ui_event_queue      = nullptr;   // KeyEvent, depth 32
static QueueHandle_t s_decoded_symbol_queue = nullptr;   // DecodedSymbol, depth 16
static TaskHandle_t  s_cw_task_handle      = nullptr;

// Written by UI task (mode enter/leave), read by CW task
static volatile bool s_cw_engine_active = false;

// Written by CW task, read by UI task
static volatile unsigned long s_cw_last_element_end_t = 0;
static volatile bool          s_cw_tame_echo          = false;
#endif

// ── Internet CW state ─────────────────────────────────────────────────────
static volatile bool s_inet_cw_active = false;  // gates timing capture
static TimingRingBuffer<64> s_timing_ringbuf;
static CwComCodec*   s_cwcom_codec  = nullptr;
static MoppCodec*    s_mopp_codec   = nullptr;
static RxCwPlayer*   s_rx_player    = nullptr;
static CWTextField*  s_inet_rx_tf   = nullptr;
static CWTextField*  s_inet_tx_tf   = nullptr;
static unsigned long s_inet_keepalive_t = 0;  // last keepalive sent (ms)

// ── CW Invaders game ─────────────────────────────────────────────────────
static CwInvaders* s_invaders_game = nullptr;
static lv_obj_t*   s_inv_score_lbl = nullptr;
static lv_obj_t*   s_inv_lives_lbl = nullptr;
static lv_obj_t*   s_inv_level_lbl = nullptr;
static lv_obj_t*   s_inv_input_lbl = nullptr;
static lv_obj_t*   s_inv_wpm_lbl  = nullptr;
static lv_obj_t*   s_inv_gameover_lbl = nullptr;

// ── NVS persistence ───────────────────────────────────────────────────────
static void save_settings()
{
    if (!s_storage) return;
    s_storage->set_blob("m32", "settings", &s_settings, sizeof(s_settings));
}

static void load_settings()
{
    if (!s_storage) return;
    AppSettings tmp;   // default-initialized — new fields get their defaults
    size_t n = s_storage->get_blob("m32", "settings", &tmp, sizeof(tmp));
    if (n >= sizeof(tmp.version) && tmp.version <= AppSettings::VERSION) {
        s_settings = tmp;  // old fields copied; new fields keep defaults
        if (s_settings.brightness == 0) s_settings.brightness = 255;  // never fully black
        if (s_settings.version < AppSettings::VERSION) {
            s_settings.version = AppSettings::VERSION;
            save_settings();   // persist migrated settings
        }
    }
}

#ifdef BOARD_POCKETWROOM
// Forward declaration needed by config API (defined later in this file)
static void apply_settings();

// ── Config API implementation ────────────────────────────────────────────
// Field metadata table — drives JSON serialization and web UI.
// To add a new setting: add it to AppSettings, then add one entry here.

static const FieldMeta s_field_meta[] = {
    // key                label               type            min  max step options                                group
    {"wpm",              "Speed (WPM)",       FieldType::INT,   5,  40, 1, nullptr,                               "General"},
    {"farnsworth",       "Farnsworth (0=off)", FieldType::INT,  0,  40, 1, nullptr,                               "General"},
    {"freq_hz",          "Frequency (Hz)",    FieldType::INT, 400, 900,10, nullptr,                               "General"},
    {"volume",           "Volume",            FieldType::INT,   0,  20, 1, nullptr,                               "General"},
    {"adsr_ms",          "ADSR (ms)",         FieldType::INT,   1,  15, 1, nullptr,                               "General"},
    {"brightness",       "Brightness",        FieldType::INT,   1, 255, 5, nullptr,                               "General"},
    {"text_font_size",   "Text Size",         FieldType::ENUM,  0,   1, 0, "Normal|Large",                        "General"},
    {"sleep_timeout_min","Sleep (min, 0=off)",FieldType::INT,   0,  60, 1, nullptr,                               "General"},
    {"quick_start",      "Quick Start",       FieldType::BOOL,  0,   1, 0, nullptr,                               "General"},

    {"mode_a",           "Keyer Mode",        FieldType::ENUM,  0,   1, 0, "Iambic B|Iambic A",                   "Keyer"},
    {"curtisb_dit_pct",  "CurtisB DitT%",    FieldType::INT,   0, 100, 5, nullptr,                               "Keyer"},
    {"curtisb_dah_pct",  "CurtisB DahT%",    FieldType::INT,   0, 100, 5, nullptr,                               "Keyer"},
    {"ext_key_iambic",   "Ext Key Mode",      FieldType::ENUM,  0,   1, 0, "Straight|Iambic",                     "Keyer"},
    {"paddle_swap",      "Paddle Swap",       FieldType::BOOL,  0,   1, 0, nullptr,                               "Keyer"},
    {"ext_key_swap",     "Ext Key Swap",      FieldType::BOOL,  0,   1, 0, nullptr,                               "Keyer"},
    {"screen_flip",      "Screen Flip",       FieldType::BOOL,  0,   1, 0, nullptr,                               "Keyer"},

    {"cont_words",       "Words",             FieldType::BOOL,  0,   1, 0, nullptr,                               "Content"},
    {"cont_abbrevs",     "Abbreviations",     FieldType::BOOL,  0,   1, 0, nullptr,                               "Content"},
    {"cont_calls",       "Callsigns",         FieldType::BOOL,  0,   1, 0, nullptr,                               "Content"},
    {"cont_chars",       "Characters",        FieldType::BOOL,  0,   1, 0, nullptr,                               "Content"},
    {"cont_qso",         "QSO",               FieldType::BOOL,  0,   1, 0, nullptr,                               "Content"},
    {"chars_group",      "Character Set",     FieldType::ENUM,  0,   2, 0, "Alpha|Alpha+Num|All CW",              "Content"},
    {"koch_lesson",      "Koch Lesson (0=off)",FieldType::INT,  0,  50, 1, nullptr,                               "Content"},
    {"koch_order",       "Koch Order",        FieldType::ENUM,  0,   3, 0, "LCWO|Morserino|CW Academy|LICW",      "Content"},
    {"word_max_length",  "Max Length (0=any)",FieldType::INT,   0,  15, 1, nullptr,                               "Content"},
    {"qso_max_words",    "QSO Words (0=all)", FieldType::INT,   0,   9, 1, nullptr,                               "Content"},
    {"session_size",     "Session Size (0=off)",FieldType::INT, 0,  99, 1, nullptr,                               "Content"},

    {"echo_max_repeats", "Echo Repeats (0=inf)",FieldType::INT, 0,   9, 1, nullptr,                               "Training"},
    {"adaptive_speed",   "Adaptive WPM",     FieldType::BOOL,  0,   1, 0, nullptr,                               "Training"},
    {"chatbot_qso_depth","QSO Depth",        FieldType::ENUM,  0,   2, 0, "Minimal|Standard|Ragchew",            "Training"},

    {"inet_proto",       "Internet CW",      FieldType::ENUM,  0,   1, 0, "CWCom|MOPP",                          "Network"},
    {"cwcom_wire",       "CWCom Wire",       FieldType::INT,   1, 999, 1, nullptr,                               "Network"},
    {"callsign",         "Callsign",         FieldType::STRING, 0,  15, 0, nullptr,                               "Network"},
};
static constexpr int s_field_meta_count = sizeof(s_field_meta) / sizeof(s_field_meta[0]);

int config_get_field_meta(const FieldMeta** out)
{
    *out = s_field_meta;
    return s_field_meta_count;
}

// Helper: get a setting value by key into a JsonDocument
static void settings_field_to_json(JsonObject obj, const char* key)
{
    auto& s = s_settings;
    // Each key maps directly to a struct field.  Bool fields stored as 0/1 in JSON.
    if      (!strcmp(key, "wpm"))              obj[key] = s.wpm;
    else if (!strcmp(key, "farnsworth"))        obj[key] = s.farnsworth;
    else if (!strcmp(key, "mode_a"))            obj[key] = s.mode_a ? 1 : 0;
    else if (!strcmp(key, "freq_hz"))           obj[key] = s.freq_hz;
    else if (!strcmp(key, "volume"))            obj[key] = s.volume;
    else if (!strcmp(key, "cont_words"))        obj[key] = s.cont_words ? 1 : 0;
    else if (!strcmp(key, "cont_abbrevs"))      obj[key] = s.cont_abbrevs ? 1 : 0;
    else if (!strcmp(key, "cont_calls"))        obj[key] = s.cont_calls ? 1 : 0;
    else if (!strcmp(key, "cont_chars"))        obj[key] = s.cont_chars ? 1 : 0;
    else if (!strcmp(key, "cont_qso"))          obj[key] = s.cont_qso ? 1 : 0;
    else if (!strcmp(key, "chars_group"))       obj[key] = s.chars_group;
    else if (!strcmp(key, "koch_lesson"))       obj[key] = s.koch_lesson;
    else if (!strcmp(key, "koch_order"))        obj[key] = s.koch_order;
    else if (!strcmp(key, "echo_max_repeats"))  obj[key] = s.echo_max_repeats;
    else if (!strcmp(key, "chatbot_qso_depth")) obj[key] = s.chatbot_qso_depth;
    else if (!strcmp(key, "text_font_size"))    obj[key] = s.text_font_size;
    else if (!strcmp(key, "ext_key_iambic"))    obj[key] = s.ext_key_iambic ? 1 : 0;
    else if (!strcmp(key, "paddle_swap"))       obj[key] = s.paddle_swap ? 1 : 0;
    else if (!strcmp(key, "ext_key_swap"))      obj[key] = s.ext_key_swap ? 1 : 0;
    else if (!strcmp(key, "screen_flip"))       obj[key] = s.screen_flip ? 1 : 0;
    else if (!strcmp(key, "word_max_length"))   obj[key] = s.word_max_length;
    else if (!strcmp(key, "sleep_timeout_min")) obj[key] = s.sleep_timeout_min;
    else if (!strcmp(key, "quick_start"))       obj[key] = s.quick_start ? 1 : 0;
    else if (!strcmp(key, "adaptive_speed"))    obj[key] = s.adaptive_speed ? 1 : 0;
    else if (!strcmp(key, "qso_max_words"))     obj[key] = s.qso_max_words;
    else if (!strcmp(key, "adsr_ms"))           obj[key] = s.adsr_ms;
    else if (!strcmp(key, "curtisb_dit_pct"))   obj[key] = s.curtisb_dit_pct;
    else if (!strcmp(key, "curtisb_dah_pct"))   obj[key] = s.curtisb_dah_pct;
    else if (!strcmp(key, "inet_proto"))        obj[key] = s.inet_proto;
    else if (!strcmp(key, "cwcom_wire"))        obj[key] = s.cwcom_wire;
    else if (!strcmp(key, "callsign"))          obj[key] = (const char*)s.callsign;
    else if (!strcmp(key, "session_size"))      obj[key] = s.session_size;
    else if (!strcmp(key, "brightness"))        obj[key] = s.brightness;
    else if (!strcmp(key, "last_mode"))         obj[key] = s.last_mode;
}

char* config_settings_to_json()
{
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    for (int i = 0; i < s_field_meta_count; i++)
        settings_field_to_json(obj, s_field_meta[i].key);
    size_t len = measureJson(doc) + 1;
    char* buf = (char*)malloc(len);
    if (buf) serializeJson(doc, buf, len);
    return buf;
}

// Helper: set a single field from a JSON value
static bool settings_field_from_json(const char* key, JsonVariant val)
{
    auto& s = s_settings;
    if      (!strcmp(key, "wpm"))              { s.wpm = val.as<int>(); return true; }
    else if (!strcmp(key, "farnsworth"))        { s.farnsworth = val.as<int>(); return true; }
    else if (!strcmp(key, "mode_a"))            { s.mode_a = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "freq_hz"))           { s.freq_hz = val.as<int>(); return true; }
    else if (!strcmp(key, "volume"))            { s.volume = val.as<int>(); return true; }
    else if (!strcmp(key, "cont_words"))        { s.cont_words = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "cont_abbrevs"))      { s.cont_abbrevs = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "cont_calls"))        { s.cont_calls = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "cont_chars"))        { s.cont_chars = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "cont_qso"))          { s.cont_qso = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "chars_group"))       { s.chars_group = val.as<int>(); return true; }
    else if (!strcmp(key, "koch_lesson"))       { s.koch_lesson = val.as<int>(); return true; }
    else if (!strcmp(key, "koch_order"))        { s.koch_order = val.as<int>(); return true; }
    else if (!strcmp(key, "echo_max_repeats"))  { s.echo_max_repeats = val.as<int>(); return true; }
    else if (!strcmp(key, "chatbot_qso_depth")) { s.chatbot_qso_depth = val.as<int>(); return true; }
    else if (!strcmp(key, "text_font_size"))    { s.text_font_size = val.as<int>(); return true; }
    else if (!strcmp(key, "ext_key_iambic"))    { s.ext_key_iambic = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "paddle_swap"))       { s.paddle_swap = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "ext_key_swap"))      { s.ext_key_swap = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "screen_flip"))       { s.screen_flip = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "word_max_length"))   { s.word_max_length = val.as<int>(); return true; }
    else if (!strcmp(key, "sleep_timeout_min")) { s.sleep_timeout_min = val.as<int>(); return true; }
    else if (!strcmp(key, "quick_start"))       { s.quick_start = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "adaptive_speed"))    { s.adaptive_speed = val.as<int>() != 0; return true; }
    else if (!strcmp(key, "qso_max_words"))     { s.qso_max_words = val.as<int>(); return true; }
    else if (!strcmp(key, "adsr_ms"))           { s.adsr_ms = val.as<int>(); return true; }
    else if (!strcmp(key, "curtisb_dit_pct"))   { s.curtisb_dit_pct = val.as<int>(); return true; }
    else if (!strcmp(key, "curtisb_dah_pct"))   { s.curtisb_dah_pct = val.as<int>(); return true; }
    else if (!strcmp(key, "inet_proto"))        { s.inet_proto = val.as<int>(); return true; }
    else if (!strcmp(key, "cwcom_wire"))        { s.cwcom_wire = val.as<int>(); return true; }
    else if (!strcmp(key, "callsign")) {
        const char* v = val.as<const char*>();
        if (v) { strncpy(s.callsign, v, sizeof(s.callsign)-1); s.callsign[sizeof(s.callsign)-1] = 0; }
        return true;
    }
    else if (!strcmp(key, "session_size"))      { s.session_size = val.as<int>(); return true; }
    else if (!strcmp(key, "brightness"))        { s.brightness = val.as<int>(); return true; }
    return false;
}

bool config_settings_from_json(const char* json, size_t len)
{
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    bool changed = false;
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (settings_field_from_json(kv.key().c_str(), kv.value()))
            changed = true;
    }
    if (changed) {
        apply_settings();
        save_settings();
    }
    return changed;
}

// ── Slots (snapshots) ──────────────────────────────────────────────────────
// Stored in NVS namespace "slots" as JSON blobs keyed by slot name.
// Slot index stored in "slots"/"index" as a pipe-separated name list.

static void slot_save_index(const char names[][17], int count)
{
    if (!s_storage) return;
    char buf[256] = {};
    int pos = 0;
    for (int i = 0; i < count && pos < 250; i++) {
        if (i > 0) buf[pos++] = '|';
        int n = snprintf(buf + pos, 250 - pos, "%s", names[i]);
        pos += n;
    }
    s_storage->set_string("slots", "index", buf);
}

int config_list_slots(char names[][17], int max_slots)
{
    if (!s_storage) return 0;
    char buf[256] = {};
    if (!s_storage->get_string("slots", "index", buf, sizeof(buf))) return 0;
    int count = 0;
    char* p = buf;
    while (*p && count < max_slots) {
        char* sep = strchr(p, '|');
        int len = sep ? (int)(sep - p) : (int)strlen(p);
        if (len > 16) len = 16;
        memcpy(names[count], p, len);
        names[count][len] = 0;
        count++;
        if (!sep) break;
        p = sep + 1;
    }
    return count;
}

bool config_save_slot(const char* name)
{
    if (!s_storage || !name || !name[0]) return false;
    char* json = config_settings_to_json();
    if (!json) return false;
    s_storage->set_string("slots", name, json);
    // Add to index if not already present
    char names[CONFIG_MAX_SLOTS][17] = {};
    int count = config_list_slots(names, CONFIG_MAX_SLOTS);
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (!strcmp(names[i], name)) { found = true; break; }
    }
    if (!found && count < CONFIG_MAX_SLOTS) {
        strncpy(names[count], name, 16);
        names[count][16] = 0;
        count++;
        slot_save_index(names, count);
    }
    free(json);
    s_storage->commit();
    return true;
}

bool config_load_slot(const char* name)
{
    if (!s_storage || !name || !name[0]) return false;
    char buf[1024] = {};
    if (!s_storage->get_string("slots", name, buf, sizeof(buf))) return false;
    return config_settings_from_json(buf, strlen(buf));
}

bool config_delete_slot(const char* name)
{
    if (!s_storage || !name || !name[0]) return false;
    // Remove from index
    char names[CONFIG_MAX_SLOTS][17] = {};
    int count = config_list_slots(names, CONFIG_MAX_SLOTS);
    int new_count = 0;
    char new_names[CONFIG_MAX_SLOTS][17] = {};
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], name) != 0) {
            strcpy(new_names[new_count++], names[i]);
        }
    }
    slot_save_index(new_names, new_count);
    // We can't truly delete from NVS easily, but the index won't list it
    s_storage->set_string("slots", name, "");
    s_storage->commit();
    return true;
}
#endif // BOARD_POCKETWROOM — config API

// ── CW engine (keyer + echo modes) ────────────────────────────────────────
static PaddleCtl*      s_paddle          = nullptr;
static IambicKeyer*    s_keyer           = nullptr;
static MorseDecoder*   s_decoder         = nullptr;
static StraightKeyer*  s_straight_keyer  = nullptr;
static read_key_fun_ptr s_read_straight_key = nullptr;   // set by platform TU
static bool            s_straight_key_state = false;      // event→polling bridge

// ── Trainer (generator + echo modes) ──────────────────────────────────────
static MorseTrainer*   s_trainer    = nullptr;
static TextGenerators* s_gen        = nullptr;
static std::mt19937    s_rng;
static bool            s_gen_paused = false;
static int             s_session_count = 0;   // phrases generated in current session

// ── Word-space timer for CW keyer ─────────────────────────────────────────
// Tracks last element END (not character decode) so the 7-dit gap is measured
// from the right point regardless of how long the next character takes to play.
static unsigned long s_keyer_last_element_end_t = 0;
static bool          s_keyer_word_pending        = false;

// ── UI widgets updated from engine callbacks ───────────────────────────────
static CWTextField* s_keyer_tf  = nullptr;
static CWTextField* s_gen_tf    = nullptr;
static StatusBar*   s_active_sb = nullptr;

// ── Echo trainer widgets (set/cleared with echo screen) ───────────────────
static lv_obj_t*   s_echo_target_lbl = nullptr;
static lv_obj_t*   s_echo_rcvd_lbl   = nullptr;
static lv_obj_t*   s_echo_result_lbl = nullptr;
static std::string s_echo_typed;
static uint8_t     s_echo_consecutive_e = 0;  // EEEE delete detection (display side)
static std::string s_pending_gen_phrase;

// ── Chatbot widgets (set/cleared with chatbot screen) ─────────────────────
static CWChatbot*   s_chatbot                = nullptr;
static std::string  s_chatbot_pending_phrase;
static bool         s_chatbot_tx_active      = false;
static CWTextField* s_chatbot_bot_tf         = nullptr;
static CWTextField* s_chatbot_oper_tf        = nullptr;
static lv_obj_t*    s_chatbot_state_lbl      = nullptr;

// ── WiFi provisioning state ──────────────────────────────────────────────
static char          s_ap_ssid[20]           = {};
static lv_obj_t*     s_wifi_status_lbl       = nullptr;
static volatile bool s_wifi_creds_ready      = false;
static bool          s_wifi_portal_pending   = false;  // deferred portal start
static char          s_pending_ssid[33]      = {};
static char          s_pending_pass[65]      = {};
#ifdef BOARD_POCKETWROOM
#include "wifi_portal.h"
#include "qr_canvas.h"
#include "screenshot_server.h"
static WifiPortal*   s_portal               = nullptr;
#endif

// ── Internet CW widgets (set/cleared with inet CW screens) ──────────────
static lv_obj_t*   s_inet_connect_btn = nullptr;
static lv_obj_t*   s_inet_status_lbl  = nullptr;
static lv_obj_t*   s_inet_wire_spin   = nullptr;
static lv_obj_t*   s_inet_proto_dd    = nullptr;
static lv_obj_t*   s_inet_wire_lbl    = nullptr;

static void save_wifi_creds(const char* ssid, const char* pass)
{
    if (!s_storage) return;
    s_storage->set_string("wifi", "ssid", ssid);
    s_storage->set_string("wifi", "pass", pass);
    s_storage->commit();
}

static bool load_wifi_creds(char* ssid_buf, size_t ssid_len,
                            char* pass_buf, size_t pass_len)
{
    if (!s_storage) return false;
    if (!s_storage->get_string("wifi", "ssid", ssid_buf, ssid_len))
        return false;
    s_storage->get_string("wifi", "pass", pass_buf, pass_len);
    return ssid_buf[0] != '\0';
}

// ── Screen stack & LVGL encoder indev ─────────────────────────────────────
static ScreenStack s_stack;
static lv_indev_t* s_enc_indev        = nullptr;
static int32_t     s_enc_diff         = 0;
static int         s_enc_press_frames = 0;
static lv_group_t* s_menu_group     = nullptr;
static lv_group_t* s_settings_group = nullptr;
static lv_group_t* s_content_group  = nullptr;
static lv_group_t* s_wifi_group     = nullptr;
static lv_group_t* s_inet_group    = nullptr;

// ── Encoder adjust mode (FN button cycles: WPM / Volume / Scroll) ───────
enum class EncoderMode { WPM, VOLUME, SCROLL };
static EncoderMode   s_encoder_mode          = EncoderMode::WPM;
static unsigned long s_fn_last_press_t       = 0;
static bool          s_fn_pending            = false;
static constexpr unsigned long FN_DOUBLE_MS  = 400;

// ── Brightness steps (matching original Morserino) ───────────────────────
static constexpr uint8_t BRIGHTNESS_STEPS[] = { 255, 127, 63, 28, 9 };
static constexpr int     BRIGHTNESS_N = sizeof(BRIGHTNESS_STEPS) / sizeof(BRIGHTNESS_STEPS[0]);

static void cycle_brightness()
{
    if (!s_set_brightness) return;
    int cur = 0;
    for (int i = 0; i < BRIGHTNESS_N; ++i) {
        if (s_settings.brightness == BRIGHTNESS_STEPS[i]) { cur = i; break; }
    }
    cur = (cur + 1) % BRIGHTNESS_N;
    s_settings.brightness = BRIGHTNESS_STEPS[cur];
    s_set_brightness(s_settings.brightness);
    save_settings();
}

static void update_invaders_hud();  // forward decl

static void update_status_bar_info()
{
    if (s_active_sb) {
        switch (s_encoder_mode) {
            case EncoderMode::WPM:    s_active_sb->set_wpm(s_settings.wpm, s_settings.farnsworth); break;
            case EncoderMode::VOLUME: s_active_sb->set_volume(s_settings.volume); break;
            case EncoderMode::SCROLL: s_active_sb->set_scroll(); break;
        }
#ifdef BOARD_POCKETWROOM
        if (s_network) {
            bool conn = s_network->wifi_is_connected();
            s_active_sb->set_wifi(conn, !conn && s_wifi_portal_pending);
        }
#endif
    }
    // Invaders mode: WPM is part of the combined HUD label
    if (s_inv_score_lbl && s_encoder_mode == EncoderMode::WPM)
        update_invaders_hud();
}

// Enable/disable auto-scroll on all active CW text fields.
static void set_active_auto_scroll(bool enable)
{
    if (s_keyer_tf)       s_keyer_tf->set_auto_scroll(enable);
    if (s_gen_tf)         s_gen_tf->set_auto_scroll(enable);
    if (s_chatbot_bot_tf) s_chatbot_bot_tf->set_auto_scroll(enable);
    if (s_chatbot_oper_tf) s_chatbot_oper_tf->set_auto_scroll(enable);
}

// Return the scrollable container of the primary active text field.
static lv_obj_t* active_scroll_target()
{
    if (s_keyer_tf)       return s_keyer_tf->obj();
    if (s_gen_tf)         return s_gen_tf->obj();
    if (s_chatbot_bot_tf) return s_chatbot_bot_tf->obj();
    return nullptr;
}

static void enter_encoder_mode(EncoderMode mode)
{
    s_encoder_mode = mode;
    set_active_auto_scroll(mode != EncoderMode::SCROLL);
    update_status_bar_info();
}

// ── Screen flip callback (set by platform TU; nullptr on simulator) ───────
static void (*s_on_screen_flip)(bool flip) = nullptr;

// ── Deep sleep (set by platform TU; nullptr on simulator) ────────────────
static void (*s_enter_deep_sleep)() = nullptr;
static unsigned long s_last_activity_t = 0;

static void reset_activity_timer()
{
    s_last_activity_t = (unsigned long)app_millis();
}

static bool cw_mode_active();  // defined below

// ── StraightKeyer state callback ──────────────────────────────────────────
// StraightKeyer polls the key, applies noise-blanker debouncing, and fires
// STRAIGHT_ON/OFF for sidetone plus DOT_ON/DASH_ON (adaptive classification)
// fed into MorseDecoder.  STOPPED fires at the inter-character gap.
// on_straight_state fires from StraightKeyer::decode().
// On pocketwroom this runs on the CW task — must not touch LVGL or MorseTrainer.
static void on_straight_state(PlayState ps)
{
#ifdef BOARD_POCKETWROOM
    if (!s_cw_engine_active) return;
#else
    if (!cw_mode_active()) return;
#endif
    switch (ps) {
    case PLAY_STATE_STRAIGHT_ON:
        s_audio->tone_on(s_settings.freq_hz);
#ifdef BOARD_POCKETWROOM
        digitalWrite(PIN_KEYER, HIGH);
#endif
        s_decoder->set_transmitting(true);
#ifdef BOARD_POCKETWROOM
        s_cw_last_element_end_t = (unsigned long)app_millis();
        s_cw_tame_echo = true;
#else
        s_keyer_last_element_end_t = (unsigned long)app_millis();
        if (s_active_mode == ActiveMode::ECHO)
            s_trainer->tame_echo_timeout();
#endif
        break;
    case PLAY_STATE_DOT_ON:
    case PLAY_STATE_DASH_ON:
        if (ps == PLAY_STATE_DOT_ON) s_decoder->append_dot();
        else                         s_decoder->append_dash();
        if (s_straight_keyer)
            s_decoder->set_decode_threshold(s_straight_keyer->get_dit_avg() * 2);
        break;
    case PLAY_STATE_STRAIGHT_OFF:
        s_audio->tone_off();
#ifdef BOARD_POCKETWROOM
        digitalWrite(PIN_KEYER, LOW);
#endif
        s_decoder->set_transmitting(false);
#ifdef BOARD_POCKETWROOM
        s_cw_last_element_end_t = (unsigned long)app_millis();
#else
        s_keyer_last_element_end_t = (unsigned long)app_millis();
#endif
        break;
    case PLAY_STATE_STOPPED:
        break;
    default:
        break;
    }
}

// Is any CW mode active?
static bool cw_mode_active()
{
    return s_active_mode == ActiveMode::KEYER ||
           s_active_mode == ActiveMode::ECHO ||
           s_active_mode == ActiveMode::CHATBOT ||
           s_active_mode == ActiveMode::INTERNET_CW ||
           s_active_mode == ActiveMode::INVADERS;
}

// ── Forward declarations ───────────────────────────────────────────────────
static lv_obj_t* build_main_menu();
static lv_obj_t* build_keyer_screen();
static lv_obj_t* build_generator_screen();
static lv_obj_t* build_echo_screen();
static lv_obj_t* build_chatbot_screen();
static lv_obj_t* build_settings_screen();
static lv_obj_t* build_content_screen();
static lv_obj_t* build_wifi_screen();
static lv_obj_t* build_inet_cw_screen();
static lv_obj_t* build_invaders_screen();
static void      update_invaders_hud();
static void      inet_cw_disconnect();
static void      apply_settings();
static std::string content_phrase();
static void      route(KeyEvent ev);
static void      push_mode_screen(int idx);

// ── LVGL encoder indev callback ────────────────────────────────────────────
static void enc_read_cb(lv_indev_t*, lv_indev_data_t* d)
{
    d->enc_diff = s_enc_diff;
    s_enc_diff  = 0;
    if (s_enc_press_frames > 0) {
        d->state = LV_INDEV_STATE_PR;
        --s_enc_press_frames;
    } else {
        d->state = LV_INDEV_STATE_REL;
    }
}

// ── CW engine callbacks ────────────────────────────────────────────────────
// on_play_state fires from IambicKeyer::tick().
// On pocketwroom this runs on the CW task — must not touch LVGL.
// s_audio->tone_on/off() is lock-free; s_decoder is on the same task.
static void on_play_state(PlayState state)
{
    unsigned long now = (unsigned long)app_millis();
    switch (state) {
        case PLAY_STATE_DOT_ON:
        case PLAY_STATE_DASH_ON:
            s_audio->tone_on(s_settings.freq_hz);
#ifdef BOARD_POCKETWROOM
            digitalWrite(PIN_KEYER, HIGH);
#endif
            s_decoder->set_transmitting(true);
            if (s_inet_cw_active)
                s_timing_ringbuf.push(true, (uint32_t)now);
#ifdef BOARD_POCKETWROOM
            s_cw_last_element_end_t = now;
#else
            s_keyer_last_element_end_t = now;
#endif
            break;
        case PLAY_STATE_DOT_OFF:
            s_audio->tone_off();
#ifdef BOARD_POCKETWROOM
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_decoder->append_dot();
            s_decoder->set_transmitting(false);
            if (s_inet_cw_active)
                s_timing_ringbuf.push(false, (uint32_t)now);
#ifdef BOARD_POCKETWROOM
            s_cw_last_element_end_t = now;
#else
            s_keyer_last_element_end_t = now;
#endif
            break;
        case PLAY_STATE_DASH_OFF:
            s_audio->tone_off();
#ifdef BOARD_POCKETWROOM
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_decoder->append_dash();
            s_decoder->set_transmitting(false);
            if (s_inet_cw_active)
                s_timing_ringbuf.push(false, (uint32_t)now);
#ifdef BOARD_POCKETWROOM
            s_cw_last_element_end_t = now;
#else
            s_keyer_last_element_end_t = now;
#endif
            break;
        default:
            break;
    }
}

static void on_lever_state(LeverState state)
{
    s_keyer->setLeverState(state);
}

static void on_letter_decoded(const std::string& letter)
{
    if (s_active_mode == ActiveMode::KEYER) {
        if (!s_keyer_tf || letter == " ") return;
        s_keyer_tf->add_string(letter);
        s_keyer_word_pending = true;
    } else if (s_active_mode == ActiveMode::ECHO) {
        s_trainer->symbol_received(letter);
        if (s_echo_rcvd_lbl) {
            // Detect delete signals: <err>, *, or 4+ consecutive 'e' (EEEE)
            bool is_delete = (letter == "<err>" || letter == "*");
            if (!is_delete && letter == "e") {
                ++s_echo_consecutive_e;
                if (s_echo_consecutive_e >= 4) {
                    // Remove the 3 previously appended 'e's
                    if (s_echo_typed.size() >= 3)
                        s_echo_typed.erase(s_echo_typed.size() - 3);
                    else
                        s_echo_typed.clear();
                    is_delete = true;
                }
            } else if (!is_delete) {
                s_echo_consecutive_e = 0;
            }
            if (is_delete) {
                s_echo_consecutive_e = 0;
                // Strip trailing spaces (from word-gap detection), then remove last word.
                while (!s_echo_typed.empty() && s_echo_typed.back() == ' ')
                    s_echo_typed.pop_back();
                auto pos = s_echo_typed.rfind(' ');
                if (pos != std::string::npos)
                    s_echo_typed.erase(pos);
                else
                    s_echo_typed.clear();
            } else {
                s_echo_typed += letter;
            }
            lv_label_set_text(s_echo_rcvd_lbl, s_echo_typed.c_str());
        }
    } else if (s_active_mode == ActiveMode::CHATBOT) {
        if (s_chatbot) s_chatbot->symbol_received(letter);
        if (s_chatbot_oper_tf && letter != " ") {
            s_chatbot_oper_tf->add_string(letter);
        }
        s_keyer_word_pending = true;
    } else if (s_active_mode == ActiveMode::INTERNET_CW) {
        if (!s_inet_tx_tf || letter == " ") return;
        s_inet_tx_tf->add_string(letter);
        s_keyer_word_pending = true;
    } else if (s_active_mode == ActiveMode::INVADERS) {
        if (!s_invaders_game || letter == " ") return;
        bool hit = s_invaders_game->try_match(letter);
        // Show keyed letter in input strip
        if (s_inv_input_lbl)
            lv_label_set_text(s_inv_input_lbl, letter.c_str());
        update_invaders_hud();
        // Show game over with score
        if (s_invaders_game->game_over() && s_inv_gameover_lbl) {
            char go[32];
            snprintf(go, sizeof(go), "GAME OVER\n%d", s_invaders_game->score());
            lv_label_set_text(s_inv_gameover_lbl, go);
            lv_obj_remove_flag(s_inv_gameover_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        (void)hit;
    }

    // Update effective WPM from straight keyer's adaptive timing
    if (s_straight_keyer && !s_settings.ext_key_iambic && s_active_sb) {
        unsigned long dit_avg = s_straight_keyer->get_dit_avg();
        int eff_wpm = (dit_avg > 0) ? (int)(1200 / dit_avg) : 0;
        s_active_sb->set_wpm(s_settings.wpm, s_settings.farnsworth, eff_wpm);
    }
}

// ── Propagate settings to all engine objects ───────────────────────────────
static void apply_settings()
{
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_keyer->setSpeedWPM((unsigned long)s_settings.wpm);
    s_keyer->setModeA(s_settings.mode_a);
    s_decoder->set_decode_threshold(dit_ms * 2);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_trainer->set_farnsworth_wpm(s_settings.farnsworth);
    s_trainer->set_max_echo_repeats(s_settings.echo_max_repeats);
    s_trainer->set_adaptive_speed(s_settings.adaptive_speed);
    if (s_chatbot) {
        s_chatbot->set_speed_wpm(s_settings.wpm);
        static const QSODepth depths[] = {
            QSODepth::MINIMAL, QSODepth::STANDARD, QSODepth::RAGCHEW };
        s_chatbot->set_qso_depth(depths[std::min((int)s_settings.chatbot_qso_depth, 2)]);
    }
    s_audio->set_volume(s_settings.volume);
    float adsr_s = s_settings.adsr_ms / 1000.0f;
    s_audio->set_adsr(adsr_s, 0.0f, 1.0f, adsr_s);
    s_keyer->setReleaseCompensation((unsigned long)s_settings.adsr_ms);
    s_keyer->setCurtisBThreshold(s_settings.curtisb_dit_pct,
                                  s_settings.curtisb_dah_pct);
    s_trainer->set_release_compensation(s_settings.adsr_ms);
    if (s_set_brightness)
        s_set_brightness(s_settings.brightness);
    update_status_bar_info();
}

// ── Content phrase generator ───────────────────────────────────────────────
// Returns the next training phrase according to the current content settings.
static std::string content_phrase()
{
    // Koch mode: character groups using the first N Koch chars
    if (s_settings.koch_lesson > 0) {
        int idx = std::min((int)s_settings.koch_order,
                           (int)(KOCH_ORDER_COUNT - 1));
        const char* order = KOCH_ORDERS[idx];
        int n = std::min((int)s_settings.koch_lesson, KOCH_MAX_LESSON);
        int glen = (int)s_settings.word_max_length;
        if (glen <= 0) glen = 5;  // default group size when unlimited
        return s_gen->random_chars_from_set(std::string(order, n), glen);
    }
    // Collect enabled content types
    int types[5]; int nt = 0;
    if (s_settings.cont_words)   types[nt++] = 0;
    if (s_settings.cont_abbrevs) types[nt++] = 1;
    if (s_settings.cont_calls)   types[nt++] = 2;
    if (s_settings.cont_chars)   types[nt++] = 3;
    if (s_settings.cont_qso)    types[nt++] = 4;
    int ml = (int)s_settings.word_max_length;
    if (nt == 0) return s_gen->random_word(ml);   // fallback: always something
    int choice = types[std::uniform_int_distribution<int>(0, nt - 1)(s_rng)];
    static const RandomOption char_opts[] = { OPT_ALPHA, OPT_ALNUM, OPT_ALL };
    switch (choice) {
        case 0: return s_gen->random_word(ml);
        case 1: return s_gen->random_abbrev(ml);
        case 2: return s_gen->random_callsign(ml);
        case 3: return s_gen->random_chars(
                    ml > 0 ? ml : 5,
                    char_opts[std::min((int)s_settings.chars_group, 2)]);
        case 4: return s_gen->random_qso_phrase((int)s_settings.qso_max_words);
    }
    return s_gen->random_word(ml);
}

// ── Push a mode/screen by index (reused by menu + quick start) ────────────
static void push_mode_screen(int idx)
{
    lv_obj_t*       ns       = nullptr;
    ScreenStack::Cb enter_cb = {};
    ScreenStack::Cb leave_cb = {};

    if (idx == 0) {
        ns = build_keyer_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::KEYER;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = true;
#endif
            lv_indev_set_group(s_enc_indev, nullptr);
        };
        leave_cb = []() {
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = false;
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_active_mode              = ActiveMode::NONE;
            s_encoder_mode             = EncoderMode::WPM;
            s_keyer_word_pending       = false;
            s_keyer_last_element_end_t = 0;
            delete s_keyer_tf; s_keyer_tf = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        };
    } else if (idx == 1) {
        ns = build_generator_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::GENERATOR;
            s_gen_paused  = false;
            s_session_count = 0;
            s_trainer->set_state(MorseTrainer::TrainerState::Player);
            s_trainer->set_playing();
            lv_indev_set_group(s_enc_indev, nullptr);
        };
        leave_cb = []() {
            s_active_mode      = ActiveMode::NONE;
            s_encoder_mode     = EncoderMode::WPM;
            s_trainer->set_idle();
            s_audio->tone_off();
#ifdef BOARD_POCKETWROOM
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_pending_gen_phrase.clear();
            delete s_gen_tf; s_gen_tf = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        };
    } else if (idx == 2) {
        ns = build_echo_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::ECHO;
            s_session_count = 0;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = true;
#endif
            s_trainer->set_state(MorseTrainer::TrainerState::Echo);
            s_trainer->set_playing();
            lv_indev_set_group(s_enc_indev, nullptr);
        };
        leave_cb = []() {
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = false;
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_active_mode      = ActiveMode::NONE;
            s_encoder_mode     = EncoderMode::WPM;
            s_trainer->set_idle();
            s_trainer->set_state(MorseTrainer::TrainerState::Player);
            s_audio->tone_off();
            s_echo_target_lbl = nullptr;
            s_echo_rcvd_lbl   = nullptr;
            s_echo_result_lbl = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        };
    } else if (idx == 3) {
        ns = build_chatbot_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::CHATBOT;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = true;
#endif
            s_chatbot = new CWChatbot(
                // send_cb: queue phrase for MorseTrainer to play
                [](const std::string& text) {
                    s_chatbot_pending_phrase = text;
                    s_chatbot_tx_active = true;
                    if (s_chatbot_bot_tf)
                        s_chatbot_bot_tf->add_string(text + " ");
                    s_trainer->set_state(MorseTrainer::TrainerState::Player);
                    s_trainer->set_playing();
                },
                // speed_cb: update WPM
                [](int wpm) {
                    s_settings.wpm = wpm;
                    apply_settings();
                    save_settings();
                },
                // event_cb
                [](QSOEvent) {},
                app_millis
            );
            s_chatbot->set_operator_call("W1TEST");
            s_chatbot->set_speed_wpm(s_settings.wpm);
            static const QSODepth depths[] = {
                QSODepth::MINIMAL, QSODepth::STANDARD, QSODepth::RAGCHEW };
            s_chatbot->set_qso_depth(
                depths[std::min((int)s_settings.chatbot_qso_depth, 2)]);
            s_chatbot->set_rng_seed((unsigned int)app_millis());
            s_chatbot->start();
            lv_indev_set_group(s_enc_indev, nullptr);
        };
        leave_cb = []() {
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = false;
            digitalWrite(PIN_KEYER, LOW);
#endif
            s_active_mode      = ActiveMode::NONE;
            s_encoder_mode     = EncoderMode::WPM;
            s_trainer->set_idle();
            s_audio->tone_off();
            delete s_chatbot; s_chatbot = nullptr;
            s_chatbot_pending_phrase.clear();
            s_chatbot_tx_active = false;
            delete s_chatbot_bot_tf;  s_chatbot_bot_tf  = nullptr;
            delete s_chatbot_oper_tf; s_chatbot_oper_tf = nullptr;
            s_chatbot_state_lbl = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        };
    } else if (idx == 4) {
        ns = build_content_screen();
        enter_cb = []() { lv_indev_set_group(s_enc_indev, s_content_group); };
        leave_cb = []() { delete s_active_sb; s_active_sb = nullptr; };
    } else if (idx == 5) {
        ns = build_settings_screen();
        enter_cb = []() { lv_indev_set_group(s_enc_indev, s_settings_group); };
        leave_cb = []() { delete s_active_sb; s_active_sb = nullptr; };
    } else if (idx == 6) {
        ns = build_wifi_screen();
        enter_cb = []() {
            lv_indev_set_group(s_enc_indev, s_wifi_group);
#ifdef BOARD_POCKETWROOM
            // Only start captive portal if not already connected.
            if (!s_network || !s_network->wifi_is_connected()) {
                if (s_wifi_status_lbl)
                    lv_label_set_text(s_wifi_status_lbl, "Starting...");
                s_wifi_portal_pending = true;
            }
#endif
        };
        leave_cb = []() {
            s_wifi_portal_pending = false;
#ifdef BOARD_POCKETWROOM
            if (s_portal) { s_portal->end(); delete s_portal; s_portal = nullptr; }
            qr_canvas_destroy();
#endif
            s_wifi_status_lbl = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        };
    } else if (idx == 7) {
        // Internet CW
        ns = build_inet_cw_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::INTERNET_CW;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = true;
#endif
            lv_indev_set_group(s_enc_indev, s_inet_group);
        };
        leave_cb = []() {
            // This fires when:
            //  a) active CW screen is pushed on top (leave settings for CW)
            //  b) popping back to main menu (leave settings entirely)
            // Only do full teardown for case (b): when active CW screen
            // isn't on top (i.e. we're going back to main menu).
            if (!s_inet_cw_active) {
                // Case (b): leaving Internet CW entirely
                inet_cw_disconnect();  // no-op if not connected
#ifdef BOARD_POCKETWROOM
                s_cw_engine_active = false;
                digitalWrite(PIN_KEYER, LOW);
#endif
                s_active_mode              = ActiveMode::NONE;
                s_encoder_mode             = EncoderMode::WPM;
                s_keyer_word_pending       = false;
                s_keyer_last_element_end_t = 0;
                s_timing_ringbuf.clear();
                delete s_inet_rx_tf;  s_inet_rx_tf  = nullptr;
                delete s_inet_tx_tf;  s_inet_tx_tf  = nullptr;
                s_audio->tone_off();
            }
            s_inet_connect_btn = nullptr;
            s_inet_status_lbl  = nullptr;
            s_inet_wire_spin   = nullptr;
            s_inet_proto_dd    = nullptr;
            s_inet_wire_lbl    = nullptr;
            // Only delete status bar when fully leaving (not when
            // active CW screen is being pushed — it creates its own).
            if (!s_inet_cw_active) {
                delete s_active_sb; s_active_sb = nullptr;
            }
        };
    } else {
        // idx == 8: CW Invaders
        ns = build_invaders_screen();
        enter_cb = []() {
            s_active_mode = ActiveMode::INVADERS;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = true;
#endif
            lv_indev_set_group(s_enc_indev, nullptr);
        };
        leave_cb = []() {
            if (s_invaders_game) {
                s_invaders_game->stop();
                delete s_invaders_game;
                s_invaders_game = nullptr;
            }
            s_inv_score_lbl = nullptr;
            s_inv_lives_lbl = nullptr;
            s_inv_level_lbl = nullptr;
            s_inv_input_lbl = nullptr;
            s_inv_wpm_lbl   = nullptr;
            s_inv_gameover_lbl = nullptr;
#ifdef BOARD_POCKETWROOM
            s_cw_engine_active = false;
#endif
            s_active_mode = ActiveMode::NONE;
            s_audio->tone_off();
            delete s_active_sb; s_active_sb = nullptr;
        };
    }

    // Remember last CW mode (0–3) for quick start
    if (idx <= 3) {
        s_settings.last_mode = (uint8_t)idx;
        save_settings();
    }

    s_stack.push(ns, std::move(enter_cb), std::move(leave_cb));
}

// ── Screen: Main Menu ──────────────────────────────────────────────────────
static lv_obj_t* build_main_menu()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

#ifdef NATIVE_BUILD
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Morserino-32-NG");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93 = scroll    e = select    Esc = quit");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 26);
#endif

    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_style_text_font(list, menu_font(), 0);
#ifdef NATIVE_BUILD
    lv_obj_set_size(list, SCREEN_W - 60, SCREEN_H - 100);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 20);
#else
    lv_obj_set_size(list, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
#endif

    if (s_menu_group) lv_group_del(s_menu_group);
    s_menu_group = lv_group_create();

    static const struct { const char* icon; const char* label; } items[] = {
        { LV_SYMBOL_AUDIO,    "CW Keyer"      },
        { LV_SYMBOL_PLAY,     "CW Generator"  },
        { LV_SYMBOL_LOOP,     "Echo Trainer"  },
        { LV_SYMBOL_CALL,     "QSO Chatbot"   },
        { LV_SYMBOL_LIST,     "Content"       },
        { LV_SYMBOL_SETTINGS, "Settings"      },
        { LV_SYMBOL_WIFI,     "WiFi Setup"    },
        { LV_SYMBOL_WIFI,     "Internet CW"   },
        { LV_SYMBOL_SHUFFLE,  "CW Invaders"   },
    };
    for (int i = 0; i < 9; ++i) {
        lv_obj_t* btn = lv_list_add_button(list, items[i].icon, items[i].label);
        lv_obj_set_style_text_font(btn, menu_font(), 0);
        lv_group_add_obj(s_menu_group, btn);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            push_mode_screen((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    return scr;
}

// ── Screen: CW Keyer ──────────────────────────────────────────────────────
static lv_obj_t* build_keyer_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_KEYBOARD);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight  "
        "\xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, content_y() + 2);
    lv_coord_t tf_y = content_y() + 24;
#else
    lv_coord_t tf_y = content_y() + 2;
#endif

    s_keyer_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_keyer_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_keyer_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

    return scr;
}

// ── Screen: CW Generator ──────────────────────────────────────────────────
static lv_obj_t* build_generator_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_PLAY);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "a=pause/resume  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, content_y() + 2);
    lv_coord_t tf_y = content_y() + 24;
#else
    lv_coord_t tf_y = content_y() + 2;
#endif

    s_gen_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_gen_tf->obj(), 4, tf_y);
    lv_obj_set_size(s_gen_tf->obj(), SCREEN_W - 8, SCREEN_H - tf_y - 4);

    return scr;
}

// ── Screen: Echo Trainer ──────────────────────────────────────────────────
static lv_obj_t* build_echo_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_LOOP);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

    const lv_coord_t ROW_H  = (SCREEN_H - content_y()) / 3;
    const lv_coord_t ROW1_Y = content_y() + 4;
    const lv_coord_t ROW2_Y = content_y() + ROW_H + 4;

    lv_obj_t* l1 = lv_label_create(scr);
    lv_label_set_text(l1, "Sent:");
    lv_obj_set_pos(l1, 8, ROW1_Y + 8);

    s_echo_target_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_target_lbl, "?");
    lv_obj_set_style_text_font(s_echo_target_lbl, cw_text_font(), 0);
    lv_obj_set_pos(s_echo_target_lbl, 60, ROW1_Y + 6);
    lv_obj_set_width(s_echo_target_lbl, SCREEN_W - 68);

    lv_obj_t* l2 = lv_label_create(scr);
    lv_label_set_text(l2, "Rcvd:");
    lv_obj_set_pos(l2, 8, ROW2_Y + 8);

    s_echo_rcvd_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_rcvd_lbl, "");
    lv_obj_set_style_text_font(s_echo_rcvd_lbl, cw_text_font(), 0);
    lv_obj_set_pos(s_echo_rcvd_lbl, 60, ROW2_Y + 6);
    lv_obj_set_width(s_echo_rcvd_lbl, SCREEN_W - 68);

    s_echo_result_lbl = lv_label_create(scr);
    lv_label_set_text(s_echo_result_lbl, "");
    lv_obj_align(s_echo_result_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Space=DIT  Enter=DAH  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 8, -22);
#endif

    return scr;
}

// ── Screen: QSO Chatbot ──────────────────────────────────────────────────
static lv_obj_t* build_chatbot_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_ENVELOPE);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint,
        "Space=DIT  Enter=DAH  /=Straight  "
        "\xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, content_y() + 2);
    lv_coord_t base_y = content_y() + 24;
#else
    lv_coord_t base_y = content_y() + 2;
#endif

    // Bot label + text field (upper half)
    lv_obj_t* bot_lbl = lv_label_create(scr);
    lv_label_set_text(bot_lbl, "Bot:");
    lv_obj_set_pos(bot_lbl, 4, base_y);

    lv_coord_t mid_y = (SCREEN_H - base_y) / 2 + base_y;
    s_chatbot_bot_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_chatbot_bot_tf->obj(), 4, base_y + 16);
    lv_obj_set_size(s_chatbot_bot_tf->obj(), SCREEN_W - 8, mid_y - base_y - 22);

    // Operator label + text field (lower half)
    lv_obj_t* op_lbl = lv_label_create(scr);
    lv_label_set_text(op_lbl, "You:");
    lv_obj_set_pos(op_lbl, 4, mid_y);

    s_chatbot_oper_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_chatbot_oper_tf->obj(), 4, mid_y + 16);
    lv_obj_set_size(s_chatbot_oper_tf->obj(), SCREEN_W - 8, SCREEN_H - mid_y - 40);

    // State label at bottom
    s_chatbot_state_lbl = lv_label_create(scr);
    lv_label_set_text(s_chatbot_state_lbl, "[IDLE]");
    lv_obj_align(s_chatbot_state_lbl, LV_ALIGN_BOTTOM_LEFT, 8, -4);

    return scr;
}

// ── Screen: Settings ──────────────────────────────────────────────────────
static lv_obj_t* build_settings_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_SETTINGS);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

    if (s_settings_group) lv_group_del(s_settings_group);
    s_settings_group = lv_group_create();

    const bool compact = (SCREEN_H <= 200);
    const lv_coord_t PAD = compact ? 4 : 8;

    // Scrollable container below the status bar
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_pos(cont, 0, content_y());
    lv_obj_set_size(cont, SCREEN_W, SCREEN_H - content_y());
    lv_obj_set_style_text_font(cont, menu_font(), 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, PAD, 0);
    lv_obj_set_style_pad_all(cont, PAD, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    // Helper: create a label + control row
    auto make_row = [&](const char* label_text) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(cont);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label_text);
        return row;
    };

    // WPM
    {
        lv_obj_t* row = make_row("Speed (WPM)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 5, 40);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.wpm);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.wpm = (int)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Farnsworth (effective WPM, 0 = off)
    {
        lv_obj_t* row = make_row("Farnsworth (0=off)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 40);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.farnsworth);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.farnsworth = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Keyer mode
    {
        lv_obj_t* row = make_row("Keyer Mode");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Iambic A\nIambic B");
        lv_dropdown_set_selected(dd, s_settings.mode_a ? 0u : 1u);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.mode_a = (lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 0u);
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Curtis B DitT%
    {
        lv_obj_t* row = make_row("CurtisB DitT%");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 100);
        lv_spinbox_set_step(spn, 5);
        lv_spinbox_set_digit_count(spn, 3);
        lv_spinbox_set_value(spn, s_settings.curtisb_dit_pct);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.curtisb_dit_pct = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Curtis B DahT%
    {
        lv_obj_t* row = make_row("CurtisB DahT%");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 100);
        lv_spinbox_set_step(spn, 5);
        lv_spinbox_set_digit_count(spn, 3);
        lv_spinbox_set_value(spn, s_settings.curtisb_dah_pct);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.curtisb_dah_pct = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Frequency
    {
        lv_obj_t* row = make_row("Freq (Hz)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 400, 900);
        lv_spinbox_set_digit_count(spn, 3);
        lv_spinbox_set_step(spn, 10);
        lv_spinbox_set_value(spn, s_settings.freq_hz);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.freq_hz = (uint16_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Volume
    {
        lv_obj_t* row = make_row("Volume (0-20)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 20);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.volume);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.volume = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            s_audio->set_volume(s_settings.volume);
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // ADSR
    {
        lv_obj_t* row = make_row("ADSR (ms)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 1, 15);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.adsr_ms);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.adsr_ms = (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Text Size
    {
        lv_obj_t* row = make_row("Text Size");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Normal\nLarge");
        lv_dropdown_set_selected(dd, s_settings.text_font_size);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.text_font_size =
                (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Echo max repeats
    {
        lv_obj_t* row = make_row("Echo rpt (0=\xe2\x88\x9e)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 9);
        lv_spinbox_set_digit_count(spn, 1);
        lv_spinbox_set_value(spn, s_settings.echo_max_repeats);
        lv_obj_set_width(spn, 80);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.echo_max_repeats =
                (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Adaptive speed
    {
        lv_obj_t* row = make_row("Adaptive WPM");
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (s_settings.adaptive_speed) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_group_add_obj(s_settings_group, cb);
        lv_obj_add_event_cb(cb, [](lv_event_t* e) {
            s_settings.adaptive_speed =
                lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // QSO depth
    {
        lv_obj_t* row = make_row("QSO Depth");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Minimal\nStandard\nRagchew");
        lv_dropdown_set_selected(dd, s_settings.chatbot_qso_depth);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.chatbot_qso_depth =
                (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
            apply_settings();
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // External key mode
    {
        lv_obj_t* row = make_row("Ext Key");
        lv_obj_t* dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, "Straight\nIambic");
        lv_dropdown_set_selected(dd, s_settings.ext_key_iambic ? 1u : 0u);
        lv_obj_set_width(dd, 140);
        lv_group_add_obj(s_settings_group, dd);
        lv_obj_add_event_cb(dd, [](lv_event_t* e) {
            s_settings.ext_key_iambic =
                (lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1u);
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Paddle dit/dah swap
    {
        lv_obj_t* row = make_row("Paddle Swap");
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (s_settings.paddle_swap) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_group_add_obj(s_settings_group, cb);
        lv_obj_add_event_cb(cb, [](lv_event_t* e) {
            s_settings.paddle_swap =
                (lv_obj_get_state(lv_event_get_target_obj(e)) & LV_STATE_CHECKED) != 0;
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // External key dit/dah swap
    {
        lv_obj_t* row = make_row("Ext Key Swap");
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (s_settings.ext_key_swap) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_group_add_obj(s_settings_group, cb);
        lv_obj_add_event_cb(cb, [](lv_event_t* e) {
            s_settings.ext_key_swap =
                (lv_obj_get_state(lv_event_get_target_obj(e)) & LV_STATE_CHECKED) != 0;
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Screen flip (lefty / custom orientation)
    {
        lv_obj_t* row = make_row("Screen Flip");
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (s_settings.screen_flip) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_group_add_obj(s_settings_group, cb);
        lv_obj_add_event_cb(cb, [](lv_event_t* e) {
            s_settings.screen_flip =
                (lv_obj_get_state(lv_event_get_target_obj(e)) & LV_STATE_CHECKED) != 0;
            save_settings();
            if (s_on_screen_flip) s_on_screen_flip(s_settings.screen_flip);
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Sleep timeout (minutes, 0 = off)
    {
        lv_obj_t* row = make_row("Sleep (min, 0=off)");
        lv_obj_t* spn = lv_spinbox_create(row);
        lv_spinbox_set_range(spn, 0, 60);
        lv_spinbox_set_digit_count(spn, 2);
        lv_spinbox_set_value(spn, s_settings.sleep_timeout_min);
        lv_obj_set_width(spn, 100);
        lv_group_add_obj(s_settings_group, spn);
        lv_obj_add_event_cb(spn, [](lv_event_t* e) {
            s_settings.sleep_timeout_min =
                (uint8_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Quick Start (auto-enter last mode on boot)
    {
        lv_obj_t* row = make_row("Quick Start");
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (s_settings.quick_start) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_group_add_obj(s_settings_group, cb);
        lv_obj_add_event_cb(cb, [](lv_event_t* e) {
            s_settings.quick_start =
                (lv_obj_get_state(lv_event_get_target_obj(e)) & LV_STATE_CHECKED) != 0;
            save_settings();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93=navigate    e=edit value    E=back");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
#endif

    return scr;
}

// ── Screen: Content ───────────────────────────────────────────────────────
static lv_obj_t* build_content_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_LIST);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

    if (s_content_group) lv_group_del(s_content_group);
    s_content_group = lv_group_create();

    const lv_coord_t CB_ROW  = 24;    // checkbox rows (compact)
    const lv_coord_t CTL_ROW = 36;   // control rows (need room for dropdowns)
    const lv_coord_t LBL_X   = 8;
    const lv_coord_t CTL_X   = 140;   // controls start here
    const lv_coord_t CTL_W   = 130;
    const lv_coord_t SPN_W   = 80;

    // Scrollable container below the status bar
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_pos(cont, 0, content_y());
    lv_obj_set_size(cont, SCREEN_W, SCREEN_H - content_y());
    lv_obj_set_style_text_font(cont, menu_font(), 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    lv_coord_t cur_y = 4;  // running Y position

    auto make_cb = [&](const char* text, bool checked) -> lv_obj_t* {
        lv_obj_t* cb = lv_checkbox_create(cont);
        lv_checkbox_set_text(cb, text);
        if (checked) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_set_pos(cb, LBL_X, cur_y);
        lv_group_add_obj(s_content_group, cb);
        cur_y += CB_ROW;
        return cb;
    };

    // Checkboxes — one per row
    lv_obj_t* cb_words = make_cb("Words",         s_settings.cont_words);
    lv_obj_t* cb_abbr  = make_cb("Abbreviations", s_settings.cont_abbrevs);
    lv_obj_t* cb_calls = make_cb("Callsigns",     s_settings.cont_calls);
    lv_obj_t* cb_chars = make_cb("Characters",    s_settings.cont_chars);
    lv_obj_t* cb_qso   = make_cb("QSO",           s_settings.cont_qso);
    cur_y += 4;  // extra gap before controls

    lv_obj_add_event_cb(cb_words, [](lv_event_t* e) {
        s_settings.cont_words = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_abbr, [](lv_event_t* e) {
        s_settings.cont_abbrevs = (lv_obj_get_state(lv_event_get_target_obj(e))
                                   & LV_STATE_CHECKED) != 0;
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_calls, [](lv_event_t* e) {
        s_settings.cont_calls = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_chars, [](lv_event_t* e) {
        s_settings.cont_chars = (lv_obj_get_state(lv_event_get_target_obj(e))
                                 & LV_STATE_CHECKED) != 0;
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(cb_qso, [](lv_event_t* e) {
        s_settings.cont_qso = (lv_obj_get_state(lv_event_get_target_obj(e))
                               & LV_STATE_CHECKED) != 0;
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Character Set
    lv_obj_t* cg_lbl = lv_label_create(cont);
    lv_label_set_text(cg_lbl, "Character Set");
    lv_obj_set_pos(cg_lbl, LBL_X, cur_y + 7);

    lv_obj_t* cg_dd = lv_dropdown_create(cont);
    lv_dropdown_set_options(cg_dd, "Alpha\nAlpha+Num\nAll CW");
    lv_dropdown_set_selected(cg_dd, s_settings.chars_group);
    lv_obj_set_width(cg_dd, CTL_W);
    lv_obj_set_pos(cg_dd, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, cg_dd);
    lv_obj_add_event_cb(cg_dd, [](lv_event_t* e) {
        s_settings.chars_group = (uint8_t)lv_dropdown_get_selected(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

    // Koch Lesson
    lv_obj_t* kl_lbl = lv_label_create(cont);
    lv_label_set_text(kl_lbl, "Koch Lesson (0=off)");
    lv_obj_set_pos(kl_lbl, LBL_X, cur_y + 7);

    lv_obj_t* koch_spn = lv_spinbox_create(cont);
    lv_spinbox_set_range(koch_spn, 0, KOCH_MAX_LESSON);
    lv_spinbox_set_digit_count(koch_spn, 2);
    lv_spinbox_set_value(koch_spn, s_settings.koch_lesson);
    lv_obj_set_width(koch_spn, SPN_W);
    lv_obj_set_pos(koch_spn, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, koch_spn);
    lv_obj_add_event_cb(koch_spn, [](lv_event_t* e) {
        s_settings.koch_lesson = (uint8_t)lv_spinbox_get_value(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

    // Koch Order
    lv_obj_t* ko_lbl = lv_label_create(cont);
    lv_label_set_text(ko_lbl, "Koch Order");
    lv_obj_set_pos(ko_lbl, LBL_X, cur_y + 7);

    lv_obj_t* ko_dd = lv_dropdown_create(cont);
    lv_dropdown_set_options(ko_dd, "LCWO\nMorserino\nCW Academy\nLICW");
    lv_dropdown_set_selected(ko_dd, std::min((uint8_t)(KOCH_ORDER_COUNT - 1),
                                             s_settings.koch_order));
    lv_obj_set_width(ko_dd, CTL_W);
    lv_obj_set_pos(ko_dd, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, ko_dd);
    lv_obj_add_event_cb(ko_dd, [](lv_event_t* e) {
        s_settings.koch_order = (uint8_t)lv_dropdown_get_selected(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

    // Max Length
    lv_obj_t* ml_lbl = lv_label_create(cont);
    lv_label_set_text(ml_lbl, "Max Length");
    lv_obj_set_pos(ml_lbl, LBL_X, cur_y + 7);

    lv_obj_t* ml_spn = lv_spinbox_create(cont);
    lv_spinbox_set_range(ml_spn, 0, 15);
    lv_spinbox_set_digit_count(ml_spn, 2);
    lv_spinbox_set_value(ml_spn, s_settings.word_max_length);
    lv_obj_set_width(ml_spn, SPN_W);
    lv_obj_set_pos(ml_spn, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, ml_spn);
    lv_obj_add_event_cb(ml_spn, [](lv_event_t* e) {
        s_settings.word_max_length = (uint8_t)lv_spinbox_get_value(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

    // QSO Words
    lv_obj_t* qw_lbl = lv_label_create(cont);
    lv_label_set_text(qw_lbl, "QSO Words");
    lv_obj_set_pos(qw_lbl, LBL_X, cur_y + 7);

    lv_obj_t* qw_spn = lv_spinbox_create(cont);
    lv_spinbox_set_range(qw_spn, 0, 9);
    lv_spinbox_set_digit_count(qw_spn, 1);
    lv_spinbox_set_value(qw_spn, s_settings.qso_max_words);
    lv_obj_set_width(qw_spn, SPN_W);
    lv_obj_set_pos(qw_spn, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, qw_spn);
    lv_obj_add_event_cb(qw_spn, [](lv_event_t* e) {
        s_settings.qso_max_words = (uint8_t)lv_spinbox_get_value(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

    // Session Size
    lv_obj_t* ss_lbl = lv_label_create(cont);
    lv_label_set_text(ss_lbl, "Session Size (0=off)");
    lv_obj_set_pos(ss_lbl, LBL_X, cur_y + 7);

    lv_obj_t* ss_spn = lv_spinbox_create(cont);
    lv_spinbox_set_range(ss_spn, 0, 99);
    lv_spinbox_set_digit_count(ss_spn, 2);
    lv_spinbox_set_value(ss_spn, s_settings.session_size);
    lv_obj_set_width(ss_spn, SPN_W);
    lv_obj_set_pos(ss_spn, CTL_X, cur_y + 2);
    lv_group_add_obj(s_content_group, ss_spn);
    lv_obj_add_event_cb(ss_spn, [](lv_event_t* e) {
        s_settings.session_size = (uint8_t)lv_spinbox_get_value(
            lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    cur_y += CTL_ROW;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(cont);
    lv_obj_set_pos(hint, LBL_X, cur_y);
    lv_label_set_text(hint, "\xe2\x86\x91/\xe2\x86\x93=navigate    e=toggle/edit    E=back");
#endif

    return scr;
}

// ── Screen: WiFi Setup ──────────────────────────────────────────────────────

// Forward: rebuild the WiFi screen in portal mode after disconnecting.
static void wifi_start_portal_mode();

static lv_obj_t* build_wifi_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_WIFI);
    s_active_sb = sb;

    if (s_wifi_group) lv_group_del(s_wifi_group);
    s_wifi_group = lv_group_create();

    lv_coord_t y0 = content_y() + 4;

#ifdef BOARD_POCKETWROOM
    bool connected = s_network && s_network->wifi_is_connected();

    if (connected) {
        // ── Connected: show IP, QR to web page, disconnect button ────────
        char ip[20] = {};
        s_network->wifi_get_ip(ip, sizeof(ip));

        char url[48];
        snprintf(url, sizeof(url), "http://%s/", ip);

        // Reserve space for disconnect button at bottom
        lv_coord_t btn_h = lv_font_get_line_height(menu_font()) + 12; // padding
        lv_coord_t qr_max = SCREEN_H - y0 - btn_h - 8; // 8px gap
        lv_obj_t* qr = qr_canvas_create(scr, url, qr_max);
        lv_coord_t qr_side = qr ? ((qr_max / 33) * 33) : 0;
        if (qr) lv_obj_set_pos(qr, 4, y0);

        lv_coord_t tx = qr ? (4 + qr_side + 8) : 8;
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text_fmt(info, "Connected\n%s", ip);
        lv_obj_set_style_text_font(info, menu_font(), 0);
        lv_obj_set_pos(info, tx, y0);
        lv_obj_set_width(info, SCREEN_W - tx - 4);

        if (s_active_sb) s_active_sb->set_wifi(true);

        s_wifi_status_lbl = nullptr;  // not needed in connected state

        // Disconnect button (bottom-left)
        lv_obj_t* disc_btn = lv_button_create(scr);
        lv_obj_t* disc_lbl = lv_label_create(disc_btn);
        lv_label_set_text(disc_lbl, "Disconnect");
        lv_obj_set_style_text_font(disc_lbl, menu_font(), 0);
        lv_obj_align(disc_btn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
        lv_group_add_obj(s_wifi_group, disc_btn);
        lv_obj_add_event_cb(disc_btn, [](lv_event_t*) {
            if (s_network) {
                screenshot_server_stop();
                s_network->wifi_disconnect();
            }
            wifi_start_portal_mode();
        }, LV_EVENT_CLICKED, nullptr);
    } else {
        // ── Not connected: show AP QR for captive portal ─────────────────
        char qr_text[64];
        snprintf(qr_text, sizeof(qr_text), "WIFI:S:%s;T:nopass;P:;;", s_ap_ssid);
        static constexpr int32_t QR_MAX_PX = 130;
        lv_obj_t* qr = qr_canvas_create(scr, qr_text, QR_MAX_PX);
        lv_coord_t qr_side = qr ? ((QR_MAX_PX / 33) * 33) : 0;
        if (qr) lv_obj_set_pos(qr, 4, y0);

        lv_coord_t tx = qr ? (4 + qr_side + 8) : 8;
        lv_obj_t* info = lv_label_create(scr);
        lv_label_set_text_fmt(info, "%s\n192.168.4.1", s_ap_ssid);
        lv_obj_set_style_text_font(info, menu_font(), 0);
        lv_obj_set_pos(info, tx, y0);
        lv_obj_set_width(info, SCREEN_W - tx - 4);

        s_wifi_status_lbl = lv_label_create(scr);
        lv_label_set_text(s_wifi_status_lbl, "");
        lv_obj_set_style_text_font(s_wifi_status_lbl, menu_font(), 0);
        lv_obj_set_pos(s_wifi_status_lbl, tx, y0 + 40);
        lv_obj_set_width(s_wifi_status_lbl, SCREEN_W - tx - 60);
    }
#else
    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info, "WiFi not available\nin simulator");
    lv_obj_set_style_text_font(info, menu_font(), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -10);

    s_wifi_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_wifi_status_lbl, "");
    lv_obj_set_style_text_font(s_wifi_status_lbl, menu_font(), 0);
    lv_obj_align(s_wifi_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -28);
#endif

    // Back button (bottom-right)
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Back");
    lv_obj_set_style_text_font(lbl, menu_font(), 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_group_add_obj(s_wifi_group, btn);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        s_stack.pop();
        if (s_stack.size() == 1)
            lv_indev_set_group(s_enc_indev, s_menu_group);
    }, LV_EVENT_CLICKED, nullptr);

    return scr;
}

// Rebuild WiFi screen in portal (not-connected) mode.
// Called from the Disconnect button handler.
static void wifi_start_portal_mode()
{
#ifdef BOARD_POCKETWROOM
    // Pop triggers leave_cb which cleans up portal/QR.
    s_stack.pop();
    // Re-select the WiFi menu item and "click" it to push a fresh WiFi screen.
    // build_wifi_screen() will see wifi not connected → portal layout.
    // We manually replicate the push+enter sequence from menu_select.
    lv_obj_t* ns = build_wifi_screen();
    s_stack.push(ns,
        []() {
            lv_indev_set_group(s_enc_indev, s_wifi_group);
            if (s_wifi_status_lbl)
                lv_label_set_text(s_wifi_status_lbl, "Starting...");
            s_wifi_portal_pending = true;
        },
        []() {
            s_wifi_portal_pending = false;
            if (s_portal) { s_portal->end(); delete s_portal; s_portal = nullptr; }
            qr_canvas_destroy();
            s_wifi_status_lbl = nullptr;
            delete s_active_sb; s_active_sb = nullptr;
        });
#endif
}

// ── Screen: Internet CW ──────────────────────────────────────────────────

// Default server hosts
static constexpr const char* CWCOM_DEFAULT_HOST = "mtc-kob.dyndns.org";
static constexpr uint16_t    CWCOM_DEFAULT_PORT = 7890;
static constexpr const char* MOPP_DEFAULT_HOST  = "mopp.hamradio.pl";
static constexpr uint16_t    MOPP_DEFAULT_PORT  = 7373;

// Forward declarations
static lv_obj_t* build_inet_cw_active_screen();
static void      inet_cw_disconnect();

static void inet_cw_connect()
{
    if (!s_network) return;

    // Allocate codecs
    delete s_cwcom_codec; s_cwcom_codec = nullptr;
    delete s_mopp_codec;  s_mopp_codec  = nullptr;
    delete s_rx_player;   s_rx_player   = nullptr;

    const char* host;
    uint16_t port;

    if (s_settings.inet_proto == 0) {
        host = CWCOM_DEFAULT_HOST;
        port = CWCOM_DEFAULT_PORT;
        s_cwcom_codec = new CwComCodec(
            s_settings.callsign[0] ? s_settings.callsign : "M32",
            s_settings.cwcom_wire);
    } else {
        host = MOPP_DEFAULT_HOST;
        port = MOPP_DEFAULT_PORT;
        s_mopp_codec = new MoppCodec((uint8_t)s_settings.wpm);
    }

    // RX player: sidetone + decode
    s_rx_player = new RxCwPlayer(
        [](){ s_audio->tone_on(s_settings.freq_hz); },
        [](){ s_audio->tone_off(); },
        [](const std::string& letter) {
            if (s_inet_rx_tf) s_inet_rx_tf->add_string(letter);
        },
        app_millis,
        1200u / (unsigned long)s_settings.wpm * 2
    );

    CwProto proto = (s_settings.inet_proto == 0)
                    ? CwProto::CWCOM : CwProto::MOPP;

    if (s_inet_status_lbl)
        lv_label_set_text(s_inet_status_lbl, "Connecting...");

    bool ok = s_network->cw_connect(proto, host, port);
    if (!ok) {
        if (s_inet_status_lbl)
            lv_label_set_text(s_inet_status_lbl, "Connect failed");
        return;
    }

    // Send connect packet
    if (s_settings.inet_proto == 0 && s_cwcom_codec) {
        uint8_t buf[CwComCodec::SHORT_SIZE + CwComCodec::PACKET_SIZE];
        int n = s_cwcom_codec->build_connect(buf);
        s_network->cw_send(buf, CwComCodec::SHORT_SIZE);
        if (n > CwComCodec::SHORT_SIZE)
            s_network->cw_send(buf + CwComCodec::SHORT_SIZE,
                               CwComCodec::PACKET_SIZE);
    } else if (s_mopp_codec) {
        uint8_t buf[16];
        int n = MoppCodec::build_connect(buf, sizeof(buf));
        if (n > 0) s_network->cw_send(buf, (size_t)n);
    }

    s_inet_cw_active = true;
    s_inet_keepalive_t = (unsigned long)app_millis();
    s_timing_ringbuf.clear();

    // Push the active CW screen (RX/TX text fields, encoder=WPM)
    lv_obj_t* cw_scr = build_inet_cw_active_screen();
    s_stack.push(cw_scr,
        []() {
            // Encoder → WPM on the active CW screen
            lv_indev_set_group(s_enc_indev, nullptr);
        },
        []() {
            // Popping back to settings screen — disconnect + cleanup
            inet_cw_disconnect();
            s_keyer_word_pending       = false;
            s_keyer_last_element_end_t = 0;
            s_timing_ringbuf.clear();
            delete s_inet_rx_tf;  s_inet_rx_tf  = nullptr;
            delete s_inet_tx_tf;  s_inet_tx_tf  = nullptr;
            delete s_active_sb;   s_active_sb   = nullptr;
            s_audio->tone_off();
        });
}

static void inet_cw_disconnect()
{
    if (!s_network) return;
    s_inet_cw_active = false;

    if (s_network->cw_is_connected()) {
        if (s_settings.inet_proto == 0 && s_cwcom_codec) {
            uint8_t buf[CwComCodec::SHORT_SIZE];
            s_cwcom_codec->build_disconnect(buf);
            s_network->cw_send(buf, CwComCodec::SHORT_SIZE);
        } else if (s_mopp_codec) {
            uint8_t buf[16];
            int n = MoppCodec::build_disconnect(buf, sizeof(buf));
            if (n > 0) s_network->cw_send(buf, (size_t)n);
        }
        s_network->cw_disconnect();
    }

    delete s_cwcom_codec; s_cwcom_codec = nullptr;
    delete s_mopp_codec;  s_mopp_codec  = nullptr;
    delete s_rx_player;   s_rx_player   = nullptr;
}

// ── Internet CW: settings/connect screen (encoder navigates controls) ────
static lv_obj_t* build_inet_cw_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_GPS);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

    if (s_inet_group) lv_group_del(s_inet_group);
    s_inet_group = lv_group_create();

    lv_coord_t y = content_y() + 4;

    // ── Protocol dropdown ────────────────────────────────────────────
    lv_obj_t* proto_lbl = lv_label_create(scr);
    lv_label_set_text(proto_lbl, "Protocol");
    lv_obj_set_style_text_font(proto_lbl, menu_font(), 0);
    lv_obj_set_pos(proto_lbl, 4, y + 4);

    s_inet_proto_dd = lv_dropdown_create(scr);
    lv_dropdown_set_options(s_inet_proto_dd, "CWCom\nMOPP");
    lv_dropdown_set_selected(s_inet_proto_dd, s_settings.inet_proto);
    lv_obj_set_style_text_font(s_inet_proto_dd, menu_font(), 0);
    lv_obj_set_pos(s_inet_proto_dd, 90, y);
    lv_obj_set_size(s_inet_proto_dd, 100, 30);
    lv_obj_add_event_cb(s_inet_proto_dd, [](lv_event_t* e) {
        s_settings.inet_proto =
            (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
        save_settings();
        // Show/hide wire controls
        if (s_inet_wire_spin && s_inet_wire_lbl) {
            if (s_settings.inet_proto != 0) {
                lv_obj_add_flag(s_inet_wire_spin, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(s_inet_wire_lbl, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(s_inet_wire_spin, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(s_inet_wire_lbl, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(s_inet_group, s_inet_proto_dd);

    y += 34;

    // ── Wire spinbox (CWCom only) ────────────────────────────────────
    s_inet_wire_lbl = lv_label_create(scr);
    lv_label_set_text(s_inet_wire_lbl, "Wire");
    lv_obj_set_style_text_font(s_inet_wire_lbl, menu_font(), 0);
    lv_obj_set_pos(s_inet_wire_lbl, 4, y + 4);

    s_inet_wire_spin = lv_spinbox_create(scr);
    lv_spinbox_set_range(s_inet_wire_spin, 1, 32000);
    lv_spinbox_set_digit_count(s_inet_wire_spin, 5);
    lv_spinbox_set_value(s_inet_wire_spin, s_settings.cwcom_wire);
    lv_obj_set_style_text_font(s_inet_wire_spin, menu_font(), 0);
    lv_obj_set_pos(s_inet_wire_spin, 90, y);
    lv_obj_set_size(s_inet_wire_spin, 100, 30);
    lv_obj_add_event_cb(s_inet_wire_spin, [](lv_event_t* e) {
        s_settings.cwcom_wire =
            (uint16_t)lv_spinbox_get_value(lv_event_get_target_obj(e));
        save_settings();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(s_inet_group, s_inet_wire_spin);

    if (s_settings.inet_proto != 0) {
        lv_obj_add_flag(s_inet_wire_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_inet_wire_spin, LV_OBJ_FLAG_HIDDEN);
    }

    y += 34;

    // ── Status label ─────────────────────────────────────────────────
    s_inet_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_inet_status_lbl, "");
    lv_obj_set_style_text_font(s_inet_status_lbl, menu_font(), 0);
    lv_obj_set_pos(s_inet_status_lbl, 4, y);

    // ── Connect button (bottom-right) ────────────────────────────────
    s_inet_connect_btn = lv_button_create(scr);
    lv_obj_align(s_inet_connect_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_size(s_inet_connect_btn, 100, 34);
    lv_obj_t* btn_lbl = lv_label_create(s_inet_connect_btn);
    lv_label_set_text(btn_lbl, "Connect");
    lv_obj_set_style_text_font(btn_lbl, menu_font(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(s_inet_connect_btn, [](lv_event_t*) {
        inet_cw_connect();
    }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(s_inet_group, s_inet_connect_btn);

    return scr;
}

// ── Internet CW: active CW screen (encoder=WPM, long press=back) ────────
static lv_obj_t* build_inet_cw_active_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    delete s_active_sb; s_active_sb = nullptr;  // free settings screen's bar
    StatusBar* sb = new StatusBar(scr, menu_font());
    sb->set_mode(LV_SYMBOL_GPS);
    sb->set_wpm(s_settings.wpm, s_settings.farnsworth);
    s_active_sb = sb;

    lv_coord_t y = content_y() + 2;

    // ── RX text field (top half) ─────────────────────────────────────
    lv_obj_t* rx_lbl = lv_label_create(scr);
    lv_label_set_text(rx_lbl, "RX");
    lv_obj_set_style_text_font(rx_lbl, menu_font(), 0);
    lv_obj_set_pos(rx_lbl, 4, y);

    lv_coord_t avail = SCREEN_H - y - 4;
    lv_coord_t rx_h = avail / 2 - 2;
    s_inet_rx_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_inet_rx_tf->obj(), 28, y);
    lv_obj_set_size(s_inet_rx_tf->obj(), SCREEN_W - 32, rx_h);
    y += rx_h + 2;

    // ── TX text field (bottom half) ──────────────────────────────────
    lv_obj_t* tx_lbl = lv_label_create(scr);
    lv_label_set_text(tx_lbl, "TX");
    lv_obj_set_style_text_font(tx_lbl, menu_font(), 0);
    lv_obj_set_pos(tx_lbl, 4, y);

    s_inet_tx_tf = new CWTextField(scr, cw_text_font());
    lv_obj_set_pos(s_inet_tx_tf->obj(), 28, y);
    lv_obj_set_size(s_inet_tx_tf->obj(), SCREEN_W - 32, SCREEN_H - y - 4);

    return scr;
}

// ── Key event router ───────────────────────────────────────────────────────

#ifdef BOARD_POCKETWROOM
// ── route_cw(): runs on the dedicated CW task (Core 1, priority 10) ──────
// Only handles CW-related events: touch paddles, external paddles, straight
// key.  Must NOT call any LVGL or MorseTrainer functions.
static void route_cw(KeyEvent ev)
{
    switch (ev) {
        // ── Touch paddles (always iambic, obey paddle_swap) ─────────────
        case KeyEvent::TOUCH_LEFT_DOWN:
            if (s_cw_engine_active) {
                if (s_settings.paddle_swap) s_paddle->setDashPushed(true);
                else                        s_paddle->setDotPushed(true);
            }
            s_cw_tame_echo = true;
            break;
        case KeyEvent::TOUCH_LEFT_UP:
            if (s_cw_engine_active) {
                if (s_settings.paddle_swap) s_paddle->setDashPushed(false);
                else                        s_paddle->setDotPushed(false);
            }
            break;
        case KeyEvent::TOUCH_RIGHT_DOWN:
            if (s_cw_engine_active) {
                if (s_settings.paddle_swap) s_paddle->setDotPushed(true);
                else                        s_paddle->setDashPushed(true);
            }
            s_cw_tame_echo = true;
            break;
        case KeyEvent::TOUCH_RIGHT_UP:
            if (s_cw_engine_active) {
                if (s_settings.paddle_swap) s_paddle->setDotPushed(false);
                else                        s_paddle->setDashPushed(false);
            }
            break;

        // ── External paddle jack (straight or iambic, obey ext_key_swap) ─
        case KeyEvent::PADDLE_DIT_DOWN:
        case KeyEvent::PADDLE_DAH_DOWN: {
            bool phys_dit = (ev == KeyEvent::PADDLE_DIT_DOWN);
            if (s_settings.ext_key_iambic) {
                bool is_dit = s_settings.ext_key_swap ? !phys_dit : phys_dit;
                if (s_cw_engine_active) {
                    if (is_dit) s_paddle->setDotPushed(true);
                    else        s_paddle->setDashPushed(true);
                }
                s_cw_tame_echo = true;
            } else {
                s_straight_key_state = true;
            }
            break;
        }
        case KeyEvent::PADDLE_DIT_UP:
        case KeyEvent::PADDLE_DAH_UP: {
            bool phys_dit = (ev == KeyEvent::PADDLE_DIT_UP);
            if (s_settings.ext_key_iambic) {
                bool is_dit = s_settings.ext_key_swap ? !phys_dit : phys_dit;
                if (s_cw_engine_active) {
                    if (is_dit) s_paddle->setDotPushed(false);
                    else        s_paddle->setDashPushed(false);
                }
            } else {
                s_straight_key_state = false;
            }
            break;
        }

        default:
            break;
    }
}

// ── route_ui(): runs on the Arduino loop task ────────────────────────────
// Handles encoder, buttons — anything that touches LVGL or MorseTrainer.
static void route_ui(KeyEvent ev)
{
    reset_activity_timer();
    switch (ev) {
        case KeyEvent::ENCODER_CW:
            s_enc_diff++;
            // Skip WPM/volume when encoder is navigating an LVGL group
            if (s_active_mode != ActiveMode::NONE &&
                !lv_indev_get_group(s_enc_indev)) {
                switch (s_encoder_mode) {
                    case EncoderMode::VOLUME:
                        s_settings.volume = std::min((int)s_settings.volume + 1, 20);
                        s_audio->set_volume(s_settings.volume);
                        update_status_bar_info();
                        save_settings();
                        break;
                    case EncoderMode::SCROLL: {
                        lv_obj_t* t = active_scroll_target();
                        if (t) lv_obj_scroll_by(t, 0, -20, LV_ANIM_OFF);
                        break;
                    }
                    default:
                        s_settings.wpm = std::min(s_settings.wpm + 1, 40);
                        apply_settings();
                        save_settings();
                        break;
                }
            }
            break;
        case KeyEvent::ENCODER_CCW:
            s_enc_diff--;
            if (s_active_mode != ActiveMode::NONE &&
                !lv_indev_get_group(s_enc_indev)) {
                switch (s_encoder_mode) {
                    case EncoderMode::VOLUME:
                        s_settings.volume = (s_settings.volume > 0)
                            ? s_settings.volume - 1 : 0;
                        s_audio->set_volume(s_settings.volume);
                        update_status_bar_info();
                        save_settings();
                        break;
                    case EncoderMode::SCROLL: {
                        lv_obj_t* t = active_scroll_target();
                        if (t) lv_obj_scroll_by(t, 0, 20, LV_ANIM_OFF);
                        break;
                    }
                    default:
                        s_settings.wpm = std::max(s_settings.wpm - 1, 5);
                        apply_settings();
                        save_settings();
                        break;
                }
            }
            break;

        case KeyEvent::BUTTON_ENCODER_SHORT:
            if (s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_gen_paused = !s_gen_paused;
                if (s_gen_paused) s_trainer->set_idle();
                else { s_session_count = 0; s_trainer->set_playing(); }
            } else if (s_active_mode == ActiveMode::INVADERS && s_invaders_game) {
                s_invaders_game->set_paused(!s_invaders_game->paused());
            } else {
                s_enc_press_frames = 2;
            }
            break;
        case KeyEvent::BUTTON_ENCODER_LONG: {
            lv_group_t* grp = lv_indev_get_group(s_enc_indev);
            if (grp && lv_group_get_editing(grp)) {
                lv_group_set_editing(grp, false);
            } else {
                s_stack.pop();
                if (s_stack.size() == 1)
                    lv_indev_set_group(s_enc_indev, s_menu_group);
            }
            break;
        }

        case KeyEvent::BUTTON_AUX_SHORT:
            if (s_active_mode != ActiveMode::NONE) {
                unsigned long now = (unsigned long)app_millis();
                if (s_fn_pending && (now - s_fn_last_press_t) < FN_DOUBLE_MS) {
                    s_fn_pending = false;
                    enter_encoder_mode(s_encoder_mode == EncoderMode::SCROLL
                        ? EncoderMode::WPM : EncoderMode::SCROLL);
                } else {
                    s_fn_pending = true;
                    s_fn_last_press_t = now;
                }
            }
            break;
        case KeyEvent::BUTTON_AUX_LONG:
            cycle_brightness();
            break;

        default:
            break;
    }
}
#endif // BOARD_POCKETWROOM

// ── route(): single-threaded path used by simulator ──────────────────────
#ifndef BOARD_POCKETWROOM
static void route(KeyEvent ev)
{
    reset_activity_timer();
    switch (ev) {
        case KeyEvent::ENCODER_CW:
            s_enc_diff++;
            // Skip WPM/volume when encoder is navigating an LVGL group
            if (s_active_mode != ActiveMode::NONE &&
                !lv_indev_get_group(s_enc_indev)) {
                switch (s_encoder_mode) {
                    case EncoderMode::VOLUME:
                        s_settings.volume = std::min((int)s_settings.volume + 1, 20);
                        s_audio->set_volume(s_settings.volume);
                        update_status_bar_info();
                        save_settings();
                        break;
                    case EncoderMode::SCROLL: {
                        lv_obj_t* t = active_scroll_target();
                        if (t) lv_obj_scroll_by(t, 0, -20, LV_ANIM_OFF);
                        break;
                    }
                    default:
                        s_settings.wpm = std::min(s_settings.wpm + 1, 40);
                        apply_settings();
                        save_settings();
                        break;
                }
            }
            break;
        case KeyEvent::ENCODER_CCW:
            s_enc_diff--;
            if (s_active_mode != ActiveMode::NONE &&
                !lv_indev_get_group(s_enc_indev)) {
                switch (s_encoder_mode) {
                    case EncoderMode::VOLUME:
                        s_settings.volume = (s_settings.volume > 0)
                            ? s_settings.volume - 1 : 0;
                        s_audio->set_volume(s_settings.volume);
                        update_status_bar_info();
                        save_settings();
                        break;
                    case EncoderMode::SCROLL: {
                        lv_obj_t* t = active_scroll_target();
                        if (t) lv_obj_scroll_by(t, 0, 20, LV_ANIM_OFF);
                        break;
                    }
                    default:
                        s_settings.wpm = std::max(s_settings.wpm - 1, 5);
                        apply_settings();
                        save_settings();
                        break;
                }
            }
            break;

        case KeyEvent::BUTTON_ENCODER_SHORT:
            if (s_active_mode == ActiveMode::GENERATOR ||
                s_active_mode == ActiveMode::ECHO ||
                s_active_mode == ActiveMode::CHATBOT) {
                s_gen_paused = !s_gen_paused;
                if (s_gen_paused) s_trainer->set_idle();
                else { s_session_count = 0; s_trainer->set_playing(); }
            } else if (s_active_mode == ActiveMode::INVADERS && s_invaders_game) {
                s_invaders_game->set_paused(!s_invaders_game->paused());
            } else {
                s_enc_press_frames = 2;
            }
            break;
        case KeyEvent::BUTTON_ENCODER_LONG: {
            lv_group_t* grp = lv_indev_get_group(s_enc_indev);
            if (grp && lv_group_get_editing(grp)) {
                lv_group_set_editing(grp, false);
            } else {
                s_stack.pop();
                if (s_stack.size() == 1)
                    lv_indev_set_group(s_enc_indev, s_menu_group);
            }
            break;
        }

        case KeyEvent::BUTTON_AUX_SHORT:
            if (s_active_mode != ActiveMode::NONE) {
                unsigned long now = (unsigned long)app_millis();
                if (s_fn_pending && (now - s_fn_last_press_t) < FN_DOUBLE_MS) {
                    s_fn_pending = false;
                    enter_encoder_mode(s_encoder_mode == EncoderMode::SCROLL
                        ? EncoderMode::WPM : EncoderMode::SCROLL);
                } else {
                    s_fn_pending = true;
                    s_fn_last_press_t = now;
                }
            }
            break;
        case KeyEvent::BUTTON_AUX_LONG:
            cycle_brightness();
            break;

        // ── Touch paddles (always iambic, obey paddle_swap) ─────────────
        case KeyEvent::TOUCH_LEFT_DOWN:
            if (cw_mode_active()) {
                if (s_settings.paddle_swap) s_paddle->setDashPushed(true);
                else                        s_paddle->setDotPushed(true);
            }
            if (s_active_mode == ActiveMode::ECHO) s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::TOUCH_LEFT_UP:
            if (cw_mode_active()) {
                if (s_settings.paddle_swap) s_paddle->setDashPushed(false);
                else                        s_paddle->setDotPushed(false);
            }
            break;
        case KeyEvent::TOUCH_RIGHT_DOWN:
            if (cw_mode_active()) {
                if (s_settings.paddle_swap) s_paddle->setDotPushed(true);
                else                        s_paddle->setDashPushed(true);
            }
            if (s_active_mode == ActiveMode::ECHO) s_trainer->tame_echo_timeout();
            break;
        case KeyEvent::TOUCH_RIGHT_UP:
            if (cw_mode_active()) {
                if (s_settings.paddle_swap) s_paddle->setDotPushed(false);
                else                        s_paddle->setDashPushed(false);
            }
            break;

        // ── External paddle jack (straight or iambic, obey ext_key_swap) ─
        case KeyEvent::PADDLE_DIT_DOWN:
        case KeyEvent::PADDLE_DAH_DOWN: {
            bool phys_dit = (ev == KeyEvent::PADDLE_DIT_DOWN);
            if (s_settings.ext_key_iambic) {
                bool is_dit = s_settings.ext_key_swap ? !phys_dit : phys_dit;
                if (cw_mode_active()) {
                    if (is_dit) s_paddle->setDotPushed(true);
                    else        s_paddle->setDashPushed(true);
                }
                if (s_active_mode == ActiveMode::ECHO)
                    s_trainer->tame_echo_timeout();
            } else {
                s_straight_key_state = true;
            }
            break;
        }
        case KeyEvent::PADDLE_DIT_UP:
        case KeyEvent::PADDLE_DAH_UP: {
            bool phys_dit = (ev == KeyEvent::PADDLE_DIT_UP);
            if (s_settings.ext_key_iambic) {
                bool is_dit = s_settings.ext_key_swap ? !phys_dit : phys_dit;
                if (cw_mode_active()) {
                    if (is_dit) s_paddle->setDotPushed(false);
                    else        s_paddle->setDashPushed(false);
                }
            } else {
                s_straight_key_state = false;
            }
            break;
        }

        // ── Straight key — StraightKeyer polls; events bridge for sim ────
        case KeyEvent::STRAIGHT_DOWN:
            s_straight_key_state = true;
            break;
        case KeyEvent::STRAIGHT_UP:
            s_straight_key_state = false;
            break;

        default:
            break;
    }
}
#endif // !BOARD_POCKETWROOM

// ── CW task infrastructure (pocketwroom only) ───────────────────────────
#ifdef BOARD_POCKETWROOM
// Decoder callback for CW task context — enqueues decoded symbols for the
// UI task instead of touching LVGL directly.
static void on_letter_decoded_cw(const std::string& letter)
{
    DecodedSymbol sym;
    strncpy(sym.text, letter.c_str(), sizeof(sym.text) - 1);
    sym.text[sizeof(sym.text) - 1] = '\0';
    xQueueSend(s_decoded_symbol_queue, &sym, 0);
}

// CW task body — runs at priority 10 on Core 1, ~1 kHz tick rate.
// Sole consumer of PocketKeyInput event queue.  Forwards non-CW events
// to s_ui_event_queue for the Arduino loop task.
static void cw_task_body(void* /*arg*/)
{
    while (true) {
        KeyEvent ev;
        // Block up to 1 ms — gives ~1 kHz tick rate when idle
        if (s_keys->wait(ev, 1)) {
            switch (ev) {
            case KeyEvent::TOUCH_LEFT_DOWN:
            case KeyEvent::TOUCH_LEFT_UP:
            case KeyEvent::TOUCH_RIGHT_DOWN:
            case KeyEvent::TOUCH_RIGHT_UP:
            case KeyEvent::PADDLE_DIT_DOWN:
            case KeyEvent::PADDLE_DIT_UP:
            case KeyEvent::PADDLE_DAH_DOWN:
            case KeyEvent::PADDLE_DAH_UP:
            case KeyEvent::STRAIGHT_DOWN:
            case KeyEvent::STRAIGHT_UP:
                route_cw(ev);
                break;
            default:
                xQueueSend(s_ui_event_queue, &ev, 0);
                break;
            }
        }

        if (s_cw_engine_active) {
            s_paddle->tick();
            s_keyer->tick();
            if (s_straight_keyer) s_straight_keyer->decode();
            s_decoder->tick();
        }
    }
}
#endif // BOARD_POCKETWROOM

// ── Shared CW engine + trainer + LVGL init ────────────────────────────────
// Call after s_audio and s_keys are assigned.
static void app_ui_init(uint32_t rng_seed)
{
    s_last_activity_t = (unsigned long)app_millis();
    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
    s_rng.seed(rng_seed);
    s_gen     = new TextGenerators(s_rng);
#ifdef BOARD_POCKETWROOM
    s_decoder = new MorseDecoder(dit_ms * 2, on_letter_decoded_cw, app_millis);
#else
    s_decoder = new MorseDecoder(dit_ms * 2, on_letter_decoded, app_millis);
#endif
    s_keyer   = new IambicKeyer(dit_ms, on_play_state, app_millis, s_settings.mode_a);
    s_paddle  = new PaddleCtl(/*debounce_ms=*/2, on_lever_state, app_millis);
    if (s_read_straight_key)
        s_straight_keyer = new StraightKeyer(on_straight_state,
                                             s_read_straight_key, app_millis);

    s_trainer = new MorseTrainer(
        [](bool on) {
            if (on) {
                s_audio->tone_on(s_settings.freq_hz);
#ifdef BOARD_POCKETWROOM
                digitalWrite(PIN_KEYER, HIGH);
#endif
            } else {
                s_audio->tone_off();
#ifdef BOARD_POCKETWROOM
                digitalWrite(PIN_KEYER, LOW);
#endif
            }
        },
        []() -> std::string {
            // Chatbot: return pending phrase, or empty to go Idle.
            // Signal tx-done here (before trainer goes Idle) to avoid
            // the 3-second ADVANCE_PHRASE_DELAY latency.
            if (s_active_mode == ActiveMode::CHATBOT) {
                if (!s_chatbot_pending_phrase.empty()) {
                    std::string p = s_chatbot_pending_phrase;
                    s_chatbot_pending_phrase.clear();
                    return p;
                }
                s_chatbot_tx_active = false;
                return std::string();
            }
            // Session limit: auto-pause after N phrases
            if (s_settings.session_size > 0 &&
                s_session_count >= (int)s_settings.session_size) {
                // Flush the last played phrase to the display before pausing
                if (s_gen_tf && !s_pending_gen_phrase.empty()) {
                    s_gen_tf->add_string(s_pending_gen_phrase + " ");
                    s_pending_gen_phrase.clear();
                }
                s_gen_paused = true;
                s_trainer->set_idle();
                return std::string();
            }
            std::string phrase = content_phrase();
            if (s_settings.session_size > 0) s_session_count++;
            // Generator: deferred — show the just-finished word, queue the new one
            if (s_gen_tf) {
                if (!s_pending_gen_phrase.empty())
                    s_gen_tf->add_string(s_pending_gen_phrase + " ");
                s_pending_gen_phrase = phrase;
            }
            // Echo: reset for new round; hide target until user echoes it back
            if (s_echo_target_lbl) {
                s_echo_typed.clear();
                s_echo_consecutive_e = 0;
                if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
                if (s_echo_result_lbl) lv_label_set_text(s_echo_result_lbl, "");
                lv_label_set_text(s_echo_target_lbl, "?");
            }
            return phrase;
        },
        app_millis);
    s_trainer->set_state(MorseTrainer::TrainerState::Player);
    s_trainer->set_speed_wpm(s_settings.wpm);
    s_trainer->set_echo_result_fn([](const std::string& phrase, bool success) {
        s_audio->play_effect(success ? SoundEffect::SUCCESS : SoundEffect::ERROR);
        // Only reveal on success; ERR keeps "?" so the word can be replayed blind
        if (success && s_echo_target_lbl)
            lv_label_set_text(s_echo_target_lbl, phrase.c_str());
        // Clear received display so the next round starts clean
        s_echo_typed.clear();
        s_echo_consecutive_e = 0;
        if (s_echo_rcvd_lbl) lv_label_set_text(s_echo_rcvd_lbl, "");
        if (s_echo_result_lbl) {
            lv_label_set_text(s_echo_result_lbl, success ? "OK" : "ERR");
            lv_obj_set_style_text_color(s_echo_result_lbl,
                success ? lv_palette_main(LV_PALETTE_GREEN)
                        : lv_palette_main(LV_PALETTE_RED), 0);
        }
    });
    s_trainer->set_echo_reveal_fn([](const std::string& phrase) {
        s_audio->play_effect(SoundEffect::ERROR);
        // Max repeats exhausted — show correct phrase and "MISS"
        if (s_echo_target_lbl) lv_label_set_text(s_echo_target_lbl, phrase.c_str());
        s_echo_typed.clear();
        s_echo_consecutive_e = 0;
        if (s_echo_rcvd_lbl)   lv_label_set_text(s_echo_rcvd_lbl, "");
        if (s_echo_result_lbl) {
            lv_label_set_text(s_echo_result_lbl, "MISS");
            lv_obj_set_style_text_color(s_echo_result_lbl,
                lv_palette_main(LV_PALETTE_ORANGE), 0);
        }
    });

    s_enc_indev = lv_indev_create();
    lv_indev_set_type(s_enc_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_enc_indev, enc_read_cb);

    s_stack.push(build_main_menu(),
        []() {
            lv_indev_set_group(s_enc_indev, s_menu_group);
            // Apply focus-key state so the first item is highlighted
            // immediately.  Normally LVGL only sets this during encoder
            // indev processing, so without actual input the highlight
            // would be missing on the first frame.
            lv_obj_t* f = lv_group_get_focused(s_menu_group);
            if (f) lv_obj_add_state(f, LV_STATE_FOCUS_KEY);
        },
        {});

    // Propagate all loaded settings (ADSR, volume, WPM, etc.) to engines.
    apply_settings();

    // Quick start: auto-jump into last CW mode
    if (s_settings.quick_start && s_settings.last_mode <= 3) {
        push_mode_screen(s_settings.last_mode);
    }

#ifdef BOARD_POCKETWROOM
    // Create cross-task queues and launch the dedicated CW task.
    // Must be last — CW task immediately starts consuming PocketKeyInput.
    s_ui_event_queue      = xQueueCreate(32, sizeof(KeyEvent));
    s_decoded_symbol_queue = xQueueCreate(16, sizeof(DecodedSymbol));
    xTaskCreatePinnedToCore(cw_task_body, "cw_engine",
                            4096, nullptr, 10, &s_cw_task_handle, 1);
#endif
}

// ── Screen: CW Invaders ──────────────────────────────────────────────────

static void update_invaders_hud()
{
    if (!s_invaders_game) return;
    char buf[16];
    if (s_inv_level_lbl) {
        snprintf(buf, sizeof(buf), "Lv%d ", s_invaders_game->level());
        lv_label_set_text(s_inv_level_lbl, buf);
    }
    if (s_inv_lives_lbl) {
        snprintf(buf, sizeof(buf), "x%d ", s_invaders_game->lives());
        lv_label_set_text(s_inv_lives_lbl, buf);
        lv_obj_align_to(s_inv_lives_lbl, s_inv_level_lbl,
                        LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }
    if (s_inv_score_lbl) {
        snprintf(buf, sizeof(buf), "%04d ", s_invaders_game->score());
        lv_label_set_text(s_inv_score_lbl, buf);
        lv_obj_align_to(s_inv_score_lbl, s_inv_lives_lbl,
                        LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }
    if (s_inv_wpm_lbl) {
        snprintf(buf, sizeof(buf), "%dWPM", s_settings.wpm);
        lv_label_set_text(s_inv_wpm_lbl, buf);
        lv_obj_align_to(s_inv_wpm_lbl, s_inv_score_lbl,
                        LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }
}

static lv_obj_t* build_invaders_screen()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // No StatusBar — maximise game area; all info in bottom strip
    delete s_active_sb;
    s_active_sb = nullptr;

    lv_coord_t y0 = 0;

#ifdef NATIVE_BUILD
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Space=DIT  Enter=DAH  \xe2\x86\x91/\xe2\x86\x93=WPM  E=back");
    lv_obj_set_style_text_font(hint, ui_font(), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 0);
    y0 = 18;
#endif

    // Game area — characters scroll here (maximised)
    lv_coord_t bottom_h = lv_font_get_line_height(ui_font()) + 4;
    lv_obj_t* game_area = lv_obj_create(scr);
    lv_obj_clear_flag(game_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(game_area, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(game_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(game_area, 0, 0);
    lv_obj_set_style_pad_all(game_area, 0, 0);
    lv_obj_set_style_radius(game_area, 0, 0);
    lv_coord_t game_h = SCREEN_H - y0 - bottom_h;
    lv_obj_set_size(game_area, SCREEN_W, game_h);
    lv_obj_set_pos(game_area, 0, y0);

    // Bottom strip: chained colored labels (each positioned relative to previous)
    s_inv_level_lbl = lv_label_create(scr);
    lv_label_set_text(s_inv_level_lbl, "Lv1 ");
    lv_obj_set_style_text_font(s_inv_level_lbl, ui_font(), 0);
    lv_obj_set_style_text_color(s_inv_level_lbl, lv_color_hex(0x88FF88), 0);
    lv_obj_align(s_inv_level_lbl, LV_ALIGN_BOTTOM_LEFT, 4, 0);

    s_inv_lives_lbl = lv_label_create(scr);
    lv_label_set_text(s_inv_lives_lbl, "x3 ");
    lv_obj_set_style_text_font(s_inv_lives_lbl, ui_font(), 0);
    lv_obj_set_style_text_color(s_inv_lives_lbl, lv_color_hex(0xFF4444), 0);
    lv_obj_align_to(s_inv_lives_lbl, s_inv_level_lbl,
                    LV_ALIGN_OUT_RIGHT_MID, 0, 0);

    s_inv_score_lbl = lv_label_create(scr);
    lv_label_set_text(s_inv_score_lbl, "0000 ");
    lv_obj_set_style_text_font(s_inv_score_lbl, ui_font(), 0);
    lv_obj_set_style_text_color(s_inv_score_lbl, lv_color_hex(0xFFFF00), 0);
    lv_obj_align_to(s_inv_score_lbl, s_inv_lives_lbl,
                    LV_ALIGN_OUT_RIGHT_MID, 0, 0);

    {
        char wbuf[12];
        snprintf(wbuf, sizeof(wbuf), "%dWPM", s_settings.wpm);
        s_inv_wpm_lbl = lv_label_create(scr);
        lv_label_set_text(s_inv_wpm_lbl, wbuf);
        lv_obj_set_style_text_font(s_inv_wpm_lbl, ui_font(), 0);
        lv_obj_set_style_text_color(s_inv_wpm_lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_align_to(s_inv_wpm_lbl, s_inv_score_lbl,
                        LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    }

    s_inv_input_lbl = lv_label_create(scr);
    lv_label_set_text(s_inv_input_lbl, "");
    lv_obj_set_style_text_font(s_inv_input_lbl, ui_font(), 0);
    lv_obj_set_style_text_color(s_inv_input_lbl, lv_color_hex(0x88CCFF), 0);
    lv_obj_align(s_inv_input_lbl, LV_ALIGN_BOTTOM_RIGHT, -4, 0);

    // Game-over label (hidden initially)
    s_inv_gameover_lbl = lv_label_create(game_area);
    lv_label_set_text(s_inv_gameover_lbl, "GAME OVER");
    lv_obj_set_style_text_font(s_inv_gameover_lbl, cw_text_font(), 0);
    lv_obj_set_style_text_color(s_inv_gameover_lbl, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_align(s_inv_gameover_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_inv_gameover_lbl);
    lv_obj_add_flag(s_inv_gameover_lbl, LV_OBJ_FLAG_HIDDEN);

    // Create game instance
    s_invaders_game = new CwInvaders(game_area, cw_text_font(),
                                     s_rng, app_millis);
    s_invaders_game->start();

    return scr;
}

// ── Internet CW network pump ──────────────────────────────────────────────
// Called from app_ui_tick() when INTERNET_CW mode is active.
// Drains TX timing ring buffer → codec → network send.
// Polls RX from network → codec → RxCwPlayer for sidetone + decode.
static void inet_cw_tick()
{
    if (!s_network || !s_network->cw_is_connected()) return;
    unsigned long now = (unsigned long)app_millis();

    // ── TX: drain timing ring buffer → codec → send ─────────────────────
    TimingEvent ev;
    static uint32_t last_down_ts = 0;
    static bool     last_was_down = false;

    while (s_timing_ringbuf.pop(ev)) {
        if (ev.key_down) {
            // If previous was also key-down (shouldn't happen), emit gap
            if (last_was_down && last_down_ts > 0) {
                int32_t gap = -(int32_t)(ev.timestamp_ms - last_down_ts);
                if (s_settings.inet_proto == 0 && s_cwcom_codec) {
                    uint8_t pkt[CwComCodec::PACKET_SIZE];
                    if (s_cwcom_codec->push_timing(gap, pkt))
                        s_network->cw_send(pkt, CwComCodec::PACKET_SIZE);
                }
            }
            last_down_ts = ev.timestamp_ms;
            last_was_down = true;
        } else {
            // Key up: compute key-down duration
            if (last_was_down && last_down_ts > 0) {
                int32_t down_ms = (int32_t)(ev.timestamp_ms - last_down_ts);
                if (down_ms < 1) down_ms = 1;

                if (s_settings.inet_proto == 0 && s_cwcom_codec) {
                    uint8_t pkt[CwComCodec::PACKET_SIZE];
                    if (s_cwcom_codec->push_timing(down_ms, pkt))
                        s_network->cw_send(pkt, CwComCodec::PACKET_SIZE);
                } else if (s_settings.inet_proto == 1 && s_mopp_codec) {
                    // MOPP: classify as dit/dah
                    unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
                    if ((unsigned long)down_ms <= dit_ms * 2)
                        s_mopp_codec->push_dit();
                    else
                        s_mopp_codec->push_dah();
                }
            }
            last_was_down = false;
            last_down_ts = ev.timestamp_ms;  // remember for gap computation
        }
    }

    // ── TX: word-space flush ─────────────────────────────────────────────
    // Reuse word-space detection: if keyer_word_pending just cleared
    // (7-dit gap elapsed), flush partial CWCom packet or send MOPP word
    if (!s_keyer_word_pending && last_was_down) {
        unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
        if (last_down_ts > 0 &&
            (now - last_down_ts) > 7 * dit_ms) {
            // Emit trailing gap + flush
            int32_t gap = -(int32_t)(now - last_down_ts);
            if (s_settings.inet_proto == 0 && s_cwcom_codec) {
                uint8_t pkt[CwComCodec::PACKET_SIZE];
                if (s_cwcom_codec->push_timing(gap, pkt))
                    s_network->cw_send(pkt, CwComCodec::PACKET_SIZE);
                if (s_cwcom_codec->flush(pkt))
                    s_network->cw_send(pkt, CwComCodec::PACKET_SIZE);
            } else if (s_settings.inet_proto == 1 && s_mopp_codec) {
                s_mopp_codec->push_word_gap();
                uint8_t buf[32];
                int len = s_mopp_codec->build_packet(buf, sizeof(buf));
                if (len > 0)
                    s_network->cw_send(buf, (size_t)len);
            }
            last_was_down = false;
        }
    }

    // ── RX: poll network ────────────────────────────────────────────────
    {
        uint8_t buf[512];
        int n = s_network->cw_receive(buf, sizeof(buf), 0);  // non-blocking
        if (n > 0 && s_rx_player) {
            if (s_settings.inet_proto == 0 && s_cwcom_codec) {
                // Dedup: MorseKOB server sends each packet twice
                static int32_t last_rx_seq = -1;
                if (n >= 496) {
                    int32_t seq = (int32_t)((uint32_t)buf[136] |
                                  ((uint32_t)buf[137]<<8) |
                                  ((uint32_t)buf[138]<<16) |
                                  ((uint32_t)buf[139]<<24));
                    if (seq == last_rx_seq) goto rx_done;
                    last_rx_seq = seq;
                }
                s_cwcom_codec->parse(buf, (size_t)n,
                    [](int32_t ms_val) {
                        // Filter latch/unlatch signals (|val| < 10ms)
                        if (ms_val > -10 && ms_val < 10) return;
                        if (s_rx_player) s_rx_player->push(ms_val);
                    });
            } else if (s_settings.inet_proto == 1 && s_mopp_codec) {
                // MOPP parse → convert symbols to timing for RxCwPlayer
                unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
                s_mopp_codec->parse(buf, (size_t)n,
                    [dit_ms](bool is_dit) {
                        int32_t dur = is_dit ? (int32_t)dit_ms
                                             : (int32_t)(dit_ms * 3);
                        if (s_rx_player) {
                            s_rx_player->push(dur);       // key-down
                            s_rx_player->push(-(int32_t)dit_ms); // inter-element gap
                        }
                    },
                    [dit_ms]() {
                        // char gap: 3 dit total (already 1 dit from inter-element)
                        if (s_rx_player)
                            s_rx_player->push(-(int32_t)(dit_ms * 2));
                    },
                    [dit_ms]() {
                        // word gap: 7 dit total
                        if (s_rx_player)
                            s_rx_player->push(-(int32_t)(dit_ms * 6));
                    });
            }
        }
    rx_done:;
    }

    // ── RX: playback tick (sidetone + decode) ───────────────────────────
    if (s_rx_player) s_rx_player->tick();

    // ── Keepalive every 10 s ────────────────────────────────────────────
    if (now - s_inet_keepalive_t >= 10000UL) {
        s_inet_keepalive_t = now;
        if (s_settings.inet_proto == 0 && s_cwcom_codec) {
            uint8_t buf[CwComCodec::SHORT_SIZE + CwComCodec::PACKET_SIZE];
            int n = s_cwcom_codec->build_keepalive(buf);
            // Send short CON packet, then ID packet
            s_network->cw_send(buf, CwComCodec::SHORT_SIZE);
            if (n > CwComCodec::SHORT_SIZE)
                s_network->cw_send(buf + CwComCodec::SHORT_SIZE,
                                   CwComCodec::PACKET_SIZE);
        }
        // MOPP keepalive: send empty packet (just header)
        if (s_settings.inet_proto == 1 && s_mopp_codec) {
            uint8_t buf[4];
            int len = s_mopp_codec->build_packet(buf, sizeof(buf));
            if (len > 0)
                s_network->cw_send(buf, (size_t)len);
        }
    }
}

// ── CW engine + LVGL tick dispatch ────────────────────────────────────────
// Call every loop iteration after polling and routing key events.
static void app_ui_tick()
{
    s_audio->poll();

#ifdef BOARD_POCKETWROOM
    // ── WiFi portal: deferred start (scan + AP + server) ────────────────
    if (s_wifi_portal_pending && s_network && !s_portal) {
        s_wifi_portal_pending = false;
        if (s_wifi_status_lbl)
            lv_label_set_text(s_wifi_status_lbl, "Scanning...");
        lv_timer_handler();

        s_portal = new WifiPortal(s_ap_ssid, *s_network,
            [](const char* ssid, const char* pass) {
                strncpy(s_pending_ssid, ssid, 32);
                s_pending_ssid[32] = '\0';
                strncpy(s_pending_pass, pass, 64);
                s_pending_pass[64] = '\0';
                s_wifi_creds_ready = true;
            });
        s_portal->begin();

        if (s_wifi_status_lbl)
            lv_label_set_text(s_wifi_status_lbl, "AP active");
        if (s_active_sb) s_active_sb->set_wifi(false, true);
    }

    // ── WiFi portal pump ────────────────────────────────────────────────
    if (s_portal) s_portal->loop();

    // ── WiFi credential handoff from async TCP task ─────────────────────
    if (s_wifi_creds_ready) {
        s_wifi_creds_ready = false;
        if (s_wifi_status_lbl)
            lv_label_set_text_fmt(s_wifi_status_lbl, "Connecting to %s...",
                                  s_pending_ssid);
        lv_timer_handler();

        if (s_portal) { s_portal->end(); delete s_portal; s_portal = nullptr; }

        bool ok = s_network && s_network->wifi_connect(
                      s_pending_ssid, s_pending_pass, 10000);
        if (ok) {
            save_wifi_creds(s_pending_ssid, s_pending_pass);
            char ip[20];
            s_network->wifi_get_ip(ip, sizeof(ip));
            if (s_wifi_status_lbl)
                lv_label_set_text_fmt(s_wifi_status_lbl, "Connected: %s", ip);
            if (s_active_sb) s_active_sb->set_wifi(true);
            screenshot_server_start();
        } else {
            if (s_wifi_status_lbl)
                lv_label_set_text(s_wifi_status_lbl, "Failed \xe2\x80\x94 press Back");
        }
    }
#endif

    // ── Battery polling (~every 10 s) ────────────────────────────────────
    {
        static unsigned long s_bat_poll_t = 0;
        if ((unsigned long)(app_millis() - s_bat_poll_t) >= 10000UL) {
            s_bat_poll_t = (unsigned long)app_millis();
            uint8_t pct = s_read_battery_percent ? s_read_battery_percent() : 255;
            bool chg = s_is_charging ? s_is_charging() : false;
            if (s_active_sb) s_active_sb->set_battery(pct, chg);
        }
    }

    // ── Deep sleep on inactivity ─────────────────────────────────────────
    if (s_settings.sleep_timeout_min > 0 &&
        s_active_mode != ActiveMode::GENERATOR &&
        (unsigned long)(app_millis() - s_last_activity_t) >=
            (unsigned long)s_settings.sleep_timeout_min * 60000UL) {
        if (s_enter_deep_sleep) s_enter_deep_sleep();
    }

    // FN double-press timeout: if no second press arrived, execute single-press
    if (s_fn_pending &&
        ((unsigned long)app_millis() - s_fn_last_press_t) >= FN_DOUBLE_MS) {
        s_fn_pending = false;
        if (s_encoder_mode != EncoderMode::SCROLL) {
            // Single press: toggle WPM ↔ Volume
            enter_encoder_mode(s_encoder_mode == EncoderMode::WPM
                ? EncoderMode::VOLUME : EncoderMode::WPM);
        }
        // Single press while in scroll mode is ignored
    }

#ifdef BOARD_POCKETWROOM
    // ── Drain decoded symbols from CW task ──────────────────────────────
    {
        DecodedSymbol sym;
        while (xQueueReceive(s_decoded_symbol_queue, &sym, 0) == pdTRUE) {
            on_letter_decoded(std::string(sym.text));
        }
    }
    // ── Drain echo-tame flag from CW task ───────────────────────────────
    if (s_cw_tame_echo) {
        s_cw_tame_echo = false;
        if (s_active_mode == ActiveMode::ECHO) s_trainer->tame_echo_timeout();
    }
    // ── Read element-end timestamp for word-space detection ──────────────
    s_keyer_last_element_end_t = s_cw_last_element_end_t;
#else
    // ── Simulator: tick CW engine directly (single-threaded) ────────────
    if (s_active_mode == ActiveMode::KEYER ||
        s_active_mode == ActiveMode::ECHO ||
        s_active_mode == ActiveMode::CHATBOT ||
        s_active_mode == ActiveMode::INTERNET_CW ||
        s_active_mode == ActiveMode::INVADERS) {
        s_paddle->tick();
        s_keyer->tick();
        if (s_straight_keyer) s_straight_keyer->decode();
        s_decoder->tick();
    }
#endif
    // Word-space: 7 dit after the last element ended (standard CW word space).
    // Measuring from element end (not character decode) ensures the timer can't
    // fire during the playback of a longer next character (e.g. a dah).
    if (s_keyer_word_pending) {
        unsigned long dit_ms = 1200u / (unsigned long)s_settings.wpm;
        if ((unsigned long)app_millis() - s_keyer_last_element_end_t > 7 * dit_ms) {
            if (s_active_mode == ActiveMode::KEYER && s_keyer_tf)
                s_keyer_tf->add_string(" ");
            if (s_active_mode == ActiveMode::CHATBOT && s_chatbot) {
                s_chatbot->symbol_received(" ");
                if (s_chatbot_oper_tf) s_chatbot_oper_tf->add_string(" ");
            }
            if (s_active_mode == ActiveMode::INTERNET_CW && s_inet_tx_tf)
                s_inet_tx_tf->add_string(" ");
            s_keyer_word_pending = false;
        }
    }
    if (s_active_mode == ActiveMode::GENERATOR ||
        s_active_mode == ActiveMode::ECHO) {
        s_trainer->tick();
    }
    // Chatbot: tick trainer (for CW playback) + chatbot state machine
    if (s_active_mode == ActiveMode::CHATBOT) {
        s_trainer->tick();
        if (s_chatbot) {
            // Detect transmission complete: phrase_cb returned empty
            // (sets s_chatbot_tx_active = false).  Must fire BEFORE chatbot
            // tick so the chatbot sees transmitting_ = false immediately.
            if (!s_chatbot_tx_active && s_chatbot->is_transmitting()) {
                s_chatbot->transmission_complete();
            }
            s_chatbot->tick();
            // Update state label
            if (s_chatbot_state_lbl) {
                static const char* STATE_NAMES[] = {
                    "IDLE", "BOT CQ", "WAIT CQ", "ANS CQ",
                    "EXCHANGE", "WAIT EXCH", "TOPICS", "CLOSING"
                };
                int si = (int)s_chatbot->state();
                char buf[48];
                snprintf(buf, sizeof(buf), "[%s] %s",
                         STATE_NAMES[si], s_chatbot->bot_callsign().c_str());
                lv_label_set_text(s_chatbot_state_lbl, buf);
            }
        }
    }
    // Internet CW: network pump (TX drain + RX poll + keepalive)
    if (s_active_mode == ActiveMode::INTERNET_CW)
        inet_cw_tick();

    // CW Invaders: game tick
    if (s_active_mode == ActiveMode::INVADERS && s_invaders_game) {
        s_invaders_game->tick();
        update_invaders_hud();
        if (s_invaders_game->game_over() && s_inv_gameover_lbl) {
            char go[32];
            snprintf(go, sizeof(go), "GAME OVER\n%d", s_invaders_game->score());
            lv_label_set_text(s_inv_gameover_lbl, go);
            lv_obj_remove_flag(s_inv_gameover_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_timer_handler();

#ifdef BOARD_POCKETWROOM
    screenshot_server_update();
#endif
}
