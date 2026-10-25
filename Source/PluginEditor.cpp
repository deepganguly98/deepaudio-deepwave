#include "PluginEditor.h"
#include "ui/Logo.h"

namespace
{
    struct KeyNote { int code; int semitone; };
    // classic tracker/DAW layout: home row = naturals, row above = sharps
    constexpr KeyNote kKeyMap[] = {
        { 'A', 0 }, { 'W', 1 }, { 'S', 2 }, { 'E', 3 }, { 'D', 4 }, { 'F', 5 }, { 'T', 6 }, { 'G', 7 },
        { 'Y', 8 }, { 'H', 9 }, { 'U', 10 }, { 'J', 11 }, { 'K', 12 }, { 'O', 13 }, { 'L', 14 }, { 'P', 15 },
    };
}

static const char* kArpBadgeParam = ParamID::arpOn;

static const char* kGridParams[] = { ParamID::snap, ParamID::rows, ParamID::baseNote, ParamID::rowInterval,
                                     ParamID::scale, ParamID::root, ParamID::vibrato, ParamID::hammer };

static const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void WaveGridEditor::Knob::refreshLabel()
{
    l.setText (hovering ? s.getTextFromValue (s.getValue()) : name, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, hovering ? Theme::cyan : Theme::dim);
}

WaveGridEditor::WaveGridEditor (WaveGridProcessor& p)
    : AudioProcessorEditor (p), proc (p), grid (p.getGridCollector()),
      meters (p.outputPeak, p.activeVoiceCount)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (grid);
    addAndMakeVisible (meters);

    // ---- preset / header controls
    for (int i = 0; i < kNumVoicePresets; ++i) voiceBox.addItem (kVoicePresets[i].name, i + 1);
    addAndMakeVisible (voiceBox);
    // No ComboBoxAttachment: it notifies onChange for host/state-driven changes too, which
    // would re-apply the factory preset over restored or automated parameters. Only a user
    // pick applies a preset; param -> combo sync below is silent.
    if (auto* vp = proc.apvts.getParameter (ParamID::voice))
        voiceBox.setSelectedItemIndex (juce::roundToInt (vp->convertFrom0to1 (vp->getValue())), juce::dontSendNotification);
    voiceBox.onShowPopup = [this] { showPresetMenu(); };
    voiceBox.onChange = [this]
    {
        const int idx = voiceBox.getSelectedItemIndex();
        updateVoiceDisplay();
        if (auto* vp = proc.apvts.getParameter (ParamID::voice))
            vp->setValueNotifyingHost (vp->convertTo0to1 ((float) idx));
        if (ignoreVoiceChange == 0) proc.applyVoicePreset (idx);
    };
    voiceBox.setTooltip ("Factory voice browser: pick a category, then a voice. Sets all sound knobs at once");
    updateVoiceDisplay();
    prevBtn.onClick = [this] { stepVoice (-1); };
    nextBtn.onClick = [this] { stepVoice (+1); };
    saveBtn.onClick = [this] { promptSavePreset(); };
    userBtn.onClick = [this] { showUserPresetMenu(); };
    abBtn.onClick   = [this] { toggleAB(); };
    saveBtn.setTooltip ("Save the current sound as a user preset");
    userBtn.setTooltip ("Load or delete your saved presets");
    abBtn.setTooltip ("A/B compare: swap between two snapshots of all settings");
    prevBtn.setTooltip ("Previous voice");
    nextBtn.setTooltip ("Next voice");
    advBtn.setClickingTogglesState (true);
    advBtn.setTooltip ("Show or hide the less-used knobs");
    advBtn.onClick = [this] { updateAdvancedVisibility(); };
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &userBtn, &abBtn, &advBtn }) addAndMakeVisible (*b);
    abBtn.setToggleState (false, juce::dontSendNotification);

    // ---- knob groups
    groups.resize (7);
    groups[0].title = "EXCITATION";
    groups[1].title = "STRING";
    groups[2].title = "SPACE";
    groups[3].title = "MASTER";
    groups[4].title = "OUT";
    groups[5].title = "ARPEGGIATOR";
    groups[6].title = "GRID";
    groups[5].extrasKind = 2;
    groups[5].extraCells = 4.4f;
    groups[6].extrasKind = 1;
    groups[6].extraCells = 2.8f;

    auto& exc = groups[0]; auto& str = groups[1]; auto& spc = groups[2]; auto& mst = groups[3];
    auto& out = groups[4]; auto& arpG = groups[5]; auto& grd = groups[6];

    addKnob (exc, ParamID::strike,  "STRIKE", "How hard the string is struck on note-on");
    addKnob (exc, ParamID::attack,  "ATTACK", "Fade-in time of the bowed excitation");
    addKnob (exc, ParamID::bow,     "BOW",    "Amount of continuous bow-like excitation");
    addKnob (exc, ParamID::hold,    "HOLD",   "How long the excitation is sustained while held", true);
    addKnob (exc, ParamID::breath,  "BREATH", "Adds breath noise to the excitation", true);

    addKnob (str, ParamID::sustain,    "SUSTAIN", "How long the string rings");
    addKnob (str, ParamID::brightness, "BRIGHT",  "Brightness of the tone");
    addKnob (str, ParamID::drive,      "DRIVE",   "Soft saturation inside the string loop");
    addKnob (str, ParamID::inharmonic, "STIFF",   "String stiffness: stretches the overtones", true);
    addKnob (str, ParamID::pickPos,    "PICK",    "Where along the string it is excited", true);
    addKnob (str, ParamID::glide,      "GLIDE",   "Pitch glide time between notes", true);

    addKnob (spc, ParamID::body,     "BODY",     "Resonant instrument body amount");
    addKnob (spc, ParamID::sympathy, "SYMPATHY", "Sympathetic ringing of the other strings");
    addKnob (spc, ParamID::space,    "SPACE",    "Reverb amount");
    addKnob (spc, ParamID::detune,   "WIDTH",    "Stereo width and voice spread", true);
    addKnob (spc, ParamID::shimmer,  "SHIMMER",  "Octave-up shimmer in the reverb tail", true);
    addKnob (spc, ParamID::delayMix, "DELAY",    "Echo delay mix");
    addKnob (spc, ParamID::delayTime,"DLY TIME", "Echo delay time", true);

    addKnob (mst, ParamID::eqLow,    "EQ LOW",   "Bass tone: boost or cut the low end (-12 to +12 dB)");
    addKnob (mst, ParamID::eqHigh,   "EQ HIGH",  "Treble tone: boost or cut the high end (-12 to +12 dB)");
    addKnob (mst, ParamID::comp,     "GLUE",     "Gentle compression that glues the sound together and evens out loudness");
    addKnob (mst, ParamID::ensemble, "ENSEMBLE", "Chorus-like thickening that makes one string sound like several");
    addKnob (mst, ParamID::warmth,   "WARMTH",   "Soft analog-style saturation for a rounder, warmer tone");

    addKnob (out, ParamID::output, "OUT", "Master output level");

    addKnob (arpG, ParamID::arpOct,   "OCTAVES", "How many octaves the arpeggio climbs through before repeating");
    addKnob (arpG, ParamID::arpGate,  "GATE",    "Note length: short and staccato to long and legato");
    addKnob (arpG, ParamID::arpSwing, "SWING",   "Delays every second note for a shuffled, swung feel");
    addKnob (arpG, ParamID::arpBpm,   "FREE BPM","Arpeggiator tempo, used only when the host (DAW) provides no tempo, e.g. in the Standalone app");

    addKnob (grd, ParamID::snap,        "SNAP",    "Fret snap: how strongly pitch sticks to note centres");
    addKnob (grd, ParamID::rows,        "ROWS",    "Number of string rows on the grid");
    addKnob (grd, ParamID::vibrato,     "VIBRATO", "Automatic vibrato from finger wobble");
    addKnob (grd, ParamID::baseNote,    "BASE",    "Pitch of the lowest row", true);
    addKnob (grd, ParamID::rowInterval, "TUNING",  "Interval between rows in semitones (5 = fourths)", true);

    // scale / root / hammer
    for (int i = 0; i < kNumScales; ++i) scaleBox.addItem (kScales[i].name, i + 1);
    for (int i = 0; i < 12; ++i) rootBox.addItem (kNoteNames[i], i + 1);
    addAndMakeVisible (scaleBox); addAndMakeVisible (rootBox); addAndMakeVisible (hammerBtn);
    scaleAttach  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamID::scale, scaleBox);
    rootAttach   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamID::root, rootBox);
    hammerBtn.setClickingTogglesState (true);
    hammerAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamID::hammer, hammerBtn);
    scaleBox.setTooltip ("Scale highlighted on the grid");
    rootBox.setTooltip ("Root note of the scale");
    hammerBtn.setTooltip ("Hammer-on / pull-off: touching a sounding row re-pitches it instead of re-plucking");

    // arp widgets
    for (int i = 0; i < wgfx::kNumArpPatterns; ++i) arpPatternBox.addItem (wgfx::kArpPatternNames[i], i + 1);
    for (int i = 0; i < wgfx::kNumArpRates; ++i)    arpRateBox.addItem (wgfx::kArpRateNames[i], i + 1);
    addAndMakeVisible (arpBtn); addAndMakeVisible (latchBtn);
    addAndMakeVisible (arpPatternBox); addAndMakeVisible (arpRateBox);
    arpBtn.setClickingTogglesState (true);
    latchBtn.setClickingTogglesState (true);
    arpBtn.getProperties().set ("onColour", (int) Theme::pink.getARGB());
    arpBtn.setColour (juce::TextButton::textColourOnId, Theme::pink);
    arpOnAttach      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamID::arpOn, arpBtn);
    arpLatchAttach   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamID::arpLatch, latchBtn);
    arpPatternAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamID::arpPattern, arpPatternBox);
    arpRateAttach    = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamID::arpRate, arpRateBox);
    arpBtn.setTooltip ("Arpeggiator on/off: turns held notes into a repeating pattern");
    latchBtn.setTooltip ("Latch: keep the arpeggio playing after you let go, until you play new notes");
    arpPatternBox.setTooltip ("Order the held notes are played in (up, down, random, chord...)");
    arpRateBox.setTooltip ("Speed of the arpeggio as a note length of the tempo (1/16 = four notes per beat; T = triplets)");

    for (auto* id : kGridParams) proc.apvts.addParameterListener (id, this);
    proc.apvts.addParameterListener (ParamID::voice, this);
    proc.apvts.addParameterListener (kArpBadgeParam, this);
    syncGridConfig();
    updateAdvancedVisibility();

    setResizable (true, true);
    setResizeLimits (760, 700, 4096, 3072);
    // keyboard playing: the editor owns focus; no child control may steal letter keys
    setWantsKeyboardFocus (true);
    for (auto* c : getChildren())
    {
        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
    }
    addMouseListener (this, true);

    setSize (1120, 800);
}

