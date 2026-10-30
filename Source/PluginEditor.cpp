#include "PluginProcessor.h"
#include "PluginEditor.h"

MPC16SamplerAudioProcessorEditor::MPC16SamplerAudioProcessorEditor(MPC16SamplerAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      waveformDisplay(p),
      padGrid(p)
{
    setLookAndFeel(&customLookAndFeel);

    addAndMakeVisible(waveformDisplay);
    addAndMakeVisible(padGrid);

    padGrid.onPadSelected = [this](int)
    {
        syncUIFromSelectedPad();
        waveformDisplay.repaint();
    };

    waveformDisplay.onSampleOrTrimChanged = [this]()
    {
        syncUIFromSelectedPad();
        padGrid.repaint();
    };

    // Global Vintage Mode Selector
    vintageModeBox.addItem("Clean 32-Bit Floating Point", 1);
    vintageModeBox.addItem("MPC-60 Vintage (12-Bit / 40 kHz)", 2);
    vintageModeBox.addItem("SP-1200 Crunch (12-Bit / 26.04 kHz)", 3);
    vintageModeBox.onChange = [this]()
    {
        if (!isSyncingUI)
            audioProcessor.globalVintageMode.store(vintageModeBox.getSelectedId() - 1);
    };
    addAndMakeVisible(vintageModeBox);

    initKnob(masterGainKnob, "MASTER", -36.0, 12.0, 0.1, " dB", juce::Colour(0xffff9800),
             [this](float v) { audioProcessor.masterGainDb.store(v); });

    // Performance Mode Buttons
    fullLevelBtn.setClickingTogglesState(true);
    fullLevelBtn.onClick = [this]()
    {
        audioProcessor.fullLevelMode.store(fullLevelBtn.getToggleState());
    };
    addAndMakeVisible(fullLevelBtn);

    sixteenLevelsBtn.setClickingTogglesState(true);
    sixteenLevelsBtn.onClick = [this]()
    {
        audioProcessor.sixteenLevelsPitchMode.store(sixteenLevelsBtn.getToggleState());
        padGrid.repaint();
    };
    addAndMakeVisible(sixteenLevelsBtn);

    loadSampleBtn.onClick = [this]() { loadSampleViaFileChooser(); };
    addAndMakeVisible(loadSampleBtn);

    resetPadBtn.onClick = [this]()
    {
        audioProcessor.resetPadToFactory(audioProcessor.getSelectedPadIndex());
        syncUIFromSelectedPad();
        waveformDisplay.repaint();
        padGrid.repaint();
    };
    addAndMakeVisible(resetPadBtn);

    factoryKitBtn.onClick = [this]()
    {
        audioProcessor.loadFactoryKit();
        syncUIFromSelectedPad();
        waveformDisplay.repaint();
        padGrid.repaint();
    };
    addAndMakeVisible(factoryKitBtn);

    // Selected Pad Toolbar
    stretchModeBox.addItem("Granular Time-Stretch (Independent)", 1);
    stretchModeBox.addItem("Classic MPC Repitch (Coupled)", 2);
    stretchModeBox.onChange = [this]()
    {
        if (isSyncingUI) return;
        auto& pad = audioProcessor.getPad(audioProcessor.getSelectedPadIndex());
        const auto mode = (stretchModeBox.getSelectedId() == 1)
                        ? mpc16::StretchMode::GranularTimeStretch
                        : mpc16::StretchMode::ClassicRepitch;
        pad.stretchMode.store(static_cast<int>(mode));
    };
    addAndMakeVisible(stretchModeBox);

    chokeGroupBox.addItem("Choke: Off", 1);
    chokeGroupBox.addItem("Choke: Group 1", 2);
    chokeGroupBox.addItem("Choke: Group 2", 3);
    chokeGroupBox.addItem("Choke: Group 3", 4);
    chokeGroupBox.addItem("Choke: Group 4", 5);
    chokeGroupBox.onChange = [this]()
    {
        if (isSyncingUI) return;
        auto& pad = audioProcessor.getPad(audioProcessor.getSelectedPadIndex());
        pad.chokeGroup.store(chokeGroupBox.getSelectedId() - 1);
        padGrid.repaint();
    };
    addAndMakeVisible(chokeGroupBox);

    oneShotBtn.setClickingTogglesState(true);
    oneShotBtn.onClick = [this]()
    {
        if (isSyncingUI) return;
        auto& pad = audioProcessor.getPad(audioProcessor.getSelectedPadIndex());
        pad.oneShot.store(oneShotBtn.getToggleState());
        oneShotBtn.setButtonText(oneShotBtn.getToggleState() ? "ONE-SHOT" : "GATE MODE");
    };
    addAndMakeVisible(oneShotBtn);

    reverseBtn.setClickingTogglesState(true);
    reverseBtn.onClick = [this]()
    {
        if (isSyncingUI) return;
        auto& pad = audioProcessor.getPad(audioProcessor.getSelectedPadIndex());
        pad.reverse.store(reverseBtn.getToggleState());
        waveformDisplay.repaint();
        padGrid.repaint();
    };
    addAndMakeVisible(reverseBtn);

    // 1. Pitch & Time-Stretch Knobs (Cyan accent)
    const juce::Colour pitchAccent(0xff00e5ff);
    initKnob(pitchKnob, "PITCH", -24.0, 24.0, 1.0, " st", pitchAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).pitchSemitones.store(v);
    });
    initKnob(fineTuneKnob, "FINE", -100.0, 100.0, 1.0, " ct", pitchAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).fineTuneCents.store(v);
    });
    initKnob(timeStretchKnob, "STRETCH", 0.50, 2.00, 0.01, "x", pitchAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).timeStretch.store(v);
    });
    initKnob(grainSizeKnob, "GRAIN", 10.0, 100.0, 1.0, " ms", pitchAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).grainSizeMs.store(v);
    });

    // 2. Bitcrusher & Sample Rate Decimator Knobs (Orange/Red accent)
    const juce::Colour crushAccent(0xffff5722);
    initKnob(bitDepthKnob, "BITS", 1.0, 16.0, 0.1, " bit", crushAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).bitDepth.store(v);
        padGrid.repaint();
    });
    initKnob(downsampleKnob, "DECIMATE", 1.0, 64.0, 0.5, "x", crushAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).downsampleFactor.store(v);
        waveformDisplay.repaint();
        padGrid.repaint();
    }, 8.0);
    initKnob(driveKnob, "DRIVE", 0.0, 24.0, 0.1, " dB", crushAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).driveDb.store(v);
    });
    initKnob(crushMixKnob, "CRUSH MIX", 0.0, 100.0, 1.0, " %", crushAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).crushMix.store(v * 0.01f);
    });

    // 3. Amplitude & ADSR Knobs (Amber/Gold accent)
    const juce::Colour envAccent(0xffffb300);
    initKnob(attackKnob, "ATTACK", 0.5, 2000.0, 0.5, " ms", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).attackMs.store(v);
    }, 100.0);
    initKnob(decayKnob, "DECAY", 5.0, 5000.0, 1.0, " ms", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).decayMs.store(v);
    }, 500.0);
    initKnob(sustainKnob, "SUSTAIN", 0.0, 100.0, 1.0, " %", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).sustainLevel.store(v * 0.01f);
    });
    initKnob(releaseKnob, "RELEASE", 5.0, 5000.0, 1.0, " ms", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).releaseMs.store(v);
    }, 250.0);
    initKnob(levelKnob, "LEVEL", -48.0, 12.0, 0.1, " dB", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).levelDb.store(v);
    });
    initKnob(panKnob, "PAN", -100.0, 100.0, 1.0, "", envAccent, [this](float v)
    {
        audioProcessor.getPad(audioProcessor.getSelectedPadIndex()).pan.store(v * 0.01f);
    });

    syncUIFromSelectedPad();
    setSize(980, 640);
    startTimerHz(60);
}

