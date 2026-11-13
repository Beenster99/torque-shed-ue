// The sky over the rider (skies plan, stages 1 and 2), one per machine, spawned by ATorqueGameState.
//
// The game state keeps the weather and the clock; this actor turns them into the sky: it works out one
// sky state (Weather/SkyModel.h) and sets the level's sun, sky light, atmosphere, height fog, clouds and
// camera from it. Every tuning figure is in Data/Sky/sky_looks.json, read at start (no recompile to
// retune). The clouds (M_RideCloud on the level's Volumetric Cloud, made by Tools/ue_build_level.py)
// read MPC_Sky: the real cover, how dense, how flat, and how far the wind has carried them, which the
// host shares so everyone in co-op sees the same cloud in the same place.
//
// The sky bench: -TorqueSkyBench[=<file in Data/Sky>] (bench.json by default) steps through a fixed list
// of skies (a place, a day, the sun's height, a weather or a recorded day), takes one screenshot of each
// into Saved/Screenshots/SkyBench with the sky's numbers beside them (bench.json there), and quits.
// Tools/sky_bench_sheet.py lays them out on one sheet to judge beside photographs.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weather/SkyModel.h"
#include "TorqueSky.generated.h"

class ATorqueGameState;
class UMaterialParameterCollection;

UCLASS(NotPlaceable, Transient)
class ATorqueSky : public AActor
{
	GENERATED_BODY()

public:
	ATorqueSky();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// The sky for this weather (the weather where this machine's camera is), with the sun SunElevation
	// degrees up (the real sun when bRealSun), the viewer's haze (WeatherModel's HazeAt), the ground under
	// the viewer (metres above sea level) and how open to the sky the camera is (0 under a roof .. 1).
	void Apply(const TorqueWeather::State& Weather, double SunElevation, bool bRealSun, double Haze, double GroundAsl, float Open);
	// The day's light (0 night .. 1 day) and where the light points (degrees above the horizon) for the
	// sun at this height: the lamps, the exposure and the haze's glow follow the first.
	float DayLightAt(double SunElevation) const;
	double LightElevationAt(double SunElevation) const;
	const TorqueSky::Sky& Now() const { return Last; }
	const TorqueSky::Looks& GetLooks() const { return Looks; }
	// [test] the sky's numbers, for the log and the bench.
	FString Describe() const;

private:
	TorqueSky::Looks Looks;
	TorqueSky::Sky Last;
	bool bApplied = false;
	TWeakObjectPtr<ATorqueGameState> Game;

	// The level's pieces, and their look as built (kept the first time).
	void Find();
	bool bFound = false, bCaptured = false;
	TWeakObjectPtr<class UDirectionalLightComponent> SunPtr;
	TWeakObjectPtr<class USkyLightComponent> SkyPtr;
	TWeakObjectPtr<class UExponentialHeightFogComponent> FogPtr;
	TWeakObjectPtr<class USkyAtmosphereComponent> AtmoPtr;
	TWeakObjectPtr<class UVolumetricCloudComponent> CloudPtr;
	TWeakObjectPtr<class APostProcessVolume> PpvPtr;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> SkyMpc;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> WeatherMpc;
	float SunLux = 0.f, SunAngle = 0.f, SkyIntensity = 1.f, FogDensity = 0.f, Mie = 0.f, Rayleigh = 0.f;
	FLinearColor FogInscatter = FLinearColor::Black, FogSunInscatter = FLinearColor::Black;
	float ExposureBias = 0.f, ExposureMin = 0.f, ExposureMax = 0.f;
	float AppliedShadowStrength = -1.0f, AppliedBottomKm = -1.0f, AppliedHeightKm = -1.0f;
	// The clouds are drawn: the volumetric clouds are on (Medium graphics and up) and the level's cloud
	// wears our own material (an older level keeps the engine's, which follows nothing).
	bool CloudsDrawn() const;
	// The level's planet surface, metres above sea level (the cloud layer's heights are above it).
	double PlanetAsl() const;
	// The high cloud's sheet (stage 3; Tools/ue_build_level.py tags it TorqueHighCloud), kept over the camera.
	TWeakObjectPtr<AActor> HighSheetPtr;
	void TickHighSheet();

	// The clouds' drift: how far the wind at each layer's height (low, middle, high) has carried it,
	// metres east and north. The host adds it up and shares it; the others add it up too and ease
	// towards the host's.
	double DriftE[3] = {0.0, 0.0, 0.0}, DriftN[3] = {0.0, 0.0, 0.0};
	double SentDriftAt = -100.0;
	void TickDrift(float DeltaSeconds);

	// The sky bench.
	struct FShot
	{
		FString Name;
		TorqueWeather::State Weather;
		double Lat = 54.35;
		int32 Day = 172;
		double SolarHour = 12.0;
		FString Look;     // horizon (default), up, sun, away
		double Yaw = 0.0; // for horizon: degrees from north, clockwise
		FString Note;
	};
	TArray<FShot> Shots;
	int32 ShotAt = -1;
	double ShotWait = 0.0, Settle = 8.0;
	bool bShotTaken = false;
	FString BenchDir;
	FString BenchLog;
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> BenchCamera;
	bool LoadBench(const FString& File);
	void TickBench(float DeltaSeconds);
	void ShowShot(const FShot& Shot);
};
