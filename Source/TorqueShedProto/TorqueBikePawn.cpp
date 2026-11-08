#include "TorqueBikePawn.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueSettings.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueFlow.h"

#include "BikeSim.h"
#include "TorqueFit.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#include "RenderCore.h"
#include "RHI.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Coop/TorqueGameState.h"
#include "Coop/TorqueNetBike.h"

using namespace TorqueSim;

namespace
{
constexpr double SimDt = 1.0 / 120.0;   // Torque Shed's physics rate

// Sim metres (x east, y up, z south) to Unreal cm (x east, y south, z up), and back.
FVector ToUE(const V3& P) { return FVector(P.x * 100.0, P.z * 100.0, P.y * 100.0); }
FVector DirToUE(const V3& D) { return FVector(D.x, D.z, D.y); }
V3 ToSim(const FVector& P) { return V3(P.X / 100.0, P.Z / 100.0, P.Y / 100.0); }
V3 DirToSim(const FVector& D) { return V3(D.X, D.Z, D.Y); }

// Sim body space (x right, y up, -z forward) to Unreal local cm (x forward, y right, z up).
FVector BodyToLocal(const V3& P) { return FVector(-P.z * 100.0, P.x * 100.0, P.y * 100.0); }

FQuat BasisToUE(const M3& B)
{
	const FVector Fwd = DirToUE(-B.Z);
	const FVector Up = DirToUE(B.Y);
	return FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
}

// The rider's shapes and kit.
enum ERiderPart
{
	RP_Torso, RP_Pelvis, RP_Shoulders, RP_Neck, RP_Helmet, RP_Visor,
	RP_UpperArmL, RP_UpperArmR, RP_ForearmL, RP_ForearmR, RP_ElbowL, RP_ElbowR, RP_HandL, RP_HandR,
	RP_ThighL, RP_ThighR, RP_ShinL, RP_ShinR, RP_KneeL, RP_KneeR, RP_BootL, RP_BootR, RP_Count
};
enum ERiderMat { RM_Jacket, RM_Jeans, RM_Boots, RM_Gloves, RM_Helmet, RM_Visor, RM_Skin, RM_Dial, RM_Needle, RM_RevNeedle, RM_Count };
// The clock's parts: the dial, the speed needle, the rev needle, then the tick marks.
enum EClockPart { CP_Dial, CP_Speed, CP_Revs, CP_Ticks };
constexpr int32 ClockTicks = 15;

bool LoadJson(const FString& Rel, TSharedPtr<FJsonObject>& Out);

double Approach(double From, double To, double RateUp, double RateDown, double Dt)
{
	const double Rate = (std::fabs(To) > std::fabs(From)) ? RateUp : RateDown;
	return MoveToward(From, To, Rate * Dt);
}

// The ground under the wheels: the first hit on the land or the road (tagged Land and Road by
// Tools/ue_build_level.py), never walls, grass, sheep or anything else in the way.
bool GroundTrace(UWorld* World, const FVector& From, const FVector& To, const FCollisionQueryParams& Params, FHitResult& Out)
{
	FCollisionQueryParams P = Params;
	for (int32 Try = 0; Try < 8; ++Try)   // step past anything else that blocks, a few at most
	{
		if (!World->LineTraceSingleByChannel(Out, From, To, ECC_Visibility, P)) return false;
		const AActor* A = Out.GetActor();
		if (A && (A->ActorHasTag(TEXT("Land")) || A->ActorHasTag(TEXT("Road")))) return true;
		if (!A) return false;
		P.AddIgnoredActor(A);
	}
	return false;
}
} // namespace

ATorqueBikePawn::ATorqueBikePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	// The game mode spawns and possesses each rider's bike; auto-possessing player 0 would let a
	// co-op rider's bike take the host's controller.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	// Co-op: the bike replicates (so every machine has every rider's bike), but its movement comes
	// from NetBike's snapshots, not Unreal's movement replication. Riders far apart on the road
	// still see each other (the default cull distance is 150 m).
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);
	NetBike = CreateDefaultSubobject<UTorqueNetBikeComponent>(TEXT("NetBike"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	ShapeCube = CubeMesh.Object;
	ShapeCylinder = CylinderMesh.Object;
	ShapeSphere = SphereMesh.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	RiderRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RiderRoot"));
	RiderRoot->SetupAttachment(Root);

	auto MakeMesh = [&](const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& Loc,
		const FVector& Scale, const FRotator& Rot = FRotator::ZeroRotator) {
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		if (Mesh) C->SetStaticMesh(Mesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = true;
		return C;
	};

	// Placeholder shapes for the Speed Twin until its model is made from your photos: body origin is at
	// axle height mid wheelbase (1.413 m), wheels about 0.6 m across.
	Body = MakeMesh(TEXT("Body"), CubeMesh.Object, Root, FVector(0, 0, 22), FVector(0.95, 0.26, 0.32));
	Tank = MakeMesh(TEXT("Tank"), SphereMesh.Object, Root, FVector(18, 0, 52), FVector(0.55, 0.3, 0.24));
	Rider = MakeMesh(TEXT("Rider"), CylinderMesh.Object, Root, FVector(-22, 0, 95), FVector(0.36, 0.42, 0.75),
		FRotator(-12, 0, 0));

	FrontPivot = CreateDefaultSubobject<USceneComponent>(TEXT("FrontPivot"));
	FrontPivot->SetupAttachment(Root);
	RearPivot = CreateDefaultSubobject<USceneComponent>(TEXT("RearPivot"));
	RearPivot->SetupAttachment(Root);
	// The engine cylinder is 100 cm tall along z, 100 cm across: lay it on its side along the axle (y).
	FrontWheel = MakeMesh(TEXT("FrontWheel"), CylinderMesh.Object, FrontPivot, FVector::ZeroVector,
		FVector(0.6, 0.6, 0.12), FRotator(0, 0, 90));
	RearWheel = MakeMesh(TEXT("RearWheel"), CylinderMesh.Object, RearPivot, FVector::ZeroVector,
		FVector(0.624, 0.624, 0.16), FRotator(0, 0, 90));

	SteerGroup = CreateDefaultSubobject<USceneComponent>(TEXT("SteerGroup"));
	SteerGroup->SetupAttachment(Root);
	FrontHub = CreateDefaultSubobject<USceneComponent>(TEXT("FrontHub"));
	FrontHub->SetupAttachment(Root);
	SwingGroup = CreateDefaultSubobject<USceneComponent>(TEXT("SwingGroup"));
	SwingGroup->SetupAttachment(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->SetFieldOfView(72.0f);

	Sim = MakeUnique<BikeSim>();
}

ATorqueBikePawn::~ATorqueBikePawn() = default;

void ATorqueBikePawn::BeginPlay()
{
	Super::BeginPlay();
	// Settings > Gameplay > Riding realism, once the player has saved settings (test runs keep their own).
	if (FTorqueSettings::Get().bSaved && !FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"))) Assist = FTorqueSettings::Get().Realism;
	Sim->Setup(BikeSpec::SpeedTwin1200());
	Sim->Ray = [this](const V3& From, const V3& To) {
		RayHit Out;
		UWorld* World = GetWorld();
		if (!World) return Out;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWheel), true, this);
		if (GroundTrace(World, ToUE(From), ToUE(To), Params, Hit))
		{
			Out.Hit = true;
			Out.Position = ToSim(Hit.ImpactPoint);
			Out.Normal = DirToSim(Hit.ImpactNormal).Normalized();
			const AActor* A = Hit.GetActor();
			Out.Surface = (A && A->ActorHasTag(TEXT("Road"))) ? SURF_TARMAC : SURF_GRASS;
		}
		return Out;
	};
	LoadRoute();
	BuildModel();
	{
		// The rider's kit, made by the level script beside the bike's finishes.
		UMaterialInterface* Mats[RM_Count] = {};
		TSharedPtr<FJsonObject> Assets;
		const TSharedPtr<FJsonObject>* Kit = nullptr;
		if (LoadJson(TEXT("Data/Bike/SpeedTwin/ue_assets.json"), Assets) && Assets->TryGetObjectField(TEXT("rider"), Kit))
		{
			static const TCHAR* Names[RM_Count] = {TEXT("jacket"), TEXT("jeans"), TEXT("boots"), TEXT("gloves"),
				TEXT("helmet"), TEXT("visor"), TEXT("skin"), TEXT("dial"), TEXT("needle"), TEXT("revneedle")};
			for (int32 I = 0; I < RM_Count; ++I)
			{
				FString Path;
				if ((*Kit)->TryGetStringField(Names[I], Path)) Mats[I] = LoadObject<UMaterialInterface>(nullptr, *Path);
			}
		}
		BuildRider(Mats);
	}
	// -TorqueStartKm=1.2 starts that far down the 3 km (for checking a spot with a hands-off run).
	float StartKm = 0.f;
	FParse::Value(FCommandLine::Get(), TEXT("TorqueStartKm="), StartKm);
	StartIndex = Line.Num() > 1 ? FMath::RoundToInt(FMath::Clamp(StartKm / (float)(RouteLengthM / 1000.0), 0.f, 1.f) * (Line.Num() - 1)) : 0;
	Respawn(StartIndex);
	// The weather comes from ATorqueGameState (-TorqueWeather= is read there), each frame in Tick.
	// The PlayStation pad is opened when this turns out to be our own bike (ReadInput).
	StartSound();
	// -TorqueCamera=1 starts in first person (2: chin camera, 3: showroom orbit of the bike with the
	// rider hidden), for checking the views. -TorqueOrbitYaw=N holds the orbit at N degrees from
	// straight ahead (positive to the bike's right) instead of turning, for repeatable screenshots.
	int32 StartCamera = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueCamera="), StartCamera)) CameraMode = FMath::Clamp(StartCamera, 0, CameraModes - 1);
	float FixedYaw = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueOrbitYaw="), FixedYaw)) { OrbitYaw = FixedYaw; bOrbitHeld = true; }
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueProfile"))) { bPerfShow = true; bPerfLog = true; }
}

void ATorqueBikePawn::EndPlay(const EEndPlayReason::Type Reason)
{
	Pad.Reset();   // stops its reading thread
	if (SoundAudio) SoundAudio->Stop();
	Super::EndPlay(Reason);
}

namespace
{
bool LoadJson(const FString& Rel, TSharedPtr<FJsonObject>& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), Rel))) return false;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

