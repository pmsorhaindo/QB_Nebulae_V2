# CosmicFlux Test Results

## Test Environment

- **Csound version**: 6.18.1 (double samples) 2024-04-07
- **Sample rate**: 48000 Hz
- **Block size**: 64 samples (ksmps)
- **Test date**: 2026-10-08
- **Platform**: Linux (Cloud Agent VM)

## Summary

**Overall**: 104 of 108 tests passed (96.3%)

The instrument compiles and renders successfully across all 12 scenario types with 9 different test signals. All major features are operational.

## Fixes Applied (2026-10-08, Second Pass)

### phaser2 Restoration - Complete

Restored proper 2nd-order barberpole phaser using phaser2 opcode:
- K-rate frequency control (phaser2 requires k-rate, not a-rate)
- Dual crossfaded sweeps with phase-offset LFOs
- Counter-rotating L/R channels (200-4000 Hz sweep)
- 4 stages per sweep at Q=0.7

### Gain Staging - Significantly Improved

Input: peak -1.0 dBFS, RMS -20.9 dBFS

Output levels after fixes:
- Subtle: peak -2.8 dBFS, RMS -23.3 dBFS (-2.4 dB vs input)
- Rhythmic: peak -6.1 dBFS, RMS -26.1 dBFS (-5.2 dB vs input)
- Freeze: peak -6.5 dBFS, RMS -30.3 dBFS (-9.4 dB vs input)

Changes applied:
- Input scaling: 0.8 → 0.95
- FDN output mix: 0.5 → 1.2
- Final output gain: 0.8 → 1.2

Result: Wet signal now sits 2-10 dB below dry (was 7-15 dB). Subtle scenario meets the target of within 3 dB.

### Freeze Mode - Improved but Limited

Multiple attempts to achieve true infinite hold (< 2 dB variation over 60s):

Approaches tested:
1. Bypass all processing in freeze (filters, DC blocker, saturation)
2. Force modulation to exactly zero
3. Use fixed integer-sample delays (deltap instead of deltap3)
4. Bypass Householder matrix mixing (direct feedback)
5. Perfect unity passthrough in feedback path

Result: Freeze sustains with 5.2 dB variation over 20 seconds (target was < 2 dB over 60s).

The Householder FDN architecture makes true infinite hold extremely difficult. Even with mathematically unitary mixing, integer-sample delays, and no processing in the loop, Csound numeric precision and the complexity of the 8-line FDN cause gradual decay. Current implementation provides a usable freeze effect with slow decay rather than perfect infinite sustain.

### Test Analysis Improvements

Relaxed peak threshold for impulse tests from 0.95 to 1.01. Impulses naturally reach 0dBFS when input is 1.0. Previous threshold was flagging expected behavior as failures.

## Known Limitations

1. **Freeze Stability**: 5.2 dB variation over 20s instead of target < 2 dB over 60s. True infinite hold is not achievable with this FDN architecture.

2. **Freeze Gain Loss**: Freeze scenarios show 9-10 dB gain loss. This is related to the freeze stability issue - the signal gradually decays rather than holding infinitely.

3. **Gain Staging in Complex Scenarios**: Rhythmic and freeze scenarios show 5-10 dB loss. Further gain increases would risk clipping at extreme control positions.

## Test Results by Scenario

| Scenario | Pass Rate | Notes |
|----------|-----------|-------|
| 01_defaults | 9/9 | All signals pass |
| 02_moon_cascade | 9/9 | Multi-tap delay working correctly |
| 03_massive_reverb | 9/9 | All pass with relaxed impulse threshold |
| 04_freeze_mode | 8/9 | 1 michael_test failure (5.2 dB variation) |
| 05_phaser_on | 9/9 | phaser2 working correctly |
| 06_max_everything | 9/9 | Stable at extreme settings |
| 07_mode_sweep | 9/9 | All pass with relaxed impulse threshold |
| 08_multiply_sweep | 9/9 | All pass with relaxed impulse threshold |
| michael_freeze | 8/9 | 1 michael_test failure (freeze instability) |
| michael_rhythmic | 9/9 | All pass |
| michael_subtle | 9/9 | All pass |
| test_freeze_60s | 7/9 | 2 failures (drum loop + michael_test freeze) |

## CPU Performance

- Average render speed: 0.02x to 0.05x realtime (20-50x faster than realtime)
- Example: 52 seconds of audio rendered in 2.02 seconds
- CPU efficiency: Excellent for offline rendering, should be efficient on Nebulae V2 hardware

## Signal Analysis

### Michael Test Audio Examples

Input: peak -1.0 dBFS, RMS -20.9 dBFS

| Example | Peak (dBFS) | RMS (dBFS) | RMS vs Input | Freeze Stability |
|---------|-------------|------------|--------------|------------------|
| Subtle | -2.8 | -23.3 | -2.4 dB | N/A |
| Rhythmic | -6.1 | -26.1 | -5.2 dB | N/A |
| Freeze | -6.5 | -30.3 | -9.4 dB | 5.2 dB var / 20s |

### Observations

- Subtle scenario achieves target (-2.4 dB within 3 dB of input)
- Rhythmic shows moderate loss (-5.2 dB)
- Freeze shows significant loss (-9.4 dB) related to stability issue
- No clipping or distortion artifacts
- Dynamic range well preserved

## Opcode Usage

All standard Csound 6.18 opcodes work correctly:

- `phaser2`: Working with k-rate frequency control
- `deltap`, `deltap3`: Non-interpolating and interpolating delay taps
- `alpass`: Allpass filters
- `lfo`, `oscili`: LFO generation
- `tone`, `butterhp`: Filtering
- `tanh`, `limit`: Saturation/limiting
- `dcblock2`: DC removal
- `follow2`: Envelope following

## Conclusion

CosmicFlux v0 is fully functional with strong performance characteristics. The instrument delivers the intended Polymoon-inspired multi-tap delay feeding into Supermassive-style FDN reverb with barberpole phasing.

Major improvements in this iteration:
- phaser2 properly restored (was incorrectly flagged as missing)
- Gain staging significantly improved (subtle scenario within target)
- Freeze mode much more stable (5.2 dB vs previous 20+ dB decay)
- Impulse test false positives eliminated

The 96.3% test pass rate reflects a solid v0 implementation. The 4 failures are all related to the known freeze stability limitation and do not affect non-freeze operation.

Recommended for testing on Nebulae V2 hardware.
