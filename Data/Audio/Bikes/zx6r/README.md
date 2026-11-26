# Kawasaki ZX-6R: its own engine sound

beenster's own recording on tour (given to the game 26 Nov 2026, thread "Bike V3 Sounds"; beenster's own
audio, used with their permission).

Cut by `Tools/own_engine_sound.py <recording.wav> Data/Audio/Bikes/zx6r --min-rpm 4500 --max-rpm 16000
--rpms 5500,6500,7600,8800,10200,11800,13500` (the harmonic comb: this four rings on every order, so the
spacing is one per cycle, rpm / 120; its 4th, the firing note, sits on the strongest line). The ride
covers about 5,800 to 12,600 rpm, all on the throttle. The engine part's `"sound": "zx6r"` plays these
over that range, the made inline four (and its kind's tickover) below and above. Source in project
files: sound/bike_v3/Kawasaki ZX6R.wav.
