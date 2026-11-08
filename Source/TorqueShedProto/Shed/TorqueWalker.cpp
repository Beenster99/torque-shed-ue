#include "Shed/TorqueWalker.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorquePartsBench.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorquePaintRack.h"
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

	// A grey-box body so other players in a co-op session can see you: legs, a torso in a work
	// jacket, arms, a head and a cap peak that shows which way you face. You don't see your own.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Engine/BasicShapes/Cube.Cube"));
	struct FBit { const TCHAR* Name; int32 Shape; FVector At, Size; };   // capsule-centre cm; 0 cylinder, 1 sphere, 2 cube
	const FBit Bits[] = {
		{TEXT("Body_LegL"), 0, FVector(0, -10, -45), FVector(15, 15, 88)},
		{TEXT("Body_LegR"), 0, FVector(0, 10, -45), FVector(15, 15, 88)},
		{TEXT("Body_Torso"), 0, FVector(0, 0, 25), FVector(26, 40, 62)},
		{TEXT("Body_ArmL"), 0, FVector(0, -25, 22), FVector(10, 10, 60)},
		{TEXT("Body_ArmR"), 0, FVector(0, 25, 22), FVector(10, 10, 60)},
		{TEXT("Body_Head"), 1, FVector(0, 0, 70), FVector(21, 19, 24)},
		{TEXT("Body_CapPeak"), 2, FVector(12, 0, 77), FVector(10, 18, 2)},
	};
	for (const FBit& B : Bits)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(B.Name);
		C->SetupAttachment(GetCapsuleComponent());
		C->SetStaticMesh(B.Shape == 0 ? Cyl.Object : B.Shape == 1 ? Ball.Object : Block.Object);
		C->SetRelativeLocation(B.At);
		C->SetRelativeScale3D(B.Size / 100.0);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetOwnerNoSee(true);
		C->SetCanEverAffectNavigation(false);
	}

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
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueProfile"))) { bPerfShow = true; bPerfLog = true; }
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
	UpdatePerf(DeltaSeconds);
	bool bUse = false, bRide = false;
	int32 Pick = 0;
	// Hands-off test runs (-TorqueNoInput) ignore the pad, keys and mouse, as on the bike.
	static const bool bNoInput = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"));
	if (!bNoInput) ReadInput(DeltaSeconds, bUse, bRide, Pick);
	FindTarget();
	if (Pick != 0) Choose(Pick);
	if (bUse) Use();
	if (bWashHeld || WashHeldFor > 0.0) Wash(DeltaSeconds);
	else WashedFor = 0.0;
	WashHeldFor = FMath::Max(WashHeldFor - DeltaSeconds, 0.0);
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

