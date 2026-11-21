// [race tracks] The lap timer on screen (the plan "UK race tracks you can ride", stage 5).
//
// The timer itself is plain C++ (World/WorldRideCircuits.h: CircuitLaps). Each rider's own game times their
// laps from their bike's place every frame; crossing a circuit's start line starts one, its sectors split
// it, the line again ends it. A lap well off the tarmac doesn't count. On a road course (the TT, the North
// West 200, Dundrod, Oliver's Mount) the gates on the road split it.
//
// While a lap is under way (and for a while after one), a panel at the top of the screen: the circuit, the
// lap's time running, its sectors, the last lap and the best; under it, every rider's best of that circuit
// this ride (sent to the others by ATorquePlayerState::LapBests). Nothing is kept after the ride.
#pragma once

#include "CoreMinimal.h"

class APawn;
class UCanvas;

namespace TorqueLapsUe
{
// A new ride: no laps yet.
void NewRide();
// The rider was put somewhere else (a respawn, the circuit's start): no line crossed on the way.
void Moved();
// The rider's own bike every frame, its place in the ride's frame (metres: x east, y up, z south).
void StepBike(APawn* Bike, const FVector& SimPlace, float Dt);
// The panel, over the riding HUD (nothing when no lap has been ridden lately).
void Draw(UCanvas* Canvas, const APawn* Bike);
// This ride's best counted lap of any circuit (for the ride awards): its seconds and circuit, false with none.
bool BestLap(double& Seconds, FString& Circuit);
}
