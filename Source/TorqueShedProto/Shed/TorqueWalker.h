// The player on foot in the shed, in first person (after Torque Shed's walker.gd): walk round the
// bike on its stand, look at a part and swap it, pick a spare off the parts bench and fit it, and
// ride out of the roller door. Keys: WASD and the mouse, Shift to hurry, Space to hop, E to fit or
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
enum class ETorqueLook : uint8 { None, BikePart, Spare, Door };

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

	// After walker.gd: eye 1.62 m, walk 3.2 m/s, hurry 5.5 m/s, reach 2.6 m.
	UPROPERTY(EditAnywhere, Category = "Walker") float WalkSpeed = 320.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float RunSpeed = 550.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float Reach = 260.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float MouseDegPerPixel = 0.143f;
	UPROPERTY(EditAnywhere, Category = "Walker") float PadDegPerSecond = 150.0f;

private:
	// What the walker is looking at.
	ETorqueLook Target = ETorqueLook::None;
	FName TargetSlot, TargetPart, TargetModelPart;
	TWeakObjectPtr<UStaticMeshComponent> TargetMesh;

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
	FName ChosenFor(FName Slot) const;
	void Choose(int32 Step);

	void ReadInput(float Dt, bool& bUse, bool& bRide, int32& Pick);
	void FindTarget();
	void Use();
	void StartRideOut();
	void UpdateHud();
};
