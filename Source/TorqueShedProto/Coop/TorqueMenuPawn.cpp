#include "TorqueMenuPawn.h"

#include "TorqueSession.h"
#include "TorquePlayerController.h"
#include "TorqueSettings.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"

ATorqueMenuPawn::ATorqueMenuPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->SetFieldOfView(60.0f);
}

void ATorqueMenuPawn::BeginPlay()
{
	Super::BeginPlay();
	// Up at head height over the start, looking along the way the ride goes.
	BaseLoc = GetActorLocation() + FVector(0.0, 0.0, 200.0);
	BaseRot = FRotator(-5.0, GetActorRotation().Yaw, 0.0);
	// A level can say where the menu looks from: an actor tagged MenuView (the shed has one by the
	// roller door, looking in at the bike, clear of the light battens and beams).
	bool bFromView = false;
	if (UWorld* World = GetWorld())
		for (TActorIterator<AActor> It(World); It; ++It)
			if (It->ActorHasTag(TEXT("MenuView")))
			{
				BaseLoc = It->GetActorLocation();
				BaseRot = It->GetActorRotation();
				bFromView = true;
				break;
			}
	UE_LOG(LogTemp, Log, TEXT("[Torque] menu view: %s at %s, looking %s (map %s)"),
		bFromView ? TEXT("from MenuView") : TEXT("fallback over the player start"), *BaseLoc.ToString(), *BaseRot.ToString(),
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("?"));
	Camera->SetWorldLocationAndRotation(BaseLoc, BaseRot);
	Pad = MakeUnique<FSonyPad>();
	if (UTorqueSession* S = UTorqueSession::Get(this))
	{
		S->LeaveSession();   // back from a ride: we're not in it any more
		Address = S->LastAddress;
	}
}

void ATorqueMenuPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	Pad.Reset();
	Super::EndPlay(Reason);
}

TArray<ATorqueMenuPawn::FEntry> ATorqueMenuPawn::Entries() const
{
	TArray<FEntry> Out;
	const UTorqueSession* S = UTorqueSession::Get(this);
	const bool bShed = UTorqueSession::FirstMap() == UTorqueSession::ShedMap;
	FEntry E;
	E.Kind = EItem::Name;
	const FString& Name = FTorqueSettings::Get().Name;
	E.Text = FString::Printf(TEXT("Your name:  %s%s"), *Name, (Selected == 0 && FMath::Fmod(Clock, 1.0) < 0.5) ? TEXT("_") : TEXT(" "));
	E.Hint = Name.IsEmpty() && S ? FString::Printf(TEXT("Type it (empty: %s); friends see it over you"), *S->PlayerName())
								 : FString(TEXT("Friends see it over you in co-op"));
	Out.Add(E);
	E.Kind = EItem::Solo;
	E.Text = bShed ? TEXT("Open the shed (solo)") : TEXT("Ride solo");
	E.Hint = bShed ? TEXT("Your shed, on your own; ride out of the roller door onto Buttertubs") : TEXT("Buttertubs, from the top down to Keld");
	Out.Add(E);
	E.Kind = EItem::Host;
	E.Text = bShed ? TEXT("Host a co-op shed") : TEXT("Host a co-op ride");
	E.Hint = S && S->HasEos()
		? FString(TEXT("You'll get a friend code to share: friends anywhere join with it (no port forwarding)"))
		: FString::Printf(TEXT("Friends type your address to join%s; the game asks your router to open the port itself, and Esc shows the address"),
			  S && !S->LocalAddress().IsEmpty() ? *FString::Printf(TEXT(" (%s at home)"), *S->LocalAddress()) : TEXT(""));
	Out.Add(E);
	E.Kind = EItem::Join;
	E.Text = FString::Printf(TEXT("Join:  %s%s"), Address.IsEmpty() ? TEXT("") : *Address,
		FMath::Fmod(Clock, 1.0) < 0.5 ? TEXT("_") : TEXT(" "));
	E.Hint = TEXT("Type the host's address or name (192.168.1.20, 81.2.3.4:7777, a name like myshed.ddns.net), Enter joins; Ctrl+V pastes");
	Out.Add(E);
	if (S && S->HasEos())
	{
		E.Kind = EItem::Code;
		E.Text = FString::Printf(TEXT("Join by friend code:  %s%s"), *Code, FMath::Fmod(Clock, 1.0) < 0.5 ? TEXT("_") : TEXT(" "));
		E.Hint = TEXT("The six letters and digits on the host's screen; from anywhere, no ports to open");
		Out.Add(E);
	}
	if (S)
	{
		const TArray<UTorqueSession::FLanGame>& Games = S->GetLanGames();
		for (int32 I = 0; I < Games.Num(); ++I)
		{
			E.Kind = EItem::Lan;
			E.Lan = I;
			E.Text = FString::Printf(TEXT("Join %s's shed  (%d of %d in, %d ms)"), *Games[I].Host, Games[I].Riders,
				Games[I].MaxRiders, Games[I].PingMs);
			E.Hint = TEXT("Found on this network");
			Out.Add(E);
		}
	}
	E.Kind = EItem::Search;
	E.Lan = -1;
	E.Text = S && S->IsSearching() ? TEXT("Looking for co-op sheds on this network...") : TEXT("Look for co-op sheds on this network");
	E.Hint = TEXT("Sheds hosted on this network appear above by themselves");
	Out.Add(E);
	E.Kind = EItem::Settings;
	E.Text = TEXT("Settings");
	E.Hint = TEXT("Graphics, sound, controller and character");
	Out.Add(E);
	E.Kind = EItem::Quit;
	E.Text = TEXT("Exit game");
	E.Hint = FString();
	Out.Add(E);
	return Out;
}

