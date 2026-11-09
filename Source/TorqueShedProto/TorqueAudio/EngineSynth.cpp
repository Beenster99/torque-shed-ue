#include "EngineSynth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace TorqueEngine
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Tau = 2.0 * Pi;
constexpr double LoopSeconds = 0.4;
constexpr int Warmup = 2400;
constexpr int WarmupTwoStroke = 9600;
constexpr double BucketStep = 1.2;
constexpr double SoundSpeedHot = 520.0;
constexpr double SoundSpeedAir = 343.0;
constexpr double PortDegrees = 180.0;
constexpr double PortShare = 0.5;
constexpr double TwoStrokeCrackle = 0.3;
constexpr double RingGain = 6.0;
constexpr double ValveTick = 0.02;

struct FVoiceDef
{
	double Hdr, Hdr2, Open, Sharp;
	double F1[3], F2[3];
	double Body, Intake, Runner, Airbox, Howl[2], RaspHz, Imbalance, Tail;
};
const FVoiceDef& VoiceOf(const std::string& Layout)
{
	static const std::map<std::string, FVoiceDef> Voices = {
		{"single", {0.95, 1.0, 240.0, 0.45, {380.0, 2.0, 0.5}, {1100.0, 3.0, 0.2}, 85.0, 0.5, 0.32, 140.0, {700.0, 3.0}, 1500.0, 0.0, 0.6}},
		{"twin", {0.85, 1.0, 235.0, 0.55, {480.0, 2.0, 0.45}, {1300.0, 3.0, 0.2}, 90.0, 0.45, 0.3, 150.0, {750.0, 3.0}, 1600.0, 0.06, 0.55}},
		{"parallel", {0.8, 1.0, 235.0, 0.5, {520.0, 2.0, 0.45}, {1450.0, 3.0, 0.25}, 85.0, 0.5, 0.28, 155.0, {820.0, 3.0}, 1700.0, 0.08, 0.55}},
		{"vtwin", {0.95, 0.7, 245.0, 0.42, {340.0, 1.8, 0.6}, {950.0, 3.0, 0.2}, 72.0, 0.5, 0.3, 130.0, {680.0, 3.0}, 1400.0, 0.12, 0.6}},
		{"boxer", {0.85, 1.07, 235.0, 0.55, {600.0, 2.5, 0.5}, {1500.0, 3.0, 0.2}, 95.0, 0.5, 0.3, 160.0, {900.0, 3.0}, 1700.0, 0.14, 0.5}},
		{"triple", {0.7, 1.0, 230.0, 0.5, {900.0, 2.5, 0.45}, {2100.0, 3.0, 0.3}, 100.0, 1.0, 0.24, 190.0, {1150.0, 3.5}, 2200.0, 0.18, 0.5}},
		{"inline", {0.5, 1.0, 225.0, 0.55, {1700.0, 2.5, 0.6}, {3100.0, 3.5, 0.4}, 110.0, 0.75, 0.2, 210.0, {1500.0, 3.0}, 2800.0, 0.05, 0.45}},
		{"2stroke", {0.0, 1.0, 190.0, 0.3, {2100.0, 3.0, 0.5}, {3400.0, 4.0, 0.35}, 120.0, 0.35, 0.18, 220.0, {1300.0, 3.0}, 3000.0, 0.05, 0.4}},
	};
	auto It = Voices.find(Layout);
	return It != Voices.end() ? It->second : Voices.at("parallel");
}

struct FExhaustDef
{
	double Lp, Body, Rasp, Ring, Gain, Pops;
};
bool HasExhaust(const std::string& Style)
{
	return Style == "stock" || Style == "slip" || Style == "full" || Style == "race" || Style == "straight";
}
const FExhaustDef& ExhaustOf(const std::string& Style)
{
	static const std::map<std::string, FExhaustDef> Styles = {
		{"stock", {1400.0, 0.9, 0.06, 0.25, 0.55, 0.25}},
		{"slip", {2400.0, 0.6, 0.16, 0.35, 0.75, 0.9}},
		{"full", {3300.0, 0.5, 0.24, 0.4, 0.9, 1.3}},
		{"race", {5000.0, 0.35, 0.36, 0.45, 1.05, 2.0}},
		{"straight", {8500.0, 0.2, 0.55, 0.55, 1.2, 2.8}},
	};
	auto It = Styles.find(Style);
	return It != Styles.end() ? It->second : Styles.at("slip");
}

double Clamp(double V, double A, double B) { return std::min(std::max(V, A), B); }
double Clamp01(double V) { return Clamp(V, 0.0, 1.0); }
double Lerp(double A, double B, double T) { return A + (B - A) * T; }
double SmoothStep(double A, double B, double X)
{
	if (A == B) return X < A ? 0.0 : 1.0;
	const double T = Clamp01((X - A) / (B - A));
	return T * T * (3.0 - 2.0 * T);
}
int CeilI(double V) { return (int)std::ceil(V); }
int RoundI(double V) { return (int)std::lround(V); }
int PosMod(int A, int N) { const int M = A % N; return M < 0 ? M + N : M; }
double FPosMod(double A, double N) { const double M = std::fmod(A, N); return M < 0.0 ? M + N : M; }

// FNV-1a: the same seed for the same recipe on every platform (the game and SimTest agree).
uint32_t Hash(const std::string& S)
{
	uint32_t H = 2166136261u;
	for (unsigned char C : S) { H ^= C; H *= 16777619u; }
	return H;
}

// Godot's RandomNumberGenerator calls, on a Mersenne Twister.
struct FRng
{
	std::mt19937 G;
	explicit FRng(uint32_t Seed) : G(Seed) {}
	double Randf() { return std::uniform_real_distribution<double>(0.0, 1.0)(G); }
	double Range(double A, double B) { return A + (B - A) * Randf(); }
	double Gauss(double Mean, double Sd) { return std::normal_distribution<double>(Mean, Sd)(G); }
	int IRange(int A, int B) { return std::uniform_int_distribution<int>(A, B)(G); }
	uint32_t Randi() { return G(); }
};

// A two-pole resonator's coefficients {b0, a1, a2} at Hz with quality Q; peak gain about 1.
struct FRes
{
	double B0, A1, A2;
};
FRes Res(double Hz, double Q)
{
	const double F = std::min(Hz, Rate * 0.45);
	const double Bw = F / std::max(Q, 0.3);
	const double R = std::exp(-Pi * Bw / Rate);
	return {(1.0 - R * R) * 0.5, 2.0 * R * std::cos(Tau * F / Rate), R * R};
}

struct FFiring
{
	double T = 0.0, A = 1.0;
	int Group = 0, Cyl = 0;
	double CrackW = 0.0;   // > 0: a crackle, its pulse this much shorter
};

struct FLoopInfo
{
	double Rpm;
	int N, Cycles;
};
FLoopInfo LoopInfo(const FSpec& S, double Rpm)
{
	const double CycleS = 60.0 / Rpm * CycleDegrees(S) / 360.0;
	const int Cycles = std::max(CeilI(LoopSeconds / CycleS), 2);
	const int N = RoundI(Cycles * CycleS * Rate);
	const double Exact = Cycles * CycleDegrees(S) / 360.0 * 60.0 * Rate / N;
	return {Exact, N, Cycles};
}

double PipeOn(const FSpec& S, double Rpm)
{
	return SmoothStep(S.Peak * 0.7, S.Peak * 0.95, Rpm) * (1.0 - 0.5 * SmoothStep(S.Peak * 1.12, S.Peak * 1.4, Rpm));
}

