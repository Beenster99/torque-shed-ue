// [everyday kits stage 6] Ride UK's village set: the schools' kit (Tools/blender_village_kit.py: a Victorian board
// school in brick or stone, a post-war school, a new primary school, a timber classroom block; our own designs) cut
// into pieces in the warehouse kit's format (Data/RideUK/village_kit.bin and village_kit.json) and laid on the map's
// schools by the warehouse kit's BuildWarehouse (RidePlan::BuildingsTile). The village set's props (playgrounds,
// pitches, courts, greens) are RidePlan::VillageProps, World/WorldRideVillage.cpp.
#pragma once

#include <memory>
#include <string>

namespace TorqueWorld
{
struct WarehouseKit;

// Reads the village kit (as SetFarmKit); kept for every ride after; false (and none: the drawn schools stay) if
// either isn't one.
bool SetVillageKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const WarehouseKit> VillageKitNow();

// The village kit's class for a building, or "" for one it doesn't take: Kind the map's building tag, Long by Short
// its box (m), Floors its floors, Stone whether the place builds in stone, Seed in [0, 1) its own.
const char* VillageClassOf(const std::string& Kind, double Long, double Short, double Floors, bool Stone, double Seed);
} // namespace TorqueWorld
