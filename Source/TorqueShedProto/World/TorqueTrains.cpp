#include "World/TorqueTrains.h"

#include "TorqueBikePawn.h"
#include "World/RideCosts.h"
#include "World/TorqueLevelCrossing.h"
#include "World/TorqueRideWorld.h"
#include "World/WorldRide.h"
#include "World/WorldTrains.h"
#include "Sim/BikeContacts.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Coop/TorqueGameState.h"
#include "Coop/TorqueSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TorqueAudio/TorqueSfx.h"

#include <algorithm>
#include <cmath>

namespace TorqueTrainsImpl
{
namespace TT = TorqueWorld::Trains;

TArray<TWeakObjectPtr<ATorqueTrains>>& Live()
{
	static TArray<TWeakObjectPtr<ATorqueTrains>> Out;   // (weak: nothing held from GC)
	return Out;
}
ATorqueTrains* Of(const UWorld* InWorld)
{
	for (const TWeakObjectPtr<ATorqueTrains>& T : Live())
		if (T.IsValid() && T->GetWorld() == InWorld) return T.Get();
	return nullptr;
}
// The ride's frame (m: x east, y up, z south) to Unreal's (cm: x east, y south, z up), and a way likewise.
FVector Ue(double X, double Y, double Z) { return FVector(X * 100.0, Z * 100.0, Y * 100.0); }
FVector UeWay(double X, double Y, double Z) { return FVector(X, Z, Y); }
// (the instance key: model, livery, backwards, shadowed, plain box)
int32 KeyOf(int32 Model, int32 Livery, bool bBack, bool bShadow, bool bBox, bool bDark = false)
{
	return Model | (Livery << 8) | (bBack ? 1 << 16 : 0) | (bShadow ? 1 << 17 : 0) | (bBox ? 1 << 18 : 0) | (bDark ? 1 << 19 : 0);
}
// A livery's family: the units, the electric units, the intercity sets, the freight locomotives, the heritage stock.
int32 FamilyOf(TT::Model M)
{
	switch (M)
	{
	case TT::Model::DmuCar: return 0;
	case TT::Model::EmuCab: case TT::Model::EmuMid: return 1;
	case TT::Model::IcPower: case TT::Model::IcCoach: return 2;
	case TT::Model::FreightLoco: return 3;
	case TT::Model::SteamTank: case TT::Model::SteamBig: case TT::Model::SteamTender: case TT::Model::CoachOld: case TT::Model::DieselOld: return 4;
	default: return -1;   // (wagons: their own colours)
	}
}
// The plain box's colour for each kind until the models are in.
FLinearColor BoxColour(int32 K)
{
	static const FLinearColor C[] = {FLinearColor(0.35f, 0.02f, 0.30f), FLinearColor(0.02f, 0.18f, 0.45f), FLinearColor(0.75f, 0.75f, 0.78f),
		FLinearColor(0.45f, 0.20f, 0.02f), FLinearColor(0.02f, 0.12f, 0.04f), FLinearColor(0.20f, 0.02f, 0.02f), FLinearColor(0.05f, 0.20f, 0.10f)};
	return C[FMath::Clamp(K, 0, 6)];
}
constexpr double CrossingReachM = 3000.0;
constexpr int32 PuffCap = 160;
const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -1.0e7), FVector(0.001));
} // namespace TorqueTrainsImpl
namespace TTI = TorqueTrainsImpl;

ATorqueTrains::ATorqueTrains()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
}

void ATorqueTrains::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSounds();
	TTI::Live().RemoveAll([this](const TWeakObjectPtr<ATorqueTrains>& T) { return !T.IsValid() || T.Get() == this; });
	Super::EndPlay(Reason);
}

void ATorqueTrains::Keep(ATorqueRideWorld* Ride, const std::shared_ptr<TorqueWorld::RidePlan>& InPlan, const FVector2D& RiderAt)
{
	UWorld* InWorld = Ride ? Ride->GetWorld() : nullptr;
	if (!InWorld || !InPlan || !InPlan->Ok) return;
	ATorqueTrains* T = TTI::Of(InWorld);
	if (!T)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		T = InWorld->SpawnActor<ATorqueTrains>(ATorqueTrains::StaticClass(), FTransform::Identity, P);
		if (!T) return;
		TTI::Live().Add(T);
		UE_LOG(LogTemp, Log, TEXT("Torque trains: stood up for the ride"));
	}
	T->Plan = InPlan;
	// (built again when the plan is new or has grown)
	const size_t Lines = InPlan->Area.Roads.Scenery.Lines.size() * 1000003u + InPlan->Rails.size();
	if ((T->NetPlan != InPlan.get() || T->NetLines != Lines) && !T->bBuilding && InWorld->GetTimeSeconds() - T->LastRebuild > 2.0) T->Rebuild();
	(void)RiderAt;
}

void ATorqueTrains::Rebuild()
{
	bBuilding = true;
	LastRebuild = GetWorld()->GetTimeSeconds();
	const std::shared_ptr<TorqueWorld::RidePlan> From = Plan;
	const size_t Lines = From->Area.Roads.Scenery.Lines.size() * 1000003u + From->Rails.size();
	TWeakObjectPtr<ATorqueTrains> Self(this);
	Async(EAsyncExecution::ThreadPool, [Self, From, Lines]()
	{
		const double T0 = FPlatformTime::Seconds();
		std::shared_ptr<TorqueWorld::Trains::Network> Built = TorqueWorld::Trains::Network::Build(*From);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		AsyncTask(ENamedThreads::GameThread, [Self, From, Lines, Built, Ms]()
		{
			ATorqueTrains* T = Self.Get();
			if (!T) return;
			T->bBuilding = false;
			T->Net = Built;
			T->NetPlan = From.get();
			T->NetLines = Lines;
			T->LaidAt = FVector2D(1e12, 1e12);
			UE_LOG(LogTemp, Log, TEXT("Torque trains: %s (built in %.0f ms)"), UTF8_TO_TCHAR(Built->Summary().c_str()), Ms);
		});
	});
}

