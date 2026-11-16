#include "Weather/TorqueSky.h"

#include "Coop/TorqueGameState.h"
#include "Fit/FitJson.h"
#include "Weather/TorqueCloudGuard.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
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
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "RHI.h"
#include "TextureResource.h"
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

	// Stage 7: the light a lightning flash casts on the land, off until one does.
	FlashLight = NewObject<UPointLightComponent>(this, TEXT("FlashLight"));
	if (FlashLight)
	{
		FlashLight->SetMobility(EComponentMobility::Movable);
		FlashLight->SetupAttachment(GetRootComponent());
		FlashLight->RegisterComponent();
		FlashLight->bUseInverseSquaredFalloff = true;
		FlashLight->SetIntensityUnits(ELightUnits::Candelas);
		FlashLight->SetAttenuationRadius(3000000.0f);   // 30 km
		FlashLight->SetCastShadows(false);
		FlashLight->SetLightColor(FLinearColor(0.82f, 0.86f, 1.0f));
		FlashLight->SetIntensity(0.0f);
		FlashLight->SetVisibility(false);
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
	// (the guard has already looked at the cloud before the first frame; this is in case it didn't run)
	TorqueCloudGuard::Check(W);
	for (TActorIterator<AActor> It(W); It && !CloudPtr.IsValid(); ++It) CloudPtr = It->FindComponentByClass<UVolumetricCloudComponent>();
	// The rebuilt clouds (M_RideCloudV2, Nov 16) in place of M_RideCloud, when asked (sky_looks.json
	// cloud_version 2, or -TorqueCloud=2), before the weather map's instance is made from the cloud's material.
	{
		int32 Version = FMath::RoundToInt(Looks.CloudVersion);
		FParse::Value(FCommandLine::Get(), TEXT("TorqueCloud="), Version);
		UVolumetricCloudComponent* Cloud = CloudPtr.Get();
		FObjectPropertyBase* Prop = Cloud ? CastField<FObjectPropertyBase>(Cloud->GetClass()->FindPropertyByName(FName(TEXT("Material")))) : nullptr;
		UMaterialInterface* Now = Prop ? Cast<UMaterialInterface>(Prop->GetObjectPropertyValue_InContainer(Cloud)) : nullptr;
		const bool bRideNow = Now && Now->GetMaterial() && Now->GetMaterial()->GetName() == TEXT("M_RideCloud");
		bCloudV2 = false;
		if (Version == 2 && bRideNow)
		{
			UMaterialInterface* V2 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_RideCloudV2.M_RideCloudV2"));
			const FString Why = TorqueCloudGuard::WhyNot(V2, W);
			if (Why.IsEmpty())
			{
				Prop->SetObjectPropertyValue_InContainer(Cloud, V2);
				Cloud->MarkRenderStateDirty();
				bCloudV2 = true;
				UE_LOG(LogTemp, Display, TEXT("Torque sky: the rebuilt clouds (M_RideCloudV2)"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Torque sky: M_RideCloudV2 won't do (%s): the old clouds stay"), *Why);
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Torque sky: clouds version %d (%s)"), Version, *GetNameSafe(Now));
		}
	}
	for (TActorIterator<AActor> It(W); It && !HighSheetPtr.IsValid(); ++It)
		if (It->ActorHasTag(FName(TEXT("TorqueHighCloud")))) HighSheetPtr = *It;
	// Stage 4: the rain shafts and the rainbow (Tools/ue_build_level.py places them, hidden).
	Shafts.Reset();
	Bolts.Reset();
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (It->ActorHasTag(FName(TEXT("TorqueRainShaft"))))
		{
			FShaftSlot Slot;
			Slot.Actor = *It;
			Shafts.Add(Slot);
		}
		if (!RainbowPtr.IsValid() && It->ActorHasTag(FName(TEXT("TorqueRainbow")))) RainbowPtr = *It;
		if (!StarDomePtr.IsValid() && It->ActorHasTag(FName(TEXT("TorqueStarDome")))) StarDomePtr = *It;
		if (It->ActorHasTag(FName(TEXT("TorqueBolt"))))
		{
			FBoltSlot Slot;
			Slot.Actor = *It;
			Bolts.Add(Slot);
		}
	}
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
	if (!Cloud || !SkyMpc || !Cloud->IsVisible()) return false;
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud"));
	if (CVar && CVar->GetInt() == 0) return false;
	// (by the base material: the cloud wears an instance of it once the weather map is on)
	const UMaterialInterface* Mat = Cast<const UMaterialInterface>(ReadObjectProperty(Cloud, TEXT("Material")));
	const UMaterial* Base = Mat ? Mat->GetMaterial() : nullptr;
	return Base && Base->GetName().Contains(TEXT("M_RideCloud"));
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

void ATorqueSky::Apply(const TorqueWeather::State& Weather, double SunElevation, bool bRealSun, double Haze, double GroundAsl, float Open,
	double ViewAsl, double ValleyAsl, double RainNear)
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
		if (Sun) { SunLux = Sun->Intensity; SunAngle = Sun->LightSourceAngle; SunColour = Sun->LightColor; }
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
	GroundAslNow = GroundAsl;
	In.ViewAsl = ViewAsl;
	In.ValleyAsl = ValleyAsl;
	In.RainNear = RainNear;
	if (bSkyClock)
	{
		In.MoonElevation = MoonNow.Elevation;
		In.MoonPhaseAngle = MoonNow.PhaseAngle;
	}
	// Stage 7: the place and the day (the aurora, noctilucent cloud, meteors), the space weather and dust.
	In.Lat = SkyLat;
	In.Lon = SkyLon;
	In.DayOfYear = bSkyClock ? FDateTime::FromUnixTimestamp((int64)SkyUnix).GetDayOfYear() : FDateTime::UtcNow().GetDayOfYear();
	if (Game.IsValid())
	{
		In.Kp = Game->GetSkyKp();
		In.Dust = Game->GetSkyDust();
		In.MeteorRate = Game->GetPresetMeteors();
	}
	WeatherNow = Weather;
	OpenNow = Open;
	In.bClouds = CloudsDrawn();
	const TorqueSky::Sky S = TorqueSky::Compute(In, Looks);
	Last = S;
	bApplied = true;
	// The day's light for the exposure and the fog's glow: the game state's (the level's own light by day).
	const float DayLight = Game.IsValid() ? Game->GetDayLight() : (float)S.DayLight;

	if (Sun)
	{
		// (at night with the moon up, the light is the moon's: the game state points it there)
		Sun->SetIntensity(SunLux * (float)(S.MoonTakeover > 0.5 ? S.MoonLight : S.SunScale));
		// A sun behind a closed deck: next to no shadow, from a light as wide as the bright patch of cloud.
		// The sky draws its sun disc as wide as the light, so as the light widens the disc is put out
		// (beenster, build 135: "the sun is massive" under a live overcast, a 40 degree disc).
		Sun->SetLightSourceAngle(SunAngle + (float)S.SunAngle);
		const float Disc = S.MoonTakeover > 0.5 ? 0.0f : (float)S.SunDisc;   // (the star dome draws the moon)
		Sun->SetAtmosphereSunDiskColorScale(FLinearColor(Disc, Disc, Disc, 1.0f));
		// Saharan dust (stage 7): a warmer, redder sun.
		const FLinearColor Base(SunColour);
		const FLinearColor Warm = FMath::Lerp(Base, FLinearColor(Base.R, Base.G * 0.62f, Base.B * 0.38f, 1.0f), (float)S.SunWarm);
		if (!Sun->GetLightColor().Equals(Warm, 0.004f)) Sun->SetLightColor(Warm);
		// The drawn clouds' shadows on the ground: on while the sky is broken, off as the deck closes (the
		// dimmed sun takes over then).
		const float Strength = (float)S.CloudShadows;
		if (FMath::Abs(Strength - AppliedShadowStrength) > 0.02f && SetFloatProperty(Sun, TEXT("CloudShadowOnSurfaceStrength"), Strength))
		{
			AppliedShadowStrength = Strength;
			Sun->MarkRenderStateDirty();
		}
	}
	if (Sky) Sky->SetIntensity(SkyIntensity * (float)(S.SkyLightScale * S.SkyFill));
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
		// By day the camera may open further in a hill's shadow, and as the sun is lower than the level's
		// own (build 147: a November 11am in shade came out as dusk, held at the level's lowest exposure).
		// Indoors (the shed under its own lights) less of it, so the shed keeps the look it had.
		const float ShadeOpen = (float)S.ExposureOpenStops * FMath::Lerp(0.4f, 1.0f, Out);
		const float Lo = FMath::Lerp(ExposureMin - 3.0f * Dull - ShadeOpen, ExposureMin - FMath::Lerp(5.0f, 3.0f, Out), Night);
		const float Hi = FMath::Lerp(ExposureMax, ExposureMin - 2.0f, Night * Out);
		Ppv->Settings.AutoExposureMinBrightness = Lo;
		Ppv->Settings.AutoExposureMaxBrightness = FMath::Max(Hi, Lo + 0.5f);
		// The engine's local exposure: the shade lifted without lifting the sky (sky_looks.json).
		Ppv->Settings.bOverride_LocalExposureShadowContrastScale = true;
		Ppv->Settings.bOverride_LocalExposureHighlightContrastScale = true;
		Ppv->Settings.LocalExposureShadowContrastScale = (float)FMath::Clamp(Looks.LocalShadowContrast, 0.0, 1.0);
		Ppv->Settings.LocalExposureHighlightContrastScale = (float)FMath::Clamp(Looks.LocalHighlightContrast, 0.0, 1.0);
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
		// The ray marching's samples (build 148's flickering white dots), by name as above.
		const float ViewSamples = (float)FMath::Clamp(Looks.CloudViewSamples, 0.25, 8.0);
		const float ShadowSamples = (float)FMath::Clamp(Looks.CloudShadowSamples, 0.25, 8.0);
		if (ViewSamples != AppliedViewSamples || ShadowSamples != AppliedShadowSamples)
		{
			AppliedViewSamples = ViewSamples;
			AppliedShadowSamples = ShadowSamples;
			const bool bView = SetFloatProperty(Cloud, TEXT("ViewSampleCountScale"), ViewSamples);
			const bool bShadow = SetFloatProperty(Cloud, TEXT("ShadowViewSampleCountScale"), ShadowSamples);
			Cloud->MarkRenderStateDirty();
			UE_LOG(LogTemp, Log, TEXT("Torque sky: cloud samples x%.2f along the view (%s), x%.2f along the shadow (%s)"),
				ViewSamples, bView ? TEXT("set") : TEXT("NOT set"), ShadowSamples, bShadow ? TEXT("set") : TEXT("NOT set"));
		}
	}
	auto SetMpc = [W](UMaterialParameterCollection* Mpc, const TCHAR* Name, float Value) {
		if (Mpc && Mpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, FName(Name), Value);
	};
	if (SkyMpc)
	{
		SetMpc(SkyMpc, TEXT("Cover"), (float)S.CoverShown);
		const double DensityMost = FMath::Max(Looks.DensityMost, 0.1);
		SetMpc(SkyMpc, TEXT("Density"), (float)FMath::Min(S.Density * Looks.ExtinctionScale, DensityMost));
		SetMpc(SkyMpc, TEXT("Albedo"), (float)S.Albedo);
		SetMpc(SkyMpc, TEXT("Shape"), (float)S.Shape);
		// Stage 3: the two layers' bands and looks, the high sheet and the halo.
		const TorqueSky::Sky::Drawn& Lo = S.LowDrawn;
		const TorqueSky::Sky::Drawn& Mi = S.MidDrawn;
		SetMpc(SkyMpc, TEXT("LowBottom"), (float)Lo.Bottom);
		SetMpc(SkyMpc, TEXT("LowTop"), (float)Lo.Top);
		SetMpc(SkyMpc, TEXT("LowLumps"), (float)Lo.Look.Lumps);
		SetMpc(SkyMpc, TEXT("LowRagged"), (float)Lo.Look.Ragged);
		SetMpc(SkyMpc, TEXT("LowBaseDark"), (float)Lo.Look.BaseDark);
		SetMpc(SkyMpc, TEXT("MidCover"), (float)Mi.CoverShown);
		SetMpc(SkyMpc, TEXT("MidBottom"), (float)Mi.Bottom);
		SetMpc(SkyMpc, TEXT("MidTop"), (float)Mi.Top);
		SetMpc(SkyMpc, TEXT("MidDensity"), (float)FMath::Min(Mi.Look.Density * Looks.ExtinctionScale, DensityMost));
		SetMpc(SkyMpc, TEXT("MidShape"), (float)Mi.Look.Shape);
		SetMpc(SkyMpc, TEXT("MidLumps"), (float)Mi.Look.Lumps);
		SetMpc(SkyMpc, TEXT("MidRagged"), (float)Mi.Look.Ragged);
		SetMpc(SkyMpc, TEXT("MidAlbedo"), (float)Mi.Albedo);
		SetMpc(SkyMpc, TEXT("MidBaseDark"), (float)Mi.Look.BaseDark);
		// The rebuilt clouds' figures (M_RideCloudV2; harmless to the old cloud, which doesn't read them).
		SetMpc(SkyMpc, TEXT("V2LowShapeM"), (float)FMath::Max(Looks.V2ShapeM * (1.3 - 0.6 * FMath::Clamp(Lo.Look.Lumps, 0.0, 1.0)), 100.0));
		const double LayerDepth = FMath::Max(S.TopAsl - S.BaseAsl, 0.0);
		SetMpc(SkyMpc, TEXT("V2LowDetailM"), (float)TorqueSky::V2DetailSize(Looks.V2DetailM, LayerDepth * FMath::Max(Lo.Top - Lo.Bottom, 0.0), Looks));
		SetMpc(SkyMpc, TEXT("V2MidShapeM"), (float)FMath::Max(Looks.V2MidShapeM * (1.3 - 0.6 * FMath::Clamp(Mi.Look.Lumps, 0.0, 1.0)), 100.0));
		SetMpc(SkyMpc, TEXT("V2MidDetailM"), (float)TorqueSky::V2DetailSize(Looks.V2MidDetailM, LayerDepth * FMath::Max(Mi.Top - Mi.Bottom, 0.0), Looks));
		SetMpc(SkyMpc, TEXT("V2BroadM"), (float)FMath::Max(Looks.V2BroadM, 1000.0));
		SetMpc(SkyMpc, TEXT("V2Patchy"), (float)Looks.V2Patchy);
		SetMpc(SkyMpc, TEXT("V2Erode"), (float)Looks.V2Erode);
		SetMpc(SkyMpc, TEXT("V2ErodeRagged"), (float)Looks.V2ErodeRagged);
		SetMpc(SkyMpc, TEXT("V2EdgeSharp"), (float)FMath::Max(Looks.V2EdgeSharp, 0.1));
		SetMpc(SkyMpc, TEXT("V2Billow"), (float)Looks.V2Billow);
		SetMpc(SkyMpc, TEXT("V2Extinction"), (float)Looks.V2Extinction);
		SetMpc(SkyMpc, TEXT("V2DeckThin"), (float)Looks.V2DeckThin);
		SetMpc(SkyMpc, TEXT("V2ClosedFrom"), (float)Looks.V2ClosedFrom);
		SetMpc(SkyMpc, TEXT("V2ClosedFull"), (float)Looks.V2ClosedFull);
		SetMpc(SkyMpc, TEXT("V2LowDensity"), (float)(S.Density));
		SetMpc(SkyMpc, TEXT("V2MidDensity"), (float)Mi.Look.Density);
		// How the cloud scatters the sun (build 147: grey, blotchy cumulus): its lobes and the light
		// bounced about inside it.
		SetMpc(SkyMpc, TEXT("CloudPhaseG"), (float)Looks.CloudPhaseG);
		SetMpc(SkyMpc, TEXT("CloudPhaseG2"), (float)Looks.CloudPhaseG2);
		SetMpc(SkyMpc, TEXT("CloudPhaseBlend"), (float)Looks.CloudPhaseBlend);
		SetMpc(SkyMpc, TEXT("CloudMsContribution"), (float)(Looks.CloudMsContribution * S.CloudMs));
		SetMpc(SkyMpc, TEXT("CloudMsOcclusion"), (float)Looks.CloudMsOcclusion);
		SetMpc(SkyMpc, TEXT("CloudMsEccentricity"), (float)Looks.CloudMsEccentricity);
		SetMpc(SkyMpc, TEXT("HighCover"), (float)S.HighShown);
		SetMpc(SkyMpc, TEXT("HighSheet"), (float)S.HighSheet);
		SetMpc(SkyMpc, TEXT("HighOpacity"), (float)Looks.HighOpacity);
		SetMpc(SkyMpc, TEXT("Halo"), (float)S.Halo);
		SetMpc(SkyMpc, TEXT("ShaftLight"), (float)Looks.ShaftLight);
		// Stage 5: the night sky (the star dome's).
		SetMpc(SkyMpc, TEXT("Stars"), (float)(S.Stars * Looks.StarGain));
		SetMpc(SkyMpc, TEXT("MilkyWay"), (float)(S.MilkyWay * Looks.MilkyWayGain));
		SetMpc(SkyMpc, TEXT("MoonDisc"), (float)(S.MoonDisc * SunLux));
		SetMpc(SkyMpc, TEXT("EarthShine"), (float)Looks.EarthShine);
		SetMpc(SkyMpc, TEXT("SkyGlow"), (float)S.Glow);
		// Stage 7: the storm's anvil; the rare skies.
		SetMpc(SkyMpc, TEXT("Anvil"), (float)S.Anvil);
		SetMpc(SkyMpc, TEXT("SunDogs"), (float)S.SunDogs);
		SetMpc(SkyMpc, TEXT("Contrails"), (float)S.Contrails);
		SetMpc(SkyMpc, TEXT("Fogbow"), (float)S.Fogbow);
		SetMpc(SkyMpc, TEXT("Glory"), (float)S.Glory);
		SetMpc(SkyMpc, TEXT("Aurora"), (float)(S.Aurora * Looks.AuroraGain));
		SetMpc(SkyMpc, TEXT("AuroraTop"), (float)S.AuroraTopDeg);
		SetMpc(SkyMpc, TEXT("AuroraBearing"), (float)S.AuroraBearing);
		SetMpc(SkyMpc, TEXT("Nlc"), (float)(S.Noctilucent * Looks.NlcGain));
	}
	// The old noise cloud shadows: where no clouds are drawn (the Low setting), the cover's amount; where
	// they are, none (the clouds you see cast their own).
	SetMpc(WeatherMpc, TEXT("CloudShadow"), (float)S.LegacyShadow);
}

void ATorqueSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickDrift(DeltaSeconds);
	TickHighSheet();
	TickMist();
	TickWeatherMap();
	TickRainShafts(DeltaSeconds);
	TickRainbow();
	TickStarDome();
	TickLightning(DeltaSeconds);
	TickMeteors();
	if (Shots.Num() > 0) TickBench(DeltaSeconds);
}

