// [garage] The garage unit on a Ride UK forecourt (beenster, Nov 17 2026: "When you get to a petrol
// station there is a garage unit that you can ride your bike into, make room for 3 bikes, then when in
// this room you can use your spanner and a shop computer mounted on a wall"; "You can't spanner your
// bike on the roadside"). Where they are is the ride's plan (RidePlan::GarageOf / GaragesNear, placed
// with the forecourts in WorldRideTownLife.cpp); the model is the props' forecourt_garage
// (Tools/props/make_props.py). This actor stands one up near the rider and gives it what the model
// hasn't: walls the walker and the camera stop at (boxes that block the visibility channel), the same
// walls as the bike's contact boxes (ContactBoxes), a floor and a ramp up to it the wheels ride on
// (parts tagged Road), the wall computer's place, and lights. The jobs done in it are the shed's
// (TorqueGarageActs.cpp); the bike pawn asks Inside / ComputerAt whether you're in one.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <vector>

#include "TorqueForecourtGarage.generated.h"

class ATorqueRideWorld;
class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
namespace TorqueSim { struct ContactBox; }
namespace TorqueWorld { class RidePlan; }

UCLASS()
class ATorqueForecourtGarage : public AActor
{
	GENERATED_BODY()

public:
	ATorqueForecourtGarage();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The ride world, every 0.2 s with the rider's place (world metres, x east and z south): garages
	// stood up within 350 m, taken down past 500 m.
	static void Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt);
	// The garage a place (Unreal cm, at the feet or the bike) is inside, or null.
	static ATorqueForecourtGarage* Inside(const UWorld* World, const FVector& At);
	// The walls (and bench) as the bike's contact boxes; the version changes whenever a garage comes or goes.
	static void ContactBoxes(const UWorld* World, std::vector<TorqueSim::ContactBox>& Out);
	static int32 ContactVersion();

	// Inside this one (Unreal cm), with a margin in from the walls.
	bool Holds(const FVector& At, double Margin = 0.0) const;
	// The wall computer: its screen's middle (world cm) and the way it faces.
	FVector ComputerAt() const;
	FVector ComputerFacing() const;
	int32 Court() const { return CourtIndex; }

private:
	void Build(const TorqueWorld::RidePlan& Plan, int32 Court, double Base, double DoorGround);
	UBoxComponent* AddBox(const FName& Name, const FVector& Lo, const FVector& Hi, bool bGround, bool bSolid);

	UPROPERTY(Transient) UStaticMeshComponent* Model = nullptr;
	UPROPERTY(Transient) TArray<UBoxComponent*> Boxes;
	UPROPERTY(Transient) TArray<UPointLightComponent*> Lamps;
	TArray<bool> BoxSolid;   // per box: a wall the bike bumps into
	int32 CourtIndex = -1;
};
