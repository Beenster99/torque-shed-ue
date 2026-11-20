#include "Photo/TorqueDrone.h"

#include "Coop/TorqueKeys.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueUi.h"
#include "Photo/TorquePhotos.h"
#include "Shed/TorqueWalker.h"
#include "SonyPad.h"
#include "TorqueBikePawn.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace TorqueDroneImpl
{
TWeakObjectPtr<ATorqueDrone> GCurrent;   // the one up on this machine (one player here)
uint64 GClosedFrame = 0;                 // the frame it last came back in

// The apertures after "off" (deep focus): wider as R1 / E goes on.
const double Stops[] = {16.0, 11.0, 8.0, 5.6, 4.0, 2.8, 1.8};
constexpr int32 NumStops = UE_ARRAY_COUNT(Stops);
constexpr double MinMm = 24.0, MaxMm = 400.0;
constexpr int32 MaxEvThirds = 9;   // +/- 3 stops

// Horizontal field of view for a lens on a full frame (36 mm wide).
double FovOf(double Mm) { return FMath::RadiansToDegrees(2.0 * FMath::Atan(36.0 / (2.0 * Mm))); }

double Dead(double V, double Zone) { return FMath::Abs(V) < Zone ? 0.0 : FMath::Sign(V) * (FMath::Abs(V) - Zone) / (1.0 - Zone); }
double Either(double A, double B) { return FMath::Abs(A) >= FMath::Abs(B) ? A : B; }
} // namespace TorqueDroneImpl

namespace TDI = TorqueDroneImpl;

ATorqueDrone::ATorqueDrone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // (after the pilot's tick: the view is the drone's when it's drawn)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DroneBody")));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("DroneCamera"));
	Camera->SetupAttachment(GetRootComponent());
	Camera->SetFieldOfView((float)TDI::FovOf(TDI::MinMm));
	Camera->bConstrainAspectRatio = false;
	bReplicates = false;   // this player's own: nobody else sees it
}

ATorqueDrone* ATorqueDrone::For(const AController* Controller)
{
	ATorqueDrone* D = TDI::GCurrent.Get();
	return D && Controller && D->PC.Get() == Controller && !D->IsActorBeingDestroyed() ? D : nullptr;
}

bool ATorqueDrone::Launch(APawn* InPilot, FString& WhyNot)
{
	// The press that brought it back (read again by the pilot this frame or the next) doesn't send it up.
	if (GFrameCounter <= TDI::GClosedFrame + 1) return true;
	UWorld* Wd = InPilot ? InPilot->GetWorld() : nullptr;
	APlayerController* P = InPilot ? Cast<APlayerController>(InPilot->GetController()) : nullptr;
	if (!Wd || !P || !P->IsLocalController()) return false;
	if (For(P)) return true;
	FVector Eye;
	FRotator View;
	P->GetPlayerViewPoint(Eye, View);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATorqueDrone* D = Wd->SpawnActor<ATorqueDrone>(Eye, FRotator(0.0, View.Yaw, 0.0), Spawn);
	if (!D)
	{
		WhyNot = TEXT("The drone wouldn't go up.");
		return false;
	}
	D->PC = P;
	D->Pilot = InPilot;
	D->Home = Eye;
	D->Yaw = View.Yaw;
	// Starts looking ahead, a little down at most: a walker looking at the floor doesn't send it up staring at the grass.
	D->Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -20.0, 10.0);
	D->Gimbal = FRotator(D->Pitch, D->Yaw, 0.0);
	D->SetActorRotation(D->Gimbal);
	// Round the shed it's slower and stays close; out on a ride, the UK drone rules' 120 m up.
	D->bIndoor = InPilot->IsA<ATorqueWalker>();
	D->MaxRange = D->bIndoor ? 6000.0 : 20000.0;
	D->MaxUp = D->bIndoor ? 3000.0 : 12000.0;
	D->Ignore.Add(InPilot);
	TArray<AActor*> Theirs;
	InPilot->GetAttachedActors(Theirs, true, true);   // (the avatar, what's in their hands)
	for (AActor* A : Theirs) D->Ignore.Add(A);
	for (TActorIterator<APostProcessVolume> It(Wd); It; ++It) D->Volumes.Add(*It);
	D->OpenFrame = GFrameCounter;
	{
		const FSonyPadState Now = D->PadNow();   // (what's held as it goes up isn't pressed again)
		D->PrevPad = Now.bConnected ? Now.Buttons : 0;
	}
	// Up over your head, a little in front.
	D->Move(FVector(0.0, 0.0, 45.0) + FRotator(0.0, View.Yaw, 0.0).Vector() * 35.0);
	D->UpdateLens(1.0f);
	P->SetViewTargetWithBlend(D, 0.35f);
	TDI::GCurrent = D;
	D->Say(TEXT("Drone up: Cross / left mouse takes a photo"), 4.0);
	UE_LOG(LogTemp, Log, TEXT("Torque drone: up at (%.0f, %.0f, %.0f), %s"), Eye.X, Eye.Y, Eye.Z, D->bIndoor ? TEXT("round the shed") : TEXT("out on a ride"));
	return true;
}