WaveGridEditor::~WaveGridEditor()
{
    for (auto* id : kGridParams) proc.apvts.removeParameterListener (id, this);
    proc.apvts.removeParameterListener (ParamID::voice, this);
    proc.apvts.removeParameterListener (kArpBadgeParam, this);
    setLookAndFeel (nullptr);
}

void WaveGridEditor::stepVoice (int delta)
{
    const int n = voiceBox.getNumItems();
    voiceBox.setSelectedItemIndex ((voiceBox.getSelectedItemIndex() + delta + n) % n, juce::sendNotificationSync);
}

bool WaveGridEditor::isArpOn() const
{
    if (auto* v = proc.apvts.getRawParameterValue (ParamID::arpOn)) return v->load() > 0.5f;
    return false;
}

void WaveGridEditor::updateVoiceDisplay()
{
    voiceBox.presetIndex = voiceBox.getSelectedItemIndex();
    voiceBox.repaint();
}

void WaveGridEditor::showPresetMenu()
{
    const int cur = voiceBox.getSelectedItemIndex();
    juce::PopupMenu menu;
    for (int c = 0; c < kNumPresetCategories; ++c)
    {
        juce::PopupMenu sub;
        bool hasCurrent = false;
        for (int i = 0; i < kNumVoicePresets; ++i)
        {
            if (juce::String (kVoicePresets[i].category) != kPresetCategories[c]) continue;
            const bool ticked = (i == cur);
            hasCurrent = hasCurrent || ticked;
            sub.addItem (juce::PopupMenu::Item (kVoicePresets[i].name)
                             .setID (i + 1)
                             .setColour (presetCategoryColour (c))
                             .setTicked (ticked));
        }
        if (sub.getNumItems() > 0)
            menu.addSubMenu (kPresetCategories[c], sub, true, nullptr, hasCurrent);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&voiceBox)
                                                  .withMinimumWidth (voiceBox.getWidth()),
                        [sp = juce::Component::SafePointer<WaveGridEditor> (this)] (int res)
                        {
                            // a user pick only: goes through voiceBox.onChange (sets param + applies preset)
                            if (sp != nullptr && res > 0)
                                sp->voiceBox.setSelectedItemIndex (res - 1, juce::sendNotificationSync);
                        });
}

