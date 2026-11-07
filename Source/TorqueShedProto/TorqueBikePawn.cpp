#include "TorqueBikePawn.h"

#include "BikeSim.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

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
enum ERiderMat { RM_Jacket, RM_Jeans, RM_Boots, RM_Gloves, RM_Helmet, RM_Visor, RM_Skin, RM_Count };

bool LoadJson(const FString& Rel, TSharedPtr<FJsonObject>& Out);

double Approach(double From, double To, double RateUp, double RateDown, double Dt)
{
	const double Rate = (std::fabs(To) > std::fabs(From)) ? RateUp : RateDown;
	return MoveToward(From, To, Rate * Dt);
}
} // namespace

ATorqueBikePawn::ATorqueBikePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	ShapeCube = CubeMesh.Object;
	ShapeCylinder = CylinderMesh.Object;
	ShapeSphere = SphereMesh.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

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
	Sim->Setup(BikeSpec::SpeedTwin1200());
	Sim->Ray = [this](const V3& From, const V3& To) {
		RayHit Out;
		UWorld* World = GetWorld();
		if (!World) return Out;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWheel), true, this);
		if (World->LineTraceSingleByChannel(Hit, ToUE(From), ToUE(To), ECC_Visibility, Params))
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
				TEXT("helmet"), TEXT("visor"), TEXT("skin")};
			for (int32 I = 0; I < RM_Count; ++I)
			{
				FString Path;
				if ((*Kit)->TryGetStringField(Names[I], Path)) Mats[I] = LoadObject<UMaterialInterface>(nullptr, *Path);
			}
		}
		BuildRider(Mats);
	}
	Respawn(0);
	Pad = MakeUnique<FSonyPad>();
	StartSound();
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

	const TSharedPtr<FJsonObject>* PartsObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Assets->TryGetObjectField(TEXT("parts"), PartsObj) || !Spec->TryGetArrayField(TEXT("parts"), List)) return;
	int32 Made = 0;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Name = P->GetStringField(TEXT("name"));
		const FString Group = P->GetStringField(TEXT("group"));
		const TSharedPtr<FJsonObject>* Where = nullptr;
		if (!(*PartsObj)->TryGetObjectField(Name, Where)) continue;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(*Where)->GetStringField(TEXT("mesh")));
		if (!Mesh) continue;
		USceneComponent* Parent = Root;
		if (Group == TEXT("steer")) Parent = SteerGroup;
		else if (Group == TEXT("front_lower")) Parent = FrontHub;
		else if (Group == TEXT("front_wheel")) Parent = FrontPivot;
		else if (Group == TEXT("swingarm")) Parent = SwingGroup;
		else if (Group == TEXT("rear_wheel")) Parent = RearPivot;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *(TEXT("Part_") + Name));
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetupAttachment(Parent);
		C->SetRelativeTransform(Fix);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *(*Where)->GetStringField(TEXT("material"))))
			C->SetMaterial(0, M);
		C->RegisterComponent();
		ModelParts.Add(C);
		++Made;
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
}

void ATorqueBikePawn::Respawn(int32 LineIndex)
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
	}
	// Find the ground under the spawn point.
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueSpawn), true, this);
		const FVector Top = ToUE(Pos + V3(0, 50, 0)), Bottom = ToUE(Pos - V3(0, 50, 0));
		if (World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Params)) Pos = ToSim(Hit.ImpactPoint);
	}
	Sim->Place(Pos, Heading);
	Sim->Assist = Assist;
	Throttle = FrontBrake = RearBrake = Lean = Shift = 0.0;
	CrashedFor = 0.0;
	CurPos = PrevPos = ToUE(Sim->Position);
	CurRot = PrevRot = BasisToUE(Sim->Basis);
	Accum = 0.0;
	bCamInit = false;
	LastGear = Sim->Gear;   // no clunk for being put back in first
	SetActorLocationAndRotation(CurPos, CurRot);
}

