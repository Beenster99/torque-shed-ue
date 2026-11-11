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
#include "TorqueMenuMouse.h"
#include "TorqueShedNet.h"
#include "TorquePing.h"
#include "TorquePlayerController.generated.h"

class AHUD;
class UCanvas;
class ATorqueProfilePreview;

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
	// A note under the middle of the screen from this machine (a ping's "nothing there").
	void ShowNote(const FString& Text, double Seconds);

	// [ping] Torque Shed's ping (TorquePing.h): to the host, which names it and sends it to everyone,
	// and on each player's machine, shown for TorquePing::Life seconds with its chime.
	UFUNCTION(Server, Reliable) void ServerPing(const FTorquePingNet& NewPing);
	UFUNCTION(Client, Reliable) void ClientPing(const FTorquePingNet& NewPing);
	void DrawPings(UCanvas* Canvas);

	// A walker put somewhere at once (stepping up to the paint bench, the tests' looks): on a joined
	// copy a pawn moved there alone is snapped back by the host's movement, so the host moves it too.
	static void PlaceOnHost(APawn* Pawn, const FVector& At, const FRotator& View);
	UFUNCTION(Server, Reliable) void ServerPlace(FVector At, FRotator View);

	// [yard] A joined player riding their bike out into the shed's yard, or parking it and walking
	// back in: the host swaps the pawns (Shed/TorqueYard.h).
	UFUNCTION(Server, Reliable) void ServerRideYard(int32 Bay, bool bAtBike);
	UFUNCTION(Server, Reliable) void ServerGetOffYard();
	UFUNCTION(Server, Reliable) void ServerWalkBackIn();

	// [shed test] The tests' way to the menus (-TorqueShedAction's key presses don't reach the game):
	// "pause", "profile", "settings" or "close" (the world isn't paused, so the test runs on), and a
	// profile slider set by its name's first word ("jaw", "chin", ... or "height", "avatar").
	FString TestMenu(const FString& Which);
	FString TestFace(const FString& Slider, float Value);
	// [settings test] "set:<field>:<value>" (fov, timeofday, telemetry, ...): changed for the run, not saved.
	FString TestSet(const FString& Field, const FString& Value);

private:
	// [co-op] The bike picker for Buttertubs: a bay's name ("Bay 3: Simon's cruiser"), the other
	// rider who has picked it (empty: free), and the next free bike from From in direction Dir.
	FString RideBikeName(int32 Bay) const;
	FString RideBikeTakenBy(int32 Bay) const;
	int32 NextRideBike(int32 From, int32 Dir) const;
	enum class EPage : uint8 { None, Pause, Settings, Graphics, Sound, Gameplay, Controller, Character, Profile, Keys, PadCheck, RideBike, Credits };
	struct FRow
	{
		FString Label, Value, Hint;
		TFunction<void()> Choose;           // Enter / Cross / A
		TFunction<void(int32)> Step;        // Left / Right: -1 or +1
		bool bTyping = false;               // the name: letters typed go into it
		// [profile] A slider: drawn as a track with a knob at Frac (0..1); dragging calls SetFrac.
		float Frac = -1.0f;
		TFunction<void(float)> SetFrac;
		bool bFace = false;                 // the Profile picture shows the face for this row
		bool bCentred = false;              // [profile] the slider fills out from its middle
		TFunction<void()> Commit;           // a slider let go: saved and put on the game
		bool bToggle = false, bOn = false;  // an on / off switch, and which
		bool bDisabled = false;             // shown but not changeable here (the host's, in co-op)
		// Key bindings: the keys in each slot, the slot picked, and what else uses one of them.
		TArray<FString> Caps;
		int32 CapOn = -1;
		FString Warning;
		bool Works() const { return Choose || Step; }
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
	// Settings > Controller > Key bindings: the action waiting for a key (-1 for none), into which of
	// its two slots, and the frame it started (the click or Enter that started it isn't the key).
	int32 ListenAction = -1, ListenSlot = 0, KeySlot = 0;
	uint64 ListenFrame = 0;
	void ReadKeyToBind();
	// A long page scrolls: the first row drawn.
	int32 FirstRow = 0;
	// Settings > Graphics > Avatar shadows, for people who arrive later (checked now and then).
	double ShadowCheckIn = 0.0;

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
	// The mouse on the menus (TorqueMenuMouse.h): where the last drawing put things, the pointer's
	// last place (a row is picked only when it moves), and the cursor shown while a menu is up.
	FTorqueMenuHits Hits;
	FVector2D LastMouse = FVector2D(-1.0, -1.0);
	bool bCursorMine = false;
	void UpdateCursor();
public:
	// Whether the pointer moved this frame (and where it is): shared with the start menu.
	static bool MouseMoved(APlayerController* PC, FVector2D& Last, FVector2D& Now);
private:
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
	TArray<TorquePing::FShown> Pings;   // [ping] showing on this machine

	// [profile] Torque Shed's profile picture, top right of the Esc menu: it opens the Profile page
	// (your MetaHuman, height and face; Shed/TorqueProfile.h), which shows you beside its sliders.
	TWeakObjectPtr<ATorqueProfilePreview> Preview;
	int32 DragRow = -1;         // the slider being dragged, and its track's ends on screen
	double DragX0 = 0.0, DragX1 = 1.0;
	bool bProfileSent = false;
	void ProfileChanged();
	void UpdateProfilePreview();
	void DrawProfile(AHUD* Hud, UCanvas* Canvas);
};
