# Morserino-32-NG v1.x – Functional Specification Document (FSD)

## 1. Purpose and Scope

This document specifies the **functional behavior** of the Morserino-32 next generation,
independent of any specific hardware implementation.  
It is intended to support a **complete firmware rewrite** and **agent-based development**, and
therefore focuses exclusively on **user-visible features, modes, workflows, and behaviors**.

Out of scope:
- Hardware connectors, pin assignments, voltages
- Physical controls beyond their logical function
- Legacy implementation details
- MCU-, RTOS-, or library-specific assumptions

---

## 2. Conceptual System Model

The system is defined as a set of cooperating *functional agents*:

- **UI & Navigation Agent**
- **CW Timing & Keying Agent**
- **CW Generation Agent**
- **Training Orchestration Agent**
- **Decoding Agent**
- **Transceiver & Networking Agent**
- **File & Content Agent**
- **Preferences & Snapshot Agent**
- **External Interface Agent (USB / Bluetooth / Network)**

Each agent exposes deterministic, testable behavior and communicates through well-defined events
(e.g. *start generation*, *character decoded*, *word completed*, *snapshot recalled*).

---

## 3. Global Interaction Principles

### 3.1 Modes

The system operates in mutually exclusive **modes**, selected from a top-level menu:

- CW Keyer
- CW Generator
- Echo Trainer
- Koch Trainer
- Transceiver
- CW Decoder
- WiFi / Network Functions
- Preferences
- Sleep

Switching modes preserves global preferences unless explicitly overridden.

### 3.2 CW Timing Model

- Base speed defined in **Words Per Minute (WPM)** using the PARIS standard
- Effective speed may differ due to spacing settings
- Character spacing and word spacing are independently configurable
- Timing applies consistently across:
  - Audio output
  - Keyed output
  - Generated content
  - Decoding expectations

### 3.3 Display and Typography

