// The player on foot in the shed, in first person: Torque Shed's walker.gd. Pick a tool and hold
// the work button on what you look at: hands take a part off a bike (once it's unbolted), pick one
// up, put it down on the workbench, a machine, the floor or in a bin, and fit what you hold to a
// bike or the engine on the stand; the wrench unbolts and bolts up; the brush scrubs off dirt; the
// grinder and welder restore the parts on the repair bench; the spray can paints; the pressure
// washer's lance blasts muck off in the wash bay. F unpacks a delivered box, uses the parts
// computer (order parts and project bikes) and rides your bike out of the roller door once it's
// complete. Every action is a request to the shed (UTorqueBuildSubsystem), checked first, so the
// prompt says why not before you start, and done by the host in co-op, so everyone sees it.
//
// Keys (as Torque Shed): WASD and the mouse, Shift to hurry, C (Ctrl) to crouch, Space to hop;
// left mouse to work; Q / E or the wheel (1-6) for the tool; R or right mouse for the spray
// colour (and the alternative: lift a wheel off the tyre changer whole), T for the finish; F to
// unpack, use the computer or ride out; H for help, Esc for the menu. A pad: sticks, L3 to hurry,
// R3 to crouch, Cross / A to work, L1 / R1 for the tool, Square / X for the colour or alternative,
// Triangle / Y for F, Circle / B to hop.
// It's a stock Character, so co-op can replicate its movement as it is.
#pragma once

#include "CoreMinimal.h"
#include "CoreGlobals.h"   // GFrameCounter
#include "GameFramework/Character.h"
#include "Shed/TorqueBuildSubsystem.h"   // FTorqueShedRequest
#include "TorqueBikePawn.h"   // FHudLine
#include "Shed/TorqueAvatar.h"
#include "Shed/TorquePartsComputer.h"
#include "TorqueWalker.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USceneComponent;
struct FTorquePlace;
struct FSonyPadState;

// What the walker is looking at.
enum class ETorqueLook : uint8
{
	None,
	BikePart,   // a part on a bay's bike (Bay, Slot)
	Item,       // a part that isn't on a bike (Item; on an engine block, Engine and Slot too)
	Place,      // a place to put things: the bench, a machine, a bin, the computer, a bay's floor
	Floor,      // the floor (or the yard)
	Door,       // the roller door
};

