#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class WaveformDisplay : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    std::function<void()> onSampleOrTrimChanged;

    explicit WaveformDisplay(MPC16SamplerAudioProcessor& proc)
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

    void fileDragEnter(const juce::StringArray&, int, int) override
    {
        isDragOver = true;
        repaint();
    }

    void fileDragExit(const juce::StringArray&) override
    {
        isDragOver = false;
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        isDragOver = false;
        if (files.size() > 0)
        {
            const int padIdx = processor.getSelectedPadIndex();
            if (processor.loadSampleForPad(padIdx, juce::File(files[0])))
            {
                if (onSampleOrTrimChanged)
                    onSampleOrTrimChanged();
            }
        }
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float w = static_cast<float>(getWidth());
        if (w <= 10.0f)
            return;

        auto& pad = processor.getPad(processor.getSelectedPadIndex());
        const float startX = pad.startPoint.load() * w;
        const float endX   = pad.endPoint.load() * w;
        const float mx     = static_cast<float>(e.x);

        if (std::abs(mx - startX) < 12.0f)
            dragMode = DragMode::StartHandle;
        else if (std::abs(mx - endX) < 12.0f)
            dragMode = DragMode::EndHandle;
        else
        {
            // Audition pad when clicking inside the waveform body
            dragMode = DragMode::None;
            processor.triggerPadFromUI(processor.getSelectedPadIndex(), 0.9f);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::None)
            return;

        const float w = static_cast<float>(std::max(1, getWidth()));
        const float normX = juce::jlimit(0.0f, 1.0f, static_cast<float>(e.x) / w);
        auto& pad = processor.getPad(processor.getSelectedPadIndex());

        if (dragMode == DragMode::StartHandle)
        {
            const float maxStart = std::max(0.0f, pad.endPoint.load() - 0.02f);
            pad.startPoint.store(juce::jlimit(0.0f, maxStart, normX));
        }
        else if (dragMode == DragMode::EndHandle)
        {
            const float minEnd = std::min(1.0f, pad.startPoint.load() + 0.02f);
            pad.endPoint.store(juce::jlimit(minEnd, 1.0f, normX));
        }

        if (onSampleOrTrimChanged)
            onSampleOrTrimChanged();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragMode == DragMode::None)
            processor.releasePadFromUI(processor.getSelectedPadIndex());
        dragMode = DragMode::None;
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff111418));
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(isDragOver ? juce::Colour(0xffff7a00) : juce::Colour(0xff2c333d));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 6.0f, isDragOver ? 2.0f : 1.2f);

        const int padIdx = processor.getSelectedPadIndex();
        const auto& pad = processor.getPad(padIdx);
        auto samplePtr = std::atomic_load(&pad.sample);

        const float w = bounds.getWidth();
        const float h = bounds.getHeight();
        const float midY = h * 0.5f;

        // Subtle grid lines
        g.setColour(juce::Colour(0xff1d222a));
        g.drawHorizontalLine(static_cast<int>(midY), 2.0f, w - 2.0f);
        for (int q = 1; q < 4; ++q)
            g.drawVerticalLine(static_cast<int>(w * 0.25f * static_cast<float>(q)), 2.0f, h - 2.0f);

        if (!samplePtr || samplePtr->buffer.getNumSamples() == 0)
        {
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(14.0f));
            g.drawText("Drop WAV / AIFF / FLAC / MP3 Here", bounds, juce::Justification::centred);
            return;
        }

        const auto& buf = samplePtr->buffer;
        const int numFrames = buf.getNumSamples();
        const float* ch0 = buf.getReadPointer(0);
        const bool isReverse = pad.reverse.load();

        // Draw waveform columns
        const int plotWidth = std::max(1, static_cast<int>(w) - 8);
        const float startNorm = pad.startPoint.load();
        const float endNorm   = pad.endPoint.load();

        // Visual hint of downsample step on the waveform
        const int crushVisualStep = juce::jlimit(1, 8, static_cast<int>(std::sqrt(pad.downsampleFactor.load())));

        for (int x = 0; x < plotWidth; x += crushVisualStep)
        {
            const float norm0 = static_cast<float>(x) / static_cast<float>(plotWidth);
            const float norm1 = static_cast<float>(x + crushVisualStep) / static_cast<float>(plotWidth);

            const float srcNorm0 = isReverse ? (1.0f - norm1) : norm0;
            const float srcNorm1 = isReverse ? (1.0f - norm0) : norm1;

            const int f0 = juce::jlimit(0, numFrames - 1, static_cast<int>(srcNorm0 * static_cast<float>(numFrames)));
            const int f1 = juce::jlimit(f0 + 1, numFrames, static_cast<int>(srcNorm1 * static_cast<float>(numFrames)) + 1);

            float minVal = 0.0f;
            float maxVal = 0.0f;
            for (int f = f0; f < f1; ++f)
            {
                const float v = ch0[f];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            const bool insideTrim = (norm0 >= startNorm && norm0 <= endNorm);
            g.setColour(insideTrim ? juce::Colour(0xffff8811) : juce::Colour(0xff3d4652));

            const float topY = midY - maxVal * (h * 0.42f);
            const float botY = midY - minVal * (h * 0.42f);
            g.fillRect(4.0f + static_cast<float>(x), topY, static_cast<float>(crushVisualStep), std::max(1.5f, botY - topY));
        }

        // Shade trimmed-out regions
        const float startX = startNorm * w;
        const float endX   = endNorm * w;
        g.setColour(juce::Colour(0x66000000));
        if (startX > 0.0f)
            g.fillRect(0.0f, 0.0f, startX, h);
        if (endX < w)
            g.fillRect(endX, 0.0f, w - endX, h);

        // Draw Start & End Trim Markers with grab handles
        g.setColour(juce::Colour(0xff00e5ff));
        g.drawLine(startX, 2.0f, startX, h - 2.0f, 2.0f);
        juce::Path startFlag;
        startFlag.addTriangle(startX, 2.0f, startX + 10.0f, 2.0f, startX, 12.0f);
        g.fillPath(startFlag);

        g.setColour(juce::Colour(0xffff4081));
        g.drawLine(endX, 2.0f, endX, h - 2.0f, 2.0f);
        juce::Path endFlag;
        endFlag.addTriangle(endX, 2.0f, endX - 10.0f, 2.0f, endX, 12.0f);
        g.fillPath(endFlag);

        // Draw live playhead cursor
        const float playhead = pad.playheadNormalized.load(std::memory_order_relaxed);
        if (playhead >= 0.0f && playhead <= 1.0f)
        {
            const float dispPlayhead = isReverse ? (1.0f - playhead) : playhead;
            const float px = dispPlayhead * w;
            g.setColour(juce::Colours::white);
            g.drawLine(px, 2.0f, px, h - 2.0f, 2.0f);
        }

        // Overlay HUD info (Pad Number, Sample Name, Duration, Pitch/Stretch readout)
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        const double durMs = (static_cast<double>(numFrames) / samplePtr->sampleRate) * 1000.0;
        juce::String headerText = "PAD " + juce::String(padIdx + 1).paddedLeft('0', 2)
                                + "  |  " + samplePtr->name
                                + "  (" + juce::String(durMs, 0) + " ms, "
                                + juce::String(samplePtr->sampleRate / 1000.0, 1) + " kHz)";
        if (isReverse)
            headerText += "  [REV]";
        g.drawText(headerText, 10, 6, static_cast<int>(w) - 20, 18, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff9aa5b4));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Drag Start/End markers to trim  |  Click waveform to audition  |  Drop audio file to load",
                   10, static_cast<int>(h) - 20, static_cast<int>(w) - 20, 16, juce::Justification::centredRight);
    }

private:
    enum class DragMode
    {
        None,
        StartHandle,
        EndHandle
    };

    MPC16SamplerAudioProcessor& processor;
    DragMode dragMode = DragMode::None;
    bool isDragOver = false;
};
