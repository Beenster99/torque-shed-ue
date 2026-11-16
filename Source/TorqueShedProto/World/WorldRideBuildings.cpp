// The buildings on a square (OSM stage 3; lifted out of SceneryTile as they were): each on its mapped
// footprint, its walls in the local stone, render, brick or the map's own material, under the roof
// the map or the local look gives it, windows a floor and a door near, a church's tower.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace TorqueWorld
{
namespace RideBuildingBits
{
// The height to the eaves of a building the map gives no height for. Most British houses are two
// storeys (eaves about 5.3 m); a few are bungalows; a town's bigger houses three storeys. A small
// footprint is an outbuilding of one storey; shops have a flat above; blocks three to five storeys.
// (Build 141: the old guess took the roof out of 4-7 m, which left most houses one storey.)
double GuessEaves(int Baked, bool Big, double HalfLong, double HalfShort, double X, double Z, bool InTown)
{
	const double Area = HalfLong * HalfShort * 4.0;
	const double Roll = RideSceneBits::Chance(X, Z, 61), Jig = RideSceneBits::Chance(X, Z, 62) - 0.5;
	if (Big) return 5.5 + 2.0 * Jig;
	switch (Baked)
	{
	case 1: return 2.5 + 0.4 * Jig;                                        // shed
	case 3: return (Roll < 0.2 && InTown ? 8.4 : 5.9) + 0.5 * Jig;         // shop, a flat above
	case 4: return 2.9 * (3 + int(Roll * 3.0)) + 0.3 + 0.4 * Jig;          // flats
	case 6: return 6.0 + 2.0 * Jig;                                        // worship
	case 7: return (Roll < 0.4 ? 8.2 : 6.0) + 0.6 * Jig;                   // civic
	default: break;
	}
	if (Area < 28.0) return 2.5 + 0.4 * Jig;                               // an outbuilding
	if (Area > 55.0 && InTown && Roll > 0.86) return 8.0 + 0.5 * Jig;      // a town's tall house
	if (Roll < (InTown ? 0.06 : 0.14)) return 2.7 + 0.3 * Jig;             // a bungalow
	return 5.6 + 0.4 * Jig;                                                // two storeys
}
// [fps plan step 2] The facade kit's sizes (Tools/make_facade_kit.py's tables, which these must match):
// a window's model by its type (0 sash, 1 casement, 2 mullion) and opening (width, height), -1 none; a
// door's by its width and height (wide: a pair of leaves).
constexpr double WindowSizes[3][3][2] = {{{0.9, 1.45}, {0.72, 1.45}, {1.0, 2.0}}, {{0.85, 1.0}, {0.68, 1.0}, {1.0, 2.0}}, {{1.3, 0.9}, {1.04, 0.9}, {1.0, 2.0}}};
constexpr double DoorSizes[2][2] = {{0.95, 2.05}, {0.9, 2.05}}, WideDoorSizes[2][2] = {{1.6, 2.05}, {1.6, 2.6}};
int FacadeSize(const double (*Sizes)[2], int Count, double W, double H)
{
	for (int I = 0; I < Count; ++I)
		if (std::fabs(Sizes[I][0] - W) < 0.03 && std::fabs(Sizes[I][1] - H) < 0.03) return I;
	return -1;
}
} // namespace RideBuildingBits

void RidePlan::BuildingsTile(double X0, double Z0, double Size, bool Fine, const LocalLook& Look, RideMesh& Out, RideMesh& Washed, RideMesh& Houses, RideMesh& RoofOut, RideMesh& BrickOut, BuildingTally* Tally) const
{
	using namespace RideSceneBits;
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	const std::shared_ptr<const HouseTypes> Types = HouseTypesNow();   // [towns stage 4]
	const bool Scots = std::strcmp(Look.Name, "lowlands") == 0 || std::strcmp(Look.Name, "highlands") == 0;
	const auto Inside = [X0, Z0, Size](double X, double Z) { return X >= X0 && X < X0 + Size && Z >= Z0 && Z < Z0 + Size; };
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const P3 Sky = At3(X0, 1e6, Z0);
	const auto RailClear = [this](double X, double Z)
	{
		double Dist = 0.0, Top = 0.0;
		int On = 0;
		return NearRail(X, Z, Dist, Top, On) && On != 2 ? Dist : 1e9;
	};
	// [OSM stage 3] Among other buildings: eight or more within 50 m (a village's or a town's street).
	const auto Busy = [&](const Building& B)
	{
		int Count = 0;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Scene->Buildings.find(SceneKey(B.X + Dx * SceneCell, B.Z + Dz * SceneCell));
				if (It == Scene->Buildings.end()) continue;
				for (int I : It->second)
				{
					const Building& O = G.Buildings[size_t(I)];
					if ((O.X - B.X) * (O.X - B.X) + (O.Z - B.Z) * (O.Z - B.Z) < 2500.0 && ++Count >= 9) return true;
				}
			}
		return false;
	};
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));

	// Buildings: each on its mapped footprint, stone under a pitched roof, its ridge along its length.
	static const Rgb Stone = {0.30f, 0.28f, 0.245f}, Slate = {0.085f, 0.085f, 0.095f}, StoneSlate = {0.17f, 0.155f, 0.13f}, Glass = {0.02f, 0.025f, 0.03f},
		DoorWood = {0.07f, 0.045f, 0.03f};
	// [towns stage 3] Where the world has every building's outline, those (cut into boxes) in place of
	// the cell's own boxes; elsewhere the cell's boxes as before.
	struct Candidate
	{
		const Building* B = nullptr;
		const BuildingLook* Look = nullptr;
		int Index = -1;
		double Floors = 0.0;
		const OutlinePiece* Piece = nullptr;   // [towns stage 4] (its row, where it came from an outline)
	};
	std::vector<Candidate> Here;
	std::vector<std::shared_ptr<const std::vector<OutlinePiece>>> Held;
	std::vector<const OutlinePiece*> Pieces;
	std::vector<CellId> Covered;
	OutlinesIn(X0, Z0, Size, Held, Pieces, Covered);
	for (const OutlinePiece* P : Pieces) Here.push_back({&P->B, &P->Look, -1, P->Floors, P});
	const Frame Fr = G.FrameOf();
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Buildings.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Buildings.end()) continue;
			for (int I : It->second)
			{
				const Building& Bi = G.Buildings[size_t(I)];
				if (!Covered.empty())
				{
					const Geo At = Fr.ToGeo(Bi.X, Bi.Z);
					if (std::find(Covered.begin(), Covered.end(), CellOf(At.Lat, At.Lon)) != Covered.end()) continue;
				}
				Here.push_back({&Bi, nullptr, I, 0.0, nullptr});
			}
		}
	if (Tally) Tally->Outlined += int(Pieces.size());
	for (const Candidate& Each : Here)
	{
		const Building& B = *Each.B;
		const int Index = Each.Index;
		if (!Inside(B.X, B.Z) || B.W < 2.0 || B.D < 2.0 || !OffRoad(B.X, B.Z, std::fmin(B.W, B.D) * 0.5) || RailClear(B.X, B.Z) < std::fmin(B.W, B.D) * 0.5 + 2.0) continue;
		// [towns stage 2] (a large or tall building is drawn at its true outline: WorldRideTowers.cpp)
		if (UnderTower(B.X, B.Z))
		{
			if (Tally) ++Tally->UnderTowers;
			continue;
		}
		const double Base = Ground(B.X, B.Z);
		if (std::isnan(Base)) continue;
		// (local to the building: x across, z deep; the turn is the one the land's levelled pad uses)
		const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
		bool LongX = B.W >= B.D;
		// [build 146] A house by a road fronts it: its ridge runs along the road and its door is on the
		// side towards it, as a terraced or semi-detached house is built, even where it is deeper than
		// wide (beenster, build 145: "many not facing the road"). Long rows keep their length.
		// [towns stage 5] And a house by a road takes its street's look: the street was built at one go, its
		// walls, roofs and roof shapes alike, most of its houses still as built (the seed: the road and
		// which 120 m of it), a few done up their own way. Away from a road each house is its own.
		double StreetX = B.X, StreetZ = B.Z;
		{
			const int Kind0 = B.More.empty() ? 0 : int(B.More[0]);
			NearOpts Opts;
			NearHit H;
			const bool ByRoad = Kind0 == 0 && NearestOf(B.X, B.Z, Opts, H) && H.Dist - H.Half < 40.0;
			if (ByRoad && Chance(B.X, B.Z, 15) >= 0.18)
			{
				StreetX = 13.0 * double(H.Which) + 0.5;   // (small: Chance takes a coordinate)
				StreetZ = 17.0 * std::floor(H.Along / 120.0) + 0.5;
			}
			if (ByRoad && std::fmax(B.W, B.D) < 2.2 * std::fmin(B.W, B.D) && std::fmin(B.W, B.D) <= 12.0 && H.Dist - H.Half < 30.0)
			{
				const double Tx = H.Foot.X - B.X, Tz = H.Foot.Z - B.Z, Tl = std::hypot(Tx, Tz);
				if (Tl > 0.1) LongX = std::fabs(Tx * Cs - Tz * Sn) / Tl < 0.707;   // (x along the road unless the road lies off an x end)
			}
		}
		// [towns stage 4] A house in a row (MarkRows: semis, a terrace) runs along the row, its ridge through
		// its party walls, and is built as the rest of its row is (a few done up their own way).
		const OutlinePiece* InRow = Each.Piece != nullptr && Each.Piece->RowSize >= 2 && Each.Piece->Party != 0 ? Each.Piece : nullptr;
		if (InRow)
		{
			const bool PartyX = (InRow->Party & 3) != 0, PartyZ = (InRow->Party & 12) != 0;
			if (PartyX != PartyZ) LongX = PartyX;
			if (Chance(B.X, B.Z, 15) >= 0.1) { StreetX = InRow->RowSeed.X * 0.37 + 0.5; StreetZ = InRow->RowSeed.Z * 0.41 + 0.5; }
		}
		// (which of its ends, along, are party walls: none over-sail a neighbour, and the stacks stand on them)
		const bool PartyPlus = InRow && (LongX ? (InRow->Party & 1) != 0 : (InRow->Party & 4) != 0);
		const bool PartyMinus = InRow && (LongX ? (InRow->Party & 2) != 0 : (InRow->Party & 8) != 0);
		const bool StackPlus = PartyPlus && (LongX ? (InRow->Stacks & 1) != 0 : (InRow->Stacks & 4) != 0);
		const bool StackMinus = PartyMinus && (LongX ? (InRow->Stacks & 2) != 0 : (InRow->Stacks & 8) != 0);
		const double HalfLong = (LongX ? B.W : B.D) * 0.5, HalfShort = (LongX ? B.D : B.W) * 0.5;
		const auto Local = [&](double Along, double Across, double Y)
		{
			const double Lx = LongX ? Along : Across, Lz = LongX ? Across : Along;
			return At3(B.X + Lx * Cs + Lz * Sn, Base + Y, B.Z - Lx * Sn + Lz * Cs);
		};
		// What the map says it looks like, where it says: the tags of the point that lies inside its
		// footprint, nearest its middle. [OpenStreetMap: building, roof:shape, roof:colour,
		// roof:material, building:material, building:colour]
		const BuildingLook* Tags = Each.Look;
		if (Tags == nullptr)
		{
			double Best = 1e9;
			const long long Key0 = SceneKey(B.X, B.Z);
			for (long long Dk : {0LL, -1LL, 1LL, -4000037LL, 4000037LL})
			{
				const auto Lk = Scene->Looks.find(Key0 + Dk);
				if (Lk == Scene->Looks.end()) continue;
				for (int Li : Lk->second)
				{
					const BuildingLook& Lo = G.Scenery.Buildings[size_t(Li)];
					const double Dx = Lo.At.X - B.X, Dz = Lo.At.Z - B.Z;
					const double Ax = Dx * Cs - Dz * Sn, Az = Dx * Sn + Dz * Cs;   // (into its own frame)
					if (std::fabs(Ax) > B.W * 0.5 + 0.5 || std::fabs(Az) > B.D * 0.5 + 0.5) continue;
					const double Dist = std::hypot(Dx, Dz);
					if (Dist < Best) { Best = Dist; Tags = &Lo; }
				}
			}
		}
		static const std::string NoWord;
		const std::string& Kind = Tags ? Tags->Words[0] : NoWord;
		const std::string& RoofShape = Tags ? Tags->Words[1] : NoWord;
		const std::string& RoofMaterial = Tags ? Tags->Words[3] : NoWord;
		const std::string& WallMaterial = Tags ? Tags->Words[4] : NoWord;
		Rgb TaggedRoof, TaggedWall;
		const bool HasRoofColour = Tags && BuildingTagColour(Tags->Words[2], TaggedRoof);
		const bool HasWallColour = Tags && BuildingTagColour(Tags->Words[5], TaggedWall);
		// [OSM stage 3] What the baker says it is (Region::Building's More[0]): a house, shed, barn,
		// shop, block of flats, industrial unit, place of worship or civic building; and the map's
		// own small kinds where it has them (a ruin, a garage).
		const int Baked = B.More.empty() ? 0 : std::clamp(int(B.More[0]), 0, 7);
		const bool IsHouse = Baked == 0, IsShed = Baked == 1, IsBarn = Baked == 2, IsShop = Baked == 3, IsBlock = Baked == 4, IsIndustrial = Baked == 5,
			IsWorship = Baked == 6, IsCivic = Baked == 7;
		const bool Ruin = Kind == "ruins" || Kind == "ruin";
		const bool Garage = Kind == "garage" || Kind == "garages" || Kind == "carport";
		const bool GlassHouse = Kind == "greenhouse" || WallMaterial == "glass";
		const bool Timber = Kind == "hut" || Kind == "cabin" || WallMaterial == "wood" || WallMaterial == "timber_framing" || (IsShed && WallMaterial.empty() && Chance(B.X, B.Z, 8) < 0.55);
		const bool Works = Kind == "hangar" || Kind == "stadium" || Kind == "grandstand" || Kind == "industrial" || Kind == "warehouse" || Kind == "retail"
			|| Kind == "commercial" || WallMaterial == "metal" || WallMaterial == "concrete" || WallMaterial == "cement_block" || (WallMaterial.empty() && (IsIndustrial || (IsShed && !Timber)));
		const bool Big = HalfShort > 6.0 || Works || IsBarn;   // (a barn or a shed: a low roof)
		// [towns stage 4] What kind of house it is and when it was built (Data/RideUK/house_types.json): a
		// cottage or one of a village row out of town; in a town a detached house, a semi, one of a terrace,
		// or a tenement (Scotland's flats, three floors and up). A row takes one period, a street mostly one.
		const bool InTown = TownLookAt(B.X, B.Z) != nullptr;
		const bool LongRow = HalfLong * 2.0 >= 12.0 && HalfLong > HalfShort * 2.5;
		int HouseKind = -1;
		if (IsHouse && !Big)
			HouseKind = InRow ? (!InTown ? HouseTypes::VillageRow : InRow->RowSize >= 3 ? HouseTypes::Terrace : HouseTypes::Semi)
				: LongRow ? (InTown ? HouseTypes::Terrace : HouseTypes::VillageRow) : InTown ? HouseTypes::Detached : HouseTypes::Cottage;
		if (InTown && Scots && (IsBlock || (IsHouse && Each.Floors >= 3.0))) HouseKind = HouseTypes::Tenement;
		const double RowX = InRow ? InRow->RowSeed.X * 0.37 + 0.5 : StreetX, RowZ = InRow ? InRow->RowSeed.Z * 0.41 + 0.5 : StreetZ;
		const bool OwnWay = HouseKind == HouseTypes::Detached || HouseKind == HouseTypes::Cottage;   // (a detached house more its own than a row's)
		const HousePeriod* Period = HouseKind >= 0 && Types ? Types->Pick(HouseKind, OwnWay && Chance(B.X, B.Z, 85) < 0.35 ? Chance(B.X, B.Z, 81) : Chance(RowX, RowZ, 81)) : nullptr;
		const bool PlainGlazing = Period && Period->Window == 3;   // (modern: casements, no bars)
		const LocalLook::EWindow WinKind = Period && Period->Window >= 0 ? (PlainGlazing ? LocalLook::Casement : LocalLook::EWindow(Period->Window)) : Look.Window;
		if (Tally && HouseKind >= 0) ++Tally->HouseKinds[HouseKind];
		// The roof's shape: the map's, else gabled; a flat-roofed block where the map has tar paper
		// or concrete on top.
		enum class ERoof { Gabled, Hipped, Flat, Skillion };
		ERoof Shape = ERoof::Gabled;
		if (RoofShape == "hipped" || RoofShape == "half_hipped" || RoofShape == "pyramidal" || RoofShape == "round" || RoofShape == "dome" || RoofShape == "mansard") Shape = ERoof::Hipped;
		else if (RoofShape == "flat") Shape = ERoof::Flat;
		else if (RoofShape == "skillion" || RoofShape == "lean_to") Shape = ERoof::Skillion;
		else if (RoofShape.empty() && (RoofMaterial == "tar_paper" || RoofMaterial == "concrete")) Shape = ERoof::Flat;
		// [OSM stage 3] (by kind where the map says nothing: a block of flats mostly flat-topped, a civic building hipped, a garage flat)
		else if (RoofShape.empty() && ((IsBlock && Chance(B.X, B.Z, 10) < 0.6) || Garage)) Shape = ERoof::Flat;
		else if (RoofShape.empty() && IsCivic) Shape = ERoof::Hipped;
		// [towns stage 3.4] Houses the map says nothing of, by their outline: a squarish detached or
		// semi-detached house in a town of brick often hipped (the inter-war semi), less so where stone
		// is the rule; a small piece of a house's outline (its back kitchen, an outshut) a lean-to.
		else if (RoofShape.empty() && IsHouse && !Big)
		{
			const double Area0 = HalfLong * HalfShort * 4.0;
			// ([towns stage 4] as its period hipped them, where the period says: fewer where stone is the rule)
			const double HipShare = Period && Period->Hipped >= 0.0 ? Period->Hipped * (InTown ? 1.0 : 0.4) * (0.35 + 0.65 * std::fmin(1.0, double(Look.BrickShare + Look.RenderShare)))
				: InTown ? 0.12 + 0.45 * double(Look.BrickShare) : 0.06 + 0.2 * double(Look.BrickShare);
			if (Each.Look != nullptr && Area0 < 22.0 && Chance(B.X, B.Z, 13) < 0.6) Shape = ERoof::Skillion;
			else if (Area0 > 45.0 && Area0 < 170.0 && HalfLong < 1.7 * HalfShort && Chance(RowX, RowZ, 14) < HipShare)
				Shape = ERoof::Hipped;
		}
		// [towns stage 4] (a terrace's roofs run on from house to house: gabled through every party wall;
		// a pair of semis may be hipped, but only at their outer ends)
		if (InRow && InRow->RowSize >= 3 && Shape == ERoof::Hipped) Shape = ERoof::Gabled;
		const double Rise = Shape == ERoof::Flat ? 0.0 : Shape == ERoof::Skillion ? std::fmin(HalfShort * 0.3, 1.6)
			: Big ? HalfShort * 0.35 : IsWorship ? std::fmin(HalfShort * 1.15, 6.5) : std::fmin(HalfShort * std::tan(double(Look.Pitch) * 0.0174533), 4.5);
		// (the map's floors where it has them; its height, except a house's under 7 m: Overture's British
		// heights put most two-storey houses at 4-6 m to the ridge, which drew them as bungalows in build 145)
		const double MapFloors = Each.Floors;
		const bool TrustHeight = B.Height > 2.0 && !(IsHouse && B.Height >= 3.0 && B.Height < 7.0);
		const double Eaves = MapFloors >= 1.0 ? std::fmin(MapFloors, 40.0) * 2.7 + 0.6
			: TrustHeight ? std::fmax(2.3, B.Height - Rise)
			: RideBuildingBits::GuessEaves(Baked, Big, HalfLong, HalfShort, B.X, B.Z, TownLookAt(B.X, B.Z) != nullptr);
		const double Pick = IsHouse ? Chance(StreetX, StreetZ, 2) : Chance(B.X, B.Z, 2);
		// (the walling: the map's material, else the local share of render, brick, else stone; a barn stays stone, or brick where brick is the rule)
		const bool TaggedRender = WallMaterial == "plaster" || WallMaterial == "render" || WallMaterial == "stucco";
		const bool TaggedBrick = WallMaterial == "brick", TaggedStone = WallMaterial == "stone" || WallMaterial == "sandstone" || WallMaterial == "limestone";
		const bool Plain = GlassHouse || Timber || Works;   // (drawn in the map's or the material's own colour, no scan)
		const bool Chosen = Plain || TaggedRender || TaggedBrick || TaggedStone;
		// ([OSM stage 3] a barn, a church or a chapel never rendered; a block of flats rendered or brick)
		const bool Rendered = !Plain && !IsBarn && !IsWorship && (TaggedRender || (!Chosen && ((!Big && Pick < Look.RenderShare) || (IsBlock && Pick < 0.45))));
		const bool Brick = !Plain && !Rendered && (TaggedBrick || (!Chosen && (IsBlock || (Big ? Pick < Look.BrickShare * 0.8 : Pick < Look.RenderShare + Look.BrickShare))));
		const Rgb Washes = HasWallColour ? TaggedWall : Rgb{Look.RenderColour.R, Look.RenderColour.G, Look.RenderColour.B};
		const Rgb Bricks = HasWallColour ? TaggedWall : Rgb{Look.BrickColour.R, Look.BrickColour.G, Look.BrickColour.B};
		static const Rgb GlassWall = {0.10f, 0.12f, 0.12f}, Planks = {0.16f, 0.11f, 0.07f}, Sheeting = {0.30f, 0.31f, 0.30f};
		const Rgb PlainWall = HasWallColour && !GlassHouse ? TaggedWall : GlassHouse ? GlassWall : Timber ? Planks : Sheeting;
		const Rgb Wall = Plain ? Shade(PlainWall, 0.92 + 0.12 * Chance(B.X, B.Z, 4))
			: Shade(Rendered ? Washes : Brick ? Bricks : Stone, (Rendered ? 0.92 : 0.85) + (Rendered ? 0.14 : 0.3) * (0.75 * Chance(StreetX, StreetZ, 4) + 0.25 * Chance(B.X, B.Z, 4)));
		// (a stone house's walls in the scanned masonry: brightness, mostly the bare scan, a little lichen)
		Rgb Mask;
		Mask.R = float(0.48 + 0.14 * Chance(B.X, B.Z, 4));
		Mask.G = 0.8f;
		Mask.B = 0.3f;
		const auto WallQuad = [&](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, double By)
		{
			if (Rendered) Quad(Washed, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By), false, 0.08f);
			else if (Brick) Quad(BrickOut, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By), false, 0.06f);
			else if (Plain) Quad(Out, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By));
			else Quad(Houses, Q0, Q1, Q2, Q3, Toward, Mask, false, 0.06f);
		};
		// (the roof: the map's material, else the local share of thatch, clay tiles or pantiles, stone slates, else slate)
		const double RoofPick = IsHouse ? Chance(StreetX, StreetZ, 6) : Chance(B.X, B.Z, 6);
		const bool AnyRoofTag = !RoofMaterial.empty();
		// ([OSM stage 3] a church, a chapel or a civic building in slate; a barn in stone slate or sheet; a timber shed in felt)
		const bool Formal = !AnyRoofTag && (IsWorship || IsCivic);
		const bool Thatch = RoofMaterial == "thatch" || (!AnyRoofTag && !Big && !Plain && !Formal && RoofPick < Look.ThatchShare);
		const bool Tiled = !Thatch && (RoofMaterial == "roof_tiles" || RoofMaterial == "tiles" || (!AnyRoofTag && !Formal && RoofPick < Look.ThatchShare + Look.TileShare));
		const bool Flags = !Thatch && !Tiled && (RoofMaterial == "stone" || (!AnyRoofTag && !Plain && !Formal && RoofPick < Look.ThatchShare + Look.TileShare + Look.StoneSlateShare));
		const bool Sheet = RoofMaterial == "metal" || RoofMaterial == "eternit" || RoofMaterial == "copper" || (!AnyRoofTag && (Works || (IsBarn && Chance(B.X, B.Z, 9) < 0.3)));
		const bool Felt = RoofMaterial == "tar_paper" || RoofMaterial == "concrete" || (!AnyRoofTag && (Shape == ERoof::Flat || (Timber && IsShed)));
		const bool GlassRoof = RoofMaterial == "glass" || (!AnyRoofTag && GlassHouse);
		static const Rgb Straw = {0.27f, 0.21f, 0.11f}, RoofSheet = {0.20f, 0.21f, 0.21f}, RoofFelt = {0.06f, 0.06f, 0.065f}, Verdigris = {0.18f, 0.32f, 0.27f};
		const Rgb Tiles = {Look.TileColour.R, Look.TileColour.G, Look.TileColour.B};
		const Rgb RoofBase = HasRoofColour ? TaggedRoof : GlassRoof ? Glass : RoofMaterial == "copper" ? Verdigris : Sheet ? RoofSheet : Felt ? RoofFelt
			: Thatch ? Straw : Tiled ? Tiles : Flags ? StoneSlate : Slate;
		const Rgb Roof = Shade(RoofBase, 0.9 + 0.2 * (0.7 * Chance(StreetX, StreetZ, 5) + 0.3 * Chance(B.X, B.Z, 5)));
		// ([OSM stage 3] its roofing for M_RideRoof's courses: glass stays plain)
		const float Roofing = HasRoofColour && !Tiled && !Thatch && !Flags && !Sheet && !Felt ? 0.0f : Sheet || Felt ? 1.0f : Thatch ? 0.75f : Tiled ? 0.5f : Flags ? 0.25f : 0.0f;
		const auto RoofFace = [&](const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, const Rgb& C)
		{
			if (GlassRoof) Quad(Out, Q0, Q1, Q2, Q3, Toward, C);
			else RoofQuad(RoofOut, Q0, Q1, Q2, Q3, Toward, C, Roofing);
		};
		const P3 Centre = Local(0.0, 0.0, Eaves * 0.5);
		// (none of it over a road: the map's footprints and the road's own line don't always agree)
		bool Clear = true;
		for (double Su : {-1.0, 0.0, 1.0})
			for (double Sv : {-1.0, 1.0})
			{
				const P3 Corner = Local(Su * HalfLong, Sv * HalfShort, 0.0);
				Clear = Clear && OffRoad(Corner.X, Corner.Z, 0.6);
			}
		if (!Clear) continue;
		const auto Outside = [&Centre](const P3& A, const P3& C)
		{
			// (a point well outside the face whose opposite corners are A and C)
			return At3(Centre.X + ((A.X + C.X) * 0.5 - Centre.X) * 50.0, Centre.Y, Centre.Z + ((A.Z + C.Z) * 0.5 - Centre.Z) * 50.0);
		};
		// (its walls go down to below the lowest ground at its corners, so none stands with a corner
		// in the air on a slope: beenster, build 141; a metre under where the ground is level)
		double Sunk = -1.0;
		for (double Su : {-1.0, 1.0})
			for (double Sv : {-1.0, 1.0})
			{
				const P3 Corner = Local(Su * (HalfLong + 0.3), Sv * (HalfShort + 0.3), 0.0);
				const double Gc = Ground(Corner.X, Corner.Z);
				if (!std::isnan(Gc)) Sunk = std::fmin(Sunk, Gc - Base - 0.5);
			}
		Sunk = std::fmax(Sunk, -8.0);
		const double Over = Shape == ERoof::Flat ? 0.0 : Thatch ? 0.55 : double(Look.Overhang), Drop = Rise > 0.0 ? Over * Rise / HalfShort : 0.0;
		const double OverPlus = PartyPlus ? 0.0 : Over, OverMinus = PartyMinus ? 0.0 : Over;   // [towns stage 4] (none over a neighbour)
		// (a hipped roof's ridge: shorter than the walls by the roof's depth each end, a point if it is square)
		const double Ridge = std::fmax(0.0, HalfLong - HalfShort);
		// [OSM stage 3] Its door on the long side nearer a road (the + side if neither is near).
		double DoorSide = 1.0;
		{
			NearOpts Opts;
			NearHit Ha, Hb;
			const P3 Pa = Local(0.0, HalfShort + 1.0, 0.0), Pb = Local(0.0, -HalfShort - 1.0, 0.0);
			const double Da = NearestOf(Pa.X, Pa.Z, Opts, Ha) ? Ha.Dist - Ha.Half : 1e9, Db = NearestOf(Pb.X, Pb.Z, Opts, Hb) ? Hb.Dist - Hb.Half : 1e9;
			if (Db < Da) DoorSide = -1.0;
			if (Tally && std::fmin(Da, Db) < 20.0) ++Tally->NearRoad;
		}
		// [OSM stage 3] A terrace: a house 12 m long or more and over two and a half times as long as
		// it is deep, among other buildings (a village or a town), is a row of houses, a bay each.
		int Bays = 1;
		if (IsHouse && !Plain && HalfLong * 2.0 >= 12.0 && HalfLong > HalfShort * 2.5 && Busy(B))
			Bays = std::max(2, int(std::lround(HalfLong * 2.0 / 5.2)));
		if (Tally)
		{
			++Tally->Drawn;
			if (Bays > 1) { ++Tally->Terraces; Tally->TerraceHouses += Bays; }
			Tally->Barns += IsBarn ? 1 : 0;
			Tally->Ruins += Ruin ? 1 : 0;
			if (IsHouse) { Tally->Hipped += Shape == ERoof::Hipped ? 1 : 0; Tally->LeanTo += Shape == ERoof::Skillion ? 1 : 0; }
			if (IsHouse && Bays == 1)
			{
				const double WinH0 = WinKind == LocalLook::Mullion ? 0.9 : WinKind == LocalLook::Sash ? 1.45 : 1.0;
				const int Fl = std::clamp(1 + int((Eaves - 1.15 - WinH0) / 2.7), 1, 3);
				++Tally->HouseFloors[Fl - 1];
				if (B.Height > 2.0) ++Tally->HouseMapped[Fl - 1];
				NearOpts Opts;
				NearHit H;
				if (NearestOf(B.X, B.Z, Opts, H) && H.Dist - H.Half < 25.0)
				{
					const P3 N = Local(0.0, 1.0, 0.0);
					const double Nx = N.X - B.X, Nz = N.Z - B.Z, Tx = H.Foot.X - B.X, Tz = H.Foot.Z - B.Z, Tl = std::hypot(Tx, Tz);
					++Tally->HousesByRoad;
					if (Tl > 0.1 && std::fabs(Nx * Tx + Nz * Tz) / Tl > 0.707) ++Tally->LongSideToRoad;
				}
			}
		}
		const double BayW = HalfLong * 2.0 / Bays;
		// [towns stage 4] A bay window on its front ground floor, as its period built them, the whole row or
		// street alike: canted or square, one storey or two (the facade kit's, where the game has them).
		int BayModel = -1;
		bool BayTwo = false;
		if (Period && FacadeKit && !Plain && Period->Bay > 0.0 && Chance(RowX, RowZ, 82) < Period->Bay)
		{
			BayModel = Chance(RowX, RowZ, 84) < Period->BaySquare ? PropBaySquare : PropBayCanted;
			if (!PropModelled[BayModel]) BayModel = -1;
			BayTwo = Chance(RowX, RowZ, 83) < Period->BayTwo;
		}
		// [towns stage 4] A row house's door: in a terrace the doors in pairs by the party walls (the first
		// house's by its neighbour, the next's by it, and on); a semi's at its outer end.
		bool DoorPlus = false;
		if (InRow)
		{
			const P3 Ahead = Local(1.0, 0.0, 0.0);
			const double ToSeed = (InRow->RowSeed.X - B.X) * (Ahead.X - B.X) + (InRow->RowSeed.Z - B.Z) * (Ahead.Z - B.Z);
			const bool HigherAtPlus = InRow->RowPos == 0 ? PartyPlus : ToSeed < 0.0;
			DoorPlus = InRow->RowSize == 2 ? (InRow->RowPos == 0) != HigherAtPlus : (InRow->RowPos % 2 == 0) == HigherAtPlus;
		}
		const Rgb StoneTrim = Shade(Stone, 1.15);
		const bool Surrounds = (Look.StoneSurrounds || (!Rendered && !Brick)) && !Plain;
		for (double Sg : {1.0, -1.0})
		{
			// The long walls (a skillion's high side up to its top), and the ends.
			const double LongTop = Shape == ERoof::Skillion && Sg < 0.0 ? Eaves + Rise : Eaves;
			const P3 L0 = Local(-HalfLong, Sg * HalfShort, Sunk), L1 = Local(HalfLong, Sg * HalfShort, Sunk), L2 = Local(HalfLong, Sg * HalfShort, LongTop), L3 = Local(-HalfLong, Sg * HalfShort, LongTop);
			WallQuad(L0, L1, L2, L3, Outside(L0, L2), 1.0);
			const P3 E0 = Local(Sg * HalfLong, -HalfShort, Sunk), E1 = Local(Sg * HalfLong, HalfShort, Sunk), E2 = Local(Sg * HalfLong, HalfShort, Eaves), E3 = Local(Sg * HalfLong, -HalfShort, Eaves);
			WallQuad(E0, E1, E2, E3, Outside(E0, E2), 0.93);
			// The roof, by its shape (a ruin has none).
			if (Ruin) continue;
			const double Low = Eaves - Drop, Top = Eaves + Rise;
			switch (Shape)
			{
			case ERoof::Gabled:
			{
				const P3 Peak = Local(Sg * HalfLong, 0.0, Top);
				WallQuad(E3, E2, Peak, E3, Outside(E0, E2), 0.93);
				RoofFace(Local(-HalfLong - OverMinus, Sg * (HalfShort + Over), Low), Local(HalfLong + OverPlus, Sg * (HalfShort + Over), Low), Local(HalfLong + OverPlus, 0.0, Top),
					Local(-HalfLong - OverMinus, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
				break;
			}
			case ERoof::Hipped:
			{
				// (a slope down each long side, and a hip down each end to the ridge's end; [towns stage 4] a
				// semi's party wall end gabled, its ridge running to the wall)
				const double RidgePlus = PartyPlus ? HalfLong : Ridge, RidgeMinus = PartyMinus ? HalfLong : Ridge;
				RoofFace(Local(-HalfLong - OverMinus, Sg * (HalfShort + Over), Low), Local(HalfLong + OverPlus, Sg * (HalfShort + Over), Low), Local(RidgePlus, 0.0, Top),
					Local(-RidgeMinus, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
				if (Sg > 0.0 ? PartyPlus : PartyMinus)
				{
					WallQuad(E3, E2, Local(Sg * HalfLong, 0.0, Top), E3, Outside(E0, E2), 0.93);
					break;
				}
				const P3 H0 = Local(Sg * (HalfLong + Over), -(HalfShort + Over), Low), H1 = Local(Sg * (HalfLong + Over), HalfShort + Over, Low), Tip = Local(Sg * (Sg > 0.0 ? RidgePlus : RidgeMinus), 0.0, Top);
				RoofFace(H0, H1, Tip, H0, Sky, Shade(Roof, 0.94));
				break;
			}
			case ERoof::Flat:
				// (one flat top, a hair over the walls, with a low parapet's coping round it on the finer squares)
				if (Sg > 0.0)
				{
					RoofFace(Local(-HalfLong, -HalfShort, Eaves + 0.02), Local(HalfLong, -HalfShort, Eaves + 0.02), Local(HalfLong, HalfShort, Eaves + 0.02),
						Local(-HalfLong, HalfShort, Eaves + 0.02), Sky, Roof);
					if (Fine)
						for (int K = 0; K < 4; ++K)
						{
							const bool OnLong = K < 2;
							const double S = K % 2 == 0 ? 1.0 : -1.0;
							const P3 Mid = OnLong ? Local(0.0, S * (HalfShort - 0.1), 0.0) : Local(S * (HalfLong - 0.1), 0.0, 0.0);
							const P3 Far = OnLong ? Local(1.0, S * (HalfShort - 0.1), 0.0) : Local(S * (HalfLong - 0.1), 1.0, 0.0);
							const double Ux = Far.X - Mid.X, Uz = Far.Z - Mid.Z;
							Box(Out, Mid.X, Mid.Z, P2(Ux, Uz), OnLong ? HalfLong : HalfShort, 0.12, Base + Eaves - 0.05, Base + Eaves + 0.45, Shade(Wall, 0.8));
						}
				}
				break;
			case ERoof::Skillion:
			{
				// (one slope, high on the - side; the ends' triangles up to it)
				const P3 Up = Local(Sg * HalfLong, -HalfShort, Top);
				WallQuad(E3, E2, Up, E3, Outside(E0, E2), 0.93);
				if (Sg > 0.0)
					RoofFace(Local(-HalfLong - Over, HalfShort + Over, Low), Local(HalfLong + Over, HalfShort + Over, Low),
						Local(HalfLong + Over, -(HalfShort + Over), Top + Drop), Local(-HalfLong - Over, -(HalfShort + Over), Top + Drop), Sky, Roof);
				break;
			}
			}
			if (!Fine || GlassHouse || Ruin) continue;
			// [OSM stage 3] The openings on this long wall, by what the building is.
			const P3 Away = Outside(L0, L2);
			const double Proud = Sg * (HalfShort + 0.04);
			const auto Flat = [&](double U0, double U1, double Lo, double Hi, const Rgb& C, double Out1)
			{
				Quad(Out, Local(U0, Proud + Sg * Out1, Lo), Local(U1, Proud + Sg * Out1, Lo), Local(U1, Proud + Sg * Out1, Hi), Local(U0, Proud + Sg * Out1, Hi), Away, C);
			};
			// (a bar along the wall from U0 to U1, Lo to Hi, D out from it: a sill, a lintel, a mullion)
			const auto Bar = [&](double U0, double U1, double Lo, double Hi, double D, const Rgb& C)
			{
				const P3 Ma = Local((U0 + U1) * 0.5, Sg * (HalfShort + D * 0.5), 0.0), Mb = Local((U0 + U1) * 0.5 + 1.0, Sg * (HalfShort + D * 0.5), 0.0);
				Box(Out, Ma.X, Ma.Z, P2(Mb.X - Ma.X, Mb.Z - Ma.Z), std::fmax(0.01, (U1 - U0) * 0.5), D * 0.5 + 0.02, Base + Lo, Base + Hi, C);
			};
			// [build 149] Windows and doors as a builder makes them (beenster, build 148: "I thought we would have
			// windows, lintels, different doors ... This is not as good as Buttertubs"): each opening has its
			// reveal (the wall's own return, or a dressed stone surround), standing 15 cm proud of the glass so the
			// glass reads set back in the wall; a projecting sill and a lintel (dressed stone, a brick soldier
			// arch, or none on a rendered wall); a painted frame; and its glazing (sash: Georgian six over six or
			// Victorian two over two; casement: two lights and top lights; mullion: stone mullions and a
			// transom). Doors: a stone step, a reveal, a fanlight over most, and one of four leaves (six-panel,
			// Victorian half-glazed, a cottage's ledged planks, a modern part-glazed door), knocker and letter
			// plate, and on some a hood. All plain boxes and quads in the near squares only: a few hundred
			// triangles a house.
			const auto Slab = [&](double U0, double U1, double Lo, double Hi, double D0, double D1, const Rgb& C)
			{
				const P3 Ma = Local((U0 + U1) * 0.5, Sg * (HalfShort + (D0 + D1) * 0.5), 0.0), Mb = Local((U0 + U1) * 0.5 + 1.0, Sg * (HalfShort + (D0 + D1) * 0.5), 0.0);
				Box(Out, Ma.X, Ma.Z, P2(Mb.X - Ma.X, Mb.Z - Ma.Z), std::fmax(0.005, (U1 - U0) * 0.5), std::fmax(0.005, (D1 - D0) * 0.5), Base + Lo, Base + Hi, C);
			};
			// (a flat face D out from the wall, U0 to U1, Lo to Hi)
			const auto Face = [&](double U0, double U1, double Lo, double Hi, double D, const Rgb& C)
			{
				const double At = Sg * (HalfShort + D);
				Quad(Out, Local(U0, At, Lo), Local(U1, At, Lo), Local(U1, At, Hi), Local(U0, At, Hi), Away, C);
			};
			// (the house's joinery paint, mostly white, by street; its reveals the wall's own colour, darker in the return)
			// ([build 153] beenster: "too many black window frames... mostly white and some wood colours": white
			// seven in ten, cream, two oaks, now and then a dark green or black)
			static const Rgb Joinery[10] = {{0.62f, 0.62f, 0.60f}, {0.62f, 0.62f, 0.60f}, {0.64f, 0.64f, 0.62f}, {0.62f, 0.62f, 0.60f}, {0.60f, 0.60f, 0.58f},
				{0.62f, 0.62f, 0.60f}, {0.60f, 0.58f, 0.50f}, {0.20f, 0.12f, 0.06f}, {0.11f, 0.065f, 0.035f}, {0.03f, 0.07f, 0.04f}};
			const double JoinPick = 0.6 * Chance(StreetX, StreetZ, 63) + 0.4 * Chance(B.X, B.Z, 63);
			const Rgb& Paint = JoinPick > 0.985 ? Joinery[9] : Joinery[std::min(8, int(JoinPick * 9.0))];
			const Rgb Reveal = Surrounds ? StoneTrim : Shade(Wall, Rendered ? 0.8 : 0.72);
			const double RevW = Surrounds ? 0.17 : 0.11, RevD = 0.07;   // (the reveal's face across, and how far it stands out of the glass: [build 153] 0.15 stood too far off the brick)
			const auto Opening = [&](double U, double Lo, double W, double H, bool Sill)
			{
				Slab(U - W * 0.5 - RevW, U - W * 0.5, Lo, Lo + H, 0.0, RevD, Reveal);   // (the jambs)
				Slab(U + W * 0.5, U + W * 0.5 + RevW, Lo, Lo + H, 0.0, RevD, Reveal);
				if (Surrounds)
					Slab(U - W * 0.5 - RevW - 0.04, U + W * 0.5 + RevW + 0.04, Lo + H, Lo + H + 0.24, 0.0, RevD + 0.03, StoneTrim);   // (a stone lintel)
				else if (Brick)
				{
					Slab(U - W * 0.5 - 0.12, U + W * 0.5 + 0.12, Lo + H, Lo + H + 0.22, 0.0, RevD - 0.02, Shade(Wall, 0.62));   // (a soldier arch)
				}
				else Slab(U - W * 0.5 - RevW, U + W * 0.5 + RevW, Lo + H, Lo + H + 0.1, 0.0, RevD, Reveal);   // (the head)
				if (Sill)
				{
					const Rgb SillC = Surrounds || Rendered ? StoneTrim : Shade(StoneTrim, 0.85);
					Slab(U - W * 0.5 - RevW - 0.06, U + W * 0.5 + RevW + 0.06, Lo - 0.1, Lo, 0.0, RevD + 0.07, SillC);
				}
			};
			// [fps plan step 2] One of the facade kit's models on this wall: its foot at U along and Lo up, on the
			// wall's face, its front out of it, with its three colours (paint, trim, third).
			const auto Kit = [&](int Model, int Variant, double U, double Lo, const Rgb& Ca, const Rgb& Cb, const Rgb& Cc)
			{
				const P3 Foot = Local(U, Sg * HalfShort, Lo), Ahead = Local(U, Sg * (HalfShort + 1.0), Lo);
				RideProp Pr;
				Pr.Kind = uint16_t(Model);
				Pr.Variant = uint8_t(Variant);
				Pr.Mapped = false;
				Pr.Facade = true;
				Pr.X = float(Foot.X - Out.OriginX);
				Pr.Y = float(Foot.Y);
				Pr.Z = float(Foot.Z - Out.OriginZ);
				Pr.Yaw = float(std::atan2(Ahead.Z - Foot.Z, Ahead.X - Foot.X));
				const Rgb* Three[3] = {&Ca, &Cb, &Cc};
				for (int K = 0; K < 3; ++K)
				{
					Pr.Tint[K * 3] = Three[K]->R;
					Pr.Tint[K * 3 + 1] = Three[K]->G;
					Pr.Tint[K * 3 + 2] = Three[K]->B;
				}
				Out.Props.push_back(Pr);
			};
			const int SurroundKind = Surrounds ? PropSurStone : Brick ? PropSurBrick : PropSurPlain;
			const Rgb SillColour = Surrounds || Rendered ? StoneTrim : Shade(StoneTrim, 0.85);
			// (after dark a share of the windows lit, warm, a hair proud of the glass)
			const auto Glow = [&](double U, double Lo, double W, double H, int Salt)
			{
				if (Chance(B.X + U * 3.1, B.Z + Lo * 7.0 + Sg * 13.0 + Salt, 61) >= 0.4) return;
				const double Lit = Sg * (HalfShort + 0.06);
				static const Rgb Warm = {0.42f, 0.26f, 0.11f};
				Quad(Out.NightOf(), Local(U - W * 0.5 + 0.05, Lit, Lo + 0.05), Local(U + W * 0.5 - 0.05, Lit, Lo + 0.05), Local(U + W * 0.5 - 0.05, Lit, Lo + H - 0.05),
					Local(U - W * 0.5 + 0.05, Lit, Lo + H - 0.05), Away, Warm, false, 0.0f);
			};
			const auto Window = [&](double U, double Lo, double W, double H, int Salt)
			{
				if (FacadeKit)
				{
					const int Type = WinKind == LocalLook::Sash ? 0 : WinKind == LocalLook::Casement ? 1 : 2;
					const int Fit = RideBuildingBits::FacadeSize(RideBuildingBits::WindowSizes[Type], 3, W, H);
					if (Fit >= 0)
					{
						const int Model = Type == 0 ? (Chance(StreetX + 3.0, StreetZ, 65) < 0.45 ? PropWinSash6 : PropWinSash2)
							: Type == 1 ? (!PlainGlazing && Chance(B.X + U, B.Z + Lo, 66) < 0.35 ? PropWinCasementTop : PropWinCasement)
							: (Chance(B.X + U, B.Z + Lo, 64) < 0.5 ? PropWinMullionLeaded : PropWinMullion);
						Kit(Model, Fit, U, Lo, Paint, Surrounds ? StoneTrim : Paint, Paint);
						Kit(SurroundKind, Type * 3 + Fit, U, Lo, Paint, Reveal, SillColour);
						Glow(U, Lo, W, H, Salt);
						return;
					}
				}
				Opening(U, Lo, W, H, true);
				Flat(U - W * 0.5, U + W * 0.5, Lo, Lo + H, Glass, 0.0);
				// (the frame: 6 cm of paint round the glass, just proud of it; flat faces, the reveal gives the depth)
				const double F0 = 0.04, F1 = 0.075, Fw = 0.06;
				Face(U - W * 0.5, U - W * 0.5 + Fw, Lo, Lo + H, F0 + 0.004, Paint);
				Face(U + W * 0.5 - Fw, U + W * 0.5, Lo, Lo + H, F0 + 0.004, Paint);
				Face(U - W * 0.5, U + W * 0.5, Lo + H - Fw, Lo + H, F0 + 0.005, Paint);
				Face(U - W * 0.5, U + W * 0.5, Lo, Lo + Fw * 1.4, F0 + 0.005, Paint);
				const double In0 = U - W * 0.5 + Fw, In1 = U + W * 0.5 - Fw, Iw = In1 - In0, Bot = Lo + Fw * 1.4, Tp = Lo + H - Fw;
				const double Gb = 0.022;   // (a glazing bar)
				const auto Vbar = [&](double Uc, double Y0, double Y1, double Wd) { Face(Uc - Wd * 0.5, Uc + Wd * 0.5, Y0, Y1, F0 + 0.007, Paint); };
				const auto Hbar = [&](double Yc, double Wd) { Face(In0, In1, Yc - Wd * 0.5, Yc + Wd * 0.5, F0 + 0.008, Paint); };
				switch (WinKind)
				{
				case LocalLook::Mullion:
				{
					// (stone mullions between two or three lights, a transom across the tall ones)
					const int Lights = W > 1.1 ? 3 : 2;
					for (int M = 1; M < Lights; ++M)
						Slab(In0 + Iw * M / Lights - 0.05, In0 + Iw * M / Lights + 0.05, Bot, Tp, 0.0, RevD - 0.02, Surrounds ? StoneTrim : Paint);
					if (H > 1.2) Slab(In0, In1, Bot + (Tp - Bot) * 0.68 - 0.04, Bot + (Tp - Bot) * 0.68 + 0.04, 0.0, RevD - 0.02, Surrounds ? StoneTrim : Paint);
					// (leaded lights' small panes in the older ones)
					if (Chance(B.X + U, B.Z + Lo, 64) < 0.5)
						for (int M = 0; M < Lights; ++M)
						{
							const double Pa0 = In0 + Iw * M / Lights + 0.05, Pa1 = In0 + Iw * (M + 1) / Lights - 0.05;
							Face(Pa0, Pa1, (Bot + Tp) * 0.5 - 0.006, (Bot + Tp) * 0.5 + 0.006, 0.05, Shade(Paint, 0.2));
							Face((Pa0 + Pa1) * 0.5 - 0.006, (Pa0 + Pa1) * 0.5 + 0.006, Bot, Tp, 0.05, Shade(Paint, 0.2));
						}
					break;
				}
				case LocalLook::Sash:
				{
					// (two sashes, the top one a hair proud of the bottom; the meeting rail; Georgian six over six
					// in the older streets, Victorian two over two else)
					const double Meet = (Bot + Tp) * 0.5;
					Slab(In0, In1, Meet - 0.035, Meet + 0.035, F0, F1 + 0.01, Paint);
					Face(In0, In1, Meet + 0.035, Tp, F0 + 0.003, Shade(Glass, 1.05));
					const bool Georgian = Chance(StreetX + 3.0, StreetZ, 65) < 0.45;
					if (Georgian)
					{
						for (int M = 1; M < 3; ++M) Vbar(In0 + Iw * M / 3.0, Bot, Tp, Gb);
						Hbar((Bot + Meet) * 0.5, Gb);
						Hbar((Meet + Tp) * 0.5, Gb);
					}
					else Vbar(U, Bot, Tp, Gb * 1.3);
					break;
				}
				case LocalLook::Casement:
				{
					// (two side-hung lights with a mullion between, and top lights over a transom)
					const double Tr = Bot + (Tp - Bot) * 0.7;
					Vbar(U, Bot, Tp, 0.06);
					Face(In0, In1, Tr - 0.03, Tr + 0.03, F0 + 0.009, Paint);
					if (!PlainGlazing && Chance(B.X + U, B.Z + Lo, 66) < 0.35)
						for (int M = 1; M < 3; ++M) { Vbar(In0 + Iw * M / 3.0, Tr, Tp, Gb); }
					break;
				}
				}
				Glow(U, Lo, W, H, Salt);
			};
			const auto Door = [&](double U, double W, double H, int Salt)
			{
				// ([build 153] beenster: "a distinct lack of colours in the doors": half the place's own colours,
				// half the bright ones every British street has)
				static const Rgb Bright[8] = {{0.36f, 0.03f, 0.025f}, {0.03f, 0.08f, 0.26f}, {0.05f, 0.17f, 0.07f}, {0.55f, 0.40f, 0.05f},
					{0.62f, 0.62f, 0.60f}, {0.20f, 0.12f, 0.06f}, {0.12f, 0.13f, 0.14f}, {0.10f, 0.20f, 0.24f}};
				const double DoorPick = Chance(B.X + U, B.Z + Salt, 62);
				const Tint& T = Look.Doors[int(DoorPick * 8.0) & 3];
				const Rgb Leaf = DoorPick < 0.5 ? Rgb{std::fmin(1.0f, T.R * 1.8f), std::fmin(1.0f, T.G * 1.8f), std::fmin(1.0f, T.B * 1.8f)} : Bright[int(Chance(B.Z + U, B.X + Salt, 72) * 8.0) & 7];
				// (a wide door, a shop's or a hall's, keeps the plain painted leaf: two of them)
				const bool Wide = W > 1.3;
				const double StylePick = Chance(B.X + U * 1.7, B.Z + Salt, 67);
				const int Style = Wide ? 0 : Period && !Period->Doors.empty() ? Period->Doors[std::min(Period->Doors.size() - 1, size_t(StylePick * double(Period->Doors.size())))]
					: int(StylePick * 4.0) & 3;   // 0 six-panel, 1 half-glazed Victorian, 2 ledged planks, 3 modern ([towns stage 4] as its period's)
				const double HoodShare = Period ? Period->Hood : 0.35;
				// ([build 153] beenster: "doors raised off the ground and no steps": on a slope the ground before a
				// door can lie well below the house's floor, its middle's ground. The door drops to meet it, up to
				// 0.9 m, and a flight of stone steps takes the rest down to the ground.)
				const P3 Before = Local(U, Sg * (HalfShort + 0.8), 0.0);
				const double GroundBefore = Ground(Before.X, Before.Z);
				const double Below = std::isnan(GroundBefore) ? 0.0 : GroundBefore - Base;
				const auto Steps = [&](double Foot)
				{
					const double Fall = Foot + 0.15 - Below;   // (the threshold's top above the ground)
					if (Fall <= 0.3) return;
					if (Tally) ++Tally->DoorSteps;
					const int Treads = std::min(8, int(std::ceil(Fall / 0.2)));
					const double Riser = Fall / Treads, Lip = FacadeKit ? 0.21 : 0.42;
					const Rgb StepStone = Shade(StoneTrim, 0.9);
					Slab(U - W * 0.5 - 0.18, U + W * 0.5 + 0.18, Below - 0.3, Foot + 0.15, 0.0, Lip, StepStone);   // (under the landing)
					for (int K = 1; K < Treads; ++K)
						Slab(U - W * 0.5 - 0.18, U + W * 0.5 + 0.18, Below - 0.3, Foot + 0.15 - K * Riser, Lip + (K - 1) * 0.28, Lip + K * 0.28, StepStone);
				};
				if (FacadeKit)
				{
					const int Fit = Wide ? RideBuildingBits::FacadeSize(RideBuildingBits::WideDoorSizes, 2, W, H) : RideBuildingBits::FacadeSize(RideBuildingBits::DoorSizes, 2, W, H);
					if (Fit >= 0)
					{
						const int Model = Wide ? PropDoorDouble : Style == 0 ? PropDoorPanel6 : Style == 1 ? PropDoorVictorian
							: Style == 2 ? (Chance(B.X + U, B.Z + Salt, 68) < 0.4 ? PropDoorLedgedLight : PropDoorLedged) : PropDoorModern;
						const double Foot = std::clamp(Below, -0.9, 0.3);
						if (Tally && Foot < -0.1) ++Tally->DoorsDropped;
						Kit(Model, Fit, U, Foot, Paint, StoneTrim, Leaf);
						Kit(SurroundKind, 9 + (Wide ? 2 : 0) + Fit, U, Foot + 0.15, Paint, Reveal, StoneTrim);
						if (!Wide && Chance(B.X + U, B.Z, 70) < HoodShare && Eaves > H + Foot + 0.9) Kit(PropDoorHood, Fit, U, Foot, Paint, Paint, Paint);
						Steps(Foot);
						return;
					}
				}
				const bool Fan = !Wide && Style != 2 && Style != 3 && H > 1.95;
				const double Lh = Fan ? H - 0.4 : H;   // (the leaf, under its fanlight)
				// (the step: a stone slab, 15 cm, out past the reveal)
				Slab(U - W * 0.5 - 0.18, U + W * 0.5 + 0.18, std::fmax(Sunk, -1.2), 0.15, 0.0, 0.42, Shade(StoneTrim, 0.9));
				Steps(0.0);
				Opening(U, 0.15, W, H - 0.15, false);
				// (the door frame, and the leaf set 5 cm back in it)
				Slab(U - W * 0.5, U - W * 0.5 + 0.07, 0.15, H, 0.03, 0.08, Paint);
				Slab(U + W * 0.5 - 0.07, U + W * 0.5, 0.15, H, 0.03, 0.08, Paint);
				Slab(U - W * 0.5, U + W * 0.5, H - 0.07, H, 0.03, 0.08, Paint);
				const double D0 = U - W * 0.5 + 0.07, D1 = U + W * 0.5 - 0.07, Dw = D1 - D0, Lo = 0.15, Hi = Lh - (Fan ? 0.0 : 0.07);
				Flat(D0, D1, Lo, Hi, Leaf, 0.0);
				const double Lf = 0.04;   // (the leaf's face, out from the wall: what its panels stand on)
				if (Fan)
				{
					// (a transom bar and the fanlight's glass over it, spoked on a Georgian one)
					Slab(D0, D1, Lh - 0.03, Lh + 0.04, 0.03, 0.08, Paint);
					Flat(D0, D1, Lh + 0.04, H - 0.07, Glass, 0.0);
					if (Style == 0)
						for (int K = 1; K < 4; ++K) Face(D0 + Dw * K / 4.0 - 0.012, D0 + Dw * K / 4.0 + 0.012, Lh + 0.04, H - 0.07, 0.046, Paint);
				}
				const Rgb Raised = Shade(Leaf, 1.25), Sunk2 = Shade(Leaf, 0.7);
				// (a panel: its sunk field, a moulding's shadow line round it, and the raised middle)
				const auto Panel = [&](double U0, double U1, double Y0, double Y1, bool Glazed)
				{
					Face(U0, U1, Y0, Y1, Lf + 0.004, Glazed ? Glass : Sunk2);
					if (!Glazed) Face(U0 + 0.035, U1 - 0.035, Y0 + 0.035, Y1 - 0.035, Lf + 0.012, Raised);
					Face(U0 - 0.012, U1 + 0.012, Y1, Y1 + 0.012, Lf + 0.006, Raised);
					Face(U0 - 0.012, U1 + 0.012, Y0 - 0.012, Y0, Lf + 0.006, Raised);
				};
				const double Sw = 0.11, Mid = (D0 + D1) * 0.5;   // (stiles)
				switch (Style)
				{
				case 0:
				{
					if (Wide)
					{
						// (a pair of leaves, two long panels each)
						for (int K = 0; K < 2; ++K)
						{
							const double A0 = D0 + K * Dw * 0.5 + Sw * 0.7, A1 = D0 + (K + 1) * Dw * 0.5 - Sw * 0.7;
							Panel(A0, A1, Lo + 0.25, Lo + (Hi - Lo) * 0.42, false);
							Panel(A0, A1, Lo + (Hi - Lo) * 0.5, Hi - 0.2, false);
						}
						Face(Mid - 0.01, Mid + 0.01, Lo, Hi, Lf + 0.006, Sunk2);
						break;
					}
					// (six panels: two short over the lock rail, two tall above, two at the foot)
					const double Rows[4] = {Lo + 0.16, Lo + (Hi - Lo) * 0.36, Lo + (Hi - Lo) * 0.44, Hi - 0.13};
					for (double Su : {-1.0, 1.0})
					{
						const double A0 = Su < 0 ? D0 + Sw : Mid + Sw * 0.4, A1 = Su < 0 ? Mid - Sw * 0.4 : D1 - Sw;
						Panel(A0, A1, Rows[0], Rows[1], false);
						Panel(A0, A1, Rows[2], Rows[2] + (Rows[3] - Rows[2]) * 0.62, false);
						Panel(A0, A1, Rows[2] + (Rows[3] - Rows[2]) * 0.7, Rows[3], false);
					}
					break;
				}
				case 1:
				{
					// (Victorian: two glazed panels over the lock rail, two below)
					for (double Su : {-1.0, 1.0})
					{
						const double A0 = Su < 0 ? D0 + Sw : Mid + Sw * 0.4, A1 = Su < 0 ? Mid - Sw * 0.4 : D1 - Sw;
						Panel(A0, A1, Lo + 0.16, Lo + (Hi - Lo) * 0.42, false);
						Panel(A0, A1, Lo + (Hi - Lo) * 0.5, Hi - 0.13, true);
					}
					break;
				}
				case 2:
				{
					// (a cottage's ledged and braced door: upright boards, three ledges and the braces between)
					for (double Ub = D0 + 0.12; Ub < D1 - 0.05; Ub += 0.12) Face(Ub - 0.006, Ub + 0.006, Lo, Hi, Lf + 0.003, Sunk2);
					for (double Yl : {Lo + 0.2, (Lo + Hi) * 0.5, Hi - 0.2}) Slab(D0 + 0.03, D1 - 0.03, Yl - 0.06, Yl + 0.06, Lf, Lf + 0.025, Raised);
					// (a small light in the top on some)
					if (Chance(B.X + U, B.Z + Salt, 68) < 0.4) Face(Mid - 0.15, Mid + 0.15, Hi - 0.45, Hi - 0.25, Lf + 0.004, Glass);
					break;
				}
				default:
				{
					// (a modern door: a tall narrow light off to one side, and a flush leaf)
					const double G0 = D0 + Dw * 0.55, G1 = D1 - 0.12;
					Face(G0, G1, Lo + 0.55, Hi - 0.15, Lf + 0.004, Glass);
					Face(G0 - 0.02, G1 + 0.02, Lo + 0.53, Lo + 0.55, Lf + 0.006, Raised);
					Face(G0 - 0.02, G1 + 0.02, Hi - 0.15, Hi - 0.13, Lf + 0.006, Raised);
					break;
				}
				}
				if (!Wide)
				{
					// (brass: a letter plate on the lock rail, a knob by the lock, a knocker on the panelled ones)
					static const Rgb Brass = {0.42f, 0.30f, 0.09f}, Iron = {0.02f, 0.02f, 0.02f};
					const Rgb& Metal = Style == 2 ? Iron : Brass;
					Slab(Mid - 0.14, Mid + 0.14, Lo + (Hi - Lo) * 0.39, Lo + (Hi - Lo) * 0.39 + 0.06, Lf, Lf + 0.03, Metal);
					const double Knob = Chance(B.X, B.Z + Salt, 69) < 0.5 ? D0 + 0.08 : D1 - 0.08;
					Slab(Knob - 0.03, Knob + 0.03, Lo + 0.95, Lo + 1.01, Lf, Lf + 0.07, Metal);
					if (Style == 0) Slab(Mid - 0.05, Mid + 0.05, Lo + (Hi - Lo) * 0.7, Lo + (Hi - Lo) * 0.7 + 0.14, Lf, Lf + 0.03, Metal);
				}
				// (a hood over some: a flat lead-topped canopy on two brackets)
				if (!Wide && Chance(B.X + U, B.Z, 70) < HoodShare && Eaves > H + 0.9)
				{
					Slab(U - W * 0.5 - 0.3, U + W * 0.5 + 0.3, H + 0.3, H + 0.42, 0.0, 0.5, Paint);
					Slab(U - W * 0.5 - 0.3, U + W * 0.5 + 0.3, H + 0.42, H + 0.45, 0.0, 0.52, Shade(Slate, 0.8));
					for (double Su : {-1.0, 1.0}) Slab(U + Su * (W * 0.5 + 0.2) - 0.04, U + Su * (W * 0.5 + 0.2) + 0.04, H + 0.05, H + 0.3, 0.0, 0.35, Paint);
				}
			};
			// [towns stage 4] A bay window (the kit's) standing out of this wall at U, on the ground; its masonry
			// the dressed stone's, its little roof the house's.
			const auto BayAt = [&](double U, bool Two)
			{
				Kit(BayModel, Two ? 1 : 0, U, 0.0, Paint, StoneTrim, Shade(Roof, 0.9));
				if (Tally) ++Tally->BayWindows;
			};
			const bool Front = Sg == DoorSide;
			const double WinW = WinKind == LocalLook::Mullion ? 1.3 : WinKind == LocalLook::Sash ? 0.9 : 0.85;
			const double WinH = WinKind == LocalLook::Mullion ? 0.9 : WinKind == LocalLook::Sash ? 1.45 : 1.0;
			// (a floor of windows wherever its top, 0.15 m under the eaves, fits: build 145 drew a
			// two-storey house's 5.3 m eaves with one row, a bungalow's look)
			const int Floors = std::clamp(1 + int((Eaves - 1.15 - WinH) / 2.7), 1, 12);
			if (IsBarn)
			{
				// A barn: a cart door on its front, and slit vents in rows along both sides.
				if (Front)
				{
					const double H = std::fmin(3.2, Eaves - 0.4);
					Flat(-1.5, 1.5, 0.0, H, Shade(DoorWood, 0.8), 0.0);
					if (Surrounds) Bar(-1.7, 1.7, H, H + 0.35, 0.1, StoneTrim);
				}
				for (double Yv : {1.4, 2.9})
				{
					if (Yv + 0.7 > Eaves - 0.3) continue;
					for (double U = -HalfLong + 1.2; U < HalfLong - 1.0; U += 2.4)
						if (!Front || std::fabs(U) > 2.2) Flat(U - 0.06, U + 0.06, Yv, Yv + 0.7, Shade(Glass, 0.6), 0.0);
				}
			}
			else if (Works || IsShed || Garage)
			{
				// An industrial unit's roller doors, a shed's or a garage's one wide door, on its front.
				if (!Front) continue;
				if (IsIndustrial || (Works && HalfLong > 8.0))
				{
					const int Doors = std::clamp(int(HalfLong * 2.0 / 15.0), 1, 3);
					for (int K = 0; K < Doors; ++K)
					{
						const double U = -HalfLong + (K + 0.5) * (HalfLong * 2.0 / Doors), H = std::fmin(4.5, Eaves - 0.6);
						Flat(U - 2.0, U + 2.0, 0.0, H, Rgb{0.2f, 0.21f, 0.22f}, 0.0);
						for (double Y = 0.4; Y < H; Y += 0.45) Flat(U - 2.0, U + 2.0, Y, Y + 0.03, Rgb{0.12f, 0.12f, 0.13f}, 0.005);
					}
				}
				else Door(0.0, std::fmin(Garage ? 2.3 : 2.4, HalfLong * 1.6), std::fmin(2.1, Eaves - 0.3), 7);
			}
			else if (IsWorship)
			{
				// A church or a chapel: tall pointed windows down its sides, its door near one end of its front.
				for (double U = -HalfLong + 2.0; U < HalfLong - 1.5; U += 3.4)
				{
					if (Front && U < -HalfLong + 3.5) continue;
					const double Lo = 1.3, Hi = std::fmin(Eaves - 0.6, 3.6);
					if (Hi - Lo < 1.0) continue;
					Flat(U - 0.4, U + 0.4, Lo, Hi, Glass, 0.0);
					Quad(Out, Local(U - 0.4, Proud, Hi), Local(U + 0.4, Proud, Hi), Local(U, Proud, Hi + 0.5), Local(U - 0.4, Proud, Hi), Away, Glass);   // (its point)
					if (Surrounds) Bar(U - 0.5, U + 0.5, Lo - 0.12, Lo, 0.12, StoneTrim);
				}
				if (Front) Door(-HalfLong + 1.8, 1.1, 2.4, 9);
			}
			else if (Bays > 1 || (InRow && IsHouse))
			{
				// A terrace: each bay a door and a window beside it, and a window above each on every floor.
				// ([towns stage 4] a house of a mapped row is one such bay, its door where its row puts it; a
				// wide one, a semi's, a second window beside the first)
				for (int Bay = 0; Bay < Bays; ++Bay)
				{
					const double U0 = -HalfLong + Bay * BayW;
					const bool Mirror = InRow && Bays == 1 ? DoorPlus : Bay % 2 == 1;   // (the doors in pairs by the party walls, as rows are built)
					const bool Wide = BayW > 6.2;
					const double Du = U0 + (Mirror ? BayW - 0.9 : 0.9), Wu = U0 + (Mirror ? BayW * (Wide ? 0.55 : 0.35) : BayW * (Wide ? 0.45 : 0.65));
					const double Wu2 = U0 + (Mirror ? BayW * 0.2 : BayW * 0.8);
					if (Front) Door(Du, 0.9, 2.05, Bay);
					else Window(Du, 1.0, WinW * 0.8, WinH, Bay + 40);
					// ([towns stage 4] its bay in place of the window beside the door: 2.4 m across, room for it
					// between the door and the party wall; two storeys only where there are two)
					const bool BayHere = Front && BayModel >= 0 && BayW >= 4.4 && Eaves > 3.7;
					const bool BayUp = BayHere && BayTwo && Floors >= 2 && Eaves > 5.0;
					if (BayHere) BayAt(Wu, BayUp);
					else Window(Wu, 1.0, WinW, WinH, Bay);
					if (Wide) Window(Wu2, 1.0, WinW, WinH, Bay + 20);
					for (int Fl = 1; Fl < Floors; ++Fl)
					{
						if (!(BayUp && Fl == 1)) Window(Wu, 1.0 + Fl * 2.7, WinW, WinH, Bay + Fl * 100);
						Window(Du, 1.0 + Fl * 2.7, WinW * 0.8, WinH, Bay + Fl * 100 + 50);
						if (Wide) Window(Wu2, 1.0 + Fl * 2.7, WinW, WinH, Bay + Fl * 100 + 20);
					}
				}
			}
			else
			{
				// A house, a shop, a block, a civic building: windows a row a floor; one door on its front,
				// in the middle (a shop's ground floor left plain for its front, to come).
				const double Space = IsCivic ? 3.0 : 3.5;
				const int Along = std::max(1, int(HalfLong * 2.0 / Space));
				// ([towns stage 4] a house's bays on its front either side of its door, where they fit)
				const bool Bays0 = Front && IsHouse && BayModel >= 0 && Along >= 2 && HalfLong * 2.0 / Along >= 3.4 && Eaves > 3.7;
				const bool BaysUp = Bays0 && BayTwo && Floors >= 2 && Eaves > 5.0;
				for (int Fl = 0; Fl < Floors; ++Fl)
					for (int Wn = 0; Wn < Along; ++Wn)
					{
						const double U = -HalfLong + (Wn + 0.5) * (HalfLong * 2.0 / Along), Y = 1.0 + Fl * 2.7;
						if (Front && Fl == 0 && Wn == Along / 2)
						{
							Door(U, IsCivic || IsBlock ? 1.6 : 0.95, IsCivic ? 2.6 : 2.05, 3);
							continue;
						}
						if (IsShop && Fl == 0) continue;
						if (Bays0 && std::abs(Wn - Along / 2) == 1 && Fl <= (BaysUp ? 1 : 0))
						{
							if (Fl == 0) BayAt(U, BaysUp);
							continue;
						}
						Window(U, Y, IsCivic ? 1.0 : WinW, IsCivic ? std::fmin(2.0, 2.4) : WinH, Wn * 31 + Fl);
					}
			}
		}
		// [OSM stage 3] Chimneys: on houses (a stack at each gable end, or on the ridge, as the place
		// builds them; a terrace's on every other party wall), shops and now and then a civic building;
		// none on a barn, a shed, a works or a church.
		if (!Plain && !Ruin && !Big && (IsHouse || IsShop || (IsCivic && Chance(B.X, B.Z, 11) < 0.5)) && (Shape == ERoof::Gabled || Shape == ERoof::Hipped))
		{
			const Rgb Stack = Shade(Rendered ? Washes : Brick ? Bricks : Stone, 0.9);
			static const Rgb Pot = {0.3f, 0.12f, 0.06f};
			const double Top = Eaves + Rise;
			const auto Chimney = [&](double U, double From)
			{
				const P3 Ma = Local(U, 0.0, 0.0), Mb = Local(U + 1.0, 0.0, 0.0);
				const P2 Dir(Mb.X - Ma.X, Mb.Z - Ma.Z);
				if (Tally) ++Tally->Chimneys;
				Box(Out, Ma.X, Ma.Z, Dir, 0.3, (Bays > 1 || InRow) ? 0.45 : 0.35, Base + From, Base + Top + 0.8, Stack);
				Box(Out, Ma.X, Ma.Z, Dir, 0.34, (Bays > 1 || InRow) ? 0.5 : 0.4, Base + Top + 0.8, Base + Top + 0.9, Shade(Stack, 1.1));   // (its cap)
				for (double Sv : {-0.15, 0.15})
				{
					const P3 Pp = Local(U, Sv * ((Bays > 1 || InRow) ? 1.6 : 1.0), 0.0);
					Box(Out, Pp.X, Pp.Z, Dir, 0.08, 0.08, Base + Top + 0.9, Base + Top + 1.25, Pot);
				}
			};
			if (InRow && Bays == 1)
			{
				// ([towns stage 4] a row's stacks on its party walls, each drawn once, by one of the two houses;
				// an end house's outer gable its own stack where the place builds them so)
				if (StackPlus) Chimney(HalfLong, Top - 1.0);
				if (StackMinus) Chimney(-HalfLong, Top - 1.0);
				if (Shape == ERoof::Gabled && Look.Chimney == LocalLook::GableEnds)
				{
					if (!PartyPlus) Chimney(HalfLong - 0.34, Eaves);
					if (!PartyMinus) Chimney(-HalfLong + 0.34, Eaves);
				}
			}
			else if (Bays > 1)
				for (int Bay = 1; Bay < Bays; Bay += 2) Chimney(-HalfLong + Bay * BayW, Top - 1.0);
			else if (Look.Chimney == LocalLook::GableEnds && Shape == ERoof::Gabled)
			{
				// (each stack just inside its gable: its outer face 4 cm in from the brickwork, so it neither
				// sticks out past the wall, beenster build 148, nor fights the wall's own face, build 141)
				Chimney(-HalfLong + 0.34, Eaves);
				if (HalfLong > 3.0 || Chance(B.X, B.Z, 12) < 0.5) Chimney(HalfLong - 0.34, Eaves);
			}
			else Chimney((Chance(B.X, B.Z, 12) - 0.5) * HalfLong * 0.8, Top - 1.0);
		}
		// (the map's own churches too, where it tags the building one and it is a church's size)
		if ((size_t(Index) < BuildingRole.size() && BuildingRole[size_t(Index)] == 1) || ((Kind == "church" || (IsWorship && Kind != "chapel")) && B.W * B.D >= 120.0 && !Plain && !Ruin))
		{
			// A church: its tower at one end (the other if that one is over a road), square and a
			// little narrower than the nave, its belfry's openings near the top, battlemented, or in
			// the lowlands now and then a spire.
			const double TowerHalf = std::fmin(HalfShort * 0.8, 3.2);
			const double TowerTop = Eaves + Rise + 5.0 + 3.0 * Chance(B.X, B.Z, 53);
			double End = -1.0;
			for (double Try : {-1.0, 1.0})
			{
				bool Free = true;
				for (double Su : {0.0, 2.0})
					for (double Sv : {-1.0, 1.0})
					{
						const P3 Corner = Local(Try * (HalfLong + TowerHalf * Su - 0.2), Sv * TowerHalf, 0.0);
						Free = Free && OffRoad(Corner.X, Corner.Z, 0.8);
					}
				if (Free) { End = Try; break; }
				End = 0.0;
			}
			if (End != 0.0)
			{
				const double Mid = End * (HalfLong + TowerHalf - 0.2);
				const P3 Axis = Local(Mid, 0.0, TowerTop * 0.5);
				const auto Away = [&Axis](const P3& A, const P3& C) { return At3(Axis.X + ((A.X + C.X) * 0.5 - Axis.X) * 50.0, Axis.Y, Axis.Z + ((A.Z + C.Z) * 0.5 - Axis.Z) * 50.0); };
				const double S4[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
				const auto At4 = [&](int K, double Y) { return Local(Mid + S4[K][0] * TowerHalf, S4[K][1] * TowerHalf, Y); };
				for (int K = 0; K < 4; ++K)
				{
					const P3 Q0 = At4(K, Sunk), Q1 = At4(K + 1, Sunk), Q2 = At4(K + 1, TowerTop), Q3 = At4(K, TowerTop);
					WallQuad(Q0, Q1, Q2, Q3, Away(Q0, Q2), K % 2 == 0 ? 1.0 : 0.93);
					if (Fine)
					{
						// (the belfry's opening, a dark louvre just proud of the face)
						const P3 Pa = At4(K, 0.0), Pb = At4(K + 1, 0.0);
						const double Fx = (Pa.X + Pb.X) * 0.5, Fz = (Pa.Z + Pb.Z) * 0.5, Ux = (Pb.X - Pa.X) / (TowerHalf * 2.0), Uz = (Pb.Z - Pa.Z) / (TowerHalf * 2.0);
						const P3 Out3 = Away(Q0, Q2);
						const double Nx = Out3.X - Axis.X, Nz = Out3.Z - Axis.Z, Nl = std::hypot(Nx, Nz);
						const double Px = Fx + Nx / Nl * 0.04, Pz = Fz + Nz / Nl * 0.04;
						Quad(Out, At3(Px - Ux * 0.45, TowerTop - 3.0, Pz - Uz * 0.45), At3(Px + Ux * 0.45, TowerTop - 3.0, Pz + Uz * 0.45), At3(Px + Ux * 0.45, TowerTop - 1.3, Pz + Uz * 0.45),
							At3(Px - Ux * 0.45, TowerTop - 1.3, Pz - Uz * 0.45), Out3, Glass);
					}
				}
				const bool Spire = Look.Boundary == LocalLook::HedgeBank || (Look.BrickShare > 0.3 && Chance(B.X, B.Z, 54) < 0.4);
				if (Spire)
				{
					const P3 Tip = Local(Mid, 0.0, TowerTop + TowerHalf * 3.5);
					for (int K = 0; K < 4; ++K)
					{
						const P3 Q0 = At4(K, TowerTop), Q1 = At4(K + 1, TowerTop);
						Quad(Out, Q0, Q1, Tip, Q0, Away(Q0, Q1), Shade(Slate, K % 2 == 0 ? 1.0 : 0.85));
					}
				}
				else
				{
					// (a flat lead roof and a battlemented parapet: merlons along each side)
					Quad(Out, At4(0, TowerTop), At4(1, TowerTop), At4(2, TowerTop), At4(3, TowerTop), Sky, Shade(Slate, 1.3));
					if (Fine)
						for (int K = 0; K < 4; ++K)
							for (int M = 0; M < 3; ++M)
							{
								const double T = (M + 0.5) / 3.0;
								const P3 Pa = At4(K, TowerTop), Pb = At4(K + 1, TowerTop);
								const double Mx = Pa.X + (Pb.X - Pa.X) * T, Mz = Pa.Z + (Pb.Z - Pa.Z) * T;
								const double Lx = Pb.X - Pa.X, Lz = Pb.Z - Pa.Z, Ll = std::hypot(Lx, Lz);
								Box(Out, Mx, Mz, P2(Lx / Ll, Lz / Ll), 0.35, 0.2, Base + TowerTop - 0.05, Base + TowerTop + 0.7, Shade(Stone, 0.9));
							}
				}
			}
		}
	}
}
// [build 141 feedback] Farther off (beenster: "increase the drawing of objects in the very far
// distance"): the coarse squares had no buildings at all, so villages and towns vanished past a few
// hundred metres. Each building as a plain block under a plain pitched roof, its colours as the look
// of the place would give it, 12 triangles; the small ones left out the farther the square.
void RidePlan::FarBuildingsTile(double X0, double Z0, double Size, double Coarse, double MinArea, RideMesh& Out, size_t Most) const
{
	using namespace RideSceneBits;
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
	const int CellX0 = int(std::floor(X0 / SceneCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SceneCell)), CellZ0 = int(std::floor(Z0 / SceneCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SceneCell));
	static const Rgb Stone = {0.30f, 0.28f, 0.245f}, Slate = {0.085f, 0.085f, 0.095f}, StoneSlate = {0.17f, 0.155f, 0.13f};
	// (the biggest first, and no more than Most a square: a city's far square stays light)
	std::vector<std::pair<double, int>> Chosen;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Buildings.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Buildings.end()) continue;
			for (int Index : It->second)
			{
				const Building& B = G.Buildings[size_t(Index)];
				if (B.X < X0 || B.X >= X0 + Size || B.Z < Z0 || B.Z >= Z0 + Size || B.W * B.D < MinArea) continue;
				Chosen.emplace_back(-B.W * B.D, Index);
			}
		}
	if (Chosen.size() > Most)
	{
		std::nth_element(Chosen.begin(), Chosen.begin() + std::ptrdiff_t(Most), Chosen.end());
		Chosen.resize(Most);
	}
	{
			for (const std::pair<double, int>& Pick0 : Chosen)
			{
				const Building& B = G.Buildings[size_t(Pick0.second)];
				if (UnderTower(B.X, B.Z)) continue;
				const double Base = Ground(B.X, B.Z, Coarse);
				if (std::isnan(Base)) continue;
				const LocalLook& Look = LookHere(B.X, B.Z);
				const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
				const bool LongX = B.W >= B.D;
				const double HalfLong = (LongX ? B.W : B.D) * 0.5, HalfShort = (LongX ? B.D : B.W) * 0.5;
				const auto Local = [&](double Along, double Across, double Y)
				{
					const double Lx = LongX ? Along : Across, Lz = LongX ? Across : Along;
					return At3(B.X + Lx * Cs + Lz * Sn, Base + Y, B.Z - Lx * Sn + Lz * Cs);
				};
				const int Baked = B.More.empty() ? 0 : std::clamp(int(B.More[0]), 0, 7);
				const bool Big = HalfShort > 6.0 || Baked == 2 || Baked == 5;
				const double Rise = Big ? HalfShort * 0.35 : std::fmin(HalfShort * std::tan(double(Look.Pitch) * 0.0174533), 4.5);
				const double Eaves = B.Height > 2.0 ? std::fmax(2.3, B.Height - Rise)
					: RideBuildingBits::GuessEaves(Baked, Big, HalfLong, HalfShort, B.X, B.Z, TownLookAt(B.X, B.Z) != nullptr);
				const double Pick = Chance(B.X, B.Z, 2);
				const Rgb Wall = Pick < Look.RenderShare ? Rgb{Look.RenderColour.R, Look.RenderColour.G, Look.RenderColour.B}
					: Pick < Look.RenderShare + Look.BrickShare ? Rgb{Look.BrickColour.R, Look.BrickColour.G, Look.BrickColour.B}
					: Rgb{Stone.R * Look.HouseStone.R, Stone.G * Look.HouseStone.G, Stone.B * Look.HouseStone.B};
				const double RoofPick = Chance(B.X, B.Z, 6);
				const Rgb Roof = RoofPick < Look.TileShare ? Rgb{Look.TileColour.R, Look.TileColour.G, Look.TileColour.B} : RoofPick < Look.TileShare + Look.StoneSlateShare ? StoneSlate : Slate;
				const double Sunk = -2.0;
				const P3 Centre = Local(0.0, 0.0, Eaves * 0.5);
				const auto Away = [&Centre](const P3& A, const P3& C) { return At3(Centre.X + ((A.X + C.X) * 0.5 - Centre.X) * 50.0, Centre.Y, Centre.Z + ((A.Z + C.Z) * 0.5 - Centre.Z) * 50.0); };
				const P3 Sky = At3(B.X, Base + 1e5, B.Z);
				for (double Sg : {1.0, -1.0})
				{
					const P3 L0 = Local(-HalfLong, Sg * HalfShort, Sunk), L1 = Local(HalfLong, Sg * HalfShort, Sunk), L2 = Local(HalfLong, Sg * HalfShort, Eaves), L3 = Local(-HalfLong, Sg * HalfShort, Eaves);
					Quad(Out, L0, L1, L2, L3, Away(L0, L2), Wall);
					const P3 E0 = Local(Sg * HalfLong, -HalfShort, Sunk), E1 = Local(Sg * HalfLong, HalfShort, Sunk), E2 = Local(Sg * HalfLong, HalfShort, Eaves), E3 = Local(Sg * HalfLong, -HalfShort, Eaves);
					Quad(Out, E0, E1, E2, E3, Away(E0, E2), Shade(Wall, 0.93));
					Quad(Out, E3, E2, Local(Sg * HalfLong, 0.0, Eaves + Rise), E3, Away(E0, E2), Shade(Wall, 0.93));
					Quad(Out, Local(-HalfLong, Sg * HalfShort, Eaves), Local(HalfLong, Sg * HalfShort, Eaves), Local(HalfLong, 0.0, Eaves + Rise), Local(-HalfLong, 0.0, Eaves + Rise), Sky,
						Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
				}
			}
		}
}
} // namespace TorqueWorld
