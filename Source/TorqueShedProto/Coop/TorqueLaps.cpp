#include "TorqueLaps.h"

#include "TorquePlayerController.h"
#include "TorquePlayerState.h"
#include "TorqueUi.h"
#include "World/TorqueRideWorld.h"
#include "World/WorldRideCircuits.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"

#include <string>

// (named, not anonymous: the unity build puts several files in one)
namespace TLAPI
{
TorqueWorld::CircuitLaps GLaps;
double LapClock = 0.0;          // the ride's clock (s), stepped with the bike
double LapShownUntil = -1.0;    // the panel stays this long after a lap
FString LapNote;                // the circuit the panel shows
FString LapBestsSent;

FString LapUe(double Seconds) { return UTF8_TO_TCHAR(TorqueWorld::LapText(Seconds).c_str()); }

FString BestsText()
{
	FString Out;
	for (const auto& KV : GLaps.Best)
		Out += FString::Printf(TEXT("%s\t%.3f\n"), UTF8_TO_TCHAR(KV.first.c_str()), KV.second.Seconds);
	return Out;
}
} // namespace TLAPI

namespace TorqueLapsUe
{
void NewRide()
{
	using namespace TLAPI;
	GLaps.Reset();
	GLaps.Best.clear();
	LapClock = 0.0;
	LapShownUntil = -1.0;
	LapNote.Reset();
	LapBestsSent.Reset();
}

void Moved()
{
	TLAPI::GLaps.Jumped();
}

void StepBike(APawn* Bike, const FVector& SimPlace, float Dt)
{
	using namespace TLAPI;
	LapClock += Dt;
	if (!ATorqueRideWorld::StepLaps(SimPlace, LapClock, GLaps))
	{
		if (GLaps.Now().Running)
		{
			LapNote = UTF8_TO_TCHAR(GLaps.Now().Circuit.c_str());
			LapShownUntil = LapClock + 20.0;
		}
		return;
	}
	// A lap done: a note, the panel held a while, and the best (if it is one) to everyone.
	const TorqueWorld::CircuitLaps::Lap& L = GLaps.Last;
	LapNote = UTF8_TO_TCHAR(L.Circuit.c_str());
	LapShownUntil = LapClock + 20.0;
	const auto Bt = GLaps.Best.find(L.Circuit);
	const bool bBest = L.Counted && Bt != GLaps.Best.end() && Bt->second.Seconds == L.Seconds;
	UE_LOG(LogTemp, Log, TEXT("Torque laps: %s %s%s"), *LapNote, *LapUe(L.Seconds), L.Counted ? (bBest ? TEXT(" (best)") : TEXT("")) : TEXT(" (not counted)"));
	if (ATorquePlayerController* PC = Bike ? Cast<ATorquePlayerController>(Bike->GetController()) : nullptr)
		PC->ShowNote(FString::Printf(TEXT("%s  %s%s"), *LapNote, *LapUe(L.Seconds),
			L.Counted ? (bBest ? TEXT("  best lap") : TEXT("")) : TEXT("  (off the track: not counted)")), 5.0);
	const FString Bests = BestsText();
	ATorquePlayerState* PS = Bike ? Bike->GetPlayerState<ATorquePlayerState>() : nullptr;
	if (PS && Bests != LapBestsSent)
	{
		LapBestsSent = Bests;
		if (PS->HasAuthority()) PS->LapBests = Bests;
		else PS->ServerSetLapBests(Bests);
	}
}

bool BestLap(double& Seconds, FString& Circuit)
{
	using namespace TLAPI;
	bool bAny = false;
	for (const auto& KV : GLaps.Best)
		if (!bAny || KV.second.Seconds < Seconds)
		{
			bAny = true;
			Seconds = KV.second.Seconds;
			Circuit = UTF8_TO_TCHAR(KV.first.c_str());
		}
	return bAny;
}

void Draw(UCanvas* Canvas, const APawn* Bike)
{
	using namespace TLAPI;
	namespace Ui = TorqueUi;
	if (!Canvas || LapClock > LapShownUntil || LapNote.IsEmpty()) return;
	const TorqueWorld::CircuitLaps::State& St = GLaps.Now();
	const bool bRunning = St.Running && FString(UTF8_TO_TCHAR(St.Circuit.c_str())) == LapNote;
	const std::string Key = TCHAR_TO_UTF8(*LapNote);

	// The riders' bests of this circuit (this one's own, then the others' from their player states).
	TArray<TPair<FString, double>> Board;
	if (const UWorld* World = Bike ? Bike->GetWorld() : nullptr)
		if (const AGameStateBase* GS = World->GetGameState())
			for (APlayerState* P : GS->PlayerArray)
			{
				const ATorquePlayerState* T = Cast<ATorquePlayerState>(P);
				if (!T) continue;
				const bool bMe = T == Bike->GetPlayerState();
				const FString Text = bMe ? BestsText() : T->LapBests;
				TArray<FString> Lines;
				Text.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					FString Name, Secs;
					if (Line.Split(TEXT("\t"), &Name, &Secs) && Name == LapNote) Board.Add({T->GetPlayerName(), FCString::Atod(*Secs)});
				}
			}
	Board.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value < B.Value; });

	const float S = Ui::Scale(Canvas);
	const float W = 420.0f * S, Pad = 14.0f * S;
	const float X = (Canvas->ClipX - W) * 0.5f;
	float Y = 24.0f * S;
	const int32 Rows = 3 + Board.Num();
	Ui::Rect(Canvas, X, Y, W, Pad * 2.0f + 30.0f * S + Rows * 24.0f * S, Ui::Panel);
	Ui::Rect(Canvas, X, Y, W, 3.0f * S, Ui::Accent);
	Y += Pad;
	Ui::Text(Canvas, LapNote.ToUpper(), X + Pad, Y, 16.0f * S, Ui::Dim, true);
	const double Running = bRunning ? LapClock - St.Started : (GLaps.HasLast ? GLaps.Last.Seconds : 0.0);
	const FString Big = LapUe(Running);
	Ui::Text(Canvas, Big, X + W - Pad - (float)Ui::Measure(Big, 28.0f * S, true).X, Y - 6.0f * S, 28.0f * S,
		bRunning && St.Spoilt ? Ui::Warn : Ui::Ink, true);
	Y += 30.0f * S;
	// The sectors of the lap under way (else the last lap's).
	FString Splits;
	if (bRunning)
		for (int32 K = 0; K < (int32)St.Splits.size(); ++K)
			Splits += FString::Printf(TEXT("S%d %s   "), K + 1, *LapUe(St.Splits[K] - (K > 0 ? St.Splits[K - 1] : 0.0)));
	else if (GLaps.HasLast)
		for (int32 K = 0; K < (int32)GLaps.Last.Splits.size(); ++K)
			Splits += FString::Printf(TEXT("S%d %s   "), K + 1, *LapUe(GLaps.Last.Splits[K]));
	Ui::Text(Canvas, Splits.IsEmpty() ? FString(TEXT("Sectors as you cross them")) : Splits, X + Pad, Y, 15.0f * S, Ui::Dim);
	Y += 24.0f * S;
	const bool bLastHere = GLaps.HasLast && GLaps.Last.Circuit == Key;
	Ui::Text(Canvas, bLastHere ? FString::Printf(TEXT("Last  %s%s"), *LapUe(GLaps.Last.Seconds), GLaps.Last.Counted ? TEXT("") : TEXT("  (not counted)"))
		: FString(TEXT("Last  -")), X + Pad, Y, 16.0f * S, Ui::Ink);
	const auto Bt = GLaps.Best.find(Key);
	const FString BestText = Bt != GLaps.Best.end() ? FString::Printf(TEXT("Best  %s"), *LapUe(Bt->second.Seconds)) : FString(TEXT("Best  -"));
	Ui::Text(Canvas, BestText, X + W - Pad - (float)Ui::Measure(BestText, 16.0f * S).X, Y, 16.0f * S, Ui::Accent);
	Y += 24.0f * S;
	if (bRunning && St.Spoilt)
		Ui::Text(Canvas, TEXT("Off the track: this lap won't count"), X + Pad, Y, 15.0f * S, Ui::Warn);
	Y += 24.0f * S;
	for (int32 K = 0; K < Board.Num(); ++K)
	{
		const FString Left = FString::Printf(TEXT("%d  %s"), K + 1, *Board[K].Key);
		const FString Right = LapUe(Board[K].Value);
		Ui::Text(Canvas, Left, X + Pad, Y, 15.0f * S, K == 0 ? Ui::Accent : Ui::Ink);
		Ui::Text(Canvas, Right, X + W - Pad - (float)Ui::Measure(Right, 15.0f * S).X, Y, 15.0f * S, Ui::Ink);
		Y += 24.0f * S;
	}
}
} // namespace TorqueLapsUe
