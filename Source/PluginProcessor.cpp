#include "PluginProcessor.h"
#include "PluginEditor.h"

MPC16SamplerAudioProcessor::MPC16SamplerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
    loadFactoryKit();
}

MPC16SamplerAudioProcessor::~MPC16SamplerAudioProcessor() = default;

const juce::String MPC16SamplerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MPC16SamplerAudioProcessor::acceptsMidi() const { return true; }
bool MPC16SamplerAudioProcessor::producesMidi() const { return false; }
bool MPC16SamplerAudioProcessor::isMidiEffect() const { return false; }
double MPC16SamplerAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MPC16SamplerAudioProcessor::getNumPrograms() { return 1; }
int MPC16SamplerAudioProcessor::getCurrentProgram() { return 0; }
void MPC16SamplerAudioProcessor::setCurrentProgram(int) {}
const juce::String MPC16SamplerAudioProcessor::getProgramName(int) { return "Default MPC Kit"; }
void MPC16SamplerAudioProcessor::changeProgramName(int, const juce::String&) {}

void MPC16SamplerAudioProcessor::loadFactoryKit()
{
    for (int i = 0; i < mpc16::kNumPads; ++i)
        resetPadToFactory(i);
}

void MPC16SamplerAudioProcessor::resetPadToFactory(int padIndex)
{
    if (padIndex < 0 || padIndex >= mpc16::kNumPads)
        return;

    auto& pad = pads[static_cast<size_t>(padIndex)];
    pad.midiNote.store(36 + padIndex, std::memory_order_relaxed); // C1 (36) to D#2 (51)

    // Assign Closed Hat (5), Pedal Hat (6), Open Hat (7) to Choke Group 1 by default
    const int defaultChoke = (padIndex >= 5 && padIndex <= 7) ? 1 : 0;
    pad.chokeGroup.store(defaultChoke, std::memory_order_relaxed);
    pad.oneShot.store(true, std::memory_order_relaxed);
    pad.reverse.store(false, std::memory_order_relaxed);

    pad.startPoint.store(0.0f, std::memory_order_relaxed);
    pad.endPoint.store(1.0f, std::memory_order_relaxed);

    pad.stretchMode.store(static_cast<int>(mpc16::StretchMode::GranularTimeStretch), std::memory_order_relaxed);
    pad.pitchSemitones.store(0.0f, std::memory_order_relaxed);
    pad.fineTuneCents.store(0.0f, std::memory_order_relaxed);
    pad.timeStretch.store(1.0f, std::memory_order_relaxed);
    pad.grainSizeMs.store(35.0f, std::memory_order_relaxed);

    // Give the Lo-Fi Chord (pad 14) and Vinyl Stab (pad 15) a subtle default 12-bit vibe
    if (padIndex == 14 || padIndex == 15)
    {
        pad.bitDepth.store(12.0f, std::memory_order_relaxed);
        pad.downsampleFactor.store(2.0f, std::memory_order_relaxed);
        pad.driveDb.store(3.0f, std::memory_order_relaxed);
        pad.crushMix.store(0.75f, std::memory_order_relaxed);
    }
    else
    {
        pad.bitDepth.store(16.0f, std::memory_order_relaxed);
        pad.downsampleFactor.store(1.0f, std::memory_order_relaxed);
        pad.driveDb.store(0.0f, std::memory_order_relaxed);
        pad.crushMix.store(1.0f, std::memory_order_relaxed);
    }

    pad.attackMs.store(1.0f, std::memory_order_relaxed);
    pad.decayMs.store(900.0f, std::memory_order_relaxed);
    pad.sustainLevel.store(1.0f, std::memory_order_relaxed);
    pad.releaseMs.store(120.0f, std::memory_order_relaxed);
    pad.levelDb.store(0.0f, std::memory_order_relaxed);
    pad.pan.store(0.0f, std::memory_order_relaxed);

    pad.sample = mpc16::FactoryDrumBuilder::createFactoryDrumSample(padIndex, 44100.0);
}

