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
#include "WorldRouteMap.h"
#include <set>

#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TorqueWorld;

// [ride anywhere] The roads laid with the plan, the network's blocks along the ride laid and their
// junctions, and every road laid so far.
static std::vector<std::reference_wrapper<const RidePlan::SideRoad>> PlanSideRoads(const RidePlan& P)
{
	std::vector<std::reference_wrapper<const RidePlan::SideRoad>> Out;
	for (int W : P.PlanSides()) Out.push_back(std::cref(*P.SideOf(W)));
	return Out;
}

static std::vector<RidePlan::Knot> LaidKnots(const RidePlan& P)
{
	TorqueWorld::Rect Box;
	Box.X0 = Box.Z0 = 1e18;
	Box.X1 = Box.Z1 = -1e18;
	for (const RoadPoint& R : P.Road)
	{
		Box.X0 = std::fmin(Box.X0, R.At.X - 300.0); Box.X1 = std::fmax(Box.X1, R.At.X + 300.0);
		Box.Z0 = std::fmin(Box.Z0, R.At.Z - 300.0); Box.Z1 = std::fmax(Box.Z1, R.At.Z + 300.0);
	}
	P.LayBlocks(Box);
	return P.KnotsLaid();
}

static std::vector<int> LaidRoads(const RidePlan& P)
{
	std::vector<int> Out = {0};
	for (int W = 1; W < P.RoadCount(); ++W)
		if (P.SideOf(W) != nullptr) Out.push_back(W);
	return Out;
}

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
			for (const int Which : LaidRoads(Plan))
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

