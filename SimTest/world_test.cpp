// Reading the real world (Ride UK stage 1): the grid and map-tile maths, the height tiles' pictures
// (WebP lossless and PNG, read by the game's own code), two cells of the world store stitched into
// one road graph, and the ground's height, each checked against what the Godot game answers on the
// same files (data/world/godot_world.json, written by Tools/make_godot_reference.py --world).
#include "CellStore.h"
#include "FitJson.h"
#include "HeightTiles.h"
#include "WorldFiles.h"
#include "WorldGeo.h"
#include "WorldImage.h"
#include "WorldInflate.h"
#include "WorldLoad.h"
#include "WorldRide.h"
#include "WorldRoadLine.h"
#include "WorldRegion.h"
#include "WorldSha256.h"
#include "WorldTerrain.h"
#include "WorldZip.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace TorqueWorld;
using TorqueFit::Json;

static int Fails = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Fails;
}
static bool Near(double A, double B, double Tol) { return std::fabs(A - B) <= Tol; }
static const std::string Data = TORQUE_WORLD_DATA;
static double Num(const Json& Row, size_t I) { return Row.A[I].N; }
// A height Godot wrote: null where it had none.
static bool SameHeight(const Json& Want, double Got, double Tol, double& Worst)
{
	if (Want.IsNull()) return std::isnan(Got);
	if (std::isnan(Got)) return false;
	Worst = std::fmax(Worst, std::fabs(Want.N - Got));
	return std::fabs(Want.N - Got) <= Tol;
}

