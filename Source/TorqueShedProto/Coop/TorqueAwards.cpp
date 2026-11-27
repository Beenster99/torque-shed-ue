#include "TorqueAwards.h"

#include "RideAwards.h"
#include "TorqueGameState.h"
#include "TorquePlayerController.h"
#include "TorquePlayerState.h"
#include "TorqueSettings.h"
#include "TorqueUi.h"
#include "TorqueDash.h"
#include "TorqueLaps.h"
#include "Sim/BikeSim.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "SonyPad.h"
#include "TorqueAudio/TorqueSfx.h"

#include <array>
#include <vector>

// ---------------------------------------------------------------------------------------------------
// The rider's own numbers.

// (named, not anonymous: the unity build puts several files in one)
namespace TAWI
{
TorqueAwards::Tracker GAwardTracker;
bool bAwardSawFar = false;        // the planned finish was more than a km away this ride (a real ride to it)
bool bAwardSaidFinished = false;  // told the host this rider is at the finish
double AwardSendClock = 0.0;
int32 AwardResultsSeq = INDEX_NONE;

TArray<ATorquePlayerState*> AwardRiderStates(const AGameStateBase* GS)
{
	TArray<ATorquePlayerState*> Out;
	if (!GS) return Out;
	for (APlayerState* P : GS->PlayerArray)
		if (ATorquePlayerState* T = Cast<ATorquePlayerState>(P)) Out.Add(T);
	return Out;
}
} // namespace TAWI

namespace TorqueAwardsUe
{
void NewRide()
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	GAwardTracker.Reset();
	bAwardSawFar = false;
	bAwardSaidFinished = false;
	AwardSendClock = 0.0;
	AwardResultsSeq = INDEX_NONE;
}

void ResultsIn(int32 Seq)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	if (Seq == AwardResultsSeq) return;
	AwardResultsSeq = Seq;
	// The rest of the ride counts afresh (another awards later on it starts from nothing).
	GAwardTracker.Reset();
	bAwardSawFar = false;
	bAwardSaidFinished = false;
}

void StepBike(UObject* Bike, const TorqueSim::BikeSim& Sim, const FTorqueDash& Dash, bool bOnFoot, float Dt)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	TorqueAwards::Sample S;
	S.Dt = Dt;
	S.Speed = Sim.Speed;
	S.LeanRad = Sim.LeanAngle;
	S.FrontDown = Sim.Wheels[0].Contact;
	S.RearDown = Sim.Wheels[1].Contact;
	S.Crashed = Sim.IsCrashed;
	S.OnFoot = bOnFoot;
	S.RearOffRoad = Sim.Wheels[1].Surf == TorqueSim::SURF_GRASS || Sim.Wheels[1].Surf == TorqueSim::SURF_GRAVEL;
	S.HaveRoute = Dash.bNav;
	S.OffRoute = Dash.bNavOff;
	S.LimitMs = Dash.NavLimit > 0 ? Dash.NavLimit * (Dash.bNavLimitMph ? 0.44704 : 1.0 / 3.6) : 0.0;
	S.FrontSlip = Sim.Wheels[0].Kappa;
	S.RearSlip = Sim.Wheels[1].Kappa;
	S.Burnout = Sim.Burnout;
	{
		// [race tracks] The lap timer's best counted lap of this ride (Coop/TorqueLaps.h).
		double LapSeconds = 0.0;
		FString LapCircuit;
		if (TorqueLapsUe::BestLap(LapSeconds, LapCircuit)) S.BestLap = LapSeconds;
	}
	GAwardTracker.Step(S);

	const APawn* Rider = Cast<APawn>(Bike);
	UWorld* W = Rider ? Rider->GetWorld() : nullptr;
	const ATorqueGameState* GS = W ? W->GetGameState<ATorqueGameState>() : nullptr;
	ATorquePlayerState* PS = Rider ? Rider->GetPlayerState<ATorquePlayerState>() : nullptr;
	if (!GS || !PS || !GS->bAwardsOn) return;

	// To the host every second (a dozen numbers: nothing to the frame rate or the network).
	AwardSendClock += Dt;
	if (AwardSendClock >= 1.0)
	{
		AwardSendClock = 0.0;
		TArray<float> Out;
		Out.Reserve(TorqueAwards::Total);
		for (const double V : GAwardTracker.V) Out.Add((float)V);
		if (PS->HasAuthority()) PS->AwardStats = Out;
		else PS->ServerSetAwardStats(Out);
	}

	// Stopped at the planned ride's finish (not a waypoint's flag): the host is told once.
	if (Dash.bNav && Dash.NavLeft > 1000.0) bAwardSawFar = true;
	// ([ferries] beenster, Nov 26: the ferry's slipway is a leg of the ride, not its end: no awards there)
	const bool bAtFinish = bAwardSawFar && Dash.bNav && !Dash.bNavVia && !Dash.bNavOff && Dash.NavFerry != 1 && Dash.NavLeft < 60.0 && FMath::Abs(Sim.Speed) < 2.0 && !Sim.IsCrashed;
	if (bAtFinish && !bAwardSaidFinished && GS->AwardsPhase != 1)
	{
		bAwardSaidFinished = true;
		PS->ServerSetAwardFinished(true);
		UE_LOG(LogTemp, Log, TEXT("Torque awards: %s is at the finish"), *PS->GetPlayerName());
		if (ATorquePlayerController* PC = Cast<ATorquePlayerController>(Rider->GetController()))
		{
			const bool bAlone = GS->PlayerArray.Num() <= 1;
			PC->ShowNote(bAlone ? FString(TEXT("At the finish: the ride awards"))
								: FString(TEXT("At the finish. The ride awards start when everyone's here (or the host starts them from the menu)")), 6.0);
		}
	}
}
} // namespace TorqueAwardsUe

// ---------------------------------------------------------------------------------------------------
// The host: starting the awards, the vote, and the scoring.

void ATorqueGameState::StartAwards()
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	if (!HasAuthority() || AwardsPhase == 1) return;
	const TArray<ATorquePlayerState*> Riders = AwardRiderStates(this);
	if (Riders.Num() == 0) return;
	++AwardsSeq;
	FString Code, Name;
	if (!RideJourney.Split(TEXT("\n"), &Code, &Name)) Name.Reset();
	AwardsRoute = Name;
	// [race tracks] The best lap tile only when someone rode a lap.
	bAwardsLaps = false;
	for (const ATorquePlayerState* R : Riders)
		if (R->AwardStats.IsValidIndex(TorqueAwards::BestLap) && R->AwardStats[TorqueAwards::BestLap] > 0.0f) bAwardsLaps = true;
	for (ATorquePlayerState* R : Riders)
	{
		R->AwardPicks.Reset();
		R->AwardVotedSeq = -1;
	}
	if (Riders.Num() == 1)
	{
		// Alone: no vote, straight to the numbers.
		FinishAwards();
		return;
	}
	AwardsPhase = 1;
	AwardsVoteEnds = GetServerWorldTimeSeconds() + AwardsVoteSeconds;
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("Torque awards: vote %d started, %d riders"), AwardsSeq, Riders.Num());
}

FString ATorqueGameState::AwardsWaitingFor() const
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	TArray<FString> Names;
	for (const ATorquePlayerState* R : AwardRiderStates(this))
		if (!R->bAwardFinished) Names.Add(R->GetPlayerName());
	return FString::Join(Names, TEXT(", "));
}

