// The walker at the tyre changer: Torque Shed's tyre fitting game (walker.gd _tyre_game_step and
// hud.gd _draw_tyre_game). The game itself is TorqueTyreGame; the shed keeps the bead it gives on
// the tyre (TyreFit's Amount).
#include "Shed/TorqueWalker.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueTyreGame.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorqueUi.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"

namespace TorqueWalkerTyreImpl
{
const FLinearColor Bad(1.0f, 0.3f, 0.2f);
const FLinearColor Good(0.3f, 1.0f, 0.4f);

// A filled circle (a fan of triangles).
void Disc(UCanvas* Canvas, const FVector2D& C, float R, const FLinearColor& Colour, int32 Segs = 32)
{
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Segs);
	for (int32 I = 0; I < Segs; ++I)
	{
		const float A0 = UE_TWO_PI * I / Segs, A1 = UE_TWO_PI * (I + 1) / Segs;
		FCanvasUVTri T;
		T.V0_Pos = C;
		T.V1_Pos = C + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R;
		T.V2_Pos = C + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R;
		T.V0_Color = T.V1_Color = T.V2_Color = Colour;
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

// A band round C from angle A0 to A1 (radians, 0 along +X, clockwise on the screen), Thick wide.
void Arc(UCanvas* Canvas, const FVector2D& C, float R, float A0, float A1, float Thick, const FLinearColor& Colour, int32 Segs)
{
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Segs * 2);
	const float In = R - Thick * 0.5f, Out = R + Thick * 0.5f;
	for (int32 I = 0; I < Segs; ++I)
	{
		const float U0 = FMath::Lerp(A0, A1, (float)I / Segs), U1 = FMath::Lerp(A0, A1, (float)(I + 1) / Segs);
		const FVector2D D0(FMath::Cos(U0), FMath::Sin(U0)), D1(FMath::Cos(U1), FMath::Sin(U1));
		FCanvasUVTri T0, T1;
		T0.V0_Pos = C + D0 * In; T0.V1_Pos = C + D0 * Out; T0.V2_Pos = C + D1 * Out;
		T1.V0_Pos = C + D0 * In; T1.V1_Pos = C + D1 * Out; T1.V2_Pos = C + D1 * In;
		T0.V0_Color = T0.V1_Color = T0.V2_Color = Colour;
		T1.V0_Color = T1.V1_Color = T1.V2_Color = Colour;
		Tris.Add(T0);
		Tris.Add(T1);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void Line(UCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thick)
{
	FCanvasLineItem L(A, B);
	L.SetColor(Colour);
	L.LineThickness = Thick;
	L.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(L);
}
} // namespace TorqueWalkerTyreImpl

namespace TWTI = TorqueWalkerTyreImpl;

bool ATorqueWalker::TyreWork(float Dt)
{
	// walker.gd _tyre_game_step: runs while the work button's held on the changer with a tyre in hand.
	const bool bHeld = bWorkHeld || ActHeldFor > 0.0;
	const bool bPlaying = bHeld && !bWaitRelease && Plan.bValid && Plan.Request.Kind == ETorqueShedAct::TyreFit && Plan.Blocked.IsEmpty();
	const FString Key = Plan.bValid ? Plan.Key() : FString();
	if (TyreGame.IsValid() && (!bPlaying || Key != TyreKey))
	{
		// Let go, or looked away, before the marker went round.
		TyreGame.Reset();
		Progress = 0.0;
		ProgressKey.Reset();
		Say(TEXT("The bead slipped back off the rim. Hold on all the way round."), false);
		bWaitRelease = bHeld;
		return true;
	}
	if (!bPlaying) return false;   // (Work says why not, or waits for the button to be let go)
	if (!TyreGame.IsValid())
	{
		TyreGame = MakeShared<FTorqueTyreGame>();
		TyreKey = Key;
	}
	TyreGame->Step(Dt);
	// "At work", for the work ring, the sounds and the others' view of you (the changer turns for them too).
	Progress = FMath::Max(TyreGame->Progress() * Plan.Time, 0.01);
	ProgressKey = Key;
	Doing(TEXT("fitting a tyre on the tyre changer"), 1.5);
	if (!TyreGame->Done()) return true;
	FPlan Finished = Plan;
	Finished.Request.Amount = (float)TyreGame->Quality();
	TyreGame.Reset();
	Progress = 0.0;
	ProgressKey.Reset();
	bWaitRelease = true;
	ActHeldFor = 0.0;
	Do(Finished);
	return true;
}

bool ATorqueWalker::TyreHudUp() const
{
	return TyreGame.IsValid() && !bComputer && !bBench;
}

bool ATorqueWalker::TurningChanger() const
{
	if (IsLocallyControlled())
		return Progress > 0.0 && TargetPlace && TargetPlace->Kind == ETorquePlace::Changer &&
			(TyreGame.IsValid() || ((bWorkHeld || ActHeldFor > 0.0) && Plan.bValid && Plan.Request.Kind == ETorqueShedAct::TyreRemove));
	// Another player: from their hands (working, aimed at the changer or the wheel on it).
	const ATorquePlayerState* PS = GetPlayerState<ATorquePlayerState>();
	return PS && PS->Hands.Work > 0 && PS->Hands.Place == FName(TEXT("changer"));
}

void ATorqueWalker::DrawTyre(AHUD* Hud, UCanvas* Canvas) const
{
	// hud.gd _draw_tyre_game: the rim as a ring, the marker going round it, your tyre lever, and each
	// stretch of the rim coloured by how well the bead went on there.
	if (!Hud || !Canvas || !TyreGame.IsValid()) return;
	const FTorqueTyreGame& G = *TyreGame;
	const float S = TorqueUi::Scale(Canvas);
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float R = FMath::Min(Canvas->ClipY * 0.2f, 150.0f * S);
	TWTI::Disc(Canvas, C, R + 34.0f * S, FLinearColor(0.03f, 0.03f, 0.04f, 0.55f), 48);
	TWTI::Arc(Canvas, C, R, 0.0f, UE_TWO_PI, 10.0f * S, FLinearColor(1.0f, 1.0f, 1.0f, 0.25f), 64);
	for (int32 I = 0; I < FTorqueTyreGame::Segments; ++I)
	{
		const double Q = G.Segment(I);
		if (Q < 0.0) continue;
		const float A0 = -UE_HALF_PI + UE_TWO_PI * I / FTorqueTyreGame::Segments;
		TWTI::Arc(Canvas, C, R, A0, A0 + UE_TWO_PI / FTorqueTyreGame::Segments + 0.01f, 10.0f * S,
			FMath::Lerp(TWTI::Bad, TWTI::Good, (float)Q), 4);
	}
	const FVector2D M = C + G.Marker() * R;
	TWTI::Disc(Canvas, M, 13.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	TWTI::Disc(Canvas, M, 10.0f * S, TorqueUi::Accent);
	const FVector2D Lever = C + G.Cursor * R;
	const float On = (float)G.OnTarget();
	TWTI::Line(Canvas, C, Lever, FLinearColor(1.0f, 1.0f, 1.0f, 0.35f), 3.0f * S);
	TWTI::Disc(Canvas, Lever, 7.0f * S, FMath::Lerp(FLinearColor::White, TWTI::Good, On), 20);
	TorqueUi::Text(Canvas, TEXT("WORK THE TYRE ON"), C.X, C.Y - R - 62.0f * S, 22.0f * S, TorqueUi::Accent, true, TorqueUi::EAlign::Centre);
	const FString Hint = FString::Printf(TEXT("Keep the lever on the marker with the mouse or right stick   ·   bead %d%%"),
		FMath::RoundToInt(G.Quality() * 100.0));
	TorqueUi::Text(Canvas, Hint, C.X, C.Y + R + 44.0f * S, 17.0f * S, TorqueUi::Ink, false, TorqueUi::EAlign::Centre);
}

FString ATorqueWalker::TestTyreGame(double Skill)
{
	if (!Plan.bValid || Plan.Request.Kind != ETorqueShedAct::TyreFit)
		return TEXT("not at the tyre changer with a tyre in hand: ") + Describe();
	if (!Plan.Blocked.IsEmpty()) return TEXT("can't: ") + Plan.Blocked;
	FTorqueTyreGame G;
	const double Lag = (1.0 - FMath::Clamp(Skill, 0.0, 1.0)) * 1.2;
	while (!G.Done())
	{
		G.Cursor = FTorqueTyreGame::OnRim(G.MarkerAngle() - Lag);
		G.Step(1.0 / 60.0);
	}
	FPlan P = Plan;
	P.Request.Amount = (float)G.Quality();
	const bool bOk = Do(P);
	return FString::Printf(TEXT("tyre game, lever %.2f rad behind the marker: bead %d%% (%s)"), Lag, FMath::RoundToInt(G.Quality() * 100.0),
		bOk ? TEXT("asked the shed") : *Note);
}
