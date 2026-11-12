#include "Shed/TorqueWalker.h"
#include "Misc/ScopeExit.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueShedPump.h"

#include "Coop/HandsState.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueAvatarDemo.h"
#include "Coop/TorqueKeys.h"
#include "Shed/TorqueProfile.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorquePaintBayRoom.h"
#include "Shed/TorqueBoothHooks.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Shed/TorqueShedRadio.h"
#include "Shed/TorqueBikeLift.h"
#include "Coop/TorqueLook.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePing.h"
#include "Coop/TorqueSettings.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueYard.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedKit.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueShedScreens.h"
#include "Shed/TorqueToolModels.h"
#include "Shed/TorqueWashJob.h"
#include "Shed/TorqueTyreGame.h"
#include "SonyPad.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderCore.h"
#include "RHI.h"
#include "UnrealClient.h"

namespace TorqueWalkerImpl
{
// walker.gd: how far the brush and the pressure washer reach round where they land (cm), how long
// each job takes (s), and the eye standing and crouched (cm above the capsule's centre).
const double BrushReach = 35.0;
const double JetReach = 30.0;
const double StandEye = 162.0 - 90.0;
const double CrouchEye = 95.0 - 55.0;
// [hands] How far behind the camera your own body stands, cm (the camera keeps to its eyes).
const double BodyBack = 6.0;
const float CrouchHalfHeight = 55.0f;

const TCHAR* ToolName(ETorqueTool T)
{
	switch (T)
	{
	case ETorqueTool::Wrench: return TEXT("Wrench");
	case ETorqueTool::Brush: return TEXT("Brush");
	case ETorqueTool::Repair: return TEXT("Grinder and welder");
	case ETorqueTool::Lance: return TEXT("Pressure washer");
	case ETorqueTool::Gun: return TEXT("Spray gun");
	default: return TEXT("Hands");
	}
}

// What the spray gun can be loaded with (walker.gd spray_loads): each paint finish (in the colour
// picked with R), then its protective coats, grey primer and the mixing bay's cup. (The spray can,
// now gone, had only primer and clear lacquer.)
struct FLoad
{
	enum EKind : uint8 { Paint, Coat, Primer, Cup };
	EKind Kind = Paint;
	FName Id;
};

TArray<FLoad> Loads(ETorqueTool T)
{
	TArray<FLoad> Out;
	for (const FName& F : UTorqueBuildSubsystem::Finishes()) Out.Add({FLoad::Paint, F});
	(void)T;   // only the spray gun sprays now
	for (const FName& C : UTorqueBuildSubsystem::Coats()) Out.Add({FLoad::Coat, C});
	Out.Add({FLoad::Primer, FName(TEXT("primer"))});
	Out.Add({FLoad::Cup, UTorqueBuildSubsystem::CupFinish()});   // paint mixed in the mixing bay
	return Out;
}

bool Sprays(ETorqueTool T) { return T == ETorqueTool::Gun; }

// Where a line from Start to End first enters a mesh's box (0..1 along it), in the mesh's own
// frame so the box turns with it. The bike's meshes have no collision; their boxes are close
// enough to point at a silencer or a handlebar.
bool HitMeshBox(const UStaticMeshComponent* C, const FVector& Start, const FVector& End, double& OutTime, double& OutVolume)
{
	const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
	if (!Mesh || !C->IsVisible()) return false;
	const FBox Box = Mesh->GetBoundingBox();
	const FTransform& T = C->GetComponentTransform();
	const FVector A = T.InverseTransformPosition(Start), B = T.InverseTransformPosition(End);
	FVector HitAt, HitNormal;
	float Time = 0.0f;
	if (Box.IsInside(A)) Time = 0.0f;
	else if (!FMath::LineExtentBoxIntersection(Box, A, B, FVector::ZeroVector, HitAt, HitNormal, Time)) return false;
	OutTime = Time;
	const FVector Size = Box.GetSize() * T.GetScale3D().GetAbs();
	OutVolume = FMath::Max(Size.X * Size.Y * Size.Z, 1.0);
	return true;
}

bool HitWorldBox(const FBox& Box, const FVector& Start, const FVector& End, double& OutTime, double& OutVolume)
{
	FVector HitAt, HitNormal;
	float Time = 0.0f;
	if (Box.IsInside(Start)) Time = 0.0f;
	else if (!FMath::LineExtentBoxIntersection(Box, Start, End, FVector::ZeroVector, HitAt, HitNormal, Time)) return false;
	OutTime = Time;
	const FVector Size = Box.GetSize();
	OutVolume = FMath::Max(Size.X * Size.Y * Size.Z, 1.0);
	return true;
}

bool IsSpotKind(ETorquePlace K)
{
	return K == ETorquePlace::Bench || K == ETorquePlace::Repair || K == ETorquePlace::Washer || K == ETorquePlace::Changer || K == ETorquePlace::Stand ||
		K == ETorquePlace::Hook;
}

FString PlaceName(const FTorquePlace& P, const UTorqueBuildSubsystem* B)
{
	switch (P.Kind)
	{
	case ETorquePlace::Bench: return TEXT("Workbench");
	case ETorquePlace::Repair: return TEXT("Repair bench (grinder and welder)");
	case ETorquePlace::Washer: return TEXT("Parts washer");
	case ETorquePlace::Changer: return TEXT("Tyre changer");
	case ETorquePlace::Stand: return TEXT("Engine stand");
	case ETorquePlace::ShedSwitch:   // [kit]
		if (P.Index == 0) return (B && B->ShedLightsOn()) ? TEXT("Shed lights (on)") : TEXT("Shed lights (off)");
		return (B && B->DoorUp()) ? TEXT("Roller door (up)") : TEXT("Roller door (down)");
	case ETorquePlace::Extinguisher:
		return (B && B->ExtinguisherHeldBy() != 0) ? TEXT("Fire extinguisher's place (someone has it)") : TEXT("Fire extinguisher");
	case ETorquePlace::Pump:   // [fluids]
		if (B && B->PumpSale() > 0.0f) return FString::Printf(TEXT("Petrol pump (unleaded, last sale %.2f L)"), B->PumpSale());
		return TEXT("Petrol pump (unleaded)");
	case ETorquePlace::Bin: return TorqueShedParts::BinName(P.Name);
	case ETorquePlace::Computer: return TEXT("Parts computer");
	case ETorquePlace::DynoStrap:
		if (B) return FString::Printf(TEXT("Strap post (%s)"), B->DynoRoom().Straps[FMath::Clamp(P.Index, 0, 3)] ? TEXT("done up") : TEXT("undone"));
		return TEXT("Strap post");
	case ETorquePlace::DynoDesk:
		if (B && B->DynoBay() != INDEX_NONE) return FString::Printf(TEXT("Dyno desk: %s on the rig"), *B->Bike(B->DynoBay()).Name);
		return TEXT("Dyno desk");
	case ETorquePlace::DynoSwitch:
		if (P.Index == 0) return (B && B->DynoRoom().bLights) ? TEXT("Dyno room lights (on)") : TEXT("Dyno room lights (off)");
		return FString::Printf(TEXT("Cooling fan (%s, engine at %.0f C)"), (B && B->DynoRoom().bFan) ? TEXT("on") : TEXT("off"),
			TorqueShedPower::DynoTemp(B ? B->DynoRoom().Heat : 0.0f));
	case ETorquePlace::Hook: return FString::Printf(TEXT("Spray bay hook %d"), P.Index + 1);
	case ETorquePlace::Radio: return TEXT("Shed radio (everyone hears it)");
	case ETorquePlace::Tv:   // [tv]
		return FString::Printf(TEXT("TV: %s"), TorqueShedTv::Name(B ? B->TvChannel(P.Index) : 0));
	case ETorquePlace::Tape: return TEXT("Masking tape");
	case ETorquePlace::Lift:
		return FString::Printf(TEXT("%s (%s)"), *UTorqueBuildSubsystem::LiftName(P.Index), (B && B->LiftUp(P.Index)) ? TEXT("up") : TEXT("down"));
	case ETorquePlace::PaintBench:
		if (B && B->PaintBay().BenchBy != 0 && B->PaintBay().BenchBy != B->Me()) return TEXT("Mixing bench (someone's at it)");
		return TEXT("Mixing bench");
	case ETorquePlace::PaintShelf:
		if (B)
			if (const FTorqueTin* Tin = B->PaintBay().Tins.Find(B->PaintBay().TinInSlot(P.Index)))
				return TEXT("Tin shelf: ") + B->PaintBay().TinText(*Tin);
		return FString::Printf(TEXT("Tin shelf (place %d, empty)"), P.Index + 1);
	case ETorquePlace::Bay:
		if (B && B->Exists(P.Index)) return FString::Printf(TEXT("Bay %d: %s"), P.Index + 1, *B->Bike(P.Index).Name);
		return FString::Printf(TEXT("Bay %d (empty)"), P.Index + 1);
	default: return FString();
	}
}

FString PutVerb(ETorquePlace K)
{
	switch (K)
	{
	case ETorquePlace::Repair: return TEXT("put it on the repair bench");
	case ETorquePlace::Washer: return TEXT("put it in the parts washer");
	case ETorquePlace::Changer: return TEXT("put it on the tyre changer");
	case ETorquePlace::Stand: return TEXT("put it on the engine stand");
	case ETorquePlace::Hook: return TEXT("hang it on the hook");
	default: return TEXT("put it on the workbench");
	}
}

FString Lower(const FString& S) { return S.ToLower(); }

// "front wheel, rear brake caliper"
FString SlotList(const TArray<FName>& Slots, int32 Most = 4)
{
	FString S;
	for (int32 I = 0; I < Slots.Num() && I < Most; ++I)
		S += (I ? TEXT(", ") : TEXT("")) + TorqueShedParts::SlotName(Slots[I]).ToLower();
	if (Slots.Num() > Most) S += FString::Printf(TEXT(" and %d more"), Slots.Num() - Most);
	return S;
}
// Hold F this long on a bike to wheel it instead of riding it (walker.gd HOLD_TO_PUSH).
constexpr double HoldToPush = 0.35;
// Carrying a part (walker.gd CARRY_STEP, CARRY_MIN, CARRY_MAX, TURN_HOLD, TURN_RATE): a click of the
// wheel moves where it goes down this far out or in, between these of arm's length; R held this
// long turns it freely, this fast (degrees a second); a tap turns it a quarter.
constexpr double CarryStep = 25.0;
constexpr double CarryMin = -15.0;
constexpr double CarryMax = 300.0;
constexpr double TurnHold = 0.2;
constexpr double TurnRate = 90.0;
} // namespace TorqueWalkerImpl

namespace TWI = TorqueWalkerImpl;

ATorqueWalker::ATorqueWalker()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	// An ordinary person: 1.8 m capsule, eyes at 1.62 m (0.95 m crouched, in a 1.1 m capsule).
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 90.0f);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0, 0.0, TWI::StandEye));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(80.0f);
	BaseEyeHeight = (float)TWI::StandEye;
	CrouchedEyeHeight = (float)TWI::CrouchEye;

	// A shape body so other players in a co-op session can see you: legs, a torso in a work
	// jacket, arms, a head and a cap peak that shows which way you face. You don't see your own.
	// Others see it in the rider's kit and your colour (Coop/TorqueLook.h).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = Block.Object;
	// The spray's sprites are drawn on the engine's plane (held here so the packaged game has it).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Flat(TEXT("/Engine/BasicShapes/Plane.Plane"));
	PlaneMesh = Flat.Object;
	struct FBit { const TCHAR* Name; int32 Shape; FVector At, Size; };   // capsule-centre cm; 0 cylinder, 1 sphere, 2 cube
	const FBit Bits[] = {
		{TEXT("Body_LegL"), 0, FVector(0, -10, -45), FVector(15, 15, 88)},
		{TEXT("Body_LegR"), 0, FVector(0, 10, -45), FVector(15, 15, 88)},
		{TEXT("Body_Torso"), 0, FVector(0, 0, 25), FVector(26, 40, 62)},
		{TEXT("Body_ArmL"), 0, FVector(0, -25, 22), FVector(10, 10, 60)},
		{TEXT("Body_ArmR"), 0, FVector(0, 25, 22), FVector(10, 10, 60)},
		{TEXT("Body_Head"), 1, FVector(0, 0, 70), FVector(21, 19, 24)},
		{TEXT("Body_Cap"), 1, FVector(0, 0, 77), FVector(23, 21, 13)},       // the crown, over the top of the head
		{TEXT("Body_CapPeak"), 2, FVector(14, 0, 76), FVector(11, 17, 1.5)},  // the peak, out of the crown's front
	};
	for (const FBit& Bit : Bits)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Bit.Name);
		C->SetupAttachment(GetCapsuleComponent());
		C->SetStaticMesh(Bit.Shape == 0 ? Cyl.Object : Bit.Shape == 1 ? Ball.Object : Block.Object);
		C->SetRelativeLocation(Bit.At);
		C->SetRelativeScale3D(Bit.Size / 100.0);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetOwnerNoSee(true);
		C->SetCanEverAffectNavigation(false);
	}

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	UCharacterMovementComponent* Moves = GetCharacterMovement();
	Moves->MaxWalkSpeed = WalkSpeed;
	Moves->MaxWalkSpeedCrouched = CrouchSpeed;
	// [co-op] Each player's own machine moves their walker, as it rides their bike: the host takes a
	// joined player's place as their machine sends it, never pulling them back. (Running is a speed
	// only their machine knows, and a bike lift's platform, made on each machine, can't be named over
	// the net as what they stand on: the host corrected both, and a friend's run stuttered, build 135.)
	Moves->bIgnoreClientMovementErrorChecksAndCorrection = true;
	Moves->bServerAcceptClientAuthoritativePosition = true;
	Moves->GetNavAgentPropertiesRef().bCanCrouch = true;
	Moves->SetCrouchedHalfHeight(TWI::CrouchHalfHeight);
	Moves->MaxAcceleration = 1800.0f;
	Moves->BrakingDecelerationWalking = 2400.0f;
	Moves->GroundFriction = 10.0f;
	// A hop of about 0.45 m (sqrt(2 g h)): not enough to get up on the bench.
	Moves->JumpZVelocity = 297.0f;
	Moves->AirControl = 0.15f;
}

ATorqueWalker::~ATorqueWalker() = default;

UTorqueBuildSubsystem* ATorqueWalker::Shed() const { return UTorqueBuildSubsystem::Of(this); }

void ATorqueWalker::BeginPlay()
{
	Super::BeginPlay();
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueProfile"))) { bPerfShow = true; bPerfLog = true; }
	if (GetNetMode() != NM_DedicatedServer) TorqueToolModels::Warm();   // [hands] the tools' meshes built now, not as you take one up
	// The loose parts are drawn by one ATorqueShedItems; a level built before it existed has none.
	if (UWorld* World = GetWorld())
	{
		bool bHave = false;
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) bHave = true;
		if (!bHave) World->SpawnActor<ATorqueShedItems>(FVector::ZeroVector, FRotator::ZeroRotator);
		// And the screens: each bay's page and the crew board (Torque Shed's TVs).
		bool bScreens = false;
		for (TActorIterator<ATorqueShedScreens> It(World); It; ++It) bScreens = true;
		if (!bScreens) World->SpawnActor<ATorqueShedScreens>(FVector::ZeroVector, FRotator::ZeroRotator);
		// And the dyno room's moving kit: the drum, the fan, the straps, the lights.
		bool bDyno = false;
		for (TActorIterator<ATorqueDynoRoom> It(World); It; ++It) bDyno = true;
		if (!bDyno) World->SpawnActor<ATorqueDynoRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
		// And the paint mixing bay's tins, bottles, swatches and scale.
		bool bPaintBay = false;
		for (TActorIterator<ATorquePaintBayRoom> It(World); It; ++It) bPaintBay = true;
		if (!bPaintBay) World->SpawnActor<ATorquePaintBayRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
		// And the spray booth's hooks on their wires, raised and lowered, swinging.
		bool bHooks = false;
		for (TActorIterator<ATorqueBoothHooks> It(World); It; ++It) bHooks = true;
		if (!bHooks) World->SpawnActor<ATorqueBoothHooks>(FVector::ZeroVector, FRotator::ZeroRotator);
		// And the radio on its shelf over the workbench.
		bool bRadio = false;
		for (TActorIterator<ATorqueShedRadio> It(World); It; ++It) bRadio = true;
		if (!bRadio) World->SpawnActor<ATorqueShedRadio>(TorqueShedRadio::At, FRotator::ZeroRotator);
		// And the bike lifts: one under each bay's stand, and the wash bay's.
		ATorqueBikeLift::SpawnMissing(World);
		// [kit] And the roller door, the main lights, the rolling engine stand and the extinguisher.
		bool bKit = false;
		for (TActorIterator<ATorqueShedKit> It(World); It; ++It) bKit = true;
		if (!bKit) World->SpawnActor<ATorqueShedKit>(FVector::ZeroVector, FRotator::ZeroRotator);
		// [furniture] And the movable shed and yard kit, the yard's built-in pieces and its markings.
		ATorqueFurnishings::SpawnMissing(World);
		// [fluids] And the petrol pump on the front wall outside, its nozzle and hose.
		ATorqueShedPump::SpawnMissing(World);
	}
	if (UTorqueBuildSubsystem* B = Shed()) TellHandle = B->OnTell.AddUObject(this, &ATorqueWalker::Told);
}

void ATorqueWalker::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = Shed()) B->OnTell.Remove(TellHandle);
	Pad.Reset();   // stops its reading thread
	if (Avatar) Avatar->Destroy();   // its MetaHuman Blueprint goes with it
	LookOutline.Clear();
	if (WashJob.IsValid()) WashJob->Detach();   // the bike's own grime back   [wash]
	WashJob.Reset();
	Super::EndPlay(Reason);
}

FVector ATorqueWalker::EyeSpot() const
{
	// [hands] Your body's eyes (first person), else Torque Shed's eye height; sat down, the seat's.
	if (SeatId != 0) return SeatEye;
	if (bFirstEye && IsLocallyControlled()) return FirstEye;
	return FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye);
}

void ATorqueWalker::UpdateAvatar(float Dt)
{
	// A MetaHuman per player colour, when Data/Avatars/avatars.json lists some (Tools/ue_metahumans.py).
	// [hands] Yours too, seen from inside it (unless -TorqueNoBody).
	static const bool bNoBody = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoBody"));
	const bool bMine = IsLocallyControlled();
	if (bMine && bNoBody) { bFirstEye = false; return; }
	if (FTorqueAvatar::Count() == 0 || GetNetMode() == NM_DedicatedServer) { bFirstEye = false; return; }
	const ATorquePlayerState* PS = Cast<ATorquePlayerState>(GetPlayerState());
	if (!PS) return;
	// Who they are: their Profile page's choice (Shed/TorqueProfile.h), or the one for their colour.
	const FTorqueProfile Look = FTorqueProfile::Unpack(PS->Profile);
	const int32 Want = Look.AvatarFor(PS->Colour);
	if (AvatarFor != Want)
	{
		AvatarFor = Want;
		AvatarLook.Reset();
		if (!AvatarRoot)
		{
			AvatarRoot = NewObject<USceneComponent>(this, TEXT("AvatarRoot"));
			AvatarRoot->SetupAttachment(GetCapsuleComponent());
			AvatarRoot->RegisterComponent();
		}
		if (!Avatar) Avatar = MakeUnique<FTorqueAvatar>();
		const bool bMade = Avatar->Build(this, AvatarRoot, Want);
		if (bMade) Avatar->SetOwnerView(bMine);   // [hands] your own face and hair out of your view
		bFirstEye = false;
		// The shape body goes while the MetaHuman stands in for it.
		TArray<UStaticMeshComponent*> Shapes;
		GetComponents(Shapes);
		for (UStaticMeshComponent* C : Shapes)
			if (C->GetName().StartsWith(TEXT("Body_"))) C->SetVisibility(!bMade);
	}
	if (!Avatar || !Avatar->IsValid()) { bFirstEye = false; return; }
	if (AvatarLook != PS->Profile)   // their height and face
	{
		AvatarLook = PS->Profile;
		Avatar->SetLook(Look);
	}
	// Feet on the floor: the capsule's bottom (it shrinks and drops when crouched). [hands] Your own a
	// few cm back, so looking down you see your chest and feet, not the inside of your collar.
	AvatarRoot->SetRelativeLocation(FVector(bMine ? -TWI::BodyBack : 0.0, 0.0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	FTorqueAvatarInput In;
	In.Velocity = GetVelocity();
	In.Facing = GetActorRotation();
	In.LookPitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
	In.Crouch = bIsCrouched ? 1.0 : 0.0;
	In.bInAir = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
	// Their hands (FollowHands, from FTorqueHandsNet): a part carried in both hands; a tool in the
	// right, the hand on its grip; both hands forward working with bare hands.
	const UTorqueBuildSubsystem* B = Shed();
	In.bCarrying = B && B->HeldBy(PeerOf()) != 0;
	In.bToolInHand = !In.bCarrying && Tool != ETorqueTool::Hands && ToolHolder && ToolHolder->IsVisible();
	if (In.bToolInHand) In.ToolGrip = ToolHolder->GetComponentLocation();
	In.bWorking = Tool == ETorqueTool::Hands && WorkBlend > 0.2;
	// [hands] The hands round the tool's handles (Data/Tools/tools.json grips), fingers on its
	// trigger pulled as you work.
	const FTransform HolderAt = ToolHolder ? ToolHolder->GetComponentTransform() : FTransform::Identity;
	TorqueToolModels::FGrip Grips[2];
	for (int32 S = 0; S < 2; ++S)
	{
		if (!In.bToolInHand || !TorqueToolModels::GripOf((int32)Tool, S, Grips[S])) continue;
		FTorqueHandGrip& G = In.Grip[S];
		G.bOn = true;
		G.At = HolderAt.TransformPosition(Grips[S].At);
		G.Axis = HolderAt.TransformVectorNoScale(Grips[S].Axis);
		G.Aim = HolderAt.TransformVectorNoScale(Grips[S].Aim);
		G.Radius = Grips[S].Radius * HolderAt.GetScale3D().X;
		G.TriggerFingers = Grips[S].Trigger;
		G.TriggerPull = WorkBlend;
		G.bIndexTop = Grips[S].bIndexTop;
	}
	Avatar->Pose(In, Dt);
	// The tool follows the right hand when the arm couldn't reach where the view holds it.
	FVector Reached;
	if (In.Grip[1].bOn && ToolHolder && Avatar->GripReached(1, Reached))
	{
		const FVector Short = Reached - In.Grip[1].At;
		if (Short.SizeSquared() > 0.01) ToolHolder->SetWorldLocation(ToolHolder->GetComponentLocation() + Short);
		// Once per tool, a second in: how far short of the view's hold the hand came (0 = in reach).
		HandsHeldFor = (int32)Tool == HandsLogged ? HandsHeldFor : HandsHeldFor + Dt;
		if (bMine && (int32)Tool != HandsLogged && HandsHeldFor > 1.0)
		{
			HandsLogged = (int32)Tool;
			HandsHeldFor = 0.0;
			FVector LeftAt;
			const bool bLeft = In.Grip[0].bOn && Avatar->GripReached(0, LeftAt);
			UE_LOG(LogTemp, Log, TEXT("Torque: hands on tool %d: right hand %.1f cm short of the hold%s, eye %s"), (int32)Tool, Short.Size(),
				bLeft ? *FString::Printf(TEXT(", left hand %.1f cm short"), (LeftAt - In.Grip[0].At).Size()) : TEXT(""), *FirstEye.ToCompactString());
		}
	}
	else HandsHeldFor = 0.0;
	// Your camera at your body's eyes (from this pose, for the next frame), eased so a step's bob is soft.
	FVector Eye;
	if (bMine && Avatar->EyeWorld(Eye))
	{
		FVector EyeWant = GetCapsuleComponent()->GetComponentTransform().InverseTransformPosition(Eye) + FVector(TWI::BodyBack, 0.0, 0.0);
		const double EyeBase = bIsCrouched ? TWI::CrouchEye : TWI::StandEye;
		EyeWant = FVector(FMath::Clamp(EyeWant.X, -20.0, 25.0), FMath::Clamp(EyeWant.Y, -15.0, 15.0), FMath::Clamp(EyeWant.Z, EyeBase - 25.0, EyeBase + 20.0));
		FirstEye = bFirstEye ? FMath::VInterpTo(FirstEye, EyeWant, (double)Dt, 14.0) : EyeWant;
		bFirstEye = true;
	}
}

void ATorqueWalker::Told(int32 Peer, const FString& Text, bool bOk)
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!IsLocallyControlled() || !B || Peer != B->Me()) return;
	Say(Text, bOk);
}

