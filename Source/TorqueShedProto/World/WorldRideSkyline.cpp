// The power and the skyline (OSM stage 2 batch B): the wires strung between the telegraph poles and
// between pylons, and what stands up tall (pylons, masts, wind turbines, silos, water towers, tanks),
// with substations' fenced yards and solar farms' rows of panels. All from the map, all plain coloured
// meshes made in code (with what stands on a square: RideMesh::Extra), so the far hills show their
// masts and turbines as the near fields show their poles.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace TorqueWorld
{
namespace RideShapes
{
V3 Pt(double X, double Y, double Z)
{
	V3 Q;
	Q.X = X; Q.Y = Y; Q.Z = Z;
	return Q;
}

// A vertex of what stands, its own (flat) normal; Alpha kept in step where the mesh has it.
void Vert(RideMesh& Out, const V3& Q, const V3& N, const Paint& C, double U, double V)
{
	Out.Pos.push_back(float(Q.X - Out.OriginX)); Out.Pos.push_back(float(Q.Y)); Out.Pos.push_back(float(Q.Z - Out.OriginZ));
	Out.Nrm.push_back(float(N.X)); Out.Nrm.push_back(float(N.Y)); Out.Nrm.push_back(float(N.Z));
	Out.Uv.push_back(float(U)); Out.Uv.push_back(float(V));
	Out.Col.push_back(C.R); Out.Col.push_back(C.G); Out.Col.push_back(C.B);
	if (!Out.Alpha.empty()) Out.Alpha.push_back(0.0f);
}

// A quad Q0..Q3, seen from both sides when Both (else facing the way its winding gives), flat shaded.
void SkyQuad(RideMesh& Out, const V3& Q0, const V3& Q1, const V3& Q2, const V3& Q3, const Paint& C, bool Both)
{
	const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
	V3 N = Pt(Ay * Bz - Az * By, Az * Bx - Ax * Bz, Ax * By - Ay * Bx);
	const double Len = std::sqrt(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
	if (Len < 1e-9) return;
	N = Pt(N.X / Len, N.Y / Len, N.Z / Len);
	const uint32_t V = uint32_t(Out.Vertices());
	const V3* Q[4] = {&Q0, &Q1, &Q2, &Q3};
	for (int K = 0; K < 4; ++K) Vert(Out, *Q[K], N, C, Q[K]->X - Out.OriginX + Q[K]->Z - Out.OriginZ, -Q[K]->Y);
	// (wound as the scenery's quads are: (Q2 - Q0) x (Q3 - Q1) is the side seen)
	Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
	if (Both) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
}

// A thin bar from A to B, W across: two flat strips at right angles, seen from both sides.
void Strut(RideMesh& Out, const V3& A, const V3& B, double W, const Paint& C)
{
	const double Dx = B.X - A.X, Dy = B.Y - A.Y, Dz = B.Z - A.Z;
	const double L = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
	if (L < 1e-6) return;
	const V3 T = Pt(Dx / L, Dy / L, Dz / L);
	// (one across it level, one across both)
	V3 S = Pt(-T.Z, 0.0, T.X);
	double Sl = std::sqrt(S.X * S.X + S.Z * S.Z);
	if (Sl < 1e-6) { S = Pt(1.0, 0.0, 0.0); Sl = 1.0; }
	S = Pt(S.X / Sl * W * 0.5, 0.0, S.Z / Sl * W * 0.5);
	const V3 U = Pt((T.Y * S.Z - T.Z * S.Y), (T.Z * S.X - T.X * S.Z), (T.X * S.Y - T.Y * S.X));
	for (const V3& O : {S, U})
		SkyQuad(Out, Pt(A.X - O.X, A.Y - O.Y, A.Z - O.Z), Pt(A.X + O.X, A.Y + O.Y, A.Z + O.Z), Pt(B.X + O.X, B.Y + O.Y, B.Z + O.Z), Pt(B.X - O.X, B.Y - O.Y, B.Z - O.Z), C, true);
}

// An upright round thing: from its foot's middle Base up H, R0 at the foot to R1 at the top, Sides
// round, capped with a cone of Cap (0 a flat top).
void Column(RideMesh& Out, const V3& Base, double R0, double R1, double H, int Sides, const Paint& C, double Cap)
{
	const uint32_t V = uint32_t(Out.Vertices());
	const double Lean = (R0 - R1) / std::fmax(H, 0.01);
	for (int K = 0; K <= Sides; ++K)
	{
		const double A = 6.283185307179586 * K / Sides, Ca = std::cos(A), Sa = std::sin(A);
		const double Nl = std::sqrt(1.0 + Lean * Lean);
		const V3 N = Pt(Ca / Nl, Lean / Nl, Sa / Nl);
		const double Shade = 0.85 + 0.15 * Ca;   // (a little light and dark round it)
		const Paint Cs = {float(C.R * Shade), float(C.G * Shade), float(C.B * Shade)};
		Vert(Out, Pt(Base.X + Ca * R0, Base.Y, Base.Z + Sa * R0), N, Cs, A * R0, 0.0);
		Vert(Out, Pt(Base.X + Ca * R1, Base.Y + H, Base.Z + Sa * R1), N, Cs, A * R1, -H);
	}
	for (int K = 0; K < Sides; ++K)
	{
		const uint32_t A = V + uint32_t(K) * 2, B = A + 2;
		Out.Idx.insert(Out.Idx.end(), {A, A + 1, B, B, A + 1, B + 1});
	}
	// The top: a cone (or flat) from the rim to the middle.
	const uint32_t Top = uint32_t(Out.Vertices());
	const Paint Lit = {float(C.R * 1.08), float(C.G * 1.08), float(C.B * 1.08)};
	Vert(Out, Pt(Base.X, Base.Y + H + Cap, Base.Z), Pt(0.0, 1.0, 0.0), Lit, 0.0, 0.0);
	for (int K = 0; K <= Sides; ++K)
	{
		const double A = 6.283185307179586 * K / Sides;
		const double Ny = R1 > 1e-6 ? R1 / std::sqrt(R1 * R1 + Cap * Cap) : 1.0, Nh = R1 > 1e-6 ? Cap / std::sqrt(R1 * R1 + Cap * Cap) : 0.0;
		Vert(Out, Pt(Base.X + std::cos(A) * R1, Base.Y + H, Base.Z + std::sin(A) * R1), Pt(std::cos(A) * Nh, Ny, std::sin(A) * Nh), Lit, std::cos(A) * R1, std::sin(A) * R1);
	}
	for (int K = 0; K < Sides; ++K) Out.Idx.insert(Out.Idx.end(), {Top, Top + 2 + uint32_t(K), Top + 1 + uint32_t(K)});
}

bool InRing(const std::vector<P2>& Ring, double X, double Z)
{
	bool In = false;
	for (size_t I = 0, J = Ring.size() - 1; I < Ring.size(); J = I++)
	{
		const P2& A = Ring[I];
		const P2& B = Ring[J];
		if ((A.Z > Z) != (B.Z > Z) && X < (B.X - A.X) * (Z - A.Z) / (B.Z - A.Z) + A.X) In = !In;
	}
	return In;
}

double RingArea(const std::vector<P2>& Ring, P2& Mid)
{
	double A = 0.0, Cx = 0.0, Cz = 0.0;
	for (size_t I = 0, J = Ring.size() - 1; I < Ring.size(); J = I++)
	{
		const double Cross = Ring[J].X * Ring[I].Z - Ring[I].X * Ring[J].Z;
		A += Cross;
		Cx += (Ring[J].X + Ring[I].X) * Cross;
		Cz += (Ring[J].Z + Ring[I].Z) * Cross;
	}
	if (std::fabs(A) < 1e-9)
	{
		Mid = Ring.empty() ? P2() : Ring[0];
		return 0.0;
	}
	Mid = P2(Cx / (3.0 * A), Cz / (3.0 * A));
	return std::fabs(A) * 0.5;
}
} // namespace RideShapes

namespace RideSkylineBits
{
using namespace RideShapes;
constexpr double SkyCell = 256.0;   // as the scenery's squares (SceneCell)
constexpr double PoleTop = 8.0;     // a telegraph pole's wires over its foot (its arm: FurnitureTile)
constexpr double PoleWire = 120.0;  // the longest span between poles
constexpr double PoleJoin = 90.0;   // a pole on no mapped line joins its neighbours this near
constexpr double PylonWire = 500.0;
constexpr double WireClear = 5.0;   // the least a wire may hang over a road

const Paint Galv = {0.26f, 0.27f, 0.27f}, WireGrey = {0.03f, 0.03f, 0.032f}, Turbine = {0.6f, 0.61f, 0.6f}, Concrete = {0.3f, 0.29f, 0.27f},
	SiloGreen = {0.07f, 0.12f, 0.08f}, KioskGreen = {0.05f, 0.09f, 0.06f}, Panel = {0.012f, 0.018f, 0.04f}, Fence = {0.06f, 0.07f, 0.065f};

long long SkyKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / SkyCell)) * 4000037LL + static_cast<long long>(std::floor(Z / SkyCell));
}