MPC16SamplerAudioProcessorEditor::~MPC16SamplerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MPC16SamplerAudioProcessorEditor::initKnob(LabeledKnob& knob,
                                                const juce::String& title,
                                                double minVal, double maxVal, double step,
                                                const juce::String& suffix,
                                                juce::Colour accentColour,
                                                std::function<void(float)> onValueChange,
                                                double skewMidpoint)
{
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 16);
    knob.slider.setRange(minVal, maxVal, step);
    knob.slider.setTextValueSuffix(suffix);
    knob.slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    if (skewMidpoint > minVal && skewMidpoint < maxVal)
        knob.slider.setSkewFactorFromMidPoint(skewMidpoint);

    knob.slider.onValueChange = [this, &knob, onValueChange]()
    {
        if (!isSyncingUI && onValueChange)
            onValueChange(static_cast<float>(knob.slider.getValue()));
    };
    addAndMakeVisible(knob.slider);

    knob.label.setText(title, juce::dontSendNotification);
    knob.label.setJustificationType(juce::Justification::centred);
    knob.label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    knob.label.setColour(juce::Label::textColourId, juce::Colour(0xff9aa5b4));
    addAndMakeVisible(knob.label);
}

void MPC16SamplerAudioProcessorEditor::syncUIFromSelectedPad()
{
    juce::ScopedValueSetter<bool> svs(isSyncingUI, true);

    vintageModeBox.setSelectedId(audioProcessor.globalVintageMode.load() + 1, juce::dontSendNotification);
    masterGainKnob.slider.setValue(audioProcessor.masterGainDb.load(), juce::dontSendNotification);
    fullLevelBtn.setToggleState(audioProcessor.fullLevelMode.load(), juce::dontSendNotification);
    sixteenLevelsBtn.setToggleState(audioProcessor.sixteenLevelsPitchMode.load(), juce::dontSendNotification);

    const auto& pad = audioProcessor.getPad(audioProcessor.getSelectedPadIndex());

    const auto mode = static_cast<mpc16::StretchMode>(pad.stretchMode.load());
    stretchModeBox.setSelectedId(mode == mpc16::StretchMode::GranularTimeStretch ? 1 : 2, juce::dontSendNotification);
    chokeGroupBox.setSelectedId(pad.chokeGroup.load() + 1, juce::dontSendNotification);

    const bool isOneShot = pad.oneShot.load();
    oneShotBtn.setToggleState(isOneShot, juce::dontSendNotification);
    oneShotBtn.setButtonText(isOneShot ? "ONE-SHOT" : "GATE MODE");
    reverseBtn.setToggleState(pad.reverse.load(), juce::dontSendNotification);

    pitchKnob.slider.setValue(pad.pitchSemitones.load(), juce::dontSendNotification);
    fineTuneKnob.slider.setValue(pad.fineTuneCents.load(), juce::dontSendNotification);
    timeStretchKnob.slider.setValue(pad.timeStretch.load(), juce::dontSendNotification);
    grainSizeKnob.slider.setValue(pad.grainSizeMs.load(), juce::dontSendNotification);

    bitDepthKnob.slider.setValue(pad.bitDepth.load(), juce::dontSendNotification);
    downsampleKnob.slider.setValue(pad.downsampleFactor.load(), juce::dontSendNotification);
    driveKnob.slider.setValue(pad.driveDb.load(), juce::dontSendNotification);
    crushMixKnob.slider.setValue(pad.crushMix.load() * 100.0f, juce::dontSendNotification);

    attackKnob.slider.setValue(pad.attackMs.load(), juce::dontSendNotification);
    decayKnob.slider.setValue(pad.decayMs.load(), juce::dontSendNotification);
    sustainKnob.slider.setValue(pad.sustainLevel.load() * 100.0f, juce::dontSendNotification);
    releaseKnob.slider.setValue(pad.releaseMs.load(), juce::dontSendNotification);
    levelKnob.slider.setValue(pad.levelDb.load(), juce::dontSendNotification);
    panKnob.slider.setValue(pad.pan.load() * 100.0f, juce::dontSendNotification);
}

