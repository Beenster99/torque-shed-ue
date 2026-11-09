// What the ridden bike bumps into besides the ground: upright boxes (the ride's drystone walls,
// buildings, gates and props, Tools/make_collision.py) and other riders' bikes (capsules, set each
// frame). The Godot game's bike was a Jolt body with a frame-and-engine box, a rider shape and wheel
// bumpers, kept out of the walls and the other bikes by the physics engine; here the sim keeps its
// own probe spheres out of these (BikeSim::ResolveContacts), so it stays the one integrator.
#pragma once

#include "SimMath.h"
#include <unordered_map>
#include <vector>

namespace TorqueSim
{
// An upright box: centre (Cx, Cz) on the ground plane, its first axis (Ux, Uz) (unit, level), half
// lengths along that axis and the one square to it, and its height range. Sim frame, metres.
struct ContactBox
{
	double Cx = 0.0, Cz = 0.0, Ux = 1.0, Uz = 0.0, Hu = 0.5, Hv = 0.5, Y0 = 0.0, Y1 = 1.0;
};

// Another rider's bike, as a capsule from its rear wheel to its front, up to the rider's shoulders
// (two capsules: the bike low, the rider high), moving at Velocity.
struct ContactCapsule
{
	V3 A, B;
	double Radius = 0.3;
	V3 Velocity;
	bool Rider = false;
};

struct ContactHit
{
	V3 Normal;          // out of what was hit, towards the probe
	double Depth = 0.0; // how far the probe sphere is into it
	V3 OtherVelocity;   // what was hit is moving (another bike)
	bool Bike = false;
};

class ContactWorld
{
public:
	void SetBoxes(const std::vector<ContactBox>& InBoxes);
	int BoxCount() const { return (int)Boxes.size(); }
	// Other bikes, replaced every frame by the game.
	std::vector<ContactCapsule> Capsules;
	// Everything the sphere at Centre, of Radius, is into.
	void Query(const V3& Centre, double Radius, std::vector<ContactHit>& Out) const;

private:
	static constexpr double CELL = 8.0;
	std::vector<ContactBox> Boxes;
	std::unordered_map<long long, std::vector<int>> Grid;
	static long long Key(int I, int J) { return ((long long)I << 32) ^ (long long)(unsigned int)J; }
};
} // namespace TorqueSim
