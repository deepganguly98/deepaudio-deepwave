# DeepWave on Windows (VST3 + standalone, FL Studio)

## What you get
| Format | File | Where it goes |
|---|---|---|
| **VST3** instrument | `DeepWave.vst3` (a folder; the actual plugin is the .dll-style binary inside it) | `C:\Program Files\Common Files\VST3\` |
| **Standalone app** | `DeepWave.exe` | anywhere, double-click |

**VST2 is not offered.** Steinberg withdrew the VST2 SDK in 2018 and JUCE 8 removed VST2 support, so a legal new VST2
`.dll` cannot be built. FL Studio loads **VST3** natively (FL Studio 20.8 or newer, 64-bit), as do Reaper, Ableton,
Cubase, Bitwig, Studio One. If you need a VST2 `.dll` for an old host, a wrapper such as a VST3-to-VST2 bridge is the
only route and is outside this project.

## Build it (you need a Windows PC, or use the cloud build below)
This was prepared on a Mac, so **the Windows build has not been compiled or run yet**: expect to read the first error
once, like any new platform.

**On a PC:** install Visual Studio 2022 or newer (workload *Desktop development with C++*) and CMake, then in a Developer
Command Prompt from the project folder: `windows\build_windows.bat`. Results land in `dist\`.

**Without a PC:** push the project to a GitHub repository and run the *Windows build* workflow
(`.github/workflows/windows.yml`, Actions tab -> Run workflow). It builds on a Windows machine, runs the tests and
attaches `DeepWave-windows-x64` (the .vst3 and the .exe) to the run. Nothing has been uploaded anywhere yet.

## FL Studio
1. Copy `DeepWave.vst3` to `C:\Program Files\Common Files\VST3\`.
2. FL Studio: *Options -> Manage plugins -> Find installed plugins* (make sure the VST3 folder is scanned).
3. Add it from *Add -> Instruments* (or the plugin database, *Installed -> Plugin database -> Generators*), then play it
   from the piano roll, a MIDI keyboard, or the on-screen grid in the plugin window.
- The grid inside the plugin window plays on its own, independent of FL's piano roll, and several fingers work on a
  touch screen.
- FL Studio sends notes on a single MIDI channel. The synth accepts that, but notes from the piano roll won't have
  per-note bend/pressure like the grid; use a MPE controller or the grid for that.
- Use a 64-bit FL Studio; this is a 64-bit plugin.

## Not verified yet
Everything Windows-specific: the build, the VST3 validator, FL Studio scanning/loading, touch input, and audio device
selection in the standalone. Source is plain JUCE C++20 with no platform-specific code, which makes problems unlikely
but not impossible.
