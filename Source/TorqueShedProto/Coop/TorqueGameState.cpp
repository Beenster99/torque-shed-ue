#include "TorqueGameState.h"

#include "TorquePlayerState.h"
#include "TorqueSettings.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePaintBay.h"
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
	DOREPLIFETIME(ATorqueGameState, Hour);
	DOREPLIFETIME(ATorqueGameState, Shed);
}

void ATorqueGameState::BeginPlay()
{
	Super::BeginPlay();
	// -TorqueWeather=sunny|overcast|wet sets the start (the host's, in co-op); otherwise the host's
	// Settings > Gameplay > Weather: held at one kind, or changing by itself from sunny.
	FString WeatherName;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings"));
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("TorqueWeather="), WeatherName))
		Weather = WeatherName.StartsWith(TEXT("wet")) || WeatherName.StartsWith(TEXT("rain")) ? 2 : WeatherName.StartsWith(TEXT("over")) ? 1 : 0;
	else if (HasAuthority() && !bTest && Set.WeatherMode > 0)
		Weather = Set.WeatherMode - 1;
	WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
	// The clock where it was left (the level's own light until the player sets one).
	if (HasAuthority() && !bTest) Hour = Set.TimeOfDay;
	ApplySun();   // (and the weather's light)

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
	// The host's clock carries on next time (once the player has settings of their own).
	FTorqueSettings& Set = FTorqueSettings::Get();
	if (HasAuthority() && Set.bSaved && Set.bDayNight && Hour >= 0.0f)
	{
		Set.TimeOfDay = Hour;
		Set.Save();
	}
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
	// For the two-copy tests: where each bike stands on this (joined) copy and the paint bench's
	// state, when either changes.
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
	// The paint mixing bench: who's at it, the tin on the scale (what's in it, how well stirred), the
	// scale's reading, the bottle being poured and the shaker.
	{
		const FTorquePaintBay& P = Build->PaintBay();
		FString Bench = FString::Printf(TEXT("paint bench: %s"), P.BenchBy != 0 ? *FString::Printf(TEXT("player %d at it"), P.BenchBy) : TEXT("no one at it"));
		if (const FTorqueTin* Tin = P.BenchTinPtr())
			Bench += FString::Printf(TEXT(", tin %d on the scale (base %d, %.0f g base + %d g of %d tints, stirred %d%%), scale %d g"), Tin->Id,
				Tin->Mix.Base, Tin->Mix.BaseGrams, FMath::RoundToInt(Tin->Mix.TintGrams() / 10.0f) * 10, Tin->Mix.Tints.Num(),
				FMath::RoundToInt(Tin->Mix.Stir * 10.0f) * 10, FMath::RoundToInt(P.ScaleGrams() / 10.0f) * 10);   // (rounded: a line per real change, not per gram)
		else
			Bench += TEXT(", scale empty");
		if (P.PourTint != INDEX_NONE) Bench += FString::Printf(TEXT(", pouring tint %d"), P.PourTint);
		Bench += P.ShakerTin != 0 ? FString::Printf(TEXT(", tin %d in the shaker"), P.ShakerTin) : FString(TEXT(", shaker empty"));
		Where += (Where.IsEmpty() ? TEXT("") : TEXT("; ")) + Bench;
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

ATorqueGameState::ATorqueGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATorqueGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	// The weather changing by itself (Torque Shed's "Changes by itself"): every few minutes, a step to
	// the next kind along (sunny, overcast, wet roads), never straight from sun to rain.
	if (Set.WeatherMode == 0 && (WeatherChangeIn -= DeltaSeconds) <= 0.0)
	{
		WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
		const int32 Next = Weather == 0 ? 1 : Weather == 2 ? 1 : (FMath::RandBool() ? 0 : 2);
		SetWeather(Next);
	}
	// The day going by: a whole day in Settings' Day length of real minutes.
	if (Set.bDayNight)
	{
		if (Hour < 0.0f) Hour = BuiltAt;
		Hour = FMath::Fmod(Hour + DeltaSeconds * 24.0f / (60.0f * (float)FMath::Max(Set.DayLength, 1)), 24.0f);
		ApplySun();
		// To the others a few times a second (they move their own sun between).
		if (const UWorld* World = GetWorld(); World && World->GetTimeSeconds() - HourSentAt > 0.25)
		{
			HourSentAt = World->GetTimeSeconds();
			ForceNetUpdate();
		}
	}
}

