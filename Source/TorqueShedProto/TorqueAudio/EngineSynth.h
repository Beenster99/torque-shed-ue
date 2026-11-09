// Torque Shed's engine sound (scripts/engine_synth.gd, engine_bank.gd and engine_sound.gd), in plain
// C++ with no Unreal types so SimTest can render it (SimTest/engine_test.cpp).
//
// Nothing is recorded but the idle: every bike's sound is built from how its engine works. Each
// cylinder fires on its own crank angle (a 270 twin's off-beat, a V-twin's lope, a triple's 240
// warble, a flat-plane four's scream, a crossplane's growl), each firing sends a pulse down its
// header which rings at the pipe's length, the pulses meet in the collector and go through the
// silencer (a stock can soaks them up, a straight pipe lets them crack out). The intake side has
// its own pulses through the runners into the airbox, and a mechanical layer of valve ticks, a dry
// clutch's rattle and a two-stroke's ring-ding.
//
// As in Torque Shed the sound is baked: for a ladder of engine speeds (buckets), on the throttle and
// off it, a short seamless loop for the exhaust side and one for the engine side (FBank, on a worker
// thread, the buckets nearest idle first). FVoice plays them, crossfading and pitching between the
// two buckets either side of the revs, with a real bike's idle recorded for each kind of engine
// (Data/Audio/TS/engines/idle) at tickover, pops on the overrun and the engine catching.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace TorqueEngine
{
constexpr int Rate = 32000;   // the baked loops' rate

// EngineSynth.make_spec's recipe.
struct FSpec
{
	std::string Layout = "parallel";   // single, twin, parallel, vtwin, boxer, triple, inline, 2stroke
	int Cylinders = 2;
	double Cc = 900.0;
	std::string Crank;                 // "" for the layout's usual
	double Idle = 1200.0, Redline = 10000.0, Peak = 0.0;
	std::string Exhaust = "slip";      // stock, slip, full, race, straight
	double Loud = 0.8;
	std::string Intake = "airbox";
	std::string Shift = "click";       // slick, click, clunk
	bool Quickshifter = false, DryClutch = false;
	double Rust = 0.0, Misfire = 0.0;
	bool Kick = false;
	int Valves = 4;
};

// Fills in what's missing the way make_spec does (the layout from the cylinders, its usual crank,
// the peak at three quarters of the redline, a known exhaust style).
FSpec MakeSpec(FSpec S);
// Everything that changes the baked sound, as a key for sharing banks between bikes.
std::string Signature(const FSpec& S);
bool IsTwoStroke(const FSpec& S);
double CycleDegrees(const FSpec& S);
// [crank degrees into the cycle, header group] for each cylinder.
std::vector<std::pair<double, int>> FiringEvents(const FSpec& S);
double FiringHz(const FSpec& S, double Rpm);
std::vector<double> BucketRpms(const FSpec& S);

// One baked loop: Layer 0 ex_on, 1 ex_off, 2 in_on, 3 in_off, at the bucket's exact rpm (LoopRpm).
std::vector<float> RenderLoop(const FSpec& S, int Layer, double Rpm, int Seed);
double LoopRpm(const FSpec& S, double Rpm);
// A pop on the overrun (Strength 1), a downshift bang (2.5) or a backfire crack (4).
std::vector<float> RenderPop(const FSpec& S, double Strength, int Seed);
// The engine catching off the starter or the kick (and the starter motor freewheeling to a stop).
std::vector<float> RenderCatch(const FSpec& S, bool bKick, int Seed);
// Turning it over: on the button, or kicked.
std::vector<float> RenderStarter(const FSpec& S, bool bKick, double Seconds, int Seed);
// Overrun pops a second at Rpm, OverrunS seconds after the throttle shut (EngineSound.pop_rate).
double PopRate(const FSpec& S, double Rpm, double OverrunS);

// A mono 16-bit WAV at any rate (the idle recordings, the recorded loops).
std::vector<float> ReadWav(const std::vector<uint8_t>& Bytes, int& SampleRate);

enum ELayer { ExOn, ExOff, InOn, InOff, Layers };

// One engine's baked sounds, shared by every bike with the same signature.
class FBank
{
public:
	// Reads a file's bytes (the game's: from the project's Data; the tests': from disk). Empty if none.
	using FReader = std::function<std::vector<uint8_t>(const std::string& Rel)>;
	// Lists the files in a folder under Data (names only).
	using FLister = std::function<std::vector<std::string>(const std::string& Rel)>;

	// The bank for this recipe: shared, made (not baked) if it's new. Recordings are looked for
	// under Data/Audio/TS/engines: a folder of recorded loops for the engine (2stroke_1), and its
	// idle (idle/<key>_<rpm>.wav).
	static std::shared_ptr<FBank> For(const FSpec& S, const FReader& Read, const FLister& List);

	explicit FBank(const FSpec& S);
	// Bakes every bucket (idle first) and the one-shots. Any thread, once; Baked() says how far it is.
	void Bake();
	bool Started() const { return bStarted.load(); }
	bool Finished() const { return bFinished.load(); }
	// Claims the bake for the caller (false if someone already has).
	bool Claim() { bool Expect = false; return bStarted.compare_exchange_strong(Expect, true); }

	FSpec Spec;
	std::string Sig;
	std::vector<double> Rpms;
	struct FBucket
	{
		std::array<std::vector<float>, Layers> Loop;
		std::atomic<bool> bReady{false};
	};
	std::vector<std::unique_ptr<FBucket>> Buckets;
	// One-shots: pop (4), bang (2), crack (1), catch (2), starter (3). Ready once bShotsReady.
	std::vector<std::vector<float>> Pops, Bangs, Cracks, Catches, Starters;
	std::atomic<bool> bShotsReady{false};
	// Recorded loops (2stroke_1): their own rate, the exhaust carrying the whole bike when there
	// are no engine-side ones.
	bool bRecorded = false, bExhaustOnly = false;
	int LoopRate = Rate;
	// A real bike of this kind ticking over, or empty: at its own rate, recorded at IdleRecRpm, its
	// RMS; and the made idle's, exhaust and engine side, off the throttle (once that bucket's baked).
	std::vector<float> IdleRec;
	int IdleRecRate = 44100;
	double IdleRecRpm = 0.0, IdleRecRms = 0.0;
	std::atomic<double> IdleMadeEx{0.0}, IdleMadeIn{0.0};
	int IdleBucket = -1;
	double BakeMs = 0.0;

	std::vector<int> BakeOrder() const;

private:
	std::atomic<bool> bStarted{false}, bFinished{false};
	void LoadRecorded(const std::string& Dir, const FReader& Read, const FLister& List);
	void LoadIdle(const FReader& Read, const FLister& List);
	void BakeShots();
};

// A bike's engine as you hear it (EngineSound): the bank's loops crossfaded and pitched to the revs
// and throttle, the recorded idle at tickover, overrun pops, the catch. Renders at OutRate, mono.
// Set and Render on the audio thread's side (the caller keeps them on one thread, or locks).
class FVoice
{
public:
	FVoice(std::shared_ptr<FBank> InBank, int InOutRate);

	struct FInputs
	{
		double Rpm = 1000.0;
		double Throttle = 0.0;
		bool bRunning = true;
		double Inside = 0.0;   // 0 heard from behind the bike (the chase camera) .. 1 in the helmet
		bool bRemote = false;  // someone else's bike: mostly pipe
		bool bCut = false;     // the ignition cut for a gear change
	};
	void Set(const FInputs& In) { Want = In; }
	// The engine's just caught (off the starter or the kick).
	void Catch();
	// Adds N samples of the engine to Out.
	void Render(float* Out, int N);

	const FBank& Bank() const { return *BankRef; }
	int Pops = 0, Catches = 0;
	double IdleShare = 0.0;
	int Sounding() const;   // loops playing (tests)

private:
	std::shared_ptr<FBank> BankRef;
	int OutRate;
	FInputs Want;
	std::mt19937 Rng{4242};
	double Uni(double A, double B) { return std::uniform_real_distribution<double>(A, B)(Rng); }

	struct FPlayer
	{
		int Layer = 0, Bucket = -1;
		double Pos = 0.0, Pitch = 1.0, Gain = 0.0, Target = 0.0;
		bool bWanted = false, bOn = false;
	};
	std::vector<FPlayer> Players;
	struct FShot
	{
		const std::vector<float>* S = nullptr;
		double Pos = 0.0, Step = 1.0, Gain = 1.0;
	};
	std::vector<FShot> Shots;
	double IdlePos = 0.0, IdleGain = 0.0, IdleStep = 1.0, IdleTarget = 0.0;
	double Load = 0.0, Level = 0.0, Blip = 0.0, CatchT = 1.0, Overrun = 0.0;
	int Block = 0;
	void Update(double Dt);
	void PlayShot(const std::vector<std::vector<float>>& From, double Gain, double Pitch);
	static double At(const std::vector<float>& S, double Pos);
};
} // namespace TorqueEngine
