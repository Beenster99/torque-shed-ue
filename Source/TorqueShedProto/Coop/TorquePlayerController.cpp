#include "TorquePlayerController.h"

#include "Shed/TorqueGeometryMode.h"
#include "TorqueMenuPawn.h"
#include "TorquePlayerState.h"
#include "TorqueSession.h"
#include "TorqueSettings.h"
#include "TorqueKeys.h"
#include "TorqueGameState.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "TorqueBikePawn.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueProfile.h"
#include "Shed/TorqueProfilePreview.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
FString Pct(float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f)); }
FString OnOff(bool b) { return b ? TEXT("On") : TEXT("Off"); }
template <typename T> T Wrap(T V, T N) { return ((V % N) + N) % N; }
float StepF(float V, int32 Dir, float Step, float Lo, float Hi)
{
	return FMath::Clamp(FMath::RoundToFloat((V + Dir * Step) / Step) * Step, Lo, Hi);
}
const int32 FrameCaps[] = {0, 30, 60, 90, 120, 144, 165, 240};
// Mouse look speeds, times the default: finer steps low down, bigger ones up high (to 10x).
const float MouseSpeeds[] = {0.1f, 0.15f, 0.25f, 0.35f, 0.5f, 0.65f, 0.8f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f,
	5.0f, 6.0f, 7.0f, 8.0f, 10.0f};
float StepList(float V, int32 Dir, const float* List, int32 Num)
{
	int32 Near = 0;
	for (int32 I = 1; I < Num; ++I)
		if (FMath::Abs(List[I] - V) < FMath::Abs(List[Near] - V)) Near = I;
	return List[FMath::Clamp(Near + Dir, 0, Num - 1)];
}
} // namespace

ATorquePlayerController::ATorquePlayerController()
{
	// The menu works while the game is paused.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bShouldPerformFullTickWhenPaused = true;
}

void ATorquePlayerController::BeginPlay()
{
	Super::BeginPlay();
	// The host's copy of a joined player's controller: the shed's answers to them go to them.
	if (HasAuthority() && !IsLocalController())
		if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) TellHandle = B->OnTell.AddUObject(this, &ATorquePlayerController::TellPeer);
	if (!IsLocalController()) return;
	Pad = MakeUnique<FSonyPad>();
	// The player's saved graphics and sound (a fresh install, and test runs, keep the project's own).
	FTorqueSettings& S = FTorqueSettings::Get();
	const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings"));
	if (S.bSaved && !bTest) S.Apply(GetWorld());
	// A joined player: shed actions go to the host.
	if (GetNetMode() == NM_Client)
		if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->Forward.BindUObject(this, &ATorquePlayerController::ForwardShed);
}

bool ATorquePlayerController::ForwardShed(const FTorqueShedRequest& Request)
{
	ServerShed(TorqueShedNet::ToNet(Request));
	return true;
}

void ATorquePlayerController::ServerShed_Implementation(const FTorqueShedRequestNet& Request)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B || !PlayerState) return;
	// The answer (done, or why not) comes back through OnTell (TellPeer).
	FString WhyNot;
	B->Apply(PlayerState->GetPlayerId(), TorqueShedNet::FromNet(Request), WhyNot);
}

void ATorquePlayerController::TellPeer(int32 Peer, const FString& Text, bool bOk)
{
	if (!IsLocalController() && PlayerState && PlayerState->GetPlayerId() == Peer && !Text.IsEmpty()) ClientNote(Text);
}

void ATorquePlayerController::ClientNote_Implementation(const FString& Text)
{
	Note = Text.Left(200);
	NoteFor = 4.0;
}

void ATorquePlayerController::SyncPlayerState(float Dt)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B || GetNetMode() == NM_Standalone) return;
	// What this player is doing and which bay is theirs, for everyone to see.
	ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	if (!PS) return;
	// Once the player state is here: the colour (and the name, if changed since joining) chosen.
	if (!bLookSent)
	{
		bLookSent = true;
		PS->ServerSetLook(FTorqueSettings::Get().Name, (uint8)FTorqueSettings::Get().Colour);
		PS->ServerSetRiderStyle((uint8)FTorqueSettings::Get().RiderStyle);
	}
	if (!bProfileSent)   // [profile]
	{
		bProfileSent = true;
		PS->ServerSetProfile(FTorqueProfile::Mine().Pack());
	}
	if (B->Activity() != SentActivity)
	{
		SentActivity = B->Activity();
		PS->ServerSetActivity(SentActivity);
	}
	if (B->MyBike() != SentBay)
	{
		SentBay = B->MyBike();
		PS->ServerSetBay(SentBay);
	}
}

void ATorquePlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		if (B->Forward.IsBoundToObject(this)) B->Forward.Unbind();
		B->OnTell.Remove(TellHandle);
		// A joined player gone: what was in their hands goes down where they stood.
		if (HasAuthority() && !IsLocalController() && PlayerState)
			B->DropHeldBy(PlayerState->GetPlayerId(), GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);
	}
	if (bPausedGame) SetPause(false);
	Pad.Reset();
	Super::EndPlay(Reason);
}

bool ATorquePlayerController::MenuUp(const AController* Controller)
{
	const ATorquePlayerController* PC = Cast<ATorquePlayerController>(Controller);
	return PC && PC->IsMenuUp();
}

bool ATorquePlayerController::IsMenuUp() const
{
	return Page != EPage::None || ClosedFrame + 1 >= GFrameCounter;
}

void ATorquePlayerController::Open(EPage P)
{
	if (Page != EPage::None) Stack.Add(Page);
	Page = P;
	Selected = 0;
	FirstRow = 0;
}

void ATorquePlayerController::Back()
{
	if (Stack.Num() > 0)
	{
		Page = Stack.Pop();
		Selected = 0;
		FirstRow = 0;
		return;
	}
	CloseMenu();
}

void ATorquePlayerController::OpenPause()
{
	Stack.Reset();
	Open(EPage::Pause);
	// Alone, the world stops; in co-op it can't.
	if (GetNetMode() == NM_Standalone && !IsPaused()) bPausedGame = SetPause(true);
}

void ATorquePlayerController::OpenSettings()
{
	Stack.Reset();
	Open(EPage::Graphics);
}

void ATorquePlayerController::CloseMenu()
{
	Page = EPage::None;
	Stack.Reset();
	ClosedFrame = GFrameCounter;
	if (bPausedGame)
	{
		SetPause(false);
		bPausedGame = false;
	}
}

// [shed test]
FString ATorquePlayerController::TestMenu(const FString& Which)
{
	Stack.Reset();
	Page = EPage::None;
	if (Which == TEXT("close")) { CloseMenu(); return TEXT("menu closed"); }
	Open(EPage::Pause);
	FString Tab;
	const FString Base = Which.Split(TEXT(":"), nullptr, &Tab) ? Which.Left(Which.Find(TEXT(":"))) : Which;
	if (Base == TEXT("profile")) Open(EPage::Profile);
	else if (Base == TEXT("settings"))
	{
		// "settings:<tab>": graphics, sound, gameplay, controller, character, or keys / padcheck.
		static const TCHAR* Names[] = {TEXT("graphics"), TEXT("sound"), TEXT("gameplay"), TEXT("controller"), TEXT("character")};
		static const EPage Pages[] = {EPage::Graphics, EPage::Sound, EPage::Gameplay, EPage::Controller, EPage::Character};
		EPage Want = EPage::Graphics;
		for (int32 I = 0; I < 5; ++I)
			if (Tab == Names[I]) Want = Pages[I];
		if (Tab == TEXT("keys") || Tab == TEXT("padcheck"))
		{
			Open(EPage::Controller);
			Want = Tab == TEXT("keys") ? EPage::Keys : EPage::PadCheck;
		}
		Open(Want);
	}
	else if (Base == TEXT("keys")) { Open(EPage::Controller); Open(EPage::Keys); }
	else if (Base != TEXT("pause")) return TEXT("menu what? pause, profile, settings[:graphics|sound|gameplay|controller|character|keys|padcheck], keys or close (opened the pause menu)");
	return FString::Printf(TEXT("menu: %s (page shows %d rows)"), *Which, Rows().Num());
}

