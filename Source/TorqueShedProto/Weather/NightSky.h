// The night sky (skies plan stage 5), in plain C++ (SimTest sky_test checks it): where the moon is and
// how much of it is lit, the stars' clock (local sidereal time), and a place in the sky from the
// stars' coordinates (right ascension, declination) to the viewer's (height, bearing).
//
// The moon is Paul Schlyter's low-precision method ("How to compute planetary positions"): its orbit
// with the twelve biggest perturbations, about a fifth of a degree from the real one, plus the parallax
// of standing on the Earth's surface (up to a degree). Good for a moon drawn in the sky and its light.
#pragma once

namespace TorqueNight
{
// Days and fractions from 2000 January 0.0 UT (Schlyter's d) for a time in seconds since 1970 (UTC).
double DaysFrom2000(double UnixSeconds);
// Greenwich mean sidereal time, and local at LonDeg east, in degrees (0 .. 360).
double SiderealDeg(double UnixSeconds, double LonDeg);

// A place among the stars (right ascension and declination, degrees) as seen at a place at a time:
// its height above the horizon and bearing from north, clockwise, degrees.
void HorizonOf(double RaDeg, double DecDeg, double UnixSeconds, double LatDeg, double LonDeg, double& Elevation, double& Bearing);

struct Moon
{
	double RaDeg = 0.0, DecDeg = 0.0;   // geocentric, of date
	double Elevation = -90.0, Bearing = 0.0;   // as seen from the place (parallax in)
	double Lit = 0.0;          // the fraction of the disc lit, 0 new .. 1 full
	double PhaseAngle = 180.0; // sun - moon - Earth, degrees: 0 full .. 180 new
	double Elongation = 0.0;   // from the sun, degrees
	bool bWaxing = true;       // lit on its west side (the evening's moon) and growing
	double DistanceKm = 384400.0;
	double SunRaDeg = 0.0, SunDecDeg = 0.0;   // the sun's, for the same time (the lit side faces it)
};
Moon MoonAt(double UnixSeconds, double LatDeg, double LonDeg);

// How bright the moon is next to a full one (the phase law: a half moon gives about a tenth of a full).
double MoonBrightness(double PhaseAngle);
} // namespace TorqueNight
