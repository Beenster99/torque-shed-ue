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
void RidePlan::BuildingsTile(double X0, double Z0, double Size, bool Fine, const LocalLook& Look, RideMesh& Out, RideMesh& Washed, RideMesh& Houses) const
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
				const bool GlassHouse = Kind == "greenhouse" || WallMaterial == "glass";
				const bool Timber = Kind == "hut" || Kind == "cabin" || WallMaterial == "wood" || WallMaterial == "timber_framing";
				const bool Works = Kind == "hangar" || Kind == "stadium" || Kind == "grandstand" || Kind == "industrial" || Kind == "warehouse" || Kind == "retail"
					|| Kind == "commercial" || WallMaterial == "metal" || WallMaterial == "concrete" || WallMaterial == "cement_block";
				const bool Big = HalfShort > 6.0 || Works;   // (a barn or a shed: a low roof)
				// The roof's shape: the map's, else gabled; a flat-roofed block where the map has tar paper
				// or concrete on top.
				enum class ERoof { Gabled, Hipped, Flat, Skillion };
				ERoof Shape = ERoof::Gabled;
				if (RoofShape == "hipped" || RoofShape == "half_hipped" || RoofShape == "pyramidal" || RoofShape == "round" || RoofShape == "dome" || RoofShape == "mansard") Shape = ERoof::Hipped;
				else if (RoofShape == "flat") Shape = ERoof::Flat;
				else if (RoofShape == "skillion" || RoofShape == "lean_to") Shape = ERoof::Skillion;
				else if (RoofShape.empty() && (RoofMaterial == "tar_paper" || RoofMaterial == "concrete")) Shape = ERoof::Flat;
				const double Rise = Shape == ERoof::Flat ? 0.0 : Shape == ERoof::Skillion ? std::fmin(HalfShort * 0.3, 1.6)
					: Big ? HalfShort * 0.35 : std::fmin(HalfShort * 0.85, 3.6);
				const double Whole = B.Height > 2.0 ? B.Height : (Big ? 6.5 : 4.0 + 3.0 * Chance(B.X, B.Z, 1));
				const double Eaves = std::fmax(2.3, Whole - Rise);
				const double Pick = Chance(B.X, B.Z, 2);
				// (the walling: the map's material, else the local share of render, brick, else stone; a barn stays stone, or brick where brick is the rule)
				const bool TaggedRender = WallMaterial == "plaster" || WallMaterial == "render" || WallMaterial == "stucco";
				const bool TaggedBrick = WallMaterial == "brick", TaggedStone = WallMaterial == "stone" || WallMaterial == "sandstone" || WallMaterial == "limestone";
				const bool Plain = GlassHouse || Timber || Works;   // (drawn in the map's or the material's own colour, no scan)
				const bool Chosen = Plain || TaggedRender || TaggedBrick || TaggedStone;
				const bool Rendered = !Plain && (TaggedRender || (!Chosen && !Big && Pick < Look.RenderShare));
				const bool Brick = !Plain && !Rendered && (TaggedBrick || (!Chosen && (Big ? Pick < Look.BrickShare * 0.8 : Pick < Look.RenderShare + Look.BrickShare)));
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
					else if (Brick || Plain) Quad(Out, Q0, Q1, Q2, Q3, Toward, Shade(Wall, By));
					else Quad(Houses, Q0, Q1, Q2, Q3, Toward, Mask, false, 0.06f);
				};
				// (the roof: the map's material, else the local share of thatch, clay tiles or pantiles, stone slates, else slate)
				const double RoofPick = Chance(B.X, B.Z, 6);
				const bool AnyRoofTag = !RoofMaterial.empty();
				const bool Thatch = RoofMaterial == "thatch" || (!AnyRoofTag && !Big && !Plain && RoofPick < Look.ThatchShare);
				const bool Tiled = !Thatch && (RoofMaterial == "roof_tiles" || RoofMaterial == "tiles" || (!AnyRoofTag && RoofPick < Look.ThatchShare + Look.TileShare));
				const bool Flags = !Thatch && !Tiled && (RoofMaterial == "stone" || (!AnyRoofTag && !Plain && RoofPick < Look.ThatchShare + Look.TileShare + Look.StoneSlateShare));
				const bool Sheet = RoofMaterial == "metal" || RoofMaterial == "eternit" || RoofMaterial == "copper" || (!AnyRoofTag && Works);
				const bool Felt = RoofMaterial == "tar_paper" || RoofMaterial == "concrete" || (!AnyRoofTag && Shape == ERoof::Flat);
				const bool GlassRoof = RoofMaterial == "glass" || (!AnyRoofTag && GlassHouse);
				static const Rgb Straw = {0.27f, 0.21f, 0.11f}, RoofSheet = {0.20f, 0.21f, 0.21f}, RoofFelt = {0.06f, 0.06f, 0.065f}, Verdigris = {0.18f, 0.32f, 0.27f};
				const Rgb Tiles = {Look.TileColour.R, Look.TileColour.G, Look.TileColour.B};
				const Rgb RoofBase = HasRoofColour ? TaggedRoof : GlassRoof ? Glass : RoofMaterial == "copper" ? Verdigris : Sheet ? RoofSheet : Felt ? RoofFelt
					: Thatch ? Straw : Tiled ? Tiles : Flags ? StoneSlate : Slate;
				const Rgb Roof = Shade(RoofBase, 0.9 + 0.2 * Chance(B.X, B.Z, 5));
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
				const double Over = Shape == ERoof::Flat ? 0.0 : Thatch ? 0.55 : 0.3, Drop = Rise > 0.0 ? Over * Rise / HalfShort : 0.0;
				// (a hipped roof's ridge: shorter than the walls by the roof's depth each end, a point if it is square)
				const double Ridge = std::fmax(0.0, HalfLong - HalfShort);
				for (double Sg : {1.0, -1.0})
				{
					// The long walls (a skillion's high side up to its top), and the ends.
					const double LongTop = Shape == ERoof::Skillion && Sg < 0.0 ? Eaves + Rise : Eaves;
					const P3 L0 = Local(-HalfLong, Sg * HalfShort, Sunk), L1 = Local(HalfLong, Sg * HalfShort, Sunk), L2 = Local(HalfLong, Sg * HalfShort, LongTop), L3 = Local(-HalfLong, Sg * HalfShort, LongTop);
					WallQuad(L0, L1, L2, L3, Outside(L0, L2), 1.0);
					const P3 E0 = Local(Sg * HalfLong, -HalfShort, Sunk), E1 = Local(Sg * HalfLong, HalfShort, Sunk), E2 = Local(Sg * HalfLong, HalfShort, Eaves), E3 = Local(Sg * HalfLong, -HalfShort, Eaves);
					WallQuad(E0, E1, E2, E3, Outside(E0, E2), 0.93);
					// The roof, by its shape.
					const double Low = Eaves - Drop, Top = Eaves + Rise;
					switch (Shape)
					{
					case ERoof::Gabled:
					{
						const P3 Peak = Local(Sg * HalfLong, 0.0, Top);
						WallQuad(E3, E2, Peak, E3, Outside(E0, E2), 0.93);
						Quad(Out, Local(-HalfLong - Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, 0.0, Top),
							Local(-HalfLong - Over, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
						break;
					}
					case ERoof::Hipped:
					{
						// (a slope down each long side, and a hip down each end to the ridge's end)
						Quad(Out, Local(-HalfLong - Over, Sg * (HalfShort + Over), Low), Local(HalfLong + Over, Sg * (HalfShort + Over), Low), Local(Ridge, 0.0, Top),
							Local(-Ridge, 0.0, Top), Sky, Shade(Roof, Sg > 0.0 ? 1.0 : 0.88));
						const P3 H0 = Local(Sg * (HalfLong + Over), -(HalfShort + Over), Low), H1 = Local(Sg * (HalfLong + Over), HalfShort + Over, Low), Tip = Local(Sg * Ridge, 0.0, Top);
						Quad(Out, H0, H1, Tip, H0, Sky, Shade(Roof, 0.94));
						break;
					}
					case ERoof::Flat:
						// (one flat top, a hair over the walls, with a low parapet's coping round it on the finer squares)
						if (Sg > 0.0)
						{
							Quad(Out, Local(-HalfLong, -HalfShort, Eaves + 0.02), Local(HalfLong, -HalfShort, Eaves + 0.02), Local(HalfLong, HalfShort, Eaves + 0.02),
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
							Quad(Out, Local(-HalfLong - Over, -(HalfShort + Over), Top + Drop), Local(HalfLong + Over, -(HalfShort + Over), Top + Drop),
								Local(HalfLong + Over, HalfShort + Over, Low), Local(-HalfLong - Over, HalfShort + Over, Low), Sky, Roof);
						break;
					}
					}
					if (!Fine || Big || GlassHouse) continue;
					// Windows along the long walls, a row a floor, and a door in the one that faces +.
					const int Floors = std::clamp(int((Eaves - 0.4) / 2.7), 1, 12), Along = std::max(1, int(HalfLong * 2.0 / 3.5));
					for (int Fl = 0; Fl < Floors; ++Fl)
						for (int Wn = 0; Wn < Along; ++Wn)
						{
							const double U = -HalfLong + (Wn + 0.5) * (HalfLong * 2.0 / Along), Y = 1.0 + Fl * 2.7;
							const bool Door = Sg > 0.0 && Fl == 0 && Wn == Along / 2;
							const double Wide = Door ? 0.5 : 0.55, Lo = Door ? 0.0 : Y, Hi = Door ? 2.05 : Y + 1.2, Proud = Sg * (HalfShort + 0.04);
							const P3 W0 = Local(U - Wide, Proud, Lo), W1 = Local(U + Wide, Proud, Lo), W2 = Local(U + Wide, Proud, Hi), W3 = Local(U - Wide, Proud, Hi);
							Quad(Out, W0, W1, W2, W3, Outside(L0, L2), Door ? DoorWood : Glass);
							// (after dark a share of the windows lit, warm, a hair proud of the glass)
							if (!Door && Chance(B.X + U * 3.1, B.Z + Fl * 7.0 + Sg * 13.0, 61) < 0.4)
							{
								const double Glow = Sg * (HalfShort + 0.06);
								static const Rgb Warm = {0.42f, 0.26f, 0.11f};
								Quad(Out.NightOf(), Local(U - Wide + 0.05, Glow, Lo + 0.05), Local(U + Wide - 0.05, Glow, Lo + 0.05), Local(U + Wide - 0.05, Glow, Hi - 0.05),
									Local(U - Wide + 0.05, Glow, Hi - 0.05), Outside(L0, L2), Warm, false, 0.0f);
							}
						}
				}
				// (the map's own churches too, where it tags the building one and it is a church's size)
				if ((size_t(Index) < BuildingRole.size() && BuildingRole[size_t(Index)] == 1) || (Kind == "church" && B.W * B.D >= 120.0 && !Plain))
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
