// How another player's walker looks in the shed: the shape body (ATorqueWalker's Body_* parts)
// dressed in the rider's kit, with the jacket in that player's colour (Settings >
// Character), so you can tell who's who across the shed as in Torque Shed.
#pragma once

#include "CoreMinimal.h"

class APawn;

namespace TorqueLook
{
// Dresses the pawn's Body_* parts and puts them in its player's colour. Cheap when nothing has
// changed, so the walker calls it every tick: the colour arrives (and can change) after BeginPlay.
void Dress(APawn* Pawn);
}