std::vector<FFiring> CycleEvents(const FSpec& S, const FLoopInfo& Info, bool bOn, FRng& Rng)
{
	const auto Fires = FiringEvents(S);
	const double CycleLen = (double)Info.N / Info.Cycles;
	const double Imbalance = VoiceOf(S.Layout).Imbalance;
	const double Rpm = Info.Rpm;
	const bool bTwo = IsTwoStroke(S);
	double Skip = S.Misfire * (bOn ? 0.5 : 1.0);
	double Crackle = 0.0;
	if (!bOn)
	{
		Skip += 0.08;
		if (bTwo)
		{
			Skip += 0.3 * Clamp01(1.0 - (Rpm - S.Idle) / (S.Peak * 0.5));
			Crackle = TwoStrokeCrackle * Clamp01((Rpm - S.Idle * 1.5) / (S.Peak * 0.5));
		}
	}
	else if (bTwo)
	{
		Skip += 0.08 * Clamp01(1.0 - Rpm / (S.Peak * 0.6));
	}
	double Jitter = 0.004 + (bTwo ? 0.02 * Clamp01(1.0 - (Rpm - S.Idle) / 2000.0) : 0.0);
	if (S.Crank == "45v") Jitter += 0.008 * Clamp01(1.0 - (Rpm - S.Idle) / 1500.0);
	std::vector<FFiring> Out;
	for (int C = 0; C < Info.Cycles; ++C)
	{
		for (int I = 0; I < (int)Fires.size(); ++I)
		{
			if (Rng.Randf() < Skip) continue;
			double A = 1.0 + Imbalance * (I == 0 ? 0.9 : (I == 1 ? -0.6 : 0.3));
			A *= bOn ? (1.0 + Rng.Gauss(0.0, 0.07)) : Rng.Range(0.35, 1.3);
			const double T = (C + Fires[I].first / CycleDegrees(S)) * CycleLen + Rng.Gauss(0.0, Jitter) * CycleLen;
			FFiring E;
			E.T = T;
			E.A = A;
			E.Group = Fires[I].second;
			E.Cyl = I;
			Out.push_back(E);
		}
		if (Crackle > 0.0 && Rng.Randf() < Crackle)
		{
			FFiring E;
			E.T = (C + Rng.Randf()) * CycleLen;
			E.A = Rng.Range(2.0, 4.0);
			E.Group = Rng.IRange(0, (int)Fires.size() - 1);
			E.Cyl = -1;
			E.CrackW = Rng.Range(0.08, 0.2);
			Out.push_back(E);
		}
	}
	return Out;
}

// The exhaust side: pulses down the headers, into the collector and silencer, out of the tail pipe.
std::vector<float> ExhaustSide(const FSpec& S, double Rpm, double LoadIn, const std::vector<FFiring>& Events, int N, int Warm,
	FRng& Rng, double Bright = 0.0)
{
	const FVoiceDef& Vo = VoiceOf(S.Layout);
	const FExhaustDef& Style = ExhaustOf(S.Exhaust);
	const bool bTwo = IsTwoStroke(S);
	const double PerCyl = S.Cc / S.Cylinders;
	const double DegSamples = 60.0 / Rpm / 360.0 * Rate;
	const double Tp = std::max(Vo.Open * 0.32 * DegSamples, 4.0);
	double Sharp = Vo.Sharp * (LoadIn > 0.5 ? 1.0 : 1.9);
	double Amp = std::pow(PerCyl / 250.0, 0.35) * (LoadIn > 0.5 ? 1.0 : 0.32);
	double Pipe = 0.0, Low = 0.0;
	if (bTwo)
	{
		Pipe = LoadIn > 0.5 ? PipeOn(S, Rpm) : 0.0;
		const double Power = LoadIn > 0.5 ? 0.5 + 0.85 * Pipe : 0.6;
		Sharp *= Lerp(1.0, 0.6, Pipe);
		Amp *= Power;
		Low = Clamp01(1.0 - (Rpm - S.Idle) / (S.Peak * 0.45));
	}
	std::vector<float> RingX;
	double Ding = 0.0;
	if (bTwo)
	{
		Ding = (LoadIn > 0.5 ? 0.45 : 1.0) * Low;
		if (Ding > 0.01) RingX.assign(N, 0.0f);
	}
	int Groups = 1;
	for (const FFiring& E : Events) Groups = std::max(Groups, E.Group + 1);
	const int Total = N + Warm;
	std::vector<float> Coll(Total, 0.0f), Env(N, 0.0f);
	const double Att = 0.5 + Sharp;
	const double Xpk = Att / (Att + 3.0);
	const double Norm = 1.0 / (std::pow(Xpk, Att) * std::pow(1.0 - Xpk, 3.0));
	for (int G = 0; G < Groups; ++G)
	{
		std::vector<float> Ex(N, 0.0f);
		for (const FFiring& E : Events)
		{
			if (E.Group != G) continue;
			const double T0 = E.T;
			const double A = E.A * Amp;
			const bool bCrack = E.CrackW > 0.0;
			const double Wid = bCrack ? std::max(Tp * E.CrackW, 3.0) : Tp;
			for (int Sx = CeilI(T0), End = CeilI(T0 + Wid); Sx < End; ++Sx)
			{
				const double X = (Sx - T0) / Wid;
				Ex[PosMod(Sx, N)] += (float)(A * Norm * std::pow(X, Att) * std::pow(1.0 - X, 3.0));
			}
			if (bTwo && !bCrack)
			{
				// The port open for half a turn: a broad slug of gas behind the spike.
				const double Pw = PortDegrees * DegSamples;
				const double Edge = std::max(20.0 * DegSamples, 3.0);
				const double Pa = A * PortShare;
				for (int Sx = CeilI(T0), End = CeilI(T0 + Pw); Sx < End; ++Sx)
				{
					const double U = Sx - T0;
					const double K = Clamp01(std::min(U, Pw - U) / Edge);
					Ex[PosMod(Sx, N)] += (float)(Pa * K * K * (3.0 - 2.0 * K));
				}
			}
			if (Ding > 0.01 && !bCrack) RingX[PosMod(CeilI(T0), N)] += (float)(A * Ding);
			// Blowdown turbulence: a burst of rasp at each pulse's front.
			const double TauS = std::max(Wid * 0.12, 8.0);
			const double Decay = std::exp(-1.0 / TauS);
			double K = 1.0;
			const int S0 = CeilI(T0);
			for (int J = 0, Len = (int)(TauS * 5.0); J < Len; ++J)
			{
				Env[PosMod(S0 + J, N)] += (float)(A * K);
				K *= Decay;
			}
		}
		double Length = Vo.Hdr * (G == 1 ? Vo.Hdr2 : 1.0);
		double Fb = -0.55;
		if (bTwo)
		{
			Length = SoundSpeedHot * 60.0 / S.Peak * 0.5 * (1.0 + 0.025 * G);
			Fb = 0.64 + 0.2 * Pipe;
		}
		const int D = (int)Clamp(RoundI(2.0 * Length / SoundSpeedHot * Rate), 2, 8000);
		std::vector<float> Line(D, 0.0f);
		int W = 0;
		double Lp = 0.0;
		int M = PosMod(-Warm, N);
		for (int I = 0; I < Total; ++I)
		{
			Lp += (Line[W] - Lp) * 0.55;
			const double Y = Ex[M] + Fb * Lp;
			Line[W] = (float)Y;
			if (++W >= D) W = 0;
			Coll[I] += (float)Y;
			if (++M >= N) M = 0;
		}
	}
	const int TailD = (int)Clamp(RoundI(2.0 * Vo.Tail / SoundSpeedHot * Rate), 2, 4000);
	std::vector<float> Tail(TailD, 0.0f);
	const double Ring = -Style.Ring;
	const double Rust = S.Rust;
	double LpHz = Style.Lp * (LoadIn > 0.5 ? 1.0 : 0.6) * (1.0 + Rust * 0.8) * (1.0 + Bright);
	if (bTwo) LpHz *= 1.5 + 0.8 * Pipe;
	const double Lpa = 1.0 - std::exp(-Tau * std::min(LpHz, Rate * 0.45) / Rate);
	const FRes R1 = Res(Vo.F1[0], Vo.F1[1]), R2 = Res(Vo.F2[0], Vo.F2[1]);
	const double G1 = Vo.F1[2] * 2.5, G2 = Vo.F2[2] * 2.5;
	const FRes Rb = Res(Vo.Body, 1.6);
	const double BodyG = Style.Body * 1.4;
	const FRes Rr = Res(Vo.RaspHz * (1.0 + 0.4 * Bright), 0.9);
	const double Gas = Clamp(Rpm / (S.Redline * 0.6), 0.35, 1.3);
	double RaspG = (Style.Rasp + Rust * 0.4) * (LoadIn > 0.5 ? 1.0 : 0.45) * 6.0 * Gas * (1.0 + Bright);
	if (bTwo) RaspG *= 1.0 + 2.5 * Low;
	const double RingHz = Lerp(2300.0, 1500.0, Clamp01((PerCyl - 50.0) / 450.0));
	const FRes RingR[3] = {Res(RingHz * 1.0, 70.0), Res(RingHz * 1.71, 70.0), Res(RingHz * 2.37, 70.0)};
	double RingS[6] = {0, 0, 0, 0, 0, 0};
	double RxA = 0.0, RxB = 0.0;
	std::vector<float> Noise(N);
	for (int I = 0; I < N; ++I) Noise[I] = (float)Rng.Range(-1.0, 1.0);
	std::vector<float> Out(N, 0.0f);
	int Tw = 0;
	double Tlp = 0.0, A1 = 0.0, A2 = 0.0, X1 = 0.0, X2 = 0.0, F1y1 = 0.0, F1y2 = 0.0, F2y1 = 0.0, F2y2 = 0.0;
	double By1 = 0.0, By2 = 0.0, Ry1 = 0.0, Ry2 = 0.0, Rx1 = 0.0, Rx2 = 0.0, Prev = 0.0, Dc = 0.0;
	const double Gain = Style.Gain * 0.55;
	const double Drive = 1.0 + Style.Ring;
	int M2 = PosMod(-Warm, N);
	for (int I = 0; I < Total; ++I)
	{
		Tlp += (Tail[Tw] - Tlp) * 0.5;
		const double Rn = Noise[M2] * Env[M2];
		const double Ra = Rr.B0 * (Rn - Rx2) + Rr.A1 * Ry1 - Rr.A2 * Ry2;
		Ry2 = Ry1;
		Ry1 = Ra;
		Rx2 = Rx1;
		Rx1 = Rn;
		if (++M2 >= N) M2 = 0;
		double Z = Coll[I] + Ring * Tlp;
		Tail[Tw] = (float)Z;
		Z += Ra * RaspG;
		if (++Tw >= TailD) Tw = 0;
		A1 += (Z - A1) * Lpa;
		A2 += (A1 - A2) * Lpa;
		const double F1 = R1.B0 * (Z - X2) + R1.A1 * F1y1 - R1.A2 * F1y2;
		F1y2 = F1y1;
		F1y1 = F1;
		const double F2 = R2.B0 * (Z - X2) + R2.A1 * F2y1 - R2.A2 * F2y2;
		F2y2 = F2y1;
		F2y1 = F2;
		const double Bo = Rb.B0 * (Z - X2) + Rb.A1 * By1 - Rb.A2 * By2;
		By2 = By1;
		By1 = Bo;
		X2 = X1;
		X1 = Z;
		double Flow = A2 + F1 * G1 + F2 * G2 + Bo * BodyG;
		if (!RingX.empty())
		{
			const double Rk = RingX[PosMod(I - Warm, N)];
			double Rsum = 0.0;
			for (int Q = 0; Q < 3; ++Q)
			{
				const double Yq = RingR[Q].B0 * (Rk - RxB) + RingR[Q].A1 * RingS[Q * 2] - RingR[Q].A2 * RingS[Q * 2 + 1];
				RingS[Q * 2 + 1] = RingS[Q * 2];
				RingS[Q * 2] = Yq;
				Rsum += Yq * (Q == 0 ? 1.0 : (Q == 1 ? 0.7 : 0.5));
			}
			RxB = RxA;
			RxA = Rk;
			Flow += Rsum * RingGain;
		}
		const double P = Flow - Prev * 0.8;
		Prev = Flow;
		Dc += (P - Dc) * 0.004;
		if (I >= Warm)
		{
			const double V = (P - Dc) * Gain * Drive;
			Out[I - Warm] = (float)(V / (1.0 + std::fabs(V)) / Drive * 1.6);
		}
	}
	return Out;
}

