#include "TorqueBikePawn.h"
#include "Coop/TorquePing.h"
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
#include "Shed/TorqueYard.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueShedLayout.h"
#include "World/TorqueRideWorld.h"
#include "World/TorqueWorldSubsystem.h"

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
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
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
#include "DrawDebugHelpers.h"
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
#include "Net/UnrealNetwork.h"

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
// Tools/ue_build_level.py), never walls, grass, sheep or anything else in the way. A part
// tagged Road counts too: a bike lift's platform and ramp while it's down (ATorqueBikeLift),
// so a bike ridden into the wash bay rolls up onto its lift.
bool GroundTrace(UWorld* World, const FVector& From, const FVector& To, const FCollisionQueryParams& Params, FHitResult& Out)
{
	FCollisionQueryParams P = Params;
	for (int32 Try = 0; Try < 8; ++Try)   // step past anything else that blocks, a few at most
	{
		if (!World->LineTraceSingleByChannel(Out, From, To, ECC_Visibility, P)) return false;
		const AActor* A = Out.GetActor();
		if (A && (A->ActorHasTag(TEXT("Land")) || A->ActorHasTag(TEXT("Road")))) return true;
		const UPrimitiveComponent* Part = Out.GetComponent();
		if (Part && Part->ComponentHasTag(TEXT("Road"))) return true;
		if (!A) return false;
		// (An actor some of whose parts are ground, tagged RoadParts: only this part is passed, as another
		// of its parts under it may be the ground. Anything else is passed whole, as before.)
		if (Part && A->ActorHasTag(TEXT("RoadParts"))) P.AddIgnoredComponent(Part);
		else P.AddIgnoredActor(A);
	}
	return false;
}
} // namespace

namespace TorqueRiderSeat
{
// The Speed Twin's seat top under the rider's hips, as Tools/make_rider.py sat the rider on it.
const FVector TwinSeat(-34.0, 0.0, 52.4);
} // namespace TorqueRiderSeat

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

void ATorqueBikePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATorqueBikePawn, bYardStart);   // [yard]
	DOREPLIFETIME(ATorqueBikePawn, YardStartAt);
	DOREPLIFETIME(ATorqueBikePawn, YardStartYaw);
	DOREPLIFETIME(ATorqueBikePawn, YardBay);   // [co-op] whose bike it is, there before BeginPlay on every machine
}

void ATorqueBikePawn::BeginPlay()
{
	Super::BeginPlay();
	// Settings > Gameplay > Riding realism, once the player has saved settings (test runs keep their own).
	if (FTorqueSettings::Get().bSaved && !FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"))) Assist = FTorqueSettings::Get().Realism;
	// Settings > Gameplay > Automatic gearbox: how each ride starts (G / R3 changes it while riding).
	bManual = !FTorqueSettings::Get().bAutoShift;
	Sim->Setup(BikeSpec::SpeedTwin1200());
	WeatherHeat(true);
	Sim->Ray = [this](const V3& From, const V3& To) {
		RayHit Out;
		if (bTestFlat)
		{
			// The test pad (TestStartMove): level tarmac at TestPadY, as SimTest's flat ground.
			if (From.y >= TestPadY && To.y <= TestPadY && From.y > To.y)
			{
				Out.Hit = true;
				Out.Position = From + (To - From) * ((From.y - TestPadY) / (From.y - To.y));
				Out.Normal = V3(0.0, 1.0, 0.0);
				Out.Surface = SURF_TARMAC;
			}
			return Out;
		}
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
			const UPrimitiveComponent* Part = Hit.GetComponent();
			Out.Surface = ((A && A->ActorHasTag(TEXT("Road"))) || (Part && Part->ComponentHasTag(TEXT("Road")))) ? SURF_TARMAC : SURF_GRASS;
			// [Ride UK] what the road there is made of: paint and tar, grit, iron covers, a bridge's joints, rails.
			int32 Kind = 0;
			double Lift = 0.0;
			if (Out.Surface == SURF_TARMAC && ATorqueRideWorld::SurfaceUnder(Hit.ImpactPoint, Kind, Lift))
			{
				Out.Surface = Kind == 1 ? SURF_SLICK : Kind == 2 || Kind == 4 ? SURF_GRAVEL : Kind == 3 ? SURF_STEEL : SURF_TARMAC;   // [OSM stage 1] 4: a gravel road
				Out.Lift = Lift;
			}
		}
		return Out;
	};
	// [yard] In the shed's map the bike is ridden on the concrete outside its door: the yard's route
	// and walls beside each other in Data/Shed/Yard (Tools/shed_yard.py), as the ride's are.
	if (UWorld* World = GetWorld())
	{
		bYard = World->GetMapName().Contains(TEXT("Shed"));
		for (TActorIterator<ATorqueShedBike> It(World); It && !bYard; ++It) bYard = true;
		if (bYard) RouteFile = TEXT("Data/Shed/Yard/route.json");
		// The real world's roads (Ride UK): the route the world subsystem wrote when it planned the ride.
		if (!bYard && World->GetMapName().Contains(TEXT("RideUK"))) RouteFile = UTorqueWorldSubsystem::RideRouteFile();
	}
	LoadRoute();
	LoadContacts();
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
	// [yard] Got on in the shed: from where the bike stood (sim heading 0 faces -z, + turning left).
	if (bYardStart)
	{
		const FVector Ahead = FRotator(0.0, YardStartYaw, 0.0).Vector();
		PlaceOnGround(ToSim(YardStartAt), std::atan2(-Ahead.X, -Ahead.Y));
	}
	// The weather comes from ATorqueGameState (-TorqueWeather= is read there), each frame in Tick.
	// The PlayStation pad is opened when this turns out to be our own bike (ReadInput).
	StartSound();
	// -TorqueCamera=1 starts in first person (2: chin camera, 3: showroom orbit of the bike with the
	// rider hidden), for checking the views. -TorqueOrbitYaw=N holds the orbit at N degrees from
	// straight ahead (positive to the bike's right) instead of turning, for repeatable screenshots.
	// Otherwise the view you last rode in (saved with your character).
	// (Test runs that ride by themselves keep the chase camera they were written for.)
	if (!FParse::Param(FCommandLine::Get(), TEXT("TorqueAutoRide")) && !FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")))
		CameraMode = FTorqueProfile::Mine().RideView;
	int32 StartCamera = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueCamera="), StartCamera)) CameraMode = FMath::Clamp(StartCamera, 0, CameraModes - 1);
	float FixedYaw = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueOrbitYaw="), FixedYaw)) { OrbitYaw = FixedYaw; bOrbitHeld = true; }
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueProfile"))) { bPerfShow = true; bPerfLog = true; }
}

void ATorqueBikePawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsLocallyControlled()) FSonyPad::SetRumble(0.0f, 0.0f);   // [Ride UK] (the pad still)
	Pad.Reset();   // stops its reading thread
	if (NetWalker) NetWalker->Destroy();   // [co-op, foot]
	if (TripLogTime > 0.0) TripLog(TEXT("ride over"));   // [dash]
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
	UE_LOG(LogTemp, Log, TEXT("Torque: the bike's lamps: none (not a part-list bike, so no lenses to light)"));   // [lights]
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
// works it out for the dyno (TorqueShedPower), all with the dyno desk's tune. False if the parts or
// the engine won't do.
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
	// The dyno desk's tune (pressures, sprockets, clickers, ratio spacing, rider aids, steering
	// damper), the chain's slack and the parts drilled and trimmed for lightness.
	for (const TPair<FName, float>& KV : B.TuneOf(Bay)) In.Tune[PawnStd(KV.Key.ToString())] = KV.Value;
	In.ChainSlack = B.ChainSlackOf(Bay);
	// [fluids] The petrol in its tanks and the oil in its engine, as the shed has them.
	if (B.FuelCapacityOf(Bay) > 0.0f) In.FuelLitres = B.FuelOf(Bay);
	if (const FTorqueItem* Sump = B.Item(B.ItemOn(Bay, FName(TEXT("engine"))))) In.OilLitres = Sump->Litres;
	for (const FName& Slot : B.LightenedOf(Bay)) In.Lightened.push_back(PawnStd(Slot.ToString()));
	// [tyre] How well each tyre's bead was seated on the tyre changer.
	In.BeadFront = B.BeadOf(Bay, true);
	In.BeadRear = B.BeadOf(Bay, false);
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
	int32 Bay = YardBay >= 0 ? YardBay : UTorqueBuildSubsystem::BikeFor(this);
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
	TArray<TorqueShedCatalogue::FDashGauge> Gauges;   // [dash] the clocks' needles, shift lights and readout
	const int32 Drawn = TorqueShedCatalogue::MakeBike(this, Holder, Parts, ModelParts, AxleHeight, Geo, &Nodes, &Frame, &Gauges);
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
			// [rider pose] The knees move with the saddle (they sit by the tank's back, over the hips).
			RiderKnee[1] = MoveOf(SaddleTag).TransformPosition(Frame.Knee);
			RiderKnee[0] = MoveOf(SaddleTag).TransformPosition(FVector(Frame.Knee.X, -Frame.Knee.Y, Frame.Knee.Z));
			RiderTankHalfW = Frame.TankHalfW;
			RiderStyleBackDeg = Frame.BackDeg;
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
			// The tank's back edge as drawn (the rider's crotch is kept behind it).
			static const FName TankTag(TEXT("Slot:tank"));
			FBox TankBox(ForceInit);
			for (UStaticMeshComponent* C : ModelParts)
				if (C && C->GetStaticMesh() && C->ComponentHasTag(TankTag))
					TankBox += C->GetStaticMesh()->GetBoundingBox().TransformBy(C->GetRelativeTransform());
			RiderTankRearX = TankBox.IsValid ? TankBox.Min.X : 1.0e9;
			UE_LOG(LogTemp, Log, TEXT("Torque: rider seat %s, tank's back edge x %.1f"), *RiderSeat.ToCompactString(), TankBox.IsValid ? TankBox.Min.X : 0.0);
			bRiderLaidBack = Frame.bLaidBack;
			bRiderOnLayout = true;
			bRiderFitted = false;
		}
		// The sim's body frame is the drawn bike's (the origin at axle height midway between the
		// axles, a wheelbase apart: SimTest/spec_test checks the layout and the spec agree).
		Sim->Setup(Spec);
		WeatherHeat(true);   // out of the shed: cold tyres, brakes and engine
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
	// [dash] Its dashboard: what the dash part shows, the tank, the engine's thirst, and its clocks
	// (on their groups now, so their rest is where they ride).
	Dash.Setup(Parts);
	// [fluids] The petrol it rides out with is what's in its tank in the shed (filled at the pump).
	if (B->FuelCapacityOf(Bay) > 0.0f) Dash.FuelLitres = FMath::Clamp((double)B->FuelOf(Bay), 0.0, Dash.FuelCapacity);
	Dash.HookClocks(this, ModelParts, Gauges, ShapeCube);
	// [lights] Its lamps: the headlight, fog lights, indicators and rear light on their lenses.
	Lights.Setup(Parts);
	Lights.Hook(this, ModelParts, true);
	SideStandPart = Parts.FindRef(TEXT("side_stand"));
	CentreStandPart = Parts.FindRef(TEXT("centre_stand"));
	StandRest.Reset();
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
		// [rider pose] The MetaHuman as it sat: its back, knees and elbows, how far its hands and ankles
		// fell short of their marks, and its knees' distance out from the bike's middle.
		if (RiderAvatar.IsValid())
		{
			const FTorqueAvatar::FRideReport& Rp = RiderAvatar.RideReport;
			double TankWidest = 0.0;
			for (const FVector2D& TankAt : RiderTankHalfW) TankWidest = FMath::Max(TankWidest, 2.0 * TankAt.Y);
			Out += FString::Printf(TEXT("\nrider avatar %s (%s pose): back %.1f deg forward of upright, knees %.0f / %.0f deg, elbows %.0f / %.0f deg, ")
				TEXT("hands %.1f / %.1f cm short, ankles %.1f / %.1f cm short, knees %.1f / %.1f cm out (pads at %.1f, tank %.1f wide at most)"),
				*RiderAvatar.Name, FTorqueAvatar::bOldRidePose ? TEXT("old") : TEXT("fitted"), Rp.BackDeg, Rp.KneeDeg[0], Rp.KneeDeg[1], Rp.ElbowDeg[0], Rp.ElbowDeg[1],
				Rp.HandShort[0], Rp.HandShort[1], Rp.AnkleShort[0], Rp.AnkleShort[1], Rp.KneeOut[0], Rp.KneeOut[1], FMath::Abs(RiderKnee[1].Y), TankWidest);
			// The hands on the controls: the right grip's roll with the throttle, and the index and
			// middle fingers' curl each side (they lift and pull with the levers).
			Out += FString::Printf(TEXT("\nrider hands: throttle %.2f, grip roll %.1f / %.1f deg, index curl %.0f / %.0f deg, middle curl %.0f / %.0f deg, levers held %.2f / %.2f, pulled %.2f / %.2f"),
				TestGripThrottleHold >= 0.0 ? TestGripThrottleHold : Throttle, Rp.GripRollDeg[0], Rp.GripRollDeg[1], Rp.IndexCurlDeg[0], Rp.IndexCurlDeg[1], Rp.MiddleCurlDeg[0], Rp.MiddleCurlDeg[1],
				RiderRig.Last.LeverFingers[0], RiderRig.Last.LeverFingers[1], RiderRig.Last.LeverPull[0], RiderRig.Last.LeverPull[1]);
			// Stopped on a tall bike: leant onto the left foot, the right kept on its peg.
			Out += FString::Printf(TEXT("\nrider stop: feet down %.2f, bike leant %.1f deg onto the left foot, right foot on its peg %.2f, leg %.1f cm, left hip joint %s, ground at z %.1f"),
				RiderFeetDown, StopLeanDeg, RiderKeepOnPeg[1], Rp.LegReach, *Rp.HipJointAt[0].ToCompactString(), -Sim->Build.RearRadius * 100.0);
		}
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

void ATorqueBikePawn::LoadContacts()
{
	// Beside the route: Data/Buttertubs/collision.json, boxes as [cx, cz, ux, uz, hu, hv, y0, y1].
	if (!FPaths::IsRelative(RouteFile)) return;   // (a real-world ride has no walls yet: bare ground and road)
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), FPaths::GetPath(RouteFile), TEXT("collision.json"));
	FString Text;
	TSharedPtr<FJsonObject> Json;
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)
		|| !Json.IsValid() || !Json->TryGetArrayField(TEXT("boxes"), Rows))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no collision boxes at %s (run Tools/make_collision.py); the bike rides through walls"), *Path);
		return;
	}
	std::vector<ContactBox> Boxes;
	Boxes.reserve(Rows->Num());
	for (const TSharedPtr<FJsonValue>& V : *Rows)
	{
		const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
		if (R.Num() < 8) continue;
		ContactBox Box;
		Box.Cx = R[0]->AsNumber();
		Box.Cz = R[1]->AsNumber();
		Box.Ux = R[2]->AsNumber();
		Box.Uz = R[3]->AsNumber();
		Box.Hu = R[4]->AsNumber();
		Box.Hv = R[5]->AsNumber();
		Box.Y0 = R[6]->AsNumber();
		Box.Y1 = R[7]->AsNumber();
		Boxes.push_back(Box);
	}
	Contacts = MakeUnique<ContactWorld>();
	Contacts->SetBoxes(Boxes);
	BaseBoxes.Reset();   // [furniture]
	for (const ContactBox& Box : Boxes) BaseBoxes.Add(Box);
	FurnitureSeen = 0;
	Sim->Contacts = Contacts.Get();
	UE_LOG(LogTemp, Log, TEXT("Torque: %d collision boxes"), Contacts->BoxCount());
}

void ATorqueBikePawn::GatherOtherBikes()
{
	if (!Contacts) return;
	// [furniture] In the shed's yard, the solid yard kit (walls, fences, the old car and buses) too.
	if (bYard && FurnitureSeen != TorqueFurniture::ContactVersion())
	{
		FurnitureSeen = TorqueFurniture::ContactVersion();
		std::vector<ContactBox> Boxes(BaseBoxes.GetData(), BaseBoxes.GetData() + BaseBoxes.Num());
		TorqueFurniture::ContactBoxes(this, Boxes);
		Contacts->SetBoxes(Boxes);
	}
	Contacts->Capsules.clear();
	// [co-op] Riders pass through each other (beenster, build 135: no clashing); -TorqueRiderCollide
	// puts the other bikes back as solid, for checking the contacts.
	static const bool bCollide = FParse::Param(FCommandLine::Get(), TEXT("TorqueRiderCollide"));
	if (!bCollide) return;
	for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It; ++It)
	{
		const ATorqueBikePawn* Other = *It;
		if (Other == this || !Other->Sim || Other->IsHidden() || Other->Sim->IsGhost()) continue;   // (a ghost passes through)
		Other->Sim->AsCapsules(Contacts->Capsules);
	}
}