- CW text areas use the **Intel One Mono** typeface
  ([github.com/intel/intel-one-mono](https://github.com/intel/intel-one-mono))
- Intel One Mono is an open-source monospaced font (OFL-1.1 license) designed
  for developer readability and accessibility, with input from low-vision users
- Monospaced rendering is required for CW text output so that character alignment
  is predictable and Morse symbols are easy to follow
- The font supports 200+ Latin-script languages, ensuring correct rendering of
  callsigns and text containing accented characters or umlauts
- Available weights: Light, Regular, Medium, Bold (each with italic variant);
  the implementation selects appropriate weights for different text sizes
- A **Text Size** preference (Normal / Large) allows users to increase CW text
  area font size for improved readability — important for operators with reduced
  eyesight
- Menus, status bar, and other chrome use LVGL's built-in Montserrat typeface,
  which includes the icon glyphs required by the UI
- CW text areas scroll automatically so that newly received or sent text is
  always visible, even in long sessions

---

## 4. Functional Specifications by Mode

## 4.1 CW Keyer

### Functional Description
Automatic Morse keyer supporting multiple keying paradigms.

### Supported Keying Modes
- Iambic A
- Iambic B (with configurable element latch timing)
- Ultimatic
- Non‑Squeeze (single‑lever emulation)
- Straight Key

### Functional Requirements
- Paddle polarity configurable
- External key polarity configurable
- Adjustable keyer latency after element generation
- Optional automatic minimum inter‑character spacing
- Straight‑key mode bypasses automatic element generation

### Memory Keyer
- multiple persistent memories
- Each memory stores enough characters for typical QSO strings
- Supports:
  - Letters, numbers, punctuation
  - Pro‑signs
  - Explicit pauses
  - speed changes
- Memories shall have option to repeat continuously until interrupted

---

## 4.2 CW Generator

### Functional Description
Produces Morse code content for listening and training.

### Generation Sources
- Random character groups
- CW abbreviations / Q‑codes
- Common English words (top 5,000)
- Simulated call signs, resp. based upon callsign list (callbase) from the QRQ utility
- Mixed mode (random selection of above)
- Text file playback
- dynamically generate typical QSO phrases, like "name is xxx", "rig is ic7100", "pwr 100w", "thx fer qso", "my qsl sure via buro", "wx cloudy 5deg" etc

### Generation Controls
- Start / stop / pause (via encoder short press or aux button)
- Inter-phrase pause is WPM-dependent (~4 word-spaces), giving a natural
  rhythm that adapts to the operator's speed
- Optional word‑by‑word stepping
- Optional word repetition
- Maximum number of generated items

### Spacing Control
- Inter‑character spacing (3–6+ dit lengths)
- Inter‑word spacing (6–45 dit lengths)
- Enforced rule: inter‑word spacing ≥ inter‑character spacing + 4 dits

### File Playback Features
- Resume from last position
- Loop at end of file
- Optional random word skipping
- Supports:
  - Pro‑signs
  - Pauses
  - Tone shifts
  - Line comments
- support multiple files

---

## 4.3 Echo Trainer

### Functional Description
Closed‑loop training: system sends a prompt, user repeats it.

### Prompt Sources
Same as CW Generator (except random groups in Koch context).

### Prompt Presentation Modes
- Audio only
- Display only
- Audio + display

### Behavioral Rules
- Prompt is hidden by default
- User input compared symbol‑by‑symbol
- Incorrect or late input triggers:
  - Error indication
  - Optional repetition
- Correct input advances sequence

### Advanced Features
- Adaptive speed control (+1 WPM on success, −1 WPM on error)
- Error reset via:
  - ≥8 dits
  - “eeee” sequence
- Statistics shown after configured word count

---

## 4.4 Koch Trainer

### Functional Description
Implements the Koch method for incremental Morse learning.

### Core Principles
- Introduces one new character per lesson
- Training uses **only learned characters**
- Emphasizes rhythm over counting

### Character Sequences
- Native Morserino sequence
- LCWO sequence
- CW Academy sequence
- LICW Carousel sequence
- Custom character set via uploaded file

### Sub‑Modes
- Learn New Character
- CW Generator (restricted set)
- Echo Trainer (restricted set)
- Adaptive Random (probability‑weighted error focus)

### Adaptive Random Rules
- Error increases selection probability
- Neighbor characters also weighted
- First error only is analyzed
- Probabilities reset per session

---

## 4.5 Transceiver

### Functional Description
Bidirectional Morse communication with simultaneous sending and receiving.

### Supported Transceiver Types (Functional View)
- Peer‑to‑peer local wireless, e.g. espnow, LoRa, bluetooth, etc.
- Network‑based (IP)
- External / Internet CW protocols (e.g. iCW, VBand)

### Common Behavior
- Local keying transmitted
- Incoming Morse decoded and displayed
- Separate speed reporting for sent vs received code
- Graceful handling of connectivity loss

---

## 4.6 CW Decoder

### Functional Description
Decodes Morse code into text.

### Input Sources
- Key input
- Audio input

### Output
- Displayed decoded text
- Optional forwarding to:
  - USB serial
  - Bluetooth keyboard

### Behavior
- Real‑time decoding
- Error‑tolerant timing
- Visual activity indication during signal detection

---

## 5. Preferences & Snapshots

### Preferences
Configurable parameters include:
- Keyer behavior (iambic mode, speed)
- CW timing & spacing (frequency, volume)
- Generator limits
- Trainer behavior (echo repeats, QSO depth)
- Display preferences (text size: Normal / Large)
- Network options
- Output routing

Preferences persist across power cycles.

### Snapshots
- Store complete preference state
- Recall or delete snapshots
- Accessible from preference menu
- Used to quickly switch operating profiles

---

## 6. File & Content Management

### Text Files
- ASCII text only
- Unsupported characters ignored
- Supports:
  - Pro‑sign syntax
  - Pauses
  - Tone changes
  - Comments

### File Lifecycle
- Upload
- Overwrite existing file
- Resume playback position
- Explicit exit required to persist position

---

## 7. External Interfaces

### USB Serial
- Outputs generated and/or decoded characters
- Accepts control commands
- Supports memory programming and configuration

### Bluetooth Keyboard
- Acts as HID keyboard
- Sends decoded or generated characters as keystrokes
- Intended for integration with external CW software

### Browser‑Based Interfaces
- Configuration
- File upload
- Firmware update (functional requirement only)

---

## 8. Non‑Functional Requirements

- Low‑latency keying response
- Deterministic timing
- Mode isolation (no cross‑mode leakage)
- Graceful failure of networking features
- Consistent behavior across device variants

---

## 9. Acceptance Criteria Summary

- All modes reachable and exit cleanly
- Preferences persist and restore correctly
- CW timing matches configured parameters
- Training logic behaves deterministically
- Decoder and generator interoperate correctly
- External outputs mirror internal state accurately

---

**End of Functional Specification**
