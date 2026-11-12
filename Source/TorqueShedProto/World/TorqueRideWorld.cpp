#include "World/TorqueRideWorld.h"
#include "TorqueDash.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueGameState.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
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
#include "Materials/MaterialInstanceDynamic.h"
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
constexpr int32 Levels = 4;
constexpr double Finest = 256.0;         // metres across a square of level 0; each level's are 4 times the last's
constexpr int32 CellsOf[Levels] = {64, 128, 64, 64};
constexpr double SplitWithin[Levels] = {0.0, 400.0, 1500.0, 6000.0};   // a square of this level is split when the rider is this near it
// The ground is laid out this far: the far hills, to the skyline heights' own reach from the road
// (Terrain::LiveReach, 18 km). Beyond 6 km in 16 km squares, a point every 256 m.
constexpr double Reach = 16000.0;
constexpr int32 ShadowLevels = 2;        // the squares of these levels cast shadows (the far hills' are past the shadows' reach)
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

TWeakObjectPtr<ATorqueRideWorld> ATorqueRideWorld::Current;

bool ATorqueRideWorld::PlaceHeard(double& Sea, double& Water, double& Town, double& Sheep, double& Cattle)
{
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	Sea = Ride->Heard[0];
	Water = Ride->Heard[1];
	Town = Ride->Heard[2];
	Sheep = Ride->Heard[3];
	Cattle = Ride->Heard[4];
	return true;
}

void ATorqueRideWorld::DropStreetLights(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UPointLightComponent>>* Lamps = StreetLights.Find(Key))
	{
		for (const TWeakObjectPtr<UPointLightComponent>& Lamp : *Lamps)
			if (Lamp.IsValid()) Lamp->DestroyComponent();
		StreetLights.Remove(Key);
	}
}

bool ATorqueRideWorld::SurfaceUnder(const FVector& At, int32& Kind, double& Lift)
{
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	const TorqueWorld::RidePlan::RideSurface S = Ride->Plan->SurfaceAt(At.X / 100.0, At.Y / 100.0);
	Kind = S.Kind;
	Lift = S.Lift - S.Drawn;   // (the ray hit the mark as drawn, a hair over the road)
	return true;
}

bool ATorqueRideWorld::CattleGrids(TArray<TPair<FVector, FVector>>& Out)
{
	Out.Reset();
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	for (const TorqueWorld::RidePlan::Furnish& F : Ride->Plan->Furniture)
		if (F.Kind == TorqueWorld::RidePlan::CattleGrid)
			Out.Emplace(FVector(F.At.X, F.Height, F.At.Z), FVector(F.Face.X, F.Size, F.Face.Z));
	return true;
}

void ATorqueRideWorld::NavFor(const FVector& At, FTorqueDash& Dash, double& Was)
{
	const ATorqueRideWorld* Ride = Current.Get();
	Dash.bNav = Ride && Ride->Plan && Ride->Plan->Ok && !Ride->Plan->Turns.empty();
	Dash.bAtFuel = false;
	if (!Dash.bNav) return;
	using TorqueWorld::RidePlan;
	const RidePlan::RideNav N = Ride->Plan->NavAt(At.X / 100.0, At.Y / 100.0, Was);
	Was = N.Along;
	Dash.bNavOff = N.OffRoute;
	Dash.NavTurn = N.Turn;
	Dash.NavTo = N.ToTurn;
	Dash.NavOnto = UTF8_TO_TCHAR(N.Onto.c_str());
	Dash.NavLeft = N.ToFinish;
	Dash.bAtFuel = N.ToStop[RidePlan::StopFuel] >= 0.0 && N.ToStop[RidePlan::StopFuel] < 25.0;
	// (the next fuel and the next cafe, whichever's nearer; a viewpoint if it's close)
	Dash.NavStop.Reset();
	double Best = 1e18;
	const bool bMetric = FTorqueSettings::Get().Units == 0;
	for (int32 K : {int32(RidePlan::StopFuel), int32(RidePlan::StopCafe), int32(RidePlan::StopView)})
	{
		const double To = N.ToStop[K];
		if (To < 0.0 || To > (K == RidePlan::StopView ? 3000.0 : 30000.0) || To >= Best) continue;
		Best = To;
		const FString Called = UTF8_TO_TCHAR(N.StopCalled[K].c_str());
		Dash.NavStop = FString::Printf(TEXT("%s %s%s%s"), UTF8_TO_TCHAR(RidePlan::StopName(K)), *FTorqueDash::NavDistance(To, bMetric), Called.IsEmpty() ? TEXT("") : TEXT(": "), *Called);
	}
}

