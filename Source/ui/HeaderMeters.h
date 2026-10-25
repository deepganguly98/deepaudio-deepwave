#pragma once
#include "ModernLookAndFeel.h"
#include <atomic>

// Output level meter (smooth, peak-hold, cyan->violet->pink, red clip tip) plus a row of
// voice dots. Polls two atomics at 30 Hz and only repaints when something visibly changed.
class HeaderMeters : public juce::Component,
                     private juce::Timer
{
public:
    HeaderMeters (std::atomic<float>& peakSource, std::atomic<int>& voiceSource, int maxVoicesToShow = 15)
        : peak (peakSource), voices (voiceSource), maxVoices (maxVoicesToShow)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
        startTimerHz (30);
    }

    ~HeaderMeters() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float barH = juce::jmax (6.0f, b.getHeight() * 0.28f);
        auto bar = juce::Rectangle<float> (b.getX(), b.getY() + 3.0f, b.getWidth(), barH);

        g.setColour (juce::Colour (0x1affffff));
        g.fillRoundedRectangle (bar, barH * 0.5f);

        const float fillW = bar.getWidth() * shownLevel;
        if (fillW > 1.0f)
        {
            juce::ColourGradient grad (Theme::cyan, bar.getX(), 0.0f, Theme::pink, bar.getRight(), 0.0f, false);
            grad.addColour (0.6, Theme::violet);
            g.saveState();
            g.reduceClipRegion (bar.getSmallestIntegerContainer().withWidth ((int) std::ceil (fillW)));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, barH * 0.5f);
            g.restoreState();
        }

        if (shownHold > 0.01f)
        {
            const float hx = bar.getX() + juce::jmin (bar.getWidth() - 2.0f, bar.getWidth() * shownHold);
            g.setColour (Theme::text.withAlpha (0.85f));
            g.fillRect (hx, bar.getY(), 2.0f, barH);
        }

        if (clipFrames > 0)
        {
            g.setColour (juce::Colour (0xffff3b4e));
            g.fillRoundedRectangle (bar.getRight() - 6.0f, bar.getY(), 6.0f, barH, barH * 0.5f);
        }

        // voice dots
        const float dotsY = bar.getBottom() + 6.0f;
        const float step = b.getWidth() / (float) maxVoices;
        const float d = juce::jlimit (3.0f, 7.0f, step * 0.55f);
        for (int i = 0; i < maxVoices; ++i)
        {
            const float cx = b.getX() + step * ((float) i + 0.5f);
            const bool on = i < shownVoices;
            if (on)
            {
                g.setColour (Theme::cyan.interpolatedWith (Theme::pink, (float) i / (float) juce::jmax (1, maxVoices - 1)));
                g.fillEllipse (cx - d * 0.5f, dotsY, d, d);
            }
            else
            {
                g.setColour (juce::Colour (0x22ffffff));
                g.fillEllipse (cx - d * 0.5f, dotsY, d, d);
            }
        }
    }

private:
    void timerCallback() override
    {
        const float p = juce::jmax (0.0f, peak.load (std::memory_order_relaxed));
        const int v = juce::jlimit (0, maxVoices, voices.load (std::memory_order_relaxed));

        const float db = juce::Decibels::gainToDecibels (p, -60.0f);
        const float inst = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f));

        level = juce::jmax (inst, level * 0.84f);
        if (level < 0.002f) level = 0.0f;

        if (inst >= hold) { hold = inst; holdFrames = 30; }
        else if (holdFrames > 0) --holdFrames;
        else hold = juce::jmax (0.0f, hold - 0.02f);

        if (p >= 0.98f) clipFrames = 45;
        else if (clipFrames > 0) --clipFrames;

        const bool changed = std::abs (level - shownLevel) > 0.004f
                          || std::abs (hold - shownHold) > 0.004f
                          || v != shownVoices
                          || (clipFrames > 0) != (shownClip);
        if (changed)
        {
            shownLevel = level; shownHold = hold; shownVoices = v; shownClip = clipFrames > 0;
            repaint();
        }
    }

    std::atomic<float>& peak;
    std::atomic<int>& voices;
    int maxVoices;
    float level = 0.0f, hold = 0.0f, shownLevel = 0.0f, shownHold = 0.0f;
    int holdFrames = 0, clipFrames = 0, shownVoices = 0;
    bool shownClip = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderMeters)
};
