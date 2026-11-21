// [real water stage 9] The tide round Britain and Ireland, made up (no tide tables, nothing paid for):
// beenster, Nov 21: "Add tides as stage 9". The sea rises and falls twice a day (the moon's semi-diurnal
// tide, 12 h 25 min), higher at springs a couple of days after a new or full moon and lower at neaps,
// by how much and when depending on the coast: a few dozen ports each with a range and a lag behind
// Dover's high water, blended between them by distance. Plausible, not a prediction: never for sailing.
#pragma once

namespace TorqueWorld
{
namespace Tide
{
// Half the spring tide's range here (m): the most the sea stands above or below its mean, about 0.4 west
// of Kintyre, 3 at Dover, 6 up the Bristol Channel.
double HalfRange(double Lat, double Lon);
// The sea's height here at a time (seconds since 1970, UTC) as a share of HalfRange: about -1.25 ..
// 1.25 (beyond 1 at the biggest springs, about 0.7 at neaps).
double Height(double Lat, double Lon, double UnixSeconds);
// The last high water's height here before that time, as a share of HalfRange (for the wet band on
// the beach: the shore stays wet up to it).
double LastHigh(double Lat, double Lon, double UnixSeconds);
// Seconds since 1970 (UTC) for a day of a year (1 ..) and hours into it.
double UnixOf(int Year, int DayOfYear, double Hours);
} // namespace Tide
} // namespace TorqueWorld