void ATorqueMenuPawn::Choose(const FEntry& E)
{
	UTorqueSession* S = UTorqueSession::Get(this);
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!S) return;
	switch (E.Kind)
	{
	case EItem::Name: break;
	case EItem::Solo: Busy = TEXT("Loading..."); S->RideSolo(); break;
	case EItem::Host: Busy = TEXT("Loading, then waiting for friends..."); S->Host(); break;
	case EItem::Join:
		S->JoinAddress(Address);
		if (S->Status.IsEmpty()) Busy = FString::Printf(TEXT("Joining %s..."), *Address);
		break;
	case EItem::Code:
		S->JoinByCode(Code);
		if (S->Status.IsEmpty()) Busy = FString::Printf(TEXT("Looking for ride %s..."), *Code.ToUpper());
		break;
	case EItem::Lan: Busy = TEXT("Joining..."); S->JoinLan(E.Lan); break;
	case EItem::Search: S->FindLanGames(); SearchClock = 0.0; break;
	case EItem::Settings:
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->OpenSettings();
		break;
	case EItem::Quit:
		if (PC) PC->ConsoleCommand(TEXT("quit"));
		break;
	}
}

void ATorqueMenuPawn::TypeAddress(APlayerController* PC)
{
	static const FKey Digits[10] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	static const FKey Pad10[10] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
		EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::V))
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		Address = Clip.TrimStartAndEnd().Left(64);
		return;
	}
	for (int32 D = 0; D < 10; ++D)
	{
		if (PC->WasInputKeyJustPressed(Digits[D]) || PC->WasInputKeyJustPressed(Pad10[D])) Address.AppendChar(TCHAR('0' + D));
	}
	// Host names (letters, digits, dots, hyphens) and IPv6 ([::1]:7777) too.
	static const FKey Letters[26] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H,
		EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S,
		EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
	if (!bCtrl)
		for (int32 L = 0; L < 26; ++L)
			if (PC->WasInputKeyJustPressed(Letters[L])) Address.AppendChar(TCHAR('a' + L));
	if (PC->WasInputKeyJustPressed(EKeys::Period) || PC->WasInputKeyJustPressed(EKeys::Decimal)) Address.AppendChar(TEXT('.'));
	if (PC->WasInputKeyJustPressed(EKeys::Semicolon)) Address.AppendChar(TEXT(':'));
	if (PC->WasInputKeyJustPressed(EKeys::Hyphen) || PC->WasInputKeyJustPressed(EKeys::Subtract)) Address.AppendChar(TEXT('-'));
	if (PC->WasInputKeyJustPressed(EKeys::LeftBracket)) Address.AppendChar(TEXT('['));
	if (PC->WasInputKeyJustPressed(EKeys::RightBracket)) Address.AppendChar(TEXT(']'));
	if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && Address.Len() > 0) Address.LeftChopInline(1);
	if (PC->WasInputKeyJustPressed(EKeys::Delete)) Address.Reset();
	Address = Address.Left(64);
}

