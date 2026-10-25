# CLAUDE.md — WaveGrid

Original, open-source expressive string instrument: an isomorphic multi-touch
grid that speaks MPE, driving a digital-waveguide string synth. Inspired by the
*published* lineage (Smith's PASP, STK, Faust physmodels.lib, MPE spec) — this
is not a port of any proprietary app; never decompile or copy one.

Targets: macOS (AU/AUv3/VST3/Standalone) and iPadOS (AUv3/Standalone) from one
CMake tree. Primary host is GarageBand on both.

## Layout

```
CMakeLists.txt                 JUCE 8.0.4 via FetchContent; iOS switch on CMAKE_SYSTEM_NAME;
                               WAVEGRID_BUILD_TESTS=ON adds the WaveGrid_Tests console app
Source/dsp/WaveguideString.h   Single-delay-loop waveguide (Lagrange-3 frac delay,
                               loop LP, dispersion AP, pick-position comb, tanh drive, bow sustainer)
Source/dsp/StringVoice.h       MPESynthesiserVoice (string + detuned twin) + shared atomic StringParams
Source/dsp/BodyResonator.h     Body/cabinet resonances
Source/dsp/SympatheticBank.h   Sympathetic string coupling
Source/fx/Effects.h            Master FX (wgfx::): limiter, space/delay/shimmer
Source/ui/GridSurface.*        Touch grid -> MPE MIDI (bend/pressure/CC74) via MidiMessageCollector
Source/ui/Scales.h             kScales table (bitmask per scale) for highlight/snap
Source/ui/ModernLookAndFeel.h  Look and feel; HeaderMeters.h = header level meters
Source/PluginProcessor.*       MPESynthesiser (15 voices, lower zone, 48-semi bend), MIDI thru,
                               APVTS params, kVoicePresets (voice param selects a preset)
Source/PluginEditor.*          Knobs + grid; grid config mirrors APVTS params
tests/                         juce::UnitTest suite (DSP pitch/stability/denormals/benchmark, scales)
tools/validate.sh              Release AU+Standalone build, auval, unit tests, PASS/FAIL summary
```

Data flow: `GridSurface → MidiMessageCollector → processBlock merges into host MIDI
→ MPESynthesiser → audio; merged MIDI also leaves as MIDI out.`
Because of that, external MPE controllers (Linnstrument, Roli, etc.) play the
synth too, and the grid can drive other synths.

## Build & verify

```bash
# macOS
cmake -B build -G Xcode
cmake --build build --config Release --target WaveGrid_Standalone   # fastest iteration
cmake --build build --config Release --target WaveGrid_AU
auval -v aumu Wgrd Dpau

# iPad (needs a signing team; open build-ios/WaveGrid.xcodeproj and set it)
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
```

Tests: `cmake -B build-test -DWAVEGRID_BUILD_TESTS=ON && cmake --build build-test --target WaveGrid_Tests`
then run the `WaveGrid_Tests` binary (exit 1 on failure). Full gate: `tools/validate.sh`.
Pitch test asserts +-5 cents over MIDI 28..100; it prints a deviation table.

Standalone on Mac with a trackpad only gives one touch; test polyphony on iPad
or with an external MPE controller plugged into the Standalone's MIDI input.

## Rules

- `processSample()` and everything under `Source/dsp` is real-time code: no
  allocation, locks, I/O or logging. Buffers are sized in `prepare()`.
- UI → audio communication is MIDI through `MidiMessageCollector` or atomics
  in `StringParams`. Don't add other shared state.
- Pitch is continuous: all pitch changes go through `setFrequency()` which
  smooths delay length; never jump `smoothedDelay` on a live note.
- `kBendRangeSemis` (48) must stay equal in the zone layout and `GridSurface::Config`.
- Keep MPE message order on note-on: bend/pressure/timbre first, then note-on.
- JUCE style: 4-space indent, space before `(`, explicit `juce::`.

## Physics notes (for tuning work)

- Loop one-pole `(1−b)/(1−b z⁻¹)`: group delay at DC ≈ b/(1−b); the all-pass
  `(a+z⁻¹)/(1+a z⁻¹)` adds ≈ (1−a)/(1+a). Both are subtracted in `computeDelay`.
  If high notes play flat/sharp, this compensation is the first suspect
  (it's a DC approximation; a per-pitch phase-delay correction is the fix).
- Loop gain g and the LP jointly set T60 per partial: T60(ω) ≈ −6.91 / (ln(g·|H(ω)|) · f0).
- Decay on release is done by dropping sustain to the floor, not by a VCA —
  keeps the "muted string" character.

## Roadmap (payoff order)

Done: voice presets, bow/hold/breath, twin string + width, body resonator,
sympathetic bank, master FX, scale table, modern UI.

1. Hammer-on / pull-off: a new touch on an already-sounding row re-pitches
   that voice instead of plucking (needs per-row voice tracking in GridSurface).
2. Per-pitch exact phase-delay compensation in `computeDelay` (kills high-note
   flat/sharp; pitch test is the gate).
3. Commuted body: convolve excitation with a short guitar-body IR.
4. STK-style Bowed/Clarinet excitation as alternative voice models per preset.
5. Scale snapping of bends (diatonic grids) using `kScales`.
6. Visual feedback of bend amount on the grid.

## When asked to "fix the build"

Read the first error only. Likely: Xcode full install missing
(`xcode-select -p`), iOS signing team not set, or JUCE 8 API drift in
`juce::Font (FontOptions)` / `MPESynthesiser` ctor signatures.
