#include "TorquePlayerController.h"
#include "TorqueNetBike.h"

#include "Shed/TorqueDecalMode.h"
#include "Shed/TorqueDecals.h"
#include "Fit/BikeDecal.h"
#include "Shed/TorqueGeometryMode.h"
#include "TorqueMenuPawn.h"
#include "TorquePlayerState.h"
#include "TorqueSession.h"
#include "TorqueSettings.h"
#include "TorqueKeys.h"
#include "TorqueGameState.h"
#include "TorqueUi.h"
#include "TorquePing.h"
#include "TorqueAudio/TorqueSfx.h"
#include "HAL/PlatformTime.h"
#include "World/TorqueWorldSubsystem.h"
#include "World/TorqueRideWorld.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "TorqueBikePawn.h"
#include "TorqueReports.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFlow.h"
#include "Shed/TorqueYard.h"
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueAvatar.h"
#include "Shed/TorqueProfile.h"
#include "Shed/TorqueProfilePreview.h"
#include "CanvasItem.h"
#include "RenderUtils.h"
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
	// ([perf, build 151] -TorqueUseSettings: a robot run on the player's own settings. Round 150's fps gate
	// ran "on beenster's config" under -TorqueNoInput, so none of it was applied: native 4K, no upscaling,
	// the project's preset, whatever the config said; the same for 149's Medium against Epic)
	const bool bUseSettings = FParse::Param(FCommandLine::Get(), TEXT("TorqueUseSettings"));
	const bool bTest = !bUseSettings && (FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput")) || FParse::Param(FCommandLine::Get(), TEXT("TorqueNoSettings")));
	if ((S.bSaved || bUseSettings) && !bTest) S.Apply(GetWorld());
	else UE_LOG(LogTemp, Display, TEXT("Torque graphics: the saved settings are NOT applied (%s): the project's own are in use"), S.bSaved ? TEXT("a test run; -TorqueUseSettings applies them") : TEXT("never saved"));
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

// --- [decals stage 5] Own pictures, shared -------------------------------------------------------------

namespace
{
// Each piece of a picture, to one machine.
template <typename FSend>
void SendPieces(const TArray<uint8>& Png, int32 Piece, FSend&& Send)
{
	for (int32 At = 0; At < Png.Num(); At += Piece)
	{
		TArray<uint8> Part;
		Part.Append(Png.GetData() + At, FMath::Min(Piece, Png.Num() - At));
		Send(Png.Num(), At, Part);
	}
}
}

void ATorquePlayerController::SendDecalPicture(const FString& Id)
{
	static TSet<FString> Sent;
	if (HasAuthority() || !IsLocalController() || Sent.Contains(Id)) return;
	TArray<uint8> Png;
	if (!TorqueDecals::SharedBytes(Id, Png)) return;
	Sent.Add(Id);
	SendPieces(Png, DecalPiece, [this, &Id](int32 Total, int32 At, const TArray<uint8>& Part) { ServerDecalPicture(Id, Total, At, Part); });
}

void ATorquePlayerController::WantDecalPicture(const FString& Id)
{
	static TSet<FString> Asked;
	if (HasAuthority() || !IsLocalController() || !TorqueDecals::IsSharedId(Id) || Asked.Contains(Id)) return;
	Asked.Add(Id);
	ServerWantDecalPicture(Id);
}

bool ATorquePlayerController::TakeDecalPiece(const FString& Id, int32 Total, int32 Offset, const TArray<uint8>& Bytes)
{
	if (!TorqueDecals::IsSharedId(Id) || Total <= 0 || Total > TorqueDecals::MaxSharedBytes || Offset < 0 || Bytes.Num() == 0 ||
		Offset + Bytes.Num() > Total)
	{
		DecalPieces.Remove(Id);
		return false;
	}
	if (DecalPieces.Num() > 8 && !DecalPieces.Contains(Id)) return false;   // (a few at once)
	TArray<uint8>& Buf = DecalPieces.FindOrAdd(Id);
	if (Offset != Buf.Num())   // pieces come in order (reliable): anything else starts it again
	{
		Buf.Reset();
		if (Offset != 0) return false;
	}
	Buf.Append(Bytes);
	if (Buf.Num() < Total) return false;
	const TArray<uint8> Png = MoveTemp(Buf);
	DecalPieces.Remove(Id);
	return TorqueDecals::TakeShared(Id, Png);
}

void ATorquePlayerController::ServerDecalPicture_Implementation(const FString& Id, int32 Total, int32 Offset, const TArray<uint8>& Bytes)
{
	if (!TakeDecalPiece(Id, Total, Offset, Bytes)) return;
	// The host has it: on to everyone else playing.
	TArray<uint8> Png;
	if (!TorqueDecals::SharedBytes(Id, Png)) return;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ATorquePlayerController* Other = Cast<ATorquePlayerController>(It->Get());
		if (!Other || Other == this || Other->IsLocalController()) continue;
		SendPieces(Png, DecalPiece, [Other, &Id](int32 T, int32 At, const TArray<uint8>& Part) { Other->ClientDecalPicture(Id, T, At, Part); });
	}
}

void ATorquePlayerController::ServerWantDecalPicture_Implementation(const FString& Id)
{
	TArray<uint8> Png;
	if (!TorqueDecals::SharedBytes(Id, Png)) return;   // (the host hasn't got it either: it comes when its owner saves)
	SendPieces(Png, DecalPiece, [this, &Id](int32 T, int32 At, const TArray<uint8>& Part) { ClientDecalPicture(Id, T, At, Part); });
}

void ATorquePlayerController::ClientDecalPicture_Implementation(const FString& Id, int32 Total, int32 Offset, const TArray<uint8>& Bytes)
{
	TakeDecalPiece(Id, Total, Offset, Bytes);
}

void ATorquePlayerController::ClientNote_Implementation(const FString& Text)
{
	Note = Text.Left(200);
	NoteFor = 4.0;
}

void ATorquePlayerController::ShowNote(const FString& Text, double Seconds)
{
	Note = Text.Left(200);
	NoteFor = Seconds;
}

// [ping] The host: who pinged (their name and colour) goes on it, and it goes out to everyone
// (shed_state.gd's "ping" action: it changes nothing in the shed).
void ATorquePlayerController::ServerPing_Implementation(const FTorquePingNet& NewPing)
{
	FTorquePingNet Out = NewPing;
	Out.What = Out.What.Left(60);
	// The pinger's profile name and colour come with it (TorquePing::Send); with no name chosen, the
	// name the player joined under.
	Out.Name = Out.Name.TrimStartAndEnd().Left(20);
	if (PlayerState)
	{
		if (Out.Name.IsEmpty()) Out.Name = PlayerState->GetPlayerName();
		Out.PlayerId = PlayerState->GetPlayerId();
	}
	if (Out.Name.IsEmpty()) Out.Name = TEXT("Someone");
	// The dark colours wouldn't show at night: a warm amber instead (as the name tags go pale).
	if ((int32)Out.Colour.R + Out.Colour.G + Out.Colour.B < 200) Out.Colour = FColor(255, 160, 50);
	UWorld* World = GetWorld();
	if (!World) return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		if (ATorquePlayerController* To = Cast<ATorquePlayerController>(It->Get())) To->ClientPing(Out);
	UE_LOG(LogTemp, Log, TEXT("Torque ping: %s pinged %s at %s"), *Out.Name, *Out.What, *Out.At.ToCompactString());
}

void ATorquePlayerController::ClientPing_Implementation(const FTorquePingNet& NewPing)
{
	if (Pings.Num() >= 16) Pings.RemoveAt(0);
	TorquePing::FShown Shown;
	Shown.P = NewPing;
	Shown.Born = FPlatformTime::Seconds();
	Pings.Add(Shown);
	// A bright two-note chime where it is (sfx_synth.gd's "ping", 2 dB up).
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("ping"), NewPing.At, 2.0f);
	// Someone else's: "Sam pinged the workbench" for 2 s.
	const bool bMine = PlayerState && NewPing.PlayerId == PlayerState->GetPlayerId();
	UE_LOG(LogTemp, Log, TEXT("Torque ping: showing %s's %s (%s, %d showing)"), *NewPing.Name, *NewPing.What, NewPing.bBox ? TEXT("box") : TEXT("ring"), Pings.Num());
	if (!bMine) ShowNote(FString::Printf(TEXT("%s pinged %s"), *NewPing.Name, *NewPing.What.ToLower()), 2.0);
}

void ATorquePlayerController::DrawPings(UCanvas* Canvas)
{
	const double Now = FPlatformTime::Seconds();
	Pings.RemoveAll([Now](const TorquePing::FShown& P) { return Now - P.Born >= TorquePing::Life; });
	if (!Canvas || Pings.Num() == 0 || !PlayerCameraManager) return;
	const FVector Eye = PlayerCameraManager->GetCameraLocation();
	const FVector Ahead = PlayerCameraManager->GetCameraRotation().Vector();
	TorquePing::Draw(Canvas, Pings, Now, Eye, Ahead);
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
	if (!bHelmetDecalsSent || TorqueDecals::MyHelmet() != SentHelmetDecals)   // [decals stage 4]
	{
		bHelmetDecalsSent = true;
		SentHelmetDecals = TorqueDecals::MyHelmet();
		PS->ServerSetHelmetDecals(SentHelmetDecals);
		// (its own pictures go to the host first, which passes them on)
		for (const TorqueFit::Decals::Decal& D : TorqueFit::Decals::Parse(TorqueDecals::ToStd(SentHelmetDecals)))
			if (D.K == TorqueFit::Decals::Kind::Image) SendDecalPicture(TorqueDecals::ToUe(D.Id));
	}
	if (FTorqueProfile::Mine().Visited.Num() != SentPlaces)   // [places stage 7]
	{
		SentPlaces = FTorqueProfile::Mine().Visited.Num();
		PS->ServerSetPlaces(SentPlaces, FTorqueProfile::Mine().LastVisitedName());
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
	// [profile] The Profile page is Settings > Profile now (build 155 feedback): the badge and the
	// pause menu's Profile open that tab.
	if (P == EPage::Profile) P = EPage::Character;
	if (P == Page) return;
	DropRow = DropCell = -1;
	if (Page != EPage::None) Stack.Add(Page);
	Page = P;
	Selected = 0;
	FirstRow = 0;
}

void ATorquePlayerController::OpenSkyPresets()
{
	SkyListBackRow = Selected;
	SkyListBackFirst = FirstRow;
	Open(EPage::SkyPresets);
	// The list opens on the sky held.
	const FTorqueSettings& S = FTorqueSettings::Get();
	if (S.WeatherMode != FTorqueSettings::WeatherPreset) return;
	const TArray<FRow> List = Rows();
	for (int32 I = 0; I < List.Num(); ++I)
		if (List[I].Value == TEXT("held")) Selected = I;
}

// [ride date] A row's pull-down: opened on the option showing.
void ATorquePlayerController::OpenDrop(const FRow& Row, int32 RowIndex, int32 Cell)
{
	if (!Row.Cells.IsValidIndex(Cell) || !Row.Cells[Cell].IsDrop() || Row.bDisabled) return;
	DropRow = RowIndex;
	DropCell = Cell;
	Selected = RowIndex;
	const FRow::FCell& C = Row.Cells[Cell];
	DropHot = FMath::Clamp(C.Pick, 0, C.Options.Num() - 1);
	DropFirst = FMath::Clamp(DropHot - DropShown / 2, 0, FMath::Max(C.Options.Num() - DropShown, 0));
}

// [ride date] The keys, pad and mouse while a pull-down is open: it has them to itself. Up / Down (the
// wheel) through its options, Enter (a click) picks one, Left / Right the next box's pull-down, Esc (a
// click outside it) closes it. False when none is open.
bool ATorquePlayerController::DropInput(TArray<FRow>& List, int32 MoveX, int32 MoveY, bool bChoose, bool bBack)
{
	if (DropRow < 0) return false;
	if (!List.IsValidIndex(DropRow) || !List[DropRow].Cells.IsValidIndex(DropCell) || !List[DropRow].Cells[DropCell].IsDrop())
	{
		DropRow = DropCell = -1;
		return false;
	}
	FRow& Row = List[DropRow];
	const FRow::FCell& C = Row.Cells[DropCell];
	const int32 N = C.Options.Num();
	auto Pick = [this, &Row](int32 Option) {
		const int32 Cell = DropCell;
		DropRow = DropCell = -1;
		if (Row.Cells.IsValidIndex(Cell) && Row.Cells[Cell].Set) Row.Cells[Cell].Set(Option);
	};
	FVector2D Mouse;
	const bool bMoved = MouseMoved(this, LastMouse, Mouse);
	const FTorqueMenuHits::FHit* Over = Hits.At(Mouse);
	if (bMoved && Over && Over->Kind == FTorqueMenuHits::DropItem && Over->Sub >= 0) DropHot = Over->Sub;
	int32 Scroll = 0;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) Scroll = -1;
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) Scroll = 1;
	if (Scroll != 0) DropFirst = FMath::Clamp(DropFirst + Scroll * 2, 0, FMath::Max(N - DropShown, 0));
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		if (Over && Over->Kind == FTorqueMenuHits::DropItem)
		{
			if (Over->Sub >= 0) Pick(Over->Sub);
		}
		else if (Over && Over->Kind == FTorqueMenuHits::Cell && Over->Index == DropRow && Over->Sub != DropCell)
			OpenDrop(Row, DropRow, Over->Sub);
		else
			DropRow = DropCell = -1;   // a click anywhere else closes it
		return true;
	}
	if (WasInputKeyJustPressed(EKeys::RightMouseButton) || bBack)
	{
		DropRow = DropCell = -1;
		return true;
	}
	if (MoveY != 0)
	{
		DropHot = FMath::Clamp(DropHot + MoveY, 0, N - 1);
		if (DropHot < DropFirst) DropFirst = DropHot;
		if (DropHot >= DropFirst + DropShown) DropFirst = DropHot - DropShown + 1;
	}
	if (MoveX != 0)
		for (int32 Next = DropCell + MoveX; Row.Cells.IsValidIndex(Next); Next += MoveX)
			if (Row.Cells[Next].IsDrop())
			{
				OpenDrop(Row, DropRow, Next);
				break;
			}
	if (bChoose && DropHot >= 0 && DropHot < N) Pick(DropHot);
	return true;
}

void ATorquePlayerController::Back()
{
	DropRow = DropCell = -1;
	if (Stack.Num() > 0)
	{
		Page = Stack.Pop();
		Selected = 0;
		FirstRow = 0;
		return;
	}
	CloseMenu();
}

FString ATorquePlayerController::AddWaypoint(double Lat, double Lon, const FString& Name, double HereLat, double HereLon, const FString& HereName)
{
	UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
	UWorld* World = GetWorld();
	if (!Real || !World) return TEXT("No Route planner in this game");
	const FString Short = Name.Left(TorqueWorld::Route::MaxName);
	// Riding a Ride UK journey (beenster, build 146: "the satnav should add it as a waypoint (and
	// retain our final destination)"): the sat nav goes by the place and then on to the finish as
	// before. The journey being ridden isn't changed under the riders (in co-op it's everyone's).
	if (World->GetMapName().Contains(TEXT("RideUK")) && ATorqueRideWorld::SetWaypoint(Lat, Lon, Short))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque info: waypoint %s (%.5f, %.5f) for the sat nav"), *Short, Lat, Lon);
		return FString::Printf(TEXT("%s is a waypoint: the sat nav goes by it, then on to the finish"), *Short);
	}
	// Otherwise the Route planner's journey: the place goes in as a stop before where it ends (the
	// planner's, else the one the riders share, else the last kept); with none, here to the place.
	auto Cut = [](const FString& S) { return std::string(TCHAR_TO_UTF8(*S.Left(TorqueWorld::Route::MaxName))); };
	TorqueWorld::RouteRequest Now;
	FString NowTitle;
	bool bHave = false;
	if (Planner && Planner->Journey().Pins.size() >= 2)
	{
		Now = Planner->Journey();
		bHave = true;
	}
	if (!bHave)
		if (const ATorqueGameState* GS = World->GetGameState<ATorqueGameState>(); GS && !GS->RideJourney.IsEmpty() && GetNetMode() != NM_Standalone)
			bHave = TorqueWorld::Route::FromCode(TCHAR_TO_UTF8(*GS->RideJourney), Now) && Now.Pins.size() >= 2;
	if (!bHave) bHave = Real->LastJourney(Now, NowTitle) && Now.Pins.size() >= 2;
	TorqueWorld::RouteRequest New;
	FString Kept;
	if (bHave && int32(Now.Pins.size()) < TorqueWorld::Route::MaxPins)
	{
		New = Now;
		if (!TorqueWorld::Route::IsStyle(New.Mode)) New.Mode = "main";
		New.Pins.insert(New.Pins.end() - 1, TorqueWorld::RoutePin{Lat, Lon, Cut(Short)});
		Kept = FString(UTF8_TO_TCHAR(New.Pins.back().Name.c_str()));
	}
	else
	{
		New.Mode = bHave && TorqueWorld::Route::IsStyle(Now.Mode) ? Now.Mode : std::string("scenic");
		New.Pins.push_back({HereLat, HereLon, Cut(HereName.IsEmpty() ? FString(TEXT("Here")) : HereName)});
		New.Pins.push_back({Lat, Lon, Cut(Short)});
	}
	const FString First(UTF8_TO_TCHAR(New.Pins.front().Name.c_str())), Last(UTF8_TO_TCHAR(New.Pins.back().Name.c_str()));
	const FString JourneyName = FString::Printf(TEXT("%s to %s"), First.IsEmpty() ? TEXT("the start") : *First, Last.IsEmpty() ? TEXT("the finish") : *Last);
	const FString Code = FString(UTF8_TO_TCHAR(TorqueWorld::Route::CodeOf(New).c_str()));
	if (Planner) Planner->SetJourney(New, FString());   // (its own hooks send it on in co-op; it names it)
	if (GetNetMode() != NM_Standalone)
	{
		if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>()) PS->ServerSetRideJourney(Code, JourneyName);
	}
	Real->KeepJourney(New, JourneyName);
	UE_LOG(LogTemp, Log, TEXT("Torque info: waypoint %s (%.5f, %.5f): journey '%s' (%d pins) %s"), *Short, Lat, Lon, *JourneyName, int32(New.Pins.size()), *Code);
	const bool bHost = GetNetMode() != NM_Client;
	const FString Where = Kept.IsEmpty() ? FString::Printf(TEXT("%s is a waypoint."), *Short)
		: FString::Printf(TEXT("%s is a waypoint on the way to %s."), *Short, *Kept);
	return Where + (bHost ? TEXT(" Esc > Plan a ride to ride it") : TEXT(" It's in the Route planner for everyone; the host sets off"));
}

