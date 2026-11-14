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
#include "Weather/NightSky.h"
#include "Weather/StormSky.h"
#include "TorqueSky.generated.h"

class ATorqueGameState;
class UMaterialParameterCollection;
class UPointLightComponent;
class USoundWaveProcedural;
class UAudioComponent;

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
	// Stage 4: the camera's height (metres above sea level), the low ground round about and the heaviest
	// rain near (mm/h); below 0, not known.
	void Apply(const TorqueWeather::State& Weather, double SunElevation, bool bRealSun, double Haze, double GroundAsl, float Open,
		double ViewAsl = -1.0, double ValleyAsl = -1.0, double RainNear = 0.0);
	// The night (stage 5): the time the sky stands for (seconds since 1970, UTC) and the place; the game
	// state sets it with its clock before each Apply. Then: whether the moon gives the light now (the
	// sun's gone and the moon is up), and where it stands (degrees up, bearing from north).
	void SetSkyClock(double UnixSeconds, double LatDeg, double LonDeg);
	bool MoonLightsNow(double SunElevation, double& MoonElevation, double& MoonBearing) const;
	const TorqueNight::Moon& GetMoon() const { return MoonNow; }
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
	// Valley mist and fog with a top (stage 4): a pool of the engine's Local Fog Volumes (made here, by
	// class name), laid in the hollows round the camera (found by tracing down to the ground).
	TArray<TWeakObjectPtr<AActor>> MistPool;
	FVector MistPlacedAt = FVector(1e12);
	double MistPlacedTime = -1000.0, MistBaseRadius = 0.0;
	float MistApplied = -1.0f;
	void TickMist();
	// The high cloud's sheet (stage 3; Tools/ue_build_level.py tags it TorqueHighCloud), kept over the camera.
	TWeakObjectPtr<AActor> HighSheetPtr;
	void TickHighSheet();
	// Stage 4. The weather map: painted from the real weather round about (TorqueSky::PaintWeatherMap)
	// into a texture the cloud's material instance reads, so a shower over the next dale is drawn there.
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> CloudMid;
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> MapTex;
	bool bMapTried = false;
	double MapPaintedAt = -1000.0;
	FVector MapCentre = FVector(1e12);
	float MapOnApplied = -1.0f;
	void TickWeatherMap();
	// Rain shafts under the far showers (a pool the level script places, tagged TorqueRainShaft), each
	// kept on its shower and faded in and out.
	struct FShaftSlot
	{
		TWeakObjectPtr<AActor> Actor;
		TObjectPtr<class UMaterialInstanceDynamic> Mid = nullptr;
		FVector At = FVector::ZeroVector, Scale = FVector::OneVector;
		float Now = 0.0f, Want = 0.0f;
		bool bSeen = false;
		float White = 0.0f;   // (stage 7: snow or hail)
	};
	TArray<FShaftSlot> Shafts;
	double ShaftsFoundAt = -1000.0;
	void TickRainShafts(float DeltaSeconds);
	int32 ShaftsShown() const;
	// The rainbow (a sphere round the camera tagged TorqueRainbow; its material draws the bow).
	TWeakObjectPtr<AActor> RainbowPtr;
	float RainbowApplied = -1.0f;
	void TickRainbow();
	// Stage 5: the clock and place the night sky stands for, the moon then, and the star dome (a sphere
	// round the camera tagged TorqueStarDome wearing M_RideStars).
	bool bSkyClock = false;
	double SkyUnix = 0.0, SkyLat = 54.3, SkyLon = -2.2;
	TorqueNight::Moon MoonNow;
	FVector SunEnu = FVector(0.0, 0.0, 1.0);
	TWeakObjectPtr<AActor> StarDomePtr;
	void TickStarDome();
	// Stage 7: lightning and thunder. The storm cells round the camera (found every few seconds), the
	// flashes on now (each lighting the cloud round it and the land), the forks drawn (a pool of planes
	// the level script places, tagged TorqueBolt), and the thunder on its way (made when it arrives).
	TorqueWeather::State WeatherNow;
	float OpenNow = 1.0f;
	std::vector<TorqueStorm::Cell> StormCellsNow;
	double CellsFoundAt = -1000.0, StrikeClock = -1.0;
	struct FFlash
	{
		TorqueStorm::Strike S;
		FVector Where = FVector::ZeroVector;   // the flash in the level, at the cloud base (cm)
		double Dist = 0.0;                      // from the camera, metres
	};
	TArray<FFlash> Flashes;
	struct FThunderDue
	{
		double At = 0.0;    // on the sky's clock
		double Dist = 0.0;
		float Loud = 0.0f;
		uint32 Seed = 0;
	};
	TArray<FThunderDue> ThunderDue;
	struct FThunderOn
	{
		TWeakObjectPtr<UAudioComponent> Audio;
		double StopAt = 0.0;
	};
	TArray<FThunderOn> ThunderOn;
	UPROPERTY(Transient) TArray<TObjectPtr<USoundWaveProcedural>> ThunderWaves;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> FlashLight;
	struct FBoltSlot
	{
		TWeakObjectPtr<AActor> Actor;
		TObjectPtr<class UMaterialInstanceDynamic> Mid = nullptr;
		TorqueStorm::Strike S;
		FVector Ground = FVector::ZeroVector, Top = FVector::ZeroVector;
		bool bOn = false;
	};
	TArray<FBoltSlot> Bolts;
	int32 FlashCount = 0, ThunderCount = 0;
	FColor SunColour = FColor::White;
	// The clock lightning and meteors keep: the server's world time (the same on every machine in co-op).
	double StormClock() const;
	void TickLightning(float DeltaSeconds);
	void PlayThunder(const FThunderDue& Due);
	void TickMeteors();
	// The ground under the viewer from the last Apply (metres above sea level).
	double GroundAslNow = 0.0;
	// The camera, where it is on Earth, and the level origin's height above sea level.
	bool CameraGeo(FVector& Cam, double& Lat, double& Lon, double& OriginAsl) const;

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
		double Pitch = 4.0; // for horizon: degrees up
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
