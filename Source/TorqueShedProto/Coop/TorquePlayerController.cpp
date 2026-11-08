#include "TorquePlayerController.h"

#include "Shed/TorqueGeometryMode.h"
#include "TorqueMenuPawn.h"
#include "TorquePlayerState.h"
#include "TorqueSession.h"
#include "TorqueSettings.h"
#include "TorqueBikePawn.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueWalker.h"
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
}

void ATorquePlayerController::Back()
{
	if (Stack.Num() > 0)
	{
		Page = Stack.Pop();
		Selected = 0;
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

bool ATorquePlayerController::WantsPause()
{
	// On the bike or on foot; the start menu has its own Esc, and so has geometry mode.
	if (ATorqueGeometryMode::For(this)) return false;
	const APawn* P = GetPawn();
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
}

FString ATorquePlayerController::Title() const
{
	if (Page == EPage::Pause) return TEXT("PAUSED");
	return TabIndex() >= 0 ? TEXT("SETTINGS") : FString();
}

void ATorquePlayerController::Changed()
{
	FTorqueSettings& S = FTorqueSettings::Get();
	S.Save();
	S.Apply(GetWorld());
	if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>()) PS->ServerSetLook(S.Name, (uint8)S.Colour);
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
		Add(TEXT("Graphics"), FString(), TEXT("Quality preset, window mode, resolution, VSync, frame-rate cap, upscaling"), [this]() { Open(EPage::Graphics); });
		Add(TEXT("Sound"), FString(), TEXT("Sound effects and engines volumes"), [this]() { Open(EPage::Sound); });
		Add(TEXT("Gameplay"), FString(), TEXT("Riding realism"), [this]() { Open(EPage::Gameplay); });
		Add(TEXT("Controller"), FString(), TEXT("Look speeds, invert, the lean stick, and every key and button"), [this]() { Open(EPage::Controller); });
		Add(TEXT("Character"), FString(), TEXT("Your name and colour, as everyone in co-op sees them"), [this]() { Open(EPage::Character); });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Graphics:
	{
		static const TCHAR* Qualities[] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic")};
		static const TCHAR* Modes[] = {TEXT("Fullscreen"), TEXT("Borderless window"), TEXT("Window")};
		static const TCHAR* Scales[] = {TEXT("Native (100%)"), TEXT("Quality (67%)"), TEXT("Balanced (58%)"), TEXT("Performance (50%)"),
			TEXT("Ultra performance (33%)")};
		Setting(TEXT("Quality preset"), Qualities[S.Quality], TEXT("Unreal's scalability: shadows, Lumen, foliage, effects"),
			[&S](int32 D) { S.Quality = FMath::Clamp(S.Quality + D, 0, 4); });
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
		const bool bDlss = FTorqueSettings::HasDlss();
		Setting(TEXT("Upscaler"), S.Upscaler == 1 && bDlss ? FString(TEXT("NVIDIA DLSS")) : FString(TEXT("Unreal TSR")),
			bDlss ? TEXT("DLSS on an RTX card: sharper for the same frame rate") : TEXT("DLSS needs NVIDIA's DLSS plugin in the build (on its way)"),
			[&S, bDlss](int32 D) { S.Upscaler = bDlss ? Wrap(S.Upscaler + D, 2) : 0; });
		const bool bDlssOn = S.Upscaler == 1 && FTorqueSettings::HasDlss();
		Setting(TEXT("Render scale"), bDlssOn && S.UpscaleMode == 0 ? FString(TEXT("DLAA (100%)")) : FString(Scales[S.UpscaleMode]), TEXT("Lower draws fewer pixels and upscales: more frames"),
			[&S](int32 D) { S.UpscaleMode = FMath::Clamp(S.UpscaleMode + D, 0, 4); });
		int32 CapAt = 0;
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(FrameCaps); ++I)
			if (FrameCaps[I] == S.FrameCap) CapAt = I;
		Setting(TEXT("Frame-rate cap"), S.FrameCap == 0 ? FString(TEXT("Off")) : FString::Printf(TEXT("%d fps"), S.FrameCap), FString(),
			[&S, CapAt](int32 D) { S.FrameCap = FrameCaps[Wrap(CapAt + D, (int32)UE_ARRAY_COUNT(FrameCaps))]; });
		Setting(TEXT("VSync"), OnOff(S.bVSync), TEXT("No tearing, but frames wait for the screen"), [&S](int32) { S.bVSync = !S.bVSync; });
		Setting(TEXT("Motion blur"), OnOff(S.bMotionBlur), FString(), [&S](int32) { S.bMotionBlur = !S.bMotionBlur; });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Sound:
		Setting(TEXT("Sound effects volume"), Pct(S.Ambience), TEXT("Wind, birds and the world"),
			[&S](int32 D) { S.Ambience = StepF(S.Ambience, D, 0.1f, 0.0f, 1.0f); });
		Setting(TEXT("Engines volume"), Pct(S.Engine), TEXT("Your bike and everyone else's"), [&S](int32 D) { S.Engine = StepF(S.Engine, D, 0.1f, 0.0f, 1.0f); });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Gameplay:
		Setting(TEXT("Riding realism"), FString::Printf(TEXT("%.2f  %s"), S.Realism, *FTorqueSettings::RealismWord(S.Realism)),
			FTorqueSettings::RealismText(S.Realism) + TEXT("   (0 = full sim, 1 = easy; [ and ] while riding)"), [this, &S](int32 D) {
				S.Realism = StepF(S.Realism, D, 0.05f, 0.0f, 1.0f);
				if (ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn())) Bike->Assist = S.Realism;
			});
		Setting(TEXT("Open my router for internet friends"), OnOff(S.bOpenRouter),
			TEXT("Hosting asks your router to open UDP port 7777 (UPnP), and closes it again after; off: forward it by hand"),
			[&S](int32) { S.bOpenRouter = !S.bOpenRouter; });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Controller:
		Setting(TEXT("Mouse look speed"), FString::Printf(TEXT("%.2fx"), S.MouseLook), FString(),
			[&S](int32 D) { S.MouseLook = StepF(S.MouseLook, D, 0.25f, 0.25f, 3.0f); });
		Setting(TEXT("Stick look speed"), FString::Printf(TEXT("%.2fx"), S.PadLook), TEXT("The right stick, on foot and on the bike"),
			[&S](int32 D) { S.PadLook = StepF(S.PadLook, D, 0.25f, 0.25f, 3.0f); });
		Setting(TEXT("Invert looking up and down"), OnOff(S.bInvertLook), FString(), [&S](int32) { S.bInvertLook = !S.bInvertLook; });
		Setting(TEXT("Lean stick dead zone"), Pct(S.StickDeadZone), TEXT("How far the stick moves before the bike leans"),
			[&S](int32 D) { S.StickDeadZone = StepF(S.StickDeadZone, D, 0.01f, 0.02f, 0.25f); });
		Setting(TEXT("Lean response"), FString::Printf(TEXT("%.1fx"), S.Steering), TEXT("How much lean the stick and the A/D keys ask for"),
			[&S](int32 D) { S.Steering = StepF(S.Steering, D, 0.1f, 0.5f, 2.0f); });
		{
			// Every key and button (Torque Shed lists them on this tab too).
			auto Info = [&Add](const FString& Text) { Add(Text, FString(), FString(), nullptr); };
			Info(TEXT("On foot:  WASD walk, Shift run, mouse look, E or click use, Q / R pick, G wash, F ride out, Space hop"));
			Info(TEXT("     Pad:  left stick walk, right stick look, Cross / A use, D-pad left / right pick, Square / X wash, Triangle / Y ride out"));
			Info(TEXT("Riding:  W throttle, S front brake, Space rear brake, A / D lean, Q / E weight, X / Z gears, G auto / manual"));
			Info(TEXT("     B ABS, C camera, R back on the road, Home restart, [ ] realism, V weather, Backspace back to the shed, H help"));
			Info(TEXT("     Pad:  R2 throttle, L2 front brake, L1 rear brake, left stick lean and weight, Cross / Square gears, R1 camera"));
			Info(TEXT("Anywhere:  Esc, Options or Start opens the menu"));
		}
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
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
	ReadMenuInput(DeltaTime);
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

void ATorquePlayerController::DrawMenu(AHUD* Hud, UCanvas* Canvas)
{
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
			Canvas->SetDrawColor(bOn ? FColor(20, 20, 20) : FColor(220, 220, 220));
			Canvas->DrawText(Font, Tab, TX, Y, K, K);
			TX += W * K + 8.0f * Sc;
			if (T == 4) Y += H * K + 18.0f * Sc;
		}
		Line(TEXT("Q / E, L1 / R1 or LB / RB: the other tabs"), FColor(190, 190, 190), 0.85f, 0.0f);
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
	for (int32 I = 0; I < List.Num(); ++I)
	{
		const bool bSel = I == Selected;
		FString Text = (bSel ? TEXT(">  ") : TEXT("    ")) + List[I].Label;
		if (!List[I].Value.IsEmpty()) Text += List[I].Step && bSel ? FString::Printf(TEXT(":   <  %s  >"), *List[I].Value) : FString::Printf(TEXT(":   %s"), *List[I].Value);
		const bool bInfo = !List[I].Choose && !List[I].Step;
		Line(Text, bSel ? FColor(255, 220, 120) : (bInfo ? FColor(210, 210, 210) : FColor(235, 235, 235)), bInfo ? 1.0f : (bSel ? 1.4f : 1.25f),
			bSel ? 0.6f : 0.35f);
	}
	Y += 16.0f * Sc;
	if (List.IsValidIndex(Selected) && !List[Selected].Hint.IsEmpty()) Line(List[Selected].Hint, FColor(190, 210, 240), 1.0f, 0.35f);
	Y = Canvas->ClipY - 90.0f * Sc;
	Line(TEXT("Up/Down to pick   Left/Right to change   Enter, Cross or A to choose   Esc, Circle or B back"), FColor(200, 200, 200), 0.9f, 0.35f);
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
