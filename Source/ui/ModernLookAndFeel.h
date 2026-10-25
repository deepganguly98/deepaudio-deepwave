#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Shared palette + a thin, glowing "instrument panel" look.
namespace Theme
{
    inline const juce::Colour bg0     { 0xff05060b };
    inline const juce::Colour bg1     { 0xff0b0d17 };
    inline const juce::Colour panel   { 0x99121522 };
    inline const juce::Colour stroke  { 0x22ffffff };
    inline const juce::Colour text    { 0xffe8ecf8 };
    inline const juce::Colour dim     { 0xff7d849c };
    inline const juce::Colour cyan    { 0xff4de8ff };
    inline const juce::Colour violet  { 0xffa07bff };
    inline const juce::Colour pink    { 0xffff6bd6 };

    inline juce::Font ui (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0x18ffffff));
        setColour (juce::ComboBox::outlineColourId, Theme::stroke);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::arrowColourId, Theme::cyan);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff10121c));
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::violet.withAlpha (0.35f));
        setColour (juce::Label::textColourId, Theme::dim);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0x18ffffff));
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::cyan);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0151827));
        setColour (juce::TooltipWindow::textColourId, Theme::text);
        setColour (juce::TooltipWindow::outlineColourId, Theme::stroke);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff10121c));
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::violet.withAlpha (0.5f));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0a0c14));
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::stroke);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::cyan);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        auto c = b.getCentre();
        const float r = d * 0.5f, thick = 3.0f;
        const float ang = startAngle + pos * (endAngle - startAngle);
        const bool active = slider.isMouseOverOrDragging() && slider.isEnabled();
        const bool dragging = slider.isMouseButtonDown();

        juce::Path track;
        track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0x22ffffff));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (pos > 0.002f)
        {
            juce::Path v;
            v.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, startAngle, ang, true);
            g.setGradientFill (juce::ColourGradient (Theme::cyan, c.x - r, c.y, Theme::violet, c.x + r, c.y, false));
            g.setOpacity (dragging ? 0.45f : active ? 0.34f : 0.22f);
            g.strokePath (v, juce::PathStrokeType (thick + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (juce::ColourGradient (Theme::cyan, c.x - r, c.y, Theme::violet, c.x + r, c.y, false));
            g.strokePath (v, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // inner disc + pointer
        g.setColour (juce::Colour (0xff0e1019));
        g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
        g.setColour (active ? Theme::cyan.withAlpha (0.55f) : juce::Colour (0x18ffffff));
        g.drawEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f, 1.0f);
        juce::Point<float> tip (c.x + std::sin (ang) * r * 0.5f, c.y - std::cos (ang) * r * 0.5f);
        g.setColour (Theme::text);
        g.drawLine (c.x + std::sin (ang) * r * 0.22f, c.y - std::cos (ang) * r * 0.22f, tip.x, tip.y, 2.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.16f), 0, 0,
                                                 Theme::violet.withAlpha (0.16f), (float) w, 0, false));
        g.fillRoundedRectangle (r, h * 0.5f);
        g.setColour (box.isPopupActive() ? Theme::cyan : Theme::stroke);
        g.drawRoundedRectangle (r, h * 0.5f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 20.0f, ay = h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (Theme::cyan);
        g.fillPath (arrow);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::ui (14.0f, true); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override
    {
        l.setBounds (16, 1, box.getWidth() - 40, box.getHeight() - 2);
        l.setFont (getComboBoxFont (box));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        if (on)
        {
            // optional per-button lit colour: button.getProperties().set ("onColour", (int) colour.getARGB())
            juce::Colour lit = Theme::cyan;
            if (b.getProperties().contains ("onColour"))
                lit = juce::Colour ((juce::uint32) (int) b.getProperties()["onColour"]);
            g.setColour (lit.withAlpha (down ? 0.42f : hover ? 0.34f : 0.26f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (lit.withAlpha (0.8f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            return;
        }
        g.setColour (juce::Colour (down ? 0x40ffffff : hover ? 0x28ffffff : 0x14ffffff));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (hover ? Theme::violet.withAlpha (0.6f) : Theme::stroke);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::ui (juce::jmin (14.0f, (float) buttonHeight * 0.42f), true);
    }
};
