// Field areas (OSM stage 2 batch C): the map's quarries, graveyards and cemeteries, allotments and
// orchards. Each colours the ground inside its outline (bare rock, mown grass, dug plots, orchard
// grass) and stands its own things on it: a quarry's worked faces along its edges, headstones in loose
// rows facing east, the allotments' sheds, and (in PlantsTile) an orchard's trees in rows.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace TorqueWorld
{
namespace RideFieldsBits
{
using namespace RideShapes;
constexpr double FieldCell = 64.0;
constexpr double BuildingCell = 256.0;   // as the scenery's squares (SceneCell): Scene->Buildings

long long FieldKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / FieldCell)) * 4000037LL + static_cast<long long>(std::floor(Z / FieldCell));
}

uint32_t FieldHash(const std::string& Id)
{
	uint32_t H = 2166136261u;
	for (char C : Id) H = (H ^ static_cast<unsigned char>(C)) * 16777619u;
	return H == 0 ? 1u : H;
}

// The same number from 0 to 1 for the same whole-metre place (and Salt).
double Roll(double X, double Z, uint32_t Salt)
{
	uint32_t V = static_cast<uint32_t>(std::lround(X * 4.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Z * 4.0)) * 19349663u ^ (Salt * 2654435761u);
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
}

// An upright slab: its foot's middle at (X, Z), its face towards Face (level, unit), W across and T
// thick, from Lo to Hi; leaning a little by Lean (m at the top, towards Face).
void Slab(RideMesh& Out, double X, double Z, const P2& Face, double W, double T, double Lo, double Hi, double Lean, const Paint& C)
{
	const P2 Side(Face.Z, -Face.X);
	const auto At = [&](double Su, double Sv, bool Top)
	{
		const double Y = Top ? Hi : Lo, L = Top ? Lean : 0.0;
		return Pt(X + Side.X * Su * W * 0.5 + Face.X * (Sv * T * 0.5 + L), Y, Z + Side.Z * Su * W * 0.5 + Face.Z * (Sv * T * 0.5 + L));
	};
	const Paint Dark = {C.R * 0.8f, C.G * 0.8f, C.B * 0.8f}, Lit = {C.R * 1.1f, C.G * 1.1f, C.B * 1.1f};
	SkyQuad(Out, At(1, 1, false), At(-1, 1, false), At(-1, 1, true), At(1, 1, true), C, false);      // its face
	SkyQuad(Out, At(-1, -1, false), At(1, -1, false), At(1, -1, true), At(-1, -1, true), Dark, false);   // its back
	SkyQuad(Out, At(-1, 1, false), At(-1, -1, false), At(-1, -1, true), At(-1, 1, true), Dark, false);
	SkyQuad(Out, At(1, -1, false), At(1, 1, false), At(1, 1, true), At(1, -1, true), Dark, false);
	SkyQuad(Out, At(-1, -1, true), At(1, -1, true), At(1, 1, true), At(-1, 1, true), Lit, false);   // its top
}
} // namespace RideFieldsBits

