#include "TorqueGameState.h"

#include "TorquePlayerState.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

void ATorqueGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATorqueGameState, Weather);
	DOREPLIFETIME(ATorqueGameState, Shed);
}

void ATorqueGameState::BeginPlay()
{
	Super::BeginPlay();
	// -TorqueWeather=sunny|overcast|wet sets the start (the host's, in co-op).
	FString WeatherName;
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("TorqueWeather="), WeatherName))
		Weather = WeatherName.StartsWith(TEXT("wet")) || WeatherName.StartsWith(TEXT("rain")) ? 2 : WeatherName.StartsWith(TEXT("over")) ? 1 : 0;
	ApplyWeather();

	UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build) return;
	// Whose bike a ridden pawn draws: its player's bay (ATorquePlayerState), so everyone's bikes
	// look as their owners built them. Our own pawn: this machine's MyBike.
	Build->BikeOfPawn.BindLambda([](const APawn* P) -> int32 {
		if (!P || P->IsLocallyControlled()) return -1;
		const ATorquePlayerState* PS = P->GetPlayerState<ATorquePlayerState>();
		return PS ? PS->Bay : -1;
	});
	// Hosting: send the shed now, and again whenever it changes.
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		SendShed();
		HostChangeHandle = Build->OnHostChange.AddUObject(this, &ATorqueGameState::QueueShed);
	}
}

void ATorqueGameState::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this))
	{
		Build->OnHostChange.Remove(HostChangeHandle);
		Build->BikeOfPawn.Unbind();
	}
	Super::EndPlay(Reason);
}

void ATorqueGameState::SendShed()
{
	const UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build || !HasAuthority()) return;
	TArray<uint8> Data;
	if (!Build->SaveShed(Data)) return;
	Shed.Data = MoveTemp(Data);
	++Shed.Version;
	ForceNetUpdate();
	if (const UWorld* World = GetWorld()) ShedSentAt = World->GetRealTimeSeconds();
}

void ATorqueGameState::QueueShed()
{
	constexpr double Gap = 0.1;
	UWorld* World = GetWorld();
	if (!World) return;
	if (bShedQueued) return;   // already going out with the next one
	const double Since = World->GetRealTimeSeconds() - ShedSentAt;
	if (ShedSentAt < 0.0 || Since >= Gap)
	{
		SendShed();
		return;
	}
	bShedQueued = true;
	World->GetTimerManager().SetTimer(ShedTimer, FTimerDelegate::CreateUObject(this, &ATorqueGameState::FlushShed),
		(float)FMath::Max(Gap - Since, 0.01), false);
}

void ATorqueGameState::FlushShed()
{
	bShedQueued = false;
	SendShed();
}

void ATorqueGameState::OnRep_Shed()
{
	UTorqueBuildSubsystem* Build = UTorqueBuildSubsystem::Of(this);
	if (!Build || Shed.Version == 0 || Shed.Version == AppliedVersion) return;
	if (!Build->LoadShedFromHost(Shed.Data)) return;
	AppliedVersion = Shed.Version;
	// For the two-copy tests: where each bike stands on this (joined) copy, when that changes.
	FString Where;
	const int32 OnRig = Build->DynoBay();
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
	{
		if (!Build->Exists(Bay)) continue;
		const FTorqueShedBike& Bike = Build->Bike(Bay);
		const FVector At = Build->BikeTransform(Bay).GetLocation();
		const TCHAR* Parked = Bike.Pusher != 0 ? TEXT("being wheeled") : OnRig == Bay ? TEXT("on the dyno rig") : !Bike.bAway ? TEXT("in its bay") : TEXT("on the floor");
		Where += FString::Printf(TEXT("%sbay %d %s %s at (%.0f, %.0f)"), Where.IsEmpty() ? TEXT("") : TEXT("; "), Bay + 1, *Bike.Name, Parked, At.X, At.Y);
	}
	if (Where != LoggedBikes)
	{
		LoggedBikes = Where;
		UE_LOG(LogTemp, Log, TEXT("Torque net: shed from the host (version %d): %s"), Shed.Version, Where.IsEmpty() ? TEXT("no bikes") : *Where);
	}
}