void ATorqueSky::TickDrift(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	ATorqueGameState* GS = Game.Get();
	if (!W || !bApplied) return;
	// Carried on the wind at each layer's height, every frame (the clouds move smoothly).
	const double SpeedE[3] = {Last.LowDrawn.DriftE, Last.MidDrawn.DriftE, Last.HighDriftE};
	const double SpeedN[3] = {Last.LowDrawn.DriftN, Last.MidDrawn.DriftN, Last.HighDriftN};
	const bool bSend = GS && GS->HasAuthority() && W->GetTimeSeconds() - SentDriftAt > 2.0;   // to everyone every couple of seconds
	if (bSend) SentDriftAt = W->GetTimeSeconds();
	for (int32 Ly = 0; Ly < 3; ++Ly)
	{
		DriftE[Ly] += SpeedE[Ly] * DeltaSeconds;
		DriftN[Ly] += SpeedN[Ly] * DeltaSeconds;
		if (!GS) continue;
		double SharedE = 0.0, SharedN = 0.0;
		if (GS->HasAuthority())
		{
			if (bSend) GS->ShareSkyDrift(Ly, DriftE[Ly], DriftN[Ly]);
		}
		else if (GS->SharedSkyDrift(Ly, SharedE, SharedN))
		{
			// Joined: the host's at once; then eased towards it (it's a couple of seconds old: close enough).
			const double Off = FMath::Sqrt(FMath::Square(SharedE - DriftE[Ly]) + FMath::Square(SharedN - DriftN[Ly]));
			const double K = Off > 2000.0 ? 1.0 : FMath::Min(DeltaSeconds * 0.5, 1.0);
			DriftE[Ly] += (SharedE - DriftE[Ly]) * K;
			DriftN[Ly] += (SharedN - DriftN[Ly]) * K;
		}
	}
	if (SkyMpc)
	{
		// (Metres along the level's x east and y south.)
		static const TCHAR* const Names[3][2] = {{TEXT("DriftX"), TEXT("DriftY")}, {TEXT("MidDriftX"), TEXT("MidDriftY")}, {TEXT("HighDriftX"), TEXT("HighDriftY")}};
		for (int32 Ly = 0; Ly < 3; ++Ly)
		{
			if (SkyMpc->GetScalarParameterByName(FName(Names[Ly][0]))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Names[Ly][0]), (float)DriftE[Ly]);
			if (SkyMpc->GetScalarParameterByName(FName(Names[Ly][1]))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Names[Ly][1]), (float)-DriftN[Ly]);
		}
	}
}

