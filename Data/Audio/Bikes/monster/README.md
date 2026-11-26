# Ducati Monster: its own tickover

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission).

The recording is the bike standing: a long tickover and a few blips. Only the tickover is used:
`Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/monster --idle 2 9 --idle-rpm 1470 --idle-only`
(1,470 rpm from the firing beat, 12.2 Hz a cycle). The engine part's `"sound": "monster"` plays
`idle_1470.wav` at tickover, handing over to the made 90-degree V-twin as the revs rise.
Source in project files: sound/bike_v3/Ducati Monster.wav.
