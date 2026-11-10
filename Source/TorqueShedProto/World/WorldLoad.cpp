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
} // namespace TorqueWorld
