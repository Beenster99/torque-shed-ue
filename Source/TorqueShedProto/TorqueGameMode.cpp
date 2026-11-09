#include "TorqueGameMode.h"
#include "TorqueBikePawn.h"
#include "TorqueHUD.h"
#include "Coop/TorqueGameState.h"
#include "Coop/TorqueMenuPawn.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorqueSession.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueWalker.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ATorqueGameMode::ATorqueGameMode()
{
	DefaultPawnClass = ATorqueBikePawn::StaticClass();
	HUDClass = ATorqueHUD::StaticClass();
	GameStateClass = ATorqueGameState::StaticClass();
	PlayerControllerClass = ATorquePlayerController::StaticClass();   // the pause and settings menus
	PlayerStateClass = ATorquePlayerState::StaticClass();             // colour, activity, bay
	// Co-op: the host moving between the shed and the road takes everyone with them, still connected.
	bUseSeamlessTravel = true;
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
	// The game starts in the shed, but hands-off ride tests (-TorqueAutoRide, -TorqueStartKm=,
	// -TorqueCamera=, -TorqueRide) want the road: straight there, as before the shed.
	if (GetNetMode() == NM_Standalone && IsShed() && !UGameplayStatics::HasOption(OptionsString, TEXT("play")))
	{
		const TCHAR* Cmd = FCommandLine::Get();
		FString Ignored;
		int32 IgnoredInt = 0;
		if (FParse::Param(Cmd, TEXT("TorqueAutoRide")) || FParse::Param(Cmd, TEXT("TorqueRide")) ||
			FParse::Value(Cmd, TEXT("TorqueStartKm="), Ignored) || FParse::Value(Cmd, TEXT("TorqueCamera="), IgnoredInt))
		{
			UE_LOG(LogTemp, Log, TEXT("Torque: a ride test: to %s"), UTorqueSession::RideMap);
			GEngine->SetClientTravel(GetWorld(), *(FString(UTorqueSession::RideMap) + TEXT("?play")), TRAVEL_Absolute);
			return;
		}
	}
	// Hosting: tell the network (the menu's LAN list) there's a ride to join.
	if (GetNetMode() == NM_ListenServer)
	{
		if (UTorqueSession* Session = UTorqueSession::Get(this)) Session->AdvertiseHostedGame();
	}
}

bool ATorqueGameMode::ShowsMenu() const
{
	if (GetNetMode() != NM_Standalone) return false;
	if (UGameplayStatics::HasOption(OptionsString, TEXT("play"))) return false;
	const TCHAR* Cmd = FCommandLine::Get();
	FString TitleSteps;
	const bool bForce = FParse::Param(Cmd, TEXT("TorqueMenu")) || FParse::Value(Cmd, TEXT("TorqueTitleTest="), TitleSteps);
	if (bForce) return true;
	// Play-in-editor goes straight to riding, as before.
	if (GetWorld() && GetWorld()->IsPlayInEditor()) return false;
	// Any test switch (-TorqueAutoRide, -TorqueNoInput, -TorqueStartKm=, -TorqueCamera=, -TorqueWeather=,
	// -TorqueRide...) means a run that goes straight to the ride, as before the menu.
	FString Token;
	while (FParse::Token(Cmd, Token, false))
	{
		if (!Token.StartsWith(TEXT("-Torque"))) continue;
		if (Token.StartsWith(TEXT("-TorqueScreenMessages")) || Token.StartsWith(TEXT("-TorqueName")) ||
			Token.StartsWith(TEXT("-TorqueNet")))
			continue;
		return false;
	}
	return true;
}

bool ATorqueGameMode::IsShed() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetMapName().Contains(TEXT("Shed"))) return true;
	TActorIterator<ATorqueShedBike> It(const_cast<UWorld*>(World));
	return (bool)It;
}

UClass* ATorqueGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (ShowsMenu() && InController && InController->IsLocalController()) return ATorqueMenuPawn::StaticClass();
	if (IsShed()) return ATorqueWalker::StaticClass();
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

FString ATorqueGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	const FString Name = UGameplayStatics::ParseOption(Options, TEXT("TorqueName"));
	if (NewPlayerController && !Name.IsEmpty()) ChangeName(NewPlayerController, Name.Left(20), false);
	return Error;
}

void ATorqueGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (GetNetMode() == NM_Standalone) return;
	const APlayerState* PS = NewPlayer ? NewPlayer->PlayerState : nullptr;
	UE_LOG(LogTemp, Log, TEXT("Torque net: %s joined (%d riding)"), PS ? *PS->GetPlayerName() : TEXT("someone"),
		GameState ? GameState->PlayerArray.Num() : 0);
}

void ATorqueGameMode::Logout(AController* Exiting)
{
	const APlayerState* PS = Exiting ? Exiting->PlayerState : nullptr;
	if (GetNetMode() != NM_Standalone)
		UE_LOG(LogTemp, Log, TEXT("Torque net: %s left"), PS ? *PS->GetPlayerName() : TEXT("someone"));
	Super::Logout(Exiting);
}
