#pragma once

// ITU-based amateur radio callsign prefix table, weighted by HF activity.
// Used by TextGenerators::random_callsign() to produce realistic callsigns.
//
// Weights approximate relative frequency on HF bands.  The total is ~100,
// so each unit is roughly 1 % of generated calls.

struct CallPrefix {
    const char* prefix;   // e.g. "W", "DL", "VK"
    uint8_t weight;       // relative frequency on HF
};

static const CallPrefix CALL_PREFIXES[] = {
    // ── USA (~30 %) ──────────────────────────────────────────────────────
    {"W",  8}, {"K",  8}, {"N",  6},
    {"KA", 2}, {"KB", 1}, {"KC", 1}, {"KD", 1},
    {"WA", 1}, {"WB", 1}, {"AA", 1},

    // ── Germany (~10 %) ──────────────────────────────────────────────────
    {"DL", 4}, {"DJ", 2}, {"DK", 2}, {"DF", 1}, {"DG", 1},

    // ── Japan (~8 %) ─────────────────────────────────────────────────────
    {"JA", 3}, {"JH", 2}, {"JR", 1}, {"JE", 1}, {"JF", 1},

    // ── United Kingdom (~5 %) ────────────────────────────────────────────
    {"G",  3}, {"M",  2},

    // ── Russia (~5 %) ────────────────────────────────────────────────────
    {"UA", 2}, {"RA", 2}, {"RV", 1},

    // ── Italy (~5 %) ─────────────────────────────────────────────────────
    {"I",  2}, {"IK", 2}, {"IZ", 1},

    // ── France (~4 %) ────────────────────────────────────────────────────
    {"F",  4},

    // ── Spain (~4 %) ─────────────────────────────────────────────────────
    {"EA", 4},

    // ── Canada (~3 %) ────────────────────────────────────────────────────
    {"VE", 3},

    // ── Australia (~3 %) ─────────────────────────────────────────────────
    {"VK", 3},

    // ── Brazil (~2 %) ────────────────────────────────────────────────────
    {"PY", 2},

    // ── Poland (~2 %) ────────────────────────────────────────────────────
    {"SP", 2},

    // ── Netherlands (~2 %) ───────────────────────────────────────────────
    {"PA", 2},

    // ── Czech Republic (~1 %) ────────────────────────────────────────────
    {"OK", 1},

    // ── Sweden (~1 %) ────────────────────────────────────────────────────
    {"SM", 1},

    // ── Finland (~1 %) ───────────────────────────────────────────────────
    {"OH", 1},

    // ── Austria (~1 %) ───────────────────────────────────────────────────
    {"OE", 1},

    // ── Belgium (~1 %) ───────────────────────────────────────────────────
    {"ON", 1},

    // ── Hungary (~1 %) ───────────────────────────────────────────────────
    {"HA", 1},

    // ── Croatia (~1 %) ───────────────────────────────────────────────────
    {"9A", 1},

    // ── Portugal (~1 %) ──────────────────────────────────────────────────
    {"CT", 1},

    // ── Greece (~1 %) ────────────────────────────────────────────────────
    {"SV", 1},

    // ── South Africa (~1 %) ──────────────────────────────────────────────
    {"ZS", 1},

    // ── Argentina (~1 %) ─────────────────────────────────────────────────
    {"LU", 1},

    // ── New Zealand (~1 %) ───────────────────────────────────────────────
    {"ZL", 1},
};

static constexpr int NUM_CALL_PREFIXES =
    (int)(sizeof(CALL_PREFIXES) / sizeof(CALL_PREFIXES[0]));

// Compute total weight at compile time is not straightforward in C++11/14,
// so we do it at first use in random_callsign().

// ── Optional suffix modifiers ────────────────────────────────────────────
// Appended after callsign with ~8 % probability.
static const char* const CALL_MODIFIERS[] = {
    "/P", "/M", "/MM", "/QRP"
};
static constexpr int NUM_CALL_MODIFIERS =
    (int)(sizeof(CALL_MODIFIERS) / sizeof(CALL_MODIFIERS[0]));