void ATorqueBikePawn::ReadInput(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
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
	const double PadLean = Stick(Either(Axis(EKeys::Gamepad_LeftX), Sony.LX), 0.08, 1.3);
	// Weight is the left stick up and down, with a wide dead zone so leaning doesn't move it.
	const double PadShift = Stick(Either(Axis(EKeys::Gamepad_LeftY), Sony.LY), 0.4, 1.0);
	// Right stick looks around (eased back to the front when let go).
	const double LookX = Stick(Either(Axis(EKeys::Gamepad_RightX), Sony.RX), 0.12, 1.0);
	const double LookY = Stick(Either(Axis(EKeys::Gamepad_RightY), Sony.RY), 0.12, 1.0);
	const uint32 SonyDown = Sony.Buttons;
	const uint32 SonyPressed = SonyDown & ~PrevPadButtons;
	PrevPadButtons = SonyDown;
	auto SonyHit = [SonyPressed](uint32 B) { return (SonyPressed & B) != 0; };
	const double LookK = 1.0 - FMath::Exp(-8.0 * Dt);
	LookYaw = FMath::Lerp(LookYaw, LookX * 130.0, LookK);
	LookPitch = FMath::Lerp(LookPitch, LookY * 25.0, LookK);

	const double KeyThrottle = FMath::Max(Down(EKeys::W), Down(EKeys::Up));
	const double KeyBrake = FMath::Max(Down(EKeys::S), Down(EKeys::Down));
	const double KeyLean = FMath::Max(Down(EKeys::D), Down(EKeys::Right)) - FMath::Max(Down(EKeys::A), Down(EKeys::Left));
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
	if (PC->WasInputKeyJustPressed(EKeys::Home) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right) ||
		SonyHit(ESonyButton::Options))
	{
		RideTime = 0.0;
		bFinished = false;
		Respawn(0);
	}
	if (PC->WasInputKeyJustPressed(EKeys::C) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) ||
		PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left) || SonyHit(ESonyButton::R1 | ESonyButton::Create))
	{
		CameraMode = (CameraMode + 1) % 3;
		bCamInit = false;
	}
	if (SonyHit(ESonyButton::DDown)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (SonyHit(ESonyButton::DUp)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::H)) bShowHelp = !bShowHelp;
	const bool bPadQuit = (SonyDown & ESonyButton::Create) && (SonyDown & ESonyButton::Options);
	const bool bXboxQuit = Down(EKeys::Gamepad_Special_Left) > 0.5 && Down(EKeys::Gamepad_Special_Right) > 0.5;
	if (PC->WasInputKeyJustPressed(EKeys::Escape) || bPadQuit || bXboxQuit)
	{
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::LeftBracket)) Assist = FMath::Clamp(Assist - 0.1f, 0.0f, 1.0f);
	if (PC->WasInputKeyJustPressed(EKeys::RightBracket)) Assist = FMath::Clamp(Assist + 0.1f, 0.0f, 1.0f);
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

	if (Sound && Sim->Gear != LastGear) Sound->GearChange(Sim->Gear > LastGear);
	LastGear = Sim->Gear;
	GearNoteFor = FMath::Max(GearNoteFor - DeltaSeconds, 0.0);

	UpdateVisuals(Accum / SimDt);
	UpdateRider();
	UpdateProgress();
	UpdateCamera(DeltaSeconds);
	UpdateSound(DeltaSeconds);
	DrawHud();
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

