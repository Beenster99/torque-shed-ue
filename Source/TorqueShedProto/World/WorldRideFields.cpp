// Field areas (OSM stage 2 batch C): the map's quarries, graveyards and cemeteries, allotments and
// orchards. Each colours the ground inside its outline (bare rock, mown grass, dug plots, orchard
// grass) and stands its own things on it: a quarry's worked faces along its edges, headstones in loose
// rows facing east, the allotments' sheds, and (in PlantsTile) an orchard's trees in rows.
#include "WorldRide.h"
#include "WorldRideChurches.h"

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
		bool Flowing = false;
		uint8_t Sub = SubNone;   // [everyday kits stage 6]
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
		if (Kind >= 0) Shapes.push_back({Kind, &A.Ring, A.Id, false});
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
		// [everyday kits stage 6] (what a park or pitch is for: the village set's furnishing)
		uint8_t Sub = SubNone;
		const std::string& W = A.Word;
		if (K == "playground") Sub = SubPlayground;
		else if (K == "village_green") Sub = SubGreen;
		else if (K == "pitch")
			Sub = W == "soccer" || W == "football" ? SubFootball : W == "rugby" || W == "rugby_union" || W == "rugby_league" ? SubRugby
				: W == "tennis" ? SubTennis : W == "basketball" ? SubBasketball : W == "multi" ? SubMultiCourt : SubNone;
		Shapes.push_back({Kind, &A.Pts, K + std::to_string(std::lround(A.Pts[0].X * 10.0)) + "_" + std::to_string(std::lround(A.Pts[0].Z * 10.0)), K == "river" || K == "canal", Sub});
	}
	// [places stage 2] The pull-offs made for places (IndexPlaces).
	for (size_t I = 0; I < PlaceStops.size(); ++I)
		if (PlaceStops[I].How == PlaceMade && PlaceStops[I].Ring.size() >= 3) Shapes.push_back({FieldPullOff, &PlaceStops[I].Ring, "pulloff" + std::to_string(I), false});
	// [roads stage 7] The petrol stations' forecourts (IndexTownLife).
	for (size_t I = 0; I < Forecourts.size(); ++I)
		if (Forecourts[I].Ring.size() >= 3) Shapes.push_back({FieldForecourt, &Forecourts[I].Ring, "forecourt" + std::to_string(I), false});
	for (size_t I = 0; I < Aprons.size(); ++I)   // [roads stage 8] (the airfields' aprons: tarmac as a forecourt's)
		if (Aprons[I].size() >= 3) Shapes.push_back({FieldForecourt, &Aprons[I], "apron" + std::to_string(I), false});
	for (const Shape& Sh : Shapes)
	{
		const std::vector<P2>& Ring = *Sh.Ring;
		if (Ring.size() < 3) continue;
		FieldArea F;
		F.Kind = Sh.Kind;
		F.Flowing = Sh.Flowing;
		F.Sub = Sh.Sub;
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
	// [churches stage 5] (the churchyards, made ones among the field areas)
	IndexChurchyards();
}

double RidePlan::OffChurch(const Churchyard& Y, double X, double Z) const
{
	const double Qx = X - Y.Church.X, Qz = Z - Y.Church.Z;
	const double U = std::fabs(Qx * Y.Cs - Qz * Y.Sn) - Y.HalfU, V = std::fabs(Qx * Y.Sn + Qz * Y.Cs) - Y.HalfV;
	return U > 0.0 && V > 0.0 ? std::hypot(U, V) : std::fmax(U, V);
}

