// The player's shed on disk: what's fitted, the dirt and the paint (UTorqueBuildSubsystem).
// Slot "Shed" for play; test runs (any -Torque test switch, such as -TorqueShedAction=) use
// "ShedTest", reset to stock at the start of every run, so a test never changes the player's bike.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TorqueShedSave.generated.h"

UCLASS()
class UTorqueShedSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() int32 Version = 1;
	UPROPERTY() FName Bike;
	UPROPERTY() TMap<FName, FName> Fitted;
	UPROPERTY() TMap<FName, float> Dirt;
	UPROPERTY() TMap<FName, FLinearColor> Paint;
};
