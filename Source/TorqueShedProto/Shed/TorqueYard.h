// The riding yard: the large flat concrete pad outside the shed's roller door (Tools/shed_yard.py),
// where everyone rides their own bike about without leaving the shed (beenster, Nov 9). Setting off on
// Buttertubs is a separate choice, the host's alone (the pause menu; TorqueFlow::RideOut).
//
// Riding out swaps the player's walker for their bay's bike (ATorqueBikePawn, which rides the yard's
// route and walls when it's in the shed's map); walking back in swaps the bike for a walker just
// inside the door. The host makes the swap (pawns are the host's to spawn and possess), so a joined
// player asks through ATorquePlayerController's ServerRideYard / ServerWalkBackIn.
#pragma once

#include "CoreMinimal.h"

class APlayerController;

namespace TorqueYard
{
// This player rides bay Bay's bike out of the roller door onto the yard. (The walker has checked the
// bike is ready and the door is up.)
void RideOut(APlayerController* PC, int32 Bay);
// Off the bike and back in through the roller door, on foot.
void WalkBackIn(APlayerController* PC);

// The host's halves (called directly when hosting or alone, or by the server RPCs).
void HostRideOut(APlayerController* PC, int32 Bay);
void HostWalkBackIn(APlayerController* PC);
}