// The engine side: intake pulses through the runners and airbox, and the mechanical layer.
std::vector<float> IntakeSide(const FSpec& S, double Rpm, double LoadIn, const std::vector<FFiring>& Events, int N, int Warm, FRng& Rng)
{
	const FVoiceDef& Vo = VoiceOf(S.Layout);
	const bool bTwo = IsTwoStroke(S);
	const bool bCone = S.Intake != "airbox";
	const double PerCyl = S.Cc / S.Cylinders;
	const double DegSamples = 60.0 / Rpm / 360.0 * Rate;
	const double Tp = std::max(200.0 * DegSamples, 8.0);
	const bool bOn = LoadIn > 0.5;
	const double Amp = std::pow(PerCyl / 250.0, 0.3) * (bOn ? 1.0 : 0.3);
	const double CycleLen = 60.0 / Rpm * CycleDegrees(S) / 360.0 * Rate;
	const double IdleNess = Clamp01(1.0 - (Rpm - S.Idle) / 2500.0);
	std::vector<float> Ex(N, 0.0f), Env(N, 0.0f), Tick(N, 0.0f), Rattle(N, 0.0f);
	for (const FFiring& E : Events)
	{
		const double T0 = E.T + (bTwo ? 180.0 : 380.0) * DegSamples;
		const double A = std::fabs(E.A) * Amp * (bOn ? 1.0 : 0.6);
		for (int Sx = CeilI(T0), End = CeilI(T0 + Tp); Sx < End; ++Sx)
		{
			const double X = (Sx - T0) / Tp;
			const double Sv = std::sin(Pi * X);
			Ex[PosMod(Sx, N)] -= (float)(A * Sv * Sv);
		}
		const double TauS = std::max(Tp * 0.25, 10.0);
		const double Decay = std::exp(-1.0 / TauS);
		double K = 1.0;
		const int S0 = CeilI(T0);
		for (int J = 0, Len = (int)(TauS * 4.0); J < Len; ++J)
		{
			Env[PosMod(S0 + J, N)] += (float)(A * K);
			K *= Decay;
		}
		if (!bTwo)
		{
			const double Ticks[2] = {360.0 + 15.0, 540.0 + 45.0};
			const int Count = S.Valves <= 1 ? 1 : 2;
			for (int Q = 0; Q < Count; ++Q)
				Tick[PosMod(RoundI(E.T + Ticks[Q] * DegSamples), N)] += (float)(ValveTick * Rng.Range(0.7, 1.2));
		}
		if (S.DryClutch && IdleNess > 0.0)
			for (int J = 0; J < 3; ++J)
			{
				const int Tt = RoundI(E.T + Rng.Range(0.0, 0.8) * CycleLen / Events.size() * 2.0);
				Rattle[PosMod(Tt, N)] += (float)(Rng.Range(0.4, 1.0) * IdleNess * (!bOn ? 1.4 : 0.8));
			}
		if (bTwo)
		{
			const int Tt = RoundI(E.T + 20.0 * DegSamples);
			Rattle[PosMod(Tt, N)] += (float)(Rng.Range(0.3, 0.8) * (0.6 + 0.4 * IdleNess));
		}
	}
	const double Ding = bTwo ? (!bOn ? 0.9 : 0.35) * Clamp01(1.0 - (Rpm - S.Idle) / (S.Peak * 0.55)) : 0.0;
	const int RunnerD = (int)Clamp(RoundI(2.0 * Vo.Runner / SoundSpeedAir * Rate), 2, 4000);
	std::vector<float> Line(RunnerD, 0.0f);
	const FRes Rbox = Res(Vo.Airbox, 4.0), Rh = Res(Vo.Howl[0], Vo.Howl[1]), Rr = Res(Vo.RaspHz * (bCone ? 1.3 : 1.0), 1.1);
	const FRes Rt = Res(3300.0, 9.0), Rc = Res(2700.0, 5.0);
	const FRes D1 = Res(1870.0, 40.0), D2 = Res(3150.0, 45.0), D3 = Res(4420.0, 50.0);
	const double BoxG = bCone ? 0.25 : 1.0;
	const double Gas = Clamp(Rpm / (S.Redline * 0.6), 0.1, 1.3);
	const double RaspG = (bCone ? 1.4 : 1.0) * (bOn ? 1.0 : 0.35) * 0.9 * Gas;
	const double HowlG = bOn ? 1.2 : 0.4;
	const double MechG = 0.12 * Clamp(PerCyl / 300.0, 0.5, 1.4);
	std::vector<float> Noise(N);
	for (int I = 0; I < N; ++I) Noise[I] = (float)Rng.Range(-1.0, 1.0);
	const int Total = N + Warm;
	std::vector<float> Out(N, 0.0f);
	int W = 0;
	double Lp = 0.0, Bx1 = 0, Bx2 = 0, By1 = 0, By2 = 0, Hy1 = 0, Hy2 = 0, Rx1 = 0, Rx2 = 0, Ry1 = 0, Ry2 = 0;
	double Tx1 = 0, Tx2 = 0, Ty1 = 0, Ty2 = 0, Cx1 = 0, Cx2 = 0, Cy1 = 0, Cy2 = 0, Dx1 = 0, Dx2 = 0;
	double E1a = 0, E1b = 0, E2a = 0, E2b = 0, E3a = 0, E3b = 0, Prev = 0, Dc = 0;
	const double Gain = Vo.Intake * (bCone ? 1.5 : 1.0) * 0.5;
	const double Ila = 1.0 - std::exp(-Tau * (bCone ? 4500.0 : 3400.0) / Rate);
	double Il1 = 0.0, Il2 = 0.0;
	int M = PosMod(-Warm, N);
	for (int I = 0; I < Total; ++I)
	{
		Lp += (Line[W] - Lp) * 0.6;
		const double Y = Ex[M] - 0.5 * Lp;
		Line[W] = (float)Y;
		if (++W >= RunnerD) W = 0;
		const double Bo = Rbox.B0 * (Y - Bx2) + Rbox.A1 * By1 - Rbox.A2 * By2;
		By2 = By1;
		By1 = Bo;
		const double Ho = Rh.B0 * (Y - Bx2) + Rh.A1 * Hy1 - Rh.A2 * Hy2;
		Hy2 = Hy1;
		Hy1 = Ho;
		Bx2 = Bx1;
		Bx1 = Y;
		const double Rn = Noise[M] * Env[M];
		const double Ra = Rr.B0 * (Rn - Rx2) + Rr.A1 * Ry1 - Rr.A2 * Ry2;
		Ry2 = Ry1;
		Ry1 = Ra;
		Rx2 = Rx1;
		Rx1 = Rn;
		const double Flow = Y * 0.15 + Bo * BoxG + Ho * HowlG * 2.5 + Ra * RaspG;
		const double P0 = Flow - Prev * 0.75;
		Prev = Flow;
		Il1 += (P0 - Il1) * Ila;
		Il2 += (Il1 - Il2) * Ila;
		const double P = Il2;
		Dc += (P - Dc) * 0.004;
		const double Tk = Tick[M];
		const double To = Rt.B0 * (Tk - Tx2) + Rt.A1 * Ty1 - Rt.A2 * Ty2;
		Ty2 = Ty1;
		Ty1 = To;
		Tx2 = Tx1;
		Tx1 = Tk;
		const double Rk = Rattle[M] * (Noise[M] * 0.5 + 0.5);
		const double Co = Rc.B0 * (Rk - Cx2) + Rc.A1 * Cy1 - Rc.A2 * Cy2;
		Cy2 = Cy1;
		Cy1 = Co;
		Cx2 = Cx1;
		Cx1 = Rk;
		double Mech = (To * 0.6 + Co * 0.9) * MechG;
		if (Ding > 0.0)
		{
			const double Dk = Rattle[M];
			const double O1 = D1.B0 * (Dk - Dx2) + D1.A1 * E1a - D1.A2 * E1b;
			E1b = E1a;
			E1a = O1;
			const double O2 = D2.B0 * (Dk - Dx2) + D2.A1 * E2a - D2.A2 * E2b;
			E2b = E2a;
			E2a = O2;
			const double O3 = D3.B0 * (Dk - Dx2) + D3.A1 * E3a - D3.A2 * E3b;
			E3b = E3a;
			E3a = O3;
			Dx2 = Dx1;
			Dx1 = Dk;
			Mech += (O1 + O2 * 0.7 + O3 * 0.5) * Ding * 1.6;
		}
		if (++M >= N) M = 0;
		if (I >= Warm)
		{
			const double V = (P - Dc) * Gain + Mech;
			Out[I - Warm] = (float)(V / (1.0 + std::fabs(V)) * 1.4);
		}
	}
	return Out;
}

