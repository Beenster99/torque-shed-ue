#include "Shed/TorquePartsComputer.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorquePaintMix.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShopPreview.h"
#include "Coop/TorqueUi.h"
#include "Fit/BikeLayout.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

namespace TorquePartsComputerImpl
{
const FName Bikes(TEXT("bikes"));
const FName PaintBases(TEXT("paint:bases"));
const FName PaintTints(TEXT("paint:tints"));

// parts_computer.gd GROUPS: within an aisle the kinds come under headings, in this order; a kind
// not listed goes under "Other" at the end.
struct FGroup { const TCHAR* Heading; TArray<const TCHAR*> Kinds; };
const TArray<FGroup>& Groups()
{
	static const TArray<FGroup> G = {
		{TEXT("Engine and drivetrain"), {TEXT("engine"), TEXT("head"), TEXT("fuel"), TEXT("air_filter"), TEXT("oil_filter"), TEXT("spark_plugs"),
			TEXT("cooling"), TEXT("gearbox"), TEXT("exhaust"), TEXT("silencers"), TEXT("final_drive"), TEXT("chain")}},
		{TEXT("Chassis"), {TEXT("frame"), TEXT("fork"), TEXT("shock"), TEXT("swingarm"), TEXT("steering_damper"), TEXT("handlebars"),
			TEXT("grips"), TEXT("bar_ends"), TEXT("levers"), TEXT("switchgear"), TEXT("lever_guards"), TEXT("rearsets"), TEXT("heel_plates"),
			TEXT("foot_controls"), TEXT("guards"), TEXT("side_stand"), TEXT("centre_stand")}},
		{TEXT("Wheels and brakes"), {TEXT("wheel"), TEXT("tyre"), TEXT("disc"), TEXT("caliper"), TEXT("pads"), TEXT("master")}},
		{TEXT("Body and lights"), {TEXT("tank"), TEXT("saddle"), TEXT("front_mudguard"), TEXT("tail"), TEXT("fairing"), TEXT("mirrors"),
			TEXT("dashboard"), TEXT("headlight"), TEXT("fog_lights"), TEXT("horn"), TEXT("indicators"), TEXT("tail_light"), TEXT("plate_hanger"),
			TEXT("filler_cap"), TEXT("side_panels"), TEXT("number_boards")}},
		{TEXT("Luggage and adventure kit"), {TEXT("panniers"), TEXT("top_box"), TEXT("tank_bag"), TEXT("aux_tank"), TEXT("satnav")}},
	};
	return G;
}

// The game's UI kit (Coop/TorqueUi.h, the menus' look): its palette, panels and crisp Slate text.
const FLinearColor Page(TorqueUi::Panel.R, TorqueUi::Panel.G, TorqueUi::Panel.B, 0.94f);
const FLinearColor Column = TorqueUi::PanelLight;
const FLinearColor Button(1.0f, 1.0f, 1.0f, 0.07f);
const FLinearColor ButtonHover(1.0f, 1.0f, 1.0f, 0.15f);
const FLinearColor Picked(TorqueUi::Accent.R, TorqueUi::Accent.G, TorqueUi::Accent.B, 0.88f);
const FLinearColor PickedHover = TorqueUi::Accent;
const FLinearColor Accent = TorqueUi::Accent;
const FLinearColor Text = TorqueUi::Ink;
const FLinearColor Dim = TorqueUi::Dim;
const FLinearColor Ink(0.03f, 0.03f, 0.035f, 1.0f);      // text on the amber
const FLinearColor InkDim(0.10f, 0.08f, 0.06f, 1.0f);
const FLinearColor Good(0.45f, 0.88f, 0.52f, 1.0f);
const FLinearColor Bad = TorqueUi::Warn;

// Text in pixel sizes, through the kit (Slate's font at the size it's shown).
struct FPen
{
	UCanvas* C = nullptr;
	float Width(const FString& S, float Px) const { return (float)TorqueUi::Measure(S, Px).X; }
	void Text(float X, float Y, const FString& S, float Px, const FLinearColor& Colour, bool bBold = false) const
	{
		TorqueUi::Text(C, S, X, Y, Px, Colour, bBold, TorqueUi::EAlign::Left, Colour.A > 0.0f && Colour != Ink && Colour != InkDim);
	}
	// Cut to fit Width, with "..." if it was cut.
	FString Fit(const FString& S, float Px, float MaxW) const
	{
		if (Width(S, Px) <= MaxW) return S;
		FString Out = S;
		while (Out.Len() > 1 && Width(Out + TEXT("..."), Px) > MaxW) Out.LeftChopInline(1);
		return Out.TrimEnd() + TEXT("...");
	}
	// Word-wrapped lines (a new line in S starts a new one; an empty one is a gap).
	TArray<FString> Wrap(const FString& S, float Px, float MaxW) const
	{
		TArray<FString> Out, Paras;
		S.ParseIntoArray(Paras, TEXT("\n"), false);
		for (const FString& P : Paras)
		{
			const TArray<FString> Lines = TorqueUi::WrapLines(P, Px, MaxW);
			if (Lines.Num() == 0) Out.Add(FString());
			else Out.Append(Lines);
		}
		return Out;
	}
};

// The shed's kit and the yard's are under kinds named "kit:<kind>" (TorqueFurniture's kinds).
const TCHAR* KitPrefix = TEXT("kit:");
bool IsKit(FName K) { return K.ToString().StartsWith(KitPrefix); }
FName KitKind(FName K) { return FName(*K.ToString().RightChop(4)); }
FName KitOf(FName Kind) { return FName(*(FString(KitPrefix) + Kind.ToString())); }

// [shop bay] Front and rear lists of wheels and tyres (beenster, Nov 20: "Its also not that obvious what the front
// and rear tyres are"): "tyre:front", "wheel:rear"... A list's side (1 front, 2 rear, 0 not a sided list), the kind
// it lists, and the kinds that get two lists.
int32 SideList(FName K)
{
	const FString S = K.ToString();
	return S.EndsWith(TEXT(":front")) ? 1 : (S.EndsWith(TEXT(":rear")) ? 2 : 0);
}
FName BaseKind(FName K)
{
	if (SideList(K) == 0) return K;
	FString Left, Right;
	K.ToString().Split(TEXT(":"), &Left, &Right);
	return FName(*Left);
}
bool Sided(FName K) { return K == FName(TEXT("wheel")) || K == FName(TEXT("tyre")); }
FName SideKind(FName K, int32 Side) { return FName(*(K.ToString() + (Side == 1 ? TEXT(":front") : TEXT(":rear")))); }
const TCHAR* SideText(int32 Sides)
{
	switch (Sides)
	{
	case 1: return TEXT("Front");
	case 2: return TEXT("Rear");
	case 3: return TEXT("Front or rear");
	default: return TEXT("No wheel takes it");
	}
}

FString KindTitle(FName K)
{
	if (const int32 Side = SideList(K))
		return (Side == 1 ? FString(TEXT("Front ")) : FString(TEXT("Rear "))) + TorqueShedParts::KindName(BaseKind(K)).ToLower();
	if (IsKit(K))
	{
		const FName Kind = KitKind(K);
		for (const TPair<FName, FString>& KV : TorqueFurniture::Kinds())
			if (KV.Key == Kind) return KV.Value;
		return Kind.ToString();
	}
	if (K == Bikes) return TEXT("Project bikes");
	if (K == PaintBases) return TEXT("Paint bases");
	if (K == PaintTints) return TEXT("Tints");
	return TorqueShedParts::KindName(K);
}

// "cafe_racer" -> "Cafe racer".
FString StyleTitle(const FString& Id)
{
	FString Out = Id.Replace(TEXT("_"), TEXT(" "));
	if (Out.Len() > 0) Out[0] = FChar::ToUpper(Out[0]);
	return Out;
}

// The bike types in Torque Shed's order (layout.json styles), and a type's name ("Cafe racer").
TArray<FName> BikeTypes()
{
	TArray<FName> Out;
	if (const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout())
		for (const std::string& St : Lay->ShopStyles()) Out.Add(FName(UTF8_TO_TCHAR(St.c_str())));
	return Out;
}

FString TypeName(FName Type)
{
	if (Type.IsNone()) return TEXT("Any bike");
	if (const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout())
		return FString(UTF8_TO_TCHAR(Lay->StyleName(TCHAR_TO_UTF8(*Type.ToString().ToLower())).c_str()));
	return StyleTitle(Type.ToString());
}

// The bike types a part suits that the game still has (beenster, build 155: "Some parts in the shop
// still refer to bike types we have retired"): the one it's made for first. Empty: any bike.
TArray<FName> KeptStyles(FName Part)
{
	const TArray<FName> Kept = BikeTypes();
	TArray<FName> Out;
	for (const FName& T : TorqueShedParts::PartStyles(Part))
		if (Kept.Num() == 0 || Kept.Contains(T)) Out.AddUnique(T);
	return Out;
}

// Whether a kind of part is needed for the bike to ride (beenster, build 155: "In the shop we should see a
// marker to indicate whether this part is essential to ride or optional"): 1 essential, 0 optional, -1 not
// a bike part (kit, paint, fluids). Essential: a slot the build screen counts as missing when it's empty.
int32 Essential(FName Kind)
{
	const TArray<FName> Slots = TorqueShedParts::SlotsForKind(Kind);
	if (Slots.Num() == 0) return -1;
	for (const FName& Slot : Slots)
		if (TorqueShedParts::IsEngineSlot(Slot) || !TorqueShedParts::MayBeEmpty(Slot)) return 1;
	return 0;
}

// The kinds a shop list shows (beenster after 156: the new sprockets belong in the Sprockets list, not
// a "Rear sprocket" one of their own): the gearing sprockets (final_drive) then the sprocket looks.
TArray<FName> ListedKinds(FName Kind)
{
	if (Kind == FName(TEXT("final_drive"))) return {Kind, FName(TEXT("sprocket"))};
	if (SideList(Kind) != 0) return {BaseKind(Kind)};
	return {Kind};
}

// [engines] An engine's capacity band for the shop's size filter: 1 up to 499 cc, 2 500-899, 3 900-1199,
// 4 1200 and up; and its maker (the first word of a V2 bike's part name: "BSX", "Kazeda").
int32 SizeBand(FName Engine)
{
	const double Cc = TorqueShedParts::PartNumber(Engine, TEXT("displacement"), 0.0);
	return Cc < 500.0 ? 1 : (Cc < 900.0 ? 2 : (Cc < 1200.0 ? 3 : 4));
}
const TCHAR* SizeBandName(int32 Band)
{
	switch (Band)
	{
	case 1: return TEXT("up to 499 cc");
	case 2: return TEXT("500-899 cc");
	case 3: return TEXT("900-1199 cc");
	case 4: return TEXT("1200 cc and up");
	default: return TEXT("any size");
	}
}
FString Maker(FName Part)
{
	if (TorqueShedParts::V2Slot(Part).IsNone()) return FString();
	FString Name = TorqueShedParts::PartName(Part), Left, Right;
	return Name.Split(TEXT(" "), &Left, &Right) ? Left : Name;
}
const TCHAR* EngineSortName(int32 Sort)
{
	switch (Sort)
	{
	case 1: return TEXT("smallest first");
	case 2: return TEXT("biggest first");
	case 3: return TEXT("by maker");
	default: return TEXT("by layout");
	}
}

FName KeptBest(FName Part)
{
	const TArray<FName> Types = KeptStyles(Part);
	return Types.Num() > 0 ? Types[0] : NAME_None;
}

FString Join(const TArray<FString>& Parts)
{
	return FString::Join(Parts, TEXT(", "));
}
} // namespace TorquePartsComputerImpl

