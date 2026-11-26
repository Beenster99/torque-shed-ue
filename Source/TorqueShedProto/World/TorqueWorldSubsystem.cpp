#include "World/TorqueWorldSubsystem.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include "World/CellStore.h"
#include "World/HeightTiles.h"
#include "World/WorldCache.h"
#include "World/WorldFerries.h"
#include "World/WorldFiles.h"
#include "World/WorldRideChurches.h"
#include "World/WorldRideCircuits.h"
#include "Coop/TorqueSettings.h"
#include "World/WorldLoad.h"
#include "Dash/TftPlannerMap.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include <set>
#include "World/WorldRide.h"
#include "World/WorldRideSigns.h"
#include "World/WorldRideForecourtKit.h"
#include "World/TorqueRideWorld.h"

#include <atomic>

struct FTorqueWorldShared
{
	TorqueWorld::CellStore Cells;
	TorqueWorld::HeightStore Heights;
	TorqueWorld::Route::MapRoads MapRoads;   // [build 147] the planner's road map's main roads
	TorqueWorld::Route::MapStreets MapStreets;   // [map detail] and zoomed in, every road of the cells in view
	TorqueTft::PlannerMap SatNavMap;   // [planner map] the sat nav's map of the cells in view, for the planner's "Sat nav" layer
	TorqueWorld::Route::MapImagery Imagery;  // [build 147] the planner's satellite pictures
	std::atomic<bool> bStop{false};   // the game is closing: every wait gives up
	std::atomic<bool> bMapMoved{false};   // [map detail] the planner's map asked for again while one draws
	// [170] A ride being planned: the planner's map reads no more cells meanwhile (it gave way: drawn again after).
	std::atomic<bool> bRidePlanning{false}, bMapGaveWay{false};
	std::atomic<bool> bOffline{false};   // [stage 8] Settings' offline: nothing is fetched
};

namespace TWSI
{
// One request's answer, filled on the game thread and read by the worker that asked.
struct FAnswer
{
	FCriticalSection Lock;
	bool bSent = false;        // [build 158] the game thread has taken it up
	bool bAbandoned = false;   // [build 158] the worker stopped waiting for it to be sent: don't
	bool bDone = false;
	int32 Code = 0;
	TArray<uint8> Bytes;
};

static std::string ToStd(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

// [tests off Buttertubs] -TorqueWorldOffline: nothing fetched whatever Settings say, so a test ride
// is ridden from what is kept on disk alone (and fails at once, saying what is missing, if it isn't).
static bool ForcedOffline()
{
	static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("TorqueWorldOffline"));
	return bForced;
}

// GETs a URL (redirects followed): the HTTP code, 0 for no answer, and the body. Blocks, so never
// on the game thread, which is the one that has to send it.
// [build 158] How long a worker waits for the game thread to take a fetch up before giving it up for now.
constexpr double CellStoreNotSentSeconds = 10.0;

static int BlockingGet(const std::shared_ptr<FTorqueWorldShared>& Shared, const std::string& Url, std::string& Body)
{
	Body.clear();
	if (IsInGameThread())
	{
		UE_LOG(LogTemp, Error, TEXT("Torque world: a fetch was asked for on the game thread and refused (%s)"), UTF8_TO_TCHAR(Url.c_str()));
		return 0;
	}
	if (Shared->bStop) return 0;
	TSharedRef<FAnswer, ESPMode::ThreadSafe> Answer = MakeShared<FAnswer, ESPMode::ThreadSafe>();
	const FString Address(UTF8_TO_TCHAR(Url.c_str()));
	const FString Agent(UTF8_TO_TCHAR(TorqueWorld::CellStore::UserAgent));
	AsyncTask(ENamedThreads::GameThread, [Answer, Address, Agent]()
	{
		{
			FScopeLock Hold(&Answer->Lock);
			if (Answer->bAbandoned) return;
			Answer->bSent = true;
		}
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Address);
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("User-Agent"), Agent);
		Req->SetTimeout(120.0f);
		Req->OnProcessRequestComplete().BindLambda([Answer](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			FScopeLock Hold(&Answer->Lock);
			if (bConnected && Response.IsValid())
			{
				Answer->Code = Response->GetResponseCode();
				Answer->Bytes = Response->GetContent();
			}
			Answer->bDone = true;
		});
		if (!Req->ProcessRequest())
		{
			FScopeLock Hold(&Answer->Lock);
			Answer->bDone = true;
		}
	});
	// (a little longer than the request's own limit, in case the game thread was slow to send it)
	const double AskedAt = FPlatformTime::Seconds();
	const double GiveUpAt = AskedAt + 150.0;
	for (;;)
	{
		{
			FScopeLock Hold(&Answer->Lock);
			if (Answer->bDone)
			{
				if (Answer->Bytes.Num() > 0) Body.assign(reinterpret_cast<const char*>(Answer->Bytes.GetData()), size_t(Answer->Bytes.Num()));
				return int(Answer->Code);
			}
			// [build 158] (beenster's 156 freezes: the game thread was itself waiting on this worker, so the
			// request was never sent. Whatever holds the game thread up, a fetch it hasn't sent within
			// NotSentSeconds is given up as "not now" (-1): what wanted it makes do, as a cell's buildings
			// do with their boxes, and asks again later.)
			if (!Answer->bSent && FPlatformTime::Seconds() - AskedAt > CellStoreNotSentSeconds)
			{
				Answer->bAbandoned = true;
				UE_LOG(LogTemp, Warning, TEXT("Torque world: the game thread didn't send a fetch within %.0f s; given up for now (%s)"),
					CellStoreNotSentSeconds, *Address);
				return -1;
			}
		}
		if (Shared->bStop || FPlatformTime::Seconds() > GiveUpAt) return 0;
		FPlatformProcess::Sleep(0.005f);
	}
}
} // namespace TWSI

void UTorqueWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Shared = std::make_shared<FTorqueWorldShared>();
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World")));
	Shared->Cells.Dir = TWSI::ToStd(FPaths::Combine(Root, TEXT("cells")));
	Shared->Heights.Disk = TWSI::ToStd(FPaths::Combine(Root, TEXT("heights")));
	// [build 147] The planner's satellite pictures, kept beside the heights and decoded by the engine.
	Shared->Imagery.Disk = TWSI::ToStd(FPaths::Combine(Root, TEXT("imagery")));
	Shared->Imagery.Decode = [](const std::string& Bytes, int& OutW, int& OutH, std::vector<uint8_t>& Rgb)
	{
		IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
		if (!Jpeg.IsValid() || !Jpeg->SetCompressed(Bytes.data(), int64(Bytes.size()))) return false;
		TArray64<uint8> Raw;
		if (!Jpeg->GetRaw(ERGBFormat::RGBA, 8, Raw)) return false;
		OutW = int(Jpeg->GetWidth());
		OutH = int(Jpeg->GetHeight());
		if (Raw.Num() != int64(OutW) * int64(OutH) * 4) return false;
		Rgb.resize(size_t(OutW) * size_t(OutH) * 3);
		for (int64 P = 0; P < int64(OutW) * int64(OutH); ++P)
		{
			Rgb[size_t(P) * 3] = Raw[P * 4];
			Rgb[size_t(P) * 3 + 1] = Raw[P * 4 + 1];
			Rgb[size_t(P) * 3 + 2] = Raw[P * 4 + 2];
		}
		return true;
	};
	// (a weak hold: a fetch asked for after the game has closed finds nothing to ask)
	std::weak_ptr<FTorqueWorldShared> Weak = Shared;
	Shared->Cells.Get = [Weak](const std::string& Url, std::string& Body)
	{
		const std::shared_ptr<FTorqueWorldShared> Held = Weak.lock();
		return Held && !Held->bOffline ? TWSI::BlockingGet(Held, Url, Body) : 0;
	};
	// [build 155] (never fetched on the game thread: asked there, a cell's buildings wait for a worker)
	Shared->Cells.MayFetch = []() { return !IsInGameThread(); };
	// [stage 8] Offline or not, before anything is asked for.
	Shared->bOffline = FTorqueSettings::Get().bWorldOffline || TWSI::ForcedOffline();
	Shared->Cells.Offline = Shared->bOffline;
	// [towns stage 1] The famous towns' own looks, before any ride is planned.
	{
		std::string Text;
		const std::string TownsPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/town_looks.json")));
		const bool bTowns = TorqueWorld::Files::Read(TownsPath, Text) && TorqueWorld::SetTownTable(Text);
		UE_LOG(LogTemp, Display, TEXT("Torque world: town looks: %d towns from Data/RideUK/town_looks.json%s"), int32(TorqueWorld::TownTable().size()), bTowns ? TEXT("") : TEXT(" (none: unreadable)"));
		// [circuits] The race circuits' notes: start lines, sectors, which way round, the road courses' gates.
		{
			std::string CircuitText;
			const std::string CircuitPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/circuits.json")));
			const bool bCircuits = TorqueWorld::Files::Read(CircuitPath, CircuitText) && TorqueWorld::SetCircuitNotes(CircuitText);
			const std::shared_ptr<const TorqueWorld::CircuitNotes> Notes = TorqueWorld::CircuitNotesNow();
			UE_LOG(LogTemp, Display, TEXT("Torque world: circuit notes: %d%s"), Notes ? int32(Notes->Circuits.size()) : 0, bCircuits ? TEXT("") : TEXT(" (none: Data/RideUK/circuits.json unreadable)"));
		}
		// [ferries stage 5] The ferries the route planner may take (Data/RideUK/ferries.json).
		{
			std::string FerryText;
			const std::string FerryPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/ferries.json")));
			const bool bFerries = TorqueWorld::Files::Read(FerryPath, FerryText) && TorqueWorld::Ferries::SetList(FerryText);
			const std::shared_ptr<const std::vector<TorqueWorld::Ferries::Listed>> FerryList = TorqueWorld::Ferries::ListNow();
			UE_LOG(LogTemp, Display, TEXT("Torque world: ferries: %d%s"), FerryList ? int32(FerryList->size()) : 0, bFerries ? TEXT("") : TEXT(" (none: Data/RideUK/ferries.json unreadable)"));
		}
		// [towns stage 5] The materials atlas: the local building stones and bricks by area.
		std::string AtlasText;
		const std::string AtlasPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/materials_atlas.json")));
		const bool bAtlas = TorqueWorld::Files::Read(AtlasPath, AtlasText) && TorqueWorld::SetMaterialsAtlas(AtlasText);
		UE_LOG(LogTemp, Display, TEXT("Torque world: materials atlas: %d areas%s"), int32(TorqueWorld::MaterialsAtlas().size()), bAtlas ? TEXT("") : TEXT(" (none: Data/RideUK/materials_atlas.json unreadable)"));
		// [towns stage 4] The house types and their periods (the built-in figures without the file).
		std::string TypesText;
		const std::string TypesPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/house_types.json")));
		const bool bTypes = TorqueWorld::Files::Read(TypesPath, TypesText) && TorqueWorld::SetHouseTypes(TypesText);
		UE_LOG(LogTemp, Display, TEXT("Torque world: house types %s"), bTypes ? TEXT("from Data/RideUK/house_types.json") : TEXT("built in (Data/RideUK/house_types.json unreadable)"));
		// [churches stage 3] The church kit: its forty churches and the rules that choose one for a place of worship.
		std::string KitText, KitBin;
		const std::string KitPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/church_kit.json")));
		const std::string KitBinPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/church_kit.bin")));
		const bool bKit = TorqueWorld::Files::Read(KitPath, KitText) && TorqueWorld::Files::Read(KitBinPath, KitBin) && TorqueWorld::SetChurchKit(KitText, KitBin);
		const std::shared_ptr<const TorqueWorld::ChurchKit> Kit = TorqueWorld::ChurchKitNow();
		UE_LOG(LogTemp, Display, TEXT("Torque world: church kit: %d churches%s"), Kit ? int32(Kit->Models.size()) : 0, bKit ? TEXT("") : TEXT(" (none: Data/RideUK/church_kit.json or .bin unreadable; the drawn churches stay)"));
		// [churches stage 6] Places of worship's highest points from the LiDAR: their towers and spires to height.
		std::string HeightsText;
		const std::string HeightsPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/church_heights.json")));
		const size_t ChurchSites = TorqueWorld::Files::Read(HeightsPath, HeightsText) ? TorqueWorld::SetChurchHeights(HeightsText) : 0;
		UE_LOG(LogTemp, Display, TEXT("Torque world: church heights: %d places of worship surveyed"), int32(ChurchSites));
		// [LiDAR stage 2] Cells' surveyed building heights, where the game's data has them (-TorqueRideLidar=0: none).
		int32 LidarAsked = 1;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueRideLidar="), LidarAsked);
		if (LidarAsked != 0)
			TorqueWorld::SetLidarDirs({TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/lidar")))});
		// [forecourts] The forecourt kit: petrol stations' canopies, pumps, kiosks and price poles in place of the
		// plain drawn ones (-TorqueForecourts=0: the plain ones, to compare).
		{
			int32 ForecourtsAsked = 1;
			FParse::Value(FCommandLine::Get(), TEXT("TorqueForecourts="), ForecourtsAsked);
			std::string FcText, FcBin;
			const std::string FcPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/forecourt_kit.json")));
			const std::string FcBinPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/forecourt_kit.bin")));
			const bool bFc = ForecourtsAsked != 0 && TorqueWorld::Files::Read(FcPath, FcText) && TorqueWorld::Files::Read(FcBinPath, FcBin) && TorqueWorld::SetForecourtKit(FcText, FcBin);
			const std::shared_ptr<const TorqueWorld::ForecourtKit> Fc = TorqueWorld::ForecourtKitNow();
			UE_LOG(LogTemp, Display, TEXT("Torque world: forecourt kit: %d pieces%s"), Fc ? int32(Fc->Pieces.size()) : 0,
				bFc ? TEXT("") : ForecourtsAsked == 0 ? TEXT(" (off: -TorqueForecourts=0)") : TEXT(" (none: Data/RideUK/forecourt_kit.json or .bin unreadable; the drawn forecourts stay)"));
		}
		// [OSM stage 6A] The signs' alphabet: the words on road signs are set from it as triangles.
		std::string GlyphText;
		const std::string GlyphsPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/sign_glyphs.json")));
		const bool bGlyphs = TorqueWorld::Files::Read(GlyphsPath, GlyphText) && TorqueWorld::SetSignFont(GlyphText);
		const std::shared_ptr<const TorqueWorld::SignFont> Font = TorqueWorld::SignFontNow();
		UE_LOG(LogTemp, Display, TEXT("Torque world: sign letters: %d from Data/RideUK/sign_glyphs.json%s"), Font ? int32(Font->Glyphs.size()) : 0, bGlyphs ? TEXT("") : TEXT(" (none: signs fall back to text components)"));
		// [OSM stage 6C] The places direction signs name (the Route planner's gazetteer, read again for the plan).
		std::string SignPlaceText;
		const std::string SignPlacesPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/World/uk_places.tsv")));
		const bool bSignPlaces = TorqueWorld::Files::Read(SignPlacesPath, SignPlaceText) && TorqueWorld::SetSignPlaces(SignPlaceText);
		UE_LOG(LogTemp, Display, TEXT("Torque world: direction signs' places%s"), bSignPlaces ? TEXT(" read") : TEXT(": none (no direction signs)"));
		// [places stage 5] The famous places' own notes (how many stones, a spire or not...).
		std::string SiteText;
		const std::string SitesPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/landmarks_uk.json")));
		const bool bSites = TorqueWorld::Files::Read(SitesPath, SiteText) && TorqueWorld::SetSiteTable(SiteText);
		UE_LOG(LogTemp, Display, TEXT("Torque world: site notes: %d from Data/RideUK/landmarks_uk.json%s"), int32(TorqueWorld::SiteTable().size()), bSites ? TEXT("") : TEXT(" (none: unreadable)"));
		// [places stage 6] And the famous places' own models the notes name (Data/RideUK/Sites/<model>.glb).
		std::vector<std::shared_ptr<const TorqueWorld::SiteModel>> Models;
		size_t Triangles = 0;
		for (const TorqueWorld::SiteNote& SiteRow : TorqueWorld::SiteTable())
		{
			if (SiteRow.Model.empty() || TorqueWorld::SiteModelOf(SiteRow.Model)) continue;
			bool bHave = false;
			for (const std::shared_ptr<const TorqueWorld::SiteModel>& Had : Models) bHave = bHave || Had->Id == SiteRow.Model;
			if (bHave) continue;
			std::string Bytes, Why;
			const std::string ModelPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/Sites") / (FString(UTF8_TO_TCHAR(SiteRow.Model.c_str())) + TEXT(".glb"))));
			std::shared_ptr<TorqueWorld::SiteModel> Model = std::make_shared<TorqueWorld::SiteModel>();
			Model->Id = SiteRow.Model;
			if (!TorqueWorld::Files::Read(ModelPath, Bytes) || !TorqueWorld::ReadSiteModel(Bytes, *Model, &Why))
			{
				UE_LOG(LogTemp, Warning, TEXT("Torque world: site model %s not read (%s): %s keeps its recipe"), UTF8_TO_TCHAR(SiteRow.Model.c_str()), Why.empty() ? TEXT("missing") : UTF8_TO_TCHAR(Why.c_str()), UTF8_TO_TCHAR(SiteRow.Name.c_str()));
				continue;
			}
			Triangles += Model->Triangles();
			Models.push_back(Model);
		}
		const int32 ModelsRead = int32(Models.size());
		TorqueWorld::SetSiteModels(std::move(Models));
		UE_LOG(LogTemp, Display, TEXT("Torque world: site models: %d from Data/RideUK/Sites (%d triangles)"), ModelsRead, int32(Triangles));
	}
	FString Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueWorldTest="), Spec, false)) StartTest(Spec);
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueRideUK="), Spec, false)) StartRide(Spec);
	else if (FParse::Value(FCommandLine::Get(), TEXT("TorqueLejog="), Spec, false)) StartLejog(Spec);
	else if (FParse::Param(FCommandLine::Get(), TEXT("TorqueLejog"))) StartLejog(TEXT("1"));
	// (and what is kept over the limit let go, on a worker, unless a ride or the test is already under way)
	WorldSettingsChanged();
}

void UTorqueWorldSubsystem::Deinitialize()
{
	if (LejogTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(LejogTicker);
	if (Shared) Shared->bStop = true;
	if (TestRun.IsValid()) TestRun.Wait();
	if (CacheRun.IsValid()) CacheRun.Wait();
	if (RideRun.IsValid()) RideRun.Wait();
	if (MapRun.IsValid()) MapRun.Wait();
	Plan.reset();
	Preview.reset();
	Shared.reset();
	Super::Deinitialize();
}

TorqueWorld::CellStore& UTorqueWorldSubsystem::Cells()
{
	return Shared->Cells;
}

TorqueWorld::HeightStore& UTorqueWorldSubsystem::Heights()
{
	return Shared->Heights;
}

void UTorqueWorldSubsystem::StartTest(const FString& Spec)
{
	// Hawes market place to Leyburn market place, unless four numbers say otherwise.
	double Ends[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	TArray<FString> Given;
	Spec.ParseIntoArray(Given, TEXT(","), true);
	if (Given.Num() == 4)
		for (int32 I = 0; I < 4; ++I) Ends[I] = FCString::Atod(*Given[I].TrimStartAndEnd());
	const bool bQuit = FParse::Param(FCommandLine::Get(), TEXT("TorqueWorldTestQuit"));
	const std::string ReportPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("world_test.json"))));
	UE_LOG(LogTemp, Display, TEXT("Torque world test: from %.4f, %.4f to %.4f, %.4f"), Ends[0], Ends[1], Ends[2], Ends[3]);
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	const double LatA = Ends[0], LonA = Ends[1], LatB = Ends[2], LonB = Ends[3];
	TestRun = Async(EAsyncExecution::Thread, [Held, LatA, LonA, LatB, LonB, ReportPath, bQuit]()
	{
		const double StartedAt = FPlatformTime::Seconds();
		const TorqueWorld::RideCheck R = TorqueWorld::CheckRide(Held->Cells, Held->Heights, LatA, LonA, LatB, LonB, [](const std::string& Step)
		{
			UE_LOG(LogTemp, Display, TEXT("Torque world test: %s"), UTF8_TO_TCHAR(Step.c_str()));
		});
		const double Took = FPlatformTime::Seconds() - StartedAt;
		if (Held->bStop) return;   // (the game closed under it: no verdict)
		TorqueWorld::Files::Write(ReportPath, R.Dump());
		UE_LOG(LogTemp, Display, TEXT("Torque world test: %d cells, %d junctions, %d roads; %.1f km by road (%.1f km straight); %d of %d points have ground, %.0f m to %.0f m, %.0f m of climbing, steepest 1 in %.1f; %d tiles, %d failed; %.1f s"),
			R.Cells, R.Junctions, R.Roads, R.Km, R.CrowKm, R.WithGround, R.Points, R.Low, R.High, R.Climb, R.Steepest > 0.0 ? 1.0 / R.Steepest : 0.0, R.Tiles, R.TilesFailed, Took);
		if (R.Ok) { UE_LOG(LogTemp, Display, TEXT("Torque world test: PASS")); }
		else { UE_LOG(LogTemp, Error, TEXT("Torque world test: FAIL: %s"), UTF8_TO_TCHAR(R.Why.c_str())); }
		if (bQuit) AsyncTask(ENamedThreads::GameThread, []() { FPlatformMisc::RequestExit(false, TEXT("TorqueWorldTest")); });
	});
}