void ATorqueGameState::TickAwards(float /*DeltaSeconds*/)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	bAwardsOn = FTorqueSettings::Get().bRideAwards;
	const TArray<ATorquePlayerState*> Riders = AwardRiderStates(this);
	if (AwardsPhase == 1)
	{
		if (!bAwardsOn)
		{
			AwardsPhase = 0;   // switched off part way: no awards
			return;
		}
		// Everyone ticked: a short countdown, then the results.
		const double Now = GetServerWorldTimeSeconds();
		bool bAll = Riders.Num() > 0;
		for (const ATorquePlayerState* R : Riders)
			if (R->AwardVotedSeq != AwardsSeq) bAll = false;
		if (bAll && AwardsVoteEnds > Now + 3.0)
		{
			AwardsVoteEnds = Now + 3.0;
			ForceNetUpdate();
		}
		if (Now >= AwardsVoteEnds) FinishAwards();
		return;
	}
	if (!bAwardsOn || Riders.Num() == 0) return;
	for (const ATorquePlayerState* R : Riders)
		if (!R->bAwardFinished) return;
	StartAwards();
}

void ATorqueGameState::FinishAwards()
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	const TArray<ATorquePlayerState*> Riders = AwardRiderStates(this);
	std::vector<std::array<double, TorqueAwards::Count>> Nums;
	std::vector<std::array<int, 3>> Votes;
	for (const ATorquePlayerState* R : Riders)
	{
		std::array<double, TorqueAwards::Count> A{};
		for (int32 K = 0; K < TorqueAwards::Count && K < R->AwardStats.Num(); ++K) A[K] = R->AwardStats[K];
		Nums.push_back(A);
		if (R->AwardVotedSeq == AwardsSeq && R->AwardPicks.Num() == 3) Votes.push_back({{R->AwardPicks[0], R->AwardPicks[1], R->AwardPicks[2]}});
	}
	const TorqueAwards::Result Res = TorqueAwards::Score(Nums, Votes);
	AwardRiders.Reset();
	for (const int Idx : Res.Order)
	{
		const ATorquePlayerState* R = Riders[Idx];
		FTorqueAwardRider Entry;
		Entry.Name = R->GetPlayerName();
		Entry.Colour = R->Colour;
		Entry.Stats.SetNumZeroed(TorqueAwards::Total);
		for (int32 K = 0; K < TorqueAwards::Total && K < R->AwardStats.Num(); ++K) Entry.Stats[K] = R->AwardStats[K];
		Entry.Places.SetNumZeroed(TorqueAwards::Count);
		for (int32 K = 0; K < TorqueAwards::Count; ++K) Entry.Places[K] = (uint8)FMath::Clamp(Res.Place[Idx][K], 0, 255);
		Entry.Points = Res.Score[Idx];
		Entry.Led = (uint8)FMath::Clamp(Res.Led[Idx], 0, 255);
		AwardRiders.Add(Entry);
	}
	AwardPickers.SetNumZeroed(TorqueAwards::Count);
	AwardWeights.SetNumZeroed(TorqueAwards::Count);
	for (int32 K = 0; K < TorqueAwards::Count; ++K)
	{
		AwardPickers[K] = (uint8)FMath::Clamp(Res.Pickers[K], 0, 255);
		AwardWeights[K] = (uint8)FMath::Clamp(Res.Weight[K], 0, 255);
	}
	bAwardsNoVotes = Res.bNoVotes;
	AwardsPhase = 2;
	for (ATorquePlayerState* R : Riders)
	{
		R->bAwardFinished = false;
		R->AwardPicks.Reset();   // (private: gone once counted)
	}
	ForceNetUpdate();
	for (int32 I = 0; I < AwardRiders.Num(); ++I)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque awards: %d. %s, %d points (led %d)"), I + 1, *AwardRiders[I].Name, AwardRiders[I].Points, AwardRiders[I].Led);
	}
}

// ---------------------------------------------------------------------------------------------------
// On screen.

namespace TAWI
{
struct FAwLay
{
	float S = 1.0f, OX = 0.0f, OY = 0.0f;
	float X(float V) const { return OX + V * S; }
	float Y(float V) const { return OY + V * S; }
	float W(float V) const { return V * S; }
};
FAwLay AwLayOf(const UCanvas* Canvas)
{
	FAwLay L;
	L.S = TorqueUi::Scale(Canvas);
	L.OX = 0.5f * (Canvas->ClipX - 1920.0f * L.S);
	L.OY = 0.5f * (Canvas->ClipY - 1080.0f * L.S);
	return L;
}
// The grid of tiles, 1080p pixels: a dozen, four across; with a best lap (someone rode a circuit) thirteen,
// five across.
constexpr float AwGridX = 160.0f, AwGridY = 214.0f, AwGridW = 1600.0f, AwGridH = 690.0f, AwGap = 18.0f;
struct FAwGrid
{
	int32 Tiles = 12, Cols = 4;
	float TileW = 0.0f, TileH = 0.0f;
	explicit FAwGrid(bool bLaps)
	{
		Tiles = bLaps ? TorqueAwards::Count : TorqueAwards::BestLap;
		Cols = Tiles > 12 ? 5 : 4;
		const int32 Rows = (Tiles + Cols - 1) / Cols;
		TileW = (AwGridW - AwGap * (float)(Cols - 1)) / (float)Cols;
		TileH = (AwGridH - AwGap * (float)(Rows - 1)) / (float)Rows;
	}
	float X(int32 K) const { return AwGridX + (float)(K % Cols) * (TileW + AwGap); }
	float Y(int32 K) const { return AwGridY + (float)(K / Cols) * (TileH + AwGap); }
};

FLinearColor AwRiderColour(uint8 Index, float Alpha = 1.0f)
{
	const TArray<FColor>& All = FTorqueSettings::Colours();
	const FColor C = All.Num() > 0 ? All[FMath::Clamp<int32>(Index, 0, All.Num() - 1)] : FColor::White;
	// (pale for the dark ones, as the name tags do)
	FLinearColor Out((C.R + C.G + C.B) < 200 ? FColor(220, 220, 220) : C);
	Out.A = Alpha;
	return Out;
}

const ATorqueGameState* AwGameStateOf(const APlayerController* PC)
{
	const UWorld* W = PC ? PC->GetWorld() : nullptr;
	return W ? W->GetGameState<ATorqueGameState>() : nullptr;
}

FString AwValueText(int32 Stat, double V)
{
	const bool bMetric = FTorqueSettings::Get().Units == 0;
	switch (TorqueAwards::Info(Stat).Kind)
	{
	case TorqueAwards::Unit::Speed: return FTorqueSettings::Get().SpeedText(V);
	case TorqueAwards::Unit::Metres:
		if (bMetric) return V >= 1000.0 ? FString::Printf(TEXT("%.1f km"), V / 1000.0) : FString::Printf(TEXT("%.0f m"), V);
		return V >= 1609.344 ? FString::Printf(TEXT("%.1f mi"), V / 1609.344) : FString::Printf(TEXT("%.0f yd"), V / 0.9144);
	case TorqueAwards::Unit::Number: return FString::Printf(TEXT("%.0f"), V);
	case TorqueAwards::Unit::Percent: return FString::Printf(TEXT("%.0f%%"), V);
	case TorqueAwards::Unit::Degrees: return FString::Printf(TEXT("%.0f\u00B0"), V);
	case TorqueAwards::Unit::Seconds: return FString::Printf(TEXT("%.1f s"), V);
	case TorqueAwards::Unit::Score: return FString::Printf(TEXT("%.0f / 100"), V);
	case TorqueAwards::Unit::LapTime:
	{
		const int32 Ms = FMath::RoundToInt(V * 1000.0);
		return FString::Printf(TEXT("%d:%02d.%03d"), Ms / 60000, (Ms / 1000) % 60, Ms % 1000);
	}
	}
	return FString::Printf(TEXT("%.1f"), V);
}

FString AwPlaceWord(int32 P)
{
	return P == 1 ? FString(TEXT("1st")) : P == 2 ? FString(TEXT("2nd")) : P == 3 ? FString(TEXT("3rd")) : FString::Printf(TEXT("%dth"), P);
}

// The keys along the bottom: a key, then what it does, and so on.
void AwHints(UCanvas* Canvas, const FAwLay& L, const TArray<FString>& Keys)
{
	float X = L.X(AwGridX);
	for (int32 I = 0; I + 1 < Keys.Num(); I += 2) X += TorqueUi::KeyHint(Canvas, Keys[I], Keys[I + 1], X, L.Y(966.0f), L.S) + L.W(28.0f);
}
} // namespace TAWI