bool MPC16SamplerAudioProcessor::loadSampleForPad(int padIndex, const juce::File& audioFile)
{
    if (padIndex < 0 || padIndex >= mpc16::kNumPads || !audioFile.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    // Cap to 60 seconds max to avoid huge allocations
    const int maxSamples = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 60.0)));
    const int numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));

    auto holder = std::make_shared<mpc16::SampleBufferHolder>();
    holder->sampleRate = reader->sampleRate;
    holder->name = audioFile.getFileNameWithoutExtension().substring(0, 22);
    holder->filePath = audioFile.getFullPathName();
    holder->buffer.setSize(numChannels, maxSamples);

    if (!reader->read(&holder->buffer, 0, maxSamples, 0, true, numChannels > 1))
        return false;

    auto& pad = pads[static_cast<size_t>(padIndex)];
    pad.startPoint.store(0.0f, std::memory_order_relaxed);
    pad.endPoint.store(1.0f, std::memory_order_relaxed);
    std::atomic_store(&pad.sample, std::shared_ptr<const mpc16::SampleBufferHolder>(holder));
    return true;
}

void MPC16SamplerAudioProcessor::triggerPadFromUI(int padIndex, float velocityNormalized)
{
    const auto scope = uiEventFifo.write(1);
    if (scope.blockSize1 > 0)
    {
        uiEventBuffer[static_cast<size_t>(scope.startIndex1)] = {
            juce::jlimit(0, mpc16::kNumPads - 1, padIndex),
            juce::jlimit(0.05f, 1.0f, velocityNormalized),
            true
        };
    }
}

void MPC16SamplerAudioProcessor::releasePadFromUI(int padIndex)
{
    const auto scope = uiEventFifo.write(1);
    if (scope.blockSize1 > 0)
    {
        uiEventBuffer[static_cast<size_t>(scope.startIndex1)] = {
            juce::jlimit(0, mpc16::kNumPads - 1, padIndex),
            0.0f,
            false
        };
    }
}

void MPC16SamplerAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    masterDecimateCounter = 0.0f;
    masterHeldL = 0.0f;
    masterHeldR = 0.0f;
}

void MPC16SamplerAudioProcessor::releaseResources() {}

bool MPC16SamplerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

int MPC16SamplerAudioProcessor::findPadForMidiNote(int midiNote) const noexcept
{
    for (int i = 0; i < mpc16::kNumPads; ++i)
    {
        if (pads[static_cast<size_t>(i)].midiNote.load(std::memory_order_relaxed) == midiNote)
            return i;
    }
    return -1;
}

void MPC16SamplerAudioProcessor::triggerNoteEvent(int padIdx, float velocityNorm)
{
    if (padIdx < 0 || padIdx >= mpc16::kNumPads)
        return;

    const float finalVel = fullLevelMode.load(std::memory_order_relaxed)
                         ? 1.0f
                         : juce::jlimit(0.05f, 1.0f, velocityNorm);

    // Visual trigger flash on the pressed pad
    pads[static_cast<size_t>(padIdx)].triggerActivity.store(finalVel, std::memory_order_relaxed);

    // In "16 Levels (Pitch)" mode, all 16 pads trigger the currently selected pad pitched chromatically (-4 to +11 semitones)
    int targetPadIdx = padIdx;
    float extraSemitones = 0.0f;
    if (sixteenLevelsPitchMode.load(std::memory_order_relaxed))
    {
        targetPadIdx = selectedPadIndex.load(std::memory_order_relaxed);
        extraSemitones = static_cast<float>(padIdx - 4); // Pad 5 (index 4) is original pitch (0 st)
    }

    auto& targetPad = pads[static_cast<size_t>(targetPadIdx)];
    const int chokeGroup = targetPad.chokeGroup.load(std::memory_order_relaxed);

    // Choke any active voices in the same choke group (or re-triggering the exact same pad)
    for (auto& voice : voices)
    {
        if (voice.isActive())
        {
            if ((chokeGroup > 0 && voice.getChokeGroup() == chokeGroup) ||
                (!sixteenLevelsPitchMode.load(std::memory_order_relaxed) && voice.getPadIndex() == targetPadIdx))
            {
                voice.triggerChoke();
            }
        }
    }

    // Find a free voice or round-robin steal
    mpc16::PadVoice* chosenVoice = nullptr;
    for (auto& voice : voices)
    {
        if (!voice.isActive())
        {
            chosenVoice = &voice;
            break;
        }
    }
    if (chosenVoice == nullptr)
    {
        chosenVoice = &voices[static_cast<size_t>(nextVoiceStealIndex)];
        nextVoiceStealIndex = (nextVoiceStealIndex + 1) % mpc16::kMaxVoices;
    }

    chosenVoice->startVoice(targetPadIdx, targetPad, finalVel, extraSemitones, currentSampleRate);
}

