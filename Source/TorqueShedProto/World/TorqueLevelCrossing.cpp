#include "World/TorqueLevelCrossing.h"

#include "World/TorqueRideWorld.h"
#include "World/WorldLevelCrossing.h"
#include "World/WorldRide.h"
#include "Sim/BikeContacts.h"
#include "TorqueAudio/TorqueSfx.h"

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include <cmath>

namespace TorqueLevelCrossingImpl
{
namespace LevelX = TorqueWorld::LevelX;
// The kit's numbers (Tools/props/make_level_crossing.py; UE cm, the model's x, z -> UE X, Y and its y -> UE Z).
const FVector PivotOnMech(0.0, 32.0, 100.0);   // PIVOT (0, 1.0, 0.32)
constexpr double BoomHalfH = 5.5;              // BOOM_H / 2
constexpr double Skirt = 0.75;                 // SKIRT (m)
constexpr double SkirtStep = 0.16, SkirtFrom = 0.35;
constexpr double NearM = 700.0, FarM = 900.0;  // stood up within, taken down past (m)
// The sequence (s from the warning): the amber alone, then the reds; when the booms start down and how long they take.
constexpr double AmberFor = 3.0, FlashHalf = 0.4;   // (each red lit 0.4 s in turn: 75 flashes a minute each)
constexpr double BoomStartHalf = 5.0, BoomStartFull = 7.0, GateStart = 2.0;
constexpr double DownSecs = 7.0, UpSecs = 6.0, GateSecs = 8.0;
// The demo's pretend train: every crossing shut DemoShut s out of every DemoEvery, each at its own time.
constexpr double DemoEvery = 150.0, DemoShut = 45.0;

TAutoConsoleVariable<int32> CVarDemo(TEXT("torque.CrossingDemo"), 1,
	TEXT("Level crossings with no trains running: 1 (default) a pretend train past each every 2.5 minutes, 2 every crossing held shut, 0 none. ")
	TEXT("Ends by itself once the trains warn the crossings."));

TArray<TWeakObjectPtr<ATorqueLevelCrossing>>& Live()
{
	static TArray<TWeakObjectPtr<ATorqueLevelCrossing>> Out;
	return Out;
}
TMap<uint32, bool>& Warned()
{
	static TMap<uint32, bool> Out;
	return Out;
}
bool& TrainsRun()
{
	static bool b = false;
	return b;
}
int32& Version()
{
	static int32 V = 0;
	return V;
}
FVector Ue(const TorqueWorld::P2& P, double Y) { return FVector(P.X * 100.0, P.Z * 100.0, Y * 100.0); }
double YawOf(const TorqueWorld::P2& Front) { return FMath::RadiansToDegrees(std::atan2(-Front.X, Front.Z)); }
double YawAlong(const TorqueWorld::P2& Dir) { return FMath::RadiansToDegrees(std::atan2(Dir.Z, Dir.X)); }
double Smooth(double T) { T = FMath::Clamp(T, 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
bool Lit(int32 Type) { return Type == LevelX::Lights || Type == LevelX::HalfBarrier || Type == LevelX::FullBarrier; }
bool Barriers(int32 Type) { return Type == LevelX::HalfBarrier || Type == LevelX::FullBarrier; }
} // namespace TorqueLevelCrossingImpl
namespace TLx = TorqueLevelCrossingImpl;

ATorqueLevelCrossing::ATorqueLevelCrossing()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	USceneComponent* Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	RootComponent = Origin;
}

void ATorqueLevelCrossing::EndPlay(const EEndPlayReason::Type Reason)
{
	TLx::Live().RemoveAll([this](const TWeakObjectPtr<ATorqueLevelCrossing>& C) { return !C.IsValid() || C.Get() == this; });
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this))
	{
		if (AlarmLoop) Sfx->FreeLoop(AlarmLoop);
		if (MotorLoop) Sfx->FreeLoop(MotorLoop);
	}
	if (bSolid) ++TLx::Version();
	Super::EndPlay(Reason);
}

