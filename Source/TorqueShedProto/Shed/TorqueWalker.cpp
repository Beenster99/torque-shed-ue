#include "Shed/TorqueWalker.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorquePartsBench.h"
#include "Shed/TorqueShedBike.h"
#include "SonyPad.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ATorqueWalker::ATorqueWalker()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	// An ordinary person: 1.8 m capsule, eyes at 1.62 m.
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 90.0f);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0, 0.0, 162.0 - 90.0));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(80.0f);
	BaseEyeHeight = 72.0f;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 1800.0f;
	Move->BrakingDecelerationWalking = 2400.0f;
	Move->GroundFriction = 10.0f;
	// A hop of about 0.45 m (sqrt(2 g h)): not enough to get up on the bench.
	Move->JumpZVelocity = 297.0f;
	Move->AirControl = 0.15f;
}

ATorqueWalker::~ATorqueWalker() = default;

void ATorqueWalker::BeginPlay()
{
	Super::BeginPlay();
}

void ATorqueWalker::EndPlay(const EEndPlayReason::Type Reason)
{
	Pad.Reset();   // stops its reading thread
	Super::EndPlay(Reason);
}

void ATorqueWalker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled()) return;
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
	bool bUse = false, bRide = false;
	ReadInput(DeltaSeconds, bUse, bRide);
	FindTarget();
	if (bUse) Use();
	if (bRide)
	{
		if (Target == ETorqueLook::Door || Target == ETorqueLook::BikePart) StartRideOut();
		else
		{
			Note = TEXT("Go to the bike or the roller door to ride out");
			NoteFor = 2.5;
		}
	}
	NoteFor = FMath::Max(NoteFor - DeltaSeconds, 0.0);
	UpdateHud();
}

void ATorqueWalker::ReadInput(float Dt, bool& bUse, bool& bRide)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
	if (!bInputReady)
	{
		// Only the player's own walker reads the pad (one reading thread), and takes the mouse.
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		Pad = MakeUnique<FSonyPad>();
		bInputReady = true;
	}
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K) ? 1.0f : 0.0f; };
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	auto Axis = [PC](const FKey& K) { return PC->GetInputAnalogKeyState(K); };
	auto Dead = [](float V) { return FMath::Abs(V) < 0.15f ? 0.0f : V; };

	const FSonyPadState S = Pad ? Pad->Get() : FSonyPadState();
	const uint32 SonyDown = S.bConnected ? S.Buttons : 0;
	const uint32 SonyHit = SonyDown & ~PrevPadButtons;
	PrevPadButtons = SonyDown;

	// Walking: keys, then whichever stick is pushed further.
	float Fwd = Down(EKeys::W) - Down(EKeys::S), Side = Down(EKeys::D) - Down(EKeys::A);
	const float PadFwd = S.bConnected ? Dead(S.LY) : Dead(Axis(EKeys::Gamepad_LeftY));
	const float PadSide = S.bConnected ? Dead(S.LX) : Dead(Axis(EKeys::Gamepad_LeftX));
	if (FMath::Abs(PadFwd) > FMath::Abs(Fwd)) Fwd = PadFwd;
	if (FMath::Abs(PadSide) > FMath::Abs(Side)) Side = PadSide;
	const bool bRun = Down(EKeys::LeftShift) > 0.5f || Down(EKeys::Gamepad_LeftThumbstick) > 0.5f || (SonyDown & ESonyButton::L3);
	GetCharacterMovement()->MaxWalkSpeed = bRun ? RunSpeed : WalkSpeed;
	const FRotator Yaw(0.0, PC->GetControlRotation().Yaw, 0.0);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Fwd);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Side);

	// Looking: the mouse, and the right stick.
	float Mx = 0.0f, My = 0.0f;
	PC->GetInputMouseDelta(Mx, My);
	float LookX = S.bConnected ? Dead(S.RX) : Dead(Axis(EKeys::Gamepad_RightX));
	float LookY = S.bConnected ? Dead(S.RY) : Dead(Axis(EKeys::Gamepad_RightY));
	FRotator R = PC->GetControlRotation();
	R.Yaw += Mx * MouseDegPerPixel + LookX * PadDegPerSecond * Dt;
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch + My * MouseDegPerPixel + LookY * PadDegPerSecond * 0.7f * Dt), -85.0, 85.0);
	R.Roll = 0.0;
	PC->SetControlRotation(R);

	if (Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle)) Jump();
	bUse = Just(EKeys::E) || Just(EKeys::LeftMouseButton) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
	bRide = Just(EKeys::F) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle);
	if (Just(EKeys::H)) bShowHelp = !bShowHelp;
	const bool bPadQuit = (SonyDown & ESonyButton::Create) && (SonyDown & ESonyButton::Options);
	const bool bXboxQuit = Down(EKeys::Gamepad_Special_Left) > 0.5f && Down(EKeys::Gamepad_Special_Right) > 0.5f;
	if (Just(EKeys::Escape) || bPadQuit || bXboxQuit) PC->ConsoleCommand(TEXT("quit"));
}

