#include "TorqueBikePawn.h"
#include "TorqueAudio/TorqueSfx.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueKeys.h"
#include "Coop/TorquePlayerState.h"
#include "Shed/TorqueProfile.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPower.h"

#include "BikeSim.h"
#include "TorqueFit.h"
#include "BikeRideSpec.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "UObject/UObjectIterator.h"
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
	// Settings > Gameplay > Automatic gearbox: how each ride starts (G / R3 changes it while riding).
	bManual = !FTorqueSettings::Get().bAutoShift;
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
	if (BuildCatalogueModel()) return;
	// Every bike is made from the part list: the Speed Twin model (beenster's own bike, the
	// prototype's) only comes back for the fitting tests (-TorqueBuild=) or with -TorqueSpeedTwin.
	if (FCString::Strifind(FCommandLine::Get(), TEXT("TorqueBuild=")) == nullptr && !FParse::Param(FCommandLine::Get(), TEXT("TorqueSpeedTwin")))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no bike from the part list to ride (no shed bike); using shapes"));
		return;
	}
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
		FName Slot;   // the shed's slot it's part of (geometry mode's offsets are by slot)
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
					ToMake.Add({R.Name.ToString(), R.Group.ToString(), R.Mesh, R.Material, R.Delta, R.Unit, TorqueShedParts::SlotOfModelPart(R)});
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
				(*Where)->GetStringField(TEXT("material")), FTransform::Identity, NAME_None, TorqueShedParts::SlotOfModelPart(FName(*Name))});
		}
	}
	FTransform LeverDelta[2] = {FTransform::Identity, FTransform::Identity};
	GripDelta[0] = GripDelta[1] = FTransform::Identity;
	GripParts[0] = GripParts[1] = nullptr;
	int32 Made = 0;
	TArray<FName> PartSlots, PartGroups;   // per part made, for geometry mode's fit
	TArray<FTransform> PartRest;
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
		PartSlots.Add(P.Slot);
		PartGroups.Add(FName(*P.Group));
		PartRest.Add(Fix * P.Delta);
		// By unit, so option levers and grips (one mesh each, modelled on the stock pivots) work too.
		const FString UnitName = P.Unit.IsNone() ? P.Name : P.Unit.ToString();
		if (UnitName == TEXT("lever_clutch")) { LeverParts[0] = C; LeverDelta[0] = P.Delta; }
		else if (UnitName == TEXT("lever_brake")) { LeverParts[1] = C; LeverDelta[1] = P.Delta; }
		else if (UnitName == TEXT("grip_l") || UnitName == TEXT("grips_l")) { GripDelta[0] = P.Delta; GripParts[0] = C; }
		else if (UnitName == TEXT("grip_r") || UnitName == TEXT("grips_r")) { GripDelta[1] = P.Delta; GripParts[1] = C; }
		++Made;
	}
	// The dirt and paint the bike has in the shed (washed or not, resprayed or not) ride out with it.
	TorqueShedModel::ApplyLooks(this, ModelParts);
	// And its fit from geometry mode, as the shed draws it (-TorqueBuild= test builds have none).
	if (FCString::Strifind(FCommandLine::Get(), TEXT("TorqueBuild=")) == nullptr) ApplyRideFit(PartSlots, PartGroups, PartRest);
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