void ATorqueBikePawn::LoadRoute()
{
	const FString Path = FPaths::IsRelative(RouteFile) ? FPaths::Combine(FPaths::ProjectDir(), RouteFile) : RouteFile;
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
	RouteFinishM = RouteLengthM;
	Json->TryGetNumberField(TEXT("finish_m"), RouteFinishM);   // (a planned ride's last pin: its road runs on past it)
	if (!FPaths::IsRelative(RouteFile)) Json->TryGetStringField(TEXT("name"), RouteName);   // (a real-world ride says what it is)
	const TArray<TSharedPtr<FJsonValue>>* Open = nullptr;   // [yard]
	if (Json->TryGetArrayField(TEXT("open_area_sim"), Open) && Open->Num() >= 4)
		YardOpen = FBox2D(FVector2D((*Open)[0]->AsNumber(), (*Open)[1]->AsNumber()), FVector2D((*Open)[2]->AsNumber(), (*Open)[3]->AsNumber()));
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

void ATorqueBikePawn::BackOnWheels()
{
	// [yard] Out on the open concrete: back on the bike where he came off, facing the way it was
	// going. (Against a wall, or round the back of the shed: back at the door.)
	const V3 P = Sim->Position;
	if (!bYard || !YardOpen.bIsValid || !YardOpen.IsInside(FVector2D(P.x, P.z)))
	{
		Respawn(bYard ? 0 : FMath::Max(Near - 2, 0));
		return;
	}
	const V3 Fwd = Sim->Basis * V3(0.0, 0.0, -1.0);
	PlaceOnGround(P, (Fwd.x * Fwd.x + Fwd.z * Fwd.z) > 1e-6 ? std::atan2(-Fwd.x, -Fwd.z) : 0.0);
}

void ATorqueBikePawn::Respawn(int32 LineIndex, double SideM)
{
	if (bOnFoot)
	{
		bOnFoot = bPushing = bUseArmed = false;   // back on the bike (the pause menu's Restart, a test)
		SetParkedLook(false);
	}
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
	PlaceOnGround(Pos, Heading);
}

void ATorqueBikePawn::PlaceOnGround(V3 Pos, double Heading)
{
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
	Sim->Gear = BikeSim::NEUTRAL;   // (beenster) every ride, and back on the road, starts in neutral
	Sim->Assist = Assist;
	Throttle = FrontBrake = RearBrake = Lean = Shift = 0.0;
	CrashedFor = 0.0;
	BodyLean = LongAcc = LastSpeed = 0.0;
	Remount();
	if (bCrashCam)
	{
		// Off the drive-by the fall switched to, back to the view you were riding in.
		bCrashCam = false;
		CameraMode = CrashCamFrom;
	}
	bDriveByPlaced = false;
	CurPos = PrevPos = ToUE(Sim->Position);
	CurRot = PrevRot = BasisToUE(Sim->Basis);
	Accum = 0.0;
	bCamInit = false;
	LastGear = Sim->Gear;   // no clunk for being put back in neutral
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
	if (InSlot <= 0 || Line.Num() < 2 || bYardStart) return;   // ([yard] got on where it stood: stays there)
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
		if (Sim->Gear == BikeSim::NEUTRAL) Sim->Gear = 1;   // (a ride starts in neutral; the robot sets off in first)
		static float AutoMph = 36.f;
		static const bool bMph = FParse::Value(FCommandLine::Get(), TEXT("TorqueAutoMph="), AutoMph);
		(void)bMph;
		const double Want = TestMove >= 0 ? TestMps : AutoMph / 2.23694;
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
		// A real-world ride's road ends where its route does (Buttertubs' runs on past the finish): the
		// robot slows for the end, as for a corner, and stops there.
		if (!FPaths::IsRelative(RouteFile) && Line.Num() > 1)
		{
			const double Left = RouteLengthM * (1.0 - double(Near) / double(Line.Num() - 1)) - 15.0;
			const double EndGap = (Left > 0.0 ? FMath::Sqrt(2.0 * 3.0 * Left) : 0.0) - Sim->Speed;
			if (bFinished || Left <= 0.0)
			{
				Throttle = 0.0;
				FrontBrake = 0.8;
				RearBrake = 0.4;
			}
			else if (EndGap < Gap)
			{
				Throttle = FMath::Clamp(EndGap * 0.4, 0.0, 1.0);
				FrontBrake = FMath::Clamp(-EndGap * 0.25, 0.0, 0.8);
				RearBrake = FrontBrake * 0.5;
			}
		}
		if (bTestStopping)
		{
			Throttle = 0.0;
			FrontBrake = 0.8;
			RearBrake = 0.4;
		}
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
		bPointing = false;   // [info] (the menu has the pointer now)
		Throttle = FrontBrake = RearBrake = Shift = 0.0;
		Lean = Approach(Lean, 0.0, 2.2, 4.0, Dt);
		return;
	}
	// The game has the mouse while no menu is up, captured and hidden, as the shed's walker takes it:
	// nothing took it on the ride, so its moves never reached the look on foot (beenster, build 134).
	// [info] M puts a pointer out to click a place in the info panel (a waypoint); M again, or a
	// right click, takes it away. The panel slides out for it.
	if (FTorqueKeys::Just(PC, ETorqueKey::PickPlace) || (bPointing && PC->WasInputKeyJustPressed(EKeys::RightMouseButton)))
	{
		bPointing = !bPointing && !PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
		if (bPointing && !FTorqueSettings::Get().bInfoPanel)
		{
			FTorqueSettings& Keep = FTorqueSettings::Get();
			Keep.bInfoPanel = true;
			if (Keep.bSaved) Keep.Save();
		}
		UE_LOG(LogTemp, Log, TEXT("Torque info: pointer %s"), bPointing ? TEXT("out") : TEXT("away"));
	}
	if (bPointing)
	{
		if (!PC->bShowMouseCursor)
		{
			PC->bShowMouseCursor = true;
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
	}
	else if (!bMouseTaken || PC->bShowMouseCursor)
	{
		bMouseTaken = true;
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
	// [foot] F (or Triangle / Y, stopped) gets off; on foot, F or Triangle / Y by the bike gets back on.
	const bool bPadUse = SonyHit(ESonyButton::Triangle) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top);
	if (bOnFoot)
	{
		Throttle = FrontBrake = RearBrake = Shift = Lean = 0.0;
		const double KeyX = (FTorqueKeys::Down(PC, ETorqueKey::WalkRight) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::WalkLeft) ? 1.0 : 0.0);
		const double KeyY = (FTorqueKeys::Down(PC, ETorqueKey::WalkForward) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::WalkBack) ? 1.0 : 0.0);
		const double StickX = Stick(Either(Axis(EKeys::Gamepad_LeftX), Sony.LX), Set.StickDeadZone, 1.0);
		const double StickY = Stick(Either(Axis(EKeys::Gamepad_LeftY), Sony.LY), Set.StickDeadZone, 1.0);
		// The controller as the shed's walker has it: left stick walks, L3 hurries, right stick looks (the
		// DualSense too, read over HID, which the engine's own gamepad axes never see).
		const bool bPadRun = PC->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick) || (SonyDown & ESonyButton::L3) != 0;
		StepFoot(PC, Dt, FMath::Abs(StickX) > FMath::Abs(KeyX) ? StickX : KeyX, FMath::Abs(StickY) > FMath::Abs(KeyY) ? StickY : KeyY,
			LookX, LookY, bPadRun);
		// F: a tap by the bike gets back on; held, takes hold of its bars to wheel it (as the shed's
		// walker: walker.gd HOLD_TO_PUSH); wheeling, F lets go and it goes back on its stand there.
		const bool bUseDown = FTorqueKeys::Down(PC, ETorqueKey::Use) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top) || (SonyDown & ESonyButton::Triangle);
		const bool bUseJust = FTorqueKeys::Just(PC, ETorqueKey::Use) || bPadUse;
		if (bPushing)
		{
			if (bUseJust) LetGoBike();
		}
		else if (bUseJust && OnFootFor > 0.4)
		{
			if (FVector::Dist2D(FootPos, GetActorLocation()) <= 250.0) { bUseArmed = true; UseHeld = 0.0; }
			else GetOn();   // (says to walk back to it)
		}
		if (bUseArmed)
		{
			UseHeld += Dt;
			if (!bUseDown) { bUseArmed = false; GetOn(); }
			else if (UseHeld >= 0.35) { bUseArmed = false; TakeHoldOfBike(); }
		}
		if (FTorqueKeys::Just(PC, ETorqueKey::BackToShed))   // (as riding; [yard] in the yard, walk back in)
		{
			if (bYard) TorqueYard::WalkBackIn(PC);
			else TorqueFlow::BackToShed(this);
		}
		// [ping] Middle mouse, or the D-pad's left as in the shed: mark what's looked at for the crew.
		if (FTorqueKeys::Just(PC, ETorqueKey::Ping) || SonyHit(ESonyButton::DLeft) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left))
			TorquePing::FromView(PC, this);
		return;
	}
	// Riding, Triangle / Y (beenster's pad layout: it gets on and off): stopped, a tap gets off; moving,
	// held 0.4 s is back on the road; crashed, a tap is back on the road straight away. (The lights'
	// cycle went to the D-pad's up.)
	bool bPadHoldUse = false, bPadTapOff = false;
	{
		const bool bTriDown = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top) || (SonyDown & ESonyButton::Triangle) != 0;
		if (bPadUse && !Sim->IsCrashed)
		{
			if (FMath::Abs(Sim->Speed) <= 2.0) bPadTapOff = true;
			else
			{
				bPadUseRideArmed = true;
				PadUseRideHeld = 0.0;
				FootNote = TEXT("Stop to get off (hold Triangle / Y: back on the road).");
				FootNoteFor = 1.5;
			}
		}
		if (bPadUseRideArmed)
		{
			PadUseRideHeld += Dt;
			if (!bTriDown) bPadUseRideArmed = false;
			else if (PadUseRideHeld >= 0.4) { bPadUseRideArmed = false; bPadHoldUse = true; }
		}
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Use) || bPadTapOff)
	{
		// [yard] In the shed's yard or the shed itself: getting off parks the bike where it stands (in
		// its bay if that's where it is) and you're on your feet in the shed's own way (TorqueYard).
		if (bYard)
		{
			if (Sim->IsCrashed || FMath::Abs(Sim->Speed) > 2.0)
			{
				FootNote = TEXT("Stop first, then get off.");
				FootNoteFor = 1.5;
			}
			else
			{
				// Left ticking over where it stands (beenster, Nov 12), heard from the shed's own sound.
				if (UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this)) Sub->SetIdling(RideBay, EngineRunning());
				TorqueYard::GetOff(PC);
				return;
			}
		}
		else if (GetOff()) return;
	}
	const double LookK = 1.0 - FMath::Exp(-8.0 * Dt);
	// The bumpers (beenster's pad layout): LB held is the clutch, RB held looks behind; both held a
	// moment with the engine off start it.
	const bool bPadLB = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder) || (SonyDown & ESonyButton::L1) != 0;
	const bool bPadRB = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder) || (SonyDown & ESonyButton::R1) != 0;
	// View / Create held in third person: the right stick and D-pad set the camera (AdjustChase), not the look.
	const bool bPadView = PC->IsInputKeyDown(EKeys::Gamepad_Special_Left) || (SonyDown & ESonyButton::Create) != 0;
	const bool bPadSetsCam = bPadView && CameraMode == CamChase;
	const double LookPadX = bPadSetsCam ? 0.0 : LookX, LookPadY = bPadSetsCam ? 0.0 : LookY;
	// Settings > Gameplay > Chase camera recentres: off, the view stays where the stick left it.
	const bool bStickLooking = FMath::Abs(LookPadX) > 0.02 || FMath::Abs(LookPadY) > 0.02;
	if (Set.bCameraRecentre || bStickLooking)
	{
		StickYaw = FMath::Lerp(StickYaw, LookPadX * 130.0, LookK);
		StickPitch = FMath::Lerp(StickPitch, LookPadY * 25.0, LookK);
	}
	// The mouse swings the view round too (Settings > Controller > Mouse look speed), and it eases
	// back behind the bike a moment after you stop moving it.
	{
		float Mx = 0.0f, My = 0.0f;
		if (!bPointing) PC->GetInputMouseDelta(Mx, My);   // [info] (the pointer's, while it's out)
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
	const bool bBehind = FTorqueKeys::Down(PC, ETorqueKey::LookBack) || (bPadRB && !(bPadLB && !Sim->EngineOn));
	LookYaw = bBehind ? 180.0 : StickYaw + MouseYaw;
	LookPitch = bBehind ? 0.0 : StickPitch + MousePitch;

	const double KeyThrottle = FTorqueKeys::Down(PC, ETorqueKey::Throttle) ? 1.0 : 0.0;
	const double KeyBrake = FTorqueKeys::Down(PC, ETorqueKey::FrontBrake) ? 1.0 : 0.0;
	const double KeyLean = FMath::Clamp(((FTorqueKeys::Down(PC, ETorqueKey::LeanRight) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::LeanLeft) ? 1.0 : 0.0)) * Set.Steering, -1.0, 1.0);
	const double KeyShift = (FTorqueKeys::Down(PC, ETorqueKey::WeightForward) ? 1.0 : 0.0) - (FTorqueKeys::Down(PC, ETorqueKey::WeightBack) ? 1.0 : 0.0);

	// Keys are all or nothing, so ease them in (as Torque Shed does); the pad is used as it is.
	Throttle = PadThrottle > 0.02 ? PadThrottle : Approach(Throttle, KeyThrottle, 2.5, 6.0, Dt);
	FrontBrake = PadBrake > 0.02 ? PadBrake : Approach(FrontBrake, KeyBrake, 3.0, 8.0, Dt);
	// The rear brake on Circle / B held (LB is the clutch now).
	RearBrake = FMath::Max(FTorqueKeys::Down(PC, ETorqueKey::RearBrake) ? 1.0 : 0.0, Down(EKeys::Gamepad_FaceButton_Right));
	if (SonyDown & ESonyButton::Circle) RearBrake = 1.0;

	// Gears: Cross/A up, Square/X down (X and Z, or Left Shift and Left Ctrl, on keys). The first
	// change by hand turns the automatic box off; G (or Settings) turns it back on.
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
	if (FTorqueKeys::Just(PC, ETorqueKey::AutoShift))
	{
		bManual = !bManual;
		GearNoteFor = 2.0;
		// As Torque Shed: the setting follows (kept once the player has saved settings).
		FTorqueSettings& Keep = FTorqueSettings::Get();
		Keep.bAutoShift = !bManual;
		if (Keep.bSaved) Keep.Save();
	}
	// The clutch, held (Left Alt, or LB on the pad; F is get off): the engine spins free of the box in any
	// gear, so it revs in gear and drives again as it's let out. The key (I): the engine off, or the
	// starter cranks it and it fires (not on an empty tank, or with the bike down).
	Sim->ClutchIn = FTorqueKeys::Down(PC, ETorqueKey::Clutch) || bTestClutch || bPadLB;
	if (FTorqueKeys::Just(PC, ETorqueKey::Ignition)) PressIgnition();
	// ABS: the D-pad's down or the B key. Off, the rear brake will lock the wheel.
	const bool bPadDDown = !bPadSetsCam && (SonyHit(ESonyButton::DDown) || Just(EKeys::Gamepad_DPad_Down));
	if (bPadDDown || FTorqueKeys::Just(PC, ETorqueKey::Abs))
	{
		// Only on a bike that has it (as Torque Shed: the ECU's aid); none, it stays off.
		bAbs = !bAbs && (bHasAbs[0] || bHasAbs[1]);
		Sim->Build.AbsFront = bAbs && bHasAbs[0];
		Sim->Build.AbsRear = bAbs && bHasAbs[1];
		GearNoteFor = 2.0;
	}
	Lean = FMath::Abs(PadLean) > 0.08 ? PadLean : Approach(Lean, KeyLean, 2.2, 4.0, Dt);
	Shift = FMath::Abs(PadShift) > 0.15 ? PadShift : Approach(Shift, KeyShift, 3.0, 3.0, Dt);

	if (FTorqueKeys::Just(PC, ETorqueKey::BackOnRoad) || (bPadUse && Sim->IsCrashed) || (bPadHoldUse && FMath::Abs(Sim->Speed) > 2.0))
	{
		BackOnWheels();
		Sim->StartGhost();
	}
	// Options / Start open the pause menu now (ATorquePlayerController), which has Restart the ride.
	if (FTorqueKeys::Just(PC, ETorqueKey::Restart)) RestartRide();
	// The pad's starter (beenster): LB and RB held together for a moment start the engine (LB is the
	// clutch and RB looks behind, so with the engine off that's a start, not a look back).
	bool bCameraTap = false;
	{
		const int32 PadTilt = bPadSetsCam ? ((SonyDown & ESonyButton::DDown) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down) ? 1 : 0) -
			((SonyDown & ESonyButton::DUp) || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up) ? 1 : 0) : 0;
		// Set with View / Create held: letting it go then isn't a camera change.
		if (AdjustChase(PC, Dt, LookX, LookY, bPadSetsCam, PadTilt) && bPadSetsCam) bPadViewUsed = true;
	}
	if (bPadView) bPadViewWas = true;
	else
	{
		if (bPadViewWas && !bPadViewUsed) bCameraTap = true;   // View / Create let go, nothing set: the camera
		bPadViewWas = bPadViewUsed = false;
	}
	if (bPadRB && bPadLB)
	{
		PadStartHeld += Dt;
		if (PadStartHeld >= 0.3 && !bPadStartDone)
		{
			bPadStartDone = true;
			if (!Sim->EngineOn && !Sim->IsStarting()) PressIgnition();
		}
	}
	else
	{
		bPadStartDone = false;
		PadStartHeld = 0.0;
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Camera) || bCameraTap) NextCamera();
	// [gestures] R3 (or the Y key): a tap nods to another rider; R3 held (or the U key held) waves, the left hand off the bar.
	{
		const bool bR3Down = PC->IsInputKeyDown(EKeys::Gamepad_RightThumbstick) || (SonyDown & ESonyButton::R3) != 0;
		bool bPadWave = false;
		if (bR3Down)
		{
			PadR3Held += Dt;
			bPadWave = PadR3Held >= WaveHoldPad;
		}
		else
		{
			if (PadR3Held > 0.0 && PadR3Held < WaveHoldPad) StartNod();
			PadR3Held = 0.0;
		}
		if (FTorqueKeys::Just(PC, ETorqueKey::Nod)) StartNod();
		SetWaving(!Sim->IsCrashed && (bPadWave || FTorqueKeys::Down(PC, ETorqueKey::Wave)));
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Help)) bShowHelp = !bShowHelp;
	// [Ride UK stage 7] A photo for the ride diary, pinned where it was taken.
	if (FTorqueKeys::Just(PC, ETorqueKey::Photo))
	{
		const FString Said = ATorqueRideWorld::TakePhoto();
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ShowNote(Said, 3.0);
	}
	// beenster (Nov 12): F2 the info lines top left (off to start with), F3 the telemetry.
	if (FTorqueKeys::Just(PC, ETorqueKey::RideHud))
	{
		bHudLines = !bHudLines;
		if (bHudLines) bShowHelp = true;
	}
	// [info] F1 slides the info panel down the left in and out (beenster, Nov 12); remembered with your settings.
	if (FTorqueKeys::Just(PC, ETorqueKey::InfoPanel))
	{
		FTorqueSettings& Keep = FTorqueSettings::Get();
		Keep.bInfoPanel = !Keep.bInfoPanel;
		if (Keep.bSaved) Keep.Save();
	}
	// Settings > Gameplay > Telemetry overlay (F3 while riding, as Torque Shed).
	if (FTorqueKeys::Just(PC, ETorqueKey::Telemetry))
	{
		FTorqueSettings& Keep = FTorqueSettings::Get();
		Keep.bTelemetry = !Keep.bTelemetry;
		if (Keep.bSaved) Keep.Save();
	}
	// The horn while J or L3 is held (Torque Shed's pad horn; the d-pad's left and right are the indicators).
	const bool bHorn = FTorqueKeys::Down(PC, ETorqueKey::Horn) || PC->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick) || (SonyDown & ESonyButton::L3) != 0;
	if (bHorn != bHornDown && HornId)
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->SetHorn(HornId, bHorn);
	bHornDown = bHorn;
	// [ping] Middle mouse: mark what's ahead (another rider, the spot on the road) for the whole crew
	// (main.gd's "ride_ping"; Coop/TorquePing.h). No pad button: they're all taken riding.
	if (FTorqueKeys::Just(PC, ETorqueKey::Ping)) TorquePing::FromView(PC, this);
	// [lights] The lamps' switches (TorqueLights.h): L headlight, K main beam, N fog lights, , and .
	// the indicators (again to cancel), / the hazards. On the pad (beenster): the d-pad's left and right
	// the indicators, both (one then the other) the hazards, and the d-pad's up steps the lights
	// round dipped, main beam, fog lights (when fitted), off.
	{
		bool bOk = false;
		if (!bPadSetsCam && (SonyHit(ESonyButton::DUp) || Just(EKeys::Gamepad_DPad_Up))) LightsNote(Lights.CycleBeam(bOk), bOk);
		if (FTorqueKeys::Just(PC, ETorqueKey::Headlight)) LightsNote(Lights.CycleBeam(bOk), bOk);   // L cycles: dipped, main, main + fog, off
		if (FTorqueKeys::Just(PC, ETorqueKey::MainBeam)) LightsNote(Lights.ToggleMainBeam(bOk), bOk);
		if (FTorqueKeys::Just(PC, ETorqueKey::FogLights)) LightsNote(Lights.ToggleFog(bOk), bOk);
		if (FTorqueKeys::Just(PC, ETorqueKey::Hazards)) LightsNote(Lights.ToggleHazard(bOk), bOk);
		const bool bPadLeft = SonyHit(ESonyButton::DLeft) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left);
		const bool bPadRight = SonyHit(ESonyButton::DRight) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right);
		const bool bPadOther = bPadLeft ? ((SonyDown & ESonyButton::DRight) != 0 || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right))
			: bPadRight && ((SonyDown & ESonyButton::DLeft) != 0 || PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left));
		if (bPadOther) LightsNote(Lights.ToggleHazard(bOk), bOk);
		else if (FTorqueKeys::Just(PC, ETorqueKey::IndicateLeft) || bPadLeft) LightsNote(Lights.Indicate(-1, bOk), bOk);
		else if (FTorqueKeys::Just(PC, ETorqueKey::IndicateRight) || bPadRight) LightsNote(Lights.Indicate(1, bOk), bOk);
	}
	// Backspace: back to the shed (when its map has been built).
	// [yard] In the shed's yard: park it and walk back in (Shed/TorqueYard.h).
	if (FTorqueKeys::Just(PC, ETorqueKey::BackToShed))
	{
		if (bYard)
		{
			TorqueYard::WalkBackIn(PC);
			return;
		}
		if (TorqueFlow::BackToShed(this)) return;
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Weather))   // (the pad's d-pad right is the right indicator now; Settings has the weather)
	{
		// The host's (or a solo rider's) to change; Tick picks it up from the game state.
		ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		if (GS && HasAuthority()) GS->SetWeather(GS->GetWeather() + 1);
		else if (GS) HostOnlyNoteFor = 3.0;
	}
	if (FTorqueKeys::Just(PC, ETorqueKey::Perf))   // (L3 is the horn now)
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

