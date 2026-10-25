# DeepWave iPad touch bridge

Use an iPad (or any phone/tablet with a browser) as a multi-touch MPE controller for DeepWave running
on the Mac. No app install, no signing: the iPad opens a web page, the Mac turns each finger into MPE
MIDI on a virtual port called **DeepWave iPad**.

## Use

1. Run `tools/ipad_bridge/run.sh` (compiles on first run, starts DeepWave, then the bridge).
2. In DeepWave: **Options -> Audio/MIDI Settings...** and tick **DeepWave iPad** under *Active MIDI inputs*.
   One-time: the Standalone remembers it. Start DeepWave **before** the bridge (run.sh does), because JUCE's
   Standalone only auto-enables MIDI devices that appear while it is running.
3. On the iPad (same Wi-Fi): Safari -> the `http://<mac-ip>:8080` address the bridge prints.
   Allow incoming connections if macOS asks. "Add to Home Screen" gives a fullscreen view.

The top bar shows connection status, live finger count and the maximum reached (iPad Air: up to 11).

## What it sends

One MPE note per finger on channels 2-16: pitch bend (48 semitones, relative to the touched cell), channel
pressure and CC74 from the height inside the string row (Apple Pencil: real pressure), note-on after the
expression, note-off on lift. A dropped connection releases all held notes.

## Notes

- Same grid as the app: 6 strings tuned in fourths, 13 frets, base note E2.
- Flags: `--port N`, `--html FILE`, `--log` (print every MIDI message), `--loopback` (self-test counter).
- This tests touch input only; it is not the iOS build of the app. Installing that needs a signing
  certificate and the iPad's trust check (see CLAUDE.md / project notes).
