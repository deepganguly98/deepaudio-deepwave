# Third-party notices

DeepWave's own source code is released under the MIT licence (see `LICENSE`). It is built on the following components.

## JUCE (important for the compiled downloads)
DeepWave is built with [JUCE](https://juce.com) 8. JUCE is available under the **GNU AGPLv3** or under a commercial
JUCE licence. Unless a commercial JUCE licence is held by whoever builds and distributes a binary, **the compiled
plugins, apps and installers that link JUCE are subject to the AGPLv3**. This is compatible with the MIT-licensed
source here, and the complete corresponding source is this repository. If you redistribute a build, you take on
JUCE's licence terms; see https://juce.com/legal/juce-8-licence/ for details.

## VST3
VST is a registered trademark of Steinberg Media Technologies GmbH. The VST3 interface code is supplied with JUCE.

## Build and test tooling (not shipped in the plugin)
- [pluginval](https://github.com/Tracktion/pluginval) (Tracktion), used only in CI to validate the plugin.
- [Inno Setup](https://jrsoftware.org/isinfo.php), used only to build the Windows installer.

## Inspiration
Digital-waveguide techniques follow the published literature (J. O. Smith, *Physical Audio Signal Processing*;
Jaffe and Smith, 1983; the Synthesis ToolKit; Faust `physmodels.lib`) and the MIDI MPE specification. No code from
proprietary instruments is used.
