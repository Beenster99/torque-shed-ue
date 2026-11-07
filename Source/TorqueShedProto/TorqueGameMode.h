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
};
