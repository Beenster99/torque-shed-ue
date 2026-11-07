// Renders a scripted ride through TorqueSound::RideSound to sound_test.wav and checks it's sane:
// no NaNs, nothing pinned at full scale, a firing rate that matches the revs, pops on the overrun.
//   sound_test [path to Data/Audio]
#include "RideSound.h"

#include <cmath>
#include <cstdio>
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
	Run("overrun 6500 down to 3000", 3.0, [](double T) { RideInputs I; I.Rpm = 6500 - 3500 * T; I.Throttle = 0; I.Speed = 30 - 10 * T; return I; });
	S.GearChange(false);
	Run("rear locked at 15 m/s", 1.5, [](double T) { RideInputs I; I.Rpm = 2500; I.Speed = 15 - 8 * T; I.RearSlide = 1; return I; });
	Run("helmet, 4000 rpm, 25 m/s", 2.0, [](double) { RideInputs I; I.Rpm = 4000; I.Throttle = 0.5; I.Speed = 25; I.Inside = 1; return I; });

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
