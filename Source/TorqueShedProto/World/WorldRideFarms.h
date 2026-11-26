// [everyday kits stage 4] Ride UK's farmyards: the farm kit, twelve original designs of UK farm building modelled
// by script in Blender (Tools/blender_farm_kit.py: steel portal stores, a cattle shed, an implement shed, a straw
// barn, a Dutch barn, a lean-to; Dales and northern stone barns, a brick threshing barn, an eastern black barn, a tin
// shed) and cut into pieces in the warehouse kit's format (Tools/blender_farm_kit_export.py: Data/RideUK/farm_kit.bin
// and farm_kit.json). A farm building the map draws (barn, farm_auxiliary, cowshed, stable, sty, or a big shed in a
// farmyard) gets the design that suits it, by what it is, its size and the place's own walls (stone country's barns
// stone, brick country's brick or black boarded), laid bay by bay on its box by the warehouse kit's BuildWarehouse.
#pragma once

#include <memory>
#include <string>

namespace TorqueWorld
{
struct WarehouseKit;

// Reads the farm kit (as SetWarehouseKit); kept for every ride after; false (and none: the drawn barns stay) if
// either isn't one.
bool SetFarmKit(const std::string& Json, const std::string& Bin);
std::shared_ptr<const WarehouseKit> FarmKitNow();

// The farm kit's class for a building, or "" for one it doesn't take: Kind the map's building tag, Baked the
// baker's kind (0 house, 1 shed, 2 barn, 5 industrial...), Long by Short its box (m), InYard whether it stands in a
// mapped farmyard, Brick its share of brick barns (0 in stone country; the lowlands' 0.65 for the black boarded ones'
// 0.35, the fens' 0.4), Seed in [0, 1) its own.
const char* FarmClassOf(const std::string& Kind, int Baked, double Long, double Short, bool InYard, double Brick, double Seed);
} // namespace TorqueWorld
