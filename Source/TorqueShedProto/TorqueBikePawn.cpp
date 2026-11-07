#include "TorqueBikePawn.h"

#include "BikeSim.h"
#include "Camera/CameraComponent.h"
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
	Respawn(0);
	Pad = MakeUnique<FSonyPad>();
}

void ATorqueBikePawn::EndPlay(const EEndPlayReason::Type Reason)
{
	Pad.Reset();   // stops its reading thread
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
	const double PadThrottle = FMath::Max(Axis(EKeys::Gamepad_RightTriggerAxis), (double)Sony.R2);
	const double PadBrake = FMath::Max(Axis(EKeys::Gamepad_LeftTriggerAxis), (double)Sony.L2);
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
	RearBrake = FMath::Max(Down(EKeys::SpaceBar), FMath::Max(Down(EKeys::Gamepad_LeftShoulder),
		Down(EKeys::Gamepad_FaceButton_Left)));
	if (SonyDown & (ESonyButton::L1 | ESonyButton::Square)) RearBrake = 1.0;
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
		CameraMode = (CameraMode + 1) % 2;
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

	UpdateVisuals(Accum / SimDt);
	UpdateProgress();
	UpdateCamera(DeltaSeconds);
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

	if (CameraMode == 1)
	{
		// Onboard: just behind the rider's eyes, leaning with the bike but less, as your head does.
		// Like the chin camera on beenster's helmet: just below the eyes, out over the back of the
		// tank, bars and clocks low in view. The head leans less than the bike.
		const FVector Eye = BikePos + BikeRot.RotateVector(FVector(-15.0, 0.0, 108.0));
		const double LeanDeg = FMath::RadiansToDegrees(Sim->LeanAngle) * 0.55;
		FRotator Rot = Flat.Rotation();
		Rot.Pitch = -12.0 + FMath::RadiansToDegrees(Sim->PitchAngle) * 0.7;
		Rot.Roll = LeanDeg;
		Rot.Yaw += LookYaw;
		Rot.Pitch += LookPitch;
		Camera->SetWorldLocationAndRotation(Eye, Rot);
		Camera->SetFieldOfView(100.0f);
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
		Add(TEXT("Pad:  R2 throttle   L2 front brake   L1/Square rear brake   left stick lean (push up/down: weight)"),
			FColor(200, 200, 200), 0.85f);
		Add(TEXT("      right stick look   R1 camera   Triangle back on road   Options restart   D-pad realism   Create+Options quit"),
			FColor(200, 200, 200), 0.85f);
		Add(TEXT("Keys: W throttle   S brake   Space rear brake   A/D lean   Q/E weight   C camera   R back on road"),
			FColor(200, 200, 200), 0.85f);
		Add(TEXT("      Home restart   [ ] realism   H hide this help   Esc quit"), FColor(200, 200, 200), 0.85f);
	}
}