void ATorquePlayerController::OpenPlanner()
{
	UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
	if (!Real) return;
	if (!Planner)
	{
		Planner = MakeUnique<FTorqueRoutePlanner>();
		// (the ride's level opens itself once the road is planned)
		Planner->OnRide = [this]() { CloseMenu(); };
	}
	// In co-op the journey is everyone's, and everyone plans it (beenster, build 141): any rider's pins
	// go to the host and out to the others' planners, and each game plans its road as it changes.
	// Only the host sets off, once everyone's game is ready.
	const bool bCoop = GetNetMode() == NM_ListenServer || GetNetMode() == NM_Client;
	if (bCoop)
	{
		Planner->OnJourneyChanged = [this](const FString& Code, const FString& Name)
		{
			if (ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>()) PS->ServerSetRideJourney(Code, Name);
		};
		Planner->SharedJourney = [this](FString& Code, FString& Name, FString& By, bool& bMine)
		{
			const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
			if (!GS || GS->RideJourney.IsEmpty()) return false;
			if (!GS->RideJourney.Split(TEXT("\n"), &Code, &Name)) Code = GS->RideJourney;
			By = GS->RideJourneyBy;
			bMine = PlayerState && GS->RideJourneyById == PlayerState->GetPlayerId();
			return true;
		};
	}
	else
	{
		Planner->OnJourneyChanged = nullptr;
		Planner->SharedJourney = nullptr;
	}
	Planner->WaitingFor = GetNetMode() == NM_ListenServer ? TFunction<FString()>([this]() { return RideWaitingFor(); }) : TFunction<FString()>();
	Planner->bCanSetOff = GetNetMode() != NM_Client;
	Planner->Open(Real);
	Open(EPage::Route);
}

FString ATorquePlayerController::RideWaitingFor() const
{
	const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	if (!GS) return FString();
	FString Code;
	if (!GS->RideJourney.Split(TEXT("\n"), &Code, nullptr)) Code = GS->RideJourney;
	TArray<FString> Names;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(PS);
		if (!TPS || TPS == GetPlayerState<ATorquePlayerState>()) continue;
		if (TPS->RideReadyFor != Code) Names.Add(TPS->GetPlayerName());
	}
	return FString::Join(Names, TEXT(", "));
}

void ATorquePlayerController::SyncRideJourney()
{
	if (GetNetMode() != NM_Client) return;
	const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
	ATorquePlayerState* Mine = GetPlayerState<ATorquePlayerState>();
	UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
	if (!GS || !Mine || !Real || GS->RideJourney.IsEmpty()) return;
	FString Code, Name;
	if (!GS->RideJourney.Split(TEXT("\n"), &Code, &Name)) Code = GS->RideJourney;
	if (Code == RideReadySent)
	{
		// (said again now and then until the host has it: a new level's player state starts without it)
		if (Mine->RideReadyFor != Code && FPlatformTime::Seconds() - RideReadyAt > 2.0)
		{
			RideReadyAt = FPlatformTime::Seconds();
			Mine->ServerSetRideReady(Code);
		}
		return;
	}
	// Planned already (the preview is of this very code): it's the ride now, and the host is told.
	const std::shared_ptr<TorqueWorld::RidePlan> Made = Real->PreviewPlan();
	if (Made && Made->Ok && UTF8_TO_TCHAR(TorqueWorld::Route::CodeOf(Made->Request).c_str()) == Code && !Real->IsPlanningRide())
	{
		if (Real->AdoptPreview())
		{
			RideReadySent = Code;
			RideReadyAt = FPlatformTime::Seconds();
			Mine->ServerSetRideReady(Code);
		}
		return;
	}
	// Else planned here once (a failure isn't tried again until the journey changes).
	if (Code == RidePlanningFor || Real->IsPlanningRide()) return;
	TorqueWorld::RouteRequest Want;
	if (!TorqueWorld::Route::FromCode(TCHAR_TO_UTF8(*Code), Want)) return;
	if (Real->StartJourney(Want, Name, false)) RidePlanningFor = Code;
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
	DropRow = DropCell = -1;
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
	if (Base == TEXT("profile"))
	{
		// "profile", or "profile:face" / "profile:helmet": the first row of that part picked (its view
		// in the picture, and for the helmet the colour picker).
		Open(EPage::Profile);
		const TArray<FRow> List = Rows();
		for (int32 I = 0; I < List.Num(); ++I)
			if ((Tab == TEXT("helmet") && List[I].bHelmet) || (Tab == TEXT("face") && List[I].bFace))
			{
				Selected = I;
				break;
			}
		UpdateProfilePreview();
		const ATorqueProfilePreview* Pv = Preview.Get();
		return FString::Printf(TEXT("menu: profile (Settings > Profile, %d rows, row %d '%s'); %s"), List.Num(), Selected,
			List.IsValidIndex(Selected) ? *List[Selected].Label : TEXT(""), Pv ? *Pv->Describe() : TEXT("no profile preview"));
	}
	else if (Base == TEXT("settings"))
	{
		// "settings:<tab>": graphics, sound, gameplay, controller, profile (or character), or keys / padcheck.
		static const TCHAR* Names[] = {TEXT("graphics"), TEXT("sound"), TEXT("gameplay"), TEXT("controller"), TEXT("character"), TEXT("profile")};
		static const EPage Pages[] = {EPage::Graphics, EPage::Sound, EPage::Gameplay, EPage::Controller, EPage::Character, EPage::Character};
		EPage Want = EPage::Graphics;
		for (int32 I = 0; I < 6; ++I)
			if (Tab == Names[I]) Want = Pages[I];
		if (Tab == TEXT("keys") || Tab == TEXT("padcheck"))
		{
			Open(EPage::Controller);
			Want = Tab == TEXT("keys") ? EPage::Keys : EPage::PadCheck;
		}
		Open(Want);
	}
	else if (Base == TEXT("keys")) { Open(EPage::Controller); Open(EPage::Keys); }
	else if (Base == TEXT("credits")) { Open(EPage::Credits); }
	else if (Base == TEXT("route"))
	{
		// "route" or "route:<a famous road's name, or a word of it>" (the Route planner, Ride UK stage 4)
		OpenPlanner();
		UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
		if (Planner && Real && !Tab.IsEmpty())
			for (const TorqueWorld::Route::FamousRoad& F : Real->FamousRoads())
				if (FString(UTF8_TO_TCHAR(F.Name.c_str())).ToLower().Contains(Tab))
				{
					Planner->SetJourney(F.Journey, FString(UTF8_TO_TCHAR(F.Name.c_str())));
					return FString::Printf(TEXT("menu: route planner on %s"), UTF8_TO_TCHAR(F.Name.c_str()));
				}
		return TEXT("menu: route planner");
	}
	else if (Base != TEXT("pause")) return TEXT("menu what? pause, profile, profile[:face|helmet], settings[:graphics|sound|gameplay|controller|profile|keys|padcheck], keys, credits, route[:<famous road>] or close (opened the pause menu)");
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
		ATorqueGameState::TestClockSet();
		if (GS) GS->SetHour(S.TimeOfDay);
	}
	else if (F == TEXT("ridedate") || F == TEXT("date"))
	{
		// (a day of the year, 1 .. 365, or 0 for today)
		S.RideDate = FMath::Clamp(N, 0, 365);
		if (GS) GS->SetRideDay(S.RideDate);
	}
	else if (F == TEXT("daynight")) S.bDayNight = bOn;
	else if (F == TEXT("realclock"))
	{
		// (Test runs keep the made clock unless a step turns this on.)
		S.bRealClock = bOn;
		ATorqueGameState::TestRealClock(bOn);
	}
	else if (F == TEXT("daylength")) S.DayLength = N;
	else if (F == TEXT("weather"))
	{
		// (At once, for the tests: the menu's own changes fade.)
		// (A preset sky by its key: set:weather=valley_fog.)
		if (!Value.IsEmpty() && !FChar::IsDigit(Value[0]) && Value[0] != TEXT('-'))
		{
			if (GS) GS->ChooseSkyPreset(Value);
		}
		else
		{
			S.WeatherMode = FMath::Clamp(N, 0, FTorqueSettings::WeatherPreset);
			if (GS) GS->WeatherModeChanged(0.0f);
		}
	}
	else if (F == TEXT("units")) S.Units = Value == TEXT("metric") || Value == TEXT("0") ? 0 : 1;
	else if (F == TEXT("telemetry")) S.bTelemetry = bOn;
	else if (F == TEXT("infopanel")) S.bInfoPanel = bOn;   // [info]
	else if (F == TEXT("placelookup")) S.bPlaceLookup = bOn;
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
	else if (F == TEXT("hdr")) S.bHdr = bOn;
	else if (F == TEXT("filmgrain")) S.FilmGrain = FMath::Clamp(V, 0.0f, 2.0f);
	else if (F == TEXT("vignette")) S.Vignette = FMath::Clamp(V, 0.0f, 2.0f);
	else if (F == TEXT("lensflares")) S.bLensFlares = bOn;
	else if (F == TEXT("ca")) S.bChromaticAberration = bOn;
	else if (F == TEXT("dof")) S.bDepthOfField = bOn;
	else if (F == TEXT("postprocess")) S.PostProcess = FMath::Clamp(N, -1, 4);
	else if (F == TEXT("shading")) S.Shading = FMath::Clamp(N, -1, 4);
	else if (F == TEXT("anisotropy")) S.Anisotropy = FMath::Clamp(N, -1, 4);
	else
		return FString::Printf(TEXT("set what? fov, timeofday, daynight, daylength, weather (0 auto .. 3 wet, 4 live, 5 preset, or a preset sky's key), units, telemetry, fps, autoshift, recentre, ")
			TEXT("lookintocorner, brightness, sharpness, bloom, quality, shadows, drawdistance, fog, clouds, ao, avatarshadows, aa, hdr, filmgrain, vignette, ")
			TEXT("lensflares, ca, dof, postprocess, shading, anisotropy (not %s)"), *Field);
	S.Apply(GetWorld());
	FString Sun;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = FString::Printf(TEXT(", sun pitch %.1f yaw %.1f intensity %.0f"), It->GetActorRotation().Pitch, It->GetActorRotation().Yaw,
			It->GetLightComponent() ? It->GetLightComponent()->Intensity : 0.0f);
		break;
	}
	const APlayerCameraManager* Cam = PlayerCameraManager;
	const FString ClockNow = !GS ? FString(TEXT("-")) : GS->GetHour() < 0.0f ? FString(TEXT("level's own")) : ATorqueGameState::ClockText(GS->GetHour());
	return FString::Printf(TEXT("set %s = %s: fov %.0f (camera now %.1f), clock %s, weather %d, telemetry %d, units %s%s"), *Field, *Value, S.Fov,
		Cam ? Cam->GetFOVAngle() : 0.0f, *ClockNow, GS ? GS->GetWeather() : -1, S.bTelemetry ? 1 : 0, S.Units == 0 ? TEXT("metric") : TEXT("imperial"), *Sun);
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
	if (ATorqueGeometryMode::For(this) || ATorqueDecalMode::For(this)) return false;
	const APawn* P = GetPawn();
	if (const ATorqueWalker* W = Cast<ATorqueWalker>(P); W && W->ComputerUpOrJustClosed()) return false;   // [shed] Esc closes the parts computer
	if (const ATorqueBikePawn* BP = Cast<ATorqueBikePawn>(P); BP && BP->GarageComputerUpOrJustClosed()) return false;   // [garage] and the garage's
	return P && (P->IsA<ATorqueBikePawn>() || P->IsA<ATorqueWalker>());
}

namespace
{
// Settings is Torque Shed's tabs: Graphics, Sound, Gameplay, Controller, Profile (was Character).
const TCHAR* TabNames[] = {TEXT("Graphics"), TEXT("Sound"), TEXT("Gameplay"), TEXT("Controller"), TEXT("Profile")};
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
	DropRow = DropCell = -1;
	Page = Tabs[Wrap(At + Dir, 5)];   // a tab, not a new page: Back still goes to where Settings was opened from
	Selected = 0;
	FirstRow = 0;
}

FString ATorquePlayerController::RideBikeName(int32 Bay) const
{
	const UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this);
	if (!Shed || !Shed->Exists(Bay)) return TEXT("none in the shed");
	return FString::Printf(TEXT("Bay %d: %s"), Bay + 1, *Shed->Bike(Bay).Name);
}

FString ATorquePlayerController::RideBikeTakenBy(int32 Bay) const
{
	// Another rider's pick (their player state's bay), by name.
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS || GetNetMode() == NM_Standalone) return FString();
	for (const APlayerState* Other : GS->PlayerArray)
	{
		const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(Other);
		if (TPS && TPS != GetPlayerState<APlayerState>() && TPS->Bay == Bay) return TPS->GetPlayerName();
	}
	return FString();
}

int32 ATorquePlayerController::NextRideBike(int32 From, int32 Dir) const
{
	// The next bike in the shed that no other rider has picked (From itself if there's none).
	const UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this);
	if (!Shed) return From;
	const int32 N = UTorqueBuildSubsystem::Bays;
	for (int32 Step = 1; Step <= N; ++Step)
	{
		const int32 Bay = ((From + Dir * Step) % N + N) % N;
		if (Shed->Exists(Bay) && RideBikeTakenBy(Bay).IsEmpty()) return Bay;
	}
	return From;
}

