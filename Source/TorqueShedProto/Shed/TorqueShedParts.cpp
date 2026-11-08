#include "Shed/TorqueShedParts.h"

#include "Shed/TorqueFitApi.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TorqueShedPartsImpl
{
// The tables are made the first time they're asked for, never while the program starts (FNames).
TArray<FName> NameList(const TCHAR* const* Names, int32 Count)
{
	TArray<FName> Out;
	Out.Reserve(Count);
	for (int32 I = 0; I < Count; ++I) Out.Add(FName(Names[I]));
	return Out;
}

#define TORQUE_LIST(Arr) NameList(Arr, UE_ARRAY_COUNT(Arr))

const TCHAR* const SlotNames[] = {TEXT("frame"), TEXT("fork"), TEXT("shock"), TEXT("front_wheel"), TEXT("rear_wheel"),
	TEXT("front_tyre"), TEXT("rear_tyre"), TEXT("front_disc"), TEXT("rear_disc"), TEXT("front_caliper"), TEXT("rear_caliper"),
	TEXT("front_pads"), TEXT("rear_pads"), TEXT("front_master"), TEXT("rear_master"), TEXT("tank"), TEXT("saddle"),
	TEXT("handlebars"), TEXT("dashboard"), TEXT("engine"), TEXT("exhaust"), TEXT("final_drive"), TEXT("chain"),
	TEXT("side_stand"), TEXT("centre_stand"), TEXT("headlight"), TEXT("horn"), TEXT("ecu"), TEXT("panniers"), TEXT("top_box"),
	TEXT("fog_lights"), TEXT("tail"), TEXT("fairing"), TEXT("mirrors"), TEXT("head"), TEXT("fuel"), TEXT("air_filter"),
	TEXT("oil_filter"), TEXT("spark_plugs"), TEXT("cooling"), TEXT("gearbox"), TEXT("front_mudguard"), TEXT("indicators"),
	TEXT("side_panels"), TEXT("rearsets"), TEXT("guards"), TEXT("number_boards"), TEXT("swingarm"), TEXT("steering_damper"),
	TEXT("tank_bag"), TEXT("aux_tank"), TEXT("toolbox"), TEXT("satnav"), TEXT("lever_guards")};
const TCHAR* const EngineSlotNames[] = {TEXT("head"), TEXT("fuel"), TEXT("air_filter"), TEXT("oil_filter"),
	TEXT("spark_plugs"), TEXT("cooling"), TEXT("gearbox")};
const TCHAR* const ServiceSlotNames[] = {TEXT("air_filter"), TEXT("oil_filter"), TEXT("spark_plugs")};
const TCHAR* const LowSlotNames[] = {TEXT("side_stand"), TEXT("centre_stand"), TEXT("final_drive"), TEXT("chain"),
	TEXT("rearsets"), TEXT("guards")};
const TCHAR* const OptionalSlotNames[] = {TEXT("headlight"), TEXT("indicators"), TEXT("front_mudguard"), TEXT("ecu"),
	TEXT("steering_damper"), TEXT("mirrors"), TEXT("lever_guards"), TEXT("fairing"), TEXT("satnav"), TEXT("tail"),
	TEXT("guards"), TEXT("panniers"), TEXT("top_box"), TEXT("tank_bag"), TEXT("number_boards"), TEXT("aux_tank"),
	TEXT("toolbox"), TEXT("fog_lights"), TEXT("horn"), TEXT("side_panels"), TEXT("centre_stand")};
const TCHAR* const FrameOwnSlotNames[] = {TEXT("rearsets"), TEXT("swingarm")};
const TCHAR* const KindNames[] = {TEXT("frame"), TEXT("fork"), TEXT("shock"), TEXT("wheel"), TEXT("tyre"), TEXT("disc"),
	TEXT("caliper"), TEXT("pads"), TEXT("master"), TEXT("tank"), TEXT("saddle"), TEXT("handlebars"), TEXT("dashboard"),
	TEXT("engine"), TEXT("head"), TEXT("fuel"), TEXT("air_filter"), TEXT("oil_filter"), TEXT("spark_plugs"), TEXT("cooling"),
	TEXT("gearbox"), TEXT("exhaust"), TEXT("final_drive"), TEXT("chain"), TEXT("side_stand"), TEXT("centre_stand"),
	TEXT("headlight"), TEXT("horn"), TEXT("ecu"), TEXT("panniers"), TEXT("top_box"), TEXT("fog_lights"), TEXT("tail"),
	TEXT("fairing"), TEXT("mirrors"), TEXT("front_mudguard"), TEXT("indicators"), TEXT("side_panels"), TEXT("rearsets"),
	TEXT("guards"), TEXT("number_boards"), TEXT("swingarm"), TEXT("steering_damper"), TEXT("tank_bag"), TEXT("aux_tank"),
	TEXT("toolbox"), TEXT("satnav"), TEXT("lever_guards")};

// PartsDB.KIND_NAMES
struct FPair { const TCHAR* Key; const TCHAR* Value; };
const FPair KindTitles[] = {
	{TEXT("frame"), TEXT("Frame")}, {TEXT("fork"), TEXT("Front fork")}, {TEXT("shock"), TEXT("Rear suspension")},
	{TEXT("wheel"), TEXT("Wheel")}, {TEXT("tyre"), TEXT("Tyre")}, {TEXT("disc"), TEXT("Brake disc")},
	{TEXT("caliper"), TEXT("Brake caliper")}, {TEXT("pads"), TEXT("Brake pads")}, {TEXT("master"), TEXT("Master cylinder")},
	{TEXT("tank"), TEXT("Fuel tank")}, {TEXT("saddle"), TEXT("Saddle")}, {TEXT("handlebars"), TEXT("Handlebars")},
	{TEXT("dashboard"), TEXT("Dashboard")}, {TEXT("engine"), TEXT("Engine block")}, {TEXT("head"), TEXT("Cylinder head")},
	{TEXT("fuel"), TEXT("Fuel system")}, {TEXT("air_filter"), TEXT("Air filter")}, {TEXT("oil_filter"), TEXT("Oil filter")},
	{TEXT("spark_plugs"), TEXT("Spark plugs")}, {TEXT("cooling"), TEXT("Cooling")}, {TEXT("gearbox"), TEXT("Gearbox")},
	{TEXT("exhaust"), TEXT("Exhaust")}, {TEXT("final_drive"), TEXT("Sprockets")}, {TEXT("chain"), TEXT("Chain")},
	{TEXT("side_stand"), TEXT("Side stand")}, {TEXT("centre_stand"), TEXT("Centre stand")}, {TEXT("headlight"), TEXT("Headlight")},
	{TEXT("horn"), TEXT("Horn")}, {TEXT("ecu"), TEXT("ECU")}, {TEXT("panniers"), TEXT("Panniers")}, {TEXT("top_box"), TEXT("Top box")},
	{TEXT("fog_lights"), TEXT("Fog lights")}, {TEXT("tail"), TEXT("Tail unit")}, {TEXT("fairing"), TEXT("Fairing")},
	{TEXT("mirrors"), TEXT("Mirrors")}, {TEXT("front_mudguard"), TEXT("Front mudguard")}, {TEXT("indicators"), TEXT("Indicators")},
	{TEXT("side_panels"), TEXT("Side panels")}, {TEXT("rearsets"), TEXT("Footpegs and rearsets")}, {TEXT("guards"), TEXT("Engine guards")},
	{TEXT("number_boards"), TEXT("Number boards")}, {TEXT("swingarm"), TEXT("Swingarm")}, {TEXT("steering_damper"), TEXT("Steering damper")},
	{TEXT("tank_bag"), TEXT("Tank bag")}, {TEXT("aux_tank"), TEXT("Auxiliary fuel tank")}, {TEXT("toolbox"), TEXT("Toolbox")},
	{TEXT("satnav"), TEXT("Sat nav")}, {TEXT("lever_guards"), TEXT("Lever guards")}, {TEXT("petrol"), TEXT("Petrol")},
	{TEXT("oil"), TEXT("Engine oil")}, {TEXT("brake_fluid"), TEXT("Brake fluid")}, {TEXT("chain_lube"), TEXT("Chain lube")},
};

// PartsDB.KIND_MATERIAL
const FPair KindMaterials[] = {
	{TEXT("frame"), TEXT("metal")}, {TEXT("fork"), TEXT("metal")}, {TEXT("shock"), TEXT("metal")}, {TEXT("wheel"), TEXT("metal")},
	{TEXT("tyre"), TEXT("rubber")}, {TEXT("disc"), TEXT("metal")}, {TEXT("caliper"), TEXT("metal")}, {TEXT("pads"), TEXT("mixed")},
	{TEXT("master"), TEXT("metal")}, {TEXT("tank"), TEXT("metal")}, {TEXT("saddle"), TEXT("mixed")}, {TEXT("handlebars"), TEXT("metal")},
	{TEXT("dashboard"), TEXT("electronics")}, {TEXT("engine"), TEXT("metal")}, {TEXT("head"), TEXT("metal")}, {TEXT("fuel"), TEXT("metal")},
	{TEXT("air_filter"), TEXT("mixed")}, {TEXT("oil_filter"), TEXT("metal")}, {TEXT("spark_plugs"), TEXT("mixed")},
	{TEXT("cooling"), TEXT("metal")}, {TEXT("gearbox"), TEXT("metal")}, {TEXT("exhaust"), TEXT("metal")},
	{TEXT("final_drive"), TEXT("metal")}, {TEXT("chain"), TEXT("metal")}, {TEXT("side_stand"), TEXT("metal")},
	{TEXT("centre_stand"), TEXT("metal")}, {TEXT("headlight"), TEXT("electronics")}, {TEXT("horn"), TEXT("metal")},
	{TEXT("ecu"), TEXT("electronics")}, {TEXT("panniers"), TEXT("fabric")}, {TEXT("top_box"), TEXT("plastic")},
	{TEXT("fog_lights"), TEXT("electronics")}, {TEXT("tail"), TEXT("plastic")}, {TEXT("fairing"), TEXT("plastic")},
	{TEXT("mirrors"), TEXT("mixed")}, {TEXT("front_mudguard"), TEXT("plastic")}, {TEXT("indicators"), TEXT("electronics")},
	{TEXT("side_panels"), TEXT("plastic")}, {TEXT("rearsets"), TEXT("metal")}, {TEXT("guards"), TEXT("metal")},
	{TEXT("number_boards"), TEXT("plastic")}, {TEXT("swingarm"), TEXT("metal")}, {TEXT("steering_damper"), TEXT("metal")},
	{TEXT("tank_bag"), TEXT("fabric")}, {TEXT("aux_tank"), TEXT("metal")}, {TEXT("toolbox"), TEXT("metal")},
	{TEXT("satnav"), TEXT("electronics")}, {TEXT("lever_guards"), TEXT("metal")},
};
const FPair MaterialTitles[] = {{TEXT("metal"), TEXT("Metal")}, {TEXT("plastic"), TEXT("Plastic")}, {TEXT("rubber"), TEXT("Rubber")},
	{TEXT("carbon"), TEXT("Carbon fibre")}, {TEXT("mixed"), TEXT("Mixed materials")}, {TEXT("electronics"), TEXT("Electronics")},
	{TEXT("fabric"), TEXT("Fabric")}, {TEXT("leather"), TEXT("Leather")}, {TEXT("wood"), TEXT("Wood")}};

const TCHAR* Lookup(const FPair* Table, int32 Count, const FString& Key)
{
	for (int32 I = 0; I < Count; ++I)
		if (Key == Table[I].Key) return Table[I].Value;
	return nullptr;
}

// PartsDB.MOUNTS: what each slot bolts onto.
struct FMount { const TCHAR* Slot; const TCHAR* On[2]; };
const FMount Mounts[] = {
	{TEXT("frame"), {nullptr, nullptr}}, {TEXT("fork"), {TEXT("frame"), nullptr}}, {TEXT("shock"), {TEXT("frame"), nullptr}},
	{TEXT("front_wheel"), {TEXT("fork"), nullptr}}, {TEXT("rear_wheel"), {TEXT("shock"), nullptr}},
	{TEXT("front_tyre"), {TEXT("front_wheel"), nullptr}}, {TEXT("rear_tyre"), {TEXT("rear_wheel"), nullptr}},
	{TEXT("front_disc"), {TEXT("front_wheel"), nullptr}}, {TEXT("rear_disc"), {TEXT("rear_wheel"), nullptr}},
	{TEXT("front_caliper"), {TEXT("front_disc"), nullptr}}, {TEXT("rear_caliper"), {TEXT("rear_disc"), nullptr}},
	{TEXT("front_pads"), {TEXT("front_caliper"), nullptr}}, {TEXT("rear_pads"), {TEXT("rear_caliper"), nullptr}},
	{TEXT("handlebars"), {TEXT("fork"), nullptr}}, {TEXT("dashboard"), {TEXT("handlebars"), nullptr}},
	{TEXT("front_master"), {TEXT("handlebars"), nullptr}}, {TEXT("rear_master"), {TEXT("frame"), nullptr}},
	{TEXT("tank"), {TEXT("frame"), nullptr}}, {TEXT("saddle"), {TEXT("frame"), nullptr}},
	{TEXT("engine"), {TEXT("frame"), nullptr}}, {TEXT("exhaust"), {TEXT("engine"), nullptr}},
	{TEXT("final_drive"), {TEXT("engine"), TEXT("rear_wheel")}}, {TEXT("chain"), {TEXT("final_drive"), nullptr}},
	{TEXT("head"), {TEXT("engine"), nullptr}}, {TEXT("oil_filter"), {TEXT("engine"), nullptr}},
	{TEXT("cooling"), {TEXT("engine"), nullptr}}, {TEXT("gearbox"), {TEXT("engine"), nullptr}},
	{TEXT("spark_plugs"), {TEXT("head"), nullptr}}, {TEXT("fuel"), {TEXT("head"), nullptr}}, {TEXT("air_filter"), {TEXT("fuel"), nullptr}},
	{TEXT("side_stand"), {TEXT("frame"), nullptr}}, {TEXT("centre_stand"), {TEXT("frame"), nullptr}}, {TEXT("headlight"), {TEXT("fork"), nullptr}},
	{TEXT("horn"), {TEXT("frame"), nullptr}}, {TEXT("ecu"), {TEXT("frame"), nullptr}}, {TEXT("panniers"), {TEXT("frame"), nullptr}},
	{TEXT("top_box"), {TEXT("frame"), nullptr}}, {TEXT("fog_lights"), {TEXT("fork"), nullptr}},
	{TEXT("tail"), {TEXT("frame"), nullptr}}, {TEXT("fairing"), {TEXT("headlight"), nullptr}}, {TEXT("mirrors"), {TEXT("handlebars"), nullptr}},
	{TEXT("front_mudguard"), {TEXT("fork"), nullptr}},
	{TEXT("indicators"), {TEXT("frame"), nullptr}}, {TEXT("side_panels"), {TEXT("frame"), nullptr}}, {TEXT("rearsets"), {TEXT("frame"), nullptr}},
	{TEXT("guards"), {TEXT("frame"), nullptr}}, {TEXT("number_boards"), {TEXT("frame"), nullptr}}, {TEXT("swingarm"), {TEXT("frame"), nullptr}},
	{TEXT("steering_damper"), {TEXT("fork"), nullptr}},
	{TEXT("tank_bag"), {TEXT("tank"), nullptr}}, {TEXT("satnav"), {TEXT("handlebars"), nullptr}}, {TEXT("aux_tank"), {TEXT("frame"), nullptr}},
	{TEXT("toolbox"), {TEXT("frame"), nullptr}}, {TEXT("lever_guards"), {TEXT("handlebars"), nullptr}},
};

const TMap<FName, TArray<FName>>& MountTable()
{
	static TMap<FName, TArray<FName>> Table;
	if (Table.Num() == 0)
		for (const FMount& M : Mounts)
		{
			TArray<FName>& On = Table.Add(FName(M.Slot));
			for (const TCHAR* O : M.On)
				if (O) On.Add(FName(O));
		}
	return Table;
}

// The Speed Twin's model parts by slot (its stock meshes; Data/Bike/SpeedTwin/fitment.json). Its
// footrests and swingarm are the frame's own (the rearsets and swingarm slots are empty on it).
struct FModelSlot { const TCHAR* Slot; const TCHAR* Parts; };
const FModelSlot ModelSlots[] = {
	{TEXT("frame"), TEXT("frame bolts battery_box footrests swingarm")},
	{TEXT("fork"), TEXT("yokes fork_stanchions fork_lowers")},
	{TEXT("shock"), TEXT("shock_bodies shock_springs shock_tops")},
	{TEXT("front_wheel"), TEXT("front_rim")}, {TEXT("rear_wheel"), TEXT("rear_rim")},
	{TEXT("front_tyre"), TEXT("front_tyre")}, {TEXT("rear_tyre"), TEXT("rear_tyre")},
	{TEXT("front_disc"), TEXT("front_discs front_carriers")}, {TEXT("rear_disc"), TEXT("rear_disc")},
	{TEXT("front_caliper"), TEXT("front_calipers")}, {TEXT("rear_caliper"), TEXT("rear_caliper rear_brake_line")},
	{TEXT("front_master"), TEXT("lever_brake")},
	{TEXT("tank"), TEXT("tank tank_decals tank_badges tank_cap")},
	{TEXT("saddle"), TEXT("seat seat_pan")},
	{TEXT("handlebars"), TEXT("handlebar grips_l grips_r grips_switches_l grips_switches_r lever_clutch cables")},
	{TEXT("dashboard"), TEXT("clock_body clock_face")},
	{TEXT("engine"), TEXT("crankcase sump cylinders engine_covers cover_badges")},
	{TEXT("head"), TEXT("cam_cover")}, {TEXT("fuel"), TEXT("throttle_bodies")},
	{TEXT("cooling"), TEXT("radiator coolant_hoses")}, {TEXT("gearbox"), TEXT("gearbox")},
	{TEXT("exhaust"), TEXT("headers silencers silencer_caps")},
	{TEXT("final_drive"), TEXT("rear_sprocket")}, {TEXT("chain"), TEXT("chain chain_guard")},
	{TEXT("side_stand"), TEXT("side_stand")},
	{TEXT("headlight"), TEXT("headlight_shell headlight_lens headlight_projector")},
	{TEXT("horn"), TEXT("horn")},
	{TEXT("tail"), TEXT("tail rear_light plate_hanger plate hugger")},
	{TEXT("mirrors"), TEXT("mirrors_l mirrors_r mirror_glass_l mirror_glass_r mirror_stalks_l mirror_stalks_r")},
	{TEXT("front_mudguard"), TEXT("front_mudguard")},
	{TEXT("indicators"), TEXT("front_indicators rear_indicators")},
	{TEXT("side_panels"), TEXT("side_panel_l side_panel_l_decal side_panel_r side_panel_r_decal")},
};
// The slots the Speed Twin comes with a part in: the ones it shows, and the ones inside it.
const TCHAR* const StockSlotNames[] = {TEXT("frame"), TEXT("fork"), TEXT("shock"), TEXT("front_wheel"), TEXT("rear_wheel"),
	TEXT("front_tyre"), TEXT("rear_tyre"), TEXT("front_disc"), TEXT("rear_disc"), TEXT("front_caliper"), TEXT("rear_caliper"),
	TEXT("front_pads"), TEXT("rear_pads"), TEXT("front_master"), TEXT("rear_master"), TEXT("tank"), TEXT("saddle"),
	TEXT("handlebars"), TEXT("dashboard"), TEXT("engine"), TEXT("exhaust"), TEXT("final_drive"), TEXT("chain"),
	TEXT("side_stand"), TEXT("headlight"), TEXT("horn"), TEXT("tail"), TEXT("mirrors"), TEXT("front_mudguard"),
	TEXT("indicators"), TEXT("side_panels"), TEXT("head"), TEXT("fuel"), TEXT("air_filter"), TEXT("oil_filter"),
	TEXT("spark_plugs"), TEXT("cooling"), TEXT("gearbox")};
// The Speed Twin's fit slots that aren't named as the Godot slots are.
const FPair FitSlotToSlot[] = {{TEXT("screen"), TEXT("fairing")}, {TEXT("rack"), TEXT("top_box")},
	{TEXT("grips"), TEXT("handlebars")}, {TEXT("levers"), TEXT("handlebars")}};

const TMap<FName, FName>& ModelPartSlots()
{
	static TMap<FName, FName> Table;
	if (Table.Num() == 0)
		for (const FModelSlot& M : ModelSlots)
		{
			TArray<FString> Parts;
			FString(M.Parts).ParseIntoArray(Parts, TEXT(" "));
			for (const FString& P : Parts) Table.Add(FName(*P), FName(M.Slot));
		}
	return Table;
}

// --- The catalogue --------------------------------------------------------------------------------
struct FPartInfo
{
	FName Kind;
	FString Name, Description, Material;
	FLinearColor Colour = FLinearColor(0.5f, 0.5f, 0.5f);
	TArray<FName> MountsOn;
};

struct FCatalogue
{
	TMap<FName, FPartInfo> Parts;
	TMap<FName, TArray<FName>> ByKind;
	bool bLoaded = false;
};

FCatalogue& Catalogue()
{
	static FCatalogue C;
	if (C.bLoaded) return C;
	C.bLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Parts/parts.json"))) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque shed: no parts catalogue (Data/Parts/parts.json)"));
		return C;
	}
	for (const auto& KindKV : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Of = nullptr;
		if (!KindKV.Value.IsValid() || !KindKV.Value->TryGetObject(Of)) continue;
		const FName Kind(*KindKV.Key);
		TArray<FName>& List = C.ByKind.FindOrAdd(Kind);
		for (const auto& PartKV : (*Of)->Values)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (!PartKV.Value.IsValid() || !PartKV.Value->TryGetObject(P)) continue;
			FPartInfo Info;
			Info.Kind = Kind;
			(*P)->TryGetStringField(TEXT("name"), Info.Name);
			(*P)->TryGetStringField(TEXT("description"), Info.Description);
			(*P)->TryGetStringField(TEXT("material"), Info.Material);
			const TArray<TSharedPtr<FJsonValue>>* Col = nullptr;
			if ((*P)->TryGetArrayField(TEXT("color"), Col) && Col->Num() >= 3)
				Info.Colour = FLinearColor((float)(*Col)[0]->AsNumber(), (float)(*Col)[1]->AsNumber(), (float)(*Col)[2]->AsNumber());
			const TArray<TSharedPtr<FJsonValue>>* On = nullptr;
			if ((*P)->TryGetArrayField(TEXT("mounts_on"), On))
				for (const TSharedPtr<FJsonValue>& V : *On) Info.MountsOn.Add(FName(*V->AsString()));
			if (Info.Name.IsEmpty()) Info.Name = PartKV.Key;
			const FName Id(*PartKV.Key);
			C.Parts.Add(Id, Info);
			List.Add(Id);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Torque shed: parts catalogue has %d parts"), C.Parts.Num());
	return C;
}

