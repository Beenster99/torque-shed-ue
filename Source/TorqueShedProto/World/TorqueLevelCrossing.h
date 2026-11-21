// [level crossings] One of Ride UK's level crossings, stood up near the rider from the crossings' kit (beenster,
// Nov 20 2026: "Please build a 3D blender model for these level crossing signs, which flash when there is an
// imminent train"; the trains plan's stage 4). Where they are is the ride's plan (RidePlan::LevelCrossings, found
// with the furniture); how each type is laid out is World/WorldLevelCrossing; the models are
// Tools/props/make_level_crossing.py's (lx_*).
//
// A train coming: the trains (World/TorqueTrains, their own thread's) call Warn with the crossing's Key from
// LeadSeconds before the train reaches it until its back has cleared it. The crossing then runs Britain's sequence
// itself: the amber steady for 3 s, then the two reds flashing in turn, the alarm, the booms lowering (half
// barriers from 5 s, full ones from 7 s); the train past, the booms rise and the lights go out once they're up (an
// open crossing's at once). A gated crossing's miniature stop light turns red and its gates swing shut across the
// road. Every machine runs it from its own trains' word (they're timed from the host's clock), so nothing is sent.
//
// Until the trains call Warn, torque.CrossingDemo 1 (the default) runs a pretend train past each crossing every
// two and a half minutes so it can be seen working; 2 holds every crossing shut (for pictures); 0 leaves them open.
//
// Lowered booms and shut gates are solid to the bike (ContactBoxes, as the ferries' barriers are).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <vector>

#include "TorqueLevelCrossing.generated.h"

class ATorqueRideWorld;
class UDecalComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
namespace TorqueSim { struct ContactBox; }
namespace TorqueWorld { class RidePlan; }

UCLASS()
class ATorqueLevelCrossing : public AActor
{
	GENERATED_BODY()

public:
	ATorqueLevelCrossing();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The ride world, every 0.2 s with the rider's place (world metres, x east and z south): crossings stood up
	// within 700 m, taken down past 900 m.
	static void Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt);
	// [trains hook] A train is near crossing Key (RidePlan::LevelCrossing::Key) or not. Call it as often as you like
	// (each tick is fine): it's a map write, held by key whether or not the crossing is stood up. The first call
	// (either way) ends the demo for the rest of the session.
	static void Warn(const UWorld* World, uint32 Key, bool bTrainNear);
	// [trains hook] How long before the train reaches a crossing of this type (LevelX::EType) to start warning it:
	// long enough for the full sequence to finish before it arrives.
	static double LeadSeconds(int32 Type);
	// The lowered booms and shut gates as the bike's contact boxes; the version changes whenever they do.
	static void ContactBoxes(const UWorld* World, std::vector<TorqueSim::ContactBox>& Out);
	static int32 ContactVersion();
	// For TorqueShedTest: "where" (the nearest crossing, its type and state).
	static FString Test(const UWorld* World, const FVector& At);

private:
	struct FUnit { TArray<UStaticMeshComponent*> Lit; };   // red left, red right, amber
	struct FBoom
	{
		USceneComponent* Pivot = nullptr;
		FVector PivotAt = FVector::ZeroVector;   // (relative to the actor)
		double Yaw = 0.0, Length = 4.0;
		bool bSkirted = false;
		TArray<UStaticMeshComponent*> Lamps;   // the three lamps' lit faces
		UStaticMeshComponent* Rail = nullptr;  // the skirt's bottom rail
		int32 FirstBar = 0, Bars = 0;          // its skirt bars in Skirts
	};
	struct FGate
	{
		USceneComponent* Hinge = nullptr;
		double OpenYaw = 0.0, ShutYaw = 0.0, Length = 3.0;
	};

	void Build(const TorqueWorld::RidePlan& Plan, int32 Index);
	UStaticMeshComponent* AddProp(USceneComponent* Parent, const FString& Id, const FVector& At, const FRotator& Rot, bool bShadow = true);
	void Pose();
	bool WantShut(double Now) const;

	uint32 Key = 0;
	int32 Type = 0;
	int32 Row = -1;
	bool bWant = false;           // a train near (or the demo's pretend one)
	double Since = 1e9;           // how long it's been as it is (s)
	double BoomAt = 90.0;         // the booms' angle (degrees: 0 down across the road, up to UpAngle)
	double GateAt = 0.0;          // the gates: 0 open along the road, 1 shut across it
	double UpAngle = 87.0;
	bool bSolid = false;          // (as last given to ContactBoxes)
	int32 AlarmLoop = 0, MotorLoop = 0;
	TArray<FUnit> Units;
	TArray<FBoom> Booms;
	TArray<FGate> Gates;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> MslRed;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> MslGreen;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Parts;   // (every model, held from GC)
	UPROPERTY(Transient) TArray<USceneComponent*> Frames;       // (the pivots and hinges)
	UPROPERTY(Transient) UInstancedStaticMeshComponent* Skirts = nullptr;
	UPROPERTY(Transient) TArray<UInstancedStaticMeshComponent*> Fences;
	UPROPERTY(Transient) TArray<UDecalComponent*> Paint;
};