void FTorqueAwardsScreen::SetView(EView V)
{
	View = V;
	ViewAt = FPlatformTime::Seconds();
	PodiumChimes = 0;
}

void FTorqueAwardsScreen::Begin(int32 InSeq)
{
	Seq = InSeq;
	Picks.Reset();
	Hot = 0;
	SentFor = -1;
	SetView(EView::Vote);
}

void FTorqueAwardsScreen::Pick(int32 Stat)
{
	if (Stat < 0 || Stat >= TorqueAwards::Count) return;
	// Picked again: taken back (and those after it move up). Else the next pick, up to three.
	const int32 At = Picks.IndexOfByKey(Stat);
	if (At != INDEX_NONE) Picks.RemoveAt(At);
	else if (Picks.Num() < 3) Picks.Add(Stat);
}

void FTorqueAwardsScreen::SendPicks(APlayerController* PC)
{
	int32 Hash = Picks.Num();
	for (const int32 P : Picks) Hash = Hash * 31 + P + 1;
	if (Hash == SentFor) return;
	ATorquePlayerState* PS = PC ? PC->GetPlayerState<ATorquePlayerState>() : nullptr;
	if (!PS) return;
	SentFor = Hash;
	TArray<uint8> Out;
	for (const int32 P : Picks) Out.Add((uint8)P);
	PS->ServerAwardVote(Seq, Out);
}

bool FTorqueAwardsScreen::Tick(APlayerController* PC, uint32 SonyHit)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	const ATorqueGameState* GS = AwGameStateOf(PC);
	if (!PC || !GS) return false;
	if (GS->AwardsSeq != Seq) Begin(GS->AwardsSeq);
	// The results are in: from the vote to the dashboard (the numbers start afresh for the rest of the ride).
	if (GS->AwardsPhase == 2)
	{
		TorqueAwardsUe::ResultsIn(Seq);
		if (View == EView::Vote) SetView(EView::Dashboard);
	}
	else if (GS->AwardsPhase == 0 && View == EView::Vote) return false;   // (the host switched them off)

	const auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	// The left stick as four buttons, once each push.
	const float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	const int32 NewX = LX > 0.6f ? 1 : LX < -0.6f ? -1 : 0, NewY = LY > 0.6f ? -1 : LY < -0.6f ? 1 : 0;
	const int32 StickHitX = NewX != StickX ? NewX : 0, StickHitY = NewY != StickY ? NewY : 0;
	StickX = NewX;
	StickY = NewY;
	const int32 MoveX = (Just(EKeys::Right) || Just(EKeys::D) || Just(EKeys::Gamepad_DPad_Right) || (SonyHit & ESonyButton::DRight) ? 1 : 0)
		- (Just(EKeys::Left) || Just(EKeys::A) || Just(EKeys::Gamepad_DPad_Left) || (SonyHit & ESonyButton::DLeft) ? 1 : 0) + StickHitX;
	const int32 MoveY = (Just(EKeys::Down) || Just(EKeys::S) || Just(EKeys::Gamepad_DPad_Down) || (SonyHit & ESonyButton::DDown) ? 1 : 0)
		- (Just(EKeys::Up) || Just(EKeys::W) || Just(EKeys::Gamepad_DPad_Up) || (SonyHit & ESonyButton::DUp) ? 1 : 0) + StickHitY;
	const bool bChoose = Just(EKeys::Enter) || Just(EKeys::SpaceBar) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyHit & ESonyButton::Cross);
	const bool bBack = Just(EKeys::BackSpace) || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyHit & ESonyButton::Circle)
		|| Just(EKeys::Gamepad_Special_Right) || (SonyHit & ESonyButton::Options);
	float MX = 0.0f, MY = 0.0f;
	Mouse = PC->GetMousePosition(MX, MY) ? FVector2D(MX, MY) : FVector2D(-1.0, -1.0);
	const bool bMouseMoved = Mouse.X >= 0.0 && LastMouse.X >= 0.0 && FVector2D::DistSquared(Mouse, LastMouse) > 1.0;
	LastMouse = Mouse;
	int32 Under = INDEX_NONE;
	for (int32 I = 0; I < TileBoxes.Num(); ++I)
		if (TileBoxes[I].IsInside(Mouse)) Under = I;
	const bool bClick = Just(EKeys::LeftMouseButton);

	if (View == EView::Vote)
	{
		if (MoveX != 0 || MoveY != 0)
		{
			const FAwGrid G(GS->bAwardsLaps);
			const int32 Col = FMath::Clamp(Hot % G.Cols + MoveX, 0, G.Cols - 1);
			const int32 Row = FMath::Clamp(Hot / G.Cols + MoveY, 0, (G.Tiles - 1) / G.Cols);
			Hot = FMath::Clamp(Row * G.Cols + Col, 0, G.Tiles - 1);
		}
		if (bMouseMoved && Under != INDEX_NONE) Hot = Under;
		if (bClick && Under != INDEX_NONE)
		{
			Hot = Under;
			Pick(Under);
		}
		else if (bChoose) Pick(Hot);
		else if (bBack && Picks.Num() > 0) Picks.Pop();   // (Esc can't skip the vote: it takes the last pick back)
		SendPicks(PC);   // three picks: ready; fewer: not
		return true;
	}
	if (View == EView::Dashboard)
	{
		// Alone there's no podium: the dashboard is the end.
		const bool bPodium = GS->AwardRiders.Num() > 1;
		if (bChoose || bClick) SetView(bPodium ? EView::Podium : EView::Dashboard);
		if ((bChoose || bClick) && !bPodium) return false;
		if (bBack) return false;
		return true;
	}
	// The podium: Enter during the reveal skips to the end (the chimes it passes stay quiet); after it,
	// rides on. R / Triangle / Y plays the reveal again.
	const int32 Steps = FMath::Min(GS->AwardRiders.Num(), 3);
	const double Age = FPlatformTime::Seconds() - ViewAt;
	if (bBack) SetView(EView::Dashboard);
	else if (Just(EKeys::R) || Just(EKeys::Gamepad_FaceButton_Top) || (SonyHit & ESonyButton::Triangle)) SetView(EView::Podium);
	else if ((bChoose || bClick) && Age < PodiumEnd(Steps))
	{
		ViewAt = FPlatformTime::Seconds() - PodiumEnd(Steps);
		PodiumChimes = 0xFF;
	}
	else if (bChoose || bClick) return false;
	if (View == EView::Podium) PodiumSounds(PC, Steps);
	return true;
}

