// The shed's parts computer, as Torque Shed draws it (parts_computer.gd): a page over the screen you
// work with the mouse. The aisles along the top (Bike, Shed), the kinds of part under their headings
// down the left, the models of the kind picked, and the one picked: what it is, what it fits, how it
// arrives, and the Order button. Orders go to the shed (UTorqueBuildSubsystem) as the walker's
// requests, so they're shared in co-op like every other order.
//
// Drawn on the HUD's canvas (ATorqueHUD asks the walker); the walker reads the mouse and the keys and
// hands them here. The pad works it too: the D-pad picks a model, L1 / R1 the kind, Cross orders.
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueBikeConfigurator.h"

class AHUD;
class ATorqueShopPreview;
class UCanvas;
class UFont;
class UTorqueBuildSubsystem;

// What a click (or the pad) asks for.
struct FTorqueComputerAction
{
	enum class EKind : uint8 { None, Close, OrderBike, OrderPart, BuyPaint, OrderNewBike };
	EKind Kind = EKind::None;
	FName Part;                 // OrderPart
	int32 Stock = INDEX_NONE;   // BuyPaint: the bay's stock index (FTorquePaintBay::BaseStock / TintStock)
	int32 Count = 1;
	FString Order;              // OrderNewBike: the configured bike (Fit/BikeShop's order text)
};

class FTorquePartsComputer
{
public:
	// Opened at the computer: as it was left (the aisle, kind and model), the lists rebuilt.
	void Open();
	// The page, sized to the screen. Mouse: the cursor, for the hover. Status: what the shed last said.
	void Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const FVector2D& Mouse, const FString& Status, bool bStatusOk);
	FTorqueComputerAction Click(const FVector2D& Mouse);
	void Scroll(const FVector2D& Mouse, int32 Dir);   // the wheel over a list: Dir +1 down, -1 up (over the preview: zoom)
	// The 3D preview's mouse (ATorqueShopPreview): every frame the page is up, where the mouse is and
	// whether the left button is held (dragging turns it); a right-click on it stops or starts it turning.
	void Hold(const FVector2D& Mouse, bool bLeftDown);
	void RightClick(const FVector2D& Mouse);
	FString PreviewShowing() const;   // for the test log
	FTorqueComputerAction Pad(int32 RowStep, int32 KindStep, bool bChoose);
	// For the test steps: pick a kind ("bikes", "paint:bases", "paint:tints" or a part kind) and a model.
	bool PickKind(FName Kind);
	bool PickPart(FName Part);
	// The New bike aisle (the configurator): opened on it, and the configurator itself for the tests.
	void OpenNewBike();
	FTorqueBikeConfigurator& NewBike() { return Configurator; }
	// Esc: closes a list open on the page first (true); false when the computer should close.
	bool Back();
	FString Describe() const;    // "Bike aisle, Exhaust: <part>" for the test log
	// For the computer's monitor in the shed (ATorqueShedScreens): the part kind showing (NAME_None
	// for project bikes or the paint supplies), its parts ("bike" for a project bike), the one picked.
	FName ScreenKind() const;
	TArray<FName> ScreenRows() const;
	int32 PickedRow() const { return Row; }

private:
	enum class ESection : uint8 { Bike, Shed, NewBike };
	struct FKindRow { FName Kind; FString Heading; };   // a heading row has no Kind
	struct FRow { FName Part; int32 Stock = INDEX_NONE; };
	enum class EHit : uint8 { Close, OrderBike, Section, Kind, Row, Order, Style };
	struct FHit { FBox2D Box; EHit What; int32 Arg; };

	ESection Section = ESection::Bike;
	FName Kind = FName(TEXT("bikes"));
	int32 Row = 0;
	int32 KindScroll = 0, RowScroll = 0;
	TArray<FKindRow> KindRows;   // the aisle's kinds under their headings
	TArray<FRow> Rows;           // the models of the kind picked
	TArray<FHit> Hits;           // where the last drawing put its buttons
	FBox2D KindBox = FBox2D(ForceInit), RowBox = FBox2D(ForceInit);
	int32 KindsShown = 1, RowsShown = 1;
	// The picked item turning in 3D at the top of the detail column (the local player's studio).
	TWeakObjectPtr<ATorqueShopPreview> Preview;
	FTorqueBikeConfigurator Configurator;   // the New bike aisle

	int32 BikeStyle = 0;         // the project bike's style shown (the one ordered is any of them)
	FString StyleShown;          // ... and its reference bike's parts, looked up when it changes
	TMap<FString, FString> StyleParts;
	void ShowPicked(AHUD* Hud);

	void Relist();               // KindRows and Rows for the aisle and kind
	void PickSection(ESection S);
	void PickKindAt(int32 Index);
	FTorqueComputerAction OrderPicked() const;
	const FHit* HitAt(const FVector2D& Mouse) const;
};
