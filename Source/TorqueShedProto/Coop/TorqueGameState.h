// What everyone in a ride shares, owned by the host:
// - the weather: one smooth weather state (Weather/WeatherModel.h) from the host's Settings > Gameplay >
//   Weather: Live (the real weather where you ride, from Open-Meteo), changing by itself, or held at
//   sunny, overcast or wet roads. Changes fade over time. The host changes it (also V or D-pad right
//   on the bike, or -TorqueWeather= at launch) and every machine applies the same sun, sky, haze,
//   exposure and wet roads;
// - the shed (TorqueShedNet.h): the host's bikes and parts, sent to everyone as they change;
// - the time of day (Settings > Gameplay > Time and weather, as Torque Shed's Daylight): the host's
//   clock, running or held, moves the sun for everyone; and the weather changing by itself.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TorqueShedNet.h"
#include "Weather/WeatherModel.h"
#include "Interfaces/IHttpRequest.h"
#include "TorqueGameState.generated.h"

class UMaterialParameterCollection;
class ATorqueSky;

// The host's weather as it is now, sent to everyone (the numbers of TorqueWeather::State, and where
// they came from).
USTRUCT()
struct FTorqueWeatherNet
{
	GENERATED_BODY()
	UPROPERTY() float Cloud = 0.1f;
	UPROPERTY() float Rain = 0.0f;
	UPROPERTY() float Snow = 0.0f;
	UPROPERTY() float Wind = 5.5f;
	UPROPERTY() float WindFrom = 225.0f;
	UPROPERTY() float Gust = 9.0f;
	UPROPERTY() float Air = 14.0f;
	UPROPERTY() float Visibility = 40000.0f;
	UPROPERTY() float RoadWet = 0.0f;
	UPROPERTY() int32 Code = 1;
	UPROPERTY() float LowCloud = 0.0f;
	UPROPERTY() float CloudBase = 5000.0f;
	UPROPERTY() float Puddles = 0.0f;
	UPROPERTY() float SnowDepth = 0.0f;   // (lying snow, m)
	// The sky's (skies plan stage 1): the cloud layers, the air, the sunshine and the wind aloft.
	UPROPERTY() float MidCloud = 0.0f;
	UPROPERTY() float HighCloud = 0.1f;
	UPROPERTY() float Humidity = 70.0f;
	UPROPERTY() float Cape = 0.0f;
	UPROPERTY() float FreezingLevel = 2500.0f;
	UPROPERTY() float DirectSun = -1.0f;
	UPROPERTY() float DiffuseSun = -1.0f;
	UPROPERTY() float Wind850 = 9.0f;
	UPROPERTY() float Wind850From = 235.0f;
	UPROPERTY() float Wind700 = 14.0f;
	UPROPERTY() float Wind700From = 245.0f;
	UPROPERTY() float Wind300 = 28.0f;
	UPROPERTY() float Wind300From = 255.0f;
	// ETorqueWeatherSource, and when the live weather was fetched (seconds since 1970, 0 for never).
	UPROPERTY() uint8 Source = 0;
	UPROPERTY() int64 FetchedAt = 0;
};

// The host's weather grid (stage 5), sent to everyone: N x N points round a centre, so each machine
// works out the weather where its own rider is.
USTRUCT()
struct FTorqueWeatherGridNet
{
	GENERATED_BODY()
	UPROPERTY() int32 N = 0;   // 0: no grid (one place's weather for all)
	UPROPERTY() double Lat = 0.0;
	UPROPERTY() double Lon = 0.0;
	UPROPERTY() float StepM = 2500.0f;
	UPROPERTY() TArray<FTorqueWeatherNet> Points;
	UPROPERTY() TArray<float> Elevation;
	UPROPERTY() int64 FetchedAt = 0;   // seconds since 1970: how long its showers have drifted
};

// Where the weather now comes from.
enum class ETorqueWeatherSource : uint8
{
	Fixed,         // held at one kind (or the V key's)
	ByItself,      // changing by itself
	Live,          // live, fresh
	LiveLastKnown, // live, but the service can't be reached: the last weather it gave
	LiveWaiting,   // live, asking for the first time
	LiveOffline,   // live, never reached (or the last is hours old): changing by itself meanwhile
};

