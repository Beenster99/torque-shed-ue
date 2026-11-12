#include "World/TorqueRideWorld.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeLock.h"
#include "ProceduralMeshComponent.h"

#include "World/WorldRide.h"

#include <atomic>
#include <cmath>

// What the workers and the actor share: the squares made and waiting to be put in place.
struct FTorqueRideJobs
{
	FCriticalSection Lock;
	TArray<TPair<FIntVector, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>>> Made;
	std::atomic<int32> Busy{0};
	std::atomic<bool> bStop{false};
};

namespace TRWI
{
constexpr int32 Levels = 3;
constexpr double Finest = 256.0;         // metres across a square of level 0; each level's are 4 times the last's
constexpr int32 CellsOf[Levels] = {64, 128, 64};
constexpr double SplitWithin[Levels] = {0.0, 400.0, 1500.0};   // a square of this level is split when the rider is this near it
constexpr double Reach = 10000.0;        // the ground is laid out this far
constexpr int32 PiecePoints = 64;        // a piece of road: 256 m
constexpr double RoadReach = 2500.0;
constexpr int32 AtATime = 3;             // workers

double SizeOf(int32 Level)
{
	double S = Finest;
	for (int32 I = 0; I < Level; ++I) S *= 4.0;
	return S;
}

// How far (At) is from a square, 0 inside it.
double Away(const FVector2D& At, const FIntVector& Key)
{
	const double S = SizeOf(Key.X);
	const double X0 = Key.Y * S, Z0 = Key.Z * S;
	const double Dx = FMath::Max3(X0 - At.X, 0.0, At.X - (X0 + S)), Dz = FMath::Max3(Z0 - At.Y, 0.0, At.Y - (Z0 + S));
	return FMath::Sqrt(Dx * Dx + Dz * Dz);
}

bool Overlap(const FIntVector& A, const FIntVector& B)
{
	const double Sa = SizeOf(A.X), Sb = SizeOf(B.X);
	const double Ax = A.Y * Sa, Az = A.Z * Sa, Bx = B.Y * Sb, Bz = B.Z * Sb;
	return Ax < Bx + Sb - 0.5 && Bx < Ax + Sa - 0.5 && Az < Bz + Sb - 0.5 && Bz < Az + Sa - 0.5;
}
} // namespace TRWI

ATorqueRideWorld::ATorqueRideWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Tags.Add(TEXT("Land"));
}

void ATorqueRideWorld::Begin(std::shared_ptr<TorqueWorld::RidePlan> InPlan)
{
	Plan = InPlan;
	if (!Plan || !Plan->Ok || Plan->Road.size() < 2) return;
	Jobs = std::make_shared<FTorqueRideJobs>();
	// (the ground in the hand-built ride's scanned grass, moor and rock when Tools/ue_build_rideuk.py
	// could make it from Buttertubs' textures; the plain mottled one otherwise)
	LandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLandScan.M_RideLandScan"));
	const bool bScannedLand = LandMaterial != nullptr;
	if (!LandMaterial) LandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLand.M_RideLand"));
	RoadMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRoad.M_RideRoad"));
	StoneMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideStone.M_RideStone"));
	MasonryMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideMasonry.M_RideMasonry"));
	LampMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLamp.M_RideLamp"));
	if (!LandMaterial || !RoadMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque ride world: no materials in /Game/RideUK/Materials (run Tools/ue_build_rideuk.py); the ground is drawn plain"));
	}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: materials: ground %s, walls %s, houses %s"), bScannedLand ? TEXT("scanned") : TEXT("plain"),
		StoneMaterial ? TEXT("scanned stone") : TEXT("plain"), MasonryMaterial ? TEXT("scanned masonry") : TEXT("plain"));
	if (!StoneMaterial) StoneMaterial = RoadMaterial;
	if (!MasonryMaterial) MasonryMaterial = StoneMaterial;
	// (before any square is made: whether its trees come as real ones, and how much grass)
	const bool bKit = FindKit();
	bool bTreeModels = false;
	for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds && bKit; ++K) bTreeModels = bTreeModels || KindNear[K].Num() > 0;
	Plan->RealTrees = bTreeModels;
	float Density = 1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueFoliage="), Density)) Plan->GrassDensity = FMath::Clamp(Density, 0.0f, 1.0f);
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueFoliageCull="), CullScale)) CullScale = FMath::Clamp(CullScale, 0.05f, 4.0f);
	if (!bKit || KitTrees == 0) Plan->GrassDensity = 0.0;   // (nothing to draw it with: don't lay it out)
	const FVector2D Start(Plan->Road[0].At.X, Plan->Road[0].At.Z);
	const double T0 = FPlatformTime::Seconds();
	Update(Start, true);
	UpdateRoad(Start, true);
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: %d squares of ground and %d pieces of road round the start in %.2f s; the road starts at %.0f m above the sea"),
		Tiles.Num(), RoadParts.Num(), FPlatformTime::Seconds() - T0, Plan->Road[0].Height);
}

void ATorqueRideWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Jobs)
	{
		// (a worker part way through a square reads the world's stores: let it finish before they can go)
		Jobs->bStop = true;
		const double GiveUpAt = FPlatformTime::Seconds() + 5.0;
		while (Jobs->Busy.load() > 0 && FPlatformTime::Seconds() < GiveUpAt) FPlatformProcess::Sleep(0.002f);
	}
	Super::EndPlay(Reason);
}

FVector2D ATorqueRideWorld::RiderAt() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				const FVector L = Pawn->GetActorLocation();
				return FVector2D(L.X / 100.0, L.Y / 100.0);
			}
		}
	}
	return LookedAt.X < 1e11 ? LookedAt : FVector2D(Plan->Road[0].At.X, Plan->Road[0].At.Z);
}

void ATorqueRideWorld::Wanted(const FVector2D& At, TArray<FIntVector>& Out) const
{
	Out.Reset();
	const int32 Top = TRWI::Levels - 1;
	const double S = TRWI::SizeOf(Top);
	const int32 X0 = FMath::FloorToInt32((At.X - TRWI::Reach) / S), X1 = FMath::FloorToInt32((At.X + TRWI::Reach) / S);
	const int32 Z0 = FMath::FloorToInt32((At.Y - TRWI::Reach) / S), Z1 = FMath::FloorToInt32((At.Y + TRWI::Reach) / S);
	TArray<FIntVector> Open;
	for (int32 Z = Z0; Z <= Z1; ++Z)
		for (int32 X = X0; X <= X1; ++X)
		{
			const FIntVector Key(Top, X, Z);
			if (TRWI::Away(At, Key) <= TRWI::Reach) Open.Add(Key);
		}
	while (Open.Num() > 0)
	{
		const FIntVector Key = Open.Pop(EAllowShrinking::No);
		if (Key.X > 0 && TRWI::Away(At, Key) < TRWI::SplitWithin[Key.X])
		{
			for (int32 Dz = 0; Dz < 4; ++Dz)
				for (int32 Dx = 0; Dx < 4; ++Dx) Open.Add(FIntVector(Key.X - 1, Key.Y * 4 + Dx, Key.Z * 4 + Dz));
		}
		else Out.Add(Key);
	}
	// Nearest first: they are made in this order.
	Out.Sort([&At](const FIntVector& A, const FIntVector& B) { return TRWI::Away(At, A) < TRWI::Away(At, B); });
}

