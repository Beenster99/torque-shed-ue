#include "RideSound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace TorqueSound
{
namespace
{
constexpr double Pi = 3.14159265358979323846;

double Clamp01(double V) { return std::min(std::max(V, 0.0), 1.0); }
double Smooth(double A, double B, double X)
{
	const double T = Clamp01((X - A) / (B - A));
	return T * T * (3.0 - 2.0 * T);
}
double Lerp(double A, double B, double T) { return A + (B - A) * T; }

uint32_t U32(const uint8_t* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | ((uint32_t)P[3] << 24); }
uint16_t U16(const uint8_t* P) { return (uint16_t)(P[0] | (P[1] << 8)); }

// A loop that runs on without a click: its last Fade samples crossfaded into its first.
std::vector<float> MakeLoop(std::vector<float> S, int Fade)
{
	const int N = (int)S.size();
	if (N < Fade * 3) return S;
	std::vector<float> Out(S.begin(), S.end() - Fade);
	for (int I = 0; I < Fade; ++I)
	{
		const double T = (double)I / Fade;
		Out[I] = (float)(S[I] * T + S[N - Fade + I] * (1.0 - T));
	}
	return Out;
}

double Rms(const std::vector<float>& S)
{
	double Sum = 0.0;
	for (float V : S) Sum += V * V;
	return S.empty() ? 0.0 : std::sqrt(Sum / S.size());
}
} // namespace

std::vector<float> ReadWav(const std::vector<uint8_t>& B, int& SampleRate)
{
	std::vector<float> Out;
	if (B.size() < 44 || std::memcmp(B.data(), "RIFF", 4) != 0 || std::memcmp(B.data() + 8, "WAVE", 4) != 0) return Out;
	int Channels = 0, Bits = 0;
	size_t P = 12;
	while (P + 8 <= B.size())
	{
		const uint32_t Len = U32(&B[P + 4]);
		const uint8_t* Body = &B[P + 8];
		if (std::memcmp(&B[P], "fmt ", 4) == 0 && Len >= 16)
		{
			Channels = U16(Body + 2);
			SampleRate = (int)U32(Body + 4);
			Bits = U16(Body + 14);
		}
		else if (std::memcmp(&B[P], "data", 4) == 0 && Bits == 16 && Channels > 0)
		{
			const size_t Frames = std::min<size_t>(Len, B.size() - P - 8) / (2 * Channels);
			Out.resize(Frames);
			for (size_t I = 0; I < Frames; ++I)
				Out[I] = (int16_t)U16(Body + I * 2 * Channels) / 32768.0f;   // the first channel
			return Out;
		}
		P += 8 + Len + (Len & 1);
	}
	return Out;
}

void RideSound::Biquad::BandPass(double Freq, double Q)
{
	const double W = 2.0 * Pi * Freq / Rate, Alpha = std::sin(W) / (2.0 * Q), A0 = 1.0 + Alpha;
	B0 = Alpha / A0; B1 = 0.0; B2 = -Alpha / A0;
	A1 = -2.0 * std::cos(W) / A0; A2 = (1.0 - Alpha) / A0;
}

void RideSound::Biquad::LowPass(double Freq, double Q)
{
	const double W = 2.0 * Pi * Freq / Rate, Alpha = std::sin(W) / (2.0 * Q), A0 = 1.0 + Alpha, C = std::cos(W);
	B0 = (1.0 - C) / 2.0 / A0; B1 = (1.0 - C) / A0; B2 = B0;
	A1 = -2.0 * C / A0; A2 = (1.0 - Alpha) / A0;
}

std::vector<float> RideSound::Resample(const std::vector<float>& In, int From)
{
	if (From == Rate || In.empty()) return In;
	const double Step = (double)From / Rate;
	const size_t N = (size_t)((In.size() - 1) / Step);
	std::vector<float> Out(N);
	for (size_t I = 0; I < N; ++I) Out[I] = (float)Sample(In, I * Step);
	return Out;
}

double RideSound::Sample(const std::vector<float>& S, double Pos)
{
	const size_t I = (size_t)Pos;
	if (I + 1 >= S.size()) return S.empty() ? 0.0 : S.back();
	const double F = Pos - I;
	return S[I] + (S[I + 1] - S[I]) * F;
}

void RideSound::SetIdle(std::vector<float> Samples, int SampleRate, double RecordedRpm)
{
	Idle = MakeLoop(Resample(Samples, SampleRate), Rate / 20);
	const double R = Rms(Idle);
	if (R > 1e-6)
		for (float& V : Idle) V = (float)(V * 0.16 / R);   // about as loud as the made sound at tickover
	IdleRpmRec = RecordedRpm;
	IdlePos = 0.0;
}

void RideSound::AddClunk(std::vector<float> Samples, int SampleRate)
{
	std::vector<float> S = Resample(Samples, SampleRate);
	float Peak = 0.0f;
	for (float V : S) Peak = std::max(Peak, std::fabs(V));
	if (Peak > 1e-6f)
		for (float& V : S) V /= Peak;
	Clunks.push_back(std::move(S));
}

void RideSound::SetAmbience(std::vector<float> Samples, int SampleRate)
{
	Amb = MakeLoop(Resample(Samples, SampleRate), Rate);
	const double R = Rms(Amb);
	if (R > 1e-6)
		for (float& V : Amb) V = (float)(V * 0.018 / R);
}

void RideSound::GearChange(bool Up)
{
	Cut = 0.09;
	if (Clunks.empty()) return;
	int I = (int)(Uniform() * Clunks.size()) % (int)Clunks.size();
	if (I == LastClunk) I = (I + 1) % (int)Clunks.size();
	LastClunk = I;
	Playing = &Clunks[I];
	ClunkPos = 0.0;
	ClunkGain = Up ? 0.35 : 0.45;
	// Knocking it down a gear with the throttle shut: the revs come up against a shut throttle and
	// unburnt mixture reaches a hot pipe, the moment a stock Speed Twin pops.
	if (!Up && Target.Throttle < 0.15) DownshiftPop = 1.2;
}

void RideSound::Retune()
{
	// The pipe's resonances (a header and a peashooter-ish silencer): fixed, the firings ring them.
	static const double F[4] = {82.0, 165.0, 330.0, 880.0}, Q[4] = {2.2, 2.8, 3.0, 1.8};
	for (int I = 0; I < 4; ++I) Pipe[I].BandPass(F[I], Q[I]);
	Tone.LowPass(140.0, 0.7);
	Intake.BandPass(520.0 + Now.Rpm * 0.05, 1.1);
	Crackle.BandPass(1700.0, 1.2);
	PopBody.LowPass(320.0, 0.8);
	WindLp.LowPass(180.0 + Now.Speed * 22.0, 0.6);
	WindBp.BandPass(500.0 + Now.Speed * 15.0, 0.7);
	Squeal.BandPass(1150.0, 6.0);
	Scrub.LowPass(450.0, 0.7);
}

void RideSound::Render(float* Out, int N)
{
	const double Kr = 1.0 - std::exp(-1.0 / (Rate * 0.012));   // revs follow in about 12 ms
	const double Kt = 1.0 - std::exp(-1.0 / (Rate * 0.02));
	const double Ks = 1.0 - std::exp(-1.0 / (Rate * 0.05));
	const double PulseFall = std::exp(-1.0 / (Rate * 0.0022));
	const double NoiseFall = std::exp(-1.0 / (Rate * 0.006));
	const double PopFall = std::exp(-1.0 / (Rate * 0.075));
	const double PopNoiseFall = std::exp(-1.0 / (Rate * 0.03));
	const double CrackFall = std::exp(-1.0 / (Rate * 0.005));
	const double PopDrop = std::exp(-1.0 / (Rate * 0.04));
	const double BirdFade = 1.0 - std::exp(-1.0 / (Rate * 2.0));
	for (int S = 0; S < N; ++S)
	{
		Now.Rpm += (Target.Rpm - Now.Rpm) * Kr;
		Now.Throttle += (Target.Throttle - Now.Throttle) * Kt;
		Now.Speed += (Target.Speed - Now.Speed) * Ks;
		Now.RearSlide += (Target.RearSlide - Now.RearSlide) * Ks;
		Now.FrontSlide += (Target.FrontSlide - Now.FrontSlide) * Ks;
		Now.SideSlide += (Target.SideSlide - Now.SideSlide) * Ks;
		Now.Inside += (Target.Inside - Now.Inside) * Ks;
		Now.OnTarmac = Target.OnTarmac;
		Now.Airborne = Target.Airborne;
		if (--RetuneIn <= 0) { Retune(); RetuneIn = 256; }

		const double Rpm = std::max(Now.Rpm, 300.0);
		const double Load = Clamp01(Now.Throttle);
		if (Cut > 0.0) Cut -= 1.0 / Rate;
		if (Load < 0.05 && Rpm > 3000.0) Overrun += 1.0 / Rate;
		else Overrun = 0.0;
		if (DownshiftPop > 0.0) DownshiftPop -= 1.0 / Rate;
		SincePop += 1.0 / Rate;
		// The pipe warms with load and revs over about 20 s and cools over a minute and a half.
		const double HeatTo = Clamp01(0.15 + 1.2 * Load * Smooth(1500.0, 5500.0, Rpm) + 0.4 * Smooth(2500.0, 6500.0, Rpm));
		Heat += (HeatTo - Heat) / (Rate * (HeatTo > Heat ? 20.0 : 90.0));

		// The crank turns; a 270-degree twin fires at 0 and 270 of every 720.
		const double Before = Crank;
		Crank += Rpm / 60.0 * 360.0 / Rate;
		const bool Wrapped = Crank >= 720.0;
		if ((NextFire == 1 && Before < 270.0 && Crank >= 270.0) || (NextFire == 0 && Wrapped))
		{
			double Amp = (0.3 + 0.7 * Load) * (1.0 + 0.1 * Noise());
			if (Cut > 0.0) Amp *= 0.12;
			if (Overrun > 0.0)
			{
				Amp = 0.22 * (1.0 + 0.15 * Noise());
				// Pops a second, then shared out over the firings: mostly just after a downshift, a
				// few as the throttle first shuts, now and then on a long run down a hill at high revs.
				const double High = Smooth(3500.0, 6500.0, Rpm);
				double PerSec = 0.2 * High;
				if (Overrun < 1.2) PerSec += 0.6 * High;
				if (DownshiftPop > 0.0) PerSec += 1.8 * Smooth(3000.0, 5500.0, Rpm);
				PerSec *= Smooth(0.4, 0.85, Heat);
				const double Chance = PerSec / std::max(Rpm / 60.0, 1.0);
				if (SincePop > 0.25 && Uniform() < Chance)
				{
					const double Size = 0.7 + 0.3 * Uniform();
					PopEnv = Size;
					PopNoiseEnv = Size;
					CrackEnv = Size * (0.2 + 0.25 * Uniform());
					PopFreq = 95.0 + 35.0 * Uniform();
					PopPhase = 0.0;
					SincePop = 0.0;
					++Pops;
				}
			}
			Pulse = Amp;
			PulseNoise = Amp;
			NextFire = 1 - NextFire;
			++Firings;
		}
		if (Wrapped) Crank -= 720.0;

		const double X = Pulse * (0.55 + 0.45 * Noise());
		Pulse *= PulseFall;
		PulseNoise *= NoiseFall;
		double Ex = Pipe[0].Run(X) * 1.0 + Pipe[1].Run(X) * 0.75 + Pipe[2].Run(X) * 0.45 + Pipe[3].Run(X) * (0.1 + 0.3 * Load);
		Ex = Ex * 2.4 + Tone.Run(X) * 1.6;
		const double In = Intake.Run(Noise()) * PulseNoise * (0.2 + 0.8 * Load);
		double Pop = 0.0;
		if (PopEnv > 1e-4)
		{
			// The thump drops from about 110 Hz toward 50 as the bang leaves the silencers.
			PopFreq = 50.0 + (PopFreq - 50.0) * PopDrop;
			PopPhase += 2.0 * Pi * PopFreq / Rate;
			if (PopPhase > 2.0 * Pi) PopPhase -= 2.0 * Pi;
			Pop = std::sin(PopPhase) * PopEnv * 1.3 + PopBody.Run(Noise()) * PopNoiseEnv * 2.2
				+ Crackle.Run(Noise()) * CrackEnv * 1.2;
			PopEnv *= PopFall;
			PopNoiseEnv *= PopNoiseFall;
			CrackEnv *= CrackFall;
		}

		// At tickover the recording has it, handing over to the made sound as the revs rise.
		double IdleOut = 0.0, Share = 0.0;
		if (!Idle.empty())
		{
			Share = (1.0 - Smooth(IdleRpm * 1.1, IdleRpm * 1.7, Rpm)) * (1.0 - 0.6 * Load);
			const double Pitch = std::min(std::max(IdleRpm / IdleRpmRec, 0.85), 1.15) * Rpm / IdleRpm;
			IdleOut = Sample(Idle, IdlePos);
			IdlePos += Pitch;
			if (IdlePos >= Idle.size() - 1) IdlePos -= Idle.size() - 1;
		}
		const double Engine = (Ex * Lerp(1.0, 0.75, Now.Inside) + In * Lerp(0.5, 1.2, Now.Inside)) * (1.0 - Share)
			+ IdleOut * Share + Pop * Lerp(1.0, 0.7, Now.Inside);

		// Wind: rushing at speed, louder in the helmet.
		const double V = Now.Speed;
		const double WindAmp = std::min(std::pow(V / 35.0, 2.0), 1.6) * Lerp(0.12, 0.32, Now.Inside);
		const double Wind = (WindLp.Run(Noise()) * 0.8 + WindBp.Run(Noise()) * 0.25) * WindAmp;

		// Tyres: a squeal when one slides on tarmac, a scrub and rumble on grass.
		const double Roll = Clamp01(V / 4.0);
		double Tyre = 0.0;
		if (!Now.Airborne)
		{
			if (Now.OnTarmac)
			{
				const double Slide = Clamp01(std::max(std::max(Now.RearSlide, Now.FrontSlide), Now.SideSlide)) * Roll;
				SquealPhase += 2.0 * Pi * (1050.0 + 120.0 * std::sin(SquealPhase * 0.0007)) / Rate;
				if (SquealPhase > 2e6) SquealPhase -= 2e6;
				Tyre = (std::sin(SquealPhase) * 0.35 + Squeal.Run(Noise()) * 1.2) * Slide * 0.14
					+ Scrub.Run(Noise()) * 0.02 * std::min(V / 25.0, 1.5);
			}
			else
			{
				Tyre = Scrub.Run(Noise()) * 0.12 * std::min(V / 15.0, 1.5);
			}
		}

		double Lever = 0.0;
		if (Playing)
		{
			Lever = Sample(*Playing, ClunkPos) * ClunkGain;
			ClunkPos += 1.0;
			if (ClunkPos >= Playing->size() - 1) Playing = nullptr;
		}
		double Bg = 0.0;
		if (!Amb.empty())
		{
			BirdTimer -= 1.0 / Rate;
			if (BirdTimer <= 0.0)
			{
				const bool Start = BirdTarget < 0.5;
				BirdTarget = Start ? 0.6 + 0.4 * Uniform() : 0.0;
				BirdTimer = Start ? 5.0 + 9.0 * Uniform() : 25.0 + 35.0 * Uniform();
				if (Start && BirdGain < 0.05) AmbPos = Uniform() * (Amb.size() - 1);   // a different stretch each time
			}
			BirdGain += (BirdTarget - BirdGain) * BirdFade;
			if (BirdGain > 1e-4)
			{
				// Fainter at speed, where the wind covers it anyway.
				Bg = Amb[(size_t)AmbPos] * BirdGain * (1.0 - 0.6 * Clamp01(Now.Speed / 20.0));
			}
			AmbPos += 1.0;
			if (AmbPos >= Amb.size()) AmbPos = 0.0;
		}

		double Mix = (Engine + Wind + Tyre + Lever + Bg) * Volume;
		// DC block, then a soft limit.
		const double Hpo = Mix - HpIn + 0.995 * Hp;
		HpIn = Mix;
		Hp = Hpo;
		Out[S] = (float)std::tanh(Hpo);
	}
}
} // namespace TorqueSound
