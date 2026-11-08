// The shed on disk (UTorqueBuildSubsystem): every bay's bike, what's fitted, the dirt, the paint
// and the parts last taken off. Saved on the host (the shed is the host's; clients keep a copy).
// Slot "Shed" for play; test runs (any -Torque test switch, such as -TorqueShedAction=) use
// "ShedTest", reset at the start of every run, so a test never changes the player's shed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TorqueShedSave.generated.h"

USTRUCT()
struct FTorqueShedSaveBike
{
	GENERATED_BODY()

	UPROPERTY() bool bExists = false;
	UPROPERTY() FName Bike;
	UPROPERTY() TMap<FName, FName> Fitted;
	UPROPERTY() TMap<FName, float> Dirt;
	UPROPERTY() TMap<FName, FLinearColor> Paint;
	UPROPERTY() TArray<FString> Removed;   // "slot:part", newest first: what lies on the bay's bench
	UPROPERTY() bool bRode = false;
};

UCLASS()
class UTorqueShedSave : public USaveGame
{
	GENERATED_BODY()

public:
	// 1: one bike (the fields below Bikes); 2: a bike per bay.
	UPROPERTY() int32 Version = 2;
	UPROPERTY() TArray<FTorqueShedSaveBike> Bikes;
	UPROPERTY() int32 MyBike = 0;

	// Version 1, read once and moved into bay 0.
	UPROPERTY() FName Bike;
	UPROPERTY() TMap<FName, FName> Fitted;
	UPROPERTY() TMap<FName, float> Dirt;
	UPROPERTY() TMap<FName, FLinearColor> Paint;
	UPROPERTY() TArray<FString> Removed;
};
