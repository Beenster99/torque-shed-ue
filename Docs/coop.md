# Co-op riding

Ride Buttertubs (and the shed) with friends. This is Phase 1's "co-op on Unreal replication" from the
plan (https://claude.ai/artifact/6RwcD2ChqkkNmGsjZgFk5y), built the way Torque Shed's Godot co-op
works: the host runs the session, and every rider simulates their own bike.

## How it works

| Piece | Where | What it does |
| --- | --- | --- |
| Start menu | `Coop/TorqueMenuPawn.*`, drawn by `TorqueHUD` | Ride solo, Host a co-op ride, Join by address, rides found on this network, Quit. Keys, Xbox pad or DualSense (read directly). Shown when the game is launched plainly; any `-Torque...` test switch or a `play` map option skips it, so hands-off runs work as before. Not shown in Play-in-Editor unless `-TorqueMenu`. |
| Sessions and travel | `Coop/TorqueSession.*` (a GameInstanceSubsystem) | Hosting is a listen server on port 7777. Join by IP (any address, `host:port` too), or pick a ride found on the LAN (Null online subsystem). Moving between the shed and the road (`TorqueFlow::RideOut` / `BackToShed`) is a seamless server travel that takes everyone along. Lost connections bring you back to the menu with the reason. |
| Each rider's bike | `Coop/TorqueNetBike.*` on `ATorqueBikePawn` | Your own bike runs the sim on your machine (no input lag; co-op, so no cheating to guard against). 30 times a second it sends a snapshot: pose, velocity, steering, lean, pitch, both axles (suspension), wheel spin rates, revs, gear, throttle, brakes, lean and weight inputs, tyre slip, crashed. Client to host as an unreliable server call; host to the others as a replicated property that skips its owner. Everyone else's bikes are drawn from those snapshots, so their riders' bodies, levers, clocks, crashes and engine sound all play out. |
| Smoothing | `Coop/NetSmoother.h` (plain C++, tested in `SimTest/net_test.cpp`) | Other bikes are drawn a little behind real time (100 ms, more if the network is jittery, at most 400 ms) on a curve through each pair of snapshots' positions and velocities, so they move smoothly however unevenly snapshots arrive. Gaps are bridged along the last velocity (up to 0.25 s); a respawn is drawn as a jump, not a slide. |
| Weather | `Coop/TorqueGameState.*` | The host owns it (V / D-pad right, or `-TorqueWeather=`) and every machine lights the world the same. A joined rider pressing V is told "The host sets the weather". |
| Who's who | `ATorqueGameMode`, `TorqueHUD` | Riders' names (`-TorqueName=`, else the Windows account name) float over their bikes, with the distance when they're far off. The HUD's blue line shows hosting/joined, riders, ping and each other rider's snapshot rate. |
| Start grid | `ATorqueBikePawn::PlaceForSlot` | Riders start two abreast either side of the line, a row every 7 m behind the first. |
| Sound | `TorqueAudio/RideSound` | Other riders' engines are 3D sounds from their bikes (fading over a few hundred metres), without the birds and with little wind. |

Bandwidth is about 4 KB/s per rider each way; the limits in `DefaultEngine.ini` leave plenty of room
for eight.

### Not yet

- Bikes pass through each other (the wheels only see the land and road). Bike-to-bike contact would
  need the host to decide it; later.
- Everyone rides the same Speed Twin as built on their own machine; each rider's shed build
  (`UTorqueBuildSubsystem`) doesn't travel to the others yet.
- Joining a ride already on the road puts you at the start grid, not next to the others.
- Over the internet the host must forward port 7777 (UDP) on their router, until EOS (below).

## Testing on one PC (the hub's checklist)

Two copies of the packaged game on the same PC, one hosting, one joined to `127.0.0.1`. Windows
Firewall asks the first time the game hosts: allow it on private networks.

1. **Hands-off, both riding by robot** (repeatable numbers):

   ```
   TorqueShedProto.exe /Game/Maps/Buttertubs?listen?play?TorqueName=Host -windowed -ResX=1280 -ResY=720 -TorqueNoInput -TorqueAutoRide -TorqueAutoMph=30 -TorqueNetLog -log
   TorqueShedProto.exe 127.0.0.1?TorqueName=Guest -windowed -ResX=1280 -ResY=720 -WinX=1300 -TorqueNoInput -TorqueAutoRide -TorqueAutoMph=40 -TorqueNetLog -log
   ```

   (Start the second when the first shows the road.) Each writes `Saved/Logs/TorqueShedProto*.log`
   with a `Torque net:` line per second: for this machine's bike `tx 30.0/s, ping N ms, riders 2`, and
   for the other bike `rx`, the arrival spacing (mean and spread), the smoothing delay, how often it
   had to bridge a gap (`extrapolated`) or wait (`held`), and respawn jumps. Good is: tx and rx about
   30/s, extrapolated under 1%, held 0% after the first second, delay 100 ms on localhost.
   Riding at different speeds, the Guest pulls away from the Host, so both see the other ahead or
   behind, out to several km (the bikes are always relevant).
2. **Smoothness by eye**: same, but the Host ridden by hand (no `-TorqueNoInput -TorqueAutoRide`),
   chase camera, keeping close behind the Guest's robot: their bike should move without stutter,
   wheels turning, rider leaning, engine heard from where it is. Screenshot or record both windows.
3. **Rough network**: add `-TorqueNetDelayMs=50` (less smoothing) on the Guest and watch `extrapolated`
   rise; for a real bad-network check, Unreal's packet simulation works in packaged Development builds:
   `-PktLag=120 -PktLagVariance=40 -PktLoss=5` on the Guest.
4. **Crash and respawn**: ride the Host off the road; on the Guest the Host's rider is thrown off and
   slides, and then reappears on the road in one jump (`jumps 1` in the log).
5. **Weather**: V on the Host changes both windows; V on the Guest shows "The host sets the weather".
6. **Menu**: launch with no switches. Host a co-op ride on one copy; on the other, the menu should
   list "Join Host's ride" under the LAN search (or type `127.0.0.1` into Join by address, Ctrl+V
   works). Quit the host: the guest returns to its menu with "Lost the connection to the host".
7. **Shed** (once `Tools/ue_build_shed.py` has built `/Game/Shed/Shed`): Host goes to the shed with the
   guest joining there; both walk about; the host rides out of the roller door and both land on
   Buttertubs still connected. Backspace on the host takes both back to the shed.

When the shed map is built, `GameDefaultMap` in `Config/DefaultEngine.ini` can become
`/Game/Shed/Shed`, so the menu sits over the shed and Ride solo/Host don't load Buttertubs first.

## Epic Online Services: friends join without port forwarding (plan, not built yet)

EOS is free (no fees for any of what's below). It gives two things this needs: a relay/NAT punch-through
so the host doesn't forward ports (EOS P2P), and a list of rides to join (EOS sessions or lobbies)
instead of typing an IP. The game already talks to sessions through Unreal's online subsystem, so the
switch is mostly configuration; the menu's LAN list becomes "rides from friends".

### What beenster does in the Epic Developer Portal (one-off, about 20 minutes)

1. Sign in at https://dev.epicgames.com/portal with your Epic account, and create an **organisation**
   (free; any name, for example "Torque Shed").
2. **Create product**: "Torque Shed". Its *Product settings* show the **Product ID**, and under
   *Sandboxes* the **Dev sandbox ID** and its **Deployment ID**. Copy all three.
3. *Product settings > Clients*: **Add new client policy**, type *Peer2Peer* (it allows Connect, P2P,
   sessions and lobbies). Then **Add new client** using that policy; copy its **Client ID** and
   **Client secret**.
4. *Epic Account Services*: create the application, link the client from step 3, and under
   *Permissions* tick Basic profile, Online presence and Friends. (Brand review is only needed for a
   public release.)
5. Send Claude the five values (Product, Sandbox, Deployment, Client ID, Client secret). They aren't
   passwords for your account, but keep them out of public places; they'll go in a config file that
   stays out of the repository.

### What Claude then changes

- `TorqueShedProto.uproject`: enable `OnlineSubsystemEOS` (it brings `EOSShared` and the EOS socket
  subsystem); keep Null for LAN and as the fallback.
- `Config/DefaultEngine.ini`:

  ```ini
  [OnlineSubsystem]
  DefaultPlatformService=EOS

  [OnlineSubsystemEOS]
  bEnabled=true

  [/Script/OnlineSubsystemEOS.EOSSettings]
  DefaultArtifactName=TorqueShed
  bUseEAS=True
  bUseEOSConnect=True
  bUseEOSSessions=True
  bShouldEnforceBeingLaunchedByEGS=False
  ; +Artifacts=(ArtifactName="TorqueShed",ClientId=...,ClientSecret=...,ProductId=...,SandboxId=...,DeploymentId=...,EncryptionKey=<64 hex>)
  ; in a Config/Custom/ or user config kept out of Git

  [/Script/Engine.GameEngine]
  !NetDriverDefinitions=ClearArray
  +NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemEOS.NetDriverEOS",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

  [/Script/OnlineSubsystemEOS.NetDriverEOS]
  bIsUsingP2PSockets=true
  ```

  (UE 5.6 removed `NetDriverEOSBase` in favour of `NetDriverEOS`; the exact class path gets checked
  against 5.8's plugin when it's built.)
- `UTorqueSession`: log in at start (Epic account through the account portal the first time, then
  remembered with `persistentauth`), create the session with `bIsLANMatch=false`,
  `bUsesPresence=true`, `bUseLobbiesIfAvailable=true`; the menu lists friends' rides
  (`FindFriendSession` / a lobby search) and joins with `JoinSession` + `GetResolvedConnectString`, which
  gives an `EOS:` P2P address instead of an IP. Join by IP and LAN stay for local play.
- Each friend needs a free Epic account (most have one from the Epic Games Store). Until the
  product passes brand review, Epic may only let accounts in the organisation sign in: if so, add the
  friends to the organisation (free) or switch login to EOS Connect's device ID, which needs no Epic
  account at all. Which one applies gets checked on the first EOS build.

Unreal's docs: https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine
