#include "World/TorqueForecourtGarage.h"

#include "World/TorqueRideWorld.h"
#include "World/WorldRide.h"
#include "Sim/BikeContacts.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

#include <cmath>

namespace TorqueForecourtGarageImpl
{
// The model's inside, in its own frame (cm: x across, y front to back with +y the door side, z up):
// as Tools/props/make_props.py forecourt_garage draws it (7.5 x 6.5 m outside, walls 25 cm, the door
// 5 m wide and 3 m high, the floor's top 2 cm up).
constexpr double HalfW = 375.0, HalfD = 325.0, Wall = 25.0, Height = 360.0, DoorHalf = 250.0, DoorTop = 300.0, FloorTop = 2.0;
constexpr double NearM = 350.0, FarM = 500.0;   // stood up within, taken down past (m)
TArray<TWeakObjectPtr<ATorqueForecourtGarage>>& Live()
{
	static TArray<TWeakObjectPtr<ATorqueForecourtGarage>> Out;   // (weak pointers: nothing held from GC)
	return Out;
}
int32& Version()
{
	static int32 V = 0;
	return V;
}
const TCHAR* MeshPath = TEXT("/Game/Shed/Props/forecourt_garage/SM_forecourt_garage.SM_forecourt_garage");
} // namespace TorqueForecourtGarageImpl
namespace TFGI = TorqueForecourtGarageImpl;

ATorqueForecourtGarage::ATorqueForecourtGarage()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	RootComponent = Origin;
	// (only the floor and ramp parts are ground: a wheel's trace that meets a wall steps past just that wall)
	Tags.Add(TEXT("RoadParts"));
}

void ATorqueForecourtGarage::EndPlay(const EEndPlayReason::Type Reason)
{
	TFGI::Live().RemoveAll([this](const TWeakObjectPtr<ATorqueForecourtGarage>& G) { return !G.IsValid() || G.Get() == this; });
	++TFGI::Version();
	Super::EndPlay(Reason);
}

UBoxComponent* ATorqueForecourtGarage::AddBox(const FName& Name, const FVector& Lo, const FVector& Hi, bool bGround, bool bSolid)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(this, Name);
	Box->SetupAttachment(RootComponent);
	Box->SetRelativeLocation((Lo + Hi) * 0.5);
	Box->SetBoxExtent((Hi - Lo) * 0.5);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Box->SetHiddenInGame(true);
	if (bGround) Box->ComponentTags.Add(TEXT("Road"));
	Box->RegisterComponent();
	Boxes.Add(Box);
	BoxSolid.Add(bSolid);
	return Box;
}

