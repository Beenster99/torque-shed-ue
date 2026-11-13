// The road's facts along a ride (OSM stage 1): the speed limit, the road's name, its class and
// surface, and the county and country, from what the cells already hold. Worked out once when the
// ride is planned, so they cost nothing while riding. The limit is information only: nothing
// enforces it. Where the map gives none it is assumed and said to be (a dimmer sign on the HUD):
// in Britain 30 mph where the road is built up, 70 on a motorway and 60 elsewhere; abroad by the
// road's class (as tools/road_facts.py's world row); none at all on a farm track.
#include "WorldRide.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RideFactsBits
{
// A change of limit is kept only with this much road on each side: the map has many short stretches
// with a stray value, and a sign that flickers is worse than none.
constexpr double LimitMinRun = 100.0;
} // namespace RideFactsBits

int RidePlan::AssumedKmh(const std::string& Class, bool InBuiltUp, bool Uk)
{
	if (Class == "track" || Class == "service") return 0;
	if (Uk)
	{
		if (Class == "motorway") return 113;    // 70 mph
		if (InBuiltUp || Class == "living_street" || Class == "residential") return 48;   // 30 mph
		return 97;                               // 60 mph, the national limit on a single carriageway
	}
	if (Class == "motorway") return 120;
	if (InBuiltUp || Class == "living_street") return Class == "living_street" ? 20 : 50;
	if (Class == "residential") return 50;
	if (Class == "trunk") return 100;
	return 90;
}

int RidePlan::ShownLimit(int Kmh, bool* National) const
{
	if (National != nullptr) *National = false;
	if (Kmh <= 0) return 0;
	if (!Britain()) return int(std::lround(Kmh / 10.0) * 10);
	const int Mph = int(std::lround(Kmh / 1.609344 / 5.0) * 5);
	if (National != nullptr) *National = Mph >= 60;
	return Mph;
}

void RidePlan::FindFacts(int From, const std::vector<int>& Path, double RawLength)
{
	Facts.clear();
	Limits.clear();
	if (Road.size() < 2 || Path.empty()) return;
	const Region& G = Area.Roads;
	const double Scale = RawLength > 1.0 ? Length / RawLength : 1.0;
	(void)From;
	double Raw = 0.0;
	for (int E : Path)
	{
		const RoadEdge& Edge = G.Edges[size_t(E)];
		RoadFact F;
		F.From = Raw * Scale;
		Raw += Edge.Length;
		F.To = Raw * Scale;
		F.MappedKmh = Edge.Limit;
		F.Name = G.NameOf(E);
		F.Class = G.ClassOf(E);
		F.Gravel = G.SurfaceOf(E) == "gravel";
		// (one fact for a run of edges that say the same)
		if (!Facts.empty() && Facts.back().MappedKmh == F.MappedKmh && Facts.back().Name == F.Name && Facts.back().Class == F.Class && Facts.back().Gravel == F.Gravel)
			Facts.back().To = F.To;
		else
			Facts.push_back(std::move(F));
	}
	if (!Facts.empty()) Facts.back().To = std::max(Facts.back().To, Length);
}

const RidePlan::RoadFact* RidePlan::FactAt(double Along) const
{
	if (Facts.empty()) return nullptr;
	const auto It = std::upper_bound(Facts.begin(), Facts.end(), Along, [](double A, const RoadFact& F) { return A < F.To; });
	return It == Facts.end() ? &Facts.back() : &*It;
}

const RidePlan::LimitStretch* RidePlan::LimitAt(double Along) const
{
	if (Limits.empty()) return nullptr;
	const auto It = std::upper_bound(Limits.begin(), Limits.end(), Along, [](double A, const LimitStretch& L) { return A < L.To; });
	return It == Limits.end() ? &Limits.back() : &*It;
}

void RidePlan::SettleLimits()
{
	using namespace RideFactsBits;
	Limits.clear();
	if (Road.size() < 2 || Facts.empty()) return;
	const bool Uk = Britain();
	// Each of the road's points: the map's limit, else the one assumed there.
	for (size_t I = 0; I < Road.size(); ++I)
	{
		const RoadFact* F = FactAt(Road[I].Along);
		if (F == nullptr) continue;
		const bool Town = I < BuiltUp.size() && BuiltUp[I] != 0;
		LimitStretch L;
		L.From = I == 0 ? 0.0 : Road[I].Along;
		L.Kmh = F->MappedKmh > 0 ? F->MappedKmh : AssumedKmh(F->Class, Town, Uk);
		L.Assumed = F->MappedKmh <= 0;
		// (a limit as signs show it: 48 and 50 km/h are both 30 mph, so they are one stretch)
		if (!Limits.empty() && ShownLimit(Limits.back().Kmh) == ShownLimit(L.Kmh))
		{
			Limits.back().Assumed = Limits.back().Assumed && L.Assumed;
			continue;
		}
		if (!Limits.empty()) Limits.back().To = L.From;
		Limits.push_back(L);
	}
	if (Limits.empty()) return;
	Limits.back().To = Length;
	// The short ones go into a neighbour: the longer one, or the one the map gives over an assumed one.
	for (bool Merged = true; Merged && Limits.size() > 1;)
	{
		Merged = false;
		size_t Shortest = Limits.size();
		double Least = LimitMinRun;
		for (size_t K = 0; K < Limits.size(); ++K)
			if (Limits[K].To - Limits[K].From < Least)
			{
				Least = Limits[K].To - Limits[K].From;
				Shortest = K;
			}
		if (Shortest == Limits.size()) break;
		const size_t K = Shortest;
		size_t Into;
		if (K == 0) Into = 1;
		else if (K + 1 == Limits.size()) Into = K - 1;
		else
		{
			const LimitStretch &A = Limits[K - 1], &B = Limits[K + 1];
			Into = A.Assumed != B.Assumed ? (A.Assumed ? K + 1 : K - 1) : (A.To - A.From >= B.To - B.From ? K - 1 : K + 1);
		}
		if (Into < K) Limits[Into].To = Limits[K].To;
		else Limits[Into].From = Limits[K].From;
		Limits.erase(Limits.begin() + std::ptrdiff_t(K));
		// (two neighbours now the same: one stretch)
		for (size_t J = 1; J < Limits.size();)
			if (ShownLimit(Limits[J].Kmh) == ShownLimit(Limits[J - 1].Kmh))
			{
				Limits[J - 1].To = Limits[J].To;
				Limits[J - 1].Assumed = Limits[J - 1].Assumed && Limits[J].Assumed;
				Limits.erase(Limits.begin() + std::ptrdiff_t(J));
			}
			else ++J;
		Merged = true;
	}
}

std::string RidePlan::PlaceAt(double X, double Z) const
{
	const Region& G = Area.Roads;
	if (G.CellPlaces.empty()) return std::string();
	const Frame F(G.FrameLat, G.FrameLon);
	const Geo Here = F.ToGeo(X, Z);
	const Region::CellPlace* Best = nullptr;
	double Least = 1e18;
	for (const Region::CellPlace& P : G.CellPlaces)
	{
		const double Dl = P.Lat - Here.Lat, Dn = (P.Lon - Here.Lon) * std::cos(Here.Lat * 3.14159265358979323846 / 180.0);
		const double D = Dl * Dl + Dn * Dn;
		if (D < Least) { Least = D; Best = &P; }
	}
	return Best != nullptr ? Best->Words : std::string();
}
} // namespace TorqueWorld