void ATorqueDrone::Land()
{
	EndShot();
	if (APlayerController* P = PC.Get())
		if (APawn* Pw = Pilot.Get()) P->SetViewTargetWithBlend(Pw, 0.3f);
	UE_LOG(LogTemp, Log, TEXT("Torque drone: back down after %.0f s, %d photo(s)"), Flying, Shots);
	TDI::GClosedFrame = GFrameCounter;
	PC = nullptr;   // (For() won't find it)
	Destroy();
}

void ATorqueDrone::EndPlay(const EEndPlayReason::Type Reason)
{
	EndShot();
	if (TDI::GCurrent.Get() == this) TDI::GCurrent = nullptr;
	Super::EndPlay(Reason);
}

FSonyPadState ATorqueDrone::PadNow() const
{
	// (One reader of the PlayStation pad: the pilot's.)
	if (const ATorqueWalker* W = Cast<ATorqueWalker>(Pilot.Get())) return W->PadState();
	if (const ATorqueBikePawn* B = Cast<ATorqueBikePawn>(Pilot.Get())) return B->PadState();
	return FSonyPadState();
}

void ATorqueDrone::Say(const FString& Text, double Seconds)
{
	Note = Text;
	NoteFor = Seconds;
}

void ATorqueDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* P = PC.Get();
	APawn* Pw = Pilot.Get();
	if (!P)
	{
		Destroy();
		return;
	}
	// The pilot's gone (back to the shed, a new ride): it comes back with them.
	if (!Pw || P->GetPawn() != Pw)
	{
		Land();
		return;
	}
	Flying += DeltaSeconds;
	NoteFor = FMath::Max(0.0, NoteFor - DeltaSeconds);
	Flash = FMath::Max(0.0, Flash - DeltaSeconds);
	if (bShotWaiting)
	{
		ShotWait += DeltaSeconds;
		if (ShotWait > 2.0)
		{
			EndShot();
			Say(TEXT("The photo didn't come out: try again"));
		}
	}
	static const bool bNoInput = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"));
	if (!bNoInput) ReadInput(DeltaSeconds);
	if (!IsValid(this) || IsActorBeingDestroyed()) return;   // (landed)
	// The gimbal eases round to where it's pointed: steady, as a drone camera is.
	Gimbal = FMath::RInterpTo(Gimbal, FRotator(Pitch, Yaw, 0.0), DeltaSeconds, 12.0f);
	SetActorRotation(FRotator(Gimbal.Pitch, Gimbal.Yaw, 0.0));
	// A hover's slight drift (a centimetre or so), less as it's flown.
	const double Calm = FMath::Clamp(1.0 - Vel.Size() / 300.0, 0.0, 1.0);
	Camera->SetRelativeLocation(FVector(0.0, FMath::Sin(Flying * 0.9) * 0.8, FMath::Sin(Flying * 1.3 + 0.7) * 1.2) * Calm);
	UpdateLens(DeltaSeconds);
}

