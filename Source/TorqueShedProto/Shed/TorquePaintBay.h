// The paint mixing bay's shared state, as Torque Shed's PaintBay (paint_bay.gd) keeps it: what's in
// stock (the bases in litres, the tints in grams), every tin (on the scale, the tin shelf, in the
// shaker or in someone's hands), the recipes on the swatch wall, the bench (who's at it, the tin on
// the scale, the bottle being poured and how far it's tipped, the scale's tare, the recipe card and
// the colour to match), the shaker, the last test card, and each player's spray gun cup.
//
// The host owns it with the rest of the shed: UTorqueBuildSubsystem's Paint* requests are checked
// and done in TorquePaintBay.cpp, and it goes to everyone in the shed's bytes. ATorquePaintBayRoom
// draws it; the walker's bench mode runs the bench.
//
// The room is through the paint shop's east wall (paint_bay.gd ROOM, in the shed's cm): x 1430 ..
// 1700, y -520 .. 520. Keep in step with ue_build_shed.py paint_bay.
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorquePaintMix.h"

enum class ETorqueTinAt : uint8
{
	Bench,    // on the scale
	Shelf,    // on the tin shelf, in slot Ref
	Held,     // in player Ref's hands
	Shaker,   // in the shaker
};

struct FTorqueTin
{
	int32 Id = 0;
	FString Name;                   // "" until it's saved at the bench
	TorquePaintMix::FTinMix Mix;
	ETorqueTinAt At = ETorqueTinAt::Bench;
	int32 Ref = 0;
	float Litres = 1.0f;            // what's left in it (a test card and the gun's cup take some)
	FString By;                     // who mixed it
	TorquePaintMix::FPaintSpec Spec() const { return TorquePaintMix::Spec(Mix, Name); }
};

// A recipe card on the swatch wall.
struct FTorquePaintRecipe
{
	int32 Id = 0;
	FString Name, Code, By;
	FLinearColor Colour = FLinearColor::White;
	FName Effect;
};

// A player's spray gun cup: the paint in it and how much.
struct FTorquePaintCup
{
	TorquePaintMix::FPaintSpec Spec;
	float Litres = 0.0f;
};

struct FTorquePaintBay
{
	// The stock: StockIndex(base) litres of each base, StockIndex(tint) grams of each tint.
	static int32 BaseStock(int32 Base) { return Base; }
	static int32 TintStock(int32 Tint) { return TorquePaintMix::Bases().Num() + Tint; }
	TArray<float> Stock;
	TMap<int32, FTorqueTin> Tins;
	TArray<FTorquePaintRecipe> Recipes;   // the oldest first
	int32 NextId = 1;
	int32 Mixed = 0;                      // tins saved (the next is named "Mix <Mixed + 1>")
	// The bench.
	int32 BenchBy = 0;                    // the player at it (0: no one)
	int32 BenchTin = 0;                   // the tin on the scale (0: none)
	int32 PourTint = INDEX_NONE;          // the bottle being poured, held over the tin
	float Tilt = 0.0f;
	float TareGrams = 0.0f;               // the scale zeroed at this much, for tin TareTin
	int32 TareTin = 0;
	int32 CardRecipe = 0;                 // the recipe card on the bench (its tints to pour), 0 for none
	TorquePaintMix::FTinMix CardMix;
	bool bTarget = false;                 // a colour to match
	FString TargetName;
	FLinearColor TargetColour = FLinearColor::White;
	FName TargetEffect;
	// The shaker, and the last test card on the spray-out stand.
	int32 ShakerTin = 0;
	float ShakerLeft = 0.0f;
	bool bTest = false;
	TorquePaintMix::FPaintSpec Test;
	FString TestBy;
	int32 TestN = 0;
	// Each player's cup and the tin in their hands.
	TMap<int32, FTorquePaintCup> Cups;
	TMap<int32, int32> Held;

