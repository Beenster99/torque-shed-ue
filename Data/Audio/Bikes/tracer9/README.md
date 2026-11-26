# Yamaha Tracer 9 GT: its own tickover

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission).

The recording is the bike standing: start-up, a steady tickover, then repeated blips. Only the tickover is
used: `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/tracer9 --idle 6 12 --idle-rpm 1370
--idle-only` (1,370 rpm: the CP3's firing note at 34.2 Hz, rpm / 40). The engine part's `"sound": "tracer9"`
plays `idle_1370.wav` at tickover, handing over to the made triple as the revs rise. Source in project
files: sound/bike_v3/Yamaha Tracer 9 GT.wav.
