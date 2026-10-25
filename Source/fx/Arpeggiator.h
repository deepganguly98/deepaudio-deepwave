#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

// MPE-aware arpeggiator. Rewrites a MidiBuffer in place; allocates only in prepare().
//
// Input (enabled): note on/off on any channel are absorbed into a held-note set together with the
// per-note expression of member channels. Pitch bend, CC74 and pressure on member channels (2..16) are
// absorbed from the source key but CARRIED INTO the arp notes: new steps start at the finger's current
// bend/timbre/pressure and sounding arp notes follow it live (so sliding/gliding works with the arp on).
// CC64 is treated as latch while held. Everything
// else passes through (ch.1 bend/CC, other controllers, sysex...). Output notes are generated on
// member channels 2..16, each preceded by pitch-bend centre, channel pressure and CC74=64.
//
// Timing: a "step grid" in beats. Step k nominally starts at k * L beats (L = rate length in beats).
// Swing: every ODD step k is delayed by swing * L / 2 beats (swing 0.6 -> 0.3 of a step); even steps
// are on the grid. Gate is a fraction of the nominal step length L (not of the swung length).
// With a host ppq the grid is absolute (ppq 0 is step 0) and re-locks after any transport jump;
// with no host position a free clock runs from the first note-on (step 0 at that sample).
namespace wgfx
{

inline constexpr const char* kArpPatternNames[] = { "Up", "Down", "Up-Down", "Down-Up", "Random", "As Played", "Chord" };
inline constexpr int kNumArpPatterns = 7;
inline constexpr const char* kArpRateNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr int kNumArpRates = 6;

class Arpeggiator
{
public:
    struct Settings
    {
        bool enabled = false;
        int pattern = 0;       // index into kArpPatternNames
        int rate = 3;          // index into kArpRateNames
        int octaves = 1;       // 1..4
        float gate = 0.6f;     // 0.05..1, note length as fraction of a step
        float swing = 0.0f;    // 0..0.6
        bool latch = false;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        out.ensureSize (65536);
        for (auto& v : voices) v.active = false;
        flushPending = false;
        wasEnabled = false;
        absNow = 0;
        reset();
    }

    // Forget held notes, latch, clocks. Any arp notes still sounding get their note-offs at the
    // start of the next process() call (so nothing sticks).
    void reset()
    {
        nHeld = 0;
        sustainDown = false;
        clockRunning = false;
        lastFired = -1;
        stepCounter = 0;
        lastL = -1.0;
        lastPpqMode = false;
        haveExpected = false;
        lastIdx = -1;
        rng = 0x9E3779B9u;
        keyOrder = 0;
        chanCursor = 0;
        chPressureValid.fill (false);
        chBendValid.fill (false);
        chTimbreValid.fill (false);
        for (const auto& v : voices)
            if (v.active) flushPending = true;
    }

    // Rewrites `midi` in place (block-relative positions).
    void process (juce::MidiBuffer& midi, int numSamples, const Settings& settingsIn, double bpmIn,
                  std::optional<double> ppqStart)
    {
        if (numSamples <= 0)
            return;

        Settings s = settingsIn;
        s.pattern = juce::jlimit (0, kNumArpPatterns - 1, s.pattern);
        s.rate = juce::jlimit (0, kNumArpRates - 1, s.rate);
        s.octaves = juce::jlimit (1, 4, s.octaves);
        s.gate = std::isfinite (s.gate) ? juce::jlimit (0.05f, 1.0f, s.gate) : 0.6f;
        s.swing = std::isfinite (s.swing) ? juce::jlimit (0.0f, 0.6f, s.swing) : 0.0f;
        double bpm = bpmIn;
        if (! (std::isfinite (bpm) && bpm > 0.0)) bpm = 120.0;
        bpm = juce::jlimit (1.0, 1000.0, bpm);

        cfg = s;
        n = numSamples;
        base = absNow;
        spb = sr * 60.0 / bpm;
        curL = kRateBeats[s.rate];
        const bool usePpq = ppqStart.has_value() && std::isfinite (*ppqStart);

        out.clear();

        // ---- disable / flush -----------------------------------------------------------------
        if (flushPending || (wasEnabled && ! s.enabled))
        {
            for (auto& v : voices)
                if (v.active) { out.addEvent (juce::MidiMessage::noteOff (v.channel, v.note, (juce::uint8) 0), 0); v.active = false; }
            flushPending = false;
        }
        if (wasEnabled && ! s.enabled)
        {
            nHeld = 0; sustainDown = false; clockRunning = false;
        }

        if (! s.enabled)
        {
            // pass everything through; remember sounding notes so a later enable can't strand them
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())       passthroughOn.set (pt (m.getChannel(), m.getNoteNumber()));
                else if (m.isNoteOff()) passthroughOn.reset (pt (m.getChannel(), m.getNoteNumber()));
                out.addEvent (m, meta.samplePosition);
            }
            wasEnabled = false;
            finish (midi);
            return;
        }

