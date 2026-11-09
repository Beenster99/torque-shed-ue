// What everyone in a ride shares, owned by the host:
// - the weather: the host changes it (V or D-pad right on the bike, or -TorqueWeather= at launch)
//   and every machine applies the same sun, sky, haze, exposure and wet roads;
// - the shed (TorqueShedNet.h): the host's bikes and parts, sent to everyone as they change;
// - the time of day (Settings > Gameplay > Time and weather, as Torque Shed's Daylight): the host's
//   clock, running or held, moves the sun for everyone; and the weather changing by itself.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TorqueShedNet.h"
#include "TorqueGameState.generated.h"

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

	// 0 sunny, 1 overcast, 2 wet roads.
	int32 GetWeather() const { return Weather; }
	// The host only (ignored elsewhere): change it for everyone.
	void SetWeather(int32 NewWeather);

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
};