namespace
{
// Where a line from Start to End first enters a part's box (0..1 along it), in the part's own
// frame so the box turns with it. The bike's meshes have no collision; their boxes are close enough
// to point at a silencer or a handlebar.
bool HitPartBox(const UStaticMeshComponent* C, const FVector& Start, const FVector& End, double& OutTime, double& OutVolume)
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
} // namespace

void ATorqueWalker::FindTarget()
{
	Target = ETorqueLook::None;
	TargetSlot = TargetPart = TargetModelPart = NAME_None;
	TargetMesh.Reset();
	UWorld* World = GetWorld();
	if (!World || !Camera) return;
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * Reach;

	// Walls, the bench and shelves stop the look; the parts are tested against their boxes.
	double Clear = 1.0;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerLook), false, this);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && !(Hit.GetActor() && Hit.GetActor()->IsA<ATorqueShedBike>()))
		Clear = Hit.Time + 0.02;   // a part lying on the bench top is just above what stopped the line

	// Of the boxes the line goes into, the smallest: a silencer inside the frame's box wins.
	double Best = TNumericLimits<double>::Max();
	auto Consider = [&](UStaticMeshComponent* C, ETorqueLook Kind)
	{
		double Time = 0.0, Volume = 0.0;
		if (!HitPartBox(C, Start, End, Time, Volume) || Time > Clear || Volume >= Best) return;
		Best = Volume;
		Target = Kind;
		TargetMesh = C;
	};
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		for (UStaticMeshComponent* C : It->Parts) Consider(C, ETorqueLook::BikePart);
	for (TActorIterator<ATorquePartsBench> It(World); It; ++It)
		for (UStaticMeshComponent* C : It->Items) Consider(C, ETorqueLook::Spare);
	if (UStaticMeshComponent* C = TargetMesh.Get())
	{
		TargetSlot = TorqueShedModel::SlotTag(C);
		TargetPart = TorqueShedModel::OptionTag(C);
		for (const FName& T : C->ComponentTags)
			if (T.ToString().StartsWith(TEXT("Part:"))) TargetModelPart = FName(*T.ToString().Mid(5));
		return;
	}
	// The roller door: stand in its opening (a point tagged RideOut by the level script).
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("RideOut"))) continue;
		if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 260.0) Target = ETorqueLook::Door;
	}
}

namespace
{
FString OptionName(FName Bike, FName Slot, FName Part)
{
	for (const FTorqueFitOption& O : TorqueFit::Options(Bike, Slot))
		if (O.PartId == Part) return O.Name.ToString();
	return Part.ToString();
}
} // namespace