double FTorqueAwardsScreen::RevealAt(int32 Place, int32 Steps)
{
	// The lowest place first, a beat between; the winner after a longer drum roll.
	if (Steps >= 3) return Place == 2 ? 1.6 : Place == 1 ? 3.4 : 6.2;
	return Place == 1 ? 1.6 : 4.4;
}

double FTorqueAwardsScreen::PodiumEnd(int32 Steps)
{
	return RevealAt(0, Steps) + 1.6;
}

void FTorqueAwardsScreen::PodiumSounds(APlayerController* PC, int32 Steps)
{
	UTorqueSfx* Sfx = UTorqueSfx::Get(PC);
	if (!Sfx) return;
	const double Age = FPlatformTime::Seconds() - ViewAt;
	for (int32 Place = Steps - 1; Place >= 0; --Place)
	{
		const int32 Bit = 1 << Place;
		if ((PodiumChimes & Bit) || Age < RevealAt(Place, Steps)) continue;
		PodiumChimes |= Bit;
		Sfx->PlayUi(Place == 0 ? TEXT("levelup") : TEXT("ting"), Place == 0 ? 0.0f : -4.0f);
		if (Place == 0) Sfx->PlayUi(TEXT("bead_pop"), -6.0f);   // (the confetti going off)
	}
}

void FTorqueAwardsScreen::Draw(UCanvas* Canvas, APlayerController* PC)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	if (!Canvas) return;
	TileBoxes.Reset();
	const FAwLay L = AwLayOf(Canvas);
	Ui::Backdrop(Canvas, 0.7f);
	Ui::Rect(Canvas, L.X(120.0f), L.Y(56.0f), L.W(1680.0f), L.W(968.0f), Ui::Panel);
	Ui::Rect(Canvas, L.X(120.0f), L.Y(56.0f), L.W(160.0f), L.W(3.0f), Ui::Accent);
	if (View == EView::Vote) DrawVote(Canvas, PC);
	else if (View == EView::Dashboard) DrawDashboard(Canvas, PC);
	else DrawPodium(Canvas, PC);
}

void FTorqueAwardsScreen::DrawVote(UCanvas* Canvas, APlayerController* PC)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	const FAwLay L = AwLayOf(Canvas);
	const ATorqueGameState* GS = AwGameStateOf(PC);
	Ui::Text(Canvas, TEXT("RIDE AWARDS: YOUR VOTE"), L.X(AwGridX), L.Y(84.0f), L.W(34.0f), Ui::Accent, true);
	Ui::Text(Canvas, TEXT("Pick the three things you care about most, in order. Your picks are private: nobody sees them, and the numbers come after."),
		L.X(AwGridX), L.Y(134.0f), L.W(18.0f), Ui::Dim);
	// The countdown, and who's ready (a tick, never what they picked).
	if (GS)
	{
		const double Left = FMath::Max(0.0, GS->AwardsVoteEnds - GS->GetServerWorldTimeSeconds());
		Ui::Text(Canvas, FString::Printf(TEXT("Votes close in %.0f s"), FMath::CeilToDouble(Left)), L.X(AwGridX + AwGridW), L.Y(90.0f), L.W(22.0f), Ui::Ink, true,
			Ui::EAlign::Right);
		float X = L.X(AwGridX + AwGridW);
		for (int32 I = GS->PlayerArray.Num() - 1; I >= 0; --I)
		{
			const ATorquePlayerState* R = Cast<ATorquePlayerState>(GS->PlayerArray[I]);
			if (!R) continue;
			const bool bReady = R->AwardVotedSeq == GS->AwardsSeq;
			const FString Word = R->GetPlayerName() + (bReady ? TEXT("  READY") : TEXT("  voting..."));
			const float Wd = (float)Ui::Measure(Word, L.W(15.0f), bReady).X;
			X -= Wd;
			Ui::Text(Canvas, Word, X, L.Y(172.0f), L.W(15.0f), bReady ? FLinearColor(0.45f, 0.9f, 0.5f) : Ui::Faint, bReady);
			X -= L.W(16.0f);
			Ui::Disc(Canvas, X + L.W(6.0f), L.Y(182.0f), L.W(6.0f), AwRiderColour(R->Colour));
			X -= L.W(22.0f);
		}
	}
	const FAwGrid G(GS && GS->bAwardsLaps);
	for (int32 K = 0; K < G.Tiles; ++K)
	{
		const float TX = L.X(G.X(K)), TY = L.Y(G.Y(K));
		const float TW = L.W(G.TileW), TH = L.W(G.TileH);
		TileBoxes.Add(FBox2D(FVector2D(TX, TY), FVector2D(TX + TW, TY + TH)));
		const int32 PickAt = Picks.IndexOfByKey(K);
		Ui::Rect(Canvas, TX, TY, TW, TH, PickAt != INDEX_NONE ? FLinearColor(0.96f, 0.63f, 0.17f, 0.16f) : Ui::PanelLight);
		if (K == Hot) Ui::Frame(Canvas, TX, TY, TW, TH, Ui::Accent, FMath::Max(2.0f, L.W(3.0f)));
		const TorqueAwards::StatInfo& Info = TorqueAwards::Info(K);
		Ui::TextFit(Canvas, UTF8_TO_TCHAR(Info.Name), TX + L.W(22.0f), TY + L.W(24.0f), L.W(26.0f), TW - L.W(100.0f), Ui::Ink, true);
		Ui::TextWrap(Canvas, UTF8_TO_TCHAR(Info.Desc), TX + L.W(22.0f), TY + L.W(70.0f), L.W(17.0f), TW - L.W(44.0f), Ui::Dim);
		if (PickAt != INDEX_NONE)
		{
			// The pick's place, big in the corner.
			const float CX = TX + TW - L.W(46.0f), CY = TY + L.W(46.0f);
			Ui::Disc(Canvas, CX, CY, L.W(30.0f), Ui::Accent, 32);
			Ui::Text(Canvas, FString::Printf(TEXT("%d"), PickAt + 1), CX, CY - L.W(19.0f), L.W(30.0f), FLinearColor(0.05f, 0.05f, 0.05f), true, Ui::EAlign::Centre,
				false);
			Ui::Text(Canvas, AwPlaceWord(PickAt + 1) + TEXT(" pick"), TX + L.W(22.0f), TY + TH - L.W(40.0f), L.W(18.0f), Ui::Accent, true);
		}
	}
	const FString State = Picks.Num() == 3 ? FString(TEXT("Voted: waiting for the others. Change your mind until the vote closes."))
		: FString::Printf(TEXT("%d of 3 picked"), Picks.Num());
	Ui::Text(Canvas, State, L.X(AwGridX + AwGridW), L.Y(968.0f), L.W(18.0f), Picks.Num() == 3 ? FLinearColor(0.45f, 0.9f, 0.5f) : Ui::Ink, Picks.Num() == 3,
		Ui::EAlign::Right);
	TAWI::AwHints(Canvas, L, {TEXT("Enter"), TEXT("Pick / take back"), TEXT("Backspace"), TEXT("Undo the last")});
}