void ATorqueRideWorld::Update(const FVector2D& At, bool bAtOnce)
{
	LookedAt = At;
	Wanted(At, Want);
	const std::shared_ptr<const TorqueWorld::RidePlan> Held = Plan;
	const std::shared_ptr<FTorqueRideJobs> Share = Jobs;
	for (const FIntVector& Key : Want)
	{
		if (Tiles.Contains(Key) || Asked.Contains(Key)) continue;
		const double Size = TRWI::SizeOf(Key.X);
		const int32 Cells = TRWI::CellsOf[Key.X];
		if (bAtOnce)
		{
			// Before anyone is on the road: the finest squares near the start now, so the bike has
			// ground under it (the rest come from the workers as soon as the ride is under way).
			if (Key.X != 0 || TRWI::Away(At, Key) > 300.0) continue;
			TorqueWorld::RideMesh Mesh;
			Plan->LandTile(Key.Y * Size, Key.Z * Size, Size, Cells, Mesh);
			Place(Key, Mesh, true);
			continue;
		}
		if (Share->Busy.load() >= TRWI::AtATime) break;
		Asked.Add(Key);
		++Share->Busy;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Held, Share, Key, Size, Cells]()
		{
			TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe> Mesh = MakeShared<TorqueWorld::RideMesh, ESPMode::ThreadSafe>();
			if (!Share->bStop) Held->LandTile(Key.Y * Size, Key.Z * Size, Size, Cells, *Mesh);
			{
				FScopeLock Hold(&Share->Lock);
				Share->Made.Emplace(Key, Mesh);
			}
			--Share->Busy;
		});
	}
}

UProceduralMeshComponent* ATorqueRideWorld::MakePart(const TorqueWorld::RideMesh& Mesh, UMaterialInterface* Material, bool bCollide, bool bAtOnce, bool bShadow)
{
	TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> One;
	One.Emplace(&Mesh, Material);
	return MakePart(One, bCollide, bAtOnce, bShadow);
}

UProceduralMeshComponent* ATorqueRideWorld::MakePart(const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections, bool bCollide, bool bAtOnce, bool bShadow)
{
	UProceduralMeshComponent* Part = nullptr;
	int32 Section = 0;
	for (const TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>& Each : Sections)
	{
		const TorqueWorld::RideMesh* Mesh = Each.Key;
		const int32 Count = Mesh ? int32(Mesh->Vertices()) : 0;
		if (Count < 3 || Mesh->Idx.size() < 3) continue;
		// The ride's frame is metres, x east, y up, z south; Unreal's centimetres, x east, y south, z up.
		TArray<FVector> Points;
		TArray<FVector> Normals;
		TArray<FVector2D> Uvs;
		TArray<int32> Triangles;
		TArray<FLinearColor> Colours;
		const bool bColoured = Mesh->Col.size() == Mesh->Pos.size();
		const bool bAlpha = Mesh->Alpha.size() == size_t(Count);
		if (bColoured) Colours.Reserve(Count);
		Points.Reserve(Count);
		Normals.Reserve(Count);
		Uvs.Reserve(Count);
		Triangles.Reserve(int32(Mesh->Idx.size()));
		for (int32 I = 0; I < Count; ++I)
		{
			const size_t V = size_t(I) * 3;
			Points.Add(FVector(double(Mesh->Pos[V]) * 100.0, double(Mesh->Pos[V + 2]) * 100.0, double(Mesh->Pos[V + 1]) * 100.0));
			Normals.Add(FVector(double(Mesh->Nrm[V]), double(Mesh->Nrm[V + 2]), double(Mesh->Nrm[V + 1])));
			Uvs.Add(FVector2D(double(Mesh->Uv[size_t(I) * 2]), double(Mesh->Uv[size_t(I) * 2 + 1])));
			if (bColoured) Colours.Add(FLinearColor(Mesh->Col[V], Mesh->Col[V + 1], Mesh->Col[V + 2], bAlpha ? Mesh->Alpha[size_t(I)] : 1.0f));
		}
		// (the plan winds its triangles anticlockwise seen from above, which is Unreal's front face too:
		// the two frames name the same points)
		for (uint32 Index : Mesh->Idx) Triangles.Add(int32(Index));
		if (!Part)
		{
			Part = NewObject<UProceduralMeshComponent>(this);
			Part->bUseAsyncCooking = !bAtOnce;
			Part->SetMobility(EComponentMobility::Movable);
			Part->SetupAttachment(RootComponent);
			Part->SetRelativeLocation(FVector(Mesh->OriginX * 100.0, Mesh->OriginZ * 100.0, 0.0));
			Part->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
			Part->SetCastShadow(bShadow);
			Part->RegisterComponent();
		}
		// (the colours are kept as they are, linear: M_RideRoad reads the vertex colour as the road's own,
		// M_RideStone and M_RideMasonry as their masks)
		Part->CreateMeshSection_LinearColor(Section, Points, Triangles, Normals, Uvs, Colours, TArray<FProcMeshTangent>(), bCollide, false);
		if (Each.Value) Part->SetMaterial(Section, Each.Value);
		++Section;
	}
	return Part;
}

