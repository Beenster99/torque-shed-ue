// [everyday kits stage 8] The fast road set: the steel and concrete barriers of the dual carriageways and motorways laid
// as models (Tools/blender_fastroad_props.py, each with two hand-made distance versions) instead of drawn as faces: the
// central reservation's double-beam barrier and the high embankments' verge barrier (the road build, RoadPieceIn), the
// map's guard rails and concrete barriers (the scenery). Their gantries, speed cameras, emergency phones and marker
// posts are found by IndexTownLife (World/WorldRideTownLife.cpp) and laid by RoadsideProps. Always on (beenster, Nov 26:
// no more switches); the barriers are drawn as before only where the game has no models for them.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace TorqueWorld
{
bool RidePlan::FastBarriersOn() const
{
	return PropModelled[PropFastBarrier] && PropModelled[PropFastConcrete];
}

void RidePlan::LayBarrierRun(std::vector<RideProp>& Props, double OriginX, double OriginZ, int Kind, int Variant, const P2& A, double Ya, const P2& B, double Yb)
{
	const double Len = A.DistanceTo(B);
	if (Len < 0.2 || std::isnan(Ya) || std::isnan(Yb)) return;
	// (whole 4 m lengths, as many as fit, each stretched a little to fill its share)
	const int N = std::max(1, int(std::lround(Len / 4.0)));
	const double Each = Len / double(N);
	const P2 Dir = (B - A) * (1.0 / Len);
	for (int K = 0; K < N; ++K)
	{
		const double T0 = double(K) / double(N), T1 = double(K + 1) / double(N);
		const P2 Mid = A + (B - A) * ((T0 + T1) * 0.5);
		const double Y0 = Ya + (Yb - Ya) * T0, Y1 = Ya + (Yb - Ya) * T1;
		RideProp Pr;
		Pr.Kind = uint16_t(Kind);
		Pr.Variant = uint8_t(Variant);
		Pr.Mapped = false;
		Pr.X = float(Mid.X - OriginX);
		Pr.Y = float((Y0 + Y1) * 0.5);
		Pr.Z = float(Mid.Z - OriginZ);
		Pr.Yaw = float(std::atan2(Dir.Z, Dir.X));
		Pr.Pitch = float(std::atan2(Y1 - Y0, Each));
		Pr.Stretch = float(Each / 4.0);
		Props.push_back(Pr);
	}
}

bool RidePlan::InWholeRim(const P2& At) const
{
	if (!WholeJunctions || WholeCells.empty()) return false;
	const auto It = WholeCells.find(static_cast<long long>(std::floor(At.X / 64.0)) * 4000037LL + static_cast<long long>(std::floor(At.Z / 64.0)));
	if (It == WholeCells.end()) return false;
	for (int Ji : It->second)
	{
		const std::shared_ptr<const WholeShape> Sh = WholeShapeOf(Ji);
		if (Sh != nullptr && Sh->Inside(At)) return true;
	}
	return false;
}
} // namespace TorqueWorld