FString ATorquePlayerController::Title() const
{
	if (Page == EPage::Pause) return TEXT("PAUSED");
	if (Page == EPage::RideBike) return TEXT("WHICH BIKE?");
	if (Page == EPage::Profile) return TEXT("PROFILE");   // [profile]
	if (Page == EPage::Keys) return TEXT("KEY BINDINGS");
	if (Page == EPage::PadCheck) return TEXT("CONTROLLER CHECK");
	if (Page == EPage::Credits) return TEXT("MAP DATA");
	if (Page == EPage::Route) return TEXT("ROUTE PLANNER");
	if (Page == EPage::SkyPresets) return TEXT("PRESET SKY");
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
	// A choice of several: Left/Right (or Enter, or its arrows) steps it, then it's saved and put on the game.
	auto Setting = [this, &Add](const FString& Label, const FString& Value, const FString& Hint, TFunction<void(int32)> Step) {
		Add(Label, Value, Hint, [this, Step]() { Step(1); Changed(); }, [this, Step](int32 Dir) { Step(Dir); Changed(); });
	};
	// An on / off switch.
	auto Toggle = [this, &R](const FString& Label, bool& V, const FString& Hint) {
		bool* Ptr = &V;
		FRow Row;
		Row.Label = Label;
		Row.Value = OnOff(V);
		Row.Hint = Hint;
		Row.bToggle = true;
		Row.bOn = V;
		Row.Choose = [this, Ptr]() { *Ptr = !*Ptr; Changed(); };
		Row.Step = [this, Ptr](int32) { *Ptr = !*Ptr; Changed(); };
		R.Add(MoveTemp(Row));
	};
	// A slider: dragged with the mouse (saved and put on the game when it's let go), or stepped with Left / Right.
	auto Slide = [this, &R](const FString& Label, const FString& Value, const FString& Hint, float Frac, TFunction<void(int32)> StepBy,
					 TFunction<void(float)> SetTo) {
		FRow Row;
		Row.Label = Label;
		Row.Value = Value;
		Row.Hint = Hint;
		Row.Frac = FMath::Clamp(Frac, 0.0f, 1.0f);
		Row.Step = [this, StepBy](int32 Dir) { StepBy(Dir); Changed(); };
		Row.SetFrac = MoveTemp(SetTo);
		Row.Commit = [this]() { Changed(); };
		R.Add(MoveTemp(Row));
	};
	// A slider over a number, Lo .. Hi in steps of By.
	auto SettingF = [&Slide](const FString& Label, const FString& Value, const FString& Hint, float& V, float Lo, float Hi, float By) {
		float* Ptr = &V;
		Slide(Label, Value, Hint, (V - Lo) / (Hi - Lo), [Ptr, Lo, Hi, By](int32 Dir) { *Ptr = StepF(*Ptr, Dir, By, Lo, Hi); },
			[Ptr, Lo, Hi, By](float F) { *Ptr = FMath::Clamp(FMath::RoundToFloat(FMath::Lerp(Lo, Hi, F) / By) * By, Lo, Hi); });
	};
	// A section's heading (in capitals), or a note (not).
	auto Heading = [&Add](const FString& Words) { Add(Words, FString(), FString(), nullptr); };
	UTorqueSession* Session = UTorqueSession::Get(this);
	const bool bClient = GetNetMode() == NM_Client;
	const bool bHost = GetNetMode() == NM_ListenServer;

	switch (Page)
	{
	case EPage::Pause:
	{
		Add(TEXT("Continue"), FString(), FString(), [this]() { CloseMenu(); });
		Add(TEXT("Settings"), FString(), TEXT("Graphics, Sound, Gameplay, Controller, Profile"), [this]() { Open(EPage::Graphics); });
		// [profile] (also the picture top right): Settings > Profile
		Add(TEXT("Profile"), FString(), TEXT("You: your name, colour, riding style, your MetaHuman and your helmet"), [this]() { Open(EPage::Character); });
		ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn());
		ATorqueWalker* Walker = Cast<ATorqueWalker>(GetPawn());
		if (Bike && Bike->InYard())
		{
			// [yard] Riding about on the concrete outside the shed.
			Add(TEXT("Back to the door"), FString(), TEXT("Your bike back in front of the roller door (Home)"), [this, Bike]() {
				CloseMenu();
				Bike->RestartRide();
			});
			Add(TEXT("Park and walk back in"), FString(), FTorqueKeys::Text(ETorqueKey::BackToShed), [this]() {
				CloseMenu();
				TorqueYard::WalkBackIn(this);
			});
		}
		if ((Bike && Bike->InYard()) || Walker)
		{
			// [build 155] beenster: the hand-built Buttertubs level is no longer offered ("remove that menu
			// entry"): its edge is flat ground, and Ride UK rides the real pass. Every ride is Ride UK's.
			// The real world's roads (Ride UK), alone for now: bare ground and road, planned and fetched
			// when chosen, which takes a few seconds the first time.
			UTorqueWorldSubsystem* Real = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
			if (Real && bHost)
				Add(TEXT("Plan a ride (Ride UK, everyone)"), FString(),
					TEXT("The Route planner, together: everyone can pin the journey, and Ride it takes everyone once they're ready"), [this]() { OpenPlanner(); });
			if (Real && bClient)
			{
				// The host's journey, as this game has got on with planning it.
				const ATorqueGameState* RideGS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
				FString RideCode, RideName;
				if (RideGS && !RideGS->RideJourney.Split(TEXT("\n"), &RideCode, &RideName)) RideCode = RideGS->RideJourney;
				if (!RideCode.IsEmpty())
				{
					const FString State = RideCode == RideReadySent ? FString(TEXT("Planned here: ready when the host sets off"))
						: Real->IsPlanningRide() ? Real->RideNote() : (Real->RideNote().IsEmpty() ? FString(TEXT("Waiting to plan it")) : Real->RideNote());
					Add(FString(TEXT("Ride UK: ")) + (RideName.IsEmpty() ? FString(TEXT("the host's journey")) : RideName), FString(), State, nullptr);
					R.Last().bDisabled = true;
				}
				// [co-op] Everyone plans the journey together; the host sets off on it.
				Add(TEXT("Plan the ride together (Ride UK)"), FString(),
					TEXT("The Route planner: pin the journey with the others, each of you on the same map; the host sets off"), [this]() { OpenPlanner(); });
			}
			if (Real && !bClient && !bHost)
			{
				// (the fixed Hawes ride left the menu, beenster Nov 16; the tests still start it by name)
				Add(TEXT("Plan a ride (Ride UK)"), FString(),
					TEXT("The Route planner: pins on a map of the UK, famous roads, round trips, and journey codes to share"), [this]() { OpenPlanner(); });
			}
		}
		if (Bike && !Bike->InYard())
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
		// The makers of the real world's data (Ride UK), as their licences ask.
		Add(TEXT("Map data credits"), FString(), TEXT("Where the real world's hills, roads and land cover come from"), [this]() { Open(EPage::Credits); });
		Add(TEXT("Leave and return to title"), FString(),
			bHost ? TEXT("Ends the co-op shed: everyone who joined goes back to their title") : FString(), [this, Session]() {
				CloseMenu();
				if (Session) Session->BackToTitle();
			});
		Add(TEXT("Exit game"), FString(), FString(), [this]() { ConsoleCommand(TEXT("quit")); });
		break;
	}
	case EPage::Credits:
	{
		// One maker a row, named by what it gave; the whole credit is said beside the row chosen.
		const UTorqueWorldSubsystem* Maps = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
		const TArray<FString> Credited = Maps ? Maps->Credits() : TArray<FString>();
		for (const FString& Credit : Credited)
		{
			FString Source, Tail;
			if (!Credit.Split(TEXT(":"), &Source, &Tail)) Source = TEXT("Map data");
			Add(Source, FString(), Credit, []() {});
		}
		break;
	}
	case EPage::SkyPresets:
	{
		// The drop-down's list: every preset sky under its group; choosing one loads it and goes back.
		ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
		const FString HeldKey = S.WeatherMode == FTorqueSettings::WeatherPreset ? S.WeatherPresetKey : FString();
		std::string Group = "\x01";
		for (const TorqueSky::NamedSky& Each : ATorqueGameState::SkyPresets())
		{
			if (Each.Group != Group)
			{
				Group = Each.Group;
				if (!Group.empty()) Heading(FString(UTF8_TO_TCHAR(Group.c_str())).ToUpper());
			}
			const FString Key = UTF8_TO_TCHAR(Each.Key.c_str());
			Add(UTF8_TO_TCHAR(Each.Label.c_str()), Key == HeldKey ? FString(TEXT("held")) : FString(), UTF8_TO_TCHAR(Each.Note.c_str()),
				[this, GS, Key]() {
					if (GS) GS->ChooseSkyPreset(Key);
					Changed();
					Back();
					Selected = SkyListBackRow;
					FirstRow = SkyListBackFirst;
				});
		}
		Add(TEXT("Back"), FString(), FString(), [this]() {
			Back();
			Selected = SkyListBackRow;
			FirstRow = SkyListBackFirst;
		});
		break;
	}
	case EPage::RideBike:
	{
		// [co-op] Which bike to ride over Buttertubs: every bike in the shed (one another rider has
		// picked is theirs). Choosing it sets off, everyone with their own bike.
		ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn());
		ATorqueWalker* Walker = Cast<ATorqueWalker>(GetPawn());
		UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this);
		if (Shed)
			for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
			{
				if (!Shed->Exists(Bay)) continue;
				const FString Taken = RideBikeTakenBy(Bay);
				Add(RideBikeName(Bay), Bay == Shed->MyBike() ? FString(TEXT("your bike")) : Taken,
					Taken.IsEmpty() ? FString(TEXT("Set off on this one")) : FString::Printf(TEXT("%s rides this one"), *Taken),
					[this, Shed, Bay, Bike, Walker]() {
						Shed->SetMyBike(Bay);
						CloseMenu();
						if (Walker) Walker->StartAdventure();
						else if (Bike) TorqueFlow::RideOut(this);
					});
				if (!Taken.IsEmpty()) R.Last().bDisabled = true;
			}
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	}
	case EPage::Settings:
		// Torque Shed's Settings tabs (less its Debug tab).
		Add(TEXT("Graphics"), FString(), TEXT("Quality, screen, upscaling, anti-aliasing, shadows, lighting, fog, clouds, bloom"), [this]() { Open(EPage::Graphics); });
		Add(TEXT("Sound"), FString(), TEXT("Everything, sound effects, engines, the world around and the radio"), [this]() { Open(EPage::Sound); });
		Add(TEXT("Gameplay"), FString(), TEXT("Riding, field of view, camera, units, time and weather"), [this]() { Open(EPage::Gameplay); });
		Add(TEXT("Controller"), FString(), TEXT("Look speeds, invert, the lean stick, key bindings and a controller check"), [this]() { Open(EPage::Controller); });
		Add(TEXT("Profile"), FString(), TEXT("Your name, colour, riding style, MetaHuman and helmet, as everyone in co-op sees them"), [this]() { Open(EPage::Character); });
		Add(TEXT("Back"), FString(), FString(), [this]() { Back(); });
		break;
	case EPage::Graphics:
	{
		static const TCHAR* Qualities[] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic")};
		static const TCHAR* Modes[] = {TEXT("Fullscreen"), TEXT("Borderless window"), TEXT("Window")};
		static const TCHAR* Scales[] = {TEXT("Native (100%)"), TEXT("Quality (67%)"), TEXT("Balanced (58%)"), TEXT("Performance (50%)"),
			TEXT("Ultra performance (33%)")};
		// One of Unreal's scalability groups: the preset's, or a level of its own.
		auto Level = [&Setting, &S](const FString& Label, int32& V, const FString& Hint) {
			int32* Ptr = &V;
			Setting(Label, V < 0 ? FString::Printf(TEXT("Preset (%s)"), Qualities[S.Quality]) : FString(Qualities[FMath::Clamp(V, 0, 4)]), Hint,
				[Ptr](int32 D) { *Ptr = FMath::Clamp(*Ptr + D, -1, 4); });
		};
		auto Times = [](float V) { return V < 0.005f ? FString(TEXT("Off")) : Pct(V); };
		const bool bCustom = S.Shadows >= 0 || S.Textures >= 0 || S.GlobalIllumination >= 0 || S.Reflections >= 0 || S.Effects >= 0 ||
							 S.Foliage >= 0 || S.Fog >= 0 || S.Clouds >= 0 || S.PostProcess >= 0 || S.Shading >= 0 || S.Anisotropy >= 0 ||
							 !FMath::IsNearlyEqual(S.DrawDistance, 1.0f);
		Heading(TEXT("DISPLAY"));
		Setting(TEXT("Window mode"), Modes[S.WindowMode], TEXT("Fullscreen gives the most frames; borderless switches away from the game quickest"),
			[&S](int32 D) { S.WindowMode = Wrap(S.WindowMode + D, 3); });
		if (Resolutions.IsEmpty())
		{
			TArray<FIntPoint> Found;
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Found);
			for (const FIntPoint& P : Found)
				if (P.X >= 1280) Resolutions.AddUnique(P);
		}
		const int32 ResAt = Resolutions.IndexOfByKey(S.Resolution);   // -1: the desktop's
		Setting(TEXT("Resolution"), ResAt < 0 ? FString(TEXT("Desktop")) : FString::Printf(TEXT("%d x %d"), S.Resolution.X, S.Resolution.Y),
			TEXT("The screen's size in pixels. Desktop is your screen's own. To draw fewer pixels and keep the screen sharp, lower Render resolution instead"),
			[this, &S, ResAt](int32 D) {
				const int32 N = Resolutions.Num() + 1;   // and "desktop"
				const int32 Next = Wrap(ResAt + 1 + D, N) - 1;
				S.Resolution = Next < 0 ? FIntPoint(0, 0) : Resolutions[Next];
			});
		{
			const int32 Screens = FMath::Max(FTorqueSettings::MonitorCount(), 1);
			Setting(TEXT("Monitor"), FTorqueSettings::MonitorName(S.Monitor), TEXT("Which screen the game is on"),
				[&S, Screens](int32 D) { S.Monitor = Wrap(S.Monitor + 1 + D, Screens + 1) - 1; });
		}
		Toggle(TEXT("VSync"), S.bVSync, TEXT("No tearing, but each frame waits for the screen"));
		{
			int32 CapAt = 0;
			for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(FrameCaps); ++I)
				if (FrameCaps[I] == S.FrameCap) CapAt = I;
			Setting(TEXT("Frame rate limit"), S.FrameCap == 0 ? FString(TEXT("Unlimited")) : FString::Printf(TEXT("%d fps"), S.FrameCap),
				TEXT("The most frames a second the game draws: lower keeps the card cooler and quieter"),
				[&S, CapAt](int32 D) { S.FrameCap = FrameCaps[Wrap(CapAt + D, (int32)UE_ARRAY_COUNT(FrameCaps))]; });
		}
		if (FTorqueSettings::HdrAvailable())
			Toggle(TEXT("HDR"), S.bHdr, TEXT("High dynamic range output, for an HDR screen with HDR turned on in Windows"));
		else
		{
			Add(TEXT("HDR"), TEXT("Not available"), FString(), nullptr);
			R.Last().bDisabled = true;
		}
		SettingF(TEXT("Brightness"), Pct(S.Brightness), TEXT("50% to 150%. Set it so the darkest corners of the shed are only just visible"), S.Brightness, 0.5f, 1.5f, 0.05f);
		Toggle(TEXT("Show frame rate"), S.bFpsCounter, TEXT("Frames a second, at the top of the screen"));

		Heading(TEXT("UPSCALING AND ANTI-ALIASING"));
		const bool bDlss = FTorqueSettings::HasDlss();
		Setting(TEXT("Upscaler"), S.Upscaler == 1 && bDlss ? FString(TEXT("NVIDIA DLSS")) : FString(TEXT("Unreal TSR")),
			bDlss ? TEXT("DLSS on an RTX card: a sharper picture for the same frame rate. TSR works on every card")
				  : TEXT("Unreal's Temporal Super Resolution. DLSS appears here once NVIDIA's DLSS plugin is in the build"),
			[&S, bDlss](int32 D) { S.Upscaler = bDlss ? Wrap(S.Upscaler + D, 2) : 0; });
		const bool bDlssOn = S.Upscaler == 1 && bDlss;
		Setting(TEXT("Render resolution"), bDlssOn && S.UpscaleMode == 0 ? FString(TEXT("DLAA (100%)")) : FString(Scales[S.UpscaleMode]),
			TEXT("How many pixels are drawn before upscaling to the screen: lower gives more frames, Native the sharpest picture"),
			[&S](int32 D) { S.UpscaleMode = FMath::Clamp(S.UpscaleMode + D, 0, 4); });
		{
			static const TCHAR* Aa[] = {TEXT("TSR"), TEXT("TAA"), TEXT("FXAA"), TEXT("Off")};
			Setting(TEXT("Anti-aliasing"), bDlssOn ? FString(TEXT("DLSS")) : FString(Aa[FMath::Clamp(S.AntiAliasing, 0, 3)]),
				TEXT("Smooths jagged edges. TSR looks best, TAA is lighter, FXAA is fastest. With DLSS on, DLSS does it"),
				[&S](int32 D) { S.AntiAliasing = Wrap(S.AntiAliasing + D, 4); });
		}
		SettingF(TEXT("Sharpening"), Times(S.Sharpness), TEXT("Sharpens the picture after upscaling"), S.Sharpness, 0.0f, 1.0f, 0.05f);
		// [perf, build 150] NVIDIA's frame generation and Reflex, when the plugin is in the build.
		if (FTorqueSettings::HasFrameGen())
			Toggle(TEXT("Frame generation"), S.bFrameGen, TEXT("NVIDIA DLSS frame generation on an RTX 40 or 50 card: about twice the frames on screen, with Reflex keeping the controls quick"));
		else
		{
			Add(TEXT("Frame generation"), TEXT("Needs NVIDIA's DLSS plugin"), FString(), nullptr);
			R.Last().bDisabled = true;
		}
		if (FTorqueSettings::HasReflex()) Toggle(TEXT("NVIDIA Reflex"), S.bReflex, TEXT("Cuts the delay between the controls and the screen (always on with frame generation)"));

		Heading(TEXT("QUALITY"));
		// (Performance, after Cinematic: High's groups without Lumen, for 100+ fps; build 150)
		Setting(TEXT("Quality preset"), bCustom ? FString(TEXT("Custom")) : S.bPerformance ? FString(TEXT("Performance")) : FString(Qualities[S.Quality]),
			TEXT("Sets every quality option below at once. Performance is High without Lumen's lighting, for the most frames. Changing one of them afterwards makes it Custom"), [&S](int32 D) {
				const int32 At = FMath::Clamp((S.bPerformance ? 5 : S.Quality) + D, 0, 5);
				S.bPerformance = At == 5;
				S.Quality = At == 5 ? 2 : At;
				S.Shadows = S.Textures = S.GlobalIllumination = S.Reflections = S.Effects = S.Foliage = S.Fog = S.Clouds = -1;
				S.PostProcess = S.Shading = S.Anisotropy = -1;
				S.DrawDistance = 1.0f;
			});
		Level(TEXT("Shadows"), S.Shadows, TEXT("Shadow detail, softness and how far away they're drawn"));
		Level(TEXT("Textures"), S.Textures, TEXT("Texture detail. Lower it if the card runs short of memory"));
		{
			static const TCHAR* Filters[] = {TEXT("Bilinear"), TEXT("2x anisotropic"), TEXT("4x anisotropic"), TEXT("8x anisotropic"), TEXT("16x anisotropic")};
			Setting(TEXT("Texture filtering"), S.Anisotropy < 0 ? FString(TEXT("Preset")) : FString(Filters[S.Anisotropy]),
				TEXT("Keeps the floor and the road sharp at a slant"), [&S](int32 D) { S.Anisotropy = FMath::Clamp(S.Anisotropy + D, -1, 4); });
		}
		SettingF(TEXT("View distance"), Pct(S.DrawDistance), TEXT("50% to 200% of the preset's: how far away small things are still drawn"),
			S.DrawDistance, 0.5f, 2.0f, 0.05f);
		Level(TEXT("Shading"), S.Shading, TEXT("How finely materials are lit: paint, metal, rubber"));
		Level(TEXT("Global illumination"), S.GlobalIllumination, TEXT("Lumen's bounce light, the light that fills the shed"));
		Level(TEXT("Reflections"), S.Reflections, TEXT("Lumen's reflections: paint, chrome, wet roads"));
		Level(TEXT("Post-processing"), S.PostProcess, TEXT("Depth of field, bloom and motion blur quality"));
		Level(TEXT("Effects"), S.Effects, TEXT("Spray, dust and smoke"));
		Level(TEXT("Foliage"), S.Foliage, TEXT("How thick the grass and plants are, and how far away"));
		{
			static const TCHAR* Fogs[] = {TEXT("Off"), TEXT("Low"), TEXT("High")};
			Setting(TEXT("Volumetric fog"), S.Fog < 0 ? FString(TEXT("Preset")) : FString(Fogs[S.Fog]), TEXT("Light shafts and haze you can see"),
				[&S](int32 D) { S.Fog = FMath::Clamp(S.Fog + D, -1, 2); });
			static const TCHAR* CloudLevels[] = {TEXT("Off"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Ultra")};
			Setting(TEXT("Volumetric clouds"), S.Clouds < 0 ? FString(TEXT("Preset")) : FString(CloudLevels[S.Clouds]), TEXT("The sky's clouds: off, or finer the higher"),
				[&S](int32 D) { S.Clouds = FMath::Clamp(S.Clouds + D, -1, 4); });
		}
		Toggle(TEXT("Ambient occlusion"), S.bAmbientOcclusion, TEXT("Soft shade in corners and under things"));
		Toggle(TEXT("Character shadows"), S.bAvatarShadows, TEXT("Players and people on foot cast shadows"));

		Heading(TEXT("POST-PROCESSING"));
		SettingF(TEXT("Bloom"), Times(S.Bloom), TEXT("The glow round bright lights: off, up to twice the usual"), S.Bloom, 0.0f, 2.0f, 0.1f);
		Toggle(TEXT("Motion blur"), S.bMotionBlur, TEXT("Blurs fast movement, as a camera does"));
		Toggle(TEXT("Depth of field"), S.bDepthOfField, TEXT("Things out of focus go soft (the part previews, the photo cameras)"));
		Toggle(TEXT("Lens flares"), S.bLensFlares, TEXT("Streaks and rings from the sun and bright lamps"));
		Toggle(TEXT("Chromatic aberration"), S.bChromaticAberration, TEXT("Colour fringes towards the edges, as a real lens has"));
		SettingF(TEXT("Film grain"), Times(S.FilmGrain), TEXT("A fine noise over the picture, as film has"), S.FilmGrain, 0.0f, 2.0f, 0.1f);
		SettingF(TEXT("Vignette"), Times(S.Vignette), TEXT("Darkening towards the corners of the screen"), S.Vignette, 0.0f, 2.0f, 0.1f);

		Heading(TEXT("ADVANCED"));
		Add(TEXT("Graphics API"), TEXT("DirectX 12"), FString(), nullptr);
		R.Last().bDisabled = true;
		break;
	}
	case EPage::Sound:
		Heading(TEXT("VOLUME"));
		SettingF(TEXT("Master"), Pct(S.Master), TEXT("Everything"), S.Master, 0.0f, 1.0f, 0.05f);
		SettingF(TEXT("Sound effects"), Pct(S.Sfx), TEXT("Tools, knocks, dings, the washer, the shed's machines"), S.Sfx, 0.0f, 1.0f, 0.05f);
		SettingF(TEXT("Engines"), Pct(S.Engine), TEXT("Your bike and everyone else's"), S.Engine, 0.0f, 1.0f, 0.05f);
		SettingF(TEXT("World"), Pct(S.Ambience), TEXT("Wind, birds, sheep and the shed's hum"), S.Ambience, 0.0f, 1.0f, 0.05f);
		SettingF(TEXT("Radio"), Pct(S.Radio), TEXT("The shed radio"), S.Radio, 0.0f, 1.0f, 0.05f);
		break;
	case EPage::Gameplay:
	{
		// Time and weather first (what's changed most), then riding, the camera, units, co-op, and the
		// Ride UK disk cache last (beenster, build 141: the page made easier to use).
		Heading(TEXT("TIME AND WEATHER"));
		{
			ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr;
			// The host's: on a joined machine they're shown, not changed.
			auto HostOnly = [&R, bClient]() {
				if (!bClient) return;
				FRow& Last = R.Last();
				Last.Choose = nullptr;
				Last.Step = nullptr;
				Last.bToggle = false;
				Last.bDisabled = true;
				Last.SetFrac = nullptr;
				Last.Commit = nullptr;
				Last.Hint += TEXT(" (set by the host)");
			};
			Toggle(TEXT("Real time of day"), S.bRealClock, TEXT("The clock and the sun follow the real time and date where you ride (night falls when it really does)"));
			HostOnly();
			// With the real clock on, the made clock's rows are shown, not changed.
			auto RealClockOnly = [&R, &S]() {
				if (!S.bRealClock) return;
				FRow& Last = R.Last();
				Last.Choose = nullptr;
				Last.Step = nullptr;
				Last.bToggle = false;
				Last.bDisabled = true;
				Last.SetFrac = nullptr;
				Last.Commit = nullptr;
				Last.Hint += TEXT(" (off while Real time of day is on)");
			};
			Toggle(TEXT("Day/night cycle"), S.bDayNight, TEXT("The clock runs, the sun crosses the sky and night falls"));
			HostOnly();
			RealClockOnly();
			{
				const TArray<int32>& Lengths = FTorqueSettings::DayLengths();
				const int32 At = FMath::Max(Lengths.IndexOfByKey(S.DayLength), 0);
				const int32 M = Lengths[At];
				const FString Len = M < 60 ? FString::Printf(TEXT("%d minutes"), M) : M == 60 ? FString(TEXT("1 hour")) : FString::Printf(TEXT("%d hours"), M / 60);
				Slide(TEXT("Day length"), Len, TEXT("Real time for a whole day, with the cycle on"), Lengths.Num() > 1 ? float(At) / (Lengths.Num() - 1) : 0.0f,
					[&S, At](int32 D) { const TArray<int32>& L = FTorqueSettings::DayLengths(); S.DayLength = L[FMath::Clamp(At + D, 0, L.Num() - 1)]; },
					[&S](float F) { const TArray<int32>& L = FTorqueSettings::DayLengths(); S.DayLength = L[FMath::Clamp(FMath::RoundToInt(F * (L.Num() - 1)), 0, L.Num() - 1)]; });
				HostOnly();
				RealClockOnly();
			}
			{
				const float Built = GS ? GS->BuiltHour() : 15.0f;
				const float Now = GS ? GS->GetHour() : S.TimeOfDay;
				const FString When = Now < 0.0f ? FString::Printf(TEXT("Level's own (about %s)"), *ATorqueGameState::ClockText(Built)) : ATorqueGameState::ClockText(Now);
				// A slider (beenster, build 141): dragged, the sun moves as you go; Left / Right half an hour.
				Slide(TEXT("Time of day"), When, TEXT("Drag for any hour; Left / Right, half an hour a step"), (Now < 0.0f ? Built : Now) / 24.0f,
					[GS, &S, Built, Now](int32 D) {
						const float From = Now < 0.0f ? FMath::RoundToFloat(Built * 2.0f) / 2.0f - 0.5f * D : FMath::RoundToFloat(Now * 2.0f) / 2.0f;
						S.TimeOfDay = FMath::Fmod(From + 0.5f * D + 24.0f, 24.0f);
						if (GS) GS->SetHour(S.TimeOfDay);
					},
					[GS, &S](float F) {
						// To the quarter hour, 00:00 .. 23:45.
						S.TimeOfDay = FMath::Clamp(FMath::RoundToFloat(F * 96.0f), 0.0f, 95.0f) / 4.0f;
						if (GS) GS->SetHour(S.TimeOfDay);
					});
				HostOnly();
				RealClockOnly();
				if (Now >= 0.0f && !bClient && !S.bRealClock)
					Add(TEXT("Back to the level's own light"), FString(), TEXT("The sun where the level was made with it (the clock stops)"), [this, GS, &S]() {
						S.TimeOfDay = -1.0f;
						S.bDayNight = false;
						if (GS) GS->SetHour(-1.0f);
						Changed();
					});
			}
			{
				// The ride's date (beenster Nov 17): the sun's path, sunrise and sunset, the night sky, the season
				// on the land (from the next ride) and the HUD's date. Build 155 feedback: a pull-down each for the
				// day, the month and the year, and Reset on the same line (back to today's date and the time now).
				// Left / Right still steps a day; Enter opens the day's pull-down.
				const FDateTime Now = FDateTime::Now();
				const int32 ThisYear = Now.GetYear();
				const int32 Year = S.RideYear > 0 ? S.RideYear : ThisYear;
				const FDateTime Shown = S.RideDate > 0 ? ATorqueGameState::DateOfDay(S.RideDate) : Now;
				const int32 Day = Shown.GetDay(), Month = Shown.GetMonth();
				// Day, month, year picked: the day of the year the game keeps (the day kept inside the month).
				auto SetDate = [this, GS, &S](int32 NewYear, int32 NewMonth, int32 NewDay) {
					NewDay = FMath::Clamp(NewDay, 1, FDateTime::DaysInMonth(NewYear, NewMonth));
					S.RideYear = NewYear == FDateTime::Now().GetYear() ? 0 : NewYear;
					S.RideDate = FDateTime(NewYear, NewMonth, NewDay).GetDayOfYear();
					if (GS) GS->SetRideDay(S.RideDate);
					Changed();
				};
				FRow Row;
				Row.Label = TEXT("Ride date");
				Row.Value = S.RideDate > 0 ? Shown.ToString(TEXT("%d %B %Y")) : FString::Printf(TEXT("Today (%s)"), *Now.ToString(TEXT("%d %B %Y")));
				Row.Hint = TEXT("The day the sun, the night sky and the season are for: pick the day, month and year. Reset goes back to today's date and the time now. Left / Right steps a day");
				{
					FRow::FCell D;
					for (int32 I = 1; I <= FDateTime::DaysInMonth(Year, Month); ++I) D.Options.Add(FString::FromInt(I));
					D.Pick = Day - 1;
					D.Text = D.Options[D.Pick];
					D.Set = [SetDate, Year, Month](int32 I) { SetDate(Year, Month, I + 1); };
					Row.Cells.Add(MoveTemp(D));
				}
				{
					FRow::FCell M;
					static const TCHAR* Months[] = {TEXT("January"), TEXT("February"), TEXT("March"), TEXT("April"), TEXT("May"), TEXT("June"),
						TEXT("July"), TEXT("August"), TEXT("September"), TEXT("October"), TEXT("November"), TEXT("December")};
					for (const TCHAR* One : Months) M.Options.Add(One);
					M.Pick = Month - 1;
					M.Text = M.Options[M.Pick];
					M.Set = [SetDate, Year, Day](int32 I) { SetDate(Year, I + 1, Day); };
					Row.Cells.Add(MoveTemp(M));
				}
				{
					FRow::FCell Yr;
					const int32 First = FMath::Min(ThisYear - 30, Year), Last = FMath::Max(ThisYear + 10, Year);
					for (int32 I = First; I <= Last; ++I) Yr.Options.Add(FString::FromInt(I));
					Yr.Pick = Year - First;
					Yr.Text = Yr.Options[Yr.Pick];
					Yr.Set = [SetDate, First, Month, Day](int32 I) { SetDate(First + I, Month, Day); };
					Row.Cells.Add(MoveTemp(Yr));
				}
				{
					FRow::FCell ResetBox;
					ResetBox.Text = TEXT("Reset");
					ResetBox.Press = [this, GS, &S]() {
						S.RideDate = 0;
						S.RideYear = 0;
						if (GS) GS->SetRideDay(0);
						// The time now too (the made clock's; the real clock already is).
						if (!S.bRealClock)
						{
							const FDateTime WallNow = FDateTime::Now();
							S.TimeOfDay = FMath::Clamp(WallNow.GetHour() + WallNow.GetMinute() / 60.0f, 0.0f, 23.99f);
							if (GS) GS->SetHour(S.TimeOfDay);
						}
						Changed();
					};
					Row.Cells.Add(MoveTemp(ResetBox));
				}
				const int32 RowIndex = R.Num();
				Row.Choose = [this, RowIndex]() {
					const TArray<FRow> Again = Rows();
					if (Again.IsValidIndex(RowIndex)) OpenDrop(Again[RowIndex], RowIndex, 0);
				};
				Row.Step = [SetDate, Shown](int32 Dir) {
					const FDateTime To = Shown + FTimespan::FromDays(Dir);
					SetDate(To.GetYear(), To.GetMonth(), To.GetDay());
				};
				R.Add(MoveTemp(Row));
				if (bClient)
				{
					FRow& Last = R.Last();
					Last.Choose = nullptr;
					Last.Step = nullptr;
					Last.bDisabled = true;
					for (FRow::FCell& C : Last.Cells) { C.Set = nullptr; C.Press = nullptr; }
					Last.Hint += TEXT(" (set by the host)");
				}
			}
			{
				// Live first (the default), then the choices the game always had, then a preset sky held.
				static const int32 Order[] = {FTorqueSettings::WeatherLive, 0, 1, 2, 3, FTorqueSettings::WeatherPreset};
				static const TCHAR* Kinds[] = {TEXT("Changes by itself"), TEXT("Always sunny"), TEXT("Always overcast"), TEXT("Always wet roads"), TEXT("Live"),
					TEXT("Preset sky")};
				const FString NowText = GS ? FString::Printf(TEXT(" Now: %s."), *GS->WeatherText(S.Units == 1)) : FString();
				const FString Hint = S.WeatherMode == FTorqueSettings::WeatherLive
					? FString::Printf(TEXT("The real weather where you ride, updated every 15 minutes. Weather data by Open-Meteo.com (CC BY 4.0).%s"), *NowText)
					: FString::Printf(TEXT("%s while riding changes it now.%s"), *FTorqueKeys::Text(ETorqueKey::Weather), *NowText);
				Setting(TEXT("Weather"), Kinds[FMath::Clamp(S.WeatherMode, 0, FTorqueSettings::WeatherPreset)], Hint, [GS, &S](int32 D) {
						int32 I = 0;
						while (I < (int32)UE_ARRAY_COUNT(Order) - 1 && Order[I] != S.WeatherMode) ++I;
						S.WeatherMode = Order[Wrap(I + D, (int32)UE_ARRAY_COUNT(Order))];
						if (GS) GS->WeatherModeChanged();
					});
				HostOnly();
				// A drop-down of preset skies, for testing the skies quickly (beenster, Nov 14): Enter opens the
				// list, Left / Right steps through it here; the weather key steps it while riding.
				const FString Held = GS ? GS->SkyPresetLabel() : FString();
				Add(TEXT("Preset sky"), Held.IsEmpty() ? FString(TEXT("Choose...")) : Held,
					FString::Printf(TEXT("Load a ready-made sky: fair, grey, rain, high cloud, mist and fog, evening light, storms and snow, recorded days (Data/Sky/presets.json). Held, %s while riding steps to the next"),
						*FTorqueKeys::Text(ETorqueKey::Weather)),
					[this]() { OpenSkyPresets(); },
					[this, GS](int32 D) {
						if (GS) GS->ChooseSkyPreset(FString(), D);
						Changed();
					});
				HostOnly();
			}
		}
		Heading(TEXT("RIDING"));
		{
			float* Ptr = &S.Realism;
			auto Tell = [this, Ptr]() {
				if (ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetPawn())) Bike->Assist = *Ptr;
			};
			Slide(TEXT("Riding realism"), FString::Printf(TEXT("%.2f  %s"), S.Realism, *FTorqueSettings::RealismWord(S.Realism)),
				FTorqueSettings::RealismText(S.Realism) + TEXT(". 0 is full simulation, 1 is easy; [ and ] change it while riding"), S.Realism,
				[Ptr, Tell](int32 D) { *Ptr = StepF(*Ptr, D, 0.05f, 0.0f, 1.0f); Tell(); },
				[Ptr, Tell](float F) { *Ptr = FMath::Clamp(FMath::RoundToFloat(F * 20.0f) / 20.0f, 0.0f, 1.0f); Tell(); });
		}
		Toggle(TEXT("Automatic gearbox"), S.bAutoShift, FString::Printf(TEXT("The gears change themselves (%s while riding)"), *FTorqueKeys::Text(ETorqueKey::AutoShift)));
		Toggle(TEXT("Telemetry overlay"), S.bTelemetry, FString::Printf(TEXT("Live strips of what the bike is doing, while riding (%s)"), *FTorqueKeys::Text(ETorqueKey::Telemetry)));
		// [info]
		Toggle(TEXT("Info panel"), S.bInfoPanel, FString::Printf(TEXT("Date and time, build, weather and forecast, where you are and who's riding, down the left while riding (%s slides it in and out)"),
			*FTorqueKeys::Text(ETorqueKey::InfoPanel)));
		Toggle(TEXT("Info panel: about this place"), S.bPlaceLookup, TEXT("The panel asks OpenStreetMap and Wikipedia about where you are (needs the internet; off asks nothing)"));
		Toggle(TEXT("Look into corners"), S.bLookIntoCorner, TEXT("Riders' heads turn towards the inside of a bend, and your view does in first person"));
		Heading(TEXT("CAMERA"));
		SettingF(TEXT("Field of view"), FString::Printf(TEXT("%d°"), FMath::RoundToInt(S.Fov)),
			TEXT("60 to 110 degrees. On foot it's this; the bike's cameras widen or narrow by as much"), S.Fov, 60.0f, 110.0f, 1.0f);
		Toggle(TEXT("Chase camera recentres"), S.bCameraRecentre, TEXT("The chase camera swings back behind the bike after you let go of the mouse or right stick"));
		{
			// [camera] The third-person riding camera, kept with your character (FTorqueProfile).
			FTorqueProfile& Cam = FTorqueProfile::Mine();
			auto ChaseSlide = [&R](const FString& Label, const FString& Value, const FString& Hint, float& V, float Lo, float Hi, float By) {
				float* Ptr = &V;
				FRow Row;
				Row.Label = Label;
				Row.Value = Value;
				Row.Hint = Hint;
				Row.Frac = FMath::Clamp((V - Lo) / (Hi - Lo), 0.0f, 1.0f);
				Row.Step = [Ptr, Lo, Hi, By](int32 Dir) { *Ptr = StepF(*Ptr, Dir, By, Lo, Hi); FTorqueProfile::Mine().Save(); };
				Row.SetFrac = [Ptr, Lo, Hi, By](float F) { *Ptr = FMath::Clamp(FMath::RoundToFloat(FMath::Lerp(Lo, Hi, F) / By) * By, Lo, Hi); };
				Row.Commit = []() { FTorqueProfile::Mine().Save(); };
				R.Add(MoveTemp(Row));
			};
			const FString Adjust = FString::Printf(TEXT(" (riding in third person: %s / %s, %s / %s, %s / %s, or R1 held with the right stick and D-pad)"),
				*FTorqueKeys::Text(ETorqueKey::CamCloser), *FTorqueKeys::Text(ETorqueKey::CamFarther), *FTorqueKeys::Text(ETorqueKey::CamHigher),
				*FTorqueKeys::Text(ETorqueKey::CamLower), *FTorqueKeys::Text(ETorqueKey::CamTiltUp), *FTorqueKeys::Text(ETorqueKey::CamTiltDown));
			ChaseSlide(TEXT("Third person: distance"), FString::Printf(TEXT("%.1f m"), Cam.ChaseDist / 100.0f),
				TEXT("How far behind the bike the camera rides") + Adjust, Cam.ChaseDist, FTorqueProfile::ChaseDistMin, FTorqueProfile::ChaseDistMax, 10.0f);
			ChaseSlide(TEXT("Third person: height"), FString::Printf(TEXT("%.1f m"), (Cam.ChaseHeight + 110.0f) / 100.0f),
				TEXT("How high above the road the camera rides"), Cam.ChaseHeight, FTorqueProfile::ChaseHeightMin, FTorqueProfile::ChaseHeightMax, 5.0f);
			ChaseSlide(TEXT("Third person: tilt"), FString::Printf(TEXT("%+.0f°"), Cam.ChaseTilt),
				TEXT("How far the camera looks down at the bike (below 0: up the road)"), Cam.ChaseTilt, FTorqueProfile::ChaseTiltMin, FTorqueProfile::ChaseTiltMax, 1.0f);
			Add(TEXT("Third person: reset"), FString(), TEXT("Back to the usual distance, height and tilt"), []() { FTorqueProfile::Mine().ResetChase(); FTorqueProfile::Mine().Save(); });
			// First person, the same (beenster): each player's own preferred view, kept in their profile.
			ChaseSlide(TEXT("First person: forward / back"), FString::Printf(TEXT("%+.0f cm"), Cam.FirstForward),
				TEXT("Your eyes nearer the bars or further back (the same keys and pad buttons as third person, riding in first person)"),
				Cam.FirstForward, FTorqueProfile::FirstForwardMin, FTorqueProfile::FirstForwardMax, 1.0f);
			ChaseSlide(TEXT("First person: height"), FString::Printf(TEXT("%+.0f cm"), Cam.FirstHeight),
				TEXT("Your eyes higher or lower than the rider's own"), Cam.FirstHeight, FTorqueProfile::FirstHeightMin, FTorqueProfile::FirstHeightMax, 1.0f);
			ChaseSlide(TEXT("First person: tilt"), FString::Printf(TEXT("%+.0f°"), Cam.FirstTilt),
				TEXT("Look further down at the clocks and hands, or up the road (0: aimed so your hands on the bars show on every bike)"),
				Cam.FirstTilt, FTorqueProfile::FirstTiltMin, FTorqueProfile::FirstTiltMax, 1.0f);
			Add(TEXT("First person: reset"), FString(), TEXT("Back to the rider's own eyes and the usual tilt"), []() { FTorqueProfile::Mine().ResetFirst(); FTorqueProfile::Mine().Save(); });
		}
		Heading(TEXT("UNITS"));
		Setting(TEXT("Units"), S.Units == 0 ? FString(TEXT("Metric (km/h, km)")) : FString(TEXT("Imperial (mph, miles)")), TEXT("Speeds and distances on the HUD"),
			[&S](int32) { S.Units = 1 - S.Units; });
		Heading(TEXT("CO-OP"));
		Toggle(TEXT("Open my router for internet friends"), S.bOpenRouter,
			TEXT("Hosting asks your router to open UDP port 7777 (UPnP), and closes it again after. Off: forward it by hand"));
		// [reports] beenster, build 155: a friend's game crashed. The crash and freeze reports, to zip up and send.
		{
			const int32 Reports = TorqueReports::Count();
			Add(TEXT("Crash and freeze reports"), Reports > 0 ? FString::Printf(TEXT("%d saved"), Reports) : FString(TEXT("None")),
				TEXT("If the game crashed or froze, a report is saved here (the call stack and the log). Opens the folder: zip it up and send it to fix the cause"),
				[]() { TorqueReports::OpenFolder(); });
		}
		// [Ride UK stage 8] The real world kept on this machine.
		Heading(TEXT("RIDE UK: THE REAL WORLD"));
		{
			UTorqueWorldSubsystem* Wss = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTorqueWorldSubsystem>() : nullptr;
			TWeakObjectPtr<UTorqueWorldSubsystem> WeakWss(Wss);
			if (Wss && Wss->CacheWords().IsEmpty()) Wss->WeighCache();
			static const int32 Limits[] = {2, 5, 10, 20, 50, 100};
			int32 On = 2;
			for (int32 I = 0; I < 6; ++I)
				if (S.WorldCacheGb >= Limits[I]) On = I;
			Setting(TEXT("Kept on disk"), FString::Printf(TEXT("Up to %d GB"), S.WorldCacheGb),
				TEXT("Roads and ground fetched for rides are kept so they ride again offline; past this, those ridden longest ago go"),
				[&S, On, WeakWss](int32 D) {
					S.WorldCacheGb = Limits[FMath::Clamp(On + D, 0, 5)];
					if (WeakWss.IsValid()) WeakWss->WorldSettingsChanged();
				});
			{
				bool* Ptr = &S.bWorldOffline;
				FRow Row;
				Row.Label = TEXT("Offline");
				Row.Value = OnOff(S.bWorldOffline);
				Row.Hint = TEXT("Nothing is fetched: only roads ridden before can be ridden (the planner's map shows them)");
				Row.bToggle = true;
				Row.bOn = S.bWorldOffline;
				Row.Choose = [this, Ptr, WeakWss]() { *Ptr = !*Ptr; Changed(); if (WeakWss.IsValid()) WeakWss->WorldSettingsChanged(); };
				Row.Step = [this, Ptr, WeakWss](int32) { *Ptr = !*Ptr; Changed(); if (WeakWss.IsValid()) WeakWss->WorldSettingsChanged(); };
				R.Add(MoveTemp(Row));
			}
			Add(TEXT("Clear what's kept"), Wss && !Wss->CacheWords().IsEmpty() ? Wss->CacheWords() : FString(TEXT("Weighing...")),
				TEXT("Deletes every road and square of ground fetched (the next ride fetches what it needs again; the ride diary stays)"),
				[this, WeakWss]() {
					FString Why = TEXT("No world here");
					if (WeakWss.IsValid()) WeakWss->ClearCache(Why);
					ShowNote(Why, 3.0);
				});
		}
		break;
	}
	case EPage::Controller:
	{
		Heading(TEXT("LOOKING"));
		{
			const int32 N = UE_ARRAY_COUNT(MouseSpeeds);
			int32 Near = 0;
			for (int32 I = 1; I < N; ++I)
				if (FMath::Abs(MouseSpeeds[I] - S.MouseLook) < FMath::Abs(MouseSpeeds[Near] - S.MouseLook)) Near = I;
			float* Ptr = &S.MouseLook;
			Slide(TEXT("Mouse sensitivity"), FString::Printf(TEXT("%.2fx"), S.MouseLook), TEXT("0.1x to 10x the default, on foot and on the bike"),
				(float)Near / (N - 1), [Ptr](int32 D) { *Ptr = StepList(*Ptr, D, MouseSpeeds, UE_ARRAY_COUNT(MouseSpeeds)); },
				[Ptr, N](float F) { *Ptr = MouseSpeeds[FMath::Clamp(FMath::RoundToInt(F * (N - 1)), 0, N - 1)]; });
		}
		SettingF(TEXT("Stick sensitivity"), FString::Printf(TEXT("%.2fx"), S.PadLook), TEXT("The right stick, on foot and on the bike"), S.PadLook, 0.25f, 3.0f, 0.25f);
		Toggle(TEXT("Invert look"), S.bInvertLook, TEXT("Up on the mouse or stick looks down"));
		Heading(TEXT("RIDING"));
		SettingF(TEXT("Lean stick dead zone"), Pct(S.StickDeadZone), TEXT("How far the stick moves before the bike leans"), S.StickDeadZone, 0.02f, 0.25f, 0.01f);
		SettingF(TEXT("Lean response"), FString::Printf(TEXT("%.1fx"), S.Steering), TEXT("How much lean the stick and the lean keys ask for"), S.Steering, 0.5f, 2.0f, 0.1f);
		Heading(TEXT("CONTROLS"));
		Add(TEXT("Key bindings"), FString(), TEXT("Every action and its keys: pick one and press the new key"), [this]() { KeySlot = 0; Open(EPage::Keys); });
		Add(TEXT("Controller check"), FString(), TEXT("The sticks and buttons as the game reads them, and Recalibrate"), [this]() { Open(EPage::PadCheck); });
		Heading(TEXT("PAD"));
		Heading(TEXT("On foot: left stick walks, right stick looks, Cross / A uses, L1 / R1 pick a tool, Square / X washes, Triangle / Y rides out, D-pad left pings."));
		Heading(TEXT("Riding: R2 throttle, L2 front brake, Circle / B rear brake, left stick lean and weight, Cross / A up a gear, Square / X down, hold LB clutch, hold RB look behind."));
		Heading(TEXT("Triangle / Y gets on and off, D-pad left / right indicators, up lights, down ABS, L3 horn, R3 nod (hold: wave), View camera. The bike starts in neutral."));
		Heading(TEXT("Anywhere: Esc, Options or Start opens the menu."));
		break;
	}
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
				Value = FString::Printf(TEXT("Press a key for the %s slot..."), ListenSlot == 0 ? TEXT("first") : TEXT("second"));
			Add(Act.Label, Value, TEXT("Click a key, or pick one with Left / Right and press Enter, then press the new key or mouse button. Esc cancels, Delete clears it"),
				[this, I]() {
					ListenAction = I;
					ListenSlot = KeySlot;
					ListenFrame = GFrameCounter;
				},
				[this](int32 D) { KeySlot = Wrap(KeySlot + D, FTorqueKeys::Slots); });
			FRow& Row = R.Last();
			if (ListenAction == I) Row.Step = nullptr;   // listening: the words, not arrows
			else
			{
				for (int32 Slot = 0; Slot < FTorqueKeys::Slots; ++Slot) Row.Caps.Add(FTorqueKeys::KeyName(FTorqueKeys::Get(A, Slot)));
				Row.CapOn = KeySlot;
			}
			const FString Clash = FTorqueKeys::Clash(A);
			if (!Clash.IsEmpty()) Row.Warning = FString::Printf(TEXT("Shares a key with \"%s\""), *Clash);
		}
		Heading(TEXT("The pad's buttons stay as they are; geometry mode has its own keys."));
		break;
	}
	case EPage::PadCheck:
	{
		// Torque Shed's Controller check: the numbers should follow the sticks; resting off centre, Recalibrate.
		auto Info = [&Add](const FString& Words) { Add(Words, FString(), FString(), nullptr); };
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
		break;
	}
	case EPage::Character:
	{
		Heading(TEXT("YOU"));
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
		// [profile] (build 155 feedback) The Profile page is this tab: you above, then your avatar and
		// your helmet, each shown big beside the rows (the shop's mouse controls work the picture).
	}
		[[fallthrough]];
	case EPage::Profile:   // [profile] Torque Shed's profile panel, with the MetaHuman's face
	{
		FTorqueProfile& Me = FTorqueProfile::Mine();
		const int32 NumAv = FTorqueAvatar::Count();
		// A slider (-1 .. 1, the middle the MetaHuman's own): dragged with the mouse, Left / Right
		// steps it. It changes your picture as it moves; letting go saves it and tells the others.
		auto FaceSlide = [this, &R](const FString& Label, float& V, const FString& Value, const FString& Hint, bool bFace) {
			float* Ptr = &V;
			FRow Row;
			Row.Label = Label;
			Row.Value = Value;
			Row.Hint = Hint;
			Row.Step = [this, Ptr](int32 Dir) { *Ptr = StepF(*Ptr, Dir, 0.1f, -1.0f, 1.0f); ProfileChanged(); };
			Row.Frac = (FMath::Clamp(V, -1.0f, 1.0f) + 1.0f) * 0.5f;
			Row.SetFrac = [Ptr](float F) { *Ptr = FMath::Clamp(F * 2.0f - 1.0f, -1.0f, 1.0f); };
			Row.bFace = bFace;
			Row.bCentred = true;
			Row.Commit = [this]() { ProfileChanged(); };
			R.Add(MoveTemp(Row));
		};
		if (NumAv == 0)
		{
			Heading(TEXT("No MetaHuman avatars in this build yet: in co-op you show as the shape body, in your colour."));
		}
		else
		{
			const ATorqueProfilePreview* Pv = Preview.Get();
			const FString Own = FTorqueAvatar::NameOf(Me.AvatarFor(S.Colour));
			Heading(TEXT("AVATAR"));
			Add(TEXT("Who"), Me.Avatar < 0 ? FString::Printf(TEXT("%s (goes with my colour)"), *Own) : Own,
				TEXT("Which MetaHuman you are, in the shed and on the bike: everyone in co-op sees you as them"),
				[this, &Me, NumAv]() { Me.Avatar = Wrap(Me.Avatar + 2, NumAv + 1) - 1; ProfileChanged(); },
				[this, &Me, NumAv](int32 D) { Me.Avatar = Wrap(Me.Avatar + 1 + D, NumAv + 1) - 1; ProfileChanged(); });
			FaceSlide(TEXT("Height"), Me.Height, Pv && Pv->Height() > 0.0 ? FString::Printf(TEXT("%.2f m"), Pv->Height() / 100.0) : FString::Printf(TEXT("%+.0f%%"), Me.Height * 6.0f),
				TEXT("Shorter or taller than the MetaHuman's own height, by up to 6%"), false);
			Heading(TEXT("FACE"));
			for (int32 F = 0; F < FTorqueProfile::NumFace; ++F)
				FaceSlide(FTorqueProfile::Sliders[F].Name, Me.Face[F], FString::Printf(TEXT("%+.1f"), Me.Face[F]), FTorqueProfile::Sliders[F].Hint, true);
			Add(TEXT("Reset the face"), FString(), TEXT("Back to the MetaHuman's own face"), [this, &Me]() { Me.ResetFace(); ProfileChanged(); });
			R.Last().bFace = true;
		}
		// [helmet plan, stage 1] Your helmet: which one (one so far; the list is ready for more), its
		// paint (as it came, a named colour, or any colour from the picker beside the picture) and its
		// finish. Everyone in co-op sees it on your helmet.
		{
			Heading(TEXT("HELMET"));
			static const TCHAR* Helmets[] = {TEXT("Racing full-face")};   // more shells come with the helmet plan's stage 4
			Add(TEXT("Helmet"), Helmets[0], TEXT("Which helmet you wear (more to come)"), nullptr, [](int32) {});
			R.Last().bHelmet = true;
			const int32 Swatch = Me.HelmetSwatch();
			const FString PaintName = Swatch == -2 ? FString(TEXT("As it came")) : Swatch == -1 ? FString(TEXT("Your own colour"))
				: FString(FTorqueProfile::HelmetSwatches[Swatch].Name);
			// Steps through: as it came, then each named colour (from your own colour: the first or the last).
			auto StepPaint = [this, &Me](int32 D) {
				const int32 Now = Me.HelmetSwatch();
				const int32 From = Now == -2 ? -1 : Now == -1 ? (D > 0 ? -1 : 0) : Now;
				Me.SetHelmetSwatch(Wrap(From + 1 + D, (int32)FTorqueProfile::NumHelmetSwatch + 1) - 1);
				ProfileChanged();
			};
			Add(TEXT("Helmet colour"), PaintName, TEXT("As the helmet came, a named colour, or any colour: pick it in the square and the strip beside the picture"),
				[StepPaint]() { StepPaint(1); }, StepPaint);
			R.Last().bHelmet = true;
			Add(TEXT("Finish"), Me.bHelmetPainted ? FString(FTorqueProfile::HelmetFinishName(Me.HelmetFinish)) : FString(TEXT("As it came")),
				TEXT("Gloss, matt, metallic flake, or pearl that shifts colour at the edges (on a colour of your own)"),
				[this, &Me]() { if (!Me.bHelmetPainted) return; Me.HelmetFinish = Wrap(Me.HelmetFinish + 1, (int32)FTorqueProfile::NumHelmetFinish); ProfileChanged(); },
				[this, &Me](int32 D) { if (!Me.bHelmetPainted) return; Me.HelmetFinish = Wrap(Me.HelmetFinish + D, (int32)FTorqueProfile::NumHelmetFinish); ProfileChanged(); });
			R.Last().bHelmet = true;
		}
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
	SyncRideJourney();
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
	// The Route planner reads its own keys and mouse; Esc there goes back to the pause menu.
	if (Page == EPage::Route)
	{
		if (!Planner || !Planner->Tick(this)) Back();
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
	// Headings and notes aren't picked: the next row that does something.
	auto NextWorking = [&List](int32 From, int32 Dir) {
		for (int32 N = 0, I = From; N < List.Num(); ++N, I = Wrap(I + Dir, List.Num()))
			if (List[I].Works()) return I;
		return From;
	};
	if (!List[Selected].Works() && DragRow < 0) Selected = NextWorking(Selected, 1);
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
	// [ride date] A pull-down open: it has the keys and the mouse.
	if (DropInput(List, MoveX, MoveY, Just(EKeys::Enter) || Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Bottom) || (Hit & ESonyButton::Cross),
			Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (Hit & ESonyButton::Circle) || Just(EKeys::BackSpace)))
		return;
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
		if (bMoved && DragRow < 0 && Over && Over->Kind != FTorqueMenuHits::Tab && Over->Kind != FTorqueMenuHits::Badge && Over->Kind != FTorqueMenuHits::BackKey &&
			List.IsValidIndex(Over->Index) && List[Over->Index].Works() && Over->Index != Selected)
		{
			if (bTyping) Changed();   // leaving the name keeps it
			Selected = Over->Index;
		}
		if (Just(EKeys::MouseScrollUp)) MoveY = -1;
		if (Just(EKeys::MouseScrollDown)) MoveY = 1;
		// [profile] The big picture on the Profile page works as the shop's preview: drag to turn it,
		// the wheel zooms, right-click stops or starts the turning; and the helmet's colour picker.
		if (IsProfilePage() && DragRow < 0 && Mouse.X >= 0.0)
		{
			if (HelmetPickerMouse(Mouse, Just(EKeys::LeftMouseButton))) return;
			if (ATorqueProfilePreview* Pv = Preview.Get())
			{
				Pv->Hold(Mouse, IsInputKeyDown(EKeys::LeftMouseButton));
				if (Pv->IsDragging()) return;
				if (MoveY != 0 && Pv->Wheel(Mouse, MoveY)) MoveY = 0;
				if (Just(EKeys::LeftMouseButton) && Pv->Press(Mouse)) return;
				if (Just(EKeys::RightMouseButton) && Pv->RightPress(Mouse)) return;
			}
		}
		// [profile] A slider dragged: the value follows the pointer along its track until let go.
		if (DragRow >= 0)
		{
			if (IsInputKeyDown(EKeys::LeftMouseButton) && List.IsValidIndex(DragRow) && List[DragRow].SetFrac && Mouse.X >= 0.0)
			{
				List[DragRow].SetFrac(FMath::Clamp((float)((Mouse.X - DragX0) / FMath::Max(DragX1 - DragX0, 1.0)), 0.0f, 1.0f));
				return;
			}
			// Let go: saved and put on the game (a profile slider tells the others).
			if (List.IsValidIndex(DragRow) && List[DragRow].Commit) List[DragRow].Commit();
			DragRow = -1;
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
				if (IsProfilePage()) Back();
				else Open(EPage::Character);   // Settings > Profile
				return;
			}
			if (Over->Kind == FTorqueMenuHits::BackKey)
			{
				if (bTyping) Changed();
				Back();
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
				if (!Row.Caps.IsEmpty() && (Over->Kind == FTorqueMenuHits::Less || Over->Kind == FTorqueMenuHits::More) && Row.Choose)
				{
					KeySlot = Over->Kind == FTorqueMenuHits::Less ? 0 : 1;   // a key binding's first or second key: listen for it
					Row.Choose();
				}
				else if (Over->Kind == FTorqueMenuHits::Cell && Row.Cells.IsValidIndex(Over->Sub) && !Row.bDisabled)
				{
					// [ride date] A box along the row: its pull-down opens, or the button's pressed.
					if (Row.Cells[Over->Sub].IsDrop()) OpenDrop(Row, Selected, Over->Sub);
					else if (Row.Cells[Over->Sub].Press) Row.Cells[Over->Sub].Press();
				}
				else if (Row.Frac >= 0.0f) {}   // a slider: only its track (above) moves it
				else if (Over->Kind == FTorqueMenuHits::Less && Row.Step) Row.Step(-1);
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
	if (MoveY != 0) Selected = NextWorking(Wrap(Selected + MoveY, List.Num()), MoveY);
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

namespace
{
// The menu's frame, laid out at 1920 x 1080 and scaled to the screen, centred.
struct FMenuLayout
{
	float S = 1.0f, OX = 0.0f, OY = 0.0f;
	float X(float V) const { return OX + V * S; }
	float Y(float V) const { return OY + V * S; }
	float W(float V) const { return V * S; }
};
FMenuLayout MenuLayout(const UCanvas* Canvas)
{
	FMenuLayout L;
	L.S = TorqueUi::Scale(Canvas);
	L.OX = 0.5f * (Canvas->ClipX - 1920.0f * L.S);
	L.OY = 0.5f * (Canvas->ClipY - 1080.0f * L.S);
	return L;
}
// The panel's parts (1080p pixels): the list on the left, the description on the right.
constexpr float PanelX0 = 150.0f, PanelX1 = 1770.0f, PanelY0 = 56.0f, PanelY1 = 1024.0f;
constexpr float ListX0 = 190.0f, ListX1 = 1150.0f, DescX0 = 1190.0f, DescX1 = 1730.0f, BodyY1 = 936.0f;
bool IsHeading(const FString& Words)
{
	return Words == Words.ToUpper() && Words != Words.ToLower();
}
} // namespace

void ATorquePlayerController::DrawMenu(AHUD* Hud, UCanvas* Canvas)
{
	namespace Ui = TorqueUi;
	Hits.Reset();
	if (Page == EPage::None || !Hud || !Canvas) return;
	if (Page == EPage::Route)
	{
		if (Planner) Planner->Draw(Canvas);
		return;
	}
	const FMenuLayout L = MenuLayout(Canvas);
	const float S = L.S;
	const float Hair = FMath::Max(1.0f, FMath::RoundToFloat(S));
	const FVector2f Mouse((float)LastMouse.X, (float)LastMouse.Y);
	auto Over = [&Mouse](float X0, float Y0, float X1, float Y1) { return Mouse.X >= X0 && Mouse.X < X1 && Mouse.Y >= Y0 && Mouse.Y < Y1; };

	// The world dimmed, and the panel.
	Ui::Backdrop(Canvas, 0.62f);
	Ui::Rect(Canvas, L.X(PanelX0), L.Y(PanelY0), L.W(PanelX1 - PanelX0), L.W(PanelY1 - PanelY0), Ui::Panel);
	Ui::Rect(Canvas, L.X(PanelX0), L.Y(PanelY0), L.W(PanelX1 - PanelX0), Hair, Ui::Line);
	Ui::Rect(Canvas, L.X(PanelX0), L.Y(PanelY0), L.W(160), L.W(3), Ui::Accent);

	// The header: where you are, and the page's name.
	const bool bSub = Page == EPage::Keys || Page == EPage::PadCheck;
	Ui::Text(Canvas, bSub ? TEXT("SETTINGS  /  CONTROLLER") : TEXT("TORQUE SHED"), L.X(ListX0), L.Y(86), L.W(15), Ui::Accent, true);
	Ui::Text(Canvas, Title(), L.X(ListX0), L.Y(108), L.W(42), Ui::Ink, true);

	// Settings' tabs, between their Q and E keys.
	float BodyY0 = 196.0f;
	if (TabIndex() >= 0)
	{
		const float TY = L.Y(186);
		float TX = L.X(ListX0);
		TX += Ui::KeyHint(Canvas, TEXT("Q"), FString(), TX, TY - L.W(1), S) + L.W(16);
		for (int32 T = 0; T < 5; ++T)
		{
			const FString Name = FString(TabNames[T]).ToUpper();
			const bool bOn = T == TabIndex();
			const float TW = (float)Ui::Measure(Name, L.W(20), true).X;
			const bool bHot = !bOn && Over(TX - L.W(14), TY - L.W(12), TX + TW + L.W(14), TY + L.W(40));
			Ui::Text(Canvas, Name, TX, TY, L.W(20), bOn ? Ui::Ink : (bHot ? FLinearColor(0.85f, 0.85f, 0.83f, 1.0f) : Ui::Dim), true);
			if (bOn) Ui::Rect(Canvas, TX, TY + L.W(34), TW, L.W(3), Ui::Accent);
			Hits.Add(TX - L.W(14), TY - L.W(12), TX + TW + L.W(14), TY + L.W(40), T, FTorqueMenuHits::Tab);
			TX += TW + L.W(40);
		}
		Ui::KeyHint(Canvas, TEXT("E"), FString(), TX - L.W(24), TY - L.W(1), S);
		BodyY0 = 244.0f;
	}
	Ui::Rect(Canvas, L.X(ListX0), L.Y(BodyY0 - 18.0f), L.W(DescX1 - ListX0), Hair, Ui::Line);

	// The rows. A page longer than the list scrolls, keeping the one picked in view.
	const TArray<FRow> List = Rows();
	const float LX0 = L.X(ListX0), LX1 = L.X(ListX1), Top = L.Y(BodyY0), Bottom = L.Y(BodyY1);
	const float VX0 = L.X(ListX0 + 520.0f), VX1 = LX1 - L.W(20);
	const float NotePx = L.W(18), NoteW = LX1 - LX0 - L.W(40);
	auto RowH = [&](int32 I) {
		const FRow& Row = List[I];
		if (Row.Works() || !Row.Value.IsEmpty()) return L.W(50);
		if (IsHeading(Row.Label)) return L.W(I == 0 ? 36 : 50);
		return Ui::WrapLines(Row.Label, NotePx, NoteW).Num() * NotePx * 1.35f + L.W(10);
	};
	if (List.Num() > 0)
	{
		const int32 Sel = FMath::Clamp(Selected, 0, List.Num() - 1);
		FirstRow = FMath::Clamp(FirstRow, 0, List.Num() - 1);
		if (Sel < FirstRow) FirstRow = Sel;
		// (the heading above the one picked comes with it)
		if (Sel == FirstRow && Sel > 0 && IsHeading(List[Sel - 1].Label) && !List[Sel - 1].Works()) FirstRow = Sel - 1;
		auto Span = [&](int32 From, int32 To) {
			float H = 0.0f;
			for (int32 I = From; I <= To; ++I) H += RowH(I);
			return H;
		};
		while (FirstRow < Sel && Span(FirstRow, Sel) > Bottom - Top) ++FirstRow;
		while (FirstRow > 0 && Span(FirstRow - 1, List.Num() - 1) <= Bottom - Top) --FirstRow;
	}
	float Y = Top;
	int32 LastShown = List.Num() - 1;
	bool bDropShown = false;   // [ride date] the open pull-down's box on screen: under it goes its list
	float DropX0 = 0.0f, DropX1 = 0.0f, DropY1 = 0.0f;
	for (int32 I = FirstRow; I < List.Num(); ++I)
	{
		const FRow& Row = List[I];
		const float H = RowH(I);
		if (Y + H > Bottom + 1.0f && I > FirstRow)
		{
			LastShown = I - 1;
			break;
		}
		const bool bSel = (I == Selected && Row.Works()) || I == DragRow;
		const float LabelPx = L.W(21), ValuePx = L.W(19);
		const float LabelY = Y + 0.5f * (H - LabelPx * 1.2f), ValueY = Y + 0.5f * (H - ValuePx * 1.2f), Mid = Y + 0.5f * H;
		if (!Row.Works() && Row.Value.IsEmpty())
		{
			if (IsHeading(Row.Label))
			{
				// A section: small amber capitals and a line after them.
				const float HY = Y + H - L.W(26);
				const float TW = Ui::Text(Canvas, Row.Label, LX0 + L.W(4), HY, L.W(15), Ui::Accent, true);
				Ui::Rect(Canvas, LX0 + L.W(4) + TW + L.W(14), HY + L.W(9), LX1 - LX0 - TW - L.W(22), Hair, Ui::Line);
			}
			else
			{
				float NY = Y + L.W(4);
				for (const FString& One : Ui::WrapLines(Row.Label, NotePx, NoteW))
				{
					Ui::Text(Canvas, One, LX0 + L.W(20), NY, NotePx, Ui::Dim);
					NY += NotePx * 1.35f;
				}
			}
			Y += H;
			continue;
		}
		// The row picked: a lighter band with an amber edge.
		if (bSel)
		{
			Ui::Rect(Canvas, LX0, Y + L.W(2), LX1 - LX0, H - L.W(4), FLinearColor(1, 1, 1, 0.075f));
			Ui::Rect(Canvas, LX0, Y + L.W(2), L.W(4), H - L.W(4), Ui::Accent);
		}
		else if (Row.Works() && Over(LX0, Y, LX1, Y + H))
			Ui::Rect(Canvas, LX0, Y + L.W(2), LX1 - LX0, H - L.W(4), FLinearColor(1, 1, 1, 0.035f));
		const FLinearColor LabelColour = Row.bDisabled || !Row.Works() ? Ui::Faint : (!Row.Warning.IsEmpty() ? Ui::Warn : (bSel ? Ui::Ink : FLinearColor(0.84f, 0.85f, 0.85f, 1.0f)));
		const bool bWide = Row.Value.IsEmpty() && !Row.Step && Row.Caps.IsEmpty();   // a button: its words can run on
		Ui::TextFit(Canvas, Row.Label, LX0 + L.W(24), LabelY, LabelPx, (bWide ? VX1 - L.W(40) : VX0 - L.W(16)) - (LX0 + L.W(24)), LabelColour);
		const FLinearColor ValueColour = bSel ? Ui::Ink : Ui::Dim;
		if (!Row.Cells.IsEmpty())
		{
			// [ride date] Boxes along the row: pull-downs (their value and a down arrow), a button last.
			static const float Weights[] = {0.17f, 0.36f, 0.22f, 0.25f};
			const float Gap = L.W(10), BH = L.W(36), BY = Mid - 0.5f * BH;
			float Sum = 0.0f;
			for (int32 C = 0; C < Row.Cells.Num(); ++C) Sum += Weights[FMath::Min(C, 3)];
			const float Room = VX1 - VX0 - Gap * (Row.Cells.Num() - 1);
			float CX = VX0;
			for (int32 C = 0; C < Row.Cells.Num(); ++C)
			{
				const FRow::FCell& Cell = Row.Cells[C];
				const float CW = Room * Weights[FMath::Min(C, 3)] / FMath::Max(Sum, 0.01f);
				const bool bOpen = DropRow == I && DropCell == C;
				const bool bHot = !Row.bDisabled && Over(CX, BY, CX + CW, BY + BH);
				if (Cell.IsDrop() || Row.bDisabled)
				{
					Ui::Rect(Canvas, CX, BY, CW, BH, FLinearColor(0, 0, 0, bHot || bOpen ? 0.5f : 0.35f));
					Ui::Frame(Canvas, CX, BY, CW, BH, bOpen ? Ui::Accent : FLinearColor(1, 1, 1, bHot ? 0.35f : 0.18f), bOpen ? L.W(2) : Hair);
					Ui::TextFit(Canvas, Cell.Text, CX + L.W(10), ValueY, ValuePx, CW - L.W(40), Row.bDisabled ? Ui::Faint : Ui::Ink);
					if (Cell.IsDrop())
					{
						// A small down-pointing triangle, drawn (no text caret: beenster, build 154).
						const FLinearColor Tri = bSel || bHot || bOpen ? Ui::Accent : Ui::Faint;
						const float TW = L.W(12), TX = CX + CW - L.W(14) - TW, TY = Mid - L.W(3);
						for (int32 Line = 0; Line < 6; ++Line)
						{
							const float Inset = TW * Line / 12.0f;
							Ui::Rect(Canvas, TX + Inset, TY + Line * L.W(1), TW - 2.0f * Inset, L.W(1), Tri);
						}
					}
				}
				else
				{
					// A button.
					Ui::Rect(Canvas, CX, BY, CW, BH, FLinearColor(1, 1, 1, bHot ? 0.16f : 0.08f));
					Ui::Frame(Canvas, CX, BY, CW, BH, bHot ? Ui::Accent : FLinearColor(1, 1, 1, 0.2f), Hair);
					Ui::TextFit(Canvas, Cell.Text, CX + 0.5f * CW, ValueY, ValuePx, CW - L.W(16), Ui::Ink, true, Ui::EAlign::Centre);
				}
				if (bOpen)
				{
					bDropShown = true;
					DropX0 = CX;
					DropX1 = CX + CW;
					DropY1 = BY + BH;
				}
				if (!Row.bDisabled) Hits.Add(CX, BY, CX + CW, BY + BH, I, FTorqueMenuHits::Cell, C);
				CX += CW + Gap;
			}
		}
		else if (!Row.Caps.IsEmpty())
		{
			// Key bindings: each slot a keycap, the slot that Enter changes outlined when the row's picked.
			for (int32 Slot = 0; Slot < Row.Caps.Num(); ++Slot)
			{
				const float CX = VX0 + Slot * L.W(212), CW = L.W(196), CH = L.W(34), CY = Mid - 0.5f * CH;
				const bool bCapOn = bSel && Slot == Row.CapOn;
				const bool bCapHot = Over(CX, Y, CX + CW, Y + H);
				Ui::Rect(Canvas, CX, CY, CW, CH, FLinearColor(1, 1, 1, bCapHot ? 0.12f : 0.06f));
				Ui::Frame(Canvas, CX, CY, CW, CH, bCapOn ? Ui::Accent : FLinearColor(1, 1, 1, 0.16f), bCapOn ? L.W(2) : Hair);
				const FString Name = Row.Caps[Slot].IsEmpty() ? FString(TEXT("-")) : Row.Caps[Slot];
				Ui::TextFit(Canvas, Name, CX + 0.5f * CW, Mid - 0.5f * L.W(18) * 1.2f, L.W(18), CW - L.W(16), Row.Caps[Slot].IsEmpty() ? Ui::Faint : Ui::Ink, true,
					Ui::EAlign::Centre);
				Hits.Add(CX, Y, CX + CW, Y + H, I, Slot == 0 ? FTorqueMenuHits::Less : FTorqueMenuHits::More);
			}
		}
		else if (Row.Frac >= 0.0f)
		{
			// A slider: the track, and the value at its end. The track is what the mouse drags.
			const float TX0 = VX0 + L.W(4), TX1 = VX1 - L.W(100);
			Ui::Slider(Canvas, TX0, Mid, TX1 - TX0, Row.Frac, Row.bCentred, bSel, S);
			Ui::TextFit(Canvas, Row.Value, VX1, ValueY, ValuePx, L.W(88), ValueColour, false, Ui::EAlign::Right);
			Hits.Add(TX0, Y, TX1, Y + H, I, FTorqueMenuHits::Slider);
		}
		else if (Row.bToggle)
		{
			const float SW = L.W(52), SH = L.W(26);
			Ui::Switch(Canvas, VX1 - SW, Mid - 0.5f * SH, SW, SH, Row.bOn, bSel);
			Ui::Text(Canvas, Row.bOn ? TEXT("On") : TEXT("Off"), VX1 - SW - L.W(14), ValueY, ValuePx, ValueColour, false, Ui::EAlign::Right);
		}
		else if (Row.Step && !Row.bTyping)
		{
			// A choice of several: the value between arrows (each a button for the mouse).
			const float APx = L.W(28), AY = Mid - 0.5f * APx * 1.2f - L.W(2);
			const bool bLessHot = Over(VX0 - L.W(12), Y, VX0 + L.W(40), Y + H), bMoreHot = Over(VX1 - L.W(40), Y, VX1 + L.W(12), Y + H);
			const FLinearColor Arrow = bSel ? Ui::Accent : Ui::Faint;
			Ui::Text(Canvas, TEXT("‹"), VX0 + L.W(6), AY, APx, bLessHot ? Ui::Ink : Arrow, true);
			Ui::Text(Canvas, TEXT("›"), VX1 - L.W(6), AY, APx, bMoreHot ? Ui::Ink : Arrow, true, Ui::EAlign::Right);
			Ui::TextFit(Canvas, Row.Value, 0.5f * (VX0 + VX1), ValueY, ValuePx, VX1 - VX0 - L.W(80), ValueColour, false, Ui::EAlign::Centre);
			Hits.Add(VX0 - L.W(12), Y, VX0 + L.W(40), Y + H, I, FTorqueMenuHits::Less);
			Hits.Add(VX1 - L.W(40), Y, VX1 + L.W(12), Y + H, I, FTorqueMenuHits::More);
		}
		else if (Row.bTyping)
		{
			// The name: a text box.
			const float BY = Mid - L.W(19), BH = L.W(38);
			Ui::Rect(Canvas, VX0, BY, VX1 - VX0, BH, FLinearColor(0, 0, 0, 0.35f));
			Ui::Frame(Canvas, VX0, BY, VX1 - VX0, BH, bSel ? Ui::Accent : FLinearColor(1, 1, 1, 0.18f), Hair);
			Ui::TextFit(Canvas, Row.Value, VX0 + L.W(12), ValueY, ValuePx, VX1 - VX0 - L.W(24), Ui::Ink);
		}
		else if (!Row.Value.IsEmpty())
		{
			// Words only (a key being listened for, or a fixed value).
			const bool bListen = Row.Works();
			Ui::TextFit(Canvas, Row.Value, bListen ? VX0 : VX1, ValueY, ValuePx, VX1 - VX0, bListen ? Ui::Accent : Ui::Faint, false,
				bListen ? Ui::EAlign::Left : Ui::EAlign::Right);
		}
		else
			Ui::Text(Canvas, TEXT("›"), VX1, Mid - 0.5f * L.W(28) * 1.2f - L.W(2), L.W(28), bSel ? Ui::Accent : Ui::Faint, true, Ui::EAlign::Right);
		if (Row.Works()) Hits.Add(LX0, Y, LX1, Y + H, I, FTorqueMenuHits::Row);
		Y += H;
	}
	// A scroll bar when the page doesn't fit.
	if (FirstRow > 0 || LastShown < List.Num() - 1)
	{
		const float BX = LX1 + L.W(10), BW = L.W(4), BH = Bottom - Top;
		Ui::Rect(Canvas, BX, Top, BW, BH, FLinearColor(1, 1, 1, 0.06f));
		const float N = (float)FMath::Max(List.Num(), 1);
		const float F0 = FirstRow / N, F1 = (LastShown + 1) / N;
		Ui::Rect(Canvas, BX, Top + BH * F0, BW, FMath::Max(BH * (F1 - F0), L.W(24)), FLinearColor(1, 1, 1, 0.35f));
	}

	// The description: the row picked, what it does; the pause menu's session; the profile's picture.
	const float DX0 = L.X(DescX0), DX1 = L.X(DescX1), DW = DX1 - DX0, In = L.W(28);
	Ui::Rect(Canvas, DX0, Top, DW, Bottom - Top, FLinearColor(1, 1, 1, 0.03f));
	if (IsProfilePage())
	{
		// [profile] You (or your helmet), big, beside the rows; on a helmet row the colour picker under it.
		ATorqueProfilePreview* Pv = Preview.Get();
		const bool bPicker = List.IsValidIndex(Selected) && List[Selected].bHelmet;
		const float PickH = bPicker ? L.W(230) : 0.0f;
		const float HintH = L.W(26);
		const float PH = Bottom - Top - L.W(32) - PickH - HintH, PW = FMath::Min(DW - L.W(32), PH * 0.75f);
		const float PX = 0.5f * (DX0 + DX1 - PW), PY = Top + L.W(16);
		if (Pv && Pv->HasAvatar())
		{
			Pv->DrawWorked(Canvas, PX, PY, PW, PH);
			Ui::Text(Canvas, TEXT("Drag to turn    Wheel to zoom    Right-click to stop or start"), 0.5f * (DX0 + DX1), PY + PH + L.W(6), L.W(14), Ui::Faint, false,
				Ui::EAlign::Centre);
		}
		else
			Ui::TextWrap(Canvas, TEXT("Your MetaHuman shows here once the avatars are in this build."), DX0 + In, Top + In, L.W(19), DW - 2.0f * In, Ui::Dim);
		if (bPicker) DrawHelmetPicker(Canvas, DX0 + In, Bottom - PickH - L.W(8), DW - 2.0f * In, PickH, L.S);
		else
		{
			PickSquare = PickHue = FBox2D(ForceInit);
			PickSwatches.Reset();
		}
	}
	else
	{
		float DY = Top + In;
		if (Page == EPage::Pause)
		{
			// As Torque Shed's Esc menu: hosting, exactly what to tell friends; who's here.
			Ui::Text(Canvas, TEXT("SESSION"), DX0 + In, DY, L.W(15), Ui::Accent, true);
			DY += L.W(32);
			const UTorqueSession* Session = UTorqueSession::Get(this);
			TArray<FString> Says;
			if (GetNetMode() == NM_ListenServer && Session)
			{
				Says.Add(FString::Printf(TEXT("Hosting on port %d."), UTorqueSession::GamePort()));
				Says.Append(Session->JoinInstructions());
			}
			else if (GetNetMode() == NM_Client)
				Says.Add(TEXT("Connected to a co-op shed."));
			else
				Says.Add(TEXT("Working solo."));
			// The weather where you are (the live grid's, made local), to compare with outside.
			if (const ATorqueGameState* WGs = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr)
				Says.Add(TEXT("Weather: ") + WGs->WeatherText(FTorqueSettings::Get().Units == 1));
			if (GetNetMode() != NM_Standalone)
				if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
				{
					TArray<FString> Names;
					for (const APlayerState* PS : GS->PlayerArray)
						if (PS) Names.Add(PS->GetPlayerName());
					Says.Add(TEXT("In the shed: ") + FString::Join(Names, TEXT(", ")));
				}
			for (const FString& Said : Says) DY += Ui::TextWrap(Canvas, Said, DX0 + In, DY, L.W(19), DW - 2.0f * In, Ui::Ink) + L.W(6);
			DY += L.W(30);
		}
		if (List.IsValidIndex(Selected) && List[Selected].Works() && (Page != EPage::Pause || !List[Selected].Hint.IsEmpty()))
		{
			const FRow& Row = List[Selected];
			Ui::TextFit(Canvas, Row.Label, DX0 + In, DY, L.W(26), DW - 2.0f * In, Ui::Ink, true);
			DY += L.W(46);
			Ui::Rect(Canvas, DX0 + In, DY, L.W(44), L.W(3), Ui::Accent);
			DY += L.W(22);
			if (!Row.Hint.IsEmpty()) DY += Ui::TextWrap(Canvas, Row.Hint + (Row.Hint.EndsWith(TEXT(".")) || Row.Hint.EndsWith(TEXT(")")) ? TEXT("") : TEXT(".")),
								 DX0 + In, DY, L.W(19), DW - 2.0f * In, Ui::Dim) + L.W(10);
			if (!Row.Warning.IsEmpty()) Ui::TextWrap(Canvas, Row.Warning + TEXT(": both work, but one key does two things."), DX0 + In, DY, L.W(19), DW - 2.0f * In, Ui::Warn);
		}
	}

	// The footer: the keys, and Esc as a button for the mouse.
	Ui::Rect(Canvas, L.X(ListX0), L.Y(952), L.W(DescX1 - ListX0), Hair, Ui::Line);
	const float FY = L.Y(970);
	float FX = L.X(ListX0);
	FX += Ui::KeyHint(Canvas, TEXT("Enter"), TEXT("Select"), FX, FY, S) + L.W(30);
	if (List.ContainsByPredicate([](const FRow& Row) { return (bool)Row.Step; }))
		FX += Ui::KeyHint(Canvas, TEXT("← →"), TEXT("Change"), FX, FY, S) + L.W(30);
	if (TabIndex() >= 0) FX += Ui::KeyHint(Canvas, TEXT("Q  E"), TEXT("Tabs"), FX, FY, S) + L.W(30);
	if (Page == EPage::Keys) FX += Ui::KeyHint(Canvas, TEXT("Del"), TEXT("Clear while listening"), FX, FY, S) + L.W(30);
	{
		const float BW = Ui::KeyHint(Canvas, TEXT("Esc"), Page == EPage::Pause ? TEXT("Resume") : TEXT("Back"), FX, FY, S);
		Hits.Add(FX - L.W(6), FY - L.W(8), FX + BW + L.W(6), FY + L.W(34), 0, FTorqueMenuHits::BackKey);
	}
	Ui::Text(Canvas, TEXT("Pad:  Cross / A select    Circle / B back    L1 R1 / LB RB tabs"), L.X(DescX1), FY + L.W(4), L.W(16), Ui::Faint, false, Ui::EAlign::Right);
	// [ride date] The open pull-down's list, over everything under its box (opening upwards when it
	// wouldn't fit below), DropShown options at a time; the wheel scrolls it.
	if (bDropShown && List.IsValidIndex(DropRow) && List[DropRow].Cells.IsValidIndex(DropCell))
	{
		const FRow::FCell& Cell = List[DropRow].Cells[DropCell];
		const int32 N = Cell.Options.Num();
		const int32 Shown = FMath::Min(N, (int32)DropShown);
		DropFirst = FMath::Clamp(DropFirst, 0, FMath::Max(N - Shown, 0));
		const float OH = L.W(34), Px = L.W(18), PadY = L.W(4);
		const float PW = FMath::Max(DropX1 - DropX0, L.W(120)), PH = Shown * OH + 2.0f * PadY;
		float PY = DropY1 + L.W(2);
		if (PY + PH > L.Y(950)) PY = DropY1 - L.W(38) - PH - L.W(2);
		Ui::Rect(Canvas, DropX0 + L.W(3), PY + L.W(4), PW, PH, FLinearColor(0, 0, 0, 0.45f));   // a shadow
		Ui::Rect(Canvas, DropX0, PY, PW, PH, FLinearColor(0.075f, 0.08f, 0.085f, 0.98f));
		Ui::Frame(Canvas, DropX0, PY, PW, PH, Ui::Accent, Hair);
		Hits.Add(DropX0, PY, DropX0 + PW, PY + PH, DropRow, FTorqueMenuHits::DropItem, -1);
		for (int32 K = 0; K < Shown; ++K)
		{
			const int32 Opt = DropFirst + K;
			const float OY = PY + PadY + K * OH;
			const bool bHot = Opt == DropHot, bPicked = Opt == Cell.Pick;
			if (bHot) Ui::Rect(Canvas, DropX0 + Hair, OY, PW - 2.0f * Hair, OH, FLinearColor(1, 1, 1, 0.1f));
			if (bPicked) Ui::Rect(Canvas, DropX0 + Hair, OY + L.W(6), L.W(3), OH - L.W(12), Ui::Accent);
			Ui::TextFit(Canvas, Cell.Options[Opt], DropX0 + L.W(14), OY + 0.5f * (OH - Px * 1.2f), Px, PW - L.W(28), bPicked ? Ui::Accent : Ui::Ink, bPicked);
			Hits.Add(DropX0, OY, DropX0 + PW, OY + OH, DropRow, FTorqueMenuHits::DropItem, Opt);
		}
		if (N > Shown)
		{
			// Where the list is, at its right edge.
			const float BX = DropX0 + PW - L.W(6), BH2 = PH - 2.0f * PadY;
			Ui::Rect(Canvas, BX, PY + PadY, L.W(3), BH2, FLinearColor(1, 1, 1, 0.08f));
			Ui::Rect(Canvas, BX, PY + PadY + BH2 * DropFirst / N, L.W(3), FMath::Max(BH2 * Shown / N, L.W(16)), FLinearColor(1, 1, 1, 0.4f));
		}
	}
	DrawProfile(Hud, Canvas);   // [profile] the badge
}

namespace TorqueProfilePick
{
// A W x H patch shaded smoothly by ColourAt(U, V) (U across, V down, 0..1), as the shop's colour
// picker draws its square and strip: a fine mesh of triangles, each corner its own colour.
template <typename FColourAt>
void Gradient(UCanvas* Canvas, float X, float Y, float W, float H, int32 Nx, int32 Ny, FColourAt&& ColourAt)
{
	if (!Canvas || W <= 0.0f || H <= 0.0f) return;
	TArray<FLinearColor> Corner;
	Corner.SetNum((Nx + 1) * (Ny + 1));
	for (int32 J = 0; J <= Ny; ++J)
		for (int32 I = 0; I <= Nx; ++I) Corner[J * (Nx + 1) + I] = ColourAt((float)I / Nx, (float)J / Ny);
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Nx * Ny * 2);
	for (int32 J = 0; J < Ny; ++J)
		for (int32 I = 0; I < Nx; ++I)
		{
			const FVector2D P00(X + W * I / Nx, Y + H * J / Ny), P10(X + W * (I + 1) / Nx, Y + H * J / Ny);
			const FVector2D P01(X + W * I / Nx, Y + H * (J + 1) / Ny), P11(X + W * (I + 1) / Nx, Y + H * (J + 1) / Ny);
			const FLinearColor& C00 = Corner[J * (Nx + 1) + I];
			const FLinearColor& C10 = Corner[J * (Nx + 1) + I + 1];
			const FLinearColor& C01 = Corner[(J + 1) * (Nx + 1) + I];
			const FLinearColor& C11 = Corner[(J + 1) * (Nx + 1) + I + 1];
			FCanvasUVTri A;
			A.V0_Pos = P00; A.V0_Color = C00;
			A.V1_Pos = P10; A.V1_Color = C10;
			A.V2_Pos = P11; A.V2_Color = C11;
			Tris.Add(A);
			FCanvasUVTri B;
			B.V0_Pos = P00; B.V0_Color = C00;
			B.V1_Pos = P11; B.V1_Color = C11;
			B.V2_Pos = P01; B.V2_Color = C01;
			Tris.Add(B);
		}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Item);
}
} // namespace TorqueProfilePick

