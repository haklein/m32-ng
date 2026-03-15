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
    sidetone_.setADSR(0.007f, 0.0f, 1.0f, 0.007f);
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
    //
    // Output common-mode voltage: 1.65V (default 1.35V is too low for
    // low-impedance headphones — can cause oscillation / HF noise).
    codec_.modifyRegister(AIC31XX_HPDRIVER, AIC31XX_HPD_OCMV_MASK,
                          AIC31XX_HPD_OCMV_1_65V << AIC31XX_HPD_OCMV_SHIFT);
    // HP performance mode: highest (bits 4-3 = 11) — best THD+N / noise floor.
    codec_.modifyRegister(AIC31XX_HPCONTROL, AIC31XX_HPCONTROL_PERFORMANCE_MASK,
                          0x3 << 3);
    // POP removal: D7=1 (optimized power-down sequence), D6-D3=0111 (304ms
    // power-on time), D2-D1=11 (3.9ms ramp step), D0=0 (AVDD divider CM).
    // Value: 1_0111_110 = 0xBE
    codec_.writeRegister(AIC31XX_HPPOP, 0xBE);
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
        // Debounce: ignore events within 500ms of the last one (WiFi can
        // cause GPIO noise on the codec INT pin, triggering spurious IRQs).
        static unsigned long last_hp_event = 0;
        unsigned long now = millis();
        if (now - last_hp_event < 500) return;
        last_hp_event = now;
        handle_headset_event();
    }
}

void PocketAudioOutput::handle_headset_event()
{
    // Reading the sticky interrupt flag is required — the codec will not fire
    // further INT1 pulses until the flag register has been read.
    uint8_t flags = codec_.readRegister(AIC31XX_INTRDACFLAG);
    if (flags == 0) return;  // spurious IRQ (WiFi GPIO noise) — nothing to do
    Serial.printf("AIC31XX: INT flags=0x%02X\n", flags);

    if (codec_.isHeadsetDetected()) {
        Serial.println("AIC31XX: Headset detected — HP on, SPK power down");
        codec_.setHeadphoneMute(false);
        codec_.setSpeakerMute(true);
        // Fully power down class-D amp to eliminate switching noise coupling
        // into headphone output.
        codec_.modifyRegister(AIC31XX_SPKAMP, AIC3100_SPKAMP_POWER_MASK, 0x0);
    } else {
        Serial.println("AIC31XX: Headset removed — SPK on, HP off");
        codec_.enableSpeakerAmp();
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

// ── Codec re-init (WiFi brown-out recovery) ──────────────────────────────────

void PocketAudioOutput::reinit_codec()
{
    // Diagnostic: read PLL J register — our config writes 16 (0x10),
    // power-on default is 4 (0x04).  If 0x04, codec has been reset.
    uint8_t pll_j = codec_.readRegister(AIC31XX_PLLJ);
    Serial.printf("AIC31XX: reinit — PLL_J=0x%02X (%s)\n",
                  pll_j, (pll_j == 0x10) ? "config intact" : "CODEC RESET");

    // ── Hardware reset ──────────────────────────────────────────────────────
    digitalWrite(CONFIG_TLV320AIC3100_RST, LOW);
    delay(5);
    digitalWrite(CONFIG_TLV320AIC3100_RST, HIGH);
    delay(5);

    // ── Software init + PLL pre-configuration ───────────────────────────────
    codec_.begin();
    codec_.reset();
    codec_init_clocking();

    // ── Enable PLL (BCLK already running from I2S) ─────────────────────────
    codec_.setPLLPower(true);
    delay(20);

    codec_.setNDACVal(4);  codec_.setNDACPower(true);
    codec_.setMDACVal(4);  codec_.setMDACPower(true);
    codec_.setDOSRVal(128);

    // ── DAC ─────────────────────────────────────────────────────────────────
    codec_.setWordLength(16);
    codec_.enableDAC();
    codec_.setDACVolume(-2.0f, -2.0f);
    codec_.setDACMute(false);

    codec_enable_outputs();

    // ── Headset detect (ISR already attached, just re-enable codec side) ────
    codec_.modifyRegister(AIC31XX_TIMERDIVIDER, AIC31XX_TIMER_SELECT_MASK, 0);
    codec_.modifyRegister(AIC31XX_TIMERDIVIDER, 0x3F, 0x10);
    codec_.enableHeadsetDetect();
    codec_.setHSDetectInt1(true);
    delay(50);
    handle_headset_event();

    Serial.println("AIC31XX: full codec reinit complete");
}

// ── ADC input for CW decoder ─────────────────────────────────────────────────

void PocketAudioOutput::enable_adc()
{
    // ADC clocking: share PLL with DAC.
    // ADC_FS = PLL_CLK / (NADC × MADC × AOSR) = 98304000 / (4 × 4 × 128) = 48000 Hz
    codec_.setNADCVal(4);   codec_.setNADCPower(true);
    codec_.setMADCVal(4);   codec_.setMADCPower(true);
    codec_.setAOSRVal(128);

    // Input routing: MIC1RP → P-terminal via 10 kΩ (single-ended from TRRS mic ring).
    // Page 1, Reg 48: bits 5:4 = 01 → MIC1RP routed to P-terminal @ 10 kΩ
    codec_.writeRegister(AIC31XX_MICPGAPI, 0x10);
    // Page 1, Reg 49: leave at default (M-terminal not connected — single-ended input)
    codec_.writeRegister(AIC31XX_MICPGAMI, 0x00);

    // MIC PGA: 0 dB gain — matches original Morserino v6.
    // Noise blanking in the decoder task handles cable noise.
    codec_.setMicPGAEnable(true);

    // No AGC — fixed gain preserves the signal-to-noise ratio needed for
    // Goertzel tone detection.  AGC compresses dynamic range and amplifies
    // the noise floor, making silence indistinguishable from tone.

    // Enable ADC digital path and unmute.
    // -12 dB ADC gain — matches original Morserino v6.
    codec_.enableADC();
    codec_.setADCGain(-12.0f);

    Serial.println("AIC31XX: ADC enabled (no AGC, MIC PGA 0 dB, ADC -12 dB)");
}

void PocketAudioOutput::disable_adc()
{
    // Disable AGC
    codec_.writeRegister(AIC31XX_REG(0, 86), 0x00);
    // Mute and power down ADC
    codec_.modifyRegister(AIC31XX_ADCFGA, AIC31XX_ADC_MUTE_MASK, 0x1);
    codec_.modifyRegister(AIC31XX_ADCSETUP, AIC31XX_ADC_POWER_MASK, 0x0);
    // Disable MIC PGA
    codec_.setMicPGAEnable(false);
    // Clear input routing
    codec_.writeRegister(AIC31XX_MICPGAPI, 0x00);
    codec_.writeRegister(AIC31XX_MICPGAMI, 0x00);
    // Power down ADC clock dividers
    codec_.setNADCPower(false);
    codec_.setMADCPower(false);

    Serial.println("AIC31XX: ADC disabled");
}

size_t PocketAudioOutput::read_audio(int16_t* buf, size_t max_samples)
{
    return sidetone_.readBytes((uint8_t*)buf, max_samples * sizeof(int16_t))
           / sizeof(int16_t);
}

#endif // BOARD_POCKETWROOM
