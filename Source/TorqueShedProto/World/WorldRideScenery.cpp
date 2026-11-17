// What stands on a ride's land (Ride UK stage 3: the look of the hand-built ride, made as you go):
// the ground's colour by what covers it, dry stone walls, hedges and fences on their mapped lines,
// buildings on their mapped footprints, and trees where the map's canopy says there are trees.
//
// All of it is plain meshes with their colours in their vertices, made from nothing but the cells,
// so every rider has the same walls and the same trees. The trees are stand-in shapes until the
// scanned ones are placed in their stead.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideSigns.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace TorqueWorld
{
namespace
{
using RideSceneBits::SceneCell;
using RideSceneBits::SceneKey;
using RideSceneBits::Chance;
using RideSceneBits::P3;
using RideSceneBits::At3;
using RideSceneBits::Rgb;
using RideSceneBits::Shade;
using RideSceneBits::BuildingTagColour;
using RideSceneBits::Quad;
using RideSceneBits::Disc;
using RideSceneBits::Box;

// [OSM stage 2] A hash of the map's own id for a thing (FNV-1a), never 0: its variant, the same for
// every rider and through a re-bake.
uint32_t PropHash(const std::string& Id)
{
	uint32_t H = 2166136261u;
	for (char C : Id) H = (H ^ static_cast<unsigned char>(C)) * 16777619u;
	return H == 0 ? 1u : H;
}

// The ground's colours (linear), by the baker's cover ids: grass, forest, crop, shrub, barren, urban,
// sand, wetland, water, rock. Pasture is the green of the Dales' valley floors and the moor (shrub)
// the dun of their tops, as the hand-built ride has them.
const Rgb CoverColour[10] = {
	{0.105f, 0.150f, 0.045f},   // grass: grazed pasture
	{0.040f, 0.060f, 0.025f},   // forest: the floor under the trees
	{0.150f, 0.160f, 0.055f},   // crop: meadow and ploughland
	{0.095f, 0.090f, 0.045f},   // shrub: moor grass and heather
	{0.160f, 0.150f, 0.130f},   // barren
	{0.120f, 0.125f, 0.095f},   // urban: yards and gardens
	{0.380f, 0.330f, 0.230f},   // sand
	{0.060f, 0.075f, 0.040f},   // wetland
	{0.030f, 0.045f, 0.050f},   // water
	{0.210f, 0.205f, 0.190f},   // rock
};
const Rgb NoCover = {0.088f, 0.105f, 0.04f};
const Rgb Scar = {0.2f, 0.195f, 0.18f};   // (limestone showing on the steepest ground)

// [OSM stage 6A/6E] A place's name board: as wide as its words set (both of them, a second-language name
// first and "|" between, the wider), H high for one line or two.
double NameBoardWide(const std::string& Text, double* High = nullptr)
{
	double Most = 0.0;
	int Lines = 0;
	size_t From = 0;
	for (;;)
	{
		const size_t Bar = Text.find('|', From);
		const std::string One = Text.substr(From, Bar == std::string::npos ? std::string::npos : Bar - From);
		double Wd = 0.0;
		Most = std::fmax(Most, SignText::Width(One, 0.22 * SignText::CapOfEm, Wd) ? Wd + 0.5 : 0.5 + 0.17 * double(One.size()));
		++Lines;
		if (Bar == std::string::npos) break;
		From = Bar + 1;
	}
	if (High != nullptr) *High = Lines > 1 ? 0.8 : 0.5;
	return Most;
}

// [OSM stage 6B] A warning a road's shape asks for: what (SignPlate::ESymbol, Text a hill's gradient),
// where the danger starts (Along) for the traffic going Way (+1 the way the road's points run, -1 back).
struct Hazard
{
	double Along = 0.0;
	int Way = 1;
	int Symbol = 0;
	std::string Text;
};
// How far before a danger its warning stands, and the triangle's side, by the limit (Britain's Traffic
// Signs Manual, chapter 4's table: 45 m at 30 mph to 200 m at 70; 600 mm to 1200 mm), a narrow road's no
// more than 750 mm.
double WarnBefore(int Mph) { return Mph <= 30 ? 45.0 : Mph <= 40 ? 65.0 : Mph <= 50 ? 100.0 : Mph <= 60 ? 150.0 : 200.0; }
double WarnSide(int Mph, double Half) { const double S = Mph <= 30 ? 0.6 : Mph <= 40 ? 0.75 : Mph <= 60 ? 0.9 : 1.2; return Half < 2.8 ? std::fmin(S, 0.75) : S; }
// The road's bends too tight for its limit (a comfortable 3 m/s2 across at no more than 0.65 of it) and
// turning 40 degrees or more, and its hills of 10% or more, each way. A bend in a built-up stretch gets
// none; a bend another's sign already covers (within 150 m after it, the same way) gets none; two
// bends the opposite ways within 120 m are a double bend. Mph: the limit at a point (0 none: no warning).
template <typename FMph>
void ShapeHazards(const std::vector<RoadPoint>& Pts, const std::vector<unsigned char>& BuiltUp, const FMph& Mph, std::vector<Hazard>& Out)
{
	using namespace SignPlate;
	const double Pi = 3.14159265358979323846;
	const size_t N = Pts.size();
	if (N < 3) return;
	std::vector<double> Radius(N, 1e9), Turn(N, 0.0);
	for (size_t I = 0; I < N; ++I)
	{
		size_t A = I, B = I;
		while (A > 0 && Pts[I].Along - Pts[A].Along < 15.0) --A;
		while (B + 1 < N && Pts[B].Along - Pts[I].Along < 15.0) ++B;
		if (A == I || B == I) continue;
		const P2 U = Pts[I].At - Pts[A].At, V = Pts[B].At - Pts[I].At;
		double T = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
		while (T > Pi) T -= 2.0 * Pi;
		while (T < -Pi) T += 2.0 * Pi;
		Turn[I] = T;
		if (std::fabs(T) > 1e-4) Radius[I] = (Pts[B].Along - Pts[A].Along) / std::fabs(T);
	}
	const auto Tight = [&](size_t I)
	{
		const int Limit = Mph(Pts[I].Along);
		if (Limit <= 0) return false;
		const double V = Limit * 0.44704 * 0.65;
		return Radius[I] < V * V / 3.0;
	};
	struct Bend { double From = 0.0, To = 0.0; bool Right = false; };
	std::vector<Bend> Bends;
	for (size_t I = 0; I < N; ++I)
	{
		if (!Tight(I) || (I < BuiltUp.size() && BuiltUp[I])) continue;
		size_t J = I;
		while (J + 1 < N && Tight(J + 1) && (Turn[J + 1] > 0.0) == (Turn[I] > 0.0)) ++J;
		double Whole = 0.0;
		for (size_t K = I + 1; K <= J; ++K) Whole += Turn[K] * (Pts[K].Along - Pts[K - 1].Along) / 30.0;
		bool Busy = false;
		for (size_t K = I; K <= J && !Busy; ++K) Busy = K < BuiltUp.size() && BuiltUp[K];
		if (std::fabs(Whole) >= 40.0 * Pi / 180.0 && !Busy)
		{
			Bend B;
			// (from where it starts to turn: the window's half before the first tight point)
			B.From = std::fmax(0.0, Pts[I].Along - 10.0);
			B.To = std::fmin(Pts[N - 1].Along, Pts[J].Along + 10.0);
			B.Right = Turn[I] > 0.0;   // (a right-hand bend as the points run turns clockwise seen from above, Turn > 0)
			Bends.push_back(B);
		}
		I = J;
	}
	for (int Way : {1, -1})
	{
		std::vector<Bend> Seen = Bends;
		if (Way < 0)
		{
			std::reverse(Seen.begin(), Seen.end());
			for (Bend& B : Seen) B.Right = !B.Right;
		}
		const auto Start = [Way](const Bend& B) { return Way > 0 ? B.From : B.To; };
		const auto End = [Way](const Bend& B) { return Way > 0 ? B.To : B.From; };
		double Covered = Way > 0 ? -1e9 : 1e9;
		for (size_t K = 0; K < Seen.size(); ++K)
		{
			const Bend& B = Seen[K];
			if ((Start(B) - Covered) * Way < 0.0) { Covered = End(B) + Way * 150.0; continue; }
			Hazard H;
			H.Along = Start(B);
			H.Way = Way;
			H.Symbol = B.Right ? SymBendRight : SymBendLeft;
			Covered = End(B) + Way * 150.0;
			if (K + 1 < Seen.size() && Seen[K + 1].Right != B.Right && (Start(Seen[K + 1]) - End(B)) * Way < 120.0)
			{
				H.Symbol = B.Right ? SymDoubleBendRight : SymDoubleBendLeft;
				Covered = End(Seen[K + 1]) + Way * 150.0;
				++K;
			}
			Out.push_back(H);
		}
	}
	// The hills: the gradient over 50 m; a stretch of 10% or more at least 40 m long (gaps under 60 m
	// closed), its steepest shown rounded down to the figures signs use.
	const auto HeightAt = [&Pts, N](double Along)
	{
		size_t Lo = 0, Hi = N;
		while (Hi - Lo > 1)
		{
			const size_t M = (Lo + Hi) / 2;
			if (Pts[M].Along <= Along) Lo = M;
			else Hi = M;
		}
		if (Lo + 1 >= N) return Pts[N - 1].Height;
		const double T = std::clamp((Along - Pts[Lo].Along) / std::fmax(1e-6, Pts[Lo + 1].Along - Pts[Lo].Along), 0.0, 1.0);
		return Pts[Lo].Height + (Pts[Lo + 1].Height - Pts[Lo].Height) * T;
	};
	const double Length = Pts[N - 1].Along;
	std::vector<double> Grade(N, 0.0);
	for (size_t I = 0; I < N; ++I)
	{
		const double A = Pts[I].Along;
		if (A < 25.0 || A > Length - 25.0) continue;
		Grade[I] = (HeightAt(A + 25.0) - HeightAt(A - 25.0)) / 50.0;
	}
	for (int Up : {1, -1})
	{
		for (size_t I = 0; I < N; ++I)
		{
			if (Grade[I] * Up < 0.1) continue;
			size_t J = I;
			double Steepest = 0.0;
			for (size_t K = I; K < N; ++K)
			{
				if (Grade[K] * Up >= 0.1) { J = K; Steepest = std::fmax(Steepest, Grade[K] * Up); continue; }
				if (Pts[K].Along - Pts[J].Along > 60.0) break;
			}
			if (Pts[J].Along - Pts[I].Along >= 40.0)
			{
				static const int Figures[] = {33, 25, 20, 17, 14, 12, 10};
				int Shown = 10;
				for (int F : Figures)
					if (Steepest * 100.0 >= F - 0.01) { Shown = F; break; }
				// (going the way the points run it climbs where Up is +1; coming back it falls)
				for (int Way : {1, -1})
				{
					Hazard H;
					H.Way = Way;
					H.Along = Way > 0 ? std::fmax(0.0, Pts[I].Along - 25.0) : std::fmin(Length, Pts[J].Along + 25.0);
					H.Symbol = Up * Way > 0 ? SymSteepUp : SymSteepDown;
					H.Text = std::to_string(Shown) + "%";
					Out.push_back(H);
				}
			}
			I = J;
		}
	}
}
} // namespace

void RidePlan::LandColour(double X, double Z, double Spread, double Slope, float* Out) const
{
	// What covers the ground here and a little way off each way, so one kind runs into the next.
	double R = 0.0, G = 0.0, B = 0.0;
	const double Offs[5][2] = {{0.0, 0.0}, {Spread, 0.0}, {-Spread, 0.0}, {0.0, Spread}, {0.0, -Spread}};
	// The season (Ride UK stage 6): the heather in flower on the moor in August, the bracken and the
	// pasture dull and straw-coloured over the winter, the crops ripening gold, then the stubble and
	// the plough.
	const double Bloom = Month == 8 ? 1.0 : Month == 9 ? 0.5 : Month == 7 ? 0.25 : 0.0;
	const double Dead = Month >= 11 || Month <= 2 ? 1.0 : Month == 3 || Month == 10 ? 0.5 : 0.0;
	const double Ripe = Month == 7 || Month == 8 ? 1.0 : Month == 6 ? 0.4 : 0.0, Stubble = Month >= 9 || Month <= 2 ? 1.0 : 0.0;
	const bool Patch = Chance(std::floor(X / 24.0), std::floor(Z / 24.0), 91) < 0.6;
	const auto Toward = [](Rgb C, const Rgb& To, double By)
	{
		C.R = float(C.R + (To.R - C.R) * By); C.G = float(C.G + (To.G - C.G) * By); C.B = float(C.B + (To.B - C.B) * By);
		return C;
	};
	static const Rgb Heather = {0.13f, 0.045f, 0.11f}, Bracken = {0.15f, 0.075f, 0.03f}, Straw = {0.125f, 0.12f, 0.06f}, Gold = {0.26f, 0.2f, 0.07f}, Plough = {0.11f, 0.08f, 0.05f};
	for (const double* Off : Offs)
	{
		const int Id = Land ? Land->Cover(X + Off[0], Z + Off[1]) : -1;
		Rgb C = Id >= 0 && Id < 10 ? CoverColour[Id] : NoCover;
		if (Id == 3) C = Toward(Toward(C, Heather, Patch ? 0.5 * Bloom : 0.0), Bracken, (Patch ? 0.15 : 0.4) * Dead);
		else if (Id == 0) C = Toward(C, Straw, 0.3 * Dead);
		else if (Id == 2) C = Toward(Toward(C, Gold, 0.5 * Ripe), Plough, 0.45 * Stubble);
		const double W = Off[0] == 0.0 && Off[1] == 0.0 ? 2.0 : 1.0;
		R += C.R * W; G += C.G * W; B += C.B * W;
	}
	R /= 6.0; G /= 6.0; B /= 6.0;
	{
		// [OSM stage 2 batch C] (a quarry's bare stone, a graveyard's mown grass, the allotments' plots)
		float Here[3] = {float(R), float(G), float(B)};
		if (FieldTint(X, Z, Here)) { R = Here[0]; G = Here[1]; B = Here[2]; }
	}
	// Steep ground is thin turf, then bare stone.
	const double Bare = std::fmin(1.0, std::fmax(0.0, (Slope - 0.7) / 0.5));
	R += (Scar.R - R) * Bare; G += (Scar.G - G) * Bare; B += (Scar.B - B) * Bare;
	// [stage 8] High mountains (none in Britain): old snow and the glaciers' ice above about 2700 m,
	// lying less on the steep, so the crags show through.
	if (Land && !Britain())
	{
		const double High = Land->Height(X, Z);
		const double Snow = std::fmin(1.0, std::fmax(0.0, (High - 2700.0) / 350.0)) * (1.0 - 0.7 * Bare);
		static const Rgb Firn = {0.62f, 0.64f, 0.68f};
		R += (Firn.R - R) * Snow; G += (Firn.G - G) * Snow; B += (Firn.B - B) * Snow;
	}
	Out[0] = float(R); Out[1] = float(G); Out[2] = float(B);
}

const LocalLook& RidePlan::LookHere(double X, double Z) const
{
	if (!Britain()) return WorldLook();   // [stage 8]
	if (const LocalLook* Town = TownLookAt(X, Z)) return *Town;   // [towns stage 1]
	const Geo Here = Frame(Area.Roads.FrameLat, Area.Roads.FrameLon).ToGeo(X, Z);
	return LookAt(Here.Lat, Here.Lon);
}

void RidePlan::IndexScenery()
{
	Scene = std::make_shared<SceneIndex>();
	// [OSM stage 5C] The map's crash barriers (the roadx bake's guard rails and concrete barriers) drawn
	// with the walls and fences: added to the scenery's lines once.
	if (!BarriersLined)
	{
		BarriersLined = true;
		for (const GroundShape& Bar : Area.Roads.Barriers)
		{
			FeatureLine F;
			F.Kind = Bar.Kind == "concrete_barrier" ? "concrete_barrier" : "guard_rail";
			F.Line = Bar.Pts;
			Area.Roads.Scenery.Lines.push_back(std::move(F));
		}
	}
	const Region& G = Area.Roads;
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& F = G.Scenery.Lines[L];
		const bool Wall = F.Kind == "wall" || F.Kind == "retaining_wall", Hedge = F.Kind == "hedge", Fence = F.Kind == "fence";
		const bool Barrier = F.Kind == "guard_rail" || F.Kind == "concrete_barrier";   // [OSM stage 5C]
		if (!(Wall || Hedge || Fence || Barrier) || F.Line.size() < 2) continue;
		long long Last = -1;
		bool Any = false;
		for (size_t I = 0; I + 1 < F.Line.size(); ++I)
		{
			const P2 Mid = (F.Line[I] + F.Line[I + 1]) * 0.5;
			const long long Key = SceneKey(Mid.X, Mid.Z);
			if (Any && Key == Last) continue;
			std::vector<int>& Row = Scene->Lines[Key];
			if (std::find(Row.begin(), Row.end(), int(L)) == Row.end()) Row.push_back(int(L));
			Last = Key;
			Any = true;
		}
	}
	for (size_t B = 0; B < G.Buildings.size(); ++B) Scene->Buildings[SceneKey(G.Buildings[B].X, G.Buildings[B].Z)].push_back(int(B));
	for (size_t B = 0; B < G.Scenery.Buildings.size(); ++B) Scene->Looks[SceneKey(G.Scenery.Buildings[B].At.X, G.Scenery.Buildings[B].At.Z)].push_back(int(B));
	for (size_t S = 0; S < G.Streams.size(); ++S)
		for (size_t I = 0; I + 1 < G.Streams[S].Line.size(); ++I)
		{
			const P2 Mid = (G.Streams[S].Line[I] + G.Streams[S].Line[I + 1]) * 0.5;
			std::vector<int>& Row = Scene->Streams[SceneKey(Mid.X, Mid.Z)];
			if (std::find(Row.begin(), Row.end(), int(S)) == Row.end()) Row.push_back(int(S));
		}
	for (size_t S = 0; S < G.Ground.Paths.size(); ++S)
		for (size_t I = 0; I + 1 < G.Ground.Paths[S].Pts.size(); ++I)
		{
			const P2 Mid = (G.Ground.Paths[S].Pts[I] + G.Ground.Paths[S].Pts[I + 1]) * 0.5;
			std::vector<int>& Row = Scene->Paths[SceneKey(Mid.X, Mid.Z)];
			if (std::find(Row.begin(), Row.end(), int(S)) == Row.end()) Row.push_back(int(S));
		}
}

// [build 149] A length of hedge from A to B (Ya, Yb its foot's heights) as the hedge models laid end to
// end along it (beenster, build 148: the hedges "look like something from roblox"): each a 4.2 m
// length of real twigs and leaves (Tools/make_hedges.py), planted every 3.9 m or so, sunk Down into
// the ground, Size its scale (a bank's hedge is smaller), turned either way round and each its own
// model, so no two neighbours match.
void RidePlan::HedgePlants(const P2& A, const P2& B, double Ya, double Yb, double Down, double Size, RideMesh& Out) const
{
	const double Len = A.DistanceTo(B);
	if (Len < 0.4) return;
	const int Count = std::max(1, int(std::lround(Len / 3.9)));
	const double Yaw0 = std::atan2(B.Z - A.Z, B.X - A.X);
	for (int K = 0; K < Count; ++K)
	{
		const double T = (K + 0.5) / Count;
		const P2 At = A + (B - A) * T;
		const double Roll = RideSceneBits::Chance(At.X, At.Z, 71);
		RidePlant P;
		P.Kind = uint16_t(HedgeKind);
		P.X = float(At.X - Out.OriginX);
		P.Z = float(At.Z - Out.OriginZ);
		P.Y = float(Ya + (Yb - Ya) * T + Down);
		P.Yaw = float(Yaw0 + (Roll < 0.5 ? 0.0 : 3.141592653589793));
		// (as long as its share of the piece, a little over so the lengths meet; 0.55 at the least)
		P.Scale = float(Size * std::clamp(Len / Count / 3.9, 0.55, 1.12) * (0.94 + 0.12 * RideSceneBits::Chance(At.X, At.Z, 72)));
		P.Height = 0.0f;   // (Scale, not a height: PlacePlants)
		P.Pick = float(RideSceneBits::Chance(At.X, At.Z, 73));
		Out.Plants.push_back(P);
	}
}

