#include "morse_trainer.h"
#include "../../cw-engine/include/char_morse_table.h"
#include <cctype>

// Adapted from m5core2-cwtrainer/lib/MorseTrainer.
// State machine logic preserved exactly; millis() injected, String removed,
// Serial debug calls removed, char2morse pulled from shared header.

// Case-insensitive comparison that ignores spaces.  Extra/missing word
// boundaries in the operator's reply don't matter — only the letters do.
// e.g. "ge om t ks fer ca ll" matches "GE OM TKS FER CALL".
static bool phrase_match(const std::string& a, const std::string& b) {
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        if (a[ia] == ' ') { ++ia; continue; }
        if (b[ib] == ' ') { ++ib; continue; }
        if (std::tolower(static_cast<unsigned char>(a[ia])) !=
            std::tolower(static_cast<unsigned char>(b[ib])))
            return false;
        ++ia; ++ib;
    }
    // Skip trailing spaces
    while (ia < a.size() && a[ia] == ' ') ++ia;
    while (ib < b.size() && b[ib] == ' ') ++ib;
    return ia == a.size() && ib == b.size();
}

MorseTrainer::MorseTrainer(sidetone_fn sidetone_cb,
                           new_phrase_fn phrase_cb,
                           millis_fun_ptr millis_cb)
    : sidetone_cb_(std::move(sidetone_cb))
    , phrase_cb_(std::move(phrase_cb))
    , millis_cb_(millis_cb)
{}

void MorseTrainer::set_speed_wpm(int wpm)
{
    wpm_ = wpm;
    dot_delay_ms_  = 1200 / wpm;
    dash_delay_ms_ = (dot_delay_ms_ * 3 * 55) / 50;
    // Recalculate Farnsworth gap if active
    set_farnsworth_wpm(farnsworth_wpm_);
}

void MorseTrainer::set_farnsworth_wpm(int eff_wpm)
{
    farnsworth_wpm_ = eff_wpm;
    if (eff_wpm > 0 && eff_wpm < wpm_) {
        // Farnsworth formula: "PARIS" = 31 element units + 19 gap units = 50 total.
        // gap_unit = (60000/eff - 31*1200/char) / 19
        int gap_unit_ms = (60000 / eff_wpm - 31 * 1200 / wpm_) / 19;
        if (gap_unit_ms < dot_delay_ms_) gap_unit_ms = dot_delay_ms_;
        // InterCharacter adds to the InterSymbol gap (1 dot) to form the full
        // inter-character pause.  Target = 3 × gap_unit, minus the 1-dot
        // InterSymbol that precedes it.
        char_gap_ms_ = 3 * gap_unit_ms - dot_delay_ms_;
        if (char_gap_ms_ < dot_delay_ms_) char_gap_ms_ = dot_delay_ms_;
    } else {
        // Normal timing: InterCharacter = 2 dots (+ 1 dot InterSymbol = 3 total)
        char_gap_ms_ = dot_delay_ms_ * 2;
    }
}

void MorseTrainer::set_echo_result_fn(echo_result_fn cb) { result_cb_ = std::move(cb); }
void MorseTrainer::set_echo_reveal_fn(echo_reveal_fn cb) { reveal_cb_ = std::move(cb); }
void MorseTrainer::set_new_phrase_fn(new_phrase_fn cb)   { phrase_cb_ = std::move(cb); }
void MorseTrainer::set_max_echo_repeats(uint8_t n)       { max_echo_repeats_ = n; }
void MorseTrainer::set_state(TrainerState s)             { current_state_ = s; }
void MorseTrainer::set_adaptive_speed(bool a)            { adaptive_speed_ = a; }

void MorseTrainer::set_idle()
{
    current_player_state_ = PlayerState::Idle;
    sidetone_cb_(false);
}

void MorseTrainer::set_playing()
{
    current_player_state_     = PlayerState::AdvancePhrase;
    last_player_state_change_ = 0;   // bypass ADVANCE_PHRASE_DELAY on next tick
}

void MorseTrainer::tame_echo_timeout()
{
    last_keyer_received_ = millis_cb_();
}

void MorseTrainer::symbol_received(const std::string& symbol)
{
    last_keyer_received_ = millis_cb_();

    // Detect delete signals: <err> (7+ dits = <HH>), * (6 dits or other
    // unknown CW), or EEEE (4 consecutive separate dits — common on-air
    // correction signal).
    bool is_delete = (symbol == "<err>" || symbol == "*");

    if (!is_delete && symbol == "e") {
        ++consecutive_e_;
        if (consecutive_e_ >= 4) {
            // Remove the 3 previously appended 'e's
            if (received_phrase_.size() >= 3)
                received_phrase_.erase(received_phrase_.size() - 3);
            else
                received_phrase_.clear();
            is_delete = true;
        }
    } else if (!is_delete) {
        consecutive_e_ = 0;
    }

    if (is_delete) {
        consecutive_e_ = 0;
        // Strip trailing spaces (from word-gap detection), then remove last word.
        while (!received_phrase_.empty() && received_phrase_.back() == ' ')
            received_phrase_.pop_back();
        auto pos = received_phrase_.rfind(' ');
        if (pos != std::string::npos)
            received_phrase_.erase(pos);
        else
            received_phrase_.clear();
    } else {
        received_phrase_ += symbol;
        if (phrase_match(received_phrase_, phrase_plain_)) {
            // Instantly trigger evaluation on next tick().
            last_keyer_received_ = 0;
        }
    }
}

