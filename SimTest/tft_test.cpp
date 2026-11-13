// The TFT dash's screens and the sat nav's map (Source/TorqueShedProto/Dash), drawn without the
// engine: every shape of screen in every design and view, checked for sense (something is drawn,
// nothing spills off the screen, the map's shapes cut cleanly), and written out as SVG pictures when
// a folder is given:
//     tft_test [out_dir]
// Tools/tft_sheets.js turns those into the PNG sheets beenster looks at.
#include "TftMap.h"
#include "TftScreen.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace TorqueTft;

namespace
{
int Fails = 0;
void Check(bool Ok, const std::string& What)
{
	if (!Ok)
	{
		++Fails;
		std::printf("FAIL %s\n", What.c_str());
	}
}

std::string Esc(const std::string& S)
{
	std::string O;
	for (char C : S)
	{
		if (C == '&') O += "&amp;";
		else if (C == '<') O += "&lt;";
		else if (C == '>') O += "&gt;";
		else O += C;
	}
	return O;
}

void WriteSvg(const std::string& Path, const Paint& P, bool Round)
{
	std::ofstream F(Path);
	F << "<svg xmlns='http://www.w3.org/2000/svg' width='" << P.W << "' height='" << P.H << "' viewBox='0 0 " << P.W << " " << P.H << "'>\n";
	if (Round) F << "<clipPath id='c'><circle cx='240' cy='240' r='240'/></clipPath><g clip-path='url(#c)'>\n";
	char B[160];
	for (const Cmd& C : P.Cmds)
	{
		std::snprintf(B, sizeof(B), "rgba(%d,%d,%d,%.3f)", int(C.Colour.R * 255.0f + 0.5f), int(C.Colour.G * 255.0f + 0.5f), int(C.Colour.B * 255.0f + 0.5f), double(C.Colour.A));
		const std::string Fill = B;
		if (C.IsText)
		{
			const char* Anchor = C.Side == Align::Centre ? "middle" : C.Side == Align::Right ? "end" : "start";
			F << "<text x='" << C.At.X << "' y='" << C.At.Y << "' font-family='Roboto, Arial, sans-serif' font-size='" << C.Px << "' font-weight='" << (C.Bold ? 700 : 400)
			  << "' text-anchor='" << Anchor << "' fill='" << Fill << "'>" << Esc(C.Words) << "</text>\n";
			continue;
		}
		F << "<path fill='" << Fill << "' d='";
		for (size_t I = 0; I + 2 < C.Tris.size(); I += 3)
		{
			// (wound one way, so a path of many triangles fills as their union)
			P2 A = C.Tris[I], Bp = C.Tris[I + 1], Cp = C.Tris[I + 2];
			if ((Bp.X - A.X) * (Cp.Y - A.Y) - (Bp.Y - A.Y) * (Cp.X - A.X) < 0.0f) std::swap(Bp, Cp);
			std::snprintf(B, sizeof(B), "M%.2f %.2fL%.2f %.2fL%.2f %.2fZ", double(A.X), double(A.Y), double(Bp.X), double(Bp.Y), double(Cp.X), double(Cp.Y));
			F << B;
		}
		F << "'/>\n";
	}
	if (Round) F << "</g>\n";
	F << "</svg>\n";
}

// A made-up market town for the pictures: a main road and a river through it, a square of streets, a
// park, a wood, a lake, houses along the streets, a few places to stop.
MapData Town()
{
	MapData D;
	auto Wavy = [](double X0, double Z0, double X1, double Z1, double Amp, int N) {
		std::vector<MapPt> Pts;
		for (int I = 0; I <= N; ++I)
		{
			const double T = double(I) / double(N);
			const double X = X0 + (X1 - X0) * T, Z = Z0 + (Z1 - Z0) * T;
			const double Len = std::sqrt((X1 - X0) * (X1 - X0) + (Z1 - Z0) * (Z1 - Z0));
			const double NX = -(Z1 - Z0) / Len, NZ = (X1 - X0) / Len;
			const double Off = Amp * std::sin(T * 9.0) + Amp * 0.4 * std::sin(T * 23.0 + 1.0);
			Pts.push_back(MapPt(X + NX * Off, Z + NZ * Off));
		}
		return Pts;
	};
	D.AddArea(AreaKind::Urban, {MapPt(-700, -600), MapPt(650, -640), MapPt(760, 420), MapPt(-520, 560)});
	D.AddArea(AreaKind::Park, {MapPt(120, 80), MapPt(420, 60), MapPt(460, 300), MapPt(300, 380), MapPt(110, 300)});
	D.AddArea(AreaKind::Wood, {MapPt(-1500, -1300), MapPt(-800, -1400), MapPt(-650, -900), MapPt(-900, -700), MapPt(-1200, -820), MapPt(-1600, -900)});
	D.AddArea(AreaKind::Wood, {MapPt(900, 700), MapPt(1500, 640), MapPt(1700, 1100), MapPt(1100, 1300)});
	D.AddArea(AreaKind::Water, {MapPt(800, -900), MapPt(1100, -1000), MapPt(1350, -850), MapPt(1300, -600), MapPt(1000, -520), MapPt(820, -650)});
	D.AddLine(RoadKind::River, "River Ure", Wavy(-2600, 900, 2600, 500, 70.0, 60));
	D.AddLine(RoadKind::Stream, "", Wavy(-300, -2000, -100, 760, 40.0, 30));
	D.AddLine(RoadKind::Rail, "", Wavy(-2600, -1500, 2600, -1250, 20.0, 20));
	D.AddLine(RoadKind::Motorway, "A1(M)", Wavy(2000, -2600, 2300, 2600, 50.0, 30));
	D.AddLine(RoadKind::Main, "A684", Wavy(-2600, -150, 2600, 80, 60.0, 60));
	D.AddLine(RoadKind::Main, "Market Place", {MapPt(-40, -60), MapPt(-30, 220), MapPt(10, 520), MapPt(-60, 900)});
	D.AddLine(RoadKind::Secondary, "B6255", Wavy(-40, -60, -900, -2400, 50.0, 30));
	D.AddLine(RoadKind::Secondary, "Station Road", Wavy(480, 30, 700, -1300, 20.0, 12));
	const char* Streets[] = {"Gayle Lane", "The Holme", "Burtersett Road", "Old Gayle Lane", "Town Head", "Penn Lane", "Wensley Road", "Brunt Acres Road"};
	for (int I = 0; I < 8; ++I)
	{
		const double X = -620.0 + 170.0 * I;
		D.AddLine(RoadKind::Minor, Streets[I], Wavy(X, -560.0, X + 40.0, 420.0, 8.0, 8));
	}
	for (int I = 0; I < 5; ++I)
	{
		const double Z = -480.0 + 200.0 * I;
		D.AddLine(RoadKind::Minor, I == 2 ? "High Street" : "", Wavy(-660.0, Z, 700.0, Z + 30.0, 6.0, 10));
	}
	D.AddLine(RoadKind::Track, "", Wavy(-900, -700, -1500, -1100, 30.0, 10));
	D.AddLine(RoadKind::Path, "", Wavy(150, 100, 420, 320, 15.0, 8));
	for (int I = 0; I < 8; ++I)
		for (int J = 0; J < 22; ++J)
		{
			const double X = -620.0 + 170.0 * I, Z = -540.0 + 44.0 * J;
			if (X > 100.0 && X < 470.0 && Z > 50.0 && Z < 390.0) continue;   // (the park)
			D.AddBox(AreaKind::Building, X + 22.0 + (J % 3), Z, 16.0, 12.0 + (J % 4) * 4.0, 0.04 * (J % 5));
			D.AddBox(AreaKind::Building, X - 22.0, Z + 9.0, 14.0, 20.0, 0.02 * (I % 4));
		}
	D.AddLabel(LabelKind::Town, "Hawes", -80, -330);
	D.AddLabel(LabelKind::Village, "Gayle", -360, 760);
	D.AddLabel(LabelKind::Village, "Burtersett", 1700, 240);
	D.AddLabel(LabelKind::Fuel, "Fuel", 340, -20);
	D.AddLabel(LabelKind::Cafe, "Penny Garth Cafe", -110, 40);
	D.AddLabel(LabelKind::Cafe, "The Crown", 60, 260);
	D.AddLabel(LabelKind::Landmark, "Creamery", -420, 300);
	D.Index();
	return D;
}

State Sample(int Which)
{
	State St;
	St.Metric = false;
	St.Redline = 10500.0;
	St.RevMax = 12000.0;
	St.RevStep = 1000.0;
	St.SpeedMax = 160.0;
	St.SpeedStep = 20.0;
	St.Fuel = 0.72;
	St.RangeKm = 276.0;
	St.TripKm = 38.4;
	St.TotalKm = 2040.0;
	St.TopKmh = 131.0;
	St.KmPerLitre = 19.5;
	St.Hour = 16;
	St.Minute = 44;
	St.AirC = 9.0;
	St.EngineC = 86.0;
	St.Lights = true;
	St.Blink = 0.1;
	St.Mode = "TOURING";
	St.Aids = {{"TC", 3, false}, {"ABS", 3, false}, {"WC", 3, false}, {"EB", 2, false}};
	St.Modes = {"WET", "URBAN", "TOURING", "SPORT", "ENDURO"};
	St.ModeNow = 2;
	St.ModeCursor = 2;
	St.ModeRows = {{"Power", "High, smooth"}, {"Traction control", "3"}, {"ABS", "3"}, {"Wheelie control", "3"}, {"Engine braking", "2"}, {"Suspension", "Comfort"}};
	St.Settings = {{"Screen design", "Dial"}, {"Power", "High", 3, 3}, {"Traction control", "3", 3, 5}, {"ABS", "3", 3, 5}, {"Wheelie control", "3", 3, 5},
		{"Engine braking", "2", 2, 3}, {"Suspension", "Comfort"}, {"Preload", "Rider"}, {"Map", "Heading up"}, {"Brightness", "Auto"}, {"Trip", "Reset"}, {"Reset this mode", ""}};
	St.SetCursor = 2;
	St.SetHelp = "Cuts the power when the back wheel spins. 5 steps in soonest, 1 lets it slide.";
	St.Nav = true;
	St.NavTurn = 2;
	St.NavDistance = "350 yd";
	St.NavOnto = "Turn right, B6255";
	St.NavLeft = "12 miles";
	St.NavEta = "17:02";
	St.NavRoad = "A684, Market Place";
	St.NavPlace = "Hawes, North Yorkshire";
	St.Limit = 30;
	if (Which == 0)
	{
		St.Speed = 0.0; St.Rpm = 1150.0; St.Gear = 0;
	}
	else if (Which == 1)
	{
		St.Speed = 58.0; St.Rpm = 5400.0; St.Gear = 4; St.Limit = 60; St.LimitNational = true;
	}
	else
	{
		St.Speed = 104.0; St.Rpm = 10300.0; St.Gear = 3; St.Mode = "SPORT"; St.Aids[0] = {"TC", 2, true};
	}
	return St;
}
} // namespace