// --- The ride (stage 2) -----------------------------------------------------------------------------

const TCHAR* UTorqueWorldSubsystem::RideMap = TEXT("/Game/Maps/RideUK");

FString UTorqueWorldSubsystem::RideRouteFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("ride_route.json")));
}

FString UTorqueWorldSubsystem::RideNote() const
{
	return Note;
}

TArray<FString> UTorqueWorldSubsystem::Credits() const
{
	TArray<FString> Out;
	if (Plan)
		for (const std::string& Credit : Plan->Area.Roads.Credits) Out.Add(FString(UTF8_TO_TCHAR(Credit.c_str())));
	if (Out.Num() > 0)
	{
		Out.Add(TEXT("Place search: GeoNames (geonames.org) (CC BY 4.0)."));
		Out.Append(ATorqueRideWorld::PropCredits);
		return Out;
	}
	// (as the UK's cells give them)
	Out.Add(TEXT("Elevation: Mapterhorn (mapterhorn.com), built from national and global terrain models; sources and licences are listed at https://download.mapterhorn.com/attribution.json."));
	Out.Add(TEXT("Roads, buildings, land cover and places: (c) OpenStreetMap contributors and others, Overture Maps Foundation (ODbL)."));
	Out.Add(TEXT("Place: Natural Earth; K\u00f6ppen-Geiger zones, Beck, H.E. et al. (2023) (CC BY 4.0); land cover and ground colour, \u00a9 ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium (CC BY 4.0); climate, CRU TS 4.09, University of East Anglia and Met Office (Open Government Licence)."));
	Out.Add(TEXT("Tree heights: Meta and World Resources Institute, High Resolution Canopy Height Maps (CC BY 4.0)."));
	Out.Add(TEXT("Place search: GeoNames (geonames.org) (CC BY 4.0)."));
	Out.Append(ATorqueRideWorld::PropCredits);   // [OSM stage 2] (the models by the road)
	return Out;
}

