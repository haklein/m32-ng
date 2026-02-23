#ifdef BOARD_POCKETWROOM

#include "audio_output.h"
#include <Arduino.h>

// ── PLL constants for 48 kHz operation ───────────────────────────────────────
//
// BCLK  = sample_rate × bits × channels = 48000 × 16 × 2 = 1 536 000 Hz
// Target PLL output: 98.304 MHz (within the required 80–110 MHz window)
//   P=1, R=4, J=16, D=0  →  PLL_CLK = 1.536 × 4 × 16 = 98.304 MHz
//
// NDAC=4, MDAC=4, DOSR=128
//   DAC_FS = PLL_CLK / (NDAC × MDAC × DOSR) = 98 304 000 / 2048 = 48 000 Hz ✓

static constexpr int    SAMPLE_RATE  = 48000;
static constexpr int    BUFFER_SIZE  = 32;      // I2S_Sidetone buffer_count
static constexpr float  DEFAULT_FREQ = 700.0f;
static constexpr float  DEFAULT_VOL  = 0.5f;

PocketAudioOutput::PocketAudioOutput()
    : codec_(&Wire)
{}

void PocketAudioOutput::begin()
{
    // ── 1. I2C bus for codec ─────────────────────────────────────────────────
    Wire.begin(CONFIG_TLV320AIC3100_SDA, CONFIG_TLV320AIC3100_SCL);

    // ── 2. Hardware reset ────────────────────────────────────────────────────
    pinMode(CONFIG_TLV320AIC3100_RST, OUTPUT);
    digitalWrite(CONFIG_TLV320AIC3100_RST, LOW);
    delay(5);
    digitalWrite(CONFIG_TLV320AIC3100_RST, HIGH);
    delay(5);

    // ── 3. Software init + PLL pre-configuration ─────────────────────────────
    codec_.begin();
    codec_.reset();
    codec_init_clocking();      // writes PLL params; PLL not powered yet

    // ── 4. Start I2S → BCLK begins ──────────────────────────────────────────
    // CONFIG_I2S_BCK/LRCK/DATA pins are consumed from build_flags by the lib.
    sidetone_.begin(SAMPLE_RATE, 16, 2, BUFFER_SIZE);

    // ── 5. Enable codec PLL (now that BCLK is present) ──────────────────────
    codec_.setPLLPower(true);
    delay(20);  // allow PLL to lock (20 ms per reference design)

    codec_.setNDACVal(4);  codec_.setNDACPower(true);
    codec_.setMDACVal(4);  codec_.setMDACPower(true);
    codec_.setDOSRVal(128);

    // ── 6. Enable DAC and output drivers ─────────────────────────────────────
    codec_.setWordLength(16);
    codec_.enableDAC();
    codec_.setDACVolume(-2.0f, -2.0f);  // slight headroom below clipping
    codec_.setDACMute(false);           // default after reset is muted

    codec_enable_outputs();

    // ── 7. Sidetone defaults ─────────────────────────────────────────────────
    sidetone_.setFrequency(DEFAULT_FREQ);
    sidetone_.setVolume(DEFAULT_VOL);
    sidetone_.setADSR(0.005f, 0.0f, 1.0f, 0.005f);
}

void PocketAudioOutput::codec_init_clocking()
{
    // Route BCLK → PLL → CODEC_CLKIN.
    // setCLKMUX(pll_clkin, codec_clkin):
    //   pll_clkin  1 = BCLK
    //   codec_clkin 3 = PLL_CLK
    codec_.setCLKMUX(1, 3);
    codec_.setPLL(/*P=*/1, /*R=*/4, /*J=*/16, /*D=*/0);
}

void PocketAudioOutput::codec_enable_outputs()
{
    // Headphone amp — used with wired headset.
    // HPLGAIN/HPRGAIN mute bit (BIT(2)) defaults to 0 = muted after reset;
    // setHeadphoneMute(false) must be called explicitly to unmute.
    codec_.enableHeadphoneAmp();
    codec_.setHeadphoneVolume(-6.0f, -6.0f);
    codec_.setHeadphoneGain(0.0f, 0.0f);
    codec_.setHeadphoneMute(false);

    // Speaker amp — class-D via HP driver on Pocket board.
    // SPLGAIN mute bit likewise defaults to muted.
    codec_.enableSpeakerAmp();
    codec_.setSpeakerGain(6.0f);
    codec_.setSpeakerVolume(-6.0f);
    codec_.setSpeakerMute(false);
}

// ── IAudioOutput ──────────────────────────────────────────────────────────────

void PocketAudioOutput::tone_on(uint16_t frequency_hz)
{
    sidetone_.setFrequency(float(frequency_hz));
    sidetone_.on();
}

void PocketAudioOutput::tone_off()
{
    sidetone_.off();
}

void PocketAudioOutput::set_volume(uint8_t level_0_to_10)
{
    if (level_0_to_10 == 0) {
        codec_.setDACMute(true);
        return;
    }
    codec_.setDACMute(false);
    // Map 1..10 to 0.1..1.0 linear volume fed to VolumeStream.
    sidetone_.setVolume(float(level_0_to_10) / 10.0f);
}

void PocketAudioOutput::set_adsr(float attack_s, float decay_s,
                                   float sustain_level, float release_s)
{
    sidetone_.setADSR(attack_s, decay_s, sustain_level, release_s);
}

void PocketAudioOutput::suspend()
{
    sidetone_.off();
    delay(20);  // let release ramp finish
    codec_.powerDown();
}

#endif // BOARD_POCKETWROOM