void ATorqueRideWorld::Begin(std::shared_ptr<TorqueWorld::RidePlan> InPlan)
{
	Plan = InPlan;
	Current = this;
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
	RenderMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRender.M_RideRender"));
	WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideWater.M_RideWater"));
	NightMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideNight.M_RideNight"));
	if (!LandMaterial || !RoadMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque ride world: no materials in /Game/RideUK/Materials (run Tools/ue_build_rideuk.py); the ground is drawn plain"));
	}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: materials: ground %s, walls %s, houses %s"), bScannedLand ? TEXT("scanned") : TEXT("plain"),
		StoneMaterial ? TEXT("scanned stone") : TEXT("plain"), MasonryMaterial ? TEXT("scanned masonry") : TEXT("plain"));
	if (!StoneMaterial) StoneMaterial = RoadMaterial;
	if (!MasonryMaterial) MasonryMaterial = StoneMaterial;
	if (!RenderMaterial) RenderMaterial = RoadMaterial;
	if (!WaterMaterial) WaterMaterial = RoadMaterial;
	if (!NightMaterial) NightMaterial = RoadMaterial;   // (pale studs and glass, never lit)
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
	if (Current.Get() == this) Current.Reset();
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

void ATorqueRideWorld::DropWords(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UTextRenderComponent>>* Words = TileWords.Find(Key))
	{
		for (const TWeakObjectPtr<UTextRenderComponent>& W : *Words)
			if (W.IsValid()) W->DestroyComponent();
		TileWords.Remove(Key);
	}
}

void ATorqueRideWorld::DropWater(const FIntVector& Key)
{
	if (TObjectPtr<UProceduralMeshComponent>* Wet = TileWater.Find(Key))
	{
		if (*Wet) (*Wet)->DestroyComponent();
		TileWater.Remove(Key);
	}
}

UMaterialInterface* ATorqueRideWorld::TintedOf(UMaterialInterface* Base, uint32 Which, const TorqueWorld::Tint& By)
{
	// (the stone as it is needs no instance; a material made before Tint was added just ignores it)
	if (!Base || (By.R == 1.0f && By.G == 1.0f && By.B == 1.0f)) return Base;
	const auto Q = [](float V) { return uint32(FMath::Clamp(FMath::RoundToInt32(V * 100.0f), 0, 255)); };
	const uint32 Key = (Which << 24) | (Q(By.R) << 16) | (Q(By.G) << 8) | Q(By.B);
	if (const TObjectPtr<UMaterialInterface>* Have = Tinted.Find(Key)) return Have->Get();
	UMaterialInstanceDynamic* Made = UMaterialInstanceDynamic::Create(Base, this);
	if (!Made) return Base;
	Made->SetVectorParameterValue(TEXT("Tint"), FLinearColor(By.R, By.G, By.B, 1.0f));
	Tinted.Add(Key, Made);
	return Made;
}