void ATorqueWalker::Say(const FString& Text, bool bOk, double For)
{
	Note = Text;
	bNoteOk = bOk;
	NoteFor = For;
}

void ATorqueWalker::Doing(const FString& What, double For)
{
	DoingFor = For;
	if (UTorqueBuildSubsystem* B = Shed()) B->ReportActivity(What);
}

void ATorqueWalker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// [co-op] Another player: dressed in the rider's kit and their colour (TorqueLook.h), their
	// tool in hand and moving as they work (FollowHands).
	if (!IsLocallyControlled())
	{
		// (Their hands: FollowHands puts their tool in front of them, and the avatar's hands take
		// it there, from UpdateToolView.)
		FollowHands(DeltaSeconds);
		if (!Avatar || !Avatar->IsValid()) TorqueLook::Dress(this);
		return;
	}
	UTorqueBuildSubsystem* B = Shed();
	// -TorqueAvatarDemo: the MetaHuman avatars walking about 3 m in front of you (TorqueAvatarDemo.h).
	if (!bAvatarDemo && FParse::Param(FCommandLine::Get(), TEXT("TorqueAvatarDemo")))
	{
		bAvatarDemo = true;
		if (UWorld* World = GetWorld())
		{
			const FRotator Ahead(0.0, GetActorRotation().Yaw, 0.0);
			const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			World->SpawnActor<ATorqueAvatarDemo>(Feet + Ahead.Vector() * 300.0, Ahead);
		}
	}
	// This machine's player, as the shed knows them (whose hands a part is in).
	if (!bPeerSet && B && GetPlayerState())
	{
		B->SetLocalPeer(GetPlayerState()->GetPlayerId());
		bPeerSet = true;
	}
	Camera->SetRelativeLocation(EyeSpot());   // [hands] at your body's eyes when it's there
	UpdateRoomExposure(DeltaSeconds);
	if (bRidingOut)
	{
		RideOutIn -= DeltaSeconds;
		if (RideOutIn <= 0.0)
		{
			bRidingOut = false;
			TorqueFlow::RideOut(this);
		}
		UpdateHud();
		return;
	}
	// Geometry mode is open (F2, ATorqueGeometryMode): it has the keys, the pad and the screen.
	if (ATorqueGeometryMode::For(GetController()))
	{
		bWorkHeld = bAltHeld = bWorkPressed = bAltPressed = false;
		const FSonyPadState Now = PadState();   // (what's held as it closes isn't pressed again here)
		PrevPadButtons = Now.bConnected ? Now.Buttons : 0;
		return;
	}
	UpdatePerf(DeltaSeconds);
	bWorkPressed = bAltPressed = false;
	// Hands-off test runs (-TorqueNoInput) ignore the pad, keys and mouse, as on the bike.
	static const bool bNoInput = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"));
	if (!bNoInput) ReadInput(DeltaSeconds);
	else bWorkHeld = bAltHeld = false;
	TickTestWalk(DeltaSeconds);   // (the test runner's walkto)
	TickSprays(DeltaSeconds);   // the coats going on (and drying off) follow their parts as they're redrawn
	TickWash(DeltaSeconds);     // the wash game's muck follows its bike (TorqueWalkerWash.cpp)
	if (Wheeling() != INDEX_NONE) PushBike(DeltaSeconds);
	else if (SeatId != 0)
	{
		// Sat down: up again when the seat's taken hold of or gone.
		const FTorqueFurniture* Seat = B ? B->FurnitureById(SeatId) : nullptr;
		if (!Seat || Seat->Mover != 0) StandUp();
	}
	else if (!bComputer && !bBench)
	{
		FindTarget();
		MakePlan();
		if (!SprayWork(DeltaSeconds) && !WashWork(DeltaSeconds) && !TyreWork(DeltaSeconds)) Work(DeltaSeconds);   // the can or gun on a part: the spray game; the lance at a bike: the wash game; a tyre on the changer: the tyre game
		HookWork(DeltaSeconds);
	}
	UpdateSway(DeltaSeconds);
	UpdateToolView(DeltaSeconds);
	UpdateCues();
	ShareHands(DeltaSeconds);   // [co-op]
	ActHeldFor = FMath::Max(ActHeldFor - DeltaSeconds, 0.0);
	NoteFor = FMath::Max(NoteFor - DeltaSeconds, 0.0);
	if (DoingFor > 0.0)
	{
		DoingFor -= DeltaSeconds;
		if (DoingFor <= 0.0) Doing(FString(), 0.0);
	}
	UpdateHud();
}

// --- Input ------------------------------------------------------------------------------------------

FSonyPadState ATorqueWalker::PadState() const
{
	return Pad ? Pad->Get() : FSonyPadState();
}

void ATorqueWalker::ReadInput(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	bWorkHeld = bAltHeld = false;
	if (!PC) return;
	if (!bInputReady)
	{
		// Only the player's own walker reads the pad (one reading thread), and takes the mouse.
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		Pad = MakeUnique<FSonyPad>();
		bInputReady = true;
	}
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	auto Axis = [PC](const FKey& K) { return PC->GetInputAnalogKeyState(K); };
	auto Dead = [](float V) { return FMath::Abs(V) < 0.15f ? 0.0f : V; };

	const FSonyPadState S = Pad ? Pad->Get() : FSonyPadState();
	const uint32 SonyDown = S.bConnected ? S.Buttons : 0;
	const uint32 SonyHit = SonyDown & ~PrevPadButtons;
	PrevPadButtons = SonyDown;
	// Settings > Gameplay > Field of view (the walker's own is the setting).
	if (Camera) Camera->SetFieldOfView(FTorqueSettings::Get().FovFor(80.0f));
	// The pause or settings menu is up (co-op, ATorquePlayerController): stand still, leave the keys to it.
	if (ATorquePlayerController::MenuUp(PC)) return;
	// Settings > Controller > Key bindings: the keyboard and mouse as the player bound them.
	auto KeyDown = [PC](ETorqueKey A) { return FTorqueKeys::Down(PC, A); };
	auto KeyJust = [PC](ETorqueKey A) { return FTorqueKeys::Just(PC, A); };
	// [label] The shelf label box is up: the keys type its label till Enter or Esc.
	if (LabelPiece != 0)
	{
		LabelInput(PC, SonyHit);
		return;
	}
	// N on a shelving unit: write on its label plate (Torque Shed's label_dialog.gd).
	if (KeyJust(ETorqueKey::LabelShelf) && TargetFurniture != 0 && !bComputer && !bBench && Wheeling() == INDEX_NONE && OpenLabel(TargetFurniture))
		return;
	// F2: geometry mode on the bike looked at, or the nearest (main.gd's F2).
	if (KeyJust(ETorqueKey::GeometryMode) && !bComputer && !bBench && Wheeling() == INDEX_NONE)
	{
		FString Why;
		if (!ATorqueGeometryMode::Open(this, Target == ETorqueLook::BikePart ? TargetBay : INDEX_NONE, Why))
			Say(Why.IsEmpty() ? FString(TEXT("Geometry mode couldn't open here.")) : Why, false);
		return;
	}
	// [ping] Middle mouse (D-pad left on a pad, as nothing else on foot has it): mark what's looked at
	// for the whole crew (walker.gd's "ping").
	if (!bComputer && !bBench && (KeyJust(ETorqueKey::Ping) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft)))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque ping: ping pressed on foot"));
		Ping();
	}
	const FTorqueSettings& Set = FTorqueSettings::Get();   // Settings > Controller: look speeds, invert

	const bool bWorkJust = KeyJust(ETorqueKey::Work) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
	const bool bAltRaw = KeyJust(ETorqueKey::AltWork) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square);
	// [tv] Right mouse (R, Square) at a wall TV changes its channel, and does nothing else.
	const bool bTvClick = bAltRaw && ((Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Tv) || TvPiece() != nullptr);
	if (bTvClick) NextTvChannel();
	// [tv] Ctrl+V at a TV: the YouTube link copied puts it on YouTube with that video, for everyone.
	const bool bAtTv = (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Tv) || TvPiece() != nullptr;
	if (bAtTv && Just(EKeys::V) && (Down(EKeys::LeftControl) || Down(EKeys::RightControl))) PasteTvVideo();
	const bool bAltJust = bAltRaw && !bTvClick;
	const bool bF = KeyJust(ETorqueKey::Use) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle);
	const bool bNext = KeyJust(ETorqueKey::NextTool) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1);
	const bool bPrev = KeyJust(ETorqueKey::PrevTool) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1);
	const bool bBack = KeyJust(ETorqueKey::Jump) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle);

	if (bComputer && bDesk)
	{
		// At the dyno desk (tuning_computer.gd): the mouse drags its sliders and clicks its buttons,
		// the wheel scrolls the settings. Keys and pad: Q / E (L1 / R1) the tabs, W / S (up / down) a
		// setting, A / D (left / right) moves it, R (Square) resets it, Enter (Cross) runs the dyno.
		// F, Esc or Circle / B walks away.
		FVector2D Mouse(-1.0, -1.0);
		float Mx = 0.0f, My = 0.0f;
		if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
		if (bF || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle))
		{
			CloseComputer();
			return;
		}
		if (Just(EKeys::LeftMouseButton))
		{
			DeskDo(Desk.Click(Mouse));
			if (!bComputer) return;
		}
		DeskDo(Desk.Hold(Mouse, Down(EKeys::LeftMouseButton)));
		if (Just(EKeys::MouseScrollDown)) Desk.Scroll(1);
		if (Just(EKeys::MouseScrollUp)) Desk.Scroll(-1);
		int32 Step = 0, Move = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Step = 1;
		if (Just(EKeys::A) || Just(EKeys::Left) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft)) Move = -1;
		if (Just(EKeys::D) || Just(EKeys::Right) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight)) Move = 1;
		const bool bPadNext = Just(EKeys::E) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1);
		const bool bPadPrev = Just(EKeys::Q) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1);
		const bool bReset = Just(EKeys::R) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square);
		const bool bRun = Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
		if (Step != 0 || Move != 0 || bPadNext || bPadPrev || bReset || bRun)
			DeskDo(Desk.Pad(Step, Move, bPadNext ? 1 : (bPadPrev ? -1 : 0), bReset, bRun));
		return;
	}
	if (bComputer)
	{
		// At the parts computer (parts_computer.gd): the mouse clicks its buttons and the wheel scrolls
		// the list under it; on a pad the D-pad picks a model, L1 / R1 the kind, Cross orders. F, Esc or
		// Circle / B walks away.
		FVector2D Mouse(-1.0, -1.0);
		float Mx = 0.0f, My = 0.0f;
		if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
		// Typing the New bike's brand name: the keys are letters (F, W, S, Q and E too) till Enter,
		// Tab or Esc; the mouse still works the page.
		if (Computer.Typing())
		{
			FTorqueBikeConfigurator& Cfg = Computer.NewBike();
			if (Just(EKeys::Enter) || Just(EKeys::Tab) || Just(EKeys::Escape)) Cfg.StopTyping();
			else
			{
				ATorquePlayerController::TypeText(PC, Cfg.TypingText(), 64);
				Cfg.Typed();
			}
			Computer.Hold(Mouse, Down(EKeys::LeftMouseButton));
			if (Just(EKeys::LeftMouseButton)) ComputerDo(Computer.Click(Mouse));
			return;
		}
		if (bF || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle))
		{
			// (A list open on the New bike page closes first.)
			if (!bF && Computer.Back()) return;
			CloseComputer();
			return;
		}
		// The 3D preview: dragging turns it, a right-click on it stops or starts it turning.
		Computer.Hold(Mouse, Down(EKeys::LeftMouseButton));
		if (Just(EKeys::RightMouseButton)) Computer.RightClick(Mouse);
		if (Just(EKeys::LeftMouseButton))
		{
			ComputerDo(Computer.Click(Mouse));
			return;
		}
		if (Just(EKeys::MouseScrollDown)) Computer.Scroll(Mouse, 1);
		if (Just(EKeys::MouseScrollUp)) Computer.Scroll(Mouse, -1);
		int32 Step = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Step = 1;
		const bool bPadNext = Just(EKeys::E) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1);
		const bool bPadPrev = Just(EKeys::Q) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1);
		const bool bChoose = Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
		ComputerDo(Computer.Pad(Step, bPadNext ? 1 : (bPadPrev ? -1 : 0), bChoose));
		return;
	}
	if (bBench)
	{
		// At the mixing bench (paint_bench.gd): the card down the right is worked with the mouse; hold
		// the left mouse on a tint (or in the view, with one picked) to pour it, dragging down to tip the
		// bottle further; hold the right mouse in the view and stir round. On a pad the D-pad goes down
		// the card, L1 / R1 the pages, Cross pours or presses, Square stirs. F, Esc or Space (Circle / B)
		// steps back. Naming a mix, the keys type its name.
		if (bNaming)
		{
			BenchTyping(PC);
			if (!Just(EKeys::LeftMouseButton)) return;
			bNaming = false;   // a click elsewhere ends it (and does what it clicked)
		}
		int32 Step = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Step = 1;
		const bool bPadWork = Down(EKeys::Gamepad_FaceButton_Bottom) || (SonyDown & ESonyButton::Cross);
		const bool bPadChoose = Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross) || Just(EKeys::Enter);
		const bool bAltDown = Down(EKeys::R) || Down(EKeys::Gamepad_FaceButton_Left) || (SonyDown & ESonyButton::Square);
		const bool bPageNext = Just(EKeys::E) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1);
		const bool bPagePrev = Just(EKeys::Q) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1);
		BenchMouse(PC, Dt);
		if (!bBench) return;
		BenchInput(Dt, Step, bPageNext ? 1 : (bPagePrev ? -1 : 0), bPadChoose, bPadWork || bMousePour, bAltDown, bF || bBack || Just(EKeys::Escape));
		return;
	}

	// Walking: keys, then whichever stick is pushed further.
	float Fwd = (KeyDown(ETorqueKey::WalkForward) ? 1.0f : 0.0f) - (KeyDown(ETorqueKey::WalkBack) ? 1.0f : 0.0f);
	float Side = (KeyDown(ETorqueKey::WalkRight) ? 1.0f : 0.0f) - (KeyDown(ETorqueKey::WalkLeft) ? 1.0f : 0.0f);
	const float PadFwd = S.bConnected ? Dead(S.LY) : Dead(Axis(EKeys::Gamepad_LeftY));
	const float PadSide = S.bConnected ? Dead(S.LX) : Dead(Axis(EKeys::Gamepad_LeftX));
	if (FMath::Abs(PadFwd) > FMath::Abs(Fwd)) Fwd = PadFwd;
	if (FMath::Abs(PadSide) > FMath::Abs(Side)) Side = PadSide;
	if (SeatId != 0)
	{
		// Sat down: F, Space (Circle) or a step gets you up; the look still goes round.
		if (bF || bBack || FMath::Abs(Fwd) > 0.5f || FMath::Abs(Side) > 0.5f)
		{
			StandUp();
			return;
		}
		Fwd = Side = 0.0f;
	}
	const bool bRun = KeyDown(ETorqueKey::Run) || Down(EKeys::Gamepad_LeftThumbstick) || (SonyDown & ESonyButton::L3);
	GetCharacterMovement()->MaxWalkSpeed = bRun ? RunSpeed : WalkSpeed;
	if (SprayItem != 0) GetCharacterMovement()->MaxWalkSpeed *= (float)TorqueSpray::ModeWalk;   // careful steps with the spray controls up
	if (Wheeling() != INDEX_NONE) GetCharacterMovement()->MaxWalkSpeed = FMath::Min(GetCharacterMovement()->MaxWalkSpeed, 240.0f);   // a bike is heavy to wheel about
	const FRotator Yaw(0.0, PC->GetControlRotation().Yaw, 0.0);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Fwd);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Side);

	// Looking: the mouse, and the right stick.
	float Mx = 0.0f, My = 0.0f;
	PC->GetInputMouseDelta(Mx, My);
	const float LookX = S.bConnected ? Dead(S.RX) : Dead(Axis(EKeys::Gamepad_RightX));
	const float LookY = S.bConnected ? Dead(S.RY) : Dead(Axis(EKeys::Gamepad_RightY));
	if (TyreGame.IsValid())
	{
		// [tyre] The tyre game: the mouse and the right stick work the tyre lever, not the look.
		TyreGame->Move(FVector2D(Mx, -My) * Set.MouseLook);
		TyreGame->Aim(FVector2D(LookX, -LookY), Dt);
	}
	else
	{
		FRotator R = PC->GetControlRotation();
		const float Invert = Set.bInvertLook ? -1.0f : 1.0f;
		const float Steady = SprayItem != 0 ? (float)TorqueSpray::ModeLook : 1.0f;   // the spray controls: a slower, steadier look
		R.Yaw += Steady * (Mx * MouseDegPerPixel * Set.MouseLook + LookX * PadDegPerSecond * Set.PadLook * Dt);
		R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + Invert * Steady * (My * MouseDegPerPixel * Set.MouseLook +
			LookY * PadDegPerSecond * Set.PadLook * 0.7f * Dt)), -85.0, 85.0);
		R.Roll = 0.0;
		PC->SetControlRotation(R);
	}

	if (bBack) Jump();
	if (SeatId == 0 && (KeyJust(ETorqueKey::Crouch) || Just(EKeys::Gamepad_RightThumbstick) || (SonyHit & ESonyButton::R3))) SetCrouched(!bCrouched);
	// The tools: Q / E, the wheel, L1 / R1, or 1-6 (along the tool bar). Carrying a part, your hands are full: the wheel
	// (Q / E, L1 / R1) moves where it goes down out or in, and R / Square turns it (CarryInput).
	const int32 Tools = TorqueToolBarCount;
	const UTorqueBuildSubsystem* CarryShed = Shed();
	const bool bCarryPart = CarryShed && CarryShed->HeldBy(CarryShed->Me()) != 0 && !CarryShed->PaintBay().HeldTin(CarryShed->Me())
		&& CarryShed->PaintBay().TapeBy != CarryShed->Me();
	const int32 MovingKit = CarryShed ? CarryShed->FurnitureMovedBy(CarryShed->Me()) : 0;
	if (MovingKit != 0)
	{
		// [furniture] Moving a piece of kit: the wheel (Q / E, L1 / R1) pushes it out or brings it in,
		// as a part in your hands (beenster, Nov 10).
		if (bNext || bPrev)
			if (UTorqueBuildSubsystem* KitShed = Shed())
			{
				FTorqueShedRequest ReachReq;
				ReachReq.Kind = ETorqueShedAct::TurnFurniture;
				ReachReq.Item = MovingKit;
				ReachReq.Spot = FName(TEXT("reach"));
				ReachReq.Amount = (float)(bNext ? TorqueFurniture::ReachStep : -TorqueFurniture::ReachStep);
				FString WhyNot;
				if (!KitShed->Request(ReachReq, WhyNot)) Say(WhyNot, false);
			}
	}
	else if (bCarryPart)
		CarryInput(Dt, bPrev, bNext, Down(EKeys::R) || Down(EKeys::Gamepad_FaceButton_Left) || (SonyDown & ESonyButton::Square),
			Down(EKeys::LeftShift) || Down(EKeys::RightShift));
	else
	{
		TurnKeyFor = -1.0;
		CarryFor = 0;
		HoldTurn = 0.0;
		if (SprayItem != 0 && SprayHook() != INDEX_NONE)
		{
			// The spray controls up on a part on a booth hook: Q / E, the wheel or L1 / R1 wind the hook
			// down and up (a click a step, held it keeps going), so you can get at it all without
			// putting the gun down. F puts the controls down to change tool.
			const bool bUpHeld = KeyDown(ETorqueKey::NextTool) || Down(EKeys::Gamepad_RightShoulder) || (SonyDown & ESonyButton::R1);
			const bool bDownHeld = KeyDown(ETorqueKey::PrevTool) || Down(EKeys::Gamepad_LeftShoulder) || (SonyDown & ESonyButton::L1);
			SprayWind(Dt, bNext ? 1 : (bPrev ? -1 : 0), bUpHeld ? 1 : (bDownHeld ? -1 : 0));
		}
		else
		{
			SprayWind(Dt, 0, 0);
			const int32 BarSlot = FMath::Max(ToolSlot(), 0);
			if (bNext) SetTool(TorqueToolBar[(BarSlot + 1) % Tools]);
			if (bPrev) SetTool(TorqueToolBar[(BarSlot + Tools - 1) % Tools]);
		}
	}
	static const ETorqueKey Numbers[] = {ETorqueKey::Tool1, ETorqueKey::Tool2, ETorqueKey::Tool3, ETorqueKey::Tool4, ETorqueKey::Tool5,
		ETorqueKey::Tool6};
	for (int32 I = 0; I < Tools && I < (int32)UE_ARRAY_COUNT(Numbers); ++I)
		if (KeyJust(Numbers[I])) SetTool(TorqueToolBar[I]);
	// The spray gun: R / right mouse / Square the colour, T the finish. Otherwise those are the
	// second button (lift a wheel off the tyre changer whole).
	const UTorqueBuildSubsystem* TinShed = Shed();
	if (TinShed && TinShed->PaintBay().HeldTin(TinShed->Me()))
	{
		// A tin from the mixing bay in your hands: R fills the spray gun's cup from it.
		bAltPressed = bAltHeld = false;
		if (bAltJust)
		{
			FTorqueShedRequest Cup;
			Cup.Kind = ETorqueShedAct::PaintCup;
			FString WhyNot;
			if (UTorqueBuildSubsystem* CupShed = Shed())
				if (!CupShed->Request(Cup, WhyNot)) Say(WhyNot, false);
		}
	}
	else if (TinShed && TinShed->PaintBay().TapeBy == TinShed->Me())
	{
		// The masking tape in your hands: R / Square picks the pattern, right mouse peels it off a part.
		if (KeyJust(ETorqueKey::AltWork) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square))
			if (UTorqueBuildSubsystem* TapeShed = Shed())
			{
				FTorqueShedRequest Pick;
				Pick.Kind = ETorqueShedAct::PaintTapePattern;
				Pick.Colour = TapeShed->PaintBay().TapePattern % TorquePaintMix::TapePatterns + 1;
				FString WhyNot;
				if (!TapeShed->Request(Pick, WhyNot)) Say(WhyNot, false);
				else Say(FString::Printf(TEXT("Masking tape: %s"), *FString(TorquePaintMix::PatternName(Pick.Colour)).ToLower()), true);
			}
		bAltPressed = Just(EKeys::RightMouseButton);
		bAltHeld = Down(EKeys::RightMouseButton);
	}
	else if (bCarryPart)
	{
		// R / Square turn the part in your hands (CarryInput): only right mouse is the second button.
		bAltPressed = Just(EKeys::RightMouseButton);
		bAltHeld = Down(EKeys::RightMouseButton);
	}
	else if (TWI::Sprays(Tool) && !(TinShed && TinShed->PumpHeldBy() == TinShed->Me()))   // [fluids] the nozzle's right mouse wins
	{
		// With the spray controls up the right mouse (L2 / LT) holds your breath to steady your hand,
		// so only R and Square change the colour then.
		const bool bColour = SprayItem != 0 ? (Just(EKeys::R) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square)) : bAltJust;
		if (bColour) SprayColour = (SprayColour + 1) % UTorqueBuildSubsystem::Paints().Num();
		bSprayBreathHeld = SprayItem != 0 && (Down(EKeys::RightMouseButton) || Axis(EKeys::Gamepad_LeftTriggerAxis) > 0.5f || (S.bConnected && S.L2 > 0.5f));
		// X (D-pad up): the scan lights up what's missed, thin, run or dry.
		if (Just(EKeys::X) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) SetSprayScan(!bSprayScan);
	}
	else
	{
		bAltPressed = bAltJust;
		bAltHeld = KeyDown(ETorqueKey::AltWork) || Down(EKeys::Gamepad_FaceButton_Left) || (SonyDown & ESonyButton::Square);
		// The lance: X (D-pad up) lights up the muck still to do, or puts it out.   [wash]
		if (Tool == ETorqueTool::Lance && (Just(EKeys::X) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp))) bWashScan = !bWashScan;
	}
	if (KeyJust(ETorqueKey::SprayFinish) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight))
		if (TWI::Sprays(Tool)) SprayFinish = (SprayFinish + 1) % FMath::Max(1, TWI::Loads(Tool).Num());
	bWorkPressed = bWorkJust;
	bWorkHeld = KeyDown(ETorqueKey::Work) || Down(EKeys::Gamepad_FaceButton_Bottom) || (SonyDown & ESonyButton::Cross);
	// F: tap to use or ride, hold on a bike to wheel it (walker.gd HOLD_TO_PUSH); wheeling, F lets go.
	const bool bFDown = KeyDown(ETorqueKey::Use) || Down(EKeys::Gamepad_FaceButton_Top) || (SonyDown & ESonyButton::Triangle);
	const UTorqueBuildSubsystem* FShed = Shed();
	const int32 Pushing = Wheeling();
	const bool bAtBike = FShed && TargetBay != INDEX_NONE && FShed->Exists(TargetBay) &&
		(Target == ETorqueLook::BikePart || (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bay));
	// [kit] The switch boxes by the roller door, the engine stand, the fire extinguisher.
	const bool bKitF = FurnitureInput(Dt, bF, bFDown, bAltJust) || KitInput(Dt, bF, bFDown) || FluidInput(bAltJust);   // [furniture] first; [fluids] the pump (right mouse)
	if (bKitF)
	{
	}
	else if (bF && SprayInteract())
	{
		// The can or gun on a part: F brings the spray controls up (or puts them down).
	}
	else if (Pushing != INDEX_NONE)
	{
		if (bF) LetGo(Pushing);
	}
	else if (bF && bAtBike)
	{
		bFArmed = true;
		FHeldFor = 0.0;
		FBay = TargetBay;
	}
	else if (bF) Interact();
	if (bFArmed && Pushing == INDEX_NONE)
	{
		FHeldFor += Dt;
		if (!bFDown)
		{
			bFArmed = false;
			Interact();
		}
		else if (FHeldFor >= TWI::HoldToPush)
		{
			bFArmed = false;
			FTorqueShedRequest Grab;
			Grab.Kind = ETorqueShedAct::GrabBike;
			Grab.Bay = FBay;
			FString WhyNot;
			if (UTorqueBuildSubsystem* GB = Shed())
			{
				if (!GB->Request(Grab, WhyNot)) Say(WhyNot, false);
				else
				{
					Doing(FString::Printf(TEXT("wheeling bay %d's bike"), FBay + 1), 30.0);
					UE_LOG(LogTemp, Log, TEXT("Torque: grab: %s"), *BikeWhere(FBay));
				}
			}
		}
	}
	if (KeyJust(ETorqueKey::Help)) bShowHelp = !bShowHelp;
	if (KeyJust(ETorqueKey::Perf)) bPerfShow = !bPerfShow;
	if (KeyJust(ETorqueKey::PerfLog))
	{
		bPerfLog = !bPerfLog;
		if (!bPerfLog) PerfFile.Reset();
	}
	// Esc, Options and Start open the pause menu (ATorquePlayerController), which has Quit.
}

