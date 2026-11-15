#include "StormSky.h"

#include <algorithm>
#include <cmath>

namespace TorqueStorm
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;
double Clamp01(double V) { return V < 0.0 ? 0.0 : V > 1.0 ? 1.0 : V; }
double Smooth(double T) { T = Clamp01(T); return T * T * (3.0 - 2.0 * T); }
double Lerp(double A, double B, double T) { return A + (B - A) * T; }

// A well mixed 32 bit hash (Chris Wellons' lowbias32), and three numbers into one.
uint32_t Mix32(uint32_t X)
{
	X ^= X >> 16;
	X *= 0x7feb352dU;
	X ^= X >> 15;
	X *= 0x846ca68bU;
	X ^= X >> 16;
	return X;
}
uint32_t Hash3(int I, int J, long long Second, uint32_t Salt)
{
	uint32_t H = Mix32(static_cast<uint32_t>(I) * 73856093U ^ Salt);
	H = Mix32(H ^ static_cast<uint32_t>(J) * 19349663U);
	H = Mix32(H ^ static_cast<uint32_t>(Second & 0xffffffffLL));
	return Mix32(H ^ static_cast<uint32_t>((Second >> 32) & 0xffffffffLL));
}
double Unit(uint32_t H) { return static_cast<double>(H & 0xffffffU) / 16777216.0; }

// A little generator for the thunder's noise.
struct Rng
{
	uint32_t S;
	explicit Rng(uint32_t Seed) : S(Seed ? Seed : 0x9e3779b9U) {}
	uint32_t Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
	double Uniform() { return Unit(Next()); }
	double Signed() { return Uniform() * 2.0 - 1.0; }
};
} // namespace

double Thunderiness(const TorqueWeather::State& W, const TorqueSky::Looks& L)
{
	const double Coded = W.Code >= 95 ? 0.65 + 0.35 * Smooth(W.Cape / std::max(L.ThunderCapeFull, 1.0)) : 0.0;
	const double Air = Smooth((W.Cape - L.ThunderCapeFrom) / std::max(L.ThunderCapeFull - L.ThunderCapeFrom, 1.0))
		* Smooth((W.Rain - 0.3 * L.ThunderRainMm) / std::max(0.7 * L.ThunderRainMm, 0.1));
	return Clamp01(std::max(Coded, Air));
}

double Haily(const TorqueWeather::State& W, const TorqueSky::Looks& L)
{
	if (W.Code == 96 || W.Code == 99) return 1.0;
	return Thunderiness(W, L) * Smooth((W.Cape - L.HailCape) / 1000.0) * (1.0 - Smooth((W.FreezingLevel - L.HailFreezeM) / 800.0));
}

double Snowy(const TorqueWeather::State& W)
{
	const int C = W.Code;
	const bool bCode = (C >= 71 && C <= 77) || C == 85 || C == 86;
	return std::max(bCode ? 1.0 : 0.0, Smooth(W.Snow / 0.3));
}

std::vector<Cell> StormCells(const TorqueSky::MapGrids& G, const TorqueWeather::State& Here, double Lat, double Lon, const TorqueSky::Looks& L)
{
	std::vector<Cell> Out;
	const double Step = std::max(L.StormSpacingM, 1000.0);
	// The lattice: fixed to the ground round the grid's own centre, carried on the grid's mean wind as
	// its showers are (the rain shafts' lattice); with no grid, round a snapped point, standing still.
	double RefLat = 0.0, RefLon = 0.0, DE = 0.0, DN = 0.0;
	const bool bGrid = G.Any();
	if (bGrid)
	{
		const TorqueWeather::Grid& Ref = (G.Wide && G.Wide->Valid()) ? *G.Wide : *G.Near;
		const double DriftS = (G.Wide && G.Wide->Valid()) ? G.WideDriftS : G.NearDriftS;
		RefLat = Ref.Lat;
		RefLon = Ref.Lon;
		double WE = 0.0, WN = 0.0;
		for (const TorqueWeather::State& P : Ref.Points)
		{
			const double To = (P.WindFrom + 180.0) * DegToRad;
			WE += std::sin(To) * P.Wind;
			WN += std::cos(To) * P.Wind;
		}
		WE /= static_cast<double>(std::max<size_t>(Ref.Points.size(), 1));
		WN /= static_cast<double>(std::max<size_t>(Ref.Points.size(), 1));
		DE = WE * std::max(DriftS, 0.0);
		DN = WN * std::max(DriftS, 0.0);
		const double Most = (Ref.N - 1) * 0.5 * Ref.StepM, Len = std::sqrt(DE * DE + DN * DN);
		if (Len > Most && Len > 0.0) { DE *= Most / Len; DN *= Most / Len; }
	}
	else
		TorqueWeather::SnapCentre(Lat, Lon, 50000.0, RefLat, RefLon);
	double CamE = 0.0, CamN = 0.0;
	TorqueWeather::MetresBetween(RefLat, RefLon, Lat, Lon, CamE, CamN);
	const int Reach = static_cast<int>(std::ceil(L.StormFarthestM / Step)) + 1;
	const int I0 = static_cast<int>(std::floor((CamE - DE) / Step)), J0 = static_cast<int>(std::floor((CamN - DN) / Step));
	const double HereStorm = Thunderiness(Here, L);
	for (int I = I0 - Reach; I <= I0 + Reach; ++I)
		for (int J = J0 - Reach; J <= J0 + Reach; ++J)
		{
			const double PE = (I + 0.5) * Step + DE, PN = (J + 0.5) * Step + DN;
			const double E = PE - CamE, N = PN - CamN;
			if (std::sqrt(E * E + N * N) > L.StormFarthestM) continue;
			double PLat = 0.0, PLon = 0.0;
			TorqueWeather::Offset(RefLat, RefLon, PE, PN, PLat, PLon);
			const TorqueWeather::State W = bGrid ? TorqueSky::MapWeatherAt(G, PLat, PLon) : Here;
			const double Storm = bGrid ? Thunderiness(W, L) : HereStorm;
			if (Storm < 0.05) continue;
			Cell C;
			C.I = I;
			C.J = J;
			C.Lat = PLat;
			C.Lon = PLon;
			C.Storm = Storm;
			C.BaseAsl = std::min(std::max(W.CloudBase, 400.0), 2500.0);
			C.Hail = Haily(W, L);
			Out.push_back(C);
		}
	return Out;
}

