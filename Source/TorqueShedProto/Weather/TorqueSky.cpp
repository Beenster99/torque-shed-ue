#include "Weather/TorqueSky.h"

#include "Coop/TorqueGameState.h"
#include "Fit/FitJson.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace
{
// A float property by name (an engine field with no setter in every version), set and the component told.
bool SetFloatProperty(UObject* Obj, const TCHAR* Name, float Value)
{
	if (!Obj) return false;
	FFloatProperty* Prop = CastField<FFloatProperty>(Obj->GetClass()->FindPropertyByName(FName(Name)));
	if (!Prop) return false;
	Prop->SetPropertyValue_InContainer(Obj, Value);
	return true;
}

int32 ReadByteProperty(const UObject* Obj, const TCHAR* Name, int32 Default)
{
	if (!Obj) return Default;
	const FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(Name));
	if (const FByteProperty* Byte = CastField<FByteProperty>(Prop)) return Byte->GetPropertyValue_InContainer(Obj);
	if (const FEnumProperty* Enum = CastField<FEnumProperty>(Prop))
		return (int32)Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Obj));
	return Default;
}

UObject* ReadObjectProperty(const UObject* Obj, const TCHAR* Name)
{
	if (!Obj) return nullptr;
	const FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Obj->GetClass()->FindPropertyByName(FName(Name)));
	return Prop ? Prop->GetObjectPropertyValue_InContainer(Obj) : nullptr;
}

std::string ReadText(const FString& Path)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) ? std::string(TCHAR_TO_UTF8(*Text)) : std::string();
}
} // namespace