TorqueWorld::Trains::Clock ATorqueTrains::ClockNow() const
{
	TorqueWorld::Trains::Clock C;
	const UWorld* InWorld = GetWorld();
	const ATorqueGameState* GS = InWorld ? InWorld->GetGameState<ATorqueGameState>() : nullptr;
	// (the server's seconds: the same on every machine of a co-op ride)
	C.T = 100000.0 + (GS ? GS->GetServerWorldTimeSeconds() : (InWorld ? InWorld->GetTimeSeconds() : 0.0));
	C.Hour = GS && GS->GetHour() >= 0.0f ? double(GS->GetHour()) : 12.0;
	C.Day = GS ? GS->DayOfYearNow() : 180;
	return C;
}

bool ATorqueTrains::TrainsOn() const
{
	const UWorld* InWorld = GetWorld();
	const ATorqueGameState* GS = InWorld ? InWorld->GetGameState<ATorqueGameState>() : nullptr;
	static IConsoleVariable* Cvar = IConsoleManager::Get().RegisterConsoleVariable(TEXT("torque.RideTrains"), 1,
		TEXT("[trains] 1: trains run on Ride UK's railways (Settings > Gameplay > Trains, the host's); 0: none (crossings open)"), ECVF_Default);
	if (Cvar && Cvar->GetInt() == 0) return false;
	return GS ? GS->bTrainsOn : FTorqueSettings::Get().bTrains;
}

FVector ATorqueTrains::Eye() const
{
	if (const UWorld* InWorld = GetWorld())
		if (const APlayerController* PC = InWorld->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager) return PC->PlayerCameraManager->GetCameraLocation();
			if (const APawn* Pawn = PC->GetPawn()) return Pawn->GetActorLocation();
		}
	return FVector::ZeroVector;
}

// --- Models and materials ------------------------------------------------------------------------------------------

UMaterialInterface* ATorqueTrains::Mat(const FString& Key)
{
	const FName N(*Key);
	if (const TObjectPtr<UMaterialInterface>* Got = Mats.Find(N)) return Got->Get();
	const FString Path = FString::Printf(TEXT("/Game/Shed/Props/Materials/MI_Prop_%s.MI_Prop_%s"), *Key, *Key);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Mats.Add(N, M);
	return M;
}

void ATorqueTrains::LoadModels()
{
	if (bModelsLoaded) return;
	bModelsLoaded = true;
	namespace TT = TorqueWorld::Trains;
	Models.SetNum(int32(TT::Model::Count));
	int32 Got = 0;
	for (int32 M = 0; M < int32(TT::Model::Count); ++M)
	{
		const FString Id = UTF8_TO_TCHAR(TT::SpecOf(TT::Model(M)).Id);
		const FString Path = FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), *Id, *Id, *Id);
		Models[M] = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		Got += Models[M] ? 1 : 0;
	}
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BoxMats.SetNum(int32(TT::Kind::Count));
	for (int32 K = 0; K < int32(TT::Kind::Count); ++K)
		if (Basic)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Basic, this);
			D->SetVectorParameterValue(TEXT("Color"), TTI::BoxColour(K));
			BoxMats[K] = D;
		}
	UE_LOG(LogTemp, Log, TEXT("Torque trains: %d of %d train models imported (plain boxes for the rest: Tools/props/make_trains.py, then Tools/ue_import_props.py)"),
		Got, int32(TT::Model::Count));
}

UInstancedStaticMeshComponent* ATorqueTrains::MakeIsm(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* M, bool bShadow)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
	C->SetStaticMesh(Mesh);
	if (M) C->SetMaterial(0, M);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bShadow);
	C->SetUsingAbsoluteLocation(true);
	C->SetUsingAbsoluteRotation(true);
	C->SetUsingAbsoluteScale(true);
	C->SetupAttachment(GetRootComponent());
	C->RegisterComponent();
	C->SetWorldTransform(FTransform::Identity);
	return C;
}

UInstancedStaticMeshComponent* ATorqueTrains::PartFor(int32 Key)
{
	if (TObjectPtr<UInstancedStaticMeshComponent>* Got = Parts.Find(Key)) return Got->Get();
	namespace TT = TorqueWorld::Trains;
	const int32 M = Key & 0xff, Livery = (Key >> 8) & 0xff;
	const bool bBack = (Key & (1 << 16)) != 0, bShadow = (Key & (1 << 17)) != 0, bBox = (Key & (1 << 18)) != 0;
	const bool bDark = (Key & (1 << 19)) != 0;   // (a cab mid-train, two units coupled: its lamps out)
	UStaticMesh* Mesh = bBox ? Cube.Get() : Models[M].Get();
	if (!Mesh) return nullptr;
	UInstancedStaticMeshComponent* C = MakeIsm(*FString::Printf(TEXT("Train_%d"), Key), Mesh, nullptr, bShadow);
	C->SetCullDistances(0, int32(DrawM * 100.0));
	if (bBox)
	{
		// (Livery carries the kind for a box)
		if (BoxMats.IsValidIndex(Livery) && BoxMats[Livery]) C->SetMaterial(0, BoxMats[Livery]);
	}
	else
	{
		// Its livery (the family's scheme over the model's own colours), its lamps (white at the front, red at the back).
		const int32 Fam = TTI::FamilyOf(TT::Model(M));
		if (Fam >= 0)
		{
			if (UMaterialInterface* A = Mat(FString::Printf(TEXT("train_liv_%d_%d_a"), Fam, Livery))) C->SetMaterialByName(TEXT("train_livery_a"), A);
			if (UMaterialInterface* B = Mat(FString::Printf(TEXT("train_liv_%d_%d_b"), Fam, Livery))) C->SetMaterialByName(TEXT("train_livery_b"), B);
		}
		if (UMaterialInterface* Off = Mat(TEXT("train_lamp_off")))
		{
			if (bBack || bDark) C->SetMaterialByName(TEXT("train_lamp_head"), Off);
			if (!bBack || bDark) C->SetMaterialByName(TEXT("train_lamp_tail"), Off);
		}
		if (bNight)
			if (UMaterialInterface* Lit = Mat(TEXT("train_window_lit"))) C->SetMaterialByName(TEXT("train_window"), Lit);
	}
	Parts.Add(Key, C);
	return C;
}

void ATorqueTrains::HideAll()
{
	for (auto& P : Parts)
		if (P.Value && P.Value->GetInstanceCount() > 0) P.Value->ClearInstances();
	if (Headlight) Headlight->SetVisibility(false);
	Shown.clear();
}

