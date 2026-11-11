#include "WorldRegion.h"

#include "WorldFiles.h"
#include "WorldSha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace TorqueWorld
{
using TorqueFit::Json;

namespace
{
// The Godot game holds positions as 32-bit floats: so does this, so both games' graphs are the same
// to the last bit (a ride planned on one is the same road on the other).
inline double F32(double V) { return double(float(V)); }
inline double Snapped(double V, double Step) { return std::floor(V / Step + 0.5) * Step; }
inline int FloorInt(double V) { return int(std::floor(V)); }

bool ReadJson(const std::string& Path, Json& Out)
{
	std::string Text;
	return Files::Read(Path, Text) && Json::Parse(Text, Out);
}

std::string Text(const Json* V)
{
	if (V == nullptr) return std::string();
	if (V->IsString()) return V->S;
	if (V->IsNumber()) return std::to_string(static_cast<long long>(V->N));
	return std::string();
}

const char* GenericName(const std::string& Class)
{
	if (Class == "motorway") return "Motorway";
	if (Class == "trunk") return "Trunk road";
	if (Class == "primary") return "Main road";
	if (Class == "secondary") return "B road";
	if (Class == "tertiary" || Class == "unclassified") return "Minor road";
	if (Class == "residential" || Class == "living_street") return "Street";
	if (Class == "track") return "Track";
	return "Road";
}

void Strings(const Json* List, std::vector<std::string>& Out)
{
	Out.clear();
	if (List == nullptr || !List->IsArray()) return;
	for (const Json& V : List->A) Out.push_back(V.S);
}
} // namespace

void Region::Finish()
{
	Adjacent.assign(Nodes.size(), std::vector<int>());
	Grid.clear();
	for (size_t I = 0; I < Nodes.size(); ++I)
		Grid[{FloorInt(Nodes[I].X / GridCell), FloorInt(Nodes[I].Z / GridCell)}].push_back(int(I));
	for (size_t Ei = 0; Ei < Edges.size(); ++Ei)
	{
		RoadEdge& E = Edges[Ei];
		double Len = 0.0;
		for (size_t K = 1; K < E.Line.size(); ++K) Len += F32(E.Line[K].DistanceTo(E.Line[K - 1]));
		E.Length = F32(Len);
		Adjacent[size_t(E.A)].push_back(int(Ei));
		Adjacent[size_t(E.B)].push_back(int(Ei));
	}
	std::sort(Bans.begin(), Bans.end());
	Bans.erase(std::unique(Bans.begin(), Bans.end()), Bans.end());
	// The connected pieces, numbered in order of their lowest node.
	Component.assign(Nodes.size(), -1);
	ComponentSize.clear();
	std::vector<int> Stack;
	for (size_t N0 = 0; N0 < Nodes.size(); ++N0)
	{
		if (Component[N0] >= 0) continue;
		const int Piece = int(ComponentSize.size());
		Stack.assign(1, int(N0));
		Component[N0] = Piece;
		int Count = 0;
		while (!Stack.empty())
		{
			const int N = Stack.back();
			Stack.pop_back();
			++Count;
			for (int Ei : Adjacent[size_t(N)])
			{
				const int M = OtherEnd(Ei, N);
				if (Component[size_t(M)] < 0)
				{
					Component[size_t(M)] = Piece;
					Stack.push_back(M);
				}
			}
		}
		ComponentSize.push_back(Count);
	}
}

bool Region::FromRows(const Json& Rows, Region& Out)
{
	const Json* NodesJ = Rows.Get("nodes");
	const Json* EdgesJ = Rows.Get("edges");
	if (NodesJ == nullptr || !NodesJ->IsArray() || EdgesJ == nullptr || !EdgesJ->IsArray()) return false;
	Out = Region();
	Strings(Rows.Get("classes"), Out.Classes);
	Strings(Rows.Get("surfaces"), Out.Surfaces);
	Strings(Rows.Get("names"), Out.Names);
	const int NodeCount = int(NodesJ->A.size());
	Out.Nodes.reserve(NodesJ->A.size());
	for (const Json& N : NodesJ->A)
	{
		if (N.Size() < 2) return false;
		Out.Nodes.push_back(P2(F32(N.A[0].N), F32(N.A[1].N)));
	}
	Out.Edges.reserve(EdgesJ->A.size());
	for (const Json& E : EdgesJ->A)
	{
		RoadEdge R;
		R.A = int(E.Num("a", -1));
		R.B = int(E.Num("b", -1));
		if (R.A < 0 || R.B < 0 || R.A >= NodeCount || R.B >= NodeCount) return false;
		R.Class = int(E.Num("c"));
		R.Surface = int(E.Num("s"));
		R.Limit = int(E.Num("l"));
		R.Name = int(E.Num("n"));
		R.Oneway = int(E.Num("o"));
		if (R.Class < 0 || R.Class >= int(Out.Classes.size()) || R.Surface < 0 || R.Surface >= int(Out.Surfaces.size()) || R.Name < 0 || R.Name >= int(Out.Names.size()))
			return false;
		R.Line.push_back(Out.Nodes[size_t(R.A)]);
		if (const Json* P = E.Get("p"))
			for (size_t K = 0; K + 1 < P->A.size(); K += 2) R.Line.push_back(P2(F32(P->A[K].N), F32(P->A[K + 1].N)));
		R.Line.push_back(Out.Nodes[size_t(R.B)]);
		Out.Edges.push_back(std::move(R));
	}
	if (const Json* BansJ = Rows.Get("bans"))
		for (const Json& B : BansJ->A)
			if (B.Size() >= 3) Out.Bans.push_back({int(B.A[0].N), int(B.A[1].N), int(B.A[2].N)});
	Out.Finish();
	return true;
}

std::string Region::CellDir(const std::vector<std::string>& CellRoots, CellId Cell)
{
	for (const std::string& Root : CellRoots)
	{
		const std::string D = Files::Join(Root, "c" + Cell.Name());
		if (Files::Exists(Files::Join(D, "region.json"))) return D;
	}
	return std::string();
}

bool Region::NamedArea(const std::vector<std::string>& CellRoots, const std::string& Name, Region& Out, std::string* Error)
{
	for (const std::string& Root : CellRoots)
	{
		Json Areas;
		if (!ReadJson(Files::Join(Root, "areas.json"), Areas)) continue;
		const Json* A = Areas.Get(Name);
		if (A == nullptr || !A->IsObject() || A->Get("frame") == nullptr) continue;
		std::vector<std::string> Dirs;
		for (const CellId& C : CellsInBox(A->Num("south"), A->Num("west"), A->Num("north"), A->Num("east")))
		{
			const std::string D = CellDir(CellRoots, C);
			if (!D.empty()) Dirs.push_back(D);
		}
		return Stitch("area:" + Name, A->Str("title", Name), Dirs, A->Get("frame")->Num("lat"), A->Get("frame")->Num("lon"), Out, Error);
	}
	if (Error != nullptr) *Error = "This game doesn't know that part of the world.";
	return false;
}

bool Region::Stitch(const std::string& InId, const std::string& InTitle, const std::vector<std::string>& Dirs, double Lat0, double Lon0, Region& Out, std::string* Error)
{
	Out = Region();
	const MetresPerDeg Ma = MetresPerDegree(Lat0);
	std::unordered_map<std::string, int> NodeAt;   // short id -> node in the joined graph
	std::unordered_map<std::string, int> EdgeSeen; // "segment|a|b" -> edge
	std::unordered_map<std::string, int> NameAt;
	std::unordered_map<std::string, bool> Seen;
	std::map<std::pair<int, int>, std::vector<std::array<double, 4>>> BuildingSeen;   // in 4 m squares
	std::vector<std::string> Hashes;
	Out.Names.push_back(std::string());
	NameAt[std::string()] = 0;
	bool AnyBounds = false;
	for (const std::string& Dir : Dirs)
	{
		Json Meta, Rows;
		if (!ReadJson(Files::Join(Dir, "region.json"), Meta) || !ReadJson(Files::Join(Dir, "roads.json"), Rows) || !Meta.IsObject() || !Rows.IsObject()
			|| Rows.Get("ids") == nullptr || !Rows.Get("ids")->IsArray() || Meta.Get("frame") == nullptr || Rows.Get("nodes") == nullptr || Rows.Get("edges") == nullptr)
		{
			if (Error != nullptr) *Error = Dir + " isn't a readable cell";
			continue;
		}
		Strings(Rows.Get("classes"), Out.Classes);   // (the baker's own lists, the same in every cell)
		Strings(Rows.Get("surfaces"), Out.Surfaces);
		const double LatC = Meta.Get("frame")->Num("lat"), LonC = Meta.Get("frame")->Num("lon");
		const MetresPerDeg Mc = MetresPerDegree(LatC);
		// A cell-frame (x, z) to the area's: x scaled by the ratio of a degree of longitude's metres, z of latitude's.
		const double Sx = Ma.Lon / Mc.Lon, Ox = (LonC - Lon0) * Ma.Lon, Sz = Ma.Lat / Mc.Lat, Oz = -(LatC - Lat0) * Ma.Lat;
		auto ToArea = [&](double X, double Z) { return P2(F32(Ox + X * Sx), F32(Oz + Z * Sz)); };
		Hashes.push_back(Meta.Str("hash"));
		if (const Json* Cr = Meta.Get("credits"))
			for (const Json& C : Cr->A)
				if (std::find(Out.Credits.begin(), Out.Credits.end(), C.S) == Out.Credits.end()) Out.Credits.push_back(C.S);
		Out.Sea = Out.Sea || Meta.Flag("sea");
		Out.Country = Meta.Str("region", Out.Country);
		if (const Json* Cb = Meta.Get("bounds"))
			if (Cb->Size() >= 4)
			{
				const P2 R0 = ToArea(Cb->A[0].N, Cb->A[1].N), R1 = ToArea(Cb->A[2].N, Cb->A[3].N);
				// (kept as Godot's Rect2 keeps it: a corner and a size, in floats)
				const Rect R{R0.X, R0.Z, F32(R0.X + F32(R1.X - R0.X)), F32(R0.Z + F32(R1.Z - R0.Z))};
				if (!AnyBounds) Out.Bounds = R;
				else Out.Bounds.Merge(R);
				AnyBounds = true;
			}
		// The roads.
		const Json& Ids = *Rows.Get("ids");
		const Json& CellNodes = *Rows.Get("nodes");
		if (CellNodes.A.size() < Ids.A.size()) continue;
		std::vector<int> Local;
		Local.reserve(Ids.A.size());
		for (size_t I = 0; I < Ids.A.size(); ++I)
		{
			const std::string Key = Text(&Ids.A[I]);
			auto It = NodeAt.find(Key);
			if (It == NodeAt.end())
			{
				It = NodeAt.emplace(Key, int(Out.Nodes.size())).first;
				Out.Nodes.push_back(ToArea(CellNodes.A[I].A[0].N, CellNodes.A[I].A[1].N));
			}
			Local.push_back(It->second);
		}
		const Json* CellNames = Rows.Get("names");
		const Json& CellEdges = *Rows.Get("edges");
		std::vector<int> EdgeLocal(CellEdges.A.size(), -1);
		for (size_t Ei = 0; Ei < CellEdges.A.size(); ++Ei)
		{
			const Json& E = CellEdges.A[Ei];
			const int La = int(E.Num("a", -1)), Lb = int(E.Num("b", -1));
			if (La < 0 || Lb < 0 || La >= int(Local.size()) || Lb >= int(Local.size())) continue;
			RoadEdge R;
			R.A = Local[size_t(La)];
			R.B = Local[size_t(Lb)];
			const std::string Key = Text(E.Get("g")) + "|" + std::to_string(R.A) + "|" + std::to_string(R.B);
			auto Had = EdgeSeen.find(Key);
			if (Had != EdgeSeen.end())
			{
				EdgeLocal[Ei] = Had->second;
				continue;
			}
			const int Ni = int(E.Num("n"));
			const std::string Nm = CellNames != nullptr && Ni >= 0 && Ni < int(CellNames->A.size()) ? CellNames->A[size_t(Ni)].S : std::string();
			auto NameIt = NameAt.find(Nm);
			if (NameIt == NameAt.end())
			{
				NameIt = NameAt.emplace(Nm, int(Out.Names.size())).first;
				Out.Names.push_back(Nm);
			}
			R.Class = int(E.Num("c"));
			R.Surface = int(E.Num("s"));
			R.Limit = int(E.Num("l"));
			R.Name = NameIt->second;
			R.Oneway = int(E.Num("o"));
			R.Line.push_back(Out.Nodes[size_t(R.A)]);
			if (const Json* P = E.Get("p"))
				for (size_t K = 0; K + 1 < P->A.size(); K += 2) R.Line.push_back(ToArea(P->A[K].N, P->A[K + 1].N));
			R.Line.push_back(Out.Nodes[size_t(R.B)]);
			EdgeSeen[Key] = int(Out.Edges.size());
			EdgeLocal[Ei] = int(Out.Edges.size());
			Out.Edges.push_back(std::move(R));
		}
		if (const Json* BansJ = Rows.Get("bans"))
			for (const Json& B : BansJ->A)
			{
				if (B.Size() < 3) continue;
				const int E0 = int(B.A[0].N), N = int(B.A[1].N), E1 = int(B.A[2].N);
				if (E0 < 0 || E1 < 0 || N < 0 || E0 >= int(EdgeLocal.size()) || E1 >= int(EdgeLocal.size()) || N >= int(Local.size())) continue;
				if (EdgeLocal[size_t(E0)] >= 0 && EdgeLocal[size_t(E1)] >= 0) Out.Bans.push_back({EdgeLocal[size_t(E0)], Local[size_t(N)], EdgeLocal[size_t(E1)]});
			}
		// What stands beside them.
		Json RowsB;
		if (ReadJson(Files::Join(Dir, "buildings.json"), RowsB) && RowsB.IsArray())
			for (const Json& B : RowsB.A)
			{
				if (B.Size() < 5) continue;
				const P2 P = ToArea(B.A[0].N, B.A[1].N);
				const double W = B.A[2].N, D = B.A[3].N;
				// Two cells that both have a building put it a few centimetres apart (each rounds its own frame's
				// metres): one within half a metre of the same size is the same one.
				const std::pair<int, int> Sq{FloorInt(P.X / 4.0), FloorInt(P.Z / 4.0)};
				bool Dup = false;
				for (int Dz = -1; Dz <= 1 && !Dup; ++Dz)
					for (int Dx = -1; Dx <= 1 && !Dup; ++Dx)
					{
						auto It = BuildingSeen.find({Sq.first + Dx, Sq.second + Dz});
						if (It == BuildingSeen.end()) continue;
						for (const std::array<double, 4>& Q : It->second)
							if (F32(P2(Q[0], Q[1]).DistanceTo(P)) < 0.5 && std::fabs(Q[2] - F32(W)) < 0.2 && std::fabs(Q[3] - F32(D)) < 0.2)
							{
								Dup = true;
								break;
							}
					}
				if (Dup) continue;
				BuildingSeen[Sq].push_back({P.X, P.Z, F32(W), F32(D)});
				Building Row;
				Row.X = Snapped(P.X, 0.1);
				Row.Z = Snapped(P.Z, 0.1);
				Row.W = W;
				Row.D = D;
				Row.Yaw = B.A[4].N;
				Row.Height = B.Size() > 5 ? B.A[5].N : 0.0;
				for (size_t K = 6; K < B.A.size(); ++K) Row.More.push_back(B.A[K].N);
				Out.Buildings.push_back(std::move(Row));
			}
		static const char* Lists[3] = {"pois", "landmarks", "places"};
		for (int L = 0; L < 3; ++L)
		{
			Json RowsX;
			if (!ReadJson(Files::Join(Dir, std::string(Lists[L]) + ".json"), RowsX) || !RowsX.IsArray()) continue;
			for (const Json& D : RowsX.A)
			{
				if (!D.IsObject()) continue;
				const P2 P = ToArea(D.Num("x"), D.Num("z"));
				const std::string Key = std::string(Lists[L]) + "|" + D.Str("kind") + "|" + D.Str("name") + "|" + std::to_string(std::lround(P.X / 10.0)) + "|"
					+ std::to_string(std::lround(P.Z / 10.0));
				if (Seen.count(Key) != 0) continue;
				Seen[Key] = true;
				Json Row = D;
				Row.Set("x", Json::MakeNumber(Snapped(P.X, 0.1)));
				Row.Set("z", Json::MakeNumber(Snapped(P.Z, 0.1)));
				(L == 0 ? Out.Pois : (L == 1 ? Out.Landmarks : Out.Places)).push_back(std::move(Row));
			}
		}
		Out.CellMetas.emplace_back(Dir, std::move(Meta));
	}
	if (Out.Nodes.size() < 2)
	{
		if (Error != nullptr && Error->empty()) *Error = "None of that part of the world is here yet.";
		return false;
	}
	Out.Id = InId;
	Out.Title = InTitle;
	std::sort(Hashes.begin(), Hashes.end());
	std::string Joined;
	for (size_t I = 0; I < Hashes.size(); ++I) Joined += (I > 0 ? "|" : "") + Hashes[I];
	Out.Hash = Sha256Hex(Joined).substr(0, 16);
	// (every cell of the world's store has its place profile: its credit goes with them)
	static const char* ProfileCredit = "Place: Natural Earth; K\xc3\xb6ppen-Geiger zones, Beck, H.E. et al. (2023) (CC BY 4.0); land cover and ground "
		"colour, \xc2\xa9 ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA "
		"WorldCover consortium (CC BY 4.0); climate, CRU TS 4.09, University of East Anglia and Met Office (Open Government Licence).";
	if (std::find(Out.Credits.begin(), Out.Credits.end(), std::string(ProfileCredit)) == Out.Credits.end()) Out.Credits.push_back(ProfileCredit);
	Out.Theme = Out.Sea ? "coastal" : "hills";
	Out.FrameLat = Lat0;
	Out.FrameLon = Lon0;
	Out.Finish();
	return true;
}

namespace
{
const double NoNumber = std::numeric_limits<double>::quiet_NaN();
inline double NumOr(const Json& V) { return V.IsNumber() ? V.N : NoNumber; }
inline std::string Word(const Json* List, const Json& Index)
{
	const int I = int(Index.N);
	return List != nullptr && I >= 0 && I < int(List->A.size()) ? List->A[size_t(I)].S : std::string();
}
// A line of points written as a first (lat, lon) and then steps, in 1e-5 degrees, from Row[First].
std::vector<P2> Steps(const Frame& F, const Json& Row, size_t First)
{
	std::vector<P2> Out;
	long long Lat = static_cast<long long>(Row.A[First].N), Lon = static_cast<long long>(Row.A[First + 1].N);
	Out.push_back(F.FromGeo(double(Lat) * 1e-5, double(Lon) * 1e-5));
	for (size_t K = First + 2; K + 1 < Row.A.size(); K += 2)
	{
		Lat += static_cast<long long>(Row.A[K].N);
		Lon += static_cast<long long>(Row.A[K + 1].N);
		Out.push_back(F.FromGeo(double(Lat) * 1e-5, double(Lon) * 1e-5));
	}
	return Out;
}
} // namespace

void Region::AddSpans(const Json& Rows)
{
	const Frame F = FrameOf();
	for (const Json& Row : Rows.A)
	{
		if (Row.Size() < 5) continue;
		Span S;
		S.Kind = int(Row.A[0].N);
		S.A = F.FromGeo(Row.A[1].N * 1e-5, Row.A[2].N * 1e-5);
		S.B = F.FromGeo(Row.A[3].N * 1e-5, Row.A[4].N * 1e-5);
		Spans.push_back(S);
	}
}

void Region::AddStreams(const Json& Rows)
{
	const Frame F = FrameOf();
	for (const Json& Row : Rows.A)
	{
		if (Row.Size() < 5) continue;
		Stream S;
		S.Kind = int(Row.A[0].N);
		S.Line = Steps(F, Row, 1);
		Streams.push_back(std::move(S));
	}
}

void Region::AddFeatures(const Json& File)
{
	if (!File.IsObject() || int(File.Num("format")) != FeaturesFormat) return;
	const Frame F = FrameOf();
	const Json* Kinds = File.Get("kinds");
	const Json* FileNames = File.Get("names");
	const Json* Words = File.Get("words");
	const int KindCount = Kinds != nullptr ? int(Kinds->A.size()) : 0;
	if (const Json* Rows = File.Get("points"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 8 || int(Row.A[0].N) < 0 || int(Row.A[0].N) >= KindCount) continue;
			FeaturePoint P;
			P.Kind = Word(Kinds, Row.A[0]);
			P.At = F.FromGeo(Row.A[1].N * 1e-5, Row.A[2].N * 1e-5);
			P.Height = NumOr(Row.A[3]);
			P.Elevation = NumOr(Row.A[4]);
			P.Heading = NumOr(Row.A[5]);
			P.Name = Word(FileNames, Row.A[6]);
			P.Id = Row.A[7].S;
			Scenery.Points.push_back(std::move(P));
		}
	if (const Json* Rows = File.Get("lines"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 10 || int(Row.A[0].N) < 0 || int(Row.A[0].N) >= KindCount) continue;
			FeatureLine L;
			L.Kind = Word(Kinds, Row.A[0]);
			L.Height = NumOr(Row.A[1]);
			L.Width = NumOr(Row.A[2]);
			L.Flags = int(Row.A[3].N);
			L.Name = Word(FileNames, Row.A[4]);
			L.Id = Row.A[5].S;
			L.Line = Steps(F, Row, 6);
			Scenery.Lines.push_back(std::move(L));
		}
	if (const Json* Rows = File.Get("areas"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 9 || int(Row.A[0].N) < 0 || int(Row.A[0].N) >= KindCount) continue;
			FeatureArea A;
			A.Kind = Word(Kinds, Row.A[0]);
			A.Height = NumOr(Row.A[1]);
			A.Elevation = NumOr(Row.A[2]);
			A.Name = Word(FileNames, Row.A[3]);
			A.Id = Row.A[4].S;
			A.Ring = Steps(F, Row, 5);
			Scenery.Areas.push_back(std::move(A));
		}
	// What the map says its buildings look like: [lat step, lon step, six words' numbers].
	if (const Json* Rows = File.Get("buildings"))
	{
		long long Lat = 0, Lon = 0;
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 8) continue;
			Lat += static_cast<long long>(Row.A[0].N);
			Lon += static_cast<long long>(Row.A[1].N);
			BuildingLook B;
			B.At = F.FromGeo(double(Lat) * 1e-5, double(Lon) * 1e-5);
			for (size_t K = 0; K < 6; ++K) B.Words[K] = Word(Words, Row.A[K + 2]);
			Scenery.Buildings.push_back(std::move(B));
		}
	}
	// The towers (tall and big buildings, a tall one's parts) as areas of kind "tower", or "skyscraper".
	if (const Json* Rows = File.Get("towers"))
		for (const Json& Row : Rows->A)
		{
			if (Row.Size() < 17) continue;
			FeatureArea A;
			A.Height = NumOr(Row.A[0]);
			A.Elevation = NumOr(Row.A[1]);
			A.Kind = Row.A[0].IsNumber() && Row.A[0].N >= Skyscraper ? "skyscraper" : "tower";
			A.Name = Word(FileNames, Row.A[2]);
			A.Id = Row.A[3].S;
			A.Group = Row.A[4].S;
			for (size_t K = 5; K < 11; ++K) A.Look.push_back(Word(Words, Row.A[K]));
			A.Ring = Steps(F, Row, 11);
			Scenery.Areas.push_back(std::move(A));
		}
}

