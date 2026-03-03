#include "cwcom_codec.h"
#include <cstring>

// CWCom wire format (little-endian, 496 bytes total):
//
// Short packet (connect/disconnect): 4 bytes
//   [0..1]  int16  cmd    (CON=4, DIS=2)
//   [2..3]  int16  wire
//
// Data packet (ID or code): 496 bytes
//   [0..1]    int16    cmd     (DAT=3)
//   [2..3]    int16    byts    (496)
//   [4..131]  char[128] id     (callsign, null-padded)
//   [132..135] padding
//   [136..139] int32   seq
//   [140..143] int32   idflag  (nonzero for ID packets)
//
//   Code packet (idflag == 0):
//   [140..151]  12 bytes padding
//   [152..355]  int32[51] code  (timing values)
//   [356..359]  int32    n      (count of valid entries, max 50)
//   [360..487]  char[128] txt
//   [488..495]  padding
//
//   ID packet (idflag != 0):
//   [144..151]  8 bytes padding
//   [152..359]  208 bytes padding
//   [360..487]  char[128] ver
//   [488..495]  padding

static void write_i16(uint8_t* p, int16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_i32(uint8_t* p, int32_t v)
{
    auto u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xFF);
    p[1] = (uint8_t)((u >> 8) & 0xFF);
    p[2] = (uint8_t)((u >> 16) & 0xFF);
    p[3] = (uint8_t)((u >> 24) & 0xFF);
}

static int16_t read_i16(const uint8_t* p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t read_i32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

CwComCodec::CwComCodec(const char* callsign, uint16_t wire)
    : wire_(wire)
{
    set_callsign(callsign);
}

void CwComCodec::set_callsign(const char* cs)
{
    memset(callsign_, 0, sizeof(callsign_));
    if (cs) strncpy(callsign_, cs, sizeof(callsign_) - 1);
}

void CwComCodec::build_short(uint8_t buf[SHORT_SIZE], int16_t cmd) const
{
    write_i16(buf + 0, cmd);
    write_i16(buf + 2, (int16_t)wire_);
}

void CwComCodec::build_id(uint8_t buf[PACKET_SIZE]) const
{
    memset(buf, 0, PACKET_SIZE);
    write_i16(buf + 0, CMD_DAT);
    write_i16(buf + 2, (int16_t)PACKET_SIZE);

    // id[128] at offset 4
    strncpy((char*)(buf + 4), callsign_, 127);

    // seq at offset 136
    write_i32(buf + 136, seq_++);

    // idflag at offset 140 (nonzero = ID packet)
    write_i32(buf + 140, 1);

    // ver[128] at offset 360
    strncpy((char*)(buf + 360), "M32-NG 1.0", 127);
}

int CwComCodec::build_connect(uint8_t* buf) const
{
    // Short CON packet (4 bytes)
    build_short(buf, CMD_CON);
    // ID packet (496 bytes)
    build_id(buf + SHORT_SIZE);
    return SHORT_SIZE + PACKET_SIZE;
}

int CwComCodec::build_disconnect(uint8_t buf[SHORT_SIZE]) const
{
    build_short(buf, CMD_DIS);
    return SHORT_SIZE;
}

bool CwComCodec::push_timing(int32_t ms_val, uint8_t buf[PACKET_SIZE])
{
    if (code_n_ < MAX_CODE) {
        code_buf_[code_n_++] = ms_val;
    }
    if (code_n_ >= MAX_CODE) {
        return emit_code_packet(buf);
    }
    return false;
}

bool CwComCodec::flush(uint8_t buf[PACKET_SIZE])
{
    if (code_n_ == 0) return false;
    return emit_code_packet(buf);
}

bool CwComCodec::emit_code_packet(uint8_t buf[PACKET_SIZE])
{
    memset(buf, 0, PACKET_SIZE);
    write_i16(buf + 0, CMD_DAT);
    write_i16(buf + 2, (int16_t)PACKET_SIZE);

    // id[128] at offset 4
    strncpy((char*)(buf + 4), callsign_, 127);

    // seq at offset 136
    write_i32(buf + 136, seq_++);

    // idflag at offset 140 = 0 (code packet)
    // code[51] at offset 152
    for (int i = 0; i < code_n_; i++) {
        write_i32(buf + 152 + i * 4, code_buf_[i]);
    }

    // n at offset 356
    write_i32(buf + 356, code_n_);

    code_n_ = 0;
    return true;
}

void CwComCodec::parse(const uint8_t* buf, size_t len,
                       TimingCb on_timing, char* sender_out) const
{
    if (len < SHORT_SIZE) return;

    int16_t cmd = read_i16(buf + 0);
    if (cmd != CMD_DAT || len < (size_t)PACKET_SIZE) return;

    // Extract sender callsign
    if (sender_out) {
        memcpy(sender_out, buf + 4, 31);
        sender_out[31] = '\0';
    }

    // Check idflag at offset 140
    int32_t idflag = read_i32(buf + 140);
    if (idflag != 0) return;   // ID packet, no code data

    // Code packet: n at offset 356
    int32_t n = read_i32(buf + 356);
    if (n < 0 || n > MAX_CODE) return;

    // code[51] at offset 152
    for (int i = 0; i < n; i++) {
        int32_t val = read_i32(buf + 152 + i * 4);
        if (val != 0 && on_timing)
            on_timing(val);
    }
}
