// [race tracks] Race circuits on a ride: their paint, their ground, their notes and the lap timer
// (WorldRideCircuits.h).
#include "WorldRideCircuits.h"

#include "WorldFiles.h"
#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
#include <limits>
#include <set>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <mutex>

namespace TorqueWorld
{
using TorqueFit::Json;

namespace
{
std::mutex NotesLock;
std::shared_ptr<const CircuitNotes> NotesHeld;

std::string Lower(std::string S)
{
	for (char& C : S) C = char(std::tolower(static_cast<unsigned char>(C)));
	return S;
}

double MetresBetween(const Geo& A, const Geo& B)
{
	const double K = std::cos((A.Lat + B.Lat) * 0.5 * 3.141592653589793 / 180.0);
	return std::hypot((A.Lat - B.Lat) * 111320.0, (A.Lon - B.Lon) * 111320.0 * K);
}

bool GeoOf(const Json* V, Geo& Out)
{
	if (V == nullptr || !V->IsArray() || V->A.size() < 2) return false;
	Out.Lat = V->A[0].N;
	Out.Lon = V->A[1].N;
	return true;
}

bool InRing(const std::vector<P2>& Ring, double X, double Z)
{
	bool In = false;
	for (size_t I = 0, J = Ring.size() - 1; I < Ring.size(); J = I++)
	{
		const P2 &A = Ring[I], &B = Ring[J];
		if ((A.Z > Z) != (B.Z > Z) && X < (B.X - A.X) * (Z - A.Z) / (B.Z - A.Z) + A.X) In = !In;
	}
	return In;
}

double HalfOn(const std::vector<RoadPoint>& Pts, double Along)
{
	if (Pts.empty()) return 0.0;
	size_t Lo = 0, Hi = Pts.size();
	while (Hi - Lo > 1)
	{
		const size_t Mid = (Lo + Hi) / 2;
		if (Pts[Mid].Along <= Along) Lo = Mid;
		else Hi = Mid;
	}
	if (Lo + 1 >= Pts.size()) return Pts.back().Half;
	const double Span = Pts[Lo + 1].Along - Pts[Lo].Along;
	const double T = Span > 1e-9 ? std::clamp((Along - Pts[Lo].Along) / Span, 0.0, 1.0) : 0.0;
	return Pts[Lo].Half + (Pts[Lo + 1].Half - Pts[Lo].Half) * T;
}

// Whether segment P0-P1 crosses segment Q0-Q1.
bool Crosses(const P2& P0, const P2& P1, const P2& Q0, const P2& Q1)
{
	const auto Side = [](const P2& A, const P2& B, const P2& C) { return (B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X); };
	const double D1 = Side(Q0, Q1, P0), D2 = Side(Q0, Q1, P1), D3 = Side(P0, P1, Q0), D4 = Side(P0, P1, Q1);
	return ((D1 > 0.0) != (D2 > 0.0)) && ((D3 > 0.0) != (D4 > 0.0));
}

constexpr float KerbRed[3] = {0.42f, 0.025f, 0.025f};
constexpr float KerbWhite[3] = {0.83f, 0.83f, 0.78f};

RidePlan::RoadMark PaintMark(double From, double To, double Left, double Right, const float* Colour)
{
	RidePlan::RoadMark M;
	M.Kind = RidePlan::MarkPaint;
	M.From = From;
	M.To = To;
	M.Left = Left;
	M.Right = Right;
	for (int C = 0; C < 3; ++C) M.Colour[C] = Colour[C];
	return M;
}
} // namespace

// ----- Data/RideUK/circuits.json -----

const CircuitNote* CircuitNotes::Find(const std::string& Name, const Geo& At) const
{
	const std::string Low = Lower(Name);
	const CircuitNote* Near = nullptr;
	double NearD = 3000.0;
	for (const CircuitNote& N : Circuits)
	{
		if (N.Road) continue;
		for (const std::string& W : N.Match)
			if (!W.empty() && Low.find(Lower(W)) != std::string::npos && MetresBetween(N.At, At) < 15000.0) return &N;
		const double D = MetresBetween(N.At, At);
		if (D < NearD)
		{
			NearD = D;
			Near = &N;
		}
	}
	return Near;
}

bool ReadCircuitNotes(const std::string& Text, CircuitNotes& Out)
{
	Json Root;
	if (!Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const Json* List = Root.Get("circuits");
	if (List == nullptr || !List->IsArray()) return false;
	Out = CircuitNotes();
	for (const Json& C : List->A)
	{
		if (!C.IsObject()) continue;
		CircuitNote N;
		N.Name = C.Str("name");
		if (const Json* M = C.Get("match"))
			for (const Json& W : M->A) N.Match.push_back(W.S);
		N.At.Lat = C.Num("lat");
		N.At.Lon = C.Num("lon");
		GeoOf(C.Get("start"), N.Start);
		if (const Json* S = C.Get("sectors"))
			for (const Json& P : S->A)
			{
				Geo G;
				if (!GeoOf(&P, G)) continue;
				N.Sectors.push_back(G);
				N.SectorNames.push_back(P.A.size() > 2 ? P.A[2].S : std::string());
			}
		N.Clockwise = C.Str("direction", "clockwise") != "anticlockwise";
		N.Road = C.Str("kind") == "road";
		if (const Json* S = C.Get("gates"))
			for (const Json& P : S->A)
			{
				Geo G;
				if (!GeoOf(&P, G)) continue;
				N.Gates.push_back(G);
				N.GateNames.push_back(P.A.size() > 2 ? P.A[2].S : std::string());
			}
		if (const Json* S = C.Get("corners"))
			for (const Json& P : S->A)
			{
				Geo G;
				if (!GeoOf(&P, G)) continue;
				N.Corners.push_back(G);
				N.CornerNames.push_back(P.A.size() > 2 ? P.A[2].S : std::string());
				N.CornerBank.push_back(P.A.size() > 3 && P.A[3].IsNumber() ? P.A[3].N : 0.0);
			}
		N.Test = C.Flag("test");
		N.About = C.Str("about");
		if (!N.Name.empty()) Out.Circuits.push_back(std::move(N));
	}
	return true;
}

bool SetCircuitNotes(const std::string& Text)
{
	std::shared_ptr<CircuitNotes> N = std::make_shared<CircuitNotes>();
	if (!ReadCircuitNotes(Text, *N)) return false;
	std::lock_guard<std::mutex> Hold(NotesLock);
	NotesHeld = N;
	return true;
}

std::shared_ptr<const CircuitNotes> CircuitNotesNow()
{
	std::lock_guard<std::mutex> Hold(NotesLock);
	return NotesHeld;
}

long long CircuitIndex::KeyOf(double X, double Z)
{
	return static_cast<long long>(std::floor(X / Square)) * 4000037LL + static_cast<long long>(std::floor(Z / Square));
}

std::string LapText(double Seconds)
{
	if (!(Seconds >= 0.0) || Seconds > 35999.0) return "-:--.---";
	const int Ms = int(std::lround(Seconds * 1000.0));
	char Buf[32];
	if (Ms >= 3600000) std::snprintf(Buf, sizeof(Buf), "%d:%02d:%02d.%03d", Ms / 3600000, (Ms / 60000) % 60, (Ms / 1000) % 60, Ms % 1000);
	else std::snprintf(Buf, sizeof(Buf), "%d:%02d.%03d", Ms / 60000, (Ms / 1000) % 60, Ms % 1000);
	return Buf;
}

// ----- The ride's circuits -----

bool RidePlan::CircuitRoad(int Which) const
{
	return Which >= 1 && Area.Roads.CircuitEdge(Which - 1);
}

void RidePlan::IndexCircuits()
{
	const Region& G = Area.Roads;
	std::shared_ptr<CircuitIndex> Ix = std::make_shared<CircuitIndex>();
	for (size_t A = 0; A < G.CircuitAreas.size(); ++A)
	{
		const CircuitArea& Ar = G.CircuitAreas[A];
		if (Ar.Kind < 5 || Ar.Kind > 8 || Ar.Kind == 7) continue;   // (gravel, sand and paved run-off: grass is grass)
		double X0 = 1e18, X1 = -1e18, Z0 = 1e18, Z1 = -1e18;
		for (const P2& P : Ar.Ring)
		{
			X0 = std::fmin(X0, P.X); X1 = std::fmax(X1, P.X);
			Z0 = std::fmin(Z0, P.Z); Z1 = std::fmax(Z1, P.Z);
		}
		// (an area far bigger than a gravel trap is the map's mistake, or the whole site: left out)
		if (X1 - X0 > 1500.0 || Z1 - Z0 > 1500.0) continue;
		for (double Z = std::floor(Z0 / CircuitIndex::Square) * CircuitIndex::Square; Z <= Z1; Z += CircuitIndex::Square)
			for (double X = std::floor(X0 / CircuitIndex::Square) * CircuitIndex::Square; X <= X1; X += CircuitIndex::Square)
				Ix->Areas[CircuitIndex::KeyOf(X + 1.0, Z + 1.0)].push_back(int(A));
	}
	// Each circuit's note, and its start and sector lines where they fall on its track.
	const std::shared_ptr<const CircuitNotes> Notes = CircuitNotesNow();
	const Frame F = G.FrameOf();
	Ix->Notes.assign(G.Circuits.size(), nullptr);
	for (size_t C = 0; C < G.Circuits.size(); ++C)
	{
		const CircuitInfo& Info = G.Circuits[C];
		const CircuitNote* Note = Notes ? Notes->Find(Info.Name, F.ToGeo(Info.At.X, Info.At.Z)) : nullptr;
		if (Note != nullptr) Ix->Notes[C] = std::make_shared<const CircuitNote>(*Note);
		// (the nearest point of the circuit's track to a place: its line across there)
		const auto OnTrack = [&](const P2& Want, double Within, CircuitIndex::Line& Out)
		{
			double Best = Within;
			bool Found = false;
			for (int E = G.CircuitFirst; E >= 0 && E < int(G.Edges.size()); ++E)
			{
				if (!G.CircuitEdge(E) || G.CircuitOf[size_t(E - G.CircuitFirst)] != int(C) || G.ClassOf(E) != "raceway") continue;
				const std::vector<P2>& L = G.Edges[size_t(E)].Line;
				for (size_t S = 0; S + 1 < L.size(); ++S)
				{
					const P2 D = L[S + 1] - L[S];
					const double Len = D.Length();
					if (Len < 1e-6) continue;
					const double T = std::clamp(((Want.X - L[S].X) * D.X + (Want.Z - L[S].Z) * D.Z) / (Len * Len), 0.0, 1.0);
					const P2 P = L[S] + D * T;
					const double Dist = P.DistanceTo(Want);
					if (Dist >= Best) continue;
					Best = Dist;
					Found = true;
					Out.At = P;
					Out.Across = P2(-D.Z / Len, D.X / Len);
					const RoadExtra* X = G.ExtraOf(E);
					Out.Half = (X != nullptr && X->Width > 0.0 ? X->Width : Region::TrackWidth) * 0.5;
				}
			}
			return Found;
		};
		CircuitIndex::Line Start;
		Start.Circuit = int(C);
		Start.Kind = 0;
		Start.Name = "Start";
		bool HaveStart = false;
		if (Note != nullptr && Note->Start.Lat != 0.0) HaveStart = OnTrack(F.FromGeo(Note->Start.Lat, Note->Start.Lon), 150.0, Start);
		if (!HaveStart)
		{
			// (no note: beside the middle of its pit lane, else its first piece of track's middle)
			P2 Want = Info.At;
			bool Pit = false;
			for (int E = G.CircuitFirst; E >= 0 && E < int(G.Edges.size()) && !Pit; ++E)
				if (G.CircuitEdge(E) && G.CircuitOf[size_t(E - G.CircuitFirst)] == int(C) && G.ClassOf(E) == "pit_lane")
				{
					const std::vector<P2>& L = G.Edges[size_t(E)].Line;
					Want = L[L.size() / 2];
					Pit = true;
				}
			HaveStart = OnTrack(Want, Pit ? 120.0 : 3000.0, Start);
		}
		if (HaveStart) Ix->Lines.push_back(Start);
		// (no sectors in the note: a third and two thirds of the way round its lap, the lap walked from the start
		// line along its track, straight on at every fork, back to the start)
		if (HaveStart && (Note == nullptr || Note->Sectors.empty()))
		{
			int E0 = -1;
			double Best = 1.0;
			for (int E = G.CircuitFirst; E >= 0 && E < int(G.Edges.size()); ++E)
			{
				if (!G.CircuitEdge(E) || G.CircuitOf[size_t(E - G.CircuitFirst)] != int(C) || G.ClassOf(E) != "raceway") continue;
				const std::vector<P2>& L = G.Edges[size_t(E)].Line;
				for (size_t S = 0; S + 1 < L.size(); ++S)
				{
					const P2 D = L[S + 1] - L[S];
					const double L2 = D.X * D.X + D.Z * D.Z;
					if (L2 < 1e-12) continue;
					const double T = std::clamp(((Start.At.X - L[S].X) * D.X + (Start.At.Z - L[S].Z) * D.Z) / L2, 0.0, 1.0);
					const double Dist = (L[S] + D * T).DistanceTo(Start.At);
					if (Dist < Best) { Best = Dist; E0 = E; }
				}
			}
			std::vector<P2> Walk;
			bool Round = false;
			if (E0 >= 0)
			{
				// (the way it's raced from the line: clockwise has the circuit's middle on the right)
				int Edge = E0, Node = G.Edges[size_t(E0)].B;
				Walk = G.Edges[size_t(E0)].Line;
				{
					const P2 D = Walk.back() - Walk.front(), V = Info.At - Start.At;
					if (((D.X * V.Z - D.Z * V.X) > 0.0) != (Note != nullptr ? Note->Clockwise : true))
					{
						std::reverse(Walk.begin(), Walk.end());
						Node = G.Edges[size_t(E0)].A;
					}
				}
				for (int Step = 0; Step < 400; ++Step)
				{
					const P2 In = G.Edges[size_t(Edge)].A == Node ? G.Edges[size_t(Edge)].Line[0] - G.Edges[size_t(Edge)].Line[1]
						: G.Edges[size_t(Edge)].Line.back() - G.Edges[size_t(Edge)].Line[G.Edges[size_t(Edge)].Line.size() - 2];
					int Next = -1;
					double Straightest = -2.0;
					for (int E : G.Adjacent[size_t(Node)])
					{
						if (E == Edge || !G.CircuitEdge(E) || G.ClassOf(E) != "raceway" || G.CircuitOf[size_t(E - G.CircuitFirst)] != int(C)) continue;
						const std::vector<P2> L = G.LineFrom(E, Node);
						const P2 Out = L[1] - L[0];
						const double Cos = (In.X * Out.X + In.Z * Out.Z) / std::fmax(In.Length() * Out.Length(), 1e-9);
						if (Cos > Straightest) { Straightest = Cos; Next = E; }
					}
					if (Next < 0) break;
					if (Next == E0) { Round = true; break; }
					const std::vector<P2> L = G.LineFrom(Next, Node);
					Walk.insert(Walk.end(), L.begin() + 1, L.end());
					Node = G.OtherEnd(Next, Node);
					Edge = Next;
				}
			}
			double Total = 0.0;
			for (size_t K = 1; K < Walk.size(); ++K) Total += Walk[K].DistanceTo(Walk[K - 1]);
			if (Round && Total > 600.0)
				for (int Sec = 1; Sec <= 2; ++Sec)
				{
					const double Want = Total * Sec / 3.0;
					double Run = 0.0;
					for (size_t K = 1; K < Walk.size(); ++K)
					{
						const double Seg = Walk[K].DistanceTo(Walk[K - 1]);
						if (Run + Seg < Want || Seg < 1e-6) { Run += Seg; continue; }
						const P2 D = (Walk[K] - Walk[K - 1]) * (1.0 / Seg);
						CircuitIndex::Line Sector = Start;
						Sector.Kind = Sec;
						Sector.Name = "Sector " + std::to_string(Sec);
						Sector.At = Walk[K - 1] + D * (Want - Run);
						Sector.Across = P2(-D.Z, D.X);
						Ix->Lines.push_back(Sector);
						break;
					}
				}
		}
		for (size_t S = 0; Note != nullptr && S < Note->Sectors.size(); ++S)
		{
			CircuitIndex::Line Sector;
			Sector.Circuit = int(C);
			Sector.Kind = int(S) + 1;
			Sector.Name = Note->SectorNames[S];
			if (OnTrack(F.FromGeo(Note->Sectors[S].Lat, Note->Sectors[S].Lon), 150.0, Sector)) Ix->Lines.push_back(Sector);
		}
	}
	// (a road course's gate: on the nearest road to its note's place, a main road first, within 600 m; where the
	// area has none yet, the place as noted)
	const auto OnMainRoad = [&G](const P2& Want)
	{
		P2 Best = Want;
		double Bd = 600.0;
		for (size_t E = 0; E < G.Edges.size(); ++E)
		{
			// (a main road preferred: a lesser one is as if 100 m further; the circuits' own roads never)
			const std::string& Class = G.ClassOf(int(E));
			if (G.CircuitFirst >= 0 && int(E) >= G.CircuitFirst) continue;
			const double Lesser = Class == "trunk" || Class == "primary" || Class == "secondary" || Class == "tertiary" ? 0.0 : 100.0;
			const std::vector<P2>& L = G.Edges[E].Line;
			if (L.empty() || std::fabs(L[0].X - Want.X) > 20000.0 || std::fabs(L[0].Z - Want.Z) > 20000.0) continue;
			for (size_t K = 0; K + 1 < L.size(); ++K)
			{
				const P2 D = L[K + 1] - L[K];
				const double L2 = D.X * D.X + D.Z * D.Z;
				const double T = L2 > 1e-12 ? std::clamp(((Want.X - L[K].X) * D.X + (Want.Z - L[K].Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
				const P2 P = L[K] + D * T;
				const double Dist = P.DistanceTo(Want) + Lesser;
				if (Dist < Bd) { Bd = Dist; Best = P; }
			}
		}
		return Best;
	};
	// The road courses whose start is within the ride's area (their gates may be beyond it yet).
	if (Notes)
		for (const CircuitNote& N : Notes->Circuits)
		{
			if (!N.Road || N.Start.Lat == 0.0) continue;
			const P2 S = OnMainRoad(F.FromGeo(N.Start.Lat, N.Start.Lon));
			if (!G.Bounds.Empty() && (S.X < G.Bounds.X0 - 20000.0 || S.X > G.Bounds.X1 + 20000.0 || S.Z < G.Bounds.Z0 - 20000.0 || S.Z > G.Bounds.Z1 + 20000.0)) continue;
			CircuitIndex::Line Gate;
			Gate.Circuit = -1;
			Gate.Kind = -1;
			Gate.Name = N.Name;
			Gate.At = S;
			Gate.Half = CircuitLaps::GateReach;
			Ix->Lines.push_back(Gate);
			std::vector<P2>& Gates = Ix->RoadGates[N.Name];
			for (const Geo& Ga : N.Gates) Gates.push_back(OnMainRoad(F.FromGeo(Ga.Lat, Ga.Lon)));
		}
	IndexTrackside(*Ix);   // (the trackside kit, once the lines are known)
	CircuitIx = Ix;
}

void RidePlan::CircuitSandTile(double X0, double Z0, double Size, int Detail, RideMesh& Out) const
{
	if (!CircuitIx || Detail < 2) return;
	const Region& G = Area.Roads;
	// (M_RideRoad keys this colour to its sand; World/WorldRide's CircuitTint gives the land under it a sand tint)
	static const RideShapes::Paint Sand = {0.205f, 0.152f, 0.083f};
	constexpr double Lift = 0.06, Step = 2.0;
	std::set<int> Here;
	for (double Z = std::floor(Z0 / CircuitIndex::Square) * CircuitIndex::Square; Z < Z0 + Size; Z += CircuitIndex::Square)
		for (double X = std::floor(X0 / CircuitIndex::Square) * CircuitIndex::Square; X < X0 + Size; X += CircuitIndex::Square)
		{
			const auto It = CircuitIx->Areas.find(CircuitIndex::KeyOf(X + 1.0, Z + 1.0));
			if (It != CircuitIx->Areas.end()) Here.insert(It->second.begin(), It->second.end());
		}
	if (Here.empty()) return;
	std::unordered_map<long long, double> Heights;
	const auto At = [&](const P2& P)
	{
		const double Y = LandDrawnAt(P.X, P.Z, &Heights);
		return RideShapes::Pt(P.X, (std::isnan(Y) ? 0.0 : Y) + Lift, P.Z);
	};
	const auto Up = [&](const P2& P)
	{
		const double Hx0 = LandDrawnAt(P.X - 1.0, P.Z, &Heights), Hx1 = LandDrawnAt(P.X + 1.0, P.Z, &Heights);
		const double Hz0 = LandDrawnAt(P.X, P.Z - 1.0, &Heights), Hz1 = LandDrawnAt(P.X, P.Z + 1.0, &Heights);
		if (std::isnan(Hx0) || std::isnan(Hx1) || std::isnan(Hz0) || std::isnan(Hz1)) return RideShapes::Pt(0.0, 1.0, 0.0);
		const double Sx = (Hx1 - Hx0) * 0.5, Sz = (Hz1 - Hz0) * 0.5, L = std::sqrt(Sx * Sx + 1.0 + Sz * Sz);
		return RideShapes::Pt(-Sx / L, 1.0 / L, -Sz / L);
	};
	const auto InTile = [&](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	// (a quad A B C D round its edge, facing up whichever way it was given)
	const auto Quad = [&](const P2& A, const P2& B, const P2& C, const P2& D)
	{
		if (!InTile((A + B + C + D) * 0.25)) return;
		const uint32_t First = uint32_t(Out.Vertices());
		for (const P2* P : {&A, &B, &C, &D}) RideShapes::Vert(Out, At(*P), Up(*P), Sand, P->X / 8.0, P->Z / 8.0);
		const double Turn = (B.X - A.X) * (C.Z - A.Z) - (B.Z - A.Z) * (C.X - A.X);
		if (Turn > 0.0) Out.Idx.insert(Out.Idx.end(), {First, First + 2, First + 1, First, First + 3, First + 2});
		else Out.Idx.insert(Out.Idx.end(), {First, First + 1, First + 2, First, First + 2, First + 3});
	};
	for (const int A : Here)
	{
		const CircuitArea& Ar = CircuitIx->AreaOf(G, A);
		if (Ar.Kind != 5 && Ar.Kind != 6) continue;   // (gravel and sand; paved run-off is tarmac)
		if (size_t(A) >= G.CircuitAreas.size())
		{
			// A trap the game shaped: a strip, its inner edge then its outer back; laid in 2 m steps across.
			const size_t N = Ar.Ring.size() / 2;
			for (size_t I = 0; I + 1 < N; ++I)
			{
				const P2 I0 = Ar.Ring[I], I1 = Ar.Ring[I + 1], O0 = Ar.Ring[Ar.Ring.size() - 1 - I], O1 = Ar.Ring[Ar.Ring.size() - 2 - I];
				const int Across = std::max(1, int(std::ceil(std::fmax(I0.DistanceTo(O0), I1.DistanceTo(O1)) / Step)));
				for (int K = 0; K < Across; ++K)
				{
					const double T0 = double(K) / Across, T1 = double(K + 1) / Across;
					Quad(I0 + (O0 - I0) * T0, I1 + (O1 - I1) * T0, I1 + (O1 - I1) * T1, I0 + (O0 - I0) * T1);
				}
			}
			continue;
		}
		// The map's own: 2 m squares wherever their middle is in it.
		double Ax0 = 1e18, Ax1 = -1e18, Az0 = 1e18, Az1 = -1e18;
		for (const P2& P : Ar.Ring) { Ax0 = std::fmin(Ax0, P.X); Ax1 = std::fmax(Ax1, P.X); Az0 = std::fmin(Az0, P.Z); Az1 = std::fmax(Az1, P.Z); }
		Ax0 = std::fmax(Ax0, X0); Ax1 = std::fmin(Ax1, X0 + Size); Az0 = std::fmax(Az0, Z0); Az1 = std::fmin(Az1, Z0 + Size);
		for (double Z = std::floor(Az0 / Step) * Step; Z < Az1; Z += Step)
			for (double X = std::floor(Ax0 / Step) * Step; X < Ax1; X += Step)
				if (RideShapes::InRing(Ar.Ring, X + Step * 0.5, Z + Step * 0.5))
					Quad(P2(X, Z), P2(X + Step, Z), P2(X + Step, Z + Step), P2(X, Z + Step));
	}
}

double RidePlan::CircuitGround(double X, double Z) const
{
	// (the point and two rings round it, 12 and 30 m out: a 25-point mean, NaN-free)
	double Sum = 0.0, Weight = 0.0;
	const auto Take = [&](double Px, double Pz, double W)
	{
		const double Y = Land->Height(Px, Pz);
		if (std::isnan(Y)) return;
		Sum += Y * W;
		Weight += W;
	};
	Take(X, Z, 2.0);
	for (int K = 0; K < 12; ++K)
	{
		const double A = double(K) * (3.14159265358979 / 6.0);
		Take(X + std::cos(A) * 12.0, Z + std::sin(A) * 12.0, 1.0);
		Take(X + std::cos(A + 0.26) * 30.0, Z + std::sin(A + 0.26) * 30.0, 0.6);
	}
	return Weight > 0.0 ? Sum / Weight : std::numeric_limits<double>::quiet_NaN();
}

void CircuitIndex::AddArea(const Region& G, CircuitArea&& Ar)
{
	double X0 = 1e18, X1 = -1e18, Z0 = 1e18, Z1 = -1e18;
	for (const P2& P : Ar.Ring)
	{
		X0 = std::fmin(X0, P.X); X1 = std::fmax(X1, P.X);
		Z0 = std::fmin(Z0, P.Z); Z1 = std::fmax(Z1, P.Z);
	}
	const int A = int(G.CircuitAreas.size() + Made.size());
	Made.push_back(std::move(Ar));
	for (double Z = std::floor(Z0 / Square) * Square; Z <= Z1; Z += Square)
		for (double X = std::floor(X0 / Square) * Square; X <= X1; X += Square)
			Areas[KeyOf(X + 1.0, Z + 1.0)].push_back(A);
}

bool RidePlan::CircuitTint(double X, double Z, float* Rgb) const
{
	const int Kind = CircuitGroundAt(X, Z);
	if (Kind < 0) return false;
	// (a gravel trap's sand (build 168: beenster wants them to look like sand); paved run-off a little lighter than the track)
	static constexpr float Gravel[3] = {0.205f, 0.152f, 0.083f}, Paved[3] = {0.035f, 0.035f, 0.038f};
	const float* C = Kind == 2 ? Gravel : Paved;
	for (int I = 0; I < 3; ++I) Rgb[I] = C[I];
	return true;
}

int RidePlan::CircuitGroundAt(double X, double Z) const
{
	if (!CircuitIx) return -1;
	const auto It = CircuitIx->Areas.find(CircuitIndex::KeyOf(X, Z));
	if (It == CircuitIx->Areas.end()) return -1;
	for (int A : It->second)
	{
		const CircuitArea& Ar = CircuitIx->AreaOf(Area.Roads, A);
		if (!InRing(Ar.Ring, X, Z)) continue;
		return Ar.Kind == 8 ? 1 : 2;
	}
	return -1;
}

void RidePlan::CircuitMarks(int Which, double From, double To, std::vector<RoadMark>& Out) const
{
	Out.clear();
	const std::vector<RoadPoint>& Pts = RoadOf(Which);
	if (Pts.size() < 2 || Which < 1) return;
	const Region& G = Area.Roads;
	const int Edge = Which - 1;
	const bool Pit = G.ClassOf(Edge) == "pit_lane";
	const double Len = Pts.back().Along;
	// The pit lane: a line across at each end, where it leaves the track and joins it again.
	if (Pit)
	{
		for (const double At : {0.6, Len - 1.2})
		{
			if (At < 0.0 || At + 0.6 > Len || At + 0.6 < From || At > To) continue;
			const double Half = HalfOn(Pts, At);
			Out.push_back(PaintMark(At, At + 0.6, Half, -Half, KerbWhite));
		}
		return;
	}
	// Its corners (build 168, beenster on 167: "Tidy up the red and white kerbs to match the really smooth
	// corners"): its heading's turn over 15 m either side of each point, a corner where that is tighter than
	// KerbRadius and on until it eases past LooseRadius (so a long bend is one corner, not a broken run),
	// corners of the same hand under 25 m apart joined, and only those that turn 15 degrees or more kerbed:
	// a red and white kerb along the inside from entry to exit, and along the outside from the apex out past the
	// exit; the blocks counted from each kerb's own start, so every kerb begins and ends whole.
	constexpr double KerbRadius = 160.0, LooseRadius = 260.0, Reach = 15.0, Block = 1.5, KerbWide = 1.1;
	constexpr double LineWide = 0.12, LineIn = 0.15;   // the white edge line: 12 cm, 15 cm in from the edge (or the kerb)
	std::vector<double> Turn(Pts.size(), 0.0);
	for (size_t I = 0, J0 = 0, J1 = 0; I < Pts.size(); ++I)
	{
		while (J0 + 1 < I && Pts[I].Along - Pts[J0 + 1].Along >= Reach) ++J0;
		while (J1 < Pts.size() - 1 && Pts[J1].Along - Pts[I].Along < Reach) ++J1;
		if (J0 >= I || J1 <= I) continue;
		const P2 A = Pts[I].At - Pts[J0].At, B = Pts[J1].At - Pts[I].At;
		const double Angle = std::atan2(A.X * B.Z - A.Z * B.X, A.X * B.X + A.Z * B.Z);
		const double Over = Pts[J1].Along - Pts[J0].Along;
		Turn[I] = Over > 1e-6 ? Angle / Over : 0.0;   // (radians a metre: + a right-hand bend as the points run)
	}
	struct Bend { double A0, A1, Apex, Sign, Total; };
	std::vector<Bend> Bends;
	for (size_t I = 0; I < Pts.size();)
	{
		if (std::fabs(Turn[I]) < 1.0 / KerbRadius) { ++I; continue; }
		const double Sign = Turn[I] > 0.0 ? 1.0 : -1.0;
		size_t J = I, Apex = I;
		double Total = 0.0;
		while (J < Pts.size() && Turn[J] * Sign >= 1.0 / LooseRadius)
		{
			if (std::fabs(Turn[J]) > std::fabs(Turn[Apex])) Apex = J;
			if (J > I) Total += std::fabs(Turn[J]) * (Pts[J].Along - Pts[J - 1].Along);
			++J;
		}
		const double A0 = Pts[I].Along, A1 = Pts[std::min(J, Pts.size() - 1)].Along;
		if (!Bends.empty() && Bends.back().Sign == Sign && A0 - Bends.back().A1 < 25.0)
		{
			Bend& Prev = Bends.back();
			if (std::fabs(Turn[Apex]) > 0.0 && Total > Prev.Total) Prev.Apex = Pts[Apex].Along;
			Prev.A1 = A1;
			Prev.Total += Total;
		}
		else Bends.push_back({A0, A1, Pts[Apex].Along, Sign, Total});
		I = J;
	}
	// (each side's kerbs, as spans along, for the edge line to step in round them)
	std::vector<std::pair<double, double>> Kerbed[2];
	const auto Kerb = [&](double A0, double A1, double Side)
	{
		A0 = std::fmax(A0, 0.5);
		A1 = std::fmin(A1, Len - 0.5);
		if (A1 - A0 < Block * 2.0) return;
		// (a whole number of blocks, so it ends as it began)
		const int Blocks = std::max(2, int(std::lround((A1 - A0) / Block)));
		const double Each = (A1 - A0) / Blocks;
		Kerbed[Side > 0.0 ? 0 : 1].push_back({A0, A1});
		for (int K = 0; K < Blocks; ++K)
		{
			const double U0 = A0 + Each * K, U1 = U0 + Each;
			if (U1 < From || U0 > To) continue;
			const double Half = HalfOn(Pts, (U0 + U1) * 0.5);
			const double Outer = Side * Half, Inner = Side * (Half - KerbWide);
			Out.push_back(PaintMark(U0, U1, std::fmax(Outer, Inner), std::fmin(Outer, Inner), (K & 1) == 0 ? KerbRed : KerbWhite));
		}
	};
	for (const Bend& B : Bends)
	{
		if (B.Total < 15.0 * 3.14159265358979 / 180.0 || B.A1 - B.A0 < 8.0) continue;
		// (+ offsets are the road's left as its points run: a right-hand bend's inside is its right)
		const double Inside = B.Sign > 0.0 ? -1.0 : 1.0;
		Kerb(B.A0 - 4.0, B.A1 + 4.0, Inside);
		Kerb(B.Apex, B.A1 + 18.0, -Inside);
	}
	// The white edge line each side, the whole way: 15 cm in from the edge, or that in from a kerb's inner edge
	// where there is one (stepping in and out square at the kerb's ends).
	for (int Sd = 0; Sd < 2; ++Sd)
	{
		const double Side = Sd == 0 ? 1.0 : -1.0;
		std::vector<std::pair<double, double>>& Ks = Kerbed[Sd];
		std::sort(Ks.begin(), Ks.end());
		// (the stretches with no kerb, and the kerbed ones)
		std::vector<std::pair<std::pair<double, double>, bool>> Runs;
		double At = 0.0;
		for (const auto& K : Ks)
		{
			if (K.second <= At) continue;
			if (K.first > At) Runs.push_back({{At, K.first}, false});
			Runs.push_back({{std::fmax(At, K.first), K.second}, true});
			At = K.second;
		}
		if (At < Len) Runs.push_back({{At, Len}, false});
		for (const auto& R : Runs)
		{
			const double U0 = std::fmax(R.first.first, From), U1 = std::fmin(R.first.second, To);
			if (U1 - U0 < 0.05) continue;
			const double In = LineIn + (R.second ? KerbWide : 0.0);
			// (in pieces of up to 6 m, each at the track's own width there)
			const int Pieces = std::max(1, int(std::ceil((U1 - U0) / 6.0)));
			for (int K = 0; K < Pieces; ++K)
			{
				const double P0 = U0 + (U1 - U0) * K / Pieces, P1 = U0 + (U1 - U0) * (K + 1) / Pieces;
				const double Half = HalfOn(Pts, (P0 + P1) * 0.5);
				const double Outer = Side * (Half - In), Inner = Side * (Half - In - LineWide);
				Out.push_back(PaintMark(P0, P1, std::fmax(Outer, Inner), std::fmin(Outer, Inner), KerbWhite));
			}
		}
	}
	// The start and finish line across it, where the circuit's is on this piece.
	if (CircuitIx)
		for (const CircuitIndex::Line& L : CircuitIx->Lines)
		{
			if (L.Kind != 0) continue;
			double Best = L.Half + 1.0, At = -1.0;
			for (size_t S = 0; S + 1 < Pts.size(); ++S)
			{
				const P2 D = Pts[S + 1].At - Pts[S].At;
				const double L2 = D.X * D.X + D.Z * D.Z;
				if (L2 < 1e-12) continue;
				const double T = std::clamp(((L.At.X - Pts[S].At.X) * D.X + (L.At.Z - Pts[S].At.Z) * D.Z) / L2, 0.0, 1.0);
				const double Dist = (Pts[S].At + D * T).DistanceTo(L.At);
				if (Dist < Best)
				{
					Best = Dist;
					At = Pts[S].Along + (Pts[S + 1].Along - Pts[S].Along) * T;
				}
			}
			if (At < 0.0 || At + 0.4 < From || At - 0.4 > To) continue;
			const double Half = HalfOn(Pts, At);
			Out.push_back(PaintMark(At - 0.4, At + 0.4, Half, -Half, KerbWhite));
		}
	std::sort(Out.begin(), Out.end(), [](const RoadMark& A, const RoadMark& B) { return A.From < B.From; });
}

void RidePlan::CircuitBanks(int Edge, const std::vector<P2>& Line, std::vector<double>& Banks) const
{
	const Region& G = Area.Roads;
	if (!G.CircuitEdge(Edge) || G.ClassOf(Edge) != "raceway" || Line.size() < 5 || Banks.size() != Line.size()) return;
	const std::shared_ptr<const CircuitNotes> Notes = CircuitNotesNow();
	if (!Notes) return;
	const Frame F = G.FrameOf();
	const CircuitInfo& Info = G.Circuits[size_t(G.CircuitOf[size_t(Edge - G.CircuitFirst)])];
	const CircuitNote* Note = Notes->Find(Info.Name, F.ToGeo(Info.At.X, Info.At.Z));
	if (Note == nullptr || Note->Corners.empty()) return;
	for (size_t K = 0; K < Note->Corners.size(); ++K)
	{
		const P2 At = F.FromGeo(Note->Corners[K].Lat, Note->Corners[K].Lon);
		const double Want = Note->CornerBank[K] / 100.0;
		for (size_t I = 2; I + 2 < Line.size(); ++I)
		{
			const double D = Line[I].DistanceTo(At);
			if (D >= CircuitNote::CornerReach) continue;
			// (which way it bends here: + right, so the outside, the left, is the higher for a bank)
			const P2 A = Line[I - 2], B = Line[I], C = Line[I + 2];
			const double Cross = (B.X - A.X) * (C.Z - B.Z) - (B.Z - A.Z) * (C.X - B.X);
			const double Sign = Cross > 0.0 ? 1.0 : Cross < 0.0 ? -1.0 : (Banks[I] >= 0.0 ? 1.0 : -1.0);
			const double U = 1.0 - D / CircuitNote::CornerReach;
			const double W = U * U * (3.0 - 2.0 * U);
			Banks[I] = Banks[I] * (1.0 - W) + Sign * Want * W;
		}
	}
}

// ----- The trackside kit -----

namespace
{
// (every circuit road, the paddock's links too, by 32 m square: what a barrier mustn't stand on)
struct TracksideRoads
{
	static constexpr double Cell = 32.0;
	struct Seg { P2 A, B; double Half; int Edge; };
	std::vector<Seg> Segs;
	std::unordered_map<long long, std::vector<int>> Cells;
	static long long Key(int Cx, int Cz) { return static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz); }
	void Add(const Seg& S)
	{
		const int I = int(Segs.size());
		Segs.push_back(S);
		const int X0 = int(std::floor((std::fmin(S.A.X, S.B.X) - S.Half) / Cell)), X1 = int(std::floor((std::fmax(S.A.X, S.B.X) + S.Half) / Cell));
		const int Z0 = int(std::floor((std::fmin(S.A.Z, S.B.Z) - S.Half) / Cell)), Z1 = int(std::floor((std::fmax(S.A.Z, S.B.Z) + S.Half) / Cell));
		for (int Cz = Z0; Cz <= Z1; ++Cz)
			for (int Cx = X0; Cx <= X1; ++Cx) Cells[Key(Cx, Cz)].push_back(I);
	}
	// How far P is clear of every circuit road's edge (negative: on one), up to Reach.
	double Clear(const P2& P, double Reach, int* Edge = nullptr) const
	{
		double Best = Reach;
		const int Cx0 = int(std::floor((P.X - Reach) / Cell)), Cx1 = int(std::floor((P.X + Reach) / Cell));
		const int Cz0 = int(std::floor((P.Z - Reach) / Cell)), Cz1 = int(std::floor((P.Z + Reach) / Cell));
		for (int Cz = Cz0; Cz <= Cz1; ++Cz)
			for (int Cx = Cx0; Cx <= Cx1; ++Cx)
			{
				const auto It = Cells.find(Key(Cx, Cz));
				if (It == Cells.end()) continue;
				for (int I : It->second)
				{
					const Seg& S = Segs[size_t(I)];
					const P2 D = S.B - S.A;
					const double L2 = D.X * D.X + D.Z * D.Z;
					const double T = L2 > 1e-12 ? std::clamp(((P.X - S.A.X) * D.X + (P.Z - S.A.Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
					const double Dist = (S.A + D * T).DistanceTo(P) - S.Half;
					if (Dist < Best) { Best = Dist; if (Edge != nullptr) *Edge = S.Edge; }
				}
			}
		return Best;
	}
};

uint32_t TracksideId(const P2& P, int Kind)
{
	return static_cast<uint32_t>(std::lround(P.X * 5.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(P.Z * 5.0)) * 19349663u ^ static_cast<uint32_t>(Kind) * 83492791u;
}
} // namespace

void RidePlan::IndexTrackside(CircuitIndex& Ix) const
{
	const Region& G = Area.Roads;
	if (G.CircuitFirst < 0) return;
	TracksideRoads Roads;
	for (int E = G.CircuitFirst; E < int(G.Edges.size()); ++E)
	{
		const RoadExtra* X = G.ExtraOf(E);
		const std::string& Class = G.ClassOf(E);
		const double Half = (X != nullptr && X->Width > 0.0 ? X->Width : Class == "raceway" ? Region::TrackWidth : Class == "pit_lane" ? Region::PitWidth : 6.0) * 0.5;
		const std::vector<P2>& L = G.Edges[size_t(E)].Line;
		for (size_t K = 0; K + 1 < L.size(); ++K) Roads.Add({L[K], L[K + 1], Half, E});
	}
	const auto TrapAt = [&](const P2& P)
	{
		const auto It = Ix.Areas.find(CircuitIndex::KeyOf(P.X, P.Z));
		if (It == Ix.Areas.end()) return -1;
		for (int A : It->second)
		{
			const CircuitArea& Ar = Ix.AreaOf(G, A);
			if (InRing(Ar.Ring, P.X, P.Z)) return Ar.Kind == 8 ? 1 : 2;
		}
		return -1;
	};
	// (the far edge of the run-off out from P along Out, or -1 where there is none within 70 m of the track's edge)
	const auto RunOffEdge = [&](const P2& P, const P2& Out, double Half)
	{
		double Edge = -1.0;
		bool In = false;
		for (double D = Half + 0.5; D < Half + 70.0; D += 1.0)
		{
			const bool Here = TrapAt(P + Out * D) >= 0;
			if (Here) { In = true; Edge = D; }
			else if (In) break;
			else if (D > Half + 6.0) break;   // (none starting near the edge: not this track's run-off)
		}
		return Edge;
	};
	const auto Put = [&](int Kind, int Variant, const P2& At, double Yaw, double RunLen, double Own)
	{
		CircuitIndex::Prop P;
		P.Kind = Kind;
		P.Variant = Variant;
		P.At = At;
		P.Yaw = Yaw;
		P.Length = RunLen;
		P.Stretch = Own > 0.0 ? float(RunLen / Own) : 1.0f;
		P.Id = TracksideId(At, Kind);
		Ix.Props.push_back(P);
	};
	// (a long piece from A to B, if both its ends and its middle stand clear of every circuit road)
	const auto Run = [&](int Kind, const P2& A, const P2& B, double Own)
	{
		const double Len = A.DistanceTo(B);
		if (Len < 0.5 || Len > Own * 1.6) return;
		const P2 Mid = (A + B) * 0.5;
		if (Roads.Clear(A, 2.0) < 0.6 || Roads.Clear(B, 2.0) < 0.6 || Roads.Clear(Mid, 2.0) < 0.6) return;
		Put(Kind, 0, A, std::atan2(B.Z - A.Z, B.X - A.X), Len, Own);
	};
	// (twice round: the gravel traps first, every track's, so that each barrier stands behind whichever trap it meets)
	for (int Pass = 0; Pass < 2; ++Pass)
	for (int E = G.CircuitFirst; E < int(G.Edges.size()); ++E)
	{
		if (!G.CircuitEdge(E)) continue;
		const int C = G.CircuitOf[size_t(E - G.CircuitFirst)];
		const std::string& Class = G.ClassOf(E);
		const RoadExtra* X = G.ExtraOf(E);
		std::vector<P2> L = G.Edges[size_t(E)].Line;
		if (L.size() < 2) continue;
		// (the way it's raced: clockwise has the circuit's middle on the right)
		const CircuitInfo& Info = G.Circuits[size_t(C)];
		const std::shared_ptr<const CircuitNote>& Note = size_t(C) < Ix.Notes.size() ? Ix.Notes[size_t(C)] : nullptr;
		{
			const P2 M = L[L.size() / 2], D = L[L.size() / 2] - L[L.size() / 2 - 1];
			const P2 V = Info.At - M;
			if (((D.X * V.Z - D.Z * V.X) > 0.0) != (Note ? Note->Clockwise : true)) std::reverse(L.begin(), L.end());
		}
		// The line every 2 m, its heading's turn (radians a metre: + right) over 8 m either side.
		std::vector<P2> Pt;
		std::vector<double> Along;
		double Total = 0.0;
		Pt.push_back(L[0]);
		Along.push_back(0.0);
		for (size_t K = 1; K < L.size(); ++K)
		{
			const double Seg = L[K].DistanceTo(L[K - 1]);
			if (Seg < 1e-6) continue;
			for (double T = 2.0 - std::fmod(Total, 2.0); T <= Seg + 1e-9; T += 2.0)
			{
				Pt.push_back(L[K - 1] + (L[K] - L[K - 1]) * (T / Seg));
				Along.push_back(Total + T);
			}
			Total += Seg;
		}
		if (Pt.size() < 4) continue;
		const size_t N = Pt.size();
		std::vector<P2> Dir(N), Left(N);
		std::vector<double> Turn(N, 0.0);
		for (size_t I = 0; I < N; ++I)
		{
			const P2 D = Pt[std::min(I + 1, N - 1)] - Pt[I > 0 ? I - 1 : 0];
			const double Len = std::fmax(D.Length(), 1e-9);
			Dir[I] = D * (1.0 / Len);
			Left[I] = P2(Dir[I].Z, -Dir[I].X);
			const size_t J0 = I >= 4 ? I - 4 : 0, J1 = std::min(I + 4, N - 1);
			if (J0 >= I || J1 <= I) continue;
			const P2 A = Pt[I] - Pt[J0], B = Pt[J1] - Pt[I];
			Turn[I] = std::atan2(A.X * B.Z - A.Z * B.X, A.X * B.X + A.Z * B.Z) / std::fmax(Along[J1] - Along[J0], 1e-6);
		}
		if (Class == "pit_lane" && Pass == 1)
		{
			// The pit wall: on the pit lane's side towards the track, its ends left open (where it leaves and joins).
			const double Half = (X != nullptr && X->Width > 0.0 ? X->Width : Region::PitWidth) * 0.5;
			for (size_t I = 0; I + 2 < N; I += 2)
			{
				if (Along[I] < 40.0 || Along[I + 2] > Total - 40.0) continue;
				double Side = 0.0;
				for (const double S : {1.0, -1.0})
				{
					int Near = -1;
					if (Roads.Clear(Pt[I + 1] + Left[I + 1] * (S * (Half + 2.0)), 25.0, &Near) < 25.0 && Near >= 0 && G.CircuitEdge(Near)
						&& G.ClassOf(Near) == "raceway") Side = Side == 0.0 ? S : Side;
				}
				if (Side == 0.0) continue;
				Run(PropPitWall, Pt[I] + Left[I] * (Side * (Half + 0.5)), Pt[I + 2] + Left[I + 2] * (Side * (Half + 0.5)), 4.0);
			}
			continue;
		}
		if (Class != "raceway") continue;
		const double Half = (X != nullptr && X->Width > 0.0 ? X->Width : Region::TrackWidth) * 0.5;
		constexpr double Corner = 1.0 / 160.0, Hard = 1.0 / 70.0;
		// (where a corner is: its turn tight at a point within 20 m)
		std::vector<int> Bend(N, 0);
		for (size_t I = 0; I < N; ++I)
			for (size_t J = I >= 10 ? I - 10 : 0; J < std::min(I + 11, N); ++J)
				if (std::fabs(Turn[J]) >= Corner) { Bend[I] = Turn[J] > 0.0 ? 1 : -1; if (std::fabs(Turn[J]) >= std::fabs(Turn[I])) break; }
		// [gravel traps] A gravel trap on the outside of each proper corner (tighter than 110 m) of a race circuit's
		// track where the map has none: from a kerb's width off the edge, as deep as the corner is fast (deeper after
		// a long straight), from the corner's start to 30 m past its exit (where a rider runs wide), its ends tapered;
		// cut short wherever another circuit road (the track doubling back, the pit lane) comes nearer than its own.
		if (Pass == 0 && Info.Kind == "track")
		{
			constexpr double Proper = 1.0 / 110.0, Kerb = 1.5;
			for (size_t I0 = 0; I0 < N;)
			{
				if (Bend[I0] == 0) { ++I0; continue; }
				size_t I1 = I0;
				double Tight = 0.0;
				while (I1 + 1 < N && Bend[I1 + 1] == Bend[I0]) { ++I1; Tight = std::fmax(Tight, std::fabs(Turn[I1])); }
				const int Way = Bend[I0];
				const size_t From = I0, To = std::min(N - 1, I1 + 15);
				I0 = I1 + 1;
				if (Tight < Proper || To < From + 8) continue;
				const double S = Way > 0 ? 1.0 : -1.0;   // (a right-hand bend's outside is its left: +)
				const size_t Mid = (From + To) / 2;
				if (TrapAt(Pt[Mid] + Left[Mid] * (S * (Half + 6.0))) >= 0) continue;   // (the map's own run-off)
				// (how fast it's met: the straighter track before it, up to 300 m)
				size_t Run0 = From;
				while (Run0 > 0 && From - Run0 < 150 && std::fabs(Turn[Run0 - 1]) < Corner) --Run0;
				const double Depth = std::clamp(12.0 + double(From - Run0) * 2.0 * 0.06, 12.0, 30.0) * (Tight >= Hard ? 1.0 : 0.8);
				std::vector<P2> Inner, Outer;
				for (size_t I = From; I <= To; ++I)
				{
					const double T = double(I - From) / double(To - From);
					const double Ease = std::fmin(std::fmin(T / 0.2, (1.0 - T) / 0.25), 1.0);
					double D = std::fmax(3.0, Depth * (Ease * Ease * (3.0 - 2.0 * Ease)));
					// (no nearer another circuit road than to this one's edge)
					while (D > 2.0 && Roads.Clear(Pt[I] + Left[I] * (S * (Half + Kerb + D)), Kerb + D + 1.0) < Kerb + D - 1.5) D -= 1.0;
					Inner.push_back(Pt[I] + Left[I] * (S * (Half + Kerb)));
					Outer.push_back(Pt[I] + Left[I] * (S * (Half + Kerb + D)));
				}
				CircuitArea Ar;
				Ar.Kind = 5;
				Ar.Circuit = Info.Id;
				Ar.Name = "gravel trap";
				Ar.Ring = Inner;
				Ar.Ring.insert(Ar.Ring.end(), Outer.rbegin(), Outer.rend());
				Ix.AddArea(G, std::move(Ar));
			}
		}
		if (Pass == 0) continue;
		// The barriers each side: out past the run-off (gravel or paved, as mapped), else a little off the edge;
		// tyres on a corner's outside, armco elsewhere. Pieces of 4 m (two tyre lengths).
		for (const double S : {1.0, -1.0})
		{
			for (size_t I = 0; I + 2 < N; I += 2)
			{
				const int B = Bend[I + 1];
				// (+ offsets are the left: a right-hand bend's outside is its left)
				const bool Outside = B != 0 && (B > 0) == (S > 0.0);
				double Off = Half + (Outside ? 9.0 : 5.0);
				const double Edge = RunOffEdge(Pt[I + 1], Left[I + 1] * S, Half);
				if (Edge > 0.0) Off = std::fmax(Off, Edge + 1.5);
				const P2 A = Pt[I] + Left[I] * (S * Off), Bp = Pt[I + 2] + Left[I + 2] * (S * Off);
				if (Outside)
				{
					const P2 M = (A + Bp) * 0.5;
					Run(PropTyreWall, A, M, 2.0);
					Run(PropTyreWall, M, Bp, 2.0);
				}
				else Run(PropArmco, A, Bp, 4.0);
			}
		}
		// Marshals' posts every 350 m or so, behind the barrier on the outside of the bend there (else the left).
		for (double At = 150.0; At < Total - 50.0; At += 350.0)
		{
			const size_t I = std::min(N - 1, size_t(At / 2.0));
			const double S = Bend[I] > 0 ? 1.0 : Bend[I] < 0 ? -1.0 : 1.0;
			double Off = Half + 12.0;
			const double Edge = RunOffEdge(Pt[I], Left[I] * S, Half);
			if (Edge > 0.0) Off = std::fmax(Off, Edge + 4.0);
			const P2 P = Pt[I] + Left[I] * (S * Off);
			if (Roads.Clear(P, 4.0) < 2.5) continue;
			Put(PropMarshalPost, 0, P, std::atan2(Pt[I].Z - P.Z, Pt[I].X - P.X), 0.0, 0.0);
		}
		// Braking boards before a hard corner met after 150 m of straighter track: at 150, 100 and 50 m, on its outside.
		for (size_t I = 75; I < N; ++I)
		{
			if (std::fabs(Turn[I]) < Hard || std::fabs(Turn[I - 1]) >= Corner) continue;
			bool Straight = true;
			for (size_t J = I - 75; J < I && Straight; ++J) Straight = std::fabs(Turn[J]) < Corner;
			if (!Straight) continue;
			const double S = Turn[I] > 0.0 ? 1.0 : -1.0;
			for (int V = 0; V < 3; ++V)
			{
				const size_t J = I - size_t(75 - 25 * V);
				const P2 P = Pt[J] + Left[J] * (S * (Half + 2.5));
				if (Roads.Clear(P, 3.0) < 1.2) continue;
				// (facing the riders coming: back along the track)
				Put(PropBrakingBoard, V, P, std::atan2(-Dir[J].Z, -Dir[J].X), 0.0, 0.0);
			}
		}
	}
	// The start gantry over each circuit's line, its lights facing the grid (back along the way it's raced).
	for (const CircuitIndex::Line& Ln : Ix.Lines)
	{
		if (Ln.Kind != 0 || Ln.Circuit < 0 || size_t(Ln.Circuit) >= G.Circuits.size()) continue;
		P2 T(Ln.Across.Z, -Ln.Across.X);
		const P2 V = G.Circuits[size_t(Ln.Circuit)].At - Ln.At;
		const std::shared_ptr<const CircuitNote>& Note = size_t(Ln.Circuit) < Ix.Notes.size() ? Ix.Notes[size_t(Ln.Circuit)] : nullptr;
		if (((T.X * V.Z - T.Z * V.X) > 0.0) != (Note ? Note->Clockwise : true)) T = T * -1.0;
		Put(PropStartGantry, 0, Ln.At, std::atan2(-T.Z, -T.X), 0.0, 0.0);
	}
	// The grandstands where the map has them: 10 m bays along the stand's front, facing the nearest track.
	for (const CircuitArea& Ar : G.CircuitAreas)
	{
		if (Ar.Kind != 9 || Ar.Ring.size() < 3) continue;
		P2 Mid(0.0, 0.0);
		for (const P2& P : Ar.Ring) Mid = Mid + P;
		Mid = Mid * (1.0 / double(Ar.Ring.size()));
		int Near = -1;
		const double Gap = Roads.Clear(Mid, 150.0, &Near);
		if (Near < 0 || Gap >= 150.0) continue;
		// (the nearest point of that road: the stand faces it)
		const std::vector<P2>& Rl = G.Edges[size_t(Near)].Line;
		P2 Best = Rl[0];
		double Bd = 1e18;
		for (size_t K = 0; K + 1 < Rl.size(); ++K)
		{
			const P2 D = Rl[K + 1] - Rl[K];
			const double L2 = D.X * D.X + D.Z * D.Z;
			const double T = L2 > 1e-12 ? std::clamp(((Mid.X - Rl[K].X) * D.X + (Mid.Z - Rl[K].Z) * D.Z) / L2, 0.0, 1.0) : 0.0;
			const P2 P = Rl[K] + D * T;
			if (P.DistanceTo(Mid) < Bd) { Bd = P.DistanceTo(Mid); Best = P; }
		}
		const P2 Face = (Best - Mid) * (1.0 / std::fmax(Bd, 1e-6));
		const P2 Across(-Face.Z, Face.X);
		double Lo = 1e18, Hi = -1e18;
		for (const P2& P : Ar.Ring)
		{
			const double U = (P.X - Mid.X) * Across.X + (P.Z - Mid.Z) * Across.Z;
			Lo = std::fmin(Lo, U); Hi = std::fmax(Hi, U);
		}
		const int Bays = std::clamp(int(std::lround((Hi - Lo) / 10.0)), 1, 30);
		for (int K = 0; K < Bays; ++K)
		{
			const double U = Lo + (Hi - Lo) * (K + 0.5) / Bays;
			Put(PropGrandstand, 0, Mid + Across * U, std::atan2(Face.Z, Face.X), 0.0, 0.0);
		}
	}
	for (size_t K = 0; K < Ix.Props.size(); ++K) Ix.PropSquares[CircuitIndex::KeyOf(Ix.Props[K].At.X, Ix.Props[K].At.Z)].push_back(int(K));
}

void RidePlan::CircuitProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	if (!CircuitIx || CircuitIx->Props.empty()) return;
	const CircuitIndex& Ix = *CircuitIx;
	for (double Z = std::floor(Z0 / CircuitIndex::Square) * CircuitIndex::Square; Z < Z0 + Size; Z += CircuitIndex::Square)
		for (double X = std::floor(X0 / CircuitIndex::Square) * CircuitIndex::Square; X < X0 + Size; X += CircuitIndex::Square)
		{
			const auto It = Ix.PropSquares.find(CircuitIndex::KeyOf(X + 1.0, Z + 1.0));
			if (It == Ix.PropSquares.end()) continue;
			for (int Pi : It->second)
			{
				const CircuitIndex::Prop& Pr = Ix.Props[size_t(Pi)];
				if (Pr.At.X < X0 || Pr.At.X >= X0 + Size || Pr.At.Z < Z0 || Pr.At.Z >= Z0 + Size) continue;
				if (Pr.Kind < 0 || Pr.Kind >= PropKinds || !PropModelled[Pr.Kind]) continue;
				const double Y = Ground(Pr.At.X, Pr.At.Z);
				if (std::isnan(Y)) continue;
				RideProp P;
				P.Kind = uint16_t(Pr.Kind);
				P.Variant = uint8_t(Pr.Variant);
				P.Id = Pr.Id;
				P.X = float(Pr.At.X - X0);
				P.Z = float(Pr.At.Z - Z0);
				P.Y = float(Y);
				P.Yaw = float(Pr.Yaw);
				if (Pr.Length > 0.0)
				{
					// (laid along its run: tipped up the slope to its far end)
					const double Y1 = Ground(Pr.At.X + std::cos(Pr.Yaw) * Pr.Length, Pr.At.Z + std::sin(Pr.Yaw) * Pr.Length);
					if (!std::isnan(Y1)) P.Pitch = float(std::atan2(Y1 - Y, Pr.Length));
					P.Stretch = Pr.Stretch;
				}
				Out.push_back(P);
			}
		}
}

// ----- The lap timer -----

bool RidePlan::CircuitStartNear(double X, double Z, double Within, double Back, P2& At, double& Heading, std::string& Name) const
{
	if (!CircuitIx) return false;
	const Region& G = Area.Roads;
	const CircuitIndex& Ix = *CircuitIx;
	const CircuitIndex::Line* Best = nullptr;
	double Near = Within;
	for (const CircuitIndex::Line& L : Ix.Lines)
	{
		if (L.Kind != 0 || L.Circuit < 0 || size_t(L.Circuit) >= G.Circuits.size()) continue;
		const double D = L.At.DistanceTo(P2(X, Z));
		if (D < Near) { Near = D; Best = &L; }
	}
	if (Best == nullptr) return false;
	const CircuitInfo& Info = G.Circuits[size_t(Best->Circuit)];
	const std::shared_ptr<const CircuitNote> Note = size_t(Best->Circuit) < Ix.Notes.size() ? Ix.Notes[size_t(Best->Circuit)] : nullptr;
	// (along the track at the line, the way round it's raced: clockwise has the circuit's middle on the right,
	// and in this frame (x east, z south) the right of T is where T.X * V.Z - T.Z * V.X > 0)
	P2 T(Best->Across.Z, -Best->Across.X);
	const P2 V = Info.At - Best->At;
	const bool Clockwise = Note ? Note->Clockwise : true;
	const double Cross = T.X * V.Z - T.Z * V.X;
	if ((Cross > 0.0) != Clockwise) T = T * -1.0;
	At = Best->At - T * Back;
	Heading = std::atan2(-T.X, -T.Z);
	Name = Note ? Note->Name : Info.Name;
	return true;
}

bool CircuitLaps::Step(const RidePlan& Plan, double X, double Z, double Now, Lap& Done)
{
	const P2 Here(X, Z);
	if (!HavePrev)
	{
		Prev = Here;
		HavePrev = true;
		return false;
	}
	const P2 Was = Prev;
	Prev = Here;
	const double Moved = Was.DistanceTo(Here);
	// (a jump, a respawn or a reset: no line was crossed, and a lap under way is spoilt)
	if (Moved > 60.0)
	{
		S.Spoilt = S.Running;
		return false;
	}
	if (!Plan.CircuitIx) return false;
	const CircuitIndex& Ix = *Plan.CircuitIx;
	const Region& G = Plan.Area.Roads;
	const auto CrossedLine = [&](const CircuitIndex::Line& L)
	{
		const P2 A = L.At + L.Across * (L.Half + 2.0), B = L.At - L.Across * (L.Half + 2.0);
		return Crosses(Was, Here, A, B);
	};
	const auto NameOf = [&](const CircuitIndex::Line& L)
	{
		if (L.Kind < 0) return L.Name;
		if (L.Circuit >= 0 && size_t(L.Circuit) < Ix.Notes.size() && Ix.Notes[size_t(L.Circuit)]) return Ix.Notes[size_t(L.Circuit)]->Name;
		return L.Circuit >= 0 && size_t(L.Circuit) < G.Circuits.size() ? G.Circuits[size_t(L.Circuit)].Name : std::string("Circuit");
	};
	if (S.Running) S.Run += Moved;
	// (off the circuit's tarmac by more than OffTrack: this lap doesn't count)
	if (S.Running && S.Way != 2)
	{
		const double Wide = Region::TrackWidth * 0.5 + OffTrack;
		if (G.CircuitAt(X, Z, Wide) < 0)
		{
			S.Spoilt = true;
			// (gone from the circuit altogether, out through the paddock: no lap under way)
			if (G.CircuitAt(X, Z, LeftBehind) < 0) S = State();
		}
	}
	// (a road course left: no gate in GateWait seconds)
	if (S.Running && S.Way == 2 && Now - (S.Splits.empty() ? S.Started : S.Started + S.Splits.back()) > GateWait) S = State();
	// A road course: its start, then its gates in order, then its start again.
	for (size_t Li = 0; Li < Ix.Lines.size(); ++Li)
	{
		const CircuitIndex::Line& L = Ix.Lines[Li];
		if (L.Kind != -1) continue;
		const bool In = Here.DistanceTo(L.At) < GateReach, WasIn = Was.DistanceTo(L.At) < GateReach;
		if (!In || WasIn) continue;
		const std::shared_ptr<const CircuitNotes> Notes = CircuitNotesNow();
		const CircuitNote* Note = nullptr;
		for (size_t K = 0; Notes && K < Notes->Circuits.size(); ++K)
			if (Notes->Circuits[K].Road && Notes->Circuits[K].Name == L.Name) Note = &Notes->Circuits[K];
		if (Note == nullptr) continue;
		if (S.Running && S.Circuit == L.Name && S.Way == 2 && S.NextSector >= int(Note->Gates.size()))
		{
			Done = Lap();
			Done.Circuit = L.Name;
			Done.Seconds = Now - S.Started;
			for (size_t K = 0; K < S.Splits.size(); ++K) Done.Splits.push_back(S.Splits[K] - (K > 0 ? S.Splits[K - 1] : 0.0));
			Done.Splits.push_back(Done.Seconds - (S.Splits.empty() ? 0.0 : S.Splits.back()));
			Done.Counted = !S.Spoilt;
			Last = Done;
			HasLast = true;
			auto Bt = Best.find(Done.Circuit);
			if (Done.Counted && (Bt == Best.end() || Done.Seconds < Bt->second.Seconds)) Best[Done.Circuit] = Done;
			S = State();
			return true;
		}
		S = State();
		S.Circuit = L.Name;
		S.Running = true;
		S.Started = Now;
		S.Way = 2;   // (a road course: gates, not lines)
		GateWas = -1;
	}
	if (S.Running && S.Way == 2)
	{
		const std::shared_ptr<const CircuitNotes> Notes = CircuitNotesNow();
		for (size_t K = 0; Notes && K < Notes->Circuits.size(); ++K)
		{
			const CircuitNote& N = Notes->Circuits[K];
			if (!N.Road || N.Name != S.Circuit || S.NextSector >= int(N.Gates.size())) continue;
			// (on the main road nearest the note's place, as IndexCircuits found it)
			const auto Found = Ix.RoadGates.find(N.Name);
			const P2 Gate = Found != Ix.RoadGates.end() && size_t(S.NextSector) < Found->second.size() ? Found->second[size_t(S.NextSector)]
				: G.FrameOf().FromGeo(N.Gates[size_t(S.NextSector)].Lat, N.Gates[size_t(S.NextSector)].Lon);
			if (Here.DistanceTo(Gate) < GateReach)
			{
				S.Splits.push_back(Now - S.Started);
				++S.NextSector;
			}
		}
		return false;
	}
	// A circuit: its start line and its sectors' lines.
	for (size_t Li = 0; Li < Ix.Lines.size(); ++Li)
	{
		const CircuitIndex::Line& L = Ix.Lines[Li];
		if (L.Kind < 0 || !CrossedLine(L)) continue;
		const std::string Name = NameOf(L);
		if (L.Kind > 0)
		{
			if (!S.Running || S.Circuit != Name) continue;
			// (the sectors in the order they are met: the first one crossed says which way round)
			int Count = 0;
			for (const CircuitIndex::Line& O : Ix.Lines) Count += O.Circuit == L.Circuit && O.Kind > 0 ? 1 : 0;
			if (S.Way == 0) S.Way = L.Kind == 1 ? 1 : L.Kind == Count ? -1 : 0;
			const int Expect = S.Way >= 0 ? S.NextSector + 1 : Count - S.NextSector;
			if (L.Kind == Expect)
			{
				S.Splits.push_back(Now - S.Started);
				++S.NextSector;
			}
			continue;
		}
		// The start line: a lap done (if one was under way, long enough and through every sector), and the next begun.
		bool Finished = false;
		if (S.Running && S.Circuit == Name)
		{
			double Length = 0.0;
			if (L.Circuit >= 0 && size_t(L.Circuit) < G.Circuits.size()) Length = G.Circuits[size_t(L.Circuit)].Length;
			int Sectors = 0;
			for (const CircuitIndex::Line& O : Ix.Lines) Sectors += O.Circuit == L.Circuit && O.Kind > 0 ? 1 : 0;
			if (S.Run < std::fmax(300.0, Length * 0.4)) continue;   // (back over the line: not a lap)
			Done = Lap();
			Done.Circuit = Name;
			Done.Seconds = Now - S.Started;
			for (size_t K = 0; K < S.Splits.size(); ++K) Done.Splits.push_back(S.Splits[K] - (K > 0 ? S.Splits[K - 1] : 0.0));
			Done.Splits.push_back(Done.Seconds - (S.Splits.empty() ? 0.0 : S.Splits.back()));
			Done.Counted = !S.Spoilt && S.NextSector >= Sectors;
			Last = Done;
			HasLast = true;
			auto Bt = Best.find(Done.Circuit);
			if (Done.Counted && (Bt == Best.end() || Done.Seconds < Bt->second.Seconds)) Best[Done.Circuit] = Done;
			Finished = true;
		}
		S = State();
		S.Circuit = Name;
		S.Running = true;
		S.Started = Now;
		if (Finished) return true;
	}
	return false;
}
} // namespace TorqueWorld
