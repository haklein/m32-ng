# Morserino-32-NG — Development Progress Log

---

## Session 1 — Initial setup, FSD review, library adoption

### Specification work (`/home/hklein/src/m32-ng-fsd/`)

- Reviewed `Morserino32-ng-FSD.md` and identified gaps (WiFi mode, sleep, UI
  navigation model, memory keyer capacity, file upload mechanism, QSO phrase detail).
- Created `Morserino32-ng-Implementation-FSD.md` covering:
  - PlatformIO monorepo structure with `env:pocketwroom`, `env:m5core2`, `env:native`
  - HAL interface contracts (7 pure-virtual interfaces)
  - M5Stack Core2 as primary touchscreen dev/test target
  - Library adoption plan from `m5core2-cwtrainer`
  - Audio subsystem init sequence (codec I2C before I2S — BCLK feeds TLV320 PLL)
  - QSO phrase engine design (template substitution + full QSO simulation mode)
  - WiFi/web UI routes, sleep/wake sequence, preferences write discipline
  - All open hardware questions resolved (display, audio path, USB CDC, buttons)

### Libraries researched

- `haklein/DisplayWrapper` — LGFX config for Pocket ST7789 (170×320); pin map confirmed
- `haklein/cw-i2s-sidetone` — I2S sine + ADSR; used on both Pocket and Core2
- `haklein/tlv320aic31xx` — TLV320AIC3100 I2C codec driver; Pocket only
- `oe1wkl/Morserino-32` MorseOutput.cpp — codec+sidetone init sequence (reference only)
- `oe1wkl/Morserino-32` m32_v6.ino — hardware pins, sleep/power management (reference only)
- `haklein/m5core2-cwtrainer` — full inventory; determined what to adopt vs. rewrite

### Project bootstrap (`/home/hklein/src/m32-ng/`)

Base project already had `platformio.ini` with pocketwroom pin defines, LGFX, LVGL,
and a working `src/main.cpp` stub (LVGL tabview + encoder input device).

---

## Session 2 — Library implementation

### `libs/cw-engine/`

| File | Source | Changes |
|------|--------|---------|
| `include/common.h` | cwtrainer `IambicKeyer/common.h` | Removed `<stdio.h>`; updated comment |
| `include/symbol_player.h` | cwtrainer `SymbolPlayer.h` | Renamed; no functional change |
| `src/symbol_player.cpp` | cwtrainer `SymbolPlayer.cpp` | Include path fixed |
| `include/iambic_keyer.h` | cwtrainer `IambicKeyer.h` | Renamed; no functional change |
| `src/iambic_keyer.cpp` | cwtrainer `IambicKeyer.cpp` | Include path fixed |
| `include/paddle_ctl.h` | cwtrainer `PaddleCtl.h` | Renamed; no functional change |
| `src/paddle_ctl.cpp` | cwtrainer `PaddleCtl.cpp` | Include path fixed |
| `include/straight_keyer.h` | cwtrainer `StraightKeyer.h` | Injected `read_key_fun_ptr` + `millis_fun_ptr`; removed Arduino.h |
| `src/straight_keyer.cpp` | cwtrainer `StraightKeyer.cpp` | Replaced `digitalRead(PADDLE_LEFT)` with `read_key_cb_()`, `millis()` with `millis_cb_()`; `constrain()` → `std::min/max`; algorithm unchanged |
| `include/char_morse_table.h` | cwtrainer MorsePlayer + MorseTrainer | **Extracted** duplicated `char2morse()` into shared header |
| `src/char_morse_table.cpp` | cwtrainer MorsePlayer + MorseTrainer | Consolidated; `toupper()` cast-safe; full punctuation coverage |

### `libs/cw-decoder/`

