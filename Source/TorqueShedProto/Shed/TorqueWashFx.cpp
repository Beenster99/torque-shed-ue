#include "Shed/TorqueWashFx.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace TorqueWashFxImpl
{
constexpr int32 MaxBits = 2600;      // the most sprites in the air at once
constexpr int32 DataFloats = 6;      // per sprite: colour (3), alpha, softness, seed
constexpr double Speed = 2400.0;     // cm/s out of the nozzle (a real one's faster still; this reads)
constexpr double Reach = 320.0;      // how far the water flies when it hits nothing
const FLinearColor Water(0.86f, 0.93f, 1.0f);
const FLinearColor Mist(0.92f, 0.95f, 0.98f);
const FLinearColor Muddy(0.42f, 0.34f, 0.25f);

UMaterialInterface* Load(const TCHAR* Path)
{
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
	if (M) M->AddToRoot();
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque wash: no %s (run Tools/ue_build_shed.py): the jet isn't drawn"), Path);
	}
	return M;
}

// A point in the jet's flat fan at Centre (a 15 degree washer tip: a stripe, Across its long way).
FVector InFan(const FVector& Centre, const FVector& Across, const FVector& Thin, double Radius)
{
	const double A = FMath::FRandRange(-1.0, 1.0);
	return Centre + Across * (A * Radius) + Thin * (FMath::FRandRange(-1.0, 1.0) * Radius * 0.12);
}

FVector Tangent(const FVector& N)
{
	return FVector::CrossProduct(N, FMath::VRand()).GetSafeNormal();
}
} // namespace TorqueWashFxImpl

namespace TWF = TorqueWashFxImpl;

UTorqueWashFx::UTorqueWashFx()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void UTorqueWashFx::Make()
{
	bMade = true;
	if (!Plane) Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	AActor* Owner = GetOwner();
	if (!Owner || !Plane) return;
	static UMaterialInterface* MistMat = TWF::Load(TEXT("/Game/Shed/Materials/M_SprayMist.M_SprayMist"));
	static UMaterialInterface* RingMat = TWF::Load(TEXT("/Game/Shed/Materials/M_SprayRing.M_SprayRing"));
	if (MistMat)
	{
		Sprites = NewObject<UInstancedStaticMeshComponent>(Owner);
		Sprites->SetStaticMesh(Plane);
		Sprites->SetMaterial(0, MistMat);
		Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprites->SetGenerateOverlapEvents(false);
		Sprites->SetCastShadow(false);
		Sprites->SetCanEverAffectNavigation(false);
		Sprites->SetUsingAbsoluteLocation(true);
		Sprites->SetUsingAbsoluteRotation(true);
		Sprites->SetUsingAbsoluteScale(true);
		Sprites->SetNumCustomDataFloats(TWF::DataFloats);
		Sprites->SetupAttachment(this);
		Sprites->RegisterComponent();
		Sprites->SetWorldTransform(FTransform::Identity);
		Pool = TWF::MaxBits;
		const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector(1e-4));
		for (int32 I = 0; I < Pool; ++I) Sprites->AddInstance(Hidden, true);
		Sprites->SetVisibility(false);
	}
	if (RingMat)
	{
		Ring = NewObject<UStaticMeshComponent>(Owner);
		Ring->SetStaticMesh(Plane);
		RingMaterial = UMaterialInstanceDynamic::Create(RingMat, this);
		Ring->SetMaterial(0, RingMaterial);
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetGenerateOverlapEvents(false);
		Ring->SetCastShadow(false);
		Ring->SetCanEverAffectNavigation(false);
		Ring->SetUsingAbsoluteLocation(true);
		Ring->SetUsingAbsoluteRotation(true);
		Ring->SetUsingAbsoluteScale(true);
		Ring->SetupAttachment(this);
		Ring->RegisterComponent();
		Ring->SetVisibility(false);
	}
}

void UTorqueWashFx::Update(float Dt, const FTorqueWashShot& Shot, const FVector& Eye)
{
	if (!bMade) Make();
	Clock += Dt;
	if (Shot.bFiring) Emit(Dt, Shot);
	else for (double& D : Debt) D = 0.0;
	Simulate(Dt);
	Draw(Eye);
	DrawRing(Shot);
}