void ATorqueRideWorld::Place(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh, bool bAtOnce)
{
	if (TObjectPtr<UProceduralMeshComponent>* Old = Tiles.Find(Key))
	{
		if (*Old) (*Old)->DestroyComponent();
	}
	Tiles.Add(Key, MakePart(Mesh, LandMaterial, Key.X == 0, bAtOnce, Key.X < TRWI::Levels - 1));
	DropPlants(Key);
	PlacePlants(Key, Mesh);
	if (TObjectPtr<UProceduralMeshComponent>* OldExtra = TileExtras.Find(Key))
	{
		if (*OldExtra) (*OldExtra)->DestroyComponent();
		TileExtras.Remove(Key);
	}
	if (Mesh.Extra)
	{
		// (what stands on it: only the nearest squares' cast shadows)
		TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> Kinds;
		Kinds.Emplace(Mesh.Extra.get(), RoadMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Stone.get(), StoneMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Masonry.get(), MasonryMaterial.Get());
		if (UProceduralMeshComponent* Stands = MakePart(Kinds, false, bAtOnce, Key.X == 0)) TileExtras.Add(Key, Stands);
	}
}

void ATorqueRideWorld::UpdateRoad(const FVector2D& At, bool bAtOnce)
{
	// Every road (the ride's own, then those that meet it) in pieces of PiecePoints points; a piece's
	// key is its road times 4096 and which piece of it.
	const int32 Roads = Plan->RoadCount();
	for (int32 Which = 0; Which < Roads; ++Which)
	{
		const std::vector<TorqueWorld::RoadPoint>& Pts = Plan->RoadOf(Which);
		const int32 Points = int32(Pts.size());
		if (Points < 2) continue;
		const int32 Pieces = FMath::Min((Points - 1 + TRWI::PiecePoints - 1) / TRWI::PiecePoints, 4096);
		for (int32 K = 0; K < Pieces; ++K)
		{
			const int32 First = K * TRWI::PiecePoints, Last = FMath::Min(First + TRWI::PiecePoints, Points - 1);
			const TorqueWorld::RoadPoint& Mid = Pts[size_t((First + Last) / 2)];
			const double HowFar = FVector2D::Distance(At, FVector2D(Mid.At.X, Mid.At.Z));
			const int32 Key = Which * 4096 + K;
			const bool bHave = RoadParts.Contains(Key);
			if (!bHave && HowFar < TRWI::RoadReach)
			{
				TorqueWorld::RideMesh Mesh;
				Plan->RoadPiece(Which, First, Last, Mesh);
				// (a tunnel's lamps as a second section: they glow, and a few real lights under them)
				TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> Sections;
				Sections.Emplace(&Mesh, RoadMaterial.Get());
				if (Mesh.Glow) Sections.Emplace(Mesh.Glow.get(), LampMaterial ? LampMaterial.Get() : RoadMaterial.Get());
				UProceduralMeshComponent* Part = MakePart(Sections, true, bAtOnce, false);
				if (Part)
				{
					Part->ComponentTags.Add(TEXT("Road"));
					TArray<TWeakObjectPtr<UPointLightComponent>> Lights;
					for (size_t L = 0; L + 2 < Mesh.Lights.size(); L += 3)
					{
						UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
						Lamp->SetMobility(EComponentMobility::Movable);
						Lamp->SetupAttachment(Part);
						Lamp->SetRelativeLocation(FVector(double(Mesh.Lights[L]) * 100.0, double(Mesh.Lights[L + 2]) * 100.0, double(Mesh.Lights[L + 1]) * 100.0));
						Lamp->SetIntensityUnits(ELightUnits::Lumens);
						Lamp->SetIntensity(2500.0f);
						Lamp->SetAttenuationRadius(1800.0f);
						Lamp->SetLightColor(FLinearColor(1.0f, 0.86f, 0.66f));
						Lamp->SetCastShadows(false);
						Lamp->RegisterComponent();
						Lights.Add(Lamp);
					}
					if (Lights.Num() > 0) RoadLights.Add(Key, Lights);
				}
				RoadParts.Add(Key, Part);
			}
			else if (bHave && HowFar > TRWI::RoadReach + 500.0)
			{
				if (UProceduralMeshComponent* Part = RoadParts[Key]) Part->DestroyComponent();
				RoadParts.Remove(Key);
				if (TArray<TWeakObjectPtr<UPointLightComponent>>* Lights = RoadLights.Find(Key))
				{
					for (const TWeakObjectPtr<UPointLightComponent>& Lamp : *Lights) if (Lamp.IsValid()) Lamp->DestroyComponent();
					RoadLights.Remove(Key);
				}
			}
		}
	}
}

void ATorqueRideWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Plan || !Jobs) return;
	// What the workers have made goes in place, a few a frame.
	for (int32 N = 0; N < 4; ++N)
	{
		TPair<FIntVector, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>> One;
		{
			FScopeLock Hold(&Jobs->Lock);
			if (Jobs->Made.Num() == 0) break;
			One = Jobs->Made[0];
			Jobs->Made.RemoveAt(0);
		}
		Asked.Remove(One.Key);
		if (One.Value.IsValid()) Place(One.Key, *One.Value, false);
	}
	SinceLook += DeltaSeconds;
	if (SinceLook < 0.2) return;
	SinceLook = 0.0;
	const FVector2D At = RiderAt();
	Update(At, false);
	UpdateRoad(At, false);
	// A square no longer wanted goes once every wanted square over the same ground is in place.
	TArray<FIntVector> Going;
	const TSet<FIntVector> WantSet(Want);
	bool bAll = true;
	for (const FIntVector& Key : Want) bAll = bAll && Tiles.Contains(Key);
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Have : Tiles)
	{
		if (WantSet.Contains(Have.Key)) continue;
		bool bCovered = true;
		for (const FIntVector& Key : Want)
		{
			if (TRWI::Overlap(Key, Have.Key) && !Tiles.Contains(Key)) { bCovered = false; break; }
		}
		if (bCovered) Going.Add(Have.Key);
	}
	for (const FIntVector& Key : Going)
	{
		if (UProceduralMeshComponent* Part = Tiles[Key]) Part->DestroyComponent();
		Tiles.Remove(Key);
		DropPlants(Key);
		if (TObjectPtr<UProceduralMeshComponent>* Stands = TileExtras.Find(Key))
		{
			if (*Stands) (*Stands)->DestroyComponent();
			TileExtras.Remove(Key);
		}
	}
	if (bAll && !bSaidReady)
	{
		bSaidReady = true;
		UE_LOG(LogTemp, Display, TEXT("Torque ride world: all the ground round the rider is in place (%d squares, %d pieces of road, %d plants and trees)"), Tiles.Num(), RoadParts.Num(), PlantsShown);
	}
}