void FTorqueAwardsScreen::DrawDashboard(UCanvas* Canvas, APlayerController* PC)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	const FAwLay L = AwLayOf(Canvas);
	const ATorqueGameState* GS = AwGameStateOf(PC);
	if (!GS) return;
	const TArray<FTorqueAwardRider>& Riders = GS->AwardRiders;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	// The header: the ride, how far, how long, the weather.
	Ui::Text(Canvas, TEXT("RIDE AWARDS"), L.X(AwGridX), L.Y(80.0f), L.W(34.0f), Ui::Accent, true);
	double Far = 0.0, Long = 0.0;
	for (const FTorqueAwardRider& R : Riders)
	{
		if (R.Stats.IsValidIndex(TorqueAwards::Distance)) Far = FMath::Max(Far, (double)R.Stats[TorqueAwards::Distance]);
		if (R.Stats.IsValidIndex(TorqueAwards::RideTime)) Long = FMath::Max(Long, (double)R.Stats[TorqueAwards::RideTime]);
	}
	const int32 Mins = FMath::RoundToInt(Long / 60.0);
	const FString When = Mins >= 60 ? FString::Printf(TEXT("%d h %02d min"), Mins / 60, Mins % 60) : FString::Printf(TEXT("%d min"), Mins);
	const FString Route = GS->AwardsRoute.IsEmpty() ? FString(TEXT("Ride UK")) : GS->AwardsRoute;
	Ui::TextFit(Canvas, FString::Printf(TEXT("%s   |   %s   |   %s ridden"), *Route, *Set.DistanceText(Far), *When), L.X(AwGridX), L.Y(130.0f), L.W(20.0f),
		L.W(1000.0f), Ui::Ink);
	Ui::TextFit(Canvas, GS->WeatherHereText(), L.X(AwGridX + AwGridW), L.Y(130.0f), L.W(17.0f), L.W(580.0f), Ui::Dim, false, Ui::EAlign::Right);
	if (!GS->bAwardsNoVotes && Riders.Num() > 1)
		Ui::Text(Canvas, TEXT("Lit tiles are the ones you voted for: they decide the podium"), L.X(AwGridX + AwGridW), L.Y(86.0f), L.W(16.0f), Ui::Accent, false,
			Ui::EAlign::Right);

	const double Rise = FMath::Clamp((FPlatformTime::Seconds() - ViewAt) / 0.8, 0.0, 1.0);
	const FAwGrid G(GS && GS->bAwardsLaps);
	for (int32 K = 0; K < G.Tiles; ++K)
	{
		const float TX = L.X(G.X(K)), TY = L.Y(G.Y(K));
		const float TW = L.W(G.TileW), TH = L.W(G.TileH);
		TileBoxes.Add(FBox2D(FVector2D(TX, TY), FVector2D(TX + TW, TY + TH)));
		const int32 Pickers = GS->AwardPickers.IsValidIndex(K) ? GS->AwardPickers[K] : 0;
		const bool bVoted = !GS->bAwardsNoVotes && Pickers > 0;
		Ui::Rect(Canvas, TX, TY, TW, TH, bVoted ? FLinearColor(0.96f, 0.63f, 0.17f, 0.10f) : Ui::PanelLight);
		if (bVoted) Ui::Rect(Canvas, TX, TY, L.W(4.0f), TH, Ui::Accent);
		const TorqueAwards::StatInfo& Info = TorqueAwards::Info(K);
		Ui::TextFit(Canvas, FString(UTF8_TO_TCHAR(Info.Name)).ToUpper(), TX + L.W(18.0f), TY + L.W(14.0f), L.W(17.0f), TW - L.W(140.0f), Ui::Dim, true);
		if (bVoted)
			Ui::Text(Canvas, FString::Printf(TEXT("picked by %d"), Pickers), TX + TW - L.W(16.0f), TY + L.W(15.0f), L.W(15.0f), Ui::Accent, true, Ui::EAlign::Right);
		// The riders, best first.
		// (a lap time: the smallest best, and nobody's 0 "no lap" ahead of a time)
		const bool bLower = Info.bLower;
		auto ValueOf = [&Riders, K](int32 I) { return Riders[I].Stats.IsValidIndex(K) ? (double)Riders[I].Stats[K] : 0.0; };
		TArray<int32> Order;
		double Best = 0.0;
		for (int32 I = 0; I < Riders.Num(); ++I)
		{
			Order.Add(I);
			const double Mine = ValueOf(I);
			if (Mine > 1e-6) Best = Best <= 1e-6 ? Mine : bLower ? FMath::Min(Best, Mine) : FMath::Max(Best, Mine);
		}
		Order.StableSort([&ValueOf, bLower](int32 A, int32 B) {
			const double VA = ValueOf(A), VB = ValueOf(B);
			if (bLower && (VA > 1e-6) != (VB > 1e-6)) return VA > 1e-6;
			return bLower ? VA < VB : VA > VB;
		});
		if (Best <= 1e-6 || Order.Num() == 0)
		{
			Ui::Text(Canvas, TEXT("Nobody"), TX + L.W(18.0f), TY + L.W(46.0f), L.W(40.0f), Ui::Faint, true);
			continue;
		}
		// The leader's number, big, in their colour.
		const FTorqueAwardRider& Top = Riders[Order[0]];
		Ui::Text(Canvas, AwValueText(K, ValueOf(Order[0]) * (bLower ? 1.0 : Rise)), TX + L.W(18.0f), TY + L.W(42.0f), L.W(42.0f), Ui::Ink, true);
		Ui::TextFit(Canvas, Top.Name, TX + TW - L.W(16.0f), TY + L.W(58.0f), L.W(20.0f), TW * 0.45f, AwRiderColour(Top.Colour), true, Ui::EAlign::Right);
		// A bar for each rider (up to four).
		const int32 Shown = FMath::Min(Order.Num(), 4);
		const float BarX = TX + L.W(96.0f), BarW = TW - L.W(96.0f + 92.0f), Row = L.W(22.0f);
		float RY = TY + TH - L.W(14.0f) - (float)Shown * Row;
		for (int32 N = 0; N < Shown; ++N, RY += Row)
		{
			const FTorqueAwardRider& R = Riders[Order[N]];
			const double V = ValueOf(Order[N]);
			Ui::TextFit(Canvas, R.Name, TX + L.W(18.0f), RY, L.W(14.0f), L.W(72.0f), N == 0 ? Ui::Ink : Ui::Dim);
			Ui::Rect(Canvas, BarX, RY + L.W(4.0f), BarW, L.W(10.0f), Ui::Line);
			Ui::Rect(Canvas, BarX, RY + L.W(4.0f), BarW * (float)FMath::Clamp((V <= 1e-6 ? 0.0 : bLower ? Best / V : V / Best) * Rise, 0.0, 1.0), L.W(10.0f), AwRiderColour(R.Colour));
			Ui::Text(Canvas, V > 1e-6 ? AwValueText(K, V) : FString(TEXT("-")), TX + TW - L.W(16.0f), RY, L.W(14.0f), N == 0 ? Ui::Ink : Ui::Dim, false,
				Ui::EAlign::Right);
		}
	}
	if (Riders.Num() > 1) TAWI::AwHints(Canvas, L, {TEXT("Enter"), TEXT("The podium"), TEXT("Esc"), TEXT("Close")});
	else TAWI::AwHints(Canvas, L, {TEXT("Enter"), TEXT("Close")});
}

