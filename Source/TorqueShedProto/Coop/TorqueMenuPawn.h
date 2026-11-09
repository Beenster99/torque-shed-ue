// The start menu: Start Solo, Host Game, Join Game, Settings (ATorquePlayerController's pages),
// Exit, each with its own page (see EPage below). Your name is in Settings > Character.
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
	// beenster's menu tree (9 Nov):
	//   Start Solo: New Game, Load Game, Delete Game, Back
	//   Host Game:  New Game, Load Game, Back
	//   Join Game:  IP address (typed), Join, Back
	//   Settings, Exit
	// Load Game and Delete Game list the saved games (UTorqueBuildSubsystem::SavedGames); deleting
	// asks first.
	enum class EPage : uint8 { Main, Solo, Host, Join, Load, Delete, Confirm };
	enum class EItem : uint8 { Solo, Host, Join, Settings, Quit, NewGame, LoadGame, DeleteGame, Back, Address, JoinNow, Game, Yes, No };
	struct FEntry
	{
		EItem Kind = EItem::Back;
		int32 Game = -1;   // EItem::Game: the index into Games
		FString Text, Hint, Side;   // Side: small grey words at the row's right (a game's date)
	};
	TArray<FEntry> Entries() const;
	FString PageTitle() const;
	void Choose(const FEntry& E);
	void GoTo(EPage To);
	void Back();
	void TypeAddress(class APlayerController* PC);
	// Starts the chosen game: solo or hosted (bHosting), new or from Slot.
	void Start(const FString& Slot, bool bNew);

	EPage Page = EPage::Main;
	bool bHosting = false;     // the Load page was opened from Host Game
	TArray<int32> SelectedOn;  // the row picked on each page, for coming back to it
	struct FGameRow
	{
		FString Slot, Name, When;
		int32 Bikes = -1;
	};
	TArray<FGameRow> Games;    // read when Load or Delete opens
	int32 DeleteIndex = -1;    // the game the Confirm page asks about
	FString Note;              // "Game 3 deleted" and the like, under the menu
	void ReadGames();

	TUniquePtr<FSonyPad> Pad;
	uint32 PrevButtons = 0;
	int32 Selected = 0;
	FString Address;            // being typed for Join
	FString Busy;               // "Loading..." once something is chosen
	double Clock = 0.0, StickRepeat = 0.0;
	int32 StickDir = 0;
	// The mouse (TorqueMenuMouse.h): where the last drawing put the rows, and the pointer's last place.
	mutable FTorqueMenuHits Hits;
	FVector2D LastMouse = FVector2D(-1.0, -1.0);
	FVector BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;
};