int main()
{
	Json Ref;
	std::string Text;
	if (!Files::Read(Files::Join(Data, "godot_world.json"), Text) || !Json::Parse(Text, Ref))
	{
		std::printf("FAIL  no godot_world.json in %s\n", Data.c_str());
		return 1;
	}

	// ------------------------------------------------------------------ the grid and the map tiles' maths
	{
		const Json& G = *Ref.Get("geo");
		bool Ok = true;
		for (const Json& R : G.Get("metres")->A)
		{
			const MetresPerDeg M = MetresPerDegree(Num(R, 0));
			Ok = Ok && M.Lat == Num(R, 1) && M.Lon == Num(R, 2);
		}
		Check(Ok, "a degree's metres at six latitudes are Godot's to the last bit");
		Ok = true;
		for (const Json& R : G.Get("zoom_for")->A) Ok = Ok && ZoomFor(Num(R, 0), Num(R, 1)) == int(Num(R, 2));
		Check(Ok, "the zoom for a spacing is Godot's at every latitude");
		Ok = true;
		for (const Json& R : G.Get("cell_of")->A)
		{
			const CellId C = CellOf(Num(R, 0), Num(R, 1));
			const Geo Mid = CellMiddle(C);
			CellId Back;
			Ok = Ok && C.Row == int(Num(R, 2)) && C.Col == int(Num(R, 3)) && Near(Mid.Lat, Num(R, 4), 1e-9) && Near(Mid.Lon, Num(R, 5), 1e-9) && ParseCell(C.Name(), Back) && Back == C;
		}
		Check(Ok, "40 points fall in Godot's cells, and a cell's name reads back");
		Ok = true;
		for (const Json& R : G.Get("mercator")->A)
		{
			const int Zm = int(Num(R, 2));
			Ok = Ok && Near(MercatorX(Num(R, 1), Zm), Num(R, 3), 1e-9) && Near(MercatorY(Num(R, 0), Zm), Num(R, 4), 1e-9) && Near(LatOfRow(Num(R, 4), Zm), Num(R, 5), 1e-9);
		}
		Check(Ok, "map-tile positions and back are Godot's at zooms 9, 11 and 14");
		const Frame F(54.34, -2.04);
		const P2 P = F.FromGeo(54.4, -1.9);
		const Geo Back = F.ToGeo(P.X, P.Z);
		Check(Near(Back.Lat, 54.4, 1e-12) && Near(Back.Lon, -1.9, 1e-12) && P.X > 0.0 && P.Z < 0.0, "a frame puts north-east at +x, -z and reads back");
		Check(CellsInBox(54.3, -2.15, 54.45, -1.85).size() == 2 && CellsUnder(TileKey{9, 253, 163}).size() >= 4, "a box of degrees and a tile know the cells under them");
		CellId Junk;
		Check(!ParseCell("12_", Junk) && !ParseCell("a_b", Junk) && !ParseCell("900_3", Junk), "junk is no cell's name");
	}

	// ------------------------------------------------------------------ squeezed data and pictures
	{
		const std::string Plain = "Swaledale, Wensleydale, Swaledale, Wensleydale, Buttertubs, Buttertubs, Buttertubs.";
		// (zlib's own output for that line, level 9)
		static const unsigned char Z[] = {0x78, 0xda, 0x0b, 0x2e, 0x4f, 0xcc, 0x49, 0x4d, 0x01, 0x62, 0x1d, 0x85, 0xf0, 0xd4, 0xbc, 0xe2, 0x9c, 0xd4, 0x4a, 0x08, 0x27, 0x18, 0x87, 0xb8, 0x53, 0x69, 0x49, 0x49, 0x6a, 0x51, 0x49, 0x69, 0x52, 0x31, 0x2e, 0xb6, 0x1e, 0x00, 0x00, 0x29, 0x1e, 0xb1};
		std::string Out;
		const bool Ok = ZlibInflate(Z, sizeof(Z), Out);
		std::string Cut;
		Check(Ok && Out == Plain, "a zlib stream unpacks to its text");
		Check(!ZlibInflate(Z, sizeof(Z) - 9, Cut) && !ZlibInflate(Z, sizeof(Z), Cut, 10), "a cut-off stream, or one that would be too big, is refused");
		std::string Cover, Raw;
		Check(Files::Read(Files::Join(Data, "cells/c577_711/cover/0_0.covz"), Cover) && ZlibInflate(reinterpret_cast<const uint8_t*>(Cover.data()), Cover.size(), Raw)
			&& Raw.size() == 128 * 128, "a cell's cover tile unpacks to 128 x 128 squares");
		Json Pngs;
		bool All = Files::Read(Files::Join(Data, "png/png_check.json"), Text) && Json::Parse(Text, Pngs) && Pngs.Size() == 4;
		for (const Json& P : Pngs.A)
		{
			std::string Bytes;
			Picture Pic;
			All = All && Files::Read(Files::Join(Data, "png/" + P.Str("file")), Bytes) && DecodePicture(reinterpret_cast<const uint8_t*>(Bytes.data()), Bytes.size(), Pic)
				&& Pic.Width == int(P.Num("width")) && Sha256Hex(Pic.Rgb.data(), Pic.Rgb.size()) == P.Str("rgb");
		}
		Check(All, "PNGs of each kind (colour, with alpha, grey, palette; every row filter) read to the right pixels");
		Check(Sha256Hex(std::string("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
			&& Sha256Hex(std::string()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 gives the standard answers");
		Sha256 Parts;
		Parts.Update(std::string(100, 'a'));
		Parts.Update(std::string(29, 'a'));
		Parts.Update(std::string());
		Parts.Update(std::string(71, 'a'));
		Check(Parts.FinishHex() == Sha256Hex(std::string(200, 'a')), "and the same fed in pieces");
		Check(Sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d" && Sha1Hex(std::string()) == "da39a3ee5e6b4b0d3255bfef95601890afd80709"
			&& CellStore::TextHash("abc\n") == "a9993e364706", "SHA-1 gives the standard answers, and a small file's name hash leaves its last newline out");
		Check(Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u, "CRC-32 gives the standard answer");
	}

	// ------------------------------------------------------------------ every fixture tile, pixel for pixel
	HeightStore Store;
	Store.Dirs.push_back(Files::Join(Data, "heights"));
	{
		int Same = 0, SameFilled = 0, SameAt = 0, SameSea = 0, Total = 0, Filled = 0, WithSea = 0;
		const auto T0 = std::chrono::steady_clock::now();
		for (const Json& R : Ref.Get("tiles")->A)
		{
			++Total;
			const TileKey Key{int(R.Num("z")), int(R.Num("x")), int(R.Num("y"))};
			std::string Bytes;
			if (!Files::Read(Files::Join(Data, "heights/" + Key.Rel() + "." + R.Str("ext")), Bytes)) continue;
			const std::shared_ptr<HeightTile> A = Store.Decode(Bytes, &Key, false), B = Store.Decode(Bytes, &Key, true);
			if (A == nullptr || B == nullptr || A->Size != int(R.Num("size"))) continue;
			if (Sha256Hex(A->Rgb.data(), A->Rgb.size()) == R.Str("rgb")) ++Same;
			if (Sha256Hex(B->Rgb.data(), B->Rgb.size()) == R.Str("despiked")) ++SameFilled;
			if (R.Str("rgb") != R.Str("despiked")) ++Filled;
			if (A->Sea.empty() == !R.Flag("sea") && (A->Sea.empty() || Sha256Hex(A->Sea.data(), A->Sea.size()) == R.Str("sea_mask"))) ++SameSea;
			if (!A->Sea.empty()) ++WithSea;
			const Json& At = *R.Get("at");
			const int S = A->Size;
			const double Got[4] = {A->At(0, 0), A->At(S - 1, 0), A->At(17, 401), A->At(S - 1, S - 1)};
			bool Ok = true;
			for (size_t K = 0; K < 4; ++K) Ok = Ok && Got[K] == Num(At, K);
			if (Ok) ++SameAt;
		}
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
		std::printf("      %d tiles read twice in %.0f ms (%.1f ms a tile); %d have pits, %d have sea\n", Total, Ms, Ms / (2.0 * Total), Filled, WithSea);
		Check(Total >= 70 && Same == Total, "every height tile's pixels are Godot's, bit for bit (WebP lossless read by the game's own code)");
		Check(SameFilled == Total && Filled > 0, "with the pits filled they are still Godot's, bit for bit");
		Check(SameSea == Total && WithSea > 0, "which tiles have sea, and the sea's smoothed floor in each, are Godot's bit for bit");
		Check(SameAt == Total, "heights sampled from the tiles (the sea's floor among them) are exactly Godot's");
		Check(Store.Decode("not a picture", nullptr, false) == nullptr && Store.Decode(std::string(), nullptr, false) == nullptr, "junk is no tile");
	}

	// ------------------------------------------------------------------ two cells stitched into one region
	Region R;
	std::string Why;
	const bool Stitched = Region::NamedArea({Files::Join(Data, "cells")}, "swaledale", R, &Why);
	Check(Stitched, "the Swaledale area stitches from its two cells");
	if (!Stitched)
	{
		std::printf("      %s\n%d failed\n", Why.c_str(), Fails + 1);
		return 1;
	}
	{
		const Json& S = *Ref.Get("stitch");
		Check(R.Id == S.Str("id") && R.Hash == S.Str("hash") && R.Country == S.Str("country") && R.Sea == S.Flag("sea"), "its id, road hash and country are Godot's");
		Check(int(R.Nodes.size()) == int(S.Num("nodes")) && int(R.Edges.size()) == int(S.Num("edges")), "it has Godot's junctions and roads (7162 and 7500): the cells' shared roads joined once");
		Check(int(R.Names.size()) == int(S.Num("names")) && int(R.Bans.size()) == int(S.Num("bans")) && int(R.ComponentSize.size()) == int(S.Num("components")),
			"its road names, banned turns and connected pieces count as Godot's");
		Check(int(R.Buildings.size()) == int(S.Num("buildings")) && int(R.Pois.size()) == int(S.Num("pois")) && int(R.Landmarks.size()) == int(S.Num("landmarks"))
			&& int(R.Places.size()) == int(S.Num("places")), "its buildings, stops, landmarks and places count as Godot's (those on the shared edge kept once)");
		const Json& Bd = *S.Get("bounds");
		Check(Near(R.Bounds.X0, Num(Bd, 0), 0.01) && Near(R.Bounds.Z0, Num(Bd, 1), 0.01) && Near(R.Bounds.X1, Num(Bd, 2), 0.01) && Near(R.Bounds.Z1, Num(Bd, 3), 0.01), "its bounds are Godot's");
		Check(R.Classes.size() == S.Get("classes")->Size() && R.Surfaces.size() == 2 && R.Credits.size() == S.Get("credits")->Size()
			&& R.Credits.back() == S.Get("credits")->A.back().S, "its classes, surfaces and credits are Godot's");
		double Sx = 0.0, Sz = 0.0, Sl = 0.0;
		for (const P2& P : R.Nodes) { Sx += P.X; Sz += P.Z; }
		for (const RoadEdge& E : R.Edges) Sl += E.Length;
		Check(Sx == Num(*S.Get("node_sum"), 0) && Sz == Num(*S.Get("node_sum"), 1), "every junction is where Godot has it, to the last bit");
		Check(Near(Sl, S.Num("length_sum"), 0.5), "the roads' total length is Godot's (1,640 km) to half a metre");
		bool Ok = true;
		for (const Json& N : S.Get("some_nodes")->A)
		{
			const size_t I = size_t(Num(N, 0));
			Ok = Ok && R.Nodes[I].X == Num(N, 1) && R.Nodes[I].Z == Num(N, 2) && int(R.Adjacent[I].size()) == int(Num(N, 3)) && R.Component[I] == int(Num(N, 4));
		}
		Check(Ok, "sampled junctions have Godot's place, roads meeting and piece");
		Ok = true;
		for (const Json& E : S.Get("some_edges")->A)
		{
			const int Ei = int(Num(E, 0));
			const RoadEdge& G = R.Edges[size_t(Ei)];
			const P2 Mid = G.Line[G.Line.size() / 2];
			Ok = Ok && G.A == int(Num(E, 1)) && G.B == int(Num(E, 2)) && G.Class == int(Num(E, 3)) && G.Surface == int(Num(E, 4)) && G.Limit == int(Num(E, 5))
				&& G.Oneway == int(Num(E, 6)) && R.NameOf(Ei) == E.A[7].S && Near(G.Length, Num(E, 8), 1e-3) && int(G.Line.size()) == int(Num(E, 9)) && Mid.X == Num(E, 10)
				&& Mid.Z == Num(E, 11);
		}
		Check(Ok, "sampled roads have Godot's ends, class, surface, limit, direction, name, length and line");
		Ok = true;
		for (const Json& B : S.Get("some_buildings")->A)
		{
			const Building& G = R.Buildings[size_t(Num(B, 0))];
			Ok = Ok && Near(G.X, Num(B, 1), 1e-9) && Near(G.Z, Num(B, 2), 1e-9) && G.W == Num(B, 3) && G.D == Num(B, 4) && G.Yaw == Num(B, 5) && G.Height == Num(B, 6)
				&& G.More.size() == B.A.size() - 7 && (G.More.empty() || G.More[0] == Num(B, 7));
		}
		Check(Ok, "sampled buildings are Godot's, in Godot's order");
		Ok = true;
		for (size_t I = 0; I < S.Get("some_places")->A.size(); ++I)
		{
			const Json& P = S.Get("some_places")->A[I];
			Ok = Ok && R.Places[I].Str("name") == P.Str("name") && R.Places[I].Str("kind") == P.Str("kind") && Near(R.Places[I].Num("x"), P.Num("x"), 1e-9) && Near(R.Places[I].Num("z"), P.Num("z"), 1e-9);
		}
		Check(Ok, "the first places are Godot's (Angram, Appersett, Arkengarthdale...)");
		Ok = true;
		for (const Json& N : S.Get("nearest")->A) Ok = Ok && R.NearestNode(Num(N, 0), Num(N, 1)) == int(Num(N, 2)) && R.NearestConnected(Num(N, 0), Num(N, 1)) == int(Num(N, 3));
		Check(Ok, "the junction nearest 42 points, and the nearest on a real road network, are Godot's");
		Ok = true;
		for (size_t I = 0; I < S.Get("some_bans")->A.size(); ++I)
		{
			const Json& B = S.Get("some_bans")->A[I];
			Ok = Ok && R.Bans[I][0] == int(Num(B, 0)) && R.Bans[I][1] == int(Num(B, 1)) && R.Bans[I][2] == int(Num(B, 2)) && R.TurnBanned(R.Bans[I][0], R.Bans[I][1], R.Bans[I][2]);
		}
		Check(Ok && !R.TurnBanned(0, 0, 1), "the banned turns are Godot's");
		// (a one-way road only from its open end; a line read from either end)
		int OneWay = -1;
		for (size_t Ei = 0; Ei < R.Edges.size() && OneWay < 0; ++Ei)
			if (R.Edges[Ei].Oneway == 1) OneWay = int(Ei);
		const RoadEdge& E0 = R.Edges[0];
		Check(R.Passable(0, E0.A) && R.Passable(0, E0.B) && (OneWay < 0 || (R.Passable(OneWay, R.Edges[size_t(OneWay)].A) && !R.Passable(OneWay, R.Edges[size_t(OneWay)].B))),
			"a two-way road goes both ways, a one-way only from its open end");
		const std::vector<P2> Rev = R.LineFrom(0, E0.B);
		Check(R.OtherEnd(0, E0.A) == E0.B && Rev.front().X == E0.Line.back().X && Rev.back().Z == E0.Line.front().Z && R.ClassOf(0) == "track" && R.SurfaceOf(0) == "gravel",
			"a road's line reads from either end, and its class and surface have their names");
		Region None;
		Check(!Region::NamedArea({Files::Join(Data, "cells")}, "atlantis", None) && !Region::Stitch("x", "x", {Files::Join(Data, "nowhere")}, 0.0, 0.0, None), "an area the game hasn't got is refused");
	}

	// ------------------------------------------------------------------ the ground
	Terrain Land(Store, R.FrameLat, R.FrameLon);
	Land.SetBuildings(R.Buildings);
	{
		const Json& Zs = *Ref.Get("zooms");
		Check(Land.Zoom(0) == int(Num(Zs, 0)) && Land.Zoom(1) == int(Num(Zs, 1)) && Land.Zoom(2) == int(Num(Zs, 2)), "the three levels' zooms in the Dales are 14, 11 and 9");
		int Ok = 0, Total = 0;
		double Worst = 0.0;
		const auto T0 = std::chrono::steady_clock::now();
		for (const Json& H : Ref.Get("heights")->A)
		{
			++Total;
			const double X = Num(H, 0), Z = Num(H, 1);
			if (SameHeight(H.A[2], Land.Height(X, Z), 2e-3, Worst) && SameHeight(H.A[3], Land.HeightAtLevel(0, X, Z), 2e-3, Worst)
				&& SameHeight(H.A[4], Land.HeightAtLevel(1, X, Z), 2e-3, Worst) && SameHeight(H.A[5], Land.HeightAtLevel(2, X, Z), 2e-3, Worst))
				++Ok;
		}
		const double Us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - T0).count() / (4.0 * Total);
		std::printf("      %d points, four heights each: worst %.6f m from Godot; %.2f microseconds a height\n", Total, Worst, Us);
		Check(Total > 3000 && Ok == Total, "the ground at 3,779 points (1,279 of them by buildings, on their pads) is Godot's to 2 mm at every level");
		Check(std::isnan(Land.Height(400000.0, 0.0)) && std::isnan(Land.HeightAtLevel(7, 0.0, 0.0)), "there is no ground where there are no tiles");
	}
	{
		// A ride's own view: only the tiles within reach of its road, eased into the coarser level where they end.
		const Json& Rd = *Ref.Get("ride");
		const Json& Pins = *Rd.Get("road");
		const int N = int(Num(Pins, 4));
		std::vector<P2> Road;
		for (int I = 0; I <= N; ++I)
		{
			// (as Godot's Vector2.lerp makes them, in floats)
			const float T = float(double(I) / N), Ax = float(Num(Pins, 0)), Az = float(Num(Pins, 1)), Bx = float(Num(Pins, 2)), Bz = float(Num(Pins, 3));
			Road.push_back(P2(double(Ax + (Bx - Ax) * T), double(Az + (Bz - Az) * T)));
		}
		const std::shared_ptr<Terrain> Ride = Land.ForRoad(Road);
		const std::vector<TileKey> Keys = Ride->UsedKeys();
		bool Same = Keys.size() == Rd.Get("keys")->Size();
		for (size_t I = 0; Same && I < Keys.size(); ++I)
		{
			const Json& K = Rd.Get("keys")->A[I];
			Same = Keys[I] == TileKey{int(Num(K, 0)), int(Num(K, 1)), int(Num(K, 2))};
		}
		Check(Same && Ride->IsRide() && !Land.IsRide(), "a ride from Gunnerside to Grinton uses Godot's 26 tiles, in Godot's order");
		bool Boxes = true;
		for (const Json& B : Rd.Get("boxes")->A)
		{
			const Rect Box = Ride->KeyBox(TileKey{int(Num(B, 0)), int(Num(B, 1)), int(Num(B, 2))});
			// (Godot holds a tile's corners in degrees as floats, a third of a metre out up here; these are held in doubles)
			Boxes = Boxes && Near(Box.X0, Num(B, 3), 0.5) && Near(Box.Z0, Num(B, 4), 0.5) && Near(Box.X1, Num(B, 5), 0.5) && Near(Box.Z1, Num(B, 6), 0.5) && Box.X1 > Box.X0 && Box.Z1 > Box.Z0;
		}
		Check(Boxes, "each tile's box in the ride's frame is Godot's to within its own rounding");
		int Ok = 0, Total = 0, SeaOk = 0, Eased = 0;
		double Worst = 0.0;
		for (const Json& H : Rd.Get("heights")->A)
		{
			++Total;
			const double X = Num(H, 0), Z = Num(H, 1);
			const double Got = Ride->Height(X, Z);
			if (SameHeight(H.A[2], Got, 2e-3, Worst)) ++Ok;
			if (Ride->SeaAt(X, Z) == H.A[3].B) ++SeaOk;
			if (!std::isnan(Got) && std::fabs(Got - Land.Height(X, Z)) > 0.05) ++Eased;
		}
		std::printf("      %d points along the ride: worst %.6f m from Godot; %d differ from the map's own view (eased, or past the fine tiles)\n", Total, Worst, Eased);
		Check(Total == 2500 && Ok == Total, "the ride's ground at 2,500 points is Godot's to 2 mm, eased into the coarser levels where its tiles end");
		Check(SeaOk == Total, "none of it is sea, as Godot says");
		Check(Eased > 100, "away from the road the ride's ground is the coarser levels', not whatever fine tiles are on disk");
		bool Ready = true;
		for (const Json& Q : Rd.Get("ready")->A)
		{
			const Rect Area{Num(Q, 0), Num(Q, 1), Num(Q, 0) + Num(Q, 2), Num(Q, 1) + Num(Q, 2)};
			Ready = Ready && Ride->Ready(Area, 0) == Q.A[3].B && Ride->Ready(Area, 1) == Q.A[4].B && Ride->Ready(Area, 2) == Q.A[5].B;
		}
		Check(Ready && !Ride->Ready(Rect{0, 0, 10, 10}, 5), "whether the ground over 30 boxes is final at each level is as Godot says");
		// Two riders who planned the same road get the same ground, whatever else is on their disks.
		HeightStore Other;
		Other.Dirs.push_back(Files::Join(Data, "heights"));
		Terrain Land2(Other, R.FrameLat, R.FrameLon);
		Land2.SetBuildings(R.Buildings);
		const std::shared_ptr<Terrain> Ride2 = Land2.ForRoad(Road);
		bool Bitwise = true;
		for (size_t I = 0; I < 400; ++I)
		{
			const Json& H = Rd.Get("heights")->A[I];
			const double A = Ride->Height(Num(H, 0), Num(H, 1)), B = Ride2->Height(Num(H, 0), Num(H, 1));
			Bitwise = Bitwise && ((std::isnan(A) && std::isnan(B)) || A == B);
		}
		Check(Bitwise, "a second machine reading the same tiles gets the same ground to the last bit");
	}

	// ------------------------------------------------------------------ the store on disk
	{
		const std::string Tmp = Files::Join(TORQUE_WORLD_TMP, "world_test_store");
		HeightStore Mine;
		Mine.Disk = Tmp;
		const TileKey Fine{14, 8100, 5231}, Gone{14, 1, 1}, Coarse{9, 253, 163};
		Files::Remove(Files::Join(Tmp, Fine.Rel() + ".webp"));
		Files::Remove(Files::Join(Tmp, Gone.Rel() + ".none"));
		Mine.Reset();
		std::string Bytes;
		Check(!Mine.Has(Fine) && Mine.Tile(Fine) == nullptr, "an empty store has no tile");
		Check(Files::Read(Files::Join(Data, "heights/" + Fine.Rel() + ".webp"), Bytes) && !Mine.Store(Fine, "junk", false) && Mine.Store(Fine, Bytes, false) && Mine.Has(Fine)
			&& Mine.Tile(Fine) != nullptr && Mine.Tile(Fine)->Size == 512, "a fetched tile is kept on disk and read back; junk is refused");
		Mine.MarkNone(Gone);
		Check(Mine.Has(Gone) && Mine.IsNone(Gone) && Mine.Tile(Gone) == nullptr && !Mine.IsNone(Fine), "a tile Mapterhorn has none of is known as none: had, but no ground");
		Mine.MarkNone(Coarse);
		HeightStore Again;
		Again.Disk = Tmp;
		Check(Again.IsNone(Gone) && Again.Has(Fine) && !Again.IsNone(Coarse), "the next run remembers both; a coarse tile is never none (AWS has it)");
		Check(HeightStore::Url(Fine, false) == "https://tiles.mapterhorn.com/14/8100/5231.webp" && HeightStore::Url(Coarse, true) == "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/253/163.png",
			"tiles are asked for at Mapterhorn's and AWS's addresses");
		// Sea only where a cell's profile has it.
		Check(Store.SeaIn(CellId{577, 711}) == -1, "a cell's sea isn't known until its profile says");
		Store.KnowSea(CellId{577, 711}, false);
		Check(Store.SeaIn(CellId{577, 711}) == 0 && Store.SeaIn(CellId{577, 712}) == -1, "then it is");
		Files::Remove(Files::Join(Tmp, Fine.Rel() + ".webp"));
		Files::Remove(Files::Join(Tmp, Gone.Rel() + ".none"));
		Files::Remove(Files::Join(Tmp, Coarse.Rel() + ".none"));
	}

	// ------------------------------------------------------------------ the world's store: index, downloads, a cell's own files
	{
		const std::string Kept = Files::Join(TORQUE_WORLD_TMP, "world_test_cells");
		Files::RemoveTree(Kept);
		int Asked = 0;
		bool Swap = false, Down = false;
		// (the test's own server: the store's files as SimTest keeps them, data/world/store)
		const CellStore::Fetcher Serve = [&](const std::string& Url, std::string& Body) {
			++Asked;
			const std::string Scheme = "store://", Tiles = "https://tiles.mapterhorn.com/", Aws = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/";
			if (Down) return 0;
			// (and the two tile servers: Mapterhorn has the fixtures' tiles, AWS one coarse tile only)
			if (Url.compare(0, Tiles.size(), Tiles) == 0) return Files::Read(Files::Join(Files::Join(Data, "heights"), Url.substr(Tiles.size())), Body) ? 200 : 404;
			if (Url.compare(0, Aws.size(), Aws) == 0) return Url.substr(Aws.size()) == "9/1/1.png" && Files::Read(Files::Join(Data, "png/rgb.png"), Body) ? 200 : 404;
			if (Url.compare(0, Scheme.size(), Scheme) != 0) return 0;
			std::string Rel = Url.substr(Scheme.size());
			if (Swap && Rel == "cells/c577_712.zip") Rel = "cells/c577_711.zip";   // (the wrong cell's zip)
			return Files::Read(Files::Join(Files::Join(Data, "store"), Rel), Body) ? 200 : 404;
		};
		CellStore Cells;
		Cells.Dir = Kept;
		Cells.IndexUrl = "store://index.json";
		Cells.Get = Serve;
		const std::shared_ptr<const WorldIndex> Idx = Cells.Index();
		const CellId A{577, 711}, B{577, 712}, NotYet{577, 713}, Sea{570, 700}, Nowhere{10, 10};
		Check(Idx->Cells.size() == 4 && Idx->Format == 1 && Idx->Cell(A) != nullptr && Idx->Cell(A)->Hash == "a3e8c61cc7b8cb72" && Idx->Cell(A)->Bytes == 415902
			&& Idx->Streams.count("577_711") == 1 && Idx->Sketches.count("28_35") == 1, "the store's index reads: its cells, their hashes and their own files' hashes");
		Check(Cells.StateOf(A, *Idx) == CellStore::EState::Baked && Cells.StateOf(Sea, *Idx) == CellStore::EState::Sea
			&& Cells.StateOf(Nowhere, *Idx) == CellStore::EState::Unknown, "a cell is baked (to fetch), sea, or not baked yet");
		Check(Cells.Finishing({A, B, NotYet, Sea}, *Idx) == std::vector<CellId>{NotYet}, "a cell baked minutes ago, its profile still on its way, counts as being finished");
		const int Before = Asked;
		Check(Cells.Index() == Idx && Asked == Before, "the index isn't asked for again within a quarter of an hour");
		Check(Files::Exists(Files::Join(Kept, "index.json")) && Files::Exists(Files::Join(Kept, "areas.json")), "it is kept on disk, with the named areas");
		// Fetching cells.
		Swap = true;
		Why = Cells.FetchCells({B});
		Check(!Why.empty() && Why.find("isn't the region the index names") != std::string::npos && Cells.StateOf(B, *Idx) == CellStore::EState::Baked
			&& !Files::IsDir(Files::Join(Kept, "c577_712.part")), "a download that isn't the cell the index names is refused, and nothing of it is left");
		Swap = false;
		int Steps = 0;
		Why = Cells.FetchCells({A, B, Sea, Nowhere}, [&](int Done, int Total) { Steps += Done == Steps + 1 && Total == 2 ? 1 : 100; });
		Check(Why.empty() && Steps == 2 && Cells.StateOf(A, *Idx) == CellStore::EState::Here && Cells.StateOf(B, *Idx) == CellStore::EState::Here,
			"the two Dales cells are fetched from their zips (the real store's own), checked against their hashes and put in place");
		const Json& Ex = *Ref.Get("extras");
		Check(CellStore::HashOfDir(Files::Join(Kept, "c577_711")) == Ex.Get("hash_of_dir")->A[0].S && CellStore::HashOfDir(Files::Join(Data, "cells/c577_712")) == Ex.Get("hash_of_dir")->A[1].S
			&& Ex.Get("hash_of_dir")->A[1].S == Idx->Cell(B)->Hash, "a cell's folder hashes as Godot hashes it, which is what the store's index says");
		const int AfterCells = Asked;
		Check(Cells.FetchCells({A, B}).empty() && Asked == AfterCells, "cells already here aren't fetched again");
		Why = Cells.FetchCells({NotYet});
		Check(Why.find("HTTP 404") != std::string::npos, "a cell the store can't send says why");
		Check(CellStore::InstallZip("not a zip", "c1_1", "", Kept) == "The download isn't a region zip." && !Files::IsDir(Files::Join(Kept, "c1_1")), "junk is no zip");
		// A cell's own files.
		Json File;
		const int BeforeKept = Asked;
		Check(Cells.Kept(CellStore::EKind::Streams, "577_711", *Idx, File) && File.Get("streams") != nullptr && Asked == BeforeKept + 1 && Cells.Kept(CellStore::EKind::Streams, "577_711", *Idx, File)
			&& Asked == BeforeKept + 1, "a cell's streams are fetched once, then read from disk");
		Files::Write(Files::Join(Kept, "stream/c577_711.json"), "{\"streams\":[]}");
		Check(Cells.Kept(CellStore::EKind::Streams, "577_711", *Idx, File) && File.Get("streams")->Size() > 100 && Asked == BeforeKept + 2,
			"a kept file that isn't the one the index names is fetched again");
		Check(!Cells.Kept(CellStore::EKind::Trees, "577_713", *Idx, File) && !Cells.Kept(CellStore::EKind::Sketches, "1_1", *Idx, File), "a file the index doesn't name isn't asked for");
		// The whole area, put together to ride.
		LoadedArea Area;
		const bool Loaded = LoadArea(Cells, Store, {A, B}, 54.34, -2.04, "Swaledale", Area, &Why);
		Check(Loaded && Area.Roads.Hash == R.Hash && Area.Roads.Nodes.size() == R.Nodes.size() && Area.Roads.Edges.size() == R.Edges.size() && Area.Roads.Id == "area:@577_711,577_712"
			&& Area.Cells.size() == 2, "the area loads from the fetched cells: the same roads as from the fixtures");
		if (Loaded)
		{
			const Region& G = Area.Roads;
			const double Tol = 0.05;   // (Godot places these from a frame held in floats, a couple of centimetres out here)
			bool Ok = int(G.Spans.size()) == int(Ex.Num("spans"));
			for (const Json& Sp : Ex.Get("some_spans")->A)
			{
				const Span& Mine = G.Spans[size_t(Num(Sp, 0))];
				Ok = Ok && Mine.Kind == int(Num(Sp, 1)) && Near(Mine.A.X, Num(Sp, 2), Tol) && Near(Mine.A.Z, Num(Sp, 3), Tol) && Near(Mine.B.X, Num(Sp, 4), Tol) && Near(Mine.B.Z, Num(Sp, 5), Tol);
			}
			Check(Ok, "its 189 bridges and tunnels are where Godot has them");
			size_t StreamPoints = 0;
			for (const Stream& St : G.Streams) StreamPoints += St.Line.size();
			Ok = int(G.Streams.size()) == int(Ex.Num("streams")) && int(StreamPoints) == int(Ex.Num("stream_points"));
			for (const Json& St : Ex.Get("some_streams")->A)
			{
				const Stream& Mine = G.Streams[size_t(Num(St, 0))];
				Ok = Ok && Mine.Kind == int(Num(St, 1)) && int(Mine.Line.size()) == int(Num(St, 2)) && Near(Mine.Line.front().X, Num(St, 3), Tol) && Near(Mine.Line.front().Z, Num(St, 4), Tol)
					&& Near(Mine.Line.back().X, Num(St, 5), Tol) && Near(Mine.Line.back().Z, Num(St, 6), Tol);
			}
			Check(Ok, "its 3,779 rivers and streams (33,512 points) are Godot's");
			auto SameNum = [](const Json& Want, double Got) { return Want.IsNull() ? std::isnan(Got) : Want.N == Got; };
			Ok = int(G.Scenery.Points.size()) == int(Ex.Num("points")) && int(G.Scenery.Lines.size()) == int(Ex.Num("lines")) && int(G.Scenery.Areas.size()) == int(Ex.Num("areas"))
				&& int(G.Scenery.Buildings.size()) == int(Ex.Num("looks"));
			Check(Ok, "its scenery counts as Godot's: 7,323 points, 12,198 lines, 718 areas, 156 buildings' looks");
			Ok = true;
			for (const Json& P : Ex.Get("some_points")->A)
			{
				const FeaturePoint& Mine = G.Scenery.Points[size_t(Num(P, 0))];
				Ok = Ok && Mine.Kind == P.A[1].S && Near(Mine.At.X, Num(P, 2), Tol) && Near(Mine.At.Z, Num(P, 3), Tol) && SameNum(P.A[4], Mine.Height) && SameNum(P.A[5], Mine.Elevation)
					&& SameNum(P.A[6], Mine.Heading) && Mine.Name == P.A[7].S && Mine.Id == P.A[8].S;
			}
			for (const Json& L : Ex.Get("some_lines")->A)
			{
				const FeatureLine& Mine = G.Scenery.Lines[size_t(Num(L, 0))];
				Ok = Ok && Mine.Kind == L.A[1].S && int(Mine.Line.size()) == int(Num(L, 2)) && Near(Mine.Line.front().X, Num(L, 3), Tol) && Near(Mine.Line.front().Z, Num(L, 4), Tol)
					&& Near(Mine.Line.back().X, Num(L, 5), Tol) && Near(Mine.Line.back().Z, Num(L, 6), Tol) && SameNum(L.A[7], Mine.Height) && SameNum(L.A[8], Mine.Width)
					&& Mine.Flags == int(Num(L, 9)) && Mine.Name == L.A[10].S && Mine.Id == L.A[11].S;
			}
			int Towers = 0;
			for (const Json& Ar : Ex.Get("some_areas")->A)
			{
				const FeatureArea& Mine = G.Scenery.Areas[size_t(Num(Ar, 0))];
				Ok = Ok && Mine.Kind == Ar.A[1].S && int(Mine.Ring.size()) == int(Num(Ar, 2)) && Near(Mine.Ring.front().X, Num(Ar, 3), Tol) && Near(Mine.Ring.front().Z, Num(Ar, 4), Tol)
					&& Near(Mine.Ring.back().X, Num(Ar, 5), Tol) && Near(Mine.Ring.back().Z, Num(Ar, 6), Tol) && SameNum(Ar.A[7], Mine.Height) && SameNum(Ar.A[8], Mine.Elevation)
					&& Mine.Name == Ar.A[9].S && Mine.Id == Ar.A[10].S && Mine.Group == Ar.A[11].S;
				if (Mine.Kind == "tower") ++Towers;
			}
			for (const Json& B2 : Ex.Get("some_looks")->A)
			{
				const BuildingLook& Mine = G.Scenery.Buildings[size_t(Num(B2, 0))];
				Ok = Ok && Near(Mine.At.X, Num(B2, 1), Tol) && Near(Mine.At.Z, Num(B2, 2), Tol);
				for (size_t K = 0; K < 6; ++K) Ok = Ok && Mine.Words[K] == B2.A[3 + K].S;
			}
			Check(Ok && Towers > 0, "sampled points, lines, areas, towers and buildings' looks are Godot's: kind, place, name, id and numbers");
			int CoverOk = 0, TreesOk = 0, Total = 0, Wooded = 0;
			for (const Json& L : Ex.Get("land")->A)
			{
				++Total;
				if (Area.Land->Cover(Num(L, 0), Num(L, 1)) == int(Num(L, 2))) ++CoverOk;
				const int Trees = Area.Land->TreesAt(Num(L, 0), Num(L, 1));
				if (Trees == int(Num(L, 3))) ++TreesOk;
				if (Trees > 0) ++Wooded;
			}
			Check(Total == 2500 && CoverOk == Total, "what covers the ground at 2,500 points (moor, grass, wood, water...) is Godot's");
			Check(TreesOk == Total && Wooded > 100 && Area.Land->HasTrees(), "the trees there (canopy height, how much of the square, leaf) are Godot's");
			Check(std::string(Terrain::CoverName(Terrain::CoverWater)) == "water" && Area.Land->Cover(9e6, 0.0) == -1 && Area.Land->TreesAt(9e6, 0.0) == -1, "off the cells there is no cover and no trees");
			Check(Store.SeaIn(B) == 0 && Area.FeaturesHash.size() == 12 && Near(Area.Land->Height(-2422.0, -4363.0), Land.Height(-2422.0, -4363.0), 1e-9),
				"the cells' profiles say they have no sea; the scenery has its own hash; the ground is the same ground");
		}
		// A ride by road, and the ground fetched for it.
		if (Loaded)
		{
			const Region& G = Area.Roads;
			const int From = G.NearestConnected(-2422.0, -4363.0), To = G.NearestConnected(7187.0, -4599.0);
			const std::vector<int> Path = G.ShortestPath(From, To);
			const std::vector<P2> Line = G.PathLine(From, Path);
			double Len = 0.0;
			bool Joined = !Path.empty(), Legal = true;
			int Node = From;
			for (size_t I = 0; I < Path.size(); ++I)
			{
				Len += G.Edges[size_t(Path[I])].Length;
				Joined = Joined && (G.Edges[size_t(Path[I])].A == Node || G.Edges[size_t(Path[I])].B == Node);
				Legal = Legal && G.Passable(Path[I], Node) && (I == 0 || !G.TurnBanned(Path[I - 1], Node, Path[I]));
				Node = G.OtherEnd(Path[I], Node);
			}
			const double Straight = G.Nodes[size_t(From)].DistanceTo(G.Nodes[size_t(To)]);
			std::printf("      Gunnerside to Grinton by road: %.1f km over %d roads (%.1f km as the crow flies)\n", Len / 1000.0, int(Path.size()), Straight / 1000.0);
			Check(Joined && Legal && Node == To && Len > Straight && Len < Straight * 1.6, "the shortest way by road from Gunnerside to Grinton joins up, keeps to the road rules and is a sensible length");
			std::vector<double> Along;
			const std::vector<P2> Road = Resample(Line, 30.0, &Along);
			bool Even = !Road.empty() && Near(Along.back(), Len, 1.0) && Line.front().X == G.Nodes[size_t(From)].X && Line.back().Z == G.Nodes[size_t(To)].Z;
			for (size_t I = 1; I < Road.size(); ++I) Even = Even && Road[I].DistanceTo(Road[I - 1]) <= 30.0 + 1e-6;
			Check(Even && G.ShortestPath(From, From).empty() && G.ShortestPath(-1, To).empty(), "its line runs junction to junction, with a point at least every 30 m");
			// The ground for it, fetched into an empty store.
			const std::string TileDir = Files::Join(TORQUE_WORLD_TMP, "world_test_tiles");
			Files::RemoveTree(TileDir);
			HeightStore Net;
			Net.Disk = TileDir;
			Terrain NetLand(Net, 54.34, -2.04);
			NetLand.SetBuildings(G.Buildings);
			const std::shared_ptr<Terrain> NetRide = NetLand.ForRoad(Road);
			const std::shared_ptr<Terrain> FixRide = Area.Land->ForRoad(Road);
			const size_t Keys = NetRide->UsedKeys().size();
			int Said = 0;
			const int BeforeTiles = Asked;
			const int Failed = FetchGround(Cells, Net, *NetRide, [&](int Done, int Total) { if (Done >= 1 && Done <= Total && Total == int(Keys)) ++Said; });
			Check(Failed == 0 && Said == int(Keys) && Asked - BeforeTiles >= int(Keys) && NetRide->Ready(NetRide->KeyBox(NetRide->UsedKeys()[0]), 0),
				"the ride's tiles are all fetched (six at a time) before it starts, each said as it comes");
			Check(Net.SeaIn(A) == 0 && !NetRide->CellsUnderRide().empty(), "and the profiles of the cells under it were read first, for their sea");
			int Same = 0, Finite = 0;
			double Low = 1e9, High = -1e9;
			for (const P2& P : Road)
			{
				const double H = NetRide->Height(P.X, P.Z), H2 = FixRide->Height(P.X, P.Z);
				if (!std::isnan(H)) { ++Finite; Low = std::fmin(Low, H); High = std::fmax(High, H); }
				if (H == H2) ++Same;
			}
			std::printf("      the road's ground: %d points, %.0f m to %.0f m above the sea\n", int(Road.size()), Low, High);
			Check(Finite == int(Road.size()) && Same == Finite && Low > 150.0 && High < 400.0, "the road has ground all the way, the same from fetched tiles as from the game's own");
			const int BeforeAgain = Asked;
			Check(FetchGround(Cells, Net, *NetRide) == 0 && Asked == BeforeAgain, "nothing is fetched twice");
			// What the servers haven't got.
			const TileKey NoFine{14, 5, 5}, FromAws{9, 1, 1}, NoCoarse{11, 7, 7};
			Check(Net.Fetch({NoFine, FromAws, NoCoarse}, Cells.Get, 2) == 1 && Net.IsNone(NoFine) && Net.Has(NoFine) && Net.Tile(NoFine) == nullptr && Net.Tile(FromAws) != nullptr
				&& Net.Tile(FromAws)->Size == 64 && !Net.Has(NoCoarse) && Files::Exists(Files::Join(TileDir, "9/1/1.png")),
				"a fine tile Mapterhorn has none of is marked none; a coarse one comes from AWS instead; one neither has is a failure");
			Check(Net.Fetch({NoFine}, nullptr) == 0 && Net.Fetch({NoCoarse}, nullptr) == 1, "with no network, only what is missing counts as not got");
			// The whole of it in one go, as the game's own check does it (-TorqueWorldTest).
			{
				HeightStore Net2;
				Net2.Disk = TileDir;
				const Geo GA = G.FrameOf().ToGeo(G.Nodes[size_t(From)].X, G.Nodes[size_t(From)].Z), GB = G.FrameOf().ToGeo(G.Nodes[size_t(To)].X, G.Nodes[size_t(To)].Z);
				int Said2 = 0;
				const RideCheck Rc = CheckRide(Cells, Net2, GA.Lat, GA.Lon, GB.Lat, GB.Lon, [&Said2](const std::string&) { ++Said2; });
				Json Back;
				Check(Rc.Ok && Rc.Why.empty() && Rc.Cells == 2 && Near(Rc.Km, Len / 1000.0, 0.01) && Rc.Points == Rc.WithGround && Rc.Points == int(Road.size()) && Rc.TilesFailed == 0
					&& Rc.Low > 150.0 && Rc.High < 400.0 && Rc.Climb > 20.0 && Rc.Steepest < 0.5 && Said2 == 4 && !Rc.Names.empty() && Rc.Credits.size() >= 3
					&& Json::Parse(Rc.Dump(), Back) && Back.Flag("ok") && Near(Back.Num("km"), Rc.Km, 1e-3),
					"the ride check does all of it from two places on the map: cells, the way by road, its ground, the road's heights");
				const RideCheck AtSea = CheckRide(Cells, Net2, 10.0, -30.0, 10.1, -30.0);
				Check(!AtSea.Ok && !AtSea.Why.empty(), "and says why not for a ride in the middle of the sea");
				// The ride itself (stage 2): its road laid, the ground cut to it, both as meshes.
				const std::shared_ptr<RidePlan> Plan = RidePlan::Make(Cells, Net2, GA.Lat, GA.Lon, GB.Lat, GB.Lon);
				Check(Plan->Ok && Plan->Why.empty() && Plan->Road.size() > 100 && Plan->Length > Len * 0.9 && Plan->Length < Len * 1.5 && Near(Plan->Road.back().Along, Plan->Length, 1e-9),
					"a ride is planned between the same two places: a road of its own, the quick way, about as long as the shortest");
				if (Plan->Ok)
				{
					double Gap = 0.0, Step = 0.0, Off = 0.0, Under = 0.0, Kink = 0.0;
					int OnRoad = 0;
					for (size_t I = 0; I < Plan->Road.size(); ++I)
					{
						const RoadPoint& P = Plan->Road[I];
						if (I > 0)
						{
							const double D = P.At.DistanceTo(Plan->Road[I - 1].At);
							Gap = std::fmax(Gap, D);
							if (D > 0.5) Step = std::fmax(Step, std::fabs(P.Height - Plan->Road[I - 1].Height) / D);
						}
						if (I > 0 && I + 1 < Plan->Road.size())
							Kink = std::fmax(Kink, std::fabs(Plan->Road[I + 1].Height - 2.0 * P.Height + Plan->Road[I - 1].Height));
						Off = std::fmax(Off, std::fabs(P.Height - Plan->Land->Height(P.At.X, P.At.Z)));
						const double Rh = Plan->RoadHeight(P.At.X, P.At.Z);
						if (!std::isnan(Rh) && std::fabs(Rh - P.Height) < 0.05) ++OnRoad;
						if (!P.Bridge) Under = std::fmax(Under, std::fabs(Plan->Ground(P.At.X, P.At.Z) - (Rh - RidePlan::Sink)));
						else if (I > 0 && I + 1 < Plan->Road.size() && Plan->Road[I - 1].Bridge && Plan->Road[I + 1].Bridge && !(Plan->Ground(P.At.X, P.At.Z) <= Rh - 1.5 + 1e-9)) Under = 1e9;   // (under a deck the land is well clear of it)
					}
					std::printf("      the ride's road: %d points, %.2f m apart at most, steepest %.3f, sharpest crest %.3f m, %.2f m off the land at most\n",
						int(Plan->Road.size()), Gap, Step, Kink, Off);
					Check(Gap <= 4.0 + 1e-6 && Step < 0.35 && Kink < 0.08 && Off < 6.0, "its points are 4 m apart at most, its height smooth, and never far off the land");
					Check(OnRoad == int(Plan->Road.size()) && Under < 0.02, "the road's height is read back along its middle, and the ground lies just under it");
					// Away from the road the ground is the land's own; beside it, the road's bench.
					const RoadPoint& Mid = Plan->Road[Plan->Road.size() / 2];
					const RoadPoint& Nx = Plan->Road[Plan->Road.size() / 2 + 1];
					P2 Dir = Nx.At - Mid.At;
					Dir = Dir * (1.0 / Dir.Length());
					const P2 Left(Dir.Z, -Dir.X);
					const P2 Far = Mid.At + Left * 200.0, Beside = Mid.At + Left * (Mid.Half + 2.0), Edge = Mid.At + Left * (Mid.Half - 0.5), Verge = Mid.At + Left * (Mid.Half + 0.6);
					double Dn = 0.0, Hn = 0.0;
					const bool FarIsFar = !Plan->Nearest(Far.X, Far.Z, Dn, Hn);
					Check((!FarIsFar || Plan->Ground(Far.X, Far.Z) == Plan->Land->Height(Far.X, Far.Z)) && std::fabs(Plan->Ground(Beside.X, Beside.Z) - (Mid.Height + RoadLine::CrossHeight(Mid.Half, Mid.Half, Mid.Bank) - RidePlan::Sink)) < 0.3
						&& !std::isnan(Plan->RoadHeight(Edge.X, Edge.Z)) && std::isnan(Plan->RoadHeight(Verge.X, Verge.Z)),
						"the bench beside the road is level with it, the road ends at its edge, and far from it the land lies as it did");
					// A square of ground and a piece of road as meshes.
					RideMesh Tile, Piece;
					const double X0 = std::floor(Mid.At.X / 256.0) * 256.0, Z0 = std::floor(Mid.At.Z / 256.0) * 256.0;
					Plan->LandTile(X0, Z0, 256.0, 64, Tile);
					bool Sound = Tile.Vertices() == size_t(65 * 65 + 4 * 65) && Tile.Idx.size() == size_t(64 * 64 * 6 + 4 * 64 * 6) && Tile.Nrm.size() == Tile.Pos.size() && Tile.Uv.size() == Tile.Vertices() * 2;
					int Up = 0, Tris = 0;
					double Worst = 0.0;
					for (size_t T = 0; T + 2 < Tile.Idx.size() && T < size_t(64 * 64 * 6); T += 3)
					{
						const float* A3 = &Tile.Pos[Tile.Idx[T] * 3], * B3 = &Tile.Pos[Tile.Idx[T + 1] * 3], * C3 = &Tile.Pos[Tile.Idx[T + 2] * 3];
						// (the y of the cross product of its two edges: up for a triangle wound as said)
						const double Ny = double(B3[2] - A3[2]) * double(C3[0] - A3[0]) - double(B3[0] - A3[0]) * double(C3[2] - A3[2]);
						++Tris;
						if (Ny > 0.0) ++Up;
					}
					for (size_t V = 0; V < size_t(65 * 65); ++V)
					{
						Sound = Sound && Tile.Nrm[V * 3 + 1] > 0.3f;
						Worst = std::fmax(Worst, std::fabs(double(Tile.Pos[V * 3 + 1]) - Plan->Ground(X0 + double(Tile.Pos[V * 3]), Z0 + double(Tile.Pos[V * 3 + 2]))));
					}
					Check(Sound && Up == Tris && Worst < 0.01, "a 256 m square of ground comes as a mesh with its skirt: every triangle facing up, every height the ground's");
					Plan->RoadPiece(0, 100, 164, Piece);
					int RoadUp = 0, RoadTris = 0;
					for (size_t T = 0; T + 2 < Piece.Idx.size(); T += 3)
					{
						const float* A3 = &Piece.Pos[Piece.Idx[T] * 3], * B3 = &Piece.Pos[Piece.Idx[T + 1] * 3], * C3 = &Piece.Pos[Piece.Idx[T + 2] * 3];
						const double Ny = double(B3[2] - A3[2]) * double(C3[0] - A3[0]) - double(B3[0] - A3[0]) * double(C3[2] - A3[2]);
						++RoadTris;
						if (Ny > 0.0) ++RoadUp;
					}
					// (three strips a step: two shoulders and the tarmac; then any paint)
					Check(Piece.Vertices() >= 64 * 12 && Piece.Vertices() % 4 == 0 && RoadTris >= 64 * 6 && RoadUp == RoadTris && Piece.Col.size() == Piece.Pos.size()
						&& Mid.Half >= 1.6 && Mid.Half <= 3.7 && Plan->WidestHalf >= Mid.Half,
						"a piece of road comes as tarmac as wide as its class with a shoulder each side, coloured, every triangle facing up");
					// The road's line: no bend tighter than a bike can take.
					double Tightest = 1e9;
					{
						std::vector<double> Kx, Kz;
						for (const RoadPoint& P : Plan->Road) { Kx.push_back(P.At.X); Kz.push_back(P.At.Z); }
						for (size_t I = 2; I + 2 < Kx.size(); ++I)
						{
							const double Bd = RoadLine::Bend(Kx, Kz, I);
							if (Bd > 1e-9) Tightest = std::fmin(Tightest, 1.0 / Bd);
						}
					}
					// The roads that meet it.
					{
						int Meeting = 0, Level = 0, SidePts = 0;
						double WorstStep = 0.0;
						for (const RidePlan::SideRoad& Sd : Plan->Sides)
						{
							SidePts += int(Sd.Pts.size());
							for (int End = 0; End < 2; ++End)
							{
								const RoadPoint& Tip = End == 0 ? Sd.Pts.front() : Sd.Pts.back();
								double Dn2 = 0.0, Hn2 = 0.0, Half2 = 0.0;
								if (!Plan->Nearest(Tip.At.X, Tip.At.Z, Dn2, Hn2, nullptr, &Half2, nullptr, true) || Dn2 > Half2) continue;
								++Meeting;
								if (std::fabs(Tip.Height - Hn2) < 0.02) ++Level;
							}
							for (size_t I = 1; I < Sd.Pts.size(); ++I)
							{
								const double D = Sd.Pts[I].At.DistanceTo(Sd.Pts[I - 1].At);
								if (D > 0.5) WorstStep = std::fmax(WorstStep, std::fabs(Sd.Pts[I].Height - Sd.Pts[I - 1].Height) / D);
							}
						}
						RideMesh SideMesh;
						if (!Plan->Sides.empty()) Plan->RoadPiece(1, 0, 64, SideMesh);
						std::printf("      %d roads beside it in %d points; %d ends meet the ride's road, %d of them level with it; steepest %.2f\n", int(Plan->Sides.size()), SidePts, Meeting, Level, WorstStep);
						Check(Plan->Sides.size() > 20 && Meeting > 10 && Level == Meeting && WorstStep < 0.6 && SideMesh.Vertices() >= 12 && Plan->RoadCount() == 1 + int(Plan->Sides.size()),
							"the roads near the ride are laid too, each level with the ride's road where it meets it");
					}
					// Its bridges: the fixture's spans it crosses, each a deck with parapets.
					{
						int First = -1, Last = -1;
						for (size_t I = 0; I < Plan->Road.size(); ++I)
							if (Plan->Road[I].Bridge) { if (First < 0) First = int(I); Last = int(I); if (I + 1 < Plan->Road.size() && !Plan->Road[I + 1].Bridge) break; }
						RideMesh Deck, Plain;
						if (First >= 0) Plan->RoadPiece(0, First, Last, Deck);
						Plan->RoadPiece(0, 100, 100 + std::max(Last - First, 1), Plain);
						float Highest = -1e9f, Lowest = 1e9f;
						for (size_t V = 0; V < Deck.Vertices(); ++V) { Highest = std::fmax(Highest, Deck.Pos[V * 3 + 1]); Lowest = std::fmin(Lowest, Deck.Pos[V * 3 + 1]); }
						std::printf("      %d bridges and %d tunnels on it; the first deck is %d knots\n", Plan->Bridges, Plan->Tunnels, First >= 0 ? Last - First + 1 : 0);
						Check(Plan->Bridges == 0 || (First >= 0 && Last > First && Deck.Vertices() > Plain.Vertices() && Highest - Lowest > 2.0 && Highest - Lowest < 2.2 + std::fabs(Plan->Road[size_t(Last)].Height - Plan->Road[size_t(First)].Height) + 0.5),
							"where the road crosses a mapped bridge it is a deck with parapets a metre high, the land under it left alone");
					}
					// A tunnel (the fixture's ride has none, so 30 knots of it are called one): a tube with the
					// hill laid back over it and a headwall at each mouth.
					{
						size_t T0 = 0;
						for (size_t I = 150; I + 40 < Plan->Road.size() && T0 == 0; ++I)
						{
							bool Clear = true;
							for (size_t J = I - 12; J < I + 42 && Clear; ++J) Clear = !Plan->Road[J].Bridge;
							// (and no other road near its middle, so the hill over it is the land's own)
							RidePlan::NearOpts Others;
							Others.NotRoad = 0;
							RidePlan::NearHit Hit;
							if (Clear && !Plan->NearestOf(Plan->Road[I + 15].At.X, Plan->Road[I + 15].At.Z, Others, Hit)) T0 = I;
						}
						RideMesh Open, Bore;
						Plan->RoadPiece(0, int(T0) - 4, int(T0) + 34, Open);
						for (size_t I = T0; I < T0 + 30; ++I) Plan->Road[I].Tunnel = true;
						Plan->RoadPiece(0, int(T0) - 4, int(T0) + 34, Bore);
						const RoadPoint& Pm = Plan->Road[T0 + 15];
						const double Top = RidePlan::TubeWall + RidePlan::TubeArch;
						double Roof = -1e9, LidLow = 1e9;
						int Grass = 0, Wall = 0;
						for (size_t V = 0; V < Bore.Vertices(); ++V)
						{
							const double X = Bore.OriginX + Bore.Pos[V * 3], Y = Bore.Pos[V * 3 + 1], Z = Bore.OriginZ + Bore.Pos[V * 3 + 2];
							const bool Green = Bore.Col[V * 3 + 1] > Bore.Col[V * 3] * 1.15 && Bore.Col[V * 3 + 1] > Bore.Col[V * 3 + 2] * 1.5;
							if (Green) ++Grass;
							else if (Bore.Col[V * 3] > 0.3f && Bore.Col[V * 3] < 0.4f) ++Wall;
							const double D = std::hypot(X - Pm.At.X, Z - Pm.At.Z);
							if (!Green && D < 0.5) Roof = std::fmax(Roof, Y - Pm.Height);
							if (Green && D < 0.5) LidLow = std::fmin(LidLow, Y - Pm.Height);
						}
						int LidTris = 0, LidUp = 0;
						for (size_t T = 0; T + 2 < Bore.Idx.size(); T += 3)
						{
							const uint32_t Va = Bore.Idx[T], Vb = Bore.Idx[T + 1], Vc = Bore.Idx[T + 2];
							if (!(Bore.Col[Va * 3 + 1] > Bore.Col[Va * 3] * 1.15 && Bore.Col[Va * 3 + 1] > Bore.Col[Va * 3 + 2] * 1.5)) continue;
							const float* A3 = &Bore.Pos[Va * 3], * B3 = &Bore.Pos[Vb * 3], * C3 = &Bore.Pos[Vc * 3];
							++LidTris;
							if (double(B3[2] - A3[2]) * double(C3[0] - A3[0]) - double(B3[0] - A3[0]) * double(C3[2] - A3[2]) > 0.0) ++LidUp;
						}
						const double Cut = Plan->Ground(Pm.At.X, Pm.At.Z), FarOff = Plan->Ground(Pm.At.X, Pm.At.Z, 64.0), Lie = Plan->Land->Height(Pm.At.X, Pm.At.Z);
						std::printf("      a tunnel of 30 knots at %.2f km: its roof %.1f m over the road, the hill's lid %.1f m; %d more points than the open road\n",
							Pm.Along / 1000.0, Roof, LidLow, int(Bore.Vertices()) - int(Open.Vertices()));
						Check(T0 > 0 && Bore.Vertices() > Open.Vertices() + 29 * 4 * 20 && Bore.Vertices() % 4 == 0 && Near(Roof, Top, 0.01) && LidLow >= Top + 0.59 && Grass > 29 * 14 * 4 - 1 && Wall > 0 && LidTris == 29 * 14 * 2 && LidUp == LidTris
							&& Near(Cut, Pm.Height + RoadLine::CrossHeight(0.0, Pm.Half, Pm.Bank) - RidePlan::Sink, 1e-6) && FarOff == Lie,
							"a tunnel is a tube with an arched roof, the hill laid back over it clear of the arch and a headwall at each mouth; the far-off land has no cutting there");
						for (size_t I = T0; I < T0 + 30; ++I) Plan->Road[I].Tunnel = false;
					}
					// Junction mouths: corners rounded, give-way lines across the lesser road, the edge line broken.
					{
						int Mouths = 0, GiveWays = 0, Painted = 0, Tested = 0, FacingUp = 0, Broken = 0, Wide = 0;
						for (size_t W = 0; W < Plan->Sides.size(); ++W)
						{
							const RidePlan::SideRoad& Sd = Plan->Sides[W];
							bool Decked = false;
							for (const RoadPoint& P : Sd.Pts) Decked = Decked || P.Bridge || P.Tunnel;
							for (int End = 0; End < 2; ++End)
							{
								const RidePlan::Mouth& M = Sd.Mouths[End];
								if (!M.Is) continue;
								++Mouths;
								if (!M.GiveWay || Decked) continue;
								++GiveWays;
								RideMesh Mesh;
								const int Count = int(Sd.Pts.size());
								Plan->RoadPiece(int(W) + 1, End == 0 ? 0 : std::max(0, Count - 1 - 64), End == 0 ? std::min(64, Count - 1) : Count - 1, Mesh);
								bool Marks = false, AllUp = true;
								for (size_t V = 0; V < Mesh.Vertices(); ++V)
									if (Mesh.Col[V * 3] > 0.8f && std::hypot(Mesh.OriginX + Mesh.Pos[V * 3] - M.At.X, Mesh.OriginZ + Mesh.Pos[V * 3 + 2] - M.At.Z) < Sd.Pts[0].Half * 2.5 + 1.0) Marks = true;
								for (size_t T = 0; T + 2 < Mesh.Idx.size(); T += 3)
								{
									const float* A3 = &Mesh.Pos[Mesh.Idx[T] * 3], * B3 = &Mesh.Pos[Mesh.Idx[T + 1] * 3], * C3 = &Mesh.Pos[Mesh.Idx[T + 2] * 3];
									if (!(double(B3[2] - A3[2]) * double(C3[0] - A3[0]) - double(B3[0] - A3[0]) * double(C3[2] - A3[2]) >= -0.05)) AllUp = false;   // (but for a sliver where a road is cut off at the mouth)
								}
								++Tested;
								if (Marks) ++Painted;
								if (AllUp) ++FacingUp;
								if (M.MainHalf < 2.95) continue;
								// (the ride's road's edge line, 0.3 m in from its edge, is not there across the mouth)
								++Wide;
								RideMesh Main;
								Plan->RoadPiece(0, M.MainIndex - 6, M.MainIndex + 6, Main);
								const P2 OnLine = M.At - M.Across * 0.3;
								bool Lined = false;
								for (size_t V = 0; V < Main.Vertices(); ++V)
									if (Main.Col[V * 3] > 0.8f && std::hypot(Main.OriginX + Main.Pos[V * 3] - OnLine.X, Main.OriginZ + Main.Pos[V * 3 + 2] - OnLine.Z) < 1.2) Lined = true;
								if (!Lined) ++Broken;
							}
						}
						std::printf("      %d junction mouths; %d give way (%d marked, %d all facing up); the edge line broken at %d of %d on a wide road\n", Mouths, Tested, Painted, FacingUp, Broken, Wide);
						Check(Mouths > 10 && Tested > 3 && Painted == Tested && FacingUp == Tested && Broken == Wide,
							"a side road's mouth on the ride's road has rounded corners, give-way lines where it is the lesser road, and the ride's road's edge line broken across it");
						(void)GiveWays;
					}
					// Where a side road lies on the ride's road it is just under that road's surface, never over
					// it (two surfaces fought there and a flat side road on a slope stood proud: build 140's bump).
					{
						int OnMain = 0, Proud = 0, Leaning = 0;
						double WorstProud = 0.0, WorstLean = 0.0;
						RidePlan::NearOpts MainOnly;
						MainOnly.MainOnly = true;
						for (size_t W = 0; W < Plan->Sides.size(); ++W)
						{
							const int Count = int(Plan->Sides[W].Pts.size());
							RideMesh Mesh;
							Plan->RoadPiece(int(W) + 1, 0, Count - 1, Mesh);
							for (size_t V = 0; V < Mesh.Vertices(); ++V)
							{
								const double X = Mesh.OriginX + Mesh.Pos[V * 3], Y = Mesh.Pos[V * 3 + 1], Z = Mesh.OriginZ + Mesh.Pos[V * 3 + 2];
								RidePlan::NearHit Hit;
								if (!Plan->NearestOf(X, Z, MainOnly, Hit) || Hit.Deck || Hit.Bore) continue;
								const double Theirs = Hit.Height + Hit.Cross;
								if (std::fabs(Y - Theirs) > 1.0 || Mesh.Col[V * 3] > 0.05f) continue;   // (over or under it on a bridge; paint and the shoulder aside)
								if (Hit.Dist < Hit.Half - 0.05)
								{
									++OnMain;
									if (Y > Theirs + 0.016) { ++Proud; WorstProud = std::fmax(WorstProud, Y - Theirs); }
								}
								else if (Hit.Dist < Hit.Half + 0.5)
								{
									++Leaning;
									WorstLean = std::fmax(WorstLean, std::fabs(Y - Theirs));
								}
							}
						}
						std::printf("      side roads' tarmac on the ride's road: %d points, %d over its surface (worst %.3f m); just past its edge %d, at most %.3f m off it\n", OnMain, Proud, WorstProud, Leaning, WorstLean);
						Check(OnMain > 100 && Proud == 0 && WorstLean < 0.12, "a side road where it lies on the ride's road is just under its surface, and meets its edge level");
					}
					// Junctions off the ride's road (side roads meeting each other, the ride's own ends): a fan
					// over all their roads, rounded between them, facing up, never under its roads' surfaces.
					{
						int Fans = 0, FansUp = 0, FansUnder = 0, Arms = 0;
						for (const RidePlan::Knot& K : Plan->Knots)
						{
							RideMesh Mesh;
							Mesh.OriginX = K.At.X;
							Mesh.OriginZ = K.At.Z;
							Plan->KnotPiece(K, Mesh);
							if (Mesh.Idx.empty()) continue;
							++Fans;
							Arms += int(K.Arms.size());
							bool AllUp = true, AllOver = true;
							for (size_t T = 0; T + 2 < Mesh.Idx.size(); T += 3)
							{
								const float* A3 = &Mesh.Pos[Mesh.Idx[T] * 3], * B3 = &Mesh.Pos[Mesh.Idx[T + 1] * 3], * C3 = &Mesh.Pos[Mesh.Idx[T + 2] * 3];
								if (double(B3[2] - A3[2]) * double(C3[0] - A3[0]) - double(B3[0] - A3[0]) * double(C3[2] - A3[2]) < 0.0) AllUp = false;
							}
							for (size_t V = 0; V < Mesh.Vertices(); ++V)
							{
								if (Mesh.Col[V * 3] > 0.05f) continue;   // (the tarmac: the shoulders go down into the ground)
								const double X = Mesh.OriginX + Mesh.Pos[V * 3], Z = Mesh.OriginZ + Mesh.Pos[V * 3 + 2];
								for (const RidePlan::KnotArm& Arm : K.Arms)
								{
									RidePlan::NearOpts Only;
									Only.OnlyRoad = Arm.Which;
									RidePlan::NearHit Hit;
									if (Plan->NearestOf(X, Z, Only, Hit) && Hit.Dist < Hit.Half - 0.1 && Arm.Which == 0 && Mesh.Pos[V * 3 + 1] < Hit.Height + Hit.Cross - 0.005) AllOver = false;
								}
							}
							if (AllUp) ++FansUp;
							if (!AllOver) ++FansUnder;
						}
						std::printf("      %d junctions off the ride's road (%d roads' ends), %d all facing up, %d under the ride's road\n", Fans, Arms, FansUp, FansUnder);
						Check(Fans >= 5 && FansUp == Fans && FansUnder == 0, "where side roads meet each other (or the ride's road ends among them) one fan of tarmac covers the junction, rounded, facing up, over the ride's road");
					}
					// The ground's colour and what stands on it.
					{
						size_t Stands = 0, StoneGrey = 0, Squares = 0, Alike = 0, OnTarmac = 0, InScan = 0;
						bool Coloured = true, Masked = true;
						const auto AlikeMesh = [](const RideMesh* Ma, const RideMesh* Mb) { return (!Ma && !Mb) || (Ma && Mb && Ma->Pos == Mb->Pos && Ma->Col == Mb->Col && Ma->Alpha == Mb->Alpha); };
						for (size_t I = 0; I < Plan->Road.size() && Squares < 12; I += 120)
						{
							const double Tx = std::floor(Plan->Road[I].At.X / 256.0) * 256.0, Tz = std::floor(Plan->Road[I].At.Z / 256.0) * 256.0;
							RideMesh Sq, Again;
							Plan->LandTile(Tx, Tz, 256.0, 64, Sq);
							Plan->LandTile(Tx, Tz, 256.0, 64, Again);
							++Squares;
							Coloured = Coloured && Sq.Col.size() == Sq.Pos.size() && !Sq.Col.empty();
							if (!Sq.Extra) { if (!Again.Extra) ++Alike; continue; }
							if (Again.Extra && AlikeMesh(Sq.Extra.get(), Again.Extra.get()) && AlikeMesh(Sq.Extra->Stone.get(), Again.Extra->Stone.get()) && AlikeMesh(Sq.Extra->Masonry.get(), Again.Extra->Masonry.get())) ++Alike;
							for (const RideMesh* Part : {Sq.Extra.get(), Sq.Extra->Stone.get(), Sq.Extra->Masonry.get()})
							{
								if (!Part) continue;
								const bool Scanned = Part != Sq.Extra.get();
								Stands += Part->Vertices();
								Coloured = Coloured && Part->Col.size() == Part->Pos.size() && Part->Vertices() % 4 == 0 && Part->Uv.size() == Part->Vertices() * 2;
								// (the scanned stone's masks: brightness about the scan's own, moss in the alpha, UVs metres of a square's walls)
								if (Scanned) Masked = Masked && Part->Alpha.size() == Part->Vertices() && Part->OriginX == Sq.OriginX && Part->OriginZ == Sq.OriginZ;
								for (size_t V = 0; V < Part->Vertices(); ++V)
								{
									const float* C = &Part->Col[V * 3];
									if (Scanned)
									{
										++InScan;
										Masked = Masked && C[0] > 0.3f && C[0] < 0.7f && C[1] >= 0.0f && C[1] <= 1.0f && Part->Alpha[V] >= 0.0f && Part->Alpha[V] <= 1.0f
											&& std::fabs(Part->Uv[V * 2]) < 1000.0f && std::fabs(Part->Uv[V * 2 + 1]) < 2000.0f;
									}
									else if (std::fabs(C[0] - C[1]) < 0.03f && C[0] > 0.15f) ++StoneGrey;
									// (nothing stands on a road's tarmac)
									const double Rh = Plan->RoadHeight(Part->OriginX + Part->Pos[V * 3], Part->OriginZ + Part->Pos[V * 3 + 2]);
									if (!std::isnan(Rh) && Part->Pos[V * 3 + 1] < Rh + 2.5) ++OnTarmac;   // (a tree's crown may reach over)
								}
							}
						}
						RideMesh FarSq;
						Plan->LandTile(std::floor(Plan->Road[0].At.X / 4096.0) * 4096.0, std::floor(Plan->Road[0].At.Z / 4096.0) * 4096.0, 4096.0, 64, FarSq);
						std::printf("      %d squares of ground along it: %d points of walls, buildings and trees stand on them, %d of them in the scanned stone and %d plain grey; %d on a road\n", int(Squares), int(Stands), int(InScan), int(StoneGrey), int(OnTarmac));
						Check(Coloured && Masked && Stands > 2000 && InScan > 500 && Alike == Squares && OnTarmac == 0 && !FarSq.Extra && FarSq.Col.size() == FarSq.Pos.size(),
							"the ground is coloured by what covers it, and its finer squares carry their walls, buildings and trees (walls and stone houses in the scanned stone's masks): the same every time, none in a rider's way on a road, none on the far-off squares");
					}
					// The grass and trees on the land, for the game to draw with Buttertubs' own plants.
					{
						size_t Squares = 0, PlantOnRoad = 0, PlantAstray = 0, Total = 0, Trees = 0, Alike = 0, Kinds = 0;
						size_t PerKind[RidePlan::PlantKinds] = {0};
						double PlantWorst = 0.0, AllMs = 0.0;
						for (size_t I = 0; I < Plan->Road.size() && Squares < 6; I += 200)
						{
							const double Tx = std::floor(Plan->Road[I].At.X / 256.0) * 256.0, Tz = std::floor(Plan->Road[I].At.Z / 256.0) * 256.0;
							RideMesh Sq, Again;
							const auto T0 = std::chrono::steady_clock::now();
							Plan->LandTile(Tx, Tz, 256.0, 64, Sq);
							AllMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
							Plan->LandTile(Tx, Tz, 256.0, 64, Again);
							++Squares;
							bool PlantSame = Sq.Plants.size() == Again.Plants.size();
							for (size_t K = 0; PlantSame && K < Sq.Plants.size(); ++K)
								PlantSame = Sq.Plants[K].Kind == Again.Plants[K].Kind && Sq.Plants[K].X == Again.Plants[K].X && Sq.Plants[K].Y == Again.Plants[K].Y && Sq.Plants[K].Z == Again.Plants[K].Z;
							if (PlantSame) ++Alike;
							for (const RidePlant& P : Sq.Plants)
							{
								++Total;
								if (P.Kind < RidePlan::PlantKinds) ++PerKind[P.Kind];
								if (P.Kind >= RidePlan::FirstTree) ++Trees;
								const double X = Sq.OriginX + P.X, Z = Sq.OriginZ + P.Z;
								// (nothing grows on a road's tarmac)
								if (!std::isnan(Plan->RoadHeight(X, Z))) ++PlantOnRoad;
								// (and each stands on the ground drawn: no higher than it, no deeper than a slope's sinking)
								const int Ci = std::min(63, std::max(0, int(P.X / 4.0f))), Cj = std::min(63, std::max(0, int(P.Z / 4.0f)));
								float Lo = 1e9f, Hi = -1e9f;
								for (int Dj = 0; Dj < 2; ++Dj)
									for (int Di = 0; Di < 2; ++Di) { const float Hy = Sq.Pos[(size_t(Cj + Dj) * 65 + size_t(Ci + Di)) * 3 + 1]; Lo = std::fmin(Lo, Hy); Hi = std::fmax(Hi, Hy); }
								PlantWorst = std::fmax(PlantWorst, double(std::fmax(P.Y - Hi, Lo - P.Y)));
								if (P.Y > Hi + 0.01f || P.Y < Lo - 1.0f) ++PlantAstray;
							}
						}
						for (size_t K = 0; K < size_t(RidePlan::PlantKinds); ++K) if (PerKind[K] > 0) ++Kinds;
						// The trees, with the real models asked for (and the stand-in shapes then left out).
						Plan->RealTrees = true;
						size_t RealTrees = 0, StandIns = 0, FarTrees = 0, Without = 0;
						for (size_t I = 0; I < Plan->Road.size(); I += 100)
						{
							RideMesh Sq, Coarse, Plain;
							Plan->RealTrees = false;
							Plan->LandTile(std::floor(Plan->Road[I].At.X / 256.0) * 256.0, std::floor(Plan->Road[I].At.Z / 256.0) * 256.0, 256.0, 64, Plain);
							Plan->RealTrees = true;
							if (Plain.Extra) Without += Plain.Extra->Vertices();
							Plan->LandTile(std::floor(Plan->Road[I].At.X / 256.0) * 256.0, std::floor(Plan->Road[I].At.Z / 256.0) * 256.0, 256.0, 64, Sq);
							Plan->LandTile(std::floor(Plan->Road[I].At.X / 1024.0) * 1024.0, std::floor(Plan->Road[I].At.Z / 1024.0) * 1024.0, 1024.0, 128, Coarse);
							for (const RidePlant& P : Sq.Plants) if (P.Kind >= RidePlan::FirstTree) { ++RealTrees; if (P.Height < 3.0f || P.Far) ++PlantAstray; }
							for (const RidePlant& P : Coarse.Plants) if (P.Kind >= RidePlan::FirstTree) { ++FarTrees; if (!P.Far) ++PlantAstray; } else ++PlantAstray;   // (no grass on the coarser squares)
							if (Sq.Extra) StandIns += Sq.Extra->Vertices();
						}
						Plan->RealTrees = false;
						std::printf("      %d squares: %d plants of %d kinds (%d carpet, %d tufts, %d far clumps, %d rush, %d heather, %d bracken), %.0f ms a square with them; %d on a road, %d not on the ground (worst %.2f m)\n",
							int(Squares), int(Total), int(Kinds), int(PerKind[0] + PerKind[1] + PerKind[2] + PerKind[3]), int(PerKind[4] + PerKind[5] + PerKind[6] + PerKind[7] + PerKind[8]),
							int(PerKind[9] + PerKind[10]), int(PerKind[11] + PerKind[12]), int(PerKind[13] + PerKind[14] + PerKind[15] + PerKind[16]), int(PerKind[17]), AllMs / double(std::max<size_t>(1, Squares)),
							int(PlantOnRoad), int(PlantAstray), PlantWorst);
						std::printf("      real trees asked for: %d near and %d far along it; what stands on the squares %d points with them, %d with the stand-ins\n", int(RealTrees), int(FarTrees), int(StandIns), int(Without));
						Check(Total > 20000 && Kinds >= 8 && Trees == 0 && PlantOnRoad == 0 && PlantAstray == 0 && Alike == Squares && RealTrees > 20 && FarTrees > 20 && StandIns < Without,
							"grass, rushes, heather and bracken lie on the finest squares near the roads and none on a road, each on the ground drawn, the same every time; real trees (near and far models) in place of the stand-ins when the game has them");
					}
					// Bridges on the roads beside it.
					{
						int Decks = 0, Crossings = 0;
						double Least = 1e9;
						for (const RidePlan::SideRoad& Sd : Plan->Sides)
							for (size_t I = 1; I < Sd.Pts.size(); ++I)
							{
								if (Sd.Pts[I].Bridge && !Sd.Pts[I - 1].Bridge) ++Decks;
								if (!Sd.Pts[I].Bridge) continue;
								double Dm = 0.0, Hm = 0.0, HalfM = 0.0;
								if (Plan->Nearest(Sd.Pts[I].At.X, Sd.Pts[I].At.Z, Dm, Hm, nullptr, &HalfM, nullptr, true) && Dm < HalfM) { ++Crossings; Least = std::fmin(Least, std::fabs(Sd.Pts[I].Height - Hm)); }
							}
						std::printf("      %d bridges and %d tunnels on the roads beside it (%d decks begin; %d deck knots over the ride's road)\n", Plan->SideBridges, Plan->SideTunnels, Decks, Crossings);
						Check(Plan->SideBridges >= Decks && (Plan->SideBridges == 0 || Decks > 0), "the roads beside it have their mapped bridges as decks too, but none at a junction with the ride's road");
					}
					// Its banking: the surface read back either side of the middle at the most banked knot.
					{
						size_t Most = 2;
						for (size_t I = 2; I + 2 < Plan->Road.size(); ++I)
							if (!Plan->Road[I].Bridge && std::fabs(Plan->Road[I].Bank) > std::fabs(Plan->Road[Most].Bank)) Most = I;
						const RoadPoint& Pm = Plan->Road[Most];
						P2 Dm = Plan->Road[Most + 1].At - Plan->Road[Most - 1].At;
						Dm = Dm * (1.0 / Dm.Length());
						const P2 Lm(Dm.Z, -Dm.X);
						const double Out3 = Pm.Half - 0.3;
						const double Hl = Plan->RoadHeight(Pm.At.X + Lm.X * Out3, Pm.At.Z + Lm.Z * Out3), Hr = Plan->RoadHeight(Pm.At.X - Lm.X * Out3, Pm.At.Z - Lm.Z * Out3);
						std::printf("      its most banked knot: %.1f%%, the left edge %.2f m and the right %.2f m off the middle's height\n", Pm.Bank * 100.0, Hl - Pm.Height, Hr - Pm.Height);
						Check(std::fabs(Pm.Bank) > 0.03 && std::fabs(Pm.Bank) <= RoadLine::MaxBank + 1e-9 && Near(Hl - Hr, 2.0 * Out3 * Pm.Bank, 0.03)
							&& std::fabs(Plan->Ground(Pm.At.X + Lm.X * Out3, Pm.At.Z + Lm.Z * Out3) - (Hl - RidePlan::Sink)) < 0.03,
							"its bends are banked: the surface is tilted across and the ground lies just under it both sides");
					}
					std::printf("      its tightest bend: %.1f m radius\n", Tightest);
					Check(Tightest > 9.0, "and no bend of it is tighter than 9 m, mapped corners and junctions rounded");
					Json Route;
					Check(Json::Parse(Plan->RouteJson(), Route) && Near(Route.Num("length_m"), Plan->Length, 1e-3) && Route.Get("line_sim") != nullptr && Route.Get("line_sim")->A.size() > 30
						&& Route.Get("spawn") != nullptr && Route.Get("spawn")->Get("sim") != nullptr && Near(Route.Get("spawn")->Get("sim")->A[1].N, Plan->Road[0].Height + 0.35, 1e-3),
						"and the route is written as the bike reads the Buttertubs one, its start a little above the road");
				}
			}
			Files::RemoveTree(TileDir);
		}
		// Offline, the next run still has what was fetched.
		CellStore Later;
		Later.Dir = Kept;
		Later.IndexUrl = "store://index.json";
		Later.Offline = true;
		Later.Get = Serve;
		const int BeforeLater = Asked;
		const std::shared_ptr<const WorldIndex> Saved = Later.Index();
		Check(Saved->Cells.size() == 4 && Later.StateOf(A, *Saved) == CellStore::EState::Here && Later.Kept(CellStore::EKind::Streams, "577_711", *Saved, File)
			&& !Later.FetchCells({NotYet}).empty() && Asked == BeforeLater, "offline, the next run has the index, the cells and their files as they were kept, and asks for nothing");
		// The disk is kept within bounds: the cell used longest ago goes, never one used this run.
		Later.Keep = 1;
		Check(Later.Prune() == 1 && (Files::IsDir(Files::Join(Kept, "c577_711")) != Files::IsDir(Files::Join(Kept, "c577_712"))), "past the limit the cell used longest ago is deleted");
		Cells.Keep = 0;
		Check(Cells.Prune() == 0, "but never a cell this run has used");
		Down = true;
		CellStore Cold;
		Cold.Dir = Files::Join(TORQUE_WORLD_TMP, "world_test_none");
		Cold.Get = Serve;
		Check(Cold.Index()->Empty() && Cold.FetchCells({A}).empty(), "with no network and nothing kept there is no index, and nothing to fetch");
		Files::RemoveTree(Kept);
	}

	{
		// The road's line made ridable (WorldRoadLine): a right-angled corner of a lane, a kink, a dead end.
		std::printf("\nThe road's line\n");
		const auto Tight = [](const std::vector<P2>& L)
		{
			std::vector<double> Kx, Kz;
			for (const P2& P : L) { Kx.push_back(P.X); Kz.push_back(P.Z); }
			double Least = 1e9;
			for (size_t I = 2; I + 2 < Kx.size(); ++I)
			{
				const double B = RoadLine::Bend(Kx, Kz, I);
				if (B > 1e-9) Least = std::fmin(Least, 1.0 / B);
			}
			return Least;
		};
		const std::vector<P2> Corner = {P2(0, 0), P2(200, 0), P2(200, 200)};
		const std::vector<P2> Round = RoadLine::SmoothLine(Corner);
		double Gap = 0.0, Off = 0.0;
		for (size_t I = 1; I < Round.size(); ++I) Gap = std::fmax(Gap, Round[I].DistanceTo(Round[I - 1]));
		for (const P2& P : Round) Off = std::fmax(Off, std::fmin(std::fabs(P.Z), std::fabs(P.X - 200.0)));
		std::printf("      a right angle: %d knots, tightest %.1f m, %.1f m off the map's line at most\n", int(Round.size()), Tight(Round), Off);
		Check(Round.size() > 90 && Round.front().DistanceTo(Corner.front()) < 1e-9 && Round.back().DistanceTo(Corner.back()) < 4.0 && Gap < 4.0 + 1e-6 && Tight(Round) >= 9.5 && Off < 12.0,
			"a right-angled corner becomes a bend of 10 m or more, knots 4 m apart, the ends kept, near the map's line");
		const std::vector<double> Main(Corner.size(), RoadLine::FairOf("primary"));
		Check(Tight(RoadLine::SmoothLine(Corner, RoadLine::MinRadius, Main)) > Tight(Round) + 3.0, "wider on a main road than a lane");
		std::vector<P2> Zig;
		for (int I = 0; I <= 40; ++I) Zig.push_back(P2(I * 15.0, (I % 2) * 6.0));
		Check(Tight(RoadLine::SmoothLine(Zig)) >= 9.5, "a road drawn as a zigzag flows");
		const std::vector<P2> There = {P2(0, 0), P2(300, 0), P2(0, 0.001), P2(-200, 0.001)};
		const std::vector<P2> Back = RoadLine::SmoothLine(There);
		double Reached = 0.0;
		for (const P2& P : Back) Reached = std::fmax(Reached, P.X);
		// (the turning place is drawn back down the road as it is pulled open, as in the Godot game: only a
		// ride with a stop up a dead end has one)
		Check(Tight(Back) >= 6.0 && Reached > 150.0, "an out-and-back gets a turning place, not a needle");
		Check(RoadLine::WidthOf("primary") == 7.4 && RoadLine::WidthOf("track") == 3.3 && RoadLine::WidthOf("?") == 4.6 && RoadLine::MainCostOf("motorway") < RoadLine::MainCostOf("residential"),
			"each class of road has the Godot game's width and cost");
		// Banking: a crown on the straight, a bend tilted to its inside by the UK's rule.
		{
			std::vector<P2> RightBend, Straight;
			for (int I = 0; I <= 100; ++I)
			{
				const double Ang = I * 4.0 / 500.0;   // a 500 m radius, turning from east towards south: right
				RightBend.push_back(P2(500.0 * std::sin(Ang), 500.0 * (1.0 - std::cos(Ang))));
				Straight.push_back(P2(I * 4.0, 0.0));
			}
			const std::vector<double> Banked = RoadLine::BankOf(RightBend, {95.0}), Flat = RoadLine::BankOf(Straight, {95.0}), Steep = RoadLine::BankOf(RightBend, {200.0});
			double FlatMost = 0.0;
			for (double B : Flat) FlatMost = std::fmax(FlatMost, std::fabs(B));
			std::printf("      a 500 m bend at 95 km/h is banked %.1f%% (the rule says %.1f%%)\n", Banked[50] * 100.0, 95.0 * 95.0 / (2.828 * 500.0));
			Check(Near(Banked[50], 95.0 * 95.0 / (282.8 * 500.0), 0.002) && Banked[50] > 0.0 && Banked.front() == 0.0 && Banked[3] < Banked[50] * 0.8 && FlatMost < 1e-9 && Near(Steep[50], RoadLine::MaxBank, 1e-9),
				"a bend to the right is banked left side up by V squared over 2.828 R per cent, eased in from its ends, 7% at most; a straight not at all");
			const double H = 3.0;
			Check(Near(RoadLine::CrossHeight(H, H, 0.0), -H * 0.025, 1e-12) && Near(RoadLine::CrossHeight(-H, H, 0.0), -H * 0.025, 1e-12) && RoadLine::CrossHeight(0.0, H, 0.05) == 0.0
				&& Near(RoadLine::CrossHeight(H, H, 0.05), H * 0.05, 1e-12) && Near(RoadLine::CrossHeight(-H, H, 0.05), -H * 0.05, 1e-12) && Near(RoadLine::CrossHeight(H + 5.0, H, 0.05), H * 0.05, 1e-12)
				&& Near(RoadLine::CrossHeight(H, H, 0.0125), -H * 0.00625, 1e-12) && Near(RoadLine::CrossHeight(-H, H, 0.0125), -H * 0.025, 1e-12),
				"across the road: a crown falling 2.5% each side on the straight, one plane tilted to the inside in a bend, the outside's fall taken out first");
		}
		const std::vector<double> Steps = {0, 0, 0, 5, 5, 5, 5};
		const std::vector<double> Graded = RoadLine::GradeLimit(Steps);
		double Steepest = 0.0;
		for (size_t I = 1; I < Graded.size(); ++I) Steepest = std::fmax(Steepest, std::fabs(Graded[I] - Graded[I - 1]) / RoadLine::Step);
		Check(Steepest <= RoadLine::MaxGrade + 1e-9, "a step in the ground is graded to 15% at most");
	}

	std::printf("\n%s\n", Fails == 0 ? "world_test: all passed" : "world_test: FAILED");
	return Fails == 0 ? 0 : 1;
}