bool ATorqueSky::CameraGeo(FVector& Cam, double& Lat, double& Lon, double& OriginAsl) const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	const ATorqueGameState* GS = Game.Get();
	if (!PC || !PC->PlayerCameraManager || !GS) return false;
	Cam = PC->PlayerCameraManager->GetCameraLocation();
	double Asl = 0.0, OLat = 0.0, OLon = 0.0;
	GS->GeoAt(Cam, Lat, Lon, Asl);
	GS->GeoAt(FVector::ZeroVector, OLat, OLon, OriginAsl);
	return true;
}

void ATorqueSky::TickWeatherMap()
{
	UWorld* W = GetWorld();
	const ATorqueGameState* GS = Game.Get();
	if (!W || !GS || !SkyMpc || !bApplied) return;
	auto SetMpc = [W, this](const TCHAR* Name, float Value) {
		if (SkyMpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Name), Value);
	};
	FVector Cam;
	double Lat = 0.0, Lon = 0.0, OriginAsl = 0.0;
	const TorqueSky::MapGrids Grids = GS->SkyGrids();
	const bool bWant = CloudsDrawn() && Grids.Any() && Looks.MapGain > 0.0 && CameraGeo(Cam, Lat, Lon, OriginAsl);
	if (!bWant)
	{
		if (MapOnApplied != 0.0f) SetMpc(TEXT("MapOn"), 0.0f);
		MapOnApplied = 0.0f;
		return;
	}
	// The cloud wears an instance of its material, with the map on it (only when the material has the
	// map's slot: an older level's has none, and the map stays off).
	UVolumetricCloudComponent* Cloud = CloudPtr.Get();
	if (!CloudMid && Cloud && !bMapTried)
	{
		bMapTried = true;
		UMaterialInterface* Mat = Cast<UMaterialInterface>(const_cast<UObject*>(ReadObjectProperty(Cloud, TEXT("Material"))));
		bool bHasMap = false;
		if (Mat)
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Mat->GetAllTextureParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& Info : Infos) bHasMap |= Info.Name == FName(TEXT("WeatherMap"));
		}
		if (bHasMap)
		{
			CloudMid = UMaterialInstanceDynamic::Create(Mat, this);
			const int32 Px = FMath::Clamp((int32)Looks.MapPx, 8, 256);
			MapTex = UTexture2D::CreateTransient(Px, Px, PF_B8G8R8A8);
			if (CloudMid && MapTex)
			{
				MapTex->SRGB = false;
				MapTex->Filter = TF_Bilinear;
				MapTex->AddressX = TA_Clamp;
				MapTex->AddressY = TA_Clamp;
				MapTex->NeverStream = true;
				MapTex->UpdateResource();
				CloudMid->SetTextureParameterValue(FName(TEXT("WeatherMap")), MapTex);
				// (by the property, as the guard does: the setter isn't exported from the engine in every version)
				if (FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Cloud->GetClass()->FindPropertyByName(FName(TEXT("Material")))))
				{
					Prop->SetObjectPropertyValue_InContainer(Cloud, CloudMid);
					Cloud->MarkRenderStateDirty();
				}
				UE_LOG(LogTemp, Log, TEXT("Torque sky: weather map on the clouds (%d x %d over %.0f km)"), Px, Px, Looks.MapSizeM / 1000.0);
			}
			else
				CloudMid = nullptr;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Torque sky: the cloud material has no weather map (an older level): one cover everywhere"));
		}
	}
	if (!CloudMid || !MapTex) return;
	// Painted again every so often or after the camera has moved; the camera's offset from the map's
	// centre every frame (small numbers for the material).
	const double Now = W->GetTimeSeconds();
	if (Now - MapPaintedAt > Looks.MapRepaintS || FVector::Dist2D(Cam, MapCentre) > Looks.MapMoveM * 100.0)
	{
		const TorqueSky::WeatherMap M = TorqueSky::PaintWeatherMap(Grids, Lat, Lon, Looks);
		if (M.Valid() && M.Px == MapTex->GetSizeX())
		{
			MapPaintedAt = Now;
			MapCentre = Cam;
			const int32 Bytes = M.Px * M.Px * 4;
			uint8* Data = new uint8[Bytes];
			FMemory::Memcpy(Data, M.Bgra.data(), Bytes);
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, M.Px, M.Px);
			MapTex->UpdateTextureRegions(0, 1, Region, (uint32)(M.Px * 4), 4, Data,
				[](uint8* Src, const FUpdateTextureRegion2D* Regions) { delete[] Src; delete Regions; });
			SetMpc(TEXT("MapSize"), (float)M.SizeM);
			SetMpc(TEXT("MapHereLow"), (float)M.HereLow);
			SetMpc(TEXT("MapHereMid"), (float)M.HereMid);
		}
	}
	SetMpc(TEXT("MapX"), (float)((Cam.X - MapCentre.X) / 100.0));
	SetMpc(TEXT("MapY"), (float)((Cam.Y - MapCentre.Y) / 100.0));
	const float On = (float)FMath::Clamp(Looks.MapGain, 0.0, 2.0);
	if (On != MapOnApplied) SetMpc(TEXT("MapOn"), On);
	MapOnApplied = On;
}

