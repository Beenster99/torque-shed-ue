#include "Shed/TorqueWashMist.h"

#include "Shed/TorqueWalker.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

namespace TorqueWashMistImpl
{
// The wash bay (ue_build_shed.py wash_bay): its pad's middle and half sizes, and its roof.
const FVector Centre(-930.0, 100.0, 0.0);
constexpr double HalfX = 240.0, HalfY = 340.0, Roof = 300.0;
// wash_mist.gd: seconds of washing for the haze to build right up, and to clear after.
constexpr double FillTime = 25.0;
constexpr double ClearTime = 40.0;
constexpr int32 MaxPuffs = 150;           // haze puffs alive at once at its thickest
constexpr float PuffAlpha = 0.11f;        // how opaque a puff is at its thickest
constexpr float FogDensity = 0.9f;        // the fog volume's radial extinction at its thickest
constexpr double SeeFrom = 2200.0;        // the volumetric fog's only switched on this close to the bay (cm)
constexpr int32 DataFloats = 6;
} // namespace TorqueWashMistImpl

namespace TWM = TorqueWashMistImpl;

ATorqueWashMist::ATorqueWashMist()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

ATorqueWashMist* ATorqueWashMist::Ensure(UWorld* World, UStaticMesh* InPlane)
{
	if (!World) return nullptr;
	if (TActorIterator<ATorqueWashMist> It(World); It) return *It;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.bDeferConstruction = true;
	ATorqueWashMist* M = World->SpawnActor<ATorqueWashMist>(ATorqueWashMist::StaticClass(), FTransform(TWM::Centre), P);
	if (!M) return nullptr;
	M->Plane = InPlane;
	M->FinishSpawning(FTransform(TWM::Centre));
	return M;
}

void ATorqueWashMist::BeginPlay()
{
	Super::BeginPlay();
	Make();
}

void ATorqueWashMist::EndPlay(const EEndPlayReason::Type Reason)
{
	UpdateVolumetric(false);
	Super::EndPlay(Reason);
}

void ATorqueWashMist::Make()
{
	// The fog volume, a sphere over the bay (its size at scale 1 is the engine's, so it's scaled from that).
	Fog = NewObject<ULocalFogVolumeComponent>(this, TEXT("WashFog"));
	Fog->SetupAttachment(GetRootComponent());
	Fog->RegisterComponent();
	FogUnit = Fog->CalcBounds(FTransform::Identity).BoxExtent.X;   // (as the level script measures the hall dust's)
	if (FogUnit < 1.0) FogUnit = 500.0;   // (the engine's unit sphere, if it gives no bounds)
	FogUnit = FMath::Max(FogUnit, 1.0);
	const double Radius = FMath::Max(TWM::HalfX, TWM::HalfY) + 60.0;
	Fog->SetRelativeLocation(FVector(0.0, 0.0, TWM::Roof * 0.5));
	Fog->SetRelativeScale3D(FVector(Radius / FogUnit));
	Fog->SetRadialFogExtinction(0.0f);
	Fog->SetHeightFogExtinction(0.0f);
	Fog->SetFogAlbedo(FLinearColor(0.9f, 0.93f, 0.96f));
	Fog->SetFogPhaseG(0.25f);   // near zero: the light shafts show (Epic's guide for local fog in volumetric fog)
	Fog->SetFogEmissive(FLinearColor(0.0f, 0.0f, 0.0f));
	Fog->SetVisibility(false);
	UE_LOG(LogTemp, Log, TEXT("Torque wash: bay mist fog unit %.0f cm, scaled to %.0f"), FogUnit, Radius);
	// The puffs (the spray game's soft sprites).
	if (!Plane) Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_SprayMist.M_SprayMist"));
	if (Plane && Mat)
	{
		Puffs = NewObject<UInstancedStaticMeshComponent>(this, TEXT("WashPuffs"));
		Puffs->SetStaticMesh(Plane);
		Puffs->SetMaterial(0, Mat);
		Puffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puffs->SetCastShadow(false);
		Puffs->SetCanEverAffectNavigation(false);
		Puffs->SetUsingAbsoluteLocation(true);
		Puffs->SetUsingAbsoluteRotation(true);
		Puffs->SetUsingAbsoluteScale(true);
		Puffs->SetNumCustomDataFloats(TWM::DataFloats);
		Puffs->SetupAttachment(GetRootComponent());
		Puffs->RegisterComponent();
		Puffs->SetWorldTransform(FTransform::Identity);
		const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector(1e-4));
		for (int32 I = 0; I < TWM::MaxPuffs; ++I) Puffs->AddInstance(Hidden, true);
		Puffs->SetVisibility(false);
	}
	if (TActorIterator<AExponentialHeightFog> It(GetWorld()); It) HeightFog = It->GetComponent();
}

