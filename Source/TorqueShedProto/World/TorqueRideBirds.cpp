// [birds] Ride UK's wild birds drawn: see TorqueRideBirds.h.
#include "World/TorqueRideBirds.h"

#include "World/WorldRide.h"
#include "Coop/TorqueGameState.h"
#include "TorqueBikePawn.h"
#include "TorqueFoliage.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

using TorqueWorld::BirdPose;
using TorqueWorld::BirdSpecies;

bool UTorqueRideBirds::Start(AActor* InOwner)
{
	Owner = InOwner;
	bKit = false;
	const int32 Count = int32(BirdSpecies::Count) * int32(BirdPose::Count);
	Models.Init(nullptr, Count);
	Materials.Init(nullptr, Count);
	Parts.Init(nullptr, Count);
	Shown.Init(0, Count);
	Sim.Clear();
	UWorld* World = InOwner ? InOwner->GetWorld() : nullptr;
	if (!World) return false;
	int32 Found = 0;
	for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("RideBirds"))) continue;
		Frame = FVector(It->FrameYaw, It->FrameMirror, It->FrameScale);
		for (const FTorqueFoliageSpecies& S : It->Species)
		{
			if (!S.Mesh) continue;
			const FString Name = S.Name.ToString();
			for (int32 Sp = 0; Sp < int32(BirdSpecies::Count); ++Sp)
				for (int32 Po = 0; Po < int32(BirdPose::Count); ++Po)
					if (Name == FString::Printf(TEXT("SM_Bird_%s_%s"), UTF8_TO_TCHAR(TorqueWorld::BirdSpeciesName(BirdSpecies(Sp))), UTF8_TO_TCHAR(TorqueWorld::BirdPoseName(BirdPose(Po)))))
					{
						Models[Slot(Sp, Po)] = S.Mesh;
						Materials[Slot(Sp, Po)] = S.Material;
						++Found;
					}
		}
	}
	bKit = Found > 0;
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: birds: %d models%s"), Found,
		bKit ? TEXT(" (out of the hedges and trees, perched, hovering, soaring and in flocks)") : TEXT(" (no RideBirds actor in the level: python Tools/make_ride_birds.py, then Tools/ue_build_rideuk.py; the old code-made birds instead)"));
	return bKit;
}

void UTorqueRideBirds::AddPerches(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	if (!bKit || Key.X != 0) return;
	using TorqueWorld::RidePlan;
	std::vector<TorqueWorld::BirdPerch> Perches;
	Perches.reserve(Mesh.Plants.size() / 4 + 8);
	for (const TorqueWorld::RidePlant& P : Mesh.Plants)
	{
		if (P.Far || P.Kind < RidePlan::FirstTree || P.Kind >= RidePlan::PlantKinds) continue;
		const FString Name = UTF8_TO_TCHAR(RidePlan::PlantName(P.Kind));
		TorqueWorld::BirdPerch B;
		B.X = float(Mesh.OriginX + double(P.X));
		B.Y = P.Y;
		B.Z = float(Mesh.OriginZ + double(P.Z));
		if (P.Kind == RidePlan::HedgeKind || Name == TEXT("garden_hedge") || Name == TEXT("garden_shrub") || Name == TEXT("hawthorn") || Name == TEXT("hazel") || Name == TEXT("holly"))
		{
			B.Type = Name.StartsWith(TEXT("garden")) ? 3 : 0;
			B.Top = FMath::Clamp(P.Height > 0.0f ? P.Height : 1.8f, 1.0f, 4.0f);
		}
		else if (Name.StartsWith(TEXT("garden")))
		{
			B.Type = 3;
			B.Top = P.Height > 0.0f ? P.Height : 3.0f;
		}
		else
		{
			B.Type = (Name == TEXT("spruce") || Name == TEXT("pine") || Name == TEXT("larch")) ? 2 : 1;
			B.Top = P.Height > 0.0f ? P.Height : 10.0f * P.Scale;
		}
		Perches.push_back(B);
	}
	// (a wood is thousands of trees: a few thousand perches a square are plenty)
	if (Perches.size() > 3000)
	{
		std::vector<TorqueWorld::BirdPerch> Fewer;
		const size_t Step = Perches.size() / 3000 + 1;
		for (size_t I = 0; I < Perches.size(); I += Step) Fewer.push_back(Perches[I]);
		Perches.swap(Fewer);
	}
	Sim.AddPerches((int64(Key.Y) << 32) ^ int64(uint32(Key.Z)), std::move(Perches));
}

