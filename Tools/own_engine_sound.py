"""Cuts a bike's own engine sound out of a ridden recording (beenster's own takes, Bike V3 Sounds).

    python Tools/own_engine_sound.py <recording.wav> <out dir> [--idle A B] [--rpms R1,R2,...] [--min-rpm R] [--max-rpm R]

A ridden recording has no steady takes: the revs rise in each gear, drop at each shift and fall on the
overrun. So the script follows the engine's harmonics through it (their spacing is the four-stroke
cycle's rate, one cycle every two crank turns: rpm / 120; checked against the firing order's own pattern of
strong and missing harmonics, as the twin-pulse V4's 4th and the 60-degree twin's 6th), picks short stretches where the revs move smoothly, flattens each to one steady pitch,
and loops it seamlessly (a whole number of engine cycles, crossfaded). Rising stretches are the bike on
the throttle (ex_on_<rpm>.wav), falling ones off it (ex_off_<rpm>.wav); the --idle stretch is tickover.
The files are mono 16-bit at the engine synth's loop rate (32 kHz), named as the recorded loops
EngineSynth's FBank reads, plus loops.json saying where each came from.
"""
import argparse
import json
import os
import wave

import numpy as np

OUT_RATE = 32000          # TorqueEngine::Rate
WIN = 0.40                # seconds per stretch looked at
LOOP_S = 0.45             # loop length (whole cycles near this)
XFADE_S = 0.06
RPM_PER_HZ = 120.0       # harmonic spacing (one per four-stroke cycle) to rpm


def read(path):
    w = wave.open(path)
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    x = np.frombuffer(w.readframes(n), np.int16).reshape(-1, ch).astype(float).mean(1) / 32768.0
    return x, sr


def comb_spacing(seg, sr, lo, hi, step, fmax=1400.0, nfft=None):
    """Best harmonic spacing in [lo, hi]: harmonics up minus half-way points down (log spectrum)."""
    nfft = nfft or (1 << int(np.ceil(np.log2(len(seg) * 4))))
    S = np.log(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft)) + 1e-7)
    f = np.fft.rfftfreq(nfft, 1.0 / sr)
    best = (-1e9, lo)
    for c in np.arange(lo, hi, step):
        k = np.arange(1, int(fmax / c) + 1)
        s = np.interp(k * c, f, S).mean() - np.interp((k + 0.5) * c, f, S).mean()
        if s > best[0]:
            best = (s, c)
    return best[1], best[0]


def track(x, sr, a, b, seed):
    """The spacing every 20 ms through [a, b], each a small search round the last."""
    hop, n = int(0.02 * sr), 4096
    out, cur = [], seed
    for i in range(int(a * sr), int(b * sr) - n, hop):
        c, s = comb_spacing(x[i:i + n], sr, cur * 0.96, cur * 1.04, cur * 0.002, nfft=1 << 15)
        out.append(((i + n / 2) / sr, c, s))
        cur = c
    return np.array(out)


def flatten(seg, sr, tr, t0, target):
    """Resamples seg (starting at t0) so its spacing curve tr becomes the steady target."""
    t = t0 + np.arange(len(seg)) / sr
    f = np.interp(t, tr[:, 0], tr[:, 1])
    phase = np.cumsum(f / target) / sr          # output time at each input sample
    out_t = np.arange(0, phase[-1], 1.0 / sr)
    return np.interp(out_t, phase, seg)


def make_loop(seg, sr, spacing, seconds=LOOP_S):
    """A seamless loop of whole cycles (the spacing is one per cycle), at OUT_RATE."""
    cyc = 1.0 / spacing
    n_cyc = max(1, round(seconds / cyc))
    L = n_cyc * cyc
    X = XFADE_S
    if len(seg) / sr < L + X:
        return None
    need = int((L + X) * sr)
    s = seg[:need]
    # resample to OUT_RATE
    tt = np.arange(0, len(s) / sr, 1.0 / OUT_RATE)
    s = np.interp(tt, np.arange(len(s)) / sr, s)
    Ln, Xn = int(round(L * OUT_RATE)), int(round(X * OUT_RATE))
    body = s[:Ln].copy()
    tail = s[Ln:Ln + Xn]
    w = np.sin(np.linspace(0, np.pi / 2, len(tail))) ** 2
    body[:len(tail)] = body[:len(tail)] * w + tail * (1 - w)
    return body


def highpass(x, sr, fc):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    X *= 1.0 / np.sqrt(1.0 + (fc / np.maximum(f, 1e-3)) ** 8)
    return np.fft.irfft(X, len(x))


