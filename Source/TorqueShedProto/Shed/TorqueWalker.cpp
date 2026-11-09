#include "Shed/TorqueWalker.h"

#include "Coop/HandsState.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueAvatarDemo.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorquePaintBayRoom.h"
#include "Coop/TorqueLook.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueSettings.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueShedScreens.h"
#include "Shed/TorqueToolModels.h"
#include "SonyPad.h"
#include "Camera/CameraComponent.h"
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
const float CrouchHalfHeight = 55.0f;

const TCHAR* ToolName(ETorqueTool T)
{
	switch (T)
	{
	case ETorqueTool::Wrench: return TEXT("Wrench");
	case ETorqueTool::Brush: return TEXT("Brush");
	case ETorqueTool::Repair: return TEXT("Grinder and welder");
	case ETorqueTool::Spray: return TEXT("Spray can");
	case ETorqueTool::Lance: return TEXT("Pressure washer");
	case ETorqueTool::Gun: return TEXT("Spray gun");
	default: return TEXT("Hands");
	}
}

// What the spray can or spray gun can be loaded with (walker.gd spray_loads): each paint finish
// (in the colour picked with R), then for the gun its protective coats and grey primer; the can
// has primer and clear lacquer.
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
	if (T == ETorqueTool::Gun)
	{
		for (const FName& C : UTorqueBuildSubsystem::Coats()) Out.Add({FLoad::Coat, C});
		Out.Add({FLoad::Primer, FName(TEXT("primer"))});
		Out.Add({FLoad::Cup, UTorqueBuildSubsystem::CupFinish()});   // paint mixed in the mixing bay
	}
	else
	{
		Out.Add({FLoad::Primer, FName(TEXT("primer"))});
		Out.Add({FLoad::Coat, FName(TEXT("clear"))});
	}
	return Out;
}

bool Sprays(ETorqueTool T) { return T == ETorqueTool::Spray || T == ETorqueTool::Gun; }

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

FString Bar(double Fraction)
{
	const int32 N = FMath::Clamp(FMath::RoundToInt(Fraction * 20.0), 0, 20);
	FString S = TEXT("[");
	for (int32 I = 0; I < 20; ++I) S += I < N ? TEXT("#") : TEXT("-");
	return S + FString::Printf(TEXT("] %.0f%%"), 100.0 * FMath::Clamp(Fraction, 0.0, 1.0));
}

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
	}
	if (UTorqueBuildSubsystem* B = Shed()) TellHandle = B->OnTell.AddUObject(this, &ATorqueWalker::Told);
}

void ATorqueWalker::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = Shed()) B->OnTell.Remove(TellHandle);
	Pad.Reset();   // stops its reading thread
	if (Avatar) Avatar->Destroy();   // its MetaHuman Blueprint goes with it
	Super::EndPlay(Reason);
}

