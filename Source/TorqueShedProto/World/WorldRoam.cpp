#include "WorldRoam.h"
#include "WorldRide.h"

namespace TorqueWorld
{
std::shared_ptr<const RoamArea> RoamOfPlan(const std::shared_ptr<const RidePlan>& Plan, int Version)
{
	std::shared_ptr<RoamArea> Out = std::make_shared<RoamArea>();
	Out->Version = Version;
	if (Plan == nullptr) return Out;
	Out->Area = std::shared_ptr<const LoadedArea>(Plan, &Plan->Area);
	const Frame F = Plan->Area.Roads.FrameOf();
	for (const CellId& C : Plan->Area.Cells)
	{
		const Geo Mid = CellMiddle(C);
		const P2 A = F.FromGeo(Mid.Lat + CellDeg * 0.5, Mid.Lon - CellDeg * 0.5), B = F.FromGeo(Mid.Lat - CellDeg * 0.5, Mid.Lon + CellDeg * 0.5);
		Rect R;
		R.X0 = A.X; R.Z0 = A.Z; R.X1 = B.X; R.Z1 = B.Z;
		if (Out->Box.Empty()) Out->Box = R;
		else Out->Box.Merge(R);
	}
	return Out;
}

std::shared_ptr<const RideGuide> GuideOfPlan(const RidePlan& Plan, int Version)
{
	std::shared_ptr<RideGuide> Out = std::make_shared<RideGuide>();
	Out->Version = Version;
	std::vector<P2> Line;
	Line.reserve(Plan.Road.size());
	for (const RoadPoint& P : Plan.Road) Line.push_back(P.At);
	Out->Line = Resample(Line, RideGuide::GuideStep);
	const Frame F = Plan.Area.Roads.FrameOf();
	for (size_t I = 1; I < Plan.Request.Pins.size(); ++I) Out->Waypoints.push_back(F.FromGeo(Plan.Request.Pins[I].Lat, Plan.Request.Pins[I].Lon));
	if (!Out->Line.empty()) Out->Finish = Out->Line.back();
	return Out;
}
std::shared_ptr<const RideGuide> GuideOfDetour(const RidePlan& Plan, const RideDetour& Back, int Version)
{
	std::shared_ptr<RideGuide> Out = std::make_shared<RideGuide>();
	Out->Version = Version;
	std::vector<P2> Line = Back.Line;
	for (const RoadPoint& P : Plan.Road)
		if (P.Along > Back.RejoinAlong) Line.push_back(P.At);
	Out->Line = Resample(Line, RideGuide::GuideStep);
	const Frame F = Plan.Area.Roads.FrameOf();
	for (size_t I = 1; I < Plan.Request.Pins.size(); ++I) Out->Waypoints.push_back(F.FromGeo(Plan.Request.Pins[I].Lat, Plan.Request.Pins[I].Lon));
	if (!Out->Line.empty()) Out->Finish = Out->Line.back();
	return Out;
}
} // namespace TorqueWorld