// [profile] The helmet's colour, as the shop's picker: the square (richness across, brightness up)
// for the hue picked in the strip beside it, and the named colours (the first: as it came).
void ATorquePlayerController::DrawHelmetPicker(UCanvas* Canvas, float X, float Y, float W, float H, float Px)
{
	namespace Ui = TorqueUi;
	const FTorqueProfile& Me = FTorqueProfile::Mine();
	const float Gap = 10.0f * Px, TitleH = 26.0f * Px;
	Ui::Text(Canvas, Me.bHelmetPainted ? FString::Printf(TEXT("HELMET COLOUR  (%s)"), FTorqueProfile::HelmetFinishName(Me.HelmetFinish))
		: FString(TEXT("HELMET COLOUR  (as it came)")), X, Y, 15.0f * Px, Ui::Accent, true);
	const float CY = Y + TitleH, SqH = H - TitleH, SqW = FMath::Min(SqH * 1.35f, W * 0.52f), HueW = 22.0f * Px;
	const float Hue = Me.HelmetHue;
	const float Fade = Me.bHelmetPainted ? 1.0f : 0.45f;   // as it came: shown, but not the colour worn
	TorqueProfilePick::Gradient(Canvas, X, CY, SqW, SqH, 24, 24,
		[Hue, Fade](float U, float V) { return FTorqueProfile::HsvColour(Hue, U, (1.0f - V) * Fade); });
	Ui::Frame(Canvas, X - 1.0f, CY - 1.0f, SqW + 2.0f, SqH + 2.0f, Ui::Line, 1.0f);
	PickSquare = FBox2D(FVector2D(X, CY), FVector2D(X + SqW, CY + SqH));
	if (Me.bHelmetPainted)
	{
		const float MX = X + Me.HelmetSat * SqW, MY = CY + (1.0f - Me.HelmetVal) * SqH, R = 6.0f * Px;
		Ui::Frame(Canvas, MX - R - Px, MY - R - Px, 2.0f * R + 2.0f * Px, 2.0f * R + 2.0f * Px, FLinearColor(0, 0, 0, 0.7f), Px);
		Ui::Frame(Canvas, MX - R, MY - R, 2.0f * R, 2.0f * R, FLinearColor::White, 2.0f * Px);
	}
	const float HueX = X + SqW + Gap;
	TorqueProfilePick::Gradient(Canvas, HueX, CY, HueW, SqH, 1, 72, [](float, float V) { return FTorqueProfile::HsvColour(V, 1.0f, 1.0f); });
	Ui::Frame(Canvas, HueX - 1.0f, CY - 1.0f, HueW + 2.0f, SqH + 2.0f, Ui::Line, 1.0f);
	PickHue = FBox2D(FVector2D(HueX - 3.0f * Px, CY), FVector2D(HueX + HueW + 3.0f * Px, CY + SqH));
	{
		const float MY = CY + Me.HelmetHue * SqH;
		Ui::Frame(Canvas, HueX - 4.0f * Px, MY - 4.0f * Px, HueW + 8.0f * Px, 8.0f * Px, FLinearColor(0, 0, 0, 0.7f), Px);
		Ui::Frame(Canvas, HueX - 3.0f * Px, MY - 3.0f * Px, HueW + 6.0f * Px, 6.0f * Px, FLinearColor::White, 2.0f * Px);
	}
	// The named colours: as it came first (crossed), then the sixteen, four across.
	PickSwatches.Reset();
	const float SX = HueX + HueW + 2.0f * Gap, SW = X + W - SX;
	const int32 Across = 4, Count = 1 + (int32)FTorqueProfile::NumHelmetSwatch, Down = (Count + Across - 1) / Across;
	const float Cell = FMath::Min(SW / Across, SqH / Down), Sw = Cell - 6.0f * Px;
	const int32 On = Me.HelmetSwatch();
	for (int32 I = 0; I < Count; ++I)
	{
		const float BX = SX + (I % Across) * Cell, BY = CY + (I / Across) * Cell;
		const bool bThis = I == 0 ? On == -2 : On == I - 1;
		if (I == 0)
		{
			Ui::Rect(Canvas, BX, BY, Sw, Sw, FLinearColor(0.08f, 0.08f, 0.09f, 1.0f));
			Ui::Text(Canvas, TEXT("Orig"), BX + 0.5f * Sw, BY + 0.5f * Sw - 7.0f * Px, 12.0f * Px, Ui::Dim, false, Ui::EAlign::Centre);
		}
		else
		{
			const FTorqueProfile::FHelmetSwatch& Sc = FTorqueProfile::HelmetSwatches[I - 1];
			Ui::Rect(Canvas, BX, BY, Sw, Sw, FTorqueProfile::HsvColour(Sc.Hue, Sc.Sat, Sc.Val));
		}
		Ui::Frame(Canvas, BX - Px, BY - Px, Sw + 2.0f * Px, Sw + 2.0f * Px, bThis ? Ui::Accent : FLinearColor(1, 1, 1, 0.18f), bThis ? 2.0f * Px : Px);
		PickSwatches.Add(FBox2D(FVector2D(BX, BY), FVector2D(BX + Sw, BY + Sw)));
	}
}

