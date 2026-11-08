"""Copies what the Godot game lays a bike out with into Data/Fitment/layout.json, for the fitting
core's layout solver (Fit/BikeLayout): the 14 styles' eight lines (data/bike_lines.json), the tank
and seat shapes BikeModel draws (TANK_SHAPES, SEAT_SHAPES), the slots and how much each says about a
bike's style (PartsDB), and the 14 reference bikes, one per style, resolved to their parts exactly as
Contracts.style_build resolves them (defaults, the style's own parts and engine, filler bits that
suit it, and tyres that go on its wheels).

    python Tools/import_layout_data.py --godot ../torque-shed

The GDScript stays the source of truth: its constants are read from the scripts as written (they
are plain dictionary and array literals), so a change there is a rerun here. The output is generated
and committed; never edit it by hand.
"""
import argparse
import ast
import json
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Data", "Fitment", "layout.json")


def strip_comments(text):
    out = []
    for line in text.split("\n"):
        s, q = [], None
        for ch in line:
            if q:
                s.append(ch)
                if ch == q:
                    q = None
            elif ch in "\"'":
                q = ch
                s.append(ch)
            elif ch == "#":
                break
            else:
                s.append(ch)
        out.append("".join(s))
    return "\n".join(out)


def balanced(text, start):
    """The bracketed literal starting at text[start] ('{' or '[')."""
    depth, q = 0, None
    for i in range(start, len(text)):
        ch = text[i]
        if q:
            if ch == q:
                q = None
            continue
        if ch in "\"'":
            q = ch
        elif ch in "{[(":
            depth += 1
        elif ch in "}])":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise ValueError("unbalanced literal")


def literal(src):
    src = re.sub(r"\btrue\b", "True", src)
    src = re.sub(r"\bfalse\b", "False", src)
    return ast.literal_eval(src)


def const(script, name):
    text = strip_comments(script)
    m = re.search(r"\bconst %s\b[^=]*=\s*" % re.escape(name), text)
    if not m:
        raise KeyError(name)
    return literal(balanced(text, m.end()))


def style_merges(contracts):
    """Contracts.style_build's match: style -> the parts it merges over the default build."""
    text = strip_comments(contracts)
    body = text[text.index("static func style_build"):]
    body = body[:body.index("b.merge(STYLE_ENGINE")]
    out = {}
    for m in re.finditer(r'"(\w+)":\s*\n\s*b\.merge\(', body):
        out[m.group(1)] = literal(balanced(body, m.end()))
    return out


class Db:
    """Just enough of PartsDB to resolve the reference bikes."""

    def __init__(self, parts_json, retired, scripts):
        self.parts, self.by_kind = {}, {}
        for kind, ids in parts_json.items():
            self.by_kind.setdefault(kind, [])
            for pid, p in ids.items():
                self.parts[pid] = dict(p, id=pid, kind=kind)
                self.by_kind[kind].append(pid)
        # Wheels smallest rim first, then narrowest, then by name (PartsDB.wheel_before).
        self.by_kind.get("wheel", []).sort(key=lambda w: (float(self.parts[w].get("diameter", 17.0)),
                                                          float(self.parts[w].get("rim_width", 0.0)), w))
        self.retired = retired
        pdb = scripts["parts_db"]
        for k in ("SLOTS", "SLOT_KIND", "DEFAULT_BUILD", "OPTIONAL_SLOTS", "FRAME_OWN_SLOTS", "FILLER_SLOTS",
                  "STYLES", "STYLE_WEIGHT", "EMPTY_STYLES", "SADDLE_TAIL", "TAIL_FRONT_GUARD", "MOUNTS"):
            setattr(self, k, const(pdb, k))
        for kind, m in const(pdb, "RETIRED_PARTS").items():
            self.retired.update(m)

    def get(self, pid):
        return self.parts.get(pid, {})

    def styles_of(self, pid):
        return self.get(pid).get("styles", [])

    def may_be_empty(self, slot):
        return slot in self.OPTIONAL_SLOTS or slot in self.FRAME_OWN_SLOTS

    def fitting_tail(self, b):
        saddle = str(self.get(b.get("saddle", "")).get("style", "bench"))
        tail = self.SADDLE_TAIL.get(saddle, self.DEFAULT_BUILD["tail"])
        if tail == "classic_mudguard" and self.get(b.get("rear_tyre", "")).get("style", "") == "offroad":
            tail = "scrambler_loop"
        return tail if tail in self.parts else self.DEFAULT_BUILD["tail"]

    def fitting_front_mudguard(self, b):
        tail = str(self.get(b.get("tail", "")).get("style", "classic"))
        guard = self.TAIL_FRONT_GUARD.get(tail, self.DEFAULT_BUILD["front_mudguard"])
        if self.get(b.get("front_tyre", "")).get("style", "") == "offroad" and tail not in ("enduro", "loop"):
            guard = "enduro_front_fender"
        return guard if guard in self.parts else self.DEFAULT_BUILD["front_mudguard"]

    def sanitize(self, build):
        clean, no_tail, no_guard = {}, False, False
        for slot in self.SLOTS:
            pid = str(build.get(slot, ""))
            pid = str(self.retired.get(pid, pid))
            if pid == "none" and self.may_be_empty(slot):
                clean[slot] = pid
                continue
            if pid not in self.parts or self.parts[pid]["kind"] != self.SLOT_KIND[slot]:
                pid = self.DEFAULT_BUILD[slot]
                no_tail = no_tail or slot == "tail"
                no_guard = no_guard or slot == "front_mudguard"
            clean[slot] = pid
        if no_tail:
            clean["tail"] = self.fitting_tail(clean)
        if no_guard:
            clean["front_mudguard"] = self.fitting_front_mudguard(clean)
        return clean

    def detect_style(self, parts):
        scores = {st: 0.0 for st in self.STYLES}
        total = 0.0
        for slot, w in self.STYLE_WEIGHT.items():
            pid = str(parts.get(slot, "none"))
            tags = self.EMPTY_STYLES.get(slot, []) if pid in ("none", "") else self.styles_of(pid)
            if not tags:
                continue
            total += float(w)
            for t in tags:
                if t in scores:
                    scores[t] += float(w)
        best = ""
        for st in self.STYLES:
            if scores[st] > 0.0 and (best == "" or scores[st] > scores[best] + 0.001):
                best = st
        return best, (scores[best] / total if best and total > 0 else 0.0)

    def dress_fillers(self, parts, seed=0):
        out = dict(parts)
        for slot in self.FILLER_SLOTS:
            out[slot] = "none"
        st, coherence = self.detect_style(out)
        if st == "" or coherence < 0.45:
            return out
        for i, slot in enumerate(self.FILLER_SLOTS):
            fits = [p for p in self.by_kind.get(self.SLOT_KIND[slot], []) if st in self.styles_of(p)]
            if fits:
                out[slot] = fits[abs(seed + i * 7) % len(fits)]
        return out

    def tyre_range(self, wheel):
        p = self.get(wheel)
        r = p.get("tyres", [])
        if len(r) == 2:
            return int(r[0]), int(r[1])
        rim = float(p.get("rim_width", 3.5))
        return int(round(rim * 24.0)), int(round(rim * 62.0))

    def tyre_fit(self, tyre, wheel):
        t, w = self.get(tyre), self.get(wheel)
        if not t or not w:
            return 0
        lo, hi = self.tyre_range(wheel)
        mm = float(t.get("width", 120.0))
        if mm > hi:
            return 1
        if mm < lo:
            return -1
        rims = t.get("rims", [])
        dia = float(w.get("diameter", 17.0))
        if rims and not any(abs(float(d) - dia) < 1e-5 for d in rims):
            return 2
        return 0


