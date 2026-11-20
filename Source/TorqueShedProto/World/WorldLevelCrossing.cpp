// [level crossings] The layout of each type of crossing (see WorldLevelCrossing.h). The kit's numbers here are
// Tools/props/make_level_crossing.py's; change both together.
#include "WorldLevelCrossing.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace LevelX
{
namespace
{
constexpr double UnitY = 2.75;       // a light unit's middle up its post (UNIT_Y)
constexpr double BoardHalfH = 0.4;   // the light unit's board's half height (its saltire stands on its top)
constexpr double PostTop = 3.9;      // POST_TOP
constexpr double KerbOut = 0.9;      // a post this far out from the road's edge
constexpr double MechOut = 0.55;     // a barrier machine's middle this far out

P2 LeftOf(const P2& D) { return P2(D.Z, -D.X); }
P2 Norm(const P2& V)
{
	const double L = V.Length();
	return L > 1e-9 ? V * (1.0 / L) : P2(1.0, 0.0);
}
double Dot(const P2& A, const P2& B) { return A.X * B.X + A.Z * B.Z; }
double Cross(const P2& A, const P2& B) { return A.X * B.Z - A.Z * B.X; }
} // namespace

const char* TypeName(int Type)
{
	static const char* Names[Types] = {"open (no lights)", "open with lights", "half barrier", "full barrier", "gated"};
	return Type >= 0 && Type < Types ? Names[Type] : "?";
}

double StandOff(const Spot& S)
{
	// (sin of the angle between the road and the line, kept off the flat: a crossing more skewed than about 27
	// degrees is laid as if it were that)
	const double Sin = std::max(0.45, std::fabs(Cross(Norm(S.Road), Norm(S.Rail))));
	const double Cot = std::sqrt(std::max(0.0, 1.0 - Sin * Sin)) / Sin;
	return Clear / Sin + S.Half * Cot;
}

int TypeFor(const std::string& Class, double Half, uint32_t Key)
{
	const uint32_t Pick = (Key >> 7) % 10u;
	auto Is = [&Class](const char* C) { return Class.rfind(C, 0) == 0; };
	if (Is("motorway") || Is("trunk") || Is("primary") || Is("secondary")) return FullBarrier;
	if (Is("tertiary")) return Pick < 3 ? FullBarrier : HalfBarrier;
	if (Is("unclassified") || Is("residential") || Is("living_street") || Is("road"))
		return Half < 2.4 ? (Pick < 5 ? Lights : Passive) : (Pick < 6 ? HalfBarrier : Lights);
	// (a track, a service road, a farm's way over the line)
	return Pick < 7 ? Gated : Passive;
}

std::string BoomMesh(double Length)
{
	const int N = std::clamp(int(std::ceil(Length - 0.05)), 3, 9);
	return "lx_boom_" + std::to_string(N);
}

std::string GateMesh(double Length)
{
	int Best = 25;
	for (int N : {25, 30, 35, 40, 45})
		if (std::fabs(N * 0.1 - Length) < std::fabs(Best * 0.1 - Length)) Best = N;
	return "lx_gate_" + std::to_string(Best);
}

double MeshLength(const std::string& Mesh)
{
	const size_t U = Mesh.rfind('_');
	if (U == std::string::npos) return 1.0;
	const double N = std::atof(Mesh.c_str() + U + 1);
	return Mesh.rfind("lx_gate_", 0) == 0 ? N * 0.1 : N;
}

void BoomLamps(double Length, double Out[3])
{
	Out[0] = 0.8;
	Out[1] = Length * 0.5 + 0.2;
	Out[2] = Length - 0.15;
}

std::vector<Piece> Layout(int Type, const Spot& S)
{
	std::vector<Piece> Out;
	const P2 Road = Norm(S.Road), Rail = Norm(S.Rail), Lft = LeftOf(Road);
	const double Half = std::max(1.5, S.Half);
	const double Far = StandOff(S);
	auto At = [&](double U, double V) { return S.Mid + Road * U + Lft * V; };
	auto Add = [&Out](const std::string& Mesh, int Role, int Approach, const P2& Where, double Up, const P2& Front) -> Piece&
	{
		Piece P;
		P.Mesh = Mesh;
		P.Role = Role;
		P.Approach = Approach;
		P.At = Where;
		P.Up = Up;
		P.Front = Front;
		Out.push_back(P);
		return Out.back();
	};
	const bool Lit = Type == Lights || Type == HalfBarrier || Type == FullBarrier;
	for (int A = 0; A < 2; ++A)
	{
		// The approach's side: the traffic comes from U = Sg * infinity heading for the line; its left (the nearside,
		// Britain keeping left) is V = Nr * ...; its signs face it.
		const double Sg = A == 0 ? -1.0 : 1.0, Nr = -Sg;
		const P2 Front = Road * Sg;
		const double Kerb = Half + KerbOut;
		const P2 Near = At(Sg * (Far + 1.0), Nr * Kerb), Off = At(Sg * (Far + 1.0), -Nr * Kerb);
		if (Lit)
		{
			// The nearside post: its light unit (with a saltire over it at an open crossing), KEEP CROSSING CLEAR and the
			// blue plate under; the offside post: its light unit (and saltire).
			for (int Side = 0; Side < 2; ++Side)
			{
				const P2 Post = Side == 0 ? Near : Off;
				Add("lx_post", Fixed, A, Post, 0.0, Front);
				Add("lx_unit", Unit, A, Post, UnitY, Front).Offside = Side == 1;
				if (Type == Lights) Add("lx_saltire", Fixed, A, Post, UnitY + BoardHalfH, Front);
			}
			Add("lx_keep_clear", Fixed, A, Near, 2.08, Front);
			Add("lx_contact", Contact, A, Near, 1.66, Front).Row = int(S.Key % 16u);
		}
		if (Type == Lights)
			Add("lx_danger", Fixed, A, At(Sg * (Far + 3.0), Nr * (Half + 2.2)), 0.0, Front);
		if (Type == HalfBarrier || Type == FullBarrier)
		{
			// The barrier machine at the nearside kerb, its boom over the approach's own lane (half barriers stop short
			// of the middle, leaving the way off the crossing open); a full barrier's second boom from the offside kerb
			// over the other lane, its machine facing the line (so its boom swings the other way).
			const double MechV = Half + MechOut;
			Piece& B = Add("", Boom, A, At(Sg * Far, Nr * MechV), 0.0, Front);
			B.Length = Type == HalfBarrier ? MechV - 0.35 : MechV + 0.05;
			B.Mesh = BoomMesh(B.Length);
			B.Skirted = Type == FullBarrier;
			if (Type == FullBarrier)
			{
				Piece& B2 = Add("", Boom, A, At(Sg * Far, -Nr * MechV), 0.0, Front * -1.0);
				B2.Length = MechV + 0.05;
				B2.Mesh = BoomMesh(B2.Length);
				B2.Skirted = true;
				B2.Offside = true;
			}
		}
		if (Type == Passive)
		{
			// A post with the saltire on its top, Give Way under it and the locomotive plate; KEEP CROSSING CLEAR and the
			// blue plate lower down.
			Add("lx_post", Fixed, A, Near, 0.0, Front);
			Add("lx_saltire_top", Fixed, A, Near, PostTop, Front);
			Add("lx_give_way", Fixed, A, Near, 3.15, Front);
			Add("lx_loco", Fixed, A, Near, 2.5, Front);
			Add("lx_keep_clear", Fixed, A, Near, 2.05, Front);
			Add("lx_contact", Contact, A, Near, 1.62, Front).Row = int(S.Key % 16u);
		}
		if (Type == Gated)
		{
			// A gate each side of the road, hinged at its kerb, lying back along the road (away from the line) while it's
			// open, swinging shut across its half; the miniature stop light and the instructions before it.
			for (int Side = 0; Side < 2; ++Side)
			{
				const double V = (Side == 0 ? Nr : -Nr) * (Half + 0.25);
				Piece& G = Add("", Gate, A, At(Sg * Far, V), 0.0, Front);
				G.Length = Half + 0.2;
				G.Mesh = GateMesh(G.Length);
				G.Shut = Lft * (V > 0.0 ? -1.0 : 1.0);
				G.Offside = Side == 1;
			}
			Add("lx_msl", Msl, A, At(Sg * (Far + 2.2), Nr * (Half + 0.8)), 0.0, Front);
			Add("lx_user_worked", Fixed, A, At(Sg * (Far + 3.4), Nr * (Half + 0.9)), 0.0, Front);
			Add("lx_post", Fixed, A, At(Sg * (Far + 4.6), Nr * (Half + 0.9)), 0.0, Front);
			Add("lx_keep_clear", Fixed, A, At(Sg * (Far + 4.6), Nr * (Half + 0.9)), 2.05, Front);
			Add("lx_contact", Contact, A, At(Sg * (Far + 4.6), Nr * (Half + 0.9)), 1.62, Front).Row = int(S.Key % 16u);
		}
		// The road's paint over the approach's lane: a stop line (STOP over it at a gated crossing), or a Give Way's
		// broken lines at an open one with no lights, its top a metre short of the barrier (or the gate's swing).
		{
			const double Len = Type == Gated ? 4.0 : 2.0;
			const double U0 = Far + (Type == Gated ? Half + 0.6 : 0.7);
			Piece& P = Add("paint", Paint, A, At(Sg * (U0 + Len * 0.5), Nr * Half * 0.5), 0.0, Front);
			P.OnRoad = true;
			P.Length = Len;
			P.Width = std::max(1.2, Half - 0.3);
			P.Row = Type == Gated ? 0 : Type == Passive ? 2 : 1;
		}
		// The fencing along the railway's edge each side of the road (picket at half barriers, palisade at full),
		// a little in from the barriers, from clear of the road's posts out 10 m along the line each way.
		if (Type == HalfBarrier || Type == FullBarrier)
		{
			// (the side of the line the approach is on: across the line towards U = Sg * infinity)
			P2 Away = LeftOf(Rail);
			if (Dot(Away, Road) * Sg < 0.0) Away = Away * -1.0;
			const double Dp = Clear - 1.0;
			const P2 Line0 = S.Mid + Away * Dp;
			const double Along = Dot(Rail, Lft);   // (how fast the fence line crosses the road's width)
			for (double Way : {-1.0, 1.0})
			{
				// (from where the line is Half + 1.8 off the road's middle, outwards)
				const double Sv = Way * Along;
				if (std::fabs(Sv) < 0.2) continue;
				const double V0 = Dot(Line0 - S.Mid, Lft);
				const double Want = (Sv > 0.0 ? 1.0 : -1.0) * (Half + 1.8);
				const double T0 = std::max(0.0, (Want - V0) / Sv);
				const P2 Dir = Rail * Way;
				const P2 PanelFront(-Dir.Z, Dir.X);   // (its +x along Dir)
				for (int K = 0; K < 5; ++K)
				{
					Piece& F = Add(Type == FullBarrier ? "lx_palisade" : "lx_picket", Fence, A, Line0 + Dir * (T0 + K * PanelLength), 0.0, PanelFront);
					F.Length = PanelLength;
				}
			}
		}
	}
	// One equipment case by the line (not at a gated or open crossing), a floodlight column at a full barrier one.
	if (Lit)
		Add("lx_cabinet", Fixed, 0, At(-(Far + 3.0), -(Half + 3.2)), 0.0, Road * -1.0);
	if (Type == FullBarrier)
		Add("lx_lamp_post", Fixed, 1, At(Far + 2.0, -(Half + 2.6)), 0.0, Road);
	return Out;
}
} // namespace LevelX
} // namespace TorqueWorld