void ATorqueDrone::ReadInput(float Dt)
{
	APlayerController* P = PC.Get();
	if (!P) return;
	const FSonyPadState S = PadNow();
	const uint32 SonyDown = S.bConnected ? S.Buttons : 0;
	const uint32 SonyHit = SonyDown & ~PrevPad;
	PrevPad = SonyDown;
	if (GFrameCounter <= OpenFrame) return;   // (the press that sent it up)
	if (ATorquePlayerController::MenuUp(P)) return;
	auto Just = [P](const FKey& K) { return P->WasInputKeyJustPressed(K); };
	auto Down = [P](const FKey& K) { return P->IsInputKeyDown(K); };
	auto Axis = [P](const FKey& K) { return (double)P->GetInputAnalogKeyState(K); };
	const FTorqueSettings& Set = FTorqueSettings::Get();

	// Back down: the drone's button again, Circle / B, Esc.
	if (FTorqueKeys::Just(P, ETorqueKey::Drone) || Just(EKeys::Escape) || Just(EKeys::Gamepad_Special_Left) || Just(EKeys::Gamepad_FaceButton_Right) ||
		(SonyHit & (ESonyButton::Create | ESonyButton::Circle)))
	{
		Land();
		return;
	}
	if (FTorqueKeys::Just(P, ETorqueKey::Work) || Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross)) Shoot();
	if (Just(EKeys::G) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle)) bGrid = !bGrid;
	if (Just(EKeys::H) || Just(EKeys::Gamepad_FaceButton_Left) || (SonyHit & ESonyButton::Square)) bClean = !bClean;
	const bool bShift = Down(EKeys::LeftShift) || Down(EKeys::RightShift);
	if ((Just(EKeys::Tab) && !bShift) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight)) Setting = (Setting + 1) % 3;
	if ((Just(EKeys::Tab) && bShift) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft)) Setting = (Setting + 2) % 3;
	if (Just(EKeys::E) || Just(EKeys::Gamepad_RightShoulder) || (SonyHit & ESonyButton::R1)) Step(1);
	if (Just(EKeys::Q) || Just(EKeys::Gamepad_LeftShoulder) || (SonyHit & ESonyButton::L1)) Step(-1);
	if (Just(EKeys::Gamepad_RightThumbstick) || (SonyHit & ESonyButton::R3)) Pitch = 0.0;   // level the camera

	// Zoom: the D-pad's up / down held (+ / -), the wheel a step at a time.
	double Zoom = 0.0;
	if (Down(EKeys::Gamepad_DPad_Up) || (SonyDown & ESonyButton::DUp) || Down(EKeys::Equals) || Down(EKeys::Add)) Zoom += 1.0;
	if (Down(EKeys::Gamepad_DPad_Down) || (SonyDown & ESonyButton::DDown) || Down(EKeys::Hyphen) || Down(EKeys::Subtract)) Zoom -= 1.0;
	FocalWant *= FMath::Exp(Zoom * 1.1 * Dt);
	if (Just(EKeys::MouseScrollUp)) FocalWant *= 1.15;
	if (Just(EKeys::MouseScrollDown)) FocalWant /= 1.15;
	FocalWant = FMath::Clamp(FocalWant, TDI::MinMm, TDI::MaxMm);

	// Looking: slower the longer the lens, so a long shot can be lined up.
	const double Slow = FMath::Clamp(TDI::MinMm / FocalMm, 0.06, 1.0);
	float Mx = 0.0f, My = 0.0f;
	P->GetInputMouseDelta(Mx, My);
	Yaw += Mx * 0.143 * Set.MouseLook * Slow;
	Pitch += My * 0.143 * Set.MouseLook * Slow * (Set.bInvertLook ? -1.0 : 1.0);
	const double LookX = TDI::Dead(TDI::Either(Axis(EKeys::Gamepad_RightX), S.RX), 0.12) * Set.PadLook;
	const double LookY = TDI::Dead(TDI::Either(Axis(EKeys::Gamepad_RightY), S.RY), 0.12) * Set.PadLook * (Set.bInvertLook ? -1.0 : 1.0);
	Yaw += LookX * 95.0 * Dt * Slow;
	Pitch += LookY * 70.0 * Dt * Slow;
	Pitch = FMath::Clamp(Pitch, -90.0, 30.0);
	Yaw = FRotator::NormalizeAxis(Yaw);

	// Flying: the left stick (WASD), R2 / L2 (Space / C) up and down, L3 or Shift held fast.
	const double KeyX = (FTorqueKeys::Down(P, ETorqueKey::WalkRight) ? 1.0 : 0.0) - (FTorqueKeys::Down(P, ETorqueKey::WalkLeft) ? 1.0 : 0.0);
	const double KeyY = (FTorqueKeys::Down(P, ETorqueKey::WalkForward) ? 1.0 : 0.0) - (FTorqueKeys::Down(P, ETorqueKey::WalkBack) ? 1.0 : 0.0);
	const double StickX = TDI::Dead(TDI::Either(Axis(EKeys::Gamepad_LeftX), S.LX), Set.StickDeadZone);
	const double StickY = TDI::Dead(TDI::Either(Axis(EKeys::Gamepad_LeftY), S.LY), Set.StickDeadZone);
	FVector2D In(FMath::Abs(StickX) > FMath::Abs(KeyX) ? StickX : KeyX, FMath::Abs(StickY) > FMath::Abs(KeyY) ? StickY : KeyY);
	if (In.SizeSquared() > 1.0) In.Normalize();
	const double Up = FMath::Max3(FTorqueKeys::Down(P, ETorqueKey::Jump) ? 1.0 : 0.0, Axis(EKeys::Gamepad_RightTriggerAxis), (double)S.R2);
	const double Sink = FMath::Max3(FTorqueKeys::Down(P, ETorqueKey::Crouch) ? 1.0 : 0.0, Axis(EKeys::Gamepad_LeftTriggerAxis), (double)S.L2);
	const bool bFast = FTorqueKeys::Down(P, ETorqueKey::Run) || Down(EKeys::Gamepad_LeftThumbstick) || (SonyDown & ESonyButton::L3);
	const double Speed = (bFast ? 1700.0 : 650.0) * (bIndoor ? 0.4 : 1.0);   // cm/s: about 15 mph, 38 fast
	const double Climb = (bFast ? 650.0 : 300.0) * (bIndoor ? 0.6 : 1.0);
	const FVector Want = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(In.Y, In.X, 0.0)) * Speed + FVector(0.0, 0.0, (Up - Sink) * Climb);
	// Eased, as a drone speeds up and comes to a hover.
	Vel = FMath::VInterpTo(Vel, Want, Dt, 2.6f);
	if (Vel.SizeSquared() > 1.0) Move(Vel * Dt);
}