void ATorqueWalker::UpdateAvatar(float Dt)
{
	// A MetaHuman per player colour, when Data/Avatars/avatars.json lists some (Tools/ue_metahumans.py).
	if (FTorqueAvatar::Count() == 0 || GetNetMode() == NM_DedicatedServer) return;
	const ATorquePlayerState* PS = Cast<ATorquePlayerState>(GetPlayerState());
	if (!PS) return;
	if (AvatarFor != (int32)PS->Colour)
	{
		AvatarFor = PS->Colour;
		if (!AvatarRoot)
		{
			AvatarRoot = NewObject<USceneComponent>(this, TEXT("AvatarRoot"));
			AvatarRoot->SetupAttachment(GetCapsuleComponent());
			AvatarRoot->RegisterComponent();
		}
		if (!Avatar) Avatar = MakeUnique<FTorqueAvatar>();
		const bool bMade = Avatar->Build(this, AvatarRoot, PS->Colour);
		// The shape body goes while the MetaHuman stands in for it.
		TArray<UStaticMeshComponent*> Shapes;
		GetComponents(Shapes);
		for (UStaticMeshComponent* C : Shapes)
			if (C->GetName().StartsWith(TEXT("Body_"))) C->SetVisibility(!bMade);
	}
	if (!Avatar || !Avatar->IsValid()) return;
	// Feet on the floor: the capsule's bottom (it shrinks and drops when crouched).
	AvatarRoot->SetRelativeLocation(FVector(0.0, 0.0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
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
	Avatar->Pose(In, Dt);
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
		// (Their hands first: the avatar's right hand reaches for the tool where FollowHands put it.)
		FollowHands(DeltaSeconds);
		UpdateAvatar(DeltaSeconds);
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
	Camera->SetRelativeLocation(FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye));
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
	if (Wheeling() != INDEX_NONE) PushBike(DeltaSeconds);
	else if (!bComputer && !bBench)
	{
		FindTarget();
		MakePlan();
		Work(DeltaSeconds);
	}
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
	// The pause or settings menu is up (co-op, ATorquePlayerController): stand still, leave the keys to it.
	if (ATorquePlayerController::MenuUp(PC)) return;
	// F2: geometry mode on the bike looked at, or the nearest (main.gd's F2).
	if (Just(EKeys::F2) && !bComputer && !bBench && Wheeling() == INDEX_NONE)
	{
		FString Why;
		if (!ATorqueGeometryMode::Open(this, Target == ETorqueLook::BikePart ? TargetBay : INDEX_NONE, Why) && !Why.IsEmpty()) Say(Why, false);
		return;
	}
	const FTorqueSettings& Set = FTorqueSettings::Get();   // Settings > Controller: look speeds, invert

	const bool bWorkJust = Just(EKeys::LeftMouseButton) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
	const bool bAltJust = Just(EKeys::R) || Just(EKeys::RightMouseButton) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square);
	const bool bF = Just(EKeys::F) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle);
	const bool bNext = Just(EKeys::E) || Just(EKeys::MouseScrollDown) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1);
	const bool bPrev = Just(EKeys::Q) || Just(EKeys::MouseScrollUp) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1);
	const bool bBack = Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle);

	if (bComputer)
	{
		// At the parts computer: W / S (D-pad, stick) choose, Q / E (L1 / R1) the category, the work
		// button orders, F or Space (Circle / B) walks away.
		int32 Step = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Step = 1;
		ComputerInput(Step, bNext ? 1 : (bPrev ? -1 : 0), bWorkJust || Just(EKeys::Enter), bF || bBack);
		return;
	}
	if (bBench)
	{
		// At the mixing bench: W / S (the D-pad) choose, Q / E (L1 / R1) the page, hold the work button
		// on a tint to pour it, hold R (Square) to stir, F or Space (Circle / B) steps back.
		int32 Step = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown)) Step = 1;
		const bool bWorkDown = Down(EKeys::LeftMouseButton) || Down(EKeys::Gamepad_FaceButton_Bottom) || (SonyDown & ESonyButton::Cross);
		const bool bAltDown = Down(EKeys::R) || Down(EKeys::RightMouseButton) || Down(EKeys::Gamepad_FaceButton_Left) || (SonyDown & ESonyButton::Square);
		BenchInput(Dt, Step, bNext ? 1 : (bPrev ? -1 : 0), bWorkJust || Just(EKeys::Enter), bWorkDown, bAltDown, bF || bBack);
		return;
	}

	// Walking: keys, then whichever stick is pushed further.
	float Fwd = (Down(EKeys::W) ? 1.0f : 0.0f) - (Down(EKeys::S) ? 1.0f : 0.0f);
	float Side = (Down(EKeys::D) ? 1.0f : 0.0f) - (Down(EKeys::A) ? 1.0f : 0.0f);
	const float PadFwd = S.bConnected ? Dead(S.LY) : Dead(Axis(EKeys::Gamepad_LeftY));
	const float PadSide = S.bConnected ? Dead(S.LX) : Dead(Axis(EKeys::Gamepad_LeftX));
	if (FMath::Abs(PadFwd) > FMath::Abs(Fwd)) Fwd = PadFwd;
	if (FMath::Abs(PadSide) > FMath::Abs(Side)) Side = PadSide;
	const bool bRun = Down(EKeys::LeftShift) || Down(EKeys::Gamepad_LeftThumbstick) || (SonyDown & ESonyButton::L3);
	GetCharacterMovement()->MaxWalkSpeed = bRun ? RunSpeed : WalkSpeed;
	if (Wheeling() != INDEX_NONE) GetCharacterMovement()->MaxWalkSpeed = FMath::Min(GetCharacterMovement()->MaxWalkSpeed, 240.0f);   // a bike is heavy to wheel about
	const FRotator Yaw(0.0, PC->GetControlRotation().Yaw, 0.0);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Fwd);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Side);

	// Looking: the mouse, and the right stick.
	float Mx = 0.0f, My = 0.0f;
	PC->GetInputMouseDelta(Mx, My);
	const float LookX = S.bConnected ? Dead(S.RX) : Dead(Axis(EKeys::Gamepad_RightX));
	const float LookY = S.bConnected ? Dead(S.RY) : Dead(Axis(EKeys::Gamepad_RightY));
	FRotator R = PC->GetControlRotation();
	const float Invert = Set.bInvertLook ? -1.0f : 1.0f;
	R.Yaw += Mx * MouseDegPerPixel * Set.MouseLook + LookX * PadDegPerSecond * Set.PadLook * Dt;
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + Invert * (My * MouseDegPerPixel * Set.MouseLook +
		LookY * PadDegPerSecond * Set.PadLook * 0.7f * Dt)), -85.0, 85.0);
	R.Roll = 0.0;
	PC->SetControlRotation(R);

	if (bBack) Jump();
	if (Just(EKeys::C) || Just(EKeys::LeftControl) || Just(EKeys::Gamepad_RightThumbstick) || (SonyHit & ESonyButton::R3)) SetCrouched(!bCrouched);
	// The tools: Q / E, the wheel, L1 / R1, or 1-7.
	const int32 Tools = (int32)ETorqueTool::Count;
	if (bNext) SetTool((ETorqueTool)(((int32)Tool + 1) % Tools));
	if (bPrev) SetTool((ETorqueTool)(((int32)Tool + Tools - 1) % Tools));
	static const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven};
	for (int32 I = 0; I < Tools && I < (int32)UE_ARRAY_COUNT(Numbers); ++I)
		if (Just(Numbers[I])) SetTool((ETorqueTool)I);
	// The spray can: R / right mouse / Square the colour, T the finish. Otherwise those are the
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
	else if (TWI::Sprays(Tool))
	{
		if (bAltJust) SprayColour = (SprayColour + 1) % UTorqueBuildSubsystem::Paints().Num();
	}
	else
	{
		bAltPressed = bAltJust;
		bAltHeld = Down(EKeys::R) || Down(EKeys::RightMouseButton) || Down(EKeys::Gamepad_FaceButton_Left) || (SonyDown & ESonyButton::Square);
	}
	if (Just(EKeys::T) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight))
		if (TWI::Sprays(Tool)) SprayFinish = (SprayFinish + 1) % FMath::Max(1, TWI::Loads(Tool).Num());
	bWorkPressed = bWorkJust;
	bWorkHeld = Down(EKeys::LeftMouseButton) || Down(EKeys::Gamepad_FaceButton_Bottom) || (SonyDown & ESonyButton::Cross);
	// F: tap to use or ride, hold on a bike to wheel it (walker.gd HOLD_TO_PUSH); wheeling, F lets go.
	const bool bFDown = Down(EKeys::F) || Down(EKeys::Gamepad_FaceButton_Top) || (SonyDown & ESonyButton::Triangle);
	const UTorqueBuildSubsystem* FShed = Shed();
	const int32 Pushing = Wheeling();
	const bool bAtBike = FShed && TargetBay != INDEX_NONE && FShed->Exists(TargetBay) &&
		(Target == ETorqueLook::BikePart || (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bay));
	if (Pushing != INDEX_NONE)
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
	if (Just(EKeys::H)) bShowHelp = !bShowHelp;
	if (Just(EKeys::P)) bPerfShow = !bPerfShow;
	if (Just(EKeys::O))
	{
		bPerfLog = !bPerfLog;
		if (!bPerfLog) PerfFile.Reset();
	}
	// Esc, Options and Start open the pause menu (ATorquePlayerController), which has Quit.
}

