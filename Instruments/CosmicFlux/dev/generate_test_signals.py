#!/usr/bin/env python3
"""
Generate test signals for CosmicFlux offline testing

Creates various synthetic test signals:
- Impulse
- Sine bursts at different frequencies
- Pink noise burst
- Synthetic drum loop
- Sawtooth chord
- Extended silence (for tail decay testing)
"""

import numpy as np
import soundfile as sf
from pathlib import Path

SR = 48000  # Sample rate
OUTPUT_DIR = Path(__file__).parent / "test_signals"

def generate_impulse(duration=2.0):
    """Unit impulse at t=0.1s"""
    samples = int(duration * SR)
    audio = np.zeros((samples, 2))
    impulse_pos = int(0.1 * SR)
    audio[impulse_pos, 0] = 1.0
    audio[impulse_pos, 1] = 1.0
    return audio

def generate_sine_burst(freq, duration=2.0, burst_len=0.5):
    """Sine burst with envelope"""
    samples = int(duration * SR)
    burst_samples = int(burst_len * SR)
    
    audio = np.zeros((samples, 2))
    
    # Generate sine
    t = np.arange(burst_samples) / SR
    sine = np.sin(2 * np.pi * freq * t) * 0.7
    
    # Apply envelope (fade in/out)
    env_len = int(0.01 * SR)  # 10ms fade
    envelope = np.ones(burst_samples)
    envelope[:env_len] = np.linspace(0, 1, env_len)
    envelope[-env_len:] = np.linspace(1, 0, env_len)
    
    sine = sine * envelope
    
    # Place burst at 0.1s
    start = int(0.1 * SR)
    audio[start:start+burst_samples, 0] = sine
    audio[start:start+burst_samples, 1] = sine
    
    return audio

def generate_pink_noise_burst(duration=2.0, burst_len=1.0):
    """Pink noise burst (approximation using IIR filter)"""
    samples = int(duration * SR)
    burst_samples = int(burst_len * SR)
    
    audio = np.zeros((samples, 2))
    
    # Generate white noise
    white = np.random.randn(burst_samples) * 0.3
    
    # Simple pink noise approximation (Voss-McCartney algorithm)
    # Using a simple IIR filter
    pink = np.zeros(burst_samples)
    b0 = 0.99886
    b1 = 0.0555179
    b2 = 0.0750759
    b3 = 0.1538520
    b4 = 0.3104856
    b5 = 0.5329522
    b6 = 0.0168980
    
    state = [0] * 7
    for i in range(burst_samples):
        state[0] = 0.99765 * state[0] + white[i] * 0.0990460
        state[1] = 0.96300 * state[1] + white[i] * 0.2965164
        state[2] = 0.57000 * state[2] + white[i] * 1.0526913
        pink[i] = state[0] + state[1] + state[2] + white[i] * 0.1848
    
    # Normalize
    pink = pink / np.max(np.abs(pink)) * 0.5
    
    # Apply envelope
    env_len = int(0.02 * SR)
    envelope = np.ones(burst_samples)
    envelope[:env_len] = np.linspace(0, 1, env_len)
    envelope[-env_len:] = np.linspace(1, 0, env_len)
    
    pink = pink * envelope
    
    # Place burst
    start = int(0.1 * SR)
    audio[start:start+burst_samples, 0] = pink
    audio[start:start+burst_samples, 1] = pink
    
    return audio

