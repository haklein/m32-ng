#ifdef NATIVE_BUILD

#include "audio_output_alsa.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// ── Oscillator ────────────────────────────────────────────────────────────────

void NativeAudioOutputAlsa::Oscillator::set_frequency(float hz, int sr)
{
    float rad = 2.0f * float(M_PI) * hz / float(sr);
    dphase = {std::cos(rad), std::sin(rad)};
    phase  = {1.0f, 0.0f};     // reset phase so clicks are avoided at key-on
}

float NativeAudioOutputAlsa::Oscillator::next()
{
    phase *= dphase;
    return phase.real();
}

// ── Ramp ──────────────────────────────────────────────────────────────────────
// Uses the first half of a Blackman-Harris window for the rising shape and
// the second half for the falling shape, identical to keyed_tone.h.

float NativeAudioOutputAlsa::Ramp::bh_sample(int size, int k)
{
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    double arg = k * 2.0 * M_PI / (size - 1);
    return float(a0 - a1 * std::cos(arg)
                    + a2 * std::cos(2.0 * arg)
                    - a3 * std::cos(3.0 * arg));
}

void NativeAudioOutputAlsa::Ramp::configure(float seconds, int sr)
{
    int n = std::max(1, int(seconds * float(sr)));
    if ((n & 1) == 0) ++n;     // keep odd so midpoint sample is symmetric
    values.resize(n);
    for (int i = 0; i < n; ++i)
        values[i] = bh_sample(2 * n - 1, i);
    current = 0;
}

void  NativeAudioOutputAlsa::Ramp::start_rise() { rising = true;  current = 0; }
void  NativeAudioOutputAlsa::Ramp::start_fall() { rising = false; current = 0; }
bool  NativeAudioOutputAlsa::Ramp::done()  const { return current >= int(values.size()); }

float NativeAudioOutputAlsa::Ramp::next()
{
    float v = (current < int(values.size())) ? values[current++] : 1.0f;
    return rising ? v : (1.0f - v);
}

// ── NativeAudioOutputAlsa ──────────────────────────────────────────────────────

NativeAudioOutputAlsa::NativeAudioOutputAlsa(const char* device)
    : device_name_(device)
{}

NativeAudioOutputAlsa::~NativeAudioOutputAlsa()
{
    suspend();
}

void NativeAudioOutputAlsa::begin()
{
    snd_pcm_t* pcm = nullptr;
    int err = snd_pcm_open(&pcm, device_name_, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "ALSA audio: cannot open '%s': %s\n",
                device_name_, snd_strerror(err));
        return;
    }

    // Set hardware parameters.
    // 50 000 µs (50 ms) target latency gives roughly 4 periods of PERIOD_FRAMES
    // at 48 kHz — comfortable for interactive use without noticeable lag.
    err = snd_pcm_set_params(pcm,
        SND_PCM_FORMAT_FLOAT,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        1,              // mono
        SAMPLE_RATE,
        1,              // allow ALSA software resampling if needed
        50000);         // 50 ms overall latency target
    if (err < 0) {
        fprintf(stderr, "ALSA audio: set_params failed: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return;
    }

    pcm_handle_ = pcm;

    // Initialise DSP state from current atomic settings.
    rise_.configure(attack_s_.load(),  SAMPLE_RATE);
    fall_.configure(release_s_.load(), SAMPLE_RATE);
    osc_.set_frequency(float(current_freq_), SAMPLE_RATE);

    running_ = true;
    audio_thread_ = std::thread(&NativeAudioOutputAlsa::audio_thread_fn, this);
}

void NativeAudioOutputAlsa::tone_on(uint16_t frequency_hz)
{
    pending_freq_.store(frequency_hz);
    pending_cmd_.store(ToneCmd::On);
}

void NativeAudioOutputAlsa::tone_off()
{
    pending_cmd_.store(ToneCmd::Off);
}

void NativeAudioOutputAlsa::set_volume(uint8_t level)
{
    gain_.store(float(std::min(level, uint8_t(20))) / 20.0f);
}

void NativeAudioOutputAlsa::set_adsr(float attack_s, float /*decay_s*/,
                                      float /*sustain_level*/, float release_s)
{
    // For a CW sidetone, decay and sustain_level are not meaningful — the tone
    // stays at full volume while the key is held.  Only attack and release matter.
    attack_s_.store( attack_s  > 0.0f ? attack_s  : 0.001f);
    release_s_.store(release_s > 0.0f ? release_s : 0.001f);
    adsr_dirty_.store(true);
}

void NativeAudioOutputAlsa::play_effect(SoundEffect effect)
{
    if (effect == SoundEffect::SUCCESS) {
        tone_on(440); std::this_thread::sleep_for(std::chrono::milliseconds(97));
        tone_off();   std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tone_on(587); std::this_thread::sleep_for(std::chrono::milliseconds(193));
        tone_off();
    } else {
        tone_on(311); std::this_thread::sleep_for(std::chrono::milliseconds(193));
        tone_off();   std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tone_on(330); std::this_thread::sleep_for(std::chrono::milliseconds(193));
        tone_off();
    }
}

void NativeAudioOutputAlsa::suspend()
{
    if (running_.exchange(false) && audio_thread_.joinable()) {
        audio_thread_.join();
    }
    if (pcm_handle_) {
        snd_pcm_drain(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

// ── Audio thread ──────────────────────────────────────────────────────────────

float NativeAudioOutputAlsa::generate_sample()
{
    // Process any pending command from the app thread.
    ToneCmd cmd = pending_cmd_.exchange(ToneCmd::None);
    if (cmd == ToneCmd::On) {
        uint16_t f = pending_freq_.load();
        if (f != current_freq_) {
            current_freq_ = f;
            osc_.set_frequency(float(f), SAMPLE_RATE);
        }
        state_ = ToneState::Rise;
        rise_.start_rise();
    } else if (cmd == ToneCmd::Off && state_ != ToneState::Off) {
        state_ = ToneState::Fall;
        fall_.start_fall();
    }

    float scale = gain_.load();
    switch (state_) {
    case ToneState::Off:
        return 0.0f;
    case ToneState::Rise:
        scale *= rise_.next();
        if (rise_.done()) state_ = ToneState::On;
        break;
    case ToneState::On:
        break;
    case ToneState::Fall:
        scale *= fall_.next();
        if (fall_.done()) state_ = ToneState::Off;
        break;
    }
    return scale * osc_.next();
}

void NativeAudioOutputAlsa::audio_thread_fn()
{
    float buf[PERIOD_FRAMES];

    while (running_) {
        // Rebuild ramps if set_adsr() was called.
        if (adsr_dirty_.exchange(false)) {
            rise_.configure(attack_s_.load(),  SAMPLE_RATE);
            fall_.configure(release_s_.load(), SAMPLE_RATE);
        }

        for (int i = 0; i < PERIOD_FRAMES; ++i)
            buf[i] = generate_sample();

        snd_pcm_sframes_t n = snd_pcm_writei(pcm_handle_, buf, PERIOD_FRAMES);
        if (n < 0) {
            // Recover from underrun or suspend.
            n = snd_pcm_recover(pcm_handle_, n, /*silent=*/0);
            if (n < 0)
                fprintf(stderr, "ALSA audio: write error: %s\n", snd_strerror(n));
        }
    }
}

#endif // NATIVE_BUILD
