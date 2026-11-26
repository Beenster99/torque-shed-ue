# Ducati Panigale V4 S: its own tickover

beenster's own recording of the Panigale on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds";
beenster's own audio, used with their permission).

The recording is the bike standing: start-up, a long tickover, then quick blips. The blips rise and fall
too fast to loop, so only the tickover is used: `Tools/own_engine_sound.py <recording.wav>
Data/Audio/Bikes/panigale --idle 15 21 --idle-rpm 1350 --idle-only` (1,350 rpm from the firing beat, the
twin-pulse V4's cycle at 11.3 Hz). The engine part's `"sound": "panigale"` plays `idle_1350.wav` at
tickover, handing over to the made twin-pulse V4 as the revs rise. The source recording stays out of the
repo (project files: sound/bike_v3/Panigale V4.wav).
