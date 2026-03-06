# Morserino-32-NG v1.x – Implementation Specification

## 1. Purpose and Scope

This document translates the Morserino-32-NG Functional Specification (FSD) into
**concrete implementation requirements** for the initial hardware targets.
It defines platform selection, architecture, HAL contract, library choices,
module boundaries, and coding standards.

Out of scope:
- Feature behavior already defined in the FSD
- Schematic / PCB / pin assignments beyond what is documented here

---

## 2. Build Environment

### 2.1 Toolchain and Targets

| Property           | Value                                                                    |
|-------------------|--------------------------------------------------------------------------|
| Build system       | **PlatformIO** (pio)                                                     |
| Framework          | Arduino core for ESP32 (ESP-IDF base)                                    |
| Primary target     | `env:pocketwroom` — ESP32-S3, Morserino Pocket                           |
| Secondary target   | `env:m5core2` — M5Stack Core2, primary touchscreen dev/test device       |
| Native test target | `env:native` — host-compiled headless unit tests (no display)            |
| Simulator target   | `env:simulator` — interactive SDL2 desktop app, full functional trainer  |
| Language           | C++17                                                                    |

### 2.2 Repository Layout (Monorepo)

```
morserino-ng/
├── platformio.ini
├── support/
│   └── sdl2_build_extra.py # post-script: aliases `pio run -t upload` → execute binary
├── libs/
│   ├── cw-engine/          # CW timing, keyer state machine, char-to-morse
│   ├── cw-decoder/         # Goertzel filter + character decoder
│   ├── cw-generator/       # Content generation (random, files, QSO)
│   ├── training/           # Echo trainer, Koch trainer orchestration
│   ├── preferences/        # Preference model + snapshot logic
│   ├── content/            # Word lists, callsign base, QSO phrase engine
│   ├── hal/
│   │   ├── interfaces/     # Pure-virtual HAL contracts (no platform headers)
│   │   ├── esp32s3/        # Morserino Pocket implementation
│   │   ├── m5core2/        # M5Stack Core2 implementation
│   │   └── native/         # Host implementations: ALSA, MIDI, SDL keyboard
│   ├── ui/                 # LVGL screens, menu framework
│   ├── games/              # CW training games (CW Invaders, etc.)
│   ├── transceiver/        # Network-level CW transceiver
│   └── ext-iface/          # USB serial, BT HID
├── src/
│   ├── main.cpp            # Arduino entry point: HAL wiring, task startup
│   └── simulator/
│       └── main.cpp        # Native int main(): SDL2 init, LVGL event loop
└── test/
    ├── native/             # Host-compiled unit tests (no hardware needed)
    └── embedded/           # On-device integration tests
```

### 2.3 Per-Library Rule

Every library under `libs/` must:

- Compile independently with no knowledge of other libraries except `hal/interfaces`
- Include a `test/` subdirectory with functional tests runnable under `env:native`
- Expose a clean public header; implementation files stay in `src/`
- Have no platform-specific `#include` outside of `hal/<platform>/`

### 2.4 Native Target Strategy

Two native environments with different purposes:

#### `env:native` — Headless CI / Unit Tests

- Compiles `test/native/` sources only; no display, no SDL2 dependency
- HAL interfaces are pure virtual C++; `hal/native/` provides deterministic stubs
- FreeRTOS calls are wrapped behind a thin `os/` abstraction:
  - `os::Queue<T>` — `QueueHandle_t` on device; `std::queue` + mutex on native
  - `os::Task` — `xTaskCreate` on device; `std::thread` on native
  - `os::Timer` — `esp_timer` on device; `std::chrono::steady_clock` on native
- **Timing injection pattern**: any class that needs wall-clock time accepts a
  `millis_fn` (or `os::Timer` reference) in its constructor — never calls
  `millis()` directly. This is already proven in `IambicKeyer` / `SymbolPlayer`
  from m5core2-cwtrainer and is the required pattern for all new timing code.
- All business logic (model, agents, content) must compile and pass tests on native

#### `env:simulator` — Interactive SDL2 Desktop App

A fully functional Morse trainer running on Linux/macOS/Windows.
Intended for manual QA, UI development, and as a standalone PC Morse trainer.

**Display:** LVGL's built-in SDL2 driver (`LV_USE_SDL=1`).
Window size 480×320 (scales the physical 170×320 display).
Enabled via `-D LV_CONF_SKIP` + individual build flags; the shared `include/lv_conf.h`
(used by embedded targets) is bypassed entirely to avoid polluting it.

**Audio:** `NativeAudioOutputAlsa` (ALSA PCM, complex-rotor oscillator with
Blackman-Harris ADSR ramps) — same class as `env:native`.

**Key input:** `NativeKeyInputSdl` (`hal/native/key_input_sdl.h`) composites:
1. SDL keyboard events (mapped below)
2. ALSA MIDI events from `NativeKeyInputMidi` when a MIDI device is available

**SDL keyboard → `KeyEvent` mapping:**

| Key         | `KeyEvent`             | Rationale                        |
|-------------|------------------------|----------------------------------|
| `Space`     | `PADDLE_DIT_DOWN/UP`   | Left-thumb dit (natural iambic)  |
| `Return`    | `PADDLE_DAH_DOWN/UP`   | Right-thumb dah                  |
| `/`         | `STRAIGHT_DOWN/UP`     | Straight key                     |
| `↑`         | `ENCODER_CW`           | Rotary encoder clockwise         |
| `↓`         | `ENCODER_CCW`          | Rotary encoder counter-clockwise |
| `e`         | `BUTTON_ENCODER_SHORT` | Encoder push (short)             |
| `E`         | `BUTTON_ENCODER_LONG`  | Encoder push (long)              |
| `a`         | `BUTTON_AUX_SHORT`     | Aux button (short)               |
| `A`         | `BUTTON_AUX_LONG`      | Aux button (long)                |
| `Escape`    | quit                   | Clean SDL2 shutdown              |

**Entry point:** `src/simulator/main.cpp` — plain `int main()`:
1. `lv_init()` + `lv_sdl_window_create(480, 320)` + mouse/wheel indevs
2. Instantiate `NativeAudioOutputAlsa` + `NativeKeyInputSdl`
3. Event loop: `SDL_Delay(5)` → `lv_tick_inc()` → `lv_timer_handler()` → key poll
4. Graceful exit on `SDL_QUIT`

**Running the simulator:**
```
pio run -e simulator -t upload   # builds and immediately launches the window
```
(The `support/sdl2_build_extra.py` post-script aliases `upload` to execute the binary.)

---

## 3. Coding Standards

- **File size limit:** 300 lines per source file; split at logical boundaries
- **Naming:** `snake_case` for functions and variables; `PascalCase` for classes
  and structs; `SCREAMING_SNAKE_CASE` for constants and enum values
- **Comments:** explain *why*, not *what*; self-documenting names preferred
- **No magic numbers:** all hardware constants and tunable values are named
  constants in config headers
- `delay()` and `millis()` are **forbidden** outside HAL implementations
  (use `os::Timer` / injected time function)
- `Arduino::String` is **forbidden** in any library under `libs/`; use `std::string`
  or plain `char` buffers
- `random()` is **forbidden** outside HAL; use `std::mt19937` seeded from
  `ISystemControl::entropy_seed()`
- No global mutable state outside `main.cpp` wiring; pass dependencies explicitly

---

## 4. Architecture

### 4.1 Layered Model

```
┌──────────────────────────────────────────────────────────┐
│                     UI Layer (View)                      │
│   LVGL Screens  ·  Menu Stack  ·  Input Event Consumer   │
├──────────────────────────────────────────────────────────┤
│                 Application Layer (Controller)           │
│  Mode Managers  ·  Agent Tasks  ·  Event Router          │
├──────────────────────────────────────────────────────────┤
│                    Model / State Layer                   │
│  Preferences  ·  Snapshots  ·  CW Timing  ·  Stats       │
│  Content Buffers  ·  Decoder State  ·  Session State     │
├──────────────────────────────────────────────────────────┤
│              Hardware Abstraction Layer (HAL)            │
│  IKeyInput · IAudioOutput · IAudioInput · IDisplay       │
│  IStorage  · INetwork  · ISystemControl                  │
└──────────────────────────────────────────────────────────┘
```

### 4.2 Strict Layer Rules

- **View** reads Model state and receives `UiEvent`s from the Application Layer.
  It never writes to Model directly.
- **Model** is pure state and data — no platform or HAL calls; fully native-testable.
- **Application Layer** owns all mode logic: reads/writes Model, calls HAL via
  interfaces, dispatches `UiEvent`s to View.
- **HAL implementations** are the only files allowed to include platform SDK headers.

### 4.3 Agent-to-Task Mapping

Each FSD agent is a FreeRTOS task communicating via typed `os::Queue` instances.
No shared global mutable state between tasks; all coordination via queues.

| FSD Agent                    | Class / Task                   | Priority | Core |
|------------------------------|--------------------------------|----------|------|
| CW Timing & Keying           | `cw::KeyerTask`                | High     | 1    |
| CW Generation                | `cw::GeneratorTask`            | Normal   | 0    |
| Training Orchestration       | `training::OrchestratorTask`   | Normal   | 0    |
| Decoding                     | `cw::DecoderTask`              | High     | 1    |
| Transceiver & Networking     | `net::TransceiverTask`         | Normal   | 0    |
| File & Content               | `content::FileTask`            | Low      | 0    |
| Preferences & Snapshot       | `prefs::Manager` (sync)        | —        | —    |
| External Interface           | `ext::InterfaceTask`           | Normal   | 0    |
| UI & Navigation              | `ui::UiTask` (main task)       | Normal   | 0    |

### 4.4 Event Types

All inter-task events are plain C++ structs defined in `libs/hal/interfaces/events.h`.

```cpp
struct CwElementEvent   { bool is_dit; bool key_down; };
struct CharDecodedEvent  { char character; };
struct ModeChangeEvent   { Mode target_mode; };
struct UiEvent           { UiEventType type; int32_t value; };
```

### 4.5 CW Input Pipeline

This pipeline is the same on all targets; only the HAL leaf nodes differ.

```
IKeyInput::poll()  (PocketKeyInput / NativeKeyInputSdl+Midi / …)
  │
  ├── PADDLE_DIT_DOWN/UP ──→ PaddleCtl::setDotPushed(bool)
  ├── PADDLE_DAH_DOWN/UP ──→ PaddleCtl::setDashPushed(bool)
  │        │
  │        │  PaddleCtl::tick()  (debounce + lever-state machine)
  │        │
  │    on_lever_state(LeverState)
  │        │
  │   IambicKeyer::setLeverState()
  │        │
  │        │  IambicKeyer::tick()  (mode-A/B iambic state machine)
  │        │
  │    on_play_state(PlayState)
  │        │
  │   ┌────┴──────────────────────────────┐
  │   │                                   │
  │ IAudioOutput::tone_on/off()    MorseDecoder::append_dot/dash()
  │                                set_transmitting(bool)
  │                                       │
  │                                MorseDecoder::tick()
  │                                (fires on inter-char timeout)
  │                                       │
  │                               on_letter_decoded(string)
  │                                       │
  └── STRAIGHT_DOWN/UP ──(inline)──→  CWTextField::add_string()
      (duration → dot or dash)          next_word() on space
```

Key classes and their responsibilities:

| Class            | Library        | Responsibility                                      |
|-----------------|----------------|-----------------------------------------------------|
| `PaddleCtl`      | `cw-engine`    | Debounce + combine dot/dash edges → `LeverState`    |
| `IambicKeyer`    | `cw-engine`    | Mode-A/B iambic state machine → `PlayState` events  |
| `SymbolPlayer`   | `cw-engine`    | Timing of individual dots and dashes                |
| `MorseDecoder`   | `cw-decoder`   | Binary-tree CW decoder; fires letter callback       |
| `CWTextField`    | `ui`           | LVGL spangroup display; per-word OK/ERR colouring   |

All timing is injected via `millis_fun_ptr`; no `millis()` / `delay()` calls.
Speed is controlled via `IambicKeyer::setSpeedWPM()` or by passing `DIT_MS`
(`1200 / wpm`) as the `duration_unit` constructor argument.

---

## 5. Hardware Abstraction Layer (HAL)

