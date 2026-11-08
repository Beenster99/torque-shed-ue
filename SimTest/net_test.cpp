// Checks the co-op smoothing (Source/TorqueShedProto/Coop/NetSmoother.h) against a bike on a known
// path: snapshots sent 30 times a second on a clock with an offset and drift, arriving with delay
// and jitter, some lost, drawn at an uneven 60 frames a second. Reports how far the drawn bike is
// from where it really was, and how smooth it is (its acceleration against the real path's).
#include "NetSmoother.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using TorqueNet::Smoother;
using TorqueNet::Snap;

namespace
{
constexpr double Pi = 3.14159265358979323846;

// The real bike (cm): round a 50 m circle, speeding up and slowing between 15 and 25 m/s, with a
// jump (a respawn) at JumpAt if set.
struct Path
{
	double JumpAt = -1.0;
	double Speed(double T) const { return 2000.0 + 500.0 * std::sin(T * 0.7); }
	// Distance along the circle: the integral of Speed.
	double Dist(double T) const { return 2000.0 * T - 500.0 / 0.7 * (std::cos(T * 0.7) - 1.0); }
	void At(double T, double P[3], double V[3], double Q[4]) const
	{
		const double R = 5000.0;
		const double A = Dist(T) / R;
		const double S = Speed(T);
		P[0] = R * std::cos(A);
		P[1] = R * std::sin(A);
		P[2] = 100.0 * std::sin(T * 0.3);   // a gentle hill
		V[0] = -S * std::sin(A);
		V[1] = S * std::cos(A);
		V[2] = 30.0 * std::cos(T * 0.3);
		if (JumpAt >= 0.0 && T >= JumpAt) P[0] += 20000.0;   // put back on the road 200 m away
		// Heading about z.
		const double Yaw = A + Pi / 2.0;
		Q[0] = 0.0;
		Q[1] = 0.0;
		Q[2] = std::sin(Yaw / 2.0);
		Q[3] = std::cos(Yaw / 2.0);
	}
};

struct Packet
{
	double Arrive;
	Snap S;
};

struct Result
{
	double RmsErr = 0.0, MaxErr = 0.0, MaxAcc = 0.0, TruthAcc = 0.0, MaxRotErr = 0.0;
	int64_t Frames = 0, Kinks = 0;
	double DelaySum = 0.0, DelayMax = 0.0;   // the smoothing delay, sampled each frame after the first 2 s
	int64_t DelayCount = 0;   // frames drawn, and frames whose acceleration was over twice the path's
	TorqueNet::SmootherStats Stats;
};

struct Net
{
	double Latency = 0.040, Jitter = 0.030, Loss = 0.03;
	double BurstEvery = 0.0, BurstLength = 0.0;   // a run of lost snapshots every so often
	double ClockOffset = 1234.5, Drift = 100e-6;
	double SendHz = 30.0, FrameHz = 60.0, FrameJitter = 0.003;
	double Seconds = 120.0;
	// A sender whose game runs slowly and unevenly (two copies sharing one GPU): snapshots only at
	// its frames, FPS on average, each frame 0.6 to 1.4 times as long.
	double SenderFps = 0.0;
	// The trip gets slower part way (a queue building up): Latency + LatencyStep from LatencyStepAt s.
	double LatencyStepAt = -1.0, LatencyStep = 0.0;
};

Result Run(const Net& N, const Path& Truth, uint32_t Seed)
{
	std::mt19937 Rng(Seed);
	std::uniform_real_distribution<double> U01(0.0, 1.0);
	std::vector<Packet> Packets;
	int32_t Seq = 0;
	double LastSent = -1.0;
	for (double T = 0.0; T < N.Seconds;
		T += N.SenderFps > 0.0 ? (0.6 + 0.8 * U01(Rng)) / N.SenderFps : 1.0 / N.SendHz)
	{
		if (N.SenderFps > 0.0)
		{
			if (T - LastSent < 1.0 / N.SendHz - 1e-9) continue;   // not yet time for the next snapshot
			LastSent = T;
		}
		const bool InBurst = N.BurstEvery > 0.0 && std::fmod(T, N.BurstEvery) > N.BurstEvery - N.BurstLength;
		if (InBurst || U01(Rng) < N.Loss) continue;
		Packet P;
		P.S.Time = N.ClockOffset + T * (1.0 + N.Drift);
		Truth.At(T, P.S.Pos, P.S.Vel, P.S.Rot);
		P.S.Val[0] = Truth.Speed(T);
		P.S.Ints[0] = ++Seq;
		P.Arrive = T + N.Latency + U01(Rng) * N.Jitter + (N.LatencyStepAt >= 0.0 && T >= N.LatencyStepAt ? N.LatencyStep : 0.0);
		Packets.push_back(P);
	}
	std::stable_sort(Packets.begin(), Packets.end(), [](const Packet& A, const Packet& B) { return A.Arrive < B.Arrive; });

	Smoother Sm;
	Result R;
	size_t Next = 0;
	double Sum2 = 0.0;
	int64_t Count = 0;
	double PrevT[2] = {0, 0}, PrevP[2][3] = {};
	int Have = 0;
	bool bPrevJump = false;
	double Now = 5.0;   // the receiver's clock starts elsewhere
	const double Start = Now;
	double Dt = 1.0 / N.FrameHz;
	while (Now - Start < N.Seconds - 1.0)
	{
		while (Next < Packets.size() && Packets[Next].Arrive <= Now - Start)
		{
			Sm.Push(Packets[Next].S, Now);
			++Next;
		}
		Snap Out;
		if (Sm.Sample(Now, Dt, Out) && Now - Start > 2.0)
		{
			R.DelaySum += Sm.WantDelay();
			R.DelayMax = std::max(R.DelayMax, Sm.WantDelay());
			++R.DelayCount;
			// Where the bike really was at the time being drawn.
			const double TrueT = (Out.Time - N.ClockOffset) / (1.0 + N.Drift);
			double P[3], V[3], Q[4];
			Truth.At(TrueT, P, V, Q);
			const bool bNearJump = Truth.JumpAt >= 0.0 && std::fabs(TrueT - Truth.JumpAt) < 0.1;
			double E2 = 0.0;
			for (int I = 0; I < 3; ++I) E2 += (Out.Pos[I] - P[I]) * (Out.Pos[I] - P[I]);
			if (!bNearJump)
			{
				Sum2 += E2;
				++Count;
				R.MaxErr = std::max(R.MaxErr, std::sqrt(E2));
				double Dot = 0.0;
				for (int I = 0; I < 4; ++I) Dot += Out.Rot[I] * Q[I];
				R.MaxRotErr = std::max(R.MaxRotErr, 2.0 * std::acos(std::min(1.0, std::fabs(Dot))) * 180.0 / Pi);
			}
			// Smoothness: acceleration over the drawn times (second difference).
			if (Have == 2 && !bNearJump && !bPrevJump)
			{
				const double H1 = PrevT[1] - PrevT[0], H2 = Out.Time - PrevT[1];
				if (H1 > 1e-4 && H2 > 1e-4)
				{
					double A2 = 0.0;
					for (int I = 0; I < 3; ++I)
					{
						const double V1 = (PrevP[1][I] - PrevP[0][I]) / H1, V2 = (Out.Pos[I] - PrevP[1][I]) / H2;
						const double A = (V2 - V1) / (0.5 * (H1 + H2));
						A2 += A * A;
					}
					R.MaxAcc = std::max(R.MaxAcc, std::sqrt(A2));
					if (std::sqrt(A2) > 2.0 * 1250.0) ++R.Kinks;
					++R.Frames;
				}
			}
			bPrevJump = bNearJump;
			PrevT[0] = PrevT[1];
			PrevT[1] = Out.Time;
			for (int I = 0; I < 3; ++I)
			{
				PrevP[0][I] = PrevP[1][I];
				PrevP[1][I] = Out.Pos[I];
			}
			Have = std::min(Have + 1, 2);
		}
		Dt = 1.0 / N.FrameHz + (U01(Rng) * 2.0 - 1.0) * N.FrameJitter;
		Now += Dt;
	}
	// The real path's largest acceleration: v^2/r plus the speeding up.
	for (double T = 0.0; T < N.Seconds; T += 0.01)
	{
		const double S = Truth.Speed(T);
		const double Along = 500.0 * 0.7 * std::cos(T * 0.7);
		R.TruthAcc = std::max(R.TruthAcc, std::sqrt(std::pow(S * S / 5000.0, 2) + Along * Along));
	}
	R.RmsErr = Count ? std::sqrt(Sum2 / Count) : 0.0;
	R.Stats = Sm.Stats;
	return R;
}

int Failures = 0;
void Check(bool Ok, const char* What)
{
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Failures;
}

void Report(const char* Name, const Result& R)
{
	const auto& S = R.Stats;
	const double Mean = S.Intervals ? S.IntervalSum / S.Intervals : 0.0;
	const double Sd = S.Intervals ? std::sqrt(std::max(0.0, S.IntervalSq / S.Intervals - Mean * Mean)) : 0.0;
	std::printf("%s: delay mean %.0f ms, max %.0f ms, step error max %.2f cm\n  ", Name, R.DelayCount ? 1000.0 * R.DelaySum / R.DelayCount : 0.0,
		1000.0 * R.DelayMax, R.Stats.StepErrMax);
	std::printf("%s: error rms %.1f cm, max %.1f cm, rot max %.2f deg; accel max %.0f cm/s2 (path %.0f); "
		"kinks %lld of %lld frames; rx %lld (interval %.1f +- %.1f ms), extrapolated %.1f%%, held %.1f%%, jumps %lld\n",
		Name, R.RmsErr, R.MaxErr, R.MaxRotErr, R.MaxAcc, R.TruthAcc, (long long)R.Kinks, (long long)R.Frames, (long long)S.Received, Mean * 1000.0, Sd * 1000.0,
		S.Frames ? 100.0 * S.Extrapolated / S.Frames : 0.0, S.Frames ? 100.0 * S.Held / S.Frames : 0.0, (long long)S.Snaps);
}
} // namespace

