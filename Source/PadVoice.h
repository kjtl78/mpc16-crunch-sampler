#pragma once

#include "PadData.h"

namespace mpc16
{

class PadVoice
{
public:
    void startVoice(int padIdx,
                    const PadParams& pad,
                    float velocityNormalized,
                    float extraPitchSemitones,
                    double hostSampleRate)
    {
        sampleHolder = pad.sample;
        if (!sampleHolder || sampleHolder->buffer.getNumSamples() < 4)
        {
            active = false;
            return;
        }

        padIndex = padIdx;
        chokeGroup = pad.chokeGroup.load(std::memory_order_relaxed);
        oneShot = pad.oneShot.load(std::memory_order_relaxed);
        reverse = pad.reverse.load(std::memory_order_relaxed);
        velocity = juce::jlimit(0.05f, 1.0f, velocityNormalized);
        extraPitch = extraPitchSemitones;
        currentHostSampleRate = (hostSampleRate > 0.0) ? hostSampleRate : 44100.0;

        const int totalFrames = sampleHolder->buffer.getNumSamples();
        float startNorm = juce::jlimit(0.0f, 0.99f, pad.startPoint.load(std::memory_order_relaxed));
        float endNorm   = juce::jlimit(startNorm + 0.01f, 1.0f, pad.endPoint.load(std::memory_order_relaxed));

        startFrame = startNorm * static_cast<double>(totalFrames - 1);
        endFrame   = endNorm   * static_cast<double>(totalFrames - 1);

        playheadFrame = reverse ? endFrame : startFrame;
        grainPhase = 0.0;
        grainAOffset = 0.0;
        grainBOffset = 0.0;

        // Reset sample-and-hold decimator state
        decimateCounter = 0.0f;
        heldSampleL = 0.0f;
        heldSampleR = 0.0f;

        // Configure JUCE ADSR
        adsr.setSampleRate(currentHostSampleRate);
        updateAdsrParameters(pad);
        adsr.noteOn();

        choking = false;
        chokeGain = 1.0f;
        chokeStep = static_cast<float>(1.0 / (0.003 * currentHostSampleRate)); // 3ms click-free choke fade
        active = true;
    }

    void noteOff()
    {
        if (active && !oneShot)
            adsr.noteOff();
    }

    void triggerChoke()
    {
        if (active && !choking)
            choking = true;
    }

