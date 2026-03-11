# Morserino-32 NG — Web API Reference

The Morserino-32 NG exposes a JSON HTTP API on port 80 when WiFi is connected.
All endpoints are served by an ESPAsyncWebServer running on the device.

**Base URL:** `http://<device-ip>/` or `http://morserino.local/` (mDNS advertised as `_http._tcp`)

**Transport:** Plain HTTP, no TLS. All requests are unauthenticated.

**Threading caveat:** The HTTP server runs on a separate FreeRTOS task from the
main LVGL/audio loop. All state-mutating operations (mode switches, settings
changes, pause/resume) are deferred to the main loop via volatile flags. This
means a `POST /api/config` returns immediately with `{"ok":true}` but the
settings are applied on the next main-loop tick (~10-50 ms later). Clients
should poll `/api/status` or `/api/config` to confirm changes took effect.

---

## Table of Contents

1. [Device Status](#1-device-status)
2. [Mode Control](#2-mode-control)
3. [Pause / Resume](#3-pause--resume)
4. [CW Text Stream](#4-cw-text-stream)
5. [Send Text as Morse](#5-send-text-as-morse)
6. [Settings — Read](#6-settings--read)
7. [Settings — Write](#7-settings--write)
8. [Settings — Field Metadata](#8-settings--field-metadata)
9. [Settings Slots](#9-settings-slots)
10. [Firmware Version](#10-firmware-version)
11. [Battery Info](#11-battery-info)
12. [Screenshot](#12-screenshot)
13. [Plain HTML Page](#13-plain-html-page)
14. [Settings Key Reference](#14-settings-key-reference)
15. [Mode Names Reference](#15-mode-names-reference)
16. [Polling Recommendations](#16-polling-recommendations)
17. [Example Client Session](#17-example-client-session)

---

## 1. Device Status

Get the current operating mode, pause state, WPM, and decoder info.

```
GET /api/status
```

**Response** `200 application/json`

```json
{
  "mode": "generator",
  "paused": false,
  "wpm": 20,
  "decoder_signal": 0,
  "decoder_wpm": 0
}
```

| Field | Type | Description |
|-------|------|-------------|
| `mode` | string | Current active mode. See [Mode Names Reference](#14-mode-names-reference). |
| `paused` | bool | `true` if generator/echo/chatbot is currently paused. Always `false` for other modes. |
| `wpm` | int | Current character speed in words-per-minute (from settings, not decoded). |
| `decoder_signal` | int | 0–100. Audio input signal level in decoder mode. 0 when not in decoder mode. |
| `decoder_wpm` | int | Estimated receive WPM in decoder mode. 0 when not decoding or not in decoder mode. |

---

## 2. Mode Control

Switch the device to a different operating mode. The switch is deferred to the
main loop for thread safety.

```
GET /api/mode?m=<mode>
```

**Query parameters:**

| Param | Required | Description |
|-------|----------|-------------|
| `m` | Yes | Target mode string. One of: `home`, `keyer`, `generator`, `echo`, `chatbot`, `decoder` |

**Response** `200 application/json`

```json
{"ok": true}
```

`ok` is `true` if the mode name was recognized and the switch was queued,
`false` if the mode name is unknown.

**Notes:**
- `home` returns to the main menu (exits current mode).
- The mode switch happens asynchronously. Poll `/api/status` to confirm the
  mode changed (typically within 50 ms).
- Switching mode while audio is playing stops the current activity cleanly.
- `internet_cw` and `invaders` modes are **not** switchable via API (they
  require special setup flows on the device). Attempting to set them returns
  `{"ok":false}`.

**Mode string → internal index mapping:**

| Mode string | Internal index | Notes |
|-------------|---------------|-------|
| `home` | -1 | Returns to main menu |
| `keyer` | 0 | CW paddle keyer |
| `generator` | 1 | CW text generator (plays CW) |
| `echo` | 2 | Echo trainer (plays, waits for reply) |
| `chatbot` | 3 | AI QSO chatbot |
| `decoder` | 9 | CW audio decoder |

---

## 3. Pause / Resume

Toggle pause state in Generator, Echo, or Chatbot mode.

```
GET /api/pause
```

**Response** `200 application/json`

```json
{"ok": true}
```

`ok` is `true` if the current mode supports pause (Generator, Echo, or
Chatbot), `false` otherwise.

**Behavior:**
- If currently playing → pauses (trainer goes idle, audio stops).
- If currently paused → resumes (trainer starts playing again, session counter
  resets if session_size > 0).
- The toggle is deferred to the main loop. Poll `/api/status` to see the new
  `paused` state.
- In Echo mode, pause works even during error/reveal delay sequences — the
  echo state machine is fully cancelled.
- Has no effect in Keyer, Decoder, or when no mode is active.

---

## 4. CW Text Stream

Get accumulated CW text (decoded characters in Keyer/Decoder, generated
phrases in Generator, echo results in Echo mode).

### 4.1 Consuming read (clears buffer)

```
GET /api/text
```

**Response** `200 application/json`

```json
{"text": "cq cq de w1ab "}
```

| Field | Type | Description |
|-------|------|-------------|
| `text` | string | Accumulated text since last call. Empty string `""` if nothing new. |

**Behavior:**
- Each call returns all text accumulated since the previous call, then clears
  the internal buffer.
- Text is **lowercase** for generated/decoded characters.
- **Word spaces** appear as literal space characters between words.
- **Newlines** (`\n`) appear in Echo mode to separate rounds.
- If the buffer was empty, returns `{"text":""}` (never null/missing).

**Echo mode text format:**
```
<phrase> OK\n          — operator keyed the phrase correctly
ERR\n                  — operator keyed it wrong (will replay)
<phrase> MISS\n        — max repeats exhausted, phrase revealed
```

Example echo session text:
```
paris OK
morse ERR
morse OK
hello MISS
```

### 4.2 Non-consuming peek

This endpoint is used internally by the `/plain` page. It returns the same
text without clearing the buffer.

```
Not exposed as a separate HTTP endpoint — only used server-side by /plain.
```

If you need peek behavior from an external client, call `/api/text` and
maintain your own buffer.

### 4.3 Clear text buffer

There is no dedicated clear endpoint in the JSON API. To clear, call
`GET /api/text` (which consumes and clears), or use the `/plain?action=clear`
endpoint (see [Plain HTML Page](#12-plain-html-page)).

---

## 5. Send Text as Morse

Queue arbitrary text to be played as Morse code through the device speaker
(and keyer output). Works in **Keyer** and **Generator** modes.

```
POST /api/send
Content-Type: application/json
```

**Request body:**

```json
{"text": "cq cq de w1ab k"}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `text` | string | Yes | Plain text to play as Morse. Split into words internally and played one word at a time with standard inter-word spacing. |

**Response** `200 application/json`

```json
{"ok": true}
```

`ok` is `true` if the current mode supports sending (Keyer or Generator),
`false` otherwise (wrong mode, e.g. Echo or Decoder).

**Behavior:**
- Text is split on whitespace and fed to the MorseTrainer word by word.
- Each word is played at the current WPM/Farnsworth settings.
- The played text appears on the device display and in the `/api/text` stream.
- In **Generator mode**, normal phrase generation pauses while the queued text
  plays. After the queue empties, the trainer goes idle (paused).
- In **Keyer mode**, the trainer plays the text and returns to idle when done.
  Manual keying is unaffected before and after.
- Sending new text while a previous send is still playing appends to the queue
  (the previous text continues playing, new text follows).
- The request is deferred to the main loop for thread safety.

**Example — curl:**

```bash
curl -X POST http://morserino.local/api/send \
  -H "Content-Type: application/json" \
  -d '{"text":"cq cq cq de w1ab w1ab k"}'
```

---

## 6. Settings — Read

Get all current settings as a flat JSON object.

```
GET /api/config
```

**Response** `200 application/json`

```json
{
  "wpm": 20,
  "farnsworth": 0,
  "freq_hz": 700,
  "volume": 14,
  "adsr_ms": 7,
  "brightness": 255,
  "text_font_size": 0,
  "sleep_timeout_min": 5,
  "quick_start": 0,
  "mode_a": 0,
  "curtisb_dit_pct": 0,
  "curtisb_dah_pct": 0,
  "ext_key_iambic": 1,
  "paddle_swap": 0,
  "ext_key_swap": 0,
  "screen_flip": 0,
  "cont_words": 1,
  "cont_abbrevs": 0,
  "cont_calls": 0,
  "cont_chars": 0,
  "cont_qso": 0,
  "chars_group": 0,
  "koch_lesson": 0,
  "koch_order": 0,
  "word_max_length": 0,
  "qso_max_words": 0,
  "session_size": 0,
  "echo_max_repeats": 3,
  "adaptive_speed": 1,
  "chatbot_qso_depth": 1,
  "inet_proto": 0,
  "cwcom_wire": 1,
  "callsign": ""
}
```

All boolean settings are encoded as integers: `0` = false, `1` = true.
All enum settings are encoded as zero-based integers.
String settings are JSON strings.

See [Settings Key Reference](#13-settings-key-reference) for the complete list.

---

## 7. Settings — Write

Update one or more settings. Only the keys present in the JSON body are
modified; all others remain unchanged.

```
POST /api/config
Content-Type: application/json
```

**Request body:** A JSON object with setting keys and their new values.

```json
{
  "wpm": 25,
  "farnsworth": 18,
  "cont_words": 1,
  "cont_chars": 0
}
```

**Response** `200 application/json`

```json
{"ok": true}
```

`ok` is `true` if at least one setting was changed, `false` if no recognized
keys were present or no values differed.

**Behavior:**
- Settings are saved to NVS (non-volatile storage) immediately.
- Hardware-affecting settings (audio, codec, display, etc.) are applied
  asynchronously on the next main-loop tick via deferred execution.
- Boolean values should be sent as `0` or `1` (integers, not JSON booleans).
- Enum values should be sent as zero-based integers.
- String values (e.g. `callsign`) should be sent as JSON strings.
- Unknown keys are silently ignored.
- To verify the change took effect, read back with `GET /api/config`.

**Partial updates are supported.** You can send a single key:

```json
{"wpm": 30}
```

**Example — curl:**

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"wpm":25,"volume":10}'
```

---

## 8. Settings — Field Metadata

Get the metadata table describing all settings fields, their types, ranges,
and UI grouping. This enables a client to dynamically build a settings editor
without hardcoding the field list.

```
GET /api/meta
```

**Response** `200 application/json`

An array of field descriptor objects:

```json
[
  {
    "key": "wpm",
    "label": "Speed (WPM)",
    "group": "General",
    "type": "int",
    "min": 5,
    "max": 40,
    "step": 1
  },
  {
    "key": "text_font_size",
    "label": "Text Size",
    "group": "General",
    "type": "enum",
    "min": 0,
    "max": 1,
    "options": "Normal|Large"
  },
  {
    "key": "quick_start",
    "label": "Quick Start",
    "group": "General",
    "type": "bool",
    "min": 0,
    "max": 1
  },
  {
    "key": "callsign",
    "label": "Callsign",
    "group": "Network",
    "type": "string",
    "min": 0,
    "max": 15
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `key` | string | JSON key used in `/api/config` GET/POST |
| `label` | string | Human-readable label for UI display |
| `group` | string | Settings group: `"General"`, `"Keyer"`, `"Content"`, `"Training"`, `"Network"` |
| `type` | string | One of: `"int"`, `"bool"`, `"enum"`, `"string"` |
| `min` | int | Minimum value (for `int`); 0 for others; max string length for `string` |
| `max` | int | Maximum value (for `int`/`enum`); 1 for `bool`; max string length for `string` |
| `step` | int | Step size for `int` fields (present only if > 0). Default 1. |
| `options` | string | Pipe-separated option labels for `enum` fields (e.g. `"LCWO\|Morserino\|CW Academy\|LICW"`). Absent for non-enum types. Index 0 = first option. |

**Groups** (in display order):
1. **General** — Speed, audio, display, power
2. **Keyer** — Paddle mode, Curtis B, swap, external key
3. **Content** — Content types, Koch, character set, session
4. **Training** — Echo repeats, adaptive speed, chatbot depth
5. **Network** — Internet CW protocol, wire, callsign

---

## 9. Settings Slots

Named snapshots of the full settings state. Up to 8 slots. Stored in NVS.

### 8.1 List Slots

```
GET /api/slots
```

**Response** `200 application/json`

```json
["Contest 40m", "Ragchew", "Koch L12"]
```

An array of slot name strings (may be empty `[]`).

### 8.2 Save Slot

Save the current settings to a named slot. Overwrites if the name already exists.

```
GET /api/slots/save?name=<slot_name>
```

| Param | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Slot name, max 16 characters |

**Response** `200 application/json`

```json
{"ok": true}
```

`false` if max slots reached and name is new.

### 8.3 Load Slot

Load a named slot, replacing all current settings.

```
GET /api/slots/load?name=<slot_name>
```

| Param | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Slot name to load |

**Response on success** `200 application/json`

Returns the full settings object (same format as `GET /api/config`):

```json
{
  "wpm": 25,
  "farnsworth": 15,
  ...
}
```

**Response on failure** `404 application/json`

```json
{"ok": false}
```

### 8.4 Delete Slot

```
GET /api/slots/delete?name=<slot_name>
```

| Param | Required | Description |
|-------|----------|-------------|
| `name` | Yes | Slot name to delete |

**Response** `200 application/json`

```json
{"ok": true}
```

`false` if the slot was not found.

---

## 10. Firmware Version

```
GET /api/version
```

**Response** `200 application/json`

```json
{"version": "v1.2.3-g1a2b3c4"}
```

The version string comes from the `GIT_VERSION` build macro. If the build
has no git info, returns `"unknown"`.

---

## 11. Battery Info

```
GET /api/battery
```

**Response** `200 application/json`

```json
{
  "percent": 78,
  "raw_mv": 3850,
  "compensated_mv": 3920,
  "comp_factor": 1.0182,
  "charging": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `percent` | int | Battery level 0–100, or -1 if unavailable |
| `raw_mv` | int | Raw ADC battery voltage in millivolts |
| `compensated_mv` | int | Calibrated battery voltage in millivolts |
| `comp_factor` | float | Calibration compensation factor (1.0 = uncalibrated) |
| `charging` | bool | `true` if USB power is connected and charging |

---

## 12. Screenshot

Capture the current display framebuffer as a BMP image.

```
GET /screenshot.bmp
```

**Response** `200 image/bmp`

A 320×170 pixel, 16-bit RGB565 BMP file (BI_BITFIELDS compression).
File size is exactly 108,866 bytes (66-byte header + 108,800 pixel bytes).

**Response on error** `503 text/plain`

```
Screenshot buffer not allocated
```

**Notes:**
- The framebuffer is continuously updated via a flush-callback hook on the
  LVGL display driver. The screenshot is always current.
- The BMP is bottom-up (standard BMP row order).
- Cache-Control is set to `no-cache`.

---

## 13. Plain HTML Page

A server-rendered HTML page that works without JavaScript. Designed for
text-based browsers (lynx, w3m), screen readers, and `curl`.

```
GET /plain
GET /plain?action=<action>
```

**Without `action` parameter:** Returns an HTML page showing:
- Current mode, WPM, pause state
- Navigation links for each mode
- Pause/Resume link (in generator/echo/chatbot)
- Accumulated CW text (non-consuming — text buffer is not cleared)
- Clear text / Refresh links

**With `action` parameter:** Performs the action, then returns the updated page.

| Action value | Effect |
|-------------|--------|
| `pause` | Toggle pause/resume |
| `clear` | Clear the text buffer |
| `home` | Switch to home (main menu) |
| `keyer` | Switch to keyer mode |
| `generator` | Switch to generator mode |
| `echo` | Switch to echo mode |
| `decoder` | Switch to decoder mode |
| `chatbot` | Switch to chatbot mode |

**Example — curl:**

```bash
# View current state
curl http://192.168.4.1/plain

# Switch to generator and view result
curl http://192.168.4.1/plain?action=generator

# Pause
curl http://192.168.4.1/plain?action=pause
```

**Example — lynx:**

```bash
lynx http://192.168.4.1/plain
```

All mode switches and pause/resume are rendered as regular HTML links
(`<a href="...">`) — no JavaScript or forms required.

---

## 14. Settings Key Reference

Complete list of all settings keys, their types, ranges, defaults, and semantics.

### General

| Key | Type | Min | Max | Step | Default | Description |
|-----|------|-----|-----|------|---------|-------------|
| `wpm` | int | 5 | 40 | 1 | 20 | Character speed in words per minute |
| `farnsworth` | int | 0 | 40 | 1 | 0 | Effective WPM for inter-character/word spacing. 0 = disabled (spacing at character speed). Must be ≤ `wpm`. |
| `freq_hz` | int | 400 | 900 | 10 | 700 | Sidetone pitch in Hz |
| `volume` | int | 0 | 20 | 1 | 14 | Audio volume (roughly 2 dB per step) |
| `adsr_ms` | int | 1 | 15 | 1 | 7 | Tone envelope attack/release time in ms. Higher = softer click. |
| `brightness` | int | 1 | 255 | 5 | 255 | Display backlight brightness (PWM level) |
| `text_font_size` | enum | 0 | 1 | — | 0 | CW text display font size. Options: `0`="Normal" (20px), `1`="Large" (28px) |
| `sleep_timeout_min` | int | 0 | 60 | 1 | 5 | Auto-sleep after N minutes of inactivity. 0 = never sleep. |
| `quick_start` | bool | 0 | 1 | — | 0 | If 1, auto-enter the last used mode on power-on instead of showing menu. |

### Keyer

| Key | Type | Min | Max | Step | Default | Description |
|-----|------|-----|-----|------|---------|-------------|
| `mode_a` | enum | 0 | 1 | — | 0 | Iambic mode. `0` = Iambic B (squeeze completes both elements), `1` = Iambic A (element stops on release). |
| `curtisb_dit_pct` | int | 0 | 100 | 5 | 0 | Curtis B dit-side threshold percentage. 0 = always accept squeeze. |
| `curtisb_dah_pct` | int | 0 | 100 | 5 | 0 | Curtis B dah-side threshold percentage. 0 = always accept squeeze. |
| `ext_key_iambic` | enum | 0 | 1 | — | 1 | External key jack mode. `0` = Straight key, `1` = Iambic paddles. |
| `paddle_swap` | bool | 0 | 1 | — | 0 | Swap built-in touch paddle polarity (dit ↔ dah). |
| `ext_key_swap` | bool | 0 | 1 | — | 0 | Swap external paddle jack polarity (dit ↔ dah). |
| `screen_flip` | bool | 0 | 1 | — | 0 | Rotate display 180° (for left-handed use). |

### Content

Controls what the Generator, Echo Trainer, and Chatbot produce.

| Key | Type | Min | Max | Step | Default | Description |
|-----|------|-----|-----|------|---------|-------------|
| `cont_words` | bool | 0 | 1 | — | 1 | Enable English words (Oxford 5000, frequency-weighted). |
| `cont_abbrevs` | bool | 0 | 1 | — | 0 | Enable ham radio abbreviations and Q-codes. |
| `cont_calls` | bool | 0 | 1 | — | 0 | Enable randomly generated callsigns (e.g. W1AB, DL3XY). |
| `cont_chars` | bool | 0 | 1 | — | 0 | Enable random character groups. |
| `cont_qso` | bool | 0 | 1 | — | 0 | Enable template-based QSO phrases. |
| `chars_group` | enum | 0 | 2 | — | 0 | Character set for random groups. `0`="Alpha" (a-z), `1`="Alpha+Num" (a-z, 0-9), `2`="All CW" (letters, digits, punctuation, prosigns). |
| `koch_lesson` | int | 0 | 44 | 1 | 0 | Koch progressive lesson number. 0 = disabled. 1–44 = use first N chars of the Koch order. **Note:** The web meta reports max=50 for headroom, but all Koch orders have 41-44 characters. Values beyond the order length use the full order. |
| `koch_order` | enum | 0 | 3 | — | 0 | Koch character introduction order. `0`="LCWO", `1`="Morserino", `2`="CW Academy", `3`="LICW". |
| `word_max_length` | int | 0 | 15 | 1 | 0 | Maximum characters per word/abbreviation/callsign. 0 = no limit. |
| `qso_max_words` | int | 0 | 9 | 1 | 0 | Maximum words per QSO phrase. 0 = no limit. |
| `session_size` | int | 0 | 99 | 1 | 0 | Auto-pause after N phrases. 0 = unlimited. When limit is reached, the trainer pauses. Encoder short press (or `/api/pause`) resumes for another session of the same size. |

**Koch filtering behavior:** When `koch_lesson > 0`, it acts as a *filter* on
the content types, not a replacement. Words/abbreviations/callsigns/QSO
phrases are retried (up to 50 attempts) until one is found that uses only
Koch-allowed characters. For Characters content, the Koch set is intersected
with `chars_group`. If no matching content is found after 50 retries, a random
character group from the Koch set is generated as fallback.

### Training

| Key | Type | Min | Max | Step | Default | Description |
|-----|------|-----|-----|------|---------|-------------|
| `echo_max_repeats` | int | 0 | 9 | 1 | 3 | Max times a phrase replays on error before revealing and advancing. 0 = unlimited repeats. |
| `adaptive_speed` | bool | 0 | 1 | — | 1 | If 1, WPM auto-adjusts: +1 on correct echo, -1 on error (minimum 5 WPM). |
| `chatbot_qso_depth` | enum | 0 | 2 | — | 1 | QSO chatbot conversation depth. `0`="Minimal", `1`="Standard", `2`="Ragchew". |

### Network

| Key | Type | Min | Max | Step | Default | Description |
|-----|------|-----|-----|------|---------|-------------|
| `inet_proto` | enum | 0 | 1 | — | 0 | Internet CW protocol. `0`="CWCom", `1`="MOPP". |
| `cwcom_wire` | int | 1 | 999 | 1 | 1 | CWCom wire/channel number. |
| `callsign` | string | — | 15 chars | — | `""` | Operator callsign. Used in CWCom and chatbot modes. |

---

## 15. Mode Names Reference

Mode strings used in `/api/status` responses and `/api/mode` requests:

| Mode string | Description | Pause | Send text | Produces text | API-switchable |
|-------------|-------------|:-----:|:---------:|:-------------:|:--------------:|
| `none` | No mode active (main menu) | No | No | No | — |
| `keyer` | CW paddle keyer | No | Yes | Yes (keyed chars) | Yes |
| `generator` | CW generator (plays text as Morse) | Yes | Yes | Yes (generated phrases) | Yes |
| `echo` | Echo trainer (play-listen-compare) | Yes | No | Yes (results) | Yes |
| `chatbot` | AI QSO chatbot | Yes | No | Yes (QSO text) | Yes |
| `decoder` | CW audio decoder | No | No | Yes (decoded chars) | Yes |
| `internet_cw` | Internet CW (CWCom/MOPP) | No | No | Yes | No |
| `invaders` | CW Invaders game | No | No | No | No |
| `home` | (request only) Return to main menu | — | — | — | Yes |

---

## 16. Polling Recommendations

The device does not support WebSockets or server-sent events. Clients must
poll. Recommended intervals:

| Endpoint | Interval | Purpose |
|----------|----------|---------|
| `/api/text` | 300–500 ms | CW text stream. Shorter = more responsive, but increases load. |
| `/api/status` | 1000 ms | Mode, pause state, WPM, decoder info. |
| `/api/config` | 5000 ms | Sync settings (device-side changes from encoder/buttons). |
| `/api/battery` | 10000 ms | Battery level (slow-changing). |

**Important:** `/api/text` is *consuming* — each call clears the buffer. If
multiple clients poll simultaneously, they will steal each other's text. For
multi-client scenarios, have one client poll and redistribute, or accept
fragmented text.

---

## 17. Example Client Session

### Startup — discover device capabilities

```bash
# 1. Get firmware version
curl http://192.168.4.1/api/version
# → {"version":"v1.2.3-g1a2b3c4"}

# 2. Get field metadata (for building a dynamic settings UI)
curl http://192.168.4.1/api/meta
# → [{...}, {...}, ...]

# 3. Get current settings
curl http://192.168.4.1/api/config
# → {"wpm":20, "farnsworth":0, ...}

# 4. Get current status
curl http://192.168.4.1/api/status
# → {"mode":"none","paused":false,"wpm":20,"decoder_signal":0,"decoder_wpm":0}
```

### Start a generator session

```bash
# Set content to words + abbreviations, 22 WPM, session of 10 phrases
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"wpm":22,"cont_words":1,"cont_abbrevs":1,"cont_chars":0,"session_size":10}'

# Switch to generator mode
curl http://192.168.4.1/api/mode?m=generator

# Poll for text (repeat every 500ms)
curl http://192.168.4.1/api/text
# → {"text":"the signal "}
curl http://192.168.4.1/api/text
# → {"text":"copy qsl "}
curl http://192.168.4.1/api/text
# → {"text":""}   ← nothing new yet

# Check status — after 10 phrases, auto-paused
curl http://192.168.4.1/api/status
# → {"mode":"generator","paused":true,"wpm":22,...}

# Resume for another 10
curl http://192.168.4.1/api/pause
```

### Echo trainer session

```bash
# Switch to echo mode
curl http://192.168.4.1/api/mode?m=echo

# Poll text for results
curl http://192.168.4.1/api/text
# → {"text":"paris OK\n"}
curl http://192.168.4.1/api/text
# → {"text":"morse ERR\n"}
curl http://192.168.4.1/api/text
# → {"text":"morse OK\n"}
curl http://192.168.4.1/api/text
# → {"text":"hello MISS\n"}
```

### Settings slots

```bash
# Save current settings
curl "http://192.168.4.1/api/slots/save?name=Contest%2040m"

# List all slots
curl http://192.168.4.1/api/slots
# → ["Contest 40m"]

# Load a slot (returns full config)
curl "http://192.168.4.1/api/slots/load?name=Contest%2040m"
# → {"wpm":28, "farnsworth":22, ...}

# Delete a slot
curl "http://192.168.4.1/api/slots/delete?name=Contest%2040m"
```

### Screenshot

```bash
# Download display screenshot
curl -o screen.bmp http://192.168.4.1/screenshot.bmp
# → 108866-byte BMP file, 320x170, RGB565
```

### Full settings backup/restore

```bash
# Backup
curl http://192.168.4.1/api/config > morserino-settings.json

# Restore (all keys at once)
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d @morserino-settings.json
```

---

## Error Handling

| HTTP Status | Meaning |
|-------------|---------|
| 200 | Success (check `ok` field for operation result) |
| 400 | Bad request (missing required parameter, empty body) |
| 404 | Slot not found (on `/api/slots/load`) |
| 500 | Internal error (JSON serialization failure) |
| 503 | Screenshot buffer not allocated (PSRAM failure) |

All error responses use `text/plain` content type with a human-readable
message, except for 404 on slot load which returns `{"ok":false}`.

---

## Content-Type Summary

| Endpoint | Request | Response |
|----------|---------|----------|
| `GET /api/*` | — | `application/json` |
| `POST /api/config` | `application/json` | `application/json` |
| `GET /screenshot.bmp` | — | `image/bmp` |
| `GET /plain` | — | `text/html` |
| `GET /` | — | `text/html` (SPA) |
