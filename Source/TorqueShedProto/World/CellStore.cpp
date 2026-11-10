#include "CellStore.h"

#include "WorldFiles.h"
#include "WorldRegion.h"
#include "WorldSha256.h"
#include "WorldZip.h"

#include <algorithm>
#include <ctime>

namespace TorqueWorld
{
using TorqueFit::Json;

namespace
{
void Hashes(const Json& Root, const char* Key, std::unordered_map<std::string, std::string>& Out)
{
	const Json* V = Root.Get(Key);
	if (V == nullptr || !V->IsObject()) return;
	Out.reserve(V->O.size());
	for (const auto& Kv : V->O)
		if (Kv.second.IsString()) Out[Kv.first] = Kv.second.S;
}

const char* FolderOf(CellStore::EKind Kind)
{
	switch (Kind)
	{
	case CellStore::EKind::Streams: return "stream";
	case CellStore::EKind::Trees: return "tree";
	case CellStore::EKind::Features: return "feature";
	case CellStore::EKind::Profiles: return "profile";
	default: return "sketch";
	}
}

void Walk(const std::string& Root, const std::string& Rel, std::vector<std::string>& Out)
{
	Out.push_back(Rel);
	for (const std::string& Sub : Files::List(Rel.empty() ? Root : Files::Join(Root, Rel), true)) Walk(Root, Rel.empty() ? Sub : Rel + "/" + Sub, Out);
}
} // namespace

bool WorldIndex::Parse(const std::string& Text)
{
	Json Root;
	if (!Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const Json* CellsJ = Root.Get("cells");
	if (CellsJ == nullptr || !CellsJ->IsObject()) return false;
	*this = WorldIndex();
	Format = int(Root.Num("format"));
	Release = Root.Str("release");
	Cells.reserve(CellsJ->O.size());
	for (const auto& Kv : CellsJ->O)
	{
		if (!Kv.second.IsObject()) continue;
		CellEntry E;
		E.Empty = Kv.second.Flag("empty");
		E.Hash = Kv.second.Str("hash");
		E.Url = Kv.second.Str("url");
		E.Bytes = static_cast<long long>(Kv.second.Num("bytes"));
		Cells[Kv.first] = E;
	}
	Hashes(Root, "streams", Streams);
	Hashes(Root, "trees", Trees);
	Hashes(Root, "features", Features);
	Hashes(Root, "profiles", Profiles);
	Hashes(Root, "sketches", Sketches);
	if (const Json* A = Root.Get("areas")) Areas = *A;
	return true;
}

const CellEntry* WorldIndex::Cell(CellId C) const
{
	auto It = Cells.find(C.Name());
	return It == Cells.end() ? nullptr : &It->second;
}

std::vector<std::string> CellStore::CellRoots() const
{
	std::vector<std::string> Out;
	if (!Dir.empty()) Out.push_back(Dir);
	Out.insert(Out.end(), MoreRoots.begin(), MoreRoots.end());
	return Out;
}

std::string CellStore::BaseUrl() const
{
	const size_t Slash = IndexUrl.rfind('/');
	return Slash == std::string::npos ? std::string() : IndexUrl.substr(0, Slash + 1);
}

std::string CellStore::TextHash(const std::string& Text)
{
	return Sha1Hex(!Text.empty() && Text.back() == '\n' ? Text.substr(0, Text.size() - 1) : Text).substr(0, 12);
}

std::shared_ptr<const WorldIndex> CellStore::SavedIndex() const
{
	std::shared_ptr<WorldIndex> Idx = std::make_shared<WorldIndex>();
	std::string Text;
	if (!Dir.empty() && Files::Read(Files::Join(Dir, "index.json"), Text)) Idx->Parse(Text);
	return Idx;
}

std::shared_ptr<const WorldIndex> CellStore::Index(bool Refresh)
{
	{
		std::lock_guard<std::mutex> G(Lock);
		if (Have != nullptr && !Have->Empty() && !Refresh && std::chrono::steady_clock::now() - HaveAt < std::chrono::minutes(15)) return Have;
	}
	std::shared_ptr<const WorldIndex> Got;
	if (!Offline && Get)
	{
		std::string Body;
		std::shared_ptr<WorldIndex> Idx = std::make_shared<WorldIndex>();
		if (Get(IndexUrl, Body) == 200 && Idx->Parse(Body))
		{
			Got = Idx;
			if (!Dir.empty())
			{
				Files::Write(Files::Join(Dir, "index.json"), Body);
				// (the named areas, put where WorldRegion reads them: areas.json beside the cells)
				if (Idx->Areas.IsObject() && Idx->Areas.Size() > 0) Files::Write(Files::Join(Dir, "areas.json"), Idx->Areas.Dump(0));
			}
		}
	}
	if (Got == nullptr) Got = SavedIndex();
	std::lock_guard<std::mutex> G(Lock);
	Have = Got;
	HaveAt = std::chrono::steady_clock::now();
	return Got;
}

std::shared_ptr<const WorldIndex> CellStore::IndexNow()
{
	{
		std::lock_guard<std::mutex> G(Lock);
		if (Have != nullptr && !Have->Empty()) return Have;
	}
	return SavedIndex();
}

void CellStore::ForgetIndex()
{
	std::lock_guard<std::mutex> G(Lock);
	Have.reset();
}

CellStore::EState CellStore::StateOf(CellId C, const WorldIndex& Idx) const
{
	if (!Region::CellDir(CellRoots(), C).empty()) return EState::Here;
	const CellEntry* E = Idx.Cell(C);
	if (E == nullptr) return EState::Unknown;
	return E->Empty ? EState::Sea : EState::Baked;
}

std::vector<CellId> CellStore::Finishing(const std::vector<CellId>& Cells, const WorldIndex& Idx) const
{
	std::vector<CellId> Out;
	if (Idx.Profiles.empty()) return Out;   // (an old index, or the tests': none has a profile)
	for (const CellId& C : Cells)
	{
		const CellEntry* E = Idx.Cell(C);
		if (E != nullptr && !E->Empty && Idx.Profiles.count(C.Name()) == 0) Out.push_back(C);
	}
	return Out;
}

std::string CellStore::FetchCells(const std::vector<CellId>& Cells, const std::function<void(int, int)>& Progress)
{
	std::lock_guard<std::mutex> One(Fetching);
	const std::shared_ptr<const WorldIndex> Idx = Index();
	std::vector<CellId> Want;
	for (const CellId& C : Cells)
		if (StateOf(C, *Idx) == EState::Baked) Want.push_back(C);
	if (Want.empty()) return std::string();
	if (Offline || !Get) return "This game doesn't have that part of the world, and it's offline.";
	int Done = 0;
	for (const CellId& C : Want)
	{
		const std::string Why = FetchOne(C, *Idx->Cell(C));
		if (!Why.empty()) return Why;
		++Done;
		if (Progress) Progress(Done, int(Want.size()));
	}
	Prune();
	return std::string();
}

std::string CellStore::FetchOne(CellId C, const CellEntry& E)
{
	std::string Zip;
	const int Code = Get(E.Url, Zip);
	if (Code != 200) return "Couldn't fetch that part of the world (HTTP " + std::to_string(Code) + "): are you online?";
	const std::string Why = InstallZip(Zip, "c" + C.Name(), E.Hash, Dir);
	if (Why.empty()) Touch({C});
	return Why;
}

std::string CellStore::RefetchStale(const std::vector<CellId>& Cells, const WorldIndex& Idx)
{
	std::vector<CellId> Stale;
	for (const CellId& C : Cells)
	{
		const CellEntry* E = Idx.Cell(C);
		const std::string D = Files::Join(Dir, "c" + C.Name());
		if (E == nullptr || E->Hash.empty() || !Files::Exists(Files::Join(D, "region.json"))) continue;
		if (HashOfDir(D) != E->Hash)
		{
			Files::RemoveTree(D);
			Stale.push_back(C);
		}
	}
	return Stale.empty() ? std::string() : FetchCells(Stale);
}

bool CellStore::Kept(EKind Kind, const std::string& Name, const WorldIndex& Idx, Json& Out)
{
	const std::unordered_map<std::string, std::string>& Map = Kind == EKind::Streams ? Idx.Streams
		: (Kind == EKind::Trees ? Idx.Trees : (Kind == EKind::Features ? Idx.Features : (Kind == EKind::Profiles ? Idx.Profiles : Idx.Sketches)));
	auto It = Map.find(Name);
	if (It == Map.end() || It->second.empty()) return false;
	const std::string Rel = std::string(FolderOf(Kind)) + "/" + (Kind == EKind::Sketches ? "b" : "c") + Name + ".json";
	const std::string Path = Files::Join(Dir, Rel);
	std::string Text;
	if (Dir.empty() || !Files::Read(Path, Text) || TextHash(Text) != It->second)
	{
		if (Offline || !Get) return false;
		Text.clear();
		if (Get(BaseUrl() + Rel, Text) != 200) return false;
		if (TextHash(Text) != It->second) return false;   // (not the one the index names: it may have changed since; next time)
		if (!Dir.empty()) Files::Write(Path, Text);
	}
	return Json::Parse(Text, Out);
}

Json CellStore::Used() const
{
	Json V;
	std::string Text;
	if (Files::Read(Files::Join(Dir, "used.json"), Text) && Json::Parse(Text, V) && V.IsObject()) return V;
	return Json::MakeObject();
}

void CellStore::Touch(const std::vector<CellId>& Cells)
{
	std::lock_guard<std::mutex> G(Lock);
	if (Dir.empty()) return;
	Json U = Used();
	const double Now = double(std::time(nullptr));
	for (const CellId& C : Cells)
	{
		U.Set(C.Name(), Json::MakeNumber(Now));
		UsedNow.insert(C.Name());
	}
	if (Files::IsDir(Dir)) Files::Write(Files::Join(Dir, "used.json"), U.Dump(0));
}

int CellStore::Prune()
{
	std::lock_guard<std::mutex> G(Lock);
	if (Dir.empty()) return 0;
	Json U = Used();
	std::vector<std::pair<long long, std::string>> KeptCells;   // (when, name)
	for (const std::string& Sub : Files::List(Dir, true))
	{
		const std::string At = Files::Join(Files::Join(Dir, Sub), "region.json");
		if (Sub.empty() || Sub[0] != 'c' || !Files::Exists(At)) continue;
		const std::string Name = Sub.substr(1);
		const Json* When = U.Get(Name);
		KeptCells.emplace_back(When != nullptr ? static_cast<long long>(When->N) : Files::ModifiedUnix(At), Name);
	}
	int Gone = 0;
	if (int(KeptCells.size()) <= Keep) return 0;
	std::sort(KeptCells.begin(), KeptCells.end());
	Json Left = Json::MakeObject();
	std::set<std::string> Went;
	for (const auto& K : KeptCells)
	{
		if (int(KeptCells.size()) - Gone <= Keep) break;
		if (UsedNow.count(K.second) != 0) continue;
		Files::RemoveTree(Files::Join(Dir, "c" + K.second));
		for (const char* Folder : {"stream", "tree", "feature"}) Files::Remove(Files::Join(Dir, std::string(Folder) + "/c" + K.second + ".json"));
		Went.insert(K.second);
		++Gone;
	}
	for (const auto& Kv : U.O)
		if (Went.count(Kv.first) == 0) Left.Set(Kv.first, Kv.second);
	Files::Write(Files::Join(Dir, "used.json"), Left.Dump(0));
	return Gone;
}

std::string CellStore::HashOfDir(const std::string& Path)
{
	std::vector<std::string> Dirs;
	Walk(Path, std::string(), Dirs);
	std::sort(Dirs.begin(), Dirs.end());
	Sha256 Ctx;
	for (const std::string& Rel : Dirs)
	{
		const std::string D = Rel.empty() ? Path : Files::Join(Path, Rel);
		for (const std::string& Fn : Files::List(D, false))
		{
			if (Fn == "region.json" || Fn == "features.json") continue;
			std::string Bytes;
			if (Files::Read(Files::Join(D, Fn), Bytes)) Ctx.Update(Bytes);
		}
	}
	return Ctx.FinishHex().substr(0, 16);
}

std::string CellStore::InstallZip(const std::string& Zip, const std::string& Id, const std::string& Expect, const std::string& Root)
{
	std::vector<ZipEntry> Entries;
	if (!ZipList(Zip, Entries)) return "The download isn't a region zip.";
	const std::string Tmp = Files::Join(Root, Id + ".part");
	Files::RemoveTree(Tmp);
	bool Any = false;
	for (const ZipEntry& E : Entries)
	{
		// (a folder's own entry, or a name that would land outside the cell's folder: passed over)
		if (E.Name.empty() || E.Name.back() == '/' || E.Name[0] == '/' || E.Name.find("..") != std::string::npos || E.Name.find('\\') != std::string::npos
			|| E.Name.find(':') != std::string::npos)
			continue;
		std::string Bytes;
		if (!ZipRead(Zip, E, Bytes) || !Files::Write(Files::Join(Tmp, E.Name), Bytes))
		{
			Files::RemoveTree(Tmp);
			return "Couldn't write the region to " + Files::Join(Tmp, E.Name) + ".";
		}
		Any = Any || E.Name == "region.json";
	}
	if (!Any)
	{
		Files::RemoveTree(Tmp);
		return "The zip has no region.json in it.";
	}
	const std::string Got = HashOfDir(Tmp);
	if (!Expect.empty() && Got != Expect)
	{
		Files::RemoveTree(Tmp);
		return "The download's data (" + Got + ") isn't the region the index names (" + Expect + "): try again later.";
	}
	const std::string Final = Files::Join(Root, Id);
	Files::RemoveTree(Final);
	if (!Files::Rename(Tmp, Final))
	{
		Files::RemoveTree(Tmp);
		return "Couldn't put the region in place.";
	}
	return std::string();
}
} // namespace TorqueWorld
