#pragma once

// Scale table shared by the grid (highlight/snap), the processor (choice param) and the editor.
// `mask` bit i set => pitch class (root + i) % 12 is in the scale.
struct ScaleDef { const char* name; int mask; };

inline constexpr ScaleDef kScales[] = {
    { "Chromatic", 0xFFF },   // 0,1,2,3,4,5,6,7,8,9,10,11
    { "Major", 0xAB5 },   // 0,2,4,5,7,9,11
    { "Natural Minor", 0x5AD },   // 0,2,3,5,7,8,10
    { "Dorian", 0x6AD },   // 0,2,3,5,7,9,10
    { "Phrygian", 0x5AB },   // 0,1,3,5,7,8,10
    { "Lydian", 0xAD5 },   // 0,2,4,6,7,9,11
    { "Mixolydian", 0x6B5 },   // 0,2,4,5,7,9,10
    { "Harmonic Minor", 0x9AD },   // 0,2,3,5,7,8,11
    { "Pentatonic Major", 0x295 },   // 0,2,4,7,9
    { "Pentatonic Minor", 0x4A9 },   // 0,3,5,7,10
    { "Blues", 0x4E9 },   // 0,3,5,6,7,10
    { "Whole Tone", 0x555 },   // 0,2,4,6,8,10
};
inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
