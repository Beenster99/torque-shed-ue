#!/usr/bin/env python3
"""The prototype's recorded sounds, from the Godot game (Torque Shed's audio folder), as 48 kHz mono
16-bit WAVs in Data/Audio that the game reads itself:

    idle_parallel270.wav   a 270-degree parallel twin at tickover (recorded at about 1319 rpm)
    gearclunk1..8.wav      Simon's gear lever clunks
    birds_moor.wav         moorland birds, the ride's background

Needs ffmpeg on the PATH.
    python Tools/make_sounds.py --torque-shed ../torque-shed
"""
import argparse, json, os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

SOUNDS = {
    "idle_parallel270": "audio/engines/idle/parallel_270_1319.wav",
    "birds_moor": "audio/place/birds_moor.ogg",
}
SOUNDS.update({"gearclunk%d" % i: "audio/sfx/gearclunk%d.ogg" % i for i in range(1, 9)})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--torque-shed", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    ap.add_argument("--out", default=os.path.join(REPO, "Data", "Audio"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, rel in SOUNDS.items():
        src = os.path.join(args.torque_shed, rel)
        dst = os.path.join(args.out, name + ".wav")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", "48000", "-sample_fmt", "s16",
                        "-map_metadata", "-1", "-bitexact", dst], check=True)
        print(name, os.path.getsize(dst) // 1024, "KB")
    json.dump({"idle_rpm": 1319, "sample_rate": 48000}, open(os.path.join(args.out, "audio.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