uint32_t SkyHash(const std::string& Id, double X, double Z)
{
	uint32_t H = 2166136261u;
	for (char C : Id) H = (H ^ static_cast<unsigned char>(C)) * 16777619u;
	if (Id.empty()) H ^= static_cast<uint32_t>(std::lround(X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(Z * 3.0)) * 19349663u;
	return H == 0 ? 1u : H;
}

double Unit(uint32_t H, int Salt)
{
	uint32_t V = H ^ (static_cast<uint32_t>(Salt) * 2654435761u);
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
}

} // namespace RideSkylineBits

void RidePlan::IndexSkyline()
{
	using namespace RideSkylineBits;
	Wires.clear();
	Talls.clear();
	Yards.clear();
	WiresDropped = 0;
	if (!Scene) return;
	const Region& G = Area.Roads;
	// The telegraph poles, by 16 m square, to find the one at a line's corner.
	std::unordered_map<long long, std::vector<int>> Poles;
	const auto PoleKey = [](double X, double Z) { return static_cast<long long>(std::floor(X / 16.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 16.0)); };
	for (size_t I = 0; I < Furniture.size(); ++I)
		if (Furniture[I].Kind == PowerPole) Poles[PoleKey(Furniture[I].At.X, Furniture[I].At.Z)].push_back(int(I));
	const auto PoleNear = [&](const P2& P, double Within)
	{
		int Best = -1;
		double BestD = Within;
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Poles.find(PoleKey(P.X + Dx * 16.0, P.Z + Dz * 16.0));
				if (It == Poles.end()) continue;
				for (int I : It->second)
				{
					const double D = Furniture[size_t(I)].At.DistanceTo(P);
					if (D < BestD) { BestD = D; Best = I; }
				}
			}
		return Best;
	};
	std::vector<P2> Along(Furniture.size(), P2(0.0, 0.0));   // each pole's wires' ways, summed (both ends the same way)
	std::vector<unsigned char> Strung(Furniture.size(), 0);
	const auto Add = [&](const P2& A, double Ya, const P2& B, double Yb, bool High, int Strands, double Spread)
	{
		const double Span = A.DistanceTo(B);
		if (Span < 2.0 || std::isnan(Ya) || std::isnan(Yb)) return false;
		if (Span > (High ? PylonWire : PoleWire)) { ++WiresDropped; return false; }
		Wire W;
		W.A = A; W.B = B; W.Ya = Ya; W.Yb = Yb;
		W.Sag = Span * (High ? 0.03 : 0.02);
		W.High = High;
		W.Strands = Strands;
		W.Spread = Spread;
		// (never lower than 5 m over a road: where it would be, it is left out)
		NearOpts Opts;
		const int Steps = std::max(2, int(Span / 4.0));
		for (int K = 1; K < Steps; ++K)
		{
			const double T = double(K) / Steps;
			const P2 P = A + (B - A) * T;
			NearHit Hit;
			if (!NearestOf(P.X, P.Z, Opts, Hit) || Hit.Dist > Hit.Half + 0.5) continue;
			const double Y = Ya + (Yb - Ya) * T - 4.0 * W.Sag * T * (1.0 - T);
			if (Y - Hit.Height < WireClear) { ++WiresDropped; return false; }
		}
		const P2 Mid = (A + B) * 0.5;
		Scene->Wires[SkyKey(Mid.X, Mid.Z)].push_back(int(Wires.size()));
		Wires.push_back(W);
		return true;
	};
	const auto PolePut = [&](const P2& P)
	{
		// (a corner of a mapped line with no pole of its own gets one)
		const int Have = PoleNear(P, 3.0);
		if (Have >= 0) return Have;
		const double Y = Ground(P.X, P.Z);
		if (std::isnan(Y)) return -1;
		NearOpts Opts;
		NearHit Hit;
		if (NearestOf(P.X, P.Z, Opts, Hit) && Hit.Dist < Hit.Half + 0.8) return -1;   // (never on a road)
		Furnish F;
		F.Kind = PowerPole;
		F.At = P;
		F.Face = P2(1.0, 0.0);
		F.Height = Y;
		F.Id = SkyHash("", P.X, P.Z);
		Scene->Furniture[SkyKey(P.X, P.Z)].push_back(int(Furniture.size()));
		Poles[PoleKey(P.X, P.Z)].push_back(int(Furniture.size()));
		Furniture.push_back(F);
		Along.push_back(P2(0.0, 0.0));
		Strung.push_back(0);
		return int(Furniture.size()) - 1;
	};
	const auto Turned = [&](int I, const P2& D)
	{
		// (summed the same way whichever end: a pole's arm square to the line through it)
		const P2 U = D.X < 0.0 || (D.X == 0.0 && D.Z < 0.0) ? D * -1.0 : D;
		Along[size_t(I)] = Along[size_t(I)] + U;
		Strung[size_t(I)] = 1;
	};
	// The pylons first (where a power line's corner has no mapped tower, one is put there).
	std::vector<P2> PylonAt;
	for (const FeaturePoint& P : G.Scenery.Points)
		if (P.Kind == "power_tower") PylonAt.push_back(P.At);
	const auto PylonNear = [&PylonAt](const P2& P) { for (const P2& Q : PylonAt) if (Q.DistanceTo(P) < 15.0) return true; return false; };
	for (const FeatureLine& L : G.Scenery.Lines)
	{
		const bool High = L.Kind == "power_line";
		if (!(High || L.Kind == "minor_line") || L.Line.size() < 2) continue;
		if (High)
		{
			const double PylonHeight = L.Height > 10.0 ? L.Height : 26.0;
			for (size_t K = 0; K < L.Line.size(); ++K)
			{
				const P2 P = L.Line[K];
				const P2 D = L.Line[std::min(K + 1, L.Line.size() - 1)] - L.Line[K > 0 ? K - 1 : 0];
				const double Dl = D.Length();
				TallThing T;
				T.Kind = TallPylon;
				T.At = P;
				T.Face = Dl > 1e-6 ? D * (1.0 / Dl) : P2(1.0, 0.0);
				T.Ground = Ground(P.X, P.Z);
				T.Height = PylonHeight;
				T.Id = SkyHash(L.Id, P.X, P.Z);
				if (std::isnan(T.Ground)) continue;
				if (!PylonNear(P)) PylonAt.push_back(P);
				Scene->Talls[SkyKey(P.X, P.Z)].push_back(int(Talls.size()));
				Talls.push_back(T);
				if (K + 1 < L.Line.size())
				{
					const P2 Q = L.Line[K + 1];
					const double Ya = T.Ground + PylonHeight * 0.8, Yb = Ground(Q.X, Q.Z) + PylonHeight * 0.8;
					Add(P, Ya, Q, Yb, true, 6, 11.0);
				}
			}
			continue;
		}
		// Along each straight of it: the mapped poles on it (within 8 m) in their order, and where two are
		// still more than a span apart, poles put up between them (the map's line is often only its corners).
		const auto String = [&](int From, int To)
		{
			if (From < 0 || To < 0 || From == To) return;
			const P2 Pa = Furniture[size_t(From)].At, Pb = Furniture[size_t(To)].At;
			const double Gap = Pa.DistanceTo(Pb);
			const int Bits = Gap <= PoleWire ? 1 : int(std::ceil(Gap / 85.0));
			int Prev = From;
			for (int B = 1; B <= Bits; ++B)
			{
				int Here = To;
				if (B < Bits)
				{
					Here = -1;
					for (double Nudge : {0.0, 8.0, -8.0, 16.0, -16.0})
					{
						const double T = std::clamp((double(B) * Gap / Bits + Nudge) / Gap, 0.02, 0.98);
						Here = PolePut(Pa + (Pb - Pa) * T);
						if (Here >= 0) break;
					}
					if (Here < 0) continue;
				}
				if (Here == Prev) continue;
				const P2 A = Furniture[size_t(Prev)].At, C = Furniture[size_t(Here)].At;
				if (Add(A, Furniture[size_t(Prev)].Height + PoleTop, C, Furniture[size_t(Here)].Height + PoleTop, false, 2, 1.2))
				{
					const P2 D = C - A;
					const P2 U = D * (1.0 / std::fmax(D.Length(), 1e-6));
					Turned(Prev, U);
					Turned(Here, U);
				}
				Prev = Here;
			}
		};
		int Last = PolePut(L.Line[0]);
		for (size_t K = 1; K < L.Line.size(); ++K)
		{
			const P2 Pa = L.Line[K - 1], Pb = L.Line[K];
			const double Len = Pa.DistanceTo(Pb);
			std::vector<std::pair<double, int>> On;
			if (Len > 1e-6)
			{
				const P2 U = (Pb - Pa) * (1.0 / Len);
				for (double T = 0.0; T <= Len; T += 12.0)
				{
					const P2 P = Pa + U * T;
					for (int Dz = -1; Dz <= 1; ++Dz)
						for (int Dx = -1; Dx <= 1; ++Dx)
						{
							const auto It = Poles.find(PoleKey(P.X + Dx * 16.0, P.Z + Dz * 16.0));
							if (It == Poles.end()) continue;
							for (int I : It->second)
							{
								const P2 Q = Furniture[size_t(I)].At - Pa;
								const double Ahead = Q.X * U.X + Q.Z * U.Z, Off = std::fabs(Q.X * U.Z - Q.Z * U.X);
								if (Ahead > 3.0 && Ahead < Len - 3.0 && Off < 8.0 && std::none_of(On.begin(), On.end(), [I](const std::pair<double, int>& O) { return O.second == I; }))
									On.emplace_back(Ahead, I);
							}
						}
				}
				std::sort(On.begin(), On.end());
			}
			for (const std::pair<double, int>& O : On)
			{
				String(Last, O.second);
				Last = O.second;
			}
			const int Next = PolePut(Pb);
			String(Last, Next);
			Last = Next;
		}
	}
	// A pole on no mapped line: joined to its nearest neighbour within 90 m, and to the next nearest
	// if that one lies the other way (a run of poles, not a fan).
	const size_t PoleCount = Furniture.size();
	for (size_t I = 0; I < PoleCount; ++I)
	{
		const Furnish& F = Furniture[I];
		if (F.Kind != PowerPole || Strung[I]) continue;
		int Best[2] = {-1, -1};
		double BestD[2] = {PoleJoin, PoleJoin};
		const int Reach = int(std::ceil(PoleJoin / 16.0));
		for (int Dz = -Reach; Dz <= Reach; ++Dz)
			for (int Dx = -Reach; Dx <= Reach; ++Dx)
			{
				const auto It = Poles.find(PoleKey(F.At.X + Dx * 16.0, F.At.Z + Dz * 16.0));
				if (It == Poles.end()) continue;
				for (int J : It->second)
				{
					if (size_t(J) == I) continue;
					const double D = Furniture[size_t(J)].At.DistanceTo(F.At);
					if (D < 3.0) continue;
					if (D < BestD[0]) { Best[1] = Best[0]; BestD[1] = BestD[0]; Best[0] = J; BestD[0] = D; }
					else if (D < BestD[1]) { Best[1] = J; BestD[1] = D; }
				}
			}
		for (int K = 0; K < 2; ++K)
		{
			const int J = Best[K];
			if (J < 0) continue;
			const Furnish& O = Furniture[size_t(J)];
			if (K == 1 && Best[0] >= 0)
			{
				// (only the other way from the first)
				const P2 D0 = Furniture[size_t(Best[0])].At - F.At, D1 = O.At - F.At;
				if ((D0.X * D1.X + D0.Z * D1.Z) / std::fmax(D0.Length() * D1.Length(), 1e-9) > -0.5) continue;
			}
			// (each pair strung once: from the lower index, or when the other has none of its own yet)
			if (size_t(J) < I && !Strung[size_t(J)]) continue;
			if (Add(F.At, F.Height + PoleTop, O.At, O.Height + PoleTop, false, 2, 1.2))
			{
				const P2 D = O.At - F.At;
				const P2 U = D * (1.0 / std::fmax(D.Length(), 1e-6));
				Turned(int(I), U);
				Turned(J, U);
			}
		}
	}
	// Each pole along its wires (its arm across them).
	for (size_t I = 0; I < Furniture.size(); ++I)
	{
		if (Furniture[I].Kind != PowerPole || !Strung[I]) continue;
		const double L = Along[I].Length();
		if (L > 1e-6) Furniture[I].Face = Along[I] * (1.0 / L);
	}
	// What stands up on its own.
	const auto Put = [&](int Kind, const P2& At, double Height, double Radius, const std::string& Id)
	{
		TallThing T;
		T.Kind = Kind;
		T.At = At;
		T.Ground = Ground(At.X, At.Z);
		if (std::isnan(T.Ground)) return;
		T.Height = Height;
		T.Radius = Radius;
		T.Id = SkyHash(Id, At.X, At.Z);
		// (a turbine faces the south-west wind, a few degrees either way)
		const double A = Kind == TallTurbine ? 2.356 + (Unit(T.Id, 3) - 0.5) * 0.4 : Unit(T.Id, 4) * 6.283;
		T.Face = P2(std::cos(A), std::sin(A));
		Scene->Talls[SkyKey(At.X, At.Z)].push_back(int(Talls.size()));
		Talls.push_back(T);
	};
	for (const FeaturePoint& P : G.Scenery.Points)
	{
		const double H = P.Height;
		if (P.Kind == "communication_tower") Put(TallMast, P.At, H > 8.0 ? H : 30.0, 0.0, P.Id);
		else if (P.Kind == "mobile_phone_tower") Put(TallMonopole, P.At, H > 8.0 ? H : 18.0, 0.0, P.Id);
		else if (P.Kind == "wind_turbine") Put(TallTurbine, P.At, H > 10.0 ? H : 45.0, 0.0, P.Id);
		else if (P.Kind == "water_tower") Put(TallWaterTower, P.At, H > 6.0 ? H : 18.0, 4.0, P.Id);
		else if (P.Kind == "silo") Put(TallSilo, P.At, H > 4.0 ? H : 14.0, 3.0, P.Id);
		else if (P.Kind == "storage_tank") Put(TallTank, P.At, H > 2.0 ? H : 8.0, 6.0, P.Id);
		else if (P.Kind == "substation") Put(TallKiosk, P.At, 1.7, 0.0, P.Id);
		else if (P.Kind == "power_tower" && !PylonNear(P.At)) Put(TallPylon, P.At, H > 10.0 ? H : 26.0, 0.0, P.Id);
	}
	for (const FeatureArea& A : G.Scenery.Areas)
	{
		if (A.Ring.size() < 3) continue;
		P2 Mid;
		const double Size = RingArea(A.Ring, Mid);
		const double R = std::sqrt(Size / 3.14159);
		if (A.Kind == "silo") Put(TallSilo, Mid, A.Height > 4.0 ? A.Height : std::fmin(25.0, std::fmax(8.0, R * 3.2)), std::fmin(8.0, std::fmax(1.5, R)), A.Id);
		else if (A.Kind == "storage_tank") Put(TallTank, Mid, A.Height > 2.0 ? A.Height : std::fmin(15.0, std::fmax(4.0, R * 0.6)), std::fmin(25.0, std::fmax(2.0, R)), A.Id);
		else if (A.Kind == "water_tower") Put(TallWaterTower, Mid, A.Height > 6.0 ? A.Height : 18.0, std::fmin(8.0, std::fmax(3.0, R)), A.Id);
		else if ((A.Kind == "substation" && Size > 40.0) || (A.Kind == "solar" && Size > 500.0))
		{
			// (a solar "area" smaller than that is panels on a roof, drawn with the building)
			Yard Y;
			Y.Solar = A.Kind == "solar";
			Y.Ring = A.Ring;
			Y.Mid = Mid;
			for (const P2& P : A.Ring) Y.Radius = std::fmax(Y.Radius, P.DistanceTo(Mid));
			Y.Id = SkyHash(A.Id, Mid.X, Mid.Z);
			Scene->Yards[SkyKey(Mid.X, Mid.Z)].push_back(int(Yards.size()));
			Yards.push_back(Y);
		}
	}
}

