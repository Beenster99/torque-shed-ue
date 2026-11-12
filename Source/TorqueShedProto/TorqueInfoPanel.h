// [info] The riding info panel (beenster, Nov 12): a see-through panel down the left of the riding
// HUD that F1 slides in and out (ETorqueKey::InfoPanel; kept in FTorqueSettings::bInfoPanel). It shows
//  - the date and the game's clock, and the build (Data/build.json, as the title screen);
//  - the weather here with a drawn icon (ATorqueGameState's live weather state), the temperature and
//    how cold it feels at the speed you're riding, the wind, rain, fog and the road;
//  - the forecast for the real place you're riding: the next 12 hours and the next three days, with
//    sunrise and sunset (Open-Meteo, the same free feed the live weather uses);
//  - where you are: the road, the village or town, the county (OpenStreetMap's Nominatim, asked at
//    most every 15 s and only after you've gone 300 m), your height and heading;
//  - about this place: the nearest Wikipedia article with a few lines of it, and what else is near;
//  - who's riding: each rider's profile name in their colour, their bike, speed and how far away.
//
// Where on Earth: on Buttertubs (and in the shed's yard) the level's origin from the route
// (ATorqueGameState::GeoAt); on a Ride UK ride, the ride's own frame (RidePlan's region).
//
// The internet is only asked while the panel is out and Settings' "about this place" is on, so test
// runs (panel off unless asked: -TorqueInfoPanel) never ask. Log lines start "Torque info:".
#pragma once

#include "CoreMinimal.h"

class UCanvas;
class AHUD;
class ATorqueBikePawn;

class FTorqueInfoPanel
{
public:
	FTorqueInfoPanel();
	// Draws it (sliding) down the left, its top no higher than TopY (under the corner lines).
	void Draw(AHUD* Hud, UCanvas* Canvas, const ATorqueBikePawn* Bike, float TopY, float Dt);

	// What the internet said (shared with the requests in flight, which may outlive a frame).
	struct FHour { FString Label; int32 Code = 0; float Temp = 0.0f; int32 RainPct = 0; bool bDay = true; };
	struct FDay { FString Label; int32 Code = 0; float Max = 0.0f, Min = 0.0f; int32 RainPct = 0; };
	struct FNear { FString Title, Text; double Metres = 0.0; };
	struct FData
	{
		// OpenStreetMap: the road, the place, the county (or what stands for them abroad), the country.
		FString Road, Place, Area, Country;
		bool bPlaceBusy = false, bPlaceFailed = false;
		double PlaceLat = 0.0, PlaceLon = 0.0, PlaceAsked = -1000.0;
		// Wikipedia: the nearest article with words, then the others near.
		TArray<FNear> Near;
		bool bWikiBusy = false, bWikiFailed = false;
		double WikiLat = 0.0, WikiLon = 0.0, WikiAsked = -1000.0;
		// Open-Meteo's forecast.
		TArray<FHour> Hours;
		TArray<FDay> Days;
		FString Sunrise, Sunset;
		bool bForecastBusy = false, bForecastFailed = false;
		double ForecastLat = 0.0, ForecastLon = 0.0, ForecastAsked = -10000.0;
	};

private:
	TSharedRef<FData> Data;
	float Slide = 0.0f;       // 0 hidden .. 1 out
	float ShownTop = -1.0f;   // eased, so a note coming and going above doesn't jolt it
	double LogClock = 3.0;
	void Ask(double Lat, double Lon, double Now);
};