// A ringing metallic ping, a dull thud, a band of noise dying away; soft-limit.
void Ping(std::vector<float>& Out, int At, double Hz, double DecayS, double Amp, FRng& Rng)
{
	const double W = Tau * Hz / Rate, K = std::exp(-1.0 / (DecayS * Rate));
	double A = Amp, Ph = Rng.Randf() * Tau;
	for (int I = std::max(At, 0); I < (int)Out.size() && A > 0.0005; ++I)
	{
		Out[I] += (float)(std::sin(Ph) * A);
		Ph += W;
		A *= K;
	}
}
void Thump(std::vector<float>& Out, int At, double Hz, double DecayS, double Amp)
{
	const double K = std::exp(-1.0 / (DecayS * Rate));
	double A = Amp, Ph = 0.0, F = Hz * 1.3;
	for (int I = std::max(At, 0); I < (int)Out.size() && A > 0.0005; ++I)
	{
		Out[I] += (float)(std::sin(Ph) * A);
		F += (Hz - F) * 0.002;
		Ph += Tau * F / Rate;
		A *= K;
	}
}
void NoiseBurst(std::vector<float>& Out, int At, double DecayS, double Hz, double Amp, FRng& Rng)
{
	const FRes R = Res(Hz, 2.0);
	const double K = std::exp(-1.0 / (DecayS * Rate));
	double A = Amp, Y1 = 0, Y2 = 0, X1 = 0, X2 = 0;
	for (int I = std::max(At, 0); I < (int)Out.size() && A > 0.0005; ++I)
	{
		const double X = Rng.Range(-1.0, 1.0) * A * (Rng.Randf() < 0.4 ? 1.0 : 0.2);
		const double Y = R.B0 * (X - X2) + R.A1 * Y1 - R.A2 * Y2;
		Y2 = Y1;
		Y1 = Y;
		X2 = X1;
		X1 = X;
		Out[I] += (float)(Y * 3.0);
		A *= K;
	}
}
void Limit(std::vector<float>& Out)
{
	for (float& V : Out) V = (float)(V / (1.0 + std::fabs(V) * 0.5));
}

struct FStarterStyle
{
	double CrankRpm, Drag, MotorHz, Whine;
	bool bBendix;
};
FStarterStyle StarterStyle(const FSpec& S)
{
	const double Size = Clamp01((S.Cc - 50.0) / 1300.0);
	static const std::map<std::string, double> Crank = {{"single", 430.0}, {"twin", 340.0}, {"parallel", 350.0}, {"vtwin", 300.0},
		{"boxer", 320.0}, {"triple", 380.0}, {"inline", 400.0}, {"2stroke", 460.0}};
	static const std::map<std::string, double> Drag = {{"single", 0.55}, {"twin", 0.35}, {"parallel", 0.35}, {"vtwin", 0.5},
		{"boxer", 0.4}, {"triple", 0.25}, {"inline", 0.14}, {"2stroke", 0.3}};
	const double C = Crank.count(S.Layout) ? Crank.at(S.Layout) : 360.0;
	const double D = Drag.count(S.Layout) ? Drag.at(S.Layout) : 0.3;
	FStarterStyle Out;
	Out.CrankRpm = C * Lerp(1.1, 0.75, Size);
	Out.Drag = Clamp(D * Clamp(0.6 + S.Cc / S.Cylinders / 600.0, 0.6, 1.4), 0.05, 0.8);
	Out.MotorHz = Lerp(520.0, 230.0, Size);
	Out.Whine = (S.Layout == "inline" || S.Layout == "triple") ? 0.05 : 0.03;
	Out.bBendix = S.Shift == "clunk" || (S.Layout == "vtwin" && S.Cc >= 1000.0);
	return Out;
}

double CompressionLoad(const std::vector<std::pair<double, int>>& Fires, double Angle, double Cyc)
{
	double L = 0.0;
	for (const auto& F : Fires)
	{
		const double D = FPosMod(Angle - F.first, Cyc);
		if (D > Cyc - 90.0)
		{
			const double X = (D - (Cyc - 90.0)) / 90.0;
			L += X * X;
		}
		else if (D < 45.0)
		{
			L -= 0.4 * (1.0 - D / 45.0);
		}
	}
	return Clamp(L, -0.5, 1.0);
}

void ChuffsBetween(const std::vector<std::pair<double, int>>& Fires, double PrevAngle, double Angle, double Cyc, int At, double Amp, std::vector<FFiring>& Out)
{
	for (int C = 0; C < (int)Fires.size(); ++C)
	{
		const double Mark = FPosMod(Fires[C].first + 140.0, Cyc);
		if (FPosMod(PrevAngle - Mark, Cyc) > FPosMod(Angle - Mark, Cyc))
		{
			FFiring E;
			E.T = At;
			E.A = Amp;
			E.Group = Fires[C].second;
			E.Cyl = C;
			Out.push_back(E);
		}
	}
}

