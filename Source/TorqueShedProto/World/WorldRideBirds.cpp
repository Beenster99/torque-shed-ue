// Ride UK's wild birds: see WorldRideBirds.h.
#include "WorldRideBirds.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace
{
constexpr double Pi = 3.14159265358979323846;

uint32_t Mix(uint32_t H)
{
	H ^= H >> 16; H *= 0x7feb352dU; H ^= H >> 15; H *= 0x846ca68bU; H ^= H >> 16;
	return H;
}

// A number from 0 to 1, always the same for the same two whole numbers and salt.
double Hash(int64_t I, int64_t J, uint32_t Salt)
{
	const uint32_t H = Mix(static_cast<uint32_t>(I) * 73856093u ^ Mix(static_cast<uint32_t>(J) * 19349663u ^ Mix(Salt * 83492791u + 0x9e3779b9u)));
	return double(H & 0xffffffu) / double(0x1000000);
}

double HashF(double X, double Z, uint32_t Salt)
{
	return Hash(static_cast<int64_t>(std::floor(X * 4.0)), static_cast<int64_t>(std::floor(Z * 4.0)), Salt);
}

int64_t CellKey(int64_t I, int64_t J, int64_t Layer)
{
	return (Layer << 56) ^ ((I & 0xfffffff) << 28) ^ (J & 0xfffffff);
}

double Smooth(double Lo, double Hi, double V)
{
	const double T = std::fmin(1.0, std::fmax(0.0, (V - Lo) / (Hi - Lo)));
	return T * T * (3.0 - 2.0 * T);
}

double Frac(double V)
{
	return V - std::floor(V);
}

bool Winter(int Month)
{
	return Month >= 10 || Month <= 3;
}

double GroundOr(const std::function<double(double, double)>& Ground, double X, double Z, double Else)
{
	const double G = Ground ? Ground(X, Z) : Else;
	return std::isnan(G) ? Else : G;
}
} // namespace

const char* BirdSpeciesName(BirdSpecies S)
{
	static const char* const Names[] = {"buzzard", "red_kite", "kestrel", "gull", "wood_pigeon", "rook", "starling", "blackbird", "chaffinch", "sparrow", "pheasant", "goose"};
	const int I = int(S);
	return I >= 0 && I < int(BirdSpecies::Count) ? Names[I] : "";
}

const char* BirdPoseName(BirdPose P)
{
	static const char* const Names[] = {"flap0", "flap1", "flap2", "flap3", "glide", "closed", "perch"};
	const int I = int(P);
	return I >= 0 && I < int(BirdPose::Count) ? Names[I] : "";
}

void RideBirds::AddPerches(int64_t Key, std::vector<BirdPerch>&& Found)
{
	Perches[Key] = std::move(Found);
}

void RideBirds::DropPerches(int64_t Key)
{
	Perches.erase(Key);
}

void RideBirds::Clear()
{
	Birds.clear();
	Out.clear();
	Perches.clear();
	Fired.clear();
	Thermals.clear();
	LastFlockSlot = -1;
	Last = -1.0;
}

void RideBirds::PerchesNear(double X, double Z, double R, std::vector<const BirdPerch*>& Found) const
{
	Found.clear();
	const double R2 = R * R;
	for (const auto& Square : Perches)
	{
		const std::vector<BirdPerch>& List = Square.second;
		if (List.empty()) continue;
		// (a square's perches are within its bounds: test its first one's 300 m round before looking at all)
		const double Dx0 = double(List[0].X) - X, Dz0 = double(List[0].Z) - Z;
		if (Dx0 * Dx0 + Dz0 * Dz0 > (R + 380.0) * (R + 380.0)) continue;
		for (const BirdPerch& P : List)
		{
			const double Dx = double(P.X) - X, Dz = double(P.Z) - Z;
			if (Dx * Dx + Dz * Dz <= R2) Found.push_back(&P);
		}
	}
}

double RideBirds::Activity(const BirdPlace& Place) const
{
	double A = Smooth(0.06, 0.3, Place.Light);
	A *= 1.0 - 0.7 * std::fmin(1.0, Place.Rain);
	if (Place.Month == 12 || Place.Month <= 2) A *= 0.85;
	else if (Place.Month >= 4 && Place.Month <= 6) A *= 1.15;
	return A;
}

void RideBirds::Tick(double Now, const std::vector<BirdRider>& Given, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	// (in one order on every machine, whichever order the game lists them in)
	std::vector<BirdRider> Riders(Given);
	std::sort(Riders.begin(), Riders.end(), [](const BirdRider& A, const BirdRider& B) { return A.X != B.X ? A.X < B.X : A.Z < B.Z; });
	if (Last < 0.0 || Now < Last - 1.0) SinceLook = 1e9;   // (a fresh start, or the clock went back)
	const double Dt = Last < 0.0 ? 0.0 : std::fmax(0.0, Now - Last);
	Last = Now;
	SinceLook += Dt;
	if (!Riders.empty() && SinceLook >= 0.25)
	{
		const bool bSecond = std::floor(Now) != std::floor(Now - SinceLook);
		SinceLook = 0.0;
		LookForFlushes(Now, Riders, Place, Ground);
		if (bSecond)
		{
			LookForPerched(Now, Riders, Place, Ground);
			LookForHovers(Now, Riders, Place, Ground);
			KeepSoarers(Now, Riders, Place, Ground);
			LookForFlock(Now, Riders, Place, Ground);
			// (the cells used long ago forgotten)
			for (auto It = Fired.begin(); It != Fired.end();)
				It = Now - It->second > 1800.0 ? Fired.erase(It) : std::next(It);
		}
	}
	Out.clear();
	for (size_t I = 0; I < Birds.size();)
	{
		bool bDone = false;
		Draw(Now, Birds[I], Riders, bDone);
		if (bDone)
		{
			if (Birds[I].How == Mode::Soar) Thermals.erase(Birds[I].Owner);
			Birds[I] = Birds.back();
			Birds.pop_back();
		}
		else ++I;
	}
}

