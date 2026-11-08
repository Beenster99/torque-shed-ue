// The start menu: Ride solo, Host a co-op ride, Join by address, rides found on this network, Quit.
// A slow camera over wherever the game starts (the shed, or the top of Buttertubs) with the menu
// drawn by ATorqueHUD. Keys, an Xbox pad or a PlayStation pad (read directly, as on the bike).
//
// ATorqueGameMode gives the player this pawn when the game is launched plainly (no "play" option on
// the map and no -Torque test switches), so hands-off test runs and co-op joins skip it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SonyPad.h"
#include "TorqueMenuPawn.generated.h"

class AHUD;
class UCanvas;
class UCameraComponent;

UCLASS()
class ATorqueMenuPawn : public APawn
{
	GENERATED_BODY()

public:
	ATorqueMenuPawn();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	void DrawMenu(AHUD* Hud, UCanvas* Canvas) const;

	UPROPERTY(VisibleAnywhere, Category = "Menu") USceneComponent* Root;
	UPROPERTY(VisibleAnywhere, Category = "Menu") UCameraComponent* Camera;

private:
	enum class EItem : uint8 { Solo, Host, Join, Lan, Search, Quit };
	struct FEntry
	{
		EItem Kind = EItem::Solo;
		int32 Lan = -1;
		FString Text, Hint;
	};
	TArray<FEntry> Entries() const;
	void Choose(const FEntry& E);
	void TypeAddress(class APlayerController* PC);

	TUniquePtr<FSonyPad> Pad;
	uint32 PrevButtons = 0;
	int32 Selected = 0;
	FString Address;            // being typed for Join
	FString Busy;               // "Loading the road..." once something is chosen
	double Clock = 0.0, SearchClock = 10.0, StickRepeat = 0.0;
	int32 StickDir = 0;
	FVector BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;
};
