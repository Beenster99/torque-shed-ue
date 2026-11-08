#include "BikeGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace TorqueFit
{
namespace
{
constexpr double GeoPi = 3.141592653589793;
constexpr double GeoEps = 0.00001;   // Godot's CMP_EPSILON

double GeoSnapped(double V, double Step) { return Step != 0.0 ? std::floor(V / Step + 0.5) * Step : V; }
double GeoWrap(double V, double Lo, double Hi)
{
	const double Range = Hi - Lo;
	return std::fabs(Range) < GeoEps ? Lo : V - Range * std::floor((V - Lo) / Range);
}
bool GeoZero(double V) { return std::fabs(V) < GeoEps; }
bool GeoEqual(double A, double B)
{
	if (A == B) return true;
	return std::fabs(A - B) < std::max(GeoEps * std::fabs(A), GeoEps);
}
double& GeoAt(V3& V, int I) { return I == 0 ? V.x : (I == 1 ? V.y : V.z); }
double GeoGet(const V3& V, int I) { return I == 0 ? V.x : (I == 1 ? V.y : V.z); }
V3 GeoUnit(int Axis) { V3 U; GeoAt(U, Axis) = 1.0; return U; }
bool GeoFinite(const V3& V) { return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z); }
V3 GeoClampV(const V3& V, double Lo, double Hi)
{
	return V3(TorqueSim::Clamp(V.x, Lo, Hi), TorqueSim::Clamp(V.y, Lo, Hi), TorqueSim::Clamp(V.z, Lo, Hi));
}
double SnapStep(GeoHandle H, GeoSnap Snap)
{
	if (Snap == GeoSnap::Off) return 0.0;
	const bool Fine = Snap == GeoSnap::Fine;
	switch (H)
	{
	case GeoHandle::Move: return Fine ? Geo::FineMove : Geo::SnapMove;
	case GeoHandle::Rotate: return Fine ? Geo::FineRot : Geo::SnapRot;
	default: return Fine ? Geo::FineScale : Geo::SnapScale;
	}
}
// A basis's inverse transpose (for normals): columns Y x Z, Z x X, X x Y (scaled alike).
GAffine NormalBasis(const GAffine& B)
{
	GAffine N;
	N.X = B.Y.Cross(B.Z);
	N.Y = B.Z.Cross(B.X);
	N.Z = B.X.Cross(B.Y);
	return N;
}
// "%.17g": the number exactly, read back as it was.
void GeoNum(std::string& Out, double V)
{
	char Buf[40];
	std::snprintf(Buf, sizeof(Buf), "%.17g", V);
	Out += Buf;
}
void GeoVec(std::string& Out, const V3& V)
{
	Out += "[";
	GeoNum(Out, V.x);
	Out += ", ";
	GeoNum(Out, V.y);
	Out += ", ";
	GeoNum(Out, V.z);
	Out += "]";
}
bool GeoVecOf(const Json* J, V3& Out)
{
	if (!J || !J->IsArray() || J->Size() != 3) return false;
	for (int I = 0; I < 3; ++I)
	{
		const Json& E = J->A[static_cast<size_t>(I)];
		if (!E.IsNumber()) return false;
		GeoAt(Out, I) = E.N;
	}
	return true;
}
} // namespace

bool GeoOffset::IsIdentity() const
{
	return GeoZero(P.x) && GeoZero(P.y) && GeoZero(P.z) && GeoZero(R.x) && GeoZero(R.y) && GeoZero(R.z) && GeoEqual(S.x, 1.0) && GeoEqual(S.y, 1.0)
		&& GeoEqual(S.z, 1.0);
}

bool GeoOffset::operator==(const GeoOffset& O) const
{
	return P.x == O.P.x && P.y == O.P.y && P.z == O.P.z && R.x == O.R.x && R.y == O.R.y && R.z == O.R.z && S.x == O.S.x && S.y == O.S.y && S.z == O.S.z;
}