// --- out of the hedges -------------------------------------------------------------------------------------

void RideBirds::LookForFlushes(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	const double Act = Activity(Place);
	if (Act <= 0.01 || int(Birds.size()) > MostBirds - 10) return;
	int Flushing = 0;
	for (const Bird& B : Birds) Flushing += (B.How == Mode::Path && (B.Owner >> 56) == 1) ? 1 : 0;
	if (Flushing > 24) return;
	const int64_t Day = static_cast<int64_t>(std::floor(Now / 1800.0));   // (the chances change every half hour)
	std::vector<const BirdPerch*> Near;
	for (const BirdRider& R : Riders)
	{
		const double Speed = std::hypot(R.Vx, R.Vz);
		if (Speed < 4.0) continue;
		const double Hx = R.Vx / Speed, Hz = R.Vz / Speed;
		const double Sx = -Hz, Sz = Hx;
		const double A0 = 10.0 + Speed * 0.55, A1 = 22.0 + Speed * 1.4;
		PerchesNear(R.X + Hx * (A0 + A1) * 0.5, R.Z + Hz * (A0 + A1) * 0.5, (A1 - A0) * 0.5 + 26.0, Near);
		const BirdPerch* Best = nullptr;
		double BestPick = 2.0;
		int64_t BestCell = 0;
		for (const BirdPerch* P : Near)
		{
			const double Dx = double(P->X) - R.X, Dz = double(P->Z) - R.Z;
			const double Along = Dx * Hx + Dz * Hz, Lat = std::fabs(Dx * Sx + Dz * Sz);
			if (Along < A0 || Along > A1) continue;
			const bool bTree = P->Type == 1 || P->Type == 2;
			if (Lat < (bTree ? 3.0 : 1.5) || Lat > (bTree ? 26.0 : 12.0)) continue;
			const int64_t I = static_cast<int64_t>(std::floor(double(P->X) / FlushCell)), J = static_cast<int64_t>(std::floor(double(P->Z) / FlushCell));
			const int64_t Cell = CellKey(I, J, 1);
			const auto Was = Fired.find(Cell);
			if (Was != Fired.end() && Now - Was->second < 300.0) continue;
			const double Chance = (Place.Town > 0.35 ? 0.065 : 0.09) * Act;
			if (Hash(I, J, uint32_t(Day) * 7u + 11u) >= Chance) continue;
			const double Pick = HashF(double(P->X), double(P->Z), 5u);
			if (Pick < BestPick) { BestPick = Pick; Best = P; BestCell = Cell; }
		}
		if (Best)
		{
			Fired[BestCell] = Now;
			Count.NearestAhead = std::fmin(Count.NearestAhead, (double(Best->X) - R.X) * Hx + (double(Best->Z) - R.Z) * Hz);
			Flush(Now, *Best, R, Place, uint32_t(BestPick * 16777216.0), BestCell, Ground);
		}
	}
}

