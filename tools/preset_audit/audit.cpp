// Offline loudness / sustain audit for the factory preset bank.
// Usage: preset_audit [--md RESULTS.md] [--apply Presets.h]
// Renders each preset through the real StringVoice (via MPESynthesiser, like the plugin)
// for MIDI 40 / 60 / 84 and a C major triad, 4 s @ 48 kHz, velocity 0.8, pressure 0.6.
#include <juce_audio_basics/juce_audio_basics.h>
#include "Presets.h"
#include "dsp/StringVoice.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <regex>

namespace
{
constexpr double kFs = 48000.0;
constexpr int kSecs = 4;
constexpr float kTargetDb = -23.0f, kPeakCeilDb = -4.0f, kTrimMin = -12.0f, kTrimMax = 6.0f;

struct Result { float rmsDb, rmsAttack, peakDb, decay, rmsLate, rmsEarly; };

std::vector<float> render (const VoicePreset& p, const std::vector<int>& notes)
{
    StringParams sp;
    sp.brightness = p.bright; sp.sustain = p.sustain; sp.inharmonic = p.stiff; sp.pickPos = p.pick;
    sp.drive = p.drive; sp.glideMs = p.glide; sp.bow = p.bow; sp.hold = p.hold; sp.attack = p.attack;
    sp.strike = p.strike; sp.breath = p.breath; sp.detune = p.width; sp.body = p.body; sp.sympathy = p.sympathy;

    juce::MPEZoneLayout layout;
    layout.setLowerZone (15, 48, 2);
    juce::MPESynthesiser synth;
    synth.setZoneLayout (layout);
    for (int i = 0; i < 15; ++i) synth.addVoice (new StringVoice (sp));
    synth.setCurrentPlaybackSampleRate (kFs);

    const int total = (int) kFs * kSecs, blk = 256;
    std::vector<float> mono ((size_t) total);
    juce::AudioBuffer<float> buf (2, blk);
    for (int pos = 0; pos < total; pos += blk)
    {
        juce::MidiBuffer mb;
        if (pos == 0)
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const int ch = 2 + (int) i;
                mb.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), 0);
                mb.addEvent (juce::MidiMessage::channelPressureChange (ch, 76), 0);
                mb.addEvent (juce::MidiMessage::controllerEvent (ch, 74, 64), 0);
                mb.addEvent (juce::MidiMessage::noteOn (ch, notes[i], 0.8f), 0);
            }
        buf.clear();
        synth.renderNextBlock (buf, mb, 0, blk);
        for (int i = 0; i < blk; ++i)
            mono[(size_t) (pos + i)] = std::sqrt (0.5f * (buf.getSample (0, i) * buf.getSample (0, i)
                                                       + buf.getSample (1, i) * buf.getSample (1, i)));
    }
    return mono;   // per-sample stereo magnitude (rms of L,R); peak taken from this too
}

float toDb (double x) { return 20.0f * std::log10 ((float) juce::jmax (x, 1.0e-9)); }

Result analyse (const std::vector<float>& m)
{
    Result r {};
    float pk = 0; for (float v : m) pk = juce::jmax (pk, v);
    r.peakDb = toDb (pk);
    auto rms = [&] (double a, double b)
    {
        double s = 0; int n = 0;
        for (int i = (int) (a * kFs); i < (int) (b * kFs) && i < (int) m.size(); ++i, ++n) s += (double) m[(size_t) i] * m[(size_t) i];
        return toDb (std::sqrt (s / juce::jmax (1, n)));
    };
    r.rmsDb = rms (0.3, 3.0);
    r.rmsAttack = rms (0.0, 1.0);
    r.rmsEarly = rms (0.3, 1.0);
    r.rmsLate = rms (3.5, 4.0);
    // decay to -30 dB below peak 50 ms-window RMS
    const int w = (int) (0.05 * kFs);
    std::vector<float> env;
    for (size_t i = 0; i + (size_t) w <= m.size(); i += (size_t) w / 2)
    {
        double s = 0; for (int k = 0; k < w; ++k) s += (double) m[i + (size_t) k] * m[i + (size_t) k];
        env.push_back (toDb (std::sqrt (s / w)));
    }
    size_t pi = 0; for (size_t i = 0; i < env.size(); ++i) if (env[i] > env[pi]) pi = i;
    r.decay = kSecs;   // '>= 4' means never fell
    for (size_t i = pi; i < env.size(); ++i)
        if (env[i] < env[pi] - 30.0f) { r.decay = (float) (i * (w / 2)) / (float) kFs - (float) (pi * (w / 2)) / (float) kFs; break; }
    return r;
}
}