namespace TPCI = TorquePartsComputerImpl;

void FTorquePartsComputer::Open()
{
	Relist();
	Hits.Reset();
}

void FTorquePartsComputer::PickSection(ESection S)
{
	if (S == Section) return;
	Section = S;
	if (S == ESection::NewBike) Configurator.Open();
	Kind = S == ESection::Bike ? TPCI::Bikes : TPCI::PaintBases;
	Row = 0;
	KindScroll = RowScroll = 0;
	Relist();
}

void FTorquePartsComputer::Relist()
{
	KindRows.Reset();
	if (Section == ESection::NewBike)
	{
		// The configurator's page instead of the lists (TorqueBikeConfigurator).
		Rows.Reset();
		return;
	}
	if (Section == ESection::Bike)
	{
		// What the shop sells for a bike: kinds with parts that go on a bike, under parts_computer.gd's headings.
		TArray<FName> Sold;
		// beenster (Nov 17): toolboxes are gone from the game, so the shop doesn't sell them.
		for (const FName& K : TorqueShedParts::Kinds())
			if (K != FName(TEXT("toolbox")) && TorqueShedParts::PartsOfKind(K).Num() > 0 && TorqueShedParts::SlotsForKind(K).Num() > 0) Sold.Add(K);
		KindRows.Add({NAME_None, TEXT("Bikes")});
		KindRows.Add({TPCI::Bikes, FString()});
		TSet<FName> Shown;
		for (const TPCI::FGroup& G : TPCI::Groups())
		{
			bool bHeading = false;
			for (const TCHAR* K : G.Kinds)
			{
				const FName Name(K);
				if (!Sold.Contains(Name)) continue;
				if (!bHeading) KindRows.Add({NAME_None, G.Heading});
				bHeading = true;
				if (TPCI::Sided(Name))
				{
					// [shop bay] Front and rear their own lists.
					KindRows.Add({TPCI::SideKind(Name, 1), FString()});
					KindRows.Add({TPCI::SideKind(Name, 2), FString()});
				}
				else KindRows.Add({Name, FString()});
				Shown.Add(Name);
			}
		}
		bool bOther = false;
		for (const FName& K : Sold)
		{
			if (Shown.Contains(K)) continue;
			if (!bOther) KindRows.Add({NAME_None, TEXT("Other")});
			bOther = true;
			KindRows.Add({K, FString()});
		}
	}
	else
	{
		KindRows.Add({NAME_None, TEXT("Paint mixing bay")});
		KindRows.Add({TPCI::PaintBases, FString()});
		KindRows.Add({TPCI::PaintTints, FString()});
		// [fluids] Petrol, oil, brake fluid and chain lube, and the cans, the drain pan and the bleed bottle.
		bool bFluidHead = false;
		for (const FName& K : TorqueShedParts::FluidKinds())
		{
			if (TorqueShedParts::PartsOfKind(K).Num() == 0) continue;
			if (!bFluidHead) KindRows.Add({NAME_None, TEXT("Fluids")});
			bFluidHead = true;
			KindRows.Add({K, FString()});
		}
		// The shed's kit, its trinkets and the yard's kit (equipment.gd's catalogue, trinkets.gd).
		const TCHAR* Headings[] = {TEXT("Shed equipment"), TEXT("Trinkets"), TEXT("Yard kit")};
		for (int32 H = 0; H < 3; ++H)
		{
			bool bHeaded = false;
			for (const TPair<FName, FString>& KV : TorqueFurniture::Kinds())
			{
				TArray<FName> Sold = TorqueFurniture::SoldOfKind(KV.Key);
				if (Sold.Num() == 0) continue;
				const TorqueFurniture::FModel* M = TorqueFurniture::Find(Sold[0]);
				const int32 Group = !M ? 0 : (M->bYard ? 2 : (M->Kind == FName(TEXT("trinket")) ? 1 : 0));
				if (Group != H) continue;
				if (!bHeaded) KindRows.Add({NAME_None, Headings[H]});
				bHeaded = true;
				KindRows.Add({TPCI::KitOf(KV.Key), FString()});
			}
		}
	}
	bool bKnown = false;
	for (const FKindRow& K : KindRows) bKnown = bKnown || (!K.Kind.IsNone() && K.Kind == Kind);
	if (!bKnown)
		for (const FKindRow& K : KindRows)
			if (!K.Kind.IsNone()) { Kind = K.Kind; break; }

	Rows.Reset();
	if (Kind == TPCI::Bikes) Rows.Add({FName(TEXT("bike"))});
	else if (Kind == TPCI::PaintBases)
		for (int32 I = 0; I < TorquePaintMix::Bases().Num(); ++I) Rows.Add({NAME_None, FTorquePaintBay::BaseStock(I)});
	else if (Kind == TPCI::PaintTints)
		for (int32 I = 0; I < TorquePaintMix::Tints().Num(); ++I) Rows.Add({NAME_None, FTorquePaintBay::TintStock(I)});
	else if (TPCI::IsKit(Kind))
		for (const FName& M : TorqueFurniture::SoldOfKind(TPCI::KitKind(Kind))) Rows.Add({M});
	else if (Kind == FName(TEXT("engine")))
	{
		// Engines by their layout (beenster, Nov 12), not by bike type: singles, two-strokes, twins,
		// parallel twins, V-twins, boxers, triples, fours; each lot smallest first.
		// Then cut to the layout, size and maker picked, in the order picked (beenster after 156).
		TArray<FName> Engines = TorqueShedParts::PartsOfKind(Kind);
		Engines.RemoveAll([this](const FName& E) {
			if (!EngineLayoutFilter.IsEmpty() && TorqueShedParts::EngineLayout(E) != EngineLayoutFilter) return true;
			if (EngineSize != 0 && TPCI::SizeBand(E) != EngineSize) return true;
			return !EngineMakerFilter.IsEmpty() && TPCI::Maker(E) != EngineMakerFilter;
		});
		const int32 Order = EngineSort;
		Engines.StableSort([Order](const FName& EngA, const FName& EngB) {
			const double Ca = TorqueShedParts::PartNumber(EngA, TEXT("displacement"), 0.0), Cb = TorqueShedParts::PartNumber(EngB, TEXT("displacement"), 0.0);
			if (Order == 1 && Ca != Cb) return Ca < Cb;
			if (Order == 2 && Ca != Cb) return Ca > Cb;
			if (Order == 3 && TPCI::Maker(EngA) != TPCI::Maker(EngB)) return TPCI::Maker(EngA) < TPCI::Maker(EngB);
			const int32 La = TorqueShedParts::EngineLayoutOrder(EngA), Lb = TorqueShedParts::EngineLayoutOrder(EngB);
			if (La != Lb) return La < Lb;
			const FString Ta = TorqueShedParts::EngineLayout(EngA), Tb = TorqueShedParts::EngineLayout(EngB);
			if (Ta != Tb) return Ta < Tb;
			if (Ca != Cb) return Ca < Cb;
			return TorqueShedParts::PartName(EngA) < TorqueShedParts::PartName(EngB);
		});
		for (const FName& P : Engines) Rows.Add({P});
	}
	else if (TypeFilter.IsNone() || !Filtered())
		for (const FName& K : TPCI::ListedKinds(Kind))
			for (const FName& P : TorqueShedParts::PartsOfKind(K)) Rows.Add({P});
	else
	{
		// Cut to the bike type: the ones made for it first, then the ones that suit it too (and any bike's).
		TArray<FName> Also;
		for (const FName& K : TPCI::ListedKinds(Kind))
		for (const FName& P : TorqueShedParts::PartsOfKind(K))
		{
			if (!TorqueShedParts::SuitsStyle(P, TypeFilter)) continue;
			if (TPCI::KeptBest(P) == TypeFilter) Rows.Add({P});
			else Also.Add(P);
		}
		for (const FName& P : Also) Rows.Add({P});
	}
	// [shop bay] Cut to the side the list is for, then to what goes on the bay's bike.
	bBayJudged = false;
	BayHidden = 0;
	BayWhyNot.Reset();
	PartSides.Reset();
	if (Section == ESection::Bike && Kind != TPCI::Bikes)
	{
		if (const UTorqueBuildSubsystem* Shed = ShopBay >= 0 ? ShedSeen.Get() : nullptr)
		{
			TArray<FName> Parts;
			for (const FRow& R : Rows) Parts.Add(R.Part);
			bBayJudged = Shed->ShopFit(ShopBay, Parts, BayWhyNot, PartSides);
		}
		if (const int32 Side = TPCI::SideList(Kind))
			Rows.RemoveAll([this, Side](const FRow& R) {
				const int32 Sides = SideOf(R.Part);
				return Sides != 0 && (Sides & Side) == 0;
			});
		if (bBayJudged)
			BayHidden = Rows.RemoveAll([this](const FRow& R) { return BayWhyNot.Contains(R.Part); });
	}
	Row = Rows.Num() > 0 ? FMath::Clamp(Row, 0, Rows.Num() - 1) : 0;
}