void RideBirds::Flush(double Now, const BirdPerch& From, const BirdRider& Rider, const BirdPlace& Place, uint32_t Salt, int64_t Cell, const std::function<double(double, double)>& Ground)
{
	const double Speed = std::fmax(0.1, std::hypot(Rider.Vx, Rider.Vz));
	const double Hx = Rider.Vx / Speed, Hz = Rider.Vz / Speed;
	const double Lat = (double(From.X) - Rider.X) * -Hz + (double(From.Z) - Rider.Z) * Hx;
	const double Ax = -Hz * (Lat >= 0.0 ? 1.0 : -1.0), Az = Hx * (Lat >= 0.0 ? 1.0 : -1.0);   // (away from the road)
	const bool bTree = From.Type == 1 || From.Type == 2;
	const double R = Hash(int64_t(Salt), 1, 3u);
	BirdSpecies Kind = BirdSpecies::Blackbird;
	int Many = 1;
	const bool bTown = Place.Town > 0.35;
	if (!bTree)
	{
		const double Pheasants = bTown || Place.Town > 0.15 ? 0.0 : (Winter(Place.Month) ? 0.26 : 0.1);
		if (bTown)
		{
			if (R < 0.5) { Kind = BirdSpecies::Sparrow; Many = 3 + int(Hash(int64_t(Salt), 2, 3u) * 6.0); }
			else if (R < 0.8) Kind = BirdSpecies::Blackbird;
			else { Kind = BirdSpecies::Chaffinch; Many = 2 + int(Hash(int64_t(Salt), 2, 3u) * 3.0); }
		}
		else if (R < Pheasants) Kind = BirdSpecies::Pheasant;
		else if (R < Pheasants + 0.36) Kind = BirdSpecies::Blackbird;
		else if (R < Pheasants + 0.72 || Place.Town < 0.05) { Kind = BirdSpecies::Chaffinch; Many = 2 + int(Hash(int64_t(Salt), 2, 3u) * 5.0); }
		else { Kind = BirdSpecies::Sparrow; Many = 3 + int(Hash(int64_t(Salt), 2, 3u) * 4.0); }
	}
	else
	{
		if (R < 0.55) { Kind = BirdSpecies::WoodPigeon; Many = 1 + int(Hash(int64_t(Salt), 2, 3u) * 3.0); }
		else if (R < 0.7 && From.Type == 1) { Kind = BirdSpecies::Rook; Many = 1 + int(Hash(int64_t(Salt), 2, 3u) * 2.0); }
		else if (R < 0.86) { Kind = BirdSpecies::Chaffinch; Many = 1 + int(Hash(int64_t(Salt), 2, 3u) * 3.0); }
		else Kind = BirdSpecies::Blackbird;
	}
	Many = std::min(Many, MostBirds - int(Birds.size()));
	if (Many <= 0) return;
	++Count.Flushes;
	Count.FlushBirds += Many;
	const double Top = From.Top > 0.2f ? double(From.Top) : (bTree ? 8.0 : 1.7);
	const double X0 = From.X, Z0 = From.Z, G0 = double(From.Y);
	// Where it goes: (Dx, Dz) the way, Far how far, Up how high it rises, Speed; Into: it drops into a hedge or tree there.
	double Dx = Ax, Dz = Az, Far = 40.0, Up = 3.0, Fly = 9.0, Start = 1.2;
	bool bInto = true;
	double Wave = 0.0, WaveLen = 6.0;
	const auto Turn = [](double& X, double& Z, double A)
	{
		const double C = std::cos(A), S = std::sin(A), Nx = X * C - Z * S, Nz = X * S + Z * C;
		X = Nx; Z = Nz;
	};
	const double J1 = Hash(int64_t(Salt), 4, 3u), J2 = Hash(int64_t(Salt), 5, 3u);
	switch (Kind)
	{
	case BirdSpecies::Blackbird:
		Start = 0.9 + 0.5 * J1;
		if (J2 < 0.55)
		{
			// (low across the road in front of the rider, with its alarm rattle: the classic)
			Dx = -Ax + Hx * 0.8; Dz = -Az + Hz * 0.8;
			Far = 22.0 + 22.0 * J1;
			Up = 1.2;
		}
		else { Dx = Hx * 0.9 + Ax * 0.5; Dz = Hz * 0.9 + Az * 0.5; Far = 30.0 + 30.0 * J1; Up = 2.0; }
		Fly = 10.0;
		break;
	case BirdSpecies::Chaffinch:
	case BirdSpecies::Sparrow:
		Start = bTree ? Top * 0.6 : 1.0 + 0.5 * J1;
		Dx = Hx * 0.5 + Ax; Dz = Hz * 0.5 + Az;
		Turn(Dx, Dz, (J2 - 0.5) * 0.9);
		Far = 30.0 + 40.0 * J1; Up = 4.0 + 3.0 * J2; Fly = Kind == BirdSpecies::Sparrow ? 7.5 : 8.5;
		Wave = 0.45; WaveLen = 5.0;
		break;
	case BirdSpecies::WoodPigeon:
	case BirdSpecies::Rook:
		Start = std::fmax(3.0, Top * 0.65);
		Dx = Ax + Hx * 0.35; Dz = Az + Hz * 0.35;
		Turn(Dx, Dz, (J2 - 0.5) * 1.0);
		Far = (Kind == BirdSpecies::WoodPigeon ? 150.0 : 120.0) + 70.0 * J1;
		Up = 22.0 + 14.0 * J2;
		Fly = Kind == BirdSpecies::WoodPigeon ? 14.0 : 10.0;
		bInto = false;
		break;
	case BirdSpecies::Pheasant:
		Start = 0.25;
		Turn(Dx, Dz, (J2 - 0.5) * 0.9);
		Far = 70.0 + 50.0 * J1; Up = 6.0; Fly = 12.0;
		break;
	default:
		break;
	}
	const double Dn = std::fmax(1e-6, std::hypot(Dx, Dz));
	Dx /= Dn; Dz /= Dn;
	double Ex = X0 + Dx * Far, Ez = Z0 + Dz * Far;
	double Ey = GroundOr(Ground, Ex, Ez, G0);
	if (bInto && Kind != BirdSpecies::Pheasant)
	{
		// (into another hedge or tree near where it was going, if there is one)
		std::vector<const BirdPerch*> Near;
		PerchesNear(Ex, Ez, 14.0, Near);
		const BirdPerch* Into = nullptr;
		double Pick = 2.0;
		for (const BirdPerch* P : Near)
		{
			const double H = HashF(double(P->X), double(P->Z), 9u);
			if (H < Pick && (std::fabs(double(P->X) - X0) + std::fabs(double(P->Z) - Z0)) > 12.0) { Pick = H; Into = P; }
		}
		if (Into)
		{
			Ex = Into->X; Ez = Into->Z; Ey = double(Into->Y) + ((Into->Type == 1 || Into->Type == 2) ? std::fmax(2.5, double(Into->Top) * 0.55) : 1.1);
		}
		else Ey += Kind == BirdSpecies::Blackbird ? 1.0 : 1.3;
	}
	else if (Kind == BirdSpecies::Pheasant) Ey += 0.2;
	else Ey += Up;
	const double Gm = GroundOr(Ground, (X0 + Ex) * 0.5, (Z0 + Ez) * 0.5, G0);
	const double Dist = std::hypot(Ex - X0, Ez - Z0);
	for (int K = 0; K < Many; ++K)
	{
		Bird B;
		B.Species = Kind;
		B.How = Mode::Path;
		B.Owner = (int64_t(1) << 56) | (Cell & 0xffffffffffffff);
		B.Seed = Hash(int64_t(Salt), 10 + K, 7u);
		const double Ox = (Hash(int64_t(Salt), 20 + K, 7u) - 0.5) * (Many > 1 ? 3.0 : 0.0), Oz = (Hash(int64_t(Salt), 30 + K, 7u) - 0.5) * (Many > 1 ? 3.0 : 0.0);
		const double Ex2 = Ex + Ox * 2.0, Ez2 = Ez + Oz * 2.0;
		B.T0 = Now + K * 0.1 + (Many > 1 ? B.Seed * 0.45 : 0.0);
		B.Dur = Dist / Fly + (Kind == BirdSpecies::Pheasant ? 0.8 : 0.3);
		const double Sx = X0 + Ox, Sz = Z0 + Oz, Sy = G0 + Start;
		B.P[0][0] = Sx; B.P[0][1] = Sy; B.P[0][2] = Sz;
		if (Kind == BirdSpecies::Pheasant)
		{
			// (straight up out of the verge, then away low over the field)
			B.P[1][0] = Sx + Dx * 3.0; B.P[1][1] = Sy + Up * 1.4; B.P[1][2] = Sz + Dz * 3.0;
			B.P[2][0] = Sx + Dx * Dist * 0.6; B.P[2][1] = Gm + Up; B.P[2][2] = Sz + Dz * Dist * 0.6;
		}
		else if (!bInto)
		{
			// (a clatter of wings and a steep climb out of the crown)
			B.P[1][0] = Sx + Dx * 14.0; B.P[1][1] = Sy + 8.0; B.P[1][2] = Sz + Dz * 14.0;
			B.P[2][0] = Sx + Dx * Dist * 0.6; B.P[2][1] = Gm + Up * 0.9; B.P[2][2] = Sz + Dz * Dist * 0.6;
		}
		else
		{
			const double Mid = std::fmax(Sy, Ey) + Up;
			B.P[1][0] = Sx + (Ex2 - Sx) * 0.3; B.P[1][1] = std::fmax(Mid, Gm + Up); B.P[1][2] = Sz + (Ez2 - Sz) * 0.3;
			B.P[2][0] = Sx + (Ex2 - Sx) * 0.75; B.P[2][1] = std::fmax(Mid, Gm + Up) * 0.6 + Ey * 0.4; B.P[2][2] = Sz + (Ez2 - Sz) * 0.75;
		}
		B.P[3][0] = Ex2; B.P[3][1] = Ey + (K % 3) * 0.3; B.P[3][2] = Ez2;
		B.Wave = Wave; B.WaveLen = WaveLen;
		B.Flap = Kind == BirdSpecies::WoodPigeon ? 6.5 : Kind == BirdSpecies::Rook ? 4.0 : Kind == BirdSpecies::Pheasant ? 11.0 : Kind == BirdSpecies::Blackbird ? 12.0 : 15.0;
		B.Leaving = !bInto;   // (Path birds: true, it flies off and out of sight; false, it eases into a perch)
		Birds.push_back(B);
	}
}