void ATorqueWalker::SetTool(ETorqueTool T)
{
	if (T == Tool) return;
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
	// A finish, a coat or "primer", among what the tool in hand (the can unless the gun is) takes.
	const TArray<TWI::FLoad> L = TWI::Loads(Tool == ETorqueTool::Gun ? ETorqueTool::Gun : ETorqueTool::Spray);
	SprayFinish = FMath::Max(L.IndexOfByPredicate([Finish](const TWI::FLoad& X) { return X.Id == Finish; }), 0);
}

// --- What's looked at ----------------------------------------------------------------------------

void ATorqueWalker::FindTarget()
{
	Target = ETorqueLook::None;
	TargetBay = INDEX_NONE;
	TargetSlot = NAME_None;
	TargetItem = TargetEngine = 0;
	TargetPlace = nullptr;
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
	if (!B || Target == ETorqueLook::None || Target == ETorqueLook::Door) return;
	const int32 Me = B->Me();
	const int32 Held = B->HeldBy(Me);
	const FTorqueItem* HeldIt = B->Item(Held);
	const FTorqueItem* Thing = B->Item(TargetItem);
	const FName Engine(TEXT("engine"));
	// The floor under where you look, for putting a part down.
	auto FloorUnder = [this](const FVector& At)
	{
		FVector Out(At.X, At.Y, 0.0);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerFloor), false, this);
		for (TActorIterator<ATorqueShedBike> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
		for (TActorIterator<ATorqueShedItems> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
		if (GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 20), At - FVector(0, 0, 400), ECC_Visibility, Params)) Out.Z = Hit.ImpactPoint.Z;
		return FTransform(FRotator(0.0, GetControlRotation().Yaw, 0.0), Out);
	};
	auto DropPlan = [&]()
	{
		FPlan P = MakeOne(ETorqueShedAct::Drop, 0.25, TEXT("put it down here"));
		P.Request.Place = FloorUnder(TargetPoint);
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
		Plan = MakeOne(ETorqueShedAct::DynoRun, 0.4, TEXT("run the dyno"));
		Plan.Request.Bay = B->DynoBay();
		Plan.Blocked = B->Check(Me, Plan.Request);
		return;
	}
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::DynoSwitch) return;   // F flicks them
	if (TargetPlace && (TargetPlace->Kind == ETorquePlace::PaintBench || TargetPlace->Kind == ETorquePlace::PaintShelf)) return;   // F, as the tins go
	if (B->PaintBay().HeldTin(Me)) return;   // a tin in your hands: nothing else until it's put down
	switch (Tool)
	{
	case ETorqueTool::Hands:
		if (HeldIt)
		{
			const FString Title = TWI::Lower(B->ItemTitle(Held));
			if (TargetPlace && TargetPlace->Kind == ETorquePlace::Bay && HeldIt->Kind == FName(TEXT("frame")) && !B->Exists(TargetPlace->Index))
			{
				// A bare frame on an empty bay's paddock stand starts a bike from scratch.
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
					Plan = MakeOne(ETorqueShedAct::TyreFit, 3.0, TEXT("work the tyre on round the rim"));
					Plan.Request.Spot = Spot;
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
		if (TargetItem != 0)
		{
			// walker.gd _spray_plan: the colour, primer or a coat, as loaded.
			const bool bGun = Tool == ETorqueTool::Gun;
			const TArray<TWI::FLoad> L = TWI::Loads(Tool);
			const TWI::FLoad Load = L.IsValidIndex(SprayFinish) ? L[SprayFinish] : L[0];
			if (Load.Kind == TWI::FLoad::Primer)
				Plan = MakeOne(ETorqueShedAct::Prime, 1.5, bGun ? FString(TEXT("prime it: grey primer, in steady passes")) : FString(TEXT("prime it: sweep grey primer over all of it")));
			else if (Load.Kind == TWI::FLoad::Coat)
			{
				Plan = MakeOne(ETorqueShedAct::Coat, 1.5, UTorqueBuildSubsystem::IsLacquer(Load.Id)
					? FString::Printf(TEXT("lacquer it: sweep %s over the paint"), *UTorqueBuildSubsystem::CoatName(Load.Id).ToLower())
					: FString::Printf(TEXT("spray it with %s: sweep over all of it"), *UTorqueBuildSubsystem::CoatName(Load.Id).ToLower()));
				Plan.Request.Finish = Load.Id;
			}
			else if (Load.Kind == TWI::FLoad::Cup)
			{
				// The gun's cup, filled from a tin from the mixing bay.
				const FTorquePaintCup* Cup = B->PaintBay().Cups.Find(Me);
				const FString What = Cup ? Cup->Spec.Name : FString(TEXT("the cup"));
				Plan = MakeOne(ETorqueShedAct::Paint, 1.5, FString::Printf(TEXT("spray %s from the cup: go along the part in steady passes"), *What));
				Plan.Request.Finish = UTorqueBuildSubsystem::CupFinish();
			}
			else
			{
				const FString What = FString::Printf(TEXT("%s %s"), *UTorqueBuildSubsystem::FinishName(Load.Id).ToLower(), *UTorqueBuildSubsystem::PaintName(SprayColour).ToLower());
				Plan = MakeOne(ETorqueShedAct::Paint, 1.5, bGun ? FString::Printf(TEXT("spray %s: go along the part in steady passes"), *What)
					: FString::Printf(TEXT("spray it %s: sweep over it evenly"), *What));
				Plan.Request.Colour = SprayColour;
				Plan.Request.Finish = Load.Id;
			}
			Plan.Request.Item = TargetItem;
			Plan.Request.Bay = TargetBay;
			Plan.Request.bGun = bGun;
		}
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
	if (Plan.bValid && !Plan.bComputer) Plan.Blocked = B->Check(Me, Plan.Request);
	if (AltPlan.bValid) AltPlan.Blocked = B->Check(Me, AltPlan.Request);
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
		OpenComputer();
		return true;
	}
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
		// The kick of the jet.
		Move += FVector(FMath::Sin(T * 60.0) * 0.3, 0.0, FMath::Sin(T * 53.0) * 0.3) * W;
		break;
	default:
		break;
	}
	// The motion is in the camera's axes, the turn about the tool's own grip.
	FTransform Hold = TorqueToolModels::HoldOf(ToolIndex);
	Hold.SetRotation(Turn.Quaternion() * Hold.GetRotation());
	Hold.AddToTranslation(Move);
	ToolHolder->SetRelativeTransform(Hold);
	UpdateFlying(Dt, bWorking && WorkBlend > 0.5 && (Tool == ETorqueTool::Repair || TWI::Sprays(Tool) || Tool == ETorqueTool::Lance));
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
	const FPlan& P = (bWorkHeld || ActHeldFor > 0.0) ? Plan : AltPlan;
	if (Progress <= 0.0 || !P.bValid || P.Time <= 0.0) return 0.0f;
	return (float)FMath::Clamp(Progress / P.Time, 0.0, 1.0);
}