std::vector<Strike> StrikesBetween(const std::vector<Cell>& Cells, double FromUnix, double ToUnix, const TorqueSky::Looks& L)
{
	std::vector<Strike> Out;
	if (ToUnix <= FromUnix) return Out;
	const long long First = static_cast<long long>(std::floor(FromUnix)), Last = static_cast<long long>(std::floor(ToUnix));
	const double Step = std::max(L.StormSpacingM, 1000.0);
	for (const Cell& C : Cells)
	{
		// (a stronger storm flashes more than in proportion: a weak one now and then, a strong one often)
		const double PerSecond = std::max(L.FlashesPerMin, 0.0) / 60.0 * std::pow(C.Storm, 1.5);
		for (long long Sec = First; Sec <= Last; ++Sec)
		{
			const uint32_t H = Hash3(C.I, C.J, Sec, 0x51ab3e7U);
			if (Unit(H) >= PerSecond) continue;
			const uint32_t H2 = Mix32(H ^ 0x68e31da4U), H3 = Mix32(H2 ^ 0xb5297a4dU), H4 = Mix32(H3 ^ 0x1b56c4e9U);
			Strike S;
			S.At = static_cast<double>(Sec) + Unit(H2);
			if (S.At < FromUnix || S.At >= ToUnix) continue;
			// Somewhere in the cell (not its middle), the fork's share growing with the storm.
			const double JE = (Unit(H3) - 0.5) * 0.8 * Step, JN = (Unit(H4) - 0.5) * 0.8 * Step;
			TorqueWeather::Offset(C.Lat, C.Lon, JE, JN, S.Lat, S.Lon);
			S.bGround = Unit(Mix32(H4 ^ 0x2f1d3c5bU)) < Clamp01(L.GroundShare) * (0.6 + 0.4 * C.Storm);
			S.Power = 0.3 + 0.7 * Unit(Mix32(H4 ^ 0x9d2c5680U));
			S.Seed = Mix32(H4 ^ 0xefc60000U);
			S.BaseAsl = C.BaseAsl;
			S.Strokes = S.bGround ? 1 + static_cast<int>(Unit(S.Seed) * 4.0) : 1 + static_cast<int>(Unit(Mix32(S.Seed)) * 2.0);
			Out.push_back(S);
		}
	}
	std::sort(Out.begin(), Out.end(), [](const Strike& A, const Strike& B) { return A.At < B.At; });
	return Out;
}

double FlashAt(const Strike& S, double T)
{
	if (T < 0.0 || T > 0.7) return 0.0;
	double Lit = 0.0;
	uint32_t H = S.Seed;
	double At = 0.0;
	for (int K = 0; K < std::max(S.Strokes, 1); ++K)
	{
		if (K > 0)
		{
			H = Mix32(H + 0x9e3779b9U);
			At += 0.04 + 0.08 * Unit(H);
		}
		const double Peak = K == 0 ? 1.0 : 0.55 + 0.35 * Unit(Mix32(H));
		if (T >= At) Lit = std::max(Lit, Peak * std::exp(-(T - At) / 0.035));
	}
	// (the glow lingering in the cloud as it fades)
	Lit = std::max(Lit, 0.12 * std::exp(-T / 0.15));
	return Clamp01(Lit) * (1.0 - Smooth((T - 0.5) / 0.2));
}