void UTorqueRideBirds::DropPerches(const FIntVector& Key)
{
	if (bKit && Key.X == 0) Sim.DropPerches((int64(Key.Y) << 32) ^ int64(uint32(Key.Z)));
}

UInstancedStaticMeshComponent* UTorqueRideBirds::PartFor(int32 Index)
{
	if (Parts[Index]) return Parts[Index];
	AActor* Holder = Owner.Get();
	UStaticMesh* Model = Models[Index];
	if (!Holder || !Model) return nullptr;
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(Holder, NAME_None, RF_Transient);
	Ism->SetMobility(EComponentMobility::Movable);
	Ism->SetupAttachment(Holder->GetRootComponent());
	Ism->SetStaticMesh(Model);
	if (Materials[Index])
		for (int32 M = 0; M < Model->GetStaticMaterials().Num(); ++M) Ism->SetMaterial(M, Materials[Index]);
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCollisionProfileName(TEXT("NoCollision"));
	Ism->SetCanEverAffectNavigation(false);
	Ism->SetGenerateOverlapEvents(false);
	Ism->SetCastShadow(true);
	Ism->bReceivesDecals = false;
	Ism->bVisibleInRayTracing = false;
	// (the small birds are gone past 300 m; the big ones are seen to 1.5 km)
	const int32 Sp = Index / int32(BirdPose::Count);
	const bool bSmall = Sp == int32(BirdSpecies::Blackbird) || Sp == int32(BirdSpecies::Chaffinch) || Sp == int32(BirdSpecies::Sparrow) || Sp == int32(BirdSpecies::Starling);
	const float Cull = bSmall ? 30000.0f : 150000.0f;
	Ism->SetCullDistances(int32(Cull * 0.9f), int32(Cull));
	Ism->LDMaxDrawDistance = Cull;
	Ism->RegisterComponent();
	Holder->AddInstanceComponent(Ism);
	Parts[Index] = Ism;
	return Ism;
}