FString ATorqueWalker::SprayFinishText() const
{
	const TArray<FString> Names = SprayLoadNames();
	return Names.IsValidIndex(SprayFinish) ? Names[SprayFinish] : FString();
}

TArray<FString> ATorqueWalker::SprayLoadNames() const
{
	TArray<FString> Out;
	for (const TWI::FLoad& L : TWI::Loads(Tool == ETorqueTool::Gun ? ETorqueTool::Gun : ETorqueTool::Spray))
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
	const TArray<TWI::FLoad> L = TWI::Loads(Tool == ETorqueTool::Gun ? ETorqueTool::Gun : ETorqueTool::Spray);
	return !L.IsValidIndex(SprayFinish) || L[SprayFinish].Kind == TWI::FLoad::Paint;
}

FString ATorqueWalker::CarryingText() const
{
	const UTorqueBuildSubsystem* B = Shed();
	const int32 Held = B ? B->HeldBy(B->Me()) : 0;
	return Held != 0 ? B->ItemTitle(Held) : FString();
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
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : Lit)
		if (UStaticMeshComponent* C = W.Get())
			if (!Now.Contains(C)) C->SetOverlayMaterial(nullptr);
	Lit.Reset();
	for (UStaticMeshComponent* C : Now)
	{
		if (C->GetOverlayMaterial() != NowMat) C->SetOverlayMaterial(NowMat);
		Lit.Add(C);
	}
	// A part in your hands: its footprint where it will go down (the floor, a bench, a machine).
	bool bGhost = false;
	FTransform At = FTransform::Identity;
	if (bShow && Held != 0 && Tool == ETorqueTool::Hands && Plan.bValid)
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
	FVector2D Foot(60.0, 30.0);
	if (bGhost)
	{
		TArray<const UStaticMeshComponent*> Meshes;
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) It->MeshesOf(Held, Meshes);
		FBox Box(ForceInit);
		for (const UStaticMeshComponent* C : Meshes) Box += C->Bounds.GetBox();
		if (Box.IsValid)
		{
			const FVector Size = Box.GetSize();
			Foot = FVector2D(FMath::Max(Size.X, Size.Y), FMath::Min(Size.X, Size.Y));
			Foot.X = FMath::Clamp(Foot.X, 10.0, 220.0);
			Foot.Y = FMath::Clamp(Foot.Y, 8.0, 120.0);
		}
	}
	// The fill, its four edges and the notch on the front edge (walker.gd's pieces).
	while (GhostParts.Num() < 6 && CubeMesh)
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
		GhostParts.Add(C);
	}
	const bool bOk = Plan.Blocked.IsEmpty();
	const double Bar = 1.2;
	struct FPiece { FVector Size, Offset; bool bEdge; };
	const FPiece Pieces[6] = {
		{FVector(Foot.X, Foot.Y, 0.2), FVector(0, 0, 0.2), false},
		{FVector(Foot.X, Bar, 0.4), FVector(0, -Foot.Y * 0.5, 0.3), true},
		{FVector(Foot.X, Bar, 0.4), FVector(0, Foot.Y * 0.5, 0.3), true},
		{FVector(Bar, Foot.Y, 0.4), FVector(-Foot.X * 0.5, 0, 0.3), true},
		{FVector(Bar, Foot.Y, 0.4), FVector(Foot.X * 0.5, 0, 0.3), true},
		{FVector(Bar * 2.0, FMath::Min(6.0, Foot.Y * 0.5), 0.5), FVector(Foot.X * 0.5, 0, 0.4), true},
	};
	for (int32 I = 0; I < GhostParts.Num() && I < 6; ++I)
	{
		UStaticMeshComponent* C = GhostParts[I];
		if (!C) continue;
		if (!bGhost)
		{
			if (C->IsVisible()) C->SetVisibility(false);
			continue;
		}
		UMaterialInstanceDynamic* M = GhostMaterials.IsValidIndex((bOk ? 0 : 2) + (Pieces[I].bEdge ? 1 : 0)) ? GhostMaterials[(bOk ? 0 : 2) + (Pieces[I].bEdge ? 1 : 0)] : nullptr;
		if (M && C->GetMaterial(0) != M) C->SetMaterial(0, M);
		C->SetWorldLocationAndRotation(At.TransformPosition(Pieces[I].Offset + FVector(0, 0, 0.6)), At.GetRotation());
		C->SetWorldScale3D(Pieces[I].Size / 100.0);
		if (!C->IsVisible()) C->SetVisibility(true);
	}
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
	if (TargetPlace && TargetPlace->Kind == ETorquePlace::Computer)
	{
		OpenComputer();
		return;
	}
	if (TargetPlace && (TargetPlace->Kind == ETorquePlace::DynoSwitch || TargetPlace->Kind == ETorquePlace::DynoDesk))
	{
		// The dyno room: the switches by the door, and the desk (F runs the bike on the rig).
		FTorqueShedRequest R;
		R.Kind = TargetPlace->Kind == ETorquePlace::DynoDesk ? ETorqueShedAct::DynoRun
			: (TargetPlace->Index == 0 ? ETorqueShedAct::DynoLights : ETorqueShedAct::DynoFan);
		R.Bay = B->DynoBay();
		FString WhyNot;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		else if (R.Kind == ETorqueShedAct::DynoRun) Doing(FString::Printf(TEXT("running bay %d's bike on the dyno"), R.Bay + 1), 10.0);
		return;
	}
	if (Target == ETorqueLook::BikePart || Target == ETorqueLook::Door || (Target == ETorqueLook::Place && TargetPlace && TargetPlace->Kind == ETorquePlace::Bay))
	{
		StartRideOut(Target == ETorqueLook::Door ? B->MyBike() : TargetBay);
		return;
	}
	Say(TEXT("F: unpack a box, use the parts computer, or ride out (at your bike or the roller door)"), true, 2.5);
}