// --- birds of prey on the tree tops, and kestrels hovering -------------------------------------------------

void RideBirds::LookForPerched(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	(void)Ground;
	const double Act = Activity(Place);
	if (Act < 0.2 || Place.Town > 0.3 || Place.Hour < 6.5 || Place.Hour > 20.0 || int(Birds.size()) > MostBirds - 2) return;
	int Have = 0;
	for (const Bird& B : Birds) Have += B.How == Mode::Perched ? 1 : 0;
	if (Have >= 3) return;
	const int64_t Day = static_cast<int64_t>(std::floor(Now / 3600.0));
	std::vector<const BirdPerch*> Near;
	for (const BirdRider& R : Riders)
	{
		const double Speed = std::hypot(R.Vx, R.Vz);
		if (Speed < 2.0) continue;
		const double Hx = R.Vx / Speed, Hz = R.Vz / Speed;
		const double Ax = R.X + Hx * 220.0, Az = R.Z + Hz * 220.0;
		const int64_t I = static_cast<int64_t>(std::floor(Ax / PerchCell)), J = static_cast<int64_t>(std::floor(Az / PerchCell));
		const int64_t Cell = CellKey(I, J, 2);
		if (Fired.count(Cell)) continue;
		Fired[Cell] = Now;
		if (Hash(I, J, uint32_t(Day) * 13u + 3u) >= 0.3 * Act) continue;
		PerchesNear(R.X + Hx * 190.0, R.Z + Hz * 190.0, 150.0, Near);
		const BirdPerch* Best = nullptr;
		double Score = -1.0;
		for (const BirdPerch* P : Near)
		{
			if ((P->Type != 1 && P->Type != 2) || P->Top < 7.0f) continue;
			const double Dx = double(P->X) - R.X, Dz = double(P->Z) - R.Z;
			const double Along = Dx * Hx + Dz * Hz, Lat = std::fabs(Dx * -Hz + Dz * Hx);
			if (Along < 60.0 || Along > 330.0 || Lat < 12.0 || Lat > 85.0) continue;
			const double S = double(P->Top) + 3.0 * HashF(double(P->X), double(P->Z), 17u);
			if (S > Score) { Score = S; Best = P; }
		}
		if (!Best) continue;
		const double Pick = Hash(I, J, uint32_t(Day) * 13u + 5u);
		Bird B;
		B.Species = Pick < 0.58 ? BirdSpecies::Buzzard : Pick < 0.78 ? BirdSpecies::WoodPigeon : Pick < 0.88 ? BirdSpecies::Rook : BirdSpecies::Kestrel;
		B.How = Mode::Perched;
		B.Owner = Cell;
		B.Seed = Hash(I, J, 77u);
		B.Cx = Best->X; B.Cy = double(Best->Y) + double(Best->Top) - 0.35; B.Cz = Best->Z;
		// (looking out over the road, more or less)
		B.Yaw = std::atan2(R.Z - B.Cz, R.X - B.Cx) + (B.Seed - 0.5) * 2.2;
		B.T0 = Now;
		Birds.push_back(B);
		++Count.Perched;
	}
}

