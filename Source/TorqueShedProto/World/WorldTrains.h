// [trains] Trains that run on every railway line in Ride UK (beenster, Nov 20 2026: "a made up timetable (don't bother
// with trying to get a real one). All train lines should have trains (not just one for a trial) add a setting in
// gameplay to turn them off if this doesn't work well"). Plan: https://claude.ai/artifact/S6xACsnVDrMdQ5EG9psUoB
//
// Plain C++ (SimTest/world_ride.cpp WORLD_RIDE_TRAINS), no Unreal. Nothing is simulated across the country: a train
// is worked out from the clock. For any stretch of line a rule says when trains pass, which way, how fast; when a
// rider is near, the game asks where the trains are now and draws them there. Every machine in co-op asks with the
// same (the server's) clock, so everyone sees the same train in the same place with nothing sent.
//
//   Build      the ride's railways (RidePlan::Rails, as RailTile draws them) as Routes a train can follow: the map's
//              pieces joined end to end through their points the straightest way (never round a sharp turn into a
//              siding or a headshunt), each place on a route the drawn track's own (pushed off a road, its ballast's
//              height, on a bridge, in a tunnel or across a road); a route that leaves another at a junction led in
//              and out along it for LeadM, so its trains come from somewhere. Each route classed (main, secondary,
//              branch, freight only, heritage; none for sidings, yards, miniature and narrow gauge lines), double
//              track found (a parallel route beside it: each carries one way, keeping to the left), electrified
//              guessed, its stations' stops found.
//   Services   the made-up timetable: per class, which trains run how often in the day, the evening and the night,
//              each line's times offset by a fixed scramble of its own so they don't all pass on the hour.
//   Near       the trains within reach of a place now: each car's place, facing and lean (the cant of a curve).
//   Closed     whether a level crossing on a route should be closed now (a train within Lead seconds of it, or on
//              it): every crossing gives its full warning, since it's worked out from the timetable, not from a
//              train that has appeared.
//
// The made-up numbers (how often, how fast, how long the stops) are first guesses to tune: Services() and Specs().
#pragma once

