#include "Shed/TorqueSprayFx.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"

namespace TorqueSprayFxImpl
{
constexpr int32 MaxBits = 2200;      // the most sprites in the air at once
constexpr int32 DataFloats = 6;      // per sprite: colour (3), alpha, softness (0 a crisp droplet, 1 a wisp of mist), seed

// Material from the shed's level script (Tools/ue_build_spray.py); without it, no spray drawn.
UMaterialInterface* Load(const TCHAR* Path)
{
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
	if (M) M->AddToRoot();
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque spray: no %s (run Tools/ue_build_shed.py): the spray isn't drawn"), Path);
	}
	return M;
}

// A point in the round fan at Centre across Axis, Radius wide (more of it to the middle, as a
// spray is: a gun's fan evens it out more).
FVector InDisc(const FVector& Centre, const FVector& Axis, double Radius, bool bEven)
{
	FVector U, V;
	Axis.FindBestAxisVectors(U, V);
	const double A = FMath::FRandRange(0.0, UE_DOUBLE_TWO_PI);
	const double R = Radius * (bEven ? FMath::Sqrt(FMath::FRand()) : FMath::Pow((double)FMath::FRand(), 0.8));
	return Centre + (U * FMath::Cos(A) + V * FMath::Sin(A)) * R;
}

FLinearColor Mistier(const FLinearColor& C, float Amount)
{
	// Atomised paint looks lighter than the wet film (the light scatters off the droplets).
	FLinearColor Out = C * (1.0f - Amount) + FLinearColor(0.9f, 0.9f, 0.9f) * Amount;
	Out.A = 1.0f;
	return Out;
}
} // namespace TorqueSprayFxImpl

namespace TSF = TorqueSprayFxImpl;

