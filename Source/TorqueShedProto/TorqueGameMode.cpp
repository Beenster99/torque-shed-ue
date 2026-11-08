#include "TorqueGameMode.h"
#include "TorqueBikePawn.h"
#include "TorqueHUD.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueWalker.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ATorqueGameMode::ATorqueGameMode()
{
	DefaultPawnClass = ATorqueBikePawn::StaticClass();
	HUDClass = ATorqueHUD::StaticClass();
}

void ATorqueGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Engine warnings in red over the game (such as the ray tracing memory note) are for developers:
	// off unless -TorqueScreenMessages. The same as the DisableAllScreenMessages console command.
	if (!FParse::Param(FCommandLine::Get(), TEXT("TorqueScreenMessages")))
	{
		GAreScreenMessagesEnabled = false;
	}
}

UClass* ATorqueGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ATorqueShedBike> It(World);
		if (It) return ATorqueWalker::StaticClass();
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}