WaveGridEditor::Knob* WaveGridEditor::addKnob (Group& group, const char* id, const juce::String& name,
                                               const juce::String& tip, bool advanced)
{
    auto k = std::make_unique<Knob>();
    auto* kp = k.get();
    k->name = name;
    k->advanced = advanced;
    k->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k->s.setTooltip (tip);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.setFont (Theme::ui (10.5f, true));
    k->l.setInterceptsMouseClicks (false, false);
    k->l.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (k->s);
    addAndMakeVisible (k->l);
    k->a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->s);

    // after the attachment, so our formatting and default win
    configureReadout (*k, id);
    if (auto* par = proc.apvts.getParameter (id))
        k->s.setDoubleClickReturnValue (true, par->convertFrom0to1 (par->getDefaultValue()));

    k->s.onHover = [kp] (bool h) { kp->hovering = h; kp->refreshLabel(); };
    k->s.onValueChange = [kp] { if (kp->hovering) kp->refreshLabel(); };
    k->refreshLabel();

    group.knobs.push_back (kp);
    knobs.push_back (std::move (k));
    return kp;
}

void WaveGridEditor::configureReadout (Knob& k, const juce::String& id)
{
    auto* s = &k.s;
    auto pct = [s] (double v)
    {
        const double range = s->getMaximum() - s->getMinimum();
        return juce::String (juce::roundToInt (range > 0.0 ? (v - s->getMinimum()) / range * 100.0 : 0.0)) + " %";
    };

    if (id == ParamID::output)
        s->textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    else if (id == ParamID::attack || id == ParamID::glide)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    else if (id == ParamID::delayTime)
        s->textFromValueFunction = [s] (double v)
        {
            const double ms = s->getMaximum() <= 4.0 ? v * 1000.0 : v;   // seconds or ms range
            return juce::String (juce::roundToInt (ms)) + " ms";
        };
    else if (id == ParamID::eqLow || id == ParamID::eqHigh)
        s->textFromValueFunction = [] (double v)
        {
            return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB";
        };
    else if (id == ParamID::arpOct)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " oct"; };
    else if (id == ParamID::arpGate || id == ParamID::arpSwing)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    else if (id == ParamID::arpBpm)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " BPM"; };
    else if (id == ParamID::rows)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    else if (id == ParamID::baseNote)
        s->textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); };
    else if (id == ParamID::rowInterval)
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };
    else
        s->textFromValueFunction = pct;
}

