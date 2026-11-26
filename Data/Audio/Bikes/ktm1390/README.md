# KTM 1390: its own tickover

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission; the file is named for the Super Duke 1390, the same LC8 75-degree
V-twin as the game's 1390 Super Adventure S).

The recording is the bike standing: start-up, a long tickover, quick blips. Only the tickover is used:
`Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/ktm1390 --idle 15 28 --idle-rpm 1550 --idle-only`
(1,550 rpm from the firing beat, 12.9 Hz a cycle). The engine part's `"sound": "ktm1390"` plays
`idle_1550.wav` at tickover, handing over to the made V-twin as the revs rise. Source in project files:
sound/bike_v3/KTM Super Duke 1390.wav.
