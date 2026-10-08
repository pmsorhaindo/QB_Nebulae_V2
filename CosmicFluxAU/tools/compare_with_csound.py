#!/usr/bin/env python3
"""
Render the dev scenarios through the C++ core and compare with Csound renders.

Usage (from the repo root, after building with CMake):
    python3 CosmicFluxAU/tools/compare_with_csound.py --render-bin /tmp/cfbuild/cosmicflux_render

For each scenario the script:
  * renders the scenario with the C++ core (`cosmicflux_render --csound-init`)
    using the same input file as the Csound harness,
  * loads the Csound render (dev/examples for the Michael scenarios, otherwise
    rendered on the fly with dev/render.py if csound is installed),
  * reports peak/RMS of both, the RMS difference, per-second envelope
    correlation, spectral centroid of both, and the residual (C++ minus Csound)
    level relative to the Csound render.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / "Instruments" / "CosmicFlux" / "dev"
SCENARIOS = DEV / "scenarios"
EXAMPLES = DEV / "examples"
INPUT = DEV / "test_signals" / "michael_test.wav"


def db(x):
    return 20 * np.log10(max(x, 1e-12))


def centroid(a, sr):
    mono = a.mean(axis=1) if a.ndim == 2 else a
    spec = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))
    freqs = np.fft.rfftfreq(len(mono), 1 / sr)
    return float((spec * freqs).sum() / (spec.sum() + 1e-12))


def per_second_rms(a, sr):
    n = len(a) // sr
    return np.array([np.sqrt(np.mean(a[i * sr:(i + 1) * sr] ** 2)) for i in range(n)])


def scenario_args(scn):
    args = []
    for k, v in scn.get("static", {}).items():
        args += ["--set", f"{k}={v}"]
    for k, times in scn.get("gates", {}).items():
        args += ["--gate", f"{k}={','.join(str(t) for t in times)}"]
    for k, auto in scn.get("automation", {}).items():
        t0 = auto.get("start_time", 0)
        t1 = auto.get("end_time", scn.get("duration", 10.0))
        args += ["--ramp", f"{k}={auto['start']},{auto['end']},{t0},{t1}"]
    return args


def csound_render(scn_path, scn, out_path):
    sys.path.insert(0, str(DEV))
    from render import render_scenario  # noqa: E402
    return render_scenario(scn_path, INPUT, out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render-bin", required=True)
    ap.add_argument("--scenarios", nargs="*", default=["michael_subtle", "michael_rhythmic", "michael_freeze",
                                                        "01_defaults", "05_phaser_on", "07_mode_sweep"])
    ap.add_argument("--out", default="/tmp/cosmicflux_compare")
    a = ap.parse_args()
    out_dir = Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not INPUT.exists():
        print(f"missing input {INPUT} (unzip uploads/michael_test.zip and resample to 48 kHz first)")
        return 1

    rows = []
    for name in a.scenarios:
        scn_path = SCENARIOS / f"{name}.json"
        scn = json.loads(scn_path.read_text())
        dur = scn.get("duration", 10.0)

        cpp_path = out_dir / f"{name}_cpp.wav"
        cmd = [a.render_bin, "--in", str(INPUT), "--out", str(cpp_path), "--duration", str(dur),
               "--csound-init", "--pcm16"] + scenario_args(scn)
        subprocess.run(cmd, check=True, capture_output=True)

        cs_path = EXAMPLES / f"{name}.wav"
        if not cs_path.exists():
            cs_path = out_dir / f"{name}_csound.wav"
            if not cs_path.exists() and not csound_render(scn_path, scn, cs_path):
                print(f"{name}: could not produce Csound reference")
                continue

        cpp, sr = sf.read(cpp_path)
        cs, sr2 = sf.read(cs_path)
        n = min(len(cpp), len(cs))
        cpp, cs = cpp[:n], cs[:n]

        rms_cpp = np.sqrt(np.mean(cpp ** 2))
        rms_cs = np.sqrt(np.mean(cs ** 2))
        env_cpp = per_second_rms(cpp, sr)
        env_cs = per_second_rms(cs, sr)
        env_corr = float(np.corrcoef(env_cpp, env_cs)[0, 1]) if len(env_cpp) > 2 else float("nan")
        resid = np.sqrt(np.mean((cpp - cs) ** 2))
        rows.append((name, db(np.abs(cs).max()), db(np.abs(cpp).max()), db(rms_cs), db(rms_cpp),
                     db(rms_cpp) - db(rms_cs), env_corr, centroid(cs, sr), centroid(cpp, sr),
                     db(resid) - db(rms_cs)))

    print(f"{'scenario':18s} {'peak cs':>8s} {'peak cpp':>8s} {'rms cs':>8s} {'rms cpp':>8s} {'d rms':>6s} "
          f"{'env r':>6s} {'cent cs':>8s} {'cent cpp':>8s} {'resid':>7s}")
    for r in rows:
        print(f"{r[0]:18s} {r[1]:8.1f} {r[2]:8.1f} {r[3]:8.1f} {r[4]:8.1f} {r[5]:+6.1f} {r[6]:6.3f} "
              f"{r[7]:8.0f} {r[8]:8.0f} {r[9]:+7.1f}")
    print("\npeak/rms in dBFS; d rms = C++ minus Csound; env r = correlation of per-second RMS envelopes;")
    print("cent = spectral centroid in Hz; resid = level of (C++ - Csound) relative to the Csound render in dB.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
