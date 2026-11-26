# Honda CB1000 GT: its own engine sound

beenster's own recording of the CB1000 GT on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds";
beenster's own audio, used with their permission).

Cut by `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/cb1000gt --per-cycle 4 --line
--min-rpm 3500 --max-rpm 12500 --rpms 4800,5600,6500,7400,8300,9200,10200,10800`. An even-firing flat-plane
four's firing note (every 180 degrees: rpm / 30) rings far above its other harmonics, so the tool follows
that one line. The ride covers about 4,800 to 10,900 rpm (no tickover in it); `loops.json` says where each
loop came from. The engine part's `"sound": "cb1000gt"` plays these over that range and the made inline
four below and above. The source recording stays out of the repo (project files: sound/bike_v3/Honda CB1000GT.wav).
