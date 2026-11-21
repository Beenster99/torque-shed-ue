"""The ferries' own sounds (Ride UK, World/TorqueFerry.cpp), made from nothing but maths here, so they are ours to
use without any licence to keep: Data/Audio/Ferry/*.wav and its manifest.json in TorqueSfx's form (UTorqueSfx loads
it after Data/Audio/TS's; Tools/import_ts_sounds.py never touches this folder).

    python3 Tools/audio/make_ferry_sounds.py

  ferry_engine    the boat's diesel under way: a six-cylinder marine engine about 600 rpm (30 firings a second), its
                  exhaust's thump, the block's clatter, a faint turbo, the hull's drone (loop)
  ferry_wash      the propellers' wash and the water churning past the hull under way (loop)
  ferry_lap       water lapping at the hull and the slipway while tied up (loop)
  ferry_ramp      the ramp's hydraulics: the pump's motor and whine, the oil's hiss (loop, while a ramp moves)
  ferry_clank     a ramp coming down onto the slipway's concrete, or home against the deck (three takes)
  ferry_barrier   the barrier's motor lifting or lowering its arm, and its stop (two takes)
  ferry_gull      [stage 7] a herring gull's call over the slipway: a few raspy "kyow"s (three takes)
  ferry_creak     [stage 7] the mooring ropes and the fenders creaking as the boat works at the slipway (three takes)
  ferry_spray     [stage 7] the bow's spray and the slap of a wave on the hull under way, rougher in a wind (loop)
  ferry_chain     [stage 7] a chain ferry's chains running over its wheels: the links' clatter and the wheels' grind (loop)

Every loop is made whole round its own length (its noise filtered round the loop, every tone a whole number of
cycles), so it joins without a click.
"""
import json
import os
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "Audio", "Ferry")
RATE = 44100
rng = np.random.default_rng(1919)


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
    """A soft band-pass's gain: flat lo..hi, falling away either side."""
    def g(f):
        f = np.maximum(f, 1.0)
        return 1.0 / (1.0 + (lo / f) ** (2 * slope)) / (1.0 + (f / hi) ** (2 * slope))
    return g


def periodic_wobble(n, rates, depth):
    """A slow wobble round the loop: a few sines, each a whole number of cycles in it."""
    t = np.arange(n) / n
    out = np.zeros(n)
    for k, r in enumerate(rates):
        cycles = max(1, round(r * n / RATE))
        out += np.sin(2 * np.pi * cycles * t + rng.uniform(0, 2 * np.pi)) / len(rates)
    return 1.0 + depth * out


def engine():
    secs = 6.0
    n = int(RATE * secs)
    fire = 30.0                       # firings a second (600 rpm, six cylinders, four-stroke)
    cycles = int(round(fire * secs))
    t = np.arange(n) / RATE
    period = n / cycles
    x = np.zeros(n)
    # The exhaust's thump: a pulse each firing, every cylinder a little different, through the pipe's resonance.
    pulses = np.zeros(n)
    strength = [1.0, 0.86, 0.95, 0.9, 1.04, 0.83]
    for k in range(cycles):
        at = int(k * period + rng.uniform(-6, 6)) % n
        pulses[at] += strength[k % 6] * rng.uniform(0.92, 1.08)
    spec = np.fft.rfft(pulses)
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    pipe = 1.0 / np.sqrt(1.0 + ((f - 62.0) / 28.0) ** 2) + 0.55 / np.sqrt(1.0 + ((f - 128.0) / 40.0) ** 2)
    pipe *= 1.0 / (1.0 + (f / 420.0) ** 4)
    x += np.fft.irfft(spec * pipe, n) * 3.0
    # The block's clatter: a noise burst at each firing, a few kilohertz, quiet.
    env = np.zeros(n)
    for k in range(cycles):
        at = int(k * period) % n
        L = int(0.012 * RATE)
        idx = (at + np.arange(L)) % n
        kk = np.arange(L)
        env[idx] += np.minimum(1.0, kk / (0.0012 * RATE)) * np.exp(-kk / (0.003 * RATE)) * strength[(k + 2) % 6]
    clatter = shaped(n, band(900, 3200)) * env
    x += clatter / (np.max(np.abs(clatter)) + 1e-9) * 0.06
    # The hull's drone (the second and third firing harmonics carried through the steel) and a faint turbo.
    for h, a in ((2, 0.22), (3, 0.12), (4, 0.05)):
        cyc = cycles * h / 1.0
        x += a * np.sin(2 * np.pi * cyc * np.arange(n) / n) * np.max(np.abs(x)) * 0.25
    turbo_c = round(1830.0 * secs)
    x += 0.004 * np.max(np.abs(x)) * np.sin(2 * np.pi * turbo_c * np.arange(n) / n + 0.3 * np.sin(2 * np.pi * 2 * np.arange(n) / n))
    # Under it all, the engine room's rumble.
    x += shaped(n, band(25, 160)) * np.max(np.abs(x)) * 0.12
    x *= periodic_wobble(n, [0.5, 1.3], 0.06)
    return x


