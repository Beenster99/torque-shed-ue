#include "TorqueInfoPanel.h"

#include "Coop/TorqueUi.h"
#include "Coop/TorqueGameState.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueKeys.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "TorqueBikePawn.h"
#include "Weather/WeatherModel.h"
#include "World/TorqueWorldSubsystem.h"
#include "World/WorldRide.h"

#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TorqueInfo
{
// The build, as the title screen shows it but shorter: "Build 141 (3a5f1c2)".
static const FString& BuildLine()
{
	static FString Line;
	static bool bRead = false;
	if (bRead) return Line;
	bRead = true;
	Line = TEXT("Dev build");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/build.json")))) return Line;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return Line;
	int32 Round = 0;
	FString Commit;
	Root->TryGetNumberField(TEXT("round"), Round);
	Root->TryGetStringField(TEXT("commit"), Commit);
	if (Round > 0) Line = FString::Printf(TEXT("Build %d"), Round);
	if (!Commit.IsEmpty()) Line += TEXT("  (") + Commit.Left(7) + TEXT(")");
	return Line;
}

static void Get(const FString& Url, TFunction<void(bool, const FString&)> Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	// (OpenStreetMap's and Wikipedia's rules ask every app to name itself.)
	Req->SetHeader(TEXT("User-Agent"), TEXT("TorqueShedUE/0.1 (free hobby motorcycle game)"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->SetTimeout(15.0f);
	Req->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bConnected)
	{
		const bool bOk = bConnected && Resp.IsValid() && EHttpResponseCodes::IsOk(Resp->GetResponseCode());
		Done(bOk, Resp.IsValid() ? Resp->GetContentAsString() : FString());
	});
	Req->ProcessRequest();
}

static TSharedPtr<FJsonObject> ParseJson(const FString& Body)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Root)) return nullptr;
	return Root;
}

static double ArrNum(const TArray<TSharedPtr<FJsonValue>>* Arr, int32 I, double Else = 0.0)
{
	if (!Arr || !Arr->IsValidIndex(I) || !(*Arr)[I].IsValid()) return Else;
	double V = Else;
	return (*Arr)[I]->TryGetNumber(V) ? V : Else;
}

static FString ArrStr(const TArray<TSharedPtr<FJsonValue>>* Arr, int32 I)
{
	FString V;
	if (Arr && Arr->IsValidIndex(I) && (*Arr)[I].IsValid()) (*Arr)[I]->TryGetString(V);
	return V;
}

// "2026-11-12T17:00" (Open-Meteo's local times) as a date and time.
static bool ParseLocal(const FString& T, FDateTime& Out)
{
	if (T.Len() < 13) return false;
	const int32 Y = FCString::Atoi(*T.Mid(0, 4)), Mo = FCString::Atoi(*T.Mid(5, 2)), D = FCString::Atoi(*T.Mid(8, 2)), H = FCString::Atoi(*T.Mid(11, 2));
	const int32 Mi = T.Len() >= 16 ? FCString::Atoi(*T.Mid(14, 2)) : 0;
	if (!FDateTime::Validate(Y, Mo, D, H, Mi, 0, 0)) return false;
	Out = FDateTime(Y, Mo, D, H, Mi);
	return true;
}

static const TCHAR* DayName(EDayOfWeek D)
{
	switch (D)
	{
	case EDayOfWeek::Monday: return TEXT("Monday");
	case EDayOfWeek::Tuesday: return TEXT("Tuesday");
	case EDayOfWeek::Wednesday: return TEXT("Wednesday");
	case EDayOfWeek::Thursday: return TEXT("Thursday");
	case EDayOfWeek::Friday: return TEXT("Friday");
	case EDayOfWeek::Saturday: return TEXT("Saturday");
	default: return TEXT("Sunday");
	}
}

