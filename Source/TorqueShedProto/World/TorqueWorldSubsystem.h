// The real world inside the game (Ride UK stage 1): the plain C++ readers in this folder given the
// game's own disk and network. It lives on the game instance, so the cells and tiles a crew has
// fetched stay known from the shed to the road and back.
//
// What is fetched is kept under Saved/World (cells/ and heights/), which a new build never wipes.
// The stores block while they fetch, so they are only ever asked from a worker thread: Get hands
// each request to the game thread (where the engine's HTTP lives) and waits for its answer.
//
// -TorqueWorldTest=hawes (or =latA,lonA,latB,lonB) runs the stage's check once the game is up: the
// cells between the two places fetched and stitched, the way by road found, its ground fetched and
// the road's height read all the way along. It logs "Torque world test: PASS" or "FAIL: why" and
// writes Saved/World/world_test.json; with -TorqueWorldTestQuit the game then closes.
//
// Stage 2, the ride itself: StartRide plans a ride between two places on a worker (the cells and
// the ground fetched, the road laid), keeps it, writes its route where the bike reads it
// (Saved/World/ride_route.json) and opens the bare level it is ridden in (/Game/Maps/RideUK, made
// by Tools/ue_build_rideuk.py), where ATorqueRideWorld lays the ground and the road out round the
// rider. -TorqueRideUK=hawes (or =latA,lonA,latB,lonB) starts one as soon as the game is up; with
// -TorqueAutoRide the robot rides it. It logs "Torque ride UK: PASS" once planned, or "FAIL: why".
//
// Stage 8, the whole UK proven: -TorqueLejog (with -TorqueAutoRide -TorqueNoInput) has the robot ride
// Land's End to John o' Groats in the legs of Data/RideUK/lejog.json, one after another, each planned
// and its level opened as a player's would be. =N starts at leg N, =next where the last run stopped
// (Saved/World/lejog_next.txt); -TorqueLejogLegs=K rides K legs and stops (an afternoon's worth);
// -TorqueLejogQuit closes the game at the end. Each leg logs "Torque lejog: leg ..." with its time,
// frame hitches, memory and the disk kept, and any stuck or failed place; all go to Saved/World/lejog.txt.
#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "World/WorldRide.h"
#include "World/WorldRoute.h"
#include "World/WorldRouteMap.h"

#include <memory>
#include <string>
#include <vector>

#include "TorqueWorldSubsystem.generated.h"

namespace TorqueWorld
{
class CellStore;
class HeightStore;
class RidePlan;
}
struct FTorqueWorldShared;

UCLASS()
class UTorqueWorldSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// The world's cells and its ground. Both block while they fetch: ask them from a worker only.
	TorqueWorld::CellStore& Cells();
	TorqueWorld::HeightStore& Heights();

	// The level a real-world ride is ridden in.
	static const TCHAR* RideMap;
	// Plans a ride and opens its level when it is ready. Spec: "hawes" (Hawes to Leyburn) or
	// "latA,lonA,latB,lonB". False if one is being planned already.
	bool StartRide(const FString& Spec);
	bool IsPlanningRide() const { return bPlanning; }
	// What the planning is doing now, or why the last one failed ("" for nothing to say).
	FString RideNote() const;
	// The ride planned last (null before any): the game mode hands it to ATorqueRideWorld.
	std::shared_ptr<TorqueWorld::RidePlan> Ride() const { return Plan; }
	// Who made the world's data, a credit a line as its licence asks: the last ride's own (from its
	// cells), or before any ride those of the UK's cells.
	TArray<FString> Credits() const;
	// Its route, as the bike reads one (an absolute path).
	static FString RideRouteFile();

	// Stage 4, the Route planner's journeys. Plans one on a worker; with bRide its level opens once
	// it is planned, else it is kept as the preview, to be summed up and drawn on the map, and
	// RidePreview rides it without planning it again. False if one is being planned already.
	bool StartJourney(const TorqueWorld::RouteRequest& Want, const FString& Title, bool bRide);
	bool RidePreview();
	// [co-op] A joined rider's game: the preview becomes the ride the host's journey will open
	// (its route written for the bike), without travelling; the host takes everyone there.
	bool AdoptPreview();
	// The journey planned last without riding it (null before any, or once the request it was for
	// has changed: ForgetPreview), what it is like, and a count that goes up with each new one.
	std::shared_ptr<TorqueWorld::RidePlan> PreviewPlan() const { return Preview; }
	const TorqueWorld::RideSummary& PreviewSummary() const { return PreviewSum; }
	int32 PreviewCount() const { return PreviewSerial; }
	void ForgetPreview() { Preview.reset(); PreviewSum = TorqueWorld::RideSummary(); }
	// The UK's places to search and the famous roads, read the first time they are asked for.
	const TorqueWorld::Route::Gazetteer& Places();
	const std::vector<TorqueWorld::Route::FamousRoad>& FamousRoads();
	// The planner's map: draws the relief under a view on a worker, from the tiles on disk at once
	// and again once the missing ones are fetched. A newer view asked for while one is drawing is
	// drawn next (those in between are skipped). TakeMap hands over a picture once, when there is a
	// new one: its view and its pixels (B G R A).
	void AskMap(const TorqueWorld::Route::MapView& View);
	bool TakeMap(TorqueWorld::Route::MapView& View, TArray<uint8>& Bgra);
	bool IsDrawingMap() const { return bMapBusy; }
	// [stage 8] What of the world is kept on disk: Settings > Gameplay's limit and offline switch put
	// on the stores (and the oldest let go past the limit, on a worker, now and at start), how much is
	// kept as last weighed ("" before the first), and clearing it all (refused while a ride is being
	// planned or ridden: Why says so).
	void WorldSettingsChanged();
	FString CacheWords() const { return CacheSaid; }
	void WeighCache();
	bool ClearCache(FString& Why);
	bool IsOffline() const;
	// The journey last planned in the planner, kept between sessions (Saved/World/journey.json).
	bool LastJourney(TorqueWorld::RouteRequest& Out, FString& Title) const;
	void KeepJourney(const TorqueWorld::RouteRequest& Want, const FString& Title) const;

