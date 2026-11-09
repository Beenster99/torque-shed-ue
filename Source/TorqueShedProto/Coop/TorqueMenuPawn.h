// The start menu, as Torque Shed's title: Your name, Open the shed (solo), Host a co-op shed, Join
// (an address or a host name), rides found on this network, Settings (ATorquePlayerController's
// pages), Exit game.
// A slow camera over wherever the game starts (the shed, or the top of Buttertubs) with the menu
// drawn by ATorqueHUD. Keys, an Xbox pad or a PlayStation pad (read directly, as on the bike).
//
// ATorqueGameMode gives the player this pawn when the game is launched plainly (no "play" option on
// the map and no -Torque test switches), so hands-off test runs and co-op joins skip it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SonyPad.h"
#include "TorqueMenuMouse.h"
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
	enum class EItem : uint8 { Name, Solo, Host, Join, Code, Lan, Search, Settings, Quit };
	struct FEntry
	{
		EItem Kind = EItem::Solo;
		int32 Lan = -1;
		FString Text, Hint;
	};
	TArray<FEntry> Entries() const;
	void Choose(const FEntry& E);
	void TypeAddress(class APlayerController* PC);
	void TypeCode(class APlayerController* PC);

	TUniquePtr<FSonyPad> Pad;
	uint32 PrevButtons = 0;
	int32 Selected = 1;          // on "Open the shed"
	bool bNameTyped = false;
	FString Address;            // being typed for Join
	FString Code;               // a friend code being typed (EOS)
	FString Busy;               // "Loading the road..." once something is chosen
	double Clock = 0.0, SearchClock = 10.0, StickRepeat = 0.0;
	int32 StickDir = 0;
	// The mouse (TorqueMenuMouse.h): where the last drawing put the rows, and the pointer's last place.
	mutable FTorqueMenuHits Hits;
	FVector2D LastMouse = FVector2D(-1.0, -1.0);
	FVector BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;
};
