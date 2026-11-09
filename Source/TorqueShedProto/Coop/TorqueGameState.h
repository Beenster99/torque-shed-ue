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

// The host's weather as it is now, sent to everyone (the numbers of TorqueWeather::State, and where
// they came from).
USTRUCT()
struct FTorqueWeatherNet
{
	GENERATED_BODY()
	UPROPERTY() float Cloud = 0.1f;
	UPROPERTY() float Rain = 0.0f;
	UPROPERTY() float Snow = 0.0f;
	UPROPERTY() float Wind = 3.0f;
	UPROPERTY() float WindFrom = 225.0f;
	UPROPERTY() float Gust = 6.0f;
	UPROPERTY() float Air = 14.0f;
	UPROPERTY() float Visibility = 40000.0f;
	UPROPERTY() float RoadWet = 0.0f;
	UPROPERTY() int32 Code = 1;
	// ETorqueWeatherSource, and when the live weather was fetched (seconds since 1970, 0 for never).
	UPROPERTY() uint8 Source = 0;
	UPROPERTY() int64 FetchedAt = 0;
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
	// The weather as it is now (fading), on every machine.
	const TorqueWeather::State& GetWeatherState() const { return Fade.Now; }
	ETorqueWeatherSource GetWeatherSource() const { return (ETorqueWeatherSource)Net.Source; }
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
	double NextFetchIn = 0.0;
	int32 LiveFailures = 0;
	bool bFetching = false, bLiveApplied = false;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> LiveRequest;
	UPROPERTY(ReplicatedUsing = OnRep_Hour) float Hour = -1.0f;
	UFUNCTION() void OnRep_Hour();
	double WeatherChangeIn = 0.0;   // the host, weather changing by itself: seconds to the next change
	double HourSentAt = 0.0;
	float BuiltAt = 15.0f;
	// The sun for the clock: its height and bearing for Swaledale, the level's own sun at BuiltAt.
	void ApplySun();
	bool bSunCaptured = false;
	FRotator BuiltSun = FRotator::ZeroRotator;
	double SunYawOffset = 0.0;
	float DayLight = 1.0f;   // 0 at night .. 1 by day: the sun's brightness and the exposure follow
	float AppliedDayLight = -1.0f;
	TWeakObjectPtr<class ADirectionalLight> SunActorPtr;

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

	// The level's light as it was built (sunny), kept the first time so each weather is made from it.
	void ApplyWeather();
	bool bCaptured = false;
	float SunLux = 0.f, SunAngle = 0.f, SkyIntensity = 1.f, FogDensity = 0.f, Mie = 0.f, Rayleigh = 0.f;
	float ExposureBias = 0.f, ExposureMin = 0.f;
	TWeakObjectPtr<class UDirectionalLightComponent> SunPtr;
	TWeakObjectPtr<class USkyLightComponent> SkyPtr;
	TWeakObjectPtr<class UExponentialHeightFogComponent> FogPtr;
	TWeakObjectPtr<class USkyAtmosphereComponent> AtmoPtr;
	TWeakObjectPtr<class APostProcessVolume> PpvPtr;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> WeatherMpc;
	bool bLooked = false;
	int32 LoggedKind = -1;
};