bool BikeGeo::Empty() const
{
	const LayoutAdjust A = Rig.Clean();
	return Slots.empty() && A.TankUpMm == 0.0 && A.PegsMm == 0.0 && A.HoopUpMm == 0.0 && A.HoopDeg == 0.0;
}

bool BikeGeo::operator==(const BikeGeo& O) const
{
	return Rig.TankUpMm == O.Rig.TankUpMm && Rig.PegsMm == O.Rig.PegsMm && Rig.HoopUpMm == O.Rig.HoopUpMm && Rig.HoopDeg == O.Rig.HoopDeg && Slots == O.Slots;
}

GeoOffset BikeGeo::Of(const std::string& Slot) const
{
	const auto It = Slots.find(Slot);
	return It != Slots.end() ? It->second : GeoOffset();
}

void BikeGeo::Set(const std::string& Slot, const GeoOffset& Off)
{
	if (Off.IsIdentity()) Slots.erase(Slot);
	else Slots[Slot] = Off;
}

namespace Geo
{
const std::vector<std::string>& Slots()
{
	static const std::vector<std::string> List = {"frame", "fork", "shock", "front_wheel", "rear_wheel", "front_tyre", "rear_tyre", "front_disc", "rear_disc",
		"front_caliper", "rear_caliper", "front_pads", "rear_pads", "front_master", "rear_master", "tank", "saddle", "handlebars", "dashboard", "engine", "exhaust",
		"final_drive", "chain", "side_stand", "centre_stand", "headlight", "horn", "ecu", "panniers", "top_box", "fog_lights", "tail", "fairing", "mirrors", "head",
		"fuel", "air_filter", "oil_filter", "spark_plugs", "cooling", "gearbox", "front_mudguard", "indicators", "side_panels", "rearsets", "guards", "number_boards",
		"swingarm", "steering_damper", "tank_bag", "aux_tank", "toolbox", "satnav", "lever_guards"};
	return List;
}

GAffine FromEuler(const V3& E)
{
	// (Basis.from_euler, YXZ: the turn about Y, then X, then Z, as Y * X * Z.)
	const GAffine X = GAffine::Rotation(V3(1, 0, 0), E.x), Y = GAffine::Rotation(V3(0, 1, 0), E.y), Z = GAffine::Rotation(V3(0, 0, 1), E.z);
	return Y * X * Z;
}

V3 EulerOf(const GAffine& B)
{
	// (Basis.get_euler, YXZ; rows[i][j] is column j's component i.)
	const double M12 = B.Z.y;
	V3 E;
	if (M12 < 1.0 - GeoEps)
	{
		if (M12 > -(1.0 - GeoEps))
		{
			if (B.X.y == 0.0 && B.Y.x == 0.0 && B.Z.x == 0.0 && B.X.z == 0.0 && B.X.x == 1.0)
				E = V3(std::atan2(-M12, B.Y.y), 0.0, 0.0);
			else
				E = V3(std::asin(-M12), std::atan2(B.Z.x, B.Z.z), std::atan2(B.X.y, B.Y.y));
		}
		else
			E = V3(GeoPi * 0.5, std::atan2(B.Y.x, B.X.x), 0.0);
	}
	else
		E = V3(-GeoPi * 0.5, -std::atan2(B.Y.x, B.X.x), 0.0);
	return E;
}

GAffine RotationOf(const GeoOffset& O) { return FromEuler(O.R * (GeoPi / 180.0)); }

GAffine OffsetXform(const GeoOffset& O, const V3& Pivot)
{
	GAffine B = RotationOf(O) * GAffine::Scale(O.S);
	B.O = Pivot + O.P;
	return B * GAffine::At(-Pivot);
}

GeoOffset Moved(const GeoOffset& Start, int Axis, double Amount, GeoSnap Snap)
{
	GeoOffset O = Start;
	double V = GeoGet(O.P, Axis) + Amount;
	if (Snap != GeoSnap::Off) V = GeoSnapped(V, SnapStep(GeoHandle::Move, Snap));
	GeoAt(O.P, Axis) = TorqueSim::Clamp(V, -MaxMove, MaxMove);
	return O;
}

GeoOffset Rotated(const GeoOffset& Start, int Axis, double Degrees, GeoSnap Snap)
{
	GeoOffset O = Start;
	if (Snap != GeoSnap::Off) Degrees = GeoSnapped(Degrees, SnapStep(GeoHandle::Rotate, Snap));
	const GAffine Turn = GAffine::Rotation(GeoUnit(Axis), Degrees * (GeoPi / 180.0)) * RotationOf(O);
	V3 E = EulerOf(Turn) * (180.0 / GeoPi);
	// (Rounding noise tidied, so an untouched axis reads exactly 0.)
	for (int I = 0; I < 3; ++I)
	{
		double& C = GeoAt(E, I);
		C = GeoSnapped(C, 0.001);
		if (std::fabs(C) < 0.001) C = 0.0;
	}
	O.R = E;
	return O;
}

GeoOffset Scaled(const GeoOffset& Start, int Axis, double Factor, GeoSnap Snap)
{
	GeoOffset O = Start;
	for (int I = 0; I < 3; ++I)
		if (Axis == Uniform || Axis == I)
		{
			double V = GeoGet(O.S, I) * Factor;
			if (Snap != GeoSnap::Off) V = GeoSnapped(V, SnapStep(GeoHandle::Scale, Snap));
			GeoAt(O.S, I) = TorqueSim::Clamp(V, MinScale, MaxScale);
		}
	return O;
}

V3 PartAxis(const GeoOffset& O, int Axis)
{
	const GAffine R = RotationOf(O);
	return (Axis == 0 ? R.X : (Axis == 1 ? R.Y : R.Z)).Normalized();
}

GeoOffset MovedAlong(const GeoOffset& Start, const V3& Dir, double Amount, GeoSnap Snap)
{
	GeoOffset O = Start;
	if (Snap != GeoSnap::Off) Amount = GeoSnapped(Amount, SnapStep(GeoHandle::Move, Snap));
	O.P = GeoClampV(O.P + Dir.Normalized() * Amount, -MaxMove, MaxMove);
	return O;
}

GeoOffset Nudged(const GeoOffset& Start, GeoHandle Handle, int Axis, int Steps, bool Fine, bool PartAxes)
{
	const double Step = SnapStep(Handle, Fine ? GeoSnap::Fine : GeoSnap::Coarse) * static_cast<double>(Steps);
	switch (Handle)
	{
	case GeoHandle::Move: return PartAxes ? MovedAlong(Start, PartAxis(Start, Axis), Step, GeoSnap::Off) : Moved(Start, Axis, Step, GeoSnap::Off);
	case GeoHandle::Rotate: return Rotated(Start, Axis, Step, GeoSnap::Off);
	default:
	{
		// A step is a share of the size it has now (1% or 5% of it), for the axis or all three.
		GeoOffset O = Start;
		for (int I = 0; I < 3; ++I)
			if (Axis == Uniform || Axis == I) GeoAt(O.S, I) = TorqueSim::Clamp(GeoGet(O.S, I) + Step, MinScale, MaxScale);
		return O;
	}
	}
}

GeoOffset WithValue(const GeoOffset& Start, GeoHandle Handle, int Axis, double Value)
{
	GeoOffset O = Start;
	if (!std::isfinite(Value)) return O;
	switch (Handle)
	{
	case GeoHandle::Move: GeoAt(O.P, Axis) = TorqueSim::Clamp(Value / 100.0, -MaxMove, MaxMove); break;
	case GeoHandle::Rotate: GeoAt(O.R, Axis) = GeoWrap(Value, -180.0, 180.0); break;
	default:
		for (int I = 0; I < 3; ++I)
			if (Axis == Uniform || Axis == I) GeoAt(O.S, I) = TorqueSim::Clamp(Value / 100.0, MinScale, MaxScale);
		break;
	}
	return O;
}

BikeGeo Sanitize(const BikeGeo& In)
{
	BikeGeo Out;
	Out.Rig = In.Rig.Clean();
	const std::vector<std::string>& Known = Slots();
	for (const auto& [Slot, Off] : In.Slots)
	{
		if (std::find(Known.begin(), Known.end(), Slot) == Known.end()) continue;
		if (!(GeoFinite(Off.P) && GeoFinite(Off.R) && GeoFinite(Off.S))) continue;
		GeoOffset C;
		C.P = GeoClampV(Off.P, -MaxMove, MaxMove);
		C.R = V3(GeoWrap(Off.R.x, -180.0, 180.0), GeoWrap(Off.R.y, -180.0, 180.0), GeoWrap(Off.R.z, -180.0, 180.0));
		C.S = GeoClampV(Off.S, MinScale, MaxScale);
		if (!C.IsIdentity()) Out.Slots[Slot] = C;
	}
	return Out;
}

std::string ToText(const BikeGeo& In)
{
	const BikeGeo G = Sanitize(In);
	std::string Out = "{";
	bool First = true;
	auto Key = [&](const std::string& K) {
		if (!First) Out += ", ";
		First = false;
		Out += "\"" + K + "\": ";
	};
	const std::pair<const char*, double> Rig[] = {{"tank_up", G.Rig.TankUpMm}, {"pegs", G.Rig.PegsMm}, {"hoop_up", G.Rig.HoopUpMm}, {"hoop_deg", G.Rig.HoopDeg}};
	std::string RigText;
	for (const auto& [K, V] : Rig)
	{
		if (V == 0.0) continue;
		RigText += RigText.empty() ? "{" : ", ";
		RigText += std::string("\"") + K + "\": ";
		GeoNum(RigText, V);
	}
	if (!RigText.empty())
	{
		Key("rig");
		Out += RigText + "}";
	}
	for (const auto& [Slot, O] : G.Slots)
	{
		Key(Slot);
		Out += "{\"p\": ";
		GeoVec(Out, O.P);
		Out += ", \"r\": ";
		GeoVec(Out, O.R);
		Out += ", \"s\": ";
		GeoVec(Out, O.S);
		Out += "}";
	}
	return Out + "}";
}

bool FromText(const std::string& Text, BikeGeo& Out)
{
	Out = BikeGeo();
	Json J;
	if (!Json::Parse(Text, J) || !J.IsObject()) return false;
	BikeGeo G;
	if (const Json* Rig = J.Get("rig"))
	{
		G.Rig.TankUpMm = Rig->Num("tank_up", 0.0);
		G.Rig.PegsMm = Rig->Num("pegs", 0.0);
		G.Rig.HoopUpMm = Rig->Num("hoop_up", 0.0);
		G.Rig.HoopDeg = Rig->Num("hoop_deg", 0.0);
	}
	for (const std::string& Slot : Slots())
	{
		const Json* S = J.Get(Slot);
		if (!S || !S->IsObject()) continue;
		GeoOffset O;
		GeoVecOf(S->Get("p"), O.P);
		GeoVecOf(S->Get("r"), O.R);
		GeoVecOf(S->Get("s"), O.S);
		G.Slots[Slot] = O;
	}
	Out = Sanitize(G);
	return true;
}

double LineParam(const V3& Origin, const V3& Dir, const V3& RayO, const V3& RayD)
{
	const V3 D = Dir.Normalized(), R = RayD.Normalized();
	const V3 W = Origin - RayO;
	const double B = D.Dot(R);
	const double Denom = 1.0 - B * B;
	if (Denom < 1e-6) return 0.0;   // looking straight down the axis: no sensible drag
	return (B * R.Dot(W) - D.Dot(W)) / Denom;
}

bool PlanePoint(const V3& Center, const V3& Axis, const V3& RayO, const V3& RayD, V3& Hit)
{
	// (Plane(normal, point).intersects_ray: a ray, so only ahead of RayO.)
	const V3 N = Axis.Normalized();
	const double D = N.Dot(Center);
	const double Den = N.Dot(RayD);
	if (GeoZero(Den)) return false;
	double Dist = (N.Dot(RayO) - D) / Den;
	if (Dist > GeoEps) return false;
	Dist = -Dist;
	Hit = RayO + RayD * Dist;
	return true;
}

double AngleAbout(const V3& A, const V3& B, const V3& Axis)
{
	const V3 C = A.Cross(B);
	const double Unsigned = std::atan2(C.Length(), A.Dot(B));
	const double Sign = C.Dot(Axis.Normalized());
	return (Sign < 0.0 ? -Unsigned : Unsigned) * (180.0 / GeoPi);
}

std::string Readout(const GeoOffset& O)
{
	char Buf[256];
	std::snprintf(Buf, sizeof(Buf), "Move    X %+6.1f  Y %+6.1f  Z %+6.1f cm\nRotate  X %+6.1f  Y %+6.1f  Z %+6.1f deg\nScale   X %5.0f  Y %5.0f  Z %5.0f %%",
		O.P.x * 100.0, O.P.y * 100.0, O.P.z * 100.0, O.R.x, O.R.y, O.R.z, O.S.x * 100.0, O.S.y * 100.0, O.S.z * 100.0);
	return Buf;
}
} // namespace Geo

