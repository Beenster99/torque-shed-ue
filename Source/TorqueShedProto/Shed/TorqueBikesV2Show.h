// [bikes v2] The Bike Models V2 bikes standing in the shed to be looked at beside the old bikes
// (BikesV2/README.md): the ones Data/BikesV2/show.json lists, each where it says, drawn by
// TorqueShedCatalogue::MakeV2Display. Not ridable, not fitted, no collision, nothing saved; each
// machine makes its own (they aren't sent in co-op). show.json's "enabled": false hides them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueBikesV2Show.generated.h"

UCLASS()
class ATorqueBikesV2Show : public AActor
{
	GENERATED_BODY()

public:
	ATorqueBikesV2Show();
	virtual void BeginPlay() override;

	// Spawns the show in World if show.json is there and enabled (and the world is the shed's).
	static void SpawnIn(UWorld* World);
};