void MPC16SamplerAudioProcessorEditor::loadSampleViaFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Drum / Instrument Sample",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file.existsAsFile())
        {
            if (audioProcessor.loadSampleForPad(audioProcessor.getSelectedPadIndex(), file))
            {
                syncUIFromSelectedPad();
                waveformDisplay.repaint();
                padGrid.repaint();
            }
        }
    });
}

void MPC16SamplerAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();
    padGrid.repaint();
    repaint(getWidth() - 180, 6, 170, 44); // Repaint master meter strip
}

void MPC16SamplerAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Main chassis background
    g.fillAll(juce::Colour(0xff14181e));

    // Top Header Bar
    g.setColour(juce::Colour(0xff1c2129));
    g.fillRect(0, 0, getWidth(), 54);
    g.setColour(juce::Colour(0xff2e3642));
    g.drawHorizontalLine(54, 0.0f, static_cast<float>(getWidth()));

    // Brand Title & Accent Stripe
    g.setColour(juce::Colour(0xffff7a00));
    g.fillRoundedRectangle(14.0f, 12.0f, 6.0f, 30.0f, 3.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("MPC-16 CRUNCH", 28, 10, 220, 22, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff8b96a5));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("DRUM PAD SAMPLER  |  TIME-STRETCH & LO-FI ENGINE", 28, 30, 310, 14, juce::Justification::centredLeft);

    // Master Output Peak Meter
    const float peakL = juce::jlimit(0.0f, 1.0f, audioProcessor.outputPeakL.load(std::memory_order_relaxed));
    const float peakR = juce::jlimit(0.0f, 1.0f, audioProcessor.outputPeakR.load(std::memory_order_relaxed));
    const int meterX = getWidth() - 135;
    const int meterW = 115;

    g.setColour(juce::Colour(0xff101318));
    g.fillRoundedRectangle(static_cast<float>(meterX), 15.0f, static_cast<float>(meterW), 10.0f, 3.0f);
    g.fillRoundedRectangle(static_cast<float>(meterX), 29.0f, static_cast<float>(meterW), 10.0f, 3.0f);

    g.setColour(peakL > 0.95f ? juce::Colour(0xffff3b30) : juce::Colour(0xff00e676));
    g.fillRoundedRectangle(static_cast<float>(meterX + 1), 16.0f, (meterW - 2) * peakL, 8.0f, 2.0f);
    g.setColour(peakR > 0.95f ? juce::Colour(0xffff3b30) : juce::Colour(0xff00e676));
    g.fillRoundedRectangle(static_cast<float>(meterX + 1), 30.0f, (meterW - 2) * peakR, 8.0f, 2.0f);

    // Right-Side Control Module Panels
    const auto drawModulePanel = [&](juce::Rectangle<float> rect, const juce::String& title, juce::Colour badgeCol)
    {
        g.setColour(juce::Colour(0xff1b2028));
        g.fillRoundedRectangle(rect, 7.0f);
        g.setColour(juce::Colour(0xff2c333f));
        g.drawRoundedRectangle(rect, 7.0f, 1.2f);

        g.setColour(badgeCol);
        g.fillRoundedRectangle(rect.getX() + 10.0f, rect.getY() + 8.0f, 4.0f, 13.0f, 2.0f);

        g.setColour(juce::Colour(0xffcfd8e3));
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(title, static_cast<int>(rect.getX() + 20.0f), static_cast<int>(rect.getY() + 6.0f),
                   static_cast<int>(rect.getWidth() - 30.0f), 16, juce::Justification::centredLeft);
    };

    const float rightX = 434.0f;
    const float rightW = static_cast<float>(getWidth()) - rightX - 14.0f;
    const float halfW = (rightW - 10.0f) * 0.5f;

    drawModulePanel({ rightX, 268.0f, halfW, 165.0f }, "PITCH & TIME-STRETCH", juce::Colour(0xff00e5ff));
    drawModulePanel({ rightX + halfW + 10.0f, 268.0f, halfW, 165.0f }, "LO-FI BITCRUSHER & DECIMATOR", juce::Colour(0xffff5722));
    drawModulePanel({ rightX, 443.0f, rightW, 183.0f }, "AMPLITUDE & ADSR ENVELOPE", juce::Colour(0xffffb300));
}

