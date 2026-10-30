#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformDisplay.h"
#include "PadGridComponent.h"

class MPC16LookAndFeel : public juce::LookAndFeel_V4
{
public:
    MPC16LookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffdce3ec));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1c2128));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38414e));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe6edf3));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b34));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff7a00));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce3ec));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xff111418));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override
    {
        const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 5.0f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float rx = centreX - radius;
        const float ry = centreY - radius;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Background track arc
        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff2a303a));
        g.strokePath(bgArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Active value arc
        juce::Path valArc;
        valArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        const juce::Colour accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(accent);
        g.strokePath(valArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Inner hardware cap
        const float capRadius = radius * 0.74f;
        juce::ColourGradient capGrad(juce::Colour(0xff2e3540), centreX, centreY - capRadius,
                                     juce::Colour(0xff191d24), centreX, centreY + capRadius, false);
        g.setGradientFill(capGrad);
        g.fillEllipse(rx + (radius - capRadius), ry + (radius - capRadius), capRadius * 2.0f, capRadius * 2.0f);

        g.setColour(juce::Colour(0xff3e4756));
        g.drawEllipse(rx + (radius - capRadius), ry + (radius - capRadius), capRadius * 2.0f, capRadius * 2.0f, 1.0f);

        // Pointer tick
        juce::Path pointer;
        const float pointerLen = capRadius * 0.82f;
        pointer.addRoundedRectangle(-1.5f, -capRadius, 3.0f, pointerLen, 1.0f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colours::white);
        g.fillPath(pointer);
    }
};

class MPC16SamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit MPC16SamplerAudioProcessorEditor(MPC16SamplerAudioProcessor&);
    ~MPC16SamplerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncUIFromSelectedPad();
    void loadSampleViaFileChooser();

    struct LabeledKnob
    {
        juce::Slider slider;
        juce::Label label;
    };

    void initKnob(LabeledKnob& knob,
                  const juce::String& title,
                  double minVal, double maxVal, double step,
                  const juce::String& suffix,
                  juce::Colour accentColour,
                  std::function<void(float)> onValueChange,
                  double skewMidpoint = 0.0);

    MPC16SamplerAudioProcessor& audioProcessor;
    MPC16LookAndFeel customLookAndFeel;

    WaveformDisplay waveformDisplay;
    PadGridComponent padGrid;

    // Top bar & Performance buttons
    juce::ComboBox vintageModeBox;
    LabeledKnob masterGainKnob;

    juce::TextButton fullLevelBtn { "FULL LEVEL" };
    juce::TextButton sixteenLevelsBtn { "16 LEVELS (PITCH)" };
    juce::TextButton loadSampleBtn { "LOAD FILE..." };
    juce::TextButton resetPadBtn { "RESET PAD" };
    juce::TextButton factoryKitBtn { "FACTORY KIT" };

    // Selected Pad Toolbar
    juce::ComboBox stretchModeBox;
    juce::ComboBox chokeGroupBox;
    juce::TextButton oneShotBtn { "ONE-SHOT" };
    juce::TextButton reverseBtn { "REVERSE" };

    // Section 1: Pitch & Time-Stretch Knobs
    LabeledKnob pitchKnob;
    LabeledKnob fineTuneKnob;
    LabeledKnob timeStretchKnob;
    LabeledKnob grainSizeKnob;

    // Section 2: Bitcrusher & Decimator Knobs
    LabeledKnob bitDepthKnob;
    LabeledKnob downsampleKnob;
    LabeledKnob driveKnob;
    LabeledKnob crushMixKnob;

    // Section 3: Amp & ADSR Knobs
    LabeledKnob attackKnob;
    LabeledKnob decayKnob;
    LabeledKnob sustainKnob;
    LabeledKnob releaseKnob;
    LabeledKnob levelKnob;
    LabeledKnob panKnob;

    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isSyncingUI = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MPC16SamplerAudioProcessorEditor)
};
