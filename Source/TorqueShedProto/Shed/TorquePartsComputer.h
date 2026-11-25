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
	enum class EKind : uint8 { None, Close, OrderBike, OrderPart, BuyPaint, OrderNewBike, OrderKit };
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
	// The preview shows the row picked now (Draw does this each frame; a test step reads it before the next).
	void SyncPreview(AHUD* Hud) { ShowPicked(Hud); }
	FTorqueComputerAction Pad(int32 RowStep, int32 KindStep, bool bChoose);
	// For the test steps: pick a kind ("bikes", "paint:bases", "paint:tints" or a part kind) and a model.
	bool PickKind(FName Kind);
	bool PickPart(FName Part);
	// The bike type filter over the part lists ("cafe", "bobber"...; "all" or "" for every type).
	// False if there's no such type.
	bool PickBikeType(const FString& Type);
	// The New bike aisle (the configurator): opened on it, and the configurator itself for the tests.
	void OpenNewBike();
	FTorqueBikeConfigurator& NewBike() { return Configurator; }
	// The New bike's brand name being typed (the keys go into it, not the computer's).
	bool Typing() const { return Section == ESection::NewBike && Configurator.Typing(); }
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
	enum class EHit : uint8 { Close, OrderBike, Section, Kind, Row, Order, Style, Type, EngineFilter, Bay, ListFilter };
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

	// The bike type the part lists are cut to (a layout style id; NAME_None: every type). Parts made
	// for any bike stay in; those that prefer the type come first.
	FName TypeFilter;
	bool Filtered() const;       // the kind showing is a part kind (the filter applies to it)
	void StepType(int32 Dir);
	// The engine list's filters and order (beenster after 156: "a sort order and filter for type, size,
	// manufacturer"): a layout ("" every one), a size band (0 any), a maker ("" every one), and the sort
	// (0 layout, 1 smallest first, 2 biggest first, 3 maker). StepEngine: 0/1 layout back/on, 2 size,
	// 3 maker, 4 sort.
	FString EngineLayoutFilter, EngineMakerFilter;
	int32 EngineSize = 0, EngineSort = 0;
	void StepEngine(int32 What);
public:
	// For the test robot: "layout=Inline four", "size=2", "maker=Kazeda", "sort=1" ("" or "all" clears).
	bool SetEngineFilter(const FString& Key, const FString& Value);
	// [shop bay] The bay the Bike aisle shops for (0 .. 3; -1 any bike), for the test robot ("bay=2" is bay 2,
	// index 1). False for a bay that isn't one.
	bool SetShopBay(const UTorqueBuildSubsystem* Shed, int32 Bay);
private:
	// [shop bay] beenster, Nov 20: "when you are shopping for parts you should nominate the bay number at the
	// top. The frame you buy then defines the engines, the engines you buy then defines some of the engine
	// parts and so on... the same with the wheels, tyres". The bay picked over the lists (-1 any bike; -2 not
	// picked yet: the first bay with a bike in it, when the page first draws), and what fitting says of the
	// kind's parts on that bay's bike (UTorqueBuildSubsystem::ShopFit): those that won't go on are left out.
	// Looked at again when the bike changes (checked twice a second while the page is up), never per row.
	int32 ShopBay = -2;
	TWeakObjectPtr<const UTorqueBuildSubsystem> ShedSeen;
	uint32 BikeSig = 0;
	double SigCheckAt = 0.0;
	bool bBayJudged = false;          // the list is cut to the bay's bike
	bool bStandJudged = false;        // ... or, with no bay's bike to go by, its engine parts to the engine on the stand
	int32 BayHidden = 0;              // how many of the kind were left out
	TMap<FName, FString> BayWhyNot;
	TMap<FName, int32> PartSides;     // wheels and tyres: 1 front, 2 rear, 3 either
	FName BayEngine;                  // the loose engine (on the stand) the engine parts went by as well as the frame
	uint32 BaySignature() const;
	void PickBay(int32 Bay);
	// Front and rear tyres (and wheels) are their own lists (beenster, Nov 20: "Its also not that obvious what
	// the front and rear tyres are"): which side of the bike a part goes on (1 front, 2 rear, 3 either, 0 none).
	int32 SideOf(FName Part) const;

	// [shop filters] (beenster after 171: "more filters for long lists... front, back, finish, spoked type") Over a
	// long part list, a row of buttons that each step on through what the list holds: which side (wheels and tyres:
	// it swaps to the other side's list), the style or type ("Type" on wheels: cast, wire spoked, billet), the
	// finish, the maker, and the order (0 the shop's, 1 cheapest, 2 dearest, 3 lightest). A filter shows only when
	// the list holds two or more of it. Cleared when another kind of part is picked.
	TMap<FString, FString> ListPicks;            // facet -> value picked ("style" -> "cast"); none: any
	TMap<FString, TArray<FString>> ListChoices;  // facet -> the values the list holds (before these filters)
	TArray<FString> ListChips;                   // the buttons drawn, in order ("side", "style", "finish", "maker", "sort")
	int32 ListSort = 0;
	FName ListKindSeen;                          // the base kind the picks are for
	int32 ListUnfiltered = 0;                    // how many the list held before these filters
	void StepListFilter(int32 Chip);
public:
	// For the test robot: "side=rear", "style=cast", "finish=chrome", "maker=BSX", "sort=1" ("" or "all" clears).
	bool SetListFilter(const FString& Key, const FString& Value);
private:

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
