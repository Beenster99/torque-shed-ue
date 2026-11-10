#include "Weather/TorqueWeatherFx.h"

#include "Coop/TorqueGameState.h"
#include "Coop/TorqueSettings.h"
#include "TorqueBikePawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace TorqueWeatherFxImpl
{
constexpr int32 DataFloats = 6;          // per sprite (M_SprayMist): colour (3), alpha, softness, seed
constexpr double BoxHalf = 1300.0;       // the rain's box round the camera: 26 m across
constexpr double BoxBelow = 400.0, BoxAbove = 1000.0;
constexpr int32 MaxBits = 900;           // splashes and spray in the air at once
const FLinearColor Rain(0.52f, 0.56f, 0.62f);
const FLinearColor Spray(0.80f, 0.82f, 0.86f);

// How many streaks for Settings > Graphics > Effects (or the quality preset's).
int32 DropPool()
{
	const FTorqueSettings& S = FTorqueSettings::Get();
	const int32 Q = FMath::Clamp(S.Effects >= 0 ? S.Effects : S.Quality, 0, 4);
	static const int32 Pool[5] = {600, 1200, 2000, 3000, 4000};
	return Pool[Q];
}
} // namespace TorqueWeatherFxImpl

namespace TWX = TorqueWeatherFxImpl;

ATorqueWeatherFx::ATorqueWeatherFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera has moved
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetReplicates(false);
}

void ATorqueWeatherFx::BeginPlay()
{
	Super::BeginPlay();
	Make();
}

void ATorqueWeatherFx::Make()
{
	bMade = true;
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	// The rain's own (lit per pixel, crisper: Tools/ue_build_spray.py), or the spray game's mist before it's built.
	UMaterialInterface* Mist = FPackageName::DoesPackageExist(TEXT("/Game/Shed/Materials/M_RainSprite"))
		? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_RainSprite.M_RainSprite")) : nullptr;
	if (!Mist) Mist = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_SprayMist.M_SprayMist"));
	UE_LOG(LogTemp, Log, TEXT("Torque weather: rain drawn with %s"), Mist ? *Mist->GetName() : TEXT("nothing"));
	if (!Plane || !Mist)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque weather: no %s: no rain or spray drawn"), Plane ? TEXT("M_SprayMist (run Tools/ue_build_shed.py)") : TEXT("plane mesh"));
		return;
	}
	auto MakeIsm = [this, Plane, Mist](const TCHAR* Name, int32 Count) {
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, Mist);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetNumCustomDataFloats(TWX::DataFloats);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetWorldTransform(FTransform::Identity);
		// (Never culled as a whole: its instances move with the camera.)
		C->SetBoundsScale(1000.0f);
		const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -100000.0), FVector(1e-4));
		TArray<FTransform> All;
		All.Init(Hidden, Count);
		C->AddInstances(All, false, true);
		C->SetVisibility(false);
		return C;
	};
	// The rain: each streak's look is fixed (a little fainter or brighter by its seed); only where it is changes.
	const int32 Pool = TWX::DropPool();
	Streaks = MakeIsm(TEXT("RainStreaks"), Pool);
	Drops.SetNum(Pool);
	TArray<float> Data;
	Data.SetNumZeroed(TWX::DataFloats);
	for (int32 I = 0; I < Pool; ++I)
	{
		FDrop& D = Drops[I];
		D.Seed = FMath::FRand();
		D.Fall = FMath::FRandRange(750.0f, 950.0f);
		D.Pos = FVector(FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf), FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf),
			FMath::FRandRange(-TWX::BoxBelow, TWX::BoxAbove));   // (round the camera once it's known)
		Data[0] = TWX::Rain.R;
		Data[1] = TWX::Rain.G;
		Data[2] = TWX::Rain.B;
		Data[3] = FMath::Lerp(0.30f, 0.55f, D.Seed);
		Data[4] = 0.05f;
		Data[5] = D.Seed;
		Streaks->SetCustomData(I, Data, false);
	}
	Streaks->MarkRenderStateDirty();
	PuffPool = TWX::MaxBits;
	Puffs = MakeIsm(TEXT("RainPuffs"), PuffPool);
}

bool ATorqueWeatherFx::GroundBelow(const FVector& At, FVector& Hit) const
{
	FHitResult H;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueRainSplash), false);
	if (!GetWorld()->LineTraceSingleByChannel(H, At, At - FVector(0.0, 0.0, 3000.0), ECC_Visibility, Params)) return false;
	Hit = H.ImpactPoint;
	return true;
}

void ATorqueWeatherFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const ATorqueGameState* GS = World ? World->GetGameState<ATorqueGameState>() : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!GS || !PC || !PC->PlayerCameraManager || !bMade || !Streaks || !Puffs) return;
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// The camera, and how fast it's going (so the rain streaks past as you ride).
	Eye = PC->PlayerCameraManager->GetCameraLocation();
	if (bHaveEye && Dt > 0.0f)
	{
		const FVector V = (Eye - LastEye) / Dt;
		EyeVel = V.SizeSquared() > FMath::Square(8000.0) ? EyeVel : FMath::Lerp(EyeVel, V, FMath::Min(Dt * 10.0f, 1.0f));   // (not across a teleport)
	}
	if (!bHaveEye)
	{
		for (FDrop& D : Drops) D.Pos += Eye;
		bHaveEye = true;
	}
	LastEye = Eye;

	// Under a roof (the shed, a bridge, a barn door): no rain where you are. Looked for a few times a second.
	if ((ShelterCheckIn -= Dt) <= 0.0)
	{
		ShelterCheckIn = 0.3;
		FHitResult H;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueRainShelter), false, PC->GetPawn());
		OpenTarget = World->LineTraceSingleByChannel(H, Eye + FVector(0.0, 0.0, 30.0), Eye + FVector(0.0, 0.0, 3000.0), ECC_Visibility, Params) ? 0.0f : 1.0f;
	}
	Open = FMath::FInterpConstantTo(Open, OpenTarget, Dt, 1.5f);

	const TorqueWeather::State& W = GS->GetWeatherState();
	const double Show = TorqueWeather::RainShow(W) * Open;
	// The wind drives the rain: a share of it, in cm/s, the way it blows (x east, y south).
	const double To = FMath::DegreesToRadians(W.WindFrom + 180.0);
	const FVector Wind(FMath::Sin(To) * W.Wind * 100.0 * 0.6, -FMath::Cos(To) * W.Wind * 100.0 * 0.6, 0.0);
	TickRain(Dt, Show, Wind);
	TickSplashes(Dt, Show);
	TickSpray(Dt, FMath::Clamp(W.RoadWet, 0.0, 1.0));
	SimulateBits(Dt);
	DrawBits();
}

void ATorqueWeatherFx::TickRain(float Dt, double Show, const FVector& Wind)
{
	const int32 Want = FMath::Clamp(FMath::RoundToInt(Drops.Num() * Show), 0, Drops.Num());
	if (Want == 0)
	{
		if (!bStreaksHidden)
		{
			Streaks->SetVisibility(false);
			bStreaksHidden = true;
		}
		Shown = 0;
		return;
	}
	if (bStreaksHidden)
	{
		Streaks->SetVisibility(true);
		bStreaksHidden = false;
	}
	const double H = TWX::BoxBelow + TWX::BoxAbove;
	TArray<FTransform> Xfs;
	Xfs.SetNumUninitialized(FMath::Max(Want, Shown));
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -100000.0), FVector(1e-4));
	for (int32 I = 0; I < Want; ++I)
	{
		FDrop& D = Drops[I];
		const FVector Vel(Wind.X, Wind.Y, -D.Fall);
		D.Pos += Vel * Dt;
		// Round the box: out one side, in the other (and a new spot each time it comes in from the top).
		FVector Rel = D.Pos - Eye;
		if (Rel.X > TWX::BoxHalf) D.Pos.X -= 2.0 * TWX::BoxHalf; else if (Rel.X < -TWX::BoxHalf) D.Pos.X += 2.0 * TWX::BoxHalf;
		if (Rel.Y > TWX::BoxHalf) D.Pos.Y -= 2.0 * TWX::BoxHalf; else if (Rel.Y < -TWX::BoxHalf) D.Pos.Y += 2.0 * TWX::BoxHalf;
		if (Rel.Z < -TWX::BoxBelow)
		{
			D.Pos.Z += H;
			D.Pos.X = Eye.X + FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf);
			D.Pos.Y = Eye.Y + FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf);
		}
		else if (Rel.Z > TWX::BoxAbove) D.Pos.Z -= H;
		// Far outside (a teleport): back in the box.
		Rel = D.Pos - Eye;
		if (FMath::Abs(Rel.X) > 2.0 * TWX::BoxHalf || FMath::Abs(Rel.Y) > 2.0 * TWX::BoxHalf || FMath::Abs(Rel.Z) > H)
		{
			D.Pos = Eye + FVector(FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf), FMath::FRandRange(-TWX::BoxHalf, TWX::BoxHalf),
				FMath::FRandRange(-TWX::BoxBelow, TWX::BoxAbove));
			Rel = D.Pos - Eye;
		}
		// A streak: as long as the drop moves (against the camera) in about a 40th of a second, facing the
		// camera, about a pixel wide wherever it is. Too near the eye it's left out (no bars across the screen).
		const double Dist = Rel.Size();
		if (Dist < 70.0)
		{
			Xfs[I] = Hidden;
			continue;
		}
		const FVector ToEye = -Rel / Dist;
		const FVector Rv = Vel - EyeVel;
		FVector Along = Rv - ToEye * FVector::DotProduct(Rv, ToEye);
		const double Moving = Along.Size();
		Along = Moving > 1.0 ? Along / Moving : FVector::UpVector;
		const double Len = FMath::Clamp(Moving * 0.04, 25.0, 180.0);
		const double Width = 0.5 + Dist * 0.0022;   // (about two pixels at 1080p)
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Along);
		Xfs[I] = FTransform(M.ToQuat(), D.Pos, FVector(Len / 100.0, Width / 100.0, 1.0));
	}
	for (int32 I = Want; I < Shown; ++I) Xfs[I] = Hidden;   // (those no longer falling)
	Streaks->BatchUpdateInstancesTransforms(0, Xfs, true, true, true);
	Shown = Want;
}