bool UTorqueWorldSubsystem::StartRide(const FString& Spec)
{
	// Hawes market place to Leyburn market place, unless four numbers say otherwise.
	double Ends[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	FString Title = TEXT("Hawes to Leyburn");
	TArray<FString> Given;
	Spec.ParseIntoArray(Given, TEXT(","), true);
	// [tests off Buttertubs] "buttertubs": Hawes to Thwaite over the real Buttertubs Pass (10.9 km,
	// 364 m of climbing, 1 in 4 at its steepest), the smoke ride and the physics preset's climb.
	if (Spec.TrimStartAndEnd().Equals(TEXT("buttertubs"), ESearchCase::IgnoreCase))
	{
		Ends[2] = 54.3792;
		Ends[3] = -2.1652;
		Title = TEXT("Hawes to Thwaite over the Buttertubs");
	}
	else if (Given.Num() == 4)
	{
		for (int32 I = 0; I < 4; ++I) Ends[I] = FCString::Atod(*Given[I].TrimStartAndEnd());
		Title = TEXT("Ride UK");
	}
	TorqueWorld::RouteRequest Want;
	Want.Pins = {{Ends[0], Ends[1], ""}, {Ends[2], Ends[3], ""}};
	// (a journey's code works as a spec too: -TorqueRideUK=TS1....)
	TorqueWorld::RouteRequest Coded;
	if (TorqueWorld::Route::FromCode(TWSI::ToStd(Spec), Coded))
	{
		Want = Coded;
		Title = TEXT("Ride UK");
	}
	return StartJourney(Want, Title, true);
}

bool UTorqueWorldSubsystem::StartJourney(const TorqueWorld::RouteRequest& Want, const FString& Title, bool bRide)
{
	if (bPlanning || !Shared || Want.Pins.size() < 2) return false;
	bPlanning = true;
	Shared->bRidePlanning = true;
	Note = TEXT("Ride UK: fetching the road...");
	UE_LOG(LogTemp, Display, TEXT("Torque ride UK: %s, %d pins, %s, from %.4f, %.4f to %.4f, %.4f (%s)"), *Title, int32(Want.Pins.size()), UTF8_TO_TCHAR(Want.Mode.c_str()),
		Want.Pins.front().Lat, Want.Pins.front().Lon, Want.Pins.back().Lat, Want.Pins.back().Lon, bRide ? TEXT("to ride") : TEXT("to look at"));
	{
		// [smooth junctions stage 3] -TorqueJunctions=whole: junctions the catalogue has a design for built whole, as one
		// piece (World/WorldRideWhole.cpp); else (the default until the plan's stage 7) as before.
		FString JunctionsAsked;
		FParse::Value(FCommandLine::Get(), TEXT("TorqueJunctions="), JunctionsAsked);
		TorqueWorld::RidePlan::WholeJunctions = JunctionsAsked.Equals(TEXT("whole"), ESearchCase::IgnoreCase);
		UE_LOG(LogTemp, Display, TEXT("Torque ride UK: junctions %s"), TorqueWorld::RidePlan::WholeJunctions ? TEXT("built whole") : TEXT("as before"));
	}
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	const std::string TitleStd = TWSI::ToStd(Title);
	const std::string RoutePath = TWSI::ToStd(RideRouteFile());
	TWeakObjectPtr<UTorqueWorldSubsystem> Self(this);
	if (RideRun.IsValid()) RideRun.Wait();   // (the last one's worker has finished by now: bPlanning was false)
	RideRun = Async(EAsyncExecution::Thread, [Held, Self, Want, TitleStd, RoutePath, bRide]()
	{
		const double StartedAt = FPlatformTime::Seconds();
		std::shared_ptr<TorqueWorld::RidePlan> Made = TorqueWorld::RidePlan::Make(Held->Cells, Held->Heights, Want, [Self](const std::string& Step)
		{
			UE_LOG(LogTemp, Display, TEXT("Torque ride UK: %s"), UTF8_TO_TCHAR(Step.c_str()));
			const FString Said = FString(TEXT("Ride UK: ")) + UTF8_TO_TCHAR(Step.c_str()) + TEXT("...");
			AsyncTask(ENamedThreads::GameThread, [Self, Said]() { if (Self.IsValid()) Self->Note = Said; });
		});
		if (Made) Made->StoresAlive = Held;   // [ride anywhere] (the plan fetches ground with them as the riders roam)
		if (Held->bStop) return;   // (the game closed under it)
		Made->Title = TitleStd;
		if (Made->Ok && bRide) TorqueWorld::Files::Write(RoutePath, Made->RouteJson());
		UE_LOG(LogTemp, Display, TEXT("Torque ride UK: planned in %.1f s"), FPlatformTime::Seconds() - StartedAt);
		// [junction priority 169] (where the ride's own road gives way, as SimTest's world_ride says it: for a
		// robot check to find the junction it looks at; build 168's Buxworth check found no give-way)
		if (Made->Ok)
		{
			int32 Gives = 0;
			for (const TorqueWorld::RidePlan::JunctionRule& J : Made->Junctions)
			{
				if (!J.CameMinor && !J.OntoMinor) continue;
				++Gives;
				const TorqueWorld::Geo At = TorqueWorld::Frame(Made->Area.Roads.FrameLat, Made->Area.Roads.FrameLon).ToGeo(Made->Area.Roads.Nodes[size_t(J.Node)].X, Made->Area.Roads.Nodes[size_t(J.Node)].Z);
				UE_LOG(LogTemp, Display, TEXT("Torque ride UK: junction at %.0f m (%.5f,%.5f): %s, %s -> %s"), J.Along, At.Lat, At.Lon,
					J.CameMinor ? TEXT("the ride gives way coming in") : TEXT("the ride leaves by a road that gives way"),
					UTF8_TO_TCHAR(Made->Area.Roads.ClassOf(J.Came).c_str()), UTF8_TO_TCHAR(Made->Area.Roads.ClassOf(J.Onto).c_str()));
			}
			int32 Lines = 0;
			for (const TorqueWorld::RidePlan::MappedMark& M : Made->MainMapped)
				if ((M.Kind == "give_way" || M.Kind == "stop") && M.Dir != 0)
				{
					++Lines;
					UE_LOG(LogTemp, Display, TEXT("Torque ride UK: %s line at %.0f m%s"), UTF8_TO_TCHAR(M.Kind.c_str()), M.Along, M.LinesDrawn ? TEXT(" (along the through road's edge)") : TEXT(""));
				}
			UE_LOG(LogTemp, Display, TEXT("Torque ride UK: junctions: %d with a through road, the ride gives way at %d; %d give-way or stop lines facing it"), int32(Made->Junctions.size()), Gives, Lines);
		}
		AsyncTask(ENamedThreads::GameThread, [Self, Made, bRide]() { if (Self.IsValid()) Self->RidePlanned(Made, bRide); });
	});
	return true;
}

void UTorqueWorldSubsystem::RidePlanned(std::shared_ptr<TorqueWorld::RidePlan> Made, bool bRide)
{
	bPlanning = false;
	if (Shared)
	{
		// [170] (the planner's map, which stopped reading cells while the ride was planned, finished now)
		Shared->bRidePlanning = false;
		if (Shared->bMapGaveWay.exchange(false)) AskMap(MapBusyView());
	}
	if (LejogOn >= 0 && bLegAsked && bRide)
	{
		// [stage 8] (a leg of the robot's ride: how long it took to plan; one that can't be planned is passed)
		bLegAsked = false;
		LegPlanSecs = FPlatformTime::Seconds() - LegAskedAt;
		if (!Made || !Made->Ok)
		{
			++LejogFailed;
			EndLeg(FString(TEXT("FAILED to plan: ")) + (Made ? UTF8_TO_TCHAR(Made->Why.c_str()) : TEXT("no plan")));
			return;
		}
		LegPlan = Made;
	}
	if (!Made || !Made->Ok)
	{
		Note = FString(TEXT("Ride UK: ")) + (Made ? UTF8_TO_TCHAR(Made->Why.c_str()) : TEXT("no ride was planned."));
		UE_LOG(LogTemp, Error, TEXT("Torque ride UK: FAIL: %s"), Made ? UTF8_TO_TCHAR(Made->Why.c_str()) : TEXT("no plan"));
		// [tests off Buttertubs] A smoke ride that can't be planned (offline, its cells not kept) ends now.
		if (FParse::Param(FCommandLine::Get(), TEXT("TorqueSmoke")))
		{
			UE_LOG(LogTemp, Error, TEXT("Torque smoke: FAIL: the ride could not be planned (%s)"), Made ? UTF8_TO_TCHAR(Made->Why.c_str()) : TEXT("no plan"));
			FPlatformMisc::RequestExit(false, TEXT("TorqueSmokeNoRide"));
		}
		return;
	}
	if (!bRide)
	{
		Preview = Made;
		PreviewSum = Made->Summary();
		++PreviewSerial;
		Note.Reset();
		UE_LOG(LogTemp, Display, TEXT("Torque ride UK: preview %.1f km to the finish, %.0f m of climbing, highest %.0f m, %.0f deg/km, about %.0f min"),
			PreviewSum.Metres / 1000.0, PreviewSum.Climb, PreviewSum.Highest, PreviewSum.Twist, PreviewSum.Minutes);
		return;
	}
	TravelTo(Made);
}

bool UTorqueWorldSubsystem::RideAgain()
{
	// [tests off Buttertubs] (the physics preset rides out 14 times: the road planned once)
	if (bPlanning || !Plan || !Plan->Ok) return false;
	TorqueWorld::Files::Write(TWSI::ToStd(RideRouteFile()), Plan->RouteJson());
	return TravelTo(Plan);
}

bool UTorqueWorldSubsystem::RidePreview()
{
	if (bPlanning || !Preview || !Preview->Ok) return false;
	TorqueWorld::Files::Write(TWSI::ToStd(RideRouteFile()), Preview->RouteJson());
	return TravelTo(Preview);
}

bool UTorqueWorldSubsystem::AdoptPreview()
{
	if (!Preview || !Preview->Ok) return false;
	TorqueWorld::Files::Write(TWSI::ToStd(RideRouteFile()), Preview->RouteJson());
	Plan = Preview;
	UE_LOG(LogTemp, Display, TEXT("Torque ride UK: the host's journey planned here too, %.1f km; ready to set off"), Plan->Length / 1000.0);
	return true;
}

bool UTorqueWorldSubsystem::TravelTo(std::shared_ptr<TorqueWorld::RidePlan> Made)
{
	if (!FPackageName::DoesPackageExist(RideMap))
	{
		Note = TEXT("Ride UK: this build has no level for it.");
		UE_LOG(LogTemp, Error, TEXT("Torque ride UK: FAIL: no level %s (run Tools/ue_build_rideuk.py in the editor)"), RideMap);
		return false;
	}
	Plan = Made;
	Note.Reset();
	UE_LOG(LogTemp, Display, TEXT("Torque ride UK: %.1f km of road in %d points, %d roads; route at %s"), Plan->Length / 1000.0, int32(Plan->Road.size()), int32(Plan->Names.size()), *RideRouteFile());
	UE_LOG(LogTemp, Display, TEXT("Torque ride UK: PASS"));
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !GEngine) return false;
	// (a co-op host takes everyone, still connected: each of them planned the same road already)
	if (World->GetNetMode() == NM_ListenServer)
	{
		World->ServerTravel(FString(RideMap) + TEXT("?listen?play"), false);
		return true;
	}
	GEngine->SetClientTravel(World, *(FString(RideMap) + TEXT("?play")), TRAVEL_Absolute);
	return true;
}