void RidePlan::IndexFields()
{
	using namespace RideFieldsBits;
	FieldAreas.clear();
	if (!Scene) return;
	// (each shape: its kind, ring and id; the features file's first, then the ground file's)
	struct Shape
	{
		int Kind;
		const std::vector<P2>* Ring;
		std::string Id;
	};
	std::vector<Shape> Shapes;
	for (const FeatureArea& A : Area.Roads.Scenery.Areas)
	{
		int Kind = -1;
		if (A.Kind == "quarry") Kind = FieldQuarry;
		else if (A.Kind == "cemetery" || A.Kind == "grave_yard") Kind = FieldGraves;
		else if (A.Kind == "allotments") Kind = FieldAllotments;
		else if (A.Kind == "orchard") Kind = FieldOrchard;
		else if (A.Kind == "scree" || A.Kind == "shingle") Kind = FieldScree;   // [OSM stage 2 batch D]
		else if (A.Kind == "reservoir") Kind = FieldReservoir;
		if (Kind >= 0) Shapes.push_back({Kind, &A.Ring, A.Id});
	}
	// [OSM stage 4] The ground file's.
	for (const GroundShape& A : Area.Roads.Ground.Areas)
	{
		const std::string& K = A.Kind;
		int Kind = -1;
		if (K == "lake" || K == "pond" || K == "reservoir" || K == "river" || K == "basin" || K == "canal") Kind = FieldReservoir;
		else if (K == "residential") Kind = FieldLawn;
		else if (K == "retail" || K == "commercial" || K == "industrial") Kind = FieldHard;
		else if (K == "farmyard") Kind = FieldFarmyard;
		else if (K == "construction") Kind = FieldSite;
		else if (K == "brownfield") Kind = FieldBrownfield;
		else if (K == "park" || K == "recreation_ground" || K == "village_green" || K == "playground") Kind = FieldPark;
		else if (K == "pitch") Kind = FieldPitch;
		else if (K == "beach" || K == "sand" || K == "dune") Kind = FieldSand;
		else if (K == "shingle") Kind = FieldScree;
		else if (K == "mud") Kind = FieldMud;
		else if (K == "car_park") Kind = FieldCarPark;
		else if (K == "wood") Kind = FieldWood;
		else if (K == "scrub") Kind = FieldScrub;
		else if (K == "heath") Kind = FieldHeath;
		else if (K == "wetland") Kind = FieldWetland;
		if (Kind < 0 || A.Pts.size() < 3) continue;
		Shapes.push_back({Kind, &A.Pts, K + std::to_string(std::lround(A.Pts[0].X * 10.0)) + "_" + std::to_string(std::lround(A.Pts[0].Z * 10.0))});
	}
	for (const Shape& Sh : Shapes)
	{
		const std::vector<P2>& Ring = *Sh.Ring;
		if (Ring.size() < 3) continue;
		FieldArea F;
		F.Kind = Sh.Kind;
		F.Ring = Ring;
		F.MinX = F.MaxX = Ring[0].X;
		F.MinZ = F.MaxZ = Ring[0].Z;
		for (const P2& P : Ring)
		{
			F.MinX = std::fmin(F.MinX, P.X); F.MaxX = std::fmax(F.MaxX, P.X);
			F.MinZ = std::fmin(F.MinZ, P.Z); F.MaxZ = std::fmax(F.MaxZ, P.Z);
		}
		// (none bigger than 3 km across: a mapping slip, or a whole estate, not a field; water and the
		// wild ground's outlines may be: a lake, a forest)
		const double Most = Sh.Kind == FieldReservoir ? 25000.0 : Sh.Kind >= FieldWood ? 8000.0 : 3000.0;
		if (F.MaxX - F.MinX > Most || F.MaxZ - F.MinZ > Most) continue;
		F.BoxArea = (F.MaxX - F.MinX) * (F.MaxZ - F.MinZ);
		F.Id = FieldHash(Sh.Id);
		if (Sh.Kind == FieldReservoir)
		{
			// (its water at the level of the lower part of its shore: a tenth of the way up its edge's
			// heights, so noisy heights on a steep bank never lift it up the slope)
			std::vector<double> Shore;
			for (const P2& P : Ring)
			{
				const double Y = Ground(P.X, P.Z);
				if (!std::isnan(Y)) Shore.push_back(Y);
			}
			if (Shore.empty()) continue;
			std::sort(Shore.begin(), Shore.end());
			F.Level = Shore[Shore.size() / 10] + 0.1;
		}
		const int Index = int(FieldAreas.size());
		for (double Z = std::floor(F.MinZ / FieldCell) * FieldCell; Z <= F.MaxZ; Z += FieldCell)
			for (double X = std::floor(F.MinX / FieldCell) * FieldCell; X <= F.MaxX; X += FieldCell) Scene->Fields[FieldKey(X + 1.0, Z + 1.0)].push_back(Index);
		FieldAreas.push_back(std::move(F));
	}
}

int RidePlan::FieldAt(double X, double Z) const
{
	using namespace RideFieldsBits;
	if (!Scene || FieldAreas.empty()) return -1;
	const auto It = Scene->Fields.find(FieldKey(X, Z));
	if (It == Scene->Fields.end()) return -1;
	// ([OSM stage 4] the smallest of those it is in: a pitch in a park in a town)
	int Best = -1;
	for (int I : It->second)
	{
		const FieldArea& F = FieldAreas[size_t(I)];
		if (Best >= 0 && F.BoxArea >= FieldAreas[size_t(Best)].BoxArea) continue;
		if (X >= F.MinX && X <= F.MaxX && Z >= F.MinZ && Z <= F.MaxZ && InRing(F.Ring, X, Z)) Best = I;
	}
	return Best;
}

