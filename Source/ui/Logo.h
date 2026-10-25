#pragma once
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// WaveGrid logo mark: three parallel glowing strings with a gentle sine ripple,
// cyan -> violet -> pink. Mirrors assets/icon.svg. Header-only, no state.
namespace Logo
{
    inline void drawMark (juce::Graphics& g, juce::Rectangle<float> area)
    {
        // Design space: x in [-200, 200], y in [-120, 120] (strings + end dots).
        constexpr float halfW = 215.0f, halfH = 125.0f;
        const float s = juce::jmin (area.getWidth() / (2.0f * halfW),
                                    area.getHeight() / (2.0f * halfH));
        if (s <= 0.0f)
            return;

        const auto c = area.getCentre();
        auto tx = [&] (float x) { return c.x + x * s; };
        auto ty = [&] (float y) { return c.y + y * s; };

        const auto cyan   = juce::Colour (0xff4de8ff);
        const auto violet = juce::Colour (0xffa07bff);
        const auto pink   = juce::Colour (0xffff6bd6);

        juce::ColourGradient grad (cyan, tx (-200.0f), c.y, pink, tx (200.0f), c.y, false);
        grad.addColour (0.5, violet);

        struct String { float y0, amp, width; };
        const String strings[] = { { -92.0f, 20.0f, 10.0f },
                                   {   0.0f, 46.0f, 14.0f },
                                   {  92.0f, 20.0f, 10.0f } };

        // Keep lines at least ~1.5px so they stay readable at 24px.
        const float minW = 1.5f;

        for (const auto& st : strings)
        {
            juce::Path p;
            constexpr int n = 48;
            for (int i = 0; i <= n; ++i)
            {
                const float t = (float) i / (float) n;
                const float x = -200.0f + 400.0f * t;
                const float y = st.y0 + st.amp * std::sin (juce::MathConstants<float>::twoPi * t);
                if (i == 0) p.startNewSubPath (tx (x), ty (y));
                else        p.lineTo (tx (x), ty (y));
            }

            const float w = juce::jmax (minW, st.width * s);

            // glow: wider translucent strokes
            g.setGradientFill (grad);
            g.setOpacity (0.10f);
            g.strokePath (p, juce::PathStrokeType (w * 4.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            g.setGradientFill (grad);
            g.setOpacity (0.22f);
            g.strokePath (p, juce::PathStrokeType (w * 2.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            // core
            g.setGradientFill (grad);
            g.setOpacity (1.0f);
            g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            // hot centre line on the main string
            if (st.width >= 14.0f && w > 3.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.strokePath (p, juce::PathStrokeType (w * 0.28f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }

            // nut / bridge dots
            const float r = juce::jmax (minW, st.width * 0.9f * s);
            g.setColour (cyan);
            g.fillEllipse (tx (-200.0f) - r, ty (st.y0) - r, 2.0f * r, 2.0f * r);
            g.setColour (pink);
            g.fillEllipse (tx (200.0f) - r, ty (st.y0) - r, 2.0f * r, 2.0f * r);
        }
    }
}