// --- The Route planner (stage 4) ------------------------------------------------------------------

const TorqueWorld::Route::Gazetteer& UTorqueWorldSubsystem::Places()
{
	if (!bPlacesRead)
	{
		bPlacesRead = true;
		std::string Text;
		const std::string Path = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/World/uk_places.tsv")));
		if (!TorqueWorld::Files::Read(Path, Text) || !PlaceList.Load(Text))
		{
			UE_LOG(LogTemp, Error, TEXT("Torque route: no places to search (%s)"), UTF8_TO_TCHAR(Path.c_str()));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Torque route: %d places to search"), int32(PlaceList.Count()));
		}
	}
	return PlaceList;
}

const std::vector<TorqueWorld::Route::FamousRoad>& UTorqueWorldSubsystem::FamousRoads()
{
	if (!bFamousRead)
	{
		bFamousRead = true;
		std::string Text;
		const std::string Path = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/famous_roads.json")));
		if (!TorqueWorld::Files::Read(Path, Text) || !TorqueWorld::Route::ReadFamousRoads(Text, Famous))
			UE_LOG(LogTemp, Error, TEXT("Torque route: no famous roads (%s)"), UTF8_TO_TCHAR(Path.c_str()));
	}
	return Famous;
}

void UTorqueWorldSubsystem::AskMap(const TorqueWorld::Route::MapView& View)
{
	if (!Shared) return;
	if (bMapBusy)
	{
		MapNext = View;
		bMapWaiting = true;
		Shared->bMapMoved = true;
		return;
	}
	DrawMap(View);
}

