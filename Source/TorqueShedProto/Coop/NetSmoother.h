// Smooths another rider's bike from the snapshots their machine sends (about 30 a second, over an
// unreliable channel: some arrive late, bunched up, or not at all).
//
// The snapshots carry the sender's own clock. The receiver keeps an estimate of the gap between
// that clock and its own (the quickest a snapshot has ever come through, drifting up slowly so it
// follows a slower network) and draws the bike a fixed delay behind it: always between two
// snapshots it already has, so the bike moves smoothly however unevenly they arrive. Position runs
// along a cubic through both snapshots' positions and velocities (so corners stay round at 33 ms
// spacing); the rotation turns between them; numbers such as revs and steering are blended; whole
// values (gear, flags) step. If the snapshots stop for longer than the delay, the bike carries on
// along its last velocity for a short while, then waits.
//
// Plain C++ with no Unreal types, so SimTest can check it against a known path with jitter, loss
// and clock drift (SimTest/net_test.cpp).
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>

namespace TorqueNet
{
constexpr int MaxVals = 24;
constexpr int MaxInts = 5;

struct Snap
{
	double Time = 0.0;                // the sender's clock, seconds
	double Pos[3] = {0, 0, 0};        // any length unit
	double Vel[3] = {0, 0, 0};        // the same unit per second
	double Rot[4] = {0, 0, 0, 1};     // a unit quaternion, x y z w
	double Val[MaxVals] = {};         // blended
	int32_t Ints[MaxInts] = {};       // stepped
};

struct SmootherStats
{
	int64_t Received = 0;     // snapshots taken
	int64_t Dropped = 0;      // late (older than the newest held) or repeated, thrown away
	int64_t Frames = 0;       // Sample calls that drew something
	int64_t Extrapolated = 0; // frames drawn past the newest snapshot
	int64_t Held = 0;         // frames drawn before the oldest (just after joining), or stopped
	int64_t Snaps = 0;        // jumps (a respawn) drawn without blending
	double LastArrival = -1.0;
	double IntervalSum = 0.0, IntervalSq = 0.0;   // arrival spacing, s
	int64_t Intervals = 0;
	// Smoothness, in the units of Pos: the largest amount a drawn frame stepped off the line its
	// previous frame was moving along (|P - (P0 + V0 dt)|). On a smooth path at 60 fps this is a few
	// mm; a hitch or a correction shows as cm. Respawn jumps aren't counted. Zero it to start a new
	// measuring period.
	double StepErrMax = 0.0;
};

class Smoother
{
public:
	double Delay = 0.1;         // the least time the bike is drawn behind the sender's clock (+ the trip), s
	double MaxDelay = 0.25;     // however bad the network, never further behind than this
	double MaxExtrapolate = 0.25;
	double SnapDistance = 1500.0;   // further than this beyond where the velocity would carry it: a jump
	double Slew = 0.1;          // how fast the drawing clock may speed up or slow down (s per s)
	double JumpClock = 0.3;     // further than this from where it should be, the drawing clock jumps there
	double OffsetWindow = 4.0;  // the quickest trip is the quickest over this long (s)

	void Reset()
	{
		Buf.clear();
		bHaveOffset = false;
		bHaveLast = bLastInJump = false;
		OffWin.clear();
		LateEnv = SpacingAvg = 0.0;
		Stats = SmootherStats();
	}

	// A snapshot as it arrives; Now is the receiver's clock.
	void Push(const Snap& S, double Now)
	{
		if (!Buf.empty() && S.Time <= Buf.back().Time)
		{
			++Stats.Dropped;
			return;
		}
		++Stats.Received;
		if (Stats.LastArrival >= 0.0)
		{
			const double Gap = Now - Stats.LastArrival;
			Stats.IntervalSum += Gap;
			Stats.IntervalSq += Gap * Gap;
			++Stats.Intervals;
		}
		Stats.LastArrival = Now;
		const double Off = Now - S.Time;
		// The quickest trip over the last few seconds (a running minimum): it follows a network that
		// has got slower (a queue building up, a host whose frame rate dropped) within seconds, where
		// the quickest ever would leave the bike drawn ahead of what has arrived.
		while (!OffWin.empty() && OffWin.back().second >= Off) OffWin.pop_back();
		OffWin.emplace_back(Now, Off);
		while (OffWin.front().first < Now - OffsetWindow) OffWin.pop_front();
		Target = OffWin.front().second;
		bHaveOffset = true;
		// How late this one came compared with the quickest: the delay has to cover the late ones.
		// It rises part way to each late one (one hitch doesn't set it) and falls back over a few
		// seconds.
		const double Late = Off - Target;
		const double Spacing = Buf.empty() ? 0.0 : S.Time - Buf.back().Time;
		LateEnv = Late > LateEnv ? LateEnv + (Late - LateEnv) * 0.3 : LateEnv * 0.99;
		SpacingAvg = SpacingAvg <= 0.0 ? Spacing : SpacingAvg + (Spacing - SpacingAvg) * 0.05;
		if (Stats.Received == 1) Cur = Target + WantDelay();
		Buf.push_back(S);
		while (Buf.size() > 64) Buf.pop_front();
	}

	bool Empty() const { return Buf.empty(); }

