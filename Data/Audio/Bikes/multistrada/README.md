# Ducati Multistrada V4 S: its own engine sound

beenster's own recording of their Multistrada V4 S on a tour (given to the game 26 Nov 2026, thread
"Bike V3 Sounds"; beenster's own audio, used with their permission).

Cut by `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/multistrada --max-rpm 11500
--idle 59.2 61.4 --rpms 4600,5600,6500,7700,8800,9900`: short stretches of the ride flattened to a steady
pitch and looped (whole engine cycles). `ex_on_<rpm>` were taken while the revs rose (on the throttle),
`ex_off_<rpm>` while they fell (ex_off_3309 is a steady roll at about 3,300 rpm). `loops.json` says where
in the recording each came from.

The recording covers about 3,300 to 10,500 rpm (the harmonics' spacing is one per four-stroke cycle,
rpm / 120: the twin-pulse V4's missing 4th harmonic confirms it). The engine part's
`"sound": "multistrada"` makes EngineSynth play these over that range, scaled to the game's level, and
the made twin-pulse V4 below and above it. The source recording stays out of the repo (project files:
sound/bike_v3/Ducati Multistrada V4 S.wav).