void ATorqueSky::TickRainShafts(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	const ATorqueGameState* GS = Game.Get();
	if (!W || !GS || Shafts.Num() == 0 || !bApplied) return;
	FVector Cam;
	double Lat = 0.0, Lon = 0.0, OriginAsl = 0.0;
	if (!CameraGeo(Cam, Lat, Lon, OriginAsl)) return;
	const double Now = W->GetTimeSeconds();
	// Where they stand, every few seconds (they drift with their showers between).
	if (Now - ShaftsFoundAt > 4.0)
	{
		ShaftsFoundAt = Now;
		const TorqueSky::MapGrids Grids = GS->SkyGrids();
		const bool bOn = CloudsDrawn() && Last.Low.Cover > 0.02;
		const std::vector<TorqueSky::RainShaft> Found = bOn ? TorqueSky::FindRainShafts(Grids, Lat, Lon, Looks) : std::vector<TorqueSky::RainShaft>();
		for (FShaftSlot& Slot : Shafts) Slot.bSeen = false;
		const double Ground = GroundAslNow - 300.0;   // (down into the dales round about; it fades into the land)
		const double TopZ = (Last.Low.BaseAsl - OriginAsl) * 100.0 + 20000.0;   // (up into the base)
		const double BottomZ = (Ground - OriginAsl) * 100.0;
		for (const TorqueSky::RainShaft& R : Found)
		{
			// (the same shower keeps its shaft: matched by where it stands, within a cell)
			const FVector At(Cam.X + R.East * 100.0, Cam.Y - R.North * 100.0, 0.0);
			FShaftSlot* Use = nullptr;
			for (FShaftSlot& Slot : Shafts)
				if (!Slot.bSeen && Slot.Want > 0.0f && FVector::Dist2D(Slot.At, At) < Looks.ShaftSpacingM * 50.0) { Use = &Slot; break; }
			if (!Use)
				for (FShaftSlot& Slot : Shafts)
					if (!Slot.bSeen && Slot.Now < 0.01f) { Use = &Slot; break; }
			if (!Use) continue;
			Use->bSeen = true;
			Use->At = FVector(At.X, At.Y, 0.5 * (TopZ + BottomZ));
			Use->Scale = FVector(R.RadiusM * 2.0, R.RadiusM * 2.0, FMath::Max(TopZ - BottomZ, 10000.0) / 100.0);
			Use->Want = (float)(R.Strength * Looks.ShaftOpacity);
			Use->White = (float)R.White;
		}
		for (FShaftSlot& Slot : Shafts)
			if (!Slot.bSeen) Slot.Want = 0.0f;
	}
	// Faded in and out over some seconds, never popping.
	for (FShaftSlot& Slot : Shafts)
	{
		AActor* A = Slot.Actor.Get();
		if (!A) continue;
		Slot.Now = FMath::FInterpConstantTo(Slot.Now, Slot.Want, DeltaSeconds, 0.08f);
		const bool bShow = Slot.Now > 0.002f;
		if (A->IsHidden() == bShow) A->SetActorHiddenInGame(!bShow);
		if (!bShow) continue;
		if (!Slot.Mid)
			if (UStaticMeshComponent* Mesh = A->FindComponentByClass<UStaticMeshComponent>())
				Slot.Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0);
		if (Slot.Mid)
		{
			Slot.Mid->SetScalarParameterValue(FName(TEXT("Strength")), Slot.Now);
			Slot.Mid->SetScalarParameterValue(FName(TEXT("White")), Slot.White);
		}
		A->SetActorLocationAndRotation(Slot.At, FRotator::ZeroRotator);
		A->SetActorScale3D(Slot.Scale);
	}
}

