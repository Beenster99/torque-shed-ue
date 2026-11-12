// Renders a scripted ride through TorqueSound::RideSound to sound_test.wav and checks it's sane:
// no NaNs, nothing pinned at full scale, a firing rate that matches the revs, and overrun pops only
// from a hot exhaust: a few on a downshift, the odd one rolling off or down a hill, none when cold.
//   sound_test [path to Data/Audio]
#include "RideSound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

int main(int Argc, char** Argv)
{
	const std::string Dir = Argc > 1 ? Argv[1] : "../Data/Audio";
	RideSound S;
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
	std::printf("idle %zu samples, ambience %zu\n", Idle.size(), Amb.size());

	std::vector<float> All;
	const int Block = 480;   // 10 ms
	int Fails = 0;
	auto Run = [&](const char* Name, double Secs, auto Fn) {
		const int Firings0 = S.Firings, Pops0 = S.Pops;
		double Sum = 0.0, Peak = 0.0;
		int Clip = 0;
		const int Blocks = (int)(Secs * 100);
		for (int B = 0; B < Blocks; ++B)
		{
			RideInputs In = Fn(B / (double)Blocks);
			S.Set(In);
			float Buf[Block];
			S.Render(Buf, Block);
			for (float V : Buf)
			{
				if (!std::isfinite(V)) { std::printf("  NaN in %s\n", Name); ++Fails; return; }
				Sum += V * V; Peak = std::max(Peak, (double)std::fabs(V));
				if (std::fabs(V) > 0.98f) ++Clip;
				All.push_back(V);
			}
		}
		const double Rms = std::sqrt(Sum / (Blocks * Block));
		std::printf("  %-28s rms %.3f peak %.2f clipped %d firings/s %.1f pops %d\n", Name, Rms, Peak, Clip,
			(S.Firings - Firings0) / Secs, S.Pops - Pops0);
		if (Clip > Blocks * Block / 200) { std::printf("    too loud\n"); ++Fails; }
		if (Rms < 0.01) { std::printf("    silent\n"); ++Fails; }
	};
	Run("idle", 3.0, [](double) { RideInputs I; I.Rpm = 950; return I; });
	Run("blip to 4000", 1.0, [](double T) { RideInputs I; I.Rpm = 950 + 3050 * std::sin(T * 3.14159); I.Throttle = T < 0.5 ? 1 : 0; return I; });
	Run("pull 2000-8000 in 1st", 4.0, [](double T) { RideInputs I; I.Rpm = 2000 + 6000 * T; I.Throttle = 1; I.Speed = 3 + 22 * T; return I; });
	S.GearChange(true);
	Run("2nd, 6000 rpm, 100 km/h", 3.0, [](double T) { RideInputs I; I.Rpm = 6000 + 1500 * T; I.Throttle = 0.8; I.Speed = 28 + 4 * T; return I; });
	auto Overrun = [](double T) { RideInputs I; I.Rpm = 6500 - 3500 * T; I.Throttle = 0; I.Speed = 30 - 10 * T; return I; };
	int Before = S.Pops;
	Run("overrun, cold pipe", 3.0, Overrun);
	std::printf("    exhaust heat %.2f\n", S.Heat);
	if (S.Pops != Before) { std::printf("    a cold exhaust shouldn't pop\n"); ++Fails; }

	// A few minutes on: the pipe is hot.
	S.Heat = 1.0;
	Run("cruise 4500 rpm, hot pipe", 2.0, [](double) { RideInputs I; I.Rpm = 4500; I.Throttle = 0.4; I.Speed = 24; return I; });
	Before = S.Pops;
	Run("overrun, hot pipe", 3.0, Overrun);
	const int RollOff = S.Pops - Before;
	if (RollOff > 3) { std::printf("    too many pops rolling off\n"); ++Fails; }
	// Downshifts on a shut throttle: 4th to 3rd to 2nd coming into a bend, the revs kicking up each time.
	Run("cruise 5000 rpm", 1.5, [](double) { RideInputs I; I.Rpm = 5000; I.Throttle = 0.5; I.Speed = 30; return I; });
	Before = S.Pops;
	for (int Shift = 0; Shift < 3; ++Shift)
	{
		const double From = 3600 + 300 * Shift, To = 5600 + 300 * Shift;
		Run("  shut throttle", 0.4, [=](double T) { RideInputs I; I.Rpm = From + 400 * (1 - T); I.Throttle = 0; I.Speed = 26 - 6 * Shift - 2 * T; return I; });
		S.GearChange(false);
		Run("  down a gear", 1.4, [=](double T) { RideInputs I; I.Rpm = To - 1600 * T; I.Throttle = 0; I.Speed = 24 - 6 * Shift - 4 * T; return I; });
	}
	const int Downshifts = S.Pops - Before;
	std::printf("    pops on 3 downshifts: %d\n", Downshifts);
	if (Downshifts < 1 || Downshifts > 9) { std::printf("    want an occasional pop on a hot downshift\n"); ++Fails; }
	Before = S.Pops;
	Run("downhill, shut, 6000 rpm", 10.0, [](double) { RideInputs I; I.Rpm = 6000; I.Throttle = 0; I.Speed = 28; return I; });
	if (S.Pops - Before > 5) { std::printf("    too many pops downhill\n"); ++Fails; }
	S.GearChange(false);
	Run("rear locked at 15 m/s", 1.5, [](double T) { RideInputs I; I.Rpm = 2500; I.Speed = 15 - 8 * T; I.RearSlide = 1; return I; });
	for (double V : {6.0, 20.0})
	{
		Run(V < 10 ? "over a cattle grid, 6 m/s" : "over a cattle grid, 20 m/s", 3.0, [V](double T) {
			RideInputs I; I.Rpm = 2400 + V * 80; I.Throttle = 0.25; I.Speed = V;
			// The grid is 2.3 m long; the front wheel then both, then the rear, in the middle second.
			const double D = (T - 0.33) * 3.0 * V, Wb = 1.41;
			I.Grid = (D > 0 && D < 2.3 ? 1.0 : 0.0) + (D - Wb > 0 && D - Wb < 2.3 ? 1.0 : 0.0);
			return I; });
	}
	Run("helmet, 4000 rpm, 25 m/s", 2.0, [](double) { RideInputs I; I.Rpm = 4000; I.Throttle = 0.5; I.Speed = 25; I.Inside = 1; return I; });

	// A cattle grid is heard over the engine, and louder the faster it's crossed.
	{
		auto Level = [](double V, double Grid) {
			RideSound T;
			RideInputs I; I.Rpm = 2400 + V * 80; I.Throttle = 0.25; I.Speed = V; I.Grid = Grid; T.Set(I);
			std::vector<float> Buf(RideSound::Rate);
			T.Render(Buf.data(), (int)Buf.size());
			double Sum = 0.0;
			for (size_t K = Buf.size() / 2; K < Buf.size(); ++K) Sum += Buf[K] * Buf[K];
			return std::sqrt(Sum / (Buf.size() / 2));
		};
		for (double V : {6.0, 20.0})
		{
			const double Off = Level(V, 0.0), On = Level(V, 2.0);
			std::printf("  cattle grid at %.0f m/s: rms %.3f, without %.3f\n", V, On, Off);
			if (On < Off * 1.25) { std::printf("    the grid can't be heard\n"); ++Fails; }
		}
	}

	// The weather (stopped, engine off): a gusty wind and rain are heard; a calm dry day is near silent.
	{
		auto Level = [](double Wind, double Gust, double Rain, double Inside) {
			RideSound T;
			RideInputs I; I.Rpm = 0; I.EngineOn = false; I.Speed = 0; I.AirWind = Wind; I.Gust = Gust; I.Rain = Rain; I.Inside = Inside; T.Set(I);
			std::vector<float> Buf(RideSound::Rate * 5);
			T.Render(Buf.data(), (int)Buf.size());
			double Sum = 0.0;
			for (size_t K = Buf.size() * 3 / 5; K < Buf.size(); ++K)
			{
				if (!std::isfinite(Buf[K])) return -1.0;
				Sum += Buf[K] * Buf[K];
			}
			return std::sqrt(Sum / (Buf.size() * 2 / 5));
		};
		const double Calm = Level(0, 0, 0, 1), Windy = Level(12, 20, 0, 1), Wet = Level(0, 0, 0.6, 1), WetOut = Level(0, 0, 0.6, 0);
		std::printf("  stopped: calm %.4f, gusty 12 m/s %.4f, rain in the helmet %.4f, rain heard from behind %.4f\n", Calm, Windy, Wet, WetOut);
		if (Calm < 0.0 || Windy < 0.0 || Wet < 0.0) { std::printf("    NaN in the weather\n"); ++Fails; }
		if (Windy < Calm * 3.0 + 0.003) { std::printf("    the wind can't be heard standing still\n"); ++Fails; }
		if (Wet < Calm * 3.0 + 0.003 || WetOut >= Wet) { std::printf("    the rain can't be heard (or isn't louder in the helmet)\n"); ++Fails; }
		if (Windy > 0.2 || Wet > 0.2) { std::printf("    the weather's too loud\n"); ++Fails; }
	}

	// The sound of the place (stopped, engine off): the sea, a beck, a town, sheep and cattle each heard,
	// none too loud, nothing when there's nothing there. SOUND_PLACE_WAV=file writes 5 s of each, in turn.
	{
		std::vector<float> Heard;
		auto Level = [&Heard](double Sea, double Water, double Town, double Sheep, double Cattle) {
			RideSound T;
			RideInputs I; I.Rpm = 0; I.EngineOn = false; I.Speed = 0;
			I.Sea = Sea; I.Water = Water; I.Town = Town; I.Sheep = Sheep; I.Cattle = Cattle; T.Set(I);
			std::vector<float> Buf(RideSound::Rate * 20);
			T.Render(Buf.data(), (int)Buf.size());
			double Sum = 0.0;
			for (size_t K = Buf.size() / 4; K < Buf.size(); ++K)
			{
				if (!std::isfinite(Buf[K])) return -1.0;
				Sum += Buf[K] * Buf[K];
			}
			Heard.insert(Heard.end(), Buf.end() - RideSound::Rate * 5, Buf.end());
			return std::sqrt(Sum / (Buf.size() * 3 / 4));
		};
		const double None = Level(0, 0, 0, 0, 0), Sea = Level(1, 0, 0, 0, 0), Beck = Level(0, 1, 0, 0, 0), Town = Level(0, 0, 1, 0, 0),
			Sheep = Level(0, 0, 0, 1, 0), Cattle = Level(0, 0, 0, 0, 1);
		std::printf("  the place, stopped: nothing %.4f, sea %.4f, beck %.4f, town %.4f, sheep %.4f, cattle %.4f\n", None, Sea, Beck, Town, Sheep, Cattle);
		bool Ok = None >= 0.0 && None < 0.001;
		for (double L : {Sea, Beck, Town, Sheep, Cattle}) Ok = Ok && L > 0.0015 && L < 0.05;
		if (!Ok) { std::printf("    a place's sound is missing, too loud, or NaN\n"); ++Fails; }
		if (const char* Path = std::getenv("SOUND_PLACE_WAV"))
			if (FILE* F = std::fopen(Path, "wb"))
			{
				const uint32_t Rate = RideSound::Rate, Bytes = uint32_t(Heard.size() * 2), Riff = 36 + Bytes, Fmt = 16, ByteRate = Rate * 2;
				const uint16_t Pcm = 1, Mono = 1, Align = 2, Bits = 16;
				std::fwrite("RIFF", 1, 4, F); std::fwrite(&Riff, 4, 1, F); std::fwrite("WAVEfmt ", 1, 8, F); std::fwrite(&Fmt, 4, 1, F);
				std::fwrite(&Pcm, 2, 1, F); std::fwrite(&Mono, 2, 1, F); std::fwrite(&Rate, 4, 1, F); std::fwrite(&ByteRate, 4, 1, F);
				std::fwrite(&Align, 2, 1, F); std::fwrite(&Bits, 2, 1, F); std::fwrite("data", 1, 4, F); std::fwrite(&Bytes, 4, 1, F);
				for (float V : Heard) { const int16_t Q = int16_t(std::max(-1.0f, std::min(1.0f, V * 4.0f)) * 32000.0f); std::fwrite(&Q, 2, 1, F); }
				std::fclose(F);
			}
	}

	// The pipe warms up with riding.
	{
		RideSound T;
		RideInputs I; I.Rpm = 5000; I.Throttle = 0.6; I.Speed = 25; T.Set(I);
		std::vector<float> Buf(RideSound::Rate * 60);
		T.Render(Buf.data(), (int)Buf.size());
		std::printf("  exhaust heat after a minute at 5000 rpm: %.2f\n", T.Heat);
		if (T.Heat < 0.85) ++Fails;
	}

	// The firing rate: a twin fires once a revolution on average.
	{
		RideSound T;
		RideInputs I; I.Rpm = 6000; T.Set(I);
		std::vector<float> Buf(RideSound::Rate * 2);
		T.Render(Buf.data(), (int)Buf.size());
		const double PerSec = T.Firings / 2.0, Want = 6000 / 60.0;
		std::printf("  firing rate at 6000 rpm: %.1f /s (want about %.1f)\n", PerSec, Want);
		if (std::fabs(PerSec - Want) > Want * 0.1) ++Fails;
	}

	// sound_test.wav to listen to.
	std::ofstream W("sound_test.wav", std::ios::binary);
	auto U32 = [&](uint32_t V) { W.write((const char*)&V, 4); };
	auto U16 = [&](uint16_t V) { W.write((const char*)&V, 2); };
	W.write("RIFF", 4); U32(36 + (uint32_t)All.size() * 2); W.write("WAVEfmt ", 8);
	U32(16); U16(1); U16(1); U32(RideSound::Rate); U32(RideSound::Rate * 2); U16(2); U16(16);
	W.write("data", 4); U32((uint32_t)All.size() * 2);
	for (float V : All) { int16_t X = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, V)) * 32767); W.write((const char*)&X, 2); }
	std::printf("wrote sound_test.wav (%.1f s)\n%s\n", All.size() / (double)RideSound::Rate, Fails ? "FAILED" : "all sound checks passed");
	return Fails ? 1 : 0;
}