const TCHAR* const StockPrefix = TEXT("speed_twin_");

FString MaterialOf(FName Part);
} // namespace TorqueShedPartsImpl

namespace TorqueShedParts
{
using namespace TorqueShedPartsImpl;
const TCHAR* const StockModel = TEXT("speed_twin_1200");

const TArray<FName>& Slots() { static const TArray<FName> L = TORQUE_LIST(SlotNames); return L; }
const TArray<FName>& EngineSlots() { static const TArray<FName> L = TORQUE_LIST(EngineSlotNames); return L; }
const TArray<FName>& Kinds() { static const TArray<FName> L = TORQUE_LIST(KindNames); return L; }
const TArray<FName>& StockSlots() { static const TArray<FName> L = TORQUE_LIST(StockSlotNames); return L; }

const TArray<FName>& BikeSlots()
{
	static TArray<FName> L;
	if (L.Num() == 0)
		for (const FName& S : Slots())
			if (!IsEngineSlot(S)) L.Add(S);
	return L;
}

bool IsEngineSlot(FName Slot) { return EngineSlots().Contains(Slot); }
bool IsServiceSlot(FName Slot) { static const TArray<FName> L = TORQUE_LIST(ServiceSlotNames); return L.Contains(Slot); }
bool IsLowSlot(FName Slot) { static const TArray<FName> L = TORQUE_LIST(LowSlotNames); return L.Contains(Slot); }
bool IsOptional(FName Slot) { static const TArray<FName> L = TORQUE_LIST(OptionalSlotNames); return L.Contains(Slot); }

bool MayBeEmpty(FName Slot)
{
	static const TArray<FName> Own = TORQUE_LIST(FrameOwnSlotNames);
	return IsOptional(Slot) || Own.Contains(Slot);
}

FName KindOf(FName Slot)
{
	const FString S = Slot.ToString();
	static const TCHAR* const Sides[] = {TEXT("front_"), TEXT("rear_")};
	for (const TCHAR* Pre : Sides)
	{
		if (!S.StartsWith(Pre) || Slot == FName(TEXT("front_mudguard"))) continue;
		const FName Rest(*S.Mid(FCString::Strlen(Pre)));
		if (Kinds().Contains(Rest)) return Rest;
	}
	return Slot;
}

const TArray<FName>& MountsOf(FName Slot)
{
	static const TArray<FName> None;
	const TArray<FName>* On = MountTable().Find(Slot);
	return On ? *On : None;
}

TArray<FName> Dependents(FName Slot)
{
	TArray<FName> Out;
	const bool bEngine = IsEngineSlot(Slot);
	for (const FName& S : Slots())
		if (IsEngineSlot(S) == bEngine && MountsOf(S).Contains(Slot)) Out.Add(S);
	return Out;
}

TArray<FName> SlotsForKind(FName Kind)
{
	TArray<FName> Out;
	for (const FName& S : Slots())
		if (KindOf(S) == Kind) Out.Add(S);
	return Out;
}

FString KindName(FName Kind)
{
	const TCHAR* N = Lookup(KindTitles, UE_ARRAY_COUNT(KindTitles), Kind.ToString());
	return N ? FString(N) : Kind.ToString();
}

FString SlotName(FName Slot)
{
	const FString N = KindName(KindOf(Slot));
	if (N.StartsWith(TEXT("Front "))) return N;   // the front mudguard is front by kind already
	const FString S = Slot.ToString();
	if (S.StartsWith(TEXT("front_"))) return TEXT("Front ") + N.ToLower();
	if (S.StartsWith(TEXT("rear_"))) return TEXT("Rear ") + N.ToLower();
	return N;
}

FName TyreOf(FName WheelSlot)
{
	if (WheelSlot == FName(TEXT("front_wheel"))) return FName(TEXT("front_tyre"));
	if (WheelSlot == FName(TEXT("rear_wheel"))) return FName(TEXT("rear_tyre"));
	return NAME_None;
}

bool IsTyreSlot(FName Slot) { return Slot == FName(TEXT("front_tyre")) || Slot == FName(TEXT("rear_tyre")); }

FName StockPart(FName Slot) { return FName(*(FString(StockPrefix) + Slot.ToString())); }
bool IsStock(FName Part) { return Part.ToString().StartsWith(StockPrefix); }
FName StockSlot(FName Part) { return IsStock(Part) ? FName(*Part.ToString().Mid(FCString::Strlen(StockPrefix))) : NAME_None; }

bool IsPart(FName Part) { return IsStock(Part) ? Slots().Contains(StockSlot(Part)) : Catalogue().Parts.Contains(Part); }

FName PartKind(FName Part)
{
	if (IsStock(Part)) return KindOf(StockSlot(Part));
	const FPartInfo* P = Catalogue().Parts.Find(Part);
	return P ? P->Kind : NAME_None;
}

FString PartName(FName Part)
{
	if (IsStock(Part))
	{
		// "Speed Twin rear wheel"
		const FName Slot = StockSlot(Part);
		return TEXT("Speed Twin ") + SlotName(Slot).ToLower();
	}
	const FPartInfo* P = Catalogue().Parts.Find(Part);
	return P ? P->Name : Part.ToString();
}

FString PartDescription(FName Part)
{
	if (IsStock(Part)) return TEXT("As it came on the Triumph Speed Twin 1200.");
	const FPartInfo* P = Catalogue().Parts.Find(Part);
	return P ? P->Description : FString();
}

FLinearColor DefaultColour(FName Part)
{
	const FPartInfo* P = Catalogue().Parts.Find(Part);
	return P ? P->Colour : FLinearColor(0.5f, 0.5f, 0.5f);
}

TArray<FName> PartsOfKind(FName Kind)
{
	const TArray<FName>* L = Catalogue().ByKind.Find(Kind);
	return L ? *L : TArray<FName>();
}

TArray<FName> PartMounts(FName Slot, FName Part)
{
	TArray<FName> Out = MountsOf(Slot);
	if (const FPartInfo* P = Catalogue().Parts.Find(Part))
		for (const FName& M : P->MountsOn) Out.AddUnique(M);
	return Out;
}

} // namespace TorqueShedParts

