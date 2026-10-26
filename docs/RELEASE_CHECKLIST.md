# Release checklist


- [ ] App icon (iOS asset catalog + macOS icns) set on the target
- [ ] Bump version in CMakeLists.txt
- [ ] Code signing: Developer ID (Mac), team + provisioning (iPad)
- [ ] Hardened runtime, notarise (`xcrun notarytool submit`) and staple
- [ ] `tools/validate.sh` passes (build, auval, unit tests)
- [ ] GarageBand test on Mac (AU) and iPad (AUv3): load, play, save/reload project
- [ ] Check CPU with 15 voices on the oldest supported iPad