void RidePlan::SceneryTile(double X0, double Z0, double Size, double Coarse, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	const bool Fine = Coarse <= 4.5;
	const auto Inside = [X0, Z0, Size](double X, double Z) { return X >= X0 && X < X0 + Size && Z >= Z0 && Z < Z0 + Size; };
	// (clear of every road: a wall stops at the verge, and nothing stands on the tarmac)
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const P3 Sky = At3(X0, 1e6, Z0);
	// (how far from a railway's middle: a railway drawn, or a disused one's bed)
	const auto RailClear = [this](double X, double Z)
	{
		double Dist = 0.0, Top = 0.0;
		int On = 0;
		return NearRail(X, Z, Dist, Top, On) && On != 2 ? Dist : 1e9;
	};
	// The local look: the walls' and houses' stone, the houses' walling and roofs (Ride UK stage 5).
	const LocalLook& Look = LookHere(X0 + Size * 0.5, Z0 + Size * 0.5);
	Out.StoneTint = Look.WallStone;
	Out.MasonryTint = Look.HouseStone;
	const bool Banks = Look.Boundary == LocalLook::HedgeBank;
	const int WallKind = WallStyle(Look);
	std::shared_ptr<RideMesh> Washed = std::make_shared<RideMesh>();
	Washed->OriginX = X0;
	Washed->OriginZ = Z0;
	// (the dry stone walls and the stone houses go in meshes of their own, in the scanned stone)
	std::shared_ptr<RideMesh> Walls = std::make_shared<RideMesh>(), Houses = std::make_shared<RideMesh>();
	Walls->OriginX = Houses->OriginX = X0;
	Walls->OriginZ = Houses->OriginZ = Z0;
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));

	// Walls, hedges and fences: each mapped line, a bit at a time over the ground.
	static const Rgb Limestone = {0.235f, 0.225f, 0.205f}, HedgeGreen = {0.035f, 0.065f, 0.02f}, Post = {0.11f, 0.085f, 0.06f}, Turf = {0.07f, 0.095f, 0.035f};
	const double Step = Fine ? 4.0 : 12.0;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Lines.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Lines.end()) continue;
			for (int L : It->second)
			{
				const FeatureLine& F = G.Scenery.Lines[size_t(L)];
				const bool Hedge = F.Kind == "hedge", Fence = F.Kind == "fence";
				const bool Rail = F.Kind == "guard_rail", Concrete = F.Kind == "concrete_barrier";   // [OSM stage 5C]
				// (in the South West a mapped wall is a hedge bank: earth faced with turf, a hedge along its top)
				const bool Bank = Banks && !Fence;
				// (a dry stone wall: 1.3 m, wider at the foot; a hedge: taller and thick; a fence: posts and a rail)
				const double High = Hedge ? 1.7 : Fence ? 1.1 : (F.Kind == "retaining_wall" ? 1.0 : 1.3) * (0.92 + 0.16 * Chance(F.Line[0].X, F.Line[0].Z, 7));
				const double Foot = Hedge ? 0.7 : Fence ? 0.05 : 0.33, Top = Hedge ? 0.55 : Fence ? 0.05 : 0.22;
				const Rgb Colour = Shade(Hedge ? HedgeGreen : Fence ? Post : Limestone, 0.88 + 0.24 * Chance(F.Line[0].X, F.Line[0].Z, 3));
				// (a wall's stone: how bright, which scan, how lichened, as the hand-built ride's walls are)
				const bool IsWall = !Hedge && !Fence && !Bank;
				Rgb Mask;
				Mask.R = float(0.42 + 0.16 * Chance(F.Line[0].X, F.Line[0].Z, 3));
				Mask.G = Chance(F.Line[0].X, F.Line[0].Z, 8) < 0.5 ? 0.15f : 0.85f;
				Mask.B = float(0.2 + 0.5 * Chance(F.Line[0].X, F.Line[0].Z, 9));
				RideMesh& Into = IsWall ? *Walls : Out;
				// (beenster, build 147: "wall ends are missing". Where a dry stone wall stops, at a gateway, a
				// road, a railway or its own end, it is closed off with an end face. Whether the next piece of
				// it is drawn is asked of the line itself, so a wall running on into the next square has none.)
				const size_t LineN = F.Line.size();
				const bool Closed = LineN > 2 && F.Line.front().DistanceTo(F.Line.back()) < 0.05;
				const auto BitsOf = [&](size_t Si) { return std::max(1, int(std::ceil(F.Line[Si].DistanceTo(F.Line[Si + 1]) / Step))); };
				const auto Standing = [&](const P2& Qa, const P2& Qb)
				{
					const P2 Qm = (Qa + Qb) * 0.5;
					return RailClear(Qm.X, Qm.Z) >= 2.6 && OffRoad(Qm.X, Qm.Z, 1.0) && OffRoad(Qa.X, Qa.Z, 1.0) && OffRoad(Qb.X, Qb.Z, 1.0) &&
						!std::isnan(Ground(Qa.X, Qa.Z)) && !std::isnan(Ground(Qb.X, Qb.Z));
				};
				// (whether the wall carries on through At, from bit Kn of the line's segment Sn: its first
				// piece if FromStart, else its last)
				const auto CarriesOn = [&](const P2& At, size_t Sn, int Kn, bool FromStart)
				{
					const P2 Sa = F.Line[Sn], Sb = F.Line[Sn + 1];
					if (Sa.DistanceTo(Sb) < 0.3) return false;
					const int Bn = BitsOf(Sn);
					P2 Got[2][2];
					const int Cn = CutGaps(Sa + (Sb - Sa) * (double(Kn) / Bn), Sa + (Sb - Sa) * (double(Kn + 1) / Bn), Got);
					if (Cn == 0) return false;
					const P2 Qa = FromStart ? Got[0][0] : Got[Cn - 1][0], Qb = FromStart ? Got[0][1] : Got[Cn - 1][1];
					return (FromStart ? Qa : Qb).DistanceTo(At) < 0.05 && Standing(Qa, Qb);
				};
				for (size_t I = 0; I + 1 < F.Line.size(); ++I)
				{
					const P2 A = F.Line[I], B = F.Line[I + 1];
					const P2 Mid = (A + B) * 0.5;
					// (each bit of a line is its middle's square's, and the finer square's if both would draw it)
					if (SceneKey(Mid.X, Mid.Z) != It->first || !Inside(Mid.X, Mid.Z)) continue;
					const double Len = A.DistanceTo(B);
					if (Len < 0.3) continue;
					const int Bits = std::max(1, int(std::ceil(Len / Step)));
					const P2 Dir = (B - A) * (1.0 / Len);
					const P2 Side(Dir.Z, -Dir.X);
					for (int K = 0; K < Bits; ++K)
					{
						// [OSM stage 2] Less the gateway of any gate or stile that stands in it: up to two pieces.
						P2 Pieces[2][2];
						const int Count = CutGaps(A + (B - A) * (double(K) / Bits), A + (B - A) * (double(K + 1) / Bits), Pieces);
						for (int Pc = 0; Pc < Count; ++Pc)
						{
						const P2 Pa = Pieces[Pc][0], Pb = Pieces[Pc][1];
						const P2 Pm = (Pa + Pb) * 0.5;
						if (RailClear(Pm.X, Pm.Z) < 2.6) continue;   // (nor across a railway)
						const double Spare = Hedge ? 1.6 : Rail || Concrete ? 0.2 : 1.0;
						if (!OffRoad(Pm.X, Pm.Z, Spare) || !OffRoad(Pa.X, Pa.Z, Spare) || !OffRoad(Pb.X, Pb.Z, Spare)) continue;
						const double Ga = Ground(Pa.X, Pa.Z), Gb = Ground(Pb.X, Pb.Z);
						if (std::isnan(Ga) || std::isnan(Gb)) continue;
						if (Rail || Concrete)
						{
							// [OSM stage 5C] A crash barrier: a steel beam on a post at each bit's start, a face each
							// side (0.45 to 0.78 m up), or a concrete step barrier (0.6 m at its foot, 0.24 m at
							// its top, 0.9 m high).
							static const Rgb Steel = {0.27f, 0.28f, 0.29f}, PostGrey = {0.2f, 0.2f, 0.21f}, Cast = {0.32f, 0.31f, 0.29f};
							if (Concrete)
							{
								for (double Sg : {1.0, -1.0})
								{
									const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 0.5, Pm.Z + Side.Z * Sg * 50.0);
									Quad(Out, At3(Pa.X + Side.X * Sg * 0.3, Ga - 0.05, Pa.Z + Side.Z * Sg * 0.3), At3(Pb.X + Side.X * Sg * 0.3, Gb - 0.05, Pb.Z + Side.Z * Sg * 0.3),
										At3(Pb.X + Side.X * Sg * 0.2, Gb + 0.25, Pb.Z + Side.Z * Sg * 0.2), At3(Pa.X + Side.X * Sg * 0.2, Ga + 0.25, Pa.Z + Side.Z * Sg * 0.2), Far, Shade(Cast, 0.95), false);
									Quad(Out, At3(Pa.X + Side.X * Sg * 0.2, Ga + 0.25, Pa.Z + Side.Z * Sg * 0.2), At3(Pb.X + Side.X * Sg * 0.2, Gb + 0.25, Pb.Z + Side.Z * Sg * 0.2),
										At3(Pb.X + Side.X * Sg * 0.12, Gb + 0.9, Pb.Z + Side.Z * Sg * 0.12), At3(Pa.X + Side.X * Sg * 0.12, Ga + 0.9, Pa.Z + Side.Z * Sg * 0.12), Far, Cast, false);
								}
								Quad(Out, At3(Pa.X + Side.X * 0.12, Ga + 0.9, Pa.Z + Side.Z * 0.12), At3(Pb.X + Side.X * 0.12, Gb + 0.9, Pb.Z + Side.Z * 0.12),
									At3(Pb.X - Side.X * 0.12, Gb + 0.9, Pb.Z - Side.Z * 0.12), At3(Pa.X - Side.X * 0.12, Ga + 0.9, Pa.Z - Side.Z * 0.12), Sky, Shade(Cast, 1.1), false);
								continue;
							}
							const P2 Along = Dir * 0.06, Across = Side * 0.06;
							const P3 Out3 = At3(Pa.X + Side.X * 5.0, Ga + 0.5, Pa.Z + Side.Z * 5.0);
							Quad(Out, At3(Pa.X - Along.X, Ga - 0.2, Pa.Z - Along.Z), At3(Pa.X + Along.X, Ga - 0.2, Pa.Z + Along.Z), At3(Pa.X + Along.X, Ga + 0.74, Pa.Z + Along.Z),
								At3(Pa.X - Along.X, Ga + 0.74, Pa.Z - Along.Z), Out3, PostGrey, true);
							Quad(Out, At3(Pa.X - Across.X, Ga - 0.2, Pa.Z - Across.Z), At3(Pa.X + Across.X, Ga - 0.2, Pa.Z + Across.Z), At3(Pa.X + Across.X, Ga + 0.74, Pa.Z + Across.Z),
								At3(Pa.X - Across.X, Ga + 0.74, Pa.Z - Across.Z), Out3, PostGrey, true);
							for (double Sg : {1.0, -1.0})
							{
								const P2 Fa = Pa + Side * (0.12 * Sg), Fb = Pb + Side * (0.12 * Sg);
								const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 0.6, Pm.Z + Side.Z * Sg * 50.0);
								Quad(Out, At3(Fa.X, Ga + 0.45, Fa.Z), At3(Fb.X, Gb + 0.45, Fb.Z), At3(Fb.X, Gb + 0.78, Fb.Z), At3(Fa.X, Ga + 0.78, Fa.Z), Far, Steel, false);
							}
							continue;
						}
						if (Fence)
						{
							// A post at the bit's start and a rail to the next.
							const P2 Along = Dir * 0.05, Across = Side * 0.05;
							const P3 Out3 = At3(Pa.X + Side.X * 5.0, Ga + 0.5, Pa.Z + Side.Z * 5.0);
							Quad(Out, At3(Pa.X - Along.X, Ga - 0.2, Pa.Z - Along.Z), At3(Pa.X + Along.X, Ga - 0.2, Pa.Z + Along.Z), At3(Pa.X + Along.X, Ga + High, Pa.Z + Along.Z),
								At3(Pa.X - Along.X, Ga + High, Pa.Z - Along.Z), Out3, Colour, true);
							Quad(Out, At3(Pa.X - Across.X, Ga - 0.2, Pa.Z - Across.Z), At3(Pa.X + Across.X, Ga - 0.2, Pa.Z + Across.Z), At3(Pa.X + Across.X, Ga + High, Pa.Z + Across.Z),
								At3(Pa.X - Across.X, Ga + High, Pa.Z - Across.Z), Out3, Colour, true);
							if (Fine)
								Quad(Out, At3(Pa.X, Ga + High - 0.22, Pa.Z), At3(Pb.X, Gb + High - 0.22, Pb.Z), At3(Pb.X, Gb + High - 0.12, Pb.Z), At3(Pa.X, Ga + High - 0.12, Pa.Z), Out3, Colour, true);
							continue;
						}
						// A length of it: two faces leaning in from Foot to Top, Lo to Hi over the ground, and its top.
						const auto Stretch = [&](double FootW, double TopW, double Lo, double Hi, const Rgb& Col, bool Scanned)
						{
							for (double Sg : {1.0, -1.0})
							{
								const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 1.0, Pm.Z + Side.Z * Sg * 50.0);
								Quad(Into, At3(Pa.X + Side.X * Sg * FootW, Ga + Lo, Pa.Z + Side.Z * Sg * FootW), At3(Pb.X + Side.X * Sg * FootW, Gb + Lo, Pb.Z + Side.Z * Sg * FootW),
									At3(Pb.X + Side.X * Sg * TopW, Gb + Hi, Pb.Z + Side.Z * Sg * TopW), At3(Pa.X + Side.X * Sg * TopW, Ga + Hi, Pa.Z + Side.Z * Sg * TopW), Far,
									Scanned ? Mask : Shade(Col, Sg > 0.0 ? 1.0 : 0.9), false, Scanned ? 0.22f : -1.0f);
							}
							Quad(Into, At3(Pa.X + Side.X * TopW, Ga + Hi, Pa.Z + Side.Z * TopW), At3(Pb.X + Side.X * TopW, Gb + Hi, Pb.Z + Side.Z * TopW),
								At3(Pb.X - Side.X * TopW, Gb + Hi, Pb.Z - Side.Z * TopW), At3(Pa.X - Side.X * TopW, Ga + Hi, Pa.Z - Side.Z * TopW), Sky, Scanned ? Mask : Shade(Col, 1.12), false, Scanned ? 0.45f : -1.0f);
						};
						// (beenster, build 141: hedges "very low poly". On the finest squares a hedge is a lumpy,
						// rounded mass: every metre or so its own height and width, a rounded top over sides that
						// bulge, each facet its own shade of leaf; flat boxes only further out)
						const auto Leafy = [&](double Lo, double High2, double HalfW)
						{
							const double Len2 = Pa.DistanceTo(Pb);
							const int Cuts = std::max(1, int(std::ceil(Len2 / 1.1)));
							// (the section: across (x of the half width) and up (of the height), foot to foot over the top)
							static constexpr double Prof[7][2] = {{-1.0, 0.0}, {-1.12, 0.45}, {-0.82, 0.85}, {0.0, 1.0}, {0.82, 0.85}, {1.12, 0.45}, {1.0, 0.0}};
							P3 Prev[7];
							for (int Cut = 0; Cut <= Cuts; ++Cut)
							{
								const double T = double(Cut) / Cuts;
								const P2 C = Pa + (Pb - Pa) * T;
								const double Gc = Ga + (Gb - Ga) * T;
								// (the lumps: keyed to the spot, so the next piece of the line meets this one)
								const double Hh = High2 * (0.82 + 0.36 * Chance(std::round(C.X * 2.0), std::round(C.Z * 2.0), 41));
								const double Ww = HalfW * (0.8 + 0.4 * Chance(std::round(C.X * 2.0), std::round(C.Z * 2.0), 42));
								const double Lean = (Chance(std::round(C.X * 2.0), std::round(C.Z * 2.0), 43) - 0.5) * 0.3 * HalfW;
								P3 Here[7];
								for (int Pk = 0; Pk < 7; ++Pk)
								{
									const double Ac = Prof[Pk][0] * Ww + Prof[Pk][1] * Lean;
									Here[Pk] = At3(C.X + Side.X * Ac, Gc + Lo + Prof[Pk][1] * Hh, C.Z + Side.Z * Ac);
								}
								if (Cut > 0)
									for (int Pk = 0; Pk + 1 < 7; ++Pk)
									{
										const double Up = (Prof[Pk][1] + Prof[Pk + 1][1]) * 0.5;
										const double Sgn = Prof[Pk][0] + Prof[Pk + 1][0] < 0.0 ? -1.0 : 1.0;
										const P3 Toward = At3(C.X + Side.X * Sgn * 50.0, Gc + Lo + Hh * Up + (Up > 0.9 ? 50.0 : 0.0), C.Z + Side.Z * Sgn * 50.0);
										const double Lit = (0.62 + 0.45 * Up) * (Sgn > 0.0 ? 1.0 : 0.9) * (0.8 + 0.4 * Chance(C.X + Pk, C.Z, 44));
										const Rgb Leaf = Chance(C.X, C.Z + Pk, 45) < 0.18 ? Rgb{0.05f, 0.07f, 0.022f} : HedgeGreen;
										Quad(Out, Prev[Pk], Here[Pk], Here[Pk + 1], Prev[Pk + 1], Toward, Shade(Leaf, Lit));
									}
								for (int Pk = 0; Pk < 7; ++Pk) Prev[Pk] = Here[Pk];
							}
						};
						if (Bank)
						{
							// (a bank about 1.4 m, a metre wide at its top, and the hedge on it; near the road its
							// faces are stone under moss and turf, as a Cornish hedge's are)
							const double BankHigh = 1.3 + 0.3 * Chance(F.Line[0].X, F.Line[0].Z, 21);
							if (Fine && Walls)
							{
								Rgb Faced = Mask;
								Faced.G = 0.15f;
								for (double Sg : {1.0, -1.0})
								{
									const P3 Far = At3(Pm.X + Side.X * Sg * 50.0, Ga + 1.0, Pm.Z + Side.Z * Sg * 50.0);
									Quad(*Walls, At3(Pa.X + Side.X * Sg * 1.15, Ga - 0.4, Pa.Z + Side.Z * Sg * 1.15), At3(Pb.X + Side.X * Sg * 1.15, Gb - 0.4, Pb.Z + Side.Z * Sg * 1.15),
										At3(Pb.X + Side.X * Sg * 0.55, Gb + BankHigh, Pb.Z + Side.Z * Sg * 0.55), At3(Pa.X + Side.X * Sg * 0.55, Ga + BankHigh, Pa.Z + Side.Z * Sg * 0.55), Far, Faced, false, 0.8f);
								}
								Quad(Out, At3(Pa.X + Side.X * 0.55, Ga + BankHigh, Pa.Z + Side.Z * 0.55), At3(Pb.X + Side.X * 0.55, Gb + BankHigh, Pb.Z + Side.Z * 0.55),
									At3(Pb.X - Side.X * 0.55, Gb + BankHigh, Pb.Z - Side.Z * 0.55), At3(Pa.X - Side.X * 0.55, Ga + BankHigh, Pa.Z - Side.Z * 0.55), Sky, Turf);
								if (RealHedges) HedgePlants(Pa, Pb, Ga + BankHigh, Gb + BankHigh, -0.25, 0.7, Out);
								else Leafy(BankHigh - 0.1, 0.9 + 0.5 * Chance(F.Line[0].X, F.Line[0].Z, 23), 0.5);
							}
							else Stretch(1.15, 0.55, -0.4, BankHigh, Shade(Turf, 0.85 + 0.3 * Chance(F.Line[0].X, F.Line[0].Z, 22)), false);
							continue;
						}
						if (Hedge && Fine && RealHedges)
						{
							HedgePlants(Pa, Pb, Ga, Gb, -0.05, 1.0, Out);
							continue;
						}
						if (Hedge && Fine)
						{
							Leafy(-0.3, High + 0.3, Foot);
							continue;
						}
						// [build 154] (beenster: "the walls remain too low def; I remember how detailed they were in
						// Buttertubs", and "don't just use Buttertubs walls, they're for Yorkshire": on the finest squares
						// a dry stone wall is laid stone by stone, in its region's walling, Tools/make_wall_kit.py)
						const bool Kitted = IsWall && Fine && PropModelled[PropWallStone] && PropModelled[PropWallEnd];
						if (!Kitted) Stretch(Foot, Top, -0.4, High, Colour, IsWall);
						if (IsWall && Fine)
						{
							const P2 BitA = A + (B - A) * (double(K) / Bits), BitB = A + (B - A) * (double(K + 1) / Bits);
							bool OnBefore = false, OnAfter = false;
							if (Pc == 0 && Pa.DistanceTo(BitA) < 0.05)
							{
								if (K > 0) OnBefore = CarriesOn(Pa, I, K - 1, false);
								else if (I > 0) OnBefore = CarriesOn(Pa, I - 1, BitsOf(I - 1) - 1, false);
								else if (Closed) OnBefore = CarriesOn(Pa, LineN - 2, BitsOf(LineN - 2) - 1, false);
							}
							if (Pc == Count - 1 && Pb.DistanceTo(BitB) < 0.05)
							{
								if (K + 1 < Bits) OnAfter = CarriesOn(Pb, I, K + 1, true);
								else if (I + 2 < LineN) OnAfter = CarriesOn(Pb, I + 1, 0, true);
								else if (Closed) OnAfter = CarriesOn(Pb, 0, 0, true);
							}
							if (Kitted)
							{
								// (its whole 2 m lengths, stretched to fit the piece, each pitched up the slope between its
								// ends; where it stops, a wall head, finished outward, over its last metre)
								const auto Lay = [&](int Model, int Variant, const P2& Q0, const P2& Q1)
								{
									const double Y0 = Ground(Q0.X, Q0.Z), Y1 = Ground(Q1.X, Q1.Z), Run = Q0.DistanceTo(Q1);
									if (std::isnan(Y0) || std::isnan(Y1) || Run < 0.05) return;
									const P2 Qm = (Q0 + Q1) * 0.5;
									RideProp Pr;
									Pr.Kind = uint16_t(Model);
									Pr.Variant = uint8_t(Variant);
									Pr.Mapped = false;
									Pr.X = float(Qm.X - Out.OriginX);
									Pr.Y = float((Y0 + Y1) * 0.5 - 0.02);
									Pr.Z = float(Qm.Z - Out.OriginZ);
									Pr.Yaw = float(std::atan2(Q1.Z - Q0.Z, Q1.X - Q0.X));
									Pr.Pitch = float(std::atan2(Y1 - Y0, Run));
									Pr.Stretch = float(Run / (Model == PropWallEnd ? 1.0 : 2.0));
									Out.Props.push_back(Pr);
								};
								const double Run = Pa.DistanceTo(Pb);
								const bool HeadA = !OnBefore && Run >= 1.6, HeadB = !OnAfter && Run >= (HeadA ? 2.6 : 1.6);
								const P2 La = HeadA ? Pa + Dir * 1.0 : Pa, Lb = HeadB ? Pb - Dir * 1.0 : Pb;
								const double Lr = La.DistanceTo(Lb);
								const int Lengths = Lr > 0.4 ? std::max(1, int(std::ceil(Lr / 2.0 - 0.2))) : 0;   // (each 0.7 to 1.25 of its own length)
								for (int Ln = 0; Ln < Lengths; ++Ln)
								{
									const P2 Q0 = La + (Lb - La) * (double(Ln) / Lengths), Q1 = La + (Lb - La) * (double(Ln + 1) / Lengths);
									Lay(PropWallStone, WallKind * WallVariants + (int(Chance(Q0.X, Q0.Z, 51) * WallVariants) % WallVariants), Q0, Q1);
								}
								if (HeadA) Lay(PropWallEnd, WallKind * WallVariants + (int(Chance(Pa.X, Pa.Z, 52) * WallVariants) % WallVariants), La, Pa);
								if (HeadB) Lay(PropWallEnd, WallKind * WallVariants + (int(Chance(Pb.X, Pb.Z, 52) * WallVariants) % WallVariants), Lb, Pb);
								continue;
							}
							// (an end face: across the wall, its foot to its top, and the top stones' end above it)
							for (int End = 0; End < 2; ++End)
							{
								if (End == 0 ? OnBefore : OnAfter) continue;
								const P2 E = End == 0 ? Pa : Pb;
								const double Ge = End == 0 ? Ga : Gb;
								const double Outward = End == 0 ? -1.0 : 1.0;
								const P3 Away = At3(E.X + Dir.X * Outward * 50.0, Ge + 0.6, E.Z + Dir.Z * Outward * 50.0);
								Quad(Into, At3(E.X + Side.X * Foot, Ge - 0.4, E.Z + Side.Z * Foot), At3(E.X - Side.X * Foot, Ge - 0.4, E.Z - Side.Z * Foot),
									At3(E.X - Side.X * Top, Ge + High, E.Z - Side.Z * Top), At3(E.X + Side.X * Top, Ge + High, E.Z + Side.Z * Top), Away, Mask, false, 0.22f);
							}
						}
						// (beenster, build 141: walls "low def". Near a road on the finest squares, a dry stone wall
						// has its coping: a row of upright stones along its top, each its own height and lean)
						if (IsWall && Step <= 4.5)
						{
							NearOpts Opts;
							NearHit Hit;
							// (beenster, build 147: 20 fps at Whaley Bridge. The top stones were most of a wall's triangles:
							// within 30 m of the road now, each a little longer)
							if (NearestOf(Pm.X, Pm.Z, Opts, Hit) && Hit.Dist < Hit.Half + 30.0)
							{
								const int Stones = std::max(1, int(Pa.DistanceTo(Pb) / 0.42));
								const double Cw = Top * 1.15;
								for (int Sn = 0; Sn < Stones; ++Sn)
								{
									const double T0 = (Sn + 0.04) / Stones, T1 = (Sn + 0.96) / Stones;
									const P2 S0 = Pa + (Pb - Pa) * T0, S1 = Pa + (Pb - Pa) * T1;
									const double Y0 = Ga + (Gb - Ga) * T0 + High - 0.02, Y1 = Ga + (Gb - Ga) * T1 + High - 0.02;
									const double Up = 0.16 + 0.14 * Chance(S0.X, S0.Z, 31), Tip = (Chance(S0.X, S0.Z, 32) - 0.5) * 0.06;
									const P2 Lean = Dir * Tip;
									const P3 Sky3 = At3(S0.X, Y0 + 50.0, S0.Z);
									for (double Sg : {1.0, -1.0})
									{
										const P3 Far = At3(S0.X + Side.X * Sg * 50.0, Y0, S0.Z + Side.Z * Sg * 50.0);
										Quad(Into, At3(S0.X + Side.X * Sg * Cw, Y0, S0.Z + Side.Z * Sg * Cw), At3(S1.X + Side.X * Sg * Cw, Y1, S1.Z + Side.Z * Sg * Cw),
											At3(S1.X + Lean.X + Side.X * Sg * Cw * 0.8, Y1 + Up, S1.Z + Lean.Z + Side.Z * Sg * Cw * 0.8),
											At3(S0.X + Lean.X + Side.X * Sg * Cw * 0.8, Y0 + Up, S0.Z + Lean.Z + Side.Z * Sg * Cw * 0.8), Far, Mask, false, 0.3f);
										// (its two ends, so the gaps between the stones show)
										const P2 Se = Sg > 0.0 ? S1 : S0;
										const double Ye = Sg > 0.0 ? Y1 : Y0;
										const P3 EndOut = At3(Se.X + Dir.X * Sg * 50.0, Ye, Se.Z + Dir.Z * Sg * 50.0);
										Quad(Into, At3(Se.X + Side.X * Cw, Ye, Se.Z + Side.Z * Cw), At3(Se.X - Side.X * Cw, Ye, Se.Z - Side.Z * Cw),
											At3(Se.X + Lean.X - Side.X * Cw * 0.8, Ye + Up, Se.Z + Lean.Z - Side.Z * Cw * 0.8), At3(Se.X + Lean.X + Side.X * Cw * 0.8, Ye + Up, Se.Z + Lean.Z + Side.Z * Cw * 0.8),
											EndOut, Mask, false, 0.3f);
									}
									Quad(Into, At3(S0.X + Lean.X + Side.X * Cw * 0.8, Y0 + Up, S0.Z + Lean.Z + Side.Z * Cw * 0.8), At3(S1.X + Lean.X + Side.X * Cw * 0.8, Y1 + Up, S1.Z + Lean.Z + Side.Z * Cw * 0.8),
										At3(S1.X + Lean.X - Side.X * Cw * 0.8, Y1 + Up, S1.Z + Lean.Z - Side.Z * Cw * 0.8), At3(S0.X + Lean.X - Side.X * Cw * 0.8, Y0 + Up, S0.Z + Lean.Z - Side.Z * Cw * 0.8),
										Sky3, Mask, false, 0.5f);
								}
							}
						}
											}
					}
				}
			}
		}

	// Buildings (World/WorldRideBuildings.cpp: OSM stage 3).
	std::shared_ptr<RideMesh> Roofs = std::make_shared<RideMesh>(), Bricks = std::make_shared<RideMesh>();
	Roofs->OriginX = Bricks->OriginX = X0;
	Roofs->OriginZ = Bricks->OriginZ = Z0;
	// (hub, build 148: the buildings were the game thread's biggest cost in towns, 32 ms a frame in York;
	// past the finest squares, 400 m and more off, each is a plain block under its roof, as further out)
	if (Fine) BuildingsTile(X0, Z0, Size, Fine, Look, Out, *Washed, *Houses, *Roofs, *Bricks);
	else FarBuildingsTile(X0, Z0, Size, Coarse, 0.0, Out, 8000);

	// Trees, where the map's canopy says there are some: stand-in shapes on a scattered grid, as tall
	// as the canopy, broadleaved (round) or needleleaved (pointed).
	if (Land->HasTrees() && !RealTrees)
	{
		static const Rgb Bark = {0.06f, 0.045f, 0.03f}, Leaf = {0.045f, 0.085f, 0.025f}, Needle = {0.02f, 0.05f, 0.025f};
		const double Grid = Fine ? 9.0 : 22.0;
		const int Gx0 = int(std::ceil(X0 / Grid)), Gx1 = int(std::ceil((X0 + Size) / Grid)), Gz0 = int(std::ceil(Z0 / Grid)), Gz1 = int(std::ceil((Z0 + Size) / Grid));
		for (int Gz = Gz0; Gz < Gz1; ++Gz)
			for (int Gx = Gx0; Gx < Gx1; ++Gx)
			{
				const double Jx = Gx * Grid, Jz = Gz * Grid;
				const double X = Jx + (Chance(Jx, Jz, 11) - 0.5) * Grid * 0.8, Z = Jz + (Chance(Jx, Jz, 12) - 0.5) * Grid * 0.8;
				if (!Inside(X, Z)) continue;
				const int Trees = Land->TreesAt(X, Z);
				if (Trees <= 0) continue;
				const double Tall = double(Trees & 31) * 2.0, Share = double((Trees >> 5) & 7) / 8.0;
				if (Tall < 3.0 || Chance(Jx, Jz, 13) > Share * 1.2 || !OffRoad(X, Z, 3.0) || RailClear(X, Z) < 5.0) continue;
				const double Base = Ground(X, Z);
				if (std::isnan(Base)) continue;
				const bool Pointed = ((Trees >> 8) & 3) == 2 || (((Trees >> 8) & 3) == 3 && Chance(Jx, Jz, 14) < 0.5);
				const double High = Tall * (0.8 + 0.35 * Chance(Jx, Jz, 15)) * (Fine ? 1.0 : 1.05);
				const double Wide = (Pointed ? High * 0.19 : High * 0.33) * (Fine ? 1.0 : 1.5), Trunk = High * (Pointed ? 0.18 : 0.3);
				const Rgb Green = Shade(Pointed ? Needle : Leaf, 0.75 + 0.5 * Chance(Jx, Jz, 16));
				const double Turn = Chance(Jx, Jz, 17) * 1.5;
				const int Faces = Fine ? 6 : 4;
				const auto Ring = [&](int K, double Radius, double Y) { const double A = Turn + 6.283185307179586 * K / Faces; return At3(X + std::cos(A) * Radius, Base + Y, Z + std::sin(A) * Radius); };
				const P3 Axis = At3(X, Base + High * 0.6, Z);
				const auto Away = [&Axis](const P3& A, const P3& C) { return At3(Axis.X + ((A.X + C.X) * 0.5 - Axis.X) * 50.0, (A.Y + C.Y) * 0.5, Axis.Z + ((A.Z + C.Z) * 0.5 - Axis.Z) * 50.0); };
				for (int K = 0; K < Faces; ++K)
				{
					if (Fine)
					{
						const P3 T0 = Ring(K, 0.035 * High, -0.3), T1 = Ring(K + 1, 0.035 * High, -0.3), T2 = Ring(K + 1, 0.025 * High, Trunk + 0.3), T3 = Ring(K, 0.025 * High, Trunk + 0.3);
						Quad(Out, T0, T1, T2, T3, Away(T0, T2), Bark);
					}
					const double Lit = 0.8 + 0.4 * (0.5 + 0.5 * std::cos(Turn + 6.283185307179586 * (K + 0.5) / Faces - 0.8));
					if (Pointed)
					{
						const P3 C0 = Ring(K, Wide, Trunk), C1 = Ring(K + 1, Wide, Trunk), Tip = At3(X, Base + High, Z);
						Quad(Out, C0, C1, Tip, C0, Away(C0, C1), Shade(Green, Lit));
					}
					else
					{
						// (a crown in three rings: narrow where it leaves the trunk, widest a little over half way, closed at the top)
						const double Mid = Trunk + (High - Trunk) * 0.45;
						const P3 A0 = Ring(K, Wide * 0.45, Trunk), A1 = Ring(K + 1, Wide * 0.45, Trunk), B0 = Ring(K, Wide, Mid), B1 = Ring(K + 1, Wide, Mid), Tip = At3(X, Base + High, Z);
						Quad(Out, A0, A1, B1, B0, Away(A0, B1), Shade(Green, Lit * 0.8));
						const P3 U0 = Ring(K, Wide * 0.62, Trunk + (High - Trunk) * 0.82), U1 = Ring(K + 1, Wide * 0.62, Trunk + (High - Trunk) * 0.82);
						Quad(Out, B0, B1, U1, U0, Away(B0, U1), Shade(Green, Lit));
						Quad(Out, U0, U1, Tip, U0, Sky, Shade(Green, Lit * 1.12));
					}
				}
			}
	}
	RailTile(X0, Z0, Size, Fine, Out, *Walls);
	FurnitureTile(X0, Z0, Size, Fine, Out);
	if (Walls->Vertices() > 0) Out.Stone = Walls;
	if (Houses->Vertices() > 0) Out.Masonry = Houses;
	if (Washed->Vertices() > 0) Out.Render = Washed;
	if (Roofs->Vertices() > 0) Out.Roofing = Roofs;
	if (Bricks->Vertices() > 0) Out.Brickwork = Bricks;
}

