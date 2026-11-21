// [trains] Routes, the made-up timetable and where the trains are now (WorldTrains.h).
#include "WorldTrains.h"

#include "WorldRegion.h"
#include "WorldRide.h"
#include "WorldTerrain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>

namespace TorqueWorld
{
namespace Trains
{
namespace
{
constexpr double Step = RidePlan::RailStep;
constexpr double SnapM = 6.0;          // a map piece's place is the drawn track's within this
constexpr double JoinTurn = 35.0;      // two pieces join into one route through a point no sharper than this (degrees)
constexpr double LeadTurn = 60.0;      // a route led in from the line it leaves at no sharper than this
constexpr double LatGrip = 1.6;        // how hard a train corners (m/s^2, the cant and its deficiency together)
constexpr double MinCurveV = 6.0;      // however tight the curve (a yard's points), no slower than this
constexpr double StandEnds = 150.0;    // a train stands this long at buffers before it leaves, and after it gets in (s)

uint32_t Mix(uint32_t H)
{
	H ^= H >> 16; H *= 0x7feb352dU; H ^= H >> 15; H *= 0x846ca68bU; H ^= H >> 16;
	return H;
}
uint32_t HashOf(const std::string& S, uint32_t H = 2166136261U)
{
	for (unsigned char C : S) { H ^= C; H *= 16777619U; }
	return H;
}
std::string Lower(const std::string& S)
{
	std::string O = S;
	for (char& C : O) C = char(std::tolower(static_cast<unsigned char>(C)));
	return O;
}
bool Has(const std::string& Lc, std::initializer_list<const char*> Words)
{
	for (const char* W : Words)
		if (Lc.find(W) != std::string::npos) return true;
	return false;
}
long long CellKey(double X, double Z, double Cell)
{
	return static_cast<long long>(std::floor(X / Cell)) * 4000037LL + static_cast<long long>(std::floor(Z / Cell));
}
P2 Unit(const P2& V)
{
	const double L = V.Length();
	return L > 1e-9 ? V * (1.0 / L) : P2(1.0, 0.0);
}
double Dot(const P2& A, const P2& B) { return A.X * B.X + A.Z * B.Z; }
// (B's way relative to A's, + to A's left: the left of a way D is (D.Z, -D.X), as RailTile's)
double LeftOf(const P2& A, const P2& B) { return B.X * A.Z - B.Z * A.X; }

// The heritage lines the map tags as railways, not "preserved" (most are preserved; these by name).
bool HeritageName(const std::string& Lc)
{
	return Has(Lc, {"steam", "heritage", "preserved", "moors railway", "worth valley", "severn valley", "bluebell", "west somerset", "great central railway",
		"wensleydale", "embsay", "llangollen", "mid-hants", "mid hants", "swanage", "strathspey", "bo'ness", "watercress", "gloucestershire warwickshire",
		"east lancashire railway", "nene valley", "dartmouth", "bodmin", "kent & east sussex", "kent and east sussex", "isle of wight", "north norfolk",
		"spa valley", "churnet valley", "ecclesbourne", "peak rail", "weardale", "aln valley", "tanfield", "derwent valley light", "east somerset",
		"dean forest", "chinnor", "colne valley", "epping ongar", "elsecar", "avon valley", "plym valley", "south devon railway", "keith and dufftown",
		"caledonian railway", "royal deeside", "downpatrick", "battlefield line", "foxfield", "telford steam", "cambrian heritage", "gwili", "pontypool",
		"barry tourist", "llangollen", "lakeside and haverthwaite", "ravenglass", "eden valley", "stainmore", "kirkby stephen"});
}
// The UK's big stations, where the intercity trains stop (an invented pattern on real places).
bool MajorName(const std::string& Lc)
{
	return Has(Lc, {"london", "king's cross", "kings cross", "euston", "paddington", "st pancras", "liverpool street", "waterloo", "victoria", "york",
		"edinburgh", "glasgow", "newcastle", "leeds", "doncaster", "peterborough", "darlington", "durham", "birmingham new street", "crewe", "preston",
		"lancaster", "carlisle", "oxenholme", "manchester piccadilly", "liverpool lime street", "bristol temple meads", "reading", "swindon",
		"cardiff central", "newport", "sheffield", "derby", "leicester", "nottingham", "wakefield westgate", "stevenage", "grantham", "newark north gate",
		"retford", "berwick", "aberdeen", "dundee", "perth", "stirling", "inverness", "exeter st davids", "plymouth", "penzance", "taunton", "bath spa",
		"milton keynes", "rugby", "stafford", "wolverhampton", "warrington", "wigan north western", "motherwell", "norwich", "ipswich", "colchester",
		"cambridge", "brighton", "southampton", "bournemouth", "hull", "middlesbrough", "sunderland", "harrogate", "scarborough", "huddersfield",
		"bradford", "chester", "shrewsbury", "swansea", "worcester", "cheltenham", "gloucester", "coventry"});
}
// Whether a line is electrified with overhead wires (the map's bake has no word for it): a guess by line and place.
bool ElectricGuess(const std::string& Lc, LineClass C, double Lat, double Lon)
{
	if (C == LineClass::Heritage || C == LineClass::None || C == LineClass::Freight) return false;
	if (Has(Lc, {"east coast main line", "west coast main line", "selby diversion"})) return true;
	if (Has(Lc, {"great western main line"})) return Lon > -3.25;           // (wired to Cardiff)
	if (Has(Lc, {"midland main line"})) return Lat < 52.45;                 // (wired to Kettering and Corby)
	if (Has(Lc, {"great eastern main line", "west anglia", "north london line", "thameslink", "chiltern"})) return !Has(Lc, {"chiltern"});
	// (the overhead-wired suburbs: north of the Thames round London (south of it is third rail), Glasgow and Edinburgh,
	// Manchester and Leeds and Bradford)
	const bool Busy = C == LineClass::Main || C == LineClass::Secondary;
	if (!Busy) return false;
	if (Lat > 51.50 && Lat < 51.80 && Lon > -0.60 && Lon < 0.40) return true;
	if (Lat > 55.75 && Lat < 56.00 && Lon > -4.75 && Lon < -3.95) return true;
	if (Lat > 55.85 && Lat < 56.00 && Lon > -3.45 && Lon < -3.05) return true;
	if (Lat > 53.35 && Lat < 53.55 && Lon > -2.45 && Lon < -2.10) return true;
	if (Lat > 53.78 && Lat < 53.95 && Lon > -1.95 && Lon < -1.50) return true;
	return false;
}

const ModelSpec Specs[int(Model::Count)] = {
	{"train_dmu_car", 23.0, 8.0, 2.75, 3.80, true},
	{"train_emu_cab", 20.4, 7.0, 2.80, 3.78, true},
	{"train_emu_mid", 20.0, 7.0, 2.80, 3.78, false},
	{"train_ic_power", 20.0, 6.5, 2.75, 3.85, true},
	{"train_ic_coach", 23.0, 8.0, 2.75, 3.85, false},
	{"train_freight_loco", 21.3, 7.0, 2.65, 3.90, true},
	{"train_wagon_box", 19.6, 6.5, 2.60, 3.70, false},
	{"train_wagon_hopper", 15.0, 4.8, 2.80, 3.70, false},
	{"train_wagon_tank", 15.5, 4.8, 2.70, 3.80, false},
	{"train_steam_tank", 10.0, 2.6, 2.60, 3.90, true},
	{"train_steam_big", 13.5, 4.0, 2.75, 4.00, true},
	{"train_steam_tender", 8.0, 2.3, 2.70, 3.60, false},
	{"train_coach_old", 19.5, 6.4, 2.75, 3.80, false},
	{"train_diesel_old", 18.0, 5.8, 2.70, 3.85, true},
};

// The fastest each class of line is built for (m/s): its cant's and its trains' cap.
double LineV(LineClass C)
{
	switch (C)
	{
	case LineClass::Main: return 56.0;
	case LineClass::Secondary: return 45.0;
	case LineClass::Branch: return 33.0;
	case LineClass::Freight: return 27.0;
	case LineClass::Heritage: return 11.0;
	default: return 20.0;
	}
}
} // namespace

const char* ClassName(LineClass C)
{
	switch (C)
	{
	case LineClass::Main: return "main";
	case LineClass::Secondary: return "secondary";
	case LineClass::Branch: return "branch";
	case LineClass::Freight: return "freight";
	case LineClass::Heritage: return "heritage";
	default: return "none";
	}
}

const char* KindName(Kind K)
{
	static const char* Names[] = {"dmu", "emu", "intercity", "freight", "steam_tank", "steam_big", "old_diesel"};
	return int(K) < int(Kind::Count) ? Names[int(K)] : "?";
}

const ModelSpec& SpecOf(Model M) { return Specs[std::clamp(int(M), 0, int(Model::Count) - 1)]; }

int Network::Band(double Hour)
{
	if (Hour < 0.0) return 0;
	const double H = std::fmod(Hour, 24.0);
	if (H >= 6.0 && H < 19.0) return 0;
	if (H >= 19.0 && H < 23.0) return 1;
	return 2;
}

bool Network::HeritageRuns(const Clock& Now)
{
	const double H = Now.Hour < 0.0 ? 12.0 : std::fmod(Now.Hour, 24.0);
	return H >= 10.0 && H < 17.0 && Now.Day >= 60 && Now.Day <= 305;
}

// --- Building the routes ------------------------------------------------------------------------------------------

std::shared_ptr<Network> Network::Build(const RidePlan& Plan)
{
	std::shared_ptr<Network> Net = std::make_shared<Network>();
	const Region& G = Plan.Area.Roads;
	// The map's pieces of railway in use (not disused, not being built; trams, metros, miniature and narrow gauge left out).
	struct Piece
	{
		int Line = 0;
		double Len = 0.0;
		bool Preserved = false;
	};
	std::vector<Piece> Pieces;
	double Bx0 = 1e18, Bz0 = 1e18, Bx1 = -1e18, Bz1 = -1e18;
	for (size_t L = 0; L < G.Scenery.Lines.size(); ++L)
	{
		const FeatureLine& F = G.Scenery.Lines[L];
		if ((F.Kind != "railway" && F.Kind != "preserved") || F.Line.size() < 2 || (F.Flags & (4 | 8)) != 0) continue;
		Piece P;
		P.Line = int(L);
		for (size_t I = 0; I + 1 < F.Line.size(); ++I) P.Len += F.Line[I].DistanceTo(F.Line[I + 1]);
		P.Preserved = F.Kind == "preserved";
		if (P.Len < 0.5) continue;
		Pieces.push_back(P);
		for (const P2& Q : F.Line) { Bx0 = std::fmin(Bx0, Q.X); Bz0 = std::fmin(Bz0, Q.Z); Bx1 = std::fmax(Bx1, Q.X); Bz1 = std::fmax(Bz1, Q.Z); }
	}
	if (Pieces.empty()) return Net;
	const auto LineOf = [&G, &Pieces](size_t K) -> const std::vector<P2>& { return G.Scenery.Lines[size_t(Pieces[K].Line)].Line; };
	// Their ends by 1 m square.
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Ends;
	for (size_t K = 0; K < Pieces.size(); ++K)
	{
		const std::vector<P2>& Ln = LineOf(K);
		Ends[CellKey(Ln.front().X, Ln.front().Z, 1.0)].emplace_back(int(K), 0);
		Ends[CellKey(Ln.back().X, Ln.back().Z, 1.0)].emplace_back(int(K), 1);
	}
	// A piece's way out of its end E (0 its first point, 1 its last), looking ~10 m in.
	const auto WayOut = [&LineOf](size_t K, int E)
	{
		const std::vector<P2>& Ln = LineOf(K);
		const P2 Tip = E == 0 ? Ln.front() : Ln.back();
		P2 In = Tip;
		double Gone = 0.0;
		for (size_t J = 1; J < Ln.size() && Gone < 10.0; ++J)
		{
			const P2 Q = E == 0 ? Ln[J] : Ln[Ln.size() - 1 - J];
			Gone = Q.DistanceTo(Tip);
			In = Q;
		}
		return Unit(In - Tip);   // (into the piece from its end)
	};
	// Pieces joined into chains, the longest first so the main lines run through: at each end the unused piece meeting
	// it that carries on straightest (the same name a little preferred), none round a sharper turn than JoinTurn.
	std::vector<int> Order(Pieces.size());
	for (size_t K = 0; K < Order.size(); ++K) Order[K] = int(K);
	std::sort(Order.begin(), Order.end(), [&Pieces](int A, int B) { return Pieces[size_t(A)].Len > Pieces[size_t(B)].Len; });
	std::vector<char> Used(Pieces.size(), 0);
	struct Chain
	{
		std::vector<P2> Pts;
		std::map<std::string, double> Names;
		double Preserved = 0.0, Len = 0.0;
		bool OpenAt[2] = {false, false};
	};
	std::vector<Chain> Chains;
	for (int Seed : Order)
	{
		if (Used[size_t(Seed)]) continue;
		Used[size_t(Seed)] = 1;
		Chain C;
		const auto Note = [&C, &G, &Pieces](size_t K)
		{
			const FeatureLine& F = G.Scenery.Lines[size_t(Pieces[K].Line)];
			C.Names[F.Name] += Pieces[K].Len;
			C.Len += Pieces[K].Len;
			if (Pieces[K].Preserved) C.Preserved += Pieces[K].Len;
		};
		Note(size_t(Seed));
		const std::string SeedName = G.Scenery.Lines[size_t(Pieces[size_t(Seed)].Line)].Name;
		C.Pts = LineOf(size_t(Seed));
		for (int End = 0; End < 2; ++End)
			for (;;)
			{
				const P2 Tip = End == 0 ? C.Pts.back() : C.Pts.front();
				// (the chain's way out at this end)
				P2 Back = Tip;
				for (size_t J = 1; J < C.Pts.size(); ++J)
				{
					const P2 Q = End == 0 ? C.Pts[C.Pts.size() - 1 - J] : C.Pts[J];
					Back = Q;
					if (Q.DistanceTo(Tip) >= 10.0) break;
				}
				const P2 Out = Unit(Tip - Back);
				int Best = -1, BestE = 0;
				double BestScore = 1e9;
				for (int Dz = -1; Dz <= 1; ++Dz)
					for (int Dx = -1; Dx <= 1; ++Dx)
					{
						const auto It = Ends.find(CellKey(Tip.X + Dx, Tip.Z + Dz, 1.0));
						if (It == Ends.end()) continue;
						for (const std::pair<int, int>& E : It->second)
						{
							if (Used[size_t(E.first)]) continue;
							const std::vector<P2>& Ln = LineOf(size_t(E.first));
							if ((E.second == 0 ? Ln.front() : Ln.back()).DistanceTo(Tip) > 0.5) continue;
							const double Turn = std::acos(std::clamp(Dot(Out, WayOut(size_t(E.first), E.second)), -1.0, 1.0)) * 57.29578;
							if (Turn > JoinTurn) continue;
							const bool Same = G.Scenery.Lines[size_t(Pieces[size_t(E.first)].Line)].Name == SeedName;
							const double Score = Turn - (Same ? 8.0 : 0.0);
							if (Score < BestScore) { BestScore = Score; Best = E.first; BestE = E.second; }
						}
					}
				if (Best < 0) break;
				Used[size_t(Best)] = 1;
				Note(size_t(Best));
				std::vector<P2> More = LineOf(size_t(Best));
				if (BestE == 1) std::reverse(More.begin(), More.end());   // (now from the tip on)
				if (End == 0) C.Pts.insert(C.Pts.end(), More.begin() + 1, More.end());
				else
				{
					std::reverse(More.begin(), More.end());
					C.Pts.insert(C.Pts.begin(), More.begin(), More.end() - 1);
				}
			}
		Chains.push_back(std::move(C));
	}

	// The drawn track (RidePlan::Rails in use) by 16 m square, to put each chain's places on it.
	constexpr double SnapCell = 16.0;
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Drawn;
	for (size_t L = 0; L < Plan.Rails.size(); ++L)
	{
		const RidePlan::RailLine& Run = Plan.Rails[L];
		if (Run.Disused) continue;
		for (size_t I = 0; I + 1 < Run.At.size(); ++I)
		{
			const P2 Mid = (Run.At[I] + Run.At[I + 1]) * 0.5;
			Drawn[CellKey(Mid.X, Mid.Z, SnapCell)].emplace_back(int(L), int(I));
		}
	}
	struct Snap
	{
		P2 At;
		double Top = 0.0;
		unsigned char On = 0;
		int Rail = -1, Point = -1;
	};
	const auto SnapTo = [&Plan, &Drawn](const P2& Q, const P2& Way, Snap& Out)
	{
		double Best = SnapM;
		bool Found = false;
		const int Cx = int(std::floor(Q.X / SnapCell)), Cz = int(std::floor(Q.Z / SnapCell));
		for (int Dz = -1; Dz <= 1; ++Dz)
			for (int Dx = -1; Dx <= 1; ++Dx)
			{
				const auto It = Drawn.find(static_cast<long long>(Cx + Dx) * 4000037LL + static_cast<long long>(Cz + Dz));
				if (It == Drawn.end()) continue;
				for (const std::pair<int, int>& Bit : It->second)
				{
					const RidePlan::RailLine& Run = Plan.Rails[size_t(Bit.first)];
					const size_t I = size_t(Bit.second);
					const P2 A = Run.At[I], B = Run.At[I + 1], D = B - A;
					const double L2 = Dot(D, D);
					if (L2 < 1e-9) continue;
					if (std::fabs(Dot(Unit(D), Way)) < 0.7) continue;   // (a line crossing it)
					const double T = std::clamp(Dot(Q - A, D) / L2, 0.0, 1.0);
					const P2 F = A + D * T;
					const double Dist = F.DistanceTo(Q);
					if (Dist >= Best) continue;
					Best = Dist;
					Found = true;
					Out.At = F;
					Out.Top = Run.Top[I] + (Run.Top[I + 1] - Run.Top[I]) * T;
					Out.On = Run.On[T < 0.5 ? I : I + 1];
					Out.Rail = Bit.first;
					Out.Point = int(T < 0.5 ? I : I + 1);
				}
			}
		return Found;
	};

	// Each chain a sample every Step metres, put on the drawn track; where the drawn track stops (the ride's land
	// doesn't reach), the chain is cut into routes there.
	const Frame Fr = G.FrameOf();
	struct Base
	{
		Route R;
		bool Preserved = false;
		int Chain = 0;
	};
	std::vector<Base> Made;
	for (size_t Ci = 0; Ci < Chains.size(); ++Ci)
	{
		const Chain& C = Chains[Ci];
		std::vector<P2> Raw;
		double Carry = 0.0;
		for (size_t I = 0; I + 1 < C.Pts.size(); ++I)
		{
			const double Len = C.Pts[I].DistanceTo(C.Pts[I + 1]);
			if (Len < 1e-6) continue;
			double S = Carry;
			for (; S < Len; S += Step) Raw.push_back(C.Pts[I] + (C.Pts[I + 1] - C.Pts[I]) * (S / Len));
			Carry = S - Len;
		}
		if (Raw.empty() || Raw.back().DistanceTo(C.Pts.back()) > 0.5) Raw.push_back(C.Pts.back());
		if (Raw.size() < 3) continue;
		std::string Name;
		double NameLen = -1.0;
		for (const auto& Nm : C.Names)
			if (!Nm.first.empty() && Nm.second > NameLen) { NameLen = Nm.second; Name = Nm.first; }
		std::vector<Snap> Snaps(Raw.size());
		std::vector<char> Ok(Raw.size(), 0);
		for (size_t I = 0; I < Raw.size(); ++I)
		{
			const P2 Way = Unit(Raw[std::min(I + 1, Raw.size() - 1)] - Raw[I > 0 ? I - 1 : 0]);
			Ok[I] = SnapTo(Raw[I], Way, Snaps[I]) ? 1 : 0;
		}
		for (size_t From = 0; From < Raw.size();)
		{
			if (!Ok[From]) { ++From; continue; }
			size_t To = From;
			while (To + 1 < Raw.size() && Ok[To + 1]) ++To;
			if (To - From + 1 >= 8)
			{
				Base B;
				B.Chain = int(Ci);
				B.Preserved = C.Preserved > 0.5 * C.Len;
				Route& R = B.R;
				R.Name = Name;
				for (size_t K = From; K <= To; ++K)
				{
					// (two places on the same drawn point, where the drawn track was pushed: one kept)
					if (!R.At.empty() && Snaps[K].At.DistanceTo(R.At.back()) < 0.5) continue;
					R.At.push_back(Snaps[K].At);
					R.Top.push_back(float(Snaps[K].Top));
					R.On.push_back(Snaps[K].On);
					R.Rail.push_back(Snaps[K].Rail);
					R.Point.push_back(Snaps[K].Point);
				}
				// (a cut end runs on: the land isn't there, nor anyone to see it)
				R.OpenStart = From > 0 || C.OpenAt[0];
				R.OpenEnd = To + 1 < Raw.size() || C.OpenAt[1];
				if (R.At.size() >= 8) Made.push_back(std::move(B));
			}
			From = To + 1;
		}
	}

	// Along each: how far, how much it bends.
	const auto Measure = [](Route& R)
	{
		const size_t N = R.At.size();
		R.S.assign(N, 0.0);
		for (size_t I = 1; I < N; ++I) R.S[I] = R.S[I - 1] + R.At[I].DistanceTo(R.At[I - 1]);
		std::vector<float> K(N, 0.0f);
		for (size_t I = 0; I < N; ++I)
		{
			const size_t A = I >= 3 ? I - 3 : 0, B = std::min(N - 1, I + 3);
			if (A == I || B == I) continue;
			const P2 U = R.At[I] - R.At[A], V = R.At[B] - R.At[I];
			const double Turn = std::atan2(LeftOf(U, V), Dot(U, V));
			K[I] = float(Turn / std::fmax(1.0, 0.5 * (U.Length() + V.Length())));
		}
		R.Bend.assign(N, 0.0f);
		for (size_t I = 0; I < N; ++I)
		{
			double Sum = 0.0;
			int Count = 0;
			for (size_t J = I >= 4 ? I - 4 : 0; J <= std::min(N - 1, I + 4); ++J) { Sum += K[J]; ++Count; }
			R.Bend[I] = float(Sum / std::max(1, Count));
		}
		R.X0 = R.Z0 = 1e18;
		R.X1 = R.Z1 = -1e18;
		for (const P2& Q : R.At) { R.X0 = std::fmin(R.X0, Q.X); R.Z0 = std::fmin(R.Z0, Q.Z); R.X1 = std::fmax(R.X1, Q.X); R.Z1 = std::fmax(R.Z1, Q.Z); }
	};
	for (Base& B : Made) Measure(B.R);

	// Every route's samples by 16 m square (to find the track beside one, and where a branch leaves).
	std::unordered_map<long long, std::vector<std::pair<int, int>>> Near;
	const auto WayAt = [](const Route& R, size_t I) { return Unit(R.At[std::min(I + 1, R.At.size() - 1)] - R.At[I > 0 ? I - 1 : 0]); };
	std::vector<int> Partner;
	// (twice when a route has to be cut: a route can run on one track of a double line, through a stretch of single
	// line (a viaduct, a junction), and on along the other track beyond it; its trains would keep left on one half
	// and run on the wrong track on the other, so it's cut in two in the single stretch and each half takes its way)
	for (int Pass = 0; Pass < 2; ++Pass)
	{
		Near.clear();
		for (size_t Ri = 0; Ri < Made.size(); ++Ri)
			for (size_t I = 0; I < Made[Ri].R.At.size(); ++I) Near[CellKey(Made[Ri].R.At[I].X, Made[Ri].R.At[I].Z, 16.0)].emplace_back(int(Ri), int(I));
		// Double track: another route parallel 2.5 to 7 m to one side along most of it (each sample tried every 20 m).
		Partner.assign(Made.size(), -1);
		std::vector<std::vector<std::pair<size_t, int>>> Sides(Made.size());   // (sample, the other track's side)
		for (size_t Ri = 0; Ri < Made.size(); ++Ri)
		{
			Route& R = Made[Ri].R;
			R.Double = false;
			R.Way = 0;
			int Tried = 0, Beside = 0;
			double Side = 0.0;
			std::map<int, int> Whose;
			for (size_t I = 0; I < R.At.size(); I += 5)
			{
				++Tried;
				const P2 W = WayAt(R, I);
				double Best = 7.0;
				int BestR = -1;
				double BestSide = 0.0;
				for (int Dz = -1; Dz <= 1; ++Dz)
					for (int Dx = -1; Dx <= 1; ++Dx)
					{
						const auto It = Near.find(CellKey(R.At[I].X + Dx * 16.0, R.At[I].Z + Dz * 16.0, 16.0));
						if (It == Near.end()) continue;
						for (const std::pair<int, int>& Q : It->second)
						{
							if (Q.first == int(Ri)) continue;
							const Route& O = Made[size_t(Q.first)].R;
							const P2 Off = O.At[size_t(Q.second)] - R.At[I];
							const double D = Off.Length();
							if (D < 2.5 || D >= Best) continue;
							if (std::fabs(Dot(W, WayAt(O, size_t(Q.second)))) < 0.96) continue;
							if (std::fabs(Dot(W, Off)) > 2.0) continue;   // (beside it, not ahead)
							Best = D;
							BestR = Q.first;
							BestSide = LeftOf(W, Off) > 0.0 ? 1.0 : -1.0;
						}
					}
				if (BestR >= 0) { ++Beside; Side += BestSide; ++Whose[BestR]; Sides[Ri].emplace_back(I, BestSide > 0.0 ? 1 : -1); }
			}
			if (Tried > 0 && Beside * 2 >= Tried)
			{
				R.Double = true;
				// (UK trains keep to the left: the other track on the right, this one carries trains along it)
				R.Way = Side < 0.0 ? 1 : -1;
				int Most = 0;
				for (const auto& W : Whose)
					if (W.second > Most) { Most = W.second; Partner[Ri] = W.first; }
			}
		}
		if (Pass == 1) break;
		// Cut where the other track changes sides: the best place splitting its samples into one side before and the
		// other after, each at least 2 km and 85% agreed; cut midway along the stretch between them.
		bool Cut = false;
		const size_t Count = Made.size();
		for (size_t Ri = 0; Ri < Count; ++Ri)
		{
			const std::vector<std::pair<size_t, int>>& Sd = Sides[Ri];
			const Route& R = Made[Ri].R;
			if (Sd.size() < 60 || R.Length() < 4000.0) continue;
			std::vector<int> Pre(Sd.size() + 1, 0);
			for (size_t K = 0; K < Sd.size(); ++K) Pre[K + 1] = Pre[K] + Sd[K].second;
			const int Total = Pre.back();
			size_t BestK = 0;
			int BestScore = 0;
			for (size_t K = 1; K < Sd.size(); ++K)
			{
				const int Score = std::abs(Pre[K] - (Total - Pre[K]));
				if (Score > BestScore) { BestScore = Score; BestK = K; }
			}
			if (BestK == 0) continue;
			const int Before = Pre[BestK], After = Total - Pre[BestK];
			const int NB = int(BestK), NA = int(Sd.size() - BestK);
			if (Before * After >= 0) continue;   // (the same side both ways)
			if (std::abs(Before) < int(0.85 * NB) || std::abs(After) < int(0.85 * NA)) continue;
			const size_t I0 = Sd[BestK - 1].first, I1 = Sd[BestK].first;
			if (R.S[I0] < 2000.0 || R.Length() - R.S[I1] < 2000.0) continue;
			const size_t At = (I0 + I1) / 2;
			Base Tail;
			Tail.Chain = Made[Ri].Chain;
			Tail.Preserved = Made[Ri].Preserved;
			Route& T = Tail.R;
			T.Name = R.Name;
			T.At.assign(R.At.begin() + long(At), R.At.end());
			T.Top.assign(R.Top.begin() + long(At), R.Top.end());
			T.On.assign(R.On.begin() + long(At), R.On.end());
			T.Rail.assign(R.Rail.begin() + long(At), R.Rail.end());
			T.Point.assign(R.Point.begin() + long(At), R.Point.end());
			T.OpenStart = true;
			T.OpenEnd = R.OpenEnd;
			Route& H = Made[Ri].R;
			H.At.resize(At + 1); H.Top.resize(At + 1); H.On.resize(At + 1); H.Rail.resize(At + 1); H.Point.resize(At + 1);
			H.OpenEnd = true;
			Measure(H);
			Measure(T);
			Made.push_back(std::move(Tail));
			Cut = true;
		}
		if (!Cut) break;
	}
	// Each route's class: by its name where it has one, else as its partner's, else by its shape.
	for (size_t Ri = 0; Ri < Made.size(); ++Ri)
	{
		Route& R = Made[Ri].R;
		const std::string Lc = Lower(R.Name);
		const double Len = R.Length();
		LineClass C = LineClass::None;
		if (Has(Lc, {"siding", "yard", "depot", "loco", "miniature", "model engineer", "exam", "reception", "sorting", "carriage", "wash", "test track",
				"headshunt", "turntable", "zoo", "express", "museum"}))
			C = LineClass::None;
		else if (Made[Ri].Preserved || HeritageName(Lc)) C = LineClass::Heritage;
		else if (!Lc.empty() && Has(Lc, {"freight", "goods", "mineral", "colliery", "quarry", "works", "power station", "dock", "terminal", " tip", "cement",
				"steel", "refinery", "oil", "sugar", "branch to", "chord freight"}))
			C = LineClass::Freight;
		else if (Has(Lc, {"main line", "diversion"})) C = LineClass::Main;
		else if (!Lc.empty()) C = R.Double ? LineClass::Secondary : LineClass::Branch;
		else C = LineClass::None;   // (unnamed: settled below)
		if (C != LineClass::None && C != LineClass::Main && Len < 300.0) C = LineClass::None;   // (a crossover, a spur)
		R.Class = C;
	}
	for (int Pass = 0; Pass < 2; ++Pass)
		for (size_t Ri = 0; Ri < Made.size(); ++Ri)
		{
			Route& R = Made[Ri].R;
			if (!R.Name.empty() || R.Class != LineClass::None) continue;
			if (Partner[Ri] >= 0 && !Made[size_t(Partner[Ri])].R.Name.empty() && Made[size_t(Partner[Ri])].R.Class != LineClass::None)
			{
				R.Class = Made[size_t(Partner[Ri])].R.Class;
				R.Name = Made[size_t(Partner[Ri])].R.Name;
			}
			else if (Pass == 1 && R.Length() >= 1500.0) R.Class = R.Double ? LineClass::Secondary : LineClass::Branch;
		}

	// Led in and out: an end that meets another route (a junction) carries on along it for LeadM, so its trains come
	// from the line it leaves and go back onto it; an end with nothing beyond it in the ride's land is open; any other
	// end is buffers.
	std::vector<Route> Final;
	for (size_t Ri = 0; Ri < Made.size(); ++Ri)
	{
		const Route& R = Made[Ri].R;
		if (R.Class == LineClass::None) continue;
		Route Out = R;
		std::vector<P2> PreAt, PostAt;
		std::vector<float> PreTop, PostTop;
		std::vector<unsigned char> PreOn, PostOn;
		std::vector<int> PreRail, PostRail, PrePoint, PostPoint;
		for (int End = 0; End < 2; ++End)
		{
			const size_t I = End == 0 ? 0 : R.At.size() - 1;
			const P2 Tip = R.At[I];
			// (our way out of this end)
			const P2 Out0 = End == 0 ? Unit(R.At[0] - R.At[std::min<size_t>(3, R.At.size() - 1)]) : Unit(R.At[I] - R.At[I >= 3 ? I - 3 : 0]);
			int BestR = -1, BestJ = 0;
			double Best = 3.0;
			for (int Dz = -1; Dz <= 1; ++Dz)
				for (int Dx = -1; Dx <= 1; ++Dx)
				{
					const auto It = Near.find(CellKey(Tip.X + Dx * 16.0, Tip.Z + Dz * 16.0, 16.0));
					if (It == Near.end()) continue;
					for (const std::pair<int, int>& Q : It->second)
					{
						if (Q.first == int(Ri)) continue;
						const Route& O = Made[size_t(Q.first)].R;
						if (O.Class == LineClass::None && O.Length() < 300.0) continue;
						const double D = O.At[size_t(Q.second)].DistanceTo(Tip);
						if (D >= Best) continue;
						// (it must carry on our way within LeadTurn, one way or the other along it)
						const double Al = std::fabs(Dot(Out0, WayAt(O, size_t(Q.second))));
						if (Al < std::cos(LeadTurn / 57.29578)) continue;
						Best = D;
						BestR = Q.first;
						BestJ = Q.second;
					}
				}
			bool Open = End == 0 ? R.OpenStart : R.OpenEnd;
			if (BestR >= 0)
			{
				const Route& O = Made[size_t(BestR)].R;
				// (the way along O that carries on out of our end)
				const int Step2 = Dot(Out0, WayAt(O, size_t(BestJ))) > 0.0 ? 1 : -1;
				std::vector<int> Take;
				double Gone = 0.0;
				for (int J = BestJ + Step2; J >= 0 && J < int(O.At.size()) && Gone < LeadM; J += Step2)
				{
					Take.push_back(J);
					Gone = std::fabs(O.S[size_t(J)] - O.S[size_t(BestJ)]);
				}
				if (!Take.empty())
				{
					Open = true;
					for (int J : Take)
					{
						auto& A = End == 0 ? PreAt : PostAt;
						auto& T = End == 0 ? PreTop : PostTop;
						auto& N = End == 0 ? PreOn : PostOn;
						auto& Rl = End == 0 ? PreRail : PostRail;
						auto& Pt = End == 0 ? PrePoint : PostPoint;
						A.push_back(O.At[size_t(J)]);
						T.push_back(O.Top[size_t(J)]);
						N.push_back(O.On[size_t(J)]);
						Rl.push_back(O.Rail[size_t(J)]);
						Pt.push_back(O.Point[size_t(J)]);
					}
				}
			}
			if (!Open)
			{
				// (nothing beyond it in the ride's land, or at the area's edge: it runs on out of sight)
				const P2 Beyond = Tip + Out0 * 40.0;
				const bool NoLand = !Plan.Land || std::isnan(Plan.Land->Height(Beyond.X, Beyond.Z));
				const bool Edge = Tip.X - Bx0 < 250.0 || Bx1 - Tip.X < 250.0 || Tip.Z - Bz0 < 250.0 || Bz1 - Tip.Z < 250.0;
				Open = NoLand || Edge;
			}
			(End == 0 ? Out.OpenStart : Out.OpenEnd) = Open;
		}
		// (the lead in reversed, so it runs towards our start)
		std::reverse(PreAt.begin(), PreAt.end());
		std::reverse(PreTop.begin(), PreTop.end());
		std::reverse(PreOn.begin(), PreOn.end());
		std::reverse(PreRail.begin(), PreRail.end());
		std::reverse(PrePoint.begin(), PrePoint.end());
		Out.OwnFrom = int(PreAt.size());
		Out.OwnTo = Out.OwnFrom + int(R.At.size()) - 1;
		Out.At.insert(Out.At.begin(), PreAt.begin(), PreAt.end());
		Out.Top.insert(Out.Top.begin(), PreTop.begin(), PreTop.end());
		Out.On.insert(Out.On.begin(), PreOn.begin(), PreOn.end());
		Out.Rail.insert(Out.Rail.begin(), PreRail.begin(), PreRail.end());
		Out.Point.insert(Out.Point.begin(), PrePoint.begin(), PrePoint.end());
		Out.At.insert(Out.At.end(), PostAt.begin(), PostAt.end());
		Out.Top.insert(Out.Top.end(), PostTop.begin(), PostTop.end());
		Out.On.insert(Out.On.end(), PostOn.begin(), PostOn.end());
		Out.Rail.insert(Out.Rail.end(), PostRail.begin(), PostRail.end());
		Out.Point.insert(Out.Point.end(), PostPoint.begin(), PostPoint.end());
		Measure(Out);
		// (its cant: banked for the line's speed on the curve, at most 150 mm on standard gauge (0.1 rad))
		Out.Cant.assign(Out.At.size(), 0.0f);
		for (size_t I = 0; I < Out.At.size(); ++I)
		{
			const double K = std::fabs(double(Out.Bend[I]));
			const double V = std::fmin(LineV(Out.Class), std::sqrt(LatGrip / std::fmax(K, 1e-7)));
			Out.Cant[I] = float((Out.Bend[I] >= 0.0f ? 1.0 : -1.0) * std::fmin(0.1, 0.6 * V * V * K / 9.81));
		}
		const P2 Mid = Out.At[size_t((Out.OwnFrom + Out.OwnTo) / 2)];
		const Geo Here = Fr.ToGeo(Mid.X, Mid.Z);
		Out.Electric = ElectricGuess(Lower(Out.Name), Out.Class, Here.Lat, Here.Lon);
		Out.Seed = Mix(HashOf(Out.Name) ^ Mix(uint32_t(int64_t(std::floor(Mid.X / 50.0)) * 73856093LL) ^ uint32_t(int64_t(std::floor(Mid.Z / 50.0)) * 19349663LL)));
		Final.push_back(std::move(Out));
	}
	Net->Routes = std::move(Final);
	Net->Index();

	// The stations: each platform a stop on every route beside it (both tracks of a double line, and a branch's trains
	// along their lead in), the train's middle at the platform's middle.
	std::map<std::string, std::set<int>> Calls;
	for (const RidePlan::Platform& P : Plan.Platforms)
	{
		if (P.Line < 0 || size_t(P.Line) >= Plan.Rails.size()) continue;
		const RidePlan::RailLine& Run = Plan.Rails[size_t(P.Line)];
		const size_t Mi = size_t(std::clamp((P.First + P.Last) / 2, 0, int(Run.At.size()) - 1));
		const P2 Here = Run.At[Mi];
		const P2 W = Unit(Run.At[std::min(Mi + 1, Run.At.size() - 1)] - Run.At[Mi > 0 ? Mi - 1 : 0]);
		std::vector<int> Rs;
		Net->RoutesNear(Here.X, Here.Z, 40.0, Rs);
		for (int Ri : Rs)
		{
			Route& R = Net->Routes[size_t(Ri)];
			double Best = P.Halt ? 9.0 : 30.0;   // (a big station's through lines too)
			int BestI = -1;
			for (size_t I = 0; I < R.At.size(); ++I)
			{
				const double D = R.At[I].DistanceTo(Here);
				if (D < Best && std::fabs(Dot(W, WayAt(R, I))) > 0.9) { Best = D; BestI = int(I); }
			}
			if (BestI < 0) continue;
			bool Dup = false;
			for (const Stop& Sp : R.Stops) Dup = Dup || (Sp.Name == P.Name && std::abs(Sp.At - BestI) < 60);
			if (Dup) continue;
			Stop Sp;
			Sp.At = BestI;
			Sp.Name = P.Name;
			Sp.Halt = P.Halt;
			R.Stops.push_back(Sp);
			if (BestI >= R.OwnFrom && BestI <= R.OwnTo) Calls[P.Name].insert(Ri);
		}
	}
	for (Route& R : Net->Routes)
	{
		std::sort(R.Stops.begin(), R.Stops.end(), [](const Stop& A, const Stop& B) { return A.At < B.At; });
		for (Stop& Sp : R.Stops) Sp.Major = !Sp.Halt && (MajorName(Lower(Sp.Name)) || Calls[Sp.Name].size() >= 5);
	}
	Net->Timetable();
	return Net;
}

void Network::Index()
{
	Cells.clear();
	OnRail.clear();
	for (size_t Ri = 0; Ri < Routes.size(); ++Ri)
	{
		const Route& R = Routes[Ri];
		long long Last = std::numeric_limits<long long>::min();
		for (size_t I = 0; I < R.At.size(); ++I)
		{
			const long long K = CellKey(R.At[I].X, R.At[I].Z, RouteCell);
			if (K != Last)
			{
				std::vector<int>& In = Cells[K];
				if (In.empty() || In.back() != int(Ri)) In.push_back(int(Ri));
				Last = K;
			}
			if (R.Rail[I] >= 0) OnRail[static_cast<long long>(R.Rail[I]) * 10000000LL + R.Point[I]].emplace_back(int(Ri), int(I));
		}
	}
}

void Network::RoutesNear(double X, double Z, double Reach, std::vector<int>& Out) const
{
	Out.clear();
	const int Cx0 = int(std::floor((X - Reach) / RouteCell)), Cx1 = int(std::floor((X + Reach) / RouteCell));
	const int Cz0 = int(std::floor((Z - Reach) / RouteCell)), Cz1 = int(std::floor((Z + Reach) / RouteCell));
	for (int Cz = Cz0; Cz <= Cz1; ++Cz)
		for (int Cx = Cx0; Cx <= Cx1; ++Cx)
		{
			const auto It = Cells.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Cells.end()) continue;
			for (int R : It->second)
				if (std::find(Out.begin(), Out.end(), R) == Out.end()) Out.push_back(R);
		}
	std::sort(Out.begin(), Out.end());
}

// --- The made-up timetable ------------------------------------------------------------------------------------------

void Network::Timetable()
{
	Services.assign(Routes.size(), {});
	for (size_t Ri = 0; Ri < Routes.size(); ++Ri)
	{
		const Route& R = Routes[Ri];
		std::vector<Service>& Out = Services[Ri];
		// (trains run on the long routes: a crossover, a loop, a short spur gets none of its own, the trains passing
		// over it are the lines' it joins; a short line to buffers with a station (a bay, a terminus) has its own)
		const double Own = R.S[size_t(R.OwnTo)] - R.S[size_t(R.OwnFrom)];
		// (a terminus: buffers at one end only; buffers at both is a stub no train reaches; a heritage line's passing
		// loops run open at both ends beside its main line and get none, or their trains would double its own)
		const bool Terminus = (R.OpenStart != R.OpenEnd) && !R.Stops.empty();
		if (Own < 1500.0 && !(Terminus && Own >= 300.0)) continue;
		uint32_t H = R.Seed;
		const auto Next = [&H]() { H = Mix(H + 0x9e3779b9U); return H; };
		const auto Add = [&](Kind K, double Period, int Day, int Evening, int Night)
		{
			Service Sv;
			Sv.Train = K;
			Sv.Period = Period;
			Sv.Phase = double(Next() % uint32_t(Period));
			Sv.Keep[0] = Day; Sv.Keep[1] = Evening; Sv.Keep[2] = Night;
			Sv.Livery = Next() % 3U;
			switch (K)
			{
			case Kind::Intercity: Sv.VMax = 56.0; Sv.Accel = 0.45; Sv.Brake = 0.6; Sv.AllStops = false; Sv.MajorStops = true; break;
			case Kind::Emu: Sv.VMax = R.Class == LineClass::Main ? 45.0 : 40.0; Sv.Accel = 0.7; Sv.Brake = 0.8; break;
			case Kind::Dmu: Sv.VMax = R.Class == LineClass::Branch ? 30.0 : 34.0; Sv.Accel = 0.5; Sv.Brake = 0.7; break;
			case Kind::Freight: Sv.VMax = 27.0; Sv.Accel = 0.18; Sv.Brake = 0.35; Sv.AllStops = false; break;
			default: Sv.VMax = 11.0; Sv.Accel = 0.25; Sv.Brake = 0.4; Sv.Heritage = true; break;
			}
			// (its train: the vehicles front to back)
			std::vector<Model>& C = Sv.Consist;
			const uint32_t Pick = Next();
			switch (K)
			{
			case Kind::Dmu:
				C = {Model::DmuCar, Model::DmuCar};
				if (Pick % 5 == 0) C = {Model::DmuCar, Model::DmuCar, Model::DmuCar, Model::DmuCar};
				break;
			case Kind::Emu:
				C = {Model::EmuCab, Model::EmuMid, Model::EmuMid, Model::EmuCab};
				if (R.Class == LineClass::Main && Pick % 5 < 2) C = {Model::EmuCab, Model::EmuMid, Model::EmuMid, Model::EmuCab, Model::EmuCab, Model::EmuMid, Model::EmuMid, Model::EmuCab};
				break;
			case Kind::Intercity:
				C = {Model::IcPower};
				for (int I = 0; I < 7; ++I) C.push_back(Model::IcCoach);
				C.push_back(Model::IcPower);
				break;
			case Kind::Freight:
			{
				C = {Model::FreightLoco};
				const Model W = Pick % 3 == 0 ? Model::WagonBox : Pick % 3 == 1 ? Model::WagonHopper : Model::WagonTank;
				const int N = 10 + int((Pick >> 8) % 13U);
				for (int I = 0; I < N; ++I) C.push_back(W);
				break;
			}
			case Kind::SteamTank: C = {Model::SteamTank, Model::CoachOld, Model::CoachOld, Model::CoachOld}; break;
			case Kind::SteamBig: C = {Model::SteamBig, Model::SteamTender, Model::CoachOld, Model::CoachOld, Model::CoachOld, Model::CoachOld, Model::CoachOld}; break;
			default: C = {Model::DieselOld, Model::CoachOld, Model::CoachOld, Model::CoachOld, Model::CoachOld}; break;
			}
			Sv.TrainLength = 0.0;
			for (Model M : C) Sv.TrainLength += SpecOf(M).Length + CarGap;
			// (the vehicles that run back to front: a unit's rear cab, the second power car)
			Sv.Flip.assign(C.size(), 0);
			if (K == Kind::Dmu)
				for (size_t I = 1; I < C.size(); I += 2) Sv.Flip[I] = 1;
			if (K == Kind::Emu)
				for (size_t I = 3; I < C.size(); I += 4) Sv.Flip[I] = 1;
			if (K == Kind::Intercity) Sv.Flip.back() = 1;
			// (one each way: along it, back along it, or both on single track)
			// (a terminus: both ways whatever runs beside it (a bay isn't the other track of a double line), the train
			// in to the buffers first and back out after it has stood, so none starts from nowhere at the buffers)
			const bool BothWays = R.Way == 0 || Terminus;
			const int First = !R.OpenStart && R.OpenEnd ? -1 : 1;
			const std::vector<int> Dirs = BothWays ? std::vector<int>{First, -First} : std::vector<int>{R.Way};
			for (int D : Dirs)
			{
				Service One = Sv;
				One.Dir = D;
				Profile(R, One);
				Out.push_back(std::move(One));
			}
			// (single track: back the other way once the first has got in and stood, or half a period on where it runs on)
			if (Dirs.size() == 2)
			{
				Service& Back = Out.back();
				const Service& There = Out[Out.size() - 2];
				const bool FarOpen = There.Dir > 0 ? R.OpenEnd : R.OpenStart;
				Back.Phase = std::fmod(There.Phase + (FarOpen ? Period * 0.5 : There.Total), Period);
			}
		};
		const Kind Local = R.Electric ? Kind::Emu : Kind::Dmu;
		// (a short line to a bay or a terminus: one stopping train in and out, an hour apart; no freight, no expresses)
		if (Own < 1500.0)
		{
			if (R.Class == LineClass::Heritage) Add(Kind::SteamTank, 10800.0, 1, 0, 0);
			else if (R.Class == LineClass::Freight) Add(Kind::Freight, 10800.0, 1, 1, 0);
			else Add(R.Class == LineClass::Branch ? Kind::Dmu : Local, 3600.0, 1, 2, 0);
			continue;
		}
		switch (R.Class)
		{
		case LineClass::Main:
			Add(Kind::Intercity, 1800.0, 1, 2, 0);
			Add(Local, 1800.0, 1, 2, 0);
			Add(Kind::Freight, 3600.0, 1, 1, 1);
			break;
		case LineClass::Secondary:
			Add(Local, 1800.0, 1, 2, 0);
			Add(Kind::Freight, 7200.0, 1, 1, 0);
			break;
		case LineClass::Branch:
			Add(Kind::Dmu, 3600.0, 1, 2, 0);
			break;
		case LineClass::Freight:
			Add(Kind::Freight, 10800.0, 1, 1, 1);
			break;
		case LineClass::Heritage:
			Add(Kind::SteamTank, 10800.0, 1, 0, 0);
			Add(Kind::SteamBig, 10800.0, 1, 0, 0);
			Add(Kind::OldDiesel, 10800.0, 1, 0, 0);
			// (hourly between them: one an hour, in turn)
			if (Out.size() >= 3)
			{
				const double Base = Out[0].Phase;
				const size_t Per = Out.size() / 3;
				for (size_t I = 0; I < Out.size(); I += Per)
				{
					Out[I].Phase = std::fmod(Base + 3600.0 * double(I / Per), 10800.0);
					if (Per == 2) Out[I + 1].Phase = std::fmod(Out[I].Phase + ((Out[I].Dir > 0 ? R.OpenEnd : R.OpenStart) ? 1800.0 : Out[I].Total), 10800.0);
				}
			}
			break;
		default: break;
		}
	}
}

// A run's speed and times along its way: as fast as its train and the line allow, slowing for curves, starting from
// and stopping at its stations (as its pattern says) and the buffers, standing at each.
void Network::Profile(const Route& R, Service& Sv) const
{
	const size_t N = R.At.size();
	const double L = R.Length();
	std::vector<double> D(N);
	std::vector<double> Cap(N);
	for (size_t K = 0; K < N; ++K)
	{
		const size_t I = Sv.Dir > 0 ? K : N - 1 - K;
		D[K] = Sv.Dir > 0 ? R.S[I] : L - R.S[I];
		Cap[K] = std::fmax(MinCurveV, std::fmin(Sv.VMax, std::sqrt(LatGrip / std::fmax(std::fabs(double(R.Bend[I])), 1e-7))));
	}
	const bool ClosedStart = Sv.Dir > 0 ? !R.OpenStart : !R.OpenEnd;
	const bool ClosedEnd = Sv.Dir > 0 ? !R.OpenEnd : !R.OpenStart;
	const double Half = Sv.TrainLength * 0.5;
	Sv.FrontFrom = ClosedStart ? std::fmin(L * 0.5, Sv.TrainLength + 4.0) : 0.0;
	const auto Nearest = [&D, N](double Want)
	{
		const size_t K = size_t(std::lower_bound(D.begin(), D.end(), Want) - D.begin());
		if (K >= N) return N - 1;
		if (K > 0 && Want - D[K - 1] < D[K] - Want) return K - 1;
		return K;
	};
	const size_t K0 = Nearest(Sv.FrontFrom);
	size_t K1 = N - 1;
	if (ClosedEnd) K1 = std::max(K0 + 1, Nearest(L - 4.0));
	K1 = std::min(K1, N - 1);
	std::vector<double> Dwell(N, 0.0);
	std::vector<char> Halt(N, 0);
	if (ClosedStart) { Halt[K0] = 1; Dwell[K0] = StandEnds; }
	if (ClosedEnd) { Halt[K1] = 1; Dwell[K1] = StandEnds; }
	for (const Stop& Sp : R.Stops)
	{
		if (!(Sv.AllStops || (Sv.MajorStops && Sp.Major))) continue;
		const double Mid = Sv.Dir > 0 ? R.S[size_t(Sp.At)] : L - R.S[size_t(Sp.At)];
		const size_t K = Nearest(std::fmin(L - 2.0, Mid + Half));
		if (K <= K0 + 2 || K + 2 >= K1) continue;
		Halt[K] = 1;
		Dwell[K] = Sp.Major ? 60.0 : Sp.Halt ? 25.0 : 35.0;
	}
	std::vector<double> V(N, 0.0);
	for (size_t K = K0; K <= K1; ++K) V[K] = Halt[K] ? 0.0 : Cap[K];
	if (!ClosedStart) V[K0] = Cap[K0];
	for (size_t K = K0 + 1; K <= K1; ++K) V[K] = std::fmin(V[K], std::sqrt(V[K - 1] * V[K - 1] + 2.0 * Sv.Accel * (D[K] - D[K - 1])));
	for (size_t K = K1; K-- > K0;) V[K] = std::fmin(V[K], std::sqrt(V[K + 1] * V[K + 1] + 2.0 * Sv.Brake * (D[K + 1] - D[K])));
	Sv.Arr.assign(N, -1.0);
	Sv.Dep.assign(N, -1.0);
	Sv.V.assign(N, 0.0f);
	double T = 0.0;
	for (size_t K = K0; K <= K1; ++K)
	{
		if (K > K0)
		{
			const double Mean = std::fmax(0.4, 0.5 * (V[K] + V[K - 1]));
			T += (D[K] - D[K - 1]) / Mean;
		}
		Sv.Arr[K] = T;
		T += Dwell[K];
		Sv.Dep[K] = T;
		Sv.V[K] = float(V[K]);
	}
	Sv.Total = ClosedEnd ? Sv.Dep[K1] : Sv.Arr[K1] + (Sv.TrainLength + 10.0) / std::fmax(3.0, V[K1]);
	// (where the arrays are used: K0..K1)
	Sv.FrontFrom = D[K0];
}

double Network::FrontAt(const Route& R, const Service& Sv, double Tau, double& Speed, bool& Standing)
{
	const size_t N = Sv.Arr.size();
	const double L = R.Length();
	size_t K0 = 0;
	while (K0 < N && Sv.Arr[K0] < 0.0) ++K0;
	size_t K1 = N;
	while (K1 > K0 && Sv.Arr[K1 - 1] < 0.0) --K1;
	if (K0 >= K1) { Speed = 0.0; Standing = true; return 0.0; }
	--K1;
	const auto DOf = [&R, &Sv, L, N](size_t K) { const size_t I = Sv.Dir > 0 ? K : N - 1 - K; return Sv.Dir > 0 ? R.S[I] : L - R.S[I]; };
	Standing = false;
	if (Tau <= Sv.Arr[K0]) { Speed = Sv.V[K0]; Standing = Sv.V[K0] < 0.1f; return DOf(K0); }
	// (the last sample it has reached)
	size_t Lo = K0, Hi = K1;
	while (Lo < Hi)
	{
		const size_t Mid = (Lo + Hi + 1) / 2;
		if (Sv.Arr[Mid] <= Tau) Lo = Mid; else Hi = Mid - 1;
	}
	const size_t K = Lo;
	if (Tau < Sv.Dep[K]) { Speed = 0.0; Standing = true; return DOf(K); }
	if (K == K1)
	{
		// (off the end of an open route: running on at its last speed)
		Speed = Sv.V[K];
		Standing = Speed < 0.1;
		return DOf(K) + Speed * (Tau - Sv.Dep[K]);
	}
	const double Dt = std::fmax(1e-6, Sv.Arr[K + 1] - Sv.Dep[K]);
	const double T = std::clamp(Tau - Sv.Dep[K], 0.0, Dt);
	const double V0 = Sv.V[K], V1 = Sv.V[K + 1];
	const double Ds = DOf(K + 1) - DOf(K);
	// (steady acceleration between them; scaled so it lands on the next sample exactly)
	const double A = (V1 - V0) / Dt;
	const double Raw = V0 * T + 0.5 * A * T * T, Full = V0 * Dt + 0.5 * A * Dt * Dt;
	Speed = V0 + A * T;
	return DOf(K) + (Full > 1e-6 ? Raw / Full : T / Dt) * Ds;
}

double Network::ReachAt(const Route& R, const Service& Sv, double Want)
{
	const size_t N = Sv.Arr.size();
	const double L = R.Length();
	size_t K0 = 0;
	while (K0 < N && Sv.Arr[K0] < 0.0) ++K0;
	size_t K1 = N;
	while (K1 > K0 && Sv.Arr[K1 - 1] < 0.0) --K1;
	if (K0 >= K1) return 1e18;
	--K1;
	const auto DOf = [&R, &Sv, L, N](size_t K) { const size_t I = Sv.Dir > 0 ? K : N - 1 - K; return Sv.Dir > 0 ? R.S[I] : L - R.S[I]; };
	if (Want <= DOf(K0)) return Sv.Arr[K0] - (DOf(K0) - Want) / std::fmax(1.0, double(Sv.V[K0]));
	if (Want >= DOf(K1))
	{
		if (Sv.V[K1] < 0.1f) return Want - DOf(K1) < 1.0 ? Sv.Arr[K1] : 1e18;
		return Sv.Dep[K1] + (Want - DOf(K1)) / double(Sv.V[K1]);
	}
	size_t Lo = K0, Hi = K1;
	while (Lo < Hi)
	{
		const size_t Mid = (Lo + Hi + 1) / 2;
		if (DOf(Mid) <= Want) Lo = Mid; else Hi = Mid - 1;
	}
	const size_t K = Lo;
	const double Ds = std::fmax(1e-6, DOf(K + 1) - DOf(K));
	return Sv.Dep[K] + (Sv.Arr[K + 1] - Sv.Dep[K]) * (Want - DOf(K)) / Ds;
}

int Network::SampleAt(int RouteIndex, double S) const
{
	const Route& R = Routes[size_t(RouteIndex)];
	const size_t K = size_t(std::lower_bound(R.S.begin(), R.S.end(), S) - R.S.begin());
	return int(std::min(K, R.S.size() - 1));
}

void Network::PlaceAt(const Route& R, double S, double& X, double& Y, double& Z, double& Dx, double& Dz, float* Cant, unsigned char* On) const
{
	const size_t N = R.S.size();
	S = std::clamp(S, 0.0, R.Length());
	size_t K = size_t(std::upper_bound(R.S.begin(), R.S.end(), S) - R.S.begin());
	K = K == 0 ? 0 : K - 1;
	if (K + 1 >= N) K = N - 2;
	const double Len = std::fmax(1e-6, R.S[K + 1] - R.S[K]);
	const double T = std::clamp((S - R.S[K]) / Len, 0.0, 1.0);
	const P2 P = R.At[K] + (R.At[K + 1] - R.At[K]) * T;
	X = P.X;
	Z = P.Z;
	Y = double(R.Top[K]) + double(R.Top[K + 1] - R.Top[K]) * T + RailTop;
	const P2 W = Unit(R.At[K + 1] - R.At[K]);
	Dx = W.X;
	Dz = W.Z;
	if (Cant) *Cant = R.Cant[K] + (R.Cant[K + 1] - R.Cant[K]) * float(T);
	if (On) *On = R.On[T < 0.5 ? K : K + 1];
}

// --- Where the trains are now -----------------------------------------------------------------------------------------

namespace
{
// The slots of a service that are running now (Tau seconds into their run) or due within Ahead seconds (negative Tau).
template <typename F>
void EachRun(const Service& Sv, const Clock& Now, double Ahead, F&& Each)
{
	const int Band = Network::Band(Now.Hour);
	if (Sv.Heritage && !Network::HeritageRuns(Now)) return;
	const long long Hi = static_cast<long long>(std::floor((Now.T + Ahead - Sv.Phase) / Sv.Period));
	for (long long K = Hi; K > Hi - 64; --K)
	{
		const double Start = Sv.Phase + double(K) * Sv.Period;
		const double Tau = Now.T - Start;
		if (Tau > Sv.Total) break;
		// (the band it set off in: the evening's every other train, the night's none)
		const int B = Network::Band(Now.Hour < 0.0 ? Now.Hour : Now.Hour - std::fmax(0.0, Tau) / 3600.0);
		const int Keep = Sv.Keep[B >= 0 ? B : Band];
		if (Keep <= 0 || ((K % Keep) + Keep) % Keep != 0) continue;
		Each(K, Tau);
	}
}
} // namespace

void Network::Near(const Clock& Now, double X, double Z, double Reach, std::vector<TrainNow>& Out) const
{
	Out.clear();
	std::vector<int> Rs;
	RoutesNear(X, Z, Reach + 300.0, Rs);
	for (int Ri : Rs)
	{
		const Route& R = Routes[size_t(Ri)];
		const double L = R.Length();
		if (X < R.X0 - Reach || X > R.X1 + Reach || Z < R.Z0 - Reach || Z > R.Z1 + Reach) continue;
		const std::vector<Service>& Svs = Services[size_t(Ri)];
		for (size_t Si = 0; Si < Svs.size(); ++Si)
		{
			const Service& Sv = Svs[Si];
			EachRun(Sv, Now, 0.0, [&](long long Slot, double Tau)
			{
				if (Tau < 0.0) return;
				double Speed = 0.0;
				bool Standing = false;
				const double Front = FrontAt(R, Sv, Tau, Speed, Standing);
				// (quick: its middle within reach and its length)
				{
					const double Mid = Front - Sv.TrainLength * 0.5;
					const double S = Sv.Dir > 0 ? Mid : L - Mid;
					double Px, Py, Pz, Dx, Dz;
					PlaceAt(R, S, Px, Py, Pz, Dx, Dz);
					if (std::hypot(Px - X, Pz - Z) > Reach + Sv.TrainLength * 0.5 + 50.0) return;
				}
				TrainNow T;
				T.Id = (static_cast<uint64_t>(uint32_t(Ri)) << 40) ^ (static_cast<uint64_t>(Si) << 32) ^ static_cast<uint64_t>(uint32_t(Slot));
				T.Route = Ri;
				T.Service = int(Si);
				T.Train = Sv.Train;
				T.Livery = Sv.Livery;
				T.Speed = Speed;
				T.Front = Front;
				T.Standing = Standing;
				{
					double SpeedOn = 0.0;
					bool StandOn = false;
					FrontAt(R, Sv, Tau + 2.0, SpeedOn, StandOn);
					T.Braking = !Standing && Speed > 0.5 && SpeedOn < Speed - 0.4;
				}
				double Gone = 0.0;
				for (size_t C = 0; C < Sv.Consist.size(); ++C)
				{
					const ModelSpec& M = SpecOf(Sv.Consist[C]);
					const double Mid = Front - Gone - M.Length * 0.5;
					Gone += M.Length + CarGap;
					if (Mid < 0.0 || Mid > L) continue;   // (not on the line yet, or off its end)
					const double A = Sv.Dir > 0 ? Mid + M.Bogie : L - (Mid + M.Bogie);
					const double B = Sv.Dir > 0 ? Mid - M.Bogie : L - (Mid - M.Bogie);
					double Ax, Ay, Az, Bx, By, Bz, Dx, Dz;
					float Cant = 0.0f;
					unsigned char On = 0;
					PlaceAt(R, A, Ax, Ay, Az, Dx, Dz);
					PlaceAt(R, B, Bx, By, Bz, Dx, Dz);
					CarNow Car;
					double Cx, Cy, Cz;
					PlaceAt(R, Sv.Dir > 0 ? Mid : L - Mid, Cx, Cy, Cz, Dx, Dz, &Cant, &On);
					Car.X = 0.5 * (Ax + Bx);
					Car.Y = 0.5 * (Ay + By);
					Car.Z = 0.5 * (Az + Bz);
					double Fx = Ax - Bx, Fy = Ay - By, Fz = Az - Bz;
					const double Fl = std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz);
					if (Fl < 1e-6) { Fx = Sv.Dir * Dx; Fy = 0.0; Fz = Sv.Dir * Dz; }
					else { Fx /= Fl; Fy /= Fl; Fz /= Fl; }
					Car.Fx = Fx; Car.Fy = Fy; Car.Fz = Fz;
					Car.Roll = Sv.Dir > 0 ? double(Cant) : -double(Cant);
					Car.Is = Sv.Consist[C];
					// (the rear cab of a unit and the second power car face backwards)
					Car.Reversed = C < Sv.Flip.size() && Sv.Flip[C] != 0;
					Car.Lead = C == 0;
					Car.Tail = C + 1 == Sv.Consist.size();
					Car.InTunnel = On == 2;
					T.Dist = std::fmin(T.Dist, std::hypot(Car.X - X, Car.Z - Z));
					T.Cars.push_back(Car);
				}
				if (!T.Cars.empty() && T.Dist <= Reach) Out.push_back(std::move(T));
			});
		}
	}
	std::sort(Out.begin(), Out.end(), [](const TrainNow& A, const TrainNow& B) { return A.Dist < B.Dist; });
}

