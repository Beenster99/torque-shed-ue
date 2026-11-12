#include "TorqueGameState.h"

#include "TorquePlayerState.h"
#include "Engine/GameInstance.h"
#include "World/TorqueRideWorld.h"
#include "World/TorqueWorldSubsystem.h"
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
#include "Fit/FitJson.h"
#include "Weather/TorqueWeatherFx.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Net/UnrealNetwork.h"

bool ATorqueGameState::bRealClockTest = false;
bool ATorqueGameState::bTestClockSet = false;

void ATorqueGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATorqueGameState, Weather);
	DOREPLIFETIME(ATorqueGameState, Net);
	DOREPLIFETIME(ATorqueGameState, Hour);
	DOREPLIFETIME(ATorqueGameState, SunShift);
	DOREPLIFETIME(ATorqueGameState, SunDec);
	DOREPLIFETIME(ATorqueGameState, SunLat);
	DOREPLIFETIME(ATorqueGameState, GridNet);
	DOREPLIFETIME(ATorqueGameState, Shed);
	DOREPLIFETIME(ATorqueGameState, RideJourney);
}

void ATorqueGameState::BeginPlay()
{
	Super::BeginPlay();
	// [Ride UK] A joined rider's own ground and road: the host's game mode lays the host's, and
	// every other machine lays its own from the road it planned from the host's journey.
	if (GetNetMode() == NM_Client && GetWorld() && GetWorld()->GetMapName().Contains(TEXT("RideUK")))
	{
		UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
		if (Real && Real->Ride())
		{
			if (ATorqueRideWorld* Ground = GetWorld()->SpawnActor<ATorqueRideWorld>()) Ground->Begin(Real->Ride());
		}
		else
			UE_LOG(LogTemp, Error, TEXT("Torque ride UK: joined the host's ride with no road planned here"));
	}
	// -TorqueWeather=sunny|overcast|wet|live|auto sets the start (the host's, in co-op); otherwise the
	// host's Settings > Gameplay > Weather: live, held at one kind, or changing by itself from sunny.
	// Test runs stay sunny unless they ask.
	FString WeatherName;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings"));
	Mode = bTest ? 1 : Set.WeatherMode;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueWeather="), WeatherName))
		Mode = WeatherName.StartsWith(TEXT("live")) ? FTorqueSettings::WeatherLive : WeatherName.StartsWith(TEXT("auto")) ? 0
			: WeatherName.StartsWith(TEXT("wet")) || WeatherName.StartsWith(TEXT("rain")) ? 3 : WeatherName.StartsWith(TEXT("over")) ? 2 : 1;
	// -TorqueWeatherAt=lat,lon asks for the weather somewhere else on Earth; otherwise the route's own
	// place (Data/Buttertubs/route.json's origin_geo), until the weather follows the rider (stage 5).
	// The route also gives the level's height above sea level at its origin (origin_region_m[1]).
	{
		FString Route;
		TorqueFit::Json Root;
		if (FFileHelper::LoadFileToString(Route, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Buttertubs/route.json")))
			&& TorqueFit::Json::Parse(TCHAR_TO_UTF8(*Route), Root))
		{
			if (const TorqueFit::Json* Geo = Root.Get("origin_geo"); Geo && Geo->IsArray() && Geo->A.size() == 2)
			{
				LatitudeAt = Geo->A[0].N;
				LongitudeAt = Geo->A[1].N;
			}
			if (const TorqueFit::Json* Origin = Root.Get("origin_region_m"); Origin && Origin->IsArray() && Origin->A.size() == 3)
				OriginAsl = Origin->A[1].N;
		}
	}
	FString At;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueWeatherAt="), At))
	{
		FString LatText, LonText;
		if (At.Split(TEXT(","), &LatText, &LonText))
		{
			LatitudeAt = FMath::Clamp(FCString::Atod(*LatText), -90.0, 90.0);
			LongitudeAt = FMath::Clamp(FCString::Atod(*LonText), -180.0, 180.0);
		}
	}
	// Rain, splashes and tyre spray, drawn on each machine around its own camera.
	if (GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Fx = GetWorld()->SpawnActor<ATorqueWeatherFx>(Params);
	}
	if (HasAuthority())
	{
		Weather = Mode >= 1 && Mode <= 3 ? Mode - 1 : 0;
		SetFromState(TorqueWeather::Preset(Weather));
		GoToMode(0.0f);
	}
	WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
	Here = HereApplied = Fade.Now;
	FetchLat = LatitudeAt;
	FetchLon = LongitudeAt;
	if (GConfig)
	{
		int32 Saved = 0;
		if (GConfig->GetInt(TEXT("TorqueWeather"), TEXT("UtcOffset"), Saved, GGameUserSettingsIni)) { UtcOffset = Saved; bUtcKnown = true; }
	}
	// The clock where it was left (the level's own light until the player sets one).
	// (A test run keeps the level's light unless one of its steps set the clock.)
	if (HasAuthority() && (!bTest || bTestClockSet)) Hour = Set.TimeOfDay;
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
	if (LiveRequest.IsValid())
	{
		LiveRequest->OnProcessRequestComplete().Unbind();
		LiveRequest->CancelRequest();
		LiveRequest.Reset();
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

void ATorqueGameState::SetWeather(int32 NewWeather, float FadeSeconds)
{
	if (!HasAuthority()) return;
	const int32 Kind = ((NewWeather % 3) + 3) % 3;
	Fade.FadeTo(TorqueWeather::Preset(Kind), FadeSeconds);
	if (FadeSeconds <= 0.0f) Weather = Kind;   // (otherwise it changes as the fade passes halfway)
	// (Live never reached keeps saying so while it changes by itself; the V key on a live day holds
	// its kind until the next live answer.)
	if (!(Mode == FTorqueSettings::WeatherLive && Net.Source == (uint8)ETorqueWeatherSource::LiveOffline))
		Net.Source = (uint8)(Mode == 0 ? ETorqueWeatherSource::ByItself : ETorqueWeatherSource::Fixed);
	AppliedAt = -1.0;
	SendNet();           // the others get it through OnRep_Net
	TickWeather(0.0f);   // and here
}

ATorqueGameState::ATorqueGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATorqueGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickWeather(DeltaSeconds);
	if (!HasAuthority()) return;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	// The weather changing by itself (Torque Shed's "Changes by itself"): every few minutes, a step to
	// the next kind along (sunny, overcast, wet roads), never straight from sun to rain, fading over a
	// few minutes. Live does the same while it has never reached the weather service.
	const bool bByItself = Mode == 0 || (Mode == FTorqueSettings::WeatherLive && Net.Source == (uint8)ETorqueWeatherSource::LiveOffline);
	if (bByItself && (WeatherChangeIn -= DeltaSeconds) <= 0.0)
	{
		WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
		const int32 Next = Weather == 0 ? 1 : Weather == 2 ? 1 : (FMath::RandBool() ? 0 : 2);
		SetWeather(Next, 180.0f);
	}
	// Live: asking again every 15 minutes (sooner after a failure), and when the host has ridden out
	// towards the grid's edge (no more than every two minutes).
	if (Mode == FTorqueSettings::WeatherLive && !bFetching && GridNow.Valid())
	{
		double VLat = 0.0, VLon = 0.0, VAsl = 0.0, East = 0.0, North = 0.0;
		ViewGeo(VLat, VLon, VAsl);
		TorqueWeather::MetresBetween(GridNow.Lat, GridNow.Lon, VLat, VLon, East, North);
		if (FMath::Max(FMath::Abs(East), FMath::Abs(North)) > 0.75 * GridNow.StepM && GetWorld()->GetTimeSeconds() - LastFetchAt > 120.0)
			NextFetchIn = 0.0;
	}
	if (Mode == FTorqueSettings::WeatherLive && !bFetching && (NextFetchIn -= DeltaSeconds) <= 0.0) FetchLive(0);
	// The real time of day, or the day going by: a whole day in Settings' Day length of real minutes.
	const bool bTestRun = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings"));
	const bool bRealClock = Set.bRealClock && (!bTestRun || bRealClockTest);
	if (bRealClock) TickRealClock();
	else if (SunShift != 0.0f || SunDec != 15.0f || SunLat != 54.35f)
	{
		SunShift = 0.0f;
		SunDec = 15.0f;
		SunLat = 54.35f;
		ApplySun();
		ForceNetUpdate();
	}
	if (Set.bDayNight && !bRealClock)
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
void SunAt(double Hours, double& Elevation, double& Bearing, double LatDeg = 54.35, double DecDeg = 15.0)
{
	const double Lat = FMath::DegreesToRadians(LatDeg), Dec = FMath::DegreesToRadians(DecDeg);
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
		SunAt(FMath::Fmod(Hour + SunShift + 48.0, 24.0), E, B, SunLat, SunDec);
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
	// (The light follows Net; the bikes read the kind.)
}

// --- The weather engine (stage 1): one State, fading; the host's sent to everyone -------------------

void ATorqueGameState::SetFromState(const TorqueWeather::State& S)
{
	Fade.Set(S);
	Weather = TorqueWeather::NearestKind(S);
	AppliedAt = -1.0;
}

void ATorqueGameState::WeatherModeChanged(float FadeSeconds)
{
	if (!HasAuthority()) return;
	Mode = FTorqueSettings::Get().WeatherMode;
	GoToMode(FadeSeconds);
}

void ATorqueGameState::GoToMode(float FadeSeconds)
{
	if (Mode != FTorqueSettings::WeatherLive && (GridNow.Valid() || GridBefore.Valid()))
	{
		// (Not live: one weather for everyone again, the local differences fading out with the change.)
		SetGrid(TorqueWeather::Grid(), 0, FadeSeconds);
		SendGrid();
	}
	if (Mode >= 1 && Mode <= 3)
	{
		SetWeather(Mode - 1, FadeSeconds);
		return;
	}
	if (Mode == 0)
	{
		Net.Source = (uint8)ETorqueWeatherSource::ByItself;
		WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
		SendNet();
		return;
	}
	// Live: the last answer at once when it's recent (a start with no internet carries on with it), and ask now.
	FString Saved;
	FString SavedAt;
	TorqueWeather::State Last;
	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	const bool bHave = GConfig && GConfig->GetString(TEXT("TorqueWeather"), TEXT("Last"), Saved, GGameUserSettingsIni)
		&& GConfig->GetString(TEXT("TorqueWeather"), TEXT("LastAt"), SavedAt, GGameUserSettingsIni)
		&& TorqueWeather::Load(TCHAR_TO_UTF8(*Saved), Last);
	const int64 Then = bHave ? FCString::Atoi64(*SavedAt) : 0;
	if (bHave && Now - Then < 3 * 3600)
	{
		Fade.FadeTo(Last, FadeSeconds > 0.0f ? 30.0 : 0.0);
		if (FadeSeconds <= 0.0f) Weather = TorqueWeather::NearestKind(Last);
		Net.Source = (uint8)ETorqueWeatherSource::LiveLastKnown;
		Net.FetchedAt = Then;
	}
	else
		Net.Source = (uint8)ETorqueWeatherSource::LiveWaiting;
	bLiveApplied = false;
	LiveFailures = 0;
	NextFetchIn = 0.0;
	if (!bFetching) FetchLive(0);
	AppliedAt = -1.0;
	SendNet();
}

void ATorqueGameState::SendNet()
{
	if (!HasAuthority()) return;
	const TorqueWeather::State& S = Fade.Now;
	Net.Cloud = (float)S.Cloud;
	Net.Rain = (float)S.Rain;
	Net.Snow = (float)S.Snow;
	Net.Wind = (float)S.Wind;
	Net.WindFrom = (float)S.WindFrom;
	Net.Gust = (float)S.Gust;
	Net.Air = (float)S.Air;
	Net.Visibility = (float)S.Visibility;
	Net.RoadWet = (float)S.RoadWet;
	Net.Code = S.Code;
	Net.LowCloud = (float)S.LowCloud;
	Net.CloudBase = (float)S.CloudBase;
	Net.Puddles = (float)S.Puddles;
	Net.SnowDepth = (float)S.SnowDepth;
	if (const UWorld* World = GetWorld()) NetSentAt = World->GetRealTimeSeconds();
	ForceNetUpdate();
}

void ATorqueGameState::OnRep_Net()
{
	if (HasAuthority()) return;
	TorqueWeather::State S;
	S.Cloud = Net.Cloud;
	S.Rain = Net.Rain;
	S.Snow = Net.Snow;
	S.Wind = Net.Wind;
	S.WindFrom = Net.WindFrom;
	S.Gust = Net.Gust;
	S.Air = Net.Air;
	S.Visibility = Net.Visibility;
	S.RoadWet = Net.RoadWet;
	S.Code = Net.Code;
	S.LowCloud = Net.LowCloud;
	S.CloudBase = Net.CloudBase;
	S.Puddles = Net.Puddles;
	S.SnowDepth = Net.SnowDepth;
	// The host sends a fade a couple of times a second: ease between, so it stays smooth here too.
	if (AppliedAt < 0.0) Fade.Set(S);
	else Fade.FadeTo(S, 0.6);
}

void ATorqueGameState::TickWeather(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World) return;
	const bool bMoved = Fade.Tick(DeltaSeconds);
	const double Now = World->GetRealTimeSeconds();
	// The weather here: the shared weather made local from the grid (every frame: it's cheap), shown
	// when it has changed enough to see.
	UpdateHere();
	const bool bHereMoved = HereAt >= 0.0 && Now - AppliedAt >= 0.1 && (FMath::Abs(Here.Cloud - HereApplied.Cloud) > 0.01
		|| FMath::Abs(Here.Rain - HereApplied.Rain) > 0.03 || FMath::Abs(Here.RoadWet - HereApplied.RoadWet) > 0.01
		|| FMath::Abs(Here.Puddles - HereApplied.Puddles) > 0.01 || FMath::Abs(Here.SnowDepth - HereApplied.SnowDepth) > 0.002 || FMath::Abs(Here.Air - HereApplied.Air) > 0.2
		|| FMath::Abs(Here.Wind - HereApplied.Wind) > 0.2 || FMath::Abs(Here.LowCloud - HereApplied.LowCloud) > 0.01
		|| FMath::Abs(Here.Visibility - HereApplied.Visibility) > 0.03 * HereApplied.Visibility);
	if (HasAuthority())
	{
		const int32 Kind = TorqueWeather::NearestKind(Fade.Now);
		if (Kind != Weather) Weather = Kind;
		// To everyone: twice a second while it fades, and as it ends.
		if (bMoved && (!Fade.Fading() || NetSentAt < 0.0 || Now - NetSentAt >= 0.5)) SendNet();
	}
	// Riding up into low cloud (or out of it): the haze follows the camera's height, checked four
	// times a second.
	bool bHazeMoved = false;
	if (Now - HazeCheckAt >= 0.25)
	{
		HazeCheckAt = Now;
		bHazeMoved = FMath::Abs(HazeNow() - AppliedHaze) > 0.05f * FMath::Max(AppliedHaze, 1.0f);
	}
	// On the screen: ten times a second while it fades (the light moves slowly), and as it ends.
	// At night the exposure follows the camera in and out of the shed.
	const bool bOpenMoved = DayLight < 0.99f && FMath::Abs(WeatherOpen() - AppliedOpen) > 0.05f && Now - AppliedAt >= 0.1;
	if (AppliedAt < 0.0 || bHazeMoved || bHereMoved || bOpenMoved || (bMoved && (!Fade.Fading() || Now - AppliedAt >= 0.1)))
	{
		AppliedAt = Now;
		HereApplied = Here;
		ApplyWeather();
	}
}

FString ATorqueGameState::WeatherText(bool bImperial) const
{
	static const TCHAR* Kinds[] = {TEXT("sunny"), TEXT("overcast"), TEXT("wet roads")};
	const FString Words = UTF8_TO_TCHAR(TorqueWeather::Describe(Here, bImperial).c_str());
	const int64 Mins = Net.FetchedAt > 0 ? FMath::Max<int64>((FDateTime::UtcNow().ToUnixTimestamp() - Net.FetchedAt) / 60, 0) : -1;
	const FString Ago = Mins < 0 ? FString() : Mins < 1 ? FString(TEXT("just now")) : Mins < 120 ? FString::Printf(TEXT("%lld min ago"), Mins)
		: FString::Printf(TEXT("%lld hours ago"), Mins / 60);
	switch ((ETorqueWeatherSource)Net.Source)
	{
	case ETorqueWeatherSource::Live: return FString::Printf(TEXT("Live: %s (updated %s)"), *Words, *Ago);
	case ETorqueWeatherSource::LiveLastKnown: return FString::Printf(TEXT("Live, last known: %s (no connection; from %s)"), *Words, *Ago);
	case ETorqueWeatherSource::LiveWaiting: return TEXT("Live: asking the weather service...");
	case ETorqueWeatherSource::LiveOffline: return FString::Printf(TEXT("Live: no connection, changing by itself meanwhile (%s now)"), Kinds[FMath::Clamp(Weather, 0, 2)]);
	default: return Kinds[FMath::Clamp(Weather, 0, 2)];
	}
}

// --- Live (stage 2) -----------------------------------------------------------------------------------

void ATorqueGameState::FetchLive(int32 Feed)
{
	if (!HasAuthority()) return;
	// Open-Meteo: the grid round where the host is now (snapped, so riding about doesn't move it);
	// MET Norway, the backup, one place.
	double VLat = LatitudeAt, VLon = LongitudeAt, VAsl = 0.0;
	ViewGeo(VLat, VLon, VAsl);
	TorqueWeather::SnapCentre(VLat, VLon, GRID_STEP_M, FetchLat, FetchLon);
	if (const UWorld* World = GetWorld()) LastFetchAt = World->GetTimeSeconds();
	const std::string Url = Feed == 0 ? TorqueWeather::OpenMeteoGridUrl(FetchLat, FetchLon, GRID_N, GRID_STEP_M) : TorqueWeather::MetNoUrl(VLat, VLon);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(UTF8_TO_TCHAR(Url.c_str()));
	Req->SetVerb(TEXT("GET"));
	// (MET Norway asks every app to name itself.)
	Req->SetHeader(TEXT("User-Agent"), TEXT("TorqueShedUE/0.1 (free hobby motorcycle game)"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->SetTimeout(15.0f);
	TWeakObjectPtr<ATorqueGameState> Self(this);
	Req->OnProcessRequestComplete().BindLambda([Self, Feed](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
	{
		ATorqueGameState* GS = Self.Get();
		if (!GS) return;
		const bool bOk = bConnected && Resp.IsValid() && EHttpResponseCodes::IsOk(Resp->GetResponseCode());
		const FString Body = Resp.IsValid() ? Resp->GetContentAsString() : FString();
		if (!bOk && Resp.IsValid() && !Body.IsEmpty() && Feed == 0)
		{
			// (Open-Meteo says why in JSON.)
			TorqueWeather::State Ignored;
			std::string Why;
			TorqueWeather::ParseOpenMeteo(TCHAR_TO_UTF8(*Body), Ignored, nullptr, &Why);
			GS->LiveFailed(Feed, FString::Printf(TEXT("HTTP %d %s"), Resp->GetResponseCode(), UTF8_TO_TCHAR(Why.c_str())));
			return;
		}
		if (!bOk)
		{
			GS->LiveFailed(Feed, Resp.IsValid() ? FString::Printf(TEXT("HTTP %d"), Resp->GetResponseCode()) : FString(TEXT("no connection")));
			return;
		}
		GS->LiveAnswer(Feed, true, Body);
	});
	bFetching = true;
	LiveRequest = Req;
	UE_LOG(LogTemp, Log, TEXT("Torque weather: asking %s for %.4f, %.4f%s"), Feed == 0 ? TEXT("Open-Meteo") : TEXT("MET Norway"),
		Feed == 0 ? FetchLat : VLat, Feed == 0 ? FetchLon : VLon, Feed == 0 ? *FString::Printf(TEXT(" (%dx%d points %.1f km apart)"), GRID_N, GRID_N, GRID_STEP_M / 1000.0) : TEXT(""));
	Req->ProcessRequest();
}

void ATorqueGameState::LiveAnswer(int32 Feed, bool, const FString& Body)
{
	bFetching = false;
	LiveRequest.Reset();
	TorqueWeather::State S;
	TorqueWeather::Grid G;
	long long When = 0;
	int32 UtcGiven = 0;
	std::string Why;
	const std::string Text = TCHAR_TO_UTF8(*Body);
	const bool bParsed = Feed == 0 ? TorqueWeather::ParseOpenMeteoGrid(Text, FetchLat, FetchLon, GRID_N, GRID_STEP_M, G, &When, &UtcGiven, &Why)
		: TorqueWeather::ParseMetNo(Text, S, &When, &Why);
	if (bParsed && Feed == 0) S = G.Centre();
	if (!bParsed)
	{
		LiveFailed(Feed, UTF8_TO_TCHAR(Why.c_str()));
		return;
	}
	if (Mode != FTorqueSettings::WeatherLive) return;   // (switched off while it was asking)
	LiveFailures = 0;
	NextFetchIn = 15.0 * 60.0;
	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	// The first answer of a ride shows at once if the ride has only just begun (you're still in the
	// shed); otherwise new data eases in over four minutes, so nothing pops.
	const UWorld* World = GetWorld();
	const bool bStart = !bLiveApplied && Net.Source != (uint8)ETorqueWeatherSource::LiveLastKnown && World && World->GetTimeSeconds() < 20.0;
	const double FadeFor = bStart ? 0.0 : !bLiveApplied ? 30.0 : 240.0;
	Fade.FadeTo(S, FadeFor);
	// The weather here from the grid (none from the backup, which gives one place).
	SetGrid(G, Now, FadeFor);
	SendGrid();
	if (Feed == 0)
	{
		UtcOffset = UtcGiven;
		bUtcKnown = true;
		if (GConfig) GConfig->SetInt(TEXT("TorqueWeather"), TEXT("UtcOffset"), UtcGiven, GGameUserSettingsIni);
	}
	if (bStart) Weather = TorqueWeather::NearestKind(S);
	bLiveApplied = true;
	Net.Source = (uint8)ETorqueWeatherSource::Live;
	Net.FetchedAt = Now;
	AppliedAt = -1.0;
	SendNet();
	if (GConfig)
	{
		GConfig->SetString(TEXT("TorqueWeather"), TEXT("Last"), UTF8_TO_TCHAR(TorqueWeather::Save(S).c_str()), GGameUserSettingsIni);
		GConfig->SetString(TEXT("TorqueWeather"), TEXT("LastAt"), *FString::Printf(TEXT("%lld"), Now), GGameUserSettingsIni);
		GConfig->SetString(TEXT("TorqueWeather"), TEXT("LastFrom"), Feed == 0 ? TEXT("Open-Meteo") : TEXT("MET Norway"), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque weather: live from %s: %s (road wet %.2f, rain %.1f mm/h, cloud %.0f%%, visibility %.0f m, data time %lld)"),
		Feed == 0 ? TEXT("Open-Meteo") : TEXT("MET Norway"), UTF8_TO_TCHAR(TorqueWeather::Describe(S, false).c_str()), S.RoadWet, S.Rain, S.Cloud * 100.0,
		S.Visibility, (long long)When);
}

void ATorqueGameState::LiveFailed(int32 Feed, const FString& Why)
{
	bFetching = false;
	LiveRequest.Reset();
	UE_LOG(LogTemp, Warning, TEXT("Torque weather: %s failed: %s"), Feed == 0 ? TEXT("Open-Meteo") : TEXT("MET Norway"), *Why);
	if (Mode != FTorqueSettings::WeatherLive) return;
	// The backup service next, then a wait that grows: 2, 5, then 10 minutes.
	if (Feed == 0)
	{
		FetchLive(1);
		return;
	}
	++LiveFailures;
	NextFetchIn = LiveFailures <= 1 ? 120.0 : LiveFailures == 2 ? 300.0 : 600.0;
	// No internet: carry on with the last weather; never reached (or hours old): change by itself meanwhile.
	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	if (Net.FetchedAt > 0 && Now - Net.FetchedAt < 3 * 3600)
		Net.Source = (uint8)ETorqueWeatherSource::LiveLastKnown;
	else if (Net.Source != (uint8)ETorqueWeatherSource::LiveOffline)
	{
		Net.Source = (uint8)ETorqueWeatherSource::LiveOffline;
		WeatherChangeIn = FMath::FRandRange(300.0, 720.0);
	}
	SendNet();
}

// --- Weather across distance and the real time of day (stage 5) -----------------------------------

namespace
{
FTorqueWeatherNet NetOf(const TorqueWeather::State& S)
{
	FTorqueWeatherNet N;
	N.Cloud = (float)S.Cloud; N.Rain = (float)S.Rain; N.Snow = (float)S.Snow; N.Wind = (float)S.Wind;
	N.WindFrom = (float)S.WindFrom; N.Gust = (float)S.Gust; N.Air = (float)S.Air; N.Visibility = (float)S.Visibility;
	N.RoadWet = (float)S.RoadWet; N.Code = S.Code; N.LowCloud = (float)S.LowCloud; N.CloudBase = (float)S.CloudBase;
	N.Puddles = (float)S.Puddles;
	N.SnowDepth = (float)S.SnowDepth;
	return N;
}

TorqueWeather::State StateOf(const FTorqueWeatherNet& N)
{
	TorqueWeather::State S;
	S.Cloud = N.Cloud; S.Rain = N.Rain; S.Snow = N.Snow; S.Wind = N.Wind; S.WindFrom = N.WindFrom; S.Gust = N.Gust;
	S.Air = N.Air; S.Visibility = N.Visibility; S.RoadWet = N.RoadWet; S.Code = N.Code; S.LowCloud = N.LowCloud;
	S.CloudBase = N.CloudBase; S.Puddles = N.Puddles; S.SnowDepth = N.SnowDepth;
	return S;
}
} // namespace

void ATorqueGameState::ViewGeo(double& Lat, double& Lon, double& Asl) const
{
	// This machine's camera (the levels: cm, x east, y south, z up, the origin at the route's
	// origin_geo), as a place on Earth.
	FVector At = FVector::ZeroVector;
	if (const UWorld* W = GetWorld())
		if (const APlayerController* PC = W->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
			At = PC->PlayerCameraManager->GetCameraLocation();
	TorqueWeather::Offset(LatitudeAt, LongitudeAt, At.X / 100.0, -At.Y / 100.0, Lat, Lon);
	Asl = OriginAsl + At.Z / 100.0;
}

void ATorqueGameState::GeoAt(const FVector& At, double& Lat, double& Lon, double& Asl) const
{
	TorqueWeather::Offset(LatitudeAt, LongitudeAt, At.X / 100.0, -At.Y / 100.0, Lat, Lon);
	Asl = OriginAsl + At.Z / 100.0;
}

void ATorqueGameState::SetGrid(const TorqueWeather::Grid& G, int64 FetchedAt, double FadeSeconds)
{
	// The grid before fades out as this one fades in (both over the shared weather's own fade).
	GridBefore = GridNow;
	GridFetchedBefore = GridFetchedNow;
	GridNow = G;
	GridFetchedNow = FetchedAt;
	GridFadeFrom = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	GridFadeSeconds = FadeSeconds;
	if (FadeSeconds <= 0.0) GridBefore = TorqueWeather::Grid();
	UpdateHere();
}

void ATorqueGameState::SendGrid()
{
	if (!HasAuthority()) return;
	GridNet.N = GridNow.Valid() ? GridNow.N : 0;
	GridNet.Lat = GridNow.Lat;
	GridNet.Lon = GridNow.Lon;
	GridNet.StepM = (float)GridNow.StepM;
	GridNet.Points.Reset();
	GridNet.Elevation.Reset();
	if (GridNow.Valid())
		for (int32 I = 0; I < GridNow.N * GridNow.N; ++I)
		{
			GridNet.Points.Add(NetOf(GridNow.Points[I]));
			GridNet.Elevation.Add((float)GridNow.Elevation[I]);
		}
	GridNet.FetchedAt = GridFetchedNow;
	ForceNetUpdate();
}

void ATorqueGameState::OnRep_Grid()
{
	if (HasAuthority()) return;
	TorqueWeather::Grid G;
	if (GridNet.N >= 2 && GridNet.Points.Num() == GridNet.N * GridNet.N && GridNet.Elevation.Num() == GridNet.Points.Num())
	{
		G.N = GridNet.N;
		G.Lat = GridNet.Lat;
		G.Lon = GridNet.Lon;
		G.StepM = GridNet.StepM;
		for (int32 I = 0; I < GridNet.Points.Num(); ++I)
		{
			G.Points.push_back(StateOf(GridNet.Points[I]));
			G.Elevation.push_back(GridNet.Elevation[I]);
		}
	}
	// (Joining: at once. Later: eased in, as the host's shared weather is.)
	SetGrid(G, GridNet.FetchedAt, HereAt < 0.0 ? 0.0 : 240.0);
}

void ATorqueGameState::UpdateHere()
{
	const UWorld* World = GetWorld();
	if (!World) return;
	if (!GridNow.Valid() && !GridBefore.Valid())
	{
		Here = Fade.Now;
		HereAt = World->GetRealTimeSeconds();
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	const double Unix = (double)FDateTime::UtcNow().ToUnixTimestamp();
	const double W = GridFadeSeconds <= 0.0 ? 1.0 : FMath::SmoothStep(0.0, 1.0, FMath::Clamp((Now - GridFadeFrom) / GridFadeSeconds, 0.0, 1.0));
	if (W >= 1.0) GridBefore = TorqueWeather::Grid();
	double Lat = 0.0, Lon = 0.0, Asl = 0.0;
	ViewGeo(Lat, Lon, Asl);
	TorqueWeather::State S = Fade.Now;
	if (GridBefore.Valid())
		S = TorqueWeather::AddDifference(S, TorqueWeather::GridAt(GridBefore, Lat, Lon, Asl, Unix - (double)GridFetchedBefore), GridBefore.Centre(), 1.0 - W);
	if (GridNow.Valid())
		S = TorqueWeather::AddDifference(S, TorqueWeather::GridAt(GridNow, Lat, Lon, Asl, Unix - (double)GridFetchedNow), GridNow.Centre(), W);
	Here = S;
	HereAt = Now;
}

void ATorqueGameState::TickRealClock()
{
	// The host's clock follows the real local time where the weather is from (Open-Meteo's offset
	// for the place; this PC's own until it has one), and the sun stands as it really does there today.
	const FDateTime Utc = FDateTime::UtcNow();
	const int32 Off = bUtcKnown ? UtcOffset : FMath::RoundToInt((FDateTime::Now() - Utc).GetTotalSeconds() / 900.0) * 900;
	const double UtcHours = Utc.GetHour() + Utc.GetMinute() / 60.0 + Utc.GetSecond() / 3600.0;
	const int32 Day = Utc.GetDayOfYear();
	const float Clock = (float)FMath::Fmod(UtcHours + Off / 3600.0 + 48.0, 24.0);
	SunDec = (float)TorqueWeather::SunDeclination(Day);
	SunLat = (float)LatitudeAt;
	SunShift = (float)(LongitudeAt / 15.0 + TorqueWeather::EquationOfTime(Day) / 60.0 - Off / 3600.0);
	if (FMath::Abs(Clock - Hour) < 1.0f / 3600.0f) return;
	Hour = Clock;
	ApplySun();
	// To the others every few seconds (the sun hardly moves between).
	if (const UWorld* World = GetWorld(); World && World->GetTimeSeconds() - ClockSentAt > 5.0)
	{
		ClockSentAt = World->GetTimeSeconds();
		ForceNetUpdate();
	}
}

FString ATorqueGameState::WeatherHereText() const
{
	double Lat = 0.0, Lon = 0.0, Asl = 0.0;
	ViewGeo(Lat, Lon, Asl);
	const TorqueWeather::State& H = Here;
	const TorqueWeather::State& C = Fade.Now;
	FString GridText = TEXT("no grid");
	if (GridNow.Valid())
	{
		double East = 0.0, North = 0.0;
		TorqueWeather::MetresBetween(GridNow.Lat, GridNow.Lon, Lat, Lon, East, North);
		GridText = FString::Printf(TEXT("grid %dx%d round %.4f, %.4f (here %.0f m east, %.0f m north of its centre; drifted %lld s)"), GridNow.N, GridNow.N,
			GridNow.Lat, GridNow.Lon, East, North, (long long)(FDateTime::UtcNow().ToUnixTimestamp() - GridFetchedNow));
	}
	return FString::Printf(TEXT("here %.4f, %.4f, %.0f m up: air %.1f C (shared %.1f), rain %.2f mm/h (shared %.2f), cloud %.0f%%, wind %.1f m/s from %.0f, gusts %.1f, road wet %.2f, puddles %.2f, visibility %.0f m, grip %.2f; %s; clock %s (sun shift %+.2f h, declination %.1f)"),
		Lat, Lon, Asl, H.Air, C.Air, H.Rain, C.Rain, H.Cloud * 100.0, H.Wind, H.WindFrom, H.Gust, H.RoadWet, H.Puddles, H.Visibility,
		TorqueWeather::WetGrip(H), *GridText, Hour < 0.0f ? TEXT("level's own") : *ClockText(Hour), SunShift, SunDec);
}

float ATorqueGameState::WeatherOpen() const
{
	return Fx ? Fx->OpenToSky() : 1.0f;
}

float ATorqueGameState::HazeNow() const
{
	// The viewer's height: this machine's camera, metres above sea level.
	double Asl = OriginAsl;
	if (const UWorld* W = GetWorld())
		if (const APlayerController* PC = W->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
			Asl = OriginAsl + PC->PlayerCameraManager->GetCameraLocation().Z / 100.0;
	return (float)TorqueWeather::HazeAt(Here, Asl);
}

// Weather: the level is built sunny. As the sky dulls (the State's cloud) the sun dims and softens,
// the sky greys and the haze thickens; a wet day darkens a little more, and the roads take their
// wetness (the materials' Wetness, MPC_Weather). Low visibility thickens the haze further. The
// three presets give exactly the light the three kinds always had. The bikes take the grip from
// GetWeather (the nearest kind) until stage 4.
void ATorqueGameState::ApplyWeather()
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (!bLooked || !SunPtr.IsValid())
	{
		bLooked = true;
		for (TActorIterator<ADirectionalLight> It(W); It && !SunPtr.IsValid(); ++It) SunPtr = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		for (TActorIterator<ASkyLight> It(W); It && !SkyPtr.IsValid(); ++It) SkyPtr = It->GetLightComponent();
		for (TActorIterator<AExponentialHeightFog> It(W); It && !FogPtr.IsValid(); ++It) FogPtr = It->GetComponent();
		for (TActorIterator<AActor> It(W); It && !AtmoPtr.IsValid(); ++It) AtmoPtr = It->FindComponentByClass<USkyAtmosphereComponent>();
		for (TActorIterator<APostProcessVolume> It(W); It && !PpvPtr.IsValid(); ++It) if (It->bUnbound) PpvPtr = *It;
		if (!WeatherMpc) WeatherMpc = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Buttertubs/Materials/MPC_Weather.MPC_Weather"));
	}
	UDirectionalLightComponent* Sun = SunPtr.Get();
	USkyLightComponent* Sky = SkyPtr.Get();
	UExponentialHeightFogComponent* Fog = FogPtr.Get();
	USkyAtmosphereComponent* Atmo = AtmoPtr.Get();
	APostProcessVolume* Ppv = PpvPtr.Get();
	if (!bCaptured)
	{
		bCaptured = true;
		if (Sun) { SunLux = Sun->Intensity; SunAngle = Sun->LightSourceAngle; }
		if (Sky) SkyIntensity = Sky->Intensity;
		if (Fog) { FogDensity = Fog->FogDensity; FogInscatter = Fog->FogInscatteringLuminance; FogSunInscatter = Fog->DirectionalInscatteringLuminance; }
		if (Atmo) { Mie = Atmo->MieScatteringScale; Rayleigh = Atmo->RayleighScatteringScale; }
		if (Ppv) { ExposureBias = Ppv->Settings.AutoExposureBias; ExposureMin = Ppv->Settings.AutoExposureMinBrightness; ExposureMax = Ppv->Settings.AutoExposureMaxBrightness; }
		// A level's own trim on that exposure, kept as data so it can be tuned without a rebuild:
		// Data/<map>/look.json {"exposure_bias": stops}, or -TorqueExposure=stops on the command line
		// (beenster, build 140: the streamed Ride UK came out washed out next to Buttertubs).
		if (Ppv)
		{
			const FString Map = UWorld::RemovePIEPrefix(W->GetMapName());
			float Trim = 0.0f;
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("Data") / Map / TEXT("look.json"))))
			{
				TSharedPtr<FJsonObject> Look;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
				double Stops = 0.0;
				if (FJsonSerializer::Deserialize(Reader, Look) && Look.IsValid() && Look->TryGetNumberField(TEXT("exposure_bias"), Stops)) Trim = float(Stops);
			}
			FParse::Value(FCommandLine::Get(), TEXT("TorqueExposure="), Trim);
			ExposureBias += Trim;
			UE_LOG(LogTemp, Display, TEXT("Torque weather: %s exposure bias %.2f (trim %.2f), range %.1f to %.1f EV"), *Map, ExposureBias, Trim, ExposureMin, ExposureMax);
		}
	}
	const TorqueWeather::State& St = Here;
	const float Dull = (float)TorqueWeather::Dullness(St);    // 0 the sunny preset's sky .. 1 overcast's
	const float WetL = (float)TorqueWeather::WetLook(St);     // 0 dry .. 1 the wet preset's
	const float Haze = HazeNow();                              // 1, or more in poor visibility or up in low cloud
	AppliedHaze = Haze;
	if (Sun)
	{
		Sun->SetIntensity(SunLux * FMath::Lerp(1.0f, FMath::Lerp(0.14f, 0.10f, WetL), Dull) * DayLight);
		// A sun behind cloud: next to no shadow, from a light as wide as the bright patch of cloud. The sky
		// draws its sun disc as wide as the light, so under any cloud that softens the shadows the disc is
		// put out (beenster, build 135: "the sun is massive" under a live overcast, a 40 degree disc).
		const float Angle = FMath::Lerp(SunAngle, 40.0f, Dull);
		Sun->SetLightSourceAngle(Angle);
		const float Disc = FMath::Clamp(1.0f - (Angle - SunAngle) / 1.5f, 0.0f, 1.0f);
		Sun->SetAtmosphereSunDiskColorScale(FLinearColor(Disc, Disc, Disc, 1.0f));
	}
	if (Sky) Sky->SetIntensity(SkyIntensity * FMath::Lerp(1.0f, 2.5f, Dull));
	// A real camera opens up under cloud: the exposure follows (the level's histogram range was set for
	// sun, so it clamped and overcast came out far too dark).
	if (Ppv)
	{
		Ppv->Settings.bOverride_AutoExposureBias = true;
		Ppv->Settings.bOverride_AutoExposureMinBrightness = true;
		Ppv->Settings.bOverride_AutoExposureMaxBrightness = true;
		Ppv->Settings.AutoExposureBias = ExposureBias + Dull * FMath::Lerp(1.6f, 1.9f, WetL);
		// At night: indoors, open up (the shed under its lights); out under the sky, the exposure is held
		// in a narrow band a little open, so the road shows in your lights but the night stays night. With
		// the old wide range the camera kept opening for ten seconds after riding out, until the dark
		// looked like day (beenster, Nov 10).
		const float Night = 1.0f - DayLight;
		const float Out = FMath::Clamp(WeatherOpen(), 0.0f, 1.0f);
		AppliedOpen = Out;
		const float Lo = FMath::Lerp(ExposureMin - 3.0f * Dull, ExposureMin - FMath::Lerp(5.0f, 3.0f, Out), Night);
		const float Hi = FMath::Lerp(ExposureMax, ExposureMin - 2.0f, Night * Out);
		Ppv->Settings.AutoExposureMinBrightness = Lo;
		Ppv->Settings.AutoExposureMaxBrightness = FMath::Max(Hi, Lo + 0.5f);
	}
	if (Fog)
	{
		Fog->SetFogDensity(FogDensity * FMath::Lerp(1.0f, FMath::Lerp(2.0f, 3.0f, WetL), Dull) * Haze);
		// The haze's own glow is a fixed colour (the level's daylight grey): it has to go down with the
		// sun, or at night (exposure opened five stops) the far hills glow flat grey against a black sky
		// (beenster, Nov 10). A trace of it stays for moonlight and the glow of distant towns.
		const float Glow = FMath::Lerp(0.006f, 1.0f, DayLight);
		Fog->SetFogInscatteringColor(FogInscatter * Glow);
		Fog->SetDirectionalInscatteringColor(FogSunInscatter * DayLight);
	}
	if (Atmo)
	{
		// A white-grey British overcast: almost no Rayleigh (the blue), thick Mie (the white haze).
		Atmo->SetMieScatteringScale(Mie * FMath::Lerp(1.0f, 12.0f, Dull));
		Atmo->SetRayleighScatteringScale(Rayleigh * FMath::Lerp(1.0f, 0.08f, Dull));
	}
	if (WeatherMpc)
	{
		// (Only those the collection has: the level's build adds the wind and cloud ones.)
		auto SetMpc = [this, W](const TCHAR* Name, float Value) {
			if (WeatherMpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, WeatherMpc, FName(Name), Value);
		};
		SetMpc(TEXT("Wetness"), (float)FMath::Clamp(St.RoadWet, 0.0, 1.0));
		// The grass and trees sway with the real wind (1 is the breeze they were made with), leaning the
		// way it blows: x east, y south, so wind from the south west blows towards +x, -y.
		const double To = FMath::DegreesToRadians(St.WindFrom + 180.0);
		SetMpc(TEXT("Wind"), (float)TorqueWeather::SwayFromWind(St));
		SetMpc(TEXT("WindX"), (float)FMath::Sin(To));
		SetMpc(TEXT("WindY"), (float)-FMath::Cos(To));
		// Cloud shadows over the fells: the made look (0.6) on the sunny preset's fair day (a tenth of the
		// sky), none on a clear one, more of the ground in shade as the cloud builds.
		SetMpc(TEXT("CloudShadow"), (float)(St.Cloud <= 0.1 ? FMath::Lerp(1.0, 0.6, St.Cloud / 0.1) : FMath::Lerp(0.6, 0.45, FMath::Clamp((St.Cloud - 0.1) / 0.5, 0.0, 1.0))));
		SetMpc(TEXT("Rain"), (float)TorqueWeather::RainShow(St));
		// Standing water: the road's edges first (inside of bends most), the middle after hours of rain.
		SetMpc(TEXT("Puddles"), (float)FMath::Clamp(St.Puddles, 0.0, 1.0));
		SetMpc(TEXT("Snow"), (float)TorqueWeather::SnowCover(St));
		// (Ride UK stage 7: lamps and lit windows come on, cat's eyes catch the headlight, as the light goes)
		SetMpc(TEXT("Night"), 1.0f - DayLight);
	}
	// The log: as the kind changes (the tests read it), not ten times a second.
	if (Weather != LoggedKind)
	{
		LoggedKind = Weather;
		UE_LOG(LogTemp, Log, TEXT("Torque: weather %d"), Weather);
	}
}