void ATorqueLevelCrossing::Warn(const UWorld* W, uint32 InKey, bool bTrainNear)
{
	(void)W;
	TLx::TrainsRun() = true;
	TLx::Warned().FindOrAdd(InKey) = bTrainNear;
}

double ATorqueLevelCrossing::LeadSeconds(int32 InType)
{
	switch (InType)
	{
	case TorqueWorld::LevelX::Lights: return 27.0;
	case TorqueWorld::LevelX::HalfBarrier: return 30.0;
	case TorqueWorld::LevelX::FullBarrier: return 50.0;
	case TorqueWorld::LevelX::Gated: return 40.0;
	default: return 20.0;   // (an open crossing with no lights shows nothing; the warning's harmless)
	}
}

int32 ATorqueLevelCrossing::ContactVersion() { return TLx::Version(); }

UStaticMeshComponent* ATorqueLevelCrossing::AddProp(USceneComponent* Parent, const FString& Id, const FVector& At, const FRotator& Rot, bool bShadow)
{
	const FString Path = FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), *Id, *Id, *Id);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque crossing: no model at %s (run Tools/ue_import_props.py %s)"), *Path, *Id);
		return nullptr;
	}
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetupAttachment(Parent ? Parent : RootComponent.Get());
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocationAndRotation(At, Rot);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCastShadow(bShadow);
	Part->bCastDynamicShadow = bShadow;
	// (small things: gone well before they'd be a speck)
	Part->LDMaxDrawDistance = 60000.0f;
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

