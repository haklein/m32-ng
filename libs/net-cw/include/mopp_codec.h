#pragma once

// MOPP (Morse over Packet Protocol) codec.
// Compact bitstream: 2-bit symbols (01=dit, 10=dah, 00=EOC, 11=EOW).
// Header: version(2b)=01, serial(6b), speed_wpm(6b).
// Reference: oe1wkl/Morserino-32 protocol documentation.

#include <cstdint>
#include <cstddef>
#include <functional>

class MoppCodec {
public:
    explicit MoppCodec(uint8_t speed_wpm = 20);

    // ── TX ─────────────────────────────────────────────────────────
    void push_dit();
    void push_dah();
    void push_char_gap();   // end of character (00)
    void push_word_gap();   // end of word (11) — triggers packet build

    // Build the accumulated word packet. Returns byte count (0 if empty).
    int build_packet(uint8_t* buf, size_t max_len) const;

    // After build_packet, clear the TX buffer for the next word.
    void clear_tx();

    // Plain text connect/disconnect.
    static int build_connect(uint8_t* buf, size_t max_len);
    static int build_disconnect(uint8_t* buf, size_t max_len);

    // ── RX ─────────────────────────────────────────────────────────
    using SymbolCb = std::function<void(bool is_dit)>;
    using GapCb    = std::function<void()>;

    // Parse a received binary MOPP packet.
    // Returns the speed_wpm from the header (0 if invalid).
    uint8_t parse(const uint8_t* buf, size_t len,
                  SymbolCb on_symbol,
                  GapCb on_char_gap,
                  GapCb on_word_gap) const;

    void set_speed_wpm(uint8_t wpm) { speed_wpm_ = wpm; }
    uint8_t speed_wpm() const       { return speed_wpm_; }

private:
    uint8_t  speed_wpm_;
    mutable uint8_t serial_ = 0;

    // TX bit accumulation
    uint8_t  tx_buf_[64] = {};
    int      tx_bit_pos_ = 14;   // start after 14-bit header
    bool     has_data_    = false;

    void push_2bits(uint8_t sym);
};