void ATorqueWalker::StartRideOut(int32 Bay)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return;
	if (!B->Exists(Bay))
	{
		Say(FString::Printf(TEXT("Bay %d is empty: press F at a bike to ride it"), Bay + 1), false, 2.5);
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
	B->SetMyBike(Bay);
	if (GetNetMode() == NM_Client)
	{
		Say(FString::Printf(TEXT("You'll ride %s: the host starts the ride"), *B->Bike(Bay).Name), true, 2.5);
		Doing(FString::Printf(TEXT("ready to ride bay %d's bike"), Bay + 1), 30.0);
		return;
	}
	Doing(FString::Printf(TEXT("riding out on bay %d's bike"), Bay + 1), 30.0);
	bRidingOut = true;
	RideOutIn = 0.4;   // long enough to show the line below
}

// --- The parts computer -----------------------------------------------------------------------------

void ATorqueWalker::OpenComputer()
{
	MenuKinds.Reset();
	MenuKinds.Add(NAME_None);   // project bikes
	for (const FName& K : TorqueShedParts::Kinds())
		if (TorqueShedParts::PartsOfKind(K).Num() > 0 && TorqueShedParts::SlotsForKind(K).Num() > 0) MenuKinds.Add(K);
	MenuCategory = FMath::Clamp(MenuCategory, 0, MenuKinds.Num() - 1);
	MenuRow = 0;
	bComputer = true;
	Progress = 0.0;
	bWaitRelease = true;
	Doing(TEXT("on the parts computer"), 6.0);
}

