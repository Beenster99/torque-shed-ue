// The mixing bench's card, as Torque Shed's PaintBench draws it (paint_bench.gd): stepped up to the
// bench, the view drops to the scale and a card down the right side runs it, with the mouse.
//   Mix       the tin on the scale: its reading (Tare), the mix beside the colour to match and how
//             close it is, how well it's stirred, the rack's tints (pick one, then hold the left
//             mouse on it or in the view and drag down to tip the bottle), add base, a test card,
//             the shaker, tip it away, and a name to save it under.
//   Recipes   the swatch wall's cards: one on the bench (its weights to pour to), or matched by eye.
//   Supplies  the bases and tints in stock, restocked.
// The pad works it too (the D-pad goes down the card, L1 / R1 the pages, Cross pours or presses).
//
// Drawn on the HUD's canvas (ATorqueHUD asks the walker); the walker reads the mouse and the keys
// and does what the card says (ATorqueWalker's bench mode, TorqueWalkerBench.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueBuildSubsystem.h"

class AHUD;
class UCanvas;

// A line on the card that does something: a shed request (PaintPour: a tint to pour).
struct FTorqueBenchRow
{
	ETorqueShedAct Kind = ETorqueShedAct::Count;
	int32 Item = 0, Colour = 0;
	FString Text;
};

// What the walker's doing at the bench, for drawing it before the host hears of it.
struct FTorqueBenchState
{
	int32 Page = 0;               // 0 Mix, 1 Recipes, 2 Supplies
	int32 Focus = 0;              // the row picked (the pad's, or the tint clicked)
	int32 Pouring = INDEX_NONE;   // the tint being poured
	float Tilt = 0.0f, PourUnsent = 0.0f, StirUnsent = 0.0f;
	FString Name;                 // what the next save is called
	bool bNaming = false;         // typing it
	FString Note;
	bool bNoteOk = true;
};

// What a click on the card asks for.
struct FTorqueBenchClick
{
	enum class EKind : uint8 { None, Close, Page, Row, Eye, Name };
	EKind Kind = EKind::None;
	int32 Arg = 0;                // the page, or the row
};

class FTorquePaintBenchPanel
{
public:
	static constexpr int32 Pages = 3;
	static const TCHAR* PageName(int32 Page);
	void Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const TArray<FTorqueBenchRow>& Rows,
		const FTorqueBenchState& State, const FVector2D& Mouse);
	FTorqueBenchClick Click(const FVector2D& Mouse) const;
	void Scroll(const FVector2D& Mouse, int32 Dir);
	bool Over(const FVector2D& Mouse) const { return Card.bIsValid && Card.IsInside(Mouse); }
	// Keep the picked row in view (the pad moving down the card).
	void Show(int32 Row) { ShowRow = Row; }

private:
	struct FHit { FBox2D Box; FTorqueBenchClick Click; };
	TArray<FHit> Hits;
	FBox2D Card = FBox2D(ForceInit), ListBox = FBox2D(ForceInit);
	int32 ListScroll = 0, ListShown = 1, ListRows = 0, ShowRow = INDEX_NONE;
};