#include "WorldGeo.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
class RidePlan;
namespace Trains
{
enum class LineClass : unsigned char { None, Main, Secondary, Branch, Freight, Heritage };
const char* ClassName(LineClass C);

// The train family (Tools/props/make_trains.py's models).
enum class Kind : unsigned char { Dmu, Emu, Intercity, Freight, SteamTank, SteamBig, OldDiesel, Count };
const char* KindName(Kind K);

// One vehicle's model (a car, a locomotive, a wagon): its prop id (Data/Props/Blender/<Id>.glb), its length over the
// buffers and its bogies' (or wheels') distance from its middle (m), its body's width and height for the plain box
// drawn while the model isn't in, and whether it has a cab (headlights) at its front.
enum class Model : unsigned char
{
	DmuCar, EmuCab, EmuMid, IcPower, IcCoach, FreightLoco, WagonBox, WagonHopper, WagonTank, SteamTank, SteamBig, SteamTender, CoachOld, DieselOld, Count
};
struct ModelSpec
{
	const char* Id;
	double Length, Bogie, Width, Height;
	bool Cab;
};
const ModelSpec& SpecOf(Model M);

static constexpr double RailTop = 0.27;   // the rails' tops over the ballast's top (RidePlan::IndexRails: flush with a road)
static constexpr double LeadM = 3000.0;   // a branch's trains led in and out along the line it leaves this far
static constexpr double CarGap = 1.0;     // between two vehicles' bodies (couplings, buffers)

struct Stop
{
	int At = 0;            // the sample the train's middle stops at
	std::string Name;
	bool Major = false;    // an intercity train stops here too
	bool Halt = false;
};

struct Route
{
	std::string Name;
	LineClass Class = LineClass::None;
	bool Double = false;   // a parallel track beside it (it carries one way)
	int Way = 0;           // +1 trains run along it (first to last sample), -1 back along it, 0 both ways (single track)
	bool Electric = false;
	bool OpenStart = true, OpenEnd = true;   // a train runs on off its end (false: buffers, it stops and stands there)
	uint32_t Seed = 0;
	// A sample every ~RailStep metres: where (the drawn track's middle), its ballast's top, what it's on (RailLine::On),
	// how far along (m), how much it bends (1/radius, + to the left), its cant (radians, + down to the left), the line
	// speed there (m/s, for the train's class's fastest), and the drawn railway's line and point it's on.
	std::vector<P2> At;
	std::vector<float> Top, Bend, Cant;
	std::vector<unsigned char> On;
	std::vector<double> S;
	std::vector<int> Rail, Point;
	int OwnFrom = 0, OwnTo = 0;   // its own samples (the rest lead in from, and out onto, the line it leaves)
	std::vector<Stop> Stops;
	double X0 = 0.0, Z0 = 0.0, X1 = 0.0, Z1 = 0.0;   // its box
	double Length() const { return S.empty() ? 0.0 : S.back(); }
};

// A timetabled service on a route: which train, which way, how often (its slots every Period seconds from Phase, of
// which every Keep[band]'th runs: 1 all, 2 every other, 0 none), how fast it goes and where it stops.
struct Service
{
	Kind Train = Kind::Dmu;
	int Dir = 1;                 // along the route (+1) or back along it (-1)
	double Period = 3600.0, Phase = 0.0;
	int Keep[3] = {1, 1, 0};     // day (6-19), evening (19-23), night
	bool Heritage = false;       // only 10:00 to 17:00, March to October
	double VMax = 30.0, Accel = 0.5, Brake = 0.6;
	bool AllStops = true, MajorStops = false;
	// Worked out once: when the train's front reaches each sample (s after its slot) and leaves it (after a stop),
	// its speed there, and how long the whole run takes until its back is off the route.
	std::vector<double> Arr, Dep;
	std::vector<float> V;
	double FrontFrom = 0.0;      // where (along its way, m) the front starts
	double Total = 0.0;
	double TrainLength = 0.0;
	std::vector<Model> Consist;
	std::vector<char> Flip;      // per vehicle: runs back to front (a unit's rear cab, the second power car)
	uint32_t Livery = 0;
};

// One vehicle of a train now: its middle (m: x east, y up, z south, at the rails' top), its facing (a unit way in the
// ride's frame, pitched: Fx, Fy, Fz) and its lean (radians, + down to the left), the model, whether it runs back to
// front (a rear cab, a reversed loco), and the train's speed.
struct CarNow
{
	double X = 0.0, Y = 0.0, Z = 0.0;
	double Fx = 0.0, Fy = 0.0, Fz = 1.0;
	double Roll = 0.0;
	Model Is = Model::DmuCar;
	bool Reversed = false;
	bool Lead = false, Tail = false;   // the train's first and last vehicles
	bool InTunnel = false;
};
struct TrainNow
{
	uint64_t Id = 0;              // the same for this run of this train while it's in sight (route, service, slot)
	int Route = 0, Service = 0;
	Kind Train = Kind::Dmu;
	uint32_t Livery = 0;
	double Speed = 0.0;           // m/s
	double Front = 0.0;           // along its way (m)
	bool Standing = false;        // stopped at a platform (or at buffers)
	bool Braking = false;
	double Dist = 1e9;            // its nearest vehicle's middle from the place asked about (m)
	std::vector<CarNow> Cars;
};

// The clock a train runs to: the server's world seconds (the same on every machine of a co-op ride), and the ride's
// hour of the day (the host's, for the timetable's day, evening and night) and day of the year (heritage seasons).
struct Clock
{
	double T = 0.0;
	double Hour = 12.0;
	int Day = 180;
};

class Network
{
public:
	// The railways of a plan as routes and their services. A few tens of ms for a big area: a worker's job.
	static std::shared_ptr<Network> Build(const RidePlan& Plan);
	std::vector<Route> Routes;
	std::vector<std::vector<Service>> Services;   // per route
	// The trains with a vehicle within Reach of (X, Z) now, nearest first.
	void Near(const Clock& Now, double X, double Z, double Reach, std::vector<TrainNow>& Out) const;
	// Whether a level crossing over railway Line's points First..Last (RidePlan::Rails) should be closed: a train's
	// front within LeadS seconds of reaching it, or a train on it. Lit (out) the seconds until the first train's front
	// gets there (0 while one is on it).
	bool Closed(const Clock& Now, int Line, int First, int Last, double LeadS, double* Until = nullptr) const;
	// Where on its route a train's vehicle at distance D along its way is (for its signals and wires): the sample.
	int SampleAt(int RouteIndex, double S) const;
	// The routes with a sample within Reach of (X, Z).
	void RoutesNear(double X, double Z, double Reach, std::vector<int>& Out) const;
	// For a signal (stage 7): whether a train is on route R's way Dir between S0 and S1 (m along the route) now.
	bool Occupied(const Clock& Now, int R, int Dir, double S0, double S1) const;
	// A route's place S metres along (interpolated): where, its rails' top, its facing (unit, in the ride's frame).
	void PlaceAt(const Route& R, double S, double& X, double& Y, double& Z, double& Dx, double& Dz, float* Cant = nullptr, unsigned char* On = nullptr) const;
	// The counts for the log: routes by class, km by class.
	std::string Summary() const;
	static int Band(double Hour);   // 0 day, 1 evening, 2 night
	static bool HeritageRuns(const Clock& Now);

private:
	// (RouteCell squares: the routes with a sample in each)
	static constexpr double RouteCell = 512.0;
	std::unordered_map<long long, std::vector<int>> Cells;
	// (a drawn railway's point -> the routes' samples on it)
	std::unordered_map<long long, std::vector<std::pair<int, int>>> OnRail;
	void Index();
	void Timetable();
	void Profile(const Route& R, Service& Sv) const;
	// The front's place along its way (m) Tau seconds into the run, and its speed there.
	static double FrontAt(const Route& R, const Service& Sv, double Tau, double& Speed, bool& Standing);
	// When (s after its slot) the front reaches distance D along its way.
	static double ReachAt(const Route& R, const Service& Sv, double D);
};

} // namespace Trains
} // namespace TorqueWorld
