// The ride's sound, made sample by sample in plain C++ (no Unreal types, so SimTest can render it):
// the engine as a 270-degree parallel twin (each firing rings the pipe's resonances, the uneven
// 270/450 gaps give the twin its lope), the recorded idle under it at tickover, pops on the
// overrun, a gear lever clunk on every change, wind with speed, tyre squeal and scrub when a tyre
// slides, and moorland birds behind it all. Mono, 48 kHz.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace TorqueSound
{
// A mono 16-bit PCM WAV, as floats -1..1 (empty if it isn't one). SampleRate is set from the file.
std::vector<float> ReadWav(const std::vector<uint8_t>& Bytes, int& SampleRate);

struct RideInputs
{
	double Rpm = 950.0;
	double Throttle = 0.0;      // what the engine gets, 0..1
	double Speed = 0.0;         // m/s
	double RearSlide = 0.0;     // 0 gripping .. 1 locked or spinning hard (tarmac only)
	double FrontSlide = 0.0;
	double SideSlide = 0.0;     // sliding sideways, 0..1
	bool OnTarmac = true;
	bool Airborne = false;
	double Inside = 0.0;        // 0 heard from behind the bike .. 1 in the rider's helmet
};

class RideSound
{
public:
	static constexpr int Rate = 48000;

	double IdleRpm = 950.0;
	double Volume = 0.8;

	void SetIdle(std::vector<float> Samples, int SampleRate, double RecordedRpm);
	void AddClunk(std::vector<float> Samples, int SampleRate);
	void SetAmbience(std::vector<float> Samples, int SampleRate);

	void Set(const RideInputs& In) { Target = In; }
	void GearChange(bool Up);   // the lever, and the ignition cut while it changes

	// N samples into Out (float -1..1).
	void Render(float* Out, int N);

	// For tests: firings so far, pops so far.
	int Firings = 0, Pops = 0;

private:
	struct Biquad
	{
		double B0 = 0, B1 = 0, B2 = 0, A1 = 0, A2 = 0, Z1 = 0, Z2 = 0;
		void BandPass(double Freq, double Q);
		void LowPass(double Freq, double Q);
		double Run(double X)
		{
			const double Y = B0 * X + Z1;
			Z1 = B1 * X - A1 * Y + Z2;
			Z2 = B2 * X - A2 * Y;
			return Y;
		}
	};

	RideInputs Target, Now;
	std::mt19937 Rng{12345};
	double Noise() { return std::uniform_real_distribution<double>(-1.0, 1.0)(Rng); }
	double Uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(Rng); }

	// Engine.
	double Crank = 0.0;               // degrees, 0..720
	int NextFire = 0;                 // 0: the firing at 0 degrees, 1: at 270
	double Pulse = 0.0, PulseNoise = 0.0, PulseDecay = 0.0, PulseAmp = 0.0;
	double Cut = 0.0;                 // seconds left of the ignition cut
	double Overrun = 0.0;             // seconds since the throttle shut with the revs up
	double PopEnv = 0.0;
	Biquad Pipe[4], Intake, Crackle, Tone;
	double Hp = 0.0, HpIn = 0.0;      // DC block
	double MechPhase = 0.0;

	// Recorded idle, looped with its end crossfaded into its start.
	std::vector<float> Idle;
	double IdleRate = 1.0, IdleRpmRec = 1319.0, IdlePos = 0.0;

	// Clunks (one plays at a time), and the background.
	std::vector<std::vector<float>> Clunks;
	const std::vector<float>* Playing = nullptr;
	double ClunkPos = 0.0, ClunkGain = 0.0;
	int LastClunk = -1;
	std::vector<float> Amb;
	double AmbPos = 0.0;

	// Wind and tyres.
	Biquad WindLp, WindBp, Squeal, Scrub;
	double SquealPhase = 0.0;

	void Retune();
	int RetuneIn = 0;
	static std::vector<float> Resample(const std::vector<float>& In, int From);
	static double Sample(const std::vector<float>& S, double Pos);
};
} // namespace TorqueSound
