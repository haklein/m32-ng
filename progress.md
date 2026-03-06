# Morserino-32-NG Implementation Progress

Tracks features implemented vs. specified in the FSD and Implementation FSD.

## CW Engine & Keyer
- [x] Iambic A/B keyer with configurable speed (5–40 WPM)
- [x] Straight key support
- [x] Morse decoder (character-by-character, word-space detection)
- [x] Decoder word-gap detection (~8 dit-lengths silence → emits word space)
- [x] Sidetone via I2S audio HAL
- [x] No-debounce paddle controller (PaddleCtl — polled, no latency)
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
- [x] Runtime settings struct (`AppSettings` VERSION 15) with defaults
- [x] Settings screen (WPM, Farnsworth, keyer mode, Curtis B dit/dah %, frequency, volume, ADSR, text size, brightness, sleep timeout, quick start, ext key mode, paddle swap, ext key swap, screen flip)
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
- [x] Native HAL (SDL2 keyboard mapping, ALSA audio, MIDI input)
- [x] Headphone detection via TLV320AIC3100 INT1 interrupt (auto speaker mute/unmute, timer clock fix)
- [ ] `IDisplay` touch input
- [x] `IStorage` NVS implementation (`PocketStorage` via ESP32 Preferences)
- [x] `play_effect()` sound effects via HAL (SPIFFS MP3 on ESP32, ALSA tones on native)
- [x] SPIFFS filesystem for sound effects and future file storage
- [x] cw-i2s-sidetone library v1.0.4

## Key Input
- [x] Touch paddle input (dit/dah via capacitive touch strips)
- [x] External paddle jack: iambic or straight key mode (Ext Key setting)
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

## Power Management
- [x] Deep sleep with configurable timeout (0=disabled, 1–60 min)
- [x] Battery voltage measurement via ADC
- [x] Wake via aux button (GPIO0 deep sleep wakeup)

## WiFi & Web Interface
- [x] WiFi captive portal (AP mode, auto SSID "Morserino-XXYY")
- [x] WiFi station mode (connect to existing network)
- [x] WiFi setup screen with QR code (dynamic size based on screen)
- [x] Web configuration SPA (embedded PROGMEM, dark theme, tabbed settings)
- [x] REST API: GET/POST `/api/config`, GET `/api/meta`, GET `/api/screenshot`
- [x] Settings slots via web API: `/api/slots`, `/api/slots/save`, `/api/slots/load`, `/api/slots/delete`
- [x] JSON download/upload of all settings
- [x] Field metadata table drives both JSON API and web UI dynamically

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

## Documentation
- [x] Keyer architecture documentation (`keyer.md`)
- [x] User manual (`manual.md`)
- [x] Implementation progress tracker (`progress.md`)

## Build Targets
- [x] `pocketwroom` — ESP32-S3 WROOM hardware target
- [x] `simulator` — SDL2 desktop simulator
- [x] `native` — unit test target
- [ ] `m5core2` — M5Stack Core2 (builds but untested)