void ATorqueWalker::ReadInput(float Dt, bool& bUse, bool& bRide, int32& Pick)
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
	bWashHeld = Down(EKeys::G) > 0.5f || Down(EKeys::Gamepad_FaceButton_Left) > 0.5f || (SonyDown & ESonyButton::Square) != 0;
	bUse = Just(EKeys::E) || Just(EKeys::LeftMouseButton) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
	bRide = Just(EKeys::F) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle);
	if (Just(EKeys::H)) bShowHelp = !bShowHelp;
	if (Just(EKeys::R) || Just(EKeys::MouseScrollDown) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight)) Pick = 1;
	if (Just(EKeys::Q) || Just(EKeys::MouseScrollUp) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft)) Pick = -1;
	if (Just(EKeys::P)) bPerfShow = !bPerfShow;
	if (Just(EKeys::O))
	{
		bPerfLog = !bPerfLog;
		if (!bPerfLog) PerfFile.Reset();
	}
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
	TargetPaint = INDEX_NONE;
	TargetMesh.Reset();
	UWorld* World = GetWorld();
	if (!World || !Camera) return;
	const FVector Start = Camera->GetComponentLocation();
	// Where the player looks (the camera follows it a frame later).
	const FVector Look = Controller ? GetControlRotation().Vector() : Camera->GetForwardVector();
	const FVector End = Start + Look * Reach;

	// Walls, the bench and shelves stop the look; the parts are tested against their boxes.
	double Clear = 1.0;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWalkerLook), false, this);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && !(Hit.GetActor() && Hit.GetActor()->IsA<ATorqueShedBike>()))
		Clear = Hit.Time + 0.02;   // a part lying on the bench top is just above what stopped the line

	// Of the boxes the line goes into near where it first meets the bike (within 30 cm), a part
	// that can be swapped wins over one that can't (the bars over the clocks in front of them),
	// then the smallest box (a silencer inside the frame's box).
	struct FHitBox { UStaticMeshComponent* C; ETorqueLook Kind; double Time, Volume; bool bSlot; };
	TArray<FHitBox> Hits;
	auto Consider = [&](UStaticMeshComponent* C, ETorqueLook Kind)
	{
		double Time = 0.0, Volume = 0.0;
		if (!HitPartBox(C, Start, End, Time, Volume) || Time > Clear) return;
		Hits.Add({C, Kind, Time, Volume, !TorqueShedModel::SlotTag(C).IsNone()});
	};
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		for (UStaticMeshComponent* C : It->Parts) Consider(C, ETorqueLook::BikePart);
	for (TActorIterator<ATorquePartsBench> It(World); It; ++It)
		for (UStaticMeshComponent* C : It->Items) Consider(C, ETorqueLook::Spare);
	for (TActorIterator<ATorquePaintRack> It(World); It; ++It)
		for (UStaticMeshComponent* C : It->Cards) Consider(C, ETorqueLook::Swatch);
	double First = TNumericLimits<double>::Max();
	for (const FHitBox& H : Hits) First = FMath::Min(First, H.Time);
	const double Near = First + 30.0 / FMath::Max((double)Reach, 1.0);
	const FHitBox* Pick = nullptr;
	for (const FHitBox& H : Hits)
	{
		if (H.Time > Near) continue;
		if (!Pick || (H.bSlot && !Pick->bSlot) || (H.bSlot == Pick->bSlot && H.Volume < Pick->Volume)) Pick = &H;
	}
	if (Pick)
	{
		Target = Pick->Kind;
		TargetMesh = Pick->C;
		TargetPoint = Start + Look * Reach * Pick->Time;
		TargetPaint = INDEX_NONE;
		if (Target == ETorqueLook::Swatch)
			if (const ATorquePaintRack* Rack = Cast<ATorquePaintRack>(Pick->C->GetOwner())) TargetPaint = Rack->PaintOf(Pick->C);
	}
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
	if (Target == ETorqueLook::BikePart && !TargetSlot.IsNone()) Part = ChosenFor(TargetSlot);
	else if (Target == ETorqueLook::Spare) Part = TargetPart;
	else if (Target == ETorqueLook::Swatch && ATorquePaintRack::Paints().IsValidIndex(TargetPaint))
	{
		PickedPaint = TargetPaint;
		Note = FString::Printf(TEXT("Picked %s: E on the tank, side panels, mudguard or tail paints it"), ATorquePaintRack::Paints()[PickedPaint].Name);
		NoteFor = 3.0;
		return;
	}
	else if (Target == ETorqueLook::BikePart && TorqueShedModel::IsBodywork(TargetModelPart))
	{
		if (!ATorquePaintRack::Paints().IsValidIndex(PickedPaint))
			Note = TEXT("Pick a colour off the paint rack first");
		else
		{
			Build->SetPaint(TargetModelPart, ATorquePaintRack::Paints()[PickedPaint].Colour);
			Note = FString::Printf(TEXT("Painted the %s %s"), *TorqueShedModel::PartLabel(TargetModelPart), ATorquePaintRack::Paints()[PickedPaint].Name);
		}
		NoteFor = 3.0;
		return;
	}
	else
	{
		if (Target == ETorqueLook::BikePart)
		{
			Note = TEXT("Nothing to swap there yet: try the silencers or the handlebar (or hold G / Square to wash it)");
			NoteFor = 2.5;
		}
		return;
	}
	const FString Off = OptionName(Build->Get().Bike, TargetSlot, Build->FittedIn(TargetSlot));
	const FString On = OptionName(Build->Get().Bike, TargetSlot, Part);
	if (Part.IsNone() || Part == Build->FittedIn(TargetSlot))
	{
		Note = FString::Printf(TEXT("%s is already on: Q / R or the D-pad chooses another"), *On);
		NoteFor = 3.0;
		return;
	}
	// A part that won't fit leaves the bike as it was.
	FText WhyNot;
	if (Build->Fit(TargetSlot, Part, WhyNot))
	{
		Note = FString::Printf(TEXT("Fitted: %s (%s on the bench)"), *On, *Off);
		Chosen.Remove(TargetSlot);
	}
	else
		Note = FString::Printf(TEXT("Won't fit: %s: %s"), *On, *WhyNot.ToString());
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
			const TArray<FTorqueFitOption> Opts = TorqueFit::Options(Bike, TargetSlot);
			const FName Pick = ChosenFor(TargetSlot);
			int32 At = 0;
			for (int32 I = 0; I < Opts.Num(); ++I)
				if (Opts[I].PartId == Pick) At = I + 1;
			HudLines.Add({FString::Printf(TEXT("E / Cross: fit %s   (Q / R, wheel or D-pad: choose, %d of %d)"),
				*OptionName(Bike, TargetSlot, Pick), At, Opts.Num()), Prompt, 0.65f});
		}
		else
		{
			HudLines.Add({FString::Printf(TEXT("Speed Twin 1200: %s"), *TargetModelPart.ToString().Replace(TEXT("_"), TEXT(" "))), FColor::White, 0.8f});
		}
		if (TorqueShedModel::IsBodywork(TargetModelPart))
			HudLines.Add({ATorquePaintRack::Paints().IsValidIndex(PickedPaint)
				? FString::Printf(TEXT("E / Cross: paint it %s"), ATorquePaintRack::Paints()[PickedPaint].Name)
				: FString(TEXT("Paint: pick a colour off the rack by the side door")), Prompt, 0.65f});
		if (Build)
			HudLines.Add({FString::Printf(TEXT("Dirt %.0f%%   hold G / Square: wash"), 100.0f * Build->DirtOf(TargetModelPart)), Prompt, 0.65f});
		HudLines.Add({TEXT("F / Triangle: ride out"), Prompt, 0.65f});
		break;
	case ETorqueLook::Swatch:
		if (ATorquePaintRack::Paints().IsValidIndex(TargetPaint))
		{
			HudLines.Add({FString::Printf(TEXT("Paint: %s"), ATorquePaintRack::Paints()[TargetPaint].Name), FColor::White, 0.8f});
			HudLines.Add({TEXT("E / Cross: pick it"), Prompt, 0.65f});
		}
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
	if (bPerfShow && !PerfText.IsEmpty()) HudLines.Add({PerfText, FColor(255, 255, 160), 0.55f});
	if (bShowHelp)
	{
		const FColor Help(200, 200, 200);
		HudLines.Add({TEXT("WASD / left stick: walk   Shift / L3: hurry   mouse / right stick: look"), Help, 0.5f});
		HudLines.Add({TEXT("E / Cross: fit the chosen part (Q / R, wheel or D-pad left/right to choose), or a spare from the bench"), Help, 0.5f});
		HudLines.Add({TEXT("Hold G / Square: wash the bike   paint rack: E picks a colour, E on bodywork paints it"), Help, 0.5f});
		HudLines.Add({TEXT("F / Triangle: ride out   Space / Circle: hop   H: hide help   Esc: quit"), Help, 0.5f});
	}
}

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