UCLASS()
class ATorqueGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ATorqueGameState();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// The kind the weather is nearest: 0 sunny, 1 overcast, 2 wet roads (the bike's grip until stage 4).
	int32 GetWeather() const { return Weather; }
	// The weather as it is now (fading) where this machine's camera is: the shared weather, made
	// local from the host's grid (live), on every machine.
	const TorqueWeather::State& GetWeatherState() const { return Here; }
	ETorqueWeatherSource GetWeatherSource() const { return (ETorqueWeatherSource)Net.Source; }
	// How open to the sky this machine's camera is: 0 under a roof .. 1 out in it (the rain's).
	float WeatherOpen() const;
	// [lights] 0 at night .. 1 by day, as the sun and the exposure have it (the bike's lamps glow by it).
	float GetDayLight() const { return DayLight; }
	// "Live: 9 °C, light rain, wind SW 18 mph", or "sunny", for the menu and the HUD.
	FString WeatherText(bool bImperial) const;
	// The host only (ignored elsewhere): change it for everyone, fading over FadeSeconds (0: at once).
	void SetWeather(int32 NewWeather, float FadeSeconds = 20.0f);
	// The host only: Settings' Weather has changed (FTorqueSettings::WeatherMode); go to it.
	void WeatherModeChanged(float FadeSeconds = 20.0f);

	// The clock: hours (0 .. 24), or -1 while the sun is where the level was built with it.
	float GetHour() const { return Hour; }
	// The hour the level's own sun stands for (found from its height in the sky), for the menu.
	float BuiltHour() const { return BuiltAt; }
	// The host only: set the clock (-1: the level's own light).
	void SetHour(float NewHour);
	// "15:30".
	static FString ClockText(float Hours);
	// [test] a test run's step turned Real time of day on or off (test runs otherwise keep the made clock).
	// (Both last the whole test run, through the move from the shed to the ride and back, as a player's
	// settings do: a new map has a new game state.)
	static void TestRealClock(bool bOn) { bRealClockTest = bOn; }
	// [test] a test run's step set the clock (set:timeofday): the next map starts at it too.
	static void TestClockSet() { bTestClockSet = true; }
	// [test] the weather where this machine's camera is, every number, and where that is on Earth.
	FString WeatherHereText() const;
	// [info] A point of the level (cm) as a place on Earth and its height above sea level, metres, by
	// the route's origin (Buttertubs' origin_geo, or -TorqueWeatherAt=).
	void GeoAt(const FVector& At, double& Lat, double& Lon, double& Asl) const;
	// The sky (Weather/TorqueSky.h): this machine's, and how far the wind has carried the clouds (the
	// host's, shared so everyone sees the same cloud in the same place; false until the host has sent one).
	ATorqueSky* GetSky() const;
	// (Layer 0 low, 1 middle, 2 high.)
	void ShareSkyDrift(int32 Layer, double East, double North);
	bool SharedSkyDrift(int32 Layer, double& East, double& North) const;
	// [test] the sky bench: this weather held, and the sun as it stands at latitude Lat on day Day
	// (1 .. 366) at solar time SolarHour (as the real sun, so the feed's sunshine figures fit it).
	void BenchSky(const TorqueWeather::State& S, double Lat, int32 Day, double SolarHour);

