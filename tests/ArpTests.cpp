#include <juce_audio_basics/juce_audio_basics.h>
#include "fx/Arpeggiator.h"
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace
{
using Settings = wgfx::Arpeggiator::Settings;

struct In { std::int64_t t; juce::MidiMessage m; };
struct Out
{
    std::int64_t t; std::vector<juce::uint8> b;
    bool operator== (const Out& o) const { return t == o.t && b == o.b; }
    juce::MidiMessage msg() const { return juce::MidiMessage (b.data(), (int) b.size(), 0.0); }
};

struct RunCfg
{
    Settings s;
    double bpm = 120.0;
    bool ppq = false;
    double ppqBase = 0.0;
    std::function<int()> blockSize = [] { return 1 << 20; };
    std::function<Settings (std::int64_t)> settingsAt;
};

constexpr double kSr = 48000.0;

std::vector<Out> run (wgfx::Arpeggiator& arp, const std::vector<In>& in, std::int64_t total, RunCfg c)
{
    std::vector<Out> res;
    std::int64_t pos = 0;
    const double spb = kSr * 60.0 / c.bpm;
    while (pos < total)
    {
        const int n = (int) std::min<std::int64_t> (std::max (1, c.blockSize()), total - pos);
        juce::MidiBuffer mb;
        for (const auto& e : in)
            if (e.t >= pos && e.t < pos + n) mb.addEvent (e.m, (int) (e.t - pos));
        const Settings s = c.settingsAt ? c.settingsAt (pos) : c.s;
        arp.process (mb, n, s, c.bpm, c.ppq ? std::optional<double> (c.ppqBase + (double) pos / spb) : std::nullopt);
        for (const auto meta : mb)
        {
            const auto m = meta.getMessage();
            res.push_back ({ pos + meta.samplePosition, std::vector<juce::uint8> (m.getRawData(), m.getRawData() + m.getRawDataSize()) });
        }
        pos += n;
    }
    return res;
}

std::vector<Out> noteOns (const std::vector<Out>& v)
{
    std::vector<Out> r;
    for (const auto& o : v) if (o.msg().isNoteOn()) r.push_back (o);
    return r;
}
std::vector<int> noteNums (const std::vector<Out>& v)
{
    std::vector<int> r;
    for (const auto& o : noteOns (v)) r.push_back (o.msg().getNoteNumber());
    return r;
}
In on (std::int64_t t, int ch, int note, int vel = 100) { return { t, juce::MidiMessage::noteOn (ch, note, (juce::uint8) vel) }; }
In off (std::int64_t t, int ch, int note) { return { t, juce::MidiMessage::noteOff (ch, note, (juce::uint8) 0) }; }

Settings en (int pattern = 0, int rate = 3)
{
    Settings s; s.enabled = true; s.pattern = pattern; s.rate = rate; return s;
}
std::vector<In> triad() { return { on (0, 2, 60), on (0, 3, 64), on (0, 4, 67) }; }
}

struct ArpTests : public juce::UnitTest
{
    ArpTests() : juce::UnitTest ("Arpeggiator", "WaveGrid") {}

    static std::vector<int> head (std::vector<int> v, size_t k) { if (v.size() > k) v.resize (k); return v; }