void RidePlan::IndexChurchyards()
{
	using namespace RideFieldsBits;
	Churchyards.clear();
	if (!Scene) return;
	const Region& G = Area.Roads;
	const std::shared_ptr<const ChurchKit> Kit = ChurchKitNow();
	const auto OffRoad = [this](double X, double Z, double Spare)
	{
		NearOpts Opts;
		NearHit Hit;
		return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + Spare;
	};
	// (in a building other than the church's own pieces, Spare round it)
	const auto InOther = [&](double X, double Z, double Spare, const std::vector<int>& Own)
	{
		const long long Key = static_cast<long long>(std::floor(X / BuildingCell)) * 4000037LL + static_cast<long long>(std::floor(Z / BuildingCell));
		const auto It = Scene->Buildings.find(Key);
		if (It == Scene->Buildings.end()) return false;
		for (int B : It->second)
		{
			if (std::find(Own.begin(), Own.end(), B) != Own.end()) continue;
			const Building& H = G.Buildings[size_t(B)];
			const double C = std::cos(H.Yaw), S = std::sin(H.Yaw), Dx = X - H.X, Dz = Z - H.Z;
			if (std::fabs(Dx * C - Dz * S) < H.W * 0.5 + Spare && std::fabs(Dx * S + Dz * C) < H.D * 0.5 + Spare) return true;
		}
		return false;
	};
	// (the places of worship, biggest first; a smaller one within 30 m of a bigger is a piece of it)
	std::vector<int> Worship;
	for (size_t I = 0; I < G.Buildings.size(); ++I)
	{
		const Building& B = G.Buildings[I];
		if (!B.More.empty() && int(B.More[0]) == 6 && B.W >= 2.0 && B.D >= 2.0) Worship.push_back(int(I));
	}
	std::sort(Worship.begin(), Worship.end(), [&G](int A, int B) { return G.Buildings[size_t(A)].W * G.Buildings[size_t(A)].D > G.Buildings[size_t(B)].W * G.Buildings[size_t(B)].D; });
	std::vector<char> Used(Worship.size(), 0);
	// (the map's own walls, fences and hedges, each line's box, to leave a yard's edge to them)
	struct LineBox { const FeatureLine* L; double X0, Z0, X1, Z1; };
	std::vector<LineBox> Bounds;
	for (const FeatureLine& L : G.Scenery.Lines)
	{
		if (L.Line.size() < 2 || !(L.Kind == "wall" || L.Kind == "fence" || L.Kind == "hedge" || L.Kind == "retaining_wall" || L.Kind == "city_wall")) continue;
		LineBox Lb{&L, L.Line[0].X, L.Line[0].Z, L.Line[0].X, L.Line[0].Z};
		for (const P2& P : L.Line) { Lb.X0 = std::fmin(Lb.X0, P.X); Lb.X1 = std::fmax(Lb.X1, P.X); Lb.Z0 = std::fmin(Lb.Z0, P.Z); Lb.Z1 = std::fmax(Lb.Z1, P.Z); }
		Bounds.push_back(Lb);
	}
	const auto SegDist = [](const P2& A, const P2& B, double X, double Z)
	{
		const double Ux = B.X - A.X, Uz = B.Z - A.Z, L2 = Ux * Ux + Uz * Uz;
		const double T = L2 > 1e-9 ? std::clamp(((X - A.X) * Ux + (Z - A.Z) * Uz) / L2, 0.0, 1.0) : 0.0;
		return std::hypot(A.X + Ux * T - X, A.Z + Uz * T - Z);
	};
	for (size_t Wi = 0; Wi < Worship.size(); ++Wi)
	{
		if (Used[Wi]) continue;
		Used[Wi] = 1;
		const Building& B = G.Buildings[size_t(Worship[Wi])];
		const double Cs = std::cos(B.Yaw), Sn = std::sin(B.Yaw);
		double U0 = -B.W * 0.5, U1 = B.W * 0.5, V0 = -B.D * 0.5, V1 = B.D * 0.5;
		std::vector<int> Own{Worship[Wi]};
		for (size_t Wj = Wi + 1; Wj < Worship.size(); ++Wj)
		{
			const Building& P = G.Buildings[size_t(Worship[Wj])];
			if (Used[Wj] || std::hypot(P.X - B.X, P.Z - B.Z) > 30.0) continue;
			Used[Wj] = 1;
			Own.push_back(Worship[Wj]);
			const double Pc = std::cos(P.Yaw), Ps = std::sin(P.Yaw);
			for (double Su : {-0.5, 0.5})
				for (double Sv : {-0.5, 0.5})
				{
					const double Qx = P.X + Su * P.W * Pc + Sv * P.D * Ps - B.X, Qz = P.Z - Su * P.W * Ps + Sv * P.D * Pc - B.Z;
					const double U = Qx * Cs - Qz * Sn, V = Qx * Sn + Qz * Cs;
					U0 = std::fmin(U0, U); U1 = std::fmax(U1, U); V0 = std::fmin(V0, V); V1 = std::fmax(V1, V);
				}
		}
		const double Area0 = (U1 - U0) * (V1 - V0);
		if (Area0 < 55.0 || Area0 > 1500.0) continue;
		Churchyard Y;
		const double Um = (U0 + U1) * 0.5, Vm = (V0 + V1) * 0.5;
		Y.Church = P2(B.X + Um * Cs + Vm * Sn, B.Z - Um * Sn + Vm * Cs);
		Y.Cs = Cs; Y.Sn = Sn; Y.HalfU = (U1 - U0) * 0.5; Y.HalfV = (V1 - V0) * 0.5;
		const auto Local = [&Y](double U, double V) { return P2(Y.Church.X + U * Y.Cs + V * Y.Sn, Y.Church.Z - U * Y.Sn + V * Y.Cs); };
		// (its name: a chapel's has no lychgate)
		std::string Name;
		double NameDist = 40.0;
		for (const TorqueFit::Json& Mark : G.Landmarks)
		{
			const double Md = std::hypot(Mark.Num("x") - Y.Church.X, Mark.Num("z") - Y.Church.Z);
			if (Md < NameDist && Mark.Str("kind") == "worship") { NameDist = Md; Name = Mark.Str("name"); }
		}
		if (Kit && Kit->NameKind(Name) == 2) continue;
		Y.Chapel = Kit && Kit->NameKind(Name) == 1;
		// (the map's graveyard round it: the smallest that holds it or reaches within 6 m of it)
		int Mapped = -1;
		for (size_t Fi = 0; Fi < FieldAreas.size(); ++Fi)
		{
			const FieldArea& F = FieldAreas[Fi];
			if (F.Kind != FieldGraves || F.Yard >= 0 || F.MaxX < Y.Church.X - 60.0 || F.MinX > Y.Church.X + 60.0 || F.MaxZ < Y.Church.Z - 60.0 || F.MinZ > Y.Church.Z + 60.0) continue;
			bool Near = InRing(F.Ring, Y.Church.X, Y.Church.Z);
			for (size_t K = 0; K < F.Ring.size() && !Near; ++K)
			{
				const P2& A = F.Ring[K];
				Near = OffChurch(Y, A.X, A.Z) < 6.0 || SegDist(A, F.Ring[(K + 1) % F.Ring.size()], Y.Church.X, Y.Church.Z) < std::fmax(Y.HalfU, Y.HalfV) + 6.0;
			}
			if (Near && (Mapped < 0 || F.BoxArea < FieldAreas[size_t(Mapped)].BoxArea)) Mapped = int(Fi);
		}
		std::vector<P2> Ring;
		if (Mapped >= 0) Ring = FieldAreas[size_t(Mapped)].Ring;
		else
		{
			// (none mapped: out of a town, a yard made round it, each side out as far as 8 m while clear of the
			// roads, the other buildings and the map's other grounds; under 3 m, none that side)
			if (TownLookAt(Y.Church.X, Y.Church.Z) != nullptr) continue;
			double Grow[4] = {0.0, 0.0, 0.0, 0.0};   // (-u, +u, -v, +v)
			for (int Side = 0; Side < 4; ++Side)
				for (double M = 1.0; M <= 8.0; M += 1.0)
				{
					bool Clear = true;
					const bool AlongV = Side < 2;   // (the -u and +u sides run along v)
					const double Fixed = (Side % 2 == 0 ? -1.0 : 1.0) * ((AlongV ? Y.HalfU : Y.HalfV) + M);
					const double Run = (AlongV ? Y.HalfV : Y.HalfU) + 2.0;
					for (double T = -Run; T <= Run + 1e-6 && Clear; T += 2.0)
					{
						const P2 Q = AlongV ? Local(Fixed, T) : Local(T, Fixed);
						Clear = OffRoad(Q.X, Q.Z, 1.5) && !InOther(Q.X, Q.Z, 1.0, Own) && FieldAt(Q.X, Q.Z) < 0;
					}
					if (!Clear) break;
					Grow[Side] = M;
				}
			for (double& Gs : Grow) if (Gs < 3.0) Gs = 0.0;
			if (Grow[0] + Grow[1] + Grow[2] + Grow[3] < 6.0) continue;
			const double A0 = -Y.HalfU - Grow[0], A1 = Y.HalfU + Grow[1], B0 = -Y.HalfV - Grow[2], B1 = Y.HalfV + Grow[3];
			Ring = {Local(A0, B0), Local(A1, B0), Local(A1, B1), Local(A0, B1)};
			Y.Made = true;
		}
		if (Ring.size() < 3) continue;
		// (the gate: the yard's edge nearest the road by the church; its way out across that edge)
		NearOpts Opts;
		NearHit H;
		const bool ByRoad = NearestOf(Y.Church.X, Y.Church.Z, Opts, H) && H.Dist - H.Half < 80.0;
		const P2 Toward = ByRoad ? H.Foot : Local(0.0, Y.HalfV + 20.0);
		double Best = 1e18;
		size_t Edge = 0;
		for (size_t K = 0; K < Ring.size(); ++K)
		{
			const P2& A = Ring[K];
			const P2& C = Ring[(K + 1) % Ring.size()];
			const double Ux = C.X - A.X, Uz = C.Z - A.Z, L2 = Ux * Ux + Uz * Uz;
			if (L2 < 16.0) continue;   // (an edge 4 m long at least, for the gate and its walls)
			const double T = std::clamp(((Toward.X - A.X) * Ux + (Toward.Z - A.Z) * Uz) / L2, 2.0 / std::sqrt(L2), 1.0 - 2.0 / std::sqrt(L2));
			const P2 Q(A.X + Ux * T, A.Z + Uz * T);
			const double D = Q.DistanceTo(Toward);
			if (D < Best) { Best = D; Y.Gate = Q; Edge = K; }
		}
		if (Best > 1e17) continue;
		{
			const P2& A = Ring[Edge];
			const P2& C = Ring[(Edge + 1) % Ring.size()];
			const double L = A.DistanceTo(C);
			Y.Out = P2((C.Z - A.Z) / L, -(C.X - A.X) / L);
			if (InRing(Ring, Y.Gate.X + Y.Out.X * 1.0, Y.Gate.Z + Y.Out.Z * 1.0)) Y.Out = Y.Out * -1.0;
		}
		// (the path's end: the point 1.5 m out of the church nearest the gate)
		{
			const double Qx = Y.Gate.X - Y.Church.X, Qz = Y.Gate.Z - Y.Church.Z;
			const double U = std::clamp(Qx * Y.Cs - Qz * Y.Sn, -Y.HalfU - 1.5, Y.HalfU + 1.5), V = std::clamp(Qx * Y.Sn + Qz * Y.Cs, -Y.HalfV - 1.5, Y.HalfV + 1.5);
			// (onto the walk's ring, wherever the gate's line meets it)
			const double Su = (Y.HalfU + 1.5) - std::fabs(U), Sv = (Y.HalfV + 1.5) - std::fabs(V);
			Y.PathEnd = Su < Sv ? Local(U < 0.0 ? -Y.HalfU - 1.5 : Y.HalfU + 1.5, V) : Local(U, V < 0.0 ? -Y.HalfV - 1.5 : Y.HalfV + 1.5);
		}
		if (Y.Gate.DistanceTo(Y.PathEnd) < 1.0) continue;
		const uint32_t Seed = FieldHash(std::to_string(std::lround(Y.Church.X * 10.0)) + "_" + std::to_string(std::lround(Y.Church.Z * 10.0)));
		Y.Lych = !Y.Chapel && Roll(Y.Church.X, Y.Church.Z, Seed ^ 41u) < 0.55;
		// (the wall: along the yard's edge, where no road, building or mapped wall, fence or hedge is, a gap at the gate)
		std::vector<const LineBox*> Near;
		double Bx0 = 1e18, Bz0 = 1e18, Bx1 = -1e18, Bz1 = -1e18;
		for (const P2& P : Ring) { Bx0 = std::fmin(Bx0, P.X); Bx1 = std::fmax(Bx1, P.X); Bz0 = std::fmin(Bz0, P.Z); Bz1 = std::fmax(Bz1, P.Z); }
		Y.MinX = Bx0; Y.MaxX = Bx1; Y.MinZ = Bz0; Y.MaxZ = Bz1;
		if (Bx1 - Bx0 > 400.0 || Bz1 - Bz0 > 400.0) continue;   // (a town's cemetery, not a churchyard)
		for (const LineBox& Lb : Bounds)
			if (Lb.X1 > Bx0 - 3.0 && Lb.X0 < Bx1 + 3.0 && Lb.Z1 > Bz0 - 3.0 && Lb.Z0 < Bz1 + 3.0) Near.push_back(&Lb);
		const auto Walled = [&](double X, double Z)
		{
			for (const LineBox* Lb : Near)
				for (size_t K = 0; K + 1 < Lb->L->Line.size(); ++K)
					if (SegDist(Lb->L->Line[K], Lb->L->Line[K + 1], X, Z) < 2.0) return true;
			return false;
		};
		for (size_t K = 0; K < Ring.size(); ++K)
		{
			const P2 A = Ring[K], C = Ring[(K + 1) % Ring.size()];
			const double L = A.DistanceTo(C);
			if (L < 0.3) continue;
			const int Steps = std::max(1, int(std::ceil(L / 2.0)));
			bool Open = false;
			P2 From;
			for (int S = 0; S <= Steps; ++S)
			{
				const P2 Q = A + (C - A) * (double(S) / Steps);
				const P2 Mid = S < Steps ? A + (C - A) * ((S + 0.5) / Steps) : Q;
				// (none across the path, nor along a notch the map cut out of the yard for it: the yard both sides)
				const P2 Nrm((C.Z - A.Z) / L, -(C.X - A.X) / L);
				const bool Notch = InRing(Ring, Mid.X + Nrm.X * 2.5, Mid.Z + Nrm.Z * 2.5) && InRing(Ring, Mid.X - Nrm.X * 2.5, Mid.Z - Nrm.Z * 2.5);
				const bool Keep = S < Steps && Mid.DistanceTo(Y.Gate) > 1.7 && !Notch && SegDist(Y.Gate, Y.PathEnd, Mid.X, Mid.Z) > 1.6 && OffRoad(Mid.X, Mid.Z, 0.6)
					&& !InOther(Mid.X, Mid.Z, 0.3, Own) && OffChurch(Y, Mid.X, Mid.Z) > 0.5 && !Walled(Mid.X, Mid.Z);
				if (Keep && !Open) { Open = true; From = Q; }
				if (!Keep && Open)
				{
					Open = false;
					if (From.DistanceTo(Q) > 0.4) Y.Walls.push_back({From, Q});
				}
			}
		}
		// (the yews: an old one just inside the gate to one side; a pair of Irish yews either side of a long
		// path, now and then; an old one in the yard's far corner)
		const P2 In = Y.Out * -1.0, Side(Y.Out.Z, -Y.Out.X);
		const auto YewFits = [&](const P2& P, double R)
		{
			return InRing(Ring, P.X, P.Z) && OffChurch(Y, P.X, P.Z) > R + 1.5 && OffRoad(P.X, P.Z, R + 1.0) && !InOther(P.X, P.Z, R, Own)
				&& SegDist(Y.Gate, Y.PathEnd, P.X, P.Z) > R + 0.6;
		};
		{
			const double Sd = Roll(Y.Gate.X, Y.Gate.Z, Seed ^ 42u) < 0.5 ? 1.0 : -1.0;
			for (double Way : {Sd, -Sd})
			{
				const P2 P = Y.Gate + In * 5.5 + Side * (Way * 5.0);
				if (YewFits(P, 3.0)) { Y.Yews.push_back({P, 0}); break; }
			}
		}
		const double PathL = Y.Gate.DistanceTo(Y.PathEnd);
		if (PathL > 12.0 && Roll(Y.Gate.X, Y.Gate.Z, Seed ^ 43u) < 0.45)
		{
			const P2 Dir = (Y.PathEnd - Y.Gate) * (1.0 / PathL), Across(Dir.Z, -Dir.X);
			for (double At : {0.45, 0.7})
				for (double Way : {-1.0, 1.0})
				{
					const P2 P = Y.Gate + Dir * (PathL * At) + Across * (Way * 1.7);
					if (InRing(Ring, P.X, P.Z) && OffChurch(Y, P.X, P.Z) > 2.0) Y.Yews.push_back({P, 1});
				}
		}
		{
			double Far = 0.0;
			P2 Corner;
			P2 Centre(0.0, 0.0);
			for (const P2& P : Ring) Centre = Centre + P * (1.0 / double(Ring.size()));
			for (const P2& P : Ring)
				if (P.DistanceTo(Y.Gate) > Far) { Far = P.DistanceTo(Y.Gate); Corner = P; }
			const double Cl = Corner.DistanceTo(Centre);
			if (Cl > 6.0)
			{
				const P2 P = Corner + (Centre - Corner) * (4.5 / Cl);
				if (YewFits(P, 3.0) && P.DistanceTo(Y.Gate) > 12.0) Y.Yews.push_back({P, 0});
			}
		}
		// (its ground: the mapped graveyard's, else the made one as a graveyard of its own)
		const int YardIndex = int(Churchyards.size());
		if (Mapped >= 0)
		{
			Y.Field = Mapped;
			FieldAreas[size_t(Mapped)].Yard = YardIndex;
		}
		else
		{
			FieldArea F;
			F.Kind = FieldGraves;
			F.Ring = Ring;
			F.MinX = Bx0; F.MaxX = Bx1; F.MinZ = Bz0; F.MaxZ = Bz1;
			F.BoxArea = (Bx1 - Bx0) * (Bz1 - Bz0);
			F.Id = Seed;
			F.Yard = YardIndex;
			Y.Field = int(FieldAreas.size());
			for (double Z = std::floor(F.MinZ / FieldCell) * FieldCell; Z <= F.MaxZ; Z += FieldCell)
				for (double X = std::floor(F.MinX / FieldCell) * FieldCell; X <= F.MaxX; X += FieldCell) Scene->Fields[FieldKey(X + 1.0, Z + 1.0)].push_back(Y.Field);
			FieldAreas.push_back(std::move(F));
		}
		Churchyards.push_back(std::move(Y));
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
	case FieldForecourt:
		To[0] = 0.04f; To[1] = 0.04f; To[2] = 0.042f;   // [roads stage 7] (tarmac, as a car park)
		By = 0.95;
		break;
	case FieldPullOff:
		To[0] = 0.13f; To[1] = 0.12f; To[2] = 0.10f;   // [places stage 2] (grey-brown gravel, as the Dales' lay-bys)
		By = 0.95;
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
			// ([churches stage 5] in a churchyard, none on its path, the walk round the church, by the gate or
			// under its yews)
			const Churchyard* Yd = F.Yard >= 0 && size_t(F.Yard) < Churchyards.size() ? &Churchyards[size_t(F.Yard)] : nullptr;
			const auto Kept = [&](double Px, double Pz)
			{
				if (!Yd) return true;
				const double Ux = Yd->PathEnd.X - Yd->Gate.X, Uz = Yd->PathEnd.Z - Yd->Gate.Z, L2 = Ux * Ux + Uz * Uz;
				const double T = L2 > 1e-9 ? std::clamp(((Px - Yd->Gate.X) * Ux + (Pz - Yd->Gate.Z) * Uz) / L2, 0.0, 1.0) : 0.0;
				if (std::hypot(Yd->Gate.X + Ux * T - Px, Yd->Gate.Z + Uz * T - Pz) < 1.3 || OffChurch(*Yd, Px, Pz) < 2.6 || std::hypot(Px - Yd->Gate.X, Pz - Yd->Gate.Z) < 2.5) return false;
				for (const auto& Yw : Yd->Yews)
					if (std::hypot(Px - Yw.first.X, Pz - Yw.first.Z) < (Yw.second == 0 ? 3.6 : 1.3)) return false;
				return true;
			};
			int Stones = 0;
			for (double X = std::ceil(std::fmax(F.MinX, X0) / 3.2) * 3.2; X < std::fmin(F.MaxX, X0 + Size) && Stones < 1500; X += 3.2)
				for (double Z = std::ceil(std::fmax(F.MinZ, Z0) / 1.5) * 1.5; Z < std::fmin(F.MaxZ, Z0 + Size) && Stones < 1500; Z += 1.5)
				{
					const double Px = X + (Roll(X, Z, 1u) - 0.5) * 0.5, Pz = Z + (Roll(X, Z, 2u) - 0.5) * 0.3;
					if (Roll(X, Z, 3u) < 0.3 || !Inside(Px, Pz) || !InRing(F.Ring, Px, Pz) || InBuilding(Px, Pz) || !OffRoad(Px, Pz, 1.0) || !Kept(Px, Pz)) continue;
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
