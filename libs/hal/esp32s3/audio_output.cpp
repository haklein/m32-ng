#ifdef BOARD_POCKETWROOM

#include "audio_output.h"
#include <Arduino.h>
#include <tlv320aic31xx_regs.h>

// ── Headset detect ISR ───────────────────────────────────────────────────────
// Codec INT1 (GPIO1) is wired to ESP32-S3 GPIO CONFIG_TLV320AIC3100_INT.
// It goes low when a headset plug/unplug event occurs and stays low until
// the sticky interrupt flag register (AIC31XX_INTRDACFLAG) is read.
static volatile bool s_hp_interrupt = false;
static void IRAM_ATTR isr_headset_detect() { s_hp_interrupt = true; }

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

    // ── 7. Headset detection ──────────────────────────────────────────────────
    // When deriving MCLK from BCLK (no external MCLK), the internal timer
    // clock must be enabled explicitly — headset detection uses it for
    // debouncing and will silently fail without it.
    codec_.modifyRegister(AIC31XX_TIMERDIVIDER, AIC31XX_TIMER_SELECT_MASK, 0);
    codec_.modifyRegister(AIC31XX_TIMERDIVIDER, 0x3F, 0x10);

    codec_.enableHeadsetDetect();
    codec_.setHSDetectInt1(true);
    pinMode(CONFIG_TLV320AIC3100_INT, INPUT);
    attachInterrupt(digitalPinToInterrupt(CONFIG_TLV320AIC3100_INT),
                    isr_headset_detect, RISING);
    delay(50);   // allow debounce timer to settle
    handle_headset_event();   // initial check — mute/unmute based on plug state

    // ── 8. Sidetone defaults ─────────────────────────────────────────────────
    sidetone_.setFrequency(DEFAULT_FREQ);
    sidetone_.setVolume(1.0f);   // full amplitude — volume via codec analog stage
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
    current_freq_ = frequency_hz;
    sidetone_.setFrequency(float(frequency_hz));
    sidetone_.on();
}

void PocketAudioOutput::tone_off()
{
    sidetone_.off();
}

void PocketAudioOutput::set_volume(uint8_t level)
{
    if (level == 0) {
        codec_.setDACMute(true);
        return;
    }
    codec_.setDACMute(false);
    // Map 1..20 to -38..0 dB on codec headphone + speaker analog volume.
    // 2 dB per step; linear in dB = perceptually logarithmic.
    uint8_t clamped = std::min(level, uint8_t(20));
    float dB = (float(clamped) - 20.0f) * 2.0f;
    codec_.setHeadphoneVolume(dB, dB);
    codec_.setSpeakerVolume(dB);
}

void PocketAudioOutput::set_adsr(float attack_s, float decay_s,
                                   float sustain_level, float release_s)
{
    sidetone_.setADSR(attack_s, decay_s, sustain_level, release_s);
}

void PocketAudioOutput::poll()
{
    if (s_hp_interrupt) {
        s_hp_interrupt = false;
        handle_headset_event();
    }
}

void PocketAudioOutput::handle_headset_event()
{
    // Reading the sticky interrupt flag is required — the codec will not fire
    // further INT1 pulses until the flag register has been read.
    uint8_t flags = codec_.readRegister(AIC31XX_INTRDACFLAG);
    Serial.printf("AIC31XX: INT flags=0x%02X\n", flags);

    if (codec_.isHeadsetDetected()) {
        Serial.println("AIC31XX: Headset detected — HP on, SPK off");
        codec_.setHeadphoneMute(false);
        codec_.setSpeakerMute(true);
    } else {
        Serial.println("AIC31XX: Headset removed — SPK on, HP off");
        codec_.setSpeakerMute(false);
        codec_.setHeadphoneMute(true);
    }
}

void PocketAudioOutput::play_effect(SoundEffect effect)
{
    const char* path = (effect == SoundEffect::SUCCESS)
                       ? "/sounds/success.mp3"
                       : "/sounds/error.mp3";
    if (!sidetone_.playSPIFFSFile(path)) {
        // Tone fallback (matches original Morserino MorseOutput)
        if (effect == SoundEffect::SUCCESS) {
            sidetone_.setFrequency(440.0f); sidetone_.on(); delay(97);
            sidetone_.off(); delay(20);
            sidetone_.setFrequency(587.0f); sidetone_.on(); delay(193);
            sidetone_.off();
        } else {
            sidetone_.setFrequency(311.0f); sidetone_.on(); delay(193);
            sidetone_.off(); delay(20);
            sidetone_.setFrequency(330.0f); sidetone_.on(); delay(193);
            sidetone_.off();
        }
    }
    // Restore CW sidetone frequency
    sidetone_.setFrequency(float(current_freq_));
}

void PocketAudioOutput::suspend()
{
    sidetone_.off();
    delay(20);  // let release ramp finish
    codec_.powerDown();
}

#endif // BOARD_POCKETWROOM
