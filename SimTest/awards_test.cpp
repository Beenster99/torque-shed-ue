// Tests Coop/RideAwards.h: the stats counted from riding samples, and the vote's scoring and podium.
#include "RideAwards.h"

#include <cstdio>

static int Failures = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Failures;
}

using namespace TorqueAwards;

static Sample Ride(double Speed)
{
	Sample S;
	S.Dt = 1.0 / 60.0;
	S.Speed = Speed;
	return S;
}

int main()
{
	std::printf("Counting\n");
	{
		Tracker T;
		for (int I = 0; I < 600; ++I) T.Step(Ride(20.0));   // 10 s at 20 m/s
		Check(std::fabs(T.V[Distance] - 200.0) < 0.5, "distance 200 m");
		Check(std::fabs(T.V[TopSpeed] - 20.0) < 1e-9, "top speed 20 m/s");
		// A wheelie: 2 s at 10 m/s on the back wheel.
		for (int I = 0; I < 120; ++I)
		{
			Sample S = Ride(10.0);
			S.FrontDown = false;
			T.Step(S);
		}
		T.Step(Ride(10.0));
		Check(std::fabs(T.V[Wheelie] - 20.0) < 0.5, "wheelie 20 m");
		// A hop: 0.2 s on the back wheel is no wheelie.
		Tracker Hop;
		for (int I = 0; I < 12; ++I)
		{
			Sample S = Ride(10.0);
			S.FrontDown = false;
			Hop.Step(S);
		}
		Hop.Step(Ride(10.0));
		Check(Hop.V[Wheelie] == 0.0, "a short hop isn't a wheelie");
		// A jump: 0.5 s in the air, then a 0.1 s bump that doesn't count.
		for (int I = 0; I < 30; ++I)
		{
			Sample S = Ride(15.0);
			S.FrontDown = S.RearDown = false;
			T.Step(S);
		}
		T.Step(Ride(15.0));
		for (int I = 0; I < 6; ++I)
		{
			Sample S = Ride(15.0);
			S.FrontDown = S.RearDown = false;
			T.Step(S);
		}
		T.Step(Ride(15.0));
		Check(std::fabs(T.V[Air] - 0.5) < 0.02, "air time 0.5 s (the short bump left out)");
		Check(T.V[Wheelie] < 21.0, "landing on the back wheel didn't add to the wheelie");
		// An off at 25 m/s, then lying there.
		for (int I = 0; I < 30; ++I) T.Step(Ride(25.0));
		for (int I = 0; I < 60; ++I)
		{
			Sample S = Ride(3.0);
			S.Crashed = true;
			T.Step(S);
		}
		Check(T.V[Offs] == 1.0, "one off");
		Check(std::fabs(T.V[FastestOff] - 25.0) < 1e-9, "off at 25 m/s");
		Check(std::fabs(T.V[TopSpeed] - 25.0) < 1e-9, "top speed 25 m/s");
		// Lean, both wheels down.
		Sample L = Ride(20.0);
		L.LeanRad = 0.8;
		T.Step(L);
		Check(std::fabs(T.V[Lean] - 45.836) < 0.01, "lean 45.8 degrees");
		// A skid: 1 s on a locked rear at 10 m/s.
		for (int I = 0; I < 60; ++I)
		{
			Sample S = Ride(10.0);
			S.RearSlip = -1.0;
			T.Step(S);
		}
		T.Step(Ride(10.0));
		Check(std::fabs(T.V[Skid] - 10.0) < 0.3, "skid 10 m");
		// A burnout: 3 s.
		for (int I = 0; I < 180; ++I)
		{
			Sample S = Ride(0.2);
			S.Burnout = 1.0;
			T.Step(S);
		}
		Check(std::fabs(T.V[Burnout] - 3.0) < 0.05, "burnout 3 s");
		// Off-road: 100 m on grass.
		for (int I = 0; I < 600; ++I)
		{
			Sample S = Ride(10.0);
			S.RearOffRoad = true;
			T.Step(S);
		}
		Check(std::fabs(T.V[OffRoad] - 100.0) < 0.5, "off-road 100 m");
		// On foot: nothing counted.
		const double Before = T.V[Distance];
		Sample F = Ride(1.5);
		F.OnFoot = true;
		T.Step(F);
		Check(T.V[Distance] == Before, "on foot isn't riding");
	}
	std::printf("Accurate and safe\n");
	{
		Tracker T;
		for (int I = 0; I < 3000; ++I)   // 50 s at 20 m/s, a 30 m/s limit, on the road
		{
			Sample S = Ride(20.0);
			S.HaveRoute = true;
			S.LimitMs = 30.0;
			T.Step(S);
		}
		for (int I = 0; I < 1000; ++I)   // then 1000 m off the route, speeding
		{
			Sample S = Ride(60.0);
			S.HaveRoute = true;
			S.OffRoute = true;
			S.LimitMs = 30.0;
			T.Step(S);
		}
		Check(std::fabs(T.V[Accurate] - 50.0) < 0.5, "half the ride on the planned road: 50%");
		Check(std::fabs(T.V[Safest] - 50.0) < 0.5, "half within the limit: 50");
		Sample C = Ride(10.0);
		C.Crashed = true;
		T.Step(C);
		Check(std::fabs(T.V[Safest] - 35.0) < 0.5, "an off takes 15");
		Tracker Short;
		Short.Step(Ride(10.0));
		Check(Short.V[Safest] == 0.0, "no score for no ride");
	}
	std::printf("Scoring\n");
	{
		// Three riders. Ann the fastest, Bob the wheelies, Cat the offs.
		std::vector<std::array<double, Count>> Stats(3);
		for (auto& A : Stats) A.fill(0.0);
		Stats[0][TopSpeed] = 60.0; Stats[1][TopSpeed] = 50.0; Stats[2][TopSpeed] = 40.0;
		Stats[1][Wheelie] = 80.0; Stats[0][Wheelie] = 5.0;
		Stats[2][Offs] = 4.0; Stats[1][Offs] = 1.0;
		// Everyone's first pick is wheelies, second top speed.
		std::vector<std::array<int, 3>> Picks = {{{Wheelie, TopSpeed, Offs}}, {{Wheelie, TopSpeed, -1}}, {{Wheelie, Offs, TopSpeed}}};
		const Result R = Score(Stats, Picks);
		Check(R.Weight[Wheelie] == 9, "wheelie weighs 9");
		Check(R.Weight[TopSpeed] == 2 + 2 + 1, "top speed weighs 5");
		Check(R.Weight[Offs] == 1 + 2, "offs weigh 3");
		Check(R.Pickers[TopSpeed] == 3 && R.Pickers[Offs] == 2 && R.Pickers[Lean] == 0, "pickers counted");
		// Ann: speed 1st (3*5) + wheelie 2nd (2*9) = 33. Bob: speed 2nd (2*5) + wheelie 1st (3*9) + offs 2nd (2*3) = 43.
		// Cat: speed 3rd (1*5) + offs 1st (3*3) = 14.
		Check(R.Score[0] == 33 && R.Score[1] == 43 && R.Score[2] == 14, "scores 33, 43, 14");
		Check(R.Order[0] == 1 && R.Order[1] == 0 && R.Order[2] == 2, "Bob, Ann, Cat");
		Check(R.Place[2][Wheelie] == 0, "no wheelie, no place");
	}
	{
		// A tie on points goes to whoever leads more voted stats.
		std::vector<std::array<double, Count>> Stats(2);
		for (auto& A : Stats) A.fill(0.0);
		// Ann leads lean (weight 3); Bob is 2nd in lean and leads air... set so both score the same.
		Stats[0][Lean] = 50.0; Stats[1][Lean] = 40.0;          // Ann 3*3=9, Bob 2*3=6
		Stats[1][Air] = 2.0; Stats[0][Air] = 1.0;              // weight 1: Bob 3, Ann 2 -> Ann 11, Bob 9
		Stats[1][Skid] = 5.0;                                  // not voted for: no points, but a placing
		std::vector<std::array<int, 3>> Picks = {{{Lean, -1, Air}}, {{-1, -1, -1}}};
		Result R = Score(Stats, Picks);
		// Ann 9 + 2 = 11, Bob 6 + 3 = 9 (skid not voted).
		Check(R.Score[0] == 11 && R.Score[1] == 9, "unvoted stats score nothing");
		Stats[1][Air] = 2.0; Stats[0][Air] = 0.0;              // Ann 9, Bob 6 + 3 = 9: a tie
		R = Score(Stats, Picks);
		Check(R.Score[0] == 9 && R.Score[1] == 9, "tied on 9");
		Check(R.Led[0] == 1 && R.Led[1] == 1, "each leads one voted stat");
		Check(R.Order[0] == 1, "then the most placing points, unweighted: Ann 3, Bob 8: Bob");
	}
	{
		// Nobody voted: every stat weighs 1.
		std::vector<std::array<double, Count>> Stats(2);
		for (auto& A : Stats) A.fill(0.0);
		Stats[0][TopSpeed] = 10.0; Stats[1][TopSpeed] = 20.0; Stats[1][Lean] = 3.0;
		const Result R = Score(Stats, {});
		Check(R.bNoVotes && R.Weight[Lean] == 1, "no votes: all weigh 1");
		Check(R.Order[0] == 1 && R.Score[1] == 6 && R.Score[0] == 2, "Bob 6, Ann 2");
		// Repeated picks count once.
		const Result D = Score(Stats, {{{TopSpeed, TopSpeed, TopSpeed}}});
		Check(D.Weight[TopSpeed] == 3 && D.Pickers[TopSpeed] == 1, "a repeated pick counts once");
	}
	{
		// [race tracks] Best lap: smaller wins, and only those who rode one are placed.
		Tracker T;
		Sample S = Ride(10.0);
		S.BestLap = 95.0;
		T.Step(S);
		S.BestLap = 0.0;
		T.Step(S);
		Check(T.V[BestLap] == 95.0, "best lap kept when the timer has none");
		std::vector<std::array<double, Count>> Stats(3);
		for (auto& A : Stats) A.fill(0.0);
		Stats[0][BestLap] = 92.5; Stats[1][BestLap] = 88.0;   // Cat rode no lap
		const Result R = Score(Stats, {{{BestLap, -1, -1}}});
		Check(R.Place[1][BestLap] == 1 && R.Place[0][BestLap] == 2 && R.Place[2][BestLap] == 0, "88.0 beats 92.5; no lap, no place");
		Check(R.Order[0] == 1 && R.Score[1] == 9 && R.Score[0] == 6, "Bob 9, Ann 6");
	}
	std::printf(Failures ? "%d FAILED\n" : "all passed\n", Failures);
	return Failures ? 1 : 0;
}
