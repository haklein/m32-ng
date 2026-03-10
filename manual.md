# Morserino-32 NG User Manual

## Overview

The Morserino-32 NG is a versatile CW (Morse code) trainer and keyer. It runs on the
**Morserino Pocket** (ESP32-S3) hardware, the **M5Stack Core2**, and as a **desktop
simulator** (SDL2). All three platforms share the same user interface and feature set
(with minor hardware-specific differences noted below).

---

## Hardware Controls

### Morserino Pocket

| Control | Location | Function |
|---------|----------|----------|
| **Left touch strip** | Left side of case | Dit (dot) paddle |
| **Right touch strip** | Right side of case | Dah (dash) paddle |
| **Encoder dial** | Rotary knob | Rotate: adjust WPM / volume / scroll. Press: select / pause / resume |
| **Encoder button** | Press the encoder | Short press: select or pause/resume. Long press: back / exit to menu |
| **Aux button** | Side button | Single press: toggle WPM/Volume mode. Double press: toggle Scroll mode. Long press: cycle brightness |
| **External paddle jack** | 3.5 mm jack | Connect external iambic paddle or straight key |

### Simulator (Desktop) Keyboard

| Key | Function |
|-----|----------|
| Space | Dit (paddle down/up) |
| Enter | Dah (paddle down/up) |
| `/` | Straight key (down/up) |
| Up arrow | Encoder clockwise (WPM up / scroll up) |
| Down arrow | Encoder counter-clockwise (WPM down / scroll down) |
| `e` | Encoder short press (select / pause / resume) |
| `E` (Shift+e) | Encoder long press (back) |
| `a` | Aux button short press |
| `A` (Shift+a) | Aux button long press (cycle brightness) |
| Escape | Quit |

The simulator also supports **MIDI input** for external paddles (Note 60 = dit,
Note 62 = dah, Note 61 = straight key).

---

## Encoder Modes

In any active CW mode, the encoder dial has three functions that you cycle through
using the **aux button**:

| Encoder Mode | Status Bar Shows | Encoder Dial Does |
|-------------|-----------------|-------------------|
| **WPM** (default) | `20 WPM` | Adjusts speed (5-40 WPM) |
| **Volume** | `Vol 14` | Adjusts volume (0-20) |
| **Scroll** | Up/Down Scroll | Scrolls the text display |

- **Single aux press**: toggles between WPM and Volume.
- **Double aux press** (within 400 ms): toggles Scroll mode on/off.
- **Long aux press**: cycles display brightness (255 > 127 > 63 > 28 > 9).

---

## Main Menu

The main menu is a scrollable list. Use the encoder to navigate, short press to enter.

| # | Icon | Mode | Description |
|---|------|------|-------------|
| 1 | Keyboard | **CW Keyer** | Manual CW keying with real-time decoding |
| 2 | Play | **CW Generator** | Auto-generated CW for listening practice |
| 3 | Loop | **Echo Trainer** | Listen-and-repeat CW practice |
| 4 | Envelope | **QSO Chatbot** | Simulated QSO conversation partner |
| 5 | List | **Content** | Configure training content sources |
| 6 | Gear | **Settings** | System settings (speed, audio, keyer, display) |
| 7 | WiFi | **WiFi Setup** | WiFi connection and web configuration |
| 8 | WiFi | **Internet CW** | CW over the internet (CWCom / MOPP) |
| 9 | Shuffle | **CW Invaders** | Morse code arcade game |

---

## Modes

### CW Keyer

Manual CW transmission using iambic paddles or straight key. Characters are decoded
in real time and displayed on screen.

- Rotate encoder to adjust WPM.
- Decoded characters appear in the text area as you key.
- Encoder short press: no action (passes through to LVGL navigation).
- Encoder long press: return to main menu.

### CW Generator

Generates random CW content and plays it through the sidetone. Use this for
listening practice.

- Content is selected in the **Content** settings screen.
- **Encoder short press** or **aux single press**: pause / resume generation.
- Rotate encoder to adjust WPM live.
- Encoder long press: return to main menu.
- If **Session Size** is set (1-99), generation auto-pauses after that many phrases.
  Short press the encoder to start another session.

### Echo Trainer

The device plays a phrase, then waits for you to key it back. Your response is
decoded and compared.

The screen shows three rows:

| Row | Content |
|-----|---------|
| **Sent:** | The phrase the device played |
| **Rcvd:** | What you keyed back |
| **Result** | Correct (green) or wrong (red) |