ATorqueSky::ATorqueSky()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATorqueSky::BeginPlay()
{
	Super::BeginPlay();
	Game = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	// The looks: Data/Sky/sky_looks.json over the defaults (the look the game always had).
	const FString LooksPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Sky/sky_looks.json"));
	const std::string Text = ReadText(LooksPath);
	std::string Why;
	if (Text.empty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque sky: no %s; the default looks"), *LooksPath);
	}
	else if (!TorqueSky::LoadLooks(Text, Looks, &Why))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque sky: %s isn't readable (%s); the default looks"), *LooksPath, UTF8_TO_TCHAR(Why.c_str()));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Torque sky: looks from %s"), *LooksPath);
	}

	FString BenchFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueSkyBench="), BenchFile) || FParse::Param(FCommandLine::Get(), TEXT("TorqueSkyBench")))
	{
		if (BenchFile.IsEmpty()) BenchFile = TEXT("bench.json");
		if (LoadBench(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Sky"), BenchFile)))
		{
			ShotAt = -1;
			ShotWait = 4.0;   // (the level settles first)
			BenchDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/SkyBench"));
			IFileManager::Get().MakeDirectory(*BenchDir, true);
			UE_LOG(LogTemp, Display, TEXT("Torque sky bench: %d skies from %s into %s"), Shots.Num(), *BenchFile, *BenchDir);
		}
	}
}

void ATorqueSky::Find()
{
	UWorld* W = GetWorld();
	if (!W) return;
	bFound = true;
	for (TActorIterator<ADirectionalLight> It(W); It && !SunPtr.IsValid(); ++It) SunPtr = Cast<UDirectionalLightComponent>(It->GetLightComponent());
	for (TActorIterator<ASkyLight> It(W); It && !SkyPtr.IsValid(); ++It) SkyPtr = It->GetLightComponent();
	for (TActorIterator<AExponentialHeightFog> It(W); It && !FogPtr.IsValid(); ++It) FogPtr = It->GetComponent();
	for (TActorIterator<AActor> It(W); It && !AtmoPtr.IsValid(); ++It) AtmoPtr = It->FindComponentByClass<USkyAtmosphereComponent>();
	for (TActorIterator<AActor> It(W); It && !CloudPtr.IsValid(); ++It) CloudPtr = It->FindComponentByClass<UVolumetricCloudComponent>();
	for (TActorIterator<APostProcessVolume> It(W); It && !PpvPtr.IsValid(); ++It)
	{
		if (It->bUnbound) PpvPtr = *It;
	}
	if (!WeatherMpc) WeatherMpc = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Buttertubs/Materials/MPC_Weather.MPC_Weather"));
	// (A level built before stage 2 has no MPC_Sky: the clouds then stay the engine's own.)
	if (!SkyMpc) SkyMpc = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Buttertubs/Materials/MPC_Sky.MPC_Sky"));
	UE_LOG(LogTemp, Log, TEXT("Torque sky: sun %s, sky light %s, fog %s, atmosphere %s, clouds %s (%s), MPC_Sky %s"),
		SunPtr.IsValid() ? TEXT("yes") : TEXT("no"), SkyPtr.IsValid() ? TEXT("yes") : TEXT("no"), FogPtr.IsValid() ? TEXT("yes") : TEXT("no"),
		AtmoPtr.IsValid() ? TEXT("yes") : TEXT("no"), CloudPtr.IsValid() ? TEXT("yes") : TEXT("no"), CloudsDrawn() ? TEXT("our own, drawn") : TEXT("not ours or off"),
		SkyMpc ? TEXT("yes") : TEXT("no"));
}

bool ATorqueSky::CloudsDrawn() const
{
	const UVolumetricCloudComponent* Cloud = CloudPtr.Get();
	if (!Cloud || !SkyMpc) return false;
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud"));
	if (CVar && CVar->GetInt() == 0) return false;
	const UObject* Mat = ReadObjectProperty(Cloud, TEXT("Material"));
	return Mat && Mat->GetName().Contains(TEXT("M_RideCloud"));
}

double ATorqueSky::PlanetAsl() const
{
	const ATorqueGameState* GS = Game.Get();
	if (!GS) return 0.0;
	// The atmosphere's ground: at its own place when the level puts the planet's top there (the levels
	// do: under the lowest land), otherwise at the level's origin.
	FVector At = FVector::ZeroVector;
	if (const USkyAtmosphereComponent* Atmo = AtmoPtr.Get())
		if (ReadByteProperty(Atmo, TEXT("TransformMode"), 0) == (int32)ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform)
			At = Atmo->GetComponentLocation();
	double Lat = 0.0, Lon = 0.0, Asl = 0.0;
	GS->GeoAt(At, Lat, Lon, Asl);
	return Asl;
}

float ATorqueSky::DayLightAt(double SunElevation) const
{
	TorqueSky::Inputs In;
	In.SunElevation = SunElevation;
	return (float)TorqueSky::Compute(In, Looks).DayLight;
}

double ATorqueSky::LightElevationAt(double SunElevation) const
{
	return FMath::Max(SunElevation, Looks.SunLowestDeg);
}

void ATorqueSky::Apply(const TorqueWeather::State& Weather, double SunElevation, bool bRealSun, double Haze, double GroundAsl, float Open)
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (!bFound || !SunPtr.IsValid()) Find();
	UDirectionalLightComponent* Sun = SunPtr.Get();
	USkyLightComponent* Sky = SkyPtr.Get();
	UExponentialHeightFogComponent* Fog = FogPtr.Get();
	USkyAtmosphereComponent* Atmo = AtmoPtr.Get();
	UVolumetricCloudComponent* Cloud = CloudPtr.Get();
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

	TorqueSky::Inputs In;
	In.Weather = Weather;
	In.SunElevation = SunElevation;
	In.bRealSun = bRealSun;
	In.Haze = Haze;
	In.GroundAsl = GroundAsl;
	In.bClouds = CloudsDrawn();
	const TorqueSky::Sky S = TorqueSky::Compute(In, Looks);
	Last = S;
	bApplied = true;
	// The day's light for the exposure and the fog's glow: the game state's (the level's own light by day).
	const float DayLight = Game.IsValid() ? Game->GetDayLight() : (float)S.DayLight;

	if (Sun)
	{
		Sun->SetIntensity(SunLux * (float)S.SunScale);
		// A sun behind a closed deck: next to no shadow, from a light as wide as the bright patch of cloud.
		// The sky draws its sun disc as wide as the light, so as the light widens the disc is put out
		// (beenster, build 135: "the sun is massive" under a live overcast, a 40 degree disc).
		Sun->SetLightSourceAngle(SunAngle + (float)S.SunAngle);
		const float Disc = (float)S.SunDisc;
		Sun->SetAtmosphereSunDiskColorScale(FLinearColor(Disc, Disc, Disc, 1.0f));
		// The drawn clouds' shadows on the ground: on while the sky is broken, off as the deck closes (the
		// dimmed sun takes over then).
		const float Strength = (float)S.CloudShadows;
		if (FMath::Abs(Strength - AppliedShadowStrength) > 0.02f && SetFloatProperty(Sun, TEXT("CloudShadowOnSurfaceStrength"), Strength))
		{
			AppliedShadowStrength = Strength;
			Sun->MarkRenderStateDirty();
		}
	}
	if (Sky) Sky->SetIntensity(SkyIntensity * (float)S.SkyLightScale);
	// A real camera opens up under cloud: the exposure follows (the level's histogram range was set for
	// sun, so it clamped and overcast came out far too dark).
	if (Ppv)
	{
		Ppv->Settings.bOverride_AutoExposureBias = true;
		Ppv->Settings.bOverride_AutoExposureMinBrightness = true;
		Ppv->Settings.bOverride_AutoExposureMaxBrightness = true;
		Ppv->Settings.AutoExposureBias = ExposureBias + (float)S.ExposureStops;
		// At night: indoors, open up (the shed under its lights); out under the sky, the exposure is held
		// in a narrow band a little open, so the road shows in your lights but the night stays night. With
		// the old wide range the camera kept opening for ten seconds after riding out, until the dark
		// looked like day (beenster, Nov 10).
		const float Dull = (float)S.Dull;
		const float Night = 1.0f - DayLight;
		const float Out = FMath::Clamp(Open, 0.0f, 1.0f);
		const float Lo = FMath::Lerp(ExposureMin - 3.0f * Dull, ExposureMin - FMath::Lerp(5.0f, 3.0f, Out), Night);
		const float Hi = FMath::Lerp(ExposureMax, ExposureMin - 2.0f, Night * Out);
		Ppv->Settings.AutoExposureMinBrightness = Lo;
		Ppv->Settings.AutoExposureMaxBrightness = FMath::Max(Hi, Lo + 0.5f);
	}
	if (Fog)
	{
		Fog->SetFogDensity(FogDensity * (float)S.FogScale);
		// The haze's own glow is a fixed colour (the level's daylight grey): it has to go down with the
		// sun, or at night (exposure opened five stops) the far hills glow flat grey against a black sky
		// (beenster, Nov 10). A trace of it stays for moonlight and the glow of distant towns.
		Fog->SetFogInscatteringColor(FogInscatter * FMath::Lerp((float)Looks.FogNightGlow, 1.0f, DayLight));
		Fog->SetDirectionalInscatteringColor(FogSunInscatter * DayLight);
	}
	if (Atmo)
	{
		// A white-grey British overcast: almost no Rayleigh (the blue), thick Mie (the white haze); less of
		// it faked once a real deck is drawn overhead. Clear air far off crisper, damp air hazier.
		Atmo->SetMieScatteringScale(Mie * (float)S.MieScale);
		Atmo->SetRayleighScatteringScale(Rayleigh * (float)S.RayleighScale);
	}
	// The clouds (stage 2): the drawn layer's base and depth, above the level's planet surface.
	if (Cloud && In.bClouds)
	{
		const double Ground = PlanetAsl();
		const float BottomKm = (float)FMath::Max((S.BaseAsl - Ground) / 1000.0, 0.05);
		const float HeightKm = (float)FMath::Max((S.TopAsl - S.BaseAsl) / 1000.0, 0.2);
		if (FMath::Abs(AppliedBottomKm - BottomKm) > 0.005f || FMath::Abs(AppliedHeightKm - HeightKm) > 0.005f)
		{
			AppliedBottomKm = BottomKm;
			AppliedHeightKm = HeightKm;
			// (By name, as the setters do it: they aren't exported from the engine in every version.)
			SetFloatProperty(Cloud, TEXT("LayerBottomAltitude"), BottomKm);
			SetFloatProperty(Cloud, TEXT("LayerHeight"), HeightKm);
			Cloud->MarkRenderStateDirty();
		}
	}
	auto SetMpc = [W](UMaterialParameterCollection* Mpc, const TCHAR* Name, float Value) {
		if (Mpc && Mpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, FName(Name), Value);
	};
	if (SkyMpc)
	{
		SetMpc(SkyMpc, TEXT("Cover"), (float)S.CoverShown);
		SetMpc(SkyMpc, TEXT("Density"), (float)S.Density);
		SetMpc(SkyMpc, TEXT("Albedo"), (float)S.Albedo);
		SetMpc(SkyMpc, TEXT("Shape"), (float)S.Shape);
	}
	// The old noise cloud shadows: where no clouds are drawn (the Low setting), the cover's amount; where
	// they are, none (the clouds you see cast their own).
	SetMpc(WeatherMpc, TEXT("CloudShadow"), (float)S.LegacyShadow);
}

void ATorqueSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickDrift(DeltaSeconds);
	if (Shots.Num() > 0) TickBench(DeltaSeconds);
}

void ATorqueSky::TickDrift(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	ATorqueGameState* GS = Game.Get();
	if (!W || !bApplied) return;
	// Carried on the wind at cloud height, every frame (the clouds move smoothly).
	DriftE += Last.DriftE * DeltaSeconds;
	DriftN += Last.DriftN * DeltaSeconds;
	if (GS)
	{
		double SharedE = 0.0, SharedN = 0.0;
		if (GS->HasAuthority())
		{
			// To everyone every couple of seconds.
			if (W->GetTimeSeconds() - SentDriftAt > 2.0)
			{
				SentDriftAt = W->GetTimeSeconds();
				GS->ShareSkyDrift(DriftE, DriftN);
			}
		}
		else if (GS->SharedSkyDrift(SharedE, SharedN))
		{
			// Joined: the host's at once; then eased towards it (it's a couple of seconds old: close enough).
			const double Off = FMath::Sqrt(FMath::Square(SharedE - DriftE) + FMath::Square(SharedN - DriftN));
			const double K = Off > 2000.0 ? 1.0 : FMath::Min(DeltaSeconds * 0.5, 1.0);
			DriftE += (SharedE - DriftE) * K;
			DriftN += (SharedN - DriftN) * K;
		}
	}
	if (SkyMpc)
	{
		// (Metres along the level's x east and y south.)
		if (SkyMpc->GetScalarParameterByName(TEXT("DriftX"))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, TEXT("DriftX"), (float)DriftE);
		if (SkyMpc->GetScalarParameterByName(TEXT("DriftY"))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, TEXT("DriftY"), (float)-DriftN);
	}
}

