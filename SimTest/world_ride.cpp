// The stage 1 gate of Ride UK, run from a desk: the roads between two places fetched from the
// world's store, the way by road between them found, its ground fetched, and the road's height read
// all the way along, with the real network (through the curl on the path). The game runs the same
// check itself with -TorqueWorldTest (World/TorqueWorldSubsystem).
//
//   world_ride <folder to keep what is fetched> [latA lonA latB lonB]     (Hawes to Leyburn if none)
#include "CellStore.h"
#include "HeightTiles.h"
#include "WorldFiles.h"
#include "WorldLoad.h"
#include "WorldRide.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TorqueWorld;

static std::string GTmp;
static std::atomic<int> GAsked{0};

// A blocking GET through curl: the HTTP code (0 for no answer) and the body.
static int CurlGet(const std::string& Url, std::string& Body)
{
	for (char C : Url)
		if (C == '\'' || C == '\\' || C == ' ') return 0;
	const std::string Out = Files::Join(GTmp, "get_" + std::to_string(++GAsked));
	const std::string Cmd = "curl -sS -L --max-time 120 -A '" + std::string(CellStore::UserAgent) + "' -o '" + Out + "' -w '%{http_code}' '" + Url + "'";
	FILE* P = popen(Cmd.c_str(), "r");
	if (P == nullptr) return 0;
	char Buf[32] = {0};
	const size_t N = fread(Buf, 1, sizeof(Buf) - 1, P);
	pclose(P);
	Buf[N] = 0;
	Body.clear();
	Files::Read(Out, Body);
	Files::Remove(Out);
	return std::atoi(Buf);
}

