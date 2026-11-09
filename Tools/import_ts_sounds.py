#!/usr/bin/env python3
"""Every sound in the original Torque Shed (Beenster99/torque-shed), brought across to Data/Audio/TS
for the Unreal port's TorqueSfx (Source/TorqueShedProto/TorqueAudio):

    synth/<sound>_<v>.wav     every sound SfxSynth makes, every variant, rendered by the Godot game
                              itself (headless) so the seeds and loudness are exactly the game's
    traffic/petrol|diesel.wav TrafficSounds' two engine loops
    sfx/<file>.wav            the recordings in audio/sfx (tools, dyno fan, doors, phone, switches,
                              gear levers, starters, horns)
    place/<file>.wav          the ride's place beds in audio/place, each with its .licence.txt
    engines/<dir>/<file>.wav  the recorded engine loops in audio/engines (copied as they are)
    sound_of_place.json       which place beds play where (PlaceSound's data)
    manifest.json             what each sound plays: TS's Sfx.sounds() worked out (its recordings if
                              it has any, else its stand-in, else its synthesised variants), and
                              whether it loops

Recordings are decoded to 16-bit mono WAV at their own rate (UE reads WAV itself; no import).

    python Tools/import_ts_sounds.py --torque-shed ../torque-shed --bin godot

Needs Godot 4.6+ and ffmpeg. It copies the Godot project to a temporary folder (the clone is left
alone), imports it, and runs Tools/ts_audio/render.tscn there.
"""
import argparse
import json
import os
import shutil
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Audio", "TS")

# scripts/sfx.gd RECORDED: the recordings that play in place of a synthesised sound.
RECORDED = {
    "ratchet": ["wrench1", "wrench2", "wrench3", "wrench4"],
    "blast": ["sandblaster1", "sandblaster2", "sandblaster3", "sandblaster4"],
    "fan": ["dynofan1", "dynofan2", "dynofan3"],
    "fan_build": ["dynofanbuild1", "dynofanbuild2", "dynofanbuild3"],
    "door": ["rollerdoor"],
    "bell_chime": ["doorbellchime"],
    "phone_ring": ["telephone1", "telephone2", "telephone3"],
    "light_switch": ["lightswitch1a", "lightswitch1b", "lightswitch2", "lightswitch3"],
    "shift_slick": ["gearslick1", "gearslick2", "gearslick3", "gearslick4"],
    "shift_click": ["gearclick1", "gearclick2", "gearclick3", "gearclick4"],
    "shift_clunk": ["gearclunk%d" % i for i in range(1, 9)],
    "starter": ["starter1", "starter2", "starter3", "starter4"],
    "starter_struggle": ["starterstruggle1", "starterstruggle2", "starterstruggle3", "starterstruggle4"],
    "horn_disc": ["horn2", "horn4", "horn7"],
    "horn_low": ["horn1"],
    "horn_snail": ["horn3", "horn5"],
    "horn_high": ["horn6"],
    "horn_pip": ["horn8"],
    "horn_air": ["airhorn10", "airhorn12"],
    "horn_truck": ["airhorn9"],
    "horn_train": ["airhorn11"],
    "horn_police": ["policehorn17", "policehorn18", "policehorn19", "policehorn20"],
    "horn_ship": ["shiphorn13", "shiphorn15", "shiphorn16"],
    "horn_tug": ["shiphorn14"],
}
# scripts/sfx.gd STAND_INS: sounds that are only ever recordings, and what plays without them.
STAND_INS = {"light_switch": "switch", "phone_ring": "bell_chime", "fan_build": "", "shift_slick": "",
             "shift_click": "", "shift_clunk": "", "starter_struggle": ""}
# scripts/horn_audio.gd: how each recorded horn plays, and where a held one's steady note loops from.
HORN_MODES = {"horn_pip": "once", "horn_ship": "once", "horn_tug": "once", "horn_police": "repeat"}
HORN_LOOP_FROM = 0.25