// --- [foot] getting off and on, the bike on its stand ----------------------------------------------

void ATorqueBikePawn::ChooseStand()
{
	// Torque Shed's parked bike (customer.gd _set_parked): on the side stand, leaned over by the
	// stand's lean; else on the centre stand, lifted by its lift; neither, upright. The Speed Twin
	// stand-in has a side stand.
	ParkStand = 0;
	ParkLean = ParkLift = 0.0;
	const TorqueFit::BikeLayout* Lay = bCatalogueModel ? TorqueShedCatalogue::Layout() : nullptr;
	const TorqueFit::Json* Side = Lay && !SideStandPart.IsEmpty() && SideStandPart != TEXT("none") ? Lay->Part(PawnStd(SideStandPart)) : nullptr;
	const TorqueFit::Json* Centre = Lay && !CentreStandPart.IsEmpty() && CentreStandPart != TEXT("none") ? Lay->Part(PawnStd(CentreStandPart)) : nullptr;
	if (Side || !bCatalogueModel)
	{
		ParkStand = 1;
		ParkLean = FMath::DegreesToRadians(Side ? Side->Num("lean", 12.0) : 12.0);
	}
	else if (Centre)
	{
		ParkStand = 2;
		ParkLift = Centre->Num("lift", 0.04);
	}
}

bool ATorqueBikePawn::GetOff()
{
	if (bOnFoot) return false;
	if (Sim->IsCrashed) return false;
	if (FMath::Abs(Sim->Speed) > 2.0)
	{
		FootNote = TEXT("Stop first, then get off.");
		FootNoteFor = 1.5;
		return false;
	}
	// The ground under the bike and the way it points.
	const V3 Fwd = Sim->Basis.Z * -1.0;
	ParkHeading = std::atan2(-Fwd.x, -Fwd.z);
	ParkGround = Sim->Position - V3(0.0, Sim->Build.RearRadius, 0.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueGetOff), true, this);
	FHitResult Hit;
	// The road the tyres are on now (the sim's own contacts), between the two. A trace from a metre up
	// found the moor's collision over the road first in a cutting on Buttertubs, and the bike stood on
	// its stand in the air (beenster, build 132).
	const TorqueSim::Wheel& TyreF = Sim->Wheels[0];
	const TorqueSim::Wheel& TyreR = Sim->Wheels[1];
	if (TyreF.Contact && TyreR.Contact) ParkGround = (TyreF.ContactPos + TyreR.ContactPos) * 0.5;
	else if (TyreR.Contact) ParkGround = TyreR.ContactPos;
	else if (TyreF.Contact) ParkGround = TyreF.ContactPos;
	else if (UWorld* World = GetWorld())
		if (GroundTrace(World, ToUE(Sim->Position + V3(0.0, 0.3, 0.0)), ToUE(Sim->Position - V3(0.0, 3.0, 0.0)), Params, Hit)) ParkGround = ToSim(Hit.ImpactPoint);
	ChooseStand();
	// The rider steps off to the left (the side stand's side), facing the way the bike points.
	const FVector Ahead = DirToUE(V3(Fwd.x, 0.0, Fwd.z)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	FootPos = ToUE(ParkGround) - Right * 90.0;
	if (UWorld* World = GetWorld())
		if (GroundTrace(World, FootPos + FVector(0, 0, 150.0), FootPos - FVector(0, 0, 300.0), Params, Hit)) FootPos = Hit.ImpactPoint;
	FootYaw = Ahead.Rotation().Yaw;
	FootPitch = 0.0;
	bOnFoot = true;
	OnFootFor = 0.0;
	Throttle = FrontBrake = RearBrake = Lean = Shift = 0.0;
	bTestClutch = false;
	Sim->ClutchIn = false;
	Sim->Gear = BikeSim::NEUTRAL;   // left in neutral, ticking over if it was running
	HoldParked();
	PrevPos = CurPos = ToUE(Sim->Position);
	PrevRot = CurRot = BasisToUE(Sim->Basis);
	SetParkedLook(true);
	UE_LOG(LogTemp, Log, TEXT("Torque: got off, the bike on its %s, engine %s"),
		ParkStand == 1 ? TEXT("side stand") : ParkStand == 2 ? TEXT("centre stand") : TEXT("wheels"), Sim->EngineOn ? TEXT("running") : TEXT("off"));
	return true;
}

double ATorqueBikePawn::GestureNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool ATorqueBikePawn::IsNodding() const
{
	return GestureNow() - NodAt < NodTime;
}

void ATorqueBikePawn::StartNod()
{
	if (!IsNodding() && !Sim->IsCrashed && !bOnFoot) NodAt = GestureNow();
}

void ATorqueBikePawn::SetWaving(bool bWave)
{
	if (bWave == bWaving) return;
	// Eased from where it is now, so a quick let-go doesn't jump.
	const double Now = GestureNow(), Was = FMath::Clamp((Now - WaveAt) / WaveEase, 0.0, 1.0);
	const double Amount = bWaving ? Was : 1.0 - Was;   // how far out the hand is
	bWaving = bWave;
	WaveAt = Now - (bWave ? Amount : 1.0 - Amount) * WaveEase;
}

void ATorqueBikePawn::SetNetGestures(bool bNod, bool bWave)
{
	if (bNod && !bNetNodWas) StartNod();
	bNetNodWas = bNod;
	SetWaving(bWave);
}

bool ATorqueBikePawn::GetOn()
{
	if (!bOnFoot) return false;
	if (bPushing) LetGoBike();
	const FVector Bike = GetActorLocation();
	if (FVector::Dist2D(FootPos, Bike) > 250.0 || FMath::Abs(FootPos.Z - Bike.Z) > 200.0)
	{
		FootNote = TEXT("Walk back to the bike to get on.");
		FootNoteFor = 1.5;
		return false;
	}
	bOnFoot = false;
	// Upright on its wheels where it stood, the stand up, the engine as it was.
	const bool bRunning = Sim->EngineOn;
	const double RpmWas = Sim->Rpm;
	Sim->Place(ParkGround, ParkHeading);
	Sim->EngineOn = bRunning;
	Sim->Rpm = RpmWas;
	Sim->Gear = BikeSim::NEUTRAL;   // (beenster) always back on in neutral
	PrevPos = CurPos = ToUE(Sim->Position);
	PrevRot = CurRot = BasisToUE(Sim->Basis);
	Accum = 0.0;
	bCamInit = false;
	LastGear = Sim->Gear;
	SetParkedLook(false);
	UE_LOG(LogTemp, Log, TEXT("Torque: got back on, engine %s"), Sim->EngineOn ? TEXT("running") : TEXT("off"));
	return true;
}

void ATorqueBikePawn::HoldParked()
{
	// Stood on its stand: leaned over about the line its tyres touch the ground along (side stand),
	// or lifted (centre stand). No riding physics; the engine ticks over (or winds down, off).
	const M3 Heading = M3::AxisAngle(V3(0.0, 1.0, 0.0), ParkHeading);
	Sim->Basis = Heading * M3::AxisAngle(V3(0.0, 0.0, 1.0), ParkLean);   // + about the back axis: the top to the left
	// The wheels are drawn where the suspension left them (Centre, body space, up by its compression),
	// so the body stands that much lower to keep the tyres on the road, not a sag's height above it.
	const double AxleUp = 0.5 * ((Sim->Wheels[0].Radius - Sim->Wheels[0].Centre.y) + (Sim->Wheels[1].Radius - Sim->Wheels[1].Centre.y));
	Sim->Position = ParkGround + Sim->Basis * V3(0.0, AxleUp + ParkLift, 0.0);
	Sim->LinearVelocity = Sim->AngularVelocity = V3();
	Sim->Speed = 0.0;
	Sim->Throttle = Sim->FrontBrake = Sim->RearBrake = Sim->LeanInput = Sim->BodyShift = 0.0;
	for (TorqueSim::Wheel& W : Sim->Wheels) W.Omega = 0.0;
	const double Want = Sim->EngineOn ? Sim->Build.IdleRpm * Sim->Heat.Idle : 0.0;
	Sim->Rpm += (Want - Sim->Rpm) * FMath::Min(1.0, SimDt * 3.0);
	Sim->EngineKw = 0.0;
}

void ATorqueBikePawn::SetParkedLook(bool bParked)
{
	if (bParked == bParkedLook) return;
	bParkedLook = bParked;
	ShowRider(!bParked);
	ShowHead(!bParked);
	if (bParked && ParkStand == 0) ChooseStand();   // (a co-op rider's bike: its stand as ours would choose)
	PoseStands(bParked ? ParkStand : 0);
}

void ATorqueBikePawn::PoseStands(int32 Stand)
{
	// The catalogue bike's stands are drawn folded (BikeParts::Stands); down, each is swung about its
	// pivot as BikeModel._draw_side_stand / _draw_centre_stand swing them with a stand_pose. Godot's
	// bike frame (x right, y up, z back, metres) is the drawn bike's (X forward, Y right, Z up, cm).
	static const FName SideTag(TEXT("Slot:side_stand")), CentreTag(TEXT("Slot:centre_stand"));
	// (Taken again if the model was made again since: a gone surface means the list is stale.)
	bool bStale = StandRest.Num() == 0;
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>& KV : StandRest)
		if (!KV.Key.IsValid()) bStale = true;
	if (bStale)
	{
		StandRest.Reset();
		for (UStaticMeshComponent* C : ModelParts)
			if (C && (C->ComponentHasTag(SideTag) || C->ComponentHasTag(CentreTag))) StandRest.Add({C, C->GetRelativeTransform()});
	}
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>& KV : StandRest)
		if (KV.Key.IsValid()) KV.Key->SetRelativeTransform(KV.Value);
	if (Stand == 0) return;
	if (StandRest.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no stand drawn to put down (%s model, %d surfaces, side stand '%s', centre stand '%s')"),
			bCatalogueModel ? TEXT("catalogue") : TEXT("Speed Twin"), ModelParts.Num(), *SideStandPart, *CentreStandPart);
		return;
	}
	auto Godot = [](double X, double Y, double Z) { return FVector(-Z, X, Y) * 100.0; };
	const FName& Tag = Stand == 1 ? SideTag : CentreTag;
	FVector Pivot = FVector::ZeroVector, Folded = FVector::ZeroVector, Down = FVector::ZeroVector;
	int32 Pieces = 0;
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>& KV : StandRest)
		if (KV.Key.IsValid() && KV.Key->ComponentHasTag(Tag)) ++Pieces;
	if (Pieces == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: the %s stand isn't drawn on this bike (%d stand surfaces of the other kind)"),
			Stand == 1 ? TEXT("side") : TEXT("centre"), StandRest.Num());
		return;
	}
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	const double Rr = Sim->Build.RearRadius;
	if (Stand == 1)
	{
		const TorqueFit::Json* P = Lay ? Lay->Part(PawnStd(SideStandPart)) : nullptr;
		const double Len = P ? P->Num("length", 0.3) : 0.3;
		const double LeanS = FMath::DegreesToRadians(P ? P->Num("lean", 12.0) : 12.0);
		// Swung out until it meets the ground with the bike leaned over on it.
		const double Px = -0.12, Height = -0.04 + Rr;
		double Best = 0.0, BestErr = 1e9;
		for (int32 I = 0; I < 90; ++I)
		{
			const double A = FMath::DegreesToRadians((double)I);
			const double Fx = Px - Len * FMath::Sin(A), Fy = Height - Len * FMath::Cos(A);
			const double Err = FMath::Abs(Fy * FMath::Cos(LeanS) + Fx * FMath::Sin(LeanS));
			if (Err < BestErr) { BestErr = Err; Best = FMath::DegreesToRadians((double)I); }
		}
		Folded = Godot(-0.03, -0.04, Len * 0.95);
		Down = Godot(-Len * FMath::Sin(Best), -Len * FMath::Cos(Best), -0.06);
		// Where BikeParts::Stands puts it: 12 cm left, 4 cm under the origin, 16 cm ahead of the
		// swingarm pivot. (Its surfaces' own places are no help: their shapes are drawn in place.)
		Pivot = FVector(SwingPivot.X + 16.0, -12.0, -4.0);
	}
	else
	{
		const TorqueFit::Json* P = Lay ? Lay->Part(PawnStd(CentreStandPart)) : nullptr;
		const double Reach = Rr - 0.06 + (P ? P->Num("lift", 0.04) : 0.04);
		Folded = Godot(0.0, -0.08, Reach * 0.9);
		Down = Godot(0.0, -Reach, -0.02);
		// The legs' tops: on the bike's middle, 6 cm under its origin, 8 cm ahead of the swingarm pivot.
		Pivot = FVector(SwingPivot.X + 8.0, 0.0, -6.0);
	}
	const FQuat Swing = FQuat::FindBetweenVectors(Folded.GetSafeNormal(), Down.GetSafeNormal());
	const FTransform About = FTransform(-Pivot) * FTransform(Swing) * FTransform(Pivot);
	for (const TPair<TWeakObjectPtr<UStaticMeshComponent>, FTransform>& KV : StandRest)
		if (KV.Key.IsValid() && KV.Key->ComponentHasTag(Tag)) KV.Key->SetRelativeTransform(KV.Value * About);
	UE_LOG(LogTemp, Log, TEXT("Torque: %s stand down, swung %.0f deg about (%.0f, %.0f, %.0f) cm (the swingarm pivot at %.0f cm along)"),
		Stand == 1 ? TEXT("side") : TEXT("centre"), FMath::RadiansToDegrees(Swing.GetAngle()), Pivot.X, Pivot.Y, Pivot.Z, SwingPivot.X);
}

void ATorqueBikePawn::StepFoot(APlayerController* PC, double Dt, double MoveX, double MoveY, double PadLookX, double PadLookY, bool bPadRun)
{
	// Walking about: WASD or the left stick, Shift to hurry; the mouse or right stick looks.
	OnFootFor += Dt;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	float Mx = 0.0f, My = 0.0f;
	if (PC && !bPointing) PC->GetInputMouseDelta(Mx, My);   // [info] (the pointer's, while it's out)
	if ((Mx != 0.0f || My != 0.0f) && OnFootFor - FootMouseLogAt > 1.0)
	{
		FootMouseLogAt = OnFootFor;
		UE_LOG(LogTemp, Log, TEXT("Torque: on foot, the mouse moved (%.1f, %.1f): looking %.0f deg round, %.0f up"), Mx, My, FootYaw, FootPitch);
	}
	// (The shed walker's rate: 0.143 degrees a count, Settings > Controller > Mouse look speed on top.)
	FootYaw += Mx * 0.143 * Set.MouseLook;
	FootPitch = FMath::Clamp(FootPitch + My * 0.143 * Set.MouseLook * (Set.bInvertLook ? -1.0 : 1.0), -80.0, 80.0);
	// (PadLookX/Y come with the look speed and Invert look already in them.)
	FootYaw += PadLookX * 120.0 * Dt;
	FootPitch = FMath::Clamp(FootPitch + PadLookY * 90.0 * Dt, -80.0, 80.0);
	FVector2D In(MoveX, MoveY);
	if (In.SizeSquared() > 1.0) In.Normalize();
	if (In.SizeSquared() < 1e-4)
	{
		if (bPushing) FollowPusher();   // (turning on the spot turns it too)
		return;
	}
	const bool bRun = bPadRun || (PC && FTorqueKeys::Down(PC, ETorqueKey::Run));
	const FRotator Yaw(0.0, FootYaw, 0.0);
	const double Pace = bPushing ? (bRun ? 240.0 : 130.0) : (bRun ? 330.0 : 150.0);   // a bike is heavy to wheel about
	const FVector Walk = (Yaw.RotateVector(FVector::ForwardVector) * In.Y + Yaw.RotateVector(FVector::RightVector) * In.X) * Pace * Dt;
	UWorld* World = GetWorld();
	if (!World) return;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueFoot), true, this);
	FString Why;   // what stopped the last try (the log, when every try is stopped)
	auto TryMove = [&](const FVector& D) {
		const FVector To = FootPos + D;
		// Walls and banks at waist height stop you; the ground under you follows, up steps of 45 cm.
		// (Not the ground itself, a bank you're walking up, nor what you're already in: on the Buttertubs
		// ride you step off into the verge's grass and walls, and nothing let you walk out of them.)
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, FootPos + FVector(0, 0, 90.0), To + FVector(0, 0, 90.0), FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(30.0f), Params) && !Hit.bStartPenetrating)
		{
			const AActor* A = Hit.GetActor();
			const UPrimitiveComponent* Part = Hit.GetComponent();
			const bool bGround = (A && (A->ActorHasTag(TEXT("Land")) || A->ActorHasTag(TEXT("Road")))) || (Part && Part->ComponentHasTag(TEXT("Road")));
			if (!bGround)
			{
				Why = FString::Printf(TEXT("waist-high %s"), A ? *A->GetName() : TEXT("something"));
				return false;
			}
		}
		FVector Ground;
		double Up = 1.0;
		if (GroundTrace(World, To + FVector(0, 0, 120.0), To - FVector(0, 0, 400.0), Params, Hit))
		{
			Ground = Hit.ImpactPoint;
			Up = Hit.ImpactNormal.Z;
		}
		else
		{
			// The ride's own ground (the sim's), where the world has none to trace.
			const RayHit Sr = Sim->Ray(ToSim(To + FVector(0, 0, 120.0)), ToSim(To - FVector(0, 0, 400.0)));
			if (!Sr.Hit)
			{
				Why = TEXT("no ground under the step");
				return false;
			}
			Ground = ToUE(Sr.Position);
			Up = Sr.Normal.y;
		}
		if (Ground.Z - FootPos.Z > 45.0 || Up < 0.6)
		{
			Why = FString::Printf(TEXT("ground %.0f cm up, %.0f deg steep"), Ground.Z - FootPos.Z, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up, -1.0, 1.0))));
			return false;
		}
		FootPos = Ground;
		return true;
	};
	if (!TryMove(Walk))
	{
		// Slide along it.
		if (!TryMove(FVector(Walk.X, 0.0, 0.0)) && !TryMove(FVector(0.0, Walk.Y, 0.0)) && OnFootFor - FootBlockLogAt > 1.0)
		{
			FootBlockLogAt = OnFootFor;
			UE_LOG(LogTemp, Log, TEXT("Torque: on foot, a step stopped at (%.0f, %.0f, %.0f): %s"), FootPos.X, FootPos.Y, FootPos.Z, *Why);
		}
	}
	if (bPushing) FollowPusher();
}

void ATorqueBikePawn::TakeHoldOfBike()
{
	if (!bOnFoot || bPushing) return;
	bPushing = true;
	PoseStands(0);   // the stand up, upright in your hands
	FootNote = TEXT("Wheeling it. F to let go.");
	FootNoteFor = 2.0;
	FollowPusher();
}

void ATorqueBikePawn::LetGoBike()
{
	if (!bPushing) return;
	bPushing = false;
	ChooseStand();   // back on its stand where you let go
	PoseStands(ParkStand);
	FootNote = TEXT("On its stand.");
	FootNoteFor = 1.5;
}

