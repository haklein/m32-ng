#include "rx_cw_player.h"

// CW binary tree (same as MorseDecoder).
struct CwNode { const char* sym; uint8_t dit; uint8_t dah; };
static const CwNode CW_TREE[69] = {
    {"",      1,  2},  //  0 root
    {"e",     3,  4},  //  1
    {"t",     5,  6},  //  2
    {"i",     7,  8},  //  3
    {"a",     9, 10},  //  4
    {"n",    11, 12},  //  5
    {"m",    13, 14},  //  6
    {"s",    15, 16},  //  7
    {"u",    17, 18},  //  8
    {"r",    19, 20},  //  9
    {"w",    21, 22},  // 10
    {"d",    23, 24},  // 11
    {"k",    25, 26},  // 12
    {"g",    27, 28},  // 13
    {"o",    29, 30},  // 14
    {"h",    31, 32},  // 15
    {"v",    33, 34},  // 16
    {"f",    63, 63},  // 17
    {"\xc3\xbc", 35, 36},  // 18  ü
    {"l",    37, 38},  // 19
    {"\xc3\xa4", 39, 63},  // 20  ä
    {"p",    63, 40},  // 21
    {"j",    63, 41},  // 22
    {"b",    42, 43},  // 23
    {"x",    44, 63},  // 24
    {"c",    63, 45},  // 25
    {"y",    46, 63},  // 26
    {"z",    47, 48},  // 27
    {"q",    63, 63},  // 28
    {"\xc3\xb6", 49, 63},  // 29  ö
    {"<ch>", 50, 51},  // 30
    {"5",    64, 63},  // 31
    {"4",    63, 63},  // 32
    {"<ve>", 63, 52},  // 33
    {"3",    63, 63},  // 34
    {"*",    53, 63},  // 35
    {"2",    63, 63},  // 36
    {"<as>", 63, 63},  // 37
    {"*",    54, 63},  // 38
    {"+",    63, 55},  // 39
    {"*",    56, 63},  // 40
    {"1",    57, 63},  // 41
    {"6",    63, 58},  // 42
    {"=",    67, 63},  // 43
    {"/",    63, 63},  // 44
    {"<ka>", 59, 60},  // 45
    {"<kn>", 63, 63},  // 46
    {"7",    63, 63},  // 47
    {"*",    63, 61},  // 48
    {"8",    62, 63},  // 49
    {"9",    63, 63},  // 50
    {"0",    63, 63},  // 51
    {"<sk>", 63, 63},  // 52
    {"?",    63, 63},  // 53
    {"\"",   63, 63},  // 54
    {".",    63, 63},  // 55
    {"@",    63, 63},  // 56
    {"'",    63, 63},  // 57
    {"-",    63, 63},  // 58
    {";",    63, 63},  // 59
    {"!",    63, 63},  // 60
    {",",    63, 63},  // 61
    {":",    63, 63},  // 62
    {"*",    63, 63},  // 63  unknown
    {"*",    65, 63},  // 64
    {"<err>",66, 63},  // 65
    {"<err>",66, 63},  // 66
    {"*",    63, 68},  // 67
    {"<bk>", 63, 63},  // 68
};

RxCwPlayer::RxCwPlayer(ToneOnFn tone_on, ToneOffFn tone_off,
                       LetterFn on_letter, MillisFn millis_fn,
                       unsigned long decode_threshold_ms)
    : on_letter_(std::move(on_letter))
    , millis_fn_(millis_fn)
    , decode_thresh_(decode_threshold_ms)
    , tone_on_(std::move(tone_on))
    , tone_off_(std::move(tone_off))
{
}

void RxCwPlayer::update_avg_dit(unsigned long down_ms, bool is_dit)
{
    unsigned long sample = is_dit ? down_ms : down_ms / 3;
    if (sample == 0) sample = 1;
    if (avg_dit_ms_ == 0)
        avg_dit_ms_ = sample;
    else
        avg_dit_ms_ = (avg_dit_ms_ * 3 + sample) / 4;
}

