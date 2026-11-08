// The pause menu (Esc, or Options / Start on a pad) and the Settings menu: graphics, sound,
// controller, character, and the controls. Drawn over the game by ATorqueHUD, worked with the
// keys, an Xbox pad or a PlayStation pad, like the start menu.
//
// As in Torque Shed, the pause menu of a player hosting co-op says exactly which address friends
// join with. Alone, it pauses the game; in co-op the world carries on.
//
// While a page is up, the bike and the walker leave the controls alone (MenuUp).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SonyPad.h"
#include "TorquePlayerController.generated.h"

class AHUD;
class UCanvas;

UCLASS()
class ATorquePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATorquePlayerController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaTime) override;

	// A menu page is up (and for the frame after it closes, so the key that closed it does nothing
	// else): the pawn ignores the controls.
	static bool MenuUp(const AController* Controller);
	bool IsMenuUp() const;

	void OpenPause();
	void OpenSettings();   // the start menu's Settings
	void CloseMenu();
	void DrawMenu(AHUD* Hud, UCanvas* Canvas);

private:
	enum class EPage : uint8 { None, Pause, Settings, Graphics, Sound, Controller, Character, Controls };
	struct FRow
	{
		FString Label, Value, Hint;
		TFunction<void()> Choose;           // Enter / Cross / A
		TFunction<void(int32)> Step;        // Left / Right: -1 or +1
		bool bTyping = false;               // the name: letters typed go into it
	};

	EPage Page = EPage::None;
	TArray<EPage> Stack;
	int32 Selected = 0;
	uint64 ClosedFrame = 0;
	bool bPausedGame = false;
	double Clock = 0.0;

	TUniquePtr<FSonyPad> Pad;
	uint32 PrevButtons = 0;
	int32 StickY = 0, StickX = 0;
	double RepeatY = 0.0, RepeatX = 0.0;
	TArray<FIntPoint> Resolutions;

	void Open(EPage P);
	void Back();
	FString Title() const;
	TArray<FRow> Rows();
	void Changed();
	void TypeName();
	void ReadMenuInput(float Dt);
	bool WantsPause() ;
};
