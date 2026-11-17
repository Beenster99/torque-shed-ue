"""Bake the far trees' impostors from the scanned trees (Tools/ue_tree_impostors.py), on their own.

Scene captures render nothing under UnrealEditor-Cmd -unattended (round 154: every view black), so
this runs in the FULL editor, once, before Tools/ue_build_rideuk.py (which then finds the cards made
and plants them as SM_TreeFar_<kind>_imp):

    UnrealEditor.exe TorqueShedProto.uproject -ExecutePythonScript=Tools/ue_bake_impostors.py

It bakes every scanned tree in /Game/ScannedTrees (rebake: -TorqueRebakeImpostors on the command
line), logs "tree impostors:" lines, and closes the editor when done.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_tree_impostors as IMP  # noqa: E402

SCAN_DEST = "/Game/ScannedTrees"


def run():
    rebake = "torquerebakeimpostors" in unreal.SystemLibrary.get_command_line().lower()
    trees = []
    if unreal.EditorAssetLibrary.does_directory_exist(SCAN_DEST):
        for a in unreal.EditorAssetLibrary.list_assets(SCAN_DEST, recursive=True, include_folder=False):
            path = a.split(".")[0]
            if path.rsplit("/", 1)[-1].startswith("SM_"):
                trees.append("%s.%s" % (path, path.rsplit("/", 1)[-1]))
    IMP.log("baking %d scanned trees from %s%s" % (len(trees), SCAN_DEST, " (rebake)" if rebake else ""))
    # (full mips and Nanite data at once, so the captures aren't of half-streamed trees)
    world = IMP._world()
    for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1"):
        unreal.SystemLibrary.execute_console_command(world, cmd)
    cards = IMP.bake_all(sorted(trees), rebake=rebake)
    IMP.log("done: %d of %d baked" % (len([c for c in cards if c]), len(trees)))


run()
unreal.SystemLibrary.quit_editor()
