#pragma once

// Plays received CW timing values as sidetone and decodes them.
// Both sidetone and decode are driven by tick() in sync.
// Character boundaries are detected from actual gap durations in
// the received timing, not from wall-clock timeouts (WPM-independent).

#include <cstdint>
#include <functional>
#include <string>

class RxCwPlayer {
public:
    using ToneOnFn  = std::function<void()>;
    using ToneOffFn = std::function<void()>;
    using LetterFn  = std::function<void(const std::string&)>;
    using MillisFn  = unsigned long(*)();

    RxCwPlayer(ToneOnFn tone_on, ToneOffFn tone_off,
               LetterFn on_letter, MillisFn millis_fn,
               unsigned long decode_threshold_ms);

    // Queue a CWCom-style timing value: +ms = key-down, -ms = key-up.
    void push(int32_t ms_val);

    // Drive sidetone playback + decode. Call every tick (~5ms).
    void tick();

    void clear();
    void set_decode_threshold(unsigned long ms);

private:
    // ── Playback queue ──
    static constexpr int QUEUE_SIZE = 128;
    int32_t       queue_[QUEUE_SIZE] = {};
    volatile int  q_head_ = 0;
    volatile int  q_tail_ = 0;

    bool          tone_active_  = false;
    unsigned long state_start_  = 0;
    unsigned long state_dur_    = 0;
    bool          playing_gap_  = false;  // current state is a gap (key-up)

    // ── Decode state ──
    LetterFn      on_letter_;
    MillisFn      millis_fn_;
    unsigned long decode_thresh_ = 0;
    unsigned long avg_dit_ms_   = 0;
    int32_t       pending_down_ = 0;      // key-down duration awaiting classification
    unsigned long pending_gap_  = 0;      // gap duration for char boundary check
    uint8_t       tree_ptr_     = 0;
    bool          has_elements_ = false;
    unsigned long last_decode_time_ = 0;  // for last-char timeout

    void classify_and_decode();
    void flush_char();
    void emit_space();
    void update_avg_dit(unsigned long down_ms, bool is_dit);

    ToneOnFn      tone_on_;
    ToneOffFn     tone_off_;
};
