"""Where things sit on a delivery box or crate, shared by the models (make_props.py, which keeps
these spots clear of battens and the brace) and the game (Data/Props/deliveries.json 'anchors',
written by `python Tools/props/delivery_layout.py`).

All in Torque Shed's frame, metres: x right, y up, z front, origin at the floor centre. A crate's
battens stand 2.2 cm proud of its boards, so anything stuck on goes on the boards, between them,
not on the outer bounds."""

import json
import os

LABEL = (0.16, 0.10)    # courier label, w x h
NOTE = (0.076, 0.07)    # sticky note's face
SKID = 0.08             # crate skid height
BATTEN_T, BATTEN_W = 0.022, 0.07
GAP = 0.02              # clear space kept round anything stuck on


def cardboard(W, H, D):
    top = H - 0.002                 # the body's top (the tape runs over the middle, 5 cm wide)
    lid_z = max(D / 2 - 0.04 - LABEL[1] / 2, 0.025 + GAP / 4 + LABEL[1] / 2)
    return {
        "lid_label": (W / 2 - 0.04 - LABEL[0] / 2, top, lid_z),
        "note": (-min(W / 4, W / 2 - 0.07), H - 0.035 - NOTE[1] / 2, D / 2),
        "arrows": (W / 2 - 0.08, H - 0.11, D / 2),
    }


def crate(W, H, D):
    h = H - SKID
    free_x = W / 2 - BATTEN_W             # boards are bare inside the battens
    top_free = H - BATTEN_W               # under the top front batten
    bottom_free = SKID + BATTEN_W         # over the bottom front batten
    note = (-(free_x - GAP - NOTE[0] / 2), top_free - GAP - NOTE[1] / 2, D / 2)
    label_x = free_x - GAP - LABEL[0] / 2
    stencil_w = min(0.24, W * 0.3)
    return {
        "lid_label": (W / 2 - BATTEN_W - GAP - LABEL[0] / 2, H, D / 2 - 0.04 - LABEL[1] / 2),
        "front_label": (label_x, bottom_free + GAP + LABEL[1] / 2, D / 2),
        "note": note,
        # 'this way up' arrows beside the note, a stencilled band low down left of the label: the
        # long crates' brace runs bottom-left to top-right, so these corners stay clear.
        "arrows": (note[0] + NOTE[0] / 2 + 0.04, top_free - GAP - 0.05, D / 2),
        "stencil": (label_x - LABEL[0] / 2 - GAP - stencil_w / 2, bottom_free + GAP + 0.02, D / 2),
        "stencil_w": stencil_w,
        "h": h,
    }


SIZES = {
    "box_cardboard": ("cardboard", (0.45, 0.35, 0.4)),
    "box_cardboard_long": ("cardboard", (0.95, 0.3, 0.32)),
    "box_cardboard_flat": ("cardboard", (0.7, 0.25, 0.6)),
    "crate_wood": ("crate", (0.75, 0.65, 0.65)),
    "crate_wood_long": ("crate", (1.5, 0.55, 0.6)),
    "crate_wood_flat": ("crate", (1.0, 0.45, 0.85)),
}


def anchors(pid):
    kind, (W, H, D) = SIZES[pid]
    a = (cardboard if kind == "cardboard" else crate)(W, H, D)
    out = {}
    for k in ("lid_label", "front_label", "note"):
        if k in a:
            x, y, z = a[k]
            out[k] = [round(x * 100, 2), round(z * 100, 2), round(y * 100, 2)]   # UE cm: x, y front, z up
    return out


if __name__ == "__main__":
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Data", "Props", "deliveries.json")
    d = json.load(open(path))
    d["anchors"] = {pid: anchors(pid) for pid in SIZES}
    d["anchors_note"] = ("Per variant, in the mesh's own UE frame at its native size (cm; x right, y front, z up, "
                         "origin the floor centre): the point ON the surface where the middle of each thing goes. "
                         "lid_label: flat, its top edge towards -y. front_label (crates): standing on the front, "
                         "facing +y. note: SM_sticky_note's origin (its back face). Scale each anchor by the box's "
                         "scale; don't scale the label or note. Place them exactly there (at most 0.05 cm proud): "
                         "a crate's battens stand 2.2 cm out from its boards, so its bounds are not its surface.")
    text = json.dumps(d, indent=2)
    open(path, "w").write(text + "\n")
    print(json.dumps(d["anchors"], indent=1))