void ATorqueSky::SetSkyClock(double UnixSeconds, double LatDeg, double LonDeg)
{
	bSkyClock = true;
	SkyUnix = UnixSeconds;
	SkyLat = LatDeg;
	SkyLon = LonDeg;
	MoonNow = TorqueNight::MoonAt(UnixSeconds, LatDeg, LonDeg);
	double SunE = 0.0, SunB = 0.0;
	TorqueNight::HorizonOf(MoonNow.SunRaDeg, MoonNow.SunDecDeg, UnixSeconds, LatDeg, LonDeg, SunE, SunB);
	auto Enu = [](double Elev, double Bear) {
		const double E = FMath::DegreesToRadians(Elev), B = FMath::DegreesToRadians(Bear);
		return FVector(FMath::Cos(E) * FMath::Sin(B), FMath::Cos(E) * FMath::Cos(B), FMath::Sin(E));
	};
	SunEnu = Enu(SunE, SunB);
	UWorld* W = GetWorld();
	if (!W || !SkyMpc) return;
	const FVector MoonEnu = Enu(MoonNow.Elevation, MoonNow.Bearing);
	auto SetMpc = [W, this](const TCHAR* Name, float Value) {
		if (SkyMpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Name), Value);
	};
	SetMpc(TEXT("Lst"), (float)TorqueNight::SiderealDeg(UnixSeconds, LonDeg));
	SetMpc(TEXT("Lat"), (float)LatDeg);
	SetMpc(TEXT("MoonE"), (float)MoonEnu.X);
	SetMpc(TEXT("MoonN"), (float)MoonEnu.Y);
	SetMpc(TEXT("MoonU"), (float)MoonEnu.Z);
	SetMpc(TEXT("SunE"), (float)SunEnu.X);
	SetMpc(TEXT("SunN"), (float)SunEnu.Y);
	SetMpc(TEXT("SunU"), (float)SunEnu.Z);
}

bool ATorqueSky::MoonLightsNow(double SunElevation, double& MoonElevation, double& MoonBearing) const
{
	MoonElevation = MoonNow.Elevation;
	MoonBearing = MoonNow.Bearing;
	return bSkyClock && SunElevation < Looks.MoonTakesOverDeg && MoonNow.Elevation > 0.5;
}

void ATorqueSky::TickStarDome()
{
	UWorld* W = GetWorld();
	AActor* Dome = StarDomePtr.Get();
	if (!W || !Dome || !SkyMpc || !bApplied) return;
	// Shown at night, and by day while the moon is up (its disc in the blue).
	const bool bShow = bSkyClock && (Last.Stars > 0.001 || Last.Glow > 0.001 || (Last.MoonDisc > 0.0 && MoonNow.Elevation > -1.0)
		|| Last.Aurora > 0.001 || Last.Noctilucent > 0.001);
	if (Dome->IsHidden() == bShow) Dome->SetActorHiddenInGame(!bShow);
	if (!bShow) return;
	const APlayerController* PC = W->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager) Dome->SetActorLocation(PC->PlayerCameraManager->GetCameraLocation());
}

// --- Stage 7: lightning and thunder, meteors ------------------------------------------------------------

double ATorqueSky::StormClock() const
{
	const UWorld* W = GetWorld();
	const ATorqueGameState* GS = Game.Get();
	return GS ? GS->GetServerWorldTimeSeconds() : W ? W->GetTimeSeconds() : 0.0;
}

