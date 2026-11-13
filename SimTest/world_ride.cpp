// The stage 1 gate of Ride UK, run from a desk: the roads between two places fetched from the
// world's store, the way by road between them found, its ground fetched, and the road's height read
// all the way along, with the real network (through the curl on the path). The game runs the same
// check itself with -TorqueWorldTest (World/TorqueWorldSubsystem).
//
//   world_ride <folder to keep what is fetched> [latA lonA latB lonB]     (Hawes to Leyburn if none)
#include "CellStore.h"
#include "HeightTiles.h"
#include "WorldFiles.h"
#include <map>
#include "WorldLoad.h"
#include "WorldRide.h"
#include "WorldRoadLine.h"
#include "WorldRoute.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TorqueWorld;

static std::string GTmp;

// [stage 8] The landmark bridges a ride carries, made as hero pieces.
static void SayHeroes(const RidePlan& Plan)
{
	std::string Text;
	std::vector<RidePlan::Landmark> Marks;
	if (!Files::Read(std::string(TORQUE_SIMTEST) + "/../Data/RideUK/landmark_bridges.json", Text) || !RidePlan::ReadLandmarks(Text, Marks)) { std::printf("  no landmark bridges file\n"); return; }
	std::vector<RidePlan::HeroPiece> Heroes;
	Plan.HeroBridges(Marks, Heroes);
	for (const RidePlan::HeroPiece& H : Heroes)
	{
		std::printf("     landmark bridge: %s, %d points\n", H.Name.c_str(), int(H.Mesh->Vertices()));
		// (WORLD_RIDE_HERO_DIR=folder: each as an OBJ with the deck's road beside it, to look at)
		if (const char* Dir = std::getenv("WORLD_RIDE_HERO_DIR"))
		{
			std::string Obj;
			const RideMesh& M = *H.Mesh;
			for (size_t I = 0; I + 2 < M.Pos.size(); I += 3) Obj += "v " + std::to_string(M.Pos[I] + M.OriginX) + " " + std::to_string(M.Pos[I + 1]) + " " + std::to_string(M.Pos[I + 2] + M.OriginZ) + "\n";
			for (size_t I = 0; I + 2 < M.Idx.size(); I += 3) Obj += "f " + std::to_string(M.Idx[I] + 1) + " " + std::to_string(M.Idx[I + 1] + 1) + " " + std::to_string(M.Idx[I + 2] + 1) + "\n";
			for (int Which = 0; Which < Plan.RoadCount(); ++Which)
				for (const RoadPoint& P : Plan.RoadOf(Which))
					if (P.Bridge && std::hypot(P.At.X - M.OriginX, P.At.Z - M.OriginZ) < 2500.0)
						Obj += "# deck " + std::to_string(P.At.X) + " " + std::to_string(P.Height) + " " + std::to_string(P.At.Z) + " " + std::to_string(P.Half) + "\n";
			Files::Write(std::string(Dir) + "/" + H.Name + ".obj", Obj);
		}
	}
}
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
	{
		// [towns stage 1] (the famous towns' looks, as the game sets them)
		std::string TownText;
		if (Files::Read(Files::Join(Files::Join(TORQUE_SIMTEST, ".."), "Data/RideUK/town_looks.json"), TownText)) SetTownTable(TownText);
	}
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
	// [stage 8] WORLD_RIDE_JOURNEYS=<a file of journeys, as the famous roads'> plans each in turn (the
	// Land's End to John o' Groats legs): how far, how long to plan, the bridges and the thin data.
	// WORLD_RIDE_FROM=n starts at the nth (1 the first).
	if (const char* JourneysFile = std::getenv("WORLD_RIDE_JOURNEYS"))
	{
		std::string Text;
		std::vector<Route::FamousRoad> Legs;
		if (!Files::Read(JourneysFile, Text) || !Route::ReadFamousRoads(Text, Legs)) { std::printf("can't read %s\n", JourneysFile); return 2; }
		const int First = std::getenv("WORLD_RIDE_FROM") != nullptr ? std::max(1, std::atoi(std::getenv("WORLD_RIDE_FROM"))) : 1;
		double AllKm = 0.0, AllMin = 0.0;
		int Failed = 0;
		for (size_t L = size_t(First - 1); L < Legs.size(); ++L)
		{
			const auto T0 = std::chrono::steady_clock::now();
			const std::shared_ptr<RidePlan> Leg = RidePlan::Make(Cells, Heights, Legs[L].Journey);
			const double Secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
			if (!Leg->Ok)
			{
				++Failed;
				std::printf("%2d %-44s FAIL in %.0f s: %s\n", int(L + 1), Legs[L].Name.c_str(), Secs, Leg->Why.c_str());
				std::fflush(stdout);
				continue;
			}
			const RideSummary Sum = Leg->Summary();
			AllKm += Sum.Metres / 1000.0;
			AllMin += Sum.Minutes;
			int LongBridges = 0;
			double Longest = 0.0;
			for (size_t I = 1; I < Leg->Road.size(); ++I)
				if (Leg->Road[I].Bridge && !Leg->Road[I - 1].Bridge)
				{
					size_t J = I;
					while (J + 1 < Leg->Road.size() && Leg->Road[J + 1].Bridge) ++J;
					const double Long = Leg->Road[J].Along - Leg->Road[I].Along;
					Longest = std::fmax(Longest, Long);
					if (Long > 150.0)
					{
						++LongBridges;
						const Geo G = Frame(Leg->Area.Roads.FrameLat, Leg->Area.Roads.FrameLon).ToGeo(Leg->Road[(I + J) / 2].At.X, Leg->Road[(I + J) / 2].At.Z);
						std::printf("     a bridge of %.0f m at %.1f km (%.5f, %.5f)\n", Long, Leg->Road[I].Along / 1000.0, G.Lat, G.Lon);
					}
				}
			std::printf("%2d %-44s %6.1f km, %4.0f min, climb %4.0f m, %d cells, planned in %3.0f s; %d bridges over 150 m (longest %.0f m); %d thin\n", int(L + 1), Legs[L].Name.c_str(),
				Sum.Metres / 1000.0, Sum.Minutes, Sum.Climb, int(Leg->Area.Cells.size()), Secs, LongBridges, Longest, int(Sum.Thin.size()));
			for (const ThinSpot& T : Sum.Thin) std::printf("     thin at %.1f km (%.4f, %.4f): %s\n", T.Along / 1000.0, T.At.Lat, T.At.Lon, T.Why.c_str());
			SayHeroes(*Leg);
			std::fflush(stdout);
		}
		std::printf("All: %.0f km, about %.1f h riding; %d failed\n", AllKm, AllMin / 60.0, Failed);
		return Failed == 0 ? 0 : 1;
	}
	const RideCheck R = CheckRide(Cells, Heights, At[0], At[1], At[2], At[3], [](const std::string& Text) { std::printf("  %s\n", Text.c_str()); std::fflush(stdout); });
	std::printf("%s\n", R.Dump().c_str());
	std::printf("%d fetches. %s\n", GAsked.load(), R.Ok ? "PASS" : ("FAIL: " + R.Why).c_str());
	if (!R.Ok) return 1;
	// Stage 2: the ride itself planned (all of it kept by now), its road laid and a first square of ground made.
	const std::shared_ptr<RidePlan> Plan = RidePlan::Make(Cells, Heights, At[0], At[1], At[2], At[3]);
	if (!Plan->Ok) { std::printf("The ride: FAIL: %s\n", Plan->Why.c_str()); return 1; }
	{
		int Urban = 0, All = 0;
		for (size_t I = 0; I < Plan->Road.size(); I += 12, ++All) Urban += Plan->Land->Cover(Plan->Road[I].At.X + 60.0, Plan->Road[I].At.Z) == 5 ? 1 : 0;
		std::printf("Thin map data: %d spots (the land beside the road built on at %d of %d points)\n", int(Plan->Thin.size()), Urban, All);
	}
	for (const ThinSpot& T : Plan->Thin) std::printf("  at %.1f km (%.4f, %.4f): %s\n", T.Along / 1000.0, T.At.Lat, T.At.Lon, T.Why.c_str());
	{
		// The railways near it (stage 5).
		int Pts = 0, Kinds[4] = {0, 0, 0, 0}, Old = 0;
		for (const RidePlan::RailLine& Run : Plan->Rails)
		{
			Pts += int(Run.At.size());
			Old += Run.Disused ? 1 : 0;
			for (unsigned char O : Run.On) ++Kinds[O & 3];
		}
		std::printf("  railways: %d lines (%d disused), %.1f km; %.1f km bridged, %.1f km in tunnels, %d level crossing points; %d platforms:", int(Plan->Rails.size()), Old,
			Pts * RidePlan::RailStep / 1000.0, Kinds[1] * RidePlan::RailStep / 1000.0, Kinds[2] * RidePlan::RailStep / 1000.0, Kinds[3], int(Plan->Platforms.size()));
		for (const RidePlan::Platform& Pl : Plan->Platforms) std::printf(" %s", Pl.Name.c_str());
		std::printf("\n");
		double Cut = 0.0, Bank = 0.0;
		for (const RidePlan::RailLine& Run : Plan->Rails)
			for (size_t I = 0; I < Run.At.size(); ++I)
			{
				if (Run.On[I] != 0) continue;
				const double Lie = Plan->Land->Height(Run.At[I].X, Run.At[I].Z) - (Run.Top[I] - 0.47);
				Cut = std::fmax(Cut, Lie);
				Bank = std::fmax(Bank, -Lie);
			}
		std::printf("  railways: deepest cutting %.1f m, highest bank %.1f m\n", Cut, Bank);
	}
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
	{
		int Mouths = 0, GiveWays = 0;
		for (const RidePlan::SideRoad& Sd : Plan->Sides)
			for (const RidePlan::Mouth& M : Sd.Mouths) { if (M.Is) ++Mouths; if (M.Is && M.GiveWay) ++GiveWays; }
		int AtStart = 0;
		for (const RidePlan::Knot& K : Plan->Knots) if (K.Owner == 0) ++AtStart;
		std::printf("Junction mouths on the ride's road: %d, %d of them with give-way lines; %d junctions off it (%d at its ends)\n", Mouths, GiveWays, int(Plan->Knots.size()), AtStart);
		int Shown = 0;
		for (const RidePlan::SideRoad& Sd : Plan->Sides)
			for (const RidePlan::Mouth& M : Sd.Mouths)
				if (M.Is && ++Shown <= 12) std::printf("  a mouth at %.2f km%s\n", Plan->Road[size_t(M.MainIndex)].Along / 1000.0, M.GiveWay ? ", give way" : "");
	}
	{
		// The ground's 4 m mesh against the road's edge: where the land's squares, straight between their
		// corners, stand over the tarmac just inside its edge, or leave the shoulder's foot in the air.
		const auto Mesh4 = [&Plan](double X, double Z)
		{
			const double Gx = std::floor(X / 4.0) * 4.0, Gz = std::floor(Z / 4.0) * 4.0, U = (X - Gx) / 4.0, V = (Z - Gz) / 4.0;
			const double H00 = Plan->Ground(Gx, Gz, 4.0), H10 = Plan->Ground(Gx + 4.0, Gz, 4.0), H01 = Plan->Ground(Gx, Gz + 4.0, 4.0), H11 = Plan->Ground(Gx + 4.0, Gz + 4.0, 4.0);
			// (the land's squares are cut corner to corner: A C B, B C D)
			return U + V <= 1.0 ? H00 + (H10 - H00) * U + (H01 - H00) * V : H11 + (H01 - H11) * (1.0 - U) + (H10 - H11) * (1.0 - V);
		};
		int Over = 0, Asked2 = 0, Air = 0, OverFlat = 0;
		double Worst = 0.0, WorstAir = 0.0;
		for (size_t I = 2; I + 2 < Plan->Road.size(); ++I)
		{
			const RoadPoint& P = Plan->Road[I];
			if (P.Bridge || P.Tunnel) continue;
			P2 D = Plan->Road[I + 1].At - Plan->Road[I - 1].At;
			D = D * (1.0 / D.Length());
			const P2 L(D.Z, -D.X);
			for (double Side : {1.0, -1.0})
			{
				++Asked2;
				const P2 In = P.At + L * (Side * (P.Half - 0.15)), Foot = P.At + L * (Side * (P.Half + RidePlan::Shoulder));
				const double Up = Mesh4(In.X, In.Z) - (P.Height + RoadLine::CrossHeight(Side * (P.Half - 0.15), P.Half, P.Bank));
				if (Up > 0.0) { ++Over; Worst = std::fmax(Worst, Up); }
				const double Gap = (P.Height + RoadLine::CrossHeight(Side * P.Half, P.Half, P.Bank) - RidePlan::ShoulderDrop) - Mesh4(Foot.X, Foot.Z);
				if (Gap > 0.0) { ++Air; WorstAir = std::fmax(WorstAir, Gap); }
				const P2 OnFlat = P.At + L * (Side * (P.Half + RidePlan::ShoulderFlat * 0.75));
				if (Mesh4(OnFlat.X, OnFlat.Z) > P.Height + RoadLine::CrossHeight(Side * P.Half, P.Half, P.Bank) - RidePlan::ShoulderDip) ++OverFlat;
			}
		}
		std::printf("The road's edges against the land's mesh: grass over the tarmac at %d of %d (%.3f m at most); over the shoulder's level part at %d; the shoulder's foot in the air at %d (%.3f m at most)\n", Over, Asked2, Worst, OverFlat, Air, WorstAir);
	}
	if (std::getenv("WORLD_RIDE_STATS") != nullptr)
	{
		// How much stands on the squares along the ride: the finest (256 m) and the middle (1 km) ones.
		for (int Level = 0; Level < 2; ++Level)
		{
			const double Size = Level == 0 ? 256.0 : 1024.0;
			const int Across = Level == 0 ? 64 : 128;
			std::map<std::pair<long long, long long>, int> Seen;
			size_t Most = 0, Sum = 0;
			double AllMs = 0.0, WorstMs = 0.0;
			for (size_t I = 0; I < Plan->Road.size(); I += 40)
			{
				const std::pair<long long, long long> Key(static_cast<long long>(std::floor(Plan->Road[I].At.X / Size)), static_cast<long long>(std::floor(Plan->Road[I].At.Z / Size)));
				if (Seen.count(Key) != 0) continue;
				Seen[Key] = 1;
				const auto T1 = std::chrono::steady_clock::now();
				RideMesh Land;
				Plan->LandTile(double(Key.first) * Size, double(Key.second) * Size, Size, Across, Land);
				const double Took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T1).count();
				AllMs += Took;
				WorstMs = std::fmax(WorstMs, Took);
				size_t N = Land.Extra ? Land.Extra->Vertices() : 0;
				if (Land.Extra && Land.Extra->Stone) N += Land.Extra->Stone->Vertices();
				if (Land.Extra && Land.Extra->Masonry) N += Land.Extra->Masonry->Vertices();
				Most = std::max(Most, N);
				Sum += N;
			}
			std::printf("Squares of %.0f m along the ride: %d; what stands on one is %d points on average, %d at most; a square with it takes %.0f ms on average, %.0f at most\n", Size, int(Seen.size()),
				int(Sum / std::max<size_t>(1, Seen.size())), int(Most), AllMs / double(std::max<size_t>(1, Seen.size())), WorstMs);
		}

		// What the cells hold round the ride: buildings, scenery by kind, cover and trees along the road.
		const Region& G = Plan->Area.Roads;
		std::map<std::string, int> Kinds;
		for (const FeaturePoint& F : G.Scenery.Points) ++Kinds["point " + F.Kind];
		for (const FeatureLine& F : G.Scenery.Lines) ++Kinds["line " + F.Kind];
		for (const FeatureArea& F : G.Scenery.Areas) ++Kinds["area " + F.Kind];
		std::printf("Buildings %d (looks %d), streams %d, pois %d, landmarks %d, places %d\n", int(G.Buildings.size()), int(G.Scenery.Buildings.size()), int(G.Streams.size()), int(G.Pois.size()), int(G.Landmarks.size()), int(G.Places.size()));
		for (const auto& K : Kinds) std::printf("  %s: %d\n", K.first.c_str(), K.second);
		int CoverN[11] = {0}, Treed = 0, Pts = 0;
		for (size_t I = 0; I < Plan->Road.size(); I += 5)
			for (double Aside : {-30.0, 30.0})
			{
				const RoadPoint& P = Plan->Road[I];
				const int C = Plan->Land->Cover(P.At.X + Aside, P.At.Z + Aside);
				++CoverN[C >= 0 && C < 10 ? C : 10];
				const int T = Plan->Land->TreesAt(P.At.X + Aside, P.At.Z + Aside);
				if (T > 0 && ((T >> 5) & 7) > 0) ++Treed;
				++Pts;
			}
		for (int C = 0; C < 10; ++C) std::printf("  cover %s: %d\n", Terrain::CoverName(C), CoverN[C]);
		std::printf("  no cover: %d; under trees: %d of %d\n", CoverN[10], Treed, Pts);
		if (!G.Buildings.empty()) { const Building& B = G.Buildings[0]; std::printf("  a building: %.0f %.0f, %.1f x %.1f, yaw %.2f, height %.1f, %d more\n", B.X, B.Z, B.W, B.D, B.Yaw, B.Height, int(B.More.size())); }
	}
	if (std::getenv("WORLD_RIDE_MESH") != nullptr)
	{
		// Every road's triangles within 45 m of a place along the ride (WORLD_RIDE_MESH_KM, or the first
		// give-way mouth): x z y r g b a corner, for a picture from above.
		P2 Centre = Plan->Road[0].At;
		bool Found = false;
		if (std::getenv("WORLD_RIDE_MESH_KM") != nullptr)
		{
			const double Want = std::atof(std::getenv("WORLD_RIDE_MESH_KM")) * 1000.0;
			for (const RoadPoint& P : Plan->Road) if (P.Along <= Want) Centre = P.At;
		}
		else if (std::getenv("WORLD_RIDE_FURNISH") != nullptr)
		{
			// (or a piece of the road's furniture: WORLD_RIDE_FURNISH=kind,n, the nth of RidePlan::EFurnish's kind)
			int Kind = 0, Nth = 0;
			std::sscanf(std::getenv("WORLD_RIDE_FURNISH"), "%d,%d", &Kind, &Nth);
			for (const RidePlan::Furnish& F : Plan->Furniture)
				if (F.Kind == Kind && Nth-- == 0)
				{
					Centre = F.At;
					std::printf("furnish %d at %.1f, %.1f facing %.2f, %.2f: %s\n", Kind, F.At.X, F.At.Z, F.Face.X, F.Face.Z, F.Text.c_str());
					break;
				}
		}
		else if (std::getenv("WORLD_RIDE_CHURCH") != nullptr)
		{
			// (or the nth church the map has, with its tower)
			int Nth = std::atoi(std::getenv("WORLD_RIDE_CHURCH"));
			for (size_t B = 0; B < Plan->BuildingRole.size(); ++B)
				if (Plan->BuildingRole[B] == 1 && Nth-- == 0) { Centre = P2(Plan->Area.Roads.Buildings[B].X, Plan->Area.Roads.Buildings[B].Z); std::printf("church at %.0f, %.0f\n", Centre.X, Centre.Z); }
		}
		else if (std::getenv("WORLD_RIDE_RAIL") != nullptr)
		{
			// (or a railway: WORLD_RIDE_RAIL=p a station's platform, b a bridge, c a level crossing, then the nth)
			char Want = 'p';
			int Nth = 0;
			std::sscanf(std::getenv("WORLD_RIDE_RAIL"), "%c,%d", &Want, &Nth);
			if (Want == 'p')
			{
				if (size_t(Nth) < Plan->Platforms.size())
				{
					const RidePlan::Platform& Pl = Plan->Platforms[size_t(Nth)];
					Centre = Plan->Rails[size_t(Pl.Line)].At[size_t((Pl.First + Pl.Last) / 2)];
					std::printf("platform %s\n", Pl.Name.c_str());
				}
			}
			else
			{
				const unsigned char Kind = Want == 'b' ? 1 : 3;
				for (const RidePlan::RailLine& Run : Plan->Rails)
					for (size_t I = 0; I < Run.At.size(); ++I)
						if (Run.On[I] == Kind && (I == 0 || Run.On[I - 1] != Kind) && Nth-- == 0) { Centre = Run.At[I]; std::printf("rail %c at %.0f, %.0f\n", Want, Run.At[I].X, Run.At[I].Z); }
			}
		}
		else if (std::getenv("WORLD_RIDE_KNOT") != nullptr)
		{
			// (or a junction off it: WORLD_RIDE_KNOT=n, the nth nearest the ride's start)
			std::vector<P2> Spots;
			for (const RidePlan::Knot& K : Plan->Knots) Spots.push_back(K.At);
			const P2 Start = Plan->Road[0].At;
			std::sort(Spots.begin(), Spots.end(), [&Start](const P2& A, const P2& B) { return A.DistanceTo(Start) < B.DistanceTo(Start); });
			const size_t Want = size_t(std::atoi(std::getenv("WORLD_RIDE_KNOT")));
			if (Want < Spots.size()) Centre = Spots[Want];
		}
		else
			for (const RidePlan::SideRoad& Sd : Plan->Sides)
				for (const RidePlan::Mouth& M : Sd.Mouths) if (!Found && M.Is && M.GiveWay) { Centre = M.At; Found = true; }
		std::string Text;
		char Buf[128];
		for (int W = 0; W < Plan->RoadCount(); ++W)
		{
			const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
			for (int First = 0; First < int(Pts.size()) - 1; First += 64)
			{
				const int Last = std::min(First + 64, int(Pts.size()) - 1);
				bool NearIt = false;
				const double Reach2 = std::getenv("WORLD_RIDE_MESH_R") != nullptr ? std::atof(std::getenv("WORLD_RIDE_MESH_R")) : 45.0;
				for (int I = First; I <= Last && !NearIt; ++I) NearIt = Pts[size_t(I)].At.DistanceTo(Centre) < Reach2 + 15.0;
				if (!NearIt) continue;
				RideMesh Mesh;
				Plan->RoadPiece(W, First, Last, Mesh);
				for (size_t T = 0; T + 2 < Mesh.Idx.size(); T += 3)
				{
					bool In = false;
					for (int C = 0; C < 3; ++C) In = In || std::hypot(Mesh.OriginX + Mesh.Pos[Mesh.Idx[T + C] * 3] - Centre.X, Mesh.OriginZ + Mesh.Pos[Mesh.Idx[T + C] * 3 + 2] - Centre.Z) < Reach2;
					if (!In) continue;
					for (int C = 0; C < 3; ++C)
					{
						const uint32_t V = Mesh.Idx[T + C];
						std::snprintf(Buf, sizeof(Buf), "%.3f %.3f %.3f %.3f %.3f %.3f\n", Mesh.OriginX + Mesh.Pos[V * 3] - Centre.X, Mesh.OriginZ + Mesh.Pos[V * 3 + 2] - Centre.Z, double(Mesh.Pos[V * 3 + 1]),
							double(Mesh.Col[V * 3]), double(Mesh.Col[V * 3 + 1]), double(Mesh.Col[V * 3 + 2]));
						Text += Buf;
					}
				}
			}
		}
		// With WORLD_RIDE_MESH_R (metres round the place): the ground's finest squares there and what stands on them.
		if (std::getenv("WORLD_RIDE_MESH_R") != nullptr)
		{
			const double Round = std::atof(std::getenv("WORLD_RIDE_MESH_R"));
			for (double Tz = std::floor((Centre.Z - Round) / 256.0) * 256.0; Tz < Centre.Z + Round; Tz += 256.0)
				for (double Tx = std::floor((Centre.X - Round) / 256.0) * 256.0; Tx < Centre.X + Round; Tx += 256.0)
				{
					RideMesh Land;
					Plan->LandTile(Tx, Tz, 256.0, 64, Land);
					const RideMesh* Extra = Land.Extra.get();
					const RideMesh* Walls = Extra ? Extra->Stone.get() : nullptr;
					const RideMesh* Houses = Extra ? Extra->Masonry.get() : nullptr;
					const RideMesh* Washed = Extra ? Extra->Render.get() : nullptr;
					const RideMesh* Wet = Land.Water.get();
					const RideMesh* Tops = Extra ? Extra->Roofing.get() : nullptr;
					const RideMesh* Brick = Extra ? Extra->Brickwork.get() : nullptr;
					for (const RideMesh* Mesh : {static_cast<const RideMesh*>(&Land), Extra, Walls, Houses, Washed, Wet, Tops, Brick})
					{
						if (Mesh == nullptr) continue;
						// (the stone's vertex colours are the scanned material's masks: shown as limestone of that brightness, a little lighter where lichened)
						const bool Masks = Mesh == Walls || Mesh == Houses;
						for (size_t T = 0; T + 2 < Mesh->Idx.size(); T += 3)
						{
							bool In = false;
							for (int C = 0; C < 3; ++C) In = In || std::hypot(Mesh->OriginX + Mesh->Pos[Mesh->Idx[T + C] * 3] - Centre.X, Mesh->OriginZ + Mesh->Pos[Mesh->Idx[T + C] * 3 + 2] - Centre.Z) < Round;
							if (!In) continue;
							for (int C = 0; C < 3; ++C)
							{
								const uint32_t V = Mesh->Idx[T + C];
								double Cr = Mesh->Col[V * 3], Cg = Mesh->Col[V * 3 + 1], Cb = Mesh->Col[V * 3 + 2];
								if (Masks)
								{
									const double Lit = Cr * 2.0 * (1.0 + 0.25 * Cb);
									Cr = 0.235 * Lit; Cg = 0.225 * Lit; Cb = 0.205 * Lit;
								}
								std::snprintf(Buf, sizeof(Buf), "%.3f %.3f %.3f %.3f %.3f %.3f\n", Mesh->OriginX + Mesh->Pos[V * 3] - Centre.X, Mesh->OriginZ + Mesh->Pos[V * 3 + 2] - Centre.Z, double(Mesh->Pos[V * 3 + 1]),
									Cr, Cg, Cb);
								Text += Buf;
							}
						}
					}
				}
		}
		Files::Write(std::getenv("WORLD_RIDE_MESH"), Text);
	}
	{
		// [OSM stage 3] What the map says of the buildings: how many of each kind the baker gave, and
		// the share with a mapped class, roof shape, roof or wall material (matched by position, as
		// BuildingsTile matches them).
		static const char* Kinds[8] = {"house", "shed", "barn", "shop", "block", "industrial", "worship", "civic"};
		const Region& G = Plan->Area.Roads;
		int ByKind[9] = {}, Classed = 0, Shaped = 0, Material = 0;
		std::map<std::string, int> Classes;
		std::unordered_map<long long, std::vector<int>> LookAt;
		const auto Key = [](double X, double Z) { return static_cast<long long>(std::floor(X / 64.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 64.0)); };
		for (size_t L = 0; L < G.Scenery.Buildings.size(); ++L) LookAt[Key(G.Scenery.Buildings[L].At.X, G.Scenery.Buildings[L].At.Z)].push_back(int(L));
		for (const Building& B : G.Buildings)
		{
			const int K = !B.More.empty() && B.More[0] >= 0 && B.More[0] < 8 ? int(B.More[0]) : 8;
			++ByKind[K];
			const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
			const BuildingLook* Tags = nullptr;
			for (int Dz = -1; Dz <= 1 && !Tags; ++Dz)
				for (int Dx = -1; Dx <= 1 && !Tags; ++Dx)
				{
					const auto It = LookAt.find(Key(B.X + Dx * 64.0, B.Z + Dz * 64.0));
					if (It == LookAt.end()) continue;
					for (int L : It->second)
					{
						const BuildingLook& Lo = G.Scenery.Buildings[size_t(L)];
						const double Ddx = Lo.At.X - B.X, Ddz = Lo.At.Z - B.Z;
						if (std::fabs(Ddx * Cs - Ddz * Sn) <= B.W * 0.5 + 0.5 && std::fabs(Ddx * Sn + Ddz * Cs) <= B.D * 0.5 + 0.5) { Tags = &Lo; break; }
					}
				}
			if (!Tags) continue;
			if (!Tags->Words[0].empty() && Tags->Words[0] != "yes") { ++Classed; ++Classes[Tags->Words[0]]; }
			Shaped += Tags->Words[1].empty() ? 0 : 1;
			Material += Tags->Words[3].empty() && Tags->Words[4].empty() ? 0 : 1;
		}
		const double All = std::max<size_t>(1, G.Buildings.size());
		std::printf("Buildings: %zu (", G.Buildings.size());
		for (int K = 0; K < 8; ++K) std::printf("%s%s %d", K ? ", " : "", Kinds[K], ByKind[K]);
		std::printf("); mapped: class %.1f%%, roof shape %.1f%%, material %.1f%%\n", 100.0 * Classed / All, 100.0 * Shaped / All, 100.0 * Material / All);
		std::vector<std::pair<int, std::string>> Top;
		for (const auto& C : Classes) Top.emplace_back(-C.second, C.first);
		std::sort(Top.begin(), Top.end());
		std::printf("  mapped classes:");
		for (size_t I = 0; I < Top.size() && I < 12; ++I) std::printf(" %s %d", Top[I].second.c_str(), -Top[I].first);
		std::printf("\n");
	}
	{
		// [towns stage 1] Which squares took a town's look, and from what.
		std::map<std::string, int> From;
		for (const auto& Sq : Plan->TownGrid) ++From[Plan->TownSources[size_t(Sq.second)]];
		std::printf("Town looks: %zu km squares (%d counted from tags, %d of the table's towns):", Plan->TownGrid.size(), Plan->TownsCounted, Plan->TownsTabled);
		for (const auto& F : From) std::printf(" %s x%d;", F.first.c_str(), F.second);
		std::printf("\n");
		for (size_t I = 0; I < Plan->TownLooks.size() && I < 6; ++I)
		{
			const LocalLook& L = Plan->TownLooks[I];
			std::printf("  %s: brick %.2f render %.2f (stone the rest); tile %.2f stone slate %.2f thatch %.2f (slate the rest); brick colour %.3f %.3f %.3f\n", Plan->TownSources[I].c_str(),
				double(L.BrickShare), double(L.RenderShare), double(L.TileShare), double(L.StoneSlateShare), double(L.ThatchShare), double(L.BrickColour.R), double(L.BrickColour.G), double(L.BrickColour.B));
		}
		// [towns stage 2] The towers.
		int Tall = 0, Parts = 0;
		for (const RidePlan::TowerShape& T : Plan->TowerShapes) { Tall += T.Top >= 40.0 ? 1 : 0; Parts += !T.Group.empty() && T.Group != T.Id ? 1 : 0; }
		int Under = 0;
		for (const Building& B : Plan->Area.Roads.Buildings) Under += Plan->UnderTower(B.X, B.Z) ? 1 : 0;
		std::printf("Towers: %zu (%d 40 m or more, %d parts, %d outlines drawn as their parts, %d heights guessed); %d box buildings left to them\n", Plan->TowerShapes.size(), Tall, Parts,
			Plan->TowersOutlines, Plan->TowersGuessed, Under);
	}
	{
		// [OSM stage 1] The road's facts: the limit all along it, the stops, the place at the start.
		std::printf("Limits along the ride (%s):\n", Plan->Britain() ? "mph" : "km/h");
		for (const RidePlan::LimitStretch& L : Plan->Limits)
		{
			bool National = false;
			const int Shown = Plan->ShownLimit(L.Kmh, &National);
			const RidePlan::RoadFact* F = Plan->FactAt((L.From + L.To) * 0.5);
			std::printf("  %6.2f-%6.2f km  %3d%s%s  %s\n", L.From / 1000.0, L.To / 1000.0, Shown, National ? " (national)" : "", L.Assumed ? " assumed" : "",
				F != nullptr ? F->Name.c_str() : "");
		}
		std::printf("Limit signs: %d roundels, %d national\n", Plan->FurnitureCount(RidePlan::Limit30), Plan->FurnitureCount(RidePlan::LimitNational));
		for (const RidePlan::RideStop& S : Plan->Stops)
			if (S.Kind >= RidePlan::StopInn) std::printf("  stop %s at %.2f km: %s\n", RidePlan::StopName(S.Kind), S.Along / 1000.0, S.Name.c_str());
		const RidePlan::RideNav N0 = Plan->NavAt(Plan->Road[0].At.X, Plan->Road[0].At.Z);
		std::printf("At the start: %s, limit %d%s, road '%s'; ride time %.0f min\n", N0.Place.c_str(), N0.Limit, N0.LimitMph ? " mph" : " km/h", N0.Road.c_str(),
			Plan->Summary().Minutes);
		std::printf("On the way: %s\n", Plan->Summary().StopsLine.c_str());
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
