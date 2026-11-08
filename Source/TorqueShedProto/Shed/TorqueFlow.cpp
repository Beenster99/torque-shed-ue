#include "Shed/TorqueFlow.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

namespace TorqueFlow
{
const TCHAR* ShedMap = TEXT("/Game/Shed/Shed");
const TCHAR* RideMap = TEXT("/Game/Maps/Buttertubs");

void RideOut(UObject* WorldContext)
{
	// Co-op: replace with UTorqueSession::StartRide(WorldContext) once that is merged; it does the
	// same OpenLevel when playing alone and a ServerTravel for everyone when hosting.
	UE_LOG(LogTemp, Log, TEXT("Torque: riding out of the shed to %s"), RideMap);
	UGameplayStatics::OpenLevel(WorldContext, FName(RideMap), true, TEXT("play"));
}

bool ShedExists()
{
	return FPackageName::DoesPackageExist(ShedMap);
}

bool BackToShed(UObject* WorldContext)
{
	if (!ShedExists()) return false;
	UE_LOG(LogTemp, Log, TEXT("Torque: back to the shed"));
	UGameplayStatics::OpenLevel(WorldContext, FName(ShedMap), true, TEXT("play"));
	return true;
}
}
