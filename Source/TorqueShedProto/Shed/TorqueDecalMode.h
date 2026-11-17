// Decal mode in the shed (the decals plan, beenster 17 Nov 2026): F3 at a bike frees the mouse and the
// camera orbits it, as geometry mode does. A panel down the left holds the library: lettering (type
// a word, pick a face: the word is one decal), stripes and shapes, tattoo artwork, and your own
// pictures (PNGs dropped in Saved/Decals). Pick one and it follows the mouse over the bike's tank,
// fairing, side panels, tail and mudguards, wrapped on the surface; click to stick it there.
//
// Click a decal to pick it (drag to slide it over the surface); Shift + wheel or [ ] sizes it,
// Ctrl + wheel or , . turns it, Delete takes it off. The two rows of colour swatches set its fill
// and its ink; switches: filled or line only, a sticker or under the lacquer, a pair (mirrored on the
// other side), and for lettering an outline, a drop shadow and an arch. Up to eight on a part.
// Right-drag orbits, the wheel zooms. "Save to this bike" sends them to the host (DecalSave, one
// part each), so friends see them; Esc leaves (asking first if they're not saved).
//
// "Helmet" at the top puts the player's own helmet on a stand beside the bike to put decals on it too
// (stage 4: kept on this machine, TorqueDecals::MyHelmet, and sent to friends with the player).
// "Sets" keeps a part's decals by name to put on another bike's (stage 7); "Worn" chips a decal.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueDecalMode.generated.h"

class AHUD;
class ATorqueWalker;
class UCameraComponent;
class UCanvas;

UCLASS()
class ATorqueDecalMode : public AActor
{
	GENERATED_BODY()

public:
	ATorqueDecalMode();
	virtual ~ATorqueDecalMode() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Opens it for the walker on the bike looked at, else the nearest within 4 m. False, and why, if
	// there's none.
	static bool Open(ATorqueWalker* Walker, int32 LookedAtBay, FString& WhyNot);
	// The one open for this player, or null.
	static ATorqueDecalMode* For(const AController* Controller);
	// Over everything else on the screen (ATorqueHUD).
	void DrawPanel(AHUD* Hud, UCanvas* Canvas);

	UPROPERTY(VisibleAnywhere, Category = "Decals") UCameraComponent* Camera;

	// --- For hands-off tests (UTorqueShedTest decal:...) ------------------------------------------
	// A decal (library id, or "text:<face>:<words>") placed on Slot facing out on its right side
	// (Side +1) or left (-1) at its middle; then saved.
	FString TestPlace(FName Slot, const FString& What, int32 Side);
	FString TestSave();
	void TestExit();

private:
	struct FState;
	TSharedPtr<FState> St;

	void Begin(ATorqueWalker* Walker, class ATorqueShedBike* Bike, int32 Bay);
	void Close();
	void RequestExit();
	void Save();
	bool IsDirty() const;
	void HideWalker(bool bHide);
	void UpdateCamera();
	void ReadInput(float Dt);
	void Press(const FVector2D& Mouse);
	void Refresh(bool bForce);
	bool HoverAt(const FVector2D& Mouse, FName& OutSlot, FVector& OutAt, FVector& OutN) const;
	int32 DecalUnder(FName Slot, const FVector& At) const;
	void PlaceBrush(FName Slot, const FVector& At, const FVector& N);
	void Resize(float Factor);
	void Turn(float Degrees);
	void Remove();
	void SetColour(bool bInk, uint32 Rgb);
	void Toggle(uint32 Flag);
	void Arch(float Step);
	void PickLibrary(const FString& Id);
	void PickText();
	void PickOwn(const FString& Name);
	// The camera on the bike, or on the player's helmet on a stand beside it (made the first time).
	void ShowHelmet(bool bHelmet);
	void AddHelmetShape();
	// Reusable sets (stage 7): the part whose decals "Keep" keeps, keeping them, and putting one on.
	FName KeepFrom() const;
	void KeepSet();
	void UseSet(const FString& Name);
	void Say(const FString& Text);
	FDelegateHandle TellHandle;
};
