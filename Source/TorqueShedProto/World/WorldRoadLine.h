// A road's line made ridable (Ride UK stage 3): a port of the smoothing in the Godot game's
// scripts/route_planner.gd. The map draws a road as straight pieces with corners; this rounds every
// corner into an arc as wide as such a road would have (Fillet), fairs the whole line by the road's
// class (FairLine: near enough a Gaussian, wider on a main road than a street), fairs wider again
// where a bend is still tighter than a bike can take, opens an out-and-back into a turning place,
// pulls open what is left too tight (Relax), and gives knots Step metres apart.
//
// Plain C++ and the same on every machine. Also here: what each class of road is like (its width,
// how far it is faired, what a metre of it costs the quick way), as the Godot game has them.
#pragma once

#include "WorldGeo.h"

#include <string>
#include <vector>

namespace TorqueWorld
{
namespace RoadLine
{
constexpr double Step = 4.0;
constexpr double MinRadius = 10.0;
constexpr double FairDefault = 7.0;
constexpr double FilletTurn = 0.25;
constexpr double FilletRadius = 1.6;
constexpr double FairWiden = 1.35;
constexpr int FairRounds = 10;
constexpr double FairMost = 30.0;
constexpr int FairLevelCount = 8;
constexpr double FairLevels[FairLevelCount] = {3.0, 4.5, 6.5, 9.0, 13.0, 18.0, 24.0, 30.0};
constexpr int FairEase = 10;
constexpr int TurnMark = 40;
constexpr double MaxGrade = 0.15;
constexpr int MaxRounds = 3000;

// By the road's class ("primary", "residential"...): the carriageway's full width (m), how far (m)
// its line is faired, and what a metre of it costs on the quick way ("main").
double WidthOf(const std::string& Class);
double FairOf(const std::string& Class);
double MainCostOf(const std::string& Class);

// A smooth road through Pts: knots Step apart, the ends kept. Fair is how far to fair at each of
// Pts (empty: FairDefault all along).
std::vector<P2> SmoothLine(const std::vector<P2>& Pts, double MinR = MinRadius, const std::vector<double>& Fair = {});
// The line as points StepM apart along it and its last point; Exact leaves a short last piece out
// instead of stretching the last step. Total is its length.
void ResampleXz(const std::vector<double>& Xs, const std::vector<double>& Zs, double StepM, bool Exact, std::vector<double>& Ox, std::vector<double>& Oz, double& Total);
// 1 / radius of the bend at point I from the points two either side.
double Bend(const std::vector<double>& Px, const std::vector<double>& Pz, size_t I);
// Heights averaged over Window knots, and with no climb steeper than Grade between knots Step apart.
std::vector<double> SmoothHeights(const std::vector<double>& H, int Window = 9);
std::vector<double> GradeLimit(const std::vector<double>& E, double Grade = MaxGrade);
} // namespace RoadLine
} // namespace TorqueWorld
