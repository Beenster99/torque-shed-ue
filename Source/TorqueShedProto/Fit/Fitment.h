// The fitment system (the plan's "Fitting parts"), plain C++ shared by the game and FitTest.
//
// A bike is a tree of fit units. Each unit is one or more model meshes that fit as one piece; it
// mounts by a typed socket onto a socket of its parent (Data/Fitment/interfaces.json says which
// types meet, what sizes must agree and what adjustment the joint allows) and offers sockets to its
// children. The frame is the datum; the wheels, fork and swingarm hang from datums that come from
// the riding physics, so what you see is what rides.
//
// Every unit's meshes are modelled in place on the stock bike, so a unit lands where its socket
// puts it: World = ParentSocket(adjusted) * inverse(its mount frame on the stock bike). Swap the
// bars and the grips, levers and mirrors follow the new bar's ends; slide the headlight down the
// fork tubes and its indicators come too.
//
// Frame: metres, x forward, y up, z right (as speedtwin.json and Tools/make_speedtwin.py).
#pragma once

#include "FitJson.h"
#include "PartsDb.h"
#include "SimMath.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueFit
{
using TorqueSim::M3;
using TorqueSim::V3;

// A rigid transform: rotation (columns are the frame's axes) then translation.
struct Xf
{
	M3 R;
	V3 T;
	V3 Point(const V3& P) const { return R * P + T; }
	V3 Dir(const V3& D) const { return R * D; }
	Xf operator*(const Xf& B) const { return {R * B.R, R * B.T + T}; }
	Xf Inverse() const
	{
		const M3 Rt = R.Transposed();
		return {Rt, -(Rt * T)};
	}
	static Xf Translate(const V3& D) { Xf X; X.T = D; return X; }
	// Rotation by Angle (radians, right handed) about an axis through Point.
	static Xf Rotate(const V3& Axis, double Angle, const V3& Through = V3());
	// The largest distance any of the points moves between two transforms (an error measure).
	static double Drift(const Xf& A, const Xf& B, double Reach = 0.5);
};

struct Interface
{
	std::string Id, Name, SizeKey, Rule, Adjust;
	double ToleranceMm = 0.0, PlateMm = 0.0;
	double SlideMin = 0.0, SlideMax = 0.0, TurnMinDeg = 0.0, TurnMaxDeg = 0.0;
};

struct Socket
{
	std::string Name, Type;
	bool IsPath = false;
	Xf Frame;                        // point socket: its frame (x is the adjustment axis)
	std::vector<V3> Path;            // path socket: a centre line (bar, rails, fork tubes)
	V3 Up{0, 1, 0};
	double SizeMm = 0.0;
	bool HasSize = false;
	std::vector<V3> Points;          // a mounting pattern (engine hard points)
	std::vector<double> Holes;       // hole positions along x (rearset plates)

	double Length() const;
	Xf FrameAt(double S) const;      // path socket: the frame S metres along (x along the path)
};

struct Proxy
{
	bool IsBox = false;
	V3 A, B;                         // capsule: segment and radius
	double R = 0.0;
	V3 C, X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, H;   // box: centre, axes, half sizes
	Proxy Moved(const Xf& M) const;
};

struct Mount
{
	std::string ParentUnit, ParentSocket, Type;
	double SizeMm = 0.0;
	bool HasSize = false;
	bool HasAlong = false, HasFromEnd = false;
	double Along = 0.0, FromEnd = 0.0;
	std::vector<V3> Points;
	bool HasChild = false;           // an author fix: this unit's own mount frame
	Xf Child;
};

struct ConformEnd
{
	std::string Unit;
	V3 At;
};

struct Unit
{
	std::string Id, Group, Slot, CatKind, CatId;
	std::vector<std::string> Meshes;
	std::vector<Socket> Sockets;
	std::vector<Proxy> Proxies;
	bool HasMount = false;
	Mount Mnt;
	std::vector<ConformEnd> Conform;
	const Socket* FindSocket(const std::string& Name) const;
};

struct MeshInfo
{
	std::string Group, Finish, File;
};

struct Option
{
	std::string Slot, Id, Name, CatKind, CatId;
	bool Stock = false;
	std::vector<Unit> Units;                      // replacing the stock units with the same ids
	std::map<std::string, MeshInfo> Meshes;       // the meshes it brings
};

// Geometry mode for one unit.
struct Adjust
{
	double Slide = 0.0;
	double TurnDeg = 0.0;
	int Hole = -1;
	bool Free = false;
	Xf FreeXf;                                    // model frame, applied after the socket placement
};

struct Build
{
	std::map<std::string, std::string> Fitted;    // slot -> option id (missing or "stock": stock)
	std::map<std::string, Adjust> Adjusts;        // unit id -> geometry mode
};

struct Placed
{
	Unit U;                                       // a copy: options bring their own units
	std::string Slot, OptionId;                   // the option it came from ("stock" if none)
	Xf World;                                     // where its stock-modelled meshes go
	Xf Socket0;                                   // its parent socket's frame before adjustment (world)
	std::string AdjustKind;
};

struct Assembly
{
	std::vector<Placed> Units;
	std::vector<std::string> Notes;               // conforming parts whose ends moved, and similar
	const Placed* Find(const std::string& Id) const;
};

struct Clash
{
	std::string A, B, Case;
	double DepthMm = 0.0;
};

// What the riding physics says, in the model frame, to check the model against.
struct PhysicsDatums
{
	double Wheelbase = 0.0, RakeDeg = 0.0, ForkOffset = 0.0, FrontRadius = 0.0, RearRadius = 0.0;
	double FrontTravel = 0.0, RearTravel = 0.0;
	V3 SwingPivot;
};

struct DatumCheck
{
	std::string Name;
	double Model = 0.0, Physics = 0.0, Tolerance = 0.0;
	bool Ok = true;
};

class BikeFit
{
public:
	bool Load(const std::string& FitmentText, const std::string& InterfacesText, std::string& Error);
	// Author fixes (fitment_fixes.json) on top: a unit's mount frame, per option.
	bool ApplyFixes(const std::string& FixesText, std::string& Error);

	const std::string& BikeId() const { return Bike; }
	const std::string& BikeName() const { return Name; }
	std::vector<std::string> Slots() const;
	const std::vector<Option>* OptionsFor(const std::string& Slot) const;
	const Option* FindOption(const std::string& Slot, const std::string& Id) const;
	const Interface* FindInterface(const std::string& Id) const;
	const std::vector<Unit>& StockUnits() const { return Units; }
	const MeshInfo* FindMesh(const std::string& Name) const;   // stock or option mesh
	V3 GroupOrigin(const std::string& Group) const;

	// Whether option Id can go in Slot on this build; WhyNot says why not (or what it needs).
	bool CanFit(const Build& B, const std::string& Slot, const std::string& Id, std::string& WhyNot) const;
	bool Assemble(const Build& B, Assembly& Out, std::string& Error) const;
	// The clearance checks over every case in the data (full lock, full compression, both). Stock
	// parts were modelled in place, so for two of them only what the movement adds counts; with a
	// Baseline (the stock bike), a swapped or moved part's clashes count beyond what the stock part
	// it replaces already had. DepthMm is that extra depth.
	std::vector<Clash> Clearance(const Assembly& A, const Assembly* Baseline = nullptr) const;
	std::vector<DatumCheck> CheckDatums(const Assembly& A, const PhysicsDatums& P) const;

	// Geometry mode, player fitting: a drag (model frame, metres) and a turn (degrees) of a unit,
	// kept to what its socket allows and snapped to holes.
	Adjust ConstrainDrag(const Assembly& A, const std::string& UnitId, const Adjust& Current, const V3& Drag, double TurnDeg) const;
	// Geometry mode, author fitting: the unit's current placement as a fix for its part data.
	bool AuthorFix(const Assembly& A, const std::string& UnitId, Json& Fix, std::string& Error) const;

	// The group motion for a clearance case (steer degrees, front/rear compression 0..1).
	Xf GroupMotion(const std::string& Group, double SteerDeg, double Front, double Rear) const;

private:
	bool MountMatches(const Unit& Child, const Unit& Parent, std::string& WhyNot) const;
	std::vector<Unit> EffectiveUnits(const Build& B, std::vector<std::pair<std::string, std::string>>& From, std::string& Error) const;
	double PathParam(const Socket& S, const Mount& M) const;
	// Overlap depth (metres) of every pair the checks look at, keyed "a|b|case" (a < b).
	void Depths(const Assembly& A, bool AllPairs, std::map<std::string, double>& Out) const;
	static bool Unmoved(const Placed& P);

	std::string Bike, Name;
	std::map<std::string, Interface> Interfaces;
	std::vector<Unit> Units;                               // stock, parents first
	std::map<std::string, std::vector<Option>> Options;
	std::vector<std::string> SlotOrder;
	std::map<std::string, MeshInfo> Meshes;                // stock model meshes
	std::map<std::string, V3> GroupOrigins;
	Json Datums, ClearanceData;
	double ToleranceMm = 1.0;
	std::vector<std::pair<std::string, std::string>> Touch;
};
} // namespace TorqueFit
