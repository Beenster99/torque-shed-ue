# Kawasaki KLE 500: its own tickover

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission).

The recording is the bike standing: a long tickover, blips, then held revs. Only the tickover is used:
`Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/kle500 --idle 0.5 8.5 --idle-rpm 1300 --idle-only`
(1,300 rpm from the firing beat, 10.8 Hz a cycle). The engine part's `"sound": "kle500"` plays
`idle_1300.wav` at tickover, handing over to the made 180-degree twin as the revs rise.
Source in project files: sound/bike_v3/Kawasaki KLE500.wav.