bool ATorquePlayerController::HelmetPickerMouse(const FVector2D& Mouse, bool bJustDown)
{
	FTorqueProfile& Me = FTorqueProfile::Mine();
	auto SetFromMouse = [this, &Me, &Mouse]() {
		if (PickDrag == 1 && PickSquare.bIsValid)
		{
			const FVector2D Sz = PickSquare.GetSize();
			Me.HelmetSat = FMath::Clamp((float)((Mouse.X - PickSquare.Min.X) / FMath::Max(Sz.X, 1.0)), 0.0f, 1.0f);
			Me.HelmetVal = 1.0f - FMath::Clamp((float)((Mouse.Y - PickSquare.Min.Y) / FMath::Max(Sz.Y, 1.0)), 0.0f, 1.0f);
		}
		else if (PickDrag == 2 && PickHue.bIsValid)
			Me.HelmetHue = FMath::Clamp((float)((Mouse.Y - PickHue.Min.Y) / FMath::Max(PickHue.GetSize().Y, 1.0)), 0.0f, 0.999f);
		Me.bHelmetPainted = true;
	};
	if (PickDrag != 0)
	{
		if (IsInputKeyDown(EKeys::LeftMouseButton))
		{
			SetFromMouse();   // the picture follows as it moves; let go saves it and tells the others
			return true;
		}
		PickDrag = 0;
		ProfileChanged();
		return true;
	}
	if (!bJustDown) return false;
	if (PickSquare.bIsValid && PickSquare.IsInside(Mouse)) PickDrag = 1;
	else if (PickHue.bIsValid && PickHue.IsInside(Mouse)) PickDrag = 2;
	if (PickDrag != 0)
	{
		SetFromMouse();
		return true;
	}
	for (int32 I = 0; I < PickSwatches.Num(); ++I)
		if (PickSwatches[I].IsInside(Mouse))
		{
			Me.SetHelmetSwatch(I - 1);   // 0: as it came
			ProfileChanged();
			return true;
		}
	return false;
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
	if (Page != EPage::Pause && !IsProfilePage()) return;
	if (!Preview.IsValid()) Preview = ATorqueProfilePreview::For(this);
	ATorqueProfilePreview::EView Want = ATorqueProfilePreview::EView::Face;   // the badge: your face
	if (IsProfilePage())
	{
		const TArray<FRow> List = Rows();
		const int32 At = DragRow >= 0 ? DragRow : Selected;
		const bool bHelmetRow = PickDrag != 0 || (List.IsValidIndex(At) && List[At].bHelmet);
		Want = bHelmetRow ? ATorqueProfilePreview::EView::Helmet
			: List.IsValidIndex(At) && List[At].bFace ? ATorqueProfilePreview::EView::Face : ATorqueProfilePreview::EView::Body;
	}
	if (ATorqueProfilePreview* Pv = Preview.Get()) Pv->Want(Want, FTorqueSettings::Get().Colour);
}