	static constexpr float StarterTint = 120.0f;
	static constexpr float MaxStock = 100000.0f;
	static constexpr int32 MaxRecipes = 80;
	static constexpr int32 MaxTins = 24;
	static constexpr int32 ShelfSlots = 12;
	static constexpr int32 MaxSwatches = 32;
	static constexpr float ShakeTime = 20.0f;
	// A new garage's bay (paint_bay.gd STARTER): 3 litres of white, 2 of black, silver and lacquer,
	// and 120 g of each stock tint.
	static FTorquePaintBay Starter();

	float StockOf(int32 Index) const { return Stock.IsValidIndex(Index) ? Stock[Index] : 0.0f; }
	const FTorqueTin* BenchTinPtr() const { return Tins.Find(BenchTin); }
	FTorqueTin* BenchTinPtr() { return Tins.Find(BenchTin); }
	const FTorqueTin* HeldTin(int32 Peer) const;
	int32 TinInSlot(int32 Slot) const;     // the tin's id, or 0
	int32 FreeSlot() const;                // -1 when the shelf's full
	// The scale's reading: what's on it, less the tare if it was zeroed for this tin.
	float ScaleGrams() const;
	bool Tared() const;
	// The swatch wall's cards, newest first (at most MaxSwatches).
	TArray<const FTorquePaintRecipe*> Swatches() const;
	const FTorquePaintRecipe* Recipe(int32 Id) const;
	// "Tin: Mix 3 (metallic), 0.8 l".
	FString TinText(const FTorqueTin& Tin) const;
	// "White base: 3.0 l", "Phthalo blue: 120 g".
	FString StockText(int32 Index) const;
	static FString StockName(int32 Index);

	// The bay as bytes, for the save and for co-op. bNet: with what only matters while playing
	// (the cups, tins in hands, who's at the bench). False if it didn't read.
	bool Serialize(FArchive& Ar, bool bNet);
	// After a save is opened: nobody holds anything or stands at the bench, the cups are clean, a
	// tin left in the shaker comes out mixed.
	void AfterLoad();
	// A player gone: their tin back on the shelf, and away from the bench.
	void PutBack(int32 Peer);
	// The host's clock: the shaker. True when a tin came out of it (the shed has changed).
	bool Tick(float Dt);
	// A short text of the whole bay, for test logs.
	FString Summary() const;
};

// Where things are in the room (paint_bay.gd, in the shed's cm).
namespace TorquePaintBayLayout
{
const FVector ScaleAt(1662.0, -40.0, 92.0);          // the scale's pan on the bench
const FVector ShakerAt(1662.0, 125.0, 92.0);
const FVector BenchStand(1560.0, -40.0, 0.0);        // where you stand at the bench
const FVector BenchEye(1595.0, -40.0, 155.0);        // and the bench view
const FVector TestCardAt(1500.0, -405.0, 140.0);     // the spray-out stand in the daylight booth
inline FVector RackAt(int32 Tint)                    // a tint's bottle on the rack (its foot)
{
	const int32 Row = Tint / 8, Col = Tint % 8;
	return FVector(1686.0, -205.0 + 25.0 * Col, Row == 0 ? 132.0 : 172.0);
}
inline FVector SlotAt(int32 Slot)                    // a tin on the tin shelf (its foot)
{
	static const double Z[] = {45.0, 95.0, 145.0};
	const int32 Row = FMath::Clamp(Slot / 4, 0, 2), Col = Slot % 4;
	return FVector(1672.0, 260.0 + (Col - 1.5) * 30.0, Z[Row]);
}
inline FVector SwatchAt(int32 I)                     // a swatch card on the wall (its middle)
{
	const int32 Col = I % 8, Row = I / 8;
	return FVector(1433.0, -360.0 + 78.0 * Col, 235.0 - 46.0 * Row);
}
inline bool InRoom(const FVector& At) { return At.X > 1420.0 && At.X < 1710.0 && At.Y > -530.0 && At.Y < 530.0; }
}
