#!/usr/bin/env python3
# Sound Pack 1 (beenster, Nov 24 2026) into the game: cuts, cleans and renames the pack's takes into
# Data/Audio/Pack1 as mono 16-bit 48 kHz WAVs (RideSound reads them; TorqueBikePawn loads them).
#   python3 -I Tools/sound_pack1.py <folder the zip was unpacked into> [Data/Audio/Pack1]
# The pack's own names carry real makers' names; the game's files don't (no real maker names in game).
#   tyre_squeal_N   the squeal alone (cut from "motorbike skid sound" #1-3, the engine under it filtered off)
#   town_twin_N     a parallel twin turned over, catching and ticking over ("speed twin" #1-4)
#   town_vtwin_N    a big 45-degree V-twin ticking over and blipped ("45 degree twin 1800cc" #1-4, first 12 s)
#   circuit_rev_N   a race bike revved hard and let settle ("race motorbike start" #1-4)
#   circuit_v4_N    a sports V4 starting and blipped ("panigale v4s" #1-4)
import glob, os, sys, wave
import numpy as np
from scipy.signal import butter, sosfiltfilt

SRC = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "Data", "Audio", "Pack1")
RATE = 48000

def load(prefix, n):
    hits = sorted(glob.glob(os.path.join(SRC, f"{prefix}_#{n}-*.wav")))
    if not hits: raise SystemExit(f"missing {prefix} #{n}")
    w = wave.open(hits[0])
    assert w.getsampwidth() == 2 and w.getframerate() == RATE, hits[0]
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    return a.reshape(-1, w.getnchannels()).mean(axis=1)

def cut(a, t0, t1, fade_in=0.01, fade_out=0.15):
    s = a[int(t0 * RATE):int(t1 * RATE)].copy()
    fi, fo = int(fade_in * RATE), int(fade_out * RATE)
    s[:fi] *= np.linspace(0.0, 1.0, fi)
    s[-fo:] *= np.linspace(1.0, 0.0, fo) ** 2
    return s

def highpass(a, hz): return sosfiltfilt(butter(4, hz, "highpass", fs=RATE, output="sos"), a)
def dcblock(a): return highpass(a, 25.0)

def save(name, a, peak=0.9):
    a = a / max(np.abs(a).max(), 1e-9) * peak
    os.makedirs(OUT, exist_ok=True)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes((np.clip(a, -1, 1) * 32767).astype("<i2").tobytes())
    print(f"{name}.wav {len(a) / RATE:.2f} s")

# The squeals: where each take's tyre is heard (from its spectrogram), the engine below 900 Hz cut away.
for n, (t0, t1) in enumerate([(1.28, 2.15), (0.52, 2.10), (0.60, 2.30)], 1):
    save(f"tyre_squeal_{n}", cut(highpass(load("motorbike_skid_sound", n), 900.0), t0, t1, 0.008, 0.2))
for n in range(1, 5):
    save(f"town_twin_{n}", cut(dcblock(load("triumph_speed_twin_1", n)), 0.05, 5.9, 0.01, 1.2), 0.8)
    save(f"town_vtwin_{n}", cut(dcblock(load("45_degree_twin_1800c", n)), 0.05, 12.0, 0.01, 2.0), 0.8)
    save(f"circuit_rev_{n}", cut(dcblock(load("race_motorbike_start", n)), 0.05, 5.9, 0.01, 1.2), 0.8)
    save(f"circuit_v4_{n}", cut(dcblock(load("ducati_panigales_v4s", n)), 0.05, 4.9, 0.01, 0.8), 0.8)