void ATorqueForecourtGarage::Build(const TorqueWorld::RidePlan& Plan, int32 Court, double Base, double DoorGround)
{
	(void)Plan;
	CourtIndex = Court;
	using namespace TorqueForecourtGarageImpl;
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
	{
		Model = NewObject<UStaticMeshComponent>(this, TEXT("Model"));
		Model->SetupAttachment(RootComponent);
		Model->SetStaticMesh(Mesh);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // (the boxes below are its collision)
		Model->RegisterComponent();
	}
	else
		UE_LOG(LogTemp, Warning, TEXT("Torque garage: no model at %s (run Tools/ue_import_props.py forecourt_garage); only its walls and floor"), MeshPath);
	// The walls (the walker, the camera and the bike stop at them), the front's piers and the lintel.
	AddBox(TEXT("WallL"), FVector(-HalfW, -HalfD, 0.0), FVector(-HalfW + Wall, HalfD, Height), false, true);
	AddBox(TEXT("WallR"), FVector(HalfW - Wall, -HalfD, 0.0), FVector(HalfW, HalfD, Height), false, true);
	AddBox(TEXT("WallB"), FVector(-HalfW, -HalfD, 0.0), FVector(HalfW, -HalfD + Wall, Height), false, true);
	AddBox(TEXT("PierL"), FVector(-HalfW, HalfD - Wall, 0.0), FVector(-DoorHalf, HalfD, Height), false, true);
	AddBox(TEXT("PierR"), FVector(DoorHalf, HalfD - Wall, 0.0), FVector(HalfW, HalfD, Height), false, true);
	AddBox(TEXT("Lintel"), FVector(-DoorHalf, HalfD - Wall, DoorTop), FVector(DoorHalf, HalfD, Height), false, false);
	AddBox(TEXT("Roof"), FVector(-HalfW, -HalfD, Height - 12.0), FVector(HalfW, HalfD, Height + 12.0), false, false);
	// The bench along the back wall (and its tool board over it).
	AddBox(TEXT("Bench"), FVector(-240.0, -HalfD + Wall, 0.0), FVector(100.0, -HalfD + Wall + 67.0, 90.0), false, true);
	// The floor the wheels and feet stand on, and its ramp down to the forecourt outside the door.
	AddBox(TEXT("Floor"), FVector(-HalfW + Wall, -HalfD + Wall, FloorTop - 40.0), FVector(HalfW - Wall, HalfD + 2.0, FloorTop), true, false);
	const double Drop = (Base - DoorGround) * 100.0 + FloorTop;   // the floor's top over the ground 1.5 m out
	if (Drop > 3.0)
	{
		const double Run = 150.0, Len = FMath::Sqrt(Run * Run + Drop * Drop);
		const double Pitch = FMath::RadiansToDegrees(std::atan2(Drop, Run));
		UBoxComponent* Ramp = AddBox(TEXT("Ramp"), FVector(-DoorHalf, 0.0, -20.0), FVector(DoorHalf, Len, 0.0), true, false);
		// (its top face from the threshold down and out: turned about x, the door's edge kept at the floor's top)
		const FQuat Tilt(FVector::XAxisVector, -FMath::DegreesToRadians(Pitch));   // (+y, out of the door, tipped down)
		Ramp->SetRelativeRotation(Tilt);
		Ramp->SetRelativeLocation(FVector(0.0, HalfD, FloorTop) + Tilt.RotateVector(FVector(0.0, Len * 0.5, -10.0)));
	}
	// Its LED battens: one light over each bay, soft and unshadowed (the walls keep it in well enough).
	for (int32 K = 0; K < 3; ++K)
	{
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetupAttachment(RootComponent);
		Lamp->SetRelativeLocation(FVector((K - 1) * 233.0, 20.0, Height - 30.0));
		Lamp->SetIntensityUnits(ELightUnits::Lumens);
		Lamp->SetIntensity(1800.0f);
		Lamp->SetAttenuationRadius(520.0f);
		Lamp->SetSourceLength(260.0f);
		Lamp->SetLightColor(FLinearColor(1.0f, 0.97f, 0.92f));
		Lamp->SetCastShadows(false);
		Lamp->RegisterComponent();
		Lamps.Add(Lamp);
	}
}

