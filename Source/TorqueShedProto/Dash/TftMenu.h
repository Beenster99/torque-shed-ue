// What the rider does with a TFT screen from the bars: left and right step through its views (the
// clocks, the map, the split view on a wide screen, the ride modes, the settings), up and down work
// within a view (the trip pages, the map's zoom, the mode the bar is on, a setting's value). It
// holds the ride modes as the rider has them: each one's template with whatever he has altered.
//
// A mode is only changed with the throttle shut, and a setting only at a standstill, as on a real
// bike. Plain C++ (SimTest/modes_test).
#pragma once

#include "RideModes.h"
#include "TftScreen.h"

#include <map>
#include <string>
#include <vector>

namespace TorqueTft
{
enum class Press { Left, Right, Up, Down };

class Menu
{
public:
	// What a press changed, for the game to act on.
	enum : int { Nothing = 0, Moved = 1, ModeChanged = 2, SetChanged = 4, LookChanged = 8, MapChanged = 16 };

	// The bike's dash: what it can set, whether the screen is the wide one, whether it has a screen
	// of its own at all (a phone sat nav's only the map), and the mode it starts in ("" Touring).
	void Setup(const TorqueSim::BikeSpec::AidCaps& InCaps, bool InUltra, const std::string& StartMode = std::string());
	// The rider's own alterations, as saved: "sport.traction" -> 2. Take loads them, Give lists them
	// (only what differs from the template).
	void Take(const std::map<std::string, int>& Saved);
	std::map<std::string, int> Give() const;

	int Do(Press P, double SpeedMs, double Throttle);
	void Tick(double Dt);
	// Fills the screen's own part of a State (the view, the lists, the aids' boxes, the notice).
	void Fill(State& St) const;

	bool HasModes() const { return !Modes.empty(); }
	TorqueSim::RideMode Mode() const { return Modes.empty() ? TorqueSim::RideMode::Touring : Modes[size_t(ModeNow)]; }
	const TorqueSim::ModeSet& Set() const { return Sets[size_t(Mode())]; }
	const TorqueSim::ModeSet& SetOf(TorqueSim::RideMode M) const { return Sets[size_t(M)]; }

	View Now = View::Ride;
	Design Look = Design::Dial;
	int TripPage = 0;
	int Zoom = 0;            // the map's, -2 (closest) .. 2
	bool NorthUp = false;
	std::string Notice;
	double NoticeFor = 0.0;

private:
	TorqueSim::BikeSpec::AidCaps Caps;
	bool Ultra = false;
	std::vector<TorqueSim::RideMode> Modes;
	int ModeNow = 0, ModeCursor = 0;
	TorqueSim::ModeSet Sets[size_t(TorqueSim::RideMode::Count)];
	int SetCursor = 0;
	bool Editing = false;

	// The settings view's rows: the screen's design, each of the mode's settings the bike has, the
	// map's way up, and putting the mode back as it came.
	struct Item
	{
		int Kind = 0;   // 0 design, 1 a mode row, 2 map north up, 3 reset the mode
		TorqueSim::ModeRow Row = TorqueSim::ModeRow::Power;
	};
	std::vector<Item> Items() const;
	std::vector<View> Views() const;
	void Say(const std::string& Words) { Notice = Words; NoticeFor = 3.0; }
};
} // namespace TorqueTft
