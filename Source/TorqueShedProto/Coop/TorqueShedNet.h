// The shed in co-op, as the old Torque Shed's shed_state: the host owns the shed and checks and
// applies every action; everyone sees the result.
//
//   A client's walker asks (unbolt, fit, scrub, spray...)  ->  UTorqueBuildSubsystem::Request
//   (checked there first)  ->  Forward (bound here)  ->  ATorquePlayerController::ServerShed  ->
//   the host's subsystem Apply(player id, request) (rules, save, OnTell to that player)  ->
//   OnHostChange  ->  ATorqueGameState writes the whole shed's bytes (SaveShed, a few KB) and bumps
//   their version (replicated)  ->  every client: OnRep  ->  LoadShedFromHost  ->  the shed's
//   actors redraw.
//
// What each player is doing ("fitting the clubman bars to bay 2") and which bay is theirs go on
// ATorquePlayerState. The shed's own code (Shed/) never needs to know whether it's in co-op.
#pragma once

#include "CoreMinimal.h"
#include "TorqueShedNet.generated.h"

struct FTorqueShedRequest;

// The whole shed, as it travels: UTorqueBuildSubsystem::SaveShed's bytes and a version the host
// bumps each time it changes.
USTRUCT()
struct FTorqueShedBayNet
{
	GENERATED_BODY()
	UPROPERTY() int32 Version = 0;
	UPROPERTY() TArray<uint8> Data;
};

// A request from a client (FTorqueShedRequest, which isn't a USTRUCT).
USTRUCT()
struct FTorqueShedRequestNet
{
	GENERATED_BODY()
	UPROPERTY() uint8 Kind = 0;
	UPROPERTY() int32 Bay = -1;
	UPROPERTY() int32 Engine = 0;
	UPROPERTY() FName Slot;
	UPROPERTY() int32 Item = 0;
	UPROPERTY() TArray<int32> Items;
	UPROPERTY() FName Spot;
	UPROPERTY() FName Part;
	UPROPERTY() FName Bin;
	UPROPERTY() FName Finish;
	UPROPERTY() int32 Colour = 0;
	UPROPERTY() FTransform Place;
	UPROPERTY() bool bCrouched = false;
	UPROPERTY() FString Text;    // GeoSave's fit (the host refuses one that's too long)
	UPROPERTY() bool bGun = false;   // [shed] the spray gun (the booth's paint jobs)
	UPROPERTY() float Amount = 0.0f; // [shed] the mixing bay: grams poured, how much stirred
	UPROPERTY() float Tilt = 0.0f;   // [shed] ... how far the bottle's tipped
	UPROPERTY() TArray<float> Amounts;   // [wash] Wash: each item's dirt times this
};

namespace TorqueShedNet
{
FTorqueShedRequestNet ToNet(const FTorqueShedRequest& R);
FTorqueShedRequest FromNet(const FTorqueShedRequestNet& R);
}