    void runTest() override
    {
        wgfx::Arpeggiator arp;

        beginTest ("pattern order");
        {
            const std::vector<int> up { 60, 64, 67, 60, 64, 67, 60, 64 };
            const std::vector<int> down { 67, 64, 60, 67, 64, 60, 67, 64 };
            const std::vector<int> ud { 60, 64, 67, 64, 60, 64, 67, 64 };
            const std::vector<int> du { 67, 64, 60, 64, 67, 64, 60, 64 };
            const std::vector<int> ap { 67, 60, 64, 67, 60, 64, 67, 60 };
            const std::vector<std::vector<int>> exp { up, down, ud, du };
            for (int p = 0; p < 4; ++p)
            {
                arp.prepare (kSr);
                RunCfg c; c.s = en (p);
                auto o = run (arp, triad(), 6000 * 8, c);
                expect (head (noteNums (o), 8) == exp[(size_t) p], juce::String ("pattern ") + wgfx::kArpPatternNames[p]);
            }
            arp.prepare (kSr);
            RunCfg c; c.s = en (5);
            auto o = run (arp, { on (0, 2, 67), on (0, 3, 60), on (0, 4, 64) }, 6000 * 8, c);
            expect (head (noteNums (o), 8) == ap, "as played");

            arp.prepare (kSr);
            c.s = en (4);
            auto r = noteNums (run (arp, triad(), 6000 * 60, c));
            bool rep = false, bad = false;
            for (size_t i = 0; i < r.size(); ++i)
            {
                bad = bad || (r[i] != 60 && r[i] != 64 && r[i] != 67);
                if (i > 0 && r[i] == r[i - 1]) rep = true;
            }
            expect (r.size() >= 55 && ! rep && ! bad, "random: no immediate repeat, only held notes");

            arp.prepare (kSr);
            c.s = en (6);
            auto ch = noteOns (run (arp, triad(), 6000 * 3, c));
            expectEquals ((int) ch.size(), 9);
            expect (ch[0].t == 0 && ch[1].t == 0 && ch[2].t == 0 && ch[3].t == 6000, "chord: simultaneous");

            arp.prepare (kSr);
            c.s = en (0); c.s.octaves = 2;
            expect (head (noteNums (run (arp, triad(), 6000 * 8, c)), 8) == std::vector<int> ({ 60, 64, 67, 72, 76, 79, 60, 64 }), "octaves stack");

            arp.prepare (kSr);
            c.s = en (2); c.s.octaves = 2;
            expect (head (noteNums (run (arp, triad(), 6000 * 12, c)), 12) == std::vector<int> ({ 60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60, 64 }), "up-down w/ octaves, ends not doubled");

            // upper clipping: 120 + 12 > 127 is dropped
            arp.prepare (kSr);
            c.s = en (0); c.s.octaves = 4;
            auto cl = noteNums (run (arp, { on (0, 2, 120) }, 6000 * 6, c));
            expect (! cl.empty() && cl[0] == 120 && cl[1] == 120, "notes above 127 skipped");
        }

        beginTest ("rate timing at 120 BPM / 48k");
        {
            const int expectSamples[] = { 24000, 12000, 8000, 6000, 4000, 3000 };
            for (int r = 0; r < wgfx::kNumArpRates; ++r)
            {
                arp.prepare (kSr);
                RunCfg c; c.s = en (0, r);
                auto o = noteOns (run (arp, { on (0, 2, 60) }, 48000 * 2, c));
                expect (o.size() >= 4, "enough notes");
                bool ok = o.size() >= 4;
                for (size_t i = 0; ok && i < o.size(); ++i)
                    ok = o[i].t == (std::int64_t) (i * (size_t) expectSamples[r]);
                expect (ok, juce::String ("rate ") + wgfx::kArpRateNames[r]);
            }
        }

        beginTest ("gate length and offs landing in later blocks");
        {
            for (float g : { 0.5f, 0.6f, 1.0f })
            {
                arp.prepare (kSr);
                RunCfg c; c.s = en(); c.s.gate = g; c.blockSize = [] { return 100; };
                auto o = run (arp, { on (0, 2, 60) }, 24000, c);
                std::int64_t onT = -1, offT = -1;
                for (auto& e : o) { if (e.msg().isNoteOn() && onT < 0) onT = e.t; if (e.msg().isNoteOff() && offT < 0) offT = e.t; }
                expectEquals ((int) (offT - onT), (int) std::lround (g * 6000));
            }
        }

        beginTest ("held-note release stops arp, pending off still fires");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en();
            auto o = run (arp, { on (0, 2, 60), off (1000, 2, 60) }, 30000, c);
            expectEquals ((int) noteOns (o).size(), 1);
            int offs = 0; for (auto& e : o) offs += e.msg().isNoteOff();
            expectEquals (offs, 1);
        }

