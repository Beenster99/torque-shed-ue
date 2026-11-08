#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TorqueGameMode.generated.h"

UCLASS()
class ATorqueGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ATorqueGameMode();
	virtual void BeginPlay() override;
	// In the shed (an ATorqueShedBike in the level) you're on foot; on the road, on the bike.
	// Co-op's game mode replaces this with its own choice per map.
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
};