| File | Source | Changes |
|------|--------|---------|
| `include/goertzel_detector.h` | cwtrainer `Goertzel/goertzel.h` | Class replacing namespace; sample buffer API |
| `src/goertzel_detector.cpp` | cwtrainer `Goertzel/goertzel.cpp` | Removed `analogRead()`; accepts `const int16_t* samples`; `goertzel_n` scaled to actual sample rate (works at 8 kHz I2S, not just 106 kHz ESP32 ADC); adaptive threshold preserved |
| `include/morse_decoder.h` | cwtrainer `MorseDecoder/MorseDecoder.hpp` | `Arduino::String` → `std::string`; callback → `std::function`; removed `Arduino.h` |
| `src/morse_decoder.cpp` | cwtrainer `MorseDecoder/MorseDecoder.cpp` | Same; CW 69-node binary tree preserved exactly (alphabet, digits, punctuation, prosigns, German umlauts) |

### `libs/content/`

| File | Source | Changes |
|------|--------|---------|
| `data/english_words.h` | cwtrainer `TextGenerators/english_words.h` | Verbatim copy — pure data (373 words) |
| `data/abbrevs.h` | cwtrainer `TextGenerators/abbrevs.h` | Verbatim copy — pure data (245 abbreviations/Q-codes) |
| `include/text_generators.h` | cwtrainer `TextGenerators/TextGenerators.h` | `String` → `std::string`; `std::mt19937&` injected; removed `Arduino.h` |
| `src/text_generators.cpp` | cwtrainer `TextGenerators/TextGenerators.cpp` | `random()` → `std::uniform_int_distribution`; `_min` → `std::min`; algorithm unchanged |

### `libs/training/`

| File | Source | Changes |
|------|--------|---------|
| `include/morse_trainer.h` | cwtrainer `MorseTrainer/MorseTrainer.h` | `String` → `std::string`; callbacks → `std::function`; `millis_fun_ptr` injected; `boolean` → `bool` |
| `src/morse_trainer.cpp` | cwtrainer `MorseTrainer/MorseTrainer.cpp` | All `millis()` → `millis_cb_()`; `Serial.print` removed; `char2morse` pulled from `cw-engine`; WPM floor of 5 added; state machine logic preserved exactly |

### `libs/hal/interfaces/`

Seven pure-virtual interface headers written from scratch, no platform deps:

- `i_key_input.h` — `KeyEvent` enum + `IKeyInput` (poll / wait)
- `i_audio_output.h` — `IAudioOutput` (begin / tone_on / tone_off / set_volume / set_adsr / suspend)
- `i_audio_input.h` — `IAudioInput` (begin / signal_level / set_detect_callback)
- `i_display.h` — `IDisplay` (begin / brightness / sleep / wake / flush / get_touch)
- `i_storage.h` — `IStorage` (KV: get/set int/string + commit; FS: open/read/write/close/list)
- `i_network.h` — `INetwork` (WiFi connect/AP + CW transport over ESPNow/UDP/TCP)
- `i_system_control.h` — `ISystemControl` (sleep / restart / uptime / battery / power_rail / entropy_seed)
- `hal.h` — umbrella include

### `platformio.ini`

Restructured with shared `[env:_embedded_base]` section:

| Environment | Target | Notes |
|-------------|--------|-------|
| `pocketwroom` | ESP32-S3 Morserino Pocket | Full pin set; TLV320 codec; encoder; USB CDC |
| `m5core2` | M5Stack Core2 | M5Unified; touch-only nav; no audio input |
| `native` | Host PC | For unit tests; no hardware deps |

`lib_extra_dirs = libs` wires all local libraries. ArduinoJson → v7, LVGL → v9,
cw-i2s-sidetone → v1.0.3.

---

## Session 3 — Native HAL (ALSA audio + MIDI key input) + build fixes

### Include / build fixes (carry-over from session 2)

- `libs/cw-decoder/include/morse_decoder.h` — fixed wrong relative path (`../../../` → `../../`); added `#include <cstdint>`
- `libs/cw-engine/include/straight_keyer.h` — added `#include <cstdint>` for `uint8_t`
- `libs/cw-engine/src/straight_keyer.cpp` — added `#include <cmath>` for `sqrt`
- `libs/training/include/morse_trainer.h` — added `#include <cstdint>` for `uint32_t`/`uint16_t`

All three environments clean: `pocketwroom` ✅  `m5core2` ✅  `native` ✅ (smoke tests ALL PASS)