std::string GeoPivots::SlotOf(const FittedPart& F, const PartSurface& S)
{
	const size_t Cut = S.Material.find("__");
	return Cut != std::string::npos ? S.Material.substr(Cut + 2) : F.Slot;
}

void GeoPivots::Add(const FittedPart& F, const PartMesh& Drawn)
{
	for (const PartSurface& S : Drawn.Surfaces)
	{
		if (S.Pos.empty()) continue;
		const std::string Slot = SlotOf(F, S);
		auto It = Box.find(Slot);
		for (const V3& P : S.Pos)
		{
			const V3 G = ToGodot(P);
			if (It == Box.end())
			{
				It = Box.emplace(Slot, Bounds{G, G}).first;
				continue;
			}
			Bounds& B = It->second;
			B.Lo = V3(std::min(B.Lo.x, G.x), std::min(B.Lo.y, G.y), std::min(B.Lo.z, G.z));
			B.Hi = V3(std::max(B.Hi.x, G.x), std::max(B.Hi.y, G.y), std::max(B.Hi.z, G.z));
		}
	}
}

V3 GeoPivots::Pivot(const std::string& Slot) const
{
	const auto It = Box.find(Slot);
	return It != Box.end() ? (It->second.Lo + It->second.Hi) * 0.5 : V3();
}

