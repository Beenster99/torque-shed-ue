// Torque Shed's ping (ping_marker.gd): middle mouse (Settings > Key bindings > Ping; D-pad left on
// foot) marks whatever is in the middle of your view, up to 60 m away, for the whole crew. Everyone
// sees a glowing wire box round the thing (or a ring on the spot when it's not a thing), a faint fill,
// a beam up from its top and "Name: what it is" over it, in the pinger's colour, through walls, pulsing
// for 6 s and fading out over the last one. A bright two-note chime sounds where it is, and the others
// get "Sam pinged the workbench" across the screen for 2 s.
//
// The ping goes to the host (ATorquePlayerController::ServerPing), which puts the pinger's name and
// colour on it and sends it to every player (ClientPing), the pinger too, as shed_state.gd's "ping"
// action did. Pings change nothing in the shed. The box is sent as it is in the world (the shed is the
// same on every machine), so nobody has to look the thing up again.
//
// It's drawn on the HUD's canvas (ATorqueHUD), not in the world, so it shows through walls as Torque
// Shed's no-depth-test materials did, and needs no material from the level scripts.
#pragma once

#include "CoreMinimal.h"
#include "TorquePing.generated.h"

class AActor;
class APawn;
class APlayerController;
class UCanvas;
class UPrimitiveComponent;

USTRUCT()
struct FTorquePingNet
{
	GENERATED_BODY()
	UPROPERTY() FVector At = FVector::ZeroVector;   // the point pinged
	UPROPERTY() FString What;                       // "Bay 2: exhaust", "Over here"
	UPROPERTY() bool bBox = false;                  // a thing: the box round it (else a spot: a ring)
	UPROPERTY() FVector BoxCentre = FVector::ZeroVector;
	UPROPERTY() FVector BoxExtent = FVector::ZeroVector;   // half sizes, along the box's own axes
	UPROPERTY() FQuat BoxRot = FQuat::Identity;
	// Filled in by the host: who pinged, in their colour.
	UPROPERTY() FString Name;
	UPROPERTY() FColor Colour = FColor(255, 160, 50);
	UPROPERTY() int32 PlayerId = -1;
};

namespace TorquePing
{
constexpr double Life = 6.0;          // seconds a ping shows (ping_marker.gd LIFE)
constexpr double ReachCm = 6000.0;    // how far you can ping (walker.gd PING_REACH, 60 m)

// The box round a component (its own axes), grown 2 cm as Torque Shed's.
void BoxOf(const UPrimitiveComponent* C, FTorquePingNet& P);
// A box lying along the world's axes (a place in the shed, a loose part's meshes together).
void BoxOf(const FBox& Box, FTorquePingNet& P);
// The box round a whole actor (another rider's bike, a walker), along its own axes.
void BoxOfActor(const AActor* A, FTorquePingNet& P);

// Another player (on foot or on a bike) within a body's width of the line from Start along Dir,
// nearer than MaxCm: named ("Sam's bike", "Sam") and boxed. False if none.
bool PlayerOnLine(const APawn* Me, const FVector& Start, const FVector& Dir, double MaxCm, FTorquePingNet& Out, double& OutCm);

// A ping from the middle of PC's view: another player, or the spot the view meets (ignoring
// Ignore, your own bike). "" when there's nothing within reach; else what was pinged (sent).
FString FromView(APlayerController* PC, const AActor* Ignore);

// Sends a ping out to the crew (through the host).
void Send(APlayerController* PC, const FTorquePingNet& P);

// A ping as this machine shows it: what it is, and when it came.
struct FShown
{
	FTorquePingNet P;
	double Born = 0.0;
};
// Draws the pings on the canvas over the world, from the view at Eye looking along Ahead.
void Draw(UCanvas* Canvas, const TArray<FShown>& Pings, double Now, const FVector& Eye, const FVector& Ahead);
} // namespace TorquePing