void ATorqueLevelCrossing::Build(const TorqueWorld::RidePlan& Plan, int32 Index)
{
	using namespace TorqueLevelCrossingImpl;
	const TorqueWorld::RidePlan::LevelCrossing& C = Plan.LevelCrossings[size_t(Index)];
	Key = C.Key;
	Type = C.Type;
	Row = Index;
	UpAngle = Type == LevelX::FullBarrier ? 82.0 : 87.0;
	const FVector Base = GetActorLocation();
	auto GroundAt = [&Plan, &Base](const TorqueWorld::P2& P, bool bRoad)
	{
		double H = bRoad ? Plan.RoadHeight(P.X, P.Z) : Plan.Ground(P.X, P.Z);
		if (std::isnan(H)) H = bRoad ? Plan.Ground(P.X, P.Z) : Plan.RoadHeight(P.X, P.Z);
		return std::isnan(H) ? Base.Z / 100.0 : H;
	};
	LevelX::Spot S;
	S.Mid = C.Mid;
	S.Road = C.Road;
	S.Rail = C.Rail;
	S.Half = C.Half;
	S.Key = C.Key;
	const std::vector<LevelX::Piece> Pieces = LevelX::Layout(Type, S);
	UMaterialInterface* PaintMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_CrossingPaint.M_CrossingPaint"));
	TMap<FString, UInstancedStaticMeshComponent*> FenceOf;
	for (const LevelX::Piece& P : Pieces)
	{
		const FString Id = UTF8_TO_TCHAR(P.Mesh.c_str());
		const FVector At = Ue(P.At, GroundAt(P.At, P.OnRoad) + P.Up) - Base;
		const FRotator Rot(0.0, YawOf(P.Front), 0.0);
		switch (P.Role)
		{
		case LevelX::Unit:
		{
			AddProp(nullptr, Id, At, Rot);
			FUnit U;
			for (const TCHAR* Lamp : {TEXT("lx_lit_red_l"), TEXT("lx_lit_red_r"), TEXT("lx_lit_amber")})
				if (UStaticMeshComponent* L = AddProp(nullptr, Lamp, At, Rot, false))
				{
					L->SetVisibility(false);
					U.Lit.Add(L);
				}
			Units.Add(U);
			break;
		}
		case LevelX::Contact:
		{
			// (the printed face's row, Courier, set on every slot: the others haven't the parameter)
			if (UStaticMeshComponent* Plate = AddProp(nullptr, Id, At, Rot, false))
				for (int32 Slot = 0; Slot < Plate->GetNumMaterials(); ++Slot)
					if (UMaterialInstanceDynamic* M = Plate->CreateDynamicMaterialInstance(Slot))
						M->SetScalarParameterValue(TEXT("Courier"), float(P.Row));
			break;
		}
		case LevelX::Msl:
		{
			AddProp(nullptr, Id, At, Rot);
			if (UStaticMeshComponent* R = AddProp(nullptr, TEXT("lx_msl_lit_red"), At, Rot, false)) { R->SetVisibility(false); MslRed.Add(R); }
			if (UStaticMeshComponent* G = AddProp(nullptr, TEXT("lx_msl_lit_green"), At, Rot, false)) MslGreen.Add(G);
			break;
		}
		case LevelX::Boom:
		{
			AddProp(nullptr, TEXT("lx_barrier_mech"), At, Rot);
			FBoom B;
			B.Yaw = Rot.Yaw;
			B.Length = P.Length;
			B.bSkirted = P.Skirted;
			B.PivotAt = At + Rot.RotateVector(PivotOnMech);
			B.Pivot = NewObject<USceneComponent>(this);
			B.Pivot->SetupAttachment(RootComponent);
			B.Pivot->SetMobility(EComponentMobility::Movable);
			B.Pivot->SetRelativeLocation(B.PivotAt);
			B.Pivot->RegisterComponent();
			Frames.Add(B.Pivot);
			if (UStaticMeshComponent* Arm = AddProp(B.Pivot, Id, FVector::ZeroVector, FRotator::ZeroRotator))
				Arm->SetRelativeScale3D(FVector(P.Length / LevelX::MeshLength(P.Mesh), 1.0, 1.0));
			double At3[3];
			LevelX::BoomLamps(P.Length, At3);
			for (int32 K = 0; K < 3; ++K)
				if (UStaticMeshComponent* L = AddProp(B.Pivot, TEXT("lx_boom_lit"), FVector(At3[K] * 100.0, 0.0, 0.0), FRotator::ZeroRotator, false))
				{
					L->SetVisibility(false);
					B.Lamps.Add(L);
				}
			if (B.bSkirted)
			{
				if (!Skirts)
				{
					UStaticMesh* Bar = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Shed/Props/lx_skirt_bar/SM_lx_skirt_bar.SM_lx_skirt_bar"));
					if (Bar)
					{
						Skirts = NewObject<UInstancedStaticMeshComponent>(this);
						Skirts->SetupAttachment(RootComponent);
						Skirts->SetMobility(EComponentMobility::Movable);
						Skirts->SetStaticMesh(Bar);
						Skirts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						Skirts->SetCastShadow(false);
						Skirts->RegisterComponent();
					}
				}
				if (Skirts)
				{
					B.FirstBar = Skirts->GetInstanceCount();
					for (double X = SkirtFrom; X < P.Length - 0.1; X += SkirtStep) ++B.Bars;
					for (int32 K = 0; K < B.Bars; ++K) Skirts->AddInstance(FTransform::Identity);
				}
				B.Rail = AddProp(nullptr, TEXT("lx_skirt_rail"), B.PivotAt, Rot, false);
			}
			Booms.Add(B);
			break;
		}
		case LevelX::Gate:
		{
			AddProp(nullptr, TEXT("lx_gate_post"), At, Rot);
			FGate G;
			G.Length = P.Length;
			G.OpenYaw = YawAlong(P.Front);
			G.ShutYaw = YawAlong(P.Shut);
			G.Hinge = NewObject<USceneComponent>(this);
			G.Hinge->SetupAttachment(RootComponent);
			G.Hinge->SetMobility(EComponentMobility::Movable);
			G.Hinge->SetRelativeLocationAndRotation(At, FRotator(0.0, G.OpenYaw, 0.0));
			G.Hinge->RegisterComponent();
			Frames.Add(G.Hinge);
			if (UStaticMeshComponent* Leaf = AddProp(G.Hinge, Id, FVector::ZeroVector, FRotator::ZeroRotator))
				Leaf->SetRelativeScale3D(FVector(P.Length / LevelX::MeshLength(P.Mesh), 1.0, 1.0));
			Gates.Add(G);
			break;
		}
		case LevelX::Fence:
		{
			UInstancedStaticMeshComponent*& Run = FenceOf.FindOrAdd(Id);
			if (!Run)
			{
				const FString Path = FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), *Id, *Id, *Id);
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
				if (!Mesh) break;
				Run = NewObject<UInstancedStaticMeshComponent>(this);
				Run->SetupAttachment(RootComponent);
				Run->SetMobility(EComponentMobility::Movable);
				Run->SetStaticMesh(Mesh);
				Run->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Run->LDMaxDrawDistance = 40000.0f;
				Run->RegisterComponent();
				Fences.Add(Run);
			}
			Run->AddInstance(FTransform(Rot, At));
			break;
		}
		case LevelX::Paint:
		{
			if (!PaintMat) break;
			// The decal projects along its X (down into the road), its Z along the road (U, the image's top towards the
			// line), its Y across (V); FlipV says which way across V runs, so the words read from the traffic's side.
			const FVector Up = FVector::UpVector;
			const FVector Toward(P.Front.X, P.Front.Z, 0.0);
			const FQuat Q = FRotationMatrix::MakeFromXZ(-Up, Toward).ToQuat();
			UDecalComponent* D = NewObject<UDecalComponent>(this);
			D->SetupAttachment(RootComponent);
			D->SetRelativeLocationAndRotation(At, Q);
			D->DecalSize = FVector(40.0, P.Width * 50.0, P.Length * 50.0);
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(PaintMat, this);
			Mid->SetScalarParameterValue(TEXT("Row"), float(P.Row));
			// (V runs towards the decal's -Y; the image's left to right is the traffic's left to right, its right being
			// Left(Front) as the ride's frame has it)
			const FVector TrafficRight(P.Front.Z, -P.Front.X, 0.0);
			Mid->SetScalarParameterValue(TEXT("FlipV"), FVector::DotProduct(Q.GetAxisY(), TrafficRight) > 0.0 ? 1.0f : 0.0f);
			D->SetDecalMaterial(Mid);
			D->FadeScreenSize = 0.002f;
			D->RegisterComponent();
			Paint.Add(D);
			break;
		}
		default:
			AddProp(nullptr, Id, At, Rot, P.Mesh != "lx_contact" && P.Mesh != "lx_keep_clear");
			break;
		}
	}
	if (!PaintMat) UE_LOG(LogTemp, Warning, TEXT("Torque crossing: no M_CrossingPaint (run Tools/ue_build_crossings.py): no stop lines"));
	// Start as the trains (or the demo) have it now, without the sequence: a crossing stood up mid-train is shut.
	const UWorld* W = GetWorld();
	const AGameStateBase* GS = W ? W->GetGameState() : nullptr;
	bWant = WantShut(GS ? GS->GetServerWorldTimeSeconds() : (W ? W->GetTimeSeconds() : 0.0));
	Since = 60.0;
	BoomAt = bWant ? 0.0 : UpAngle;
	GateAt = bWant ? 1.0 : 0.0;
	Pose();
}