void ATorqueWalker::Use()
{
	UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build) return;
	FName Part;
	if (Target == ETorqueLook::BikePart && !TargetSlot.IsNone()) Part = Build->NextOption(TargetSlot);
	else if (Target == ETorqueLook::Spare) Part = TargetPart;
	else
	{
		if (Target == ETorqueLook::BikePart)
		{
			Note = TEXT("Nothing to swap there yet: try the silencers or the handlebar");
			NoteFor = 2.5;
		}
		return;
	}
	const FString Off = OptionName(Build->Get().Bike, TargetSlot, Build->FittedIn(TargetSlot));
	const FString On = OptionName(Build->Get().Bike, TargetSlot, Part);
	FText WhyNot;
	if (Build->Fit(TargetSlot, Part, WhyNot))
		Note = FString::Printf(TEXT("Fitted: %s (%s on the bench)"), *On, *Off);
	else
		Note = FString::Printf(TEXT("Won't fit: %s"), *WhyNot.ToString());
	NoteFor = 3.0;
}

void ATorqueWalker::StartRideOut()
{
	if (GetNetMode() == NM_Client)
	{
		Note = TEXT("The host starts the ride");
		NoteFor = 2.5;
		return;
	}
	bRidingOut = true;
	RideOutIn = 0.4;   // long enough to show the line below
}

void ATorqueWalker::UpdateHud()
{
	HudLines.Reset();
	if (bRidingOut)
	{
		HudLines.Add({TEXT("Riding out to Buttertubs..."), FColor::White, 1.0f});
		return;
	}
	const UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	const FName Bike = Build ? Build->Get().Bike : FName(TEXT("speed_twin_1200"));
	const FColor Prompt(255, 214, 120);
	switch (Target)
	{
	case ETorqueLook::BikePart:
		if (!TargetSlot.IsNone() && Build)
		{
			HudLines.Add({FString::Printf(TEXT("%s: %s"), *TorqueShedModel::SlotLabel(TargetSlot),
				*OptionName(Bike, TargetSlot, Build->FittedIn(TargetSlot))), FColor::White, 0.8f});
			HudLines.Add({FString::Printf(TEXT("E / Cross: fit %s"), *OptionName(Bike, TargetSlot, Build->NextOption(TargetSlot))), Prompt, 0.65f});
		}
		else
		{
			HudLines.Add({FString::Printf(TEXT("Speed Twin 1200: %s"), *TargetModelPart.ToString().Replace(TEXT("_"), TEXT(" "))), FColor::White, 0.8f});
		}
		HudLines.Add({TEXT("F / Triangle: ride out"), Prompt, 0.65f});
		break;
	case ETorqueLook::Spare:
		HudLines.Add({FString::Printf(TEXT("On the bench: %s"), *OptionName(Bike, TargetSlot, TargetPart)), FColor::White, 0.8f});
		if (Build)
			HudLines.Add({FString::Printf(TEXT("E / Cross: fit it (the %s come off)"), *OptionName(Bike, TargetSlot, Build->FittedIn(TargetSlot)).ToLower()), Prompt, 0.65f});
		break;
	case ETorqueLook::Door:
		HudLines.Add({TEXT("Roller door"), FColor::White, 0.8f});
		HudLines.Add({TEXT("F / Triangle: ride out to Buttertubs"), Prompt, 0.65f});
		break;
	default:
		break;
	}
	if (NoteFor > 0.0) HudLines.Add({Note, FColor(170, 230, 170), 0.65f});
	if (bShowHelp)
	{
		const FColor Help(200, 200, 200);
		HudLines.Add({TEXT("WASD / left stick: walk   Shift / L3: hurry   mouse / right stick: look"), Help, 0.5f});
		HudLines.Add({TEXT("E / Cross: swap the part you look at, or fit a spare from the bench"), Help, 0.5f});
		HudLines.Add({TEXT("F / Triangle: ride out   Space / Circle: hop   H: hide help   Esc: quit"), Help, 0.5f});
	}
}
