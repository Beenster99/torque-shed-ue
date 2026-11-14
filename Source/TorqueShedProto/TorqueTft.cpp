#include "TorqueTft.h"

#include "Async/Async.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/TorqueUi.h"
#include "Dash/TftMapFill.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ConfigCacheIni.h"
#include "Sim/BikeSpec.h"

// The ride world's area and guide (World/WorldRoam.h) come with Ride UK's free roaming; without
// them the screens work and the map views say there's no map.
#if __has_include("World/WorldRoam.h")
#include "World/TorqueRideWorld.h"
#include "World/WorldRoam.h"
#define TORQUE_TFT_ROAM 1
#else
#define TORQUE_TFT_ROAM 0
#endif

namespace TorqueTftImpl
{
const TCHAR* Section = TEXT("TorqueTft");
// The HUD's map tile, in its own pixels.
const float TileW = 480.0f, TileH = 360.0f;
// A screen's glow (M_ShedScreenPage's Brightness): by night and in full day.
const float GlowNight = 3.0f, GlowDay = 14.0f;

// The map of the ride's area, shared by every bike's screen: made on a worker whenever the ride
// world publishes a new area. Game thread only.
std::shared_ptr<const TorqueTft::MapData> GMap;
std::shared_ptr<const void> GMapOf;   // the area GMap was made from
bool GMapBuilding = false;

void PollMap()
{
#if TORQUE_TFT_ROAM
	const std::shared_ptr<const TorqueWorld::RoamArea> Roam = ATorqueRideWorld::RoamNow();
	if (!Roam || !Roam->Area)
	{
		GMap.reset();
		GMapOf.reset();
		return;
	}
	if (GMapOf.get() == Roam.get() || GMapBuilding) return;
	GMapBuilding = true;
	Async(EAsyncExecution::ThreadPool, [Roam]()
	{
		std::shared_ptr<TorqueTft::MapData> Data = std::make_shared<TorqueTft::MapData>();
		TorqueTft::FillMap(*Data, Roam->Area->Roads);
		AsyncTask(ENamedThreads::GameThread, [Roam, Data]()
		{
			GMap = Data;
			GMapOf = Roam;
			GMapBuilding = false;
		});
	});
#else
	GMap.reset();
#endif
}

FLinearColor Colour(const TorqueTft::Rgba& C) { return FLinearColor(C.R, C.G, C.B, C.A); }

// The screen's pixel size for a tag's shape word.
bool ShapeOf(const FString& Word, TorqueTft::Shape& Out)
{
	if (Word == TEXT("wide")) Out = TorqueTft::Shape::Wide;
	else if (Word == TEXT("ultra")) Out = TorqueTft::Shape::Ultra;
	else if (Word == TEXT("round")) Out = TorqueTft::Shape::Round;
	else if (Word == TEXT("phone")) Out = TorqueTft::Shape::Phone;
	else return false;
	return true;
}

// A rider's colour on the map, by his place in the crew.
TorqueTft::Rgba CrewColour(int32 I)
{
	static const TorqueTft::Rgba Table[4] = {TorqueTft::Rgba(0.20f, 0.65f, 1.0f), TorqueTft::Rgba(1.0f, 0.45f, 0.75f),
		TorqueTft::Rgba(0.45f, 0.9f, 0.4f), TorqueTft::Rgba(1.0f, 0.8f, 0.2f)};
	return Table[((I % 4) + 4) % 4];
}
} // namespace TorqueTftImpl

namespace TTI = TorqueTftImpl;

// --- What the rider has set -------------------------------------------------------------------