Interfaces live in `libs/hal/interfaces/`.
ESP32-S3 implementations live in `libs/hal/esp32s3/`.
M5Core2 implementations live in `libs/hal/m5core2/`.
Native stubs live in `libs/hal/native/`.

### 5.1 IKeyInput

```cpp
enum class KeyEvent {
    PADDLE_DIT_DOWN, PADDLE_DIT_UP,
    PADDLE_DAH_DOWN, PADDLE_DAH_UP,
    STRAIGHT_DOWN, STRAIGHT_UP,
    ENCODER_CW, ENCODER_CCW,
    BUTTON_ENCODER_SHORT, BUTTON_ENCODER_LONG,
    BUTTON_AUX_SHORT, BUTTON_AUX_LONG,
    TOUCH_LEFT_DOWN, TOUCH_LEFT_UP,
    TOUCH_RIGHT_DOWN, TOUCH_RIGHT_UP,
};

class IKeyInput {
public:
    virtual ~IKeyInput() = default;
    virtual bool poll(KeyEvent& out) = 0;
    virtual bool wait(KeyEvent& out, uint32_t timeout_ms) = 0;
};
```

On M5Core2: `BUTTON_AUX_SHORT/LONG` map to M5.BtnA (or touch button region);
`ENCODER_CW/CCW` are not emitted (no encoder hardware) — navigation is touch-only.

### 5.2 IAudioOutput

```cpp
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;
    virtual void begin() = 0;
    virtual void tone_on(uint16_t frequency_hz) = 0;
    virtual void tone_off() = 0;
    virtual void set_volume(uint8_t level_0_to_10) = 0;
    virtual void set_adsr(float attack, float decay, float sustain, float release) = 0;
    virtual void suspend() = 0;   // codec / amp power-down before sleep
};
```

### 5.3 IAudioInput

```cpp
class IAudioInput {
public:
    virtual ~IAudioInput() = default;
    virtual void begin(uint32_t sample_rate_hz) = 0;
    virtual uint8_t signal_level() = 0;   // normalised 0–100
    using DetectCallback = std::function<void(bool carrier_present)>;
    virtual void set_detect_callback(DetectCallback cb) = 0;
};
```

### 5.4 IDisplay

```cpp
class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual void begin() = 0;
    virtual void set_brightness(uint8_t level_0_to_255) = 0;
    virtual void sleep() = 0;
    virtual void wake() = 0;
    virtual uint16_t width() const = 0;
    virtual uint16_t height() const = 0;
    virtual bool has_touch() const = 0;
    // Called by LVGL flush callback
    virtual void flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       const uint16_t* color_map) = 0;
    // Called by LVGL touch read callback; returns false if no touch
    virtual bool get_touch(int16_t& x, int16_t& y) = 0;
};
```

### 5.5 IStorage

```cpp
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool   get_int(const char* ns, const char* key, int32_t& val) = 0;
    virtual void   set_int(const char* ns, const char* key, int32_t val) = 0;
    virtual bool   get_string(const char* ns, const char* key,
                              char* buf, size_t len) = 0;
    virtual void   set_string(const char* ns, const char* key, const char* val) = 0;
    virtual void   commit() = 0;
    virtual bool   file_open_read(const char* path) = 0;
    virtual bool   file_open_write(const char* path) = 0;
    virtual int    file_read(uint8_t* buf, size_t len) = 0;
    virtual int    file_write(const uint8_t* buf, size_t len) = 0;
    virtual void   file_close() = 0;
    virtual bool   file_exists(const char* path) = 0;
    virtual bool   file_remove(const char* path) = 0;
    virtual void   list_files(const char* dir,
                              std::function<void(const char* name, size_t size)> cb) = 0;
};
```

### 5.6 INetwork

```cpp
enum class CwProto { ESP_NOW, UDP, TCP };

class INetwork {
public:
    virtual ~INetwork() = default;
    virtual bool   wifi_connect(const char* ssid, const char* password,
                                uint32_t timeout_ms) = 0;
    virtual bool   wifi_start_ap(const char* ssid, const char* password) = 0;
    virtual void   wifi_disconnect() = 0;
    virtual bool   wifi_is_connected() = 0;
    virtual void   wifi_get_ip(char* buf, size_t len) = 0;
    virtual bool   cw_connect(CwProto proto, const char* target, uint16_t port) = 0;
    virtual void   cw_disconnect() = 0;
    virtual bool   cw_send(const uint8_t* data, size_t len) = 0;
    virtual int    cw_receive(uint8_t* buf, size_t len, uint32_t timeout_ms) = 0;
    virtual bool   cw_is_connected() = 0;
};
```

### 5.7 ISystemControl

```cpp
class ISystemControl {
public:
    virtual ~ISystemControl() = default;
    virtual void     deep_sleep(uint32_t wakeup_gpio_mask) = 0;
    virtual void     restart() = 0;
    virtual uint32_t uptime_ms() = 0;
    virtual uint8_t  battery_percent() = 0;   // 255 = not available
    virtual bool     is_charging() = 0;
    virtual void     set_power_rail(bool on) = 0;
    virtual uint32_t entropy_seed() = 0;   // hardware entropy for RNG seeding
};
```

---

## 6. ESP32-S3 Hardware Map (Morserino Pocket WROOM)

Constants defined in `libs/hal/esp32s3/pocket_pins.h`.
Referenced only inside `libs/hal/esp32s3/`.

### Display (ST7789, 170×320)

| Signal        | GPIO | Notes              |
|--------------|------|--------------------|
| SPI MOSI     | 48   |                    |
| SPI SCLK     | 38   |                    |
| CS           | 39   |                    |
| DC           | 47   |                    |
| RST          | 40   |                    |
| Backlight    | 21   | PWM                |
| Pixel width  | 170  |                    |
| Pixel height | 320  |                    |
| X offset     | 35   | panel offset       |

### I2S Audio

| Signal      | GPIO |
|------------|------|
| BCK        | 16   |
| LRCK       | 17   |
| DATA OUT   | 18   |
| DATA IN    | 8    |

### Codec (TLV320AIC3100) I2C + Control

| Signal         | GPIO |
|---------------|------|
| SDA           | 15   |
| SCL           | 7    |
| INT (headset) | 6    |
| RST / PWR EN  | 45   |

### User Input

