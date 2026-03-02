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

## Still to do

### HAL implementations
- `libs/hal/m5core2/` — Core2 implementations (M5Unified, AXP192)

### Application layer
- `libs/preferences/` — `prefs::Manager` with debounced NVS writes + snapshot support
- `libs/transceiver/` — ESPNow, UDP, iCW/TCP transport tasks
- `libs/ext-iface/` — USB serial command protocol; BT HID keyboard (NimBLE)

### Content extensions (`libs/content/`)
- Callsign database stream-reader (line-index table for random access from LittleFS)

### WiFi / web server
- ESPAsyncWebServer routes (`/api/config`, `/api/files`, `/api/ota`, `/api/status`)
- Single-page web UI (served from LittleFS `/www/index.html`)

### Audio
- Headphone HF noise: analog-domain issue; potential hardware fix with better
  power filtering on next PCB revision
