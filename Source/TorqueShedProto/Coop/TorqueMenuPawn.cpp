#include "TorqueMenuPawn.h"

#include "TorqueUi.h"

#include "TorqueSession.h"
#include "TorquePlayerController.h"
#include "TorqueSettings.h"
#include "Shed/TorqueBuildSubsystem.h"
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

FString ATorqueMenuPawn::PageTitle() const
{
	switch (Page)
	{
	case EPage::Solo: return TEXT("START SOLO");
	case EPage::Host: return TEXT("HOST GAME");
	case EPage::Join: return TEXT("JOIN GAME");
	case EPage::Load: return bHosting ? TEXT("HOST GAME  ·  LOAD GAME") : TEXT("START SOLO  ·  LOAD GAME");
	case EPage::Delete: return TEXT("START SOLO  ·  DELETE GAME");
	case EPage::Confirm:
		return Games.IsValidIndex(DeleteIndex) ? FString::Printf(TEXT("DELETE %s?"), *Games[DeleteIndex].Name.ToUpper()) : FString(TEXT("DELETE?"));
	default: return FString();
	}
}

TArray<ATorqueMenuPawn::FEntry> ATorqueMenuPawn::Entries() const
{
	TArray<FEntry> Out;
	auto Add = [&Out](EItem Kind, const FString& Text, const FString& Hint) {
		FEntry E;
		E.Kind = Kind;
		E.Text = Text;
		E.Hint = Hint;
		Out.Add(E);
	};
	const UTorqueSession* S = UTorqueSession::Get(this);
	switch (Page)
	{
	case EPage::Main:
		Add(EItem::Solo, TEXT("Start Solo"), TEXT("Your shed, on your own: a new game, or carry on with a saved one"));
		Add(EItem::Host, TEXT("Host Game"), FString::Printf(TEXT("Open your shed to friends: they join with your address%s. The game asks your router to open the port itself, and Esc in the game shows what to tell them"),
			S && !S->LocalAddress().IsEmpty() ? *FString::Printf(TEXT(" (%s at home)"), *S->LocalAddress()) : TEXT("")));
		Add(EItem::Join, TEXT("Join Game"), TEXT("Join a friend's shed by their address"));
		Add(EItem::Settings, TEXT("Settings"), TEXT("Graphics, sound, gameplay, controller, and your name and colour"));
		Add(EItem::Quit, TEXT("Exit"), FString());
		break;
	case EPage::Solo:
	case EPage::Host:
	{
		const bool bHost = Page == EPage::Host;
		Add(EItem::NewGame, TEXT("New Game"), bHost ? TEXT("A new shed with a couple of project bikes, open to friends")
												   : TEXT("A new shed with a couple of project bikes to get stuck into. Your other games are kept"));
		Add(EItem::LoadGame, TEXT("Load Game"), bHost ? TEXT("Carry on with a saved shed, open to friends") : TEXT("Carry on with a saved shed"));
		if (!bHost) Add(EItem::DeleteGame, TEXT("Delete Game"), TEXT("Remove a saved game for good (it asks first)"));
		Add(EItem::Back, TEXT("Back"), FString());
		break;
	}
	case EPage::Join:
		Add(EItem::Address, TEXT("IP address"),
			TEXT("Type the host's address (192.168.1.20, 81.2.3.4:7777, or a name like myshed.ddns.net); Ctrl+V pastes, Delete clears"));
		Add(EItem::JoinNow, TEXT("Join"), Address.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Type the address first")) : FString::Printf(TEXT("Join %s"), *Address));
		Add(EItem::Back, TEXT("Back"), FString());
		break;
	case EPage::Load:
	case EPage::Delete:
		for (int32 I = 0; I < Games.Num(); ++I)
		{
			FEntry E;
			E.Kind = EItem::Game;
			E.Game = I;
			E.Text = Games[I].Name;
			E.Side = Games[I].When;
			const FString Bikes = Games[I].Bikes < 0 ? FString(TEXT("couldn't be read"))
								 : FString::Printf(TEXT("%d bike%s"), Games[I].Bikes, Games[I].Bikes == 1 ? TEXT("") : TEXT("s"));
			E.Hint = FString::Printf(TEXT("%s, saved %s: %s"), *Games[I].Name, *Games[I].When, *Bikes);
			if (Page == EPage::Delete) E.Hint += TEXT(". Choose it to delete it (it asks first)");
			Out.Add(E);
		}
		Add(EItem::Back, TEXT("Back"), FString());
		break;
	case EPage::Confirm:
		Add(EItem::Yes, TEXT("Delete it"), TEXT("Gone for good: this can't be undone"));
		Add(EItem::No, TEXT("Keep it"), FString());
		break;
	}
	return Out;
}

void ATorqueMenuPawn::ReadGames()
{
	Games.Reset();
	// File times are UTC; shown in the PC's own time.
	const FTimespan ToLocal = FDateTime::Now() - FDateTime::UtcNow();
	static const TCHAR* Months[12] = {TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"),
		TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec")};
	for (const UTorqueBuildSubsystem::FSavedGame& G : UTorqueBuildSubsystem::SavedGames())
	{
		FGameRow R;
		R.Slot = G.Slot;
		R.Name = FString::Printf(TEXT("Game %d"), G.Number);
		const FDateTime T = G.Saved + ToLocal;
		R.When = FString::Printf(TEXT("%d %s %d, %02d:%02d"), T.GetDay(), Months[FMath::Clamp(T.GetMonth(), 1, 12) - 1], T.GetYear(), T.GetHour(), T.GetMinute());
		R.Bikes = G.BikeCount;
		Games.Add(R);
	}
}

void ATorqueMenuPawn::GoTo(EPage To)
{
	SelectedOn.SetNumZeroed(8);
	SelectedOn[(int32)Page] = Selected;
	Page = To;
	Note.Reset();
	if (To == EPage::Load || To == EPage::Delete)
	{
		ReadGames();
		if (Games.IsEmpty()) Note = TEXT("No saved games yet: start a New Game");
		Selected = 0;   // the newest game
	}
	else if (To == EPage::Confirm)
		Selected = 1;   // on "Keep it"
	else
		Selected = SelectedOn[(int32)To];
	if (UTorqueSession* S = UTorqueSession::Get(this)) S->Status.Reset();
}

void ATorqueMenuPawn::Back()
{
	switch (Page)
	{
	case EPage::Solo:
	case EPage::Host:
	case EPage::Join: GoTo(EPage::Main); break;
	case EPage::Load: GoTo(bHosting ? EPage::Host : EPage::Solo); break;
	case EPage::Delete: GoTo(EPage::Solo); break;
	case EPage::Confirm: GoTo(EPage::Delete); break;
	default: break;
	}
}

void ATorqueMenuPawn::Start(const FString& Slot, bool bNew)
{
	UTorqueSession* S = UTorqueSession::Get(this);
	if (!S) return;
	if (UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this))
		Shed->UseGame(bNew ? UTorqueBuildSubsystem::NewGameSlot() : Slot, bNew);
	if (bHosting)
	{
		Busy = TEXT("Loading, then waiting for friends...");
		S->Host();
	}
	else
	{
		Busy = TEXT("Loading...");
		S->RideSolo();
	}
}

void ATorqueMenuPawn::Choose(const FEntry& E)
{
	UTorqueSession* S = UTorqueSession::Get(this);
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!S) return;
	switch (E.Kind)
	{
	case EItem::Solo:
		bHosting = false;
		GoTo(EPage::Solo);
		break;
	case EItem::Host:
		bHosting = true;
		GoTo(EPage::Host);
		break;
	case EItem::Join: GoTo(EPage::Join); break;
	case EItem::Settings:
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->OpenSettings();
		break;
	case EItem::Quit:
		if (PC) PC->ConsoleCommand(TEXT("quit"));
		break;
	case EItem::NewGame: Start(FString(), true); break;
	case EItem::LoadGame: GoTo(EPage::Load); break;
	case EItem::DeleteGame: GoTo(EPage::Delete); break;
	case EItem::Back: Back(); break;
	case EItem::Address:   // Enter in the box joins
	case EItem::JoinNow:
		S->JoinAddress(Address);
		if (S->Status.IsEmpty()) Busy = FString::Printf(TEXT("Joining %s..."), *Address.TrimStartAndEnd());
		break;
	case EItem::Game:
		if (!Games.IsValidIndex(E.Game)) break;
		if (Page == EPage::Load) Start(Games[E.Game].Slot, false);
		else
		{
			DeleteIndex = E.Game;
			GoTo(EPage::Confirm);
		}
		break;
	case EItem::Yes:
		if (Games.IsValidIndex(DeleteIndex))
		{
			const FString Gone = Games[DeleteIndex].Name;
			UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this);
			const bool bOk = Shed && Shed->DeleteGame(Games[DeleteIndex].Slot);
			DeleteIndex = -1;
			GoTo(EPage::Delete);
			if (!bOk) Note = FString::Printf(TEXT("Couldn't delete %s"), *Gone);
			else Note = Games.IsEmpty() ? FString::Printf(TEXT("%s deleted. No saved games left"), *Gone) : FString::Printf(TEXT("%s deleted"), *Gone);
		}
		break;
	case EItem::No: Back(); break;
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


void ATorqueMenuPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	// The view drifts slowly from side to side.
	Camera->SetWorldLocationAndRotation(BaseLoc, BaseRot + FRotator(0.0, 14.0 * FMath::Sin(Clock * 0.07), 0.0));

	UTorqueSession* S = UTorqueSession::Get(this);
	if (S && !S->Status.IsEmpty()) Busy.Reset();   // something went wrong: the menu is usable again

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Busy.IsEmpty()) return;
	if (ATorquePlayerController::MenuUp(PC)) return;   // Settings is open over the menu
	const TArray<FEntry> List = Entries();
	if (List.IsEmpty()) return;
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
	// The mouse: over an entry picks it, a click chooses it, the wheel moves, the right button (or
	// the footer's Esc) goes back.
	FVector2D Mouse;
	const bool bMoved = ATorquePlayerController::MouseMoved(PC, LastMouse, Mouse);
	const FTorqueMenuHits::FHit* Over = Hits.At(Mouse);
	const bool bClick = Just(EKeys::LeftMouseButton);
	bool bClicked = false, bBackClicked = false;
	if (Over && Over->Kind == FTorqueMenuHits::Row && List.IsValidIndex(Over->Index))
	{
		if (bMoved || bClick) Selected = Over->Index;
		bClicked = bClick;
	}
	if (Over && Over->Kind == FTorqueMenuHits::BackKey && bClick) bBackClicked = true;
	if (Just(EKeys::MouseScrollUp)) Move = -1;
	if (Just(EKeys::MouseScrollDown)) Move = 1;
	if (Move != 0) Selected = (Selected + Move + List.Num()) % List.Num();

	const bool bTyping = List[Selected].Kind == EItem::Address;
	if (bTyping) TypeAddress(PC);
	const bool bChoose = Just(EKeys::Enter) || (!bTyping && Just(EKeys::SpaceBar)) || Just(EKeys::Gamepad_FaceButton_Bottom) ||
						 (Pressed & ESonyButton::Cross) || bClicked;
	const bool bBack = Just(EKeys::Escape) || Just(EKeys::RightMouseButton) || Just(EKeys::Gamepad_FaceButton_Right) ||
					   (Pressed & ESonyButton::Circle) || (!bTyping && Just(EKeys::BackSpace)) || bBackClicked;
	if (bChoose && !(bTyping && bClicked)) Choose(List[Selected]);   // a click on the box only puts you in it
	else if (bBack) Back();
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
	namespace Ui = TorqueUi;
	Hits.Reset();
	if (!Hud || !Canvas) return;
	const float Sc = Ui::Scale(Canvas);
	const float X = 140.0f * Sc;
	// The left side darkened (more at the edge) so the words read over the shed behind: one smooth
	// fade (the curve the old 16 stacked bands made, without their steps), easing out to nothing.
	{
		const float Edge = FMath::Max(900.0f * Sc, Canvas->ClipX * 0.46f);
		Ui::HGradient(Canvas, 0, 0, Edge, Canvas->ClipY, FLinearColor::Black, [](float T) {
			const float Body = 1.0f - FMath::Pow(0.925f, 16.0f * (1.0f - T));
			const float Tail = 1.0f - FMath::SmoothStep(0.8f, 1.0f, T);   // no hard edge where it ends
			return Body * Tail;
		}, 96);
	}
	float Y = 150.0f * Sc;
	Ui::Rect(Canvas, X, Y, 64.0f * Sc, 4.0f * Sc, Ui::Accent);
	Ui::Text(Canvas, TEXT("TORQUE SHED"), X, Y + 18.0f * Sc, 84.0f * Sc, Ui::Ink, true);
	Ui::Text(Canvas, TEXT("Strip it. Clean it. Fix it. Ride it."), X + 4.0f * Sc, Y + 126.0f * Sc, 22.0f * Sc, Ui::Dim);
	Y += 200.0f * Sc;
	// Which page this is (none on the first).
	const FString Title = PageTitle();
	if (!Title.IsEmpty()) Ui::Text(Canvas, Title, X, Y, 18.0f * Sc, Ui::Accent, true);
	Y += 34.0f * Sc;
	const TArray<FEntry> List = Entries();
	const UTorqueSession* Ses = UTorqueSession::Get(this);
	float MX = -1.f, MY = -1.f;
	if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->GetMousePosition(MX, MY);
	const float RowH = 50.0f * Sc, RowW = 620.0f * Sc;
	const bool bBlink = FMath::Fmod(Clock, 1.0) < 0.5;
	for (int32 I = 0; I < List.Num(); ++I)
	{
		const FEntry& E = List[I];
		const bool bSel = I == Selected;
		const bool bHot = MX >= X - 20.0f * Sc && MX < X + RowW && MY >= Y && MY < Y + RowH;
		// A gap above Back, so it sits apart from the choices.
		if (E.Kind == EItem::Back && I > 0) Y += 10.0f * Sc;
		if (bSel)
		{
			Ui::Rect(Canvas, X - 20.0f * Sc, Y + 3.0f * Sc, RowW, RowH - 6.0f * Sc, FLinearColor(1, 1, 1, 0.07f));
			Ui::Rect(Canvas, X - 20.0f * Sc, Y + 3.0f * Sc, 4.0f * Sc, RowH - 6.0f * Sc, Ui::Accent);
		}
		const float Px = 26.0f * Sc;
		const FLinearColor Colour = bSel ? Ui::Ink : (bHot ? FLinearColor(0.85f, 0.85f, 0.83f, 1.0f) : Ui::Dim);
		const float TX = X + (bSel ? 8.0f * Sc : 0.0f), TY = Y + 0.5f * (RowH - Px * 1.2f);
		if (E.Kind == EItem::Address)
		{
			// The label, then a box with the address in it (and the caret while it's picked).
			Ui::Text(Canvas, E.Text, TX, TY, Px, Colour, true);
			const float BX = X + 200.0f * Sc, BW = RowW - 230.0f * Sc, BY = Y + 7.0f * Sc, BH = RowH - 14.0f * Sc;
			Ui::Rect(Canvas, BX, BY, BW, BH, FLinearColor(0, 0, 0, 0.35f));
			Ui::Frame(Canvas, BX, BY, BW, BH, bSel ? Ui::Accent : FLinearColor(1, 1, 1, 0.25f), FMath::Max(1.0f, 1.5f * Sc));
			const float FPx = 22.0f * Sc;
			const FString Shown = Address.IsEmpty() && !bSel ? FString(TEXT("192.168.1.20")) : Address + (bSel && bBlink ? TEXT("_") : TEXT(" "));
			Ui::TextFit(Canvas, Shown, BX + 12.0f * Sc, BY + 0.5f * (BH - FPx * 1.2f), FPx, BW - 24.0f * Sc,
				Address.IsEmpty() && !bSel ? Ui::Faint : Ui::Ink, false);
		}
		else
		{
			const float SideW = E.Side.IsEmpty() ? 0.0f : (float)Ui::Measure(E.Side, 18.0f * Sc).X + 24.0f * Sc;
			Ui::TextFit(Canvas, E.Text, TX, TY, Px, RowW - 40.0f * Sc - SideW, Colour, true);
			if (!E.Side.IsEmpty())
				Ui::Text(Canvas, E.Side, X + RowW - 40.0f * Sc, Y + 0.5f * (RowH - 18.0f * Sc * 1.2f), 18.0f * Sc, bSel ? Ui::Dim : Ui::Faint,
					false, Ui::EAlign::Right);
		}
		Hits.Add(X - 20.0f * Sc, Y, X + RowW, Y + RowH, I, FTorqueMenuHits::Row);   // for the mouse
		Y += RowH;
	}
	Y += 24.0f * Sc;
	if (!Note.IsEmpty()) Y += Ui::TextWrap(Canvas, Note, X, Y, 20.0f * Sc, RowW, Ui::Accent) + 12.0f * Sc;
	if (List.IsValidIndex(Selected) && !List[Selected].Hint.IsEmpty()) Y += Ui::TextWrap(Canvas, List[Selected].Hint, X, Y, 19.0f * Sc, RowW, Ui::Dim) + 12.0f * Sc;
	if (!Busy.IsEmpty()) Ui::TextWrap(Canvas, Busy, X, Y, 21.0f * Sc, RowW, Ui::Accent);
	else if (Ses && !Ses->Status.IsEmpty()) Ui::TextWrap(Canvas, Ses->Status, X, Y, 21.0f * Sc, RowW, Ui::Warn);
	// The keys along the bottom.
	{
		const float FY = Canvas->ClipY - 70.0f * Sc;
		float FX = X;
		FX += Ui::KeyHint(Canvas, TEXT("Enter"), TEXT("Select"), FX, FY, Sc) + 30.0f * Sc;
		FX += Ui::KeyHint(Canvas, TEXT("↑ ↓"), TEXT("Move"), FX, FY, Sc) + 30.0f * Sc;
		if (Page != EPage::Main)
		{
			const float W = Ui::KeyHint(Canvas, TEXT("Esc"), TEXT("Back"), FX, FY, Sc);
			Hits.Add(FX, FY, FX + W, FY + 26.0f * Sc, 0, FTorqueMenuHits::BackKey);   // clickable
			FX += W + 30.0f * Sc;
		}
		Ui::Text(Canvas, Page == EPage::Main ? TEXT("Pad:  Cross / A select") : TEXT("Pad:  Cross / A select,  Circle / B back"), FX, FY + 4.0f * Sc,
			16.0f * Sc, Ui::Faint);
	}
	// The build number, bottom right, on every build (beenster, 9 Nov).
	Ui::Text(Canvas, BuildStamp(), Canvas->ClipX - 40.0f * Sc, Canvas->ClipY - 66.0f * Sc, 17.0f * Sc, Ui::Dim, false, Ui::EAlign::Right);
}