// --- The road's furniture (stage 5) -----------------------------------------------------------------

int RidePlan::FurnitureCount(int Kind) const
{
	int N = 0;
	for (const Furnish& F : Furniture) N += F.Kind == Kind ? 1 : 0;
	return N;
}

bool RidePlan::StreetClass(const std::string& Class)
{
	return Class == "residential" || Class == "living_street" || Class == "unclassified" || Class == "tertiary" || Class == "secondary" || Class == "primary"
		|| Class == "pedestrian";
}

std::vector<char> RidePlan::StreetsAlong(int Which) const
{
	// [OSM stage 5D] Built up as the ride's road is (IndexFurniture), closer in: four buildings or more
	// within 45 m of the point, gaps under 60 m closed, stretches under 40 m left out.
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	std::vector<char> Out(Pts.size(), 0);
	if (!Scene || Pts.empty()) return Out;
	const Region& G = Area.Roads;
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		const P2 P = Pts[I].At;
		int Count = 0;
		for (int Dz = -1; Dz <= 1 && Count < 4; ++Dz)
			for (int Dx = -1; Dx <= 1 && Count < 4; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
					if (std::hypot(G.Buildings[size_t(B)].X - P.X, G.Buildings[size_t(B)].Z - P.Z) < 45.0 && ++Count >= 4) break;
			}
		Out[I] = Count >= 4 ? 1 : 0;
	}
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		const char Want = Pass == 0 ? 0 : 1;
		for (size_t I = 0; I < Out.size();)
		{
			if (Out[I] != Want) { ++I; continue; }
			size_t J = I;
			while (J + 1 < Out.size() && Out[J + 1] == Want) ++J;
			const double Long = Pts[J].Along - Pts[I].Along;
			// (a gap inside a street closed; a short street dropped)
			if (Pass == 0 && I > 0 && J + 1 < Out.size() && Long < 60.0) for (size_t K = I; K <= J; ++K) Out[K] = 1;
			if (Pass == 1 && Long < 40.0 && !(I == 0 && J + 1 == Out.size() && Long > 15.0)) for (size_t K = I; K <= J; ++K) Out[K] = 0;
			I = J + 1;
		}
	}
	return Out;
}

