#include "TorqueGameMode.h"
#include "TorqueBikePawn.h"
#include "TorqueHUD.h"
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