int32 ATorqueWalker::ToolSlot() const
{
	for (int32 I = 0; I < TorqueToolBarCount; ++I)
		if (TorqueToolBar[I] == Tool) return I;
	return INDEX_NONE;
}

void ATorqueWalker::SetTool(ETorqueTool T)
{
	if (T == ETorqueTool::Spray) T = ETorqueTool::Gun;   // the spray can is gone: the gun paints
	if (T == Tool) return;
	if (SprayItem != 0) LeaveSprayMode();   // putting the gun away
	Tool = T;
	Progress = 0.0;
	ProgressKey.Reset();
	if (TWI::Sprays(T) && SprayFinish >= TWI::Loads(T).Num()) SprayFinish = 0;
	Say(FString::Printf(TEXT("%s in hand"), TWI::ToolName(T)), true, 1.5);
}

void ATorqueWalker::SetCrouched(bool bOn)
{
	if (bOn == bCrouched) return;
	bCrouched = bOn;
	if (bOn) Crouch();
	else UnCrouch();
}

void ATorqueWalker::TestSpray(int32 Colour, FName Finish)
{
	SprayColour = FMath::Clamp(Colour, 0, UTorqueBuildSubsystem::Paints().Num() - 1);
	// A finish, a coat or "primer", among what the spray gun takes.
	const TArray<TWI::FLoad> L = TWI::Loads(ETorqueTool::Gun);
	SprayFinish = FMath::Max(L.IndexOfByPredicate([Finish](const TWI::FLoad& X) { return X.Id == Finish; }), 0);
}

// --- What's looked at ----------------------------------------------------------------------------

void ATorqueWalker::FindTarget()
{
	ON_SCOPE_EXIT { FindFurniture(); };   // [furniture] a piece of kit nearer than what's found here wins
	Target = ETorqueLook::None;
	TargetBay = INDEX_NONE;
	TargetSlot = NAME_None;
	TargetItem = TargetEngine = 0;
	TargetPlace = nullptr;
	TargetMesh.Reset();
	UWorld* World = GetWorld();
	const UTorqueBuildSubsystem* B = Shed();
	if (!World || !Camera || !B) return;
	const FVector Start = Camera->GetComponentLocation();
	// Where the player looks (the camera follows it a frame later).
	const FVector Look = Controller ? GetControlRotation().Vector() : Camera->GetForwardVector();
	const FVector End = Start + Look * Reach;

	// Walls, the bench and the floor stop the look; the bikes and parts are tested against their boxes.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerLook), false, this);
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It) Params.AddIgnoredActor(*It);
	for (TActorIterator<ATorqueShedItems> It(World); It; ++It) Params.AddIgnoredActor(*It);
	for (TActorIterator<ATorqueWalker> It(World); It; ++It) Params.AddIgnoredActor(*It);
	double Clear = 1.0;
	bool bFloor = false;
	FVector FloorAt = FVector::ZeroVector;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		Clear = Hit.Time + 0.02;   // a part lying on the bench top is just above what stopped the line
		if (Hit.ImpactNormal.Z > 0.7) { bFloor = true; FloorAt = Hit.ImpactPoint; }
	}

	// Of the boxes the line goes into near where it first meets something (within 30 cm), a part
	// wins over a place (the wheel on the changer over the changer), then the smallest box (a
	// silencer inside the frame's box).
	// Each part is tested by its own shape where the bike's .glb gives one (TorqueShedPick: the
	// twin silencers' box fills the middle of the bike), else by its box. Of what the line goes into
	// near where it first meets something (within 30 cm), a part wins over a place (the wheel on
	// the changer over the changer); of the parts, the first one the line touches, or, known only
	// by their boxes, the smallest (a silencer inside the frame's box).
	LookHits.Reset();
	auto Consider = [&](const UStaticMeshComponent* C, ETorqueLook Kind)
	{
		double Time = 0.0, Volume = 0.0;
		if (!TWI::HitMeshBox(C, Start, End, Time, Volume)) return;
		double Exact = 0.0;
		const TorqueShedPick::EHit Shape = TorqueShedPick::Trace(C, Start, End, Exact);
		if (Shape == TorqueShedPick::EHit::Miss) return;
		if (Shape == TorqueShedPick::EHit::Hit) Time = Exact;
		if (Time > Clear) return;
		// The engine's own cubes and cylinders (boxes, blocks) are their boxes.
		const bool bExact = Shape == TorqueShedPick::EHit::Hit || (C->GetStaticMesh() && C->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")));
		FLookHit L;
		L.Kind = Kind;
		L.C = C;
		L.Time = Time;
		L.Volume = Volume;
		L.bExact = bExact;
		LookHits.Add(L);
	};
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		for (const UStaticMeshComponent* C : It->Parts)
			if (C && !TorqueShedModel::SlotTag(C).IsNone()) Consider(C, ETorqueLook::BikePart);
	for (TActorIterator<ATorqueShedItems> It(World); It; ++It)
		for (const UStaticMeshComponent* C : It->Meshes)
		{
			const FTorqueItem* Thing = B->Item(ATorqueShedItems::ItemOf(C));
			if (Thing && Thing->At != ETorqueAt::Held) Consider(C, ETorqueLook::Item);
		}
	for (const FTorquePlace& P : TorqueShedLayout::Places())
	{
		double Time = 0.0, Volume = 0.0;
		if (!TWI::HitWorldBox(P.Box, Start, End, Time, Volume) || Time > Clear) continue;
		FLookHit L;
		L.Kind = ETorqueLook::Place;
		L.Place = &P;
		L.Time = Time;
		L.Volume = Volume;
		L.bExact = true;
		LookHits.Add(L);
	}
	double First = TNumericLimits<double>::Max();
	for (const FLookHit& H : LookHits) First = FMath::Min(First, H.Time);
	const double Near = First + 30.0 / FMath::Max((double)Reach, 1.0);
	const FLookHit* Pick = nullptr;
	for (const FLookHit& H : LookHits)
	{
		if (H.Time > Near || (H.Kind != ETorqueLook::Place && !H.C.IsValid())) continue;
		if (!Pick) { Pick = &H; continue; }
		const bool bPart = H.Kind != ETorqueLook::Place, bPickPart = Pick->Kind != ETorqueLook::Place;
		if (bPart != bPickPart) { if (bPart) Pick = &H; continue; }
		if (H.bExact != Pick->bExact) { if (H.bExact) Pick = &H; continue; }
		if (H.bExact ? H.Time < Pick->Time : H.Volume < Pick->Volume) Pick = &H;
	}
	if (Pick)
	{
		Target = Pick->Kind;
		TargetPoint = Start + Look * Reach * Pick->Time;
		TargetMesh = Pick->C;
		if (Target == ETorqueLook::BikePart)
		{
			const ATorqueShedBike* Bike = Cast<ATorqueShedBike>(Pick->C->GetOwner());
			TargetBay = Bike ? Bike->Bay : INDEX_NONE;
			TargetSlot = TorqueShedModel::SlotTag(Pick->C.Get());
			TargetItem = B->ItemOn(TargetBay, TargetSlot);
		}
		else if (Target == ETorqueLook::Item)
		{
			TargetItem = ATorqueShedItems::ItemOf(Pick->C.Get());
			const FTorqueItem* Thing = B->Item(TargetItem);
			const FName Slot = TorqueShedModel::SlotTag(Pick->C.Get());
			// A part on an engine block: that part (unbolt it, take it off); a tyre stays its wheel's.
			if (Thing && Thing->Kind == FName(TEXT("engine")) && Slot != FName(TEXT("engine")) && Thing->Slots.FindRef(Slot) != 0)
			{
				TargetEngine = TargetItem;
				TargetSlot = Slot;
				TargetItem = Thing->Slots.FindRef(Slot);
			}
			const FTorqueItem* Base = B->Item(TargetEngine != 0 ? TargetEngine : TargetItem);
			if (Base && Base->At == ETorqueAt::Spot) TargetPlace = TorqueShedLayout::FindPlace(Base->Spot);
		}
		else
		{
			TargetPlace = Pick->Place;
			if (TargetPlace->Kind == ETorquePlace::Bay) TargetBay = TargetPlace->Index;
			// A place with a part on it: that part.
			if (TWI::IsSpotKind(TargetPlace->Kind))
				if (const int32 On = B->ItemAtSpot(TargetPlace->Name))
				{
					Target = ETorqueLook::Item;
					TargetItem = On;
				}
		}
		return;
	}
	if (bFloor)
	{
		Target = ETorqueLook::Floor;
		TargetPoint = FloorAt;
	}
	// The roller door: stand in its opening (a point tagged RideOut by the level script).
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->ActorHasTag(TEXT("RideOut")) && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 260.0)
		{
			Target = ETorqueLook::Door;
			TargetBay = B->MyBike();
		}
}

FString ATorqueWalker::Ping()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	UWorld* World = GetWorld();
	if (!PC || !World || !Camera)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque ping: pressed, but the walker has no controller or camera"));
		return FString();
	}
	// What the look finds as far as a ping reaches (walker.gd PING_REACH): the walker's own look, with
	// a longer arm, then put back as it was.
	const float ArmReach = Reach;
	Reach = (float)TorquePing::ReachCm;
	FindTarget();
	const ETorqueLook Kind = Target;
	const int32 Bay = TargetBay, ItemId = TargetItem, EngineId = TargetEngine, Furn = TargetFurniture;
	const FName Slot = TargetSlot;
	const FTorquePlace* PlaceAt = TargetPlace;
	const FVector Point = TargetPoint;
	const UStaticMeshComponent* PickedMesh = TargetMesh.Get();
	Reach = ArmReach;
	FindTarget();

	const FVector Start = Camera->GetComponentLocation();
	const FVector Look = GetControlRotation().Vector();
	const UTorqueBuildSubsystem* B = Shed();
	FTorquePingNet P;
	P.At = Point;
	if (B && Furn != 0)
	{
		// A piece of kit (furniture.gd's name: "Shelving unit: Brakes").
		if (const FTorqueFurniture* F = B->FurnitureById(Furn)) P.What = TorqueFurniture::NameOf(*F);
		for (TActorIterator<ATorqueFurnishings> It(World); It; ++It)
			if (const UStaticMeshComponent* const* C = It->Pieces().Find(Furn)) TorquePing::BoxOf(*C, P);
	}
	else if (B && Kind == ETorqueLook::BikePart && B->Exists(Bay))
	{
		// "Bay 2's bike: exhaust" (ping_marker.gd: the bike's name and the slot's).
		P.What = FString::Printf(TEXT("%s: %s"), *B->Bike(Bay).Name, *TorqueShedParts::SlotName(Slot).ToLower());
		TorquePing::BoxOf(PickedMesh, P);
	}
	else if (B && Kind == ETorqueLook::Item && ItemId != 0)
	{
		P.What = B->ItemTitle(ItemId);
		if (EngineId != 0 && PickedMesh) TorquePing::BoxOf(PickedMesh, P);   // a part on an engine block: that part
		else
		{
			// A loose part: all its meshes (a wheel and its tyre, an engine and what's on it).
			FBox Box(ForceInit);
			for (TActorIterator<ATorqueShedItems> It(World); It; ++It)
				for (const UStaticMeshComponent* C : It->Meshes)
					if (C && ATorqueShedItems::ItemOf(C) == ItemId) Box += C->Bounds.GetBox();
			if (Box.IsValid) TorquePing::BoxOf(Box, P);
			else TorquePing::BoxOf(PickedMesh, P);
		}
	}
	else if (B && Kind == ETorqueLook::Place && PlaceAt)
	{
		P.What = TWI::PlaceName(*PlaceAt, B);
		TorquePing::BoxOf(PlaceAt->Box, P);
	}
	if (P.What.IsEmpty() && Kind != ETorqueLook::None && Kind != ETorqueLook::Floor && Kind != ETorqueLook::Door) P.What = TEXT("Over here");

	// Another player nearer than that: them.
	const double ThingCm = P.What.IsEmpty() ? TorquePing::ReachCm : FVector::Dist(Start, Point);
	FTorquePingNet Who;
	double WhoCm = 0.0;
	if (TorquePing::PlayerOnLine(this, Start, Look, ThingCm, Who, WhoCm))
	{
		TorquePing::Send(PC, Who);
		return Who.What;
	}
	if (!P.What.IsEmpty())
	{
		TorquePing::Send(PC, P);
		return P.What;
	}
	// Nothing in the shed by name: the spot the look meets (the floor, a wall, the road), or nothing.
	const FString Spot = TorquePing::FromView(PC, this);
	if (Spot.IsEmpty())
	{
		Say(TEXT("Nothing there to ping."), false, 2.0);
		UE_LOG(LogTemp, Log, TEXT("Torque ping: pressed, nothing within reach"));
	}
	return Spot;
}

TArray<int32> ATorqueWalker::ItemsNear(const FVector& At, double Radius) const
{
	TArray<int32> Out;
	const UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World) return Out;
	if (TargetItem != 0) Out.Add(TargetItem);
	const double R2 = Radius * Radius;
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		for (const UStaticMeshComponent* C : It->Parts)
		{
			if (!C || C->Bounds.GetBox().ComputeSquaredDistanceToPoint(At) > R2) continue;
			const FName Slot = TorqueShedModel::SlotTag(C);
			const int32 Id = Slot.IsNone() ? 0 : B->ItemOn(It->Bay, Slot);
			if (Id != 0) Out.AddUnique(Id);
		}
	for (TActorIterator<ATorqueShedItems> It(World); It; ++It)
		for (const UStaticMeshComponent* C : It->Meshes)
		{
			if (!C || C->Bounds.GetBox().ComputeSquaredDistanceToPoint(At) > R2) continue;
			const int32 Id = ATorqueShedItems::ItemOf(C);
			const FTorqueItem* Thing = B->Item(Id);
			if (!Thing || Thing->At == ETorqueAt::Held) continue;
			// What's on an engine or a wheel gets it too.
			const FName Slot = TorqueShedModel::SlotTag(C);
			int32 Sub = Thing->Slots.FindRef(Slot);
			if (Sub == 0 && Thing->Tyre != 0 && TorqueShedParts::IsTyreSlot(Slot)) Sub = Thing->Tyre;
			Out.AddUnique(Sub != 0 ? Sub : Id);
		}
	return Out;
}

// --- What the work button does (walker.gd _make_plan) ----------------------------------------------

FString ATorqueWalker::FPlan::Key() const
{
	FString Ids;
	for (int32 Id : Request.Items) Ids += FString::Printf(TEXT("%d,"), Id);
	return FString::Printf(TEXT("%d/%d/%d/%s/%d/%s/%s/%d/%s"), (int32)Request.Kind, Request.Bay, Request.Engine, *Request.Slot.ToString(),
		Request.Item, *Request.Spot.ToString(), *Request.Bin.ToString(), Request.Colour, *Ids);
}

ATorqueWalker::FPlan ATorqueWalker::MakeOne(ETorqueShedAct Kind, double Time, const FString& Verb) const
{
	FPlan P;
	P.bValid = true;
	P.Request.Kind = Kind;
	P.Request.bCrouched = bCrouched;
	P.Time = Time;
	P.Verb = Verb;
	return P;
}

void ATorqueWalker::MakePlan()
{
	Plan = FPlan();
	AltPlan = FPlan();
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	const int32 Me = B->Me();
	if (SprayItem != 0)
	{
		// The spray controls are up: the can stays on that part, wherever you look.
		if (TWI::Sprays(Tool) && B->Item(SprayItem)) Plan = SprayPlanFor(SprayItem, SprayBay);
		if (Plan.bValid) Plan.Blocked = B->Check(Me, Plan.Request);
		return;
	}
	const int32 Held = B->HeldBy(Me);
	const FTorqueItem* HeldIt = B->Item(Held);
	if (Target == ETorqueLook::None || Target == ETorqueLook::Door)
	{
		// Something in your hands and nothing in reach to put it on (looking ahead, out in the yard,
		// in the doorway): it goes down on the ground at your feet, a step in front.
		if (HeldIt && !B->PaintBay().HeldTin(Me) && B->PaintBay().TapeBy != Me)
		{
			Plan = MakeOne(ETorqueShedAct::Drop, 0.25, CarryReach > 1.0 ? TEXT("put it down there") : TEXT("put it down in front of you"));
			Plan.Request.Place = CarryDrop();
			Plan.Blocked = B->Check(Me, Plan.Request);
		}
		return;
	}
	const FTorqueItem* Thing = B->Item(TargetItem);
	const FName Engine(TEXT("engine"));
	// Putting a part down on the floor: as far out as the wheel has put it, turned as R has
	// (walker.gd: equipment you carry goes down where its outline is, not where the crosshair is).
	auto DropPlan = [&]()
	{
		FPlan P = MakeOne(ETorqueShedAct::Drop, 0.25, TEXT("put it down there"));
		P.Request.Place = CarryDrop();
		return P;
	};

	// The dyno room's kit (dyno_room.gd plan_for): the straps whatever's in hand, the desk's run button.
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoStrap)
	{
		const int32 Point = FMath::Clamp(TargetPlace->Index, 0, 3);
		const bool bDone = B->DynoRoom().Straps[Point];
		Plan = MakeOne(ETorqueShedAct::DynoStrap, bDone ? 0.8 : 1.4, bDone ? TEXT("undo this strap") : TEXT("hook the strap on and ratchet it down tight"));
		Plan.Request.Item = Point;
		Plan.Blocked = B->Check(Me, Plan.Request);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoDesk)
	{
		// tuning_computer.gd: the desk's page (tune the bike, run the dyno).
		Plan = MakeOne(ETorqueShedAct::DynoRun, 0.15, TEXT("use the dyno desk"));
		Plan.Request.Bay = B->DynoBay();
		Plan.bComputer = true;
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoSwitch) return;   // F flicks them
	// [kit] F works the switch boxes and takes the extinguisher; holding it, the work button sprays it.
	if (TargetPlace && (TargetPlace->Kind == ETorquePlace::ShedSwitch || TargetPlace->Kind == ETorquePlace::Extinguisher)) return;
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Tv) return;   // [tv] right mouse changes the channel
	if (B->ExtinguisherHeldBy() == Me || B->StandPushedBy() == Me) return;
	// [fluids] The pump's nozzle in your hand: the work button fills the bike you're at (TorqueWalkerFluids.cpp).
	if (B->PumpHeldBy() == Me || (TargetPlace && TargetPlace->Kind == ETorquePlace::Pump))
	{
		PumpPlan();
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Radio)
	{
		// The radio (walker.gd _make_plan): on, then round the stations; F switches it off.
		const int32 Num = TorqueShedRadio::Stations().Num();
		UE_LOG(LogTemp, Verbose, TEXT("Torque radio: looking at it (%d stations)"), Num);
		if (Num == 0) return;
		if (!B->RadioOn()) Plan = MakeOne(ETorqueShedAct::Radio, 0.2, TEXT("switch the radio on"));
		else
		{
			const int32 Next = (B->RadioStation() + 1) % Num;
			Plan = MakeOne(ETorqueShedAct::Radio, 0.2, FString::Printf(TEXT("tune to %s"), *TorqueShedRadio::Stations()[Next].Name));
			Plan.Request.Colour = Next;
		}
		if (!B->RadioOn()) Plan.Request.Colour = B->RadioStation();
		Plan.Request.Item = 1;
		Plan.Blocked = B->Check(Me, Plan.Request);
		return;
	}
	// Masking tape (walker.gd _paint_plan "tape"): off its hook or back on it, and in your hands, on a
	// painted part to mask it off (R picks the pattern), right mouse peeling it off again.
	const bool bTape = B->PaintBay().TapeBy == Me;
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Tape)
	{
		Plan = bTape ? MakeOne(ETorqueShedAct::PaintTapePut, 0.3, TEXT("hang the tape back on its hook"))
			: MakeOne(ETorqueShedAct::PaintTapeTake, 0.3, TEXT("take the masking tape"));
		Plan.Blocked = B->Check(Me, Plan.Request);
		return;
	}
	if (bTape)
	{
		if (TargetItem != 0)
		{
			Plan = MakeOne(ETorqueShedAct::PaintTape, 2.5, FString::Printf(TEXT("mask it off: %s (R picks)"),
				*FString(TorquePaintMix::PatternName(B->PaintBay().TapePattern)).ToLower()));
			Plan.Request.Item = TargetItem;
			Plan.Blocked = B->Check(Me, Plan.Request);
			const FTorqueItem* Taped = B->Item(TargetItem);
			if (Taped && Taped->Tape != 0)
			{
				AltPlan = MakeOne(ETorqueShedAct::PaintPeel, 1.2, TEXT("peel the masking tape off"));
				AltPlan.Request.Item = TargetItem;
				AltPlan.Blocked = B->Check(Me, AltPlan.Request);
			}
		}
		return;   // the tape in your hands: nothing else until it's hung back up
	}
	if (TargetPlace && (TargetPlace->Kind == ETorquePlace::PaintBench || TargetPlace->Kind == ETorquePlace::PaintShelf)) return;   // F, as the tins go
	if (B->PaintBay().HeldTin(Me)) return;   // a tin in your hands: nothing else until it's put down
	// A part in your hands: your hands are full, whatever tool was last picked (the scroll wheel
	// changes it), so it's put down or fitted as with bare hands.
	switch (HeldIt ? ETorqueTool::Hands : Tool)
	{
	case ETorqueTool::Hands:
		// [fluids] A can or bottle in your hands: pour it in, drain into it, lube the chain, tip it away
		// (TorqueWalkerFluids.cpp); else it's put down like any part.
		if (HeldIt && TorqueShedParts::IsFluid(HeldIt->Part) && FluidPlan(Held))
			break;
		if (HeldIt)
		{
			const FString Title = TWI::Lower(B->ItemTitle(Held));
			if (TargetPlace && TargetPlace->Kind == ETorquePlace::Bay && HeldIt->Kind == FName(TEXT("frame")) &&
				(!B->Exists(TargetPlace->Index) || B->IsAway(TargetPlace->Index)))
			{
				// A bare frame on an empty bay's paddock stand starts a bike from scratch. (A bay whose bike
				// is parked elsewhere is empty to look at, so it's free too: that bike keeps another bay's
				// place where it stands, or the prompt says why not, rather than the frame going down on
				// the lift's platform.)
				Plan = MakeOne(ETorqueShedAct::StartBuild, 1.5, FString::Printf(TEXT("set the frame up on the paddock stand in bay %d"), TargetPlace->Index + 1));
				Plan.Request.Bay = TargetPlace->Index;
			}
			else if (Target == ETorqueLook::BikePart)
			{
				Plan = MakeOne(ETorqueShedAct::Install, 1.2, TEXT("fit the ") + Title);
				Plan.Request.Bay = TargetBay;
				Plan.Request.Slot = TargetSlot;
			}
			else if (TargetPlace && TWI::IsSpotKind(TargetPlace->Kind))
			{
				const FName Spot = TargetPlace->Name;
				const int32 OnSpot = B->ItemAtSpot(Spot);
				const FTorqueItem* On = B->Item(OnSpot);
				if (TargetPlace->Kind == ETorquePlace::Changer && On && HeldIt->Kind == FName(TEXT("tyre")))
				{
					// Work the tyre on round the rim: a skill game (TorqueWalkerTyre.cpp, TorqueTyreGame).
					Plan = MakeOne(ETorqueShedAct::TyreFit, FTorqueTyreGame::Grace + FTorqueTyreGame::Duration,
						TEXT("work the tyre on: keep the lever on the marker round the rim"));
					Plan.Request.Spot = Spot;
					Plan.Request.Amount = 1.0f;   // the bead (the game sets it; a test's act seats it fully)
				}
				else if (TargetPlace->Kind == ETorquePlace::Stand && On && On->Kind == Engine && HeldIt->Kind != Engine)
				{
					// Building an engine back up: fit the part to the block on the stand.
					Plan = MakeOne(ETorqueShedAct::Install, 1.2, FString::Printf(TEXT("fit the %s to the engine"), *Title));
					Plan.Request.Engine = OnSpot;
					Plan.Request.Slot = TargetSlot;
				}
				else
				{
					// On the place looked at, or the nearest free one on the same bench or machine.
					FName Free = Spot;
					if (OnSpot != 0)
					{
						double Best = TNumericLimits<double>::Max();
						for (const FTorquePlace& P : TorqueShedLayout::Places())
						{
							if (P.Kind != TargetPlace->Kind || B->ItemAtSpot(P.Name) != 0) continue;
							const double D = FVector::DistSquared(P.Put, TargetPoint);
							if (D < Best) { Best = D; Free = P.Name; }
						}
					}
					Plan = MakeOne(ETorqueShedAct::Place, 0.25, TWI::PutVerb(TargetPlace->Kind));
					Plan.Request.Spot = Free;
				}
			}
			else if (TargetPlace && TargetPlace->Kind == ETorquePlace::Bin)
			{
				const bool bGeneral = TargetPlace->Name == FName(TEXT("general"));
				Plan = MakeOne(ETorqueShedAct::Bin, 0.4, (bGeneral ? TEXT("throw the ") + Title + TEXT(" away") : TEXT("recycle the ") + Title));
				Plan.Request.Bin = TargetPlace->Name;
			}
			else if (Target == ETorqueLook::Floor || Target == ETorqueLook::Item || (TargetPlace && TargetPlace->Kind == ETorquePlace::Bay))
				Plan = DropPlan();
		}
		else if (Target == ETorqueLook::BikePart)
		{
			Plan = MakeOne(ETorqueShedAct::Remove, 1.0, TEXT("take it off"));
			Plan.Request.Bay = TargetBay;
			Plan.Request.Slot = TargetSlot;
		}
		else if (Target == ETorqueLook::Item && TargetEngine != 0)
		{
			Plan = MakeOne(ETorqueShedAct::Remove, 1.0, TEXT("take it off the engine"));
			Plan.Request.Engine = TargetEngine;
			Plan.Request.Slot = TargetSlot;
		}
		else if (Target == ETorqueLook::Item && Thing)
		{
			if (Thing->At == ETorqueAt::Spot)
			{
				if (TargetPlace && TargetPlace->Kind == ETorquePlace::Changer && Thing->Tyre != 0)
				{
					// On the tyre changer: break the bead and lever the old tyre off, or lift the wheel off whole.
					Plan = MakeOne(ETorqueShedAct::TyreRemove, 2.5, TEXT("break the bead and lever the tyre off"));
					Plan.Request.Spot = Thing->Spot;
					AltPlan = MakeOne(ETorqueShedAct::Take, 0.4, TEXT("lift the wheel off, tyre and all"));
					AltPlan.Request.Spot = Thing->Spot;
				}
				else
				{
					Plan = MakeOne(ETorqueShedAct::Take, 0.25, TEXT("pick it up"));
					Plan.Request.Spot = Thing->Spot;
				}
			}
			else
			{
				Plan = MakeOne(ETorqueShedAct::Take, 0.25, TEXT("pick it up"));
				Plan.Request.Item = TargetItem;
			}
		}
		else if (TargetPlace && TargetPlace->Kind == ETorquePlace::Computer)
		{
			Plan = MakeOne(ETorqueShedAct::OrderPart, 0.15, TEXT("use the parts computer"));
			Plan.bComputer = true;
		}
		break;
	case ETorqueTool::Wrench:
		if (Target == ETorqueLook::BikePart || (Target == ETorqueLook::Item && TargetEngine != 0))
		{
			const int32 OnBay = Target == ETorqueLook::BikePart ? TargetBay : INDEX_NONE;
			const bool bBolted = B->IsBolted(OnBay, TargetSlot, TargetEngine);
			Plan = MakeOne(bBolted ? ETorqueShedAct::Unbolt : ETorqueShedAct::Bolt, 1.6, bBolted ? TEXT("unbolt it") : TEXT("bolt it up"));
			Plan.Request.Bay = OnBay;
			Plan.Request.Engine = TargetEngine;
			Plan.Request.Slot = TargetSlot;
		}
		break;
	case ETorqueTool::Brush:
	{
		// The brush scrubs everything round where it lands, not just one part.
		TArray<int32> Near;
		float Dirt = 0.0f;
		for (int32 Id : ItemsNear(TargetPoint, TWI::BrushReach))
		{
			const FTorqueItem* It = B->Item(Id);
			if (It && (Id == TargetItem || (It->Dirt >= 0.02f && !It->bBoxed)))
			{
				Near.Add(Id);
				Dirt += It->Dirt;
			}
		}
		if (Near.Num() > 0)
		{
			Plan = MakeOne(ETorqueShedAct::Clean, 1.0 + FMath::Min(2.5 * Dirt, 5.0),
				Near.Num() == 1 ? FString(TEXT("scrub it clean")) : FString::Printf(TEXT("scrub %d parts clean"), Near.Num()));
			Plan.Request.Items = Near;
			Plan.Request.Bay = TargetBay;
		}
		break;
	}
	case ETorqueTool::Repair:
	{
		// A bike that owes fabrication (a part fitted that needed it): make the next job.
		const TArray<FString> Owed = Target == ETorqueLook::BikePart ? B->FabOwed(TargetBay) : TArray<FString>();
		if (Owed.Num() > 0)
		{
			Plan = MakeOne(ETorqueShedAct::Fabricate, UTorqueBuildSubsystem::FabTime(Owed[0]), TEXT("make the ") + Owed[0]);
			Plan.Request.Bay = TargetBay;
			break;
		}
		// On the repair bench it does the lot; anywhere else the shed says to put it there.
		TArray<int32> Batch;
		const bool bAtRepair = TargetPlace && TargetPlace->Kind == ETorquePlace::Repair;
		if (bAtRepair)
		{
			for (const FTorquePlace& P : TorqueShedLayout::Places())
				if (P.Kind == ETorquePlace::Repair)
					if (const int32 On = B->ItemAtSpot(P.Name)) Batch.Add(On);
		}
		else if (TargetItem != 0)
			Batch.Add(TargetItem);
		if (Batch.Num() > 0)
		{
			Plan = MakeOne(ETorqueShedAct::Repair, B->RepairTime(Batch),
				Batch.Num() > 1 ? FString::Printf(TEXT("restore all %d parts on the repair bench"), Batch.Num()) : FString(TEXT("grind off the rust and rebuild it")));
			Plan.Request.Items = Batch;
		}
		break;
	}
	case ETorqueTool::Spray:
	case ETorqueTool::Gun:
		if (TargetItem != 0) Plan = SprayPlanFor(TargetItem, TargetBay);
		break;
	case ETorqueTool::Lance:
	{
		TArray<int32> Near = ItemsNear(TargetPoint, TWI::JetReach);
		if (Near.Num() > 0)
		{
			Plan = MakeOne(ETorqueShedAct::Wash, 0.2, TEXT("blast the muck off"));
			Plan.Request.Items = Near;
			Plan.Request.Place = FTransform(TargetPoint);
			Plan.Request.Bay = TargetBay;
			Plan.bRepeat = true;
		}
		break;
	}
	default:
		break;
	}
	// Hands free on a part with masking tape on it: right mouse peels it off (walker.gd _part_alt).
	if (!AltPlan.bValid && Tool == ETorqueTool::Hands && !HeldIt && !B->PaintBay().HeldTin(Me) && TargetItem != 0)
		if (const FTorqueItem* Taped = B->Item(TargetItem); Taped && Taped->Tape != 0)
		{
			AltPlan = MakeOne(ETorqueShedAct::PaintPeel, 1.2, TEXT("peel the masking tape off"));
			AltPlan.Request.Item = TargetItem;
		}
	// Hands free at a spray booth hook (or the part on it): right mouse winds it up or down.
	const int32 Hook = HookOf();
	if (!AltPlan.bValid && Hook != INDEX_NONE && Tool == ETorqueTool::Hands && !HeldIt && !B->PaintBay().HeldTin(Me))
	{
		const bool bUp = HookMoving == Hook ? HookDir < 0 : HookDir > 0;   // while winding: the way it's going
		AltPlan = MakeOne(ETorqueShedAct::PaintHook, 0.0, FString::Printf(TEXT("hold to %s the hook (%d cm)"),
			bUp ? TEXT("raise") : TEXT("lower"), FMath::RoundToInt(HookMoving == Hook ? HookSet : B->HookTipOf(Hook))));
		AltPlan.Request.Item = Hook;
		AltPlan.Request.Amount = B->HookTipOf(Hook);
	}
	// Hands free on a bay's bike: right mouse, held, scraps it (shed_state.gd remove_bike: no money
	// in the UE shed, so it and every part on it are simply gone, and the bay is free).
	if (!AltPlan.bValid && Target == ETorqueLook::BikePart && B->Exists(TargetBay) && Tool == ETorqueTool::Hands && !HeldIt && !B->PaintBay().HeldTin(Me))
	{
		AltPlan = MakeOne(ETorqueShedAct::ScrapBike, 4.0, FString::Printf(TEXT("hold to scrap %s and every part on it (frees bay %d)"),
			*B->Bike(TargetBay).Name, TargetBay + 1));
		AltPlan.Request.Bay = TargetBay;
	}
	if (Plan.bValid && !Plan.bComputer) Plan.Blocked = B->Check(Me, Plan.Request);
	if (AltPlan.bValid) AltPlan.Blocked = B->Check(Me, AltPlan.Request);
}

// walker.gd _spray_plan: what the can or gun does to Item (a bike part on bay Bay): its colour,
// primer or a coat, as loaded.
ATorqueWalker::FPlan ATorqueWalker::SprayPlanFor(int32 Item, int32 Bay) const
{
	FPlan P;
	const UTorqueBuildSubsystem* B = Shed();
	if (!B || Item == 0 || !TWI::Sprays(Tool)) return P;
	const bool bGun = Tool == ETorqueTool::Gun;
	const TArray<TWI::FLoad> L = TWI::Loads(Tool);
	const TWI::FLoad Load = L.IsValidIndex(SprayFinish) ? L[SprayFinish] : L[0];
	if (Load.Kind == TWI::FLoad::Primer)
		P = MakeOne(ETorqueShedAct::Prime, 1.5, bGun ? FString(TEXT("prime it: grey primer, in steady passes")) : FString(TEXT("prime it: sweep grey primer over all of it")));
	else if (Load.Kind == TWI::FLoad::Coat)
	{
		P = MakeOne(ETorqueShedAct::Coat, 1.5, UTorqueBuildSubsystem::IsLacquer(Load.Id)
			? FString::Printf(TEXT("lacquer it: sweep %s over the paint"), *UTorqueBuildSubsystem::CoatName(Load.Id).ToLower())
			: FString::Printf(TEXT("spray it with %s: sweep over all of it"), *UTorqueBuildSubsystem::CoatName(Load.Id).ToLower()));
		P.Request.Finish = Load.Id;
	}
	else if (Load.Kind == TWI::FLoad::Cup)
	{
		// The gun's cup, filled from a tin from the mixing bay.
		const FTorquePaintCup* Cup = B->PaintBay().Cups.Find(B->Me());
		const FString What = Cup ? Cup->Spec.Name : FString(TEXT("the cup"));
		P = MakeOne(ETorqueShedAct::Paint, 1.5, FString::Printf(TEXT("spray %s from the cup: go along the part in steady passes"), *What));
		P.Request.Finish = UTorqueBuildSubsystem::CupFinish();
	}
	else
	{
		const FString What = FString::Printf(TEXT("%s %s"), *UTorqueBuildSubsystem::FinishName(Load.Id).ToLower(), *UTorqueBuildSubsystem::PaintName(SprayColour).ToLower());
		P = MakeOne(ETorqueShedAct::Paint, 1.5, bGun ? FString::Printf(TEXT("spray %s: go along the part in steady passes"), *What)
			: FString::Printf(TEXT("spray it %s: sweep over it evenly"), *What));
		P.Request.Colour = SprayColour;
		P.Request.Finish = Load.Id;
	}
	P.Request.Item = Item;
	P.Request.Bay = Bay;
	P.Request.bGun = bGun;
	return P;
}

int32 ATorqueWalker::HookOf() const
{
	if (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Hook) return TargetPlace->Index;
	const UTorqueBuildSubsystem* B = Shed();
	if (Target != ETorqueLook::Item || !B) return INDEX_NONE;
	const FTorqueItem* It = B->Item(TargetItem);
	if (!It || It->At != ETorqueAt::Spot) return INDEX_NONE;
	const FTorquePlace* P = TorqueShedLayout::FindPlace(It->Spot);
	return P && P->Kind == ETorquePlace::Hook ? P->Index : INDEX_NONE;
}

void ATorqueWalker::HookWork(float Dt)
{
	UTorqueBuildSubsystem* B = Shed();
	const bool bOn = B && bAltHeld && AltPlan.bValid && AltPlan.Request.Kind == ETorqueShedAct::PaintHook && AltPlan.Blocked.IsEmpty();
	if (!bOn || (HookMoving != INDEX_NONE && HookMoving != AltPlan.Request.Item))
	{
		if (HookMoving != INDEX_NONE)
		{
			HookMoving = INDEX_NONE;
			HookDir = -HookDir;   // the next hold goes back the other way
		}
		return;
	}
	const int32 Hook = AltPlan.Request.Item;
	if (HookMoving == INDEX_NONE)
	{
		HookMoving = Hook;
		HookSet = B->HookTipOf(Hook);
		HookSendIn = 0.0;
		// At the top or the bottom already: go the way it can.
		if (HookSet >= FTorquePaintBay::HookHigh - 0.5f) HookDir = -1;
		else if (HookSet <= FTorquePaintBay::HookLow + 0.5f) HookDir = 1;
	}
	const float Was = HookSet;
	HookSet = FMath::Clamp(HookSet + HookDir * 45.0f * Dt, FTorquePaintBay::HookLow, FTorquePaintBay::HookHigh);
	HookSendIn -= Dt;
	if (HookSendIn > 0.0 && HookSet != FTorquePaintBay::HookLow && HookSet != FTorquePaintBay::HookHigh) return;
	if (FMath::Abs(HookSet - B->HookTipOf(Hook)) < 0.5f && Was == HookSet) return;
	HookSendIn = 0.15;
	FTorqueShedRequest R = AltPlan.Request;
	R.Amount = HookSet;
	FString WhyNot;
	if (!B->Request(R, WhyNot)) Say(WhyNot, false);
}

void ATorqueWalker::Work(float Dt)
{
	const bool bHeld = bWorkHeld || ActHeldFor > 0.0;
	const FPlan* P = bHeld ? &Plan : (bAltHeld ? &AltPlan : nullptr);
	if (!bHeld && !bAltHeld) bWaitRelease = false;
	if (!P || !P->bValid || bWaitRelease)
	{
		Progress = 0.0;
		ProgressKey.Reset();
		return;
	}
	if (!P->Blocked.IsEmpty())
	{
		if (bWorkPressed || bAltPressed) Say(P->Blocked, false);
		Progress = 0.0;
		return;
	}
	const FString Key = P->Key();
	if (Key != ProgressKey)
	{
		ProgressKey = Key;
		Progress = 0.0;
	}
	Progress += Dt;
	if (P->Time >= 0.6)
		Doing(TargetBay != INDEX_NONE ? FString::Printf(TEXT("%s on bay %d"), *P->Verb, TargetBay + 1) : P->Verb, 1.5);
	if (Progress < P->Time) return;
	const FPlan Done = *P;   // Do can change the plans
	Progress = 0.0;
	if (!Done.bRepeat)
	{
		bWaitRelease = true;
		ProgressKey.Reset();
		ActHeldFor = 0.0;
	}
	Do(Done);
}

