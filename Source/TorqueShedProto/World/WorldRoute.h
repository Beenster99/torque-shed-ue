// The Route planner's own workings (Ride UK stage 4), in plain C++ so SimTest can try them: what a
// journey is (pins on the map and a style of ride), the styles' road costs, journey codes the Godot
// game reads too, map links and GPX files turned into pins, the UK's places to search, round trips
// of a given length, and the famous roads. Planning the road itself is RidePlan::Make (WorldRide).
#pragma once

#include "WorldGeo.h"

#include <memory>
#include <string>
#include <vector>

namespace TorqueWorld
{
class Region;

struct RoutePin
{
	double Lat = 0.0, Lon = 0.0;
	std::string Name;
};

// A journey as the crew plans it: where it goes through, in order, and how.
struct RouteRequest
{
	std::vector<RoutePin> Pins;
	std::string Mode = "main";   // one of RouteStyles
};

namespace Route
{
// The styles of ride, as the Godot planner has them ("main" the quick way, "scenic" quiet back roads,
// "tracks" gravel where there is any), and Ride UK's "curvy": bends and hills, no towns or dual
// carriageways.
static constexpr int StyleCount = 4;
extern const char* const Styles[StyleCount];
bool IsStyle(const std::string& Mode);
// What a metre of a class of road costs in a style (RoutePlanner.CLASS_COST); 1.5 for a class it lacks.
double ClassCost(const std::string& Mode, const std::string& Class);
// What a metre of each of a region's edges costs in a style: the class's cost, and for "curvy" less on
// a twisty or hilly road, more on a straight one or through a built-up place.
std::vector<double> EdgeCosts(const Region& G, const std::string& Mode);
// How twisty a line is: degrees of turning per km, bends gentler than 3 degrees a step left out.
double Twistiness(const std::vector<P2>& Line);

// The most pins a journey has, and the longest a pin's name is (AdventurePlans.MAX_PINS, MAX_NAME);
// a GPX or a round trip may make more pins than a code for the Godot game can carry (8).
static constexpr int MaxPins = 8, MaxPinsAnyway = 32, MaxName = 32;
// A journey code: "TS1." and base64 (URL-safe, unpadded) of {"m": style, "p": [[lat e5, lon e5,
// name], ...], "r": map}, as AdventureJourney.code_of makes them, so a code made in either game
// rides in the other (a style the Godot game lacks rides there as "main").
static constexpr const char* CodePrefix = "TS1.";
std::string CodeOf(const RouteRequest& R);
// The journey in a code anywhere in Text; false when there is none.
bool FromCode(const std::string& Text, RouteRequest& Out);

// Where a map link (Google, Apple, OpenStreetMap, or "lat, lon" typed) points: every place it names in
// order (a Google directions link's stops, an OpenStreetMap route's ends) else its one place.
std::vector<RoutePin> PlacesInLink(const std::string& Text);
// A GPX file's way (its route's points, else its track's, else its waypoints) as at most MaxPins pins:
// the ends, and between them the points the way bends most about, so the planner follows it.
bool FromGpx(const std::string& Text, RouteRequest& Out, int MostPins = MaxPinsAnyway);

// The UK's places to search (Data/World/uk_places.tsv: Tools/make_uk_places.py), the biggest first.
struct Place
{
	std::string Name, Kind;   // city, town, village, hamlet, peak, pass, hill, lake, castle, lighthouse, station
	double Lat = 0.0, Lon = 0.0;
	int Population = 0;
};
class Gazetteer
{
public:
	bool Load(const std::string& Text);
	size_t Count() const { return Rows.size(); }
	// Those whose name starts with Query (folded: case, accents and punctuation set aside), then those
	// with a word that does, the biggest first, at most Limit.
	std::vector<Place> Search(const std::string& Query, size_t Limit = 8) const;
	// The biggest place within Reach metres of a point, else the nearest; null when there are none.
	const Place* Near(double Lat, double Lon, double Reach = 3000.0) const;
	// The biggest places of Kinds (a list like "city town") inside a box, at most Limit, the biggest
	// first: the names a map shows.
	std::vector<const Place*> InBox(double South, double West, double North, double East, const std::string& Kinds, size_t Limit) const;
	static std::string Fold(const std::string& Text);

private:
	std::vector<Place> Rows;
	std::vector<std::string> Keys;
};

// Round trips from a start: Count journeys of about Km kilometres by road, each a loop out one way and
// back another (start, two turning points, start), heading off in different directions.
std::vector<RouteRequest> Loops(double Lat, double Lon, double Km, const std::string& Mode, int Count = 3);

// The famous roads ready to ride (Data/RideUK/famous_roads.json: [{name, about, mode, pins: [[lat,
// lon, name], ...]}, ...]).
struct FamousRoad
{
	std::string Name, About;
	RouteRequest Journey;
};
bool ReadFamousRoads(const std::string& Text, std::vector<FamousRoad>& Out);

// Metres between two points on the ground (equirectangular: near enough over a few hundred km).
double GroundMetres(double Lat0, double Lon0, double Lat1, double Lon1);
} // namespace Route
} // namespace TorqueWorld
