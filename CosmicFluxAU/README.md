# CosmicFlux AUv3 (iOS)

An AUv3 audio effect (`aufx`) version of the CosmicFlux Nebulae instrument, for
use inside AUM and other AUv3 hosts on iPhone and iPad. The DSP is a C++ port of
`Instruments/CosmicFlux/cosmicflux.instr`; nothing here depends on Csound at
runtime, and the Nebulae `.instr` is untouched.

## Status: what has and has not been verified

Built and tested on Linux (g++ 13, CMake):

- `dsp/` - the portable C++ core.
- `tools/cosmicflux_render` - offline renderer that mirrors the Csound dev harness.
- `tools/cosmicflux_tests` - automated checks: bit-identical output for any host
  buffer size (1 to 4096 frames, mixed sizes), zero heap allocations inside
  `process()`, 44.1 kHz and 48 kHz operation, 60 s freeze stability, silence.
- `tools/compare_with_csound.py` - renders the dev scenarios through the C++
  core and compares them with the Csound renders (see table below).

Not compiled or run anywhere (requires Xcode on macOS, an iPhone and Apple
signing; none of that exists in the Linux environment this was written in):

- `Extension/` - the `AUAudioUnit` subclass (Objective-C++) and the Swift view
  controller.
- `App/` - the container app.
- `project.yml` - the XcodeGen spec.

The AUv3 glue follows Apple's Audio Unit extension template closely, but expect
to fix small compile or signing issues the first time it is opened in Xcode.

## Why no framework

The plug-in is written directly against Apple's AUv3 API with the DSP in plain
C++. JUCE (GPLv3 or paid licence) and iPlug2 (MIT) both support iOS AUv3, but
they add a large dependency that could not be validated here, and a single
AUv3 target on one platform does not need an abstraction layer. Keeping it to
Apple's framework plus ~700 lines of C++ keeps licensing simple (the repository
is MIT) and makes the project easy to generate with XcodeGen.

## Layout

```
CosmicFluxAU/
  project.yml                  XcodeGen spec: app + AUv3 extension
  CMakeLists.txt               builds the C++ core and offline tools on any OS
  dsp/CosmicFluxParams.*       parameter ids, names, Nebulae channel mapping
  dsp/CosmicFluxDSP.*          the effect (port of cosmicflux.instr)
  tools/render_offline.cpp     cosmicflux_render CLI
  tools/cosmicflux_tests.cpp   automated checks
  tools/compare_with_csound.py Csound vs C++ comparison
  Extension/                   AUv3 app extension (aufx, stereo in/out)
  App/                         minimal SwiftUI container app
```

## DSP parity with the Csound instrument

The port reproduces the Csound orchestra rather than re-tuning it:

- Every opcode used by the instrument is reimplemented with the same formulas:
  `portk` (half-time smoothing), `tone`/`atone`, `dcblock2`, `follow2`,
  `flanger`, `alpass`, `phaser2` (mode 1, harmonic notch spacing),
  `deltap`/`deltap3`, `lfo` (sine and triangle), `oscili` phase offsets,
  `clip` method 2. Coefficients were checked against Csound 6.18 micro-tests
  (impulse responses and step responses).
- Control-rate behaviour is kept on a fixed 64-sample grid (ksmps), independent
  of the host buffer size. This is what makes the output bit-identical for any
  frame count.
- Csound evaluates a block at a time, so the feedback signal written into the
  Flux Chain and into the Cosmos delay lines is one block (64 samples) old.
  That extra loop latency is reproduced on purpose, otherwise the Cosmos
  tuning and the Flux Chain repeat times would differ from the renders.
