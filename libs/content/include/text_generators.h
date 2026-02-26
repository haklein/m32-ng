#pragma once

// Adapted from m5core2-cwtrainer/lib/TextGenerators.
// Changes: Arduino::String -> std::string; random() -> injected std::mt19937;
// Arduino.h removed; _min -> std::min.

#include <string>
#include <random>

// Character pool covering the full CW character set.
// SANK encodes prosigns: S=<as> A=<ka> N=<kn> K=<sk> E=<ve> B=<bk> H=<ch>
static constexpr const char CW_CHARS[] =
    "abcdefghijklmnopqrstuvwxyz0123456789.,:-/=?@+SANKEBäöüH";
//   0....5....1....5....2....5....3....5....4....5....5....5

// Koch character orderings from different training programs.
// Lesson N uses the first N characters of the selected string.
enum KochOrder : uint8_t {
    KOCH_LCWO      = 0,  // Learn CW Online
    KOCH_MORSERINO = 1,  // Morserino-32
    KOCH_CWAC      = 2,  // CW Academy (CWops)
    KOCH_LICW      = 3,  // Long Island CW Club carousel
    KOCH_ORDER_COUNT
};

static constexpr const char* KOCH_ORDERS[] = {
    "kmuresnaptlwi.jz=foy,vg5/q92h38b?47c1d60x",     // LCWO
    "mkrsuaptlowi.njef0yv,g5/q9zh38b?427c1d6x-=",     // Morserino
    "teanois14rhdl25ucmw36?fy,pgq79/bv+kj80=xz.-",    // CW Academy
    "reatinpgslcdhofuwbkmy59,qxv73?+=16.zj/28040-",    // LICW
};

static constexpr const char* KOCH_ORDER_NAMES[] = {
    "LCWO", "Morserino", "CW Academy", "LICW"
};

// Convenience alias — length of the longest Koch order (all are the same length).
static constexpr int KOCH_MAX_LESSON = 44;

enum RandomOption
{
    OPT_ALL,        // all CW characters
    OPT_ALPHA,      // a-z only
    OPT_NUM,        // 0-9
    OPT_PUNCT,      // punctuation
    OPT_PRO,        // prosigns
    OPT_ALNUM,      // a-z + 0-9
    OPT_NUMPUNCT,   // 0-9 + punctuation
    OPT_PUNCTPRO,   // punctuation + prosigns
    OPT_ALNUMPUNCT, // a-z + 0-9 + punctuation
    OPT_NUMPUNCTPRO,// 0-9 + punctuation + prosigns
    OPT_KOCH,       // Koch lesson set (handled by caller)
    OPT_KOCH_ADAPTIVE
};

// Generates random training phrases from built-in word, abbreviation,
// callsign, and character-group sources.
// Pass a seeded mt19937 reference; the caller owns the RNG.
class TextGenerators
{
public:
    explicit TextGenerators(std::mt19937& rng);

    // Random English word, optionally limited to max character length (0 = any).
    std::string random_word(int max_length = 0);

    // Random ham radio abbreviation / Q-code.
    std::string random_abbrev(int max_length = 0);

    // Random synthetic callsign (e.g. "W1AB", "DL3XY", "VK2/p").
    std::string random_callsign(int max_length = 0);

    // Random character group from the selected character pool.
    std::string random_chars(int length, RandomOption option = OPT_ALL);

    // Random character group drawn from an explicit charset string.
    std::string random_chars_from_set(const std::string& charset, int length);

private:
    std::mt19937& rng_;

    int rng_range(int lo, int hi);  // returns integer in [lo, hi)
};
