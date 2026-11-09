// The pause menu (Esc, or Options / Start on a pad) and the Settings menu: graphics, sound,
// controller, character, and the controls. Drawn over the game by ATorqueHUD, worked with the
// keys, an Xbox pad or a PlayStation pad, like the start menu.
//
// As in Torque Shed, the pause menu of a player hosting co-op says exactly which address friends
// join with. Alone, it pauses the game; in co-op the world carries on.
//
// While a page is up, the bike and the walker leave the controls alone (MenuUp).
//
// It also carries this player's shed actions to the host in co-op (TorqueShedNet.h): a joined
// player's fits, paints and new bikes as they happen, washing summed ten times a second.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SonyPad.h"
#include "TorqueShedNet.h"
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
	// A short note from the host (a part that didn't fit), under the middle of the screen.
	void DrawNote(AHUD* Hud, UCanvas* Canvas);

	// The shed in co-op: a joined player's actions, for the host to apply.
	UFUNCTION(Server, Reliable) void ServerShed(const FTorqueShedRequestNet& Request);
	UFUNCTION(Client, Reliable) void ClientNote(const FString& Text);

	// A walker put somewhere at once (stepping up to the paint bench, the tests' looks): on a joined
	// copy a pawn moved there alone is snapped back by the host's movement, so the host moves it too.
	static void PlaceOnHost(APawn* Pawn, const FVector& At, const FRotator& View);
	UFUNCTION(Server, Reliable) void ServerPlace(FVector At, FRotator View);

private:
	enum class EPage : uint8 { None, Pause, Settings, Graphics, Sound, Gameplay, Controller, Character };
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
	int32 TabIndex() const;      // Settings' tab open (0 Graphics .. 4 Character), -1 for none
	void SwitchTab(int32 Dir);
	TArray<FRow> Rows();
	void Changed();
public:
	// Typing into a text row (a name): letters (Shift for capitals), digits, space, - and _,
	// Backspace, Delete clears, Ctrl+V pastes. Shared with the start menu.
	static void TypeText(APlayerController* PC, FString& Text, int32 Max);
private:
	void ReadMenuInput(float Dt);
	bool WantsPause();

	// The shed in co-op.
	bool ForwardShed(const FTorqueShedRequest& Request);
	// The host: what the shed says to this (joined) player about what they did.
	void TellPeer(int32 Peer, const FString& Text, bool bOk);
	FDelegateHandle TellHandle;
	void SyncPlayerState(float Dt);
	FString SentActivity;
	int32 SentBay = -2;
	bool bLookSent = false;
	FString Note;
	double NoteFor = 0.0;
};