bool Network::Closed(const Clock& Now, int Line, int First, int Last, double LeadS, double* Until) const
{
	if (Until) *Until = 1e18;
	if (Last < First) std::swap(First, Last);
	// (each route over it: where on it, from first to last)
	std::map<int, std::pair<double, double>> Over;
	for (int P = First; P <= Last; ++P)
	{
		const auto It = OnRail.find(static_cast<long long>(Line) * 10000000LL + P);
		if (It == OnRail.end()) continue;
		for (const std::pair<int, int>& Q : It->second)
		{
			const double S = Routes[size_t(Q.first)].S[size_t(Q.second)];
			auto Got = Over.find(Q.first);
			if (Got == Over.end()) Over[Q.first] = {S, S};
			else { Got->second.first = std::fmin(Got->second.first, S); Got->second.second = std::fmax(Got->second.second, S); }
		}
	}
	bool Shut = false;
	for (const auto& O : Over)
	{
		const Route& R = Routes[size_t(O.first)];
		const double L = R.Length();
		const double S0 = O.second.first - 4.0, S1 = O.second.second + 4.0;
		for (const Service& Sv : Services[size_t(O.first)])
		{
			const double In = Sv.Dir > 0 ? S0 : L - S1, Off = Sv.Dir > 0 ? S1 : L - S0;
			const double TIn = ReachAt(R, Sv, In), TOff = ReachAt(R, Sv, Off + Sv.TrainLength);
			if (TIn > 1e17) continue;
			EachRun(Sv, Now, LeadS + std::fmax(0.0, TIn) + 60.0, [&](long long, double Tau)
			{
				// (Tau is now less the slot's start: closed from LeadS before the front gets there till the back's off)
				if (Tau >= TIn - LeadS && Tau <= TOff)
				{
					Shut = true;
					if (Until) *Until = std::fmin(*Until, std::fmax(0.0, TIn - Tau));
				}
			});
		}
	}
	return Shut;
}