void UTorqueRideBirds::Tick(const TorqueWorld::RidePlan& Plan, const double Heard[6], const FString& Region)
{
	AActor* Holder = Owner.Get();
	UWorld* World = Holder ? Holder->GetWorld() : nullptr;
	if (!bKit || !World) return;
	// Everyone riding, from their bikes (each machine has every rider's: co-op's are replicated).
	std::vector<TorqueWorld::BirdRider> Riders;
	for (TActorIterator<ATorqueBikePawn> It(World); It; ++It)
	{
		const FVector L = It->GetActorLocation() - Holder->GetActorLocation();
		const FVector V = It->GetVelocity();
		TorqueWorld::BirdRider R;
		R.X = L.X / 100.0; R.Z = L.Y / 100.0; R.Y = L.Z / 100.0;
		R.Vx = V.X / 100.0; R.Vz = V.Y / 100.0;
		Riders.push_back(R);
	}
	TorqueWorld::BirdPlace Place;
	Place.Sea = Heard[0]; Place.Water = Heard[1]; Place.Town = Heard[2]; Place.Farm = FMath::Min(1.0, Heard[3] + Heard[4] + 0.25);
	Place.Kites = Region == TEXT("wales") || Region == TEXT("lowland") || Region == TEXT("cotswolds") || Region == TEXT("dales") || Region == TEXT("highlands");
	Place.Month = Plan.Month;
	double Now = World->GetTimeSeconds();
	if (const ATorqueGameState* GS = World->GetGameState<ATorqueGameState>())
	{
		Now = GS->GetServerWorldTimeSeconds();
		Place.Hour = GS->GetHour() >= 0.0f ? GS->GetHour() : 12.0;
		Place.Light = GS->GetDayLight();
		const TorqueWeather::State& W = GS->GetWeatherState();
		Place.Rain = FMath::Clamp(W.Rain / 4.0, 0.0, 1.0);
		Place.WindFrom = W.WindFrom;
	}
	const auto Ground = [&Plan](double X, double Z) { return Plan.Ground(X, Z); };
	Sim.Tick(Now, Riders, Place, Ground);
	// Each bird into its model's instances, in the importer's frame.
	const int32 Count = Parts.Num();
	TArray<TArray<FTransform>> Want;
	Want.SetNum(Count);
	const FQuat Fix(FVector::UpVector, FMath::DegreesToRadians(Frame.X));
	const FVector FixScale(Frame.Z, Frame.Z * Frame.Y, Frame.Z);
	for (const TorqueWorld::BirdDraw& D : Sim.Draws())
	{
		int32 Index = Slot(int32(D.Species), int32(D.Pose));
		// (a pose a species wasn't made in: its glide, else its first wingbeat)
		if (!Models[Index]) Index = Slot(int32(D.Species), int32(BirdPose::Glide));
		if (!Models[Index]) Index = Slot(int32(D.Species), int32(BirdPose::Flap1));
		if (!Models[Index]) continue;
		// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up: a yaw there is a yaw here)
		const FRotator Turn(FMath::RadiansToDegrees(D.Pitch), FMath::RadiansToDegrees(D.Yaw), FMath::RadiansToDegrees(D.Roll));
		const FTransform Where(Turn.Quaternion(), FVector(D.X * 100.0, D.Z * 100.0, D.Y * 100.0));
		Want[Index].Add(FTransform(Fix, FVector::ZeroVector, FixScale) * Where);
	}
	for (int32 I = 0; I < Count; ++I)
	{
		const int32 N = Want[I].Num();
		if (N == 0 && Shown[I] == 0) continue;
		UInstancedStaticMeshComponent* Ism = N > 0 ? PartFor(I) : Parts[I].Get();
		if (!Ism) continue;
		if (N == 0)
		{
			Ism->SetVisibility(false);
			Shown[I] = 0;
			continue;
		}
		if (Shown[I] == 0) Ism->SetVisibility(true);
		// (the instances kept, the spare ones shrunk to nothing out of sight)
		const int32 Have = Ism->GetInstanceCount();
		if (Have < N)
		{
			TArray<FTransform> More;
			More.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), N - Have);
			Ism->AddInstances(More, false, false, false);
		}
		TArray<FTransform>& All = Want[I];
		for (int32 K = N; K < FMath::Min(Shown[I], Ism->GetInstanceCount()); ++K) All.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
		Ism->BatchUpdateInstancesTransforms(0, All, false, true, false);
		Shown[I] = N;
	}
	if (World->GetTimeSeconds() - LastSaid > 120.0)
	{
		LastSaid = World->GetTimeSeconds();
		const TorqueWorld::RideBirds::Counts& C = Sim.Seen();
		UE_LOG(LogTemp, Log, TEXT("Torque birds: %d now; so far %d out of hedges and trees (%d birds), %d perched (%d took off), %d hovering, %d soaring, %d flocks (%d birds)"),
			Sim.Alive(), C.Flushes, C.FlushBirds, C.Perched, C.TookOff, C.Hovers, C.Soarers, C.Flocks, C.FlockBirds);
	}
}

void UTorqueRideBirds::Stop()
{
	Sim.Clear();
	for (TObjectPtr<UInstancedStaticMeshComponent>& Part : Parts)
		if (Part) { Part->DestroyComponent(); Part = nullptr; }
	for (int32& N : Shown) N = 0;
}