void ATorqueDrone::Step(int32 Dir)
{
	switch (Setting)
	{
	case 0:
		EvThirds = FMath::Clamp(EvThirds + Dir, -TDI::MaxEvThirds, TDI::MaxEvThirds);
		break;
	case 1:
		Aperture = FMath::Clamp(Aperture + Dir, 0, TDI::NumStops);
		break;
	default:
		Filter = (Filter + Dir + TorquePhotos::NumFilters()) % TorquePhotos::NumFilters();
		break;
	}
}

FString ATorqueDrone::SettingText(int32 Which) const
{
	switch (Which)
	{
	case 0:
		return EvThirds == 0 ? FString(TEXT("EV 0")) : FString::Printf(TEXT("EV %+.1f"), EvThirds / 3.0);
	case 1:
		return Aperture == 0 ? FString(TEXT("Focus: all sharp")) : FString::Printf(TEXT("f/%.1f"), TDI::Stops[Aperture - 1]).Replace(TEXT(".0"), TEXT(""));
	default:
		return TorquePhotos::FilterName(Filter);
	}
}

void ATorqueDrone::Move(const FVector& Delta)
{
	UWorld* Wd = GetWorld();
	if (!Wd) return;
	FVector At = GetActorLocation();
	FVector To = At + Delta;
	// Its limits: so far from where it went up, so high over it (and no deeper than a valley's 50 m).
	bAtLimit = false;
	const FVector2D Off(To.X - Home.X, To.Y - Home.Y);
	if (Off.Size() > MaxRange)
	{
		const FVector2D In = Off.GetSafeNormal() * MaxRange;
		To.X = Home.X + In.X;
		To.Y = Home.Y + In.Y;
		bAtLimit = true;
	}
	if (To.Z > Home.Z + MaxUp)
	{
		To.Z = Home.Z + MaxUp;
		bAtLimit = true;
	}
	To.Z = FMath::Max(To.Z, Home.Z - 5000.0);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(TorqueDrone), false, this);
	for (const TWeakObjectPtr<AActor>& A : Ignore)
		if (A.IsValid()) Q.AddIgnoredActor(A.Get());
	// Stops at what it meets and slides along it (a wall, the ground, a tree's trunk).
	for (int32 Try = 0; Try < 3; ++Try)
	{
		if ((To - At).IsNearlyZero(0.01)) break;
		FHitResult Hit;
		if (!Wd->SweepSingleByChannel(Hit, At, To, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(18.0f), Q))
		{
			At = To;
			break;
		}
		if (Hit.bStartPenetrating)
		{
			At = To;   // (started inside something: let it out)
			break;
		}
		At = Hit.Location + Hit.ImpactNormal * 0.5;
		To = At + FVector::VectorPlaneProject(To - At, Hit.ImpactNormal);
		Vel = FVector::VectorPlaneProject(Vel, Hit.ImpactNormal);
	}
	SetActorLocation(At);
}

