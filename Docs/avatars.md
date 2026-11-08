# Co-op avatars: MetaHumans in the shed

In co-op, every other player in the shed is shown as a MetaHuman (`Source/TorqueShedProto/Shed/TorqueAvatar.*`). Each player colour gets the next avatar in `Data/Avatars/avatars.json`.

The bodies are posed in code, with no animation assets or Anim Blueprint:

- the feet step with the walk, longer strides when hurrying;
- the arms swing against the legs;
- the hips drop and the knees bend when crouching;
- the back leans forward with speed;
- the head looks where the player looks;
- the hands come up in front while the player is working (their crew-board activity).

The assembled Blueprint's parts (body, face, hair and clothes) all follow a hidden poseable copy of the body.

If there's no `avatars.json`, or an avatar fails to load, players keep the shape body as before.

MetaHuman Creator is built into the editor from UE 5.6 and is free. Rigging and the texture download run in Epic's cloud and need an Epic account sign-in (the same one as Fab).

## Making the avatars (on the PC, once)

1. **Plugins:** in Edit > Plugins, search "MetaHuman". Turn on **MetaHuman Creator** and **MetaHuman Character** (and **MetaHuman Core Tech** if it's listed on its own), then restart the editor.
2. **Find out what can be scripted:** run `py Tools/ue_metahumans.py discover` in the Output Log, or headless:
   `UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_metahumans.py discover" -unattended`.
   Use forward slashes in that path: UE escape-processes backslashes, so `\t` would become a tab.
   It writes `Saved/metahuman_api.txt`. Send that file back, so the making can be fully scripted next time.
3. **Make them by script:** run `py Tools/ue_metahumans.py make`. It creates `Avatar_A` to `Avatar_D` under `/Game/MetaHumans`, each with a different height, and tries to build each one without the cloud. The log line `can_build_meta_human = False`, with the reason above it, means the build needs a rig. In that case:
   - sign in to your Epic account, then run `py Tools/ue_metahumans.py make rig`. That auto-rigs each one in Epic's cloud (Joints Only), then builds it;
   - if a build still fails, the script exports the preview body mesh instead, and the game poses that body on its own.

   It registers whatever it made at the end.
4. **Or make them by hand.** Four people is enough for a 4-player crew. For each one:
   - In the Content Browser, go to `/Game/MetaHumans/<Name>/`, right-click and choose **MetaHuman > MetaHuman Character**. Name it after the person and open it.
   - **Presets:** pick a different face for each person, a mix of ages and looks.
   - **Body:** parametric, a different height and build for each (for example 1.70, 1.76, 1.82 and 1.88 m).
   - **Hair:** short styles. Grooms cost GPU, so use the **card** hair LODs if you're offered a choice.
   - **Wardrobe:** workshop clothes from the stock items: work trousers or jeans, a t-shirt or hoodie, and boots or trainers.
   - **Auto-Rig:** choose "Joints and Blend Shapes". Then **Download Texture Sources** at 2K.
   - **Assemble:** pipeline **UE Optimized**, quality **Medium**, output folder `/Game/MetaHumans/<Name>`. This makes `BP_<Name>`.
5. **Register them:** run `py Tools/ue_metahumans.py`. It writes `Data/Avatars/avatars.json`, one line per `BP_*`. Commit that file.
6. **Package:** `/Game/MetaHumans` is already in `DirectoriesToAlwaysCook`, because the game spawns the Blueprints by path. Until avatars exist, the MetaHuman plugins' own content is in `DirectoriesToNeverCook` (the `metahuman-never-cook` block in `Config/DefaultGame.ini`), because it adds about 557 MB to the package even when nothing uses it. When `register` lists avatars, it takes that block out, since their Blueprints may use plugin materials. When there are none, it puts the block back. Commit `DefaultGame.ini` when it changes.

## Checking without a co-op game

Start the shed with `-TorqueAvatarDemo`. A row of the avatars appears 3 m in front of you, each on a 17-second loop:

- walking away at 1.5 m/s, then turning;
- hurrying back at 3 m/s, then turning;
- crouching;
- standing and working, hands out in front;

and looking up and down throughout. The log shows `Torque: N MetaHuman avatars listed` and one `Torque: avatar <name> (...)` line for each avatar built.

## Cost

Each avatar is a MetaHuman at Medium (the face and body together are about 60-100k triangles) plus its hair. Only other players get one; you never see your own.
