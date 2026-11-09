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
#include "Shed/TorqueLookOutline.h"
#include "Shed/TorqueDynoDesk.h"
#include "Shed/TorquePartsComputer.h"
#include "Shed/TorquePaintBenchPanel.h"
#include "Shed/TorqueSprayJob.h"
#include "TorqueWalker.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USceneComponent;
class UTorqueSprayFx;
class UTorqueWashFx;
class FTorqueWashJob;
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
	// How far the part in your hands is turned from the way you face, in degrees (R turns it, as
	// walker.gd's hold_turn): the items actor turns it in your hands, and it goes down that way.
	double HeldTurn() const { return HoldTurn; }
	// For the parts computer's monitor (ATorqueShedScreens): whether this player is at it, the
	// category (NAME_None: project bikes) and its rows, and the one picked.
	bool AtComputer() const { return bComputer; }
	// At the computer this frame or the last (Esc that closed it isn't also the pause menu's).
	bool ComputerUpOrJustClosed() const { return bComputer || bBench || GFrameCounter <= ComputerClosedFrame + 1; }   // (the mixing bench too: Esc closes it)
	FName ComputerKind() const { return Computer.ScreenKind(); }
	TArray<FName> ComputerRows() const { return Computer.ScreenRows(); }
	int32 ComputerRow() const { return Computer.PickedRow(); }
	// The parts computer's page over the screen (parts_computer.gd), for the HUD to draw.
	void DrawComputer(class AHUD* Hud, class UCanvas* Canvas) const;
	// For the test steps: the computer's page as it'd be clicked (pick a kind and a model, order it).
	FString TestComputer(FName Kind, FName Part, bool bOrder);
	// The parts computer's bike type filter ("cafe", "all"...): what the page shows after.
	FString TestComputerType(const FString& Type);
	// The New bike aisle: Style's bike (as it starts) with Swaps (slot=part) on, a colour of its own, ordered.
	FString TestNewBike(const FString& Style, const TArray<FString>& Swaps, bool bOrder);
	// The New bike page's colour picker (FTorqueBikeConfigurator::TestColour's steps).
	FString TestColour(const TArray<FString>& Args);
	void TestCloseComputer() { CloseComputer(); }
	// The dyno desk's page as it'd be worked (opened where you stand if it isn't up): What "open",
	// "tab" (Key: its name), "set" (Key to Value), "stock" (Key back to stock), "run", "close",
	// "log". Says what the page shows after.
	FString TestDesk(const FString& What, const FString& Key, float Value);
	bool AtDesk() const { return bComputer && bDesk; }
	// For the HUD at the mixing bench: the colour in the tin on the scale (as it'd spray), and the
	// colour to match, if there's one. False when not at the bench or there's no tin.
	bool AtBench() const { return bBench; }
	bool BenchSwatches(FLinearColor& OutMix, bool& bOutTarget, FLinearColor& OutTarget) const;
	// The mixing bench's card (TorquePaintBenchPanel), on the HUD's canvas.
	void DrawBench(class AHUD* Hud, class UCanvas* Canvas) const;

	// The spray mini game (Torque Shed's spray_job.gd, spray_hud.gd; TorqueWalkerSpray.cpp): its
	// readout's up (a coat going on, the spray controls up, or a cue showing), drawn over the view.
	bool SprayHudUp() const;
	void DrawSpray(class AHUD* Hud, class UCanvas* Canvas) const;
	bool SprayControlsUp() const { return SprayItem != 0; }
	// The wash game (Torque Shed's wash_job.gd, wash_hud.gd; TorqueWalkerWash.cpp): its readout's up
	// (the lance out at a bike in the wash bay, or a cue showing), and whether this walker's jet is
	// going in the wash bay (anyone's: the bay's mist, ATorqueWashMist).
	bool WashHudUp() const;
	void DrawWash(class AHUD* Hud, class UCanvas* Canvas) const;
	bool WashJetting() const;
	// For the tests: how the wash on the bike in the wash bay is going ("" with none).
	FString TestWashState() const;
	// For the tests: sweep the lance over the wash bay's bike from every side, Passes times at Distance cm
	// (from underneath too if it's raised), the shed told; says how it went.
	FString TestWashPasses(int32 Passes, double Distance);
	void TestWashScan(bool bOn) { bWashScan = bOn; }

	UPROPERTY(VisibleAnywhere, Category = "Walker") UCameraComponent* Camera;

	// --- For hands-off tests (UTorqueShedTest, -TorqueShedAction=) --------------------------------
	// Stand Distance cm from a point, on the side Away points to, and look at it; say what's seen.
	FString TestLookAt(const FVector& Point, const FVector& Away, double Distance);
	// The tests' last look: the act after it aims there again from wherever the walker's settled
	// (it can be nudged off a bench's edge between steps, and the look slides off what it was on).
	FVector TestAim = FVector::ZeroVector;
	bool bTestAim = false;
	void ReAim();
	void TestTool(ETorqueTool T) { SetTool(T); }
	void TestCrouch(bool bOn) { SetCrouched(bOn); }
	void TestSpray(int32 Colour, FName Finish);
	// Do what holding the work button (or the alternative) would do to what's looked at, without
	// the wait: through the same plan and request as the keys. Says what happened.
	FString TestAct(bool bAlt = false);
	// Hold the work button for this long (the lance's passes, a real hold).
	void TestHoldAct(double Seconds) { ActHeldFor = Seconds; }
	// Carrying a part, as the wheel and R would (Reach: cm beyond arm's length, Wheel: clicks out (+) or
	// in (-), Turn: degrees, Taps: quarter turns): then the plan and drop box as they stand. Says it.
	FString TestCarry(const FString& What, double Value);
	FString CarryText() const;
	// Press F at what's looked at (unpack, the computer, ride out).
	FString TestInteract();
	// The pause menu's "Set off on Buttertubs" (the host's, or alone): your bike, checked as F at the
	// door checks it, and everyone goes. A joined player is told it's the host's to choose.
	void StartAdventure();
	// Order from the parts computer, as its menu does: a part id, or "bike" for a project bike.
	FString TestOrder(FName Part);
	// [fluids] (TorqueWalkerFluids.cpp) pump: lift, fill[:seconds] (bay Bay's bike, as the work
	// button), hang, state; fluid: carry:<kind or part>, log, slosh[:kick]. Each says the litres
	// before and after, the nozzle's state, the can's slosh.
	FString TestFluid(const FString& Verb, const TArray<FString>& Args, int32 Bay);
	FString FluidLog() const;
	// The movable kit (TorqueWalkerFurniture.cpp): look:<model or piece id> (stand back from the nearest
	// one and look at it), grab, turn:<deg>, walk:<x>:<y> (carry it there), drop, order:<model> (the
	// parts computer's Shed aisle), log. Each says what happened and where the piece is.
	FString TestFurniture(const FString& What, const TArray<FString>& Args);
	FString Describe() const;
	bool IsRidingOut() const { return bRidingOut; }
	// For the shed's sounds (TorqueAudio/TorqueSfx): the tool in hand, whether it's at work (another
	// player's too, from their hands), and the job the work button's on now (false: none).
	ETorqueTool SoundTool() const { return Tool; }
	bool SoundWorking() const { return Progress > 0.0; }
	bool SoundJob(FTorqueShedRequest& Out) const
	{
		const FPlan* P = (bWorkHeld || ActHeldFor > 0.0) ? &Plan : (bAltHeld ? &AltPlan : nullptr);
		if (!P || !P->bValid || !P->Blocked.IsEmpty() || Progress <= 0.0 || P->bComputer) return false;
		Out = P->Request;
		return true;
	}
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
	// The spray game: F with the can or gun on what's looked at (the spray controls), then Passes
	// sweeps across it at Distance cm, held on the trigger as a player would; let go. Says how the
	// coat went (coverage, evenness, runs, dry) and what the shed made of it.
	FString TestSprayPasses(int32 Passes, double Distance);
	// X's scan over the coats going on.
	void TestSprayScan(bool bOn) { SetSprayScan(bOn); }

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
	FPlan SprayPlanFor(int32 Item, int32 Bay) const;   // the can or gun on Item, as loaded
	double Progress = 0.0;
	FString ProgressKey;
	bool bWorkHeld = false, bAltHeld = false, bWorkPressed = false, bAltPressed = false;
	double ActHeldFor = 0.0;
	bool bWaitRelease = false;        // a job's done: let go before the next
	void Work(float Dt);
	bool Do(const FPlan& P);
	// The spray booth's hooks: with your hands free at one, hold right mouse / Square to wind it up
	// or down (the way flips each hold); the shed's told as it goes (PaintHook).
	int32 HookOf() const;              // the hook looked at (or the part on it), else INDEX_NONE
	void HookWork(float Dt);
	int32 HookDir = 1;                 // the next hold's way: +1 up, -1 down
	int32 HookMoving = INDEX_NONE;     // the hook being wound
	float HookSet = 0.0f;
	double HookSendIn = 0.0;

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
	// The same edges again, seen through whatever's in the way (the part in your hands, a bench):
	// M_ShedGhostXRay draws without the depth test. Empty when the level script hasn't made it.
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> GhostXRayParts;
	UPROPERTY(Transient) TArray<UMaterialInstanceDynamic*> GhostXRayMaterials;   // edge; blocked edge
	double GhostClock = 0.0;
	// A thin soft line round what's looked at, and round a place's own machine (TorqueLookOutline.h).
	FTorqueLookOutline LookOutline;
	void UpdateCues();
	// Carrying a part (walker.gd carry_reach, hold_turn): the mouse wheel (Q / E, L1 / R1) puts where
	// it goes down a step further out or nearer, and R (Square) turns it, a quarter a tap or freely
	// while held. Both start again with each part you pick up.
	double CarryReach = 0.0;          // cm further out than arm's length (TWI::CarryMin..CarryMax)
	double HoldTurn = 0.0;            // degrees from the way you face
	double TurnKeyFor = 0.0;          // how long R has been held (-1: not held)
	int32 TurnSign = 1;               // Shift+R turns the other way
	int32 CarryFor = 0;               // the part these were set for
	void CarryInput(float Dt, bool bOut, bool bIn, bool bTurnDown, bool bTurnBack);
	// Where the part in your hands goes down on the floor: arm's length plus CarryReach ahead (short
	// of a wall), turned by HoldTurn (walker.gd _push_transform, for a part).
	FTransform CarryDrop() const;
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
	// The dyno desk (tuning_computer.gd; TorqueDynoDesk): a page like the parts computer's, so it
	// rides on bComputer (the walker stands still, the mouse is out), with bDesk saying which.
	bool bDesk = false;
	mutable FTorqueDynoDesk Desk;     // (mutable: drawing it notes where its buttons went)
	void OpenDesk();
	void DeskDo(const FTorqueDeskOut& Out);

	// --- The paint mixing bench (paint_bench.gd; TorqueWalkerBench.cpp) ---------------------------------
	// Its pages: Mix (a tin on the scale, pour, stir, test, save), Recipes (cards from the swatch
	// wall), Supplies (restock). Holding the work button on a tint pours it, the bottle tipping
	// further the longer it's held; holding R stirs. Pours and stirs go to the host in flushes.
	using FBenchRow = FTorqueBenchRow;   // what the work button does (PaintPour: hold it)
	bool bBench = false;
	mutable FTorquePaintBenchPanel BenchPanel;   // (mutable: drawing it notes where its buttons went)
	FString BenchName;                // the name the next save goes under ("" until it's typed)
	bool bNaming = false;             // typing it
	bool bMousePour = false;          // pouring with the left mouse held (on a tint or in the view)
	float PourPressY = 0.0f;          // where the mouse was pressed: dragging down tips the bottle
	float StirAngle = 0.0f;           // the mouse round the scale, stirring
	float BenchDragTilt = 0.0f;       // how far the drag has tipped the bottle
	bool bStirTrack = false;
	void BenchMouse(class APlayerController* PC, float Dt);
	void BenchTyping(class APlayerController* PC);
	void DoBenchRow(const FBenchRow& Row, bool bByEye);
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

	// --- Spraying (Torque Shed's spray mini game; TorqueWalkerSpray.cpp) --------------------------------
	// Hold the work button with the can or gun on a part and the coat goes on where you aim, as wide
	// and heavy as the distance makes it (TorqueSprayJob); let go once it's on (90%) to finish it, or
	// it finishes itself at 99.5%. F brings up the spray controls, locked on that part: a moment to get
	// set, then slower, careful passes, the look and walk slowed, a slight hand sway that holding your
	// breath (right mouse / L2) steadies; X (D-pad up) scans it for what's missed, thin, run or dry.
	// With the gun on a booth hook, a pass coats every part hanging there that it crosses.
	int32 SprayItem = 0;               // the spray controls are up, locked on this item (0: not)
	int32 SprayBay = INDEX_NONE;       // ... a bike part's bay and slot
	FName SpraySlot;
	double SpraySettle = 0.0;          // seconds still to get set
	double SprayBreath = 1.0;          // breath left to hold (0 .. 1)
	bool bSprayBreathHeld = false, bSpraySteady = false;
	bool bSprayScan = false;
	bool bSprayNeedRelease = false;    // let go of the trigger before it sprays again
	bool bSprayFiring = false;         // paint went on this frame
	FRotator SwayNow = FRotator::ZeroRotator;
	double SwayClock = 0.0;
	TMap<FString, TSharedPtr<FTorqueSprayJob>> SprayJobs;   // every coat started and not finished, by key
	TSharedPtr<FTorqueSprayJob> Spray;                      // the one under the trigger (or aimed at)
	TArray<TSharedPtr<FTorqueSprayJob>> SprayDrying;        // finished coats, flashing and drying off
	TSharedPtr<FTorqueSprayJob> SprayShown;                 // the readout's (it lingers a moment after)
	double SprayLinger = 0.0, SprayFlash = 0.0;
	TorqueSpray::FCue SprayCue;
	double SprayCueAge = 100.0;
	UPROPERTY(Transient) UTorqueSprayFx* SprayFx = nullptr;
	UPROPERTY() UStaticMesh* PlaneMesh = nullptr;
	bool SprayWork(float Dt);          // the work button with the can or gun: true when the spray game has it
	bool SprayInteract();              // F with the can or gun: the spray controls up or down
	void EnterSprayMode();
	void LeaveSprayMode();
	void UpdateSway(float Dt);
	void UpdateSprayFx(float Dt);
	void TickSprays(float Dt);
	void SprayLetGo(bool bFinishReady);
	void FinishSpray(const TSharedPtr<FTorqueSprayJob>& Job);
	void SetSprayScan(bool bOn);
	// The booth hook the part under the spray controls hangs on (INDEX_NONE: not on one), and winding
	// it: Step a click (+1 up, -1 down), Held the way a button's held down.
	int32 SprayHook() const;
	void SprayWind(float Dt, int32 Step, int32 Held);
	float SprayHookSet = -1.0f;        // the height being wound to (-1: not winding)
	double SprayHookHeld = 0.0, SprayHookSend = 0.0;
	FString SprayKey(const FTorqueShedRequest& R) const;
	TSharedPtr<FTorqueSprayJob> SprayJobFor(int32 Item, const FTorqueShedRequest& R, int32 Bay, FName Slot);
	bool SprayOwns(const UStaticMeshComponent* C) const;
	bool IsSprayPlan(const FPlan& P) const;
	FLinearColor SprayMistColour(bool* bOutClear = nullptr) const;   // the mist of what's loaded, for anyone's spray
	TSet<FString> SprayNoGo;           // coats that couldn't start (nothing drawn to paint): the timed hold does those
	FString SprayControlsText() const;

	// --- Pressure washing (Torque Shed's wash mini game; TorqueWalkerWash.cpp) --------------------------
	// With the lance at a bike in the wash bay, holding the work button washes the muck off exactly
	// where the jet lands (FTorqueWashJob), the shed told as it goes; X shows the muck still to do. The
	// job stays on the bike (its muck drawn as washed) until it leaves the bay.
	TSharedPtr<FTorqueWashJob> WashJob;
	UPROPERTY(Transient) UTorqueWashFx* WashFx = nullptr;
	bool bWashScan = false;            // X: the muck still to do lit up (off to start with, as in Torque Shed)
	bool bWashFiring = false;          // the jet's going this frame (this walker's own)
	double WashSendIn = 0.0, WashLinger = 0.0, WashCueAge = 100.0, WashFlash = 0.0;
	FString WashCue;
	bool bWashCueBike = false;
	int32 WashBayOf() const;           // the bike in the wash bay, nearest (INDEX_NONE: none)
	bool WashWork(float Dt);           // the work button with the lance: true when the wash game has it
	void TickWash(float Dt);           // the job follows its bike, the shed's dirt, the scan
	void UpdateWashFx(float Dt);
	void SendWash();
	bool WashOwns(const UStaticMeshComponent* C) const;
	FString WashControlsText() const;

	// --- F ---------------------------------------------------------------------------------------------
	void Interact();
	// F at a bike or the roller door: ride it out into the yard (anyone). bAdventure: set off on
	// Buttertubs with everyone instead (the host's choice alone: the pause menu, StartAdventure).
	// bAtBike: got on where the bike stands (F at it), else just outside the door (F at the door).
	void StartRideOut(int32 Bay, bool bAdventure = false, bool bAtBike = false);
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
	// [kit] The switch boxes by the roller door, the engine stand and the fire extinguisher
	// (TorqueWalkerKit.cpp): true when F went to one of them.
	bool KitInput(float Dt, bool bF, bool bFDown);
	// [fluids] (TorqueWalkerFluids.cpp) The petrol pump: F at it lifts the nozzle, F anywhere hangs it
	// back (and walking off further than the hose goes pulls it out of your hand); with it in your
	// hand the work button fills the bike you're at (PumpPlan). A can or bottle of fluid in your hands:
	// the work button pours it into the tank, engine or brake looked at, the other button drains into
	// it, it lubes a chain, and at the general waste it's tipped away (FluidPlan: false when it has no
	// fluid job here, so it's put down like any part).
	bool FluidInput(bool bF);
	void PumpPlan();
	bool FluidPlan(int32 Held);
	// [furniture] The movable shed and yard kit (TorqueWalkerFurniture.cpp): the piece looked at (it
	// wins over what FindTarget found when it's nearer), hold F on it to take hold of it, R turns it,
	// F puts it down; its HUD lines and outline.
	int32 TargetFurniture = 0;
	bool bFurnArmed = false;
	double FurnFHeld = 0.0;
	void FindFurniture();
	bool FurnitureInput(float Dt, bool bF, bool bFDown, bool bAltJust);
	bool FurnitureHud(TArray<FHudLine>& Lines) const;   // true: moving one, nothing else to show
	void FurnitureOutline(TArray<UPrimitiveComponent*>& Lined) const;
	bool bStandArmed = false;
	double StandFHeld = 0.0;
	bool bSprayAsked = false;

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
	int32 AvatarFor = -1;             // the avatar it was made as (their profile's, or their colour's)
	TArray<uint8> AvatarLook;         // the profile (height, face) put on it
	bool bAvatarDemo = false;
	void UpdateAvatar(float Dt);
	// [hands] Your own body in first person: your avatar under the camera, its hands round the
	// tool (the tool goes where the hand can reach), the camera at its eyes. Off with -TorqueNoBody.
	FVector FirstEye = FVector::ZeroVector;   // the camera's spot (capsule frame) from the body's eyes
	bool bFirstEye = false;
	int32 HandsLogged = -1;                    // the tool whose grip was last logged
	double HandsHeldFor = 0.0;
	FVector EyeSpot() const;                  // where the camera goes: FirstEye, else Torque Shed's eye height
};
