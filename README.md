# PartialResynth

<img src="docs/images/VST_Compatible_Logo_Steinberg_with_TM.png" alt="VST Compatible" width="110">

A VST® 3 effect by Hadronyche that listens to its input, finds the five loudest partials, and plays each of them with its own synth voice. The analysis is a port of `fiddle~` from Pure Data. It's built directly on the Steinberg VST 3 SDK and VSTGUI.

`docs/signal-path.md` describes the design and how each module behaves.

## Download

**On Windows,** get the installer from the [latest release](https://github.com/HadronycheAudio/PartialResynth/releases/latest). It needs 64-bit Windows 10 or later, and installs to `C:\Program Files\Common Files\VST3`. The installer isn't code-signed, so Windows will probably warn you: choose **More info**, then **Run anyway**.

**On a Mac,** get `PartialResynth-<version>-macOS.pkg` from the same page. It's a VST 3 and an Audio Unit, for Intel and Apple Silicon, on macOS 10.15 or later. It installs to `/Library/Audio/Plug-Ins`. The installer isn't signed by Apple, so macOS will refuse it the first time: the release notes explain how to open it. See [macOS: untested](#macos-untested) first.

**On Linux,** get `PartialResynth-<version>-Linux-x86_64.tar.gz` from the same page. It's a VST 3 for x86_64, and it needs glibc 2.34 or later, as on Ubuntu 22.04, Debian 12 or newer. Extract it where hosts look:

```bash
mkdir -p ~/.vst3
tar -xzf PartialResynth-<version>-Linux-x86_64.tar.gz -C ~/.vst3
```

It needs a few common libraries, which you probably have. On Debian or Ubuntu:

```bash
sudo apt install libxcb-cursor0 libxcb-util1 libxkbcommon-x11-0 libcairo2 libpango-1.0-0 libpangocairo-1.0-0
```

There's also an LV2 version, in `PartialResynth-<version>-Linux-x86_64-LV2.tar.gz`, with the same panel. Extract it into `~/.lv2`. See [Linux: lightly tested](#linux-lightly-tested) first.

## macOS: untested

> [!WARNING]
> **The Mac versions are untested.** They pass automated tests and Apple's Audio Unit validation on build machines, on both Intel and Apple Silicon, but nobody has used them in a real host on a real Mac yet. Nothing has checked how they sound, or how the panel looks.

**Are you a Mac user who's ready to commit to testing? You're more than welcome!** Please [open an issue](https://github.com/HadronycheAudio/PartialResynth/issues/new) saying which Mac, macOS version and host you use.

To build it yourself, you need Xcode, CMake 3.25 or newer, [Rust](https://rustup.rs) with both Mac targets (`rustup target add aarch64-apple-darwin x86_64-apple-darwin`), and an internet connection the first time, which fetches Apple's AudioUnitSDK:

```bash
git clone --recurse-submodules https://github.com/HadronycheAudio/PartialResynth.git
cd PartialResynth
cmake -S . -B build -G Xcode
cmake --build build --config Release --target PartialResynth PartialResynth-au
```

This produces `build/VST3/Release/PartialResynth.vst3` and `build/VST3/Release/PartialResynth.component`, for Intel and Apple Silicon, and runs the Steinberg validator on the plug-in. The build also copies the Audio Unit to `~/Library/Audio/Plug-Ins/Components`, where it keeps pointing at your build folder. To use the VST 3, copy it to `~/Library/Audio/Plug-Ins/VST3`.

To run the tests, which leave out the Windows-only panel test:

```bash
cmake --build build --config Release --target PartialResynthTests
ctest --test-dir build -C Release --output-on-failure
```

## Linux: lightly tested

> [!NOTE]
> **The Linux version is new.** It passes the automated tests on Ubuntu 22.04 and 24.04 and Debian 12 and 13, and it has been tried briefly by hand in REAPER on Debian 13. Other hosts and distributions haven't been tried. Please [open an issue](https://github.com/HadronycheAudio/PartialResynth/issues/new) saying which distribution and host you use if something goes wrong.

To build it yourself, you need CMake 3.25 or newer, [Rust](https://rustup.rs), and the packages that this script installs on Debian or Ubuntu:

```bash
git clone --recurse-submodules https://github.com/HadronycheAudio/PartialResynth.git
cd PartialResynth
bash external/hadronychecore/tools/linux/install-build-packages.sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --target PartialResynth
```

This produces `build/Release/VST3/Release/PartialResynth.vst3`, and runs the Steinberg validator on the plug-in. To use it, copy it to `~/.vst3`.

To build the LV2 too, install `lv2-dev`, and build the target `PartialResynth-lv2`. This produces `build/Release/LV2/PartialResynth.lv2`, and to use it, copy it to `~/.lv2`. The tests that compare it with the VST 3 also need the target `PartialResynthLv2Compare`.

To run the tests, which include one that opens the panel and so needs a display or `xvfb-run`:

```bash
cmake --build build/Release --target PartialResynthTests
xvfb-run -a ctest --test-dir build/Release --output-on-failure
```

## Using it

Each voice follows one partial, taking its frequency and its level. There are no envelopes to set: the analysis is the envelope. All the controls apply to all five voices.

**Analysis**

- Threshold is in dB below a full-scale sine, measured on the input's total power. Below it, the voices fade out.
- Glide slows the voices' pitch down as they follow their partials. At 0, they track as tightly as the analysis allows.

**Oscillator 1 and 2**

- Wave is sine, saw or square.
- Level sets how much of it goes to the filter. Both at 100% can be up to 6 dB louder than the input, and there's no limiter.
- Coarse and Fine tune it relative to the partial.

**Filter**

- Mode is low-pass, high-pass, band-pass or notch.
- Cutoff and Resonance work as you'd expect, and each voice has its own filter.
- Key follow moves the cutoff with the voice's frequency. At 100%, it keeps the same distance from the partial as 440 Hz does from Cutoff.
- Env amount moves the cutoff with the voice's level, by up to 8 octaves either way. It's measured in terms of level, so a quiet partial still moves it, from nothing at -60 dB to the whole amount at full scale.

**Output**

- Spread places the voices across the stereo field. Which partial goes where depends on which voice it found free.
- Dry/wet at 50% plays both at full level.
- Volume is last.

The plug-in has 1536 samples of latency at 44.1 and 48 kHz (35 and 32 ms), and it reports this to the host. The dry signal is delayed to match.

## Requirements for building

- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.25 or newer
- [Rust](https://rustup.rs), which builds the signal processing in `rust/` into the plug-in
- Git

## Getting the code

The SDK and HadronycheCore, the code shared by the Hadronyche plug-ins, are pinned git submodules in `external/`:

```powershell
git -c core.longpaths=true clone --recurse-submodules <repo-url>
# or, in an existing clone:
git -c core.longpaths=true submodule update --init --recursive
```

Long paths are needed if you clone into a deep folder, because some of the SDK's sample files would otherwise exceed the Windows path limit.

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PartialResynth
```

This produces `build/VST3/Release/PartialResynth.vst3`. Each build also runs the Steinberg validator on the plug-in, and a validation failure fails the build.

To open the project in the Visual Studio IDE, use `build/PartialResynth.sln`.

## Testing

```powershell
cmake --build build --config Release --target PartialResynthTests
ctest --test-dir build -C Release --output-on-failure -j 4
```

The tests load the built plug-in and drive it like a host, including its panel. They also run Pure Data's original `fiddle~` source alongside the port, and compare every frame. `AGENTS.md` explains how they work.

## Installing your own build

Close your host, then run this from an elevated PowerShell in the project folder:

```powershell
Copy-Item -Recurse -Force build/VST3/Release/PartialResynth.vst3 "C:\Program Files\Common Files\VST3\"
```

## Project layout

| Path | Purpose |
| --- | --- |
| `src/PluginConfig.h` | Plug-in name, vendor details and class IDs |
| `src/PluginEntry.cpp` | VST3 module entry, registers the classes below |
| `rust/src/lib.rs` | The parameters, with their IDs, ranges and text, and the processor that runs the analysis, the voices and the dry path |
| `rust/src/fiddle_analyzer.rs` | The port of `fiddle~`'s peak analysis |
| `rust/src/partial_tracker.rs` | Chooses the partials, and keeps each with its voice |
| `rust/src/` | Also the voice, oscillator and filter |
| `rust/tests/fiddle.rs` | The port against the original `fiddle~`, frame by frame, and the tracker |
| `rust/reference-fiddle/` | Builds the original for those tests, and nothing else |
| `src/ParamIds.h` | The parameter IDs for the C++ side, generated from `rust/` |
| `src/Processor.*` | The VST3 audio component, which HadronycheCore's `RustProcessor` runs on the Rust side |
| `src/Controller.*` | Registers the parameters and creates the editor |
| `src/Editor.*` | The plug-in window |
| `tests/` | Tests that drive the built plug-in like a host |
| `tests/reference/` | Pure Data's original `fiddle~.c`, unchanged, for the Rust tests to compare with |
| `installer/` | The Windows installer. HadronycheCore's `tools/windows/` tests it, and its `tools/macos/` and `tools/linux/` build the macOS installer and the Linux tarballs |
| `docs/` | The behaviour specification and release notes |

## Contributing

PartialResynth is free software that follows its author's own workflow, and there's no company or support promise behind it. If you find it handy and want it to work better with your host, your distribution or your way of working, you're welcome to take it further. Please [open an issue](https://github.com/HadronycheAudio/PartialResynth/issues/new) or a pull request. The LV2 version is new, and help testing in other hosts is especially useful. `AGENTS.md` lists the rules that keep people's saved projects safe, so please read it before changing anything.

## Licence

PartialResynth is released under the MIT licence: see `LICENSE`.

It's built with the following, which keep their own licences:

- VST3 SDK: MIT (`external/vst3sdk/LICENSE.txt`)
- VSTGUI (`external/vst3sdk/vstgui4/LICENSE`)
- HadronycheCore: MIT (`external/hadronychecore/LICENSE`)
- Apple's AudioUnitSDK, on macOS only: Apache 2.0
- `fiddle~` from Pure Data, by Miller Puckette and Ted Apel, whose analysis is ported in `rust/src/fiddle_analyzer.rs`: Standard Improved BSD License (`licenses/pure-data.txt`)

VST is a registered trademark of Steinberg Media Technologies GmbH.
