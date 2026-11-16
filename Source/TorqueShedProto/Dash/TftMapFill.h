// Fills the sat nav's map (TftMap.h) from what the game knows of the land round a ride: a Region's
// roads by class and name, its buildings, rivers, woods and water, the places' names and the fuel
// and cafes. Plain C++ (the game does it on a worker each time the ride's area grows).
#pragma once

#include "TftMap.h"

namespace TorqueWorld { class Region; }

namespace TorqueTft
{
void FillMap(MapData& Out, const TorqueWorld::Region& Land);
// The kind of line a road of this class is drawn as ("motorway", "primary", "track"...).
RoadKind RoadKindOf(const std::string& Class);
} // namespace TorqueTft
