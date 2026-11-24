// [Sound Pack 1, beenster Nov 24] Checks the pack's takes in the ride's sound (RideSound) and writes
// before/after listening files: a hard stop on a dry road, a burnout, the same stop on a wet road (no
// squeal), a town stopped at its middle (a bike started up a street or two off) and the paddock of a circuit.
//   pack1_test [path to Data/Audio] [folder for the listening WAVs, or nothing for none]
#include "RideSound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace TorqueSound;

static std::vector<float> Load(const std::string& Path, int& Rate)
{
	std::ifstream F(Path, std::ios::binary);
	std::vector<uint8_t> B((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
	return ReadWav(B, Rate);
}

static void Save(const std::string& Path, const std::vector<float>& S)
{
	std::ofstream F(Path, std::ios::binary);
	auto U32 = [&F](uint32_t V) { F.write((const char*)&V, 4); };
	auto U16 = [&F](uint16_t V) { F.write((const char*)&V, 2); };
	F.write("RIFF", 4); U32(36 + (uint32_t)S.size() * 2); F.write("WAVEfmt ", 8);
	U32(16); U16(1); U16(1); U32(RideSound::Rate); U32(RideSound::Rate * 2); U16(2); U16(16);
	F.write("data", 4); U32((uint32_t)S.size() * 2);
	for (float V : S) { const int16_t I = (int16_t)std::lround(std::clamp(V, -1.0f, 1.0f) * 32767.0f); F.write((const char*)&I, 2); }
}

static void Base(RideSound& S, const std::string& Dir)
{
	int R = 0;
	auto Idle = Load(Dir + "/idle_parallel270.wav", R);
	if (!Idle.empty()) S.SetIdle(Idle, R, 1319.0);
	for (int I = 1; I <= 8; ++I)
	{
		auto C = Load(Dir + "/gearclunk" + std::to_string(I) + ".wav", R);
		if (!C.empty()) S.AddClunk(C, R);
	}
	auto Amb = Load(Dir + "/birds_moor.wav", R);
	if (!Amb.empty()) S.SetAmbience(Amb, R);
}

// The pack, as TorqueBikePawn::StartSound loads it. Returns how many takes it found.
static int Pack(RideSound& S, const std::string& Dir)
{
	int R = 0, Got = 0;
	for (int I = 1; I <= 3; ++I)
	{
		auto T = Load(Dir + "/Pack1/tyre_squeal_" + std::to_string(I) + ".wav", R);
		if (!T.empty()) { S.AddSqueal(T, R); ++Got; }
	}
	for (int I = 1; I <= 4; ++I)
	{
		for (const char* Town : {"town_twin", "town_vtwin"})
		{
			auto T = Load(Dir + "/Pack1/" + Town + "_" + std::to_string(I) + ".wav", R);
			if (!T.empty()) { S.AddDistant(RideSound::DistantTown, T, R); ++Got; }
		}
		for (const char* Paddock : {"circuit_rev", "circuit_v4"})
		{
			auto T = Load(Dir + "/Pack1/" + Paddock + "_" + std::to_string(I) + ".wav", R);
			if (!T.empty()) { S.AddDistant(RideSound::DistantCircuit, T, R); ++Got; }
		}
	}
	return Got;
}

struct Result { std::vector<float> Wav; double Rms = 0.0, Peak = 0.0; int Squeals = 0, Bikes = 0; bool Bad = false; };

static Result Render(const std::string& Dir, bool WithPack, double Secs, const std::function<RideInputs(double)>& Fn)
{
	RideSound S;
	Base(S, Dir);
	if (WithPack) Pack(S, Dir);
	Result Out;
	const int Block = 480;
	const int Blocks = (int)(Secs * 100);
	double Sum = 0.0;
	for (int B = 0; B < Blocks; ++B)
	{
		S.Set(Fn(B / 100.0));
		float Buf[Block];
		S.Render(Buf, Block);
		for (float V : Buf)
		{
			if (!std::isfinite(V)) Out.Bad = true;
			Sum += V * V; Out.Peak = std::max(Out.Peak, (double)std::fabs(V));
			Out.Wav.push_back(V);
		}
	}
	Out.Rms = std::sqrt(Sum / std::max<size_t>(Out.Wav.size(), 1));
	Out.Squeals = S.Squeals;
	Out.Bikes = S.DistantBikes;
	return Out;
}

int main(int Argc, char** Argv)
{
	const std::string Dir = Argc > 1 ? Argv[1] : "../Data/Audio";
	const std::string Listen = Argc > 2 ? Argv[2] : "";
	{
		RideSound Probe;
		const int Got = Pack(Probe, Dir);
		std::printf("Sound Pack 1: %d takes found (want 19)\n", Got);
		if (Got != 19) { std::printf("FAIL: Data/Audio/Pack1 incomplete (Tools/sound_pack1.py makes it)\n"); return 1; }
	}
	int Fails = 0;
	// A hard stop from 90 km/h, the back wheel locked from 0.4 s.
	auto Stop = [](double Wet) {
		return [Wet](double T) {
			// (on the wet road it's been raining a while: the road's water is let settle before the stop)
			const double From = Wet > 0.0 ? 3.0 : 0.0;
			const double Ts = std::max(T - From, 0.0);
			RideInputs I;
			I.Speed = std::max(25.0 - 8.0 * Ts, 0.0);
			I.Rpm = std::max(4000.0 - 900.0 * Ts, 1000.0);
			const double Lock = Ts > 0.4 && I.Speed > 0.5 ? 1.0 : 0.0;
			I.RearSlide = Lock; I.SlipSpeed = Lock * I.Speed; I.RoadWet = Wet;
			return I;
		};
	};
	// A burnout: still, the throttle pinned, the back tyre spinning at 60 km/h from 0.5 s to 3.5 s.
	auto Burnout = [](double T) {
		RideInputs I;
		const bool Spin = T > 0.5 && T < 3.5;
		I.Rpm = Spin ? 6500.0 : 1200.0; I.Throttle = Spin ? 1.0 : 0.0;
		I.RearSlide = Spin ? 1.0 : 0.0; I.SlipSpeed = Spin ? 17.0 : 0.0;
		return I;
	};
	auto Town = [](double) { RideInputs I; I.Rpm = 950; I.Town = 1.0; return I; };
	auto Paddock = [](double) { RideInputs I; I.Rpm = 950; I.Circuit = 1.0; return I; };

	struct Case { const char* Name; double Secs; std::function<RideInputs(double)> Fn; };
	const Case Cases[] = {{"stop_dry", 4.5, Stop(0.0)}, {"burnout", 4.5, Burnout}, {"stop_wet", 7.5, Stop(1.0)},
		{"town", 70.0, Town}, {"paddock", 45.0, Paddock}};
	std::vector<float> All;
	for (const Case& C : Cases)
	{
		const Result Was = Render(Dir, false, C.Secs, C.Fn), Now = Render(Dir, true, C.Secs, C.Fn);
		std::printf("  %-9s before: rms %.3f peak %.2f | after: rms %.3f peak %.2f, squeals %d, bikes heard %d\n", C.Name,
			Was.Rms, Was.Peak, Now.Rms, Now.Peak, Now.Squeals, Now.Bikes);
		if (Was.Bad || Now.Bad) { std::printf("    NaN\n"); ++Fails; }
		if (Was.Squeals || Was.Bikes) { std::printf("    the pack heard without it loaded\n"); ++Fails; }
		const std::string N = C.Name;
		if ((N == "stop_dry" || N == "burnout") && Now.Squeals < 1) { std::printf("    no squeal\n"); ++Fails; }
		if (N == "stop_wet" && Now.Squeals) { std::printf("    a wet road squealed\n"); ++Fails; }
		if (N == "town" && Now.Bikes < 1) { std::printf("    no bike heard in town\n"); ++Fails; }
		if (N == "paddock" && Now.Bikes < 2) { std::printf("    the paddock is quiet\n"); ++Fails; }
		if (Now.Peak > 0.97) { std::printf("    too loud\n"); ++Fails; }
		if (!Listen.empty())
		{
			if (N == "stop_dry" || N == "burnout")
			{
				Save(Listen + "/" + N + "_before.wav", Was.Wav);
				All.insert(All.end(), Was.Wav.begin(), Was.Wav.end());
				All.insert(All.end(), RideSound::Rate, 0.0f);
			}
			else Save(Listen + "/" + N + "_before.wav", Was.Wav);
			Save(Listen + "/" + N + "_after.wav", Now.Wav);
			All.insert(All.end(), Now.Wav.begin(), Now.Wav.end());
			All.insert(All.end(), RideSound::Rate * 2, 0.0f);
		}
	}
	if (!Listen.empty()) Save(Listen + "/sound_pack1_before_after.wav", All);
	std::printf(Fails ? "pack1_test: %d FAILED\n" : "pack1_test: all passed\n", Fails);
	return Fails ? 1 : 0;
}