namespace TAWI
{
// [podium finale] A steady pseudo-random 0..1, the K-th number of particle I (the same every frame: the
// confetti keeps no state, it's worked out from the clock).
float AwRand(int32 I, int32 K)
{
	const float V = FMath::Sin((float)I * 12.9898f + (float)K * 78.233f) * 43758.5453f;
	return V - FMath::FloorToFloat(V);
}
// A filled ellipse (a floor's glow, a spotlight's pool).
void AwEllipse(UCanvas* Canvas, float CX, float CY, float RX, float RY, const FLinearColor& Colour)
{
	TArray<FVector2D> Pts;
	for (int32 I = 0; I < 40; ++I)
	{
		const float A = 2.0f * PI * (float)I / 40.0f;
		Pts.Add(FVector2D(CX + RX * FMath::Cos(A), CY + RY * FMath::Sin(A)));
	}
	TorqueUi::Polygon(Canvas, Pts, Colour);
}
// A W x H quad turned by Angle round its middle: a piece of confetti.
void AwQuad(UCanvas* Canvas, float CX, float CY, float W, float H, float Angle, const FLinearColor& Colour)
{
	const float Cs = FMath::Cos(Angle), Sn = FMath::Sin(Angle);
	const float HW = 0.5f * W, HH = 0.5f * H;
	TArray<FVector2D> Pts;
	Pts.Add(FVector2D(CX - HW * Cs + HH * Sn, CY - HW * Sn - HH * Cs));
	Pts.Add(FVector2D(CX + HW * Cs + HH * Sn, CY + HW * Sn - HH * Cs));
	Pts.Add(FVector2D(CX + HW * Cs - HH * Sn, CY + HW * Sn + HH * Cs));
	Pts.Add(FVector2D(CX - HW * Cs - HH * Sn, CY - HW * Sn + HH * Cs));
	TorqueUi::Polygon(Canvas, Pts, Colour);
}
// Gold, silver, bronze.
FLinearColor AwMetal(int32 Place, float Alpha = 1.0f)
{
	const FLinearColor C = Place == 0 ? FLinearColor(1.0f, 0.78f, 0.25f) : Place == 1 ? FLinearColor(0.80f, 0.84f, 0.90f) : FLinearColor(0.82f, 0.50f, 0.28f);
	return FLinearColor(C.R, C.G, C.B, Alpha);
}
// 0..1 with a little overshoot at the end (a step landing).
float AwEaseBack(float T)
{
	const float U = FMath::Clamp(T, 0.0f, 1.0f) - 1.0f;
	return 1.0f + 2.70158f * U * U * U + 1.70158f * U * U;
}
} // namespace TAWI

