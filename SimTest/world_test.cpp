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
			const std::string Scheme = "store://";
			if (Down || Url.compare(0, Scheme.size(), Scheme) != 0) return 0;
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

	std::printf("\n%s\n", Fails == 0 ? "world_test: all passed" : "world_test: FAILED");
	return Fails == 0 ? 0 : 1;
}