int32 FTorquePartsComputer::SideOf(FName Part) const
{
	if (const int32* Known = PartSides.Find(Part)) return *Known;
	const FName K = TorqueShedParts::PartKind(Part);
	if (K == FName(TEXT("tyre"))) return UTorqueBuildSubsystem::TyreSides(Part);
	if (K == FName(TEXT("wheel")))
	{
		const FName Own = TorqueShedParts::V2Slot(Part);
		return Own == FName(TEXT("front_wheel")) ? 1 : (Own == FName(TEXT("rear_wheel")) ? 2 : 3);
	}
	return 3;
}

uint32 FTorquePartsComputer::BaySignature() const
{
	const UTorqueBuildSubsystem* Shed = ShedSeen.Get();
	if (!Shed || ShopBay < 0) return 0;
	if (!Shed->Exists(ShopBay)) return 1;
	TMap<FString, FString> Parts;
	if (!Shed->LayoutParts(ShopBay, Parts)) return 2;
	uint32 Sig = 3;
	for (const TPair<FString, FString>& KV : Parts) Sig += HashCombine(GetTypeHash(KV.Key), GetTypeHash(KV.Value));   // (any order)
	return Sig;
}

void FTorquePartsComputer::PickBay(int32 Bay)
{
	ShopBay = Bay;
	BikeSig = BaySignature();
	const FName Was = Rows.IsValidIndex(Row) ? Rows[Row].Part : NAME_None;
	Row = 0;
	RowScroll = 0;
	Relist();
	for (int32 I = 0; I < Rows.Num(); ++I)
		if (!Was.IsNone() && Rows[I].Part == Was) Row = I;
}

bool FTorquePartsComputer::SetShopBay(const UTorqueBuildSubsystem* Shed, int32 Bay)
{
	if (Shed) ShedSeen = Shed;
	if (Bay < -1 || Bay >= UTorqueBuildSubsystem::Bays) return false;
	PickBay(Bay);
	return true;
}

void FTorquePartsComputer::PickKindAt(int32 Index)
{
	if (!KindRows.IsValidIndex(Index) || KindRows[Index].Kind.IsNone()) return;
	if (KindRows[Index].Kind != Kind)
	{
		Kind = KindRows[Index].Kind;
		Row = 0;
		RowScroll = 0;
	}
	Relist();
}

bool FTorquePartsComputer::PickKind(FName K)
{
	for (ESection S : {ESection::Bike, ESection::Shed})
	{
		PickSection(S);
		for (int32 I = 0; I < KindRows.Num(); ++I)
			if (KindRows[I].Kind == K || (!KindRows[I].Kind.IsNone() && TPCI::ListedKinds(KindRows[I].Kind).Contains(K)))
			{
				PickKindAt(I);
				return true;
			}
	}
	return false;
}

bool FTorquePartsComputer::Filtered() const
{
	return Section == ESection::Bike && Kind != TPCI::Bikes && !TPCI::IsKit(Kind) && Kind != TPCI::PaintBases && Kind != TPCI::PaintTints &&
		Kind != FName(TEXT("engine"));   // (engines go by their layout, not the bike type)
}

void FTorquePartsComputer::StepEngine(int32 What)
{
	const TArray<FName> All = TorqueShedParts::PartsOfKind(FName(TEXT("engine")));
	auto Round = [](TArray<FString> List, const FString& Now, int32 Dir) {
		List.Sort([](const FString& A, const FString& B) { return A < B; });
		List.Insert(FString(), 0);   // every one
		const int32 N = List.Num();
		const int32 At = FMath::Max(0, List.IndexOfByKey(Now));
		return List[((At + Dir) % N + N) % N];
	};
	if (What == 0 || What == 1)
	{
		// The layouts in the shop's order (singles first, fours last).
		TArray<FName> ByOrder = All;
		ByOrder.StableSort([](const FName& A, const FName& B) { return TorqueShedParts::EngineLayoutOrder(A) < TorqueShedParts::EngineLayoutOrder(B); });
		TArray<FString> Layouts;
		Layouts.Add(FString());
		for (const FName& E : ByOrder) Layouts.AddUnique(TorqueShedParts::EngineLayout(E));
		const int32 N = Layouts.Num();
		const int32 At = FMath::Max(0, Layouts.IndexOfByKey(EngineLayoutFilter));
		EngineLayoutFilter = Layouts[((At + (What == 0 ? -1 : 1)) % N + N) % N];
	}
	else if (What == 2) EngineSize = (EngineSize + 1) % 5;
	else if (What == 3)
	{
		TArray<FString> Makers;
		for (const FName& E : All)
			if (!TPCI::Maker(E).IsEmpty()) Makers.AddUnique(TPCI::Maker(E));
		EngineMakerFilter = Round(Makers, EngineMakerFilter, 1);
	}
	else if (What == 4) EngineSort = (EngineSort + 1) % 4;
	const FName Was = Rows.IsValidIndex(Row) ? Rows[Row].Part : NAME_None;
	Row = 0;
	RowScroll = 0;
	Relist();
	for (int32 I = 0; I < Rows.Num(); ++I)
		if (!Was.IsNone() && Rows[I].Part == Was) Row = I;
}

bool FTorquePartsComputer::SetEngineFilter(const FString& Key, const FString& Value)
{
	const bool bClear = Value.IsEmpty() || Value.Equals(TEXT("all"), ESearchCase::IgnoreCase);
	if (Key == TEXT("layout")) EngineLayoutFilter = bClear ? FString() : Value;
	else if (Key == TEXT("size")) EngineSize = bClear ? 0 : FMath::Clamp(FCString::Atoi(*Value), 0, 4);
	else if (Key == TEXT("maker")) EngineMakerFilter = bClear ? FString() : Value;
	else if (Key == TEXT("sort")) EngineSort = bClear ? 0 : FMath::Clamp(FCString::Atoi(*Value), 0, 3);
	else return false;
	Row = 0;
	RowScroll = 0;
	Relist();
	return true;
}