void ATorqueSky::TickLightning(float DeltaSeconds)
{
	UWorld* W = GetWorld();
	const ATorqueGameState* GS = Game.Get();
	if (!W || !GS || !bApplied) return;
	FVector Cam;
	double Lat = 0.0, Lon = 0.0, OriginAsl = 0.0;
	if (!CameraGeo(Cam, Lat, Lon, OriginAsl)) return;
	const double Clock = StormClock();
	const double Now = W->GetTimeSeconds();
	// The storm cells round about, every few seconds (they drift with their showers between).
	if (Now - CellsFoundAt > 5.0)
	{
		CellsFoundAt = Now;
		const size_t Before = StormCellsNow.size();
		StormCellsNow = TorqueStorm::StormCells(GS->SkyGrids(), WeatherNow, Lat, Lon, Looks);
		if ((Before == 0) != StormCellsNow.empty())
		{
			UE_LOG(LogTemp, Log, TEXT("Torque sky: %d thundery storm cells within %.0f km"), int32(StormCellsNow.size()), Looks.StormFarthestM / 1000.0);
		}
	}
	// (joining, or after a hitch: carry on from now, no burst of flashes all at once)
	if (StrikeClock < 0.0 || Clock < StrikeClock || Clock - StrikeClock > 2.0) StrikeClock = Clock;
	const float Scale = SunLux > 0.0f ? SunLux / 4700.0f : 1.0f;   // (the level's sun against the one it was built with)
	if (!StormCellsNow.empty())
		for (const TorqueStorm::Strike& St : TorqueStorm::StrikesBetween(StormCellsNow, StrikeClock, Clock, Looks))
		{
			double E = 0.0, N = 0.0;
			TorqueWeather::MetresBetween(Lat, Lon, St.Lat, St.Lon, E, N);
			FFlash F;
			F.S = St;
			F.Where = FVector(Cam.X + E * 100.0, Cam.Y - N * 100.0, (St.BaseAsl - OriginAsl) * 100.0);
			const double Up = St.BaseAsl - (OriginAsl + Cam.Z / 100.0);
			F.Dist = FMath::Sqrt(E * E + N * N + Up * Up);
			if (Flashes.Num() < 8) Flashes.Add(F);
			++FlashCount;
			// Its thunder, on its way at the speed of sound (a flash in the cloud rumbles softer).
			const float Loud = (float)(TorqueStorm::ThunderLoudness(F.Dist, St.Power, Looks) * (St.bGround ? 1.0 : 0.7));
			if (Loud > 0.02f && ThunderDue.Num() < 12)
			{
				FThunderDue Due;
				Due.At = St.At + TorqueStorm::ThunderDelay(F.Dist, Looks);
				Due.Dist = F.Dist;
				Due.Loud = Loud;
				Due.Seed = St.Seed;
				ThunderDue.Add(Due);
			}
			// A fork to the ground near enough to see: one of the bolts stood up there.
			if (St.bGround && FMath::Sqrt(E * E + N * N) < Looks.BoltFarthestM)
				for (FBoltSlot& Slot : Bolts)
					if (!Slot.bOn && Slot.Actor.IsValid())
					{
						Slot.bOn = true;
						Slot.S = St;
						Slot.Top = F.Where;
						Slot.Ground = FVector(F.Where.X, F.Where.Y, (GroundAslNow - 80.0 - OriginAsl) * 100.0);
						break;
					}
		}
	StrikeClock = Clock;
	// The light now: the cloud lit round the brightest flash, and the land by the nearest bright one.
	double Glow = 0.0, Land = 0.0;
	const FFlash* GlowAt = nullptr;
	const FFlash* LandAt = nullptr;
	for (const FFlash& F : Flashes)
	{
		const double B = TorqueStorm::FlashAt(F.S, Clock - F.S.At) * F.S.Power;
		if (B > Glow) { Glow = B; GlowAt = &F; }
		const double Near = B / FMath::Max(FMath::Square(F.Dist / 3000.0), 1.0);
		if (Near > Land) { Land = Near; LandAt = &F; }
	}
	auto SetMpc = [W, this](const TCHAR* Name, float Value) {
		if (SkyMpc && SkyMpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Name), Value);
	};
	SetMpc(TEXT("Flash"), (float)(Glow * Looks.FlashCloudGlow) * Scale);
	if (GlowAt)
	{
		SetMpc(TEXT("FlashX"), (float)((GlowAt->Where.X - Cam.X) / 100.0));
		SetMpc(TEXT("FlashY"), (float)((GlowAt->Where.Y - Cam.Y) / 100.0));
		SetMpc(TEXT("FlashR"), (float)Looks.FlashRadiusM);
	}
	if (UPointLightComponent* L = FlashLight.Get())
	{
		const bool bLit = LandAt && Land > 0.005;
		if (L->IsVisible() != bLit) L->SetVisibility(bLit);
		if (bLit)
		{
			const double Peak = TorqueStorm::FlashAt(LandAt->S, Clock - LandAt->S.At) * LandAt->S.Power;
			L->SetWorldLocation(LandAt->Where);
			L->SetIntensity((float)(Looks.FlashLightCd * Peak * SunLux / 75000.0));
		}
	}
	Flashes.RemoveAll([Clock](const FFlash& F) { return Clock - F.S.At > 0.8; });
	// The forks: stood up between the cloud base and the ground, facing the camera, flickering with their flash.
	for (FBoltSlot& Slot : Bolts)
	{
		AActor* A = Slot.Actor.Get();
		if (!A) continue;
		const double Age = Clock - Slot.S.At;
		if (Slot.bOn && Age > 0.7) Slot.bOn = false;
		const bool bShow = Slot.bOn;
		if (A->IsHidden() == bShow) A->SetActorHiddenInGame(!bShow);
		if (!bShow) continue;
		if (!Slot.Mid)
			if (UStaticMeshComponent* Mesh = A->FindComponentByClass<UStaticMeshComponent>())
				Slot.Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0);
		const FVector Mid = (Slot.Top + Slot.Ground) * 0.5;
		const double HeightM = FMath::Max((Slot.Top.Z - Slot.Ground.Z) / 100.0, 100.0);
		const FVector ToCam = Cam - Mid;
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToCam.Y, ToCam.X));
		A->SetActorLocationAndRotation(Mid, FRotator(90.0, Yaw, 0.0));
		A->SetActorScale3D(FVector(HeightM, Looks.BoltWidthM, 1.0));
		if (Slot.Mid)
		{
			Slot.Mid->SetScalarParameterValue(FName(TEXT("Seed")), (float)(Slot.S.Seed % 100000u));
			Slot.Mid->SetScalarParameterValue(FName(TEXT("Strength")), (float)(TorqueStorm::FlashAt(Slot.S, Age) * Slot.S.Power * Looks.BoltNits) * Scale);
		}
	}
	// The thunder, as it arrives; and those that have rolled away, stopped.
	for (int32 I = ThunderDue.Num() - 1; I >= 0; --I)
		if (ThunderDue[I].At <= Clock)
		{
			PlayThunder(ThunderDue[I]);
			ThunderDue.RemoveAt(I);
		}
	for (int32 I = ThunderOn.Num() - 1; I >= 0; --I)
		if (Now >= ThunderOn[I].StopAt || !ThunderOn[I].Audio.IsValid())
		{
			if (UAudioComponent* Au = ThunderOn[I].Audio.Get()) Au->Stop();
			ThunderOn.RemoveAt(I);
		}
}