bool ATorqueLevelCrossing::WantShut(double Now) const
{
	using namespace TorqueLevelCrossingImpl;
	if (TrainsRun())
	{
		const bool* W = Warned().Find(Key);
		return W && *W;
	}
	const int32 Demo = CVarDemo.GetValueOnGameThread();
	if (Demo == 2) return true;
	if (Demo != 1) return false;
	// (each crossing at its own time: its key spreads them over the cycle)
	const double Phase = std::fmod(Now + double(Key % 1000u) * 0.37, DemoEvery);
	return Phase < DemoShut;
}

void ATorqueLevelCrossing::Pose()
{
	using namespace TorqueLevelCrossingImpl;
	for (FBoom& B : Booms)
	{
		const FRotator R(BoomAt, B.Yaw, 0.0);
		B.Pivot->SetRelativeRotation(R);
		if (B.bSkirted && Skirts)
		{
			// (the skirt's bars hang plumb from the boom whatever its angle; its bottom rail stays parallel to it)
			for (int32 K = 0; K < B.Bars; ++K)
			{
				const FVector Top = B.PivotAt + R.RotateVector(FVector((SkirtFrom + K * SkirtStep) * 100.0, 0.0, -BoomHalfH));
				Skirts->UpdateInstanceTransform(B.FirstBar + K, FTransform(FRotator(0.0, B.Yaw, 0.0), Top, FVector(1.0, 1.0, Skirt)), false, false);
			}
			if (B.Rail)
			{
				B.Rail->SetRelativeLocationAndRotation(B.PivotAt + R.RotateVector(FVector(SkirtFrom * 100.0, 0.0, -BoomHalfH)) - FVector(0.0, 0.0, Skirt * 100.0), R);
				B.Rail->SetRelativeScale3D(FVector(FMath::Max(0.1, B.Length - SkirtFrom - 0.1), 1.0, 1.0));
			}
		}
	}
	if (Skirts) Skirts->MarkRenderStateDirty();
	for (FGate& G : Gates)
	{
		const double Yaw = G.OpenYaw + FMath::FindDeltaAngleDegrees(G.OpenYaw, G.ShutYaw) * Smooth(GateAt);
		G.Hinge->SetRelativeRotation(FRotator(0.0, Yaw, 0.0));
	}
}