int main(int Argc, char** Argv)
{
	const std::string Out = Argc > 1 ? Argv[1] : "";
	const MapData Data = Town();
	// The map's shapes: an L cut into triangles covers its area exactly; a line is no shape.
	{
		std::vector<MapPt> Tris;
		Check(Triangulate({MapPt(0, 0), MapPt(4, 0), MapPt(4, 1), MapPt(1, 1), MapPt(1, 3), MapPt(0, 3)}, Tris), "an L is cut");
		double Area = 0.0;
		for (size_t I = 0; I + 2 < Tris.size(); I += 3)
			Area += std::fabs((Tris[I + 1].X - Tris[I].X) * (Tris[I + 2].Z - Tris[I].Z) - (Tris[I + 1].Z - Tris[I].Z) * (Tris[I + 2].X - Tris[I].X)) * 0.5;
		Check(std::fabs(Area - 6.0) < 1.0e-9, "the L's triangles cover its 6 square metres");
		Check(!Triangulate({MapPt(0, 0), MapPt(1, 1)}, Tris), "two points are no shape");
		std::vector<int> L, A, Lb;
		Data.Near(0.0, 0.0, 300.0, L, A, Lb);
		Check(!L.empty() && !A.empty() && !Lb.empty(), "the town's middle has roads, land and names near it");
		Data.Near(90000.0, 90000.0, 300.0, L, A, Lb);
		Check(ViewMetres(0.0, -1.0, 0) < ViewMetres(40.0, -1.0, 0), "the view widens with speed");
		Check(ViewMetres(40.0, 120.0, 0) < ViewMetres(40.0, -1.0, 0), "and closes in for a turn");
	}
	MapScene Scene;
	Scene.Data = &Data;
	for (int I = 0; I <= 80; ++I) Scene.Route.push_back(MapPt(-2400.0 + 30.0 * I + 20.0 * std::sin(I * 0.4), -140.0 + 6.0 * std::sin(I * 0.3)));
	for (int I = 0; I <= 60; ++I) Scene.Route.push_back(MapPt(-40.0 - 14.0 * I, -60.0 - 38.0 * I));
	Scene.Bike = MapPt(-420.0, -128.0);
	Scene.Heading = 1.52;
	Scene.Metres = 700.0;
	Scene.HasFinish = true;
	Scene.Finish = MapPt(-880.0, -2340.0);
	Scene.Riders.push_back({MapPt(-250.0, -120.0), Rgba(0.9f, 0.3f, 0.6f), "Kev"});

	const Shape Shapes[] = {Shape::Wide, Shape::Ultra, Shape::Round, Shape::Phone};
	const char* ShapeNames[] = {"wide", "ultra", "round", "phone"};
	const char* ViewNames[] = {"ride", "map", "split", "modes", "settings"};
	int Pictures = 0;
	for (int Sh = 0; Sh < 4; ++Sh)
		for (int V = 0; V < int(View::Count); ++V)
			for (int D = 0; D < int(Design::Count); ++D)
				for (int Night = 0; Night < 2; ++Night)
				{
					const Shape S = Shapes[Sh];
					// (only the ride view differs by design; the phone has one screen; night changes the map alone)
					if (V != int(View::Ride) && D > 0) continue;
					if (S == Shape::Phone && (V != int(View::Map) || D > 0)) continue;
					if (S != Shape::Ultra && V == int(View::Split)) continue;
					if (Night == 1 && V != int(View::Map) && V != int(View::Split)) continue;
					State St = Sample(V == int(View::Ride) ? (D == int(Design::Track) ? 2 : 1) : 1);
					St.Look = Design(D);
					St.Now = View(V);
					St.Night = Night == 1;
					if (V == int(View::Settings)) St.SetEditing = true;
					Scene.Night = St.Night;
					Paint P;
					Draw(P, S, St, &Scene);
					float W = 0.0f, H = 0.0f;
					SizeOf(S, W, H);
					const std::string Name = std::string(ShapeNames[Sh]) + "_" + ViewNames[V] + (V == int(View::Ride) ? std::string("_") + DesignName(Design(D)) : std::string()) + (Night ? "_night" : "");
					Check(P.Triangles() > 4 && P.Cmds.size() > 6, Name + " draws something");
					bool Inside = true;
					for (const Cmd& C : P.Cmds)
						for (const P2& Q : C.Tris)
							if (Q.X < -700.0f || Q.Y < -700.0f || Q.X > W + 700.0f || Q.Y > H + 700.0f) Inside = false;
					Check(Inside, Name + " stays about the screen");
					if (!Out.empty()) WriteSvg(Out + "/" + Name + ".svg", P, S == Shape::Round);
					++Pictures;
				}
	// A parked bike, the engine off: the screen sleeps. No map: the map view says so.
	{
		State St = Sample(0);
		St.EngineOn = false;
		Paint P;
		Draw(P, Shape::Wide, St, &Scene);
		Check(P.Cmds.size() < 6, "ignition off: only the clock");
		St.EngineOn = true;
		St.Now = View::Map;
		Paint Q;
		Draw(Q, Shape::Wide, St, nullptr);
		bool Said = false;
		for (const Cmd& C : Q.Cmds)
			if (C.IsText && C.Words == "No map here") Said = true;
		Check(Said, "no map: it says so");
		if (!Out.empty())
		{
			State Idle = Sample(0);
			Paint R;
			Draw(R, Shape::Wide, Idle, &Scene);
			WriteSvg(Out + "/wide_ride_Dial_idle.svg", R, false);
		}
	}
	// The HUD's tile.
	{
		State St = Sample(1);
		Paint P(420.0f, 300.0f);
		DrawMapTile(P, St, &Scene);
		Check(P.Triangles() > 50, "the HUD's map tile draws");
		bool Inside = true;
		for (const Cmd& C : P.Cmds)
			for (const P2& Q : C.Tris)
				if (Q.X < -0.5f || Q.Y < -0.5f || Q.X > 420.5f || Q.Y > 300.5f) Inside = false;
		Check(Inside, "the tile's map is cut to its box");
		if (!Out.empty()) WriteSvg(Out + "/hud_tile.svg", P, false);
	}
	std::printf("tft_test: %d pictures, %d failed\n", Pictures, Fails);
	return Fails == 0 ? 0 : 1;
}