bool ATorqueWalker::Do(const FPlan& P)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	if (P.bComputer)
	{
		if (P.Request.Kind == ETorqueShedAct::DynoRun) OpenDesk();
		else OpenComputer();
		return true;
	}
	if (P.Request.Kind == ETorqueShedAct::PaintHook) return true;   // wound while it's held (HookWork)
	// Working on a bike makes it yours (the one you ride out).
	const int32 Bay = P.Request.Bay != INDEX_NONE ? P.Request.Bay : B->BayOf(P.Request.Item);
	if (Bay != INDEX_NONE && B->Exists(Bay) && P.Request.Kind != ETorqueShedAct::Wash) B->SetMyBike(Bay);
	FString WhyNot;
	if (!B->Request(P.Request, WhyNot))
	{
		Say(WhyNot, false);
		return false;
	}
	// The host says what it did (OnTell); a joined player hears it through the net layer.
	if (P.Request.Kind == ETorqueShedAct::StartBuild) B->SetMyBike(P.Request.Bay);
	return true;
}

// --- The tool in your hand ------------------------------------------------------------------------

void ATorqueWalker::UpdateToolView(float Dt)
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!ToolHolder)
	{
		ToolHolder = NewObject<USceneComponent>(this, TEXT("ToolHolder"));
		ToolHolder->SetupAttachment(Camera);
		ToolHolder->RegisterComponent();
	}
	// The can is drawn in the spray colour; a new colour or tool redraws it.
	const int32 ToolIndex = (int32)Tool;
	const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
	const FLinearColor Can = Paints.IsValidIndex(SprayColour) ? Paints[SprayColour] : FLinearColor(0.9f, 0.75f, 0.1f);
	if (ToolIndex != ShownTool || (TWI::Sprays(Tool) && !Can.Equals(ShownCan)))
	{
		for (UStaticMeshComponent* C : ToolParts)
			if (C) C->DestroyComponent();
		ToolParts.Reset();
		TorqueToolModels::Make(this, ToolHolder, ToolIndex, Can, ToolParts, ToolNozzle);
		ShownTool = ToolIndex;
		ShownCan = Can;
	}
	// Hidden while you carry a part (Torque Shed puts the tool away), at the computer and riding out.
	const bool bCarrying = B && (B->HeldBy(PeerOf()) != 0 || B->PaintBay().HeldTin(PeerOf()) != nullptr);   // [co-op] PeerOf: a remote walker's own; a tin from the mixing bay too
	ToolHolder->SetVisibility(!bCarrying && !bComputer && !bBench && !bRidingOut, true);
	// Working with it: Progress runs while the work button's held on a job that can be done.
	const bool bWorking = Progress > 0.0 && !bCarrying;
	WorkBlend = FMath::FInterpTo(WorkBlend, bWorking ? 1.0 : 0.0, (double)Dt, 12.0);
	ToolClock += Dt;
	const double T = ToolClock, W = WorkBlend;
	// The work's motion on top of Torque Shed's hold (Unreal's frame, cm and degrees: x ahead, y
	// right, z up; roll about the view).
	FVector Move = FVector::ZeroVector;
	FRotator Turn = FRotator::ZeroRotator;
	// A little sway as you walk about.
	const double Speed = GetVelocity().Size2D();
	Move += FVector(0.0, FMath::Sin(T * 7.0) * 0.4, FMath::Abs(FMath::Sin(T * 7.0)) * -0.5) * FMath::Clamp(Speed / 300.0, 0.0, 1.0);
	switch (Tool)
	{
	case ETorqueTool::Wrench:
		// Ratcheting a bolt: a quarter swing about the view, out and back, pushed onto the job.
		Turn.Roll = (FMath::Sin(T * 8.0) * 0.5 + 0.5) * 35.0 * W;
		Move += FVector(6.0, -4.0, 3.0) * W;
		break;
	case ETorqueTool::Brush:
		// Scrubbing: quick strokes side to side.
		Move += FVector(7.0 + FMath::Sin(T * 28.0) * 1.0, -6.0 + FMath::Sin(T * 14.0) * 4.0, 2.0) * W;
		Turn.Yaw = FMath::Sin(T * 14.0) * 6.0 * W;
		break;
	case ETorqueTool::Repair:
		// The grinder's buzz, held into the work.
		Move += FVector(8.0 + FMath::Sin(T * 91.0) * 0.25, -5.0 + FMath::Sin(T * 77.0) * 0.25, 4.0 + FMath::Sin(T * 83.0) * 0.25) * W;
		Turn.Pitch = -6.0 * W;
		break;
	case ETorqueTool::Spray:
	case ETorqueTool::Gun:
		// Raised and swept gently across the part, the nozzle pressed.
		Move += FVector(4.0, -6.0 + FMath::Sin(T * 3.0) * 3.0, 6.0) * W;
		Turn.Pitch = 4.0 * W;
		break;
	case ETorqueTool::Lance:
		// Held a little further right, so the barrel comes in across the view to the aim; the kick of the jet.
		Move += FVector(0.0, 6.0, 0.0);
		Move += FVector(FMath::Sin(T * 60.0) * 0.3, 0.0, FMath::Sin(T * 53.0) * 0.3) * W;
		break;
	default:
		break;
	}
	// The motion is in the camera's axes, the turn about the tool's own grip.
	FTransform Hold = TorqueToolModels::HoldOf(ToolIndex);
	Hold.SetRotation(Turn.Quaternion() * Hold.GetRotation());
	Hold.AddToTranslation(Move);
	if (Tool == ETorqueTool::Lance)
	{
		// The water comes straight out of the lance's tip along its barrel (the tool's +x in both
		// models), so the lance is turned about your grip to point that barrel at where the water lands.
		const FTransform CamXf = Camera->GetComponentTransform();
		const FVector LTarget = CamXf.InverseTransformPositionNoScale(LanceAimPoint());
		const FQuat LBase = Hold.GetRotation();
		FQuat LRot = LBase;
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			const FVector LTip = Hold.GetLocation() + LRot.RotateVector(ToolNozzle);
			const FVector LBarrel = LRot.RotateVector(FVector::ForwardVector);
			const FVector LWant = (LTarget - LTip).GetSafeNormal();
			if (LWant.IsNearlyZero()) break;
			LRot = FQuat::FindBetweenNormals(LBarrel, LWant) * LRot;
		}
		const FQuat LWanted = LRot * LBase.Inverse();
		LanceTurn = bLanceTurnSet ? FQuat::Slerp(LanceTurn, LWanted, FMath::Clamp((double)Dt * 14.0, 0.0, 1.0)) : LWanted;
		LanceTurn.Normalize();
		bLanceTurnSet = true;
		Hold.SetRotation(LanceTurn * LBase);
	}
	else bLanceTurnSet = false;
	ToolHolder->SetRelativeTransform(Hold);
	UpdateAvatar(Dt);   // [hands] the body, its hands round the tool (the tool brought to the hand when it can't reach)
	UpdateFlying(Dt, bWorking && WorkBlend > 0.5 && Tool == ETorqueTool::Repair);   // (the lance's water: UpdateWashFx)
	UpdateSprayFx(Dt);   // the spray itself: the jet, the overspray, the footprint (TorqueWalkerSpray.cpp)
	UpdateWashFx(Dt);    // the pressure washer's jet, its mist and splash (TorqueWalkerWash.cpp)
}

void ATorqueWalker::UpdateFlying(float Dt, bool bEmit)
{
	if (!CubeMesh) return;
	if (FlyMaterials.Num() == 0)
	{
		FlyMaterials.Add(TorqueToolModels::Finish(this, FLinearColor(1.0f, 0.55f, 0.15f), 0.0f, 1.0f, 40.0f));   // sparks glow
		FlyMaterials.Add(TorqueToolModels::Finish(this, FLinearColor(0.5f, 0.5f, 0.5f), 0.0f, 0.6f));            // paint mist
		FlyMaterials.Add(TorqueToolModels::Finish(this, FLinearColor(0.75f, 0.85f, 0.95f), 0.0f, 0.1f, 0.6f));   // water
	}
	const int32 Kind = Tool == ETorqueTool::Repair ? 0 : (TWI::Sprays(Tool) ? 1 : 2);
	if (Kind == 1 && FlyMaterials.IsValidIndex(1) && FlyMaterials[1])
	{
		FlyMaterials[1]->SetVectorParameterValue(TEXT("Color"), ShownCan);
		FlyMaterials[1]->SetVectorParameterValue(TEXT("Color2"), ShownCan);
	}
	// New bits off the nozzle, a stream while working.
	if (bEmit && ToolHolder)
	{
		const FTransform Nozzle = FTransform(ToolNozzle) * ToolHolder->GetComponentTransform();
		const FVector At = Nozzle.GetLocation();
		const FVector Ahead = Camera->GetForwardVector(), Up = Camera->GetUpVector(), Right = Camera->GetRightVector();
		const double Rate = Kind == 0 ? 160.0 : (Kind == 1 ? 90.0 : 140.0);
		FlyDebt += Rate * Dt;
		while (FlyDebt >= 1.0 && Flying.Num() < 220)
		{
			FlyDebt -= 1.0;
			FFlyBit Bt;
			Bt.Pos = At;
			Bt.Kind = Kind;
			if (Kind == 0)
			{
				// Sparks fan off the disc's edge, down and back towards you and out to the side.
				Bt.Vel = (Ahead * FMath::FRandRange(-0.6, 0.2) - Up * FMath::FRandRange(0.2, 1.0) + Right * FMath::FRandRange(-0.9, 0.9)).GetSafeNormal()
					* FMath::FRandRange(300.0, 700.0);
				Bt.Life = FMath::FRandRange(0.15f, 0.45f);
			}
			else if (Kind == 1)
			{
				Bt.Vel = (Ahead + Up * FMath::FRandRange(-0.12, 0.12) + Right * FMath::FRandRange(-0.12, 0.12)).GetSafeNormal() * FMath::FRandRange(250.0, 420.0);
				Bt.Life = FMath::FRandRange(0.3f, 0.5f);
				Bt.Grow = 3.0f;
			}
			else
			{
				Bt.Vel = (Ahead + Up * FMath::FRandRange(-0.04, 0.04) + Right * FMath::FRandRange(-0.06, 0.06)).GetSafeNormal() * FMath::FRandRange(700.0, 950.0);
				Bt.Life = FMath::FRandRange(0.18f, 0.3f);
				Bt.Grow = 2.0f;
			}
			Flying.Add(Bt);
		}
	}
	else FlyDebt = 0.0;
	// Fly, fall and fade.
	for (int32 I = Flying.Num() - 1; I >= 0; --I)
	{
		FFlyBit& Bt = Flying[I];
		Bt.Age += Dt;
		if (Bt.Age >= Bt.Life) { Flying.RemoveAtSwap(I); continue; }
		if (Bt.Kind == 0) Bt.Vel += FVector(0.0, 0.0, -980.0) * Dt;               // sparks fall
		else Bt.Vel *= FMath::Max(0.0, 1.0 - (Bt.Kind == 1 ? 6.0 : 2.0) * Dt);    // the mist and the spray slow in the air
		Bt.Pos += Bt.Vel * Dt;
	}
	while (FlyMeshes.Num() < Flying.Num())
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(CubeMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		FlyMeshes.Add(C);
	}
	for (int32 I = 0; I < FlyMeshes.Num(); ++I)
	{
		UStaticMeshComponent* C = FlyMeshes[I];
		if (!C) continue;
		if (I >= Flying.Num())
		{
			if (C->IsVisible()) C->SetVisibility(false);
			continue;
		}
		const FFlyBit& Bt = Flying[I];
		const float F = Bt.Age / FMath::Max(Bt.Life, 0.01f);
		UMaterialInstanceDynamic* M = FlyMaterials.IsValidIndex(Bt.Kind) ? FlyMaterials[Bt.Kind] : nullptr;
		if (M && C->GetMaterial(0) != M) C->SetMaterial(0, M);
		// A spark is a short streak along its flight; mist and water puffs grow and shrink away.
		FVector Size;
		FRotator Facing = FRotator::ZeroRotator;
		if (Bt.Kind == 0)
		{
			Size = FVector(FMath::Clamp(Bt.Vel.Size() * 0.004, 0.4, 2.5), 0.12, 0.12) * (1.0f - F * 0.5f);
			Facing = Bt.Vel.Rotation();
		}
		else
		{
			const double Puff = (Bt.Kind == 1 ? 0.25 : 0.18) * (1.0 + (Bt.Grow - 1.0) * F) * FMath::Sin(UE_DOUBLE_PI * FMath::Min(1.0f, F * 1.4f + 0.15f));
			Size = FVector(Puff);
		}
		C->SetWorldLocationAndRotation(Bt.Pos, Facing);
		C->SetWorldScale3D(Size / 100.0);   // the cube is a metre a side
		if (!C->IsVisible()) C->SetVisibility(true);
	}
}

// --- Cues --------------------------------------------------------------------------------------------

float ATorqueWalker::WorkFraction() const
{
	// Spraying: how much of the coat's on, to the mark where it counts.
	if (Spray.IsValid() && Spray->bLive) return FMath::Clamp(Spray->Last.Covered / TorqueSpray::Done, 0.0f, 1.0f);
	// Washing: how clean the bike is.   [wash]
	if (bWashFiring && WashJob.IsValid()) return FMath::Clamp(WashJob->Stats().Clean, 0.0f, 1.0f);
	const FPlan& P = (bWorkHeld || ActHeldFor > 0.0) ? Plan : AltPlan;
	if (Progress <= 0.0 || !P.bValid || P.Time <= 0.0) return 0.0f;
	return (float)FMath::Clamp(Progress / P.Time, 0.0, 1.0);
}

FLinearColor ATorqueWalker::SprayMistColour(bool* bOutClear) const
{
	// What's loaded, as a request, for its mist (anyone's: another player's walker too).
	const TArray<TWI::FLoad> L = TWI::Loads(ETorqueTool::Gun);
	const TWI::FLoad Load = L.IsValidIndex(SprayFinish) ? L[SprayFinish] : L[0];
	FTorqueShedRequest R;
	R.Kind = Load.Kind == TWI::FLoad::Primer ? ETorqueShedAct::Prime : (Load.Kind == TWI::FLoad::Coat ? ETorqueShedAct::Coat : ETorqueShedAct::Paint);
	R.Finish = Load.Id;
	R.Colour = SprayColour;
	if (bOutClear) *bOutClear = R.Kind == ETorqueShedAct::Coat && UTorqueBuildSubsystem::IsLacquer(R.Finish);
	const UTorqueBuildSubsystem* B = Shed();
	return B ? TorqueSpray::MistColour(*B, PeerOf(), R) : ShownCan;
}

FString ATorqueWalker::SprayFinishText() const
{
	const TArray<FString> Names = SprayLoadNames();
	return Names.IsValidIndex(SprayFinish) ? Names[SprayFinish] : FString();
}

TArray<FString> ATorqueWalker::SprayLoadNames() const
{
	TArray<FString> Out;
	for (const TWI::FLoad& L : TWI::Loads(ETorqueTool::Gun))
	{
		if (L.Kind == TWI::FLoad::Paint) Out.Add(UTorqueBuildSubsystem::FinishName(L.Id));
		else if (L.Kind == TWI::FLoad::Coat) Out.Add(UTorqueBuildSubsystem::CoatName(L.Id));
		else if (L.Kind == TWI::FLoad::Cup) Out.Add(TEXT("Cup"));
		else Out.Add(TEXT("Grey primer"));
	}
	return Out;
}

bool ATorqueWalker::SprayIsPaint() const
{
	const TArray<TWI::FLoad> L = TWI::Loads(ETorqueTool::Gun);
	return !L.IsValidIndex(SprayFinish) || L[SprayFinish].Kind == TWI::FLoad::Paint;
}

FString ATorqueWalker::CarryingText() const
{
	const UTorqueBuildSubsystem* B = Shed();
	const int32 Held = B ? B->HeldBy(B->Me()) : 0;
	return Held != 0 ? B->ItemTitle(Held) : FString();
}

void ATorqueWalker::CarryInput(float Dt, bool bOut, bool bIn, bool bTurnDown, bool bTurnBack)
{
	const UTorqueBuildSubsystem* B = Shed();
	const int32 Held = B ? B->HeldBy(B->Me()) : 0;
	if (Held != CarryFor)
	{
		// Picked up something new: it starts at arm's length, the way you face (walker.gd _push).
		CarryFor = Held;
		CarryReach = 0.0;
		HoldTurn = 0.0;
		TurnKeyFor = -1.0;
	}
	if (bOut) CarryReach = FMath::Clamp(CarryReach + TWI::CarryStep, TWI::CarryMin, TWI::CarryMax);
	if (bIn) CarryReach = FMath::Clamp(CarryReach - TWI::CarryStep, TWI::CarryMin, TWI::CarryMax);
	if (bTurnDown)
	{
		if (TurnKeyFor < 0.0)
		{
			TurnKeyFor = 0.0;
			TurnSign = bTurnBack ? -1 : 1;
		}
		TurnKeyFor += Dt;
		// Held: it turns freely while the key's down, from the angle it's at.
		if (TurnKeyFor >= TWI::TurnHold) HoldTurn = FRotator::NormalizeAxis(HoldTurn + TurnSign * TWI::TurnRate * Dt);
	}
	else if (TurnKeyFor >= 0.0)
	{
		// A tap: a quarter turn, onto the next quarter (walker.gd _stepped, Placement.snap_yaw).
		if (TurnKeyFor < TWI::TurnHold)
			HoldTurn = FRotator::NormalizeAxis(90.0 * FMath::RoundToDouble((HoldTurn + TurnSign * 90.0) / 90.0));
		TurnKeyFor = -1.0;
	}
}

FString ATorqueWalker::TestCarry(const FString& What, double Value)
{
	const UTorqueBuildSubsystem* B = Shed();
	const int32 Held = B ? B->HeldBy(B->Me()) : 0;
	if (Held == 0) return TEXT("not holding a part");
	if (Held != CarryFor)
	{
		CarryFor = Held;
		CarryReach = 0.0;
		HoldTurn = 0.0;
	}
	TurnKeyFor = -1.0;
	if (What == TEXT("reach")) CarryReach = FMath::Clamp(Value, TWI::CarryMin, TWI::CarryMax);
	else if (What == TEXT("wheel")) CarryReach = FMath::Clamp(CarryReach + TWI::CarryStep * FMath::RoundToDouble(Value), TWI::CarryMin, TWI::CarryMax);
	else if (What == TEXT("turn")) HoldTurn = FRotator::NormalizeAxis(Value);
	else if (What == TEXT("turntap"))
	{
		// As taps of R: each onto the next quarter (CarryInput).
		const int32 Taps = FMath::RoundToInt(Value);
		for (int32 I = 0; I < FMath::Abs(Taps); ++I)
			HoldTurn = FRotator::NormalizeAxis(90.0 * FMath::RoundToDouble((HoldTurn + (Taps < 0 ? -90.0 : 90.0)) / 90.0));
	}
	FindTarget();
	MakePlan();
	UpdateCues();
	return CarryText();
}

FString ATorqueWalker::CarryText() const
{
	int32 Shown = 0;
	for (const UStaticMeshComponent* C : GhostParts) if (C && C->IsVisible()) ++Shown;
	int32 Through = 0;
	for (const UStaticMeshComponent* C : GhostXRayParts) if (C && C->IsVisible()) ++Through;
	const FTransform& At = Plan.Request.Place;
	const bool bDrop = Plan.bValid && Plan.Request.Kind == ETorqueShedAct::Drop;
	return FString::Printf(TEXT("carry: reach %+.0f cm, turned %.0f deg, plan '%s'%s%s, drop %s, box %s (%d pieces, %d seen through)"),
		CarryReach, FRotator::ClampAxis(HoldTurn), Plan.bValid ? *Plan.Verb : TEXT("none"),
		Plan.Blocked.IsEmpty() ? TEXT("") : TEXT(" BLOCKED: "), *Plan.Blocked,
		bDrop ? *FString::Printf(TEXT("at (%.0f, %.0f, %.0f) yaw %.0f, %.0f cm ahead"), At.GetLocation().X, At.GetLocation().Y, At.GetLocation().Z,
			At.Rotator().Yaw, FVector::Dist2D(At.GetLocation(), GetActorLocation())) : TEXT("none (not on the floor)"),
		Shown == 0 ? TEXT("hidden") : (Plan.Blocked.IsEmpty() ? TEXT("blue") : TEXT("red")), Shown, Through);
}

