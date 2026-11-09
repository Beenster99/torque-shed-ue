// The riding yard: the large flat concrete pad outside the shed's roller door (Tools/shed_yard.py),
// where everyone rides their own bike about without leaving the shed (beenster, Nov 9). Setting off on
// Buttertubs is a separate choice, the host's alone (the pause menu; TorqueFlow::RideOut).
//
// Getting on a bike (F at it) swaps the player's walker for that bike (ATorqueBikePawn, which rides
// the shed's floor and the yard, with their walls, when it's in the shed's map), where it stands:
// ridden out of the roller door and back in. Getting off (F, stopped) parks it there and puts a
// walker beside it; Backspace sends it back to its bay and the walker in at the door. The host makes the swap (pawns are the host's to spawn and possess), so a joined
// player asks through ATorquePlayerController's ServerRideYard / ServerWalkBackIn.
#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace TorqueYard
{
// This player gets on bay Bay's bike: bAtBike, where it stands (its bay, or wherever it was parked
// in the shed or on the pad), to ride it out of the roller door and back in; else (F at the door)
// just outside the door. (The walker has checked the bike is ready.)
void RideOut(APlayerController* PC, int32 Bay, bool bAtBike);
// Stopped, F: off the bike, which is parked where it stands (its bay if it's by it, on a lift it's
// over, else on its stand there), and the rider is on foot beside it.
void GetOff(APlayerController* PC);
// Off the bike and back in through the roller door, on foot.
void WalkBackIn(APlayerController* PC);

// The host's halves (called directly when hosting or alone, or by the server RPCs).
void HostRideOut(APlayerController* PC, int32 Bay, bool bAtBike);
void HostGetOff(APlayerController* PC);
void HostWalkBackIn(APlayerController* PC);
}
