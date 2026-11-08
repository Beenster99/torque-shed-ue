// The paint mixing bay's maths, as Torque Shed's PaintMix (paint_mix.gd): what a tin of paint looks
// like from the base and the tints poured into it, how close it is to a colour it should match, the
// short recipe codes, and the coats each finish needs.
//
// A tin is a base (white, black, metallic silver, or clear lacquer for flattener) plus up to
// MaxTints tints by weight. The colour is worked out like real pigment: each tint pulls the mix
// towards its own colour by its weight times its tinting strength, as a weighted geometric mean of
// the reflectances (blue and yellow make green, and a strong tint like phthalo blue goes a long
// way), so the base sets how light the colour can go. Aluminium tints make it metallic.
//
// Torque Shed unlocks more as you level up (candy, pearl, flake, chrome and chameleon, the effects
// rack). The UE shed has no levels yet, so this is the bay a new garage starts with: the four
// level 1 bases, the stock rack's sixteen tints, and solid, metallic and lacquer.
//
// Colours are Torque Shed's sRGB numbers held in FLinearColor as they are (as Paints() does), so
// the model draws them the same as the spray can's.
#pragma once

#include "CoreMinimal.h"

namespace TorquePaintMix
{
constexpr float TinGrams = 1000.0f;      // a 1 litre tin's worth of base
constexpr float MaxTinGrams = 2000.0f;   // with base added to bring an over-poured mix back
constexpr int32 MaxTints = 5;
constexpr float AddBaseGrams = 250.0f;   // each "add base" pours in this much more
constexpr float GramsPerLitre = 1000.0f;
constexpr float BottleGrams = 250.0f;    // a tint bottle as bought
constexpr float CupLitres = 0.6f;        // the spray gun's cup
constexpr float TestCardGrams = 15.0f;   // paint a test card takes from the tin
constexpr float GoodMatch = 0.95f;       // a target is matched at this much
constexpr float EffectMiss = 0.12f;      // off the match for the wrong effect (solid for a metallic)

enum class ETintKind : uint8 { Tint, Alu, Sparkle, Flattener };

struct FBase
{
	const TCHAR* Code;
	const TCHAR* Name;
	FLinearColor Colour;
	bool bOpaque;
	float Metal;
};

struct FTint
{
	const TCHAR* Code;
	const TCHAR* Name;
	FLinearColor Colour;
	float K;          // tinting strength
	ETintKind Kind;
};

// BASES (W, B, S, L) and the stock rack's TINTS, in the rack's order.
const TArray<FBase>& Bases();
const TArray<FTint>& Tints();
int32 BaseIndex(const FString& Code);    // INDEX_NONE if there's none
int32 TintIndex(const FString& Code);
inline bool IsLacquerBase(int32 Base) { return Base == 3; }

struct FTintGrams
{
	int32 Tint = 0;      // an index into Tints()
	float Grams = 0.0f;
};

// What's in a tin (paint_mix.gd's tin Dictionary: base, base_g, tints, stir).
struct FTinMix
{
	int32 Base = 0;                 // an index into Bases()
	float BaseGrams = TinGrams;
	TArray<FTintGrams> Tints;       // at most MaxTints, in the order poured
	float Stir = 1.0f;              // 0 .. 1: an even mix at 1
	float GramsOf(int32 Tint) const;
	float TintGrams() const;
	float TotalGrams() const { return BaseGrams + TintGrams(); }
	float Litres() const { return TotalGrams() / GramsPerLitre; }
	void Add(int32 Tint, float Grams);
};

FTinMix NewTin(int32 Base);

// The paint a tin makes (paint_mix.gd's spec), as kept on a part once it's sprayed.
struct FPaintSpec
{
	FString Name;               // the tin's name ("Midnight Blue")
	FString Code;               // its full code ("W-PB40-LY12.5:M")
	FName Effect;               // "solid", "metallic", "lacquer"
	FLinearColor Colour = FLinearColor(0.9f, 0.9f, 0.9f);
	float Metal = 0.0f, Rough = 0.12f, Flake = 0.0f, Flat = 0.0f, Stir = 1.0f;
	int32 Need = 2;             // coats it needs
	bool IsLacquer() const { return Effect == FName(TEXT("lacquer")); }
};

FPaintSpec Spec(const FTinMix& Tin, const FString& Name = FString());

// "" or why a tint can't go in: not a sixth tint, flattener only in lacquer (and lacquer takes
// nothing else), not into a full tin.
FString PourProblem(const FTinMix& Tin, int32 Tint);

// A recipe as a short code: the base letter, a "+250" for added base, each tint's code and grams
// (to a tenth), and with the effect's letter: "W-PB40-LY12.5:M". ParseCode reads one back (false
// if it doesn't read: the effect letter is worked out again).
FString CodeOf(const FTinMix& Tin);
FString FullCode(const FTinMix& Tin);
bool ParseCode(const FString& Code, FTinMix& Out);
FString EffectName(FName Effect);
FString EffectLetter(FName Effect);
FString SheenName(float Flat);
FString GramsText(float Grams);

// Grams a second out of a bottle tipped to Tilt (0 upright .. 1 straight down): a drip when it's
// just tipped, a glug at full tilt. A turn of the stick stirs StirPerTurn of an even mix (level 1),
// and pouring Grams into Total grams un-mixes it.
float PourRate(float Tilt);
constexpr float StirPerTurn = 0.12f * 1.04f;
float Unstir(float Stir, float Grams, float Total);

// What a part looks like after Coats coats of S over Under (a first coat of two lets a bit of what
// was under it show through), and how well the coats so far suit the paint (thin short of Need).
FLinearColor SprayedColour(const FPaintSpec& S, int32 Coats, const FLinearColor& Under);
float CoatsFactor(const FPaintSpec& S, int32 Coats);
// The finish word kept as a part's Finish for a sprayed spec ("gloss" for solid).
FName FinishOf(const FPaintSpec& S);

// How far apart two colours look (CIE76 delta E), and how close Have is to Want, 0 .. 1 (less for
// the wrong effect), and "93.4%".
float DeltaE(const FLinearColor& A, const FLinearColor& B);
float MatchOf(const FLinearColor& Have, const FLinearColor& Want, FName HaveEffect = NAME_None, FName WantEffect = NAME_None);
FString MatchText(float Match);
// How close the tints poured are to a recipe's, 0 .. 1 (by weight, over the recipe's tints).
float RecipeAccuracy(const FTinMix& Tin, const FTinMix& Recipe);

// Litres a coat takes on a part of Kind (a litre does about a tank, the guards and the side panels
// in two coats).
float LitresFor(FName Kind);
}
