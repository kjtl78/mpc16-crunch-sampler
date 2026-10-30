#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <cmath>

namespace mpc16
{

static constexpr int kNumPads = 16;
static constexpr int kMaxVoices = 32;

// Immutable audio buffer container shared via std::shared_ptr between UI and Audio thread
struct SampleBufferHolder
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String name = "Empty";
    juce::String filePath;
};

enum class StretchMode : int
{
    ClassicRepitch = 0,   // Classic hardware sampler: pitch & speed coupled + optional time-stretch
    GranularTimeStretch   // Independent pitch shift & granular overlap-add time-stretching
};

enum class VintageMode : int
{
    Clean = 0,
    MPC60_12Bit,   // 12-bit, ~40 kHz with warm saturation
    SP1200_12Bit   // 12-bit, ~26.04 kHz with gritty sample-and-hold aliasing
};

struct PadParams
{
    // Sample identification
    std::shared_ptr<const SampleBufferHolder> sample;
    std::atomic<int> midiNote { 36 };        // Default C1 (36) .. D#2 (51)
    std::atomic<int> chokeGroup { 0 };       // 0 = Off, 1..4 = Choke group
    std::atomic<bool> oneShot { true };      // true = plays full envelope on trigger; false = gated by note-off
    std::atomic<bool> reverse { false };     // Reverse playback

    // Trim Points (normalized 0..1)
    std::atomic<float> startPoint { 0.0f };
    std::atomic<float> endPoint { 1.0f };

    // Pitch & Time Stretch
    std::atomic<int> stretchMode { static_cast<int>(StretchMode::GranularTimeStretch) };
    std::atomic<float> pitchSemitones { 0.0f };   // -24.0 to +24.0 st
    std::atomic<float> fineTuneCents { 0.0f };    // -100.0 to +100.0 cents
    std::atomic<float> timeStretch { 1.0f };      // 0.5x (faster) to 2.0x (slower)
    std::atomic<float> grainSizeMs { 35.0f };     // 10.0 to 100.0 ms grain window for granular time-stretch

    // Bitcrusher / Sample Rate Decimator / Drive
    std::atomic<float> bitDepth { 16.0f };        // 1.0 to 16.0 bits (16 = bypass quantizer)
    std::atomic<float> downsampleFactor { 1.0f }; // 1.0 to 64.0x sample-and-hold reduction
    std::atomic<float> driveDb { 0.0f };          // 0.0 to 24.0 dB saturation drive
    std::atomic<float> crushMix { 1.0f };         // 0.0 (dry) to 1.0 (wet)

    // Amplitude & ADSR Envelope
    std::atomic<float> attackMs { 1.0f };         // 0.5 to 2000.0 ms
    std::atomic<float> decayMs { 600.0f };        // 5.0 to 5000.0 ms
    std::atomic<float> sustainLevel { 1.0f };     // 0.0 to 1.0
    std::atomic<float> releaseMs { 80.0f };       // 5.0 to 5000.0 ms
    std::atomic<float> levelDb { 0.0f };          // -60.0 to +12.0 dB
    std::atomic<float> pan { 0.0f };              // -1.0 (L) to +1.0 (R)

    // Real-time UI visual feedback atomics (written by audio thread, read by UI timer)
    std::atomic<float> playheadNormalized { -1.0f }; // -1.0 when idle, 0..1 when playing
    std::atomic<float> triggerActivity { 0.0f };     // 0..1 velocity flash for UI pad grid
};

