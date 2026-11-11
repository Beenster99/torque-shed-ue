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
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace TorqueWeatherFxImpl
{
constexpr int32 DataFloats = 6;          // per sprite (M_SprayMist): colour (3), alpha, softness, seed
constexpr double BoxHalf = 1300.0;       // the rain's box round the camera: 26 m across
constexpr double BoxBelow = 400.0, BoxAbove = 1000.0;
constexpr int32 MaxBits = 2400;          // splash droplets, spray droplets and mist in the air at once
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
	// (Built but failed to compile: the mist, not the default material.)
	if (Mist && GetWorld())
		if (UMaterial* Base = Mist->GetMaterial(); Base && Base->IsCompilingOrHadCompileError(GetWorld()->GetFeatureLevel()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque weather: M_RainSprite didn't compile; the rain uses M_SprayMist"));
			Mist = nullptr;
		}
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
		Data[4] = 2.0f;   // (M_RainSprite: a thin tapered streak)
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
		// (A drop is 1 to 3 mm across: its streak a hairline, about a pixel wide at 1080p wherever it is;
		// build 135's were two pixels and read as thick rods, beenster.)
		const double Len = FMath::Clamp(Moving * 0.035, 15.0, 150.0);
		const double Width = 0.25 + Dist * 0.0012;
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
	// Drops landing on the ground round you, more the harder it rains, most of them near. Each one a
	// tiny crown: a few droplets of a millimetre or two jumping a few centimetres and falling back (build
	// 135's were 10 to 18 cm puffs, far too big, beenster).
	SplashDebt += Dt * Show * 420.0;
	while (SplashDebt >= 1.0 && Bits.Num() + 4 <= PuffPool)
	{
		SplashDebt -= 1.0;
		const double R = 700.0 * FMath::Sqrt(FMath::FRand()) + 60.0;
		const double A = FMath::FRand() * UE_DOUBLE_TWO_PI;
		FVector Ground;
		if (!GroundBelow(Eye + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 300.0), Ground)) continue;
		const int32 Count = FMath::RandRange(2, 4);
		for (int32 K = 0; K < Count; ++K)
		{
			FBit B;
			const double Out = FMath::FRand() * UE_DOUBLE_TWO_PI;
			const double Spread = FMath::FRandRange(25.0, 70.0);
			B.Pos = Ground + FVector(0.0, 0.0, 0.5);
			B.Vel = FVector(FMath::Cos(Out) * Spread, FMath::Sin(Out) * Spread, FMath::FRandRange(70.0, 150.0));
			B.Life = FMath::FRandRange(0.12f, 0.24f);
			B.Size0 = FMath::FRandRange(0.25f, 0.45f);
			B.Size1 = B.Size0 * 0.8f;
			B.Alpha = FMath::FRandRange(0.5f, 0.8f);
			B.Seed = FMath::FRand();
			B.Gravity = 980.0f;
			B.Drag = 0.0f;
			B.Soft = 0.1f;
			Bits.Add(B);
		}
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
		// Off a wet road, above a jogging pace, the tyres throw the water up. As in beenster's reference
		// (a rooster tail off a wet track): the tread picks the water up at the contact patch and flings
		// it off as it comes round the back of the tyre, a fan of fine droplets rising steeply behind the
		// bike, with a faint mist through it; the front tyre throws less, low and to the sides.
		if (Wet < 0.2 || Speed < 500.0 || Speed > 10000.0) continue;
		double& Debt = SprayDebt.FindOrAdd(Key);
		const double Pace = FMath::Clamp(Speed / 2500.0, 0.0, 1.6);
		Debt += Dt * (Wet - 0.1) * Pace * 520.0;
		const FVector Fwd = FVector(Vel.X, Vel.Y, 0.0).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Fwd);
		while (Debt >= 1.0 && Bits.Num() < PuffPool)
		{
			Debt -= 1.0;
			const float Pick = FMath::FRand();
			const bool bMist = Pick < 0.07f;
			const bool bFront = !bMist && Pick > 0.85f;
			const UStaticMeshComponent* Wheel = bFront ? Bike->FrontWheel : Bike->RearWheel;
			const double Radius = FMath::Max(Wheel->Bounds.BoxExtent.Z, 20.0);
			const FVector Axle = bFront ? Front : Rear;
			FBit B;
			B.bSpray = true;
			B.Seed = FMath::FRand();
			// Where it leaves the tread: Theta round from the bottom of the tyre towards the back. A point
			// of the tread there moves (on the ground's frame) at the bike's speed times (1 - cos, sin):
			// a little forward and mostly up, so the water rises behind the bike as it rides away from it.
			const double Theta = FMath::DegreesToRadians(bFront ? FMath::FRandRange(5.0, 30.0) : FMath::FRandRange(8.0, 55.0));
			const double Width = FMath::FRandRange(-1.0, 1.0) * (bFront ? 6.0 : 7.0);
			B.Pos = Axle - FVector(0.0, 0.0, Radius * FMath::Cos(Theta)) - Fwd * Radius * FMath::Sin(Theta) + Side * Width;
			// (The water slips on the tread and the air slows it fast: it leaves at a fraction of the tread's
			// speed, so the tail stands a metre or so high at 50 mph, as in the reference.)
			const double Fling = Speed * FMath::FRandRange(0.18, 0.42);
			B.Vel = Fwd * Fling * (1.0 - FMath::Cos(Theta)) + FVector(0.0, 0.0, Fling * FMath::Sin(Theta))
				+ Side * Fling * FMath::FRandRange(-1.0, 1.0) * (bFront ? 0.22 : 0.06);
			if (bMist)
			{
				// The mist: a few soft, faint puffs that hang in the air behind.
				B.Vel *= 0.35;
				B.Life = FMath::FRandRange(0.6f, 1.1f);
				B.Size0 = FMath::FRandRange(6.0f, 12.0f);
				B.Size1 = FMath::FRandRange(35.0f, 60.0f) * (float)FMath::Lerp(0.7, 1.2, Pace / 1.6);
				B.Alpha = FMath::FRandRange(0.04f, 0.08f) * (float)FMath::Min(Wet * 1.3, 1.0);
				B.Gravity = 120.0f;
				B.Drag = 2.5f;
				B.Soft = 1.0f;
			}
			else
			{
				// A droplet: a millimetre or three, falling back under gravity, slowed a little by the air.
				B.Life = FMath::FRandRange(0.35f, 0.75f);
				B.Size0 = FMath::FRandRange(0.3f, 0.7f);
				B.Size1 = B.Size0;
				B.Alpha = FMath::FRandRange(0.45f, 0.8f) * (float)FMath::Min(Wet * 1.3, 1.0);
				B.Gravity = 980.0f;
				B.Drag = 1.5f;
				B.Soft = 0.15f;
			}
			Bits.Add(B);
		}
		Debt = FMath::Min(Debt, 40.0);
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
		float Size = FMath::Lerp(B.Size0, B.Size1, B.Soft > 0.5f ? FMath::Sqrt(F) : F);
		// A droplet smaller than about a pixel is drawn a pixel wide and fainter, so it doesn't flicker
		// in and out (the same light, spread wider).
		float Fade = 1.0f;
		const float Least = (float)FVector::Dist(Eye, B.Pos) * 0.0012f;
		if (Size < Least)
		{
			Fade = FMath::Square(Size / Least);
			Size = Least;
		}
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Along);
		Xfs[Slot] = FTransform(M.ToQuat(), B.Pos, FVector(Size / 100.0f, Size / 100.0f, 1.0f));
		const FLinearColor& C = B.bSpray ? TWX::Spray : TWX::Rain;
		Data[0] = C.R;
		Data[1] = C.G;
		Data[2] = C.B;
		const float Strength = B.Soft > 0.5f ? FMath::Sin(UE_PI * FMath::Min(1.0f, F * 1.1f + 0.05f)) : 1.0f - FMath::Square(F);
		Data[3] = FMath::Clamp(B.Alpha * Fade * Strength, 0.0f, 1.0f);
		Data[4] = B.Soft;
		Data[5] = B.Seed;
		Puffs->SetCustomData(Slot, Data, false);
	}
	Puffs->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Puffs->MarkRenderStateDirty();
}