// Geometry mode's fit on the Speed Twin model, as the shed shows it (FTorqueBikeFitView): each slot's
// offset about the middle of its parts at rest, in the bike's frame. Standing still each group sits
// at its origin (TorqueShedModel::LoadFrame), so a part at Rest on group origin O is at Rest * O in
// the bike; moved, Rest * O * M, which on the group is Rest * (O * M * O^-1): it rides with the group,
// the bars' offset turning with the steering. The wheels spin, so theirs is put back every frame in
// UpdateVisuals, in the frame of the fork lowers or the swingarm that carry them.
void ATorqueBikePawn::ApplyRideFit(const TArray<FName>& PartSlots, const TArray<FName>& PartGroups, const TArray<FTransform>& PartRest)
{
	RideFit.Reset();
	LeverFit[0] = LeverFit[1] = GripFitMove[0] = GripFitMove[1] = SeatFitMove = PegFitMove = FTransform::Identity;
	const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
	const int32 Bay = UTorqueBuildSubsystem::BikeFor(this);
	if (!Sub || Bay < 0 || !Sub->Exists(Bay) || Sub->Bike(Bay).Geo.IsEmpty()) return;
	const FString& Geo = Sub->Bike(Bay).Geo;
	FTransform UnusedFix;
	TMap<FName, FVector> Origins;
	TorqueShedModel::LoadFrame(UnusedFix, Origins);
	auto OriginOf = [&Origins](FName Group) { return FTransform(Origins.FindRef(Group)); };
	// Each slot's middle: the box round its parts as they stand still.
	TMap<FName, FBox> Boxes;
	for (int32 I = 0; I < ModelParts.Num() && I < PartSlots.Num(); ++I)
	{
		const UStaticMeshComponent* C = ModelParts[I];
		if (!C || !C->GetStaticMesh() || PartSlots[I].IsNone()) continue;
		Boxes.FindOrAdd(PartSlots[I], FBox(ForceInit)) += C->GetStaticMesh()->GetBoundingBox().TransformBy(PartRest[I] * OriginOf(PartGroups[I]));
	}
	TMap<FName, FVector> Pivots;
	for (const TPair<FName, FBox>& KV : Boxes)
		if (KV.Value.IsValid) Pivots.Add(KV.Key, KV.Value.GetCenter());
	TMap<FName, FTransform> Moves;
	TorqueGeometryMode::SlotMoves(Geo, Pivots, Moves);
	if (Moves.Num() == 0) return;
	// The rider sits on the seat and rests his feet on the pegs (on the frame: bike's frame offsets).
	if (const FTransform* Seat = Moves.Find(FName(TEXT("saddle")))) SeatFitMove = *Seat;
	if (const FTransform* Pegs = Moves.Find(FName(TEXT("rearsets")))) PegFitMove = *Pegs;
	static const FName FrontWheelGroup(TEXT("front_wheel")), RearWheelGroup(TEXT("rear_wheel"));
	static const FName FrontLowerGroup(TEXT("front_lower")), SwingarmGroup(TEXT("swingarm"));
	int32 Moved = 0;
	for (int32 I = 0; I < ModelParts.Num() && I < PartSlots.Num(); ++I)
	{
		UStaticMeshComponent* C = ModelParts[I];
		const FTransform* M = Moves.Find(PartSlots[I]);
		if (!C || !M) continue;
		++Moved;
		const FName Group = PartGroups[I];
		if (Group == FrontWheelGroup || Group == RearWheelGroup)
		{
			// (The fork lowers or the swingarm stand at their origins too: the offset in their frame.)
			const bool bFront = Group == FrontWheelGroup;
			const FName CarrierGroup = bFront ? FrontLowerGroup : SwingarmGroup;
			const FTransform C0 = Origins.Contains(CarrierGroup) ? OriginOf(CarrierGroup) : (bFront ? OriginOf(FrontWheelGroup) : FTransform(SwingPivot));
			FTorqueRideFit Fit;
			Fit.Part = C;
			Fit.Rest = PartRest[I];
			Fit.Spin = bFront ? FrontPivot : RearPivot;
			Fit.Carrier = bFront ? FrontHub : SwingGroup;
			Fit.Move = C0 * *M * C0.Inverse();
			RideFit.Add(Fit);
			continue;
		}
		const FTransform O = OriginOf(Group);
		const FTransform OnGroup = O * *M * O.Inverse();
		C->SetRelativeTransform(PartRest[I] * OnGroup);
		// The levers (set again as they're pulled) and the grips (the rider's hands follow them).
		for (int32 S = 0; S < 2; ++S)
		{
			if (C == LeverParts[S]) LeverFit[S] = OnGroup;
			if (C == GripParts[S]) GripFitMove[S] = OnGroup;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: the Speed Twin rides with its fit from geometry mode: %d slots moved, %d parts (%d on the wheels)"),
		Moves.Num(), Moved, RideFit.Num());
}

namespace
{
std::string PawnStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

// A catalogue bike's riding spec, as Torque Shed's BikeBuild works it out (Fit/BikeRideSpec, checked
// against the Godot game by SimTest/spec_test): the frame's wheelbase, rake and centre of mass, the
// fork's offset and the wheels' size giving the trail, suspension, tyres, brakes, gearbox and aids
// from their parts, the mass from every part, each part's wear, rust and dirt taking off what Torque
// Shed takes off; and the engine's torque curve with every engine part's multiplier, as the shed
// works it out for the dyno (TorqueShedPower). False if the parts or the engine won't do.
bool CatalogueRideSpec(const UTorqueBuildSubsystem& B, int32 Bay, const TMap<FString, FString>& Parts, BikeSpec& Out, FString& OutText)
{
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return false;
	TorqueFit::RideSpecInput In;
	for (const TPair<FString, FString>& KV : Parts)
	{
		const std::string Slot = PawnStd(KV.Key);
		In.Parts[Slot] = PawnStd(KV.Value);
		if (const FTorqueItem* It = B.Item(B.ItemOn(Bay, FName(*KV.Key))))
			In.Health[Slot] = TorqueFit::PartHealth(It->Wear, It->Rust, It->Dirt);
	}
	std::vector<std::string> Notes;
	BikeSpec S;
	if (!TorqueFit::RideSpec(*Lay, In, S, &Notes)) return false;
	const FTorqueEngineNumbers E = TorqueShedPower::Numbers(B, Bay);
	if (!E.bValid) return false;
	S.IdleRpm = E.IdleRpm;
	S.Redline = E.Redline;
	S.PeakTorque = E.PeakTorque;
	S.PeakTorqueRpm = E.PeakTorqueRpm;
	S.TorqueRpm.clear();
	S.TorqueNm.clear();
	for (int32 I = 0; I < 25; ++I)
	{
		const double Rpm = FMath::Lerp((double)E.IdleRpm, (double)E.Redline, I / 24.0);
		S.TorqueRpm.push_back(Rpm);
		S.TorqueNm.push_back(TorqueShedPower::EngineTorque(E, (float)Rpm));
	}
	// (Not BikeSpec::Finish: that works the trail, fork path, swingarm and inertia out the Speed
	// Twin's way, and BikeBuild has given them already.)
	Out = S;
	FString Ratios;
	for (const double R : S.GearRatios) Ratios += FString::Printf(TEXT("%s%.2f"), Ratios.IsEmpty() ? TEXT("") : TEXT(" "), R);
	OutText = FString::Printf(
		TEXT("wheelbase %.3f m, rake %.1f deg, trail %.0f mm (offset %.0f mm), wheels %.0f / %.0f mm, bike %.1f kg (%.1f with the rider), ")
		TEXT("CoM %.3f m up, seat %.0f mm; travel %.0f / %.0f mm, springs %.0f / %.0f N/m; brakes %.0f / %.0f N m, ABS %s / %s, TC %s; ")
		TEXT("engine idle %.0f, peak %.1f N m at %.0f, redline %.0f rpm; primary %.3f, gears %s, final %.3f, drive %.2f; ")
		TEXT("tyres %s %.2f / %s %.2f grip; steer rate %.2f, lean clearance %.1f deg, CdA %.3f"),
		S.Wheelbase, S.RakeDeg, S.Trail * 1000.0, S.ForkOffset * 1000.0, S.FrontRadius * 1000.0, S.RearRadius * 1000.0, S.BikeMass, S.TotalMass,
		S.ComHeight, S.SeatHeight * 1000.0, S.FrontTravel * 1000.0, S.RearTravel * 1000.0, S.FrontSpring, S.RearSpring,
		S.FrontBrakeTorque, S.RearBrakeTorque, S.AbsFront ? TEXT("on") : TEXT("off"), S.AbsRear ? TEXT("on") : TEXT("off"),
		S.TractionControl ? *FString::Printf(TEXT("%.2f"), S.TractionStrength) : TEXT("off"),
		S.IdleRpm, S.PeakTorque, S.PeakTorqueRpm, S.Redline, S.Primary, *Ratios, S.FinalDrive, S.DrivetrainEfficiency,
		UTF8_TO_TCHAR(S.TyreFront.Construction.c_str()), S.TyreFront.GripTarmac, UTF8_TO_TCHAR(S.TyreRear.Construction.c_str()), S.TyreRear.GripTarmac,
		S.SteerRate, S.LeanClearance, S.CdA);
	for (const std::string& N : Notes) OutText += TEXT("; ") + FString(UTF8_TO_TCHAR(N.c_str()));
	return true;
}
} // namespace

// A bike of catalogue parts (any of Torque Shed's styles, built in the shed) rides out drawn as
// the shed draws it (TorqueShedCatalogue::MakeBike), not as the Speed Twin, and on its own riding
// spec (CatalogueRideSpec). Its parts go on the Speed Twin model's groups by what fitting says each
// moves with (FittedPart.Node): the wheels, tyres and discs spin on the axles, the bars, top yoke,
// lamp and fork tubes turn about the steering axis, the fork lowers ride the front axle and the
// swingarm swings about its pivot, the layout giving where those are. -TorqueBuild= on the command
// line always means the Speed Twin.
bool ATorqueBikePawn::BuildCatalogueModel()
{
	if (FCString::Strifind(FCommandLine::Get(), TEXT("TorqueBuild=")) != nullptr) return false;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return false;
	// Your bike, or (none in that bay) the shed's first bike from the part list.
	int32 Bay = UTorqueBuildSubsystem::BikeFor(this);
	TMap<FString, FString> Parts;
	if (Bay < 0 || !B->LayoutParts(Bay, Parts))
	{
		Bay = INDEX_NONE;
		for (int32 I = 0; I < UTorqueBuildSubsystem::Bays && Bay < 0; ++I)
			if (B->LayoutParts(I, Parts)) Bay = I;
		if (Bay < 0) return false;
	}
	RideBay = Bay;
	// Its own physics, or if that won't work out, the Speed Twin's (on which the drawn bike's origin,
	// at its own rear axle's height, is lifted or dropped to the Speed Twin's 31 cm).
	BikeSpec Spec;
	const bool bOwnSpec = CatalogueRideSpec(*B, Bay, Parts, Spec, CatalogueSpec);
	static constexpr double SpeedTwinAxleCm = 31.0;
	USceneComponent* Holder = NewObject<USceneComponent>(this, TEXT("CatalogueRoot"));
	Holder->SetupAttachment(Root);
	Holder->RegisterComponent();
	double AxleHeight = SpeedTwinAxleCm;
	// With its fit from geometry mode, as the shed draws it: the rig's handles lay it out, and each
	// slot's offset moves its parts (FTorqueBikeFitView).
	const FString& Geo = B->Bike(Bay).Geo;
	TArray<FName> Nodes;
	TorqueShedCatalogue::FBikeFrame Frame;
	const int32 Drawn = TorqueShedCatalogue::MakeBike(this, Holder, Parts, ModelParts, AxleHeight, Geo, &Nodes, &Frame);
	if (Drawn <= 0)
	{
		Holder->DestroyComponent();
		UE_LOG(LogTemp, Warning, TEXT("Torque: bay %d's catalogue bike drew nothing"), Bay + 1);
		return false;
	}
	FTorqueBikeFitView Fit;
	Fit.Capture(ModelParts);
	if (!Geo.IsEmpty()) Fit.Show(ModelParts, Geo);
	TorqueShedModel::ApplyLooks(this, ModelParts);
	for (UStaticMeshComponent* Shape : {Body, Tank, Rider, FrontWheel, RearWheel}) Shape->SetVisibility(false);
	if (bOwnSpec && Frame.bValid && Nodes.Num() == ModelParts.Num())
	{
		// Where the rider sits, before the parts go on their groups (the bike's frame is the holder's,
		// which stays at the pawn's origin): the layout's seat and pegs, moved as geometry mode moved
		// the saddle and the rearsets, and the grips' middles as drawn (their surfaces' boxes).
		{
			static const FName SaddleTag(TEXT("Slot:saddle")), RearsetsTag(TEXT("Slot:rearsets")), GripTag(TEXT("Part:controls/grip.glb"));
			auto MoveOf = [&](const FName& Tag) {
				for (int32 I = 0; I < ModelParts.Num() && I < Fit.Rest.Num(); ++I)
					if (ModelParts[I] && ModelParts[I]->ComponentHasTag(Tag)) return Fit.Rest[I].Inverse() * ModelParts[I]->GetRelativeTransform();
				return FTransform::Identity;
			};
			RiderSeat = MoveOf(SaddleTag).TransformPosition(Frame.Seat);
			const FTransform PegMove = MoveOf(RearsetsTag);
			RiderPeg[1] = PegMove.TransformPosition(Frame.Peg);
			RiderPeg[0] = PegMove.TransformPosition(FVector(Frame.Peg.X, -Frame.Peg.Y, Frame.Peg.Z));
			FBox GripBox[2] = {FBox(ForceInit), FBox(ForceInit)};
			for (UStaticMeshComponent* C : ModelParts)
				if (C && C->GetStaticMesh() && C->ComponentHasTag(GripTag))
				{
					const FBox Box = C->GetStaticMesh()->GetBoundingBox().TransformBy(C->GetRelativeTransform());
					GripBox[Box.GetCenter().Y < 0.0 ? 0 : 1] += Box;
				}
			// No drawn grips (a style whose bars don't take them): the layout's.
			RiderGrip[1] = GripBox[1].IsValid ? GripBox[1].GetCenter() : Frame.Grip;
			RiderGrip[0] = GripBox[0].IsValid ? GripBox[0].GetCenter() : FVector(RiderGrip[1].X, -RiderGrip[1].Y, RiderGrip[1].Z);
			bRiderLaidBack = Frame.bLaidBack;
			bRiderOnLayout = true;
			bRiderFitted = false;
		}
		// The sim's body frame is the drawn bike's (the origin at axle height midway between the
		// axles, a wheelbase apart: SimTest/spec_test checks the layout and the spec agree).
		Sim->Setup(Spec);
		// ABS as its ECU gives it (the HUD and the B key follow).
		bHasAbs[0] = Spec.AbsFront;
		bHasAbs[1] = Spec.AbsRear;
		bAbs = Spec.AbsFront || Spec.AbsRear;
		SteerPoint = Frame.SteerPoint;
		SteerDir = Frame.SteerDir;
		SwingPivot = Frame.SwingPivot;
		RearRest = Frame.RearAxle;
		// Each group as UpdateVisuals has it standing still, straight ahead: a part drawn at T in the
		// bike goes on its group at T * GroupRest^-1.
		static const FName SteerNode(TEXT("steer")), FrontHubNode(TEXT("front_hub")), FrontWheelNode(TEXT("front_wheel"));
		static const FName RearHubNode(TEXT("rear_hub")), RearWheelNode(TEXT("rear_wheel")), SwingNode(TEXT("swingarm"));
		int32* Count = GroupParts;
		for (int32 I = 0; I < ModelParts.Num(); ++I)
		{
			UStaticMeshComponent* C = ModelParts[I];
			if (!C) continue;
			const FName N = Nodes[I];
			USceneComponent* Group = Holder;
			FTransform GroupRest = FTransform::Identity;
			int32 Which = 0;
			if (N == SteerNode) { Group = SteerGroup; Which = 1; }
			else if (N == FrontHubNode) { Group = FrontHub; GroupRest = FTransform(Frame.FrontAxle); Which = 2; }
			else if (N == FrontWheelNode) { Group = FrontPivot; GroupRest = FTransform(Frame.FrontAxle); Which = 3; }
			else if (N == RearHubNode || N == SwingNode) { Group = SwingGroup; GroupRest = FTransform(Frame.SwingPivot); Which = 4; }
			else if (N == RearWheelNode) { Group = RearPivot; GroupRest = FTransform(Frame.RearAxle); Which = 5; }
			++Count[Which];
			if (Group == Holder) continue;
			const FTransform InBike = C->GetRelativeTransform();
			C->AttachToComponent(Group, FAttachmentTransformRules::KeepRelativeTransform);
			C->SetRelativeTransform(InBike * GroupRest.Inverse());
		}
		bCatalogueModel = true;
		UE_LOG(LogTemp, Log, TEXT("Torque: catalogue bike's parts on the bike %d, the steering %d, the fork lowers %d, the front wheel %d, the swingarm %d, the rear wheel %d"),
			Count[0], Count[1], Count[2], Count[3], Count[4], Count[5]);
	}
	else
	{
		Holder->SetRelativeLocation(FVector(0.0, 0.0, AxleHeight - SpeedTwinAxleCm));
		CatalogueSpec = TEXT("SpeedTwin1200 (its own spec didn't work out)");
	}
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: catalogue bike %s (bay %d), %d surfaces, axle %.1f cm%s"),
			*B->Bike(Bay).Name, Bay + 1, Drawn, AxleHeight, Geo.IsEmpty() ? TEXT("") : TEXT(", with its fit from geometry mode"));
		UE_LOG(LogTemp, Log, TEXT("Torque: catalogue bike's riding spec: %s"), *CatalogueSpec);
	}
	return true;
}

