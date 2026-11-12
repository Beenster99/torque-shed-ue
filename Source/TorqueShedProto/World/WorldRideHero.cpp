// Britain's landmark bridges as hero pieces (Ride UK stage 8): the Humber, the Severn, the Forth's,
// the Tamar and the rest, their towers, main cables and hangers, cable stays or arches stood over
// the deck the road already has, from where the map's bridge is and a few numbers for each.
#include "WorldRide.h"

#include "FitJson.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RideHeroBits
{
struct V3
{
	double X = 0.0, Y = 0.0, Z = 0.0;
};

V3 Make3(double X, double Y, double Z)
{
	V3 Q;
	Q.X = X;
	Q.Y = Y;
	Q.Z = Z;
	return Q;
}

// One flat-shaded quad, wound to face away from Inside.
void Face4(RideMesh& Out, const V3& A, const V3& B, const V3& C, const V3& D, const V3& Inside, const float* Rgb, double Shade)
{
	const double Ux = C.X - A.X, Uy = C.Y - A.Y, Uz = C.Z - A.Z, Wx = D.X - B.X, Wy = D.Y - B.Y, Wz = D.Z - B.Z;
	double Nx = Uy * Wz - Uz * Wy, Ny = Uz * Wx - Ux * Wz, Nz = Ux * Wy - Uy * Wx;
	const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	if (Len < 1e-9) return;
	const bool Flip = Nx * (A.X - Inside.X) + Ny * (A.Y - Inside.Y) + Nz * (A.Z - Inside.Z) < 0.0;
	const double Sg = (Flip ? -1.0 : 1.0) / Len;
	Nx *= Sg;
	Ny *= Sg;
	Nz *= Sg;
	const uint32_t V = uint32_t(Out.Vertices());
	for (const V3* Q : {&A, &B, &C, &D})
	{
		Out.Pos.push_back(float(Q->X - Out.OriginX));
		Out.Pos.push_back(float(Q->Y));
		Out.Pos.push_back(float(Q->Z - Out.OriginZ));
		Out.Nrm.push_back(float(Nx));
		Out.Nrm.push_back(float(Ny));
		Out.Nrm.push_back(float(Nz));
		Out.Uv.push_back(float(Q->X - Out.OriginX + Q->Z - Out.OriginZ));
		Out.Uv.push_back(float(-Q->Y));
		for (int K = 0; K < 3; ++K) Out.Col.push_back(float(Rgb[K] * Shade));
	}
	if (!Flip) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
	else Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
}

// A straight member of square section Wide from A to B (a tower's leg, a cable's length, a hanger).
void Beam(RideMesh& Out, const V3& A, const V3& B, double Wide, const float* Rgb)
{
	const double Dx = B.X - A.X, Dy = B.Y - A.Y, Dz = B.Z - A.Z;
	const double L = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
	if (L < 1e-6) return;
	const double Fx = Dx / L, Fy = Dy / L, Fz = Dz / L;
	// (two sides square to it: one level where it can be, the other square to both)
	double Sx = -Fz, Sy = 0.0, Sz = Fx;
	double Sl = std::sqrt(Sx * Sx + Sz * Sz);
	if (Sl < 1e-6) { Sx = 1.0; Sz = 0.0; Sl = 1.0; }
	Sx /= Sl;
	Sz /= Sl;
	const double Tx = Fy * Sz - Fz * Sy, Ty = Fz * Sx - Fx * Sz, Tz = Fx * Sy - Fy * Sx;
	const double H = Wide * 0.5;
	const auto Corner = [&](const V3& P, double Ss, double Ts) { return Make3(P.X + (Sx * Ss + Tx * Ts) * H, P.Y + (Sy * Ss + Ty * Ts) * H, P.Z + (Sz * Ss + Tz * Ts) * H); };
	const V3 Mid = Make3((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, (A.Z + B.Z) * 0.5);
	const double Sides[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
	for (int K = 0; K < 4; ++K)
		Face4(Out, Corner(A, Sides[K][0], Sides[K][1]), Corner(A, Sides[K + 1][0], Sides[K + 1][1]), Corner(B, Sides[K + 1][0], Sides[K + 1][1]), Corner(B, Sides[K][0], Sides[K][1]), Mid, Rgb,
			K % 2 == 0 ? 1.0 : 0.82);
}
} // namespace RideHeroBits

bool RidePlan::ReadLandmarks(const std::string& Text, std::vector<Landmark>& Out)
{
	Out.clear();
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsArray()) return false;
	for (const TorqueFit::Json& J : Root.A)
	{
		Landmark L;
		L.Name = J.Str("name");
		L.Kind = J.Str("kind", "suspension");
		const TorqueFit::Json* At = J.Get("at");
		if (At == nullptr || !At->IsArray() || At->A.size() < 2) continue;
		L.At.Lat = At->A[0].N;
		L.At.Lon = At->A[1].N;
		L.Span = J.Num("span", 300.0);
		L.Up = J.Num("up", 60.0);
		L.Towers = std::max(1, int(J.Num("towers", L.Kind == "arch" ? 0.0 : 2.0)));
		if (const TorqueFit::Json* C = J.Get("colour"))
			if (C->IsArray() && C->A.size() >= 3)
				for (int K = 0; K < 3; ++K) L.Colour[K] = float(C->A[size_t(K)].N);
		Out.push_back(L);
	}
	return true;
}

void RidePlan::HeroBridges(const std::vector<Landmark>& Marks, std::vector<HeroPiece>& Out) const
{
	using namespace RideHeroBits;
	Out.clear();
	const Frame F(Area.Roads.FrameLat, Area.Roads.FrameLon);
	// Each landmark's bridge: the run of deck nearest its middle (within 250 m), on any road; a run
	// two landmarks are near (the Forth's two) is the nearer one's.
	struct Match
	{
		size_t Mark = 0;
		int Road = -1;
		size_t At = 0, S = 0, E = 0;
		double D = 0.0;
	};
	std::vector<Match> Found;
	for (size_t K = 0; K < Marks.size(); ++K)
	{
		const P2 Want = F.FromGeo(Marks[K].At.Lat, Marks[K].At.Lon);
		Match Best;
		Best.Mark = K;
		Best.D = 250.0;
		for (int Which = 0; Which < RoadCount(); ++Which)
		{
			const std::vector<RoadPoint>& Pts = RoadOf(Which);
			for (size_t I = 0; I < Pts.size(); ++I)
			{
				if (!Pts[I].Bridge) continue;
				const double D = Pts[I].At.DistanceTo(Want);
				if (D < Best.D) { Best.D = D; Best.Road = Which; Best.At = I; }
			}
		}
		if (Best.Road < 0) continue;
		const std::vector<RoadPoint>& Pts = RoadOf(Best.Road);
		Best.S = Best.E = Best.At;
		while (Best.S > 0 && Pts[Best.S - 1].Bridge) --Best.S;
		while (Best.E + 1 < Pts.size() && Pts[Best.E + 1].Bridge) ++Best.E;
		Found.push_back(Best);
	}
	std::sort(Found.begin(), Found.end(), [](const Match& A, const Match& B) { return A.D < B.D; });
	std::vector<Match> Taken;
	for (const Match& Fm : Found)
	{
		bool Free = true;
		for (const Match& T : Taken) Free = Free && !(T.Road == Fm.Road && T.S == Fm.S);
		if (!Free) continue;
		Taken.push_back(Fm);
		const Landmark& Mark = Marks[Fm.Mark];
		const std::vector<RoadPoint>& Pts = RoadOf(Fm.Road);
		const size_t S = Fm.S, E = Fm.E, BestAt = Fm.At;
		const double Long = Pts[E].Along - Pts[S].Along;
		// (a deck shorter than most of the main span is another bridge near by: an approach's, a slip road's)
		if (Long < std::max(150.0, Mark.Span * 0.8)) continue;
		// (where along the deck: by its distance along the run, and the deck's middle, height, half width and way there)
		const auto IndexAt = [&Pts, S, E](double Along)
		{
			size_t I = S;
			while (I < E && Pts[I + 1].Along <= Along) ++I;
			return I;
		};
		const auto DirAt = [&Pts](size_t I)
		{
			const size_t A = I > 0 ? I - 1 : I, B = std::min(Pts.size() - 1, I + 1);
			const P2 D = Pts[B].At - Pts[A].At;
			const double L = D.Length();
			return L > 1e-9 ? D * (1.0 / L) : P2(1.0, 0.0);
		};
		const double From = Pts[S].Along, To = Pts[E].Along, Mid = Pts[BestAt].Along;
		HeroPiece Piece;
		Piece.Name = Mark.Name;
		Piece.Mesh = std::make_shared<RideMesh>();
		RideMesh& M = *Piece.Mesh;
		M.OriginX = Pts[BestAt].At.X;
		M.OriginZ = Pts[BestAt].At.Z;
		const float* Rgb = Mark.Colour;
		const auto Edge = [&](double Along, double Side, double Out2, double Up)
		{
			const size_t I = IndexAt(Along);
			const P2 D = DirAt(I);
			const P2 Across(D.Z, -D.X);
			const P2 P = Pts[I].At + Across * (Side * (Pts[I].Half + Out2));
			return Make3(P.X, Pts[I].Height + Up, P.Z);
		};
		const auto Foot = [&](double Along, double Side)
		{
			// (a tower stands from the water or the ground under it, or 40 m under the deck at most)
			const V3 Top = Edge(Along, Side, 1.6, 0.0);
			return Make3(Top.X, std::max(Ground(Top.X, Top.Z) - 2.0, Top.Y - 40.0), Top.Z);
		};
		// The towers: a leg each side of the deck, beams across under the deck, at the top and between.
		std::vector<double> TowerAt;
		const int Count = Mark.Kind == "arch" ? 0 : Mark.Towers;
		// (the towers as far apart as the deck lets them be: the map's deck can be a little short)
		const double Span = Count > 1 ? std::min(Mark.Span, (Long - 24.0) / (Count - 1)) : Mark.Span;
		const double Centre = std::clamp(Mid, From + 12.0 + Span * (Count - 1) * 0.5, std::max(From + 12.0 + Span * (Count - 1) * 0.5, To - 12.0 - Span * (Count - 1) * 0.5));
		for (int K = 0; K < Count; ++K)
		{
			const double At = Centre + (K - (Count - 1) * 0.5) * Span;
			if (At < From + 10.0 || At > To - 10.0) continue;
			TowerAt.push_back(At);
		}
		const double Leg = std::max(2.0, std::min(6.0, Mark.Up / 25.0));
		for (double At : TowerAt)
		{
			const V3 L0 = Foot(At, 1.0), R0 = Foot(At, -1.0);
			const V3 L1 = Edge(At, 1.0, 1.6, Mark.Up), R1 = Edge(At, -1.0, 1.6, Mark.Up);
			Beam(M, L0, L1, Leg, Rgb);
			Beam(M, R0, R1, Leg, Rgb);
			for (double Up : {-2.5, Mark.Up * 0.55, Mark.Up - Leg * 0.5})
				Beam(M, Edge(At, 1.0, 1.6, Up), Edge(At, -1.0, 1.6, Up), Leg * 0.7, Rgb);
		}
		static constexpr float Steel[3] = {0.20f, 0.21f, 0.22f};
		if (Mark.Kind == "suspension" && TowerAt.size() >= 2)
		{
			// The main cables: hanging between the towers' tops nearly to the deck, and back to the ends.
			const double T0 = TowerAt.front(), T1 = TowerAt.back();
			const auto CableUp = [&](double Along)
			{
				if (Along >= T0 && Along <= T1)
				{
					const double U = (Along - T0) / std::max(1.0, T1 - T0) * 2.0 - 1.0;
					return 2.5 + (Mark.Up - 2.5) * U * U;
				}
				const double V = Along < T0 ? (Along - From) / std::max(1.0, T0 - From) : (To - Along) / std::max(1.0, To - T1);
				return 1.0 + (Mark.Up - 1.0) * std::pow(std::max(0.0, V), 1.4);
			};
			for (double Side : {1.0, -1.0})
			{
				double Last = From;
				for (double A = From + 8.0; A <= To + 1e-6; A += 8.0)
				{
					const double Cur = std::min(A, To);
					Beam(M, Edge(Last, Side, 1.6, CableUp(Last)), Edge(Cur, Side, 1.6, CableUp(Cur)), 0.7, Steel);
					Last = Cur;
				}
				for (double A = From + 12.0; A < To - 6.0; A += 12.0)
				{
					const double Up = CableUp(A);
					if (Up > 1.8) Beam(M, Edge(A, Side, 1.6, 1.0), Edge(A, Side, 1.6, Up), 0.12, Steel);
				}
			}
		}
		else if (Mark.Kind == "cable_stayed")
		{
			// Stays: from high on each tower out to the deck's edges every 15 m, half way to the next tower or to the end.
			for (size_t K = 0; K < TowerAt.size(); ++K)
			{
				const double At = TowerAt[K];
				const double Back = K == 0 ? std::min(Mark.Span * 0.5, At - From) : (At - TowerAt[K - 1]) * 0.5;
				const double On = K + 1 == TowerAt.size() ? std::min(Mark.Span * 0.5, To - At) : (TowerAt[K + 1] - At) * 0.5;
				for (double Side : {1.0, -1.0})
					for (double Way : {-1.0, 1.0})
					{
						const double Reach = Way < 0.0 ? Back : On;
						int N = 0;
						for (double D = 20.0; D <= Reach - 5.0; D += 15.0, ++N)
						{
							const double Hi = Mark.Up * (0.97 - 0.45 * std::min(1.0, N / 14.0));
							Beam(M, Edge(At, Side, 1.6, Mark.Up - (Mark.Up - Hi) * 0.5 - N * 0.4), Edge(At + Way * D, Side, 0.8, 0.8), 0.18, Steel);
						}
					}
			}
		}
		else if (Mark.Kind == "arch")
		{
			// A through arch: a rib each side over the main span, the deck hung from it.
			const double A0 = std::max(From, Mid - Mark.Span * 0.5), A1 = std::min(To, Mid + Mark.Span * 0.5);
			const auto RibUp = [&](double Along)
			{
				const double U = (Along - A0) / std::max(1.0, A1 - A0) * 2.0 - 1.0;
				return Mark.Up * (1.0 - U * U) - 3.0 * U * U;
			};
			for (double Side : {1.0, -1.0})
			{
				double Last = A0;
				for (double A = A0 + 6.0; A <= A1 + 1e-6; A += 6.0)
				{
					const double Cur = std::min(A, A1);
					Beam(M, Edge(Last, Side, 1.2, RibUp(Last)), Edge(Cur, Side, 1.2, RibUp(Cur)), 2.6, Rgb);
					Last = Cur;
				}
				for (double A = A0 + 10.0; A < A1 - 5.0; A += 10.0)
					if (RibUp(A) > 1.5) Beam(M, Edge(A, Side, 1.2, 0.5), Edge(A, Side, 1.2, RibUp(A)), 0.3, Rgb);
			}
			// (braced across the top)
			for (double A = A0 + Mark.Span * 0.25; A <= A1 - Mark.Span * 0.25 + 1e-6; A += 12.0)
				Beam(M, Edge(A, 1.0, 1.2, RibUp(A)), Edge(A, -1.0, 1.2, RibUp(A)), 0.8, Rgb);
		}
		if (M.Vertices() > 0) Out.push_back(std::move(Piece));
	}
}
} // namespace TorqueWorld