int main(int Argc, char** Argv)
{
	if (Argc != 2 && Argc != 6)
	{
		std::printf("usage: world_ride <folder> [latA lonA latB lonB]\n");
		return 2;
	}
	const std::string Root = Argv[1];
	// Hawes market place to Leyburn market place.
	double At[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	for (int I = 0; I < 4 && Argc == 6; ++I) At[I] = std::atof(Argv[2 + I]);
	GTmp = Files::Join(Root, "tmp");
	Files::Write(Files::Join(GTmp, "here"), "");
	CellStore Cells;
	Cells.Dir = Files::Join(Root, "cells");
	Cells.Get = CurlGet;
	HeightStore Heights;
	Heights.Disk = Files::Join(Root, "heights");
	const RideCheck R = CheckRide(Cells, Heights, At[0], At[1], At[2], At[3], [](const std::string& Text) { std::printf("  %s\n", Text.c_str()); std::fflush(stdout); });
	std::printf("%s\n", R.Dump().c_str());
	std::printf("%d fetches. %s\n", GAsked.load(), R.Ok ? "PASS" : ("FAIL: " + R.Why).c_str());
	if (!R.Ok) return 1;
	// Stage 2: the ride itself planned (all of it kept by now), its road laid and a first square of ground made.
	const std::shared_ptr<RidePlan> Plan = RidePlan::Make(Cells, Heights, At[0], At[1], At[2], At[3]);
	if (!Plan->Ok) { std::printf("The ride: FAIL: %s\n", Plan->Why.c_str()); return 1; }
	double Steep = 0.0, Crest = 0.0, Off = 0.0;
	for (size_t I = 1; I + 1 < Plan->Road.size(); ++I)
	{
		const RoadPoint& P = Plan->Road[I];
		const double D = P.At.DistanceTo(Plan->Road[I - 1].At);
		if (D > 0.5) Steep = std::fmax(Steep, std::fabs(P.Height - Plan->Road[I - 1].Height) / D);
		Crest = std::fmax(Crest, std::fabs(Plan->Road[I + 1].Height - 2.0 * P.Height + Plan->Road[I - 1].Height));
		Off = std::fmax(Off, std::fabs(P.Height - Plan->Land->Height(P.At.X, P.At.Z)));
	}
	// The bends: the radius through each three points 8 m apart; the tight ones said.
	{
		int Tight = 0;
		double Least = 1e9;
		for (size_t I = 2; I + 2 < Plan->Road.size(); ++I)
		{
			const P2 A = Plan->Road[I - 2].At, B = Plan->Road[I].At, C = Plan->Road[I + 2].At;
			const double La = A.DistanceTo(B), Lb = B.DistanceTo(C), Lc = A.DistanceTo(C);
			const double Cross = std::fabs((B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X));
			const double Radius = Cross > 1e-9 ? La * Lb * Lc / (2.0 * Cross) : 1e9;
			if (Radius < Least) Least = Radius;
			if (Radius < 10.0)
			{
				++Tight;
				if (Tight <= 40) std::printf("  a bend of %.1f m radius at %.2f km (%.0f, %.0f)\n", Radius, Plan->Road[I].Along / 1000.0, B.X, B.Z);
			}
		}
		std::printf("The bends: tightest %.1f m radius; %d points tighter than 10 m\n", Least, Tight);
	}
	const auto T0 = std::chrono::steady_clock::now();
	RideMesh Tile;
	const double X0 = std::floor(Plan->Road[0].At.X / 256.0) * 256.0, Z0 = std::floor(Plan->Road[0].At.Z / 256.0) * 256.0;
	Plan->LandTile(X0, Z0, 256.0, 64, Tile);
	const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
	std::printf("The ride: %.1f km of road in %d points, steepest %.3f, sharpest crest %.3f m in 4 m, %.2f m off the land at most; a 256 m square of ground in %.1f ms\n",
		Plan->Length / 1000.0, int(Plan->Road.size()), Steep, Crest, Off, Ms);
	{
		size_t SidePts = 0;
		for (const RidePlan::SideRoad& Sd : Plan->Sides) SidePts += Sd.Pts.size();
		std::printf("Beside it: %d roads in %d points; %d bridges and %d tunnels on the way\n", int(Plan->Sides.size()), int(SidePts), Plan->Bridges, Plan->Tunnels);
		for (size_t I = 1; I < Plan->Road.size(); ++I)
			if (Plan->Road[I].Bridge && !Plan->Road[I - 1].Bridge)
			{
				size_t J = I;
				double Deep = 0.0;
				while (J + 1 < Plan->Road.size() && Plan->Road[J + 1].Bridge) { ++J; Deep = std::fmax(Deep, Plan->Road[J].Height - Plan->Land->Height(Plan->Road[J].At.X, Plan->Road[J].At.Z)); }
				std::printf("  a bridge at %.2f km, %.0f m long, %.1f m over the land at most\n", Plan->Road[I].Along / 1000.0, Plan->Road[J].Along - Plan->Road[I].Along, Deep);
			}
	}
	{
		// The other roads' bridges and tunnels, and where one goes over or under the ride's road.
		std::printf("On the roads beside it: %d bridges and %d tunnels\n", Plan->SideBridges, Plan->SideTunnels);
		int Shown = 0;
		for (const RidePlan::SideRoad& Sd : Plan->Sides)
			for (size_t I = 1; I < Sd.Pts.size(); ++I)
			{
				const RoadPoint& P = Sd.Pts[I];
				RidePlan::NearOpts MainOnly;
				MainOnly.MainOnly = true;
				RidePlan::NearHit Hit;
				if (!Plan->NearestOf(P.At.X, P.At.Z, MainOnly, Hit) || Hit.Dist > 2.0) continue;
				if (!(P.Bridge || P.Tunnel || Hit.Deck || Hit.Bore)) continue;
				if (++Shown <= 40)
					std::printf("  at %.2f km a road %s the ride's: %.1f m %s it\n", Plan->Road[size_t(Hit.Index)].Along / 1000.0, P.Bridge || Hit.Bore ? "goes over" : "goes under",
						std::fabs(P.Height - Hit.Height), P.Height > Hit.Height ? "above" : "below");
			}
	}
	if (std::getenv("WORLD_RIDE_DUMP") != nullptr)
	{
		// Every road's knots (x z height half), a road a line, for a picture.
		std::string Text;
		char Buf[96];
		for (int W = 0; W < Plan->RoadCount(); ++W)
		{
			for (const RoadPoint& P : Plan->RoadOf(W))
			{
				std::snprintf(Buf, sizeof(Buf), "%.2f %.2f %.2f %.2f ", P.At.X, P.At.Z, P.Height, P.Half);
				Text += Buf;
			}
			Text += "\n";
		}
		Files::Write(std::getenv("WORLD_RIDE_DUMP"), Text);
	}
	if (Argc == 2) Files::Write(Files::Join(Root, "route.json"), Plan->RouteJson());
	return 0;
}