bool Region::Passable(int Edge, int Node) const
{
	const RoadEdge& E = Edges[size_t(Edge)];
	return E.Oneway == 0 || (E.Oneway == 1 && E.A == Node) || (E.Oneway == -1 && E.B == Node);
}

bool Region::TurnBanned(int Came, int Node, int Onto) const
{
	return !Bans.empty() && std::binary_search(Bans.begin(), Bans.end(), std::array<int, 3>{Came, Node, Onto});
}

std::vector<P2> Region::LineFrom(int Edge, int Node) const
{
	const RoadEdge& E = Edges[size_t(Edge)];
	if (E.A == Node) return E.Line;
	return std::vector<P2>(E.Line.rbegin(), E.Line.rend());
}

std::string Region::NameOf(int Edge) const
{
	const std::string& N = Names[size_t(Edges[size_t(Edge)].Name)];
	return !N.empty() ? N : std::string(GenericName(ClassOf(Edge)));
}

int Region::NearestNode(double X, double Z, double Within) const
{
	int Best = -1;
	double BestD = Within * Within;
	const int Span = int(std::ceil(Within / GridCell));
	const int Cx = FloorInt(X / GridCell), Cz = FloorInt(Z / GridCell);
	for (int Gz = Cz - Span; Gz <= Cz + Span; ++Gz)
		for (int Gx = Cx - Span; Gx <= Cx + Span; ++Gx)
		{
			auto It = Grid.find({Gx, Gz});
			if (It == Grid.end()) continue;
			for (int N : It->second)
			{
				const double Dx = Nodes[size_t(N)].X - X, Dz = Nodes[size_t(N)].Z - Z;
				const double D = Dx * Dx + Dz * Dz;
				if (D < BestD || (D == BestD && (Best < 0 || N < Best)))
				{
					BestD = D;
					Best = N;
				}
			}
		}
	return Best;
}