void ATorqueRideWorld::Place(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh, bool bAtOnce)
{
	if (TObjectPtr<UProceduralMeshComponent>* Old = Tiles.Find(Key))
	{
		if (*Old) (*Old)->DestroyComponent();
	}
	Tiles.Add(Key, MakePart(Mesh, LandMaterial, Key.X == 0, bAtOnce, Key.X < TRWI::ShadowLevels));
	DropPlants(Key);
	PlacePlants(Key, Mesh);
	PlaceAnimals(Key, Mesh);
	DropWords(Key);
	DropWater(Key);
	// (its water: seen, not cast as shadow, not ridden on)
	if (Mesh.Water && Mesh.Water->Vertices() > 0)
	{
		if (UProceduralMeshComponent* Wet = MakePart(*Mesh.Water, WaterMaterial, false, bAtOnce, false)) TileWater.Add(Key, Wet);
	}
	if (TObjectPtr<UProceduralMeshComponent>* OldExtra = TileExtras.Find(Key))
	{
		if (*OldExtra) (*OldExtra)->DestroyComponent();
		TileExtras.Remove(Key);
	}
	DropStreetLights(Key);
	if (Mesh.Extra)
	{
		// (what stands on it: only the nearest squares' cast shadows)
		TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> Kinds;
		Kinds.Emplace(Mesh.Extra.get(), RoadMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Stone.get(), TintedOf(StoneMaterial.Get(), 0, Mesh.Extra->StoneTint));
		Kinds.Emplace(Mesh.Extra->Masonry.get(), TintedOf(MasonryMaterial.Get(), 1, Mesh.Extra->MasonryTint));
		Kinds.Emplace(Mesh.Extra->Render.get(), RenderMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Night.get(), NightMaterial.Get());
		UProceduralMeshComponent* Stands = MakePart(Kinds, false, bAtOnce, Key.X == 0);
		if (Stands) TileExtras.Add(Key, Stands);
		// [stage 7] The street lamps' lights, on the nearest squares only: warm, unshadowed, on after dark.
		if (Stands && Key.X == 0 && Mesh.Extra->Lights.size() >= 3)
		{
			TArray<TWeakObjectPtr<UPointLightComponent>>& Lamps = StreetLights.FindOrAdd(Key);
			for (size_t L = 0; L + 2 < Mesh.Extra->Lights.size(); L += 3)
			{
				UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetupAttachment(Stands);
				Lamp->SetRelativeLocation(FVector(double(Mesh.Extra->Lights[L]) * 100.0, double(Mesh.Extra->Lights[L + 2]) * 100.0, double(Mesh.Extra->Lights[L + 1]) * 100.0));
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetIntensity(4000.0f);
				Lamp->SetAttenuationRadius(2200.0f);
				Lamp->SetLightColor(FLinearColor(1.0f, 0.8f, 0.55f));
				Lamp->SetCastShadows(false);
				Lamp->SetVisibility(bStreetLightsOn);
				Lamp->RegisterComponent();
				Lamps.Add(Lamp);
			}
		}
		// The words on its signs, as text standing just proud of each board.
		TArray<TWeakObjectPtr<UTextRenderComponent>>& Words = TileWords.FindOrAdd(Key);
		for (const TorqueWorld::RideMesh::Label& L : Mesh.Extra->Labels)
		{
			UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
			if (!Text) continue;
			Text->SetupAttachment(RootComponent);
			// (the ride's frame: x east, y up, z south; Unreal's: x east, y south, z up)
			Text->SetRelativeLocation(FVector((Mesh.Extra->OriginX + L.X) * 100.0, (Mesh.Extra->OriginZ + L.Z) * 100.0, L.Y * 100.0));
			Text->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(double(L.Nz), double(L.Nx))), 0.0));
			Text->SetText(FText::FromString(FString(UTF8_TO_TCHAR(L.Text.c_str()))));
			Text->SetHorizontalAlignment(EHTA_Center);
			Text->SetVerticalAlignment(EVRTA_TextCenter);
			Text->SetWorldSize(L.Size * 100.0f);
			Text->SetTextRenderColor(FLinearColor(L.R, L.G, L.B).ToFColor(true));
			Text->SetCastShadow(false);
			Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Text->RegisterComponent();
			Words.Add(Text);
		}
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
				if (Mesh.Night) Sections.Emplace(Mesh.Night.get(), NightMaterial.Get());   // [stage 7] cat's eyes
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
	TickBirds(DeltaSeconds);
	// [stage 7] The street lamps come on as the light goes, and off in the morning.
	if (const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr)
	{
		const bool bOn = GS->GetDayLight() < (bStreetLightsOn ? 0.55f : 0.45f);
		if (bOn != bStreetLightsOn)
		{
			bStreetLightsOn = bOn;
			for (TPair<FIntVector, TArray<TWeakObjectPtr<UPointLightComponent>>>& Square : StreetLights)
				for (const TWeakObjectPtr<UPointLightComponent>& Lamp : Square.Value)
					if (Lamp.IsValid()) Lamp->SetVisibility(bOn);
		}
	}
	// (the sound of the place, once a second: a fraction of a millisecond)
	SinceHeard += DeltaSeconds;
	if (SinceHeard >= 1.0 && BirdsLookedAt.X < 1e11)
	{
		SinceHeard = 0.0;
		const TorqueWorld::RidePlace Place = Plan->SoundsAt(BirdsLookedAt.X, BirdsLookedAt.Y);
		Heard[0] = Place.Sea; Heard[1] = Place.Water; Heard[2] = Place.Town; Heard[3] = Place.Sheep; Heard[4] = Place.Cattle;
	}
	SinceLook += DeltaSeconds;
	if (SinceLook < 0.2) return;
	SinceLook = 0.0;
	const FVector2D At = RiderAt();
	if (BirdsLookedAt.X < 1e11 && FVector2D::Distance(At, BirdsLookedAt) > 0.5) BirdsGoing = (At - BirdsLookedAt).GetSafeNormal();
	BirdsLookedAt = At;
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
		DropWords(Key);
		DropWater(Key);
		if (TObjectPtr<UProceduralMeshComponent>* Stands = TileExtras.Find(Key))
		{
			if (*Stands) (*Stands)->DestroyComponent();
			TileExtras.Remove(Key);
		}
		DropStreetLights(Key);
	}
	if (bAll && !bSaidReady)
	{
		bSaidReady = true;
		UE_LOG(LogTemp, Display, TEXT("Torque ride world: all the ground round the rider is in place (%d squares, %d pieces of road, %d plants, trees and animals; %d animals placed so far)"), Tiles.Num(), RoadParts.Num(), PlantsShown, AnimalsShown);
	}
}