void ATorqueTrains::Draw(const std::vector<TorqueWorld::Trains::TrainNow>& Trains)
{
	namespace TT = TorqueWorld::Trains;
	LoadModels();
	const FVector At = Eye();
	TMap<int32, TArray<FTransform>> Want;
	for (const TT::TrainNow& T : Trains)
		for (const TT::CarNow& Car : T.Cars)
		{
			const TT::ModelSpec& Spec = TT::SpecOf(Car.Is);
			const bool bBox = !Models.IsValidIndex(int32(Car.Is)) || !Models[int32(Car.Is)];
			const FVector Mid = TTI::Ue(Car.X, Car.Y, Car.Z);
			const bool bShadow = FVector::Dist(Mid, At) < ShadowM * 100.0;
			FVector Fwd = TTI::UeWay(Car.Fx, Car.Fy, Car.Fz).GetSafeNormal();
			if (Fwd.IsNearlyZero()) Fwd = FVector::YAxisVector;
			// (its lean: the cant of the curve, down to the train's left; the left of a way in Unreal's frame is (y, -x))
			const FVector Left = FVector(Fwd.Y, -Fwd.X, 0.0).GetSafeNormal();
			const FVector Up = (FVector::UpVector * FMath::Cos(Car.Roll) + Left * FMath::Sin(Car.Roll)).GetSafeNormal();
			const FVector Facing = Car.Reversed ? -Fwd : Fwd;
			const FQuat Rot = FRotationMatrix::MakeFromYZ(Facing, Up).ToQuat();
			if (bBox)
			{
				// (a plain box the vehicle's size, on its wheels: the engine's cube is 1 m, about its middle)
				const double H = Spec.Height - 0.6;
				const FVector Lift = Up * ((0.6 + H * 0.5) * 100.0);
				Want.FindOrAdd(TTI::KeyOf(int32(Car.Is), int32(T.Train), false, bShadow, true))
					.Add(FTransform(Rot, Mid + Lift, FVector(Spec.Width, Spec.Length, H)));
			}
			else
				Want.FindOrAdd(TTI::KeyOf(int32(Car.Is), int32(T.Livery % 3), Car.Reversed, bShadow, false, !Car.Lead && !Car.Tail)).Add(FTransform(Rot, Mid, FVector::OneVector));
		}
	// Each part's instances put in place (moved where the count's the same, laid again where it isn't).
	for (auto& W : Want)
	{
		UInstancedStaticMeshComponent* C = PartFor(W.Key);
		if (!C) continue;
		if (C->GetInstanceCount() == W.Value.Num()) C->BatchUpdateInstancesTransforms(0, W.Value, true, true, true);
		else
		{
			C->ClearInstances();
			C->AddInstances(W.Value, false, true);
		}
	}
	for (auto& P : Parts)
		if (P.Value && !Want.Contains(P.Key) && P.Value->GetInstanceCount() > 0) P.Value->ClearInstances();
}

// --- Every frame ------------------------------------------------------------------------------------------------------

void ATorqueTrains::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RideCosts::FScope Cost(RideCosts::Trains);
	const double T0 = FPlatformTime::Seconds();
	const bool bOn = TrainsOn() && Net;
	const TorqueWorld::Trains::Clock Now = ClockNow();
	// (after dark: the windows lit)
	{
		const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		const bool bDark = GS && GS->GetDayLight() < 0.35f;
		if (bDark != bNight)
		{
			bNight = bDark;
			if (UMaterialInterface* Glass = Mat(bNight ? TEXT("train_window_lit") : TEXT("train_window")))
				for (auto& P : Parts)
					if (P.Value && (P.Key & (1 << 18)) == 0) P.Value->SetMaterialByName(TEXT("train_window"), Glass);
		}
	}
	SinceCrossings += DeltaSeconds;
	if (SinceCrossings >= 0.1)
	{
		SinceCrossings = 0.0;
		TellCrossings(Now, bOn);
	}
	if (!bOn)
	{
		if (!Shown.empty() || Puffs.Num() > 0) { HideAll(); Puffs.Reset(); StopSounds(); }
		if (Masts && Masts->GetInstanceCount() > 0) { Masts->ClearInstances(); Wires->ClearInstances(); SignalPosts->ClearInstances(); LampRed->ClearInstances(); LampGreen->ClearInstances(); Signals.Reset(); LaidAt = FVector2D(1e12, 1e12); }
		return;
	}
	const FVector At = Eye();
	Net->Near(Now, At.X / 100.0, At.Y / 100.0, DrawM, Shown);
	Draw(Shown);
	// The nearest train's headlight after dark (one light, moved: never one per train).
	{
		const TorqueWorld::Trains::TrainNow* Lit = nullptr;
		for (const TorqueWorld::Trains::TrainNow& T : Shown)
			if (T.Dist < 700.0 && !T.Cars.empty() && T.Cars.front().Lead && !T.Cars.front().InTunnel) { Lit = &T; break; }
		if (bNight && Lit)
		{
			if (!Headlight)
			{
				Headlight = NewObject<USpotLightComponent>(this, TEXT("TrainHeadlight"));
				Headlight->SetupAttachment(GetRootComponent());
				Headlight->SetUsingAbsoluteLocation(true);
				Headlight->SetUsingAbsoluteRotation(true);
				Headlight->SetIntensityUnits(ELightUnits::Candelas);
				Headlight->SetIntensity(9000.0f);
				Headlight->SetAttenuationRadius(25000.0f);
				Headlight->SetOuterConeAngle(16.0f);
				Headlight->SetInnerConeAngle(6.0f);
				Headlight->SetCastShadows(false);
				Headlight->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f));
				Headlight->RegisterComponent();
			}
			const TorqueWorld::Trains::CarNow& C = Lit->Cars.front();
			const FVector Fwd = TTI::UeWay(C.Fx, C.Fy, C.Fz).GetSafeNormal();
			const double Half = TorqueWorld::Trains::SpecOf(C.Is).Length * 0.5;
			Headlight->SetWorldLocationAndRotation(TTI::Ue(C.X, C.Y, C.Z) + Fwd * (Half * 100.0 + 20.0) + FVector(0, 0, 110.0), (Fwd - FVector(0, 0, 0.03)).Rotation());
			Headlight->SetVisibility(true);
		}
		else if (Headlight) Headlight->SetVisibility(false);
	}
	TickSounds(DeltaSeconds, Now);
	TickSmoke(DeltaSeconds);
	if (FVector2D::Distance(LaidAt, FVector2D(At.X / 100.0, At.Y / 100.0)) > 200.0) LayLineside(FVector2D(At.X / 100.0, At.Y / 100.0));
	SinceSignals += DeltaSeconds;
	if (SinceSignals >= 0.5)
	{
		SinceSignals = 0.0;
		TickSignals(Now);
	}
	// (the log every 10 s: how many, and what it costs)
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	CostMs += Ms;
	WorstMs = FMath::Max(WorstMs, Ms);
	++CostFrames;
	LogClock += DeltaSeconds;
	if (LogClock >= 10.0)
	{
		int32 Vehicles = 0;
		for (const TorqueWorld::Trains::TrainNow& T : Shown) Vehicles += int32(T.Cars.size());
		UE_LOG(LogTemp, Log, TEXT("Torque trains: %d in sight (%d vehicles, nearest %.0f m), %.3f ms a frame (worst %.3f), %d puffs, %d signals"), int32(Shown.size()), Vehicles,
			Shown.empty() ? -1.0 : Shown.front().Dist, CostMs / FMath::Max(1, CostFrames), WorstMs, Puffs.Num(), Signals.Num());
		LogClock = 0.0;
		CostMs = WorstMs = 0.0;
		CostFrames = 0;
	}
}