FVector JsonVec(const TSharedPtr<FJsonObject>& O, const FString& Field)
{
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O->TryGetArrayField(Field, A) || A->Num() < 3) return FVector::ZeroVector;
	return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
}

// The model's frame (metres, x forward, y up, z right) to Unreal local cm (x forward, y right, z up).
FVector ModelToLocal(const FVector& P) { return FVector(P.X * 100.0, P.Z * 100.0, P.Y * 100.0); }
FVector ModelDirToLocal(const FVector& D) { return FVector(D.X, D.Z, D.Y).GetSafeNormal(); }
} // namespace

void ATorqueBikePawn::BuildModel()
{
	// The level script imports the parts and writes where they are, with the importer's frame.
	TSharedPtr<FJsonObject> Assets, Spec;
	if (!LoadJson(TEXT("Data/Bike/SpeedTwin/ue_assets.json"), Assets) ||
		!LoadJson(TEXT("Data/Bike/SpeedTwin/speedtwin.json"), Spec))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no Speed Twin model yet (run Tools/ue_build_level.py); using shapes"));
		return;
	}
	const TSharedPtr<FJsonObject>* Frame = nullptr;
	double Yaw = 0.0, Mirror = 1.0, Scale = 1.0;
	if (Assets->TryGetObjectField(TEXT("frame"), Frame))
	{
		Yaw = (*Frame)->GetNumberField(TEXT("yaw"));
		Mirror = (*Frame)->GetNumberField(TEXT("mirror"));
		Scale = (*Frame)->GetNumberField(TEXT("scale"));
	}
	// What the level's meshes get too: the importer's turn of the glTF frame undone.
	const FTransform Fix(FRotator(0.0, Yaw, 0.0), FVector::ZeroVector, FVector(Scale, Scale * Mirror, Scale));

	const TSharedPtr<FJsonObject>* Axis = nullptr;
	if (Spec->TryGetObjectField(TEXT("steer_axis"), Axis))
	{
		SteerPoint = ModelToLocal(JsonVec(*Axis, TEXT("point")));
		SteerDir = ModelDirToLocal(JsonVec(*Axis, TEXT("dir")));
	}
	SwingPivot = ModelToLocal(JsonVec(Spec, TEXT("swing_pivot")));
	const TSharedPtr<FJsonObject>* Origins = nullptr;
	if (Spec->TryGetObjectField(TEXT("group_origins"), Origins))
		RearRest = ModelToLocal(JsonVec(*Origins, TEXT("rear_wheel")));

	// What to make: the fitment system's assembly of the build (parts on their sockets, swapped
	// parts, geometry mode; Fit/TorqueFit.h), or if there's no fitment data, the model as
	// speedtwin.json lists it.
	struct FPartToMake
	{
		FString Name, Group, MeshPath, MaterialPath;
		FTransform Delta;
		FName Unit;   // the fit unit (the levers and grips are found by it, whichever option they are)
	};
	TArray<FPartToMake> ToMake;
	{
		TArray<FTorqueResolvedPart> Resolved;
		TArray<FTorqueFitClash> Clashes;
		// The bike as built in the shed (it rides out with you); -TorqueBuild= on the command line
		// overrides it for testing swaps without the shed.
		const bool bCommandLineBuild = FCString::Strifind(FCommandLine::Get(), TEXT("TorqueBuild=")) != nullptr;
		const FTorqueBuild Build = bCommandLineBuild ? TorqueFit::BuildFromCommandLine() : UTorqueBuildSubsystem::BuildFor(this);
		if (TorqueFit::ResolveModel(Build, Resolved, &Clashes))
		{
			for (const FTorqueResolvedPart& R : Resolved)
				if (bCommandLineBuild || UTorqueBuildSubsystem::RidesWith(this, R))   // what the shed took off stays off
					ToMake.Add({R.Name.ToString(), R.Group.ToString(), R.Mesh, R.Material, R.Delta, R.Unit});
			for (const TPair<FName, FName>& KV : Build.Fitted)
				UE_LOG(LogTemp, Log, TEXT("Torque fit: %s: %s"), *KV.Key.ToString(), *KV.Value.ToString());
			for (const FTorqueFitClash& C : Clashes)
				UE_LOG(LogTemp, Log, TEXT("Torque fit: %s hits %s at %s (%.0f mm)"), *C.A.ToString(), *C.B.ToString(), *C.Case, C.DepthMm);
		}
	}
	const TSharedPtr<FJsonObject>* PartsObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Assets->TryGetObjectField(TEXT("parts"), PartsObj) || !Spec->TryGetArrayField(TEXT("parts"), List)) return;
	if (ToMake.Num() == 0)
	{
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			const FString Name = P->GetStringField(TEXT("name"));
			const TSharedPtr<FJsonObject>* Where = nullptr;
			if (!(*PartsObj)->TryGetObjectField(Name, Where)) continue;
			ToMake.Add({Name, P->GetStringField(TEXT("group")), (*Where)->GetStringField(TEXT("mesh")),
				(*Where)->GetStringField(TEXT("material")), FTransform::Identity, NAME_None});
		}
	}
	FTransform LeverDelta[2] = {FTransform::Identity, FTransform::Identity};
	GripDelta[0] = GripDelta[1] = FTransform::Identity;
	int32 Made = 0;
	for (const FPartToMake& P : ToMake)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *P.MeshPath);
		if (!Mesh) continue;
		USceneComponent* Parent = Root;
		if (P.Group == TEXT("steer")) Parent = SteerGroup;
		else if (P.Group == TEXT("front_lower")) Parent = FrontHub;
		else if (P.Group == TEXT("front_wheel")) Parent = FrontPivot;
		else if (P.Group == TEXT("swingarm")) Parent = SwingGroup;
		else if (P.Group == TEXT("rear_wheel")) Parent = RearPivot;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *(TEXT("Part_") + P.Name));
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetupAttachment(Parent);
		C->SetRelativeTransform(Fix * P.Delta);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *P.MaterialPath))
			C->SetMaterial(0, M);
		C->ComponentTags.Add(FName(*(TEXT("Part:") + P.Name)));   // for the shed's dirt and paint (ApplyLooks)
		C->RegisterComponent();
		ModelParts.Add(C);
		// By unit, so option levers and grips (one mesh each, modelled on the stock pivots) work too.
		const FString UnitName = P.Unit.IsNone() ? P.Name : P.Unit.ToString();
		if (UnitName == TEXT("lever_clutch")) { LeverParts[0] = C; LeverDelta[0] = P.Delta; }
		else if (UnitName == TEXT("lever_brake")) { LeverParts[1] = C; LeverDelta[1] = P.Delta; }
		else if (UnitName == TEXT("grip_l") || UnitName == TEXT("grips_l")) GripDelta[0] = P.Delta;
		else if (UnitName == TEXT("grip_r") || UnitName == TEXT("grips_r")) GripDelta[1] = P.Delta;
		++Made;
	}
	// The dirt and paint the bike has in the shed (washed or not, resprayed or not) ride out with it.
	TorqueShedModel::ApplyLooks(this, ModelParts);
	// The clutch and front brake levers swing on their perches as they're pulled.
	PartFix = Fix;
	for (int32 S = 0; S < 2; ++S) LeverBase[S] = Fix * LeverDelta[S];
	const TSharedPtr<FJsonObject>* Levers = nullptr;
	if (Spec->TryGetObjectField(TEXT("levers"), Levers))
	{
		for (int32 S = 0; S < 2; ++S)
		{
			const TSharedPtr<FJsonObject>* L = nullptr;
			if (!(*Levers)->TryGetObjectField(S == 0 ? TEXT("clutch") : TEXT("brake"), L)) continue;
			// On fitted bars a lever sits where its socket put it: its perch moves with it.
			LeverPivot[S] = LeverDelta[S].TransformPosition(ModelToLocal(JsonVec(*L, TEXT("pivot"))));
			LeverAxis[S] = LeverDelta[S].TransformVectorNoScale(ModelDirToLocal(JsonVec(*L, TEXT("axis"))));
			LeverPullRad[S] = FMath::DegreesToRadians((*L)->GetNumberField(TEXT("pull_deg")));
			LeverBase[S] = Fix * LeverDelta[S];
		}
	}
	if (Made > 0)
	{
		bHaveModel = true;
		for (UStaticMeshComponent* Shape : {Body, Tank, Rider, FrontWheel, RearWheel}) Shape->SetVisibility(false);
		UE_LOG(LogTemp, Log, TEXT("Torque: Speed Twin model, %d parts"), Made);
	}
}