def wash():
    secs = 8.0
    n = int(RATE * secs)
    churn = shaped(n, band(160, 1800, 1.5)) * periodic_wobble(n, [0.35, 0.8, 1.7], 0.35)
    hiss = shaped(n, band(2500, 9000, 1.5)) * 0.25 * periodic_wobble(n, [0.6, 2.2], 0.5)
    low = shaped(n, band(40, 220)) * 1.3 * periodic_wobble(n, [0.25, 0.5], 0.3)
    x = churn + hiss + low
    # Bubbles bursting in the white water: short pitched blips scattered round the loop.
    for _ in range(int(secs * 26)):
        at = rng.integers(0, n)
        L = int(rng.uniform(0.006, 0.02) * RATE)
        fr = rng.uniform(700, 2400)
        k = np.arange(L)
        blip = np.sin(2 * np.pi * (fr * k / RATE + 0.5 * fr * 1.8 * (k / RATE) ** 2)) * np.exp(-k / (0.004 * RATE))
        x[(at + k) % n] += blip * rng.uniform(0.04, 0.12) * np.std(x) * 3
    return x


def lap():
    secs = 10.0
    n = int(RATE * secs)
    x = shaped(n, band(80, 900, 1.2)) * 0.12
    at = 0.0
    while at < secs - 0.3:
        at += rng.uniform(0.7, 1.9)
        i = int(at * RATE) % n
        L = int(0.6 * RATE)
        k = np.arange(L)
        att = np.minimum(1.0, k / (0.035 * RATE))
        slap = shaped(L, band(250, 1400, 1.5)) * att * np.exp(-k / (0.16 * RATE))
        plop_f = rng.uniform(85, 150)
        plop = np.sin(2 * np.pi * plop_f * k / RATE) * att * np.exp(-k / (0.09 * RATE)) * 0.6
        trickle = shaped(L, band(1500, 5000)) * np.exp(-((k / RATE - 0.2) / 0.12) ** 2) * 0.25
        x[(i + k) % n] += (slap / (np.max(np.abs(slap)) + 1e-9) + plop + trickle) * rng.uniform(0.4, 1.0)
    return x


def ramp():
    secs = 4.0
    n = int(RATE * secs)
    t = np.arange(n) / n
    x = np.zeros(n)
    # The pump's motor: 50 Hz mains hum's harmonics; the gear pump's whine (nine teeth at 24 turns a second).
    for h, a in ((1, 0.5), (2, 0.8), (3, 0.3), (4, 0.25), (6, 0.12)):
        x += a * np.sin(2 * np.pi * round(50 * h * secs) * t)
    whine = round(216.0 * secs)
    wob = 0.4 * np.sin(2 * np.pi * 3 * t)
    for h, a in ((1, 0.35), (2, 0.22), (3, 0.1), (5, 0.05)):
        x += a * np.sin(2 * np.pi * whine * h * t + h * wob)
    # The oil's hiss through the valves, and the rams' creak now and then.
    x += shaped(n, band(1800, 7000)) * 0.12 / 0.3
    x *= periodic_wobble(n, [0.75, 2.0], 0.12)
    creak = np.zeros(n)
    for c in range(3):
        i = rng.integers(0, n)
        L = int(0.25 * RATE)
        k = np.arange(L)
        fr = rng.uniform(380, 620)
        creak[(i + k) % n] += np.sin(2 * np.pi * fr * k / RATE * (1 + 0.15 * np.sin(2 * np.pi * 11 * k / RATE))) * np.sin(np.pi * k / L) ** 2 * 0.25
    return x + creak * np.max(np.abs(x))