// --- [stage 4] Level crossings ----------------------------------------------------------------------------------------

void ATorqueTrains::TellCrossings(const TorqueWorld::Trains::Clock& Now, bool bOn)
{
	if (!Plan || (bOn && NetPlan != Plan.get())) return;
	const FVector At = Eye();
	const double X = At.X / 100.0, Z = At.Y / 100.0;
	for (const TorqueWorld::RidePlan::LevelCrossing& C : Plan->LevelCrossings)
	{
		if (std::hypot(C.Mid.X - X, C.Mid.Z - Z) > TTI::CrossingReachM) continue;
		// (trains off: every crossing open, and told so once, which also ends the crossings' own demo cycle)
		const bool bShut = bOn && Net && Net->Closed(Now, C.Line, C.First, C.Last, ATorqueLevelCrossing::LeadSeconds(int32(C.Type)));
		if (bShut || bOn || !bCrossingsTold) ATorqueLevelCrossing::Warn(GetWorld(), C.Key, bShut);
	}
	bCrossingsTold = true;
}

// --- The vehicles as a bike's contacts -------------------------------------------------------------------------------

void ATorqueTrains::Capsules(const UWorld* InWorld, double X, double Y, double Z, std::vector<TorqueSim::ContactCapsule>& Out)
{
	const ATorqueTrains* T = TTI::Of(InWorld);
	if (!T || !T->Net || !T->TrainsOn()) return;
	for (const TorqueWorld::Trains::TrainNow& Tr : T->Shown)
	{
		if (Tr.Dist > 400.0) break;   // (nearest first; the camera's never far from the bike)
		for (const TorqueWorld::Trains::CarNow& C : Tr.Cars)
		{
			if (std::hypot(C.X - X, C.Z - Z) > 40.0 || std::fabs(C.Y - Y) > 8.0) continue;
			const TorqueWorld::Trains::ModelSpec& S = TorqueWorld::Trains::SpecOf(C.Is);
			const double Half = S.Length * 0.5 - S.Width * 0.5;
			// (its body: a capsule along it at mid height, as wide as it; moving at the train's speed)
			const double Cy = C.Y + 1.55;
			TorqueSim::ContactCapsule Cap;
			Cap.A = TorqueSim::V3(C.X + C.Fx * Half, Cy + C.Fy * Half, C.Z + C.Fz * Half);
			Cap.B = TorqueSim::V3(C.X - C.Fx * Half, Cy - C.Fy * Half, C.Z - C.Fz * Half);
			Cap.Radius = S.Width * 0.5;
			Cap.Velocity = TorqueSim::V3(C.Fx * Tr.Speed, C.Fy * Tr.Speed, C.Fz * Tr.Speed);
			Cap.Rider = false;
			Out.push_back(Cap);
		}
	}
}

bool ATorqueTrains::AnyNear(const UWorld* InWorld, double X, double Z, double Reach)
{
	const ATorqueTrains* T = TTI::Of(InWorld);
	if (!T || !T->Net || !T->TrainsOn()) return false;
	for (const TorqueWorld::Trains::TrainNow& Tr : T->Shown)
		for (const TorqueWorld::Trains::CarNow& C : Tr.Cars)
			if (std::hypot(C.X - X, C.Z - Z) < Reach) return true;
	return false;
}