void RidePlan::FieldsIn(double X0, double Z0, double Size, std::vector<int>& Out) const
{
	using namespace RideFieldsBits;
	Out.clear();
	if (!Scene || FieldAreas.empty()) return;
	for (double Z = std::floor(Z0 / FieldCell) * FieldCell; Z < Z0 + Size; Z += FieldCell)
		for (double X = std::floor(X0 / FieldCell) * FieldCell; X < X0 + Size; X += FieldCell)
		{
			const auto It = Scene->Fields.find(FieldKey(X + 1.0, Z + 1.0));
			if (It == Scene->Fields.end()) continue;
			for (int I : It->second)
				if (std::find(Out.begin(), Out.end(), I) == Out.end()) Out.push_back(I);
		}
	std::sort(Out.begin(), Out.end());
}

bool RidePlan::FieldTint(double X, double Z, float* Rgb) const
{
	using namespace RideFieldsBits;
	const int I = FieldAt(X, Z);
	if (I < 0) return false;
	const FieldArea& F = FieldAreas[size_t(I)];
	float To[3] = {Rgb[0], Rgb[1], Rgb[2]};
	double By = 0.0;
	switch (F.Kind)
	{
	case FieldQuarry:
	{
		// (bare stone, paler where it is worked, a little patchy)
		const double Pale = Roll(std::floor(X / 9.0), std::floor(Z / 9.0), F.Id);
		To[0] = float(0.19 + 0.05 * Pale); To[1] = float(0.18 + 0.05 * Pale); To[2] = float(0.155 + 0.045 * Pale);
		By = 0.9;
		break;
	}
	case FieldGraves:
		To[0] = 0.055f; To[1] = 0.1f; To[2] = 0.03f;   // (mown short and green)
		By = 0.6;
		break;
	case FieldAllotments:
	{
		// (plots 5 m by 12 m, dug or growing)
		const double Plot = Roll(std::floor(X / 5.0), std::floor(Z / 12.0), F.Id);
		if (Plot < 0.45) { To[0] = 0.075f; To[1] = 0.052f; To[2] = 0.035f; }
		else if (Plot < 0.85) { To[0] = 0.06f; To[1] = 0.11f; To[2] = 0.03f; }
		else { To[0] = 0.1f; To[1] = 0.11f; To[2] = 0.05f; }
		By = 0.85;
		break;
	}
	case FieldOrchard:
		To[0] = 0.075f; To[1] = 0.11f; To[2] = 0.035f;
		By = 0.45;
		break;
	case FieldScree:
	{
		// (grey broken stone, lighter and darker in patches)
		const double Pale = Roll(std::floor(X / 6.0), std::floor(Z / 6.0), F.Id);
		To[0] = float(0.17 + 0.06 * Pale); To[1] = float(0.165 + 0.06 * Pale); To[2] = float(0.15 + 0.05 * Pale);
		By = 0.8;
		break;
	}
	case FieldReservoir: return false;   // (its water is drawn over it)
	// [OSM stage 4] The ground file's shapes.
	case FieldLawn:
	{
		// (gardens: lawn, a little patchy from plot to plot)
		const double Plot = Roll(std::floor(X / 11.0), std::floor(Z / 9.0), F.Id);
		To[0] = float(0.07 + 0.03 * Plot); To[1] = float(0.12 + 0.03 * Plot); To[2] = 0.04f;
		By = 0.55;
		break;
	}
	case FieldHard:
	{
		// (yards' concrete and tarmac, with mown verges and beds between: a third of it green)
		const double Patch = Roll(std::floor(X / 14.0), std::floor(Z / 14.0), F.Id);
		if (Patch < 0.33) { To[0] = 0.065f; To[1] = 0.11f; To[2] = 0.035f; By = 0.5; }
		else { To[0] = float(0.075 + 0.02 * Patch); To[1] = float(0.075 + 0.02 * Patch); To[2] = float(0.07 + 0.02 * Patch); By = 0.75; }
		break;
	}
	case FieldFarmyard:
	{
		// (mud, straw and concrete)
		const double Mix = Roll(std::floor(X / 7.0), std::floor(Z / 7.0), F.Id);
		if (Mix < 0.4) { To[0] = 0.075f; To[1] = 0.06f; To[2] = 0.04f; }
		else if (Mix < 0.7) { To[0] = 0.15f; To[1] = 0.13f; To[2] = 0.07f; }
		else { To[0] = 0.12f; To[1] = 0.12f; To[2] = 0.11f; }
		By = 0.8;
		break;
	}
	case FieldSite:
		To[0] = 0.12f; To[1] = 0.09f; To[2] = 0.06f;   // (bare earth)
		By = 0.85;
		break;
	case FieldBrownfield:
		To[0] = 0.11f; To[1] = 0.115f; To[2] = 0.06f;   // (rough grass over rubble)
		By = 0.6;
		break;
	case FieldPark:
		To[0] = 0.06f; To[1] = 0.115f; To[2] = 0.032f;   // (mown)
		By = 0.65;
		break;
	case FieldPitch:
	{
		// (mown in stripes 6 m wide)
		const bool Light = int(std::floor(X / 6.0)) % 2 == 0;
		To[0] = Light ? 0.07f : 0.055f; To[1] = Light ? 0.135f : 0.11f; To[2] = Light ? 0.04f : 0.03f;
		By = 0.85;
		break;
	}
	case FieldSand:
		To[0] = 0.36f; To[1] = 0.32f; To[2] = 0.23f;
		By = 0.85;
		break;
	case FieldMud:
		To[0] = 0.08f; To[1] = 0.07f; To[2] = 0.055f;
		By = 0.8;
		break;
	case FieldCarPark:
		To[0] = 0.04f; To[1] = 0.04f; To[2] = 0.042f;   // (tarmac)
		By = 0.95;
		break;
	case FieldWood:
		To[0] = 0.040f; To[1] = 0.060f; To[2] = 0.025f;   // (the floor under the trees, as the cover's forest)
		By = 0.7;
		break;
	case FieldScrub:
		To[0] = 0.07f; To[1] = 0.08f; To[2] = 0.035f;
		By = 0.55;
		break;
	case FieldHeath:
		To[0] = 0.095f; To[1] = 0.08f; To[2] = 0.05f;
		By = 0.55;
		break;
	case FieldWetland:
		To[0] = 0.06f; To[1] = 0.075f; To[2] = 0.04f;
		By = 0.6;
		break;
	default: return false;
	}
	for (int K = 0; K < 3; ++K) Rgb[K] = float(Rgb[K] + (To[K] - Rgb[K]) * By);
	return true;
}