bool ATorqueRideWorld::FindKit()
{
	Kit.Reset();
	KindNear.Reset();
	KindFar.Reset();
	KindNear.SetNum(TorqueWorld::RidePlan::PlantKinds);
	KindFar.SetNum(TorqueWorld::RidePlan::PlantKinds);
	KitTrees = 0;
	const ATorqueFoliage* Grass = nullptr;
	const ATorqueFoliage* Trees = nullptr;
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("RideGrass"))) Grass = *It;
			if (It->ActorHasTag(TEXT("RideTrees"))) Trees = *It;
		}
	if (Grass)
	{
		GrassFrame = FVector(Grass->FrameYaw, Grass->FrameMirror, Grass->FrameScale);
		for (const FTorqueFoliageSpecies& S : Grass->Species)
		{
			if (!S.Mesh && S.Variants.Num() == 0) continue;
			for (int32 K = 0; K < TorqueWorld::RidePlan::FirstTree; ++K)
				if (S.Name == FName(TorqueWorld::RidePlan::PlantName(K))) KindNear[K].Add(Kit.Num());
			Kit.Add(S);
		}
	}
	KitTrees = Kit.Num();
	if (Trees)
	{
		TreeFrame = FVector(Trees->FrameYaw, Trees->FrameMirror, Trees->FrameScale);
		for (const FTorqueFoliageSpecies& S : Trees->Species)
		{
			if (!S.Mesh && S.Variants.Num() == 0) continue;
			const FString Name = S.Name.ToString();
			for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
			{
				const FString Kind = UTF8_TO_TCHAR(TorqueWorld::RidePlan::PlantName(K));
				if (Name.StartsWith(TEXT("SM_Tree_") + Kind + TEXT("_"))) KindNear[K].Add(Kit.Num());
				if (Name.StartsWith(TEXT("SM_TreeFar_") + Kind + TEXT("_"))) KindFar[K].Add(Kit.Num());
			}
			Kit.Add(S);
		}
		// (a kind with near models but no far ones uses the near ones far off too, and the other way about;
		// one with neither, the models of the kind that stands in for it: oak for ash and so on)
		for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		{
			if (KindFar[K].Num() == 0) KindFar[K] = KindNear[K];
			if (KindNear[K].Num() == 0) KindNear[K] = KindFar[K];
		}
		for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		{
			const int32 Instead = TorqueWorld::RidePlan::PlantStandIn(K);
			if (KindNear[K].Num() == 0 && Instead != K) { KindNear[K] = KindNear[Instead]; KindFar[K] = KindFar[Instead]; }
		}
	}
	FString Missing;
	for (int32 K = 0; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		if (KindNear[K].Num() == 0) Missing += FString(TEXT(" ")) + UTF8_TO_TCHAR(TorqueWorld::RidePlan::PlantName(K));
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: plant kit: %d grass species, %d tree models%s%s"), KitTrees, Kit.Num() - KitTrees,
		Grass || Trees ? TEXT("") : TEXT(" (no RideGrass or RideTrees actor in the level: run Tools/ue_build_rideuk.py)"),
		Missing.IsEmpty() ? TEXT("") : *(TEXT("; none for") + Missing));
	return Kit.Num() > 0;
}

void ATorqueRideWorld::DropPlants(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>* Old = TilePlants.Find(Key))
	{
		for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Part : *Old)
			if (UInstancedStaticMeshComponent* Ism = Part.Get())
			{
				PlantsShown -= Ism->GetInstanceCount();
				Ism->DestroyComponent();
			}
		TilePlants.Remove(Key);
	}
}

