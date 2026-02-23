#include "text_generators.h"
#include "../data/english_words.h"
#include "../data/abbrevs.h"
#include <algorithm>
#include <cctype>

// Adapted from m5core2-cwtrainer/lib/TextGenerators.
// Algorithmic content preserved; Arduino::String -> std::string,
// random() -> injected std::mt19937, _min -> std::min.

TextGenerators::TextGenerators(std::mt19937& rng) : rng_(rng) {}

int TextGenerators::rng_range(int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi - 1)(rng_);
}

std::string TextGenerators::random_word(int max_length)
{
    using namespace EnglishWords;
    if (max_length > 5) max_length = 0;
    int start = (max_length != 0) ? WORDS_POINTER[max_length + 1] : 0;
    return words[rng_range(start, WORDS_NUMBER_OF_ELEMENTS)];
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