int main (int argc, char** argv)
{
    std::string mdPath, applyPath;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string (argv[i]) == "--md") mdPath = argv[i + 1];
        if (std::string (argv[i]) == "--apply") applyPath = argv[i + 1];
    }
    for (int i = 1; i < argc; ++i)
        if (std::string (argv[i]) == "--profile")   // dump RMS-over-time for each preset (60)
        {
            const double w[] { 0, 0.02, 0.05, 0.1, 0.2, 0.3, 0.6, 1.0, 2.0, 3.0, 4.0 };
            for (int pi = 0; pi < kNumVoicePresets; ++pi)
            {
                auto m = render (kVoicePresets[pi], { 60 });
                std::printf ("%-18s", kVoicePresets[pi].name);
                for (int k = 0; k < 10; ++k)
                {
                    double s = 0; int n = 0;
                    for (int j = (int) (w[k] * kFs); j < (int) (w[k + 1] * kFs); ++j, ++n) s += (double) m[(size_t) j] * m[(size_t) j];
                    std::printf (" %6.1f", toDb (std::sqrt (s / n)));
                }
                std::printf ("\n");
            }
            return 0;
        }
    const std::vector<std::vector<int>> tests { { 40 }, { 60 }, { 84 }, { 60, 64, 67 } };
    std::ostringstream md;
    md << "# Preset audit results\n\nRendered 4 s @ 48 kHz, vel 0.8, pressure 0.6, notes 40/60/84 + C triad. "
          "RMS = mean dB over the 4 tests (0.3-3.0 s); peak = worst of the 4; decay = time to -30 dB (4.0 = never within 4 s). "
          "Target RMS " << kTargetDb << " dBFS, peak ceiling " << kPeakCeilDb << " dBFS after trim (1 dB margin under the -3 dBFS requirement: the excitation noise is unseeded, so runs vary by ~1 dB).\n\n"
          "* RMS window: 0.3-3.0 s for sustaining presets, 0-1.0 s for plucks (see notes column).\n\n| # | Name | Category | trim dB | RMS pre* | RMS post* | peak post | decay s | notes |\n|---|---|---|---|---|---|---|---|---|\n";
    std::ostringstream trims;
    float minPre = 99, maxPre = -99, minPost = 99, maxPost = -99;
    for (int pi = 0; pi < kNumVoicePresets; ++pi)
    {
        const auto& p = kVoicePresets[pi];
        float rmsSum = 0, atkSum = 0, peak = -200, decay = 99, earlyMin = 99, lateMin = 99; 
        std::string flags;
        for (size_t t = 0; t < tests.size(); ++t)
        {
            auto r = analyse (render (p, tests[t]));
            rmsSum += r.rmsDb; atkSum += r.rmsAttack; peak = juce::jmax (peak, r.peakDb); decay = juce::jmin (decay, r.decay);
            earlyMin = juce::jmin (earlyMin, r.rmsEarly); lateMin = juce::jmin (lateMin, r.rmsLate - r.rmsEarly);
            if (t == 0 && std::string (p.category) != "Arp & Rhythm" && r.decay < 0.3f) flags += "dies <0.3s; ";
        }
        // Decaying presets (plucks: hold/bow ~0, or sustain tail >20 dB below the attack) cannot
        // hold 0.3-3 s RMS, so they are leveled on the first second instead (marked 'a' in the table).
        const bool decaying = p.hold < 0.5f && p.bow < 0.4f;
        const float rmsPre = (decaying ? atkSum : rmsSum) / (float) tests.size();
        float trim = juce::jlimit (kTrimMin, kTrimMax, kTargetDb - rmsPre);
        trim = std::round (trim * 2.0f) / 2.0f;   // 0.5 dB grid
        if (peak + trim > kPeakCeilDb) { trim = std::floor ((kPeakCeilDb - peak) * 2.0f) / 2.0f; flags += "peak-limited; "; }
        if (trim <= kTrimMin + 0.01f) flags += "trim at floor; ";
        if (trim >= kTrimMax - 0.01f) flags += "trim at ceiling; ";
        if (decaying) flags += "leveled on 0-1 s RMS; ";
        const bool shouldSustain = p.hold >= 0.7f && p.bow >= 0.5f && p.sustain >= 0.9f;
        if (shouldSustain && lateMin < -12.0f) flags += "sustain fails (late-early " + std::to_string ((int) lateMin) + " dB); ";
        const float post = rmsPre + trim;
        minPre = juce::jmin (minPre, rmsPre); maxPre = juce::jmax (maxPre, rmsPre);
        minPost = juce::jmin (minPost, post); maxPost = juce::jmax (maxPost, post);
        char line[512];
        std::snprintf (line, sizeof line, "| %d | %s | %s | %+.1f | %.1f | %.1f | %.1f | %.2f%s | %s |\n", pi, p.name, p.category,
                       trim, rmsPre, post, peak + trim, decay, decay >= 3.99f ? "+" : "", flags.c_str());
        md << line;
        std::printf ("%s", line);
        trims << p.name << "|" << trim << "\n";
    }
    char s[256];
    std::snprintf (s, sizeof s, "\nRMS spread before trim: %.1f..%.1f dB (%.1f dB); after trim: %.1f..%.1f dB (%.1f dB)\n",
                   minPre, maxPre, maxPre - minPre, minPost, maxPost, maxPost - minPost);
    md << s; std::printf ("%s", s);
    if (! mdPath.empty()) std::ofstream (mdPath) << md.str();

    if (! applyPath.empty())
    {
        std::ifstream in (applyPath); std::stringstream ss; ss << in.rdbuf(); std::string src = ss.str();
        std::istringstream ts (trims.str()); std::string ln;
        while (std::getline (ts, ln))
        {
            const auto bar = ln.find ('|');
            const std::string name = ln.substr (0, bar); const float trim = std::stof (ln.substr (bar + 1));
            const auto at = src.find ("{ \"" + name + "\"");
            if (at == std::string::npos) continue;
            const auto eol = src.find ('\n', at);
            std::string row = src.substr (at, eol - at);
            // trimDb is the 18th float after the two strings
            size_t pos = 0;
            for (int q = 0; q < 4; ++q) pos = row.find ('"', pos) + 1;   // skip the name and category strings
            pos = row.find (',', pos) + 1;
            for (int k = 0; k < 17; ++k) pos = row.find (',', pos) + 1;
            const auto end = row.find (',', pos);
            char nb[32]; std::snprintf (nb, sizeof nb, " %+.1ff", trim);
            row.replace (pos, end - pos, nb);
            src.replace (at, eol - at, row);
        }
        std::ofstream (applyPath) << src;
    }
}