void RidePlan::IndexFurniture()
{
	Furniture.clear();
	BuiltUp.assign(Road.size(), 0);
	if (!Scene || Road.size() < 2) return;
	const Region& G = Area.Roads;
	const auto Put = [this](Furnish F)
	{
		if (std::isnan(F.Height)) return;
		Scene->Furniture[SceneKey(F.At.X, F.At.Z)].push_back(int(Furniture.size()));
		Furniture.push_back(std::move(F));
	};
	const auto Dir = [this](size_t I)
	{
		const size_t A = I > 0 ? I - 1 : 0, B = std::min(Road.size() - 1, I + 1);
		const P2 D = Road[B].At - Road[A].At;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const auto OffAll = [this](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	// Built up: six buildings or more within 70 m of the road, gaps under 120 m closed, stretches
	// under 150 m left out.
	{
		std::vector<unsigned char> Busy(Road.size(), 0);
		for (size_t I = 0; I < Road.size(); ++I)
		{
			const P2 P = Road[I].At;
			int Count = 0;
			for (int Dz = -1; Dz <= 1 && Count < 6; ++Dz)
				for (int Dx = -1; Dx <= 1 && Count < 6; ++Dx)
				{
					const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
					if (It == Scene->Buildings.end()) continue;
					for (int B : It->second)
						if (std::hypot(G.Buildings[size_t(B)].X - P.X, G.Buildings[size_t(B)].Z - P.Z) < 70.0 && ++Count >= 6) break;
				}
			Busy[I] = Count >= 6 ? 1 : 0;
		}
		const auto Runs = [](const std::vector<unsigned char>& Of, unsigned char Want)
		{
			std::vector<std::pair<size_t, size_t>> Out;
			for (size_t I = 0; I < Of.size();)
			{
				if (Of[I] != Want) { ++I; continue; }
				size_t J = I;
				while (J + 1 < Of.size() && Of[J + 1] == Want) ++J;
				Out.emplace_back(I, J);
				I = J + 1;
			}
			return Out;
		};
		for (const auto& Gap : Runs(Busy, 0))
			if (Gap.first > 0 && Gap.second + 1 < Busy.size() && Road[Gap.second].Along - Road[Gap.first].Along < 120.0)
				for (size_t I = Gap.first; I <= Gap.second; ++I) Busy[I] = 1;
		for (const auto& Run : Runs(Busy, 1))
			if (Road[Run.second].Along - Road[Run.first].Along >= 150.0)
				for (size_t I = Run.first; I <= Run.second; ++I) BuiltUp[I] = 1;
	}
	// [OSM stage 1] The limit all along it, now the built-up stretches are known.
	SettleLimits();
	const auto PointAt = [this](double Along)
	{
		size_t I = 0;
		while (I + 1 < Road.size() && Road[I + 1].Along <= Along) ++I;
		return I;
	};
	const auto Verge = [&](size_t I, double Side, double Out)
	{
		// (Side +1 the left of the way the road runs, -1 its right; Out metres past the edge)
		const P2 D = Dir(I);
		const P2 L(D.Z, -D.X);
		return Road[I].At + L * (Side * (Road[I].Half + Out));
	};
	// Coming into each built-up stretch, each way: the place's name. In Britain it stands on the left
	// of the traffic that reads it, [stage 8] elsewhere on the side the traffic keeps to. [OSM stage 1]
	// The limit signs go where the limit changes (Limits), below.
	const double Keep = KeepLeft() ? 1.0 : -1.0;
	const bool Uk = Britain();
	// [OSM stage 2] The street lamps the map has, near a road.
	MappedLamps.clear();
	for (const FeaturePoint& P : G.Scenery.Points)
		if (P.Kind == "street_lamp") MappedLamps.push_back(P.At);
	MappedLeft.assign(G.Scenery.Points.size(), 0);
	for (size_t I = 0; I < Road.size(); ++I)
	{
		if (!BuiltUp[I] || (I > 0 && BuiltUp[I - 1])) continue;
		size_t J = I;
		while (J + 1 < Road.size() && BuiltUp[J + 1]) ++J;
		// (named by the nearest of the map's places to its middle)
		const P2 Mid = Road[(I + J) / 2].At;
		std::string Called;
		double Best = 2500.0;
		for (const TorqueFit::Json& Pl : G.Places)
		{
			const double D = std::hypot(Pl.Num("x") - Mid.X, Pl.Num("z") - Mid.Z);
			if (D >= Best) continue;
			Best = D;
			Called = Pl.Str("name");
			// [OSM stage 6E] its Welsh or Gaelic name above, where the map gives a second one (a "name2" the
			// places bake carries)
			const std::string Second = Pl.Str("name2");
			if (Uk && !Second.empty() && Second != Called) Called = Second + "|" + Called;
		}
		for (int Way = 0; Way < 2; ++Way)
		{
			// (forwards it starts at I, backwards at J; its left is the forward left, then the right)
			const double Side = (Way == 0 ? 1.0 : -1.0) * Keep;
			const double Start = Way == 0 ? Road[I].Along : Road[J].Along, Back = Way == 0 ? -1.0 : 1.0;
			const auto Sign = [&](int Kind, double Along, const std::string& Words)
			{
				if (Along < 0.0 || Along > Length) return;
				const size_t K = PointAt(Along);
				Furnish F;
				F.Kind = Kind;
				// (a board stands across the way, so its near edge, not its middle, is kept clear of the road)
				const double Wide = Kind == NameSign ? NameBoardWide(Words) : 0.8;
				F.At = Verge(K, Side, 0.7 + Wide * 0.5);
				F.Face = Dir(K) * Back;   // (towards the traffic coming in)
				F.Text = Words;
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (OffAll(F.At, 0.6) && OffAll(F.At + Across * (Wide * 0.5), 0.3) && OffAll(F.At - Across * (Wide * 0.5), 0.3)) Put(F);
			};
			if (!Called.empty()) Sign(NameSign, Start + Back * 10.0, Called);
		}
		// Street lamps through it, on its left every 36 m; [OSM stage 2] none within 150 m of one the map
		// has (those stand where they are mapped, below).
		for (double Along = Road[I].Along + 12.0; Along < Road[J].Along - 6.0; Along += 36.0)
		{
			const size_t K = PointAt(Along);
			Furnish F;
			F.Kind = StreetLamp;
			F.Mapped = false;
			F.At = Verge(K, Keep, 0.9);
			if (std::any_of(MappedLamps.begin(), MappedLamps.end(), [&F](const P2& M) { return M.DistanceTo(F.At) < 150.0; })) continue;
			const P2 D = Dir(K);
			F.Face = P2(-D.Z, D.X) * Keep;   // (its arm reaches out over the road)
			F.Height = Ground(F.At.X, F.At.Z);
			if (OffAll(F.At, 0.5)) Put(F);
		}
		I = J;
	}
	// [OSM stage 1] The limit signs: where the limit changes, a pair facing each way, each showing the
	// limit its own reader is coming into (on the traffic's side); the national limit's own sign in
	// Britain at 60 and 70; none where the limit is none (a track). The map's limits where it has them,
	// else assumed: 30 where it is built up, the national limit outside.
	for (size_t K = 1; K < Limits.size(); ++K)
	{
		const double At = Limits[K].From;
		for (int Way = 0; Way < 2; ++Way)
		{
			const LimitStretch& Into = Way == 0 ? Limits[K] : Limits[K - 1];
			bool National = false;
			const int Shown = ShownLimit(Into.Kmh, &National);
			if (Shown <= 0 || At < 0.0 || At > Length) continue;
			const size_t P = PointAt(At);
			const double Side = (Way == 0 ? 1.0 : -1.0) * Keep, Back = Way == 0 ? -1.0 : 1.0;
			Furnish F;
			F.Kind = National ? LimitNational : Limit30;
			F.At = Verge(P, Side, 0.7 + 0.4);
			F.Face = Dir(P) * Back;   // (towards the traffic coming in)
			F.Text = National ? std::string() : std::to_string(Shown);
			F.Height = Ground(F.At.X, F.At.Z);
			const P2 Across(F.Face.Z, -F.Face.X);
			if (OffAll(F.At, 0.6) && OffAll(F.At + Across * 0.4, 0.3) && OffAll(F.At - Across * 0.4, 0.3)) Put(F);
		}
	}
	// Chevrons round the sharp bends: under 40 m round, turning 45 degrees or more, on the outside.
	{
		const double Pi = 3.14159265358979323846;
		std::vector<double> Radius(Road.size(), 1e9), Turn(Road.size(), 0.0);
		for (size_t I = 0; I < Road.size(); ++I)
		{
			size_t A = I, B = I;
			while (A > 0 && Road[I].Along - Road[A].Along < 15.0) --A;
			while (B + 1 < Road.size() && Road[B].Along - Road[I].Along < 15.0) ++B;
			if (A == I || B == I) continue;
			const P2 U = Road[I].At - Road[A].At, V = Road[B].At - Road[I].At;
			double T = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
			while (T > Pi) T -= 2.0 * Pi;
			while (T < -Pi) T += 2.0 * Pi;
			Turn[I] = T;
			if (std::fabs(T) > 1e-4) Radius[I] = (Road[B].Along - Road[A].Along) / std::fabs(T);
		}
		for (size_t I = 0; I < Road.size(); ++I)
		{
			if (Radius[I] >= 40.0 || BuiltUp[I]) continue;
			size_t J = I, Apex = I;
			while (J + 1 < Road.size() && Radius[J + 1] < 40.0 && (Turn[J + 1] > 0.0) == (Turn[I] > 0.0))
			{
				++J;
				if (Radius[J] < Radius[Apex]) Apex = J;
			}
			double Whole = 0.0;
			for (size_t K = I; K <= J; ++K) Whole += Turn[K] * (K > I ? (Road[K].Along - Road[K - 1].Along) : 0.0) / 30.0;
			if (std::fabs(Whole) >= 45.0 * Pi / 180.0 || Radius[Apex] < 20.0)
				for (double Off : {-12.0, 0.0, 12.0})
				{
					const size_t K = PointAt(Road[Apex].Along + Off);
					// (a right-hand bend, Turn > 0, has its outside on the left)
					Furnish F;
					F.Kind = Chevron;
					F.At = Verge(K, Turn[Apex] > 0.0 ? 1.0 : -1.0, 1.6);
					F.Face = Dir(K) * -1.0;
					F.Size = Turn[Apex] > 0.0 ? -1.0 : 1.0;   // (+1: the arrows point left, as the forward rider reads them)
					F.Height = Ground(F.At.X, F.At.Z);
					if (OffAll(F.At, 0.8)) Put(F);
				}
			I = J;
		}
	}
	// A red phone box half way through each built-up stretch of 250 m or more (most of them), on the left.
	for (size_t I = 0; I < Road.size(); ++I)
	{
		if (!BuiltUp[I] || (I > 0 && BuiltUp[I - 1])) continue;
		size_t J = I;
		while (J + 1 < Road.size() && BuiltUp[J + 1]) ++J;
		const size_t K = PointAt((Road[I].Along + Road[J].Along) * 0.5 + 20.0);
		if (Uk && Road[J].Along - Road[I].Along >= 250.0 && Chance(Road[K].At.X, Road[K].At.Z, 51) < 0.75)
		{
			Furnish F;
			F.Kind = PhoneBox;
			F.At = Verge(K, 1.0, 1.5);
			const P2 D = Dir(K);
			F.Face = P2(-D.Z, D.X);   // (its door to the road)
			F.Height = Ground(F.At.X, F.At.Z);
			if (OffAll(F.At, 0.9)) Put(F);
		}
		I = J;
	}
	// The pubs (the map's pubs and inns that are surely one) with their signs out by the road, and the
	// churches: the building nearest each, given its tower.
	BuildingRole.assign(G.Buildings.size(), 0);
	const auto NearestBuilding = [this, &G](double X, double Z, double Within)
	{
		int Best = -1;
		double BestD = Within;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(X + Dx * SceneCell, Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
				{
					const double D = std::hypot(G.Buildings[size_t(B)].X - X, G.Buildings[size_t(B)].Z - Z);
					if (D < BestD) { BestD = D; Best = B; }
				}
			}
		return Best;
	};
	for (const TorqueFit::Json& Poi : G.Pois)
	{
		const std::string What = Poi.Str("kind");
		if (What != "pub" && !(What == "inn" && Poi.Num("conf", 1.0) >= 0.75)) continue;
		const std::string Called = Uk ? Poi.Str("name") : std::string();   // (a pub sign hung out by the road is Britain's)
		const P2 At(Poi.Num("x"), Poi.Num("z"));
		const int Home = NearestBuilding(At.X, At.Z, 30.0);
		if (Home >= 0) BuildingRole[size_t(Home)] = 2;
		if (Called.empty() || Home < 0) continue;
		// (a guest house, a B&B or a lodge the map calls an inn has no sign: only a pub's, beenster build 148)
		if (What == "inn")
		{
			bool Lodging = false;
			for (const char* Word : {"Guest House", "Guesthouse", "B&B", "Bed and Breakfast", "Bed & Breakfast", "Lodge", "Cottage", "Farm", "Apartments"})
				Lodging = Lodging || Called.find(Word) != std::string::npos;
			if (Lodging) continue;
		}
		// The pub's name on its own front wall, the one facing the road (beenster, build 148: "only pub
		// signs and they should be on the building facade itself"), not on a post by the road.
		const Building& B = G.Buildings[size_t(Home)];
		NearOpts Opts;
		NearHit Hit;
		if (!NearestOf(B.X, B.Z, Opts, Hit) || Hit.Dist > Hit.Half + 40.0 || Hit.Deck || Hit.Bore) continue;
		// (the building's own axes, as WorldRideBuildings lays it: x across, z deep)
		const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
		const P2 Ax(Cs, -Sn), Az(Sn, Cs);
		const P2 ToRoad = Hit.Foot - P2(B.X, B.Z);
		const double Ux = (ToRoad.X * Ax.X + ToRoad.Z * Ax.Z) / std::fmax(B.W * 0.5, 0.5), Uz = (ToRoad.X * Az.X + ToRoad.Z * Az.Z) / std::fmax(B.D * 0.5, 0.5);
		const bool OnX = std::fabs(Ux) >= std::fabs(Uz);
		const P2 Out = OnX ? Ax * (Ux >= 0.0 ? 1.0 : -1.0) : Az * (Uz >= 0.0 ? 1.0 : -1.0);
		const double Wall = OnX ? B.W : B.D, Deep = OnX ? B.D : B.W;
		if (Wall < 4.0 || Deep < 3.0) continue;
		Furnish F;
		F.Kind = PubSign;
		F.At = P2(B.X, B.Z) + Out * (Deep * 0.5);
		F.Face = Out;
		F.Size = std::fmin(Wall - 1.0, 6.5);   // (the board's most width: the wall less its corners)
		F.Big = B.Height;
		F.Text = Called;
		F.Height = Ground(B.X, B.Z);   // (the building's own ground: its walls stand on it)
		// (none where the map's wall stands on the road's own footprint: it would hang over the riders)
		if (!std::isnan(F.Height) && std::isnan(RoadHeight(F.At.X, F.At.Z))) Put(F);
	}
	for (const TorqueFit::Json& Mark : G.Landmarks)
	{
		if (Mark.Str("kind") != "worship") continue;
		const int Home = NearestBuilding(Mark.Num("x"), Mark.Num("z"), 40.0);
		if (Home >= 0 && G.Buildings[size_t(Home)].W * G.Buildings[size_t(Home)].D >= 60.0) BuildingRole[size_t(Home)] = 1;
	}
	// Level crossings: on each road's approach to a railway in use, a barrier (up: no trains run) and
	// its lights, on the traffic's left.
	for (const RailLine& Run : Rails)
	{
		if (Run.Disused) continue;
		for (size_t I = 0; I < Run.At.size(); ++I)
		{
			if (Run.On[I] != 3 || (I > 0 && Run.On[I - 1] == 3)) continue;
			size_t J = I;
			while (J + 1 < Run.At.size() && Run.On[J + 1] == 3) ++J;
			// (the rail's points just off the road each side, and the way across the line there)
			for (int End = 0; End < 2; ++End)
			{
				const size_t K = End == 0 ? (I > 0 ? I - 1 : I) : std::min(Run.At.size() - 1, J + 1);
				const size_t A = K > 0 ? K - 1 : K, B = std::min(Run.At.size() - 1, K + 1);
				const P2 D = Run.At[B] - Run.At[A];
				const double Len = D.Length();
				if (Len < 1e-6) continue;
				const P2 Left(D.Z / Len, -D.X / Len);
				const double Sg = End == 0 ? 1.0 : -1.0;
				Furnish F;
				F.Kind = CrossingBarrier;
				F.At = Run.At[K] + Left * (Sg * 3.2);
				F.Face = Left * Sg;   // (towards the traffic coming to the line on this side)
				F.Height = Ground(F.At.X, F.At.Z);
				if (OffAll(F.At, 0.4)) Put(F);
			}
			I = J;
		}
	}
	// What the map has: cattle grids on the road, and beside it poles, boxes, stops, benches, milestones and gates.
	for (size_t Pi = 0; Pi < G.Scenery.Points.size(); ++Pi)
	{
		const FeaturePoint& P = G.Scenery.Points[Pi];
		int Kind = -1;
		if (P.Kind == "cattle_grid") Kind = CattleGrid;
		else if (P.Kind == "power_pole") Kind = PowerPole;
		else if (P.Kind == "post_box" && Uk) Kind = PostBox;   // (a red pillar box is Britain's)
		else if (P.Kind == "bus_stop") Kind = BusStop;
		else if (P.Kind == "bench") Kind = Seat;
		else if (P.Kind == "milestone") Kind = Milestone;
		else if (P.Kind == "gate") Kind = FieldGate;
		// [OSM stage 2]
		else if (P.Kind == "stile") Kind = Stile;
		else if (P.Kind == "kissing_gate") Kind = KissingGate;
		else if (P.Kind == "lift_gate") Kind = LiftGate;
		else if (P.Kind == "bollard") Kind = Bollard;
		else if (P.Kind == "picnic_table") Kind = PicnicTable;
		else if (P.Kind == "information") Kind = InfoBoard;
		else if (P.Kind == "street_lamp") Kind = StreetLamp;
		if (Kind < 0) continue;
		NearOpts Opts;
		NearHit Hit;
		const bool Any = NearestOf(P.At.X, P.At.Z, Opts, Hit);
		// (only near a road anyone sees: [ride anywhere 4] one by a road laid later is put there then)
		if (!Any && Kind != PowerPole) { MappedLeft[Pi] = 1; continue; }
		Furnish F;
		F.Kind = Kind;
		F.At = P.At;
		F.Face = P2(std::cos(P.Heading), std::sin(P.Heading));
		if (std::isnan(F.Face.X) || std::isnan(F.Face.Z)) F.Face = P2(1.0, 0.0);
		F.Id = PropHash(P.Id);
		if (Kind == CattleGrid)
		{
			// (across the road it lies on, the road's own way)
			if (!Any || Hit.Dist > Hit.Half) continue;
			const std::vector<RoadPoint>& On = RoadOf(Hit.Which);
			const size_t K = size_t(std::max(0, Hit.Index));
			if (K + 1 >= On.size()) continue;
			const P2 D = On[K + 1].At - On[K].At;
			const double L = D.Length();
			if (L < 1e-6) continue;
			F.At = Hit.Foot;
			F.Face = D * (1.0 / L);
			F.Size = Hit.Half * 2.0 + 0.6;
			F.Height = Hit.Height;
			Put(F);
			continue;
		}
		if (Any && Hit.Dist < Hit.Half + 0.5) continue;   // (a gate across a lane, a box on the tarmac: not here)
		const bool InLine = Kind == FieldGate || Kind == Stile || Kind == KissingGate;
		if (InLine)
		{
			// In the wall, hedge or fence it stands in: [OSM stage 2] the nearest within 6 m, moved onto
			// it, along it, and a gap left in the line its width. With none near, it faces the road.
			double BestD = 6.0;
			P2 Along, OnLine;
			for (int Dz = -1; Dz <= 1; ++Dz)
				for (int Dx = -1; Dx <= 1; ++Dx)
				{
					const auto It = Scene->Lines.find(SceneKey(P.At.X + Dx * 6.0, P.At.Z + Dz * 6.0));
					if (It == Scene->Lines.end()) continue;
					for (int L : It->second)
					{
						const FeatureLine& Line = G.Scenery.Lines[size_t(L)];
						if (Line.Kind == "guard_rail" || Line.Kind == "concrete_barrier") continue;   // ([OSM stage 5C] no gate in a crash barrier)
						for (size_t K = 0; K + 1 < Line.Line.size(); ++K)
						{
							const P2 A = Line.Line[K], B = Line.Line[K + 1], AB = B - A;
							const double Len2 = AB.X * AB.X + AB.Z * AB.Z;
							if (Len2 < 1e-6) continue;
							const double T = std::fmax(0.0, std::fmin(1.0, ((P.At.X - A.X) * AB.X + (P.At.Z - A.Z) * AB.Z) / Len2));
							const P2 Foot = A + AB * T;
							const double D = Foot.DistanceTo(P.At);
							if (D < BestD) { BestD = D; Along = AB * (1.0 / std::sqrt(Len2)); OnLine = Foot; }
						}
					}
				}
			F.Size = Kind == FieldGate ? 3.6 : Kind == KissingGate ? 1.5 : 1.0;
			if (BestD < 6.0)
			{
				// (moved onto the line, unless that puts it on a road)
				NearHit OnRoad;
				if (!(NearestOf(OnLine.X, OnLine.Z, Opts, OnRoad) && OnRoad.Dist < OnRoad.Half + F.Size * 0.5 + 0.3)) F.At = OnLine;
				F.Face = Along;
				Scene->Gaps[SceneKey(F.At.X, F.At.Z)].push_back(F.At);
				Scene->GapWidths[SceneKey(F.At.X, F.At.Z)].push_back(F.Size);
			}
			else if (Kind == FieldGate) continue;   // (a field gate in no line is one across a track: not drawn)
			else if (Any) F.Face = Hit.Foot.DistanceTo(F.At) > 1e-3 ? P2(-(Hit.Foot.Z - F.At.Z), Hit.Foot.X - F.At.X) * (1.0 / Hit.Foot.DistanceTo(F.At)) : F.Face;
		}
		else if (Any && Kind != PowerPole && Kind != StreetLamp && Kind != Bollard)
		{
			// [OSM stage 2] It faces the road, 0.6 m or more back from its edge.
			const double D = Hit.Foot.DistanceTo(F.At);
			if (D > 1e-3)
			{
				const P2 Out = (F.At - Hit.Foot) * (1.0 / D);
				F.Face = Out * -1.0;
				const double Back = Hit.Half + (Kind == PicnicTable ? 1.6 : Kind == InfoBoard ? 1.0 : 0.6);
				if (D < Back) F.At = Hit.Foot + Out * Back;
			}
		}
		else if (Any && Kind == StreetLamp)
		{
			// (a mapped lamp's arm reaches out over the road)
			const double D = Hit.Foot.DistanceTo(F.At);
			if (D > 1e-3) F.Face = (Hit.Foot - F.At) * (1.0 / D);
		}
		F.Height = Ground(F.At.X, F.At.Z);
		Put(F);
	}
	// [OSM stage 6B] The warnings, in Britain (its own triangles): where the road's shape or what is on it
	// asks for one, the distance before it and the size the limit there sets, on the traffic's side, 0.7 m
	// or more back from the edge. None within 10 m of another sign facing the same way (it moves back up
	// to 30 m, else it is left out): the more important placed first.
	if (Uk)
	{
		const auto MphAt = [this](double Along)
		{
			const LimitStretch* L = LimitAt(std::clamp(Along, 0.0, Length));
			return L != nullptr ? ShownLimit(L->Kmh) : 0;
		};
		// (and no two warnings within 40 m of each other the same way: the lesser left out, not crowded in)
		struct Standing { P2 At, Face; bool Warning; };
		std::vector<Standing> Signs;
		for (const Furnish& F : Furniture)
			if (F.Kind == NameSign || F.Kind == Limit30 || F.Kind == LimitNational) Signs.push_back({F.At, F.Face, false});
		const auto Clear = [&Signs](const P2& At, const P2& Face, double Apart)
		{
			for (const Standing& S : Signs)
				if (S.At.DistanceTo(At) < (S.Warning ? Apart : 10.0) && S.Face.X * Face.X + S.Face.Z * Face.Z > 0.5) return false;
			return true;
		};
		// [OSM stage 6C] The direction signs first: they matter most (none within 50 m of another the same
		// way, and a warning keeps 40 m from them).
		const std::vector<Furnish> Directions = DirectionSigns();
		for (const Furnish& D : Directions)
			if ((D.Kind == DirAdvance || D.Kind == DirConfirm) && Clear(D.At, D.Face, 50.0))
			{
				Signs.push_back({D.At, D.Face, true});
				Put(D);
			}
		// ([6D] then the brown signs and fingerposts, 10 m from any other)
		for (const Furnish& D : Directions)
			if (D.Kind != DirAdvance && D.Kind != DirConfirm && Clear(D.At, D.Face, 10.0))
			{
				Signs.push_back({D.At, D.Face, false});
				Put(D);
			}
		const auto Warn = [&](const Hazard& H)
		{
			const int Mph = MphAt(H.Along - H.Way * 40.0);
			if (Mph <= 0) return;
			const double Before = WarnBefore(Mph);
			// (a warning already near where this one's would stand, the same way: this one is left out)
			{
				const size_t K = PointAt(std::clamp(H.Along - H.Way * Before, 0.0, Length));
				if (!Clear(Verge(K, H.Way * Keep, 1.0), Dir(K) * -double(H.Way), 40.0)) return;
			}
			for (double Extra = 0.0; Extra <= 30.0; Extra += 5.0)
			{
				const double Along = H.Along - H.Way * (Before + Extra);
				if (Along < 0.0 || Along > Length) return;
				const size_t K = PointAt(Along);
				Furnish F;
				F.Kind = WarnSign;
				F.Mapped = false;
				F.Size = H.Symbol;
				F.Text = H.Text;
				F.Big = WarnSide(Mph, Road[K].Half);
				F.At = Verge(K, H.Way * Keep, 0.7 + F.Big * 0.5);
				F.Face = Dir(K) * -double(H.Way);
				F.Height = Ground(F.At.X, F.At.Z);
				const P2 Across(F.Face.Z, -F.Face.X);
				if (!Clear(F.At, F.Face, 10.0) || !OffAll(F.At, 0.6) || !OffAll(F.At + Across * (F.Big * 0.5), 0.5) || !OffAll(F.At - Across * (F.Big * 0.5), 0.5)) continue;
				Signs.push_back({F.At, F.Face, true});
				Put(F);
				return;
			}
		};
		std::vector<Hazard> Ahead;
		// Roundabouts on a road of 40 mph or more: where the ride's road comes onto a ring, each way.
		for (const RoadRing& Ring : RoadRings)
		{
			const double Reach = Ring.Radius + 2.0;
			for (size_t I = 0; I < Road.size(); ++I)
			{
				if (Road[I].At.DistanceTo(Ring.Mid) >= Reach) continue;
				size_t J = I;
				while (J + 1 < Road.size() && Road[J + 1].At.DistanceTo(Ring.Mid) < Reach) ++J;
				for (int Way : {1, -1})
				{
					Hazard H;
					H.Way = Way;
					H.Along = Way > 0 ? Road[I].Along : Road[J].Along;
					H.Symbol = SignPlate::SymRoundabout;
					if (MphAt(H.Along - Way * 60.0) >= 40 && (Way > 0 ? I > 0 : J + 1 < Road.size())) Ahead.push_back(H);
				}
				I = J;
			}
		}
		// Traffic lights out of town (40 mph or more), for the traffic they govern.
		for (const MappedMark& M : MainMapped)
		{
			if (M.Kind != "traffic_signals") continue;
			for (int Way : {1, -1})
			{
				if (M.Dir != 0 && M.Dir != Way) continue;
				Hazard H;
				H.Way = Way;
				H.Along = M.Along;
				H.Symbol = SignPlate::SymSignals;
				if (MphAt(M.Along - Way * 60.0) >= 40) Ahead.push_back(H);
			}
		}
		// Side roads and crossroads on a fast road (50 mph or more, not built up) where the ride's road is
		// the greater and the other a classified road (tertiary or more): which side as each way reads it.
		{
			struct Meet { double Along = 0.0; bool Left = false, Right = false; };
			std::vector<Meet> Meets;
			for (int Which : Planned)
			{
				const SideRoad* Sd = SideOf(Which);
				if (Sd == nullptr || Sd->Track || Sd->Edge < 0) continue;
				const std::string& Class = G.ClassOf(Sd->Edge);
				if (Class != "tertiary" && Class != "secondary" && Class != "primary" && Class != "trunk" && Class != "tertiary_link" && Class != "secondary_link" && Class != "primary_link") continue;
				for (const Mouth& M : Sd->Mouths)
				{
					if (!M.Is || !M.GiveWay || M.Ring || M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size()) continue;
					const size_t I = size_t(M.MainIndex);
					if (BuiltUp[I]) continue;
					const P2 D = Dir(I);
					const bool Left = M.Across.X * D.Z - M.Across.Z * D.X > 0.0;
					Meet Mt;
					Mt.Along = Road[I].Along;
					(Left ? Mt.Left : Mt.Right) = true;
					Meets.push_back(Mt);
				}
			}
			std::sort(Meets.begin(), Meets.end(), [](const Meet& A, const Meet& B) { return A.Along < B.Along; });
			for (size_t K = 0; K < Meets.size(); ++K)
			{
				Meet Mt = Meets[K];
				while (K + 1 < Meets.size() && Meets[K + 1].Along - Mt.Along < 20.0)
				{
					Mt.Left = Mt.Left || Meets[K + 1].Left;
					Mt.Right = Mt.Right || Meets[K + 1].Right;
					++K;
				}
				for (int Way : {1, -1})
				{
					if (MphAt(Mt.Along - Way * 60.0) < 50) continue;
					Hazard H;
					H.Way = Way;
					H.Along = Mt.Along;
					const bool L = Way > 0 ? Mt.Left : Mt.Right, R = Way > 0 ? Mt.Right : Mt.Left;
					H.Symbol = L && R ? SignPlate::SymCrossroads : L ? SignPlate::SymSideLeft : SignPlate::SymSideRight;
					Ahead.push_back(H);
				}
			}
		}
		// The bends and the hills.
		ShapeHazards(Road, BuiltUp, MphAt, Ahead);
		// Road humps: before the first of each run of them (humps within 150 m of each other), each way.
		{
			std::vector<double> Humps;
			for (const MappedMark& M : MainMapped)
				if (M.Kind == "hump" || M.Kind == "table" || M.Kind == "cushion" || M.Kind == "bump") Humps.push_back(M.Along);
			std::sort(Humps.begin(), Humps.end());
			for (size_t K = 0; K < Humps.size(); ++K)
			{
				size_t J = K;
				while (J + 1 < Humps.size() && Humps[J + 1] - Humps[J] < 150.0) ++J;
				for (int Way : {1, -1})
				{
					Hazard H;
					H.Way = Way;
					H.Along = Way > 0 ? Humps[K] : Humps[J];
					H.Symbol = SignPlate::SymHumps;
					Ahead.push_back(H);
				}
				K = J;
			}
		}
		// The map's cattle grids across the ride's road: the danger sign and its "Cattle grid" plate.
		for (size_t Fi = 0; Fi < Furniture.size(); ++Fi)
		{
			const Furnish& F = Furniture[Fi];
			if (F.Kind != CattleGrid) continue;
			NearOpts Opts;
			NearHit Hit;
			if (!NearestOf(F.At.X, F.At.Z, Opts, Hit) || Hit.Which != 0 || Hit.Index < 0 || size_t(Hit.Index) >= Road.size()) continue;
			for (int Way : {1, -1})
			{
				Hazard H;
				H.Way = Way;
				H.Along = Road[size_t(Hit.Index)].Along;
				H.Symbol = SignPlate::SymDanger;
				H.Text = "Cattle grid";
				Ahead.push_back(H);
			}
		}
		for (const Hazard& H : Ahead) Warn(H);
		// The streets' names: a plate at the corner of each named street that meets the ride's road where
		// it is built up, on the street's left as it is turned into, facing the ride's road, at the back
		// of the pavement.
		const auto InBuilding = [this, &G](const P2& P, double Margin)
		{
			for (int Dz = -1; Dz <= 1; ++Dz)
				for (int Dx = -1; Dx <= 1; ++Dx)
				{
					const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
					if (It == Scene->Buildings.end()) continue;
					for (int B : It->second)
					{
						const Building& H = G.Buildings[size_t(B)];
						const double C = std::cos(H.Yaw), S = std::sin(H.Yaw), Ex = P.X - H.X, Ez = P.Z - H.Z;
						if (std::fabs(Ex * C - Ez * S) < H.W * 0.5 + Margin && std::fabs(Ex * S + Ez * C) < H.D * 0.5 + Margin) return true;
					}
				}
			return false;
		};
		std::vector<std::pair<P2, std::string>> Named;   // (one plate for a street within 60 m)
		for (int Which : Planned)
		{
			const SideRoad* Sd = SideOf(Which);
			if (Sd == nullptr || Sd->Track || Sd->Edge < 0 || Sd->Pts.size() < 2) continue;
			const RoadEdge& E = G.Edges[size_t(Sd->Edge)];
			const std::string Called = E.Name > 0 && size_t(E.Name) < G.Names.size() ? G.Names[size_t(E.Name)] : std::string();
			if (Called.empty() || !StreetClass(G.ClassOf(Sd->Edge))) continue;
			for (int End = 0; End < 2; ++End)
			{
				const Mouth& M = Sd->Mouths[End];
				if (!M.Is || M.Ring || M.MainIndex < 0 || size_t(M.MainIndex) >= Road.size() || !BuiltUp[size_t(M.MainIndex)]) continue;
				double Wide = 0.0;
				if (!SignText::Width(Called, 0.1, Wide)) continue;
				const double Half = End == 0 ? Sd->Pts.front().Half : Sd->Pts.back().Half, W = Wide + 0.2;
				const P2 Left(M.Away.Z, -M.Away.X);
				Furnish F;
				F.Kind = StreetPlate;
				F.Mapped = false;
				F.Text = Called;
				F.Big = 0.1;
				F.Face = M.Away * -1.0;
				const P2 Across(F.Face.Z, -F.Face.X);
				// (past the corner's rounding, the street's left first, then its right; never against a house)
				bool Found = false;
				for (double Sigma : {1.0, -1.0})
				{
					P2 Corner, AlongMain;
					double Reach = 0.0;
					MouthCorner(M, Road[size_t(M.MainIndex)].At, Half, Sigma, Corner, AlongMain, Reach);
					F.At = M.At + M.Away * (Reach + 0.5) + Left * (Sigma * (Half + 2.4 + W * 0.5));
					if (std::any_of(Named.begin(), Named.end(), [&F](const std::pair<P2, std::string>& N) { return N.second == F.Text && N.first.DistanceTo(F.At) < 60.0; })) break;
					if (!OffAll(F.At, 1.0) || !OffAll(F.At + Across * (W * 0.5), 0.8) || !OffAll(F.At - Across * (W * 0.5), 0.8)) continue;
					if (InBuilding(F.At, W * 0.5 + 0.8)) continue;
					Found = true;
					break;
				}
				if (!Found) continue;
				F.Height = Ground(F.At.X, F.At.Z);
				{
					Named.emplace_back(F.At, F.Text);
					Put(F);
				}
			}
		}
	}
}

int RidePlan::CutGaps(const P2& A, const P2& B, P2 Out[2][2]) const
{
	Out[0][0] = A;
	Out[0][1] = B;
	const double Len = A.DistanceTo(B);
	if (!Scene || Len < 1e-6) return 1;
	// (the gateways near it: in the squares of its ends)
	double Lo = 2.0, Hi = -1.0;
	const P2 Dir = (B - A) * (1.0 / Len);
	for (const long long Key : {SceneKey(A.X, A.Z), SceneKey(B.X, B.Z)})
	{
		const auto It = Scene->Gaps.find(Key);
		if (It == Scene->Gaps.end()) continue;
		const auto Wd = Scene->GapWidths.find(Key);
		for (size_t K = 0; K < It->second.size(); ++K)
		{
			const P2 G = It->second[K];
			const double Half = (Wd != Scene->GapWidths.end() && K < Wd->second.size() ? Wd->second[K] : 3.6) * 0.5 + 0.05;
			const double T = (G - A).X * Dir.X + (G - A).Z * Dir.Z;
			const double Off = std::fabs((G - A).X * Dir.Z - (G - A).Z * Dir.X);
			if (Off > 0.8 || T < -Half || T > Len + Half) continue;
			Lo = std::fmin(Lo, (T - Half) / Len);
			Hi = std::fmax(Hi, (T + Half) / Len);
		}
		if (Key == SceneKey(B.X, B.Z)) break;   // (one square for both ends)
	}
	if (Hi < Lo) return 1;
	int N = 0;
	if (Lo > 0.05) { Out[N][0] = A; Out[N][1] = A + (B - A) * Lo; ++N; }
	if (Hi < 0.95) { Out[N][0] = A + (B - A) * Hi; Out[N][1] = B; ++N; }
	return N;
}

bool RidePlan::InGap(double X, double Z) const
{
	if (!Scene) return false;
	const auto It = Scene->Gaps.find(SceneKey(X, Z));
	if (It == Scene->Gaps.end()) return false;
	for (const P2& G : It->second)
		if (std::hypot(G.X - X, G.Z - Z) < 1.9) return true;
	return false;
}

void RidePlan::FurnitureTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out) const
{
	if (!Scene) return;
	static const Rgb White = {0.75f, 0.75f, 0.74f}, Black = {0.012f, 0.012f, 0.012f}, Red = {0.45f, 0.012f, 0.01f}, Grey = {0.16f, 0.16f, 0.165f}, Steel = {0.24f, 0.25f, 0.26f},
		Wood = {0.075f, 0.05f, 0.032f}, PoleWood = {0.05f, 0.04f, 0.03f}, Stone3 = {0.22f, 0.21f, 0.19f}, Lamp = {0.6f, 0.58f, 0.5f};
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	const auto Label = [&Out](const P3& At, const P2& Face, double Tall, const std::string& Words, const Rgb& Colour)
	{
		// ([OSM stage 6A] set as triangles in the signs' layer, capitals as tall as a text component's
		// would draw them; a text component only for a letter the baked alphabet lacks)
		const float Ink[3] = {Colour.R, Colour.G, Colour.B};
		if (SignText::Set(Out.SignsOf(), At.X, At.Y, At.Z, Face.X, Face.Z, Tall * SignText::CapOfEm, Words, Ink)) return;
		RideMesh::Label L;
		L.X = float(At.X - Out.OriginX);
		L.Y = float(At.Y);
		L.Z = float(At.Z - Out.OriginZ);
		L.Nx = float(Face.X);
		L.Nz = float(Face.Z);
		L.Size = float(Tall);
		L.R = Colour.R; L.G = Colour.G; L.B = Colour.B;
		L.Text = Words;
		Out.Labels.push_back(L);
	};
	// A flat board facing Face (its back grey), its middle Mid, W by H.
	const auto BoardInto = [](RideMesh& Into, const P3& Mid, const P2& Face, double W, double H, const Rgb& Colour, double Proud)
	{
		const P2 U(Face.Z, -Face.X);
		const P3 C = At3(Mid.X + Face.X * Proud, Mid.Y, Mid.Z + Face.Z * Proud);
		const auto Pt = [&](double Su, double Sv) { return At3(C.X + U.X * Su * W * 0.5, C.Y + Sv * H * 0.5, C.Z + U.Z * Su * W * 0.5); };
		Quad(Into, Pt(-1, -1), Pt(1, -1), Pt(1, 1), Pt(-1, 1), At3(C.X + Face.X * 50.0, C.Y, C.Z + Face.Z * 50.0), Colour);
	};
	const auto Board = [&Out, &BoardInto](const P3& Mid, const P2& Face, double W, double H, const Rgb& Colour, double Proud) { BoardInto(Out, Mid, Face, W, H, Colour, Proud); };
	// ([OSM stage 6A] a road sign's face: in the signs' layer, which shines back the headlight)
	const auto SignBoard = [&Out, &BoardInto](const P3& Mid, const P2& Face, double W, double H, const Rgb& Colour, double Proud) { BoardInto(Out.SignsOf(), Mid, Face, W, H, Colour, Proud); };
	// (beenster, build 141: the road furniture "a bit basic". Posts, lamp columns and poles are round
	// and tapered now, ten sides near, five farther off)
	const auto Post = [&Out, Fine](double X, double Z, double Lo, double Hi, double R0, double R1, const Rgb& Colour)
	{
		RideShapes::Paint Pp;
		Pp.R = Colour.R; Pp.G = Colour.G; Pp.B = Colour.B;
		RideShapes::Column(Out, RideShapes::Pt(X, Lo, Z), R0, R1, Hi - Lo, Fine ? 10 : 5, Pp, R1);
	};
	const auto Rod = [&Out](const P3& A, const P3& B, double W, const Rgb& Colour)
	{
		RideShapes::Paint Pp;
		Pp.R = Colour.R; Pp.G = Colour.G; Pp.B = Colour.B;
		RideShapes::Strut(Out, RideShapes::Pt(A.X, A.Y, A.Z), RideShapes::Pt(B.X, B.Y, B.Z), W, Pp);
	};
	std::vector<const Furnish*> Here;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Furniture.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Furniture.end()) continue;
			for (int Index : It->second) Here.push_back(&Furniture[size_t(Index)]);
		}
	// [ride anywhere 4] and the network's roads' own
	std::vector<std::shared_ptr<const std::vector<Furnish>>> Held;
	NetworkFurniture(X0, Z0, Size, Held, Here);
	for (const Furnish* Each : Here)
	{
		const Furnish& F = *Each;
		if (F.At.X < X0 || F.At.X >= X0 + Size || F.At.Z < Z0 || F.At.Z >= Z0 + Size) continue;
		if (!Fine && F.Kind != PowerPole && F.Kind != StreetLamp) continue;
		// [OSM stage 2] a kind the prop kit has a model for is drawn from it (PropsTile), but a
		// street lamp's glowing head and real light stay here
		if (Modelled(F.Kind) && F.Kind != StreetLamp) continue;
		const double Y = F.Height;
		const P2 Face = F.Face;
		const P2 Side(Face.Z, -Face.X);
		switch (F.Kind)
		{
		case NameSign:
		{
			double H = 0.5;
			const double W = NameBoardWide(F.Text, &H);
			for (double Su : {-1.0, 1.0}) Post(F.At.X + Side.X * Su * (W * 0.5 - 0.15), F.At.Z + Side.Z * Su * (W * 0.5 - 0.15), Y - 0.3, Y + 1.7 + H * 0.5, 0.045, 0.04, Grey);
			const P3 Mid = At3(F.At.X, Y + 1.3 + H * 0.5, F.At.Z);
			Board(Mid, Face, W + 0.08, H + 0.08, Grey, -0.01);
			Board(Mid, Face * -1.0, W + 0.08, H + 0.08, Grey, 0.03);
			SignBoard(Mid, Face, W + 0.04, H + 0.04, Black, 0.05);
			SignBoard(Mid, Face, W, H, White, 0.06);
			const size_t Bar = F.Text.find('|');
			if (Bar == std::string::npos) Label(At3(Mid.X + Face.X * 0.07, Mid.Y, Mid.Z + Face.Z * 0.07), Face, 0.22, F.Text, Black);
			else
			{
				// ([OSM stage 6E] the Welsh or Gaelic name above the English, Gaelic in a second colour)
				static const Rgb GaelicGreen = {0.0f, 0.16f, 0.07f};
				const bool Gaelic = PlaceAt(F.At.X, F.At.Z).find("Scotland") != std::string::npos;
				Label(At3(Mid.X + Face.X * 0.07, Mid.Y + 0.17, Mid.Z + Face.Z * 0.07), Face, 0.22, F.Text.substr(0, Bar), Gaelic ? GaelicGreen : Black);
				Label(At3(Mid.X + Face.X * 0.07, Mid.Y - 0.17, Mid.Z + Face.Z * 0.07), Face, 0.22, F.Text.substr(Bar + 1), Black);
			}
			break;
		}
		case Limit30:
		case LimitNational:
		{
			Post(F.At.X, F.At.Z, Y - 0.3, Y + 2.15, 0.045, 0.04, Grey);
			const P3 Mid = At3(F.At.X, Y + 1.75, F.At.Z);
			Disc(Out, At3(Mid.X - Face.X * 0.05, Mid.Y, Mid.Z - Face.Z * 0.05), Face * -1.0, 0.38, Grey);
			if (F.Kind == Limit30)
			{
				Disc(Out.SignsOf(), At3(Mid.X + Face.X * 0.05, Mid.Y, Mid.Z + Face.Z * 0.05), Face, 0.38, Red);
				Disc(Out.SignsOf(), At3(Mid.X + Face.X * 0.06, Mid.Y, Mid.Z + Face.Z * 0.06), Face, 0.30, White);
				Label(At3(Mid.X + Face.X * 0.07, Mid.Y, Mid.Z + Face.Z * 0.07), Face, 0.3, F.Text, Black);
			}
			else
			{
				// (white, crossed by a black bar from top right to bottom left)
				Disc(Out.SignsOf(), At3(Mid.X + Face.X * 0.05, Mid.Y, Mid.Z + Face.Z * 0.05), Face, 0.38, White);
				const P2 U(Face.Z, -Face.X);
				const double Px = Mid.X + Face.X * 0.065, Pz = Mid.Z + Face.Z * 0.065, R = 0.36, T = 0.05;
				const auto Pt = [&](double Su, double Sv) { return At3(Px + U.X * Su, Mid.Y + Sv, Pz + U.Z * Su); };
				const double C = R * 0.7071;
				Quad(Out.SignsOf(), Pt(-C - T, -C + T), Pt(-C + T, -C - T), Pt(C + T, C - T), Pt(C - T, C + T), At3(Px + Face.X * 50.0, Mid.Y, Pz + Face.Z * 50.0), Black, true);
			}
			break;
		}
		case Chevron:
		{
			for (double Su : {-0.3, 0.3}) Post(F.At.X + Side.X * Su, F.At.Z + Side.Z * Su, Y - 0.3, Y + 1.7, 0.04, 0.035, Grey);
			const P3 Mid = At3(F.At.X, Y + 1.3, F.At.Z);
			// (each face its arrows the way its own reader's bend turns: forwards F.Size, back the other)
			for (int Back = 0; Back < 2; ++Back)
			{
				const P2 N = Back ? Face * -1.0 : Face;
				const double Point = Back ? -F.Size : F.Size;   // +1: the tip to the reader's left
				SignBoard(Mid, N, 0.9, 0.7, Black, 0.03);
				const P2 U(N.Z, -N.X);   // (the reader's left, as they face it)
				const double Px = Mid.X + N.X * 0.04, Pz = Mid.Z + N.Z * 0.04;
				const auto Pt = [&](double Su, double Sv) { return At3(Px - U.X * Su * Point, Mid.Y + Sv, Pz - U.Z * Su * Point); };
				const P3 Toward = At3(Px + N.X * 50.0, Mid.Y, Pz + N.Z * 50.0);
				for (double Sv : {1.0, -1.0})
					Quad(Out.SignsOf(), Pt(-0.2, 0.0), Pt(-0.06, 0.0), Pt(0.2, 0.26 * Sv), Pt(0.06, 0.26 * Sv), Toward, White, true);
			}
			break;
		}
		case WarnSign:
		{
			// [OSM stage 6B] Its triangle's foot 1.5 m up (over its plate's, if it has words under it), on a
			// post behind it, its back grey.
			const double S = F.Big > 0.0 ? F.Big : 0.75;
			const bool Hill = F.Size == SignPlate::SymSteepDown || F.Size == SignPlate::SymSteepUp;
			const std::string Words = Hill ? std::string() : F.Text;
			double Wp = 0.0, Hp = 0.0, Foot = Y + 1.5;
			static const float Ink[3] = {Black.R, Black.G, Black.B}, Paper[3] = {White.R, White.G, White.B};
			if (!Words.empty() && SignText::Width(Words, S * 0.09, Wp))
			{
				SignPlate::Frame P;
				P.X = F.At.X; P.Z = F.At.Z; P.Y = Foot + S * 0.09 * 1.2; P.Fx = Face.X; P.Fz = Face.Z;
				if (SignPlate::WordPlate(Out.SignsOf(), Out, P, S * 0.09, Words, Paper, Ink, Wp, Hp)) Foot += Hp + 0.05;
			}
			SignPlate::Frame T;
			T.X = F.At.X; T.Z = F.At.Z; T.Y = Foot + S * 0.2887; T.Fx = Face.X; T.Fz = Face.Z;
			Post(F.At.X - Face.X * 0.06, F.At.Z - Face.Z * 0.06, Y - 0.3, T.Y + S * 0.35, 0.04, 0.036, Grey);
			SignPlate::Warning(Out.SignsOf(), Out, T, S, int(F.Size), Hill ? F.Text : std::string());
			break;
		}
		case DirAdvance:
		case DirConfirm:
		case BoundarySign:
		{
			// [OSM stage 6C] A direction sign: its board's foot 1.2 m up, on two posts behind it (one under a
			// narrow board).
			const double Cap = F.Big > 0.0 ? F.Big : 0.1;
			double W = 0.0, H = 0.0;
			const bool Advance = F.Kind == DirAdvance;
			const bool Sized = Advance ? SignPlate::AdvanceBoard(nullptr, nullptr, SignPlate::Frame(), Cap, int(F.Size), F.Text, W, H)
									   : SignPlate::ConfirmBoard(nullptr, nullptr, SignPlate::Frame(), Cap, int(F.Size), F.Text, W, H);
			if (!Sized) break;
			SignPlate::Frame P;
			P.X = F.At.X; P.Z = F.At.Z; P.Y = Y + 1.2 + H * 0.5; P.Fx = Face.X; P.Fz = Face.Z;
			const double R = std::clamp(W * 0.012, 0.04, 0.07);
			if (W < 1.0) Post(F.At.X - Face.X * 0.07, F.At.Z - Face.Z * 0.07, Y - 0.3, P.Y + H * 0.45, R, R * 0.9, Grey);
			else
				for (double Su : {-1.0, 1.0}) Post(F.At.X + Side.X * Su * W * 0.3 - Face.X * 0.07, F.At.Z + Side.Z * Su * W * 0.3 - Face.Z * 0.07, Y - 0.3, P.Y + H * 0.45, R, R * 0.9, Grey);
			if (Advance) SignPlate::AdvanceBoard(&Out.SignsOf(), &Out, P, Cap, int(F.Size), F.Text, W, H);
			else SignPlate::ConfirmBoard(&Out.SignsOf(), &Out, P, Cap, int(F.Size), F.Text, W, H);
			break;
		}
		case Countdown:
		{
			// [OSM stage 6F] Its plate's foot 0.9 m up, on two posts behind it.
			SignPlate::Frame P;
			P.X = F.At.X; P.Z = F.At.Z; P.Y = Y + 0.9 + 0.6; P.Fx = Face.X; P.Fz = Face.Z;
			for (double Su : {-0.3, 0.3}) Post(F.At.X + Side.X * Su - Face.X * 0.05, F.At.Z + Side.Z * Su - Face.Z * 0.05, Y - 0.3, P.Y + 0.55, 0.04, 0.035, Grey);
			SignPlate::Countdown(Out.SignsOf(), Out, P, int(F.Size));
			break;
		}
		case BrownFlag:
		{
			// [OSM stage 6D] A brown sign's flag on a post behind it, its foot 1.2 m up.
			const double Cap = F.Big > 0.0 ? F.Big : 0.08;
			double W = 0.0, H = 0.0;
			if (!SignPlate::FlagBoard(nullptr, nullptr, SignPlate::Frame(), Cap, int(F.Size), F.Text, W, H)) break;
			SignPlate::Frame P;
			P.X = F.At.X; P.Z = F.At.Z; P.Y = Y + 1.2 + H * 0.5; P.Fx = Face.X; P.Fz = Face.Z;
			// (the post under the flag's square end)
			const double Su = F.Text[0] == '<' ? 1.0 : -1.0;
			Post(F.At.X + Side.X * Su * (W * 0.5 - 0.15) - Face.X * 0.05, F.At.Z + Side.Z * Su * (W * 0.5 - 0.15) - Face.Z * 0.05, Y - 0.3, P.Y + H * 0.4, 0.045, 0.04, Grey);
			SignPlate::FlagBoard(&Out.SignsOf(), &Out, P, Cap, int(F.Size), F.Text, W, H);
			break;
		}
		case Fingerpost:
		{
			// [OSM stage 6D] A white post with a ball on top and an arm for each road, each lettered both
			// sides, the highest 2.3 m up.
			static const Rgb Paint = {0.62f, 0.62f, 0.6f};
			const double Cap = F.Big > 0.0 ? F.Big : 0.055;
			Post(F.At.X, F.At.Z, Y - 0.3, Y + 2.55, 0.055, 0.045, Paint);
			{
				RideShapes::Paint Pp;
				Pp.R = Paint.R; Pp.G = Paint.G; Pp.B = Paint.B;
				RideShapes::Column(Out, RideShapes::Pt(F.At.X, Y + 2.55, F.At.Z), 0.07, 0.07, 0.1, 8, Pp, 0.0);
			}
			double Arm = Y + 2.35;
			size_t From = 0;
			while (From < F.Text.size() && Arm > Y + 1.3)
			{
				size_t End = F.Text.find('|', From);
				if (End == std::string::npos) End = F.Text.size();
				const std::string One = F.Text.substr(From, End - From);
				From = End + 1;
				const size_t Semi = One.find(';');
				if (Semi == std::string::npos) continue;
				double Hx = 0.0, Hz = 0.0;
				if (std::sscanf(One.c_str(), "%lf,%lf", &Hx, &Hz) != 2) continue;
				const double Hl = std::hypot(Hx, Hz);
				if (Hl < 1e-6) continue;
				Hx /= Hl; Hz /= Hl;
				const std::string Words = One.substr(Semi + 1);
				double W = 0.0, H = 0.0;
				if (!SignPlate::FlagBoard(nullptr, nullptr, SignPlate::Frame(), Cap, SignPlate::SchemeLocal, ">" + Words, W, H)) continue;
				// (each face's reader's right runs along the arm one way, the other's the other: (Face.z, -Face.x))
				for (int Sd : {1, -1})
				{
					SignPlate::Frame P;
					P.Fx = -Hz * Sd; P.Fz = Hx * Sd;
					P.X = F.At.X + Hx * (W * 0.5 + 0.06) + P.Fx * 0.02;
					P.Z = F.At.Z + Hz * (W * 0.5 + 0.06) + P.Fz * 0.02;
					P.Y = Arm;
					SignPlate::FlagBoard(&Out.SignsOf(), &Out, P, Cap, SignPlate::SchemeLocal, (Sd > 0 ? ">" : "<") + Words, W, H);
				}
				Arm -= H + 0.05;
			}
			break;
		}
		case StreetPlate:
		{
			// [OSM stage 6B] A street's name, black on white with a black border, on two short posts.
			static const float Ink[3] = {Black.R, Black.G, Black.B}, Paper[3] = {White.R, White.G, White.B};
			const double Cap = F.Big > 0.0 ? F.Big : 0.1;
			double Wide = 0.0;
			if (!SignText::Width(F.Text, Cap, Wide)) break;
			const double W = Wide + Cap * 2.0, H = Cap * 2.4;
			SignPlate::Frame P;
			P.X = F.At.X; P.Z = F.At.Z; P.Y = Y + 0.75 + H * 0.5; P.Fx = Face.X; P.Fz = Face.Z;
			for (double Su : {-1.0, 1.0}) Post(F.At.X + Side.X * Su * (W * 0.5 - 0.12) - Face.X * 0.05, F.At.Z + Side.Z * Su * (W * 0.5 - 0.12) - Face.Z * 0.05, Y - 0.2, P.Y + H * 0.5, 0.03, 0.03, Grey);
			double Wp = 0.0, Hp = 0.0;
			SignPlate::WordPlate(Out.SignsOf(), Out, P, Cap, F.Text, Paper, Ink, Wp, Hp);
			break;
		}
		case StreetLamp:
		{
			// A tapered round column on a wider foot, an outreach arm swept up to the lantern, and the
			// lantern's cowl over its glowing face.
			// ([fps plan step 2] with the kit's model, Tools/make_ride_props.py, placed by PropsTile, only the
			// glowing face and the light: the model's lantern is 1.1 m out at the same height)
			const bool Drawn = !Modelled(F.Kind);
			if (Drawn) Post(F.At.X, F.At.Z, Y - 0.3, Y + 6.0, 0.085, 0.05, Grey);
			if (!Fine) break;
			const double Ax = F.At.X + Face.X * 1.1, Az = F.At.Z + Face.Z * 1.1;
			if (Drawn)
			{
				Post(F.At.X, F.At.Z, Y - 0.1, Y + 0.9, 0.13, 0.11, Shade(Grey, 0.85));
				const P3 Bend = At3(F.At.X + Face.X * 0.35, Y + 6.05, F.At.Z + Face.Z * 0.35);
				Rod(At3(F.At.X, Y + 5.75, F.At.Z), Bend, 0.06, Grey);
				Rod(Bend, At3(Ax - Face.X * 0.2, Y + 5.95, Az - Face.Z * 0.2), 0.05, Grey);
				Box(Out, Ax, Az, Face, 0.27, 0.11, Y + 5.75, Y + 5.88, Grey);
				Box(Out, Ax, Az, Face, 0.2, 0.08, Y + 5.88, Y + 5.96, Shade(Grey, 0.8));
				Board(At3(Ax, Y + 5.745, Az), P2(0.0, 1.0), 0.4, 0.15, Lamp, 0.0);
			}
			// (its light: the lamp's face glowing after dark, and a real light under it near the rider)
			static const Rgb LampLight = {1.0f, 0.88f, 0.66f};
			{
				const P2 Crosswise(Face.Z, -Face.X);
				const double Ly = Y + 5.735;
				const auto Pt = [&](double Su, double Sv) { return At3(Ax + Face.X * Su * 0.18 + Crosswise.X * Sv * 0.07, Ly, Az + Face.Z * Su * 0.18 + Crosswise.Z * Sv * 0.07); };
				Quad(Out.NightOf(), Pt(-1, -1), Pt(1, -1), Pt(1, 1), Pt(-1, 1), At3(Ax, Ly - 100.0, Az), LampLight, false, 0.0f);
			}
			Out.Lights.push_back(float(Ax - Out.OriginX));
			Out.Lights.push_back(float(Y + 5.4));
			Out.Lights.push_back(float(Az - Out.OriginZ));
			break;
		}
		case CattleGrid:
		{
			// Steel bars across the road, a little proud of it, and a short rail each side.
			const P2 Across(Face.Z, -Face.X);
			const double Half = F.Size * 0.5;
			for (int K = -9; K <= 9; ++K)
			{
				const double Off = K * 0.13;
				const P2 C = F.At + Face * Off;
				const auto Top = [&](double Su)
				{
					const P2 Q = C + Across * (Su * Half);
					const double H = RoadHeight(Q.X, Q.Z);
					return std::isnan(H) ? Y : H;
				};
				// ([build 154] beenster: "cattle grids need to be on the surface of the road, not sunken": a bar
				// was straight from edge to edge, so the road's crown rose over it in the middle. Now it follows
				// the surface, in pieces of half a metre or so, a hair proud of it all the way.)
				const int Pieces = std::max(2, int(std::ceil(F.Size / 0.5)));
				const P2 T = Face * 0.025;
				for (int Pc = 0; Pc < Pieces; ++Pc)
				{
					const double Sa = -1.0 + 2.0 * Pc / Pieces, Sb = -1.0 + 2.0 * (Pc + 1) / Pieces;
					const double Ya = Top(std::clamp(Sa, -0.95, 0.95)) + 0.015, Yb = Top(std::clamp(Sb, -0.95, 0.95)) + 0.015;   // (just in from the edge, where the road is found)
					const P2 A = C + Across * (Sa * Half), B = C + Across * (Sb * Half);
					Quad(Out, At3(A.X - T.X, Ya, A.Z - T.Z), At3(B.X - T.X, Yb, B.Z - T.Z), At3(B.X + T.X, Yb, B.Z + T.Z), At3(A.X + T.X, Ya, A.Z + T.Z), At3(C.X, Y + 100.0, C.Z), Steel);
				}
			}
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Across * (Su * (Half + 0.3));
				const double G0 = Ground(Q.X, Q.Z);
				if (!std::isnan(G0)) Box(Out, Q.X, Q.Z, Face, 1.3, 0.05, G0 - 0.2, G0 + 0.9, Steel);
			}
			break;
		}
		case PowerPole:
		{
			// A round timber pole, tapering, its cross-arm and (near) three insulators and a cap.
			Post(F.At.X, F.At.Z, Y - 0.5, Y + 8.5, 0.15, 0.11, PoleWood);
			Box(Out, F.At.X, F.At.Z, Side, 0.8, 0.05, Y + 7.9, Y + 8.05, PoleWood);
			if (Fine)
			{
				static const Rgb Porcelain = {0.55f, 0.56f, 0.55f};
				for (double Su : {-0.65, 0.0, 0.65})
					Post(F.At.X + Side.X * Su, F.At.Z + Side.Z * Su, Y + 8.05, Y + 8.25, 0.045, 0.03, Porcelain);
				Post(F.At.X, F.At.Z, Y + 8.5, Y + 8.56, 0.12, 0.02, Shade(PoleWood, 0.7));
			}
			break;
		}
		case PostBox:
		{
			Box(Out, F.At.X, F.At.Z, Face, 0.25, 0.25, Y - 0.1, Y + 1.45, Red);
			Box(Out, F.At.X, F.At.Z, Face, 0.28, 0.28, Y + 1.45, Y + 1.55, Shade(Red, 0.7));
			Board(At3(F.At.X, Y + 1.15, F.At.Z), Face, 0.3, 0.04, Black, 0.26);
			break;
		}
		case BusStop:
		{
			Post(F.At.X, F.At.Z, Y - 0.3, Y + 2.7, 0.045, 0.04, Grey);
			const P3 Mid = At3(F.At.X + Side.X * 0.3, Y + 2.45, F.At.Z + Side.Z * 0.3);
			for (int Back = 0; Back < 2; ++Back) Board(Mid, Back ? Face * -1.0 : Face, 0.5, 0.42, White, 0.01);
			for (int Back = 0; Back < 2; ++Back)
				Label(At3(Mid.X + (Back ? -Face.X : Face.X) * 0.03, Mid.Y, Mid.Z + (Back ? -Face.Z : Face.Z) * 0.03), Back ? Face * -1.0 : Face, 0.14, "BUS", Black);
			break;
		}
		case Seat:
		{
			Box(Out, F.At.X, F.At.Z, Side, 0.8, 0.22, Y + 0.42, Y + 0.47, Wood);
			Box(Out, F.At.X - Face.X * 0.2, F.At.Z - Face.Z * 0.2, Side, 0.8, 0.03, Y + 0.5, Y + 0.85, Wood);
			for (double Su : {-0.7, 0.7}) Box(Out, F.At.X + Side.X * Su, F.At.Z + Side.Z * Su, Side, 0.04, 0.2, Y - 0.1, Y + 0.42, Steel);
			break;
		}
		case PhoneBox:
		{
			// A red box with a domed top, its glazed sides, and TELEPHONE over the door.
			Box(Out, F.At.X, F.At.Z, Face, 0.45, 0.45, Y - 0.1, Y + 2.3, Red);
			Box(Out, F.At.X, F.At.Z, Face, 0.5, 0.5, Y + 2.3, Y + 2.45, Shade(Red, 0.85));
			Box(Out, F.At.X, F.At.Z, Face, 0.36, 0.36, Y + 2.45, Y + 2.55, Shade(Red, 0.75));
			for (int Way = 0; Way < 4; ++Way)
			{
				const P2 N = Way == 0 ? Face : Way == 1 ? Side : Way == 2 ? Face * -1.0 : Side * -1.0;
				Board(At3(F.At.X, Y + 1.3, F.At.Z), N, 0.62, 1.4, Shade(Grey, 0.35), 0.455);
				Board(At3(F.At.X, Y + 2.17, F.At.Z), N, 0.7, 0.13, White, 0.505);
			}
			Label(At3(F.At.X + Face.X * 0.52, Y + 2.17, F.At.Z + Face.Z * 0.52), Face, 0.09, "TELEPHONE", Black);
			break;
		}
		case PubSign:
		{
			// On the pub's front wall (F.At on the wall, Face out of it): a painted fascia board along it over
			// the ground floor's windows, the name in cream letters, a moulded edge round it; F.Size is the
			// widest it may be, F.Big the building's height.
			static const Rgb Paints[4] = {{0.01f, 0.06f, 0.025f}, {0.12f, 0.012f, 0.015f}, {0.008f, 0.015f, 0.07f}, {0.012f, 0.012f, 0.012f}};
			static const Rgb Cream = {0.70f, 0.64f, 0.48f};
			const Rgb& Paint = Paints[std::min(3, int(Chance(F.At.X, F.At.Z, 52) * 4.0))];
			const double Letters = double(F.Text.size());
			const double W = std::clamp(0.8 + 0.2 * Letters, 2.0, std::fmax(2.0, F.Size));
			const double Hb = 0.55;
			const double Lift = F.Big > 5.0 ? 2.75 : std::fmax(2.2, F.Big * 0.5);
			const P3 Mid = At3(F.At.X, Y + Lift, F.At.Z);
			Board(Mid, Face, W + 0.12, Hb + 0.12, Shade(Paint, 0.6), 0.03);
			Board(Mid, Face, W, Hb, Paint, 0.06);
			Label(At3(Mid.X + Face.X * 0.075, Mid.Y, Mid.Z + Face.Z * 0.075), Face, std::fmin(0.3, 0.9 * (W - 0.3) / std::fmax(1.0, Letters * 0.6)), F.Text, Cream);
			break;
		}
		case CrossingBarrier:
		{
			// The post with its two red lights in a black board facing the traffic, and the red and
			// white barrier arm raised.
			Box(Out, F.At.X, F.At.Z, Face, 0.08, 0.08, Y - 0.3, Y + 2.6, White);
			const P3 Lights = At3(F.At.X, Y + 2.2, F.At.Z);
			Board(Lights, Face, 1.0, 0.45, Black, 0.09);
			for (double Su : {-0.3, 0.3})
				Disc(Out, At3(Lights.X + Side.X * Su + Face.X * 0.1, Lights.Y, Lights.Z + Side.Z * Su + Face.Z * 0.1), Face, 0.11, Shade(Red, 1.3), 12);
			Box(Out, F.At.X + Side.X * 0.3, F.At.Z + Side.Z * 0.3, Face, 0.18, 0.18, Y - 0.1, Y + 1.1, Shade(Grey, 0.6));
			for (int K = 0; K < 6; ++K)
				Box(Out, F.At.X + Side.X * 0.3, F.At.Z + Side.Z * 0.3, Face, 0.05, 0.05, Y + 1.1 + K * 0.6, Y + 1.7 + K * 0.6, K % 2 == 0 ? Red : White);
			break;
		}
		case Milestone:
		{
			Box(Out, F.At.X, F.At.Z, Face, 0.18, 0.12, Y - 0.2, Y + 0.75, Stone3);
			break;
		}
		case FieldGate:
		{
			// Five bars and a brace between two posts, in line with its wall, galvanised or wooden.
			const Rgb Bar = Chance(F.At.X, F.At.Z, 31) < 0.6 ? Steel : Shade(Wood, 1.6);
			const double Half = F.Size * 0.5;
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Face * (Su * (Half + 0.12));
				Box(Out, Q.X, Q.Z, Face, 0.1, 0.1, Y - 0.3, Y + 1.35, Shade(Stone3, 1.1));
			}
			const P2 A = F.At + Face * -Half, B = F.At + Face * Half;
			for (int K = 0; K < 5; ++K)
			{
				const double H = 0.25 + K * 0.22;
				Quad(Out, At3(A.X, Y + H, A.Z), At3(B.X, Y + H, B.Z), At3(B.X, Y + H + 0.06, B.Z), At3(A.X, Y + H + 0.06, A.Z), At3(F.At.X + Side.X * 50.0, Y + 1.0, F.At.Z + Side.Z * 50.0), Bar, true);
			}
			Quad(Out, At3(A.X, Y + 0.25, A.Z), At3(A.X, Y + 0.31, A.Z), At3(B.X, Y + 1.19, B.Z), At3(B.X, Y + 1.13, B.Z), At3(F.At.X + Side.X * 50.0, Y + 1.0, F.At.Z + Side.Z * 50.0), Bar, true);
			break;
		}
		// [OSM stage 2] Stand-ins until the kit has models for them.
		case Stile:
		{
			// (a step stile: two posts in the line, a rail between them and a step each side)
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Face * (Su * 0.45);
				Box(Out, Q.X, Q.Z, Face, 0.06, 0.06, Y - 0.2, Y + 1.2, Shade(Wood, 1.4));
			}
			Box(Out, F.At.X, F.At.Z, Face, 0.5, 0.04, Y + 0.95, Y + 1.02, Shade(Wood, 1.5));
			for (double Sv : {-1.0, 1.0})
			{
				const P2 Q = F.At + Side * (Sv * 0.3);
				Box(Out, Q.X, Q.Z, Face, 0.4, 0.12, Y + 0.38, Y + 0.43, Shade(Wood, 1.6));
			}
			break;
		}
		case KissingGate:
		{
			// (a gate swinging in a three-sided pen of rails, between two posts)
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Face * (Su * 0.75);
				Box(Out, Q.X, Q.Z, Face, 0.07, 0.07, Y - 0.2, Y + 1.3, Shade(Wood, 1.4));
			}
			const P2 Back = F.At + Side * 0.7;
			for (double H : {0.45, 0.95})
			{
				Box(Out, Back.X, Back.Z, Face, 0.75, 0.03, Y + H, Y + H + 0.06, Shade(Wood, 1.5));
				for (double Su : {-1.0, 1.0})
				{
					const P2 Q = F.At + Face * (Su * 0.75) + Side * 0.35;
					Box(Out, Q.X, Q.Z, Side, 0.35, 0.03, Y + H, Y + H + 0.06, Shade(Wood, 1.5));
				}
			}
			Box(Out, F.At.X + Side.X * 0.35, F.At.Z + Side.Z * 0.35, Side, 0.02, 0.6, Y + 0.25, Y + 1.15, Shade(Wood, 1.7));
			break;
		}
		case LiftGate:
		{
			// (a post, and the red and white arm raised beside it)
			Box(Out, F.At.X, F.At.Z, Face, 0.1, 0.1, Y - 0.2, Y + 1.1, Grey);
			Box(Out, F.At.X + Side.X * 0.12, F.At.Z + Side.Z * 0.12, Face, 0.03, 0.03, Y + 1.0, Y + 3.6, White);
			break;
		}
		case Bollard:
		{
			Box(Out, F.At.X, F.At.Z, Face, 0.08, 0.08, Y - 0.1, Y + 0.95, Britain() ? White : Grey);
			Box(Out, F.At.X, F.At.Z, Face, 0.085, 0.085, Y + 0.7, Y + 0.78, Black);
			break;
		}
		case PicnicTable:
		{
			// (a timber top and a bench each side, along the road)
			Box(Out, F.At.X, F.At.Z, Side, 0.9, 0.35, Y + 0.72, Y + 0.77, Shade(Wood, 1.7));
			for (double Sv : {-1.0, 1.0})
			{
				const P2 Q = F.At + Face * (Sv * 0.6);
				Box(Out, Q.X, Q.Z, Side, 0.9, 0.13, Y + 0.42, Y + 0.46, Shade(Wood, 1.7));
			}
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Side * (Su * 0.75);
				Box(Out, Q.X, Q.Z, Face, 0.7, 0.04, Y, Y + 0.72, Shade(Wood, 1.4));
			}
			break;
		}
		case InfoBoard:
		{
			// (two legs and a sloping panel facing the road)
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Side * (Su * 0.5);
				Box(Out, Q.X, Q.Z, Face, 0.05, 0.05, Y - 0.2, Y + 1.0, Shade(Wood, 1.3));
			}
			const P2 U = Side;
			const P3 Lo3 = At3(F.At.X + Face.X * 0.15, Y + 0.85, F.At.Z + Face.Z * 0.15), Hi3 = At3(F.At.X - Face.X * 0.15, Y + 1.25, F.At.Z - Face.Z * 0.15);
			const auto Pt = [&](const P3& C, double Su) { return At3(C.X + U.X * Su * 0.55, C.Y, C.Z + U.Z * Su * 0.55); };
			Quad(Out, Pt(Lo3, -1.0), Pt(Lo3, 1.0), Pt(Hi3, 1.0), Pt(Hi3, -1.0), At3(F.At.X + Face.X * 50.0, Y + 60.0, F.At.Z + Face.Z * 50.0), Shade(Grey, 1.6), true);
			if (!F.Text.empty())
			{
				// [places stage 7] A place's board lettered: what it is ("Stone circle"), then its note in
				// lines of up to 26 letters, dark on the pale panel, read up close.
				static const Rgb Ink = {0.03f, 0.03f, 0.035f};
				const size_t Bar = F.Text.find('|');
				std::vector<std::string> Lines = {F.Text.substr(0, Bar)};
				if (Bar != std::string::npos)
				{
					std::string Line, Word;
					const std::string Note = F.Text.substr(Bar + 1) + " ";
					for (char C : Note)
					{
						if (C != ' ') { Word += C; continue; }
						if (!Line.empty() && Line.size() + 1 + Word.size() > 26) { Lines.push_back(Line); Line.clear(); }
						Line += (Line.empty() ? "" : " ") + Word;
						Word.clear();
					}
					if (!Line.empty()) Lines.push_back(Line);
				}
				for (size_t L = 0; L < Lines.size() && L < 4; ++L)
				{
					// (down the sloping panel: each line a little lower and further forward)
					const double T = 0.8 - 0.2 * double(L) - (L > 0 ? 0.1 : 0.0);
					const P3 C = At3(Lo3.X + (Hi3.X - Lo3.X) * T + Face.X * 0.05, Lo3.Y + (Hi3.Y - Lo3.Y) * T, Lo3.Z + (Hi3.Z - Lo3.Z) * T + Face.Z * 0.05);
					Label(C, Face, L == 0 ? 0.07 : 0.045, Lines[L], Ink);
				}
			}
			break;
		}
		case PlaceBoard:
		{
			// [places stage 2] A place's name board: cream letters on heritage brown, on two oak posts; a
			// place with no name a plain oak marker post with a brown band.
			static const Rgb Brown = {0.075f, 0.032f, 0.014f}, Cream = {0.62f, 0.58f, 0.47f}, Oak = {0.10f, 0.075f, 0.05f};
			if (F.Text.empty())
			{
				Box(Out, F.At.X, F.At.Z, Face, 0.07, 0.07, Y - 0.2, Y + 1.1, Oak);
				Box(Out, F.At.X, F.At.Z, Face, 0.075, 0.075, Y + 0.85, Y + 1.0, Brown);
				break;
			}
			const std::string Words = F.Text.size() > 34 ? F.Text.substr(0, 33) + "." : F.Text;
			const double W = std::clamp(0.4 + 0.11 * double(Words.size()), 1.0, 3.6), H = 0.42;
			for (double Su : {-1.0, 1.0})
			{
				const P2 Q = F.At + Side * (Su * (W * 0.5 - 0.12));
				Box(Out, Q.X, Q.Z, Face, 0.09, 0.09, Y - 0.3, Y + 1.45, Oak);
			}
			const P3 Mid = At3(F.At.X, Y + 1.15, F.At.Z);
			Board(Mid, Face, W + 0.06, H + 0.06, Oak, 0.05);
			Board(Mid, Face * -1.0, W + 0.06, H + 0.06, Oak, 0.03);
			Board(Mid, Face, W, H, Brown, 0.065);
			Label(At3(Mid.X + Face.X * 0.08, Mid.Y, Mid.Z + Face.Z * 0.08), Face, 0.15, Words, Cream);
			break;
		}
		case TrafficSignal:
		{
			// [OSM stage 5D] A grey pole, and on it a black head with a white-edged backing board facing the
			// traffic: red over amber over green, lit in turn by the shared clock (M_RideSignal).
			static const Rgb LampRed = {1.0f, 0.03f, 0.01f}, LampAmber = {1.0f, 0.35f, 0.0f}, LampGreen = {0.05f, 1.0f, 0.35f};
			Box(Out, F.At.X, F.At.Z, Face, 0.055, 0.055, Y - 0.3, Y + 2.35, Shade(Grey, 0.7));
			Box(Out, F.At.X, F.At.Z, Face, 0.13, 0.17, Y + 2.3, Y + 3.3, Black);
			const P3 Head = At3(F.At.X, Y + 2.8, F.At.Z);
			Board(Head, Face * -1.0, 0.62, 1.22, Black, 0.13);
			Board(Head, Face, 0.62, 1.22, White, -0.135);
			Board(Head, Face, 0.56, 1.16, Black, -0.13);
			RideMesh& Lamps = Out.SignalsOf();
			const Rgb* Colours[3] = {&LampRed, &LampAmber, &LampGreen};
			for (int L = 0; L < 3; ++L)
			{
				const size_t V0 = Lamps.Vertices();
				Disc(Lamps, At3(Head.X + Face.X * 0.14, Head.Y + 0.3 - 0.3 * L, Head.Z + Face.Z * 0.14), Face, 0.1, *Colours[L], 14);
				for (size_t V = V0; V < Lamps.Vertices(); ++V)
				{
					Lamps.Uv[V * 2] = float(F.Size);
					Lamps.Uv[V * 2 + 1] = float(SignalRed + L);
				}
			}
			break;
		}
		case Beacon:
		{
			// [OSM stage 5D] A Belisha beacon: a pole banded black and white, an amber globe flashing on top.
			static const Rgb Globe = {1.0f, 0.4f, 0.0f};
			for (int Band = 0; Band < 8; ++Band)
				Box(Out, F.At.X, F.At.Z, Face, 0.045, 0.045, Y - 0.3 + (Band == 0 ? 0.0 : 0.3 * Band), Y + 0.3 * (Band + 1), Band % 2 == 0 ? Black : White);
			RideMesh& Lamps = Out.SignalsOf();
			const size_t V0 = Lamps.Vertices();
			// (the globe: four discs round, two across, enough at a glance)
			const double Gy = Y + 2.62;
			for (int K = 0; K < 4; ++K)
			{
				const double A = 1.5707963 * K;
				const P2 N(std::cos(A), std::sin(A));
				Disc(Lamps, At3(F.At.X + N.X * 0.12, Gy, F.At.Z + N.Z * 0.12), N, 0.17, Globe, 12);
			}
			for (size_t V = V0; V < Lamps.Vertices(); ++V)
			{
				Lamps.Uv[V * 2] = 0.0f;
				Lamps.Uv[V * 2 + 1] = float(SignalBeacon);
			}
			break;
		}
		default: break;
		}
	}
}