void WaveGridEditor::updateAdvancedVisibility()
{
    const bool adv = advBtn.getToggleState();
    for (auto& k : knobs)
    {
        const bool show = adv || ! k->advanced;
        k->s.setVisible (show);
        k->l.setVisible (show);
    }
    resized();
    repaint();
}

void WaveGridEditor::withVoiceApplySuppressed (const std::function<void()>& fn)
{
    // Loading a preset/snapshot changes the "voice" choice param, whose combo would otherwise
    // re-apply the factory voice over the loaded values. Notifications may be async, so release
    // the guard from a queued message (FIFO after them).
    ++ignoreVoiceChange;
    fn();
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<WaveGridEditor> (this)]
                                     { if (sp != nullptr) --sp->ignoreVoiceChange; });
}

void WaveGridEditor::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Name for your preset:", juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("name", voiceBox.getText(), juce::String());
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true,
                         juce::ModalCallbackFunction::create ([sp = juce::Component::SafePointer<WaveGridEditor> (this), aw] (int result)
                         {
                             if (result != 1 || sp == nullptr) return;
                             const auto n = aw->getTextEditorContents ("name").trim();
                             if (n.isNotEmpty()) sp->proc.saveUserPreset (n);
                         }),
                         true);
}

void WaveGridEditor::showUserPresetMenu()
{
    const auto names = proc.getUserPresetNames();
    juce::PopupMenu m, del;
    if (names.isEmpty())
        m.addItem (9999, "(no user presets yet)", false);
    for (int i = 0; i < names.size(); ++i)
    {
        m.addItem (1000 + i, names[i]);
        del.addItem (2000 + i, names[i]);
    }
    if (! names.isEmpty())
    {
        m.addSeparator();
        m.addSubMenu ("Delete", del);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&userBtn),
                     juce::ModalCallbackFunction::create ([sp = juce::Component::SafePointer<WaveGridEditor> (this), names] (int res)
                     {
                         if (sp == nullptr || res == 0) return;
                         if (res >= 2000 && res < 2000 + names.size())
                             sp->proc.deleteUserPreset (names[res - 2000]);
                         else if (res >= 1000 && res < 1000 + names.size())
                             sp->withVoiceApplySuppressed ([sp, &names, res] { sp->proc.loadUserPreset (names[res - 1000]); });
                     }));
}