// [OSM stage 4] WORLD_RIDE_GROUND=<folder of ground_ROW_COL.json and paths_ROW_COL.json, as Tools' bakers
// wrote them>: the store's index is given their hashes and the files are served from there, as if the
// world's store had them already.
static std::string GGround;
static int FixtureGet(const std::string& Url, std::string& Body)
{
	if (!GGround.empty())
		for (const char* Kind : {"ground", "paths"})
		{
			const std::string Mark = std::string("/") + Kind + "/c";
			const size_t At = Url.find(Mark);
			if (At == std::string::npos) continue;
			const std::string Cell = Url.substr(At + Mark.size(), Url.size() - At - Mark.size() - 5);
			if (Files::Read(Files::Join(GGround, std::string(Kind) + "_" + Cell + ".json"), Body)) return 200;
		}
	const int Code = CurlGet(Url, Body);
	if (GGround.empty()) return Code;
	const auto Ends = [&Url](const std::string& Tail) { return Url.size() >= Tail.size() && Url.compare(Url.size() - Tail.size(), Tail.size(), Tail) == 0; };
	if (Ends("index.json") && Code == 200)
	{
		std::string Grounds, PathsJ;
		for (const Files::FileFound& F : Files::Walk(GGround))
		{
			const std::string Base = F.Path.substr(F.Path.find_last_of('/') + 1);
			std::string Text;
			if (Base.size() < 6 || Base.compare(Base.size() - 5, 5, ".json") != 0 || (!Files::Read(F.Path, Text) && !Files::Read(Files::Join(GGround, F.Path), Text))) continue;
			const bool IsGround = Base.rfind("ground_", 0) == 0, IsPaths = Base.rfind("paths_", 0) == 0;
			if (!IsGround && !IsPaths) continue;
			const std::string Cell = Base.substr(IsGround ? 7 : 6, Base.size() - 5 - (IsGround ? 7 : 6));
			std::string& Into = IsGround ? Grounds : PathsJ;
			Into += std::string(Into.empty() ? "" : ",") + "\"" + Cell + "\":\"" + CellStore::TextHash(Text) + "\"";
		}
		const size_t Last = Body.find_last_of('}');
		if (Last != std::string::npos) Body.insert(Last, ",\"grounds\":{" + Grounds + "},\"paths\":{" + PathsJ + "}");
		return Code;
	}
	return Code;
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
		std::string SiteText;
		if (Files::Read(Files::Join(Files::Join(TORQUE_SIMTEST, ".."), "Data/RideUK/landmarks_uk.json"), SiteText) && SetSiteTable(SiteText))
		{
			std::printf("site notes: %d\n", int(SiteTable().size()));
			// [places stage 6] (and their own models, as the game reads them)
			std::vector<std::shared_ptr<const SiteModel>> Models;
			for (const SiteNote& N : SiteTable())
			{
				if (N.Model.empty()) continue;
				std::string Bytes, Why;
				std::shared_ptr<SiteModel> M = std::make_shared<SiteModel>();
				M->Id = N.Model;
				if (Files::Read(Files::Join(Files::Join(TORQUE_SIMTEST, ".."), "Data/RideUK/Sites/" + N.Model + ".glb"), Bytes) && ReadSiteModel(Bytes, *M, &Why)) Models.push_back(M);
				else std::printf("site model %s not read: %s\n", N.Model.c_str(), Why.c_str());
			}
			std::printf("site models: %d\n", int(Models.size()));
			SetSiteModels(std::move(Models));
		}
	}
	// Hawes market place to Leyburn market place.
	double At[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	for (int I = 0; I < 4 && Argc == 6; ++I) At[I] = std::atof(Argv[2 + I]);
	GTmp = Files::Join(Root, "tmp");
	Files::Write(Files::Join(GTmp, "here"), "");
	CellStore Cells;
	Cells.Dir = Files::Join(Root, "cells");
	Cells.Get = FixtureGet;
	if (const char* Fixtures = std::getenv("WORLD_RIDE_GROUND")) GGround = Fixtures;
	HeightStore Heights;
	Heights.Disk = Files::Join(Root, "heights");
	// [build 147] WORLD_RIDE_MAP=lat,lon,zoom[,file.ppm]: the route planner's relief there (with the
	// coast known, as the game draws it) and how much of it is sea; WORLD_RIDE_MAP_NOCOAST=1 without.
	if (const char* MapAt = std::getenv("WORLD_RIDE_MAP"))
	{
		Route::MapView V;
		char File[512] = {0};
		if (std::sscanf(MapAt, "%lf,%lf,%lf,%511s", &V.Lat, &V.Lon, &V.Zoom, File) < 3) { std::printf("WORLD_RIDE_MAP=lat,lon,zoom[,file]\n"); return 2; }
		V.W = 1024; V.H = 768;
		if (const char* Layer = std::getenv("WORLD_RIDE_MAP_LAYER")) V.Layer = std::atoi(Layer);
		Route::MapRoads Roads;
		const std::vector<TileKey> Keys = V.Tiles();
		if (std::getenv("WORLD_RIDE_MAP_NOCOAST") == nullptr)
		{
			std::set<CellId> Under;
			for (const TileKey& K : Keys)
				for (const CellId& C : CellsUnder(K)) Under.insert(C);
			Cells.IndexNow();
			KnowCoasts(Cells, Heights, std::vector<CellId>(Under.begin(), Under.end()), 24, &Roads);
		}
		Heights.Fetch(Keys, Cells.Get, 6);
		std::vector<uint8_t> Px;
		Route::DrawRelief(Heights, V, Px, &Roads);
		size_t Sea = 0, All = 0;
		for (int J = 0; J < V.H; ++J)
			for (int I = 0; I < V.W; ++I)
			{
				++All;
				const uint8_t* C = &Px[(size_t(J) * size_t(V.W) + size_t(I)) * 4];
				if (C[0] > C[1] && C[0] > C[2] + 30) ++Sea;   // (blue: B over G and R)
			}
		std::printf("Map at %.4f, %.4f zoom %.1f: %zu tiles, sea %.1f%% of it\n", V.Lat, V.Lon, V.Zoom, Keys.size(), 100.0 * double(Sea) / double(All));
		if (File[0] != 0)
		{
			std::string Ppm = "P6\n" + std::to_string(V.W) + " " + std::to_string(V.H) + "\n255\n";
			for (size_t P = 0; P < size_t(V.W) * size_t(V.H); ++P) { Ppm += char(Px[P * 4 + 2]); Ppm += char(Px[P * 4 + 1]); Ppm += char(Px[P * 4]); }
			Files::Write(File, Ppm);
		}
		return 0;
	}
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
		for (const RidePlan::SideRoad& Sd : PlanSideRoads(*Plan)) SidePts += Sd.Pts.size();
		std::printf("Beside it: %d roads in %d points; %d bridges and %d tunnels on the way\n", int(Plan->PlanSides().size()), int(SidePts), Plan->Bridges, Plan->Tunnels);
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
		for (const RidePlan::SideRoad& Sd : PlanSideRoads(*Plan))
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
		for (const RidePlan::SideRoad& Sd : PlanSideRoads(*Plan))
			for (const RidePlan::Mouth& M : Sd.Mouths) { if (M.Is) ++Mouths; if (M.Is && M.GiveWay) ++GiveWays; }
		int AtStart = 0;
		const std::vector<RidePlan::Knot> Knots = LaidKnots(*Plan);
		for (const RidePlan::Knot& K : Knots) if (K.Owner == 0) ++AtStart;
		std::printf("Junction mouths on the ride's road: %d, %d of them with give-way lines; %d junctions off it (%d at its ends)\n", Mouths, GiveWays, int(Knots.size()), AtStart);
		// [towns stage 3] The buildings drawn round the ride's start (WORLD_RIDE_OUTLINES=1): how many
		// came from true outlines, how long the squares took, and their tally.
		if (std::getenv("WORLD_RIDE_OUTLINES") != nullptr)
		{
			RidePlan::BuildingTally Tally;
			size_t Tris = 0;
			const auto OutT0 = std::chrono::steady_clock::now();
			const double Cx = std::floor(Plan->Road[0].At.X / 256.0) * 256.0, Cz = std::floor(Plan->Road[0].At.Z / 256.0) * 256.0;
			for (int Dz = -2; Dz <= 2; ++Dz)
				for (int Dx = -2; Dx <= 2; ++Dx)
				{
					RideMesh Body, Wash, Homes, Tops, Brick;
					const double Sx = Cx + Dx * 256.0, Sz = Cz + Dz * 256.0;
					Plan->BuildingsTile(Sx, Sz, 256.0, true, Plan->LookHere(Sx + 128.0, Sz + 128.0), Body, Wash, Homes, Tops, Brick, &Tally);
					Tris += (Body.Idx.size() + Wash.Idx.size() + Homes.Idx.size() + Tops.Idx.size() + Brick.Idx.size()) / 3;
				}
			const double OutMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - OutT0).count();
			std::printf("Buildings round the start (25 squares of 256 m): %d boxes from true outlines, %d drawn, %d terraces, %zu triangles, in %.0f ms\n",
				Tally.Outlined, Tally.Drawn, Tally.Terraces, Tris, OutMs);
			std::printf("Houses (not rows): 1 storey %d (%d from the map's height), 2 storeys %d (%d), 3+ %d (%d); by a road %d, a long side towards it %d\n",
				Tally.HouseFloors[0], Tally.HouseMapped[0], Tally.HouseFloors[1], Tally.HouseMapped[1], Tally.HouseFloors[2], Tally.HouseMapped[2],
				Tally.HousesByRoad, Tally.LongSideToRoad);
			std::printf("House roofs: %d hipped, %d lean-to of %d drawn\n", Tally.Hipped, Tally.LeanTo, Tally.Drawn);
		}
		// [ride anywhere 3] The ride grown round a place (WORLD_RIDE_GROW=x,z in the frame, m): the cells
		// added, how long it took, and that every old road and the ride's own road are as they were.
		if (const char* GrowAt = std::getenv("WORLD_RIDE_GROW"))
		{
			double Gx = 0.0, Gz = 0.0;
			if (std::sscanf(GrowAt, "%lf,%lf", &Gx, &Gz) == 2)
			{
				const std::vector<CellId> Near = Plan->CellsToGrow(Gx, Gz, RidePlan::GrowPad);
				const auto GrowT0 = std::chrono::steady_clock::now();
				std::string GrowWhy;
				const std::shared_ptr<RidePlan> Big = RidePlan::Grow(Cells, Heights, *Plan, Gx, Gz, &GrowWhy);
				const double GrowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - GrowT0).count();
				if (!Big) std::printf("Growing at (%.0f, %.0f): %zu cells near not read; not grown: %s\n", Gx, Gz, Near.size(), GrowWhy.c_str());
				else
				{
					const Region& G0 = Plan->Area.Roads;
					const Region& G1 = Big->Area.Roads;
					int Moved = 0;
					for (size_t E = 0; E < G0.Edges.size(); ++E)
					{
						const RoadEdge& A = G0.Edges[E];
						const RoadEdge& B = G1.Edges[E];
						if (A.A != B.A || A.B != B.B || A.Line.size() != B.Line.size()) { ++Moved; continue; }
						for (size_t K = 0; K < A.Line.size(); ++K)
							if (A.Line[K].X != B.Line[K].X || A.Line[K].Z != B.Line[K].Z) { ++Moved; break; }
					}
					double RoadOff = Plan->Road.size() == Big->Road.size() ? 0.0 : 1e9;
					for (size_t I = 0; I < Plan->Road.size() && I < Big->Road.size(); ++I)
						RoadOff = std::fmax(RoadOff, std::fabs(Plan->Road[I].At.X - Big->Road[I].At.X) + std::fabs(Plan->Road[I].At.Z - Big->Road[I].At.Z)
							+ std::fabs(Plan->Road[I].Height - Big->Road[I].Height));
					std::printf("Growing at (%.0f, %.0f): %zu cells near not read; grown from %zu to %zu cells, %zu to %zu roads in %.0f ms; "
						"%d old roads changed, the ride's road %.3f m off; %zu roads to draw again\n",
						Gx, Gz, Near.size(), Plan->Area.Cells.size(), Big->Area.Cells.size(), G0.Edges.size(), G1.Edges.size(), GrowMs, Moved, RoadOff,
						Big->RedrawRoads.size());
				}
			}
		}
		// [ride anywhere] The network round a place off the ride (WORLD_RIDE_ROAM=x,z in the frame, m):
		// its blocks within 1.2 km laid, and how long that took.
		if (const char* Roam = std::getenv("WORLD_RIDE_ROAM"))
		{
			double Rx = 0.0, Rz = 0.0;
			if (std::sscanf(Roam, "%lf,%lf", &Rx, &Rz) == 2)
			{
				TorqueWorld::Rect Around;
				Around.X0 = Rx - 1200.0; Around.Z0 = Rz - 1200.0; Around.X1 = Rx + 1200.0; Around.Z1 = Rz + 1200.0;
				const auto RoamT0 = std::chrono::steady_clock::now();
				const int Before = Plan->BlocksLaid();
				const std::vector<int> Roads = Plan->RoadsNear(Around, true);
				const double RoamMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RoamT0).count();
				size_t Pts = 0;
				double Worst = 0.0;
				int NoGround = 0;
				for (int W : Roads)
				{
					Pts += Plan->RoadOf(W).size();
					for (const RoadPoint& P : Plan->RoadOf(W))
					{
						const double G = Plan->Land->Height(P.At.X, P.At.Z);
						if (std::isnan(G)) { ++NoGround; continue; }
						if (!P.Bridge && !P.Tunnel) Worst = std::fmax(Worst, std::fabs(P.Height - G));
					}
				}
				std::printf("  the roads there sit within %.1f m of the ground (%d points with none)\n", Worst, NoGround);
				{
					// [ride anywhere 4] the network's furniture there, made and then asked again (kept)
					std::vector<std::shared_ptr<const std::vector<RidePlan::Furnish>>> FurnHeld;
					std::vector<const RidePlan::Furnish*> FurnHere;
					const auto FurnT0 = std::chrono::steady_clock::now();
					Plan->NetworkFurniture(Around.X0, Around.Z0, Around.X1 - Around.X0, FurnHeld, FurnHere);
					const double FurnMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - FurnT0).count();
					std::map<int, int> ByKind;
					for (const RidePlan::Furnish* F : FurnHere) ++ByKind[F->Kind];
					std::printf("  the network's furniture there: %zu in %.0f ms (lamps %d, chevrons %d, post boxes %d, bus stops %d, benches %d, cattle grids %d)\n",
						FurnHere.size(), FurnMs, ByKind[RidePlan::StreetLamp], ByKind[RidePlan::Chevron], ByKind[RidePlan::PostBox], ByKind[RidePlan::BusStop],
						ByKind[RidePlan::Seat], ByKind[RidePlan::CattleGrid]);
				}
				RidePlan::NearHit Hit;
				const bool On = Plan->NearestOf(Rx, Rz, RidePlan::NearOpts(), Hit);
				std::printf("Roaming at (%.0f, %.0f): %d blocks laid in %.0f ms, %d roads in %d points; nearest road %s %.1f m off\n", Rx, Rz, Plan->BlocksLaid() - Before, RoamMs,
					int(Roads.size()), int(Pts), On ? std::to_string(Hit.Which).c_str() : "none", On ? Hit.Dist : -1.0);
				// (the sat nav's way back from a road there, heading north and then south)
				for (const int W : Roads)
				{
					if (W == 0) continue;
					const std::vector<RoadPoint>& On2 = Plan->RoadOf(W);
					if (On2.size() < 4) continue;
					const P2 At2 = On2[On2.size() / 2].At;
					for (const double Heading : {0.0, 3.14159265})
					{
						const auto WayT0 = std::chrono::steady_clock::now();
						const std::shared_ptr<const RideDetour> D = Plan->DetourFrom(At2.X, At2.Z, Heading, 0.0);
						const double DMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - WayT0).count();
						if (D == nullptr) { std::printf("  no way back from road %d\n", W); continue; }
						std::printf("  the way back from road %d at (%.0f, %.0f) heading %.0f: %.2f km, %d turns, rejoins at %.2f km (%.1f ms)\n", W, At2.X, At2.Z, Heading, D->Length / 1000.0,
							int(D->Turns.size()), D->RejoinAlong / 1000.0, DMs);
						for (const RidePlan::RideTurn& Tn : D->Turns) std::printf("    at %.2f km turn %d onto %s\n", Tn.Along / 1000.0, Tn.Turn, Tn.Onto.c_str());
						const RidePlan::RideNav Nv = Plan->NavDetour(*D, At2.X, At2.Z);
						std::printf("    nav: turn %d in %.0f m onto %s; %.2f km to the finish; on %s\n", Nv.Turn, Nv.ToTurn, Nv.Onto.c_str(), Nv.ToFinish / 1000.0, Nv.Road.c_str());
					}
					break;
				}
			}
		}
		int Shown = 0;
		for (const RidePlan::SideRoad& Sd : PlanSideRoads(*Plan))
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
	{
		// [OSM stage 4] The ground and paths the cells brought (none until the store has them, or WORLD_RIDE_GROUND).
		const GroundData& Gd = Plan->Area.Roads.Ground;
		std::map<std::string, int> ByKind;
		for (const GroundShape& Sh : Gd.Areas) ++ByKind[Sh.Kind];
		for (const GroundShape& Sh : Gd.Lines) ++ByKind[Sh.Kind];
		for (const GroundShape& Sh : Gd.Points) ++ByKind[Sh.Kind];
		for (const GroundShape& Sh : Gd.Paths) ++ByKind[Sh.Kind];
		std::printf("Ground: %d areas, %d lines, %d points, %d paths;", int(Gd.Areas.size()), int(Gd.Lines.size()), int(Gd.Points.size()), int(Gd.Paths.size()));
		for (const auto& Kv : ByKind) std::printf(" %s %d", Kv.first.c_str(), Kv.second);
		std::printf("\n");
	}
	{
		// [OSM stage 5] The road itself the cells brought (roadx): how much each rule will have to carry.
		const Region& G = Plan->Area.Roads;
		int Tagged = 0, WithLanes = 0, Single = 0, Widths = 0, Rings = 0, Lit = 0, Paved = 0, Halves = 0, Snapped = 0;
		for (int E = 0; E < int(G.Edges.size()); ++E)
			if (const RoadExtra* X = G.ExtraOf(E))
			{
				++Tagged;
				WithLanes += X->Lanes > 0 ? 1 : 0;
				Single += X->Lanes == 1 && X->Has(RoadExtra::Oneway) == false ? 1 : 0;
				Widths += X->Width > 0.0 ? 1 : 0;
				Rings += X->Has(RoadExtra::Roundabout) ? 1 : 0;
				Lit += X->Has(RoadExtra::Lit) ? 1 : 0;
				Paved += X->Has(RoadExtra::PaveLeft) || X->Has(RoadExtra::PaveRight) ? 1 : 0;
				Halves += X->Half.empty() ? 0 : 1;
			}
		std::map<std::string, int> ByKind;
		for (const MappedPoint& P : G.RoadPoints)
		{
			++ByKind[P.Kind];
			Snapped += P.Edge >= 0 ? 1 : 0;
		}
		std::printf("Road itself: %d of %d edges tagged (%d with lanes, %d single track, %d widths, %d roundabout, %d lit, %d paved, %d dual halves); %d points, %d on a road;",
			Tagged, int(G.Edges.size()), WithLanes, Single, Widths, Rings, Lit, Paved, Halves, int(G.RoadPoints.size()), Snapped);
		for (const auto& Kv : ByKind) std::printf(" %s %d", Kv.first.c_str(), Kv.second);
		std::printf("; %d barrier lines\n", int(G.Barriers.size()));
	}
	{
		// [OSM stage 5] The mapped marks on the ride's own road, and none of their paint off its edge.
		std::map<std::string, int> ByKind;
		for (const RidePlan::MappedMark& M : Plan->MainMapped) ++ByKind[M.Kind];
		std::vector<RidePlan::RoadMark> Marks;
		const double Len = Plan->Road.empty() ? 0.0 : Plan->Road.back().Along;
		Plan->MappedMarks(0, 0.0, Len, Marks);
		int OffEdge = 0, Paint = 0, Humps = 0, Bays = 0;
		for (const RidePlan::RoadMark& Mk : Marks)
		{
			Paint += Mk.Kind == RidePlan::MarkPaint ? 1 : 0;
			Humps += Mk.Kind == RidePlan::MarkHump ? 1 : 0;
			Bays += Mk.Kind == RidePlan::MarkBay ? 1 : 0;
			if (Mk.Kind == RidePlan::MarkBay) continue;
			double Half = 0.0;
			for (size_t I = 0; I + 1 < Plan->Road.size(); ++I)
				if (Plan->Road[I].Along <= Mk.From && Plan->Road[I + 1].Along > Mk.From) { Half = std::fmin(Plan->Road[I].Half, Plan->Road[I + 1].Half); break; }
			if (Mk.Left > Half + 0.01 || Mk.Right < -Half - 0.01) ++OffEdge;
		}
		std::printf("Mapped on the ride: %d;", int(Plan->MainMapped.size()));
		for (const auto& Kv : ByKind) std::printf(" %s %d", Kv.first.c_str(), Kv.second);
		std::printf("; marks: %d paint, %d hump slices, %d bay slices; paint off the road's edge: %d\n", Paint, Humps, Bays, OffEdge);
	}
	if (std::getenv("WORLD_RIDE_SIGHTS_FROM_LANDMARKS") != nullptr && Plan->Area.Roads.Sights.empty())
	{
		// (no places file baked here yet: the cells' landmarks stand in for them, to try the stops)
		Region& G = Plan->Area.Roads;
		for (const TorqueFit::Json& Lm : G.Landmarks)
		{
			Sight S;
			const std::string K = Lm.Str("kind");
			S.Kind = K == "worship" ? "church" : K;
			S.Name = Lm.Str("name");
			S.Qid = K == "castle" || K == "worship" ? "Q" + std::to_string(G.Sights.size() + 1) : "";
			S.At = P2(Lm.Num("x"), Lm.Num("z"));
			G.Sights.push_back(S);
		}
		const auto Began = std::chrono::steady_clock::now();
		Plan->IndexPlaces();
		std::printf("IndexPlaces: %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Began).count());
		Plan->IndexFields();
		if (std::getenv("WORLD_RIDE_RECIPES") != nullptr)
		{
			// (one of each recipe's kind in a row 40 m off the ride's road from 300 m along, 70 m apart)
			const char* Kinds[] = {"stone_circle", "standing_stone", "cross", "obelisk", "memorial", "church", "tower", "lighthouse", "windmill", "chimney", "castle_ruin", "castle", "hillfort",
				"stone_circle henge", "stone_circle arbor", "standing_stone arrows", "lighthouse hoad"};
			int K = 0;
			std::string TestSites;
			for (const char* Kd : Kinds)
			{
				const double Want = 300.0 + 70.0 * K++;
				for (size_t I = 1; I < Plan->Road.size(); ++I)
					if (Plan->Road[I].Along >= Want)
					{
						const P2 D = Plan->Road[I].At - Plan->Road[I - 1].At;
						const double L = std::fmax(1e-6, D.Length());
						Sight S;
						const std::string Full = Kd;
						S.Kind = Full.substr(0, Full.find(' '));
						S.Name = std::string("Test ") + Kd;
						S.Qid = "QT" + std::to_string(K);
						S.At = Plan->Road[I].At + P2(D.Z / L, -D.X / L) * (std::string(Kd) == "hillfort" ? 90.0 : 40.0);
						if (std::string(Kd) == "castle_ruin")
							for (const P2& C : {P2(-22, -15), P2(20, -18), P2(24, 12), P2(5, 20), P2(-20, 14)}) S.Outline.push_back(S.At + C);
						G.Sights.push_back(S);
						std::printf("recipe test %s at %.1f, %.1f\n", Kd, S.At.X, S.At.Z);
						if (Full.find(' ') != std::string::npos)
						{
							// [places stage 5] (a site note for it, as landmarks_uk.json's famous ones)
							const Geo Where = Frame(G.FrameLat, G.FrameLon).ToGeo(S.At.X, S.At.Z);
							const std::string Word = Full.substr(Full.find(' ') + 1);
							const char* Extra = Word == "henge" ? "\"radius\": 16.5, \"tall\": 4.1, \"stones\": 30, \"lintels\": true"
								: Word == "arbor" ? "\"radius\": 19, \"tall\": 2.0, \"stones\": 46, \"fallen\": true"
								: Word == "arrows" ? "\"radius\": 60, \"tall\": 6.0, \"stones\": 3" : "\"tall\": 30, \"banded\": false, \"unlit\": true";
							char Row[512];
							std::snprintf(Row, sizeof(Row), "%s{\"name\": \"%s\", \"match\": \"%s\", \"at\": [%.7f, %.7f], \"note\": \"test %s\", %s}", TestSites.empty() ? "" : ",",
								S.Name.c_str(), Word.c_str(), Where.Lat, Where.Lon, Word.c_str(), Extra);
							TestSites += Row;
						}
						break;
					}
			}
			if (!TestSites.empty()) SetSiteTable("{\"sites\": [" + TestSites + "]}");
		}
		if (std::getenv("WORLD_RIDE_RECIPES") != nullptr)
		{
			Plan->NoteSites();
			int Noted = 0;
			for (int N : Plan->SightNote) Noted += N >= 0 ? 1 : 0;
			std::printf("site notes on this ride's places: %d\n", Noted);
		}
		Plan->IndexPlaceRecipes();
		{
			int Dams = 0;
			for (const RidePlan::WildLine& W : Plan->WildLines) Dams += W.Kind == 1 ? 1 : 0;
			std::printf("Dams in the region: %d\n", Dams);
		}
		std::printf("Models: %d; recipes:", Plan->ModelCount);
		for (int Rk = 0; Rk < RidePlan::RecipeKinds; ++Rk) std::printf(" %d", Plan->RecipeCount[Rk]);
		std::printf("\n");
	}
	{
		// [places stage 2] The stops made at the places worth riding to.
		std::printf("Places: %d looked at (of %d mapped), %d too far from a road; stops: %d made pull-offs, %d car parks, %d lay-bys, %d boards in streets\n", Plan->PlacesSeen,
			int(Plan->Area.Roads.Sights.size()), Plan->PlacesFar, Plan->PlaceStopCount[RidePlan::PlaceMade], Plan->PlaceStopCount[RidePlan::PlaceCarPark],
			Plan->PlaceStopCount[RidePlan::PlaceLayby], Plan->PlaceStopCount[RidePlan::PlaceStreet]);
		std::printf("Places stage 7: %d paths in to the places, %d kissing gates on them\n", Plan->PlacePaths, Plan->PlaceGates);
		const Frame Fr(Plan->Area.Roads.FrameLat, Plan->Area.Roads.FrameLon);
		int Shown = 0;
		for (size_t I = 0; I < Plan->PlaceStops.size() && Shown < 12; ++I)
		{
			const RidePlan::PlaceStop& S = Plan->PlaceStops[I];
			const Sight& First = Plan->Area.Roads.Sights[size_t(S.Sights[0])];
			const Geo Where = Fr.ToGeo(S.At.X, S.At.Z);
			static const char* Hows[] = {"pull-off", "car park", "lay-by", "street"};
			std::printf("  stop '%s' (%s, %zu place%s): %s at %.5f,%.5f, %.0f m from it, depth %.1f m\n", Plan->PlaceStopName(int(I)).c_str(), First.Kind.c_str(), S.Sights.size(),
				S.Sights.size() == 1 ? "" : "s", Hows[S.How], Where.Lat, Where.Lon, S.At.DistanceTo(First.At), S.Depth);
			++Shown;
		}
	}
	if (const char* ProbeAt = std::getenv("WORLD_RIDE_PROBE"))
	{
		// (the roads round a point x,z: each one's nearest bit, how far, how wide, which way)
		P2 Q;
		std::sscanf(ProbeAt, "%lf,%lf", &Q.X, &Q.Z);
		Rect Round;
		Round.X0 = Q.X - 300.0; Round.Z0 = Q.Z - 300.0; Round.X1 = Q.X + 300.0; Round.Z1 = Q.Z + 300.0;
		Plan->LayBlocks(Round);
		RidePlan::NearOpts O;
		for (int Guard = 0; Guard < 6; ++Guard)
		{
			RidePlan::NearHit H;
			if (!Plan->NearestOf(Q.X, Q.Z, O, H) || H.Dist > 30.0) break;
			const std::vector<RoadPoint>& Pts = Plan->RoadOf(H.Which);
			std::printf("  probe: road %d (%zu points, %.0f m) at %.1f m along, %.2f m off its middle (half %.2f), height %.2f, one way %d dual %d\n", H.Which, Pts.size(), Pts.empty() ? 0.0 : Pts.back().Along, H.Along, H.Dist, H.Half,
				H.Height, int(Pts[size_t(H.Index)].OneWay), int(Pts[size_t(H.Index)].Dual));
			std::printf("    ground there %.2f\n", Plan->Ground(Q.X, Q.Z));
			O.NotRoad = H.Which;
			// (only one road can be left out at a time: the next nearest after this one)
		}
	}
	if (std::getenv("WORLD_RIDE_DUALS") != nullptr)
	{
		// [OSM stage 5C] The dual carriageways: the region's edges the map pairs, and how much of the
		// ride's road is one half of one, with its other half found across from it.
		int Edges = 0, Paired = 0;
		std::map<std::string, int> ByClass;
		for (size_t E = 0; E < Plan->Area.Roads.Edges.size(); ++E)
		{
			const RoadExtra* X = Plan->Area.Roads.ExtraOf(int(E));
			if (X == nullptr || X->Half.empty()) continue;
			++Edges;
			Paired += X->Has(RoadExtra::Roundabout) ? 0 : 1;
			char Key[96];
			std::snprintf(Key, sizeof(Key), "%s lanes %d width %.0f", Plan->Area.Roads.ClassOf(int(E)).c_str(), X->Lanes, X->Width);
			++ByClass[Key];
		}
		for (const auto& Kv : ByClass)
			if (Kv.second >= 5) std::printf("  %d halves: %s\n", Kv.second, Kv.first.c_str());
		// (where slip roads meet a dual carriageway's half: the first few)
		int Slips = 0;
		const Region& G = Plan->Area.Roads;
		for (size_t E = 0; E < G.Edges.size() && Slips < 6; ++E)
		{
			const std::string& Class = G.ClassOf(int(E));
			const RoadExtra* Xe = G.ExtraOf(int(E));
			if (G.Edges[E].Oneway == 0 || (Xe != nullptr && (!Xe->Half.empty() || Xe->Has(RoadExtra::Roundabout))) || (Class != "motorway" && Class != "trunk")) continue;
			for (int Nd : {G.Edges[E].A, G.Edges[E].B})
				for (size_t F = 0; F < G.Edges.size(); ++F)
				{
					const RoadExtra* X = G.ExtraOf(int(F));
					if (F == E || X == nullptr || X->Half.empty() || (G.Edges[F].A != Nd && G.Edges[F].B != Nd)) continue;
					if (Slips++ < 6) std::printf("  %s meets %s at %.0f, %.0f\n", Class.c_str(), G.ClassOf(int(F)).c_str(), G.Nodes[size_t(Nd)].X, G.Nodes[size_t(Nd)].Z);
					break;
				}
		}
		// (the ride's way by class, where the class changes: onto and off the fast roads)
		std::string Was;
		double Run = 0.0;
		for (const auto& St : Plan->MadeSteps)
		{
			const std::string& Class = Plan->Area.Roads.ClassOf(St.Edge);
			if (Class != Was) std::printf("  at %.0f m along the way: %s\n", Run, Class.c_str());
			Was = Class;
			Run += Plan->Area.Roads.Edges[size_t(St.Edge)].Length;
		}
		double DualM = 0.0, FoundM = 0.0, GapSum = 0.0, Proud = -1e9;
		P2 ProudAt;
		for (size_t I = 1; I < Plan->Road.size(); ++I)
		{
			if (!Plan->Road[I].Dual) continue;
			const double Step = Plan->Road[I].Along - Plan->Road[I - 1].Along;
			DualM += Step;
			RidePlan::DualHit D;
			if (Plan->DualAcross(0, int(I), 0, D))
			{
				FoundM += Step;
				GapSum += D.Gap * Step;
				if (std::getenv("WORLD_RIDE_DUAL_GAPS")) std::printf("    gap %.1f half %.2f\n", D.Gap, Plan->Road[I].Half);
				// (the land across the reservation against the line from one edge to the other: never over it)
				const P2 Dir = Plan->Road[I].At - Plan->Road[I - 1].At;
				const double Len = Dir.Length();
				if (Len < 1e-9) continue;
				const P2 Left(Dir.Z / Len, -Dir.X / Len);
				for (double T = 0.1; T < 0.95 && D.Gap <= RidePlan::MedianMost; T += 0.2)
				{
					const P2 Q = Plan->Road[I].At + Left * (double(D.Side) * (Plan->Road[I].Half + D.Gap * T));
					RidePlan::NearOpts Any;
					RidePlan::NearHit On;
					if (Plan->NearestOf(Q.X, Q.Z, Any, On) && On.Dist <= On.Half + 0.5) continue;   // (on tarmac: the road edge checks' business)
					const double Over = Plan->Ground(Q.X, Q.Z) - std::fmax(D.Near + (D.Far - D.Near) * T, T < 0.5 ? D.Near : D.Far);
					if (Over > Proud) { Proud = Over; ProudAt = Q; }
				}
			}
		}
		std::printf("Duals: %d edges name another half (%d not on a ring); the ride's road %.0f m on a dual carriageway, its other half found across for %.0f m (reservation %.1f m on average)\n",
			Edges, Paired, DualM, FoundM, FoundM > 0.0 ? GapSum / FoundM : 0.0);
		std::printf("  the reservation's ground (up to MedianMost wide) at most %.2f m over the line between its edges (at %.0f, %.0f)\n", Proud, ProudAt.X, ProudAt.Z);
	}
	if (std::getenv("WORLD_RIDE_RINGS") != nullptr)
	{
		// [OSM stage 5B] The roundabouts the map marks, and how round each is.
		Plan->IndexRings();
		int Round = 0, Mini = 0;
		for (const RidePlan::RoadRing& Rg : Plan->RoadRings) { Round += Rg.Circle ? 1 : 0; Mini += Rg.Mini ? 1 : 0; }
		std::printf("Rings: %d (%d near a circle, %d mini)\n", int(Plan->RoadRings.size()), Round, Mini);
		// (the rings the ride goes round: how well its smoothed road keeps to each)
		for (size_t I = 0; I < Plan->RoadRings.size(); ++I)
		{
			const RidePlan::RoadRing& Rg = Plan->RoadRings[I];
			bool Ridden = false;
			for (const auto& St : Plan->MadeSteps)
				for (int E : Rg.Edges) Ridden = Ridden || St.Edge == E;
			if (!Ridden) continue;
			double Least = 1e9, Most = 0.0, Tightest = 1e9;
			int On = 0;
			for (size_t K = 2; K + 2 < Plan->Road.size(); ++K)
			{
				const double D = Plan->Road[K].At.DistanceTo(Rg.Mid);
				if (D > Rg.Radius + 12.0) continue;
				++On;
				Least = std::fmin(Least, D);
				Most = std::fmax(Most, D);
				// (the bend: the circle through this point and those two either side)
				const P2 A = Plan->Road[K - 2].At, B = Plan->Road[K].At, C = Plan->Road[K + 2].At;
				const double Ab = A.DistanceTo(B), Bc = B.DistanceTo(C), Ac = A.DistanceTo(C);
				const double Cross = std::fabs((B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X));
				if (Cross > 1e-9) Tightest = std::fmin(Tightest, Ab * Bc * Ac / (2.0 * Cross));
			}
			std::printf("  ridden ring %zu (radius %.1f%s): %d road knots within 12 m, %.1f to %.1f m from its middle, tightest bend %.1f m\n", I, Rg.Radius,
				Rg.Circle ? ", a circle" : "", On, Least, Most, Tightest);
		}
		for (const RidePlan::RideTurn& T : Plan->Turns)
			if (T.Exit > 0) std::printf("  sat nav: at %.0f m, roundabout, exit %d (turn %d) onto '%s'\n", T.Along, T.Exit, T.Turn, T.Onto.c_str());
		for (size_t I = 0; I < Plan->RoadRings.size() && I < 20; ++I)
		{
			const RidePlan::RoadRing& Rg = Plan->RoadRings[I];
			std::printf("  ring %zu at %.0f, %.0f: radius %.1f m, strays %.1f m, %zu edges, %zu points%s%s\n", I, Rg.Mid.X, Rg.Mid.Z, Rg.Radius, Rg.Stray, Rg.Edges.size(), Rg.Line.size(),
				Rg.Circle ? ", a circle" : "", Rg.Clockwise ? ", clockwise" : ", anticlockwise");
		}
	}
	if (const char* LinesAt = std::getenv("WORLD_RIDE_LINES_AT"))
	{
		// The walls, hedges and fences mapped within 80 m of lat,lon, and the roads' points there.
		double La = 0.0, Lo = 0.0;
		std::sscanf(LinesAt, "%lf,%lf", &La, &Lo);
		const Region& G = Plan->Area.Roads;
		const P2 C = Frame(G.FrameLat, G.FrameLon).FromGeo(La, Lo);
		for (const FeatureLine& F : G.Scenery.Lines)
		{
			bool Near = false;
			for (const P2& Q : F.Line) Near = Near || Q.DistanceTo(C) < 80.0;
			if (!Near) continue;
			std::printf("  line %s:", F.Kind.c_str());
			for (const P2& Q : F.Line) std::printf(" (%.1f %.1f)", Q.X - C.X, Q.Z - C.Z);
			std::printf("\n");
		}
		int Shown = 0;
		for (const FeatureLine& F : G.Scenery.Lines)
		{
			if (Shown >= 12 || (F.Kind != "hedge" && F.Kind != "wall")) continue;
			for (const RoadPoint& P : Plan->Road)
				if (P.At.DistanceTo(F.Line[0]) < 15.0) { std::printf("  %s near the ride at %.0f m\n", F.Kind.c_str(), P.Along); ++Shown; break; }
		}
		for (const RoadPoint& P : Plan->Road)
			if (P.At.DistanceTo(C) < 80.0) std::printf("  ride road %.1f %.1f at %.0f h %.1f half %.1f%s%s\n", P.At.X - C.X, P.At.Z - C.Z, P.Along, P.Height, P.Half, P.Bridge ? " bridge" : "", P.Tunnel ? " tunnel" : "");
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
		else if (std::getenv("WORLD_RIDE_DAM") != nullptr)
		{
			// [places stage 4] (or the nth dam)
			int Nth = std::atoi(std::getenv("WORLD_RIDE_DAM"));
			for (const RidePlan::WildLine& W : Plan->WildLines)
				if (W.Kind == 1 && Nth-- == 0)
				{
					Centre = W.Line[W.Line.size() / 2];
					double Long = 0.0;
					for (size_t I = 1; I < W.Line.size(); ++I) Long += W.Line[I].DistanceTo(W.Line[I - 1]);
					std::printf("dam %.0f m long at %.0f, %.0f\n", Long, Centre.X, Centre.Z);
					break;
				}
		}
		else if (std::getenv("WORLD_RIDE_RECIPE") != nullptr)
		{
			// [places stage 3] (or the nth place drawn by a recipe)
			const std::string Want = std::getenv("WORLD_RIDE_RECIPE");
			size_t Nth = size_t(std::atoi(Want.c_str()));
			if (!Want.empty() && (Want[0] < '0' || Want[0] > '9'))
				for (size_t I = 0; I < Plan->PlaceRecipes.size(); ++I)
					if (Plan->Area.Roads.Sights[size_t(Plan->PlaceRecipes[I].Sight)].Name.find(Want) != std::string::npos) { Nth = I; break; }
			if (Nth < Plan->PlaceRecipes.size())
			{
				Centre = Plan->PlaceRecipes[Nth].At;
				std::printf("recipe %d for '%s'\n", Plan->PlaceRecipes[Nth].Kind, Plan->Area.Roads.Sights[size_t(Plan->PlaceRecipes[Nth].Sight)].Name.c_str());
			}
		}
		else if (std::getenv("WORLD_RIDE_MESH_AT") != nullptr)
		{
			// (or a place: x,z in the ride's frame, its roads laid round it)
			std::sscanf(std::getenv("WORLD_RIDE_MESH_AT"), "%lf,%lf", &Centre.X, &Centre.Z);
			Rect Round;
			Round.X0 = Centre.X - 300.0; Round.Z0 = Centre.Z - 300.0; Round.X1 = Centre.X + 300.0; Round.Z1 = Centre.Z + 300.0;
			Plan->LayBlocks(Round);
		}
		else if (std::getenv("WORLD_RIDE_DUAL") != nullptr)
		{
			// [OSM stage 5C] (or the ride's road where it has gone WORLD_RIDE_DUAL m along its dual
			// carriageway, counting only the dual parts)
			double Want = std::atof(std::getenv("WORLD_RIDE_DUAL")), Run = 0.0;
			for (size_t I = 1; I < Plan->Road.size(); ++I)
			{
				if (!Plan->Road[I].Dual) continue;
				Run += Plan->Road[I].Along - Plan->Road[I - 1].Along;
				if (Run < Want) continue;
				Centre = Plan->Road[I].At;
				Rect Round;
				Round.X0 = Centre.X - 300.0; Round.Z0 = Centre.Z - 300.0; Round.X1 = Centre.X + 300.0; Round.Z1 = Centre.Z + 300.0;
				Plan->LayBlocks(Round);
				RidePlan::DualHit D;
				const bool Has = Plan->DualAcross(0, int(I), 0, D);
				std::printf("dual at %.0f, %.0f (%.0f m along): other half %s, side %d, road %d, reservation %.1f m, edges %.2f / %.2f m\n", Centre.X, Centre.Z, Plan->Road[I].Along,
					Has ? "found" : "NOT found", D.Side, D.Which, D.Gap, D.Near, D.Far);
				break;
			}
		}
		else if (std::getenv("WORLD_RIDE_RING") != nullptr)
		{
			// [OSM stage 5B] (or the nth roundabout the ride goes round)
			int Nth = std::atoi(std::getenv("WORLD_RIDE_RING"));
			for (const RidePlan::RoadRing& Rg : Plan->RoadRings)
			{
				bool Ridden = false;
				for (const auto& St : Plan->MadeSteps)
					for (int E : Rg.Edges) Ridden = Ridden || St.Edge == E;
				if (!Ridden || Nth-- > 0) continue;
				Rect Round;
				Round.X0 = Rg.Mid.X - 300.0; Round.Z0 = Rg.Mid.Z - 300.0; Round.X1 = Rg.Mid.X + 300.0; Round.Z1 = Rg.Mid.Z + 300.0;
				Plan->LayBlocks(Round);
				Centre = Rg.Mid;
				std::printf("ring at %.0f, %.0f, radius %.1f\n", Rg.Mid.X, Rg.Mid.Z, Rg.Radius);
				break;
			}
		}
		else if (std::getenv("WORLD_RIDE_PLACEPATH") != nullptr)
		{
			// [places stage 7] (or the nth stop with a path in to its place: centred halfway along it)
			int Nth = std::atoi(std::getenv("WORLD_RIDE_PLACEPATH"));
			for (const RidePlan::PlaceStop& S : Plan->PlaceStops)
			{
				if (!S.HasView || Nth-- > 0) continue;
				Rect Round;
				Round.X0 = S.At.X - 300.0; Round.Z0 = S.At.Z - 300.0; Round.X1 = S.At.X + 300.0; Round.Z1 = S.At.Z + 300.0;
				Plan->LayBlocks(Round);
				Centre = (S.Board + S.View) * 0.5;
				std::printf("place path for '%s', %.0f m long, view spot at %.0f, %.0f\n", Plan->Area.Roads.Sights[size_t(S.Sights[0])].Name.c_str(), S.Board.DistanceTo(S.View), S.View.X - Centre.X, S.View.Z - Centre.Z);
				break;
			}
		}
		else if (std::getenv("WORLD_RIDE_PULLOFF") != nullptr)
		{
			// [places stage 2] (or the nth pull-off made beside a laid road)
			int Nth = std::atoi(std::getenv("WORLD_RIDE_PULLOFF"));
			RidePlan::NearOpts Opts;
			for (const RidePlan::PlaceStop& S : Plan->PlaceStops)
			{
				RidePlan::NearHit Hit;
				if (S.How != RidePlan::PlaceMade || Nth-- > 0) continue;
				// (the network's roads round it laid, as the game lays them when a rider comes near)
				Rect Round;
				Round.X0 = S.At.X - 300.0; Round.Z0 = S.At.Z - 300.0; Round.X1 = S.At.X + 300.0; Round.Z1 = S.At.Z + 300.0;
				Plan->LayBlocks(Round);
				if (!Plan->NearestOf(S.RoadMid.X, S.RoadMid.Z, Opts, Hit)) Hit.Which = -1;
				Centre = S.At;
				std::printf("pull-off by road %d for '%s', depth %.1f m\n", Hit.Which, Plan->Area.Roads.Sights[size_t(S.Sights[0])].Name.c_str(), S.Depth);
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
			for (const RidePlan::Knot& K : LaidKnots(*Plan)) Spots.push_back(K.At);
			const P2 Start = Plan->Road[0].At;
			std::sort(Spots.begin(), Spots.end(), [&Start](const P2& A, const P2& B) { return A.DistanceTo(Start) < B.DistanceTo(Start); });
			const size_t Want = size_t(std::atoi(std::getenv("WORLD_RIDE_KNOT")));
			if (Want < Spots.size()) Centre = Spots[Want];
		}
		else
			for (const RidePlan::SideRoad& Sd : PlanSideRoads(*Plan))
				for (const RidePlan::Mouth& M : Sd.Mouths) if (!Found && M.Is && M.GiveWay) { Centre = M.At; Found = true; }
		std::string Text;
		char Buf[128];
		for (const int W : LaidRoads(*Plan))
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
		for (const int W : LaidRoads(*Plan))
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