FTorqueTftPrefs& FTorqueTftPrefs::Get()
{
	static FTorqueTftPrefs P;
	static bool bLoaded = false;
	if (!bLoaded && GConfig)
	{
		bLoaded = true;
		GConfig->GetInt(TTI::Section, TEXT("Design"), P.Design, GGameUserSettingsIni);
		GConfig->GetBool(TTI::Section, TEXT("NorthUp"), P.bNorthUp, GGameUserSettingsIni);
		GConfig->GetInt(TTI::Section, TEXT("Zoom"), P.Zoom, GGameUserSettingsIni);
		GConfig->GetBool(TTI::Section, TEXT("Overlay"), P.bOverlay, GGameUserSettingsIni);
		GConfig->GetBool(TTI::Section, TEXT("MapTile"), P.bMapTile, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("OverlayX"), P.OverlayX, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("OverlayY"), P.OverlayY, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("OverlayScale"), P.OverlayScale, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("TileX"), P.TileX, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("TileY"), P.TileY, GGameUserSettingsIni);
		GConfig->GetFloat(TTI::Section, TEXT("TileScale"), P.TileScale, GGameUserSettingsIni);
		GConfig->GetString(TTI::Section, TEXT("Mode"), P.Mode, GGameUserSettingsIni);
		// The modes' alterations in one line: "sport.traction=2,race.abs=1".
		FString Line;
		GConfig->GetString(TTI::Section, TEXT("Sets"), Line, GGameUserSettingsIni);
		TArray<FString> Bits;
		Line.ParseIntoArray(Bits, TEXT(","));
		for (const FString& Bit : Bits)
		{
			FString Key, Value;
			if (Bit.Split(TEXT("="), &Key, &Value)) P.Sets.Add(Key, FCString::Atoi(*Value));
		}
		P.Design = FMath::Clamp(P.Design, 0, (int32)TorqueTft::Design::Count - 1);
		P.Zoom = FMath::Clamp(P.Zoom, -2, 2);
		P.OverlayScale = FMath::Clamp(P.OverlayScale, 0.5f, 2.0f);
		P.TileScale = FMath::Clamp(P.TileScale, 0.5f, 2.0f);
	}
	return P;
}

void FTorqueTftPrefs::Save() const
{
	if (!GConfig) return;
	GConfig->SetInt(TTI::Section, TEXT("Design"), Design, GGameUserSettingsIni);
	GConfig->SetBool(TTI::Section, TEXT("NorthUp"), bNorthUp, GGameUserSettingsIni);
	GConfig->SetInt(TTI::Section, TEXT("Zoom"), Zoom, GGameUserSettingsIni);
	GConfig->SetBool(TTI::Section, TEXT("Overlay"), bOverlay, GGameUserSettingsIni);
	GConfig->SetBool(TTI::Section, TEXT("MapTile"), bMapTile, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("OverlayX"), OverlayX, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("OverlayY"), OverlayY, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("OverlayScale"), OverlayScale, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("TileX"), TileX, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("TileY"), TileY, GGameUserSettingsIni);
	GConfig->SetFloat(TTI::Section, TEXT("TileScale"), TileScale, GGameUserSettingsIni);
	GConfig->SetString(TTI::Section, TEXT("Mode"), *Mode, GGameUserSettingsIni);
	FString Line;
	for (const TPair<FString, int32>& KV : Sets)
	{
		if (!Line.IsEmpty()) Line += TEXT(",");
		Line += FString::Printf(TEXT("%s=%d"), *KV.Key, KV.Value);
	}
	GConfig->SetString(TTI::Section, TEXT("Sets"), *Line, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

// --- The bike's screens ------------------------------------------------------------------------

void UTorqueTftComponent::Hook(const TArray<UStaticMeshComponent*>& Meshes, const TArray<TorqueShedCatalogue::FDashGauge>& Gauges, const TorqueSim::BikeSpec& Spec)
{
	bTft = bPhone = false;
	ScreenFace.Reset();
	PhoneFace.Reset();
	ScreenRT = PhoneRT = nullptr;
	ScreenMat = PhoneMat = nullptr;
	if (!PageBase) PageBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedScreenPage.M_ShedScreenPage"));
	if (!PageBase)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedScreenPage (run Tools/ue_build_shed.py): the bike's screens stay dark"));
	}
	for (const TorqueShedCatalogue::FDashGauge& G : Gauges)
	{
		// "screen_wide_110x66": the shape, then the glass in millimetres.
		const FString What = G.What.ToString();
		if (!What.StartsWith(TEXT("screen_")) || !Meshes.IsValidIndex(G.Mesh) || !Meshes[G.Mesh]) continue;
		TArray<FString> Bits;
		What.ParseIntoArray(Bits, TEXT("_"));
		TorqueTft::Shape S = TorqueTft::Shape::Wide;
		if (Bits.Num() < 2 || !TTI::ShapeOf(Bits[1], S)) continue;
		float W = 0.0f, H = 0.0f;
		TorqueTft::SizeOf(S, W, H);
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, FMath::RoundToInt(W), FMath::RoundToInt(H), RTF_RGBA8_SRGB, FLinearColor::Black);
		if (!RT) continue;
		if (S == TorqueTft::Shape::Phone)
		{
			if (bPhone) continue;
			bPhone = true;
			PhoneRT = RT;
			PhoneFace = Meshes[G.Mesh];
			PhoneMat = Light(Meshes[G.Mesh], RT);
		}
		else
		{
			if (bTft) continue;
			bTft = true;
			ScreenShape = S;
			ScreenRT = RT;
			ScreenFace = Meshes[G.Mesh];
			ScreenMat = Light(Meshes[G.Mesh], RT);
		}
	}
	Menu = TorqueTft::Menu();
	Caps = Spec.Aids;
	Menu.Setup(Caps, ScreenShape == TorqueTft::Shape::Ultra, std::string());
	bModeDirty = true;
	Bright = -1.0f;
	ScreenIn = PhoneIn = TileIn = WorldIn = 0.0;
	Scene = TorqueTft::MapScene();
	GuideHeld.reset();
	if (bOwnBike)
	{
		// (rebuilt under the rider: his settings again)
		bOwnBike = false;
		Own();
	}
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: tft: %s"), *Describe());
	}
}