bool ATorqueWashMist::AnyoneWashing() const
{
	for (TActorIterator<ATorqueWalker> It(GetWorld()); It; ++It)
		if (It->WashJetting()) return true;
	return false;
}

void ATorqueWashMist::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = DeltaSeconds;
	Clock += Dt;
	const bool bWashing = AnyoneWashing();
	Haze = bWashing ? FMath::Min(1.0, Haze + Dt / TWM::FillTime) : FMath::Max(0.0, Haze - Dt / TWM::ClearTime);
	const double K = Haze * Haze * (3.0 - 2.0 * Haze);
	// A little mist right away while the jet's going (it fills the air round it), thickening slowly.
	const double Shown = FMath::Max(K, bWashing ? 0.12 : 0.0);
	if (Fog)
	{
		const bool bOn = Shown > 0.01;
		if (Fog->IsVisible() != bOn) Fog->SetVisibility(bOn);
		if (bOn)
		{
			Fog->SetRadialFogExtinction(TWM::FogDensity * (float)Shown);
			// Heavier low down, where the spray hangs.
			Fog->SetHeightFogExtinction(0.5f * TWM::FogDensity * (float)Shown);
			Fog->SetHeightFogFalloff(400.0f);
			Fog->SetHeightFogOffset(-TWM::Roof * 0.5f);
		}
	}
	// The volumetric fog: on while there's mist to light and you're near enough to see it.
	bool bNear = false;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager)
			bNear = FVector::Dist2D(PC->PlayerCameraManager->GetCameraLocation(), TWM::Centre) < TWM::SeeFrom;
	UpdateVolumetric(Shown > 0.02 && bNear);
	DrawPuffs(DeltaSeconds);
}

void ATorqueWashMist::UpdateVolumetric(bool bWant)
{
	UExponentialHeightFogComponent* F = HeightFog.Get();
	if (!F) return;
	// Volumetric fog cost the shed ~3x its GPU time when it was on everywhere (round 88), so only
	// while there's mist and you're near it, only when the Settings already have volumetric fog on
	// (r.VolumetricFog), only to 25 m out, and torque.WashVolumetric 0 keeps the mist to the local
	// fog volume alone (its own cheap pass) for measuring.
	static TAutoConsoleVariable<int32> CVarWash(TEXT("torque.WashVolumetric"), 1,
		TEXT("1: the wash bay's mist turns on volumetric fog (near the bay, while misty, if r.VolumetricFog is on). 0: local fog volume only."));
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog"));
	if (bWant && ((CVar && CVar->GetInt() == 0) || CVarWash.GetValueOnGameThread() == 0)) bWant = false;
	if (bWant && !bTurnedOnVolumetric && !F->bEnableVolumetricFog)
	{
		WasFogDistance = F->VolumetricFogDistance;
		F->SetVolumetricFogDistance(2500.0f);
		F->SetVolumetricFog(true);
		bTurnedOnVolumetric = true;
	}
	else if (!bWant && bTurnedOnVolumetric)
	{
		F->SetVolumetricFog(false);
		F->SetVolumetricFogDistance(WasFogDistance);
		bTurnedOnVolumetric = false;
	}
}