// [settings test] A setting changed for the test run (not saved), put on the game, and what it is now.
FString ATorquePlayerController::TestSet(const FString& Field, const FString& Value)
{
	FTorqueSettings& S = FTorqueSettings::Get();
	const float V = FCString::Atof(*Value);
	const int32 N = FCString::Atoi(*Value);
	const bool bOn = Value == TEXT("on") || Value == TEXT("1") || Value == TEXT("true");
	ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	const FString F = Field.ToLower();
	if (F == TEXT("fov")) S.Fov = FMath::Clamp(V, 60.0f, 110.0f);
	else if (F == TEXT("timeofday") || F == TEXT("time"))
	{
		S.TimeOfDay = V < 0.0f ? -1.0f : FMath::Fmod(V, 24.0f);
		if (GS) GS->SetHour(S.TimeOfDay);
	}
	else if (F == TEXT("daynight")) S.bDayNight = bOn;
	else if (F == TEXT("daylength")) S.DayLength = N;
	else if (F == TEXT("weather"))
	{
		S.WeatherMode = FMath::Clamp(N, 0, 3);
		if (GS && S.WeatherMode > 0) GS->SetWeather(S.WeatherMode - 1);
	}
	else if (F == TEXT("units")) S.Units = Value == TEXT("metric") || Value == TEXT("0") ? 0 : 1;
	else if (F == TEXT("telemetry")) S.bTelemetry = bOn;
	else if (F == TEXT("fps")) S.bFpsCounter = bOn;
	else if (F == TEXT("autoshift")) S.bAutoShift = bOn;
	else if (F == TEXT("recentre")) S.bCameraRecentre = bOn;
	else if (F == TEXT("lookintocorner")) S.bLookIntoCorner = bOn;
	else if (F == TEXT("brightness")) S.Brightness = FMath::Clamp(V, 0.5f, 1.5f);
	else if (F == TEXT("sharpness")) S.Sharpness = FMath::Clamp(V, 0.0f, 1.0f);
	else if (F == TEXT("bloom")) S.Bloom = FMath::Clamp(V, 0.0f, 2.0f);
	else if (F == TEXT("quality")) S.Quality = FMath::Clamp(N, 0, 4);
	else if (F == TEXT("shadows")) S.Shadows = FMath::Clamp(N, -1, 4);
	else if (F == TEXT("drawdistance")) S.DrawDistance = FMath::Clamp(V, 0.5f, 2.0f);
	else if (F == TEXT("fog")) S.Fog = FMath::Clamp(N, -1, 2);
	else if (F == TEXT("clouds")) S.Clouds = FMath::Clamp(N, -1, 4);
	else if (F == TEXT("ao")) S.bAmbientOcclusion = bOn;
	else if (F == TEXT("avatarshadows")) S.bAvatarShadows = bOn;
	else if (F == TEXT("aa")) S.AntiAliasing = FMath::Clamp(N, 0, 3);
	else
		return FString::Printf(TEXT("set what? fov, timeofday, daynight, daylength, weather (0 auto .. 3 wet), units, telemetry, fps, autoshift, recentre, ")
			TEXT("lookintocorner, brightness, sharpness, bloom, quality, shadows, drawdistance, fog, clouds, ao, avatarshadows, aa (not %s)"), *Field);
	S.Apply(GetWorld());
	FString Sun;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = FString::Printf(TEXT(", sun pitch %.1f yaw %.1f intensity %.0f"), It->GetActorRotation().Pitch, It->GetActorRotation().Yaw,
			It->GetLightComponent() ? It->GetLightComponent()->Intensity : 0.0f);
		break;
	}
	const APlayerCameraManager* Cam = PlayerCameraManager;
	const FString Clock = !GS ? FString(TEXT("-")) : GS->GetHour() < 0.0f ? FString(TEXT("level's own")) : ATorqueGameState::ClockText(GS->GetHour());
	return FString::Printf(TEXT("set %s = %s: fov %.0f (camera now %.1f), clock %s, weather %d, telemetry %d, units %s%s"), *Field, *Value, S.Fov,
		Cam ? Cam->GetFOVAngle() : 0.0f, *Clock, GS ? GS->GetWeather() : -1, S.bTelemetry ? 1 : 0, S.Units == 0 ? TEXT("metric") : TEXT("imperial"), *Sun);
}

FString ATorquePlayerController::TestFace(const FString& Slider, float Value)
{
	FTorqueProfile& Me = FTorqueProfile::Mine();
	const float V = FMath::Clamp(Value, -1.0f, 1.0f);
	FString Set;
	if (Slider == TEXT("height")) { Me.Height = V; Set = TEXT("Height"); }
	else if (Slider == TEXT("avatar")) { Me.Avatar = FMath::Max(FMath::RoundToInt(Value), -1); Set = TEXT("Avatar"); }
	else
		for (int32 I = 0; I < FTorqueProfile::NumFace; ++I)
			if (FString(FTorqueProfile::Sliders[I].Name).ToLower().StartsWith(Slider))
			{
				Me.Face[I] = V;
				Set = FTorqueProfile::Sliders[I].Name;
				break;
			}
	if (Set.IsEmpty()) return FString::Printf(TEXT("no face slider like %s"), *Slider);
	// Shown and sent like a change on the page, but not saved: a test run leaves your own face alone.
	if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>()) PS->ServerSetProfile(Me.Pack());
	return FString::Printf(TEXT("profile: %s set to %.2f"), *Set, Set == TEXT("Avatar") ? (float)Me.Avatar : V);
}

bool ATorquePlayerController::WantsPause()
{
	// On the bike or on foot; the start menu has its own Esc, and so has geometry mode.
	if (ATorqueGeometryMode::For(this)) return false;
	const APawn* P = GetPawn();
	if (const ATorqueWalker* W = Cast<ATorqueWalker>(P); W && W->ComputerUpOrJustClosed()) return false;   // [shed] Esc closes the parts computer
	return P && (P->IsA<ATorqueBikePawn>() || P->IsA<ATorqueWalker>());
}

namespace
{
// Settings is Torque Shed's tabs: Graphics, Sound, Gameplay, Controller, Character.
const TCHAR* TabNames[] = {TEXT("Graphics"), TEXT("Sound"), TEXT("Gameplay"), TEXT("Controller"), TEXT("Character")};
} // namespace

int32 ATorquePlayerController::TabIndex() const
{
	switch (Page)
	{
	case EPage::Graphics: return 0;
	case EPage::Sound: return 1;
	case EPage::Gameplay: return 2;
	case EPage::Controller: return 3;
	case EPage::Character: return 4;
	default: return -1;
	}
}

void ATorquePlayerController::SwitchTab(int32 Dir)
{
	static const EPage Tabs[] = {EPage::Graphics, EPage::Sound, EPage::Gameplay, EPage::Controller, EPage::Character};
	const int32 At = TabIndex();
	if (At < 0) return;
	if (Page == EPage::Character) Changed();   // leaving the name keeps it
	Page = Tabs[Wrap(At + Dir, 5)];   // a tab, not a new page: Back still goes to where Settings was opened from
	Selected = 0;
	FirstRow = 0;
}

FString ATorquePlayerController::Title() const
{
	if (Page == EPage::Pause) return TEXT("PAUSED");
	if (Page == EPage::Profile) return TEXT("PROFILE");   // [profile]
	if (Page == EPage::Keys) return TEXT("KEY BINDINGS");
	if (Page == EPage::PadCheck) return TEXT("CONTROLLER CHECK");
	return TabIndex() >= 0 ? TEXT("SETTINGS") : FString();
}

void ATorquePlayerController::Changed()
{
	FTorqueSettings& S = FTorqueSettings::Get();
	S.Save();
	S.Apply(GetWorld());
	if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>())
	{
		PS->ServerSetLook(S.Name, (uint8)S.Colour);
		PS->ServerSetRiderStyle((uint8)S.RiderStyle);
	}
}

