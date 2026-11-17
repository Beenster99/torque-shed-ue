"""Bake the far trees' impostors from the scanned trees (Tools/ue_tree_impostors.py), on their own.

Scene captures render nothing under UnrealEditor-Cmd -unattended (round 154: every view black), so
this runs in the FULL editor, once, before Tools/ue_build_rideuk.py (which then finds the cards made
and plants them as SM_TreeFar_<kind>_imp):

    UnrealEditor.exe TorqueShedProto.uproject -ExecCmds="py G:/Dev/torque-shed-ue/Tools/ue_bake_impostors.py"

(not -ExecutePythonScript: the editor quits as soon as that script returns, before any tick; round 154)

It bakes every scanned tree in /Game/ScannedTrees (rebake: -TorqueRebakeImpostors on the command
line), logs "tree impostors:" lines, and closes the editor when done.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_tree_impostors as IMP  # noqa: E402

SCAN_DEST = "/Game/ScannedTrees"


class Baker:
    """[round 154] Run from -ExecutePythonScript the bake started before the editor had drawn a frame,
    and every capture came back black. So it waits for the editor to be up (WAIT_TICKS frames, and no
    shaders compiling), then bakes one tree per editor tick, and quits when done."""
    WAIT_TICKS = 300

    def __init__(self):
        self.rebake = "torquerebakeimpostors" in unreal.SystemLibrary.get_command_line().lower()
        self.trees = []
        if unreal.EditorAssetLibrary.does_directory_exist(SCAN_DEST):
            for a in unreal.EditorAssetLibrary.list_assets(SCAN_DEST, recursive=True, include_folder=False):
                path = a.split(".")[0]
                if path.rsplit("/", 1)[-1].startswith("SM_"):
                    self.trees.append("%s.%s" % (path, path.rsplit("/", 1)[-1]))
        self.trees.sort()
        self.ticks = 0
        self.at = -1
        self.done = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        IMP.log("waiting for the editor to draw before baking %d scanned trees%s" % (len(self.trees), " (rebake)" if self.rebake else ""))

    def compiling(self):
        try:
            return unreal.ShaderCompilingLibrary.get_num_remaining_jobs() > 0 if hasattr(unreal, "ShaderCompilingLibrary") else False
        except Exception:
            return False

    def tick(self, dt):
        self.ticks += 1
        if self.at < 0:
            if self.ticks < self.WAIT_TICKS or (self.compiling() and self.ticks < self.WAIT_TICKS * 20):
                return
            try:
                self.setup()
            except Exception as e:
                import traceback
                IMP.log("setup failed, stopping: %s\n%s" % (e, traceback.format_exc()))
                self.stop()
            return
        if self.ticks % 10:
            return   # (a few frames between trees)
        if self.at < len(self.trees):
            try:
                if IMP.bake(self.trees[self.at], self.rebake):
                    self.done += 1
            except Exception as e:
                import traceback
                IMP.log("%s: bake failed: %s\n%s" % (self.trees[self.at], e, traceback.format_exc()))
            self.at += 1
            return
        IMP.log("done: %d of %d baked" % (self.done, len(self.trees)))
        self.stop()

    def stop(self):
        self.at = len(self.trees) + 1
        try:
            unreal.unregister_slate_post_tick_callback(self.handle)
        finally:
            unreal.SystemLibrary.quit_editor()

    def setup(self):
        if True:
            world = IMP._world()
            # (full mips at once, so the captures aren't of half-streamed trees)
            for cmd in ("r.TextureStreaming 0", "r.Streaming.FullyLoadUsedTextures 1"):
                unreal.SystemLibrary.execute_console_command(world, cmd)
            if not unreal.EditorAssetLibrary.does_directory_exist(IMP.IMP_DIR):
                unreal.EditorAssetLibrary.make_directory(IMP.IMP_DIR)
            IMP.master_material()
            self.at = 0
            IMP.log("editor up after %d ticks: baking" % self.ticks)


_baker = Baker()