- Deviations: the limiter detector uses the peak of the previous 64 samples
  instead of the first sample of the current block (Csound's `k(abs(a))`), and
  the smoothers start at their targets when the plug-in loads instead of
  sweeping up from zero (the offline renderer has `--csound-init` to mimic
  Csound exactly for comparisons). The Tap parameter is wired like the
  Nebulae Reset button: in v0 of the instrument it measures the interval but
  does not yet drive the delay time, so it has no audible effect.

### Comparison with the Csound renders

`compare_with_csound.py` on Michael's test file (48 kHz), C++ core with
`--csound-init` against the renders in `Instruments/CosmicFlux/dev/examples`
and fresh Csound renders for the other scenarios:

| scenario | peak Csound | peak C++ | RMS Csound | RMS C++ | envelope corr. | centroid Csound / C++ | residual |
|---|---|---|---|---|---|---|---|
| michael_subtle | -2.8 dBFS | -2.8 dBFS | -23.3 dBFS | -23.4 dBFS | 1.000 | 2638 / 2638 Hz | -34.0 dB |
| michael_rhythmic | -6.1 | -6.0 | -26.1 | -26.1 | 1.000 | 2646 / 2648 | -17.2 dB |
| michael_freeze | -6.5 | -6.6 | -30.3 | -30.3 | 1.000 | 2621 / 2641 | -4.7 dB |
| 01_defaults | -1.5 | -1.6 | -19.4 | -19.4 | 1.000 | 2596 / 2596 | -29.7 dB |
| 05_phaser_on | -4.7 | -4.8 | -22.9 | -22.9 | 1.000 | 2660 / 2663 | -18.1 dB |
| 07_mode_sweep | -6.1 | -6.2 | -26.1 | -26.1 | 1.000 | 2592 / 2584 | -14.3 dB |

"Residual" is the level of (C++ minus Csound) relative to the Csound render; -34
dB is a near sample-exact match. The freeze scenario's residual is higher
because a small difference in exactly which 64-sample block gets captured into
the loop shifts the frozen waveform in time; level, envelope and spectrum match.

Freeze at 100 % mix, frozen at 3 s, measured from 12 s to 65 s: Csound varies
by 0.81 dB, the C++ core by 1.16 dB. Both hold.

Speed: 60 s of stereo audio renders in 1.9 s on one core of the Linux VM used
here (about 32x real time, so roughly 3 % of a core), which leaves plenty of
headroom on any iPhone that runs AUM.

## Parameters exposed to the host

All continuous parameters are 0..1 (shown as percent). Switches are booleans.
AUM can MIDI-map and automate every one of them.

| AU parameter | Nebulae control | Notes |
|---|---|---|
| Time | Start | Flux Chain delay, 10 ms to 1.2 s |
| Feedback | Size | Flux Chain feedback |
| Dimension | Overlap | per-stage smear |
| Multiply | Speed | number of taps, 1 to 6 |
| Early Mod, Late Mod | Start alt, Overlap alt | Flux Chain modulation |
| Feedback Filter | Size alt | dark / flat / bright tilt |
| Flanger Depth, Flanger Mode, Flanger Neg FB | Blend alt, File alt, Freeze alt | dynamic flanger |
| Phaser, Phaser Sync | Record, Record alt | barberpole phaser |
| Cosmos Delay, Density, Feedback, Warp, Mod, Mode | Pitch, Density, Window, Density alt, Window alt, Pitch alt | 8-line FDN |
| Freeze | Freeze | infinite hold (input to Cosmos muted) |
| Half Speed, Dotted Eighth | File, Reset alt | time multipliers |
| Mix | Blend | equal-power dry/wet |
| Mute Input | Source | |
| Tap | Reset | momentary, no audible effect in v0 |

Factory presets: Default, Subtle Smear, Rhythmic Multi-Tap, Cosmic Freeze (the
three Michael scenarios). State save/restore is implemented through
`fullState`, so AUM sessions and user presets keep all settings.

## Building and testing the C++ core (Linux or macOS)

```
cd CosmicFluxAU
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cosmicflux_tests
./build/cosmicflux_render --in input.wav --out out.wav --set blend=0.6 --set record=1
python3 tools/compare_with_csound.py --render-bin build/cosmicflux_render   # needs csound + the dev test file
```

`cosmicflux_render` accepts the Nebulae channel names used by the dev scenarios
(`--set start=0.3`, `--gate freeze=3.0`, `--ramp pitch=0,1`), honours the input
file's sample rate and pads with silence to `--duration`.

## Building and installing on an iPhone

What you need: a Mac with Xcode 15 or newer, an iPhone on iOS 15 or newer with a
USB cable, an Apple ID signed into Xcode (a free account is enough to install on
your own phone; builds expire after 7 days and must be re-run from Xcode; a paid
developer account allows TestFlight and 1-year profiles), AUM installed on the
phone, and [XcodeGen](https://github.com/yonaskolb/XcodeGen)
(`brew install xcodegen`).

1. Clone the fork and check out this branch, then generate the project:

   ```
   cd QB_Nebulae_V2/CosmicFluxAU
   xcodegen generate
   open CosmicFluxAU.xcodeproj
   ```

2. In Xcode select the project in the navigator, then for each of the two
   targets (`CosmicFluxHost` and `CosmicFluxAU`) open Signing & Capabilities,
   tick "Automatically manage signing" and pick your Team. If Xcode reports that
   a bundle identifier is taken, change `com.pmsorhaindo` in `project.yml` to
   your own prefix (keep the extension id prefixed by the app id), run
   `xcodegen generate` again and reopen.

3. Plug in the iPhone, unlock it and tap "Trust" if asked. On iOS 16 and later
   enable Developer Mode (Settings > Privacy & Security > Developer Mode, then
   restart the phone). In Xcode choose the `CosmicFluxHost` scheme and your
   iPhone as the run destination (top toolbar).

4. Press Run (Cmd-R). The first build is a few minutes. With a free Apple ID the
   first launch fails with an "untrusted developer" message: on the phone go to
   Settings > General > VPN & Device Management, tap your Apple ID under
   Developer App and tap Trust, then run again (or just tap the app icon).

5. Open the CosmicFlux app once. That is all it does; it registers the Audio
   Unit extension with iOS.

6. In AUM: tap "+" to add a channel, pick an input (Audio Input for a mic or
   interface, or a File Player), then tap the "+" below it in the Effects slot,
   choose "Audio Unit Effects" and pick "Sorhaindo: CosmicFlux". Tap the node to
   open the plug-in UI, or use AUM's own parameter view. MIDI mapping and
   automation work through AUM's standard parameter mapping (long-press a
   control in AUM's parameter view, or use the MIDI Control page). Saving the
   AUM session stores the plug-in state.

7. Optional, TestFlight (paid account): Product > Archive, Distribute App >
   TestFlight, then install from the TestFlight app on the phone.

If you do not want XcodeGen: in Xcode create a new iOS App named
`CosmicFluxHost`, add a target with the "Audio Unit Extension" template
(type Effect, manufacturer `PMSo`, subtype `Cflx`), delete the template's
generated DSP/UI files, add the files from `Extension/` and `dsp/` to the
extension target, set the bridging header to
`Extension/CosmicFluxAU-Bridging-Header.h`, and replace the extension's
`Info.plist` with `Extension/Info.plist`.

## Troubleshooting

- The plug-in does not appear in AUM: launch the CosmicFlux app once, then
  fully quit and reopen AUM. If it still does not show, delete the app from the
  phone, reboot, and install again. AUM lists it under Audio Unit Effects.
- Build error about ObjC pointers in a C++ struct: make sure the file is
  compiled as Objective-C++ (`.mm`) and that the extension target has
  "Objective-C Automatic Reference Counting" on (the default).
- Signing error "no profiles": pick a Team for both targets; for the free
  account also make the bundle identifiers unique.
- Silence: check the channel's input in AUM is live and the Mix parameter is
  above 0 %. Freeze mutes the input to Cosmos but the dry path still passes.
- Clicks when changing Multiply or Cosmos Mode: these are stepped controls in
  the instrument itself (the Nebulae behaves the same way).

## Known gaps

- Tap tempo is a momentary parameter but, as in v0 of the `.instr`, the measured
  interval is not yet applied to the delay time.
- No custom artwork or icon; the UI is a plain list of sliders and switches.
- The limiter and smoother start-up behaviour differ slightly from Csound (see
  above); audibly identical in the comparisons, but noted for completeness.