void CrankOver(std::vector<float>& Out, const FSpec& S, const FStarterStyle& Style, FRng& Rng, std::vector<FFiring>& Chuffs)
{
	const int N = (int)Out.size();
	const auto Fires = FiringEvents(S);
	const double Cyc = CycleDegrees(S);
	const double Battery = Rng.Range(0.9, 1.08);
	const double CrankRpm = Style.CrankRpm * Battery;
	const double Drag = Style.Drag * Rng.Range(0.9, 1.1);
	const double MotorHz = Style.MotorHz * Rng.Range(0.96, 1.04) * std::sqrt(Battery);
	Ping(Out, 0, Rng.Range(2300.0, 2800.0), 0.008, 0.35, Rng);
	Thump(Out, 0, 180.0, 0.012, 0.25);
	const int Start = (int)(Rate * Rng.Range(0.02, 0.04));
	if (Style.bBendix)
	{
		Ping(Out, Start, Rng.Range(1600.0, 1800.0), 0.02, 0.3, Rng);
		Ping(Out, Start, Rng.Range(780.0, 860.0), 0.04, 0.25, Rng);
		NoiseBurst(Out, Start, 0.02, 2000.0, 0.15, Rng);
	}
	static std::vector<float> Table;
	static std::once_flag TableOnce;
	std::call_once(TableOnce, [] {
		Table.assign(512, 0.0f);
		for (int J = 0; J < 512; ++J)
			for (int K = 1; K < 6; ++K) Table[J] += (float)(std::sin(Tau * J / 512.0 * K) / K);
	});
	double Angle = Rng.Randf() * Cyc, Speed = 0.0, Phase = 0.0, GearPh = 0.0, Hiss = 0.0;
	const int Step = 8;
	for (int I = Start; I < N; I += Step)
	{
		const double T = double(I - Start) / Rate;
		const double L = CompressionLoad(Fires, Angle, Cyc);
		const double Target = CrankRpm * Clamp01(T / 0.12) * (1.0 - Drag * L);
		Speed += (Target - Speed) * (1.0 - std::pow(0.996, Step));
		const double Was = Angle;
		Angle = std::fmod(Angle + Speed / 60.0 * 360.0 * Step / Rate, Cyc);
		ChuffsBetween(Fires, Was, Angle, Cyc, I, 0.35 * Clamp(Speed / CrankRpm, 0.2, 1.2), Chuffs);
		const double Hz = MotorHz * std::max(Speed / CrankRpm, 0.05);
		const double Strain = 0.85 + 0.35 * std::max(L, 0.0);
		const double FadeIn = Clamp01(T / 0.04);
		for (int J = 0; J < std::min(Step, N - I); ++J)
		{
			Phase = std::fmod(Phase + Hz / Rate, 1.0);
			GearPh = std::fmod(GearPh + Hz * 4.3 / Rate, 1.0);
			Hiss += (Rng.Range(-1.0, 1.0) - Hiss) * 0.35;
			Out[I + J] += (float)((Table[(int)(Phase * 512.0) & 511] * 0.08 * Strain + std::sin(Tau * GearPh) * Style.Whine + Hiss * 0.03 * Strain) * FadeIn);
		}
	}
}

void KickOver(std::vector<float>& Out, const FSpec& S, FRng& Rng, std::vector<FFiring>& Chuffs)
{
	const int N = (int)Out.size();
	const auto Fires = FiringEvents(S);
	const double Cyc = CycleDegrees(S);
	const double Size = Clamp01((S.Cc - 50.0) / 1300.0);
	Ping(Out, 0, Rng.Range(1150.0, 1350.0), 0.03, 0.4, Rng);
	Ping(Out, 0, Rng.Range(2900.0, 3300.0), 0.015, 0.25, Rng);
	NoiseBurst(Out, (int)(Rate * 0.01), 0.1, 900.0, 0.1, Rng);
	const double Push = Rng.Range(0.24, 0.32);
	const double PeakRpm = Rng.Range(850.0, 1200.0) * Lerp(1.1, 0.8, Size);
	double Angle = Rng.Randf() * Cyc, Speed = 0.0;
	const int Spin = std::min(N, (int)(Rate * (Push + 0.3)));
	for (int I = 0; I < Spin; ++I)
	{
		const double T = double(I) / Rate;
		const double L = CompressionLoad(Fires, Angle, Cyc);
		if (T < Push) Speed += (PeakRpm * (1.0 - 0.5 * L) - Speed) * 0.0015;
		else Speed = std::max(Speed - (40.0 + 260.0 * std::max(L, 0.0)) * 60.0 / Rate, 0.0);
		const double Was = Angle;
		Angle = std::fmod(Angle + Speed / 60.0 * 360.0 / Rate, Cyc);
		ChuffsBetween(Fires, Was, Angle, Cyc, I, 0.55 * Clamp01(Speed / PeakRpm), Chuffs);
	}
	const int Back = (int)(Rate * (Push + Rng.Range(0.06, 0.1)));
	for (int K = 0; K < 5; ++K)
		Ping(Out, Back + (int)(Rate * 0.012 * K), Rng.Range(2400.0, 2800.0), 0.004, 0.16 * (1.0 - K * 0.12), Rng);
	const int Top = Back + (int)(Rate * 0.075);
	Ping(Out, Top, Rng.Range(1300.0, 1500.0), 0.025, 0.35, Rng);
	Thump(Out, Top, 300.0, 0.015, 0.2);
}

uint16_t U16(const uint8_t* P) { return (uint16_t)(P[0] | (P[1] << 8)); }
uint32_t U32(const uint8_t* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | ((uint32_t)P[3] << 24); }

double Rms(const std::vector<float>& S)
{
	double E = 0.0;
	for (float V : S) E += (double)V * V;
	return S.empty() ? 0.0 : std::sqrt(E / S.size());
}

bool IsNumber(const std::string& S)
{
	if (S.empty()) return false;
	char* End = nullptr;
	std::strtod(S.c_str(), &End);
	return End && *End == '\0';
}
} // namespace

// ---------------------------------------------------------------------------- the recipe

FSpec MakeSpec(FSpec S)
{
	static const char* Known[] = {"single", "twin", "parallel", "vtwin", "boxer", "triple", "inline", "2stroke"};
	bool bKnown = false;
	for (const char* K : Known) bKnown = bKnown || S.Layout == K;
	if (!bKnown) S.Layout = S.Cylinders >= 4 ? "inline" : (S.Cylinders == 3 ? "triple" : (S.Cylinders == 2 ? "parallel" : "single"));
	if (S.Crank.empty())
	{
		static const std::map<std::string, std::string> Usual = {{"inline", "flat"}, {"parallel", "270"}, {"twin", "360"},
			{"vtwin", "90v"}, {"boxer", "boxer"}, {"triple", "240"}};
		auto It = Usual.find(S.Layout);
		S.Crank = It != Usual.end() ? It->second : S.Layout;
	}
	if (S.Peak <= 0.0) S.Peak = S.Redline * 0.75;
	if (!HasExhaust(S.Exhaust)) S.Exhaust = "slip";
	S.Cylinders = std::max(S.Cylinders, 1);
	S.Cc = std::max(S.Cc, 20.0);
	S.Redline = std::max(S.Redline, S.Idle * 2.0);
	return S;
}

std::string Signature(const FSpec& S)
{
	char Buf[512];
	std::snprintf(Buf, sizeof(Buf), "%s|%d|%d|%s|%d|%d|%d|%s|%s|%s|%d|%d|%s|%s|%s%s", S.Layout.c_str(), S.Cylinders, RoundI(S.Cc / 25.0),
		S.Crank.c_str(), RoundI(S.Idle / 50.0), RoundI(S.Redline / 100.0), RoundI(S.Peak / 100.0), S.Exhaust.c_str(), S.Intake.c_str(),
		S.DryClutch ? "true" : "false", RoundI(S.Rust * 4.0), RoundI(S.Misfire * 10.0), S.Kick ? "true" : "false", S.Shift.c_str(),
		S.Quickshifter ? "true" : "false", S.Valves <= 1 ? "|1v" : "");
	return Buf;
}

bool IsTwoStroke(const FSpec& S) { return S.Layout == "2stroke"; }
double CycleDegrees(const FSpec& S) { return IsTwoStroke(S) ? 360.0 : 720.0; }

std::vector<std::pair<double, int>> FiringEvents(const FSpec& S)
{
	const std::string& C = S.Crank;
	if (C == "single") return {{0.0, 0}};
	if (C == "360") return {{0.0, 0}, {360.0, 0}};
	if (C == "270") return {{0.0, 0}, {270.0, 0}};
	if (C == "90v") return {{0.0, 0}, {270.0, 1}};
	if (C == "45v") return {{0.0, 0}, {315.0, 1}};
	if (C == "60v") return {{0.0, 0}, {300.0, 1}};
	if (C == "boxer") return {{0.0, 0}, {360.0, 1}};
	if (C == "240") return {{0.0, 0}, {240.0, 0}, {480.0, 0}};
	if (C == "flat") return {{0.0, 0}, {180.0, 0}, {360.0, 0}, {540.0, 0}};
	if (C == "crossplane") return {{0.0, 0}, {270.0, 0}, {450.0, 0}, {540.0, 0}};
	if (C == "bigbang") return {{0.0, 0}, {90.0, 0}, {360.0, 0}, {450.0, 0}};
	std::vector<std::pair<double, int>> Out;
	if (C == "2stroke")
	{
		for (int I = 0; I < S.Cylinders; ++I) Out.push_back({360.0 * I / S.Cylinders, I});
		return Out;
	}
	for (int I = 0; I < S.Cylinders; ++I) Out.push_back({CycleDegrees(S) * I / S.Cylinders, 0});
	return Out;
}