void WaveGridEditor::toggleAB()
{
    auto cur = proc.apvts.copyState();
    (onB ? snapB : snapA) = cur;
    onB = ! onB;
    auto& target = onB ? snapB : snapA;
    if (target.isValid())
    {
        auto copy = target.createCopy();
        withVoiceApplySuppressed ([this, copy] { proc.apvts.replaceState (copy); });
    }
    else
    {
        target = cur;   // first visit: slot starts as a copy of the current sound
    }
    abBtn.setButtonText (onB ? "B" : "A");
    abBtn.setToggleState (onB, juce::dontSendNotification);
}

bool WaveGridEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    const int c = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    for (auto& m : kKeyMap) if (m.code == c) return true;      // consume so the OS doesn't beep
    return c == 'Z' || c == 'X';
}

bool WaveGridEditor::keyStateChanged (bool)
{
    if (juce::ModifierKeys::currentModifiers.isCommandDown() || juce::ModifierKeys::currentModifiers.isCtrlDown())
    {
        grid.releaseAllKeys();
        heldKeys.clear();
        return false;
    }

    bool used = false;
    for (auto& m : kKeyMap)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (m.code);
        auto it = heldKeys.find (m.code);
        if (down && it == heldKeys.end())
        {
            const int note = juce::jlimit (0, 127, 12 * (kbOctave + 1) + m.semitone);
            heldKeys[m.code] = note;
            grid.keyNote (note, true);
            used = true;
        }
        else if (! down && it != heldKeys.end())
        {
            grid.keyNote (it->second, false);   // release the note that was pressed, even if octave moved
            heldKeys.erase (it);
            used = true;
        }
    }

    const bool zDown = juce::KeyPress::isKeyCurrentlyDown ('Z'), xDown = juce::KeyPress::isKeyCurrentlyDown ('X');
    if (zDown && ! octDownHeld) { kbOctave = juce::jmax (0, kbOctave - 1); repaint (0, 44, 360, 18); used = true; }
    if (xDown && ! octUpHeld)   { kbOctave = juce::jmin (7, kbOctave + 1); repaint (0, 44, 360, 18); used = true; }
    octDownHeld = zDown;
    octUpHeld = xDown;
    return used;
}

void WaveGridEditor::focusLost (FocusChangeType)
{
    grid.releaseAllKeys();     // never leave a key note stuck when focus moves away
    heldKeys.clear();
    octDownHeld = octUpHeld = false;
}

void WaveGridEditor::parentHierarchyChanged()
{
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<WaveGridEditor> (this)]
                                     { if (sp != nullptr && sp->isShowing()) sp->grabKeyboardFocus(); });
}

void WaveGridEditor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == ParamID::voice)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<WaveGridEditor> (this), newValue]
        {
            if (sp != nullptr)
            {
                sp->voiceBox.setSelectedItemIndex (juce::roundToInt (newValue), juce::dontSendNotification);
                sp->updateVoiceDisplay();
            }
        });
        return;
    }
    if (id == kArpBadgeParam)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<WaveGridEditor> (this)]
                                         { if (sp != nullptr) sp->repaint (sp->arpBadge.expanded (10)); });
        return;
    }
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<WaveGridEditor> (this)]
                                     { if (sp != nullptr) sp->syncGridConfig(); });
}

void WaveGridEditor::syncGridConfig()
{
    auto c = grid.getConfig();
    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    c.snapDeadzone = get (ParamID::snap);
    c.rows         = (int) get (ParamID::rows);
    c.baseNote     = (int) get (ParamID::baseNote);
    c.rowInterval  = (int) get (ParamID::rowInterval);
    c.scaleMask    = kScales[juce::jlimit (0, kNumScales - 1, (int) get (ParamID::scale))].mask;
    c.rootPc       = juce::jlimit (0, 11, (int) get (ParamID::root));
    c.autoVibrato  = get (ParamID::vibrato);
    c.hammerOn     = get (ParamID::hammer) > 0.5f;
    c.bendRangeSemis = kBendRangeSemis;
    grid.setConfig (c);
}

