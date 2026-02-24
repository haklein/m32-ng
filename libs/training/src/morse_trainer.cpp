#include "morse_trainer.h"
#include "../../cw-engine/include/char_morse_table.h"

// Adapted from m5core2-cwtrainer/lib/MorseTrainer.
// State machine logic preserved exactly; millis() injected, String removed,
// Serial debug calls removed, char2morse pulled from shared header.

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
    if (symbol == "<err>") {
        // <HH> resets the whole received word, not just the last character
        // if (!received_phrase_.empty()) received_phrase_.pop_back();
        received_phrase_.clear();
    } else {
        received_phrase_ += symbol;
        if (received_phrase_ == phrase_plain_) {
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
                bool correct = (phrase_plain_ == received_phrase_);
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
                last_player_state_change_ =
                    now - static_cast<uint32_t>(ADVANCE_PHRASE_DELAY_MS);
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
            if (now > last_player_state_change_ + static_cast<uint32_t>(dot_delay_ms_ * 2)) {
                current_player_state_   = PlayerState::InterSymbol;
                last_player_state_change_ = now;
            }
        } else {
            advance_player();
        }
        break;

    case PlayerState::AdvancePhrase:
        if (now > last_player_state_change_ + ADVANCE_PHRASE_DELAY_MS) {
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
            for (char c : phrase_plain_) {
                phrase_morse_ += char_to_morse(c);
                phrase_morse_ += ' ';
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
    }
}