void ATorqueWeatherFx::TickSplashes(float Dt, double Show)
{
	if (Show <= 0.0) return;
	// Little bursts on the ground round you, more the harder it rains, most of them near.
	SplashDebt += Dt * Show * 420.0;
	while (SplashDebt >= 1.0 && Bits.Num() < PuffPool)
	{
		SplashDebt -= 1.0;
		const double R = 700.0 * FMath::Sqrt(FMath::FRand()) + 60.0;
		const double A = FMath::FRand() * UE_DOUBLE_TWO_PI;
		FVector Ground;
		if (!GroundBelow(Eye + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 300.0), Ground)) continue;
		FBit B;
		B.Pos = Ground + FVector(0.0, 0.0, 1.5);
		B.Vel = FVector(FMath::FRandRange(-30.0, 30.0), FMath::FRandRange(-30.0, 30.0), FMath::FRandRange(40.0, 110.0));
		B.Life = FMath::FRandRange(0.16f, 0.3f);
		B.Size0 = FMath::FRandRange(3.0f, 5.0f);
		B.Size1 = FMath::FRandRange(10.0f, 18.0f);
		B.Alpha = FMath::FRandRange(0.4f, 0.65f);
		B.Seed = FMath::FRand();
		B.Gravity = 980.0f;
		B.Drag = 0.0f;
		Bits.Add(B);
	}
	SplashDebt = FMath::Min(SplashDebt, 4.0);
}

void ATorqueWeatherFx::TickSpray(float Dt, double Wet)
{
	UWorld* World = GetWorld();
	for (TActorIterator<ATorqueBikePawn> It(World); It; ++It)
	{
		ATorqueBikePawn* Bike = *It;
		if (!Bike || Bike->IsOnFoot() || !Bike->RearWheel || !Bike->FrontWheel) continue;
		const FVector Rear = Bike->RearWheel->GetComponentLocation();
		const FVector Front = Bike->FrontWheel->GetComponentLocation();
		const TWeakObjectPtr<AActor> Key(Bike);
		FVector* Was = LastWheels.Find(Key);
		const FVector Vel = Was && Dt > 0.0f ? (Rear - *Was) / Dt : FVector::ZeroVector;
		LastWheels.Add(Key, Rear);
		const double Speed = Vel.Size2D();
		// Off a wet road, above a jogging pace, the tyres throw up a mist behind them that grows with speed.
		if (Wet < 0.2 || Speed < 500.0 || Speed > 10000.0) continue;
		double& Debt = SprayDebt.FindOrAdd(Key);
		const double Pace = FMath::Clamp(Speed / 2500.0, 0.0, 1.6);
		Debt += Dt * (Wet - 0.1) * Pace * 60.0;
		const FVector Fwd = FVector(Vel.X, Vel.Y, 0.0).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
		while (Debt >= 1.0 && Bits.Num() < PuffPool)
		{
			Debt -= 1.0;
			const bool bFront = FMath::FRand() < 0.3f;
			const UStaticMeshComponent* Wheel = bFront ? Bike->FrontWheel : Bike->RearWheel;
			const double Radius = FMath::Max(Wheel->Bounds.BoxExtent.Z, 20.0);
			const FVector Axle = bFront ? Front : Rear;
			FBit B;
			B.bSpray = true;
			B.Pos = Axle - FVector(0.0, 0.0, Radius * 0.85) - Fwd * Radius * 0.3 + Side * FMath::FRandRange(-8.0, 8.0);
			// The water leaves the tyre slower than the bike goes (so it hangs behind), rising and fanning out;
			// the front wheel's goes more to the sides.
			B.Vel = Vel * FMath::FRandRange(0.25, 0.45) + FVector(0.0, 0.0, Speed * FMath::FRandRange(0.06, 0.13) + FMath::FRandRange(0.0, 120.0))
				+ Side * Speed * FMath::FRandRange(-1.0, 1.0) * (bFront ? 0.16 : 0.07);
			B.Life = FMath::FRandRange(0.5f, 0.95f);
			B.Size0 = FMath::FRandRange(12.0f, 20.0f);
			B.Size1 = FMath::FRandRange(55.0f, 95.0f) * (float)FMath::Lerp(0.7, 1.2, Pace / 1.6);
			B.Alpha = FMath::FRandRange(0.10f, 0.18f) * (float)FMath::Min(Wet * 1.3, 1.0);
			B.Seed = FMath::FRand();
			B.Gravity = 380.0f;
			B.Drag = 2.2f;
			Bits.Add(B);
		}
		Debt = FMath::Min(Debt, 6.0);
	}
	// Forget bikes that have gone.
	for (auto It = LastWheels.CreateIterator(); It; ++It)
		if (!It.Key().IsValid())
		{
			SprayDebt.Remove(It.Key());
			It.RemoveCurrent();
		}
}