FTransform ATorqueWalker::CarryDrop() const
{
	const UTorqueBuildSubsystem* B = Shed();
	const int32 Held = B ? B->HeldBy(B->Me()) : 0;
	UWorld* World = GetWorld();
	const double Yaw = GetControlRotation().Yaw;
	const FVector Flat = FRotator(0.0, Yaw, 0.0).Vector();
	// How far it reaches towards you and away, turned however you've turned it (walker.gd _push_transform).
	FVector Size(60.0, 30.0, 20.0);
	if (World)
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It)
			if (It->SizeOf(Held, Size)) break;
	const double Rad = FMath::DegreesToRadians(HoldTurn);
	const double Half = FMath::Abs(FMath::Cos(Rad)) * Size.X * 0.5 + FMath::Abs(FMath::Sin(Rad)) * Size.Y * 0.5;
	const double Base = 55.0 + Half;
	double Dist = Base + FMath::Clamp(CarryReach, TWI::CarryMin, TWI::CarryMax);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerCarry), false, this);
	if (World)
	{
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It) Params.AddIgnoredActor(*It);
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) Params.AddIgnoredActor(*It);
	}
	const FVector Feet = GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0);
	if (World && Dist > Base)
	{
		// Pushed out: not through a wall (looked for low down and at waist height, walker.gd _wall_gap).
		for (const double H : {30.0, 100.0})
		{
			FHitResult Hit;
			const FVector From = Feet + FVector(0.0, 0.0, H);
			if (World->LineTraceSingleByChannel(Hit, From, From + Flat * (Dist + Half + 10.0), ECC_Visibility, Params))
				Dist = FMath::Min(Dist, FMath::Max(Base, Hit.Distance - Half - 2.0));
		}
	}
	FVector At = Feet + Flat * Dist;
	// beenster (Nov 12): where the crosshair points. Looking down at the floor (or a low surface)
	// within reach, it goes down there; looking ahead or up, the wheel's reach as before.
	if (World)
		if (const APlayerController* AimPC = Cast<APlayerController>(GetController()); AimPC && AimPC->PlayerCameraManager)
		{
			const FVector Eye = AimPC->PlayerCameraManager->GetCameraLocation();
			const FVector Look = AimPC->PlayerCameraManager->GetCameraRotation().Vector();
			FHitResult Aim;
			const double AimRange = 55.0 + Half + TWI::CarryMax + 150.0;
			if (Look.Z < -0.05 && World->LineTraceSingleByChannel(Aim, Eye, Eye + Look * AimRange, ECC_Visibility, Params) &&
				Aim.ImpactNormal.Z > 0.7 && Aim.ImpactPoint.Z < Feet.Z + 60.0)
			{
				const FVector2D Off(Aim.ImpactPoint.X - Feet.X, Aim.ImpactPoint.Y - Feet.Y);
				const double Along = FMath::Clamp(Off.Size(), Base + TWI::CarryMin, Base + TWI::CarryMax);
				const FVector2D Dir2 = Off.SizeSquared() > 1.0 ? Off.GetSafeNormal() : FVector2D(Flat.X, Flat.Y);
				At = FVector(Feet.X + Dir2.X * Along, Feet.Y + Dir2.Y * Along, Feet.Z);
			}
		}
	FHitResult Down;
	const FVector Top(At.X, At.Y, Feet.Z + 120.0);
	if (World && World->LineTraceSingleByChannel(Down, Top, Top - FVector(0, 0, 400), ECC_Visibility, Params)) At.Z = Down.ImpactPoint.Z;
	else At.Z = Feet.Z;
	// A box or crate let go over another box on the floor goes on top of it (beenster, Nov 10: boxes
	// and crates stack), squared up on it when it's about over the middle; the pile no higher than the
	// delivery area's.
	const FTorqueItem* HeldIt = B ? B->Item(Held) : nullptr;
	double DropYaw = Yaw + HoldTurn;
	if (B && HeldIt && HeldIt->bBoxed)
	{
		const FVector Mine = TorqueShedLayout::DeliverySize(HeldIt->Kind);
		double Best = At.Z;
		for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
		{
			const FTorqueItem& Under = KV.Value;
			if (KV.Key == Held || Under.At != ETorqueAt::Floor || !Under.bBoxed) continue;
			const FVector S = TorqueShedLayout::DeliverySize(Under.Kind);
			const FVector Local = Under.Place.InverseTransformPosition(FVector(At.X, At.Y, Under.Place.GetLocation().Z));
			if (FMath::Abs(Local.X) > S.X * 0.5 || FMath::Abs(Local.Y) > S.Y * 0.5) continue;
			// Near its middle: squared up on it, the way you'd stack them.
			const bool bSquare = FMath::Abs(Local.X) < S.X * 0.3 && FMath::Abs(Local.Y) < S.Y * 0.3;
			const double UnderYaw = Under.Place.Rotator().Yaw;
			const double SquareYaw = UnderYaw + 90.0 * FMath::RoundToDouble(FRotator::NormalizeAxis(DropYaw - UnderYaw) / 90.0);
			// On a crate's lid boards, or on the battens across its ends when it reaches them.
			const bool bAcross = bSquare && FMath::Abs(FRotator::NormalizeAxis(SquareYaw - UnderYaw)) > 45.0
				&& FMath::Abs(FRotator::NormalizeAxis(SquareYaw - UnderYaw)) < 135.0;
			const FVector2D OnHalf = bSquare ? FVector2D(bAcross ? Mine.Y : Mine.X, bAcross ? Mine.X : Mine.Y) * 0.5
				: FVector2D(FMath::Max(Mine.X, Mine.Y) * 0.5);
			const double TopZ = Under.Place.GetLocation().Z
				+ TorqueShedDelivery::TopAt(Under.Kind, S, bSquare ? FVector2D::ZeroVector : FVector2D(Local.X, Local.Y), OnHalf);
			if (TopZ <= Best || TopZ + Mine.Z > TorqueShedLayout::DeliveryPileTop + 5.0) continue;
			Best = TopZ;
			if (bSquare)
			{
				At.X = Under.Place.GetLocation().X;
				At.Y = Under.Place.GetLocation().Y;
				DropYaw = SquareYaw;
			}
		}
		At.Z = Best;
	}
	return FTransform(FRotator(0.0, DropYaw, 0.0), At);
}

void ATorqueWalker::UpdateCues()
{
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World) return;
	// M_ShedHighlight comes from the shed's level script (Tools/ue_build_shed.py); without it, no cues.
	static UMaterialInterface* Glow = nullptr;
	static bool bLooked = false;
	if (!bLooked)
	{
		bLooked = true;
		Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedHighlight.M_ShedHighlight"));
		if (Glow) Glow->AddToRoot();
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedHighlight (run Tools/ue_build_shed.py): no highlight or footprint"));
		}
	}
	if (!Glow) return;
	auto Make = [this](const FLinearColor& C, float Opacity)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glow, this);
		M->SetVectorParameterValue(TEXT("Color"), C);
		M->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return M;
	};
	if (!LitMaterial)
	{
		LitMaterial = Make(FLinearColor(1.0f, 0.6f, 0.15f), 0.3f);
		LitFaintMaterial = Make(FLinearColor(1.0f, 0.6f, 0.15f), 0.16f);
		GhostMaterials.Add(Make(FLinearColor(1.0f, 1.0f, 1.0f), 0.16f));
		GhostMaterials.Add(Make(FLinearColor(1.0f, 1.0f, 1.0f), 0.8f));
		GhostMaterials.Add(Make(FLinearColor(1.0f, 0.25f, 0.2f), 0.2f));
		GhostMaterials.Add(Make(FLinearColor(1.0f, 0.25f, 0.2f), 0.8f));
		GhostMaterials.Add(Make(FLinearColor(0.35f, 0.85f, 1.0f), 0.1f));   // the box itself, faint (can, can't)
		GhostMaterials.Add(Make(FLinearColor(1.0f, 0.3f, 0.2f), 0.1f));
	}
	const int32 Held = B->HeldBy(B->Me());
	// What's looked at glows: a bike part's whole slot (not with the lance, which washes the bike).
	TArray<UStaticMeshComponent*> Now;
	UMaterialInterface* NowMat = LitMaterial;
	const bool bShow = !bComputer && !bRidingOut;
	if (bShow && Target == ETorqueLook::BikePart && Tool != ETorqueTool::Lance && TargetBay != INDEX_NONE)
	{
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
			if (It->Bay == TargetBay)
				for (UStaticMeshComponent* C : It->Parts)
					if (C && C->IsVisible() && TorqueShedModel::SlotTag(C) == TargetSlot) Now.Add(C);
	}
	else if (bShow && Target == ETorqueLook::Item && TargetItem != 0 && Held == 0)
	{
		TArray<const UStaticMeshComponent*> Meshes;
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) It->MeshesOf(TargetItem, Meshes);
		for (const UStaticMeshComponent* C : Meshes) Now.Add(const_cast<UStaticMeshComponent*>(C));
		NowMat = LitFaintMaterial;
	}
	// (A part being sprayed shows its wet coat instead: the glow leaves it be.)
	if (SprayItem != 0) Now.Reset();
	Now.RemoveAll([this](const UStaticMeshComponent* C) { return SprayOwns(C) || WashOwns(C); });
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : Lit)
		if (UStaticMeshComponent* C = W.Get())
			if (!Now.Contains(C) && (C->GetOverlayMaterial() == LitMaterial || C->GetOverlayMaterial() == LitFaintMaterial)) C->SetOverlayMaterial(nullptr);
	Lit.Reset();
	for (UStaticMeshComponent* C : Now)
	{
		if (C->GetOverlayMaterial() != NowMat) C->SetOverlayMaterial(NowMat);
		Lit.Add(C);
	}
	// The same, with a thin soft line round it (TorqueLookOutline). Only bike parts and things that
	// move (beenster, Nov 9: "Only bike parts and objects which can be moved need highlighting"): with
	// empty hands, the engine stand, the extinguisher, the tape roll or a paint tin too, never the
	// fixed kit (benches, machines, bins, the computer, hooks, switches, the radio).
	if (LookOutline.Attach(this))
	{
		TArray<UPrimitiveComponent*> Lined;
		for (UStaticMeshComponent* C : Now) Lined.Add(C);
		if (bShow && Now.Num() == 0 && Held == 0 && Target == ETorqueLook::Place && TargetPlace && SprayItem == 0
			&& FTorqueLookOutline::Movable(TargetPlace->Kind))
			Lined.Append(LookOutline.PlaceMeshes(World, TargetPlace));
		if (bShow && Held == 0 && SprayItem == 0) FurnitureOutline(Lined);   // [furniture]
		if (bTestNoOutline) Lined.Reset();   // [outline test] the screen without it
		LookOutline.Show(Lined);
	}
	// A part in your hands: where it will go down (the floor, a bench, a machine), as a box its size
	// with bright edges, a notch on its front edge, glowing so it shows in the lit shed, and its
	// edges seen faintly through whatever's in the way (the part in your hands). Whatever tool was
	// last picked: your hands are full.
	bool bGhost = false;
	FTransform At = FTransform::Identity;
	if (bShow && Held != 0 && Plan.bValid)
	{
		if (Plan.Request.Kind == ETorqueShedAct::Drop)
		{
			At = Plan.Request.Place;
			bGhost = true;
		}
		else if (Plan.Request.Kind == ETorqueShedAct::Place)
		{
			if (const FTorquePlace* P = TorqueShedLayout::FindPlace(Plan.Request.Spot))
			{
				At = FTransform(P->Put);
				if (P->Kind == ETorquePlace::Bench) At.SetRotation(FRotator(0.0, 90.0, 0.0).Quaternion());
				bGhost = true;
			}
		}
	}
	FVector Size(60.0, 30.0, 20.0);
	if (bGhost)
	{
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It)
			if (It->SizeOf(Held, Size)) break;
		Size.X = FMath::Clamp(Size.X, 10.0, 220.0);
		Size.Y = FMath::Clamp(Size.Y, 8.0, 160.0);
		Size.Z = FMath::Clamp(Size.Z, 4.0, 160.0);
	}
	static UMaterialInterface* XRay = nullptr;
	static bool bLookedXRay = false;
	if (!bLookedXRay)
	{
		bLookedXRay = true;
		XRay = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedGhostXRay.M_ShedGhostXRay"));
		if (XRay) XRay->AddToRoot();
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedGhostXRay (run Tools/ue_build_shed.py): the drop box isn't seen through things"));
		}
	}
	if (XRay && GhostXRayMaterials.Num() == 0)
		for (const FLinearColor& C : {FLinearColor(1.0f, 0.42f, 0.03f), FLinearColor(1.0f, 0.12f, 0.1f)})
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(XRay, this);
			M->SetVectorParameterValue(TEXT("Color"), C * 2.0f);
			M->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
			GhostXRayMaterials.Add(M);
		}
	// The pieces, in the footprint's frame (its bottom centre, X ahead): only the floor the part will
	// take up (beenster, Nov 9: "a thinner line, be orange and only show the floor space that the
	// placed part will occupy"): a faint fill and a thin line round its edge, no box, no notch.
	const double Bar = 0.6;
	const double X = Size.X * 0.5, Y = Size.Y * 0.5;
	struct FPiece { FVector Size, Offset; int32 Kind; };   // 0 floor fill, 2 edge
	TArray<FPiece, TInlineAllocator<8>> Pieces;
	Pieces.Add({FVector(Size.X, Size.Y, 0.2), FVector(0, 0, 0.25), 0});
	const double Up = 0.45;
	Pieces.Add({FVector(Size.X + Bar, Bar, Bar * 0.5), FVector(0, -Y, Up), 2});
	Pieces.Add({FVector(Size.X + Bar, Bar, Bar * 0.5), FVector(0, Y, Up), 2});
	Pieces.Add({FVector(Bar, Size.Y + Bar, Bar * 0.5), FVector(-X, 0, Up), 2});
	Pieces.Add({FVector(Bar, Size.Y + Bar, Bar * 0.5), FVector(X, 0, Up), 2});
	auto MakePart = [this](TArray<UStaticMeshComponent*>& Into)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(CubeMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		Into.Add(C);
	};
	while (GhostParts.Num() < Pieces.Num() && CubeMesh) MakePart(GhostParts);
	while (XRay && GhostXRayParts.Num() < Pieces.Num() && CubeMesh) MakePart(GhostXRayParts);
	const bool bOk = Plan.Blocked.IsEmpty();
	// Orange where it can go (beenster, Nov 12: "where your mouse is pointing (and orange)"; it was
	// yellow after build 135), red where it can't; steady, no pulse.
	if (GhostMaterials.Num() >= 4)
	{
		// fill, edge (can), fill, edge (can't): bright colours over 1, as the glow is in light units.
		const FLinearColor Can(1.0f, 0.42f, 0.03f), Cannot(1.0f, 0.12f, 0.1f);
		GhostMaterials[0]->SetVectorParameterValue(TEXT("Color"), Can * 3.0f);
		GhostMaterials[0]->SetScalarParameterValue(TEXT("Opacity"), 0.22f);
		GhostMaterials[1]->SetVectorParameterValue(TEXT("Color"), Can * 6.0f);
		GhostMaterials[1]->SetScalarParameterValue(TEXT("Opacity"), 0.95f);
		GhostMaterials[2]->SetVectorParameterValue(TEXT("Color"), Cannot * 3.0f);
		GhostMaterials[2]->SetScalarParameterValue(TEXT("Opacity"), 0.22f);
		GhostMaterials[3]->SetVectorParameterValue(TEXT("Color"), Cannot * 6.0f);
		GhostMaterials[3]->SetScalarParameterValue(TEXT("Opacity"), 0.95f);
	}
	auto Lay = [&](TArray<UStaticMeshComponent*>& Parts, bool bThrough)
	{
		for (int32 I = 0; I < Parts.Num(); ++I)
		{
			UStaticMeshComponent* C = Parts[I];
			if (!C) continue;
			// Through things, only the edges.
			if (!bGhost || !Pieces.IsValidIndex(I) || (bThrough && Pieces[I].Kind != 2))
			{
				if (C->IsVisible()) C->SetVisibility(false);
				continue;
			}
			UMaterialInstanceDynamic* M = nullptr;
			if (bThrough) M = GhostXRayMaterials.IsValidIndex(bOk ? 0 : 1) ? GhostXRayMaterials[bOk ? 0 : 1] : nullptr;
			else
			{
				const int32 Which = Pieces[I].Kind == 1 ? (bOk ? 4 : 5) : (bOk ? 0 : 2) + (Pieces[I].Kind == 2 ? 1 : 0);
				M = GhostMaterials.IsValidIndex(Which) ? GhostMaterials[Which] : nullptr;
			}
			if (M && C->GetMaterial(0) != M) C->SetMaterial(0, M);
			C->SetWorldLocationAndRotation(At.TransformPosition(Pieces[I].Offset), At.GetRotation());
			C->SetWorldScale3D(Pieces[I].Size / 100.0);
			if (!C->IsVisible()) C->SetVisibility(true);
		}
	};
	Lay(GhostParts, false);
	Lay(GhostXRayParts, true);
}

// --- Wheeling a bike (walker.gd _push_bike) ---------------------------------------------------------

int32 ATorqueWalker::Wheeling() const
{
	const UTorqueBuildSubsystem* B = Shed();
	return B ? B->PushedBy(B->Me()) : INDEX_NONE;
}

FTransform ATorqueWalker::PushTransform() const
{
	// Alongside you holding the bars (walker.gd: 0.75 m to the right, a little behind), facing your way.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const double Yaw = PC ? PC->GetControlRotation().Yaw : GetActorRotation().Yaw;
	const FRotator Heading(0.0, Yaw, 0.0);
	const FVector Ahead = Heading.Vector(), Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	const FVector At = GetActorLocation() + Right * 85.0 - Ahead * 25.0;
	return FTransform(Heading, FVector(At.X, At.Y, 0.0));
}

ATorqueShedBike* ATorqueWalker::BikeActor(int32 Bay) const
{
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
			if (It->Bay == Bay) return *It;
	return nullptr;
}

void ATorqueWalker::PushBike(float Dt)
{
	const int32 Bay = Wheeling();
	UTorqueBuildSubsystem* B = Shed();
	if (!B || Bay == INDEX_NONE) return;
	Target = ETorqueLook::None;
	TargetBay = INDEX_NONE;
	TargetItem = 0;
	TargetPlace = nullptr;
	Plan = FPlan();
	AltPlan = FPlan();
	Progress = 0.0;
	const FTransform Xf = PushTransform();
	if (ATorqueShedBike* Bike = BikeActor(Bay)) Bike->Preview(Xf);
	// [co-op] No MoveBike on the way: everyone else works out where the bike is from where the one
	// wheeling it walks (their movement already comes over the net: FollowHands), and the shed
	// hears where it ends up at ReleaseBike. (Each MoveBike made the host resend the whole shed.)
}

void ATorqueWalker::LetGo(int32 Bay)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::ReleaseBike;
	R.Bay = Bay;
	R.Place = PushTransform();
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		Say(WhyNot, false);
		UE_LOG(LogTemp, Log, TEXT("Torque: let go refused: %s"), *WhyNot);
	}
	else
	{
		Doing(FString(), 0.0);
		UE_LOG(LogTemp, Log, TEXT("Torque: let go, park: %s"), *BikeWhere(Bay));
	}
	// [co-op] Held where it was let go until the shed's word comes back (the bike drops the preview
	// itself once no one's wheeling it, then glides to where the shed put it: its bay, the rig...).
	if (ATorqueShedBike* Bike = BikeActor(Bay)) Bike->Preview(R.Place);
}

// --- F -------------------------------------------------------------------------------------------

// [tv] The wall TV looked at on to its next channel, for everyone (Torque Shed's tv_channels.gd).
void ATorqueWalker::PasteTvVideo()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FString Clip;
	FPlatformApplicationMisc::ClipboardPaste(Clip);
	const FString Id = TorqueShedTv::VideoId(Clip);
	if (Id.IsEmpty())
	{
		Say(TEXT("Copy a YouTube link first (from the browser's address bar or Share), then Ctrl+V at the TV."), false);
		return;
	}
	FTorqueShedRequest V;
	V.Kind = ETorqueShedAct::TvVideo;
	V.Text = Id;
	FString WhyNot;
	if (!B->Request(V, WhyNot))
	{
		Say(WhyNot, false);
		return;
	}
	// The TV looked at goes to YouTube (if it isn't on it already).
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::TvChannel;
	R.Colour = TorqueShedTv::YouTube;
	if (const FTorqueFurniture* Piece = TvPiece())
	{
		if (Piece->Channel == TorqueShedTv::YouTube) return;
		R.Spot = TorqueShedTv::PieceSpot;
		R.Item = Piece->Id;
	}
	else if (TargetPlace && TargetPlace->Kind == ETorquePlace::Tv)
	{
		if (B->TvChannel(TargetPlace->Index) == TorqueShedTv::YouTube) return;
		R.Item = TargetPlace->Index;
	}
	else return;
	if (!B->Request(R, WhyNot)) Say(WhyNot, false);
}

void ATorqueWalker::NextTvChannel()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::TvChannel;
	if (const FTorqueFurniture* Piece = TvPiece())
	{
		// A bought TV: the same channels (bar the bay's own page), its own.
		R.Spot = TorqueShedTv::PieceSpot;
		R.Item = Piece->Id;
		R.Colour = TorqueShedTv::NextPiece(Piece->Channel);
	}
	else if (TargetPlace && TargetPlace->Kind == ETorquePlace::Tv)
	{
		R.Item = TargetPlace->Index;
		R.Colour = TorqueShedTv::Next(R.Item, B->TvChannel(R.Item));
	}
	else return;
	FString WhyNot;
	if (!B->Request(R, WhyNot)) Say(WhyNot, false);
}

void ATorqueWalker::Interact()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	const FTorqueItem* Thing = B->Item(TargetItem);
	if (Target == ETorqueLook::Item && Thing && Thing->bBoxed)
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Unpack;
		R.Item = TargetItem;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		else Doing(FString::Printf(TEXT("unpacking the %s"), *B->ItemTitle(TargetItem).ToLower()));
		return;
	}
	if (TinInteract()) return;   // the mixing bay's bench, its tin shelf, a tin into a bin
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Tape)
	{
		// The masking tape's hook (walker.gd interact "tape"): take it, or hang it back.
		FTorqueShedRequest R;
		R.Kind = B->PaintBay().TapeBy == B->Me() ? ETorqueShedAct::PaintTapePut : ETorqueShedAct::PaintTapeTake;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Computer)
	{
		OpenComputer();
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Pump)
	{
		// [fluids] The pump's on right mouse now (FluidInput); F just says so.
		Say(TEXT("Right mouse (R / Square) lifts the nozzle off the pump."), true);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Lift)
	{
		// A bike lift's button post (walker.gd interact "lift"): up if it's down, down if it's up.
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Lift;
		R.Item = TargetPlace->Index;
		R.Colour = B->LiftUp(TargetPlace->Index) ? 0 : 1;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Radio)
	{
		// The radio's switch (walker.gd: interact at the radio).
		UE_LOG(LogTemp, Log, TEXT("Torque radio: F at the radio (it's %s)"), B->RadioOn() ? TEXT("on") : TEXT("off"));
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Radio;
		R.Item = B->RadioOn() ? 0 : 1;
		R.Colour = B->RadioStation();
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		return;
	}
	if (TargetPlace && (TargetPlace->Kind == ETorquePlace::ShedSwitch || TargetPlace->Kind == ETorquePlace::Extinguisher))
	{
		// [kit] The light switch or the roller door's box right of the door (up if it's down, down if
		// it's up); the fire extinguisher off its place (the work button sprays it, F puts it back).
		FTorqueShedRequest R;
		if (TargetPlace->Kind == ETorquePlace::ShedSwitch)
		{
			R.Kind = TargetPlace->Index == 0 ? ETorqueShedAct::ShedLights : ETorqueShedAct::Door;
			R.Item = TargetPlace->Index == 0 ? (B->ShedLightsOn() ? 0 : 1) : (B->DoorUp() ? 0 : 1);
		}
		else
		{
			R.Kind = ETorqueShedAct::Extinguisher;
			R.Item = 1;
		}
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		else if (R.Kind == ETorqueShedAct::Extinguisher) Doing(TEXT("holding the fire extinguisher"), 6.0);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoDesk)
	{
		OpenDesk();
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoSwitch)
	{
		// The dyno room: the switches by the door.
		FTorqueShedRequest R;
		R.Kind = TargetPlace->Index == 0 ? ETorqueShedAct::DynoLights : ETorqueShedAct::DynoFan;
		R.Bay = B->DynoBay();
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		return;
	}
	if (Target == ETorqueLook::BikePart || Target == ETorqueLook::Door || (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bay))
	{
		StartRideOut(Target == ETorqueLook::Door ? B->MyBike() : TargetBay, false, Target != ETorqueLook::Door);
		return;
	}
	Say(TEXT("F: unpack a box, use the parts computer, or ride out (at your bike or the roller door)"), true, 2.5);
}