FString ATorqueTrains::Test(const UWorld* InWorld, const FString& What, ATorqueBikePawn* Bike)
{
	namespace TT = TorqueWorld::Trains;
	ATorqueTrains* T = TTI::Of(InWorld);
	if (!T) return TEXT("no trains (not on a Ride UK ride)");
	if (!T->Net) return TEXT("trains: network not built yet");
	if (What == TEXT("count")) return FString::Printf(TEXT("%d trains in sight"), int32(T->Shown.size()));
	// draw: what's actually laid as instances (to tell a train not drawn from one not looked at)
	if (What == TEXT("draw"))
	{
		int32 NParts = 0, Instances = 0;
		FString First;
		for (auto& P : T->Parts)
			if (P.Value && P.Value->GetInstanceCount() > 0)
			{
				++NParts;
				Instances += P.Value->GetInstanceCount();
				if (First.IsEmpty())
				{
					FTransform X;
					P.Value->GetInstanceTransform(0, X, true);
					First = FString::Printf(TEXT("%s at %s (visible %d, mesh %s, bounds %.0f m)"), *P.Value->GetName(), *X.GetLocation().ToCompactString(), P.Value->IsVisible() ? 1 : 0,
						P.Value->GetStaticMesh() ? *P.Value->GetStaticMesh()->GetName() : TEXT("none"), P.Value->Bounds.SphereRadius / 100.0);
				}
			}
		return FString::Printf(TEXT("trains drawn: %d parts, %d vehicles; first %s; camera at %s"), NParts, Instances, *First, *T->Eye().ToCompactString());
	}
	// goto: our bike put 15 m beside the line ahead of the nearest moving train (within 2.5 km), facing the track,
	// about 25 s before the train gets there.
	if (What == TEXT("goto"))
	{
		if (!Bike) return TEXT("trains:goto needs our bike");
		const FVector Me = Bike->GetActorLocation();
		std::vector<TT::TrainNow> Near;
		T->Net->Near(T->ClockNow(), Me.X / 100.0, Me.Y / 100.0, 2500.0, Near);
		for (const TT::TrainNow& N : Near)
		{
			if (N.Speed < 5.0 || N.Standing) continue;
			const TT::Route& R = T->Net->Routes[size_t(N.Route)];
			const TT::Service& Sv = T->Net->Services[size_t(N.Route)][size_t(N.Service)];
			const double Ahead = FMath::Max(80.0, N.Speed * 25.0);
			const double FrontS = Sv.Dir > 0 ? N.Front : R.Length() - N.Front;
			const double S = FrontS + Sv.Dir * Ahead;
			if (S < 50.0 || S > R.Length() - 50.0) continue;
			double X = 0.0, Y = 0.0, Z = 0.0, Dx = 0.0, Dz = 1.0;
			T->Net->PlaceAt(R, S, X, Y, Z, Dx, Dz);
			const double Wx = Dx * Sv.Dir, Wz = Dz * Sv.Dir;   // (the way the train comes)
			const double Lx = Wz, Lz = -Wx;                    // (its left)
			const double Px = X + Lx * 15.0, Pz = Z + Lz * 15.0;
			const FString Placed = Bike->TestPlaceAt(Px * 100.0, Pz * 100.0, FMath::RadiansToDegrees(std::atan2(-Lz, -Lx)));
			return FString::Printf(TEXT("trains:goto: by %s (%s line), a %s due in %.0f s at %.0f mph, %d vehicles; %s"), UTF8_TO_TCHAR(R.Name.c_str()), UTF8_TO_TCHAR(TT::ClassName(R.Class)),
				UTF8_TO_TCHAR(TT::KindName(N.Train)), Ahead / FMath::Max(1.0, N.Speed), N.Speed * 2.23694, int32(N.Cars.size()), *Placed);
		}
		return FString::Printf(TEXT("trains:goto: no moving train within 2.5 km now (%d in reach); try again in a minute"), int32(Near.size()));
	}
	if (T->Shown.empty()) return FString::Printf(TEXT("no train within %.0f m; %s"), DrawM, UTF8_TO_TCHAR(T->Net->Summary().c_str()));
	const TT::TrainNow& N = T->Shown.front();
	const TT::Route& R = T->Net->Routes[size_t(N.Route)];
	return FString::Printf(TEXT("nearest train: %s on %s (%s line), %.0f m away, %.0f mph%s"), UTF8_TO_TCHAR(TT::KindName(N.Train)), UTF8_TO_TCHAR(R.Name.c_str()),
		UTF8_TO_TCHAR(TT::ClassName(R.Class)), N.Dist, N.Speed * 2.23694, N.Standing ? TEXT(" (standing)") : TEXT(""));
}

// crossings:next: our bike put on the road 25 m short of the level crossing (within 4 km) whose barriers come down
// soonest but at least 20 s from now (a train's front due within the next 3 minutes), facing it.
FString ATorqueTrains::NextCrossing(const UWorld* InWorld, ATorqueBikePawn* Bike)
{
	namespace TT = TorqueWorld::Trains;
	ATorqueTrains* T = TTI::Of(InWorld);
	if (!T || !T->Net || !T->Plan) return TEXT("crossings:next: no trains here yet");
	if (!Bike) return TEXT("crossings:next needs our bike");
	const FVector Me = Bike->GetActorLocation();
	const TT::Clock Now = T->ClockNow();
	const TorqueWorld::RidePlan::LevelCrossing* Best = nullptr;
	double BestIn = 1e9;
	int32 Tried = 0;
	for (const TorqueWorld::RidePlan::LevelCrossing& C : T->Plan->LevelCrossings)
	{
		if (FVector2D::Distance(FVector2D(C.Mid.X, C.Mid.Z), FVector2D(Me.X / 100.0, Me.Y / 100.0)) > 4000.0) continue;
		++Tried;
		const double Lead = ATorqueLevelCrossing::LeadSeconds(int32(C.Type));
		if (T->Net->Closed(Now, C.Line, C.First, C.Last, Lead)) continue;   // (shut already: too late to watch it close)
		for (double Dt = 20.0; Dt <= 180.0 && Dt < BestIn; Dt += 5.0)
		{
			TT::Clock Later = Now;
			Later.T += Dt;
			Later.Hour += Dt / 3600.0;
			if (T->Net->Closed(Later, C.Line, C.First, C.Last, Lead)) { Best = &C; BestIn = Dt; break; }
		}
	}
	if (!Best) return FString::Printf(TEXT("crossings:next: none of %d crossings within 4 km closes in the next 3 minutes; try again soon"), Tried);
	const double Px = Best->Mid.X - Best->Road.X * 25.0, Pz = Best->Mid.Z - Best->Road.Z * 25.0;
	const FString Placed = Bike->TestPlaceAt(Px * 100.0, Pz * 100.0, FMath::RadiansToDegrees(std::atan2(Best->Road.Z, Best->Road.X)));
	return FString::Printf(TEXT("crossings:next: crossing %u (type %d) closes in about %.0f s; %s"), Best->Key, Best->Type, BestIn, *Placed);
}

// --- [stage 5] Sounds -------------------------------------------------------------------------------------------------

void ATorqueTrains::StopSounds()
{
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this))
		for (int32 E = 0; E < 2; ++E)
			for (int32* L : {&EngineLoop[E], &RailLoop[E], &RushLoop[E]})
				if (*L) { Sfx->FreeLoop(*L); *L = 0; }
	EarTrain[0] = EarTrain[1] = 0;
}