void ATorqueBikePawn::FollowPusher()
{
	// Alongside you, its bars in your hands (walker.gd: 0.75 m to your right, a little behind), facing
	// your way, upright on its wheels on the ground there.
	const FRotator Yaw(0.0, FootYaw, 0.0);
	const FVector Ahead = Yaw.Vector(), Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	FVector At = FootPos + Right * 85.0 - Ahead * 25.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorquePush), true, this);
	FHitResult Hit;
	// (From just above your feet first: from higher, a cutting's moor collision over the road is found first.)
	if (UWorld* World = GetWorld())
		if (GroundTrace(World, At + FVector(0, 0, 60.0), At - FVector(0, 0, 300.0), Params, Hit) ||
			GroundTrace(World, At + FVector(0, 0, 150.0), At - FVector(0, 0, 300.0), Params, Hit))
			At = Hit.ImpactPoint;
	ParkGround = ToSim(At);
	ParkHeading = std::atan2(-Ahead.X, -Ahead.Y);   // (sim: x east = UE X, z south = UE Y)
	ParkLean = ParkLift = 0.0;
}

double ATorqueBikePawn::TestSpeedMph() const { return FMath::Abs(Sim->Speed) * 2.23694; }

FString ATorqueBikePawn::TestFoot(const FString& What)
{
	if (What == TEXT("off")) GetOff();
	else if (What == TEXT("on")) GetOn();
	else if (What == TEXT("push")) TakeHoldOfBike();
	else if (What == TEXT("letgo")) LetGoBike();
	else if (What.StartsWith(TEXT("walk:")) && bOnFoot)
	{
		const double Cm = FCString::Atod(*What.Mid(5));
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Cm) / 2.0));
		for (int32 I = 0; I < Steps; ++I) StepFoot(nullptr, 2.0 / 150.0, 0.0, Cm >= 0.0 ? 1.0 : -1.0);
	}
	// Where the bike is now (the actor only catches up on the next tick), and how it stands.
	if (bOnFoot) HoldParked();
	const FVector BikeAt = ToUE(Sim->Position);
	const TCHAR* Stood = !bOnFoot ? TEXT("on its wheels") : bPushing ? TEXT("upright in your hands") : ParkStand == 1 ? TEXT("on its side stand")
		: ParkStand == 2 ? TEXT("on its centre stand") : TEXT("on its wheels");
	return FString::Printf(TEXT("%s; bike %s at %.1f mph, engine %s (%.0f rpm), %.0f cm from you, at (%.0f, %.0f) cm; looking %.0f deg round, %.0f up"),
		bOnFoot ? TEXT("on foot") : TEXT("riding"), Stood, Sim->Speed * 2.23694, Sim->EngineOn ? TEXT("running") : TEXT("off"), Sim->Rpm,
		bOnFoot ? FVector::Dist2D(FootPos, BikeAt) : 0.0, BikeAt.X, BikeAt.Y, FootYaw, FootPitch);
}

FString ATorqueBikePawn::TestPlaceAt(double X, double Y, double Yaw, bool bPlace)
{
	if (bPlace)
	{
		const FVector Ahead = FRotator(0.0, Yaw, 0.0).Vector();
		PlaceOnGround(ToSim(FVector(X, Y, 0.0)), std::atan2(-Ahead.X, -Ahead.Y));
	}
	// What's under each wheel: the ground trace the wheels use, from 60 cm up to 40 cm down.
	FString Under;
	UWorld* World = GetWorld();
	const FVector Mid = ToUE(Sim->Position);
	const FVector Along = GetActorRotation().Vector().GetSafeNormal2D();
	for (const double Off : {70.0, -70.0})
	{
		const FVector Wheel = Mid + Along * Off;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueTestPlace), true, this);
		const bool bHit = World && GroundTrace(World, Wheel + FVector(0.0, 0.0, 60.0), Wheel - FVector(0.0, 0.0, 40.0), Params, Hit);
		Under += FString::Printf(TEXT("%s%s wheel on %s.%s at z %.1f"), Under.IsEmpty() ? TEXT("") : TEXT(", "), Off > 0.0 ? TEXT("front") : TEXT("rear"),
			bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("nothing"), bHit && Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("-"),
			bHit ? Hit.ImpactPoint.Z : 0.0);
	}
	return FString::Printf(TEXT("bike at (%.0f, %.0f, %.1f) cm, yaw %.0f, %.1f mph, %s; %s"), Mid.X, Mid.Y, Mid.Z, GetActorRotation().Yaw,
		Sim->Speed * 2.23694, TorqueShedLayout::InWashBay(Mid) ? TEXT("in the wash bay") : TEXT("outside the wash bay"), *Under);
}

bool ATorqueBikePawn::EngineRunning() const
{
	return Sim && (Sim->EngineOn || Sim->IsStarting() || EngineCatchIn > 0.0);
}

// [info] The info panel's riders list.
double ATorqueBikePawn::SpeedMs() const
{
	return Sim ? FMath::Abs(Sim->Speed) : 0.0;
}

bool ATorqueBikePawn::RiderCrashed() const
{
	return Sim && Sim->IsCrashed;
}

void ATorqueBikePawn::PressIgnition()
{
	UTorqueSfx* Sfx = UTorqueSfx::Get(this);
	if (Sim->EngineOn || Sim->IsStarting())
	{
		Sim->StopEngine();
		EngineNote = TEXT("Engine off");
		EngineNoteFor = 1.2;
	}
	else
	{
		Sim->FuelLitres = Dash.FuelLitres;
		const std::string Why = Sim->StartEngine();
		if (!Why.empty())
		{
			EngineNote = UTF8_TO_TCHAR(Why.c_str());
			EngineNoteFor = 2.0;
		}
		else if (Sfx) Sfx->PlayOn(TEXT("starter"), Root, FVector(0.0, 0.0, 45.0), 0.0f, 0.8f);
	}
}

FString ATorqueBikePawn::TestControl(const FString& What, int32 Arg)
{
	if (What == TEXT("gear"))
	{
		bManual = true;
		Sim->AutoShift = false;
		if (Arg > 0) Sim->ShiftUp();
		else Sim->ShiftDown();
	}
	else if (What == TEXT("ignition")) PressIgnition();
	else if (What == TEXT("clutch"))
	{
		bTestClutch = Arg != 0;
		Sim->ClutchIn = bTestClutch;
	}
	const TCHAR* Engine = Sim->IsStarting() ? TEXT("cranking") : Sim->EngineOn ? TEXT("running") : TEXT("off");
	return FString::Printf(TEXT("gear %s at %.1f mph, engine %s (%.0f rpm), clutch %s, %s gears; tyres %s (%.0f / %.0f C), engine %s (%.0f C); ghost %.1f s"),
		Sim->Gear == BikeSim::NEUTRAL ? TEXT("N") : *FString::FromInt(Sim->Gear), Sim->Speed * 2.23694, Engine, Sim->Rpm,
		Sim->ClutchIn ? TEXT("in") : TEXT("out"), bManual ? TEXT("manual") : TEXT("automatic"), UTF8_TO_TCHAR(Sim->Heat.TyreState()),
		Sim->Heat.TyreTemp(0), Sim->Heat.TyreTemp(1), UTF8_TO_TCHAR(Sim->Heat.EngineState()), Sim->Heat.Engine, Sim->GhostLeft);
}

void ATorqueBikePawn::TestStartMove(int32 Kind, double Mph, bool bFlat)
{
	bTestWantFlat = bFlat || Kind == 1;
	bTestFlat = false;
	TestMove = FMath::Clamp(Kind, 0, 4);
	bTestWantFlat = bTestWantFlat || TestMove >= 2;
	TestT30 = TestT60 = TestT100k = -1.0;
	TestStill = 0.0;
	bTestManualWas = bManual;
	TestContacts.SetBoxes({});
	TestContacts.Capsules.clear();
	TestParked.Reset();
	TestAfterHit = -1.0;
	TestFirstHit = TestPreHit = 0.0;
	TestFirstBike = false;
	TestPhase = 0;
	TestMps = FMath::Max(Mph, 5.0) / 2.23694;
	TestClock = TestSteady = TestDist = TestV0 = 0.0;
	TestNose = TestLeanSum = TestGSum = TestLeanPeak = TestGPeak = TestSlide = TestLateLean = 0.0;
	TestSamples = TestLateSamples = 0;
	TestMoveText.Reset();
	bTestRobotWas = bTestAutoRide;
	bTestAutoRide = true;   // the robot steers along the road and holds the speed
	bTestTarmac = false;
	TestAssistWas = Assist;
	Assist = 0.5;   // the road tests' rider (SimTest/realism_test), from the settling on

	// Start each manoeuvre the same way, whatever the bike was doing (stopped on a steep bit after the
	// last one, crashed, in a bend): put it on the straightest, most level stretch of the road long
	// enough for 2 s at the speed and the manoeuvre after, already rolling at the speed in a gear
	// that suits it, as Torque Shed's ride_slope starts its runs. No launch, no hunting for a straight.
	if (Line.Num() < 3) return;
	// The straightest stretch: how far the road strays from the straight line between its ends
	// (Buttertubs has few true straights; a long gentle bend scored as straight by its heading
	// alone, and the robot never settled on it at 60). The grade counts for a lot on the test pad
	// (it starts pitched otherwise), less on the road, where the log allows for it.
	const double Need = TestMps * 4.0 + 60.0;
	const double GradeWeight = bTestWantFlat ? 40.0 : 12.0;
	int32 Best = 0;
	double BestScore = 1e30;
	for (int32 I = 0; I + 1 < Line.Num(); ++I)
	{
		double Run = 0.0;
		int32 J = I;
		while (J + 1 < Line.Num() && Run < Need)
		{
			Run += FVector2D(Line[J + 1].X - Line[J].X, Line[J + 1].Z - Line[J].Z).Size();
			++J;
		}
		if (Run < Need) break;
		const FVector2D A(Line[I].X, Line[I].Z), Chord = FVector2D(Line[J].X, Line[J].Z) - A;
		const double ChordLen = FMath::Max(Chord.Size(), 1.0);
		double Stray = 0.0;
		for (int32 K = I; K <= J; ++K)
		{
			const FVector2D P = FVector2D(Line[K].X, Line[K].Z) - A;
			Stray = FMath::Max(Stray, FMath::Abs(P.X * Chord.Y - P.Y * Chord.X) / ChordLen);
		}
		const double Grade = FMath::Abs(Line[J].Y - Line[I].Y) / Run;
		const double Score = Stray + GradeWeight * Grade;
		if (Score < BestScore) { BestScore = Score; Best = I; }
	}
	if (!Sim->EngineOn) { Sim->StopEngine(); Sim->EngineOn = true; }   // (switched off on the key: the robot needs it)
	Sim->Heat.WarmThrough();   // the road tests' bike is warmed through, as SimTest's
	Sim->GhostLeft = 0.0;   // solid: the crash test hits things
	Respawn(Best);
	if (bTestWantFlat)
	{
		// The level test pad at the ground under the bike, from now until it's done.
		const RayHit Ground = Sim->Ray(Sim->Position + V3(0.0, 2.0, 0.0), Sim->Position - V3(0.0, 5.0, 0.0));
		TestPadY = Ground.Hit ? Ground.Position.y : Sim->Position.y - Sim->Build.RearRadius;
		bTestFlat = true;
	}
	const V3 Fwd = Sim->Basis.Z * -1.0;
	Sim->LinearVelocity = Fwd * TestMps;
	Sim->Wheels[0].Omega = TestMps / Sim->Build.FrontRadius;
	Sim->Wheels[1].Omega = TestMps / Sim->Build.RearRadius;
	int32 Gi = 1;
	while (Gi < Sim->Build.GearCount() && TestMps / Sim->Build.RearRadius * Sim->Build.OverallRatio(Gi) * 60.0 / (2.0 * PI) > Sim->Build.Redline * 0.75) ++Gi;
	Sim->Gear = Gi;
	Sim->Rpm = TestMps / Sim->Build.RearRadius * Sim->Build.OverallRatio(Gi) * 60.0 / (2.0 * PI);
	UE_LOG(LogTemp, Log, TEXT("Torque: test %s starts at route point %d of %d (%.0f m of road, strays and grade score %.2f m), %.0f mph in gear %d%s"),
		Kind == 0 ? TEXT("brakestop") : Kind == 1 ? TEXT("lean") : Kind == 2 ? TEXT("crash:wall") : TEXT("crash:bike"), Best, Line.Num(), Need, BestScore, Mph, Gi, bTestFlat ? TEXT(", on the test pad") : TEXT(""));
}

// One 120 Hz step of TestStartMove's manoeuvre, after ReadInput (the robot's inputs) and before the
// sim steps: it takes over what it needs, measures, and when it's done says so and hands back.
void ATorqueBikePawn::HoldTestSpeed(double V)
{
	// Throttle by the gap (less of a base at a walking pace), and the back brake once it runs over: in
	// first on tickover a bike pulls along faster than a slow target.
	const double Gap = TestMps - V;
	Throttle = FMath::Clamp(0.25 * FMath::Min(TestMps / 13.0, 1.0) + Gap * 0.4, 0.0, 1.0);
	FrontBrake = 0.0;
	RearBrake = FMath::Clamp(-Gap * 0.35 - 0.05, 0.0, 0.6);
	if (RearBrake > 0.0) Throttle = 0.0;
}