private:
	void StartTest(const FString& Spec);

	// [stage 8] The Land's End to John o' Groats robot ride (see above).
	std::vector<TorqueWorld::Route::FamousRoad> LejogLegs;
	int32 LejogOn = -1, LejogLast = -1, LejogFailed = 0, LejogStuck = 0;
	bool bLegRiding = false, bLegAsked = false;
	double LegAskedAt = 0.0, LegStartedAt = 0.0, LegPlanSecs = 0.0, LegBest = 0.0, LegBestAt = 0.0, LegWas = -1.0, LegLookAt = 0.0, LegMemAt = 0.0;
	int32 LegHitches = 0;
	double LegWorstMs = 0.0, LejogKm = 0.0;
	uint64 LejogMemStart = 0;
	std::shared_ptr<TorqueWorld::RidePlan> LegPlan;
	FTSTicker::FDelegateHandle LejogTicker;
	void StartLejog(const FString& Spec);
	void NextLeg();
	void EndLeg(const FString& How);
	bool LejogTick(float DeltaSeconds);
	void LejogSay(const FString& Line) const;

	// (shared with the workers, which may outlast a frame but never the subsystem: Deinitialize waits for them)
	std::shared_ptr<FTorqueWorldShared> Shared;
	TFuture<void> TestRun;
	TFuture<void> RideRun;
	std::shared_ptr<TorqueWorld::RidePlan> Plan;
	bool bPlanning = false;
	FString Note;   // (game thread only)
	void RidePlanned(std::shared_ptr<TorqueWorld::RidePlan> Made, bool bRide);
	bool TravelTo(std::shared_ptr<TorqueWorld::RidePlan> Made);

	std::shared_ptr<TorqueWorld::RidePlan> Preview;
	TorqueWorld::RideSummary PreviewSum;
	int32 PreviewSerial = 0;
	bool bPlacesRead = false, bFamousRead = false;
	TorqueWorld::Route::Gazetteer PlaceList;
	std::vector<TorqueWorld::Route::FamousRoad> Famous;

	TFuture<void> CacheRun;
	FString CacheSaid;
	bool bCacheBusy = false;
	void TidyCache(bool bClear);

	TFuture<void> MapRun;
	bool bMapBusy = false, bMapWaiting = false, bMapNew = false;
	TorqueWorld::Route::MapView MapNext, MapDrawn;
	TArray<uint8> MapPixels;
	void DrawMap(const TorqueWorld::Route::MapView& View);
	void MapDone(const TorqueWorld::Route::MapView& View, TArray<uint8>&& Pixels, bool bFinal);
};