void ATorqueBikePawn::LoadRoute()
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), RouteFile);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTemp, Error, TEXT("Torque: no route at %s (run Tools/export_buttertubs.py)"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
	if (Json->TryGetArrayField(TEXT("line_sim"), Pts))
	{
		for (const TSharedPtr<FJsonValue>& V : *Pts)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
			if (P.Num() >= 3) Line.Add(FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: route with %d points"), Line.Num());

	// Cattle grids (Tools/buttertubs_detail.py): the wheels ride over their bars (a jolt, less grip)
	// and the sound drums over them. Each lies across the road where the line passes it.
	double RoadHalf = 2.5;
	Json->TryGetNumberField(TEXT("road_half_width_m"), RoadHalf);
	Json->TryGetNumberField(TEXT("length_m"), RouteLengthM);
	const TArray<TSharedPtr<FJsonValue>>* Grids = nullptr;
	Sim->Grids.clear();
	if (Json->TryGetArrayField(TEXT("cattle_grids"), Grids) && Line.Num() >= 2)
	{
		for (const TSharedPtr<FJsonValue>& V : *Grids)
		{
			const TSharedPtr<FJsonObject>* G = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (!V->TryGetObject(G) || !(*G)->TryGetArrayField(TEXT("sim"), P) || P->Num() < 3) continue;
			const FVector C((*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber());
			int32 K = 0;
			double Best = TNumericLimits<double>::Max();
			for (int32 I = 0; I < Line.Num(); ++I)
			{
				const double D = FVector::DistSquared2D(FVector(Line[I].X, Line[I].Z, 0.0), FVector(C.X, C.Z, 0.0));
				if (D < Best) { Best = D; K = I; }
			}
			const FVector A = Line[FMath::Max(K - 1, 0)], B = Line[FMath::Min(K + 1, Line.Num() - 1)];
			V3 Along(B.X - A.X, 0.0, B.Z - A.Z);
			if (Along.Length() < 1e-3) continue;
			CattleGrid Grid;
			Grid.Centre = V3(C.X, C.Y, C.Z);
			Grid.Along = Along.Normalized();
			Grid.HalfWidth = RoadHalf + 0.15;
			Sim->Grids.push_back(Grid);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: %d cattle grids"), (int32)Sim->Grids.size());
}

void ATorqueBikePawn::Respawn(int32 LineIndex, double SideM)
{
	V3 Pos(0, 0, 0);
	double Heading = 0.0;
	if (Line.Num() >= 2)
	{
		const int32 I = FMath::Clamp(LineIndex, 0, Line.Num() - 2);
		const FVector A = Line[I], B = Line[I + 1];
		Pos = V3(A.X, A.Y, A.Z);
		Heading = std::atan2(-(B.X - A.X), -(B.Z - A.Z));   // 0 faces -z, + turns left
		Near = I;
		if (SideM != 0.0)
		{
			// To the right of the way along (x east, z south: right of (fx, fz) is (-fz, fx)).
			const FVector2D Fwd = FVector2D(B.X - A.X, B.Z - A.Z).GetSafeNormal();
			Pos = Pos + V3(-Fwd.Y * SideM, 0.0, Fwd.X * SideM);
		}
	}
	// Find the ground under the spawn point.
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueSpawn), true, this);
		const FVector Top = ToUE(Pos + V3(0, 50, 0)), Bottom = ToUE(Pos - V3(0, 50, 0));
		FHitResult First;   // what a plain trace hits first, in the log for checking the level
		if (World->LineTraceSingleByChannel(First, Top, Bottom, ECC_Visibility, Params))
			UE_LOG(LogTemp, Log, TEXT("Torque: respawn trace first hits %s at z %.0f cm (start penetrating %d)"),
				First.GetActor() ? *First.GetActor()->GetName() : TEXT("nothing"), First.ImpactPoint.Z, First.bStartPenetrating ? 1 : 0);
		if (GroundTrace(World, Top, Bottom, Params, Hit)) Pos = ToSim(Hit.ImpactPoint);
	}
	Sim->Place(Pos, Heading);
	Sim->Assist = Assist;
	Throttle = FrontBrake = RearBrake = Lean = Shift = 0.0;
	CrashedFor = 0.0;
	BodyLean = LongAcc = LastSpeed = 0.0;
	Remount();
	CurPos = PrevPos = ToUE(Sim->Position);
	CurRot = PrevRot = BasisToUE(Sim->Basis);
	Accum = 0.0;
	bCamInit = false;
	LastGear = Sim->Gear;   // no clunk for being put back in first
	SetActorLocationAndRotation(CurPos, CurRot);
}

void ATorqueBikePawn::Remount()
{
	if (!bThrown) return;
	// Back on the bike.
	bThrown = false;
	RiderRoot->SetUsingAbsoluteLocation(false);
	RiderRoot->SetUsingAbsoluteRotation(false);
	RiderRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
}

void ATorqueBikePawn::PlaceForSlot(int32 InSlot)
{
	if (InSlot <= 0 || Line.Num() < 2) return;
	// Two abreast either side of the line, a row every 7 m back from the start; at the very start of
	// the road, where there's no room behind, the rows go forward instead.
	const int32 Row = (InSlot + 1) / 2;
	const double SideM = (InSlot % 2 == 1) ? -1.2 : 1.2;
	const double Want = 7.0 * Row;
	auto Walk = [this](int32 From, int32 Dir, double Dist) {
		int32 I = From;
		double Acc = 0.0;
		while (Acc < Dist && I + Dir >= 0 && I + Dir < Line.Num() - 1)
		{
			Acc += FVector2D(Line[I + Dir].X - Line[I].X, Line[I + Dir].Z - Line[I].Z).Size();
			I += Dir;
		}
		return TPair<int32, double>(I, Acc);
	};
	TPair<int32, double> Back = Walk(StartIndex, -1, Want);
	if (Back.Value < Want - 0.5) Back = Walk(StartIndex, 1, Want);
	Respawn(Back.Key, SideM);
	UE_LOG(LogTemp, Log, TEXT("Torque net: start slot %d, line point %d, %.1f m to the side"), InSlot, Back.Key, SideM);
}

void ATorqueBikePawn::ReadInput(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
	if (!Pad) Pad = MakeUnique<FSonyPad>();   // our own bike: read the PlayStation pad
	// Hands-off test runs (-TorqueNoInput) ignore the pad and keyboard, so nobody playing can change them.
	static const bool bNoInput = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"));
	// -TorqueAutoRide (with -TorqueNoInput): a simple robot rides the route at -TorqueAutoMph (default
	// 36), the same pure-pursuit rider as SimTest, for checking pop-in and frame time in motion.
	static const bool bAuto = FParse::Param(FCommandLine::Get(), TEXT("TorqueAutoRide"));
	if (bAuto && Line.Num() > 2 && !Sim->IsCrashed)
	{
		static float AutoMph = 36.f;
		static const bool bMph = FParse::Value(FCommandLine::Get(), TEXT("TorqueAutoMph="), AutoMph);
		(void)bMph;
		const double Want = AutoMph / 2.23694;
		const V3 P = Sim->Position;
		int32 Ahead = Near;
		double Acc = 0.0;
		const double Look = 6.0 + 0.6 * FMath::Abs(Sim->Speed);
		while (Ahead + 1 < Line.Num() && Acc < Look)
		{
			Acc += FMath::Sqrt(FMath::Square(Line[Ahead + 1].X - Line[Ahead].X) + FMath::Square(Line[Ahead + 1].Z - Line[Ahead].Z));
			++Ahead;
		}
		const double Tx = Line[Ahead].X - P.x, Tz = Line[Ahead].Z - P.z;
		const V3 Fwd = Sim->Basis.Z * -1.0;
		double Err = FMath::Atan2(-Tx, -Tz) - FMath::Atan2(-Fwd.x, -Fwd.z);
		while (Err > PI) Err -= 2.0 * PI;
		while (Err < -PI) Err += 2.0 * PI;
		const double Dist = FMath::Max(FMath::Sqrt(Tx * Tx + Tz * Tz), 1.0);
		const double Curv = 2.0 * FMath::Sin(Err) / Dist;
		const double LeanWant = FMath::Atan(Sim->Speed * Sim->Speed * Curv / 9.81);
		Lean = FMath::Clamp(-LeanWant / 0.55, -1.0, 1.0);
		double Sharp = 0.0;
		Acc = 0.0;
		for (int32 K = Near + 1; K + 1 < Line.Num() && Acc < 60.0; ++K)
		{
			const double Ax = Line[K].X - Line[K - 1].X, Az = Line[K].Z - Line[K - 1].Z;
			const double Bx = Line[K + 1].X - Line[K].X, Bz = Line[K + 1].Z - Line[K].Z;
			const double La = FMath::Sqrt(Ax * Ax + Az * Az), Lb = FMath::Sqrt(Bx * Bx + Bz * Bz);
			Acc += La;
			if (La < 0.5 || Lb < 0.5) continue;
			const double Turn = FMath::Acos(FMath::Clamp((Ax * Bx + Az * Bz) / (La * Lb), -1.0, 1.0));
			Sharp = FMath::Max(Sharp, Turn / (0.5 * (La + Lb)));
		}
		const double Corner = Sharp > 1e-3 ? FMath::Sqrt(9.81 * FMath::Tan(0.5) / Sharp) : 99.0;
		const double Gap = FMath::Min(Want, Corner) - Sim->Speed;
		Throttle = FMath::Clamp(Gap * 0.4, 0.0, 1.0);
		FrontBrake = FMath::Clamp(-Gap * 0.25, 0.0, 0.8);
		RearBrake = FrontBrake * 0.5;
		Shift = 0.0;
		bManual = false;
		return;
	}
	if (bNoInput) return;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K) ? 1.0 : 0.0; };
	auto Axis = [PC](const FKey& K) { return (double)PC->GetInputAnalogKeyState(K); };

	// Pad: right trigger throttle, left trigger front brake, left bumper rear brake, left stick lean,
	// left stick up and down to move your weight. Keys: W throttle, S front brake, Space rear brake,
	// A and D lean, Q and E weight back and forward.
	// Sticks get a dead zone (rescaled so the rest of the travel is all used) and a gentle curve, so
	// small corrections near the centre are fine and a full push is still full lean.
	auto Stick = [](double V, double Dead, double Curve) {
		const double A = FMath::Abs(V);
		if (A < Dead) return 0.0;
		return FMath::Sign(V) * FMath::Pow((A - Dead) / (1.0 - Dead), Curve);
	};
	// An Xbox pad through Unreal, and a PlayStation pad read directly: whichever is pushed further.
	const FSonyPadState Sony = Pad ? Pad->Get() : FSonyPadState();
	auto Either = [](double A, double B) { return FMath::Abs(A) >= FMath::Abs(B) ? A : B; };
	// Triggers count only once they've moved from where they sat when the game started: a pad or a
	// driver that reports a resting trigger as half pressed (or a stray virtual pad) mustn't ride off
	// on its own.
	const double RawTrig[4] = {Axis(EKeys::Gamepad_RightTriggerAxis), Axis(EKeys::Gamepad_LeftTriggerAxis),
		(double)Sony.R2, (double)Sony.L2};
	double Trig[4];
	for (int32 I = 0; I < 4; ++I)
	{
		if (!bTrigSeen[I]) { TrigRest[I] = RawTrig[I]; bTrigSeen[I] = true; }
		if (!bTrigLive[I] && FMath::Abs(RawTrig[I] - TrigRest[I]) > 0.2) bTrigLive[I] = true;
		Trig[I] = bTrigLive[I] ? FMath::Clamp(RawTrig[I], 0.0, 1.0) : 0.0;
	}
	InputLogTimer += Dt;
	if (InputLogTimer > 1.0 && InputLogCount < 15)
	{
		InputLogTimer = 0.0;
		++InputLogCount;
		FString RawHex;
		for (int32 B = 0; B < 12; ++B) RawHex += FString::Printf(TEXT("%02X "), Sony.Raw[B]);
		UE_LOG(LogTemp, Log, TEXT("Torque: input xR2 %.2f xL2 %.2f psR2 %.2f psL2 %.2f live %d%d%d%d  throttle %.2f speed %.1f  raw(%d) %s"),
			RawTrig[0], RawTrig[1], RawTrig[2], RawTrig[3], bTrigLive[0], bTrigLive[1], bTrigLive[2], bTrigLive[3],
			Throttle, Sim->Speed, Sony.RawLen, *RawHex);
	}
	const double PadThrottle = FMath::Max(Trig[0], Trig[2]);
	const double PadBrake = FMath::Max(Trig[1], Trig[3]);
	const FTorqueSettings& Set = FTorqueSettings::Get();   // Settings > Controller
	const double PadLean = FMath::Clamp(Stick(Either(Axis(EKeys::Gamepad_LeftX), Sony.LX), Set.StickDeadZone, 1.3) * Set.Steering, -1.0, 1.0);
	// Weight is the left stick up and down, with a wide dead zone so leaning doesn't move it.
	const double PadShift = Stick(Either(Axis(EKeys::Gamepad_LeftY), Sony.LY), 0.4, 1.0);
	// Right stick looks around (eased back to the front when let go).
	const double LookX = FMath::Clamp(Stick(Either(Axis(EKeys::Gamepad_RightX), Sony.RX), 0.12, 1.0) * Set.PadLook, -1.0, 1.0);
	const double LookY = FMath::Clamp(Stick(Either(Axis(EKeys::Gamepad_RightY), Sony.RY), 0.12, 1.0) * Set.PadLook, -1.0, 1.0) *
						 (Set.bInvertLook ? -1.0 : 1.0);
	const uint32 SonyDown = Sony.Buttons;
	const uint32 SonyPressed = SonyDown & ~PrevPadButtons;
	PrevPadButtons = SonyDown;
	auto SonyHit = [SonyPressed](uint32 B) { return (SonyPressed & B) != 0; };
	// The pause or settings menu is up: let go of the controls (the menu has the keys).
	if (ATorquePlayerController::MenuUp(PC))
	{
		Throttle = FrontBrake = RearBrake = Shift = 0.0;
		Lean = Approach(Lean, 0.0, 2.2, 4.0, Dt);
		return;
	}
	const double LookK = 1.0 - FMath::Exp(-8.0 * Dt);
	LookYaw = FMath::Lerp(LookYaw, LookX * 130.0, LookK);
	LookPitch = FMath::Lerp(LookPitch, LookY * 25.0, LookK);

	const double KeyThrottle = FMath::Max(Down(EKeys::W), Down(EKeys::Up));
	const double KeyBrake = FMath::Max(Down(EKeys::S), Down(EKeys::Down));
	const double KeyLean = FMath::Clamp((FMath::Max(Down(EKeys::D), Down(EKeys::Right)) - FMath::Max(Down(EKeys::A), Down(EKeys::Left))) * Set.Steering, -1.0, 1.0);
	const double KeyShift = Down(EKeys::E) - Down(EKeys::Q);

	// Keys are all or nothing, so ease them in (as Torque Shed does); the pad is used as it is.
	Throttle = PadThrottle > 0.02 ? PadThrottle : Approach(Throttle, KeyThrottle, 2.5, 6.0, Dt);
	FrontBrake = PadBrake > 0.02 ? PadBrake : Approach(FrontBrake, KeyBrake, 3.0, 8.0, Dt);
	RearBrake = FMath::Max(Down(EKeys::SpaceBar), Down(EKeys::Gamepad_LeftShoulder));
	if (SonyDown & ESonyButton::L1) RearBrake = 1.0;

	// Gears: Cross/A up, Square/X down (X and Z, or Left Shift and Left Ctrl, on keys). The first
	// change by hand turns the automatic box off; R3 or G turns it back on.
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	const bool bUp = SonyHit(ESonyButton::Cross) || Just(EKeys::Gamepad_FaceButton_Bottom) || Just(EKeys::X) || Just(EKeys::LeftShift);
	const bool bDown = SonyHit(ESonyButton::Square) || Just(EKeys::Gamepad_FaceButton_Left) || Just(EKeys::Z) || Just(EKeys::LeftControl);
	if (bUp || bDown)
	{
		bManual = true;
		Sim->AutoShift = false;
		if (bUp) Sim->ShiftUp();
		else Sim->ShiftDown();
	}
	if (SonyHit(ESonyButton::R3) || Just(EKeys::Gamepad_RightThumbstick) || Just(EKeys::G))
	{
		bManual = !bManual;
		GearNoteFor = 2.0;
	}
	// ABS: Circle/B or the B key. Off, the rear brake will lock the wheel.
	if (SonyHit(ESonyButton::Circle) || Just(EKeys::Gamepad_FaceButton_Right) || Just(EKeys::B))
	{
		bAbs = !bAbs;
		Sim->Build.AbsFront = Sim->Build.AbsRear = bAbs;
		GearNoteFor = 2.0;
	}
	Lean = FMath::Abs(PadLean) > 0.08 ? PadLean : Approach(Lean, KeyLean, 2.2, 4.0, Dt);
	Shift = FMath::Abs(PadShift) > 0.15 ? PadShift : Approach(Shift, KeyShift, 3.0, 3.0, Dt);

	if (PC->WasInputKeyJustPressed(EKeys::R) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top) ||
		SonyHit(ESonyButton::Triangle))
		Respawn(FMath::Max(Near - 2, 0));
	// Options / Start open the pause menu now (ATorquePlayerController), which has Restart the ride.
	if (PC->WasInputKeyJustPressed(EKeys::Home)) RestartRide();
	if (PC->WasInputKeyJustPressed(EKeys::C) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) ||
		PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left) || SonyHit(ESonyButton::R1 | ESonyButton::Create))
	{
		CameraMode = (CameraMode + 1) % CameraModes;
		bCamInit = false;
	}
	if (SonyHit(ESonyButton::DDown)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (SonyHit(ESonyButton::DUp)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::H)) bShowHelp = !bShowHelp;
	// Backspace: back to the shed (when its map has been built).
	if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && TorqueFlow::BackToShed(this)) return;
	if (PC->WasInputKeyJustPressed(EKeys::V) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
	{
		// The host's (or a solo rider's) to change; Tick picks it up from the game state.
		ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		if (GS && HasAuthority()) GS->SetWeather(GS->GetWeather() + 1);
		else if (GS) HostOnlyNoteFor = 3.0;
	}
	if (PC->WasInputKeyJustPressed(EKeys::P) || SonyHit(ESonyButton::L3) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick))
		bPerfShow = !bPerfShow;
	if (PC->WasInputKeyJustPressed(EKeys::O))
	{
		bPerfLog = !bPerfLog;
		if (!bPerfLog) PerfFile.Reset();
	}
	// Esc, Options and Start open the pause menu (ATorquePlayerController), which has Quit.
	if (PC->WasInputKeyJustPressed(EKeys::LeftBracket)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::RightBracket)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
}