void ATorqueLevelCrossing::Tick(float DeltaSeconds)
{
	using namespace TorqueLevelCrossingImpl;
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min(double(DeltaSeconds), 0.1);
	const UWorld* W = GetWorld();
	const AGameStateBase* GS = W ? W->GetGameState() : nullptr;
	const bool bNow = WantShut(GS ? GS->GetServerWorldTimeSeconds() : (W ? W->GetTimeSeconds() : 0.0));
	if (bNow != bWant)
	{
		bWant = bNow;
		Since = 0.0;
	}
	else
		Since += Dt;
	// The booms: down from their start time, up once the train's past.
	const double OldBoom = BoomAt, OldGate = GateAt;
	if (Barriers(Type))
	{
		const double Start = Type == LevelX::FullBarrier ? BoomStartFull : BoomStartHalf;
		const bool bDown = bWant && Since >= Start;
		BoomAt = bDown ? FMath::Max(0.0, BoomAt - UpAngle / DownSecs * Dt) : (bWant ? BoomAt : FMath::Min(UpAngle, BoomAt + UpAngle / UpSecs * Dt));
	}
	if (Type == LevelX::Gated)
	{
		const bool bShut = bWant && Since >= GateStart;
		GateAt = bShut ? FMath::Min(1.0, GateAt + Dt / GateSecs) : (bWant ? GateAt : FMath::Max(0.0, GateAt - Dt / GateSecs));
	}
	const bool bMoving = BoomAt != OldBoom || GateAt != OldGate;
	if (bMoving) Pose();
	// The lights: on while a train's due and (at barriers) until the booms are back up; the amber alone first.
	const bool bBoomsUp = BoomAt >= UpAngle - 0.01;
	const bool bLights = Lit(Type) && (bWant || (Barriers(Type) && !bBoomsUp));
	const double OnFor = bWant ? Since : 1e9;   // (still lit after the train: the reds keep flashing)
	const bool bAmber = bLights && OnFor < AmberFor;
	const int32 Phase = int32(std::floor(FMath::Max(0.0, (bWant ? Since - AmberFor : Since)) / FlashHalf)) & 1;
	const bool bRedL = bLights && !bAmber && Phase == 0, bRedR = bLights && !bAmber && Phase == 1;
	for (FUnit& U : Units)
	{
		if (U.Lit.Num() < 3) continue;
		if (U.Lit[0]->IsVisible() != bRedL) U.Lit[0]->SetVisibility(bRedL);
		if (U.Lit[1]->IsVisible() != bRedR) U.Lit[1]->SetVisibility(bRedR);
		if (U.Lit[2]->IsVisible() != bAmber) U.Lit[2]->SetVisibility(bAmber);
	}
	// The booms' lamps: lit while the boom's off its rest, the tip steady and the other two flashing with the reds.
	for (FBoom& B : Booms)
		for (int32 K = 0; K < B.Lamps.Num(); ++K)
		{
			const bool bOn = !bBoomsUp && (K == 2 || (K == 0 ? Phase == 0 : Phase == 1));
			if (B.Lamps[K]->IsVisible() != bOn) B.Lamps[K]->SetVisibility(bOn);
		}
	// The gated crossing's miniature stop light: red from the warning until the gates are open again, else green.
	const bool bMslRed = Type == LevelX::Gated && (bWant || GateAt > 0.0);
	for (UStaticMeshComponent* M : MslRed)
		if (M->IsVisible() != bMslRed) M->SetVisibility(bMslRed);
	for (UStaticMeshComponent* M : MslGreen)
		if (M->IsVisible() == bMslRed) M->SetVisibility(!bMslRed);
	// Solid to the bike: a boom more than two thirds of the way down, a gate more than four fifths shut.
	const bool bNowSolid = (Barriers(Type) && BoomAt < UpAngle / 3.0) || (Type == LevelX::Gated && GateAt > 0.8);
	if (bNowSolid != bSolid)
	{
		bSolid = bNowSolid;
		++Version();
	}
	// The alarm while the lights are on at an open or half barrier crossing (a full barrier's only while its booms
	// come down), the barrier motors while they move.
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this))
	{
		if (!AlarmLoop && Lit(Type)) AlarmLoop = Sfx->MakeLoop(RootComponent, FVector(0.0, 0.0, 250.0), -6.0f, 1200.0f);
		if (!MotorLoop && (Barriers(Type) || Type == LevelX::Gated)) MotorLoop = Sfx->MakeLoop(RootComponent, FVector(0.0, 0.0, 100.0), -14.0f, 500.0f);
		const bool bAlarm = bLights && (Type != LevelX::FullBarrier || (bWant && BoomAt > 0.0));
		if (AlarmLoop && bAlarm == Sfx->LoopSound(AlarmLoop).IsEmpty()) Sfx->SetLoop(AlarmLoop, bAlarm ? TEXT("lx_alarm") : FString());
		if (MotorLoop && bMoving == Sfx->LoopSound(MotorLoop).IsEmpty()) Sfx->SetLoop(MotorLoop, bMoving ? TEXT("lx_motor") : FString());
	}
}