void RidePlan::SkylineTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	using namespace RideSkylineBits;
	if (!Scene) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const int CellX0 = int(std::floor(X0 / SkyCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / SkyCell));
	const int CellZ0 = int(std::floor(Z0 / SkyCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / SkyCell));
	const auto Each = [&](const std::unordered_map<long long, std::vector<int>>& Index, auto&& Do)
	{
		for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
			for (int Cx = CellX0; Cx <= CellX1; ++Cx)
			{
				const auto It = Index.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
				if (It != Index.end())
					for (int I : It->second) Do(I);
			}
	};
	if (Detail >= 2)
	{
		// The wires: each strand a parabola from end to end, a point every 8 m or so.
		Each(Scene->Wires, [&](int I)
		{
			const Wire& W = Wires[size_t(I)];
			if (!Inside((W.A + W.B) * 0.5)) return;
			const P2 D = W.B - W.A;
			const double Span = D.Length();
			const P2 Across = P2(-D.Z, D.X) * (1.0 / std::fmax(Span, 1e-6));
			const int Steps = std::clamp(int(Span / 8.0), 3, 16);
			const double Thick = W.High ? 0.05 : 0.03;
			for (int S = 0; S < W.Strands; ++S)
			{
				// (a pylon's three each side at its arms' three heights; a pole's side by side)
				const double Off = W.Strands == 1 ? 0.0 : W.High ? ((S % 2) * 2 - 1) * W.Spread * (0.5 + 0.12 * double(S / 2 == 1)) : (double(S) / (W.Strands - 1) - 0.5) * W.Spread;
				const double Drop = W.High ? -double(S / 2) * 3.2 : 0.0;
				V3 Last;
				for (int K = 0; K <= Steps; ++K)
				{
					const double T = double(K) / Steps;
					const V3 P = Pt(W.A.X + D.X * T + Across.X * Off, W.Ya + (W.Yb - W.Ya) * T - 4.0 * W.Sag * T * (1.0 - T) + Drop, W.A.Z + D.Z * T + Across.Z * Off);
					if (K > 0) Strut(Out, Last, P, Thick, WireGrey);
					Last = P;
				}
			}
		});
	}
	// The tall things: near, whole; far, plain.
	const auto Draw = [&](const TallThing& T)
	{
		const double Y = T.Ground, H = T.Height;
		const P2 F = T.Face, S(F.Z, -F.X);
		const bool Whole = Detail >= 1;
		switch (T.Kind)
		{
		case TallMast:
		{
			if (!Whole) { Strut(Out, Pt(T.At.X, Y, T.At.Z), Pt(T.At.X, Y + H, T.At.Z), 1.4, Galv); break; }
			// Three legs drawing in to the top, ringed and braced every 4 m, dishes and panels up top.
			const double R0 = std::fmax(1.2, H * 0.05), R1 = 0.5;
			V3 Leg[3][2];
			const int Rings = std::max(3, int(H / 4.0));
			for (int R = 0; R <= Rings; ++R)
			{
				const double Yr = Y + H * R / Rings, Rr = R0 + (R1 - R0) * R / Rings;
				V3 Now[3];
				for (int K = 0; K < 3; ++K)
				{
					const double A = 2.0944 * K + std::atan2(F.Z, F.X);
					Now[K] = Pt(T.At.X + std::cos(A) * Rr, Yr - (R == 0 ? 0.5 : 0.0), T.At.Z + std::sin(A) * Rr);
				}
				for (int K = 0; K < 3; ++K)
				{
					if (R > 0)
					{
						Strut(Out, Leg[K][0], Now[K], 0.12, Galv);
						Strut(Out, Leg[K][0], Now[(K + 1) % 3], 0.05, Galv);
					}
					if (R > 0) Strut(Out, Now[K], Now[(K + 1) % 3], 0.06, Galv);
					Leg[K][0] = Now[K];
				}
			}
			for (int K = 0; K < 3; ++K)
			{
				const double A = 2.0944 * K + 0.5;
				const V3 C = Pt(T.At.X + std::cos(A) * 0.9, Y + H * (0.82 + 0.06 * K), T.At.Z + std::sin(A) * 0.9);
				Column(Out, Pt(C.X, C.Y - 0.6, C.Z), 0.45, 0.45, 0.25, 10, Paint{0.55f, 0.55f, 0.53f});
			}
			Strut(Out, Pt(T.At.X, Y + H, T.At.Z), Pt(T.At.X, Y + H + 4.0, T.At.Z), 0.08, Galv);
			break;
		}
		case TallMonopole:
		{
			if (!Whole) { Strut(Out, Pt(T.At.X, Y, T.At.Z), Pt(T.At.X, Y + H, T.At.Z), 0.9, Galv); break; }
			Column(Out, Pt(T.At.X, Y - 0.3, T.At.Z), 0.42, 0.24, H + 0.3, 10, Galv);
			// (a triangle of antenna panels round the top)
			for (int K = 0; K < 3; ++K)
			{
				const double A = 2.0944 * K + std::atan2(F.Z, F.X);
				const double Cx = T.At.X + std::cos(A) * 0.9, Cz = T.At.Z + std::sin(A) * 0.9, Tx = -std::sin(A) * 0.15, Tz = std::cos(A) * 0.15;
				SkyQuad(Out, Pt(Cx - Tx, Y + H - 2.4, Cz - Tz), Pt(Cx + Tx, Y + H - 2.4, Cz + Tz), Pt(Cx + Tx, Y + H - 0.2, Cz + Tz), Pt(Cx - Tx, Y + H - 0.2, Cz - Tz), Paint{0.5f, 0.5f, 0.49f}, true);
				Strut(Out, Pt(T.At.X, Y + H - 1.2, T.At.Z), Pt(Cx, Y + H - 1.2, Cz), 0.06, Galv);
			}
			break;
		}
		case TallTurbine:
		{
			// The tower, the nacelle, and three blades standing still (at an angle of their own).
			const double Hub = Y + H, Blade = H * 0.45;
			const double Spin = Unit(T.Id, 5) * 2.0944;
			if (!Whole)
			{
				Strut(Out, Pt(T.At.X, Y, T.At.Z), Pt(T.At.X, Hub, T.At.Z), H * 0.06, Turbine);
				for (int K = 0; K < 3; ++K)
				{
					const double A = Spin + 2.0944 * K;
					Strut(Out, Pt(T.At.X + F.X * H * 0.06, Hub, T.At.Z + F.Z * H * 0.06),
						Pt(T.At.X + F.X * H * 0.06 + S.X * std::cos(A) * Blade, Hub + std::sin(A) * Blade, T.At.Z + F.Z * H * 0.06 + S.Z * std::cos(A) * Blade), H * 0.035, Turbine);
				}
				break;
			}
			Column(Out, Pt(T.At.X, Y - 0.5, T.At.Z), H * 0.035, H * 0.02, H + 0.5 - H * 0.03, 14, Turbine);
			const V3 Nose = Pt(T.At.X + F.X * H * 0.08, Hub, T.At.Z + F.Z * H * 0.08), Tail = Pt(T.At.X - F.X * H * 0.06, Hub, T.At.Z - F.Z * H * 0.06);
			Strut(Out, Tail, Nose, H * 0.055, Turbine);
			for (int K = 0; K < 3; ++K)
			{
				const double A = Spin + 2.0944 * K, Ca = std::cos(A), Sa = std::sin(A);
				// (a long, flat blade, widest near the hub)
				const auto At = [&](double Along, double Wide)
				{
					return Pt(Nose.X + S.X * (Ca * Along - Sa * Wide), Hub + Sa * Along + Ca * Wide, Nose.Z + S.Z * (Ca * Along - Sa * Wide));
				};
				const double Root = H * 0.04;
				SkyQuad(Out, At(0.5, -Root * 0.5), At(Blade * 0.25, -Root), At(Blade * 0.25, Root * 0.4), At(0.5, Root * 0.5), Turbine, true);
				SkyQuad(Out, At(Blade * 0.25, -Root), At(Blade, -Root * 0.15), At(Blade, Root * 0.1), At(Blade * 0.25, Root * 0.4), Turbine, true);
			}
			break;
		}
		case TallSilo:
		case TallTank:
		{
			const bool Silo = T.Kind == TallSilo;
			const Paint C = Silo ? (Unit(T.Id, 6) < 0.5 ? Galv : (Unit(T.Id, 7) < 0.5 ? SiloGreen : Concrete)) : Paint{0.36f, 0.38f, 0.35f};
			Column(Out, Pt(T.At.X, Y - 0.3, T.At.Z), T.Radius, T.Radius, H + 0.3, Whole ? 18 : 6, Silo ? Paint{float(C.R * 1.3f), float(C.G * 1.3f), float(C.B * 1.3f)} : C,
				Silo ? T.Radius * 0.45 : T.Radius * 0.05);
			break;
		}
		case TallWaterTower:
		{
			const double Legs = H * 0.65;
			if (Whole)
				for (int K = 0; K < 4; ++K)
				{
					const double A = 1.5708 * K + 0.785;
					Strut(Out, Pt(T.At.X + std::cos(A) * T.Radius * 0.8, Y - 0.3, T.At.Z + std::sin(A) * T.Radius * 0.8),
						Pt(T.At.X + std::cos(A) * T.Radius * 0.55, Y + Legs, T.At.Z + std::sin(A) * T.Radius * 0.55), 0.5, Concrete);
				}
			else Strut(Out, Pt(T.At.X, Y, T.At.Z), Pt(T.At.X, Y + Legs, T.At.Z), T.Radius, Concrete);
			Column(Out, Pt(T.At.X, Y + Legs, T.At.Z), T.Radius, T.Radius, H - Legs, Whole ? 16 : 6, Concrete, T.Radius * 0.2);
			break;
		}
		case TallPylon:
		{
			// A lattice tower: four legs drawn in to a waist, three cross-arms a side across its line
			// with their insulators hanging, and the earth wire's peak.
			if (!Whole)
			{
				SkyQuad(Out, Pt(T.At.X - S.X * H * 0.12, Y, T.At.Z - S.Z * H * 0.12), Pt(T.At.X + S.X * H * 0.12, Y, T.At.Z + S.Z * H * 0.12),
					Pt(T.At.X + S.X * 0.6, Y + H, T.At.Z + S.Z * 0.6), Pt(T.At.X - S.X * 0.6, Y + H, T.At.Z - S.Z * 0.6), Galv, true);
				Strut(Out, Pt(T.At.X - S.X * H * 0.24, Y + H * 0.76, T.At.Z - S.Z * H * 0.24), Pt(T.At.X + S.X * H * 0.24, Y + H * 0.76, T.At.Z + S.Z * H * 0.24), 0.6, Galv);
				break;
			}
			const double Base = H * 0.13, Waist = H * 0.035;
			const auto Corner = [&](int K, double Half, double Yc)
			{
				const double Su = (K == 0 || K == 3) ? -1.0 : 1.0, Sv = K < 2 ? -1.0 : 1.0;
				return Pt(T.At.X + S.X * Su * Half + F.X * Sv * Half, Yc, T.At.Z + S.Z * Su * Half + F.Z * Sv * Half);
			};
			const int Sections = 7;
			for (int R = 0; R < Sections; ++R)
			{
				const double Ya = Y + H * 0.9 * R / Sections, Yb = Y + H * 0.9 * (R + 1) / Sections;
				const double Ha = Base + (Waist - Base) * std::pow(double(R) / Sections, 0.7), Hb = Base + (Waist - Base) * std::pow(double(R + 1) / Sections, 0.7);
				for (int K = 0; K < 4; ++K)
				{
					Strut(Out, Corner(K, Ha, Ya - (R == 0 ? 0.5 : 0.0)), Corner(K, Hb, Yb), 0.18, Galv);
					Strut(Out, Corner(K, Ha, Ya), Corner((K + 1) % 4, Hb, Yb), 0.07, Galv);
					Strut(Out, Corner((K + 1) % 4, Ha, Ya), Corner(K, Hb, Yb), 0.07, Galv);
					Strut(Out, Corner(K, Hb, Yb), Corner((K + 1) % 4, Hb, Yb), 0.08, Galv);
				}
			}
			const double ArmY[3] = {0.62, 0.76, 0.9}, ArmL[3] = {0.21, 0.25, 0.21};
			for (int A = 0; A < 3; ++A)
				for (double Su : {-1.0, 1.0})
				{
					const double Ya = Y + H * ArmY[A], Reach = H * ArmL[A];
					const V3 Tip = Pt(T.At.X + S.X * Su * Reach, Ya, T.At.Z + S.Z * Su * Reach);
					for (double Sv : {-1.0, 1.0})
					{
						Strut(Out, Pt(T.At.X + S.X * Su * Waist + F.X * Sv * Waist, Ya, T.At.Z + S.Z * Su * Waist + F.Z * Sv * Waist), Tip, 0.12, Galv);
						Strut(Out, Pt(T.At.X + S.X * Su * Waist + F.X * Sv * Waist, Ya + H * 0.05, T.At.Z + S.Z * Su * Waist + F.Z * Sv * Waist), Tip, 0.07, Galv);
					}
					Strut(Out, Tip, Pt(Tip.X, Tip.Y - 2.6, Tip.Z), 0.16, Paint{0.2f, 0.21f, 0.22f});   // (the insulator string)
				}
			Strut(Out, Corner(0, Waist, Y + H * 0.9), Pt(T.At.X, Y + H, T.At.Z), 0.1, Galv);
			Strut(Out, Corner(2, Waist, Y + H * 0.9), Pt(T.At.X, Y + H, T.At.Z), 0.1, Galv);
			break;
		}
		case TallKiosk:
		{
			if (!Whole) break;
			SkyQuad(Out, Pt(T.At.X - S.X * 0.8 - F.X * 0.6, Y + H, T.At.Z - S.Z * 0.8 - F.Z * 0.6), Pt(T.At.X + S.X * 0.8 - F.X * 0.6, Y + H, T.At.Z + S.Z * 0.8 - F.Z * 0.6),
				Pt(T.At.X + S.X * 0.8 + F.X * 0.6, Y + H, T.At.Z + S.Z * 0.8 + F.Z * 0.6), Pt(T.At.X - S.X * 0.8 + F.X * 0.6, Y + H, T.At.Z - S.Z * 0.8 + F.Z * 0.6), KioskGreen, true);
			for (double Su : {-1.0, 1.0})
			{
				SkyQuad(Out, Pt(T.At.X + S.X * Su * 0.8 - F.X * 0.6, Y - 0.2, T.At.Z + S.Z * Su * 0.8 - F.Z * 0.6), Pt(T.At.X + S.X * Su * 0.8 + F.X * 0.6, Y - 0.2, T.At.Z + S.Z * Su * 0.8 + F.Z * 0.6),
					Pt(T.At.X + S.X * Su * 0.8 + F.X * 0.6, Y + H, T.At.Z + S.Z * Su * 0.8 + F.Z * 0.6), Pt(T.At.X + S.X * Su * 0.8 - F.X * 0.6, Y + H, T.At.Z + S.Z * Su * 0.8 - F.Z * 0.6), KioskGreen, true);
				SkyQuad(Out, Pt(T.At.X - S.X * 0.8 + F.X * Su * 0.6, Y - 0.2, T.At.Z - S.Z * 0.8 + F.Z * Su * 0.6), Pt(T.At.X + S.X * 0.8 + F.X * Su * 0.6, Y - 0.2, T.At.Z + S.Z * 0.8 + F.Z * Su * 0.6),
					Pt(T.At.X + S.X * 0.8 + F.X * Su * 0.6, Y + H, T.At.Z + S.Z * 0.8 + F.Z * Su * 0.6), Pt(T.At.X - S.X * 0.8 + F.X * Su * 0.6, Y + H, T.At.Z - S.Z * 0.8 + F.Z * Su * 0.6), KioskGreen, true);
			}
			break;
		}
		default: break;
		}
	};
	if (Detail >= 1) Each(Scene->Talls, [&](int I) { if (Inside(Talls[size_t(I)].At)) Draw(Talls[size_t(I)]); });
	else
		for (const TallThing& T : Talls)
		{
			if (!Inside(T.At)) continue;
			// (beyond 10 km only the turbines; nearer, everything taller than a tree)
			if (Detail < 0 ? T.Kind != TallTurbine : (T.Kind == TallKiosk || T.Height < 12.0)) continue;
			Draw(T);
		}
	if (Detail < 1) return;
	// The yards: a substation's palisade and its plant; a solar farm's rows of panels.
	Each(Scene->Yards, [&](int I)
	{
		const Yard& Y = Yards[size_t(I)];
		if (!Inside(Y.Mid)) return;
		NearOpts Opts;
		const auto Clear = [&](double X, double Z)
		{
			NearHit Hit;
			return !NearestOf(X, Z, Opts, Hit) || Hit.Dist > Hit.Half + 1.0;
		};
		double MinX = 1e18, MaxX = -1e18, MinZ = 1e18, MaxZ = -1e18;
		for (const P2& P : Y.Ring)
		{
			MinX = std::fmin(MinX, P.X); MaxX = std::fmax(MaxX, P.X);
			MinZ = std::fmin(MinZ, P.Z); MaxZ = std::fmax(MaxZ, P.Z);
		}
		if (Y.Solar)
		{
			// Rows east to west, 6 m apart, each panel 2.8 m deep tilted to face the south (+z), its
			// low edge 0.7 m up and its high one 2.1 m; a row broken where it leaves the outline.
			for (double Z = MinZ + 3.0; Z < MaxZ - 1.0; Z += 6.0)
			{
				double RunFrom = 0.0;
				bool Run = false;
				for (double X = MinX + 1.0; X <= MaxX + 2.0; X += 2.0)
				{
					const bool In = X <= MaxX && InRing(Y.Ring, X, Z) && Clear(X, Z);
					if (In && !Run) { RunFrom = X - 1.0; Run = true; }
					if ((!In || X + 2.0 > MaxX + 2.0) && Run)
					{
						const double Xa = RunFrom, Xb = In ? X + 1.0 : X - 1.0;
						const double Ga = Ground(Xa, Z), Gb = Ground(Xb, Z);
						if (!std::isnan(Ga) && !std::isnan(Gb) && Xb - Xa > 1.5)
						{
							SkyQuad(Out, Pt(Xa, Ga + 0.7, Z + 1.3), Pt(Xb, Gb + 0.7, Z + 1.3), Pt(Xb, Gb + 2.1, Z - 1.3), Pt(Xa, Ga + 2.1, Z - 1.3), Panel, true);
							for (double Xl = Xa + 0.5; Xl < Xb; Xl += 4.0)
							{
								const double Gl = Ga + (Gb - Ga) * (Xl - Xa) / (Xb - Xa);
								Strut(Out, Pt(Xl, Gl - 0.2, Z - 1.0), Pt(Xl, Gl + 1.95, Z - 1.0), 0.08, Galv);
							}
						}
						Run = false;
					}
				}
			}
			return;
		}
		// A substation: a palisade 2.4 m high round it, and transformers and gantries on a grid inside.
		for (size_t K = 0; K < Y.Ring.size(); ++K)
		{
			const P2 A = Y.Ring[K], B = Y.Ring[(K + 1) % Y.Ring.size()];
			const double L = A.DistanceTo(B);
			const int Posts = std::max(1, int(L / 2.5));
			for (int P = 0; P <= Posts; ++P)
			{
				const P2 Q = A + (B - A) * (double(P) / Posts);
				const double G = Ground(Q.X, Q.Z);
				if (std::isnan(G) || !Clear(Q.X, Q.Z)) continue;
				Strut(Out, Pt(Q.X, G - 0.2, Q.Z), Pt(Q.X, G + 2.4, Q.Z), 0.08, Fence);
			}
			const double Ga = Ground(A.X, A.Z), Gb = Ground(B.X, B.Z);
			if (std::isnan(Ga) || std::isnan(Gb) || !Clear(A.X, A.Z) || !Clear(B.X, B.Z)) continue;
			for (double H : {0.4, 2.1})
				Strut(Out, Pt(A.X, Ga + H, A.Z), Pt(B.X, Gb + H, B.Z), 0.06, Fence);
			// (the palisade's pales, close, seen as a dark screen)
			SkyQuad(Out, Pt(A.X, Ga + 0.1, A.Z), Pt(B.X, Gb + 0.1, B.Z), Pt(B.X, Gb + 2.3, B.Z), Pt(A.X, Ga + 2.3, A.Z), Paint{0.035f, 0.04f, 0.038f}, true);
		}
		int N = 0;
		for (double Z = MinZ + 4.0; Z < MaxZ - 3.0; Z += 7.0)
			for (double X = MinX + 4.0; X < MaxX - 3.0; X += 7.0)
			{
				if (!InRing(Y.Ring, X, Z) || !InRing(Y.Ring, X + 2.5, Z) || !InRing(Y.Ring, X - 2.5, Z)) continue;
				const double G = Ground(X, Z);
				if (std::isnan(G)) continue;
				++N;
				if (Unit(Y.Id, N) < 0.55)
				{
					// (a transformer: a grey box with its fins and three bushings)
					Column(Out, Pt(X, G, Z), 1.6, 1.6, 2.6, 4, Paint{0.3f, 0.31f, 0.3f});
					for (int B = -1; B <= 1; ++B) Strut(Out, Pt(X + B * 0.7, G + 2.6, Z), Pt(X + B * 0.7, G + 3.8, Z), 0.18, Paint{0.45f, 0.3f, 0.2f});
				}
				else
				{
					// (a gantry: two posts and a beam)
					for (double Su : {-2.5, 2.5}) Strut(Out, Pt(X + Su, G - 0.2, Z), Pt(X + Su, G + 6.5, Z), 0.25, Galv);
					Strut(Out, Pt(X - 2.5, G + 6.3, Z), Pt(X + 2.5, G + 6.3, Z), 0.3, Galv);
				}
			}
	});
}
} // namespace TorqueWorld
