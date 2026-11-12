// The trial's own placement rules by bike type (never in the game). The files in this folder are
// copies of the game's Fit/BikeBody.cpp, BikeFrame.cpp and BikeParts.cpp; every change in them is
// marked "[trial rule]" and only acts when Rules.On is set, so with it off the trial builds every
// bike exactly as the game does (the "before" pictures), and with it on by the type's rules.
#pragma once

#include "SimMath.h"
#include <string>
#include <vector>

namespace TorqueFit
{
using TorqueSim::V3;

struct TrialRules
{
	bool On = false;
	// The type's envelope (Data in FitTrial/rules/types.json, read by trial.cpp).
	std::string Type;
	bool RearEnd = false;         // this type has rear-end rules
	double TipBehind = 0.28;      // the tail's tip behind the rear axle (m)
	double KickDeg = 10.0;        // the tail's line above the seat's line (degrees)
	double Flush = 0.0;           // the tail's top at the joint above the seat's top (m)
	double MinKz = 0.6, MaxKz = 1.25;   // how far a tail may be stretched or shortened to suit
	// Worked out per bike while it is fitted (Godot frame): where the tail went, for the frame.
	bool HasCowl = false;
	V3 CowlFrom, CowlDir, CowlUp;
	double CowlKz = 1.0;
	V3 SubEnd;                    // the subframe's end, under the tail near its tip
	double SubEndHalfW = 0.055;
	V3 SubJoin;                   // under the seat/tail joint
	std::vector<std::string> Notes;   // what the rules did, and what does not suit
	void Reset() { HasCowl = false; Notes.clear(); }
};

extern thread_local TrialRules Rules;
} // namespace TorqueFit