def clank(seed):
    r = np.random.default_rng(seed)
    secs = 1.8
    n = int(RATE * secs)
    k = np.arange(n) / RATE
    x = np.zeros(n)
    # The thud of a few tonnes of steel on concrete, then the plate ringing, then a rattle as it settles.
    x += np.sin(2 * np.pi * r.uniform(55, 75) * k) * np.exp(-k / 0.12) * 1.0
    x += np.sin(2 * np.pi * r.uniform(110, 140) * k) * np.exp(-k / 0.08) * 0.5
    for fr, d, a in ((233, 0.5, 0.25), (517, 0.35, 0.2), (931, 0.28, 0.14), (1462, 0.2, 0.1), (2210, 0.14, 0.07), (3170, 0.09, 0.05)):
        x += a * np.sin(2 * np.pi * fr * r.uniform(0.97, 1.03) * k + r.uniform(0, 6)) * np.exp(-k / d)
    click = np.fft.irfft(np.fft.rfft(r.standard_normal(n)) * band(1500, 6000)(np.fft.rfftfreq(n, 1.0 / RATE)), n) * np.exp(-k / 0.006) * 1.2
    x += click
    for j in range(r.integers(2, 5)):
        at = 0.07 + j * r.uniform(0.05, 0.11)
        kk = np.clip(k - at, 0, None)
        x += (k > at) * np.sin(2 * np.pi * r.uniform(400, 1100) * kk) * np.exp(-kk / 0.03) * r.uniform(0.05, 0.15)
    return x


def barrier(up):
    secs = 2.6
    n = int(RATE * secs)
    k = np.arange(n) / RATE
    run = np.clip(k / 0.15, 0, 1) * np.clip((2.2 - k) / 0.1, 0, 1)
    fr = 140 + 25 * np.clip(k / 0.3, 0, 1) * (1.0 if up else 0.9)
    ph = 2 * np.pi * np.cumsum(fr) / RATE
    x = (np.sin(ph) * 0.5 + np.sin(2 * ph) * 0.3 + np.sin(5 * ph) * 0.08) * run
    hum = np.fft.irfft(np.fft.rfft(np.random.default_rng(3 if up else 4).standard_normal(n)) * band(300, 2500)(np.fft.rfftfreq(n, 1.0 / RATE)), n)
    x += hum / (np.std(hum) + 1e-9) * 0.05 * run
    stop = np.clip(k - 2.2, 0, None)
    x += (k > 2.2) * (np.sin(2 * np.pi * 180 * stop) * 0.6 + np.sin(2 * np.pi * 760 * stop) * 0.25) * np.exp(-stop / 0.05)
    return x


def gull(seed):
    r = np.random.default_rng(seed)
    secs = 2.4
    n = int(RATE * secs)
    x = np.zeros(n)
    at = 0.05
    for c in range(r.integers(2, 5)):
        L = int(r.uniform(0.18, 0.32) * RATE)
        if int(at * RATE) + L >= n:
            break
        k = np.arange(L) / L
        # The call: up quickly then falling away, a nasal voice (strong odd and even harmonics), a raspy flutter.
        f0 = r.uniform(780, 980) * (1.0 + 0.35 * np.sin(np.pi * np.clip(k * 1.6, 0, 1)) - 0.25 * k)
        ph = 2 * np.pi * np.cumsum(f0) / RATE
        flutter = 1.0 + 0.35 * np.sin(2 * np.pi * r.uniform(55, 75) * np.arange(L) / RATE)
        voice = sum(a * np.sin(h * ph) for h, a in ((1, 0.6), (2, 1.0), (3, 0.7), (4, 0.4), (5, 0.25), (6, 0.12)))
        env = np.sin(np.pi * k) ** 0.6 * np.exp(-1.2 * k)
        rasp = np.fft.irfft(np.fft.rfft(r.standard_normal(L)) * band(1500, 5000)(np.fft.rfftfreq(L, 1.0 / RATE)), L)
        rasp = rasp / (np.std(rasp) + 1e-9)
        i = int(at * RATE)
        x[i:i + L] += (voice * flutter + rasp * 0.5) * env * r.uniform(0.6, 1.0)
        at += L / RATE + r.uniform(0.08, 0.25)
    return x


def creak(seed):
    r = np.random.default_rng(seed)
    secs = 1.6
    n = int(RATE * secs)
    k = np.arange(n) / RATE
    # Rope or rubber under load: a stick-slip, a train of little pulses whose rate sweeps, ringing a hollow body.
    rate = r.uniform(35, 60) * (1.0 + 0.6 * np.sin(np.pi * np.clip(k / 1.2, 0, 1)))
    ph = np.cumsum(rate) / RATE
    pulses = (np.diff(np.floor(ph), prepend=0) > 0).astype(float) * (0.6 + 0.4 * r.random(n))
    body = np.zeros(n)
    for fr, d in ((r.uniform(180, 260), 0.02), (r.uniform(520, 700), 0.012), (r.uniform(1100, 1500), 0.006)):
        L = int(0.08 * RATE)
        kk = np.arange(L) / RATE
        body = body + np.convolve(pulses, np.sin(2 * np.pi * fr * kk) * np.exp(-kk / d))[:n]
    env = np.sin(np.pi * np.clip(k / 1.3, 0, 1)) ** 0.7
    return body * env