void ATorqueGameState::SetHour(float NewHour)
{
	if (!HasAuthority()) return;
	Hour = NewHour < 0.0f ? -1.0f : FMath::Fmod(NewHour, 24.0f);
	ApplySun();
	ForceNetUpdate();
}

void ATorqueGameState::OnRep_Hour()
{
	ApplySun();
}

FString ATorqueGameState::ClockText(float Hours)
{
	const int32 Mins = FMath::RoundToInt(FMath::Fmod(FMath::Max(Hours, 0.0f), 24.0f) * 60.0f) % (24 * 60);
	return FString::Printf(TEXT("%02d:%02d"), Mins / 60, Mins % 60);
}

namespace
{
// The sun over Swaledale (54.35 N) in late spring (declination 15 degrees), by solar time: its height
// and its bearing from north, clockwise, in degrees.
void SunAt(double Hours, double& Elevation, double& Bearing)
{
	const double Lat = FMath::DegreesToRadians(54.35), Dec = FMath::DegreesToRadians(15.0);
	const double H = FMath::DegreesToRadians(15.0 * (Hours - 12.0));
	const double SinE = FMath::Sin(Lat) * FMath::Sin(Dec) + FMath::Cos(Lat) * FMath::Cos(Dec) * FMath::Cos(H);
	Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinE, -1.0, 1.0)));
	Bearing = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Dec) * FMath::Sin(H),
		FMath::Sin(Dec) * FMath::Cos(Lat) - FMath::Cos(Dec) * FMath::Sin(Lat) * FMath::Cos(H)));
}
// The light's yaw for a bearing: the levels have x east and y south, and the light shines away from the sun.
double LightYaw(double Bearing)
{
	const double B = FMath::DegreesToRadians(Bearing);
	return FMath::RadiansToDegrees(FMath::Atan2(FMath::Cos(B), -FMath::Sin(B)));
}
} // namespace

void ATorqueGameState::ApplySun()
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (!SunActorPtr.IsValid())
		for (TActorIterator<ADirectionalLight> It(W); It && !SunActorPtr.IsValid(); ++It) SunActorPtr = *It;
	ADirectionalLight* SunActor = SunActorPtr.Get();
	if (!SunActor || !SunActor->GetLightComponent())
	{
		if (AppliedDayLight < 0.0f)
		{
			AppliedDayLight = DayLight;
			ApplyWeather();
		}
		return;
	}
	if (!bSunCaptured)
	{
		// The level's own sun: which afternoon hour has the sun that high, and how far its bearing is turned.
		bSunCaptured = true;
		BuiltSun = SunActor->GetActorRotation();
		const double Height = FMath::Clamp(-(double)BuiltSun.Pitch, 1.0, 50.0);
		BuiltAt = 12.0f;
		for (double Hr = 12.0; Hr <= 21.0; Hr += 1.0 / 60.0)
		{
			double E = 0.0, B = 0.0;
			SunAt(Hr, E, B);
			BuiltAt = (float)Hr;
			if (E <= Height) break;
		}
		double E0 = 0.0, B0 = 0.0;
		SunAt(BuiltAt, E0, B0);
		SunYawOffset = FRotator::NormalizeAxis(BuiltSun.Yaw - LightYaw(B0));
	}
	if (Hour < 0.0f)
	{
		SunActor->SetActorRotation(BuiltSun);
		DayLight = 1.0f;
	}
	else
	{
		double E = 0.0, B = 0.0;
		SunAt(Hour, E, B);
		// Below the horizon the light goes out (and stays pointing down, so nothing is lit from under).
		SunActor->SetActorRotation(FRotator(-FMath::Max(E, 2.0), LightYaw(B) + SunYawOffset, BuiltSun.Roll));
		DayLight = FMath::SmoothStep(-4.0f, 6.0f, (float)E);
	}
	// The sun's brightness and the exposure with it, when the light has changed enough to see.
	if (AppliedDayLight < 0.0f || FMath::Abs(DayLight - AppliedDayLight) > 0.01f || (DayLight != AppliedDayLight && (DayLight == 0.0f || DayLight == 1.0f)))
	{
		AppliedDayLight = DayLight;
		ApplyWeather();
	}
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
		Sun->SetIntensity((bDull ? SunLux * (bWet ? 0.10f : 0.14f) : SunLux) * DayLight);
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
		// (and at night, by as much again: dark, but you can see the road in your lights).
		Ppv->Settings.AutoExposureMinBrightness = (bDull ? ExposureMin - 3.0f : ExposureMin) - 5.0f * (1.0f - DayLight);
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