void ATorqueTrains::TickSounds(float Dt, const TorqueWorld::Trains::Clock& Now)
{
	namespace TT = TorqueWorld::Trains;
	UTorqueSfx* Sfx = UTorqueSfx::Get(this);
	if (!Sfx) return;
	if (Ears.Num() == 0)
		for (int32 E = 0; E < 2; ++E)
		{
			USceneComponent* C = NewObject<USceneComponent>(this, *FString::Printf(TEXT("TrainEar%d"), E));
			C->SetupAttachment(GetRootComponent());
			C->SetUsingAbsoluteLocation(true);
			C->RegisterComponent();
			Ears.Add(C);
		}
	const FVector At = Eye();
	for (int32 E = 0; E < 2; ++E)
	{
		const TT::TrainNow* T = int32(Shown.size()) > E && Shown[size_t(E)].Dist < 1500.0 ? &Shown[size_t(E)] : nullptr;
		if (!T)
		{
			for (int32* L : {&EngineLoop[E], &RailLoop[E], &RushLoop[E]})
				if (*L) { Sfx->FreeLoop(*L); *L = 0; }
			EarTrain[E] = 0;
			continue;
		}
		// (the ear on the vehicle nearest the camera)
		const TT::CarNow* Near = &T->Cars.front();
		double Best = 1e18;
		for (const TT::CarNow& C : T->Cars)
		{
			const double D = FVector::DistSquared(TTI::Ue(C.X, C.Y, C.Z), At);
			if (D < Best) { Best = D; Near = &C; }
		}
		Ears[E]->SetWorldLocation(TTI::Ue(Near->X, Near->Y + 1.0, Near->Z));
		if (!EngineLoop[E]) EngineLoop[E] = Sfx->MakeLoop(Ears[E], FVector::ZeroVector, 0.0f, 1500.0f);
		if (!RailLoop[E]) RailLoop[E] = Sfx->MakeLoop(Ears[E], FVector::ZeroVector, 0.0f, 1500.0f);
		if (!RushLoop[E]) RushLoop[E] = Sfx->MakeLoop(Ears[E], FVector::ZeroVector, 0.0f, 1500.0f);
		const bool bSteam = T->Train == TT::Kind::SteamTank || T->Train == TT::Kind::SteamBig;
		const TCHAR* Engine = bSteam ? TEXT("train_steam") : T->Train == TT::Kind::Emu ? TEXT("train_electric")
			: T->Train == TT::Kind::Intercity ? TEXT("train_intercity") : (T->Train == TT::Kind::Freight || T->Train == TT::Kind::OldDiesel) ? TEXT("train_loco") : TEXT("train_dmu");
		if (Sfx->LoopSound(EngineLoop[E]) != Engine) Sfx->SetLoop(EngineLoop[E], Engine);
		const double V = T->Speed;
		// (the engine: idling standing, working harder as it goes; steam's chuffs quicken with the wheels)
		Sfx->SetLoopPitch(EngineLoop[E], float(bSteam ? FMath::Clamp(0.35 + V / 9.0, 0.35, 2.2) : FMath::Clamp(0.8 + V / 60.0, 0.8, 1.6)));
		Sfx->SetLoopDb(EngineLoop[E], float(T->Standing ? -12.0 : -4.0));
		// (the wheels over the rail joints: their beat by the speed; a bridge rumbles)
		const bool bMoving = V > 1.0;
		const FString Rails = bMoving ? (Near->InTunnel ? TEXT("") : TEXT("train_rails")) : TEXT("");
		if (Sfx->LoopSound(RailLoop[E]) != Rails) Sfx->SetLoop(RailLoop[E], Rails);
		if (bMoving)
		{
			Sfx->SetLoopPitch(RailLoop[E], float(FMath::Clamp(V / 25.0, 0.3, 2.4)));
			Sfx->SetLoopDb(RailLoop[E], float(FMath::Clamp(-20.0 + V * 0.5, -20.0, 2.0)));
		}
		// (the rush of a fast train going by)
		const FString Rush = V > 25.0 ? TEXT("train_rush") : TEXT("");
		if (Sfx->LoopSound(RushLoop[E]) != Rush) Sfx->SetLoop(RushLoop[E], Rush);
		if (V > 25.0) Sfx->SetLoopDb(RushLoop[E], float(FMath::Clamp((V - 25.0) * 0.4 - 6.0, -6.0, 6.0)));
		EarTrain[E] = T->Id;
	}
	// One-shots for every train in earshot: the horn coming up to a level crossing or a tunnel (once a minute at
	// most), brakes squealing into a stop, a steam engine's whistle as it leaves.
	const double WorldNow = GetWorld()->GetTimeSeconds();
	for (const TT::TrainNow& T : Shown)
	{
		if (T.Dist > 1500.0 || T.Cars.empty()) break;
		const TT::Route& R = Net->Routes[size_t(T.Route)];
		const TT::Service& Sv = Net->Services[size_t(T.Route)][size_t(T.Service)];
		const bool bSteam = T.Train == TT::Kind::SteamTank || T.Train == TT::Kind::SteamBig;
		const TT::CarNow& Lead = T.Cars.front();
		const FVector Front = TTI::Ue(Lead.X, Lead.Y + 2.0, Lead.Z);
		double& LastHorn = Horned[T.Id];
		if (T.Speed > 5.0 && WorldNow - LastHorn > 60.0)
		{
			// (what's ahead of the front within 12 s: a tunnel's mouth or a crossing)
			const double Ahead = T.Speed * 12.0;
			bool bSound = false;
			for (double D = 20.0; D <= Ahead && !bSound; D += 8.0)
			{
				const double S = Sv.Dir > 0 ? T.Front + D : R.Length() - (T.Front + D);
				if (S < 0.0 || S > R.Length()) break;
				const int32 I = Net->SampleAt(T.Route, S);
				if (R.On[size_t(I)] == 3 || (R.On[size_t(I)] == 2 && !Lead.InTunnel)) bSound = true;
			}
			if (bSound)
			{
				Sfx->PlayAt(bSteam ? TEXT("train_whistle") : TEXT("train_horn"), Front, -2.0f);
				LastHorn = WorldNow;
			}
		}
		int32& Was = WasStanding.emplace(T.Id, T.Standing ? 1 : 0).first->second;
		if (T.Braking && T.Speed < 7.0 && T.Speed > 1.0 && Was == 0 && T.Dist < 600.0)
		{
			Sfx->PlayAt(TEXT("train_brakes"), TTI::Ue(T.Cars[T.Cars.size() / 2].X, T.Cars[T.Cars.size() / 2].Y, T.Cars[T.Cars.size() / 2].Z), -6.0f);
			Was = 2;   // (once per stop)
		}
		if (T.Standing) Was = 1;
		else if (Was == 1)
		{
			if (bSteam && T.Dist < 1000.0) Sfx->PlayAt(TEXT("train_whistle"), Front, -4.0f);
			Was = 0;
		}
	}
	// (forgetting trains long gone)
	if (Horned.size() > 256) Horned.clear();
	if (WasStanding.size() > 256) WasStanding.clear();
	(void)Dt;
	(void)Now;
}

