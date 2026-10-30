#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PadGridComponent : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    std::function<void(int)> onPadSelected;

    explicit PadGridComponent(MPC16SamplerAudioProcessor& proc)
        : processor(proc)
    {
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& f : files)
        {
            if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") ||
                f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac") ||
                f.endsWithIgnoreCase(".ogg") || f.endsWithIgnoreCase(".mp3"))
                return true;
        }
        return false;
    }

    void fileDragEnter(const juce::StringArray&, int x, int y) override
    {
        hoverPadIndex = getPadAtPosition(x, y);
        repaint();
    }

    void fileDragMove(const juce::StringArray&, int x, int y) override
    {
        const int newHover = getPadAtPosition(x, y);
        if (newHover != hoverPadIndex)
        {
            hoverPadIndex = newHover;
            repaint();
        }
    }

    void fileDragExit(const juce::StringArray&) override
    {
        hoverPadIndex = -1;
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        const int padIdx = getPadAtPosition(x, y);
        hoverPadIndex = -1;
        if (padIdx >= 0 && files.size() > 0)
        {
            // If multiple files are dropped at once, load them sequentially starting at padIdx!
            for (int i = 0; i < files.size() && (padIdx + i) < mpc16::kNumPads; ++i)
                processor.loadSampleForPad(padIdx + i, juce::File(files[i]));

            processor.setSelectedPadIndex(padIdx);
            if (onPadSelected)
                onPadSelected(padIdx);
        }
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int padIdx = getPadAtPosition(e.x, e.y);
        if (padIdx < 0)
            return;

        pressedPadIndex = padIdx;
        processor.setSelectedPadIndex(padIdx);
        if (onPadSelected)
            onPadSelected(padIdx);

        // Compute velocity from vertical click position inside the pad (lower on pad = higher velocity, or top-to-bottom)
        const auto rect = getPadBounds(padIdx);
        const float relY = juce::jlimit(0.0f, 1.0f, (static_cast<float>(e.y) - rect.getY()) / std::max(1.0f, rect.getHeight()));
        const float velocity = juce::jlimit(0.25f, 1.0f, 0.35f + 0.65f * relY);

        processor.triggerPadFromUI(padIdx, velocity);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (pressedPadIndex >= 0)
        {
            processor.releasePadFromUI(pressedPadIndex);
            pressedPadIndex = -1;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const int selectedIdx = processor.getSelectedPadIndex();
        const bool is16Levels = processor.sixteenLevelsPitchMode.load(std::memory_order_relaxed);

        for (int padIdx = 0; padIdx < mpc16::kNumPads; ++padIdx)
        {
            const auto rect = getPadBounds(padIdx);
            const auto& pad = processor.getPad(padIdx);
            const float activity = juce::jlimit(0.0f, 1.0f, pad.triggerActivity.load(std::memory_order_relaxed));
            const bool isSelected = (padIdx == selectedIdx);
            const bool isHovered  = (padIdx == hoverPadIndex);

            // Pad body gradient (thick rubber MPC pad look)
            juce::Colour baseTop = isSelected ? juce::Colour(0xff38404d) : juce::Colour(0xff262b33);
            juce::Colour baseBot = isSelected ? juce::Colour(0xff282e38) : juce::Colour(0xff1c2026);

            if (activity > 0.02f)
            {
                baseTop = baseTop.interpolatedWith(juce::Colour(0xffff7700), activity * 0.75f);
                baseBot = baseBot.interpolatedWith(juce::Colour(0xffcc3300), activity * 0.65f);
            }

            juce::ColourGradient grad(baseTop, rect.getX(), rect.getY(),
                                      baseBot, rect.getX(), rect.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(rect, 7.0f);

            // Outer border / LED rim
            if (isHovered)
                g.setColour(juce::Colour(0xff00e5ff));
            else if (activity > 0.05f)
                g.setColour(juce::Colour(0xffffaa33).withAlpha(0.6f + 0.4f * activity));
            else if (isSelected)
                g.setColour(juce::Colour(0xffff7a00));
            else
                g.setColour(juce::Colour(0xff373e4a));

            g.drawRoundedRectangle(rect.reduced(1.0f), 7.0f, (isSelected || isHovered || activity > 0.05f) ? 2.2f : 1.2f);

            // Top row: Pad # and MIDI Note name
            const int midiNote = pad.midiNote.load(std::memory_order_relaxed);
            const juce::String noteName = juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3);

            g.setColour(isSelected ? juce::Colour(0xffffae42) : juce::Colour(0xff9aa5b4));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("A" + juce::String(padIdx + 1).paddedLeft('0', 2),
                       rect.toNearestInt().reduced(7, 5),
                       juce::Justification::topLeft);

            g.setColour(juce::Colour(0xff788496));
            g.setFont(juce::FontOptions(10.5f));
            g.drawText(noteName,
                       rect.toNearestInt().reduced(7, 5),
                       juce::Justification::topRight);

            // Center: Sample Name (or Semitone offset when 16-Levels Pitch Mode is active)
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            if (is16Levels)
            {
                const int st = padIdx - 4;
                const juce::String stText = (st >= 0 ? "+" : "") + juce::String(st) + " ST";
                g.drawText(stText, rect.toNearestInt().reduced(6, 20), juce::Justification::centred);
            }
            else
            {
                auto samplePtr = std::atomic_load(&pad.sample);
                const juce::String sampleName = samplePtr ? samplePtr->name : "Empty";
                g.drawText(sampleName, rect.toNearestInt().reduced(6, 18), juce::Justification::centred);
            }

            // Bottom badges: Choke group / Reverse / Bitcrushed indicator
            juce::String badges;
            const int choke = pad.chokeGroup.load(std::memory_order_relaxed);
            if (choke > 0)
                badges += "CHK" + juce::String(choke) + " ";
            if (pad.reverse.load(std::memory_order_relaxed))
                badges += "REV ";
            if (pad.bitDepth.load(std::memory_order_relaxed) < 15.9f || pad.downsampleFactor.load(std::memory_order_relaxed) > 1.05f)
                badges += "LO-FI";

            if (badges.isNotEmpty())
            {
                g.setColour(juce::Colour(0xff00e5ff).withAlpha(0.8f));
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(badges.trim(), rect.toNearestInt().reduced(6, 5), juce::Justification::bottomLeft);
            }
        }
    }

private:
    // Classic MPC Layout: Pad 1 (index 0) is bottom-left, Pad 16 (index 15) is top-right
    juce::Rectangle<float> getPadBounds(int padIndex) const
    {
        const int col = padIndex % 4;
        const int rowFromBottom = padIndex / 4;
        const int rowFromTop = 3 - rowFromBottom;

        const float gap = 8.0f;
        const float totalW = static_cast<float>(getWidth());
        const float totalH = static_cast<float>(getHeight());
        const float cellW = (totalW - gap * 3.0f) / 4.0f;
        const float cellH = (totalH - gap * 3.0f) / 4.0f;

        const float x = static_cast<float>(col) * (cellW + gap);
        const float y = static_cast<float>(rowFromTop) * (cellH + gap);
        return { x, y, cellW, cellH };
    }

    int getPadAtPosition(int x, int y) const
    {
        const juce::Point<float> pt(static_cast<float>(x), static_cast<float>(y));
        for (int i = 0; i < mpc16::kNumPads; ++i)
        {
            if (getPadBounds(i).contains(pt))
                return i;
        }
        return -1;
    }

    MPC16SamplerAudioProcessor& processor;
    int hoverPadIndex = -1;
    int pressedPadIndex = -1;
};
