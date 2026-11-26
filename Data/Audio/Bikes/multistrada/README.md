# Ducati Multistrada V4 S: its own engine sound

beenster's own recording of their Multistrada V4 S on a tour (given to the game 26 Nov 2026, thread
"Bike V3 Sounds"; beenster's own audio, used with their permission).

Cut by `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/multistrada --idle 59.2 61.4
--rpms 2300,2800,3300,3900,4500,5050`: short stretches of the ride flattened to a steady pitch and looped
(whole engine cycles). `ex_on_<rpm>` were taken while the revs rose (on the throttle), `ex_off_<rpm>`
while they fell or at tickover. `loops.json` says where in the recording each came from.

The recording covers about 1,650 to 5,300 rpm. The engine part's `"sound": "multistrada"` makes
EngineSynth play these up to there and the made twin-pulse V4 above (matched to the recording's level).
The source recording itself stays out of the repo (project files: Ducati Multistrada V4 S.wav).