void UTorqueWorldSubsystem::DrawMap(const TorqueWorld::Route::MapView& View)
{
	bMapBusy = true;
	MapAsked = View;
	bMapWaiting = false;
	if (Shared) Shared->bMapMoved = false;
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	TWeakObjectPtr<UTorqueWorldSubsystem> Self(this);
	if (MapRun.IsValid()) MapRun.Wait();
	MapRun = Async(EAsyncExecution::Thread, [Held, Self, View]()
	{
		const auto Draw = [&Held, &View]()
		{
			std::vector<uint8_t> Px;
			bool bDrawn = false;
			if (View.Layer == TorqueWorld::Route::SatNav)
			{
				// [planner map] The sat nav's map on the relief's sea, once every cell in view is read; till then
				// (and zoomed out past it) the road map.
				TorqueWorld::Route::MapView Plain = View;
				Plain.Layer = TorqueWorld::Route::RoadMap;
				TorqueWorld::Route::DrawRelief(Held->Heights, Plain, Px);
				bDrawn = Held->SatNavMap.Draw(View, Px);
				if (bDrawn) TorqueWorld::Route::DrawFerryLines(View, Px);
			}
			if (!bDrawn) TorqueWorld::Route::DrawRelief(Held->Heights, View, Px, &Held->MapRoads, &Held->Imagery, &Held->MapStreets);
			FTorqueMapPicture Out;
			Out.Pixels.Append(Px.data(), int32(Px.size()));
			// [map detail] (and the roads' names to write over it, found here rather than on the game thread)
			if (TorqueWorld::Route::RoadLayer(View.Layer)) Out.Names = Held->MapStreets.Labels(View);
			return Out;
		};
		// [map detail] Zoomed in, the roads of the cells in view: those on disk at once, the rest fetched
		// (the middle's first) while the first picture shows, given up on once the view has moved on.
		const bool bRoads = TorqueWorld::Route::RoadLayer(View.Layer);
		// ([170] and while a ride is being planned, the map reads no more cells: the plan reads the same
		// stores and wants the machine to itself; the map is drawn again once it is planned)
		const auto Moved = [&Held]()
		{
			if (Held->bRidePlanning.load()) Held->bMapGaveWay = true;
			return Held->bStop.load() || Held->bMapMoved.load() || Held->bRidePlanning.load();
		};
		const auto Streets = [&Held, &View, &Moved](int Load, int* Loaded)
		{
			int Missing = TorqueWorld::KnowStreets(Held->Cells, Held->MapStreets, View, Load, Moved, Loaded);
			if (View.Layer == TorqueWorld::Route::SatNav)
			{
				// [planner map] (and the sat nav's map of the same cells: they are on disk by now)
				int MoreLoaded = 0;
				Missing += Held->SatNavMap.Know(Held->Cells, View, Load, Moved, &MoreLoaded);
				if (Loaded != nullptr) *Loaded += MoreLoaded;
			}
			return Missing;
		};
		// (a few cells at a time, the picture drawn again after each few, so the roads come in round the middle first)
		const auto FetchStreets = [&]()
		{
			int Was = TNumericLimits<int32>::Max();
			for (int Round = 0; Round < 16 && !Moved(); ++Round)
			{
				int Loaded = 0;
				const int Missing = Streets(3, &Loaded);
				if (Loaded == 0 || Missing >= Was || Moved()) return;   // (none read this round: offline, or cells that won't read)
				Was = Missing;
				FTorqueMapPicture More = Draw();
				AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(More)]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), false); });
			}
		};
		// [build 147] The satellite layer: its pictures on disk at once, then again with those that weren't.
		if (TorqueWorld::Route::PhotoLayer(View.Layer))
		{
			const std::vector<TorqueWorld::TileKey> Pics = TorqueWorld::Route::MapImagery::Keys(View);
			bool bHave = true;
			for (const TorqueWorld::TileKey& K : Pics) bHave = bHave && Held->Imagery.OnDisk(K);
			if (bRoads)
			{
				// (the main roads too, as the road map has them)
				std::set<TorqueWorld::CellId> Under;
				for (const TorqueWorld::TileKey& K : View.Tiles())
					for (const TorqueWorld::CellId& C : TorqueWorld::CellsUnder(K)) Under.insert(C);
				TorqueWorld::KnowCoasts(Held->Cells, Held->Heights, std::vector<TorqueWorld::CellId>(Under.begin(), Under.end()), 24, &Held->MapRoads);
				bHave = Streets(0, nullptr) == 0 && bHave;   // (none read yet: the picture first)
			}
			FTorqueMapPicture First = Draw();
			AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(First), bHave]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), bHave); });
			if (bHave || Held->bStop) return;
			Held->Imagery.Fetch(Pics, Held->Cells.Get, 6);
			if (bRoads && !Held->bStop) FetchStreets();
			if (Held->bStop) return;
			FTorqueMapPicture Second = Draw();
			AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(Second)]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), true); });
			return;
		}
		// What is on disk at once, then again with the tiles that weren't.
		const std::vector<TorqueWorld::TileKey> Keys = View.Tiles();
		// [build 147] Where the coast is, so low land (the Fens) isn't drawn as sea.
		{
			std::set<TorqueWorld::CellId> Under;
			for (const TorqueWorld::TileKey& K : Keys)
				for (const TorqueWorld::CellId& C : TorqueWorld::CellsUnder(K)) Under.insert(C);
			TorqueWorld::KnowCoasts(Held->Cells, Held->Heights, std::vector<TorqueWorld::CellId>(Under.begin(), Under.end()), 24,
				bRoads ? &Held->MapRoads : nullptr);
		}
		bool bAll = true;
		for (const TorqueWorld::TileKey& K : Keys) bAll = bAll && Held->Heights.Has(K);
		if (bRoads) bAll = Streets(0, nullptr) == 0 && bAll;
		FTorqueMapPicture First = Draw();
		AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(First), bAll]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), bAll); });
		if (bAll || Held->bStop) return;
		Held->Heights.Fetch(Keys, Held->Cells.Get, 6);
		if (bRoads && !Held->bStop) FetchStreets();
		if (Held->bStop) return;
		FTorqueMapPicture Second = Draw();
		AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(Second)]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), true); });
	});
}