double ThunderDelay(double DistanceM, const TorqueSky::Looks& L) { return std::max(DistanceM, 0.0) / std::max(L.SoundMps, 1.0); }

double ThunderLoudness(double DistanceM, double Power, const TorqueSky::Looks& L)
{
	if (DistanceM > L.ThunderFarthestM) return 0.0;
	const double Near = std::max(L.ThunderNearM, 1.0);
	const double Fall = DistanceM <= Near ? 1.0 : std::pow(Near / DistanceM, 0.8);
	const double Edge = 1.0 - Smooth((DistanceM - 0.75 * L.ThunderFarthestM) / std::max(0.25 * L.ThunderFarthestM, 1.0));
	return Clamp01(Power) * Fall * Edge * std::max(L.ThunderVolume, 0.0);
}

std::vector<int16_t> ThunderSound(double DistanceM, double Loud, uint32_t Seed, int Rate)
{
	Rate = std::max(Rate, 8000);
	const double Km = std::max(DistanceM, 50.0) / 1000.0;
	const double Seconds = std::min(std::max(4.0 + Km * 0.9, 4.0), 16.0);
	const size_t Count = static_cast<size_t>(Seconds * Rate);
	std::vector<double> Buf(Count, 0.0);
	Rng R(Seed);
	// The crack (a near strike's tearing start) and how dull the rumble is: the air takes the high notes
	// first, so far thunder is a low roll.
	const double Crack = 1.0 - Smooth((Km - 0.4) / 2.0);
	const double Cutoff = 1600.0 * std::exp(-Km / 2.5) + 110.0;
	const double K = 1.0 - std::exp(-2.0 * Pi * Cutoff / Rate);
	const double KBoom = 1.0 - std::exp(-2.0 * Pi * 45.0 / Rate);
	// The rolls: the channel's parts at different distances arriving one after another.
	struct Roll { double At, Amp, Rise, Fall; };
	std::vector<Roll> Rolls;
	const int RollCount = 3 + static_cast<int>(R.Uniform() * 6.0);
	for (int I = 0; I < RollCount; ++I)
	{
		Roll Ro;
		Ro.At = I == 0 ? 0.0 : R.Uniform() * Seconds * 0.55;
		Ro.Amp = I == 0 ? 1.0 : 0.35 + 0.6 * R.Uniform();
		Ro.Rise = Lerp(0.03, 0.35, Smooth(Km / 8.0)) * (0.6 + 0.8 * R.Uniform());
		Ro.Fall = (0.6 + 1.6 * R.Uniform()) * Lerp(0.8, 1.6, Smooth(Km / 10.0));
		Rolls.push_back(Ro);
	}
	double Low1 = 0.0, Low2 = 0.0, Boom = 0.0, Wobble = 0.0;
	for (size_t N = 0; N < Count; ++N)
	{
		const double T = static_cast<double>(N) / Rate;
		const double White = R.Signed();
		Low1 += (White - Low1) * K;
		Low2 += (Low1 - Low2) * K;
		Boom += (Low2 - Boom) * KBoom;
		double Env = 0.0;
		for (const Roll& Ro : Rolls)
		{
			const double Dt = T - Ro.At;
			if (Dt < 0.0) continue;
			Env += Ro.Amp * (Dt < Ro.Rise ? Dt / Ro.Rise : std::exp(-(Dt - Ro.Rise) / Ro.Fall));
		}
		// (a slow wander in the roll's loudness, as thunder has)
		Wobble += (R.Signed() - Wobble) * 0.0004;
		Env *= 0.75 + 0.5 * std::fabs(Wobble) * 6.0;
		const double Tail = 1.0 - Smooth((T - (Seconds - 1.2)) / 1.2);
		const double Rumble = Low2 / std::sqrt(std::max(K, 1e-4)) + Boom / std::sqrt(std::max(KBoom, 1e-5)) * 0.6 * (1.0 - Smooth(Km / 6.0));
		const double Tear = Crack * (White - Low1) * std::exp(-T / 0.12) * (T < 0.5 ? 1.0 : 0.0) * (0.6 + 0.4 * std::sin(T * 210.0));
		Buf[N] = (Rumble * Env + Tear * 2.5) * Tail;
	}
	double Peak = 1e-9;
	for (double V : Buf) Peak = std::max(Peak, std::fabs(V));
	const double Scale = Clamp01(Loud) * 30000.0 / Peak;
	std::vector<int16_t> Out(Count);
	for (size_t N = 0; N < Count; ++N)
		Out[N] = static_cast<int16_t>(std::min(std::max(Buf[N] * Scale, -32767.0), 32767.0));
	return Out;
}