FString ATorqueSky::Describe() const
{
	const TorqueSky::Sky& S = Last;
	return FString::Printf(TEXT("sun %.1f deg (light at %.1f), block %.2f, dull %.2f, %.0f lux (EV100 %.1f; clear sky %.0f), day light %.2f, sun x%.2f, sky light x%.2f, exposure %+.2f, mie x%.2f, rayleigh x%.2f, fog x%.2f; cloud %.0f%% (drawn %.2f, shape %.2f, density %.2f) %.0f-%.0f m, drifting %.1f m/s E %.1f m/s N, its shadows %.2f; clouds %s"),
		S.SunElevation, S.LightElevation, S.Block, S.Dull, S.GroundLux, TorqueSky::Ev100(S.GroundLux), S.ClearLux, S.DayLight, S.SunScale, S.SkyLightScale,
		S.ExposureStops, S.MieScale, S.RayleighScale, S.FogScale, S.Cover * 100.0, S.CoverShown, S.Shape, S.Density, S.BaseAsl, S.TopAsl, S.DriftE, S.DriftN,
		S.CloudShadows, CloudsDrawn() ? TEXT("drawn") : TEXT("not drawn"));
}

// --- The sky bench ---------------------------------------------------------------------------------------

bool ATorqueSky::LoadBench(const FString& File)
{
	const std::string Text = ReadText(File);
	TorqueFit::Json Root;
	std::string Why;
	if (Text.empty() || !TorqueFit::Json::Parse(Text, Root, &Why) || !Root.IsObject())
	{
		UE_LOG(LogTemp, Error, TEXT("Torque sky bench: can't read %s %s"), *File, UTF8_TO_TCHAR(Why.c_str()));
		return false;
	}
	Settle = Root.Num("settle_seconds", 8.0);
	const TorqueFit::Json* List = Root.Get("shots");
	if (!List || !List->IsArray()) return false;
	for (const TorqueFit::Json& J : List->A)
	{
		FShot Shot;
		Shot.Name = UTF8_TO_TCHAR(J.Str("name", "shot").c_str());
		Shot.Look = UTF8_TO_TCHAR(J.Str("look", "horizon").c_str());
		Shot.Yaw = J.Num("yaw", 0.0);
		Shot.Note = UTF8_TO_TCHAR(J.Str("note").c_str());
		Shot.Lat = J.Num("lat", 54.35);
		Shot.Day = (int32)J.Num("day", 172.0);
		Shot.SolarHour = J.Num("hour", 12.0);
		// A recorded day (Data/Sky/days/<name>.json): its weather, its place and its hour.
		const std::string Recorded = J.Str("recorded");
		if (!Recorded.empty())
		{
			const FString DayPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Sky/days"), UTF8_TO_TCHAR((Recorded + ".json").c_str()));
			const std::string DayText = ReadText(DayPath);
			TorqueFit::Json DayRoot;
			long long When = 0;
			if (!TorqueWeather::ParseOpenMeteo(DayText, Shot.Weather, &When, &Why) || !TorqueFit::Json::Parse(DayText, DayRoot))
			{
				UE_LOG(LogTemp, Warning, TEXT("Torque sky bench: recorded day %s unreadable (%s)"), *DayPath, UTF8_TO_TCHAR(Why.c_str()));
				continue;
			}
			Shot.Lat = DayRoot.Num("latitude", Shot.Lat);
			const FDateTime At = FDateTime::FromUnixTimestamp(When);
			Shot.Day = At.GetDayOfYear();
			Shot.SolarHour = FMath::Fmod(At.GetHour() + At.GetMinute() / 60.0 + DayRoot.Num("longitude", -2.2) / 15.0
				+ TorqueWeather::EquationOfTime(Shot.Day) / 60.0 + 24.0, 24.0);
			if (Shot.Note.IsEmpty()) Shot.Note = FString::Printf(TEXT("recorded %s"), *At.ToString(TEXT("%Y-%m-%d %H:%M UTC")));
		}
		if (const TorqueFit::Json* Weather = J.Get("weather")) TorqueSky::StateFromJson(*Weather, Shot.Weather);
		// The sun's height instead of an hour: found on the day, in the morning or the evening.
		if (const TorqueFit::Json* Sun = J.Get("sun"); Sun && Sun->IsNumber())
		{
			const bool bEvening = J.Flag("evening", false);
			double Best = 1e9;
			for (double H = bEvening ? 12.0 : 0.0; H <= (bEvening ? 24.0 : 12.0); H += 1.0 / 120.0)
			{
				double E = 0.0, B = 0.0;
				TorqueSky::SunAt(Shot.Lat, Shot.Day, H, E, B);
				if (FMath::Abs(E - Sun->N) < Best) { Best = FMath::Abs(E - Sun->N); Shot.SolarHour = H; }
			}
		}
		Shots.Add(Shot);
	}
	return Shots.Num() > 0;
}