double FiringHz(const FSpec& S, double Rpm)
{
	return Rpm / 60.0 * FiringEvents(S).size() * (IsTwoStroke(S) ? 1.0 : 0.5);
}

double LoopRpm(const FSpec& S, double Rpm) { return LoopInfo(S, Rpm).Rpm; }

std::vector<double> BucketRpms(const FSpec& S)
{
	const double Lo = S.Idle * 0.85, Hi = S.Redline * 1.05;
	const int Count = (int)Clamp(CeilI(std::log(Hi / Lo) / std::log(BucketStep)) + 1, 5, 20);
	std::vector<double> Out;
	for (int I = 0; I < Count; ++I) Out.push_back(LoopRpm(S, Lo * std::pow(Hi / Lo, double(I) / (Count - 1))));
	return Out;
}

std::vector<float> RenderLoop(const FSpec& S, int Layer, double Rpm, int Seed)
{
	const FLoopInfo Info = LoopInfo(S, Rpm);
	const bool bOn = Layer == ExOn || Layer == InOn;
	const bool bEx = Layer == ExOn || Layer == ExOff;
	FRng Rng(Hash(Signature(S)) + Seed * 7919u + (bOn ? 0u : 31u) + (bEx ? 0u : 101u));
	const std::vector<FFiring> Events = CycleEvents(S, Info, bOn, Rng);
	const int Warm = IsTwoStroke(S) ? WarmupTwoStroke : Warmup;
	if (bEx) return ExhaustSide(S, Info.Rpm, bOn ? 1.0 : 0.0, Events, Info.N, Warm, Rng);
	return IntakeSide(S, Info.Rpm, bOn ? 1.0 : 0.0, Events, Info.N, Warm, Rng);
}

std::vector<float> RenderPop(const FSpec& S, double Strength, int Seed)
{
	FRng Rng(Hash(Signature(S)) + Seed * 131u);
	const int N = (int)(Rate * (0.3 + 0.12 * Strength));
	std::vector<FFiring> Events;
	FFiring First;
	First.T = 4.0;
	First.A = 2.2 * Strength;
	Events.push_back(First);
	const bool bTwo = IsTwoStroke(S);
	const int Crackles = Rng.IRange(1, 2) + (int)(Strength * 1.5) + (bTwo ? 3 : 0);
	double T = 4.0;
	for (int I = 0; I < Crackles; ++I)
	{
		T += bTwo ? Rng.Range(0.005, 0.028) * Rate : Rng.Range(0.012, 0.05) * Rate;
		if (T > N * 0.7) break;
		FFiring E;
		E.T = T;
		E.A = Rng.Range(0.4, 1.0) * Strength * 1.2 / (1.0 + I * 0.4);
		Events.push_back(E);
	}
	const double Rpm = 9000.0 / std::max(Strength, 1.0);
	std::vector<float> Out = ExhaustSide(S, Rpm, 1.0, Events, N, 0, Rng, 0.6);
	const int Fade = (int)(Rate * 0.04);
	for (int I = 0; I < Fade && I < N; ++I) Out[N - 1 - I] *= (float)I / Fade;
	return Out;
}

std::vector<float> RenderStarter(const FSpec& S, bool bKick, double Seconds, int Seed)
{
	FRng Rng(Hash(Signature(S)) + Seed * 7717u + (bKick ? 5u : 3u));
	const int N = (int)(Rate * Seconds);
	std::vector<float> Out(N, 0.0f);
	std::vector<FFiring> Chuffs;
	if (bKick) KickOver(Out, S, Rng, Chuffs);
	else CrankOver(Out, S, StarterStyle(S), Rng, Chuffs);
	if (!Chuffs.empty())
	{
		const std::vector<float> Puffs = ExhaustSide(S, 900.0, 0.0, Chuffs, N, 0, Rng);
		for (int I = 0; I < N; ++I) Out[I] += Puffs[I] * 1.6f;
	}
	const int Fade = (int)(Rate * 0.03);
	for (int I = 0; I < Fade && I < N; ++I) Out[N - 1 - I] *= (float)I / Fade;
	Limit(Out);
	return Out;
}

std::vector<float> RenderCatch(const FSpec& S, bool bKick, int Seed)
{
	FRng Rng(Hash(Signature(S)) + Seed * 3331u + (bKick ? 9u : 1u));
	const int N = (int)(Rate * 0.5);
	const FStarterStyle Style = StarterStyle(S);
	const auto Fires = FiringEvents(S);
	const double Cyc = CycleDegrees(S);
	const bool bLumpy = S.Layout == "single" || S.Layout == "vtwin" || S.Layout == "boxer" || S.Layout == "twin" || S.Layout == "2stroke";
	const double Flare = S.Idle * (bLumpy ? Rng.Range(1.7, 2.3) : Rng.Range(1.4, 1.8));
	const double Rise = Rng.Range(0.1, 0.16);
	const double From = Style.CrankRpm * 1.2;
	std::vector<FFiring> Events;
	double Angle = 0.0;
	for (int I = 0; I < N; ++I)
	{
		const double T = double(I) / Rate;
		const double Rpm = T < Rise ? Lerp(From, Flare, SmoothStep(0.0, Rise, T)) : Lerp(Flare, S.Idle * 1.15, 1.0 - std::exp(-(T - Rise) / 0.12));
		const double Was = Angle;
		Angle = std::fmod(Angle + Rpm / 60.0 * 360.0 / Rate, Cyc);
		for (int C = 0; C < (int)Fires.size(); ++C)
		{
			const double At = Fires[C].first;
			if (FPosMod(Was - At, Cyc) > FPosMod(Angle - At, Cyc))
			{
				const double Miss = (bLumpy ? 0.35 : 0.2) * Clamp01(1.0 - T / 0.2);
				if (Rng.Randf() >= Miss)
				{
					FFiring E;
					E.T = I;
					E.A = Rng.Range(0.7, 1.2);
					E.Group = Fires[C].second;
					E.Cyl = C;
					Events.push_back(E);
				}
			}
		}
	}
	std::vector<float> Out = ExhaustSide(S, Flare, 1.0, Events, N, 0, Rng);
	for (int I = 0; I < N; ++I)
	{
		const double T = double(I) / Rate;
		Out[I] *= (float)(0.8 * (T < 0.15 ? 1.0 : std::cos(Clamp01((T - 0.15) / 0.35) * Pi * 0.5)));
	}
	if (!bKick)
	{
		Ping(Out, 0, 2200.0, 0.006, 0.15, Rng);
		double Phase = 0.0;
		const double Hz = Style.MotorHz * 1.3;
		const int Len = (int)(Rate * 0.18);
		for (int I = 0; I < Len && I < N; ++I)
		{
			const double X = double(I) / Len;
			Phase = std::fmod(Phase + Hz * (1.0 - X) / Rate, 1.0);
			Out[I] += (float)((std::sin(Tau * Phase) + 0.4 * std::sin(Tau * Phase * 2.0)) * 0.05 * (1.0 - X) * (1.0 - X));
		}
	}
	Limit(Out);
	return Out;
}

double PopRate(const FSpec& S, double Rpm, double OverrunS)
{
	const FExhaustDef& Style = ExhaustOf(S.Exhaust);
	const double Lo = std::max(S.Idle * 2.2, 3000.0);
	if (Rpm <= Lo) return 0.0;
	const double Revs = Clamp01((Rpm - Lo) / (S.Redline * 0.5));
	const double Settle = 0.3 + 0.7 * std::exp(-OverrunS / 1.2);
	const double Two = IsTwoStroke(S) ? 1.6 : 1.0;
	return Style.Pops * 3.0 * Revs * Settle * (1.0 + S.Rust) * Two;
}

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
			for (size_t I = 0; I < Frames; ++I) Out[I] = (int16_t)U16(Body + I * 2 * Channels) / 32768.0f;
			return Out;
		}
		P += 8 + Len + (Len & 1);
	}
	return Out;
}

// ---------------------------------------------------------------------------- the bank