void ATorqueSky::PlayThunder(const FThunderDue& Due)
{
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_DedicatedServer) return;
	// (three rolling at once is a storm overhead already: a quiet fourth isn't missed)
	if (ThunderOn.Num() >= 3 && Due.Loud < 0.4f) return;
	const int32 Rate = 22050;
	const std::vector<int16_t> Pcm = TorqueStorm::ThunderSound(Due.Dist, 1.0, Due.Seed, Rate);
	if (Pcm.empty()) return;
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(Rate);
	Wave->NumChannels = 1;
	Wave->Duration = (float)Pcm.size() / (float)Rate;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.data()), (int32)(Pcm.size() * sizeof(int16_t)));
	// (muffled a little indoors: the shed's walls)
	const float Volume = Due.Loud * FMath::Lerp(0.45f, 1.0f, FMath::Clamp(OpenNow, 0.0f, 1.0f));
	UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(this, Wave, Volume);
	ThunderWaves.Add(Wave);
	if (ThunderWaves.Num() > 6) ThunderWaves.RemoveAt(0);
	FThunderOn On;
	On.Audio = Audio;
	On.StopAt = W->GetTimeSeconds() + Wave->Duration + 0.5;
	ThunderOn.Add(On);
	++ThunderCount;
	UE_LOG(LogTemp, Log, TEXT("Torque sky: thunder from %.1f km (%.1f s after its flash), loudness %.2f"), Due.Dist / 1000.0,
		TorqueStorm::ThunderDelay(Due.Dist, Looks), Due.Loud);
}

void ATorqueSky::TickMeteors()
{
	UWorld* W = GetWorld();
	if (!W || !SkyMpc || !bApplied) return;
	const TorqueStorm::Meteor M = TorqueStorm::MeteorAt(StormClock(), Last.MeteorsPerHour);
	auto SetMpc = [W, this](const TCHAR* Name, float Value) {
		if (SkyMpc->GetScalarParameterByName(FName(Name))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(Name), Value);
	};
	SetMpc(TEXT("Meteor"), M.bOn ? (float)(M.Bright * Looks.MeteorGain) : 0.0f);
	if (!M.bOn) return;
	SetMpc(TEXT("MeteorEl"), (float)M.El);
	SetMpc(TEXT("MeteorAz"), (float)M.Az);
	SetMpc(TEXT("MeteorDir"), (float)M.Dir);
	SetMpc(TEXT("MeteorLen"), (float)M.Length);
	SetMpc(TEXT("MeteorAlong"), (float)M.Along);
	// (the dome shows while there are stars; a meteor needs them anyway)
}

int32 ATorqueSky::ShaftsShown() const
{
	int32 N = 0;
	for (const FShaftSlot& Slot : Shafts) N += Slot.Now > 0.002f ? 1 : 0;
	return N;
}

void ATorqueSky::TickRainbow()
{
	UWorld* W = GetWorld();
	AActor* Bow = RainbowPtr.Get();
	if (!W || !Bow || !SkyMpc || !bApplied) return;
	const float Strength = (float)(Last.Rainbow * Looks.RainbowOpacity);
	const bool bShow = Strength > 0.005f || Last.Fogbow > 0.005 || Last.Glory > 0.005;   // (stage 7: the same sphere draws them)
	if (Bow->IsHidden() == bShow) Bow->SetActorHiddenInGame(!bShow);
	if (FMath::Abs(Strength - RainbowApplied) > 0.002f || (Strength > 0.005f) != (RainbowApplied > 0.005f))
	{
		if (SkyMpc->GetScalarParameterByName(FName(TEXT("Rainbow")))) UKismetMaterialLibrary::SetScalarParameterValue(W, SkyMpc, FName(TEXT("Rainbow")), Strength);
		RainbowApplied = Strength;
	}
	if (!bShow) return;
	const APlayerController* PC = W->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager) Bow->SetActorLocation(PC->PlayerCameraManager->GetCameraLocation());
}

void ATorqueSky::TickHighSheet()
{
	UWorld* W = GetWorld();
	AActor* Sheet = HighSheetPtr.Get();
	if (!W || !Sheet || !bApplied) return;
	// Hidden on a sky without high cloud, or where the sky has no MPC_Sky to draw it by.
	const bool bShow = SkyMpc && Last.HighShown > 0.005;
	if (Sheet->IsHidden() == bShow) Sheet->SetActorHiddenInGame(!bShow);
	if (!bShow) return;
	// Over the camera, at the high cloud's height above sea level.
	const APlayerController* PC = W->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	double OriginAsl = 0.0;
	if (const ATorqueGameState* GS = Game.Get())
	{
		double Lat = 0.0, Lon = 0.0;
		GS->GeoAt(FVector::ZeroVector, Lat, Lon, OriginAsl);
	}
	const FVector At(Cam.X, Cam.Y, (Last.High.BaseAsl - OriginAsl) * 100.0);
	if (FVector::DistSquared(At, Sheet->GetActorLocation()) > 100.0 * 100.0) Sheet->SetActorLocation(At);
}