void ATorqueBikePawn::RestartRide()
{
	RideTime = 0.0;
	bFinished = false;
	Respawn(0);
}

void ATorqueBikePawn::StepSim()
{
	Sim->Throttle = Throttle;
	Sim->FrontBrake = FrontBrake;
	Sim->RearBrake = RearBrake;
	Sim->LeanInput = Lean;
	Sim->BodyShift = Shift;
	Sim->Assist = Assist;
	Sim->AutoShift = !bManual;
	PrevPos = CurPos;
	PrevRot = CurRot;
	Sim->Step(SimDt);
	CurPos = ToUE(Sim->Position);
	CurRot = BasisToUE(Sim->Basis);
}

void ATorqueBikePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// In co-op, a bike someone else rides: drawn from their snapshots, not simulated here.
	const bool bRemote = NetBike && NetBike->IsRemote();
	if (const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr)
	{
		if (GS->GetWeather() != Weather)
		{
			Weather = GS->GetWeather();
			ApplyWeather();
		}
	}
	HostOnlyNoteFor = FMath::Max(HostOnlyNoteFor - DeltaSeconds, 0.0);

	if (bRemote)
	{
		NetBike->ApplyRemote(DeltaSeconds);
		if (!Sim->IsCrashed) Remount();   // they're back on the road
	}
	else
	{
		ReadInput(DeltaSeconds);

		// Fixed 120 Hz steps, at most a tenth of a second's worth after a hitch.
		Accum += FMath::Min((double)DeltaSeconds, 0.1);
		while (Accum >= SimDt)
		{
			StepSim();
			Accum -= SimDt;
		}

		if (Sim->IsCrashed)
		{
			CrashedFor += DeltaSeconds;
			if (CrashedFor > 2.5) Respawn(FMath::Max(Near - 2, 0));
		}
		else if (!bFinished)
		{
			RideTime += DeltaSeconds;
		}
	}

	{
		// The rider feels the lean and the speeding up and slowing down a moment after the bike does.
		const double Dt = FMath::Max((double)DeltaSeconds, 1e-3);
		const double Acc = FMath::Clamp((Sim->Speed - LastSpeed) / Dt, -15.0, 15.0);
		LastSpeed = Sim->Speed;
		LongAcc += (Acc - LongAcc) * FMath::Min(1.0, Dt / 0.25);
		BodyLean += (FMath::RadiansToDegrees(Sim->LeanAngle) - BodyLean) * FMath::Min(1.0, Dt / 0.15);
	}
	if (Sim->Gear != LastGear) SinceShift = 0.0;   // the rider's left hand pulls the clutch
	SinceShift += DeltaSeconds;
	{
		// Front brake: two fingers go over the lever as soon as you brake, and come off after.
		const double Want = FrontBrake > 0.03 ? 1.0 : 0.0;
		BrakeFingers = MoveToward(BrakeFingers, Want, (Want > BrakeFingers ? 10.0 : 4.0) * DeltaSeconds);
	}
	if (Sound && Sim->Gear != LastGear) Sound->GearChange(Sim->Gear > LastGear);
	LastGear = Sim->Gear;
	GearNoteFor = FMath::Max(GearNoteFor - DeltaSeconds, 0.0);

	UpdateVisuals(bRemote ? 1.0 : Accum / SimDt);
	ThrowRider(DeltaSeconds);
	UpdateRider();
	UpdateClock();
	if (!bRemote)
	{
		UpdateProgress();
		UpdateCamera(DeltaSeconds);
	}
	UpdateSound(DeltaSeconds);
	if (!bRemote)
	{
		UpdatePerf(DeltaSeconds);
		DrawHud();
	}
	if (NetBike) NetBike->AfterTick(DeltaSeconds);
}

