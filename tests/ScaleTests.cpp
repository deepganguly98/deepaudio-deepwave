#include <juce_core/juce_core.h>
#include "ui/Scales.h"
#include <vector>

struct ScaleTableTests : public juce::UnitTest
{
    ScaleTableTests() : juce::UnitTest ("Scales", "WaveGrid") {}

    void runTest() override
    {
        struct Ref { const char* name; std::vector<int> iv; };
        const std::vector<Ref> ref {
            { "Chromatic",        { 0,1,2,3,4,5,6,7,8,9,10,11 } },
            { "Major",            { 0,2,4,5,7,9,11 } },
            { "Natural Minor",    { 0,2,3,5,7,8,10 } },
            { "Dorian",           { 0,2,3,5,7,9,10 } },
            { "Phrygian",         { 0,1,3,5,7,8,10 } },
            { "Lydian",           { 0,2,4,6,7,9,11 } },
            { "Mixolydian",       { 0,2,4,5,7,9,10 } },
            { "Harmonic Minor",   { 0,2,3,5,7,8,11 } },
            { "Pentatonic Major", { 0,2,4,7,9 } },
            { "Pentatonic Minor", { 0,3,5,7,10 } },
            { "Blues",            { 0,3,5,6,7,10 } },
            { "Whole Tone",       { 0,2,4,6,8,10 } },
        };

        beginTest ("table matches documented intervals");
        expectEquals (kNumScales, (int) ref.size());
        for (int s = 0; s < juce::jmin (kNumScales, (int) ref.size()); ++s)
        {
            expectEquals (juce::String (kScales[s].name), juce::String (ref[(size_t) s].name));
            int expected = 0;
            for (int i : ref[(size_t) s].iv) expected |= 1 << i;
            expect (kScales[s].mask == expected,
                    juce::String (kScales[s].name) + ": mask 0x" + juce::String::toHexString (kScales[s].mask)
                    + " expected 0x" + juce::String::toHexString (expected));
            expect ((kScales[s].mask & 1) != 0, juce::String (kScales[s].name) + " must contain the root");
            expect ((kScales[s].mask & ~0xFFF) == 0, juce::String (kScales[s].name) + " has bits above 11");
        }
    }
};
static ScaleTableTests scaleTableTests;
