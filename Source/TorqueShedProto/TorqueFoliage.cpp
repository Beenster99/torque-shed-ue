#include "TorqueFoliage.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<float> CVarFoliageDensity(
	TEXT("torque.FoliageDensity"), 1.0f,
	TEXT("Share of the moorland plants kept when the level starts (0..1); times the actor's Density."));

namespace
{
template <typename T>
bool Read(const TArray<uint8>& B, int64& At, T& Out)
{
	if (At + (int64)sizeof(T) > B.Num()) return false;
	FMemory::Memcpy(&Out, B.GetData() + At, sizeof(T));
	At += sizeof(T);
	return true;
}

// A stable 0..1 per plant, so a lower density keeps a subset of the same plants.
float Hash01(uint32 X)
{
	X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16;
	return (X & 0xFFFFFF) / float(0x1000000);
}
}

ATorqueFoliage::ATorqueFoliage()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

void ATorqueFoliage::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
}

void ATorqueFoliage::Clear()
{
	for (UInstancedStaticMeshComponent* C : Parts)
		if (C) C->DestroyComponent();
	Parts.Reset();
	NumPlants = 0;
}

void ATorqueFoliage::Rebuild()
{
	Clear();
	const double Start = FPlatformTime::Seconds();

	float Keep = Density * CVarFoliageDensity.GetValueOnGameThread();
	float Cmd = 1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueFoliage="), Cmd)) Keep *= Cmd;
	Keep = FMath::Clamp(Keep, 0.0f, 1.0f);
	if (Keep <= 0.0f) return;

	TArray<uint8> File;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), InstancesFile);
	if (!FFileHelper::LoadFileToArray(File, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Torque] no foliage file at %s (python Tools/make_foliage.py)"), *Path);
		return;
	}
	// "TSFZ", raw size, compressed size, then zlib.
	int64 At = 0;
	uint32 Magic = 0, RawSize = 0, CompSize = 0;
	if (!Read(File, At, Magic) || !Read(File, At, RawSize) || !Read(File, At, CompSize) ||
		Magic != 0x5A465354u || At + CompSize > File.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Torque] %s isn't a foliage file"), *Path);
		return;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, File.GetData() + At, CompSize))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Torque] couldn't unpack %s"), *Path);
		return;
	}
	File.Empty();

	At = 0;
	uint32 Magic2 = 0, NumChunks = 0;
	if (!Read(Raw, At, Magic2) || !Read(Raw, At, NumChunks) || Magic2 != 0x31465354u) return;   // "TSF1"

	TArray<int32> PerSpecies;
	PerSpecies.SetNumZeroed(Species.Num());
	TArray<FTransform> Xf;
	uint32 Counter = 0;
	for (uint32 C = 0; C < NumChunks; ++C)
	{
		uint16 Si = 0, Pad = 0;
		float Ox = 0, Oy = 0, Oz = 0;
		uint32 Count = 0;
		if (!Read(Raw, At, Si) || !Read(Raw, At, Pad) || !Read(Raw, At, Ox) || !Read(Raw, At, Oy) ||
			!Read(Raw, At, Oz) || !Read(Raw, At, Count) || At + int64(Count) * 8 > Raw.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Torque] foliage file cut short"));
			break;
		}
		const uint8* P = Raw.GetData() + At;
		At += int64(Count) * 8;
		if (Si >= Species.Num() || !Species[Si].Mesh)
		{
			Counter += Count;
			continue;
		}
		const FTorqueFoliageSpecies& S = Species[Si];
		Xf.Reset(Count);
		for (uint32 I = 0; I < Count; ++I, P += 8, ++Counter)
		{
			if (Keep < 1.0f && Hash01(Counter) >= Keep) continue;
			uint16 Dx, Dy, Dz;
			FMemory::Memcpy(&Dx, P, 2); FMemory::Memcpy(&Dy, P + 2, 2); FMemory::Memcpy(&Dz, P + 4, 2);
			const float Yaw = P[6] * (360.0f / 256.0f);
			const float Sc = P[7] / 128.0f * S.ScaleMultiplier;
			const FVector Loc(Ox + Dx, Oy + Dy, Oz + Dz);
			if (S.bNativeMesh)
				Xf.Add(FTransform(FRotator(0, Yaw, 0), Loc, FVector(Sc)));
			else
				Xf.Add(FTransform(FRotator(0, FrameYaw + Yaw, 0), Loc,
					FVector(Sc * FrameScale, Sc * FrameScale * FrameMirror, Sc * FrameScale)));
		}
		if (Xf.Num() == 0) continue;

		// Transient: made afresh at every start, never saved into the level.
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Static);
		Ism->SetupAttachment(Root);
		Ism->SetStaticMesh(S.Mesh);
		if (S.Material) Ism->SetMaterial(0, S.Material);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(S.bCastShadow);
		Ism->bCastContactShadow = S.bCastShadow;
		// Swaying grass would otherwise redraw its virtual shadow pages every frame.
		Ism->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		Ism->bReceivesDecals = false;
		// Keep a million plants out of Lumen's scene and the ray tracer's.
		Ism->bAffectDistanceFieldLighting = false;
		Ism->bAffectDynamicIndirectLighting = false;
		Ism->bVisibleInRayTracing = false;
		Ism->WorldPositionOffsetDisableDistance = WpoDisableDistance;
		Ism->SetCullDistances(int32(S.CullDistance * 0.85f), int32(S.CullDistance));
		Ism->LDMaxDrawDistance = S.CullDistance * 1.05f;
		Ism->RegisterComponent();
		Ism->AddInstances(Xf, false, true);
		AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PerSpecies[Si] += Xf.Num();
		NumPlants += Xf.Num();
	}
	FString Summary;
	for (int32 I = 0; I < Species.Num(); ++I)
		if (PerSpecies[I]) Summary += FString::Printf(TEXT(" %s %d"), *Species[I].Name.ToString(), PerSpecies[I]);
	UE_LOG(LogTemp, Log, TEXT("[Torque] foliage: %d plants in %d parts, %.0f ms (density %.2f):%s"),
		NumPlants, Parts.Num(), (FPlatformTime::Seconds() - Start) * 1000.0, Keep, *Summary);
}