UMaterialInstanceDynamic* UTorqueTftComponent::Light(UStaticMeshComponent* Face, UTextureRenderTarget2D* Target)
{
	if (!Face || !Target || !PageBase) return nullptr;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(PageBase, this);
	if (!M) return nullptr;
	M->SetTextureParameterValue(TEXT("Page"), Target);
	M->SetScalarParameterValue(TEXT("Brightness"), TTI::GlowDay);
	Face->SetMaterial(0, M);
	Face->SetCastShadow(false);
	return M;
}

void UTorqueTftComponent::Own()
{
	if (bOwnBike) return;
	bOwnBike = true;
	const FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	Menu.Setup(Caps, ScreenShape == TorqueTft::Shape::Ultra, std::string(TCHAR_TO_UTF8(*P.Mode)));
	std::map<std::string, int> Saved;
	for (const TPair<FString, int32>& KV : P.Sets) Saved[std::string(TCHAR_TO_UTF8(*KV.Key))] = KV.Value;
	Menu.Take(Saved);
	Menu.Look = TorqueTft::Design(FMath::Clamp(P.Design, 0, (int32)TorqueTft::Design::Count - 1));
	Menu.NorthUp = P.bNorthUp;
	Menu.Zoom = P.Zoom;
	bModeDirty = true;
	if (!TileRT && HasMap())
		TileRT = UKismetRenderingLibrary::CreateRenderTarget2D(this, FMath::RoundToInt(TTI::TileW), FMath::RoundToInt(TTI::TileH), RTF_RGBA8_SRGB, FLinearColor::Black);
}

bool UTorqueTftComponent::TakeModeDirty()
{
	const bool bWas = bModeDirty;
	bModeDirty = false;
	return bWas;
}

void UTorqueTftComponent::KeepPrefs() const
{
	if (!bOwnBike) return;
	FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	P.Design = (int32)Menu.Look;
	P.bNorthUp = Menu.NorthUp;
	P.Zoom = Menu.Zoom;
	if (Menu.HasModes())
	{
		P.Mode = FString(UTF8_TO_TCHAR(TorqueSim::ModeKey(Menu.Mode())));
		// This bike's modes as altered; another bike's (a mode this one hasn't) are kept as they were.
		const std::map<std::string, int> Now = Menu.Give();
		const std::vector<TorqueSim::RideMode> Mine = TorqueSim::ModesFor(Caps);
		TArray<FString> Stale;
		for (const TPair<FString, int32>& Old : P.Sets)
		{
			FString ModeWord, Rest;
			if (!Old.Key.Split(TEXT("."), &ModeWord, &Rest)) continue;
			bool bMine = false;
			for (const TorqueSim::RideMode M : Mine)
				if (ModeWord == FString(UTF8_TO_TCHAR(TorqueSim::ModeKey(M)))) bMine = true;
			if (bMine) Stale.Add(Old.Key);
		}
		for (const FString& Key : Stale) P.Sets.Remove(Key);
		for (const std::pair<const std::string, int>& KV : Now) P.Sets.Add(FString(UTF8_TO_TCHAR(KV.first.c_str())), KV.second);
	}
	P.Save();
}