void RidePlan::FieldsTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	using namespace RideFieldsBits;
	if (!Scene || FieldAreas.empty() || Detail < 1) return;
	std::vector<int> Here;
	FieldsIn(X0, Z0, Size, Here);
	const auto Inside = [X0, Z0, Size](double X, double Z) { return X >= X0 && X < X0 + Size && Z >= Z0 && Z < Z0 + Size; };
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	const Region& G = Area.Roads;
	const auto InBuilding = [&](double X, double Z)
	{
		const long long Key = static_cast<long long>(std::floor(X / BuildingCell)) * 4000037LL + static_cast<long long>(std::floor(Z / BuildingCell));
		const auto It = Scene->Buildings.find(Key);
		if (It == Scene->Buildings.end()) return false;
		for (int B : It->second)
		{
			const Building& H = G.Buildings[size_t(B)];
			const double C = std::cos(H.Yaw), S = std::sin(H.Yaw), Dx = X - H.X, Dz = Z - H.Z;
			if (std::fabs(Dx * C + Dz * S) < H.W * 0.5 + 1.0 && std::fabs(-Dx * S + Dz * C) < H.D * 0.5 + 1.0) return true;
		}
		return false;
	};
	for (int I : Here)
	{
		const FieldArea& F = FieldAreas[size_t(I)];
		if (F.Kind == FieldQuarry)
		{
			// Worked faces along the edge, where the ground inside lies well below the ground outside:
			// a rock face from the lip down to the floor, facing in.
			const Paint Rock = {0.23f, 0.22f, 0.19f};
			for (size_t K = 0; K < F.Ring.size(); ++K)
			{
				const P2 A = F.Ring[K], B = F.Ring[(K + 1) % F.Ring.size()];
				const double L = A.DistanceTo(B);
				if (L < 1.0) continue;
				const P2 U = (B - A) * (1.0 / L);
				P2 In(-U.Z, U.X);
				const P2 Mid = (A + B) * 0.5;
				if (!InRing(F.Ring, Mid.X + In.X * 3.0, Mid.Z + In.Z * 3.0)) In = In * -1.0;
				const int Steps = std::max(1, int(L / 4.0));
				for (int S = 0; S < Steps; ++S)
				{
					const P2 Pa = A + U * (L * S / Steps), Pb = A + U * (L * (S + 1) / Steps);
					const P2 Pm = (Pa + Pb) * 0.5;
					if (!Inside(Pm.X, Pm.Z)) continue;
					const double TopA = Ground(Pa.X - In.X, Pa.Z - In.Z), TopB = Ground(Pb.X - In.X, Pb.Z - In.Z);
					const double LowA = Ground(Pa.X + In.X * 10.0, Pa.Z + In.Z * 10.0), LowB = Ground(Pb.X + In.X * 10.0, Pb.Z + In.Z * 10.0);
					if (std::isnan(TopA) || std::isnan(TopB) || std::isnan(LowA) || std::isnan(LowB)) continue;
					if (std::fmin(TopA - LowA, TopB - LowB) < 1.0 || !OffRoad(Pm.X, Pm.Z, 1.0)) continue;
					const Paint C = {float(Rock.R * (0.85 + 0.3 * Roll(Pm.X, Pm.Z, 5u))), float(Rock.G * (0.85 + 0.3 * Roll(Pm.X, Pm.Z, 5u))), float(Rock.B * (0.85 + 0.3 * Roll(Pm.X, Pm.Z, 5u)))};
					SkyQuad(Out, Pt(Pa.X, LowA - 0.3, Pa.Z), Pt(Pb.X, LowB - 0.3, Pb.Z), Pt(Pb.X, TopB + 0.2, Pb.Z), Pt(Pa.X, TopA + 0.2, Pa.Z), C, true);
				}
			}
			continue;
		}
		if (Detail < 2) continue;
		if (F.Kind == FieldScree)
		{
			// [OSM stage 2 batch D] A scatter of broken stones, more and bigger downhill.
			int Stones = 0;
			for (double Z = std::ceil(std::fmax(F.MinZ, Z0) / 2.5) * 2.5; Z < std::fmin(F.MaxZ, Z0 + Size) && Stones < 900; Z += 2.5)
				for (double X = std::ceil(std::fmax(F.MinX, X0) / 2.5) * 2.5; X < std::fmin(F.MaxX, X0 + Size) && Stones < 900; X += 2.5)
				{
					if (Roll(X, Z, 31u) > 0.35) continue;
					const double Px = X + (Roll(X, Z, 32u) - 0.5) * 2.0, Pz = Z + (Roll(X, Z, 33u) - 0.5) * 2.0;
					if (!Inside(Px, Pz) || !InRing(F.Ring, Px, Pz) || !OffRoad(Px, Pz, 1.5)) continue;
					const double Y = Ground(Px, Pz);
					if (std::isnan(Y)) continue;
					++Stones;
					const double R = 0.15 + 0.5 * Roll(X, Z, 34u) * Roll(X, Z, 35u), Tone = 0.8 + 0.4 * Roll(X, Z, 36u);
					Column(Out, Pt(Px, Y - R * 0.4, Pz), R, R * 0.6, R * 0.7, 5, Paint{float(0.2 * Tone), float(0.195 * Tone), float(0.18 * Tone)}, R * 0.25);
				}
		}
		else if (F.Kind == FieldGraves)
		{
			// Headstones in loose rows running north to south, each facing east (+x), some leaning,
			// some gone; crosses now and then; none in the church or on the path.
			int Stones = 0;
			for (double X = std::ceil(std::fmax(F.MinX, X0) / 3.2) * 3.2; X < std::fmin(F.MaxX, X0 + Size) && Stones < 1500; X += 3.2)
				for (double Z = std::ceil(std::fmax(F.MinZ, Z0) / 1.5) * 1.5; Z < std::fmin(F.MaxZ, Z0 + Size) && Stones < 1500; Z += 1.5)
				{
					const double Px = X + (Roll(X, Z, 1u) - 0.5) * 0.5, Pz = Z + (Roll(X, Z, 2u) - 0.5) * 0.3;
					if (Roll(X, Z, 3u) < 0.3 || !Inside(Px, Pz) || !InRing(F.Ring, Px, Pz) || InBuilding(Px, Pz) || !OffRoad(Px, Pz, 1.0)) continue;
					const double Y = Ground(Px, Pz);
					if (std::isnan(Y)) continue;
					++Stones;
					const double Pick = Roll(X, Z, 4u), Tone = 0.75 + 0.5 * Roll(X, Z, 6u);
					const Paint C = Pick < 0.15 ? Paint{float(0.3 * Tone), float(0.3 * Tone), float(0.29 * Tone)} : Paint{float(0.2 * Tone), float(0.19 * Tone), float(0.17 * Tone)};
					const P2 East(1.0, 0.0);
					const double Lean = (Roll(X, Z, 7u) - 0.5) * (Roll(X, Z, 8u) < 0.2 ? 0.25 : 0.05);
					if (Pick > 0.93)
					{
						// (a cross on a plinth)
						Slab(Out, Px, Pz, East, 0.45, 0.35, Y - 0.1, Y + 0.25, 0.0, C);
						Slab(Out, Px, Pz, East, 0.12, 0.12, Y + 0.25, Y + 1.3, Lean, C);
						Slab(Out, Px, Pz, East, 0.6, 0.1, Y + 0.9, Y + 1.02, Lean * 0.8, C);
					}
					else
					{
						const double H = Pick < 0.4 ? 0.6 + 0.3 * Roll(X, Z, 9u) : 0.85 + 0.45 * Roll(X, Z, 9u);
						Slab(Out, Px, Pz, East, 0.5 + 0.3 * Roll(X, Z, 10u), 0.09, Y - 0.15, Y + H, Lean, C);
					}
				}
		}
		else if (F.Kind == FieldAllotments)
		{
			// A shed in the corner of one plot in three: plank walls, a pitched felt roof, a door.
			for (double Z = std::floor(std::fmax(F.MinZ, Z0) / 12.0) * 12.0; Z < std::fmin(F.MaxZ, Z0 + Size); Z += 12.0)
				for (double X = std::floor(std::fmax(F.MinX, X0) / 5.0) * 5.0; X < std::fmin(F.MaxX, X0 + Size); X += 5.0)
				{
					const double Sx = X + 1.4, Sz = Z + 1.2;
					if (Roll(X, Z, F.Id) > 0.33 || !Inside(Sx, Sz) || !InRing(F.Ring, Sx - 1.2, Sz - 1.0) || !InRing(F.Ring, Sx + 1.2, Sz + 1.0) || !OffRoad(Sx, Sz, 1.5) || InBuilding(Sx, Sz)) continue;
					const double Y = Ground(Sx, Sz);
					if (std::isnan(Y)) continue;
					const double Tone = 0.7 + 0.6 * Roll(X, Z, 11u);
					const Paint Wood = Roll(X, Z, 12u) < 0.3 ? Paint{float(0.04 * Tone), float(0.08 * Tone), float(0.05 * Tone)} : Paint{float(0.11 * Tone), float(0.07 * Tone), float(0.04 * Tone)};
					Slab(Out, Sx, Sz, P2(1.0, 0.0), 1.6, 2.0, Y - 0.1, Y + 1.9, 0.0, Wood);
					// (the roof: two slopes from the eaves to a ridge along x)
					const Paint Felt = {0.02f, 0.022f, 0.025f};
					SkyQuad(Out, Pt(Sx - 1.1, Y + 1.85, Sz - 0.9), Pt(Sx + 1.1, Y + 1.85, Sz - 0.9), Pt(Sx + 1.1, Y + 2.25, Sz), Pt(Sx - 1.1, Y + 2.25, Sz), Felt, true);
					SkyQuad(Out, Pt(Sx - 1.1, Y + 2.25, Sz), Pt(Sx + 1.1, Y + 2.25, Sz), Pt(Sx + 1.1, Y + 1.85, Sz + 0.9), Pt(Sx - 1.1, Y + 1.85, Sz + 0.9), Felt, true);
					for (double Su : {-1.0, 1.0})
						SkyQuad(Out, Pt(Sx + Su * 1.0, Y + 1.9, Sz - 0.8), Pt(Sx + Su * 1.0, Y + 1.9, Sz + 0.8), Pt(Sx + Su * 1.0, Y + 2.22, Sz), Pt(Sx + Su * 1.0, Y + 2.22, Sz), Wood, true);
					SkyQuad(Out, Pt(Sx + 1.01, Y, Sz - 0.35), Pt(Sx + 1.01, Y, Sz + 0.35), Pt(Sx + 1.01, Y + 1.75, Sz + 0.35), Pt(Sx + 1.01, Y + 1.75, Sz - 0.35),
						Paint{Wood.R * 0.6f, Wood.G * 0.6f, Wood.B * 0.6f}, true);   // (the door)
				}
		}
	}
}
} // namespace TorqueWorld