void RideBirds::LookForHovers(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	const double Act = Activity(Place);
	if (Act < 0.3 || Place.Town > 0.2 || Place.Sea > 0.6 || Place.Rain > 0.3 || Place.Hour < 7.0 || Place.Hour > 19.5 || int(Birds.size()) > MostBirds - 1) return;
	for (const Bird& B : Birds) if (B.How == Mode::Hover) return;
	const int64_t Day = static_cast<int64_t>(std::floor(Now / 3600.0));
	for (const BirdRider& R : Riders)
	{
		const double Speed = std::hypot(R.Vx, R.Vz);
		if (Speed < 3.0) continue;
		const double Hx = R.Vx / Speed, Hz = R.Vz / Speed;
		const double Ax = R.X + Hx * 280.0, Az = R.Z + Hz * 280.0;
		const int64_t I = static_cast<int64_t>(std::floor(Ax / HoverCell)), J = static_cast<int64_t>(std::floor(Az / HoverCell));
		const int64_t Cell = CellKey(I, J, 3);
		if (Fired.count(Cell)) continue;
		Fired[Cell] = Now;
		if (Hash(I, J, uint32_t(Day) * 17u + 1u) >= 0.16 * Act) continue;
		const double Side = Hash(I, J, 41u) < 0.5 ? -1.0 : 1.0;
		const double Off = 25.0 + 35.0 * Hash(I, J, 42u);
		Bird B;
		B.Species = BirdSpecies::Kestrel;
		B.How = Mode::Hover;
		B.Owner = Cell;
		B.Seed = Hash(I, J, 43u);
		B.Cx = R.X + Hx * (220.0 + 80.0 * B.Seed) - Hz * Off * Side;
		B.Cz = R.Z + Hz * (220.0 + 80.0 * B.Seed) + Hx * Off * Side;
		B.Cy = GroundOr(Ground, B.Cx, B.Cz, R.Y) + 10.0 + 7.0 * Hash(I, J, 44u);
		// (facing into the wind, as a kestrel holds itself still on it)
		const double From = Place.WindFrom * Pi / 180.0;
		B.Yaw = std::atan2(-std::cos(From), std::sin(From));
		B.Flap = 9.0;
		B.T0 = Now;
		Birds.push_back(B);
		++Count.Hovers;
		return;
	}
}

// --- soaring overhead ---------------------------------------------------------------------------------------

void RideBirds::KeepSoarers(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	// (thermals: late morning to early evening, more in the summer half, none in the rain)
	double Chance = Smooth(8.5, 10.5, Place.Hour) * (1.0 - Smooth(17.0, 19.5, Place.Hour));
	Chance *= (Place.Month >= 4 && Place.Month <= 9) ? 0.5 : 0.3;
	Chance *= Place.Rain > 0.25 ? 0.0 : 1.0;
	Chance *= Smooth(0.35, 0.6, Place.Light);
	if (Place.Sea > 0.1) Chance = std::fmax(Chance, 0.45 * Smooth(0.2, 0.5, Place.Light) * (Place.Rain > 0.5 ? 0.3 : 1.0));   // (gulls about all day)
	if (Place.Town > 0.6) Chance *= 0.4;
	int Have = 0;
	for (const Bird& B : Birds) Have += B.How == Mode::Soar ? 1 : 0;
	for (const BirdRider& R : Riders)
	{
		const int64_t I0 = static_cast<int64_t>(std::floor(R.X / ThermalCell)), J0 = static_cast<int64_t>(std::floor(R.Z / ThermalCell));
		for (int64_t J = J0 - 1; J <= J0 + 1; ++J)
			for (int64_t I = I0 - 1; I <= I0 + 1; ++I)
			{
				if (Have >= 5 || int(Birds.size()) >= MostBirds) return;
				const int64_t Cell = CellKey(I, J, 4);
				if (Thermals.count(Cell)) continue;
				if (Hash(I, J, 91u) >= Chance) continue;
				const double Pick = Hash(I, J, 92u);
				Bird B;
				const bool bGull = Place.Sea > 0.1 ? Pick < 0.75 : (Place.Town > 0.4 && Pick < 0.25);
				B.Species = bGull ? BirdSpecies::Gull : (Place.Kites && Pick < 0.5) ? BirdSpecies::RedKite : BirdSpecies::Buzzard;
				B.How = Mode::Soar;
				B.Owner = Cell;
				B.Seed = Hash(I, J, 93u);
				B.Cx = (double(I) + 0.2 + 0.6 * Hash(I, J, 94u)) * ThermalCell;
				B.Cz = (double(J) + 0.2 + 0.6 * Hash(I, J, 95u)) * ThermalCell;
				B.Cy = GroundOr(Ground, B.Cx, B.Cz, R.Y) + (bGull ? 25.0 + 40.0 * B.Seed : 45.0 + 90.0 * B.Seed);
				B.Radius = 35.0 + 50.0 * Hash(I, J, 96u);
				B.Speed = (bGull ? 11.0 : B.Species == BirdSpecies::RedKite ? 8.0 : 9.0) * (Hash(I, J, 97u) < 0.5 ? -1.0 : 1.0);
				B.T0 = Now;
				Thermals[Cell] = true;
				Birds.push_back(B);
				++Count.Soarers;
				++Have;
			}
	}
}

// --- flocks crossing the sky --------------------------------------------------------------------------------

