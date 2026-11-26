// [ferries] A small vehicle ferry across one of Ride UK's crossings (beenster, Nov 19 2026: "The ferries should be short
// in realtime and we have to negotiate riding our bikes on and off the ferry, we should be able to walk about on the
// ferry. Each ferry should feel small (for a couple of cars and a couple of bikes)").
//
// Where the crossings are and how each is laid on the real shore is World/WorldFerries.h (plain C++, tested in
// SimTest). This actor stands one crossing up near the rider: its two slipways (and the approach road from the map's
// road where it stops short of the water), the shore kit at each slipway's head (the ticket hut, the barrier, the
// shelter, the sign), and the boat (Tools/props/make_ferry.py's ferry_small, its two ramps ferry_ramp, two parked cars
// from the ride's prop kit) with what the model hasn't: a steel deck and ramps the wheels and feet stand on (boxes
// tagged Road and Steel), walls the walker and the camera stop at, the same walls as the bike's contact boxes while
// it's tied up, the stair up to the upper deck.
//
// The boat lies at one slipway with that end's ramp down. Ride up the ramp, stop, get off: once every rider's bike is
// parked on the deck and they are aboard, the ramp comes up and it sails (a minute or two whatever the route's
// length), carrying the parked bike and the riders walking about; on the far side the other ramp comes down and you
// get back on and ride off. Waiting at the other side, the boat comes over for you. The bike is never ridden while it
// moves (getting on is held until the ramp is down), so the bike's sim never meets a moving ground.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <memory>
#include <vector>

#include "TorqueFerry.generated.h"

class ATorqueRideWorld;
class ATorqueBikePawn;
class UBoxComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
namespace TorqueSim { struct ContactBox; }
namespace TorqueWorld { class RidePlan; namespace Ferries { struct Crossing; struct Layout; } }

UCLASS()
class ATorqueFerry : public AActor
{
	GENERATED_BODY()

public:
	ATorqueFerry();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The ride world, every 0.2 s with the rider's place (world metres, x east and z south): crossings with an end
	// within 1.5 km stood up, taken down past 2.5 km.
	static void Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt);
	// The tied-up boats' walls, the shut barriers and the cars on deck as the bike's contact boxes; the version changes
	// whenever they do (a boat comes, ties up or casts off, a barrier opens or shuts).
	static void ContactBoxes(const UWorld* World, std::vector<TorqueSim::ContactBox>& Out);
	static int32 ContactVersion();
	// The line for a rider's HUD near a ferry (empty when there's none near).
	static FString HintFor(const ATorqueBikePawn* Pawn);
	// For TorqueShedTest: "where" (the nearest crossing and its state), "goto:<end>" (the bike put at that end's
	// slipway head, facing down it).
	static FString Test(const UWorld* World, const FString& What, ATorqueBikePawn* Pawn);

	// A place (Unreal cm) on this boat: on its deck, the stair or the upper deck (with a margin out from its sides).
	bool OnBoard(const FVector& At, double Margin = 0.0) const;
	bool IsSailing() const { return State != EState::Docked; }
	// [build 174 feedback] (beenster, co-op: "the other players bikes shuffled back on the ferry") A mate's parked bike
	// and their feet are sent in the boat's own frame while it's away from its slipway, so each machine draws them on
	// its own deck as it moves, not a tenth of a second behind it. DeckFrame: the boat whose deck At (cm) is on, out
	// from its slipway: its frame, and its crossing's row + 1 (0 when none). DeckFrameOf: that boat's frame here.
	static int32 DeckFrame(const UWorld* World, const FVector& At, FTransform& Out);
	static bool DeckFrameOf(const UWorld* World, int32 Deck, FTransform& Out);

