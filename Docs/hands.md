# Hands and tools in first person

Your own avatar is drawn in first person, and its hands hold the tool. The tool models are detailed meshes generated without Blender.

## Tools (Data/Tools)

`Tools/props/make_hand_tools.py` builds the six tools from distance fields (`Tools/props/sdf_kit.py`):
- a 13 mm combination spanner
- a wire brush
- a 115 mm angle grinder
- an aerosol can
- a gravity-feed spray gun
- a pressure washer gun and lance

It meshes them with marching cubes, or as exact surfaces of revolution where a part is round. It writes:

- `Data/Tools/tool_<id>.tmesh`: the game reads these at run time (`TorqueToolModels.cpp` `ReadTMesh`), so there is no editor import. The format is described in `sdf_kit.Model.write_tmesh`.
- `Data/Tools/tools.json`: for each tool:
  - its material slots: a shed prop material `MI_Prop_<prop>` with its tiling, or a plain finish. `paint` slots take the spray colour.
  - the nozzle
  - the first-person `hold`: where the right fist sits in the camera's frame (cm), and the tool's x and z axes there.
  - the `grips`: the bar through each fist, running from the little finger to the index (`axis`), the way the knuckles point (`aim`), the bar's `radius`, any fingers on a `trigger`, and `index: top` for the spray can.

To regenerate, run `pip install scikit-image fast_simplification`, then `python Tools/props/make_hand_tools.py [ids] [--glb DIR]`. `--glb` also writes preview glbs. The tools carry no makers' names or logos.

If `Data/Tools` is missing, the walker draws Torque Shed's primitive tools at Torque Shed's hold, as before.

## Hands (FTorqueAvatar)

- `FTorqueAvatarInput::Grip[2]` takes a hand round a bar. `HandOnBar` turns the hand and places its wrist. `WrapFingers` curls each finger joint until it touches the bar, then tucks the thumb round. The riding grips use the same two functions.
- Trigger fingers open to hook the trigger and close as you work (`TriggerPull` = the walker's `WorkBlend`). On the spray can, the index presses the head.
- After `Pose`, `GripReached` says where the bar really ended up. If the arm can't reach the hold, the walker moves the tool to the hand, so the tool is never left floating.

## Your own body (ATorqueWalker)

- The local player's avatar is built like the others'. Its face and hair are hidden from you (`SetOwnerView`, owner-no-see) but still cast shadows.
- The body stands `BodyBack` (6 cm) behind the camera. The camera sits at the body's eyes (`EyeWorld`), taken from the face's `FACIAL_L_Eye` and `FACIAL_R_Eye` joints, and is eased each frame.
- `-TorqueNoBody` turns your own body off. You then get the floating tool, the detailed model, and Torque Shed's eye height.
- The log says `Torque: hands on tool N: right hand X cm short of the hold` once per tool. 0 means the hold is in reach. Adjust `hold` in tools.json if a hand comes up short.
