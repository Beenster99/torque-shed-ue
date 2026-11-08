#include "BikeBody.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TorqueFit
{

namespace
{
// RigBodywork's constants (rig_bodywork.gd).
constexpr double KneeAt0 = 0.58, KneeAt1 = 0.84;
constexpr double BaseTunnelHalf = 0.035, BaseTunnelDeep = 0.035;
constexpr double TunnelSink = 0.08, FlankSkirt = 0.06;
constexpr double HeadClear = 0.035, HeadLong = 0.08;
constexpr double DeepSkirt = 0.12, OverlapUnder = 0.16, TunnelSpare = 0.006;
constexpr double SeatNoseW = 0.06, SeatNoseH = 0.04, SeatFoam = 0.04;
constexpr double CowlFrontW = 0.05;
constexpr double CowlHipLead = 0.2;
const double NoLimit = std::numeric_limits<double>::infinity();

double TankKnee(const std::string& Shape)
{
	static const std::map<std::string, double> K = {{"sport", 1.0}, {"race", 1.0}, {"wedge", 1.0}, {"streetfighter", 1.0}, {"gt", 0.8},
		{"adv", 0.8}, {"touring", 0.7}, {"cafe", 0.8}, {"enduro", 0.5}, {"teardrop", 0.35}};
	auto It = K.find(Shape);
	return It == K.end() ? 0.0 : It->second;
}

double Nth(const Json& Arr, size_t I, double Default = 0.0)
{
	return (Arr.IsArray() && I < Arr.A.size() && Arr.A[I].IsNumber()) ? Arr.A[I].N : Default;
}

// A profile key list's values at u (BikeModel._key_at, _seat_key_at): Width values after the share.
std::vector<double> KeysAt(const Json& Keys, double U, size_t Width)
{
	const size_t N = Keys.A.size();
	std::vector<double> Out(Width);
	for (size_t I = 0; I + 1 < N; ++I)
	{
		const Json& K0 = Keys.A[I];
		const Json& K1 = Keys.A[I + 1];
		if (U <= Nth(K1, 0))
		{
			double F = (U - Nth(K0, 0)) / std::max(Nth(K1, 0) - Nth(K0, 0), 0.0001);
			F = F * F * (3.0 - 2.0 * F);
			for (size_t C = 0; C < Width; ++C) Out[C] = TorqueSim::Lerp(Nth(K0, C + 1), Nth(K1, C + 1), F);
			return Out;
		}
	}
	for (size_t C = 0; C < Width; ++C) Out[C] = Nth(Keys.A[N - 1], C + 1);
	return Out;
}

// A polyline running back along +z (Godot frame), its point and direction at z, carried straight on
// past its ends (RigBodywork.line_at).
struct OnLine { V3 P, D; };
OnLine LineAt(const std::vector<V3>& Line, double Z)
{
	const size_t N = Line.size();
	size_t K = 1;
	while (K < N - 1 && Line[K].z < Z) ++K;
	const V3 A = Line[K - 1], B = Line[K];
	const V3 D = B - A;
	if (std::fabs(D.z) < 0.00001) return {A, V3(0, 0, 1)};
	double F = (Z - A.z) / D.z;
	if (K > 1 && K < N - 1) F = TorqueSim::Clamp(F, 0.0, 1.0);
	return {A + D * F, D.Normalized()};
}

V3 UpFrom(const V3& Dir)
{
	const V3 Up(0.0, Dir.z, -Dir.y);
	return Up.y >= 0.0 ? Up.Normalized() : -Up.Normalized();
}

double SmoothStep01(double A, double B, double X)
{
	return TorqueSim::SmoothStep(A, B, X);
}

V3 LookVec(const Json* Part, const char* Key, const V3& Default)
{
	const Json* Look = Part ? Part->Get("look") : nullptr;
	const Json* V = Look ? Look->Get(Key) : nullptr;
	if (!V || !V->IsArray() || V->A.size() < 3) return Default;
	return V3(Nth(*V, 0, Default.x), Nth(*V, 1, Default.y), Nth(*V, 2, Default.z));
}

std::string LookStr(const Json* Part, const char* Key)
{
	const Json* Look = Part ? Part->Get("look") : nullptr;
	return Look ? Look->Str(Key) : std::string();
}

// A warp written in the Godot frame (the glb's point and the bike's, as rig_bodywork.gd), wrapped to
// take and give ours.
PartMesh::Warp InOurs(std::function<V3(const V3&)> GodotWarp)
{
	return [GodotWarp](const V3& Ours) { return FromGodot(GodotWarp(ToGodot(Ours))); };
}

std::string SlotPart(const PartsMap& P, const std::string& Slot)
{
	auto It = P.find(Slot);
	return It == P.end() || It->second.empty() ? std::string("none") : It->second;
}
} // namespace

Bodywork BikeBody::Fit(const PartsMap& Parts, const Layout& L) const
{
	Bodywork Out;
	const Json& T = Lay.Tables();
	// The layout's lines back in the Godot frame, as RigBodywork reads them.
	std::vector<V3> Floor;
	for (const V3& P : L.TankFloor) Floor.push_back(ToGodot(P));
	if (Floor.size() < 2) return Out;
	const V3 Nose = ToGodot(L.TankNose), Rear = ToGodot(L.TankRear);
	const V3 Crank = ToGodot(L.Crank);
	std::vector<V3> Tops;
	for (const V3& P : L.Hard.HeadTops) Tops.push_back(ToGodot(P));

	// ------------------------------------------------------------------ tank
	const Json* Tank = Lay.Part(SlotPart(Parts, "tank"));
	if (Tank)
	{
		const Json* Shapes = T.Get("tank_shapes");
		const std::string Style = Tank->Str("style", "cafe");
		const std::string Key = LookStr(Tank, "shape").empty() ? Style : LookStr(Tank, "shape");
		const std::string Name = Shapes->Get(Key) ? Key : (Shapes->Get(Style) ? Style : "cafe");
		const Json* Shape = Shapes->Get(Name);
		const Json& Keys = *(Shape->Get("rig_keys") ? Shape->Get("rig_keys") : Shape->Get("keys"));
		const double Size = std::pow(Tank->Num("capacity", 15.0) / 15.0, 0.33);
		const V3 Sc = LookVec(Tank, "scale", V3(1, 1, 1));
		const double Len = Shape->Num("len", 0.6);
		double Back = Rear.z + 0.03;
		if (L.SeatOverlap)
		{
			const double Want = Len * TorqueSim::Lerp(0.85, 1.0, Size) * Sc.x;
			Back = TorqueSim::Clamp(Nose.z + 0.03 + Want, Back, Back + OverlapUnder);
		}
		const double Room = Back - (Nose.z + 0.03);
		const double Length = std::min(TorqueSim::Clamp(Len * TorqueSim::Lerp(0.85, 1.0, Size) * Sc.x, 0.25, std::max(Room, 0.25)), Room);
		const double Front = Back - Length;
		const double Kx = Size * Sc.y;
		double Ky = Size * Sc.z;
		const double HalfW = L.TunnelHalfW;
		double Skirt = std::max(2.0 * (HalfW - 0.01) + 0.012 - BaseTunnelDeep * Ky, 0.0);
		auto Tallest = [&](double K) {
			double M = 0.0;
			for (const Json& Kk : Keys.A) M = std::max(M, (Nth(Kk, 2) + Nth(Kk, 3)) * K);
			return M;
		};
		double Sink = 0.0;
		{
			double Over = 0.0;
			const double Tall = Tallest(Ky);
			for (const Json& Kk : Keys.A)
			{
				const V3 At = LineAt(Floor, Front + Nth(Kk, 0) * Length).P;
				Over = std::max(Over, At.y + (Nth(Kk, 2) + Nth(Kk, 3)) * Ky - L.TankTopMax);
			}
			Sink = TorqueSim::Clamp(Over, 0.0, Tall * 0.5);
			const double Left = Over - Sink;
			if (Left > 0.002 && Tall > 0.01)
			{
				const double Squash = std::max((Tall - Left) / Tall, 0.6);
				Ky *= Squash;
				Sink *= Squash;
			}
		}
		// How far under the floor the flanks may come and stay clear of the heads (clear_under).
		double Below = NoLimit;
		for (const V3& Tp : Tops)
		{
			const V3 At = Crank + Tp;
			for (double Dz : {-HeadLong, -HeadLong * 0.5, 0.0, HeadLong * 0.5, HeadLong})
			{
				const double Z = At.z + Dz;
				if (Z < Front || Z > Front + Length) continue;
				Below = std::min(Below, LineAt(Floor, Z).P.y - At.y - HeadClear);
			}
		}
		Below -= BaseTunnelDeep * Ky;
		if (Sink > Below)
		{
			const double Short = Sink - std::max(Below, 0.0);
			Sink = std::max(Below, 0.0);
			const double Tall = Tallest(Ky);
			if (Tall > 0.01)
			{
				const double Squash = std::max((Tall - Short) / Tall, 0.6);
				Ky *= Squash;
				Sink *= Squash;
			}
		}
		if (Name == "adv") Skirt = std::max(Skirt, std::min(Below - 0.01, DeepSkirt * Ky));
		Skirt = std::max(std::min(Skirt, Below), -0.8 * BaseTunnelDeep * Ky);
		FittedPart P;
		P.Slot = "tank";
		P.Glb = "tank/" + Name + ".glb";
		P.Weights["tunnel_width"] = TorqueSim::Clamp((HalfW + TunnelSpare - BaseTunnelHalf) / 0.05 / std::max(Kx, 0.5), -0.3, 1.5);
		P.Weights["tunnel_height"] = Sink / (TunnelSink * Ky);
		P.Weights["flank_depth"] = (Skirt < 0.0 ? Skirt : std::max(Skirt - Sink, 0.0)) / (FlankSkirt * Ky);
		P.Weights["nose_reach"] = 0.6;
		const double Depth = TankKnee(Name);
		if (Depth > 0.0)
		{
			const double Tk = TorqueSim::Clamp((ToGodot(L.KneeR).z - Front) / Length, KneeAt0, KneeAt1);
			const double F = (Tk - KneeAt0) / (KneeAt1 - KneeAt0);
			P.Weights["knee_fore"] = Depth * (1.0 - F);
			P.Weights["knee_aft"] = Depth * F;
		}
		const double KyF = Ky;
		P.Warp = InOurs([Floor, Front, Length, Kx, KyF](const V3& G) {
			const OnLine On = LineAt(Floor, Front + G.z * Length);
			return On.P + UpFrom(On.D) * (G.y * KyF) + V3(G.x * Kx, 0.0, 0.0);
		});
		Out.Parts.push_back(P);
		// Its loft sections (tank_secs), as the seat's nose and the rest of the bike read them.
		const int Steps = Shape->Flag("facet") ? 20 : 14;
		for (int I = 0; I <= Steps; ++I)
		{
			const double U = static_cast<double>(I) / Steps;
			const std::vector<double> K = KeysAt(Keys, U, 3);
			const OnLine On = LineAt(Floor, Front + U * Length);
			const double Down = K[2] * Ky;
			Out.TankSecs.push_back({On.P + V3(0.0, Down - Sink, 0.0), K[0] * Kx, K[1] * Ky, Down});
		}
		// (RigBodywork.tank_fit's "flank", and BikeModel._draw_tank's base line from it.)
		const double Flank = (Skirt < 0.0 ? Skirt : std::max(Sink, Skirt)) + BaseTunnelDeep * Ky;
		const double Under = Sink - Flank;
		Out.TankA = Out.TankSecs.front().C + V3(0.0, Under - Out.TankSecs.front().Down, 0.0);
		Out.TankB = Out.TankSecs.back().C + V3(0.0, Under - Out.TankSecs.back().Down, 0.0);
	}

	// ------------------------------------------------------------------ seat
	const Json* Saddle = Lay.Part(SlotPart(Parts, "saddle"));
	const Json* SeatShapes = T.Get("seat_shapes");
	const Json* Cowls = T.Get("tail_cowls");
	const Json* Tail = Lay.Part(SlotPart(Parts, "tail"));
	const std::string CowlKind = LookStr(Tail, "cowl");
	const Json* CowlKeys = (Cowls && !CowlKind.empty()) ? Cowls->Get(CowlKind) : nullptr;
	const double CowlLn = (Tail && Tail->Get("look")) ? Tail->Get("look")->Num("length", 1.0) : 1.0;
	// A full fairing's sports tail comes forward to just behind the rider (_cowl_lead).
	std::string FairingSet;
	if (const Json* Fr = Lay.Part(SlotPart(Parts, "fairing")))
		if (Fr->Flag("frame_mount"))
		{
			const std::string Sh = LookStr(Fr, "shape").empty() ? Fr->Str("style") : LookStr(Fr, "shape");
			const Json* Lk = Fr->Get("look");
			if (Sh == "race") FairingSet = (Lk && !Lk->Flag("lamps", true)) ? "race" : "sport";
		}
	const double Lead = (CowlKeys && (FairingSet == "sport" || FairingSet == "race") && (CowlKind == "sport" || CowlKind == "racing")) ? CowlHipLead : NoLimit;
	const V3 RailA = ToGodot(L.SeatFront), RailB = ToGodot(L.Tail), Hip = ToGodot(L.Hip);
	auto CowlStart = [&](const Json& Keys) {
		double Z = std::max(RailB.z + Nth(Keys.A[0], 0) * CowlLn, RailA.z + 0.3);
		if (Lead < NoLimit) Z = std::min(Z, std::max(Hip.z + Lead, RailA.z + 0.2));
		return Z;
	};
	if (Saddle && SeatShapes)
	{
		const std::string Style = Saddle->Str("style", "bench");
		const std::string Key = LookStr(Saddle, "shape").empty() ? Style : LookStr(Saddle, "shape");
		std::string Name = SeatShapes->Get(Key) ? Key : (SeatShapes->Get(Style) ? Style : "bench");
		const Json* Shape = SeatShapes->Get(Name);
		const V3 Sc = LookVec(Saddle, "scale", V3(1, 1, 1));
		double CowlZ = NoLimit;
		if (CowlKeys && SlotPart(Parts, "tail") != "none")
		{
			CowlZ = CowlStart(*CowlKeys);
			if (Lead < NoLimit)
			{
				Name = "brat";
				Shape = SeatShapes->Get("brat");
			}
		}
		const Json& Keys = *(Shape->Get("rig_keys") ? Shape->Get("rig_keys") : Shape->Get("keys"));
		const double Span = Shape->Get("rig_span") ? Shape->Num("rig_span", 1.0) : Shape->Num("span", 1.0);
		const double ZLimit = CowlZ < NoLimit ? CowlZ + 0.03 : NoLimit;
		// seat_fit
		double Slope = (RailB.y - RailA.y) / std::max(RailB.z - RailA.z, 0.01);
		if (RailB.z - Hip.z > 0.05) Slope = std::min(Slope, (RailB.y - Hip.y) / (RailB.z - Hip.z));
		const double Z0 = !L.SeatOverlap ? std::max(RailA.z, Rear.z - 0.01) : RailA.z + Shape->Num("from", 0.0);
		const double Z1 = TorqueSim::Lerp(Z0, std::max(std::min(RailB.z + Shape->Num("to", 0.0), ZLimit), Z0 + 0.25), Span * Sc.x);
		auto RailY = [RailA, Slope](double Z) { return RailA.y + Slope * (Z - RailA.z); };
		const double Length = Z1 - Z0;
		FittedPart P;
		P.Slot = "saddle";
		P.Glb = "saddle/" + Name + ".glb";
		P.ColourTag = "leather";
		{
			const double U = TorqueSim::Clamp((Hip.z - Z0) / std::max(Length, 0.01), 0.0, 1.0);
			const std::vector<double> K = KeysAt(Keys, U, 4);
			const double Top = (K[2] + K[3] + K[1]) * Sc.z;
			P.Weights["foam"] = TorqueSim::Clamp((Hip.y - RailY(Hip.z) - Top) / SeatFoam / std::max(Sc.z, 0.5), -1.0, 2.5);
		}
		if (!Out.TankSecs.empty())
		{
			const LoftSection& Tr = Out.TankSecs.back();
			const std::vector<double> K0 = KeysAt(Keys, 0.0, 4);
			P.Weights["nose_width"] = TorqueSim::Clamp((Tr.W - K0[0] * Sc.y) / SeatNoseW, -1.0, 1.5);
			const double TankTop = Tr.C.y + Tr.Up;
			const double SeatTop = RailY(Z0) + (K0[1] + K0[2] + K0[3] + SeatFoam * P.Weights["foam"]) * Sc.z;
			P.Weights["nose_height"] = TorqueSim::Clamp((TankTop - SeatTop) / SeatNoseH, -1.0, Shape->Num("nose_max", 1.5));
		}
		const V3 Up = UpFrom(V3(0.0, Slope, 1.0).Normalized());
		P.Warp = InOurs([Z0, Length, Sc, Up, RailY](const V3& G) {
			const double Z = Z0 + G.z * Length;
			return V3(G.x * Sc.y, RailY(Z), Z) + Up * (G.y * Sc.z);
		});
		// Its sections (for the tail cowl's join and the stitching).
		const int Steps = 16;
		for (int I = 0; I <= Steps; ++I)
		{
			const double U = static_cast<double>(I) / Steps;
			const std::vector<double> K = KeysAt(Keys, U, 4);
			const double Z = TorqueSim::Lerp(Z0, Z1, U);
			const double NoseF = 1.0 - SmoothStep01(0.0, 0.2, U);
			const double Low = (K[2] + K[3]) * Sc.z;
			Out.SeatSecs.push_back({V3(0.0, RailY(Z) + Low, Z), (K[0] + SeatNoseW * P.Weights["nose_width"] * NoseF) * Sc.y,
				(K[1] + SeatFoam * P.Weights["foam"] + SeatNoseH * P.Weights["nose_height"] * NoseF) * Sc.z, Low});
		}
		Out.SeatEnd = Out.SeatSecs[Steps];
		if (CowlZ < Z1)
			Out.SeatEnd = Out.SeatSecs[static_cast<size_t>(TorqueSim::Clamp(std::round((CowlZ - Z0) / std::max(Z1 - Z0, 0.01) * Steps), 0.0, Steps))];
		Out.SeatZ0 = Z0;
		Out.SeatZ1 = Z1;
		Out.SeatSlope = Slope;
		Out.HasSeat = true;
		Out.SeatN = Shape->Num("n", 2.4);
		Out.Parts.push_back(P);
	}

	// ------------------------------------------------------------------ tail cowl
	if (CowlKeys && Out.HasSeat)
	{
		const double Z1 = CowlStart(*CowlKeys);
		const double Slope = Out.SeatSlope;
		const V3 From(0.0, RailA.y + Slope * (Z1 - RailA.z), Z1);
		FittedPart P;
		P.Slot = "tail";
		P.Glb = "tail/cowl/" + CowlKind + ".glb";
		P.Weights["front_width"] = TorqueSim::Clamp((Out.SeatEnd.W - Nth(CowlKeys->A[0], 1)) / CowlFrontW, 0.0, 1.0);
		const V3 Dir = V3(0.0, Slope, 1.0).Normalized();
		const V3 Up = UpFrom(Dir);
		const double Kz = CowlLn;
		P.Warp = InOurs([From, Dir, Up, Kz](const V3& G) { return From + Dir * (G.z * Kz) + Up * G.y + V3(G.x, 0.0, 0.0); });
		Out.Parts.push_back(P);
		Out.CowlKind = CowlKind;
		Out.CowlT = From + V3(0.0, Nth(CowlKeys->A[0], 3) - 0.02, -Nth(CowlKeys->A[0], 0) * CowlLn);
		for (const Json& K : CowlKeys->A)
			Out.CowlSecs.push_back({Out.CowlT + V3(0.0, Nth(K, 4), Nth(K, 0) * CowlLn), Nth(K, 1), Nth(K, 2), Nth(K, 3)});
	}
	return Out;
}
} // namespace TorqueFit