GAffine GeoPivots::Xform(const BikeGeo& G, const std::string& Slot) const
{
	// (Godot frame to ours: ours = FromGodot(T(ToGodot(p))), as columns.)
	const GAffine T = Geo::OffsetXform(G.Of(Slot), Pivot(Slot));
	GAffine O;
	const V3 C[3] = {T.Apply(V3(0, 0, -1)) - T.O, T.Apply(V3(0, 1, 0)) - T.O, T.Apply(V3(1, 0, 0)) - T.O};   // ours x, y, z in Godot
	O.X = FromGodot(C[0]);
	O.Y = FromGodot(C[1]);
	O.Z = FromGodot(C[2]);
	O.O = FromGodot(T.O);
	return O;
}

void GeoPivots::Bake(const BikeGeo& G, const FittedPart& F, PartMesh& Drawn) const
{
	for (PartSurface& S : Drawn.Surfaces)
	{
		const std::string Slot = SlotOf(F, S);
		const auto It = G.Slots.find(Slot);
		if (It == G.Slots.end() || It->second.IsIdentity()) continue;
		const GAffine T = Xform(G, Slot);
		const GAffine N = NormalBasis(T);
		for (V3& P : S.Pos) P = T.Apply(P);
		for (V3& Nr : S.Nrm) Nr = (N.X * Nr.x + N.Y * Nr.y + N.Z * Nr.z).Normalized();
		// (A mirroring size, never made by the clamps, would turn the faces inside out: none here.)
	}
}