        // ---- pre-pass: split input into passthrough and handled events ------------------------
        nIn = 0;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int pos = juce::jlimit (0, n - 1, meta.samplePosition);
            const int ch = m.getChannel();
            int prio = -1;

            if (m.isNoteOn())
            {
                passthroughOn.reset (pt (ch, m.getNoteNumber()));
                prio = 2;
            }
            else if (m.isNoteOff())
            {
                if (passthroughOn.test (pt (ch, m.getNoteNumber())))
                {
                    passthroughOn.reset (pt (ch, m.getNoteNumber()));
                    out.addEvent (m, pos);
                    continue;
                }
                prio = 0;
            }
            else if (m.isChannelPressure())
                prio = 1;
            else if (m.isPitchWheel() && ch >= 2)
                prio = 1;
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();
                if (cc == 64 || (cc == 74 && ch >= 2)) prio = 1;
                else if (cc == 123 || cc == 120) prio = 1;
            }

            if (prio < 0 || m.getRawDataSize() > 3 || nIn >= kMaxIn)
            {
                if (prio < 0) out.addEvent (m, pos);
                continue; // handled events beyond capacity are dropped
            }
            InEv& e = inEv[(size_t) nIn++];
            e.pos = pos; e.prio = prio; e.size = m.getRawDataSize();
            std::memcpy (e.data, m.getRawData(), (size_t) e.size);
        }
        for (int i = 1; i < nIn; ++i) // stable insertion sort by (pos, prio)
        {
            const InEv x = inEv[(size_t) i];
            int j = i - 1;
            while (j >= 0 && (inEv[(size_t) j].pos > x.pos || (inEv[(size_t) j].pos == x.pos && inEv[(size_t) j].prio > x.prio)))
            { inEv[(size_t) j + 1] = inEv[(size_t) j]; --j; }
            inEv[(size_t) j + 1] = x;
        }

        // ---- clock set-up --------------------------------------------------------------------
        ppqMode = usePpq;
        if (usePpq)
        {
            const double b0 = *ppqStart;
            const bool jump = ! lastPpqMode || ! haveExpected || std::abs (b0 - expectedPpq) > 0.01 || std::abs (curL - lastL) > 1e-12;
            anchorSample = base; anchorBeat = b0; anchorBpm = bpm;
            if (jump || ! wasEnabled) resync (b0);
            expectedPpq = b0 + (double) n / spb;
            haveExpected = true;
        }
        else
        {
            if (lastPpqMode) clockRunning = false;
            haveExpected = false;
            if (clockRunning && std::abs (bpm - anchorBpm) > 1e-9)
            {
                anchorBeat += (double) (base - anchorSample) * anchorBpm / (60.0 * sr); // old tempo
                anchorSample = base; anchorBpm = bpm;
            }
            if (clockRunning && std::abs (curL - lastL) > 1e-12)
                resync (beatAt (base));
        }
        lastL = curL;
        lastPpqMode = usePpq;
        wasEnabled = true;

        if (! latchActive()) purgeReleased();
        if (! ppqMode && ! clockRunning && nHeld > 0)
            startClock (base);

        // ---- event loop ------------------------------------------------------------------------
        int cur = 0, ip = 0;
        for (;;)
        {
            const int tIn = ip < nIn ? inEv[(size_t) ip].pos : std::numeric_limits<int>::max();
            const int tOff = nextOffRel (cur);
            const int tStep = nextStepRel (cur);
            const int t = std::min ({ tIn, tOff, tStep });
            if (t >= n) break;
            cur = t;

            releaseDue (t);
            while (ip < nIn && inEv[(size_t) ip].pos == t)
                handleInput (inEv[(size_t) ip++], t);
            if (nextStepRel (t) == t)
                fireStep (t);
        }

        absNow += n;
        finish (midi);
    }