void MPC16SamplerAudioProcessorEditor::resized()
{
    // Top Header Controls
    vintageModeBox.setBounds(375, 14, 260, 26);
    masterGainKnob.label.setBounds(655, 4, 60, 12);
    masterGainKnob.slider.setBounds(710, 4, 115, 46);
    masterGainKnob.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);

    // Left Side: Performance Buttons + 4x4 MPC Pad Grid
    const int leftX = 14;
    const int leftW = 406;

    fullLevelBtn.setBounds(leftX, 66, 96, 28);
    sixteenLevelsBtn.setBounds(leftX + 102, 66, 128, 28);
    loadSampleBtn.setBounds(leftX + 236, 66, 84, 28);
    resetPadBtn.setBounds(leftX + 326, 66, 80, 28);

    padGrid.setBounds(leftX, 102, leftW, 488);
    factoryKitBtn.setBounds(leftX, 596, leftW, 28);

    // Right Side Top: Waveform Display & Pad Mode Strip
    const int rightX = 434;
    const int rightW = getWidth() - rightX - 14;

    waveformDisplay.setBounds(rightX, 66, rightW, 156);

    stretchModeBox.setBounds(rightX, 230, 225, 28);
    chokeGroupBox.setBounds(rightX + 233, 230, 125, 28);
    oneShotBtn.setBounds(rightX + 366, 230, 86, 28);
    reverseBtn.setBounds(rightX + 458, 230, rightW - 458, 28);

    // Helper lambda to lay out a row of knobs inside a module panel
    const auto placeKnobsInRow = [](juce::Rectangle<int> area, std::initializer_list<LabeledKnob*> knobs)
    {
        const int count = static_cast<int>(knobs.size());
        if (count == 0) return;
        const int cellW = area.getWidth() / count;
        int idx = 0;
        for (auto* k : knobs)
        {
            auto cell = area.withX(area.getX() + idx * cellW).withWidth(cellW);
            k->label.setBounds(cell.removeFromTop(16));
            k->slider.setBounds(cell.reduced(2, 2));
            ++idx;
        }
    };

    const int halfW = (rightW - 10) / 2;

    // Module 1: Pitch & Time-Stretch (4 knobs)
    placeKnobsInRow({ rightX + 4, 294, halfW - 8, 132 },
                    { &pitchKnob, &fineTuneKnob, &timeStretchKnob, &grainSizeKnob });

    // Module 2: Bitcrusher & Decimator (4 knobs)
    placeKnobsInRow({ rightX + halfW + 14, 294, halfW - 8, 132 },
                    { &bitDepthKnob, &downsampleKnob, &driveKnob, &crushMixKnob });

    // Module 3: Amp & ADSR Envelope (6 knobs)
    placeKnobsInRow({ rightX + 6, 472, rightW - 12, 146 },
                    { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob, &levelKnob, &panKnob });
}
