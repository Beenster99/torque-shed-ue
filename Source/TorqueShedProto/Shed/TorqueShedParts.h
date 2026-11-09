// What the shed knows about parts, ported from Torque Shed's parts_db.gd: the bike's slots (the
// same 54 names as the Godot game), the kind of part each takes, what each bolts onto, which may
// be left empty, which you crouch to reach, and the parts catalogue (Data/Parts/parts.json: names,
// colours, materials and bins). Plus the bridge to the one bike model we have so far, the Speed
// Twin: which of its model parts belong to which slot, and which of its fit options is which
// catalogue part (so a part from the catalogue shows as that option when the model has it).
#pragma once

#include "CoreMinimal.h"

struct FTorqueResolvedPart;

namespace TorqueShedParts
{
// --- Slots (PartsDB.SLOTS and friends) --------------------------------------------------------
const TArray<FName>& Slots();          // every slot, bike and engine
const TArray<FName>& BikeSlots();      // the slots on the bike itself
const TArray<FName>& EngineSlots();    // the slots on an engine block (they travel with it)
bool IsEngineSlot(FName Slot);
bool IsServiceSlot(FName Slot);        // engine parts you can change with the engine in the frame
bool IsLowSlot(FName Slot);            // crouch to reach it while it's on the bike
bool IsOptional(FName Slot);           // a bike is complete without it
bool MayBeEmpty(FName Slot);           // optional, or the frame's own pegs or swingarm
FName KindOf(FName Slot);              // the kind of part the slot takes ("front_wheel" -> "wheel")
const TArray<FName>& MountsOf(FName Slot);   // what it bolts onto
// Slots that bolt onto this one (which come off first). Engine parts ride along with the block.
TArray<FName> Dependents(FName Slot);
// Every slot that takes a kind of part.
TArray<FName> SlotsForKind(FName Kind);
// "Front brake caliper", "Rear tyre", "Spark plugs".
FString SlotName(FName Slot);
FString KindName(FName Kind);
// The tyre on each wheel slot (the tyre comes off with its wheel).
FName TyreOf(FName WheelSlot);   // NAME_None for any other slot
bool IsTyreSlot(FName Slot);

// --- The catalogue (parts.json) ---------------------------------------------------------------
bool IsPart(FName Part);              // the catalogue has it, or it's one of the Speed Twin's own
FName PartKind(FName Part);
FString PartName(FName Part);
FString PartDescription(FName Part);
// A number from the part's parts.json entry ("mass", "torque_mult"; an array's length as
// "<field>_count"), or Default (a Speed Twin part, or a field it hasn't got).
double PartNumber(FName Part, FName Field, double Default);
// The field's mean over every part of the kind (a Speed Twin part's mass), or Default.
double KindAverage(FName Kind, FName Field, double Default);
FLinearColor DefaultColour(FName Part);
// Bike types (Torque Shed's styles: "cafe", "bobber", "adventure"...; layout.json style_names has
// their names). A part's preferred type is the style pack it came in, else the first of its
// "styles"; NAME_None when it has none (brakes, gearboxes, plugs...: they go on any bike).
FName PreferredStyle(FName Part);
// Every type it suits, the preferred one first (empty: any bike).
TArray<FName> PartStyles(FName Part);
// It suits a bike of this type (true for a part made for any bike, or for Style none).
bool SuitsStyle(FName Part, FName Style);
// The catalogue's parts of a kind, in its order.
TArray<FName> PartsOfKind(FName Kind);
// Kinds that bolt on (no fluids), in the game's order.
const TArray<FName>& Kinds();
// Extra mounts a part needs as well as its slot's (fairing-mount mirrors): parts_db part_mounts.
TArray<FName> PartMounts(FName Slot, FName Part);
// Which bin it goes in: "metal", "plastic" or "general"; and what it's made of in words.
FName BinFor(FName Part);
FString MaterialName(FName Part);
// The three bins by the roller door.
const TArray<FName>& Bins();
FString BinName(FName Bin);

// --- [fluids] Fluids and what holds them (PartsDB FLUID_KINDS, FLUID_HOLDERS) ---------------------
// Fluids come in cans and bottles you carry and pour ("petrol", "oil", "brake_fluid", "chain_lube":
// parts.json kinds of their own); they don't bolt on anywhere. A tank holds petrol, an engine its
// oil, a master cylinder brake fluid.
const TArray<FName>& FluidKinds();
bool IsFluid(FName Part);              // a can or bottle of fluid
FName FluidKind(FName Part);           // the fluid it holds (a container its own kind), NAME_None for none
FName FluidGrade(FName Part);          // the grade a container holds: its own id, or a big can's "grade"
float FluidCapacity(FName Part);       // litres: a tank's capacity, an engine's sump, a container's label
bool ComesEmpty(FName Part);           // an empty jerry can, the drain pan, the bleed bottle
FString ContainerName(FName Part);     // "Jerry can", "Oil bottle", "Drain pan"
FLinearColor LiquidColour(FName Part); // the colour of the fluid itself (parts.json "liquid")
FString FluidName(FName Kind);         // "Petrol", "Oil", "Brake fluid", "Chain lube"
constexpr float MasterFluid = 0.25f;   // a master cylinder's reservoir and line (PartsDB.MASTER_FLUID)

// --- The Speed Twin model -----------------------------------------------------------------------
// The Speed Twin's own parts are items like any other, with the part id "speed_twin_<slot>"
// (they aren't in the catalogue): "speed_twin_front_wheel".
extern const TCHAR* const StockModel;      // "speed_twin_1200", the model they belong to
FName StockPart(FName Slot);
bool IsStock(FName Part);
FName StockSlot(FName Part);               // the slot a Speed Twin part came off
// The slots the Speed Twin comes with something in.
const TArray<FName>& StockSlots();
// The slot a part of the drawn model belongs to (NAME_None: part of the frame, always drawn).
FName SlotOfModelPart(const FTorqueResolvedPart& P);
FName SlotOfModelPart(FName ModelPart);   // stock model parts only
// The Speed Twin fit slot and option that shows a catalogue part, if the model has one.
bool ModelOption(FName Part, FName Kind, FName& OutFitSlot, FName& OutOption);
}