        beginTest ("no stuck notes after disable / reset");
        {
            arp.prepare (kSr);
            RunCfg c;
            c.settingsAt = [] (std::int64_t t) { auto s = en (0, 5); s.gate = 1.0f; s.enabled = t < 10000; return s; };
            c.blockSize = [] { return 64; };
            auto o = run (arp, triad(), 20000, c);
            std::multiset<std::pair<int, int>> sounding;
            for (auto& e : o)
            {
                auto m = e.msg();
                if (m.isNoteOn()) sounding.insert ({ m.getChannel(), m.getNoteNumber() });
                else if (m.isNoteOff()) { auto it = sounding.find ({ m.getChannel(), m.getNoteNumber() }); if (it != sounding.end()) sounding.erase (it); }
            }
            expect (sounding.empty(), "all arp notes released after disable");

            // reset(): offs go out at the next block
            arp.prepare (kSr);
            RunCfg c2; c2.s = en(); c2.s.gate = 1.0f;
            auto a = run (arp, { on (0, 2, 60) }, 3000, c2);
            arp.reset();
            c2.s.enabled = true;
            auto b = run (arp, {}, 100, c2);
            expect (noteOns (a).size() == 1 && ! b.empty() && b[0].msg().isNoteOff() && b[0].t == 0, "reset flushes");
        }

        beginTest ("disabled passes through untouched; passthrough notes not stranded on enable");
        {
            arp.prepare (kSr);
            RunCfg c; c.s.enabled = false;
            std::vector<In> in { on (10, 2, 60), off (500, 2, 60) };
            auto o = run (arp, in, 1000, c);
            expectEquals ((int) o.size(), 2);
            expect (o[0].t == 10 && o[0].msg().isNoteOn() && o[1].t == 500 && o[1].msg().isNoteOff(), "untouched");

            arp.prepare (kSr);
            c.settingsAt = [] (std::int64_t t) { return t < 1000 ? Settings() : en(); };
            c.blockSize = [] { return 1000; };
            auto o2 = run (arp, { on (10, 2, 60), off (1500, 2, 60) }, 3000, c);
            int offs = 0; for (auto& e : o2) offs += e.msg().isNoteOff() && e.t == 1500;
            expectEquals (offs, 1);
        }