int32 UTorqueTftComponent::Press(TorqueTft::Press P, double SpeedMs, double Throttle)
{
	int32 Did = TorqueTft::Menu::Nothing;
	if (bTft) Did = Menu.Do(P, SpeedMs, Throttle);
	else if (bPhone)
	{
		// The phone's only the map: up and down its zoom, left and right which way is up.
		if (P == TorqueTft::Press::Up || P == TorqueTft::Press::Down)
		{
			const int32 To = FMath::Clamp(Menu.Zoom + (P == TorqueTft::Press::Up ? -1 : 1), -2, 2);
			if (To != Menu.Zoom) Did = TorqueTft::Menu::MapChanged;
			Menu.Zoom = To;
		}
		else
		{
			Menu.NorthUp = !Menu.NorthUp;
			Did = TorqueTft::Menu::MapChanged;
		}
	}
	if (Did & (TorqueTft::Menu::ModeChanged | TorqueTft::Menu::SetChanged)) bModeDirty = true;
	if (Did & (TorqueTft::Menu::ModeChanged | TorqueTft::Menu::SetChanged | TorqueTft::Menu::LookChanged | TorqueTft::Menu::MapChanged)) KeepPrefs();
	if (Did != TorqueTft::Menu::Nothing) ScreenIn = PhoneIn = TileIn = 0.0;   // shown at once
	return Did;
}

void UTorqueTftComponent::Notice(const FString& Words)
{
	Menu.Notice = std::string(TCHAR_TO_UTF8(*Words));
	Menu.NoticeFor = 3.0;
}

// --- Each frame --------------------------------------------------------------------------------

void UTorqueTftComponent::TakeWorld(const FVector& At, const FVector& Forward, double SpeedMs)
{
	TTI::PollMap();
	Map = TTI::GMap;
	Scene.Data = Map.get();
	Scene.Bike = TorqueTft::MapPt(At.X / 100.0, At.Y / 100.0);
	Scene.Heading = FMath::Atan2(Forward.X, -Forward.Y);
	Scene.NorthUp = Menu.NorthUp;
	Scene.Metres = TorqueTft::ViewMetres(SpeedMs, Shown.Nav && !Shown.NavOff ? Shown.NavMetres : 0.0, Menu.Zoom);
	bRerouting = bOffRoute = false;
#if TORQUE_TFT_ROAM
	const std::shared_ptr<const TorqueWorld::RideGuide> Guide = ATorqueRideWorld::GuideNow();
	if (Guide.get() != GuideHeld.get())
	{
		GuideHeld = Guide;
		Scene.Route.clear();
		Scene.Waypoints.clear();
		Scene.HasFinish = false;
		if (Guide)
		{
			Scene.Route.reserve(Guide->Line.size());
			for (const TorqueWorld::P2& Pt : Guide->Line) Scene.Route.push_back(TorqueTft::MapPt(Pt.X, Pt.Z));
			// (the pins still to come; the finish, the last of them, has its own flag)
			for (size_t I = 0; I + 1 < Guide->Waypoints.size(); ++I) Scene.Waypoints.push_back(TorqueTft::MapPt(Guide->Waypoints[I].X, Guide->Waypoints[I].Z));
			Scene.HasFinish = !Guide->Line.empty();
			Scene.Finish = TorqueTft::MapPt(Guide->Finish.X, Guide->Finish.Z);
		}
	}
	if (Guide)
	{
		bRerouting = Guide->Rerouting;
		bOffRoute = Guide->OffRoute;
	}
#endif
	// The rest of the crew, by name.
	Scene.Riders.clear();
	const AActor* Me = GetOwner();
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GS)
	{
		int32 Nth = 0;
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const APawn* Other = PS ? PS->GetPawn() : nullptr;
			if (!Other || Other == Me)
			{
				++Nth;
				continue;
			}
			TorqueTft::MapRider R;
			R.At = TorqueTft::MapPt(Other->GetActorLocation().X / 100.0, Other->GetActorLocation().Y / 100.0);
			R.Colour = TTI::CrewColour(Nth++);
			R.Name = std::string(TCHAR_TO_UTF8(*PS->GetPlayerName()));
			Scene.Riders.push_back(R);
		}
	}
}