namespace TRWBirds
{
// A bird with its wings spread, facing +x in the ride's frame, wings along z: 0 a buzzard (broad
// brown wings, a fanned tail), 1 a red kite (long, angled rufous wings, a forked tail), 2 a gull
// (white, grey wings with black tips). Seen from both sides.
TorqueWorld::RideMesh BirdMesh(int32 Kind)
{
	TorqueWorld::RideMesh M;
	const float Span = Kind == 1 ? 0.8f : Kind == 2 ? 0.68f : 0.62f;   // (half the wingspan, m)
	const float Body[3] = {Kind == 2 ? 0.55f : 0.09f, Kind == 2 ? 0.55f : 0.06f, Kind == 2 ? 0.55f : 0.04f};
	const float Wing[3] = {Kind == 2 ? 0.32f : Kind == 1 ? 0.17f : 0.075f, Kind == 2 ? 0.33f : Kind == 1 ? 0.065f : 0.052f, Kind == 2 ? 0.35f : Kind == 1 ? 0.028f : 0.035f};
	const float Tip[3] = {Kind == 2 ? 0.02f : 0.03f, Kind == 2 ? 0.02f : 0.025f, Kind == 2 ? 0.02f : 0.02f};
	const auto Tri = [&M](const float* A, const float* B, const float* C, const float* Colour)
	{
		for (int Side = 0; Side < 2; ++Side)
		{
			const uint32 V = uint32(M.Vertices());
			for (const float* P : {A, B, C})
			{
				M.Pos.insert(M.Pos.end(), {P[0], P[1], P[2]});
				M.Nrm.insert(M.Nrm.end(), {0.0f, Side == 0 ? 1.0f : -1.0f, 0.0f});
				M.Uv.insert(M.Uv.end(), {P[0], P[2]});
				M.Col.insert(M.Col.end(), {Colour[0], Colour[1], Colour[2]});
				M.Alpha.push_back(0.0f);
			}
			if (Side == 0) M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2});
			else M.Idx.insert(M.Idx.end(), {V, V + 2, V + 1});
		}
	};
	// The body: a long diamond, nose to tail.
	const float Nose[3] = {0.28f, 0.0f, 0.0f}, Back[3] = {-0.25f, 0.0f, 0.0f}, L[3] = {0.0f, 0.02f, -0.07f}, R[3] = {0.0f, 0.02f, 0.07f};
	Tri(Nose, L, Back, Body);
	Tri(Nose, Back, R, Body);
	// The wings, raised a little (a buzzard's shallow V), the kite's swept back at the wrist.
	for (float S : {-1.0f, 1.0f})
	{
		const float Root0[3] = {0.12f, 0.0f, S * 0.05f}, Root1[3] = {-0.1f, 0.0f, S * 0.05f};
		const float Wrist0[3] = {Kind == 1 ? 0.08f : 0.1f, Kind == 0 ? 0.06f : 0.03f, S * Span * 0.55f}, Wrist1[3] = {Kind == 1 ? -0.14f : -0.12f, Kind == 0 ? 0.06f : 0.03f, S * Span * 0.55f};
		const float End0[3] = {Kind == 1 ? -0.05f : 0.02f, Kind == 0 ? 0.11f : 0.02f, S * Span}, End1[3] = {Kind == 1 ? -0.2f : -0.1f, Kind == 0 ? 0.11f : 0.02f, S * Span * 0.95f};
		Tri(Root0, Wrist0, Root1, Wing);
		Tri(Root1, Wrist0, Wrist1, Wing);
		Tri(Wrist0, End0, Wrist1, Kind == 2 ? Tip : Wing);
		Tri(Wrist1, End0, End1, Kind == 2 ? Tip : Wing);
	}
	// The tail: fanned, or forked on a kite.
	if (Kind == 1)
	{
		const float A[3] = {-0.22f, 0.0f, -0.03f}, B[3] = {-0.22f, 0.0f, 0.03f}, C[3] = {-0.5f, 0.0f, -0.09f}, D[3] = {-0.5f, 0.0f, 0.09f}, E[3] = {-0.42f, 0.0f, 0.0f};
		Tri(A, C, E, Wing);
		Tri(B, E, D, Wing);
		Tri(A, E, B, Wing);
	}
	else
	{
		const float A[3] = {-0.22f, 0.0f, -0.03f}, B[3] = {-0.22f, 0.0f, 0.03f}, C[3] = {-0.42f, 0.0f, -0.09f}, D[3] = {-0.42f, 0.0f, 0.09f};
		Tri(A, C, D, Body);
		Tri(A, D, B, Body);
	}
	return M;
}

