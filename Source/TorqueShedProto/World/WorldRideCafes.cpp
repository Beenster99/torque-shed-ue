// [cafes] Ride UK's roadside cafes (beenster Nov 26: "Please can you build about 20 different cafes, from small to large. I
// also want to be able to sit at tables outside them"). Twenty designs of our own (Tools/blender_cafe_kit.py: a tea hut, a
// park kiosk, a container coffee bar and a log cabin up to a biker cafe, a chrome diner, a stone coaching roadhouse and a
// truck stop), each with two hand-made distance versions, and the terrace's chairs, tables, picnic benches, parasols and
// A-boards. IndexCafes (from IndexTownLife) finds the kits bake's cafes out of town and sets each one out: the building on
// the map's box there (the design that fits it best, stretched to it; the box then left undrawn, CafeBuilding) or, where
// the map has no building, set back from the road; a made-up name on its fascia (never a real chain's); its terrace in
// front with every seat listed for the rider on foot to sit at (CafeSeatNear; ATorqueBikePawn's sitting). CafesTile lays
// a square's cafes as props and their names in the sign alphabet. The roadside boards 60 m before the A and B road ones
// are the lay-by set's (World/WorldRideTownLife.cpp); the burger vans' cafes ("van") are theirs too. No switch.
#include "WorldRide.h"
#include "WorldRideSigns.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace TorqueWorld
{
namespace
{
struct CafeDesign
{
	double D, W, Own, Fx, Fy, Fz, Fw;
	float Ink[3];
	double Cap, Door, Front;
	int Terrace, Sets, Size;
};
#include "WorldRideCafeKit.inc"
constexpr int CafeDesignCount = int(sizeof(CafeDesigns) / sizeof(CafeDesigns[0]));

double CafeDice(uint32_t Seed, uint32_t Salt)
{
	uint32_t V = Seed * 2654435761u ^ (Salt + 0x7f4a7c15u) * 2246822519u;
	V ^= V >> 15; V *= 2246822519u; V ^= V >> 13; V *= 3266489917u; V ^= V >> 16;
	return double(V & 0xffffff) / double(0x1000000);
}

long long CafeKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / 64.0)) * 4000037LL + static_cast<long long>(std::floor(Z / 64.0));
}

// A made-up name for a cafe of size class Size (0 a kiosk or hut .. 3 a large roadhouse): never a chain's. Truck: a truck
// stop's. ("Little" never goes with a cook, nor "Ace" anywhere: real roadside names.)
std::string CafeName(uint32_t Seed, int Size, int Design, bool Truck)
{
	static const char* const Places[] = {"Moorside", "Ridgeway", "Hollin Bank", "Fellgate", "Kestrel Edge", "Brackenhurst", "Windy Corner",
		"Copse Hill", "Ashby Cross", "Larkrise", "Heron Bridge", "Stonecroft", "Thornbeck", "Mill Lane", "Oakmere", "Gorse Top", "Coldharbour",
		"Five Lane Ends", "Whinfell", "Beacon View", "Hartside", "Dunmoor", "Sandy Bank", "Crossways", "Linfield", "Old Toll"};
	static const char* const Things[] = {"Kettle", "Teapot", "Piston", "Spanner", "Badger", "Heron", "Lantern", "Milestone", "Cog", "Gatepost",
		"Throttle", "Lapwing", "Curlew", "Tin Cup", "Kingpin", "Hare", "Sprocket", "Signpost"};
	static const char* const Adjs[] = {"Old", "Rusty", "Hungry", "Green", "Red", "Blue", "Wandering", "Sleepy", "Copper", "Crooked", "Happy", "Jolly"};
	const uint32_t S = Seed * 31u + uint32_t(Design);
	const int P = int(CafeDice(S, 1) * 26.0) % 26, T = int(CafeDice(S, 2) * 18.0) % 18, A = int(CafeDice(S, 3) * 12.0) % 12;
	const double Pick = CafeDice(S, 4);
	if (Truck) return std::string(Places[P]) + (Pick < 0.5 ? " Truck Stop" : " Transport Cafe");
	switch (Design)
	{
	case 5: return std::string("The ") + (Pick < 0.5 ? "Carriage" : "Old Coach") + " at " + Places[P];
	case 16: return std::string(Places[P]) + " Diner";
	case 17: return std::string("The ") + Adjs[A] + " " + Things[T];
	case 18: return std::string(Places[P]) + (Pick < 0.5 ? " Truck Stop" : " Transport Cafe");
	case 15: return Pick < 0.5 ? std::string("The ") + Things[T] + " Bikers' Cafe" : std::string(Places[P]) + " Bike Stop";
	default: break;
	}
	if (Size == 0)
	{
		static const char* const Huts[] = {" Tea Hut", " Snack Bar", " Coffee Stop", " Refreshments", " Butty Bar"};
		return std::string(Pick < 0.35 ? std::string("The ") + Things[T] : std::string(Places[P])) + Huts[int(CafeDice(S, 5) * 5.0) % 5];
	}
	if (Size == 1 || Design == 6 || Design == 7)
	{
		static const char* const Rooms[] = {" Tea Room", " Tea Rooms", " Cafe", " Kitchen", " Coffee House"};
		return std::string(Pick < 0.4 ? std::string("The ") + Adjs[A] + " " + Things[T] : std::string(Places[P])) + Rooms[int(CafeDice(S, 5) * 5.0) % 5];
	}
	static const char* const Cafes[] = {" Cafe", " Cafe", " Diner", " Pit Stop", " Roadhouse", " Kitchen"};
	return std::string(Pick < 0.35 ? std::string("The ") + Adjs[A] + " " + Things[T] : std::string(Places[P])) + Cafes[int(CafeDice(S, 5) * 6.0) % 6];
}
} // namespace

