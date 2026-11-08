#include "Shed/TorqueFlow.h"

#include "Coop/TorqueSession.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

namespace TorqueFlow
{
const TCHAR* ShedMap = TEXT("/Game/Shed/Shed");
const TCHAR* RideMap = TEXT("/Game/Maps/Buttertubs");

void RideOut(UObject* WorldContext)
{
	// Alone, load the ride; hosting co-op, take everyone (a seamless server travel). A joined rider
	// waits for the host (the walker refuses on a client).
	UE_LOG(LogTemp, Log, TEXT("Torque: riding out of the shed to %s"), RideMap);
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(WorldContext)) B->RodeOut();   // it comes back dirtier
	UTorqueSession::StartRide(WorldContext);
}

bool ShedExists()
{
	return FPackageName::DoesPackageExist(ShedMap);
}

bool BackToShed(UObject* WorldContext)
{
	if (!ShedExists()) return false;
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World && World->GetNetMode() == NM_Client) return false;   // the host takes everyone back
	UE_LOG(LogTemp, Log, TEXT("Torque: back to the shed"));
	UTorqueSession::GoToShed(WorldContext);
	return true;
}
}