void ATorqueMenuPawn::TypeCode(APlayerController* PC)
{
	static const FKey Letters[26] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H,
		EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M, EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S,
		EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
	static const FKey Digits[10] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::V))
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		Code = Clip.TrimStartAndEnd().ToUpper().Left(6);
		return;
	}
	for (int32 L = 0; L < 26 && Code.Len() < 6; ++L)
		if (PC->WasInputKeyJustPressed(Letters[L])) Code.AppendChar(TCHAR('A' + L));
	for (int32 D = 0; D < 10 && Code.Len() < 6; ++D)
		if (PC->WasInputKeyJustPressed(Digits[D])) Code.AppendChar(TCHAR('0' + D));
	if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && Code.Len() > 0) Code.LeftChopInline(1);
	if (PC->WasInputKeyJustPressed(EKeys::Delete)) Code.Reset();
}

void ATorqueMenuPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	// The view drifts slowly from side to side.
	Camera->SetWorldLocationAndRotation(BaseLoc, BaseRot + FRotator(0.0, 14.0 * FMath::Sin(Clock * 0.07), 0.0));

	UTorqueSession* S = UTorqueSession::Get(this);
	// Keep looking for rides on the network while the menu's up.
	SearchClock += DeltaSeconds;
	if (S && !S->IsSearching() && SearchClock > 6.0)
	{
		SearchClock = 0.0;
		S->FindLanGames();
	}
	if (S && !S->Status.IsEmpty()) Busy.Reset();   // something went wrong: the menu is usable again

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Busy.IsEmpty()) return;
	if (ATorquePlayerController::MenuUp(PC)) return;   // Settings is open over the menu
	const TArray<FEntry> List = Entries();
	Selected = FMath::Clamp(Selected, 0, List.Num() - 1);

	const FSonyPadState Sony = Pad ? Pad->Get() : FSonyPadState();
	const uint32 Pressed = Sony.Buttons & ~PrevButtons;
	PrevButtons = Sony.Buttons;
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	int32 Move = 0;
	if (Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (Pressed & ESonyButton::DUp)) Move = -1;
	if (Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (Pressed & ESonyButton::DDown)) Move = 1;
	// The left stick moves too, repeating while held.
	const float StickY = FMath::Abs(Sony.LY) > FMath::Abs(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY))
		? Sony.LY : PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	const int32 Dir = StickY > 0.6f ? -1 : (StickY < -0.6f ? 1 : 0);
	if (Dir != StickDir)
	{
		StickDir = Dir;
		StickRepeat = 0.35;
		if (Dir != 0) Move = Dir;
	}
	else if (Dir != 0 && (StickRepeat -= DeltaSeconds) <= 0.0)
	{
		StickRepeat = 0.15;
		Move = Dir;
	}
	// The mouse: over an entry picks it, a click chooses it, the wheel moves.
	FVector2D Mouse;
	const bool bMoved = ATorquePlayerController::MouseMoved(PC, LastMouse, Mouse);
	const FTorqueMenuHits::FHit* Over = Hits.At(Mouse);
	bool bClicked = false;
	if (Over && List.IsValidIndex(Over->Index) && (bMoved || Just(EKeys::LeftMouseButton))) Selected = Over->Index;
	if (Over && List.IsValidIndex(Over->Index) && Just(EKeys::LeftMouseButton)) bClicked = true;
	if (Just(EKeys::MouseScrollUp)) Move = -1;
	if (Just(EKeys::MouseScrollDown)) Move = 1;
	if (Move != 0) Selected = (Selected + Move + List.Num()) % List.Num();

	// Your name: kept as soon as you move off it (or start).
	if (List[Selected].Kind == EItem::Name)
	{
		const FString Before = FTorqueSettings::Get().Name;
		ATorquePlayerController::TypeText(PC, FTorqueSettings::Get().Name, 20);
		bNameTyped |= Before != FTorqueSettings::Get().Name;
	}
	else if (bNameTyped)
	{
		bNameTyped = false;
		FTorqueSettings::Get().Save();
	}
	if (List[Selected].Kind == EItem::Join) TypeAddress(PC);
	if (List[Selected].Kind == EItem::Code) TypeCode(PC);
	const bool bTyping = List[Selected].Kind == EItem::Name;
	const bool bChoose = Just(EKeys::Enter) || (!bTyping && Just(EKeys::SpaceBar)) || Just(EKeys::Gamepad_FaceButton_Bottom) || (Pressed & ESonyButton::Cross)
		|| (bClicked && !bTyping);   // a click on your name puts you in it to type
	if (bChoose)
	{
		if (bNameTyped)
		{
			bNameTyped = false;
			FTorqueSettings::Get().Save();
		}
		Choose(List[Selected]);
	}
}

