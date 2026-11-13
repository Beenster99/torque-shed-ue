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
#include "World/WorldFiles.h"
#include "Coop/TorqueSettings.h"
#include "World/WorldLoad.h"
#include "World/WorldRide.h"
#include "World/TorqueRideWorld.h"

#include <atomic>

struct FTorqueWorldShared
{
	TorqueWorld::CellStore Cells;
	TorqueWorld::HeightStore Heights;
	std::atomic<bool> bStop{false};   // the game is closing: every wait gives up
	std::atomic<bool> bOffline{false};   // [stage 8] Settings' offline: nothing is fetched
};

namespace TWSI
{
// One request's answer, filled on the game thread and read by the worker that asked.
struct FAnswer
{
	FCriticalSection Lock;
	bool bDone = false;
	int32 Code = 0;
	TArray<uint8> Bytes;
};

static std::string ToStd(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

// GETs a URL (redirects followed): the HTTP code, 0 for no answer, and the body. Blocks, so never
// on the game thread, which is the one that has to send it.
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
	const double GiveUpAt = FPlatformTime::Seconds() + 150.0;
	for (;;)
	{
		{
			FScopeLock Hold(&Answer->Lock);
			if (Answer->bDone)
			{
				if (Answer->Bytes.Num() > 0) Body.assign(reinterpret_cast<const char*>(Answer->Bytes.GetData()), size_t(Answer->Bytes.Num()));
				return int(Answer->Code);
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
	// (a weak hold: a fetch asked for after the game has closed finds nothing to ask)
	std::weak_ptr<FTorqueWorldShared> Weak = Shared;
	Shared->Cells.Get = [Weak](const std::string& Url, std::string& Body)
	{
		const std::shared_ptr<FTorqueWorldShared> Held = Weak.lock();
		return Held && !Held->bOffline ? TWSI::BlockingGet(Held, Url, Body) : 0;
	};
	// [stage 8] Offline or not, before anything is asked for.
	Shared->bOffline = FTorqueSettings::Get().bWorldOffline;
	Shared->Cells.Offline = Shared->bOffline;
	// [towns stage 1] The famous towns' own looks, before any ride is planned.
	{
		std::string Text;
		const std::string TownsPath = TWSI::ToStd(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/town_looks.json")));
		const bool bTowns = TorqueWorld::Files::Read(TownsPath, Text) && TorqueWorld::SetTownTable(Text);
		UE_LOG(LogTemp, Display, TEXT("Torque world: town looks: %d towns from Data/RideUK/town_looks.json%s"), int32(TorqueWorld::TownTable().size()), bTowns ? TEXT("") : TEXT(" (none: unreadable)"));
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
	if (Given.Num() == 4)
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
	Note = TEXT("Ride UK: fetching the road...");
	UE_LOG(LogTemp, Display, TEXT("Torque ride UK: %s, %d pins, %s, from %.4f, %.4f to %.4f, %.4f (%s)"), *Title, int32(Want.Pins.size()), UTF8_TO_TCHAR(Want.Mode.c_str()),
		Want.Pins.front().Lat, Want.Pins.front().Lon, Want.Pins.back().Lat, Want.Pins.back().Lon, bRide ? TEXT("to ride") : TEXT("to look at"));
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
		if (Held->bStop) return;   // (the game closed under it)
		Made->Title = TitleStd;
		if (Made->Ok && bRide) TorqueWorld::Files::Write(RoutePath, Made->RouteJson());
		UE_LOG(LogTemp, Display, TEXT("Torque ride UK: planned in %.1f s"), FPlatformTime::Seconds() - StartedAt);
		AsyncTask(ENamedThreads::GameThread, [Self, Made, bRide]() { if (Self.IsValid()) Self->RidePlanned(Made, bRide); });
	});
	return true;
}

void UTorqueWorldSubsystem::RidePlanned(std::shared_ptr<TorqueWorld::RidePlan> Made, bool bRide)
{
	bPlanning = false;
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
			UE_LOG(LogTemp, Error, TEXT("Torque route: no places to search (%s)"), UTF8_TO_TCHAR(Path.c_str()));
		else
			UE_LOG(LogTemp, Display, TEXT("Torque route: %d places to search"), int32(PlaceList.Count()));
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
		return;
	}
	DrawMap(View);
}

void UTorqueWorldSubsystem::DrawMap(const TorqueWorld::Route::MapView& View)
{
	bMapBusy = true;
	bMapWaiting = false;
	const std::shared_ptr<FTorqueWorldShared> Held = Shared;
	TWeakObjectPtr<UTorqueWorldSubsystem> Self(this);
	if (MapRun.IsValid()) MapRun.Wait();
	MapRun = Async(EAsyncExecution::Thread, [Held, Self, View]()
	{
		const auto Draw = [&Held, &View]()
		{
			std::vector<uint8_t> Px;
			TorqueWorld::Route::DrawRelief(Held->Heights, View, Px);
			TArray<uint8> Out;
			Out.Append(Px.data(), int32(Px.size()));
			return Out;
		};
		// What is on disk at once, then again with the tiles that weren't.
		const std::vector<TorqueWorld::TileKey> Keys = View.Tiles();
		bool bAll = true;
		for (const TorqueWorld::TileKey& K : Keys) bAll = bAll && Held->Heights.Has(K);
		TArray<uint8> First = Draw();
		AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(First), bAll]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), bAll); });
		if (bAll || Held->bStop) return;
		Held->Heights.Fetch(Keys, Held->Cells.Get, 6);
		if (Held->bStop) return;
		TArray<uint8> Second = Draw();
		AsyncTask(ENamedThreads::GameThread, [Self, View, Got = MoveTemp(Second)]() mutable { if (Self.IsValid()) Self->MapDone(View, MoveTemp(Got), true); });
	});
}

void UTorqueWorldSubsystem::MapDone(const TorqueWorld::Route::MapView& View, TArray<uint8>&& Pixels, bool bFinal)
{
	// (a picture of a view since moved on from is still shown while the newer one draws)
	MapDrawn = View;
	MapPixels = MoveTemp(Pixels);
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
	Shared->bOffline = Set.bWorldOffline;
	Shared->Cells.Offline = Set.bWorldOffline;
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