double ATorqueDrone::LevelExposure() const
{
	// What the level's exposure is here (the sky sets it on the world's volume as the light changes),
	// so the photo's EV is on top of it rather than in place of it.
	static const float Default = FPostProcessSettings().AutoExposureBias;
	double Bias = Default;
	float Best = -FLT_MAX;
	const FVector At = GetActorLocation();
	for (const TWeakObjectPtr<APostProcessVolume>& V : Volumes)
	{
		const APostProcessVolume* Vol = V.Get();
		if (!Vol || !Vol->bEnabled || !Vol->Settings.bOverride_AutoExposureBias || Vol->Priority < Best) continue;
		if (!Vol->bUnbound && !const_cast<APostProcessVolume*>(Vol)->EncompassesPoint(At, 0.0f, nullptr)) continue;
		Best = Vol->Priority;
		Bias = Vol->Settings.AutoExposureBias;
	}
	// In the shed: the walker's own (its white rooms', blended in by its weight).
	if (const ATorqueWalker* W = Cast<ATorqueWalker>(Pilot.Get()); W && W->Camera && W->Camera->PostProcessSettings.bOverride_AutoExposureBias)
		Bias = FMath::Lerp(Bias, (double)W->Camera->PostProcessSettings.AutoExposureBias, (double)W->Camera->PostProcessBlendWeight);
	return Bias;
}

void ATorqueDrone::UpdateLens(float Dt)
{
	// The zoom eases to where it's wanted.
	FocalMm = FMath::Exp(FMath::FInterpTo(FMath::Loge(FocalMm), FMath::Loge(FocalWant), (double)Dt, 9.0));
	Camera->SetFieldOfView((float)TDI::FovOf(FocalMm));
	// Focus on what's in the middle of the frame.
	double Target = 100000.0;
	if (UWorld* Wd = GetWorld())
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(TorqueDroneFocus), false, this);
		for (const TWeakObjectPtr<AActor>& A : Ignore)
			if (A.IsValid()) Q.AddIgnoredActor(A.Get());
		const FVector From = Camera->GetComponentLocation();
		FHitResult Hit;
		if (Wd->LineTraceSingleByChannel(Hit, From, From + Camera->GetForwardVector() * 200000.0, ECC_Visibility, Q)) Target = FMath::Max(Hit.Distance, 30.0);
	}
	const double L = FMath::FInterpTo(FMath::Loge(FocusCm), FMath::Loge(Target), (double)Dt, 6.0);
	bFocused = FMath::Abs(L - FMath::Loge(Target)) < 0.04;
	FocusCm = FMath::Exp(L);

	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP = FPostProcessSettings();
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = (float)(LevelExposure() + EvThirds / 3.0);
	PP.bOverride_MotionBlurAmount = true;   // (a photo is sharp)
	PP.MotionBlurAmount = 0.0f;
	// Depth of field as a full frame camera's: the blur grows with the lens and the aperture.
	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = 36.0f;
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = Aperture > 0 ? (float)FocusCm : 0.0f;   // (0: no depth of field at all)
	if (Aperture > 0)
	{
		PP.bOverride_DepthOfFieldFstop = true;
		PP.DepthOfFieldFstop = (float)TDI::Stops[Aperture - 1];
	}
	TorquePhotos::ApplyFilter(Filter, PP);
	Camera->PostProcessBlendWeight = 1.0f;
}