void ATorqueWeatherFx::SimulateBits(float Dt)
{
	for (int32 I = Bits.Num() - 1; I >= 0; --I)
	{
		FBit& B = Bits[I];
		B.Age += Dt;
		if (B.Age >= B.Life)
		{
			Bits.RemoveAtSwap(I);
			continue;
		}
		B.Vel *= FMath::Max(1.0f - B.Drag * Dt, 0.0f);
		B.Vel.Z -= B.Gravity * Dt;
		B.Pos += B.Vel * Dt;
	}
}

void ATorqueWeatherFx::DrawBits()
{
	if (Bits.Num() == 0)
	{
		if (Puffs->IsVisible()) Puffs->SetVisibility(false);
		return;
	}
	if (!Puffs->IsVisible()) Puffs->SetVisibility(true);
	// Back to front (they're see-through).
	TArray<int32> Order;
	TArray<double> Dist2;
	Order.SetNumUninitialized(Bits.Num());
	Dist2.SetNumUninitialized(Bits.Num());
	for (int32 I = 0; I < Bits.Num(); ++I)
	{
		Order[I] = I;
		Dist2[I] = FVector::DistSquared(Bits[I].Pos, Eye);
	}
	Order.Sort([&Dist2](int32 A, int32 B) { return Dist2[A] > Dist2[B]; });
	TArray<FTransform> Xfs;
	Xfs.SetNumUninitialized(PuffPool);
	TArray<float> Data;
	Data.SetNumZeroed(TWX::DataFloats);
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -100000.0), FVector(1e-4));
	for (int32 Slot = 0; Slot < PuffPool; ++Slot)
	{
		if (Slot >= Order.Num())
		{
			Xfs[Slot] = Hidden;
			continue;
		}
		const FBit& B = Bits[Order[Slot]];
		const float F = B.Age / FMath::Max(B.Life, 0.001f);
		const FVector ToEye = (Eye - B.Pos).GetSafeNormal();
		FVector U, V;
		ToEye.FindBestAxisVectors(U, V);
		const float Turn = B.Seed * 6.283f;
		const FVector Along = U * FMath::Cos(Turn) + V * FMath::Sin(Turn);
		const float Size = FMath::Lerp(B.Size0, B.Size1, B.bSpray ? FMath::Sqrt(F) : F);
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Along);
		Xfs[Slot] = FTransform(M.ToQuat(), B.Pos, FVector(Size / 100.0f, Size / 100.0f, 1.0f));
		const FLinearColor& C = B.bSpray ? TWX::Spray : TWX::Rain;
		Data[0] = C.R;
		Data[1] = C.G;
		Data[2] = C.B;
		Data[3] = FMath::Clamp(B.Alpha * FMath::Sin(UE_PI * FMath::Min(1.0f, F * 1.1f + 0.05f)), 0.0f, 1.0f);
		Data[4] = B.bSpray ? 1.0f : 0.35f;
		Data[5] = B.Seed;
		Puffs->SetCustomData(Slot, Data, false);
	}
	Puffs->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Puffs->MarkRenderStateDirty();
}