void UTorqueTftComponent::Step(float Dt, const TorqueTft::State& Ride, const FVector& At, const FVector& Forward, double SpeedMs, float DayLight)
{
	if (!bTft && !bPhone) return;
	Menu.Tick(Dt);
	Shown = Ride;
	Menu.Fill(Shown);
	Shown.Night = DayLight < 0.3f;
	Scene.Night = Shown.Night;
	// The world: the map as it grows and the ride's way (a few times a second), the bike on it (always).
	WorldIn -= Dt;
	if (WorldIn <= 0.0)
	{
		WorldIn = bOwnBike ? 0.08 : 0.5;
		TakeWorld(At, Forward, SpeedMs);
	}
	else
	{
		Scene.Bike = TorqueTft::MapPt(At.X / 100.0, At.Y / 100.0);
		Scene.Heading = FMath::Atan2(Forward.X, -Forward.Y);
	}
	if (Shown.Notice.empty() && Shown.Nav)
	{
		if (bRerouting) Shown.Notice = "Finding a new way...";
		else if (bOffRoute) Shown.Notice = "Off the route";
	}
	// The glow follows the day; the faces keep their pages (a repaint of the bike puts its own back).
	const float Glow = FMath::Lerp(TTI::GlowNight, TTI::GlowDay, FMath::Clamp(DayLight, 0.0f, 1.0f));
	if (FMath::Abs(Glow - Bright) > 0.25f)
	{
		Bright = Glow;
		if (ScreenMat) ScreenMat->SetScalarParameterValue(TEXT("Brightness"), Glow);
		if (PhoneMat) PhoneMat->SetScalarParameterValue(TEXT("Brightness"), Glow * 0.8f);
	}
	if (ScreenMat && ScreenFace.IsValid() && ScreenFace->GetMaterial(0) != ScreenMat) ScreenFace->SetMaterial(0, ScreenMat);
	if (PhoneMat && PhoneFace.IsValid() && PhoneFace->GetMaterial(0) != PhoneMat) PhoneFace->SetMaterial(0, PhoneMat);

	const TorqueTft::MapScene* MapNow = &Scene;
	// The bike's screen: 30 times a second for its rider (15 with the map on it), 8 for anyone
	// else's, and only while someone can see it (or it's on the HUD).
	ScreenIn -= Dt;
	if (bTft && ScreenRT && ScreenIn <= 0.0)
	{
		const bool bMapOn = Shown.Now == TorqueTft::View::Map || Shown.Now == TorqueTft::View::Split;
		ScreenIn = bOwnBike ? (bMapOn ? 1.0 / 15.0 : 1.0 / 30.0) : 1.0 / 8.0;
		const bool bSeen = (bOwnBike && OverlayOn()) || (ScreenFace.IsValid() && ScreenFace->WasRecentlyRendered(0.4f));
		if (bSeen)
		{
			TorqueTft::Paint List;
			TorqueTft::Draw(List, ScreenShape, Shown, MapNow);
			PaintTo(ScreenRT, List);
		}
	}
	PhoneIn -= Dt;
	if (bPhone && PhoneRT && PhoneIn <= 0.0)
	{
		PhoneIn = bOwnBike ? 1.0 / 12.0 : 1.0 / 3.0;
		if (PhoneFace.IsValid() && PhoneFace->WasRecentlyRendered(0.4f))
		{
			TorqueTft::Paint List;
			TorqueTft::Draw(List, TorqueTft::Shape::Phone, Shown, MapNow);
			PaintTo(PhoneRT, List);
		}
	}
	TileIn -= Dt;
	if (bOwnBike && TileRT && TileIn <= 0.0 && MapTileOn())
	{
		TileIn = 1.0 / 12.0;
		TorqueTft::Paint List(TTI::TileW, TTI::TileH);
		TorqueTft::DrawMapTile(List, Shown, MapNow);
		PaintTo(TileRT, List);
	}
}

