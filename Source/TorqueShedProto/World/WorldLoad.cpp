#include "WorldLoad.h"

#include "WorldSha256.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace TorqueWorld
{
using TorqueFit::Json;

std::string CellsAreaId(const std::vector<CellId>& Cells)
{
	std::string Out = "area:@";
	for (size_t I = 0; I < Cells.size(); ++I) Out += (I > 0 ? "," : "") + Cells[I].Name();
	return Out;
}

bool LoadCells(CellStore& Store, HeightStore& Heights, const std::vector<CellId>& Cells, LoadedArea& Out, std::string* Why)
{
	if (Cells.empty()) return false;
	const Geo Mid = CellMiddle(Cells[0]);
	return LoadArea(Store, Heights, Cells, Mid.Lat, Mid.Lon, "the roads here", Out, Why);
}

bool LoadArea(CellStore& Store, HeightStore& Heights, const std::vector<CellId>& Cells, double Lat0, double Lon0, const std::string& Title, LoadedArea& Out, std::string* Why)
{
	const std::string Fetched = Store.FetchCells(Cells);
	// (a cell that couldn't be fetched is only fatal if none of the area is here: the rest still rides)
	std::vector<std::string> Dirs;
	std::vector<CellId> Here;
	const std::vector<std::string> Roots = Store.CellRoots();
	for (const CellId& C : Cells)
	{
		const std::string D = Region::CellDir(Roots, C);
		if (!D.empty())
		{
			Dirs.push_back(D);
			Here.push_back(C);
		}
	}
	std::string StitchWhy;
	if (Dirs.empty() || !Region::Stitch(CellsAreaId(Cells), Title, Dirs, Lat0, Lon0, Out.Roads, &StitchWhy))
	{
		if (Why != nullptr) *Why = !Fetched.empty() ? Fetched : (!StitchWhy.empty() ? StitchWhy : "None of that part of the world is here yet.");
		return false;
	}
	Store.Touch(Here);   // (in use: the last to be pruned, and never this run)
	Out.Cells = Here;
	Out.Land = std::make_shared<Terrain>(Heights, Lat0, Lon0);
	for (const auto& M : Out.Roads.CellMetas) Out.Land->AddCoverCell(M.first, M.second);
	Out.Land->SetBuildings(Out.Roads.Buildings);

	const std::shared_ptr<const WorldIndex> Idx = Store.IndexNow();
	// Bridges and tunnels come in each 5 degree block's sketch.
	std::map<std::string, Json> Sketches;
	std::vector<std::string> Hashes;
	for (const CellId& C : Here)
	{
		const std::string Name = C.Name();
		const std::string Block = std::to_string(C.Row / 20) + "_" + std::to_string(C.Col / 20);
		if (Sketches.count(Block) == 0)
		{
			Json Sk;
			if (!Store.Kept(CellStore::EKind::Sketches, Block, *Idx, Sk)) Sk = Json::MakeObject();
			Sketches[Block] = std::move(Sk);
		}
		if (const Json* Sp = Sketches[Block].Get("spans"))
			if (const Json* Rows = Sp->Get(Name))
				if (Rows->IsArray()) Out.Roads.AddSpans(*Rows);
		Json File;
		if (Store.Kept(CellStore::EKind::Streams, Name, *Idx, File))
			if (const Json* Rows = File.Get("streams"))
				if (Rows->IsArray()) Out.Roads.AddStreams(*Rows);
		if (Store.Kept(CellStore::EKind::Features, Name, *Idx, File) && File.Get("points") != nullptr)
		{
			Out.Roads.AddFeatures(File);
			auto It = Idx->Features.find(Name);
			Hashes.push_back(Name + ":" + (It != Idx->Features.end() ? It->second : std::string()));
		}
		if (Store.Kept(CellStore::EKind::Trees, Name, *Idx, File)) Out.Land->AddTrees(File);
		// Its place profile says whether it has sea: land below sea level in a cell with none (a polder) is land.
		if (Store.Kept(CellStore::EKind::Profiles, Name, *Idx, File) && !File.Flag("guessed", true))
			Heights.KnowSea(C, File.Num("sea") > 0.0 || File.Flag("coast"));
	}
	std::sort(Hashes.begin(), Hashes.end());
	std::string Joined;
	for (size_t I = 0; I < Hashes.size(); ++I) Joined += (I > 0 ? "|" : "") + Hashes[I];
	Out.FeaturesHash = Hashes.empty() ? std::string() : Sha1Hex(Joined).substr(0, 12);
	if (Out.Land->HasTrees()) Out.Roads.Credits.push_back("Tree heights: Meta and World Resources Institute, High Resolution Canopy Height Maps (CC BY 4.0).");
	return true;
}
int FetchGround(CellStore& Store, HeightStore& Heights, const Terrain& Ride, const std::function<void(int, int)>& Progress)
{
	const std::shared_ptr<const WorldIndex> Idx = Store.Index();
	for (const CellId& C : Ride.CellsUnderRide())
	{
		if (Heights.SeaIn(C) >= 0) continue;
		Json File;
		// (a guessed profile only knows the sea where the game has the cell: the sea stays as it was)
		if (Store.Kept(CellStore::EKind::Profiles, C.Name(), *Idx, File) && !File.Flag("guessed", true)) Heights.KnowSea(C, File.Num("sea") > 0.0 || File.Flag("coast"));
	}
	return Heights.Fetch(Ride.UsedKeys(), Store.Get, 6, Progress);
}

std::vector<P2> Resample(const std::vector<P2>& Line, double Step, std::vector<double>* Along)
{
	std::vector<P2> Out;
	if (Along != nullptr) Along->clear();
	double Run = 0.0;
	for (size_t I = 0; I < Line.size(); ++I)
	{
		if (I > 0)
		{
			const double Len = Line[I].DistanceTo(Line[I - 1]);
			const int Parts = Step > 0.0 ? int(std::ceil(Len / Step)) : 1;
			for (int K = 1; K < Parts; ++K)
			{
				Out.push_back(Line[I - 1] + (Line[I] - Line[I - 1]) * (double(K) / Parts));
				if (Along != nullptr) Along->push_back(Run + Len * K / Parts);
			}
			Run += Len;
		}
		Out.push_back(Line[I]);
		if (Along != nullptr) Along->push_back(Run);
	}
	return Out;
}

RideCheck CheckRide(CellStore& Store, HeightStore& Heights, double LatA, double LonA, double LatB, double LonB, const std::function<void(const std::string&)>& Say)
{
	RideCheck R;
	const auto Tell = [&Say](const std::string& Text) { if (Say) Say(Text); };
	Tell("Reading the world's index");
	const std::shared_ptr<const WorldIndex> Idx = Store.Index();
	if (!Idx || Idx->Empty()) { R.Why = "The world's index can't be had."; return R; }
	// (a little past both ends, so a place at a cell's edge has its roads on both sides)
	const double Pad = 0.03;
	const std::vector<CellId> Want = CellsInBox(std::fmin(LatA, LatB) - Pad, std::fmin(LonA, LonB) - Pad, std::fmax(LatA, LatB) + Pad, std::fmax(LonA, LonB) + Pad);
	std::vector<CellId> Cells;
	for (const CellId& C : Want)
	{
		const CellStore::EState St = Store.StateOf(C, *Idx);
		if (St == CellStore::EState::Here || St == CellStore::EState::Baked) Cells.push_back(C);
	}
	if (Cells.empty()) { R.Why = "Nobody has baked that part of the world yet."; return R; }
	Tell("Fetching " + std::to_string(Cells.size()) + " cells of roads");
	LoadedArea Area;
	if (!LoadCells(Store, Heights, Cells, Area, &R.Why)) return R;
	const Region& G = Area.Roads;
	R.Cells = int(Area.Cells.size());
	R.Junctions = int(G.Nodes.size());
	R.Roads = int(G.Edges.size());
	R.Credits = G.Credits;
	const Frame F = G.FrameOf();
	const P2 A = F.FromGeo(LatA, LonA), B = F.FromGeo(LatB, LonB);
	R.From = G.NearestConnected(A.X, A.Z, 2000.0);
	R.To = G.NearestConnected(B.X, B.Z, 2000.0);
	if (R.From < 0 || R.To < 0) { R.Why = std::string("There is no road within 2 km of the ") + (R.From < 0 ? "start." : "end."); return R; }
	Tell("Finding the way by road");
	const std::vector<int> Path = G.ShortestPath(R.From, R.To);
	if (Path.empty()) { R.Why = "There is no way by road between them."; return R; }
	for (int E : Path)
	{
		R.Km += G.Edges[size_t(E)].Length / 1000.0;
		const std::string Name = G.NameOf(E);
		if (R.Names.empty() || R.Names.back() != Name) R.Names.push_back(Name);
	}
	R.CrowKm = G.Nodes[size_t(R.From)].DistanceTo(G.Nodes[size_t(R.To)]) / 1000.0;
	const std::vector<P2> Road = Resample(G.PathLine(R.From, Path), 30.0);
	const std::shared_ptr<Terrain> Ride = Area.Land->ForRoad(Road);
	R.Tiles = int(Ride->UsedKeys().size());
	Tell("Fetching the ground: " + std::to_string(R.Tiles) + " tiles");
	R.TilesFailed = FetchGround(Store, Heights, *Ride);
	R.Points = int(Road.size());
	double Last = std::nan("");
	for (size_t I = 0; I < Road.size(); ++I)
	{
		const double H = Ride->Height(Road[I].X, Road[I].Z);
		if (std::isnan(H)) { Last = H; continue; }
		R.Low = R.WithGround == 0 ? H : std::fmin(R.Low, H);
		R.High = R.WithGround == 0 ? H : std::fmax(R.High, H);
		++R.WithGround;
		if (I > 0 && !std::isnan(Last))
		{
			const double Run = Road[I].DistanceTo(Road[I - 1]);
			if (H > Last) R.Climb += H - Last;
			// (a point a metre from the last says nothing of the slope)
			if (Run > 10.0) R.Steepest = std::fmax(R.Steepest, std::fabs(H - Last) / Run);
		}
		Last = H;
	}
	if (R.TilesFailed > 0) R.Why = std::to_string(R.TilesFailed) + " of the ground's tiles couldn't be fetched.";
	else if (R.WithGround != R.Points) R.Why = std::to_string(R.Points - R.WithGround) + " points of the road have no ground under them.";
	else R.Ok = true;
	return R;
}

std::string RideCheck::Dump() const
{
	Json J = Json::MakeObject();
	const auto Set = [&J](const char* Key, Json V) { J.O.emplace_back(Key, std::move(V)); };
	const auto List = [](const std::vector<std::string>& Rows) { Json L = Json::MakeArray(); for (const std::string& S : Rows) L.A.push_back(Json::MakeString(S)); return L; };
	Set("ok", Json::MakeBool(Ok));
	Set("why", Json::MakeString(Why));
	Set("cells", Json::MakeNumber(Cells));
	Set("junctions", Json::MakeNumber(Junctions));
	Set("roads", Json::MakeNumber(Roads));
	Set("km", Json::MakeNumber(Km));
	Set("crow_km", Json::MakeNumber(CrowKm));
	Set("points", Json::MakeNumber(Points));
	Set("with_ground", Json::MakeNumber(WithGround));
	Set("low_m", Json::MakeNumber(Low));
	Set("high_m", Json::MakeNumber(High));
	Set("climb_m", Json::MakeNumber(Climb));
	Set("steepest", Json::MakeNumber(Steepest));
	Set("tiles", Json::MakeNumber(Tiles));
	Set("tiles_failed", Json::MakeNumber(TilesFailed));
	Set("names", List(Names));
	Set("credits", List(Credits));
	return J.Dump();
}
} // namespace TorqueWorld