bool Network::Occupied(const Clock& Now, int Ri, int Dir, double S0, double S1) const
{
	const Route& R = Routes[size_t(Ri)];
	const double L = R.Length();
	for (const Service& Sv : Services[size_t(Ri)])
	{
		if (Sv.Dir != Dir) continue;
		bool Hit = false;
		EachRun(Sv, Now, 0.0, [&](long long, double Tau)
		{
			if (Tau < 0.0 || Hit) return;
			double Speed = 0.0;
			bool Standing = false;
			const double Front = FrontAt(R, Sv, Tau, Speed, Standing);
			const double Back = Front - Sv.TrainLength;
			const double A = Dir > 0 ? Back : L - Front, B = Dir > 0 ? Front : L - Back;
			if (B >= S0 && A <= S1) Hit = true;
		});
		if (Hit) return true;
	}
	return false;
}

std::string Network::Summary() const
{
	double Km[6] = {};
	int Count[6] = {};
	int Doubles = 0, Wired = 0, Stops = 0, Buffers = 0;
	size_t Runs = 0;
	for (size_t Ri = 0; Ri < Routes.size(); ++Ri)
	{
		const Route& R = Routes[Ri];
		const int C = int(R.Class);
		Km[C] += (R.S[size_t(R.OwnTo)] - R.S[size_t(R.OwnFrom)]) / 1000.0;
		++Count[C];
		Doubles += R.Double ? 1 : 0;
		Wired += R.Electric ? 1 : 0;
		Stops += int(R.Stops.size());
		Buffers += (R.OpenStart ? 0 : 1) + (R.OpenEnd ? 0 : 1);
		Runs += Services[Ri].size();
	}
	char Buf[512];
	std::snprintf(Buf, sizeof(Buf), "%zu routes (%d double track, %d wired, %d buffer ends, %d stops), %zu services: main %d (%.1f km), secondary %d (%.1f km), branch %d (%.1f km), freight %d (%.1f km), heritage %d (%.1f km)",
		Routes.size(), Doubles, Wired, Buffers, Stops, Runs, Count[1], Km[1], Count[2], Km[2], Count[3], Km[3], Count[4], Km[4], Count[5], Km[5]);
	return Buf;
}

} // namespace Trains
} // namespace TorqueWorld