void UTorqueWorldSubsystem::MapDone(const TorqueWorld::Route::MapView& View, FTorqueMapPicture&& Picture, bool bFinal)
{
	// (a picture of a view since moved on from is still shown while the newer one draws)
	MapDrawn = View;
	MapPixels = MoveTemp(Picture.Pixels);
	MapNames = std::move(Picture.Names);
	bMapNew = true;
	if (!bFinal) return;
	bMapBusy = false;
	if (bMapWaiting) DrawMap(MapNext);
}

bool UTorqueWorldSubsystem::TakeMap(TorqueWorld::Route::MapView& View, TArray<uint8>& Bgra)
{
	if (!bMapNew) return false;
	bMapNew = false;
	View = MapDrawn;
	Bgra = MapPixels;
	return true;
}

namespace TWSI
{
static FString JourneyFile()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("journey.txt")));
}
} // namespace TWSI

bool UTorqueWorldSubsystem::LastJourney(TorqueWorld::RouteRequest& Out, FString& Title) const
{
	// (two lines: the journey's code, and what it is called)
	std::string Text;
	if (!TorqueWorld::Files::Read(TWSI::ToStd(TWSI::JourneyFile()), Text)) return false;
	const size_t Break = Text.find('\n');
	if (!TorqueWorld::Route::FromCode(Text.substr(0, Break), Out)) return false;
	Title = Break == std::string::npos ? FString() : FString(UTF8_TO_TCHAR(Text.substr(Break + 1).c_str())).TrimStartAndEnd();
	return true;
}

void UTorqueWorldSubsystem::KeepJourney(const TorqueWorld::RouteRequest& Want, const FString& Title) const
{
	TorqueWorld::Files::Write(TWSI::ToStd(TWSI::JourneyFile()), TorqueWorld::Route::CodeOf(Want) + "\n" + TWSI::ToStd(Title) + "\n");
}

// --- What is kept on disk (stage 8) ----------------------------------------------------------------

bool UTorqueWorldSubsystem::IsOffline() const
{
	return Shared && Shared->bOffline;
}

void UTorqueWorldSubsystem::WorldSettingsChanged()
{
	if (!Shared) return;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	const bool bWasOffline = Shared->bOffline;
	Shared->bOffline = Set.bWorldOffline || TWSI::ForcedOffline();
	Shared->Cells.Offline = Shared->bOffline;
	// (the store's own count is read on workers as they fetch: an int, written here between rides)
	const long long Limit = (long long)FMath::Clamp(Set.WorldCacheGb, 1, 200) * 1000000000LL;
	Shared->Cells.Keep = TorqueWorld::Cache::CellsToKeep(Limit);
	if (bWasOffline != Shared->bOffline) UE_LOG(LogTemp, Display, TEXT("Torque world: %s"), Shared->bOffline ? TEXT("offline: only what is on disk") : TEXT("online"));
	if (!bPlanning && !TestRun.IsValid()) TidyCache(false);
}

void UTorqueWorldSubsystem::WeighCache()
{
	if (!bPlanning) TidyCache(false);
}

bool UTorqueWorldSubsystem::ClearCache(FString& Why)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const bool bRiding = World && World->GetMapName().Contains(TEXT("RideUK"));
	if (bPlanning || bMapBusy || bCacheBusy || bRiding)
	{
		Why = bRiding ? TEXT("Not while riding the real world: clear it from the shed") : TEXT("Busy with the world just now: try again in a moment");
		return false;
	}
	TidyCache(true);
	Why = TEXT("Clearing what of the world is kept on this machine");
	return true;
}

void UTorqueWorldSubsystem::TidyCache(bool bClear)
{
	if (!Shared || bCacheBusy) return;
	bCacheBusy = true;
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	TWeakObjectPtr<UTorqueWorldSubsystem> Self(this);
	const long long Limit = (long long)FMath::Clamp(FTorqueSettings::Get().WorldCacheGb, 1, 200) * 1000000000LL;
	if (CacheRun.IsValid()) CacheRun.Wait();
	CacheRun = Async(EAsyncExecution::Thread, [Held, Self, Limit, bClear]()
	{
		if (bClear)
		{
			TorqueWorld::Cache::Clear(Held->Cells.Dir, Held->Heights.Disk);
			Held->Heights.Reset();
			Held->Cells.ForgetIndex();
			UE_LOG(LogTemp, Display, TEXT("Torque world: cleared what was kept"));
		}
		else
		{
			const int CellsGone = Held->Cells.Prune();
			const long long Freed = TorqueWorld::Cache::PruneHeights(Held->Heights.Disk, Limit);
			// (tiles already read stay in memory: a plan being made alongside keeps its ground)
			if (CellsGone > 0 || Freed > 0)
			{
				UE_LOG(LogTemp, Display, TEXT("Torque world: over the limit of %s, %d cells and %s of ground let go"), UTF8_TO_TCHAR(TorqueWorld::Cache::Words(Limit).c_str()), CellsGone,
					UTF8_TO_TCHAR(TorqueWorld::Cache::Words(Freed).c_str()));
			}
		}
		const TorqueWorld::Cache::Use U = TorqueWorld::Cache::Measure(Held->Cells.Dir, Held->Heights.Disk);
		const FString Said = FString::Printf(TEXT("%s: %d places' roads, %d squares of ground"), UTF8_TO_TCHAR(TorqueWorld::Cache::Words(U.Total()).c_str()), U.Cells, U.Tiles);
		UE_LOG(LogTemp, Display, TEXT("Torque world: kept on disk %s"), *Said);
		AsyncTask(ENamedThreads::GameThread, [Self, Said]()
		{
			if (!Self.IsValid()) return;
			Self->CacheSaid = Said;
			Self->bCacheBusy = false;
		});
	});
}

// --- Land's End to John o' Groats (stage 8) --------------------------------------------------------

void UTorqueWorldSubsystem::LejogSay(const FString& Line) const
{
	UE_LOG(LogTemp, Display, TEXT("Torque lejog: %s"), *Line);
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("lejog.txt")));
	FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S  ")) + Line + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