void ATorqueSky::ShowShot(const FShot& Shot)
{
	UWorld* W = GetWorld();
	ATorqueGameState* GS = Game.Get();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!W || !GS || !PC) return;
	GS->BenchSky(Shot.Weather, Shot.Lat, Shot.Day, Shot.SolarHour);
	// The camera: where the player's view was, a little higher, looking where the shot says.
	if (!BenchCamera)
	{
		const FVector From = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BenchCamera = W->SpawnActor<ACameraActor>(From + FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator, Params);
		if (BenchCamera && BenchCamera->GetCameraComponent()) BenchCamera->GetCameraComponent()->SetFieldOfView(80.0f);
	}
	if (!BenchCamera) return;
	double E = 0.0, Bearing = 0.0;
	TorqueSky::SunAt(Shot.Lat, Shot.Day, Shot.SolarHour, E, Bearing);
	// (The levels: x east, y south; a bearing from north, clockwise, as a yaw.)
	auto YawOf = [](double FromNorth) { return (float)FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(FMath::DegreesToRadians(FromNorth)), FMath::Sin(FMath::DegreesToRadians(FromNorth)))); };
	FRotator Rot(4.0f, YawOf(Shot.Yaw), 0.0f);
	if (Shot.Look == TEXT("up")) Rot = FRotator(89.0f, 0.0f, 0.0f);
	else if (Shot.Look == TEXT("sun")) Rot = FRotator(FMath::Clamp((float)E, 4.0f, 60.0f), YawOf(Bearing), 0.0f);
	else if (Shot.Look == TEXT("away")) Rot = FRotator(8.0f, YawOf(Bearing + 180.0), 0.0f);
	BenchCamera->SetActorRotation(Rot);
	PC->SetViewTarget(BenchCamera);
}