void ATorqueBikePawn::TestMoveStep()
{
	const TCHAR* Name = TestMove == 0 ? TEXT("brakestop") : TestMove == 1 ? TEXT("lean") : TestMove == 2 ? TEXT("crash:wall") : TestMove == 3 ? TEXT("crash:bike") : TEXT("launch");
	const double Mph = TestMps * 2.23694;
	auto Finish = [&](const FString& Text) {
		TestMoveText = Text;
		UE_LOG(LogTemp, Log, TEXT("Torque: test %s at %.0f mph: %s"), Name, Mph, *Text);
		bTestTarmac = false;
		bTestAutoRide = bTestRobotWas;
		Assist = TestAssistWas;
		bManual = bTestManualWas;
		const bool bWasFlat = bTestFlat;
		bTestFlat = false;
		TestContacts.SetBoxes({});
		TestContacts.Capsules.clear();
		TestParked.Reset();
		if (bWasFlat || (TestMove == 1 && TestPhase == 1))
		{
			Respawn(TestFromNear);   // back on the road where it began
			Throttle = FrontBrake = RearBrake = Lean = 0.0;
			// After a crash test, back on as after any fall: a ghost for 5 s (so tests see its look too).
			if (TestMove == 2 || TestMove == 3) Sim->StartGhost();
		}
		TestMove = -1;
	};
	TestClock += SimDt;
	const double V = Sim->Speed;
	if (Sim->IsCrashed && !((TestMove == 2 || TestMove == 3) && TestPhase == 1))
	{
		Finish(FString::Printf(TEXT("crashed (%s, %.1f s in, %.1f m)"), TestPhase == 0 ? TEXT("getting up to speed") : TEXT("measuring"), TestClock, TestDist));
		return;
	}
	if (TestPhase == 0)
	{
		// The robot rides (slowing for bends) until it's held the speed on a straight for 1.5 s (a
		// fairly level one, under 4%, for the test pad, so the bike doesn't land on it pitched).
		// On the test pad, straight on, the road's bends nothing to it: the robot's steering for them
		// kept a race bike from settling at 45 mph (round 128). (Before the steadiness check, which
		// asks for the steering centred: round 131 zeroed it after, and nothing settled.)
		if (bTestFlat) Lean = 0.0;
		const bool bSteady = FMath::Abs(V - TestMps) < 0.5 && FMath::Abs(Lean) < 0.15 && FMath::Abs(Sim->LeanAngle) < 0.1;
		TestSteady = bSteady ? TestSteady + SimDt : 0.0;
		// Held by the robot's throttle, and at a slow target (the bike pulls faster than it on tickover
		// in first) the back brake too.
		HoldTestSpeed(V);
		Shift = 0.0;
		if (TestSteady < 1.5)
		{
			if (TestClock > 15.0)
				Finish(FString::Printf(TEXT("never settled at %.0f mph in 15 s (at %.1f mph, lean %.1f deg, steer input %.2f)"), Mph, V * 2.23694,
					FMath::RadiansToDegrees(Sim->LeanAngle), Lean));
			return;
		}
		TestPhase = 1;
		TestClock = TestDist = 0.0;
		TestV0 = V;
		TestFromNear = Near;
		TestSide = Lean < 0.0 ? -1.0 : 1.0;
		TestY0 = Sim->Position.y;
		if (TestMove == 2 || TestMove == 3)
		{
			// What it's to hit: 12 m ahead, square across its path, standing on the pad.
			const V3 Fwd = Sim->Basis.Z * -1.0;
			TestFwd = FVector2D(Fwd.x, Fwd.z).GetSafeNormal();
			const double Ahead = 12.0;
			TestAt = FVector2D(Sim->Position.x, Sim->Position.z) + TestFwd * Ahead;
			TestHitsWas = Sim->HitCount;
			if (TestMove == 2)
			{
				// A drystone wall, as Tools/make_collision.py's wall boxes: 0.94 m thick, 1.7 m high; 6 m long.
				ContactBox Wall;
				Wall.Cx = TestAt.X;
				Wall.Cz = TestAt.Y;
				Wall.Ux = -TestFwd.Y;
				Wall.Uz = TestFwd.X;
				Wall.Hu = 3.0;
				Wall.Hv = 0.47;
				Wall.Y0 = TestPadY - 0.2;
				Wall.Y1 = TestPadY + 1.73;
				TestContacts.SetBoxes({Wall});
				TestFace = Ahead - Wall.Hv;
			}
			else
			{
				// A bike of the same build, stopped across the path with its rider on.
				TestParked = MakeUnique<BikeSim>();
				TestParked->Setup(Sim->Build);
				const double PadY = TestPadY;
				TestParked->Ray = [PadY](const V3& From, const V3& To) {
					RayHit Out;
					if (From.y >= PadY && To.y <= PadY && From.y > To.y)
					{
						Out.Hit = true;
						Out.Position = From + (To - From) * ((From.y - PadY) / (From.y - To.y));
						Out.Normal = V3(0.0, 1.0, 0.0);
						Out.Surface = SURF_TARMAC;
					}
					return Out;
				};
				const double Heading = FMath::Atan2(-TestFwd.X, -TestFwd.Y) + PI * 0.5;
				TestParked->Place(V3(TestAt.X, PadY, TestAt.Y), Heading);
				for (int32 I = 0; I < 60; ++I) TestParked->Step(SimDt);
				TestParked->LinearVelocity = TestParked->AngularVelocity = V3();
				TestContacts.SetBoxes({});
				TestParked->AsCapsules(TestContacts.Capsules);
				TestFace = Ahead - 0.3;
			}
		}
	}
	TestDist += FMath::Abs(V) * SimDt;
	if (TestMove == 4)
	{
		// The launch: straight ahead, stopped, then flat out on the automatic box (realism_test's).
		Lean = 0.0;
		bManual = false;
		if (TestPhase == 1)
		{
			// Held on the back brake: the front held hard at a standstill with the throttle shut is the
			// rider paddling it backwards with their feet (UpdateFeet), as round 128's launches did.
			Throttle = 0.0;
			Shift = 0.0;
			FrontBrake = 0.3;
			RearBrake = 0.8;
			if (Sim->Gear == BikeSim::NEUTRAL) Sim->Gear = 1;   // (out of neutral, as the launches always began)
			TestStill = FMath::Abs(V) < 0.05 ? TestStill + SimDt : 0.0;
			if (TestStill > 1.0)
			{
				TestPhase = 2;
				TestClock = TestDist = 0.0;
			}
			else if (TestClock > 15.0) Finish(FString::Printf(TEXT("never stood still (%.1f mph)"), V * 2.23694));
			return;
		}
		Throttle = 1.0;
		Shift = 0.6;
		FrontBrake = RearBrake = 0.0;
		TestNose = FMath::Max(TestNose, Sim->PitchAngle);   // (here the front's height: the most wheelie)
		if (TestT30 < 0.0 && V >= 13.4112) TestT30 = TestClock;
		if (TestT100k < 0.0 && V >= 27.7778) TestT100k = TestClock;
		if (TestT60 < 0.0 && V >= 26.8224) TestT60 = TestClock;
		if (TestClock >= 10.0)
		{
			const BikeSpec& B = Sim->Build;
			auto T = [](double X) { return X < 0.0 ? FString(TEXT("not reached")) : FString::Printf(TEXT("%.2f s"), X); };
			Finish(FString::Printf(TEXT("0-30 mph %s, 0-60 mph %s, 0-100 km/h %s on the test pad; %.1f mph and %.0f m after 10 s in gear %d, front up %.1f deg at most; ")
				TEXT("peak %.0f N m at %.0f, redline %.0f rpm, %.0f kg with the rider, TC %s, rear tyre %.2f grip"),
				*T(TestT30), *T(TestT60), *T(TestT100k), V * 2.23694, TestDist, Sim->Gear, FMath::RadiansToDegrees(TestNose),
				B.PeakTorque, B.PeakTorqueRpm, B.Redline, B.TotalMass, B.TractionControl ? *FString::Printf(TEXT("%.2f"), B.TractionStrength) : TEXT("off"),
				B.TyreRear.GripTarmac));
		}
		return;
	}
	if (TestMove >= 2)
	{
		// Straight on into it, holding the speed until the first knock, then off the throttle.
		const bool bHit = Sim->HitCount != TestHitsWas;
		Lean = 0.0;
		Shift = 0.0;
		FrontBrake = RearBrake = 0.0;
		if (bHit || Sim->IsCrashed) Throttle = 0.0;
		else
		{
			HoldTestSpeed(V);
			TestPreHit = V;   // the speed the step before it struck
		}
		if (TestAfterHit < 0.0 && bHit)
		{
			// The first knock's own speed (later scrapes and nudges overwrite LastHitSpeed).
			TestAfterHit = 0.0;
			TestFirstHit = Sim->LastHitSpeed;
			TestFirstBike = Sim->LastHitBike;
		}
		if (TestAfterHit >= 0.0) TestAfterHit += SimDt;
#if ENABLE_DRAW_DEBUG
		if (UWorld* W = GetWorld())
		{
			for (const ContactCapsule& C : TestContacts.Capsules)
				DrawDebugLine(W, ToUE(C.A), ToUE(C.B), FColor::Orange, false, 0.0f, 0, (float)(C.Radius * 200.0));
			if (TestMove == 2)
			{
				const FVector Mid = ToUE(V3(TestAt.X, TestPadY + 0.765, TestAt.Y));
				const FQuat Turn = FRotationMatrix::MakeFromX(FVector(TestFwd.X, TestFwd.Y, 0.0)).ToQuat();
				DrawDebugSolidBox(W, Mid, FVector(47.0, 300.0, 96.5), Turn, FColor(150, 140, 120), false, 0.0f);
			}
		}
#endif
		const double Along = FVector2D::DotProduct(FVector2D(Sim->Position.x, Sim->Position.z) - (TestAt - TestFwd * 12.0), TestFwd);
		const double Nose = Along + Sim->Build.Wheelbase * 0.5 + Sim->Build.FrontRadius;
		if (TestAfterHit > 2.0 || (TestAfterHit > 0.3 && FMath::Abs(V) < 0.3) || TestClock > 8.0)
		{
			const TCHAR* What = TestMove == 2 ? TEXT("a drystone wall") : TEXT("a stopped bike");
			if (TestAfterHit < 0.0)
				Finish(FString::Printf(TEXT("never hit %s %.0f m ahead (%.1f m along, %.1f mph)"), What, TestFace, Along, V * 2.23694));
			else
				Finish(FString::Printf(TEXT("hit %s at %.1f m/s (%.0f mph) into it, riding at %.1f mph just before, from %.1f mph; %s; front tyre %.2f m %s its face, %.1f mph a little after; the knock was %s"),
					What, TestFirstHit, TestFirstHit * 2.23694, TestPreHit * 2.23694, TestV0 * 2.23694,
					Sim->IsCrashed ? (Sim->KnockedOff ? TEXT("the rider was knocked off") : TEXT("the rider came off")) : TEXT("the rider stayed on"),
					FMath::Abs(Nose - TestFace), Nose > TestFace ? TEXT("past") : TEXT("short of"), V * 2.23694, TestFirstBike ? TEXT("a bike") : TEXT("a wall")));
		}
		return;
	}
	if (TestMove == 0)
	{
		// As the road tests (and SimTest/realism_test): front full, rear 0.6, no weight forward, and on
		// the test pad no steering (the robot's for the road's bends tipped the bikes without ABS over
		// with the front at its limit, round 128).
		if (bTestFlat) Lean = 0.0;
		Throttle = 0.0;
		Shift = 0.0;
		FrontBrake = 1.0;
		RearBrake = 0.6;
		TestNose = FMath::Min(TestNose, Sim->PitchAngle);
		if (V < 0.3)
		{
			// What the road's grade did (the height lost or gained, as energy), and the stop the brakes
			// alone give from exactly 60 mph on the level, to set beside SimTest/realism_test's.
			const double D = FMath::Max(TestDist, 0.1), Rise = Sim->Position.y - TestY0;
			const double G = TestV0 * TestV0 / (2.0 * D) / 9.81;
			// (Downhill, Rise < 0, the brakes took the height's energy as well as the speed's.)
			const double Brakes = FMath::Max((0.5 * TestV0 * TestV0 - 9.81 * Rise) / D, 0.1);
			const double Level60 = 26.8224 * 26.8224 / (2.0 * Brakes);
			const BikeSpec& B = Sim->Build;
			Finish(FString::Printf(TEXT("stopped in %.1f m, %.2f s (from %.1f mph, %.2f g on average, nose down %.1f deg at most, ABS front %s); ")
				TEXT("%s, %.1f%% grade on average, so %.1f m from 60 mph on the level; brakes %.0f / %.0f N m, tyres %.2f / %.2f grip, wet grip x%.2f"),
				TestDist, TestClock, TestV0 * 2.23694, G, FMath::RadiansToDegrees(-TestNose), B.AbsFront ? TEXT("on") : TEXT("off"),
				bTestFlat ? TEXT("on the test pad") : TEXT("on the road"), 100.0 * Rise / D, Level60, B.FrontBrakeTorque, B.RearBrakeTorque,
				B.TyreFront.GripTarmac, B.TyreRear.GripTarmac, B.WetGrip));
		}
		else if (TestClock > 20.0) Finish(FString::Printf(TEXT("still going after 20 s (%.1f mph, %.0f m)"), V * 2.23694, TestDist));
		return;
	}
	// Lean: the steer input held full the way the road was bending, the speed held, for 6 s;
	// measured over the last 3 (the lean it settles at), as SimTest/realism_test.
	Lean = TestSide;
	Throttle = FMath::Clamp(0.25 + (TestMps - V) * 0.4, 0.0, 1.0);
	FrontBrake = RearBrake = 0.0;
	Shift = 0.0;
	const double LeanDeg = FMath::Abs(FMath::RadiansToDegrees(Sim->LeanAngle));
	const double G = FMath::Abs(Sim->AngularVelocity.y * V) / 9.81;
	TestLeanPeak = FMath::Max(TestLeanPeak, LeanDeg);
	if (TestClock > 3.0)
	{
		TestLeanSum += LeanDeg;
		TestGSum += G;
		TestGPeak = FMath::Max(TestGPeak, G);
		++TestSamples;
	}
	if (TestClock > 5.0)
	{
		TestLateLean += LeanDeg;
		++TestLateSamples;
	}
	if (FMath::Max(Sim->Wheels[0].GripUsed, Sim->Wheels[1].GripUsed) > 1.05) TestSlide += SimDt;
	if (TestClock >= 6.0)
	{
		const double N = FMath::Max(TestSamples, 1), Late = TestLateLean / FMath::Max(TestLateSamples, 1);
		const bool bSlid = TestSlide > 0.25, bStoodUp = Late < 0.6 * TestLeanPeak;
		Finish(FString::Printf(TEXT("leaned %.1f deg (peak %.1f), %.2f g (peak %.2f) at %.1f mph on the test pad; %s%s (past the tyres' peak %.2f s); tyres %.2f / %.2f grip, wet grip x%.2f"),
			TestLeanSum / N, TestLeanPeak, TestGSum / N, TestGPeak, V * 2.23694, bSlid ? TEXT("slid") : TEXT("held its line"), bStoodUp ? TEXT(", stood up") : TEXT(""),
			TestSlide, Sim->Build.TyreFront.GripTarmac, Sim->Build.TyreRear.GripTarmac, Sim->Build.WetGrip));
	}
}

void ATorqueBikePawn::StepSim()
{
	if (bOnFoot)
	{
		// Parked on its stand while you walk about: held there, the engine ticking over.
		PrevPos = CurPos;
		PrevRot = CurRot;
		HoldParked();
		CurPos = ToUE(Sim->Position);
		CurRot = BasisToUE(Sim->Basis);
		return;
	}
	if (TestMove >= 0) TestMoveStep();
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
	if (!bRemote && !bTestFlat) WeatherHeat(false);
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
			Sample.Steer = (float)FMath::RadiansToDegrees(Sim->Steer);
			for (int32 WI = 0; WI < 2; ++WI)
			{
				const TorqueSim::Wheel& Wh = Sim->Wheels[WI];
				Sample.SlipRatio[WI] = (float)Wh.Kappa;
				Sample.SlipAngle[WI] = (float)FMath::RadiansToDegrees(Wh.Alpha);
				Sample.GripUsed[WI] = (float)Wh.GripUsed;
				Sample.LoadN[WI] = (float)Wh.Load;
				Sample.bContact[WI] = Wh.Contact;
				Sample.SuspMm[WI] = (float)(Wh.Compression * 1000.0);
				Sample.TravelMm[WI] = (float)(Wh.Travel * 1000.0);
				Sample.bBottomed[WI] = Wh.Bottomed;
				Sample.TyreSurface[WI] = (float)Sim->Heat.Surface[WI];
				Sample.TyreCarcass[WI] = (float)Sim->Heat.Carcass[WI];
				Sample.TyreC[WI] = (float)Sim->Heat.TyreTemp(WI);
				Sample.TyreBand[WI] = (int8)Sim->Heat.TyreBand(WI);
				Sample.DiscC[WI] = (float)Sim->Heat.Discs[WI];
			}
			Sample.EngineC = (float)Sim->Heat.Engine;
			Sample.OilC = (float)Sim->Heat.Oil;
			Sample.AirC = (float)Sim->Heat.Air;
			Sample.RoadC = (float)Sim->Heat.Road;
			{
				const FString EngineBandText = UTF8_TO_TCHAR(Sim->Heat.EngineState());
				Sample.EngineBand = EngineBandText == TEXT("hot") ? 1 : EngineBandText == TEXT("cold") ? -1 : 0;
			}
			Sample.bLiquid = Sim->Heat.Liquid();
			Sample.bFan = Sim->Heat.Fan;
			Sample.PowerMult = (float)Sim->Heat.Power;
			Sample.PitchDeg = (float)FMath::RadiansToDegrees(Sim->Dive);
			Sample.RakeDeg = (float)FMath::RadiansToDegrees(Sim->RakeNow);
			Sample.TrailMm = (float)(Sim->TrailSprings * 1000.0);
			Sample.TrailStandMm = (float)(Sim->Build.Trail * 1000.0);
			if (Telemetry.Num() >= 120) Telemetry.RemoveAt(0);
			Telemetry.Add(Sample);
		}

		// Walls and the other riders' bikes (off on the test pad, which runs wide of the road).
		GatherOtherBikes();
		Sim->Contacts = bTestFlat ? (TestMove >= 2 ? &TestContacts : nullptr) : Contacts.Get();
		// Fixed 120 Hz steps, at most a tenth of a second's worth after a hitch.
		Accum += FMath::Min((double)DeltaSeconds, 0.1);
		while (Accum >= SimDt)
		{
			StepSim();
			Accum -= SimDt;
		}
		if (Sim->HitCount != HitsHeard)
		{
			// A knock: the bike's metal on stone (a wall) or steel (another bike), as hard as it went in.
			HitsHeard = Sim->HitCount;
			const float Strength = (float)FMath::Clamp(Sim->LastHitSpeed / 8.0, 0.15, 1.0);
			if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->Impact(TEXT("metal"), Sim->LastHitBike ? TEXT("steel") : TEXT("concrete"), Strength, GetActorLocation());
			UE_LOG(LogTemp, Log, TEXT("Torque: hit %s at %.1f m/s%s"), Sim->LastHitBike ? TEXT("a bike") : TEXT("a wall"), Sim->LastHitSpeed, Sim->KnockedOff ? TEXT(", knocked off") : TEXT(""));
		}

		if (Sim->IsCrashed)
		{
			if (CrashedFor == 0.0 && !bRemote && !bCrashCam && CameraMode != CamDriveBy && CameraMode != 3)
			{
				// Fallen off: the drive-by camera, stood just down the road, watches you slide (beenster).
				bCrashCam = true;
				CrashCamFrom = CameraMode;
				CameraMode = CamDriveBy;
				bDriveByPlaced = false;
			}
			else if (CrashedFor == 0.0 && CameraMode == CamDriveBy) bDriveByPlaced = false;   // (already drive-by: a fresh spot for the fall)
			CrashedFor += DeltaSeconds;
			if (CrashedFor > 2.5)
			{
				BackOnWheels();
				Sim->StartGhost();   // a ghost for 5 s: nothing knocks the rider off again straight away
			}
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
	// [dash] The trip and the petrol (the rider's own bike), and the clocks on every bike.
	if (!bRemote)
	{
		Dash.Step(*Sim, Throttle, Sim->EngineOn, DeltaSeconds);
		TripSpeedKm += FMath::Abs(Sim->Speed) * DeltaSeconds / 1000.0;
		TripLogTime += DeltaSeconds;
		if ((TripLogClock += DeltaSeconds) >= 10.0)
		{
			TripLogClock = 0.0;
			TripLog(TEXT("riding"));
		}
		// The tank is the dash's (it burns the petrol); run dry, the engine stops.
		Sim->FuelLitres = Dash.FuelLitres;
		if (Sim->EngineOn && Dash.FuelLitres <= 0.0)
		{
			Sim->StopEngine();
			EngineNote = TEXT("Out of petrol!");
			EngineNoteFor = 4.0;
		}
		// [fluids] The shed keeps what's left in the tank (sent as it burns, a quarter litre at a time).
		if (RideBay >= 0)
			if (UTorqueBuildSubsystem* FuelShed = UTorqueBuildSubsystem::Of(this)) FuelShed->ReportRideFuel(RideBay, (float)Dash.FuelLitres);
	}
	else Dash.Step(*Sim, 0.0, true, 0.0);
	EngineNoteFor = FMath::Max(EngineNoteFor - DeltaSeconds, 0.0);
	FootNoteFor = FMath::Max(FootNoteFor - DeltaSeconds, 0.0);
	CamNoteFor = FMath::Max(CamNoteFor - DeltaSeconds, 0.0);
	if (ChaseSaveIn >= 0.0 && (ChaseSaveIn -= DeltaSeconds) < 0.0) FTorqueProfile::Mine().Save();   // the third-person camera, once it's settled
	if (!bRemote)
	{
		ATorqueRideWorld::NavFor(GetActorLocation(), Dash, NavWas);   // [Ride UK] the sat nav
		// (stopped at the pumps: the tank fills, a litre every second or so)
		if (Dash.bAtFuel && FMath::Abs(Sim->Speed) < 0.3) Dash.FuelLitres = FMath::Min(Dash.FuelCapacity, Dash.FuelLitres + 0.9 * DeltaSeconds);
	}
	Dash.UpdateClocks(FTorqueSettings::Get().Units);
	StepLights(DeltaSeconds, bRemote);   // [lights]
	if (!bRemote)
	{
		UpdateProgress();
		UpdateCamera(DeltaSeconds);
	}
	UpdateSound(DeltaSeconds);
	if (!bRemote)
	{
		UpdateRumble(DeltaSeconds);
		UpdatePerf(DeltaSeconds);
		DrawHud();
	}
	if (NetBike) NetBike->AfterTick(DeltaSeconds);
}

void ATorqueBikePawn::UpdateVisuals(double Alpha)
{
	// Draw between the last two steps so the bike moves smoothly at any frame rate.
	SetActorLocationAndRotation(FMath::Lerp(PrevPos, CurPos, Alpha), FQuat::Slerp(PrevRot, CurRot, Alpha));
	// [rider pose] Stopped on a tall bike: leant onto the rider's left foot about the tyres' contact line.
	if (bOnFoot || bParkedLook) StopLeanDeg = 0.0;   // (off it, or parked on its stand: no rider's foot to lean on)
	if (FMath::Abs(StopLeanDeg) > 0.01)
	{
		const FQuat Tip(FVector::ForwardVector, FMath::DegreesToRadians(StopLeanDeg));
		const FVector Contact(0.0, 0.0, -Sim->Build.RearRadius * 100.0);
		const FQuat Was = GetActorQuat();
		SetActorLocationAndRotation(GetActorLocation() + Was.RotateVector(Contact - Tip.RotateVector(Contact)), Was * Tip);
	}
	// The respawn ghost (BikeGhost): a faint, steady wireframe of the bike and rider's polygons, no blink.
	UpdateGhostLook();

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

void ATorqueBikePawn::GhostRestore()
{
	// The meshes' own materials back, as they were before the wire.
	for (int32 I = 0; I < GhostComps.Num(); ++I)
	{
		UMeshComponent* C = GhostComps[I];
		if (!IsValid(C) || !GhostMatStart.IsValidIndex(I)) continue;
		const int32 From = GhostMatStart[I];
		const int32 To = GhostMatStart.IsValidIndex(I + 1) ? GhostMatStart[I + 1] : GhostSavedMats.Num();
		// Only slots still wearing the wire: one the game changed meanwhile (the avatar dressing) keeps that.
		UMaterialInterface* const Wire = GhostMid ? (UMaterialInterface*)GhostMid : (UMaterialInterface*)GhostFillMid;
		for (int32 Slot = 0; Slot < To - From; ++Slot)
			if (C->GetMaterial(Slot) == Wire) C->SetMaterial(Slot, GhostSavedMats[From + Slot]);
		const int32 LodWas = GhostLodWas.IsValidIndex(I) ? GhostLodWas[I] : -1;
		if (LodWas >= 0)
		{
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(C)) SMC->SetForcedLodModel(LodWas);
		}
	}
	GhostLodWas.Reset();
	GhostComps.Reset();
	GhostMatStart.Reset();
	GhostSavedMats.Reset();
	for (const TWeakObjectPtr<USceneComponent>& H : GhostHairHidden)
		if (H.IsValid()) H->SetHiddenInGame(false);
	GhostHairHidden.Reset();
}

