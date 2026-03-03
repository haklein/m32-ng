#include "mopp_codec.h"
#include <cstring>

// MOPP bit layout:
//   Byte 0, bits 7-6: version (must be 01)
//   Byte 0, bits 5-0: serial number (upper 6 bits, but actually:
//     header is 14 bits: version[1:0] serial[5:0] speed[5:0])
//
// Bit numbering: MSB first within each byte.
//   bit_pos 0  = byte 0, bit 7
//   bit_pos 1  = byte 0, bit 6
//   bit_pos 7  = byte 0, bit 0
//   bit_pos 8  = byte 1, bit 7
//   etc.

static void set_bit(uint8_t* buf, int pos, bool val)
{
    int byte_idx = pos / 8;
    int bit_idx  = 7 - (pos % 8);
    if (val)
        buf[byte_idx] |= (1 << bit_idx);
    else
        buf[byte_idx] &= ~(1 << bit_idx);
}

static bool get_bit(const uint8_t* buf, int pos, int total_bits)
{
    if (pos >= total_bits) return false;
    int byte_idx = pos / 8;
    int bit_idx  = 7 - (pos % 8);
    return (buf[byte_idx] >> bit_idx) & 1;
}

MoppCodec::MoppCodec(uint8_t speed_wpm)
    : speed_wpm_(speed_wpm)
{
    memset(tx_buf_, 0, sizeof(tx_buf_));
}

void MoppCodec::push_2bits(uint8_t sym)
{
    if (tx_bit_pos_ + 2 > (int)sizeof(tx_buf_) * 8) return;  // overflow guard
    set_bit(tx_buf_, tx_bit_pos_,     (sym >> 1) & 1);
    set_bit(tx_buf_, tx_bit_pos_ + 1, sym & 1);
    tx_bit_pos_ += 2;
    has_data_ = true;
}

void MoppCodec::push_dit()      { push_2bits(0b01); }
void MoppCodec::push_dah()      { push_2bits(0b10); }
void MoppCodec::push_char_gap() { push_2bits(0b00); }

void MoppCodec::push_word_gap()
{
    push_2bits(0b11);
}

int MoppCodec::build_packet(uint8_t* buf, size_t max_len) const
{
    if (!has_data_ || tx_bit_pos_ <= 14) return 0;

    int total_bytes = (tx_bit_pos_ + 7) / 8;
    if ((size_t)total_bytes > max_len) return 0;

    // Copy TX buffer
    memcpy(buf, tx_buf_, total_bytes);

    // Write header (14 bits):
    //   bits 0-1:  version = 01
    //   bits 2-7:  serial (6 bits)
    //   bits 8-13: speed_wpm (6 bits)
    set_bit(buf, 0, false);  // version bit 1
    set_bit(buf, 1, true);   // version bit 0

    uint8_t ser = serial_++;
    for (int i = 0; i < 6; i++)
        set_bit(buf, 2 + i, (ser >> (5 - i)) & 1);

    uint8_t spd = speed_wpm_ & 0x3F;
    for (int i = 0; i < 6; i++)
        set_bit(buf, 8 + i, (spd >> (5 - i)) & 1);

    return total_bytes;
}

void MoppCodec::clear_tx()
{
    memset(tx_buf_, 0, sizeof(tx_buf_));
    tx_bit_pos_ = 14;   // skip header area
    has_data_ = false;
}

int MoppCodec::build_connect(uint8_t* buf, size_t max_len)
{
    if (max_len < 2) return 0;
    buf[0] = 'h';
    buf[1] = 'i';
    return 2;
}

int MoppCodec::build_disconnect(uint8_t* buf, size_t max_len)
{
    if (max_len < 4) return 0;
    buf[0] = ':';
    buf[1] = 'b';
    buf[2] = 'y';
    buf[3] = 'e';
    return 4;
}

uint8_t MoppCodec::parse(const uint8_t* buf, size_t len,
                         SymbolCb on_symbol,
                         GapCb on_char_gap,
                         GapCb on_word_gap) const
{
    if (len < 2) return 0;

    int total_bits = (int)len * 8;

    // Check version (bits 0-1 must be 01)
    bool v1 = get_bit(buf, 0, total_bits);
    bool v0 = get_bit(buf, 1, total_bits);
    if (v1 || !v0) return 0;   // not version 1

    // Serial (bits 2-7) — ignore for now
    // Speed (bits 8-13)
    uint8_t speed = 0;
    for (int i = 0; i < 6; i++) {
        speed <<= 1;
        if (get_bit(buf, 8 + i, total_bits)) speed |= 1;
    }

    // Payload starts at bit 14
    int pos = 14;
    while (pos + 1 < total_bits) {
        bool b1 = get_bit(buf, pos, total_bits);
        bool b0 = get_bit(buf, pos + 1, total_bits);
        pos += 2;

        uint8_t sym = (b1 ? 2 : 0) | (b0 ? 1 : 0);
        switch (sym) {
            case 0b01:  // dit
                if (on_symbol) on_symbol(true);
                break;
            case 0b10:  // dah
                if (on_symbol) on_symbol(false);
                break;
            case 0b00:  // end of character
                if (on_char_gap) on_char_gap();
                break;
            case 0b11:  // end of word
                if (on_word_gap) on_word_gap();
                return speed;   // done with this packet
        }
    }

    return speed;
}
