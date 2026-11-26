// Ride UK's wild birds (beenster Nov 26: "more birds flying ... flying out of bushes or trees as we
// approach. Not too often but enough to remind us they are there. Occasional birds of prey perched on
// tree tops"). Plain C++ (SimTest/bird_test): where every bird is and what pose it is in; the game
// (World/TorqueRideBirds) draws them with Tools/make_ride_birds.py's models, as instances.
//
//  - out of the hedges and trees by the road as a rider comes by: a blackbird low across the road, a
//    handful of finches or house sparrows, wood pigeons clattering out of a tree, a pheasant up out of the
//    verge (farmland, mostly autumn and winter), now and then a crow;
//  - perched on tree tops near the road: buzzards, sometimes a kestrel; some take off as a rider passes;
//  - kestrels hovering over the verges and rough grass;
//  - soaring: buzzards, red kites where kites live, gulls by the sea, circling their thermals;
//  - flocks crossing the sky: rooks, starlings, wood pigeons, gulls, and greylag skeins in the winter half.
//
// All of it by the month, the hour, the light and the rain. Co-op: every machine works it out alone from
// the same things (the shared server clock, every rider's place, the same world), so everyone sees the
// same birds in the same places: the chances hang on fixed squares of the map and the clock, not on dice.
#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace TorqueWorld
{
enum class BirdSpecies : uint8_t { Buzzard, RedKite, Kestrel, Gull, WoodPigeon, Rook, Starling, Blackbird, Chaffinch, Sparrow, Pheasant, Goose, Count };
// (Tools/make_ride_birds.py's POSES_ALL, in this order)
enum class BirdPose : uint8_t { Flap0, Flap1, Flap2, Flap3, Glide, Closed, Perch, Count };

const char* BirdSpeciesName(BirdSpecies S);   // "buzzard", "red_kite", ... as the models are named
const char* BirdPoseName(BirdPose P);         // "flap0", ... "perch"

// Somewhere a bird might be sitting: a hedge or bush (Type 0), a broadleaf tree (1), a conifer (2), a
// garden bush or tree (3). The ride's frame (x east, y up, z south; m); Top its height above Y.
struct BirdPerch
{
	float X = 0.0f, Y = 0.0f, Z = 0.0f, Top = 0.0f;
	uint8_t Type = 0;
};

struct BirdRider
{
	double X = 0.0, Y = 0.0, Z = 0.0, Vx = 0.0, Vz = 0.0;
};

// What the place and the day are like round the riders (RidePlan::SoundsAt's shares, 0..1).
struct BirdPlace
{
	double Sea = 0.0, Water = 0.0, Town = 0.0, Farm = 0.0;
	bool Kites = false;          // a region red kites live in
	int Month = 6;               // 1..12
	double Hour = 12.0;          // the clock
	double Light = 1.0;          // daylight 0 (night) .. 1
	double Rain = 0.0;           // 0..1
	double WindFrom = 225.0;     // degrees, the way the wind blows from (0 north, 90 east)
};

// One bird to draw: its model (species, pose) and where (the ride's frame, m), which way it faces
// (Yaw radians: its front along (cos, sin) in x east, z south), its nose up (Pitch) and bank (Roll,
// radians, + its right wing down).
struct BirdDraw
{
	BirdSpecies Species = BirdSpecies::Buzzard;
	BirdPose Pose = BirdPose::Glide;
	double X = 0.0, Y = 0.0, Z = 0.0, Yaw = 0.0, Pitch = 0.0, Roll = 0.0;
};

class RideBirds
{
public:
	// The perches of a square of the map (its key the game's own), and letting them go.
	void AddPerches(int64_t Key, std::vector<BirdPerch>&& Perches);
	void DropPerches(int64_t Key);
	void Clear();

	// Now: the shared clock (s). Ground: the ground's height at (x, z) (NaN where it isn't known).
	void Tick(double Now, const std::vector<BirdRider>& Given, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	const std::vector<BirdDraw>& Draws() const { return Out; }

	// (for the log and the test) how many of each kind of sighting so far
	struct Counts { int Flushes = 0, FlushBirds = 0, Perched = 0, TookOff = 0, Hovers = 0, Soarers = 0, Flocks = 0, FlockBirds = 0; double NearestAhead = 1e9; };
	const Counts& Seen() const { return Count; }
	int Alive() const { return int(Birds.size()); }

	// (tuning; the test reads them)
	static constexpr double FlushCell = 200.0;   // m: one chance of a flush per square this size
	static constexpr double PerchCell = 450.0;   // m: one chance of a perched bird of prey
	static constexpr double HoverCell = 600.0;
	static constexpr double ThermalCell = 700.0;
	static constexpr double FlockEvery = 75.0;   // s
	static constexpr int MostBirds = 110;

private:
	enum class Mode : uint8_t { Path, Perched, Hover, Soar };
	struct Bird
	{
		BirdSpecies Species = BirdSpecies::Blackbird;
		Mode How = Mode::Path;
		double T0 = 0.0, Dur = 1.0;
		double P[4][3] = {};           // a path: a cubic from P[0] to P[3]
		double Seed = 0.0;             // 0..1, its own
		double Flap = 10.0;            // wingbeats a second
		double Wave = 0.0;             // undulation (m) and its wavelength along the path
		double WaveLen = 6.0;
		int64_t Owner = 0;             // the event (a cell or a slot) it belongs to
		double Yaw = 0.0;              // perched or hovering: which way it faces
		double Cx = 0.0, Cy = 0.0, Cz = 0.0, Radius = 50.0, Speed = 9.0;   // a soarer's circle, a hover's spot
		bool Leaving = false;
	};
	std::vector<Bird> Birds;
	std::vector<BirdDraw> Out;
	std::unordered_map<int64_t, std::vector<BirdPerch>> Perches;
	std::unordered_map<int64_t, double> Fired;     // a flush, perch or hover cell: when it was used
	std::unordered_map<int64_t, bool> Thermals;    // the thermal cells with a bird now
	int64_t LastFlockSlot = -1;
	double Last = -1.0;
	double SinceLook = 1e9;
	Counts Count;

	void LookForFlushes(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	void LookForPerched(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	void LookForHovers(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	void KeepSoarers(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	void LookForFlock(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground);
	void Flush(double Now, const BirdPerch& From, const BirdRider& Rider, const BirdPlace& Place, uint32_t Salt, int64_t Cell, const std::function<double(double, double)>& Ground);
	void Draw(double Now, Bird& B, const std::vector<BirdRider>& Riders, bool& bDone);
	void PerchesNear(double X, double Z, double R, std::vector<const BirdPerch*>& Found) const;
	double Activity(const BirdPlace& Place) const;
};
} // namespace TorqueWorld