void ATorqueForecourtGarage::Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt)
{
	UWorld* World = Ride ? Ride->GetWorld() : nullptr;
	if (!World) return;
	TArray<TWeakObjectPtr<ATorqueForecourtGarage>>& Have = TFGI::Live();
	// Gone too far: taken down.
	for (int32 I = Have.Num() - 1; I >= 0; --I)
	{
		ATorqueForecourtGarage* G = Have[I].Get();
		if (!G || G->GetWorld() != World)
		{
			Have.RemoveAt(I);
			continue;
		}
		const FVector L = G->GetActorLocation();
		if (FVector2D::Distance(FVector2D(L.X, L.Y) / 100.0, RiderAt) > TFGI::FarM)
		{
			Have.RemoveAt(I);
			G->Destroy();
			++TFGI::Version();
		}
	}
	std::vector<TorqueWorld::RidePlan::GarageSpot> Near;
	Plan.GaragesNear(RiderAt.X, RiderAt.Y, TFGI::NearM, Near);
	for (const TorqueWorld::RidePlan::GarageSpot& S : Near)
	{
		bool bStanding = false;
		for (const TWeakObjectPtr<ATorqueForecourtGarage>& G : Have) bStanding |= G.IsValid() && G->CourtIndex == S.Court;
		if (bStanding) continue;
		// Its front (the model's +y) to the door: a yaw that turns +y onto the door's way out.
		const double Yaw = FMath::RadiansToDegrees(std::atan2(-S.Door.X, S.Door.Z));
		const FVector At(S.Middle.X * 100.0, S.Middle.Z * 100.0, S.Base * 100.0);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient;
		ATorqueForecourtGarage* G = World->SpawnActor<ATorqueForecourtGarage>(ATorqueForecourtGarage::StaticClass(), At, FRotator(0.0, Yaw, 0.0), P);
		if (!G) continue;
		G->Build(Plan, S.Court, S.Base, S.DoorGround);
		Have.Add(G);
		++TFGI::Version();
		UE_LOG(LogTemp, Log, TEXT("Torque garage: forecourt %d's garage stood up at %s, facing %.0f deg (floor %.2f m, the ground at its door %.2f m)"),
			S.Court, *At.ToCompactString(), Yaw, S.Base, S.DoorGround);
	}
}

bool ATorqueForecourtGarage::Holds(const FVector& At, double Margin) const
{
	using namespace TorqueForecourtGarageImpl;
	const FVector L = GetActorTransform().InverseTransformPosition(At);
	return FMath::Abs(L.X) <= HalfW - Wall - Margin && L.Y >= -HalfD + Wall + Margin && L.Y <= HalfD + 20.0 - Margin && L.Z > -150.0 && L.Z < Height;
}

ATorqueForecourtGarage* ATorqueForecourtGarage::Inside(const UWorld* World, const FVector& At)
{
	for (const TWeakObjectPtr<ATorqueForecourtGarage>& G : TFGI::Live())
		if (G.IsValid() && G->GetWorld() == World && G->Holds(At)) return G.Get();
	return nullptr;
}

FVector ATorqueForecourtGarage::ComputerAt() const
{
	// The screen on the right-hand wall, 1.25 m in from the back, its middle 1.53 m up (make_props.py).
	using namespace TorqueForecourtGarageImpl;
	return GetActorTransform().TransformPosition(FVector(HalfW - Wall - 17.0, -HalfD + 125.0, 153.0));
}

FVector ATorqueForecourtGarage::ComputerFacing() const
{
	return GetActorQuat().RotateVector(-FVector::XAxisVector);
}

void ATorqueForecourtGarage::ContactBoxes(const UWorld* World, std::vector<TorqueSim::ContactBox>& Out)
{
	for (const TWeakObjectPtr<ATorqueForecourtGarage>& Weak : TFGI::Live())
	{
		const ATorqueForecourtGarage* G = Weak.Get();
		if (!G || G->GetWorld() != World) continue;
		for (int32 I = 0; I < G->Boxes.Num(); ++I)
		{
			const UBoxComponent* Box = G->Boxes[I];
			if (!Box || !G->BoxSolid.IsValidIndex(I) || !G->BoxSolid[I]) continue;
			// Sim frame: metres, x east, z the world's y (Unreal's), y up.
			const FVector C = Box->GetComponentLocation();
			const FVector U = Box->GetComponentQuat().RotateVector(FVector::XAxisVector);
			const FVector E = Box->GetUnscaledBoxExtent();
			TorqueSim::ContactBox B;
			B.Cx = C.X / 100.0;
			B.Cz = C.Y / 100.0;
			B.Ux = U.X;
			B.Uz = U.Y;
			B.Hu = E.X / 100.0;
			B.Hv = E.Y / 100.0;
			B.Y0 = (C.Z - E.Z) / 100.0;
			B.Y1 = (C.Z + E.Z) / 100.0;
			Out.push_back(B);
		}
	}
}

int32 ATorqueForecourtGarage::ContactVersion()
{
	return TFGI::Version();
}