double Unit(uint32 H)
{
	H ^= H >> 16; H *= 0x7feb352du; H ^= H >> 15; H *= 0x846ca68bu; H ^= H >> 16;
	return double(H & 0xffffffu) / double(0x1000000);
}
} // namespace TRWBirds

void ATorqueRideWorld::SendBird(FRideBird& Bird, const FVector2D& At, uint32 Salt)
{
	// Somewhere 150 to 450 m off, mostly ahead (the way the rider was last seen going), circling 35 to
	// 110 m up (gulls lower and quicker), round 40 to 90 m.
	const FVector2D Going = BirdsGoing;
	const double Turn = (TRWBirds::Unit(Salt * 3u + 1u) - 0.5) * 2.4;
	const double Far = 150.0 + 300.0 * TRWBirds::Unit(Salt * 3u + 2u);
	const FVector2D Dir(Going.X * FMath::Cos(Turn) - Going.Y * FMath::Sin(Turn), Going.X * FMath::Sin(Turn) + Going.Y * FMath::Cos(Turn));
	const FVector2D Where = At + Dir * Far;
	// (which kind: gulls if the sea is within 4 km, red kites in a share of the places they live, else buzzards)
	bool bSea = false;
	if (Plan->Land)
		for (int32 K = 0; K < 8 && !bSea; ++K)
			for (double R : {1500.0, 4000.0})
				bSea = bSea || Plan->Land->SeaAt(Where.X + FMath::Cos(K * PI / 4.0) * R, Where.Y + FMath::Sin(K * PI / 4.0) * R);
	const FString Region = UTF8_TO_TCHAR(Plan->LookHere(Where.X, Where.Y).Name.c_str());
	const bool bKites = Region == TEXT("wales") || Region == TEXT("lowland") || Region == TEXT("cotswolds") || Region == TEXT("dales") || Region == TEXT("highlands");
	const double Pick = TRWBirds::Unit(Salt * 7u + 5u);
	const int32 Kind = bSea && Pick < 0.7 ? 2 : bKites && Pick < 0.5 ? 1 : 0;
	const double Ground = Plan->Ground(Where.X, Where.Y);
	Bird.Centre = FVector(Where.X, (FMath::IsNaN(Ground) ? 300.0 : Ground) + (Kind == 2 ? 20.0 + 30.0 * Pick : 35.0 + 75.0 * Pick), Where.Y);
	Bird.Radius = 40.0 + 50.0 * TRWBirds::Unit(Salt * 11u + 3u);
	Bird.Speed = (Kind == 2 ? 11.0 : Kind == 1 ? 8.0 : 9.0) * (TRWBirds::Unit(Salt * 13u + 7u) < 0.5 ? -1.0 : 1.0);
	Bird.Angle = TRWBirds::Unit(Salt * 17u + 9u) * 2.0 * PI;
	Bird.Bank = FMath::Atan2(Bird.Speed * Bird.Speed, 9.81 * Bird.Radius);
	Bird.Drift = 0.0;
	if (!Bird.Part.IsValid() || Bird.Kind != Kind)
	{
		if (Bird.Part.IsValid()) Bird.Part->DestroyComponent();
		const TorqueWorld::RideMesh Shape = TRWBirds::BirdMesh(Kind);
		Bird.Part = MakePart(Shape, RenderMaterial, false, true, false);
	}
	Bird.Kind = Kind;
}

