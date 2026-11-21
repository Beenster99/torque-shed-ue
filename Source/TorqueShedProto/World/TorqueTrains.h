// [trains] The trains on Ride UK's railways, drawn round the rider (beenster, Nov 20 2026: "a made up timetable ...
// All train lines should have trains ... add a setting in gameplay to turn them off"). Plan:
// https://claude.ai/artifact/S6xACsnVDrMdQ5EG9psUoB
//
// Where every train is comes from World/WorldTrains.h (plain C++): the routes, the made-up timetable and the clock.
// This actor, one per ride, only draws what that says and makes it felt:
//   - the network built on a worker from the ride's plan (again when the area grows);
//   - every frame, the trains within DrawM of the camera (the server's clock: every co-op machine sees the same
//     train in the same place with nothing sent), each vehicle an instance of its model (Tools/props/make_trains.py:
//     /Game/Shed/Props/train_*/SM_train_*), a plain box in its place until the model is imported; its livery by
//     material overrides, its windows lit after dark, white lamps at its front and red at its back, a headlight;
//   - [stage 4] the level crossings (World/TorqueLevelCrossing.h): each crossing near the rider told whether a train
//     is due (ATorqueLevelCrossing::Warn), from the timetable, so the warning is always full;
//   - the vehicles near a bike as moving capsules in its contacts (a rider who meets a train is knocked off: an
//     ordinary crash and respawn, as beenster chose);
//   - [stage 5] their sounds; [stage 7] overhead wires and masts on electrified lines, signals, steam and diesel haze.
// Settings > Gameplay > Trains off (the host's, ATorqueGameState::bTrainsOn): nothing drawn or heard, crossings open.
// Its game-thread time is the "trains" line of the ride's costs (RideCosts).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "World/WorldTrains.h"   // the full TrainNow: the generated code instantiates ~vector<TrainNow> (MSVC C2036)

#include "TorqueTrains.generated.h"

class ATorqueBikePawn;
class ATorqueRideWorld;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class USpotLightComponent;
class UStaticMesh;
namespace TorqueSim { struct ContactCapsule; }
namespace TorqueWorld { class RidePlan; namespace Trains { class Network; struct TrainNow; struct Clock; } }

UCLASS(NotPlaceable, Transient)
class ATorqueTrains : public AActor
{
	GENERATED_BODY()

public:
	ATorqueTrains();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The ride world, every 0.2 s: the trains stood up for this ride (the network built again when the plan is new).
	static void Keep(ATorqueRideWorld* Ride, const std::shared_ptr<TorqueWorld::RidePlan>& Plan, const FVector2D& RiderAt);
	// The vehicles within 40 m of (X, Y, Z) (the bike sim's frame: m, y up) as moving capsules for its contacts.
	static void Capsules(const UWorld* InWorld, double X, double Y, double Z, std::vector<TorqueSim::ContactCapsule>& Out);
	// Whether any train is within Reach (m) of (X, Z) now (a bike makes its contacts for it).
	static bool AnyNear(const UWorld* InWorld, double X, double Z, double Reach);
	// For the log and TorqueShedTest: "where" (the nearest train and route), "count", "draw" (what's laid as instances),
	// "goto" (our bike put beside the line ahead of the nearest moving train).
	static FString Test(const UWorld* InWorld, const FString& What, ATorqueBikePawn* Bike = nullptr);
	// TorqueShedTest crossings:next: our bike put by the level crossing that closes soonest for a train.
	static FString NextCrossing(const UWorld* InWorld, ATorqueBikePawn* Bike);

	static constexpr double DrawM = 2500.0;    // trains drawn within this of the camera
	static constexpr double ShadowM = 500.0;   // and cast shadows within this

private:
	std::shared_ptr<TorqueWorld::RidePlan> Plan;
	std::shared_ptr<TorqueWorld::Trains::Network> Net;
	const TorqueWorld::RidePlan* NetPlan = nullptr;   // the plan Net was built from
	size_t NetLines = 0;
	bool bBuilding = false;
	double LastRebuild = -1e9;
	void Rebuild();

	TorqueWorld::Trains::Clock ClockNow() const;
	bool TrainsOn() const;
	FVector Eye() const;

	// Drawing: one instanced mesh per (model, livery, faces backwards, near enough for shadows, plain box).
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UInstancedStaticMeshComponent>> Parts;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> Models;   // per Trains::Model (null: not imported yet)
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube = nullptr;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInterface>> Mats;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BoxMats;   // per Trains::Kind
	bool bModelsLoaded = false;
	bool bNight = false;
	void LoadModels();
	UMaterialInterface* Mat(const FString& Key);
	UInstancedStaticMeshComponent* PartFor(int32 Key);
	void Draw(const std::vector<TorqueWorld::Trains::TrainNow>& Trains);
	void HideAll();
	std::vector<TorqueWorld::Trains::TrainNow> Shown;

	// The headlight of the nearest train after dark.
	UPROPERTY(Transient) TObjectPtr<USpotLightComponent> Headlight = nullptr;

	// [stage 4] The crossings near the rider told every 0.1 s.
	double SinceCrossings = 0.0;
	bool bCrossingsTold = false;
	void TellCrossings(const TorqueWorld::Trains::Clock& Now, bool bOn);

	// [stage 5] Sounds: two listeners' worth (the two nearest trains), each its engine, its wheels on the rails, its
	// rush past; horns before crossings and tunnels, brakes into stations, steam's whistle.
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Ears;
	int32 EngineLoop[2] = {0, 0}, RailLoop[2] = {0, 0}, RushLoop[2] = {0, 0};
	uint64 EarTrain[2] = {0, 0};
	std::unordered_map<uint64, double> Horned;     // train id -> when it last sounded (s)
	std::unordered_map<uint64, int32> WasStanding;  // train id -> 1 standing, 0 moving (for whistles and brakes)
	void TickSounds(float Dt, const TorqueWorld::Trains::Clock& Now);
	void StopSounds();

	// [stage 7] Steam and diesel exhaust: puffs on one instanced sprite (the tyre smoke's M_TyreSmoke).
	struct FPuff
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		float Age = 0.0f, Life = 6.0f, Size0 = 80.0f, Size1 = 500.0f, Alpha = 0.5f, Seed = 0.0f, Grey = 0.9f;
	};
	TArray<FPuff> Puffs;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Smoke = nullptr;
	std::unordered_map<uint64, double> PuffDue;   // train id -> chuffs owed
	void TickSmoke(float Dt);

	// [stage 7] Overhead wires and their masts on electrified lines, and the signals, round the camera (laid again
	// every 200 m it moves), the signals' lamps every 0.5 s.
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Masts = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Wires = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> SignalPosts = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> LampRed = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> LampGreen = nullptr;
	FVector2D LaidAt = FVector2D(1e12, 1e12);
	struct FSignal
	{
		int32 Route = 0, Dir = 1;
		double S = 0.0, Block = 1500.0;
		FTransform Lamp;
	};
	TArray<FSignal> Signals;
	double SinceSignals = 0.0;
	void LayLineside(const FVector2D& At);
	void TickSignals(const TorqueWorld::Trains::Clock& Now);
	UInstancedStaticMeshComponent* MakeIsm(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* M, bool bShadow);

	double LogClock = 0.0;
	double CostMs = 0.0, WorstMs = 0.0;
	int32 CostFrames = 0;
};