void ATorqueLevelCrossing::Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt)
{
	using namespace TorqueLevelCrossingImpl;
	UWorld* W = Ride ? Ride->GetWorld() : nullptr;
	if (!W || !Plan.CrossingKit) return;
	TArray<TWeakObjectPtr<ATorqueLevelCrossing>>& Have = Live();
	// (a new ride's plan: every crossing of the last one taken down)
	static const TorqueWorld::RidePlan* Was = nullptr;
	const bool bNewPlan = Was != &Plan;
	Was = &Plan;
	for (int32 I = Have.Num() - 1; I >= 0; --I)
	{
		ATorqueLevelCrossing* C = Have[I].Get();
		if (!C || C->GetWorld() != W)
		{
			Have.RemoveAt(I);
			continue;
		}
		const FVector L = C->GetActorLocation();
		if (bNewPlan || FVector2D::Distance(FVector2D(L.X, L.Y) / 100.0, RiderAt) > FarM)
		{
			Have.RemoveAt(I);
			C->Destroy();
		}
	}
	for (int32 I = 0; I < int32(Plan.LevelCrossings.size()); ++I)
	{
		const TorqueWorld::RidePlan::LevelCrossing& X = Plan.LevelCrossings[size_t(I)];
		if (FVector2D::Distance(FVector2D(X.Mid.X, X.Mid.Z), RiderAt) > NearM) continue;
		bool bStanding = false;
		for (const TWeakObjectPtr<ATorqueLevelCrossing>& C : Have) bStanding |= C.IsValid() && C->Key == X.Key;
		if (bStanding) continue;
		double H = Plan.RoadHeight(X.Mid.X, X.Mid.Z);
		if (std::isnan(H)) H = Plan.Ground(X.Mid.X, X.Mid.Z);
		if (std::isnan(H)) continue;   // (its ground not here yet: next time)
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient;
		ATorqueLevelCrossing* C = W->SpawnActor<ATorqueLevelCrossing>(ATorqueLevelCrossing::StaticClass(), Ue(X.Mid, H), FRotator::ZeroRotator, P);
		if (!C) continue;
		C->Build(Plan, I);
		Have.Add(C);
		UE_LOG(LogTemp, Log, TEXT("Torque crossing: %s crossing stood up at %.0f, %.0f (%s road, %.1f m wide, key %u)"),
			UTF8_TO_TCHAR(LevelX::TypeName(X.Type)), X.Mid.X, X.Mid.Z, UTF8_TO_TCHAR(X.Class.c_str()), X.Half * 2.0, X.Key);
	}
}

