// Geometry mode in the shed: Torque Shed's geometry mode (scripts/geometry_mode.gd and geo_gizmo.gd),
// ported, with finer fit control on top. F2 at a bike frees the mouse and the camera orbits it.
// Click a part of it (or pick it in the parts list on the left) to select it, then drag the gizmo:
// arrows to move it, rings to turn it, boxes to stretch it along one of its axes (the middle box
// sizes it all over). W / E / R switch handles, Ctrl snaps (1 cm, 5 degrees, 5%), Z undoes,
// Backspace resets the part, right-drag orbits, middle-drag pans, the wheel zooms.
//
// A catalogue bike (laid out by fitting's rig) opens on the rig's range handles: the tank's mounts,
// the pegs along their plate, the seat hoop's height and angle, the bike re-forming round them.
// Moving any part any way is behind the "Expert: free move" box (always on for the Speed Twin
// model, which has no rig).
//
// Finer control, new here (Fit/BikeGeometry.h): Ctrl+Shift snaps to 1 mm, half a degree and 1%;
// Shift drags at a tenth of the mouse; Left / Right nudge along the lit axis (Up / Down picks it;
// Ctrl for Godot's bigger steps); click a number of the readout to type it in; L moves along the
// part's own turned axes; the gaps from the part to the ones round it are listed after each move.
// A pad works it too: the right stick orbits, L2 / R2 zoom, L1 / R1 pick the part, Triangle the
// handle, the D-pad nudges, Options saves, Circle leaves.
//
// The fit is this one bike's, a draft until saved: "Save to this bike" sends it to the host (the
// GeoSave request), which keeps it with the bike, so in co-op everyone sees it as they see a part
// fitted. Leaving with changes not saved asks to save or discard them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueGeometryMode.generated.h"

class AHUD;
class APlayerController;
class ATorqueShedBike;
class ATorqueWalker;
class UCameraComponent;
class UCanvas;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTorqueBuildSubsystem;
struct FTorqueShedBike;
struct FTorqueShedRequest;

// A bay bike's parts as drawn, for showing a fit on them: each part's transform at rest and each
// slot's middle (its parts' box, in the bike's own cm), which a slot's offset turns and sizes about.
struct FTorqueBikeFitView
{
	TArray<FTransform> Rest;
	TMap<FName, FVector> Pivots;
	// After the bike's drawn (at rest): what it's drawn as.
	void Capture(const TArray<UStaticMeshComponent*>& Parts);
	// Each part moved by its slot's offset in Geo (Fit/BikeGeometry text; "" puts them back).
	void Show(const TArray<UStaticMeshComponent*>& Parts, const FString& Geo) const;
};

namespace TorqueGeometryMode
{
// The host's side of the GeoSave request (shed_state geo_save): why not ("" when it can), and doing
// it: the fit, made safe, kept with the bike. Says what was done.
FString CheckSave(const UTorqueBuildSubsystem& Shed, const FTorqueShedRequest& R);
FString ApplySave(FTorqueShedBike& Bike, const FString& Text);
// A fit's text made safe (a save from disk or the host): parsed, sanitized, written back; "" if bad.
FString Clean(const FString& Text);
// Each slot's offset in Geo (Fit/BikeGeometry text) as a transform in the bike's own cm, about that
// slot's middle in Pivots (the box round its parts at rest): a part at rest T is drawn at T * Out[slot].
void SlotMoves(const FString& Geo, const TMap<FName, FVector>& Pivots, TMap<FName, FTransform>& Out);
// The longest a fit's text may be (a slot each, every number exact, is well under it).
constexpr int32 MaxText = 16384;
}

UCLASS()
class ATorqueGeometryMode : public AActor
{
	GENERATED_BODY()

public:
	ATorqueGeometryMode();
	virtual ~ATorqueGeometryMode() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Opens it for the walker on the bike it's at (main.gd geometry_bike: the one looked at, else the
	// nearest within 4 m). False, and why, if there's none.
	static bool Open(ATorqueWalker* Walker, int32 LookedAtBay, FString& WhyNot);
	// The one open for this player, or null.
	static ATorqueGeometryMode* For(const AController* Controller);
	// Over everything else on the screen (ATorqueHUD): the panel, the parts list and the gizmo.
	void DrawPanel(AHUD* Hud, UCanvas* Canvas);

	UPROPERTY(VisibleAnywhere, Category = "Geometry") UCameraComponent* Camera;

	// --- For hands-off tests (UTorqueShedTest geo:...) ------------------------------------------------
	// A rig handle (tank_up, pegs, hoop_up, hoop_deg) set, the bike re-formed round it.
	FString TestRig(const FString& Key, double Value);
	// Slot's part moved by Cm (Godot's frame: x right, y up, z back), free move on, and selected.
	FString TestMove(FName Slot, const FVector& Cm);
	FString TestSelect(FName Slot);
	// "Save to this bike", and the fit as sent.
	FString TestSave();
	// Leaves as Exit does (asking if not saved: then discards).
	void TestExit();
	int32 TestBay() const;

private:
	struct FState;
	TSharedPtr<FState> St;   // shared, not unique: UHT's generated vtable-helper ctor would need FState complete to delete a TUniquePtr
	UPROPERTY(Transient) UMaterialInstanceDynamic* LitMaterial = nullptr;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Lit;

	void Begin(ATorqueWalker* Walker, ATorqueShedBike* Bike, int32 Bay);
	void Close();
	void RequestExit();
	void DiscardAndClose();
	void Save();
	bool IsDirty() const;
	bool HasRig() const;
	bool FreeMove() const;
	void SetExpert(bool bOn);
	void Select(FName Slot);
	void Refit(bool bRig);
	void PushUndo();
	void UndoOnce();
	void ResetPart();
	void ResetAll();
	void UpdateCamera();
	void HideWalker(bool bHide);
	void UpdateHighlight();
	void UpdateGaps();
	void ReadInput(float Dt);
	void Press(const FVector2D& Mouse);
	void Release();
	void DragTo(const FVector2D& Mouse);
	int32 PickHandle(const FVector2D& Mouse) const;
	FName PickPart(const FVector2D& Mouse) const;
	TArray<FName> PickableSlots() const;
	void Say(const FString& Text);
	FDelegateHandle TellHandle;
};
