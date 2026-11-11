// The game's rules on every map: who you are (the start menu, on foot in the shed, or on the bike),
// and co-op's comings and goings. The host's game runs it; joined riders' games don't have one.
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
	// In the real world's level (/Game/Maps/RideUK) the ground and road are laid out round the ride's
	// start before anyone is put down on it (World/TorqueRideWorld).
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	// The start menu when the game is launched plainly; in the shed (the shed map, or an
	// ATorqueShedBike in the level) on foot; on the road, the bike.
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	// Co-op riders' names (the TorqueName option the start menu puts on the address).
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// Launched with no "play" option on the map, alone, and with none of the -Torque test switches.
	bool ShowsMenu() const;
	bool IsShed() const;
};