void ATorqueBikePawn::UpdateVisuals(double Alpha)
{
	// Draw between the last two steps so the bike moves smoothly at any frame rate.
	SetActorLocationAndRotation(FMath::Lerp(PrevPos, CurPos, Alpha), FQuat::Slerp(PrevRot, CurRot, Alpha));

	const Wheel& F = Sim->Wheels[0];
	const Wheel& R = Sim->Wheels[1];
	// Wheels roll about their axle (local y); forward rolling turns the top forward, a negative pitch.
	const double Rake = FMath::DegreesToRadians(Sim->Build.RakeDeg);
	const FVector SteerAxis(-FMath::Sin(Rake), 0.0, FMath::Cos(Rake));   // tilted back by the rake
	const FQuat SteerQ(bHaveModel ? SteerDir : SteerAxis, Sim->Steer);
	// A turn about +y by a positive angle carries the top of the wheel forwards: rolling ahead.
	FrontPivot->SetRelativeLocation(BodyToLocal(F.Centre));
	FrontPivot->SetRelativeRotation(SteerQ * FQuat(FVector::RightVector, F.Spin));
	RearPivot->SetRelativeLocation(BodyToLocal(R.Centre));
	RearPivot->SetRelativeRotation(FQuat(FVector::RightVector, R.Spin));
	if (!bHaveModel) return;

	// Bars, yokes and lamp turn about the steering axis; the fork lowers follow the axle.
	const FQuat Turn(SteerDir, Sim->Steer);
	SteerGroup->SetRelativeLocationAndRotation(SteerPoint - Turn.RotateVector(SteerPoint), Turn);
	FrontHub->SetRelativeLocationAndRotation(BodyToLocal(F.Centre), SteerQ);
	// The swingarm swings about its pivot to point at the rear axle.
	const FVector Rest = RearRest - SwingPivot, Now = BodyToLocal(R.Centre) - SwingPivot;
	const double Swing = FMath::Atan2(Now.Z, Now.X) - FMath::Atan2(Rest.Z, Rest.X);
	SwingGroup->SetRelativeLocationAndRotation(SwingPivot, FQuat(FVector::RightVector, -Swing));
}

namespace
{
// The middle joint of a two-bone limb from Root to Tip (lengths A then B), bending towards Pole.
FVector MidJoint(const FVector& Root, const FVector& Tip, double A, double B, const FVector& Pole)
{
	FVector D = Tip - Root;
	const double Len = FMath::Clamp(D.Size(), FMath::Abs(A - B) + 0.1, (A + B) * 0.999);
	D = D.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const double Along = (A * A - B * B + Len * Len) / (2.0 * Len);
	const double Out = FMath::Sqrt(FMath::Max(A * A - Along * Along, 0.0));
	FVector Side = Pole - D * FVector::DotProduct(Pole, D);
	Side = Side.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	return Root + D * Along + Side * Out;
}

// A basic-shapes cylinder (100 cm tall, 100 across, centred) from P to Q, Width x Depth cm across.
void PlaceLimb(UStaticMeshComponent* C, const FVector& P, const FVector& Q, double Width, double Depth = -1.0)
{
	FVector D = Q - P;
	const double L = FMath::Max(D.Size(), 0.1);
	const FRotator Rot = FRotationMatrix::MakeFromZX(D.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector), FVector::ForwardVector).Rotator();
	C->SetRelativeLocationAndRotation((P + Q) * 0.5, Rot);
	C->SetRelativeScale3D(FVector((Depth > 0 ? Depth : Width) / 100.0, Width / 100.0, L / 100.0));
}

void PlaceBlob(UStaticMeshComponent* C, const FVector& Centre, const FVector& SizeCm, const FRotator& Rot = FRotator::ZeroRotator)
{
	C->SetRelativeLocationAndRotation(Centre, Rot);
	C->SetRelativeScale3D(SizeCm / 100.0);
}
} // namespace

bool ATorqueBikePawn::BuildRiderRig()
{
	// The level script imports Data/Rider and writes where the meshes and kit materials went.
	TSharedPtr<FJsonObject> Assets;
	const TSharedPtr<FJsonObject>* Rig = nullptr;
	if (!LoadJson(TEXT("Data/Bike/SpeedTwin/ue_assets.json"), Assets) || !Assets->TryGetObjectField(TEXT("rider_rig"), Rig))
		return false;
	double Yaw = 0.0, Mirror = 1.0, Scale = 1.0;
	const TSharedPtr<FJsonObject>* Frame = nullptr;
	if (Assets->TryGetObjectField(TEXT("frame"), Frame))
	{
		Yaw = (*Frame)->GetNumberField(TEXT("yaw"));
		Mirror = (*Frame)->GetNumberField(TEXT("mirror"));
		Scale = (*Frame)->GetNumberField(TEXT("scale"));
	}
	const FTransform Fix(FRotator(0.0, Yaw, 0.0), FVector::ZeroVector, FVector(Scale, Scale * Mirror, Scale));
	TMap<FString, UMaterialInterface*> Mats;
	const TSharedPtr<FJsonObject>* MatObj = nullptr;
	if ((*Rig)->TryGetObjectField(TEXT("materials"), MatObj))
		for (const auto& KV : (*MatObj)->Values)
			Mats.Add(FString(KV.Key), LoadObject<UMaterialInterface>(nullptr, *KV.Value->AsString()));
	if (!RiderRig.Build(this, RiderRoot, (*Rig)->GetStringField(TEXT("mesh")), (*Rig)->GetStringField(TEXT("helmet")), Mats, Fix))
		return false;
	RiderBody = RiderRig.Body;
	RiderHelmet = RiderRig.Helmet;
	UE_LOG(LogTemp, Log, TEXT("Torque: rider in kit (skinned)"));
	return true;
}

void ATorqueBikePawn::BuildRider(UMaterialInterface* const* Mats)
{
	Rider->SetVisibility(false);   // the old placeholder
	if (BuildRiderRig())
	{
		UpdateRider();
	}
	else
	{
		struct FKind { UStaticMesh* Mesh; int32 Mat; };
		const FKind Cyl[2] = {{ShapeCylinder, RM_Jacket}, {ShapeCylinder, RM_Jeans}};
		FKind Kinds[RP_Count];
		Kinds[RP_Torso] = {ShapeCylinder, RM_Jacket};
		Kinds[RP_Pelvis] = {ShapeSphere, RM_Jeans};
		Kinds[RP_Shoulders] = {ShapeCylinder, RM_Jacket};
		Kinds[RP_Neck] = {ShapeCylinder, RM_Jacket};
		Kinds[RP_Helmet] = {ShapeSphere, RM_Helmet};
		Kinds[RP_Visor] = {ShapeSphere, RM_Visor};
		for (int32 S = 0; S < 2; ++S)
		{
			Kinds[RP_UpperArmL + S] = Cyl[0];
			Kinds[RP_ForearmL + S] = Cyl[0];
			Kinds[RP_ElbowL + S] = {ShapeSphere, RM_Jacket};
			Kinds[RP_HandL + S] = {ShapeSphere, RM_Gloves};
			Kinds[RP_ThighL + S] = Cyl[1];
			Kinds[RP_ShinL + S] = Cyl[1];
			Kinds[RP_KneeL + S] = {ShapeSphere, RM_Jeans};
			Kinds[RP_BootL + S] = {ShapeCube, RM_Boots};
		}
		for (int32 I = 0; I < RP_Count; ++I)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("RiderPart%d"), I));
			C->SetStaticMesh(Kinds[I].Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			C->bCastHiddenShadow = true;   // the head still throws its shadow when the camera's inside it
			C->SetupAttachment(Root);
			if (Mats && Mats[Kinds[I].Mat]) C->SetMaterial(0, Mats[Kinds[I].Mat]);
			C->RegisterComponent();
			RiderParts.Add(C);
		}
		UpdateRider();
	}

	if (!bHaveModel) return;
	for (int32 I = 0; I < CP_Ticks + ClockTicks; ++I)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("ClockPart%d"), I));
		C->SetStaticMesh(I == CP_Dial ? ShapeCylinder : ShapeCube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = false;
		C->SetupAttachment(SteerGroup);
		const int32 Mat = I == CP_Dial ? RM_Dial : I == CP_Revs ? RM_RevNeedle : RM_Needle;
		if (Mats && Mats[Mat]) C->SetMaterial(0, Mats[Mat]);
		C->RegisterComponent();
		ClockParts.Add(C);
	}
	UpdateClock();
}

namespace
{
// The Speed Twin's clock (Tools/make_speedtwin.py): face centre and normal, pawn local cm, bars straight.
const FVector ClockCentre(43.26, 0.0, 73.59);
const FVector ClockNormal = FVector(-0.503, 0.0, 0.864).GetSafeNormal();
const FVector ClockTop = FVector::CrossProduct(FVector(0.0, 1.0, 0.0), ClockNormal).GetSafeNormal();   // forward and up
// A point on the dial: Angle clockwise from the top as the rider sees it, R from the centre (cm).
FVector OnDial(double Angle, double R, const FVector& Centre)
{
	return Centre + (ClockTop * FMath::Cos(Angle) + FVector(0.0, 1.0, 0.0) * FMath::Sin(Angle)) * R;
}
void PlaceNeedle(UStaticMeshComponent* C, const FVector& Hub, double Angle, double Len, double Width, double Lift)
{
	const FVector D = ClockTop * FMath::Cos(Angle) + FVector(0.0, 1.0, 0.0) * FMath::Sin(Angle);
	C->SetRelativeLocationAndRotation(Hub + ClockNormal * Lift + D * (Len * 0.5 - 0.4),
		FRotationMatrix::MakeFromXZ(D, ClockNormal).Rotator());
	C->SetRelativeScale3D(FVector(Len, Width, 0.12) / 100.0);
}
} // namespace