        beginTest ("latch");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en(); c.s.latch = true;
            auto o = run (arp, { on (0, 2, 60), on (0, 3, 64), off (1000, 2, 60), off (1000, 3, 64),
                                 on (20000, 2, 72) }, 6000 * 8, c);
            auto nn = noteNums (o);
            expect (head (nn, 5) == std::vector<int> ({ 60, 64, 60, 64, 72 }), "keeps cycling after release");
            auto ons = noteOns (o);
            bool replaced = false;
            for (auto& e : ons) if (e.t >= 20000) replaced = (e.msg().getNoteNumber() == 72);
            expect (replaced && nn.back() == 72, "new press after full release replaces set");
            // sustain pedal as latch
            arp.prepare (kSr);
            RunCfg c2; c2.s = en();
            auto o2 = run (arp, { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) }, on (0, 2, 60), off (1000, 2, 60),
                                  { 20000, juce::MidiMessage::controllerEvent (1, 64, 0) } }, 6000 * 8, c2);
            int cnt = 0; for (auto& e : noteOns (o2)) cnt += e.t < 20000;
            expectEquals (cnt, 4);
            expectEquals ((int) noteOns (o2).size(), 4);
            for (auto& e : o2) expect (! (e.msg().isController() && e.msg().getControllerNumber() == 64), "pedal absorbed");
        }

        beginTest ("MPE ordering; source expression is carried into the arp notes");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en();
            std::vector<In> in {
                { 0, juce::MidiMessage::pitchWheel (2, 12000) },
                { 0, juce::MidiMessage::channelPressureChange (2, 100) },
                { 0, juce::MidiMessage::controllerEvent (2, 74, 99) },
                on (0, 2, 60, 90),
                { 100, juce::MidiMessage::pitchWheel (1, 9000) } };
            auto o = run (arp, in, 20000, c);
            int ons = 0;
            for (size_t i = 0; i < o.size(); ++i)
            {
                auto m = o[i].msg();
                if (! m.isNoteOn()) continue;
                ++ons;
                expect (i >= 3 && o[i - 3].msg().isPitchWheel() && o[i - 3].msg().getPitchWheelValue() == 12000
                        && o[i - 2].msg().isChannelPressure() && o[i - 1].msg().isController()
                        && o[i - 1].msg().getControllerNumber() == 74 && o[i - 1].msg().getControllerValue() == 99
                        && o[i - 1].msg().getChannel() == m.getChannel() && o[i - 3].t == o[i].t, "bend, pressure, cc74, then note-on");
                expectEquals (o[i - 2].msg().getChannelPressureValue(), (int) std::lround (100.0 / 127.0 * 127.0));
                expectEquals ((int) m.getVelocity(), 90);
            }
            expect (ons >= 3, "notes produced");
            int bends1 = 0;
            for (auto& e : o) if (e.msg().isPitchWheel() && e.msg().getPitchWheelValue() == 9000) ++bends1;
            expectEquals (bends1, 1); // ch1 bend passes through
            int carried = 0;
            for (auto& e : o) if (e.msg().isPitchWheel() && e.msg().getPitchWheelValue() == 12000) ++carried;
            expectEquals (carried, ons);   // member bend is not passed on as-is: exactly one carried bend per arp note
        }

        beginTest ("sliding/gliding a held key moves the sounding arp note and later steps");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en (0, 3);                 // up, 1/16 at 120 BPM = 6000 samples/step, gate .6 => 3600 on
            std::vector<In> in {
                { 0, juce::MidiMessage::pitchWheel (2, 8192) },
                on (0, 2, 60, 90),
                { 13000, juce::MidiMessage::pitchWheel (2, 10000) },   // slide while the step starting at 12000 sounds
                { 13500, juce::MidiMessage::pitchWheel (2, 11000) },
                { 20000, juce::MidiMessage::pitchWheel (2, 12000) } }; // between notes (step at 18000 ended at 21600? no: sounding) -> still moves it
            auto o = run (arp, in, 40000, c);

            int voiceCh = -1;
            for (auto& e : o) if (e.msg().isNoteOn() && e.t == 12000) voiceCh = e.msg().getChannel();
            expect (voiceCh >= 2, "step at 12000 sounds");
            bool b10000 = false, b11000 = false;
            for (auto& e : o)
                if (e.msg().isPitchWheel() && e.msg().getChannel() == voiceCh)
                {
                    if (e.t == 13000 && e.msg().getPitchWheelValue() == 10000) b10000 = true;
                    if (e.t == 13500 && e.msg().getPitchWheelValue() == 11000) b11000 = true;
                }
            expect (b10000 && b11000, "bend forwarded live to the sounding arp note");

            // the next step (24000) starts at the finger's latest bend (12000), before its note-on
            for (size_t i = 0; i < o.size(); ++i)
                if (o[i].msg().isNoteOn() && o[i].t == 24000)
                {
                    bool ok = false;
                    for (size_t j = 0; j < i; ++j)
                        if (o[j].t == 24000 && o[j].msg().isPitchWheel() && o[j].msg().getChannel() == o[i].msg().getChannel()
                            && o[j].msg().getPitchWheelValue() == 12000) ok = true;
                    expect (ok, "new step starts at the current bend");
                }
        }

        beginTest ("pressure and timbre also follow the finger while an arp note sounds");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en (0, 3);
            std::vector<In> in {
                on (0, 2, 60, 90),
                { 1000, juce::MidiMessage::channelPressureChange (2, 120) },
                { 1500, juce::MidiMessage::controllerEvent (2, 74, 10) } };
            auto o = run (arp, in, 4000, c);
            int voiceCh = -1;
            for (auto& e : o) if (e.msg().isNoteOn() && e.t == 0) voiceCh = e.msg().getChannel();
            bool p = false, t74 = false;
            for (auto& e : o)
            {
                if (e.t == 1000 && e.msg().isChannelPressure() && e.msg().getChannel() == voiceCh && e.msg().getChannelPressureValue() == 120) p = true;
                if (e.t == 1500 && e.msg().isController() && e.msg().getControllerNumber() == 74 && e.msg().getChannel() == voiceCh
                    && e.msg().getControllerValue() == 10) t74 = true;
            }
            expect (p && t74, "pressure and CC74 forwarded to the sounding arp note");
        }

        beginTest ("block-boundary independence");
        {
            std::vector<In> in { on (100, 2, 60), on (700, 3, 67), on (5000, 4, 64), off (9000, 2, 60), off (9500, 3, 67),
                                 off (9700, 4, 64), on (30000, 5, 62) };
            for (int pat = 0; pat < kNP; ++pat)
                for (int mode = 0; mode < 2; ++mode)
                {
                    RunCfg c; c.s = en (pat, 4); c.s.swing = 0.3f; c.s.octaves = 2; c.s.latch = (pat % 2 == 0);
                    c.ppq = mode == 1; c.ppqBase = 3.07;
                    arp.prepare (kSr);
                    auto big = run (arp, in, 60000, c);
                    juce::Random rnd (pat * 7 + mode);
                    c.blockSize = [&rnd] { return 1 + rnd.nextInt (900); };
                    arp.prepare (kSr);
                    auto many = run (arp, in, 60000, c);
                    c.blockSize = [] { return 1; };
                    arp.prepare (kSr);
                    auto tiny = run (arp, in, 20000, c);
                    arp.prepare (kSr);
                    c.blockSize = [] { return 1 << 20; };
                    auto big20 = run (arp, in, 20000, c);
                    expect (big.size() > 10, "events produced");
                    if (! (big == many)) dumpDiff (big, many);
                    if (! (big20 == tiny)) dumpDiff (big20, tiny);
                    expect (big == many, "big vs random blocks, pattern " + juce::String (pat) + " mode " + juce::String (mode));
                    expect (big20 == tiny, "1-sample blocks, pattern " + juce::String (pat) + " mode " + juce::String (mode));
                }
        }

        beginTest ("ppq lock and transport jump");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en(); c.ppq = true; c.ppqBase = 0.1;
            auto o = noteOns (run (arp, { on (0, 2, 60) }, 20000, c));
            // grid steps at 0.25, 0.5 beat: (0.25-0.1)*24000 = 3600, then +6000
            expect (o.size() >= 2 && o[0].t == 3600 && o[1].t == 9600, "locked to ppq grid");

            arp.prepare (kSr);
            RunCfg c1; c1.s = en(); c1.ppq = true; c1.ppqBase = 0.0;
            juce::MidiBuffer mb; mb.addEvent (on (0, 2, 60).m, 0);
            arp.process (mb, 10000, c1.s, 120.0, 0.0);
            juce::MidiBuffer mb2;
            arp.process (mb2, 10000, c1.s, 120.0, 5.1); // jump forward
            int firstOn = -1;
            for (const auto meta : mb2) if (meta.getMessage().isNoteOn()) { firstOn = meta.samplePosition; break; }
            expectEquals (firstOn, 3600);
            juce::MidiBuffer mb3;
            arp.process (mb3, 10000, c1.s, 120.0, 1.0); // jump backward, exactly on a step
            firstOn = -1;
            for (const auto meta : mb3) if (meta.getMessage().isNoteOn()) { firstOn = meta.samplePosition; break; }
            expectEquals (firstOn, 0);
        }

        beginTest ("swing");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en(); c.s.swing = 0.5f;
            auto o = noteOns (run (arp, { on (0, 2, 60) }, 30000, c));
            expect (o.size() >= 5 && o[0].t == 0 && o[1].t == 7500 && o[2].t == 12000 && o[3].t == 19500 && o[4].t == 24000, "odd steps delayed by swing*step/2");
        }

        beginTest ("channel allocation");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en (6, 5); c.s.octaves = 4; c.s.gate = 1.0f;
            c.blockSize = [] { return 137; };
            std::vector<In> in; for (int i = 0; i < 16; ++i) in.push_back (on (0, 2 + (i % 15), 40 + i));
            auto o = run (arp, in, 48000, c);
            std::map<int, int> active; int peak = 0; bool ok = true;
            for (auto& e : o)
            {
                auto m = e.msg();
                if (m.isNoteOn())
                {
                    ok = ok && m.getChannel() >= 2 && m.getChannel() <= 16 && active.find (m.getChannel()) == active.end();
                    active[m.getChannel()] = m.getNoteNumber();
                    peak = std::max (peak, (int) active.size());
                }
                else if (m.isNoteOff()) active.erase (m.getChannel());
            }
            expect (ok, "no channel double-booked; channels 2..16");
            expect (peak <= 15 && peak > 1, "peak voices " + juce::String (peak));
        }

        beginTest ("robustness: bad bpm, tempo change, all-notes-off");
        {
            arp.prepare (kSr);
            RunCfg c; c.s = en(); c.bpm = std::nan ("");
            expect (! noteOns (run (arp, { on (0, 2, 60) }, 30000, c)).empty(), "NaN bpm falls back");
            arp.prepare (kSr);
            c.bpm = 0.0;
            expect (! noteOns (run (arp, { on (0, 2, 60) }, 30000, c)).empty(), "zero bpm falls back");
            arp.prepare (kSr);
            juce::MidiBuffer mb; mb.addEvent (on (0, 2, 60).m, 0);
            arp.process (mb, 6001, en(), 120.0, std::nullopt);
            juce::MidiBuffer mb2;
            arp.process (mb2, 12000, en(), 240.0, std::nullopt);
            std::vector<int> ts; for (const auto meta : mb2) if (meta.getMessage().isNoteOn()) ts.push_back (meta.samplePosition);
            expect (ts.size() == 3 && ts[0] == 3000 && ts[1] == 6000, "tempo change re-anchors, steps at 3000-sample spacing");
            juce::MidiBuffer mb3; mb3.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            auto s = en(); s.gate = 1.0f;
            arp.process (mb3, 500, s, 120.0, std::nullopt);
            bool anyOff = false, anyOn = false;
            for (const auto meta : mb3) { anyOff = anyOff || meta.getMessage().isNoteOff(); anyOn = anyOn || meta.getMessage().isNoteOn(); }
            expect (! anyOn, "no arp after all-notes-off");
            juce::MidiBuffer mb4;
            arp.process (mb4, 6000, s, 120.0, std::nullopt);
            expect (mb4.isEmpty(), "silent");
            (void) anyOff;
        }
    }

    static constexpr int kNP = 7;
    void dumpDiff (const std::vector<Out>& a, const std::vector<Out>& b)
    {
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            if (! (a[i] == b[i])) { logMessage ("diff at " + juce::String ((int) i) + ": " + a[i].msg().getDescription() + "@" + juce::String ((int) a[i].t) + " vs " + b[i].msg().getDescription() + "@" + juce::String ((int) b[i].t) + " sizes " + juce::String ((int) a.size()) + "/" + juce::String ((int) b.size())); return; }
        logMessage ("size diff " + juce::String ((int) a.size()) + "/" + juce::String ((int) b.size()));
    }
};

static ArpTests arpTests;
