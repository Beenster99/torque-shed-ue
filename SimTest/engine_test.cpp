// Torque Shed's engine sound in C++ (TorqueAudio/EngineSynth): bakes a bank for each kind of engine,
// plays a ride through it and checks it's sane: no NaNs, nothing pinned, loops seamless, the note's
// firing rate following the revs, the recorded idle at tickover and handing over as the revs rise,
// every layout about as loud as the others, overrun pops from a loud pipe, the catch.
// Writes engine_<name>.wav (idle, a blip, a pull through the revs, the overrun) to listen to.
//   engine_test [path to Data] [folder for the wavs]
#include "EngineSynth.h"
#include "RideSound.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace TorqueEngine;
namespace fs = std::filesystem;

#ifndef TORQUE_DATA
#define TORQUE_DATA "../Data"
#endif

static int Fails = 0;
#define CHECK(C, ...) do { if (!(C)) { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++Fails; } } while (0)

static void WriteWav(const std::string& Path, const std::vector<float>& S, int SampleRate)
{
	std::ofstream F(Path, std::ios::binary);
	auto U32 = [&F](uint32_t V) { F.write((const char*)&V, 4); };
	auto U16 = [&F](uint16_t V) { F.write((const char*)&V, 2); };
	F.write("RIFF", 4); U32(36 + (uint32_t)S.size() * 2); F.write("WAVEfmt ", 8);
	U32(16); U16(1); U16(1); U32(SampleRate); U32(SampleRate * 2); U16(2); U16(16);
	F.write("data", 4); U32((uint32_t)S.size() * 2);
	for (float V : S) { const int16_t I = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, V)) * 32767.0f); F.write((const char*)&I, 2); }
}

static double Rms(const float* S, size_t N)
{
	double E = 0.0;
	for (size_t I = 0; I < N; ++I) E += (double)S[I] * S[I];
	return N ? std::sqrt(E / N) : 0.0;
}

// The strongest period in a stretch (autocorrelation of its envelope), as a rate in Hz.
static double Beat(const std::vector<float>& S, int SampleRate, double MinHz, double MaxHz)
{
	// The envelope: rectified and smoothed to 300 Hz.
	std::vector<double> Env(S.size());
	double Lp = 0.0;
	const double A = 1.0 - std::exp(-2.0 * 3.14159265 * 300.0 / SampleRate);
	for (size_t I = 0; I < S.size(); ++I) { Lp += (std::fabs(S[I]) - Lp) * A; Env[I] = Lp; }
	double Mean = 0.0;
	for (double V : Env) Mean += V;
	Mean /= Env.size();
	for (double& V : Env) V -= Mean;
	int Best = 0;
	double BestC = -1e30;
	for (int Lag = (int)(SampleRate / MaxHz); Lag <= (int)(SampleRate / MinHz); ++Lag)
	{
		double C = 0.0;
		for (size_t I = 0; I + Lag < Env.size(); I += 2) C += Env[I] * Env[I + Lag];
		C /= (double)(Env.size() - Lag);
		if (C > BestC) { BestC = C; Best = Lag; }
	}
	return Best ? (double)SampleRate / Best : 0.0;
}