### `libs/hal/native/` — native HAL implementations

| File | Description |
|------|-------------|
| `library.json` | Restricts library to `"platforms": ["native"]`; ALSA headers never reach ESP32 builds |
| `audio_output_alsa.h/cpp` | `IAudioOutput` via ALSA PCM; background thread, float32 48 kHz mono |
| `key_input_midi.h/cpp` | `IKeyInput` via ALSA MIDI sequencer; background thread, event queue |

**Audio output design:**
- Complex-rotor oscillator (`phase *= dphase` per sample, same as iambic-keyer `OSCILLATOR_Z`)
- Blackman-Harris windowed attack/release ramps (same BH coefficients as `keyed_tone.h`)
- State machine: Off → Rise → On → Fall → Off
- 256-frame periods written via `snd_pcm_writei`; XRUN recovery via `snd_pcm_recover`
- Thread-safe via atomics (`pending_cmd_`, `pending_freq_`, `gain_`, `adsr_dirty_`)

**MIDI key input design:**
- Creates ALSA sequencer client `m32-keyer`, port `keys`; prints `aconnect` hint on startup
- Optional `auto_connect` finds first available MIDI read port
- Background thread polls with 100 ms timeout; processes Note On/Off → `KeyEvent` queue
- Running-status Note Off (velocity=0 Note On) handled correctly
- Default note mapping: 60=DIT, 61=DAH, 62=STRAIGHT (all configurable)
- `::poll()` qualified to avoid collision with `IKeyInput::poll()`

### `platformio.ini` native env
Added `-lasound -lpthread` to `build_flags`.

### `test/native/main.cpp`
Added audible "VVV" test section: `NativeAudioOutputAlsa` plays `. . . —` × 3 at 700 Hz / 18 WPM. Verified working on host audio.

---

## Session (cont.) — CW task split, touch tuning, codec volume, large font

This session continued from a previous context that covered the simulator UI,
multi-screen architecture, CW pipeline, pocketwroom hardware bringup, and many
intermediate sessions. Picking up from the most recent work:

### Dedicated CW FreeRTOS task (`src/app_ui.hpp`, `src/main.cpp`)

Isolated CW timing-critical code from LVGL render spikes:

- **CW task**: `cw_task_body()` on Core 1, priority 10, 4096 stack, ~1 kHz tick
- **Cross-task comms**: two FreeRTOS queues (`s_ui_event_queue`, `s_decoded_symbol_queue`)
  plus volatile flags for atomic state sharing
- **route() split**: `route_cw()` handles paddle/touch/straight events on CW task;
  `route_ui()` handles encoder/button events on UI task; original `route()` kept
  for simulator (single-threaded)
- **Decoder callback**: `on_letter_decoded_cw()` enqueues `DecodedSymbol` structs
  instead of touching LVGL directly; UI task drains queue in `app_ui_tick()`
- **Mode activation**: `s_cw_engine_active` volatile flag set in screen enter/leave callbacks

### Touch sensor release threshold (`libs/hal/esp32s3/key_input.h`)

User reported "A becomes R" at 33+ WPM on touch sensors (not external paddle).
Root cause: `HYST_OFF` (release threshold above idle) too high → slow release.

- `HYST_OFF`: 2000 → **1000** (press threshold `HYST_ON` unchanged at 4000)

### Codec-level volume control (`libs/hal/esp32s3/audio_output.cpp`)

Switched from sine amplitude control (VolumeStream) to TLV320AIC3100 analog
output stage for better SNR and finer granularity:

- **Sine amplitude**: fixed at 1.0 (full scale)
- **DAC digital volume**: fixed at -2 dB (headroom)
- **User volume**: 0–20 levels via `setHeadphoneVolume()` + `setSpeakerVolume()`
- **Formula**: `dB = (level - 20) * 2.0f` → -38 dB (level 1) to 0 dB (level 20)
- **Level 0**: DAC mute
- **AppSettings**: `VERSION` bumped to 10, default volume 14, spinbox range 0–20

### ADSR envelope tuning