namespace
{
// The nearest point of triangle ABC to P (Ericson, Real-Time Collision Detection 5.1.5).
V3 GeoClosestOnTri(const V3& P, const V3& A, const V3& B, const V3& C)
{
	const V3 Ab = B - A, Ac = C - A, Ap = P - A;
	const double D1 = Ab.Dot(Ap), D2 = Ac.Dot(Ap);
	if (D1 <= 0.0 && D2 <= 0.0) return A;
	const V3 Bp = P - B;
	const double D3 = Ab.Dot(Bp), D4 = Ac.Dot(Bp);
	if (D3 >= 0.0 && D4 <= D3) return B;
	const double Vc = D1 * D4 - D3 * D2;
	if (Vc <= 0.0 && D1 >= 0.0 && D3 <= 0.0) return A + Ab * (D1 / (D1 - D3));
	const V3 Cp = P - C;
	const double D5 = Ab.Dot(Cp), D6 = Ac.Dot(Cp);
	if (D6 >= 0.0 && D5 <= D6) return C;
	const double Vb = D5 * D2 - D1 * D6;
	if (Vb <= 0.0 && D2 >= 0.0 && D6 <= 0.0) return A + Ac * (D2 / (D2 - D6));
	const double Va = D3 * D6 - D5 * D4;
	if (Va <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0) return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
	const double Den = 1.0 / (Va + Vb + Vc);
	return A + Ab * (Vb * Den) + Ac * (Vc * Den);
}

struct GeoTri { V3 A, B, C; };

// Triangles bucketed on a grid, so a point only looks at those near it.
struct GeoGrid
{
	double Cell = 0.03;
	std::unordered_map<long long, std::vector<size_t>> Cells;
	std::vector<GeoTri> Tris;
	std::vector<V3> Pts;
	static long long KeyOf(long long X, long long Y, long long Z) { return (X * 73856093LL) ^ (Y * 19349663LL) ^ (Z * 83492791LL); }
	long long Ix(double V) const { return static_cast<long long>(std::floor(V / Cell)); }
	void Add(const GeoTri& T)
	{
		const size_t I = Tris.size();
		Tris.push_back(T);
		const V3 Lo(std::min({T.A.x, T.B.x, T.C.x}), std::min({T.A.y, T.B.y, T.C.y}), std::min({T.A.z, T.B.z, T.C.z}));
		const V3 Hi(std::max({T.A.x, T.B.x, T.C.x}), std::max({T.A.y, T.B.y, T.C.y}), std::max({T.A.z, T.B.z, T.C.z}));
		for (long long X = Ix(Lo.x); X <= Ix(Hi.x); ++X)
			for (long long Y = Ix(Lo.y); Y <= Ix(Hi.y); ++Y)
				for (long long Z = Ix(Lo.z); Z <= Ix(Hi.z); ++Z) Cells[KeyOf(X, Y, Z)].push_back(I);
	}
	// The nearest of its triangles to P, if one is within Within (else Within).
	double Nearest(const V3& P, double Within) const
	{
		double Best = Within;
		const long long X = Ix(P.x), Y = Ix(P.y), Z = Ix(P.z);
		const long long Rc = static_cast<long long>(std::ceil(Within / Cell));   // cells out to Within
		for (long long Dx = -Rc; Dx <= Rc; ++Dx)
			for (long long Dy = -Rc; Dy <= Rc; ++Dy)
				for (long long Dz = -Rc; Dz <= Rc; ++Dz)
				{
					const auto It = Cells.find(KeyOf(X + Dx, Y + Dy, Z + Dz));
					if (It == Cells.end()) continue;
					for (size_t I : It->second)
					{
						const GeoTri& T = Tris[I];
						Best = std::min(Best, (P - GeoClosestOnTri(P, T.A, T.B, T.C)).Length());
					}
				}
		return Best;
	}
};
} // namespace

