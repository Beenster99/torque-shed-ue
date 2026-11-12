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
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#include "World/CellStore.h"
#include "World/HeightTiles.h"
#include "World/WorldFiles.h"
#include "World/WorldLoad.h"
#include "World/WorldRide.h"

#include <atomic>

struct FTorqueWorldShared
{
	TorqueWorld::CellStore Cells;
	TorqueWorld::HeightStore Heights;
	std::atomic<bool> bStop{false};   // the game is closing: every wait gives up
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
		return Held ? TWSI::BlockingGet(Held, Url, Body) : 0;
	};
	FString Spec;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueWorldTest="), Spec, false)) StartTest(Spec);
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueRideUK="), Spec, false)) StartRide(Spec);
}

void UTorqueWorldSubsystem::Deinitialize()
{
	if (Shared) Shared->bStop = true;
	if (TestRun.IsValid()) TestRun.Wait();
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
		return Out;
	}
	// (as the UK's cells give them)
	Out.Add(TEXT("Elevation: Mapterhorn (mapterhorn.com), built from national and global terrain models; sources and licences are listed at https://download.mapterhorn.com/attribution.json."));
	Out.Add(TEXT("Roads, buildings, land cover and places: (c) OpenStreetMap contributors and others, Overture Maps Foundation (ODbL)."));
	Out.Add(TEXT("Place: Natural Earth; K\u00f6ppen-Geiger zones, Beck, H.E. et al. (2023) (CC BY 4.0); land cover and ground colour, \u00a9 ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium (CC BY 4.0); climate, CRU TS 4.09, University of East Anglia and Met Office (Open Government Licence)."));
	Out.Add(TEXT("Tree heights: Meta and World Resources Institute, High Resolution Canopy Height Maps (CC BY 4.0)."));
	Out.Add(TEXT("Place search: GeoNames (geonames.org) (CC BY 4.0)."));
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
