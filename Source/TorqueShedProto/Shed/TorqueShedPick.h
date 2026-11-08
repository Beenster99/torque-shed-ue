// Pointing at a part by its real shape. The bike's meshes are imported without collision, and a
// part's box can be far bigger than the part: the Speed Twin's twin silencers and headers are one
// mesh each, so their box fills the middle of the bike, and the swingarm, shock or frame inside it
// would win the walker's look. So each mesh's own triangles (read once from its .glb in
// Data/Bike/SpeedTwin, the file the level script imported it from) are dropped into a grid of
// small cells, and a look hits the part only where it goes through a cell the part fills.
#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

namespace TorqueShedPick
{
enum class EHit : uint8
{
	NoShape,   // no .glb for this mesh (a grey-box shape, a placeholder): use its box
	Miss,      // the line misses the part itself
	Hit,
};

// Where the line from Start to End first meets the part (0..1 along it).
EHit Trace(const UStaticMeshComponent* C, const FVector& Start, const FVector& End, double& OutTime);

// A point on the part's surface (world), the one furthest along Side: something to look at from
// that side (for tests). False without a shape.
bool SurfacePoint(const UStaticMeshComponent* C, const FVector& Side, FVector& Out);
}
