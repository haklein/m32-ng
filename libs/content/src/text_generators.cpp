#include "text_generators.h"
#include "../data/english_words.h"
#include "../data/abbrevs.h"
#include "../data/qso_phrases.h"
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
    // Prefix type: 0=a  1=aa  2=a9  3=9a
    const uint8_t prefix_type[] = {1, 0, 1, 2, 3, 1};
    std::string call;
    call.reserve(10);
    int len = 0;

    if (max_length > 4) max_length = 4;
    if (max_length != 0) max_length += 2;

    int prefix;
    if (max_length == 3) {
        prefix = 0;
    } else {
        prefix = prefix_type[rng_range(0, 6)];
    }

    switch (prefix) {
    case 1:
        call += CW_CHARS[rng_range(0, 26)];
        ++len;
        [[fallthrough]];
    case 0:
        call += CW_CHARS[rng_range(0, 26)];
        ++len;
        break;
    case 2:
        call += CW_CHARS[rng_range(0, 26)];
        call += CW_CHARS[rng_range(26, 36)];
        len = 2;
        break;
    case 3:
        call += CW_CHARS[rng_range(26, 36)];
        call += CW_CHARS[rng_range(0, 26)];
        len = 2;
        break;
    }

    // Digit
    call += CW_CHARS[rng_range(26, 36)];
    ++len;

    // Suffix: 1–3 letters
    int suffix_len;
    if (max_length == 3) {
        suffix_len = 1;
    } else if (max_length == 0) {
        suffix_len = rng_range(1, 4);
        if (suffix_len == 2) suffix_len = rng_range(1, 4); // bias toward 2
    } else {
        suffix_len = std::min(max_length - len, 3);
    }
    while (suffix_len-- > 0) {
        call += CW_CHARS[rng_range(0, 26)];
        ++len;
    }

    // Occasional portable suffix (/p or /m), only when no length constraint
    if (max_length == 0 && rng_range(0, 9) == 0) {
        call += '/';
        call += (rng_range(0, 2) == 0) ? 'm' : 'p';
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

std::string TextGenerators::random_qso_phrase()
{
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
    return result;
}