void ATorqueBikePawn::BuildRider(UMaterialInterface* const* Mats)
{
	Rider->SetVisibility(false);   // the old placeholder
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

void ATorqueBikePawn::ShowHead(bool bShow)
{
	if (bShow == bHeadShown || RiderParts.Num() < RP_Count) return;
	bHeadShown = bShow;
	for (int32 I : {RP_Helmet, RP_Visor, RP_Neck}) RiderParts[I]->SetVisibility(bShow);
}

void ATorqueBikePawn::UpdateRider()
{
	if (RiderParts.Num() < RP_Count) return;
	// Pawn local cm: x forward, y right, z up, from the body origin (axle height, mid wheelbase).
	// The Speed Twin's seat is at about z 52, its grips at x 41, its rider's pegs at x -44.
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
		PlaceLimb(RiderParts[RP_UpperArmL + S], Shoulder, Elbow, 12.0);
		PlaceLimb(RiderParts[RP_ForearmL + S], Elbow, Grip, 10.0);
		PlaceBlob(RiderParts[RP_ElbowL + S], Elbow, FVector(12.0));
		PlaceBlob(RiderParts[RP_HandL + S], Grip + FVector(0.0, 0.0, 1.5), FVector(11.0, 10.0, 9.0));

		const FVector HipS = Hip + FVector(0.0, 11.0 * Y, -2.0);
		const FVector Ankle = FMath::Lerp(FVector(-46.0, 23.0 * Y, 24.0), FVector(-30.0, 36.0 * Y, -22.0), Down);
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
	In.OnTarmac = (R.Contact ? R.Surf : F.Surf) == SURF_TARMAC;
	In.Airborne = !F.Contact && !R.Contact;
	In.Inside = CameraMode == 0 ? 0.0 : 1.0;
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

	ShowHead(CameraMode == 0);
	if (CameraMode == 1 || CameraMode == 2)
	{
		// 1, first person: the rider's eyes, inside the helmet, arms and bars below. 2, the chin
		// camera on beenster's helmet: just below the eyes at the front of the chin bar, wide.
		// Either way the head leans with the bike, but less, as yours does.
		const bool bChin = CameraMode == 2;
		const FVector Eye = BikePos + BikeRot.RotateVector(HeadLocal + (bChin ? FVector(15.0, 0.0, -11.0) : FVector(4.0, 0.0, 0.0)));
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
	const double Done = Line.Num() > 1 ? 3.0 * Near / (Line.Num() - 1) : 0.0;
	const int32 Mins = (int32)(RideTime / 60.0);
	const double Secs = RideTime - Mins * 60.0;
	HudLines.Reset();
	auto Add = [this](const FString& T, FColor C, float Size) { HudLines.Add({T, C, Size}); };
	Add(FString::Printf(TEXT("%.0f mph   gear %d"), FMath::Abs(Mph), Sim->Gear), FColor::White, 2.0f);
	Add(FString::Printf(TEXT("%s gears   ABS %s"), bManual ? TEXT("Manual") : TEXT("Automatic"), bAbs ? TEXT("on") : TEXT("off")),
		GearNoteFor > 0.0 ? FColor(255, 220, 120) : FColor(200, 200, 200), GearNoteFor > 0.0 ? 1.2f : 0.95f);
	Add(FString::Printf(TEXT("%.0f rpm   lean %.0f deg"), Sim->Rpm, FMath::Abs(FMath::RadiansToDegrees(Sim->LeanAngle))),
		FColor(220, 220, 220), 1.1f);
	Add(FString::Printf(TEXT("Buttertubs  %.2f of 3.00 km   %d:%05.2f%s"), Done, Mins, Secs,
		bFinished ? TEXT("   FINISHED - Options/Home to ride again") : TEXT("")), FColor(200, 220, 255), 1.1f);
	if (Sim->IsCrashed) Add(TEXT("Off! Back on the road in a moment..."), FColor::Orange, 1.4f);
	const bool bSony = Pad && Pad->Get().bConnected;
	Add(FString::Printf(TEXT("Riding realism %.1f (0 full sim, 1 easy)   %s"), Assist,
		bSony ? TEXT("PlayStation pad connected") : TEXT("no PlayStation pad")), FColor(170, 200, 170), 0.9f);
	if (bShowHelp)
	{
		const FColor Help(200, 200, 200);
		Add(TEXT("Pad:  R2 throttle   L2 front brake   L1 rear brake   left stick lean (push up/down: weight)"), Help, 0.85f);
		Add(TEXT("      Cross gear up   Square gear down   R3 auto/manual   Circle ABS on/off   right stick look"), Help, 0.85f);
		Add(TEXT("      R1 camera   Triangle back on road   Options restart   D-pad realism   Create+Options quit"), Help, 0.85f);
		Add(TEXT("Keys: W throttle   S brake   Space rear brake   A/D lean   Q/E weight   X/Z gear up/down   G auto/manual"), Help, 0.85f);
		Add(TEXT("      B ABS   C camera   R back on road   Home restart   [ ] realism   H hide this help   Esc quit"), Help, 0.85f);
	}
}