void FTorqueAwardsScreen::DrawPodium(UCanvas* Canvas, APlayerController* PC)
{
	using namespace TAWI;
	namespace Ui = TorqueUi;
	const FAwLay L = AwLayOf(Canvas);
	const ATorqueGameState* GS = AwGameStateOf(PC);
	if (!GS) return;
	const TArray<FTorqueAwardRider>& Riders = GS->AwardRiders;
	const int32 Steps = FMath::Min(Riders.Num(), 3);
	if (Steps == 0) return;
	// The reveal (beenster, build 160: "the podium could be improved as it was a bit basic"): the stage goes
	// dark, spotlights sweep, the lowest place is revealed first, a drum roll, then the winner: their step
	// lands, the medal drops, the name slams in, the lights settle on them and the confetti goes off. All
	// drawn on the canvas from the clock: a few hundred flat shapes, nothing to the frame rate.
	const float Age = (float)(FPlatformTime::Seconds() - ViewAt);
	const float End = (float)PodiumEnd(Steps);
	auto Since = [Age, Steps](int32 Place) { return Age - (float)RevealAt(Place, Steps); };
	const float WinnerAt = Since(0);
	const float Floor = 820.0f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Age * 7.0f);

	// The stage: darker, a warm glow on the floor.
	Ui::Rect(Canvas, L.X(120.0f), L.Y(56.0f), L.W(1680.0f), L.W(968.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	AwEllipse(Canvas, L.X(960.0f), L.Y(Floor + 12.0f), L.W(640.0f), L.W(64.0f), FLinearColor(0.96f, 0.63f, 0.17f, 0.05f));
	AwEllipse(Canvas, L.X(960.0f), L.Y(Floor + 12.0f), L.W(420.0f), L.W(40.0f), FLinearColor(0.96f, 0.63f, 0.17f, 0.06f));

	// Two spotlights from the top corners, sweeping the stage until the winner, then both on them.
	{
		const float Settle = FMath::Clamp(WinnerAt / 0.6f, 0.0f, 1.0f);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float FromX = Side == 0 ? 300.0f : 1620.0f;
			const float Sweep = 960.0f + (Side == 0 ? 1.0f : -1.0f) * 440.0f * FMath::Sin(Age * 1.4f + (float)Side * 1.9f);
			const float ToX = FMath::Lerp(Sweep, 960.0f, Settle);
			for (int32 Layer = 0; Layer < 3; ++Layer)
			{
				const float Half = 60.0f + 50.0f * (float)Layer;
				TArray<FVector2D> Beam;
				Beam.Add(FVector2D(L.X(FromX - 10.0f), L.Y(58.0f)));
				Beam.Add(FVector2D(L.X(FromX + 10.0f), L.Y(58.0f)));
				Beam.Add(FVector2D(L.X(ToX + Half), L.Y(Floor + 20.0f)));
				Beam.Add(FVector2D(L.X(ToX - Half), L.Y(Floor + 20.0f)));
				Ui::Polygon(Canvas, Beam, FLinearColor(1.0f, 0.95f, 0.82f, 0.03f));
			}
			AwEllipse(Canvas, L.X(ToX), L.Y(Floor + 18.0f), L.W(170.0f), L.W(24.0f), FLinearColor(1.0f, 0.95f, 0.82f, 0.08f));
		}
	}

	// The heading, typed out; then the podium's.
	{
		const FString Head = WinnerAt >= 0.0f ? FString(TEXT("THE PODIUM")) : FString(TEXT("AND THE RIDE AWARDS GO TO..."));
		const int32 Chars = WinnerAt >= 0.0f ? Head.Len() : FMath::Clamp((int32)(Age * 28.0f), 0, Head.Len());
		Ui::Text(Canvas, Head.Left(Chars), L.X(960.0f), L.Y(78.0f), L.W(40.0f), Ui::Accent, true, Ui::EAlign::Centre);
		Ui::Text(Canvas, GS->bAwardsNoVotes ? FString(TEXT("Nobody voted: every stat counted the same")) : FString(TEXT("Scored on what you all voted for")),
			L.X(960.0f), L.Y(132.0f), L.W(17.0f), Ui::Dim, false, Ui::EAlign::Centre);
	}

	// The steps: 2nd on the left, 1st in the middle, 3rd on the right. Pass 0 the light behind them (a
	// flash as each is revealed, the winner's rays and halo); pass 1 the steps, medals and names.
	static const int32 SlotOf[3] = {1, 0, 2};
	static const float StepH[3] = {300.0f, 210.0f, 150.0f};   // 1st, 2nd, 3rd
	const float ColW = 340.0f, ColGap = 24.0f;
	const float Left = 960.0f - 1.5f * ColW - ColGap;
	for (int32 Pass = 0; Pass < 2; ++Pass)
		for (int32 Col = 0; Col < 3; ++Col)
		{
			const int32 Place = SlotOf[Col];
			if (Place >= Steps) continue;
			const FTorqueAwardRider& R = Riders[Place];
			const float T = Since(Place);
			const bool bShown = T >= 0.0f;
			const float X0 = Left + (float)Col * (ColW + ColGap), XM = X0 + 0.5f * ColW;
			// (a low dark plinth until its moment; then it rises, landing with a little bounce)
			const float Ease = bShown ? AwEaseBack(T / 0.55f) : 0.0f;
			const float H = StepH[Place] * (0.15f + 0.85f * Ease);
			const float Top = Floor - H;
			const float MedalY = Floor - StepH[Place] - 66.0f;
			const float NamePx = Place == 0 ? 46.0f : 34.0f;
			const float NameY = MedalY - 96.0f - NamePx * 1.2f;
			if (Pass == 0)
			{
				if (!bShown) continue;
				// The flash down the column as it's revealed.
				if (T < 0.4f) Ui::Rect(Canvas, L.X(X0 - 12.0f), L.Y(58.0f), L.W(ColW + 24.0f), L.W(Floor - 58.0f), FLinearColor(1.0f, 0.97f, 0.9f, 0.22f * (1.0f - T / 0.4f)));
				if (Place == 0)
				{
					// The winner: slow-turning rays and a halo behind the medal.
					const float Glow = FMath::Clamp(T / 0.6f, 0.0f, 1.0f);
					const float Spin = Age * 0.25f;
					for (int32 Ray = 0; Ray < 16; ++Ray)
					{
						const float A0 = Spin + 2.0f * PI * (float)Ray / 16.0f, A1 = A0 + 0.12f;
						TArray<FVector2D> Tri;
						Tri.Add(FVector2D(L.X(XM), L.Y(MedalY)));
						Tri.Add(FVector2D(L.X(XM + 300.0f * FMath::Cos(A0)), L.Y(MedalY + 300.0f * FMath::Sin(A0))));
						Tri.Add(FVector2D(L.X(XM + 300.0f * FMath::Cos(A1)), L.Y(MedalY + 300.0f * FMath::Sin(A1))));
						Ui::Polygon(Canvas, Tri, AwMetal(0, 0.06f * Glow));
					}
					Ui::Disc(Canvas, L.X(XM), L.Y(MedalY), L.W(130.0f), AwMetal(0, 0.05f * Glow), 48);
					Ui::Disc(Canvas, L.X(XM), L.Y(MedalY), L.W(80.0f), AwMetal(0, 0.07f * Glow), 40);
				}
				continue;
			}
			// The step: a top face in perspective, the front, a metal trim, the place cut into it.
			{
				TArray<FVector2D> Face;
				Face.Add(FVector2D(L.X(X0), L.Y(Top)));
				Face.Add(FVector2D(L.X(X0 + ColW), L.Y(Top)));
				Face.Add(FVector2D(L.X(X0 + ColW - 22.0f), L.Y(Top - 20.0f)));
				Face.Add(FVector2D(L.X(X0 + 22.0f), L.Y(Top - 20.0f)));
				Ui::Polygon(Canvas, Face, bShown ? FLinearColor(0.20f, 0.21f, 0.23f, 0.96f) : FLinearColor(0.10f, 0.105f, 0.115f, 0.9f));
			}
			Ui::Rect(Canvas, L.X(X0), L.Y(Top), L.W(ColW), L.W(H), FLinearColor(0.07f, 0.075f, 0.085f, 0.97f));
			Ui::Rect(Canvas, L.X(X0 + ColW - 12.0f), L.Y(Top), L.W(12.0f), L.W(H), FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
			Ui::Rect(Canvas, L.X(X0), L.Y(Top), L.W(ColW), L.W(6.0f), AwMetal(Place, bShown ? 1.0f : 0.3f));
			if (!bShown)
			{
				// Still to come: a question mark over the plinth (pulsing for the next one up).
				const bool bNext = Place == 0 ? Since(1) >= 0.0f || Steps < 2 : true;
				Ui::Text(Canvas, TEXT("?"), L.X(XM), L.Y(Top - 110.0f), L.W(64.0f), FLinearColor(1.0f, 1.0f, 1.0f, bNext ? 0.15f + 0.25f * Pulse : 0.15f), true,
					Ui::EAlign::Centre);
				continue;
			}
			const float NumPx = Place == 0 ? 120.0f : 90.0f;
			if (H > NumPx * 0.9f)
				Ui::Text(Canvas, FString::Printf(TEXT("%d"), Place + 1), L.X(XM), L.Y(Top + 14.0f), L.W(NumPx), AwMetal(Place, 0.9f), true, Ui::EAlign::Centre, false);
			// The points, counting up.
			{
				const float Counted = FMath::Clamp((T - 0.3f) / 0.8f, 0.0f, 1.0f);
				const int32 Shown = FMath::RoundToInt((float)R.Points * Counted);
				if (H > NumPx * 0.9f + 40.0f)
					Ui::Text(Canvas, FString::Printf(TEXT("%d points"), Shown), L.X(XM), L.Y(Top + 24.0f + NumPx * 1.05f), L.W(18.0f), Ui::Ink, false, Ui::EAlign::Centre);
			}
			// The medal drops onto its ribbon, in the rider's colour.
			{
				const float Drop = FMath::Clamp(T / 0.35f, 0.0f, 1.0f);
				const float MY = MedalY - (1.0f - Drop) * (1.0f - Drop) * 180.0f;
				const FLinearColor Ribbon = AwRiderColour(R.Colour, Drop);
				TArray<FVector2D> RL, RR;
				RL.Add(FVector2D(L.X(XM - 34.0f), L.Y(MY - 86.0f)));
				RL.Add(FVector2D(L.X(XM - 12.0f), L.Y(MY - 86.0f)));
				RL.Add(FVector2D(L.X(XM + 6.0f), L.Y(MY - 24.0f)));
				RL.Add(FVector2D(L.X(XM - 14.0f), L.Y(MY - 24.0f)));
				RR.Add(FVector2D(L.X(XM + 12.0f), L.Y(MY - 86.0f)));
				RR.Add(FVector2D(L.X(XM + 34.0f), L.Y(MY - 86.0f)));
				RR.Add(FVector2D(L.X(XM + 14.0f), L.Y(MY - 24.0f)));
				RR.Add(FVector2D(L.X(XM - 6.0f), L.Y(MY - 24.0f)));
				Ui::Polygon(Canvas, RL, Ribbon * FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
				Ui::Polygon(Canvas, RR, Ribbon);
				Ui::Disc(Canvas, L.X(XM), L.Y(MY), L.W(38.0f), AwMetal(Place, Drop), 40);
				Ui::Disc(Canvas, L.X(XM), L.Y(MY), L.W(29.0f), AwMetal(Place, Drop) * FLinearColor(0.78f, 0.78f, 0.78f, 1.0f), 40);
				Ui::Disc(Canvas, L.X(XM - 10.0f), L.Y(MY - 12.0f), L.W(7.0f), FLinearColor(1.0f, 1.0f, 1.0f, 0.35f * Drop), 16);
				Ui::Text(Canvas, FString::Printf(TEXT("%d"), Place + 1), L.X(XM), L.Y(MY - 19.0f), L.W(30.0f), FLinearColor(0.08f, 0.06f, 0.03f, Drop), true,
					Ui::EAlign::Centre, false);
			}
			// The name slams in (big to its size), in the rider's colour.
			{
				const float Slam = FMath::Clamp(T / 0.25f, 0.0f, 1.0f);
				const float Px = NamePx * FMath::Lerp(1.9f, 1.0f, Slam);
				Ui::TextFit(Canvas, R.Name, L.X(XM), L.Y(NameY + 0.5f * (NamePx - Px)), L.W(Px), L.W(ColW + 60.0f), AwRiderColour(R.Colour, Slam), true,
					Ui::EAlign::Centre);
			}
			// The winner's banner, unrolling over the name.
			if (Place == 0)
			{
				const float Unroll = FMath::Clamp((T - 0.2f) / 0.45f, 0.0f, 1.0f);
				const float BW = 440.0f * Unroll, BY = NameY - 58.0f;
				if (Unroll > 0.0f)
				{
					// The swallowtails each side, a shade darker, a little lower: two pieces each (the canvas fans
					// a shape from its first point, so the notch can't be one shape).
					const FLinearColor Dark(0.62f, 0.38f, 0.08f, 1.0f);
					for (int32 Side = -1; Side <= 1; Side += 2)
					{
						const float Edge = XM + (float)Side * 0.5f * BW;
						auto Pt = [&L, Edge, Side, BY](float Out, float Down) { return FVector2D(L.X(Edge + (float)Side * Out), L.Y(BY + Down)); };
						TArray<FVector2D> Upper, Lower;
						Upper.Add(Pt(30.0f, 6.0f));
						Upper.Add(Pt(-4.0f, 6.0f));
						Upper.Add(Pt(-4.0f, 24.0f));
						Upper.Add(Pt(16.0f, 24.0f));
						Lower.Add(Pt(16.0f, 24.0f));
						Lower.Add(Pt(-4.0f, 24.0f));
						Lower.Add(Pt(-4.0f, 46.0f));
						Lower.Add(Pt(30.0f, 46.0f));
						Ui::Polygon(Canvas, Upper, Dark);
						Ui::Polygon(Canvas, Lower, Dark);
					}
					Ui::Rect(Canvas, L.X(XM - 0.5f * BW), L.Y(BY), L.W(BW), L.W(40.0f), Ui::Accent);
					Ui::Rect(Canvas, L.X(XM - 0.5f * BW), L.Y(BY), L.W(BW), L.W(3.0f), FLinearColor(1.0f, 0.9f, 0.6f, 1.0f));
					if (Unroll >= 1.0f)
						Ui::Text(Canvas, TEXT("CHAMPION OF THE RIDE"), L.X(XM), L.Y(BY + 8.0f), L.W(22.0f), FLinearColor(0.08f, 0.05f, 0.02f, 1.0f), true,
							Ui::EAlign::Centre, false);
				}
			}
			// What they won on (the voted stats they led), as chips under the step, one after another.
			{
				TArray<FString> Won;
				for (int32 K = 0; K < TorqueAwards::Count && K < R.Places.Num(); ++K)
				{
					const bool bCounts = GS->bAwardsNoVotes || (GS->AwardWeights.IsValidIndex(K) && GS->AwardWeights[K] > 0);
					if (R.Places[K] == 1 && bCounts) Won.Add(UTF8_TO_TCHAR(TorqueAwards::Info(K).Name));
				}
				float CX = X0, CY = Floor + 22.0f;
				const float ChipPx = 14.0f, ChipH = 24.0f, Pad = 10.0f;
				for (int32 N = 0; N < Won.Num(); ++N)
				{
					if (T < 0.8f + 0.15f * (float)N) break;
					const float TW = (float)Ui::Measure(Won[N], L.W(ChipPx), true).X / L.S + 2.0f * Pad;
					if (CX + TW > X0 + ColW && CX > X0)
					{
						CX = X0;
						CY += ChipH + 6.0f;
					}
					if (CY > Floor + 22.0f + 2.0f * (ChipH + 6.0f)) break;   // (two rows at most)
					Ui::Rect(Canvas, L.X(CX), L.Y(CY), L.W(TW), L.W(ChipH), AwMetal(Place, 0.18f));
					Ui::Rect(Canvas, L.X(CX), L.Y(CY), L.W(3.0f), L.W(ChipH), AwMetal(Place));
					Ui::Text(Canvas, Won[N], L.X(CX + Pad), L.Y(CY + 3.0f), L.W(ChipPx), Ui::Ink, true, Ui::EAlign::Left, false);
					CX += TW + 6.0f;
				}
			}
		}

	// The confetti: a burst from either side of the winner's step, then a gentle fall from the top for as
	// long as the podium's up. In the winner's colour, gold, white and amber.
	if (WinnerAt >= 0.0f)
	{
		const FLinearColor Pal[4] = {AwRiderColour(Riders[0].Colour), AwMetal(0), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), Ui::Accent};
		const float WinTop = Floor - StepH[0];
		for (int32 I = 0; I < 150; ++I)
		{
			const float T = WinnerAt - 0.25f * AwRand(I, 0);
			if (T < 0.0f || T > 5.0f) continue;
			const float Dir = (I % 2 == 0) ? -1.0f : 1.0f;
			const float OX = 960.0f + Dir * 150.0f, OY = WinTop;
			const float VX = Dir * (80.0f + 520.0f * AwRand(I, 1)), VY = -(450.0f + 650.0f * AwRand(I, 2));
			const float Peak = -VY / 900.0f;
			const float Up = FMath::Min(T, Peak);
			const float X = OX + VX * (1.0f - FMath::Exp(-1.6f * T)) / 1.6f + 22.0f * FMath::Sin(T * 3.0f + (float)I);
			const float Y = OY + VY * Up + 450.0f * Up * Up + 120.0f * FMath::Max(0.0f, T - Peak);
			if (Y > 1020.0f || Y < 60.0f || X < 125.0f || X > 1795.0f) continue;
			const float Alpha = FMath::Clamp(5.0f - T, 0.0f, 1.0f);
			const float Flip = FMath::Abs(FMath::Cos(T * (5.0f + 4.0f * AwRand(I, 3)) + (float)I));
			const FLinearColor C = Pal[I % 4];
			AwQuad(Canvas, L.X(X), L.Y(Y), L.W(11.0f), L.W(2.0f + 6.0f * Flip), T * 3.0f + (float)I, FLinearColor(C.R, C.G, C.B, Alpha));
		}
		const float Gentle = FMath::Clamp((WinnerAt - 1.5f) / 1.5f, 0.0f, 0.8f);
		if (Gentle > 0.0f)
			for (int32 I = 0; I < 70; ++I)
			{
				const float Speed = 70.0f + 60.0f * AwRand(I, 4);
				const float Span = 960.0f / Speed;
				const float T = FMath::Fmod(Age + Span * AwRand(I, 5), Span);
				const float X = 140.0f + 1640.0f * AwRand(I, 6) + 26.0f * FMath::Sin(T * 1.7f + (float)I);
				const float Y = 60.0f + Speed * T;
				const float Flip = FMath::Abs(FMath::Cos(T * 4.0f + (float)I));
				const FLinearColor C = Pal[I % 4];
				AwQuad(Canvas, L.X(X), L.Y(Y), L.W(9.0f), L.W(2.0f + 5.0f * Flip), T * 2.0f + (float)I, FLinearColor(C.R, C.G, C.B, Gentle));
			}
	}

	// Everyone else, below.
	if (Riders.Num() > 3 && WinnerAt >= 0.0f)
	{
		TArray<FString> Rest;
		for (int32 I = 3; I < Riders.Num(); ++I) Rest.Add(FString::Printf(TEXT("%d. %s (%d)"), I + 1, *Riders[I].Name, Riders[I].Points));
		Ui::TextFit(Canvas, FString::Join(Rest, TEXT("    ")), L.X(960.0f), L.Y(930.0f), L.W(17.0f), L.W(1500.0f), Ui::Dim, false, Ui::EAlign::Centre);
	}
	if (Age < End) TAWI::AwHints(Canvas, L, {TEXT("Enter"), TEXT("Skip"), TEXT("Esc"), TEXT("Back to the numbers")});
	else TAWI::AwHints(Canvas, L, {TEXT("Enter"), TEXT("Ride on"), TEXT("R"), TEXT("Again"), TEXT("Esc"), TEXT("Back to the numbers")});
}