void RideBirds::LookForFlock(double Now, const std::vector<BirdRider>& Riders, const BirdPlace& Place, const std::function<double(double, double)>& Ground)
{
	const int64_t Slot = static_cast<int64_t>(std::floor(Now / FlockEvery));
	if (Slot == LastFlockSlot) return;
	LastFlockSlot = Slot;
	const double Act = Activity(Place);
	if (Act < 0.2) return;
	// (the rider it shows itself to: the same one on every machine, the one furthest west)
	const BirdRider* R = nullptr;
	for (const BirdRider& One : Riders)
		if (std::hypot(One.Vx, One.Vz) > 1.0 && (!R || One.X < R->X)) R = &One;
	if (!R) return;
	const int64_t Cx = static_cast<int64_t>(std::floor(R->X / 2000.0)), Cz = static_cast<int64_t>(std::floor(R->Z / 2000.0));
	if (Hash(Slot, Cx * 131 + Cz, 61u) >= 0.55 * Act) return;
	// Which kind, by the season, the hour and the place.
	const bool bWin = Winter(Place.Month);
	const double Dusk = Smooth(15.0, 17.0, Place.Hour);
	double W[5] = {
		(bWin && (Place.Hour < 9.5 || Place.Hour > 15.0)) ? ((Place.Water > 0.05 || Place.Farm > 0.2) ? 1.6 : 0.6) : 0.0,   // geese
		(Place.Town < 0.5 ? 1.0 : 0.3) + Dusk,                                                                    // rooks
		bWin ? 1.0 + Dusk : 0.4,                                                                                 // starlings
		Place.Town < 0.4 ? (bWin ? 1.0 : 0.5) : 0.1,                                                             // pigeons
		Place.Sea > 0.1 ? 2.2 : (Place.Farm > 0.3 ? 0.4 : 0.15)};                                                 // gulls
	double Sum = 0.0;
	for (double V : W) Sum += V;
	double Pick = Hash(Slot, Cx * 131 + Cz, 62u) * Sum;
	int Which = 0;
	while (Which < 4 && Pick >= W[Which]) { Pick -= W[Which]; ++Which; }
	static const BirdSpecies Kinds[5] = {BirdSpecies::Goose, BirdSpecies::Rook, BirdSpecies::Starling, BirdSpecies::WoodPigeon, BirdSpecies::Gull};
	const BirdSpecies Kind = Kinds[Which];
	const double H1 = Hash(Slot, Cx * 131 + Cz, 63u), H2 = Hash(Slot, Cx * 131 + Cz, 64u), H3 = Hash(Slot, Cx * 131 + Cz, 65u);
	int Many = Kind == BirdSpecies::Goose ? 7 + int(H1 * 16.0) : Kind == BirdSpecies::Starling ? (bWin ? 20 + int(H1 * 30.0) : 6 + int(H1 * 10.0))
		: Kind == BirdSpecies::Rook ? 8 + int(H1 * 20.0) : Kind == BirdSpecies::WoodPigeon ? 8 + int(H1 * 25.0) : 8 + int(H1 * 16.0);
	Many = std::min(Many, MostBirds - int(Birds.size()));
	if (Many < 3) return;
	const double Speed = std::fmax(1.0, std::hypot(R->Vx, R->Vz));
	const double Hx = R->Vx / Speed, Hz = R->Vz / Speed;
	// It crosses the rider's way some way ahead.
	const double Qx = R->X + Hx * (70.0 + 130.0 * H2) - Hz * (H3 - 0.5) * 120.0, Qz = R->Z + Hz * (70.0 + 130.0 * H2) + Hx * (H3 - 0.5) * 120.0;
	const double Ang = std::atan2(Hz, Hx) + (H1 < 0.5 ? 1.0 : -1.0) * (0.9 + 1.3 * H3);
	const double Dx = std::cos(Ang), Dz = std::sin(Ang);
	const double Fly = Kind == BirdSpecies::Goose ? 17.0 : Kind == BirdSpecies::Starling ? 17.0 : Kind == BirdSpecies::WoodPigeon ? 16.0 : Kind == BirdSpecies::Gull ? 11.0 : 11.0;
	const double High = Kind == BirdSpecies::Goose ? 60.0 + 60.0 * H2 : Kind == BirdSpecies::Starling ? 18.0 + 25.0 * H2 : 25.0 + 30.0 * H2;
	const double Gq = GroundOr(Ground, Qx, Qz, R->Y);
	const double Half = 450.0;
	++Count.Flocks;
	Count.FlockBirds += Many;
	for (int K = 0; K < Many; ++K)
	{
		Bird B;
		B.Species = Kind;
		B.How = Mode::Path;
		B.Owner = (int64_t(5) << 56) | (Slot & 0xffffffffffff);
		B.Seed = Hash(Slot, K, 66u);
		double Back = 0.0, Side = 0.0, Up = 0.0;
		if (Kind == BirdSpecies::Goose)
		{
			// (a V: the leader at the front, the rest in two lines behind)
			const int Row = (K + 1) / 2;
			const double Arm = (K % 2 == 0 ? 1.0 : -1.0) * (H3 < 0.3 && K % 2 == 0 ? 0.0 : 1.0);
			Back = Row * 2.4; Side = Row * 2.0 * Arm; Up = (B.Seed - 0.5) * 0.6;
		}
		else
		{
			const double Spread = Kind == BirdSpecies::Starling ? 9.0 : Kind == BirdSpecies::WoodPigeon ? 16.0 : 26.0;
			Back = (Hash(Slot, K, 67u) - 0.5) * Spread * 2.0;
			Side = (Hash(Slot, K, 68u) - 0.5) * Spread * 1.6;
			Up = (Hash(Slot, K, 69u) - 0.5) * Spread * 0.5;
		}
		const double Sx = Qx - Dx * (Half + Back) - Dz * Side, Sz = Qz - Dz * (Half + Back) + Dx * Side;
		const double Ex = Qx + Dx * (Half - Back) - Dz * Side, Ez = Qz + Dz * (Half - Back) + Dx * Side;
		const double Y = Gq + High + Up;
		for (int P = 0; P < 4; ++P)
		{
			const double T = P / 3.0;
			B.P[P][0] = Sx + (Ex - Sx) * T;
			B.P[P][1] = Y + (P == 1 || P == 2 ? 4.0 * (H2 - 0.5) : 0.0);
			B.P[P][2] = Sz + (Ez - Sz) * T;
		}
		B.T0 = Now;
		B.Dur = 2.0 * Half / Fly;
		B.Flap = Kind == BirdSpecies::Goose ? 3.2 : Kind == BirdSpecies::Starling ? 13.0 : Kind == BirdSpecies::WoodPigeon ? 6.0 : Kind == BirdSpecies::Gull ? 3.0 : 3.8;
		B.Wave = Kind == BirdSpecies::Starling ? 1.6 : Kind == BirdSpecies::Goose ? 0.3 : 1.2;
		B.WaveLen = Kind == BirdSpecies::Starling ? 35.0 : 60.0;
		B.Leaving = true;
		Birds.push_back(B);
	}
}