- After a correct echo, the next phrase plays automatically.
- After an incorrect echo, the same phrase is repeated (up to **Echo Repeats** times,
  then the answer is revealed and the trainer moves on).
- If **Adaptive WPM** is enabled, speed increases by 1 WPM on success and decreases
  by 1 WPM on failure.
- **Session Size** applies here too.
- Encoder short press: pause / resume.

#### Delete / Correction Signals

While keying your echo response:

| Signal | Effect |
|--------|--------|
| 7+ dits (`<err>`) | Delete last word |
| 4 consecutive E's | Delete last word |

### QSO Chatbot

A virtual CW operator that conducts realistic amateur radio QSO conversations.

The chatbot has a randomly generated persona each session (callsign, name, location,
rig, antenna, etc.) and follows a standard QSO structure:

1. **CQ phase** -- the bot or you initiate a CQ call.
2. **Exchange** -- RST, name, and QTH are exchanged.
3. **Topic rounds** -- rig, power, weather, antenna, etc. (depth depends on
   **QSO Depth** setting).
4. **Closing** -- 73 and SK.

Controls:

- Key your responses with the paddle, just like a real QSO.
- The bot adapts to your speed. Send `QRS` to slow it down, `QRQ` to speed it up.
- Send `AGN` or `?` to request a repeat.
- Encoder short press: pause / resume.
- Encoder long press: return to menu.

**QSO Depth** settings:

| Depth | Duration | Topics |
|-------|----------|--------|
| Minimal | 30-90 sec | Exchange only |
| Standard | 2-5 min | 1-2 topics (rig, weather) |
| Ragchew | 5-30 min | 3-6 topics (rig, antenna, power, weather, age, free-form) |

### CW Invaders

A Space Invaders-style arcade game for Morse character recognition.

- Characters scroll from right to left across the screen.
- Key the correct Morse for each character to destroy it before it reaches the left edge.
- Uses Koch progression: starts with 2 characters (K, M), unlocks new characters
  as you advance levels.
- 3 lives per game; game over when all lives are lost.
- Score: 10 + current streak per hit.
- Difficulty scales with level (faster spawns, faster scrolling).

Controls:

- Paddle: key Morse characters.
- Encoder short press: pause / resume.
- Encoder long press: back to menu.

### Internet CW

Connect to CW servers over WiFi and exchange Morse with other operators.

Two protocols are supported:

| Protocol | Default Server | Description |
|----------|---------------|-------------|
| **CWCom** (MorseKOB) | mtc-kob.dyndns.org:7890 | UDP, duration-based timing |
| **MOPP** | mopp.hamradio.pl:7373 | Compact binary dit/dah encoding |

The screen shows received CW (decoded text) and your transmitted CW. Adjust WPM
with the encoder. Encoder long press disconnects and returns to menu.

Configure the **CWCom Wire** (channel 1-999) and **Callsign** in Network settings.

---

## Content Settings

The **Content** screen configures what the Generator, Echo Trainer, and Chatbot use
as source material. Multiple content types can be enabled simultaneously.

### Content Types

| Type | Description | Example |
|------|-------------|---------|
| **Words** | Common English vocabulary (Oxford 5000, frequency-weighted) | "the", "signal", "copy" |
| **Abbreviations** | Ham radio Q-codes and abbreviations | "QSO", "CQ", "DE", "RST", "HW" |
| **Callsigns** | Randomly generated realistic callsigns | "W1AB", "DL3XY", "VK2ZZ/P" |
| **Characters** | Random character groups | "KMRSU", "38B47" |
| **QSO** | Template-based QSO phrases with random fills | "NAME IS HANS QTH MUNICH" |

### Content Options

| Setting | Range | Description |
|---------|-------|-------------|
| **Character Set** | Alpha / Alpha+Num / All CW | Which characters appear in random groups |
| **Koch Lesson** | 0-50 (0=off) | Limit to first N Koch characters |
| **Koch Order** | LCWO / Morserino / CW Academy / LICW | Character introduction sequence |
| **Max Length** | 0-15 (0=any) | Maximum characters per word/call/abbreviation |
| **QSO Words** | 0-9 (0=all) | Maximum words per QSO phrase |
| **Session Size** | 0-99 (0=unlimited) | Auto-pause after N phrases |

### Koch Character Orders