| Signal         | GPIO |
|---------------|------|
| Paddle LEFT   | 2    |
| Paddle RIGHT  | 1    |
| Straight key  | 41   |
| Touch LEFT    | 4    |
| Touch RIGHT   | 5    |
| Encoder DT    | 14   |
| Encoder CLK   | 13   |
| Encoder button| 12   |

### Power Management (MCP73871)

| Signal    | GPIO |
|----------|------|
| VEXT EN  | 42   |
| PG       | 11   |
| STAT1    | 9    |
| STAT2    | 10   |
| BATT ADC | 3    |

---

## 7. M5Stack Core2 Hardware Map (Touchscreen Dev/Test Target)

Constants defined in `libs/hal/m5core2/m5core2_pins.h`.
The M5Core2 is the **primary development and touchscreen validation target**
due to its integrated capacitive touch display and convenient form factor.

### Display

| Property    | Value                                        |
|------------|----------------------------------------------|
| Controller | ILI9342C (320×240, landscape)                |
| Driver     | LovyanGFX M5Core2 config via `M5Unified`     |
| Touch      | FT6336 capacitive, integrated, via M5Unified |
| `has_touch`| `true`                                       |

`M5Unified` provides an LGFX-compatible device; `IDisplay` wraps it.

### Audio

| Property      | Value                                                     |
|--------------|-----------------------------------------------------------|
| Output        | I2S via `haklein/cw-i2s-sidetone` (same lib as Pocket)   |
| Amp enable    | AXP192 GPIO2 (SPK_EN) via `M5.Power.Axp192.setGPIO2(true)` |
| I2S BCK       | GPIO 12                                                   |
| I2S LRCK      | GPIO 0                                                    |
| I2S DATA OUT  | GPIO 2                                                    |
| Audio input   | Not available on base Core2 — decoder key-input only      |

### User Input

| Signal         | Source                              |
|---------------|-------------------------------------|
| Paddle LEFT   | GPIO 32 (external)                  |
| Paddle RIGHT  | GPIO 33 (external)                  |
| Button A/B/C  | M5Unified touch buttons (y > 240)   |
| Touch screen  | FT6336 via M5Unified                |
| Encoder       | Not present — no `ENCODER_*` events |

Navigation on M5Core2 is touch-only. LVGL pointer input driver is primary;
encoder input driver is registered but emits no events.

### Power Management

| Property     | Value                          |
|-------------|--------------------------------|
| PMIC         | AXP192 via M5Unified           |
| Battery %    | `M5.Power.getBatteryLevel()`   |
| Charging     | `M5.Power.isCharging()`        |
| Sleep        | `M5.Power.powerOff()` / light sleep with touch wakeup |

---

## 8. Library Selection

### 8.1 External Libraries

| Concern               | Library                              | Rationale                                       |
|-----------------------|--------------------------------------|-------------------------------------------------|
| Display driver        | **LovyanGFX**                        | Via DisplayWrapper (Pocket) or M5Unified (Core2)|
| UI framework          | **LVGL v9**                          | Touch-ready, encoder-ready, widget library      |
| LGFX/LVGL bridge      | `haklein/DisplayWrapper`             | Proven Pocket pin config, flush/touch callbacks |
| CW sidetone           | `haklein/cw-i2s-sidetone`           | I2S + ADSR sine; used identically on both targets |
| Audio codec (Pocket)  | `haklein/tlv320aic31xx`             | TLV320AIC3100 I2C driver, headset detect        |
| M5Core2 SDK           | `M5Unified`                          | Display, touch, power, buttons on Core2         |
| NVS / Preferences     | ESP32 Preferences (Arduino)          | Wear-levelled NVS                               |
| File system           | LittleFS (ESP32 Arduino)             | Reliable on flash, partial-write safe           |
| WiFi web UI           | ESPAsyncWebServer + AsyncTCP         | Non-blocking HTTP + WebSocket                   |
| BT HID keyboard       | NimBLE-Arduino                       | Lower RAM than classic BT stack                 |
| ESPNow                | Built-in ESP-IDF ESPNow API          | Native, low-latency peer-to-peer                |
| JSON                  | ArduinoJson v7                       | Zero-alloc parsing mode                         |
| Rotary encoder        | ESP32Encoder (half-quadrature)       | Hardware-decoded, interrupt-driven (Pocket only)|
| Button debounce       | ClickButton                          | Short/long/multi-click detection (Pocket only)  |
| OTA update            | ESP32 `Update` library (Arduino)     | Partition-aware, invoked from web UI            |

### 8.2 Internal Libraries Adopted from m5core2-cwtrainer

See §20 for full adoption details and required changes per library.

| Source Library   | Target in morserino-ng         | Effort   |
|-----------------|--------------------------------|----------|
| `IambicKeyer`   | `libs/cw-engine/` (core)       | None — already agnostic |
| `SymbolPlayer`  | `libs/cw-engine/` (core)       | None — already agnostic |
| `PaddleCtl`     | `libs/cw-engine/` (core)       | None — already agnostic |
| `StraightKeyer` | `libs/cw-engine/`              | Minor — inject GPIO read + millis |
| `MorseDecoder`  | `libs/cw-decoder/`             | Minor — replace `Arduino::String` with `std::string` |
| `Goertzel`      | `libs/cw-decoder/`             | Minor — inject ADC sample buffer |
| `MorseTrainer`  | `libs/training/` (basis)       | Moderate — inject millis, replace String |
| `TextGenerators`| `libs/content/`                | Minor — replace String + random() |
| `english_words.h` | `libs/content/`              | None — pure data, copy as-is |
| `abbrevs.h`     | `libs/content/`                | None — pure data, copy as-is |

---

## 9. Audio Subsystem

### 9.1 Pocket: Initialization Sequence

The TLV320AIC3100 PLL uses I2S BCLK as its clock source, so the codec must
be configured over I2C before the sidetone generator starts I2S.

```
1. Assert codec RST / power-enable (GPIO 45 high)
2. TLV320AIC31xx::begin() — I2C register setup
3. Set word length (16-bit), PLL source = BCLK
4. PLL: P=1, R=2, J=32, D=0; dividers: NDAC=8, MDAC=2, NADC=8, MADC=2
5. Enable PLL, DAC, ADC; set gains
6. Enable appropriate output (speaker or headphone based on headset detect)
7. I2S_Sidetone::begin(44100, 16, 2, 128) — starts I2S; BCLK now locks codec PLL
8. I2S_Sidetone::set_frequency(600 Hz default)
```