// --- The photo ---------------------------------------------------------------------------------------------

void ATorqueDrone::Shoot()
{
	if (bShotWaiting) return;
	// The screen as it's drawn this frame, handed to us (not saved by the engine), the HUD left off it.
	ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ATorqueDrone::Shot);
	bShotWaiting = true;
	ShotWait = 0.0;
	ShotFrame = GFrameCounter;
	FScreenshotRequest::RequestScreenshot(false);
}

void ATorqueDrone::EndShot()
{
	// Unbound straight after, or every later screenshot would come here instead of to its file.
	if (ShotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
	ShotHandle.Reset();
	bShotWaiting = false;
}

void ATorqueDrone::Shot(int32 W, int32 H, const TArray<FColor>& Pixels)
{
	EndShot();
	Flash = 0.22;
	++Shots;
	TArray<FColor> Copy = Pixels;
	TWeakObjectPtr<ATorqueDrone> Self(this);
	const FString Filtered = TorquePhotos::FilterName(Filter);
	TorquePhotos::SaveShot(W, H, MoveTemp(Copy), [Self, W, H, Filtered](const FString& Path)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque drone: the photo couldn't be saved in %s"), *TorquePhotos::Folder());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Torque drone: photo %d x %d (%s) saved as %s"), W, H, *Filtered, *Path);
		}
		ATorqueDrone* D = Self.Get();
		if (!D) return;
		D->LastPhoto = Path;
		D->Say(Path.IsEmpty() ? FString(TEXT("The photo couldn't be saved")) : FString::Printf(TEXT("Saved: Pictures > Torque Shed > %s"), *FPaths::GetCleanFilename(Path)), 4.0);
	});
}

bool ATorqueDrone::HidesHud() const { return bShotWaiting || GFrameCounter <= ShotFrame + 1; }

// --- The viewfinder ------------------------------------------------------------------------------------------