void MorseTrainer::tick()
{
    const uint32_t now = millis_cb_();

    // Echo state machine (runs when not in Playing sub-state)
    if (current_state_ == TrainerState::Echo &&
        current_echo_state_ != EchoState::Playing)
    {
        switch (current_echo_state_) {
        case EchoState::Receiving:
            if (now > last_keyer_received_ + ECHO_START_RECEIVE_DELAY_MS) {
                bool correct = phrase_match(phrase_plain_, received_phrase_);
                if (correct) {
                    echo_repeat_count_     = 0;
                    current_echo_state_    = EchoState::Success;
                    last_echo_state_change_ = now;
                    if (result_cb_) result_cb_(phrase_plain_, true);
                    if (adaptive_speed_) set_speed_wpm(wpm_ + 1);
                } else {
                    if (max_echo_repeats_ > 0 &&
                        ++echo_repeat_count_ >= max_echo_repeats_)
                    {
                        // Max repeats exhausted — reveal phrase, then advance.
                        echo_repeat_count_     = 0;
                        current_echo_state_    = EchoState::Reveal;
                        last_echo_state_change_ = now;
                        if (reveal_cb_) reveal_cb_(phrase_plain_);
                    } else {
                        current_echo_state_    = EchoState::Error;
                        last_echo_state_change_ = now;
                        current_player_state_  = PlayerState::InterCharacter;
                        player_position_       = 0;
                        if (result_cb_) result_cb_(phrase_plain_, false);
                    }
                    if (adaptive_speed_) set_speed_wpm(std::max(5, wpm_ - 1));
                }
                received_phrase_.clear();
            }
            break;

        case EchoState::Success:
            if (now > last_echo_state_change_ + SUCCESS_DELAY_MS) {
                current_echo_state_ = EchoState::Playing;
            }
            break;

        case EchoState::Error:
            if (now > last_echo_state_change_ + ERROR_DELAY_MS) {
                current_echo_state_ = EchoState::Playing;
            }
            break;

        case EchoState::Reveal:
            if (now > last_echo_state_change_ + REVEAL_DELAY_MS) {
                current_echo_state_   = EchoState::Playing;
                current_player_state_ = PlayerState::AdvancePhrase;
                // Bypass AdvancePhrase's own delay so next phrase starts quickly.
                last_player_state_change_ = 0;
            }
            break;

        default:
            break;
        }
        return;
    }

    // Player state machine
    switch (current_player_state_) {
    case PlayerState::Dot:
        if (now > last_player_state_change_ + static_cast<uint32_t>(dot_delay_ms_)) {
            current_player_state_   = PlayerState::InterSymbol;
            last_player_state_change_ = now;
            sidetone_cb_(false);
        }
        break;

    case PlayerState::Dash:
        if (now > last_player_state_change_ + static_cast<uint32_t>(dash_delay_ms_)) {
            current_player_state_   = PlayerState::InterSymbol;
            last_player_state_change_ = now;
            sidetone_cb_(false);
        }
        break;

    case PlayerState::InterSymbol:
        if (player_position_ < phrase_morse_.size() + 1) {
            if (now > last_player_state_change_ + static_cast<uint32_t>(dot_delay_ms_)) {
                ++player_position_;
                char ch = (player_position_ <= phrase_morse_.size())
                          ? phrase_morse_[player_position_ - 1] : '\0';
                switch (ch) {
                case '-':
                    current_player_state_   = PlayerState::Dash;
                    last_player_state_change_ = now;
                    sidetone_cb_(true);
                    break;
                case '.':
                    current_player_state_   = PlayerState::Dot;
                    last_player_state_change_ = now;
                    sidetone_cb_(true);
                    break;
                case ' ':
                default:
                    current_player_state_   = PlayerState::InterCharacter;
                    last_player_state_change_ = now;
                    break;
                }
            }
        } else {
            advance_player();
        }
        break;

    case PlayerState::InterCharacter:
        if (player_position_ < phrase_morse_.size() + 1) {
            if (now > last_player_state_change_ + static_cast<uint32_t>(char_gap_ms_)) {
                current_player_state_   = PlayerState::InterSymbol;
                last_player_state_change_ = now;
            }
        } else {
            advance_player();
        }
        break;

    case PlayerState::AdvancePhrase:
        if (now > last_player_state_change_ + static_cast<uint32_t>(advance_phrase_delay_ms())) {
            phrase_morse_.clear();
            player_position_ = 0;

            phrase_plain_ = phrase_cb_();

            // Empty phrase means nothing to play — go idle.
            if (phrase_plain_.empty()) {
                current_player_state_ = PlayerState::Idle;
                sidetone_cb_(false);
                return;
            }

            // Expand plain text to dot/dash string with inter-character spaces.
            // Prosigns enclosed in <...> are played without inter-character
            // gaps (e.g. <AR> → .-.-. as one symbol, not A R as two).
            {
                bool in_prosign = false;
                for (char c : phrase_plain_) {
                    if (c == '<') { in_prosign = true; continue; }
                    if (c == '>') { in_prosign = false; phrase_morse_ += ' '; continue; }
                    phrase_morse_ += char_to_morse(c);
                    if (!in_prosign) phrase_morse_ += ' ';
                }
            }

            last_player_state_change_ = now;
            current_player_state_    = PlayerState::InterSymbol;
        }
        break;

    case PlayerState::Idle:
    default:
        break;
    }
}

void MorseTrainer::advance_player()
{
    const uint32_t now = millis_cb_();
    current_player_state_   = PlayerState::AdvancePhrase;
    last_player_state_change_ = now;

    // In echo mode: phrase finished playing → switch to Receiving.
    if (current_state_ == TrainerState::Echo &&
        current_echo_state_ == EchoState::Playing)
    {
        current_echo_state_   = EchoState::Receiving;
        last_keyer_received_  = now;
        received_phrase_.clear();
        consecutive_e_ = 0;
    }
}
