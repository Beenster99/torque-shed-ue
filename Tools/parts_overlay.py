"""This repo's own changes to the Godot parts catalogue, kept in Data/Parts/Redrawn/parts_overlay.json
so a re-import from the Godot game keeps them: tanks beenster drew for this game that the Godot game
does not have, the old ones they replace, and which tank each style's reference bike carries.

    {"add":        {kind: {id: part}},          new parts, in the catalogue's own fields
     "remove":     {kind: [id, ...]},           parts taken off sale (old saves move to "retired")
     "drop_styles":{kind: {id: [style, ...]}},  styles a part no longer suits
     "retired":    {old id: new id},            what a removed part becomes in an old save
     "reference":  {style: {slot: id}}}         reference-bike parts, where the Godot pick is gone

import_parts.py applies it to Data/Parts/parts.json and import_layout_data.py to the catalogue it
resolves the reference bikes from. Run on its own, it applies itself to the committed
Data/Parts/parts.json and Data/Fitment/layout.json (both are idempotent):

    python Tools/parts_overlay.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PATH = os.path.join(REPO, "Data", "Parts", "Redrawn", "parts_overlay.json")
PARTS = os.path.join(REPO, "Data", "Parts", "parts.json")
LAYOUT = os.path.join(REPO, "Data", "Fitment", "layout.json")


def load():
    return json.load(open(PATH)) if os.path.exists(PATH) else {}


def apply_catalogue(cat, ov=None):
    ov = load() if ov is None else ov
    for kind, ids in ov.get("remove", {}).items():
        for pid in ids:
            cat.get(kind, {}).pop(pid, None)
    for kind, m in ov.get("drop_styles", {}).items():
        for pid, styles in m.items():
            p = cat.get(kind, {}).get(pid)
            if p is not None:
                p["styles"] = [s for s in p.get("styles", []) if s not in styles]
    for kind, parts in ov.get("add", {}).items():
        cat.setdefault(kind, {}).update(parts)
    return cat


def apply_layout(lay, ov=None):
    ov = load() if ov is None else ov
    lay.setdefault("retired", {}).update(ov.get("retired", {}))
    for style, slots in ov.get("reference", {}).items():
        lay["reference_builds"].setdefault(style, {}).update(slots)
    return lay


def main():
    ov = load()
    cat = json.loads(open(PARTS, encoding="utf-8").read())
    data = json.dumps(apply_catalogue(cat, ov), indent="\t", ensure_ascii=False) + "\n"
    if open(PARTS, encoding="utf-8").read() != data:
        open(PARTS, "w", encoding="utf-8").write(data)
    lay = json.load(open(LAYOUT))
    data = json.dumps(apply_layout(lay, ov), indent=1) + "\n"
    if open(LAYOUT).read() != data:
        open(LAYOUT, "w").write(data)
    print("overlay: %d added, %d removed, %d reference parts" % (
        sum(len(v) for v in ov.get("add", {}).values()), sum(len(v) for v in ov.get("remove", {}).values()),
        sum(len(v) for v in ov.get("reference", {}).values())))


if __name__ == "__main__":
    main()