void ATorqueLevelCrossing::ContactBoxes(const UWorld* W, std::vector<TorqueSim::ContactBox>& Out)
{
	using namespace TorqueLevelCrossingImpl;
	for (const TWeakObjectPtr<ATorqueLevelCrossing>& Weak : Live())
	{
		const ATorqueLevelCrossing* C = Weak.Get();
		if (!C || C->GetWorld() != W || !C->bSolid) continue;
		const FTransform Root = C->GetActorTransform();
		auto Add = [&Out, &Root](const FVector& A, const FVector& B, double Y0, double Y1)
		{
			// (a thin box from A to B (actor cm) between heights Y0..Y1 (m, absolute))
			const FVector Wa = Root.TransformPosition(A), Wb = Root.TransformPosition(B);
			FVector2D D(Wb.X - Wa.X, Wb.Y - Wa.Y);
			const double Len = D.Size();
			if (Len < 1.0) return;
			D /= Len;
			TorqueSim::ContactBox Box;
			Box.Cx = (Wa.X + Wb.X) * 0.005;
			Box.Cz = (Wa.Y + Wb.Y) * 0.005;
			Box.Ux = D.X;
			Box.Uz = D.Y;
			Box.Hu = Len * 0.005;
			Box.Hv = 0.08;
			Box.Y0 = Y0;
			Box.Y1 = Y1;
			Out.push_back(Box);
		};
		const double Floor = Root.GetLocation().Z / 100.0;
		for (const FBoom& B : C->Booms)
		{
			const FVector Dir = FRotator(0.0, B.Yaw, 0.0).RotateVector(FVector::XAxisVector);
			Add(B.PivotAt, B.PivotAt + Dir * B.Length * 100.0, B.bSkirted ? Floor : (Root.GetLocation().Z + B.PivotAt.Z) / 100.0 - 0.35, (Root.GetLocation().Z + B.PivotAt.Z) / 100.0 + 0.12);
		}
		if (C->Type == TorqueWorld::LevelX::Gated)
			for (const FGate& G : C->Gates)
			{
				const FVector Dir = FRotator(0.0, G.ShutYaw, 0.0).RotateVector(FVector::XAxisVector);
				const FVector At = G.Hinge->GetRelativeLocation();
				Add(At, At + Dir * G.Length * 100.0, Floor - 0.2, Floor + 1.3);
			}
	}
}

FString ATorqueLevelCrossing::Test(const UWorld* W, const FVector& At)
{
	using namespace TorqueLevelCrossingImpl;
	const ATorqueLevelCrossing* Best = nullptr;
	double BestD = 1e18;
	for (const TWeakObjectPtr<ATorqueLevelCrossing>& Weak : Live())
		if (const ATorqueLevelCrossing* C = Weak.Get())
			if (C->GetWorld() == W && FVector::Dist(C->GetActorLocation(), At) < BestD)
			{
				BestD = FVector::Dist(C->GetActorLocation(), At);
				Best = C;
			}
	if (!Best) return TEXT("no level crossing stood up");
	return FString::Printf(TEXT("%s crossing %.0f m off, key %u: %s, booms %.0f deg, gates %.2f shut%s"), UTF8_TO_TCHAR(LevelX::TypeName(Best->Type)), BestD / 100.0, Best->Key,
		Best->bWant ? TEXT("train due") : TEXT("open"), Best->BoomAt, Best->GateAt, TrainsRun() ? TEXT("") : TEXT(" (demo)"));
}