namespace
{
	// The build stamp on the title screen: Data/build.json, written by the PC's packaging step
	// ({"round": 100, "commit": "abc1234", "built": "2026-11-09 13:05"}). A build without one says so.
	const FString& BuildStamp()
	{
		static FString Stamp;
		static bool bRead = false;
		if (bRead) return Stamp;
		bRead = true;
		Stamp = TEXT("Dev build (no build number)");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/build.json")))) return Stamp;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return Stamp;
		int32 Round = 0;
		FString Commit, Built;
		Root->TryGetNumberField(TEXT("round"), Round);
		Root->TryGetStringField(TEXT("commit"), Commit);
		Root->TryGetStringField(TEXT("built"), Built);
		if (Round > 0) Stamp = FString::Printf(TEXT("Build %d"), Round);
		if (!Built.IsEmpty()) Stamp += TEXT("  ") + Built;
		if (!Commit.IsEmpty()) Stamp += TEXT("  (") + Commit.Left(7) + TEXT(")");
		UE_LOG(LogTemp, Log, TEXT("[Torque] build stamp: %s"), *Stamp);
		return Stamp;
	}
}

void ATorqueMenuPawn::DrawMenu(AHUD* Hud, UCanvas* Canvas) const
{
	Hits.Reset();
	if (!Hud || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetLargeFont();
	const float Sc = Canvas->ClipY / 1080.0f;
	const float X = 120.0f * Sc;
	float Y = 150.0f * Sc;
	auto Line = [&](const FString& Text, const FColor& Colour, float Size, float Box) {
		float W = 0, H = 0;
		Canvas->StrLen(Font, Text, W, H);
		const float K = Sc * Size;
		if (Box > 0.0f) Hud->DrawRect(FLinearColor(0, 0, 0, Box), X - 16.0f * Sc, Y - 6.0f * Sc, W * K + 32.0f * Sc, H * K + 12.0f * Sc);
		Canvas->SetDrawColor(Colour);
		Canvas->DrawText(Font, Text, X, Y, K, K);
		Y += H * K + 14.0f * Sc;
	};
	Line(TEXT("TORQUE SHED"), FColor::White, 3.2f, 0.45f);
	Line(TEXT("Strip it. Clean it. Fix it. Ride it."), FColor(210, 210, 210), 1.1f, 0.35f);
	Y += 30.0f * Sc;
	const TArray<FEntry> List = Entries();
	const UTorqueSession* S = UTorqueSession::Get(this);
	for (int32 I = 0; I < List.Num(); ++I)
	{
		const bool bSel = I == Selected;
		const float Top = Y;
		Line((bSel ? TEXT(">  ") : TEXT("    ")) + List[I].Text, bSel ? FColor(255, 220, 120) : FColor(235, 235, 235), bSel ? 1.6f : 1.4f,
			bSel ? 0.6f : 0.35f);
		Hits.Add(X - 16.0f * Sc, Top - 6.0f * Sc, X + 900.0f * Sc, Y - 8.0f * Sc, I, FTorqueMenuHits::Row);   // for the mouse
	}
	Y += 20.0f * Sc;
	if (List.IsValidIndex(Selected) && !List[Selected].Hint.IsEmpty()) Line(List[Selected].Hint, FColor(190, 210, 240), 1.0f, 0.35f);
	if (!Busy.IsEmpty()) Line(Busy, FColor(170, 255, 170), 1.2f, 0.45f);
	else if (S && !S->Status.IsEmpty()) Line(S->Status, FColor(255, 170, 90), 1.2f, 0.45f);
	Y = Canvas->ClipY - 90.0f * Sc;
	Line(TEXT("Click to choose (the wheel scrolls)   Up/Down, D-pad or left stick to pick   Enter, Cross or A to choose"), FColor(200, 200, 200), 0.9f, 0.35f);
	{
		// The build number, bottom right, on every build (beenster, 9 Nov).
		const FString& Stamp = BuildStamp();
		float W = 0, H = 0;
		Canvas->StrLen(Font, Stamp, W, H);
		const float K = 1.1f * Sc;
		const float BX = Canvas->ClipX - W * K - 40.0f * Sc;
		const float BY = Canvas->ClipY - H * K - 30.0f * Sc;
		Hud->DrawRect(FLinearColor(0, 0, 0, 0.45f), BX - 14.0f * Sc, BY - 6.0f * Sc, W * K + 28.0f * Sc, H * K + 12.0f * Sc);
		Canvas->SetDrawColor(FColor(255, 220, 120));
		Canvas->DrawText(Font, Stamp, BX, BY, K, K);
	}
}