FString ATorqueWalker::Describe() const
{
	switch (Target)
	{
	case ETorqueLook::BikePart: return FString::Printf(TEXT("bike part %s (slot %s)"), *TargetModelPart.ToString(), *TargetSlot.ToString());
	case ETorqueLook::Spare: return FString::Printf(TEXT("spare %s for %s (%s)"), *TargetPart.ToString(), *TargetSlot.ToString(), *TargetModelPart.ToString());
	case ETorqueLook::Door: return TEXT("the roller door");
	case ETorqueLook::Swatch: return FString::Printf(TEXT("paint card %d"), TargetPaint);
	default: return TEXT("nothing");
	}
}

FString ATorqueWalker::TestLookAt(const FVector& Point, const FVector& Away, double Distance)
{
	FVector Side = FVector(Away.X, Away.Y, 0.0).GetSafeNormal();
	if (Side.IsNearlyZero()) Side = FVector(1.0, 0.0, 0.0);
	// Standing on the floor under the spot, the capsule's centre 90 cm up (the floor is z 0 in the shed).
	const double FloorZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Stand(Point.X + Side.X * Distance, Point.Y + Side.Y * Distance, FloorZ + GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Eye = Camera->GetComponentLocation();
	const FRotator View = (Point - Eye).Rotation();
	if (AController* C = GetController()) C->SetControlRotation(View);
	Camera->SetWorldRotation(View);
	FindTarget();
	UpdateHud();
	return Describe();
}

FString ATorqueWalker::TestPress(bool bRideOut)
{
	FindTarget();
	const FString Seen = Describe();
	if (bRideOut)
	{
		if (Target == ETorqueLook::Door || Target == ETorqueLook::BikePart)
		{
			StartRideOut();
			return FString::Printf(TEXT("F on %s: %s"), *Seen, bRidingOut ? TEXT("riding out") : *Note);
		}
		return FString::Printf(TEXT("F on %s: nothing to ride out from"), *Seen);
	}
	Note.Reset();
	Use();
	UpdateHud();
	return FString::Printf(TEXT("E on %s: %s"), *Seen, Note.IsEmpty() ? TEXT("nothing happened") : *Note);
}

FName ATorqueWalker::ChosenFor(FName Slot) const
{
	const UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build) return NAME_None;
	if (const FName* C = Chosen.Find(Slot))
		if (*C != Build->FittedIn(Slot)) return *C;
	return Build->NextOption(Slot);
}