void ATorqueGameState::SetWeather(int32 NewWeather)
{
	if (!HasAuthority()) return;
	Weather = ((NewWeather % 3) + 3) % 3;
	ApplyWeather();   // here; the others get it through OnRep_Weather
}

void ATorqueGameState::OnRep_Weather()
{
	ApplyWeather();
}

// Weather: the level is built sunny; overcast dims and softens the sun, greys the sky and thickens the
// haze; wet is overcast plus wet roads (the materials' Wetness, MPC_Weather). The bikes take the grip
// from GetWeather.
void ATorqueGameState::ApplyWeather()
{
	UWorld* W = GetWorld();
	if (!W) return;
	UDirectionalLightComponent* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(W); It && !Sun; ++It) Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
	USkyLightComponent* Sky = nullptr;
	for (TActorIterator<ASkyLight> It(W); It && !Sky; ++It) Sky = It->GetLightComponent();
	UExponentialHeightFogComponent* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(W); It && !Fog; ++It) Fog = It->GetComponent();
	USkyAtmosphereComponent* Atmo = nullptr;
	for (TActorIterator<AActor> It(W); It && !Atmo; ++It) Atmo = It->FindComponentByClass<USkyAtmosphereComponent>();
	APostProcessVolume* Ppv = nullptr;
	for (TActorIterator<APostProcessVolume> It(W); It && !Ppv; ++It) if (It->bUnbound) Ppv = *It;
	if (!bCaptured)
	{
		bCaptured = true;
		if (Sun) { SunLux = Sun->Intensity; SunAngle = Sun->LightSourceAngle; }
		if (Sky) SkyIntensity = Sky->Intensity;
		if (Fog) FogDensity = Fog->FogDensity;
		if (Atmo) { Mie = Atmo->MieScatteringScale; Rayleigh = Atmo->RayleighScatteringScale; }
		if (Ppv) { ExposureBias = Ppv->Settings.AutoExposureBias; ExposureMin = Ppv->Settings.AutoExposureMinBrightness; }
	}
	const bool bDull = Weather >= 1;
	const bool bWet = Weather == 2;
	if (Sun)
	{
		Sun->SetIntensity(bDull ? SunLux * (bWet ? 0.10f : 0.14f) : SunLux);
		Sun->SetLightSourceAngle(bDull ? 40.0f : SunAngle);   // a sun behind cloud: next to no shadow
	}
	if (Sky) Sky->SetIntensity(bDull ? SkyIntensity * 2.5f : SkyIntensity);
	// A real camera opens up under cloud: the exposure follows (the level's histogram range was set for
	// sun, so it clamped and overcast came out far too dark).
	if (Ppv)
	{
		Ppv->Settings.bOverride_AutoExposureBias = true;
		Ppv->Settings.bOverride_AutoExposureMinBrightness = true;
		Ppv->Settings.AutoExposureBias = bDull ? ExposureBias + (bWet ? 1.9f : 1.6f) : ExposureBias;
		Ppv->Settings.AutoExposureMinBrightness = bDull ? ExposureMin - 3.0f : ExposureMin;
	}
	if (Fog) Fog->SetFogDensity(bDull ? FogDensity * (bWet ? 3.0f : 2.0f) : FogDensity);
	if (Atmo)
	{
		// A white-grey British overcast: almost no Rayleigh (the blue), thick Mie (the white haze).
		Atmo->SetMieScatteringScale(bDull ? Mie * 12.0f : Mie);
		Atmo->SetRayleighScatteringScale(bDull ? Rayleigh * 0.08f : Rayleigh);
	}
	if (UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/Buttertubs/Materials/MPC_Weather.MPC_Weather")))
	{
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Wetness"), bWet ? 1.0f : 0.0f);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: weather %d"), Weather);
}
