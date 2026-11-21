"""The trains' own sounds (Ride UK, World/TorqueTrains.cpp), made from nothing but maths here, so they are ours to use
without any licence to keep: Data/Audio/Trains/*.wav and its manifest.json in TorqueSfx's form (UTorqueSfx loads it
after Data/Audio/TS's, as it does the ferries').

    python3 Tools/audio/make_train_sounds.py

  train_dmu        a diesel unit's underfloor engine working: a six-cylinder about 1800 rpm, its gearbox's whine (loop)
  train_loco       a big freight or heritage diesel: a slow heavy throb, its turbo's whistle (loop)
  train_intercity  the intercity power car: a quick twelve-cylinder's hum and a loud turbo (loop)
  train_electric   an electric unit: its traction motors' rising whine and the cooling fans (loop)
  train_steam      a steam engine at about 6 m/s: four chuffs a turn of its wheels, the hiss under them (loop; the
                   game pitches it with the wheels)
  train_rails      the wheels on the rails at 25 m/s: each car's axles over a rail joint, and the rolling roar (loop)
  train_rush       the air of a fast train going by (loop)
  train_horn       a two-tone horn, high then low (two takes)
  train_whistle    a steam engine's chime whistle (two takes)
  train_brakes     the brakes' squeal and the air's release coming to a stand (two takes)

Every loop is made whole round its own length (its noise filtered round the loop, every tone a whole number of
cycles), so it joins without a click.
"""
import json
import os
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Audio", "Trains")
RATE = 44100
rng = np.random.default_rng(1825)


def write(name, x):
    x = np.asarray(x, dtype=np.float64)
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * 0.89
    data = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())
    return name


