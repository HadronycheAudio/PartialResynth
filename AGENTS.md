# AGENTS.md

Guidance for coding agents working in this repository. Read [Rules that protect users' work](#rules-that-protect-users-work) before changing anything.

PartialResynth is a VST® 3 effect for Windows that finds the five loudest partials of its input, using a port of `fiddle~` from Pure Data, and plays each with a synth voice. It uses the Steinberg VST3 SDK and VSTGUI directly. JUCE was deliberately rejected, so don't introduce it.

`docs/signal-path.md` is the behaviour specification. `README.md` covers building and usage.

Code shared with the other Hadronyche plug-ins is in the `HadronycheCore` submodule, at `external/hadronychecore`: the parameters' curves and saved format, `PanelEditor`, `Smoother`, the test host and the build setup. Its `AGENTS.md` has its own rules.

## Commands

Windows x64 with Visual Studio 2022 and CMake 3.25 or newer is where this is developed. macOS is built and tested by CI, on Intel and Apple Silicon, so see [macOS](#macos).

```powershell
# Clone. Without long paths, cloning into a folder deeper than about 100 characters fails partway
# through the SDK, because some of its sample files exceed the Windows path limit.
git -c core.longpaths=true clone --recurse-submodules <repo-url>

# In an existing clone, if submodules are missing
git -c core.longpaths=true submodule update --init --recursive

# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build. This also runs the Steinberg validator, and a failure fails the build.
cmake --build build --config Release --target PartialResynth
cmake --build build --config Debug --target PartialResynth

# Run the validator on its own, for example with extra output
build/bin/Release/validator.exe build/VST3/Release/PartialResynth.vst3

# Build and run all the tests
cmake --build build --config Release --target PartialResynthTests
ctest --test-dir build -C Release --output-on-failure -j 4

# Run a single test, and list them
ctest --test-dir build -C Release --output-on-failure -R "^fiddle-match$"
ctest --test-dir build -C Release -N

# Install for testing in Cubase. Close Cubase first, and use an elevated shell.
Copy-Item -Recurse -Force build/VST3/Release/PartialResynth.vst3 "C:\Program Files\Common Files\VST3\"
```

There's no linter or formatter.

Follow the existing style, which comes from the SDK: a space before call parentheses (`foo (x)`), `_member` for private members, `kName` for constants, and comments that explain why rather than what.

## Rules that protect users' work

People use this plug-in in real projects. Breaking these rules damages saved projects, and usually nothing fails at build time.

### Saved projects

- **Never change the Audio Unit codes,** `aufx` `PrRs` here and the manufacturer `Hdrn` in HadronycheCore. Hosts use them to find the plug-in in saved projects.
- **Never change the class IDs in `src/PluginConfig.h`.** Hosts use them to find the plug-in in saved projects.
- **Never renumber, reorder or remove entries in `ParamId`** (`src/Parameters.h`). Their values are saved in projects and automation. Add new parameters just before `kParamCount`, and add the matching row to `kSpecs` in `src/Parameters.cpp`, which is listed in the same order.
- **Don't change the range or curve of an existing parameter.** Hosts store values normalized to 0..1, so this silently changes what saved projects sound like. If it's unavoidable, migrate old values and bump the state version.
- **Only ever add to the end of `kWaves` and `kFilterModes`,** and keep `Oscillator::Wave` and `StateVariableFilter::Mode` in step with them. Projects save a position in the list.
- **Changing the saved format requires bumping `kStateVersion` and still reading every older version.** The format is `int32` version, `int32` count, then `int32` ID and `double` normalized value pairs (`writeParamState`). A state without a parameter leaves it at its default, and an unknown one is skipped.
- **Loading must tolerate missing or damaged data.**

### The analysis

- **`FiddleAnalyzer` is a port, not a design.** It keeps the original's arithmetic, names and even its rounded constants, so that `fiddle-match` can hold it to the original. Don't "improve" or tidy its maths. A deliberate change of behaviour belongs in `PartialTracker` or `Processor`, or in the short list of differences at the top of `FiddleAnalyzer.h`.
- **Don't edit `tests/reference/fiddle~.c`.** It's Pure Data's file, byte for byte. Only `m_pd.h` next to it is ours.
- **The port must keep the original's copyright notice,** and `licenses/pure-data.txt` must accompany anything distributed.

### The audio thread

`Processor::process` and everything it calls runs on the audio thread, including `src/dsp/`, except `FiddleAnalyzer::prepare` and `Voice::prepare`.

- **No allocation, locks, file access, system calls or anything else that can block.** This includes `std::vector` growth, `std::string` and `std::function` copies. The failure is audio dropouts that may not show up in testing.

### Sound and timing

- **`getLatencySamples` must stay in step with what actually happens.** It's half the window plus the hop. The dry delay uses the same value, and `latency-alignment` measures both.
- **A voice's ramps last exactly as long as the gap between frames.** `PartialTracker` relies on this: a voice given zero is exactly silent by the next frame, and that's when it can take on a new partial. Changing one means changing the other.
- **Only a silent voice changes partial.** Re-pitching one that can be heard is the jump that following partials exists to remove.
- **The output doesn't depend on the block size,** even across sleeping, which is why `skipSilence` keeps the analyzers' place and `block-sizes` checks for exact equality.
- **`Hadronyche::Smoother` must stay double precision.** In single precision it stalls just short of its target.

### Decisions made by the project owner

These were chosen deliberately, so don't "fix" them without the owner's agreement:

- Five partials, each with one voice, and one set of controls for all of them.
- Two oscillators per voice with sine, saw and square, then a filter. The only envelope is the one from the analysis: there's no ADSR.
- It takes the five loudest peaks, not the five lowest that `fiddle~` outputs.
- A voice follows its partial, rather than playing a position in `fiddle~`'s list.
- A window of 2048 with a hop of 512, not `fiddle~`'s own hop of half a window, for less latency.
- Key follow, envelope to cutoff, Spread and Glide are all wanted, as additions to the original brief.

In `docs/signal-path.md`, **Requirement** marks the owner's decisions and **Proposed** marks suggestions that were accepted by default. Update it when behaviour changes, and don't turn a Proposed item into a Requirement yourself.

### The repository

- **Don't edit anything in `external/vst3sdk`.** It's a submodule pinned to `v3.8.1_build_84`. `git submodule status` shows a misleading `v3.7.3...` label for that same commit, so check with `git -C external/vst3sdk describe --tags`.
- **Don't edit `external/hadronychecore` from here.** Change it in its own repository, and move the submodule here afterwards, once the new version has been compared with the old as its `AGENTS.md` describes.
- Don't change git configuration. Commit or push only when asked.

## Architecture

### Signal path

`Processor::render` runs this for every sample:

```
             ┌─→ Dry delay ───────────────────────────────────────────────┐
In (stereo) ─┤                                                            ├→ Dry/wet → Volume → Out
             └─→ Mono → 2 × fiddle~ → Tracker → 5 voices → Spread ────────┘

Voice:  Osc 1 ─┐
                ├→ Filter → × level from the analysis
        Osc 2 ─┘
```

### Threads

| Thread | Runs | Talks to the others through |
| --- | --- | --- |
| Audio | `Processor::process` | Atomics only |
| UI | `Editor`, on a 33 ms timer | The controller's parameters |
| Host | `setState`, `getState`, `setActive` | Atomics |

The processor and controller share no memory, so the plug-in is marked as distributable. `getParamSpec` is the one definition of every parameter's range and text, shared by `Processor`, `Controller` and `Editor`. The last parameter value per block is used, followed by smoothing.

### The analysis

- **Two copies of `FiddleAnalyzer`** run half a hop apart. Each does exactly what the original does, every half window, and together they give a frame every quarter window.
- **The window scales with the sample rate:** 2048 up to 64 kHz, 4096 up to 128 kHz, then 8192. This keeps its length in time.
- **Threshold** is converted to the original's power scale with `getFullScalePower`, which depends on the window.

### The voices

- **`PartialTracker`** chooses the loudest peaks, and gives a peak that's already being played a 3 dB advantage so that two similar peaks don't take turns. It matches them to the voices by closest pitch, up to a semitone per frame.
- **Control rate:** pitch, filter coefficients, tuning and panning update every 16 samples. Only the level and the audio itself run per sample.
- **Sleep:** with a silent input for longer than the buffers, and all the voices silent, the analyzers stop and the output is flagged as silent. Parameter changes then apply without a ramp.

### The editor

`Editor.cpp` builds the panel in `buildPanel`, on `Hadronyche::PanelEditor`, with no `.uidesc` file. A 33 ms timer in `PanelEditor` pushes parameter values to the controls.

- A control's tag is its `ParamId`.

## Verifying a change

Before calling a change done:

1. **Build Debug and Release, and run the tests on both.** The validator runs as part of the build, but it only proves that the plug-in follows the VST3 rules.
2. **Add a test for any new behaviour,** and check that it fails without your change.
3. **Test it in Cubase** if you can, or say clearly that you couldn't. The tests measure the output, and nothing about how it sounds. Don't report behaviour you haven't observed as working.

### The tests

`.github/workflows/ci.yml` builds Debug and Release and runs all of this on every push and pull request. Don't weaken or skip it to get a change through.

`tests/` builds one program, `PartialResynthTests`, which loads the built plug-in and drives it through the VST3 interfaces, the way a host does. `FiddleTests.cpp` is the exception: it compiles `FiddleAnalyzer.cpp` and `PartialTracker.h` in directly, because a host can't see what they produce.

| File | Covers |
| --- | --- |
| `CompatibilityTests.cpp` | Class IDs, parameter IDs and ranges, the saved format, the buses and the latency |
| `FiddleTests.cpp` | The port against the original `fiddle~`, frame by frame, and the tracker |
| `ResynthTests.cpp` | The whole path: partials in, voices out, and the timing |
| `SynthTests.cpp` | The oscillators, filter, glide, spread, dry/wet, volume, state and parameter text |
| `PanelTest.cpp` | The real editor, driven with mouse messages |

- **Never change an expected value in `CompatibilityTests.cpp` to make a test pass.** Those values are written out by hand on purpose, and a failure there means the change would damage saved projects. Adding new parameters to them is fine, and expected.
- **Don't loosen a limit elsewhere to get a test passing** without working out why it failed. The limits in `fiddle-match` are close to what the port achieves on purpose: looser ones missed a 1% error in a filter coefficient on most of the signals.
- **To add a test,** add it to `Tests.h`, `kTests` in `TestMain.cpp` and `PARTIALRESYNTH_TESTS` in `tests/CMakeLists.txt`.
- **`PanelTest.cpp` clicks fixed positions,** so moving the panel's controls means updating the positions at the top of that file. It can't open the menus, whose pop-ups would block it.
- **To see the panel,** set `PARTIALRESYNTH_SCREENSHOTS` to a folder before running the `panel` test, which saves a BMP at each step.
- **To measure CPU use,** run `build/bin/Release/PartialResynthTests.exe <plug-in DLL> cpu`, in a Release build. The DLL is at `build/VST3/Release/PartialResynth.vst3/Contents/x86_64-win/PartialResynth.vst3`.

## Releasing

Pushing a tag `v<version>` publishes a release, once everything has passed:

1. Set the version in `project()` in `CMakeLists.txt`. CI fails if the tag doesn't match it.
2. Add `docs/release-notes/v<version>.md`, which becomes the release's text.
3. Commit, push, and wait for CI to pass.
4. Tag that commit and push the tag.

CI builds the installer on every push, installs it on its own machine, runs the validator and all the tests on the installed copy, and uninstalls it. `installer/test-installer.ps1` writes to Program Files, so don't run it on a machine you care about.

Rules for releases:

- **Don't change `AppId` in `installer/PartialResynth.iss`.** It's how an upgrade finds the previous version.
- **Keep `SMTG_USE_STATIC_CRT` on,** which HadronycheCore's `cmake/HadronychePlugin.cmake` does. Without it, the plug-in fails to load on PCs without Microsoft's runtime, and neither the tests nor CI would notice, because that runtime is on those machines. If you configured a build before this option existed, its cached value overrides the default.
- **The installer must keep shipping the licence files.** The VST 3 SDK's, VSTGUI's, HadronycheCore's and Pure Data's licences require their notices to accompany binaries.
- **Follow Steinberg's usage guidelines** (`external/vst3sdk/VST3_Usage_Guidelines.pdf`) wherever "VST" appears in product-related material: use ® on first mention, include the sentence "VST is a registered trademark of Steinberg Media Technologies GmbH.", show the logo from `docs/images/` unaltered, and don't put "VST" in product or company names.

## macOS

- **Build with the Xcode generator,** which the SDK's Audio Unit wrapper needs: `cmake -S . -B build -G Xcode`, then `cmake --build build --config Release --target PartialResynthTests PartialResynth PartialResynth-au`.
- **`CMAKE_OSX_DEPLOYMENT_TARGET` has to stay before `project ()`,** or the plug-in only loads on macOS as new as the build machine, and only an older Mac would notice. `test-installer.sh` checks for this, because a host on the build machine wouldn't.
- **The SDK's `-ffast-math` stays off,** as HadronycheCore's AGENTS.md explains.
- **The installer is built and tested by HadronycheCore's `tools/macos/`.** `test-installer.sh` writes to `/Library`, so it's meant for CI machines.
- **The panel test only exists on Windows,** because it uses Windows' mouse messages. Nothing automated opens the panel on macOS, so its look needs checking by eye.

## Linux

- **Use one build folder per configuration, with Ninja:** `cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build/Release --target PartialResynthTests PartialResynth`. The SDK's Linux output paths break with a multi-configuration generator.
- **`external/hadronychecore/tools/linux/install-build-packages.sh` installs the build's packages.** gtkmm and SQLite are only needed to configure, and the plug-in doesn't link them.
- **`panel-opens` needs an X server,** so run `xvfb-run -a ctest ...` where there's no display. It opens and closes the real editor, and nothing more: `panel`, which drives it, only exists on Windows, so the panel's look and its controls need checking by eye.
- **The release is built in an `ubuntu:22.04` container on CI, not on the runner,** because a binary only loads on a glibc as new as the one it's built against. HadronycheCore's `tools/linux/test-tarball.sh` checks this, and CI runs the tarball on other distributions too.
- **The LV2 is HadronycheCore's wrapper around the VST3's own classes.** Its `AGENTS.md` has the rules. It has audio, control ports, state, latency, transport and the VST3's panel. `lv2-matches-vst3` compares it with the VST3, bit for bit.
- **Never change the URI, the panel's URI and the symbols in `src/lv2/PluginInfo.cpp`.** They've been released, and hosts use them to find the plug-in and its values in saved projects. A new parameter adds a symbol at the end.

## Known limits

- It has only been used on Windows, and other hosts than the owner's haven't been tried.
- The installer isn't code-signed, so Windows warns users when they run it. The macOS one isn't signed by Apple or notarized, so macOS refuses it until the user allows it.
- The Linux VST 3 and the LV2 have only been tried briefly, in REAPER on Debian 13, by the owner.
- The test that opens the panel used to crash rarely on CI, as the test program unloaded the plug-in. Pango's font thread was still running in it. Plug-ins now link with `-z nodelete`, as HadronycheCore's `AGENTS.md` explains, which removes that cause. The crash was too rare for reruns to prove it gone.
- The Mac version hasn't been used in a real host on a real Mac. The tests measure its output, and Apple's `auval` validates it, on CI only.
- Partials closer than about 86 Hz merge at 44.1 and 48 kHz, which is the width of the window's main lobe. Below about 90 Hz, adjacent harmonics can't be told apart.
- There's no limiter. Two oscillators at full level, particularly saw or square, can go over full scale.
- On noisy or dense input, peaks come and go between frames, which can make voices flutter. This comes from the analysis.
