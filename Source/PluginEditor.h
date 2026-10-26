#pragma once
#include "PluginProcessor.h"
#include "ui/GridSurface.h"
#include "ui/ModernLookAndFeel.h"
#include "ui/HeaderMeters.h"
#include "ui/Scales.h"

// Slider that tells its owner when the mouse enters/leaves (for the value readout).
class ReadoutSlider : public juce::Slider
{
public:
    std::function<void (bool)> onHover;

    void mouseEnter (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseEnter (e);
        if (onHover) onHover (true);
        repaint();
    }
    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseExit (e);
        if (onHover) onHover (false);
        repaint();
    }
};

// Distinct accent per preset category (index into kPresetCategories).
inline juce::Colour presetCategoryColour (int c)
{
    static const juce::uint32 cols[] = { 0xffa07bff, 0xff4de8ff, 0xff6b8bff, 0xffff6bd6,
                                         0xff7dffb0, 0xffffb454, 0xffff7a6b, 0xffc4a8ff };
    return juce::Colour (cols[juce::jlimit (0, 7, c) % 8]);
}

inline int presetCategoryIndex (const char* name)
{
    for (int c = 0; c < kNumPresetCategories; ++c)
        if (juce::String (kPresetCategories[c]) == name) return c;
    return 0;
}

// ComboBox whose popup is replaced by a categorised preset browser (supplied by the owner).
// Items are still held in the ComboBox, so selection/getText/silent sync work unchanged.
class PresetCombo : public juce::ComboBox
{
public:
    std::function<void()> onShowPopup;

    void showPopup() override
    {
        if (onShowPopup) onShowPopup(); else juce::ComboBox::showPopup();
    }

    void paint (juce::Graphics& g) override
    {
        juce::ComboBox::paint (g);
        if (presetIndex < 0 || presetIndex >= kNumVoicePresets) return;
        const auto* p = &kVoicePresets[presetIndex];
        const int cat = presetCategoryIndex (p->category);
        const auto col = presetCategoryColour (cat);
        const float cy = (float) getHeight() * 0.5f;
        g.setColour (col.withAlpha (0.35f));
        g.fillEllipse (4.0f, cy - 5.0f, 10.0f, 10.0f);
        g.setColour (col);
        g.fillEllipse (6.0f, cy - 3.0f, 6.0f, 6.0f);

        const auto font = Theme::ui (10.0f, true);
        const juce::String catText = juce::String (p->category).toUpperCase();
        const int catW = juce::GlyphArrangement::getStringWidthInt (font, catText);
        const int nameW = juce::GlyphArrangement::getStringWidthInt (Theme::ui (14.0f, true), p->name);
        if (16 + nameW + 12 + catW + 28 < getWidth())
        {
            g.setColour (col.withAlpha (0.85f));
            g.setFont (font);
            g.drawText (catText, getWidth() - 28 - catW - 2, 0, catW + 2, getHeight(), juce::Justification::centredRight);
        }
    }

    int presetIndex = -1;
};

class WaveGridEditor : public juce::AudioProcessorEditor,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit WaveGridEditor (WaveGridProcessor&);
    ~WaveGridEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    GridSurface& getGrid() { return grid; }   // used by the README screenshot tool

    // computer keyboard = piano: A W S E D F T G Y H U J K O L P, Z / X = octave down / up
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void parentHierarchyChanged() override;

private:
    struct Knob
    {
        ReadoutSlider s;
        juce::Label l;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a;
        juce::String name;
        bool advanced = false;
        bool hovering = false;

        void refreshLabel();
    };

    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        int extrasKind = 0;          // 0 none, 1 grid (scale/root/hammer), 2 arp (toggles + boxes)
        float extraCells = 0.0f;
        juce::Rectangle<int> bounds;
        bool visible = true;
    };

    Knob* addKnob (Group& group, const char* id, const juce::String& name, const juce::String& tip, bool advanced = false);
    void configureReadout (Knob& k, const juce::String& id);
    void parameterChanged (const juce::String&, float) override;
    void syncGridConfig();
    void stepVoice (int delta);
    void updateAdvancedVisibility();
    void showPresetMenu();
    void updateVoiceDisplay();
    bool isArpOn() const;

    void promptSavePreset();
    void showUserPresetMenu();
    void toggleAB();
    void withVoiceApplySuppressed (const std::function<void()>& fn);

    WaveGridProcessor& proc;
    ModernLookAndFeel laf;
    GridSurface grid;
    HeaderMeters meters;
    juce::TooltipWindow tooltip { this, 600 };

    PresetCombo voiceBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    juce::TextButton saveBtn { "SAVE" }, userBtn { "USER" }, abBtn { "A" }, advBtn { "ADVANCED" };

    juce::ComboBox scaleBox, rootBox;
    juce::TextButton hammerBtn { "HAMMER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAttach, rootAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hammerAttach;

    juce::TextButton arpBtn { "ARP" }, latchBtn { "LATCH" };
    juce::ComboBox arpPatternBox, arpRateBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpOnAttach, arpLatchAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> arpPatternAttach, arpRateAttach;
    juce::Rectangle<int> arpBadge { 214, 14, 46, 20 };

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Group> groups;

    juce::ValueTree snapA, snapB;
    bool onB = false;
    int ignoreVoiceChange = 0;

    int kbOctave = 3;                // computer-keyboard octave: A = C<kbOctave>
    std::map<int, int> heldKeys;     // key code -> MIDI note sounding
    bool octDownHeld = false, octUpHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveGridEditor)
};