void FTorquePartsComputer::StepType(int32 Dir)
{
	// Round the types: every type, then each of Torque Shed's styles in its order.
	const TArray<FName> Types = TPCI::BikeTypes();
	const int32 N = Types.Num() + 1;
	int32 At = TypeFilter.IsNone() ? 0 : Types.IndexOfByKey(TypeFilter) + 1;
	At = ((At + Dir) % N + N) % N;
	TypeFilter = At == 0 ? NAME_None : Types[At - 1];
	// The project bike shown in the type picked, too.
	if (!TypeFilter.IsNone())
	{
		const int32 Style = ATorqueShopPreview::BikeStyles().IndexOfByKey(TypeFilter.ToString());
		if (Style != INDEX_NONE) BikeStyle = Style;
	}
	const FName Was = Rows.IsValidIndex(Row) ? Rows[Row].Part : NAME_None;
	Row = 0;
	RowScroll = 0;
	Relist();
	// Keep the one picked if it's still listed.
	for (int32 I = 0; I < Rows.Num(); ++I)
		if (!Was.IsNone() && Rows[I].Part == Was) Row = I;
}

bool FTorquePartsComputer::PickBikeType(const FString& Type)
{
	FName Want;
	if (!Type.IsEmpty() && !Type.Equals(TEXT("all"), ESearchCase::IgnoreCase) && !Type.Equals(TEXT("any"), ESearchCase::IgnoreCase))
	{
		for (const FName& T : TPCI::BikeTypes())
			if (T.ToString().Equals(Type, ESearchCase::IgnoreCase) || TPCI::TypeName(T).Equals(Type, ESearchCase::IgnoreCase)) Want = T;
		if (Want.IsNone()) return false;
	}
	if (Want == TypeFilter) return true;
	const TArray<FName> Types = TPCI::BikeTypes();
	const int32 From = TypeFilter.IsNone() ? 0 : Types.IndexOfByKey(TypeFilter) + 1;
	const int32 To = Want.IsNone() ? 0 : Types.IndexOfByKey(Want) + 1;
	StepType(To - From);
	return true;
}

bool FTorquePartsComputer::PickPart(FName Part)
{
	// A part the bike type filter hides: show every type again.
	if (!TypeFilter.IsNone() && Filtered() && !TorqueShedParts::SuitsStyle(Part, TypeFilter))
	{
		TypeFilter = NAME_None;
		Relist();
	}
	auto Find = [this, Part]() {
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].Part == Part || (Rows[I].Stock != INDEX_NONE && FTorquePaintBay::StockName(Rows[I].Stock).Equals(Part.ToString(), ESearchCase::IgnoreCase)))
			{
				Row = I;
				return true;
			}
		return false;
	};
	if (Find()) return true;
	// [shop bay] On the other side's list (a rear tyre asked for under "tyre"), or left out for the bay's bike.
	if (const int32 Side = TPCI::SideList(Kind))
	{
		const FName Was = Kind;
		Kind = TPCI::SideKind(TPCI::BaseKind(Kind), Side == 1 ? 2 : 1);
		Relist();
		if (Find()) return true;
		Kind = Was;
		Relist();
	}
	if (bBayJudged && BayWhyNot.Contains(Part))
	{
		PickBay(-1);
		return Find();
	}
	return false;
}

void FTorquePartsComputer::OpenNewBike()
{
	PickSection(ESection::NewBike);
}

bool FTorquePartsComputer::Back()
{
	return Section == ESection::NewBike && Configurator.Back();
}

FString FTorquePartsComputer::Describe() const
{
	if (Section == ESection::NewBike) return Configurator.Describe();
	FString Picked;
	if (Rows.IsValidIndex(Row))
		Picked = Rows[Row].Stock != INDEX_NONE ? FTorquePaintBay::StockName(Rows[Row].Stock) : Rows[Row].Part.ToString();
	FString Out = FString::Printf(TEXT("%s aisle, %s: %s"), Section == ESection::Bike ? TEXT("Bike") : TEXT("Shed"), *TPCI::KindTitle(Kind), *Picked);
	if (Section == ESection::Bike && Kind == FName(TEXT("engine")))
		Out += FString::Printf(TEXT(" (layout: %s, size: %s, maker: %s, %s, %d listed)"), EngineLayoutFilter.IsEmpty() ? TEXT("all") : *EngineLayoutFilter,
			TPCI::SizeBandName(EngineSize), EngineMakerFilter.IsEmpty() ? TEXT("all") : *EngineMakerFilter, TPCI::EngineSortName(EngineSort), Rows.Num());
	if (bBayJudged) Out += FString::Printf(TEXT(" (for bay %d's bike: %d listed, %d that don't fit left out)"), ShopBay + 1, Rows.Num(), BayHidden);
	else if (Section == ESection::Bike && ShopBay < 0) Out += TEXT(" (for any bike)");
	if (Filtered())
	{
		Out += FString::Printf(TEXT(" (bike type: %s, %d listed)"), TypeFilter.IsNone() ? TEXT("all") : *TPCI::TypeName(TypeFilter), Rows.Num());
		if (Rows.IsValidIndex(Row) && !Rows[Row].Part.IsNone())
			Out += TEXT(" [suits: ") + TPCI::TypeName(TPCI::KeptBest(Rows[Row].Part)) + TEXT("]");
	}
	return Out;
}

FName FTorquePartsComputer::ScreenKind() const
{
	return (Kind == TPCI::Bikes || Kind == TPCI::PaintBases || Kind == TPCI::PaintTints || TPCI::IsKit(Kind)) ? NAME_None : TPCI::BaseKind(Kind);
}

TArray<FName> FTorquePartsComputer::ScreenRows() const
{
	TArray<FName> Out;
	for (const FRow& R : Rows)
		if (R.Stock == INDEX_NONE) Out.Add(R.Part);
	return Out;
}

FTorqueComputerAction FTorquePartsComputer::OrderPicked() const
{
	FTorqueComputerAction A;
	if (!Rows.IsValidIndex(Row)) return A;
	const FRow& R = Rows[Row];
	if (R.Stock != INDEX_NONE)
	{
		A.Kind = FTorqueComputerAction::EKind::BuyPaint;
		A.Stock = R.Stock;
	}
	else if (R.Part == FName(TEXT("bike"))) A.Kind = FTorqueComputerAction::EKind::OrderBike;
	else if (TPCI::IsKit(Kind))
	{
		A.Kind = FTorqueComputerAction::EKind::OrderKit;
		A.Part = R.Part;
	}
	else
	{
		A.Kind = FTorqueComputerAction::EKind::OrderPart;
		A.Part = R.Part;
	}
	return A;
}

const FTorquePartsComputer::FHit* FTorquePartsComputer::HitAt(const FVector2D& Mouse) const
{
	for (const FHit& H : Hits)
		if (H.Box.IsInside(Mouse)) return &H;
	return nullptr;
}

FTorqueComputerAction FTorquePartsComputer::Click(const FVector2D& Mouse)
{
	FTorqueComputerAction A;
	const bool bNewBike = Section == ESection::NewBike;
	if (ATorqueShopPreview* P = Preview.Get())
		if (!(bNewBike && Configurator.PopupOpen()) && P->Press(Mouse)) return A;
	const FHit* H = HitAt(Mouse);
	// The New bike aisle: its page takes the click, but for the header and the aisles.
	if (bNewBike && (!H || (H->What != EHit::Close && H->What != EHit::Section && H->What != EHit::OrderBike)))
	{
		if (Configurator.Click(Mouse, A.Order)) A.Kind = FTorqueComputerAction::EKind::OrderNewBike;
		return A;
	}
	if (!H) return A;
	if (bNewBike) Configurator.Back();
	switch (H->What)
	{
	case EHit::Close: A.Kind = FTorqueComputerAction::EKind::Close; break;
	case EHit::OrderBike: A.Kind = FTorqueComputerAction::EKind::OrderBike; break;
	case EHit::Section: PickSection(H->Arg == 0 ? ESection::Bike : (H->Arg == 1 ? ESection::Shed : ESection::NewBike)); break;
	case EHit::Kind: PickKindAt(H->Arg); break;
	case EHit::Row: if (Rows.IsValidIndex(H->Arg)) Row = H->Arg; break;
	case EHit::Order: A = OrderPicked(); break;
	case EHit::Type: StepType(H->Arg); break;
	case EHit::EngineFilter: StepEngine(H->Arg); break;
	case EHit::Bay: PickBay(H->Arg); break;
	case EHit::Style:
	{
		const int32 N = ATorqueShopPreview::BikeStyles().Num();
		if (N > 0) BikeStyle = (BikeStyle + H->Arg + N) % N;
		break;
	}
	}
	return A;
}