static const TCHAR* MonthName(int32 M)
{
	static const TCHAR* Names[12] = {TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"), TEXT("June"), TEXT("July"),
		TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
	return Names[FMath::Clamp(M, 1, 12) - 1];
}

static FString Capital(const FString& In)
{
	return In.IsEmpty() ? In : In.Left(1).ToUpper() + In.Mid(1);
}

// What an icon draws, from a WMO weather code (and the cloud cover, for the weather now).
enum class EIcon : uint8 { Clear, Partly, Cloudy, Fog, Drizzle, Rain, Heavy, Snow, Thunder };

static EIcon IconOf(int32 Code, double Cloud = -1.0)
{
	if (Code >= 95) return EIcon::Thunder;
	if ((Code >= 71 && Code <= 77) || Code == 85 || Code == 86) return EIcon::Snow;
	if (Code == 65 || Code == 67 || Code == 82) return EIcon::Heavy;
	if ((Code >= 61 && Code <= 66) || Code == 80 || Code == 81) return EIcon::Rain;
	if (Code >= 51 && Code <= 57) return EIcon::Drizzle;
	if (Code == 45 || Code == 48) return EIcon::Fog;
	if (Code == 3) return EIcon::Cloudy;
	if (Code == 2) return EIcon::Partly;
	if (Cloud >= 0.0) return Cloud > 0.8 ? EIcon::Cloudy : Cloud > 0.3 ? EIcon::Partly : EIcon::Clear;
	return Code == 1 ? EIcon::Partly : EIcon::Clear;
}

// The weather's icons, drawn on the canvas so they stay crisp at any size: Sz the box, CX, CY its middle.
struct FIconPen
{
	UCanvas* Canvas;
	float CX, CY, Sz;
	void Line(float X0, float Y0, float X1, float Y1, float T, const FLinearColor& C) const
	{
		Canvas->K2_DrawLine(FVector2D(CX + X0 * Sz, CY + Y0 * Sz), FVector2D(CX + X1 * Sz, CY + Y1 * Sz), FMath::Max(1.0f, T * Sz), C);
	}
	void Disc(float X, float Y, float R, const FLinearColor& C) const
	{
		TorqueUi::Disc(Canvas, CX + X * Sz, CY + Y * Sz, R * Sz, C, 28);
	}
	void Sun(float X, float Y, float K) const
	{
		const FLinearColor Gold(1.0f, 0.78f, 0.25f, 1.0f);
		for (int32 I = 0; I < 8; ++I)
		{
			const float A = I * PI / 4.0f + PI / 8.0f;
			Line(X + FMath::Cos(A) * 0.33f * K, Y + FMath::Sin(A) * 0.33f * K, X + FMath::Cos(A) * 0.45f * K, Y + FMath::Sin(A) * 0.45f * K, 0.055f * K, Gold);
		}
		Disc(X, Y, 0.24f * K, Gold);
	}
	void Moon(float X, float Y, float K) const
	{
		// A crescent: quads between the outer edge and an inner curve (each convex, for the fan).
		const FLinearColor Pale(0.93f, 0.9f, 0.76f, 1.0f);
		const int32 Seg = 16;
		const float R = 0.25f * K;
		for (int32 I = 0; I < Seg; ++I)
		{
			const float A0 = -PI * 0.5f + PI * I / Seg, A1 = -PI * 0.5f + PI * (I + 1) / Seg;
			TArray<FVector2D> Q;
			Q.Add(FVector2D(CX + (X - FMath::Cos(A0) * R) * Sz, CY + (Y + FMath::Sin(A0) * R) * Sz));
			Q.Add(FVector2D(CX + (X - FMath::Cos(A1) * R) * Sz, CY + (Y + FMath::Sin(A1) * R) * Sz));
			Q.Add(FVector2D(CX + (X - FMath::Cos(A1) * R * 0.3f) * Sz, CY + (Y + FMath::Sin(A1) * R) * Sz));
			Q.Add(FVector2D(CX + (X - FMath::Cos(A0) * R * 0.3f) * Sz, CY + (Y + FMath::Sin(A0) * R) * Sz));
			TorqueUi::Polygon(Canvas, Q, Pale);
		}
	}
	void Cloud(float X, float Y, float K, const FLinearColor& C) const
	{
		Disc(X - 0.17f * K, Y + 0.04f * K, 0.14f * K, C);
		Disc(X + 0.01f * K, Y - 0.06f * K, 0.2f * K, C);
		Disc(X + 0.19f * K, Y + 0.05f * K, 0.13f * K, C);
		TorqueUi::Rect(Canvas, CX + (X - 0.17f * K) * Sz, CY + (Y + 0.04f * K) * Sz, 0.36f * K * Sz, 0.14f * K * Sz, C);
	}
	void Draw(EIcon Icon, bool bDay) const
	{
		const FLinearColor White(0.9f, 0.92f, 0.95f, 1.0f), Grey(0.66f, 0.7f, 0.76f, 1.0f), Dark(0.5f, 0.53f, 0.6f, 1.0f);
		const FLinearColor Blue(0.45f, 0.72f, 1.0f, 1.0f);
		auto Lamp = [this, bDay](float X, float Y, float K) { if (bDay) Sun(X, Y, K); else Moon(X, Y, K); };
		switch (Icon)
		{
		case EIcon::Clear: Lamp(0.0f, 0.0f, 1.0f); break;
		case EIcon::Partly:
			Lamp(-0.12f, -0.12f, 0.8f);
			Cloud(0.06f, 0.1f, 0.85f, White);
			break;
		case EIcon::Cloudy:
			Cloud(0.1f, -0.06f, 0.7f, Grey);
			Cloud(-0.03f, 0.06f, 0.95f, White);
			break;
		case EIcon::Fog:
			Cloud(0.0f, -0.12f, 0.8f, Grey);
			for (int32 I = 0; I < 3; ++I) Line(-0.3f + 0.05f * (I % 2), 0.1f + 0.1f * I, 0.3f - 0.05f * (I % 2), 0.1f + 0.1f * I, 0.05f, Grey);
			break;
		case EIcon::Drizzle:
			Cloud(0.0f, -0.12f, 0.95f, White);
			for (int32 I = 0; I < 3; ++I)
			{
				Disc(-0.15f + 0.15f * I, 0.2f, 0.03f, Blue);
				Disc(-0.08f + 0.15f * I, 0.33f, 0.03f, Blue);
			}
			break;
		case EIcon::Rain:
		case EIcon::Heavy:
		{
			const int32 N = Icon == EIcon::Heavy ? 4 : 3;
			Cloud(0.0f, -0.12f, 0.95f, Icon == EIcon::Heavy ? Grey : White);
			for (int32 I = 0; I < N; ++I)
			{
				const float X = -0.16f + 0.32f * I / FMath::Max(1, N - 1) + 0.04f;
				Line(X, 0.15f, X - 0.06f, 0.36f, 0.045f, Blue);
			}
			break;
		}
		case EIcon::Snow:
			Cloud(0.0f, -0.12f, 0.95f, White);
			for (int32 I = 0; I < 3; ++I)
			{
				const float X = -0.15f + 0.15f * I, Y = 0.22f + (I % 2) * 0.1f;
				for (int32 K = 0; K < 3; ++K)
				{
					const float A = K * PI / 3.0f;
					Line(X - FMath::Cos(A) * 0.05f, Y - FMath::Sin(A) * 0.05f, X + FMath::Cos(A) * 0.05f, Y + FMath::Sin(A) * 0.05f, 0.025f, White);
				}
			}
			break;
		case EIcon::Thunder:
		{
			Cloud(0.0f, -0.12f, 0.95f, Dark);
			const FLinearColor Bolt(1.0f, 0.84f, 0.2f, 1.0f);
			TArray<FVector2D> A, B;
			auto P = [this](float X, float Y) { return FVector2D(CX + X * Sz, CY + Y * Sz); };
			A.Add(P(0.02f, 0.08f)); A.Add(P(-0.1f, 0.26f)); A.Add(P(0.02f, 0.24f));
			B.Add(P(0.06f, 0.2f)); B.Add(P(-0.03f, 0.22f)); B.Add(P(-0.07f, 0.42f));
			TorqueUi::Polygon(Canvas, A, Bolt);
			TorqueUi::Polygon(Canvas, B, Bolt);
			break;
		}
		}
	}
};

// Where a point of this level is on Earth, and how high (m above sea level).
static void GeoOf(const UWorld* World, const FVector& At, double& Lat, double& Lon, double& Asl)
{
	Lat = 54.3535;
	Lon = -2.2058;
	Asl = 0.0;
	if (!World) return;
	// A Ride UK ride: the ride's own frame (cm, x east, y south; heights above sea level).
	if (World->GetMapName().Contains(TEXT("RideUK")))
		if (const UGameInstance* GI = World->GetGameInstance())
			if (const UTorqueWorldSubsystem* Real = GI->GetSubsystem<UTorqueWorldSubsystem>())
				if (const std::shared_ptr<TorqueWorld::RidePlan> Plan = Real->Ride())
				{
					const TorqueWorld::Geo G = Plan->Area.Roads.FrameOf().ToGeo(At.X / 100.0, At.Y / 100.0);
					Lat = G.Lat;
					Lon = G.Lon;
					Asl = At.Z / 100.0;
					return;
				}
	if (const ATorqueGameState* GS = World->GetGameState<ATorqueGameState>()) GS->GeoAt(At, Lat, Lon, Asl);
}
} // namespace TorqueInfo