void RidePlan::IndexCafes(const CafeHelp& Help)
{
	Cafes.clear();
	CafeGrid.clear();
	CafesSeen = CafesOnBoxes = 0;
	if (!Scene) return;
	const Region& G = Area.Roads;
	const auto MiddleOf = [](const GroundShape& T)
	{
		P2 C(0.0, 0.0);
		for (const P2& P : T.Pts) C = C + P;
		return C * (1.0 / double(T.Pts.size()));
	};
	const auto Unit = [](const P2& V) { const double L = V.Length(); return L > 1e-9 ? V * (1.0 / L) : P2(1.0, 0.0); };
	for (const GroundShape& T : G.KitThings)
	{
		if ((T.Kind != "cafe" && T.Kind != "fast_food") || T.Pts.empty() || T.Word == "van") continue;
		const P2 P = MiddleOf(T);
		++CafesSeen;
		bool Twice = false;
		for (const CafeSite& C : Cafes)
			Twice = Twice || C.At.DistanceTo(P) < 45.0;
		if (Twice || Help.Houses(P) > 12) continue;
		double RailDist = 0.0, RailTop = 0.0;
		int RailOn = 0;
		if (NearRail(P.X, P.Z, RailDist, RailTop, RailOn) && RailOn != 2 && RailDist < 25.0) continue;   // (a station's cafe, on its platform: left as the map draws it)
		P2 Foot, Way;
		double Half = 0.0;
		if (!Help.Road(P, 80.0, Foot, Way, Half)) continue;
		const uint32_t Seed = (static_cast<uint32_t>(std::lround(P.X * 3.0)) * 73856093u ^ static_cast<uint32_t>(std::lround(P.Z * 3.0)) * 19349663u) | 1u;
		const bool Truck = T.Word == "truck_stop";
		const Building* Box = Help.BoxAt(P);
		// (a fast food place with a big building is a takeaway or a drive-through: left as the map draws it)
		if (T.Kind == "fast_food" && !Truck && (Box == nullptr || Box->W * Box->D > 160.0)) continue;
		CafeSite C;
		const auto FitDesign = [&](double Deep, double Wide, int MinSize, int MaxSize)
		{
			// (the three designs nearest its box in depth and width, one of them by the place's own dice)
			std::vector<std::pair<double, int>> Cost;
			for (int I = 0; I < CafeDesignCount; ++I)
			{
				const CafeDesign& Dn = CafeDesigns[I];
				if (Dn.Size < MinSize || Dn.Size > MaxSize) continue;
				Cost.push_back({std::fabs(std::log(Deep / Dn.D)) + std::fabs(std::log(Wide / Dn.W)), I});
			}
			std::sort(Cost.begin(), Cost.end());
			if (Cost.empty()) return -1;
			const int N = int(std::min<size_t>(3, Cost.size()));
			int K = int(CafeDice(Seed, 11) * N) % N;
			while (K > 0 && Cost[size_t(K)].first > Cost[0].first + 0.35) --K;
			return Cost[size_t(K)].second;
		};
		if (Box != nullptr)
		{
			const double BoxArea = Box->W * Box->D;
			if (BoxArea < 8.0 || BoxArea > 700.0) continue;
			// (its front: the side of its box facing the road)
			const double Cs = std::cos(Box->Yaw), Sn = std::sin(Box->Yaw);
			const P2 Ax(Cs, -Sn), Az(Sn, Cs);
			const P2 To = Foot - P2(Box->X, Box->Z);
			const double Ux = To.X * Ax.X + To.Z * Ax.Z, Uz = To.X * Az.X + To.Z * Az.Z;
			const bool OnX = std::fabs(Ux) / std::fmax(Box->W, 1.0) >= std::fabs(Uz) / std::fmax(Box->D, 1.0);
			C.Front = OnX ? Ax * (Ux >= 0.0 ? 1.0 : -1.0) : Az * (Uz >= 0.0 ? 1.0 : -1.0);
			const double Deep = OnX ? Box->W : Box->D, Wide = OnX ? Box->D : Box->W;
			C.Design = Truck ? 18 : FitDesign(Deep, Wide, BoxArea < 30.0 ? 0 : 1, 3);
			if (C.Design < 0) continue;
			const CafeDesign& Dn = CafeDesigns[C.Design];
			C.Stretch = std::clamp(Deep / Dn.D, 0.75, 1.35);
			C.Across = std::clamp(Wide / Dn.W, 0.75, 1.35);
			C.At = P2(Box->X, Box->Z);
			C.HalfD = std::fmax(Deep, Dn.D * C.Stretch) * 0.5;
			C.HalfW = std::fmax(Wide, Dn.W * C.Across) * 0.5;
			C.OnMapBox = true;
		}
		else
		{
			// (no building: a kiosk or hut mostly, now and then a bigger cafe, set back from the road square to it with its
			// terrace between, where the ground is clear and level enough)
			const double R = CafeDice(Seed, 12);
			const int Want = Truck ? 3 : R < 0.5 ? 0 : R < 0.72 ? 1 : R < 0.95 ? 2 : 3;
			std::vector<int> Of;
			for (int I = 0; I < CafeDesignCount; ++I)
				if (CafeDesigns[I].Size == Want && (!Truck || I == 18)) Of.push_back(I);
			if (Of.empty()) continue;
			C.Design = Of[size_t(int(CafeDice(Seed, 13) * double(Of.size())) % int(Of.size()))];
			const CafeDesign& Dn = CafeDesigns[C.Design];
			P2 Out = Unit(P - Foot);
			if (P.DistanceTo(Foot) < 0.5) Out = P2(Way.Z, -Way.X);
			C.Front = Out * -1.0;
			C.HalfD = Dn.D * 0.5;
			C.HalfW = Dn.W * 0.5;
			const double Reach = Dn.Front - Dn.D * 0.5;   // (a veranda's depth in front of the walls)
			bool Placed = false;
			for (const double Gap : {Dn.Size == 0 ? 3.5 : 5.0, 7.0, 10.0})
			{
				for (const double Shift : {0.0, 6.0, -6.0})
				{
					const P2 Mid = Foot + Out * (Half + Gap + Reach + C.HalfD) + Way * Shift;
					// (the footprint's corners, middle and front clear; its ground level within 1.4 m)
					bool Fine = !Help.Walled(Foot + Out * (Half + 0.5), Mid);
					double Lo = 1e18, Hi = -1e18;
					for (const double U : {-1.0, 0.0, 1.0})
						for (const double V : {-1.0, 0.0, 1.0})
						{
							if (!Fine) break;
							const P2 Q = Mid + Out * (U * (C.HalfD + 0.5)) + Way * (V * (C.HalfW + 0.5));
							Fine = Help.Clear(Q, 0.5);
							const double Y = Ground(Q.X, Q.Z);
							if (std::isnan(Y)) { Fine = false; break; }
							Lo = std::fmin(Lo, Y); Hi = std::fmax(Hi, Y);
						}
					if (!Fine || Hi - Lo > 1.4) continue;
					C.At = Mid;
					Placed = true;
					break;
				}
				if (Placed) break;
			}
			if (!Placed) continue;
		}
		const CafeDesign& Dn = CafeDesigns[C.Design];
		// (the floor's foot: the lowest of its footprint's ground, the footing taking up the rest)
		{
			const P2 Side(C.Front.Z, -C.Front.X);
			double Lo = 1e18;
			for (const double U : {-1.0, 0.0, 1.0})
				for (const double V : {-1.0, 0.0, 1.0})
				{
					const P2 Q = C.At + C.Front * (U * C.HalfD) + Side * (V * C.HalfW);
					const double Y = Ground(Q.X, Q.Z);
					if (!std::isnan(Y)) Lo = std::fmin(Lo, Y);
				}
			if (Lo > 1e17) continue;
			const double Mid = Ground(C.At.X, C.At.Z);
			C.Y = std::isnan(Mid) ? Lo : std::fmax(Lo, Mid - 0.45);
		}
		C.Name = CafeName(Seed, Dn.Size, C.Design, Truck);
		for (uint32_t Again = 1; Again < 12; ++Again)   // (no two cafes on a ride share a name)
		{
			bool Taken = false;
			for (const CafeSite& O : Cafes) Taken = Taken || O.Name == C.Name;
			if (!Taken) break;
			C.Name = CafeName(Seed + Again * 977u, Dn.Size, C.Design, Truck);
		}
		// The terrace: its sets in a row (two when there's room) along the front, 1.2 m out from the walls (or the
		// veranda's edge), short of the road; or down a side when the front has no room.
		{
			const P2 Side(C.Front.Z, -C.Front.X);
			const bool Picnic = Dn.Terrace == 0;
			const double Pitch = Picnic ? 3.0 : Dn.Terrace == 2 ? 2.8 : 2.4, RowDeep = Picnic ? 2.7 : 2.3;
			const double FrontAt = (Dn.Front - Dn.D * 0.5) + Dn.D * 0.5 * C.Stretch;   // (the walls' or veranda's front from the middle)
			const double Wide = Dn.W * C.Across;
			int Laid = 0;
			const auto TrySet = [&](const P2& Q, const P2& Face)
			{
				// Face: the way the set's long side faces (the picnic bench's benches either side of it along Face)
				if (!Help.Clear(Q, Picnic ? 1.1 : 0.9) || Help.Walled(C.At + C.Front * FrontAt, Q)) return false;
				const P2 Across(Face.Z, -Face.X);
				const int SetVariant = int(CafeDice(Seed, 21) * 2.0) % 2;
				const uint32_t Id = Seed * 13u + uint32_t(Laid) * 7u;
				if (Picnic)
				{
					RoadsideThing Th;
					Th.Kind = PropCafePicnic; Th.At = Q; Th.Front = Face; Th.Variant = SetVariant; Th.Id = Id;
					C.Things.push_back(Th);
					for (const double S : {1.0, -1.0})
						for (const double A : {-0.45, 0.45})
							C.Seats.push_back({Q + Face * (0.62 * S) + Across * A, Face * -S, 0.45});
				}
				else
				{
					const int Tv = Dn.Terrace == 2 ? 1 : (int(CafeDice(Seed, 22) * 2.0) % 2 == 0 ? 0 : 2);
					const int Cv = Dn.Terrace == 2 ? 1 : Tv == 0 ? (int(CafeDice(Seed, 23) * 2.0) % 2 == 0 ? 0 : 2) : 3;
					RoadsideThing Th;
					Th.Kind = PropCafeTable; Th.At = Q; Th.Front = Face; Th.Variant = Tv; Th.Id = Id;
					C.Things.push_back(Th);
					// (two chairs face to face across it; four round the square timber one)
					std::vector<P2> Ways = {Across, Across * -1.0};
					if (Dn.Terrace == 2) { Ways.push_back(Face); Ways.push_back(Face * -1.0); }
					for (size_t K = 0; K < Ways.size(); ++K)
					{
						RoadsideThing Ch;
						Ch.Kind = PropCafeChair; Ch.At = Q + Ways[K] * 0.58; Ch.Front = Ways[K] * -1.0; Ch.Variant = Cv; Ch.Id = Id + 1u + uint32_t(K);
						C.Things.push_back(Ch);
						C.Seats.push_back({Ch.At + Ch.Front * 0.02, Ch.Front, 0.46});
					}
					if (CafeDice(Id, 24) < 0.55)
					{
						RoadsideThing Pa;
						Pa.Kind = PropCafeParasol; Pa.At = Q; Pa.Front = Face; Pa.Variant = int(CafeDice(Seed, 25) * 4.0) % 4; Pa.Id = Id + 9u;
						C.Things.push_back(Pa);
					}
				}
				++Laid;
				return true;
			};
			const int Most = std::max(1, Dn.Sets);
			const int PerRow = std::max(1, std::min(Most, int(std::floor((Wide + 2.0) / Pitch))));
			for (int Row = 0; Row < 2 && Laid < Most; ++Row)
				for (int K = 0; K < PerRow && Laid < Most; ++K)
				{
					const double U = (double(K) - (PerRow - 1) * 0.5) * Pitch;
					const P2 Q = C.At + C.Front * (FrontAt + 1.2 + RowDeep * (Row + 0.5)) + Side * U;
					TrySet(Q, Picnic ? C.Front : Side);
				}
			if (Laid == 0)
				for (const double S : {1.0, -1.0})
					for (int K = 0; K < 2 && Laid < Most; ++K)
					{
						const P2 Q = C.At + Side * (S * (Wide * 0.5 + 1.2 + RowDeep * 0.5)) + C.Front * (FrontAt - 1.5 - K * Pitch);
						TrySet(Q, Picnic ? Side * S : C.Front);
					}
			// (the A-board by the door, out on the terrace's road side)
			if (Dn.Door > -90.0)
			{
				RoadsideThing Ab;
				Ab.Kind = PropCafeABoard;
				Ab.At = C.At + C.Front * (FrontAt + 0.8) + Side * (Dn.Door * C.Across + 1.3);
				Ab.Front = Side;
				Ab.Variant = int(CafeDice(Seed, 26) * 2.0) % 2;
				Ab.Id = Seed * 13u + 101u;
				if (Help.Clear(Ab.At, 0.3)) C.Things.push_back(Ab);
			}
		}
		// (its hardstanding: the footprint and the terrace in front, out to its furthest thing)
		{
			const P2 Side(C.Front.Z, -C.Front.X);
			double Out = C.HalfD + 1.0, Wide = C.HalfW + 1.0;
			for (const RoadsideThing& Th : C.Things)
			{
				const P2 D = Th.At - C.At;
				Out = std::fmax(Out, D.X * C.Front.X + D.Z * C.Front.Z + 1.6);
				Wide = std::fmax(Wide, std::fabs(D.X * Side.X + D.Z * Side.Z) + 1.6);
			}
			const double Back = C.HalfD + 1.0;
			C.Ring = {C.At - C.Front * Back - Side * Wide, C.At + C.Front * Out - Side * Wide, C.At + C.Front * Out + Side * Wide, C.At - C.Front * Back + Side * Wide};
		}
		CafesOnBoxes += C.OnMapBox ? 1 : 0;
		CafeGrid[CafeKey(C.At.X, C.At.Z)].push_back(int(Cafes.size()));
		Cafes.push_back(std::move(C));
	}
}

