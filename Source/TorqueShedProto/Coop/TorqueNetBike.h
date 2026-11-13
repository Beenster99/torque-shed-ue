// Co-op riding: each rider's own machine rides their bike (the sim runs where the rider is, so it
// answers the pad with no lag), and sends a compact snapshot of it about 30 times a second. Everyone
// else draws that bike from the snapshots, smoothed (NetSmoother.h): body pose, steering, lean,
// suspension, wheel spin, revs, gear, throttle and brakes, so the rider's body, the levers, the
// clock and the engine sound all follow, and a crash throws the rider off on every screen.
//
// The snapshots go client -> host as an unreliable server call, and host -> the other clients as a
// replicated property that skips its owner, holding the last four, so a host whose game runs slower
// than 30 frames a second (each frame replicates once) still passes every snapshot on. The host's own
// bike sets the property directly.
//
// This component sits on ATorqueBikePawn, which asks it each frame whether the bike is someone
// else's (IsRemote) and, if so, lets it fill the sim's fields instead of stepping the sim.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "NetSmoother.h"
#include "TorqueNetBike.generated.h"

class ATorqueBikePawn;

USTRUCT()
struct FTorqueBikeNetState
{
	GENERATED_BODY()

	UPROPERTY() double Time = 0.0;              // the sender's clock, s
	UPROPERTY() FVector_NetQuantize100 Pos;     // world cm
	UPROPERTY() FVector_NetQuantize10 Vel;      // cm/s
	UPROPERTY() FQuat Rot = FQuat::Identity;
	UPROPERTY() float Steer = 0.f;              // radians
	UPROPERTY() float Speed = 0.f;              // m/s, signed
	UPROPERTY() float LeanAngle = 0.f;          // radians
	UPROPERTY() float PitchAngle = 0.f;
	UPROPERTY() float Rpm = 0.f;
	UPROPERTY() float Throttle = 0.f;           // the rider's hands and weight
	UPROPERTY() float FrontBrake = 0.f;
	UPROPERTY() float RearBrake = 0.f;
	UPROPERTY() float LeanInput = 0.f;
	UPROPERTY() float ShiftInput = 0.f;
	UPROPERTY() FVector3f FrontCentre = FVector3f::ZeroVector;   // the axles, sim body space m (suspension)
	UPROPERTY() FVector3f RearCentre = FVector3f::ZeroVector;
	UPROPERTY() float FrontOmega = 0.f;         // wheel spin, rad/s
	UPROPERTY() float RearOmega = 0.f;
	UPROPERTY() float FrontKappa = 0.f;         // tyre slip, for the remote bike's squeal
	UPROPERTY() float RearKappa = 0.f;
	UPROPERTY() float FrontSlip = 0.f;
	UPROPERTY() float RearSlip = 0.f;
	UPROPERTY() uint8 Gear = 1;
	UPROPERTY() uint8 Flags = 0;                // 1 crashed, 2 front on the ground, 4 rear on the ground, 8 a ghost, 16 parked (rider off), 32 nodding, 64 waving
	UPROPERTY() uint8 FrontSurf = 0;
	UPROPERTY() uint8 RearSurf = 0;
	UPROPERTY() uint8 Lamps = 0;                // [lights] FTorqueLights::NetBits: headlight, main beam, fog, indicators, hazards, ignition
	UPROPERTY() uint8 Sounds = 0;               // 1 the engine running, 2 the horn held: heard on everyone's copy
	UPROPERTY() FVector_NetQuantize10 FootPos;  // off the bike (Flags 16): where the rider stands, world cm (their feet)
	UPROPERTY() float FootYaw = 0.f;            // and the way they face, degrees
	UPROPERTY() int32 Seq = 0;
	UPROPERTY() int32 Sender = 0;               // [co-op] the sending machine (bikes can change hands): a new one starts afresh
};

UCLASS(ClassGroup = (Torque), meta = (BlueprintSpawnableComponent))
class UTorqueNetBikeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTorqueNetBikeComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// In a networked game, a bike this machine doesn't ride.
	bool IsRemote() const;
	// Someone else's bike: the smoothed snapshot into the pawn's sim fields and drawing state.
	void ApplyRemote(float DeltaSeconds);
	// After the pawn's frame: send our own bike's snapshot when it's time, keep the stats.
	void AfterTick(float DeltaSeconds);
	// [co-op] Bikes swapped on the road (ATorquePlayerController::ServerTakeBike): this machine rides
	// it now where it stands (not moved to a start slot); or it's someone else's now, drawn from
	// theirs; and on the host, the snapshots kept from its last rider are dropped.
	void TakenOver();
	void HandedOver();
	void ForgetSnapshots();
	// A line for the HUD: who's riding, ping, snapshot rates. Empty when riding alone.
	FString StatusLine() const;

	// Where this rider starts, side by side and staggered back from the start: set by the host.
	UPROPERTY(Replicated) int32 Slot = 0;

	// Snapshots a second (-TorqueNetHz=, default 30).
	float SendHz = 30.f;

private:
	// The newest few snapshots, written round in turn (RecentNext is the next to overwrite).
	static constexpr int32 RecentCount = 4;
	UPROPERTY(ReplicatedUsing = OnRep_State) FTorqueBikeNetState Recent[4];   // RecentCount (UHT wants a number)
	int32 RecentNext = 0;
	int32 LastPushedSeq = 0;
	int32 LastSender = 0;
	bool bHandedOver = false;       // let go of here, before the possession's change has come back
	void Keep(const FTorqueBikeNetState& S);

	UFUNCTION() void OnRep_State();
	UFUNCTION(Server, Unreliable) void ServerState(const FTorqueBikeNetState& S);

	ATorqueBikePawn* Bike() const;
	void Capture(FTorqueBikeNetState& S) const;
	void Receive(const FTorqueBikeNetState& S);
	void SetUpSound(bool bRemote);

	TorqueNet::Smoother Smooth;
	double SendClock = 0.0;
	int32 SendSeq = 0;
	bool bPlaced = false;           // our own bike moved to its slot
	int8 SoundRemote = -1;          // how the sound is set up: -1 not yet, 0 ours, 1 someone else's
	double FrontSpin = 0.0, RearSpin = 0.0;

	// Once a second: rates for the HUD and the log (Saved/Logs, "Torque net:" lines).
	double StatClock = 0.0;
	int32 LogCount = 9;             // the first summary goes in the log straight away
	int32 SentInSecond = 0, FramesInSecond = 0;
	int64 RxAtLast = 0, FramesAtLast = 0, ExtrapAtLast = 0, HeldAtLast = 0;
	float TxHz = 0.f, RxHz = 0.f, ExtrapPct = 0.f, Fps = 0.f;
	FString Summary;
};