| Order | Sequence |
|-------|----------|
| LCWO | K M R S U A P T L O W I N J E F 0 Y V ... |
| Morserino | K M R S U A P T L O W I N J E F 0 Y V , G 5 / Q 9 Z H 3 8 B ? 4 2 7 C 1 D 6 X |
| CW Academy | T A N O I S 1 4 R H D L 2 5 U C ... |
| LICW | K M R S U A P T L O W I ... |

### Koch Lesson Filtering

When **Koch Lesson** is set to a value greater than 0, it acts as a *filter* on the
selected content types — it does not replace them. The trainer still generates words,
abbreviations, callsigns, characters, or QSO phrases according to your content type
selections, but only those that can be spelled using the first N characters of your
chosen Koch order.

**How it works with each content type:**

- **Words / Abbreviations**: The trainer picks a random word or abbreviation, checks
  whether every letter is within the Koch character set, and retries (up to 50 attempts)
  if it contains characters not yet learned.
- **Characters**: The Koch character set is intersected with your **Character Set**
  selection (Alpha, Alpha+Num, or All CW). For example, if your Koch lesson includes
  digits but Character Set is "Alpha", you will only get letters from the Koch set.
  Conversely, if Character Set is "Alpha+Num" but your Koch lesson hasn't reached any
  digits yet, you will only get letters.
- **Callsigns / QSO**: Filtered the same way as Words — callsigns and QSO phrases are
  checked against the Koch character set and retried if they contain unknown characters.

**Fallback behavior:** If no matching phrase is found after 50 attempts (e.g. the Koch
lesson is very short and few words can be spelled), the trainer falls back to generating
a random character group drawn from the Koch character set.

**When do digits appear?** Digits become available only once they appear in your Koch
order. The first digit arrives at different lesson numbers depending on the order:

| Order | First Digit | Lesson # |
|-------|-------------|----------|
| LCWO | 5 | 24 |
| Morserino | 0 | 18 |
| CW Academy | 1 | 8 |
| LICW | 5 | 22 |

If you select "Alpha+Num" as Character Set but your Koch lesson hasn't reached any
digits yet, character groups will contain only letters from the Koch set — this is
expected behavior, not a bug.

---

## Settings

### General

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Speed (WPM)** | 5-40 | 20 | Character speed in words per minute |
| **Farnsworth** | 0-40 | 0 (off) | Effective WPM for inter-character spacing (0 = same as Speed) |
| **Frequency** | 400-900 Hz | 700 | Sidetone pitch |
| **Volume** | 0-20 | 14 | Audio volume (2 dB per step) |
| **ADSR** | 1-15 ms | 7 | Tone attack/release envelope (smooth click reduction) |
| **Text Size** | Normal / Large | Normal | CW text display: 20 px or 28 px |
| **Brightness** | 1-255 | 255 | Display backlight level |
| **Sleep** | 0-60 min | 5 | Auto-sleep after inactivity (0 = disabled) |
| **Quick Start** | On / Off | Off | Auto-enter last used mode on power-on |

### Keyer

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Keyer Mode** | Iambic A / Iambic B | Iambic B | Paddle keying mode |
| **Curtis B Dit %** | 0-100 | 75 | Curtis B dit timing compensation |
| **Curtis B Dah %** | 0-100 | 45 | Curtis B dah timing compensation |
| **Ext Key Mode** | Straight / Iambic | Straight | External key jack behavior |
| **Paddle Swap** | On / Off | Off | Swap dit/dah on touch paddles |
| **Ext Key Swap** | On / Off | Off | Swap dit/dah on external key |
| **Screen Flip** | On / Off | Off | Rotate display 180 degrees (lefty mode) |

### Training

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Echo Repeats** | 0-9 | 3 | Max failed attempts before revealing answer (0 = unlimited) |
| **Adaptive WPM** | On / Off | On | Auto-adjust speed on echo success/failure |
| **QSO Depth** | Minimal / Standard / Ragchew | Standard | Chatbot conversation length |

### Network

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| **Internet CW** | CWCom / MOPP | CWCom | Protocol for Internet CW mode |
| **CWCom Wire** | 1-999 | 111 | CWCom channel number |
| **Callsign** | up to 15 chars | (empty) | Your callsign for Internet CW and QSO Chatbot |

---

## WiFi Setup

The WiFi screen lets you connect the device to a WiFi network and access the
web configuration interface.

### AP Mode (Access Point)

When no WiFi network is configured, the device creates its own access point:

- SSID: **Morserino-XXYY** (last 2 bytes of MAC address)
- Default IP: **192.168.4.1**

A QR code is displayed for easy connection from a phone.

### Station Mode

After connecting to your WiFi network through the captive portal:

- The device's IP address is shown on screen.
- A QR code links directly to the web interface.

### Web Configuration Interface

Open `http://<device-ip>/` in a browser to access the full web interface:

- **Tabbed settings editor** -- all settings organized by group (General, Keyer,
  Content, Training, Network), with live editing.
- **JSON download** -- export all current settings as a JSON file.
- **JSON upload** -- import settings from a previously saved JSON file.
- **Settings slots** -- save up to 8 named snapshots of all settings, and restore
  them instantly. Useful for switching between different practice configurations.
- **Screenshot** -- capture a live screenshot of the device display.

#### API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/config` | All settings as JSON |
| POST | `/api/config` | Update settings from JSON body |
| GET | `/api/meta` | Field metadata (types, ranges, labels) |
| GET | `/api/screenshot` | BMP screenshot |
| GET | `/api/slots` | List saved setting slots |
| GET | `/api/slots/save?name=X` | Save current settings to slot X |
| GET | `/api/slots/load?name=X` | Load settings from slot X |
| GET | `/api/slots/delete?name=X` | Delete slot X |

---

## Status Bar

A thin bar at the top of every screen shows current state:

| Position | Content |
|----------|---------|
| Left | Mode icon (keyboard, play, loop, envelope, gear, etc.) |
| Center-left | Speed info: `20 WPM`, `20(15)W` (Farnsworth), or `Vol 14` (volume mode) |
| Right | Battery icon (color-coded) and WiFi icon (green = connected, orange = AP mode) |

### Battery Indicator

| Level | Icon | Color |
|-------|------|-------|
| 80-100% | Full | Green |
| 60-79% | 3/4 | Yellow-green |
| 40-59% | 1/2 | Yellow |
| 20-39% | 1/4 | Orange |
| 0-19% | Empty | Red |
| Charging | Lightning bolt | Green |

---

## Iambic Keyer

The built-in keyer supports **Iambic A** and **Iambic B** (Curtis B) modes.

### Iambic A

Squeezing both paddles produces alternating dits and dahs. Releasing both paddles
immediately stops output after the current element completes.

### Iambic B (Default)

Same as Iambic A, but when both paddles are released during the inter-element gap
of an alternating sequence, **one additional opposite element** is automatically
inserted. This is the standard Curtis B behavior preferred by most operators.

The **Curtis B Dit %** and **Curtis B Dah %** settings control how much of each
element's duration counts as the "Curtis B window" for detecting the opposite
paddle release.

### Straight Key

When **Ext Key Mode** is set to Straight, the external key jack operates as a
straight key -- the tone sounds for exactly as long as the key is held down.

---

## Power Management

### Sleep

The device enters deep sleep after the configured inactivity timeout (default 5
minutes). Sleep can also be triggered by low battery.

Wake the device by pressing the aux button.

On wake, the device performs a full reboot. If **Quick Start** is enabled, it
automatically enters the last used mode. Otherwise, it shows the main menu.

### Brightness

Display brightness can be cycled through 5 preset levels (255, 127, 63, 28, 9)
by long-pressing the aux button. The current level is persisted across reboots.

---

## Supported CW Characters

### Letters
a-z (case-insensitive)

### Numbers
0-9

### Punctuation
`.` `,` `:` `;` `/` `=` `?` `@` `+`

### Umlauts
a, o, u (German umlauts)

### Prosigns
`<as>` `<ka>` `<kn>` `<sk>` `<ve>` `<bk>` `<ch>`

---

## Quick Reference Card

| Action | Pocket Hardware | Simulator Key |
|--------|----------------|---------------|
| Dit | Left touch / Left paddle | Space |
| Dah | Right touch / Right paddle | Enter |
| Straight key | External jack | `/` |
| WPM up | Encoder CW | Up arrow |
| WPM down | Encoder CCW | Down arrow |
| Select / Pause / Resume | Encoder short press | `e` |
| Back / Exit | Encoder long press | `E` |
| Toggle WPM / Volume | Aux single press | `a` |
| Toggle Scroll mode | Aux double press | `a` `a` (quick) |
| Cycle brightness | Aux long press | `A` |
| Quit (simulator only) | -- | Escape |
