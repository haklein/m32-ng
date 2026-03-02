// Native build entry point.
// Run with: pio run -e native  (compilation check)
//           pio test -e native (unit tests, once test cases are added)
//
// Add test cases here as the libs mature.
// Each lib under libs/ must compile and behave correctly on this target.

#include <chrono>
#include <cstdio>
#include <thread>

// ── Smoke-test includes: verify every library header compiles on native ───────
#include "../../libs/cw-engine/include/common.h"
#include "../../libs/cw-engine/include/symbol_player.h"
#include "../../libs/cw-engine/include/iambic_keyer.h"
#include "../../libs/cw-engine/include/paddle_ctl.h"
#include "../../libs/cw-engine/include/straight_keyer.h"
#include "../../libs/cw-engine/include/char_morse_table.h"
#include "../../libs/cw-decoder/include/goertzel_detector.h"
#include "../../libs/cw-decoder/include/morse_decoder.h"
#include "../../libs/content/include/text_generators.h"
#include "../../libs/training/include/morse_trainer.h"
#include "../../libs/hal/interfaces/hal.h"
#include "../../libs/hal/native/audio_output_alsa.h"

int main()
{
    // ── char_morse_table ─────────────────────────────────────────────────────
    auto check = [](char c, const char* expected) {
        auto got = char_to_morse(c);
        bool ok = (got == expected);
        printf("char_to_morse('%c'): %s  %s\n", c, got.c_str(), ok ? "OK" : "FAIL");
        return ok;
    };

    bool all_ok = true;
    all_ok &= check('E', ".");
    all_ok &= check('T', "-");
    all_ok &= check('A', ".-");
    all_ok &= check('0', "-----");
    all_ok &= check('.', ".-.-.-");

    // ── MorseDecoder ─────────────────────────────────────────────────────────
    std::string decoded;
    auto millis_stub = []() -> unsigned long { return 0UL; };
    MorseDecoder dec(200, [&](const std::string& s){ decoded = s; }, millis_stub);
    dec.append_dot();                   // E
    dec.decode();
    bool dec_ok = (decoded == "e");
    printf("MorseDecoder 'E': %s  %s\n", decoded.c_str(), dec_ok ? "OK" : "FAIL");
    all_ok &= dec_ok;

    // ── TextGenerators ───────────────────────────────────────────────────────
    std::mt19937 rng(42);
    TextGenerators gen(rng);
    std::string word = gen.random_word();
    printf("TextGenerators random_word: '%s'  %s\n",
           word.c_str(), word.empty() ? "FAIL" : "OK");
    all_ok &= !word.empty();

    // ── NativeAudioOutputAlsa — play "VVV" (... - ... - ... -) at 18 WPM ─────
    // This is an audible test; you should hear three V's through your speakers.
    printf("Audio: playing 'VVV' via ALSA (700 Hz, 18 WPM) ...\n");
    {
        using ms = std::chrono::milliseconds;
        constexpr int dit_ms  = 1200 / 18;          // 66 ms
        constexpr int dah_ms  = dit_ms * 3;          // 200 ms
        constexpr int gap_ms  = dit_ms;              // inter-symbol gap
        constexpr int cgap_ms = dit_ms * 3;          // inter-character gap

        auto dit = [&](NativeAudioOutputAlsa& a) {
            a.tone_on(700); std::this_thread::sleep_for(ms(dit_ms));
            a.tone_off();   std::this_thread::sleep_for(ms(gap_ms));
        };
        auto dah = [&](NativeAudioOutputAlsa& a) {
            a.tone_on(700); std::this_thread::sleep_for(ms(dah_ms));
            a.tone_off();   std::this_thread::sleep_for(ms(gap_ms));
        };
        auto play_V = [&](NativeAudioOutputAlsa& a) {
            dit(a); dit(a); dit(a); dah(a);
        };

        NativeAudioOutputAlsa audio;
        audio.set_volume(7);
        audio.set_adsr(0.007f, 0.0f, 1.0f, 0.007f);
        audio.begin();

        play_V(audio); std::this_thread::sleep_for(ms(cgap_ms));
        play_V(audio); std::this_thread::sleep_for(ms(cgap_ms));
        play_V(audio);

        audio.suspend();
    }
    printf("Audio: done\n");

    // ── Result ───────────────────────────────────────────────────────────────
    printf("\nResult: %s\n", all_ok ? "ALL PASS" : "FAILURES DETECTED");
    return all_ok ? 0 : 1;
}