void ATorqueSky::TickBench(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	if (!W) return;
	if ((ShotWait -= DeltaSeconds) > 0.0) return;
	// The shot shown has settled (the camera's exposure follows over a few seconds): take it, and give the
	// screenshot a second to be written before the sky changes for the next.
	if (ShotAt >= 0 && ShotAt < Shots.Num() && !bShotTaken)
	{
		bShotTaken = true;
		ShotWait = 1.0;
		const FShot& Shot = Shots[ShotAt];
		const FString File = FPaths::Combine(BenchDir, Shot.Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, false, false);
		const FString Line = FString::Printf(TEXT("  {\"name\": \"%s\", \"file\": \"%s.png\", \"note\": \"%s\", \"day\": %d, \"solar_hour\": %.2f, \"sky\": \"%s\"}"),
			*Shot.Name, *Shot.Name, *Shot.Note.Replace(TEXT("\""), TEXT("'")), Shot.Day, Shot.SolarHour, *Describe());
		BenchLog += (BenchLog.IsEmpty() ? FString() : FString(TEXT(",\n"))) + Line;
		UE_LOG(LogTemp, Display, TEXT("Torque sky bench: %s: %s"), *Shot.Name, *Describe());
		return;
	}
	bShotTaken = false;
	++ShotAt;
	if (ShotAt >= Shots.Num())
	{
		FFileHelper::SaveStringToFile(TEXT("{\"shots\": [\n") + BenchLog + TEXT("\n]}\n"), *FPaths::Combine(BenchDir, TEXT("bench.json")));
		UE_LOG(LogTemp, Display, TEXT("Torque sky bench: done, %d shots in %s"), Shots.Num(), *BenchDir);
		Shots.Reset();
		if (!FParse::Param(FCommandLine::Get(), TEXT("TorqueSkyBenchStay")))
			if (APlayerController* PC = W->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	ShowShot(Shots[ShotAt]);
	ShotWait = Settle;
}