void MPC16SamplerAudioProcessor::releaseNoteEvent(int padIdx)
{
    const int targetPadIdx = sixteenLevelsPitchMode.load(std::memory_order_relaxed)
                           ? selectedPadIndex.load(std::memory_order_relaxed)
                           : padIdx;

    for (auto& voice : voices)
    {
        if (voice.isActive() && voice.getPadIndex() == targetPadIdx)
            voice.noteOff();
    }
}

void MPC16SamplerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. Drain UI pad trigger events
    {
        const auto scope = uiEventFifo.read(uiEventFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
        {
            const auto& ev = uiEventBuffer[static_cast<size_t>(scope.startIndex1 + i)];
            if (ev.isNoteOn)
                triggerNoteEvent(ev.padIndex, ev.velocity);
            else
                releaseNoteEvent(ev.padIndex);
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            const auto& ev = uiEventBuffer[static_cast<size_t>(scope.startIndex2 + i)];
            if (ev.isNoteOn)
                triggerNoteEvent(ev.padIndex, ev.velocity);
            else
                releaseNoteEvent(ev.padIndex);
        }
    }

    // 2. Process incoming MIDI events with sample-accurate sub-block rendering
    int currentSample = 0;
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const int eventSample = juce::jlimit(0, numSamples, metadata.samplePosition);

        if (eventSample > currentSample)
        {
            for (auto& voice : voices)
            {
                if (voice.isActive())
                    voice.renderNextBlock(buffer, currentSample, eventSample - currentSample, pads[static_cast<size_t>(voice.getPadIndex())]);
            }
            currentSample = eventSample;
        }

        if (msg.isNoteOn())
        {
            const int padIdx = findPadForMidiNote(msg.getNoteNumber());
            if (padIdx >= 0)
                triggerNoteEvent(padIdx, msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            const int padIdx = findPadForMidiNote(msg.getNoteNumber());
            if (padIdx >= 0)
                releaseNoteEvent(padIdx);
        }
    }

    // Render remaining samples in block
    if (currentSample < numSamples)
    {
        for (auto& voice : voices)
        {
            if (voice.isActive())
                voice.renderNextBlock(buffer, currentSample, numSamples - currentSample, pads[static_cast<size_t>(voice.getPadIndex())]);
        }
    }

    // 3. Apply Global Vintage Mode (MPC-60 / SP-1200 Master Emulation) & Master Gain
    const auto vintage = static_cast<mpc16::VintageMode>(globalVintageMode.load(std::memory_order_relaxed));
    const float masterGain = juce::Decibels::decibelsToGain(masterGainDb.load(std::memory_order_relaxed), -60.0f);

    float* outL = buffer.getWritePointer(0);
    float* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

    if (vintage != mpc16::VintageMode::Clean)
    {
        const double targetRate = (vintage == mpc16::VintageMode::MPC60_12Bit) ? 40000.0 : 26040.0;
        const float downsample = static_cast<float>(std::max(1.0, currentSampleRate / targetRate));
        const float quantSteps = 2048.0f; // 12-bit signed (-2048..2048)
        const float drive = (vintage == mpc16::VintageMode::MPC60_12Bit) ? 1.18f : 1.32f;

        for (int i = 0; i < numSamples; ++i)
        {
            masterDecimateCounter += 1.0f;
            if (masterDecimateCounter >= downsample)
            {
                masterDecimateCounter -= downsample;
                float sL = std::tanh(outL[i] * drive);
                float sR = (outR != nullptr) ? std::tanh(outR[i] * drive) : sL;

                masterHeldL = std::round(sL * quantSteps) / quantSteps;
                masterHeldR = std::round(sR * quantSteps) / quantSteps;
            }
            outL[i] = masterHeldL * masterGain;
            if (outR != nullptr)
                outR[i] = masterHeldR * masterGain;
        }
    }
    else if (std::abs(masterGain - 1.0f) > 0.0001f)
    {
        buffer.applyGain(masterGain);
    }

    // 4. Update Master Peak Meters & Decay Pad Activity LEDs
    const float blockPeakL = buffer.getMagnitude(0, 0, numSamples);
    const float blockPeakR = (buffer.getNumChannels() > 1) ? buffer.getMagnitude(1, 0, numSamples) : blockPeakL;

    const float prevPeakL = outputPeakL.load(std::memory_order_relaxed);
    const float prevPeakR = outputPeakR.load(std::memory_order_relaxed);
    outputPeakL.store(std::max(blockPeakL, prevPeakL * 0.90f), std::memory_order_relaxed);
    outputPeakR.store(std::max(blockPeakR, prevPeakR * 0.90f), std::memory_order_relaxed);

    for (auto& pad : pads)
    {
        const float act = pad.triggerActivity.load(std::memory_order_relaxed);
        if (act > 0.001f)
            pad.triggerActivity.store(act * 0.92f, std::memory_order_relaxed);
        else
            pad.triggerActivity.store(0.0f, std::memory_order_relaxed);
    }
}

