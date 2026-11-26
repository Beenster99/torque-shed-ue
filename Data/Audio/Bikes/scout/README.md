# Indian Scout 101: its own engine sound

beenster's own recording of the Scout on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds";
beenster's own audio, used with their permission).

Cut by `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/scout --min-rpm 900 --max-rpm 8300
--rpms 3000,3600,4300,5000,5800,6700,7600`. `ex_on_<rpm>` were taken while the revs rose, `ex_off_<rpm>`
while they fell; `loops.json` says where each came from. The recording has no clean tickover (the stops
are wind and road noise), so it covers about 2,900 to 5,200 rpm, where the gears were changed. The engine
part's `"sound": "scout"` plays these over that range and the made 60-degree V-twin below and above.
The source recording stays out of the repo (project files: sound/bike_v3/Indian Scout.wav).
