# stacksampler

minimal vst3 sample layering tool.

## what it does

- opens with three sample layers ready to use
- accepts wav, aiff, and flac files by drag and drop
- grows to 32 layers without opening more sampler instances
- gives every layer its own waveform, mute, solo, remove, and sound controls
- includes clap, snare, hi-hat, perc, texture, vocal, and 808 quick modes
- adds subtle random and humanized variation for faster natural stacks
- runs as a vst3 instrument or standalone app

## audio notes

midi note 60 plays every active layer at its original pitch. higher and lower
notes transpose the whole stack, with 16 voices available for overlapping hits.

stacksampler does not normalize samples or add a hidden limiter. project state
saves each sample path rather than embedding audio, so keep source files in
place when sharing a session.

## requirements

- macOS 11 or newer
- Apple Silicon or Intel Mac
- a vst3 host for the plugin build
- cmake 3.22+ and an Apple C++17 toolchain when building from source

## quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## package

```bash
bash scripts/package_release.sh
open dist
```

The release script builds and tests universal Apple Silicon and Intel
binaries, ad-hoc signs them, and writes
`dist/StackSampler-macOS-universal.zip` plus `dist/SHA256SUMS`.

See `packaging/INSTALL.txt` for beta installation and Gatekeeper notes.

## license

AGPL-3.0. This open-source beta uses JUCE under its AGPLv3/commercial
dual-license terms. A proprietary distribution requires an appropriate JUCE
commercial license.

The complete corresponding source for v0.1.0 is available at
https://github.com/figtracer/stacksampler/tree/v0.1.0. Third-party attribution
and licence details are in `THIRD_PARTY_NOTICES.md`.
