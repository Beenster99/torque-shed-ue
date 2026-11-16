#include "TftMap.h"

#include <algorithm>
#include <cmath>

namespace TorqueTft
{
namespace
{
void Grow(double& X0, double& Z0, double& X1, double& Z1, const std::vector<MapPt>& Pts)
{
	if (Pts.empty()) return;
	X0 = X1 = Pts[0].X;
	Z0 = Z1 = Pts[0].Z;
	for (const MapPt& Q : Pts)
	{
		X0 = std::min(X0, Q.X); X1 = std::max(X1, Q.X);
		Z0 = std::min(Z0, Q.Z); Z1 = std::max(Z1, Q.Z);
	}
}

double Cross(const MapPt& A, const MapPt& B, const MapPt& C) { return (B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X); }

bool InTri(const MapPt& Q, const MapPt& A, const MapPt& B, const MapPt& C)
{
	const double D1 = Cross(A, B, Q), D2 = Cross(B, C, Q), D3 = Cross(C, A, Q);
	const bool Neg = D1 < 0.0 || D2 < 0.0 || D3 < 0.0, Pos = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
	return !(Neg && Pos);
}

// The map's colours, by day and by night.
struct Looks
{
	Rgba Land, Urban, Park, Wood, Water, Building, Casing, Road, Main, Motorway, Track, Rail, Route, RouteEdge, Ink, InkSoft, Halo;
};
Looks LooksOf(bool Night)
{
	Looks L;
	if (!Night)
	{
		L.Land = Rgba(0.953f, 0.945f, 0.925f);
		L.Urban = Rgba(0.918f, 0.910f, 0.890f);
		L.Park = Rgba(0.808f, 0.906f, 0.780f);
		L.Wood = Rgba(0.737f, 0.863f, 0.706f);
		L.Water = Rgba(0.667f, 0.827f, 0.957f);
		L.Building = Rgba(0.855f, 0.835f, 0.800f);
		L.Casing = Rgba(0.760f, 0.760f, 0.760f);
		L.Road = Rgba(1.0f, 1.0f, 1.0f);
		L.Main = Rgba(0.996f, 0.922f, 0.627f);
		L.Motorway = Rgba(0.980f, 0.780f, 0.380f);
		L.Track = Rgba(0.780f, 0.720f, 0.620f);
		L.Rail = Rgba(0.600f, 0.600f, 0.620f);
		L.Route = Rgba(0.259f, 0.522f, 0.957f);
		L.RouteEdge = Rgba(0.118f, 0.345f, 0.780f);
		L.Ink = Rgba(0.200f, 0.210f, 0.230f);
		L.InkSoft = Rgba(0.420f, 0.430f, 0.450f);
		L.Halo = Rgba(1.0f, 1.0f, 1.0f, 0.85f);
	}
	else
	{
		L.Land = Rgba(0.106f, 0.122f, 0.149f);
		L.Urban = Rgba(0.130f, 0.146f, 0.172f);
		L.Park = Rgba(0.098f, 0.172f, 0.145f);
		L.Wood = Rgba(0.082f, 0.150f, 0.125f);
		L.Water = Rgba(0.055f, 0.110f, 0.196f);
		L.Building = Rgba(0.165f, 0.180f, 0.208f);
		L.Casing = Rgba(0.070f, 0.080f, 0.100f);
		L.Road = Rgba(0.290f, 0.314f, 0.360f);
		L.Main = Rgba(0.470f, 0.420f, 0.280f);
		L.Motorway = Rgba(0.580f, 0.450f, 0.220f);
		L.Track = Rgba(0.240f, 0.230f, 0.210f);
		L.Rail = Rgba(0.330f, 0.330f, 0.350f);
		L.Route = Rgba(0.310f, 0.580f, 1.0f);
		L.RouteEdge = Rgba(0.130f, 0.300f, 0.680f);
		L.Ink = Rgba(0.860f, 0.870f, 0.890f);
		L.InkSoft = Rgba(0.600f, 0.620f, 0.650f);
		L.Halo = Rgba(0.0f, 0.0f, 0.0f, 0.6f);
	}
	return L;
}

// A road's width on the ground (m) and the least it is drawn (px), and its turn in the drawing order.
struct RoadLook
{
	double Metres;
	float LeastPx;
	int Order;
};
RoadLook RoadLookOf(RoadKind K)
{
	switch (K)
	{
	case RoadKind::Motorway: return {22.0, 6.0f, 6};
	case RoadKind::Main: return {12.0, 5.0f, 5};
	case RoadKind::Secondary: return {9.0, 4.0f, 4};
	case RoadKind::Minor: return {6.5, 3.0f, 3};
	case RoadKind::Track: return {3.5, 1.6f, 2};
	case RoadKind::Path: return {1.5, 1.0f, 1};
	case RoadKind::Rail: return {2.0, 1.4f, 2};
	case RoadKind::River: return {12.0, 3.0f, 0};
	default: return {3.0, 1.4f, 0};
	}
}
} // namespace

void MapData::AddLine(RoadKind Kind, const std::string& Name, const std::vector<MapPt>& Pts)
{
	if (Pts.size() < 2) return;
	MapLine L;
	L.Kind = Kind;
	L.Name = Name;
	L.Pts = Pts;
	Grow(L.X0, L.Z0, L.X1, L.Z1, L.Pts);
	Lines.push_back(std::move(L));
}

void MapData::AddArea(AreaKind Kind, const std::vector<MapPt>& Ring)
{
	MapArea A;
	A.Kind = Kind;
	if (!Triangulate(Ring, A.Tris)) return;
	Grow(A.X0, A.Z0, A.X1, A.Z1, A.Tris);
	Areas.push_back(std::move(A));
}

void MapData::AddBox(AreaKind Kind, double X, double Z, double W, double D, double Yaw)
{
	const double C = std::cos(Yaw), S = std::sin(Yaw), HW = W * 0.5, HD = D * 0.5;
	auto At = [&](double U, double V) { return MapPt(X + U * C + V * S, Z - U * S + V * C); };
	const MapPt Q[4] = {At(-HW, -HD), At(HW, -HD), At(HW, HD), At(-HW, HD)};
	MapArea A;
	A.Kind = Kind;
	A.Tris = {Q[0], Q[1], Q[2], Q[0], Q[2], Q[3]};
	Grow(A.X0, A.Z0, A.X1, A.Z1, A.Tris);
	Areas.push_back(std::move(A));
}

void MapData::AddLabel(LabelKind Kind, const std::string& Name, double X, double Z)
{
	MapLabel L;
	L.Kind = Kind;
	L.Name = Name;
	L.At = MapPt(X, Z);
	Labels.push_back(std::move(L));
}

int MapData::CellOf(double X, double Z) const
{
	const int CX = std::min(std::max(int(std::floor((X - GX0) / Cell)), 0), GW - 1);
	const int CZ = std::min(std::max(int(std::floor((Z - GZ0) / Cell)), 0), GH - 1);
	return CZ * GW + CX;
}

void MapData::Index()
{
	bool Any = false;
	double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;
	auto Take = [&](double AX0, double AZ0, double AX1, double AZ1) {
		if (!Any)
		{
			X0 = AX0; Z0 = AZ0; X1 = AX1; Z1 = AZ1;
			Any = true;
			return;
		}
		X0 = std::min(X0, AX0); Z0 = std::min(Z0, AZ0);
		X1 = std::max(X1, AX1); Z1 = std::max(Z1, AZ1);
	};
	for (const MapLine& L : Lines) Take(L.X0, L.Z0, L.X1, L.Z1);
	for (const MapArea& A : Areas) Take(A.X0, A.Z0, A.X1, A.Z1);
	for (const MapLabel& L : Labels) Take(L.At.X, L.At.Z, L.At.X, L.At.Z);
	GX0 = X0;
	GZ0 = Z0;
	GW = std::max(1, std::min(4096, int((X1 - X0) / Cell) + 1));
	GH = std::max(1, std::min(4096, int((Z1 - Z0) / Cell) + 1));
	LineCells.assign(size_t(GW) * size_t(GH), {});
	AreaCells.assign(size_t(GW) * size_t(GH), {});
	LabelCells.assign(size_t(GW) * size_t(GH), {});
	auto File = [&](std::vector<std::vector<int>>& Cells, int Which, double AX0, double AZ0, double AX1, double AZ1) {
		const int A = CellOf(AX0, AZ0), B = CellOf(AX1, AZ1);
		for (int CZ = A / GW; CZ <= B / GW; ++CZ)
			for (int CX = A % GW; CX <= B % GW; ++CX) Cells[size_t(CZ) * size_t(GW) + size_t(CX)].push_back(Which);
	};
	for (size_t I = 0; I < Lines.size(); ++I) File(LineCells, int(I), Lines[I].X0, Lines[I].Z0, Lines[I].X1, Lines[I].Z1);
	for (size_t I = 0; I < Areas.size(); ++I) File(AreaCells, int(I), Areas[I].X0, Areas[I].Z0, Areas[I].X1, Areas[I].Z1);
	for (size_t I = 0; I < Labels.size(); ++I) File(LabelCells, int(I), Labels[I].At.X, Labels[I].At.Z, Labels[I].At.X, Labels[I].At.Z);
}

void MapData::Near(double X, double Z, double Reach, std::vector<int>& OutLines, std::vector<int>& OutAreas, std::vector<int>& OutLabels) const
{
	OutLines.clear();
	OutAreas.clear();
	OutLabels.clear();
	if (GW <= 0 || GH <= 0) return;
	const int A = CellOf(X - Reach, Z - Reach), B = CellOf(X + Reach, Z + Reach);
	for (int CZ = A / GW; CZ <= B / GW; ++CZ)
		for (int CX = A % GW; CX <= B % GW; ++CX)
		{
			const size_t At = size_t(CZ) * size_t(GW) + size_t(CX);
			OutLines.insert(OutLines.end(), LineCells[At].begin(), LineCells[At].end());
			OutAreas.insert(OutAreas.end(), AreaCells[At].begin(), AreaCells[At].end());
			OutLabels.insert(OutLabels.end(), LabelCells[At].begin(), LabelCells[At].end());
		}
	// (a thing in more than one square comes up once)
	auto Once = [](std::vector<int>& V) {
		std::sort(V.begin(), V.end());
		V.erase(std::unique(V.begin(), V.end()), V.end());
	};
	Once(OutLines);
	Once(OutAreas);
	Once(OutLabels);
}

bool Triangulate(const std::vector<MapPt>& RingIn, std::vector<MapPt>& OutTris)
{
	OutTris.clear();
	std::vector<MapPt> Ring = RingIn;
	if (Ring.size() >= 2 && std::fabs(Ring.front().X - Ring.back().X) < 1.0e-6 && std::fabs(Ring.front().Z - Ring.back().Z) < 1.0e-6) Ring.pop_back();
	if (Ring.size() < 3) return false;
	double Area = 0.0;
	for (size_t I = 0; I < Ring.size(); ++I)
	{
		const MapPt& A = Ring[I];
		const MapPt& B = Ring[(I + 1) % Ring.size()];
		Area += A.X * B.Z - B.X * A.Z;
	}
	if (std::fabs(Area) < 1.0e-6) return false;
	if (Area < 0.0) std::reverse(Ring.begin(), Ring.end());
	std::vector<int> Left(Ring.size());
	for (size_t I = 0; I < Ring.size(); ++I) Left[I] = int(I);
	int Guard = int(Ring.size()) * int(Ring.size()) + 16;
	while (Left.size() > 3 && Guard-- > 0)
	{
		bool Cut = false;
		for (size_t I = 0; I < Left.size(); ++I)
		{
			const MapPt& A = Ring[size_t(Left[(I + Left.size() - 1) % Left.size()])];
			const MapPt& B = Ring[size_t(Left[I])];
			const MapPt& C = Ring[size_t(Left[(I + 1) % Left.size()])];
			if (Cross(A, B, C) <= 0.0) continue;   // a corner that turns the wrong way is no ear
			bool Clear = true;
			for (size_t J = 0; J < Left.size() && Clear; ++J)
			{
				const int Q = Left[J];
				if (Q == Left[(I + Left.size() - 1) % Left.size()] || Q == Left[I] || Q == Left[(I + 1) % Left.size()]) continue;
				if (InTri(Ring[size_t(Q)], A, B, C)) Clear = false;
			}
			if (!Clear) continue;
			OutTris.push_back(A);
			OutTris.push_back(B);
			OutTris.push_back(C);
			Left.erase(Left.begin() + std::ptrdiff_t(I));
			Cut = true;
			break;
		}
		if (!Cut) break;   // (a knotted outline: what was cut so far is kept)
	}
	if (Left.size() == 3)
	{
		OutTris.push_back(Ring[size_t(Left[0])]);
		OutTris.push_back(Ring[size_t(Left[1])]);
		OutTris.push_back(Ring[size_t(Left[2])]);
	}
	return !OutTris.empty();
}

double ViewMetres(double SpeedMs, double TurnInMetres, int Step)
{
	// About 18 seconds of road ahead, between 350 m and 2.4 km; closing in for a turn.
	double M = std::min(std::max(std::fabs(SpeedMs) * 18.0, 350.0), 2400.0);
	if (TurnInMetres > 0.0 && TurnInMetres < 400.0) M = std::min(M, std::max(260.0, TurnInMetres * 1.6));
	const double Zoom[5] = {0.4, 0.65, 1.0, 1.8, 3.6};
	return M * Zoom[std::min(std::max(Step + 2, 0), 4)];
}

void DrawMap(Paint& P, float X, float Y, float W, float H, const MapScene& Scene, float BikeDown)
{
	const Looks L = LooksOf(Scene.Night);
	P.Clip(X, Y, W, H);
	P.Rect(X, Y, W, H, L.Land);
	if (!Scene.Data || W <= 4.0f || H <= 4.0f)
	{
		P.NoClip();
		return;
	}
	const double Scale = double(W) / std::max(Scene.Metres, 50.0);   // pixels a metre
	const double Head = Scene.NorthUp ? 0.0 : Scene.Heading;
	const double RX = std::cos(Head), RZ = std::sin(Head);     // the view's right, on the ground
	const double FX = std::sin(Head), FZ = -std::cos(Head);    // and its up
	const float CX = X + W * 0.5f, CY = Y + H * (Scene.NorthUp ? 0.5f : BikeDown);
	auto To = [&](const MapPt& Q) {
		const double DX = Q.X - Scene.Bike.X, DZ = Q.Z - Scene.Bike.Z;
		return P2(CX + float((DX * RX + DZ * RZ) * Scale), CY - float((DX * FX + DZ * FZ) * Scale));
	};
	// Everything that could show: within the view's far corner of the bike.
	const double Reach = std::sqrt(double(W) * double(W) + double(H) * double(H)) / Scale;
	std::vector<int> NearLines, NearAreas, NearLabels;
	Scene.Data->Near(Scene.Bike.X, Scene.Bike.Z, Reach, NearLines, NearAreas, NearLabels);
	const bool Close = Scene.Metres < 1500.0, Far = Scene.Metres > 5000.0;

	// The land: towns, parks, woods, water, then buildings (only when close enough to tell them apart).
	for (int Pass = 0; Pass < int(AreaKind::Count); ++Pass)
	{
		if (Pass == int(AreaKind::Building) && !Close) continue;
		for (int I : NearAreas)
		{
			const MapArea& A = Scene.Data->Areas[size_t(I)];
			if (int(A.Kind) != Pass) continue;
			const Rgba C = A.Kind == AreaKind::Urban ? L.Urban : A.Kind == AreaKind::Park ? L.Park : A.Kind == AreaKind::Wood ? L.Wood
				: A.Kind == AreaKind::Water ? L.Water : L.Building;
			for (size_t T = 0; T + 2 < A.Tris.size(); T += 3) P.Tri(To(A.Tris[T]), To(A.Tris[T + 1]), To(A.Tris[T + 2]), C);
		}
	}
	// Rivers, then each kind of road from the least up: its edge first, then its top, so a greater road
	// lies over a lesser one's edge.
	std::vector<P2> Pts;
	auto Stroke = [&](const MapLine& Ln, float Px, const Rgba& C) {
		Pts.clear();
		for (const MapPt& Q : Ln.Pts) Pts.push_back(To(Q));
		P.Path(Pts, Px, C);
	};
	for (int Order = 0; Order <= 6; ++Order)
		for (int Layer = 0; Layer < 2; ++Layer)
			for (int I : NearLines)
			{
				const MapLine& Ln = Scene.Data->Lines[size_t(I)];
				const RoadLook R = RoadLookOf(Ln.Kind);
				if (R.Order != Order) continue;
				if (Far && (Ln.Kind == RoadKind::Track || Ln.Kind == RoadKind::Path || Ln.Kind == RoadKind::Stream)) continue;
				if (!Close && Ln.Kind == RoadKind::Path) continue;
				const float Px = std::max(R.LeastPx, float(R.Metres * Scale));
				const bool Water = Ln.Kind == RoadKind::River || Ln.Kind == RoadKind::Stream;
				const bool Thin = Water || Ln.Kind == RoadKind::Rail || Ln.Kind == RoadKind::Path || Ln.Kind == RoadKind::Track;
				if (Layer == 0)
				{
					if (!Thin) Stroke(Ln, Px + 2.4f, L.Casing);
					continue;
				}
				const Rgba C = Water ? L.Water : Ln.Kind == RoadKind::Rail ? L.Rail : Ln.Kind == RoadKind::Track || Ln.Kind == RoadKind::Path ? L.Track
					: Ln.Kind == RoadKind::Motorway ? L.Motorway : Ln.Kind == RoadKind::Main ? L.Main : L.Road;
				Stroke(Ln, Px, C);
			}
	// The ride's way.
	if (Scene.Route.size() >= 2)
	{
		Pts.clear();
		const double Reach2 = Reach * Reach * 1.3;
		for (const MapPt& Q : Scene.Route)
		{
			const double DX = Q.X - Scene.Bike.X, DZ = Q.Z - Scene.Bike.Z;
			if (DX * DX + DZ * DZ > Reach2)
			{
				if (Pts.size() >= 2)
				{
					P.Path(Pts, 9.0f, L.RouteEdge);
					P.Path(Pts, 6.0f, L.Route);
				}
				Pts.clear();
				continue;
			}
			Pts.push_back(To(Q));
		}
		if (Pts.size() >= 2)
		{
			P.Path(Pts, 9.0f, L.RouteEdge);
			P.Path(Pts, 6.0f, L.Route);
		}
	}
	// Road names: each once, where its road passes nearest the middle of the view.
	if (Close)
	{
		struct Named { const std::string* Name; P2 At; float Far2; };
		std::vector<Named> Names;
		const float MX = X + W * 0.5f, MY = Y + H * 0.5f;
		for (int I : NearLines)
		{
			const MapLine& Ln = Scene.Data->Lines[size_t(I)];
			if (Ln.Name.empty() || int(Ln.Kind) > int(RoadKind::Minor)) continue;
			P2 Best;
			float BestD = 1.0e12f;
			for (size_t K = 0; K + 1 < Ln.Pts.size(); ++K)
			{
				const P2 A = To(Ln.Pts[K]), B = To(Ln.Pts[K + 1]);
				const P2 M((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f);
				if (M.X < X + 30.0f || M.X > X + W - 30.0f || M.Y < Y + 18.0f || M.Y > Y + H - 10.0f) continue;
				const float D = (M.X - MX) * (M.X - MX) + (M.Y - MY) * (M.Y - MY);
				if (D < BestD)
				{
					BestD = D;
					Best = M;
				}
			}
			if (BestD > 1.0e11f) continue;
			bool Had = false;
			for (Named& N : Names)
				if (*N.Name == Ln.Name)
				{
					Had = true;
					if (BestD < N.Far2)
					{
						N.Far2 = BestD;
						N.At = Best;
					}
				}
			if (!Had) Names.push_back({&Ln.Name, Best, BestD});
		}
		std::sort(Names.begin(), Names.end(), [](const Named& A, const Named& B) { return A.Far2 < B.Far2; });
		const float Px = std::max(11.0f, H * 0.036f);
		std::vector<P2> Used;
		int Shown = 0;
		for (const Named& N : Names)
		{
			if (Shown >= 7) break;
			bool Crowded = false;
			for (const P2& U : Used)
				if (std::fabs(U.X - N.At.X) < Px * 7.0f && std::fabs(U.Y - N.At.Y) < Px * 1.5f) Crowded = true;
			if (Crowded) continue;
			P.Text(*N.Name, N.At.X + 1.0f, N.At.Y + Px * 0.36f + 1.0f, Px, L.Halo, Align::Centre);
			P.Text(*N.Name, N.At.X, N.At.Y + Px * 0.36f, Px, L.InkSoft, Align::Centre);
			Used.push_back(N.At);
			++Shown;
		}
	}
	// Places, fuel and cafes.
	{
		const float Px = std::max(12.0f, H * 0.042f);
		for (int I : NearLabels)
		{
			const MapLabel& Lb = Scene.Data->Labels[size_t(I)];
			const P2 At = To(Lb.At);
			if (At.X < X + 8.0f || At.X > X + W - 8.0f || At.Y < Y + 8.0f || At.Y > Y + H - 8.0f) continue;
			if (Lb.Kind == LabelKind::Town || Lb.Kind == LabelKind::Village)
			{
				if (Lb.Kind == LabelKind::Village && Far) continue;
				const float S = Lb.Kind == LabelKind::Town ? Px * 1.25f : Px;
				P.Text(Lb.Name, At.X + 1.0f, At.Y + 1.0f, S, L.Halo, Align::Centre, true);
				P.Text(Lb.Name, At.X, At.Y, S, L.Ink, Align::Centre, true);
				continue;
			}
			if (!Close) continue;
			const Rgba Pin = Lb.Kind == LabelKind::Fuel ? Rgba(0.30f, 0.50f, 0.95f) : Lb.Kind == LabelKind::Cafe ? Rgba(0.96f, 0.56f, 0.20f) : Rgba(0.55f, 0.40f, 0.75f);
			const float R = std::max(6.0f, H * 0.022f);
			P.Disc(At.X, At.Y, R + 1.5f, Rgba(1.0f, 1.0f, 1.0f));
			P.Disc(At.X, At.Y, R, Pin);
			// (a pump's nozzle, a cup's handle, a landmark's dot: a hint, at this size)
			if (Lb.Kind == LabelKind::Fuel) P.Rect(At.X - R * 0.35f, At.Y - R * 0.5f, R * 0.7f, R, Rgba(1.0f, 1.0f, 1.0f));
			else if (Lb.Kind == LabelKind::Cafe) P.Pie(At.X, At.Y - R * 0.2f, R * 0.55f, 0.0f, 180.0f, Rgba(1.0f, 1.0f, 1.0f));
			else P.Disc(At.X, At.Y, R * 0.35f, Rgba(1.0f, 1.0f, 1.0f));
			if (!Lb.Name.empty() && Scene.Metres < 900.0)
			{
				P.Text(Lb.Name, At.X + R + 5.0f, At.Y + Px * 0.3f, Px * 0.85f, L.InkSoft);
			}
		}
	}
	// The finish, waypoints, the other riders, and the bike.
	auto Flag = [&](const MapPt& Where, const Rgba& C) {
		const P2 At = To(Where);
		const float S = std::max(9.0f, H * 0.035f);
		P.Rect(At.X - 1.2f, At.Y - S * 1.8f, 2.4f, S * 1.8f, L.Ink);
		P.Fan({P2(At.X + 1.2f, At.Y - S * 1.8f), P2(At.X + S * 1.1f, At.Y - S * 1.35f), P2(At.X + 1.2f, At.Y - S * 0.9f)}, C);
	};
	for (const MapPt& Wp : Scene.Waypoints) Flag(Wp, Rgba(0.96f, 0.63f, 0.17f));
	if (Scene.HasFinish) Flag(Scene.Finish, Rgba(0.90f, 0.20f, 0.18f));
	for (const MapRider& R : Scene.Riders)
	{
		const P2 At = To(R.At);
		const float S = std::max(6.0f, H * 0.02f);
		P.Disc(At.X, At.Y, S + 2.0f, Rgba(1.0f, 1.0f, 1.0f));
		P.Disc(At.X, At.Y, S, R.Colour);
		if (!R.Name.empty()) P.Text(R.Name, At.X, At.Y - S - 5.0f, std::max(11.0f, H * 0.036f), L.Ink, Align::Centre, true);
	}
	{
		// The bike: an arrowhead, pointing the way it heads.
		const double Turn = Scene.NorthUp ? Scene.Heading : 0.0;
		const float S = std::max(11.0f, std::min(W, H) * 0.045f);
		const float C = float(std::cos(Turn)), Sn = float(std::sin(Turn));
		auto Rot = [&](float U, float V) { return P2(CX + U * C - V * Sn, CY + U * Sn + V * C); };
		P.Disc(CX, CY, S * 1.5f, L.Route.Fade(0.22f));
		P.Fan({Rot(0.0f, -S * 1.15f), Rot(S * 0.85f, S * 0.9f), Rot(0.0f, S * 0.45f), Rot(-S * 0.85f, S * 0.9f)}, Rgba(1.0f, 1.0f, 1.0f));
		P.Fan({Rot(0.0f, -S * 0.9f), Rot(S * 0.62f, S * 0.7f), Rot(0.0f, S * 0.32f), Rot(-S * 0.62f, S * 0.7f)}, L.Route);
	}
	P.NoClip();
}
} // namespace TorqueTft