// The walker's tools (walker.gd TOOLS, without the paint gun, the stopwatch and the polisher).
enum class ETorqueTool : uint8 { Hands, Wrench, Brush, Repair, Spray, Lance, Gun, Count };   // Gun: the spray gun (key 7)

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
	// The PlayStation pad as it is now (geometry mode reads it while it's open: one reader).
	FSonyPadState PadState() const;
	// Whether to draw the aiming dot (walking about, not at the computer).
	bool ShowsCrosshair() const { return !bRidingOut && !bComputer && !bBench; }
	// For the HUD (Torque Shed's hud.gd): the crosshair lit over something, the work ring, the tool
	// bar, the spray swatches and what you're carrying.
	bool HasTarget() const { return Target != ETorqueLook::None && Target != ETorqueLook::Floor; }
	float WorkFraction() const;
	int32 ToolIndex() const { return (int32)Tool; }
	int32 SprayColourIndex() const { return SprayColour; }
	FString SprayFinishText() const;
	// What the spray can or gun can be loaded with (walker.gd spray_loads), in the order T goes
	// through them, and which is loaded.
	TArray<FString> SprayLoadNames() const;
	int32 SprayLoadIndex() const { return SprayFinish; }
	bool SprayIsPaint() const;   // a colour (R picks it), not primer or a coat
	FString CarryingText() const;
	// For the parts computer's monitor (ATorqueShedScreens): whether this player is at it, the
	// category (NAME_None: project bikes) and its rows, and the one picked.
	bool AtComputer() const { return bComputer; }
	// At the computer this frame or the last (Esc that closed it isn't also the pause menu's).
	bool ComputerUpOrJustClosed() const { return bComputer || GFrameCounter <= ComputerClosedFrame + 1; }
	FName ComputerKind() const { return Computer.ScreenKind(); }
	TArray<FName> ComputerRows() const { return Computer.ScreenRows(); }
	int32 ComputerRow() const { return Computer.PickedRow(); }
	// The parts computer's page over the screen (parts_computer.gd), for the HUD to draw.
	void DrawComputer(class AHUD* Hud, class UCanvas* Canvas) const;
	// For the test steps: the computer's page as it'd be clicked (pick a kind and a model, order it).
	FString TestComputer(FName Kind, FName Part, bool bOrder);
	// For the HUD at the mixing bench: the colour in the tin on the scale (as it'd spray), and the
	// colour to match, if there's one. False when not at the bench or there's no tin.
	bool AtBench() const { return bBench; }
	bool BenchSwatches(FLinearColor& OutMix, bool& bOutTarget, FLinearColor& OutTarget) const;

	UPROPERTY(VisibleAnywhere, Category = "Walker") UCameraComponent* Camera;

	// --- For hands-off tests (UTorqueShedTest, -TorqueShedAction=) --------------------------------
	// Stand Distance cm from a point, on the side Away points to, and look at it; say what's seen.
	FString TestLookAt(const FVector& Point, const FVector& Away, double Distance);
	void TestTool(ETorqueTool T) { SetTool(T); }
	void TestCrouch(bool bOn) { SetCrouched(bOn); }
	void TestSpray(int32 Colour, FName Finish);
	// Do what holding the work button (or the alternative) would do to what's looked at, without
	// the wait: through the same plan and request as the keys. Says what happened.
	FString TestAct(bool bAlt = false);
	// Hold the work button for this long (the lance's passes, a real hold).
	void TestHoldAct(double Seconds) { ActHeldFor = Seconds; }
	// Press F at what's looked at (unpack, the computer, ride out).
	FString TestInteract();
	// Order from the parts computer, as its menu does: a part id, or "bike" for a project bike.
	FString TestOrder(FName Part);
	FString Describe() const;
	bool IsRidingOut() const { return bRidingOut; }
	FName TestTargetSlot() const { return TargetSlot; }
	// Everything the last look went into, nearest first (for a test whose aim missed).
	FString TestCandidates() const;
	int32 TestTargetItem() const { return TargetItem; }
	FName TestTargetPlace() const;
	// Take hold of a bay's bike (as holding F on it does), walk it straight onto the dyno's rig or
	// back to its bay (a test shortcut for the walk, not the requests), and let go (F). Says what happened.
	// Wheel bay Bay's bike to Where: 0 home onto its stand, 1 onto the dyno rig, 2 out mid-floor.
	FString TestWheel(int32 Bay, int32 Where);
	// Where bay Bay's bike is (its bay stand, the rig, being wheeled, or the floor), and its pitch and roll as drawn.
	FString BikeWhere(int32 Bay) const;
	const FString& LastNote() const { return Note; }
	// The mixing bay: step up to the bench (or away), and a bay request as its keys make one
	// (TorqueWalkerBench.cpp); pour Grams of tint Code as holding the work button would, in
	// flushes. Say what happened.
	FString TestBench(bool bOn);
	FString TestPaint(const FTorqueShedRequest& R);
	FString TestPour(const FString& Code, float Grams);

	// After walker.gd: eye 1.62 m (0.95 crouched), walk 3.2 m/s, hurry 5.5, crouched 1.5, reach 2.6 m.
	UPROPERTY(EditAnywhere, Category = "Walker") float WalkSpeed = 320.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float RunSpeed = 550.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float CrouchSpeed = 150.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float Reach = 260.0f;
	UPROPERTY(EditAnywhere, Category = "Walker") float MouseDegPerPixel = 0.143f;
	UPROPERTY(EditAnywhere, Category = "Walker") float PadDegPerSecond = 150.0f;