void UTorqueTftComponent::PaintTo(UTextureRenderTarget2D* Target, const TorqueTft::Paint& List)
{
	if (!Target) return;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor::Black);
	UCanvas* C = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, C, Size, Context);
	if (C)
	{
		// The list is in the screen's own pixels; the target may be another size.
		const float Sx = List.W > 0.0f ? (float)Size.X / List.W : 1.0f, Sy = List.H > 0.0f ? (float)Size.Y / List.H : 1.0f;
		TArray<FCanvasUVTri> Tris;
		for (const TorqueTft::Cmd& Op : List.Cmds)
		{
			const FLinearColor Tint = TTI::Colour(Op.Colour);
			if (Op.IsText)
			{
				const float Px = Op.Px * Sy;
				const TorqueUi::EAlign Side = Op.Side == TorqueTft::Align::Centre ? TorqueUi::EAlign::Centre
					: Op.Side == TorqueTft::Align::Right ? TorqueUi::EAlign::Right : TorqueUi::EAlign::Left;
				TorqueUi::Text(C, FString(UTF8_TO_TCHAR(Op.Words.c_str())), Op.At.X * Sx, Op.At.Y * Sy - 0.93f * Px, Px, Tint, Op.Bold, Side, false);
				continue;
			}
			Tris.Reset();
			Tris.Reserve((int32)(Op.Tris.size() / 3));
			for (size_t I = 0; I + 2 < Op.Tris.size(); I += 3)
			{
				FCanvasUVTri T;
				T.V0_Pos = FVector2D(Op.Tris[I].X * Sx, Op.Tris[I].Y * Sy);
				T.V1_Pos = FVector2D(Op.Tris[I + 1].X * Sx, Op.Tris[I + 1].Y * Sy);
				T.V2_Pos = FVector2D(Op.Tris[I + 2].X * Sx, Op.Tris[I + 2].Y * Sy);
				T.V0_Color = T.V1_Color = T.V2_Color = Tint;
				Tris.Add(T);
			}
			if (Tris.Num() == 0) continue;
			FCanvasTriangleItem Item(Tris, GWhiteTexture);
			Item.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(Item);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

// --- The HUD -----------------------------------------------------------------------------------

bool UTorqueTftComponent::OverlayOn() const
{
	return bOwnBike && bTft && ScreenRT && (FTorqueTftPrefs::Get().bOverlay || bArrange);
}

bool UTorqueTftComponent::MapTileOn() const
{
	// With a map to show, or while it's being placed.
	return bOwnBike && HasMap() && TileRT && FTorqueTftPrefs::Get().bMapTile && (Scene.Data != nullptr || bArrange);
}

FBox2D UTorqueTftComponent::OverlayBox(const UCanvas* Canvas) const
{
	const FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	const float S = TorqueUi::Scale(Canvas) * P.OverlayScale;
	float W = 0.0f, H = 0.0f;
	TorqueTft::SizeOf(ScreenShape, W, H);
	// At 1080p: the landscape screens 380 across, the widescreen 540, the round one 230.
	const float K = ScreenShape == TorqueTft::Shape::Ultra ? 540.0f / W : ScreenShape == TorqueTft::Shape::Round ? 230.0f / W : 380.0f / W;
	const FVector2D Size(W * K * S, H * K * S);
	const float Edge = 22.0f * TorqueUi::Scale(Canvas);
	FVector2D Corner(Canvas->SizeX - Edge, Canvas->SizeY - Edge);
	if (P.OverlayX >= 0.0f && P.OverlayY >= 0.0f) Corner = FVector2D(P.OverlayX * Canvas->SizeX, P.OverlayY * Canvas->SizeY);
	// Kept on the screen whatever was saved (another window size).
	Corner.X = FMath::Clamp(Corner.X, Size.X, (double)Canvas->SizeX);
	Corner.Y = FMath::Clamp(Corner.Y, Size.Y, (double)Canvas->SizeY);
	return FBox2D(Corner - Size, Corner);
}

FBox2D UTorqueTftComponent::TileBox(const UCanvas* Canvas, float PlainTop) const
{
	const FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	const float U = TorqueUi::Scale(Canvas);
	const FVector2D Size(300.0f * U * P.TileScale, 225.0f * U * P.TileScale);
	// Its usual place: over the screen's overlay (or the plain speedo), against the right edge.
	const float Edge = 22.0f * U;
	const float Top = OverlayOn() ? (float)OverlayBox(Canvas).Min.Y : PlainTop;
	FVector2D Corner(Canvas->SizeX - Edge, Top - 12.0f * U);
	if (P.TileX >= 0.0f && P.TileY >= 0.0f) Corner = FVector2D(P.TileX * Canvas->SizeX, P.TileY * Canvas->SizeY);
	Corner.X = FMath::Clamp(Corner.X, Size.X, (double)Canvas->SizeX);
	Corner.Y = FMath::Clamp(Corner.Y, Size.Y, (double)Canvas->SizeY);
	return FBox2D(Corner - Size, Corner);
}

void UTorqueTftComponent::DrawOnHud(UCanvas* Canvas, float PlainTop)
{
	if (!Canvas || !bOwnBike) return;
	const float U = TorqueUi::Scale(Canvas);
	const FLinearColor Bezel(0.02f, 0.022f, 0.026f, 0.96f);
	if (MapTileOn())
	{
		const FBox2D B = TileBox(Canvas, PlainTop);
		const float T = 4.0f * U;
		TorqueUi::Rect(Canvas, (float)B.Min.X - T, (float)B.Min.Y - T, (float)B.GetSize().X + 2.0f * T, (float)B.GetSize().Y + 2.0f * T, Bezel);
		Canvas->K2_DrawTexture(TileRT, B.Min, B.GetSize(), FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), FLinearColor::White, BLEND_Opaque);
	}
	if (OverlayOn())
	{
		const FBox2D B = OverlayBox(Canvas);
		const float T = 5.0f * U;
		if (ScreenShape == TorqueTft::Shape::Round)
		{
			// The round screen: a disc of its page in a ring.
			const FVector2D Mid = B.GetCenter();
			const float R = (float)B.GetSize().X * 0.5f;
			TorqueUi::Disc(Canvas, (float)Mid.X, (float)Mid.Y, R + T, Bezel, 48);
			TArray<FCanvasUVTri> Tris;
			const int32 Segs = 48;
			Tris.Reserve(Segs);
			for (int32 I = 0; I < Segs; ++I)
			{
				const float A0 = UE_TWO_PI * I / Segs, A1 = UE_TWO_PI * (I + 1) / Segs;
				const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0)), D1(FMath::Cos(A1), FMath::Sin(A1));
				FCanvasUVTri Tri;
				Tri.V0_Pos = Mid;
				Tri.V1_Pos = Mid + D0 * R;
				Tri.V2_Pos = Mid + D1 * R;
				Tri.V0_UV = FVector2D(0.5, 0.5);
				Tri.V1_UV = FVector2D(0.5, 0.5) + D0 * 0.5;
				Tri.V2_UV = FVector2D(0.5, 0.5) + D1 * 0.5;
				Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = FLinearColor::White;
				Tris.Add(Tri);
			}
			Canvas->K2_DrawTriangle(ScreenRT, Tris);
		}
		else
		{
			TorqueUi::Rect(Canvas, (float)B.Min.X - T, (float)B.Min.Y - T, (float)B.GetSize().X + 2.0f * T, (float)B.GetSize().Y + 2.0f * T, Bezel);
			Canvas->K2_DrawTexture(ScreenRT, B.Min, B.GetSize(), FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), FLinearColor::White, BLEND_Opaque);
		}
	}
	if (bArrange)
	{
		// Arranging: a frame round each, the one being moved in amber, and how.
		const bool bHasTile = HasMap() && TileRT;
		const bool bHasScreen = bTft && ScreenRT;
		for (int32 I = 0; I < 2; ++I)
		{
			if (I == 0 ? !bHasScreen : !bHasTile) continue;
			const FBox2D B = I == 0 ? OverlayBox(Canvas) : TileBox(Canvas, PlainTop);
			const bool bPicked = ArrangeWhat == I;
			TorqueUi::Frame(Canvas, (float)B.Min.X - 6.0f * U, (float)B.Min.Y - 6.0f * U, (float)B.GetSize().X + 12.0f * U, (float)B.GetSize().Y + 12.0f * U,
				bPicked ? TorqueUi::Accent : TorqueUi::Dim, (bPicked ? 3.0f : 1.5f) * U);
			TorqueUi::Text(Canvas, I == 0 ? TEXT("SCREEN") : TEXT("MAP"), (float)B.Min.X, (float)B.Min.Y - 30.0f * U, 16.0f * U, bPicked ? TorqueUi::Accent : TorqueUi::Dim, true);
		}
		const float Y = Canvas->SizeY * 0.16f;
		TorqueUi::Text(Canvas, TEXT("ARRANGE THE HUD"), Canvas->SizeX * 0.5f, Y, 24.0f * U, TorqueUi::Accent, true, TorqueUi::EAlign::Centre);
		TorqueUi::Text(Canvas, TEXT("Screen buttons (LB + D-pad, or 1 2 3 4): move.   Camera closer / farther: size.   F4: screen or map.   F5: done."),
			Canvas->SizeX * 0.5f, Y + 34.0f * U, 16.0f * U, TorqueUi::Ink, false, TorqueUi::EAlign::Centre);
	}
}

