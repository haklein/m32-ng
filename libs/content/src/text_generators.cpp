#include "text_generators.h"
#include "../data/english_words.h"
#include "../data/abbrevs.h"
#include "../data/qso_phrases.h"
#include "../data/callsign_prefixes.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

// Adapted from m5core2-cwtrainer/lib/TextGenerators.
// Algorithmic content preserved; Arduino::String -> std::string,
// random() -> injected std::mt19937, _min -> std::min.

TextGenerators::TextGenerators(std::mt19937& rng) : rng_(rng) {}

int TextGenerators::rng_range(int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi - 1)(rng_);
}

// Weighted random selection from Oxford 5000 word list.
// Words with higher frequency weights appear more often.
std::string TextGenerators::random_word(int max_length)
{
    // Compute total weight for eligible words
    long total_weight = 0;
    for (size_t i = 0; i < OXFORD_WORD_COUNT; ++i) {
        if (max_length <= 0 || (int)std::strlen(words[i].word) <= max_length)
            total_weight += words[i].weight;
    }
    if (total_weight == 0) return "cq";

    // Pick a random point in the cumulative weight
    long r = std::uniform_int_distribution<long>(0, total_weight - 1)(rng_);
    long cumulative = 0;
    for (size_t i = 0; i < OXFORD_WORD_COUNT; ++i) {
        if (max_length <= 0 || (int)std::strlen(words[i].word) <= max_length) {
            cumulative += words[i].weight;
            if (r < cumulative)
                return words[i].word;
        }
    }
    return words[0].word;
}

std::string TextGenerators::random_abbrev(int max_length)
{
    using namespace Abbrev;
    if (max_length > 5) max_length = 0;
    int start = (max_length != 0) ? ABBREV_POINTER[max_length + 1] : 0;
    return abbreviations[rng_range(start, ABBREV_NUMBER_OF_ELEMENTS)];
}

std::string TextGenerators::random_callsign(int max_length)
{
    // Compute total prefix weight on first call
    static int total_weight = 0;
    if (total_weight == 0) {
        for (int i = 0; i < NUM_CALL_PREFIXES; ++i)
            total_weight += CALL_PREFIXES[i].weight;
    }

    std::string call;
    call.reserve(10);

    // Pick prefix by cumulative weight
    int r = rng_range(0, total_weight);
    int cum = 0;
    const char* pfx = CALL_PREFIXES[0].prefix;
    for (int i = 0; i < NUM_CALL_PREFIXES; ++i) {
        cum += CALL_PREFIXES[i].weight;
        if (r < cum) { pfx = CALL_PREFIXES[i].prefix; break; }
    }
    int pfx_len = (int)std::strlen(pfx);

    // If max_length is too tight for this prefix, fall back to 1-letter prefix
    // (minimum call = prefix + digit + 1 suffix = pfx_len + 2)
    if (max_length > 0 && pfx_len + 2 > max_length) {
        // Pick a random 1-letter prefix from the common ones
        static const char* SHORT_PFX[] = {"W", "K", "N", "G", "F", "I", "M"};
        pfx = SHORT_PFX[rng_range(0, 7)];
        pfx_len = 1;
    }

    call += pfx;

    // Digit 0–9
    call += static_cast<char>('0' + rng_range(0, 10));

    // Suffix: 1–3 letters, biased toward 2–3
    int budget = (max_length > 0) ? max_length - pfx_len - 1 : 3;
    budget = std::min(budget, 3);
    int suffix_len;
    if (budget <= 1) {
        suffix_len = 1;
    } else {
        // ~15% 1-letter, ~45% 2-letter, ~40% 3-letter (if budget allows)
        int roll = rng_range(0, 20);
        if (roll < 3)       suffix_len = 1;
        else if (roll < 12) suffix_len = 2;
        else                suffix_len = 3;
        suffix_len = std::min(suffix_len, budget);
    }
    for (int i = 0; i < suffix_len; ++i)
        call += static_cast<char>('A' + rng_range(0, 26));

    // Optional modifier (~8 %): /P, /M, /MM, /QRP — only when unconstrained
    if (max_length <= 0 && rng_range(0, 25) < 2) {
        call += CALL_MODIFIERS[rng_range(0, NUM_CALL_MODIFIERS)];
    }

    return call;
}