void RxCwPlayer::flush_char()
{
    if (!has_elements_) return;
    if (on_letter_) on_letter_(CW_TREE[tree_ptr_].sym);
    tree_ptr_ = 0;
    has_elements_ = false;
}

void RxCwPlayer::emit_space()
{
    if (on_letter_) on_letter_(" ");
}

// Classify the pending key-down as dit/dah, walk tree,
// then check if the pending gap is a character/word boundary.
void RxCwPlayer::classify_and_decode()
{
    if (pending_down_ <= 0) return;

    unsigned long down = (unsigned long)pending_down_;
    unsigned long boundary = avg_dit_ms_ > 0
        ? avg_dit_ms_ * 2
        : decode_thresh_ * 2 / 3;
    if (boundary == 0) boundary = 120;

    bool is_dit = (down <= boundary);
    if (is_dit)
        tree_ptr_ = CW_TREE[tree_ptr_].dit;
    else
        tree_ptr_ = CW_TREE[tree_ptr_].dah;
    has_elements_ = true;
    last_decode_time_ = millis_fn_();

    update_avg_dit(down, is_dit);
    pending_down_ = 0;

    // Check gap for character/word boundary
    if (pending_gap_ > 0) {
        unsigned long char_thresh = avg_dit_ms_ > 0
            ? avg_dit_ms_ * 5 / 2 : boundary;
        if (pending_gap_ > char_thresh) {
            flush_char();
            if (avg_dit_ms_ > 0 && pending_gap_ > avg_dit_ms_ * 5)
                emit_space();
        }
        pending_gap_ = 0;
    }
}

void RxCwPlayer::push(int32_t ms_val)
{
    int next = (q_head_ + 1) % QUEUE_SIZE;
    if (next == q_tail_) return;   // full
    queue_[q_head_] = ms_val;
    q_head_ = next;
}

void RxCwPlayer::tick()
{
    unsigned long now = millis_fn_();

    // ── Playback + decode state machine ──
    if (state_dur_ > 0) {
        if ((now - state_start_) < state_dur_)
            goto timeout_check;

        // Duration elapsed
        if (tone_active_) {
            tone_off_();
            tone_active_ = false;
        }
        // When a gap finishes playing, classify the preceding element
        if (playing_gap_)
            classify_and_decode();
        state_dur_ = 0;
        playing_gap_ = false;
    }

    // Pop next timing value
    {
        if (q_tail_ == q_head_)
            goto timeout_check;

        int32_t val = queue_[q_tail_];
        q_tail_ = (q_tail_ + 1) % QUEUE_SIZE;

        state_start_ = now;
        if (val > 0) {
            // Key down — start tone, save duration for later classification
            state_dur_ = (unsigned long)val;
            pending_down_ = val;
            playing_gap_ = false;
            tone_on_();
            tone_active_ = true;
        } else if (val < 0) {
            // Key up — start gap, save gap duration
            unsigned long gap = (unsigned long)(-val);
            state_dur_ = gap;
            pending_gap_ = gap;
            playing_gap_ = true;
            if (tone_active_) {
                tone_off_();
                tone_active_ = false;
            }
        }
    }

timeout_check:
    // Last-character timeout: flush if no new data for a while
    if (has_elements_ && last_decode_time_ > 0 &&
        q_tail_ == q_head_ && state_dur_ == 0) {
        unsigned long thresh = avg_dit_ms_ > 0
            ? avg_dit_ms_ * 3 : decode_thresh_;
        if ((now - last_decode_time_) > thresh)
            flush_char();
    }
}

void RxCwPlayer::clear()
{
    if (tone_active_) {
        tone_off_();
        tone_active_ = false;
    }
    q_head_ = 0;
    q_tail_ = 0;
    state_dur_ = 0;
    pending_down_ = 0;
    pending_gap_ = 0;
    avg_dit_ms_ = 0;
    tree_ptr_ = 0;
    has_elements_ = false;
    last_decode_time_ = 0;
    playing_gap_ = false;
}

void RxCwPlayer::set_decode_threshold(unsigned long ms)
{
    decode_thresh_ = ms;
}
