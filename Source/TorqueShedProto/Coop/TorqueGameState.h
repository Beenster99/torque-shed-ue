// What everyone in a ride shares, owned by the host:
// - the weather: the host changes it (V or D-pad right on the bike, or -TorqueWeather= at launch)
//   and every machine applies the same sun, sky, haze, exposure and wet roads;
// - the shed (TorqueShedNet.h): the host's bikes and parts, sent to everyone as they change.
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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// 0 sunny, 1 overcast, 2 wet roads.
	int32 GetWeather() const { return Weather; }
	// The host only (ignored elsewhere): change it for everyone.
	void SetWeather(int32 NewWeather);

private:
	UPROPERTY(ReplicatedUsing = OnRep_Weather) int32 Weather = 0;

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
	FDelegateHandle HostChangeHandle;
	UFUNCTION() void OnRep_Weather();

	// The level's light as it was built (sunny), kept the first time so each weather is made from it.
	void ApplyWeather();
	bool bCaptured = false;
	float SunLux = 0.f, SunAngle = 0.f, SkyIntensity = 1.f, FogDensity = 0.f, Mie = 0.f, Rayleigh = 0.f;
	float ExposureBias = 0.f, ExposureMin = 0.f;
};