bool RidePlan::CafeBuilding(double X, double Z) const
{
	if (Cafes.empty() || !PropModelled[PropCafeBuilding]) return false;
	for (int Dz = -1; Dz <= 1; ++Dz)
		for (int Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = CafeGrid.find(CafeKey(X + Dx * 64.0, Z + Dz * 64.0));
			if (It == CafeGrid.end()) continue;
			for (int I : It->second)
			{
				const CafeSite& C = Cafes[size_t(I)];
				const P2 D = P2(X, Z) - C.At;
				const double U = D.X * C.Front.X + D.Z * C.Front.Z, V = D.X * C.Front.Z - D.Z * C.Front.X;
				if (std::fabs(U) < C.HalfD + 0.5 && std::fabs(V) < C.HalfW + 0.5) return true;
			}
		}
	return false;
}

void RidePlan::CafesTile(double X0, double Z0, double Size, RideMesh& Out) const
{
	if (Cafes.empty()) return;
	const auto Inside = [X0, Z0, Size](const P2& P) { return P.X >= X0 && P.X < X0 + Size && P.Z >= Z0 && P.Z < Z0 + Size; };
	const auto Put = [&Out](int Kind, int Variant, const P2& At, double Y, const P2& Front, double Scale, double Stretch, double Across, uint32_t Id)
	{
		RideProp Pr;
		Pr.Kind = uint16_t(Kind);
		Pr.Variant = uint8_t(Variant);
		Pr.Mapped = false;
		Pr.Id = Id;
		Pr.X = float(At.X - Out.OriginX);
		Pr.Y = float(Y);
		Pr.Z = float(At.Z - Out.OriginZ);
		Pr.Yaw = float(std::atan2(Front.Z, Front.X));
		Pr.Scale = float(Scale);
		Pr.Stretch = float(Stretch);
		Pr.Across = float(Across);
		Out.Props.push_back(Pr);
	};
	for (double Z = Z0 - 64.0; Z < Z0 + Size + 64.0; Z += 64.0)
		for (double X = X0 - 64.0; X < X0 + Size + 64.0; X += 64.0)
		{
			const auto It = CafeGrid.find(CafeKey(X + 1.0, Z + 1.0));
			if (It == CafeGrid.end()) continue;
			for (int I : It->second)
			{
				const CafeSite& C = Cafes[size_t(I)];
				const CafeDesign& Dn = CafeDesigns[C.Design];
				if (Inside(C.At) && PropModelled[PropCafeBuilding])
				{
					// (each model at its own height: the kind's height over its own undoes the scaling to the kind's)
					Put(PropCafeBuilding, C.Design, C.At, C.Y, C.Front, Dn.Own / CafeKindHeight, C.Stretch, C.Across, uint32_t(I) * 977u + 1u);
					double W = 0.0, Cap = Dn.Cap;
					if (SignText::Width(C.Name, Cap, W) && W > 0.0)
					{
						if (W > Dn.Fw * C.Across) Cap *= Dn.Fw * C.Across / W;
						const P2 Side(C.Front.Z, -C.Front.X);
						const P2 At = C.At + C.Front * (Dn.Fx * C.Stretch + 0.03) + Side * (Dn.Fy * C.Across);
						SignText::Set(Out.SignsOf(), At.X, C.Y + Dn.Fz, At.Z, C.Front.X, C.Front.Z, Cap, C.Name, Dn.Ink);
					}
				}
				for (const RoadsideThing& T : C.Things)
				{
					if (!Inside(T.At) || !PropModelled[T.Kind]) continue;
					const double Y = Ground(T.At.X, T.At.Z);
					if (std::isnan(Y)) continue;
					Put(T.Kind, T.Variant, T.At, Y, T.Front, 1.0, 1.0, 1.0, T.Id);
				}
			}
		}
}

bool RidePlan::CafeSeatNear(double X, double Z, double Reach, P2& At, double& Y, P2& Face, const std::vector<P2>* Taken, std::string* Name) const
{
	double Best = Reach;
	bool Got = false;
	for (int Dz = -1; Dz <= 1; ++Dz)
		for (int Dx = -1; Dx <= 1; ++Dx)
		{
			const auto It = CafeGrid.find(CafeKey(X + Dx * 64.0, Z + Dz * 64.0));
			if (It == CafeGrid.end()) continue;
			for (int I : It->second)
				for (const CafeSeat& S : Cafes[size_t(I)].Seats)
				{
					const double D = std::hypot(S.At.X - X, S.At.Z - Z);
					if (D >= Best) continue;
					bool Sat = false;
					if (Taken)
						for (const P2& T : *Taken) Sat = Sat || T.DistanceTo(S.At) < 0.3;
					if (Sat) continue;
					const double G = Ground(S.At.X, S.At.Z);
					if (std::isnan(G)) continue;
					Best = D;
					At = S.At;
					Face = S.Face;
					Y = G + S.High;
					if (Name) *Name = Cafes[size_t(I)].Name;
					Got = true;
				}
		}
	return Got;
}
} // namespace TorqueWorld
