# Iambic Keyer — Architecture and Detailed Walkthrough

This document describes the complete iambic keyer pipeline as implemented in
the Morserino-32 NG firmware.  It covers every class, every state, every
transition, and every design decision — including the subtle ones that took
several iterations to get right.

---

## 1. The Three Layers

The keyer is a three-layer pipeline.  Data flows **down** (paddle hardware to
audio output); timing decisions flow **up** (SymbolPlayer tells IambicKeyer
when it's ready for the next element).

```
  +-----------+
  | PaddleCtl |   raw dit/dah booleans  -->  combined LeverState
  +-----------+
       |
       | setLeverState(LeverState)
       v
  +-------------+
  | IambicKeyer |   LeverState + keyer logic  -->  playDot() / playDash()
  +-------------+
       |
       | playDot() / playDash()
       v
  +--------------+
  | SymbolPlayer |   element timing  -->  PlayState callbacks (tone on/off)
  +--------------+
       |
       | state_changed_cb(PlayState)
       v
    [ audio output / sidetone ]
```

All three are ticked from the same loop (the CW task on ESP32, or the main
loop on the simulator), running at approximately 1 kHz.

---

## 2. PaddleCtl — Paddle Input to Lever State

**Files:** `paddle_ctl.h`, `paddle_ctl.cpp`

### Purpose

Converts two independent boolean inputs (dit pressed, dah pressed) into a
single `LeverState` enum that the keyer can reason about.

### LeverState enum

| Value            | Meaning                                |
|------------------|----------------------------------------|
| `LEVER_UNSET`    | Neither paddle is pressed              |
| `LEVER_DOT`      | Only dit paddle pressed                |
| `LEVER_DASH`     | Only dah paddle pressed                |
| `LEVER_DOT_DASH` | Both pressed; dit was pressed first    |
| `LEVER_DASH_DOT` | Both pressed; dah was pressed first    |

The two squeeze states (`DOT_DASH` and `DASH_DOT`) encode **which paddle was
already active** when the second one was pressed.  This is important for
determining the first element of an alternation sequence (see Section 4).

### Input path

1. **Hardware events** (touch sensor threshold crossings, external key
   interrupts, MIDI note-on/off, or SDL keyboard events) call
   `setDotPushed(true/false)` and `setDashPushed(true/false)`.  These just
   store the raw boolean — no processing happens here.

2. **`tick()`** is called ~1000 times/second.  It:
   - Copies `raw` to `stable` for each lever (immediate, no debounce).
   - Calls `combine(dit_stable, dah_stable, current_lever_state)` to compute
     the new `LeverState`.
   - If the state changed, fires the callback (`state_changed_cb_`) which
     calls `IambicKeyer::setLeverState()`.

### Why no debounce?

PaddleCtl historically had a press-delay debounce (press was delayed by N ms,
release was immediate).  This was removed because:

- **Touch sensors don't bounce.**  The ESP32's `touchRead()` is already
  filtered at the hardware level.
- **External keys (MIDI, GPIO) are sampled at 1 kHz**, which is slow enough
  that mechanical contact bounce (typically < 5 ms) resolves between samples.
- **The iambic keyer state machine is the real "debounce".**  It only makes
  decisions at element boundaries (when `symbol_player.ready()` returns true),
  not on every lever transition.  Transient glitches between boundaries are
  harmless — only the lever state at the moment of the decision matters.
- **Press debounce was actively harmful.**  A brief opposite-paddle tap
  shorter than the debounce window was completely invisible to the keyer,
  causing missed elements in letters like N (dah dit), G (dah dah dit), and
  F (dit dit dah dit).

### The `combine()` function

```
combine(dit, dah, prev_lever_state) -> LeverState
```

- Both pressed: returns `DOT_DASH` or `DASH_DOT` depending on which paddle
  was already active (`prev`).  If already in a squeeze, keeps the same
  direction.  If both pressed from idle (rare), defaults to `DOT_DASH`.
- One pressed: returns `DOT` or `DASH`.
- Neither: returns `UNSET`.

---

## 3. SymbolPlayer — Element Timing Engine

**Files:** `symbol_player.h`, `symbol_player.cpp`

### Purpose

Plays one Morse element (dit or dah) with correct timing: tone-on for the
element duration, then silence for 1 dit-unit (the inter-element gap).
Reports state changes via a callback so the audio system can turn the sidetone
on and off.

### PlayState lifecycle

When `playDot()` is called:

```
STOPPED --> DOT_ON --> DOT_OFF --> STOPPED
             |          |
          1 unit     1 unit
        (tone on)  (silence)
```

When `playDash()` is called:

```
STOPPED --> DASH_ON --> DASH_OFF --> STOPPED
              |           |
           3 units     1 unit
         (tone on)   (silence)
```

Each state has a duration.  `tick()` checks if the current state's duration
has elapsed (`playStateFinished()`), and if so, transitions to the next state
via `setPlayState()`.  The callback fires on every transition.

### Key methods

- **`ready()`** — Returns true when `play_state` is `STOPPED` or `UNSET`.
  This is the signal to `IambicKeyer::tick()` that it can queue the next
  element.  Note: `ready()` is false during BOTH the on-phase and the
  off-phase (inter-element gap).  The keyer cannot start a new element until
  the gap has finished.

- **`isSounding()`** — Returns true only during `DOT_ON` or `DASH_ON` (the
  tone is actually playing).  This is a narrower check than `!ready()`.
  Used by the keyer's `lever_upgrade` mechanism to distinguish "tone is
  sounding" from "we're in the silent gap."  This distinction matters:
  latching opposite-paddle presses during the gap causes false elements from
  finger repositioning.

- **`isPastElementThreshold(dit_pct, dah_pct)`** — Returns true if the
  current element has progressed past the given percentage of its duration.
  Used for enhanced Curtis B timing.

### Duration unit

`duration_unit` is the dit duration in milliseconds.  For standard PARIS
timing at W WPM: `duration_unit = 60000 / (50 * W)`.  At 20 WPM this is
60 ms; at 30 WPM it's 40 ms.

---

## 4. IambicKeyer — The State Machine

**Files:** `iambic_keyer.h`, `iambic_keyer.cpp`

This is the brain of the keyer.  It decides **which element to play next**
based on the current lever state, the previous keyer state, and a latched
"lever upgrade" flag.

### KeyerState enum

| Value                       | Meaning                                    |
|-----------------------------|--------------------------------------------|
| `KEYER_STATE_UNSET`         | Initial state, never been used             |
| `KEYER_STATE_STOPPED`       | Idle, no element playing or queued         |
| `KEYER_STATE_DOT`           | Playing a dit from single dit paddle       |
| `KEYER_STATE_DASH`          | Playing a dah from single dah paddle       |
| `KEYER_STATE_ALTERNATING_DOT`  | Playing a dit as part of alternation    |
| `KEYER_STATE_ALTERNATING_DASH` | Playing a dah as part of alternation    |

The `ALTERNATING_*` states are distinct from `DOT`/`DASH` because their
"what comes next" logic is different (see Section 4.3).

### 4.1. The tick() loop

Called ~1000 times/second.  Does three things:

```cpp
void IambicKeyer::tick()
{
    symbol_player.tick();           // 1. advance element timing
    if (!symbol_player.ready())
        return;                     // 2. not time for a decision yet

    // 3. decide next element
    KeyerState next = nextKeyerState();

    if (!mode_a)
        prev_lever_state = lever_state;   // Curtis B memory

    if (keyer_state == next && next == KEYER_STATE_STOPPED)
        return;                     // already stopped, nothing to do

    keyer_state = next;

    if (next is DOT or ALTERNATING_DOT)   symbol_player.playDot();
    if (next is DASH or ALTERNATING_DASH) symbol_player.playDash();
}
```

**The critical insight:** The keyer only makes decisions when
`symbol_player.ready()` returns true — i.e., when the previous element AND
its inter-element gap have both finished.  Between decisions, `lever_state`
can bounce around freely; only its value at decision time matters.

### 4.2. The lever_upgrade latch

`lever_upgrade` is a boolean latch that captures "the operator indicated they
want the opposite element."  It is set in `setLeverState()` and consumed in
`nextKeyerState()`.

#### When lever_upgrade is set

Two triggers:

1. **Squeeze detected (always):** If the new `lever_state` is `LEVER_DOT_DASH`
   or `LEVER_DASH_DOT`, set `lever_upgrade = true`.  This is the classic
   iambic squeeze — both paddles held simultaneously.

2. **Opposite paddle pressed during tone-on (non-alternating states only):**
   If `symbol_player.isSounding()` is true AND we're in `KEYER_STATE_DOT` or
   `KEYER_STATE_DASH` (not ALTERNATING), AND the new lever_state is the
   opposite single paddle (`LEVER_DOT` during a dah, `LEVER_DASH` during a
   dit), set `lever_upgrade = true`.

   This catches the common sending pattern for letters like **N** (dah dit):
   the operator presses dah, releases it, then presses dit while the dah tone
   is still sounding — but there's no simultaneous squeeze moment.  Without
   this latch, if the dit is released before the dah+gap finishes, the keyer
   would see `LEVER_UNSET` at decision time and stop.

#### Why only during isSounding(), not !ready()?

The element lifecycle has two phases: tone-on (`DOT_ON`/`DASH_ON`) and
inter-element gap (`DOT_OFF`/`DASH_OFF`).  `isSounding()` is true only during
tone-on.  `!ready()` is true during both.

If we latched during the gap, finger repositioning (briefly touching the
opposite paddle while moving to the next one) would set `lever_upgrade` and
produce unwanted extra elements.  For example, F (dit dit dah dit) would gain
an extra dah because the operator's dah finger grazes the paddle during the
gap after the final dit.

#### Why only for DOT/DASH, not ALTERNATING states?

`ALTERNATING_DOT` and `ALTERNATING_DASH` don't need the latch because their
`nextKeyerState()` logic examines the live `lever_state` directly and handles
all paddle combinations.  Setting `lever_upgrade` during ALTERNATING states
caused it to "leak" through subsequent state transitions and produce extra
elements.  For example, Q (dah dah dit dah):

1. Elements 1-2 are dahs.  Element 3 is ALTERNATING_DOT.
2. During element 3, the operator presses dah for the final element.
3. If `lever_upgrade` were set, it would persist through the
   ALTERNATING_DOT -> DASH transition and fire again when that dah ended,
   producing a 5th element.

To prevent any stale `lever_upgrade` from leaking, both ALTERNATING cases in
`nextKeyerState()` explicitly clear it:

```cpp
case KEYER_STATE_ALTERNATING_DOT:
    lever_upgrade = false;
    ...
case KEYER_STATE_ALTERNATING_DASH:
    lever_upgrade = false;
    ...
```

#### When lever_upgrade is consumed

In `nextKeyerState()`, when `keyer_state` is `DOT` or `DASH` and
`lever_upgrade` is true:

```cpp
if ((keyer_state == DOT || keyer_state == DASH) && lever_upgrade)
{
    lever_upgrade = false;
    return (keyer_state == DOT) ? KEYER_STATE_DASH : KEYER_STATE_DOT;
}
```

This takes priority over the live `lever_state` check below it.  So even if
the operator has already released the opposite paddle (`lever_state == UNSET`),
the latched upgrade ensures the alternate element plays.

**Important:** the upgrade transitions to a plain `DOT`/`DASH` state, not
`ALTERNATING_DOT`/`ALTERNATING_DASH`.  ALTERNATING states have special
squeeze-exit logic ("same paddle held = stop alternation") which is wrong for
lever_upgrade scenarios — the operator tapped the opposite paddle once and
expects normal keying to resume after that element.  Using plain states means
the *next* boundary decision simply reads the live `lever_state` with no
squeeze assumptions.

### 4.3. nextKeyerState() — Full State Transition Table

This function is called once per element boundary.  It returns the next
`KeyerState` based on the current state and the current `lever_state` (plus
`lever_upgrade` and `prev_lever_state`).

#### From UNSET / STOPPED / DOT / DASH

First check: if `lever_upgrade` and we're in DOT or DASH, switch to the
opposite plain state (see 4.2).

Otherwise, read live `lever_state`:

| lever_state    | next keyer_state      | Notes                        |
|----------------|-----------------------|------------------------------|
| UNSET          | STOPPED               | Nothing pressed              |
| DOT            | DOT                   | Dit paddle only              |
| DASH           | DASH                  | Dah paddle only              |
| DOT_DASH       | ALTERNATING_DASH      | Squeeze, dit was first       |
| DASH_DOT       | ALTERNATING_DOT       | Squeeze, dah was first       |

When entering ALTERNATING from a squeeze, `prev_lever_state` is set to record
that we entered via a squeeze (used for Curtis B "one more element" logic).

#### From ALTERNATING_DOT (just played a dit in alternation)

`lever_upgrade` is cleared first.

| lever_state     | next keyer_state       | Notes                         |
|-----------------|------------------------|-------------------------------|
| UNSET           | ALTERNATING_DASH *or* STOPPED | Curtis B: one more if prev was squeeze |
| DOT             | STOPPED                | Same paddle held, opposite released — stop |
| DASH            | DASH                   | Opposite paddle only — exit alternation |
| DOT_DASH / DASH_DOT | ALTERNATING_DASH  | Still squeezing — continue    |

The `LEVER_DOT -> STOPPED` rule is critical for letters like **C** (dah dit
dah dit).  After the final dit, the operator releases dah but keeps dit held.
Without this rule, the keyer would see "dit paddle held" and play another dit,
turning C into something longer.

#### From ALTERNATING_DASH (just played a dah in alternation)

Mirror of ALTERNATING_DOT:

| lever_state     | next keyer_state       | Notes                         |
|-----------------|------------------------|-------------------------------|
| UNSET           | ALTERNATING_DOT *or* STOPPED | Curtis B: one more if prev was squeeze |
| DASH            | STOPPED                | Same paddle held — stop       |
| DOT             | DOT                    | Opposite paddle only — exit alternation |
| DOT_DASH / DASH_DOT | ALTERNATING_DOT   | Still squeezing — continue    |

### 4.4. Curtis B "One More Element"

In Curtis B mode (the default; `mode_a = false`), when both paddles are
released during alternation, the keyer plays **one more element** of the
opposite type.  This is the defining characteristic of Curtis B.

Implementation:

- `prev_lever_state` is set when entering alternation via a squeeze
  (`DOT_DASH` or `DASH_DOT`).
- On each `tick()`, after `nextKeyerState()`, Curtis B mode snapshots
  `prev_lever_state = lever_state`.
- In the ALTERNATING cases, when `lever_state == UNSET` and
  `prev_lever_state` was a squeeze state, the keyer produces one more
  alternating element and then clears `prev_lever_state`.

This means: if the operator squeezes, then releases both paddles mid-
alternation, the keyer finishes the current element plus one more, then stops.

In Iambic A mode (`mode_a = true`), the `prev_lever_state` snapshot is
skipped, so `LEVER_UNSET` always produces `STOPPED` immediately.

---

## 5. Putting It All Together — Worked Examples

In these examples, "decision point" means `symbol_player.ready()` returned
true and `nextKeyerState()` is called.

### Example: C = dah dit dah dit (-.-.))

1. Operator squeezes both paddles (dah first).
   - PaddleCtl: `LEVER_DASH` -> `LEVER_DASH_DOT`
   - setLeverState: `lever_upgrade = true` (squeeze)

2. Decision point (idle): `keyer_state=UNSET`, `lever_state=DASH_DOT`
   - `lever_upgrade` not checked for UNSET.
   - `DASH_DOT` -> `ALTERNATING_DOT`.  `prev_lever_state = DASH_DOT`.
   - BUT: `lever_upgrade` consumed? No — it's true, but we entered via the
     `switch(lever_state)` path, not the `lever_upgrade` path.
   - Actually: UNSET falls through to the switch. `DASH_DOT` -> ALTERNATING_DOT.
   - Plays dah (ALTERNATING has "opposite" naming — ALTERNATING_DOT means
     "next is dit but we just entered, so play the *first* element").

   Wait — let me be precise.  `KEYER_STATE_ALTERNATING_DOT` means "play a
   dit".  But we entered from UNSET with DASH_DOT, which returns
   `ALTERNATING_DOT`.  The tick() function then calls `playDot()`.  So the
   first element is a **dit**, not a dah.

   Actually no.  Let's re-read: `LEVER_DASH_DOT` means "dah was first, then
   dit".  The keyer returns `ALTERNATING_DOT` — "we're alternating, play a
   dot."  So first element is dit.  But C starts with dah!

   Let me re-trace.  The operator presses dah first (alone), so:
   - PaddleCtl: `LEVER_DASH`.
   - Decision point: `LEVER_DASH` -> `KEYER_STATE_DASH`.  Plays dah.
   - During dah, operator presses dit -> `LEVER_DASH_DOT` -> `lever_upgrade = true`.
   - Dah ends.  Decision: keyer_state=DASH, lever_upgrade=true ->
     `ALTERNATING_DOT`.  Plays dit.
   - Dit ends.  ALTERNATING_DOT, lever_state = DOT_DASH (still squeezing) ->
     `ALTERNATING_DASH`.  Plays dah.
   - Operator releases dah during this dit, keeps dit held.
   - Dah ends.  ALTERNATING_DASH, lever_state = DOT ->
     `KEYER_STATE_DOT`.  Plays dit.
   - Wait no: ALTERNATING_DASH + LEVER_DOT -> returns `KEYER_STATE_DOT`.
   - Dit ends.  KEYER_STATE_DOT, lever_state = DOT or UNSET.  If still held:
     another dit.  If released: STOPPED.

   Hmm, that would give: dah dit dah dit = C.  But the last dit plays as
   `KEYER_STATE_DOT`, not `ALTERNATING_DOT`.

   Actually, let me re-check.  After the 3rd element (dah, ALTERNATING_DASH),
   the operator has released dah and holds dit.  lever_state = LEVER_DOT.

   ALTERNATING_DASH + LEVER_DOT -> `KEYER_STATE_DOT` (line 106-107).

   So 4th element is DOT (plain, not alternating).  Plays dit.
   Then dit ends: KEYER_STATE_DOT + LEVER_DOT -> KEYER_STATE_DOT (another dit!).
   This would make C into dah dit dah dit dit...

   The fix: the operator must release the dit paddle before/during the 4th dit.
   Then: KEYER_STATE_DOT + LEVER_UNSET -> STOPPED.  Four elements total: C.

   This matches how operators actually send C: squeeze both paddles, release
   dah during element 3 or 4, release dit to stop.

### Example: N = dah dit (-.)

1. Operator presses dah paddle only.
   - PaddleCtl: `LEVER_DASH`.
   - Decision (idle): `KEYER_STATE_DASH`.  Plays dah.

2. During the dah tone, operator releases dah and presses dit.
   - PaddleCtl: `LEVER_DASH` -> `LEVER_UNSET` -> `LEVER_DOT`.
   - setLeverState: when `LEVER_DOT` arrives, `isSounding()` is true,
     `keyer_state == DASH`, so `lever_upgrade = true`.

3. Operator releases dit.  `lever_state = LEVER_UNSET`.

4. Dah + gap finishes.  Decision: `keyer_state=DASH`, `lever_upgrade=true`.
   - Consumes upgrade: `lever_upgrade=false`, returns `KEYER_STATE_DOT` (plain).
   - Plays dit.

5. Dit + gap finishes.  Decision: `KEYER_STATE_DOT`, `lever_state=UNSET`.
   - Plain DOT + UNSET -> `STOPPED`.

Result: dah dit = N.

### Example: F = dit dit dah dit (..-.)

1. Tap dit.  `LEVER_DOT` -> `KEYER_STATE_DOT`.  Plays dit.
2. Release, tap dit again.  `DOT` -> `KEYER_STATE_DOT`.  Plays dit.
3. During 2nd dit, squeeze dah.  `LEVER_DOT_DASH` -> `lever_upgrade = true`.
4. 2nd dit ends.  `DOT` + `lever_upgrade` -> `KEYER_STATE_DASH` (plain).  Plays dah.
5. During dah, operator releases dah.  `lever_state = LEVER_DOT`.
6. Dah ends.  `DASH` + `LEVER_DOT` -> `KEYER_STATE_DOT`.  Plays dit.
7. Dit ends.  `DOT` + `LEVER_UNSET` (released) -> `STOPPED`.

Result: dit dit dah dit = F.

### Example: Q = dah dah dit dah (--.-)

1. Hold dah.  `LEVER_DASH` -> `KEYER_STATE_DASH`.  Plays dah.
2. Still holding dah.  Decision: `DASH` + `LEVER_DASH` -> `KEYER_STATE_DASH`.
   Plays dah.
3. During 2nd dah, squeeze dit.  `LEVER_DASH_DOT` -> `lever_upgrade = true`.
4. Release dit, keep dah.  `lever_state = LEVER_DASH`.
5. 2nd dah ends.  `DASH` + `lever_upgrade` -> `KEYER_STATE_DOT` (plain).  Plays dit.
   `lever_upgrade = false`.
6. During dit, dah is still held.  `lever_state = LEVER_DASH`.
   Since `keyer_state == KEYER_STATE_DOT` (plain), the `isSounding()` latch
   CAN fire if dah is pressed during the dit tone — but dah was already held,
   so `setLeverState` sees no change and doesn't set `lever_upgrade`.
7. Dit ends.  `KEYER_STATE_DOT` + `LEVER_DASH` -> `KEYER_STATE_DASH`.
   Plays dah.
8. Release dah.  Dah ends.  `DASH` + `LEVER_UNSET` -> `STOPPED`.

Result: dah dah dit dah = Q.

---

## 6. Known Subtleties and Design Decisions

### 6.1. Why ALTERNATING states exist

One might ask: why not just use DOT and DASH for everything, relying on
`lever_upgrade` to drive alternation?

The answer is the "stop on same paddle" rule.  When the operator is in the
middle of a sustained squeeze and releases the opposite paddle (keeping the
original), they want to STOP — not continue generating elements of the held
paddle.

For plain DOT/DASH, holding the paddle means "keep generating this element."
For ALTERNATING, holding the *same* paddle means "I'm done alternating."
These are opposite behaviors, so they need separate states.

**Important:** `lever_upgrade` transitions to plain DOT/DASH, NOT to
ALTERNATING states.  Only actual squeezes (LEVER_DOT_DASH / LEVER_DASH_DOT
present at an element boundary) enter ALTERNATING.  This prevents the
"same paddle = stop" logic from swallowing a held paddle after a brief
opposite tap (e.g., holding dah, tapping dit for N — the dah must continue
after the dit, not stop).

### 6.2. Why lever_upgrade is not set during ALTERNATING states

ALTERNATING states examine live `lever_state` at each decision point.  They
don't need latching because:

- If the operator squeezes during an alternation, `lever_state` will be
  `DOT_DASH` or `DASH_DOT` at decision time -> continue alternating.
- If the operator transitions to the opposite paddle, `lever_state` will be
  `DOT` or `DASH` -> transition to that state.
- If the operator releases everything, `lever_state` will be `UNSET` ->
  Curtis B "one more" or stop.

Setting `lever_upgrade` during ALTERNATING states was found to cause extra
elements because the latch persists across the transition back to plain
DOT/DASH and then fires again.

### 6.3. Why there's no debounce

See Section 2 ("Why no debounce?").  The tl;dr: the keyer state machine only
makes decisions at element boundaries, so transient glitches between
boundaries don't matter.  Press debounce was actively dropping brief taps.

### 6.4. Curtis B threshold (curtisb_dit_pct_ / curtisb_dah_pct_)

These fields exist but are not currently used in the keyer logic.
`isPastElementThreshold()` is available in SymbolPlayer and could be
integrated into `setLeverState()` or `tick()` to implement enhanced Curtis B
timing, where the squeeze/latch is only accepted after a certain percentage
of the current element has played.  The original Morserino defaults are 75%
for dit and 45% for dah.

### 6.5. Release compensation (release_comp_ms)

Stored in SymbolPlayer but not currently applied to timing.  Intended to
compensate for the mechanical delay of releasing a paddle lever — the element
sounds slightly longer than intended because the contact remains closed during
the operator's release motion.  Subtracting a few ms from the element duration
would compensate.

---

## 7. Data Flow Summary

```
Hardware (touch/key/MIDI)
    |
    |  setDotPushed(bool) / setDashPushed(bool)
    v
PaddleCtl::tick()
    |
    |  combine(dit, dah, prev) -> LeverState
    |  callback if changed
    v
IambicKeyer::setLeverState(LeverState)
    |
    |  May set lever_upgrade = true (squeeze, or opposite paddle during tone-on)
    |  Stores lever_state
    |
    v
IambicKeyer::tick()   [called ~1000/s]
    |
    |  1. symbol_player.tick()  — advances PlayState timing
    |  2. if !symbol_player.ready() -> return  (element still playing)
    |  3. nextKeyerState()  — decides next element
    |  4. playDot() or playDash()
    v
SymbolPlayer::playDot()/playDash()
    |
    |  Sets play_state to DOT_ON or DASH_ON
    |  Fires state_changed_cb(DOT_ON / DASH_ON)
    v
Audio callback turns sidetone ON
    ...time passes (1 or 3 units)...
SymbolPlayer::tick() transitions to DOT_OFF / DASH_OFF
    |
    |  Fires state_changed_cb(DOT_OFF / DASH_OFF)
    v
Audio callback turns sidetone OFF
    ...time passes (1 unit, inter-element gap)...
SymbolPlayer::tick() transitions to STOPPED
    |
    |  symbol_player.ready() now returns true
    v
IambicKeyer::tick() can make next decision
```

---

## 8. File Reference

| File | Class | Role |
|------|-------|------|
| `libs/cw-engine/include/paddle_ctl.h` | PaddleCtl | Paddle input -> LeverState |
| `libs/cw-engine/src/paddle_ctl.cpp` | | |
| `libs/cw-engine/include/iambic_keyer.h` | IambicKeyer | Keyer state machine |
| `libs/cw-engine/src/iambic_keyer.cpp` | | |
| `libs/cw-engine/include/symbol_player.h` | SymbolPlayer | Element timing |
| `libs/cw-engine/src/symbol_player.cpp` | | |
| `libs/cw-engine/include/common.h` | | `millis_fun_ptr` typedef |
