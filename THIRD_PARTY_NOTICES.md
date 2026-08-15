# Third-party notices

## BlackHole virtual audio driver

The optional `M5 StopWatch Mic` Core Audio HAL driver is a customized build of
BlackHole by Existential Audio:

- Source: https://github.com/ExistentialAudio/BlackHole
- Pinned source commit: `ffcb74433fbcf8c8ca5c736677c1a4864384dc09`
- Upstream license: GNU General Public License v3.0
- Local build recipe: `tools/typeless_bridge/virtual_mic_driver/build_driver.sh`

The driver is an independently built and installed component. The StopWatch
firmware and macOS Bridge application remain under this repository's MIT
license. Distributors must preserve the GPLv3 license and provide the complete
corresponding BlackHole source for the driver build they distribute.
