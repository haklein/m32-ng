#pragma once

// Adapted from m5core2-cwtrainer/lib/MorseTrainer.
// Changes:
//   - Arduino::String -> std::string
//   - millis() hardcoded calls -> injected millis_fun_ptr
//   - Serial.print debug calls removed
//   - char2morse() pulled from libs/cw-engine/char_morse_table.h
//   - boolean -> bool
//   - Callback typedefs use std::function for easier lambda binding

#include <cstdint>
#include <string>
#include <functional>
#include "../../cw-engine/include/common.h"

// Called to turn the sidetone on (true) or off (false).
using sidetone_fn     = std::function<void(bool)>;

// Reports echo trainer result: (phrase_plain, success).
using echo_result_fn  = std::function<void(const std::string&, bool)>;

// Called when max echo repeats is reached with the correct phrase, so the UI
// can reveal it to the operator.
using echo_reveal_fn  = std::function<void(const std::string&)>;

// Fetches the next phrase to play; returns plain text string.
using new_phrase_fn   = std::function<std::string()>;

// Non-blocking Morse player and echo trainer state machine.
//
// Player mode: continuously fetches phrases via phrase_cb and plays them.
// Echo mode:   plays a phrase, waits for the operator to key it back,
//              compares, reports result, adapts WPM.
//
// Call tick() on every iteration of the task loop.
class MorseTrainer
{
public:
    enum class TrainerState { Player, Echo };
    enum class EchoState    { Playing, Receiving, Success, Error, Reveal };
    enum class PlayerState  {
        Dot, Dash, InterSymbol, InterCharacter, InterWord, AdvancePhrase, Idle
    };

    MorseTrainer(sidetone_fn sidetone_cb,
                 new_phrase_fn phrase_cb,
                 millis_fun_ptr millis_cb);

    // Drive the state machine — call every loop iteration.
    void tick();

    void set_idle();
    void set_playing();
    void set_state(TrainerState state);
    void set_speed_wpm(int wpm);
    // Farnsworth: effective WPM for inter-character/word spacing.
    // 0 = off (gaps at character speed).  Must be <= character WPM.
    void set_farnsworth_wpm(int eff_wpm);
    void set_adaptive_speed(bool adaptive);
    void set_echo_result_fn(echo_result_fn cb);
    void set_echo_reveal_fn(echo_reveal_fn cb);
    void set_new_phrase_fn(new_phrase_fn cb);

    // 0 = unlimited repeats; N = advance after N failures without success.
    void set_max_echo_repeats(uint8_t n);

    // Feed a decoded letter from MorseDecoder into the echo trainer.
    void symbol_received(const std::string& symbol);

    // Call when paddle activity is detected to reset the echo receive timeout.
    void tame_echo_timeout();

    TrainerState trainer_state() const { return current_state_; }
    PlayerState  player_state()  const { return current_player_state_; }

private:
    void advance_player();

    int wpm_ = 18;
    bool adaptive_speed_ = true;

    int dot_delay_ms_  = 1200 / 18;
    int dash_delay_ms_ = (dot_delay_ms_ * 3 * 55) / 50;
    int char_gap_ms_   = dot_delay_ms_ * 2;  // InterCharacter delay (stretched for Farnsworth)
    int farnsworth_wpm_ = 0;                  // 0 = off

    static constexpr int SUCCESS_DELAY_MS            = 1000;
    static constexpr int ERROR_DELAY_MS              = 1000;
    static constexpr int REVEAL_DELAY_MS             = 2000;
    static constexpr int ECHO_START_RECEIVE_DELAY_MS = 2000;

    // Inter-phrase pause scales with WPM: ~4 word-spaces (28 dit-lengths).
    // At 15 WPM ≈ 2.2 s, at 30 WPM ≈ 1.1 s, at 5 WPM ≈ 6.7 s.
    int advance_phrase_delay_ms() const { return dot_delay_ms_ * 28; }

    sidetone_fn    sidetone_cb_;
    echo_result_fn result_cb_;
    echo_reveal_fn reveal_cb_;
    new_phrase_fn  phrase_cb_;
    millis_fun_ptr millis_cb_;

    uint8_t max_echo_repeats_  = 3;   // 0 = unlimited
    uint8_t echo_repeat_count_ = 0;

    TrainerState current_state_        = TrainerState::Player;
    PlayerState  current_player_state_ = PlayerState::Idle;
    EchoState    current_echo_state_   = EchoState::Playing;

    std::string received_phrase_;
    std::string phrase_morse_;   // expanded as ".- -... " etc.
    std::string phrase_plain_;   // plain text, used for echo comparison

    uint32_t last_keyer_received_     = 0;
    uint32_t last_player_state_change_ = 0;
    uint32_t last_echo_state_change_  = 0;
    uint16_t player_position_         = 0;
};