void UTorqueTftComponent::ToggleOverlay()
{
	if (!bOwnBike) return;
	FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	if (bArrange)
	{
		// Arranging, the same key picks which of the two is moved.
		ArrangeNext();
		return;
	}
	// Round: both, the screen only, the map only, neither (what the bike has of them).
	const bool bCanScreen = bTft, bCanTile = HasMap();
	if (bCanScreen && bCanTile)
	{
		const int32 Now = (P.bOverlay ? 0 : 2) + (P.bMapTile ? 0 : 1);   // 0 both, 1 screen, 2 map, 3 neither
		const int32 To = (Now + 1) % 4;
		P.bOverlay = To < 2;
		P.bMapTile = (To % 2) == 0;
	}
	else if (bCanTile) P.bMapTile = !P.bMapTile;
	else return;
	P.Save();
	Notice(P.bOverlay && P.bMapTile ? TEXT("HUD: screen and map") : P.bOverlay ? TEXT("HUD: screen") : P.bMapTile ? TEXT("HUD: map") : TEXT("HUD: plain"));
}

void UTorqueTftComponent::ToggleArrange()
{
	if (!bOwnBike || !HasMap()) return;
	bArrange = !bArrange;
	if (bArrange)
	{
		ArrangeWhat = bTft ? 0 : 1;
		TileIn = ScreenIn = 0.0;
	}
	else FTorqueTftPrefs::Get().Save();
}