void ATorqueBikePawn::UpdateClock()
{
	if (ClockParts.Num() < CP_Ticks + ClockTicks) return;
	// Speed: 0 to 140 mph round 270 degrees from bottom left; ticks every 10 mph.
	const double Sweep = FMath::DegreesToRadians(270.0), From = -Sweep * 0.5;
	ClockParts[CP_Dial]->SetRelativeLocationAndRotation(ClockCentre + ClockNormal * 0.15,
		FRotationMatrix::MakeFromZX(ClockNormal, ClockTop).Rotator());
	ClockParts[CP_Dial]->SetRelativeScale3D(FVector(9.3, 9.3, 0.2) / 100.0);
	for (int32 T = 0; T < ClockTicks; ++T)
	{
		const double A = From + Sweep * T / (ClockTicks - 1);
		const bool Big = T % 2 == 0;
		PlaceNeedle(ClockParts[CP_Ticks + T], OnDial(A, Big ? 3.5 : 3.8, ClockCentre), A, Big ? 1.3 : 0.8, 0.22, 0.3);
	}
	const double Mph = FMath::Clamp(FMath::Abs(Sim->Speed) * 2.23694, 0.0, 140.0);
	PlaceNeedle(ClockParts[CP_Speed], ClockCentre, From + Sweep * Mph / 140.0, 3.9, 0.3, 0.45);
	// Revs on a small dial in the lower half: 0 to 9000 over 180 degrees.
	const FVector RevHub = ClockCentre - ClockTop * 2.0;
	const double Rpm = FMath::Clamp(Sim->Rpm, 0.0, 9000.0);
	PlaceNeedle(ClockParts[CP_Revs], RevHub, FMath::DegreesToRadians(-90.0 + 180.0 * Rpm / 9000.0), 1.6, 0.3, 0.55);
}

void ATorqueBikePawn::ShowHead(bool bShow)
{
	if (bShow == bHeadShown) return;
	if (RiderRig.IsValid())
	{
		bHeadShown = bShow;
		if (RiderHelmet) RiderHelmet->SetVisibility(bShow && bRiderShown);
		return;
	}
	if (RiderParts.Num() < RP_Count) return;
	bHeadShown = bShow;
	for (int32 I : {RP_Helmet, RP_Visor, RP_Neck}) RiderParts[I]->SetVisibility(bShow);
}

void ATorqueBikePawn::ShowRider(bool bShow)
{
	if (bShow == bRiderShown) return;
	if (RiderRig.IsValid())
	{
		bRiderShown = bShow;
		if (RiderBody) RiderBody->SetVisibility(bShow);
		if (RiderHelmet) RiderHelmet->SetVisibility(bShow && bHeadShown);
		return;
	}
	if (RiderParts.Num() < RP_Count) return;
	bRiderShown = bShow;
	for (UStaticMeshComponent* C : RiderParts) C->SetVisibility(bShow);
	if (bShow)
	{
		// Every part is showing again, the head too: put the head back as the view wants it.
		const bool bWantHead = bHeadShown;
		bHeadShown = true;
		ShowHead(bWantHead);
	}
}

void ATorqueBikePawn::ThrowRider(float Dt)
{
	if (!RiderRig.IsValid()) return;
	const FVector Pelvis0 = RiderRig.PelvisRest();
	if (Sim->IsCrashed && !bThrown)
	{
		// Let go of the bike where it is now, moving as it was, with a little hop up off the seat.
		bThrown = true;
		ThrownFor = 0.0;
		const FTransform At = RiderRoot->GetComponentTransform();
		ThrowRot0 = At.GetRotation();
		ThrowPelvis = At.TransformPosition(Pelvis0);
		ThrowVel = DirToUE(Sim->LinearVelocity) * 100.0 + FVector(0.0, 0.0, 150.0);
		// He comes to rest on his back, feet the way he was going (or the way the bike faced, slow).
		FVector Dir(ThrowVel.X, ThrowVel.Y, 0.0);
		if (Dir.SizeSquared() < 200.0 * 200.0) Dir = ThrowRot0.GetForwardVector() * FVector(1.0, 1.0, 0.0);
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		// Tipped back past upright so his back lies along the road (the chest is also pushed back).
		ThrowLie = FRotator(95.0, Yaw, 0.0).Quaternion();
		RiderRoot->SetUsingAbsoluteLocation(true);
		RiderRoot->SetUsingAbsoluteRotation(true);
	}
	if (!bThrown) return;

	ThrownFor += Dt;
	const double Step = FMath::Min((double)Dt, 0.05);
	ThrowVel.Z -= 981.0 * Step;
	ThrowPelvis += ThrowVel * Step;
	// The road or land under him: the pelvis rides 14 cm up (the seat of the jeans on the ground),
	// and once down he slides to a stop.
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueThrow), true, this);
		if (GroundTrace(World, ThrowPelvis + FVector(0, 0, 150.0), ThrowPelvis - FVector(0, 0, 300.0), Params, Hit))
		{
			const double Rest = Hit.ImpactPoint.Z + 14.0;
			if (ThrowPelvis.Z <= Rest)
			{
				ThrowPelvis.Z = Rest;
				ThrowVel.Z = FMath::Max(ThrowVel.Z, 0.0);
				FVector Flat(ThrowVel.X, ThrowVel.Y, 0.0);
				const double Sp = Flat.Size(), Slow = 600.0 * Step;   // leather and denim on tarmac
				ThrowVel = Sp > Slow ? Flat * ((Sp - Slow) / Sp) : FVector::ZeroVector;
			}
		}
	}
	const double A = FMath::SmoothStep(0.0, 0.7, ThrownFor);
	const FQuat Q = FQuat::Slerp(ThrowRot0, ThrowLie, A);
	RiderRoot->SetWorldLocationAndRotation(ThrowPelvis - Q.RotateVector(Pelvis0), Q);
}