// Procedural 909 / MPC drum synthesizer so all 16 pads are playable out of the box
class FactoryDrumBuilder
{
public:
    static std::shared_ptr<SampleBufferHolder> createFactoryDrumSample(int padIndex, double sampleRate = 44100.0)
    {
        auto holder = std::make_shared<SampleBufferHolder>();
        holder->sampleRate = sampleRate;

        struct PadPresetInfo
        {
            const char* name;
            float durationSec;
        };

        static const PadPresetInfo presets[kNumPads] = {
            { "01 Punch Kick",     0.48f },
            { "02 Sub 808 Kick",   0.75f },
            { "03 Crispy Snare",   0.38f },
            { "04 MPC Clap",       0.40f },
            { "05 Tight Rimshot",  0.16f },
            { "06 Closed Hat",     0.12f },
            { "07 Pedal Hat",      0.18f },
            { "08 Open Hat",       0.55f },
            { "09 Low Tom",        0.45f },
            { "10 Mid Tom",        0.40f },
            { "11 High Tom",       0.35f },
            { "12 808 Cowbell",    0.32f },
            { "13 Shaker",         0.20f },
            { "14 Crash Cymbal",   1.10f },
            { "15 Lo-Fi Chord",    0.85f },
            { "16 Vinyl Stab",     0.65f }
        };

        const int idx = juce::jlimit(0, kNumPads - 1, padIndex);
        holder->name = presets[idx].name;
        const int numSamples = static_cast<int>(presets[idx].durationSec * sampleRate);
        holder->buffer.setSize(2, numSamples);
        holder->buffer.clear();

        auto* left  = holder->buffer.getWritePointer(0);
        auto* right = holder->buffer.getWritePointer(1);

        juce::Random rng(1337 + idx * 97);
        double phase = 0.0;
        double phase2 = 0.0;
        double phase3 = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            switch (idx)
            {
                case 0: // Punch Kick
                {
                    double freq = 48.0 + 140.0 * std::exp(-t * 38.0);
                    phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                    float env = static_cast<float>(std::exp(-t * 8.5));
                    float click = static_cast<float>(std::exp(-t * 180.0)) * (rng.nextFloat() * 2.0f - 1.0f) * 0.4f;
                    sampleL = sampleR = std::tanh((static_cast<float>(std::sin(phase)) * env + click) * 1.4f);
                    break;
                }
                case 1: // Sub 808 Kick
                {
                    double freq = 38.0 + 95.0 * std::exp(-t * 28.0);
                    phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                    float env = static_cast<float>(std::exp(-t * 4.2));
                    sampleL = sampleR = std::tanh(static_cast<float>(std::sin(phase)) * env * 1.3f);
                    break;
                }
                case 2: // Crispy Snare
                {
                    double freq = 175.0 + 90.0 * std::exp(-t * 45.0);
                    phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                    float body = static_cast<float>(std::sin(phase) * std::exp(-t * 18.0));
                    float snares = (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 11.5));
                    sampleL = std::tanh((body * 0.65f + snares * 0.75f) * 1.2f);
                    sampleR = std::tanh((body * 0.65f + (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 11.5)) * 0.75f) * 1.2f);
                    break;
                }
                case 3: // MPC Clap
                {
                    // Multi-burst envelope for classic clap
                    float burstEnv = 0.0f;
                    if (t < 0.010) burstEnv = static_cast<float>( std::exp(-t * 180.0));
                    else if (t < 0.020) burstEnv = static_cast<float>(std::exp(-(t - 0.010) * 180.0));
                    else if (t < 0.032) burstEnv = static_cast<float>(std::exp(-(t - 0.020) * 180.0));
                    else burstEnv = static_cast<float>(std::exp(-(t - 0.032) * 10.5));

                    float nL = (rng.nextFloat() * 2.0f - 1.0f) * burstEnv;
                    float nR = (rng.nextFloat() * 2.0f - 1.0f) * burstEnv;
                    phase += juce::MathConstants<double>::twoPi * 1150.0 / sampleRate;
                    float bandMod = 0.65f + 0.35f * static_cast<float>(std::sin(phase));
                    sampleL = std::tanh(nL * bandMod * 1.4f);
                    sampleR = std::tanh(nR * bandMod * 1.4f);
                    break;
                }
                case 4: // Tight Rimshot
                {
                    phase  += juce::MathConstants<double>::twoPi * 460.0 / sampleRate;
                    phase2 += juce::MathConstants<double>::twoPi * 1680.0 / sampleRate;
                    float env = static_cast<float>(std::exp(-t * 36.0));
                    float s = static_cast<float>(std::sin(phase) * 0.6 + std::sin(phase2) * 0.5) * env;
                    sampleL = sampleR = std::tanh(s * 1.8f);
                    break;
                }
                case 5: // Closed Hat
                case 6: // Pedal Hat
                case 7: // Open Hat
                {
                    double decayRate = (idx == 5) ? 42.0 : (idx == 6 ? 26.0 : 6.5);
                    float env = static_cast<float>(std::exp(-t * decayRate));
                    // Metallic harmonic cluster + high-frequency noise
                    phase  += juce::MathConstants<double>::twoPi * 3420.0 / sampleRate;
                    phase2 += juce::MathConstants<double>::twoPi * 5180.0 / sampleRate;
                    phase3 += juce::MathConstants<double>::twoPi * 7830.0 / sampleRate;
                    float metal = static_cast<float>((std::sin(phase) > 0 ? 0.3 : -0.3)
                                                   + (std::sin(phase2) > 0 ? 0.3 : -0.3)
                                                   + (std::sin(phase3) > 0 ? 0.3 : -0.3));
                    float noiseL = (rng.nextFloat() * 2.0f - 1.0f);
                    float noiseR = (rng.nextFloat() * 2.0f - 1.0f);
                    sampleL = (metal * 0.35f + noiseL * 0.65f) * env * 0.8f;
                    sampleR = (metal * 0.35f + noiseR * 0.65f) * env * 0.8f;
                    break;
                }
                case 8:  // Low Tom
                case 9:  // Mid Tom
                case 10: // High Tom
                {
                    double baseFreq = (idx == 8) ? 85.0 : (idx == 9 ? 125.0 : 175.0);
                    double freq = baseFreq + baseFreq * 0.8 * std::exp(-t * 22.0);
                    phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
                    float env = static_cast<float>(std::exp(-t * 9.0));
                    sampleL = sampleR = std::tanh(static_cast<float>(std::sin(phase)) * env * 1.2f);
                    break;
                }
                case 11: // 808 Cowbell
                {
                    phase  += juce::MathConstants<double>::twoPi * 540.0 / sampleRate;
                    phase2 += juce::MathConstants<double>::twoPi * 800.0 / sampleRate;
                    float env = static_cast<float>(std::exp(-t * 14.0));
                    float sq1 = std::sin(phase) >= 0.0 ? 0.5f : -0.5f;
                    float sq2 = std::sin(phase2) >= 0.0 ? 0.5f : -0.5f;
                    sampleL = sampleR = std::tanh((sq1 + sq2) * env * 0.9f);
                    break;
                }
                case 12: // Shaker
                {
                    float env = static_cast<float>(std::sin(juce::MathConstants<double>::pi * std::min(1.0, t / 0.18)) * std::exp(-t * 10.0));
                    sampleL = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.7f;
                    sampleR = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.7f;
                    break;
                }
                case 13: // Crash Cymbal
                {
                    float env = static_cast<float>(std::exp(-t * 3.2));
                    phase  += juce::MathConstants<double>::twoPi * 2960.0 / sampleRate;
                    phase2 += juce::MathConstants<double>::twoPi * 4670.0 / sampleRate;
                    float shimmer = static_cast<float>(std::sin(phase) * std::cos(phase2));
                    sampleL = ((rng.nextFloat() * 2.0f - 1.0f) * 0.7f + shimmer * 0.3f) * env * 0.8f;
                    sampleR = ((rng.nextFloat() * 2.0f - 1.0f) * 0.7f - shimmer * 0.3f) * env * 0.8f;
                    break;
                }
                case 14: // Lo-Fi Minor 9th Rhodes Chord (Great for testing Pitch & Time-Stretch!)
                {
                    // C minor 9 chord: C4, Eb4, G4, Bb4, D5
                    const double freqs[5] = { 261.63, 311.13, 392.00, 466.16, 587.33 };
                    float chordSumL = 0.0f;
                    float chordSumR = 0.0f;
                    for (int n = 0; n < 5; ++n)
                    {
                        double p = juce::MathConstants<double>::twoPi * freqs[n] * t;
                        float tine = static_cast<float>(std::sin(p * 3.0) * std::exp(-t * 18.0) * 0.25);
                        float voice = static_cast<float>(std::sin(p + tine)) * 0.22f;
                        chordSumL += voice * (n % 2 == 0 ? 1.0f : 0.7f);
                        chordSumR += voice * (n % 2 == 1 ? 1.0f : 0.7f);
                    }
                    float env = static_cast<float>(std::exp(-t * 3.5));
                    float vinylCrack = (rng.nextFloat() > 0.992f) ? (rng.nextFloat() - 0.5f) * 0.25f : 0.0f;
                    sampleL = std::tanh(chordSumL * env * 1.4f + vinylCrack);
                    sampleR = std::tanh(chordSumR * env * 1.4f + vinylCrack);
                    break;
                }
                case 15: // Vinyl Brass/Synth Stab (Also awesome for Time-Stretch & Bitcrusher!)
                {
                    // F minor 7 stab with filter-like harmonic decay
                    const double freqs[4] = { 174.61, 261.63, 311.13, 415.30 };
                    float sumL = 0.0f;
                    float sumR = 0.0f;
                    float harmDecay = static_cast<float>(std::exp(-t * 12.0));
                    for (int n = 0; n < 4; ++n)
                    {
                        double p1 = juce::MathConstants<double>::twoPi * freqs[n] * t;
                        double p2 = juce::MathConstants<double>::twoPi * (freqs[n] * 1.005) * t;
                        float s1 = static_cast<float>(std::sin(p1) + 0.5 * harmDecay * std::sin(2.0 * p1) + 0.25 * harmDecay * std::sin(3.0 * p1));
                        float s2 = static_cast<float>(std::sin(p2) + 0.5 * harmDecay * std::sin(2.0 * p2) + 0.25 * harmDecay * std::sin(3.0 * p2));
                        sumL += s1 * 0.22f;
                        sumR += s2 * 0.22f;
                    }
                    float env = static_cast<float>(std::exp(-t * 4.8));
                    sampleL = std::tanh(sumL * env * 1.3f);
                    sampleR = std::tanh(sumR * env * 1.3f);
                    break;
                }
            }

            // Apply quick 2ms fade-in and 5ms fade-out to guarantee zero clicks
            float fade = 1.0f;
            const int fadeInSamples = static_cast<int>(0.0015 * sampleRate);
            const int fadeOutSamples = static_cast<int>(0.005 * sampleRate);
            if (i < fadeInSamples && fadeInSamples > 0)
                fade = static_cast<float>(i) / static_cast<float>(fadeInSamples);
            else if (i > numSamples - fadeOutSamples && fadeOutSamples > 0)
                fade = static_cast<float>(numSamples - i) / static_cast<float>(fadeOutSamples);

            left[i]  = sampleL * fade;
            right[i] = sampleR * fade;
        }

        return holder;
    }
};

} // namespace mpc16
