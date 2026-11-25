"""Put the bought bikes' drawings in the game before a PC build (beenster, Nov 25: "I want my friends to be able to ride
it too"). Run it in every round, before packaging. Pure python, no Blender needed.

    python Tools/bought_bikes.py              copies each bike in Data/BikesV2/bought.json from its "source" on this PC
                                              into Data/BikesV2/<model>.tsb (scrambled: Fit/BoughtFile.h), and removes any
                                              plain Data/BikesV2/<model>.glb so a bare model file never ships
    python Tools/bought_bikes.py --check DIR  checks a staged/packaged build folder has every bought bike's .tsb in it

A bought model is never committed: its licence lets it be used inside the game, not handed out as a file. A missing
source or a build without the bike STOPS here with "BOUGHT BIKE MISSING" (exit code 2): never ship without it quietly.
The game falls back to drawing the bike from its catalogue parts only for a build that really hasn't got it.
"""
import hashlib
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(REPO, "Data", "BikesV2")
MAGIC = b"TSB1"


def scramble(raw):
    """Fit/BoughtFile.h BoughtXor: every byte xor'd with a xorshift32 stream (the same call undoes it)."""
    out = bytearray(raw)
    x = 0x2545F491
    for i in range(len(out)):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] ^= x & 0xFF
    return bytes(out)


def bikes():
    return {k: v for k, v in json.load(open(os.path.join(DATA, "bought.json"))).items() if not k.startswith("_")}


def fail(msg):
    print("BOUGHT BIKE MISSING: " + msg)
    print("The build must not go on without it (tell the hub thread). The game glb is made from the bought .blend by "
          "BikesV2/foundry/bought_multistrada.py (cloud: pip install bpy).")
    sys.exit(2)


def put():
    for model, b in bikes().items():
        src = b["source"]
        if not os.path.exists(src):
            fail(f"{b['name']}: {src} is not on this PC")
        raw = open(src, "rb").read()
        if raw[:4] != b"glTF":
            fail(f"{b['name']}: {src} is not a glb")
        sha = hashlib.sha256(raw).hexdigest()
        if b.get("sha256") and sha != b["sha256"]:
            print(f"NOTE {b['name']}: {src} is not the exported copy bought.json names ({sha[:12]} vs {b['sha256'][:12]}): using it anyway")
        if not os.path.exists(os.path.join(DATA, model + ".json")):
            fail(f"{b['name']}: Data/BikesV2/{model}.json (its points and surfaces, in git) is missing")
        dst = os.path.join(DATA, model + ".tsb")
        data = MAGIC + scramble(raw)
        if not os.path.exists(dst) or open(dst, "rb").read() != data:
            open(dst, "wb").write(data)
        plain = os.path.join(DATA, model + ".glb")
        if os.path.exists(plain):
            os.remove(plain)
        print(f"bought bike {b['name']}: Data/BikesV2/{model}.tsb ({len(data) // 1024} KB) OK")


def check(folder):
    found = {}
    for root, _dirs, files in os.walk(folder):
        for f in files:
            found.setdefault(f, []).append(os.path.join(root, f))
    for model, b in bikes().items():
        for ext in (".tsb", ".json"):
            hits = [p for p in found.get(model + ext, []) if os.path.basename(os.path.dirname(p)) == "BikesV2"]
            if not hits:
                fail(f"{b['name']}: no BikesV2/{model}{ext} in {folder}")
        if found.get(model + ".glb"):
            fail(f"{b['name']}: a plain {model}.glb is in {folder}: only the scrambled .tsb may ship")
        print(f"bought bike {b['name']}: in the build OK")


if __name__ == "__main__":
    if "--check" in sys.argv:
        check(sys.argv[sys.argv.index("--check") + 1])
    else:
        put()