private:
    static constexpr double kRateBeats[kNumArpRates] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    static constexpr int kMaxHeld = 16;
    static constexpr int kMaxVoices = 15;
    static constexpr int kMaxIn = 512;
    static constexpr int kMaxSeq = kMaxHeld * 4;
    static constexpr float kDefaultPressure = 0.6f;
    static constexpr float kMinPressure = 0.1f; // never hand the synth a silent note

    // bend14 / timbre are the source finger's live expression (so slides and glides carry into the arp)
    struct Held { int note = 0, ch = 0, vel = 0; float pressure = kDefaultPressure; int bend14 = 8192, timbre = 64;
                  std::int64_t order = 0; bool down = false; };
    // srcOrder = Held::order of the key this note came from (0 = none), so live expression follows the right finger
    struct Voice { bool active = false; int channel = 2, note = 0; std::int64_t start = 0, offAbs = 0, srcOrder = -1; };
    struct InEv { int pos = 0, prio = 0, size = 0; juce::uint8 data[3] {}; };
    struct SeqNote { int note = 0, vel = 0; float pressure = 0; int bend14 = 8192, timbre = 64; std::int64_t order = -1; };

    static size_t pt (int ch, int note) noexcept { return (size_t) (((ch - 1) & 15) * 128 + (note & 127)); }

    bool latchActive() const noexcept { return cfg.latch || sustainDown; }

    void finish (juce::MidiBuffer& midi)
    {
        midi.clear();
        midi.addEvents (out, 0, std::numeric_limits<int>::max(), 0);
        if (! cfg.enabled) absNow += n;
    }

    // ---- clock ------------------------------------------------------------------------------
    double stepTime (std::int64_t k) const noexcept
    {
        return (double) k * curL + ((k & 1) ? (double) cfg.swing * curL * 0.5 : 0.0);
    }
    double beatAt (std::int64_t abs) const noexcept { return anchorBeat + (double) (abs - anchorSample) / spb; }

    void resync (double beatNow) noexcept
    {
        std::int64_t k = (std::int64_t) std::floor (beatNow / curL) - 1;
        while (stepTime (k) < beatNow - 1e-6) ++k;
        lastFired = k - 1;
    }

    void startClock (std::int64_t abs) noexcept
    {
        anchorSample = abs; anchorBeat = 0.0; anchorBpm = sr * 60.0 / spb;
        lastFired = -1; stepCounter = 0; clockRunning = true;
    }

    int nextStepRel (int cur) const noexcept
    {
        if (nHeld == 0 || (! ppqMode && ! clockRunning)) return std::numeric_limits<int>::max();
        const double x = (stepTime (lastFired + 1) - anchorBeat) * spb - 1e-6;
        const double rel = (double) (anchorSample - base) + std::ceil (x);
        if (rel >= (double) n) return std::numeric_limits<int>::max();
        return std::max (cur, (int) std::max (rel, 0.0));
    }

    int nextOffRel (int cur) const noexcept
    {
        int best = std::numeric_limits<int>::max();
        for (const auto& v : voices)
            if (v.active)
                best = std::min (best, (int) std::max<std::int64_t> (v.offAbs - base, cur));
        return best;
    }

    void releaseDue (int t)
    {
        for (auto& v : voices)
            if (v.active && v.offAbs - base <= t)
            {
                out.addEvent (juce::MidiMessage::noteOff (v.channel, v.note, (juce::uint8) 0), t);
                v.active = false;
            }
    }

    void silenceAll (int t)
    {
        for (auto& v : voices)
            if (v.active) { out.addEvent (juce::MidiMessage::noteOff (v.channel, v.note, (juce::uint8) 0), t); v.active = false; }
    }

    // ---- held notes ---------------------------------------------------------------------------
    void purgeReleased() noexcept
    {
        int w = 0;
        for (int i = 0; i < nHeld; ++i)
            if (held[(size_t) i].down) held[(size_t) w++] = held[(size_t) i];
        nHeld = w;
    }

    void transition (int prevN, int t)
    {
        if (prevN == 0 && nHeld > 0)
        {
            stepCounter = 0;
            if (ppqMode) resync (beatAt (base + t));
            else if (! clockRunning) startClock (base + t);
        }
        else if (prevN > 0 && nHeld == 0 && ! ppqMode)
            clockRunning = false;
    }

    void handleInput (const InEv& e, int t)
    {
        const juce::MidiMessage m (e.data, e.size, 0.0);
        const int prevN = nHeld;
        const int ch = m.getChannel();

        if (m.isNoteOn())
        {
            bool anyDown = false;
            for (int i = 0; i < nHeld; ++i) anyDown = anyDown || held[(size_t) i].down;
            if (latchActive() && nHeld > 0 && ! anyDown)
            {
                nHeld = 0;      // new phrase replaces the latched set
                stepCounter = 0;
            }
            int idx = -1;
            for (int i = 0; i < nHeld; ++i)
                if (held[(size_t) i].ch == ch && held[(size_t) i].note == m.getNoteNumber()) idx = i;
            if (idx < 0)
            {
                if (nHeld == kMaxHeld) // full: drop the oldest key-down
                {
                    int o = 0;
                    for (int i = 1; i < nHeld; ++i) if (held[(size_t) i].order < held[(size_t) o].order) o = i;
                    for (int i = o; i < nHeld - 1; ++i) held[(size_t) i] = held[(size_t) i + 1];
                    --nHeld;
                }
                idx = nHeld++;
            }
            Held& h = held[(size_t) idx];
            h.note = m.getNoteNumber(); h.ch = ch; h.vel = std::max (1, (int) m.getVelocity());
            h.pressure = chPressureValid[(size_t) ch] ? chPressure[(size_t) ch] : kDefaultPressure;
            h.bend14 = chBendValid[(size_t) ch] ? chBend[(size_t) ch] : 8192;
            h.timbre = chTimbreValid[(size_t) ch] ? chTimbre[(size_t) ch] : 64;
            h.order = keyOrder++; h.down = true;
        }
        else if (m.isNoteOff())
        {
            chPressureValid[(size_t) ch] = false;
            chBendValid[(size_t) ch] = false;
            chTimbreValid[(size_t) ch] = false;
            for (int i = 0; i < nHeld; ++i)
                if (held[(size_t) i].ch == ch && held[(size_t) i].note == m.getNoteNumber())
                {
                    held[(size_t) i].down = false;
                    if (! latchActive())
                    {
                        for (int j = i; j < nHeld - 1; ++j) held[(size_t) j] = held[(size_t) j + 1];
                        --nHeld;
                    }
                    break;
                }
        }
        else if (m.isChannelPressure())
        {
            const float p = (float) m.getChannelPressureValue() / 127.0f;
            chPressure[(size_t) ch] = p; chPressureValid[(size_t) ch] = true;
            const int pv = juce::jlimit (0, 127, (int) std::lround (juce::jmax (kMinPressure, p) * 127.0f));
            for (int i = 0; i < nHeld; ++i)
                if (held[(size_t) i].ch == ch && held[(size_t) i].down)
                {
                    held[(size_t) i].pressure = p;
                    for (const auto& v : voices)
                        if (v.active && v.srcOrder == held[(size_t) i].order)
                            out.addEvent (juce::MidiMessage::channelPressureChange (v.channel, pv), t);
                }
        }
        else if (m.isPitchWheel() && ch >= 2)
        {
            // slide / glide / vibrato on the finger: remember it and move every sounding arp note from that key
            chBend[(size_t) ch] = m.getPitchWheelValue(); chBendValid[(size_t) ch] = true;
            for (int i = 0; i < nHeld; ++i)
                if (held[(size_t) i].ch == ch && held[(size_t) i].down)
                {
                    held[(size_t) i].bend14 = chBend[(size_t) ch];
                    for (const auto& v : voices)
                        if (v.active && v.srcOrder == held[(size_t) i].order)
                            out.addEvent (juce::MidiMessage::pitchWheel (v.channel, chBend[(size_t) ch]), t);
                }
        }
        else if (m.isController() && m.getControllerNumber() == 74 && ch >= 2)
        {
            chTimbre[(size_t) ch] = m.getControllerValue(); chTimbreValid[(size_t) ch] = true;
            for (int i = 0; i < nHeld; ++i)
                if (held[(size_t) i].ch == ch && held[(size_t) i].down)
                {
                    held[(size_t) i].timbre = chTimbre[(size_t) ch];
                    for (const auto& v : voices)
                        if (v.active && v.srcOrder == held[(size_t) i].order)
                            out.addEvent (juce::MidiMessage::controllerEvent (v.channel, 74, chTimbre[(size_t) ch]), t);
                }
        }
        else if (m.isController() && m.getControllerNumber() == 64)
        {
            sustainDown = m.getControllerValue() >= 64;
            if (! latchActive()) purgeReleased();
        }
        else if (m.isController()) // 120 / 123: all sound off / all notes off
        {
            nHeld = 0;
            silenceAll (t);
            sustainDown = false;
            out.addEvent (m, t);
        }

        transition (prevN, t);
    }

    // ---- sequence -----------------------------------------------------------------------------
    int buildBase (Held* b) const noexcept
    {
        for (int i = 0; i < nHeld; ++i) b[i] = held[(size_t) i];
        const bool byOrder = cfg.pattern == 5;
        for (int i = 1; i < nHeld; ++i) // insertion sort
        {
            const Held x = b[i];
            int j = i - 1;
            while (j >= 0 && (byOrder ? b[j].order > x.order : b[j].note > x.note)) { b[j + 1] = b[j]; --j; }
            b[j + 1] = x;
        }
        return nHeld;
    }

    juce::uint32 nextRand() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    }

    void fireStep (int t)
    {
        const std::int64_t k = lastFired + 1;
        lastFired = k;
        const std::int64_t counter = stepCounter++;
        if (nHeld == 0) return;

        Held b[kMaxHeld];
        const int nb = buildBase (b);
        const std::int64_t abs = base + t;
        const auto len = (std::int64_t) std::llround (cfg.gate * curL * spb);
        const std::int64_t offAbs = abs + std::max<std::int64_t> (1, len);

        if (cfg.pattern == 6) // chord: all notes together, stepping through the octave stack
        {
            const int o = (int) (counter % cfg.octaves);
            int started = 0;
            for (int i = 0; i < nb && started < kMaxVoices; ++i)
            {
                const int note = b[i].note + 12 * o;
                if (note > 127) continue;
                startVoice (t, note, b[i].vel, b[i].pressure, b[i].bend14, b[i].timbre, b[i].order, abs, offAbs);
                ++started;
            }
            return;
        }

        SeqNote seq[kMaxSeq];
        int ns = 0;
        for (int o = 0; o < cfg.octaves; ++o)
            for (int i = 0; i < nb; ++i)
            {
                const int note = b[i].note + 12 * o;
                if (note <= 127) seq[ns++] = { note, b[i].vel, b[i].pressure, b[i].bend14, b[i].timbre, b[i].order };
            }
        if (ns == 0) return;

        int idx = 0;
        const int pat = cfg.pattern;
        const int period = ns <= 1 ? 1 : 2 * ns - 2;
        const int p = (int) (counter % period);
        const int tri = p < ns ? p : period - p;
        switch (pat)
        {
            case 0: case 5: idx = (int) (counter % ns); break;
            case 1:         idx = ns - 1 - (int) (counter % ns); break;
            case 2:         idx = tri; break;
            case 3:         idx = ns - 1 - tri; break;
            case 4:
                if (ns == 1) idx = 0;
                else if (lastIdx >= 0 && lastIdx < ns)
                {
                    const int r = (int) (nextRand() % (juce::uint32) (ns - 1));
                    idx = r >= lastIdx ? r + 1 : r;
                }
                else idx = (int) (nextRand() % (juce::uint32) ns);
                lastIdx = idx;
                break;
            default: break;
        }
        startVoice (t, seq[idx].note, seq[idx].vel, seq[idx].pressure, seq[idx].bend14, seq[idx].timbre, seq[idx].order, abs, offAbs);
    }

    void startVoice (int t, int note, int vel, float pressure, int bend14, int timbre, std::int64_t srcOrder,
                     std::int64_t abs, std::int64_t offAbs)
    {
        int idx = -1;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const int j = (chanCursor + i) % kMaxVoices;
            if (! voices[(size_t) j].active) { idx = j; break; }
        }
        if (idx < 0) // all 15 busy: steal the oldest
        {
            idx = 0;
            for (int j = 1; j < kMaxVoices; ++j)
                if (voices[(size_t) j].start < voices[(size_t) idx].start) idx = j;
            out.addEvent (juce::MidiMessage::noteOff (voices[(size_t) idx].channel, voices[(size_t) idx].note, (juce::uint8) 0), t);
        }
        chanCursor = (idx + 1) % kMaxVoices;
        const int ch = idx + 2;
        voices[(size_t) idx] = { true, ch, note, abs, offAbs, srcOrder };

        const int pv = juce::jlimit (0, 127, (int) std::lround (juce::jmax (kMinPressure, pressure) * 127.0f));
        out.addEvent (juce::MidiMessage::pitchWheel (ch, juce::jlimit (0, 16383, bend14)), t);   // the finger's current bend
        out.addEvent (juce::MidiMessage::channelPressureChange (ch, pv), t);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 74, juce::jlimit (0, 127, timbre)), t);
        out.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) juce::jlimit (1, 127, vel)), t);
    }

    // ---- state ----------------------------------------------------------------------------------
    double sr = 44100.0;
    juce::MidiBuffer out;
    Settings cfg;
    int n = 0;
    std::int64_t base = 0, absNow = 0;
    double spb = 24000.0, curL = 0.25;

    std::array<Held, kMaxHeld> held {};
    int nHeld = 0;
    std::int64_t keyOrder = 0;
    bool sustainDown = false;
    std::array<float, 17> chPressure {};
    std::array<bool, 17> chPressureValid {};
    std::array<int, 17> chBend {}, chTimbre {};
    std::array<bool, 17> chBendValid {}, chTimbreValid {};
    std::bitset<2048> passthroughOn;

    std::array<InEv, kMaxIn> inEv {};
    int nIn = 0;

    std::array<Voice, kMaxVoices> voices {};
    int chanCursor = 0;

    bool wasEnabled = false, flushPending = false;
    bool ppqMode = false, lastPpqMode = false, haveExpected = false, clockRunning = false;
    double expectedPpq = 0.0, anchorBeat = 0.0, anchorBpm = 120.0, lastL = -1.0;
    std::int64_t anchorSample = 0, lastFired = -1, stepCounter = 0;
    int lastIdx = -1;
    juce::uint32 rng = 0x9E3779B9u;
};

} // namespace wgfx
