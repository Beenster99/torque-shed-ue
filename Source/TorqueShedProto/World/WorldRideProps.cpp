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

const char* RidePlan::PropName(int Kind)
{
	static const char* Names[PropKinds] = {"post_box", "seat", "bus_stop", "milestone", "phone_box", "power_pole", "field_gate", "street_lamp", "stile", "kissing_gate",
		"lift_gate", "bollard", "picnic_table", "info_board"};
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
	default: return -1;   // (the signs, the cattle grids, the pubs' signs and the crossings' barriers stay code-made)
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
		const int Kind = PropOf(F.Kind);
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
		const bool InLine = F.Kind == FieldGate || F.Kind == Stile || F.Kind == KissingGate;
		const P2 Front = InLine ? P2(F.Face.Z, -F.Face.X) : F.Face;
		P.Yaw = float(std::atan2(Front.Z, Front.X));
		P.Scale = 1.0f;
		Out.push_back(P);
	}
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