void ATorqueRideWorld::TickBirds(float DeltaSeconds)
{
	if (!Plan || BirdsLookedAt.X > 1e11) return;
	if (Birds.Num() == 0) Birds.SetNum(3);
	static uint32 Sent = 1;
	for (FRideBird& Bird : Birds)
	{
		const FVector2D Off(Bird.Centre.X - BirdsLookedAt.X, Bird.Centre.Z - BirdsLookedAt.Y);
		if (!Bird.Part.IsValid() || Off.Size() > 1600.0) SendBird(Bird, BirdsLookedAt, Sent++);
		if (!Bird.Part.IsValid()) continue;
		// Round its circle, the circle itself drifting slowly east as a thermal on the westerlies does.
		Bird.Angle += double(DeltaSeconds) * Bird.Speed / Bird.Radius;
		Bird.Drift += double(DeltaSeconds) * 1.2;
		const double X = Bird.Centre.X + FMath::Cos(Bird.Angle) * Bird.Radius + Bird.Drift, Z = Bird.Centre.Z + FMath::Sin(Bird.Angle) * Bird.Radius;
		const double Y = Bird.Centre.Y + 6.0 * FMath::Sin(Bird.Angle * 0.5);
		// (its heading along the circle, banked into it; the frames share x and their z is Unreal's y)
		const double Heading = Bird.Angle + (Bird.Speed > 0.0 ? 0.5 * PI : -0.5 * PI);
		const double Roll = FMath::RadiansToDegrees(Bird.Bank) * (Bird.Speed > 0.0 ? 1.0 : -1.0);
		Bird.Part->SetRelativeLocationAndRotation(FVector(X * 100.0, Z * 100.0, Y * 100.0), FRotator(0.0, FMath::RadiansToDegrees(Heading), Roll));
	}
}