// --- [stage 7] Steam and diesel exhaust -------------------------------------------------------------------------------

void ATorqueTrains::TickSmoke(float Dt)
{
	namespace TT = TorqueWorld::Trains;
	if (!Smoke)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_TyreSmoke.M_TyreSmoke"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (!M || !Plane) return;
		Smoke = MakeIsm(TEXT("TrainSmoke"), Plane, M, false);
		Smoke->SetNumCustomDataFloats(6);   // (as the tyre smoke's: colour, alpha, softness, seed)
		Smoke->SetBoundsScale(1000.0f);
		TArray<FTransform> All;
		All.Init(TTI::Hidden, TTI::PuffCap);
		Smoke->AddInstances(All, false, true);
	}
	// Emitted: a steam engine's chuffs from its chimney (four a turn of its 1.7 m wheels, a wisp standing), and a
	// diesel locomotive's haze pulling away.
	for (const TT::TrainNow& T : Shown)
	{
		if (T.Dist > 800.0 || T.Cars.empty()) break;
		const bool bSteam = T.Train == TT::Kind::SteamTank || T.Train == TT::Kind::SteamBig;
		const bool bDiesel = T.Train == TT::Kind::OldDiesel || T.Train == TT::Kind::Freight;
		if (!bSteam && !bDiesel) continue;
		const TT::CarNow& L = T.Cars.front();
		if (L.InTunnel) continue;
		const TT::ModelSpec& S = TT::SpecOf(L.Is);
		const FVector Fwd = TTI::UeWay(L.Fx, L.Fy, L.Fz).GetSafeNormal();
		const FVector Stack = TTI::Ue(L.X, L.Y, L.Z) + Fwd * (S.Length * (bSteam ? 0.32 : 0.1) * 100.0) + FVector(0, 0, (S.Height + 0.1) * 100.0);
		double& Owed = PuffDue[T.Id];
		Owed += Dt * (bSteam ? (T.Standing ? 0.6 : FMath::Max(0.8, 4.0 * T.Speed / 5.34)) : (T.Speed > 1.0 && T.Speed < 15.0 && !T.Braking ? 2.0 : 0.0));
		while (Owed >= 1.0 && Puffs.Num() < TTI::PuffCap)
		{
			Owed -= 1.0;
			FPuff P;
			P.Pos = Stack;
			P.Vel = FVector(0, 0, bSteam ? 260.0 : 120.0) - Fwd * (T.Speed * 30.0);
			P.Life = bSteam ? FMath::FRandRange(5.0f, 9.0f) : FMath::FRandRange(3.0f, 5.0f);
			P.Size0 = bSteam ? 90.0f : 60.0f;
			P.Size1 = bSteam ? FMath::FRandRange(500.0f, 800.0f) : 350.0f;
			P.Alpha = bSteam ? 0.55f : 0.25f;
			P.Grey = bSteam ? 0.92f : 0.25f;
			P.Seed = FMath::FRand();
			Puffs.Add(P);
		}
		Owed = FMath::Min(Owed, 3.0);
	}
	if (PuffDue.size() > 64) PuffDue.clear();
	for (int32 I = Puffs.Num() - 1; I >= 0; --I)
	{
		FPuff& P = Puffs[I];
		P.Age += Dt;
		if (P.Age >= P.Life) { Puffs.RemoveAtSwap(I); continue; }
		P.Vel *= FMath::Pow(0.6f, Dt);
		P.Vel.Z = FMath::Max(P.Vel.Z, 40.0);
		P.Pos += P.Vel * Dt;
	}
	// (drawn: each a sprite facing the camera)
	const FVector At = Eye();
	TArray<FTransform> Xfs;
	Xfs.Init(TTI::Hidden, TTI::PuffCap);
	TArray<float> Data;
	Data.Init(0.9f, 6);
	Data[4] = 1.0f;
	for (int32 I = 0; I < TTI::PuffCap; ++I)
	{
		if (I >= Puffs.Num())
		{
			Data[3] = 0.0f;
			Smoke->SetCustomData(I, Data, false);
			continue;
		}
		const FPuff& P = Puffs[I];
		const float F = P.Age / P.Life;
		const float Size = FMath::Lerp(P.Size0, P.Size1, 1.0f - FMath::Cube(1.0f - F));
		const FVector ToEye = (At - P.Pos).GetSafeNormal();
		FVector U, V;
		ToEye.FindBestAxisVectors(U, V);
		const float Turn = P.Seed * 6.283f + P.Age * 0.2f;
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, U * FMath::Cos(Turn) + V * FMath::Sin(Turn));
		Xfs[I] = FTransform(M.ToQuat(), P.Pos, FVector(Size / 100.0f, Size / 100.0f, 1.0f));
		Data[0] = Data[1] = Data[2] = P.Grey;
		Data[3] = P.Alpha * FMath::Min(1.0f, F * 8.0f) * (1.0f - F);
		Data[5] = P.Seed;
		Smoke->SetCustomData(I, Data, false);
	}
	Smoke->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Smoke->MarkRenderStateDirty();
}

// --- [stage 7] Wires, masts and signals ------------------------------------------------------------------------------