std::vector<GeoGap> Gaps(const std::vector<std::pair<const FittedPart*, const PartMesh*>>& Meshes, const std::string& Slot, double Within)
{
	// Each slot's surfaces and box; only those whose box comes within reach of the moved slot's
	// are put on a grid of their own and measured, both ways (a big flat face of either has few
	// corners near the other).
	struct Of { std::vector<const PartSurface*> Surfs; V3 Lo{1e30, 1e30, 1e30}, Hi{-1e30, -1e30, -1e30}; };
	std::map<std::string, Of> Slots;
	for (const auto& [F, M] : Meshes)
	{
		if (!F || !M) continue;
		for (const PartSurface& S : M->Surfaces)
		{
			if (S.Pos.empty()) continue;
			Of& O = Slots[GeoPivots::SlotOf(*F, S)];
			O.Surfs.push_back(&S);
			for (const V3& P : S.Pos)
			{
				O.Lo = V3(std::min(O.Lo.x, P.x), std::min(O.Lo.y, P.y), std::min(O.Lo.z, P.z));
				O.Hi = V3(std::max(O.Hi.x, P.x), std::max(O.Hi.y, P.y), std::max(O.Hi.z, P.z));
			}
		}
	}
	std::vector<GeoGap> Out;
	const auto Mine = Slots.find(Slot);
	if (Mine == Slots.end()) return Out;
	const double Cell = std::max(Within, 0.005);
	// (Only what's inside the box Lo..Hi goes on: nothing outside it can be the nearest.)
	auto GridOf = [Cell](const Of& O, const V3& BLo, const V3& BHi) {
		auto In = [&](const V3& P) { return P.x >= BLo.x && P.x <= BHi.x && P.y >= BLo.y && P.y <= BHi.y && P.z >= BLo.z && P.z <= BHi.z; };
		GeoGrid G;
		G.Cell = Cell;
		for (const PartSurface* S : O.Surfs)
		{
			for (const V3& P : S->Pos)
				if (In(P)) G.Pts.push_back(P);
			for (size_t K = 0; K + 2 < S->Idx.size(); K += 3)
			{
				const V3 A = S->Pos[S->Idx[K]], B = S->Pos[S->Idx[K + 1]], C = S->Pos[S->Idx[K + 2]];
				if (std::max({A.x, B.x, C.x}) < BLo.x || std::min({A.x, B.x, C.x}) > BHi.x || std::max({A.y, B.y, C.y}) < BLo.y || std::min({A.y, B.y, C.y}) > BHi.y
					|| std::max({A.z, B.z, C.z}) < BLo.z || std::min({A.z, B.z, C.z}) > BHi.z)
					continue;
				G.Add({A, B, C});
			}
		}
		return G;
	};
	const V3 Reach(Within, Within, Within);
	const V3 Lo = Mine->second.Lo - Reach, Hi = Mine->second.Hi + Reach;
	const GeoGrid MyGrid = GridOf(Mine->second, Mine->second.Lo - Reach, Mine->second.Hi + Reach);
	for (const auto& [Name, O] : Slots)
	{
		if (Name == Slot || O.Hi.x < Lo.x || O.Lo.x > Hi.x || O.Hi.y < Lo.y || O.Lo.y > Hi.y || O.Hi.z < Lo.z || O.Lo.z > Hi.z) continue;
		const GeoGrid G = GridOf(O, Lo, Hi);
		double Best = Within;
		// (A corner further than Best from the other's box can't be nearer; touching ends it.)
		auto Near = [&Best](const V3& P, const V3& BLo, const V3& BHi) {
			return P.x > BLo.x - Best && P.x < BHi.x + Best && P.y > BLo.y - Best && P.y < BHi.y + Best && P.z > BLo.z - Best && P.z < BHi.z + Best;
		};
		for (const V3& P : MyGrid.Pts)
			if (Best > 0.0 && Near(P, O.Lo, O.Hi)) Best = std::min(Best, G.Nearest(P, Best));
		for (const V3& P : G.Pts)
			if (Best > 0.0 && Near(P, Mine->second.Lo, Mine->second.Hi)) Best = std::min(Best, MyGrid.Nearest(P, Best));
		if (Best < Within) Out.push_back({Name, Best});
	}
	std::sort(Out.begin(), Out.end(), [](const GeoGap& A, const GeoGap& B) { return A.Gap != B.Gap ? A.Gap < B.Gap : A.Slot < B.Slot; });
	return Out;
}
} // namespace TorqueFit
