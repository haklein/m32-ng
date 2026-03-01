#pragma once

#ifdef NATIVE_BUILD

// Native HAL: sidetone output via ALSA PCM.
//
// Audio is generated in a background thread.  The oscillator is a complex
// rotor (phase *= dphase each sample) — numerically stable and cheap.
// Attack and release envelopes use a Blackman-Harris window half, matching
// the approach in haklein/iambic-keyer / keyed_tone.h.
//
// Thread safety: tone_on/off, set_volume and set_adsr may be called from any
// thread; they communicate with the audio thread via atomics.

#include "../interfaces/i_audio_output.h"

#include <alsa/asoundlib.h>
#include <atomic>
#include <complex>
#include <thread>
#include <vector>

class NativeAudioOutputAlsa : public IAudioOutput
{
public:
    // device: ALSA PCM device string, e.g. "default" or "hw:0,0"
    explicit NativeAudioOutputAlsa(const char* device = "default");
    ~NativeAudioOutputAlsa() override;

    void begin() override;
    void tone_on(uint16_t frequency_hz) override;
    void tone_off() override;
    void set_volume(uint8_t level) override;   // 0 = mute, 20 = full
    void set_adsr(float attack_s, float decay_s,
                  float sustain_level, float release_s) override;
    void suspend() override;
    void play_effect(SoundEffect effect) override;

private:
    // ── Complex-rotor oscillator ────────────────────────────────────────────
    struct Oscillator {
        std::complex<float> phase{1.0f, 0.0f};
        std::complex<float> dphase{1.0f, 0.0f};
        void set_frequency(float hz, int sample_rate);
        float next();   // returns real part (one audio sample)
    };

    // ── Blackman-Harris windowed ramp ───────────────────────────────────────
    struct Ramp {
        std::vector<float> values;
        int  current = 0;
        bool rising  = true;

        void configure(float seconds, int sample_rate);
        void start_rise();
        void start_fall();
        bool done() const;
        float next();

    private:
        static float bh_sample(int window_size, int k);
    };

    // ── Tone state machine ──────────────────────────────────────────────────
    enum class ToneCmd   : int { None = 0, On = 1, Off = 2 };
    enum class ToneState     { Off, Rise, On, Fall };

    float generate_sample();
    void  audio_thread_fn();

    // ── Configuration ───────────────────────────────────────────────────────
    const char* device_name_;

    static constexpr int SAMPLE_RATE   = 48000;
    static constexpr int PERIOD_FRAMES = 256;   // ≈5.3 ms per callback

    // ── ALSA handle ─────────────────────────────────────────────────────────
    snd_pcm_t*  pcm_handle_ = nullptr;

    // ── Audio thread ─────────────────────────────────────────────────────────
    std::thread       audio_thread_;
    std::atomic<bool> running_{false};

    // ── Cross-thread tone control ────────────────────────────────────────────
    std::atomic<ToneCmd>    pending_cmd_{ToneCmd::None};
    std::atomic<uint16_t>   pending_freq_{700};

    // ── Per-sample state (audio thread only) ────────────────────────────────
    ToneState    state_        = ToneState::Off;
    uint16_t     current_freq_ = 700;
    Oscillator   osc_;
    Ramp         rise_;
    Ramp         fall_;

    // ── Cross-thread ADSR / volume ───────────────────────────────────────────
    std::atomic<float> gain_{0.5f};
    std::atomic<float> attack_s_{0.005f};
    std::atomic<float> release_s_{0.005f};
    std::atomic<bool>  adsr_dirty_{false};
};

#endif // NATIVE_BUILD
