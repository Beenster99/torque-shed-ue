// The buildings on a square (OSM stage 3; lifted out of SceneryTile as they were): each on its mapped
// footprint, its walls in the local stone, render, brick or the map's own material, under the roof
// the map or the local look gives it, windows a floor and a door near, a church's tower.
#include "WorldRide.h"
#include "WorldRideSceneBits.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace TorqueWorld
{
void RidePlan::BuildingsTile(double X0, double Z0, double Size, bool Fine, const LocalLook& Look, RideMesh& Out, RideMesh& Washed, RideMesh& Houses, RideMesh& RoofOut, RideMesh& BrickOut, BuildingTally* Tally) const
{
	using namespace RideSceneBits;
	if (!Scene || !Land) return;
	const Region& G = Area.Roads;
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
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Buildings.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Buildings.end()) continue;
			for (int Index : It->second)
			{
				const Building& B = G.Buildings[size_t(Index)];
				if (!Inside(B.X, B.Z) || B.W < 2.0 || B.D < 2.0 || !OffRoad(B.X, B.Z, std::fmin(B.W, B.D) * 0.5) || RailClear(B.X, B.Z) < std::fmin(B.W, B.D) * 0.5 + 2.0) continue;
				const double Base = Ground(B.X, B.Z);
				if (std::isnan(Base)) continue;
				// (local to the building: x across, z deep; the turn is the one the land's levelled pad uses)
				const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
				const bool LongX = B.W >= B.D;
				const double HalfLong = (LongX ? B.W : B.D) * 0.5, HalfShort = (LongX ? B.D : B.W) * 0.5;
				const auto Local = [&](double Along, double Across, double Y)
				{
					const double Lx = LongX ? Along : Across, Lz = LongX ? Across : Along;
					return At3(B.X + Lx * Cs + Lz * Sn, Base + Y, B.Z - Lx * Sn + Lz * Cs);
				};
				// What the map says it looks like, where it says: the tags of the point that lies inside its
				// footprint, nearest its middle. [OpenStreetMap: building, roof:shape, roof:colour,
				// roof:material, building:material, building:colour]
				const BuildingLook* Tags = nullptr;
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
				const double Rise = Shape == ERoof::Flat ? 0.0 : Shape == ERoof::Skillion ? std::fmin(HalfShort * 0.3, 1.6)
					: Big ? HalfShort * 0.35 : IsWorship ? std::fmin(HalfShort * 1.15, 6.5) : std::fmin(HalfShort * std::tan(double(Look.Pitch) * 0.0174533), 4.5);
				const double Whole = B.Height > 2.0 ? B.Height : (Big ? 6.5 : 4.0 + 3.0 * Chance(B.X, B.Z, 1));
				const double Eaves = std::fmax(2.3, Whole - Rise);
				const double Pick = Chance(B.X, B.Z, 2);
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
					: Shade(Rendered ? Washes : Brick ? Bricks : Stone, (Rendered ? 0.92 : 0.85) + (Rendered ? 0.14 : 0.3) * Chance(B.X, B.Z, 4));
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
				const double RoofPick = Chance(B.X, B.Z, 6);
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
				const Rgb Roof = Shade(RoofBase, 0.9 + 0.2 * Chance(B.X, B.Z, 5));
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
				const double Sunk = -1.0;
				const double Over = Shape == ERoof::Flat ? 0.0 : Thatch ? 0.55 : double(Look.Overhang), Drop = Rise > 0.0 ? Over * Rise / HalfShort : 0.0;
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
				}
				const double BayW = HalfLong * 2.0 / Bays;
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
						RoofFace(Local(-HalfLong - Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, 0.0, Top),
							Local(-HalfLong - Over, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
						break;
					}
					case ERoof::Hipped:
					{
						// (a slope down each long side, and a hip down each end to the ridge's end)
						RoofFace(Local(-HalfLong - Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, Sg * (HalfShort + Over), Low), Local(Ridge, 0.0, Top),
							Local(-Ridge, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
						const P3 H0 = Local(Sg * (HalfLong + Over), -(HalfShort + Over), Low), H1 = Local(Sg * (HalfLong + Over), HalfShort + Over, Low), Tip = Local(Sg * Ridge, 0.0, Top);
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
					const auto Window = [&](double U, double Lo, double W, double H, int Salt)
					{
						Flat(U - W * 0.5, U + W * 0.5, Lo, Lo + H, Glass, 0.0);
						if (Surrounds)
						{
							Bar(U - W * 0.5 - 0.08, U + W * 0.5 + 0.08, Lo - 0.12, Lo, 0.12, StoneTrim);       // (the sill)
							Bar(U - W * 0.5 - 0.12, U + W * 0.5 + 0.12, Lo + H, Lo + H + 0.2, 0.06, StoneTrim);   // (the lintel)
						}
						static const Rgb Paint = {0.55f, 0.55f, 0.53f};
						switch (Look.Window)
						{
						case LocalLook::Mullion:
							for (int M = 1; M < 3 && W > 0.9; ++M) Bar(U - W * 0.5 + W * M / 3.0 - 0.04, U - W * 0.5 + W * M / 3.0 + 0.04, Lo, Lo + H, 0.08, Surrounds ? StoneTrim : Paint);
							break;
						case LocalLook::Sash:
							Bar(U - W * 0.5, U + W * 0.5, Lo + H * 0.5 - 0.03, Lo + H * 0.5 + 0.03, 0.04, Paint);
							Bar(U - 0.02, U + 0.02, Lo, Lo + H, 0.03, Paint);
							break;
						case LocalLook::Casement:
							Bar(U - 0.03, U + 0.03, Lo, Lo + H, 0.04, Paint);
							break;
						}
						// (after dark a share of them lit, warm, a hair proud of the glass)
						if (Chance(B.X + U * 3.1, B.Z + Lo * 7.0 + Sg * 13.0 + Salt, 61) < 0.4)
						{
							const double Glow = Sg * (HalfShort + 0.06);
							static const Rgb Warm = {0.42f, 0.26f, 0.11f};
							Quad(Out.NightOf(), Local(U - W * 0.5 + 0.05, Glow, Lo + 0.05), Local(U + W * 0.5 - 0.05, Glow, Lo + 0.05), Local(U + W * 0.5 - 0.05, Glow, Lo + H - 0.05),
								Local(U - W * 0.5 + 0.05, Glow, Lo + H - 0.05), Away, Warm, false, 0.0f);
						}
					};
					const auto Door = [&](double U, double W, double H, int Salt)
					{
						const Tint& T = Look.Doors[int(Chance(B.X + U, B.Z + Salt, 62) * 4.0) & 3];
						Flat(U - W * 0.5, U + W * 0.5, 0.0, H, Rgb{T.R, T.G, T.B}, 0.0);
						if (Surrounds)
						{
							Bar(U - W * 0.5 - 0.14, U - W * 0.5, 0.0, H, 0.08, StoneTrim);
							Bar(U + W * 0.5, U + W * 0.5 + 0.14, 0.0, H, 0.08, StoneTrim);
							Bar(U - W * 0.5 - 0.14, U + W * 0.5 + 0.14, H, H + 0.22, 0.08, StoneTrim);
						}
					};
					const bool Front = Sg == DoorSide;
					const int Floors = std::clamp(int((Eaves - 0.4) / 2.7), 1, 12);
					const double WinW = Look.Window == LocalLook::Mullion ? 1.3 : Look.Window == LocalLook::Sash ? 0.9 : 0.85;
					const double WinH = Look.Window == LocalLook::Mullion ? 0.9 : Look.Window == LocalLook::Sash ? 1.45 : 1.0;
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
					else if (Bays > 1)
					{
						// A terrace: each bay a door and a window beside it, and a window above each on every floor.
						for (int Bay = 0; Bay < Bays; ++Bay)
						{
							const double U0 = -HalfLong + Bay * BayW;
							const bool Mirror = Bay % 2 == 1;   // (the doors in pairs by the party walls, as rows are built)
							const double Du = U0 + (Mirror ? BayW - 0.9 : 0.9), Wu = U0 + (Mirror ? BayW * 0.35 : BayW * 0.65);
							if (Front) Door(Du, 0.9, 2.05, Bay);
							else Window(Du, 1.0, WinW * 0.8, WinH, Bay + 40);
							Window(Wu, 1.0, WinW, WinH, Bay);
							for (int Fl = 1; Fl < Floors; ++Fl)
							{
								Window(Wu, 1.0 + Fl * 2.7, WinW, WinH, Bay + Fl * 100);
								Window(Du, 1.0 + Fl * 2.7, WinW * 0.8, WinH, Bay + Fl * 100 + 50);
							}
						}
					}
					else
					{
						// A house, a shop, a block, a civic building: windows a row a floor; one door on its front,
						// in the middle (a shop's ground floor left plain for its front, to come).
						const double Space = IsCivic ? 3.0 : 3.5;
						const int Along = std::max(1, int(HalfLong * 2.0 / Space));
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
						Box(Out, Ma.X, Ma.Z, Dir, 0.3, Bays > 1 ? 0.45 : 0.35, Base + From, Base + Top + 0.8, Stack);
						Box(Out, Ma.X, Ma.Z, Dir, 0.34, Bays > 1 ? 0.5 : 0.4, Base + Top + 0.8, Base + Top + 0.9, Shade(Stack, 1.1));   // (its cap)
						for (double Sv : {-0.15, 0.15})
						{
							const P3 Pp = Local(U, Sv * (Bays > 1 ? 1.6 : 1.0), 0.0);
							Box(Out, Pp.X, Pp.Z, Dir, 0.08, 0.08, Base + Top + 0.9, Base + Top + 1.25, Pot);
						}
					};
					if (Bays > 1)
						for (int Bay = 1; Bay < Bays; Bay += 2) Chimney(-HalfLong + Bay * BayW, Top - 1.0);
					else if (Look.Chimney == LocalLook::GableEnds && Shape == ERoof::Gabled)
					{
						Chimney(-HalfLong + 0.3, Eaves);
						if (HalfLong > 3.0 || Chance(B.X, B.Z, 12) < 0.5) Chimney(HalfLong - 0.3, Eaves);
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
}
} // namespace TorqueWorld
