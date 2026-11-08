// The Unreal side of the fitment system: which parts a bike can wear, whether one fits, and where
// every model part ends up once a build is assembled from typed sockets (Fit/Fitment.h does the
// work in plain C++, shared with the standalone FitTest).
//
// Data: Data/Bike/<Bike>/fitment.json (sockets, datums, options; Tools/make_speedtwin_fit.py),
// Data/Fitment/interfaces.json (the interface types), Data/Parts/parts.json (the Godot catalogue,
// Tools/import_parts.py) and Data/Bike/<Bike>/ue_assets.json (mesh and material paths, written by
// Tools/ue_build_level.py). Without ue_assets.json (no import yet) ResolveModel returns nothing.
//
// Plain structs with Unreal types, no USTRUCT: the shed's UTorqueBuildSubsystem holds an
// FTorqueBuild; ATorqueBikePawn::BuildModel and the shed's bike both call ResolveModel.
#pragma once

#include "CoreMinimal.h"

// Geometry mode for one fit unit: only the movement its socket allows (Slide along it, turn about it,
// or a Hole position), plus a free override for anything the sockets can't express.
struct FTorqueFitAdjust
{
	double Slide = 0.0;          // metres along the socket's travel (clamped to what it allows)
	double TurnDeg = 0.0;        // degrees about the socket's axis (clamped)
	int32 Hole = -1;             // a hole position, for hole-pattern sockets (-1: the stock hole)
	bool bFree = false;          // the free override, saved per bike
	FTransform Free;             // pawn-local cm, applied after the socket placement
};

// What a bike is wearing.
struct FTorqueBuild
{
	FName Bike = TEXT("speed_twin_1200");
	TMap<FName, FName> Fitted;              // slot ("handlebars", "exhaust") -> option id; missing = stock
	TMap<FName, FTorqueFitAdjust> Adjust;   // fit unit ("bars", "headlight", "seat") -> geometry mode
};

struct FTorqueFitMesh
{
	FName ModelPart;             // glb / mesh name, e.g. "silencers" or "opt_exhaust_peashooter_twins"
	FString Mesh, Material;      // asset paths, from ue_assets.json (empty before an import)
	FName Group;                 // body, steer, front_lower, front_wheel, swingarm, rear_wheel
};

struct FTorqueFitOption
{
	FName PartId;                // option id within its slot ("stock", "clubman_bar", ...)
	FText Name;
	FName CatalogueKind, CatalogueId;      // its parts.json entry (mass, price, sound), if any
	bool bStock = false;
	TArray<FName> ReplacesModelParts;      // stock model parts it takes off
	TArray<FTorqueFitMesh> Meshes;         // what it puts on
};

struct FTorqueResolvedPart
{
	FName Name;                  // model part (mesh) name
	FName Slot;                  // the slot its unit belongs to ("handlebars" for the bar), NAME_None if none
	FName Unit;                  // the fit unit it belongs to (what geometry mode moves)
	FString Mesh, Material;
	FName Group;
	FTransform Delta;            // pawn-local cm, applied after the importer fix: PartFix * Delta
};

struct FTorqueFitClash
{
	FName A, B;                  // fit units
	FString Case;                // "rest", "lock_left", "front_bump_lock_right", "rear_bump", ...
	double DepthMm = 0.0;
};

namespace TorqueFit
{
	// Slots a bike has options for, e.g. "handlebars", "exhaust".
	TArray<FName> Slots(FName Bike);
	// The options for a slot, stock first.
	TArray<FTorqueFitOption> Options(FName Bike, FName Slot);
	// Whether PartId can go in Slot on this build (sockets match, sizes agree); WhyNot says why not.
	bool CanFit(const FTorqueBuild& Build, FName Slot, FName PartId, FText& WhyNot);
	// Every model part of the assembled build, stock ones included. Clashes (if asked) are the
	// clearance checks: full lock both ways, full compression at each end, and both together.
	bool ResolveModel(const FTorqueBuild& Build, TArray<FTorqueResolvedPart>& Out, TArray<FTorqueFitClash>* Clashes = nullptr);

	// Geometry mode, player fitting: a free drag of a unit (pawn-local cm, and degrees about the
	// socket) turned into the movement its socket allows, snapped to holes; returns the new adjust.
	FTorqueFitAdjust ConstrainDrag(const FTorqueBuild& Build, FName Unit, const FVector& DragCm, double TurnDeg);
	// Geometry mode, author fitting: save the build's current placement of Unit back into the part's
	// data (Data/Bike/<Bike>/fitment_fixes.json), so every build with that part gets it.
	bool AuthorFix(const FTorqueBuild& Build, FName Unit, FString& Error);

	// A build from the command line, for testing swaps without the shed:
	// -TorqueBuild=handlebars:clubman_bar,exhaust:cafe_reverse_cones
	FTorqueBuild BuildFromCommandLine();
	// Forget the loaded data (after an author fix, or new data on disk).
	void Reload();
}
