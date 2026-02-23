#pragma once

// Adapted from m5core2-cwtrainer/lib/MorseDecoder/MorseDecoder.
// Changes: Arduino::String replaced with std::string; Arduino.h removed.

#include <cstdint>
#include <string>
#include <functional>
#include "../../cw-engine/include/common.h"

enum SignalType { SIGNAL_DOT, SIGNAL_DASH };

// Callback fired with the decoded character string (e.g. "e", "<sk>", " ").
using letter_decoded_fn = std::function<void(const std::string&)>;

// CW binary tree node.
struct CwTreeNode {
    const char* symb;
    uint8_t dit;
    uint8_t dah;
};

// Decodes a sequence of dots and dashes into characters using a binary CW tree.
// Call tick() from the task loop so timeout-based decoding fires automatically.
// Alternatively call decode() explicitly after the last element of a character.
class MorseDecoder
{
public:
    MorseDecoder(unsigned long decode_threshold_ms,
                 letter_decoded_fn letter_decoded_cb,
                 millis_fun_ptr millis_cb);

    void append_dot();
    void append_dash();
    void append(SignalType signal_type);

    // Call every loop iteration; fires decode when inter-character timeout expires.
    void tick();

    // Force decode of whatever is accumulated in the tree path.
    void decode();

    void clear_letter_buf();

    // While key is pressed, suppresses timeout-based decode.
    void set_transmitting(bool transmitting);

    bool is_decode_expired() const;

private:
    bool transmitting = false;
    millis_fun_ptr millis_cb;
    unsigned long last_input_time = 0;
    unsigned long decode_threshold;
    letter_decoded_fn letter_decoded_cb;
    uint8_t treeptr = 0;
};