void ATorqueBikePawn::UpdateGhostLook()
{
	// beenster, Nov 11: "the ghost mode for respawned rider should only show the green mesh and then
	// fade to solid". Mode 1 (the ghost's first GHOST_TIME - GHOST_FADE s): every mesh of the bike and
	// rider drawn only as its green wireframe (M_RideGhostMatrix in place of all its materials; the
	// hair hidden). Mode 2 (the last GHOST_FADE s): its own materials back under a green fill
	// (M_RideGhostFill, the overlay) that clears to nothing, so it comes back solid.
	const double Look = Sim ? Sim->GhostLook() : 0.0;
	const int32 Want = Look <= 0.0 ? 0 : (Look >= 1.0 ? 1 : 2);
	if (Want == 0)
	{
		if (GhostMode != 0)
		{
			GhostRestore();
			TInlineComponentArray<UMeshComponent*> Meshes(this);
			for (UMeshComponent* C : Meshes)
				if (C && GhostFillMid && C->GetOverlayMaterial() == GhostFillMid) C->SetOverlayMaterial(nullptr);
			for (const TWeakObjectPtr<UStaticMeshComponent>& N : GhostNaniteOff)
				if (N.IsValid()) N->SetForceDisableNanite(false);
			GhostNaniteOff.Reset();
			GhostMode = 0;
			int32 StillWire = 0;
			for (UMeshComponent* C : Meshes)
				for (int32 Slot = 0; C && Slot < C->GetNumMaterials(); ++Slot)
					if (C->GetMaterial(Slot) && (C->GetMaterial(Slot) == GhostMid || C->GetMaterial(Slot) == GhostFillMid)) ++StillWire;
			UE_LOG(LogTemp, Log, TEXT("Torque: ghost look off: solid again (%d slots still on a ghost material)"), StillWire);
		}
		return;
	}
	if (!bGhostTried)
	{
		bGhostTried = true;
		if (UMaterialInterface* Wire = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_RideGhostMatrix.M_RideGhostMatrix")))
			GhostMid = UMaterialInstanceDynamic::Create(Wire, this);
		UMaterialInterface* Fill = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_RideGhostFill.M_RideGhostFill"));
		if (!Fill) Fill = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedHighlight.M_ShedHighlight"));
		if (Fill) GhostFillMid = UMaterialInstanceDynamic::Create(Fill, this);
		if (!GhostMid || !GhostFillMid)
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: ghost materials missing (wire %d, fill %d; run Tools/ue_build_ghost.py)"), GhostMid ? 1 : 0, GhostFillMid ? 1 : 0);
		}
	}
	const FLinearColor Green(0.45f, 1.1f, 0.95f);
	if (GhostMid)
	{
		GhostMid->SetScalarParameterValue(TEXT("Amount"), 1.0f);
		GhostMid->SetScalarParameterValue(TEXT("Opacity"), 0.5f);
	}
	if (GhostFillMid)
	{
		GhostFillMid->SetVectorParameterValue(TEXT("Color"), Green);
		GhostFillMid->SetScalarParameterValue(TEXT("Opacity"), (float)(Want == 2 ? 0.9 * Look : 0.9));
	}
	// The wire is the wireframe, or (not made yet) the fill, see-through.
	UMaterialInterface* WireMat = GhostMid ? (UMaterialInterface*)GhostMid : (UMaterialInterface*)GhostFillMid;
	if (!WireMat) return;
	if (Want == 2 && GhostMode == 1) GhostRestore();
	if (Want == 1 && GhostMode == 2 && GhostFillMid)
	{
		// A fresh ghost while the last one was fading: the fill off again, only the wire.
		TInlineComponentArray<UMeshComponent*> Faded(this);
		for (UMeshComponent* C : Faded)
			if (C && C->GetOverlayMaterial() == GhostFillMid) C->SetOverlayMaterial(nullptr);
	}

	TInlineComponentArray<USceneComponent*> All(this);
	int32 Wired = 0, Filled = 0, Switched = 0, Hair = 0;
	for (USceneComponent* S : All)
	{
		if (!S) continue;
		const bool bHair = S->GetClass()->GetName().Contains(TEXT("Groom"));
		if (bHair)
		{
			// Hair can't take a mesh material: hidden while only the wire shows, back for the fade.
			if (Want == 1 && !GhostHairHidden.Contains(S))
			{
				S->SetHiddenInGame(true);
				GhostHairHidden.Add(S);
				++Hair;
			}
			continue;
		}
		UMeshComponent* C = Cast<UMeshComponent>(S);
		if (!C) continue;
		if (Want == 1)
		{
			if (!GhostComps.Contains(C))
			{
				GhostComps.Add(C);
				GhostMatStart.Add(GhostSavedMats.Num());
				const int32 Slots = C->GetNumMaterials();
				for (int32 Slot = 0; Slot < Slots; ++Slot) GhostSavedMats.Add(C->GetMaterial(Slot));
				for (int32 Slot = 0; Slot < Slots; ++Slot) C->SetMaterial(Slot, WireMat);
				// Its coarsest LOD while it's a wire: the full-detail polygons are so close together
				// they fill in to a solid shape (build 135); the coarse ones read as a mesh.
				// Only the bike's static meshes: the rider's skinned meshes keep their LOD (forcing the
				// coarsest one on them lost the finger and wrist bones the levers and throttle drive).
				int32 LodWas = -1;
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(C))
				{
					LodWas = SMC->ForcedLodModel;
					if (SMC->GetStaticMesh()) SMC->SetForcedLodModel(SMC->GetStaticMesh()->GetNumLODs());
				}
				GhostLodWas.Add(LodWas);
				++Wired;
			}
		}
		else if (GhostFillMid && !C->GetOverlayMaterial())
		{
			C->SetOverlayMaterial(GhostFillMid);
			++Filled;
		}
		// Nanite draws neither wireframes, see-through materials nor overlays: its fallback mesh instead.
		UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
		if (SM && SM->GetStaticMesh() && SM->GetStaticMesh()->HasValidNaniteData() && !SM->bForceDisableNanite)
		{
			SM->SetForceDisableNanite(true);
			GhostNaniteOff.Add(SM);
			++Switched;
		}
	}
	if (Want != GhostMode || Wired > 0 || Filled > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: ghost look %s (amount %.2f): %d meshes wired, %d filled, %d hair hidden, %d taken off Nanite"),
			Want == 1 ? TEXT("wire") : TEXT("fading to solid"), Look, Wired, Filled, Hair, Switched);
	}
	GhostMode = Want;
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
	FTransform Fix(FRotator(0.0, Yaw, 0.0), FVector::ZeroVector, FVector(Scale, Scale * Mirror, Scale));
	// beenster's Fab helmet, when the level script found one (Data/Rider/fab_helmet.json): fitted
	// over our helmet's place by its bounding box (turn, scale, then offset, in our helmet's frame).
	const TSharedPtr<FJsonObject>* HelmetFit = nullptr;
	if ((*Rig)->TryGetObjectField(TEXT("helmet_fit"), HelmetFit))
	{
		auto Three = [&HelmetFit](const TCHAR* Field)
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			return (*HelmetFit)->TryGetArrayField(Field, A) && A->Num() >= 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : FVector::ZeroVector;
		};
		const FVector Turn = Three(TEXT("rotate")), Offset = Three(TEXT("offset"));
		double FitScale = 1.0;
		(*HelmetFit)->TryGetNumberField(TEXT("scale"), FitScale);
		const FTransform Fit(FRotator(Turn.X, Turn.Y, Turn.Z), Offset, FVector(FitScale));
		Fix = Fit * Fix;
		UE_LOG(LogTemp, Log, TEXT("Torque: rider wears the Fab helmet %s (turn %s, scale %.4f, offset %s)"),
			*(*Rig)->GetStringField(TEXT("helmet")), *Turn.ToCompactString(), FitScale, *Offset.ToCompactString());
	}
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

void ATorqueBikePawn::ShowNetWalker(bool bOn, const FVector& Feet, double Yaw, float Dt)
{
	if (!bOn)
	{
		if (bNetWalkerShown && NetWalker && NetWalker->IsValid()) NetWalker->SetShown(false, false, false);
		bNetWalkerShown = false;
		return;
	}
	if (!NetWalker && !bNetWalkerTried && FTorqueAvatar::Count() > 0)
	{
		// Made the first time they get off: the same MetaHuman, height and face as their rider.
		bNetWalkerTried = true;
		const ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
		const FTorqueProfile Look = PS ? FTorqueProfile::Unpack(PS->Profile) : FTorqueProfile::Mine();
		const int32 Colour = PS ? PS->Colour : FTorqueSettings::Get().Colour;
		NetWalkerRoot = NewObject<USceneComponent>(this, TEXT("NetWalkerRoot"));
		NetWalkerRoot->SetupAttachment(Root);
		NetWalkerRoot->SetUsingAbsoluteLocation(true);
		NetWalkerRoot->SetUsingAbsoluteRotation(true);
		NetWalkerRoot->RegisterComponent();
		NetWalkerRoot->SetWorldLocationAndRotation(Feet, FRotator(0.0, Yaw, 0.0));
		NetWalker = MakeUnique<FTorqueAvatar>();
		if (NetWalker->Build(this, NetWalkerRoot, Look.AvatarFor(Colour)))
		{
			NetWalker->SetLook(Look);
			UE_LOG(LogTemp, Log, TEXT("Torque net: %s off their bike, walking as %s"), PS ? *PS->GetPlayerName() : TEXT("a rider"), *NetWalker->Name);
		}
		NetWalkerWas = Feet;
	}
	if (!NetWalker || !NetWalker->IsValid() || !NetWalkerRoot) return;
	if (!bNetWalkerShown)
	{
		NetWalker->SetShown(true, true, true);
		bNetWalkerShown = true;
		NetWalkerWas = Feet;
	}
	NetWalkerRoot->SetWorldLocationAndRotation(Feet, FRotator(0.0, Yaw, 0.0));
	FTorqueAvatarInput In;
	In.Velocity = Dt > 1.0e-4f ? (Feet - NetWalkerWas) / Dt : FVector::ZeroVector;
	In.Velocity.Z = 0.0;
	In.Facing = FRotator(0.0, Yaw, 0.0);
	NetWalker->Pose(In, Dt);
	NetWalkerWas = Feet;
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
	// The seat's top under the hips: the rider rig's pelvis less its height over the seat it was made
	// sat on (the Speed Twin's), so the MetaHuman sits by its own hips; and the tank's back edge.
	T.Seat = R.Hip + (TorqueRiderSeat::TwinSeat - RiderRig.PelvisRest());
	T.bSeat = true;
	T.TankRearX = RiderTankRearX;
	// [rider pose] On a catalogue bike, its own knees, pegs and tank: the MetaHuman's knees go by the
	// tank's knee pads, the balls of its feet on the pegs.
	T.bOnLayout = bRiderOnLayout;
	T.FeetDown = RiderFeetDown;
	for (int32 S = 0; S < 2; ++S) T.KeepOnPeg[S] = RiderKeepOnPeg[S];
	T.bLaidBack = bRiderLaidBack;
	if (bRiderOnLayout)
	{
		T.TankHalfW = RiderTankHalfW;
		T.BackDeg = RiderStyleBackDeg;
		for (int32 S = 0; S < 2; ++S)
		{
			T.Knee[S] = RiderKnee[S];
			T.Peg[S] = RiderPeg[S];
		}
	}
	T.Neck = R.Neck;
	T.Look = R.Look;
	T.Limp = R.Limp;
	for (int32 S = 0; S < 2; ++S)
	{
		T.HandOff[S] = R.HandOff[S];
		T.Wrist[S] = R.Wrist[S];
		T.WristUnrolled[S] = R.WristUnrolled[S];
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
	const FVector TwinSeat = TorqueRiderSeat::TwinSeat;
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
			// [rider pose] The feet go down under the hips, a little ahead of them (as on the Speed Twin
			// the rig was made on), not ahead with pegs that sit forward (beenster's marked poses).
			if (RiderAvatar.IsValid() && RiderAvatar.RideReport.LegReach > 1.0)
			{
				// (And 18 cm out from each hip joint, the legs straight down and a little apart.)
				In.FootDownL = FVector(RiderAvatar.RideReport.HipJointAt[0].X + 6.0, RiderAvatar.RideReport.HipJointAt[0].Y - 18.0, In.FootDownL.Z);
				In.FootDownR = FVector(RiderAvatar.RideReport.HipJointAt[1].X + 6.0, RiderAvatar.RideReport.HipJointAt[1].Y + 18.0, In.FootDownR.Z);
			}
			// [rider pose] Too tall to reach the road (an MX bike's saddle): lean the bike onto the left
			// foot, about the tyres' contact line, as far as that leg needs, and keep the right on its peg.
			// (The MetaHuman's own hip joint and leg, as last posed; leant left by A, a point y across and
			// z up from the ground sits (z - Ground) cos A + y sin A up, so the ankle's mark rises by that.)
			const double Ground = -Sim->Build.RearRadius * 100.0;
			const double AnkleUp = In.FootDownL.Z - Ground;
			double LeanWant = 0.0;
			if (In.FeetDown > 0.0 && RiderAvatar.IsValid() && RiderAvatar.RideReport.LegReach > 1.0 && !bThrown)
			{
				const FVector HipL = RiderAvatar.RideReport.HipJointAt[0];
				const double Reach = RiderAvatar.RideReport.LegReach * 0.99;
				for (double Deg = 0.0; Deg <= 20.0; Deg += 0.25)
				{
					LeanWant = Deg;
					const double Ar = FMath::DegreesToRadians(Deg);
					const FVector Mark(In.FootDownL.X, In.FootDownL.Y, Ground + (AnkleUp - In.FootDownL.Y * FMath::Sin(Ar)) / FMath::Cos(Ar));
					if ((Mark - HipL).Size() <= Reach) break;
				}
			}
			// (Within a few degrees of reaching, the rider stretches onto his toes and the bike stays upright:
			// beenster, both feet down at a standstill, the bike vertical.)
			if (LeanWant < 4.0) LeanWant = 0.0;
			const double LeanTo = LeanWant * FMath::SmoothStep(0.0, 1.0, In.FeetDown);
			StopLeanDeg = MoveToward(StopLeanDeg, LeanTo, 25.0 * (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0));
			if (StopLeanDeg > 0.01)
			{
				const double Ar = FMath::DegreesToRadians(StopLeanDeg);
				In.FootDownL.Z = Ground + (AnkleUp - In.FootDownL.Y * FMath::Sin(Ar)) / FMath::Cos(Ar);
			}
			In.KeepOnPeg[1] = FMath::Clamp(StopLeanDeg / 2.0, 0.0, 1.0);
		}
		else StopLeanDeg = 0.0;
		for (int32 S = 0; S < 2; ++S) RiderKeepOnPeg[S] = In.KeepOnPeg[S];
		// The head turns toward where the corner goes (the apex) and stays nearer level than the bike.
		// Unreal: + yaw looks right, + roll drops the right side.
		const double HeadLean = FMath::Clamp(BodyLean, -55.0, 55.0);
		// (Settings > Gameplay > Look into corners: off, the head stays square to the bike.)
		const double IntoCorner = FTorqueSettings::Get().bLookIntoCorner ? 0.45 : 0.0;
		In.Look = FRotator(LookPitch * -0.4 - 3.0 * Braking, LookYaw * 0.5 + IntoCorner * HeadLean, -0.45 * HeadLean);
		// Right hand: the throttle, and the front brake lever under two fingers.
		In.Throttle = TestGripThrottleHold >= 0.0 ? FMath::Clamp(TestGripThrottleHold, 0.0, 1.0) : Throttle;
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
		else
		{
			// [gestures] The nod: the head dips and comes back up over NodTime. The wave: the left hand
			// eased off the bar and out (FRiderRig), back onto the bar as it's let go.
			const double Now = GestureNow();
			const double NodT = (Now - NodAt) / NodTime;
			if (NodT >= 0.0 && NodT < 1.0) In.Look.Pitch -= 16.0 * FMath::Sin(NodT * PI);
			const double WaveT = FMath::Clamp((Now - WaveAt) / WaveEase, 0.0, 1.0);
			In.Wave = bWaving ? WaveT : 1.0 - WaveT;
			In.WaveTime = Now - WaveAt;
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
			// The clutch held in (LB, or Left Alt): the lever pulled to the bar.
			const double Lever = Sim->ClutchIn ? 1.0 : 0.0;
			In.LeverFingers[0] = FMath::Max3(FingersShift, FMath::Clamp(Held * 3.0, 0.0, 1.0), Lever);
			In.LeverPull[0] = FMath::Max3(PullShift, Held, Lever);
		}
		for (int32 S = 0; S < 2; ++S)
			if (TestLeverHold[S] >= 0.0)
			{
				In.LeverFingers[S] = 1.0;
				In.LeverPull[S] = FMath::Clamp(TestLeverHold[S], 0.0, 1.0);
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
		RiderFeetDown = FMath::Clamp(In.FeetDown, 0.0, 1.0);
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
	// beenster: a ride starts with the engine off; LB + RB held (or I) starts it. Test runs keep the
	// old start-on-arrival so their rides go as before.
	static const bool bTestStart = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) ||
		FParse::Param(FCommandLine::Get(), TEXT("TorqueAutoRide")) || FCString::Strifind(FCommandLine::Get(), TEXT("TorqueShedAction=")) != nullptr;
	// [yard] Got back on a bike left idling in the shed: it's still running.
	UTorqueBuildSubsystem* IdleShed = bYard && RideBay >= 0 ? UTorqueBuildSubsystem::Of(this) : nullptr;
	const bool bWasIdling = IdleShed && IdleShed->IsIdling(RideBay);
	if (IdleShed) IdleShed->SetIdling(RideBay, false);
	if (bWasIdling)
	{
		SoundAudio->Play();
		Sound->Catch();
		Sim->EngineOn = true;
		bEngineWas = true;
		EngineNote = TEXT("Engine running");
		EngineNoteFor = 2.0;
	}
	else if (!bTestStart)
	{
		SoundAudio->Play();
		Sim->StopEngine();
		bEngineWas = false;
		EngineNote = TEXT("Engine off: hold LB + RB (or press I) to start it");
		EngineNoteFor = 5.0;
	}
	else if (Sfx && Engine && Engine->Spec.Kick)
	{
		Sfx->PlayMadeOn(TorqueEngine::RenderStarter(Engine->Spec, true, 1.0, FMath::RandRange(1, 3)), TorqueEngine::Rate, Root, FVector(0.0, 0.0, 45.0));
		EngineCatchIn = 0.5;
		Sim->EngineOn = false;
	}
	else if (Sfx && Sfx->PlayOn(TEXT("starter"), Root, FVector(0.0, 0.0, 45.0), 0.0f, 1.3f))
	{
		EngineCatchIn = 1.1;
		Sim->EngineOn = false;
	}
	else
	{
		SoundAudio->Play();
		Sound->Catch();
	}
	if (Sfx) HornId = Sfx->StartBikeHorn(Root, RideBay >= 0 ? RideBay : UTorqueBuildSubsystem::BikeFor(this));   // (the drawn bike's own horn)
}

// [lights] The lamps each frame: on our own bike from the ignition, the brakes as the rider holds
// them and the lean and turn for the indicators' self-cancel, with the relay ticking behind the
// clocks; on someone else's from their snapshot (the switches) and the brakes it carries.
void ATorqueBikePawn::StepLights(float Dt, bool bRemote)
{
	const ATorqueGameState* LightGs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	const float Day = LightGs ? LightGs->GetDayLight() : 1.0f;
	const bool bRunning = bRemote ? Lights.bIgnition : Sim->EngineOn;
	const double TurnRate = -Sim->AngularVelocity.y;   // (sim y is up: + turning right, as bike_lamps.gd)
	const int32 Heard = Lights.Step(Dt, bRunning, FrontBrake, RearBrake, Sim->LeanAngle, TurnRate, !bRemote, Day);
	if (Heard != 0 && !bRemote)
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this))
			Sfx->PlayMadeOn(FTorqueLights::Click(Heard > 0), FTorqueLights::ClickRate, Root, FVector(60.0, 0.0, 100.0), -14.0f);
}