TArray<ATorquePlayerController::FRow> ATorquePlayerController::Rows()
{
	TArray<FRow> R;
	FTorqueSettings& S = FTorqueSettings::Get();
	auto Add = [&R](const FString& Label, const FString& Value, const FString& Hint, TFunction<void()> Choose,
				   TFunction<void(int32)> Step = nullptr) {
		FRow Row;
		Row.Label = Label;
		Row.Value = Value;
		Row.Hint = Hint;
		Row.Choose = MoveTemp(Choose);
		Row.Step = MoveTemp(Step);
		R.Add(MoveTemp(Row));
	};
	// A setting: Left/Right (or Enter) steps it, then it's saved and put on the game.
	auto Setting = [this, &Add](const FString& Label, const FString& Value, const FString& Hint, TFunction<void(int32)> Step) {
		Add(Label, Value, Hint, [this, Step]() { Step(1); Changed(); }, [this, Step](int32 Dir) { Step(Dir); Changed(); });
	};
	UTorqueSession* Session = UTorqueSession::Get(this);
	const bool bClient = GetNetMode() == NM_Client;
	const bool bHost = GetNetMode() == NM_ListenServer;

	switch (Page)
	{
	case EPage::Pause:
	{
		Add(TEXT("Continue"), FString(), FString(), [this]() { CloseMenu(); });
		Add(TEXT("Settings"), FString(), TEXT("Graphics, Sound, Gameplay, Controller, Character"), [this]() { Open(EPage::Graphics); });
		// [profile] (also the picture top right)
		Add(TEXT("Profile"), FString(), TEXT("Your character: which MetaHuman you are, your height and your face"), [this]() { Open(EPage::Profile); });
		if (ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn()))
		{
			Add(TEXT("Restart the ride"), FString(), TEXT("Back to the start of the road (Home)"), [this, Bike]() {
				CloseMenu();
				Bike->RestartRide();
			});
			if (!bClient && TorqueFlow::ShedExists())
				Add(bHost ? TEXT("Back to the shed (everyone)") : TEXT("Back to the shed"), FString(), TEXT("Backspace"), [this]() {
					CloseMenu();
					TorqueFlow::BackToShed(this);
				});
		}
		Add(TEXT("Leave and return to title"), FString(),
			bHost ? TEXT("Ends the co-op shed: everyone who joined goes back to their title") : FString(), [this, Session]() {
				CloseMenu();
				if (Session) Session->BackToTitle();
			});
		Add(TEXT("Exit game"), FString(), FString(), [this]() { ConsoleCommand(TEXT("quit")); });
		break;
	}
	case EPage::Settings:
		// Torque Shed's Settings tabs (less its Debug tab).
		Add(TEXT("Graphics"), FString(), TEXT("Quality, screen, upscaling, anti-aliasing, shadows, lighting, fog, clouds, bloom"), [this]() { Open(EPage::Graphics); });
		Add(TEXT("Sound"), FString(), TEXT("Everything, sound effects, engines, the world around and the radio"), [this]() { Open(EPage::Sound); });
		Add(TEXT("Gameplay"), FString(), TEXT("Riding, field of view, camera, units, time and weather"), [this]() { Open(EPage::Gameplay); });
		Add(TEXT("Controller"), FString(), TEXT("Look speeds, invert, the lean stick, key bindings and a controller check"), [this]() { Open(EPage::Controller); });
		Add(TEXT("Character"), FString(), TEXT("Your name, colour and riding style, as everyone in co-op sees them"), [this]() { Open(EPage::Character); });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Graphics:
	{
		static const TCHAR* Qualities[] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic")};
		static const TCHAR* Modes[] = {TEXT("Fullscreen"), TEXT("Borderless window"), TEXT("Window")};
		static const TCHAR* Scales[] = {TEXT("Native (100%)"), TEXT("Quality (67%)"), TEXT("Balanced (58%)"), TEXT("Performance (50%)"),
			TEXT("Ultra performance (33%)")};
		auto Heading = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
		// One of Unreal's scalability groups: the preset's, or a level of its own (Torque Shed's own rows).
		auto Level = [&Setting, &S](const FString& Label, int32& V, const FString& Hint) {
			int32* Ptr = &V;
			Setting(Label, V < 0 ? FString::Printf(TEXT("As the preset (%s)"), Qualities[S.Quality]) : FString(Qualities[FMath::Clamp(V, 0, 4)]), Hint,
				[Ptr](int32 D) { *Ptr = FMath::Clamp(*Ptr + D, -1, 4); });
		};
		const bool bCustom = S.Shadows >= 0 || S.Textures >= 0 || S.GlobalIllumination >= 0 || S.Reflections >= 0 || S.Effects >= 0 ||
							 S.Foliage >= 0 || S.Fog >= 0 || S.Clouds >= 0 || !FMath::IsNearlyEqual(S.DrawDistance, 1.0f);
		Setting(TEXT("Quality preset"), FString(Qualities[S.Quality]) + (bCustom ? TEXT(" (custom)") : TEXT("")),
			TEXT("Unreal's scalability. Picking one puts every quality row below back to it; changing a row makes it custom"),
			[&S](int32 D) {
				S.Quality = FMath::Clamp(S.Quality + D, 0, 4);
				S.Shadows = S.Textures = S.GlobalIllumination = S.Reflections = S.Effects = S.Foliage = S.Fog = S.Clouds = -1;
				S.DrawDistance = 1.0f;
			});
		Heading(TEXT("DISPLAY"));
		Setting(TEXT("Window mode"), Modes[S.WindowMode], FString(), [&S](int32 D) { S.WindowMode = Wrap(S.WindowMode + D, 3); });
		if (Resolutions.IsEmpty())
		{
			TArray<FIntPoint> Found;
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Found);
			for (const FIntPoint& P : Found)
				if (P.X >= 1280) Resolutions.AddUnique(P);
		}
		int32 ResAt = Resolutions.IndexOfByKey(S.Resolution);   // -1: the desktop's
		Setting(TEXT("Resolution"), ResAt < 0 ? FString(TEXT("Desktop")) : FString::Printf(TEXT("%d x %d"), S.Resolution.X, S.Resolution.Y),
			TEXT("Desktop is your screen's own"), [this, &S, ResAt](int32 D) {
				const int32 N = Resolutions.Num() + 1;   // and "desktop"
				const int32 Next = Wrap(ResAt + 1 + D, N) - 1;
				S.Resolution = Next < 0 ? FIntPoint(0, 0) : Resolutions[Next];
			});
		{
			const int32 Screens = FMath::Max(FTorqueSettings::MonitorCount(), 1);
			Setting(TEXT("Monitor"), FTorqueSettings::MonitorName(S.Monitor), TEXT("Which screen the game is on"),
				[&S, Screens](int32 D) { S.Monitor = Wrap(S.Monitor + 1 + D, Screens + 1) - 1; });
		}
		Setting(TEXT("VSync"), OnOff(S.bVSync), TEXT("No tearing, but frames wait for the screen"), [&S](int32) { S.bVSync = !S.bVSync; });
		int32 CapAt = 0;
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(FrameCaps); ++I)
			if (FrameCaps[I] == S.FrameCap) CapAt = I;
		Setting(TEXT("Frame-rate cap"), S.FrameCap == 0 ? FString(TEXT("Off")) : FString::Printf(TEXT("%d fps"), S.FrameCap), FString(),
			[&S, CapAt](int32 D) { S.FrameCap = FrameCaps[Wrap(CapAt + D, (int32)UE_ARRAY_COUNT(FrameCaps))]; });
		Setting(TEXT("Brightness"), Pct(S.Brightness), TEXT("50% to 150%"), [&S](int32 D) { S.Brightness = StepF(S.Brightness, D, 0.05f, 0.5f, 1.5f); });
		Setting(TEXT("Show the frame rate"), OnOff(S.bFpsCounter), TEXT("At the top of the screen"), [&S](int32) { S.bFpsCounter = !S.bFpsCounter; });
		Heading(TEXT("UPSCALING AND SMOOTH EDGES"));
		const bool bDlss = FTorqueSettings::HasDlss();
		Setting(TEXT("Upscaler"), S.Upscaler == 1 && bDlss ? FString(TEXT("NVIDIA DLSS")) : FString(TEXT("Unreal TSR")),
			bDlss ? TEXT("DLSS on an RTX card: sharper for the same frame rate") : TEXT("DLSS needs NVIDIA's DLSS plugin in the build (on its way)"),
			[&S, bDlss](int32 D) { S.Upscaler = bDlss ? Wrap(S.Upscaler + D, 2) : 0; });
		const bool bDlssOn = S.Upscaler == 1 && bDlss;
		Setting(TEXT("Render scale"), bDlssOn && S.UpscaleMode == 0 ? FString(TEXT("DLAA (100%)")) : FString(Scales[S.UpscaleMode]), TEXT("Lower draws fewer pixels and upscales: more frames"),
			[&S](int32 D) { S.UpscaleMode = FMath::Clamp(S.UpscaleMode + D, 0, 4); });
		Setting(TEXT("Sharpness"), S.Sharpness < 0.001f ? FString(TEXT("Off")) : Pct(S.Sharpness), TEXT("Sharpens the picture after upscaling"),
			[&S](int32 D) { S.Sharpness = StepF(S.Sharpness, D, 0.05f, 0.0f, 1.0f); });
		{
			static const TCHAR* Aa[] = {TEXT("TSR (temporal, best looking)"), TEXT("TAA (temporal)"), TEXT("FXAA (fast)"), TEXT("Off")};
			Setting(TEXT("Anti-aliasing"), bDlssOn ? FString(TEXT("DLSS does its own")) : FString(Aa[FMath::Clamp(S.AntiAliasing, 0, 3)]),
				TEXT("Smooth edges (with DLSS on, DLSS smooths them)"), [&S](int32 D) { S.AntiAliasing = Wrap(S.AntiAliasing + D, 4); });
		}
		Heading(TEXT("QUALITY"));
		Level(TEXT("Shadows"), S.Shadows, TEXT("Shadow detail and how far they reach"));
		Level(TEXT("Detail (textures)"), S.Textures, TEXT("Texture detail and filtering"));
		Setting(TEXT("Draw distance"), Pct(S.DrawDistance), TEXT("50% to 200% of the preset's: how far away small things are still drawn"),
			[&S](int32 D) { S.DrawDistance = StepF(S.DrawDistance, D, 0.05f, 0.5f, 2.0f); });
		Level(TEXT("Global illumination"), S.GlobalIllumination, TEXT("Lumen's bounce light"));
		Level(TEXT("Reflections"), S.Reflections, TEXT("Lumen's reflections: paint, chrome, wet roads"));
		Level(TEXT("Effects"), S.Effects, TEXT("Spray, dust and smoke"));
		Level(TEXT("Grass and foliage"), S.Foliage, TEXT("How thick the grass and plants are, and how far"));
		{
			static const TCHAR* Fogs[] = {TEXT("Off"), TEXT("Low"), TEXT("High")};
			Setting(TEXT("Volumetric fog and light"), S.Fog < 0 ? FString(TEXT("As the preset")) : FString(Fogs[S.Fog]), TEXT("Light shafts and haze you can see"),
				[&S](int32 D) { S.Fog = FMath::Clamp(S.Fog + D, -1, 2); });
			static const TCHAR* CloudLevels[] = {TEXT("Off"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Ultra")};
			Setting(TEXT("Clouds"), S.Clouds < 0 ? FString(TEXT("As the preset")) : FString(CloudLevels[S.Clouds]), TEXT("The volumetric clouds: off, or finer the higher"),
				[&S](int32 D) { S.Clouds = FMath::Clamp(S.Clouds + D, -1, 4); });
		}
		Setting(TEXT("Ambient occlusion"), OnOff(S.bAmbientOcclusion), TEXT("Soft shade in corners and under things"),
			[&S](int32) { S.bAmbientOcclusion = !S.bAmbientOcclusion; });
		Setting(TEXT("Glow and bloom"), S.Bloom < 0.005f ? FString(TEXT("Off")) : Pct(S.Bloom), TEXT("How strong the glow round bright things is: off, to twice the usual"),
			[&S](int32 D) { S.Bloom = StepF(S.Bloom, D, 0.1f, 0.0f, 2.0f); });
		Setting(TEXT("Motion blur"), OnOff(S.bMotionBlur), FString(), [&S](int32) { S.bMotionBlur = !S.bMotionBlur; });
		Setting(TEXT("Avatar shadows"), OnOff(S.bAvatarShadows), TEXT("Players and people on foot cast shadows"),
			[&S](int32) { S.bAvatarShadows = !S.bAvatarShadows; });
		Add(TEXT("Graphics API"), TEXT("DirectX 12"), TEXT("Lumen, Nanite and the shadows need DirectX 12 here (Torque Shed's Vulkan choice has no Unreal match)"), nullptr);
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Sound:
		Setting(TEXT("Master volume"), Pct(S.Master), TEXT("Everything"), [&S](int32 D) { S.Master = StepF(S.Master, D, 0.1f, 0.0f, 1.0f); });
		Setting(TEXT("Sound effects volume"), Pct(S.Sfx), TEXT("Tools, knocks, dings, the washer, the shed's machines"),
			[&S](int32 D) { S.Sfx = StepF(S.Sfx, D, 0.1f, 0.0f, 1.0f); });
		Setting(TEXT("Engines volume"), Pct(S.Engine), TEXT("Your bike and everyone else's"), [&S](int32 D) { S.Engine = StepF(S.Engine, D, 0.1f, 0.0f, 1.0f); });
		Setting(TEXT("World volume"), Pct(S.Ambience), TEXT("Wind, birds, sheep and the shed's hum"),
			[&S](int32 D) { S.Ambience = StepF(S.Ambience, D, 0.1f, 0.0f, 1.0f); });
		Setting(TEXT("Radio volume"), Pct(S.Radio), TEXT("The shed radio"), [&S](int32 D) { S.Radio = StepF(S.Radio, D, 0.1f, 0.0f, 1.0f); });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Gameplay:
	{
		auto Heading = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
		Heading(TEXT("RIDING"));
		Setting(TEXT("Riding realism"), FString::Printf(TEXT("%.2f  %s"), S.Realism, *FTorqueSettings::RealismWord(S.Realism)),
			FTorqueSettings::RealismText(S.Realism) + TEXT("   (0 = full sim, 1 = easy; [ and ] while riding)"), [this, &S](int32 D) {
				S.Realism = StepF(S.Realism, D, 0.05f, 0.0f, 1.0f);
				if (ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn())) Bike->Assist = S.Realism;
			});
		Setting(TEXT("Automatic gearbox"), OnOff(S.bAutoShift), FString::Printf(TEXT("The gears change themselves (%s while riding)"), *FTorqueKeys::Text(ETorqueKey::AutoShift)),
			[&S](int32) { S.bAutoShift = !S.bAutoShift; });
		Setting(TEXT("Telemetry overlay"), OnOff(S.bTelemetry), FString::Printf(TEXT("Live strips of what the bike is doing, while riding (%s)"), *FTorqueKeys::Text(ETorqueKey::Telemetry)),
			[&S](int32) { S.bTelemetry = !S.bTelemetry; });
		Setting(TEXT("Look into corners"), OnOff(S.bLookIntoCorner), TEXT("Riders' heads turn towards the inside of a bend, and your view does in first person"),
			[&S](int32) { S.bLookIntoCorner = !S.bLookIntoCorner; });
		Heading(TEXT("CAMERA"));
		Setting(TEXT("Field of view"), FString::Printf(TEXT("%d degrees"), FMath::RoundToInt(S.Fov)),
			TEXT("60 to 110: on foot it's this; the bike's cameras widen or narrow by as much"),
			[&S](int32 D) { S.Fov = StepF(S.Fov, D, 1.0f, 60.0f, 110.0f); });
		Setting(TEXT("Chase camera recentres"), OnOff(S.bCameraRecentre), TEXT("The chase camera swings back behind the bike after you let go of the mouse or right stick"),
			[&S](int32) { S.bCameraRecentre = !S.bCameraRecentre; });
		Heading(TEXT("UNITS"));
		Setting(TEXT("Units"), S.Units == 0 ? FString(TEXT("Metric (km/h, km)")) : FString(TEXT("Imperial (mph, miles)")), TEXT("Speeds and distances on the HUD"),
			[&S](int32) { S.Units = 1 - S.Units; });
		Heading(TEXT("TIME AND WEATHER"));
		{
			ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
			const FString ByHost = bClient ? FString(TEXT("   (set by the host)")) : FString();
			auto Host = [&Setting, bClient](const FString& Label, const FString& Value, const FString& Hint, TFunction<void(int32)> Step) {
				Setting(Label, Value, Hint, bClient ? TFunction<void(int32)>([](int32) {}) : MoveTemp(Step));
			};
			Host(TEXT("Day/night cycle"), OnOff(S.bDayNight) + ByHost, TEXT("The clock runs, the sun crosses the sky and night falls"),
				[&S](int32) { S.bDayNight = !S.bDayNight; });
			{
				const TArray<int32>& Lengths = FTorqueSettings::DayLengths();
				const int32 At = FMath::Max(Lengths.IndexOfByKey(S.DayLength), 0);
				const int32 M = Lengths[At];
				const FString Text = M < 60 ? FString::Printf(TEXT("%d minutes"), M) : M == 60 ? FString(TEXT("1 hour")) : FString::Printf(TEXT("%d hours"), M / 60);
				Host(TEXT("Day length"), Text + ByHost, TEXT("Real time for a whole day, with the cycle on"),
					[&S, At](int32 D) { const TArray<int32>& L = FTorqueSettings::DayLengths(); S.DayLength = L[FMath::Clamp(At + D, 0, L.Num() - 1)]; });
			}
			{
				const float Built = GS ? GS->BuiltHour() : 15.0f;
				const float Now = GS ? GS->GetHour() : S.TimeOfDay;
				const FString Text = Now < 0.0f ? FString::Printf(TEXT("The level's own light (about %s)"), *ATorqueGameState::ClockText(Built))
												: ATorqueGameState::ClockText(Now);
				Host(TEXT("Time of day"), Text + ByHost, TEXT("Half an hour a step"), [GS, &S, Built, Now](int32 D) {
					const float From = Now < 0.0f ? FMath::RoundToFloat(Built * 2.0f) / 2.0f - 0.5f * D : FMath::RoundToFloat(Now * 2.0f) / 2.0f;
					S.TimeOfDay = FMath::Fmod(From + 0.5f * D + 24.0f, 24.0f);
					if (GS) GS->SetHour(S.TimeOfDay);
				});
				if (Now >= 0.0f && !bClient)
					Add(TEXT("Back to the level's own light"), FString(), TEXT("The sun where the level was made with it (the clock stops)"), [this, GS, &S]() {
						S.TimeOfDay = -1.0f;
						S.bDayNight = false;
						if (GS) GS->SetHour(-1.0f);
						Changed();
					});
			}
			{
				static const TCHAR* Kinds[] = {TEXT("Changes by itself"), TEXT("Always sunny"), TEXT("Always overcast"), TEXT("Always wet roads")};
				static const TCHAR* NowNames[] = {TEXT("sunny"), TEXT("overcast"), TEXT("wet roads")};
				const FString NowText = GS ? FString::Printf(TEXT("   now: %s"), NowNames[FMath::Clamp(GS->GetWeather(), 0, 2)]) : FString();
				Host(TEXT("Weather"), FString(Kinds[S.WeatherMode]) + NowText + ByHost, FString::Printf(TEXT("%s while riding changes it now"), *FTorqueKeys::Text(ETorqueKey::Weather)),
					[GS, &S](int32 D) {
						S.WeatherMode = Wrap(S.WeatherMode + D, 4);
						if (GS && S.WeatherMode > 0) GS->SetWeather(S.WeatherMode - 1);
					});
			}
		}
		Heading(TEXT("CO-OP"));
		Setting(TEXT("Open my router for internet friends"), OnOff(S.bOpenRouter),
			TEXT("Hosting asks your router to open UDP port 7777 (UPnP), and closes it again after; off: forward it by hand"),
			[&S](int32) { S.bOpenRouter = !S.bOpenRouter; });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Controller:
		Setting(TEXT("Mouse look speed"), FString::Printf(TEXT("%.2fx"), S.MouseLook), TEXT("0.1x to 10x the default, on foot and on the bike"),
			[&S](int32 D) { S.MouseLook = StepList(S.MouseLook, D, MouseSpeeds, UE_ARRAY_COUNT(MouseSpeeds)); });
		Setting(TEXT("Stick look speed"), FString::Printf(TEXT("%.2fx"), S.PadLook), TEXT("The right stick, on foot and on the bike"),
			[&S](int32 D) { S.PadLook = StepF(S.PadLook, D, 0.25f, 0.25f, 3.0f); });
		Setting(TEXT("Invert looking up and down"), OnOff(S.bInvertLook), FString(), [&S](int32) { S.bInvertLook = !S.bInvertLook; });
		Setting(TEXT("Lean stick dead zone"), Pct(S.StickDeadZone), TEXT("How far the stick moves before the bike leans"),
			[&S](int32 D) { S.StickDeadZone = StepF(S.StickDeadZone, D, 0.01f, 0.02f, 0.25f); });
		Setting(TEXT("Lean response"), FString::Printf(TEXT("%.1fx"), S.Steering), TEXT("How much lean the stick and the lean keys ask for"),
			[&S](int32 D) { S.Steering = StepF(S.Steering, D, 0.1f, 0.5f, 2.0f); });
		Add(TEXT("Key bindings"), FString(), TEXT("Every action and its keys: click one and press the new key"), [this]() { KeySlot = 0; Open(EPage::Keys); });
		Add(TEXT("Controller check"), FString(), TEXT("The sticks and buttons as the game reads them, and Recalibrate"), [this]() { Open(EPage::PadCheck); });
		{
			auto Info = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
			Info(TEXT("Pad, on foot:  left stick walk, right stick look, Cross / A use, D-pad left / right pick, Square / X wash, Triangle / Y ride out"));
			Info(TEXT("Pad, riding:  R2 throttle, L2 front brake, L1 rear brake, left stick lean and weight, Cross / Square gears, R1 camera"));
			Info(TEXT("Anywhere:  Esc, Options or Start opens the menu"));
		}
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Keys:
	{
		// Torque Shed's Key bindings tab (keybind_tab.gd).
		Add(TEXT("Reset to defaults"), FString(), TEXT("Every key back as it came"), [this]() {
			FTorqueKeys::ResetAll();
			ListenAction = -1;
		});
		uint8 Group = 0;
		for (int32 I = 0; I < (int32)ETorqueKey::Count; ++I)
		{
			const ETorqueKey A = (ETorqueKey)I;
			const FTorqueKeys::FAction& Act = FTorqueKeys::Action(A);
			if (Act.Where != Group)
			{
				Group = Act.Where;
				Add(Group == FTorqueKeys::Foot ? TEXT("ON FOOT") : Group == FTorqueKeys::Ride ? TEXT("RIDING") : TEXT("ON FOOT AND RIDING"), FString(), FString(), nullptr);
			}
			FString Value;
			if (ListenAction == I)
				Value = FString::Printf(TEXT("press the %s key or mouse button...   (Esc cancels, Delete clears it)"), ListenSlot == 0 ? TEXT("first") : TEXT("second"));
			else
			{
				for (int32 Slot = 0; Slot < FTorqueKeys::Slots; ++Slot)
				{
					const FString Name = FTorqueKeys::KeyName(FTorqueKeys::Get(A, Slot));
					const FString Shown = Name.IsEmpty() ? FString(TEXT("-")) : Name;
					Value += (Slot > 0 ? TEXT("   ") : TEXT("")) + (Slot == KeySlot ? FString::Printf(TEXT("[%s]"), *Shown) : Shown);
				}
				const FString Clash = FTorqueKeys::Clash(A);
				if (!Clash.IsEmpty()) Value += FString::Printf(TEXT("   CLASHES with %s"), *Clash);
			}
			Add(Act.Label, Value, TEXT("Click or Enter, then press the new key (Esc cancels, Delete clears it). < > picks the first or second key"),
				[this, I]() {
					ListenAction = I;
					ListenSlot = KeySlot;
					ListenFrame = GFrameCounter;
				},
				[this](int32 D) { KeySlot = Wrap(KeySlot + D, FTorqueKeys::Slots); });
		}
		Add(TEXT("The pad's buttons stay as they are; geometry mode has its own keys"), FString(), FString(), nullptr);
		Add(TEXT("Back"), FString(), FString(), [this]() { ListenAction = -1; Back(); });
		break;
	}
	case EPage::PadCheck:
	{
		// Torque Shed's Controller check: the numbers should follow the sticks; resting off centre, Recalibrate.
		auto Info = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
		Info(TEXT("Move the sticks and press buttons: the numbers should follow. If a stick shows a value while you're not touching it, let go and Recalibrate."));
		const FSonyPadState Sony = Pad ? Pad->Get() : FSonyPadState();
		const FSonyPadState Raw = Pad ? Pad->GetRaw() : FSonyPadState();
		if (Sony.bConnected)
		{
			Info(FString::Printf(TEXT("PlayStation pad:  left stick %+.2f, %+.2f   right stick %+.2f, %+.2f   L2 %.2f   R2 %.2f"), Sony.LX, Sony.LY, Sony.RX, Sony.RY, Sony.L2, Sony.R2));
			static const TCHAR* Names[] = {TEXT("Square"), TEXT("Cross"), TEXT("Circle"), TEXT("Triangle"), TEXT("L1"), TEXT("R1"), TEXT("Create"), TEXT("Options"),
				TEXT("L3"), TEXT("R3"), TEXT("Up"), TEXT("Right"), TEXT("Down"), TEXT("Left"), TEXT("PS")};
			TArray<FString> Held;
			for (int32 B = 0; B < (int32)UE_ARRAY_COUNT(Names); ++B)
				if (Sony.Buttons & (1u << B)) Held.Add(Names[B]);
			Info(TEXT("     buttons held:  ") + (Held.IsEmpty() ? FString(TEXT("none")) : FString::Join(Held, TEXT(", "))));
		}
		else
			Info(TEXT("PlayStation pad:  not found (plugged in? DSX or Steam can hide it)"));
		Info(FString::Printf(TEXT("Xbox-style pad:  left stick %+.2f, %+.2f   right stick %+.2f, %+.2f   triggers %.2f, %.2f"),
			GetInputAnalogKeyState(EKeys::Gamepad_LeftX), GetInputAnalogKeyState(EKeys::Gamepad_LeftY), GetInputAnalogKeyState(EKeys::Gamepad_RightX),
			GetInputAnalogKeyState(EKeys::Gamepad_RightY), GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis), GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis)));
		Add(TEXT("Recalibrate (hands off the controller)"), FString(), TEXT("Where the PlayStation pad's sticks rest now becomes their centre"), [this, &S, Raw]() {
			if (!Raw.bConnected) return;
			const float Rest[4] = {Raw.LX, Raw.LY, Raw.RX, Raw.RY};
			for (int32 I = 0; I < 4; ++I) S.PadCentre[I] = FMath::Clamp(Rest[I], -0.5f, 0.5f);
			FSonyPad::SetCentre(S.PadCentre);
			Changed();
		});
		Add(TEXT("Forget the calibration"), FString(), TEXT("The sticks' centres back to the pad's own"), [this, &S]() {
			for (int32 I = 0; I < 4; ++I) S.PadCentre[I] = 0.0f;
			FSonyPad::SetCentre(S.PadCentre);
			Changed();
		});
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Character:
	{
		FRow Name;
		Name.Label = TEXT("Name");
		Name.Value = (S.Name.IsEmpty() ? FString() : S.Name) + (FMath::Fmod(Clock, 1.0) < 0.5 ? TEXT("_") : TEXT(" "));
		Name.Hint = S.Name.IsEmpty() ? FString::Printf(TEXT("Type a name (empty: %s)"), Session ? *Session->PlayerName() : TEXT("your Windows name"))
									 : FString(TEXT("Shown over you in co-op; Enter keeps it, Ctrl+V pastes"));
		Name.bTyping = true;
		Name.Choose = [this]() { Changed(); };
		R.Add(MoveTemp(Name));
		Setting(TEXT("Colour"), FTorqueSettings::ColourNames()[S.Colour], TEXT("Your jacket, and your name over you"),
			[&S](int32 D) { S.Colour = Wrap(S.Colour + D, FTorqueSettings::Colours().Num()); });
		{
			const FTorqueSettings::FRiderStyle& St = FTorqueSettings::RiderStyles()[FMath::Clamp(S.RiderStyle, 0, FTorqueSettings::RiderStyles().Num() - 1)];
			Setting(TEXT("Riding style"), St.Name, FString(St.Desc) + TEXT(" Only looks: the bike rides the same."),
				[&S](int32 D) { S.RiderStyle = Wrap(S.RiderStyle + D, FTorqueSettings::RiderStyles().Num()); });
		}
		Add(TEXT("Profile"), FString(), TEXT("Your MetaHuman, height and face (also the picture top right of the Esc menu)"),
			[this]() { Changed(); Open(EPage::Profile); });   // [profile]
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Profile:   // [profile] Torque Shed's profile panel, with the MetaHuman's face
	{
		FTorqueProfile& Me = FTorqueProfile::Mine();
		const int32 NumAv = FTorqueAvatar::Count();
		// A slider (-1 .. 1, the middle the MetaHuman's own): dragged with the mouse, Left / Right
		// steps it. It changes your picture as it moves; letting go saves it and tells the others.
		auto Slide = [this, &R](const FString& Label, float& V, const FString& Value, const FString& Hint, bool bFace) {
			float* Ptr = &V;
			FRow Row;
			Row.Label = Label;
			Row.Value = Value;
			Row.Hint = Hint;
			Row.Step = [this, Ptr](int32 Dir) { *Ptr = StepF(*Ptr, Dir, 0.1f, -1.0f, 1.0f); ProfileChanged(); };
			Row.Frac = (FMath::Clamp(V, -1.0f, 1.0f) + 1.0f) * 0.5f;
			Row.SetFrac = [Ptr](float F) { *Ptr = FMath::Clamp(F * 2.0f - 1.0f, -1.0f, 1.0f); };
			Row.bFace = bFace;
			R.Add(MoveTemp(Row));
		};
		auto Heading = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
		if (NumAv == 0)
		{
			Heading(TEXT("No MetaHuman avatars in this build yet: in co-op you show as the shape body, in your colour."));
		}
		else
		{
			const ATorqueProfilePreview* Pv = Preview.Get();
			const FString Own = FTorqueAvatar::NameOf(Me.AvatarFor(S.Colour));
			Heading(TEXT("CHARACTER"));
			Add(TEXT("Who"), Me.Avatar < 0 ? FString::Printf(TEXT("%s (goes with my colour)"), *Own) : Own,
				TEXT("Which MetaHuman you are, in the shed and on the bike: everyone in co-op sees you as them"),
				[this, &Me, NumAv]() { Me.Avatar = Wrap(Me.Avatar + 2, NumAv + 1) - 1; ProfileChanged(); },
				[this, &Me, NumAv](int32 D) { Me.Avatar = Wrap(Me.Avatar + 1 + D, NumAv + 1) - 1; ProfileChanged(); });
			Slide(TEXT("Height"), Me.Height, Pv && Pv->Height() > 0.0 ? FString::Printf(TEXT("%.2f m"), Pv->Height() / 100.0) : FString::Printf(TEXT("%+.0f%%"), Me.Height * 6.0f),
				TEXT("Shorter or taller than the MetaHuman's own height, by up to 6%"), false);
			Heading(TEXT("FACE"));
			for (int32 F = 0; F < FTorqueProfile::NumFace; ++F)
				Slide(FTorqueProfile::Sliders[F].Name, Me.Face[F], FString::Printf(TEXT("%+.1f"), Me.Face[F]), FTorqueProfile::Sliders[F].Hint, true);
			Add(TEXT("Reset the face"), FString(), TEXT("Back to the MetaHuman's own face"), [this, &Me]() { Me.ResetFace(); ProfileChanged(); });
			R.Last().bFace = true;
		}
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	default: break;
	}
	return R;
}

void ATorquePlayerController::TypeText(APlayerController* PC, FString& Text, int32 Max)
{
	static const FKey Letters[26] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H,
		EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S,
		EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
	static const FKey Digits[10] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	if (!PC) return;
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::V))
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		Text = Clip.TrimStartAndEnd().Left(Max);
		return;
	}
	if (bCtrl) return;
	for (int32 L = 0; L < 26; ++L)
		if (PC->WasInputKeyJustPressed(Letters[L])) Text.AppendChar(TCHAR((bShift || Text.IsEmpty() ? 'A' : 'a') + L));
	for (int32 D = 0; D < 10; ++D)
		if (PC->WasInputKeyJustPressed(Digits[D])) Text.AppendChar(TCHAR('0' + D));
	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar) && !Text.IsEmpty()) Text.AppendChar(TEXT(' '));
	if (PC->WasInputKeyJustPressed(EKeys::Hyphen)) Text.AppendChar(bShift ? TEXT('_') : TEXT('-'));
	if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && Text.Len() > 0) Text.LeftChopInline(1);
	if (PC->WasInputKeyJustPressed(EKeys::Delete)) Text.Reset();
	Text = Text.Left(Max);
}

void ATorquePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) return;
	Clock += DeltaTime;
	NoteFor -= DeltaTime;
	SyncPlayerState(DeltaTime);
	// Settings > Graphics > Avatar shadows off: people who've come in since go without too.
	if ((ShadowCheckIn -= DeltaTime) <= 0.0)
	{
		ShadowCheckIn = 1.0;
		const FTorqueSettings& Set = FTorqueSettings::Get();
		if (!Set.bAvatarShadows) Set.ApplyAvatarShadows(GetWorld());
	}
	UpdateCursor();
	ReadMenuInput(DeltaTime);
	UpdateProfilePreview();   // [profile]
}

void ATorquePlayerController::UpdateCursor()
{
	// The pointer while a menu's up (the start menu or this one), for the mouse to work it; back to
	// the game's own (hidden, captured) when it closes, unless something else had it showing (geometry mode).
	const bool bWant = Page != EPage::None || Cast<ATorqueMenuPawn>(GetPawn()) != nullptr;
	if (bWant && !bCursorMine)
	{
		bCursorMine = true;
		if (!bShowMouseCursor)
		{
			bShowMouseCursor = true;
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			SetInputMode(Mode);
		}
		else
		{
			bCursorMine = false;   // already shown by someone else: theirs to hide
		}
	}
	else if (!bWant && bCursorMine)
	{
		bCursorMine = false;
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
}

bool ATorquePlayerController::MouseMoved(APlayerController* PC, FVector2D& Last, FVector2D& Now)
{
	float MX = 0.f, MY = 0.f;
	if (!PC || !PC->GetMousePosition(MX, MY))
	{
		Now = FVector2D(-1.0, -1.0);
		return false;
	}
	Now = FVector2D(MX, MY);
	const bool bMoved = Last.X >= 0.0 && FVector2D::DistSquared(Now, Last) > 1.0;
	Last = Now;
	return bMoved;
}

void ATorquePlayerController::ReadMenuInput(float Dt)
{
	const FSonyPadState Sony = Pad ? Pad->Get() : FSonyPadState();
	const uint32 Hit = Sony.Buttons & ~PrevButtons;
	PrevButtons = Sony.Buttons;
	auto Just = [this](const FKey& K) { return WasInputKeyJustPressed(K); };
	const bool bMenuKey = Just(EKeys::Escape) || Just(EKeys::Gamepad_Special_Right) || (Hit & ESonyButton::Options);

	if (Page == EPage::None)
	{
		if (bMenuKey && WantsPause()) OpenPause();
		return;
	}
	// Waiting for a key to bind (Settings > Controller > Key bindings): it has the keys to itself.
	if (ListenAction >= 0)
	{
		ReadKeyToBind();
		return;
	}

	TArray<FRow> List = Rows();
	if (List.IsEmpty()) return;
	Selected = FMath::Clamp(Selected, 0, List.Num() - 1);
	const bool bTyping = List[Selected].bTyping;

	// Up / down, left / right: keys, D-pads, and the left stick (repeating while held).
	auto StickAxis = [&](float Ps, const FKey& Xb) {
		const float X = GetInputAnalogKeyState(Xb);
		return FMath::Abs(Ps) > FMath::Abs(X) ? Ps : X;
	};
	auto Repeat = [Dt](float V, int32& Dir, double& Timer) {
		const int32 Now = V > 0.6f ? 1 : (V < -0.6f ? -1 : 0);
		if (Now != Dir)
		{
			Dir = Now;
			Timer = 0.35;
			return Now;
		}
		if (Now != 0 && (Timer -= Dt) <= 0.0)
		{
			Timer = 0.12;
			return Now;
		}
		return 0;
	};
	int32 MoveY = Repeat(-StickAxis(Sony.LY, EKeys::Gamepad_LeftY), StickY, RepeatY);
	int32 MoveX = Repeat(StickAxis(Sony.LX, EKeys::Gamepad_LeftX), StickX, RepeatX);
	if (Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (Hit & ESonyButton::DUp)) MoveY = -1;
	if (Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (Hit & ESonyButton::DDown)) MoveY = 1;
	if (Just(EKeys::Left) || Just(EKeys::Gamepad_DPad_Left) || (Hit & ESonyButton::DLeft)) MoveX = -1;
	if (Just(EKeys::Right) || Just(EKeys::Gamepad_DPad_Right) || (Hit & ESonyButton::DRight)) MoveX = 1;
	// Settings' tabs: Q / E, the bumpers (L1 / R1, LB / RB), or Left / Right on a row that isn't a setting.
	if (TabIndex() >= 0)
	{
		int32 Tab = 0;
		if (!bTyping && (Just(EKeys::Q) || Just(EKeys::PageUp))) Tab = -1;
		if (!bTyping && (Just(EKeys::E) || Just(EKeys::PageDown))) Tab = 1;
		if (Just(EKeys::Gamepad_LeftShoulder) || (Hit & ESonyButton::L1)) Tab = -1;
		if (Just(EKeys::Gamepad_RightShoulder) || (Hit & ESonyButton::R1)) Tab = 1;
		if (MoveY == 0 && MoveX != 0 && !List[Selected].Step && !bTyping) Tab = MoveX;
		if (Tab != 0)
		{
			SwitchTab(Tab);
			return;
		}
	}
	// The mouse: over a row picks it, a click chooses it or steps it (its arrows, or the next value),
	// a click on a tab opens it, the wheel moves, the right button goes back.
	{
		FVector2D Mouse;
		const bool bMoved = MouseMoved(this, LastMouse, Mouse);
		const FTorqueMenuHits::FHit* Over = Hits.At(Mouse);
		if (bMoved && DragRow < 0 && Over && Over->Kind != FTorqueMenuHits::Tab && Over->Kind != FTorqueMenuHits::Badge && List.IsValidIndex(Over->Index) && Over->Index != Selected)
		{
			if (bTyping) Changed();   // leaving the name keeps it
			Selected = Over->Index;
		}
		if (Just(EKeys::MouseScrollUp)) MoveY = -1;
		if (Just(EKeys::MouseScrollDown)) MoveY = 1;
		// [profile] A slider dragged: the value follows the pointer along its track until let go.
		if (DragRow >= 0)
		{
			if (IsInputKeyDown(EKeys::LeftMouseButton) && List.IsValidIndex(DragRow) && List[DragRow].SetFrac && Mouse.X >= 0.0)
			{
				List[DragRow].SetFrac(FMath::Clamp((float)((Mouse.X - DragX0) / FMath::Max(DragX1 - DragX0, 1.0)), 0.0f, 1.0f));
				return;
			}
			DragRow = -1;
			ProfileChanged();
			return;
		}
		if (Just(EKeys::LeftMouseButton) && Over && Over->Kind == FTorqueMenuHits::Slider && List.IsValidIndex(Over->Index))
		{
			Selected = DragRow = Over->Index;
			DragX0 = Over->Box.Min.X;
			DragX1 = Over->Box.Max.X;
			if (List[DragRow].SetFrac) List[DragRow].SetFrac(FMath::Clamp((float)((Mouse.X - DragX0) / FMath::Max(DragX1 - DragX0, 1.0)), 0.0f, 1.0f));
			return;
		}
		if (Just(EKeys::LeftMouseButton) && Over)
		{
			if (Over->Kind == FTorqueMenuHits::Badge)   // [profile] the picture top right: your profile, or back from it
			{
				if (bTyping) Changed();
				if (Page == EPage::Profile) Back();
				else Open(EPage::Profile);
				return;
			}
			if (Over->Kind == FTorqueMenuHits::Tab)
			{
				if (Over->Index != TabIndex()) SwitchTab(Over->Index - TabIndex());
				return;
			}
			if (List.IsValidIndex(Over->Index))
			{
				Selected = Over->Index;
				FRow& Row = List[Selected];
				if (Over->Kind == FTorqueMenuHits::Less && Row.Step) Row.Step(-1);
				else if (Over->Kind == FTorqueMenuHits::More && Row.Step) Row.Step(1);
				else if (Row.Choose) Row.Choose();
				else if (Row.Step) Row.Step(1);
				return;
			}
		}
		if (Just(EKeys::RightMouseButton))
		{
			if (bTyping) Changed();
			Back();
			return;
		}
	}
	if (MoveY != 0) Selected = Wrap(Selected + MoveY, List.Num());
	else if (MoveX != 0 && List[Selected].Step) List[Selected].Step(MoveX);

	if (bTyping) TypeText(this, FTorqueSettings::Get().Name, 20);
	const bool bChoose = Just(EKeys::Enter) || (!bTyping && Just(EKeys::SpaceBar)) || Just(EKeys::Gamepad_FaceButton_Bottom) ||
						 (Hit & ESonyButton::Cross);
	const bool bBack = (Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (Hit & ESonyButton::Circle) ||
						   (!bTyping && Just(EKeys::BackSpace)));
	if (bTyping && (bBack || MoveY != 0)) Changed();   // leaving the name keeps it
	if (bChoose && List[Selected].Choose) List[Selected].Choose();
	else if (Just(EKeys::Gamepad_Special_Right) || (Hit & ESonyButton::Options)) CloseMenu();
	else if (bBack) Back();
}

void ATorquePlayerController::ReadKeyToBind()
{
	if (Page != EPage::Keys || ListenAction < 0 || ListenAction >= (int32)ETorqueKey::Count)
	{
		ListenAction = -1;
		return;
	}
	if (GFrameCounter == ListenFrame) return;   // the click or Enter that started it
	const FSonyPadState Sony = Pad ? Pad->Get() : FSonyPadState();
	const bool bPadBack = (Sony.Buttons & ESonyButton::Circle) != 0 || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right);
	if (WasInputKeyJustPressed(EKeys::Escape) || bPadBack)
	{
		ListenAction = -1;
		ClosedFrame = GFrameCounter;   // (and Esc doesn't also close the page)
		return;
	}
	const ETorqueKey A = (ETorqueKey)ListenAction;
	if (WasInputKeyJustPressed(EKeys::Delete))
	{
		FTorqueKeys::Set(A, ListenSlot, FKey());
		ListenAction = -1;
		return;
	}
	static TArray<FKey> Candidates;
	if (Candidates.IsEmpty())
	{
		TArray<FKey> All;
		EKeys::GetAllKeys(All);
		for (const FKey& K : All)
			if (FTorqueKeys::Bindable(K)) Candidates.Add(K);
	}
	for (const FKey& K : Candidates)
		if (WasInputKeyJustPressed(K))
		{
			FTorqueKeys::Set(A, ListenSlot, K);
			UE_LOG(LogTemp, Log, TEXT("Torque: key binding: %s slot %d = %s"), FTorqueKeys::Action(A).Id, ListenSlot + 1, *K.ToString());
			ListenAction = -1;
			return;
		}
}

