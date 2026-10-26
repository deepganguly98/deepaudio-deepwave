<p align="center">
  <img src="https://github.com/{{REPO}}/raw/main/docs/banner.png" alt="DeepWave" width="100%">
</p>

<h3 align="center">An expressive string instrument for your fingers.<br>Physical-modelling strings, a multi-touch MPE grid, an arpeggiator and 40 presets.</h3>

## ⬇️ Download (Windows 10/11, 64-bit)

| | File | What you get |
|---|---|---|
| ✅ **Recommended** | [**{{SETUP_NAME}}**](https://github.com/{{REPO}}/releases/download/{{TAG}}/{{SETUP_NAME}}) | one-click installer: VST3 plugin + standalone app |
| 📦 Manual | [DeepWave-windows-x64.zip](https://github.com/{{REPO}}/releases/download/{{TAG}}/DeepWave-windows-x64.zip) | `DeepWave.vst3` folder + `DeepWave.exe` |

<p align="center">
  <img src="https://github.com/{{REPO}}/raw/main/docs/screenshots/deepwave-main.png" alt="DeepWave playing a chord with one finger sliding across a string" width="100%">
</p>

## 🚀 Install in 3 steps

1. **Run the installer** and keep both boxes ticked: *VST3 plugin* and *Standalone app*.
2. **FL Studio** (20.8 or newer, 64-bit): *Options → Manage plugins → Find installed plugins*, then *Add → DeepWave* from the instruments.
   Ableton Live, Reaper, Cubase, Bitwig and Studio One load it as a VST3 instrument as well.
3. **Standalone:** open **DeepWave** from the Start menu and pick your audio device under *Options → Audio/MIDI Settings*.
   Your computer keyboard plays notes too: `A W S E D F T G Y H U J K O L P`, with `Z` / `X` for the octave.

> **Windows says "Windows protected your PC"?** The installer is not code-signed yet. Click **More info → Run anyway**.
> To remove DeepWave later: *Settings → Apps → DeepWave → Uninstall*.

## ✨ Highlights

- 🎛️ **Multi-touch MPE grid:** every finger is its own note with its own bend, pressure and timbre. Slides are continuous.
- 🎻 **Hold & bow:** a resting finger keeps the string singing (E-bow / stick-slip bow), so you can glide and sustain, not just pluck.
- 🔁 **Arpeggiator that keeps your slides:** 7 patterns, 6 rates, 1-4 octaves, gate, swing, latch and host tempo sync.
- 🎼 **12 scales and any root**, hammer-on / pull-off, and auto-vibrato.
- 🔊 **40 presets** in 8 categories (Bowed, Plucked, Pads & Drones, Leads, Wind & Breath, Bass, Arp & Rhythm, Cinematic), loudness-matched.
- ✨ **Master polish:** plate reverb, ping-pong delay, shimmer, ensemble chorus, glue compressor, EQ, warmth and a transparent limiter.

<p align="center">
  <img src="https://github.com/{{REPO}}/raw/main/docs/screenshots/deepwave-arp.png" alt="The Crystal Arp preset with the arpeggiator switched on" width="100%">
</p>

## ✅ Checked automatically on a clean Windows machine

- Builds with Visual Studio; all unit tests pass (pitch accuracy within about a cent, stability, arpeggiator, effects, presets).
- **Silent install** puts the plugin in `C:\Program Files\Common Files\VST3\` and the app in `C:\Program Files\DeepAudio\DeepWave\`.
- The standalone app **starts and keeps running**.
- The installed VST3 passes **[pluginval](https://github.com/Tracktion/pluginval) at strictness 5**.
- **Uninstall** removes everything.

## ⚠️ Good to know

- This is an **early release (0.1.0)**. The checks above are automatic; **nobody has yet loaded it in FL Studio on a real PC and judged the sound**,
  and the preset balance was tuned by measurement. Please tell us what sounds off.
- **No VST2.** Steinberg withdrew the VST2 SDK; VST3 is what current hosts use.
- Not code-signed yet, hence the SmartScreen prompt.
- macOS and iPad builds are available from source, see the [README](https://github.com/{{REPO}}#build).

## 🔒 Checksums (SHA-256)

```
{{SHA_SETUP}}  {{SETUP_NAME}}
{{SHA_ZIP}}  DeepWave-windows-x64.zip
```

Questions or bugs? [Open an issue](https://github.com/{{REPO}}/issues).