void UTorqueWashFx::Emit(float Dt, const FTorqueWashShot& Shot)
{
	const FVector Dir = Shot.Dir.GetSafeNormal();
	const bool bLand = Shot.bHit && FVector::Dist(Shot.Nozzle, Shot.At) < TWF::Reach;
	const FVector Aim = bLand ? Shot.At : Shot.Nozzle + Dir * TWF::Reach;
	const double Dist = FMath::Max(FVector::Dist(Shot.Nozzle, Aim), 5.0);
	// The fan lies flat across the way you hold the lance (sideways).
	FVector Across = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	if (Across.IsNearlyZero()) Across = FVector::RightVector;
	const FVector Thin = FVector::CrossProduct(Across, Dir).GetSafeNormal();
	const FVector N = bLand ? Shot.Normal.GetSafeNormal() : -Dir;
	const FLinearColor SplashCol = Shot.bMuddy ? FMath::Lerp(TWF::Water, TWF::Muddy, 0.55f) : TWF::Water;
	// How many a second: the jet, its mist, the splash, the core, the rebound, the drips.
	const double Rates[6] = {1500.0, 160.0, bLand ? 520.0 : 0.0, 420.0, bLand ? 170.0 : 0.0, bLand ? 18.0 : 0.0};
	for (int32 Kind = 0; Kind < 6; ++Kind)
	{
		Debt[Kind] += Rates[Kind] * Dt;
		while (Debt[Kind] >= 1.0)
		{
			Debt[Kind] -= 1.0;
			if (Bits.Num() >= TWF::MaxBits) break;
			FBit B;
			B.Kind = (uint8)Kind;
			B.Seed = FMath::FRand();
			B.Spin = FMath::FRandRange(-2.0f, 2.0f);
			switch (Kind)
			{
			case 0:
			{
				// A streak of fast water, from just out of the nozzle to a point in the fan: thin rods
				// stretched along their flight, as fast water blurs into lines.
				const FVector To = TWF::InFan(Aim, Across, Thin, Shot.Radius * (bLand ? 1.0 : 1.6));
				const FVector From = Shot.Nozzle + Dir * FMath::FRandRange(8.0, 20.0);
				const double T = FVector::Dist(From, To) / (TWF::Speed * FMath::FRandRange(0.9, 1.1));
				B.Pos = From;
				B.Vel = (To - From) / FMath::Max(T, 0.005);
				B.Life = (float)FMath::Max(T, 0.02);
				B.bLands = bLand;
				B.Normal = N;
				B.Size0 = FMath::FRandRange(0.25f, 0.4f);
				B.Size1 = FMath::FRandRange(0.5f, 0.9f);
				B.Alpha = FMath::FRandRange(0.22f, 0.4f);
				B.Stretch = FMath::FRandRange(28.0f, 46.0f);
				B.Soft = 0.0f;
				B.Colour = TWF::Water;
				break;
			}
			case 1:
			{
				// The fine mist hanging all along the jet, thickest where it lands, drifting out round it.
				const double Along = FMath::Pow((double)FMath::FRand(), 0.5);
				const FVector P = Shot.Nozzle + (Aim - Shot.Nozzle) * Along;
				B.Pos = P + TWF::Tangent(Dir) * FMath::FRandRange(0.0, 3.0 + Along * Shot.Radius);
				B.Vel = Dir * FMath::FRandRange(20.0, 120.0) * (1.0 - Along) + FMath::VRand() * FMath::FRandRange(10.0, 45.0)
					+ (bLand ? N * FMath::FRandRange(10.0, 60.0) * Along : FVector::ZeroVector);
				B.Life = FMath::FRandRange(1.2f, 2.8f);
				B.Size0 = FMath::FRandRange(3.0f, 7.0f);
				B.Size1 = FMath::FRandRange(26.0f, 60.0f) * (float)(0.5 + Along);
				B.Alpha = FMath::FRandRange(0.03f, 0.06f);
				B.Soft = 1.0f;
				B.Colour = TWF::Mist;
				break;
			}
			case 2:
			{
				// Droplets thrown back off the surface: out of the footprint, fanning off along it, falling.
				const FVector Reflect = (Dir - 2.0 * FVector::DotProduct(Dir, N) * N).GetSafeNormal();
				B.Pos = TWF::InFan(Aim, Across, Thin, Shot.Radius * 0.8) + N * 0.6;
				B.Vel = Reflect * FMath::FRandRange(80.0, 260.0) + N * FMath::FRandRange(60.0, 280.0) + TWF::Tangent(N) * FMath::FRandRange(60.0, 360.0);
				B.Life = FMath::FRandRange(0.35f, 0.9f);
				B.Size0 = FMath::FRandRange(0.3f, 0.75f);
				B.Size1 = B.Size0 * 0.8f;
				B.Alpha = FMath::FRandRange(0.4f, 0.75f);
				B.Stretch = 4.0f;
				B.Soft = 0.0f;
				B.Colour = SplashCol;
				break;
			}
			case 3:
			{
				// The solid glassy pencil right out of the nozzle, before it breaks up.
				B.Pos = Shot.Nozzle + Dir * FMath::FRandRange(0.0, 10.0);
				B.Vel = Dir * TWF::Speed;
				B.Life = FMath::FRandRange(0.008f, 0.016f);
				B.Size0 = 0.45f;
				B.Size1 = 0.7f;
				B.Alpha = 0.55f;
				B.Stretch = 26.0f;
				B.Soft = 0.0f;
				B.Colour = FLinearColor(0.95f, 0.98f, 1.0f);
				break;
			}
			case 4:
			{
				// The rebound: a sheet of fine spray blasting out along the surface round the footprint.
				B.Pos = TWF::InFan(Aim, Across, Thin, Shot.Radius * 0.6) + N * 1.0;
				const FVector Off = (TWF::Tangent(N) + N * 0.35).GetSafeNormal();
				B.Vel = Off * FMath::FRandRange(150.0, 420.0);
				B.Life = FMath::FRandRange(0.25f, 0.55f);
				B.Size0 = FMath::FRandRange(2.0f, 4.0f);
				B.Size1 = FMath::FRandRange(12.0f, 24.0f);
				B.Alpha = FMath::FRandRange(0.06f, 0.12f);
				B.Stretch = 2.2f;
				B.Soft = 0.7f;
				B.Colour = Shot.bMuddy ? FMath::Lerp(TWF::Mist, TWF::Muddy, 0.3f) : TWF::Mist;
				break;
			}
			default:
			{
				// Water running off and dripping down from under where it's landed.
				B.Pos = TWF::InFan(Aim, Across, Thin, Shot.Radius * 1.2) + FVector(0.0, 0.0, -FMath::FRandRange(2.0, 10.0));
				B.Vel = FVector(0.0, 0.0, -FMath::FRandRange(20.0, 80.0));
				B.Life = FMath::FRandRange(0.5f, 0.9f);
				B.Size0 = FMath::FRandRange(0.45f, 0.8f);
				B.Size1 = B.Size0;
				B.Alpha = 0.5f;
				B.Stretch = 3.0f;
				B.Soft = 0.0f;
				B.Colour = SplashCol;
				break;
			}
			}
			Bits.Add(B);
		}
	}
}