std::string TextGenerators::random_chars_from_set(const std::string& charset, int length)
{
    if (charset.empty() || length <= 0) return "";
    std::string result;
    result.reserve(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        result += charset[rng_range(0, (int)charset.size())];
    return result;
}

std::string TextGenerators::random_chars(int length, RandomOption option)
{
    std::string result;
    result.reserve(static_cast<size_t>(length));

    int s = 0, e = 51;

    switch (option) {
    case OPT_NUM:
    case OPT_NUMPUNCT:
    case OPT_NUMPUNCTPRO:
        s = 26; break;
    case OPT_PUNCT:
    case OPT_PUNCTPRO:
        s = 36; break;
    case OPT_PRO:
        s = 44; break;
    default:
        s = 0;  break;
    }

    switch (option) {
    case OPT_ALPHA:
        e = 26; break;
    case OPT_ALNUM:
    case OPT_NUM:
        e = 36; break;
    case OPT_ALNUMPUNCT:
    case OPT_NUMPUNCT:
    case OPT_PUNCT:
        e = 45; break;
    default:
        e = 51; break;
    }

    if (length > 6) {
        length = rng_range(2, length - 3);
    }

    for (int i = 0; i < length; ++i) {
        result += CW_CHARS[rng_range(s, e)];
    }
    return result;
}

static int word_count(const std::string& s)
{
    if (s.empty()) return 0;
    int n = 1;
    for (char c : s) if (c == ' ') ++n;
    return n;
}

std::string TextGenerators::random_qso_phrase(int max_words)
{
    // Retry with different templates if the result exceeds max_words.
    for (int attempt = 0; attempt < 50; ++attempt) {
    const char* tmpl = QSO_TEMPLATES[rng_range(0, NUM_QSO_TEMPLATES)];
    std::string result;
    result.reserve(32);

    // Cache: each slot type resolves once per phrase so repeated
    // placeholders (e.g. "RST {rst} {rst}") produce the same value.
    std::map<std::string, std::string> cache;

    for (const char* p = tmpl; *p; ) {
        if (*p == '{') {
            const char* end = std::strchr(p, '}');
            if (!end) { result += *p++; continue; }
            std::string slot(p + 1, end);
            p = end + 1;

            auto it = cache.find(slot);
            if (it != cache.end()) {
                result += it->second;
                continue;
            }

            std::string val;
            if      (slot == "name")  val = QSO_NAMES[rng_range(0, NUM_QSO_NAMES)];
            else if (slot == "qth")   val = QSO_CITIES[rng_range(0, NUM_QSO_CITIES)];
            else if (slot == "rst")   val = QSO_RST[rng_range(0, NUM_QSO_RST)];
            else if (slot == "rig")   val = QSO_RIGS[rng_range(0, NUM_QSO_RIGS)];
            else if (slot == "pwr")   val = QSO_POWERS[rng_range(0, NUM_QSO_POWERS)];
            else if (slot == "ant")   val = QSO_ANTENNAS[rng_range(0, NUM_QSO_ANTENNAS)];
            else if (slot == "wx")    val = QSO_WX[rng_range(0, NUM_QSO_WX)];
            else if (slot == "condx") val = QSO_CONDX[rng_range(0, NUM_QSO_CONDX)];
            else if (slot == "band")  val = QSO_BANDS[rng_range(0, NUM_QSO_BANDS)];
            else if (slot == "call")  val = random_callsign(0);
            else if (slot == "temp") {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", rng_range(-10, 40));
                val = buf;
            }
            else if (slot == "age") {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", rng_range(20, 85));
                val = buf;
            }
            else if (slot == "lic") {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", rng_range(1, 55));
                val = buf;
            }
            cache[slot] = val;
            result += val;
        } else {
            result += *p++;
        }
    }
    if (max_words <= 0 || word_count(result) <= max_words)
        return result;
    } // retry loop
    // All attempts exceeded max_words — return last result anyway.
    return "CW";
}
