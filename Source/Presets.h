#pragma once
// Factory voice bank. Header-only constexpr data.
// The first 7 entries are the original presets and MUST stay in this order
// (saved sessions refer to the 'voice' choice index; entry 0 seeds the defaults).
// trimDb values are measured by tools/preset_audit (target ~-23 dBFS RMS); do not hand-edit
// them, re-run the audit with --apply instead.

struct VoicePreset
{
    const char* name; const char* category;
    float bright, sustain, stiff, pick, drive, glide, bow, hold, attack, strike, breath, width, space;
    float body, sympathy, shimmer, delayMix, trimDb;
    float delayTime;                                   // ms 60..1000
    float eqLowDb, eqHighDb, comp, ensemble, warmth;   // master chain: dB -12..12 / 0..1 amounts
    int arpOn, arpPattern, arpRate, arpOct; float arpGate;   // pattern 0..6 (Up,Down,Up-Down,Down-Up,Random,As Played,Chord), rate 0..5 (1/4,1/8,1/8T,1/16,1/16T,1/32), octaves 1..4
};

inline constexpr VoicePreset kVoicePresets[] = {
    //  name, category,       bright sust  stiff pick  drive glide  bow   hold  atk     strike breath width space  body  symp  shim  dlyMx trimDb dlyMs eqLo  eqHi  comp  ens   warm  arp pat rate oct gate
    // ---- original seven ----
    { "Glass Continuum", "Bowed",        0.55f, 0.95f, 0.25f, 0.18f, 0.00f, 30.0f, 0.90f, 0.95f, 140.0f, 0.10f, 0.00f, 0.60f, 0.60f, 0.30f, 0.30f, 0.35f, 0.15f, -0.5f, 380.0f,  0.0f,  1.0f, 0.20f, 0.30f, 0.10f, 0, 0, 3, 1, 0.60f },
    { "Velvet Cello",    "Bowed",        0.35f, 0.92f, 0.08f, 0.30f, 0.05f, 35.0f, 0.85f, 0.90f, 220.0f, 0.05f, 0.10f, 0.40f, 0.45f, 0.60f, 0.35f, 0.05f, 0.08f, +1.0f, 320.0f,  2.0f, -2.0f, 0.25f, 0.15f, 0.45f, 0, 0, 3, 1, 0.60f },
    { "Neon Lead",       "Leads",        0.75f, 0.97f, 0.15f, 0.10f, 0.55f, 55.0f, 1.00f, 1.00f,  30.0f, 0.10f, 0.00f, 0.45f, 0.30f, 0.10f, 0.10f, 0.10f, 0.25f, -4.0f, 360.0f, -2.0f,  2.0f, 0.40f, 0.20f, 0.15f, 0, 0, 3, 1, 0.60f },
    { "Breath Flute",    "Wind & Breath",0.45f, 0.90f, 0.00f, 0.50f, 0.00f, 45.0f, 0.30f, 0.70f,  90.0f, 0.00f, 0.80f, 0.30f, 0.50f, 0.20f, 0.10f, 0.15f, 0.12f, +4.0f, 420.0f, -1.0f,  1.0f, 0.15f, 0.10f, 0.25f, 0, 0, 3, 1, 0.60f },
    { "Aurora Drone",    "Pads & Drones",0.40f, 1.00f, 0.40f, 0.20f, 0.10f, 70.0f, 0.80f, 1.00f, 600.0f, 0.00f, 0.20f, 1.00f, 0.90f, 0.35f, 0.45f, 0.55f, 0.20f, -0.5f, 560.0f,  1.0f,  0.0f, 0.10f, 0.55f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Soft Harp",       "Plucked",      0.50f, 0.88f, 0.05f, 0.15f, 0.00f, 10.0f, 0.25f, 0.30f,   8.0f, 0.45f, 0.00f, 0.50f, 0.50f, 0.50f, 0.50f, 0.20f, 0.12f, +3.0f, 340.0f,  0.0f,  1.0f, 0.15f, 0.15f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Steel Pluck",     "Plucked",      0.70f, 0.80f, 0.20f, 0.13f, 0.00f,  6.0f, 0.30f, 0.00f,   1.0f, 0.80f, 0.00f, 0.35f, 0.25f, 0.45f, 0.30f, 0.00f, 0.10f, -2.0f, 300.0f,  0.0f,  2.0f, 0.30f, 0.05f, 0.10f, 0, 0, 3, 1, 0.60f },
    // ---- Bowed ----
    { "Violin Aria",     "Bowed",        0.62f, 0.93f, 0.04f, 0.25f, 0.08f, 45.0f, 0.90f, 0.90f, 120.0f, 0.08f, 0.03f, 0.30f, 0.40f, 0.60f, 0.30f, 0.05f, 0.06f, -1.5f, 340.0f, -1.0f,  2.0f, 0.25f, 0.20f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Glass Harmonica", "Bowed",        0.30f, 1.00f, 0.55f, 0.50f, 0.00f, 60.0f, 0.70f, 1.00f, 350.0f, 0.00f, 0.10f, 0.70f, 0.70f, 0.05f, 0.20f, 0.40f, 0.15f, +1.5f, 450.0f,  0.0f,  1.0f, 0.10f, 0.30f, 0.05f, 0, 0, 3, 1, 0.60f },
    { "E-Bow Haze",      "Bowed",        0.50f, 1.00f, 0.02f, 0.08f, 0.35f, 90.0f, 1.00f, 1.00f, 450.0f, 0.00f, 0.00f, 0.55f, 0.55f, 0.10f, 0.20f, 0.30f, 0.30f, -3.5f, 500.0f,  0.0f,  0.0f, 0.35f, 0.25f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Hurdy Gurdy",     "Bowed",        0.70f, 1.00f, 0.15f, 0.12f, 0.30f,  8.0f, 1.00f, 1.00f,  25.0f, 0.20f, 0.00f, 0.40f, 0.30f, 0.55f, 0.60f, 0.00f, 0.08f, -4.5f, 280.0f,  1.0f,  1.0f, 0.40f, 0.10f, 0.40f, 0, 0, 3, 1, 0.60f },
    { "Dark Fiddle",     "Bowed",        0.48f, 0.90f, 0.10f, 0.20f, 0.20f, 20.0f, 0.85f, 0.85f,  60.0f, 0.25f, 0.05f, 0.35f, 0.30f, 0.50f, 0.35f, 0.00f, 0.10f, -0.5f, 300.0f,  1.0f, -1.0f, 0.35f, 0.10f, 0.35f, 0, 0, 3, 1, 0.60f },
    // ---- Plucked ----
    { "Nylon Dream",     "Plucked",      0.38f, 0.95f, 0.04f, 0.20f, 0.00f,  8.0f, 0.10f, 0.10f,   3.0f, 0.60f, 0.00f, 0.40f, 0.40f, 0.75f, 0.25f, 0.05f, 0.08f, +0.5f, 360.0f,  1.0f, -1.0f, 0.20f, 0.05f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Kora Rain",       "Plucked",      0.60f, 0.96f, 0.10f, 0.09f, 0.00f,  6.0f, 0.15f, 0.20f,   2.0f, 0.70f, 0.00f, 0.60f, 0.45f, 0.55f, 0.60f, 0.10f, 0.18f, +1.5f, 390.0f,  0.0f,  1.0f, 0.20f, 0.20f, 0.15f, 0, 0, 3, 1, 0.60f },
    { "Koto Moon",       "Plucked",      0.55f, 0.95f, 0.30f, 0.35f, 0.00f, 25.0f, 0.10f, 0.05f,   1.0f, 1.00f, 0.00f, 0.35f, 0.35f, 0.30f, 0.35f, 0.15f, 0.20f, +1.5f, 450.0f, -1.0f,  2.0f, 0.15f, 0.00f, 0.10f, 0, 0, 3, 1, 0.60f },
    { "Mandolin Spark",  "Plucked",      0.80f, 0.88f, 0.12f, 0.08f, 0.00f,  4.0f, 0.10f, 0.00f,   1.0f, 0.95f, 0.00f, 0.55f, 0.25f, 0.40f, 0.35f, 0.00f, 0.06f, -1.0f, 250.0f, -1.0f,  3.0f, 0.35f, 0.10f, 0.00f, 0, 0, 3, 1, 0.60f },
    { "Dulcimer Hammer", "Plucked",      0.50f, 0.93f, 0.35f, 0.40f, 0.00f,  5.0f, 0.10f, 0.00f,   2.0f, 0.90f, 0.00f, 0.50f, 0.40f, 0.30f, 0.70f, 0.15f, 0.15f, -2.0f, 330.0f,  0.0f,  1.0f, 0.25f, 0.15f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Wire Chime",      "Plucked",      0.65f, 0.94f, 0.85f, 0.30f, 0.00f,  3.0f, 0.00f, 0.00f,   1.0f, 0.85f, 0.00f, 0.65f, 0.60f, 0.05f, 0.20f, 0.35f, 0.25f, -0.5f, 600.0f, -2.0f,  3.0f, 0.10f, 0.10f, 0.00f, 0, 0, 3, 1, 0.60f },
    // ---- Pads & Drones ----
    { "Evolving Strings","Pads & Drones",0.45f, 1.00f, 0.12f, 0.20f, 0.05f, 50.0f, 0.70f, 1.00f, 700.0f, 0.00f, 0.05f, 0.90f, 0.70f, 0.40f, 0.35f, 0.30f, 0.20f, +1.0f, 480.0f,  0.0f,  0.0f, 0.15f, 0.70f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Glacier",         "Pads & Drones",0.20f, 1.00f, 0.50f, 0.35f, 0.00f, 80.0f, 0.75f, 1.00f, 900.0f, 0.00f, 0.15f, 0.85f, 1.00f, 0.10f, 0.40f, 0.60f, 0.30f, +4.0f, 640.0f,  1.0f, -1.0f, 0.10f, 0.40f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Choir Breath",    "Pads & Drones",0.40f, 0.95f, 0.00f, 0.45f, 0.00f, 40.0f, 0.70f, 0.90f, 500.0f, 0.00f, 0.60f, 0.80f, 0.65f, 0.15f, 0.25f, 0.20f, 0.18f, +4.0f, 450.0f,  0.0f,  0.0f, 0.15f, 0.65f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Sub Drone",       "Pads & Drones",0.10f, 1.00f, 0.00f, 0.18f, 0.10f, 60.0f, 0.80f, 1.00f, 800.0f, 0.00f, 0.00f, 0.50f, 0.50f, 0.30f, 0.30f, 0.05f, 0.10f, +4.0f, 380.0f,  3.0f, -3.0f, 0.30f, 0.15f, 0.60f, 0, 0, 3, 1, 0.60f },
    // ---- Leads ----
    { "Singing Lead",    "Leads",        0.60f, 0.97f, 0.05f, 0.15f, 0.25f, 80.0f, 0.95f, 1.00f,  60.0f, 0.15f, 0.05f, 0.40f, 0.30f, 0.35f, 0.15f, 0.10f, 0.22f, -4.0f, 380.0f, -1.0f,  2.0f, 0.35f, 0.15f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Vintage Saw",     "Leads",        0.90f, 1.00f, 0.00f, 0.05f, 0.70f, 20.0f, 1.00f, 1.00f,  12.0f, 0.30f, 0.00f, 0.65f, 0.20f, 0.00f, 0.05f, 0.00f, 0.18f, -4.0f, 330.0f, -2.0f,  1.0f, 0.50f, 0.35f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Glide Lead",      "Leads",        0.55f, 0.98f, 0.08f, 0.12f, 0.35f,180.0f, 1.00f, 1.00f,  40.0f, 0.10f, 0.00f, 0.30f, 0.35f, 0.15f, 0.10f, 0.20f, 0.28f, -4.0f, 400.0f,  0.0f,  1.0f, 0.40f, 0.10f, 0.15f, 0, 0, 3, 1, 0.60f },
    // ---- Wind & Breath ----
    { "Shakuhachi",      "Wind & Breath",0.35f, 0.97f, 0.02f, 0.45f, 0.00f, 90.0f, 0.45f, 0.65f, 130.0f, 0.00f, 0.90f, 0.25f, 0.55f, 0.25f, 0.05f, 0.10f, 0.15f, +3.0f, 450.0f, -1.0f,  0.0f, 0.10f, 0.05f, 0.30f, 0, 0, 3, 1, 0.60f },
    { "Pure Whistle",    "Wind & Breath",0.25f, 0.97f, 0.00f, 0.50f, 0.00f, 70.0f, 0.60f, 0.85f,  80.0f, 0.00f, 0.45f, 0.20f, 0.40f, 0.05f, 0.05f, 0.10f, 0.10f, +5.0f, 350.0f, -1.0f,  1.0f, 0.15f, 0.00f, 0.10f, 0, 0, 3, 1, 0.60f },
    // ---- Bass ----
    { "Upright Bass",    "Bass",         0.25f, 0.90f, 0.08f, 0.20f, 0.05f,  6.0f, 0.10f, 0.00f,   2.0f, 0.80f, 0.00f, 0.20f, 0.20f, 0.80f, 0.20f, 0.00f, 0.04f, -2.5f, 260.0f,  3.0f, -2.0f, 0.40f, 0.00f, 0.50f, 0, 0, 3, 1, 0.60f },
    { "Sub Pluck",       "Bass",         0.12f, 0.92f, 0.00f, 0.15f, 0.10f,  5.0f, 0.10f, 0.00f,   1.0f, 0.90f, 0.00f, 0.10f, 0.15f, 0.30f, 0.10f, 0.00f, 0.03f, -1.0f, 240.0f,  2.0f, -3.0f, 0.45f, 0.00f, 0.55f, 0, 0, 3, 1, 0.60f },
    { "Growl Bass",      "Bass",         0.35f, 0.97f, 0.05f, 0.10f, 0.75f, 25.0f, 1.00f, 1.00f,  15.0f, 0.40f, 0.00f, 0.25f, 0.15f, 0.20f, 0.10f, 0.00f, 0.05f, -3.0f, 270.0f,  2.0f, -1.0f, 0.55f, 0.00f, 0.40f, 0, 0, 3, 1, 0.60f },
    // ---- Arp & Rhythm ----
    { "Crystal Arp",     "Arp & Rhythm", 0.80f, 0.88f, 0.30f, 0.15f, 0.00f,  3.0f, 0.10f, 0.00f,   1.0f, 0.90f, 0.00f, 0.60f, 0.45f, 0.10f, 0.25f, 0.30f, 0.30f, -2.0f, 375.0f, -1.0f,  3.0f, 0.20f, 0.20f, 0.05f, 1, 0, 3, 2, 0.45f },
    { "Pulse Pluck 1/8", "Arp & Rhythm", 0.60f, 0.85f, 0.10f, 0.12f, 0.10f,  3.0f, 0.10f, 0.00f,   1.0f, 0.95f, 0.00f, 0.40f, 0.25f, 0.35f, 0.20f, 0.00f, 0.20f, -1.5f, 375.0f,  1.0f,  1.0f, 0.45f, 0.00f, 0.20f, 1, 0, 1, 1, 0.55f },
    { "Cascade 1/16T",   "Arp & Rhythm", 0.65f, 0.90f, 0.20f, 0.10f, 0.00f,  4.0f, 0.10f, 0.00f,   1.0f, 0.85f, 0.00f, 0.65f, 0.50f, 0.30f, 0.50f, 0.20f, 0.30f, -1.5f, 450.0f,  0.0f,  2.0f, 0.25f, 0.20f, 0.10f, 1, 1, 4, 3, 0.50f },
    { "Ping Pong Bells", "Arp & Rhythm", 0.70f, 0.92f, 0.65f, 0.28f, 0.00f,  3.0f, 0.05f, 0.00f,   1.0f, 0.85f, 0.00f, 0.70f, 0.50f, 0.10f, 0.25f, 0.30f, 0.35f, -1.0f, 333.0f, -1.0f,  2.0f, 0.15f, 0.25f, 0.00f, 1, 2, 3, 2, 0.40f },
    { "Harp Chords",     "Arp & Rhythm", 0.50f, 0.94f, 0.05f, 0.15f, 0.00f,  8.0f, 0.20f, 0.25f,   4.0f, 0.60f, 0.00f, 0.55f, 0.50f, 0.50f, 0.50f, 0.20f, 0.15f, +1.0f, 400.0f,  0.0f,  1.0f, 0.20f, 0.15f, 0.20f, 1, 6, 1, 1, 0.80f },
    { "Random Sparkle",  "Arp & Rhythm", 0.75f, 0.84f, 0.40f, 0.14f, 0.00f,  3.0f, 0.05f, 0.00f,   1.0f, 0.90f, 0.00f, 0.75f, 0.60f, 0.10f, 0.20f, 0.45f, 0.35f, -1.5f, 280.0f, -2.0f,  3.0f, 0.15f, 0.30f, 0.00f, 1, 4, 5, 3, 0.35f },
    // ---- Cinematic ----
    { "Shimmer Swell",   "Cinematic",    0.40f, 1.00f, 0.20f, 0.20f, 0.00f, 70.0f, 0.75f, 1.00f, 950.0f, 0.00f, 0.10f, 0.90f, 0.95f, 0.20f, 0.35f, 0.95f, 0.30f, +2.5f, 700.0f,  0.0f,  1.0f, 0.10f, 0.55f, 0.20f, 0, 0, 3, 1, 0.60f },
    { "Frozen Cathedral","Cinematic",    0.30f, 1.00f, 0.60f, 0.40f, 0.00f, 50.0f, 0.65f, 1.00f, 800.0f, 0.00f, 0.20f, 1.00f, 1.00f, 0.15f, 0.55f, 0.70f, 0.40f, +4.0f, 900.0f,  1.0f, -2.0f, 0.10f, 0.35f, 0.25f, 0, 0, 3, 1, 0.60f },
    { "Event Horizon",   "Cinematic",    0.18f, 1.00f, 0.30f, 0.15f, 0.30f, 90.0f, 0.90f, 1.00f, 700.0f, 0.00f, 0.00f, 0.80f, 1.00f, 0.25f, 0.30f, 0.50f, 0.35f, -1.0f, 800.0f,  4.0f, -3.0f, 0.30f, 0.30f, 0.50f, 0, 0, 3, 1, 0.60f },
    { "Tidal Hollow",    "Cinematic",    0.28f, 0.95f, 0.10f, 0.30f, 0.05f, 60.0f, 0.60f, 0.95f, 600.0f, 0.05f, 0.45f, 0.85f, 0.85f, 0.45f, 0.40f, 0.35f, 0.30f, +5.5f, 650.0f,  2.0f, -2.0f, 0.15f, 0.45f, 0.40f, 0, 0, 3, 1, 0.60f },
};
inline constexpr int kNumVoicePresets = (int) (sizeof (kVoicePresets) / sizeof (kVoicePresets[0]));

inline constexpr const char* kPresetCategories[] = {
    "Bowed", "Plucked", "Pads & Drones", "Leads", "Wind & Breath", "Bass", "Arp & Rhythm", "Cinematic"
};
inline constexpr int kNumPresetCategories = (int) (sizeof (kPresetCategories) / sizeof (kPresetCategories[0]));