Cleaner codec output revealed aggressive attack/release transients ("quack"):

- **ADSR**: 5 ms → **7 ms** attack and release (all platforms)
- Sweet spot between click-free (8 ms) and crisp at 35 WPM (5 ms)

### Headphone noise investigation

Investigated HF background noise in headphones (audible even at volume 0 / DAC muted):

- **OCMV**: raised from 1.35 V (default) to **1.65 V** — reduces HP amp oscillation risk
- **HP performance mode**: set to highest (bits 4-3 = 11) — best THD+N / noise floor
- **POP removal**: enabled (register 0xBE) — optimized power-down sequencing
- **Speaker amp power-down**: fully powered down (not just muted) when headphones detected;
  eliminates class-D switching noise coupling into HP output
- **32-bit I2S attempt**: tried doubling BCLK to reduce PLL ratio; broke audio, reverted
- **Conclusion**: noise is analog-domain (HP amp + power supply), persists even with
  DAC muted. Software mitigations applied; remaining noise is a hardware limitation.

### Large font support (`libs/ui/`, `src/app_ui.hpp`)

When "Large" text size selected in Settings, font size now applies to menu and
status bar (previously only CW text fields changed):

**Font mapping** (Normal → Large):
- CW text: `lv_font_intel_20` → `lv_font_intel_28` (unchanged)
- Menu items: `lv_font_montserrat_14` → `lv_font_montserrat_20` (new)
- Status bar: `lv_font_montserrat_14` → `lv_font_montserrat_20` (new)
- Settings / Content labels: `lv_font_montserrat_14` → `lv_font_montserrat_20` (new)

Menu uses montserrat (not Intel) because it includes LVGL symbol glyphs for icons.

**Files changed:**
- `libs/ui/include/status_bar.hpp` — accepts `const lv_font_t* font` param;
  dynamic `height()` computed from font line height + padding
- `libs/ui/src/status_bar.cpp` — applies font to all labels; battery reserve
  offset computed proportionally (`3 × font_height + 8`)
- `src/app_ui.hpp`:
  - Added `ui_font()` (Intel 14/20) and `menu_font()` (montserrat 14/20) helpers
  - Replaced compile-time `CONTENT_Y` constant with `content_y()` function
  - All StatusBar constructors pass `menu_font()`
  - Menu list, settings container, content container use `menu_font()`
- `include/lv_conf.h` — enabled `LV_FONT_MONTSERRAT_20`
- `platformio.ini` — added `-D LV_FONT_MONTSERRAT_20=1` for simulator env

---

## Session — Keyer fixes, FET output, brightness, content screen, status bar

### Iambic keyer timing fixes (`libs/cw-engine/`)

User-reported "extra dits" and "missed dits" vs original Morserino v7.03.
Investigated original Morserino keyer state machine and identified three issues:

- **Paddle latching**: Rewrote to match original Morserino's `checkPaddles()` pattern.
  Latches updated from live `lever_state` every `tick()` (accumulate, never cleared in
  tick). Cleared only at element start (like original `clearPaddleLatches()`). Effective
  lever state built from accumulated latches when element finishes. Fixes runaway dahs
  after squeeze sequences and ignored held dit paddle.
- **Inter-element gap**: Removed `release_comp_ms` from gap duration in `symbol_player.cpp`
  (was making total cycle `2*dit + release_comp` instead of `2*dit`).
- **Curtis B threshold**: Changed default from 40% to 0% (always accept squeeze, matching
  original Morserino behavior).

### MOSFET keyer output (`src/app_ui.hpp`, `libs/hal/esp32s3/key_input.cpp`)

PIN_KEYER (GPIO 41) was incorrectly configured as INPUT for straight key. Fixed:
- Removed ISR and INPUT_PULLUP on PIN_KEYER from `key_input.cpp`
- Configured as OUTPUT, driven LOW by default
- `digitalWrite(PIN_KEYER, HIGH/LOW)` added alongside `tone_on/tone_off` in:
  - `on_play_state()` (iambic keyer)
  - `on_straight_state()` (straight key via paddle jack)
  - MorseTrainer sidetone lambda (generator/echo/chatbot)