### 9.2 Pocket: Speaker / Headphone Switching

GPIO 6 interrupt fires on headset plug/unplug. Handler reads
`AIC31XX_INTRDACFLAG` bit `AIC31XX_HSPLUG` to select output path.
Speaker: +6 dB gain. Headphone: speaker equivalent −12 dB (sensitivity offset).

### 9.3 Volume Mapping (both targets)

Volume preference is 0–19. Mapping to analog dB:

| Level | dB     |
|-------|--------|
| 0     | −78 (mute) |
| 1     | −60    |
| 2     | −54    |
| 3–19  | −48 to 0, in −3 dB steps |

On Pocket: applied to codec `setSpeakerVolume()` / `setHeadphoneVolume()`.
On M5Core2: applied to `I2S_Sidetone::setVolume()` (0.0–1.0 mapped from table).

### 9.4 M5Core2: Audio Setup

AXP192 GPIO2 (SPK_EN) must be asserted before I2S output is audible.
`I2S_Sidetone::begin()` is the only audio init call; no external codec.
Audio input (CW decoder) is not available on base Core2 — decoder operates
from key input only on this target.

### 9.5 Suspend and Resume

Before sleep: `IAudioOutput::suspend()`.
- Pocket: calls `TLV320AIC31xx::reset()` (de-asserts codec RST / PWR EN).
- M5Core2: mutes amp via AXP192 GPIO2 low.
On wakeup (cold start): full initialization repeats.

---

## 10. UI Layer

### 10.1 Framework

All UI is built with **LVGL v9** widgets. LovyanGFX provides flush and touch
callbacks. The UI layer has no knowledge of which target is running — it
interacts only with the `IDisplay` interface and LVGL APIs.

The UI is **touch-ready by design**:
- All interactive elements sized for finger touch (minimum 44×44 px tap target)
- Touch and encoder navigation coexist; LVGL supports both input device drivers
  simultaneously
- On targets without encoder (M5Core2), encoder driver registers but fires no events

### 10.2 Input Device Drivers

Two LVGL input device drivers are registered:

1. **Encoder driver** — `ENCODER_CW/CCW` → `LV_KEY_LEFT/RIGHT`; encoder button
   → `LV_KEY_ENTER`; aux button long → `LV_KEY_ESC`
2. **Touch/pointer driver** — reads `IDisplay::get_touch()`

### 10.3 Navigation Model

- Navigation is a **screen stack** managed by `ui::ScreenStack`
- Encoder rotation: scroll list / adjust focused widget
- Encoder short press: select / confirm (menu/settings); **pause/resume**
  (generator, echo trainer, QSO chatbot)
- Encoder long press: back (pop screen)
- Aux button short press: **pause/resume** (generator, echo trainer, QSO chatbot)
- Aux button long press: return to main menu (pop all screens)
- Display layout: **status bar** (fixed top) + **content area** (scrollable)
- Status bar: mode name · WPM · battery · WiFi/BT icons

### 10.4 Screen Dimensions by Target

| Target     | Width | Height | Status bar | Content area |
|-----------|-------|--------|------------|--------------|
| Pocket    | 170   | 320    | 20 px      | 300 px       |
| M5Core2   | 320   | 240    | 20 px      | 220 px       |

### 10.5 Typography — Intel One Mono