FString ATorqueBikePawn::TestLights(const FString& What)
{
	FString Said;
	bool bOk = true;
	const FString W = What.ToLower();
	if (W == TEXT("dipped")) { Lights.bHeadlight = true; Lights.bMainBeam = false; Lights.bFog = false; }
	else if (W == TEXT("main")) { Lights.bHeadlight = true; Lights.bMainBeam = true; }
	else if (W == TEXT("fog")) { Said = Lights.ToggleFog(bOk); if (bOk && !Lights.bFog) Said = Lights.ToggleFog(bOk); }
	else if (W == TEXT("off")) { Lights.bHeadlight = Lights.bMainBeam = Lights.bFog = false; }
	else if (W == TEXT("cycle")) Said = Lights.CycleBeam(bOk);
	else if (W == TEXT("left") || W == TEXT("right"))
	{
		if (Lights.Side != (W == TEXT("left") ? -1 : 1)) Said = Lights.Indicate(W == TEXT("left") ? -1 : 1, bOk);
	}
	else if (W == TEXT("hazard")) { if (!Lights.bHazard) Said = Lights.ToggleHazard(bOk); }
	else if (W == TEXT("cancel"))
	{
		if (Lights.bHazard) Lights.ToggleHazard(bOk);
		if (Lights.Side != 0) Lights.Indicate(Lights.Side, bOk);
	}
	else if (W == TEXT("brake") || W == TEXT("nobrake")) Lights.bTestBrake = W == TEXT("brake");
	else if (W == TEXT("night") || W == TEXT("day"))
	{
		ATorqueGameState* Gs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		if (Gs && HasAuthority()) Gs->SetHour(W == TEXT("night") ? 23.0f : -1.0f);
		else Said = TEXT("(the clock is the host's)");
	}
	else if (W != TEXT("state") && !W.IsEmpty()) Said = TEXT("(unknown: ") + What + TEXT(")");
	// Shown now, as the next frame would (the stop lamp needs Step for the brakes).
	const ATorqueGameState* LightGs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	const float Day = LightGs ? LightGs->GetDayLight() : 1.0f;
	Lights.Step(0.0, Sim->EngineOn, FrontBrake, RearBrake, Sim->LeanAngle, 0.0, false, Day);
	Lights.ShowNow(Day);
	const FString Report = FString::Printf(TEXT("lights:%s%s%s -> engine %s, daylight %.2f; %s"), *W, Said.IsEmpty() ? TEXT("") : TEXT(" "), *Said,
		Sim->EngineOn ? TEXT("running") : TEXT("off"), Day, *Lights.Describe());
	UE_LOG(LogTemp, Log, TEXT("Torque: %s"), *Report);
	return Report;
}

void ATorqueBikePawn::LightsNote(const FString& Text, bool bOk)
{
	// What the switch did (or why it couldn't), a moment on the HUD, and its click.
	EngineNote = Text;
	EngineNoteFor = bOk ? 1.2 : 2.0;
	if (bOk)
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("switch"), GetActorLocation() + FVector(0.0, 0.0, 100.0), -10.0f);
}

void ATorqueBikePawn::UpdateRumble(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	float Low = 0.0f, High = 0.0f;
	if (PC && Sim && Dt > 0.0f && !UGameplayStatics::IsGamePaused(this))
	{
		const double V = FMath::Abs(Sim->Speed);
		const float Moving = (float)FMath::Clamp(V / 12.0, 0.0, 1.0);
		float Grid = 0.0f, Rough = 0.0f, Gravel = 0.0f;
		for (int32 I = 0; I < 2; ++I)
		{
			const Wheel& W = Sim->Wheels[I];
			const int32 Surf = W.Contact ? W.Surf : -1;
			if (Surf == SURF_GRID) Grid += 0.5f;
			// Gravel only a light buzz (beenster, build 141: too strong for a whole gravel road); grass rougher.
			if (Surf == SURF_GRASS) Rough += 0.5f;
			if (Surf == SURF_GRAVEL) { Rough += 0.5f * 0.35f; Gravel += 0.5f; }
			// (a knock as a tyre goes onto steel, a lighter one onto tar banding or paint)
			if (Surf != RumbleSurfWas[I] && V > 1.0)
			{
				if (Surf == SURF_STEEL) RumbleKnock = FMath::Max(RumbleKnock, 0.55f);
				else if (Surf == SURF_SLICK) RumbleKnock = FMath::Max(RumbleKnock, 0.18f);
			}
			RumbleSurfWas[I] = Surf;
		}
		RumbleKnock *= FMath::Exp(-Dt / 0.06f);
		// (the tyres squirming in loose gravel isn't a slide worth shaking for: much less of it there)
		const float Slide = (float)FMath::Max(FMath::Max(FMath::Abs(Sim->Wheels[0].Slip), FMath::Abs(Sim->Wheels[1].Slip)) * 2.0 - 0.4, 0.0) * (1.0f - 0.6f * Gravel);
		Low = Grid * (0.35f + 0.3f * Moving) + Rough * 0.3f * Moving + RumbleKnock * 0.7f;
		High = Grid * 0.55f * Moving + Rough * 0.15f * Moving + RumbleKnock + FMath::Min(Slide, 1.0f) * 0.35f;
	}
	Low = FMath::Clamp(Low, 0.0f, 1.0f);
	High = FMath::Clamp(High, 0.0f, 1.0f);
	FSonyPad::SetRumble(Low, High);
	if (!PC) return;
	// (an Xbox pad: the big motor on the left, the small on the right)
	for (int32 M = 0; M < 2; ++M)
	{
		const float Value = M == 0 ? Low : High;
		uint64& Handle = RumbleHandle[M];
		if (Value < 0.01f)
		{
			if (Handle != 0) PC->PlayDynamicForceFeedback(0.0f, 0.0f, M == 0, false, false, M == 1, EDynamicForceFeedbackAction::Stop, Handle);
			Handle = 0;
			continue;
		}
		Handle = PC->PlayDynamicForceFeedback(Value, -1.0f, M == 0, false, false, M == 1, Handle != 0 ? EDynamicForceFeedbackAction::Update : EDynamicForceFeedbackAction::Start, Handle);
	}
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
		Sim->EngineOn = true;
		bEngineWas = true;
	}
	// Switched off and on again on the key (the sim's starter): heard catching once it fires.
	if (Sim->EngineOn && !bEngineWas) Sound->Catch();
	bEngineWas = Sim->EngineOn;
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
	In.OnTarmac = Under == SURF_TARMAC || Under == SURF_GRID || Under == SURF_STEEL || Under == SURF_SLICK;
	In.Grid = (F.Contact && F.Surf == SURF_GRID ? 1.0 : 0.0) + (R.Contact && R.Surf == SURF_GRID ? 1.0 : 0.0);
	In.Airborne = !F.Contact && !R.Contact;
	In.Inside = (CameraMode == 1 || CameraMode == 2) ? 1.0 : 0.0;
	In.EngineOn = Sim->EngineOn;
	// The weather round you: its wind and rain heard (Weather/WeatherModel.h).
	if (const ATorqueGameState* WeatherGs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr)
	{
		const TorqueWeather::State& Air = WeatherGs->GetWeatherState();
		const double Open = WeatherGs->WeatherOpen();   // (under a roof, out of most of it)
		In.AirWind = Air.Wind * FMath::Lerp(0.3, 1.0, Open);
		In.Gust = Air.Gust * FMath::Lerp(0.3, 1.0, Open);
		In.Rain = TorqueWeather::RainShow(Air) * Open;
	}
	// On a ride, the place round you (World/TorqueRideWorld): the sea, a beck, a town, the sheep and cattle.
	ATorqueRideWorld::PlaceHeard(In.Sea, In.Water, In.Town, In.Sheep, In.Cattle);
	if (!bRideGridsSet)
	{
		TArray<TPair<FVector, FVector>> RideGrids;
		if (ATorqueRideWorld::CattleGrids(RideGrids))
		{
			bRideGridsSet = true;
			for (const TPair<FVector, FVector>& G : RideGrids)
			{
				CattleGrid Grid;
				Grid.Centre = V3(G.Key.X, G.Key.Y, G.Key.Z);   // (the ride's frame is the sim's)
				Grid.Along = V3(G.Value.X, 0.0, G.Value.Z).Normalized();
				Grid.HalfWidth = G.Value.Y * 0.5;
				Sim->Grids.push_back(Grid);
			}
			UE_LOG(LogTemp, Log, TEXT("Torque: Ride UK's %d cattle grids in the sim"), RideGrids.Num());
		}
	}
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
	if (Line.Num() < 2 || bYard) return;   // [yard] riding about: no way along, no finish
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
	if (bOnFoot)
	{
		// On foot, in first person at eye height (the shed walker's view).
		// (FootPitch up is looking up: the mouse or stick pushed away, unless Invert look is on.)
		Camera->SetWorldLocationAndRotation(FootPos + FVector(0.0, 0.0, 165.0), FRotator(FootPitch, FootYaw, 0.0));
		Camera->SetFieldOfView(FTorqueSettings::Get().FovFor(80.0f));
		return;
	}
	const FVector BikePos = GetActorLocation();
	const FQuat BikeRot = GetActorQuat();
	const FVector Fwd = BikeRot.GetForwardVector();
	FVector Flat(Fwd.X, Fwd.Y, 0.0);
	Flat = Flat.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	ShowHead(CameraMode != 1 && CameraMode != 2);
	ShowRider(true);
	if (CameraMode == CamDriveBy)
	{
		UpdateDriveBy(Dt);
		return;
	}
	if (CameraMode == 3)
	{
		// Showroom: round the bike at a walking-round distance, eye height a little above the tank,
		// starting from the front three-quarter on the right (the side the photos are from). The
		// right stick nudges the angle and height.
		if (!bOrbitHeld) OrbitYaw = FMath::Fmod(OrbitYaw + 12.0 * Dt, 360.0);
		const double Yaw = FMath::DegreesToRadians(OrbitYaw + LookYaw);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Flat);
		FVector Centre = BikePos + Flat * 5.0 + FVector(0, 0, 55.0);
		if (OrbitAt > 0)
		{
			// A test's close look at one of the rider's hands on its grip.
			const FName HandName(OrbitAt == 1 ? TEXT("hand_l") : TEXT("hand_r"));
			FVector HandAt = RiderAvatar.IsValid() ? RiderAvatar.BoneWorld(HandName) : FVector::ZeroVector;
			if (HandAt.IsZero() && RiderBody && RiderBody->GetBoneIndex(HandName) != INDEX_NONE)
				HandAt = RiderBody->GetBoneLocationByName(HandName, EBoneSpaces::WorldSpace);
			if (!HandAt.IsZero()) Centre = HandAt;
		}
		const FVector Pos = Centre + (Flat * FMath::Cos(Yaw) + Right * FMath::Sin(Yaw)) * OrbitDist
			+ FVector(0, 0, OrbitRise != 0.0 ? OrbitRise : 55.0 - LookPitch * 4.0);
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
		// [first person] Per bike: aimed so the hands on the grips are in view, low in the picture
		// (FirstPersonAim), eased so the head's sway doesn't shake it.
		double AimPitch = -21.0, AimFovAdd = 0.0;
		if (!bChin) FirstPersonAim(EyeLocal, AimPitch, AimFovAdd);
		const double AimK = bFpAimInit ? 1.0 - FMath::Exp(-3.0 * Dt) : 1.0;
		bFpAimInit = true;
		FpPitch = FMath::Lerp(FpPitch, AimPitch, AimK);
		FpFovAdd = FMath::Lerp(FpFovAdd, AimFovAdd, AimK);
		Rot.Pitch = (bChin ? -12.0 : FpPitch) + FMath::RadiansToDegrees(Sim->PitchAngle) * 0.7;
		Rot.Roll = FMath::RadiansToDegrees(Sim->LeanAngle) * (bChin ? 0.55 : 0.6);
		Rot.Yaw += LookYaw;
		Rot.Pitch += LookPitch;
		// Looking into corners: your view turns towards the inside of the bend, as the rider's head does.
		if (FTorqueSettings::Get().bLookIntoCorner) Rot.Yaw += 0.25 * FMath::Clamp(BodyLean, -55.0, 55.0);
		Camera->SetWorldLocationAndRotation(Eye, Rot);
		Camera->SetFieldOfView(FMath::Min(FTorqueSettings::Get().FovFor(bChin ? 100.0f : 108.0f) + (bChin ? 0.0f : (float)FpFovAdd), 140.0f));   // Settings > Gameplay > Field of view
		return;
	}

	// Chase: behind and above, following the road's direction, the horizon mostly level.
	const FVector Target = BikePos + FVector(0, 0, 110.0);
	const FVector Behind = FRotator(0.0, LookYaw, 0.0).RotateVector(-Flat);
	// How far back, how high and how far down it looks: the player's (FTorqueProfile, AdjustChase).
	const FTorqueProfile& Prof = FTorqueProfile::Mine();
	const FVector Want = Target + Behind * (double)Prof.ChaseDist + FVector(0, 0, (double)Prof.ChaseHeight - LookPitch * 6.0);
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
	Rot.Pitch -= Prof.ChaseTilt;
	Rot.Roll = FMath::RadiansToDegrees(Sim->LeanAngle) * 0.25;
	Camera->SetWorldLocationAndRotation(Pos, Rot);
	Camera->SetFieldOfView(FTorqueSettings::Get().FovFor(72.0f));   // Settings > Gameplay > Field of view
}

// First person: the grips' middle (pawn local, as the rider is fitted to this bike: the layout's grips,
// else the rig's own) seen from the eye. The view looks down far enough that it sits about three
// quarters of the way from the middle of the picture to its bottom edge, so the bars, the gloves on
// them and the clocks show on every bike, low clip-ons or high ape hangers, with the road above. Past
// 40 degrees down the view stops tilting (you'd lose the road) and widens a little instead.
void ATorqueBikePawn::FirstPersonAim(const FVector& EyeLocal, double& OutPitch, double& OutFovAdd) const
{
	OutPitch = -21.0;
	OutFovAdd = 0.0;
	FVector GripMid = FVector::ZeroVector;
	if (!RiderGrip[0].IsZero() && !RiderGrip[1].IsZero()) GripMid = (RiderGrip[0] + RiderGrip[1]) * 0.5;
	else if (RiderRig.IsValid()) GripMid = (RiderRig.GripRest(0) + RiderRig.GripRest(1)) * 0.5;
	if (GripMid.IsZero()) return;
	const double Ahead = GripMid.X - EyeLocal.X, Below = EyeLocal.Z - GripMid.Z;
	if (Ahead < 5.0) return;   // (grips behind the eye: something's not fitted yet)
	const double DownDeg = FMath::RadiansToDegrees(FMath::Atan2(Below, Ahead));
	// Half the picture's height in degrees, from the lens and the screen's shape.
	double Aspect = 16.0 / 9.0;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Px = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Px.X > 0 && Px.Y > 0) Aspect = (double)Px.X / Px.Y;
	}
	const double HFov = FTorqueSettings::Get().FovFor(108.0f);
	const double HalfV = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HFov * 0.5)) / Aspect));
	OutPitch = FMath::Clamp(-(DownDeg - 0.72 * HalfV), -40.0, -6.0);
	// Still below the bottom tenth at 40 degrees down: widen the lens till they're in.
	const double Short = DownDeg + OutPitch - 0.85 * HalfV;
	if (Short > 0.0) OutFovAdd = FMath::Min(Short * 2.0 * Aspect, 20.0);
}

// The Camera key (C, R1, Create / View): first person, third person, drive-by, round again. A test
// view (the chin camera, the showroom) goes on to first person. Your choice is kept with your
// character, and the ride after starts in it.
void ATorqueBikePawn::NextCamera()
{
	CameraMode = CameraMode == CamFirst ? CamChase : CameraMode == CamChase ? CamDriveBy : CamFirst;
	bCrashCam = false;   // (changed by hand while you're off: it stays as you chose)
	bCamInit = false;
	bDriveByPlaced = false;
	FTorqueProfile& Prof = FTorqueProfile::Mine();
	Prof.RideView = CameraMode;
	Prof.Save();
	const auto K = [](ETorqueKey A) { return FTorqueKeys::Text(A); };
	CamNote = CameraMode == CamFirst ? FString(TEXT("First person"))
		: CameraMode == CamDriveBy ? FString(TEXT("Drive-by camera"))
		: FString::Printf(TEXT("Third person   %s / %s distance   %s / %s height   %s / %s tilt   (pad: R1 held + right stick, D-pad)"),
			*K(ETorqueKey::CamCloser), *K(ETorqueKey::CamFarther), *K(ETorqueKey::CamHigher), *K(ETorqueKey::CamLower),
			*K(ETorqueKey::CamTiltUp), *K(ETorqueKey::CamTiltDown));
	CamNoteFor = CameraMode == CamChase ? 4.0 : 2.0;
}

// Third person, set while riding: the keys (Settings > Controller > Key bindings) step it or, held,
// move it smoothly; the pad's R1 held moves it with the right stick (up / down: higher / lower, left /
// right: closer / further) and the D-pad (up / down: tilt up / down). Saved with your character a
// moment after you stop. True if it moved.
bool ATorqueBikePawn::AdjustChase(const APlayerController* PC, float Dt, double PadX, double PadY, bool bPadHeld, int32 PadTilt)
{
	if (CameraMode != CamChase || !PC) return false;
	FTorqueProfile& Prof = FTorqueProfile::Mine();
	const FTorqueProfile Was = Prof;
	// A key: a step when it's pressed (the mouse wheel's click), then steadily while it's held.
	auto KeyAxis = [PC, Dt](ETorqueKey Plus, ETorqueKey Minus, double StepBy, double PerSecond) {
		double Move = 0.0;
		if (FTorqueKeys::Just(PC, Plus)) Move += StepBy;
		else if (FTorqueKeys::Down(PC, Plus)) Move += PerSecond * Dt;
		if (FTorqueKeys::Just(PC, Minus)) Move -= StepBy;
		else if (FTorqueKeys::Down(PC, Minus)) Move -= PerSecond * Dt;
		return Move;
	};
	double DistBy = KeyAxis(ETorqueKey::CamFarther, ETorqueKey::CamCloser, 30.0, 300.0);
	double HeightBy = KeyAxis(ETorqueKey::CamHigher, ETorqueKey::CamLower, 15.0, 120.0);
	double TiltBy = KeyAxis(ETorqueKey::CamTiltDown, ETorqueKey::CamTiltUp, 1.5, 15.0);
	if (bPadHeld)
	{
		// (Up is +y on the look stick: pushed up raises the camera.)
		DistBy += PadX * 300.0 * Dt;
		HeightBy += PadY * 120.0 * Dt;
		TiltBy += PadTilt * 15.0 * Dt;
	}
	if (DistBy == 0.0 && HeightBy == 0.0 && TiltBy == 0.0) return false;
	Prof.ChaseDist += (float)DistBy;
	Prof.ChaseHeight += (float)HeightBy;
	Prof.ChaseTilt += (float)TiltBy;
	Prof.ClampChase();
	if (Prof.ChaseDist == Was.ChaseDist && Prof.ChaseHeight == Was.ChaseHeight && Prof.ChaseTilt == Was.ChaseTilt) return false;
	ChaseSaveIn = 1.0;
	CamNote = FString::Printf(TEXT("Third person: %.1f m back, %.1f m up, tilted %+.0f°"),
		Prof.ChaseDist / 100.0f, (Prof.ChaseHeight + 110.0f) / 100.0f, Prof.ChaseTilt);
	CamNoteFor = 1.5;
	return true;
}