UTorqueSprayFx::UTorqueSprayFx()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void UTorqueSprayFx::Make()
{
	bMade = true;
	if (!Plane) Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	AActor* Owner = GetOwner();
	if (!Owner || !Plane) return;
	static UMaterialInterface* Mist = TSF::Load(TEXT("/Game/Shed/Materials/M_SprayMist.M_SprayMist"));
	static UMaterialInterface* RingMat = TSF::Load(TEXT("/Game/Shed/Materials/M_SprayRing.M_SprayRing"));
	if (Mist)
	{
		Sprites = NewObject<UInstancedStaticMeshComponent>(Owner);
		Sprites->SetStaticMesh(Plane);
		Sprites->SetMaterial(0, Mist);
		Sprites->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprites->SetGenerateOverlapEvents(false);
		Sprites->SetCastShadow(false);
		Sprites->SetCanEverAffectNavigation(false);
		Sprites->SetUsingAbsoluteLocation(true);
		Sprites->SetUsingAbsoluteRotation(true);
		Sprites->SetUsingAbsoluteScale(true);
		Sprites->SetNumCustomDataFloats(TSF::DataFloats);
		Sprites->SetupAttachment(this);
		Sprites->RegisterComponent();
		Sprites->SetWorldTransform(FTransform::Identity);
		// A fixed pool, the unused ones folded away to nothing.
		Pool = TSF::MaxBits;
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

void UTorqueSprayFx::Update(float Dt, const FTorqueSprayShot& Shot, const FVector& Eye)
{
	if (!bMade) Make();
	Clock += Dt;
	if (Shot.bFiring) Emit(Dt, Shot);
	else for (double& D : Debt) D = 0.0;
	Simulate(Dt);
	Draw(Eye);
	DrawRing(Shot);
}

void UTorqueSprayFx::Emit(float Dt, const FTorqueSprayShot& Shot)
{
	const FVector Dir = Shot.Dir.GetSafeNormal();
	const FVector Aim = Shot.bHit ? Shot.At : Shot.Nozzle + Dir * 180.0;
	const double Dist = FMath::Max(FVector::Dist(Shot.Nozzle, Aim), 5.0);
	const bool bOut = !Shot.bHit || Shot.Zone == TorqueSpray::EZone::Out;
	const FLinearColor Paint = Shot.Colour;
	const float AlphaK = Shot.bClear ? 0.45f : 1.0f;
	// How many a second: the gun puts out more, finer, than a can.
	const double Rates[4] = {Shot.bGun ? 1100.0 : 800.0, Shot.bGun ? 70.0 : 45.0, Shot.bGun ? 150.0 : 110.0, 40.0};
	FVector Up, Side;
	Dir.FindBestAxisVectors(Up, Side);
	for (int32 Kind = 0; Kind < 4; ++Kind)
	{
		Debt[Kind] += Rates[Kind] * Dt;
		while (Debt[Kind] >= 1.0)
		{
			Debt[Kind] -= 1.0;
			if (Bits.Num() >= TSF::MaxBits) break;
			FBit B;
			B.Kind = (uint8)Kind;
			B.Seed = FMath::FRand();
			B.Spin = FMath::FRandRange(-2.0f, 2.0f);
			switch (Kind)
			{
			case 0:
			{
				// The atomised jet: from the nozzle to a point in the fan, slowing as it goes. Held too
				// far off, more of it gives out on the way and hangs in the air as overspray.
				const FVector To = TSF::InDisc(Aim, Dir, Shot.Radius * (bOut ? 1.4 : 1.0), Shot.bGun);
				const FVector Start = Shot.Nozzle + Side * FMath::FRandRange(-0.3, 0.3) + Up * FMath::FRandRange(-0.3, 0.3);
				const double T = FMath::FRandRange(0.07, 0.12) * FMath::Clamp(Dist / 60.0, 0.6, 2.2);
				B.Pos = Start;
				B.Vel = (To - Start) / T;
				B.Life = (float)T;
				B.bLands = !bOut && FMath::FRand() > Shot.Dry * 0.7;
				if (!B.bLands) B.Life *= FMath::FRandRange(0.6f, 1.0f);
				B.Normal = Shot.Normal;
				B.Size0 = FMath::FRandRange(0.25f, 0.6f);
				B.Size1 = FMath::FRandRange(1.2f, 2.6f) * (Shot.bGun ? 1.2f : 1.0f);
				B.Alpha = FMath::FRandRange(0.05f, 0.11f) * AlphaK;
				B.Stretch = FMath::FRandRange(3.0f, 6.0f);
				B.Soft = 0.25f;
				B.Colour = TSF::Mistier(Paint, 0.12f);
				break;
			}
			case 1:
			{
				// Overspray: a soft cloud billowing off where it lands (or where it gives out), the fan's
				// edge most, drifting away and down on the air.
				const FVector Centre = TSF::InDisc(Aim, Dir, Shot.Radius * 1.3, true);
				const FVector N = Shot.bHit ? Shot.Normal : -Dir;
				B.Pos = Centre + N * FMath::FRandRange(1.0, 5.0);
				FVector Tangent = FVector::CrossProduct(N, FMath::VRand()).GetSafeNormal();
				B.Vel = N * FMath::FRandRange(15.0, 45.0) + Tangent * FMath::FRandRange(10.0, 40.0) + (bOut ? Dir * 40.0 : FVector::ZeroVector);
				B.Life = FMath::FRandRange(1.4f, 3.2f);
				B.Size0 = FMath::FRandRange(4.0f, 9.0f);
				B.Size1 = FMath::FRandRange(26.0f, 48.0f);
				B.Alpha = FMath::FRandRange(0.035f, 0.07f) * AlphaK * (1.0f + (float)Shot.Dry);
				B.Soft = 1.0f;
				B.Colour = TSF::Mistier(Paint, 0.3f);
				break;
			}
			case 2:
			{
				// Fine droplets bouncing back off the wet surface.
				if (bOut) continue;
				const FVector N = Shot.Normal;
				B.Pos = TSF::InDisc(Aim, N, Shot.Radius * 0.8, true) + N * 0.5;
				const FVector Spread = FVector::CrossProduct(N, FMath::VRand()).GetSafeNormal();
				B.Vel = N * FMath::FRandRange(40.0, 150.0) + Spread * FMath::FRandRange(20.0, 90.0);
				B.Life = FMath::FRandRange(0.25f, 0.6f);
				B.Size0 = FMath::FRandRange(0.18f, 0.35f);
				B.Size1 = B.Size0 * 0.8f;
				B.Alpha = FMath::FRandRange(0.35f, 0.6f) * AlphaK;
				B.Stretch = 1.6f;
				B.Soft = 0.0f;
				B.Colour = Paint;
				break;
			}
			default:
			{
				// A little haze right at the nozzle, where the air breaks the paint up.
				B.Pos = Shot.Nozzle + Dir * FMath::FRandRange(0.5, 3.0);
				B.Vel = Dir * FMath::FRandRange(30.0, 80.0) + FMath::VRand() * 12.0;
				B.Life = FMath::FRandRange(0.2f, 0.45f);
				B.Size0 = 0.8f;
				B.Size1 = FMath::FRandRange(5.0f, 9.0f);
				B.Alpha = 0.08f * AlphaK;
				B.Soft = 1.0f;
				B.Colour = TSF::Mistier(Paint, 0.35f);
				break;
			}
			}
			Bits.Add(B);
		}
	}
}

void UTorqueSprayFx::Simulate(float Dt)
{
	for (int32 I = Bits.Num() - 1; I >= 0; --I)
	{
		FBit& B = Bits[I];
		B.Age += Dt;
		if (B.Age >= B.Life)
		{
			if (B.Kind == 0 && !B.bLands && Bits.Num() < TSF::MaxBits && FMath::FRand() < 0.06f)
			{
				// Given out short of the part: it hangs there as overspray.
				FBit M = B;
				M.Kind = 1;
				M.Age = 0.0f;
				M.Life = FMath::FRandRange(1.0f, 2.4f);
				M.Vel = B.Vel * 0.05;
				M.Size0 = 3.0f;
				M.Size1 = FMath::FRandRange(18.0f, 34.0f);
				M.Alpha = 0.04f;
				M.Soft = 1.0f;
				M.Stretch = 1.0f;
				Bits.Add(M);
			}
			Bits.RemoveAtSwap(I);
			continue;
		}
		switch (B.Kind)
		{
		case 0:
			B.Vel *= FMath::Max(0.0, 1.0 - (B.bLands ? 0.6 : 3.5) * Dt);   // landing: hardly slowed; giving out: slows
			break;
		case 1:
		{
			// The booth's air pulls the mist down to the floor filters; it curls as it goes.
			const double T = Clock * 0.7 + B.Seed * 10.0;
			const FVector Curl(FMath::Sin(T * 1.3 + B.Pos.Z * 0.03), FMath::Cos(T * 1.1 + B.Pos.X * 0.03), 0.0);
			B.Vel += (FVector(0.0, 0.0, -18.0) + Curl * 14.0 - B.Vel) * FMath::Min(1.0, 1.4 * Dt);
			break;
		}
		case 2:
			B.Vel += FVector(0.0, 0.0, -300.0) * Dt;
			B.Vel *= FMath::Max(0.0, 1.0 - 1.5 * Dt);
			break;
		default:
			B.Vel *= FMath::Max(0.0, 1.0 - 4.0 * Dt);
			break;
		}
		B.Pos += B.Vel * Dt;
	}
}

void UTorqueSprayFx::Draw(const FVector& Eye)
{
	if (!Sprites) return;
	if (Bits.Num() == 0)
	{
		if (Sprites->IsVisible())
		{
			Sprites->SetVisibility(false);
		}
		return;
	}
	if (!Sprites->IsVisible()) Sprites->SetVisibility(true);
	// Back to front, so the soft sprites lay over each other right.
	TArray<int32> Order;
	Order.SetNumUninitialized(Bits.Num());
	TArray<double> Far;
	Far.SetNumUninitialized(Bits.Num());
	for (int32 I = 0; I < Bits.Num(); ++I)
	{
		Order[I] = I;
		Far[I] = FVector::DistSquared(Bits[I].Pos, Eye);
	}
	Order.Sort([&Far](int32 A, int32 B) { return Far[A] > Far[B]; });
	Xfs.SetNum(Pool);
	Data.SetNumZeroed(TSF::DataFloats);
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
		float Size = FMath::Lerp(B.Size0, B.Size1, B.Kind == 1 ? FMath::Sqrt(F) : F);
		// Fades: the jet stays thick to the surface; the mist swells in and thins away.
		float A = B.Alpha;
		if (B.Kind == 1) A *= FMath::Sin(UE_PI * FMath::Min(1.0f, F * 1.15f + 0.04f));
		else if (B.Kind == 3) A *= 1.0f - F;
		else A *= FMath::Min(1.0f, (1.0f - F) * 4.0f + 0.2f);
		// Lined up with its flight (a streak), facing the eye.
		FVector Along = B.Vel - ToEye * FVector::DotProduct(B.Vel, ToEye);
		float Stretch = 1.0f;
		if (B.Stretch > 1.0f && Along.SizeSquared() > 1.0)
		{
			Stretch = FMath::Lerp(1.0f, B.Stretch, FMath::Clamp((float)Along.Size() / 400.0f, 0.0f, 1.0f));
			Along.Normalize();
		}
		else
		{
			// A wisp turns slowly.
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

void UTorqueSprayFx::DrawRing(const FTorqueSprayShot& Shot)
{
	if (!Ring || !RingMaterial) return;
	const bool bShow = (Shot.bFiring || Shot.bAiming) && Shot.bHit;
	if (!bShow)
	{
		if (bRingShown) Ring->SetVisibility(false);
		bRingShown = false;
		return;
	}
	if (!bRingShown) Ring->SetVisibility(true);
	bRingShown = true;
	FLinearColor C(0.7f, 0.7f, 0.7f);
	switch (Shot.Zone)
	{
	case TorqueSpray::EZone::Good: C = FLinearColor(0.35f, 1.0f, 0.5f); break;
	case TorqueSpray::EZone::Close: C = FLinearColor(1.0f, 0.35f, 0.2f); break;
	case TorqueSpray::EZone::Far: C = FLinearColor(0.35f, 0.65f, 1.0f); break;
	default: break;
	}
	RingMaterial->SetVectorParameterValue(TEXT("Color"), C);
	RingMaterial->SetScalarParameterValue(TEXT("Opacity"), Shot.bFiring ? 0.75f : 0.55f);
	const FVector N = Shot.Normal.GetSafeNormal();
	const FQuat Q = FRotationMatrix::MakeFromZ(N).ToQuat();
	const double D = Shot.Radius * 2.0 / 100.0;   // the plane is a metre across
	Ring->SetWorldTransform(FTransform(Q, Shot.At + N * 0.6, FVector(D, D, 1.0)));
}
