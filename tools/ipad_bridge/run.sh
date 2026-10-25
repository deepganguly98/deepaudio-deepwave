#!/bin/zsh
# One command: start DeepWave (if needed), then the iPad bridge. Open the printed URL in Safari on the iPad.
cd "${0:A:h}"
[[ -x ipad_bridge && ipad_bridge -nt main.swift ]] || swiftc -O -suppress-warnings main.swift -o ipad_bridge || exit 1
# DeepWave first: the Standalone auto-enables MIDI devices that appear while it is running
pgrep -x DeepWave >/dev/null || { open /Applications/DeepWave.app; sleep 3; }
exec ./ipad_bridge --html index.html "$@"