std::shared_ptr<FBank> FBank::For(const FSpec& S, const FReader& Read, const FLister& List)
{
	static std::mutex Lock;
	static std::map<std::string, std::shared_ptr<FBank>> Banks;
	const FSpec Full = MakeSpec(S);
	const std::string Key = Signature(Full);
	std::lock_guard<std::mutex> Guard(Lock);
	auto It = Banks.find(Key);
	if (It != Banks.end()) return It->second;
	if (Banks.size() >= 10)
	{
		// Forget a finished bank nobody's playing.
		for (auto Old = Banks.begin(); Old != Banks.end(); ++Old)
			if (Old->second.use_count() == 1 && Old->second->Finished()) { Banks.erase(Old); break; }
	}
	auto Bank = std::make_shared<FBank>(Full);
	// A folder of recorded loops: the most specific of <layout>_<cylinders>_<exhaust>,
	// <layout>_<cylinders>, <layout>_<exhaust>, <layout>.
	const std::string Keys[4] = {Full.Layout + "_" + std::to_string(Full.Cylinders) + "_" + Full.Exhaust,
		Full.Layout + "_" + std::to_string(Full.Cylinders), Full.Layout + "_" + Full.Exhaust, Full.Layout};
	if (List)
		for (const std::string& K : Keys)
		{
			const std::string Dir = "Audio/TS/engines/" + K;
			if (!List(Dir).empty())
			{
				Bank->LoadRecorded(Dir, Read, List);
				break;
			}
		}
	if (!Bank->bRecorded) Bank->LoadIdle(Read, List);
	Banks[Key] = Bank;
	return Bank;
}

FBank::FBank(const FSpec& S) : Spec(MakeSpec(S))
{
	Sig = Signature(Spec);
	Rpms = BucketRpms(Spec);
	for (size_t I = 0; I < Rpms.size(); ++I) Buckets.push_back(std::make_unique<FBucket>());
}

std::vector<int> FBank::BakeOrder() const
{
	std::vector<int> Order;
	for (int I = 0; I < (int)Rpms.size(); ++I) Order.push_back(I);
	const double Idle = Spec.Idle;
	std::stable_sort(Order.begin(), Order.end(), [this, Idle](int A, int B) {
		return std::fabs(std::log(Rpms[A] / Idle)) < std::fabs(std::log(Rpms[B] / Idle));
	});
	return Order;
}

void FBank::LoadRecorded(const std::string& Dir, const FReader& Read, const FLister& List)
{
	static const char* LayerNames[Layers] = {"ex_on", "ex_off", "in_on", "in_off"};
	std::map<double, std::array<std::vector<float>, Layers>> Found;
	for (const std::string& File : List(Dir))
	{
		if (File.size() < 5 || File.substr(File.size() - 4) != ".wav") continue;
		const std::string Base = File.substr(0, File.size() - 4);
		for (int L = 0; L < Layers; ++L)
		{
			const std::string Prefix = std::string(LayerNames[L]) + "_";
			if (Base.compare(0, Prefix.size(), Prefix) != 0 || !IsNumber(Base.substr(Prefix.size()))) continue;
			int R = Rate;
			std::vector<float> S = ReadWav(Read(Dir + "/" + File), R);
			if (S.empty()) continue;
			LoopRate = R;
			Found[std::atof(Base.substr(Prefix.size()).c_str())][L] = std::move(S);
		}
	}
	if (Found.empty()) return;
	Rpms.clear();
	Buckets.clear();
	bExhaustOnly = true;
	for (auto& KV : Found)
	{
		auto& Lay = KV.second;
		for (int Side = 0; Side < 2; ++Side)
		{
			std::vector<float>& On = Lay[Side * 2];
			std::vector<float>& Off = Lay[Side * 2 + 1];
			if (On.empty() && !Off.empty()) On = Off;
			else if (Off.empty() && !On.empty()) Off = On;
		}
		if (!Lay[InOn].empty()) bExhaustOnly = false;
		Rpms.push_back(KV.first);
		auto B = std::make_unique<FBucket>();
		B->Loop = Lay;
		B->bReady = true;
		Buckets.push_back(std::move(B));
	}
	bRecorded = true;
}

void FBank::LoadIdle(const FReader& Read, const FLister& List)
{
	if (!List) return;
	const std::string Keys[4] = {Spec.Layout + "_" + Spec.Crank + "_" + Spec.Exhaust, Spec.Layout + "_" + Spec.Crank,
		Spec.Layout + "_" + Spec.Exhaust, Spec.Layout};
	std::map<std::string, std::string> ByKey;
	const std::string Dir = "Audio/TS/engines/idle";
	for (const std::string& File : List(Dir))
	{
		if (File.size() < 5 || File.substr(File.size() - 4) != ".wav") continue;
		const std::string Base = File.substr(0, File.size() - 4);
		const size_t Cut = Base.rfind('_');
		if (Cut != std::string::npos && Cut > 0 && IsNumber(Base.substr(Cut + 1))) ByKey[Base.substr(0, Cut)] = File;
	}
	for (const std::string& K : Keys)
	{
		auto It = ByKey.find(K);
		if (It == ByKey.end()) continue;
		int R = 44100;
		std::vector<float> S = ReadWav(Read(Dir + "/" + It->second), R);
		if (S.size() < 500) return;
		IdleRec = std::move(S);
		IdleRecRate = R;
		const std::string Base = It->second.substr(0, It->second.size() - 4);
		IdleRecRpm = std::atof(Base.substr(Base.rfind('_') + 1).c_str());
		IdleRecRms = Rms(IdleRec);
		IdleBucket = BakeOrder()[0];
		return;
	}
}

void FBank::Bake()
{
	bStarted = true;
	const auto T0 = std::chrono::steady_clock::now();
	int Done = 0;
	if (!bRecorded)
		for (int I : BakeOrder())
		{
			FBucket& B = *Buckets[I];
			for (int L = 0; L < Layers; ++L) B.Loop[L] = RenderLoop(Spec, L, Rpms[I], I + 1);
			if (I == IdleBucket)
			{
				IdleMadeEx = Rms(B.Loop[ExOff]);
				IdleMadeIn = Rms(B.Loop[InOff]);
			}
			B.bReady = true;
			if (++Done == 2) BakeShots();
		}
	if (Done < 2) BakeShots();
	BakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
	bFinished = true;
}

void FBank::BakeShots()
{
	if (bShotsReady) return;
	for (int I = 0; I < 3; ++I) Starters.push_back(RenderStarter(Spec, Spec.Kick, 1.0, I + 1));
	for (int I = 0; I < 2; ++I) Catches.push_back(RenderCatch(Spec, Spec.Kick, I + 1));
	for (int I = 0; I < 4; ++I) Pops.push_back(RenderPop(Spec, 0.7 + 0.25 * I, I + 1));
	Bangs.push_back(RenderPop(Spec, 2.5, 11));
	Bangs.push_back(RenderPop(Spec, 2.2, 12));
	Cracks.push_back(RenderPop(Spec, 4.0, 21));
	bShotsReady = true;
}

// ---------------------------------------------------------------------------- the voice

namespace
{
constexpr double GainEase = 0.02;
constexpr double IdleHandover[2] = {0.1, 0.6};
constexpr double IdlePitch[2] = {0.85, 1.15};
constexpr int PlayersPerLayer = 4;
constexpr int UpdateEvery = 64;   // samples between the voice working out what it wants
} // namespace

FVoice::FVoice(std::shared_ptr<FBank> InBank, int InOutRate) : BankRef(std::move(InBank)), OutRate(InOutRate)
{
	for (int L = 0; L < Layers; ++L)
		for (int I = 0; I < PlayersPerLayer; ++I)
		{
			FPlayer P;
			P.Layer = L;
			Players.push_back(P);
		}
	Shots.resize(4);
	Rng.seed(Hash(BankRef->Sig));
	IdlePos = BankRef->IdleRec.empty() ? 0.0 : Uni(0.0, (double)BankRef->IdleRec.size() - 2);
}

double FVoice::At(const std::vector<float>& S, double Pos)
{
	const size_t I = (size_t)Pos;
	if (I + 1 >= S.size()) return S.empty() ? 0.0 : S.back();
	const double F = Pos - I;
	return S[I] + (S[I + 1] - S[I]) * F;
}

int FVoice::Sounding() const
{
	int N = 0;
	for (const FPlayer& P : Players) N += P.bOn ? 1 : 0;
	return N;
}

void FVoice::PlayShot(const std::vector<std::vector<float>>& From, double Gain, double Pitch)
{
	if (!BankRef->bShotsReady || From.empty()) return;
	FShot* Slot = &Shots[0];
	for (FShot& S : Shots)
		if (!S.S) { Slot = &S; break; }
	Slot->S = &From[std::uniform_int_distribution<size_t>(0, From.size() - 1)(Rng)];
	Slot->Pos = 0.0;
	Slot->Step = Pitch * Rate / OutRate;
	Slot->Gain = Gain;
}

void FVoice::Catch()
{
	if (BankRef->bShotsReady && !BankRef->Catches.empty())
	{
		PlayShot(BankRef->Catches, 1.0, Uni(0.97, 1.03));
		++Catches;
	}
	CatchT = 0.0;
	Blip = 0.3;
}