// --- Water (stage 5) ----------------------------------------------------------------------------------

namespace
{
const Rgb SeaWater = {0.022f, 0.048f, 0.062f}, LakeWater = {0.026f, 0.040f, 0.044f}, RiverWater = {0.034f, 0.044f, 0.038f};
}

void RidePlan::WaterTile(double X0, double Z0, double Size, int Cells, const RideMesh& Sheet, RideMesh& Out) const
{
	Out = RideMesh();
	Out.OriginX = X0;
	Out.OriginZ = Z0;
	if (!Land || Cells < 1) return;
	const int N = Cells + 1;
	const double Step = Size / Cells;
	if (Sheet.Vertices() < size_t(N) * N) return;
	// Each point of the square: 0 dry, 1 a lake or a wide river (the land's cover), 2 the sea.
	std::vector<unsigned char> Wet(size_t(N) * N, 0);
	std::vector<double> Held(size_t(N) * N, 0.0);   // (a reservoir's level, where one is)
	bool AnyWet = false, Reservoirs = false;
	std::vector<int> MappedWater;   // [OSM stage 4] (the map's own water shapes here)
	{
		std::vector<int> Here;
		FieldsIn(X0, Z0, Size, Here);
		for (int Fa : Here)
			if (FieldAreas[size_t(Fa)].Kind == FieldReservoir) { Reservoirs = true; MappedWater.push_back(Fa); }   // (a river's own outline: kept dry of the cover's water too)
	}
	for (int J = 0; J < N; ++J)
		for (int I = 0; I < N; ++I)
		{
			const double X = X0 + I * Step, Z = Z0 + J * Step;
			unsigned char W = Land->SeaAt(X, Z) ? 2 : Land->Cover(X, Z) == Terrain::CoverWater ? 1 : 0;
			// [OSM stage 2 batch D] (a mapped reservoir: flat, at its own level)
			if (W != 2 && Reservoirs)
			{
				const int Fa = FieldAt(X, Z);
				if (Fa >= 0 && FieldAreas[size_t(Fa)].Kind == FieldReservoir && !FieldAreas[size_t(Fa)].Flowing)
				{
					W = 3;
					Held[size_t(J) * N + I] = FieldAreas[size_t(Fa)].Level;
				}
				// [OSM stage 4] (the land's cover is 10 m squares from space, a little off the map: where the
				// map draws a lake or river's own shape, the cover's water beside that shape is dry land)
				else if (W == 1 && !MappedWater.empty())
					for (int Wa : MappedWater)
					{
						const FieldArea& A = FieldAreas[size_t(Wa)];
						if (X >= A.MinX - 15.0 && X <= A.MaxX + 15.0 && Z >= A.MinZ - 15.0 && Z <= A.MaxZ + 15.0) { W = 0; break; }
					}
			}
			// ([build 147 feedback] a river's water by the land's cover is its run's, drawn level across)
			if (W == 1)
			{
				double Wd = 0.0, Ws = 0.0, Wh = 0.0, Wdp = 0.0;
				if (NearWater(X, Z, Wd, Ws, Wh, Wdp) && Wd < Wh + 12.0) W = 0;
			}
			// (never on a road: a causeway or a road along a lake's edge stays dry)
			if (W != 0)
			{
				NearOpts Opts;
				Opts.NoDecks = true;
				NearHit Hit;
				if (NearestOf(X, Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.0) W = 0;
			}
			Wet[size_t(J) * N + I] = W;
			AnyWet = AnyWet || W != 0;
		}
	// (beenster, build 141: lakes and wide rivers lay on the ground as a tilted sheet, every point at
	// its own ground. Water lies level: each wet point at the lowest wet ground within Reach of it, so
	// a lake is flat to its shore and a river steps down its valley only over tens of metres. The
	// window runs past the square's edge on the same lattice, so the squares either side agree.)
	std::vector<double> Lake(size_t(N) * N, std::nan(""));
	if (AnyWet)
	{
		const int Reach = std::max(2, int(std::lround(40.0 / Step)));
		const int M = N + 2 * Reach;
		std::vector<double> Low(size_t(M) * M, 1e9), Row(size_t(M) * M, 1e9);
		for (int J = 0; J < M; ++J)
			for (int I = 0; I < M; ++I)
			{
				const int Ii = I - Reach, Jj = J - Reach;
				const bool In = Ii >= 0 && Ii < N && Jj >= 0 && Jj < N;
				const double X = X0 + Ii * Step, Z = Z0 + Jj * Step;
				const bool LakeHere = In ? Wet[size_t(Jj) * N + Ii] == 1 : (!Land->SeaAt(X, Z) && Land->Cover(X, Z) == Terrain::CoverWater);
				if (!LakeHere) continue;
				const double Y = In ? double(Sheet.Pos[(size_t(Jj) * N + Ii) * 3 + 1]) : Ground(X, Z, Step);
				if (!std::isnan(Y)) Low[size_t(J) * M + I] = Y;
			}
		for (int J = 0; J < M; ++J)
			for (int I = Reach; I < M - Reach; ++I)
			{
				double Least = 1e9;
				for (int D = -Reach; D <= Reach; ++D) Least = std::fmin(Least, Low[size_t(J) * M + I + D]);
				Row[size_t(J) * M + I] = Least;
			}
		for (int J = 0; J < N; ++J)
			for (int I = 0; I < N; ++I)
			{
				if (Wet[size_t(J) * N + I] != 1) continue;
				double Least = 1e9;
				for (int D = -Reach; D <= Reach; ++D) Least = std::fmin(Least, Row[size_t(J + Reach + D) * M + I + Reach]);
				if (Least < 1e8) Lake[size_t(J) * N + I] = Least + 0.1;
			}
	}
	const auto Put = [&Out](double X, double Y, double Z, const Rgb& C)
	{
		Out.Pos.push_back(float(X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z - Out.OriginZ));
		Out.Nrm.push_back(0.0f); Out.Nrm.push_back(1.0f); Out.Nrm.push_back(0.0f);
		// (metres over 8 from a corner near the square, a whole 10 of M_RideWater's two ripples' repeats
		// (0.9 and 2.3 to the unit) from the next square's: half precision UVs 40 km out were whole units)
		Out.Uv.push_back(float((X - std::floor(Out.OriginX / 80.0) * 80.0) / 8.0)); Out.Uv.push_back(float((Z - std::floor(Out.OriginZ / 80.0) * 80.0) / 8.0));
		Out.Col.push_back(C.R); Out.Col.push_back(C.G); Out.Col.push_back(C.B);
	};
	if (AnyWet)
	{
		// A quad of water over every cell with a wet corner: a wet corner at its own ground and a little
		// over (the sea at its level), a dry one no higher than the lowest wet one, so the water runs
		// under the bank and the land itself draws the shore.
		for (int J = 0; J < Cells; ++J)
			for (int I = 0; I < Cells; ++I)
			{
				const int Ci[4] = {I, I + 1, I, I + 1}, Cj[4] = {J, J, J + 1, J + 1};
				double Level[4], Lowest = 1e9;
				bool Sea = false, Any = false;
				for (int K = 0; K < 4; ++K)
				{
					const size_t V = size_t(Cj[K]) * N + Ci[K];
					Level[K] = std::nan("");
					if (Wet[V] == 0) continue;
					Any = true;
					Sea = Sea || Wet[V] == 2;
					Level[K] = Wet[V] == 2 ? 0.0 : Wet[V] == 3 ? Held[V]
						: !std::isnan(Lake[V]) ? Lake[V] : double(Sheet.Pos[V * 3 + 1]) + 0.12;
					Lowest = std::fmin(Lowest, Level[K]);
				}
				if (!Any) continue;
				const uint32_t Base = uint32_t(Out.Vertices());
				for (int K = 0; K < 4; ++K)
				{
					// (a dry corner a little under its own ground, so on flat land the shore runs between
					// the points, not round the squares)
					const double Y = std::isnan(Level[K]) ? std::fmin(Lowest, double(Sheet.Pos[(size_t(Cj[K]) * N + Ci[K]) * 3 + 1]) - 0.25) : Level[K];
					Put(X0 + Ci[K] * Step, Y, Z0 + Cj[K] * Step, Sea ? SeaWater : LakeWater);
				}
				// (A B across the top, C D below: as LandTile's own quads, facing up)
				Out.Idx.insert(Out.Idx.end(), {Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3});
			}
	}
	// The map's rivers, streams and canals (WaterRuns): their water level across at each point's level,
	// out under the lip of each bank, on the finer squares.
	if (Step > 8.5 || WaterNear.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const auto LeftOf = [](const WaterRun& Run, size_t I)
	{
		const size_t Count = Run.At.size();
		const P2 D = Run.At[std::min(Count - 1, I + 1)] - Run.At[I > 0 ? I - 1 : 0];
		const double L = D.Length();
		return L > 1e-9 ? P2(D.Z / L, -D.X / L) : P2(1.0, 0.0);
	};
	const int CellX0 = int(std::floor(X0 / WaterCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / WaterCell)), CellZ0 = int(std::floor(Z0 / WaterCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / WaterCell));
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = WaterNear.find(Key);
			if (It == WaterNear.end()) continue;
			for (const std::pair<int, int>& Bit : It->second)
			{
				const WaterRun& Run = WaterRuns[size_t(Bit.first)];
				const size_t I = size_t(Bit.second);
				const P2 A = Run.At[I], B = Run.At[I + 1];
				const P2 Mid = (A + B) * 0.5;
				// (each bit once: in the square its middle is in)
				if (int(std::floor(Mid.X / WaterCell)) != Cx || int(std::floor(Mid.Z / WaterCell)) != Cz || !Inside(Mid)) continue;
				if (Land->SeaAt(Mid.X, Mid.Z) || !std::isnan(StillLevel(Mid.X, Mid.Z))) continue;   // (the sea's or a lake's own water)
				const double Ha = Run.Half[I] + 0.35, Hb = Run.Half[I + 1] + 0.35;
				{
					// (under a road, not a bridge: a culvert)
					NearOpts Opts;
					Opts.NoDecks = true;
					NearHit Hit;
					if (NearestOf(Mid.X, Mid.Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.0 && Run.Kind == 1) continue;
				}
				const P2 La = LeftOf(Run, I), Lb = LeftOf(Run, I + 1);
				const Rgb C = Run.Kind == 2 ? LakeWater : RiverWater;
				const uint32_t Base = uint32_t(Out.Vertices());
				Put(A.X + La.X * Ha, Run.Surface[I], A.Z + La.Z * Ha, C);
				Put(A.X - La.X * Ha, Run.Surface[I], A.Z - La.Z * Ha, C);
				Put(B.X + Lb.X * Hb, Run.Surface[I + 1], B.Z + Lb.Z * Hb, C);
				Put(B.X - Lb.X * Hb, Run.Surface[I + 1], B.Z - Lb.Z * Hb, C);
				// (left is the run's left going down it, so these face up)
				Out.Idx.insert(Out.Idx.end(), {Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
			}
		}
}

// --- Paths (OSM stage 4) ------------------------------------------------------------------------------

namespace
{
// A path's width (m) where the map gives none, and its colour by its surface (or its kind's own).
double PathWidth(const GroundShape& P)
{
	if (P.Width > 0.3) return std::fmin(8.0, P.Width);
	if (P.Kind == "pedestrian") return 5.0;
	if (P.Kind == "bridleway" || P.Kind == "cycleway") return 2.5;
	if (P.Kind == "steps") return 1.8;
	if (P.Kind == "footway") return 1.8;
	return 1.0;   // (a path across fields: a worn line)
}

Rgb PathColour(const GroundShape& P)
{
	const std::string& S = P.Word;
	const Rgb Tarmac = {0.045f, 0.045f, 0.047f}, Flags = {0.13f, 0.125f, 0.115f}, Gravel = {0.15f, 0.135f, 0.11f}, Dirt = {0.085f, 0.07f, 0.048f}, Worn = {0.075f, 0.08f, 0.04f}, Stone = {0.17f, 0.16f, 0.145f};
	if (S == "asphalt" || S == "paved" || S == "concrete") return Tarmac;
	if (S == "paving_stones" || S == "sett" || S == "cobblestone" || S == "bricks") return Flags;
	if (S == "gravel" || S == "fine_gravel" || S == "compacted" || S == "pebblestone") return Gravel;
	if (S == "dirt" || S == "ground" || S == "earth" || S == "mud" || S == "unpaved") return Dirt;
	if (S == "grass") return Worn;
	if (P.Kind == "steps") return Stone;
	if (P.Kind == "pedestrian") return Flags;
	if (P.Kind == "footway" || P.Kind == "cycleway") return Tarmac;
	if (P.Kind == "bridleway") return Dirt;
	return Worn;   // (a path: trodden grass)
}

// [beenster, build 147] Whether a path's surface is loose (gravel, dirt, grass), and the colour its worn
// middle takes so M_RideRoad draws it in a scan: a loose one on the gravel's run of colour (its stones),
// a tarmac one in the tarmac's own; flags and setts keep their colour.
bool PathIsLoose(const GroundShape& P)
{
	const std::string& S = P.Word;
	return !(S == "asphalt" || S == "paved" || S == "concrete" || S == "paving_stones" || S == "sett" || S == "cobblestone" || S == "bricks");
}

Rgb PathScanColour(const GroundShape& P, const Rgb& Own)
{
	const std::string& S = P.Word;
	if (S == "asphalt" || S == "paved") return Rgb{0.022f, 0.022f, 0.025f};
	if (PathIsLoose(P)) return S == "gravel" || S == "fine_gravel" || S == "compacted" || S == "pebblestone" ? Rgb{0.095f, 0.088f, 0.07f} : Rgb{0.072f, 0.078f, 0.045f};
	return Own;
}
}

void RidePlan::PathsTile(double X0, double Z0, double Size, double Step, RideMesh& Out) const
{
	if (!Scene || Scene->Paths.empty() || Step > 4.5) return;
	const Region& G = Area.Roads;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = Scene->Paths.find(Key);
			if (It == Scene->Paths.end()) continue;
			for (int S : It->second)
			{
				const GroundShape& Way = G.Ground.Paths[size_t(S)];
				const double Half = PathWidth(Way) * 0.5;
				const Rgb Base = PathColour(Way);
				const double Lift = Way.Kind == "steps" ? 0.08 : 0.045;
				for (size_t I = 0; I + 1 < Way.Pts.size(); ++I)
				{
					const P2 A = Way.Pts[I], B = Way.Pts[I + 1];
					const P2 Mid = (A + B) * 0.5;
					if (SceneKey(Mid.X, Mid.Z) != Key || !Inside(Mid)) continue;
					const double Len = A.DistanceTo(B);
					if (Len < 0.2) continue;
					const P2 Dir = (B - A) * (1.0 / Len), Left(Dir.Z, -Dir.X);
					// (each end runs on past its point by a little of its width, so a corner of the line has no notch)
					const double Over = std::fmin(Half * 0.7, 1.0);
					const P2 From = A - Dir * (I > 0 ? Over : 0.0), To = B + Dir * (I + 2 < Way.Pts.size() ? Over : 0.0);
					const double Run = From.DistanceTo(To);
					const int Bits = std::max(1, int(std::ceil(Run / 2.0)));
					for (int K = 0; K < Bits; ++K)
					{
						const P2 Pa = From + (To - From) * (double(K) / Bits), Pb = From + (To - From) * (double(K + 1) / Bits);
						const P2 Pm = (Pa + Pb) * 0.5;
						NearOpts Opts;
						NearHit Hit;
						if (NearestOf(Pm.X, Pm.Z, Opts, Hit) && Hit.Dist <= Hit.Half + Half + 0.4) continue;   // (on or at the road: the road's own)
						if (Land->Cover(Pm.X, Pm.Z) == Terrain::CoverWater || Land->SeaAt(Pm.X, Pm.Z)) continue;
						// (beenster, build 147, Jenkin Chapel: a path was a flat slab with hard straight edges on the
						// grass. Now its worn middle in the colour M_RideRoad draws as its surface's scan, and each
						// side a ragged lip from the verge's grit out to its grass, every 1.5 m its own width, laid
						// down onto the ground at its outer edge, so the grass scan breaks through it as on a verge)
						static const Rgb VergeGrit = {0.072f, 0.078f, 0.045f}, VergeLawn = {0.10f, 0.16f, 0.03f};
						const auto Spread = [&](const P2& P, double Side, double& In, double& OutW)
						{
							const double Nx = std::round(P.X * 10.0), Nz = std::round(P.Z * 10.0);
							In = Half * (0.7 + 0.35 * Chance(Nx, Nz, Side > 0.0 ? 101 : 102));
							OutW = In + std::fmin(0.9, 0.25 + Half * 0.4 * Chance(Nx, Nz, Side > 0.0 ? 103 : 104));
						};
						double InA[2], OutA[2], InB[2], OutB[2];
						Spread(Pa, 1.0, InA[0], OutA[0]); Spread(Pa, -1.0, InA[1], OutA[1]);
						Spread(Pb, 1.0, InB[0], OutB[0]); Spread(Pb, -1.0, InB[1], OutB[1]);
						// (left outer, left inner, right inner, right outer at each end)
						const P2 Qa[4] = {Pa + Left * OutA[0], Pa + Left * InA[0], Pa - Left * InA[1], Pa - Left * OutA[1]};
						const P2 Qb[4] = {Pb + Left * OutB[0], Pb + Left * InB[0], Pb - Left * InB[1], Pb - Left * OutB[1]};
						double Ya[4], Yb[4];
						bool Laid = true;
						for (int C = 0; C < 4 && Laid; ++C)
						{
							// (the middle at the higher of its edge and the line's own ground, so the ground doesn't show
							// through; the lips down to the ground)
							const bool Lip = C == 0 || C == 3;
							const double Ea = Ground(Qa[C].X, Qa[C].Z, Step), Eb = Ground(Qb[C].X, Qb[C].Z, Step), Ca = Ground(Pa.X, Pa.Z, Step), Cb = Ground(Pb.X, Pb.Z, Step);
							Laid = !std::isnan(Ea) && !std::isnan(Eb) && !std::isnan(Ca) && !std::isnan(Cb);
							Ya[C] = Lip ? Ea + 0.012 : std::fmax(Ea, Ca - 0.15) + Lift * 0.6;
							Yb[C] = Lip ? Eb + 0.012 : std::fmax(Eb, Cb - 0.15) + Lift * 0.6;
						}
						if (!Laid) continue;
						const Rgb Middle = PathScanColour(Way, Base);
						const bool Grit = PathIsLoose(Way);
						const Rgb Inner = Grit ? VergeGrit : Middle;
						const auto Band = [&](int L, const Rgb& Cl, const Rgb& Cr)
						{
							const size_t Was = Out.Vertices();
							Quad(Out, At3(Qa[L].X, Ya[L], Qa[L].Z), At3(Qb[L].X, Yb[L], Qb[L].Z), At3(Qb[L + 1].X, Yb[L + 1], Qb[L + 1].Z), At3(Qa[L + 1].X, Ya[L + 1], Qa[L + 1].Z),
								At3(Pm.X, Ya[1] + 100.0, Pm.Z), Cl);
							if (Out.Vertices() == Was + 4)
								for (size_t V : {Was + 2, Was + 3}) { Out.Col[V * 3] = Cr.R; Out.Col[V * 3 + 1] = Cr.G; Out.Col[V * 3 + 2] = Cr.B; }
						};
						Band(0, VergeLawn, Inner);
						Band(1, Middle, Middle);
						Band(2, Inner, VergeLawn);
					}
				}
			}
		}
}

// --- Railways (stage 5) -------------------------------------------------------------------------------

namespace
{
// How far P is from the segment A B, and how far along it (0..1) the nearest point is.
double ToSegment(const P2& P, const P2& A, const P2& B, double& T)
{
	const P2 D = B - A;
	const double L2 = D.X * D.X + D.Z * D.Z;
	T = L2 > 1e-12 ? std::clamp(((P.X - A.X) * D.X + (P.Z - A.Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
	return P.DistanceTo(A + D * T);
}

long long RailNearKey(double X, double Z, double Cell)
{
	return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell));
}
} // namespace

bool RidePlan::NearRail(double X, double Z, double& Dist, double& Top, int& On, bool* Disused, double* Room) const
{
	if (!Scene || Scene->RailNearBy.empty()) return false;
	const auto It = Scene->RailNearBy.find(RailNearKey(X, Z, RailCell));
	if (It == Scene->RailNearBy.end()) return false;
	const P2 Here(X, Z);
	double Best = 40.0;
	bool Found = false;
	for (const std::pair<int, int>& Bit : It->second)
	{
		const RailLine& Run = Rails[size_t(Bit.first)];
		const size_t I = size_t(Bit.second);
		double T = 0.0;
		const double D = ToSegment(Here, Run.At[I], Run.At[I + 1], T);
		if (D >= Best) continue;
		Best = D;
		Found = true;
		Dist = D;
		Top = Run.Top[I] + (Run.Top[I + 1] - Run.Top[I]) * T;
		On = Run.On[T < 0.5 ? I : I + 1];
		if (Disused) *Disused = Run.Disused;
		if (Room) *Room = Run.Room.size() == Run.At.size() ? double(Run.Room[T < 0.5 ? I : I + 1]) : 1e9;
	}
	return Found;
}

void RidePlan::IndexRails()
{
	Rails.clear();
	Platforms.clear();
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	// The map's pieces of railway (not those still being built).
	std::vector<int> Pieces;
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& F = G.Scenery.Lines[L];
		const bool Rail = F.Kind == "railway" || F.Kind == "narrow_gauge" || F.Kind == "preserved" || F.Kind == "light_rail";
		if (Rail && F.Line.size() >= 2 && (F.Flags & 8) == 0) Pieces.push_back(int(L));
	}
	const auto OnOf = [](int Flags) { return static_cast<unsigned char>((Flags & 1) ? 1 : (Flags & 2) ? 2 : 0); };
	std::vector<char> Used(Pieces.size(), 0);
	for (size_t Seed = 0; Seed < Pieces.size(); ++Seed)
	{
		if (Used[Seed]) continue;
		Used[Seed] = 1;
		const FeatureLine& Start = G.Scenery.Lines[size_t(Pieces[Seed])];
		const bool Gone = (Start.Flags & 4) != 0;
		// Joined end to end with the pieces that meet it (a junction: the first found carries on), each
		// point with what the stretch after it is on.
		std::vector<P2> Pts(Start.Line.begin(), Start.Line.end());
		std::vector<unsigned char> After(Pts.size(), OnOf(Start.Flags));
		for (int End = 0; End < 2; ++End)
			for (bool Grew = true; Grew;)
			{
				Grew = false;
				const P2 Tip = End == 0 ? Pts.back() : Pts.front();
				for (size_t K = 0; K < Pieces.size() && !Grew; ++K)
				{
					if (Used[K]) continue;
					const FeatureLine& F = G.Scenery.Lines[size_t(Pieces[K])];
					if (((F.Flags & 4) != 0) != Gone) continue;
					const bool AtStart = F.Line.front().DistanceTo(Tip) < 0.5, AtEnd = F.Line.back().DistanceTo(Tip) < 0.5;
					if (!AtStart && !AtEnd) continue;
					std::vector<P2> More(F.Line.begin(), F.Line.end());
					if (End == 0 ? !AtStart : !AtEnd) std::reverse(More.begin(), More.end());
					const unsigned char On = OnOf(F.Flags);
					if (End == 0)
					{
						After.back() = On;
						Pts.insert(Pts.end(), More.begin() + 1, More.end());
						After.insert(After.end(), More.size() - 1, On);
					}
					else
					{
						Pts.insert(Pts.begin(), More.begin(), More.end() - 1);
						After.insert(After.begin(), More.size() - 1, On);
					}
					Used[K] = 1;
					Grew = true;
				}
			}
		// A point every RailStep metres along it.
		std::vector<P2> At;
		std::vector<unsigned char> On;
		double Carry = 0.0;
		for (size_t I = 0; I + 1 < Pts.size(); ++I)
		{
			const double Len = Pts[I].DistanceTo(Pts[I + 1]);
			if (Len < 1e-6) continue;
			double S = Carry;
			for (; S < Len; S += RailStep)
			{
				At.push_back(Pts[I] + (Pts[I + 1] - Pts[I]) * (S / Len));
				On.push_back(After[I]);
			}
			Carry = S - Len;
		}
		if (At.empty() || At.back().DistanceTo(Pts.back()) > 0.5)
		{
			At.push_back(Pts.back());
			On.push_back(After[Pts.size() >= 2 ? Pts.size() - 2 : 0]);
		}
		// (beenster, build 147, Whaley Bridge: a line mapped alongside a road, its bed over the road's edge.
		// Moved clear of a road it runs beside: its middle 3 m past the road's edge, eased in and out.)
		{
			std::vector<P2> Push(At.size(), P2(0.0, 0.0));
			bool Any = false;
			for (size_t K = 0; K < At.size(); ++K)
			{
				if (On[K] != 0) continue;
				NearOpts Opts;
				NearHit Hit;
				if (!NearestOf(At[K].X, At[K].Z, Opts, Hit) || Hit.Deck || Hit.Bore || Hit.Dist >= Hit.Half + 3.0 || Hit.Dist < 0.5) continue;
				const std::vector<RoadPoint>& Way = RoadOf(Hit.Which);
				const size_t Ri = size_t(std::clamp(Hit.Index, 0, int(Way.size()) - 1)), Rn = Ri + 1 < Way.size() ? Ri + 1 : Ri, Rp = Ri + 1 < Way.size() ? Ri : (Ri > 0 ? Ri - 1 : Ri);
				const P2 RoadDir = Way[Rn].At - Way[Rp].At;
				const P2 LineDir = At[K + 1 < At.size() ? K + 1 : K] - At[K > 0 ? K - 1 : K];
				const double Lr = RoadDir.Length(), Ll = LineDir.Length();
				if (Lr < 1e-6 || Ll < 1e-6 || std::fabs(RoadDir.X * LineDir.Z - RoadDir.Z * LineDir.X) / (Lr * Ll) >= 0.34) continue;
				const P2 Away = (At[K] - Hit.Foot) * (1.0 / Hit.Dist);
				Push[K] = Away * (Hit.Half + 3.0 - Hit.Dist);
				Any = true;
			}
			if (Any)
			{
				std::vector<P2> Eased(At.size(), P2(0.0, 0.0));
				for (size_t K = 0; K < At.size(); ++K)
				{
					// (the greatest push within 8 m, faded over the next 12 m)
					for (size_t J = K > 5 ? K - 5 : 0; J <= std::min(At.size() - 1, K + 5); ++J)
					{
						const double Gap = std::fabs(double(J) - double(K)) * RailStep, Keep = Gap <= 8.0 ? 1.0 : std::fmax(0.0, 1.0 - (Gap - 8.0) / 12.0);
						const P2 P = Push[J] * Keep;
						if (P.Length() > Eased[K].Length()) Eased[K] = P;
					}
				}
				for (size_t K = 0; K < At.size(); ++K) At[K] = At[K] + Eased[K];
			}
		}
		// The ground under it, and the stretches where the ride's land has it (each a line of its own).
		std::vector<double> Lie(At.size());
		for (size_t I = 0; I < At.size(); ++I) Lie[I] = Land->Height(At[I].X, At[I].Z);
		for (size_t From = 0; From < At.size();)
		{
			if (std::isnan(Lie[From])) { ++From; continue; }
			size_t To = From;
			while (To + 1 < At.size() && !std::isnan(Lie[To + 1])) ++To;
			const size_t Count = To - From + 1;
			if (Count >= 3)
			{
				RailLine Run;
				Run.Disused = Gone;
				Run.At.assign(At.begin() + long(From), At.begin() + long(To) + 1);
				Run.On.assign(On.begin() + long(From), On.begin() + long(To) + 1);
				std::vector<double> Y(Lie.begin() + long(From), Lie.begin() + long(To) + 1);
				// Bridges and tunnels straight from one end to the other.
				const auto Spans = [&Run, &Y]()
				{
					for (size_t A = 0; A < Y.size();)
					{
						if (Run.On[A] != 1 && Run.On[A] != 2) { ++A; continue; }
						size_t B = A;
						while (B + 1 < Y.size() && (Run.On[B + 1] == 1 || Run.On[B + 1] == 2)) ++B;
						const size_t Lo = A > 0 ? A - 1 : A, Hi = B + 1 < Y.size() ? B + 1 : B;
						if (Hi > Lo)
							for (size_t K = Lo + 1; K < Hi; ++K) Y[K] = Y[Lo] + (Y[Hi] - Y[Lo]) * double(K - Lo) / double(Hi - Lo);
						A = B + 1;
					}
				};
				Spans();
				// Eased along (a railway climbs gently: it cuts through the rises and banks over the dips),
				// then no steeper anywhere than 1 in 40: halfway between the highest such line under the
				// eased one and the lowest over it.
				for (int Pass = 0; Pass < 3; ++Pass)
				{
					std::vector<double> Sum(Y.size() + 1, 0.0);
					for (size_t K = 0; K < Y.size(); ++K) Sum[K + 1] = Sum[K] + Y[K];
					std::vector<double> Eased(Y.size());
					for (size_t K = 0; K < Y.size(); ++K)
					{
						const size_t A = K > 12 ? K - 12 : 0, B = std::min(Y.size() - 1, K + 12);
						Eased[K] = (Sum[B + 1] - Sum[A]) / double(B - A + 1);
					}
					Y.swap(Eased);
				}
				const double Grade = RailStep / 40.0;
				{
					std::vector<double> Under = Y, Over = Y;
					for (size_t K = 1; K < Y.size(); ++K) { Under[K] = std::fmin(Under[K], Under[K - 1] + Grade); Over[K] = std::fmax(Over[K], Over[K - 1] - Grade); }
					for (size_t K = Y.size() - 1; K-- > 0;) { Under[K] = std::fmin(Under[K], Under[K + 1] + Grade); Over[K] = std::fmax(Over[K], Over[K + 1] - Grade); }
					for (size_t K = 0; K < Y.size(); ++K) Y[K] = (Under[K] + Over[K]) * 0.5;
				}
				Spans();
				// The roads it meets: level with one it crosses, over one it bridges, under one that bridges it.
				std::vector<double> Floor(Y.size(), -1e9), Ceiling(Y.size(), 1e9), Level(Y.size(), std::nan(""));
				for (size_t K = 0; K < Y.size(); ++K)
				{
					NearOpts Opts;
					NearHit Hit;
					if (Run.On[K] == 2 || !NearestOf(Run.At[K].X, Run.At[K].Z, Opts, Hit) || Hit.Dist > Hit.Half + 2.0 || Hit.Bore) continue;
					const double Surface = Hit.Height + Hit.Cross;
					if (Hit.Deck && Run.On[K] != 1) Ceiling[K] = Surface - 6.5;
					else if (Run.On[K] == 1) Floor[K] = Surface + 6.0;
					else if (!Hit.Deck && Hit.Dist <= Hit.Half + 0.6)
					{
						// (beenster, build 147, Whaley Bridge: a line mapped alongside a road, within its width,
						// is not a level crossing; only one that crosses it, 20 degrees or more across it)
						const std::vector<RoadPoint>& Way = RoadOf(Hit.Which);
						const size_t Ri = size_t(std::clamp(Hit.Index, 0, int(Way.size()) - 1)), Rn = Ri + 1 < Way.size() ? Ri + 1 : Ri, Rp = Ri + 1 < Way.size() ? Ri : (Ri > 0 ? Ri - 1 : Ri);
						const P2 RoadDir = Way[Rn].At - Way[Rp].At;
						const P2 LineDir = Run.At[K + 1 < Run.At.size() ? K + 1 : K] - Run.At[K > 0 ? K - 1 : K];
						const double Lr = RoadDir.Length(), Ll = LineDir.Length();
						const double Across = Lr > 1e-6 && Ll > 1e-6 ? std::fabs(RoadDir.X * LineDir.Z - RoadDir.Z * LineDir.X) / (Lr * Ll) : 1.0;
						if (Across < 0.34) continue;
						Floor[K] = Ceiling[K] = Level[K] = Surface - 0.27;   // (the rails' tops flush with the road)
						Run.On[K] = 3;
					}
				}
				// (a railway's ramp to and from them: 1 in 40 too)
				const double Ramp = Grade;
				for (size_t K = 1; K < Y.size(); ++K) { Floor[K] = std::fmax(Floor[K], Floor[K - 1] - Ramp); Ceiling[K] = std::fmin(Ceiling[K], Ceiling[K - 1] + Ramp); }
				for (size_t K = Y.size() - 1; K-- > 0;) { Floor[K] = std::fmax(Floor[K], Floor[K + 1] - Ramp); Ceiling[K] = std::fmin(Ceiling[K], Ceiling[K + 1] + Ramp); }
				for (size_t K = 0; K < Y.size(); ++K) Y[K] = std::fmax(std::fmin(Y[K], Ceiling[K]), Floor[K]);
				// (beenster, build 147, Whaley Bridge: a crossing the line can't be level with, its bank's
				// climb to a bridge nearby holding it metres over the road, is a bridge over the road)
				{
					std::vector<unsigned char> Lifted(Y.size(), 0);
					for (size_t K = 0; K < Y.size(); ++K)
						if (Run.On[K] == 3 && !std::isnan(Level[K]) && Y[K] > Level[K] + 1.0) Lifted[K] = 1;
					// (and a bit either side: its abutments stand clear of the road's verge)
					for (size_t K = 0; K < Y.size(); ++K)
						if (Lifted[K] || (K > 0 && Lifted[K - 1]) || (K + 1 < Y.size() && Lifted[K + 1]))
							if (Run.On[K] == 3 || Run.On[K] == 0) Run.On[K] = 1;
				}
				Run.Top = Y;
				// (the room for its bank each side: to the nearest house, less 1.5 m)
				Run.Room.assign(Y.size(), 1e9f);
				for (size_t K = 0; K < Y.size(); ++K)
				{
					const P2 Q = Run.At[K];
					double Near = 1e9;
					for (int Dz = -1; Dz <= 1; ++Dz)
						for (int Dx = -1; Dx <= 1; ++Dx)
						{
							const auto Hb = Scene->Buildings.find(SceneKey(Q.X + Dx * SceneCell, Q.Z + Dz * SceneCell));
							if (Hb == Scene->Buildings.end()) continue;
							for (int Bi : Hb->second)
							{
								const Building& H = G.Buildings[size_t(Bi)];
								const double C = std::cos(H.Yaw), S = std::sin(H.Yaw), Ex = Q.X - H.X, Ez = Q.Z - H.Z;
								const double U = std::fmax(0.0, std::fabs(Ex * C - Ez * S) - H.W * 0.5), V = std::fmax(0.0, std::fabs(Ex * S + Ez * C) - H.D * 0.5);
								Near = std::fmin(Near, std::hypot(U, V));
							}
						}
					Run.Room[K] = float(std::fmax(RailBedHalf, Near - 1.5));
				}
				{
					// (steady along the line: the least within 12 m, so its walls run straight)
					const std::vector<float> Was = Run.Room;
					for (size_t K = 0; K < Was.size(); ++K)
						for (size_t J = K > 3 ? K - 3 : 0; J <= std::min(Was.size() - 1, K + 3); ++J) Run.Room[K] = std::fmin(Run.Room[K], Was[J]);
				}
				Rails.push_back(std::move(Run));
			}
			From = To + 1;
		}
	}
	// Filed by square: each bit by its middle's 256 m square, and by every RailCell square within 40 m.
	for (size_t L = 0; L < Rails.size(); ++L)
	{
		const RailLine& Run = Rails[L];
		for (size_t I = 0; I + 1 < Run.At.size(); ++I)
		{
			const P2 A = Run.At[I], B = Run.At[I + 1];
			const P2 Mid = (A + B) * 0.5;
			Scene->RailBits[SceneKey(Mid.X, Mid.Z)].emplace_back(int(L), int(I));
			const int X0 = int(std::floor((std::fmin(A.X, B.X) - 40.0) / RailCell)), X1 = int(std::floor((std::fmax(A.X, B.X) + 40.0) / RailCell));
			const int Z0 = int(std::floor((std::fmin(A.Z, B.Z) - 40.0) / RailCell)), Z1 = int(std::floor((std::fmax(A.Z, B.Z) + 40.0) / RailCell));
			for (int Cz = Z0; Cz <= Z1; ++Cz)
				for (int Cx = X0; Cx <= X1; ++Cx)
					Scene->RailNearBy[static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz)].emplace_back(int(L), int(I));
		}
	}
	// The stations' platforms, beside the line nearest each station on the map.
	for (const FeaturePoint& P : G.Scenery.Points)
	{
		const bool Halt = P.Kind == "railway_halt";
		if (!Halt && P.Kind != "railway_station") continue;
		const auto It = Scene->RailNearBy.find(RailNearKey(P.At.X, P.At.Z, RailCell));
		if (It == Scene->RailNearBy.end()) continue;
		double Best = 40.0;
		int BestLine = -1, BestAt = 0;
		for (const std::pair<int, int>& Bit : It->second)
		{
			const RailLine& Run = Rails[size_t(Bit.first)];
			double T = 0.0;
			const double D = ToSegment(P.At, Run.At[size_t(Bit.second)], Run.At[size_t(Bit.second) + 1], T);
			if (D < Best) { Best = D; BestLine = Bit.first; BestAt = Bit.second + (T < 0.5 ? 0 : 1); }
		}
		if (BestLine < 0) continue;
		const RailLine& Run = Rails[size_t(BestLine)];
		if (Run.On[size_t(BestAt)] != 0) continue;
		Platform Made;
		Made.Line = BestLine;
		const int Reach = Halt ? 5 : 10;
		Made.First = BestAt;
		Made.Last = BestAt;
		while (Made.First > 0 && BestAt - Made.First < Reach && Run.On[size_t(Made.First - 1)] == 0) --Made.First;
		while (Made.Last + 1 < int(Run.At.size()) && Made.Last - BestAt < Reach && Run.On[size_t(Made.Last + 1)] == 0) ++Made.Last;
		if (Made.Last - Made.First < 3) continue;
		const size_t A = size_t(std::max(0, BestAt - 1)), B = std::min(Run.At.size() - 1, size_t(BestAt) + 1);
		const P2 Dir = Run.At[B] - Run.At[A];
		const P2 Off = P.At - Run.At[size_t(BestAt)];
		Made.Side = Dir.Z * Off.X - Dir.X * Off.Z >= 0.0 ? 1.0 : -1.0;   // (left of the way along: (Dir.Z, -Dir.X))
		Made.Name = P.Name;
		Platforms.push_back(Made);
	}
}

void RidePlan::RailTile(double X0, double Z0, double Size, bool Fine, RideMesh& Out, RideMesh& Stone) const
{
	if (!Scene || Scene->RailBits.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const P3 Sky = At3(X0, 1e6, Z0);
	static const Rgb Ballast = {0.125f, 0.118f, 0.108f}, Trackbed = {0.07f, 0.075f, 0.04f}, Sleeper = {0.19f, 0.185f, 0.175f}, RailHead = {0.42f, 0.40f, 0.38f},
		RailWeb = {0.10f, 0.065f, 0.04f}, Girder = {0.06f, 0.065f, 0.07f}, Asphalt = {0.085f, 0.085f, 0.09f}, Coping = {0.32f, 0.31f, 0.29f},
		Warning = {0.55f, 0.42f, 0.02f}, Board = {0.008f, 0.04f, 0.22f}, PostGrey = {0.15f, 0.15f, 0.15f};
	// (the viaducts' stone, as the walls' masks: a little lichened, mostly the bare scan)
	Rgb Mask;
	Mask.R = 0.46f;
	Mask.G = 0.7f;
	Mask.B = 0.35f;
	// The way left (unit) across the line at a point, from its neighbours (so bends close).
	const auto LeftAt = [](const RailLine& Run, size_t I)
	{
		const size_t A = I > 0 ? I - 1 : I, B = I + 1 < Run.At.size() ? I + 1 : I;
		const P2 D = Run.At[B] - Run.At[A];
		const double Len = D.Length();
		return Len > 1e-9 ? P2(D.Z / Len, -D.X / Len) : P2(1.0, 0.0);
	};
	// A strip of the line from point I to I + 1, Off0 to Off1 across (+ left), Lift0 at the first
	// offset and Lift1 at the second over its top there.
	const auto Strip = [&](RideMesh& Into, const RailLine& Run, size_t I, double Off0, double Lift0, double Off1, double Lift1, const P3& Toward, const Rgb& C, float Moss)
	{
		const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
		const P2 A = Run.At[I], B = Run.At[I + 1];
		const double Ya = Run.Top[I], Yb = Run.Top[I + 1];
		Quad(Into, At3(A.X + La.X * Off0, Ya + Lift0, A.Z + La.Z * Off0), At3(B.X + Lb.X * Off0, Yb + Lift0, B.Z + Lb.Z * Off0), At3(B.X + Lb.X * Off1, Yb + Lift1, B.Z + Lb.Z * Off1),
			At3(A.X + La.X * Off1, Ya + Lift1, A.Z + La.Z * Off1), Toward, C, false, Moss);
	};
	// A point out to one side of the line at I (for a face to face it).
	const auto Beside = [&](const RailLine& Run, size_t I, double Off, double Lift)
	{
		const P2 L = LeftAt(Run, I);
		return At3(Run.At[I].X + L.X * Off, Run.Top[I] + Lift, Run.At[I].Z + L.Z * Off);
	};
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const long long Key = static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz);
			const auto It = Scene->RailBits.find(Key);
			if (It == Scene->RailBits.end()) continue;
			for (const std::pair<int, int>& Bit : It->second)
			{
				const RailLine& Run = Rails[size_t(Bit.first)];
				const size_t I = size_t(Bit.second);
				const P2 A = Run.At[I], B = Run.At[I + 1];
				if (!Inside((A + B) * 0.5) || A.DistanceTo(B) < 0.05) continue;
				const unsigned char Oa = Run.On[I], Ob = Run.On[I + 1];
				if (Oa == 2 || Ob == 2) continue;   // (in a tunnel)
				const bool Bridge = Oa == 1 || Ob == 1, Crossing = Oa == 3 || Ob == 3;
				const double Ya = Run.Top[I];
				if (Bridge) [&]()
				{
					// The deck, 4.6 m wide and 1.2 m deep, its parapets stone.
					Strip(Out, Run, I, -2.3, 0.0, 2.3, 0.0, Sky, Run.Disused ? Trackbed : Ballast, -1.0f);
					for (double Sg : {1.0, -1.0})
					{
						const P3 Away = Beside(Run, I, Sg * 50.0, 0.0), Inward = Beside(Run, I, -Sg * 50.0, 0.5);
						Strip(Stone, Run, I, Sg * 2.75, -1.2, Sg * 2.75, 1.0, Away, Mask, 0.3f);
						Strip(Stone, Run, I, Sg * 2.3, 0.0, Sg * 2.3, 1.0, Inward, Mask, 0.3f);
						Strip(Stone, Run, I, Sg * 2.3, 1.0, Sg * 2.75, 1.0, Sky, Mask, 0.5f);
					}
					// What holds it up: where it is high, stone piers every 16 m (arches between them on a
					// viaduct), and a stone abutment at each end.
					const double Under = Ground(A.X, A.Z);
					if (std::isnan(Under)) return;
					const double High = Ya - 1.2 - Under;
					size_t First = I;
					while (First > 0 && Run.On[First - 1] == 1) --First;
					const bool Viaduct = High > 9.0;
					const double Span = 4.0 * RailStep, Radius = Span * 0.5 - 1.0, Spring = Ya - 1.2 - 0.7 - Radius;
					const auto ArchAt = [&](double S)
					{
						// (the arch's underside S metres from a pier's middle: the pier's top within a metre of it)
						const double U = S - Span * 0.5;
						return S < 1.0 || S > Span - 1.0 ? Spring : Spring + std::sqrt(std::fmax(0.0, Radius * Radius - U * U));
					};
					const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
					if (Viaduct)
					{
						// (four slices of the arch per bit: each a spandrel face each side down to the arch, and its soffit)
						const double S0 = double((I - First) % 4) * RailStep;
						for (int Slice = 0; Slice < 4; ++Slice)
						{
							const double T0 = Slice / 4.0, T1 = (Slice + 1) / 4.0;
							const double Sa = S0 + T0 * RailStep, Sb = S0 + T1 * RailStep;
							const P2 Pa = A + (B - A) * T0, Pb = A + (B - A) * T1;
							const P2 Lx = La + (Lb - La) * ((T0 + T1) * 0.5);
							const double Da = Ya + (Run.Top[I + 1] - Ya) * T0 - 1.2, Db = Ya + (Run.Top[I + 1] - Ya) * T1 - 1.2;
							for (double Sg : {1.0, -1.0})
							{
								const P3 Away = At3(Pa.X + Lx.X * Sg * 50.0, Da, Pa.Z + Lx.Z * Sg * 50.0);
								Quad(Stone, At3(Pa.X + Lx.X * Sg * 2.75, ArchAt(Sa), Pa.Z + Lx.Z * Sg * 2.75), At3(Pb.X + Lx.X * Sg * 2.75, ArchAt(Sb), Pb.Z + Lx.Z * Sg * 2.75),
									At3(Pb.X + Lx.X * Sg * 2.75, Db, Pb.Z + Lx.Z * Sg * 2.75), At3(Pa.X + Lx.X * Sg * 2.75, Da, Pa.Z + Lx.Z * Sg * 2.75), Away, Mask, false, 0.35f);
							}
							Quad(Stone, At3(Pa.X + Lx.X * 2.75, ArchAt(Sa), Pa.Z + Lx.Z * 2.75), At3(Pb.X + Lx.X * 2.75, ArchAt(Sb), Pb.Z + Lx.Z * 2.75),
								At3(Pb.X - Lx.X * 2.75, ArchAt(Sb), Pb.Z - Lx.Z * 2.75), At3(Pa.X - Lx.X * 2.75, ArchAt(Sa), Pa.Z - Lx.Z * 2.75), At3(Pa.X, -1e6, Pa.Z), Shade(Mask, 0.8), false, 0.5f);
						}
					}
					else if (High > 0.5)
					{
						// (a plain deck's sides and underside)
						for (double Sg : {1.0, -1.0}) Strip(Out, Run, I, Sg * 2.8, -1.25, Sg * 2.8, -0.2, Beside(Run, I, Sg * 50.0, 0.0), Girder, -1.0f);
						Strip(Out, Run, I, -2.8, -1.25, 2.8, -1.25, Beside(Run, I, 0.0, -100.0), Girder, -1.0f);
					}
					// A pier (every 16 m from the bridge's start, not on a road) or an abutment (where it meets the ground).
					const bool Abutment = Oa != 1 || Ob != 1;
					const bool Pier = Oa == 1 && (I - First) % 4 == 0 && High > 2.5;
					if (!Abutment && !Pier) return;
					NearOpts Opts;
					NearHit Hit;
					if (!Abutment && NearestOf(A.X, A.Z, Opts, Hit) && Hit.Dist <= Hit.Half + 1.5) return;
					const P2 Along = (B - A) * (1.0 / A.DistanceTo(B));
					const double Thick = Abutment ? 0.9 : 1.0, Wide = Abutment ? 3.6 : 2.6;
					const double TopY = Abutment ? Ya - 0.3 : Viaduct ? Spring + 0.01 : Ya - 1.2;
					const P2 C = Oa != 1 ? B : A;   // (an abutment where the deck meets the ground)
					const double Foot = Abutment ? Ground(C.X, C.Z) : Under;
					if (std::isnan(Foot)) return;
					const P3 Mid3 = At3(C.X, (Foot + TopY) * 0.5, C.Z);
					const auto Corner = [&](double Su, double Sv, double Y) { return At3(C.X + Along.X * Su * Thick + La.X * Sv * Wide, Y, C.Z + Along.Z * Su * Thick + La.Z * Sv * Wide); };
					const double S4[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
					for (int K = 0; K < 4; ++K)
					{
						const P3 Q0 = Corner(S4[K][0], S4[K][1], Foot - 1.0), Q1 = Corner(S4[K + 1][0], S4[K + 1][1], Foot - 1.0), Q2 = Corner(S4[K + 1][0], S4[K + 1][1], TopY),
							Q3 = Corner(S4[K][0], S4[K][1], TopY);
						const P3 Away = At3(Mid3.X + ((Q0.X + Q2.X) * 0.5 - Mid3.X) * 50.0, Mid3.Y, Mid3.Z + ((Q0.Z + Q2.Z) * 0.5 - Mid3.Z) * 50.0);
						Quad(Stone, Q0, Q1, Q2, Q3, Away, Mask, false, 0.3f);
					}
				}();
				// (beenster, build 147, Whaley Bridge: a line on a bank beside a road hung in the air where the
				// road's own bench took its ground. Where the land beside the line falls away more than its
				// bank's shoulder, a stone retaining wall each side down to it, and a wall across each end.)
				if (!Crossing && !Bridge && Fine)
				{
					// (the wall stands at the bed's shoulder where a road's bench took the land beside the
					// line, or where its bank must stop short of a house)
					const auto WallOff = [&](size_t K, double Sg)
					{
						const P2 L = LeftAt(Run, K), P = Run.At[K];
						const auto Drop = [&](double Off)
						{
							const double G2 = Ground(P.X + L.X * Sg * Off, P.Z + L.Z * Sg * Off);
							return std::isnan(G2) ? 0.0 : G2 - Run.Top[K];
						};
						// (never on a road's tarmac: where the line comes to a bridge over one at a slant)
						const auto Clear = [&](double Off)
						{
							NearOpts Opts;
							Opts.NoDecks = true;
							NearHit Hit;
							return !(NearestOf(P.X + L.X * Sg * Off, P.Z + L.Z * Sg * Off, Opts, Hit) && Hit.Dist <= Hit.Half + 0.5);
						};
						if (Drop(2.6) < -1.9) return Clear(2.6) ? 2.6 : 0.0;
						const double Room = Run.Room.size() == Run.At.size() ? double(Run.Room[K]) : 1e9;
						if (Room < 40.0 && Drop(Room + 0.6) < -2.4 && Clear(Room)) return Room;
						return 0.0;
					};
					const auto FootAt = [&](size_t K, double Sg, double Off)
					{
						const P2 L = LeftAt(Run, K), P = Run.At[K];
						const double Gs = Ground(P.X + L.X * Sg * (Off + 0.6), P.Z + L.Z * Sg * (Off + 0.6));
						return std::isnan(Gs) ? -2.0 : std::fmin(-1.0, Gs - Run.Top[K] - 0.4);
					};
					const double Yb2 = Run.Top[I + 1];
					for (double Sg : {1.0, -1.0})
					{
						double Oa2 = WallOff(I, Sg), Ob2 = WallOff(I + 1, Sg);
						if (Oa2 <= 0.0 && Ob2 <= 0.0) continue;
						if (Oa2 <= 0.0) Oa2 = Ob2;
						if (Ob2 <= 0.0) Ob2 = Oa2;
						Oa2 = Ob2 = std::fmin(Oa2, Ob2);
						const P2 La2 = LeftAt(Run, I), Lb2 = LeftAt(Run, I + 1);
						// (its top: the coping just over the land at its back, the ballast's level by the line)
						const double Ta = Oa2 < 3.0 ? 0.35 : -0.47 - (Oa2 - RailBedHalf) / 1.5 + 0.35, Tb = Ob2 < 3.0 ? 0.35 : -0.47 - (Ob2 - RailBedHalf) / 1.5 + 0.35;
						const double Da = FootAt(I, Sg, Oa2), Db = FootAt(I + 1, Sg, Ob2);
						const P3 Qa0 = At3(A.X + La2.X * Sg * Oa2, Ya + Da, A.Z + La2.Z * Sg * Oa2), Qb0 = At3(B.X + Lb2.X * Sg * Ob2, Yb2 + Db, B.Z + Lb2.Z * Sg * Ob2);
						const P3 Qa1 = At3(A.X + La2.X * Sg * Oa2, Ya + Ta, A.Z + La2.Z * Sg * Oa2), Qb1 = At3(B.X + Lb2.X * Sg * Ob2, Yb2 + Tb, B.Z + Lb2.Z * Sg * Ob2);
						Quad(Stone, Qa0, Qb0, Qb1, Qa1, Beside(Run, I, Sg * 80.0, 0.0), Mask, false, 0.35f);
						const P3 Ra1 = At3(A.X + La2.X * Sg * (Oa2 - 0.35), Ya + Ta, A.Z + La2.Z * Sg * (Oa2 - 0.35)), Rb1 = At3(B.X + Lb2.X * Sg * (Ob2 - 0.35), Yb2 + Tb, B.Z + Lb2.Z * Sg * (Ob2 - 0.35));
						Quad(Stone, Qa1, Qb1, Rb1, Ra1, Sky, Shade(Mask, 1.1), false, 0.5f);
						const P3 Ra0 = At3(A.X + La2.X * Sg * (Oa2 - 0.35), Ya + Ta - 1.0, A.Z + La2.Z * Sg * (Oa2 - 0.35)), Rb0 = At3(B.X + Lb2.X * Sg * (Ob2 - 0.35), Yb2 + Tb - 1.0, B.Z + Lb2.Z * Sg * (Ob2 - 0.35));
						Quad(Stone, Ra0, Rb0, Rb1, Ra1, Beside(Run, I, -Sg * 80.0, 0.0), Mask, false, 0.35f);
					}
					// (the ends: across the line under the ballast, where the walls start or stop)
					for (int End = 0; End < 2; ++End)
					{
						const size_t K = End == 0 ? I : I + 1;
						const double Wl = WallOff(K, 1.0), Wr = WallOff(K, -1.0);
						if (Wl <= 0.0 && Wr <= 0.0) continue;
						const size_t Next = End == 0 ? (I > 0 ? I - 1 : I) : (I + 2 < Run.At.size() ? I + 2 : I + 1);
						if (Next == K || Run.On[Next] != 0 || WallOff(Next, 1.0) > 0.0 || WallOff(Next, -1.0) > 0.0) continue;
						const P2 P = Run.At[K], L = LeftAt(Run, K);
						const double Ol = Wl > 0.0 ? Wl : 2.6, Or = Wr > 0.0 ? Wr : 2.6;
						const double Down = std::fmin(FootAt(K, 1.0, Ol), FootAt(K, -1.0, Or));
						const P2 Other = Run.At[End == 0 ? I + 1 : I];
						const P3 Away50 = At3(P.X + (P.X - Other.X) * 50.0, Run.Top[K], P.Z + (P.Z - Other.Z) * 50.0);
						Quad(Stone, At3(P.X + L.X * Ol, Run.Top[K] + Down, P.Z + L.Z * Ol), At3(P.X - L.X * Or, Run.Top[K] + Down, P.Z - L.Z * Or),
							At3(P.X - L.X * Or, Run.Top[K] + 0.35, P.Z - L.Z * Or), At3(P.X + L.X * Ol, Run.Top[K] + 0.35, P.Z + L.Z * Ol), Away50, Mask, false, 0.35f);
					}
				}
				if (Run.Disused) continue;   // (an old line: its cuttings and banks in the land, no track)
				if (!Crossing && !Bridge)
				{
					// The ballast: its top 3.2 m wide, its shoulders down to the cess.
					Strip(Out, Run, I, -1.6, 0.0, 1.6, 0.0, Sky, Shade(Ballast, 0.9 + 0.2 * Chance(A.X, A.Z, 41)), -1.0f);
					Strip(Out, Run, I, 1.6, 0.0, 2.25, -0.47, Sky, Shade(Ballast, 0.85), -1.0f);
					Strip(Out, Run, I, -2.25, -0.47, -1.6, 0.0, Sky, Shade(Ballast, 0.85), -1.0f);
				}
				if (!Fine) continue;
				const P2 La = LeftAt(Run, I), Lb = LeftAt(Run, I + 1);
				const double Yb = Run.Top[I + 1];
				const double Len = A.DistanceTo(B);
				if (!Crossing)
				{
					// Concrete sleepers every 0.65 m.
					const int Count = std::max(1, int(std::lround(Len / 0.65)));
					const P2 Along = (B - A) * (1.0 / Len);
					for (int K = 0; K < Count; ++K)
					{
						const double T = (K + 0.5) / Count;
						const P2 P = A + (B - A) * T, L = La + (Lb - La) * T;
						const double Y = Ya + (Yb - Ya) * T;
						const P2 F = Along * 0.13;
						const Rgb C = Shade(Sleeper, 0.9 + 0.2 * Chance(P.X, P.Z, 42));
						const auto At2 = [&](double Across, double Fore, double Lift) { return At3(P.X + L.X * Across + F.X * Fore, Y + Lift, P.Z + L.Z * Across + F.Z * Fore); };
						Quad(Out, At2(-1.3, -1.0, 0.12), At2(1.3, -1.0, 0.12), At2(1.3, 1.0, 0.12), At2(-1.3, 1.0, 0.12), Sky, C);
						Quad(Out, At2(-1.3, 1.0, -0.02), At2(1.3, 1.0, -0.02), At2(1.3, 1.0, 0.12), At2(-1.3, 1.0, 0.12), At2(0.0, 30.0, 0.05), Shade(C, 0.8));
						Quad(Out, At2(-1.3, -1.0, -0.02), At2(1.3, -1.0, -0.02), At2(1.3, -1.0, 0.12), At2(-1.3, -1.0, 0.12), At2(0.0, -30.0, 0.05), Shade(C, 0.8));
					}
				}
				// The rails: a head and two sides each.
				for (double Sg : {1.0, -1.0})
				{
					const double Mid = Sg * (RailGauge * 0.5 + 0.035);
					Strip(Out, Run, I, Mid - 0.035, 0.27, Mid + 0.035, 0.27, Sky, RailHead, -1.0f);
					Strip(Out, Run, I, Mid + 0.035, 0.12, Mid + 0.035, 0.27, Beside(Run, I, Mid + 30.0, 0.2), RailWeb, -1.0f);
					Strip(Out, Run, I, Mid - 0.035, 0.12, Mid - 0.035, 0.27, Beside(Run, I, Mid - 30.0, 0.2), RailWeb, -1.0f);
				}
			}
		}
	// The stations: a platform beside the line, its coping and yellow line, and its name board.
	for (const Platform& Plat : Platforms)
	{
		const RailLine& Run = Rails[size_t(Plat.Line)];
		const int Middle = (Plat.First + Plat.Last) / 2;
		if (!Inside(Run.At[size_t(Middle)])) continue;
		const double S = Plat.Side, Edge = 1.6, Back = Edge + 4.0, Lift = 1.18;
		for (int K = Plat.First; K < Plat.Last; ++K)
		{
			const size_t I = size_t(K);
			Strip(Out, Run, I, S * Edge, -0.4, S * Edge, Lift, Beside(Run, I, 0.0, 0.5), Shade(Coping, 0.7), -1.0f);
			Strip(Out, Run, I, S * Edge, Lift, S * (Edge + 0.6), Lift, Sky, Coping, -1.0f);
			Strip(Out, Run, I, S * (Edge + 0.6), Lift, S * Back, Lift, Sky, Asphalt, -1.0f);
			Strip(Out, Run, I, S * (Edge + 0.68), Lift + 0.006, S * (Edge + 0.78), Lift + 0.006, Sky, Warning, -1.0f);
			Strip(Out, Run, I, S * Back, -1.5, S * Back, Lift, Beside(Run, I, S * 50.0, 0.0), Shade(Coping, 0.6), -1.0f);
		}
		// (the platform's two ends)
		for (int End : {Plat.First, Plat.Last})
		{
			const size_t I = size_t(End);
			const P3 Q0 = Beside(Run, I, S * Edge, -0.4), Q1 = Beside(Run, I, S * Back, -1.5), Q2 = Beside(Run, I, S * Back, Lift), Q3 = Beside(Run, I, S * Edge, Lift);
			const size_t Other = End == Plat.First ? I + 1 : I - 1;
			const P3 Out3 = At3(Run.At[I].X * 2.0 - Run.At[Other].X, Run.Top[I] + Lift, Run.At[I].Z * 2.0 - Run.At[Other].Z);
			Quad(Out, Q0, Q1, Q2, Q3, Out3, Shade(Coping, 0.65));
		}
		if (Plat.Name.empty()) continue;
		// The name board: white words on blue, on two posts at the back of the platform, facing the line.
		const size_t I = size_t(Middle);
		const P2 L = LeftAt(Run, I);
		const P2 Face(-L.X * S, -L.Z * S);
		const P2 Along(L.Z, -L.X);   // (along the line, against the way it runs)
		const double Wide = 0.8 + 0.19 * double(Plat.Name.size()), Base = Run.Top[I] + Lift;
		const P2 C = Run.At[I] + L * (S * (Back - 0.6));
		for (double Sg : {1.0, -1.0}) Box(Out, C.X + Along.X * Sg * (Wide * 0.5 - 0.1), C.Z + Along.Z * Sg * (Wide * 0.5 - 0.1), Along, 0.04, 0.04, Base - 0.1, Base + 2.4, PostGrey);
		const P2 Half = Along * (Wide * 0.5);
		const P2 Proud = Face * 0.05;
		const double Lo = Base + 1.75, Hi = Base + 2.35;
		Quad(Out, At3(C.X - Half.X + Proud.X, Lo, C.Z - Half.Z + Proud.Z), At3(C.X + Half.X + Proud.X, Lo, C.Z + Half.Z + Proud.Z), At3(C.X + Half.X + Proud.X, Hi, C.Z + Half.Z + Proud.Z),
			At3(C.X - Half.X + Proud.X, Hi, C.Z - Half.Z + Proud.Z), At3(C.X + Face.X * 50.0, (Lo + Hi) * 0.5, C.Z + Face.Z * 50.0), Board);
		Quad(Out, At3(C.X - Half.X, Lo, C.Z - Half.Z), At3(C.X + Half.X, Lo, C.Z + Half.Z), At3(C.X + Half.X, Hi, C.Z + Half.Z), At3(C.X - Half.X, Hi, C.Z - Half.Z),
			At3(C.X - Face.X * 50.0, (Lo + Hi) * 0.5, C.Z - Face.Z * 50.0), PostGrey);
		// ([OSM stage 6A] lettered as triangles; a text component only for a letter the alphabet lacks)
		static constexpr float Cream[3] = {0.9f, 0.9f, 0.9f};
		if (SignText::Set(Out, C.X + Face.X * 0.06, (Lo + Hi) * 0.5, C.Z + Face.Z * 0.06, Face.X, Face.Z, 0.3 * SignText::CapOfEm, Plat.Name, Cream)) continue;
		RideMesh::Label Words;
		Words.X = float(C.X + Face.X * 0.07 - Out.OriginX);
		Words.Y = float((Lo + Hi) * 0.5);
		Words.Z = float(C.Z + Face.Z * 0.07 - Out.OriginZ);
		Words.Nx = float(Face.X);
		Words.Nz = float(Face.Z);
		Words.Size = 0.3f;
		Words.R = Words.G = Words.B = 0.9f;
		Words.Text = Plat.Name;
		Out.Labels.push_back(Words);
	}
}
// [ride anywhere 4] The furniture of the network's roads (see NetworkFurniture).
std::vector<RidePlan::Furnish> RidePlan::RoadFurniture(int Which) const
{
	std::vector<Furnish> Out;
	const SideRoad* Mine = SideOf(Which);
	if (Mine == nullptr || Mine->Pts.size() < 2 || Mine->Track || !Scene) return Out;
	const std::vector<RoadPoint>& Pts = Mine->Pts;
	const Region& G = Area.Roads;
	if (Mine->Edge >= 0)
	{
		const std::string& Class = G.ClassOf(Mine->Edge);
		if (Class == "track" || Class == "service" || Class == "path" || Class == "footway" || Class == "cycleway" || Class == "bridleway") return Out;
	}
	const auto Dir = [&Pts](size_t I)
	{
		const size_t A = I > 0 ? I - 1 : 0, B = std::min(Pts.size() - 1, I + 1);
		const P2 D = Pts[B].At - Pts[A].At;
		const double L = D.Length();
		return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
	};
	const auto Verge = [&](size_t I, double Side, double Past)
	{
		const P2 D = Dir(I);
		const P2 L(D.Z, -D.X);
		return Pts[I].At + L * (Side * (Pts[I].Half + Past));
	};
	const auto OffAll = [this](const P2& P, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const auto PointAt = [&Pts](double Along)
	{
		size_t I = 0;
		while (I + 1 < Pts.size() && Pts[I + 1].Along <= Along) ++I;
		return I;
	};
	const auto Put = [&Out](Furnish F)
	{
		if (!std::isnan(F.Height)) Out.push_back(std::move(F));
	};
	// Built up as the ride's road is (five buildings within 70 m: town streets are short), gaps under
	// 120 m closed, stretches under 60 m left out.
	std::vector<unsigned char> Busy(Pts.size(), 0);
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		const P2 P = Pts[I].At;
		int Count = 0;
		for (int Dz = -1; Dz <= 1 && Count < 5; ++Dz)
			for (int Dx = -1; Dx <= 1 && Count < 5; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(P.X + Dx * SceneCell, P.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int B : It->second)
					if (std::hypot(G.Buildings[size_t(B)].X - P.X, G.Buildings[size_t(B)].Z - P.Z) < 70.0 && ++Count >= 5) break;
			}
		Busy[I] = Count >= 5 ? 1 : 0;
	}
	for (size_t I = 0; I < Pts.size();)
	{
		if (Busy[I]) { ++I; continue; }
		size_t J = I;
		while (J + 1 < Pts.size() && !Busy[J + 1]) ++J;
		if (I > 0 && J + 1 < Pts.size() && Pts[J].Along - Pts[I].Along < 120.0)
			for (size_t K = I; K <= J; ++K) Busy[K] = 1;
		I = J + 1;
	}
	const double Keep = KeepLeft() ? 1.0 : -1.0;
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		if (!Busy[I] || (I > 0 && Busy[I - 1])) continue;
		size_t J = I;
		while (J + 1 < Pts.size() && Busy[J + 1]) ++J;
		if (Pts[J].Along - Pts[I].Along >= 60.0)
			// Street lamps on its left every 36 m, none within 150 m of one the map has.
			for (double Along = Pts[I].Along + 12.0; Along < Pts[J].Along - 6.0; Along += 36.0)
			{
				const size_t K = PointAt(Along);
				Furnish F;
				F.Kind = StreetLamp;
				F.Mapped = false;
				F.At = Verge(K, Keep, 0.9);
				if (std::any_of(MappedLamps.begin(), MappedLamps.end(), [&F](const P2& M) { return M.DistanceTo(F.At) < 150.0; })) continue;
				const P2 D = Dir(K);
				F.Face = P2(-D.Z, D.X) * Keep;
				F.Height = Ground(F.At.X, F.At.Z);
				if (OffAll(F.At, 0.5)) Put(F);
			}
		I = J;
	}
	// Chevrons round its sharp bends out of town, as the ride's road's.
	if (Mine->Gravel) return Out;
	const double Pi = 3.14159265358979323846;
	std::vector<double> Radius(Pts.size(), 1e9), Turn(Pts.size(), 0.0);
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		size_t A = I, B = I;
		while (A > 0 && Pts[I].Along - Pts[A].Along < 15.0) --A;
		while (B + 1 < Pts.size() && Pts[B].Along - Pts[I].Along < 15.0) ++B;
		if (A == I || B == I) continue;
		const P2 U = Pts[I].At - Pts[A].At, V = Pts[B].At - Pts[I].At;
		double T = std::atan2(V.Z, V.X) - std::atan2(U.Z, U.X);
		while (T > Pi) T -= 2.0 * Pi;
		while (T < -Pi) T += 2.0 * Pi;
		Turn[I] = T;
		if (std::fabs(T) > 1e-4) Radius[I] = (Pts[B].Along - Pts[A].Along) / std::fabs(T);
	}
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		if (Radius[I] >= 40.0 || Busy[I]) continue;
		size_t J = I, Apex = I;
		while (J + 1 < Pts.size() && Radius[J + 1] < 40.0 && (Turn[J + 1] > 0.0) == (Turn[I] > 0.0))
		{
			++J;
			if (Radius[J] < Radius[Apex]) Apex = J;
		}
		double Whole = 0.0;
		for (size_t K = I; K <= J; ++K) Whole += Turn[K] * (K > I ? (Pts[K].Along - Pts[K - 1].Along) : 0.0) / 30.0;
		// (not within 20 m of its ends: a junction's corner is no bend)
		if ((std::fabs(Whole) >= 45.0 * Pi / 180.0 || Radius[Apex] < 20.0) && Pts[Apex].Along > 20.0 && Pts[Apex].Along < Mine->Length - 20.0)
			for (double Off : {-12.0, 0.0, 12.0})
			{
				const size_t K = PointAt(Pts[Apex].Along + Off);
				Furnish F;
				F.Kind = Chevron;
				F.Mapped = false;
				F.At = Verge(K, Turn[Apex] > 0.0 ? 1.0 : -1.0, 1.6);
				F.Face = Dir(K) * -1.0;
				F.Size = Turn[Apex] > 0.0 ? -1.0 : 1.0;
				F.Height = Ground(F.At.X, F.At.Z);
				if (OffAll(F.At, 0.8)) Put(F);
			}
		I = J;
	}
	// [OSM stage 6B] Its bends' and hills' warnings, in Britain, as the ride's road's: where the warning
	// fits on this road before the danger (not within 25 m of its ends: a junction's corner is no bend).
	if (Britain())
	{
		const int Mapped = Mine->Edge >= 0 ? G.Edges[size_t(Mine->Edge)].Limit : 0;
		const std::string Class = Mine->Edge >= 0 ? G.ClassOf(Mine->Edge) : std::string();
		const auto MphAt = [&](double Along)
		{
			if (Mapped > 0) return ShownLimit(Mapped);
			return Busy[PointAt(Along)] ? 30 : Class == "motorway" ? 70 : 60;
		};
		std::vector<Hazard> Ahead;
		ShapeHazards(Pts, Busy, MphAt, Ahead);
		const double Keep2 = KeepLeft() ? 1.0 : -1.0;
		std::vector<std::pair<P2, P2>> Placed;
		for (const Hazard& H : Ahead)
		{
			if (H.Along < 25.0 || H.Along > Mine->Length - 25.0) continue;
			const int Mph = MphAt(H.Along);
			const double Along = H.Along - H.Way * WarnBefore(Mph);
			if (Along < 10.0 || Along > Mine->Length - 10.0) continue;
			const size_t K = PointAt(Along);
			Furnish F;
			F.Kind = WarnSign;
			F.Mapped = false;
			F.Size = H.Symbol;
			F.Text = H.Text;
			F.Big = WarnSide(Mph, Pts[K].Half);
			F.At = Verge(K, H.Way * Keep2, 0.7 + F.Big * 0.5);
			F.Face = Dir(K) * -double(H.Way);
			F.Height = Ground(F.At.X, F.At.Z);
			const P2 Across(F.Face.Z, -F.Face.X);
			if (std::any_of(Placed.begin(), Placed.end(), [&F](const std::pair<P2, P2>& P) { return P.first.DistanceTo(F.At) < 40.0 && P.second.X * F.Face.X + P.second.Z * F.Face.Z > 0.5; })) continue;
			if (!OffAll(F.At, 0.6) || !OffAll(F.At + Across * (F.Big * 0.5), 0.5) || !OffAll(F.At - Across * (F.Big * 0.5), 0.5)) continue;
			Placed.emplace_back(F.At, F.Face);
			Put(F);
		}
	}
	return Out;
}