    bool isActive() const noexcept { return active; }
    int getPadIndex() const noexcept { return padIndex; }
    int getChokeGroup() const noexcept { return chokeGroup; }

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples,
                         PadParams& pad)
    {
        if (!active || !sampleHolder)
            return;

        const auto& srcBuffer = sampleHolder->buffer;
        const int totalFrames = srcBuffer.getNumSamples();
        const int numSrcChannels = srcBuffer.getNumChannels();
        if (totalFrames < 4 || numSrcChannels < 1)
        {
            active = false;
            return;
        }

        const float* srcL = srcBuffer.getReadPointer(0);
        const float* srcR = (numSrcChannels > 1) ? srcBuffer.getReadPointer(1) : srcL;

        // Read real-time parameters from PadParams
        const float startNorm = juce::jlimit(0.0f, 0.99f, pad.startPoint.load(std::memory_order_relaxed));
        const float endNorm   = juce::jlimit(startNorm + 0.01f, 1.0f, pad.endPoint.load(std::memory_order_relaxed));
        startFrame = startNorm * static_cast<double>(totalFrames - 1);
        endFrame   = endNorm   * static_cast<double>(totalFrames - 1);

        reverse = pad.reverse.load(std::memory_order_relaxed);
        const auto mode = static_cast<StretchMode>(pad.stretchMode.load(std::memory_order_relaxed));

        const float totalSemitones = pad.pitchSemitones.load(std::memory_order_relaxed)
                                   + (pad.fineTuneCents.load(std::memory_order_relaxed) * 0.01f)
                                   + extraPitch;
        const double pitchRatio = std::pow(2.0, static_cast<double>(totalSemitones) / 12.0);

        // Time stretch > 1.0 means slower progression (longer sound), < 1.0 means faster progression
        const double stretchFactor = static_cast<double>(juce::jlimit(0.25f, 4.0f, pad.timeStretch.load(std::memory_order_relaxed)));
        const double sampleRateRatio = sampleHolder->sampleRate / currentHostSampleRate;

        // Grain window length in source frames for Granular OLA Time-Stretch
        const double grainMs = static_cast<double>(juce::jlimit(10.0f, 120.0f, pad.grainSizeMs.load(std::memory_order_relaxed)));
        const double grainLengthHostSamples = std::max(64.0, (grainMs * 0.001) * currentHostSampleRate);
        const double grainPhaseInc = 1.0 / grainLengthHostSamples;

        // Bitcrusher / Decimator params
        const float bitDepth   = juce::jlimit(1.0f, 16.0f, pad.bitDepth.load(std::memory_order_relaxed));
        const float downsample = juce::jlimit(1.0f, 64.0f, pad.downsampleFactor.load(std::memory_order_relaxed));
        const float driveDb    = juce::jlimit(0.0f, 24.0f, pad.driveDb.load(std::memory_order_relaxed));
        const float crushMix   = juce::jlimit(0.0f, 1.0f, pad.crushMix.load(std::memory_order_relaxed));
        const float driveLin   = juce::Decibels::decibelsToGain(driveDb);
        const bool crushActive = (bitDepth < 15.95f || downsample > 1.02f || driveDb > 0.1f) && (crushMix > 0.001f);
        const float quantSteps = std::pow(2.0f, bitDepth - 1.0f);

        // Amplitude & Equal-power Pan
        const float levelGain = juce::Decibels::decibelsToGain(pad.levelDb.load(std::memory_order_relaxed), -60.0f) * velocity;
        const float panVal    = juce::jlimit(-1.0f, 1.0f, pad.pan.load(std::memory_order_relaxed));
        const float panAngle  = (panVal + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
        const float gainL     = std::cos(panAngle) * levelGain;
        const float gainR     = std::sin(panAngle) * levelGain;

        updateAdsrParameters(pad);

        float* outL = outputBuffer.getWritePointer(0, startSample);
        float* outR = (outputBuffer.getNumChannels() > 1) ? outputBuffer.getWritePointer(1, startSample) : nullptr;

        const double direction = reverse ? -1.0 : 1.0;

        for (int i = 0; i < numSamples; ++i)
        {
            float rawL = 0.0f;
            float rawR = 0.0f;

            if (mode == StretchMode::ClassicRepitch || ( std::abs(stretchFactor - 1.0) < 0.005 && std::abs(totalSemitones) < 0.005 ))
            {
                // Classic MPC repitch (or coupled speed = pitchRatio / stretchFactor)
                const double step = direction * sampleRateRatio * (pitchRatio / stretchFactor);
                rawL = cubicInterpolate(srcL, totalFrames, playheadFrame);
                rawR = cubicInterpolate(srcR, totalFrames, playheadFrame);
                playheadFrame += step;
            }
            else
            {
                // Real-time Dual-Grain Hann-Windowed Overlap-Add (WSOLA/Granular) Time-Stretcher
                // Grain A at phase [0..1), Grain B at phase fmod(phase + 0.5, 1.0)
                const double phaseA = grainPhase;
                const double phaseB = (grainPhase < 0.5) ? (grainPhase + 0.5) : (grainPhase - 0.5);

                // Hann windows sum strictly to 1.0: sin^2(pi * phaseA) + sin^2(pi * phaseB) == 1.0
                const float winA = static_cast<float>(std::sin(juce::MathConstants<double>::pi * phaseA));
                const float winB = static_cast<float>(std::sin(juce::MathConstants<double>::pi * phaseB));
                const float weightA = winA * winA;
                const float weightB = winB * winB;

                // Each grain sweeps at pitchRatio relative to its anchor offset
                const double grainSpanFrames = grainLengthHostSamples * sampleRateRatio * (pitchRatio - (1.0 / stretchFactor));
                const double posA = playheadFrame + grainAOffset + direction * (phaseA - 0.5) * grainSpanFrames;
                const double posB = playheadFrame + grainBOffset + direction * (phaseB - 0.5) * grainSpanFrames;

                const float clampedPosA = static_cast<float>(juce::jlimit(startFrame, endFrame, posA));
                const float clampedPosB = static_cast<float>(juce::jlimit(startFrame, endFrame, posB));

                rawL = weightA * cubicInterpolate(srcL, totalFrames, clampedPosA)
                     + weightB * cubicInterpolate(srcL, totalFrames, clampedPosB);
                rawR = weightA * cubicInterpolate(srcR, totalFrames, clampedPosA)
                     + weightB * cubicInterpolate(srcR, totalFrames, clampedPosB);

                // Advance master timeline purely by time-stretch factor (independent of pitch!)
                playheadFrame += direction * sampleRateRatio / stretchFactor;

                // Advance grain phase & re-anchor grains when their window hits 0
                const double prevPhase = grainPhase;
                grainPhase += grainPhaseInc;
                if (grainPhase >= 1.0)
                {
                    grainPhase -= 1.0;
                    grainAOffset = 0.0; // Re-center Grain A at zero-crossing of its window
                }
                if (prevPhase < 0.5 && grainPhase >= 0.5)
                {
                    grainBOffset = 0.0; // Re-center Grain B at zero-crossing of its window
                }
            }

            // Apply Bitcrusher / Sample-Rate Decimator / Saturation Drive
            float procL = rawL;
            float procR = rawR;
            if (crushActive)
            {
                decimateCounter += 1.0f;
                if (decimateCounter >= downsample)
                {
                    decimateCounter -= downsample;

                    // Pre-drive saturation
                    float dL = (driveDb > 0.1f) ? std::tanh(rawL * driveLin) : rawL;
                    float dR = (driveDb > 0.1f) ? std::tanh(rawR * driveLin) : rawR;

                    // Bit-depth mid-tread quantization
                    if (bitDepth < 15.95f)
                    {
                        dL = std::round(dL * quantSteps) / quantSteps;
                        dR = std::round(dR * quantSteps) / quantSteps;
                    }

                    heldSampleL = dL;
                    heldSampleR = dR;
                }

                procL = rawL + crushMix * (heldSampleL - rawL);
                procR = rawR + crushMix * (heldSampleR - rawR);
            }

            // Apply ADSR & Choke fade
            const float env = adsr.getNextSample();
            if (choking)
            {
                chokeGain -= chokeStep;
                if (chokeGain <= 0.0f)
                {
                    chokeGain = 0.0f;
                    active = false;
                    pad.playheadNormalized.store(-1.0f, std::memory_order_relaxed);
                    break;
                }
            }

            const float finalEnv = env * chokeGain;
            outL[i] += procL * gainL * finalEnv;
            if (outR != nullptr)
                outR[i] += procR * gainR * finalEnv;

            // Check if playhead reached trim boundary or envelope finished
            const bool outOfBounds = (!reverse && playheadFrame >= endFrame)
                                  || (reverse  && playheadFrame <= startFrame);
            if (outOfBounds || !adsr.isActive())
            {
                active = false;
                pad.playheadNormalized.store(-1.0f, std::memory_order_relaxed);
                break;
            }
        }

        if (active && totalFrames > 1)
        {
            const float normPos = static_cast<float>(playheadFrame / static_cast<double>(totalFrames - 1));
            pad.playheadNormalized.store(juce::jlimit(0.0f, 1.0f, normPos), std::memory_order_relaxed);
        }
    }

private:
    void updateAdsrParameters(const PadParams& pad)
    {
        juce::ADSR::Parameters params;
        params.attack  = juce::jlimit(0.0005f, 5.0f, pad.attackMs.load(std::memory_order_relaxed) * 0.001f);
        params.decay   = juce::jlimit(0.005f,  5.0f, pad.decayMs.load(std::memory_order_relaxed) * 0.001f);
        params.sustain = oneShot ? 0.0f : juce::jlimit(0.0f, 1.0f, pad.sustainLevel.load(std::memory_order_relaxed));
        params.release = juce::jlimit(0.005f,  5.0f, pad.releaseMs.load(std::memory_order_relaxed) * 0.001f);
        adsr.setParameters(params);
    }

    // Catmull-Rom 4-point cubic interpolation for warm, low-aliasing pitch shifting
    static inline float cubicInterpolate(const float* data, int numFrames, double pos) noexcept
    {
        const int i1 = static_cast<int>( std::floor(pos));
        const float frac = static_cast<float>(pos - static_cast<double>(i1));

        const int i0 = juce::jlimit(0, numFrames - 1, i1 - 1);
        const int idx1 = juce::jlimit(0, numFrames - 1, i1);
        const int i2 = juce::jlimit(0, numFrames - 1, i1 + 1);
        const int i3 = juce::jlimit(0, numFrames - 1, i1 + 2);

        const float y0 = data[i0];
        const float y1 = data[idx1];
        const float y2 = data[i2];
        const float y3 = data[i3];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    std::shared_ptr<const SampleBufferHolder> sampleHolder;
    juce::ADSR adsr;

    int padIndex = 0;
    int chokeGroup = 0;
    bool oneShot = true;
    bool reverse = false;
    bool active = false;
    bool choking = false;

    float velocity = 1.0f;
    float extraPitch = 0.0f;
    float chokeGain = 1.0f;
    float chokeStep = 0.001f;

    double currentHostSampleRate = 44100.0;
    double startFrame = 0.0;
    double endFrame = 0.0;
    double playheadFrame = 0.0;

    // Dual-grain state for OLA time-stretching
    double grainPhase = 0.0;
    double grainAOffset = 0.0;
    double grainBOffset = 0.0;

    // Bitcrusher sample-and-hold state
    float decimateCounter = 0.0f;
    float heldSampleL = 0.0f;
    float heldSampleR = 0.0f;
};

} // namespace mpc16