void ATorqueRideWorld::PlaceAnimals(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	if (Mesh.Animals.empty() || AnimalKit.Num() == 0) return;
	TMap<int32, TArray<FTransform>> Groups;   // by model
	for (const TorqueWorld::RideAnimal& A : Mesh.Animals)
	{
		if (A.Kind >= TorqueWorld::RidePlan::AnimalKinds) continue;
		const TArray<int32>& Choice = AnimalModels[A.Kind];
		if (Choice.Num() == 0) continue;
		const int32 Mi = Choice[FMath::Min(int32(A.Pick * Choice.Num()), Choice.Num() - 1)];
		// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up: a yaw a there is a here)
		const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(FMath::RadiansToDegrees(double(A.Yaw)) + AnimalFrame.X));
		const double Size = double(A.Scale) * AnimalKit[Mi].ScaleMultiplier;
		const FVector Scale3(Size * AnimalFrame.Z, Size * AnimalFrame.Z * AnimalFrame.Y, Size * AnimalFrame.Z);
		Groups.FindOrAdd(Mi).Add(FTransform(Rot, FVector(double(A.X) * 100.0, double(A.Z) * 100.0, double(A.Y) * 100.0), Scale3));
	}
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Parts = TilePlants.FindOrAdd(Key);
	for (TPair<int32, TArray<FTransform>>& Group : Groups)
	{
		const FTorqueFoliageSpecies& S = AnimalKit[Group.Key];
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetupAttachment(RootComponent);
		Ism->SetRelativeLocation(FVector(Mesh.OriginX * 100.0, Mesh.OriginZ * 100.0, 0.0));
		Ism->SetStaticMesh(S.Mesh);
		if (S.Material) Ism->SetMaterial(0, S.Material);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(true);
		Ism->bReceivesDecals = false;
		Ism->bVisibleInRayTracing = false;
		Ism->WorldPositionOffsetDisableDistance = 6000;
		const float Cull = FMath::Max(S.CullDistance, 20000.0f);
		Ism->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
		Ism->LDMaxDrawDistance = Cull * 1.05f;
		Ism->RegisterComponent();
		Ism->AddInstances(Group.Value, false, false);
		AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PlantsShown += Group.Value.Num();   // (let go with the plants, which count them off)
		AnimalsShown += Group.Value.Num();
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
	// [stage 6] The livestock's models, by the kind their name says (SM_Animal_<kind>_<n>).
	AnimalKit.Reset();
	AnimalModels.Reset();
	AnimalModels.SetNum(TorqueWorld::RidePlan::AnimalKinds);
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("RideAnimals"))) continue;
			AnimalFrame = FVector(It->FrameYaw, It->FrameMirror, It->FrameScale);
			for (const FTorqueFoliageSpecies& S : It->Species)
			{
				if (!S.Mesh) continue;
				const FString Name = S.Name.ToString();
				for (int32 K = 0; K < TorqueWorld::RidePlan::AnimalKinds; ++K)
					if (Name.StartsWith(FString(TEXT("SM_Animal_")) + UTF8_TO_TCHAR(TorqueWorld::RidePlan::AnimalName(K)) + TEXT("_"))) AnimalModels[K].Add(AnimalKit.Num());
				AnimalKit.Add(S);
			}
		}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: livestock: %d models%s"), AnimalKit.Num(),
		AnimalKit.Num() ? TEXT("") : TEXT(" (no RideAnimals actor in the level: python Tools/make_ride_animals.py, then Tools/ue_build_rideuk.py)"));
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
