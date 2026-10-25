# WaveGrid

An open-source expressive string instrument for GarageBand (Mac + iPad):
an isomorphic multi-touch grid that emits MPE, driving a digital-waveguide
string synthesizer. Slide, bend, vibrato and pressure are all continuous.

Lineage, all public: J. O. Smith's *Physical Audio Signal Processing*
(digital waveguides, Jaffe–Smith extended Karplus–Strong), the Synthesis
ToolKit, Faust `physmodels.lib`, and the MIDI MPE specification.

## Build

macOS (AU, AUv3, VST3, Standalone):
```bash
cmake -B build -G Xcode
cmake --build build --config Release --target WaveGrid_AU
auval -v aumu Wgrd Dpau
```

iPad (AUv3 + Standalone; open the project in Xcode and set your signing team):
```bash
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
open build-ios/WaveGrid.xcodeproj
```

## Features

- Isomorphic multi-touch grid emitting MPE (per-note bend, pressure, CC74).
- Digital-waveguide string synth: 15 voices, each a main + detuned twin string,
  fractional-delay continuous pitch, dispersion, pick-position comb.
- Hold / Bow sustain (E-bow style: held finger or motion keeps the string singing),
  Breath excitation, Attack, Strike.
- Voice presets (Glass Continuum, Velvet Cello, Neon Lead, Breath Flute, Aurora
  Drone, Soft Harp, Steel Pluck) via the Voice parameter.
- Body resonator, sympathetic strings, space/shimmer FX, master limiter.
- Scale highlighting/snap (12 scales), configurable rows/base note/row interval.
- External MPE controllers (Linnstrument, Roli, ...) play the synth; the grid
  can drive other synths through MIDI out.
- Formats: AU, AUv3, VST3, Standalone (macOS); AUv3, Standalone (iPadOS).

## Test

```bash
cmake -B build-test -DWAVEGRID_BUILD_TESTS=ON
cmake --build build-test --target WaveGrid_Tests
./build-test/WaveGrid_Tests_artefacts/WaveGrid_Tests   # path may vary by generator
tools/validate.sh                                       # build + auval + tests
```

## Playing

- Rows are strings tuned in fourths (configurable), columns are semitones.
- Touch = pluck. Drag horizontally = continuous bend with fret snapping
  (SNAP knob: 0 = fretless, 0.49 = hard quantised). Wiggle = vibrato.
- Vertical position within a row (or real pressure on Force Touch / Pencil)
  = pressure → brightness and drive. CC74 (timbre) also tracks Y.
- Multi-touch on iPad gives full polyphony with per-note expression.
- MIDI out carries the MPE stream, so WaveGrid can play any MPE synth in
  the same host; MIDI in accepts external MPE controllers.

## Controls

- Grid: touch = pluck; horizontal drag = bend; vertical = pressure/timbre.
- HOLD: how long a resting finger sustains. BOW: energy fed back while held or moving.
- PRESET / VOICE: loads a full parameter set; all knobs stay editable afterwards.
- MPE: lower zone, 15 member channels, +-48 semitone bend. Set external
  controllers to the same range.

## Knobs

BRIGHT (loop loss filter), SUSTAIN (loop gain), STIFF (dispersion →
inharmonicity), PICK (pick position comb), DRIVE (post soft-clip), GLIDE
(pitch smoothing), SNAP, ROWS, BASE (lowest note), TUNING (semitones per
row), OUT.

See `CLAUDE.md` for architecture, physics notes and roadmap.

## iPad / GarageBand

Build the AUv3 with a signing team set in `build-ios/WaveGrid.xcodeproj`, run
the Standalone app once so the extension registers, then in GarageBand choose
Instruments > Audio Recorder > Plug-ins and Apps > WaveGrid (AUv3 slot).
GarageBand on Mac loads the AU (`~/Library/Audio/Plug-Ins/Components`). GarageBand
does not route plug-in MIDI out to other tracks; use the Standalone for that.
Multi-touch polyphony needs a real iPad (Mac trackpad gives one touch).

## Release checklist

- [ ] App icon (iOS asset catalog + macOS icns) set on the target
- [ ] Bump version in CMakeLists.txt
- [ ] Code signing: Developer ID (Mac), team + provisioning (iPad)
- [ ] Hardened runtime, notarise (`xcrun notarytool submit`) and staple
- [ ] `tools/validate.sh` passes (build, auval, unit tests)
- [ ] GarageBand test on Mac (AU) and iPad (AUv3): load, play, save/reload project
- [ ] Check CPU with 15 voices on the oldest supported iPad