def generate_drum_loop(duration=5.0, tempo=120):
    """Synthetic drum loop with kick, snare, hihat"""
    samples = int(duration * SR)
    audio = np.zeros((samples, 2))
    
    beat_interval = 60.0 / tempo  # seconds per beat
    beat_samples = int(beat_interval * SR)
    
    num_beats = int(duration / beat_interval)
    
    for beat in range(num_beats):
        pos = int(beat * beat_samples)
        
        # Kick drum (sine sweep down)
        if beat % 4 == 0:
            kick_len = int(0.15 * SR)
            t = np.arange(kick_len) / SR
            freq = 60 * np.exp(-t * 20)  # Sweep from 60 Hz down
            kick = np.sin(2 * np.pi * freq * t) * np.exp(-t * 15) * 0.8
            end_pos = min(pos + kick_len, samples)
            audio[pos:end_pos, 0] += kick[:end_pos-pos]
            audio[pos:end_pos, 1] += kick[:end_pos-pos]
        
        # Snare (filtered noise)
        if beat % 4 == 2:
            snare_len = int(0.1 * SR)
            noise = np.random.randn(snare_len) * 0.4
            # Simple HPF
            for i in range(1, snare_len):
                noise[i] = noise[i] * 0.9 + noise[i-1] * 0.1
            noise = noise * np.exp(-np.arange(snare_len) / SR * 20)
            end_pos = min(pos + snare_len, samples)
            audio[pos:end_pos, 0] += noise[:end_pos-pos]
            audio[pos:end_pos, 1] += noise[:end_pos-pos]
        
        # Hihat (short noise burst)
        if beat % 2 == 1:
            hat_len = int(0.05 * SR)
            noise = np.random.randn(hat_len) * 0.15
            noise = noise * np.exp(-np.arange(hat_len) / SR * 40)
            end_pos = min(pos + hat_len, samples)
            audio[pos:end_pos, 0] += noise[:end_pos-pos] * 0.5
            audio[pos:end_pos, 1] += noise[:end_pos-pos] * 1.5  # Pan right
    
    return audio

def generate_sawtooth_chord(duration=3.0):
    """Sawtooth chord (root, fifth, octave)"""
    samples = int(duration * SR)
    
    # Frequencies (A minor chord: A2, E3, A3)
    freqs = [110, 165, 220]
    
    audio = np.zeros((samples, 2))
    
    chord_len = int(2.0 * SR)
    t = np.arange(chord_len) / SR
    
    # Generate sawtooth waves
    for freq in freqs:
        # Sawtooth = sum of harmonics
        saw = np.zeros(chord_len)
        for n in range(1, 20):  # 20 harmonics
            saw += np.sin(2 * np.pi * freq * n * t) / n
        saw = saw * 0.3 / len(freqs)
        
        # Envelope
        env_attack = int(0.01 * SR)
        env_release = int(0.5 * SR)
        envelope = np.ones(chord_len)
        envelope[:env_attack] = np.linspace(0, 1, env_attack)
        envelope[-env_release:] = np.linspace(1, 0, env_release)
        
        saw = saw * envelope
        
        audio[:chord_len, 0] += saw
        audio[:chord_len, 1] += saw
    
    return audio

def generate_silence(duration=60.0):
    """Extended silence for tail decay testing"""
    samples = int(duration * SR)
    return np.zeros((samples, 2))

def main():
    """Generate all test signals"""
    
    print("Generating test signals for CosmicFlux...")
    
    OUTPUT_DIR.mkdir(exist_ok=True)
    
    signals = [
        ("01_impulse.wav", generate_impulse()),
        ("02_sine_110hz.wav", generate_sine_burst(110)),
        ("03_sine_440hz.wav", generate_sine_burst(440)),
        ("04_sine_2000hz.wav", generate_sine_burst(2000)),
        ("05_pink_noise.wav", generate_pink_noise_burst()),
        ("06_drum_loop.wav", generate_drum_loop()),
        ("07_sawtooth_chord.wav", generate_sawtooth_chord()),
        ("08_silence_tail.wav", generate_silence(60)),
    ]
    
    for filename, audio in signals:
        filepath = OUTPUT_DIR / filename
        sf.write(filepath, audio, SR, subtype='PCM_16')
        print(f"  ✓ {filename} ({audio.shape[0]/SR:.1f}s)")
    
    print(f"\nGenerated {len(signals)} test signals in {OUTPUT_DIR}")
    print("Ready for offline rendering!")

if __name__ == "__main__":
    main()
