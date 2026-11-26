// [everyday kits stage 5b] The lay-by and roadside cafe set: the things IndexTownLife (World/WorldRideTownLife.cpp)
// found room for at the kits bake's lay-bys and roadside cafes (RidePlan::RoadsideThings), laid as props: the
// catering trailers, their flags, the blue lay-by signs and the cafes' boards (Tools/blender_roadside_props.py, each
// with two hand-made distance versions), and the lay-bys' litter bins and picnic tables (the village set's and the
// OSM kit's). Each square lays the ones standing in it, on the ground there. Off: -TorqueRoadside=0
// (WORLD_RIDE_ROADSIDE=0), which leaves the lay-bys unmade too.
#include "WorldRide.h"

#include <cmath>
#include <vector>

namespace TorqueWorld
{
void RidePlan::RoadsideProps(double X0, double Z0, double Size, std::vector<RideProp>& Out) const
{
	if (RoadsideThings.empty()) return;
	const auto Key = [](double X, double Z) { return static_cast<long long>(std::floor(X / 64.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 64.0)); };
	for (double Z = Z0; Z < Z0 + Size; Z += 64.0)
		for (double X = X0; X < X0 + Size; X += 64.0)
		{
			const auto It = RoadsideGrid.find(Key(X + 1.0, Z + 1.0));
			if (It == RoadsideGrid.end()) continue;
			for (int I : It->second)
			{
				const RoadsideThing& T = RoadsideThings[size_t(I)];
				if (!PropModelled[T.Kind] || T.At.X < X0 || T.At.X >= X0 + Size || T.At.Z < Z0 || T.At.Z >= Z0 + Size) continue;
				const double Y = Ground(T.At.X, T.At.Z);
				if (std::isnan(Y)) continue;
				RideProp P;
				P.Kind = uint16_t(T.Kind);
				P.Id = T.Id;
				P.Variant = uint8_t(T.Variant & 0xff);
				P.X = float(T.At.X - X0);
				P.Z = float(T.At.Z - Z0);
				P.Y = float(Y);
				P.Yaw = float(std::atan2(T.Front.Z, T.Front.X));
				Out.push_back(P);
			}
		}
}
} // namespace TorqueWorld
