// [race tracks] Race circuits on a ride: their paint, their ground, their notes and the lap timer
// (WorldRideCircuits.h).
#include "WorldRideCircuits.h"

#include "WorldFiles.h"
#include "WorldRide.h"
#include "WorldRoadLine.h"

#include <algorithm>
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
				int Edge = E0, Node = G.Edges[size_t(E0)].B;
				Walk = G.Edges[size_t(E0)].Line;
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
						const double Cos = -(In.X * Out.X + In.Z * Out.Z) / std::fmax(In.Length() * Out.Length(), 1e-9);
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
	// The road courses whose start is within the ride's area (their gates may be beyond it yet).
	if (Notes)
		for (const CircuitNote& N : Notes->Circuits)
		{
			if (!N.Road || N.Start.Lat == 0.0) continue;
			P2 S = F.FromGeo(N.Start.Lat, N.Start.Lon);
			const int Snap = G.NearestNode(S.X, S.Z, 400.0);
			if (Snap >= 0) S = G.Nodes[size_t(Snap)];
			if (!G.Bounds.Empty() && (S.X < G.Bounds.X0 - 20000.0 || S.X > G.Bounds.X1 + 20000.0 || S.Z < G.Bounds.Z0 - 20000.0 || S.Z > G.Bounds.Z1 + 20000.0)) continue;
			CircuitIndex::Line Gate;
			Gate.Circuit = -1;
			Gate.Kind = -1;
			Gate.Name = N.Name;
			Gate.At = S;
			Gate.Half = CircuitLaps::GateReach;
			Ix->Lines.push_back(Gate);
		}
	IndexTrackside(*Ix);   // (the trackside kit, once the lines are known)
	CircuitIx = Ix;
}

bool RidePlan::CircuitTint(double X, double Z, float* Rgb) const
{
	const int Kind = CircuitGroundAt(X, Z);
	if (Kind < 0) return false;
	// (a gravel trap's pale washed stone; paved run-off a little lighter than the track)
	static constexpr float Gravel[3] = {0.16f, 0.14f, 0.11f}, Paved[3] = {0.035f, 0.035f, 0.038f};
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
		const CircuitArea& Ar = Area.Roads.CircuitAreas[size_t(A)];
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
	// Its corners: where it turns tighter than KerbRadius, a red and white kerb along the inside, and along
	// the outside from the apex out to the exit. (Its heading's turn over 8 m either side of each point.)
	constexpr double KerbRadius = 160.0, Reach = 8.0, Block = 1.5, KerbWide = 1.1;
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
	const double Tight = 1.0 / KerbRadius;
	const auto Kerb = [&](double A0, double A1, double Side)
	{
		A0 = std::fmax(A0, 0.5);
		A1 = std::fmin(A1, Len - 0.5);
		for (double B = std::floor(A0 / Block) * Block; B < A1; B += Block)
		{
			const double U0 = std::fmax(B, A0), U1 = std::fmin(B + Block, A1);
			if (U1 <= U0 || U1 < From || U0 > To) continue;
			const double Half = HalfOn(Pts, (U0 + U1) * 0.5);
			const double Outer = Side * Half, Inner = Side * (Half - KerbWide);
			const bool Red = (static_cast<long long>(std::floor(B / Block)) & 1) == 0;
			Out.push_back(PaintMark(U0, U1, std::fmax(Outer, Inner), std::fmin(Outer, Inner), Red ? KerbRed : KerbWhite));
		}
	};
	for (size_t I = 0; I < Pts.size();)
	{
		if (std::fabs(Turn[I]) < Tight) { ++I; continue; }
		const double Sign = Turn[I] > 0.0 ? 1.0 : -1.0;
		size_t J = I, Apex = I;
		while (J < Pts.size() && Turn[J] * Sign >= Tight)
		{
			if (std::fabs(Turn[J]) > std::fabs(Turn[Apex])) Apex = J;
			++J;
		}
		const double A0 = Pts[I].Along, A1 = Pts[std::min(J, Pts.size() - 1)].Along;
		if (A1 - A0 >= 6.0)
		{
			// (+ offsets are the road's left as its points run: a right-hand bend's inside is its right)
			const double Inside = Sign > 0.0 ? -1.0 : 1.0;
			Kerb(A0 - 4.0, A1 + 4.0, Inside);
			Kerb(Pts[Apex].Along, A1 + 18.0, -Inside);
		}
		I = J;
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
			if (InRing(G.CircuitAreas[size_t(A)].Ring, P.X, P.Z)) return G.CircuitAreas[size_t(A)].Kind == 8 ? 1 : 2;
		return -1;
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
		if (Class == "pit_lane")
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
				for (double D = Half + 1.0; D < Half + 70.0; D += 2.0)
				{
					if (TrapAt(Pt[I + 1] + Left[I + 1] * (S * D)) < 0) { if (D > Half + 3.0) Off = std::fmax(Off, D + 1.0); break; }
				}
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
			for (double D = Half + 1.0; D < Half + 70.0; D += 2.0)
				if (TrapAt(Pt[I] + Left[I] * (S * D)) < 0) { Off = std::fmax(Off, D + 4.0); break; }
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
		if (G.CircuitAt(X, Z, Wide) < 0) S.Spoilt = true;
	}
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
			P2 Gate = G.FrameOf().FromGeo(N.Gates[size_t(S.NextSector)].Lat, N.Gates[size_t(S.NextSector)].Lon);
			// (the note's place is rough: on the nearest road to it)
			const int Snap = G.NearestNode(Gate.X, Gate.Z, 400.0);
			if (Snap >= 0) Gate = G.Nodes[size_t(Snap)];
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
