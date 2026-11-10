# Co-op avatars: MetaHumans in the shed

In co-op, every other player in the shed is shown as a MetaHuman (`Source/TorqueShedProto/Shed/TorqueAvatar.*`). Each player colour gets the next avatar in `Data/Avatars/avatars.json`.

The bodies are posed in code, with no animation assets or Anim Blueprint:

- the feet step with the walk, longer strides when hurrying;
- the arms swing against the legs;
- the hips drop and the knees bend when crouching;
- the back leans forward with speed;
- the head looks where the player looks;
- the hands come up in front while the player is working (their crew-board activity).

The game doesn't load the assembled Blueprint, because its Anim Blueprints (LiveLink, face post-process) need the MetaHuman plugin, which is editor-only. `register` reads the Blueprint's meshes and grooms into `avatars.json` instead, and moves the Blueprint to `/Game/MetaHumanSources`, which is never cooked. It also drops any mesh's post-process Anim Blueprint that needs the plugin, and warns about anything else of the plugin that's still pulled in. In the game, the body is a poseable mesh posed in code, and the face, clothes and hair follow it.

If there's no `avatars.json`, or an avatar fails to load, players keep the shape body as before.

MetaHuman Creator is built into the editor from UE 5.6 and is free. Rigging and the texture download run in Epic's cloud and need an Epic account sign-in (the same one as Fab).

## Making the avatars (on the PC, once)