- Safety LOW in all mode leave callbacks
- Straight key input comes from paddle jack (PADDLE_DIT/DAH events when ext_key_iambic=false)

### Display brightness (`src/app_ui.hpp`, `src/main.cpp`)

- 5 brightness steps matching original Morserino: 255 → 127 → 63 → 28 → 9
- Cycled on long FN (aux) press (previously was pop-all, now encoder long press does that)
- Persisted in `AppSettings.brightness` (VERSION bumped to 15)
- Applied at boot via `gfx.setBrightness()` right after `display_init()`
- Safety: `brightness=0` treated as 255 (prevents black screen on migration)

### Status bar improvements (`libs/ui/`)

- Mode text replaced with LVGL symbol icons
- WPM format: `22 WPM` normal, `22(18)W` with Farnsworth, `eff/setW` with effective WPM
- Graphical battery icons with color coding (green→yellow→red), charge indicator
- Dynamic label positioning via `lv_obj_align_to` chaining

### Content screen rework (`src/app_ui.hpp`)

- Reworked from 2-per-row abbreviated labels to one item per row with full labels
- Scrollable container for all content settings
- Added Session limit spinbox (0=unlimited, 1–99 phrases per session)

### Session limit (`src/app_ui.hpp`)

- Phrase counter incremented in content phrase callback
- Auto-pause when `session_size` reached; encoder short press resumes for another session
- Counter reset on mode entry and unpause

---

## Session — Serial bridge, WiFi serial API, wifi_autostart, PSRAM fix

### Serial JSON bridge (`src/serial_bridge.cpp`, `src/serial_bridge.h`)

Exposed the existing JSON web API over USB serial so any tool (Python script,
terminal, screen reader app) can control the M32-NG without WiFi:

- Line-oriented protocol: `METHOD /path [json-body]\n` over 115200 baud USB CDC
- All web API endpoints mirrored: config, status, battery, version, meta, slots,
  text, send, mode, pause
- Coexists with ArduinoLog output (non-command lines ignored by clients)
- Integrated in `main.cpp`: `serial_bridge_init()` in setup, `serial_bridge_poll()` in loop

### WiFi via serial (`POST /api/wifi`, `GET /api/scan`)

- `POST /api/wifi {"ssid":"...","pass":"..."}` — synchronous connect (blocks ~10s),
  returns `{"ok":true/false}` with actual connection result
- `GET /api/scan` — triggers WiFi scan, returns `[{"ssid":"...","rssi":-45},...]`
- `config_set_wifi()` — new synchronous helper: connects, saves creds on success, starts web server

### WiFi autostart setting (AppSettings VERSION 16)

- `wifi_autostart` bool (default `true`) — when disabled, WiFi only starts on
  WiFi menu entry or Internet CW connect
- `ensure_wifi_connected()` — on-demand WiFi helper called from menu/iCW callbacks
- Important for power-conscious users who train offline; defaults on for accessibility
  (visually impaired users who always connect via browser)

### PSRAM graceful failure (`src/web_server.cpp`)

- Web server no longer aborts if PSRAM `heap_caps_malloc` fails for screenshot buffer
- Screenshots return 503 when buffer unavailable; all other endpoints work normally
- Enables LoRa boards (SX1262 uses GPIO 35/36/37 conflicting with octal PSRAM)

### Test scripts (`tools/`)

- `tools/serial_bridge_test.py` — smoke tests all serial bridge endpoints
- `tools/serial_wifi_setup.py` — sets WiFi credentials via serial, blocks for result

---

# Feature Checklist

Tracks features implemented vs. specified in the FSD and Implementation FSD.

## CW Engine & Keyer
- [x] Iambic A/B keyer with configurable speed (5–40 WPM)
- [x] Straight key support
- [x] Morse decoder (character-by-character, word-space detection)
- [x] Decoder word-gap detection (~8 dit-lengths silence → emits word space)
- [x] Sidetone via I2S audio HAL
- [x] Debounced paddle controller (PaddleCtl — 2 ms debounce, 1 kHz poll)
- [x] lever_upgrade latch for mid-element opposite paddle detection
- [x] ADSR envelope shaping (configurable 1–15 ms attack/release)