FString TorqueShedPartsImpl::MaterialOf(FName Part)
{
	const FPartInfo* P = Catalogue().Parts.Find(Part);
	if (P && !P->Material.IsEmpty()) return P->Material;
	const TCHAR* M = Lookup(KindMaterials, UE_ARRAY_COUNT(KindMaterials), PartKind(Part).ToString());
	return M ? FString(M) : FString(TEXT("mixed"));
}

namespace TorqueShedParts
{
using namespace TorqueShedPartsImpl;

FName BinFor(FName Part)
{
	const FString M = MaterialOf(Part);
	return (M == TEXT("metal") || M == TEXT("plastic")) ? FName(*M) : FName(TEXT("general"));
}

FString MaterialName(FName Part)
{
	const FString M = MaterialOf(Part);
	const TCHAR* N = Lookup(MaterialTitles, UE_ARRAY_COUNT(MaterialTitles), M);
	return N ? FString(N) : M;
}

const TArray<FName>& Bins()
{
	static const TCHAR* const Names[] = {TEXT("general"), TEXT("plastic"), TEXT("metal")};
	static const TArray<FName> L = TORQUE_LIST(Names);
	return L;
}

FString BinName(FName Bin)
{
	if (Bin == FName(TEXT("plastic"))) return TEXT("Plastic recycling");
	if (Bin == FName(TEXT("metal"))) return TEXT("Metal recycling");
	return TEXT("General waste");
}

FName SlotOfModelPart(FName ModelPart)
{
	const FName* S = ModelPartSlots().Find(ModelPart);
	return S ? *S : NAME_None;
}

FName SlotOfModelPart(const FTorqueResolvedPart& P)
{
	if (const FName* S = ModelPartSlots().Find(P.Name)) return *S;
	if (P.Slot.IsNone()) return FName(TEXT("frame"));
	// An option's part: by its fit slot.
	if (P.Slot == FName(TEXT("tyres")))
		return P.Group == FName(TEXT("rear_wheel")) ? FName(TEXT("rear_tyre")) : FName(TEXT("front_tyre"));
	if (const TCHAR* S = Lookup(FitSlotToSlot, UE_ARRAY_COUNT(FitSlotToSlot), P.Slot.ToString())) return FName(S);
	return P.Slot;
}

bool ModelOption(FName Part, FName Kind, FName& OutFitSlot, FName& OutOption)
{
	if (IsStock(Part)) return false;
	const FName Model(StockModel);
	for (const FName& FitSlot : TorqueFit::Slots(Model))
		for (const FTorqueFitOption& O : TorqueFit::Options(Model, FitSlot))
			if (!O.bStock && O.CatalogueId == Part && (Kind.IsNone() || O.CatalogueKind == Kind))
			{
				OutFitSlot = FitSlot;
				OutOption = O.PartId;
				return true;
			}
	return false;
}
} // namespace TorqueShedParts

#undef TORQUE_LIST