void ATorqueWalker::Choose(int32 Step)
{
	if (Target != ETorqueLook::BikePart || TargetSlot.IsNone()) return;
	const UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build) return;
	// Every option but the one that's on, round and round.
	const FName Fitted = Build->FittedIn(TargetSlot);
	TArray<FName> Others;
	for (const FTorqueFitOption& O : TorqueFit::Options(Build->Get().Bike, TargetSlot))
		if (O.PartId != Fitted) Others.Add(O.PartId);
	if (Others.Num() == 0) return;
	const int32 Now = FMath::Max(Others.IndexOfByKey(ChosenFor(TargetSlot)), 0);
	Chosen.Add(TargetSlot, Others[(Now + Step + Others.Num()) % Others.Num()]);
}

void ATorqueWalker::Wash(float Dt)
{
	UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build || Target != ETorqueLook::BikePart || TargetModelPart.IsNone()) return;
	// The part looked at comes clean in about two seconds; whatever is within 30 cm of the sponge
	// in about three.
	Build->Clean(TargetModelPart, 0.5f * Dt, false);
	for (TActorIterator<ATorqueShedBike> It(GetWorld()); It; ++It)
		for (const UStaticMeshComponent* C : It->Parts)
		{
			if (!C || C->Bounds.GetBox().ComputeSquaredDistanceToPoint(TargetPoint) > 30.0 * 30.0) continue;
			const FName P = TorqueShedModel::PartTag(C);
			if (!P.IsNone() && P != TargetModelPart) Build->Clean(P, 0.33f * Dt, false);
		}
	Build->TellLooks();
	WashedFor += Dt;
	Note = FString::Printf(TEXT("Washing the %s: %.0f%% dirt left"), *TorqueShedModel::PartLabel(TargetModelPart), 100.0f * Build->DirtOf(TargetModelPart));
	NoteFor = 1.0;
}