void ATorqueTrains::LayLineside(const FVector2D& At)
{
	namespace TT = TorqueWorld::Trains;
	LaidAt = At;
	if (!Masts)
	{
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UStaticMesh* Box = Cube ? Cube.Get() : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Cyl || !Box || !Ball || !Basic) return;
		const auto Tint = [this, Basic](const FLinearColor& C, float Glow)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Basic, this);
			D->SetVectorParameterValue(TEXT("Color"), C * (1.0f + Glow));
			BoxMats.Add(D);   // (kept alive)
			return D;
		};
		// (the masts' galvanised steel, the wires' dark copper; the signals' black heads and their lamps)
		UMaterialInterface* Steel = Mat(TEXT("train_mast"));
		Masts = MakeIsm(TEXT("TrainMasts"), Box, Steel ? Steel : Tint(FLinearColor(0.32f, 0.33f, 0.33f), 0.0f), true);
		Wires = MakeIsm(TEXT("TrainWires"), Cyl, Tint(FLinearColor(0.03f, 0.025f, 0.02f), 0.0f), false);
		SignalPosts = MakeIsm(TEXT("TrainSignals"), Box, Tint(FLinearColor(0.02f, 0.02f, 0.02f), 0.0f), true);
		UMaterialInterface* Red = Mat(TEXT("train_lamp_red"));
		UMaterialInterface* Green = Mat(TEXT("train_lamp_green"));
		LampRed = MakeIsm(TEXT("TrainLampRed"), Ball, Red ? Red : Tint(FLinearColor(1.0f, 0.02f, 0.01f), 20.0f), false);
		LampGreen = MakeIsm(TEXT("TrainLampGreen"), Ball, Green ? Green : Tint(FLinearColor(0.05f, 1.0f, 0.3f), 20.0f), false);
		for (UInstancedStaticMeshComponent* C : {Masts.Get(), Wires.Get(), SignalPosts.Get(), LampRed.Get(), LampGreen.Get()}) C->SetCullDistances(0, 150000);
	}
	TArray<FTransform> MastXf, WireXf, PostXf;
	Signals.Reset();
	std::vector<int> Rs;
	constexpr double Reach = 1400.0, MastStep = 60.0, SignalStep = 1500.0;
	Net->RoutesNear(At.X, At.Y, Reach, Rs);
	for (int Ri : Rs)
	{
		const TT::Route& R = Net->Routes[size_t(Ri)];
		if (Net->Services[size_t(Ri)].empty() && !R.Electric) continue;
		const double S0 = R.S[size_t(R.OwnFrom)], S1 = R.S[size_t(R.OwnTo)];
		// Overhead wires: a mast every 60 m on the side away from the other track (both sides on single track), its arm
		// over the track, the contact wire at 5.1 m zigzagging a little either side, the catenary over it.
		if (R.Electric)
		{
			const double Out = R.Way < 0 ? -1.0 : 1.0;   // (left of the way along it for the track carrying trains along it)
			double PrevX = 0, PrevY = 0, PrevZ = 0;
			bool bPrev = false;
			int32 N = 0;
			for (double S = std::ceil(S0 / MastStep) * MastStep; S <= S1; S += MastStep, ++N)
			{
				double X, Y, Z, Dx, Dz;
				unsigned char On = 0;
				Net->PlaceAt(R, S, X, Y, Z, Dx, Dz, nullptr, &On);
				if (std::hypot(X - At.X, Z - At.Y) > Reach) { bPrev = false; continue; }
				const FVector Fwd(Dx, Dz, 0.0);
				const FVector Left(Dz, -Dx, 0.0);
				const FVector Base = TTI::Ue(X, Y, Z);
				const FQuat Rot = FRotationMatrix::MakeFromYZ(Fwd, FVector::UpVector).ToQuat();
				if (On != 2)
				{
					// (the mast: an H-section upright 3 m out, 7 m tall; its arm out over the track)
					MastXf.Add(FTransform(Rot, Base + Left * (Out * 300.0) + FVector(0, 0, 330.0), FVector(0.25, 0.25, 7.0)));
					MastXf.Add(FTransform(Rot, Base + Left * (Out * 150.0) + FVector(0, 0, 610.0), FVector(3.2, 0.12, 0.12)));
				}
				const double Stagger = (N % 2 == 0 ? 0.2 : -0.2);
				const double Wx = X + Left.X * Stagger, Wz = Z + Left.Y * Stagger;
				if (bPrev)
				{
					for (const double H : {5.1, 6.1})
					{
						const FVector A = TTI::Ue(PrevX, PrevY + H, PrevZ), B = TTI::Ue(Wx, Y + H, Wz);
						const FVector D = B - A;
						WireXf.Add(FTransform(FRotationMatrix::MakeFromZ(D.GetSafeNormal()).ToQuat(), (A + B) * 0.5, FVector(0.012, 0.012, D.Size() / 100.0)));
					}
				}
				PrevX = Wx; PrevY = Y; PrevZ = Wz;
				bPrev = true;
			}
		}
		// Signals on double track with trains: every 1.5 km, on the left of the way trains run, facing them.
		if (R.Double && R.Way != 0 && !Net->Services[size_t(Ri)].empty())
			for (double S = std::ceil(S0 / SignalStep) * SignalStep + 300.0; S <= S1; S += SignalStep)
			{
				double X, Y, Z, Dx, Dz;
				unsigned char On = 0;
				Net->PlaceAt(R, S, X, Y, Z, Dx, Dz, nullptr, &On);
				if (On != 0 || std::hypot(X - At.X, Z - At.Y) > Reach) continue;
				const FVector Way = FVector(Dx, Dz, 0.0) * double(R.Way);
				const FVector Left(Way.Y, -Way.X, 0.0);
				const FVector Foot = TTI::Ue(X, Y, Z) + Left * 230.0;
				const FQuat Rot = FRotationMatrix::MakeFromYZ(Way, FVector::UpVector).ToQuat();
				PostXf.Add(FTransform(Rot, Foot + FVector(0, 0, 200.0), FVector(0.14, 0.14, 4.0)));
				PostXf.Add(FTransform(Rot, Foot + FVector(0, 0, 400.0) - Way * 12.0, FVector(0.4, 0.2, 0.9)));
				FSignal Sig;
				Sig.Route = Ri;
				Sig.Dir = R.Way;
				Sig.S = S;
				Sig.Block = SignalStep;
				Sig.Lamp = FTransform(Rot, Foot + FVector(0, 0, 420.0) - Way * 24.0, FVector(0.14));
				Signals.Add(Sig);
			}
	}
	Masts->ClearInstances();
	Masts->AddInstances(MastXf, false, true);
	Wires->ClearInstances();
	Wires->AddInstances(WireXf, false, true);
	SignalPosts->ClearInstances();
	SignalPosts->AddInstances(PostXf, false, true);
	SinceSignals = 1.0;   // (lit at once)
}

void ATorqueTrains::TickSignals(const TorqueWorld::Trains::Clock& Now)
{
	if (!LampRed || !LampGreen) return;
	// Red while a train is in the block beyond it (behind a train that's passed), green once it's clear.
	TArray<FTransform> Red, Green;
	for (const FSignal& S : Signals)
	{
		const double A = S.Dir > 0 ? S.S : S.S - S.Block, B = S.Dir > 0 ? S.S + S.Block : S.S;
		(Net->Occupied(Now, S.Route, S.Dir, A, B) ? Red : Green).Add(S.Lamp);
	}
	LampRed->ClearInstances();
	LampRed->AddInstances(Red, false, true);
	LampGreen->ClearInstances();
	LampGreen->AddInstances(Green, false, true);
}