void WaveGridEditor::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth(), h = (float) getHeight();
    g.setGradientFill (juce::ColourGradient (Theme::bg1, 0, 0, Theme::bg0, w, h, false));
    g.fillAll();

    // soft aurora glows
    g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.16f), w * 0.1f, 0.0f,
                                             Theme::cyan.withAlpha (0.0f), w * 0.1f + w * 0.45f, 0.0f, true));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (Theme::violet.withAlpha (0.18f), w * 0.95f, h * 0.05f,
                                             Theme::violet.withAlpha (0.0f), w * 0.95f - w * 0.5f, h * 0.05f, true));
    g.fillRect (getLocalBounds());

    // logo mark + wordmark
    Logo::drawMark (g, juce::Rectangle<float> (18.0f, 8.0f, 44.0f, 44.0f));
    g.setGradientFill (juce::ColourGradient (Theme::cyan, 70, 0, Theme::violet, 238, 0, false));
    g.setFont (Theme::ui (22.0f, true));
    g.drawText ("DEEPWAVE", 70, 12, 180, 28, juce::Justification::centredLeft);
    g.setColour (Theme::dim);
    g.setFont (Theme::ui (11.0f));
    g.drawText ("expressive string instrument", 70, 36, 230, 14, juce::Justification::centredLeft);
    g.setColour (Theme::cyan.withAlpha (0.8f));
    g.setFont (Theme::ui (10.0f, true));
    g.drawText ("KEYS  A-P play   Z/X octave   A = C" + juce::String (kbOctave), 70, 50, 260, 12, juce::Justification::centredLeft);

    // ARP badge (only while the arpeggiator is on)
    if (isArpOn())
    {
        auto b = arpBadge.toFloat();
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Theme::pink.withAlpha (0.07f * (float) (4 - i)));
            g.fillRoundedRectangle (b.expanded ((float) i * 2.5f), b.getHeight() * 0.5f + (float) i * 2.5f);
        }
        g.setColour (Theme::pink.withAlpha (0.28f));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (Theme::pink);
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.2f);
        g.setFont (Theme::ui (11.0f, true));
        g.drawText ("ARP", arpBadge, juce::Justification::centred);
    }

    // glass panels with group headings
    for (auto& grp : groups)
    {
        if (! grp.visible) continue;
        auto f = grp.bounds.toFloat();
        g.setColour (juce::Colour (0x0fffffff));
        g.fillRoundedRectangle (f, 14.0f);
        g.setColour (Theme::stroke);
        g.drawRoundedRectangle (f.reduced (0.5f), 14.0f, 1.0f);

        g.setColour (Theme::violet.withAlpha (0.9f));
        g.setFont (Theme::ui (10.0f, true));
        g.drawText (grp.title, grp.bounds.getX() + 14, grp.bounds.getY() + 5, grp.bounds.getWidth() - 28, 14,
                    juce::Justification::centredLeft);
        g.setGradientFill (juce::ColourGradient (Theme::cyan.withAlpha (0.7f), f.getX() + 14.0f, 0.0f,
                                                 Theme::pink.withAlpha (0.0f), f.getRight() - 14.0f, 0.0f, false));
        g.fillRect (f.getX() + 14.0f, f.getY() + 20.0f, f.getWidth() - 28.0f, 1.0f);
    }
}