int main()
{
	{
		Net N;
		Path P;
		const Result R = Run(N, P, 1);
		Report("LAN-ish (40 ms + 0-30 ms jitter, 3% loss)", R);
		Check(R.RmsErr < 3.0, "drawn within 3 cm rms of the real path");
		Check(R.MaxErr < 30.0, "never more than 30 cm off");
		Check(R.Kinks * 1000 <= R.Frames, "smooth: no more than 1 frame in 1000 accelerates over twice the path's");
		Check(R.MaxRotErr < 2.0, "heading within 2 degrees");
		Check(R.DelaySum / R.DelayCount < 0.15, "smoothing delay averages under 150 ms");
		Check(R.Stats.StepErrMax < 2.0, "step error (the in-game smoothness figure) under 2 cm");
	}
	{
		Net N;
		N.Latency = 0.120;
		N.Jitter = 0.080;
		N.Loss = 0.05;
		N.Drift = 300e-6;
		N.Seconds = 300.0;
		Path P;
		const Result R = Run(N, P, 2);
		Report("Internet (120 ms + 0-80 ms jitter, 5% loss, 300 ppm drift)", R);
		Check(R.RmsErr < 10.0, "within 10 cm rms");
		Check(R.Kinks * 200 <= R.Frames, "smooth: no more than 1 frame in 200 accelerates over twice the path's");
		Check(R.MaxErr < 10.0, "never more than 10 cm off");
	}
	{
		Net N;
		N.BurstEvery = 7.0;
		N.BurstLength = 0.3;   // nine snapshots in a row lost
		Path P;
		const Result R = Run(N, P, 3);
		Report("Bursts (0.3 s lost every 7 s)", R);
		Check(R.Stats.Extrapolated > 0, "carries on along its velocity through a gap");
		Check(R.MaxErr < 200.0, "never more than 2 m off through a gap");
	}
	{
		Net N;
		Path P;
		P.JumpAt = 30.0;
		const Result R = Run(N, P, 4);
		Report("Respawn jump at 30 s", R);
		Check(R.Stats.Snaps == 1, "the jump is drawn as one jump, not a slide");
		Check(R.MaxErr < 30.0, "within 30 cm away from the jump");
	}
	{
		// The PC test that showed the trouble: the host's game at 10-15 fps, so its snapshots come
		// bunched, and the trip slowing from 20 ms to 350 ms part way.
		Net N;
		N.Latency = 0.020;
		N.Jitter = 0.020;
		N.Loss = 0.0;
		N.SenderFps = 12.0;
		N.LatencyStepAt = 40.0;
		N.LatencyStep = 0.33;
		Path P;
		const Result R = Run(N, P, 5);
		Report("Slow host (12 fps, bunched) and a trip slowing by 330 ms at 40 s", R);
		Check(R.MaxErr < 60.0, "never more than 60 cm off");
		Check(R.Stats.Frames > 0 && R.Stats.Held * 100 < R.Stats.Frames, "waiting on snapshots under 1% of frames");
		Check(R.Stats.Extrapolated * 20 < R.Stats.Frames, "bridging gaps under 5% of frames");
		Check(R.DelayMax <= 0.25 + 1e-9, "smoothing delay never over 250 ms");
	}
	{
		// A good link: the delay should settle near the 100 ms floor.
		Net N;
		N.Latency = 0.002;
		N.Jitter = 0.004;
		N.Loss = 0.0;
		Path P;
		const Result R = Run(N, P, 6);
		Report("Loopback at full frame rate", R);
		Check(R.MaxErr < 5.0, "within 5 cm");
		Check(R.DelayMax < 0.125, "smoothing delay stays near 100 ms");
	}
	std::printf(Failures ? "net_test: %d FAILED\n" : "net_test: all passed\n", Failures);
	return Failures ? 1 : 0;
}
