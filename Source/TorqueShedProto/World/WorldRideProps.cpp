// The prop layer (OSM stage 2): what stands by the road drawn from real models, as the trees are.
// IndexFurniture (WorldRideScenery.cpp) finds where each thing stands from the map (or by rule) and
// FurnitureTile draws a code-made shape for each; here a square's things of the kinds the kit has
// models for become props, for the game to draw as instances of those models (World/TorqueRideWorld,
// the RideUK level's kit actor tagged RideProps, made by Tools/ue_build_rideuk.py from
// Data/RideUK/prop_kit.json). A kind the kit has no model for keeps its code-made shape, so the ride
// looks as it did with no props installed.
#include "WorldRide.h"

#include "FitJson.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RidePropBits
{
constexpr double PropCell = 256.0;   // as the scenery's squares (SceneCell)
} // namespace RidePropBits

int RidePlan::WallStyle(const LocalLook& Look)
{
	const std::string N = Look.Name;
	if (N == "dales") return 0;
	if (N == "cotswolds") return 1;
	if (N == "lakes" || N == "wales") return 2;
	if (N == "peak" || N == "lowlands") return 3;
	if (N == "highlands" || N == "ulster" || N == "cornwall") return 4;
	return 5;
}

const char* RidePlan::PropName(int Kind)
{
	static const char* Names[PropKinds] = {"post_box", "seat", "bus_stop", "milestone", "phone_box", "power_pole", "field_gate", "street_lamp", "stile", "kissing_gate",
		"lift_gate", "bollard", "picnic_table", "info_board",
		"win_sash6", "win_sash2", "win_casement", "win_casement_top", "win_mullion", "win_mullion_leaded", "sur_stone", "sur_brick", "sur_plain",
		"door_panel6", "door_victorian", "door_ledged", "door_ledged_light", "door_modern", "door_double", "door_hood",
		"bay_canted", "bay_square", "wall_stone", "wall_end", "lamp_heritage", "parked_car", "traffic_signal",
		"armco", "tyre_wall", "pit_wall", "marshal_post", "braking_board", "start_gantry", "grandstand",   // [circuits]
		"touring_caravan", "static_caravan",   // [caravans]
		"farm_bales", "farm_bale", "farm_clamp", "farm_tank", "farm_feedbin", "farm_trough", "farm_tractor", "farm_trailer", "farm_diesel", "farm_muck",   // [everyday kits stage 4]
		"farm_tractor_utility", "farm_tractor_compact",   // [farm pack tractors]
		"play_swings", "play_slide", "play_climber", "play_springer", "play_roundabout", "play_seesaw", "play_fence",
		"pitch_goal", "pitch_posts", "tennis_net", "court_fence", "basket_hoop", "notice_board", "litter_bin", "village_sign",   // [everyday kits stage 6]
		"power_pylon", "power_mast", "power_monopole", "power_turbine", "power_rotor", "power_watertower", "power_kiosk",
		"power_transformer", "power_gantry", "power_palisade",   // [everyday kits stage 9]
		"coast_beachhut", "coast_seawall", "coast_promrail", "coast_lifebuoy",   // [everyday kits stage 10]
		"road_burgervan", "road_flag", "road_laybysign", "road_cafesign",   // [everyday kits stage 5b]
		"bridge_arch", "bridge_pier", "bridge_abutment", "bridge_rail",   // [everyday kits stage 7]
		"farm_combine", "farm_forager", "farm_sprayer", "farm_tractor4wd", "farm_tractortrack", "farm_crawler",
		"farm_loader", "farm_mower", "farm_buggy", "farm_chaser", "farm_tipper", "farm_baler", "farm_drill",
		"farm_cultivator",   // [farm machinery]
		"house_bike"};   // [Nov 26] (our own bikes outside houses)
	return Kind >= 0 && Kind < PropKinds ? Names[Kind] : "";
}

int RidePlan::PropOf(int FurnishKind)
{
	switch (FurnishKind)
	{
	case PostBox: return PropPostBox;
	case Seat: return PropSeat;
	case BusStop: return PropBusStop;
	case Milestone: return PropMilestone;
	case PhoneBox: return PropPhoneBox;
	case PowerPole: return PropPowerPole;
	case FieldGate: return PropFieldGate;
	case StreetLamp: return PropStreetLamp;
	case Stile: return PropStile;
	case KissingGate: return PropKissingGate;
	case LiftGate: return PropLiftGate;
	case Bollard: return PropBollard;
	case PicnicTable: return PropPicnicTable;
	case InfoBoard: return PropInfoBoard;
	case TrafficSignal: return PropTrafficSignal;
	default: return -1;   // (the signs, the cattle grids, the pubs' signs, the beacons and the crossings' barriers stay code-made)
	}
}