void ATorquePlayerController::DrawProfile(AHUD* Hud, UCanvas* Canvas)
{
	namespace Ui = TorqueUi;
	if (Page != EPage::Pause && !IsProfilePage()) return;
	const FMenuLayout L = MenuLayout(Canvas);
	const ATorqueProfilePreview* Pv = Preview.Get();
	// The badge, top right of the panel: your picture, your name, and which MetaHuman you are.
	const float BW = L.W(380), BH = L.W(76), BX = L.X(DescX1) - BW, BY = L.Y(84), Pic = BH - L.W(14);
	const bool bOn = IsProfilePage();
	float MX = 0.f, MY = 0.f;
	const bool bHot = GetMousePosition(MX, MY) && MX >= BX && MX < BX + BW && MY >= BY && MY < BY + BH;
	Ui::Rect(Canvas, BX, BY, BW, BH, FLinearColor(1, 1, 1, bHot ? 0.09f : 0.05f));
	Ui::Frame(Canvas, BX, BY, BW, BH, bOn ? Ui::Accent : FLinearColor(1, 1, 1, bHot ? 0.3f : 0.14f), bOn ? L.W(2) : FMath::Max(1.0f, L.S));
	if (Pv && Pv->HasAvatar())
		Pv->Draw(Canvas, BX + L.W(7), BY + L.W(7), Pic, Pic, true);
	else
		Ui::Rect(Canvas, BX + L.W(7), BY + L.W(7), Pic, Pic, FLinearColor(FTorqueSettings::Get().RiderColour()));
	const FTorqueSettings& Set = FTorqueSettings::Get();
	UTorqueSession* Session = UTorqueSession::Get(this);
	const FString Who = !Set.Name.IsEmpty() ? Set.Name : (Session ? Session->PlayerName() : FString(TEXT("You")));
	const FString What = Pv && Pv->HasAvatar() ? FString::Printf(TEXT("%s, about %.2f m"), *Pv->AvatarName(), Pv->Height() / 100.0)
											   : FString(TEXT("Profile and character"));
	const float TX = BX + Pic + L.W(22), TW = BX + BW - TX - L.W(12);
	Ui::TextFit(Canvas, Who, TX, BY + L.W(12), L.W(22), TW, Ui::Ink, true);
	Ui::TextFit(Canvas, bOn ? FString(TEXT("Click to go back")) : What, TX, BY + L.W(44), L.W(16), TW, Ui::Dim);
	Hits.Add(BX, BY, BX + BW, BY + BH, 0, FTorqueMenuHits::Badge);
}