1. **Plugins:** in Edit > Plugins, search "MetaHuman". Turn on **MetaHuman Creator** and **MetaHuman Character** (and **MetaHuman Core Tech** if it's listed on its own), then restart the editor.
2. **Find out what can be scripted:** run `py Tools/ue_metahumans.py discover` in the Output Log, or headless:
   `UnrealEditor-Cmd.exe TorqueShedProto.uproject -ExecutePythonScript="G:/Dev/torque-shed-ue/Tools/ue_metahumans.py discover" -unattended`.
   Use forward slashes in that path: UE escape-processes backslashes, so `\t` would become a tab.
   It writes `Saved/metahuman_api.txt`. Send that file back, so the making can be fully scripted next time.
3. **Make them by script:** run `py Tools/ue_metahumans.py make`. It creates `Avatar_A` to `Avatar_D` under `/Game/MetaHumanSources` (the built avatars go to `/Game/MetaHumans`), each with a different height, and tries to build each one without the cloud. The log line `can_build_meta_human = False`, with the reason above it, means the build needs a rig. In that case:
   - sign in to your Epic account, then run `py Tools/ue_metahumans.py make rig`. That auto-rigs each one in Epic's cloud (Joints Only), then builds it;
   - if a build still fails, the script exports the preview body mesh instead, and the game poses that body on its own.

   It registers whatever it made at the end. `make rig nowardrobe` does the same with the hair, beard and clothes taken off, which gives whole bodies.

   **Hair and clothes in the game:** `register` lists each groom of the Blueprint (except peach fuzz) with its binding, what it's attached to, and `lod`, which is the first LOD drawn as cards. The game forces that LOD, so no strands are drawn. The Blueprint has no component for the clothes, so `register` takes them from the build folder: skeletal meshes that aren't the body or face, otherwise the Chaos outfit or cloth assets. The game poses them from the body, with cloth simulation off. This needs the `HairStrands`, `ChaosClothAsset` and `ChaosOutfitAsset` plugins, which are on in the `.uproject`. The log lines are `Torque: avatar X: groom ...` and `Torque: avatar X: clothes ...`.

   **Hair as a cards mesh:** the grooms drew only from far away and were bald close up, whatever their LOD (rounds 125 to 129). So the game draws each groom's cards mesh as a plain static mesh on the head bone instead. It uses the game's own `/Game/MetaHumans/Common/M_HairCards`, which `register` makes, because the plugin's hair material is editor-only. The material is masked and two-sided. Its opacity is one channel of the groom's cards atlas (`cards_texture`), and its colour is the avatar's `hair_colour`. Test switches: `-TorqueGroomHair` draws the grooms instead, and `-TorqueHairMask=R|G|B|A` picks the atlas channel used for opacity (the default is G).
4. **Or make them by hand.** Four people is enough for a 4-player crew. For each one:
   - In the Content Browser, go to `/Game/MetaHumanSources/<Name>/` (sources live there and are never cooked; `register` moves any it finds under `/Game/MetaHumans`), right-click and choose **MetaHuman > MetaHuman Character**. Name it after the person and open it.
   - **Presets:** pick a different face for each person, a mix of ages and looks.
   - **Body:** parametric, a different height and build for each (for example 1.70, 1.76, 1.82 and 1.88 m).
   - **Hair:** short styles. Grooms cost GPU, so use the **card** hair LODs if you're offered a choice.
   - **Wardrobe:** workshop clothes from the stock items: work trousers or jeans, a t-shirt or hoodie, and boots or trainers.
   - **Auto-Rig:** choose "Joints and Blend Shapes". Then **Download Texture Sources** at 2K.
   - **Assemble:** pipeline **UE Optimized**, quality **Medium**, output folder `/Game/MetaHumans/<Name>`. This makes `BP_<Name>`.
5. **Register them:** run `py Tools/ue_metahumans.py`. It writes `Data/Avatars/avatars.json`, one line per `BP_*`. Commit that file.
6. **Package:** `/Game/MetaHumans` is already in `DirectoriesToAlwaysCook`, because the game spawns the Blueprints by path. Until avatars exist, the MetaHuman plugins' own content is in `DirectoriesToNeverCook` (the `metahuman-never-cook` block in `Config/DefaultGame.ini`), because it adds about 557 MB to the package even when nothing uses it. When `register` lists avatars, it takes that block out, since their Blueprints may use plugin materials; the character sources stay out, in `/Game/MetaHumanSources`. When there are none, it puts the block back. Commit `DefaultGame.ini` when it changes.

## Checking without a co-op game

Start the shed with `-TorqueAvatarDemo`. A row of the avatars appears 3 m in front of you, each on a 17-second loop:

- walking away at 1.5 m/s, then turning;
- hurrying back at 3 m/s, then turning;
- crouching;
- standing and working, hands out in front;

and looking up and down throughout. The log shows `Torque: N MetaHuman avatars listed` and one `Torque: avatar <name> (...)` line for each avatar built.

## Cost

Each avatar is a MetaHuman at Medium (the face and body together are about 60-100k triangles) plus its hair. Only other players get one; you never see your own.

On disk, the first four built at the default quality added about 1.76 GB to the package. `make` now builds UE Optimized at Medium, with card hair, and caps every texture under `/Game/MetaHumans` at 1024 (`PIPELINE_QUALITY` and `MAX_TEXTURE` at the top of `Tools/ue_metahumans.py`). It also clears an avatar's old build before rebuilding, because `/Game/MetaHumans` is always cooked.

## Your profile: character and face

This works as in Torque Shed: the profile picture at the top right of the Esc menu opens your **Profile**. You can also get there from Esc > Profile, or from Settings > Character > Profile. On the Profile page:

- **Character:** which MetaHuman you are. The default is the one that goes with your colour.
- **Height:** up to 6% shorter or taller than that MetaHuman.
- **Face:** eleven sliders: head size, brow, eye spacing, nose length, width and bridge, cheekbones, lips, jaw width, chin and ears. Each one moves a group of the MetaHuman face's own joints (`FACIAL_*`) from rest. **Reset the face** puts them all back.
- The sliders are drawn as tracks with knobs. Drag one with the mouse, or use Left / Right. The live picture on the right zooms to your face while a face slider is picked, and shows your whole figure for Who and Height. Letting go saves the change and sends it to the others.

The picture is a scene capture of your avatar, posed high above the level, so it doesn't show up in the world. Your choice is saved in `GameUserSettings.ini` under `[TorqueProfile]`. It's sent to everyone in co-op as about 13 bytes (`ATorquePlayerState::Profile`), so the others see your height and face on your avatar.

This doesn't edit the MetaHuman itself: skin, hair and clothes come from the avatars made on the PC. The face sliders are a runtime layer on top. If a slider seems to do nothing, that MetaHuman's face probably has no joints with those names at its quality level.

## On the bike

When avatars exist, you ride as your MetaHuman. The rider rig (`RiderRig`) is still posed for the bike's own seat, bars and pegs, including other bike styles, fitted bars, and geometry-mode seat and peg moves. The MetaHuman then reaches for the same points with its own limbs (`FTorqueAvatar::PoseRiding`):

- its hips where the rig's are, and its back leaning the same way;
- its wrists on the grips, hands turned palm down, and fingers wrapped round the grips, with two fingers over the clutch or brake lever when the rig's are;
- its ankles on the pegs, or down on the road when stopped.

A taller or shorter MetaHuman bends its knees and elbows more or less to fit. The kit body is hidden, and the helmet sits on the MetaHuman's head, with its hair hidden underneath. In first person you look out from that helmet. Start with `-TorqueKitRider` to ride as the old kit rider instead.
