// Each player in co-op, as everyone sees them: name (Unreal's own), the colour they ride in, and
// what they're doing in the shed ("fitting clubman bars to bay 2"), drawn over them by ATorqueHUD.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TorquePlayerState.generated.h"

// What a player's hands are doing in the shed (Coop/HandsState.h), for the others to see: the
// tool and its motion, the spray can's colour, and a faint glow on what they're working on.
USTRUCT()
struct FTorqueHandsNet
{
	GENERATED_BODY()
	UPROPERTY() uint8 Tool = 0;        // ETorqueTool
	UPROPERTY() uint8 Work = 0;        // 0 not working, 1..16 how far through (TorqueHands::QuantiseWork)
	UPROPERTY() uint8 Colour = 0;      // the spray colour (UTorqueBuildSubsystem::Paints)
	UPROPERTY() uint8 Finish = 0;
	UPROPERTY() uint8 Look = 0;        // ETorqueLook: what they're aimed at
	UPROPERTY() int8 Bay = -1;         // a bike part's bay (or a bay's floor)
	UPROPERTY() FName Slot;            // its slot
	UPROPERTY() int32 Item = 0;        // a loose part
	UPROPERTY() FName Place;           // a place (bench_3, the booth, the dyno...)
	UPROPERTY() bool bAway = false;    // at the parts computer: tool put away
	UPROPERTY() uint8 Turn = 0;        // how far the part in their hands is turned (R), 256ths of a turn
};

UCLASS()
class ATorquePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* Into) override;

	UPROPERTY(Replicated) FString Activity;
	UPROPERTY(Replicated) uint8 Colour = 0;
	// How they sit on the bike (FTorqueSettings::RiderStyles), so everyone's riders look as they chose.
	UPROPERTY(Replicated) uint8 RiderStyle = 0;
	// [profile] Their character from the Profile page (Shed/TorqueProfile.h, FTorqueProfile::Pack):
	// which MetaHuman, height and face. About 13 bytes, sent when it changes.
	UPROPERTY(Replicated) TArray<uint8> Profile;
	// The shed bay this player last worked on or claimed (-1 none): which bike they ride out on.
	UPROPERTY(Replicated) int32 Bay = -1;
	// [Ride UK] The journey code (the host's, ATorqueGameState::RideJourney) whose road this player's
	// game has planned, ready to set off on it ("" for none).
	UPROPERTY(Replicated) FString RideReadyFor;
	UFUNCTION(Server, Reliable) void ServerSetRideReady(const FString& Code);
	// [co-op] This player's change to the shared journey in the Route planner (everyone plans it).
	UFUNCTION(Server, Reliable) void ServerSetRideJourney(const FString& Code, const FString& Name);
	// The hands (the owner's own copy is set at once; the others get the host's).
	UPROPERTY(ReplicatedUsing = OnRep_Hands) FTorqueHandsNet Hands;
	UFUNCTION() void OnRep_Hands(const FTorqueHandsNet& Before);

	// From the owning player's game; the host passes it on to everyone.
	UFUNCTION(Server, Reliable) void ServerSetActivity(const FString& What);
	UFUNCTION(Server, Reliable) void ServerSetLook(const FString& NewName, uint8 NewColour);
	UFUNCTION(Server, Reliable) void ServerSetBay(int32 NewBay);
	UFUNCTION(Server, Reliable) void ServerSetRiderStyle(uint8 NewStyle);
	UFUNCTION(Server, Reliable) void ServerSetProfile(const TArray<uint8>& NewProfile);   // [profile]
	UFUNCTION(Server, Reliable) void ServerSetHands(const FTorqueHandsNet& NewHands);

	// The owner's walker, every frame: sent on to the host when it's worth it (HandsState.h).
	void OfferHands(const FTorqueHandsNet& Now, float Dt);
	// The bay this player is working on now (their hands on a bike part or a bay), -1 if none.
	int32 WorkingBay() const { return Hands.Work > 0 ? Hands.Bay : -1; }

	// This player's own state, from any of their objects.
	static ATorquePlayerState* Mine(const UObject* WorldContext);
	FColor ColourValue() const;

private:
	FTorqueHandsNet SentHands;
	double SinceHands = 0.0;
	bool bHandsSent = false;
};
