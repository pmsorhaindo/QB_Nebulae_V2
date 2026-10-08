# CosmicFlux Test Results

## Test Environment

- **Csound version**: 6.18.1 (double samples) 2024-04-07
- **Sample rate**: 48000 Hz
- **Block size**: 64 samples (ksmps)
- **Test date**: 2026-10-08
- **Platform**: Linux (Cloud Agent VM)

## Summary

**Overall**: 108 of 108 tests passed.

12 scenarios times 9 test signals (impulse, sines at 110/440/2000 Hz, pink
noise, drum loop, sawtooth chord, silence, and Michael's 10.7 s test file).

## Correction to the previous report

The previous version of this file said freeze could not hold within 2 dB and
"decays to zero". That was a harness error, not an instrument problem:

- `test_freeze_60s.json` had its settings under a `controls` key that
  `render.py` does not read, so the render ran with every pot at 0, including
  Mix. The output was 100% dry and simply stopped when the 10.7 s input ended.
- The freeze stability metric started at 5 s, while the input file was still
  playing and the dry path (Mix at 90%) was still audible, so it measured the
  input, not the frozen tail.

Both are fixed: the scenario uses `static` with Mix at 100%, and the metric
measures max/min of per-second RMS from 12 s (after the input has ended) to the
end of the render, with a 2 dB limit. The instrument's freeze path itself is
unchanged from the previous commit.

## Freeze stability (measured 12 s to end of render)

| Scenario | Signal | Window | Frozen RMS | max-min |
|----------|--------|--------|------------|---------|
| test_freeze_60s | michael_test | 12-65 s | -34.6 dBFS | 0.81 dB |
| test_freeze_60s | drum loop | 12-65 s | -30.7 dBFS | 0.73 dB |
| test_freeze_60s | pink noise | 12-65 s | -42.4 dBFS | 0.15 dB |
| test_freeze_60s | sawtooth chord | 12-65 s | -39.7 dBFS | 0.23 dB |
| test_freeze_60s | sine 440 Hz | 12-65 s | -26.4 dBFS | 0.70 dB |
| test_freeze_60s | sine 2000 Hz | 12-65 s | -26.8 dBFS | 1.32 dB |
| 04_freeze_mode | all 8 signals | 12-20 s | -24.8 to -74.7 dBFS | 0.06 to 0.93 dB |
| michael_freeze | all 8 signals | 12-20 s | -24.8 to -76.3 dBFS | 0.06 to 1.44 dB |

Every freeze render holds within the 2 dB target; the 60 s holds vary by at
most 1.33 dB. The frozen level depends on how much energy was in the Cosmos
network when Freeze engaged (impulse and short-sine inputs hold at a low level,
sustained material holds at -25 to -35 dBFS).

How freeze works in the instrument: input to the Cosmos network is muted,
feedback is exactly 1.0 with smoothing bypassed, modulation is forced to zero,
delay reads switch to integer-sample `deltap`, the Householder mix, saturation,
filters and DC blockers are bypassed, so the loop is a pure delay.

## Fixes applied on this branch

1. **phaser2 restored.** It is available in Csound 6.18; the earlier failure
   was an a-rate frequency argument where phaser2 requires k-rate. Dual
   crossfaded sweeps (two phase-offset LFOs), 4 second-order stages each,
   L sweeps up and R sweeps down.
2. **Gain staging.** Input scaling 0.8 to 0.95, Cosmos output mix 0.5 to 1.2,
   output gain 0.8 to 1.2.
3. **Freeze.** Pure delay loop as described above.
4. **Harness.** Gates stay latched after the first trigger instead of a 50 ms
   pulse; impulse tests allow a 0 dBFS peak (the input impulse is 1.0, so a
   1.0 peak is expected, not clipping); freeze stability measured after the
   input ends; 60 s scenario fixed.

## Levels on Michael's test file

Input: peak -1.0 dBFS, RMS -20.9 dBFS.

| Example | Mix | Peak | RMS | RMS vs input |
|---------|-----|------|-----|--------------|
| michael_subtle | 60% | -2.8 dBFS | -23.3 dBFS | -2.4 dB |
| michael_rhythmic | 75% | -6.1 dBFS | -26.1 dBFS | -5.2 dB |
| michael_freeze | 90% | -6.5 dBFS | -30.3 dBFS | -9.4 dB |

Before the gain staging fix the three renders peaked at -7.7, -10.9 and
-11.4 dBFS with RMS 7 to 15 dB below the input. The subtle example now sits
within 3 dB of the input. The rhythmic and freeze examples are at 75% and 90%
mix with low Flux Chain feedback, so most of the output is wet and the level
reflects the reverb density rather than a gain loss; the freeze example also
spends half its length frozen at the captured level after the input stops.

## Test results by scenario

| Scenario | Pass rate | Notes |
|----------|-----------|-------|
| 01_defaults | 9/9 | |
| 02_moon_cascade | 9/9 | multi-tap delay |
| 03_massive_reverb | 9/9 | |
| 04_freeze_mode | 9/9 | freeze holds within 0.06 to 0.93 dB |
| 05_phaser_on | 9/9 | phaser2 |
| 06_max_everything | 9/9 | stable at extreme settings |
| 07_mode_sweep | 9/9 | |
| 08_multiply_sweep | 9/9 | |
| michael_subtle | 9/9 | |
| michael_rhythmic | 9/9 | |
| michael_freeze | 9/9 | freeze holds within 0.06 to 1.44 dB |
| test_freeze_60s | 9/9 | 60 s hold, 0.15 to 1.33 dB |

## CPU performance

Offline render speed 20 to 50 times faster than real time on the VM (for
example 52 s of audio in 2.0 s). No measurement on Nebulae hardware yet.

## Opcodes used

`phaser2`, `deltap`, `deltap3`, `alpass`, `flanger`, `follow2`, `lfo`,
`oscili`, `tone`, `atone`, `dcblock2`, `tanh`, `limit`, `clip`, `portk`,
`trigger`, `timeinsts`. All standard in Csound 6.18.

## Known limitations

- Tap/clock input measures the interval but v0 does not yet apply it to the
  delay time.
- Multiply and Cosmos Mode are stepped controls and can click when changed.
- The limiter samples the first sample of each k-block (Csound `k(abs(a))`),
  so it reacts to peaks with up to one block of delay.