void ATorqueWashMist::DrawPuffs(float Dt)
{
	if (!Puffs) return;
	const double K = Haze * Haze * (3.0 - 2.0 * Haze);
	// New puffs while it's thick enough to show, as many as it's thick.
	if (K > 0.02)
	{
		Debt += TWM::MaxPuffs / 8.0 * K * Dt;
		while (Debt >= 1.0 && Live.Num() < FMath::CeilToInt(TWM::MaxPuffs * K))
		{
			Debt -= 1.0;
			FPuff P;
			P.Pos = TWM::Centre + FVector(FMath::FRandRange(-TWM::HalfX + 50.0, TWM::HalfX - 50.0), FMath::FRandRange(-TWM::HalfY + 50.0, TWM::HalfY - 30.0),
				FMath::FRandRange(40.0, TWM::Roof - 50.0));
			P.Vel = FMath::VRand() * FMath::FRandRange(2.0, 12.0);
			P.Life = FMath::FRandRange(6.0f, 11.0f);
			P.Size = FMath::FRandRange(70.0f, 160.0f);
			P.Seed = FMath::FRand();
			Live.Add(P);
		}
	}
	else Debt = 0.0;
	for (int32 I = Live.Num() - 1; I >= 0; --I)
	{
		FPuff& P = Live[I];
		P.Age += Dt;
		if (P.Age >= P.Life) { Live.RemoveAtSwap(I); continue; }
		const double T = Clock * 0.25 + P.Seed * 20.0;
		P.Vel += (FVector(FMath::Sin(T), FMath::Cos(T * 0.8), 0.15) * 10.0 - P.Vel) * FMath::Min(1.0, 0.3 * Dt);
		P.Pos += P.Vel * Dt;
	}
	if (Live.Num() == 0)
	{
		if (Puffs->IsVisible()) Puffs->SetVisibility(false);
		return;
	}
	if (!Puffs->IsVisible()) Puffs->SetVisibility(true);
	FVector Eye = TWM::Centre + FVector(0.0, 1000.0, 160.0);
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Eye = PC->PlayerCameraManager->GetCameraLocation();
	TArray<int32> Order;
	TArray<double> Dist2;
	for (int32 I = 0; I < Live.Num(); ++I)
	{
		Order.Add(I);
		Dist2.Add(FVector::DistSquared(Live[I].Pos, Eye));
	}
	Order.Sort([&Dist2](int32 A, int32 B) { return Dist2[A] > Dist2[B]; });
	TArray<FTransform> Xfs;
	Xfs.SetNum(TWM::MaxPuffs);
	TArray<float> Data;
	Data.SetNumZeroed(TWM::DataFloats);
	const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -10000.0), FVector(1e-4));
	for (int32 Slot = 0; Slot < TWM::MaxPuffs; ++Slot)
	{
		if (Slot >= Order.Num()) { Xfs[Slot] = Hidden; continue; }
		const FPuff& P = Live[Order[Slot]];
		const float F = P.Age / P.Life;
		const FVector ToEye = (Eye - P.Pos).GetSafeNormal();
		FVector U, V;
		ToEye.FindBestAxisVectors(U, V);
		const float Turn = P.Seed * 6.283f + P.Age * 0.05f;
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, U * FMath::Cos(Turn) + V * FMath::Sin(Turn));
		const float Size = P.Size * (0.7f + 0.3f * F);
		Xfs[Slot] = FTransform(M.ToQuat(), P.Pos, FVector(Size / 100.0f, Size / 100.0f, 1.0f));
		Data[0] = 0.9f;
		Data[1] = 0.93f;
		Data[2] = 0.96f;
		// Swells in and thins away; and thin close to the eye, so walking through it doesn't white out the view.
		const float Away = FMath::Clamp((float)(FMath::Sqrt(Dist2[Order[Slot]]) / (Size * 1.5f)), 0.0f, 1.0f);
		Data[3] = TWM::PuffAlpha * (float)K * FMath::Sin(UE_PI * F) * Away;
		Data[4] = 1.0f;
		Data[5] = P.Seed;
		Puffs->SetCustomData(Slot, Data, false);
	}
	Puffs->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Puffs->MarkRenderStateDirty();
}