void ATorqueDrone::DrawHud(AHUD* Hud, UCanvas* Canvas)
{
	if (!Canvas || HidesHud()) return;
	const float S = TorqueUi::Scale(Canvas);
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	// The shutter's flash.
	if (Flash > 0.0) TorqueUi::Rect(Canvas, 0.0f, 0.0f, CW, CH, FLinearColor(1.0f, 1.0f, 1.0f, (float)(Flash / 0.22) * 0.55f));
	if (bGrid)
	{
		const FLinearColor G(1.0f, 1.0f, 1.0f, 0.22f);
		for (int32 I = 1; I <= 2; ++I)
		{
			TorqueUi::Rect(Canvas, CW * I / 3.0f, 0.0f, FMath::Max(1.0f, S), CH, G);
			TorqueUi::Rect(Canvas, 0.0f, CH * I / 3.0f, CW, FMath::Max(1.0f, S), G);
		}
	}
	if (bClean) return;   // (Square / H: the view alone)
	// The frame's corners.
	const float M = 40.0f * S, L = 46.0f * S, T = 2.0f * S;
	const FLinearColor Edge(1.0f, 1.0f, 1.0f, 0.75f);
	for (int32 Cx = 0; Cx < 2; ++Cx)
		for (int32 Cy = 0; Cy < 2; ++Cy)
		{
			const float X = Cx ? CW - M : M, Y = Cy ? CH - M : M;
			TorqueUi::Rect(Canvas, Cx ? X - L : X, Cy ? Y - T : Y, L, T, Edge);
			TorqueUi::Rect(Canvas, Cx ? X - T : X, Cy ? Y - L : Y, T, L, Edge);
		}
	// The focus box in the middle: green once it's found what's there.
	{
		const float B = 34.0f * S;
		const FLinearColor F = Aperture == 0 ? FLinearColor(1.0f, 1.0f, 1.0f, 0.55f) : (bFocused ? FLinearColor(0.45f, 0.95f, 0.5f, 0.95f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
		TorqueUi::Frame(Canvas, CW * 0.5f - B * 0.5f, CH * 0.5f - B * 0.5f, B, B, F, FMath::Max(1.0f, 1.5f * S));
	}
	// Along the top: the lens, then the three settings, the one L1 / R1 change lit.
	{
		const float Px = 21.0f * S, Y = M + 10.0f * S;
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%.0f mm  %.1fx"), FocalMm, FocalMm / TDI::MinMm));
		for (int32 I = 0; I < 3; ++I) Parts.Add(SettingText(I));
		const float Gap = 34.0f * S;
		float W = 0.0f;
		for (const FString& P : Parts) W += (float)TorqueUi::Measure(P, Px, true).X + Gap;
		W -= Gap;
		float X = CW * 0.5f - W * 0.5f;
		TorqueUi::Rect(Canvas, X - 18.0f * S, Y - 8.0f * S, W + 36.0f * S, Px * 1.2f + 16.0f * S, TorqueUi::Panel);
		for (int32 I = 0; I < Parts.Num(); ++I)
		{
			const bool bLit = I == Setting + 1;
			const float PW = TorqueUi::Text(Canvas, Parts[I], X, Y, Px, bLit ? TorqueUi::Accent : TorqueUi::Ink, true);
			if (bLit) TorqueUi::Rect(Canvas, X, Y + Px * 1.25f, PW, 2.0f * S, TorqueUi::Accent);
			X += PW + Gap;
		}
		const FString Which = Setting == 0 ? TEXT("Exposure") : Setting == 1 ? TEXT("Aperture") : TEXT("Filter");
		TorqueUi::Text(Canvas, FString::Printf(TEXT("%s: L1 / R1 (Q / E) change it    D-pad left / right (Tab): another setting"), *Which),
			CW * 0.5f, Y + Px * 1.2f + 16.0f * S, 15.0f * S, TorqueUi::Dim, false, TorqueUi::EAlign::Centre);
	}
	// Bottom left: height, how far out, the way it's facing.
	{
		const FVector At = GetActorLocation();
		const double Alt = (At.Z - Home.Z) / 100.0, Dist = FVector::Dist2D(At, Home) / 100.0;
		static const TCHAR* Points[] = {TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW")};
		// (On the Ride UK map north is the world's -y and east its +x, as the bike's screen has it.)
		const FVector F = FRotator(0.0, Gimbal.Yaw, 0.0).Vector();
		const double Heading = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(F.X, -F.Y)) + 360.0, 360.0);
		const FString Line = FString::Printf(TEXT("UP %.1f m    OUT %.0f m    %s %03.0f    %d photo%s"), Alt, Dist,
			Points[FMath::RoundToInt(Heading / 45.0) % 8], Heading, Shots, Shots == 1 ? TEXT("") : TEXT("s"));
		TorqueUi::Text(Canvas, Line, M + 10.0f * S, CH - M - 44.0f * S, 18.0f * S, TorqueUi::Ink, true);
		if (bAtLimit) TorqueUi::Text(Canvas, bIndoor ? TEXT("As far as it goes round the shed") : TEXT("At its limit: 120 m up, 200 m out"),
			M + 10.0f * S, CH - M - 70.0f * S, 17.0f * S, TorqueUi::Warn, true);
	}
	// What the buttons do, under it.
	{
		const FSonyPadState Pad = PadNow();
		const FString Keys = Pad.bConnected
			? TEXT("Left stick fly   R2 / L2 up, down   Right stick look   D-pad up / down zoom   Cross photo   Triangle grid   Square hide   R3 level   Circle land")
			: FString::Printf(TEXT("WASD fly   Space / C up, down   Shift fast   Mouse look   Wheel zoom   Left mouse photo   G grid   H hide   %s or Esc land"),
				*FTorqueKeys::Text(ETorqueKey::Drone));
		TorqueUi::TextFit(Canvas, Keys, M + 10.0f * S, CH - M - 20.0f * S, 15.0f * S, CW - 2.0f * M - 20.0f * S, TorqueUi::Dim);
	}
	// The last photo, small, with where it went.
	if (NoteFor > 0.0 && !Note.IsEmpty())
	{
		const float Px = 18.0f * S;
		const float Alpha = (float)FMath::Clamp(NoteFor / 0.5, 0.0, 1.0);
		float Y = CH - M - 110.0f * S;
		if (UTexture2D* Thumb = LastPhoto.IsEmpty() ? nullptr : TorquePhotos::Thumb(LastPhoto))
		{
			const float TW = 192.0f * S, TH = TW * Thumb->GetSizeY() / FMath::Max(1, Thumb->GetSizeX());
			Y -= TH + 10.0f * S;
			TorqueUi::Rect(Canvas, CW - M - TW - 14.0f * S, Y - 4.0f * S, TW + 8.0f * S, TH + 8.0f * S, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f * Alpha));
			FCanvasTileItem Tile(FVector2D(CW - M - TW - 10.0f * S, Y), Thumb->GetResource(), FVector2D(TW, TH), FLinearColor(1.0f, 1.0f, 1.0f, Alpha));
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
			Y += TH + 14.0f * S;
		}
		TorqueUi::Text(Canvas, Note, CW - M - 10.0f * S, Y, Px, FLinearColor(TorqueUi::Ink.R, TorqueUi::Ink.G, TorqueUi::Ink.B, Alpha), true, TorqueUi::EAlign::Right);
	}
}