// --- where each is now -------------------------------------------------------------------------------------

void RideBirds::Draw(double Now, Bird& B, const std::vector<BirdRider>& Riders, bool& bDone)
{
	double Nearest = 1e12;
	const BirdRider* Close = nullptr;
	const double Bx = B.How == Mode::Path ? B.P[0][0] : B.Cx, Bz = B.How == Mode::Path ? B.P[0][2] : B.Cz;
	for (const BirdRider& R : Riders)
	{
		const double D = std::hypot(R.X - Bx, R.Z - Bz);
		if (D < Nearest) { Nearest = D; Close = &R; }
	}
	BirdDraw D;
	D.Species = B.Species;
	const double Tau = Now - B.T0;
	const double Ph = Frac(Tau * B.Flap + B.Seed);
	const auto Flap = [&Ph]() { return BirdPose(std::min(3, int(Ph * 4.0))); };
	if (B.How == Mode::Perched)
	{
		if (Nearest > 800.0) { bDone = true; return; }
		// (a buzzard sits tight more often than not; a pigeon or a crow seldom waits)
		const double Nerve = B.Species == BirdSpecies::Buzzard ? 0.45 : B.Species == BirdSpecies::Kestrel ? 0.6 : 0.85;
		if (Close && Nearest < (B.Species == BirdSpecies::Buzzard ? 38.0 : 55.0) && B.Seed < Nerve)
		{
			// Off: away from the rider, dropping off the tree then climbing away.
			double Ax = B.Cx - Close->X, Az = B.Cz - Close->Z;
			const double An = std::fmax(1e-6, std::hypot(Ax, Az));
			Ax /= An; Az /= An;
			const double Rx = Ax * std::cos(B.Seed * 1.4 - 0.7) - Az * std::sin(B.Seed * 1.4 - 0.7), Rz = Ax * std::sin(B.Seed * 1.4 - 0.7) + Az * std::cos(B.Seed * 1.4 - 0.7);
			B.How = Mode::Path;
			B.P[0][0] = B.Cx; B.P[0][1] = B.Cy + 0.3; B.P[0][2] = B.Cz;
			B.P[1][0] = B.Cx + Rx * 12.0; B.P[1][1] = B.Cy - 2.0; B.P[1][2] = B.Cz + Rz * 12.0;
			B.P[2][0] = B.Cx + Rx * 90.0; B.P[2][1] = B.Cy + 6.0; B.P[2][2] = B.Cz + Rz * 90.0;
			B.P[3][0] = B.Cx + Rx * 240.0; B.P[3][1] = B.Cy + 20.0; B.P[3][2] = B.Cz + Rz * 240.0;
			B.T0 = Now;
			B.Dur = 240.0 / (B.Species == BirdSpecies::WoodPigeon ? 14.0 : 10.0);
			B.Flap = B.Species == BirdSpecies::Buzzard ? 3.2 : B.Species == BirdSpecies::Kestrel ? 6.0 : B.Species == BirdSpecies::WoodPigeon ? 6.5 : 4.0;
			B.Leaving = true;
			B.Wave = 0.0;
			++Count.TookOff;
			Draw(Now, B, Riders, bDone);
			return;
		}
		D.Pose = BirdPose::Perch;
		D.X = B.Cx; D.Y = B.Cy; D.Z = B.Cz;
		// (now and then it looks about)
		D.Yaw = B.Yaw + 0.35 * std::sin(Now * 0.21 + B.Seed * 20.0) * (std::sin(Now * 0.05 + B.Seed * 9.0) > 0.3 ? 1.0 : 0.0);
		Out.push_back(D);
		return;
	}
	if (B.How == Mode::Hover)
	{
		if (Nearest > 900.0) { bDone = true; return; }
		// Held still on the wind, a little bob; every few seconds a short glide and back.
		const double Cycle = Frac(Tau / 7.0 + B.Seed);
		const bool bGlide = Cycle > 0.86;
		D.Pose = bGlide ? BirdPose::Glide : Flap();
		D.X = B.Cx + (bGlide ? 3.0 * std::sin((Cycle - 0.86) / 0.14 * Pi) * std::cos(B.Yaw + 1.2) : 0.0);
		D.Z = B.Cz + (bGlide ? 3.0 * std::sin((Cycle - 0.86) / 0.14 * Pi) * std::sin(B.Yaw + 1.2) : 0.0);
		D.Y = B.Cy + 0.25 * std::sin(Tau * 1.7 + B.Seed * 6.0);
		D.Yaw = B.Yaw;
		D.Pitch = bGlide ? 0.1 : 0.55;
		Out.push_back(D);
		return;
	}
	if (B.How == Mode::Soar)
	{
		if (Nearest > 2200.0) { bDone = true; return; }
		const double Sign = B.Speed > 0.0 ? 1.0 : -1.0;
		const double Ang = B.Seed * 2.0 * Pi + Now * B.Speed / B.Radius;
		const double Cx = B.Cx + 60.0 * std::sin(Now / 90.0 + B.Seed * 6.0), Cz = B.Cz + 60.0 * std::cos(Now / 120.0 + B.Seed * 5.0);
		D.X = Cx + std::cos(Ang) * B.Radius;
		D.Z = Cz + std::sin(Ang) * B.Radius;
		D.Y = B.Cy + 8.0 * std::sin(Ang * 0.5);
		D.Yaw = Ang + Sign * 0.5 * Pi;
		D.Roll = Sign * std::atan2(B.Speed * B.Speed, 9.81 * B.Radius);
		// (a few lazy wingbeats now and then)
		const double Beat = Frac(Now / (B.Species == BirdSpecies::Gull ? 9.0 : 23.0) + B.Seed);
		D.Pose = Beat < (B.Species == BirdSpecies::Gull ? 0.18 : 0.06) ? BirdPose(std::min(3, int(Frac(Now * 3.0 + B.Seed) * 4.0))) : BirdPose::Glide;
		Out.push_back(D);
		return;
	}
	// A path: still hidden in its hedge until it goes; gone at the end.
	if (Tau < 0.0) return;
	const double T = Tau / B.Dur;
	if (T >= 1.0) { bDone = true; return; }
	// (easing: into a perch it slows at the end; flying off it gathers speed)
	const auto Ease = [&B](double V) { return B.Leaving ? 0.6 * V + 0.4 * V * V : V * V * (3.0 - 2.0 * V) * 0.55 + V * 0.45; };
	const auto At = [&B](double S, double* Pt)
	{
		const double U = 1.0 - S;
		const double W0 = U * U * U, W1 = 3.0 * U * U * S, W2 = 3.0 * U * S * S, W3 = S * S * S;
		for (int I = 0; I < 3; ++I) Pt[I] = W0 * B.P[0][I] + W1 * B.P[1][I] + W2 * B.P[2][I] + W3 * B.P[3][I];
	};
	const double S0 = Ease(T), S1 = Ease(std::fmin(1.0, T + 0.02)), Sm = Ease(std::fmax(0.0, T - 0.02));
	double P[3], Pn[3], Pp[3];
	At(S0, P); At(S1, Pn); At(Sm, Pp);
	const double Along = std::hypot(P[0] - B.P[0][0], P[2] - B.P[0][2]);
	if (B.Wave > 0.0)
	{
		const double W = std::sin(Along / B.WaveLen * 2.0 * Pi + B.Seed * 6.0) * B.Wave * std::fmin(1.0, Along / 6.0);
		P[1] += W;
		if (B.Owner >> 56 == 5) { P[0] += 0.6 * W * std::cos(B.Seed * 9.0); P[2] += 0.6 * W * std::sin(B.Seed * 9.0); }
	}
	const double Tx = Pn[0] - Pp[0], Ty = Pn[1] - Pp[1], Tz = Pn[2] - Pp[2];
	const double Th = std::hypot(Tx, Tz);
	D.X = P[0]; D.Y = P[1]; D.Z = P[2];
	D.Yaw = std::atan2(Tz, Tx);
	D.Pitch = std::fmax(-0.6, std::fmin(0.7, std::atan2(Ty, std::fmax(Th, 1e-6)) * 0.8));
	// (banked into its turn)
	{
		double Pa[3], Pb[3];
		At(Ease(std::fmax(0.0, T - 0.04)), Pa);
		At(Ease(std::fmin(1.0, T + 0.04)), Pb);
		const double Y0 = std::atan2(P[2] - Pa[2], P[0] - Pa[0]), Y1 = std::atan2(Pb[2] - P[2], Pb[0] - P[0]);
		double Dy = Y1 - Y0;
		while (Dy > Pi) Dy -= 2.0 * Pi;
		while (Dy < -Pi) Dy += 2.0 * Pi;
		const double V = std::hypot(Pb[0] - Pa[0], Pb[2] - Pa[2]) / std::fmax(1e-3, 0.08 * B.Dur);
		const double Rate = Dy / std::fmax(1e-3, 0.04 * B.Dur);
		D.Roll = std::fmax(-0.9, std::fmin(0.9, std::atan(Rate * V / 9.81)));
	}
	// Its wings, by its kind: how it flies.
	const bool bClimb = D.Pitch > 0.25;
	BirdPose Pose = Flap();
	switch (B.Species)
	{
	case BirdSpecies::Chaffinch:
	case BirdSpecies::Sparrow:
	case BirdSpecies::Starling:
		// (bounding: a burst of beats, then wings shut)
		if (!bClimb && Frac(Tau / 0.5 + B.Seed) > (B.Species == BirdSpecies::Starling ? 0.75 : 0.6)) Pose = B.Species == BirdSpecies::Starling ? BirdPose::Glide : BirdPose::Closed;
		if (T > 0.9 && !B.Leaving) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::Blackbird:
		if (!bClimb && Tau > 0.7 && Frac(Tau / 1.2 + B.Seed) > 0.72) Pose = BirdPose::Glide;
		if (T > 0.88) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::WoodPigeon:
		if (!bClimb && Tau > 2.5 && Frac(Tau / 3.0 + B.Seed) > 0.72) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::Rook:
		if (!bClimb && Frac(Tau / 4.0 + B.Seed) > 0.75) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::Pheasant:
		if (Tau > 1.6 && Frac(Tau / 1.8 + B.Seed) > 0.35) Pose = BirdPose::Glide;
		if (T > 0.9) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::Gull:
		if (Frac(Tau / 6.0 + B.Seed) > 0.3) Pose = BirdPose::Glide;
		break;
	case BirdSpecies::Buzzard:
	case BirdSpecies::RedKite:
	case BirdSpecies::Kestrel:
		if (Tau > 1.6 && !bClimb && Frac(Tau / 5.0 + B.Seed) > 0.25) Pose = BirdPose::Glide;
		break;
	default:
		break;
	}
	D.Pose = Pose;
	Out.push_back(D);
}
} // namespace TorqueWorld
