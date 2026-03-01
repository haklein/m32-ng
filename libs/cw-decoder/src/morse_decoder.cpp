#include "morse_decoder.h"

// Adapted from m5core2-cwtrainer/lib/MorseDecoder/MorseDecoder.
// CW binary tree and decode logic preserved exactly.
// String -> std::string; Arduino.h removed.

// CW binary tree — 69 nodes covering full alphabet, digits, punctuation,
// prosigns and German umlauts.  63 = dead end (unknown character path).
static const CwTreeNode CW_TREE[69] = {
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
    {"*",    63, 63},  // 63  default unknown
    {"*",    65, 63},  // 64
    {"<err>",66, 63},  // 65
    {"<err>",66, 63},  // 66  error / backspace
    {"*",    63, 68},  // 67
    {"<bk>", 63, 63},  // 68
};

MorseDecoder::MorseDecoder(unsigned long decode_threshold_ms,
                           letter_decoded_fn letter_decoded_cb,
                           millis_fun_ptr millis_cb)
    : decode_threshold(decode_threshold_ms)
    , letter_decoded_cb(std::move(letter_decoded_cb))
    , millis_cb(millis_cb)
{}

void MorseDecoder::set_transmitting(bool t)
{
    transmitting = t;
}

void MorseDecoder::set_decode_threshold(unsigned long ms)
{
    decode_threshold = ms;
}

bool MorseDecoder::is_decode_expired() const
{
    if (last_input_time == 0 || transmitting) {
        return false;
    }
    return (millis_cb() - last_input_time) > decode_threshold;
}

void MorseDecoder::tick()
{
    if (is_decode_expired()) {
        decode();
        last_input_time = 0;
    }

    // Word-gap detection: if a character was decoded and silence continues,
    // emit a word space.  Fires at ~decode_threshold*3 after the character
    // decode, which puts total silence at ~8 dit-lengths from the last
    // element (comfortably above the standard 7-dit word gap).
    if (space_pending_ && last_input_time == 0 &&
        (millis_cb() - char_decode_time_) > decode_threshold * 3) {
        space_pending_ = false;
        letter_decoded_cb(" ");
    }
}

void MorseDecoder::decode()
{
    std::string symbol;
    if (treeptr == 0) {
        symbol = " ";
    } else {
        symbol = CW_TREE[treeptr].symb;
        // Start word-gap timer after decoding a real character.
        space_pending_ = true;
        char_decode_time_ = millis_cb();
    }
    treeptr = 0;
    letter_decoded_cb(symbol);
    clear_letter_buf();
}

void MorseDecoder::clear_letter_buf()
{
    treeptr = 0;
}

void MorseDecoder::append_dash()
{
    append(SIGNAL_DASH);
}

void MorseDecoder::append_dot()
{
    append(SIGNAL_DOT);
}

void MorseDecoder::append(SignalType signal_type)
{
    space_pending_ = false;   // new input cancels word-gap detection
    if (signal_type == SIGNAL_DASH) {
        treeptr = CW_TREE[treeptr].dah;
    } else {
        treeptr = CW_TREE[treeptr].dit;
    }
    last_input_time = millis_cb();
}