## Content Library
- [x] Text generation from multiple content sources (words, abbreviations, callsigns, character groups)
- [x] Oxford 5000 word list with frequency-weighted random selection
- [x] Max word length setting (0=unlimited)
- [x] Farnsworth timing support (separate character/word speeds)
- [x] Koch method with 4 orderings (LCWO, Morserino, CW Academy, LICW)
- [x] Realistic ITU-based callsign generation (`libs/content/data/callsign_prefixes.h`)
  - Weighted prefix table (~45 prefixes from 20+ countries, reflecting HF activity)
  - Suffix length biased toward 2–3 letters (real distribution)
  - Optional modifiers: /P, /M, /MM, /QRP (~8% chance)
- [x] QSO phrase templates with `{slot}` substitution (29 templates)
  - Per-phrase caching: repeated `{rst}` or `{name}` slots produce same value
  - Slots: name, qth, rst, rig, pwr, ant, wx, condx, band, call, temp, age, lic

## CW Generator
- [x] Text generation from content library
- [x] Koch method progressive character introduction
- [x] Pause/resume via encoder short press or aux button
- [x] Session limit (0=unlimited, 1–99 phrases; auto-pause, encoder short resumes)

## Echo Trainer
- [x] Hear-and-repeat training loop
- [x] Configurable max repeats (0=unlimited)
- [x] Green/red word feedback (OK/ERR/MISS)
- [x] Multi-word phrase support (QSO phrases with spaces)
- [x] Relaxed phrase comparison (case-insensitive, ignores extra/missing word spaces)
- [x] `<HH>` error prosign removes last word only (not entire phrase)
- [x] EEEE (4 consecutive E's) removes last word
- [x] Sound effects on OK/ERR/MISS (SPIFFS MP3 with tone fallback)
- [x] Pause/resume via encoder short press or aux button
- [x] Adaptive WPM (auto ±1 WPM on success/failure, configurable on/off)
- [x] Session limit (shared with Generator)

## QSO Chatbot
- [x] Standalone CW chatbot library (`libs/cw-chatbot/`)
- [x] Full QSO state machine (CQ → Exchange → Topics → Closing)
- [x] Configurable depth (Minimal/Standard/Ragchew)
- [x] Prosign notation (`<AR>`, `<KN>`, `<SK>`) and `=` separators
- [x] 12 countries with correlated callsigns/names/cities
  (USA, Canada, UK, Germany, France, Italy, Japan, Australia, Spain, Russia, Netherlands, Brazil)
- [x] Callsign suffix bias toward 2–3 letters (realistic distribution)
- [x] Pause/resume via encoder short press or aux button
- [x] Operator callsign configuration (callsign setting in Network, used by chatbot)

## UI — Display & Typography
- [x] Intel One Mono typeface for CW text areas (20 px Medium, 28 px Bold)
- [x] Montserrat (LVGL built-in) for menus, status bar, icons
- [x] Text Size preference: Normal (20 px) / Large (28 px)
- [x] CW text areas auto-scroll when content overflows
- [x] Settings page scrollable flex layout (all items reachable on 170 px screen)

## UI — Navigation
- [x] Screen stack (push/pop)
- [x] Encoder rotation: scroll list / adjust widget; WPM ±1 / Volume / Scroll in CW modes
- [x] Encoder short press: select (menu/settings) / pause (CW modes)
- [x] Encoder long press: back
- [x] Aux short press: pause (CW modes) / FN single-press toggles encoder WPM↔Volume
- [x] Aux double press: toggle encoder Scroll mode (manual textarea scrolling)
- [x] Aux long press: cycle display brightness (5 steps: 255→127→63→28→9)
- [x] Fullscreen menu on pocketwroom (no title bar, maximized screen estate)

## Preferences
- [x] Runtime settings struct (`AppSettings` VERSION 16) with defaults
- [x] Settings screen (WPM, Farnsworth, keyer mode, Curtis B dit/dah %, frequency, volume, ADSR, text size, brightness, sleep timeout, quick start, ext key mode, paddle swap, ext key swap, screen flip, wifi autostart)
- [x] Content screen (word/abbrev/call/char/QSO toggles, char group, Koch order, Koch lesson, max word length, session limit)
- [x] Content screen reworked: one item per row, full labels, scrollable
- [x] NVS persistence via `IStorage` + `PocketStorage` (blob save/load on every change)
- [x] Quick Start (auto-enter last CW mode on boot)
- [x] Default WPM: 20
- [x] Snapshots (up to 8 named slots, save/load/delete via web API and NVS)

## HAL
- [x] `IKeyInput` interface with polling
- [x] `IAudioOutput` interface (tone on/off, volume, poll)
- [x] ESP32-S3 HAL (paddle, encoder, TLV320 codec, ST7789 display)
- [x] Native HAL (SDL2 keyboard mapping, ALSA audio output, ALSA MIDI key input)
- [x] Headphone detection via TLV320AIC3100 INT1 interrupt (auto speaker mute/unmute, timer clock fix)
- [x] `IStorage` NVS implementation (`PocketStorage` via ESP32 Preferences)
- [x] `play_effect()` sound effects via HAL (SPIFFS MP3 on ESP32, ALSA tones on native)
- [x] SPIFFS filesystem for sound effects and future file storage
- [x] cw-i2s-sidetone library v1.0.4

## Key Input
- [x] Touch paddle input (dit/dah via capacitive touch strips)
- [x] External paddle jack: iambic or straight key mode (Ext Key setting)
- [x] External paddles polled at 1 kHz (no ISRs — eliminates bounce-induced queue overflow)
- [x] Touch sensor 3-second watchdog (auto-release stuck touch readings)
- [x] Priority UP events via front-of-queue (`xQueueSendToFront`) — prevents lost releases
- [x] Dit/dah swap setting for touch paddles
- [x] Dit/dah swap setting for external key input (iambic mode)
- [x] MOSFET keyer output (GPIO41) — keys external transmitter in keyer, generator, echo, chatbot modes
- [x] Paddle latching (live-level latch updates matching original Morserino checkPaddles pattern)
- [x] Curtis B squeeze threshold (configurable dit/dah %, 0=always accept)

## Display
- [x] ST7789 170×320 TFT with rotation
- [x] Screen upside-down rotation (live flip via Screen Flip checkbox in Settings)
- [x] Backlight brightness control (5 steps, persisted, long FN press to cycle)
- [x] Status bar: mode icons, compact WPM format (Farnsworth, effective WPM), graphical battery, WiFi indicator (green=connected, orange=AP)
- [x] Splash screen with M32 logo (RGB565, SVG-converted), git version, WiFi status (4 s minimum)

## Audio
- [x] Codec-level volume control via TLV320AIC3100 headphone/speaker gain (0–20 steps)
- [x] ADSR 7 ms attack/release (tuned for codec output)
- [x] HP performance mode + OCMV 1.65 V + POP removal (noise floor mitigation)
- [x] Speaker amp auto-power-down when headphones detected

## Power Management
- [x] Deep sleep with configurable timeout (0=disabled, 1–60 min)
- [x] Sleep timer resets from both UI and CW tasks (keying keeps device awake)
- [x] Battery voltage measurement via ADC
- [x] Wake via aux button (GPIO0 deep sleep wakeup)

## WiFi & Web Interface
- [x] WiFi captive portal (AP mode, auto SSID "Morserino-XXYY")
- [x] WiFi station mode (connect to existing network)
- [x] WiFi setup screen with QR code (dynamic size based on screen)
- [x] Web configuration SPA (embedded PROGMEM, dark theme, tabbed settings)
- [x] REST API: GET/POST `/api/config`, GET `/api/meta`, GET `/api/screenshot`, GET `/api/version`, GET `/api/status`, GET `/api/battery`, GET `/api/text`, POST `/api/send`, GET `/api/mode`, GET `/api/pause`, POST `/api/wifi`, GET `/api/scan`
- [x] Git version/tag displayed in web interface header
- [x] Settings slots via web API: `/api/slots`, `/api/slots/save`, `/api/slots/load`, `/api/slots/delete`
- [x] JSON download/upload of all settings
- [x] Field metadata table drives both JSON API and web UI dynamically
- [x] mDNS (`morserino.local`)
- [x] WiFi autostart setting (default on; when off, WiFi only starts on WiFi menu or Internet CW entry)
- [x] Screenshot buffer graceful PSRAM failure (web server starts even without PSRAM; screenshots return 503)

## Serial Bridge
- [x] USB CDC serial JSON bridge (`src/serial_bridge.cpp`) — same API as web server
- [x] Line-oriented protocol: `METHOD /path [json-body]\n` over 115200 baud
- [x] All web API endpoints available over serial (config, status, battery, version, meta, slots, text, send, mode, pause, wifi, scan)
- [x] Coexists with ArduinoLog output (non-command lines ignored)
- [x] Python test scripts (`tools/serial_bridge_test.py`, `tools/serial_wifi_setup.py`)

## Internet CW
- [x] CWCom (MorseKOB) protocol — UDP, duration-based timing
- [x] MOPP (Morse over Packet Protocol) — compact binary dit/dah encoding
- [x] Internet CW connect/disconnect screen with status display
- [x] Split RX/TX text fields on active CW screen
- [x] Configurable CWCom wire/channel (1–999)
- [x] Keepalive packets

## CW Invaders
- [x] Space Invaders-style Morse training game
- [x] Koch progression (start with 2 characters, unlock per level)
- [x] 3 lives, score tracking, streak bonus
- [x] Difficulty scaling (spawn interval, scroll speed)
- [x] Pause/resume via encoder short press

## CW Decoder
- [x] Decoder mode: decode CW from radio headphone output via TLV320AIC3100 ADC
- [x] Goertzel tone detection with adaptive threshold (narrowband, tuned to sidetone frequency)
- [x] Hardware AGC on codec (auto-adjusts for varying headphone output levels)
- [x] Adaptive WPM estimation (exponential moving average of dit/dash durations)
- [x] Signal amplitude bar + estimated WPM display
- [x] Decoded text in scrolling CWTextField
- [x] Dedicated FreeRTOS task for audio processing (Core 1, priority 5)

## Documentation
- [x] Keyer architecture documentation (`keyer.md`)
- [x] User manual (`manual.md`)
- [x] Web API documentation (`WEB-API.md`)
- [x] Implementation progress tracker (`progress.md`)

## Build & CI
- [x] `pocketwroom` — ESP32-S3 WROOM hardware target
- [x] `simulator` — SDL2 desktop simulator
- [x] `native` — unit test target
- [x] GitHub Actions CI: build on push, deploy firmware to GitHub Pages
- [x] Web flasher (ESP Web Tools) with version selector (releases + dev builds)
- [x] Automatic dev→release promotion when tags are detected on existing commits
- [x] `GIT_VERSION` build-time define via PlatformIO extra_script (`support/git_version.py`)
- [x] `support/svg_to_lvgl.py` — SVG to LVGL RGB565 C header converter
- [x] `pocketwroom_smoke` — hardware smoke test env

## Architecture
- [x] Dedicated CW FreeRTOS task (Core 1, priority 10, ~1 kHz tick) — isolates timing from LVGL render
- [x] Cross-task communication via FreeRTOS queues (`s_ui_event_queue`, `s_decoded_symbol_queue`)
- [x] `route_cw()` / `route_ui()` split for multi-core; single `route()` for simulator

## Not Yet Implemented
- [ ] M5Stack Core2 HAL (builds but untested)
- [ ] `IDisplay` touch input
- [ ] BT HID keyboard output (NimBLE)
- [ ] ESPNow CW transport
- [ ] OTA firmware update via web API
- [ ] Callsign database stream-reader (line-index table for random access from SPIFFS)
- [ ] LoRa board variant (SX1262 — requires PSRAM-disabled build env due to GPIO 35/36/37 pin conflict)
