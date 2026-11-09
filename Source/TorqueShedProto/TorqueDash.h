// The bike's dashboard while riding, as Torque Shed has it: what the fitted dash part can show (its
// "features": every dash the speed, gear, trip, economy and range; most the revs; a TFT or race
// logger the shift light...), the trip computer (hud.gd's trip_km and trip_fuel), the petrol in the
// tank going down as the engine burns it (bike.gd's burn, bike_build.gd's fuel_rate), and the bike's
// own clocks moved to match: needles for speed and revs on dial clocks, shift lights, and the
// readout written on the clocks' window or screen (main.gd _update_dash, bike_model.gd set_gauges
// and dash_readout).
//
// It only reads the ride (the sim's speed, revs, gear and drive): ATorqueHUD draws the on-screen
// speedo from it (hud.gd _draw_speedo).
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueShedCatalogue.h"   // FDashGauge

class AActor;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
namespace TorqueSim { class BikeSim; struct BikeSpec; }

struct FTorqueDash
{
	// --- From the parts (Setup).
	TArray<FString> Features = {TEXT("speed")};   // BikeBuild.dash_features (a bike with no dash part: the speed)
	FString Style = TEXT("analog");               // the dash part's style: analog, twin, lcd, tft, race
	double FuelCapacity = 15.0;                   // the tank and any auxiliary tank, litres
	double Displacement = 700.0;                  // cc
	double FrictionMult = 1.0;                    // the head's thirst and the cylinders' drag
	bool bInjected = false, bTwoStroke = false;

	// --- The ride now (Step).
	double FuelLitres = 15.0;   // in the tank: full as it leaves the shed (the shed doesn't keep fluids yet)
	double TripKm = 0.0;        // this ride: the ground the bike has covered
	double TripFuel = 0.0;      // litres burnt this ride
	double TopKmh = 0.0;
	double Speed = 0.0;         // m/s, as the sim has it
	double Rpm = 0.0, Redline = 8500.0;
	int32 Gear = 1;
	bool bEngineOn = true;      // false while the starter's cranking it over
	bool bCrashed = false;

	bool Has(const TCHAR* Feature) const { return Features.Contains(Feature); }

	// Where the bike was last frame (sim metres), for the trip: a move bigger than a bike makes in a
	// frame is the bike put somewhere (back on the road, a restart) and isn't counted.
	FVector TripFrom = FVector::ZeroVector;
	bool bTripFrom = false;

	// The dash, tank and engine parts the bike has (Godot slot -> catalogue id).
	void Setup(const TMap<FString, FString>& Parts);
	// One frame of riding: the trip and the petrol it took. Throttle 0..1 (the rider's).
	void Step(const TorqueSim::BikeSim& Sim, double Throttle, bool bRunning, double Dt);

	// This ride's km per litre (0 until there's enough of the ride to go on; hud.gd's km_per_l).
	double TripEconomy() const;
	// How far the petrol left goes: at this ride's economy once there is one, else the bike's own at
	// a steady cruise (hud.gd range_km).
	double RangeKm(const TorqueSim::BikeSpec& Spec) const;
	// bike_build.gd: litres an hour making Kw at the crank at Rpm, and km per litre at 90 km/h.
	double FuelRate(const TorqueSim::BikeSpec& Spec, double Kw, double AtRpm) const;
	double CruiseEconomy(const TorqueSim::BikeSpec& Spec) const;

	// --- The bike's own clocks (catalogue bikes: the meshes TorqueShedCatalogue::MakeBike tagged).
	// Call once the meshes are on their groups (their relative transforms are kept as their rest).
	void HookClocks(AActor* Owner, const TArray<UStaticMeshComponent*>& Meshes, const TArray<TorqueShedCatalogue::FDashGauge>& Gauges);
	// Every frame: the needles, the shift lights and the readout. Units: 0 metric, 1 imperial.
	void UpdateClocks(int32 Units);
	bool HasClocks() const { return Needles.Num() > 0 || Leds.Num() > 0 || Readout.IsValid(); }

private:
	struct FNeedle
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		bool bRpm = false;
		FVector At, Rest, Side;   // the pivot, and the needle at rest (mid-scale) and a quarter turn clockwise, mesh space
		FTransform Base;          // its relative transform at rest
	};
	struct FLed
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		UMaterialInstanceDynamic* Mat = nullptr;   // held by the mesh it's on
		FLinearColor Colour;
		int32 Lit = -1;
	};
	TArray<FNeedle> Needles;
	TArray<FLed> Leds;
	TWeakObjectPtr<UTextRenderComponent> Readout;
	bool bDials = false;   // the readout is the dial's little window (dark ink), not a screen
	FString ReadoutShown;
	FColor ReadoutColour;
};