void ATorquePlayerController::DrawMenu(AHUD* Hud, UCanvas* Canvas)
{
	Hits.Reset();
	if (Page == EPage::None || !Hud || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetLargeFont();
	const float Sc = Canvas->ClipY / 1080.0f;
	Hud->DrawRect(FLinearColor(0, 0, 0, 0.45f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	const float X = 120.0f * Sc;
	float Y = 120.0f * Sc;
	auto Line = [&](const FString& Text, const FColor& Colour, float Size, float Box) {
		float W = 0, H = 0;
		Canvas->StrLen(Font, Text, W, H);
		const float K = Sc * Size;
		if (Box > 0.0f) Hud->DrawRect(FLinearColor(0, 0, 0, Box), X - 16.0f * Sc, Y - 6.0f * Sc, W * K + 32.0f * Sc, H * K + 12.0f * Sc);
		Canvas->SetDrawColor(Colour);
		Canvas->DrawText(Font, Text, X, Y, K, K);
		Y += H * K + 14.0f * Sc;
	};
	Line(Title(), FColor::White, 2.6f, 0.45f);
	if (TabIndex() >= 0)
	{
		// The tab bar: the one open bright, the others dim.
		float TX = X;
		for (int32 T = 0; T < 5; ++T)
		{
			const bool bOn = T == TabIndex();
			const FString Tab = FString::Printf(TEXT("  %s  "), TabNames[T]);
			float W = 0, H = 0;
			Canvas->StrLen(Font, Tab, W, H);
			const float K = Sc * 1.3f;
			Hud->DrawRect(bOn ? FLinearColor(0.85f, 0.65f, 0.25f, 0.85f) : FLinearColor(0, 0, 0, 0.45f), TX, Y - 4.0f * Sc, W * K, H * K + 8.0f * Sc);
			Hits.Add(TX, Y - 4.0f * Sc, TX + W * K, Y + H * K + 4.0f * Sc, T, FTorqueMenuHits::Tab);
			Canvas->SetDrawColor(bOn ? FColor(20, 20, 20) : FColor(220, 220, 220));
			Canvas->DrawText(Font, Tab, TX, Y, K, K);
			TX += W * K + 8.0f * Sc;
			if (T == 4) Y += H * K + 18.0f * Sc;
		}
		Line(TEXT("Click a tab, or Q / E, L1 / R1 or LB / RB: the other tabs"), FColor(190, 190, 190), 0.85f, 0.0f);
	}
	// As Torque Shed's Esc menu: hosting, exactly what to tell friends; who's here.
	if (Page == EPage::Pause)
	{
		const UTorqueSession* Session = UTorqueSession::Get(this);
		if (GetNetMode() == NM_ListenServer && Session)
		{
			Line(FString::Printf(TEXT("Hosting on port %d."), UTorqueSession::GamePort()), FColor(170, 230, 255), 1.0f, 0.4f);
			for (const FString& L : Session->JoinInstructions()) Line(L, FColor(170, 230, 255), 1.0f, 0.4f);
		}
		else if (GetNetMode() == NM_Client)
			Line(TEXT("Connected to a co-op shed."), FColor(170, 230, 255), 1.0f, 0.4f);
		else
			Line(TEXT("Working solo."), FColor(200, 200, 200), 1.0f, 0.35f);
		if (GetNetMode() != NM_Standalone)
			if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
			{
				TArray<FString> Names;
				for (const APlayerState* PS : GS->PlayerArray)
					if (PS) Names.Add(PS->GetPlayerName());
				Line(TEXT("In the shed: ") + FString::Join(Names, TEXT(", ")), FColor(220, 220, 220), 1.0f, 0.35f);
			}
	}
	Y += 24.0f * Sc;
	const TArray<FRow> List = Rows();
	// A page longer than the screen scrolls: the rows from FirstRow, keeping the one picked in view.
	float FontH = 0.0f, FontW = 0.0f;
	Canvas->StrLen(Font, TEXT("Ag"), FontW, FontH);
	auto RowHeight = [&](int32 I) {
		if (List[I].Frac >= 0.0f) return 46.0f * Sc;
		const bool bInfo = !List[I].Choose && !List[I].Step;
		return FontH * Sc * (bInfo ? 1.0f : (I == Selected ? 1.4f : 1.25f)) + 14.0f * Sc;
	};
	const float RowsTop = Y, RowsBottom = Canvas->ClipY - 210.0f * Sc;
	const float MoreH = FontH * Sc * 0.85f + 14.0f * Sc;   // a "more" line, above or below
	if (List.Num() > 0)
	{
		const int32 Sel = FMath::Clamp(Selected, 0, List.Num() - 1);
		FirstRow = FMath::Clamp(FirstRow, 0, List.Num() - 1);
		if (Sel < FirstRow) FirstRow = Sel;
		auto Span = [&](int32 From, int32 To) {
			float H = From > 0 ? MoreH : 0.0f;
			for (int32 I = From; I <= To; ++I) H += RowHeight(I);
			return H + (To < List.Num() - 1 ? MoreH : 0.0f);
		};
		while (FirstRow < Sel && Span(FirstRow, Sel) > RowsBottom - RowsTop) ++FirstRow;
		// (and no empty space at the bottom when it's scrolled down)
		while (FirstRow > 0 && Span(FirstRow - 1, List.Num() - 1) <= RowsBottom - RowsTop) --FirstRow;
	}
	if (FirstRow > 0) Line(TEXT("    ... more above (scroll up)"), FColor(170, 170, 170), 0.85f, 0.0f);
	int32 LastShown = List.Num() - 1;
	for (int32 I = FirstRow; I < List.Num(); ++I)
	{
		if (Y + RowHeight(I) + (I < List.Num() - 1 ? MoreH : 0.0f) > RowsBottom && I > FirstRow && I > Selected)
		{
			LastShown = I - 1;
			break;
		}
		if (List[I].Frac >= 0.0f)
		{
			// [profile] A slider row: the name, a track with the middle marked, filled from the middle
			// to the knob, and the value; the track is what the mouse drags.
			const bool bOn = I == Selected || I == DragRow;
			const float RowH = 40.0f * Sc, K = Sc * (bOn ? 1.15f : 1.05f);
			Hud->DrawRect(FLinearColor(0, 0, 0, bOn ? 0.6f : 0.35f), X - 16.0f * Sc, Y - 6.0f * Sc, 900.0f * Sc, RowH);
			if (bOn) Hud->DrawRect(FLinearColor(0.85f, 0.65f, 0.25f, 0.9f), X - 16.0f * Sc, Y - 6.0f * Sc, 4.0f * Sc, RowH);
			Canvas->SetDrawColor(bOn ? FColor(255, 220, 120) : FColor(235, 235, 235));
			Canvas->DrawText(Font, List[I].Label, X, Y, K, K);
			const float TX0 = X + 300.0f * Sc, TX1 = X + 700.0f * Sc, TY = Y + 12.0f * Sc;
			const float Mid = 0.5f * (TX0 + TX1), Knob = FMath::Lerp(TX0, TX1, FMath::Clamp(List[I].Frac, 0.0f, 1.0f));
			Hud->DrawRect(FLinearColor(1, 1, 1, 0.18f), TX0, TY, TX1 - TX0, 6.0f * Sc);
			Hud->DrawRect(FLinearColor(0.85f, 0.65f, 0.25f, bOn ? 0.95f : 0.7f), FMath::Min(Mid, Knob), TY, FMath::Abs(Knob - Mid), 6.0f * Sc);
			Hud->DrawRect(FLinearColor(1, 1, 1, 0.5f), Mid - 1.0f * Sc, TY - 6.0f * Sc, 2.0f * Sc, 18.0f * Sc);
			Hud->DrawRect(bOn ? FLinearColor(1.0f, 0.86f, 0.5f, 1.0f) : FLinearColor(0.92f, 0.92f, 0.92f, 1.0f), Knob - 7.0f * Sc, TY - 9.0f * Sc, 14.0f * Sc, 24.0f * Sc);
			Canvas->SetDrawColor(FColor(210, 210, 210));
			Canvas->DrawText(Font, List[I].Value, TX1 + 24.0f * Sc, Y, Sc, Sc);
			Hits.Add(X - 16.0f * Sc, Y - 6.0f * Sc, X + 884.0f * Sc, Y - 6.0f * Sc + RowH, I, FTorqueMenuHits::Row);
			Hits.Add(TX0, Y - 6.0f * Sc, TX1, Y - 6.0f * Sc + RowH, I, FTorqueMenuHits::Slider);
			Y += RowH + 6.0f * Sc;
			continue;
		}
		const bool bSel = I == Selected;
		FString Text = (bSel ? TEXT(">  ") : TEXT("    ")) + List[I].Label;
		if (!List[I].Value.IsEmpty()) Text += List[I].Step && bSel ? FString::Printf(TEXT(":   <  %s  >"), *List[I].Value) : FString::Printf(TEXT(":   %s"), *List[I].Value);
		const bool bInfo = !List[I].Choose && !List[I].Step;
		const float Size = bInfo ? 1.0f : (bSel ? 1.4f : 1.25f);
		const float Top = Y;
		Line(Text, bSel ? FColor(255, 220, 120) : (bInfo ? FColor(210, 210, 210) : FColor(235, 235, 235)), Size, bSel ? 0.6f : 0.35f);
		// Where it went, for the mouse: the row's box (the width of the menu, easy to hit), and a
		// setting's < and > when they're drawn.
		const float K = Sc * Size, Bottom = Y - 8.0f * Sc;
		if (!bInfo) Hits.Add(X - 16.0f * Sc, Top - 6.0f * Sc, X + 900.0f * Sc, Bottom, I, FTorqueMenuHits::Row);
		int32 Less = INDEX_NONE, More = INDEX_NONE;
		if (bSel && List[I].Step && Text.FindChar(TEXT('<'), Less) && Text.FindLastChar(TEXT('>'), More) && More > Less)
		{
			float WL = 0, WM = 0, WA = 0, H = 0;
			Canvas->StrLen(Font, Text.Left(Less), WL, H);
			Canvas->StrLen(Font, Text.Left(More), WM, H);
			Canvas->StrLen(Font, TEXT(">"), WA, H);
			Hits.Add(X + WL * K - 14.0f * Sc, Top - 6.0f * Sc, X + (WL + WA) * K + 14.0f * Sc, Bottom, I, FTorqueMenuHits::Less);
			Hits.Add(X + WM * K - 14.0f * Sc, Top - 6.0f * Sc, X + (WM + WA) * K + 14.0f * Sc, Bottom, I, FTorqueMenuHits::More);
		}
	}
	if (LastShown < List.Num() - 1) Line(TEXT("    ... more below (scroll down)"), FColor(170, 170, 170), 0.85f, 0.0f);
	Y += 16.0f * Sc;
	if (List.IsValidIndex(Selected) && !List[Selected].Hint.IsEmpty()) Line(List[Selected].Hint, FColor(190, 210, 240), 1.0f, 0.35f);
	Y = Canvas->ClipY - 90.0f * Sc;
	Line(TEXT("Mouse: click to choose, < > to change, right-click back   Keys: Up/Down, Left/Right, Enter, Esc   Pad: Cross or A, Circle or B"), FColor(200, 200, 200), 0.9f, 0.35f);
	DrawProfile(Hud, Canvas);   // [profile]
}

// [profile]
void ATorquePlayerController::ProfileChanged()
{
	const FTorqueProfile& Me = FTorqueProfile::Mine();
	Me.Save();
	if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>()) PS->ServerSetProfile(Me.Pack());
}

void ATorquePlayerController::UpdateProfilePreview()
{
	// Filmed while the pause menu (its badge) or the Profile page is up.
	if (Page != EPage::Pause && Page != EPage::Profile) return;
	if (!Preview.IsValid()) Preview = ATorqueProfilePreview::For(this);
	bool bFace = true;
	if (Page == EPage::Profile)
	{
		const TArray<FRow> List = Rows();
		const int32 At = DragRow >= 0 ? DragRow : Selected;
		bFace = List.IsValidIndex(At) && List[At].bFace;
	}
	if (ATorqueProfilePreview* Pv = Preview.Get()) Pv->Want(bFace, FTorqueSettings::Get().Colour);
}

void ATorquePlayerController::DrawProfile(AHUD* Hud, UCanvas* Canvas)
{
	if (Page != EPage::Pause && Page != EPage::Profile) return;
	UFont* Font = GEngine->GetLargeFont();
	const float Sc = Canvas->ClipY / 1080.0f;
	const ATorqueProfilePreview* Pv = Preview.Get();
	// The badge, top right: your picture, your name, and which MetaHuman you are.
	const float BW = 400.0f * Sc, BH = 88.0f * Sc, BX = Canvas->ClipX - BW - 28.0f * Sc, BY = 28.0f * Sc, Pic = BH - 16.0f * Sc;
	Hud->DrawRect(Page == EPage::Profile ? FLinearColor(0.85f, 0.65f, 0.25f, 0.85f) : FLinearColor(0, 0, 0, 0.6f), BX, BY, BW, BH);
	if (Pv && Pv->HasAvatar())
		Pv->Draw(Canvas, BX + 8.0f * Sc, BY + 8.0f * Sc, Pic, Pic, true);
	else
		Hud->DrawRect(FLinearColor(FTorqueSettings::Get().RiderColour()), BX + 8.0f * Sc, BY + 8.0f * Sc, Pic, Pic);
	const FTorqueSettings& S = FTorqueSettings::Get();
	UTorqueSession* Session = UTorqueSession::Get(this);
	const FString Who = !S.Name.IsEmpty() ? S.Name : (Session ? Session->PlayerName() : FString(TEXT("You")));
	const FString What = Pv && Pv->HasAvatar() ? FString::Printf(TEXT("%s, about %.2f m"), *Pv->AvatarName(), Pv->Height() / 100.0)
											   : FString(TEXT("Your profile and character"));
	const FColor Ink = Page == EPage::Profile ? FColor(20, 20, 20) : FColor::White;
	Canvas->SetDrawColor(Ink);
	Canvas->DrawText(Font, Who, BX + Pic + 22.0f * Sc, BY + 12.0f * Sc, 1.3f * Sc, 1.3f * Sc);
	Canvas->SetDrawColor(Page == EPage::Profile ? FColor(40, 40, 40) : FColor(200, 200, 200));
	Canvas->DrawText(Font, What, BX + Pic + 22.0f * Sc, BY + 50.0f * Sc, 0.95f * Sc, 0.95f * Sc);
	Hits.Add(BX, BY, BX + BW, BY + BH, 0, FTorqueMenuHits::Badge);
	// The Profile page: you, big, beside the sliders.
	if (Page == EPage::Profile && Pv && Pv->HasAvatar())
	{
		const float H = Canvas->ClipY - BY - BH - 160.0f * Sc, W = H * 0.75f;
		const float X = Canvas->ClipX - W - 60.0f * Sc, Y = BY + BH + 30.0f * Sc;
		Hud->DrawRect(FLinearColor(0, 0, 0, 0.6f), X - 6.0f * Sc, Y - 6.0f * Sc, W + 12.0f * Sc, H + 12.0f * Sc);
		Pv->Draw(Canvas, X, Y, W, H, false);
	}
}

void ATorquePlayerController::DrawNote(AHUD* Hud, UCanvas* Canvas)
{
	if (NoteFor <= 0.0 || Note.IsEmpty() || !Hud || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetLargeFont();
	const float Sc = Canvas->ClipY / 1080.0f;
	float W = 0, H = 0;
	Canvas->StrLen(Font, Note, W, H);
	const float X = 0.5f * (Canvas->ClipX - W * Sc), Y = Canvas->ClipY * 0.62f;
	Hud->DrawRect(FLinearColor(0, 0, 0, 0.45f), X - 12.0f * Sc, Y - 5.0f * Sc, W * Sc + 24.0f * Sc, H * Sc + 10.0f * Sc);
	Canvas->SetDrawColor(FColor(255, 190, 120));
	Canvas->DrawText(Font, Note, X, Y, Sc, Sc);
}

void ATorquePlayerController::PlaceOnHost(APawn* Pawn, const FVector& At, const FRotator& View)
{
	if (!Pawn || Pawn->HasAuthority()) return;
	if (ATorquePlayerController* PC = Cast<ATorquePlayerController>(Pawn->GetController())) PC->ServerPlace(At, View);
}

void ATorquePlayerController::ServerPlace_Implementation(FVector At, FRotator View)
{
	APawn* P = GetPawn();
	if (!P) return;
	P->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
	SetControlRotation(View);
	UE_LOG(LogTemp, Log, TEXT("Torque net: %s put at %s (from its own machine)"), *P->GetName(), *At.ToCompactString());
}
