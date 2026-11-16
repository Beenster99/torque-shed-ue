// The stage 1 gate of Ride UK, run from a desk: the roads between two places fetched from the
// world's store, the way by road between them found, its ground fetched, and the road's height read
// all the way along, with the real network (through the curl on the path). The game runs the same
// check itself with -TorqueWorldTest (World/TorqueWorldSubsystem).
//
//   world_ride <folder to keep what is fetched> [latA lonA latB lonB]     (Hawes to Leyburn if none)
#include "CellStore.h"
#include "HeightTiles.h"
#include "WorldFiles.h"
#include "WorldRideSigns.h"
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
		std::string GlyphText;   // [OSM stage 6A]
		if (Files::Read(Files::Join(Files::Join(TORQUE_SIMTEST, ".."), "Data/RideUK/sign_glyphs.json"), GlyphText)) SetSignFont(GlyphText);
		std::string PlaceText;   // [OSM stage 6C]
		if (Files::Read(Files::Join(Files::Join(TORQUE_SIMTEST, ".."), "Data/World/uk_places.tsv"), PlaceText)) SetSignPlaces(PlaceText);
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
	// [fps plan step 2] WORLD_RIDE_FACADE=1: the houses' windows and doors as the facade kit's models, as
	// the game places them when it has them all.
	if (Plan && std::getenv("WORLD_RIDE_FACADE") != nullptr) Plan->FacadeKit = std::atoi(std::getenv("WORLD_RIDE_FACADE")) != 0;
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
					std::printf("  the network's furniture there: %zu in %.0f ms (lamps %d, chevrons %d, post boxes %d, bus stops %d, benches %d, cattle grids %d, traffic lights %d, beacons %d, warnings %d)\n",
						FurnHere.size(), FurnMs, ByKind[RidePlan::StreetLamp], ByKind[RidePlan::Chevron], ByKind[RidePlan::PostBox], ByKind[RidePlan::BusStop],
						ByKind[RidePlan::Seat], ByKind[RidePlan::CattleGrid], ByKind[RidePlan::TrafficSignal], ByKind[RidePlan::Beacon], ByKind[RidePlan::WarnSign]);
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
		// [waypoint] WORLD_RIDE_VIA=lat,lon[,name]: the sat nav's way by a waypoint from a quarter of the
		// way along, and how it says it as the rider goes (the info panel's click, beenster build 146).
		if (const char* ViaEnv = std::getenv("WORLD_RIDE_VIA"))
		{
			double VLat = 0.0, VLon = 0.0;
			char VName[64] = "the waypoint";
			if (std::sscanf(ViaEnv, "%lf,%lf,%63[^\n]", &VLat, &VLon, VName) >= 2 && Plan->Road.size() > 8)
			{
				const P2 V = Plan->Area.Roads.FrameOf().FromGeo(VLat, VLon);
				const RoadPoint& Rv = Plan->Road[Plan->Road.size() / 4];
				const RoadPoint& Rv2 = Plan->Road[Plan->Road.size() / 4 + 1];
				const double Hd = std::atan2(-(Rv2.At.X - Rv.At.X), -(Rv2.At.Z - Rv.At.Z));
				const auto ViaT0 = std::chrono::steady_clock::now();
				const std::shared_ptr<const RideDetour> Dv = Plan->DetourVia(Rv.At.X, Rv.At.Z, Hd, Rv.Along, V.X, V.Z, VName);
				const double ViaMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ViaT0).count();
				if (Dv == nullptr) std::printf("Waypoint %s: no way by it from %.2f km (%.1f ms)\n", VName, Rv.Along / 1000.0, ViaMs);
				else
				{
					std::printf("Waypoint %s at (%.0f, %.0f): from %.2f km of %.2f, %.2f km there and back to the ride's road at %.2f km; reached at %.2f km, %.0f m from the place; %d turns (%.1f ms)\n",
						VName, V.X, V.Z, Rv.Along / 1000.0, Plan->FinishM / 1000.0, Dv->Length / 1000.0, Dv->RejoinAlong / 1000.0, Dv->ViaAlong / 1000.0, Dv->Via.DistanceTo(V),
						int(Dv->Turns.size()), ViaMs);
					for (const RidePlan::RideTurn& Tn : Dv->Turns) std::printf("    at %.2f km turn %d onto %s\n", Tn.Along / 1000.0, Tn.Turn, Tn.Onto.c_str());
					for (const double Frac : {0.0, 0.25, 0.5, 0.75, 0.99})
					{
						const size_t I = std::min(Dv->Line.size() - 1, size_t(Frac * double(Dv->Line.size() - 1)));
						const RidePlan::RideNav Nv = Plan->NavDetour(*Dv, Dv->Line[I].X, Dv->Line[I].Z);
						std::printf("    nav at %.2f km of the way: turn %d in %.0f m onto %s; %.2f km to the finish%s\n", Dv->Along[I] / 1000.0, Nv.Turn, Nv.ToTurn, Nv.Onto.c_str(),
							Nv.ToFinish / 1000.0, Nv.OffRoute ? " (off route!)" : "");
					}
					std::printf("    the ride's own from there: %.2f km to the finish\n", (Plan->FinishM - Rv.Along) / 1000.0);
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
	if (const char* RailsNear = std::getenv("WORLD_RIDE_RAILS_NEAR"))
	{
		// (the railways within r m of the ride's start: each sample's place from the start, what it is on,
		// its rails' top, the land's own height and the ground the game draws under it; and how many
		// samples anywhere stand clear of the ground off a bridge)
		const double Reach = std::atof(RailsNear);
		const P2 Start = Plan->Road[0].At;
		Rect Round;
		Round.X0 = Start.X - Reach - 100.0; Round.Z0 = Start.Z - Reach - 100.0; Round.X1 = Start.X + Reach + 100.0; Round.Z1 = Start.Z + Reach + 100.0;
		Plan->LayBlocks(Round);
		int Floating = 0, Looked = 0;
		for (size_t L = 0; L < Plan->Rails.size(); ++L)
		{
			const RidePlan::RailLine& Run = Plan->Rails[L];
			for (size_t I = 0; I < Run.At.size(); ++I)
			{
				const P2 A = Run.At[I];
				if (A.DistanceTo(Start) > Reach) continue;
				const double G = Plan->Ground(A.X, A.Z), Lie = Plan->Land ? Plan->Land->Height(A.X, A.Z) : 0.0;
				// (and 4 m to its side: the bank it stands on)
				const P2 Next = Run.At[I + 1 < Run.At.size() ? I + 1 : I - 1];
				P2 D = Next - A;
				const double Len = std::fmax(1e-6, std::hypot(D.X, D.Z));
				D = D * (1.0 / Len);
				const double Gs = std::fmin(Plan->Ground(A.X + D.Z * 4.0, A.Z - D.X * 4.0), Plan->Ground(A.X - D.Z * 4.0, A.Z + D.X * 4.0));
				++Looked;
				const bool Hangs = (Run.On[I] == 0 || Run.On[I] == 3) && G < Run.Top[I] - 1.5;   // (a side that falls away has its retaining wall)
				Floating += Hangs ? 1 : 0;
				std::printf("  rail %zu.%zu at %.0f, %.0f: on %d top %.1f land %.1f ground %.1f side %.1f%s\n", L, I, A.X - Start.X, A.Z - Start.Z, int(Run.On[I]), Run.Top[I], Lie, G, Gs, Hangs ? "  FLOATING" : "");
			}
		}
		std::printf("Rails near the start: %d samples, %d floating off a bridge\n", Looked, Floating);
	}
	if (const char* TallNear = std::getenv("WORLD_RIDE_TALL"))
	{
		// (what stands more than 6 m over the ground within r m of the ride's start, by layer, in 8 m squares:
		// each square's top, its colour and how many triangles)
		const double Reach = std::atof(TallNear);
		const P2 Start = Plan->Road[0].At;
		std::map<std::string, std::map<std::pair<int, int>, std::array<double, 5>>> Seen;
		const std::function<void(const RideMesh*, const std::string&)> Walk = [&](const RideMesh* M, const std::string& Tag)
		{
			if (M == nullptr) return;
			for (size_t T = 0; T + 2 < M->Idx.size(); T += 3)
			{
				const uint32_t V = M->Idx[T];
				const double X = M->OriginX + M->Pos[V * 3], Y = M->Pos[V * 3 + 1], Z = M->OriginZ + M->Pos[V * 3 + 2];
				if (std::hypot(X - Start.X, Z - Start.Z) > Reach) continue;
				const double G = Plan->Ground(X, Z);
				if (std::isnan(G) || Y - G < (std::getenv("WORLD_RIDE_TALL_M") ? std::atof(std::getenv("WORLD_RIDE_TALL_M")) : 6.0)) continue;
				std::array<double, 5>& A = Seen[Tag][{int(std::floor((X - Start.X) / 8.0)), int(std::floor((Z - Start.Z) / 8.0))}];
				A[0] = std::fmax(A[0], Y - G);
				if (!M->Col.empty()) { A[1] = M->Col[V * 3]; A[2] = M->Col[V * 3 + 1]; A[3] = M->Col[V * 3 + 2]; }
				A[4] += 1.0;
			}
			Walk(M->Extra.get(), Tag + ".extra");
			Walk(M->Stone.get(), Tag + ".stone");
			Walk(M->Masonry.get(), Tag + ".masonry");
			Walk(M->Render.get(), Tag + ".render");
			Walk(M->Roofing.get(), Tag + ".roof");
			Walk(M->Brickwork.get(), Tag + ".brick");
		};
		for (double Tz = std::floor((Start.Z - Reach) / 256.0) * 256.0; Tz < Start.Z + Reach; Tz += 256.0)
			for (double Tx = std::floor((Start.X - Reach) / 256.0) * 256.0; Tx < Start.X + Reach; Tx += 256.0)
			{
				RideMesh LandHere;
				Plan->LandTile(Tx, Tz, 256.0, 64, LandHere);
				Walk(&LandHere, "land");
			}
		for (double Tz = std::floor((Start.Z - Reach) / 64.0) * 64.0; Tz < Start.Z + Reach; Tz += 64.0)
			for (double Tx = std::floor((Start.X - Reach) / 64.0) * 64.0; Tx < Start.X + Reach; Tx += 64.0)
			{
				RideMesh Stands;
				Plan->SceneryTile(Tx, Tz, 64.0, 2.0, Stands);
				Walk(&Stands, "scenery");
			}
		for (const RidePlan::TallThing& T : Plan->Talls)
			if (T.At.DistanceTo(Start) < Reach) std::printf("  tall thing kind %d at %.0f, %.0f: %.1f m high, radius %.1f\n", T.Kind, T.At.X - Start.X, T.At.Z - Start.Z, T.Height, T.Radius);
		for (const auto& [Tag, Squares] : Seen)
			for (const auto& [Key, A] : Squares)
				std::printf("  tall %s at %d, %d: %.1f m up, colour %.2f %.2f %.2f, %d triangles\n", Tag.c_str(), Key.first * 8, Key.second * 8, A[0], A[1], A[2], A[3], int(A[4]));
	}
	if (std::getenv("WORLD_RIDE_DRAW") != nullptr)
	{
		// (what the game draws standing at the ride's start, as ATorqueRideWorld::Wanted and UpdateRoad
		// choose it: every square of ground at its level and every piece of road within reach, its
		// triangles and sections by layer, and how long each level's squares take to make)
		const P2 Start = Plan->Road[0].At;
		const int Levels = 4;
		const double Finest = 256.0, Reach = 16000.0, RoadReach = 2500.0;
		const int CellsOf[Levels] = {64, 128, 64, 64};
		const double SplitWithin[Levels] = {0.0, 400.0, 1500.0, 6000.0};
		const auto SizeOf = [&](int Lv) { double Sz = Finest; for (int Q = 0; Q < Lv; ++Q) Sz *= 4.0; return Sz; };
		const auto Away = [&](int Lv, long long Kx, long long Kz)
		{
			const double Sz = SizeOf(Lv), Ax = double(Kx) * Sz, Az = double(Kz) * Sz;
			const double Dx = std::max({Ax - Start.X, 0.0, Start.X - (Ax + Sz)}), Dz = std::max({Az - Start.Z, 0.0, Start.Z - (Az + Sz)});
			return std::hypot(Dx, Dz);
		};
		std::vector<std::array<long long, 3>> Keys, Open;
		const double TopSize = SizeOf(Levels - 1);
		for (long long Kz = (long long)std::floor((Start.Z - Reach) / TopSize); Kz <= (long long)std::floor((Start.Z + Reach) / TopSize); ++Kz)
			for (long long Kx = (long long)std::floor((Start.X - Reach) / TopSize); Kx <= (long long)std::floor((Start.X + Reach) / TopSize); ++Kx)
				if (Away(Levels - 1, Kx, Kz) <= Reach) Open.push_back({Levels - 1, Kx, Kz});
		while (!Open.empty())
		{
			const std::array<long long, 3> Key = Open.back();
			Open.pop_back();
			if (Key[0] > 0 && Away(int(Key[0]), Key[1], Key[2]) < SplitWithin[Key[0]])
			{
				for (int Dz = 0; Dz < 4; ++Dz)
					for (int Dx = 0; Dx < 4; ++Dx) Open.push_back({Key[0] - 1, Key[1] * 4 + Dx, Key[2] * 4 + Dz});
			}
			else Keys.push_back(Key);
		}
		Rect Round;
		Round.X0 = Start.X - RoadReach - 600.0; Round.Z0 = Start.Z - RoadReach - 600.0; Round.X1 = Start.X + RoadReach + 600.0; Round.Z1 = Start.Z + RoadReach + 600.0;
		Plan->LayBlocks(Round);
		struct Tally { double Tris = 0.0, Verts = 0.0, Sections = 0.0; };
		std::map<std::string, Tally> ByLayer[Levels + 1];
		double Plants[Levels] = {0, 0, 0, 0}, Seconds[Levels] = {0, 0, 0, 0};
		int Squares[Levels] = {0, 0, 0, 0};
		std::map<int, int> PlantKinds;
		double LightsAt = 0.0, Animals = 0.0, PropsAt = 0.0;
		const std::function<void(const RideMesh*, const std::string&, int)> Count = [&](const RideMesh* M, const std::string& Tag, int Lv)
		{
			if (M == nullptr) return;
			if (!M->Idx.empty())
			{
				Tally& Tl = ByLayer[Lv][Tag];
				Tl.Tris += double(M->Idx.size() / 3);
				Tl.Verts += double(M->Vertices());
				Tl.Sections += 1.0;
			}
			Count(M->Extra.get(), Tag == "ground" ? "scenery" : Tag + ".extra", Lv);
			Count(M->Stone.get(), "walls (stone)", Lv);
			Count(M->Masonry.get(), "stone houses", Lv);
			Count(M->Render.get(), "rendered houses", Lv);
			Count(M->Roofing.get(), "roofs", Lv);
			Count(M->Brickwork.get(), "brick", Lv);
			Count(M->Water.get(), "water", Lv);
			Count(M->Night.get(), "night", Lv);
			Count(M->Signals.get(), "signals", Lv);
			Count(M->Signs.get(), "signs", Lv);
			Count(M->Glow.get(), "glow", Lv);
		};
		for (const std::array<long long, 3>& Key : Keys)
		{
			const int Lv = int(Key[0]);
			const double Sz = SizeOf(Lv);
			RideMesh Mesh;
			const auto Made = std::chrono::steady_clock::now();
			Plan->LandTile(double(Key[1]) * Sz, double(Key[2]) * Sz, Sz, CellsOf[Lv], Mesh);
			Seconds[Lv] += std::chrono::duration<double>(std::chrono::steady_clock::now() - Made).count();
			++Squares[Lv];
			Plants[Lv] += double(Mesh.Plants.size());
			for (const RidePlant& Pl : Mesh.Plants) ++PlantKinds[Pl.Kind];
			LightsAt += double(Mesh.Lights.size() / 3) + (Mesh.Extra ? double(Mesh.Extra->Lights.size() / 3) : 0.0);
			Animals += double(Mesh.Animals.size());
			PropsAt += double(Mesh.Props.size());
			Count(&Mesh, "ground", Lv);
		}
		// (WORLD_RIDE_DRAW_PARTS=1: the levels 0 and 1 squares' standing things by what draws them, and
		// how long each takes)
		if (std::getenv("WORLD_RIDE_DRAW_PARTS") != nullptr)
			for (int Lv = 0; Lv <= 1; ++Lv)
			{
				std::map<std::string, std::array<double, 2>> Parts;
				const auto Tris = [](const RideMesh& M)
				{
					double T = 0.0;
					const std::function<void(const RideMesh*)> Add = [&](const RideMesh* Mm)
					{
						if (!Mm) return;
						T += double(Mm->Idx.size() / 3);
						for (const RideMesh* Sub : {Mm->Extra.get(), Mm->Stone.get(), Mm->Masonry.get(), Mm->Render.get(), Mm->Roofing.get(), Mm->Brickwork.get(), Mm->Night.get(), Mm->Signals.get(), Mm->Signs.get()}) Add(Sub);
					};
					Add(&M);
					return T;
				};
				for (const std::array<long long, 3>& Key : Keys)
				{
					if (Key[0] != Lv) continue;
					const double Sz = SizeOf(Lv), Kx = double(Key[1]) * Sz, Kz = double(Key[2]) * Sz;
					const double StepHere = Sz / CellsOf[Lv];
					const int Detail = StepHere <= 4.5 ? 2 : 1;
					const auto Time = [&](const char* Name, const std::function<void(RideMesh&)>& Make)
					{
						RideMesh M;
						M.OriginX = Kx;
						M.OriginZ = Kz;
						const auto Began = std::chrono::steady_clock::now();
						Make(M);
						Parts[Name][0] += Tris(M);
						Parts[Name][1] += std::chrono::duration<double>(std::chrono::steady_clock::now() - Began).count();
					};
					Time("scenery (all)", [&](RideMesh& M) { Plan->SceneryTile(Kx, Kz, Sz, StepHere, M); });
					Time("buildings", [&](RideMesh& M)
					{
						RideMesh Washed, Houses, Roofs, Bricks;
						Plan->BuildingsTile(Kx, Kz, Sz, StepHere <= 4.5, Plan->LookHere(Kx + Sz * 0.5, Kz + Sz * 0.5), M, Washed, Houses, Roofs, Bricks);
						for (RideMesh* Sub : {&Washed, &Houses, &Roofs, &Bricks}) { M.Idx.insert(M.Idx.end(), Sub->Idx.begin(), Sub->Idx.end()); }
					});
					Time("skyline", [&](RideMesh& M) { Plan->SkylineTile(Kx, Kz, Sz, Detail, M); });
					Time("fields", [&](RideMesh& M) { Plan->FieldsTile(Kx, Kz, Sz, Detail, M); });
					Time("wild", [&](RideMesh& M) { Plan->WildTile(Kx, Kz, Sz, Detail, M); });
					Time("towers", [&](RideMesh& M) { Plan->TowersTile(Kx, Kz, Sz, Detail, M); });
					Time("places", [&](RideMesh& M) { Plan->PlacesTile(Kx, Kz, Sz, Detail, M); });
					Time("rings", [&](RideMesh& M) { Plan->RingsTile(Kx, Kz, Sz, Detail, M); });
				}
				for (const auto& [Name, A] : Parts) std::printf("Draw parts level %d: %-14s %9.0f triangles, %.2f s\n", Lv, Name.c_str(), A[0], A[1]);
			}
		int Pieces = 0;
		for (const int Which : Plan->RoadsNear(Round, false))
		{
			const std::vector<RoadPoint>& Pts = Plan->RoadOf(Which);
			const int Points = int(Pts.size());
			if (Points < 2) continue;
			for (int First = 0; First < Points - 1; First += 64)
			{
				const int Last = std::min(First + 64, Points - 1);
				const RoadPoint& Mid = Pts[size_t((First + Last) / 2)];
				if (Mid.At.DistanceTo(Start) >= RoadReach) continue;
				RideMesh Mesh;
				Plan->RoadPiece(Which, First, Last, Mesh);
				++Pieces;
				Count(&Mesh, "road", Levels);
			}
		}
		double AllTris = 0.0, AllSections = 0.0;
		for (int Lv = 0; Lv <= Levels; ++Lv)
		{
			if (Lv < Levels) std::printf("Draw level %d: %d squares of %.0f m, %.0f plants, made in %.2f s\n", Lv, Squares[Lv], SizeOf(Lv), Plants[Lv], Seconds[Lv]);
			else std::printf("Draw roads: %d pieces within %.0f m\n", Pieces, RoadReach);
			for (const auto& [Tag, Tl] : ByLayer[Lv])
			{
				std::printf("  %-18s %9.0f triangles %9.0f vertices %5.0f sections\n", Tag.c_str(), Tl.Tris, Tl.Verts, Tl.Sections);
				AllTris += Tl.Tris;
				AllSections += Tl.Sections;
			}
		}
		std::printf("Draw total: %.0f triangles in %.0f sections; %.0f real lights, %.0f animals, %.0f props\n", AllTris, AllSections, LightsAt, Animals, PropsAt);
		for (const auto& [Kind, Many] : PlantKinds) std::printf("  plants of kind %d: %d\n", Kind, Many);
	}
	if (const char* WaterAt = std::getenv("WORLD_RIDE_WATER"))
	{
		// (the water runs and lakes within r m of the ride's start: each run's points there with its level,
		// half width and the land's own height; each lake's level)
		const double Reach = std::atof(WaterAt);
		const P2 Start = Plan->Road[0].At;
		for (size_t Wr = 0; Wr < Plan->WaterRuns.size(); ++Wr)
		{
			const RidePlan::WaterRun& Run = Plan->WaterRuns[Wr];
			for (size_t I = 0; I < Run.At.size(); I += 3)
				if (Run.At[I].DistanceTo(Start) < Reach)
					std::printf("  water run %zu (kind %d) at %.0f, %.0f: level %.2f half %.1f depth %.1f ground %.2f land %.2f\n", Wr, Run.Kind, Run.At[I].X - Start.X, Run.At[I].Z - Start.Z, double(Run.Surface[I]), double(Run.Half[I]), double(Run.Depth[I]), Plan->Ground(Run.At[I].X, Run.At[I].Z),
						Plan->Land->Height(Run.At[I].X, Run.At[I].Z));
		}
		for (const RidePlan::FieldArea& F : Plan->FieldAreas)
			if (F.Kind == RidePlan::FieldReservoir && std::hypot((F.MinX + F.MaxX) * 0.5 - Start.X, (F.MinZ + F.MaxZ) * 0.5 - Start.Z) < Reach + 200.0)
				std::printf("  water area %s at %.0f, %.0f (%.0f x %.0f m): level %.2f\n", F.Flowing ? "flowing" : "still", (F.MinX + F.MaxX) * 0.5 - Start.X, (F.MinZ + F.MaxZ) * 0.5 - Start.Z, F.MaxX - F.MinX, F.MaxZ - F.MinZ, F.Level);
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
		// (the ride's road on a high embankment: a barrier along its nearside)
		double Banked = 0.0, Highest = -1e9;
		P2 BankAt;
		for (size_t I = 1; I < Plan->Road.size(); ++I)
		{
			const RoadPoint& P = Plan->Road[I];
			if (!P.Dual || P.Near == 0 || P.Bridge || P.Tunnel) continue;
			const P2 Dir = P.At - Plan->Road[I - 1].At;
			const double Len = Dir.Length();
			if (Len < 1e-9) continue;
			const P2 Out6 = P.At + P2(Dir.Z / Len, -Dir.X / Len) * (double(P.Near) * (P.Half + 6.0));
			const double Lie = Plan->Land->Height(Out6.X, Out6.Z);
			if (!std::isnan(Lie)) Highest = std::fmax(Highest, P.Height - Lie);
			if (!std::isnan(Lie) && P.Height - Lie > RidePlan::EmbankBarrier) { if (Banked == 0.0) BankAt = P.At; Banked += Len; }
		}
		std::printf("  the ride's road on an embankment over %.0f m high for %.0f m (first at %.0f, %.0f; at most %.1f m over the land)\n", RidePlan::EmbankBarrier, Banked, BankAt.X, BankAt.Z, Highest);
		// (the map's traffic lights and zebra crossings: how many say which way they face)
		{
			int Lights = 0, LightsFacing = 0, Zebras = 0, OnEdge = 0;
			for (const MappedPoint& Mp : G.RoadPoints)
			{
				if (Mp.Kind == "traffic_signals") { if (Lights < 3) std::printf("  a traffic light at %.1f, %.1f heading %.0f\n", Mp.At.X, Mp.At.Z, Mp.Heading); ++Lights; LightsFacing += Mp.Heading >= 0.0 ? 1 : 0; OnEdge += Mp.Edge >= 0 ? 1 : 0; }
				if (Mp.Kind == "crossing_zebra") { if (Zebras < 3) std::printf("  a zebra crossing at %.0f, %.0f\n", Mp.At.X, Mp.At.Z); ++Zebras; }
			}
			std::printf("  %d traffic lights (%d facing a way, %d on a road), %d zebra crossings\n", Lights, LightsFacing, OnEdge, Zebras);
			int Embanked = 0, Cut = 0, NoPark = 0, Bays = 0;
			double EmbankedM = 0.0, CutM = 0.0;
			for (size_t E = 0; E < G.Edges.size(); ++E)
				if (const RoadExtra* X = G.ExtraOf(int(E)))
				{
					for (int Kd = 0; Kd < 2; ++Kd)
						if (X->Has(Kd == 0 ? RoadExtra::Embankment : RoadExtra::Cutting) && G.Edges[E].Length > 60.0 && (Kd == 0 ? Embanked : Cut) < 3)
						{
							const P2& Mid = G.Edges[E].Line[G.Edges[E].Line.size() / 2];
							std::printf("  a mapped %s (%s, %.0f m) at %.0f, %.0f\n", Kd == 0 ? "embankment" : "cutting", G.ClassOf(int(E)).c_str(), G.Edges[E].Length, Mid.X, Mid.Z);
						}
					if (X->Has(RoadExtra::Embankment)) { ++Embanked; EmbankedM += G.Edges[E].Length; }
					if (X->Has(RoadExtra::Cutting)) { ++Cut; CutM += G.Edges[E].Length; }
					if (Bays == 0 && (X->ParkLeft == RoadExtra::ParkBays || X->ParkRight == RoadExtra::ParkBays))
					{
						const P2& Mid = G.Edges[E].Line[G.Edges[E].Line.size() / 2];
						std::printf("  mapped parking bays (%s) at %.0f, %.0f\n", G.ClassOf(int(E)).c_str(), Mid.X, Mid.Z);
					}
					for (int Pk : {X->ParkLeft, X->ParkRight}) { NoPark += Pk == RoadExtra::NoParking || Pk == RoadExtra::NoStopping ? 1 : 0; Bays += Pk == RoadExtra::ParkBays ? 1 : 0; }
				}
			std::printf("  mapped embankments %d (%.0f m), cuttings %d (%.0f m); kerbs mapped no parking %d, parking bays %d\n", Embanked, EmbankedM, Cut, CutM, NoPark, Bays);
			double BankedOnRide[3] = {0.0, 0.0, 0.0}, FirstAt[3] = {-1.0, -1.0, -1.0};
			for (size_t I = 1; I < Plan->Road.size(); ++I)
			{
				const int B = Plan->Road[I].Banked;
				BankedOnRide[B] += Plan->Road[I].Along - Plan->Road[I - 1].Along;
				if (FirstAt[B] < 0.0) FirstAt[B] = Plan->Road[I].Along;
			}
			std::printf("  the ride on a mapped embankment for %.0f m (first %.0f m along), in a cutting for %.0f m (first %.0f m along)\n", BankedOnRide[1], FirstAt[1], BankedOnRide[2], FirstAt[2]);
		}
		// (the map's crash barriers: where the first few are)
		for (size_t B = 0; B < G.Barriers.size() && B < 4; ++B)
			std::printf("  %s, %zu points, from %.0f, %.0f\n", G.Barriers[B].Kind.c_str(), G.Barriers[B].Pts.size(), G.Barriers[B].Pts[0].X, G.Barriers[B].Pts[0].Z);
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
	// [OSM stage 5E] WORLD_RIDE_SECTION=x,z: the ground across the road nearest there, against the land as it lies.
	// [build 149 smoke] WORLD_RIDE_ALONG=from,to (km): the ride's road points between, with their height,
	// grade, turn, tilt, width, bridges and the furniture near (the robot fell twice on the same spots).
	if (const char* AlongEnv = std::getenv("WORLD_RIDE_ALONG"))
	{
		double FromKm = 0.0, ToKm = 0.0;
		if (std::sscanf(AlongEnv, "%lf,%lf", &FromKm, &ToKm) == 2)
		{
			const std::vector<RoadPoint>& Rd = Plan->Road;
			std::printf("ride length %.2f km\n", Rd.empty() ? 0.0 : Rd.back().Along / 1000.0);
			for (size_t I = 1; I + 1 < Rd.size(); ++I)
			{
				if (Rd[I].Along < FromKm * 1000.0 || Rd[I].Along > ToKm * 1000.0) continue;
				const double D = Rd[I + 1].Along - Rd[I - 1].Along;
				const double Grade = (Rd[I + 1].Height - Rd[I - 1].Height) / std::fmax(D, 1e-6);
				const P2 A = Rd[I].At - Rd[I - 1].At, B = Rd[I + 1].At - Rd[I].At;
				const double Turn = std::atan2(A.X * B.Z - A.Z * B.X, A.X * B.X + A.Z * B.Z);
				const double Hump = Rd[I].Height - 0.5 * (Rd[I - 1].Height + Rd[I + 1].Height);
				const auto G = Plan->Area.Roads.FrameOf().ToGeo(Rd[I].At.X, Rd[I].At.Z);
				std::printf("  %.3f km  h %.2f  grade %+.3f  crest %+.3f  turn %+.3f  bank %+.3f  half %.2f%s%s  (%.5f, %.5f)\n", Rd[I].Along / 1000.0, Rd[I].Height, Grade, Hump, Turn, Rd[I].Bank, Rd[I].Half,
					Rd[I].Bridge ? " BRIDGE" : "", Rd[I].Tunnel ? " TUNNEL" : "", G.Lat, G.Lon);
			}
			for (const RidePlan::Furnish& F : Plan->Furniture)
			{
				RidePlan::NearHit H;
				RidePlan::NearOpts O;
				O.MainOnly = true;
				if (!Plan->NearestOf(F.At.X, F.At.Z, O, H) || H.Along < FromKm * 1000.0 || H.Along > ToKm * 1000.0 || H.Dist > H.Half + 1.0) continue;
				std::printf("  furniture kind %d at %.3f km, %.2f m off the middle (half %.2f)\n", F.Kind, H.Along / 1000.0, H.Dist, H.Half);
			}
		}
	}
	// [fps plan step 2] WORLD_RIDE_BUDGET=lat,lon: the triangles of the near square (256 m) there, by what
	// makes them: the scenery as drawn and each of its layers, the buildings and the furniture alone, the
	// roads' pieces through it and how much of them is paint.
	if (const char* BudgetAt = std::getenv("WORLD_RIDE_BUDGET"))
	{
		double La = 0.0, Lo = 0.0;
		if (std::sscanf(BudgetAt, "%lf,%lf", &La, &Lo) == 2)
		{
			const P2 C = Plan->Area.Roads.FrameOf().FromGeo(La, Lo);
			Rect Round;
			Round.X0 = C.X - 600.0; Round.Z0 = C.Z - 600.0; Round.X1 = C.X + 600.0; Round.Z1 = C.Z + 600.0;
			Plan->LayBlocks(Round);
			const double Bx0 = std::floor(C.X / 256.0) * 256.0, Bz0 = std::floor(C.Z / 256.0) * 256.0;
			const auto Tris = [](const RideMesh* M) { return M ? M->Idx.size() / 3 : size_t(0); };
			RideMesh Scen;
			Plan->SceneryTile(Bx0, Bz0, 256.0, 2.0, Scen);
			const RideMesh* E = Scen.Extra.get();
			size_t Sub = Tris(Scen.Signs.get()) + Tris(Scen.Night.get()) + Tris(Scen.Signals.get());
			std::printf("budget square %.0f,%.0f: scenery %zu tris in its own layer; stone %zu, masonry %zu, render %zu, roofing %zu, brick %zu, signs+night+signals %zu; props %zu\n",
				Bx0, Bz0, Tris(&Scen), Tris(E ? E->Stone.get() : nullptr), Tris(E ? E->Masonry.get() : nullptr), Tris(E ? E->Render.get() : nullptr), Tris(E ? E->Roofing.get() : nullptr),
				Tris(E ? E->Brickwork.get() : nullptr), Sub, Scen.Props.size());
			RideMesh BOut, BWash, BHouse, BRoof, BBrick;
			for (RideMesh* M : {&BOut, &BWash, &BHouse, &BRoof, &BBrick}) { M->OriginX = Bx0; M->OriginZ = Bz0; }
			Plan->BuildingsTile(Bx0, Bz0, 256.0, true, Plan->LookHere(Bx0 + 128.0, Bz0 + 128.0), BOut, BWash, BHouse, BRoof, BBrick);
			size_t KitTris = 0;
			for (const RideProp& P : BOut.Props) KitTris += P.Facade ? 1 : 0;
			std::printf("  buildings alone: plain/trim %zu, render %zu, stone %zu, roofs %zu, brick %zu; %zu facade kit models\n", Tris(&BOut), Tris(&BWash), Tris(&BHouse), Tris(&BRoof), Tris(&BBrick), KitTris);
			size_t RoadTris = 0, PaintTris = 0;
			for (const int W : LaidRoads(*Plan))
			{
				const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
				for (int First = 0; First < int(Pts.size()) - 1; First += 64)
				{
					const int Last = std::min(First + 64, int(Pts.size()) - 1);
					bool In = false;
					for (int I = First; I <= Last && !In; ++I) In = Pts[size_t(I)].At.X >= Bx0 && Pts[size_t(I)].At.X < Bx0 + 256.0 && Pts[size_t(I)].At.Z >= Bz0 && Pts[size_t(I)].At.Z < Bz0 + 256.0;
					if (!In) continue;
					RideMesh Piece;
					Plan->RoadPiece(W, First, Last, Piece);
					RoadTris += Tris(&Piece);
					for (size_t T = 0; T + 2 < Piece.Idx.size(); T += 3)
					{
						const uint32_t V = Piece.Idx[T];
						const float Cr = Piece.Col[V * 3], Cg = Piece.Col[V * 3 + 1], Cb = Piece.Col[V * 3 + 2];
						if (Cr > 0.3f && Cg > 0.2f && Cb < Cr * 0.7f) ++PaintTris;   // (yellow)
						else if (Cr > 0.3f && Cg > 0.3f && Cb > 0.3f) ++PaintTris;   // (white)
					}
				}
			}
			std::printf("  roads' pieces through it: %zu tris, %zu of them paint\n", RoadTris, PaintTris);
		}
	}
	// [build 150] WORLD_RIDE_PIECETIME=1: how long each road piece (64 knots) takes to make, the slowest listed.
	if (std::getenv("WORLD_RIDE_PIECETIME") != nullptr)
	{
		std::vector<std::pair<double, std::string>> Times;
		double Sum = 0.0, Check = 0.0;
		for (const int W : LaidRoads(*Plan))
		{
			const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
			for (int First = 0; First < int(Pts.size()) - 1; First += 64)
			{
				const int Last = std::min(First + 64, int(Pts.size()) - 1);
				RideMesh Piece;
				const auto PieceT0 = std::chrono::steady_clock::now();
				Plan->RoadPiece(W, First, Last, Piece);
				const double PieceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PieceT0).count();
				Sum += PieceMs;
				for (size_t V = 0; V < Piece.Pos.size(); ++V) Check += double(Piece.Pos[V]) * double((V % 7) + 1);
				char Line[200];
				std::snprintf(Line, sizeof(Line), "road %d knots %d-%d at %.0f,%.0f: %zu tris", W, First, Last, Pts[size_t(First)].At.X, Pts[size_t(First)].At.Z, Piece.Idx.size() / 3);
				Times.emplace_back(PieceMs, Line);
			}
		}
		std::sort(Times.begin(), Times.end(), [](const std::pair<double, std::string>& A, const std::pair<double, std::string>& B) { return A.first > B.first; });
		std::printf("road pieces: %zu, %.0f ms in all (check %.3f)\n", Times.size(), Sum, Check);
		for (size_t I = 0; I < Times.size() && I < 12; ++I) std::printf("  %.1f ms  %s\n", Times[I].first, Times[I].second.c_str());
		// (=2: then all of them again and again, for a profiler to sample)
		if (std::atoi(std::getenv("WORLD_RIDE_PIECETIME")) == 2)
			for (int Again = 0; Again < 1000; ++Again)
				for (const int W : LaidRoads(*Plan))
				{
					const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
					for (int First = 0; First < int(Pts.size()) - 1; First += 64)
					{
						RideMesh Piece;
						Plan->RoadPiece(W, First, std::min(First + 64, int(Pts.size()) - 1), Piece);
					}
				}
	}
	if (const char* Sect = std::getenv("WORLD_RIDE_SECTION"))
	{
		double Sx = 0.0, Sz = 0.0;
		if (std::sscanf(Sect, "%lf,%lf", &Sx, &Sz) == 2)
		{
			RideMesh Lay;
			Plan->LandTile(std::floor(Sx / 256.0) * 256.0, std::floor(Sz / 256.0) * 256.0, 256.0, 4, Lay);
			RidePlan::NearHit Hit;
			if (Plan->NearestOf(Sx, Sz, RidePlan::NearOpts(), Hit) && Hit.Index >= 0)
			{
				const std::vector<RoadPoint>& On = Plan->RoadOf(Hit.Which);
				const size_t K = std::min(size_t(Hit.Index), On.size() - 2);
				P2 Dir = On[K + 1].At - On[K].At;
				Dir = Dir * (1.0 / std::fmax(1e-9, Dir.Length()));
				const P2 Left(Dir.Z, -Dir.X);
				std::printf("Section across road %d (half %.1f, banked %d, parking %d, height %.2f):\n", Hit.Which, Hit.Half, Hit.Banked, int(On[size_t(Hit.Index)].Park), Hit.Height);
				for (double Across = -40.0; Across <= 40.0; Across += 2.0)
				{
					const P2 P = Hit.Foot + Left * Across;
					std::printf("  %+5.0f  ground %7.2f  land %7.2f\n", Across, Plan->Ground(P.X, P.Z), Plan->Land->Height(P.X, P.Z));
				}
			}
		}
	}
	// [build 149] WORLD_RIDE_GROWCHECK=lat,lon: the ride grown there (as the game grows it when a rider
	// roams near the edge of what was read), and how far its roads' heights moved from the old plan's,
	// for the roads already laid round that place (the game's pieces drawn before the growth).
	if (const char* GrowAt = std::getenv("WORLD_RIDE_GROWCHECK"))
	{
		double La = 0.0, Lo = 0.0;
		if (std::sscanf(GrowAt, "%lf,%lf", &La, &Lo) == 2)
		{
			const P2 C = Plan->Area.Roads.FrameOf().FromGeo(La, Lo);
			Rect Round;
			Round.X0 = C.X - 1500.0; Round.Z0 = C.Z - 1500.0; Round.X1 = C.X + 1500.0; Round.Z1 = C.Z + 1500.0;
			Plan->LayBlocks(Round);
			std::string Why;
			const std::shared_ptr<RidePlan> Grown = RidePlan::Grow(Cells, Heights, *Plan, C.X, C.Z, &Why);
			if (!Grown) std::printf("growcheck: not grown: %s\n", Why.c_str());
			else
			{
				int Roads = 0, Moved = 0, Points = 0, MovedPoints = 0;
				double Worst = 0.0;
				int WorstRoad = -1;
				for (const int W : Plan->RoadsNear(Round, false))
				{
					const std::vector<RoadPoint>& A = Plan->RoadOf(W);
					const std::vector<RoadPoint>& B = Grown->RoadOf(W);
					if (A.empty() || B.empty()) continue;
					++Roads;
					bool Any = A.size() != B.size();
					for (size_t K = 0; K < std::min(A.size(), B.size()); ++K)
					{
						++Points;
						const double D = std::fabs(A[K].Height - B[K].Height) + A[K].At.DistanceTo(B[K].At);
						if (D > 0.03) { Any = true; ++MovedPoints; }
						if (D > Worst) { Worst = D; WorstRoad = W; }
					}
					Moved += Any ? 1 : 0;
				}
				std::printf("growcheck: %zu cells to %zu; %d roads laid in both, %d moved (%d of %d points by over 3 cm; worst %.2f m on road %d), %zu redrawn by the game's rule\n",
					Plan->Area.Cells.size(), Grown->Area.Cells.size(), Roads, Moved, MovedPoints, Points, Worst, WorstRoad, Grown->RedrawRoads.size());
			}
		}
	}
	// [build 149] WORLD_RIDE_COVER=lat,lon,R: the land over the laid roads within R m of a place, at the finest
	// level (256 m squares, 4 m cells) and the middle one (1 km, 8 m): every road point whose land, as its
	// square's mesh draws it, stands over the road's surface (beenster, build 148: the road in slabs with
	// the ground between them, at Brierley Green).
	if (const char* Cover = std::getenv("WORLD_RIDE_COVER"))
	{
		double La = 0.0, Lo = 0.0, Reach = 150.0;
		if (std::sscanf(Cover, "%lf,%lf,%lf", &La, &Lo, &Reach) >= 2)
		{
			const P2 C = Plan->Area.Roads.FrameOf().FromGeo(La, Lo);
			Rect Round;
			Round.X0 = C.X - Reach - 300.0; Round.Z0 = C.Z - Reach - 300.0; Round.X1 = C.X + Reach + 300.0; Round.Z1 = C.Z + Reach + 300.0;
			Plan->LayBlocks(Round);
			for (int Level = 0; Level < 2; ++Level)
			{
				const double Size = Level == 0 ? 256.0 : 1024.0;
				const int Across = Level == 0 ? 64 : 128, N = Across + 1;
				std::map<std::pair<long long, long long>, std::shared_ptr<RideMesh>> Made;
				const auto LandAt = [&](double X, double Z)
				{
					const long long Sx = (long long)std::floor(X / Size), Sz = (long long)std::floor(Z / Size);
					std::shared_ptr<RideMesh>& M = Made[{Sx, Sz}];
					if (!M) { M = std::make_shared<RideMesh>(); Plan->LandTile(double(Sx) * Size, double(Sz) * Size, Size, Across, *M); }
					if (M->Vertices() < size_t(N) * N) return std::nan("");
					const double Step = Size / Across, U = (X - double(Sx) * Size) / Step, V = (Z - double(Sz) * Size) / Step;
					const int I = std::min(int(U), Across - 1), J = std::min(int(V), Across - 1);
					const double Tu = U - I, Tv = V - J;
					const auto Y = [&](int Ii, int Jj) { return double(M->Pos[(size_t(Jj) * N + Ii) * 3 + 1]); };
					const double A = Y(I, J), B = Y(I + 1, J), Cc = Y(I, J + 1), D = Y(I + 1, J + 1);
					return Tu + Tv <= 1.0 ? A + (B - A) * Tu + (Cc - A) * Tv : D + (Cc - D) * (1.0 - Tu) + (B - D) * (1.0 - Tv);
				};
				int Points = 0, Over = 0;
				double Worst = 0.0;
				P2 WorstAt;
				for (const int W : Plan->RoadsNear(Round, false))
				{
					const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
					for (size_t K = 0; K + 1 < Pts.size(); ++K)
					{
						const P2 Dir0 = Pts[K + 1].At - Pts[K].At;
						const double Len = Dir0.Length();
						if (Len < 1e-6) continue;
						const P2 Side(Dir0.Z / Len, -Dir0.X / Len);
						for (double T = 0.0; T < 1.0; T += 0.25)
						{
							const P2 P = Pts[K].At + Dir0 * T;
							if (P.DistanceTo(C) > Reach || Pts[K].Bridge || Pts[K].Tunnel) continue;
							const double Road = Pts[K].Height + (Pts[K + 1].Height - Pts[K].Height) * T;
							for (double Aside : {-0.7, 0.0, 0.7})
							{
								const P2 Q = P + Side * (Aside * Pts[K].Half);
								const double L = LandAt(Q.X, Q.Z);
								if (std::isnan(L)) continue;
								++Points;
								if (L > Road + 0.08) { ++Over; if (L - Road > Worst) { Worst = L - Road; WorstAt = Q; } }
							}
						}
					}
				}
				// (every road with a point within 12 m of the worst spot: which, how far, its height there)
				if (Worst > 0.0)
					for (const int W : Plan->RoadsNear(Round, false))
					{
						const std::vector<RoadPoint>& Pts = Plan->RoadOf(W);
						for (size_t K = 0; K < Pts.size(); ++K)
							if (Pts[K].At.DistanceTo(WorstAt) < 12.0)
							{
								std::printf("  near the worst: road %d point %zu of %zu, %.1f m off, height %.2f%s%s\n", W, K, Pts.size(), Pts[K].At.DistanceTo(WorstAt), Pts[K].Height,
									Pts[K].Bridge ? " bridge" : "", Pts[K].Tunnel ? " tunnel" : "");
								for (size_t Q = 0; Q < Pts.size(); ++Q)
									if (Pts[Q].At.DistanceTo(WorstAt) < 30.0) std::printf("      %zu: %.1f, %.1f along %.1f height %.2f ground %.2f land %.2f half %.1f\n", Q, Pts[Q].At.X - WorstAt.X, Pts[Q].At.Z - WorstAt.Z, Pts[Q].Along, Pts[Q].Height,
										Plan->Ground(Pts[Q].At.X, Pts[Q].At.Z), Plan->Land->Height(Pts[Q].At.X, Pts[Q].At.Z), Pts[Q].Half);
								break;
							}
					}
				const Geo G = Plan->Area.Roads.FrameOf().ToGeo(WorstAt.X, WorstAt.Z);
				std::printf("cover, %s level: %d of %d road points under the land (worst %.2f m at %.5f, %.5f)\n", Level == 0 ? "finest" : "middle", Over, Points, Worst, G.Lat, G.Lon);
			}
		}
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
			// (or a place: x,z in the ride's frame, its roads laid round it; "ll:lat,lon" a place on the map)
			const char* MeshAt = std::getenv("WORLD_RIDE_MESH_AT");
			double La = 0.0, Lo = 0.0;
			if (std::sscanf(MeshAt, "ll:%lf,%lf", &La, &Lo) == 2)
			{
				Centre = Plan->Area.Roads.FrameOf().FromGeo(La, Lo);
				std::printf("mesh at %.5f, %.5f: %.1f, %.1f in the frame\n", La, Lo, Centre.X, Centre.Z);
			}
			else std::sscanf(MeshAt, "%lf,%lf", &Centre.X, &Centre.Z);
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
		std::string Text, PropText;
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
		// (and with WORLD_RIDE_MESH_R, the scenery standing there: traffic lights lit as at WORLD_RIDE_SIGNALS seconds)
		if (std::getenv("WORLD_RIDE_MESH_R") != nullptr)
		{
			const double Round = std::atof(std::getenv("WORLD_RIDE_MESH_R"));
			const double Clock = std::getenv("WORLD_RIDE_SIGNALS") != nullptr ? std::atof(std::getenv("WORLD_RIDE_SIGNALS")) : 0.0;
			for (double Tz = std::floor((Centre.Z - Round) / 64.0) * 64.0; Tz < Centre.Z + Round; Tz += 64.0)
				for (double Tx = std::floor((Centre.X - Round) / 64.0) * 64.0; Tx < Centre.X + Round; Tx += 64.0)
				{
					RideMesh Stands;
					Plan->SceneryTile(Tx, Tz, 64.0, 2.0, Stands);
					// (the facade kit's models there, a line each, to WORLD_RIDE_MESH.props: name size x z y yaw, 9 colours)
					for (const RideProp& P : Stands.Props)
					{
						if (!P.Facade) continue;
						std::snprintf(Buf, sizeof(Buf), "%s %d %.3f %.3f %.3f %.4f", RidePlan::PropName(P.Kind), int(P.Variant), Stands.OriginX + P.X - Centre.X, Stands.OriginZ + P.Z - Centre.Z, double(P.Y), double(P.Yaw));
						PropText += Buf;
						for (const float Tc : P.Tint) { std::snprintf(Buf, sizeof(Buf), " %.3f", double(Tc)); PropText += Buf; }
						PropText += "\n";
					}
					const RideMesh* Extra = Stands.Extra.get();
					for (const RideMesh* Mesh : std::initializer_list<const RideMesh*>{&Stands, Extra, Stands.Signals.get(), Extra ? Extra->Signals.get() : nullptr, Stands.Signs.get()})
					{
						if (Mesh == nullptr) continue;
						const bool Lamps = Mesh == Stands.Signals.get() || (Extra && Mesh == Extra->Signals.get());
						for (size_t T = 0; T + 2 < Mesh->Idx.size(); T += 3)
						{
							bool In = false;
							for (int C = 0; C < 3; ++C) In = In || std::hypot(Mesh->OriginX + Mesh->Pos[Mesh->Idx[T + C] * 3] - Centre.X, Mesh->OriginZ + Mesh->Pos[Mesh->Idx[T + C] * 3 + 2] - Centre.Z) < Round;
							if (!In) continue;
							for (int C = 0; C < 3; ++C)
							{
								const uint32_t V = Mesh->Idx[T + C];
								double Cr = Mesh->Col[V * 3], Cg = Mesh->Col[V * 3 + 1], Cb = Mesh->Col[V * 3 + 2];
								if (Lamps && Mesh->Uv.size() >= V * 2 + 2)
								{
									// (as M_RideSignal lights them)
									const double Tc = std::fmod((Clock + Mesh->Uv[V * 2]) / RidePlan::SignalCycle, 1.0);
									const int Lamp = int(std::lround(Mesh->Uv[V * 2 + 1]));
									const bool Red = Tc < 0.55, Amber = (Tc >= 0.5 && Tc < 0.55) || Tc >= 0.95, Green = Tc >= 0.55 && Tc < 0.95;
									const bool On = Lamp == RidePlan::SignalRed ? Red : Lamp == RidePlan::SignalAmber ? Amber : Lamp == RidePlan::SignalGreen ? Green : std::fmod(Clock * 0.8, 1.0) < 0.5;
									const double K = On ? 1.0 : 0.15;
									Cr *= K; Cg *= K; Cb *= K;
								}
								std::snprintf(Buf, sizeof(Buf), "%.3f %.3f %.3f %.3f %.3f %.3f\n", Mesh->OriginX + Mesh->Pos[V * 3] - Centre.X, Mesh->OriginZ + Mesh->Pos[V * 3 + 2] - Centre.Z, double(Mesh->Pos[V * 3 + 1]), Cr, Cg, Cb);
								Text += Buf;
							}
						}
					}
				}
		}
		Files::Write(std::getenv("WORLD_RIDE_MESH"), Text);
		if (!PropText.empty()) Files::Write(std::string(std::getenv("WORLD_RIDE_MESH")) + ".props", PropText);
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
		{
			// [OSM stage 6B] the warnings by symbol, and the street name plates
			int BySym[TorqueWorld::SignPlate::SymKinds] = {};
			for (const RidePlan::Furnish& F : Plan->Furniture)
				if (F.Kind == RidePlan::WarnSign && F.Size >= 0.0 && F.Size < TorqueWorld::SignPlate::SymKinds) ++BySym[int(F.Size)];
			std::printf("Warning signs: %d (bends %d, double bends %d, hills %d, junctions %d, roundabouts %d, lights %d, humps %d, cattle grids %d); street name plates %d; direction signs %d before junctions, %d after; %d brown signs, %d fingerposts, %d countdown markers, %d boundary boards\n",
				Plan->FurnitureCount(RidePlan::WarnSign), BySym[0] + BySym[1], BySym[2] + BySym[3], BySym[10] + BySym[11], BySym[4] + BySym[5] + BySym[6] + BySym[7], BySym[8], BySym[9], BySym[12], BySym[14],
				Plan->FurnitureCount(RidePlan::StreetPlate), Plan->FurnitureCount(RidePlan::DirAdvance), Plan->FurnitureCount(RidePlan::DirConfirm), Plan->FurnitureCount(RidePlan::BrownFlag), Plan->FurnitureCount(RidePlan::Fingerpost), Plan->FurnitureCount(RidePlan::Countdown), Plan->FurnitureCount(RidePlan::BoundarySign));
			if (std::getenv("WORLD_RIDE_WARNINGS") != nullptr)
				for (const RidePlan::Furnish& F : Plan->Furniture)
					if (F.Kind == RidePlan::WarnSign || F.Kind == RidePlan::StreetPlate || F.Kind == RidePlan::DirAdvance || F.Kind == RidePlan::DirConfirm || F.Kind == RidePlan::BrownFlag || F.Kind == RidePlan::Fingerpost)
						std::printf("  %s %d %s at %.0f,%.0f facing %.2f,%.2f size %.2f\n", F.Kind == RidePlan::WarnSign ? "warn" : F.Kind == RidePlan::StreetPlate ? "plate" : F.Kind == RidePlan::DirAdvance ? "ads" : F.Kind == RidePlan::DirConfirm ? "confirm" : F.Kind == RidePlan::BrownFlag ? "brown" : "finger", int(F.Size), F.Text.c_str(), F.At.X, F.At.Z, F.Face.X, F.Face.Z, F.Big);
		}
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