using namespace TorqueInfo;

FTorqueInfoPanel::FTorqueInfoPanel()
	: Data(MakeShared<FData>())
{
}

void FTorqueInfoPanel::Ask(double Lat, double Lon, double Now)
{
	const FTorqueSettings& Set = FTorqueSettings::Get();
	TWeakPtr<FData> Weak = Data;
	FData& D = *Data;
	auto Far = [Lat, Lon](double FromLat, double FromLon)
	{
		double East = 0.0, North = 0.0;
		TorqueWeather::MetresBetween(FromLat, FromLon, Lat, Lon, East, North);
		return FMath::Sqrt(East * East + North * North);
	};
	// The forecast: with live weather, or when the place lookups are on. Every half hour, or when
	// you've ridden 15 km from where it was asked for.
	if ((Set.bPlaceLookup || Set.WeatherMode == FTorqueSettings::WeatherLive) && !D.bForecastBusy)
	{
		const double Since = Now - D.ForecastAsked;
		if (Since >= 1800.0 || (Since >= 60.0 && Far(D.ForecastLat, D.ForecastLon) > 15000.0) || (D.bForecastFailed && Since >= 120.0))
		{
			D.bForecastBusy = true;
			D.ForecastAsked = Now;
			D.ForecastLat = Lat;
			D.ForecastLon = Lon;
			const FString Url = FString::Printf(TEXT("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f")
				TEXT("&hourly=temperature_2m,weather_code,precipitation_probability,is_day")
				TEXT("&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset")
				TEXT("&timezone=auto&forecast_days=4"), Lat, Lon);
			TorqueInfo::Get(Url, [Weak](bool bOk, const FString& Body)
			{
				const TSharedPtr<FData> Got = Weak.Pin();
				if (!Got) return;
				Got->bForecastBusy = false;
				const TSharedPtr<FJsonObject> Root = bOk ? ParseJson(Body) : nullptr;
				const TSharedPtr<FJsonObject>* Hourly = nullptr;
				const TSharedPtr<FJsonObject>* Daily = nullptr;
				if (!Root || !Root->TryGetObjectField(TEXT("hourly"), Hourly) || !Root->TryGetObjectField(TEXT("daily"), Daily))
				{
					Got->bForecastFailed = true;
					UE_LOG(LogTemp, Log, TEXT("Torque info: forecast failed (%s)"), bOk ? TEXT("not understood") : TEXT("no answer"));
					return;
				}
				Got->bForecastFailed = false;
				const double Offset = Root->GetNumberField(TEXT("utc_offset_seconds"));
				const FDateTime LocalNow = FDateTime::UtcNow() + FTimespan::FromSeconds(Offset);
				const TArray<TSharedPtr<FJsonValue>>* HTime = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* HTemp = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* HCode = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* HRain = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* HDay = nullptr;
				(*Hourly)->TryGetArrayField(TEXT("time"), HTime);
				(*Hourly)->TryGetArrayField(TEXT("temperature_2m"), HTemp);
				(*Hourly)->TryGetArrayField(TEXT("weather_code"), HCode);
				(*Hourly)->TryGetArrayField(TEXT("precipitation_probability"), HRain);
				(*Hourly)->TryGetArrayField(TEXT("is_day"), HDay);
				Got->Hours.Reset();
				if (HTime)
				{
					// From the next whole hour, every two hours: the next twelve.
					int32 First = INDEX_NONE;
					for (int32 I = 0; I < HTime->Num() && First == INDEX_NONE; ++I)
					{
						FDateTime T;
						if (ParseLocal(ArrStr(HTime, I), T) && T > LocalNow) First = I;
					}
					for (int32 I = First; First != INDEX_NONE && I < HTime->Num() && Got->Hours.Num() < 6; I += 2)
					{
						FHour H;
						H.Label = ArrStr(HTime, I).Mid(11, 5);
						H.Temp = (float)ArrNum(HTemp, I);
						H.Code = (int32)ArrNum(HCode, I);
						H.RainPct = (int32)ArrNum(HRain, I);
						H.bDay = ArrNum(HDay, I, 1.0) > 0.5;
						Got->Hours.Add(H);
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* DTime = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DCode = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DMax = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DMin = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DRain = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DRise = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* DSet = nullptr;
				(*Daily)->TryGetArrayField(TEXT("time"), DTime);
				(*Daily)->TryGetArrayField(TEXT("weather_code"), DCode);
				(*Daily)->TryGetArrayField(TEXT("temperature_2m_max"), DMax);
				(*Daily)->TryGetArrayField(TEXT("temperature_2m_min"), DMin);
				(*Daily)->TryGetArrayField(TEXT("precipitation_probability_max"), DRain);
				(*Daily)->TryGetArrayField(TEXT("sunrise"), DRise);
				(*Daily)->TryGetArrayField(TEXT("sunset"), DSet);
				Got->Days.Reset();
				for (int32 I = 0; DTime && I < DTime->Num() && Got->Days.Num() < 4; ++I)
				{
					FDay Day;
					FDateTime T;
					const bool bDate = ParseLocal(ArrStr(DTime, I) + TEXT("T12:00"), T);
					Day.Label = I == 0 ? FString(TEXT("Today")) : bDate ? FString(DayName(T.GetDayOfWeek())).Left(3) : ArrStr(DTime, I);
					Day.Code = (int32)ArrNum(DCode, I);
					Day.Max = (float)ArrNum(DMax, I);
					Day.Min = (float)ArrNum(DMin, I);
					Day.RainPct = (int32)ArrNum(DRain, I);
					Got->Days.Add(Day);
				}
				Got->Sunrise = ArrStr(DRise, 0).Mid(11, 5);
				Got->Sunset = ArrStr(DSet, 0).Mid(11, 5);
				UE_LOG(LogTemp, Log, TEXT("Torque info: forecast %d hours, %d days, sunrise %s, sunset %s"), Got->Hours.Num(), Got->Days.Num(), *Got->Sunrise, *Got->Sunset);
			});
		}
	}
	if (!Set.bPlaceLookup) return;
	// The road and place: OpenStreetMap's Nominatim, no more than every 15 s and only after 300 m
	// (its rules: at most one a second, and cache what you can).
	if (!D.bPlaceBusy)
	{
		const double Since = Now - D.PlaceAsked;
		const bool bFirst = D.PlaceAsked < -999.0;
		if (Since >= 15.0 && (bFirst || Far(D.PlaceLat, D.PlaceLon) > 300.0 || (D.bPlaceFailed && Since >= 60.0)))
		{
			D.bPlaceBusy = true;
			D.PlaceAsked = Now;
			D.PlaceLat = Lat;
			D.PlaceLon = Lon;
			const FString Url = FString::Printf(TEXT("https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=%.5f&lon=%.5f&zoom=17&addressdetails=1&accept-language=en"), Lat, Lon);
			TorqueInfo::Get(Url, [Weak](bool bOk, const FString& Body)
			{
				const TSharedPtr<FData> Got = Weak.Pin();
				if (!Got) return;
				Got->bPlaceBusy = false;
				const TSharedPtr<FJsonObject> Root = bOk ? ParseJson(Body) : nullptr;
				const TSharedPtr<FJsonObject>* Address = nullptr;
				if (!Root || !Root->TryGetObjectField(TEXT("address"), Address))
				{
					Got->bPlaceFailed = true;
					UE_LOG(LogTemp, Log, TEXT("Torque info: place failed (%s)"), bOk ? TEXT("nothing there") : TEXT("no answer"));
					return;
				}
				Got->bPlaceFailed = false;
				auto First = [Address](std::initializer_list<const TCHAR*> Keys)
				{
					FString Found;
					for (const TCHAR* Key : Keys)
						if ((*Address)->TryGetStringField(Key, Found) && !Found.IsEmpty()) return Found;
					return FString();
				};
				Got->Road = First({TEXT("road"), TEXT("pedestrian"), TEXT("track"), TEXT("path")});
				Got->Place = First({TEXT("hamlet"), TEXT("village"), TEXT("suburb"), TEXT("town"), TEXT("city"), TEXT("municipality"), TEXT("locality")});
				Got->Area = First({TEXT("county"), TEXT("state_district"), TEXT("state"), TEXT("region")});
				Got->Country = First({TEXT("country")});
				if (Got->Place.IsEmpty())
				{
					FString Name;
					if (Root->TryGetStringField(TEXT("name"), Name) && Name != Got->Road) Got->Place = Name;
				}
				UE_LOG(LogTemp, Log, TEXT("Torque info: place %s, %s, %s, %s"), *Got->Road, *Got->Place, *Got->Area, *Got->Country);
			});
		}
	}
	// About this place: Wikipedia's articles within 10 km, after 1.5 km or 2 minutes (when moved).
	if (!D.bWikiBusy)
	{
		const double Since = Now - D.WikiAsked;
		const bool bFirst = D.WikiAsked < -999.0;
		const double Moved = Far(D.WikiLat, D.WikiLon);
		if (Since >= 20.0 && (bFirst || Moved > 1500.0 || (Since >= 120.0 && Moved > 300.0) || (D.bWikiFailed && Since >= 120.0)))
		{
			D.bWikiBusy = true;
			D.WikiAsked = Now;
			D.WikiLat = Lat;
			D.WikiLon = Lon;
			const FString Url = FString::Printf(TEXT("https://en.wikipedia.org/w/api.php?action=query&format=json&generator=geosearch")
				TEXT("&ggscoord=%.5f%%7C%.5f&ggsradius=10000&ggslimit=8&prop=extracts%%7Ccoordinates&exintro=1&explaintext=1&exsentences=3&exlimit=8&colimit=8"),
				Lat, Lon);
			const double AskLat = Lat, AskLon = Lon;
			TorqueInfo::Get(Url, [Weak, AskLat, AskLon](bool bOk, const FString& Body)
			{
				const TSharedPtr<FData> Got = Weak.Pin();
				if (!Got) return;
				Got->bWikiBusy = false;
				const TSharedPtr<FJsonObject> Root = bOk ? ParseJson(Body) : nullptr;
				if (!Root)
				{
					Got->bWikiFailed = true;
					UE_LOG(LogTemp, Log, TEXT("Torque info: Wikipedia failed (no answer)"));
					return;
				}
				Got->bWikiFailed = false;
				Got->Near.Reset();
				const TSharedPtr<FJsonObject>* Query = nullptr;
				const TSharedPtr<FJsonObject>* Pages = nullptr;
				if (Root->TryGetObjectField(TEXT("query"), Query) && (*Query)->TryGetObjectField(TEXT("pages"), Pages))
				{
					for (const auto& KV : (*Pages)->Values)
					{
						const TSharedPtr<FJsonObject> Page = KV.Value.IsValid() ? KV.Value->AsObject() : nullptr;
						if (!Page) continue;
						FNear N;
						Page->TryGetStringField(TEXT("title"), N.Title);
						Page->TryGetStringField(TEXT("extract"), N.Text);
						N.Text.TrimStartAndEndInline();
						const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
						N.Metres = 1.0e9;
						if (Page->TryGetArrayField(TEXT("coordinates"), Coords) && Coords->Num() > 0 && (*Coords)[0].IsValid())
						{
							const TSharedPtr<FJsonObject> C = (*Coords)[0]->AsObject();
							if (C)
							{
								double East = 0.0, North = 0.0;
								TorqueWeather::MetresBetween(AskLat, AskLon, C->GetNumberField(TEXT("lat")), C->GetNumberField(TEXT("lon")), East, North);
								N.Metres = FMath::Sqrt(East * East + North * North);
							}
						}
						if (!N.Title.IsEmpty()) Got->Near.Add(N);
					}
					// The nearest with words first, then the rest by distance.
					Got->Near.Sort([](const FNear& A, const FNear& B) { return A.Metres < B.Metres; });
					const int32 Worded = Got->Near.IndexOfByPredicate([](const FNear& N) { return N.Text.Len() > 40; });
					if (Worded > 0)
					{
						const FNear Lead = Got->Near[Worded];
						Got->Near.RemoveAt(Worded);
						Got->Near.Insert(Lead, 0);
					}
				}
				UE_LOG(LogTemp, Log, TEXT("Torque info: Wikipedia %d near, first %s"), Got->Near.Num(), Got->Near.Num() > 0 ? *Got->Near[0].Title : TEXT("(none)"));
			});
		}
	}
}

void FTorqueInfoPanel::Draw(AHUD* Hud, UCanvas* Canvas, const ATorqueBikePawn* Bike, float TopY, float Dt)
{
	if (!Hud || !Canvas || !Bike) return;
	UWorld* World = Hud->GetWorld();
	if (!World) return;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	static const bool bAsked = FParse::Param(FCommandLine::Get(), TEXT("TorqueInfoPanel"));
	const bool bTestRun = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings"));
	// Test runs keep a clean screen (and ask nothing of the internet) unless -TorqueInfoPanel.
	const bool bWant = Set.bInfoPanel && (!bTestRun || bAsked);
	Slide = FMath::FInterpConstantTo(Slide, bWant ? 1.0f : 0.0f, Dt, 4.0f);   // a quarter of a second in or out
	if (Slide <= 0.0f) return;
	const float Ease = 1.0f - FMath::Pow(1.0f - Slide, 3.0f);

	const float S = TorqueUi::Scale(Canvas);
	const float PW = 380.0f * S, Pad = 14.0f * S, CW = PW - 2.0f * Pad - 3.0f * S;
	const float X0 = 20.0f * S - (1.0f - Ease) * (PW + 40.0f * S), TX = X0 + 3.0f * S + Pad;
	const float Want = FMath::Max(TopY, 90.0f * S);
	ShownTop = ShownTop < 0.0f ? Want : FMath::FInterpTo(ShownTop, Want, Dt, 6.0f);
	const float Top = ShownTop;

	// Where we are, from the camera.
	const APlayerController* PC = Hud->GetOwningPlayerController();
	FVector Eye = Bike->GetActorLocation();
	FRotator EyeRot = Bike->GetActorRotation();
	if (PC) PC->GetPlayerViewPoint(Eye, EyeRot);
	double Lat = 0.0, Lon = 0.0, Asl = 0.0;
	GeoOf(World, Eye, Lat, Lon, Asl);
	const double Now = FPlatformTime::Seconds();
	if (!bTestRun || bAsked) Ask(Lat, Lon, Now);
	const FData& D = *Data;
	const ATorqueGameState* GS = World->GetGameState<ATorqueGameState>();
	const bool bImperial = Set.Units == 1;

	// Each part: how high it is, how to draw it (its top at Y), and how much it matters (the least
	// go first when the screen's too short for them all).
	struct FPart { float H; int32 Keep; TFunction<void(float)> Paint; };
	TArray<FPart> Parts;
	const FLinearColor Ink = TorqueUi::Ink, Dim = TorqueUi::Dim, Faint = TorqueUi::Faint, Accent = TorqueUi::Accent;
	const FLinearColor RainBlue(0.5f, 0.75f, 1.0f, 1.0f);
	auto Heading = [Canvas, S, TX, CW, Accent, Faint](const FString& Title, const FString& Right, float Y)
	{
		TorqueUi::Text(Canvas, Title, TX, Y, 11.5f * S, Accent, true);
		if (!Right.IsEmpty()) TorqueUi::TextFit(Canvas, Right, TX + CW, Y + 0.5f * S, 11.0f * S, CW * 0.62f, Faint, false, TorqueUi::EAlign::Right);
		TorqueUi::Rect(Canvas, TX, Y + 16.0f * S, CW, 1.0f * S, TorqueUi::Line);
	};
	const float HeadH = 22.0f * S, Gap = 12.0f * S;

	// 1. The date and the time (the game's clock), and the build.
	{
		const float Hour = GS ? (GS->GetHour() >= 0.0f ? GS->GetHour() : GS->BuiltHour()) : -1.0f;
		const FString Clock = Hour >= 0.0f ? ATorqueGameState::ClockText(Hour) : FDateTime::Now().ToString(TEXT("%H:%M"));
		const FDateTime Today = FDateTime::Now();
		const FString Date = FString::Printf(TEXT("%s %d %s %d"), DayName(Today.GetDayOfWeek()), Today.GetDay(), MonthName(Today.GetMonth()), Today.GetYear());
		Parts.Add({52.0f * S, 100, [=](float Y)
		{
			const float CWide = TorqueUi::Text(Canvas, Clock, TX, Y - 4.0f * S, 34.0f * S, Ink, true);
			TorqueUi::TextFit(Canvas, Date, TX + CWide + 12.0f * S, Y + 1.0f * S, 15.0f * S, CW - CWide - 12.0f * S, Ink);
			TorqueUi::TextFit(Canvas, BuildLine(), TX + CWide + 12.0f * S, Y + 22.0f * S, 12.0f * S, CW - CWide - 12.0f * S, Dim);
		}});
	}

	// 2. The weather here, now.
	if (GS)
	{
		const TorqueWeather::State W = GS->GetWeatherState();
		const ETorqueWeatherSource Src = GS->GetWeatherSource();
		const FString SrcText = Src == ETorqueWeatherSource::Live ? FString(TEXT("live"))
			: Src == ETorqueWeatherSource::LiveLastKnown ? FString(TEXT("live, last known")) : FString(TEXT("set in Settings"));
		const bool bDay = GS->GetDayLight() > 0.3f;
		const float Air = (float)W.Air;
		const double Per = bImperial ? 2.23694 : 3.6;
		const TCHAR* Unit = bImperial ? TEXT("mph") : TEXT("km/h");
		TArray<FString> Lines;
		if (W.Wind < 0.5) Lines.Add(TEXT("Still air"));
		else
		{
			const FString From(UTF8_TO_TCHAR(TorqueWeather::Compass(W.WindFrom).c_str()));
			FString Wind = FString::Printf(TEXT("Wind %s %d %s"), *From, FMath::RoundToInt(W.Wind * Per), Unit);
			if (W.Gust > W.Wind * 1.3 && W.Gust - W.Wind > 3.0) Wind += FString::Printf(TEXT(", gusts %d"), FMath::RoundToInt(W.Gust * Per));
			Lines.Add(Wind);
		}
		// How cold it feels riding: the wind chill (Environment Canada's) at the bike's speed plus the wind.
		const double RideKmh = Bike->SpeedMs() * 3.6;
		if (RideKmh > 15.0 && Air <= 18.0f)
		{
			const double V = FMath::Pow(RideKmh + W.Wind * 3.6 * 0.5, 0.16);
			const double Chill = FMath::Min((double)Air, 13.12 + 0.6215 * Air - 11.37 * V + 0.3965 * Air * V);
			Lines.Add(FString::Printf(TEXT("Feels like %d°C at %s"), FMath::RoundToInt(Chill), *Set.SpeedText(Bike->SpeedMs())));
		}
		FString Road = W.RoadWet < 0.08 ? TEXT("dry") : W.RoadWet < 0.35 ? TEXT("damp") : W.RoadWet < 0.7 ? TEXT("wet") : TEXT("soaked");
		if (W.Puddles > 0.4) Road += TEXT(", puddles");
		Lines.Add(W.Rain > 0.05 ? FString::Printf(TEXT("Rain %.1f mm/h, road %s"), W.Rain, *Road) : FString::Printf(TEXT("Road %s"), *Road));
		if (W.Visibility < 5000.0)
			Lines.Add(W.Visibility < 1000.0 ? FString::Printf(TEXT("Visibility %d m"), FMath::RoundToInt(W.Visibility / 10.0) * 10)
				: FString::Printf(TEXT("Visibility %.1f km"), W.Visibility / 1000.0));
		const FString Words = Capital(UTF8_TO_TCHAR(TorqueWeather::CodeWords(W.Code).c_str()));
		const EIcon Icon = IconOf(W.Code, W.Cloud);
		const float LineH = 17.0f * S;
		const float H = HeadH + FMath::Max(64.0f * S, 52.0f * S + Lines.Num() * LineH) + 4.0f * S;
		Parts.Add({H, 90, [=](float Y)
		{
			Heading(TEXT("WEATHER"), SrcText, Y);
			const float IY = Y + HeadH;
			FIconPen{Canvas, TX + 32.0f * S, IY + 32.0f * S, 64.0f * S}.Draw(Icon, bDay);
			const float RX = TX + 76.0f * S;
			const float TW = TorqueUi::Text(Canvas, FString::Printf(TEXT("%d°C"), FMath::RoundToInt(Air)), RX, IY - 2.0f * S, 30.0f * S, Ink, true);
			TorqueUi::TextFit(Canvas, Words, RX + TW + 10.0f * S, IY + 10.0f * S, 15.0f * S, CW - 76.0f * S - TW - 10.0f * S, Ink);
			float LY = IY + 40.0f * S;
			for (const FString& L : Lines)
			{
				TorqueUi::TextFit(Canvas, L, RX, LY, 13.5f * S, CW - 76.0f * S, L.StartsWith(TEXT("Feels")) ? RainBlue : Dim);
				LY += LineH;
			}
		}});
	}

	// 3. The forecast for the real place (Open-Meteo).
	{
		const bool bLiveGame = GS && (GS->GetWeatherSource() == ETorqueWeatherSource::Live || GS->GetWeatherSource() == ETorqueWeatherSource::LiveLastKnown);
		const FString Sun = D.Sunrise.IsEmpty() ? FString() : FString::Printf(TEXT("sunrise %s  ·  sunset %s"), *D.Sunrise, *D.Sunset);
		const TArray<FHour> Hours = D.Hours;
		const TArray<FDay> Days = D.Days;
		const bool bAsking = Set.bPlaceLookup || Set.WeatherMode == FTorqueSettings::WeatherLive;
		const FString Waiting = !bAsking ? FString(TEXT("Turn on live weather or \"about this place\" in Settings for a forecast"))
			: D.bForecastFailed ? FString(TEXT("No forecast: no answer from the internet")) : FString(TEXT("Asking for the forecast..."));
		if (Hours.Num() == 0 && Days.Num() == 0)
			Parts.Add({HeadH + 20.0f * S, 40, [=](float Y)
			{
				Heading(TEXT("FORECAST"), FString(), Y);
				TorqueUi::TextFit(Canvas, Waiting, TX, Y + HeadH, 12.5f * S, CW, Faint);
			}});
		else
		{
			const float StripH = Hours.Num() > 0 ? 78.0f * S : 0.0f;
			const float RowH = 21.0f * S;
			const float NoteH = bLiveGame ? 0.0f : 17.0f * S;
			Parts.Add({HeadH + StripH + NoteH + 4.0f * S, 60, [=](float Y)
			{
				Heading(TEXT("FORECAST"), Sun, Y);
				float FY = Y + HeadH;
				const float ColW = CW / FMath::Max(1, Hours.Num());
				for (int32 I = 0; I < Hours.Num(); ++I)
				{
					const FHour& Hr = Hours[I];
					const float MX = TX + ColW * (I + 0.5f);
					TorqueUi::Text(Canvas, Hr.Label, MX, FY, 11.5f * S, Dim, false, TorqueUi::EAlign::Centre);
					FIconPen{Canvas, MX, FY + 31.0f * S, 30.0f * S}.Draw(IconOf(Hr.Code), Hr.bDay);
					TorqueUi::Text(Canvas, FString::Printf(TEXT("%d°"), FMath::RoundToInt(Hr.Temp)), MX, FY + 47.0f * S, 13.5f * S, Ink, true, TorqueUi::EAlign::Centre);
					if (Hr.RainPct >= 10)
						TorqueUi::Text(Canvas, FString::Printf(TEXT("%d%%"), Hr.RainPct), MX, FY + 63.0f * S, 11.0f * S, RainBlue, false, TorqueUi::EAlign::Centre);
				}
				FY += StripH;
				if (!bLiveGame) TorqueUi::TextFit(Canvas, TEXT("(The real place's: the game's weather is set in Settings)"), TX, FY, 11.5f * S, CW, Faint);
			}});
			if (Days.Num() > 0)
				Parts.Add({Days.Num() * RowH + 4.0f * S, 30, [=](float Y)
				{
					float FY = Y;
					for (const FDay& Dy : Days)
					{
						TorqueUi::Text(Canvas, Dy.Label, TX, FY + 2.0f * S, 13.0f * S, Ink);
						FIconPen{Canvas, TX + 82.0f * S, FY + 10.0f * S, 20.0f * S}.Draw(IconOf(Dy.Code), true);
						TorqueUi::Text(Canvas, FString::Printf(TEXT("%d°"), FMath::RoundToInt(Dy.Max)), TX + 104.0f * S, FY + 2.0f * S, 13.0f * S, Ink, true);
						TorqueUi::Text(Canvas, FString::Printf(TEXT("%d°"), FMath::RoundToInt(Dy.Min)), TX + 140.0f * S, FY + 2.0f * S, 13.0f * S, Dim);
						TorqueUi::TextFit(Canvas, Capital(UTF8_TO_TCHAR(TorqueWeather::CodeWords(Dy.Code).c_str())), TX + 176.0f * S, FY + 3.0f * S, 12.0f * S,
							CW - 176.0f * S - 44.0f * S, Dim);
						if (Dy.RainPct >= 10)
							TorqueUi::Text(Canvas, FString::Printf(TEXT("%d%%"), Dy.RainPct), TX + CW, FY + 3.0f * S, 12.0f * S, RainBlue, false, TorqueUi::EAlign::Right);
						FY += RowH;
					}
				}});
		}
	}

	// 4. Where you are.
	{
		const double Bearing = FMath::Fmod(90.0 + EyeRot.Yaw + 720.0, 360.0);   // (x east, y south: yaw 0 faces east)
		const FString Heads = UTF8_TO_TCHAR(TorqueWeather::Compass(Bearing).c_str());
		const FString Height = bImperial ? FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Asl * 3.28084)) : FString::Printf(TEXT("%d m"), FMath::RoundToInt(Asl));
		const FString Coords = FString::Printf(TEXT("%.4f° %s, %.4f° %s  ·  %s  ·  heading %s"), FMath::Abs(Lat), Lat >= 0.0 ? TEXT("N") : TEXT("S"),
			FMath::Abs(Lon), Lon >= 0.0 ? TEXT("E") : TEXT("W"), *Height, *Heads);
		FString RoadLine = D.Road, PlaceLine;
		if (!D.Place.IsEmpty()) PlaceLine = D.Place;
		if (!D.Area.IsEmpty()) PlaceLine += (PlaceLine.IsEmpty() ? TEXT("") : TEXT(", ")) + D.Area;
		if (!D.Country.IsEmpty() && D.Country != TEXT("United Kingdom")) PlaceLine += (PlaceLine.IsEmpty() ? TEXT("") : TEXT(", ")) + D.Country;
		if (!Set.bPlaceLookup) PlaceLine = TEXT("(Names: turn on \"about this place\" in Settings)");
		else if (RoadLine.IsEmpty() && PlaceLine.IsEmpty())
			PlaceLine = D.bPlaceFailed ? TEXT("No place names: no answer from the internet") : D.bPlaceBusy || D.PlaceAsked < -999.0 ? TEXT("Finding where this is...") : TEXT("Off the mapped roads");
		const float H = HeadH + (RoadLine.IsEmpty() ? 0.0f : 20.0f * S) + 19.0f * S + 17.0f * S + 4.0f * S;
		Parts.Add({H, 80, [=](float Y)
		{
			Heading(TEXT("WHERE YOU ARE"), FString(), Y);
			float LY = Y + HeadH;
			if (!RoadLine.IsEmpty())
			{
				TorqueUi::TextFit(Canvas, RoadLine, TX, LY, 16.0f * S, CW, Ink, true);
				LY += 20.0f * S;
			}
			TorqueUi::TextFit(Canvas, PlaceLine, TX, LY, 14.0f * S, CW, Set.bPlaceLookup ? Ink : Faint);
			LY += 19.0f * S;
			TorqueUi::TextFit(Canvas, Coords, TX, LY, 12.0f * S, CW, Dim);
		}});
	}

	// 5. About this place (Wikipedia).
	if (Set.bPlaceLookup && D.Near.Num() > 0)
	{
		const FNear Lead = D.Near[0];
		const float TextPx = 13.0f * S;
		TArray<FString> Wrapped = TorqueUi::WrapLines(Lead.Text, TextPx, CW);
		if (Wrapped.Num() > 5)
		{
			Wrapped.SetNum(5);
			Wrapped[4] += TEXT("...");
		}
		FString Also;
		for (int32 I = 1; I < D.Near.Num() && I < 5; ++I)
			Also += (Also.IsEmpty() ? TEXT("Also near: ") : TEXT(", ")) + FString::Printf(TEXT("%s (%s)"), *D.Near[I].Title, *Set.DistanceText(D.Near[I].Metres));
		TArray<FString> AlsoLines = TorqueUi::WrapLines(Also, 12.0f * S, CW);
		if (AlsoLines.Num() > 2) AlsoLines.SetNum(2);
		const FString Dist = Lead.Metres < 1.0e8 ? Set.DistanceText(Lead.Metres) + TEXT(" away") : FString();
		const float H = HeadH + 20.0f * S + Wrapped.Num() * TextPx * 1.3f + AlsoLines.Num() * 16.0f * S + 6.0f * S;
		Parts.Add({H, 20, [=](float Y)
		{
			Heading(TEXT("ABOUT THIS PLACE"), Dist, Y);
			float LY = Y + HeadH;
			TorqueUi::TextFit(Canvas, Lead.Title, TX, LY, 15.0f * S, CW, Ink, true);
			LY += 20.0f * S;
			for (const FString& L : Wrapped)
			{
				TorqueUi::Text(Canvas, L, TX, LY, TextPx, Dim);
				LY += TextPx * 1.3f;
			}
			LY += 2.0f * S;
			for (const FString& L : AlsoLines)
			{
				TorqueUi::Text(Canvas, L, TX, LY, 12.0f * S, Faint);
				LY += 16.0f * S;
			}
		}});
	}

	// 6. Who's riding: their profile name in their colour, their bike, speed, and how far from you.
	{
		struct FRider { FString Name, BikeName, Speed, Away; FLinearColor Colour; float Angle; bool bMe; };
		TArray<FRider> Riders;
		const UTorqueBuildSubsystem* Builds = UTorqueBuildSubsystem::Of(Bike);
		const FVector MyAt = Bike->GetActorLocation();
		const bool bNet = World->GetNetMode() != NM_Standalone;
		for (TActorIterator<ATorqueBikePawn> It(World); It; ++It)
		{
			const ATorqueBikePawn* B = *It;
			const bool bMe = B == Bike;
			const APlayerState* PS = B->GetPlayerState();
			if (!bMe && !PS) continue;   // a bike nobody rides
			FRider R;
			R.bMe = bMe;
			R.Name = bMe && !Set.Name.IsEmpty() ? Set.Name : PS ? PS->GetPlayerName() : FString(TEXT("You"));
			const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(PS);
			const FColor C = TPS ? TPS->ColourValue() : Set.RiderColour();
			R.Colour = (C.R + C.G + C.B) < 200 ? FLinearColor(0.85f, 0.85f, 0.85f, 1.0f) : FLinearColor(C);
			const int32 Bay = B->GetRideBay();
			R.BikeName = Builds && Bay >= 0 && Builds->Bike(Bay).bExists && !Builds->Bike(Bay).Name.IsEmpty() ? Builds->Bike(Bay).Name : FString(TEXT("Speed Twin"));
			R.Speed = B->IsOnFoot() ? FString(TEXT("on foot")) : B->RiderCrashed() ? FString(TEXT("off!")) : Set.SpeedText(B->SpeedMs());
			const FVector Delta = B->GetActorLocation() - MyAt;
			R.Away = bMe ? FString(TEXT("you")) : Set.DistanceText(Delta.Size2D() / 100.0);
			R.Angle = bMe ? 0.0f : FMath::DegreesToRadians((float)(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) - EyeRot.Yaw));
			Riders.Add(R);
		}
		Riders.Sort([](const FRider& A, const FRider& B) { return A.bMe && !B.bMe; });
		if (Riders.Num() > 8) Riders.SetNum(8);
		const float RowH = 38.0f * S;
		const FString Right = bNet ? FString::Printf(TEXT("%d riding"), Riders.Num()) : FString(TEXT("riding alone"));
		Parts.Add({HeadH + Riders.Num() * RowH + 2.0f * S, 85, [=](float Y)
		{
			Heading(TEXT("RIDERS"), Right, Y);
			float RY = Y + HeadH;
			for (const FRider& R : Riders)
			{
				TorqueUi::Disc(Canvas, TX + 6.0f * S, RY + 9.0f * S, 5.0f * S, R.Colour, 16);
				const float SpeedW = (float)TorqueUi::Measure(R.Speed, 14.0f * S, true).X;
				TorqueUi::TextFit(Canvas, R.bMe ? R.Name + TEXT("  (you)") : R.Name, TX + 18.0f * S, RY, 15.0f * S, CW - 18.0f * S - SpeedW - 10.0f * S, R.Colour, true);
				TorqueUi::Text(Canvas, R.Speed, TX + CW, RY + 1.0f * S, 14.0f * S, Ink, true, TorqueUi::EAlign::Right);
				const float AwayW = (float)TorqueUi::Measure(R.Away, 12.5f * S).X;
				TorqueUi::TextFit(Canvas, R.BikeName, TX + 18.0f * S, RY + 18.0f * S, 12.5f * S, CW - 18.0f * S - AwayW - 30.0f * S, Dim);
				TorqueUi::Text(Canvas, R.Away, TX + CW, RY + 18.0f * S, 12.5f * S, Dim, false, TorqueUi::EAlign::Right);
				if (!R.bMe)
				{
					// Which way they are from where you're looking: up is ahead.
					const float AX = TX + CW - AwayW - 12.0f * S, AY = RY + 25.0f * S, AR = 6.0f * S;
					const float A = R.Angle - PI * 0.5f;
					auto Pt = [AX, AY, A](float Fwd, float Side, float Rad)
					{
						const float CA = FMath::Cos(A), SA = FMath::Sin(A);
						return FVector2D(AX + (Fwd * CA - Side * SA) * Rad, AY + (Fwd * SA + Side * CA) * Rad);
					};
					TArray<FVector2D> Tri;
					Tri.Add(Pt(1.0f, 0.0f, AR));
					Tri.Add(Pt(-0.7f, 0.65f, AR));
					Tri.Add(Pt(-0.7f, -0.65f, AR));
					TorqueUi::Polygon(Canvas, Tri, Accent);
				}
				RY += RowH;
			}
		}});
	}

	// The credits the data's licences ask for.
	if (Set.bPlaceLookup)
		Parts.Add({16.0f * S, 10, [=](float Y)
		{
			TorqueUi::TextFit(Canvas, TEXT("Places © OpenStreetMap contributors  ·  Wikipedia (CC BY-SA)  ·  Forecast Open-Meteo.com"), TX, Y, 10.5f * S, CW, Faint);
		}});

	// Fit the screen: down to the bottom, less a margin; the least wanted parts go first.
	const float Room = Canvas->ClipY - Top - 24.0f * S;
	auto Total = [&Parts, Gap, Pad]()
	{
		float Sum = 2.0f * Pad;
		for (const FPart& P : Parts) Sum += P.H + Gap;
		return Sum - Gap;
	};
	while (Parts.Num() > 1 && Total() > Room)
	{
		int32 Least = 0;
		for (int32 I = 1; I < Parts.Num(); ++I)
			if (Parts[I].Keep < Parts[Least].Keep) Least = I;
		Parts.RemoveAt(Least);
	}
	const float PH = Total();
	TorqueUi::Rect(Canvas, X0, Top, PW, PH, FLinearColor(0.02f, 0.025f, 0.035f, 0.55f * Ease));
	TorqueUi::Rect(Canvas, X0, Top, 3.0f * S, PH, Accent);
	float Y = Top + Pad;
	for (const FPart& P : Parts)
	{
		P.Paint(Y);
		Y += P.H + Gap;
	}

	// Every 10 s, so a test run can tell it's there and what it shows.
	LogClock -= Dt;
	if (LogClock <= 0.0)
	{
		LogClock = 10.0;
		UE_LOG(LogTemp, Log, TEXT("Torque info: panel at %.4f, %.4f (%s, %s): %d parts, %d forecast hours, %d near, place lookup %s"),
			Lat, Lon, *D.Road, *D.Place, Parts.Num(), D.Hours.Num(), D.Near.Num(), Set.bPlaceLookup ? TEXT("on") : TEXT("off"));
	}
}