void UTorqueWashFx::Simulate(float Dt)
{
	for (int32 I = Bits.Num() - 1; I >= 0; --I)
	{
		FBit& B = Bits[I];
		B.Age += Dt;
		if (B.Age >= B.Life)
		{
			// A streak that hits nothing breaks up into mist where it gives out.
			if (B.Kind == 0 && !B.bLands && Bits.Num() < TWF::MaxBits && FMath::FRand() < 0.05f)
			{
				FBit M = B;
				M.Kind = 1;
				M.Age = 0.0f;
				M.Life = FMath::FRandRange(1.0f, 2.0f);
				M.Vel = B.Vel * 0.03;
				M.Size0 = 4.0f;
				M.Size1 = FMath::FRandRange(20.0f, 40.0f);
				M.Alpha = 0.04f;
				M.Soft = 1.0f;
				M.Stretch = 1.0f;
				M.Colour = TWF::Mist;
				Bits.Add(M);
			}
			Bits.RemoveAtSwap(I);
			continue;
		}
		switch (B.Kind)
		{
		case 0:
			if (!B.bLands) B.Vel += FVector(0.0, 0.0, -980.0) * Dt;
			break;
		case 1:
		{
			// The mist curls and rises a touch on the air of the bay as it thins.
			const double T = Clock * 0.6 + B.Seed * 10.0;
			const FVector Curl(FMath::Sin(T * 1.3 + B.Pos.Z * 0.02), FMath::Cos(T * 1.1 + B.Pos.X * 0.02), 0.3);
			B.Vel += (Curl * 12.0 - B.Vel) * FMath::Min(1.0, 1.2 * Dt);
			break;
		}
		case 2:
		case 5:
			B.Vel += FVector(0.0, 0.0, -980.0) * Dt;
			break;
		case 4:
			B.Vel *= FMath::Max(0.0, 1.0 - 5.0 * Dt);
			break;
		default:
			break;
		}
		B.Pos += B.Vel * Dt;
	}
}