private:
	UPROPERTY(ReplicatedUsing = OnRep_Weather) int32 Weather = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Net) FTorqueWeatherNet Net;
	UFUNCTION() void OnRep_Net();
	TorqueWeather::Fader Fade;
	int32 Mode = 1;                 // the host's: FTorqueSettings::WeatherMode, or the launch's -TorqueWeather=
	double NetSentAt = -1.0, AppliedAt = -1.0;
	void GoToMode(float FadeSeconds);
	void TickWeather(float DeltaSeconds);
	void SendNet();
	void SetFromState(const TorqueWeather::State& S);
	// Live (the host's): asks Open-Meteo, then MET Norway when it can't; again every 15 minutes, sooner
	// after a failure. The last answer is kept (GameUserSettings.ini [TorqueWeather]) for a start offline.
	void FetchLive(int32 Feed);
	void LiveAnswer(int32 Feed, bool bOk, const FString& Body);
	void LiveFailed(int32 Feed, const FString& Why);
	double LatitudeAt = 54.3535, LongitudeAt = -2.2058;
	// The level's height above sea level at its origin (route.json's origin_region_m[1]), for low cloud.
	double OriginAsl = 515.0;
	float AppliedHaze = 1.0f;
	double HazeCheckAt = 0.0;
	float HazeNow() const;
	double NextFetchIn = 0.0;
	int32 LiveFailures = 0;
	bool bFetching = false, bLiveApplied = false;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> LiveRequest;
	UPROPERTY(ReplicatedUsing = OnRep_Hour) float Hour = -1.0f;
	UFUNCTION() void OnRep_Hour();
	// The real time of day (Settings' Real time of day): the sun for the date and the place. The sun
	// stands where solar time Hour + SunShift puts it, at latitude SunLat on a day of declination
	// SunDec (the made days: 0, Swaledale, late spring).
	UPROPERTY(ReplicatedUsing = OnRep_Hour) float SunShift = 0.0f;
	UPROPERTY(ReplicatedUsing = OnRep_Hour) float SunDec = 15.0f;
	UPROPERTY(ReplicatedUsing = OnRep_Hour) float SunLat = 54.35f;
	void TickRealClock();
	static bool bRealClockTest, bTestClockSet;
	int32 UtcOffset = 0;      // the weather's place's clock, seconds ahead of UTC (from Open-Meteo)
	bool bUtcKnown = false;
	double ClockSentAt = -100.0;
	// Weather across distance (stage 5): the host's grid, the one before it (fading out), and the
	// weather here worked out from them four times a second.
	UPROPERTY(ReplicatedUsing = OnRep_Grid) FTorqueWeatherGridNet GridNet;
	UFUNCTION() void OnRep_Grid();
	void SetGrid(const TorqueWeather::Grid& G, int64 FetchedAt, double FadeSeconds);
	void SendGrid();
	void UpdateHere();
	void ViewGeo(double& Lat, double& Lon, double& Asl) const;
	TorqueWeather::Grid GridNow, GridBefore;
	double GridFadeFrom = 0.0, GridFadeSeconds = 0.0;   // (real seconds)
	int64 GridFetchedNow = 0, GridFetchedBefore = 0;
	TorqueWeather::State Here, HereApplied;
	double HereAt = -1.0;
	double FetchLat = 0.0, FetchLon = 0.0, LastFetchAt = -1000.0;
	static constexpr int32 GRID_N = 5;
	static constexpr double GRID_STEP_M = 2500.0;
	double WeatherChangeIn = 0.0;   // the host, weather changing by itself: seconds to the next change
	double HourSentAt = 0.0;
	float BuiltAt = 15.0f;
	// The sun for the clock: its height and bearing for Swaledale, the level's own sun at BuiltAt.
	void ApplySun();
	bool bSunCaptured = false;
	FRotator BuiltSun = FRotator::ZeroRotator;
	double SunYawOffset = 0.0;
	float DayLight = 1.0f;   // 0 at night .. 1 by day: the exposure, the haze's glow and the lamps follow
	float AppliedDayLight = -1.0f;
	double SunElevationNow = 30.0, AppliedSunElevation = -1000.0;   // the sun's height, degrees
	bool bRealSunNow = false;   // the sun is the real one (Real time of day, or the bench)
	bool bSunHeld = false;      // [test] the bench holds the sun's day and place
	UPROPERTY(Transient) TObjectPtr<ATorqueSky> Sky;
	UPROPERTY(Replicated) FVector2D SkyDriftLow = FVector2D::ZeroVector;    // metres east, north
	UPROPERTY(Replicated) FVector2D SkyDriftMid = FVector2D::ZeroVector;
	UPROPERTY(Replicated) FVector2D SkyDriftHigh = FVector2D::ZeroVector;
	UPROPERTY(Replicated) bool bSkyDriftSet = false;
	TWeakObjectPtr<class ADirectionalLight> SunActorPtr;

public:
	// [Ride UK] The host's journey in the Route planner: its code and its name on a line each ("" for
	// none). Everyone's game plans the same road from it ahead of setting off (the cells and the
	// ground are fetched on each machine), and says so (ATorquePlayerState::RideReadyFor).
	UPROPERTY(Replicated) FString RideJourney;
	void SetRideJourney(const FString& CodeAndName) { if (HasAuthority()) RideJourney = CodeAndName; }
private:
	// The shed: written by the host, read by everyone else.
	UPROPERTY(ReplicatedUsing = OnRep_Shed) FTorqueShedBayNet Shed;
	UFUNCTION() void OnRep_Shed();
	void SendShed();
	// The host's changes come in bursts (a pour, a wash, a stir several times a second): the shed
	// goes out at once, then at most ten times a second, the latest each time.
	void QueueShed();
	void FlushShed();
	double ShedSentAt = -1.0;
	bool bShedQueued = false;
	FTimerHandle ShedTimer;
	int32 AppliedVersion = 0;
	FString LoggedBikes;
	FDelegateHandle HostChangeHandle;
	UFUNCTION() void OnRep_Weather();

	// The weather on screen: the sky (ATorqueSky: sun, sky light, air, fog, clouds, camera) and the
	// materials' weather (MPC_Weather: wet roads, wind, rain, puddles, snow, night).
	void ApplyWeather();
	float AppliedOpen = -1.0f;   // how open to the sky the camera was when the exposure was last set
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> WeatherMpc;
	// The weather effects you see around you (rain, splashes, tyre spray): one per machine.
	UPROPERTY(Transient) TObjectPtr<class ATorqueWeatherFx> Fx;
	int32 LoggedKind = -1;
};
