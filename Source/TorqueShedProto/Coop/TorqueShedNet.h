// The shed in co-op, as the old Torque Shed's shed_state: the host owns the shed and checks and
// applies every action; everyone sees the result.
//
//   A client's walker fits, washes or paints  ->  UTorqueBuildSubsystem's action  ->  Forward
//   (bound here)  ->  ATorquePlayerController::ServerShed / ServerClean  ->  the host's subsystem
//   Apply (rules, save)  ->  OnHostChange(bay)  ->  ATorqueGameState writes that bay's bytes
//   (replicated)  ->  every client: OnRep  ->  ApplyFromHost(bay)  ->  the shed's actors redraw.
//
// What each player is doing ("fitting clubman bars to bay 2") and which bay is theirs go on
// ATorquePlayerState. The shed's own code (Shed/) never needs to know whether it's in co-op.
#pragma once

#include "CoreMinimal.h"
#include "TorqueShedNet.generated.h"

struct FTorqueShedBike;
struct FTorqueShedRequest;
class UTorqueBuildSubsystem;

// One bay, as it travels: the whole bike, written with TorqueShedNet::Write (a few KB at most) and
// a version the host bumps each time it changes.
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
	UPROPERTY() int32 Bike = 0;
	UPROPERTY() FName Slot;
	UPROPERTY() FName Part;
	UPROPERTY() FName BikeId;
	UPROPERTY() FLinearColor Colour = FLinearColor::White;
	UPROPERTY() float Amount = 0.0f;
};

// Washing, summed per part over a tenth of a second.
USTRUCT()
struct FTorqueCleanNet
{
	GENERATED_BODY()
	UPROPERTY() int32 Bike = 0;
	UPROPERTY() FName Part;
	UPROPERTY() float Amount = 0.0f;
};

namespace TorqueShedNet
{
// A bay to bytes and back. Every field of FTorqueShedBike (and the build's geometry-mode
// adjustments) is here: a field added there must be added here too.
TArray<uint8> Write(const FTorqueShedBike& Bike);
bool Read(const TArray<uint8>& Data, FTorqueShedBike& Out);

FTorqueShedRequestNet ToNet(const FTorqueShedRequest& R);
FTorqueShedRequest FromNet(const FTorqueShedRequestNet& R);
}