void WaveGridEditor::resized()
{
    const int width = getWidth();
    const bool narrow = width < 1180;
    const bool touch = width < 900;

    auto r = getLocalBounds().reduced (16, 12);

    // ---- header
    const int btnPad = touch ? 3 : 6;
    auto placePresetRow = [&] (juce::Rectangle<int> row)
    {
        row = row.reduced (0, btnPad);
        advBtn.setBounds (row.removeFromRight (96));  row.removeFromRight (6);
        abBtn.setBounds (row.removeFromRight (44));   row.removeFromRight (6);
        userBtn.setBounds (row.removeFromRight (60)); row.removeFromRight (6);
        saveBtn.setBounds (row.removeFromRight (60)); row.removeFromRight (10);
        nextBtn.setBounds (row.removeFromRight (36)); row.removeFromRight (4);
        voiceBox.setBounds (row.removeFromRight (juce::jmin (210, row.getWidth() - 40)));
        row.removeFromRight (4);
        prevBtn.setBounds (row.removeFromRight (36));
    };

    if (! narrow)
    {
        auto header = r.removeFromTop (48);
        header.removeFromLeft (310);
        placePresetRow (header.removeFromRight (640));
        header.removeFromRight (12);
        meters.setBounds (header.removeFromRight (180).withSizeKeepingCentre (180, 34));
    }
    else
    {
        auto row1 = r.removeFromTop (44);
        meters.setBounds (row1.removeFromRight (180).withSizeKeepingCentre (180, 34));
        placePresetRow (r.removeFromTop (44));
    }
    r.removeFromTop (6);

    // ---- control groups (flow layout, wraps rows)
    const int cellW = touch ? 60 : narrow ? 66 : 60, nominalCellH = touch ? 88 : narrow ? 84 : 76;
    const int titleH = 24, padX = 10, padB = 6, gap = 10;
    const int avail = r.getWidth();

    struct RowInfo { std::vector<Group*> gs; int need = 0; };
    std::vector<RowInfo> rows;
    auto cellsOf = [] (const Group& g)
    {
        int n = 0;
        for (auto* k : g.knobs) if (k->s.isVisible()) ++n;
        return (float) n + g.extraCells;
    };
    auto minWOf = [&] (const Group& g) { return (int) std::ceil (cellsOf (g) * (float) cellW) + 2 * padX; };

    for (auto& g : groups)
    {
        g.visible = cellsOf (g) > 0.0f;
        if (! g.visible) continue;
        const int mw = minWOf (g);
        if (rows.empty() || rows.back().need + gap + mw > avail)
            rows.push_back ({ {}, 0 });
        auto& row = rows.back();
        row.need += (row.gs.empty() ? 0 : gap) + mw;
        row.gs.push_back (&g);
    }

    // shrink panel height (down to a floor) so the grid keeps a playable minimum height
    const int gridMinH = touch ? 170 : 200;
    int cellH = nominalCellH;
    if (! rows.empty())
    {
        const int perRow = (r.getHeight() - gridMinH) / (int) rows.size() - (titleH + padB + gap);
        cellH = juce::jlimit (62, nominalCellH, perRow);
    }
    const int panelH = titleH + cellH + padB;
    int y = r.getY();
    for (auto& row : rows)
    {
        const int extra = juce::jmax (0, avail - row.need);
        int totalMin = 0;
        for (auto* g : row.gs) totalMin += minWOf (*g);

        int x = r.getX();
        for (auto* g : row.gs)
        {
            const int mw = minWOf (*g);
            const int pw = mw + (totalMin > 0 ? extra * mw / totalMin : 0);
            g->bounds = { x, y, pw, panelH };
            x += pw + gap;

            auto inner = g->bounds.withTrimmedTop (titleH).withTrimmedBottom (padB).reduced (padX, 0);
            int n = 0;
            for (auto* k : g->knobs) if (k->s.isVisible()) ++n;
            const float unit = (float) inner.getWidth() / ((float) n + g->extraCells);
            for (auto* k : g->knobs)
            {
                if (! k->s.isVisible()) continue;
                auto cell = inner.removeFromLeft (juce::roundToInt (unit));
                k->l.setBounds (cell.removeFromTop (14));
                k->s.setBounds (cell);
            }
            if (g->extrasKind == 2)
            {
                auto e = inner;
                auto top = e.removeFromTop (e.getHeight() / 2).reduced (2, 3);
                arpBtn.setBounds (top.removeFromLeft (top.getWidth() / 2 - 2));
                top.removeFromLeft (4);
                latchBtn.setBounds (top);
                auto bot = e.reduced (2, 3);
                arpPatternBox.setBounds (bot.removeFromLeft (bot.getWidth() / 2 - 2));
                bot.removeFromLeft (4);
                arpRateBox.setBounds (bot);
            }
            else if (g->extrasKind == 1)
            {
                auto e = inner;
                auto top = e.removeFromTop (e.getHeight() / 2).reduced (2, 4);
                scaleBox.setBounds (top);
                auto bot = e.reduced (2, 4);
                rootBox.setBounds (bot.removeFromLeft (bot.getWidth() / 2 - 2));
                bot.removeFromLeft (4);
                hammerBtn.setBounds (bot);
            }
        }
        y += panelH + gap;
    }

    r.removeFromTop (y - r.getY());
    grid.setBounds (r);
}