void ATorqueSky::TickMist()
{
	UWorld* W = GetWorld();
	if (!W || !bApplied) return;
	const APlayerController* PC = W->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;
	// How much: the valley mist, or fog lying with a top (thicker); none, and the pool hides.
	const bool bFogTop = Last.FogTopAsl >= 0.0;
	const float Amount = (float)FMath::Max(Last.Mist, bFogTop ? 1.0 : 0.0);
	const double TopAsl = bFogTop ? FMath::Max(Last.FogTopAsl, Last.MistTopAsl) : Last.MistTopAsl;
	if (Amount < 0.02f && MistApplied <= 0.0f) return;
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const double Now = W->GetTimeSeconds();
	const TorqueSky::Looks& L = Looks;
	if (MistPool.Num() == 0)
	{
		UClass* FogClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.LocalFogVolume"));
		if (!FogClass)
		{
			MistApplied = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("Torque sky: no Local Fog Volume in this engine: no valley mist"));
			return;
		}
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < 12; ++I)
			if (AActor* A = W->SpawnActor<AActor>(FogClass, FTransform(FVector(0.0, 0.0, -1e7)), P))
			{
				A->SetActorHiddenInGame(true);
				MistPool.Add(A);
			}
		// The volume's own size at scale 1 (its sphere's radius, cm), to scale it to the hollows.
		if (MistPool.Num() > 0 && MistPool[0].IsValid())
		{
			FVector Origin, Extent;
			MistPool[0]->GetActorBounds(false, Origin, Extent, true);
			MistBaseRadius = FMath::Max(Extent.X, 1.0);
		}
		UE_LOG(LogTemp, Log, TEXT("Torque sky: valley mist pool of %d (radius %.0f cm at scale 1)"), MistPool.Num(), MistBaseRadius);
	}
	// Re-laid when the camera has moved 1.5 km, or every minute.
	const bool bPlace = FVector::Dist2D(Cam, MistPlacedAt) > 150000.0 || Now - MistPlacedTime > 60.0;
	double OriginAsl = 0.0;
	if (const ATorqueGameState* GS = Game.Get())
	{
		double Lat = 0.0, Lon = 0.0;
		GS->GeoAt(FVector::ZeroVector, Lat, Lon, OriginAsl);
	}
	const double TopZ = (TopAsl - OriginAsl) * 100.0;
	if (bPlace && Amount >= 0.02f)
	{
		MistPlacedAt = Cam;
		MistPlacedTime = Now;
		// The ground on a 13 x 13 lattice 400 m apart round the camera; hollows are points below the fog's
		// (or mist's) top, the lowest first, no two within 700 m.
		struct FSpot { FVector At; double Below; };
		TArray<FSpot> Spots;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(TorqueMist), false);
		if (APawn* Pawn = PC->GetPawn()) Q.AddIgnoredActor(Pawn);
		for (int32 X = -6; X <= 6; ++X)
			for (int32 Y = -6; Y <= 6; ++Y)
			{
				const FVector From(Cam.X + X * 40000.0, Cam.Y + Y * 40000.0, Cam.Z + 300000.0);
				FHitResult Hit;
				if (!W->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, 800000.0), ECC_Visibility, Q)) continue;
				const double Below = TopZ - Hit.ImpactPoint.Z;
				if (Below > 300.0) Spots.Add({Hit.ImpactPoint, Below});
			}
		Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Below > B.Below; });
		TArray<FVector> Chosen;
		for (const FSpot& Spot : Spots)
		{
			if (Chosen.Num() >= MistPool.Num()) break;
			bool bNear = false;
			for (const FVector& C : Chosen) bNear = bNear || FVector::Dist2D(C, Spot.At) < 70000.0;
			if (!bNear) Chosen.Add(Spot.At);
		}
		const double Radius = 45000.0;   // each a lens of mist 900 m across
		for (int32 I = 0; I < MistPool.Num(); ++I)
		{
			AActor* A = MistPool[I].Get();
			if (!A) continue;
			if (I >= Chosen.Num())
			{
				A->SetActorHiddenInGame(true);
				continue;
			}
			// Centred on the ground, its top at the fog's top: a flattened lens, half of it below ground.
			const double Depth = FMath::Max(TopZ - Chosen[I].Z, 500.0);
			const double Base = FMath::Max(MistBaseRadius, 1.0);
			A->SetActorLocation(FVector(Chosen[I].X, Chosen[I].Y, Chosen[I].Z));
			A->SetActorScale3D(FVector(Radius / Base, Radius / Base, Depth / Base));
			A->SetActorHiddenInGame(false);
		}
		UE_LOG(LogTemp, Log, TEXT("Torque sky: valley mist %.2f%s, top %.0f m: %d hollows"), Amount, bFogTop ? TEXT(" (fog lying with a top)") : TEXT(""), TopAsl, Chosen.Num());
	}
	// How thick (the engine's radial fog, by name: tunable in sky_looks.json as mist_extinction).
	const float Thick = Amount < 0.02f ? 0.0f : Amount * (float)L.MistExtinction * (bFogTop ? 3.0f : 1.0f);
	if (FMath::Abs(Thick - MistApplied) > 0.005f)
	{
		MistApplied = Thick;
		for (const TWeakObjectPtr<AActor>& Ptr : MistPool)
			if (AActor* A = Ptr.Get())
			{
				if (Thick <= 0.0f) A->SetActorHiddenInGame(true);
				if (UActorComponent* C = A->GetRootComponent())
				{
					SetFloatProperty(C, TEXT("RadialFogExtinction"), Thick);
					SetFloatProperty(C, TEXT("HeightFogExtinction"), 0.0f);
					C->MarkRenderStateDirty();
				}
			}
	}
}

FString ATorqueSky::Describe() const
{
	const TorqueSky::Sky& S = Last;
	return FString::Printf(TEXT("sun %.1f deg (light at %.1f), block %.2f, dull %.2f, %.0f lux (EV100 %.1f; clear sky %.0f), day light %.2f, sun x%.2f, sky light x%.2f (shade x%.2f), exposure %+.2f (may open %.1f), mie x%.2f, rayleigh x%.2f, fog x%.2f; cloud %.0f%% (drawn %.2f, shape %.2f, density %.2f) %.0f-%.0f m, drifting %.1f m/s E %.1f m/s N, its shadows %.2f; clouds %s"),
		S.SunElevation, S.LightElevation, S.Block, S.Dull, S.GroundLux, TorqueSky::Ev100(S.GroundLux), S.ClearLux, S.DayLight, S.SunScale, S.SkyLightScale, S.SkyFill,
		S.ExposureStops, S.ExposureOpenStops, S.MieScale, S.RayleighScale, S.FogScale, S.Cover * 100.0, S.CoverShown, S.Shape, S.Density, S.BaseAsl, S.TopAsl, S.DriftE, S.DriftN,
		S.CloudShadows, CloudsDrawn() ? TEXT("drawn") : TEXT("not drawn"))
		+ FString::Printf(TEXT("; kinds: low %s, middle %s, high %s (veil %.2f, halo %.2f); bands low %.2f-%.2f middle %.2f-%.2f"),
			UTF8_TO_TCHAR(TorqueSky::KindName(S.Dominant(0))), UTF8_TO_TCHAR(TorqueSky::KindName(S.Dominant(1))), UTF8_TO_TCHAR(TorqueSky::KindName(S.Dominant(2))),
			S.HighSheet, S.Halo, S.LowDrawn.Bottom, S.LowDrawn.Top, S.MidDrawn.Bottom, S.MidDrawn.Top)
		+ FString::Printf(TEXT("; mist %.2f (top %.0f m), fog top %.0f m, rainbow %.2f, under the base %.2f; weather map %s, rain shafts %d"),
			S.Mist, S.MistTopAsl, S.FogTopAsl, S.Rainbow, S.BaseFog, MapOnApplied > 0.0f ? TEXT("on") : TEXT("off"), ShaftsShown())
		+ FString::Printf(TEXT("; moon %.1f deg up, bearing %.0f, %.0f%% lit%s, light x%.5f; stars %.2f, Milky Way %.2f, glow %.3f"),
			MoonNow.Elevation, MoonNow.Bearing, MoonNow.Lit * 100.0, S.MoonTakeover > 0.5 ? TEXT(" (lighting the night)") : TEXT(""), S.MoonLight, S.Stars, S.MilkyWay, S.Glow)
		+ FString::Printf(TEXT("; storms: thunder %.2f, anvil %.2f, hail %.2f, snow %.2f, %d storm cells near, %d flashes and %d thunder so far"),
			S.Thunder, S.Anvil, S.Hail, S.Snowing, int32(StormCellsNow.size()), FlashCount, ThunderCount)
		+ FString::Printf(TEXT("; rare: sun dogs %.2f, contrails %.2f, fogbow %.2f, glory %.2f, aurora %.2f (top %.0f deg, bearing %.0f), noctilucent %.2f, meteors %.0f an hour, dust %.2f"),
			S.SunDogs, S.Contrails, S.Fogbow, S.Glory, S.Aurora, S.AuroraTopDeg, S.AuroraBearing, S.Noctilucent, S.MeteorsPerHour, S.Dust);
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
		Shot.Pitch = J.Num("pitch", 4.0);
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
	FRotator Rot((float)Shot.Pitch, YawOf(Shot.Yaw), 0.0f);
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