private:
	enum class EState : uint8 { Docked, Raising, Sailing, Lowering };
	void Build(const TorqueWorld::RidePlan& Plan);
	void BuildShore(const TorqueWorld::RidePlan& Plan, int32 End);
	void BuildBoat();
	UBoxComponent* AddBox(USceneComponent* Parent, const FName& Name, const FVector& Lo, const FVector& Hi, bool bGround, bool bSolid, bool bSteel = false);
	UStaticMeshComponent* AddProp(USceneComponent* Parent, const TCHAR* Id, const FVector& At, const FRotator& Rot);
	// The boat's place on its way (0 end 0's berth, 1 end 1's) as a world transform (its middle at the waterline).
	FTransform BoatAt(double S) const;
	void PoseRamps();
	void PoseBarriers();
	void Carry(const FTransform& From, const FTransform& To);
	void Riders(TArray<ATorqueBikePawn*>& Out) const;
	// [ferries stage 4] Its sounds (the engine, the wash, the water lapping, the ramps' hydraulics and clank, the
	// barriers, the horn as it casts off), its wake, its lamps after dark.
	void TickSounds(double Dt, double SpeedMs);
	void TickWake(double Dt, double SpeedMs, const FTransform& Now);
	void TickLamps();
	bool EveryoneAboard() const;
	// [ferries stage 6] Co-op: the host's boat sends its state as it changes (Publish, into the game state's FerryNet;
	// bGone takes it out); a client's follows the host's (Follow: false while the host has none for this crossing,
	// and then it runs itself).
	void Publish(bool bGone = false);
	bool Follow();
	// The riders the boat waits for: within WaitM of either slipway's head, or aboard (a mate far off isn't waited for).
	bool WaitsFor(const ATorqueBikePawn* P) const;
	// [ferries stage 6] The host on deck holding the horn: it sails without the riders not aboard yet.
	bool HostSaysGo(double Dt);

	std::shared_ptr<TorqueWorld::Ferries::Crossing> Cross;
	std::shared_ptr<TorqueWorld::Ferries::Layout> Lay;
	int32 ListRow = -1;
	FString PlaceName[2];
	EState State = EState::Docked;
	int32 DockEnd = 0;                // where it's tied up, or bound for while it's under way
	double Along = 0.0;               // 0..1 of the way from end 0's berth to end 1's
	double Clock = 0.0;               // in this state (s)
	double ReadyFor = 0.0;            // everyone aboard and parked this long (s)
	double SailFor = 60.0;            // this sailing's time (s)
	bool bEmpty = false;              // sailing over to fetch a rider at the other end
	bool bJustIn = false;             // tied up with its riders still aboard from the sailing just ended
	double RampAt[2] = {0.0, 1.0};    // each ramp: 0 down on its slipway, 1 raised
	double ArmAt[2] = {0.0, 0.0};     // each barrier's arm: 0 down across the lane, 1 up
	double ArmYaw[2] = {0.0, 0.0};    // each arm's pivot's facing (degrees)
	FTransform BoatWas;
	bool bBoatPlaced = false;
	double Bob = 0.0;                 // the boat's gentle roll and heave's clock
	double HornHeld = 0.0;            // [stage 6] the host's horn held on deck (s)
	int32 Waiting = 0;                // [stage 6] riders it waits for not aboard yet (for the hint)
	bool bFollowing = false;          // [stage 6] a client's boat following the host's
	// [stage 7] The spray at the bow under way, a chain ferry's chains; the gulls and the ropes' creak now and then.
	int32 SprayLoop = 0, ChainLoop = 0;
	double GullIn = 6.0, CreakIn = 3.0;
	double Rough() const;             // [stage 7] how rough the water is from the weather's wind (0 calm .. 2)
	void PutBackAshore();             // [stage 7] a rider in the water by the crossing: back at the nearer slipway
	void FadeForHold();               // [stage 7] a long route's hold at sea: the riders' screens dark over the jump
	bool bHoldFade = false;

	UPROPERTY(Transient) USceneComponent* Boat = nullptr;
	UPROPERTY(Transient) USceneComponent* Look = nullptr;          // the boat's model (rolls a little; the boxes don't)
	UPROPERTY(Transient) TArray<USceneComponent*> Ramps;           // each end's ramp, at its hinge (end 0's is the stern's)
	UPROPERTY(Transient) TArray<USceneComponent*> Arms;            // each slipway's barrier arm, at its pivot
	UPROPERTY(Transient) TArray<UBoxComponent*> Gates;             // each deck end, shut while its ramp isn't down
	UPROPERTY(Transient) TArray<UBoxComponent*> Barriers;          // each slipway's barrier, shut while the boat isn't there
	UPROPERTY(Transient) TArray<UProceduralMeshComponent*> Slips;
	UPROPERTY(Transient) TArray<UBoxComponent*> Boxes;
	TArray<bool> BoxSolid;   // per box: a wall the bike bumps into (while tied up)
	TArray<TWeakObjectPtr<ATorqueBikePawn>> Tied;   // pawns told to tick after this
	int32 EngineLoop = 0, WashLoop = 0, LapLoop = 0, RampLoop[2] = {0, 0};
	double RampHeard[2] = {0.0, 1.0}, ArmHeard[2] = {0.0, 0.0};
	bool bRampMoving[2] = {false, false};
	TArray<FVector> Trail;            // the wake: where the boat's trailing end passed (world cm), oldest first
	TArray<float> TrailAge, TrailStrength;
	double WakeClock = 0.0;
	bool bWakeShown = false;
	bool bLampsOn = false;
	UPROPERTY(Transient) UProceduralMeshComponent* WakeMesh = nullptr;
	UPROPERTY(Transient) TArray<UPointLightComponent*> Lamps;
};