bool MPC16SamplerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MPC16SamplerAudioProcessor::createEditor()
{
    return new MPC16SamplerAudioProcessorEditor(*this);
}

void MPC16SamplerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement root("MPC16SamplerState");
    root.setAttribute("selectedPad", selectedPadIndex.load());
    root.setAttribute("fullLevelMode", fullLevelMode.load());
    root.setAttribute("sixteenLevelsPitchMode", sixteenLevelsPitchMode.load());
    root.setAttribute("globalVintageMode", globalVintageMode.load());
    root.setAttribute("masterGainDb", static_cast<double>(masterGainDb.load()));

    for (int i = 0; i < mpc16::kNumPads; ++i)
    {
        const auto& pad = pads[static_cast<size_t>(i)];
        auto* padEl = root.createNewChildElement("Pad");
        padEl->setAttribute("index", i);
        padEl->setAttribute("midiNote", pad.midiNote.load());
        padEl->setAttribute("chokeGroup", pad.chokeGroup.load());
        padEl->setAttribute("oneShot", pad.oneShot.load());
        padEl->setAttribute("reverse", pad.reverse.load());
        padEl->setAttribute("startPoint", static_cast<double>(pad.startPoint.load()));
        padEl->setAttribute("endPoint", static_cast<double>(pad.endPoint.load()));
        padEl->setAttribute("stretchMode", pad.stretchMode.load());
        padEl->setAttribute("pitchSemitones", static_cast<double>(pad.pitchSemitones.load()));
        padEl->setAttribute("fineTuneCents", static_cast<double>(pad.fineTuneCents.load()));
        padEl->setAttribute("timeStretch", static_cast<double>(pad.timeStretch.load()));
        padEl->setAttribute("grainSizeMs", static_cast<double>(pad.grainSizeMs.load()));
        padEl->setAttribute("bitDepth", static_cast<double>(pad.bitDepth.load()));
        padEl->setAttribute("downsampleFactor", static_cast<double>(pad.downsampleFactor.load()));
        padEl->setAttribute("driveDb", static_cast<double>(pad.driveDb.load()));
        padEl->setAttribute("crushMix", static_cast<double>(pad.crushMix.load()));
        padEl->setAttribute("attackMs", static_cast<double>(pad.attackMs.load()));
        padEl->setAttribute("decayMs", static_cast<double>(pad.decayMs.load()));
        padEl->setAttribute("sustainLevel", static_cast<double>(pad.sustainLevel.load()));
        padEl->setAttribute("releaseMs", static_cast<double>(pad.releaseMs.load()));
        padEl->setAttribute("levelDb", static_cast<double>(pad.levelDb.load()));
        padEl->setAttribute("pan", static_cast<double>(pad.pan.load()));

        auto samplePtr = std::atomic_load(&pad.sample);
        if (samplePtr && samplePtr->filePath.isNotEmpty())
            padEl->setAttribute("filePath", samplePtr->filePath);
    }

    copyXmlToBinary(root, destData);
}