// Where the drive-by camera stands next: down the road ahead of you (the route on Buttertubs, the way
// you're going in the yard), far enough that you ride up to it, at one side then the other, on the
// ground at about head height, where it can see the road. Falling off, just beyond where you're
// sliding to, so you slide towards it.
void ATorqueBikePawn::PlaceDriveBy(bool bCrash)
{
	UWorld* World = GetWorld();
	const FVector BikeAt = GetActorLocation();
	FVector Going = DirToUE(Sim->LinearVelocity) * 100.0;   // cm/s
	Going.Z = 0.0;
	const double SpeedCm = Going.Size();
	FVector Fwd2 = GetActorForwardVector();
	Fwd2.Z = 0.0;
	Fwd2 = Fwd2.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Heading2 = SpeedCm > 150.0 ? Going / SpeedCm : Fwd2;
	// About four seconds ahead riding (20 to 80 m); a fall, a second's slide on (8 to 20 m).
	const double AheadCm = bCrash ? FMath::Clamp(SpeedCm * 0.9, 800.0, 2000.0) : FMath::Clamp(SpeedCm * 4.0, 2000.0, 8000.0);
	FVector Spot = BikeAt + Heading2 * AheadCm, Along = Heading2;
	if (!bCrash && !bYard && Line.Num() > 1)
	{
		// Along the route from where you are, whichever way you're riding it.
		const int32 From = FMath::Clamp(Near, 0, Line.Num() - 2);
		const FVector Tangent = FVector(Line[From + 1].X - Line[From].X, Line[From + 1].Z - Line[From].Z, 0.0).GetSafeNormal();
		const int32 Step = FVector::DotProduct(Tangent, Heading2) >= 0.0 ? 1 : -1;
		double Run = 0.0;
		int32 At = From;
		while (Run * 100.0 < AheadCm && At + Step >= 0 && At + Step < Line.Num())
		{
			Run += FVector2D(Line[At + Step].X - Line[At].X, Line[At + Step].Z - Line[At].Z).Size();
			At += Step;
		}
		const int32 Prev = FMath::Clamp(At - Step, 0, Line.Num() - 1);
		const FVector RoadDir(Line[At].X - Line[Prev].X, Line[At].Z - Line[Prev].Z, 0.0);
		if (At != From && !RoadDir.IsNearlyZero())
		{
			Spot = ToUE(TorqueSim::V3(Line[At].X, Line[At].Y, Line[At].Z));
			Along = RoadDir.GetSafeNormal();
		}
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Along).GetSafeNormal();
	const FVector RoadEye = Spot + FVector(0.0, 0.0, 100.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueDriveBy), true, this);
	bool bFound = false;
	FVector Best = RoadEye + FVector(0.0, 0.0, 250.0);   // (nowhere clear: up above the road)
	const double Offsets[] = {650.0, 420.0, 260.0};
	for (int32 Pick = 0; Pick < 6 && !bFound && World; ++Pick)
	{
		const double SideSign = (Pick % 2 == 0) ? DriveBySide : -DriveBySide;
		const FVector Foot = Spot + Right * (SideSign * Offsets[Pick / 2]);
		FHitResult Ground;
		FVector Cam = Foot + FVector(0.0, 0.0, 150.0);
		if (World->LineTraceSingleByChannel(Ground, Foot + FVector(0.0, 0.0, 3000.0), Foot - FVector(0.0, 0.0, 5000.0), ECC_Visibility, Params))
			Cam = Ground.ImpactPoint + FVector(0.0, 0.0, bCrash ? 170.0 : 120.0 + 40.0 * (Pick % 2));
		FHitResult Block;
		if (!World->LineTraceSingleByChannel(Block, Cam, RoadEye, ECC_Visibility, Params))
		{
			Best = Cam;
			bFound = true;
			DriveBySide = SideSign;
		}
	}
	DriveBySide = -DriveBySide;   // the next one on the other side
	DriveByPos = Best;
	DriveByBlocked = 0.0;
	bDriveByPlaced = true;
	const FVector Subject = BikeAt + FVector(0.0, 0.0, 70.0);
	DriveByLook = Subject;
	DriveByFov = FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(350.0 / FMath::Max(FVector::Dist(DriveByPos, Subject), 1.0))), 12.0, 70.0);
}

// Drive-by: standing still, it turns to follow you (and you and the bike apart, after a fall) and
// zooms as a roadside camera would, so you're about the same size in the picture near or far. Once
// you're well past it, too far away, or out of its sight for a moment, it moves on ahead.
void ATorqueBikePawn::UpdateDriveBy(float Dt)
{
	const FVector BikeAt = GetActorLocation() + FVector(0.0, 0.0, 70.0);
	FVector Subject = BikeAt;
	double Span = 0.0;
	if (bThrown && RiderRoot)
	{
		const FVector RiderAt = RiderRoot->GetComponentTransform().TransformPosition(HeadLocal - FVector(0.0, 0.0, 50.0));
		Subject = (BikeAt + RiderAt) * 0.5;
		Span = FVector::Dist(BikeAt, RiderAt);
	}
	if (!bDriveByPlaced) PlaceDriveBy(Sim->IsCrashed);
	else
	{
		const double FarCm = FVector::Dist(DriveByPos, Subject);
		FVector Gone = Subject - DriveByPos;
		Gone.Z = 0.0;
		FVector Going = DirToUE(Sim->LinearVelocity);
		Going.Z = 0.0;
		const bool bPast = !Sim->IsCrashed && FVector::DotProduct(Gone, Going) > 0.0 && FarCm > 2500.0;
		if (bPast || FarCm > 12000.0 || (!Sim->IsCrashed && DriveByBlocked > 0.7)) PlaceDriveBy(Sim->IsCrashed);
	}
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueDriveBySight), true, this);
		FHitResult Block;
		if (World->LineTraceSingleByChannel(Block, DriveByPos, Subject, ECC_Visibility, Params)) DriveByBlocked += Dt;
		else DriveByBlocked = 0.0;
	}
	const double Away = FMath::Max(FVector::Dist(DriveByPos, Subject), 1.0);
	DriveByLook = FMath::Lerp(DriveByLook, Subject, 1.0 - FMath::Exp(-10.0 * Dt));
	const double WantFov = FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan((350.0 + Span * 0.7) / Away)), 12.0, 70.0);
	DriveByFov = FMath::Lerp(DriveByFov, WantFov, 1.0 - FMath::Exp(-4.0 * Dt));
	Camera->SetWorldLocationAndRotation(DriveByPos, (DriveByLook - DriveByPos).Rotation());
	Camera->SetFieldOfView((float)DriveByFov);
}

// [dash] "Torque trip:" the dash's trip (the ground covered), speed x frame time, the route's
// progress (Buttertubs) and the seconds ridden, so the trip can be checked against the road.
void ATorqueBikePawn::TripLog(const TCHAR* Why)
{
	if (!Sim) return;
	const double DoneM = Line.Num() > 1 ? RouteLengthM * Near / (Line.Num() - 1) : 0.0;
	UE_LOG(LogTemp, Log, TEXT("Torque trip (%s): dash %.3f km, speed x time %.3f km, %s, %.1f s ridden, now %.1f km/h"),
		Why, Dash.TripKm, TripSpeedKm, bYard ? TEXT("in the yard (no route)") : *FString::Printf(TEXT("Buttertubs route %.3f km of %.3f"), DoneM / 1000.0, RouteLengthM / 1000.0),
		TripLogTime, FMath::Abs(Sim->Speed) * 3.6);
}

void ATorqueBikePawn::DrawHud()
{
	const FTorqueSettings& Set = FTorqueSettings::Get();   // Settings > Gameplay > Units
	const double DoneM = Line.Num() > 1 ? RouteLengthM * Near / (Line.Num() - 1) : 0.0;
	const int32 Mins = (int32)(RideTime / 60.0);
	const double Secs = RideTime - Mins * 60.0;
	HudLines.Reset();
	auto Add = [this](const FString& T, FColor C, float Size) { HudLines.Add({T, C, Size}); };
	// [dash] The speed, gear and revs are on the speedo now (ATorqueHUD::DrawSpeedo, hud.gd's); the
	// gears and ABS show here for a moment as they're switched.
	if (EngineNoteFor > 0.0) Add(EngineNote, FColor(255, 220, 120), 1.2f);
	if (GearNoteFor > 0.0)
		Add(FString::Printf(TEXT("%s gears   ABS %s"), bManual ? TEXT("Manual") : TEXT("Automatic"),
			!(bHasAbs[0] || bHasAbs[1]) ? TEXT("none fitted") : bAbs ? TEXT("on") : TEXT("off")), FColor(255, 220, 120), 1.2f);
	// beenster (Nov 12): a clean screen by default. The lines that stay up (the route, the heat, the
	// realism and weather, the help) only with F2; notes that come and go still show.
	if (!bHudLines) {}
	else if (bYard)   // [yard]
		Add(FString::Printf(TEXT("The yard   stopped, %s: get off and park it here   %s: back to its bay and walk in"),
			*FTorqueKeys::Text(ETorqueKey::Use), *FTorqueKeys::Text(ETorqueKey::BackToShed)), FColor(200, 220, 255), 1.1f);
	else
		Add(FString::Printf(TEXT("%s  %s of %s   %d:%05.2f%s"), *RouteName, *Set.DistanceText(FMath::Min(DoneM, RouteFinishM)), *Set.DistanceText(RouteFinishM), Mins, Secs,
			bFinished ? TEXT("   FINISHED - Home, or Restart in the Options menu, to ride again") : TEXT("")), FColor(200, 220, 255), 1.1f);
	if (!bHudLines && bFinished && !bYard) Add(TEXT("FINISHED - Home, or Restart in the Options menu, to ride again"), FColor(200, 220, 255), 1.1f);
	if (Sim->IsCrashed) Add(bYard ? TEXT("Off! Back on in a moment...") : TEXT("Off! Back on the road in a moment..."), FColor::Orange, 1.4f);
	// [foot] Getting off and on.
	if (FootNoteFor > 0.0) Add(FootNote, FColor(255, 220, 120), 1.2f);
	if (CamNoteFor > 0.0 && !bOnFoot) Add(CamNote, FColor(200, 220, 255), 1.1f);
	if (bOnFoot)
	{
		const double Away = FVector::Dist2D(FootPos, GetActorLocation());
		const FString Use = FTorqueKeys::Text(ETorqueKey::Use);
		if (bPushing)
			Add(FString::Printf(TEXT("Wheeling the bike: %s to walk, %s to let go (it goes on its stand), engine %s"), *FTorqueKeys::Text(ETorqueKey::WalkForward),
				*Use, Sim->EngineOn ? TEXT("ticking over") : TEXT("off")), FColor(200, 230, 200), 1.0f);
		else
			Add(FString::Printf(TEXT("On foot: %s or the left stick to walk (Shift / L3 hurries), %s %s   (the bike's on its %s, engine %s)"), *FTorqueKeys::Text(ETorqueKey::WalkForward),
				*Use, Away <= 250.0 ? *FString::Printf(TEXT("to get back on, hold %s to wheel it"), *Use) : TEXT("by the bike to get back on"),
				ParkStand == 1 ? TEXT("side stand") : ParkStand == 2 ? TEXT("centre stand") : TEXT("wheels"), Sim->EngineOn ? TEXT("ticking over") : TEXT("off")),
				FColor(200, 230, 200), 1.0f);
	}
	else if (bHudLines && !Sim->IsCrashed && FMath::Abs(Sim->Speed) < 0.5)
		Add(FString::Printf(TEXT("%s: get off"), *FTorqueKeys::Text(ETorqueKey::Use)), FColor(200, 230, 200), 0.9f);
	if (bHudLines)
	{
		// The heat (BikeHeat): cold tyres grip less, an overheating engine loses power.
		const FString Tyres = UTF8_TO_TCHAR(Sim->Heat.TyreState()), Engine = UTF8_TO_TCHAR(Sim->Heat.EngineState());
		if (Tyres != TEXT("warm") || Engine != TEXT("warm"))
		{
			FString T;
			if (Tyres != TEXT("warm")) T = FString::Printf(TEXT("Tyres %s (%.0f / %.0f C)"), *Tyres, Sim->Heat.TyreTemp(0), Sim->Heat.TyreTemp(1));
			if (Engine != TEXT("warm")) T += FString::Printf(TEXT("%sEngine %s (%.0f C)"), T.IsEmpty() ? TEXT("") : TEXT("   "), *Engine, Sim->Heat.Engine);
			Add(T, Tyres == TEXT("hot") || Engine == TEXT("hot") ? FColor(255, 150, 90) : FColor(150, 200, 255), 0.95f);
		}
	}
	if (bPerfShow && !PerfText.IsEmpty())
		Add(PerfText + (bPerfLog ? TEXT("   [logging to CSV]") : TEXT("")), FColor(140, 255, 160), 1.0f);
	const bool bSony = Pad && Pad->Get().bConnected;
	static const TCHAR* WeatherNames[3] = {TEXT("sunny"), TEXT("overcast"), TEXT("wet roads")};
	const ATorqueGameState* WeatherGs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	const FString WeatherNow = WeatherGs ? WeatherGs->WeatherText(FTorqueSettings::Get().Units == 1) : FString(WeatherNames[FMath::Clamp(Weather, 0, 2)]);
	if (bHudLines)
		Add(FString::Printf(TEXT("Riding realism %.1f (0 full sim, 1 easy)   %s   weather: %s   (%s hides these lines, %s telemetry)"), Assist,
			bSony ? TEXT("PlayStation pad connected") : TEXT("no PlayStation pad"), *WeatherNow, *FTorqueKeys::Text(ETorqueKey::RideHud),
			*FTorqueKeys::Text(ETorqueKey::Telemetry)), FColor(170, 200, 170), 0.9f);
	if (HostOnlyNoteFor > 0.0) Add(TEXT("The host sets the weather"), FColor(255, 220, 120), 1.1f);
	if (NetBike && bHudLines)
	{
		const FString NetLine = NetBike->StatusLine();
		if (!NetLine.IsEmpty()) Add(NetLine, FColor(170, 210, 255), 0.9f);
	}
	if (bShowHelp && bHudLines)
	{
		const FColor Help(200, 200, 200);
		Add(TEXT("Pad:  R2 throttle   L2 front brake   Circle / B rear brake   left stick lean (push up/down: weight)   right stick look"), Help, 0.62f);
		Add(TEXT("      Cross / A gear up   Square / X gear down   hold L1 / LB clutch (rev it in gear)   hold R1 / RB look behind   L3 horn"), Help, 0.62f);
		Add(TEXT("      Triangle / Y get off (stopped; held while moving: back on road)   D-pad left / right indicators, up lights, down ABS"), Help, 0.62f);
		Add(TEXT("      R3 nod, hold R3 wave   L1 + R1 held: start   Create / View camera (held + right stick / D-pad: set the third-person camera)   Options menu"), Help, 0.62f);
		// The keys as they're bound now (Settings > Controller > Key bindings).
		auto K = [](ETorqueKey A) { return FTorqueKeys::Text(A); };
		Add(FString::Printf(TEXT("Keys: %s throttle   %s brake   %s rear brake   %s / %s lean   %s / %s weight   %s / %s gear up/down   %s auto/manual"),
			*K(ETorqueKey::Throttle), *K(ETorqueKey::FrontBrake), *K(ETorqueKey::RearBrake), *K(ETorqueKey::LeanLeft), *K(ETorqueKey::LeanRight),
			*K(ETorqueKey::WeightBack), *K(ETorqueKey::WeightForward), *K(ETorqueKey::ShiftUp), *K(ETorqueKey::ShiftDown), *K(ETorqueKey::AutoShift)), Help, 0.62f);
		Add(FString::Printf(TEXT("      %s ABS   %s camera   %s look back   %s back on road   %s restart   %s / %s realism   %s weather   %s horn   %s telemetry"),
			*K(ETorqueKey::Abs), *K(ETorqueKey::Camera), *K(ETorqueKey::LookBack), *K(ETorqueKey::BackOnRoad), *K(ETorqueKey::Restart),
			*K(ETorqueKey::RealismDown), *K(ETorqueKey::RealismUp), *K(ETorqueKey::Weather), *K(ETorqueKey::Horn), *K(ETorqueKey::Telemetry)), Help, 0.62f);
		Add(FString::Printf(TEXT("      %s lights   %s main beam   %s fog lights   %s / %s indicate left / right   %s hazards   %s clutch   %s nod   %s wave"),
			*K(ETorqueKey::Headlight), *K(ETorqueKey::MainBeam), *K(ETorqueKey::FogLights), *K(ETorqueKey::IndicateLeft), *K(ETorqueKey::IndicateRight),
			*K(ETorqueKey::Hazards), *K(ETorqueKey::Clutch), *K(ETorqueKey::Nod), *K(ETorqueKey::Wave)), Help, 0.62f);
		Add(FString::Printf(TEXT("      %s performance   %s log it   %s help   %s info panel (%s: click a place)   %s shed   Esc menu"),
			*K(ETorqueKey::Perf), *K(ETorqueKey::PerfLog), *K(ETorqueKey::Help), *K(ETorqueKey::InfoPanel), *K(ETorqueKey::PickPlace), *K(ETorqueKey::BackToShed)), Help, 0.62f);
		Add(FString::Printf(TEXT("      Third person: %s / %s closer / further   %s / %s higher / lower   %s / %s tilt up / down"),
			*K(ETorqueKey::CamCloser), *K(ETorqueKey::CamFarther), *K(ETorqueKey::CamHigher), *K(ETorqueKey::CamLower),
			*K(ETorqueKey::CamTiltUp), *K(ETorqueKey::CamTiltDown)), Help, 0.62f);
	}
}


// Weather: ATorqueGameState lights the world for it (the host's weather, in co-op). What it does
// to the ride follows the weather as it is now, not just its kind: the grip from how wet the road
// is (TorqueWeather::WetGrip), the tyres and engine from the real air (Live) and the road's
// warmth, and the wind with its gusts pushing on the bike (BikeSim::Wind).
void ATorqueBikePawn::ApplyWeather()
{
	const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	if (Sim) Sim->Build.WetGrip = GS ? TorqueWeather::WetGrip(GS->GetWeatherState()) : Weather == 2 ? 0.68 : 1.0;
	UE_LOG(LogTemp, Log, TEXT("Torque: bike in weather %d (grip %.2f)"), Weather, Sim ? Sim->Build.WetGrip : 1.0);
}

void ATorqueBikePawn::WeatherHeat(bool bSoak)
{
	if (!Sim) return;
	const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	const double Hour = GS && GS->GetHour() >= 0.0f ? GS->GetHour() : 13.0;
	const TorqueWeather::State St = GS ? GS->GetWeatherState() : TorqueWeather::Preset(FMath::Clamp(Weather, 0, 2));
	const ETorqueWeatherSource Src = GS ? GS->GetWeatherSource() : ETorqueWeatherSource::Fixed;
	const bool bReal = Src == ETorqueWeatherSource::Live || Src == ETorqueWeatherSource::LiveLastKnown;
	const double Cover = FMath::Clamp(St.Cloud, 0.0, 1.0);
	const double Wet = FMath::Clamp(St.RoadWet, 0.0, 1.0) * 0.8;   // (0.8 the soaked road the heat was tuned on)
	Sim->Build.WetGrip = TorqueWeather::WetGrip(St);
	// The air: the real temperature when live; otherwise the made day's, warmest mid afternoon.
	const double Swing = 7.0 * (1.0 - 0.6 * Cover);
	const double Air = bReal ? St.Air : 12.0 + Swing * FMath::Cos((Hour - 15.0) / 24.0 * UE_DOUBLE_TWO_PI) - 3.0 * Wet;
	const double Sun = FMath::Abs(Hour - 13.0) < 7.5 ? FMath::Max(FMath::Cos((Hour - 13.0) / 24.0 * UE_DOUBLE_TWO_PI * 1.6), 0.0) : 0.0;
	Sim->Heat.Air = Air;
	Sim->Heat.Road = Air + 10.0 * Sun * (1.0 - Cover) * (1.0 - Wet);
	Sim->Heat.Wetness = Wet;
	// The wind at the rider, gusts and all, less where walls or a roof shelter it. Test runs (no
	// input) ride in still air unless they ask for weather, so the physics checks match SimTest.
	static const bool bStill = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) && !FCString::Strifind(FCommandLine::Get(), TEXT("TorqueWeather="));
	if (bStill || !GS) Sim->Wind = TorqueSim::V3();
	else
	{
		double From = St.WindFrom;
		const double Shelter = 1.0 - FMath::Clamp((double)GS->WeatherOpen(), 0.0, 1.0);
		const double Speed = TorqueWeather::WindAtRider(St, GetWorld()->GetTimeSeconds(), Shelter, From);
		// Levels have x east and y south; the sim's x is the level's x and its z the level's y.
		const double To = FMath::DegreesToRadians(From + 180.0);
		Sim->Wind = TorqueSim::V3(FMath::Sin(To), 0.0, -FMath::Cos(To)) * Speed;
	}
	// The shed is out of the wind and a little warmer than outside.
	if (bSoak) Sim->Heat.Soak(Air + 3.0);
}
