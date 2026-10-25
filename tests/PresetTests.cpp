#include <juce_core/juce_core.h>
#include "Presets.h"
#include <cmath>
#include <set>
#include <string>

struct PresetTableTests : public juce::UnitTest
{
    PresetTableTests() : juce::UnitTest ("Presets", "WaveGrid") {}

    template <typename T>
    bool inRange (T v, T lo, T hi) { return std::isfinite ((double) v) && v >= lo && v <= hi; }

    void runTest() override
    {
        beginTest ("ranges");
        expect (kNumVoicePresets >= 7);
        for (int i = 0; i < kNumVoicePresets; ++i)
        {
            const auto& p = kVoicePresets[i];
            const juce::String n (p.name);
            auto unit = [&] (float v, const char* f) { expect (inRange (v, 0.0f, 1.0f), n + ": " + f + " outside 0..1"); };
            unit (p.bright, "bright");   unit (p.sustain, "sustain"); unit (p.stiff, "stiff");
            unit (p.drive, "drive");     unit (p.bow, "bow");         unit (p.hold, "hold");
            unit (p.strike, "strike");   unit (p.breath, "breath");   unit (p.width, "width");
            unit (p.space, "space");     unit (p.body, "body");       unit (p.sympathy, "sympathy");
            unit (p.shimmer, "shimmer"); unit (p.delayMix, "delayMix");
            unit (p.comp, "comp");       unit (p.ensemble, "ensemble"); unit (p.warmth, "warmth");
            unit (p.arpGate, "arpGate");
            expect (inRange (p.pick, 0.02f, 0.5f), n + ": pick");
            expect (inRange (p.glide, 0.0f, 200.0f), n + ": glide");
            expect (inRange (p.attack, 1.0f, 1000.0f), n + ": attack");
            expect (inRange (p.trimDb, -12.0f, 6.0f), n + ": trimDb outside clamp");
            expect (inRange (p.delayTime, 60.0f, 1000.0f), n + ": delayTime");
            expect (inRange (p.eqLowDb, -12.0f, 12.0f), n + ": eqLowDb");
            expect (inRange (p.eqHighDb, -12.0f, 12.0f), n + ": eqHighDb");
            expect (p.name != nullptr && p.name[0] != 0 && p.category != nullptr, n + ": empty name/category");
        }

        beginTest ("arp fields");
        for (int i = 0; i < kNumVoicePresets; ++i)
        {
            const auto& p = kVoicePresets[i];
            const juce::String n (p.name);
            expect (p.arpOn == 0 || p.arpOn == 1, n + ": arpOn");
            expect (p.arpPattern >= 0 && p.arpPattern <= 6, n + ": arpPattern");
            expect (p.arpRate >= 0 && p.arpRate <= 5, n + ": arpRate");
            expect (p.arpOct >= 1 && p.arpOct <= 4, n + ": arpOct");
        }

        beginTest ("unique names");
        std::set<std::string> names;
        for (int i = 0; i < kNumVoicePresets; ++i)
            expect (names.insert (kVoicePresets[i].name).second, juce::String (kVoicePresets[i].name) + " duplicated");

        beginTest ("first seven are the originals, in order");
        {
            struct Orig { const char* name; float bright, sustain, stiff, pick, drive, glide, bow, hold, attack, strike, breath, width, space, body, sympathy, shimmer, delayMix; };
            const Orig orig[7] = {
                { "Glass Continuum", 0.55f, 0.95f, 0.25f, 0.18f, 0.00f, 30.0f, 0.90f, 0.95f, 140.0f, 0.10f, 0.00f, 0.60f, 0.60f, 0.30f, 0.30f, 0.35f, 0.15f },
                { "Velvet Cello",    0.35f, 0.92f, 0.08f, 0.30f, 0.05f, 35.0f, 0.85f, 0.90f, 220.0f, 0.05f, 0.10f, 0.40f, 0.45f, 0.60f, 0.35f, 0.05f, 0.08f },
                { "Neon Lead",       0.75f, 0.97f, 0.15f, 0.10f, 0.55f, 55.0f, 1.00f, 1.00f,  30.0f, 0.10f, 0.00f, 0.45f, 0.30f, 0.10f, 0.10f, 0.10f, 0.25f },
                { "Breath Flute",    0.45f, 0.90f, 0.00f, 0.50f, 0.00f, 45.0f, 0.30f, 0.70f,  90.0f, 0.00f, 0.80f, 0.30f, 0.50f, 0.20f, 0.10f, 0.15f, 0.12f },
                { "Aurora Drone",    0.40f, 1.00f, 0.40f, 0.20f, 0.10f, 70.0f, 0.80f, 1.00f, 600.0f, 0.00f, 0.20f, 1.00f, 0.90f, 0.35f, 0.45f, 0.55f, 0.20f },
                { "Soft Harp",       0.50f, 0.88f, 0.05f, 0.15f, 0.00f, 10.0f, 0.25f, 0.30f,   8.0f, 0.45f, 0.00f, 0.50f, 0.50f, 0.50f, 0.50f, 0.20f, 0.12f },
                { "Steel Pluck",     0.70f, 0.80f, 0.20f, 0.13f, 0.00f,  6.0f, 0.30f, 0.00f, 1.0f, 0.80f, 0.00f, 0.35f, 0.25f, 0.45f, 0.30f, 0.00f, 0.10f },
            };
            for (int i = 0; i < 7; ++i)
            {
                const auto& p = kVoicePresets[i]; const auto& o = orig[i];
                const juce::String n (o.name);
                expectEquals (juce::String (p.name), n);
                const float a[] { p.bright, p.sustain, p.stiff, p.pick, p.drive, p.glide, p.bow, p.hold, p.attack, p.strike, p.breath, p.width, p.space, p.body, p.sympathy, p.shimmer, p.delayMix };
                const float b[] { o.bright, o.sustain, o.stiff, o.pick, o.drive, o.glide, o.bow, o.hold, o.attack, o.strike, o.breath, o.width, o.space, o.body, o.sympathy, o.shimmer, o.delayMix };
                for (int k = 0; k < 17; ++k)
                    expect (std::abs (a[k] - b[k]) < 1.0e-6f, n + ": original field " + juce::String (k) + " changed");
                expect (p.arpOn == 0, n + ": originals default to arp off");
            }
        }

        beginTest ("categories");
        {
            std::set<std::string> known;
            for (int c = 0; c < kNumPresetCategories; ++c) known.insert (kPresetCategories[c]);
            expectEquals ((int) known.size(), kNumPresetCategories);
            std::set<std::string> used;
            for (int i = 0; i < kNumVoicePresets; ++i)
            {
                expect (known.count (kVoicePresets[i].category) == 1, juce::String (kVoicePresets[i].name) + ": unknown category");
                used.insert (kVoicePresets[i].category);
            }
            for (int c = 0; c < kNumPresetCategories; ++c)
                expect (used.count (kPresetCategories[c]) == 1, juce::String (kPresetCategories[c]) + " has no presets");
            int arp = 0;
            for (int i = 0; i < kNumVoicePresets; ++i) arp += kVoicePresets[i].arpOn;
            expect (arp >= 4, "expected several arp presets");
        }
    }
};
static PresetTableTests presetTableTests;