private:
	// --- What's looked at -------------------------------------------------------------------------
	ETorqueLook Target = ETorqueLook::None;
	int32 TargetBay = INDEX_NONE;     // a bike's part (or a bay's floor)
	FName TargetSlot;                 // its slot, or an engine part's
	int32 TargetItem = 0;             // the item looked at (a bike part's item too)
	int32 TargetEngine = 0;           // an engine block whose part is looked at
	const FTorquePlace* TargetPlace = nullptr;   // the place looked at, or the one an item is on
	FVector TargetPoint = FVector::ZeroVector;   // where the look meets it
	void FindTarget();
	struct FLookHit
	{
		ETorqueLook Kind = ETorqueLook::None;
		TWeakObjectPtr<const UStaticMeshComponent> C;
		const FTorquePlace* Place = nullptr;
		double Time = 0.0, Volume = 0.0;
		bool bExact = false;   // by the part's own shape (or a box that is its shape)
	};
	TArray<FLookHit> LookHits;
	// The items within Radius of a point (bike parts and loose ones), the target's first.
	TArray<int32> ItemsNear(const FVector& At, double Radius) const;

	// --- What the work button would do (walker.gd _make_plan) --------------------------------------
	struct FPlan
	{
		bool bValid = false;
		FTorqueShedRequest Request;
		double Time = 0.0;
		FString Verb;          // "take it off", "bolt it up"
		FString Blocked;       // why it can't be done ("" when it can)
		bool bRepeat = false;  // the lance: a pass each Time while held
		bool bComputer = false;   // opens the parts computer instead of asking the shed
		FString Key() const;   // the same job (holding on stays with it)
	};
	FPlan Plan, AltPlan;
	void MakePlan();
	FPlan MakeOne(ETorqueShedAct Kind, double Time, const FString& Verb) const;
	double Progress = 0.0;
	FString ProgressKey;
	bool bWorkHeld = false, bAltHeld = false, bWorkPressed = false, bAltPressed = false;
	double ActHeldFor = 0.0;
	bool bWaitRelease = false;        // a job's done: let go before the next
	void Work(float Dt);
	bool Do(const FPlan& P);

	// --- Tools ---------------------------------------------------------------------------------------
	ETorqueTool Tool = ETorqueTool::Hands;
	void SetTool(ETorqueTool T);
	// The tool in your hand (TorqueToolModels, held where walker.gd holds it), moving as you work
	// with it: the spanner turning, the brush scrubbing, the grinder buzzing and throwing sparks,
	// the can's mist and the lance's jet. Hidden while you carry a part.
	UPROPERTY(Transient) USceneComponent* ToolHolder = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> ToolParts;
	FVector ToolNozzle = FVector::ZeroVector;   // in ToolHolder's frame
	int32 ShownTool = -1;
	FLinearColor ShownCan = FLinearColor::Black;
	double ToolClock = 0.0, WorkBlend = 0.0;
	void UpdateToolView(float Dt);
	// Sparks, mist and spray: small bits flying off the tool's nozzle (world space).
	struct FFlyBit
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 0.0f, Grow = 1.0f;
		int32 Kind = 0;   // 0 spark, 1 paint mist, 2 water
	};
	TArray<FFlyBit> Flying;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> FlyMeshes;
	UPROPERTY(Transient) TArray<UMaterialInstanceDynamic*> FlyMaterials;   // spark, mist, water
	UPROPERTY() UStaticMesh* CubeMesh = nullptr;
	double FlyDebt = 0.0;
	void UpdateFlying(float Dt, bool bEmit);
	// Torque Shed's cues for handling parts: what you look at glows orange (bike_model.gd
	// highlight; a loose part fainter), and a part in your hands shows its footprint where it will
	// go down (walker.gd _update_part_ghost: white, red when it can't), a notch on its front edge.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Lit;
	UPROPERTY(Transient) UMaterialInstanceDynamic* LitMaterial = nullptr;
	UPROPERTY(Transient) UMaterialInstanceDynamic* LitFaintMaterial = nullptr;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> GhostParts;
	UPROPERTY(Transient) TArray<UMaterialInstanceDynamic*> GhostMaterials;   // fill, edge; blocked fill, edge
	void UpdateCues();
	int32 SprayColour = 0;
	int32 SprayFinish = 0;
	bool bCrouched = false;
	void SetCrouched(bool bOn);

	// --- The parts computer (Torque Shed's PartsComputer: order parts and project bikes) ------------
	bool bComputer = false;
	mutable FTorquePartsComputer Computer;
	uint64 ComputerClosedFrame = 0;   // (mutable: drawing it notes where its buttons went)
	void OpenComputer();
	void CloseComputer();
	void ComputerDo(const FTorqueComputerAction& A);
	FString Order(FName Part);

	// --- The paint mixing bench (paint_bench.gd; TorqueWalkerBench.cpp) ---------------------------------
	// Its pages: Mix (a tin on the scale, pour, stir, test, save), Recipes (cards from the swatch
	// wall), Supplies (restock). Holding the work button on a tint pours it, the bottle tipping
	// further the longer it's held; holding R stirs. Pours and stirs go to the host in flushes.
	struct FBenchRow
	{
		ETorqueShedAct Kind = ETorqueShedAct::Count;   // what the work button does (PaintPour: hold it)
		int32 Item = 0, Colour = 0;
		FString Text;
	};
	bool bBench = false;
	int32 BenchPage = 0, BenchRow = 0;
	float BenchTilt = 0.0f, BenchPourHeld = 0.0f;
	float PourUnsent = 0.0f, StirUnsent = 0.0f, BenchSendIn = 0.0f;
	int32 PouringTint = INDEX_NONE;
	TArray<FBenchRow> BenchRows() const;
	void OpenBench();
	void CloseBench();
	void BenchInput(float Dt, int32 RowStep, int32 PageStep, bool bChoose, bool bWorkDown, bool bAltDown, bool bClose);
	void FlushBench(bool bStopPour);
	bool BenchAsk(const FTorqueShedRequest& R);
	void BenchHud(const UTorqueBuildSubsystem* B);
	// Holding a tin from the mixing bay: F on the bench, the shelf or a bin puts it there, R fills the
	// spray gun's cup from it.
	bool TinInteract();

	// --- F ---------------------------------------------------------------------------------------------
	void Interact();
	void StartRideOut(int32 Bay);
	bool bRidingOut = false;
	double RideOutIn = 0.0;

	// --- Input, HUD ------------------------------------------------------------------------------------
	TUniquePtr<FSonyPad> Pad;
	uint32 PrevPadButtons = 0;
	bool bInputReady = false;
	bool bShowHelp = true;
	FString Note;                     // a line under the prompt for a moment: what was done, or why not
	double NoteFor = 0.0;
	bool bNoteOk = true;
	void Say(const FString& Text, bool bOk, double For = 3.0);
	TArray<FHudLine> HudLines;
	void ReadInput(float Dt);
	void UpdateHud();
	FDelegateHandle TellHandle;
	void Told(int32 Peer, const FString& Text, bool bOk);
	bool bPeerSet = false;
	// Wheeling a bike (walker.gd _push_bike): hold F on one to take hold, F again to let go.
	double FHeldFor = 0.0;
	bool bFArmed = false;
	int32 FBay = INDEX_NONE;
	double PushClock = 0.0;
	int32 Wheeling() const;
	FTransform PushTransform() const;
	void PushBike(float Dt);
	void LetGo(int32 Bay);
	class ATorqueShedBike* BikeActor(int32 Bay) const;

	// Performance, as on the bike: P shows it, O (or -TorqueProfile) logs it each second to
	// Saved/Profiling/torque_shed_perf_<time>.csv.
	bool bPerfShow = false, bPerfLog = false;
	double PerfClock = 0.0, PerfSum = 0.0, PerfWorst = 0.0, PerfRun = 0.0;
	int32 PerfFrames = 0;
	FString PerfText, PerfFile;
	void UpdatePerf(float Dt);

	// The eye in the white rooms (the paint shop's booth, the mixing bay): the auto exposure sees a
	// room of white walls and would make them mid grey, so the camera opens up a little there, as a
	// photographer does in snow. 0 out in the shed, 1 in a white room, eased in and out.
	double WhiteRoom = 0.0;
	void UpdateRoomExposure(float Dt);

	// --- Co-op (Coop/TorquePlayerState.h FTorqueHandsNet) ---------------------------------------------
	// This player's hands for the others: offered each frame (sent when it's worth it).
	void ShareHands(float Dt);
	// Another player's walker: their tool, its motion and a faint glow (their colour) on what
	// they're working on, from their hands.
	void FollowHands(float Dt);
	int32 PeerOf() const;   // whose hands these are, as the shed knows them
	TArray<TWeakObjectPtr<UStaticMeshComponent>> TheirLit;
	UPROPERTY(Transient) UMaterialInstanceDynamic* TheirLitMaterial = nullptr;
	// Tell the others what you're doing; it lapses after a few seconds without anything new.
	void Doing(const FString& What, double For = 4.0);
	double DoingFor = 0.0;
	UTorqueBuildSubsystem* Shed() const;

	// Another player's body as a MetaHuman (Shed/TorqueAvatar.h), when the PC has made some; the
	// shape body (Body_*) otherwise.
	TUniquePtr<FTorqueAvatar> Avatar;
	UPROPERTY(Transient) USceneComponent* AvatarRoot = nullptr;
	int32 AvatarFor = -1;             // the colour it was made for
	bool bAvatarDemo = false;
	void UpdateAvatar(float Dt);
};