void FTorquePartsComputer::Scroll(const FVector2D& Mouse, int32 Dir)
{
	if (Section == ESection::NewBike && Configurator.PopupOpen())
	{
		Configurator.Scroll(Mouse, Dir);
		return;
	}
	if (ATorqueShopPreview* P = Preview.Get())
		if (P->Wheel(Mouse, Dir)) return;
	if (Section == ESection::NewBike)
	{
		Configurator.Scroll(Mouse, Dir);
		return;
	}
	if (KindBox.bIsValid && KindBox.IsInside(Mouse))
		KindScroll = FMath::Clamp(KindScroll + Dir * 3, 0, FMath::Max(0, KindRows.Num() - KindsShown));
	else if (RowBox.bIsValid && RowBox.IsInside(Mouse))
		RowScroll = FMath::Clamp(RowScroll + Dir * 2, 0, FMath::Max(0, Rows.Num() - RowsShown));
}

void FTorquePartsComputer::Hold(const FVector2D& Mouse, bool bLeftDown)
{
	// (The New bike page's badge sliders are dragged too; it hands the rest to the preview.)
	if (Section == ESection::NewBike)
	{
		Configurator.Hold(Mouse, bLeftDown);
		return;
	}
	if (ATorqueShopPreview* P = Preview.Get()) P->Hold(Mouse, bLeftDown);
}

void FTorquePartsComputer::RightClick(const FVector2D& Mouse)
{
	if (ATorqueShopPreview* P = Preview.Get()) P->RightPress(Mouse);
}

FString FTorquePartsComputer::PreviewShowing() const
{
	const ATorqueShopPreview* P = Preview.Get();
	return P ? P->Showing() : FString(TEXT("no preview"));
}

void FTorquePartsComputer::ShowPicked(AHUD* Hud)
{
	ATorqueShopPreview* P = Preview.Get();
	if (!P && Hud)
	{
		P = ATorqueShopPreview::For(Hud->PlayerOwner);
		Preview = P;
	}
	if (!P) return;
	if (!Rows.IsValidIndex(Row))
	{
		P->ShowNothing();
		return;
	}
	const FRow& R = Rows[Row];
	if (R.Stock != INDEX_NONE) P->ShowPaint(R.Stock);
	else if (R.Part == FName(TEXT("bike")))
	{
		const TArray<FString> Styles = ATorqueShopPreview::BikeStyles();
		if (Styles.Num() == 0) P->ShowNothing();
		else
		{
			const FString& Style = Styles[FMath::Clamp(BikeStyle, 0, Styles.Num() - 1)];
			if (Style != StyleShown)
			{
				StyleShown = Style;
				StyleParts = ATorqueShopPreview::StyleBike(Style);
			}
			P->ShowBike(StyleParts);
		}
	}
	else if (TPCI::IsKit(Kind)) P->ShowKit(R.Part);
	else P->ShowPart(R.Part);
}

FTorqueComputerAction FTorquePartsComputer::Pad(int32 RowStep, int32 KindStep, bool bChoose)
{
	if (Section == ESection::NewBike)
	{
		// L1 / R1: back round the aisles; the D-pad and Cross work the configurator's rows.
		FTorqueComputerAction A;
		if (KindStep != 0)
		{
			Configurator.Back();
			PickSection(KindStep > 0 ? ESection::Bike : ESection::Shed);
			return A;
		}
		if (Configurator.Pad(RowStep, bChoose, A.Order)) A.Kind = FTorqueComputerAction::EKind::OrderNewBike;
		return A;
	}
	if (KindStep != 0)
	{
		// The next kind (over the headings), round into the other aisle at either end.
		int32 At = INDEX_NONE;
		for (int32 I = 0; I < KindRows.Num(); ++I)
			if (KindRows[I].Kind == Kind) At = I;
		int32 Next = At + KindStep;
		while (KindRows.IsValidIndex(Next) && KindRows[Next].Kind.IsNone()) Next += KindStep;
		if (KindRows.IsValidIndex(Next)) PickKindAt(Next);
		else PickSection(Section == ESection::Bike ? (KindStep > 0 ? ESection::Shed : ESection::NewBike) : (KindStep > 0 ? ESection::NewBike : ESection::Bike));
	}
	if (RowStep != 0 && Rows.Num() > 0)
	{
		Row = (Row + RowStep + Rows.Num()) % Rows.Num();
		if (Row < RowScroll) RowScroll = Row;
		if (Row >= RowScroll + RowsShown) RowScroll = Row - RowsShown + 1;
	}
	return bChoose ? OrderPicked() : FTorqueComputerAction();
}

