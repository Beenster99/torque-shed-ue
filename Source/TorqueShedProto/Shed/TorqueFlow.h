// Moving between the shed and the road. One place for it, so co-op can make it a ServerTravel
// for everyone in the session (UTorqueSession::StartRide) without touching the shed.
#pragma once

#include "CoreMinimal.h"

namespace TorqueFlow
{
// The shed and the ride's maps.
extern const TCHAR* ShedMap;
extern const TCHAR* RideMap;

// Out of the roller door: load the ride with the bike as built (the build is on the game instance).
void RideOut(UObject* WorldContext);
// Back to the shed from the road, when the shed map has been built.
bool BackToShed(UObject* WorldContext);
bool ShedExists();
}