def shaped(n, gain):
    """Noise round a loop of n samples, its spectrum gain(f) (filtered round the loop: no join)."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    return np.fft.irfft(spec * gain(f), n)


def band(lo, hi, slope=2.0):
    def g(f):
        f = np.maximum(f, 1.0)
        return 1.0 / (1.0 + (lo / f) ** (2 * slope)) / (1.0 + (f / hi) ** (2 * slope))
    return g


def filt(x, gain):
    """x filtered round itself (a loop's sound stays whole)."""
    n = len(x)
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    return np.fft.irfft(np.fft.rfft(x) * gain(f), n)


def norm(x):
    return x / (np.max(np.abs(x)) + 1e-9)


def wobble(n, rates, depth):
    t = np.arange(n) / n
    out = np.zeros(n)
    for r in rates:
        cycles = max(1, round(r * n / RATE))
        out += np.sin(2 * np.pi * cycles * t + rng.uniform(0, 2 * np.pi)) / len(rates)
    return 1.0 + depth * out


def tone(n, hz, phase=0.0):
    """A sine a whole number of cycles round n samples, near hz."""
    cyc = max(1, round(hz * n / RATE))
    return np.sin(2 * np.pi * cyc * np.arange(n) / n + phase)


def firing(secs, fire, cyl, pipe_hz, pipe_q, clatter=0.08, rumble=0.12):
    """A diesel's exhaust: a pulse each firing (each cylinder a little different) through its pipe's resonance."""
    n = int(RATE * secs)
    cycles = int(round(fire * secs))
    period = n / cycles
    strength = rng.uniform(0.82, 1.06, cyl)
    pulses = np.zeros(n)
    for k in range(cycles):
        pulses[int(k * period + rng.uniform(-4, 4)) % n] += strength[k % cyl] * rng.uniform(0.94, 1.06)
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    pipe = 1.0 / np.sqrt(1.0 + ((f - pipe_hz) / pipe_q) ** 2) + 0.5 / np.sqrt(1.0 + ((f - 2.1 * pipe_hz) / (1.4 * pipe_q)) ** 2)
    pipe *= 1.0 / (1.0 + (f / (pipe_hz * 6.0)) ** 4)
    x = norm(np.fft.irfft(np.fft.rfft(pulses) * pipe, n))
    env = np.zeros(n)
    L = int(0.008 * RATE)
    kk = np.arange(L)
    for k in range(cycles):
        env[(int(k * period) + kk) % n] += np.exp(-kk / (0.002 * RATE)) * strength[(k + 1) % cyl]
    x += norm(shaped(n, band(1200, 4000)) * env) * clatter
    x += norm(shaped(n, band(25, 140))) * rumble
    return x, n


def dmu():
    x, n = firing(6.0, 90.0, 6, 180.0, 60.0, clatter=0.1)
    # (the hydraulic gearbox's whine and the cardan shaft's hum under the floor)
    x += 0.05 * tone(n, 742.0) * wobble(n, [0.3], 0.4) + 0.04 * tone(n, 1484.0)
    x += 0.05 * norm(shaped(n, band(300, 2000)))
    return x * wobble(n, [0.4, 1.1], 0.05)


def loco():
    x, n = firing(6.0, 60.0, 16, 95.0, 30.0, clatter=0.06, rumble=0.2)
    # (a big turbocharger's whistle and the cooling fans' roar on the roof)
    x += 0.03 * tone(n, 2350.0, 0.4) * wobble(n, [0.25], 0.3)
    x += 0.12 * norm(shaped(n, band(200, 1400)))
    return x * wobble(n, [0.3, 0.9], 0.05)


def intercity():
    x, n = firing(6.0, 150.0, 12, 260.0, 90.0, clatter=0.07, rumble=0.1)
    x += 0.08 * tone(n, 3550.0) * wobble(n, [0.2], 0.2) + 0.03 * tone(n, 7100.0)
    x += 0.15 * norm(shaped(n, band(400, 3000)))
    return x * wobble(n, [0.35, 1.2], 0.04)


def electric():
    n = int(RATE * 6.0)
    x = np.zeros(n)
    # (the motors' and the inverter's tones: a few harmonics of a whine, a faint hum of the transformer)
    for hz, a in ((620.0, 0.5), (1240.0, 0.32), (1860.0, 0.16), (2480.0, 0.1), (310.0, 0.2)):
        x += a * tone(n, hz, rng.uniform(0, 6.28))
    x *= wobble(n, [0.5, 1.7], 0.15)
    x += 0.15 * tone(n, 100.0) + 0.07 * tone(n, 50.0)
    x += 0.6 * norm(shaped(n, band(150, 1800))) * wobble(n, [0.6], 0.1)
    return x


def steam():
    secs = 6.0
    n = int(RATE * secs)
    chuffs = int(round(4.0 * secs))
    period = n / chuffs
    x = np.zeros(n)
    hiss = shaped(n, band(250, 5000, 1.5))
    for k in range(chuffs):
        at = int(k * period + rng.uniform(-60, 60))
        L = int(period * 0.85)
        kk = np.arange(L)
        # (each beat a blast of exhaust up the chimney: sharp onset, a quick fall; the fourth a little weaker)
        env = np.minimum(1.0, kk / (0.006 * RATE)) * np.exp(-kk / (0.07 * RATE)) * (0.8 if k % 4 == 3 else 1.0) * rng.uniform(0.9, 1.1)
        x[(at + kk) % n] += env * hiss[(at + kk) % n]
    x = filt(x, lambda f: 1.0 / (1.0 + (f / 2500.0) ** 2) + 0.7 / np.sqrt(1.0 + ((f - 180.0) / 60.0) ** 2))
    x = norm(x)
    # (the safety valves' and the cylinder cocks' faint hiss under it, and the motion's clank each turn)
    x += 0.08 * norm(shaped(n, band(2000, 8000)))
    turns = chuffs // 4
    for k in range(turns):
        at = int(k * n / turns + 0.3 * period)
        L = int(0.05 * RATE)
        kk = np.arange(L)
        x[(at + kk) % n] += 0.12 * np.exp(-kk / (0.01 * RATE)) * np.sin(2 * np.pi * 820.0 * kk / RATE)
    return x


def rails():
    """At 25 m/s: each 23 m car's four axles over a joint (two bogies), and the wheels' rolling roar."""
    car_s = 23.0 / 25.0
    cars = 7
    n = int(round(RATE * car_s * cars))
    x = np.zeros(n)
    ping = lambda L: np.exp(-np.arange(L) / (0.012 * RATE))
    L = int(0.06 * RATE)
    for c in range(cars):
        for off in (0.0, 2.6, 16.0, 18.6):
            at = int((c * car_s + off / 25.0 + rng.uniform(-0.003, 0.003)) * RATE) % n
            x[(at + np.arange(L)) % n] += ping(L) * rng.uniform(0.75, 1.0)
    clack = norm(filt(x * shaped(n, band(80, 2500)), band(60, 3000)))
    roll = norm(shaped(n, band(60, 900))) * wobble(n, [0.5, 1.4], 0.15)
    # (a steel wheel's faint ring)
    ring = 0.03 * tone(n, 1650.0) * wobble(n, [0.7], 0.6)
    return clack + 0.5 * roll + ring


def rush():
    n = int(RATE * 5.0)
    x = norm(shaped(n, band(120, 3500, 1.2)))
    return x * wobble(n, [0.4, 0.9, 2.3], 0.3)


def horn(seed):
    r = np.random.default_rng(seed)
    hi, lo = (370.0, 311.0) if seed % 2 == 0 else (349.0, 294.0)
    parts = []
    for hz, secs in ((hi, 0.75), (lo, 0.95)):
        n = int(RATE * secs)
        t = np.arange(n) / RATE
        f = hz * (1.0 + 0.004 * np.sin(2 * np.pi * 5.0 * t))
        ph = 2 * np.pi * np.cumsum(f) / RATE
        # (a brassy reed: odd and even harmonics falling away, a little breath)
        x = sum(np.sin(k * ph) / k ** 1.1 for k in range(1, 12))
        x += 0.04 * r.standard_normal(n)
        env = np.minimum(1.0, t / 0.05) * np.minimum(1.0, (secs - t) / 0.08)
        parts.append(x * env)
    x = np.concatenate(parts + [np.zeros(int(RATE * 0.6))])
    # (heard from afar: the top softened)
    return np.convolve(x, np.ones(6) / 6, mode="same")


def whistle(seed):
    r = np.random.default_rng(seed)
    secs = 1.6 if seed % 2 == 0 else 2.4
    n = int(RATE * secs)
    t = np.arange(n) / RATE
    x = np.zeros(n)
    # (a three-note chime and the steam rushing through it)
    for hz, a in ((523.0, 1.0), (659.0, 0.8), (784.0, 0.6)):
        x += a * np.sin(2 * np.pi * hz * t * (1.0 + 0.003 * np.sin(2 * np.pi * 3.0 * t)) + r.uniform(0, 6.28))
    noise = r.standard_normal(n)
    noise = np.convolve(noise, np.ones(3) / 3, mode="same")
    x += 0.35 * noise
    env = np.minimum(1.0, t / 0.12) * np.minimum(1.0, (secs - t) / 0.25)
    return np.concatenate([x * env, np.zeros(int(RATE * 0.8))])


def brakes(seed):
    r = np.random.default_rng(seed)
    secs = 3.0
    n = int(RATE * secs)
    t = np.arange(n) / RATE
    hz = 2900.0 + 400.0 * (seed % 2)
    f = hz * (1.0 + 0.01 * np.sin(2 * np.pi * 7.0 * t)) * (1.0 - 0.06 * t / secs)
    sq = np.sin(2 * np.pi * np.cumsum(f) / RATE) + 0.3 * np.sin(4 * np.pi * np.cumsum(f) / RATE)
    sq *= np.clip(np.sin(np.pi * t / 2.3), 0, 1) ** 0.7 * (0.6 + 0.4 * np.sin(2 * np.pi * 1.3 * t) ** 2)
    sq *= t < 2.3
    # (the air let out once it stands)
    air = r.standard_normal(n)
    air = air - np.convolve(air, np.ones(4) / 4, mode="same")
    air *= np.clip((t - 2.2) / 0.05, 0, 1) * np.exp(-np.clip(t - 2.2, 0, None) / 0.35)
    return 0.6 * sq + 0.8 * air


def main():
    os.makedirs(OUT, exist_ok=True)
    loop = lambda name, x: {"files": [write(name + ".wav", x)], "from": "synth", "loop": True}
    sounds = {
        "train_dmu": loop("train_dmu", dmu()),
        "train_loco": loop("train_loco", loco()),
        "train_intercity": loop("train_intercity", intercity()),
        "train_electric": loop("train_electric", electric()),
        "train_steam": loop("train_steam", steam()),
        "train_rails": loop("train_rails", rails()),
        "train_rush": loop("train_rush", rush()),
        "train_horn": {"files": [write("train_horn_%d.wav" % i, horn(10 + i)) for i in range(2)], "from": "synth", "loop": False},
        "train_whistle": {"files": [write("train_whistle_%d.wav" % i, whistle(20 + i)) for i in range(2)], "from": "synth", "loop": False},
        "train_brakes": {"files": [write("train_brakes_%d.wav" % i, brakes(30 + i)) for i in range(2)], "from": "synth", "loop": False},
    }
    manifest = {
        "about": "The trains' own sounds for TorqueSfx, made by Tools/audio/make_train_sounds.py from maths alone (no recordings: "
                 "nothing to licence). UTorqueSfx reads this after Data/Audio/TS/manifest.json; the files are relative to this folder.",
        "sounds": sounds,
    }
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1, sort_keys=True)
    print("wrote", len(sounds), "sounds to", OUT)


if __name__ == "__main__":
    main()