void FTorquePartsComputer::Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const FVector2D& Mouse, const FString& Status, bool bStatusOk)
{
	Hits.Reset();
	if (!Hud || !Canvas || !GEngine) return;
	// [shop bay] The shed seen (the lists are cut to a bay's bike), the bay picked the first time, and the lists
	// again when the bay's bike has changed (a part fitted or taken off, by you or a mate).
	if (Shed != ShedSeen.Get())
	{
		ShedSeen = Shed;
		SigCheckAt = 0.0;
	}
	if (Shed && ShopBay == -2)
	{
		int32 First = -1;
		for (int32 B = 0; B < UTorqueBuildSubsystem::Bays && First < 0; ++B)
			if (Shed->Exists(B)) First = B;
		PickBay(First);
	}
	else if (Shed && Section == ESection::Bike && FPlatformTime::Seconds() >= SigCheckAt)
	{
		SigCheckAt = FPlatformTime::Seconds() + 0.5;
		const uint32 Sig = BaySignature();
		if (Sig != BikeSig) PickBay(ShopBay);
	}
	TPCI::FPen Pen;
	Pen.C = Canvas;
	const float S = TorqueUi::Scale(Canvas);   // Torque Shed's sizes at 1080p (the kit's scale, as the menus)
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C) { TorqueUi::Rect(Canvas, X, Y, W, H, C); };
	auto Over = [&Mouse](float X, float Y, float W, float H) { return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H; };
	auto AddHit = [this](float X, float Y, float W, float H, EHit What, int32 Arg) { Hits.Add({FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), What, Arg}); };
	// A button: its box, lit when the mouse is over it (or it's the one picked), its words centred.
	auto Btn = [&](float X, float Y, float W, float H, const FString& Label, float Px, bool bPicked, EHit What, int32 Arg)
	{
		const bool bOver = Over(X, Y, W, H);
		Box(X, Y, W, H, bPicked ? (bOver ? TPCI::PickedHover : TPCI::Picked) : (bOver ? TPCI::ButtonHover : TPCI::Button));
		const FString Fitted = Pen.Fit(Label, Px, W - 16.0f * S);
		Pen.Text(X + (W - Pen.Width(Fitted, Px)) * 0.5f, Y + (H - Px * 1.2f) * 0.5f, Fitted, Px, bPicked ? TPCI::Ink : TPCI::Text, bPicked);
		AddHit(X, Y, W, H, What, Arg);
	};

	// The shade over the shed, and the page (parts_computer.gd PANEL_MAX 1240 x 840, a margin round it).
	TorqueUi::Backdrop(Canvas, 0.6f);
	const float PW = FMath::Min(1240.0f * S, Canvas->ClipX - 80.0f * S), PH = FMath::Min(840.0f * S, Canvas->ClipY - 60.0f * S);
	const float X0 = (Canvas->ClipX - PW) * 0.5f, Y0 = (Canvas->ClipY - PH) * 0.5f;
	const float Margin = 18.0f * S;
	Box(X0, Y0, PW, PH, TPCI::Page);
	Box(X0, Y0, PW, 3.0f * S, FLinearColor(TPCI::Accent));

	// The header: the title, and on the right the project bike order and Close.
	float Y = Y0 + Margin;
	Pen.Text(X0 + Margin, Y, TEXT("PARTS COMPUTER"), 28.0f * S, TPCI::Accent, true);
	const float BH = 36.0f * S;
	const float CloseW = 150.0f * S, BikeW = 240.0f * S;
	Btn(X0 + PW - Margin - CloseW, Y, CloseW, BH, TEXT("Close (F / Esc)"), 15.0f * S, false, EHit::Close, 0);
	Btn(X0 + PW - Margin - CloseW - 10.0f * S - BikeW, Y, BikeW, BH, TEXT("Order a project bike"), 15.0f * S, false, EHit::OrderBike, 0);
	Y += BH + 8.0f * S;
	Pen.Text(X0 + Margin, Y, Section == ESection::NewBike
		? TEXT("Configure a new bike: a style, then its parts, colours and finishes. It's built new into a free bay, ready to ride.")
		: TEXT("Pick a kind of part, then a model. Orders arrive in boxes outside the roller door, in the deliveries bay."), 14.0f * S, TPCI::Dim);
	Y += 28.0f * S;
	// The aisles (and the New bike configurator, Torque Shed's Bike shop "New: configure your own").
	const TCHAR* Aisles[] = {TEXT("Bike"), TEXT("Shed"), TEXT("New bike")};
	for (int32 I = 0; I < 3; ++I)
		Btn(X0 + Margin + I * 130.0f * S, Y, 120.0f * S, 34.0f * S, Aisles[I], 16.0f * S, (int32)Section == I, EHit::Section, I);
	Y += 34.0f * S + 12.0f * S;
	if (Section == ESection::Bike && Shed)
	{
		// [shop bay] The bay you're shopping for: its bike decides what's listed (Any bike: everything).
		const float BayH = 30.0f * S, BayPx = 14.0f * S;
		const FString Lead = TEXT("Shopping for:");
		Pen.Text(X0 + Margin, Y + (BayH - 18.0f * S) * 0.5f, Lead, 15.0f * S, TPCI::Dim);
		float BX = X0 + Margin + Pen.Width(Lead, 15.0f * S) + 12.0f * S;
		const float AnyW = 110.0f * S, BGap = 8.0f * S;
		Btn(BX, Y, AnyW, BayH, TEXT("Any bike"), BayPx, ShopBay < 0, EHit::Bay, -1);
		BX += AnyW + BGap;
		const int32 N = UTorqueBuildSubsystem::Bays;
		const float BW = FMath::Max(60.0f * S, (X0 + PW - Margin - BX - (N - 1) * BGap) / N);
		for (int32 B = 0; B < N; ++B)
		{
			FString Label = FString::Printf(TEXT("Bay %d: "), B + 1);
			if (!Shed->Exists(B)) Label += TEXT("empty");
			else Label += Shed->Bike(B).Name.IsEmpty() ? FString(TEXT("a bike")) : Shed->Bike(B).Name;
			Btn(BX + B * (BW + BGap), Y, BW, BayH, Label, BayPx, ShopBay == B, EHit::Bay, B);
		}
		Y += BayH + 10.0f * S;
	}
	if (Section == ESection::NewBike)
	{
		Configurator.Draw(Hud, Canvas, Shed, FBox2D(FVector2D(X0 + Margin, Y), FVector2D(X0 + PW - Margin, Y0 + PH - Margin)), S, Mouse, Status, bStatusOk);
		return;
	}

	// The three columns: kinds, models, the one picked.
	const float ColTop = Y, ColBottom = Y0 + PH - Margin;
	const float KindW = 230.0f * S, RowW = 380.0f * S, Gap = 14.0f * S;
	const float KX = X0 + Margin, RX = KX + KindW + Gap, DX = RX + RowW + Gap, DW = X0 + PW - Margin - DX;
	Box(KX, ColTop, KindW, ColBottom - ColTop, TPCI::Column);
	Box(RX, ColTop, RowW, ColBottom - ColTop, TPCI::Column);
	KindBox = FBox2D(FVector2D(KX, ColTop), FVector2D(KX + KindW, ColBottom));
	RowBox = FBox2D(FVector2D(RX, ColTop), FVector2D(RX + RowW, ColBottom));

	// Kinds, under their headings.
	const float KH = 30.0f * S;
	KindsShown = FMath::Max(1, FMath::FloorToInt((ColBottom - ColTop - 8.0f * S) / KH));
	KindScroll = FMath::Clamp(KindScroll, 0, FMath::Max(0, KindRows.Num() - KindsShown));
	for (int32 I = KindScroll; I < KindRows.Num() && I < KindScroll + KindsShown; ++I)
	{
		const float RY = ColTop + 4.0f * S + (I - KindScroll) * KH;
		const FKindRow& K = KindRows[I];
		if (K.Kind.IsNone())
		{
			Pen.Text(KX + 10.0f * S, RY + 8.0f * S, K.Heading.ToUpper(), 12.0f * S, TPCI::Dim);
			continue;
		}
		Btn(KX + 6.0f * S, RY + 2.0f * S, KindW - 12.0f * S, KH - 4.0f * S, TPCI::KindTitle(K.Kind), 15.0f * S, K.Kind == Kind, EHit::Kind, I);
	}
	if (KindRows.Num() > KindsShown)
		Pen.Text(KX + KindW - 70.0f * S, ColBottom - 22.0f * S, TEXT("(wheel)"), 12.0f * S, TPCI::Dim);

	// [shop bay] Over the list: what bay it's cut to, and how many were left out.
	float ListTop = ColTop;
	if (Section == ESection::Bike && Kind != TPCI::Bikes && ShopBay >= 0 && Shed)
	{
		FString Note;
		bool bLit = false;
		if (!Shed->Exists(ShopBay)) Note = FString::Printf(TEXT("Bay %d is empty: buy a frame to start a bike there."), ShopBay + 1);
		else if (!bBayJudged) Note = FString::Printf(TEXT("Bay %d's bike can't be checked: every part shown."), ShopBay + 1);
		else if (Kind == FName(TEXT("frame"))) Note = TEXT("A frame starts a new bike: every one is shown.");
		else
		{
			Note = FString::Printf(TEXT("Only what fits bay %d's bike"), ShopBay + 1);
			if (BayHidden > 0) Note += FString::Printf(TEXT(" (%d that don't are hidden)"), BayHidden);
			bLit = true;
		}
		Pen.Text(RX + 12.0f * S, ColTop + 7.0f * S, Pen.Fit(Note, 13.0f * S, RowW - 24.0f * S), 13.0f * S, bLit ? TPCI::Accent : TPCI::Dim);
		ListTop += 24.0f * S;
	}
	// Over the models of a part kind: the bike type they're cut to (< and > go round the types).
	float RowTop = ListTop;
	if (Filtered())
	{
		const float TB = 32.0f * S, TX = RX + 6.0f * S, TWide = RowW - 12.0f * S, TY = ListTop + 6.0f * S;
		Btn(TX, TY, TB, TB, TEXT("<"), 16.0f * S, false, EHit::Type, -1);
		Btn(TX + TWide - TB, TY, TB, TB, TEXT(">"), 16.0f * S, false, EHit::Type, 1);
		const bool bAll = TypeFilter.IsNone();
		const FString Label = Pen.Fit(bAll ? FString(TEXT("Bike type: all types")) : TEXT("Bike type: ") + TPCI::TypeName(TypeFilter), 15.0f * S, TWide - 2.0f * TB - 16.0f * S);
		Pen.Text(TX + (TWide - Pen.Width(Label, 15.0f * S)) * 0.5f, TY + (TB - 18.0f * S) * 0.5f, Label, 15.0f * S, bAll ? TPCI::Text : TPCI::Accent, !bAll);
		RowTop = TY + TB + 4.0f * S;
		if (Rows.Num() == 0 && BayHidden == 0)
			for (const FString& L : Pen.Wrap(FString::Printf(TEXT("No %s here suits this bike type. Press < or > for another."),
				*TPCI::KindTitle(Kind).ToLower()), 14.0f * S, TWide - 20.0f * S))
			{
				Pen.Text(TX + 10.0f * S, RowTop + 8.0f * S, L, 14.0f * S, TPCI::Dim);
				RowTop += 19.0f * S;
			}
	}
	// Over the engines: the layout (< and > go round them), then the size, maker and order, each a
	// button that steps on (beenster after 156).
	if (Section == ESection::Bike && Kind == FName(TEXT("engine")))
	{
		const float TB = 32.0f * S, TX = RX + 6.0f * S, TWide = RowW - 12.0f * S, TY = ListTop + 6.0f * S;
		Btn(TX, TY, TB, TB, TEXT("<"), 16.0f * S, false, EHit::EngineFilter, 0);
		Btn(TX + TWide - TB, TY, TB, TB, TEXT(">"), 16.0f * S, false, EHit::EngineFilter, 1);
		const bool bAll = EngineLayoutFilter.IsEmpty();
		const FString Label = Pen.Fit(bAll ? FString(TEXT("Layout: every layout")) : TEXT("Layout: ") + EngineLayoutFilter, 15.0f * S, TWide - 2.0f * TB - 16.0f * S);
		Pen.Text(TX + (TWide - Pen.Width(Label, 15.0f * S)) * 0.5f, TY + (TB - 18.0f * S) * 0.5f, Label, 15.0f * S, bAll ? TPCI::Text : TPCI::Accent, !bAll);
		const float Y2 = TY + TB + 4.0f * S, EGap = 4.0f * S, EBW = (TWide - 2.0f * EGap) / 3.0f;
		Btn(TX, Y2, EBW, TB, Pen.Fit(FString(TEXT("Size: ")) + TPCI::SizeBandName(EngineSize), 13.0f * S, EBW - 8.0f * S), 13.0f * S, EngineSize != 0, EHit::EngineFilter, 2);
		Btn(TX + EBW + EGap, Y2, EBW, TB, Pen.Fit(TEXT("Maker: ") + (EngineMakerFilter.IsEmpty() ? FString(TEXT("any")) : EngineMakerFilter), 13.0f * S, EBW - 8.0f * S), 13.0f * S,
			!EngineMakerFilter.IsEmpty(), EHit::EngineFilter, 3);
		Btn(TX + 2.0f * (EBW + EGap), Y2, EBW, TB, Pen.Fit(FString(TEXT("Sort: ")) + TPCI::EngineSortName(EngineSort), 13.0f * S, EBW - 8.0f * S), 13.0f * S, false, EHit::EngineFilter, 4);
		RowTop = Y2 + TB + 4.0f * S;
		if (Rows.Num() == 0 && BayHidden == 0)
			for (const FString& L : Pen.Wrap(TEXT("No engine here matches. Change the layout, size or maker."), 14.0f * S, TWide - 20.0f * S))
			{
				Pen.Text(TX + 10.0f * S, RowTop + 8.0f * S, L, 14.0f * S, TPCI::Dim);
				RowTop += 19.0f * S;
			}
	}
	if (Rows.Num() == 0 && BayHidden > 0)
		for (const FString& L : Pen.Wrap(FString::Printf(TEXT("Nothing here fits bay %d's bike. Pick Any bike at the top to see every one."), ShopBay + 1),
			14.0f * S, RowW - 32.0f * S))
		{
			Pen.Text(RX + 16.0f * S, RowTop + 8.0f * S, L, 14.0f * S, TPCI::Dim);
			RowTop += 19.0f * S;
		}
	// The models of the kind: name, and under it what it is (or how much the bay has).
	const float RH = 50.0f * S;
	RowsShown = FMath::Max(1, FMath::FloorToInt((ColBottom - RowTop - 8.0f * S) / RH));
	RowScroll = FMath::Clamp(RowScroll, 0, FMath::Max(0, Rows.Num() - RowsShown));
	const FTorquePaintBay* Bay = Shed ? &Shed->PaintBay() : nullptr;
	for (int32 I = RowScroll; I < Rows.Num() && I < RowScroll + RowsShown; ++I)
	{
		const float RY = RowTop + 4.0f * S + (I - RowScroll) * RH;
		const FRow& R = Rows[I];
		const bool bPicked = I == Row;
		const bool bOver = Over(RX + 6.0f * S, RY, RowW - 12.0f * S, RH - 4.0f * S);
		Box(RX + 6.0f * S, RY, RowW - 12.0f * S, RH - 4.0f * S, bPicked ? (bOver ? TPCI::PickedHover : TPCI::Picked) : (bOver ? TPCI::ButtonHover : TPCI::Button));
		FString Name, Sub, Tag;
		bool bTagMatch = false;
		if (R.Stock != INDEX_NONE)
		{
			Name = FTorquePaintBay::StockName(R.Stock);
			Sub = Bay ? TEXT("In the bay: ") + Bay->StockText(R.Stock) : FString();
		}
		else if (R.Part == FName(TEXT("bike")))
		{
			Name = TEXT("A project bike");
			Sub = TEXT("Tired, rusty and complete: into a free bay");
		}
		else if (const TorqueFurniture::FModel* M = TPCI::IsKit(Kind) ? TorqueFurniture::Find(R.Part) : nullptr)
		{
			Name = M->Name;
			Sub = M->Desc;
		}
		else
		{
			Name = TorqueShedParts::PartName(R.Part);
			Sub = TorqueShedParts::PartDescription(R.Part);
			const FName Best = TPCI::KeptBest(R.Part);
			if (Kind == FName(TEXT("engine")))
			{
				// Engines: their layout (beenster, Nov 12), not a bike type, and their capacity (after 156).
				Tag = TorqueShedParts::EngineLayout(R.Part);
				const double Cc = TorqueShedParts::PartNumber(R.Part, TEXT("displacement"), 0.0);
				if (Cc > 0.0) Tag += FString::Printf(TEXT(" \u00B7 %d cc"), FMath::RoundToInt(Cc));
				bTagMatch = false;
			}
			else if (TPCI::Sided(TorqueShedParts::PartKind(R.Part)))
			{
				// [shop bay] Wheels and tyres: the side of the bike they go on.
				Tag = TPCI::SideText(SideOf(R.Part));
				bTagMatch = false;
			}
			else if (Kind == FName(TEXT("frame")))
			{
				// Frames: the bike types they're for (the one it's made for first).
				TArray<FString> Names;
				for (const FName& T : TPCI::KeptStyles(R.Part))
					if (Names.Num() < 3) Names.Add(TPCI::TypeName(T));
				Tag = Names.Num() > 0 ? FString::Join(Names, TEXT(" \u00B7 ")) : TPCI::TypeName(Best);
				bTagMatch = !TypeFilter.IsNone() && TorqueShedParts::SuitsStyle(R.Part, TypeFilter);
			}
			else
			{
				Tag = TPCI::TypeName(Best);
				bTagMatch = !TypeFilter.IsNone() && Best == TypeFilter;
			}
		}
		const float TW = RowW - 32.0f * S;
		float NameW = TW;
		if (!Tag.IsEmpty())
		{
			// The preferred bike type, a tag at the right of the name.
			const float TagPx = 11.0f * S;
			const float TagW = Pen.Width(Tag, TagPx) + 14.0f * S, TagH = 18.0f * S;
			const float TagX = RX + RowW - 16.0f * S - TagW, TagY = RY + 5.0f * S;
			const FLinearColor TagBg = bPicked ? FLinearColor(0.0f, 0.0f, 0.0f, 0.22f) : (bTagMatch ? FLinearColor(TPCI::Accent.R, TPCI::Accent.G, TPCI::Accent.B, 0.28f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.09f));
			Box(TagX, TagY, TagW, TagH, TagBg);
			Pen.Text(TagX + 7.0f * S, TagY + (TagH - TagPx * 1.2f) * 0.5f, Tag, TagPx, bPicked ? TPCI::Ink : (bTagMatch ? TPCI::Accent : TPCI::Dim));
			NameW = TW - TagW - 8.0f * S;
		}
		Pen.Text(RX + 16.0f * S, RY + 5.0f * S, Pen.Fit(Name, 16.0f * S, NameW), 16.0f * S, bPicked ? TPCI::Ink : TPCI::Text);
		// Needed to ride, or not: a small mark at the right of the second line.
		const FName RowKind = R.Part.IsNone() || TPCI::IsKit(Kind) ? Kind : TorqueShedParts::PartKind(R.Part);   // (a list may hold two kinds)
		const int32 Need = (R.Stock == INDEX_NONE && R.Part != FName(TEXT("bike")) && !TPCI::IsKit(Kind)) ? TPCI::Essential(RowKind.IsNone() ? Kind : RowKind) : -1;
		float SubW = TW;
		if (Need >= 0)
		{
			const FString Mark = Need == 1 ? TEXT("Essential") : TEXT("Optional");
			const float MPx = 11.0f * S, MH = 17.0f * S;
			const float MW = Pen.Width(Mark, MPx) + 14.0f * S;
			const float MX = RX + RowW - 16.0f * S - MW, MY = RY + 25.0f * S;
			const FLinearColor Green(0.42f, 0.78f, 0.45f, 1.0f);
			Box(MX, MY, MW, MH, Need == 1 ? FLinearColor(Green.R, Green.G, Green.B, bPicked ? 0.35f : 0.18f) : FLinearColor(1.0f, 1.0f, 1.0f, bPicked ? 0.16f : 0.06f));
			Pen.Text(MX + 7.0f * S, MY + (MH - MPx * 1.2f) * 0.5f, Mark, MPx, Need == 1 ? (bPicked ? TPCI::Ink : Green) : (bPicked ? TPCI::InkDim : TPCI::Dim));
			SubW = TW - MW - 8.0f * S;
		}
		Pen.Text(RX + 16.0f * S, RY + 26.0f * S, Pen.Fit(Sub, 12.0f * S, SubW), 12.0f * S, bPicked ? TPCI::InkDim : TPCI::Dim);
		AddHit(RX + 6.0f * S, RY, RowW - 12.0f * S, RH - 4.0f * S, EHit::Row, I);
	}
	if (Rows.Num() > RowsShown)
		Pen.Text(RX + RowW - 150.0f * S, ColBottom - 22.0f * S, FString::Printf(TEXT("%d more (wheel)"), Rows.Num() - RowsShown), 12.0f * S, TPCI::Dim);

	// The one picked: its name, what it fits and weighs, what it is, how it comes; Order at the bottom.
	FString Title, Spec, Desc, OrderText = TEXT("Order it");
	if (Rows.IsValidIndex(Row))
	{
		const FRow& R = Rows[Row];
		if (R.Stock != INDEX_NONE)
		{
			const bool bBase = R.Stock < TorquePaintMix::Bases().Num();
			Title = FTorquePaintBay::StockName(R.Stock);
			Spec = Bay ? TEXT("In the bay now: ") + Bay->StockText(R.Stock) : FString();
			Desc = bBase ? TEXT("A one litre tin of base for the mixing bay. Pour tints into it at the bench, stir or shake it, then fill the spray gun's cup from it.")
				: TEXT("A bottle of tint for the rack over the mixing bench. Tip it over a tin on the scale to pour.");
			Desc += TEXT("\n\nIt goes straight onto the bay's shelves.");
			OrderText = bBase ? TEXT("Order a tin") : TEXT("Order a bottle");
		}
		else if (R.Part == FName(TEXT("bike")))
		{
			Title = TEXT("A project bike");
			Desc = TEXT("A tired, rusty bike, complete, put in the first free bay. Strip it down, clean it up, rebuild it and ride it out of the door.")
				TEXT("\n\nIt comes in any of the styles: the one above is shown clean, as it was built.");
			OrderText = TEXT("Order a project bike");
		}
		else if (const TorqueFurniture::FModel* M = TPCI::IsKit(Kind) ? TorqueFurniture::Find(R.Part) : nullptr)
		{
			Title = M->Name;
			Spec = FString::Printf(TEXT("Size: %.1f m wide, %.1f m high, %.1f m deep"), M->Size.X / 100.0, M->Size.Y / 100.0, M->Size.Z / 100.0);
			if (M->bRide) Spec += TEXT("\nThe bike rides on it");
			else if (M->bSolid) Spec += TEXT("\nSolid: the bike bumps into it");
			Desc = M->Desc;
			Desc += M->bYard ? TEXT("\n\nArrives in the yard kit drop-off out on the pad. Hold F on it to take hold of it, walk to push it about, R to turn it, F to put it down.")
				: (M->bWall ? TEXT("\n\nArrives in the delivery area outside the roller door. Hold F on it to take hold of it, look at a wall and press F to hang it there.")
				: TEXT("\n\nArrives in the delivery area outside the roller door. Hold F on it to take hold of it, R to turn it, F to put it down."));
			OrderText = TEXT("Order it");
		}
		else
		{
			Title = TorqueShedParts::PartName(R.Part);
			TArray<FString> Fits;
			// [bare frame] A V2 bike's part: its own slot, and the frames (or engine) it bolts to.
			const FName OwnSlot = TorqueShedParts::V2Slot(R.Part);
			if (!OwnSlot.IsNone()) Fits.Add(TorqueShedParts::SlotName(OwnSlot).ToLower());
			else
				for (const FName& Slot : TorqueShedParts::SlotsForKind(TorqueShedParts::PartKind(R.Part)))
				{
					// [shop bay] A tyre (or wheel) only on the side it goes.
					const FString SlotText = Slot.ToString();
					const int32 Bit = SlotText.StartsWith(TEXT("front_")) ? 1 : (SlotText.StartsWith(TEXT("rear_")) ? 2 : 3);
					if (TPCI::Sided(TorqueShedParts::PartKind(R.Part)) && SideOf(R.Part) != 0 && (SideOf(R.Part) & Bit) == 0) continue;
					Fits.Add(TorqueShedParts::SlotName(Slot).ToLower());
				}
			Spec = Fits.Num() > 0 ? TEXT("Fits: ") + TPCI::Join(Fits) : FString();
			if (bBayJudged && TorqueShedParts::PartKind(R.Part) != FName(TEXT("frame")))
				Spec += FString::Printf(TEXT("\nGoes on bay %d's bike"), ShopBay + 1);
			const FString BoltsTo = TorqueShedParts::V2Fits(R.Part);
			if (!BoltsTo.IsEmpty()) Spec += TEXT("\nBolts to: ") + BoltsTo;
			const int32 Need = TPCI::Essential(TorqueShedParts::PartKind(R.Part));
			if (Need == 1) Spec += TEXT("\nEssential: the bike won't ride without one");
			else if (Need == 0) Spec += TEXT("\nOptional: the bike rides without it");
			const double Mass = TorqueShedParts::PartNumber(R.Part, FName(TEXT("mass")), -1.0);
			if (Mass > 0.0) Spec += FString::Printf(TEXT("\nWeight: %.1f kg"), Mass);
			// The bike types it suits: the one it's made for, and the others it looks right on.
			const TArray<FName> Types = TPCI::KeptStyles(R.Part);
			if (Kind == FName(TEXT("engine")))
			{
				Spec += TEXT("\nLayout: ") + TorqueShedParts::EngineLayout(R.Part);
				const double Cc = TorqueShedParts::PartNumber(R.Part, TEXT("displacement"), 0.0);
				if (Cc > 0.0) Spec += FString::Printf(TEXT("\nCapacity: %d cc"), FMath::RoundToInt(Cc));
			}
			if (Types.Num() == 0) Spec += TEXT("\nPreferred bike type: any (it suits every bike)");
			else
			{
				Spec += TEXT("\nPreferred bike type: ") + TPCI::TypeName(Types[0]);
				TArray<FString> Others;
				for (int32 I = 1; I < Types.Num(); ++I) Others.Add(TPCI::TypeName(Types[I]));
				if (Others.Num() > 0) Spec += TEXT("\nAlso suits: ") + TPCI::Join(Others);
			}
			Desc = TorqueShedParts::PartDescription(R.Part);
			Desc += TEXT("\n\nArrives boxed outside the roller door. Press F on the box to unpack it, pick the part up with your hands, ")
				TEXT("take it to the bike and hold the work button on where it goes; then bolt it up with the wrench.");
		}
	}
	float DY = ColTop + 4.0f * S;
	// The picked item in 3D, turning (drag to turn, wheel to zoom, right-click to pause).
	ShowPicked(Hud);
	if (ATorqueShopPreview* P = Preview.Get())
	{
		const float PrevH = FMath::Min(DW / 1.6f, (ColBottom - ColTop) * 0.5f);
		P->Draw(Hud, Canvas, FBox2D(FVector2D(DX, DY), FVector2D(DX + DW, DY + PrevH)), Mouse);
		DY += PrevH + 8.0f * S;
		// A project bike comes in any style: page through them.
		if (Rows.IsValidIndex(Row) && Rows[Row].Part == FName(TEXT("bike")))
		{
			const TArray<FString> Styles = ATorqueShopPreview::BikeStyles();
			if (Styles.Num() > 0)
			{
				BikeStyle = FMath::Clamp(BikeStyle, 0, Styles.Num() - 1);
				const float SB = 30.0f * S;
				Btn(DX, DY, SB, SB, TEXT("<"), 16.0f * S, false, EHit::Style, -1);
				Btn(DX + DW - SB, DY, SB, SB, TEXT(">"), 16.0f * S, false, EHit::Style, 1);
				const FString Label = Pen.Fit(FString::Printf(TEXT("%s  (style %d of %d)"), *TPCI::StyleTitle(Styles[BikeStyle]), BikeStyle + 1, Styles.Num()),
					15.0f * S, DW - 2.0f * SB - 16.0f * S);
				Pen.Text(DX + (DW - Pen.Width(Label, 15.0f * S)) * 0.5f, DY + (SB - 18.0f * S) * 0.5f, Label, 15.0f * S, TPCI::Text);
				DY += SB + 8.0f * S;
			}
		}
	}
	for (const FString& L : Pen.Wrap(Title, 22.0f * S, DW))
	{
		Pen.Text(DX, DY, L, 22.0f * S, TPCI::Text, true);
		DY += 28.0f * S;
	}
	DY += 6.0f * S;
	const float OrderH = 44.0f * S, OrderY = ColBottom - OrderH;
	const float StatusY = OrderY - 30.0f * S;
	for (const FString& L : Pen.Wrap(Spec, 14.0f * S, DW))
	{
		if (DY > StatusY - 20.0f * S) break;
		Pen.Text(DX, DY, L, 14.0f * S, TPCI::Accent);
		DY += 19.0f * S;
	}
	DY += 8.0f * S;
	for (const FString& L : Pen.Wrap(Desc, 15.0f * S, DW))
	{
		if (DY > StatusY - 20.0f * S) break;
		Pen.Text(DX, DY, L, 15.0f * S, TPCI::Text);
		DY += 20.0f * S;
	}
	if (!Status.IsEmpty()) Pen.Text(DX, StatusY, Pen.Fit(Status, 14.0f * S, DW), 14.0f * S, bStatusOk ? TPCI::Good : TPCI::Bad);
	if (Rows.IsValidIndex(Row)) Btn(DX, OrderY, DW, OrderH, OrderText, 18.0f * S, true, EHit::Order, 0);
}