def style_build(db, style, merges, engines):
    b = dict(db.DEFAULT_BUILD)
    b.update(merges.get(style, {}))
    b.update(engines.get(style, {}))
    b = db.sanitize(b)
    dressed = db.dress_fillers(b)
    for slot in db.FILLER_SLOTS:
        if str(b[slot]) == "none":
            b[slot] = dressed[slot]
    for ws, ts in (("front_wheel", "front_tyre"), ("rear_wheel", "rear_tyre")):
        if db.tyre_fit(b[ts], b[ws]) != 0:
            fits = [t for t in db.by_kind.get("tyre", []) if db.tyre_fit(t, b[ws]) == 0
                    and db.get(t).get("style", "") != "slick"]
            if fits:
                b[ts] = fits[0]
    return b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.path.join(os.path.dirname(REPO), "torque-shed"))
    a = ap.parse_args()
    g = a.godot
    read = lambda *p: open(os.path.join(g, *p), encoding="utf-8").read()
    scripts = {"parts_db": read("scripts", "parts_db.gd"), "contracts": read("scripts", "contracts.gd"),
               "bike_model": read("scripts", "bike_model.gd")}
    parts_json = json.loads(read("data", "parts.json"))
    retired_path = os.path.join(g, "data", "retired_parts.json")
    retired = json.load(open(retired_path)).get("parts", {}) if os.path.exists(retired_path) else {}
    db = Db(parts_json, retired, scripts)
    merges = style_merges(scripts["contracts"])
    engines = const(scripts["contracts"], "STYLE_ENGINE")
    lines = json.loads(read("data", "bike_lines.json"))
    styles = list(db.STYLES)
    missing = [s for s in styles if s not in merges and s != "classic"]
    builds = {s: style_build(db, s, merges, engines) for s in styles}
    try:
        commit = subprocess.run(["git", "-C", g, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        commit = ""
    out = {
        "about": "What the Godot game lays a bike out with (scripts/bike_rig.gd, bike_model.gd, parts_db.gd, contracts.gd; "
                 "data/bike_lines.json), for Fit/BikeLayout. Written by Tools/import_layout_data.py; do not edit.",
        "source": {"repo": "Beenster99/torque-shed", "commit": commit},
        "styles": styles,
        "style_names": const(scripts["parts_db"], "STYLE_NAMES"),
        "slots": db.SLOTS, "slot_kind": db.SLOT_KIND, "mounts": db.MOUNTS,
        "optional_slots": db.OPTIONAL_SLOTS, "frame_own_slots": db.FRAME_OWN_SLOTS, "filler_slots": db.FILLER_SLOTS,
        "style_weight": db.STYLE_WEIGHT, "empty_styles": db.EMPTY_STYLES,
        "default_build": db.DEFAULT_BUILD,
        "reference_builds": builds,
        "lines": lines.get("styles", lines),
        "tank_shapes": const(scripts["bike_model"], "TANK_SHAPES"),
        "seat_shapes": const(scripts["bike_model"], "SEAT_SHAPES"),
        "retired": db.retired,
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    data = json.dumps(out, indent=1) + "\n"
    if not os.path.exists(OUT) or open(OUT).read() != data:
        open(OUT, "w").write(data)
    print("%d styles (%s without their own parts), %d slots, %d tank shapes; reference bikes:" % (
        len(styles), ", ".join(missing) or "none", len(db.SLOTS), len(out["tank_shapes"])))
    for s in styles:
        b = builds[s]
        print("  %-10s %s / %s / %s / %s" % (s, b["frame"], b["engine"], b["tank"], b["saddle"]))


if __name__ == "__main__":
    main()
