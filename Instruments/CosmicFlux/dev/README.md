# CosmicFlux Development and Test Harness

This directory contains offline testing tools for the CosmicFlux instrument.

## Overview

The test harness allows you to:
- Generate synthetic test signals
- Render the instrument offline with various control scenarios
- Automatically check for common issues (NaN, clipping, DC offset, etc.)
- Measure approximate CPU usage (via render time)

## Requirements

- Python 3.7+
- Csound 6+ (command-line tool)
- Python packages: `numpy`, `soundfile`

Install Python dependencies:
```bash
pip3 install numpy soundfile
```

## Quick Start

1. **Generate test signals**:
   ```bash
   python3 generate_test_signals.py
   ```
   
   This creates 8 test signals in `test_signals/`:
   - Impulse (for impulse response)
   - Sine bursts at 110 Hz, 440 Hz, 2 kHz
   - Pink noise burst
   - Synthetic drum loop
   - Sawtooth chord
   - 60s silence (for tail decay testing)

2. **Run the test harness**:
   ```bash
   python3 render.py
   ```
   
   This renders each scenario with each test signal and analyzes the results.
   Outputs are written to `output/`.

## Test Scenarios

Scenarios are defined in `scenarios/*.json` and specify control values and automation.

### Included Scenarios

1. **defaults**: All controls at neutral/default positions
2. **moon_cascade**: High dimension, 6 taps, moderate feedback
3. **massive_reverb**: High density, high warp, smooth mode
4. **freeze_mode**: Tests infinite hold (freeze engaged at 2s)
5. **phaser_on**: Barberpole phaser active
6. **max_everything**: Stress test with all parameters at maximum
7. **mode_sweep**: Sweeps through all 4 Massive modes
8. **multiply_sweep**: Sweeps tap count from 1 to 6

### Scenario Format

```json
{
  "name": "Scenario Name",
  "description": "What this tests",
  "duration": 10.0,
  "static": {
    "loopstart": 0.5,
    "density": 0.7
  },
  "automation": {
    "blend": {
      "start": 0.0,
      "end": 1.0,
      "start_time": 0,
      "end_time": 10.0
    }
  },
  "gates": {
    "freeze": [2.0, 5.0]
  }
}
```

- **static**: Fixed control values (0-1 range)
- **automation**: Linear ramps over time
- **gates**: Trigger times for button presses (20ms pulse)

## Analysis

The harness automatically checks for:
- **NaN/Inf**: Indicates numerical instability
- **DC offset**: Average > 0.01 indicates DC creep
- **Peak level**: Should be < 0.95 after limiter
- **Clipping**: Sustained samples at max level
- **RMS per second**: Printed for decay analysis

## CPU Estimation

Render time is reported relative to real time:
- **1.0×** means real-time on the test machine
- **0.5×** means 2× faster than real-time (50% CPU)
- **2.0×** means slower than real-time (CPU overload)

Note: x86 desktop CPU ≠ Pi 3 ARM CPU. The Pi 3 is roughly 3-5× slower 
per core, so aim for < 0.3× render time on a modern desktop for comfortable 
Pi 3 headroom.

## Adding New Tests

### Add a scenario:
1. Create `scenarios/09_my_test.json`
2. Define static/automation/gates as needed
3. Run `python3 render.py`

### Add a test signal:
1. Edit `generate_test_signals.py`
2. Add your signal generator function
3. Append to the `signals` list in `main()`
4. Run `python3 generate_test_signals.py`

## Troubleshooting

### "Csound not found"
- Install Csound 6+ and ensure `csound` is in your PATH
- Check: `csound --version`

### "No test signals found"
- Run `python3 generate_test_signals.py` first

### Render fails with errors
- Check `output/*.log` (if created)
- Look for Csound syntax errors in the .instr file
- Try running a simple scenario first (01_defaults)

### Analysis shows issues
- **NaN/Inf**: Check for divide-by-zero, sqrt of negative, etc.
- **DC offset**: Ensure `dcblock2` is in feedback loops
- **Clipping**: Reduce gain or improve limiter
- **Silent output**: Check input muting (gksource), freeze timing, or mix level

## File Structure

```
dev/
├── README.md                 # This file
├── render.py                 # Main test harness
├── generate_test_signals.py  # Test signal generator
├── scenarios/                # Test scenarios (JSON)
│   ├── 01_defaults.json
│   ├── 02_moon_cascade.json
│   └── ...
├── test_signals/             # Generated test audio (created by script)
│   ├── 01_impulse.wav
│   ├── 02_sine_110hz.wav
│   └── ...
└── output/                   # Rendered results (created by script)
    ├── defaults_impulse.wav
    ├── moon_cascade_drum_loop.wav
    └── ...
```

## Expected Results (v0.1)

With the current implementation, you should see:
- ✓ All renders complete without errors
- ✓ No NaN/Inf in any output
- ✓ DC offset < 0.001 in all cases
- ✓ Peak levels < 0.95 (limiter working)
- ✓ Freeze mode sustains RMS within ±3 dB for 20+ seconds
- ✓ Silent tails decay below -90 dBFS (no denormal CPU creep)
- ✓ Render time < 0.5× real-time on modern desktop (good Pi 3 margin)

## Next Steps (v1 ideas)

- 16-zone modulation tables (FM and pitch pairs)
- 4-state phaser cycle
- Series/parallel blend between Moon and Massive
- Freeze holds Massive while Moon stays live
- Better barberpole (Esqueda method)
- Per-mode gain staging
