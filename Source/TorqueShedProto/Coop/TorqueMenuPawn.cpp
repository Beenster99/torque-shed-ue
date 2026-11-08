#include "TorqueMenuPawn.h"

#include "TorqueSession.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/HUD.h"
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
	E.Kind = EItem::Solo;
	E.Text = TEXT("Ride solo");
	E.Hint = bShed ? TEXT("Into the shed, then out onto Buttertubs") : TEXT("Buttertubs, from the top down to Keld");
	Out.Add(E);
	E.Kind = EItem::Host;
	E.Text = TEXT("Host a co-op ride");
	E.Hint = S && S->HasEos()
		? FString(TEXT("You'll get a friend code to share: friends anywhere join with it (no port forwarding)"))
		: S && !S->LocalAddress().IsEmpty()
		? FString::Printf(TEXT("Friends join %s (over the internet: your public address, with port 7777 forwarded)"), *S->LocalAddress())
		: FString(TEXT("Friends join by this PC's address, port 7777"));
	Out.Add(E);
	E.Kind = EItem::Join;
	E.Text = FString::Printf(TEXT("Join by address:  %s%s"), Address.IsEmpty() ? TEXT("") : *Address,
		FMath::Fmod(Clock, 1.0) < 0.5 ? TEXT("_") : TEXT(" "));
	E.Hint = TEXT("Type the host's address (digits, dots, : for a port); Ctrl+V pastes");
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
			E.Text = FString::Printf(TEXT("Join %s's ride  (%d of %d riding, %d ms)"), *Games[I].Host, Games[I].Riders,
				Games[I].MaxRiders, Games[I].PingMs);
			E.Hint = TEXT("Found on this network");
			Out.Add(E);
		}
	}
	E.Kind = EItem::Search;
	E.Lan = -1;
	E.Text = S && S->IsSearching() ? TEXT("Looking for rides on this network...") : TEXT("Look for rides on this network");
	E.Hint = TEXT("Rides hosted on this network appear above by themselves");
	Out.Add(E);
	E.Kind = EItem::Quit;
	E.Text = TEXT("Quit");
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
	if (PC->WasInputKeyJustPressed(EKeys::Period) || PC->WasInputKeyJustPressed(EKeys::Decimal)) Address.AppendChar(TEXT('.'));
	if (PC->WasInputKeyJustPressed(EKeys::Semicolon)) Address.AppendChar(TEXT(':'));
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
	if (Move != 0) Selected = (Selected + Move + List.Num()) % List.Num();

	if (List[Selected].Kind == EItem::Join) TypeAddress(PC);
	if (List[Selected].Kind == EItem::Code) TypeCode(PC);
	const bool bChoose = Just(EKeys::Enter) || Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Bottom) || (Pressed & ESonyButton::Cross);
	if (bChoose) Choose(List[Selected]);
	if (Just(EKeys::Escape)) PC->ConsoleCommand(TEXT("quit"));
}

void ATorqueMenuPawn::DrawMenu(AHUD* Hud, UCanvas* Canvas) const
{
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
	Line(TEXT("Buttertubs, on your own or with friends"), FColor(210, 210, 210), 1.1f, 0.35f);
	Y += 30.0f * Sc;
	const TArray<FEntry> List = Entries();
	const UTorqueSession* S = UTorqueSession::Get(this);
	for (int32 I = 0; I < List.Num(); ++I)
	{
		const bool bSel = I == Selected;
		Line((bSel ? TEXT(">  ") : TEXT("    ")) + List[I].Text, bSel ? FColor(255, 220, 120) : FColor(235, 235, 235), bSel ? 1.6f : 1.4f,
			bSel ? 0.6f : 0.35f);
	}
	Y += 20.0f * Sc;
	if (List.IsValidIndex(Selected) && !List[Selected].Hint.IsEmpty()) Line(List[Selected].Hint, FColor(190, 210, 240), 1.0f, 0.35f);
	if (!Busy.IsEmpty()) Line(Busy, FColor(170, 255, 170), 1.2f, 0.45f);
	else if (S && !S->Status.IsEmpty()) Line(S->Status, FColor(255, 170, 90), 1.2f, 0.45f);
	Y = Canvas->ClipY - 90.0f * Sc;
	Line(TEXT("Up/Down, D-pad or left stick to pick   Enter, Cross or A to choose   Esc quits"), FColor(200, 200, 200), 0.9f, 0.35f);
}
