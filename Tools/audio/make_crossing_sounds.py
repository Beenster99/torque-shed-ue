"""The level crossings' sounds (World/TorqueLevelCrossing.cpp), made from maths alone here (nothing to licence):
Data/Audio/Crossing/*.wav and its manifest.json in TorqueSfx's form (UTorqueSfx adds it after Data/Audio/TS's, as the
ferries' are).

    python3 Tools/audio/make_crossing_sounds.py

  lx_alarm   the crossing's warning alarm while its lights show: Britain's electronic "yodel", a tone swooping up and
             down between about 400 and 750 Hz two and a half times a second, its odd harmonics a little buzzy
             through the weatherproof horn (loop)
  lx_motor   a barrier's hydraulic power pack lifting or lowering the boom (or a gate's motor): the motor's hum,
             the pump's whine, the oil's hiss (loop)

Every loop is whole round its own length (each tone a whole number of cycles), so it joins without a click.
"""
import json
import os
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Audio", "Crossing")
RATE = 44100
rng = np.random.default_rng(2611)


def write(name, x):
    x = np.asarray(x, dtype=np.float64)
    x = x / (np.max(np.abs(x)) or 1.0) * 0.89
    data = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())
    return name


def looped_noise(n, lo, hi):
    """Noise band-passed round the loop (in the frequency domain, so it wraps without a seam)."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    spec[(f < lo) | (f > hi)] = 0.0
    x = np.fft.irfft(spec, n)
    return x / (np.std(x) + 1e-9)


def alarm():
    secs, rate = 4.0, 2.5   # (10 whole swoops)
    n = int(RATE * secs)
    t = np.arange(n) / RATE
    # The swoop: up quickly, down more slowly (a skewed triangle), its mean 575 Hz.
    ph = (t * rate) % 1.0
    tri = np.where(ph < 0.35, ph / 0.35, 1.0 - (ph - 0.35) / 0.65)
    f = 400.0 + 350.0 * tri
    f = f - f.mean() + 575.0   # (the mean exact: 2300 whole cycles over the loop)
    phase = 2 * np.pi * np.cumsum(f) / RATE
    x = np.zeros(n)
    for k, a in ((1, 1.0), (3, 0.42), (5, 0.2), (7, 0.1), (9, 0.05)):
        x += a * np.sin(k * phase)
    # The horn: a resonance about 2 kHz and a little grit.
    x = np.tanh(x * 1.6)
    x += 0.03 * looped_noise(n, 1500, 4000)
    return x


def motor():
    secs = 3.0
    n = int(RATE * secs)
    t = np.arange(n) / RATE
    x = np.zeros(n)
    for fr, a in ((100.0, 0.5), (200.0, 0.3), (300.0, 0.12), (50.0, 0.25)):
        x += a * np.sin(2 * np.pi * fr * t)
    x += 0.18 * np.sin(2 * np.pi * 940.0 * t) * (0.8 + 0.2 * np.sin(2 * np.pi * 2.0 * t))   # (the pump's whine)
    x += 0.12 * looped_noise(n, 2000, 7000)   # (the oil)
    x += 0.15 * looped_noise(n, 60, 400)
    return x


def main():
    os.makedirs(OUT, exist_ok=True)
    sounds = {
        "lx_alarm": {"files": [write("lx_alarm.wav", alarm())], "from": "synth", "loop": True},
        "lx_motor": {"files": [write("lx_motor.wav", motor())], "from": "synth", "loop": True},
    }
    manifest = {
        "about": "The level crossings' own sounds for TorqueSfx, made by Tools/audio/make_crossing_sounds.py from maths alone (no recordings: "
                 "nothing to licence). UTorqueSfx adds this after Data/Audio/TS/manifest.json; the files are relative to this folder.",
        "sounds": sounds,
    }
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1, sort_keys=True)
    print("wrote", len(sounds), "sounds to", OUT)


if __name__ == "__main__":
    main()