void MPC16SamplerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> root(getXmlFromBinary(data, sizeInBytes));
    if (root == nullptr || !root->hasTagName("MPC16SamplerState"))
        return;

    selectedPadIndex.store(juce::jlimit(0, mpc16::kNumPads - 1, root->getIntAttribute("selectedPad", 0)));
    fullLevelMode.store(root->getBoolAttribute("fullLevelMode", false));
    sixteenLevelsPitchMode.store(root->getBoolAttribute("sixteenLevelsPitchMode", false));
    globalVintageMode.store(root->getIntAttribute("globalVintageMode", 0));
    masterGainDb.store(static_cast<float>(root->getDoubleAttribute("masterGainDb", 0.0)));

    for (auto* padEl : root->getChildWithTagNameIterator("Pad"))
    {
        const int idx = padEl->getIntAttribute("index", -1);
        if (idx < 0 || idx >= mpc16::kNumPads)
            continue;

        const juce::String filePath = padEl->getStringAttribute("filePath");
        if (filePath.isNotEmpty())
        {
            juce::File file(filePath);
            if (file.existsAsFile())
                loadSampleForPad(idx, file);
        }

        auto& pad = pads[static_cast<size_t>(idx)];
        pad.midiNote.store(padEl->getIntAttribute("midiNote", 36 + idx));
        pad.chokeGroup.store(padEl->getIntAttribute("chokeGroup", 0));
        pad.oneShot.store(padEl->getBoolAttribute("oneShot", true));
        pad.reverse.store(padEl->getBoolAttribute("reverse", false));
        pad.startPoint.store(static_cast<float>(padEl->getDoubleAttribute("startPoint", 0.0)));
        pad.endPoint.store(static_cast<float>(padEl->getDoubleAttribute("endPoint", 1.0)));
        pad.stretchMode.store(padEl->getIntAttribute("stretchMode", static_cast<int>(mpc16::StretchMode::GranularTimeStretch)));
        pad.pitchSemitones.store(static_cast<float>(padEl->getDoubleAttribute("pitchSemitones", 0.0)));
        pad.fineTuneCents.store(static_cast<float>(padEl->getDoubleAttribute("fineTuneCents", 0.0)));
        pad.timeStretch.store(static_cast<float>(padEl->getDoubleAttribute("timeStretch", 1.0)));
        pad.grainSizeMs.store(static_cast<float>(padEl->getDoubleAttribute("grainSizeMs", 35.0)));
        pad.bitDepth.store(static_cast<float>(padEl->getDoubleAttribute("bitDepth", 16.0)));
        pad.downsampleFactor.store(static_cast<float>(padEl->getDoubleAttribute("downsampleFactor", 1.0)));
        pad.driveDb.store(static_cast<float>(padEl->getDoubleAttribute("driveDb", 0.0)));
        pad.crushMix.store(static_cast<float>(padEl->getDoubleAttribute("crushMix", 1.0)));
        pad.attackMs.store(static_cast<float>(padEl->getDoubleAttribute("attackMs", 1.0)));
        pad.decayMs.store(static_cast<float>(padEl->getDoubleAttribute("decayMs", 900.0)));
        pad.sustainLevel.store(static_cast<float>(padEl->getDoubleAttribute("sustainLevel", 1.0)));
        pad.releaseMs.store(static_cast<float>(padEl->getDoubleAttribute("releaseMs", 120.0)));
        pad.levelDb.store(static_cast<float>(padEl->getDoubleAttribute("levelDb", 0.0)));
        pad.pan.store(static_cast<float>(padEl->getDoubleAttribute("pan", 0.0)));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MPC16SamplerAudioProcessor();
}