namespace
{
// The year's main showers: their peak (day of the year), how wide (days, a Gaussian's sigma), the zenith
// hourly rate at their best, and how much of it a British sky sees (a radiant low in the south shows few).
struct Shower { const char* Name; double Peak, Sigma, Zhr, Seen; };
const Shower Showers[] = {
	{"Quadrantids", 3.5, 0.6, 110.0, 1.0},   {"Lyrids", 112.0, 1.0, 18.0, 1.0},     {"Eta Aquariids", 126.0, 3.0, 50.0, 0.25},
	{"Delta Aquariids", 211.0, 4.0, 25.0, 0.3}, {"Perseids", 224.5, 2.0, 100.0, 1.0}, {"Draconids", 281.0, 0.5, 10.0, 1.0},
	{"Orionids", 294.0, 3.0, 20.0, 0.7},     {"Taurids", 309.0, 10.0, 5.0, 1.0},     {"Leonids", 321.0, 1.0, 15.0, 1.0},
	{"Geminids", 348.0, 1.0, 150.0, 1.0},    {"Ursids", 356.0, 0.7, 10.0, 1.0},
};
double ShowerShare(const Shower& S, int DayOfYear)
{
	double D = std::fabs(static_cast<double>(DayOfYear) + 0.5 - S.Peak);
	D = std::min(D, 365.25 - D);
	return S.Zhr * S.Seen * std::exp(-0.5 * (D / S.Sigma) * (D / S.Sigma));
}
} // namespace

double MeteorRate(int DayOfYear, const TorqueSky::Looks& L)
{
	double Rate = std::max(L.MeteorSporadic, 0.0);
	for (const Shower& S : Showers) Rate += ShowerShare(S, DayOfYear);
	return Rate * Clamp01(L.MeteorSeenShare);
}

const char* ShowerOn(int DayOfYear)
{
	const char* Best = "";
	double Most = 5.0;
	for (const Shower& S : Showers)
		if (const double R = ShowerShare(S, DayOfYear); R > Most) { Most = R; Best = S.Name; }
	return Best;
}

Meteor MeteorAt(double Unix, double PerHour)
{
	Meteor M;
	const double P = std::max(PerHour, 0.0) / 3600.0;
	if (P <= 0.0) return M;
	const long long Now = static_cast<long long>(std::floor(Unix));
	// (one starts in a second with chance P, and lasts under 1.5 s: look back two)
	for (long long Sec = Now - 2; Sec <= Now; ++Sec)
	{
		const uint32_t H = Hash3(0x3e7, 0x1d, Sec, 0x6d2b79f5U);
		if (Unit(H) >= P) continue;
		const uint32_t H2 = Mix32(H ^ 0x85ebca6bU), H3 = Mix32(H2 ^ 0xc2b2ae35U), H4 = Mix32(H3 ^ 0x27d4eb2fU);
		const double Start = static_cast<double>(Sec) + Unit(H2);
		const double Dur = 0.3 + 0.9 * Unit(H3);
		const double Along = (Unix - Start) / Dur;
		if (Along < 0.0 || Along > 1.0) continue;
		M.bOn = true;
		M.Along = Along;
		M.El = 15.0 + 60.0 * std::sqrt(Unit(H4));
		M.Az = 360.0 * Unit(Mix32(H4 ^ 0x165667b1U));
		M.Dir = (Unit(Mix32(H4 ^ 0xd3a2646cU)) - 0.5) * 220.0;   // (mostly falling: within 110 degrees of straight down)
		M.Length = 5.0 + 20.0 * Unit(Mix32(H4 ^ 0xfd7046c5U));
		M.Bright = 0.15 + 0.85 * std::pow(Unit(Mix32(H4 ^ 0xb55a4f09U)), 2.5);   // (most faint, now and then a bright one)
	}
	return M;
}

double GeomagneticLat(double Lat, double Lon, double PoleLat, double PoleLon)
{
	const double S = std::sin(Lat * DegToRad) * std::sin(PoleLat * DegToRad)
		+ std::cos(Lat * DegToRad) * std::cos(PoleLat * DegToRad) * std::cos((Lon - PoleLon) * DegToRad);
	return std::asin(std::min(std::max(S, -1.0), 1.0)) / DegToRad;
}

double BearingTo(double Lat, double Lon, double ToLat, double ToLon)
{
	const double P1 = Lat * DegToRad, P2 = ToLat * DegToRad, DL = (ToLon - Lon) * DegToRad;
	const double Y = std::sin(DL) * std::cos(P2);
	const double X = std::cos(P1) * std::sin(P2) - std::sin(P1) * std::cos(P2) * std::cos(DL);
	return std::fmod(std::atan2(Y, X) / DegToRad + 360.0, 360.0);
}
} // namespace TorqueStorm