def ff(src, dst):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-sample_fmt", "s16",
                    "-map_metadata", "-1", "-bitexact", dst], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--bin", default="godot", help="Godot 4.6 or later")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    ts = a.torque_shed
    out = a.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    # 1. The synthesised sounds, from the game itself.
    tmp = tempfile.mkdtemp(prefix="ts_audio_")
    try:
        proj = os.path.join(tmp, "ts")
        shutil.copytree(ts, proj, ignore=shutil.ignore_patterns(".git"))
        shutil.copytree(os.path.join(HERE, "ts_audio"), os.path.join(proj, "ts_audio"))
        subprocess.run([a.bin, "--headless", "--path", proj, "--import"], check=True, capture_output=True)
        subprocess.run([a.bin, "--headless", "--path", proj, "res://ts_audio/render.tscn", "--", "--out=" + out],
                       check=True, timeout=1800)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    synth = json.load(open(os.path.join(out, "synth.json")))
    os.remove(os.path.join(out, "synth.json"))

    # 2. The recordings, each licence beside its file.
    recordings = {}
    for folder in ("sfx", "place"):
        src_dir = os.path.join(ts, "audio", folder)
        os.makedirs(os.path.join(out, folder), exist_ok=True)
        names = []
        for f in sorted(os.listdir(src_dir)):
            if not f.endswith(".ogg"):
                continue
            stem = f[:-4]
            ff(os.path.join(src_dir, f), os.path.join(out, folder, stem + ".wav"))
            lic = os.path.join(src_dir, f + ".licence.txt")
            if os.path.exists(lic):
                shutil.copyfile(lic, os.path.join(out, folder, stem + ".wav.licence.txt"))
            names.append(stem)
        recordings[folder] = names
    eng = []
    for root, _, files in os.walk(os.path.join(ts, "audio", "engines")):
        for f in sorted(files):
            if f.endswith(".wav") or f.endswith(".licence.txt"):
                rel = os.path.relpath(os.path.join(root, f), os.path.join(ts, "audio"))
                os.makedirs(os.path.join(out, os.path.dirname(rel)), exist_ok=True)
                shutil.copyfile(os.path.join(root, f), os.path.join(out, rel))
                if f.endswith(".wav"):
                    eng.append(rel.replace(os.sep, "/"))
    recordings["engines"] = sorted(eng)
    shutil.copyfile(os.path.join(ts, "data", "sound_of_place.json"), os.path.join(out, "sound_of_place.json"))

    # 3. What each sound plays (Sfx.sounds): recordings first, then the stand-in, then the made ones.
    have = set(recordings["sfx"])
    sounds = {}

    def resolve(name):
        files = ["sfx/%s.wav" % r for r in RECORDED.get(name, []) if r in have]
        if files:
            return files, "recording"
        if name in STAND_INS:
            if STAND_INS[name] == "":
                return [], "silent"
            return resolve(STAND_INS[name])[0], "stand_in:" + STAND_INS[name]
        if name in synth["sounds"]:
            return ["synth/%s.wav" % f for f in synth["sounds"][name]["files"]], "synth"
        return [], "silent"

    for name in sorted(set(synth["sounds"]) | set(RECORDED)):
        files, src = resolve(name)
        loop = bool(synth["sounds"].get(name, {}).get("loop", False))
        entry = {"files": files, "loop": loop, "from": src}
        if name.startswith("horn_"):
            entry["horn"] = HORN_MODES.get(name, "hold")
            if entry["horn"] == "hold":
                entry["loop_from"] = HORN_LOOP_FROM
        sounds[name] = entry
    commit = subprocess.run(["git", "-C", ts, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    manifest = {
        "about": "Torque Shed's sounds for the Unreal port (TorqueSfx). Written by Tools/import_ts_sounds.py; do not edit.",
        "source": {"repo": "Beenster99/torque-shed", "commit": commit, "godot": synth["godot"]},
        "sounds": sounds,
        "synth": {k: {"files": ["synth/%s.wav" % f for f in v["files"]], "loop": v["loop"]} for k, v in synth["sounds"].items()},
        "recordings": recordings,
        "traffic": {"petrol": "traffic/petrol.wav", "diesel": "traffic/diesel.wav"},
    }
    json.dump(manifest, open(os.path.join(out, "manifest.json"), "w"), indent=1, sort_keys=True)
    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs)
    print("wrote %s: %d sounds, %d synthesised takes, %d recordings, %.1f MB" % (
        out, len(sounds), sum(len(v["files"]) for v in synth["sounds"].values()),
        sum(len(v) for v in recordings.values()), total / 1e6))


if __name__ == "__main__":
    main()