// --- Tests ---------------------------------------------------------------------------------------------------

FString ATorqueDrone::TestShot()
{
	Shoot();
	return FString::Printf(TEXT("drone: photo asked for (%s, %.0f mm, %s, %s)"), *SettingText(2), FocalWant, *SettingText(0), *SettingText(1));
}

FString ATorqueDrone::TestSet(const FString& What, double Value)
{
	if (What == TEXT("zoom")) FocalWant = FocalMm = FMath::Clamp(Value, TDI::MinMm, TDI::MaxMm);
	else if (What == TEXT("ev")) EvThirds = FMath::Clamp(FMath::RoundToInt(Value * 3.0), -TDI::MaxEvThirds, TDI::MaxEvThirds);
	else if (What == TEXT("filter")) Filter = FMath::Clamp((int32)Value, 0, TorquePhotos::NumFilters() - 1);
	else if (What == TEXT("fstop"))
	{
		Aperture = 0;
		for (int32 I = 0; I < TDI::NumStops; ++I)
			if (Value > 0.0 && FMath::Abs(TDI::Stops[I] - Value) < 0.05) Aperture = I + 1;
	}
	else if (What == TEXT("pitch")) Pitch = FMath::Clamp(Value, -90.0, 30.0);
	else if (What == TEXT("yaw")) Yaw = FRotator::NormalizeAxis(Value);
	else return FString::Printf(TEXT("drone: no setting %s (zoom, ev, fstop, filter, pitch, yaw)"), *What);
	// A test's setting is there at once (the gimbal doesn't ease round to it), so a shot straight after shows it.
	Gimbal = FRotator(Pitch, Yaw, 0.0);
	SetActorRotation(Gimbal);
	UpdateLens(1.0f);
	return TestStatus();
}

FString ATorqueDrone::TestFly(const FVector& By)
{
	Move(By);
	return TestStatus();
}

FString ATorqueDrone::TestStatus() const
{
	const FVector At = GetActorLocation();
	const FRotator Looks = Camera ? Camera->GetComponentRotation() : GetActorRotation();
	return FString::Printf(TEXT("drone: at (%.0f, %.0f, %.0f), %.1f m up, %.0f m out, looking pitch %.0f yaw %.0f, %.0f mm, %s, %s, %s, focus %.1f m, %d photo(s)%s"), At.X, At.Y, At.Z,
		(At.Z - Home.Z) / 100.0, FVector::Dist2D(At, Home) / 100.0, Looks.Pitch, Looks.Yaw, FocalMm, *SettingText(0), *SettingText(1), *SettingText(2), FocusCm / 100.0, Shots,
		bAtLimit ? TEXT(", at its limit") : TEXT(""));
}
