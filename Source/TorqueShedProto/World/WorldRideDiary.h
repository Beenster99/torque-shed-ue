// The ride diary (Ride UK stage 7): each Ride UK ride kept in the shed, its road drawn on the Route
// planner's map with the photos taken on it pinned where they were taken.
//
// Plain C++ (SimTest's world_test tries it): the game steps it as the rider goes and saves it as
// JSON in Saved/RideDiary (one file a ride, its photos beside it).
#pragma once

#include "WorldGeo.h"

#include <string>
#include <vector>

namespace TorqueWorld
{
struct DiaryPhoto
{
	double Lat = 0.0, Lon = 0.0;
	double Metres = 0.0;      // how far into the ride
	long long When = 0;       // seconds since 1970
	std::string File;         // its picture, beside the diary's file
};

struct DiaryRide
{
	std::string Id;           // "20261112-213000": when it began, and its file's name
	std::string Title;        // what the ride was ("Ride UK: Hawes to Leyburn")
	std::string Code;         // its journey's code (TS1.), to ride it again
	long long Started = 0, Ended = 0;
	double Metres = 0.0;      // ridden
	double Top = 0.0;         // the fastest, m/s
	double Moving = 0.0;      // seconds on the move
	std::vector<Geo> Track;   // the road ridden, a point every TrackStep metres or so
	std::vector<DiaryPhoto> Photos;
	static constexpr double TrackStep = 40.0;

	// One step of the ride: the rider at (Lat, Lon), going Speed (m/s), Dt seconds since the last. A
	// jump of more than JumpM (put back on the road, a restart) isn't counted as ridden.
	void Step(double Lat, double Lon, double Speed, double Dt, long long Now);
	static constexpr double JumpM = 150.0;
	// A photo taken now, at the rider's last step.
	void AddPhoto(const std::string& File, long long Now);

	std::string ToJson() const;
	static bool FromJson(const std::string& Text, DiaryRide& Out);
	// "20261112-213000" for a time (UTC).
	static std::string IdFor(long long Seconds);

private:
	Geo Last;
	bool bLast = false;
};
} // namespace TorqueWorld