void ATorqueBikePawn::UpdateRider()
{
	if (RiderRig.IsValid())
	{
		// Pawn local cm: x forward, y right, z up, from the body origin (axle height, mid wheelbase).
		FRiderPoseInput In;
		const double Fwd = Shift;   // weight forward (+) or back (-)
		const double Hang = Lean;   // into the turn
		// Riding as people do: the upper body goes in a little further than the bike leans, and the
		// seat of the pants slides in; braking throws the chest forward and down onto the arms, and
		// pulling hard leaves it a little back.
		const double LeanN = FMath::Clamp(BodyLean / 45.0, -1.0, 1.0);
		const double Braking = FMath::Clamp(-LongAcc / 9.0, 0.0, 1.0), Pulling = FMath::Clamp(LongAcc / 5.0, 0.0, 1.0);
		In.ChestShift = FVector(7.0 * Fwd + 9.0 * Braking - 4.0 * Pulling, Hang * 6.0 + 9.0 * LeanN,
			-4.0 * FMath::Max(Fwd, 0.0) - 3.0 * Braking);
		In.HipShift = Hang * 2.0 + 2.5 * LeanN;
		In.BarPivot = bHaveModel ? SteerPoint : FVector(68.0, 0.0, -1.0);
		const FVector Axis = bHaveModel ? SteerDir : FVector(-FMath::Sin(FMath::DegreesToRadians(Sim->Build.RakeDeg)), 0.0,
			FMath::Cos(FMath::DegreesToRadians(Sim->Build.RakeDeg)));
		In.BarTurn = FQuat(Axis, Sim->Steer);
		// Feet go down as the bike stops: ankles a little forward of the pegs and out, on the road.
		In.FeetDown = FMath::Clamp(1.0 - FMath::Abs(Sim->Speed) / 1.8, 0.0, 1.0);
		In.FootDownL = FVector(-32.0, -36.0, -22.0);
		In.FootDownR = FVector(-32.0, 36.0, -22.0);
		// The head turns toward where the corner goes (the apex) and stays nearer level than the bike.
		// Unreal: + yaw looks right, + roll drops the right side.
		const double HeadLean = FMath::Clamp(BodyLean, -55.0, 55.0);
		In.Look = FRotator(LookPitch * -0.4 - 3.0 * Braking, LookYaw * 0.5 + 0.45 * HeadLean, -0.45 * HeadLean);
		// Right hand: the throttle, and the front brake lever under two fingers.
		In.Throttle = Throttle;
		if (bThrown)
		{
			// Off the bike: lying back, legs out along the road, arms out at his sides.
			const double Off = FMath::SmoothStep(0.0, 0.5, ThrownFor);
			In.ChestShift = FMath::Lerp(In.ChestShift, FVector(-20.0, 0.0, -5.0), Off);
			In.HipShift = 0.0;
			In.FeetDown = Off;
			In.FootDownL = FVector(-47.0, -25.0, -17.5);
			In.FootDownR = FVector(-47.0, 25.0, -17.5);
			In.Limp = Off;
			In.HandsOff[0] = FVector(-43.0, -50.0, 77.0);
			In.HandsOff[1] = FVector(-43.0, 50.0, 77.0);
			In.Look = FRotator::ZeroRotator;
			In.Throttle = 0.0;
		}
		In.LeverFingers[1] = BrakeFingers;
		In.LeverPull[1] = FMath::Clamp(FrontBrake, 0.0, 1.0) * BrakeFingers;
		// Left hand: the clutch, pulled for each gear change (fingers over, pull, let out), and held
		// in while stopped, let out as you pull away.
		{
			const double T = SinceShift;
			const double FingersShift = T < 0.06 ? T / 0.06 : T < 0.30 ? 1.0 : T < 0.42 ? 1.0 - (T - 0.30) / 0.12 : 0.0;
			const double PullShift = T < 0.03 ? 0.0 : T < 0.10 ? (T - 0.03) / 0.07 : T < 0.22 ? 1.0 : T < 0.34 ? 1.0 - (T - 0.22) / 0.12 : 0.0;
			const double Held = FMath::Clamp(1.0 - FMath::Abs(Sim->Speed) / 2.5, 0.0, 1.0);
			In.LeverFingers[0] = FMath::Max(FingersShift, FMath::Clamp(Held * 3.0, 0.0, 1.0));
			In.LeverPull[0] = FMath::Max(PullShift, Held);
		}
		for (int32 S = 0; S < 2; ++S)
		{
			if (!LeverParts[S]) continue;
			const FQuat Q(LeverAxis[S], LeverPullRad[S] * In.LeverPull[S]);
			LeverParts[S]->SetRelativeTransform(LeverBase[S] * FTransform(-LeverPivot[S]) * FTransform(Q, LeverPivot[S]));
		}
		In.GripFit[0] = GripDelta[0];
		In.GripFit[1] = GripDelta[1];
		RiderRig.Pose(In);
		HeadLocal = RiderRig.HelmetCentre;
		return;
	}
	if (RiderParts.Num() < RP_Count) return;
	// Pawn local cm: x forward, y right, z up, from the body origin (axle height, mid wheelbase).
	// The Speed Twin's seat is at about z 54, its grips at x 41, its rider's pegs at x -22, z 4.
	const double Fwd = Shift;                   // weight forward (+) or back (-)
	const double Hang = Lean;                   // into the turn
	const FVector Hip(-36.0, Hang * 2.0, 62.0);
	const FVector Chest(-10.0 + 7.0 * Fwd, Hang * 6.0, 104.0 - 4.0 * FMath::Max(Fwd, 0.0));
	const FVector Head = Chest + FVector(1.0, Hang * 1.5, 24.0);
	HeadLocal = Head;

	// Grips turn with the bars about the steering axis.
	const FVector Axis = bHaveModel ? SteerDir : FVector(-FMath::Sin(FMath::DegreesToRadians(Sim->Build.RakeDeg)), 0.0,
		FMath::Cos(FMath::DegreesToRadians(Sim->Build.RakeDeg)));
	const FVector Pivot = bHaveModel ? SteerPoint : FVector(68.0, 0.0, -1.0);
	const FQuat Turn(Axis, Sim->Steer);
	// Feet go down as the bike stops.
	const double Down = FMath::Clamp(1.0 - FMath::Abs(Sim->Speed) / 1.8, 0.0, 1.0);

	for (int32 S = 0; S < 2; ++S)
	{
		const double Y = S == 0 ? -1.0 : 1.0;   // left, right
		const FVector Shoulder = Chest + FVector(0.0, 19.0 * Y, 0.0);
		const FVector Grip = Pivot + Turn.RotateVector(FVector(41.3, 31.5 * Y, 65.5) - Pivot);
		const FVector Elbow = MidJoint(Shoulder, Grip, 34.0, 32.0, FVector(-0.3, 0.75 * Y, -0.6));
		// The sleeve stops at the wrist; the glove wraps the grip, knuckles up and a little forward.
		const FVector Wrist = Grip + (Elbow - Grip).GetSafeNormal() * 7.0 + FVector(0.0, 0.0, 2.0);
		PlaceLimb(RiderParts[RP_UpperArmL + S], Shoulder, Elbow, 12.0);
		PlaceLimb(RiderParts[RP_ForearmL + S], Elbow, Wrist, 9.5);
		PlaceBlob(RiderParts[RP_ElbowL + S], Elbow, FVector(12.0));
		const FVector Bar = Turn.RotateVector(FVector(0.0, 1.0, 0.0));
		PlaceBlob(RiderParts[RP_HandL + S], Grip + FVector(1.0, 0.0, 2.0), FVector(12.0, 10.5, 8.5),
			FRotationMatrix::MakeFromYZ(Bar, FVector::UpVector).Rotator());

		const FVector HipS = Hip + FVector(0.0, 11.0 * Y, -2.0);
		const FVector Ankle = FMath::Lerp(FVector(-25.0, 23.0 * Y, 12.0), FVector(-30.0, 36.0 * Y, -22.0), Down);
		const FVector Knee = MidJoint(HipS, Ankle, 44.0, 44.0, FVector(1.0, 0.35 * Y, 0.5));
		PlaceLimb(RiderParts[RP_ThighL + S], HipS, Knee, 16.0);
		PlaceLimb(RiderParts[RP_ShinL + S], Knee, Ankle, 12.0);
		PlaceBlob(RiderParts[RP_KneeL + S], Knee, FVector(15.0));
		PlaceBlob(RiderParts[RP_BootL + S], Ankle + FVector(8.0, 0.0, -6.0), FVector(28.0, 11.0, 13.0));
	}
	PlaceLimb(RiderParts[RP_Torso], Hip + FVector(0.0, 0.0, -2.0), Chest + FVector(0.0, 0.0, 6.0), 38.0, 26.0);
	PlaceBlob(RiderParts[RP_Pelvis], Hip + FVector(2.0, 0.0, -2.0), FVector(34.0, 36.0, 22.0));
	PlaceLimb(RiderParts[RP_Shoulders], Chest + FVector(0.0, -21.0, 0.0), Chest + FVector(0.0, 21.0, 0.0), 14.0);
	PlaceLimb(RiderParts[RP_Neck], Chest + FVector(0.0, 0.0, 4.0), Head + FVector(-1.0, 0.0, -8.0), 11.0);
	// The head turns a little where you look.
	const FRotator Look(LookPitch * -0.4, LookYaw * 0.5, 0.0);
	PlaceBlob(RiderParts[RP_Helmet], Head, FVector(29.0, 25.0, 28.0), Look);
	PlaceBlob(RiderParts[RP_Visor], Head + Look.RotateVector(FVector(9.5, 0.0, -0.5)), FVector(11.0, 21.0, 11.0), Look);
}

void ATorqueBikePawn::StartSound()
{
	Sound = MakeUnique<TorqueSound::RideSound>();
	Sound->IdleRpm = Sim->Build.IdleRpm;
	auto Wav = [](const TCHAR* Name, int& Rate) {
		TArray<uint8> Bytes;
		std::vector<float> Out;
		if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Audio"), Name)))
			Out = TorqueSound::ReadWav(std::vector<uint8_t>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), Rate);
		return Out;
	};
	int Rate = 0, Got = 0;
	std::vector<float> Idle = Wav(TEXT("idle_parallel270.wav"), Rate);
	if (!Idle.empty()) { Sound->SetIdle(std::move(Idle), Rate, 1319.0); ++Got; }
	for (int32 I = 1; I <= 8; ++I)
	{
		std::vector<float> C = Wav(*FString::Printf(TEXT("gearclunk%d.wav"), I), Rate);
		if (!C.empty()) { Sound->AddClunk(std::move(C), Rate); ++Got; }
	}
	std::vector<float> Amb = Wav(TEXT("birds_moor.wav"), Rate);
	if (!Amb.empty()) { Sound->SetAmbience(std::move(Amb), Rate); ++Got; }
	UE_LOG(LogTemp, Log, TEXT("Torque: sound with %d recordings"), Got);

	SoundWave = NewObject<USoundWaveProcedural>(this);
	SoundWave->SetSampleRate(TorqueSound::RideSound::Rate);
	SoundWave->NumChannels = 1;
	SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	SoundWave->SoundGroup = SOUNDGROUP_Default;
	SoundWave->bLooping = false;
	SoundAudio = NewObject<UAudioComponent>(this, TEXT("RideSound"));
	SoundAudio->bAutoActivate = false;
	SoundAudio->bAllowSpatialization = false;
	SoundAudio->SetupAttachment(Root);
	SoundAudio->RegisterComponent();
	SoundAudio->SetSound(SoundWave);
	UpdateSound(0.0f);   // a little queued before it starts
	SoundAudio->Play();
}

void ATorqueBikePawn::UpdateSound(float Dt)
{
	if (!Sound || !SoundWave) return;
	TorqueSound::RideInputs In;
	In.Rpm = Sim->Rpm;
	In.Throttle = Throttle;
	In.Speed = FMath::Abs(Sim->Speed);
	const Wheel& F = Sim->Wheels[0];
	const Wheel& R = Sim->Wheels[1];
	auto Lock = [](const Wheel& W) { return W.Contact ? FMath::Clamp((FMath::Abs(W.Kappa) - 0.12) / 0.3, 0.0, 1.0) : 0.0; };
	auto Side = [](const Wheel& W) { return W.Contact ? FMath::Clamp(W.Slip * 2.0, 0.0, 1.0) : 0.0; };
	In.RearSlide = Lock(R);
	In.FrontSlide = Lock(F);
	In.SideSlide = FMath::Max(Side(F), Side(R));
	const int32 Under = R.Contact ? R.Surf : F.Surf;
	In.OnTarmac = Under == SURF_TARMAC || Under == SURF_GRID;
	In.Grid = (F.Contact && F.Surf == SURF_GRID ? 1.0 : 0.0) + (R.Contact && R.Surf == SURF_GRID ? 1.0 : 0.0);
	In.Airborne = !F.Contact && !R.Contact;
	In.Inside = (CameraMode == 0 || CameraMode == 3) ? 0.0 : 1.0;
	Sound->Set(In);

	// Keep about 80 ms queued: enough for a slow frame, little enough that it answers the throttle.
	const int32 Rate = TorqueSound::RideSound::Rate;
	const int32 Have = SoundWave->GetAvailableAudioByteCount() / 2;
	const int32 Need = FMath::Clamp(Rate * 8 / 100 - Have, 0, Rate / 4);
	if (Need <= 0) return;
	SoundFloat.SetNumUninitialized(Need);
	Sound->Render(SoundFloat.GetData(), Need);
	SoundPcm.SetNumUninitialized(Need);
	for (int32 I = 0; I < Need; ++I) SoundPcm[I] = (int16)FMath::Clamp(FMath::RoundToInt(SoundFloat[I] * 32767.0f), -32767, 32767);
	SoundWave->QueueAudio(reinterpret_cast<const uint8*>(SoundPcm.GetData()), Need * 2);
}

