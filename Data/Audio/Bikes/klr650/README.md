# Kawasaki KLR 650: its own tickover

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission).

The recording is the bike standing: tickover and a few blips. Only the tickover is used:
`Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/klr650 --idle 7 14 --idle-rpm 1540 --idle-only`
(1,540 rpm from the firing beat, 12.8 Hz a cycle, one firing a cycle on the single). The engine part's
`"sound": "klr650"` plays `idle_1540.wav` at tickover, handing over to the made single as the revs rise.
Source in project files: sound/bike_v3/Kawasaki KLR 650.wav.