void ATorquePlayerController::DrawNote(AHUD* Hud, UCanvas* Canvas)
{
	namespace Ui = TorqueUi;
	if (NoteFor <= 0.0 || Note.IsEmpty() || !Hud || !Canvas) return;
	const float S = Ui::Scale(Canvas), Px = 22.0f * S;
	const float W = (float)Ui::Measure(Note, Px).X, H = Px * 1.2f;
	const float Gap = 18.0f * S, X = 0.5f * (Canvas->ClipX - W), Y = Canvas->ClipY * 0.62f;
	const float A = (float)FMath::Clamp(NoteFor * 3.0, 0.0, 1.0);   // fades out at the end
	Ui::Rect(Canvas, X - Gap, Y - 0.6f * Gap, W + 2.0f * Gap, H + 1.2f * Gap, FLinearColor(Ui::Panel.R, Ui::Panel.G, Ui::Panel.B, Ui::Panel.A * A));
	Ui::Rect(Canvas, X - Gap, Y - 0.6f * Gap, 4.0f * S, H + 1.2f * Gap, FLinearColor(Ui::Accent.R, Ui::Accent.G, Ui::Accent.B, A));
	Ui::Text(Canvas, Note, X, Y, Px, FLinearColor(Ui::Ink.R, Ui::Ink.G, Ui::Ink.B, A));
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

void ATorquePlayerController::ServerRideYard_Implementation(int32 Bay, bool bAtBike)
{
	TorqueYard::HostRideOut(this, Bay, bAtBike);
}

void ATorquePlayerController::ServerGetOffYard_Implementation()
{
	TorqueYard::HostGetOff(this);
}

void ATorquePlayerController::ServerTakeBike_Implementation(ATorqueBikePawn* Theirs)
{
	ATorqueBikePawn* Mine = Cast<ATorqueBikePawn>(GetPawn());
	if (!Mine || !Theirs || Mine == Theirs || Mine->InYard() || Theirs->InYard()) return;
	ATorquePlayerController* Other = Cast<ATorquePlayerController>(Theirs->GetController());
	const FString OtherName = Theirs->GetPlayerState() ? Theirs->GetPlayerState()->GetPlayerName() : FString(TEXT("a mate"));
	// Both stood parked with their riders off (as the host sees them: its own, or their snapshots),
	// and the asker's feet by it (a little more reach than theirs: the host sees them a moment late).
	if (!Mine->IsParkedUp() || !Theirs->IsParkedUp() || !Other || Other == this)
	{
		ClientNote(FString::Printf(TEXT("%s's bike can't be taken now: they need to be off it, and you off yours."), *OtherName));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque net: %s takes %s's bike (bay %d), %s gets theirs (bay %d)"),
		PlayerState ? *PlayerState->GetPlayerName() : TEXT("a rider"), *OtherName, Theirs->GetRideBay() + 1, *OtherName, Mine->GetRideBay() + 1);
	UnPossess();
	Other->UnPossess();
	Possess(Theirs);
	Other->Possess(Mine);
	// The snapshots kept from each bike's last rider go: the new rider's machine sends afresh.
	if (Mine->NetBike) Mine->NetBike->ForgetSnapshots();
	if (Theirs->NetBike) Theirs->NetBike->ForgetSnapshots();
	const FString MyName = PlayerState ? PlayerState->GetPlayerName() : FString(TEXT("a mate"));
	ClientSwappedBike(Theirs, Mine, true, OtherName);
	Other->ClientSwappedBike(Mine, Theirs, false, MyName);
}

void ATorquePlayerController::ClientSwappedBike_Implementation(ATorqueBikePawn* NowMine, ATorqueBikePawn* Was, bool bRide, const FString& Whose)
{
	if (!NowMine) return;
	// Where we stand (as our own machine had us), on foot by the bike that's ours now.
	FVector Feet = NowMine->GetActorLocation();
	double Yaw = NowMine->GetActorRotation().Yaw;
	if (Was && Was->IsOnFoot())
	{
		Feet = Was->FeetAt();
		Yaw = Was->FeetYaw();
	}
	if (Was && Was != NowMine) Was->LetOver();
	NowMine->TakeOver(Feet, Yaw, bRide);
	ShowNote(bRide ? FString::Printf(TEXT("Riding %s's bike"), *Whose)
		: FString::Printf(TEXT("%s took your bike: theirs is yours, where they left it"), *Whose), 4.0);
}

void ATorquePlayerController::ServerWalkBackIn_Implementation()
{
	TorqueYard::HostWalkBackIn(this);
}