def spray():
    secs = 7.0
    n = int(RATE * secs)
    t = np.arange(n) / n
    # The hiss of the bow wave and its spray, swelling with the swell; now and then a wave's slap on the hull.
    hiss = shaped(n, band(900, 9000, 1.5)) * periodic_wobble(n, [0.35, 0.8], 0.45)
    hiss = hiss / (np.std(hiss) + 1e-9) * 0.25
    x = hiss
    for c in range(5):
        i = rng.integers(0, n)
        L = int(0.5 * RATE)
        kk = np.arange(L) / RATE
        thud = np.sin(2 * np.pi * rng.uniform(60, 95) * kk) * np.exp(-kk / 0.07)
        splash = shaped(L, band(500, 6000)) * np.exp(-kk / 0.18)
        splash = splash / (np.std(splash) + 1e-9) * 0.35
        x[(i + np.arange(L)) % n] += (thud * 0.8 + splash) * rng.uniform(0.5, 1.0)
    return x


def chain():
    secs = 4.0
    n = int(RATE * secs)
    t = np.arange(n) / n
    x = np.zeros(n)
    # Links over the wheel's teeth about 6 a second (whole cycles round the loop), each a little clack and ring.
    links = round(6.0 * secs)
    for j in range(links):
        i = int(j * n / links + rng.integers(-300, 300)) % n
        L = int(0.12 * RATE)
        kk = np.arange(L) / RATE
        ring = sum(a * np.sin(2 * np.pi * fr * rng.uniform(0.95, 1.05) * kk) * np.exp(-kk / d) for fr, d, a in ((420, 0.05, 0.5), (1150, 0.03, 0.35), (2600, 0.015, 0.2)))
        x[(i + np.arange(L)) % n] += ring * rng.uniform(0.6, 1.0)
    # The wheels' grind and the chain dragging up out of the water.
    grind = shaped(n, band(120, 900)) * periodic_wobble(n, [1.5, 3.0], 0.3)
    x += grind / (np.std(grind) + 1e-9) * 0.12
    drip = shaped(n, band(2000, 7000))
    x += drip / (np.std(drip) + 1e-9) * 0.04
    return x


def main():
    os.makedirs(OUT, exist_ok=True)
    sounds = {
        "ferry_engine": {"files": [write("ferry_engine.wav", engine())], "from": "synth", "loop": True},
        "ferry_wash": {"files": [write("ferry_wash.wav", wash())], "from": "synth", "loop": True},
        "ferry_lap": {"files": [write("ferry_lap.wav", lap())], "from": "synth", "loop": True},
        "ferry_ramp": {"files": [write("ferry_ramp.wav", ramp())], "from": "synth", "loop": True},
        "ferry_clank": {"files": [write("ferry_clank_%d.wav" % i, clank(40 + i)) for i in range(3)], "from": "synth", "loop": False},
        "ferry_barrier": {"files": [write("ferry_barrier_up.wav", barrier(True)), write("ferry_barrier_down.wav", barrier(False))], "from": "synth", "loop": False},
        "ferry_gull": {"files": [write("ferry_gull_%d.wav" % i, gull(70 + i)) for i in range(3)], "from": "synth", "loop": False},
        "ferry_creak": {"files": [write("ferry_creak_%d.wav" % i, creak(80 + i)) for i in range(3)], "from": "synth", "loop": False},
        "ferry_spray": {"files": [write("ferry_spray.wav", spray())], "from": "synth", "loop": True},
        "ferry_chain": {"files": [write("ferry_chain.wav", chain())], "from": "synth", "loop": True},
    }
    manifest = {
        "about": "The ferries' own sounds for TorqueSfx, made by Tools/audio/make_ferry_sounds.py from maths alone (no recordings: "
                 "nothing to licence). UTorqueSfx reads this after Data/Audio/TS/manifest.json; the files are relative to this folder.",
        "sounds": sounds,
    }
    json.dump(manifest, open(os.path.join(OUT, "manifest.json"), "w"), indent=1, sort_keys=True)
    print("wrote", len(sounds), "sounds to", OUT)


if __name__ == "__main__":
    main()
