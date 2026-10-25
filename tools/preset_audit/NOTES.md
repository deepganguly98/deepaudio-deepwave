# preset_audit

Offline loudness/sustain audit for `Source/Presets.h`. Renders every preset through the real `StringVoice`
(via `juce::MPESynthesiser`, same zone layout as the plugin) at 48 kHz for MIDI 40, 60, 84 and a C triad
(vel 0.8, pressure 0.6, 4 s) and sets `trimDb` so RMS lands at -23 dBFS with peaks <= -4 dBFS (spec: -3).

Build (scratch project, not part of the main CMake tree): `juce_add_console_app` with `audit.cpp`,
include dirs `Source`, link `juce_dsp juce_audio_basics`, then
`PresetAudit --md tools/preset_audit/RESULTS.md --apply Source/Presets.h`
(`--apply` rewrites the trimDb column in place; `--profile` dumps RMS-over-time per preset).

Method notes
- Sustaining presets are leveled on mean-dB RMS over 0.3-3.0 s. Decaying ones (hold < 0.5 and bow < 0.4:
  plucks, bass, arps) are leveled on 0-1.0 s because their 0.3-3 s RMS is 30-60 dB down; their peak ceiling
  is usually the binding constraint, so their sustained RMS stays a few dB below the bowed presets.
- trimDb is applied after the voice, before effects; it does not model Space/shimmer/delay/limiter.
- The excitation noise is unseeded (`juce::Random` default), so figures vary about +-1 dB run to run.

Engine observations (not edited, for the DSP owner)
1. `setSustain` maps to loop gain PER ROUND TRIP, so decay time scales with pitch: a pluck at MIDI 84 rings
   ~4x shorter than at MIDI 60 and ~16x shorter than at MIDI 40. Mapping sustain to a T60 in seconds
   (g = 10^(-3 / (T60 * f0))) would give uniform decay across the keyboard.
2. A pluck is a one-period noise burst, so energy is spread over all partials: after the first ~100 ms the
   fundamental is 25-35 dB below the transient. Plucked presets sound "thin" in the tail compared with bowed ones
   and need +3..+6 dB trim; Soft Harp and Breath Flute hit the +6 dB trim ceiling (Soft Harp's 0-1 s RMS is ~-40 dBFS pre-trim).
   A lowpassed/pitched excitation (or a Karplus-Strong style sample) would lift the tail.
3. Breath-only voices (breath 0.8-0.9, low bow) are ~10 dB quieter than bowed ones because the breath injection is only
   0.05 * breath; bow >= 0.45 is needed for them to reach the target.
4. Decay-to-30 dB is dominated by the attack transient for plucks (0.1-0.3 s) even though the low notes ring for seconds.