int Region::NearestConnected(double X, double Z, double Within, int MinNodes) const
{
	int Best = -1, Fallback = -1;
	double BestD = 1e300, FallbackD = 1e300;
	const int Span = int(std::ceil(Within / GridCell));
	const int Cx = FloorInt(X / GridCell), Cz = FloorInt(Z / GridCell);
	for (int Gz = Cz - Span; Gz <= Cz + Span; ++Gz)
		for (int Gx = Cx - Span; Gx <= Cx + Span; ++Gx)
		{
			auto It = Grid.find({Gx, Gz});
			if (It == Grid.end()) continue;
			for (int N : It->second)
			{
				const double D = Nodes[size_t(N)].DistanceTo(P2(X, Z));
				if (D > Within) continue;
				if (D < FallbackD || (D == FallbackD && N < Fallback))
				{
					FallbackD = D;
					Fallback = N;
				}
				if (ComponentSize[size_t(Component[size_t(N)])] >= MinNodes && (D < BestD || (D == BestD && N < Best)))
				{
					BestD = D;
					Best = N;
				}
			}
		}
	return Best >= 0 ? Best : Fallback;
}
std::vector<int> Region::ShortestPath(int From, int To, const std::vector<double>* ClassCost) const
{
	std::vector<int> Out;
	if (From < 0 || To < 0 || From >= int(Nodes.size()) || To >= int(Nodes.size()) || From == To) return Out;
	// Over (edge arrived by, at its far node), so a banned turn can be refused: 2 states an edge.
	const size_t States = Edges.size() * 2;
	std::vector<double> Dist(States, 1e300);
	std::vector<int> Prev(States, -1);
	using Item = std::pair<double, int>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> Open;
	const auto Cost = [&](int Edge)
	{
		const RoadEdge& E = Edges[size_t(Edge)];
		return ClassCost != nullptr && size_t(E.Class) < ClassCost->size() ? E.Length * (*ClassCost)[size_t(E.Class)] : E.Length;
	};
	auto Arrive = [&](int Edge, int AtNode) { return Edge * 2 + (Edges[size_t(Edge)].B == AtNode ? 1 : 0); };
	for (int Ei : Adjacent[size_t(From)])
	{
		if (!Passable(Ei, From) || Edges[size_t(Ei)].A == Edges[size_t(Ei)].B) continue;
		const int S = Arrive(Ei, OtherEnd(Ei, From));
		if (Cost(Ei) < Dist[size_t(S)])
		{
			Dist[size_t(S)] = Cost(Ei);
			Open.push({Dist[size_t(S)], S});
		}
	}
	int End = -1;
	while (!Open.empty())
	{
		const Item Top = Open.top();
		Open.pop();
		const int S = Top.second;
		if (Top.first > Dist[size_t(S)]) continue;
		const int Came = S / 2;
		const int Node = (S & 1) ? Edges[size_t(Came)].B : Edges[size_t(Came)].A;
		if (Node == To)
		{
			End = S;
			break;
		}
		for (int Ei : Adjacent[size_t(Node)])
		{
			if (Ei == Came || !Passable(Ei, Node) || TurnBanned(Came, Node, Ei) || Edges[size_t(Ei)].A == Edges[size_t(Ei)].B) continue;
			const int N = Arrive(Ei, OtherEnd(Ei, Node));
			const double D = Top.first + Cost(Ei);
			if (D < Dist[size_t(N)])
			{
				Dist[size_t(N)] = D;
				Prev[size_t(N)] = S;
				Open.push({D, N});
			}
		}
	}
	for (int S = End; S >= 0; S = Prev[size_t(S)]) Out.push_back(S / 2);
	std::reverse(Out.begin(), Out.end());
	return Out;
}

std::vector<P2> Region::PathLine(int From, const std::vector<int>& Path) const
{
	std::vector<P2> Out;
	int Node = From;
	for (int Ei : Path)
	{
		const std::vector<P2> Line = LineFrom(Ei, Node);
		Out.insert(Out.end(), Line.begin() + (Out.empty() ? 0 : 1), Line.end());
		Node = OtherEnd(Ei, Node);
	}
	return Out;
}
} // namespace TorqueWorld
