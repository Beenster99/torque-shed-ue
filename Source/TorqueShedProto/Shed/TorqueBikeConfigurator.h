// The New bike configurator (Torque Shed's Bike shop, "New: configure your own": bike_shop.gd and
// bike_shop_tab.gd), as the parts computer's third aisle, "New bike". Pick a style, then its parts
// slot by slot from the ones that suit it (or anything that fits: the ones that won't go on greyed
// out with why, the ones that need fabrication marked), its extras (a front mudguard, panniers, a
// top box, a sat nav, a toolbox), and each part's colour and paint finish. The bike turns in the
// shop's 3D studio beside the list (ATorqueShopPreview: drag it, zoom, stop it turning), with what
// it is, what it's laid out to and the fabrication the works will do, and Order. It comes new,
// built in those colours and ready to ride, into the first free bay (an OrderBike request carrying
// Fit/BikeShop's order text, so the host builds it and everyone in co-op sees it).
//
// The rules are Fit/BikeShop's (checked against the Godot game by SimTest/shop_test); this is the
// page, drawn on the HUD's canvas like the rest of the parts computer and worked with the mouse
// (or the pad: the D-pad picks a row, Cross goes to its next part, or orders).
#pragma once

#include "CoreMinimal.h"

#include <string>

class AHUD;
namespace TorqueFit { namespace Shop { struct Order; } }
class ATorqueShopPreview;
class UCanvas;
class UTorqueBuildSubsystem;
struct FTorqueShopFinish;

class FTorqueBikeConfigurator
{
public:
	// Opened (the aisle picked): the bike being configured is kept from last time; a cafe racer to start.
	void Open();
	// The page in Area (below the parts computer's aisles). S: the page's scale (1 at 1080p).
	void Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const FBox2D& Area, float S, const FVector2D& Mouse,
		const FString& Status, bool bStatusOk);
	// A left click: true with OutOrder set (Fit/BikeShop's order text) when it asks for the bike.
	bool Click(const FVector2D& Mouse, FString& OutOrder);
	bool Scroll(const FVector2D& Mouse, int32 Dir);
	// The pad: RowStep moves down the rows, bChoose puts the row's next part on (or orders).
	bool Pad(int32 RowStep, bool bChoose, FString& OutOrder);
	// Esc with a list or the colour picker open closes that (true); else the computer closes.
	bool Back();
	bool PopupOpen() const { return Popup != EPopup::None; }

	// For the test steps: start Style's bike and put these parts on (slot=part), give it a colour.
	FString TestSetup(const FString& StyleId, const TArray<FString>& Swaps);
	FString OrderText() const;
	FString Describe() const;

private:
	// The bike being configured: its style and parts (fitting's, in the .cpp), the colours picked
	// on it and how each is finished, and its paint (the tank's colour).
	struct FBikeState;
	mutable TSharedPtr<FBikeState> St;
	FBikeState& B() const;
	TMap<FString, FLinearColor> Colours;   // a slot, or a group ("wheels", "mudguards") -> colour
	TMap<FString, FName> Finishes;         // ... -> gloss, matte, metallic, chrome, patina (left out: as it comes)
	FLinearColor Paint = FLinearColor(0.78f, 0.09f, 0.08f);

	// Each configurator slot's choices on it now (BikeShop.options with BikeRig.part_fit), kept
	// until the bike changes.
	struct FChoice { FString Id, Label, Tip; bool bOff = false; };
	struct FSlotRow { FString Slot; TArray<FChoice> Choices; int32 Picked = 0; };
	TArray<FSlotRow> SlotRows;
	TArray<FString> About;    // the lines under the picture
	bool bDirty = true;

	// The page's rows (for the pad and the scroll), the list's scroll (px) and what the last
	// drawing put where.
	enum class EHit : uint8 { None, Style, Part, Colour, Finish, Reset, Order, PopupRow, PopupSwatch, PopupHue, PopupSv, PopupShade };
	struct FHit { FBox2D Box; EHit What = EHit::None; int32 Arg = 0; };
	TArray<FHit> Hits;
	FBox2D ListBox = FBox2D(ForceInit);
	float ListScroll = 0.0f, ListHeight = 0.0f, RowH = 30.0f;
	int32 Focus = 0;           // the pad's row: 0 the style, 1.. the slots, then Order
	TWeakObjectPtr<ATorqueShopPreview> Preview;

	// The list open over the page (a part's, the style's, a finish's) or the colour picker.
	enum class EPopup : uint8 { None, Style, Part, Finish, Colour };
	EPopup Popup = EPopup::None;
	FString PopupKey;          // the slot (or "style")
	FBox2D PopupAnchor = FBox2D(ForceInit);
	int32 PopupScroll = 0;
	float PopupHue = 0.0f, PopupSat = 1.0f, PopupVal = 1.0f;
	FLinearColor PopupStart = FLinearColor::White;

	void NewStyle(const std::string& NewStyleId);
	void SetPart(const FString& Slot, const std::string& Id);
	void Relist();
	FLinearColor StylePaint(const std::string& StyleId) const;
	FLinearColor ColourOf(const FString& Slot) const;
	FLinearColor FactoryColour(const FString& Slot) const;
	bool Changed(const FString& Slot) const;
	bool TakesColour(const FString& Slot) const;
	void SetColour(const FString& Key, const FLinearColor& C);
	void SetFinish(const FString& Key, FName Finish);
	void ResetColour(const FString& Slot);
	TorqueFit::Shop::Order MakeOrder() const;
	void ShowOnPreview(AHUD* Hud);
	void OpenPopup(EPopup Kind, const FString& Key, const FBox2D& Anchor);
	void ClosePopup();
	int32 PopupCount() const;
	bool ClickPopup(const FVector2D& Mouse);
};
