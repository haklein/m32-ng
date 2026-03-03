#pragma once

// CWCom / MorseKOB UDP protocol codec.
// All packets are 496 bytes on the wire, little-endian.
// Timing: positive int32 = key-down ms, negative = key-up ms.
// Reference: https://github.com/Morse-Code-over-IP/protocol-cwcom

#include <cstdint>
#include <cstddef>
#include <functional>

class CwComCodec {
public:
    static constexpr int PACKET_SIZE  = 496;
    static constexpr int SHORT_SIZE   = 4;
    static constexpr int MAX_CODE     = 50;   // max timing entries per packet

    static constexpr int16_t CMD_DIS  = 2;
    static constexpr int16_t CMD_DAT  = 3;
    static constexpr int16_t CMD_CON  = 4;

    explicit CwComCodec(const char* callsign = "M32",
                        uint16_t wire = 111);

    // ── TX ─────────────────────────────────────────────────────────
    // Accumulate a key-down (+ms) or key-up (-ms) timing value.
    // Returns true and fills buf[496] when packet is full.
    bool push_timing(int32_t ms_val, uint8_t buf[PACKET_SIZE]);

    // Force-send a partial packet. Returns true if there was data.
    bool flush(uint8_t buf[PACKET_SIZE]);

    // Build a CON (connect) + ID packet pair.
    // Writes two packets: buf[0..3] = short CON, buf[4..499] = ID.
    // Returns total byte count (500).
    int build_connect(uint8_t* buf) const;

    // Build DIS (disconnect). Returns 4 bytes.
    int build_disconnect(uint8_t buf[SHORT_SIZE]) const;

    // Keepalive = same as connect.
    int build_keepalive(uint8_t* buf) const { return build_connect(buf); }

    // ── RX ─────────────────────────────────────────────────────────
    using TimingCb = std::function<void(int32_t ms_val)>;
    // Parse a received packet. Fires on_timing for each timing entry.
    // Also fires with the sender's callsign in sender_out (if non-null).
    void parse(const uint8_t* buf, size_t len,
               TimingCb on_timing, char* sender_out = nullptr) const;

    void set_wire(uint16_t wire)      { wire_ = wire; }
    void set_callsign(const char* cs);

private:
    char     callsign_[32] = {};
    uint16_t wire_         = 111;
    mutable int32_t seq_   = 1;

    // TX accumulation
    int32_t  code_buf_[MAX_CODE + 1] = {};
    int      code_n_   = 0;

    bool emit_code_packet(uint8_t buf[PACKET_SIZE]);
    void build_short(uint8_t buf[SHORT_SIZE], int16_t cmd) const;
    void build_id(uint8_t buf[PACKET_SIZE]) const;
};