// EngineSound._process_body: the loads, the level, the mix, the overrun.
void FVoice::Update(double Dt)
{
	const FBank& B = *BankRef;
	const FSpec& S = B.Spec;
	double Target = std::max(Want.Throttle, Blip);
	if (Want.bCut) Target = std::min(Target, 0.04);
	Load += (Target - Load) * (1.0 - std::exp(-Dt * (Target > Load ? 30.0 : 16.0)));
	Blip = std::max(Blip - Dt * 6.0, 0.0);
	const double Alive = Want.bRunning ? 1.0 : 0.0;
	CatchT += Dt;
	const double Rise = CatchT < 0.3 ? 5.0 : 14.0;
	Level += (Alive - Level) * (1.0 - std::exp(-Dt * (Want.bRunning ? Rise : 4.0)));
	const double Rpm = std::max(Want.Rpm, 1.0);
	const double Life = Level * Clamp01((Rpm - S.Idle * 0.25) / (S.Idle * 0.5));
	// In the helmet you hear the airbox, behind the bike the pipe; someone else's bike is mostly pipe.
	double Mix[2] = {Lerp(1.0, 0.75, Want.Inside), Lerp(0.7, 1.3, Want.Inside)};
	if (Want.bRemote) { Mix[0] = 1.0; Mix[1] = 0.55; }
	if (B.bExhaustOnly) Mix[0] = std::max(Mix[0], Mix[1]);
	const double Loud = 0.75 + 0.35 * S.Loud;

	// The recorded idle has tickover to itself, crossfading into the made sound as the revs rise.
	double Rec = 0.0;
	const double MadeEx = B.IdleMadeEx.load(), MadeIn = B.IdleMadeIn.load();
	if (!B.IdleRec.empty() && (MadeEx > 0.0 || MadeIn > 0.0))
	{
		const double U = Clamp01((Rpm / S.Idle - 1.0 - IdleHandover[0]) / (IdleHandover[1] - IdleHandover[0]));
		Rec = std::cos(U * Pi * 0.5) * Lerp(1.0, 0.4, Clamp01(Load));
	}
	const double Made = std::sqrt(std::max(1.0 - Rec * Rec, 0.0));
	IdleShare = Rec * Life;
	IdleTarget = 0.0;
	if (IdleShare > 0.0 && B.IdleRecRms > 0.0)
		IdleTarget = IdleShare * std::hypot(MadeEx * Mix[0] * Loud, MadeIn * Mix[1]) / std::max(B.IdleRecRms, 0.001);
	if (B.IdleRecRpm > 0.0)
		IdleStep = Clamp(Clamp(S.Idle / B.IdleRecRpm, IdlePitch[0], IdlePitch[1]) * Rpm / S.Idle, 0.05, 4.0) * B.IdleRecRate / OutRate;

	// The two baked buckets either side of the revs, crossfaded (equal power), each pitched to them;
	// on and off the throttle crossfaded the same way.
	for (FPlayer& P : Players) P.bWanted = false;
	if (Life > 0.001)
	{
		int Lo = -1, Hi = -1;
		for (int I = 0; I < (int)B.Rpms.size(); ++I)
		{
			if (!B.Buckets[I]->bReady) continue;
			if (B.Rpms[I] <= Rpm) Lo = I;
			else if (Hi < 0) Hi = I;
		}
		std::pair<int, double> Pairs[2] = {{-1, 0.0}, {-1, 0.0}};
		if (Lo >= 0 && Hi >= 0)
		{
			const double U = std::log(Rpm / B.Rpms[Lo]) / std::log(B.Rpms[Hi] / B.Rpms[Lo]);
			Pairs[0] = {Lo, std::cos(U * Pi * 0.5)};
			Pairs[1] = {Hi, std::sin(U * Pi * 0.5)};
		}
		else if (Lo >= 0 || Hi >= 0)
		{
			Pairs[0] = {Lo >= 0 ? Lo : Hi, 1.0};
		}
		const double L = Clamp01(Load);
		const double WOn = std::sin(L * Pi * 0.5), WOff = std::cos(L * Pi * 0.5);
		for (const auto& Pr : Pairs)
		{
			if (Pr.first < 0) continue;
			const double Pitch = Rpm / B.Rpms[Pr.first];
			for (int Layer = 0; Layer < Layers; ++Layer)
			{
				const bool bEx = Layer == ExOn || Layer == ExOff;
				const bool bOnLayer = Layer == ExOn || Layer == InOn;
				const double W = bOnLayer ? WOn : WOff;
				if (W <= 0.001) continue;
				if (B.Buckets[Pr.first]->Loop[Layer].empty()) continue;
				const double G = Pr.second * W * Life * Made * (bEx ? Mix[0] * Loud : Mix[1]);
				// Its player: the one on it already, else a free one (or the quietest fading out).
				FPlayer* Use = nullptr;
				for (FPlayer& P : Players)
					if (P.Layer == Layer && P.bOn && P.Bucket == Pr.first) { Use = &P; break; }
				if (!Use)
				{
					if (G < 0.002) continue;
					for (FPlayer& P : Players)
						if (P.Layer == Layer && !P.bOn) { Use = &P; break; }
					if (!Use)
						for (FPlayer& P : Players)
							if (P.Layer == Layer && !P.bWanted && (!Use || P.Gain < Use->Gain)) Use = &P;
					if (!Use) continue;
					Use->bOn = true;
					Use->Bucket = Pr.first;
					Use->Gain = 0.0;
					// Each loop starts somewhere different so the layers don't line up and phase.
					Use->Pos = Uni(0.0, (double)B.Buckets[Pr.first]->Loop[Layer].size() - 1.0);
				}
				Use->bWanted = true;
				Use->Target = G;
				Use->Pitch = Pitch;
			}
		}
	}
	for (FPlayer& P : Players)
		if (!P.bWanted) P.Target = 0.0;

	// Overrun: the throttle shut with the revs up pops, more from a loud pipe.
	if (Want.bRunning && Want.Throttle < 0.08 && !Want.bCut)
	{
		Overrun += Dt;
		const double PerSec = PopRate(S, Rpm, Overrun);
		if (PerSec > 0.0 && Uni(0.0, 1.0) < PerSec * Dt)
		{
			const double Pops2 = ExhaustOf(S.Exhaust).Pops;
			const double Ex = Mix[0] * std::pow(10.0, 2.0 * (S.Loud - 1.0) / 20.0);
			if (Uni(0.0, 1.0) < 0.04 * Pops2) PlayShot(B.Cracks, Ex, Uni(0.9, 1.1));
			else PlayShot(B.Pops, Uni(0.6, 1.1) * Ex, Uni(0.9, 1.1));
			++Pops;
		}
	}
	else
	{
		Overrun = 0.0;
	}
}

void FVoice::Render(float* Out, int N)
{
	const FBank& B = *BankRef;
	const double Ease = 1.0 - std::exp(-1.0 / (GainEase * OutRate));
	const double LoopStep = (double)B.LoopRate / OutRate;
	for (int I = 0; I < N; ++I)
	{
		if (Block-- <= 0)
		{
			Update((double)UpdateEvery / OutRate);
			Block = UpdateEvery - 1;
		}
		double Sum = 0.0;
		for (FPlayer& P : Players)
		{
			if (!P.bOn) continue;
			P.Gain += (P.Target - P.Gain) * Ease;
			if (!P.bWanted && P.Gain < 0.0005)
			{
				P.bOn = false;
				continue;
			}
			const std::vector<float>& L = B.Buckets[P.Bucket]->Loop[P.Layer];
			const size_t Len = L.size();
			// Seamless: the loop's start read on past its end.
			const size_t K = (size_t)P.Pos;
			const double F = P.Pos - K;
			const double A = L[K % Len], Bv = L[(K + 1) % Len];
			Sum += (A + (Bv - A) * F) * P.Gain;
			P.Pos += P.Pitch * LoopStep;
			while (P.Pos >= Len) P.Pos -= Len;
		}
		if (!B.IdleRec.empty())
		{
			IdleGain += (IdleTarget - IdleGain) * Ease;
			if (IdleGain > 0.0005 || IdleTarget > 0.0005)
			{
				Sum += At(B.IdleRec, IdlePos) * IdleGain;
				IdlePos += IdleStep;
				if (IdlePos >= B.IdleRec.size() - 1) IdlePos -= B.IdleRec.size() - 1;
			}
		}
		for (FShot& S : Shots)
		{
			if (!S.S) continue;
			Sum += At(*S.S, S.Pos) * S.Gain;
			S.Pos += S.Step;
			if (S.Pos >= S.S->size() - 1) S.S = nullptr;
		}
		Out[I] += (float)Sum;
	}
}
} // namespace TorqueEngine