FString ATorqueBikePawn::TestRideLog() const
{
	const Wheel& F = Sim->Wheels[0];
	const Wheel& R = Sim->Wheels[1];
	const FVector Start = Line.IsValidIndex(StartIndex) ? Line[StartIndex] : FVector::ZeroVector;
	const double FromStart = FMath::Sqrt(FMath::Square(Sim->Position.x - Start.X) + FMath::Square(Sim->Position.z - Start.Z));
	FString Out = FString::Printf(TEXT("%s; spec: %s\n"), bCatalogueModel ? TEXT("catalogue bike, its own spec") : bHaveModel ? TEXT("Speed Twin model") : TEXT("shapes"),
		CatalogueSpec.IsEmpty() ? TEXT("SpeedTwin1200") : *CatalogueSpec);
	Out += FString::Printf(TEXT("now: %.1f km/h, %.0f m from the start, gear %d, %.0f rpm, lean %.1f deg, steer %.2f deg, front spin %.0f deg, rear %.0f deg, ")
		TEXT("suspension %.0f / %.0f mm%s%s"),
		Sim->Speed * 3.6, FromStart, Sim->Gear, Sim->Rpm, FMath::RadiansToDegrees(Sim->LeanAngle), FMath::RadiansToDegrees(Sim->Steer),
		FMath::RadiansToDegrees(F.Spin), FMath::RadiansToDegrees(R.Spin), F.Compression * 1000.0, R.Compression * 1000.0,
		Sim->IsCrashed ? TEXT(", crashed") : TEXT(""), bTestAutoRide ? TEXT(", the robot riding") : TEXT(""));
	if (bCatalogueModel)
	{
		// The drawn groups, as UpdateVisuals left them: the bars' turn about the steering axis (signed
		// as the sim's steer, so the two agree when the bars turn the way the sim steers), the
		// wheels' pivots, the swingarm's swing about its axle line (+ down), and how many meshes each
		// group carries.
		auto Signed = [](const FQuat& Q, const FVector& About) {
			FVector Axis;
			double Angle = 0.0;
			Q.ToAxisAndAngle(Axis, Angle);
			if (Angle > PI) Angle -= 2.0 * PI;
			return FMath::RadiansToDegrees(FVector::DotProduct(Axis, About) < 0.0 ? -Angle : Angle);
		};
		Out += FString::Printf(TEXT("\ndrawn: bars turned %.2f deg, front wheel at %s, rear at %s, swingarm %.2f deg; ")
			TEXT("meshes on the bike %d, steering %d, fork lowers %d, front wheel %d, swingarm %d, rear wheel %d"),
			Signed(SteerGroup->GetRelativeRotation().Quaternion(), SteerDir),
			*FrontPivot->GetRelativeLocation().ToString(), *RearPivot->GetRelativeLocation().ToString(),
			Signed(SwingGroup->GetRelativeRotation().Quaternion(), FVector::RightVector),
			GroupParts[0], GroupParts[1], GroupParts[2], GroupParts[3], GroupParts[4], GroupParts[5]);
	}
	if (bRiderOnLayout)
	{
		// Where the rider was sat (FitRiderToBike): the bike's seat, pegs and grips, and the pose's
		// targets on them (the rig's pelvis, wrists and ankles), pawn local cm.
		// (The wrist bone sits 6.4 cm behind and 3.4 cm above the grip's middle, the palm round the
		// grip; the ankle 11.5 cm behind and above the peg, the ball of the foot on it: as on the
		// Speed Twin the rig was made on.)
		Out += FString::Printf(TEXT("\nrider (%s%s): seat %s, pegs %s / %s, grips %s / %s; pose hips %s, wrists %s / %s, ankles %s / %s, ")
			TEXT("back %.1f deg forward of upright (style's own %.1f; spine posed %.1f, set %.1f, drawn %.1f); reach %.1f / %.1f cm of a %.1f cm arm (%s), hip to ankle %.1f / %.1f of a %.1f cm leg%s"),
			*RiderStyle, bRiderLaidBack ? TEXT(", laid back") : TEXT(""),
			*RiderSeat.ToString(), *RiderPeg[0].ToString(), *RiderPeg[1].ToString(), *RiderGrip[0].ToString(), *RiderGrip[1].ToString(),
			*PoseHip.ToString(), *PoseWrist[0].ToString(), *PoseWrist[1].ToString(), *PoseAnkle[0].ToString(), *PoseAnkle[1].ToString(),
			RiderBackDeg, RiderStyleDeg, RiderRig.PosedSpineDeg, RiderRig.SetSpineDeg(), RiderRig.DrawnSpineDeg(), RiderReach[0], RiderReach[1], RiderArm,
			FMath::Max(RiderReach[0], RiderReach[1]) > RiderArm ? *FString::Printf(TEXT("hands %.1f cm short of the grips"), FMath::Max(RiderReach[0], RiderReach[1]) - RiderArm) : TEXT("hands on the grips"),
			RiderLegReach[0], RiderLegReach[1], RiderLeg, bRiderFitted && RiderRig.IsValid() ? TEXT("") : TEXT(" (not fitted: no rig)"));
		if (RiderRig.Helmet)
			Out += FString::Printf(TEXT("\nrider helmet at %s (pawn local)"), *RiderRig.Helmet->GetRelativeLocation().ToString());
	}
	// Every body that could be the rider you see: the pawn's skinned meshes and everything under its
	// rider root, and any skinned mesh in the world within 3 m (another player's or a walker's).
	{
		auto Describe = [this](const UPrimitiveComponent* C) {
			FString Desc = FString::Printf(TEXT("\n  %s.%s (%s): %s%s%s%s, rendered %s, at %s"),
				C->GetOwner() ? *C->GetOwner()->GetName() : TEXT("?"), *C->GetName(), *C->GetClass()->GetName(),
				C->IsVisible() ? TEXT("visible") : TEXT("not visible"), C->bHiddenInGame ? TEXT(", hidden in game") : TEXT(""),
				C->bOwnerNoSee ? TEXT(", owner no see") : TEXT(""), C->bOnlyOwnerSee ? TEXT(", only owner sees") : TEXT(""),
				C->WasRecentlyRendered(0.5f) ? TEXT("yes") : TEXT("no"),
				*GetActorTransform().InverseTransformPosition(C->GetComponentLocation()).ToString());
			if (const USkinnedMeshComponent* Sk = Cast<USkinnedMeshComponent>(C))
			{
				const USkinnedMeshComponent* Leader = Sk->LeaderPoseComponent.Get();
				Desc += FString::Printf(TEXT(", mesh %s, leader pose %s"), Sk->GetSkinnedAsset() ? *Sk->GetSkinnedAsset()->GetName() : TEXT("none"),
					Leader ? *FString::Printf(TEXT("%s.%s"), Leader->GetOwner() ? *Leader->GetOwner()->GetName() : TEXT("?"), *Leader->GetName()) : TEXT("none"));
				if (const USkeletalMeshComponent* Skel = Cast<USkeletalMeshComponent>(Sk))
					Desc += FString::Printf(TEXT(", anim mode %d, anim instance %s"), (int32)Skel->GetAnimationMode(),
						Skel->GetAnimInstance() ? *Skel->GetAnimInstance()->GetClass()->GetName() : TEXT("none"));
			}
			return Desc;
		};
		TArray<const UPrimitiveComponent*> Seen;
		TArray<UPrimitiveComponent*> Mine;
		GetComponents(Mine);
		for (const UPrimitiveComponent* C : Mine)
			if (C && (C->IsA<USkinnedMeshComponent>() || (RiderRoot && C->IsAttachedTo(RiderRoot)))) Seen.AddUnique(C);
		const FVector Here = GetActorLocation();
		for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
			if (It->GetWorld() == GetWorld() && It->IsRegistered() && FVector::Dist(It->GetComponentLocation(), Here) < 300.0) Seen.AddUnique(*It);
		Out += FString::Printf(TEXT("\nrider components (%d):"), Seen.Num());
		for (const UPrimitiveComponent* C : Seen) Out += Describe(C);
	}
	return Out;
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
	if ((bAuto || bTestAutoRide) && Line.Num() > 2 && !Sim->IsCrashed)
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
	// Settings > Gameplay > Chase camera recentres: off, the view stays where the stick left it.
	const bool bStickLooking = FMath::Abs(LookX) > 0.02 || FMath::Abs(LookY) > 0.02;
	if (Set.bCameraRecentre || bStickLooking)
	{
		StickYaw = FMath::Lerp(StickYaw, LookX * 130.0, LookK);
		StickPitch = FMath::Lerp(StickPitch, LookY * 25.0, LookK);
	}
	// The mouse swings the view round too (Settings > Controller > Mouse look speed), and it eases
	// back behind the bike a moment after you stop moving it.
	{
		float Mx = 0.0f, My = 0.0f;
		PC->GetInputMouseDelta(Mx, My);
		if (FMath::Abs(Mx) + FMath::Abs(My) > 0.0f)
		{
			MouseYaw = FMath::Clamp(MouseYaw + Mx * 0.25 * Set.MouseLook, -170.0, 170.0);
			MousePitch = FMath::Clamp(MousePitch + My * 0.25 * Set.MouseLook * (Set.bInvertLook ? -1.0 : 1.0), -25.0, 25.0);
			MouseStill = 0.0;
		}
		else if ((MouseStill += Dt) > 1.0 && Set.bCameraRecentre)
		{
			const double Back = 1.0 - FMath::Exp(-2.5 * Dt);
			MouseYaw = FMath::Lerp(MouseYaw, 0.0, Back);
			MousePitch = FMath::Lerp(MousePitch, 0.0, Back);
		}
	}
	// Reverse view: held, the camera looks back down the road.
	const bool bBehind = FTorqueKeys::Down(PC, ETorqueKey::LookBack);
	LookYaw = bBehind ? 180.0 : StickYaw + MouseYaw;
	LookPitch = bBehind ? 0.0 : StickPitch + MousePitch;

	const double KeyThrottle = FTorqueKeys::Down(PC, ETorqueKey::Throttle) ? 1.0 : 0.0;
	const double KeyBrake = FTorqueKeys::Down(PC, ETorqueKey::FrontBrake) ? 1.0 : 0.0;
	const double KeyLean = FMath::Clamp(((FTorqueKeys::Down(PC, ETorqueKey::LeanRight) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::LeanLeft) ? 1.0 : 0.0)) * Set.Steering, -1.0, 1.0);
	const double KeyShift = (FTorqueKeys::Down(PC, ETorqueKey::WeightForward) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::WeightBack) ? 1.0 : 0.0);

	// Keys are all or nothing, so ease them in (as Torque Shed does); the pad is used as it is.
	Throttle = PadThrottle > 0.02 ? PadThrottle : Approach(Throttle, KeyThrottle, 2.5, 6.0, Dt);
	FrontBrake = PadBrake > 0.02 ? PadBrake : Approach(FrontBrake, KeyBrake, 3.0, 8.0, Dt);
	RearBrake = FMath::Max(FTorqueKeys::Down(PC, ETorqueKey::RearBrake) ? 1.0 : 0.0, Down(EKeys::Gamepad_LeftShoulder));
	if (SonyDown & ESonyButton::L1) RearBrake = 1.0;

	// Gears: Cross/A up, Square/X down (X and Z, or Left Shift and Left Ctrl, on keys). The first
	// change by hand turns the automatic box off; R3 or G turns it back on.
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	const bool bUp = SonyHit(ESonyButton::Cross) || Just(EKeys::Gamepad_FaceButton_Bottom) || FTorqueKeys::Just(PC, ETorqueKey::ShiftUp);
	const bool bDown = SonyHit(ESonyButton::Square) || Just(EKeys::Gamepad_FaceButton_Left) || FTorqueKeys::Just(PC, ETorqueKey::ShiftDown);
	if (bUp || bDown)
	{
		bManual = true;
		Sim->AutoShift = false;
		if (bUp) Sim->ShiftUp();
		else Sim->ShiftDown();
	}
	if (SonyHit(ESonyButton::R3) || Just(EKeys::Gamepad_RightThumbstick) || FTorqueKeys::Just(PC, ETorqueKey::AutoShift))
	{
		bManual = !bManual;
		GearNoteFor = 2.0;
		// As Torque Shed: the setting follows (kept once the player has saved settings).
		FTorqueSettings& Keep = FTorqueSettings::Get();
		Keep.bAutoShift = !bManual;
		if (Keep.bSaved) Keep.Save();
	}
	// ABS: Circle/B or the B key. Off, the rear brake will lock the wheel.
	if (SonyHit(ESonyButton::Circle) || Just(EKeys::Gamepad_FaceButton_Right) || FTorqueKeys::Just(PC, ETorqueKey::Abs))
	{
		// Only on a bike that has it (as Torque Shed: the ECU's aid); none, it stays off.
		bAbs = !bAbs && (bHasAbs[0] || bHasAbs[1]);
		Sim->Build.AbsFront = bAbs && bHasAbs[0];
		Sim->Build.AbsRear = bAbs && bHasAbs[1];
		GearNoteFor = 2.0;
	}
	Lean = FMath::Abs(PadLean) > 0.08 ? PadLean : Approach(Lean, KeyLean, 2.2, 4.0, Dt);
	Shift = FMath::Abs(PadShift) > 0.15 ? PadShift : Approach(Shift, KeyShift, 3.0, 3.0, Dt);

	if (FTorqueKeys::Just(PC, ETorqueKey::BackOnRoad) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top) ||
		SonyHit(ESonyButton::Triangle))
		Respawn(FMath::Max(Near - 2, 0));
	// Options / Start open the pause menu now (ATorquePlayerController), which has Restart the ride.
	if (FTorqueKeys::Just(PC, ETorqueKey::Restart)) RestartRide();
	if (FTorqueKeys::Just(PC, ETorqueKey::Camera) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) ||
		PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left) || SonyHit(ESonyButton::R1 | ESonyButton::Create))
	{
		CameraMode = (CameraMode + 1) % CameraModes;
		bCamInit = false;
	}
	if (SonyHit(ESonyButton::DDown)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (SonyHit(ESonyButton::DUp)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (FTorqueKeys::Just(PC, ETorqueKey::Help)) bShowHelp = !bShowHelp;
	// Settings > Gameplay > Telemetry overlay (F3 while riding, as Torque Shed).
	if (FTorqueKeys::Just(PC, ETorqueKey::Telemetry))
	{
		FTorqueSettings& Keep = FTorqueSettings::Get();
		Keep.bTelemetry = !Keep.bTelemetry;
		if (Keep.bSaved) Keep.Save();
	}
	// The horn while J or the d-pad's left is held (Torque Shed: H or L3, both taken here).
	const bool bHorn = FTorqueKeys::Down(PC, ETorqueKey::Horn) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left) || (SonyDown & ESonyButton::DLeft) != 0;
	if (bHorn != bHornDown && HornId)
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->SetHorn(HornId, bHorn);
	bHornDown = bHorn;
	// Backspace: back to the shed (when its map has been built).
	if (FTorqueKeys::Just(PC, ETorqueKey::BackToShed) && TorqueFlow::BackToShed(this)) return;
	if (FTorqueKeys::Just(PC, ETorqueKey::Weather) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
	{
		// The host's (or a solo rider's) to change; Tick picks it up from the game state.
		ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		if (GS && HasAuthority()) GS->SetWeather(GS->GetWeather() + 1);
		else if (GS) HostOnlyNoteFor = 3.0;
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Perf) || SonyHit(ESonyButton::L3) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick))
		bPerfShow = !bPerfShow;
	if (FTorqueKeys::Just(PC, ETorqueKey::PerfLog))
	{
		bPerfLog = !bPerfLog;
		if (!bPerfLog) PerfFile.Reset();
	}
	// Esc, Options and Start open the pause menu (ATorquePlayerController), which has Quit.
	if (FTorqueKeys::Just(PC, ETorqueKey::RealismDown)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (FTorqueKeys::Just(PC, ETorqueKey::RealismUp)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
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
		// The telemetry overlay's strips: the last 12 seconds, ten samples a second.
		if ((TelemetryClock += DeltaSeconds) >= 0.1)
		{
			TelemetryClock = 0.0;
			FTorqueTelemetry Sample;
			Sample.Speed = (float)FMath::Abs(Sim->Speed);
			Sample.Rpm = (float)Sim->Rpm;
			Sample.Throttle = (float)Throttle;
			Sample.FrontBrake = (float)FrontBrake;
			Sample.RearBrake = (float)RearBrake;
			Sample.Lean = (float)FMath::RadiansToDegrees(Sim->LeanAngle);
			Sample.Gear = Sim->Gear;
			if (Telemetry.Num() >= 120) Telemetry.RemoveAt(0);
			Telemetry.Add(Sample);
		}

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
	const bool bGroups = bHaveModel || bCatalogueModel;   // the model's parts are on the groups
	const FQuat SteerQ(bGroups ? SteerDir : SteerAxis, Sim->Steer);
	// A turn about +y by a positive angle carries the top of the wheel forwards: rolling ahead.
	FrontPivot->SetRelativeLocation(BodyToLocal(F.Centre));
	FrontPivot->SetRelativeRotation(SteerQ * FQuat(FVector::RightVector, F.Spin));
	RearPivot->SetRelativeLocation(BodyToLocal(R.Centre));
	RearPivot->SetRelativeRotation(FQuat(FVector::RightVector, R.Spin));
	if (!bGroups) return;

	// Bars, yokes and lamp turn about the steering axis; the fork lowers follow the axle.
	const FQuat Turn(SteerDir, Sim->Steer);
	SteerGroup->SetRelativeLocationAndRotation(SteerPoint - Turn.RotateVector(SteerPoint), Turn);
	FrontHub->SetRelativeLocationAndRotation(BodyToLocal(F.Centre), SteerQ);
	// The swingarm swings about its pivot to point at the rear axle.
	const FVector Rest = RearRest - SwingPivot, Now = BodyToLocal(R.Centre) - SwingPivot;
	const double Swing = FMath::Atan2(Now.Z, Now.X) - FMath::Atan2(Rest.Z, Rest.X);
	SwingGroup->SetRelativeLocationAndRotation(SwingPivot, FQuat(FVector::RightVector, -Swing));
	// Geometry mode's offsets on the wheels' parts: in the fork's or swingarm's frame, under the spin.
	for (const FTorqueRideFit& Fit : RideFit)
	{
		UStaticMeshComponent* C = Fit.Part.Get();
		if (!C || !Fit.Spin || !Fit.Carrier) continue;
		const FTransform X = Fit.Spin->GetRelativeTransform() * Fit.Carrier->GetRelativeTransform().Inverse();
		C->SetRelativeTransform(Fit.Rest * X * Fit.Move * X.Inverse());
	}
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
		ShowRiderAvatar();
		return;
	}
	if (RiderParts.Num() < RP_Count) return;
	bHeadShown = bShow;
	for (int32 I : {RP_Helmet, RP_Visor, RP_Neck}) RiderParts[I]->SetVisibility(bShow);
}