	// The bike as it should be drawn now. Dt is the receiver's frame time. False with nothing yet.
	bool Sample(double Now, double Dt, Snap& Out)
	{
		if (!Draw(Now, Dt, Out)) return false;
		const double H = Out.Time - Last.Time;
		const bool bAcrossJump = bInJump || bLastInJump;
		bLastInJump = bInJump;
		if (bHaveLast && !bAcrossJump && H > 1e-5 && H < 0.5)
		{
			double E2 = 0.0;
			for (int I = 0; I < 3; ++I)
			{
				const double E = Out.Pos[I] - (Last.Pos[I] + Last.Vel[I] * H);
				E2 += E * E;
			}
			const double E = std::sqrt(E2);
			if (E > Stats.StepErrMax) Stats.StepErrMax = E;
		}
		Last = Out;
		bHaveLast = true;
		return true;
	}

private:
	Snap Last;
	bool bHaveLast = false, bLastInJump = false;

	bool Draw(double Now, double Dt, Snap& Out)
	{
		if (Buf.empty()) return false;
		// The drawing clock eases towards the estimate rather than jumping with it.
		const double Step = Slew * (Dt > 0.0 ? Dt : 0.0);
		const double D = Target + WantDelay() - Cur;
		if (D > JumpClock || D < -JumpClock) Cur += D;   // far out (joining, a long stall): go straight there
		else Cur += D > Step ? Step : (D < -Step ? -Step : D);
		const double T = Now - Cur;
		++Stats.Frames;

		if (T <= Buf.front().Time)
		{
			++Stats.Held;
			Out = Buf.front();
			return true;
		}
		if (T >= Buf.back().Time)
		{
			const Snap& L = Buf.back();
			Out = L;
			double E = T - L.Time;
			if (E > MaxExtrapolate)
			{
				E = MaxExtrapolate;
				++Stats.Held;
			}
			else
			{
				++Stats.Extrapolated;
			}
			for (int I = 0; I < 3; ++I) Out.Pos[I] = L.Pos[I] + L.Vel[I] * E;
			Out.Time = L.Time + E;   // the moment drawn
			// Keep turning as it was between the last two.
			if (Buf.size() >= 2)
			{
				const Snap& P = Buf[Buf.size() - 2];
				const double Span = L.Time - P.Time;
				if (Span > 1e-3) Blend(P.Rot, L.Rot, 1.0 + E / Span, Out.Rot);
			}
			return true;
		}
		// The two snapshots either side of T; older ones before them are no longer needed.
		size_t K = 0;
		while (K + 1 < Buf.size() && Buf[K + 1].Time <= T) ++K;
		while (K > 1)
		{
			Buf.pop_front();
			--K;
		}
		const Snap& A = Buf[K];
		const Snap& B = Buf[K + 1];
		const double Span = B.Time - A.Time;
		const double U = Span > 1e-6 ? (T - A.Time) / Span : 1.0;

		// A jump (respawned, put back on the road): no blending across it.
		double Miss = 0.0;
		for (int I = 0; I < 3; ++I)
		{
			const double M = B.Pos[I] - (A.Pos[I] + 0.5 * (A.Vel[I] + B.Vel[I]) * Span);
			Miss += M * M;
		}
		if (Miss > SnapDistance * SnapDistance)
		{
			if (!bInJump) ++Stats.Snaps;
			bInJump = true;
			Out = U < 1.0 ? A : B;
			return true;
		}
		bInJump = false;

		Out = A;
		Out.Time = T;
		// Cubic Hermite: positions and velocities at both ends.
		const double U2 = U * U, U3 = U2 * U;
		const double H00 = 2 * U3 - 3 * U2 + 1, H10 = U3 - 2 * U2 + U, H01 = -2 * U3 + 3 * U2, H11 = U3 - U2;
		for (int I = 0; I < 3; ++I)
		{
			Out.Pos[I] = H00 * A.Pos[I] + H10 * Span * A.Vel[I] + H01 * B.Pos[I] + H11 * Span * B.Vel[I];
			Out.Vel[I] = A.Vel[I] + (B.Vel[I] - A.Vel[I]) * U;
		}
		Blend(A.Rot, B.Rot, U, Out.Rot);
		for (int I = 0; I < MaxVals; ++I) Out.Val[I] = A.Val[I] + (B.Val[I] - A.Val[I]) * U;
		for (int I = 0; I < MaxInts; ++I) Out.Ints[I] = U < 0.5 ? A.Ints[I] : B.Ints[I];
		return true;
	}

public:
	// The delay the drawing clock is heading for (s, beyond the quickest trip seen).
	double WantDelay() const
	{
		const double W = 1.2 * SpacingAvg + LateEnv + 0.01;
		return W < Delay ? Delay : (W > MaxDelay ? MaxDelay : W);
	}
	// How far behind the newest snapshot we're drawing (s): negative is waiting on the next one.
	double Behind(double Now) const { return Buf.empty() ? 0.0 : Buf.back().Time - (Now - Cur); }

	SmootherStats Stats;

private:
	std::deque<Snap> Buf;
	std::deque<std::pair<double, double>> OffWin;   // (arrived, offset), offsets rising
	bool bHaveOffset = false, bInJump = false;
	double Target = 0.0, Cur = 0.0, LateEnv = 0.0, SpacingAvg = 0.0;

	// Rotation: the shorter way round, blended and renormalised (close to a slerp over 33 ms). U
	// past 1 carries the turn on.
	static void Blend(const double* A, const double* B, double U, double* Out)
	{
		double Dot = 0.0;
		for (int I = 0; I < 4; ++I) Dot += A[I] * B[I];
		const double Sign = Dot < 0.0 ? -1.0 : 1.0;
		double Len = 0.0;
		for (int I = 0; I < 4; ++I)
		{
			Out[I] = A[I] + (B[I] * Sign - A[I]) * U;
			Len += Out[I] * Out[I];
		}
		Len = std::sqrt(Len);
		if (Len > 1e-9)
			for (int I = 0; I < 4; ++I) Out[I] /= Len;
	}
};
} // namespace TorqueNet