int main(int Argc, char** Argv)
{
	const std::string Data = Argc > 1 ? Argv[1] : TORQUE_DATA;
	const std::string OutDir = Argc > 2 ? Argv[2] : ".";
	FBank::FReader Read = [&Data](const std::string& Rel) {
		std::ifstream F(Data + "/" + Rel, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
	};
	FBank::FLister List = [&Data](const std::string& Rel) {
		std::vector<std::string> Out;
		std::error_code Ec;
		if (fs::is_directory(Data + "/" + Rel, Ec))
			for (const auto& E : fs::directory_iterator(Data + "/" + Rel)) Out.push_back(E.path().filename().string());
		return Out;
	};

	struct FCase
	{
		const char* Name;
		FSpec S;
		bool bIdleRec;   // a real bike of its kind ticking over in Data/Audio/TS/engines/idle
	};
	auto Make = [](const char* Layout, int Cyl, double Cc, double Idle, double Redline, const char* Exhaust, const char* Crank = "") {
		FSpec S;
		S.Layout = Layout; S.Cylinders = Cyl; S.Cc = Cc; S.Idle = Idle; S.Redline = Redline; S.Exhaust = Exhaust; S.Crank = Crank;
		S.Peak = Redline * 0.7;
		return S;
	};
	std::vector<FCase> Cases = {
		{"single_450", Make("single", 1, 450, 1500, 10500, "full"), false},
		{"parallel_900", Make("parallel", 2, 900, 1100, 9000, "stock"), true},
		{"twin360_650", Make("twin", 2, 650, 1200, 8000, "slip"), true},
		{"vtwin45_1800", Make("vtwin", 2, 1800, 900, 5500, "straight", "45v"), true},
		{"vtwin90_1000", Make("vtwin", 2, 1000, 1300, 10500, "full"), true},
		{"boxer_1250", Make("boxer", 2, 1250, 1100, 9000, "stock"), true},
		{"triple_765", Make("triple", 3, 765, 1300, 12500, "full"), true},
		{"inline4_1000", Make("inline", 4, 1000, 1300, 14000, "race"), true},
		{"2stroke_250", Make("2stroke", 2, 250, 1400, 12000, "full"), false},
		{"2stroke_125", Make("2stroke", 1, 125, 1500, 11500, "full"), false},
	};
	std::vector<double> IdleLoud, PullLoud;
	for (FCase& C : Cases)
	{
		auto T0 = std::chrono::steady_clock::now();
		std::shared_ptr<FBank> B = FBank::For(C.S, Read, List);
		B->Bake();
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		std::printf("%-14s %s: %zu buckets%s%s, baked in %.0f ms\n", C.Name, B->Sig.c_str(), B->Rpms.size(), B->bRecorded ? " (recorded)" : "",
			B->IdleRec.empty() ? "" : ", idle recording", Ms);
		CHECK(B->Finished() && B->bShotsReady, "%s didn't finish", C.Name);
		CHECK(C.bIdleRec == !B->IdleRec.empty(), "%s: idle recording %s", C.Name, B->IdleRec.empty() ? "missing" : "unexpected");
		// Every loop finite, not silent, not pinned, and seamless (its end runs into its start).
		for (size_t I = 0; I < B->Buckets.size(); ++I)
			for (int L = 0; L < Layers; ++L)
			{
				const std::vector<float>& Lp = B->Buckets[I]->Loop[L];
				if (Lp.empty()) continue;
				double Peak = 0.0, Jump = 0.0, Step = 0.0;
				bool bFinite = true;
				for (size_t K = 0; K < Lp.size(); ++K)
				{
					bFinite = bFinite && std::isfinite(Lp[K]);
					Peak = std::max(Peak, (double)std::fabs(Lp[K]));
					if (K) Step = std::max(Step, (double)std::fabs(Lp[K] - Lp[K - 1]));
				}
				Jump = std::fabs(Lp.front() - Lp.back());
				CHECK(bFinite, "%s bucket %zu layer %d isn't finite", C.Name, I, L);
				CHECK(Peak < 1.6 && Peak > 1e-4, "%s bucket %zu layer %d peak %.3f", C.Name, I, L, Peak);
				CHECK(B->bRecorded || Jump <= Step * 1.5 + 1e-3, "%s bucket %zu layer %d clicks going round (%.3f, steps up to %.3f)", C.Name, I, L, Jump, Step);
			}

		// A ride: idle, a blip, a pull to near the redline on the throttle, the overrun.
		const int OutRate = 48000;
		FVoice V(B, OutRate);
		std::vector<float> All;
		const double Idle = B->Spec.Idle, Red = B->Spec.Redline;
		auto Run = [&](double Secs, auto Fn) {
			const size_t From = All.size();
			const int Blocks = (int)(Secs * 100);
			for (int K = 0; K < Blocks; ++K)
			{
				FVoice::FInputs In = Fn(K / (double)Blocks);
				V.Set(In);
				float Buf[480] = {};
				V.Render(Buf, 480);
				All.insert(All.end(), Buf, Buf + 480);
			}
			return From;
		};
		V.Catch();
		const size_t IdleAt = Run(3.0, [&](double) { FVoice::FInputs In; In.Rpm = Idle; return In; });
		const double IdleShare = V.IdleShare;
		const size_t BlipAt = Run(1.0, [&](double T) { FVoice::FInputs In; In.Rpm = Idle + (Red * 0.5 - Idle) * std::sin(T * 3.14159); In.Throttle = T < 0.4 ? 1 : 0; return In; });
		const size_t PullAt = Run(4.0, [&](double T) { FVoice::FInputs In; In.Rpm = Idle * 1.8 + (Red * 0.95 - Idle * 1.8) * T; In.Throttle = 1; return In; });
		const int Pops0 = V.Pops;
		const size_t OverAt = Run(3.0, [&](double T) { FVoice::FInputs In; In.Rpm = Red * 0.85 - (Red * 0.85 - Idle * 1.5) * T; In.Throttle = 0; return In; });
		Run(1.5, [&](double) { FVoice::FInputs In; In.Rpm = Idle; return In; });
		(void)BlipAt;
		double Peak = 0.0;
		bool bFinite = true;
		for (float S : All) { bFinite = bFinite && std::isfinite(S); Peak = std::max(Peak, (double)std::fabs(S)); }
		const double IdleRms = Rms(&All[IdleAt + OutRate], OutRate * 2);
		const double PullRms = Rms(&All[PullAt + OutRate], OutRate * 2);
		const double OverRms = Rms(&All[OverAt], OutRate * 3);
		IdleLoud.push_back(IdleRms);
		PullLoud.push_back(PullRms);
		std::printf("  idle rms %.3f (recorded share %.2f), pull %.3f, overrun %.3f (%d pops), peak %.2f, catches %d\n", IdleRms, IdleShare, PullRms,
			OverRms, V.Pops - Pops0, Peak, V.Catches);
		CHECK(bFinite, "%s: NaN in the ride", C.Name);
		CHECK(Peak < 1.5, "%s: too loud (peak %.2f)", C.Name, Peak);
		CHECK(IdleRms > 0.004, "%s: silent at idle", C.Name);
		CHECK(PullRms > IdleRms, "%s: no louder on the throttle (%.3f vs %.3f)", C.Name, PullRms, IdleRms);
		CHECK(V.Catches == 1, "%s: didn't catch", C.Name);
		CHECK(!C.bIdleRec || IdleShare > 0.9, "%s: the idle recording has only %.2f of tickover", C.Name, IdleShare);

		// The note follows the revs: with the made sound alone (no idle recording) at a steady 3000
		// and 6000 rpm on the throttle, the envelope beats at the firing rate (or a whole multiple of
		// a cycle, for an uneven engine).
		if (!B->bRecorded)
			for (double Rpm : {3000.0, 6000.0})
			{
				FVoice W(B, OutRate);
				std::vector<float> S(OutRate);
				for (int K = 0; K < 100; ++K)
				{
					FVoice::FInputs In; In.Rpm = Rpm; In.Throttle = 1.0;
					W.Set(In);
					W.Render(&S[K * 480], 480);
				}
				std::vector<float> Tail(S.begin() + OutRate / 2, S.end());
				const double Fire = FiringHz(B->Spec, Rpm), Cycle = Rpm / 60.0 / (CycleDegrees(B->Spec) / 360.0);
				const double Got = Beat(Tail, OutRate, Cycle * 0.8, Fire * 1.25);
				// Whole cycles or firings: the beat's a whole number of firings to a cycle.
				const double Per = Fire / Got;
				const bool bWhole = std::fabs(Per - std::round(Per)) < 0.12 && std::round(Per) >= 1.0;
				std::printf("  %5.0f rpm: beats at %.1f Hz (firing %.1f, cycle %.1f)\n", Rpm, Got, Fire, Cycle);
				CHECK(bWhole, "%s at %.0f rpm beats at %.1f Hz, not with its firings (%.1f Hz)", C.Name, Rpm, Got, Fire);
			}
		WriteWav(OutDir + "/engine_" + C.Name + ".wav", All, OutRate);
	}
	// Every layout about as loud as the rest (within 9 dB at idle and on the pull).
	auto Spread = [](const std::vector<double>& L) {
		double Lo = 1e9, Hi = 0.0;
		for (double V : L) { Lo = std::min(Lo, V); Hi = std::max(Hi, V); }
		return 20.0 * std::log10(Hi / std::max(Lo, 1e-9));
	};
	std::printf("loudness spread: idle %.1f dB, pull %.1f dB\n", Spread(IdleLoud), Spread(PullLoud));
	CHECK(Spread(IdleLoud) < 12.0, "idle loudness spreads %.1f dB", Spread(IdleLoud));
	CHECK(Spread(PullLoud) < 12.0, "pull loudness spreads %.1f dB", Spread(PullLoud));

	// In the ride's mix: about as loud as the made twin was, idle and pulling.
	{
		auto Ride = [](TorqueSound::RideSound& R, double Rpm, double Thr) {
			std::vector<float> S;
			for (int K = 0; K < 150; ++K)
			{
				TorqueSound::RideInputs In; In.Rpm = Rpm; In.Throttle = Thr;
				R.Set(In);
				float Buf[480];
				R.Render(Buf, 480);
				if (K >= 50) S.insert(S.end(), Buf, Buf + 480);
			}
			return Rms(S.data(), S.size());
		};
		TorqueSound::RideSound Old, New;
		Old.Ambience = New.Ambience = false;
		Old.IdleRpm = New.IdleRpm = 1100.0;
		New.SetEngine(FBank::For(Cases[1].S, Read, List));
		const double OldIdle = Ride(Old, 1100, 0), NewIdle = Ride(New, 1100, 0);
		const double OldPull = Ride(Old, 6000, 1), NewPull = Ride(New, 6000, 1);
		std::printf("ride mix: made twin idle %.3f pull %.3f; Torque Shed's parallel twin idle %.3f pull %.3f\n", OldIdle, OldPull, NewIdle, NewPull);
		CHECK(std::fabs(20.0 * std::log10(NewPull / OldPull)) < 6.0, "the engine's %.1f dB off the old one pulling", 20.0 * std::log10(NewPull / OldPull));
		CHECK(New.EngineSound() && New.EngineSound()->Sounding() > 0, "the ride isn't playing the engine");
	}
	std::printf(Fails ? "%d FAILED\n" : "engine_test passed\n", Fails);
	return Fails ? 1 : 0;
}
