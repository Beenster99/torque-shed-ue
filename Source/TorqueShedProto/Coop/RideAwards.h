// [ride awards] The end-of-ride awards (beenster, Nov 19 2026): an optional game on a planned ride.
// Each rider's game keeps a dozen numbers as they ride (the top speed, the longest wheelie, the offs,
// the air time...); at the end, before anyone sees them, each rider privately ranks the three they
// care about most; then the numbers are shown on a dashboard and the top three riders stand on a
// podium. No replays, and nothing is kept after.
//
// Plain C++ (no Unreal), so SimTest/awards_test.cpp checks the counting and the scoring. The game's
// side (the vote, the dashboard and the podium on screen, the numbers sent to the host) is
// Coop/TorqueAwards.h.
//
// Scoring, as agreed: each stat's weight is the votes it got, 3 for a rider's 1st pick, 2 for a 2nd,
// 1 for a 3rd (every stat weighs 1 if nobody voted). In each stat the riders are placed by their
// number (bigger is better in all of them; nothing done, no place) and the top three placings earn
// 3, 2 and 1. A rider's score is the sum of placing points times weight. Ties go to whoever leads
// more of the voted stats, then to the most placing points unweighted.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace TorqueAwards
{
// The stats the riders vote on, in the dashboard's order.
enum Stat : int
{
	TopSpeed = 0,   // m/s
	Wheelie,        // the longest, m
	Stoppie,        // the longest endo / stoppie, m
	Offs,           // how many times off
	FastestOff,     // the speed going into the fastest off, m/s
	OffRoad,        // m ridden off the tarmac (grass, gravel)
	Accurate,       // % of the ride on the planned road
	Lean,           // the most lean, degrees
	Air,            // seconds in the air (both wheels off, a quarter second or more)
	Skid,           // the longest skid (a locked wheel), m
	Burnout,        // the longest burnout, s
	Safest,         // 0..100: within the limits, no offs
	Count
};
// Kept with them but not voted on: for the dashboard's header.
constexpr int Distance = Count;   // m
constexpr int RideTime = Count + 1;   // s on the bike, not off it
constexpr int Total = Count + 2;

enum class Unit : int { Speed, Metres, Number, Percent, Degrees, Seconds, Score };
struct StatInfo
{
	const char* Name;
	const char* Desc;
	Unit Kind;
};
inline const StatInfo& Info(int S)
{
	static const StatInfo Table[Count] = {
		{"Highest speed", "The fastest anyone went", Unit::Speed},
		{"Longest wheelie", "Furthest on the back wheel alone", Unit::Metres},
		{"Longest endo", "Furthest on the front wheel alone (a stoppie)", Unit::Metres},
		{"Most offs", "Who fell off the most", Unit::Number},
		{"Highest-speed off", "The fastest anyone came off", Unit::Speed},
		{"Most off-road", "Furthest on grass, gravel and dirt", Unit::Metres},
		{"Most accurate", "Most of the ride on the planned road", Unit::Percent},
		{"Highest lean", "The most lean angle, both wheels down", Unit::Degrees},
		{"Most air time", "Seconds with both wheels off the ground", Unit::Seconds},
		{"Longest skid", "Furthest on a locked wheel", Unit::Metres},
		{"Best burnout", "The longest burnout", Unit::Seconds},
		{"Safest rider", "Kept to the limits and stayed on", Unit::Score},
	};
	return Table[S >= 0 && S < Count ? S : 0];
}

// One frame of riding, as the game sees it.
struct Sample
{
	double Dt = 0.0;
	double Speed = 0.0;          // m/s, along the bike
	double LeanRad = 0.0;
	bool FrontDown = true, RearDown = true;
	bool Crashed = false;
	bool OnFoot = false;
	bool RearOffRoad = false;    // the back tyre on grass or gravel
	bool HaveRoute = false;      // the sat nav has the ride's road
	bool OffRoute = false;       // more than 40 m off it
	double LimitMs = 0.0;        // the road's speed limit, m/s (0 not known)
	double FrontSlip = 0.0, RearSlip = 0.0;   // slip ratio: about -1 locked, + spinning
	double Burnout = 0.0;        // 0..1, the back tyre spinning on the spot
};

class Tracker
{
public:
	std::array<double, Total> V{};

	void Reset() { *this = Tracker(); }

	void Step(const Sample& S)
	{
		const double Dt = std::max(S.Dt, 0.0);
		if (S.OnFoot)
		{
			EndSpells();
			bWasCrashed = false;
			return;
		}
		if (S.Crashed)
		{
			if (!bWasCrashed)
			{
				V[Offs] += 1.0;
				V[FastestOff] = std::max(V[FastestOff], LastSpeed);
			}
			bWasCrashed = true;
			EndSpells();
			UpdateSafest();
			return;
		}
		bWasCrashed = false;
		const double Sp = std::fabs(S.Speed);
		const double Dist = Sp * Dt;
		LastSpeed = Sp;
		V[Distance] += Dist;
		V[RideTime] += Dt;
		V[TopSpeed] = std::max(V[TopSpeed], Sp);

		// A wheelie (the back wheel alone) and an endo (the front alone), counted after a moment so a
		// bump's hop isn't one.
		Spell(!S.FrontDown && S.RearDown && Sp > 2.0, Dt, Dist, WheelieT, WheelieM, 0.4, V[Wheelie]);
		Spell(S.FrontDown && !S.RearDown && Sp > 1.5, Dt, Dist, StoppieT, StoppieM, 0.3, V[Stoppie]);
		// In the air: the whole spell counts once it's a quarter second.
		if (!S.FrontDown && !S.RearDown && Sp > 2.0) AirT += Dt;
		else
		{
			if (AirT >= 0.25) V[Air] += AirT;
			AirT = 0.0;
		}
		if (S.FrontDown && S.RearDown && Sp > 5.0) V[Lean] = std::max(V[Lean], std::fabs(S.LeanRad) * 57.29577951308232);
		if (S.RearDown && S.RearOffRoad) V[OffRoad] += Dist;
		// A skid: a wheel locked on the ground.
		const bool bLocked = (S.FrontDown && S.FrontSlip < -0.3) || (S.RearDown && S.RearSlip < -0.3);
		Spell(bLocked && Sp > 2.0, Dt, Dist, SkidT, SkidM, 0.15, V[Skid]);
		// A burnout: the back wheel spinning with the bike near enough still.
		if (S.Burnout > 0.4 && Sp < 4.0)
		{
			BurnT += Dt;
			if (BurnT >= 0.5) V[Burnout] = std::max(V[Burnout], BurnT);
		}
		else BurnT = 0.0;
		// On the planned road (and on the tarmac), of the ride ridden with one.
		if (S.HaveRoute && Sp > 2.0)
		{
			RouteM += Dist;
			if (!S.OffRoute && !S.RearOffRoad) OnRouteM += Dist;
			V[Accurate] = RouteM > 200.0 ? 100.0 * OnRouteM / RouteM : 0.0;
		}
		// Within the limits: no more than a tenth over (and 1 m/s for the speedo).
		if (S.LimitMs > 1.0 && Sp > 2.0)
		{
			LimitM += Dist;
			if (Sp <= S.LimitMs * 1.1 + 1.0) LegalM += Dist;
		}
		UpdateSafest();
	}

private:
	bool bWasCrashed = false;
	double LastSpeed = 0.0;
	double WheelieT = 0.0, WheelieM = 0.0, StoppieT = 0.0, StoppieM = 0.0, SkidT = 0.0, SkidM = 0.0;
	double AirT = 0.0, BurnT = 0.0;
	double RouteM = 0.0, OnRouteM = 0.0, LimitM = 0.0, LegalM = 0.0;

	static void Spell(bool bOn, double Dt, double Dist, double& T, double& M, double MinT, double& Best)
	{
		if (!bOn)
		{
			T = M = 0.0;
			return;
		}
		T += Dt;
		M += Dist;
		if (T >= MinT) Best = std::max(Best, M);
	}
	void EndSpells()
	{
		WheelieT = WheelieM = StoppieT = StoppieM = SkidT = SkidM = 0.0;
		if (AirT >= 0.25) V[Air] += AirT;
		AirT = BurnT = 0.0;
	}
	void UpdateSafest()
	{
		// Nothing much ridden: no score. Else the share within the limits (all of it where no limit was
		// known), less 15 for each off.
		if (V[Distance] < 200.0)
		{
			V[Safest] = 0.0;
			return;
		}
		const double Within = LimitM > 100.0 ? 100.0 * LegalM / LimitM : 100.0;
		V[Safest] = std::max(1.0, Within - 15.0 * V[Offs]);
	}
};

// The scoring (see the top). Stats: each rider's numbers; Picks: each rider's three picks in order
// (a stat each, -1 none; repeats count once, at the better pick).
struct Result
{
	std::array<int, Count> Weight{};     // each stat's votes, 3/2/1 a pick
	std::array<int, Count> Pickers{};    // how many riders picked it at all
	std::vector<int> Score, Led, Raw;    // each rider's
	std::vector<std::array<int, Count>> Place;   // each rider's place in each stat (1 first, 0 none)
	std::vector<int> Order;              // the riders, the winner first
	bool bNoVotes = false;
};

inline Result Score(const std::vector<std::array<double, Count>>& Stats, const std::vector<std::array<int, 3>>& Picks)
{
	Result R;
	const int N = (int)Stats.size();
	for (const std::array<int, 3>& P : Picks)
	{
		std::array<bool, Count> Seen{};
		for (int K = 0; K < 3; ++K)
		{
			const int S = P[K];
			if (S < 0 || S >= Count || Seen[S]) continue;
			Seen[S] = true;
			R.Weight[S] += 3 - K;
			R.Pickers[S] += 1;
		}
	}
	int Votes = 0;
	for (int W : R.Weight) Votes += W;
	if (Votes == 0)
	{
		R.bNoVotes = true;
		R.Weight.fill(1);
	}
	R.Score.assign(N, 0);
	R.Led.assign(N, 0);
	R.Raw.assign(N, 0);
	R.Place.assign(N, std::array<int, Count>{});
	for (int S = 0; S < Count; ++S)
	{
		for (int I = 0; I < N; ++I)
		{
			const double Mine = Stats[I][S];
			if (!(Mine > 1e-6)) continue;
			int Place = 1;
			for (int J = 0; J < N; ++J)
				if (Stats[J][S] > Mine + 1e-9) ++Place;
			R.Place[I][S] = Place;
			const int Pts = std::max(0, 4 - Place);
			R.Raw[I] += Pts;
			R.Score[I] += Pts * R.Weight[S];
			if (Place == 1 && R.Weight[S] > 0 && !R.bNoVotes) ++R.Led[I];
			else if (Place == 1 && R.bNoVotes) ++R.Led[I];
		}
	}
	R.Order.resize(N);
	for (int I = 0; I < N; ++I) R.Order[I] = I;
	std::stable_sort(R.Order.begin(), R.Order.end(), [&R](int A, int B) {
		if (R.Score[A] != R.Score[B]) return R.Score[A] > R.Score[B];
		if (R.Led[A] != R.Led[B]) return R.Led[A] > R.Led[B];
		return R.Raw[A] > R.Raw[B];
	});
	return R;
}
} // namespace TorqueAwards