void ATorqueRideWorld::PlacePlants(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	if (Mesh.Plants.empty() || Kit.Num() == 0) return;
	// Instances by the kit's species, its variant, and (for the grass with a near limit, which Unreal
	// applies to a whole component) a 32 m patch of the square.
	TMap<FIntVector, TArray<FTransform>> Groups;
	uint32 Counter = 0;
	for (const TorqueWorld::RidePlant& P : Mesh.Plants)
	{
		++Counter;
		if (P.Kind >= TorqueWorld::RidePlan::PlantKinds) continue;
		const bool bTree = P.Kind >= TorqueWorld::RidePlan::FirstTree;
		const TArray<int32>& Choice = bTree && P.Far ? KindFar[P.Kind] : KindNear[P.Kind];
		if (Choice.Num() == 0) continue;
		// (the grass's kinds have their models as variants in one species; the trees' as species of their own)
		const int32 Si = Choice[FMath::Min(int32(P.Pick * Choice.Num()), Choice.Num() - 1)];
		const FTorqueFoliageSpecies& S = Kit[Si];
		const uint32 H = (uint32(FMath::FloorToInt(P.X * 37.0f)) * 73856093u) ^ (uint32(FMath::FloorToInt(P.Z * 37.0f)) * 19349663u) ^ (Counter * 83492791u);
		const float Roll = float((H * 2654435761u) >> 8) / float(1 << 24);
		if (!bTree && S.Keep < 1.0f && Roll >= S.Keep) continue;   // (bigger scanned plants: fewer of them)
		const int32 Vi = S.Variants.Num() > 1 ? FMath::Min(int32(float((H ^ 0x9e3779b9u) * 2246822519u >> 8) / float(1 << 24) * S.Variants.Num()), S.Variants.Num() - 1) : 0;
		UStaticMesh* Model = S.Variants.Num() ? S.Variants[Vi].Get() : S.Mesh.Get();
		if (!Model) continue;
		const FVector& Frame = bTree ? TreeFrame : GrassFrame;
		double Size = double(P.Scale) * S.ScaleMultiplier;
		if (bTree)
		{
			// (as tall as the canopy map says, whatever the model's own height)
			const double Own = Model->GetBounds().BoxExtent.Z * 2.0 * (S.bNativeMesh ? 1.0 : FMath::Abs(Frame.Z));
			Size = Own > 1.0 ? FMath::Clamp(double(P.Height) * 100.0 / Own, 0.4, 2.5) : 1.0;
		}
		FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(FMath::RadiansToDegrees(double(P.Yaw)) + (S.bNativeMesh ? 0.0 : Frame.X)));
		if (P.Nx != 0.0f || P.Nz != 0.0f)
		{
			// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up)
			const FVector N(P.Nx, P.Nz, FMath::Sqrt(FMath::Max(0.0f, 1.0f - P.Nx * P.Nx - P.Nz * P.Nz)));
			Rot = FQuat::FindBetweenNormals(FVector::UpVector, N.GetSafeNormal()) * Rot;
		}
		const FVector Scale3 = S.bNativeMesh ? FVector(Size) : FVector(Size * Frame.Z, Size * Frame.Z * Frame.Y, Size * Frame.Z);
		const bool bPatch = !bTree && S.MinDrawDistance > 0.0f;
		const int32 Patch = bPatch ? FMath::Clamp(int32(P.X / 32.0f), 0, 63) + 64 * FMath::Clamp(int32(P.Z / 32.0f), 0, 63) : -1;
		Groups.FindOrAdd(FIntVector(Si, Vi, Patch)).Add(FTransform(Rot, FVector(double(P.X) * 100.0, double(P.Z) * 100.0, double(P.Y) * 100.0), Scale3));
	}
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Parts = TilePlants.FindOrAdd(Key);
	for (TPair<FIntVector, TArray<FTransform>>& Group : Groups)
	{
		const FTorqueFoliageSpecies& S = Kit[Group.Key.X];
		const int32 Vi = Group.Key.Y;
		UStaticMesh* Model = S.Variants.Num() ? S.Variants[Vi].Get() : S.Mesh.Get();
		UMaterialInterface* Mat = S.Variants.Num() ? (S.VariantMaterials.IsValidIndex(Vi) ? S.VariantMaterials[Vi].Get() : nullptr) : S.Material.Get();
		const bool bTree = Group.Key.X >= KitTrees;
		// As ATorqueFoliage makes its parts (see TorqueFoliage.cpp): no collision, shadows only from the
		// taller plants and cached while they sway, out of Lumen's and the ray tracer's scenes.
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetupAttachment(RootComponent);
		Ism->SetRelativeLocation(FVector(Mesh.OriginX * 100.0, Mesh.OriginZ * 100.0, 0.0));
		Ism->SetStaticMesh(Model);
		if (Mat) Ism->SetMaterial(0, Mat);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(S.bCastShadow);
		Ism->bCastContactShadow = S.bCastShadow && !bTree;
		Ism->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		Ism->bReceivesDecals = false;
		Ism->bAffectDistanceFieldLighting = bTree;
		Ism->bAffectDynamicIndirectLighting = bTree;
		Ism->bVisibleInRayTracing = false;
		Ism->WorldPositionOffsetDisableDistance = bTree ? 8000 : 2500;
		const float Cull = S.CullDistance * (bTree ? 1.0f : CullScale * 0.5f * (1.0f + FMath::Max(1.0f, S.CullSpread)));
		Ism->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
		Ism->LDMaxDrawDistance = Cull * 1.05f;
		Ism->MinDrawDistance = S.MinDrawDistance * CullScale;
		Ism->RegisterComponent();
		Ism->AddInstances(Group.Value, false, false);
		AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PlantsShown += Group.Value.Num();
	}
}