def write(path, s):
    s = np.clip(s, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes((s * 32767).astype(np.int16).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--idle", nargs=2, type=float, help="a stretch of tickover, seconds")
    ap.add_argument("--rpms", default="", help="bucket rpms (comma list); default a ladder over what's recorded")
    ap.add_argument("--idle-rpm", type=float, default=0.0,
                    help="the tickover's revs when its harmonics are too smeared to follow (found from the firing beat)")
    ap.add_argument("--idle-only", action="store_true",
                    help="no rev loops (a standing recording whose blips are too quick to loop): just the tickover, "
                         "written as idle_<rpm>.wav, which the game plays at tickover over the made sound")
    ap.add_argument("--min-rpm", type=float, default=1200.0, help="lowest revs to look for")
    ap.add_argument("--max-rpm", type=float, default=12000.0, help="highest revs to look for (the redline)")
    ap.add_argument("--hp", type=float, default=28.0, help="high-pass (wind rumble), Hz")
    a = ap.parse_args()
    x, sr = read(a.src)
    x = highpass(x, sr, a.hp)
    dur = len(x) / sr

    # Every stretch: its spacing curve, how clean (comb score) and how smooth it is.
    cands = []
    t = 0.0
    while t + WIN + 0.2 < dur:
        mid = x[int((t + WIN / 2 - 0.1) * sr):int((t + WIN / 2 + 0.1) * sr)]
        seed, score = comb_spacing(mid, sr, a.min_rpm / RPM_PER_HZ, a.max_rpm / RPM_PER_HZ, 0.05)
        tr = track(x, sr, t, t + WIN + 0.2, seed)
        if len(tr) > 5:
            ratio = tr[1:, 1] / tr[:-1, 1]
            jump = np.abs(np.log(ratio)).max()
            slope = np.log(tr[-1, 1] / tr[0, 1]) / (tr[-1, 0] - tr[0, 0])   # per second
            cands.append({"t": t, "rpm": float(np.median(tr[:, 1]) * RPM_PER_HZ), "score": float(tr[:, 2].mean()),
                          "jump": float(jump), "slope": float(slope), "tr": tr})
        t += 0.1
    smooth = [c for c in cands if c["jump"] < 0.012 and a.min_rpm <= c["rpm"] <= a.max_rpm]
    if a.idle_only:
        # A tickover take: as it is (its own small wobble kept), a few seconds looped seamlessly.
        rpm = a.idle_rpm
        seg = x[int(a.idle[0] * sr):int(a.idle[1] * sr)]
        loop = make_loop(seg, sr, rpm / RPM_PER_HZ, seconds=min(3.0, (a.idle[1] - a.idle[0]) - XFADE_S - 0.1))
        loop *= 0.5 / np.abs(loop).max()
        os.makedirs(a.out, exist_ok=True)
        name = "idle_%d.wav" % int(round(rpm))
        write(os.path.join(a.out, name), loop)
        info = {"source": os.path.basename(a.src), "loops": [{"file": name, "from_s": a.idle[0], "to_s": a.idle[1],
                "rpm": int(round(rpm)), "seconds": round(len(loop) / OUT_RATE, 2)}]}
        with open(os.path.join(a.out, "loops.json"), "w") as f:
            json.dump(info, f, indent=1)
        print(info)
        return

    def pick(lo, hi, kind):
        pool = [c for c in smooth if lo <= c["rpm"] <= hi and
                (kind == "on" and c["slope"] > 0.03 or kind == "off" and c["slope"] < -0.06)]
        if not pool:
            return None
        # cleanest, preferring gentle slopes (less flattening)
        return max(pool, key=lambda c: c["score"] - 2.0 * abs(c["slope"]))

    os.makedirs(a.out, exist_ok=True)
    made = []
    rec_lo = min(c["rpm"] for c in smooth)
    rec_hi = max(c["rpm"] for c in smooth if c["slope"] > 0.02)
    if a.rpms:
        ladder = [float(r) for r in a.rpms.split(",")]
    else:
        n = int(np.ceil(np.log(rec_hi / rec_lo) / np.log(1.17))) + 1
        ladder = list(np.exp(np.linspace(np.log(rec_lo * 1.08), np.log(rec_hi * 0.97), n)))

    def emit(c, kind, span=WIN + 0.2):
        tr = c["tr"]
        target = float(np.median(tr[:, 1]))
        a0, a1 = c["t"], c["t"] + span
        seg = x[int(a0 * sr):int(a1 * sr)]
        flat = flatten(seg, sr, tr, a0, target)
        loop = make_loop(flat, sr, target)
        if loop is None:
            return
        rpm = int(round(target * RPM_PER_HZ))
        if any(m["rpm"] == rpm and m["file"].startswith("ex_" + kind) for m in made):
            return
        name = "ex_%s_%d.wav" % (kind, rpm)
        made.append({"file": name, "from_s": round(a0, 2), "to_s": round(a1, 2), "rpm": rpm,
                     "spacing_hz": round(target, 2), "slope_per_s": round(c["slope"], 3), "loop": loop})

    if a.idle:
        seg = x[int(a.idle[0] * sr):int(a.idle[1] * sr)]
        if a.idle_rpm > 0:
            # (a steady tickover played as it is: a flat curve at the given revs)
            sp = a.idle_rpm / RPM_PER_HZ
            tr = np.array([[a.idle[0], sp, 0.0], [a.idle[1], sp, 0.0]])
        else:
            sp, sc = comb_spacing(seg[:int(0.5 * sr)], sr, 8.0, 40.0, 0.02)
            tr = track(x, sr, a.idle[0], a.idle[1], sp)
        emit({"t": a.idle[0], "tr": tr, "slope": 0.0}, "off", span=a.idle[1] - a.idle[0])
    for r in ladder:
        for kind in ("on", "off"):
            c = pick(r / 1.08, r * 1.08, kind)
            if c:
                emit(c, kind)

    # One loudness for the set: the loudest loop at -6 dBFS peak, all keeping their recorded levels.
    peak = max(np.abs(m["loop"]).max() for m in made)
    for m in made:
        loop = m.pop("loop") * (0.5 / peak)
        write(os.path.join(a.out, m["file"]), loop)
        m["rms"] = round(float(np.sqrt((loop ** 2).mean())), 4)
    info = {"source": os.path.basename(a.src), "recorded_rpm": [int(rec_lo), int(rec_hi)],
            "loops": made}
    with open(os.path.join(a.out, "loops.json"), "w") as f:
        json.dump(info, f, indent=1)
    for m in info["loops"]:
        print(m)
    print("recorded", int(rec_lo), "-", int(rec_hi), "rpm;", len(made), "loops")


if __name__ == "__main__":
    main()