void RidePlan::PropsTile(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	using namespace RidePropBits;
	Out.clear();
	if (!Scene) return;
	bool Any = false;
	for (bool M : PropModelled) Any = Any || M;
	if (!Any) return;
	const int CellX0 = int(std::floor(X0 / PropCell)), CellX1 = int(std::floor((X0 + Size - 0.001) / PropCell));
	const int CellZ0 = int(std::floor(Z0 / PropCell)), CellZ1 = int(std::floor((Z0 + Size - 0.001) / PropCell));
	std::vector<const Furnish*> Here;
	for (int Cz = CellZ0; Cz <= CellZ1; ++Cz)
		for (int Cx = CellX0; Cx <= CellX1; ++Cx)
		{
			const auto It = Scene->Furniture.find(static_cast<long long>(Cx) * 4000037LL + static_cast<long long>(Cz));
			if (It == Scene->Furniture.end()) continue;
			for (int Index : It->second) Here.push_back(&Furniture[size_t(Index)]);
		}
	// [ride anywhere 4] and the network's roads' own
	std::vector<std::shared_ptr<const std::vector<Furnish>>> Held;
	NetworkFurniture(X0, Z0, Size, Held, Here);
	for (const Furnish* Each : Here)
	{
		const Furnish& F = *Each;
		if (F.At.X < X0 || F.At.X >= X0 + Size || F.At.Z < Z0 || F.At.Z >= Z0 + Size) continue;
		int Kind = PropOf(F.Kind);
		if (Kind == PropStreetLamp && HeritageLampAt(F.At.X, F.At.Z)) Kind = PropLampHeritage;   // [towns stage 6]
		if (Kind < 0 || !PropModelled[Kind] || std::isnan(F.Height)) continue;
		RideProp P;
		P.Kind = uint16_t(Kind);
		P.Mapped = F.Mapped;
		P.Id = F.Id != 0 ? F.Id : static_cast<uint32_t>(std::lround(F.At.X * 7.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(F.At.Z * 7.0)) * 19349663u;
		P.Variant = uint8_t((P.Id >> 8) & 0xff);
		P.X = float(F.At.X - X0);
		P.Z = float(F.At.Z - Z0);
		P.Y = float(F.Height);
		// Its front (the model's +x) to the road, or for a thing that stands in a line (a gate,
		// a stile) square to the line: the line runs along Face, so the front is across it.
		// ([Nov 19, beenster: "turn the telegraph poles to the direction of cable travel"] a telegraph
		// pole too: its wires run along Face, and the model's cross arm lies along its x, so its front is
		// across them and the arm square to the wires, its stay along them)
		const bool InLine = F.Kind == FieldGate || F.Kind == Stile || F.Kind == KissingGate || F.Kind == PowerPole;
		const P2 Front = InLine ? P2(F.Face.Z, -F.Face.X) : F.Face;
		P.Yaw = float(std::atan2(Front.Z, Front.X));
		P.Scale = 1.0f;
		Out.push_back(P);
	}
	CircuitProps(X0, Z0, Size, Out);   // [circuits]
	TownLifeProps(X0, Z0, Size, Out);   // [caravans]
	FarmYardProps(X0, Z0, Size, Out);   // [everyday kits stage 4]
	FieldGateProps(X0, Z0, Size, Here, Out);   // [farm machinery] (while Held keeps the network's gates)
	VillageProps(X0, Z0, Size, Out);   // [everyday kits stage 6]
	CoastProps(X0, Z0, Size, Out);   // [everyday kits stage 10]
	RoadsideProps(X0, Z0, Size, Out);   // [everyday kits stage 5b]
}

bool RidePlan::ReadPropKit(const std::string& Text, std::vector<PropKitRow>& Out)
{
	Out.clear();
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const TorqueFit::Json* Kinds = Root.Get("kinds");
	if (Kinds == nullptr || !Kinds->IsObject()) return false;
	for (int K = 0; K < PropKinds; ++K)
	{
		const TorqueFit::Json* Row = Kinds->Get(PropName(K));
		if (Row == nullptr || !Row->IsObject()) continue;
		PropKitRow R;
		R.Kind = PropName(K);
		if (const TorqueFit::Json* Words = Row->Get("words"))
			if (Words->IsArray())
				for (const TorqueFit::Json& W : Words->A) R.Words.push_back(W.S);
		R.Reach = Row->Num("reach", R.Reach);
		R.Height = Row->Num("height", R.Height);
		R.Turn = Row->Num("turn", 0.0);
		R.Drop = Row->Num("drop", 0.0);
		R.Shadow = Row->Flag("shadow", false);
		R.Credit = Row->Str("credit");
		Out.push_back(std::move(R));
	}
	return !Out.empty();
}
} // namespace TorqueWorld