void ATorqueBikePawn::ShowRiderAvatar()
{
	if (!RiderAvatar.IsValid()) return;
	if (RiderBody) RiderBody->SetVisibility(false);
	// Hair stays hidden: the helmet's on.
	RiderAvatar.SetShown(bRiderShown, bRiderShown && bHeadShown, false);
}

void ATorqueBikePawn::UpdateRiderAvatar()
{
	if (FTorqueAvatar::Count() == 0 || !RiderRig.IsValid() || !RiderRig.Last.bValid) return;
	static const bool bKit = FParse::Param(FCommandLine::Get(), TEXT("TorqueKitRider"));
	if (bKit) return;
	// Whose: this machine's own profile, or the player riding it in co-op.
	FTorqueProfile Look;
	int32 Colour = 0;
	const ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	if (IsLocallyControlled() || !PS)
	{
		Look = FTorqueProfile::Mine();
		Colour = FTorqueSettings::Get().Colour;
	}
	else
	{
		Look = FTorqueProfile::Unpack(PS->Profile);
		Colour = PS->Colour;
	}
	const int32 Want = Look.AvatarFor(Colour);
	if (Want != RiderAvatarFor)
	{
		RiderAvatarFor = Want;
		RiderAvatarLook.Reset();
		if (!RiderAvatar.Build(this, RiderRoot, Want))
		{
			if (RiderBody) RiderBody->SetVisibility(bRiderShown);
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("Torque: riding as MetaHuman %s"), *RiderAvatar.Name);
		ShowRiderAvatar();
	}
	if (!RiderAvatar.IsValid()) return;
	const TArray<uint8> Packed = Look.Pack();
	if (Packed != RiderAvatarLook)
	{
		RiderAvatarLook = Packed;
		RiderAvatar.SetLook(Look);
	}
	// Where the rider rig put its joints on this bike: the MetaHuman reaches for the same.
	const FRiderPoseOut& R = RiderRig.Last;
	FTorqueRideTargets T;
	T.Hip = R.Hip;
	T.Neck = R.Neck;
	T.Look = R.Look;
	T.Limp = R.Limp;
	for (int32 S = 0; S < 2; ++S)
	{
		T.Wrist[S] = R.Wrist[S];
		T.Grip[S] = R.Grip[S];
		T.Ankle[S] = R.Ankle[S];
		T.LeverFingers[S] = R.LeverFingers[S];
		T.LeverPull[S] = R.LeverPull[S];
	}
	RiderAvatar.PoseRiding(T);
	// The helmet on its head, and the eyes (first person) in the helmet.
	FVector At;
	FQuat Turn;
	if (RiderAvatar.HeadInParent(At, Turn))
	{
		if (RiderHelmet) RiderHelmet->SetRelativeTransform(RiderRig.HelmetOnHead() * FTransform(Turn, At));
		HeadLocal = At + Turn.RotateVector(RiderRig.HelmetCentreOnHead());
	}
}