void ATorqueWalker::StartAdventure()
{
	const UTorqueBuildSubsystem* B = Shed();
	if (B) StartRideOut(B->MyBike(), true);
}

void ATorqueWalker::StartRideOut(int32 Bay, bool bAdventure, bool bAtBike)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	if (!B->Exists(Bay))
	{
		Say(FString::Printf(TEXT("Bay %d is empty: press F at a bike to ride it"), Bay + 1), false, 2.5);
		return;
	}
	// (A bike parked out on the yard's pad, or in the wash bay, open to the yard, doesn't need the
	// door: it's out already.)
	const FVector Stands = B->BikeTransform(Bay).GetLocation();
	if (!B->DoorUp() && !(bAtBike && (Stands.Y > 600.0 || TorqueShedLayout::InWashBay(Stands))))   // [kit]
	{
		Say(TEXT("The roller door's down: open it at the yellow box right of the door (F)."), false, 3.0);
		return;
	}
	// A bike rides only with everything on it and bolted up (shed_state ride check).
	const TArray<FName> Missing = B->MissingSlots(Bay), Loose = B->LooseSlots(Bay);
	if (Missing.Num() > 0 || Loose.Num() > 0)
	{
		FString Why = FString::Printf(TEXT("%s isn't ready to ride."), *B->Bike(Bay).Name);
		if (Missing.Num() > 0) Why += TEXT(" Fit the ") + TWI::SlotList(Missing) + TEXT(".");
		if (Loose.Num() > 0) Why += TEXT(" Bolt up the ") + TWI::SlotList(Loose) + TEXT(".");
		Say(Why, false, 4.0);
		return;
	}
	if (B->LiftRaised(Bay))
	{
		Say(TEXT("It's up on the lift. Lower the lift first (F on the button post)."), false, 3.0);
		return;
	}
	const FString Fab = B->FabOwedText(Bay);
	if (!Fab.IsEmpty())
	{
		Say(FString::Printf(TEXT("It needs fabrication first: %s. Take the grinder and welder to it."), *Fab), false, 4.0);
		return;
	}
	if (B->HeldBy(B->Me()) != 0)
	{
		Say(TEXT("Put down what you're holding first."), false, 2.5);
		return;
	}
	if (bAdventure && GetNetMode() == NM_Client)
	{
		Say(TEXT("Only the host can set off on an adventure. Ride in the yard till they do."), false, 3.0);
		return;
	}
	B->SetMyBike(Bay);
	if (!bAdventure)
	{
		// [yard] On it where it stands (at the door: just outside), to ride it out of the roller door
		// and back in (Shed/TorqueYard.h); the host's pawn swap, asked for over the net by a joined player.
		Doing(FString::Printf(TEXT("riding bay %d's bike in the yard"), Bay + 1), 30.0);
		TorqueYard::RideOut(Cast<APlayerController>(GetController()), Bay, bAtBike);
		return;
	}
	Doing(FString::Printf(TEXT("riding out on bay %d's bike"), Bay + 1), 30.0);
	bRidingOut = true;
	RideOutIn = 0.4;   // long enough to show the line below
}

// --- The parts computer -----------------------------------------------------------------------------

void ATorqueWalker::OpenComputer()
{
	// parts_computer.gd: a page over the screen, worked with the mouse (the pad works it too).
	Computer.Open();
	bComputer = true;
	Progress = 0.0;
	bWaitRelease = true;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	Doing(TEXT("on the parts computer"), 6.0);
}

void ATorqueWalker::CloseComputer()
{
	if (!bComputer) return;
	if (bDesk)
	{
		bDesk = false;   // (first: DeskDo's close comes back here)
		FTorqueDeskOut Out = Desk.Close();
		Out.bClose = false;
		DeskDo(Out);
	}
	bComputer = false;
	bWaitRelease = true;
	ComputerClosedFrame = GFrameCounter;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ATorqueWalker::ComputerDo(const FTorqueComputerAction& A)
{
	switch (A.Kind)
	{
	case FTorqueComputerAction::EKind::Close:
		CloseComputer();
		break;
	case FTorqueComputerAction::EKind::OrderBike:
		Order(FName(TEXT("bike")));
		break;
	case FTorqueComputerAction::EKind::OrderPart:
		Order(A.Part);
		break;
	case FTorqueComputerAction::EKind::OrderNewBike:
		if (UTorqueBuildSubsystem* B = Shed())
		{
			// The New bike configurator's bike: the host builds it into a free bay (a project bike
			// request carrying the order).
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::OrderBike;
			R.Text = A.Order;
			FString WhyNot;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
			else Doing(TEXT("ordering a new bike"));
		}
		break;
	case FTorqueComputerAction::EKind::OrderKit:
		if (UTorqueBuildSubsystem* B = Shed())
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::OrderFurniture;
			R.Part = A.Part;
			FString WhyNot;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
			else if (const TorqueFurniture::FModel* M = TorqueFurniture::Find(A.Part))
				Say(FString::Printf(TEXT("%s ordered: it's %s."), *M->Name, M->bYard ? TEXT("in the yard kit drop-off out on the pad") : TEXT("in the delivery area outside the roller door")), true, 4.0);
		}
		break;
	case FTorqueComputerAction::EKind::BuyPaint:
		if (UTorqueBuildSubsystem* B = Shed())
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::PaintBuy;
			R.Colour = A.Stock;
			R.Item = FMath::Max(A.Count, 1);
			FString WhyNot;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
			else Doing(TEXT("restocking the mixing bay"));
		}
		break;
	default:
		break;
	}
}

void ATorqueWalker::OpenDesk()
{
	// tuning_computer.gd open: the bike on the rig, or the nearest within reach.
	OpenComputer();
	bDesk = true;
	Desk.Open(Shed(), GetActorLocation());
	Doing(TEXT("at the dyno desk"), 6.0);
}

void ATorqueWalker::DeskDo(const FTorqueDeskOut& Out)
{
	UTorqueBuildSubsystem* B = Shed();
	for (const FTorqueShedRequest& R : Out.Requests)
	{
		if (!B) break;
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		else if (R.Kind == ETorqueShedAct::DynoRun)
		{
			B->SetMyBike(R.Bay);
			Doing(FString::Printf(TEXT("running bay %d's bike on the dyno"), R.Bay + 1), 10.0);
		}
		else if (R.Kind == ETorqueShedAct::Tune) Doing(FString::Printf(TEXT("tuning bay %d's bike"), R.Bay + 1), 4.0);
	}
	if (Out.bClose) CloseComputer();
}

void ATorqueWalker::DrawComputer(AHUD* Hud, UCanvas* Canvas) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	FVector2D Mouse(-1.0, -1.0);
	float Mx = 0.0f, My = 0.0f;
	if (PC && PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
	if (bDesk)
	{
		Desk.Draw(Hud, Canvas, Shed(), Mouse, NoteFor > 0.0 ? Note : FString(), bNoteOk);
		return;
	}
	Computer.Draw(Hud, Canvas, Shed(), Mouse, NoteFor > 0.0 ? Note : FString(), bNoteOk);
}

FString ATorqueWalker::TestComputer(FName Kind, FName Part, bool bOrder)
{
	if (!bComputer) OpenComputer();
	if (!Kind.IsNone() && !Computer.PickKind(Kind)) return FString::Printf(TEXT("no kind %s on the computer"), *Kind.ToString());
	if (!Part.IsNone() && !Computer.PickPart(Part)) return FString::Printf(TEXT("no %s under %s"), *Part.ToString(), *Kind.ToString());
	{
		const APlayerController* MyPC = Cast<APlayerController>(GetController());
		Computer.SyncPreview(MyPC ? MyPC->GetHUD() : nullptr);   // (the pick is made after this frame's Draw)
	}
	FString Out = Computer.Describe() + TEXT(" [3D preview: ") + Computer.PreviewShowing() + TEXT("]");
	if (bOrder)
	{
		Note.Reset();
		ComputerDo(Computer.Pad(0, 0, true));
		Out += TEXT(": ") + (Note.IsEmpty() ? FString(TEXT("ordered")) : Note);
	}
	return Out;
}

FString ATorqueWalker::TestComputerType(const FString& Type)
{
	if (!bComputer) OpenComputer();
	if (!Computer.PickBikeType(Type)) return FString::Printf(TEXT("no bike type %s on the computer"), *Type);
	return Computer.Describe();
}

FString ATorqueWalker::TestColour(const TArray<FString>& Args)
{
	if (!bComputer) OpenComputer();
	Computer.OpenNewBike();   // (already on it: stays as it is, the picker open)
	return Computer.NewBike().TestColour(Args);
}

FString ATorqueWalker::TestNewBike(const FString& Style, const TArray<FString>& Swaps, bool bOrder)
{
	if (!bComputer) OpenComputer();
	Computer.OpenNewBike();
	FString Out = Computer.NewBike().TestSetup(Style, Swaps) + TEXT(" [3D preview: ") + Computer.PreviewShowing() + TEXT("]");
	if (bOrder)
	{
		Note.Reset();
		FTorqueComputerAction A;
		A.Kind = FTorqueComputerAction::EKind::OrderNewBike;
		A.Order = Computer.NewBike().OrderText();
		ComputerDo(A);
		Out += TEXT(": ") + (Note.IsEmpty() ? FString(TEXT("ordered")) : Note);
		// Where the tank's badge went (BadgeOf: the tank item carries it).
		if (const UTorqueBuildSubsystem* Sh = Shed())
			for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
				if (!Sh->BadgeOf(Bay).IsEmpty()) Out += FString::Printf(TEXT(" [bay %d's tank badge: %s]"), Bay + 1, *Sh->BadgeOf(Bay));
	}
	return Out;
}

FString ATorqueWalker::Order(FName Part)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return TEXT("no shed");
	FTorqueShedRequest R;
	if (Part == FName(TEXT("bike"))) R.Kind = ETorqueShedAct::OrderBike;
	else
	{
		R.Kind = ETorqueShedAct::OrderPart;
		R.Part = Part;
	}
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		Say(WhyNot, false);
		return WhyNot;
	}
	Doing(R.Kind == ETorqueShedAct::OrderBike ? FString(TEXT("ordering a project bike")) : FString::Printf(TEXT("ordering the %s"), *TorqueShedParts::PartName(Part).ToLower()));
	return Note;
}

// --- The HUD ------------------------------------------------------------------------------------------