CW text areas use the **Intel One Mono** typeface
([github.com/intel/intel-one-mono](https://github.com/intel/intel-one-mono)),
licensed under OFL-1.1.  Menus, status bar, and other UI chrome retain LVGL's
built-in **Montserrat** as the default font because it includes the icon glyphs
required by the UI.

**Font usage:**

| Element                     | Font              | Size        | Weight  |
|-----------------------------|-------------------|-------------|---------|
| Menus / status bar / icons  | Montserrat (LVGL) | 14 px       | Regular |
| CW text areas (Normal)      | Intel One Mono    | 20 px       | Medium  |
| CW text areas (Large)       | Intel One Mono    | 28 px       | Bold    |

The **Text Size** preference (Normal / Large) lets the user choose between
20 px and 28 px for CW text areas — all CWTextField instances, echo trainer
sent/received labels, and chatbot text fields. The setting takes effect the
next time the user enters a CW screen (screens are rebuilt on navigation).

CW text areas auto-scroll to keep the most recent text visible. The widget is
implemented as a `lv_spangroup` inside a scrollable `lv_obj` container;
`scroll_to_bottom()` is called after every text update.

On M5Core2 the wider layout allows a two-column preference list.

**LVGL integration:**

Intel One Mono fonts are applied explicitly on text-area widgets via
`lv_obj_set_style_text_font()`, selecting the font returned by the
`cw_text_font()` helper based on the current Text Size preference.
The LVGL default font (`LV_FONT_DEFAULT`) remains Montserrat 14 so that all
other UI elements — menus, labels, icons, status bar — render correctly.

The TTF source files are converted to LVGL bitmap fonts using `lv_font_conv`.
Each required size/weight combination is generated as a C array (`lv_font_t`)
and compiled in. Only the ASCII + extended Latin character ranges
(Unicode 0x0020–0x017F) are included to minimise flash footprint.

Font source files are stored in `libs/ui/fonts/`:

```
libs/ui/fonts/
├── IntelOneMono-Regular.ttf    # source (not compiled directly)
├── IntelOneMono-Medium.ttf
├── IntelOneMono-Bold.ttf
├── lv_font_intel_14.c          # generated: 14 px Regular (available, not yet assigned)
├── lv_font_intel_20.c          # generated: 20 px Medium  (Text Size = Normal)
└── lv_font_intel_28.c          # generated: 28 px Bold    (Text Size = Large)
```

The generated `lv_font_*` files are checked into the repository so that
`lv_font_conv` (Node.js) is not required for normal builds.

---

## 11. CW Engine Library (`libs/cw-engine/`)

**Basis:** `IambicKeyer`, `SymbolPlayer`, `PaddleCtl`, `StraightKeyer` from
m5core2-cwtrainer. These three are already fully platform-agnostic and are
adopted with minimal changes (see §20).

### 11.1 Module Structure

```
libs/cw-engine/
├── include/
│   ├── char_morse_table.h    # shared by engine + generator; replaces duplication
│   ├── symbol_player.h       # from cwtrainer (adopted as-is)
│   ├── iambic_keyer.h        # from cwtrainer (adopted as-is)
│   ├── paddle_ctl.h          # from cwtrainer (adopted as-is)
│   └── straight_keyer.h      # from cwtrainer (minor refactor)
└── src/
    ├── char_morse_table.cpp
    ├── symbol_player.cpp
    ├── iambic_keyer.cpp
    ├── paddle_ctl.cpp
    └── straight_keyer.cpp
```

`char_morse_table` consolidates the `char2morse()` function that was
**duplicated** in both `MorsePlayer.cpp` and `MorseTrainer.cpp` in cwtrainer.

### 11.2 Timing

- All timing uses `os::Timer` / injected `millis_fn` — never `millis()` directly
- Dit length in µs = `1 200 000 / wpm` (PARIS standard, matching cwtrainer formula)
- Dah = 3× dit; inter-element = 1× dit; inter-char = 3× dit; inter-word = 7× dit
  (adjusted by spacing preferences)
- ISRs only enqueue raw paddle edge events; state machine runs in `KeyerTask`

### 11.3 Keyer Modes

Iambic A, Iambic B, Ultimatic, Non-squeeze, Straight key — mode is a preference,
applied at next idle state. Ultimatic and Non-squeeze extend the Mode A/B state
machine in `iambic_keyer.cpp`.

### 11.4 Memory Keyer

- 8 memories, up to 100 characters each, stored in NVS namespace `keyer_mem`
- Token encoding: printable ASCII + escape tokens for prosigns (`<sk>` etc.),
  pauses (`|`), speed changes (`+N` / `-N`)
- Playback synthesises paddle events into `KeyerTask` queue; live paddle input
  interrupts playback

---

## 12. CW Decoder Library (`libs/cw-decoder/`)

**Basis:** `Goertzel` and `MorseDecoder` from m5core2-cwtrainer (see §20).

### 12.1 Module Structure

```
libs/cw-decoder/
├── include/
│   ├── goertzel_detector.h   # refactored from cwtrainer Goertzel
│   └── morse_decoder.h       # refactored from cwtrainer MorseDecoder
└── src/
    ├── goertzel_detector.cpp
    └── morse_decoder.cpp
```

### 12.2 Audio Path

- `IAudioInput::begin(8000)` — 8 kHz sample rate
- `GoertzelDetector` accepts pre-sampled `int16_t` blocks; does **not** call
  `analogRead()` internally (refactored from cwtrainer Goertzel)
- Block size: 50 ms = 400 samples; configurable between 25–100 ms
- Target frequency tracks configured sidetone pitch ±50 Hz
- Adaptive threshold: rolling RMS, threshold = 1.5× noise floor
  (algorithm preserved from cwtrainer Goertzel)

### 12.3 Character Decoder

`MorseDecoder` is adopted from cwtrainer with `Arduino::String` replaced by
`std::string`. The 69-entry CW binary tree (letters, digits, punctuation,
prosigns, umlauts) and timeout-based decode logic are preserved unchanged.

Character decoded callback: `std::function<void(const std::string&)>`.

### 12.4 Key Input Path

Straight key / paddle events bypass audio path and feed directly into
`MorseDecoder::append()` — same decoder used for both audio and key input.

---

## 13. Content Library (`libs/content/`)

**Basis:** `TextGenerators`, `english_words.h`, `abbrevs.h` from m5core2-cwtrainer
(see §20). Extended with callsign generator, expanded word list, and QSO engine.

### 13.1 Word and Group Sources

| Source              | Storage                          | Origin                          |
|---------------------|----------------------------------|---------------------------------|
| Random CW groups    | Generated at runtime             | New                             |
| Common words        | `libs/content/data/words.h`      | cwtrainer `english_words.h` + expanded |
| CW abbreviations    | `libs/content/data/abbrevs.h`    | cwtrainer `abbrevs.h` (245 entries) |
| Callsign generator  | Runtime (algorithm)              | cwtrainer `getRandomCall()` basis |
| Callsign database   | LittleFS `/content/calls.txt`    | QRQ callbase (stream-indexed)   |
| Koch character sets | Compiled-in (4 sequences)        | Native, LCWO, CW Academy, LICW  |
| User files          | LittleFS `/files/`               | Uploaded via web UI             |
| QSO phrases         | `libs/content/data/qso_pools.h`  | New (see §13.2)                 |

The word and abbreviation data arrays are compiled in as `const char*[]` arrays
in `data/` headers, exactly as in cwtrainer — no file-system access needed for
built-in content.

### 13.2 QSO Phrase Engine

Two operating modes:

**Template substitution** — fixed phrase templates with `{slot}` placeholders:

```
"NAME IS {name} {name}"
"RIG IS {rig} PWR {watt} W"
"ANT IS {ant}"
"WX {wx} {temp} DEG"
"QTH IS {qth}"
"RST {rst}"
"THX FER QSO 73 {name} DE {mycall}"
```

Random pools for each slot (`qso_pools.h`, compiled-in):

| Slot       | Example values                                    |
|-----------|---------------------------------------------------|
| `{name}`   | TOM, JIM, HANS, FRITZ, ELKE, MARIA, ...           |
| `{rig}`    | IC7300, FT991, TS590, FLEX6600, KX3, ...          |
| `{watt}`   | 5, 10, 25, 50, 100, 200, 400, 1000                |
| `{ant}`    | DIPOLE, YAGI, VERTICAL, HEXBEAM, RANDOM WIRE, EFH |
| `{wx}`     | SUNNY, CLOUDY, RAIN, SNOW, FOGGY                  |
| `{temp}`   | −10 to +35 (integer, random)                      |
| `{qth}`    | VIENNA, BERLIN, LONDON, PARIS, TOKYO, ...         |
| `{rst}`    | 559, 579, 599, 589                                |
| `{mycall}` | Preference `station.callsign`                     |

**Full QSO simulation** — chains phrases into a complete exchange for the echo
trainer. The sequence is generated once at session start:

```
CQ CQ DE {mycall} {mycall} K
→  {call} DE {mycall} GM = NAME IS {name} {name} = QTH IS {qth} =
→  RST {rst} {rst} = RIG IS {rig} PWR {watt} W = ANT IS {ant} =
→  WX {wx} {temp} DEG =
→  THX FER QSO 73 {name} DE {mycall} SK
```

The trainer presents one phrase at a time; echo trainer expects only that phrase
back (not the full QSO string). This gives realistic QSO context while keeping
the echo challenge bounded to a short, copyable phrase.

---

## 14. Training Library (`libs/training/`)

**Basis:** `MorseTrainer` from m5core2-cwtrainer, refactored for this codebase.

### 14.1 Design

The cwtrainer `MorseTrainer` state machine (Player / Echo modes, phrase callbacks,
adaptive WPM ±1 on success/error) is the starting point. The inter-phrase pause
(`AdvancePhrase` state) scales with WPM: `dot_delay_ms * 28` (~4 word-spaces),
giving a natural rhythm at any speed. Refactoring required:

- Replace `millis()` with injected `millis_fn` (to match IambicKeyer pattern)
- Replace `Arduino::String` with `std::string` throughout
- Remove `Serial.print()` debug calls (use structured logging or remove)
- Extract `char2morse()` to `libs/cw-engine/char_morse_table.h`

The resulting `training::Orchestrator` class drives both Echo and Koch trainer
modes, delegating content selection to `libs/content/` callbacks.

### 14.2 Koch Trainer

Koch trainer is a wrapper around the training orchestrator that restricts the
content callback to return only characters within the current Koch lesson set.
Lesson progress and per-character error statistics are stored in `prefs::Manager`.

Adaptive Random rules (from FSD):
- Error on character → increase selection probability for that character and
  its Koch-sequence neighbours
- First error per character per session only counts
- Probability weights reset at session start

---

## 14b. Games Library (`libs/games/`)

CW training games that combine Morse practice with arcade-style gameplay.

### 14b.1 CW Invaders

**Status:** Implemented (simulator + pocketwroom).

Space Invaders–style game: single characters scroll right-to-left across the
screen. The player morses each character with paddles to destroy it before it
reaches the left edge.

#### Files

| File | Purpose |
|------|---------|
| `libs/games/include/cw_invaders.h` | Game class: state, spawning, scoring, Koch progression |
| `libs/games/src/cw_invaders.cpp` | Implementation |
| `libs/games/library.json` | PlatformIO lib metadata (`platforms: *`) |

#### Gameplay

- Characters appear as LVGL labels at random Y on the right edge, animated
  left via `lv_anim_t` (linear scroll).
- Player morses a character → `try_match()` finds the leftmost matching
  invader, flashes it green, schedules deletion via `lv_timer` (150 ms).
- Invader reaches x ≤ −30 → `anim_done_cb` flashes red, marks `dying`.
  `tick()` deducts a life (distinguished from player kills via `hit` flag).
- 3 lives. Game over when all lives lost; "GAME OVER" + final score shown.

#### Koch Progression

- Koch order: `KMRSUAPTLOWINJEF0YV,G5/Q9ZH38B?427C1D6X` (40 chars).
- Level 1: 2 characters (K, M). Spawned uniformly from unlocked set.
- **Advance trigger:** single correct copy of the highest unlocked Koch
  character → `advance_level()`: unlock next Koch char, decrease
  `spawn_interval_ms` by 150 (min 800), decrease `scroll_duration_ms`
  by 400 (min 3000).

#### Pause / Resume

- Encoder short press toggles `set_paused(bool)`.
- Pause: deletes all running `lv_anim` on live invaders; `tick()` returns
  immediately.
- Resume: recreates anims from each invader's current X with proportional
  remaining duration; resets spawn timer.

#### UI Layout (320 × 170 pocketwroom)

- **No StatusBar** — game area starts at y = 0 for maximum vertical space.
- **Game area:** sharp-corner dark rectangle (`0x111122`), fills screen minus
  bottom strip.
- **Bottom strip:** chained colored labels using `lv_obj_align_to()`:
  Level (green) → Lives (red) → Score (yellow) → WPM (grey) → Input (blue,
  right-aligned). Labels re-chain on every HUD update, preventing overlap
  regardless of digit count.
- **Game Over:** centered label in game area shows "GAME OVER\n{score}".

#### Integration (`src/app_ui.hpp`)

- `ActiveMode::INVADERS` enum value.
- Menu entry: `{ LV_SYMBOL_SHUFFLE, "CW Invaders" }`.
- `build_invaders_screen()`: creates game area, bottom strip, instantiates
  `CwInvaders`, calls `start()`.
- `on_letter_decoded()`: INVADERS case calls `try_match()`, updates input
  label and HUD.
- `app_ui_tick()`: calls `game->tick()`, refreshes HUD, shows game-over
  label when triggered.
- Encoder short press: pause toggle. Encoder long press: back to menu.
- Encoder rotate: adjusts WPM (keyer speed, not scroll speed); WPM label
  in bottom strip updates live.

---

## 15. WiFi / Web Interface

### 15.1 Modes

- **AP mode** (default when no STA credentials): SSID `Morserino-XXXX`, no password
- **STA mode**: connects to saved network; IP shown in status bar

### 15.2 Web UI Routes

| Method | Path                  | Function                            |
|--------|-----------------------|-------------------------------------|
| GET    | `/`                   | Single-page app (served from LittleFS `/www/index.html`) |
| GET    | `/api/config`         | All preferences as JSON             |
| POST   | `/api/config`         | Write one or more preferences       |
| GET    | `/api/files`          | List user files in `/files/`        |
| POST   | `/api/files/upload`   | Multipart file upload               |
| DELETE | `/api/files/{name}`   | Delete file                         |
| GET    | `/api/files/{name}`   | Download file                       |
| POST   | `/api/ota`            | Firmware OTA upload                 |
| GET    | `/api/status`         | Runtime status (mode, WPM, battery) |

The SPA uses plain `fetch` / JSON; no external JS dependencies.

### 15.3 IP Transceiver Connectivity

WiFi is also used by `net::TransceiverTask` for:

- **UDP peer-to-peer**: timestamped UDP datagrams encoding Morse elements
- **iCW**: TCP connection to iCW server; simple timing-stream protocol
- **VBand**: TCP; protocol TBD (placeholder TCP transport)

---

## 16. Sleep and Power Management

### 16.1 Pocket: Sleep Entry

1. `prefs::Manager::commit_if_dirty()`
2. Notify active mode → stop audio, close network
3. `IAudioOutput::suspend()` — codec reset (GPIO 45 low)
4. `IDisplay::sleep()` — backlight off, display sleep command
5. `ISystemControl::set_power_rail(false)` — VEXT rail off (GPIO 42 low)
6. `ISystemControl::deep_sleep(wakeup_mask)` — ESP32-S3 deep sleep;
   wakeup sources: encoder button (GPIO 12) and paddle GPIOs

### 16.2 M5Core2: Sleep Entry

1. `prefs::Manager::commit_if_dirty()`
2. Notify active mode
3. `IAudioOutput::suspend()` — AXP192 GPIO2 low (SPK_EN off)
4. `IDisplay::sleep()`
5. `M5.Power.powerOff()` (or light sleep with touch wakeup for Core2)

### 16.3 Sleep Triggers

- Explicit "Go to Sleep" menu item
- Configurable inactivity timeout (preference, default 10 min)
- Low battery threshold (< 5 %)

### 16.4 Wakeup

Cold start on both targets. Previous mode restored from `session.last_mode`.

---

## 17. Preferences System

### 17.1 Storage

NVS via `IStorage`. Namespace scheme:
`cw.*` · `keyer.*` · `trainer.*` · `display.*` · `net.*` · `station.*` · `session.*`

### 17.2 Write Discipline

`prefs::Manager` marks preferences dirty on any set; commits to NVS at most
once per 2-second idle window, or immediately on mode exit / sleep entry.

### 17.3 Snapshots

Full preference serialization to LittleFS `/prefs/snapshot_{name}.json`.
Up to 8 snapshots. Recall atomically replaces the live preference set and commits.

---

## 18. File and Content Management

### 18.1 Upload

- **Primary**: HTTP multipart POST via browser web UI
- **Secondary**: USB serial (CDC) — raw text paste with `\x04` terminator

### 18.2 Resume Position

Playback position stored in NVS as `content.{filename_hash}.position` (byte offset).
Updated on explicit mode exit only.

---

## 19. External Interfaces

### 19.1 USB Serial (CDC)

Available on ESP32-S3 via native USB (`ARDUINO_USB_CDC_ON_BOOT=1`).
Line-oriented text protocol (`\n` terminated):

| Command          | Effect                          |
|------------------|---------------------------------|
| `SET key=value`  | Write preference                |
| `GET key`        | Read preference                 |
| `MODE name`      | Switch to named mode            |
| `MEM n text`     | Write memory keyer slot n       |
| `STATUS`         | Emit current status JSON        |

### 19.2 Bluetooth HID Keyboard

NimBLE-Arduino BLE HID. Sends decoded/generated characters as HID keycodes.
Toggle via preference `ext.bt_hid_enabled`.

---

## 20. Library Adoption Plan (from m5core2-cwtrainer)

### 20.1 Adopt Without Changes

These libraries are fully platform-agnostic and can be copied directly:

| Library       | Action                            | Notes                                   |
|--------------|-----------------------------------|-----------------------------------------|
| `IambicKeyer` | Copy to `libs/cw-engine/src/`    | Inject millis via constructor — already done |
| `SymbolPlayer`| Copy to `libs/cw-engine/src/`    | Same — millis injected                  |
| `PaddleCtl`   | Copy to `libs/cw-engine/src/`    | GPIO read is caller's responsibility    |
| `common.h`    | Copy to `libs/cw-engine/include/`| Only defines `millis_fun_ptr` typedef   |
| `english_words.h` | Copy to `libs/content/data/` | Pure `const char*[]` data               |
| `abbrevs.h`   | Copy to `libs/content/data/`     | Pure `const char*[]` data (245 entries) |

### 20.2 Adopt With Minor Changes (< 1 day each)

| Library       | Required Changes                                                     |
|--------------|----------------------------------------------------------------------|
| `StraightKeyer` | Inject `read_key_fn` (replaces `digitalRead(PADDLE_LEFT)`) and `millis_fn`; remove `#include <Arduino.h>` |
| `MorseDecoder`  | Replace `Arduino::String` with `std::string`; update callback typedef; remove `Arduino.h` |
| `Goertzel`      | Refactor `checkInput()` to accept `const int16_t* samples, size_t count`; remove `analogRead()`; keep adaptive threshold algorithm |
| `TextGenerators`| Replace `String` with `std::string`; replace `random()` with injected `std::mt19937&`; remove `Arduino.h` |

### 20.3 Adopt With Moderate Changes (reference design, partial rewrite)

| Library       | Required Changes                                                     |
|--------------|----------------------------------------------------------------------|
| `MorseTrainer`  | Rename to `training::Orchestrator`; inject `millis_fn`; replace `String` with `std::string`; remove `Serial.print`; extract `char2morse` to `cw-engine` |

### 20.4 Algorithm / Pattern Reference Only

| Library        | What to take                                        |
|---------------|-----------------------------------------------------|
| `MorsePlayer`  | `char2morse()` lookup table only (consolidate into `cw-engine/char_morse_table`) |
| `TextSDReader` | Random-line-seek algorithm pattern (re-implement against `IStorage`) |
| `HAL_m5core2`  | `init()` pattern: M5Unified init + AXP192 SPK_EN sequence |

### 20.5 Key Design Rule

The `millis_fn` injection pattern established in `IambicKeyer`/`SymbolPlayer`
is the **required pattern** for all timing-dependent classes in this project.
No class in `libs/` may call `millis()` or any equivalent directly.

---

## 21. Non-Functional Requirements

- Keyer task latency: < 1 ms from paddle edge to element start
- UI frame rate: ≥ 15 fps during active CW output
- NVS commit: debounced, ≤ once per 2 s idle or on mode exit
- Source file size: ≤ 300 lines
- Stack sizes, queue depths, task priorities: defined in `libs/hal/<platform>/task_config.h`
- All libraries in `libs/` except `hal/<platform>/`: compile and pass tests on
  native target without any ESP32 or Arduino SDK headers

---

*Sections marked "TBD" are placeholders pending protocol specification.*
*This document is a living specification; update alongside implementation.*