void ATorqueBikePawn::ShowRider(bool bShow)
{
	if (bShow == bRiderShown) return;
	if (RiderRig.IsValid())
	{
		bRiderShown = bShow;
		if (RiderBody) RiderBody->SetVisibility(bShow);
		if (RiderHelmet) RiderHelmet->SetVisibility(bShow && bHeadShown);
		ShowRiderAvatar();
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

// The rider (made sat on the Speed Twin, Tools/make_rider.py) sat on a catalogue bike as Torque Shed
// sits its own (bike_model.gd _build_rider: hips on BikeRig's hip point, feet on its pegs, hands on
// the bars' grips): everything moved by the seat's, pegs' and grips' places against the Speed
// Twin's, and the back leant forward or back about the hips until the hands reach the grips as far
// as they do on the Speed Twin (a racer's crouch over clip-ons, a chopper's sit-up to high bars).
void ATorqueBikePawn::FitRiderToBike()
{
	bRiderFitted = true;
	if (!bRiderOnLayout || !RiderRig.IsValid()) return;
	// The Speed Twin's, as make_rider.py sat the rider: the seat's top under the hips, the pegs.
	static const FVector TwinSeat(-34.0, 0.0, 52.4);
	static const FVector TwinPeg[2] = {FVector(-22.0, -17.0, 4.0), FVector(-22.0, 17.0, 4.0)};
	FVector Neck0, Shoulder0[2], Wrist0[2], Ankle0[2];
	const FVector Pelvis0 = RiderRig.PelvisRest();
	if (!RiderRig.BoneRest(TEXT("neck_01"), Neck0)) return;
	for (int32 S = 0; S < 2; ++S)
	{
		const TCHAR* Side = S == 0 ? TEXT("l") : TEXT("r");
		if (!RiderRig.BoneRest(*FString::Printf(TEXT("upperarm_%s"), Side), Shoulder0[S]) ||
			!RiderRig.BoneRest(*FString::Printf(TEXT("hand_%s"), Side), Wrist0[S]) ||
			!RiderRig.BoneRest(*FString::Printf(TEXT("foot_%s"), Side), Ankle0[S])) return;
	}
	const FVector SeatMove = RiderSeat - TwinSeat;
	RiderSeatFit = FTransform(SeatMove);
	PoseHip = Pelvis0 + SeatMove;
	for (int32 S = 0; S < 2; ++S)
	{
		RiderPegFit[S] = FTransform(RiderPeg[S] - TwinPeg[S]);
		RiderGripFit[S] = FTransform(RiderGrip[S] - RiderRig.GripRest(S));
		PoseWrist[S] = RiderGripFit[S].TransformPosition(Wrist0[S]);
		PoseAnkle[S] = RiderPegFit[S].TransformPosition(Ankle0[S]);
	}
	// The back's lean as Torque Shed has it: by the rider's riding style (RiderStyle.INFO's torso,
	// hips to shoulders; the profile's choice there, -TorqueRiderStyle= here, situp by default), most
	// of the way up to LAID_BACK_TORSO on cruiser and chopper seating (feet forward). Not by the bike:
	// a sport bike's rider crouches only as a scratcher. Godot's arms then stretch 0.85..1.15 to the
	// grips; the rig's can't, so where the grips are out of reach (past 0.95 of the arm) the back
	// leans on forward until they're in it, and where they're very near (under 0.6, the elbows
	// folded: an MX's bars) it sits back up, to upright at most.
	struct FStyleTorso { const TCHAR* Name; double Up, Fwd; };
	static const FStyleTorso Torsos[] = {{TEXT("situp"), 0.57, 0.13}, {TEXT("scratcher"), 0.42, 0.4}, {TEXT("naked"), 0.45, 0.37}, {TEXT("rossi"), 0.45, 0.35}};
	// [co-op] Settings > Character > Riding style (or -TorqueRiderStyle=), another player's from theirs.
	const FString Style = FTorqueSettings::RiderStyleFor(this);
	FStyleTorso Torso = Torsos[0];
	for (const FStyleTorso& T : Torsos)
		if (Style.Equals(T.Name, ESearchCase::IgnoreCase)) Torso = T;
	RiderStyle = Torso.Name;
	if (bRiderLaidBack)
	{
		Torso.Up = FMath::Lerp(Torso.Up, 0.56, 0.75);
		Torso.Fwd = FMath::Lerp(Torso.Fwd, 0.05, 0.75);
	}
	const double RestLean = FMath::RadiansToDegrees(FMath::Atan2(Neck0.X - Pelvis0.X, Neck0.Z - Pelvis0.Z));
	const double StyleLean = FMath::RadiansToDegrees(FMath::Atan2(Torso.Fwd, Torso.Up));
	RiderStyleDeg = StyleLean;
	auto Tilt = [](const FVector& V, double Deg) {
		const double A = FMath::DegreesToRadians(Deg), C = FMath::Cos(A), Sn = FMath::Sin(A);
		return FVector(V.X * C + V.Z * Sn, V.Y, -V.X * Sn + V.Z * C);
	};
	FVector Elbow0[2], Thigh0[2], Knee0[2];
	for (int32 S = 0; S < 2; ++S)
	{
		const TCHAR* Side = S == 0 ? TEXT("l") : TEXT("r");
		if (!RiderRig.BoneRest(*FString::Printf(TEXT("lowerarm_%s"), Side), Elbow0[S]) ||
			!RiderRig.BoneRest(*FString::Printf(TEXT("thigh_%s"), Side), Thigh0[S]) ||
			!RiderRig.BoneRest(*FString::Printf(TEXT("calf_%s"), Side), Knee0[S])) return;
	}
	RiderArm = (Elbow0[1] - Shoulder0[1]).Size() + (Wrist0[1] - Elbow0[1]).Size();
	RiderLeg = (Knee0[1] - Thigh0[1]).Size() + (Ankle0[1] - Knee0[1]).Size();
	auto Reach = [&](double Deg, double& MinReach, double& MaxReach) {
		MinReach = TNumericLimits<double>::Max();
		MaxReach = 0.0;
		for (int32 S = 0; S < 2; ++S)
		{
			RiderReach[S] = (PoseWrist[S] - (PoseHip + Tilt(Shoulder0[S] - Pelvis0, Deg))).Size();
			MinReach = FMath::Min(MinReach, RiderReach[S]);
			MaxReach = FMath::Max(MaxReach, RiderReach[S]);
		}
	};
	double Deg = StyleLean - RestLean, MinReach = 0.0, MaxReach = 0.0;
	Reach(Deg, MinReach, MaxReach);
	while (MaxReach > 0.95 * RiderArm && Deg + RestLean < 60.0) Reach(Deg += 0.25, MinReach, MaxReach);
	while (MinReach < 0.6 * RiderArm && MaxReach < 0.95 * RiderArm && Deg + RestLean > 0.0)
	{
		Reach(Deg - 0.25, MinReach, MaxReach);
		if (MaxReach > 0.95 * RiderArm) { Reach(Deg, MinReach, MaxReach); break; }
		Deg -= 0.25;
	}
	RiderBackDeg = Deg + RestLean;   // forward of upright, as the log says it
	// The rig turns the back up its spine by shares (RiderRig::Pose: the pelvis 0.15 of the turn,
	// then 0.4, 0.75, and the top of the spine and the neck all of it), so the line from the hips to
	// the neck turns only about two thirds as far as the chest is told. Tell it further, so the
	// back as drawn leans as far as the style says (found by halving on the same chain of bones).
	double TurnDeg = Deg;
	{
		FVector Chain[5] = {Pelvis0, Pelvis0, Pelvis0, Pelvis0, Neck0};
		const TCHAR* Names[3] = {TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03")};
		bool bChain = true;
		for (int32 K = 0; K < 3; ++K) bChain = RiderRig.BoneRest(Names[K], Chain[K + 1]) && bChain;
		if (bChain)
		{
			const double Share[4] = {0.15, 0.4, 0.75, 1.0};
			auto DrawnLean = [&](double Turn) {
				FVector At = Chain[0];
				for (int32 K = 0; K < 4; ++K) At += Tilt(Chain[K + 1] - Chain[K], Turn * Share[K]);
				return FMath::RadiansToDegrees(FMath::Atan2(At.X - Chain[0].X, At.Z - Chain[0].Z));
			};
			double Lo = -90.0, Hi = 90.0;
			for (int32 It = 0; It < 40; ++It)
			{
				const double Mid = 0.5 * (Lo + Hi);
				(DrawnLean(Mid) < RiderBackDeg ? Lo : Hi) = Mid;
			}
			TurnDeg = 0.5 * (Lo + Hi);
		}
	}
	RiderChest = PoseHip + Tilt(Neck0 - Pelvis0, TurnDeg) - Neck0;
	for (int32 S = 0; S < 2; ++S) RiderLegReach[S] = (PoseAnkle[S] - (PoseHip + (Thigh0[S] - Pelvis0))).Size();
	UE_LOG(LogTemp, Log, TEXT("Torque: the rider sat on the catalogue bike (%s%s): hips %s, wrists %s / %s, ankles %s / %s, back %.1f deg forward of upright; ")
		TEXT("reach %.1f / %.1f cm of a %.1f cm arm, hip to ankle %.1f / %.1f of a %.1f cm leg"),
		*RiderStyle, bRiderLaidBack ? TEXT(", laid back") : TEXT(""), *PoseHip.ToString(), *PoseWrist[0].ToString(), *PoseWrist[1].ToString(),
		*PoseAnkle[0].ToString(), *PoseAnkle[1].ToString(), RiderBackDeg, RiderReach[0], RiderReach[1], RiderArm,
		RiderLegReach[0], RiderLegReach[1], RiderLeg);
}

void ATorqueBikePawn::UpdateRider()
{
	// [co-op] A new riding style (chosen in Settings, or another player's arriving): sit again.
	if (bRiderFitted && !RiderStyle.IsEmpty() && FTorqueSettings::RiderStyleFor(this) != RiderStyle) bRiderFitted = false;
	if (RiderRig.IsValid() && bRiderOnLayout && !bRiderFitted) FitRiderToBike();
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
		if (bRiderOnLayout) In.ChestShift += RiderChest;   // sat on a catalogue bike (FitRiderToBike)
		In.HipShift = Hang * 2.0 + 2.5 * LeanN;
		In.BarPivot = (bHaveModel || bCatalogueModel) ? SteerPoint : FVector(68.0, 0.0, -1.0);
		const FVector Axis = (bHaveModel || bCatalogueModel) ? SteerDir : FVector(-FMath::Sin(FMath::DegreesToRadians(Sim->Build.RakeDeg)), 0.0,
			FMath::Cos(FMath::DegreesToRadians(Sim->Build.RakeDeg)));
		In.BarTurn = FQuat(Axis, Sim->Steer);
		// Feet go down as the bike stops: ankles a little forward of the pegs and out, on the road.
		In.FeetDown = FMath::Clamp(1.0 - FMath::Abs(Sim->Speed) / 1.8, 0.0, 1.0);
		In.FootDownL = FVector(-32.0, -36.0, -22.0);
		In.FootDownR = FVector(-32.0, 36.0, -22.0);
		if (bRiderOnLayout)
		{
			// On a catalogue bike: down from its own pegs, to its own ground (the Speed Twin's axles
			// are 31 cm up, this one's its rear wheel's radius).
			const FVector Down(RiderPeg[1].X - (-22.0), 0.0, 31.0 - Sim->Build.RearRadius * 100.0);
			In.FootDownL += Down;
			In.FootDownR += Down;
		}
		// The head turns toward where the corner goes (the apex) and stays nearer level than the bike.
		// Unreal: + yaw looks right, + roll drops the right side.
		const double HeadLean = FMath::Clamp(BodyLean, -55.0, 55.0);
		// (Settings > Gameplay > Look into corners: off, the head stays square to the bike.)
		const double IntoCorner = FTorqueSettings::Get().bLookIntoCorner ? 0.45 : 0.0;
		In.Look = FRotator(LookPitch * -0.4 - 3.0 * Braking, LookYaw * 0.5 + IntoCorner * HeadLean, -0.45 * HeadLean);
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
			LeverParts[S]->SetRelativeTransform(LeverBase[S] * FTransform(-LeverPivot[S]) * FTransform(Q, LeverPivot[S]) * LeverFit[S]);
		}
		In.GripFit[0] = GripDelta[0] * GripFitMove[0];
		In.GripFit[1] = GripDelta[1] * GripFitMove[1];
		In.SeatFit = SeatFitMove;
		In.PegFit[0] = In.PegFit[1] = PegFitMove;
		if (bRiderOnLayout)
		{
			// (Geometry mode's offsets are in these already: FitRiderToBike's points are as drawn.)
			for (int32 S = 0; S < 2; ++S)
			{
				In.GripFit[S] = RiderGripFit[S];
				In.PegFit[S] = RiderPegFit[S];
			}
			In.SeatFit = RiderSeatFit;
		}
		RiderRig.Pose(In);
		HeadLocal = RiderRig.HelmetCentre;
		UpdateRiderAvatar();
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
	const FVector Axis = (bHaveModel || bCatalogueModel) ? SteerDir : FVector(-FMath::Sin(FMath::DegreesToRadians(Sim->Build.RakeDeg)), 0.0,
		FMath::Cos(FMath::DegreesToRadians(Sim->Build.RakeDeg)));
	const FVector Pivot = (bHaveModel || bCatalogueModel) ? SteerPoint : FVector(68.0, 0.0, -1.0);
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
	// The bike's own engine, as Torque Shed makes it from its engine, head, exhaust, air filter,
	// gearbox and plugs (EngineSynth), in place of the made 270 twin: every bike from the part list.
	std::shared_ptr<TorqueEngine::FBank> Engine = RideBay >= 0 ? UTorqueSfx::BikeEngine(this, RideBay) : nullptr;
	if (Engine)
	{
		Sound->SetEngine(Engine);
		UE_LOG(LogTemp, Log, TEXT("Torque: riding with Torque Shed's engine sound %s"), UTF8_TO_TCHAR(Engine->Sig.c_str()));
	}
	UpdateSound(0.0f);   // a little queued before it starts
	// Torque Shed's start: the starter motor cranks it over (Simon's recordings), or it's kicked
	// over (made for the engine), then it catches.
	UTorqueSfx* Sfx = UTorqueSfx::Get(this);
	if (Sfx && Engine && Engine->Spec.Kick)
	{
		Sfx->PlayMadeOn(TorqueEngine::RenderStarter(Engine->Spec, true, 1.0, FMath::RandRange(1, 3)), TorqueEngine::Rate, Root, FVector(0.0, 0.0, 45.0));
		EngineCatchIn = 0.5;
	}
	else if (Sfx && Sfx->PlayOn(TEXT("starter"), Root, FVector(0.0, 0.0, 45.0), 0.0f, 1.3f))
	{
		EngineCatchIn = 1.1;
	}
	else
	{
		SoundAudio->Play();
		Sound->Catch();
	}
	if (Sfx) HornId = Sfx->StartBikeHorn(Root, UTorqueBuildSubsystem::BikeFor(this));
}

void ATorqueBikePawn::UpdateSound(float Dt)
{
	if (!Sound || !SoundWave) return;
	if (EngineCatchIn > 0.0)
	{
		EngineCatchIn -= Dt;
		if (EngineCatchIn > 0.0) return;
		SoundAudio->Play();   // it's caught: the engine from here
		Sound->Catch();
	}
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
		const FVector Pos = Centre + (Flat * FMath::Cos(Yaw) + Right * FMath::Sin(Yaw)) * OrbitDist
			+ FVector(0, 0, 55.0 - LookPitch * 4.0);
		Camera->SetWorldLocationAndRotation(Pos, (Centre - Pos).Rotation());
		Camera->SetFieldOfView((float)OrbitFov);
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
		// Looking into corners: your view turns towards the inside of the bend, as the rider's head does.
		if (FTorqueSettings::Get().bLookIntoCorner) Rot.Yaw += 0.25 * FMath::Clamp(BodyLean, -55.0, 55.0);
		Camera->SetWorldLocationAndRotation(Eye, Rot);
		Camera->SetFieldOfView(FTorqueSettings::Get().FovFor(bChin ? 100.0f : 108.0f));   // Settings > Gameplay > Field of view
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
	Camera->SetFieldOfView(FTorqueSettings::Get().FovFor(72.0f));   // Settings > Gameplay > Field of view
}

void ATorqueBikePawn::DrawHud()
{
	const FTorqueSettings& Set = FTorqueSettings::Get();   // Settings > Gameplay > Units
	const double DoneM = Line.Num() > 1 ? RouteLengthM * Near / (Line.Num() - 1) : 0.0;
	const int32 Mins = (int32)(RideTime / 60.0);
	const double Secs = RideTime - Mins * 60.0;
	HudLines.Reset();
	auto Add = [this](const FString& T, FColor C, float Size) { HudLines.Add({T, C, Size}); };
	Add(FString::Printf(TEXT("%s   gear %d"), *Set.SpeedText(Sim->Speed), Sim->Gear), FColor::White, 2.0f);
	Add(FString::Printf(TEXT("%s gears   ABS %s"), bManual ? TEXT("Manual") : TEXT("Automatic"),
		!(bHasAbs[0] || bHasAbs[1]) ? TEXT("none fitted") : bAbs ? TEXT("on") : TEXT("off")),
		GearNoteFor > 0.0 ? FColor(255, 220, 120) : FColor(200, 200, 200), GearNoteFor > 0.0 ? 1.2f : 0.95f);
	Add(FString::Printf(TEXT("%.0f rpm   lean %.0f deg"), Sim->Rpm, FMath::Abs(FMath::RadiansToDegrees(Sim->LeanAngle))),
		FColor(220, 220, 220), 1.1f);
	Add(FString::Printf(TEXT("Buttertubs  %s of %s   %d:%05.2f%s"), *Set.DistanceText(DoneM), *Set.DistanceText(RouteLengthM), Mins, Secs,
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
		Add(TEXT("      R1 camera   Triangle back on road   D-pad up/down realism, right weather, left horn   Options menu"), Help, 0.85f);
		// The keys as they're bound now (Settings > Controller > Key bindings).
		auto K = [](ETorqueKey A) { return FTorqueKeys::Text(A); };
		Add(FString::Printf(TEXT("Keys: %s throttle   %s brake   %s rear brake   %s / %s lean   %s / %s weight   %s / %s gear up/down   %s auto/manual"),
			*K(ETorqueKey::Throttle), *K(ETorqueKey::FrontBrake), *K(ETorqueKey::RearBrake), *K(ETorqueKey::LeanLeft), *K(ETorqueKey::LeanRight),
			*K(ETorqueKey::WeightBack), *K(ETorqueKey::WeightForward), *K(ETorqueKey::ShiftUp), *K(ETorqueKey::ShiftDown), *K(ETorqueKey::AutoShift)), Help, 0.85f);
		Add(FString::Printf(TEXT("      %s ABS   %s camera   %s look back   %s back on road   %s restart   %s / %s realism   %s weather   %s horn   %s telemetry"),
			*K(ETorqueKey::Abs), *K(ETorqueKey::Camera), *K(ETorqueKey::LookBack), *K(ETorqueKey::BackOnRoad), *K(ETorqueKey::Restart),
			*K(ETorqueKey::RealismDown), *K(ETorqueKey::RealismUp), *K(ETorqueKey::Weather), *K(ETorqueKey::Horn), *K(ETorqueKey::Telemetry)), Help, 0.85f);
		Add(FString::Printf(TEXT("      %s performance   %s log it   %s help   %s shed   Esc menu"),
			*K(ETorqueKey::Perf), *K(ETorqueKey::PerfLog), *K(ETorqueKey::Help), *K(ETorqueKey::BackToShed)), Help, 0.85f);
	}
}


// Weather: ATorqueGameState lights the world for it (the host's weather, in co-op); the bike takes
// a third less grip on wet roads.
void ATorqueBikePawn::ApplyWeather()
{
	if (Sim) Sim->Build.WetGrip = Weather == 2 ? 0.68 : 1.0;
	UE_LOG(LogTemp, Log, TEXT("Torque: bike in weather %d"), Weather);
}