std::vector<RidePlan::Furnish> RidePlan::BlockFurniture(const Rect& In) const
{
	std::vector<Furnish> Out;
	const Region& G = Area.Roads;
	if (!Scene || MappedLeft.size() != G.Scenery.Points.size()) return Out;
	const bool Uk = Britain();
	for (size_t Pi = 0; Pi < G.Scenery.Points.size(); ++Pi)
	{
		if (!MappedLeft[Pi]) continue;
		const FeaturePoint& P = G.Scenery.Points[Pi];
		if (P.At.X < In.X0 || P.At.X >= In.X1 || P.At.Z < In.Z0 || P.At.Z >= In.Z1) continue;
		// (the kinds that stand by a road; gates and stiles stand in walls drawn already)
		int Kind = -1;
		if (P.Kind == "cattle_grid") Kind = CattleGrid;
		else if (P.Kind == "post_box" && Uk) Kind = PostBox;
		else if (P.Kind == "bus_stop") Kind = BusStop;
		else if (P.Kind == "bench") Kind = Seat;
		else if (P.Kind == "milestone") Kind = Milestone;
		else if (P.Kind == "lift_gate") Kind = LiftGate;
		else if (P.Kind == "bollard") Kind = Bollard;
		else if (P.Kind == "picnic_table") Kind = PicnicTable;
		else if (P.Kind == "information") Kind = InfoBoard;
		else if (P.Kind == "street_lamp") Kind = StreetLamp;
		if (Kind < 0) continue;
		NearOpts Opts;
		NearHit Hit;
		if (!NearestOf(P.At.X, P.At.Z, Opts, Hit)) continue;
		Furnish F;
		F.Kind = Kind;
		F.At = P.At;
		F.Face = P2(std::cos(P.Heading), std::sin(P.Heading));
		if (std::isnan(F.Face.X) || std::isnan(F.Face.Z)) F.Face = P2(1.0, 0.0);
		F.Id = PropHash(P.Id);
		if (Kind == CattleGrid)
		{
			if (Hit.Dist > Hit.Half) continue;
			const std::vector<RoadPoint>& On = RoadOf(Hit.Which);
			const size_t K = size_t(std::max(0, Hit.Index));
			if (K + 1 >= On.size()) continue;
			const P2 D = On[K + 1].At - On[K].At;
			const double L = D.Length();
			if (L < 1e-6) continue;
			F.At = Hit.Foot;
			F.Face = D * (1.0 / L);
			F.Size = Hit.Half * 2.0 + 0.6;
			F.Height = Hit.Height;
			Out.push_back(F);
			continue;
		}
		if (Hit.Dist < Hit.Half + 0.5) continue;
		const double D = Hit.Foot.DistanceTo(F.At);
		if (Kind == StreetLamp)
		{
			if (D > 1e-3) F.Face = (Hit.Foot - F.At) * (1.0 / D);
		}
		else if (Kind != Bollard && D > 1e-3)
		{
			const P2 Away = (F.At - Hit.Foot) * (1.0 / D);
			F.Face = Away * -1.0;
			const double Back = Hit.Half + (Kind == PicnicTable ? 1.6 : Kind == InfoBoard ? 1.0 : 0.6);
			if (D < Back) F.At = Hit.Foot + Away * Back;
		}
		F.Height = Ground(F.At.X, F.At.Z);
		if (!std::isnan(F.Height)) Out.push_back(F);
	}
	// [OSM stage 5D] The map's traffic lights (a pole on the nearside at the stop line, facing the traffic
	// it governs; both ways where the map doesn't say which) and zebra crossings' beacons (one each side).
	// The lights at one junction share a cycle; those on roads across each other run half a cycle apart.
	for (const MappedPoint& Mp : G.RoadPoints)
	{
		const bool Signal = Mp.Kind == "traffic_signals" || Mp.Kind == "crossing_signals", Zebra = Mp.Kind == "crossing_zebra";
		if ((!Signal && !Zebra) || Mp.At.X < In.X0 || Mp.At.X >= In.X1 || Mp.At.Z < In.Z0 || Mp.At.Z >= In.Z1) continue;
		NearOpts Opts;
		NearHit Hit;
		if (!NearestOf(Mp.At.X, Mp.At.Z, Opts, Hit) || Hit.Dist > Hit.Half + 3.0 || Hit.Deck || Hit.Bore) continue;
		const std::vector<RoadPoint>& On = RoadOf(Hit.Which);
		const size_t K = size_t(std::max(0, std::min(Hit.Index, int(On.size()) - 2)));
		if (K + 1 >= On.size()) continue;
		P2 Along = On[K + 1].At - On[K].At;
		const double L = Along.Length();
		if (L < 1e-6) continue;
		Along = Along * (1.0 / L);
		const P2 Left(Along.Z, -Along.X);
		const uint32_t Id = static_cast<uint32_t>(std::lround(Mp.At.X * 7.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Mp.At.Z * 7.0)) * 19349663u;
		if (Zebra)
		{
			for (double Sd : {1.0, -1.0})
			{
				Furnish F;
				F.Kind = Beacon;
				F.At = Hit.Foot + Left * (Sd * (Hit.Half + 0.45));
				F.Face = Left * Sd;
				F.Id = Id + (Sd > 0.0 ? 1u : 2u);
				F.Height = Ground(F.At.X, F.At.Z);
				if (!std::isnan(F.Height)) Out.push_back(F);
			}
			continue;
		}
		// [build 155] beenster: "traffic lights seem to be everywhere and anywhere at major junctions". The
		// map tags a junction's lights on many of its nodes (the middle, each stop line, each carriageway),
		// and each node had a pole both ways. Now the signal nodes within JunctionReach of each other are one
		// junction, laid once (by its first node), and it has one pole per way in: on each road that meets
		// it, each way traffic comes towards it (one-way roads only their own way), at the nearside of its
		// stop line, clear of the roads across, facing the traffic.
		static constexpr double JunctionReach = 45.0;
		std::vector<P2> Nodes;
		bool First = true;
		double Heading = Mp.Heading;
		for (size_t Oi = 0; Oi < G.RoadPoints.size() && First; ++Oi)
		{
			const MappedPoint& O = G.RoadPoints[Oi];
			if (O.Kind != "traffic_signals" && O.Kind != "crossing_signals") continue;
			if (O.At.DistanceTo(Mp.At) > JunctionReach) continue;
			if (&O < &Mp) First = false;   // (an earlier node of the same junction lays it)
			Nodes.push_back(O.At);
			if (&O != &Mp) Heading = -1.0;
		}
		if (!First) continue;
		P2 Centre(0.0, 0.0);
		for (const P2& Q : Nodes) Centre = Centre + Q;
		Centre = Centre * (1.0 / double(Nodes.size()));
		// (the roads that meet there: each one's point nearest the junction's middle)
		struct Meet { int Which; size_t K; };
		std::vector<Meet> Meets;
		double Across = 0.0;
		{
			Rect Round;
			Round.X0 = Centre.X - 60.0; Round.Z0 = Centre.Z - 60.0; Round.X1 = Centre.X + 60.0; Round.Z1 = Centre.Z + 60.0;
			for (const int Wr : RoadsNear(Round, false))
			{
				const std::vector<RoadPoint>& Pts = RoadOf(Wr);
				if (Pts.size() < 2) continue;
				size_t Best = 0;
				double BestD = 1e18;
				for (size_t Pk = 0; Pk < Pts.size(); ++Pk)
				{
					const double Dk = Pts[Pk].At.DistanceTo(Centre);
					if (Dk < BestD) { BestD = Dk; Best = Pk; }
				}
				if (BestD > Pts[Best].Half + 8.0 || Pts[Best].Bridge || Pts[Best].Tunnel) continue;
				Meets.push_back({Wr, Best});
				Across = std::fmax(Across, Pts[Best].Half);
			}
		}
		if (Meets.empty()) continue;
		// (the stop line: back from the middle past the widest road across, and past the map's own stop-line
		// node on that way in where it has one)
		const double Stop = std::clamp(Across + 2.5, 5.0, 25.0);
		const double Start = std::fmod(Chance(std::floor(Centre.X / 40.0), std::floor(Centre.Z / 40.0), 61) * SignalCycle, SignalCycle);
		std::vector<std::pair<P2, P2>> Placed;   // (pole, the way its traffic goes)
		for (const Meet& Mt : Meets)
		{
			const std::vector<RoadPoint>& Pts = RoadOf(Mt.Which);
			for (const int Sd : {1, -1})
			{
				// (one-way: its points run the way its traffic goes, so only the way in from before the junction)
				if (Pts[Mt.K].OneWay && Sd > 0) continue;
				size_t Pk = Mt.K;
				bool Reached = false;
				while (true)
				{
					if (Sd > 0 ? Pk + 1 >= Pts.size() : Pk == 0) break;
					Pk = Sd > 0 ? Pk + 1 : Pk - 1;
					if (Pts[Pk].At.DistanceTo(Centre) >= Stop) { Reached = true; break; }
				}
				if (!Reached) continue;
				// (the way the traffic goes here: towards the junction, back along Sd)
				const size_t Pn = Sd > 0 ? Pk - 1 : Pk + 1;
				P2 Goes = Pts[Pn].At - Pts[Pk].At;
				const double Gl = Goes.Length();
				if (Gl < 1e-6) continue;
				Goes = Goes * (1.0 / Gl);
				if (Heading >= 0.0)
				{
					const double H = Heading * 3.14159265358979 / 180.0;
					if (std::sin(H) * Goes.X - std::cos(H) * Goes.Z < 0.0) continue;   // (the map's one way only)
				}
				const P2 Near = Pts[Pk].OneWay && Pts[Pk].Near != 0
					? (Pts[Pk].Near > 0 ? P2(Goes.Z, -Goes.X) : P2(-Goes.Z, Goes.X))
					: (KeepLeft() ? P2(Goes.Z, -Goes.X) : P2(-Goes.Z, Goes.X));
				Furnish F;
				F.Kind = TrafficSignal;
				F.At = Pts[Pk].At + Near * (Pts[Pk].Half + 0.55);
				bool Twin = false;
				for (const std::pair<P2, P2>& Pl : Placed) Twin = Twin || (Pl.first.DistanceTo(F.At) < 6.0 && Pl.second.X * Goes.X + Pl.second.Z * Goes.Z > 0.8);
				if (Twin) continue;
				{
					// (not standing in another road's carriageway)
					NearOpts Clear;
					NearHit Over;
					if (NearestOf(F.At.X, F.At.Z, Clear, Over) && Over.Dist < Over.Half + 0.3 && Over.Which != Mt.Which) continue;
				}
				F.Face = Goes * -1.0;
				F.Size = std::fmod(Start + (std::fabs(Goes.X) > std::fabs(Goes.Z) ? SignalCycle * 0.5 : 0.0), SignalCycle);
				F.Id = Id + 3u + uint32_t(Placed.size());
				F.Height = Ground(F.At.X, F.At.Z);
				if (std::isnan(F.Height)) continue;
				Placed.push_back({F.At, Goes});
				Out.push_back(F);
			}
		}
	}
	return Out;
}
} // namespace TorqueWorld
