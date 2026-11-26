// World/WorldRideBirds: a rider down a hedged lane with trees, for half an hour at a time.
// Checks: birds come out of the hedges now and then (not constantly), none in the night, the same on
// two machines (co-op), they start ahead of and beside the rider, nothing goes wild.
#include "WorldRideBirds.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace TorqueWorld;

static int Fails = 0;
#define CHECK(C, ...) do { if (!(C)) { ++Fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// A lane running east along z = 0 with a hedge each side (2.5 m off), a tree every ~45 m, small woods.
static void Lane(RideBirds& B)
{
	for (int Sq = -2; Sq < 60; ++Sq)
	{
		std::vector<BirdPerch> P;
		for (int I = 0; I < 256; I += 3)
		{
			const float X = float(Sq * 256 + I);
			for (float Side : {-2.6f, 2.6f})
			{
				BirdPerch H; H.X = X; H.Z = Side; H.Y = 0.0f; H.Top = 1.8f; H.Type = 0;
				P.push_back(H);
			}
			if ((I / 3) % 15 == 0)
			{
				BirdPerch T; T.X = X + 1.0f; T.Z = ((I / 3) % 2 ? 9.0f : -14.0f) + float(I % 7); T.Y = 0.0f; T.Top = 12.0f + float(I % 5); T.Type = 1;
				P.push_back(T);
			}
		}
		B.AddPerches(Sq, std::move(P));
	}
}

struct Run { int Flushes = 0, Birds = 0, Perched = 0, Soar = 0, Flocks = 0, Most = 0; bool Bad = false; double MinAhead = 1e9; };

static Run Ride(double Hour, double Light, int Month, double Speed, double Seconds, double Rain = 0.0, int Riders = 1)
{
	RideBirds B;
	Lane(B);
	BirdPlace Pl;
	Pl.Hour = Hour; Pl.Light = Light; Pl.Month = Month; Pl.Farm = 0.5; Pl.Rain = Rain;
	Run R;
	const auto Ground = [](double, double) { return 0.0; };
	const double Dt = 1.0 / 30.0;
	for (double T = 0.0; T < Seconds; T += Dt)
	{
		std::vector<BirdRider> Rs;
		for (int K = 0; K < Riders; ++K)
		{
			BirdRider One;
			One.X = -200.0 + Speed * T - K * 40.0; One.Z = 0.0; One.Vx = Speed;
			Rs.push_back(One);
		}
		B.Tick(1000.0 + T, Rs, Pl, Ground);
		R.Most = std::max(R.Most, B.Alive());
		for (const BirdDraw& D : B.Draws())
			if (!std::isfinite(D.X) || !std::isfinite(D.Y) || !std::isfinite(D.Z) || !std::isfinite(D.Yaw) || std::fabs(D.Y) > 600.0) R.Bad = true;
	}
	R.Flushes = B.Seen().Flushes; R.Birds = B.Seen().FlushBirds; R.Perched = B.Seen().Perched; R.Soar = B.Seen().Soarers; R.Flocks = B.Seen().Flocks; R.MinAhead = B.Seen().NearestAhead;
	return R;
}

int main()
{
	// Half an hour at 50 mph on a June morning.
	const Run Day = Ride(10.5, 1.0, 6, 22.0, 600.0);
	std::printf("day 10 min at 22 m/s: %d flushes (%d birds), %d perched, %d soaring, %d flocks, most alive %d, nearest start %.1f m ahead\n",
		Day.Flushes, Day.Birds, Day.Perched, Day.Soar, Day.Flocks, Day.Most, Day.MinAhead);
	CHECK(!Day.Bad, "a bird went wild (NaN or far off)");
	CHECK(Day.Flushes >= 5 && Day.Flushes <= 20, "flushes in 10 minutes: %d (want 5..20: now and then, not constantly)", Day.Flushes);
	CHECK(Day.Most <= RideBirds::MostBirds, "too many birds at once: %d", Day.Most);
	CHECK(Day.Soar >= 1, "no soaring birds of prey on a June morning");
	CHECK(Day.MinAhead > 5.0, "a bird burst out beside or behind the rider (%.1f m ahead)", Day.MinAhead);
	const Run Night = Ride(1.0, 0.0, 6, 22.0, 300.0);
	std::printf("night: %d flushes, %d soaring, %d flocks\n", Night.Flushes, Night.Soar, Night.Flocks);
	CHECK(Night.Flushes == 0 && Night.Soar == 0 && Night.Flocks == 0, "birds at night");
	const Run Wet = Ride(10.5, 1.0, 6, 22.0, 600.0, 0.9);
	std::printf("heavy rain: %d flushes, %d soaring\n", Wet.Flushes, Wet.Soar);
	CHECK(Wet.Flushes < Day.Flushes, "as many birds in heavy rain as in the dry");
	CHECK(Wet.Soar == 0, "soaring in heavy rain");
	const Run Winter = Ride(8.0, 0.7, 12, 22.0, 900.0);
	std::printf("december morning: %d flushes, %d flocks (%d perched)\n", Winter.Flushes, Winter.Flocks, Winter.Perched);
	const Run Slow = Ride(10.5, 1.0, 6, 2.0, 300.0);
	CHECK(Slow.Flushes == 0, "flushes at walking pace: %d", Slow.Flushes);
	// Co-op: two machines given the same riders and clock see the same birds.
	{
		RideBirds A, C;
		Lane(A); Lane(C);
		BirdPlace Pl; Pl.Hour = 11.0; Pl.Month = 5; Pl.Farm = 0.5;
		const auto Ground = [](double, double) { return 0.0; };
		bool Same = true;
		for (double T = 0.0; T < 200.0; T += 1.0 / 30.0)
		{
			std::vector<BirdRider> Rs(2);
			Rs[0].X = 22.0 * T; Rs[0].Vx = 22.0;
			Rs[1].X = 22.0 * T - 35.0; Rs[1].Vx = 22.0;
			std::vector<BirdRider> Rs2 = {Rs[1], Rs[0]};   // (the other machine lists them the other way round)
			A.Tick(5000.0 + T, Rs, Pl, Ground);
			C.Tick(5000.0 + T, Rs2, Pl, Ground);
			if (A.Draws().size() != C.Draws().size()) Same = false;
		}
		std::printf("co-op: %d and %d flushes\n", A.Seen().Flushes, C.Seen().Flushes);
		CHECK(Same && A.Seen().Flushes == C.Seen().Flushes, "the two machines saw different birds");
	}
	std::printf(Fails ? "bird_test: %d FAILED\n" : "bird_test: all passed\n", Fails);
	return Fails ? 1 : 0;
}