void ATorqueWalker::UpdateHud()
{
	HudLines.Reset();
	if (bRidingOut)
	{
		HudLines.Add({TEXT("Riding out to Buttertubs..."), FColor::White, 1.0f});
		return;
	}
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	const FColor Title(190, 210, 255), Prompt(255, 214, 120), Blocked(255, 140, 120), Help(200, 200, 200), Info(220, 220, 220);
	if (const int32 Pushed = Wheeling(); Pushed != INDEX_NONE)
	{
		HudLines.Add({FString::Printf(TEXT("Wheeling %s"), *B->Bike(Pushed).Name), FColor::White, 0.8f});
		HudLines.Add({TEXT("Walk to push it, back up to pull it. Bikes don't have reverse."), Info, 0.6f});
		HudLines.Add({TEXT("F / Triangle: let go (into its bay, onto the dyno rig, or on its stand)"), Prompt, 0.65f});
		if (NoteFor > 0.0) HudLines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
		return;
	}
	if (bComputer) return;   // the parts computer's page instead (DrawComputer)
	if (bBench) return;   // the bench's card instead (DrawBench)
	if (FurnitureHud(HudLines)) return;   // [furniture] moving a piece of kit
	// The tool, and the spray gun's paint.
	FString ToolLine = FString::Printf(TEXT("%s (Q / E)"), TWI::ToolName(Tool));
	if (TWI::Sprays(Tool))
	{
		if (SprayIsPaint())
			ToolLine += FString::Printf(TEXT("   %s %s (R colour, T finish)"), *SprayFinishText(), *UTorqueBuildSubsystem::PaintName(SprayColour));
		else ToolLine += FString::Printf(TEXT("   %s (T next)"), *SprayFinishText());
		if (Tool == ETorqueTool::Gun) ToolLine += TEXT("   runs off the spray bay's air line: hang the part on a hook there");
	}
	if (bCrouched) ToolLine += TEXT("   crouching");
	HudLines.Add({ToolLine, Title, 0.6f});
	if (const int32 Held = B->HeldBy(B->Me()))
	{
		HudLines.Add({TEXT("Holding: ") + B->ItemTitle(Held), Info, 0.6f});
		if (!B->PaintBay().HeldTin(B->Me()) && B->PaintBay().TapeBy != B->Me())
			HudLines.Add({FString::Printf(TEXT("Wheel (Q / E): put it down nearer or further (%+.2f m)   R: turn it, tap a quarter or hold to turn (Shift+R the other way, %.0f deg)"),
				CarryReach / 100.0, FRotator::ClampAxis(HoldTurn)), Prompt, 0.55f});
	}
	if (const FTorqueTin* Tin = B->PaintBay().HeldTin(B->Me()))
	{
		HudLines.Add({TEXT("Holding a tin: ") + B->PaintBay().TinText(*Tin), Info, 0.6f});
		HudLines.Add({TEXT("R / Square: fill the spray gun's cup from it   F: onto the tin shelf, the bench's scale, or into a bin"), Prompt, 0.6f});
	}
	if (B->PaintBay().TapeBy == B->Me())
	{
		HudLines.Add({FString::Printf(TEXT("Holding the masking tape: %s"), *FString(TorquePaintMix::PatternName(B->PaintBay().TapePattern)).ToLower()), Info, 0.6f});
		HudLines.Add({TEXT("R / Square: the pattern   Hold the work button on a painted part: tape it   F on its hook: hang it back"), Prompt, 0.6f});
	}
	if (Tool == ETorqueTool::Gun)
		if (const FTorquePaintCup* Cup = B->PaintBay().Cups.Find(B->Me()))
			HudLines.Add({FString::Printf(TEXT("Gun's cup: %s, %.2f l (load Cup with T)"), *Cup->Spec.Name, Cup->Litres), Info, 0.55f});
	// What's looked at: hud.gd's panel at the side (the title, its lines, then the prompts).
	const int32 LookFrom = HudLines.Num();
	const FTorqueItem* Thing = B->Item(TargetItem);
	switch (Target)
	{
	case ETorqueLook::BikePart:
	{
		const FString Bike = B->Exists(TargetBay) ? B->Bike(TargetBay).Name : FString(TEXT("empty"));
		HudLines.Add({FString::Printf(TEXT("Bay %d: %s%s"), TargetBay + 1, *Bike, TargetBay == B->MyBike() ? TEXT("   (your bike)") : TEXT("")), Title, 0.6f});
		HudLines.Add({FString::Printf(TEXT("%s: %s   %s"), *TorqueShedParts::SlotName(TargetSlot), *B->ItemTitle(TargetItem),
			B->IsBolted(TargetBay, TargetSlot) ? TEXT("bolted") : TEXT("loose")), FColor::White, 0.8f});
		if (Thing) HudLines.Add({B->ConditionText(TargetItem), Info, 0.55f});
		if (Thing && Thing->Kind == FName(TEXT("wheel")))
		{
			const FString Fits = UTorqueBuildSubsystem::TyresThatFit(Thing->Part);
			if (!Fits.IsEmpty()) HudLines.Add({Fits, Info, 0.55f});
		}
		if (B->Exists(TargetBay))
			HudLines.Add({FString::Printf(TEXT("%s: geometry mode (fit this bike's parts)"), *FTorqueKeys::Text(ETorqueKey::GeometryMode)), Prompt, 0.55f});
		const FString Fab = B->FabOwedText(TargetBay);
		if (!Fab.IsEmpty()) HudLines.Add({TEXT("Needs fabrication: ") + Fab + TEXT(" (grinder and welder)"), Info, 0.55f});
		break;
	}
	case ETorqueLook::Item:
		if (Thing)
		{
			FString Where = TargetPlace ? TEXT("   on the ") + TWI::PlaceName(*TargetPlace, B).ToLower() : FString();
			if (TargetEngine != 0) Where = FString::Printf(TEXT("   %s on the engine, %s"), *TorqueShedParts::SlotName(TargetSlot).ToLower(),
				B->IsBolted(INDEX_NONE, TargetSlot, TargetEngine) ? TEXT("bolted") : TEXT("loose"));
			HudLines.Add({B->ItemTitle(TargetItem) + Where, FColor::White, 0.8f});
			HudLines.Add({B->ConditionText(TargetItem), Info, 0.55f});
			if (Thing->Tyre != 0) HudLines.Add({TEXT("Tyre: ") + B->ItemTitle(Thing->Tyre), Info, 0.55f});
			if (Thing->Kind == FName(TEXT("wheel")) && !Thing->bBoxed)
			{
				const FString Fits = UTorqueBuildSubsystem::TyresThatFit(Thing->Part);
				if (!Fits.IsEmpty()) HudLines.Add({Fits, Info, 0.55f});
			}
			if (Thing->bBoxed) HudLines.Add({TEXT("F / Triangle: unpack it"), Prompt, 0.65f});
		}
		break;
	case ETorqueLook::Place:
		if (TargetPlace)
		{
			HudLines.Add({TWI::PlaceName(*TargetPlace, B), FColor::White, 0.8f});
			if (TargetPlace->Kind == ETorquePlace::Computer) HudLines.Add({TEXT("F / Triangle (or left mouse): order parts and project bikes"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::DynoSwitch) HudLines.Add({TEXT("F / Triangle: flick the switch"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Tv)   // [tv]
				HudLines.Add({FString::Printf(TEXT("Right mouse / Square: change channel (next: %s)"),
					TorqueShedTv::Name(TorqueShedTv::Next(TargetPlace->Index, B->TvChannel(TargetPlace->Index)))), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Tv && B->TvChannel(TargetPlace->Index) == TorqueShedTv::YouTube)   // [tv]
				HudLines.Add({TEXT("Ctrl+V: play the YouTube link you've copied"), Prompt, 0.55f});
			if (TargetPlace->Kind == ETorquePlace::Lift)
				HudLines.Add({B->LiftUp(TargetPlace->Index) ? TEXT("F / Triangle: lower it") : TEXT("F / Triangle: raise it"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::ShedSwitch)   // [kit]
				HudLines.Add({TargetPlace->Index == 0 ? FString(TEXT("F / Triangle: flick the lights"))
					: FString(B->DoorUp() ? TEXT("F / Triangle: roller door down") : TEXT("F / Triangle: roller door up")), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Extinguisher && B->ExtinguisherHeldBy() == 0)
				HudLines.Add({TEXT("F / Triangle: take the fire extinguisher"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Pump)   // [fluids]
				HudLines.Add({B->PumpHeldBy() == B->Me() ? TEXT("Right mouse / Square: hang the nozzle back up")
					: (B->PumpHeldBy() == 0 ? TEXT("Right mouse / Square: lift the nozzle off the pump") : TEXT("Someone has the nozzle")), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Stand && B->HeldBy(B->Me()) == 0)
				HudLines.Add({TEXT("Hold F / Triangle: wheel the engine stand"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::Radio)
			{
				// What's on (walker.gd: the station, then the song), and its switch.
				TArray<FString> Playing;
				ATorqueShedRadio::NowPlaying(this).ParseIntoArray(Playing, TEXT("\n"));
				for (const FString& L : Playing) HudLines.Add({L, Info, 0.6f});
				HudLines.Add({B->RadioOn() ? TEXT("F / Triangle: switch it off") : TEXT("F / Triangle: switch it on"), Prompt, 0.65f});
			}
			if (TargetPlace->Kind == ETorquePlace::PaintBench)
				HudLines.Add({B->PaintBay().HeldTin(B->Me()) ? TEXT("F / Triangle: put the tin on the scale and step up to the bench")
					: TEXT("F / Triangle: step up to the mixing bench"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::PaintShelf)
			{
				const bool bHolding = B->PaintBay().HeldTin(B->Me()) != nullptr;
				const bool bThere = B->PaintBay().TinInSlot(TargetPlace->Index) != 0;
				if (bHolding && !bThere) HudLines.Add({TEXT("F / Triangle: put the tin down here"), Prompt, 0.65f});
				else if (!bHolding && bThere) HudLines.Add({TEXT("F / Triangle: take the tin"), Prompt, 0.65f});
			}
			if (TargetPlace->Kind == ETorquePlace::DynoDesk)
			{
				HudLines.Add({TEXT("F / Triangle (or left mouse): tune the bike and run the dyno"), Prompt, 0.65f});
				if (B->HasDynoRun(B->DynoBay()))
				{
					const FTorqueDynoRun& Last = B->DynoRunOf(B->DynoBay());
					HudLines.Add({FString::Printf(TEXT("Last run: %.1f hp at %.0f rpm, %.1f Nm at %.0f rpm (at the wheel)"), Last.PeakHp, Last.PeakHpRpm, Last.PeakNm, Last.PeakNmRpm), Info, 0.55f});
				}
			}
			if (TargetPlace->Kind == ETorquePlace::Bay && B->Exists(TargetPlace->Index)) HudLines.Add({TEXT("F / Triangle: get on and ride it"), Prompt, 0.65f});
		}
		break;
	case ETorqueLook::Door:
		HudLines.Add({TEXT("Roller door"), FColor::White, 0.8f});
		HudLines.Add({FString::Printf(TEXT("F / Triangle: ride bay %d's bike out from the door"), B->MyBike() + 1), Prompt, 0.65f});
		// [yard] The adventure is the host's to start, from the pause menu.
		HudLines.Add({GetNetMode() == NM_Client ? FString(TEXT("The host sets off on Buttertubs (their menu)"))
			: FString(TEXT("Esc / Options: Set off on Buttertubs")), Info, 0.55f});
		break;
	default:
		break;
	}
	// What the work button does, or why not.
	if (Plan.bValid)
	{
		if (!Plan.Blocked.IsEmpty()) HudLines.Add({FString::Printf(TEXT("Can't %s: %s"), *Plan.Verb, *Plan.Blocked), Blocked, 0.65f});
		else HudLines.Add({FString::Printf(TEXT("%s left mouse / Cross: %s"), Plan.Time > 0.3 ? TEXT("Hold") : TEXT("Press"), *Plan.Verb), Prompt, 0.65f});
	}
	if (AltPlan.bValid)
		HudLines.Add({AltPlan.Blocked.IsEmpty() ? FString::Printf(TEXT("%s: %s"),
				AltPlan.Request.Kind == ETorqueShedAct::PumpFill ? TEXT("Hold right mouse / Square") : TEXT("Right mouse / Square"), *AltPlan.Verb)
			: FString::Printf(TEXT("Can't %s: %s"), *AltPlan.Verb, *AltPlan.Blocked), AltPlan.Blocked.IsEmpty() ? Prompt : Blocked, 0.6f});
	// [fluids] The nozzle in your hand, away from the pump and not filling: the same button hangs it up.
	const bool bPumpFill = AltPlan.bValid && AltPlan.Request.Kind == ETorqueShedAct::PumpFill;
	if (B->PumpHeldBy() == B->Me() && (!bPumpFill || !AltPlan.Blocked.IsEmpty()) && !(Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Pump))
		HudLines.Add({TEXT("Right mouse / Square: hang the nozzle back on the pump"), Prompt, 0.6f});
	if (Target == ETorqueLook::BikePart) HudLines.Add({TEXT("F / Triangle: ride this bike out   Hold F / Triangle: wheel it"), Prompt, 0.6f});
	// (No progress bar in the panel: the ring round the crosshair shows it, always: beenster, Nov 10.)
	{
		bool bTitled = false;
		for (int32 I = LookFrom; I < HudLines.Num(); ++I)
		{
			FHudLine& L = HudLines[I];
			if (L.Colour == Prompt || L.Colour == Blocked) L.Role = 3;
			else if (!bTitled && L.Text.Len() < 90) { L.Role = 1; bTitled = true; }
			else L.Role = 2;
		}
	}
	if (NoteFor > 0.0)
	{
		HudLines.Add({Note, bNoteOk ? FColor(255, 217, 178) : FColor(255, 140, 115), 0.8f});
		HudLines.Last().Role = 4;   // hud.gd flash: across the top
	}
	if (bPerfShow && !PerfText.IsEmpty()) HudLines.Add({PerfText, FColor(255, 255, 160), 0.55f});
	if (bShowHelp)
	{
		HudLines.Add({TEXT("WASD / left stick: walk   Shift / L3: hurry   C / R3: crouch   Space / Circle: hop"), Help, 0.5f});
		HudLines.Add({TEXT("Hold left mouse / Cross: work   Q / E, wheel, 1-6 or L1 / R1: tool (hands, wrench, brush, grinder, spray, washer)"), Help, 0.5f});
		HudLines.Add({TEXT("Unbolt with the wrench, then take it off by hand; fit what you hold, then bolt it up. R / T: spray colour and finish"), Help, 0.5f});
		HudLines.Add({TEXT("F / Triangle: unpack a box, the parts computer, ride out   H: hide help   Esc: menu"), Help, 0.5f});
	}
}

FString ATorqueWalker::Describe() const
{
	const UTorqueBuildSubsystem* B = Shed();
	const FString Title = B && TargetItem != 0 ? B->ItemTitle(TargetItem) : FString();
	switch (Target)
	{
	case ETorqueLook::BikePart: return FString::Printf(TEXT("bay %d %s (item %d, %s)"), TargetBay + 1, *TargetSlot.ToString(), TargetItem, *Title);
	case ETorqueLook::Item:
		if (TargetEngine != 0) return FString::Printf(TEXT("engine %d's %s (item %d, %s)"), TargetEngine, *TargetSlot.ToString(), TargetItem, *Title);
		return FString::Printf(TEXT("item %d (%s)%s"), TargetItem, *Title, TargetPlace ? *(TEXT(" on ") + TargetPlace->Name.ToString()) : TEXT(""));
	case ETorqueLook::Place: return FString::Printf(TEXT("place %s"), TargetPlace ? *TargetPlace->Name.ToString() : TEXT("?"));
	case ETorqueLook::Floor: return FString::Printf(TEXT("the floor at %.0f, %.0f"), TargetPoint.X, TargetPoint.Y);
	case ETorqueLook::Door: return TEXT("the roller door");
	default:
		if (TargetFurniture != 0)
		{
			const FTorqueFurniture* F = B ? B->FurnitureById(TargetFurniture) : nullptr;
			return FString::Printf(TEXT("kit piece %d (%s)"), TargetFurniture, F ? *F->Model.ToString() : TEXT("?"));
		}
		return TEXT("nothing");
	}
}

// --- Tests --------------------------------------------------------------------------------------------

void ATorqueWalker::UpdateRoomExposure(float Dt)
{
	// The paint shop (x 680 .. 1400, y -520 .. 520, as ue_build_shed.py rooms) and the mixing bay.
	const FVector At = GetActorLocation();
	const bool bPaintShop = At.X > 680.0 && At.X < 1400.0 && At.Y > -520.0 && At.Y < 520.0;
	const double Want = (bPaintShop || TorquePaintBayLayout::InRoom(At)) ? 1.0 : 0.0;
	WhiteRoom = FMath::FInterpConstantTo(WhiteRoom, Want, (double)Dt, 0.8);
	// The level's own exposure bias (ue_build_shed.py EXPOSURE_BIAS; keep in step) plus the white
	// room's, blended in by the camera's weight so the shed's volume rules elsewhere.
	constexpr float LevelBias = 0.2f, WhiteBias = 0.9f;
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = LevelBias + WhiteBias;
	Camera->PostProcessBlendWeight = (float)WhiteRoom;
}

FString ATorqueWalker::TestLookAt(const FVector& Point, const FVector& Away, double Distance)
{
	FVector Side = FVector(Away.X, Away.Y, 0.0).GetSafeNormal();
	if (Side.IsNearlyZero()) Side = FVector(1.0, 0.0, 0.0);
	CloseComputer();
	// Standing on the floor under the spot (the floor is z 0 in the shed and the yard).
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Stand(Point.X + Side.X * Distance, Point.Y + Side.Y * Distance, Half + 2.0);
	// Not behind a wall: from the spot out to where we'd stand at eye height, and if a wall's in the
	// way, stand this side of it (else the shot is of the wall's back).
	if (UWorld* World = GetWorld())
	{
		const double EyeZ = Half + 2.0 + TWI::StandEye;
		const FVector From(Point.X, Point.Y, EyeZ), To(Stand.X, Stand.Y, EyeZ);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TorqueLookStand), false, this);
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Query) && Hit.Distance > 80.0)
		{
			const FVector Back = Hit.Location - Side * 45.0;
			Stand = FVector(Back.X, Back.Y, Half + 2.0);
		}
	}
	SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
	TestAim = Point;
	bTestAim = true;
	UpdateRoomExposure(10.0f);   // there now, not easing in
	const FVector Eye = GetActorLocation() + FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye);
	const FRotator View = (Point - Eye).Rotation();
	if (AController* C = GetController()) C->SetControlRotation(View);
	ATorquePlayerController::PlaceOnHost(this, GetActorLocation(), View);   // [co-op] a joined copy: the host too
	Camera->SetRelativeLocation(FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye));
	Camera->SetWorldRotation(View);
	FindTarget();
	MakePlan();
	UpdateCues();
	UE_LOG(LogTemp, Log, TEXT("Torque test: look outline round %d mesh(es)"), LookOutline.Count());
	UpdateHud();
	return Describe();
}

FString ATorqueWalker::TestOutlineStatus(bool& bOk) const
{
	return LookOutline.Status(bOk);
}

void ATorqueWalker::ReAim()
{
	if (!bTestAim || !Camera) return;
	const FVector Eye = GetActorLocation() + FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye);
	const FRotator View = (TestAim - Eye).Rotation();
	if (AController* C = GetController()) C->SetControlRotation(View);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye));
	Camera->SetWorldRotation(View);
}

FString ATorqueWalker::TestAct(bool bAlt)
{
	ReAim();
	FindTarget();
	MakePlan();
	const FString Seen = Describe();
	const FPlan P = bAlt ? AltPlan : Plan;
	if (!P.bValid) return FString::Printf(TEXT("%s with %s on %s: nothing to do (the look went into: %s)"), bAlt ? TEXT("alt") : TEXT("work"),
		TWI::ToolName(Tool), *Seen, *TestCandidates());
	if (!P.Blocked.IsEmpty()) return FString::Printf(TEXT("can't %s (%s on %s): %s"), *P.Verb, TWI::ToolName(Tool), *Seen, *P.Blocked);
	Note.Reset();
	const bool bOk = Do(P);
	FindTarget();
	MakePlan();
	UpdateHud();
	return FString::Printf(TEXT("%s %s (%s on %s): %s"), bOk ? TEXT("did") : TEXT("failed to"), *P.Verb, TWI::ToolName(Tool), *Seen,
		Note.IsEmpty() ? (bComputer ? TEXT("the parts computer is up") : TEXT("done")) : *Note);
}

FString ATorqueWalker::TestCandidates() const
{
	// What the last look went into, nearest first: kind, what it is, how far along, shape or box.
	const UTorqueBuildSubsystem* B = Shed();
	TArray<const FLookHit*> Sorted;
	for (const FLookHit& H : LookHits) Sorted.Add(&H);
	Sorted.Sort([](const FLookHit& X, const FLookHit& Y) { return X.Time < Y.Time; });
	FString Out;
	for (const FLookHit* H : Sorted)
	{
		FString What;
		if (H->Kind == ETorqueLook::Place) What = H->Place ? TEXT("place ") + H->Place->Name.ToString() : FString(TEXT("place ?"));
		else if (const UStaticMeshComponent* C = H->C.Get())
		{
			const ATorqueShedBike* Bike = Cast<ATorqueShedBike>(C->GetOwner());
			What = Bike ? FString::Printf(TEXT("bay %d %s"), Bike->Bay + 1, *TorqueShedModel::SlotTag(C).ToString())
				: FString::Printf(TEXT("item %d %s"), ATorqueShedItems::ItemOf(C), B ? *B->ItemTitle(ATorqueShedItems::ItemOf(C)) : TEXT(""));
			What += TEXT(" (") + (C->GetStaticMesh() ? C->GetStaticMesh()->GetName() : FString(TEXT("?"))) + TEXT(")");
		}
		Out += FString::Printf(TEXT("%s%s at %.0f cm %s"), Out.IsEmpty() ? TEXT("") : TEXT("; "), *What, H->Time * Reach, H->bExact ? TEXT("shape") : TEXT("box"));
	}
	return Out.IsEmpty() ? FString(TEXT("nothing")) : Out;
}

FString ATorqueWalker::TestInteract()
{
	ReAim();
	FindTarget();
	const FString Seen = Describe();
	Note.Reset();
	Interact();
	UpdateHud();
	return FString::Printf(TEXT("F on %s: %s"), *Seen, bRidingOut ? TEXT("riding out") : (Note.IsEmpty() ? (bComputer ? (bDesk ? TEXT("the dyno desk is up") : TEXT("the parts computer is up")) : TEXT("nothing happened")) : *Note));
}

FString ATorqueWalker::TestDesk(const FString& What, const FString& Key, float Value)
{
	if (What == TEXT("close"))
	{
		CloseComputer();
		return TEXT("walked away from the desk");
	}
	if (!(bComputer && bDesk))
	{
		if (bComputer) CloseComputer();
		OpenDesk();
	}
	Note.Reset();
	if (What == TEXT("tab") && !Desk.PickTab(Key)) return TEXT("no tab ") + Key;
	if (What == TEXT("set") || What == TEXT("stock")) DeskDo(Desk.TestSet(Key, Value, What == TEXT("stock")));
	if (What == TEXT("run")) DeskDo(Desk.Pad(0, 0, 0, false, true));
	FString Out = Desk.Describe(Shed());
	if (!Note.IsEmpty()) Out += TEXT(" [") + Note + TEXT("]");
	return Out;
}

FName ATorqueWalker::TestTargetPlace() const
{
	return TargetPlace ? TargetPlace->Name : NAME_None;
}

FString ATorqueWalker::BikeWhere(int32 Bay) const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B || !B->Exists(Bay)) return FString::Printf(TEXT("no bike in bay %d"), Bay + 1);
	const FTransform Xf = B->BikeTransform(Bay);
	FString Spot = TEXT("on its stand on the floor");
	if (B->Bike(Bay).Pusher != 0) Spot = TEXT("being wheeled");
	else if (!B->IsAway(Bay)) Spot = TEXT("on its bay stand");
	else if (B->DynoBay() == Bay) Spot = TEXT("on the dyno rig");
	FRotator Drawn = Xf.Rotator();
	if (const ATorqueShedBike* Bike = BikeActor(Bay)) Drawn = Bike->GetActorRotation();
	return FString::Printf(TEXT("bay %d's bike %s at (%.0f, %.0f) yaw %.0f, pitch %.1f roll %.1f"), Bay + 1, *Spot,
		Xf.GetLocation().X, Xf.GetLocation().Y, Xf.Rotator().Yaw, Drawn.Pitch, Drawn.Roll);
}

FString ATorqueWalker::TestWheel(int32 Bay, int32 Where)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B || !B->Exists(Bay)) return TEXT("no bike in that bay");
	if (Wheeling() != Bay)
	{
		FTorqueShedRequest Grab;
		Grab.Kind = ETorqueShedAct::GrabBike;
		Grab.Bay = Bay;
		FString WhyNot;
		if (!B->Request(Grab, WhyNot)) return TEXT("couldn't take hold of it: ") + WhyNot;
		UE_LOG(LogTemp, Log, TEXT("Torque: grab: %s"), *BikeWhere(Bay));
	}
	if (Wheeling() != Bay) return TEXT("took hold of it, but the shed doesn't have us wheeling it");
	// Stand at its bars: the bike 85 cm to our right and 25 cm behind (PushTransform), facing its way.
	// Mid-floor is 2.5 m out from its bay towards the door, square to it.
	FTransform To = UTorqueBuildSubsystem::HomeOf(Bay);
	if (Where == 1) To = UTorqueBuildSubsystem::RigTransform();
	else if (Where == 2) To.AddToTranslation(FVector(0.0, 250.0, 0.0));
	else if (Where == 3) To = TorqueShedLayout::WashLiftXf();
	// [fluids] Out by the petrol pump on the front wall, its filler cap within the hose's reach.
	else if (Where == 4) To = FTransform(FRotator(0.0, 180.0, 0.0), UTorqueBuildSubsystem::PumpAt().TransformPosition(FVector(170.0, 140.0, 0.0)));
	const FRotator Heading(0.0, To.Rotator().Yaw, 0.0);
	const FVector Ahead = Heading.Vector(), Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Stand = To.GetLocation() - Right * 85.0 + Ahead * 25.0;
	SetActorLocation(FVector(Stand.X, Stand.Y, Half + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = GetController()) C->SetControlRotation(Heading);
	bTestAim = false;
	ATorquePlayerController::PlaceOnHost(this, GetActorLocation(), Heading);   // [co-op] a joined copy: the host too
	Note.Reset();
	LetGo(Bay);
	bool bThere = B->IsAway(Bay) && B->DynoBay() != Bay && B->Bike(Bay).Pusher == 0;
	if (Where == 0) bThere = !B->IsAway(Bay);
	else if (Where == 1) bThere = B->DynoBay() == Bay;
	else if (Where == 3) bThere = B->LiftOf(Bay) == UTorqueBuildSubsystem::WashLift;
	else if (Where == 4) bThere = B->IsAway(Bay) && B->Bike(Bay).Pusher == 0 && B->PumpReaches(Bay);
	FString WhereText = TEXT("home");
	if (Where == 1) WhereText = TEXT("onto the rig");
	else if (Where == 2) WhereText = TEXT("mid-floor");
	else if (Where == 3) WhereText = TEXT("onto the wash bay lift");
	else if (Where == 4) WhereText = TEXT("up to the petrol pump");
	FString Result = TEXT("it didn't park there");
	if (bThere) Result = Note.IsEmpty() ? FString(TEXT("there")) : Note;
	return FString::Printf(TEXT("wheeled bay %d's bike %s: %s"), Bay + 1, *WhereText, *Result);
}

FString ATorqueWalker::TestOrder(FName Part)
{
	Note.Reset();
	const FString Said = Order(Part);
	return FString::Printf(TEXT("order %s: %s"), *Part.ToString(), Said.IsEmpty() ? *Note : *Said);
}

// --- Performance ----------------------------------------------------------------------------------------

void ATorqueWalker::UpdatePerf(float Dt)
{
	PerfClock += Dt;
	PerfRun += Dt;
	PerfSum += Dt;
	PerfWorst = FMath::Max(PerfWorst, (double)Dt);
	++PerfFrames;
	if (PerfClock < 1.0) return;
	const double AvgMs = PerfSum / FMath::Max(PerfFrames, 1) * 1000.0;
	const double Fps = PerfFrames / PerfClock;
	const double GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const double RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	FIntPoint Res(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) Res = GEngine->GameViewport->Viewport->GetSizeXY();
	PerfText = FString::Printf(TEXT("%.0f fps   frame %.1f ms (worst %.1f)   game %.1f   render %.1f   GPU %.1f ms   %dx%d"),
		Fps, AvgMs, PerfWorst * 1000.0, GameMs, RenderMs, GpuMs, Res.X, Res.Y);
	if (bPerfLog)
	{
		if (PerfFile.IsEmpty())
		{
			PerfFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"),
				FString::Printf(TEXT("torque_shed_perf_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
			FFileHelper::SaveStringToFile(TEXT("time_s,fps,frame_ms,worst_ms,game_ms,render_ms,gpu_ms,width,height,x_cm,y_cm,yaw\n"), *PerfFile);
			UE_LOG(LogTemp, Log, TEXT("Torque: logging shed performance to %s"), *PerfFile);
		}
		const FVector At = GetActorLocation();
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.1f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.0f,%.0f,%.0f\n"),
			PerfRun, Fps, AvgMs, PerfWorst * 1000.0, GameMs, RenderMs, GpuMs, Res.X, Res.Y, At.X, At.Y, GetControlRotation().Yaw),
			*PerfFile, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
	PerfClock = 0.0;
	PerfSum = 0.0;
	PerfWorst = 0.0;
	PerfFrames = 0;
}

// --- Co-op: hands (Coop/TorquePlayerState.h FTorqueHandsNet) -------------------------------------------

int32 ATorqueWalker::PeerOf() const
{
	if (IsLocallyControlled())
	{
		const UTorqueBuildSubsystem* B = Shed();
		return B ? B->Me() : INDEX_NONE;
	}
	return GetPlayerState() ? GetPlayerState()->GetPlayerId() : INDEX_NONE;
}

void ATorqueWalker::ShareHands(float Dt)
{
	ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	if (!PS || GetNetMode() == NM_Standalone) return;
	FTorqueHandsNet H;
	H.Tool = (uint8)Tool;
	H.Work = TorqueHands::QuantiseWork(WorkFraction(), Progress > 0.0);
	H.Colour = (uint8)FMath::Clamp(SprayColour, 0, 255);
	H.Finish = (uint8)FMath::Clamp(SprayFinish, 0, 255);
	H.Look = (uint8)Target;
	H.Bay = (int8)FMath::Clamp(TargetBay, -1, 127);
	H.Slot = TargetSlot;
	H.Item = TargetItem;
	H.Place = TargetPlace ? TargetPlace->Name : NAME_None;
	H.bAway = bComputer || bRidingOut || bBench;
	H.Turn = (uint8)(FMath::RoundToInt(FRotator::ClampAxis(HoldTurn) * 256.0 / 360.0) & 0xFF);
	PS->OfferHands(H, Dt);
}

void ATorqueWalker::FollowHands(float Dt)
{
	const ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	if (!PS) return;
	const FTorqueHandsNet& H = PS->Hands;
	Tool = (ETorqueTool)FMath::Clamp<int32>(H.Tool, 0, (int32)ETorqueTool::Count - 1);
	if (Tool == ETorqueTool::Spray) Tool = ETorqueTool::Gun;   // an older game's spray can
	SprayColour = H.Colour;
	SprayFinish = H.Finish;
	bComputer = H.bAway;
	Target = (ETorqueLook)H.Look;
	TargetBay = H.Bay;
	TargetSlot = H.Slot;
	TargetItem = H.Item;
	HoldTurn = H.Turn * 360.0 / 256.0;   // the part in their hands, turned as they've turned it
	// Progress only says "working" to UpdateToolView (it's never run as a job here).
	Progress = H.Work > 0 ? FMath::Max(TorqueHands::WorkFraction(H.Work), 0.01) : 0.0;
	// Their camera: where they look (Unreal replicates the pitch), at their eye height.
	Camera->SetRelativeLocation(FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye));
	Camera->SetWorldRotation(GetBaseAimRotation());
	UpdateToolView(Dt);
	const UTorqueBuildSubsystem* B = Shed();
	// A bike they're wheeling: alongside them, worked out here from where they walk (no MoveBike).
	if (B)
	{
		const int32 Wheeled = B->PushedBy(PeerOf());
		if (Wheeled != INDEX_NONE)
			if (ATorqueShedBike* Bike = BikeActor(Wheeled)) Bike->Preview(PushTransform());
	}
	TorqueLook::Pose(this, ToolHolder && ToolHolder->IsVisible(), B && B->HeldBy(PeerOf()) != 0, WorkBlend);

	// What they're working on glows faintly in their colour (only while they work: you see them
	// at it, not everything they glance at). Not a bike they're washing: the lance does the bike.
	TArray<UStaticMeshComponent*> Now;
	if (UWorld* World = GetWorld())
	{
		if (H.Work > 0 && Target == ETorqueLook::BikePart && Tool != ETorqueTool::Lance && TargetBay != INDEX_NONE)
		{
			for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
				if (It->Bay == TargetBay)
					for (UStaticMeshComponent* C : It->Parts)
						if (C && C->IsVisible() && TorqueShedModel::SlotTag(C) == TargetSlot) Now.Add(C);
		}
		else if (H.Work > 0 && Target == ETorqueLook::Item && TargetItem != 0)
		{
			TArray<const UStaticMeshComponent*> Meshes;
			for (TActorIterator<ATorqueShedItems> It(World); It; ++It) It->MeshesOf(TargetItem, Meshes);
			for (const UStaticMeshComponent* C : Meshes) Now.Add(const_cast<UStaticMeshComponent*>(C));
		}
	}
	if (!TheirLitMaterial && Now.Num() > 0)
	{
		static UMaterialInterface* Glow = []() {
			UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedHighlight.M_ShedHighlight"));
			if (M) M->AddToRoot();
			return M;
		}();
		if (Glow)
		{
			TheirLitMaterial = UMaterialInstanceDynamic::Create(Glow, this);
			TheirLitMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(PS->ColourValue()));
			TheirLitMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.14f);
		}
	}
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : TheirLit)
		if (UStaticMeshComponent* C = W.Get())
			if (!Now.Contains(C) && C->GetOverlayMaterial() == TheirLitMaterial) C->SetOverlayMaterial(nullptr);
	TheirLit.Reset();
	if (!TheirLitMaterial) return;
	for (UStaticMeshComponent* C : Now)
	{
		// Your own look's glow wins over theirs (yours is the one you act on).
		if (!C->GetOverlayMaterial()) C->SetOverlayMaterial(TheirLitMaterial);
		TheirLit.Add(C);
	}
}