TArray<FName> ATorqueWalker::MenuRows() const
{
	TArray<FName> Rows;
	if (!MenuKinds.IsValidIndex(MenuCategory)) return Rows;
	if (MenuKinds[MenuCategory].IsNone())
	{
		Rows.Add(FName(TEXT("bike")));
		return Rows;
	}
	return TorqueShedParts::PartsOfKind(MenuKinds[MenuCategory]);
}

void ATorqueWalker::ComputerInput(int32 RowStep, int32 CategoryStep, bool bChoose, bool bClose)
{
	if (bClose)
	{
		bComputer = false;
		bWaitRelease = true;
		return;
	}
	if (CategoryStep != 0 && MenuKinds.Num() > 0)
	{
		MenuCategory = (MenuCategory + CategoryStep + MenuKinds.Num()) % MenuKinds.Num();
		MenuRow = 0;
	}
	const TArray<FName> Rows = MenuRows();
	if (Rows.Num() == 0) return;
	MenuRow = (MenuRow + RowStep + Rows.Num()) % Rows.Num();
	if (bChoose) Order(Rows[MenuRow]);
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
	if (bComputer)
	{
		HudLines.Add({TEXT("PARTS: order anything"), FColor::White, 0.9f});
		const FName Kind = MenuKinds.IsValidIndex(MenuCategory) ? MenuKinds[MenuCategory] : NAME_None;
		HudLines.Add({FString::Printf(TEXT("< %s >   (%d of %d: Q / E, L1 / R1)"), Kind.IsNone() ? TEXT("Project bikes") : *TorqueShedParts::KindName(Kind),
			MenuCategory + 1, MenuKinds.Num()), Title, 0.7f});
		const TArray<FName> Rows = MenuRows();
		const int32 From = FMath::Clamp(MenuRow - 4, 0, FMath::Max(Rows.Num() - 9, 0));
		for (int32 I = From; I < Rows.Num() && I < From + 9; ++I)
		{
			const FString Name = Rows[I] == FName(TEXT("bike")) ? FString(TEXT("Triumph Speed Twin 1200 (a project: tired, rusty, complete)"))
				: TorqueShedParts::PartName(Rows[I]) + TEXT("   ") + TorqueShedParts::PartDescription(Rows[I]).Left(70);
			HudLines.Add({(I == MenuRow ? TEXT("> ") : TEXT("   ")) + Name, I == MenuRow ? Prompt : Info, 0.6f});
		}
		HudLines.Add({TEXT("W / S or the D-pad: choose   left mouse / Cross: order (delivered outside the roller door)   F / Circle: done"), Help, 0.55f});
		if (NoteFor > 0.0) HudLines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
		return;
	}
	if (bBench)
	{
		BenchHud(B);
		return;
	}
	// The tool, and the spray can's paint.
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
		HudLines.Add({TEXT("Holding: ") + B->ItemTitle(Held), Info, 0.6f});
	if (const FTorqueTin* Tin = B->PaintBay().HeldTin(B->Me()))
	{
		HudLines.Add({TEXT("Holding a tin: ") + B->PaintBay().TinText(*Tin), Info, 0.6f});
		HudLines.Add({TEXT("R / Square: fill the spray gun's cup from it   F: onto the tin shelf, the bench's scale, or into a bin"), Prompt, 0.6f});
	}
	if (Tool == ETorqueTool::Gun)
		if (const FTorquePaintCup* Cup = B->PaintBay().Cups.Find(B->Me()))
			HudLines.Add({FString::Printf(TEXT("Gun's cup: %s, %.2f l (load Cup with T)"), *Cup->Spec.Name, Cup->Litres), Info, 0.55f});
	// What's looked at.
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
			if (Thing->bBoxed) HudLines.Add({TEXT("F / Triangle: unpack it"), Prompt, 0.65f});
		}
		break;
	case ETorqueLook::Place:
		if (TargetPlace)
		{
			HudLines.Add({TWI::PlaceName(*TargetPlace, B), FColor::White, 0.8f});
			if (TargetPlace->Kind == ETorquePlace::Computer) HudLines.Add({TEXT("F / Triangle (or left mouse): order parts and project bikes"), Prompt, 0.65f});
			if (TargetPlace->Kind == ETorquePlace::DynoSwitch) HudLines.Add({TEXT("F / Triangle: flick the switch"), Prompt, 0.65f});
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
				HudLines.Add({TEXT("F / Triangle (or left mouse): run the bike on the rig"), Prompt, 0.65f});
				if (B->HasDynoRun(B->DynoBay()))
				{
					const FTorqueDynoRun& Last = B->DynoRunOf(B->DynoBay());
					HudLines.Add({FString::Printf(TEXT("Last run: %.1f hp at %.0f rpm, %.1f Nm at %.0f rpm (at the wheel)"), Last.PeakHp, Last.PeakHpRpm, Last.PeakNm, Last.PeakNmRpm), Info, 0.55f});
				}
			}
			if (TargetPlace->Kind == ETorquePlace::Bay && B->Exists(TargetPlace->Index)) HudLines.Add({TEXT("F / Triangle: ride this bike out"), Prompt, 0.65f});
		}
		break;
	case ETorqueLook::Door:
		HudLines.Add({TEXT("Roller door"), FColor::White, 0.8f});
		HudLines.Add({FString::Printf(TEXT("F / Triangle: ride bay %d's bike out to Buttertubs"), B->MyBike() + 1), Prompt, 0.65f});
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
		HudLines.Add({AltPlan.Blocked.IsEmpty() ? FString::Printf(TEXT("Right mouse / Square: %s"), *AltPlan.Verb)
			: FString::Printf(TEXT("Can't %s: %s"), *AltPlan.Verb, *AltPlan.Blocked), AltPlan.Blocked.IsEmpty() ? Prompt : Blocked, 0.6f});
	if (Target == ETorqueLook::BikePart) HudLines.Add({TEXT("F / Triangle: ride this bike out"), Prompt, 0.6f});
	const FPlan* Active = (bWorkHeld || ActHeldFor > 0.0) ? &Plan : (bAltHeld ? &AltPlan : nullptr);
	if (Active && Active->bValid && Active->Blocked.IsEmpty() && Progress > 0.0 && Active->Time > 0.3)
		HudLines.Add({TWI::Bar(Progress / Active->Time), FColor::White, 0.6f});
	if (NoteFor > 0.0) HudLines.Add({Note, bNoteOk ? FColor(170, 230, 170) : Blocked, 0.65f});
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
	default: return TEXT("nothing");
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
	bComputer = false;
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
	UpdateRoomExposure(10.0f);   // there now, not easing in
	const FVector Eye = GetActorLocation() + FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye);
	const FRotator View = (Point - Eye).Rotation();
	if (AController* C = GetController()) C->SetControlRotation(View);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, bIsCrouched ? TWI::CrouchEye : TWI::StandEye));
	Camera->SetWorldRotation(View);
	FindTarget();
	MakePlan();
	UpdateHud();
	return Describe();
}

