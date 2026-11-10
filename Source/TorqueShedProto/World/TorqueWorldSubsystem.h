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
#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include <memory>
#include <string>

#include "TorqueWorldSubsystem.generated.h"

namespace TorqueWorld
{
class CellStore;
class HeightStore;
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

private:
	void StartTest(const FString& Spec);

	// (shared with the workers, which may outlast a frame but never the subsystem: Deinitialize waits for them)
	std::shared_ptr<FTorqueWorldShared> Shared;
	TFuture<void> TestRun;
};
