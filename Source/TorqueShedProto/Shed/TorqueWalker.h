// The player on foot in the shed, in first person (after Torque Shed's walker.gd): walk round the
// bikes in the bays, look at a part on any of them and swap it, wash it or paint it, pick a spare
// off a bay's table or the parts store and fit it, and ride your bike out of the roller door. The
// bay you last worked on (or pressed F at) is your bike: the store fits to it and you ride it.
// What you're doing goes to the others in a co-op shed (UTorqueBuildSubsystem::ReportActivity). Keys: WASD and the mouse, Shift to hurry, Space to hop, E to fit or
// swap, F to ride out, H for help, Esc to quit. A DualSense (read directly, as on the bike) or an
// Xbox pad: sticks, L3 to hurry, Cross/A to fit or swap, Triangle/Y to ride out, Circle/B to hop.
// It's a stock Character, so co-op can replicate its movement as it is.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TorqueBikePawn.h"   // FHudLine
#include "TorqueWalker.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

// What the walker is looking at: a part of the bike, a spare on the bench, or the roller door.
enum class ETorqueLook : uint8 { None, BikePart, Spare, Door, Swatch };

UCLASS()
class ATorqueWalker : public ACharacter
{
	GENERATED_BODY()

public:
	ATorqueWalker();
	virtual ~ATorqueWalker() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	const TArray<FHudLine>& GetHudLines() const { return HudLines; }
	// Whether to draw the aiming dot (only while walking about).
	bool ShowsCrosshair() const { return !bRidingOut; }

	UPROPERTY(VisibleAnywhere, Category = "Walker") UCameraComponent* Camera;

	// For hands-off tests (UTorqueShedTest, -TorqueShedAction=): stand Distance cm from a point,
	// on the side Away points to, and look at it; then press what E (Cross) or F (Triangle) does.
	// Both go through the same targeting and the same Use / ride out as the keys, and say what
	// they found or did.
	FString TestLookAt(const FVector& Point, const FVector& Away, double Distance);
	FString TestPress(bool bRideOut);
	// Choose which option E fits in a slot (as Q/R, the mouse wheel or the D-pad do while looking at it).
	void TestChoose(FName Slot, FName Part) { Chosen.Add(Slot, Part); }
	// Hold the wash button (G / Square) for this long, wherever the walker is looking.
	void TestHoldWash(double Seconds) { WashHeldFor = Seconds; }

	// After walker.gd: eye 1.62 m, walk 3.2 m/s, hurry 5.5 m/s, reach 2.6 m.
	UPROPERTY(EditAnywhere, Category = "Walker") float WalkSpeed = 320.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float RunSpeed = 550.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float Reach = 260.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float MouseDegPerPixel = 0.143f;
	UPROPERTY(EditAnywhere, Category = "Walker") float PadDegPerSecond = 150.0f;

private:
	// What the walker is looking at.
	ETorqueLook Target = ETorqueLook::None;
	int32 TargetBike = INDEX_NONE;               // the bay it belongs to (a bike's part, a table's spare)
	FName TargetSlot, TargetPart, TargetModelPart;
	TWeakObjectPtr<UStaticMeshComponent> TargetMesh;
	FVector TargetPoint = FVector::ZeroVector;   // where the look meets the target's box
	int32 TargetPaint = INDEX_NONE;              // the paint card looked at

	// Washing: hold G / Square (X on an Xbox pad) on the bike; a sponge takes the dirt off the part
	// you look at and anything within 30 cm of where you look.
	bool bWashHeld = false;
	double WashHeldFor = 0.0;
	double WashedFor = 0.0;
	void Wash(float Dt);
	// The paint picked off the rack, which E puts on bodywork.
	int32 PickedPaint = INDEX_NONE;

	TUniquePtr<FSonyPad> Pad;
	uint32 PrevPadButtons = 0;
	bool bInputReady = false;
	bool bShowHelp = true;
	bool bRidingOut = false;
	double RideOutIn = 0.0;
	FString Note;                     // a line under the prompt for a moment: what was fitted, or why not
	double NoteFor = 0.0;
	TArray<FHudLine> HudLines;

	// Performance, as on the bike: P shows it, O (or -TorqueProfile) logs it each second to
	// Saved/Profiling/torque_shed_perf_<time>.csv.
	bool bPerfShow = false, bPerfLog = false;
	double PerfClock = 0.0, PerfSum = 0.0, PerfWorst = 0.0, PerfRun = 0.0;
	int32 PerfFrames = 0;
	FString PerfText, PerfFile;
	void UpdatePerf(float Dt);
	FString Describe() const;

	// The option E will fit in each slot, chosen with Q/R, the mouse wheel or the D-pad while
	// looking at the slot's part (the next one after what's fitted until you choose).
	TMap<FName, FName> Chosen;
	int32 ChosenBike = INDEX_NONE;    // the bay Chosen is for (it starts again on another bike)
	FName ChosenFor(FName Slot) const;
	void Choose(int32 Step);

	void ReadInput(float Dt, bool& bUse, bool& bRide, int32& Pick);
	void FindTarget();
	void Use();
	void StartRideOut();
	// Tell the others what you're doing; it lapses after a few seconds without anything new.
	void Doing(const FString& What, double For = 4.0);
	double DoingFor = 0.0;
	// This bay becomes your bike.
	void Mine(int32 Bike);
	void UpdateHud();
};