void UTorqueTftComponent::ArrangeNext()
{
	if (bTft && HasMap()) ArrangeWhat = 1 - ArrangeWhat;
}

void UTorqueTftComponent::ArrangeMove(int32 Dx, int32 Dy)
{
	if (!bArrange) return;
	FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	float& X = ArrangeWhat == 0 ? P.OverlayX : P.TileX;
	float& Y = ArrangeWhat == 0 ? P.OverlayY : P.TileY;
	// From where the game had put it: near enough the bottom right.
	if (X < 0.0f || Y < 0.0f)
	{
		X = 0.985f;
		Y = ArrangeWhat == 0 ? 0.975f : 0.70f;
	}
	X = FMath::Clamp(X + 0.02f * Dx, 0.1f, 1.0f);
	Y = FMath::Clamp(Y + 0.03f * Dy, 0.1f, 1.0f);
}

void UTorqueTftComponent::ArrangeSize(int32 Step)
{
	if (!bArrange) return;
	FTorqueTftPrefs& P = FTorqueTftPrefs::Get();
	float& S = ArrangeWhat == 0 ? P.OverlayScale : P.TileScale;
	S = FMath::Clamp(S + 0.1f * Step, 0.5f, 2.0f);
}

FString UTorqueTftComponent::Describe() const
{
	static const TCHAR* Shapes[4] = {TEXT("wide"), TEXT("ultra"), TEXT("round"), TEXT("phone")};
	static const TCHAR* Views[5] = {TEXT("ride"), TEXT("map"), TEXT("split"), TEXT("modes"), TEXT("settings")};
	const FString Look = UTF8_TO_TCHAR(TorqueTft::DesignName(Menu.Look));
	const FString ModeWord = Menu.HasModes() ? FString(UTF8_TO_TCHAR(TorqueSim::ModeKey(Menu.Mode()))) : FString(TEXT("none"));
	return FString::Printf(TEXT("screen %s, phone %s, own %d, view %s, design %s, mode %s, map %s (%d roads, route %d pts), overlay %d, tile %d"),
		bTft ? Shapes[FMath::Clamp((int32)ScreenShape, 0, 3)] : TEXT("none"), bPhone ? TEXT("yes") : TEXT("no"), bOwnBike ? 1 : 0,
		Views[FMath::Clamp((int32)Menu.Now, 0, 4)], *Look, *ModeWord,
		Scene.Data ? TEXT("yes") : TEXT("no"), Scene.Data ? (int32)Scene.Data->Lines.size() : 0, (int32)Scene.Route.size(),
		OverlayOn() ? 1 : 0, MapTileOn() ? 1 : 0);
}
