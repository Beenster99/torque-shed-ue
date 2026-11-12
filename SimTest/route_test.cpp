// The Route planner's own workings (World/WorldRoute): journey codes as the Godot game makes them, map
// links, GPX files, the UK's places to search, round trips, the famous roads, and the styles' costs.
#include "WorldRoute.h"
#include "WorldFiles.h"
#include "WorldRideStyle.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace TorqueWorld;

namespace
{
int Failed = 0;
void Check(bool Ok, const char* What)
{
	std::printf("%s  %s\n", Ok ? "ok  " : "FAIL", What);
	if (!Ok) ++Failed;
}
bool Near(double A, double B, double Tol) { return std::fabs(A - B) <= Tol; }
} // namespace

int main()
{
	// ------------------------------------------------------------------ journey codes
	{
		RouteRequest R;
		R.Mode = "scenic";
		R.Pins = {{54.30387, -2.19605, "Hawes"}, {54.31483, -2.08082, "Askrigg"}, {54.31004, -1.83041, "Leyburn"}};
		const std::string Code = Route::CodeOf(R);
		// (what Godot's AdventureJourney.code_of makes of the same journey: Python's json.dumps with sorted
		// keys and no spaces, as JSON.stringify writes, then URL-safe base64 with no padding)
		const std::string Godot = "TS1.eyJtIjoic2NlbmljIiwicCI6W1s1NDMwMzg3LC0yMTk2MDUsIkhhd2VzIl0sWzU0MzE0ODMsLTIwODA4MiwiQXNrcmlnZyJdLFs1NDMxMDA0LC0xODMwNDEsIkxleWJ1cm4iXV0sInIiOiIifQ";
		std::printf("      code: %s\n", Code.c_str());
		Check(Code == Godot, "a journey's code is the one the Godot game makes for the same pins and style");
		RouteRequest Back;
		const bool Read = Route::FromCode("have a go at this: " + Godot + " (from Dave)", Back);
		Check(Read && Back.Pins.size() == 3 && Back.Mode == "scenic" && Near(Back.Pins[1].Lat, 54.31483, 1e-9) && Near(Back.Pins[2].Lon, -1.83041, 1e-9) && Back.Pins[0].Name == "Hawes",
			"and a code pasted with words round it reads back to the same pins, names and style");
		RouteRequest Curvy = R;
		Curvy.Mode = "curvy";
		RouteRequest Again;
		Check(Route::FromCode(Route::CodeOf(Curvy), Again) && Again.Mode == "curvy", "Ride UK's curvy style goes through a code too (the Godot game rides it as main)");
		RouteRequest Junk;
		Check(!Route::FromCode("TS1.not-a-journey", Junk) && !Route::FromCode("no code here", Junk), "nothing is read from a code that isn't one");
	}
	// ------------------------------------------------------------------ map links
	{
		const auto One = [](const std::string& Link, double Lat, double Lon)
		{
			const std::vector<RoutePin> P = Route::PlacesInLink(Link);
			return P.size() == 1 && Near(P[0].Lat, Lat, 1e-6) && Near(P[0].Lon, Lon, 1e-6);
		};
		Check(One("https://www.google.com/maps/place/Hawes/@54.3040512,-2.1965,15z/data=!3m1!4b1!4m6!3m5!1s0x0:0x0!8m2!3d54.30387!4d-2.19605", 54.30387, -2.19605)
				&& One("https://www.google.com/maps/@54.3040512,-2.1965,15z", 54.3040512, -2.1965)
				&& One("https://www.openstreetmap.org/#map=15/54.3104/-1.8290", 54.3104, -1.8290)
				&& One("https://www.openstreetmap.org/?mlat=54.31&mlon=-1.83#map=16/54.31/-1.83", 54.31, -1.83)
				&& One("https://maps.apple.com/?ll=54.30,-2.19&q=Hawes", 54.30, -2.19)
				&& One("54.30387, -2.19605", 54.30387, -2.19605) && Route::PlacesInLink("Hawes market place").empty(),
			"a map link's place, the Godot game's ways: Google's place and view, OpenStreetMap's map and marker, Apple's, and lat, lon typed");
		const std::vector<RoutePin> Dir = Route::PlacesInLink("https://www.google.com/maps/dir/54.30387,-2.19605/54.31483,-2.08082/54.31004,-1.83041/@54.3,-2.0,12z/data=!4m2");
		const std::vector<RoutePin> Osm = Route::PlacesInLink("https://www.openstreetmap.org/directions?engine=fossgis_osrm_car&route=54.3039%2C-2.1961%3B54.3100%2C-1.8304#map=12/54.3/-2.0");
		Check(Dir.size() == 3 && Near(Dir[1].Lon, -2.08082, 1e-9) && Osm.size() == 2 && Near(Osm[1].Lat, 54.31, 1e-9),
			"and a directions link's stops in order: Google's and OpenStreetMap's");
	}
	// ------------------------------------------------------------------ GPX
	{
		// A track that goes east, then north, then east again: 300 points.
		std::string Gpx = "<?xml version=\"1.0\"?><gpx version=\"1.1\" creator=\"test\"><trk><name>Loop</name><trkseg>";
		double Lat = 54.30, Lon = -2.20;
		for (int I = 0; I < 300; ++I)
		{
			if (I < 100) Lon += 0.001; else if (I < 200) Lat += 0.001; else Lon += 0.001;
			char Buf[96];
			std::snprintf(Buf, sizeof(Buf), "<trkpt lat=\"%.5f\" lon=\"%.5f\"><ele>300</ele></trkpt>", Lat, Lon);
			Gpx += Buf;
		}
		Gpx += "</trkseg></trk></gpx>";
		RouteRequest R;
		const bool Read = Route::FromGpx(Gpx, R, 8);
		std::printf("      a 300-point GPX track as %d pins\n", int(R.Pins.size()));
		Check(Read && R.Pins.size() >= 4 && R.Pins.size() <= 8 && Near(R.Pins.front().Lon, -2.199, 1e-6) && Near(R.Pins.back().Lat, 54.40, 1e-6) && Near(R.Pins.back().Lon, -2.0, 1e-6),
			"a GPX track becomes a few pins: its ends and its corners");
		RouteRequest Rte;
		Check(Route::FromGpx("<gpx><rte><rtept lat='54.3' lon='-2.2'><name>Hawes</name></rtept><rtept lat='54.31' lon='-1.83'/></rte></gpx>", Rte) && Rte.Pins.size() == 2 && Rte.Pins[0].Name == "Hawes",
			"and a GPX route's points are its pins, names and all");
	}
	// ------------------------------------------------------------------ the UK's places
	{
		std::string Text;
		Route::Gazetteer Places;
		const bool Loaded = Files::Read(TORQUE_ROUTE_PLACES, Text) && Places.Load(Text);
		const std::vector<Route::Place> Hawes = Places.Search("hawes", 5), Ley = Places.Search("Leyb", 3), Llan = Places.Search("llanberis", 3), Pass = Places.Search("wrynose", 3);
		std::printf("      %d places; 'hawes': %s (%s), 'Leyb': %s\n", int(Places.Count()), Hawes.empty() ? "-" : Hawes[0].Name.c_str(), Hawes.empty() ? "" : Hawes[0].Kind.c_str(), Ley.empty() ? "-" : Ley[0].Name.c_str());
		Check(Loaded && Places.Count() > 40000 && !Hawes.empty() && Hawes[0].Name == "Hawes" && Near(Hawes[0].Lat, 54.304, 0.01) && !Ley.empty() && Ley[0].Name == "Leyburn" && !Llan.empty() && !Pass.empty(),
			"the UK's towns, villages, hills and passes are searched by the start of their name");
		const std::vector<Route::Place> Big = Places.Search("man", 3);
		Check(!Big.empty() && Big[0].Name == "Manchester", "the biggest place first");
		Check(Route::Gazetteer::Fold("Ynys M\xc3\xb4n") == "ynys mon" && Route::Gazetteer::Fold("St Mary's Loch") == "st marys loch", "names are matched without their accents, case or apostrophes");
		const Route::Place* At = Places.Near(54.3045, -2.1950);
		Check(At != nullptr && At->Name == "Hawes", "and the place a pin is in is named");
		const std::vector<const Route::Place*> Dales = Places.InBox(54.0, -2.6, 54.6, -1.5, "city town", 12);
		bool InDales = !Dales.empty() && Dales.size() <= 12;
		for (size_t I = 0; I < Dales.size(); ++I)
			InDales = InDales && Dales[I]->Lat >= 54.0 && Dales[I]->Lat <= 54.6 && (Dales[I]->Kind == "town" || Dales[I]->Kind == "city") && (I == 0 || Dales[I]->Population <= Dales[I - 1]->Population);
		std::printf("      towns in the Dales' box: %d, the biggest %s\n", int(Dales.size()), Dales.empty() ? "-" : Dales[0]->Name.c_str());
		Check(InDales, "and a map's box has its biggest towns named, the biggest first");
	}
	// ------------------------------------------------------------------ round trips
	{
		const std::vector<RouteRequest> L = Route::Loops(54.304, -2.196, 60.0, "curvy");
		bool Closed = L.size() == 3;
		double Least = 1e18, Most = 0.0;
		for (const RouteRequest& R : L)
		{
			Closed = Closed && R.Pins.size() == 4 && R.Mode == "curvy" && Near(R.Pins.front().Lat, R.Pins.back().Lat, 1e-12) && Near(R.Pins.front().Lon, R.Pins.back().Lon, 1e-12);
			double Crow = 0.0;
			for (size_t I = 1; I < R.Pins.size(); ++I) Crow += Route::GroundMetres(R.Pins[I - 1].Lat, R.Pins[I - 1].Lon, R.Pins[I].Lat, R.Pins[I].Lon);
			Least = std::fmin(Least, Crow);
			Most = std::fmax(Most, Crow);
		}
		std::printf("      three 60 km loops from Hawes: %.1f to %.1f km as the crow flies\n", Least / 1000.0, Most / 1000.0);
		Check(Closed && Least > 40000.0 && Most < 50000.0 && std::fabs(L[0].Pins[1].Lat - L[1].Pins[1].Lat) > 0.05,
			"a round trip of a given length: three loops out different ways and back to the start, a little shorter as the crow flies");
	}
	// ------------------------------------------------------------------ the famous roads
	{
		std::string Text;
		std::vector<Route::FamousRoad> Roads;
		const bool Read = Files::Read(TORQUE_ROUTE_FAMOUS, Text) && Route::ReadFamousRoads(Text, Roads);
		// (every pin in the UK, but for the one abroad the plan's stage 8 rides: the Stelvio)
		bool InUk = Read, Stelvio = false;
		for (const Route::FamousRoad& F : Roads)
		{
			const bool Abroad = F.Name.find("Stelvio") != std::string::npos;
			Stelvio = Stelvio || Abroad;
			for (const RoutePin& P : F.Journey.Pins) InUk = InUk && (Abroad || (P.Lat > 49.8 && P.Lat < 61.0 && P.Lon > -8.7 && P.Lon < 2.0));
		}
		std::printf("      %d famous roads\n", int(Roads.size()));
		Check(Read && Roads.size() >= 8 && InUk && Stelvio, "the famous roads are ready to ride, every pin of them in the UK but the Stelvio's");
	}
	// ------------------------------------------------------------------ the styles
	{
		Check(Route::ClassCost("main", "motorway") < Route::ClassCost("main", "unclassified") && Route::ClassCost("scenic", "motorway") > Route::ClassCost("scenic", "unclassified")
				&& Route::ClassCost("tracks", "track") < 1.0 && Route::ClassCost("curvy", "trunk") > Route::ClassCost("scenic", "trunk") && Route::ClassCost("main", "bridleway") == 1.5,
			"the styles cost the roads as the Godot planner does: main likes the big roads, scenic the small, tracks the tracks; curvy shuns the big roads more");
		std::vector<P2> Straight, Wiggly;
		for (int I = 0; I <= 100; ++I)
		{
			Straight.push_back(P2(I * 10.0, 0.0));
			Wiggly.push_back(P2(I * 10.0, 25.0 * std::sin(I * 0.25)));
		}
		const double S = Route::Twistiness(Straight), W = Route::Twistiness(Wiggly);
		std::printf("      twistiness: straight %.0f, wiggly %.0f degrees a km\n", S, W);
		Check(S < 1.0 && W > 200.0, "a road's twistiness is how much it turns a km");
	}
	// ------------------------------------------------------------------ the look of Britain (stage 5)
	{
		struct Where { double Lat, Lon; const char* Want; };
		const Where Places[] = {{54.304, -2.196, "dales"}, {51.884, -1.758, "cotswolds"}, {54.43, -3.03, "lakes"}, {50.15, -5.07, "cornwall"}, {50.78, -3.78, "devon"},
			{52.96, -3.92, "wales"}, {57.48, -4.22, "highlands"}, {52.58, 0.10, "fens"}, {52.48, -1.90, "lowland"}, {53.33, -1.78, "peak"}, {55.42, -1.70, "northumberland"},
			{55.86, -4.25, "lowlands"}, {54.40, -0.70, "moors"}};
		bool All = true;
		for (const Where& W : Places)
		{
			const LocalLook& L = LookAt(W.Lat, W.Lon);
			if (std::string(L.Name) != W.Want) { std::printf("      %.2f, %.2f is %s, not %s\n", W.Lat, W.Lon, L.Name, W.Want); All = false; }
			All = All && L.RenderShare + L.BrickShare <= 1.0f && L.ThatchShare + L.TileShare + L.StoneSlateShare <= 1.0f;
		}
		Check(All, "each part of the country has its own look: Dales limestone, Cotswold honey stone, Lakeland slate, Cornish and Devon hedge banks, Welsh slate, Highland harl, Fenland brick...");
		Check(LookAt(50.15, -5.07).Boundary == LocalLook::HedgeBank && LookAt(54.304, -2.196).Boundary == LocalLook::Wall && LookAt(51.884, -1.758).WallStone.R > 1.1f,
			"the South West's walls are hedge banks, the Dales' dry stone; Cotswold stone is honey coloured");
	}
	std::printf("\nroute_test: %s\n", Failed == 0 ? "all passed" : "FAILED");
	return Failed == 0 ? 0 : 1;
}
