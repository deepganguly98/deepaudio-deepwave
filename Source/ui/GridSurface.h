#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <vector>
#include <map>
#include "Scales.h"

/*
    GridSurface — isomorphic multi-touch playing surface that speaks MPE.

    Layout: `rows` strings stacked bottom→top, each tuned `rowInterval`
    semitones above the one below (5 = all-fourths, like a guitar's low strings).
    `cols` cells per row, one semitone each, starting at `baseNote`.

    Per touch:
      note-on   : MPE channel allocated, note = nearest cell, velocity from
                  touch pressure if available else from Y position.
      drag X    : continuous pitch → pitch bend on that channel, with fret
                  snapping (deadzone around each cell centre) and, for
                  non-chromatic scales, a gentle continuous pull toward
                  in-scale pitches.
      drag Y    : CC74 (timbre).  Pressure (Force Touch / Pencil) → channel
                  pressure; if unavailable, Y also drives pressure.
      release   : note-off, channel freed.

    Hammer-on / pull-off: a new touch on a row that already has a held touch
    re-pitches that row's voice (legato) instead of plucking. Lifting the
    top finger bends back to the finger underneath.

    Auto vibrato: a touch resting on one pitch fades in a ~5.5 Hz bend LFO.

    Messages are posted to a MidiMessageCollector so the processor can both
    play them and pass them downstream as MIDI out.
*/
class GridSurface : public juce::Component,
                    private juce::Timer
{
public:
    struct Config
    {
        int rows = 6, cols = 13;
        int baseNote = 40;          // E2
        int rowInterval = 5;        // fourths
        float snapDeadzone = 0.30f; // 0 = pure continuous, 0.5 = hard quantise
        int bendRangeSemis = 48;    // must match the synth's MPE zone
        bool yIsPressure = true;    // use Y as pressure when no force sensor
        int scaleMask = 0xFFF;      // bit i = pitch class (rootPc + i) % 12, see Scales.h
        int rootPc = 0;             // 0..11
        float autoVibrato = 0.0f;   // 0..1 depth of the automatic vibrato
        bool hammerOn = true;       // legato re-pitch when a 2nd finger lands on a held row
    };

    explicit GridSurface (juce::MidiMessageCollector& collector);
    ~GridSurface() override;

    void setConfig (const Config& c);
    Config getConfig() const { return cfg; }

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Computer-keyboard playing: each held key is its own MPE note/channel (centre bend,
    // steady pressure), so the synth's Hold sustains it like a held finger.
    void keyNote (int midiNote, bool down);
    void releaseAllKeys();
    void parentHierarchyChanged() override;

private:
    static constexpr int kMaxRows = 8;

    // sounding voice state (one MPE channel)
    struct Voice
    {
        int channel = 0;      // 2..16, 0 = none
        int noteNumber = 0;
        float lastBend = 0.0f, lastPressure = -1.0f, lastTimbre = -1.0f;
        float baseBend = 0.0f;       // target bend without vibrato (semitones)
        float basePressure = 0.5f;   // pressure without hammer accent
        float accent = 0.0f;         // decaying pressure bump
        float vibEnv = 0.0f, vibPhase = 0.0f;
        float anchorPitch = 0.0f;    // raw cell coordinate where the finger "rests"
        double anchorMs = 0.0;       // when it started resting
    };

    struct Touch
    {
        Voice v;                     // valid only if hasVoice
        bool hasVoice = false;       // owns the voice (channel)
        bool inStack = false;        // member of the row's hammer stack
        int row = 0;
        float pitchNow = 0.0f;       // snapped continuous cell coordinate under the finger
        juce::Point<float> pos;
        std::vector<juce::Point<float>> trail;
    };

    // --- geometry helpers (cfg is sanitised in setConfig)
    float cellW() const { return (float) getWidth()  / (float) cfg.cols; }
    float cellH() const { return (float) getHeight() / (float) cfg.rows; }
    int rowAt (float y) const { return juce::jlimit (0, cfg.rows - 1, cfg.rows - 1 - (int) (y / cellH())); }
    int rowBaseNote (int row) const { return cfg.baseNote + row * cfg.rowInterval; }

    // scale helpers
    bool isChromatic() const;
    bool inScale (int note) const;
    float applyScalePull (float cellPitch, int row) const;

    // continuous pitch (in semitones relative to row base) for an X position, with snapping
    float snappedPitchAt (float x, int row) const;

    // --- MPE channel pool (2..16)
    int allocateChannel();
    void releaseChannel (int ch);
    std::array<bool, 17> channelBusy {};

    void timerCallback() override;
    void post (juce::MidiMessage m);   // stamps the time the collector needs

    // per-row visual string vibration (decays after a pluck/release)
    std::array<float, 16> vibAmp {}, vibPhase {};

    void sendBend     (Voice&, float semitones);
    void sendPressure (Voice&, float v01);
    void sendTimbre   (Voice&, float v01);
    void applyBend (Voice&);
    void applyPressure (Voice&);
    float pressureFor (const juce::MouseEvent&, int row) const;

    // --- touch / hammer-on bookkeeping
    Touch* voiceTouch (const Touch&);
    const Touch* voiceTouch (const Touch&) const;
    bool isDriver (int id, const Touch&) const;
    void retarget (int row, float accent);
    void releaseTouch (int id);
    void releaseAllTouches();
    void endVoice (Touch&);

    juce::MidiMessageCollector& midi;
    Config cfg;
    std::map<int, int> keyChannels;   // held computer-keyboard note -> MPE channel
    std::map<int, Touch> touches;   // keyed by MouseInputSource index
    std::array<std::vector<int>, kMaxRows> rowStack;  // touch ids sharing a row's voice: front = owner, back = driver
    double lastTickMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GridSurface)
};