void UTorqueWashFx::Draw(const FVector& Eye)
{
	if (!Sprites) return;
	if (Bits.Num() == 0)
	{
		if (Sprites->IsVisible()) Sprites->SetVisibility(false);
		return;
	}
	if (!Sprites->IsVisible()) Sprites->SetVisibility(true);
	TArray<int32> Order;
	Order.SetNumUninitialized(Bits.Num());
	TArray<double> Dist2;
	Dist2.SetNumUninitialized(Bits.Num());
	for (int32 I = 0; I < Bits.Num(); ++I)
	{
		Order[I] = I;
		Dist2[I] = FVector::DistSquared(Bits[I].Pos, Eye);
	}
	Order.Sort([&Dist2](int32 A, int32 B) { return Dist2[A] > Dist2[B]; });
	Xfs.SetNum(Pool);
	Data.SetNumZeroed(TWF::DataFloats);
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector(1e-4));
	for (int32 Slot = 0; Slot < Pool; ++Slot)
	{
		if (Slot >= Order.Num())
		{
			Xfs[Slot] = Hidden;
			continue;
		}
		const FBit& B = Bits[Order[Slot]];
		const float F = B.Age / FMath::Max(B.Life, 0.001f);
		const FVector ToEye = (Eye - B.Pos).GetSafeNormal();
		const float Size = FMath::Lerp(B.Size0, B.Size1, B.Kind == 1 ? FMath::Sqrt(F) : F);
		float A = B.Alpha;
		if (B.Kind == 1) A *= FMath::Sin(UE_PI * FMath::Min(1.0f, F * 1.1f + 0.05f));
		else if (B.Kind == 4) A *= 1.0f - F;
		else A *= FMath::Min(1.0f, (1.0f - F) * 4.0f + 0.3f);
		FVector Along = B.Vel - ToEye * FVector::DotProduct(B.Vel, ToEye);
		float Stretch = 1.0f;
		if (B.Stretch > 1.0f && Along.SizeSquared() > 1.0)
		{
			Stretch = FMath::Lerp(1.0f, B.Stretch, FMath::Clamp((float)Along.Size() / 600.0f, 0.0f, 1.0f));
			Along.Normalize();
		}
		else
		{
			FVector U, V;
			ToEye.FindBestAxisVectors(U, V);
			const float Turn = B.Seed * 6.283f + B.Spin * B.Age;
			Along = U * FMath::Cos(Turn) + V * FMath::Sin(Turn);
		}
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Along);
		Xfs[Slot] = FTransform(M.ToQuat(), B.Pos, FVector(Size * Stretch / 100.0f, Size / 100.0f, 1.0f));
		Data[0] = B.Colour.R;
		Data[1] = B.Colour.G;
		Data[2] = B.Colour.B;
		Data[3] = FMath::Clamp(A, 0.0f, 1.0f);
		Data[4] = B.Soft;
		Data[5] = B.Seed;
		Sprites->SetCustomData(Slot, Data, false);
	}
	Sprites->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Sprites->MarkRenderStateDirty();
}

void UTorqueWashFx::DrawRing(const FTorqueWashShot& Shot)
{
	if (!Ring || !RingMaterial) return;
	const bool bShow = Shot.bRing && Shot.bHit;
	if (!bShow)
	{
		if (bRingShown) Ring->SetVisibility(false);
		bRingShown = false;
		return;
	}
	if (!bRingShown) Ring->SetVisibility(true);
	bRingShown = true;
	FLinearColor C(0.6f, 0.6f, 0.6f);
	switch (Shot.Zone)
	{
	case TorqueWash::EZone::Good: C = FLinearColor(0.35f, 1.0f, 0.5f); break;
	case TorqueWash::EZone::Close: C = FLinearColor(1.0f, 0.75f, 0.3f); break;
	case TorqueWash::EZone::Far: C = FLinearColor(0.35f, 0.65f, 1.0f); break;
	default: break;
	}
	RingMaterial->SetVectorParameterValue(TEXT("Color"), C);
	RingMaterial->SetScalarParameterValue(TEXT("Opacity"), Shot.bFiring ? 0.45f : 0.6f);
	const FVector N = Shot.Normal.GetSafeNormal();
	const FQuat Q = FRotationMatrix::MakeFromZ(N).ToQuat();
	const double D = Shot.Radius * 2.0 / 100.0;
	Ring->SetWorldTransform(FTransform(Q, Shot.At + N * 0.6, FVector(D, D, 1.0)));
}