FString ATorqueWalker::TestAct(bool bAlt)
{
	FindTarget();
	MakePlan();
	const FString Seen = Describe();
	const FPlan P = bAlt ? AltPlan : Plan;
	if (!P.bValid) return FString::Printf(TEXT("%s with %s on %s: nothing to do"), bAlt ? TEXT("alt") : TEXT("work"), TWI::ToolName(Tool), *Seen);
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
	FindTarget();
	const FString Seen = Describe();
	Note.Reset();
	Interact();
	UpdateHud();
	return FString::Printf(TEXT("F on %s: %s"), *Seen, bRidingOut ? TEXT("riding out") : (Note.IsEmpty() ? (bComputer ? TEXT("the parts computer is up") : TEXT("nothing happened")) : *Note));
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
	const FRotator Heading(0.0, To.Rotator().Yaw, 0.0);
	const FVector Ahead = Heading.Vector(), Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
	const double Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Stand = To.GetLocation() - Right * 85.0 + Ahead * 25.0;
	SetActorLocation(FVector(Stand.X, Stand.Y, Half + 2.0), false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = GetController()) C->SetControlRotation(Heading);
	Note.Reset();
	LetGo(Bay);
	bool bThere = B->IsAway(Bay) && B->DynoBay() != Bay && B->Bike(Bay).Pusher == 0;
	if (Where == 0) bThere = !B->IsAway(Bay);
	else if (Where == 1) bThere = B->DynoBay() == Bay;
	FString WhereText = TEXT("home");
	if (Where == 1) WhereText = TEXT("onto the rig");
	else if (Where == 2) WhereText = TEXT("mid-floor");
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
	PS->OfferHands(H, Dt);
}

void ATorqueWalker::FollowHands(float Dt)
{
	const ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	if (!PS) return;
	const FTorqueHandsNet& H = PS->Hands;
	Tool = (ETorqueTool)FMath::Clamp<int32>(H.Tool, 0, (int32)ETorqueTool::Count - 1);
	SprayColour = H.Colour;
	SprayFinish = H.Finish;
	bComputer = H.bAway;
	Target = (ETorqueLook)H.Look;
	TargetBay = H.Bay;
	TargetSlot = H.Slot;
	TargetItem = H.Item;
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