void ATorqueBikePawn::UpdatePerf(float Dt)
{
	PerfClock += Dt;
	PerfRun += Dt;
	PerfSum += Dt;
	PerfWorst = FMath::Max(PerfWorst, (double)Dt);
	++PerfFrames;
	if (PerfClock < 1.0) return;
	const double AvgMs = PerfSum / FMath::Max(PerfFrames, 1) * 1000.0;
	const double Fps = PerfFrames / PerfClock;
	const double GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const double RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	FIntPoint Res(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) Res = GEngine->GameViewport->Viewport->GetSizeXY();
	PerfText = FString::Printf(TEXT("%.0f fps   frame %.1f ms (worst %.1f)   game %.1f   render %.1f   GPU %.1f ms   %dx%d"),
		Fps, AvgMs, PerfWorst * 1000.0, GameMs, RenderMs, GpuMs, Res.X, Res.Y);
	if (bPerfLog)
	{
		if (PerfFile.IsEmpty())
		{
			PerfFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"),
				FString::Printf(TEXT("torque_perf_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
			FFileHelper::SaveStringToFile(TEXT("time_s,fps,frame_ms,worst_ms,game_ms,render_ms,gpu_ms,width,height,speed_mph,km,camera\n"), *PerfFile);
			UE_LOG(LogTemp, Log, TEXT("Torque: logging performance to %s"), *PerfFile);
		}
		const double Km = Line.Num() > 1 ? RouteLengthM / 1000.0 * Near / (Line.Num() - 1) : 0.0;
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.1f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%.0f,%.2f,%d\n"),
			PerfRun, Fps, AvgMs, PerfWorst * 1000.0, GameMs, RenderMs, GpuMs, Res.X, Res.Y, FMath::Abs(Sim->Speed) * 2.23694, Km, CameraMode),
			*PerfFile, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
	PerfClock = 0.0;
	PerfSum = 0.0;
	PerfWorst = 0.0;
	PerfFrames = 0;
}

void ATorqueBikePawn::UpdateProgress()
{
	if (Line.Num() < 2) return;
	const V3 P = Sim->Position;
	double Best = 1e18;
	int32 BestI = Near;
	for (int32 I = FMath::Max(Near - 20, 0); I < FMath::Min(Near + 40, Line.Num()); ++I)
	{
		const double D = FMath::Square(Line[I].X - P.x) + FMath::Square(Line[I].Z - P.z);
		if (D < Best) { Best = D; BestI = I; }
	}
	Near = BestI;
	if (!bFinished && Near >= Line.Num() - 1) bFinished = true;
}

void ATorqueBikePawn::UpdateCamera(float Dt)
{
	const FVector BikePos = GetActorLocation();
	const FQuat BikeRot = GetActorQuat();
	const FVector Fwd = BikeRot.GetForwardVector();
	FVector Flat(Fwd.X, Fwd.Y, 0.0);
	Flat = Flat.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	ShowHead(CameraMode == 0 || CameraMode == 3);
	ShowRider(true);
	if (CameraMode == 3)
	{
		// Showroom: round the bike at a walking-round distance, eye height a little above the tank,
		// starting from the front three-quarter on the right (the side the photos are from). The
		// right stick nudges the angle and height.
		if (!bOrbitHeld) OrbitYaw = FMath::Fmod(OrbitYaw + 12.0 * Dt, 360.0);
		const double Yaw = FMath::DegreesToRadians(OrbitYaw + LookYaw);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Flat);
		const FVector Centre = BikePos + Flat * 5.0 + FVector(0, 0, 55.0);
		const FVector Pos = Centre + (Flat * FMath::Cos(Yaw) + Right * FMath::Sin(Yaw)) * 270.0
			+ FVector(0, 0, 55.0 - LookPitch * 4.0);
		Camera->SetWorldLocationAndRotation(Pos, (Centre - Pos).Rotation());
		Camera->SetFieldOfView(40.0f);
		return;
	}
	if (CameraMode == 1 || CameraMode == 2)
	{
		// 1, first person: the rider's eyes, inside the helmet, arms and bars below. 2, the chin
		// camera on beenster's helmet: just below the eyes at the front of the chin bar, wide.
		// Either way the head leans with the bike, but less, as yours does.
		const bool bChin = CameraMode == 2;
		// (First person sits a little behind the helmet's centre, so more of the bars and arms show.)
		const FVector EyeLocal = HeadLocal + (bChin ? FVector(15.0, 0.0, -11.0) : FVector(-7.0, 0.0, 3.5));
		// The head is the rider's: thrown off in a crash, the view goes with him.
		const FVector Eye = bThrown ? RiderRoot->GetComponentTransform().TransformPosition(EyeLocal)
			: BikePos + BikeRot.RotateVector(EyeLocal);
		FRotator Rot = Flat.Rotation();
		// Looking down a little more in first person, so the clocks and your hands are in view.
		// (The clocks are about 50 degrees below the rider's eyes and the grips a little more, so the
		// first-person view looks down 21 degrees through a wide lens: road ahead in the top half,
		// clocks, bars, gloves and forearms along the bottom.)
		Rot.Pitch = (bChin ? -12.0 : -21.0) + FMath::RadiansToDegrees(Sim->PitchAngle) * 0.7;
		Rot.Roll = FMath::RadiansToDegrees(Sim->LeanAngle) * (bChin ? 0.55 : 0.6);
		Rot.Yaw += LookYaw;
		Rot.Pitch += LookPitch;
		Camera->SetWorldLocationAndRotation(Eye, Rot);
		Camera->SetFieldOfView(bChin ? 100.0f : 108.0f);
		return;
	}

	// Chase: behind and above, following the road's direction, the horizon mostly level.
	const FVector Target = BikePos + FVector(0, 0, 110.0);
	const FVector Behind = FRotator(0.0, LookYaw, 0.0).RotateVector(-Flat);
	const FVector Want = Target + Behind * 480.0 + FVector(0, 0, 70.0 - LookPitch * 6.0);
	if (!bCamInit)
	{
		CamPos = Want;
		CamLookAt = Target;
		bCamInit = true;
	}
	const double K = 1.0 - FMath::Exp(-6.0 * Dt);
	CamPos = FMath::Lerp(CamPos, Want, K);
	CamLookAt = FMath::Lerp(CamLookAt, Target - Behind * 200.0, 1.0 - FMath::Exp(-12.0 * Dt));

	// Keep the camera out of the hillside.
	FVector Pos = CamPos;
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueCamera), true, this);
		if (World->SweepSingleByChannel(Hit, Target, CamPos, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(25.0f), Params))
			Pos = Hit.Location;
	}
	FRotator Rot = (CamLookAt - Pos).Rotation();
	Rot.Roll = FMath::RadiansToDegrees(Sim->LeanAngle) * 0.25;
	Camera->SetWorldLocationAndRotation(Pos, Rot);
	Camera->SetFieldOfView(72.0f);
}

void ATorqueBikePawn::DrawHud()
{
	const double Mph = Sim->Speed * 2.23694;
	const double Done = Line.Num() > 1 ? RouteLengthM / 1000.0 * Near / (Line.Num() - 1) : 0.0;
	const int32 Mins = (int32)(RideTime / 60.0);
	const double Secs = RideTime - Mins * 60.0;
	HudLines.Reset();
	auto Add = [this](const FString& T, FColor C, float Size) { HudLines.Add({T, C, Size}); };
	Add(FString::Printf(TEXT("%.0f mph   gear %d"), FMath::Abs(Mph), Sim->Gear), FColor::White, 2.0f);
	Add(FString::Printf(TEXT("%s gears   ABS %s"), bManual ? TEXT("Manual") : TEXT("Automatic"), bAbs ? TEXT("on") : TEXT("off")),
		GearNoteFor > 0.0 ? FColor(255, 220, 120) : FColor(200, 200, 200), GearNoteFor > 0.0 ? 1.2f : 0.95f);
	Add(FString::Printf(TEXT("%.0f rpm   lean %.0f deg"), Sim->Rpm, FMath::Abs(FMath::RadiansToDegrees(Sim->LeanAngle))),
		FColor(220, 220, 220), 1.1f);
	Add(FString::Printf(TEXT("Buttertubs  %.2f of %.2f km   %d:%05.2f%s"), Done, RouteLengthM / 1000.0, Mins, Secs,
		bFinished ? TEXT("   FINISHED - Home, or Restart in the Options menu, to ride again") : TEXT("")), FColor(200, 220, 255), 1.1f);
	if (Sim->IsCrashed) Add(TEXT("Off! Back on the road in a moment..."), FColor::Orange, 1.4f);
	if (bPerfShow && !PerfText.IsEmpty())
		Add(PerfText + (bPerfLog ? TEXT("   [logging to CSV]") : TEXT("")), FColor(140, 255, 160), 1.0f);
	const bool bSony = Pad && Pad->Get().bConnected;
	static const TCHAR* WeatherNames[3] = {TEXT("sunny"), TEXT("overcast"), TEXT("wet roads")};
	Add(FString::Printf(TEXT("Riding realism %.1f (0 full sim, 1 easy)   %s   weather: %s"), Assist,
		bSony ? TEXT("PlayStation pad connected") : TEXT("no PlayStation pad"), WeatherNames[FMath::Clamp(Weather, 0, 2)]), FColor(170, 200, 170), 0.9f);
	if (HostOnlyNoteFor > 0.0) Add(TEXT("The host sets the weather"), FColor(255, 220, 120), 1.1f);
	if (NetBike)
	{
		const FString NetLine = NetBike->StatusLine();
		if (!NetLine.IsEmpty()) Add(NetLine, FColor(170, 210, 255), 0.9f);
	}
	if (bShowHelp)
	{
		const FColor Help(200, 200, 200);
		Add(TEXT("Pad:  R2 throttle   L2 front brake   L1 rear brake   left stick lean (push up/down: weight)"), Help, 0.85f);
		Add(TEXT("      Cross gear up   Square gear down   R3 auto/manual   Circle ABS on/off   right stick look   L3 performance"), Help, 0.85f);
		Add(TEXT("      R1 camera   Triangle back on road   D-pad up/down realism, right weather   Options menu"), Help, 0.85f);
		Add(TEXT("Keys: W throttle   S brake   Space rear brake   A/D lean   Q/E weight   X/Z gear up/down   G auto/manual"), Help, 0.85f);
		Add(TEXT("      B ABS   C camera   R back on road   Home restart   [ ] realism   V weather   P performance   O log it   H help   Backspace shed   Esc menu"), Help, 0.85f);
	}
}


// Weather: ATorqueGameState lights the world for it (the host's weather, in co-op); the bike takes
// a third less grip on wet roads.
void ATorqueBikePawn::ApplyWeather()
{
	if (Sim) Sim->Build.WetGrip = Weather == 2 ? 0.68 : 1.0;
	UE_LOG(LogTemp, Log, TEXT("Torque: bike in weather %d"), Weather);
}