void UTorqueWorldSubsystem::StartLejog(const FString& Spec)
{
	std::string Text;
	const std::string Path = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/lejog.json")));
	if (!TorqueWorld::Files::Read(Path, Text) || !TorqueWorld::Route::ReadFamousRoads(Text, LejogLegs) || LejogLegs.empty())
	{
		UE_LOG(LogTemp, Error, TEXT("Torque lejog: FAIL: no legs (%s)"), UTF8_TO_TCHAR(Path.c_str()));
		return;
	}
	const FString NextFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("lejog_next.txt"));
	int32 From = 1;
	if (Spec.TrimStartAndEnd().Equals(TEXT("next"), ESearchCase::IgnoreCase))
	{
		FString Kept;
		if (FFileHelper::LoadFileToString(Kept, *NextFile)) From = FMath::Max(1, FCString::Atoi(*Kept.TrimStartAndEnd()));
	}
	else
		From = FMath::Max(1, FCString::Atoi(*Spec));
	int32 Count = int32(LejogLegs.size());
	FParse::Value(FCommandLine::Get(), TEXT("TorqueLejogLegs="), Count);
	LejogOn = FMath::Min(From, int32(LejogLegs.size())) - 1;
	LejogLast = FMath::Min(int32(LejogLegs.size()) - 1, LejogOn + FMath::Max(1, Count) - 1);
	LejogMemStart = FPlatformMemory::GetStats().UsedPhysical;
	LejogSay(FString::Printf(TEXT("START legs %d to %d of %d, memory %.2f GB"), LejogOn + 1, LejogLast + 1, int32(LejogLegs.size()), double(LejogMemStart) / 1e9));
	LejogTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTorqueWorldSubsystem::LejogTick), 0.0f);
	NextLeg();
}

void UTorqueWorldSubsystem::NextLeg()
{
	bLegRiding = false;
	LegPlan.reset();
	if (LejogOn < 0) return;
	FFileHelper::SaveStringToFile(FString::FromInt(LejogOn + 1), *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("World"), TEXT("lejog_next.txt")));
	if (LejogOn > LejogLast)
	{
		const uint64 Mem = FPlatformMemory::GetStats().UsedPhysical;
		LejogSay(FString::Printf(TEXT("DONE %.0f km ridden; %d legs failed to plan, %d stuck; memory %.2f GB at the start, %.2f GB now; kept on disk %s"), LejogKm, LejogFailed, LejogStuck,
			double(LejogMemStart) / 1e9, double(Mem) / 1e9, *CacheSaid));
		LejogOn = -1;
		if (FParse::Param(FCommandLine::Get(), TEXT("TorqueLejogQuit"))) FPlatformMisc::RequestExit(false, TEXT("TorqueLejog"));
		return;
	}
	if (bPlanning) return;   // (asked again from the ticker once the planning in hand is done)
	const TorqueWorld::Route::FamousRoad& Leg = LejogLegs[size_t(LejogOn)];
	bLegAsked = true;
	LegAskedAt = FPlatformTime::Seconds();
	LejogSay(FString::Printf(TEXT("leg %d/%d %s: planning"), LejogOn + 1, int32(LejogLegs.size()), UTF8_TO_TCHAR(Leg.Name.c_str())));
	if (!StartJourney(Leg.Journey, UTF8_TO_TCHAR(Leg.Name.c_str()), true)) bLegAsked = false;
}

void UTorqueWorldSubsystem::EndLeg(const FString& How)
{
	const uint64 Mem = FPlatformMemory::GetStats().UsedPhysical;
	const TorqueWorld::Route::FamousRoad& Leg = LejogLegs[size_t(LejogOn)];
	const double Mins = (FPlatformTime::Seconds() - LegStartedAt) / 60.0;
	LejogSay(FString::Printf(TEXT("leg %d/%d %s: %s; %.1f km in %.0f min, planned in %.0f s; %d hitches over 100 ms, the worst %.0f ms; memory %.2f GB (%+.2f since the start)"),
		LejogOn + 1, int32(LejogLegs.size()), UTF8_TO_TCHAR(Leg.Name.c_str()), *How, LegBest / 1000.0, bLegRiding ? Mins : 0.0, LegPlanSecs, LegHitches, LegWorstMs,
		double(Mem) / 1e9, (double(Mem) - double(LejogMemStart)) / 1e9));
	LejogKm += LegBest / 1000.0;
	++LejogOn;
	bLegRiding = false;
	LegPlan.reset();
	WeighCache();   // (what is kept on disk, over the limit let go between legs, and logged)
	NextLeg();
}

bool UTorqueWorldSubsystem::LejogTick(float DeltaSeconds)
{
	if (LejogOn < 0) return true;
	const double Now = FPlatformTime::Seconds();
	if (!bLegAsked && !bLegRiding && !LegPlan && !bPlanning)
	{
		NextLeg();
		return true;
	}
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const bool bOnRide = World && World->GetMapName().Contains(TEXT("RideUK")) && Pawn && LegPlan && Plan == LegPlan;
	if (!bLegRiding)
	{
		if (!bOnRide) return true;
		bLegRiding = true;
		LegStartedAt = LegBestAt = LegLookAt = LegMemAt = Now;
		LegBest = 0.0;
		LegWas = -1.0;
		LegHitches = 0;
		LegWorstMs = 0.0;
		LejogSay(FString::Printf(TEXT("leg %d/%d: riding %.1f km"), LejogOn + 1, int32(LejogLegs.size()), LegPlan->FinishM / 1000.0));
		return true;
	}
	if (!bOnRide) return true;   // (the level going or coming: nothing to measure)
	// (the frames: a hitch is one over 100 ms, after the first few seconds' settling)
	if (Now - LegStartedAt > 8.0 && DeltaSeconds > 0.1f)
	{
		++LegHitches;
		LegWorstMs = FMath::Max(LegWorstMs, double(DeltaSeconds) * 1000.0);
	}
	if (Now - LegLookAt < 1.0) return true;
	LegLookAt = Now;
	const FVector At = Pawn->GetActorLocation();
	const TorqueWorld::RidePlan::RideNav N = LegPlan->NavAt(At.X / 100.0, At.Y / 100.0, LegWas);
	LegWas = N.Along;
	if (N.Along > LegBest + 20.0)
	{
		LegBest = N.Along;
		LegBestAt = Now;
	}
	if (Now - LegMemAt > 60.0)
	{
		LegMemAt = Now;
		LejogSay(FString::Printf(TEXT("leg %d at %.1f km, memory %.2f GB, %d hitches so far"), LejogOn + 1, N.Along / 1000.0, double(FPlatformMemory::GetStats().UsedPhysical) / 1e9, LegHitches));
	}
	if (N.ToFinish < 30.0)
	{
		LegBest = FMath::Max(LegBest, N.Along);
		EndLeg(TEXT("done"));
	}
	else if (Now - LegBestAt > 120.0)
	{
		// (no way along in two minutes: crashed, lost or stuck; where, then on to the next leg)
		const TorqueWorld::Geo G = TorqueWorld::Frame(LegPlan->Area.Roads.FrameLat, LegPlan->Area.Roads.FrameLon).ToGeo(At.X / 100.0, At.Y / 100.0);
		++LejogStuck;
		EndLeg(FString::Printf(TEXT("STUCK at %.2f km (%.5f, %.5f)%s"), N.Along / 1000.0, G.Lat, G.Lon, N.OffRoute ? TEXT(", off the road") : TEXT("")));
	}
	return true;
}
