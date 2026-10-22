#!/usr/bin/env python3
"""Render example files with Michael's test audio"""

import sys
import time
from pathlib import Path
from render import render_scenario, analyze_output

DEV_DIR = Path(__file__).parent
SCENARIOS = [
    'michael_subtle.json',
    'michael_rhythmic.json',
    'michael_freeze.json'
]
INPUT_FILE = DEV_DIR / 'test_signals' / 'michael_test.wav'
OUTPUT_DIR = DEV_DIR / 'examples'

def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    
    print("Rendering example files with Michael's test audio...")
    print(f"Input: {INPUT_FILE}")
    print()
    
    total_start = time.time()
    total_audio_duration = 0
    
    for scenario_name in SCENARIOS:
        scenario_file = DEV_DIR / 'scenarios' / scenario_name
        output_name = scenario_name.replace('.json', '.wav')
        output_file = OUTPUT_DIR / output_name
        
        print(f"Rendering: {scenario_name}")
        start = time.time()
        
        success = render_scenario(scenario_file, INPUT_FILE, output_file)
        
        elapsed = time.time() - start
        
        if success and output_file.exists():
            analyze_output(output_file, output_name)
            
            # Get audio duration
            import soundfile as sf
            audio, sr = sf.read(output_file)
            duration = len(audio) / sr
            total_audio_duration += duration
            
            realtime_ratio = elapsed / duration
            print(f"  Render time: {elapsed:.2f}s for {duration:.1f}s audio ({realtime_ratio:.3f}x realtime)")
            print()
        else:
            print(f"  FAILED")
            print()
    
    total_elapsed = time.time() - total_start
    overall_ratio = total_elapsed / total_audio_duration if total_audio_duration > 0 else 0
    
    print(f"Total: {total_elapsed:.2f}s render time for {total_audio_duration:.1f}s audio")
    print(f"Overall ratio: {overall_ratio:.3f}x realtime")
    print()
    print("Example files saved to:", OUTPUT_DIR)
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
