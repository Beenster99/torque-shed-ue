#include "TorqueHUD.h"

#include "Coop/TorqueUi.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "TorqueBikePawn.h"
#include "Shed/TorqueGeometryMode.h"
#include "Shed/TorqueWalker.h"
#include "Coop/TorqueMenuPawn.h"
#include "Coop/TorqueSession.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueKeys.h"
#include "HAL/PlatformTime.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "GameFramework/PlayerState.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "TorqueInfoPanel.h"

void ATorqueHUD::DrawHUD()
{
	Super::DrawHUD();
	ATorquePlayerController* TPC = Cast<ATorquePlayerController>(GetOwningPlayerController());
	if (ATorqueMenuPawn* Menu = Cast<ATorqueMenuPawn>(GetOwningPawn()))
	{
		// The start menu, or its Settings over it.
		if (TPC && TPC->IsMenuUp()) TPC->DrawMenu(this, Canvas);
		else Menu->DrawMenu(this, Canvas);
		return;
	}
	if (TPC && TPC->IsMenuUp())
	{
		TPC->DrawMenu(this, Canvas);   // the pause menu: nothing else over it
		return;
	}
	if (ATorqueGeometryMode* Geo = ATorqueGeometryMode::For(GetOwningPlayerController()))
	{
		Geo->DrawPanel(this, Canvas);   // geometry mode's panel, parts list and gizmo
		return;
	}
	if (TPC) TPC->DrawPings(Canvas);   // [ping] under everything else on the HUD, over the world
	if (TPC) TPC->DrawNote(this, Canvas);
	DrawFrameRate();
	DrawRiderNames();
	// Hosting: what to tell friends, for the first minute (then it's in the Esc menu).
	if (const UTorqueSession* Session = UTorqueSession::Get(this); Session && Canvas && GEngine && GetWorld() &&
		Session->IsHosting() && GetWorld()->GetTimeSeconds() < 60.0)
	{
		const float HS = TorqueUi::Scale(Canvas), Px = 17.0f * HS;
		float HY = 90.0f * HS;
		TArray<FString> Lines = Session->JoinInstructions();
		Lines.Add(TEXT("(Esc shows this again)"));
		for (const FString& L : Lines)
		{
			const float W = (float)TorqueUi::Measure(L, Px).X;
			const float HX = Canvas->ClipX - W - 40.0f * HS;
			TorqueUi::Rect(Canvas, HX - 10.0f * HS, HY - 4.0f * HS, W + 20.0f * HS, Px * 1.2f + 8.0f * HS, TorqueUi::Panel);
			TorqueUi::Text(Canvas, L, HX, HY, Px, TorqueUi::Ink);
			HY += Px * 1.2f + 10.0f * HS;
		}
	}
	// Hosting with EOS: the code friends join with, top right.
	if (const UTorqueSession* Session = UTorqueSession::Get(this); Session && !Session->FriendCode.IsEmpty() && Canvas && GEngine)
	{
		const float CS = TorqueUi::Scale(Canvas), Px = 20.0f * CS;
		const FString Code = FString::Printf(TEXT("FRIEND CODE  %s"), *Session->FriendCode);
		const float W = (float)TorqueUi::Measure(Code, Px, true).X;
		const float CX = Canvas->ClipX - W - 40.0f * CS, CY = 36.0f * CS;
		TorqueUi::Rect(Canvas, CX - 12.0f * CS, CY - 6.0f * CS, W + 24.0f * CS, Px * 1.2f + 12.0f * CS, TorqueUi::Panel);
		TorqueUi::Rect(Canvas, CX - 12.0f * CS, CY - 6.0f * CS, 3.0f * CS, Px * 1.2f + 12.0f * CS, TorqueUi::Accent);
		TorqueUi::Text(Canvas, Code, CX, CY, Px, TorqueUi::Accent, true);
	}
	const ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetOwningPawn());
	const ATorqueWalker* Walker = Cast<ATorqueWalker>(GetOwningPawn());   // on foot in the shed
	if ((!Bike && !Walker) || !Canvas || !GEngine) return;
	const float S = Canvas->ClipY / 1080.0f;     // the same size on screen at 1080p, 1440p and 4K
	float X = 40.0f * S, Y = 36.0f * S;

	// At the parts computer: its page, worked with the mouse (parts_computer.gd), and nothing else.
	if (Walker && Walker->AtComputer())
	{
		Walker->DrawComputer(this, Canvas);
		return;
	}
	// At the mixing bench: its card down the right, worked with the mouse (paint_bench.gd).   [shed]
	if (Walker && Walker->AtBench())
	{
		Walker->DrawBench(this, Canvas);
		return;
	}
	// [label] Writing on a shelving unit's label (label_dialog.gd): its box over the view.
	if (Walker && Walker->LabelBoxUp())
	{
		Walker->DrawLabelBox(Canvas);
		return;
	}
	if (Bike && FTorqueSettings::Get().bTelemetry) DrawTelemetry(Bike, S);
	if (Bike) DrawSpeedo(Bike);
	// Big speed and gear, then smaller lines under them.
	if (Walker && Walker->ShowsCrosshair())
	{
		DrawWalkerCues(Walker, S);
	}
	// The spray mini game's readout (spray_hud.gd): its card stands in for the side panel.
	const bool bSprayCard = Walker && Walker->SprayHudUp();
	if (bSprayCard) Walker->DrawSpray(this, Canvas);
	// The wash game's readout (wash_hud.gd), the same way.   [wash]
	const bool bWashCard = Walker && Walker->WashHudUp();
	if (bWashCard) Walker->DrawWash(this, Canvas);
	// The tyre fitting game's ring (hud.gd _draw_tyre_game), over the middle of the view.   [tyre]
	if (Walker && Walker->TyreHudUp()) Walker->DrawTyre(this, Canvas);
	const TArray<FHudLine>& Lines = Bike ? Bike->GetHudLines() : Walker->GetHudLines();
	if (Walker && !Walker->SprayControlsUp() && !bWashCard) DrawLookPanel(Lines, S);   // [shed] hud.gd's panel at the side and the message across the top
	for (const FHudLine& L : Lines)
	{
		if (L.Role != 0) continue;
		// The corner lines: crisp text on a dark plate (the first, biggest, in bold).
		const float Px = 22.0f * S * L.Size;
		const bool bBig = L.Size >= 1.5f;
		const float W = (float)TorqueUi::Measure(L.Text, Px, bBig).X, H = Px * 1.2f;
		TorqueUi::Rect(Canvas, X - 12.0f * S, Y - 5.0f * S, W + 24.0f * S, H + 10.0f * S, FLinearColor(0.035f, 0.04f, 0.045f, 0.62f));
		TorqueUi::Text(Canvas, L.Text, X, Y, Px, FLinearColor(L.Colour), bBig);
		Y += H + (L.Size < 0.8f ? 7.0f : 14.0f) * S;   // the small help lines sit closer together
	}
	// [info] Riding: the info panel down the left, under the corner lines (F1 slides it in and out).
	if (Bike)
	{
		if (!InfoPanel.IsValid()) InfoPanel = MakeShared<FTorqueInfoPanel>();
		InfoPanel->Draw(this, Canvas, Bike, Y + 8.0f * S, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f);
	}
}

void ATorqueHUD::DrawLookPanel(const TArray<FHudLine>& Lines, float S)
{
	// hud.gd: what you're looking at goes in a panel at the right, out of the way of the crosshair
	// (a dark box, an accent edge on its left): its title, what it is, then what the buttons do. A
	// message (what just happened, or why not) across the top. Crisp text (TorqueUi), not the large
	// bitmap font scaled up, which drew it soft and fuzzy (beenster, Nov 10: "remove any fuzzy tooltip
	// help which has been ported over from torque shed").
	if (!Canvas) return;
	// The panel: 406 px wide at 1080p, 24 px in from the right, about the screen's middle.
	const float PW = 406.0f * S, Margin = 14.0f * S, TextW = PW - 2.0f * Margin - 3.0f * S;
	struct FDrawn { FString Text; FLinearColor Colour; float Px; bool bBold; float Gap; };
	TArray<FDrawn> Panel;
	for (const FHudLine& L : Lines)
	{
		if (L.Role < 1 || L.Role > 3) continue;
		const float Px = S * (L.Role == 1 ? 19.0f : (L.Role == 2 ? (L.Size >= 0.8f ? 17.0f : 14.0f) : 15.0f));
		const bool bBold = L.Role == 1 || (L.Role == 2 && L.Size >= 0.8f);
		const FLinearColor Colour = L.Role == 2 && L.Size < 0.8f ? TorqueUi::Dim : FLinearColor(L.Colour);
		for (const FString& W : TorqueUi::WrapLines(L.Text, Px, TextW, bBold)) Panel.Add({W, Colour, Px, bBold, 0.0f});
		if (Panel.Num() > 0) Panel.Last().Gap = 6.0f * S;
	}
	if (Panel.Num() > 0)
	{
		float H = 2.0f * Margin;
		for (const FDrawn& D : Panel) H += D.Px * 1.25f + D.Gap;
		const float X = Canvas->ClipX - 24.0f * S - PW, Y = FMath::Max(80.0f * S, Canvas->ClipY * 0.5f - 170.0f * S);
		TorqueUi::Rect(Canvas, X, Y, PW, H, TorqueUi::Panel);
		TorqueUi::Rect(Canvas, X, Y, 3.0f * S, H, TorqueUi::Accent);
		float TY = Y + Margin;
		for (const FDrawn& D : Panel)
		{
			TorqueUi::Text(Canvas, D.Text, X + 3.0f * S + Margin, TY, D.Px, D.Colour, D.bBold);
			TY += D.Px * 1.25f + D.Gap;
		}
	}
	for (const FHudLine& L : Lines)
	{
		if (L.Role != 4) continue;
		const float Px = 21.0f * S;
		float Y = 40.0f * S;
		for (const FString& W : TorqueUi::WrapLines(L.Text, Px, 1000.0f * S, true))
		{
			const float TW = (float)TorqueUi::Measure(W, Px, true).X;
			const float X = (Canvas->ClipX - TW) * 0.5f;
			TorqueUi::Rect(Canvas, X - 12.0f * S, Y - 4.0f * S, TW + 24.0f * S, Px * 1.2f + 8.0f * S, FLinearColor(0.035f, 0.04f, 0.045f, 0.62f));
			TorqueUi::Text(Canvas, W, X, Y, Px, FLinearColor(L.Colour), true);
			Y += Px * 1.2f + 12.0f * S;
		}
	}
}


void ATorqueHUD::DrawRiderNames()
{
	// Co-op: each other rider's name over their head, with how far away they are.
	if (!Canvas || !GEngine || !GetWorld() || GetNetMode() == NM_Standalone) return;
	const APawn* Mine = GetOwningPawn();
	const APlayerController* PC = GetOwningPlayerController();
	FVector Eye = FVector::ZeroVector;
	FRotator EyeRot;
	if (PC) PC->GetPlayerViewPoint(Eye, EyeRot);
	UFont* Font = GEngine->GetLargeFont();
	const float Sc = Canvas->ClipY / 1080.0f;
	int32 Others = 0, NoState = 0, Behind = 0, Drawn = 0;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const APawn* P = *It;
		if (P == Mine || P->IsA<ATorqueMenuPawn>()) continue;
		++Others;
		const APlayerState* PS = P->GetPlayerState();
		if (!PS)
		{
			++NoState;
			continue;
		}
		// Over the head: the bike's rider, or a walker's capsule top (plus a hand's width).
		const double Up = P->IsA<ATorqueBikePawn>() ? 185.0 : P->GetSimpleCollisionHalfHeight() + 35.0;
		const FVector Head = P->GetActorLocation() + FVector(0.0, 0.0, Up);
		const FVector Ahead = PC ? EyeRot.Vector() : FVector::ForwardVector;
		if (FVector::DotProduct(Head - Eye, Ahead) <= 10.0)
		{
			++Behind;   // behind the camera
			continue;
		}
		const FVector Screen = Project(Head, false);
		++Drawn;
		const double Metres = FVector::Dist(Eye, Head) / 100.0;
		const FString Text = Metres < 30.0 ? PS->GetPlayerName() : FString::Printf(TEXT("%s  %.0f m"), *PS->GetPlayerName(), Metres);
		// Big enough to read at a glance (and in a small window): as the HUD's main lines.
		const float K = Sc * (Metres < 30.0 ? 1.5f : Metres < 100.0 ? 1.2f : 1.0f);
		float W = 0, H = 0;
		Canvas->StrLen(Font, Text, W, H);
		const float X = Screen.X - 0.5f * W * K, Y = Screen.Y - H * K;
		DrawRect(FLinearColor(0, 0, 0, 0.35f), X - 6.0f * Sc, Y - 3.0f * Sc, W * K + 12.0f * Sc, H * K + 6.0f * Sc);
		// The name in the colour they chose (Settings > Character); pale for the dark ones.
		const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(PS);
		FColor NameColour(255, 230, 150);
		if (TPS)
		{
			const FColor C = TPS->ColourValue();
			NameColour = (C.R + C.G + C.B) < 200 ? FColor(220, 220, 220) : C;
		}
		Canvas->SetDrawColor(NameColour);
		Canvas->DrawText(Font, Text, X, Y, K, K);
		// In the shed, what they're doing: "fitting clubman bars to bay 2".
		if (TPS && !TPS->Activity.IsEmpty() && Metres < 40.0)
		{
			const float AK = K * 0.7f;
			float AW = 0, AH = 0;
			Canvas->StrLen(Font, TPS->Activity, AW, AH);
			const float AX = Screen.X - 0.5f * AW * AK, AY = Y + H * K + 6.0f * Sc;
			DrawRect(FLinearColor(0, 0, 0, 0.3f), AX - 5.0f * Sc, AY - 2.0f * Sc, AW * AK + 10.0f * Sc, AH * AK + 4.0f * Sc);
			Canvas->SetDrawColor(FColor(200, 230, 200));
			Canvas->DrawText(Font, TPS->Activity, AX, AY, AK, AK);
		}
	}
	// Once every 10 s in co-op, so a test can tell why a name tag isn't there.
	NameLogClock -= GetWorld()->GetDeltaSeconds();
	if (NameLogClock <= 0.0f)
	{
		NameLogClock = 10.0f;
		UE_LOG(LogTemp, Log, TEXT("Torque net: name tags: %d drawn of %d other players (%d without a player state yet, %d behind the camera)"),
			Drawn, Others, NoState, Behind);
	}
}

namespace TorqueHudCues
{
// Torque Shed's UITheme.ACCENT.
const FLinearColor Accent(1.0f, 0.62f, 0.2f);
}

void ATorqueHUD::DrawWalkerCues(const ATorqueWalker* Walker, float S)
{
	// Torque Shed's hud.gd, sized from its 1080p pixels: the large font is about 20 px high there.
	// (Crisp text, TorqueUi, at the same sizes: the scaled bitmap font drew it fuzzy.)
	if (!Walker || !Canvas) return;
	const FLinearColor Accent = TorqueHudCues::Accent;
	auto Px = [S](float Size) { return S * Size / 20.0f; };
	auto Text = [this](const FString& T, float X, float Y, float Scale, const FLinearColor& C)
	{
		TorqueUi::Text(Canvas, T, X, Y, Scale * 20.0f, C);
	};
	auto Measure = [](const FString& T, float Scale)
	{
		return TorqueUi::Measure(T, Scale * 20.0f);
	};
	auto Frame = [this](float X, float Y, float W, float H, const FLinearColor& C, float T)
	{
		DrawRect(C, X, Y, W, T);
		DrawRect(C, X, Y + H - T, W, T);
		DrawRect(C, X, Y, T, H);
		DrawRect(C, X + W - T, Y, T, H);
	};

	// The dot in the middle, lit when it's over something you can work on (_draw_crosshair).
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	const float D = 4.0f * S;
	DrawRect(FLinearColor(0, 0, 0, 0.5f), CX - D, CY - D, 2.0f * D, 2.0f * D);
	const FLinearColor Dot = Walker->HasTarget() ? Accent : FLinearColor(1, 1, 1, 0.8f);
	DrawRect(Dot, CX - 0.5f * D, CY - 0.5f * D, D, D);
	// The work ring: how far through the job you are, from the top round clockwise.
	const float Done = Walker->PumpFilling() ? 0.0f : Walker->WorkFraction();   // [fluids] no ring pumping petrol
	if (Done > 0.0f)
	{
		const float R = 18.0f * S, Width = 5.0f * S;
		const int32 Steps = 40;
		for (int32 I = 0; I < Steps; ++I)
		{
			const float A0 = -UE_PI * 0.5f + 2.0f * UE_PI * I / Steps, A1 = -UE_PI * 0.5f + 2.0f * UE_PI * (I + 1) / Steps;
			DrawLine(CX + R * FMath::Cos(A0), CY + R * FMath::Sin(A0), CX + R * FMath::Cos(A1), CY + R * FMath::Sin(A1),
				FLinearColor(0, 0, 0, 0.4f), Width);
		}
		const int32 Lit = FMath::Max(1, FMath::RoundToInt(Steps * Done));
		for (int32 I = 0; I < Lit; ++I)
		{
			const float A0 = -UE_PI * 0.5f + 2.0f * UE_PI * Done * I / Lit, A1 = -UE_PI * 0.5f + 2.0f * UE_PI * Done * (I + 1) / Lit;
			DrawLine(CX + R * FMath::Cos(A0), CY + R * FMath::Sin(A0), CX + R * FMath::Cos(A1), CY + R * FMath::Sin(A1), Accent, Width);
		}
	}

	// The tool bar along the bottom: a numbered box for each tool, the one in hand lit (_draw_toolbar).
	static const TCHAR* Names[] = {TEXT("Hands"), TEXT("Wrench"), TEXT("Brush"), TEXT("Grinder"), TEXT("Pressure wash"), TEXT("Spray gun")};
	static_assert(UE_ARRAY_COUNT(Names) == TorqueToolBarCount, "a name for each tool on the bar (TorqueToolBar)");
	const int32 N = UE_ARRAY_COUNT(Names);
	const float Gap = 8.0f * S;
	const float BW = FMath::Min(120.0f * S, (Canvas->ClipX - 40.0f * S) / N - Gap), BH = 46.0f * S;
	const float X0 = CX - N * (BW + Gap) * 0.5f + Gap * 0.5f;
	const float BarY = Canvas->ClipY - 70.0f * S;
	for (int32 I = 0; I < N; ++I)
	{
		const bool bOn = I == Walker->ToolSlot();
		const float BX = X0 + I * (BW + Gap);
		DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, bOn ? 0.75f : 0.45f), BX, BarY, BW, BH);
		if (bOn) Frame(BX, BarY, BW, BH, Accent, 2.0f * S);
		Text(FString::FromInt(I + 1), BX + 8.0f * S, BarY + 4.0f * S, Px(13.0f), FLinearColor(1, 1, 1, 0.5f));
		const FString Label = Names[I];
		const FVector2D L = Measure(Label, Px(16.0f));
		Text(Label, BX + 0.5f * (BW - L.X), BarY + BH - L.Y - 5.0f * S, Px(16.0f), bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.65f));
	}

	// The spray gun's colours and loads above the bar while it's in your hand (_draw_spray_picker).
	const bool bSprayTool = Walker->ToolIndex() == (int32)ETorqueTool::Gun;
	if (bSprayTool)
	{
		const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
		const float Sw = 30.0f * S, SwGap = 6.0f * S;
		const float Total = FMath::Max(1, Paints.Num()) * (Sw + SwGap) - SwGap;
		const float PX0 = CX - Total * 0.5f;
		const float PY = BarY - 120.0f * S;
		DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, 0.7f), PX0 - 14.0f * S, PY - 32.0f * S, Total + 28.0f * S, 140.0f * S);
		const int32 Pick = Walker->SprayColourIndex();
		FString Heading = TEXT("SPRAY GUN  -  ");
		if (Walker->SprayIsPaint()) Heading += UTorqueBuildSubsystem::PaintName(Pick).ToUpper() + TEXT("  ");
		Heading += Walker->SprayFinishText().ToUpper();
		Text(Heading, PX0, PY - 28.0f * S, Px(15.0f), Accent);
		for (int32 I = 0; I < Paints.Num(); ++I)
		{
			float SX = PX0 + I * (Sw + SwGap), SY = PY, SS = Sw;
			if (I == Pick)
			{
				SX -= 4.0f * S;
				SY -= 4.0f * S;
				SS += 8.0f * S;
				Frame(SX - 2.0f * S, SY - 2.0f * S, SS + 4.0f * S, SS + 4.0f * S, FLinearColor::White, 2.0f * S);
			}
			FLinearColor C = Paints[I];
			C.A = 1.0f;
			DrawRect(C, SX, SY, SS, SS);
		}
		// What it's loaded with (T): the finishes, then primer and the coats, in rows across the panel.
		float FX = PX0, FY = PY + 42.0f * S;
		const TArray<FString> Loads = Walker->SprayLoadNames();
		for (int32 I = 0; I < Loads.Num(); ++I)
		{
			const FString& Label = Loads[I];
			const FVector2D L = Measure(Label, Px(15.0f));
			const float FW = L.X + 16.0f * S, FH = 24.0f * S;
			if (FX > PX0 && FX + FW > PX0 + Total)
			{
				FX = PX0;
				FY += FH + 6.0f * S;
			}
			const bool bOn = I == Walker->SprayLoadIndex();
			DrawRect(FLinearColor(1, 1, 1, bOn ? 0.18f : 0.05f), FX, FY, FW, FH);
			if (bOn) Frame(FX, FY, FW, FH, Accent, 2.0f * S);
			Text(Label, FX + 8.0f * S, FY + 0.5f * (FH - L.Y), Px(15.0f), bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.6f));
			FX += FW + 6.0f * S;
		}
	}

	// What's in your hands, over the bar.
	const FString Carrying = Walker->CarryingText();
	if (!Carrying.IsEmpty())
	{
		const FString Line = TEXT("Carrying: ") + Carrying;
		const FVector2D L = Measure(Line, Px(17.0f));
		const float LY = BarY - 10.0f * S - L.Y - (bSprayTool ? 150.0f * S : 0.0f);
		DrawRect(FLinearColor(0, 0, 0, 0.35f), CX - 0.5f * L.X - 8.0f * S, LY - 2.0f * S, L.X + 16.0f * S, L.Y + 4.0f * S);
		Text(Line, CX - 0.5f * L.X, LY, Px(17.0f), Accent);
	}
}

// Settings > Graphics > Show the frame rate: at the top of the screen, in the middle, averaged over
// half a second so it can be read.
void ATorqueHUD::DrawFrameRate()
{
	const UWorld* World = GetWorld();
	if (!FTorqueSettings::Get().bFpsCounter || !Canvas || !GEngine || !World) return;
	FpsTime += (float)FApp::GetDeltaTime();
	++FpsFrames;
	if (FpsTime >= 0.5f)
	{
		FpsShown = FpsFrames / FpsTime;
		FpsTime = 0.0f;
		FpsFrames = 0;
	}
	const float Sc = TorqueUi::Scale(Canvas), Px = 17.0f * Sc;
	const FString Fps = FString::Printf(TEXT("%.0f FPS"), FpsShown);
	const float W = (float)TorqueUi::Measure(Fps, Px, true).X;
	const float FX = 0.5f * (Canvas->ClipX - W), FY = 8.0f * Sc;
	TorqueUi::Rect(Canvas, FX - 10.0f * Sc, FY - 3.0f * Sc, W + 20.0f * Sc, Px * 1.2f + 6.0f * Sc, TorqueUi::Panel);
	TorqueUi::Text(Canvas, Fps, FX, FY, Px, FpsShown >= 59.0f ? FLinearColor(0.55f, 1.0f, 0.55f, 1.0f) : FpsShown >= 40.0f ? TorqueUi::Accent : TorqueUi::Warn, true);
}

// Settings > Gameplay > Telemetry overlay (F3): Torque Shed's telemetry_overlay.gd down the right of
// the screen. At the top the speed, gear and revs; then each tyre (its friction circle: the ring is
// its grip, the dot how much of it is used, red past it), its load, its suspension, its tread and
// carcass temperature and its disc; beside them the throttle and brakes, the geometry on the springs
// and the engine, oil and air; under it all the last twelve seconds as strips (beenster, Nov 12:
// "tyre temps, engine temps, and more details ... like with Torque Shed").
void ATorqueHUD::DrawTelemetry(const ATorqueBikePawn* Bike, float S)
{
	const TArray<FTorqueTelemetry>& Samples = Bike->GetTelemetry();
	if (Samples.Num() < 2 || !Canvas || !GEngine) return;
	const FTorqueSettings& Set = FTorqueSettings::Get();
	const FTorqueTelemetry& Now = Samples.Last();
	const FLinearColor FrontCol(0.45f, 0.75f, 1.0f), RearCol(1.0f, 0.62f, 0.3f), Over(1.0f, 0.3f, 0.25f), Cold(0.6f, 0.82f, 1.0f);
	const FLinearColor Ink = TorqueUi::Ink, Dim = TorqueUi::Dim;
	const float PW = 420.0f * S, X0 = Canvas->ClipX - PW - 24.0f * S, Top = 64.0f * S;
	const float Bottom = Canvas->ClipY - 250.0f * S;   // clear of the speedo in the corner
	if (Bottom - Top < 200.0f * S) return;
	TorqueUi::Rect(Canvas, X0, Top, PW, Bottom - Top, FLinearColor(0.02f, 0.025f, 0.035f, 0.78f));
	TorqueUi::Rect(Canvas, X0, Top, 3.0f * S, Bottom - Top, TorqueUi::Accent);
	const float Px = 13.0f * S, Row = 16.0f * S;
	float Y = Top + 8.0f * S;
	TorqueUi::Text(Canvas, TEXT("TELEMETRY"), X0 + 12.0f * S, Y, 15.0f * S, TorqueUi::Accent, true);
	TorqueUi::Text(Canvas, FString::Printf(TEXT("%s  ·  gear %s  ·  %d rpm"), *Set.SpeedText(Now.Speed),
		Now.Gear <= 0 ? TEXT("N") : *FString::FromInt(Now.Gear), FMath::RoundToInt(Now.Rpm / 50.0f) * 50), X0 + 118.0f * S, Y + 1.0f * S, 14.0f * S, Ink);
	Y += 26.0f * S;

	// The two tyres.
	const float D = 96.0f * S, ColW = 124.0f * S;
	for (int32 WI = 0; WI < 2; ++WI)
	{
		const float CX = X0 + 14.0f * S + WI * ColW + D * 0.5f, CY = Y + 16.0f * S + D * 0.5f, Rad = D * 0.5f - 4.0f * S;
		const FLinearColor Col = WI == 0 ? FrontCol : RearCol;
		TorqueUi::Text(Canvas, WI == 0 ? TEXT("FRONT") : TEXT("REAR"), CX - Rad, Y, 12.0f * S, Dim, true);
		// The ring (the grip at this load), half-way ring, and the dot: how much is used, pointing
		// the way the tyre's working (sideways from the slip angle, along from the slip ratio).
		const int32 Seg = 40;
		for (int32 K = 0; K < Seg; ++K)
		{
			const float A0 = 2.0f * PI * K / Seg, A1 = 2.0f * PI * (K + 1) / Seg;
			Canvas->K2_DrawLine(FVector2D(CX + Rad * FMath::Cos(A0), CY + Rad * FMath::Sin(A0)), FVector2D(CX + Rad * FMath::Cos(A1), CY + Rad * FMath::Sin(A1)),
				1.5f, FLinearColor(1, 1, 1, 0.35f));
			Canvas->K2_DrawLine(FVector2D(CX + 0.5f * Rad * FMath::Cos(A0), CY + 0.5f * Rad * FMath::Sin(A0)),
				FVector2D(CX + 0.5f * Rad * FMath::Cos(A1), CY + 0.5f * Rad * FMath::Sin(A1)), 1.0f, FLinearColor(1, 1, 1, 0.1f));
		}
		Canvas->K2_DrawLine(FVector2D(CX - Rad, CY), FVector2D(CX + Rad, CY), 1.0f, FLinearColor(1, 1, 1, 0.08f));
		Canvas->K2_DrawLine(FVector2D(CX, CY - Rad), FVector2D(CX, CY + Rad), 1.0f, FLinearColor(1, 1, 1, 0.08f));
		const float Used = Now.GripUsed[WI];
		FVector2D Dir(FMath::Clamp(Now.SlipAngle[WI] / 6.0f, -1.0f, 1.0f), -FMath::Clamp(Now.SlipRatio[WI] / 0.12f, -1.0f, 1.0f));
		if (Dir.SizeSquared() < 1e-4f) Dir = FVector2D(0.0f, 0.0f);
		else Dir.Normalize();
		const float Reach = FMath::Min(Used, 1.15f) * Rad;
		TorqueUi::Disc(Canvas, CX + (float)Dir.X * Reach, CY + (float)Dir.Y * Reach, 5.0f * S, Used > 1.0f ? Over : Col, 16);
		float LY = CY + Rad + 8.0f * S;
		TorqueUi::Text(Canvas, Now.bContact[WI] ? FString::Printf(TEXT("%.1f kN  grip %d%%"), Now.LoadN[WI] / 1000.0f, FMath::RoundToInt(Used * 100.0f)) : FString(TEXT("in the air")),
			CX - Rad, LY, Px, Used > 1.0f ? Over : Ink);
		LY += Row;
		TorqueUi::Text(Canvas, Now.bBottomed[WI] ? FString(TEXT("BOTTOMED")) : FString::Printf(TEXT("%s %d/%d mm"), WI == 0 ? TEXT("fork") : TEXT("shock"),
			FMath::RoundToInt(Now.SuspMm[WI]), FMath::RoundToInt(Now.TravelMm[WI])), CX - Rad, LY, Px, Now.bBottomed[WI] ? Over : Dim);
		LY += Row;
		const FLinearColor TyreCol = Now.TyreBand[WI] > 0 ? Over : Now.TyreBand[WI] < 0 ? Cold : Ink;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("tyre %d/%d°C"), FMath::RoundToInt(Now.TyreSurface[WI]), FMath::RoundToInt(Now.TyreCarcass[WI])),
			CX - Rad, LY, Px, TyreCol);
		LY += Row;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("disc %d°C"), FMath::RoundToInt(Now.DiscC[WI])), CX - Rad, LY, Px,
			Now.DiscC[WI] > 500.0f ? Over : Dim);
	}
	// The throttle and brakes as bars, then the geometry and the engine's heat.
	{
		const float BX = X0 + 14.0f * S + 2.0f * ColW, BW = X0 + PW - 12.0f * S - BX;
		float BY = Y + 4.0f * S;
		const struct { const TCHAR* Name; float V; FLinearColor C; } Bars[] = {
			{TEXT("Throttle"), Now.Throttle, FLinearColor(0.45f, 1.0f, 0.45f)}, {TEXT("Front brake"), Now.FrontBrake, FrontCol}, {TEXT("Rear brake"), Now.RearBrake, RearCol}};
		for (const auto& Bar : Bars)
		{
			TorqueUi::Text(Canvas, Bar.Name, BX, BY, 12.0f * S, Dim);
			TorqueUi::Rect(Canvas, BX, BY + 15.0f * S, BW, 7.0f * S, FLinearColor(1, 1, 1, 0.08f));
			TorqueUi::Rect(Canvas, BX, BY + 15.0f * S, BW * FMath::Clamp(Bar.V, 0.0f, 1.0f), 7.0f * S, Bar.C);
			BY += 28.0f * S;
		}
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Pitch %+.1f°  rake %.1f°"), Now.PitchDeg, Now.RakeDeg), BX, BY, 12.0f * S, Dim);
		BY += 15.0f * S;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Trail %d mm (%d standing)"), FMath::RoundToInt(Now.TrailMm), FMath::RoundToInt(Now.TrailStandMm)), BX, BY, 12.0f * S, Dim);
		BY += 15.0f * S;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Steer %+.1f°  lean %+.0f°"), Now.Steer, Now.Lean), BX, BY, 12.0f * S, Dim);
		BY += 19.0f * S;
		const FLinearColor EngCol = Now.EngineBand > 0 ? Over : Now.EngineBand < 0 ? Cold : Ink;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("%s %d°C%s"), Now.bLiquid ? TEXT("Coolant") : TEXT("Head"), FMath::RoundToInt(Now.EngineC),
			Now.bFan ? TEXT("  fan") : TEXT("")), BX, BY, 13.0f * S, EngCol, true);
		BY += 16.0f * S;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Oil %d°C  ·  air %d°C"), FMath::RoundToInt(Now.OilC), FMath::RoundToInt(Now.AirC)), BX, BY, 12.0f * S, Dim);
		BY += 15.0f * S;
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Road %d°C%s"), FMath::RoundToInt(Now.RoadC),
			Now.PowerMult < 0.995f ? *FString::Printf(TEXT("  ·  power %d%%"), FMath::RoundToInt(Now.PowerMult * 100.0f)) : TEXT("")), BX, BY, 12.0f * S,
			Now.PowerMult < 0.995f ? Over : Dim);
	}
	Y += 16.0f * S + D + 4.0f * Row + 14.0f * S;

	// The strips: the last twelve seconds, front and rear together where there are two.
	struct FStrip { const TCHAR* Name; float Lo, Hi; bool bTwo; };
	const float TopSpeed = Set.Units == 0 ? 220.0f / 3.6f : 140.0f / 2.23694f;
	const FStrip Strips[] = {
		{TEXT("Speed"), 0.0f, TopSpeed, false},
		{TEXT("Revs"), 0.0f, 12000.0f, false},
		{TEXT("Lean (+ right)"), -60.0f, 60.0f, false},
		{TEXT("Steering (+ right)"), -10.0f, 10.0f, false},
		{TEXT("Slip ratio"), -0.2f, 0.2f, true},
		{TEXT("Grip used (1 = the limit)"), 0.0f, 1.2f, true},
		{TEXT("Suspension travel used"), 0.0f, 1.0f, true},
		{TEXT("Tyre temperature °C"), 0.0f, 120.0f, true},
		{TEXT("Brake temperature °C"), 0.0f, 600.0f, true},
		{TEXT("Engine temperature °C"), 0.0f, 130.0f, false},
	};
	const int32 NStrips = UE_ARRAY_COUNT(Strips);
	const float Gap = 4.0f * S;
	const float SH = FMath::Min(46.0f * S, (Bottom - 8.0f * S - Y - Gap * NStrips) / NStrips);
	if (SH < 18.0f * S) return;
	const float SX = X0 + 10.0f * S, SW = PW - 20.0f * S;
	auto ValueOf = [](int32 Which, int32 WI, const FTorqueTelemetry& T) -> float {
		switch (Which)
		{
		case 0: return T.Speed;
		case 1: return T.Rpm;
		case 2: return T.Lean;
		case 3: return T.Steer;
		case 4: return T.SlipRatio[WI];
		case 5: return T.GripUsed[WI];
		case 6: return T.TravelMm[WI] > 0.0f ? T.SuspMm[WI] / T.TravelMm[WI] : 0.0f;
		case 7: return T.TyreC[WI];
		case 8: return T.DiscC[WI];
		default: return T.EngineC;
		}
	};
	const int32 N = Samples.Num();
	for (int32 I = 0; I < NStrips; ++I)
	{
		const FStrip& St = Strips[I];
		TorqueUi::Rect(Canvas, SX, Y, SW, SH, FLinearColor(0, 0, 0, 0.35f));
		if (St.Lo < 0.0f) TorqueUi::Rect(Canvas, SX, Y + SH * (St.Hi / (St.Hi - St.Lo)), SW, 1.0f, FLinearColor(1, 1, 1, 0.18f));
		if (I == 5) TorqueUi::Rect(Canvas, SX, Y + SH - SH * (1.0f - St.Lo) / (St.Hi - St.Lo), SW, 1.0f, FLinearColor(1.0f, 0.3f, 0.25f, 0.5f));   // the limit
		for (int32 WI = 0; WI < (St.bTwo ? 2 : 1); ++WI)
		{
			const FLinearColor Col = !St.bTwo ? (I == 9 ? FLinearColor(1.0f, 0.5f, 0.35f) : FLinearColor(0.9f, 0.9f, 0.9f)) : WI == 0 ? FrontCol : RearCol;
			for (int32 K = 1; K < N; ++K)
			{
				const float Xa = SX + SW * (K - 1) / 119.0f, Xb = SX + SW * K / 119.0f;
				const float Ya = Y + SH - SH * FMath::Clamp((ValueOf(I, WI, Samples[K - 1]) - St.Lo) / (St.Hi - St.Lo), 0.0f, 1.0f);
				const float Yb = Y + SH - SH * FMath::Clamp((ValueOf(I, WI, Samples[K]) - St.Lo) / (St.Hi - St.Lo), 0.0f, 1.0f);
				Canvas->K2_DrawLine(FVector2D(Xa, Ya), FVector2D(Xb, Yb), 1.5f, Col);
			}
		}
		FString Value;
		switch (I)
		{
		case 0: Value = Set.SpeedText(Now.Speed); break;
		case 1: Value = FString::Printf(TEXT("%.0f"), Now.Rpm); break;
		case 2: Value = FString::Printf(TEXT("%+.0f°"), Now.Lean); break;
		case 3: Value = FString::Printf(TEXT("%+.1f°"), Now.Steer); break;
		case 4: Value = FString::Printf(TEXT("%+.2f / %+.2f"), Now.SlipRatio[0], Now.SlipRatio[1]); break;
		case 5: Value = FString::Printf(TEXT("%.2f / %.2f"), Now.GripUsed[0], Now.GripUsed[1]); break;
		case 6: Value = FString::Printf(TEXT("%d / %d mm"), FMath::RoundToInt(Now.SuspMm[0]), FMath::RoundToInt(Now.SuspMm[1])); break;
		case 7: Value = FString::Printf(TEXT("%.0f / %.0f"), Now.TyreC[0], Now.TyreC[1]); break;
		case 8: Value = FString::Printf(TEXT("%.0f / %.0f"), Now.DiscC[0], Now.DiscC[1]); break;
		default: Value = FString::Printf(TEXT("%.0f"), Now.EngineC); break;
		}
		TorqueUi::Text(Canvas, FString::Printf(TEXT("%s   %s"), St.Name, *Value), SX + 5.0f * S, Y + 2.0f * S, 11.5f * S, Dim);
		Y += SH + Gap;
	}
}

// [dash] hud.gd _draw_speedo: "The speedo shows what the bike's dashboard can: every dash the speed,
// gear, trip, fuel economy and range; most the revs; a TFT or race logger a shift light." A dark
// box in the bottom right corner, in Torque Shed's own pixels (its window is 1600 x 900, stretched
// to the screen's height), its text on the same baselines.
void ATorqueHUD::DrawSpeedo(const ATorqueBikePawn* Bike)
{
	if (!Bike || !Canvas || !GEngine) return;
	const FTorqueDash& D = Bike->GetDash();
	const FTorqueSettings& Set = FTorqueSettings::Get();
	const bool bMetric = Set.Units == 0;
	const float G = TorqueUi::Scale(Canvas) * 1080.0f / 900.0f;   // screen pixels per Godot pixel
	const float W = Canvas->ClipX / G, H = Canvas->ClipY / G;     // the screen in Godot pixels
	const FLinearColor Accent(1.0f, 0.62f, 0.2f, 1.0f);           // UITheme.ACCENT
	auto Dim = [](float A) { return FLinearColor(1.0f, 1.0f, 1.0f, A); };
	auto Box = [this, G](float X, float Y, float BW, float BH, const FLinearColor& C) { TorqueUi::Rect(Canvas, X * G, Y * G, BW * G, BH * G, C); };
	// Godot draws a string on its baseline; Roboto's ascent is 0.93 of its size.
	auto Say = [this, G](const FString& Str, float X, float Base, float Size, const FLinearColor& C, TorqueUi::EAlign Align = TorqueUi::EAlign::Left) {
		TorqueUi::Text(Canvas, Str, X * G, (Base - 0.93f * Size) * G, Size * G, C, false, Align);
	};
	auto SpeedValue = [bMetric](double Mps) { return FMath::RoundToInt(FMath::Abs(Mps) * (bMetric ? 3.6 : 2.23694)); };
	auto Distance = [bMetric](double Km, int32 Decimals) {
		const double V = bMetric ? Km : Km / 1.609344;
		return (Decimals > 0 ? FString::Printf(TEXT("%.1f"), V) : FString::Printf(TEXT("%.0f"), V)) + (bMetric ? TEXT(" km") : TEXT(" miles"));
	};
	const TCHAR* SpeedUnit = bMetric ? TEXT("km/h") : TEXT("mph");

	const float RX = W - 250.0f, RY = H - 196.0f;   // Rect2(size.x - 250, size.y - 196, 226, 172)
	Box(RX, RY, 226.0f, 172.0f, FLinearColor(0.03f, 0.03f, 0.04f, 0.6f));
	Say(FString::FromInt(SpeedValue(D.Speed)), RX + 12.0f, RY + 58.0f, 50.0f, FLinearColor::White);
	Say(SpeedUnit, RX + 120.0f, RY + 34.0f, 15.0f, Dim(0.6f));
	if (D.Has(TEXT("gear"))) Say(D.Gear == 0 ? FString(TEXT("GEAR N")) : FString::Printf(TEXT("GEAR %d"), D.Gear), RX + 120.0f, RY + 60.0f, 20.0f, Accent);
	if (D.Has(TEXT("top_speed"))) Say(FString::Printf(TEXT("TOP %d"), SpeedValue(D.TopKmh / 3.6)), RX + 170.0f, RY + 34.0f, 12.0f, Dim(0.6f));
	const float Frac = (float)FMath::Clamp(D.Rpm / FMath::Max(D.Redline, 1.0), 0.0, 1.0);
	if (D.Has(TEXT("rpm")))
	{
		Box(RX + 12.0f, RY + 74.0f, 202.0f, 8.0f, Dim(0.12f));
		Box(RX + 12.0f, RY + 74.0f, 202.0f * Frac, 8.0f, Frac < 0.92f ? Accent : FLinearColor(1.0f, 0.2f, 0.15f));
		// The revs as a number under the bar, to the nearest 50.
		Say(FString::Printf(TEXT("%d rpm"), FMath::RoundToInt(D.Rpm / 50.0) * 50), RX + 214.0f, RY + 93.0f, 11.0f, Dim(0.6f), TorqueUi::EAlign::Right);
	}
	if (D.Has(TEXT("shift_light")) && Frac > 0.88f)
	{
		// draw_circle(r.position + (206, 18), 8): yellow, red past 94%.
		const FLinearColor Lamp = Frac > 0.94f ? FLinearColor(1.0f, 0.15f, 0.1f) : FLinearColor(1.0f, 0.85f, 0.1f);
		const float CX = (RX + 206.0f) * G, CY = (RY + 18.0f) * G, Rad = 8.0f * G;
		for (int32 Row = -FMath::CeilToInt(Rad); Row <= FMath::CeilToInt(Rad); ++Row)
		{
			const float Half = FMath::Sqrt(FMath::Max(Rad * Rad - (float)(Row * Row), 0.0f));
			if (Half > 0.0f) TorqueUi::Rect(Canvas, CX - Half, CY + Row, 2.0f * Half, 1.0f, Lamp);
		}
	}
	// Fuel gauge and how far this ride's petrol has gone.
	const double Fuel = FMath::Clamp(D.FuelLitres / FMath::Max(D.FuelCapacity, 0.1), 0.0, 1.0);
	Say(TEXT("FUEL"), RX + 12.0f, RY + 106.0f, 13.0f, Dim(0.6f));
	Box(RX + 52.0f, RY + 96.0f, 162.0f, 10.0f, Dim(0.12f));
	Box(RX + 52.0f, RY + 96.0f, 162.0f * (float)Fuel, 10.0f, Fuel < 0.15 ? FLinearColor(1.0f, 0.3f, 0.2f) : FLinearColor(0.9f, 0.85f, 0.3f));
	const double KmPerL = D.TripEconomy();
	FString Economy;
	if (KmPerL <= 0.0) Economy = bMetric ? TEXT("-- L/100 km") : TEXT("-- mpg");
	else Economy = bMetric ? FString::Printf(TEXT("%.1f L/100 km"), 100.0 / KmPerL) : FString::Printf(TEXT("%d mpg"), FMath::RoundToInt(KmPerL / 1.609344 * 4.54609));
	FString Line = FString::Printf(TEXT("%.1f L"), D.FuelLitres);
	if (D.Has(TEXT("economy"))) Line += TEXT("  \u00B7  ") + Economy;
	Say(Line, RX + 12.0f, RY + 130.0f, 15.0f, Dim(0.8f));
	// The trip and the range left: the trip computer the dash has.
	TArray<FString> Bits;
	if (D.Has(TEXT("trip"))) Bits.Add(TEXT("TRIP ") + Distance(D.TripKm, 1));
	if (D.Has(TEXT("range"))) Bits.Add(TEXT("RANGE ") + Distance(Bike->DashRangeKm(), 0));
	Say(FString::Join(Bits, TEXT("   ")), RX + 12.0f, RY + 152.0f, 13.0f, Dim(0.7f));
	// The low fuel light, blinking.
	if (Fuel < 0.15 && ((int32)(FPlatformTime::Seconds() * 2.0)) % 2 == 0)
		Say(TEXT("LOW FUEL"), RX + 140.0f, RY + 106.0f, 13.0f, FLinearColor(1.0f, 0.65f, 0.1f));
	DrawTellTales(Bike, RX, RY, G);   // [lights]
	// [Ride UK stage 7] The sat nav over the speedo: the next turn as an arrow, how far, onto what; the
	// way left to the finish; the next fuel or cafe.
	if (D.bNav)
	{
		const float NY = RY - 96.0f;
		Box(RX, NY, 226.0f, 88.0f, FLinearColor(0.03f, 0.03f, 0.04f, 0.6f));
		if (D.bNavOff)
		{
			Say(TEXT("OFF ROUTE"), RX + 12.0f, NY + 32.0f, 20.0f, Accent);
			Say(TEXT("Back to the ride's road"), RX + 12.0f, NY + 54.0f, 13.0f, Dim(0.7f));
		}
		else
		{
			// (the arrow: up the stem, then off the way the road goes; a flag at the finish)
			const FVector2D Base((RX + 30.0f) * G, (NY + 64.0f) * G), Elbow((RX + 30.0f) * G, (NY + 40.0f) * G);
			const float Thick = 4.0f * G;
			if (D.NavTurn == 9)
			{
				Canvas->K2_DrawLine(Base, FVector2D(Base.X, (NY + 14.0f) * G), Thick, FLinearColor::White);
				Box(RX + 30.0f, NY + 14.0f, 18.0f, 12.0f, Accent);
			}
			else
			{
				const float Ang = FMath::DegreesToRadians(45.0f * (float)D.NavTurn);
				const FVector2D Dir(FMath::Sin(Ang), -FMath::Cos(Ang));
				const FVector2D Tip = Elbow + Dir * (22.0f * G);
				Canvas->K2_DrawLine(Base, Elbow, Thick, FLinearColor::White);
				Canvas->K2_DrawLine(Elbow, Tip, Thick, FLinearColor::White);
				const FVector2D Side(-Dir.Y, Dir.X);
				Canvas->K2_DrawLine(Tip, Tip - Dir * (9.0f * G) + Side * (8.0f * G), Thick, FLinearColor::White);
				Canvas->K2_DrawLine(Tip, Tip - Dir * (9.0f * G) - Side * (8.0f * G), Thick, FLinearColor::White);
			}
			Say(FTorqueDash::NavDistance(D.NavTo, bMetric), RX + 62.0f, NY + 30.0f, 22.0f, FLinearColor::White);
			const FString What = D.NavTurn == 9 ? (D.NavOnto.IsEmpty() ? FString(TEXT("Finish")) : TEXT("Finish: ") + D.NavOnto)
				: FTorqueDash::NavWords(D.NavTurn) + (D.NavOnto.IsEmpty() ? FString() : TEXT(", ") + D.NavOnto);
			Say(What.Left(30), RX + 62.0f, NY + 50.0f, 13.0f, Dim(0.8f));
			Say(FString::Printf(TEXT("%s to go"), *FTorqueDash::NavDistance(D.NavLeft, bMetric)), RX + 62.0f, NY + 66.0f, 12.0f, Dim(0.6f));
		}
		if (!D.NavStop.IsEmpty()) Say(D.NavStop.Left(36), RX + 12.0f, NY + 82.0f, 12.0f, FLinearColor(0.75f, 0.9f, 1.0f, 0.85f));
	}
	// What to press, along the bottom: starting, or stopped.
	FString Hint;
	if (Bike->IsStarting()) Hint = TEXT("Starting...");
	else if (D.bAtFuel && FMath::Abs(D.Speed) < 0.3)
		Hint = D.FuelLitres >= D.FuelCapacity - 0.05 ? FString(TEXT("At the pumps: the tank's full")) : FString::Printf(TEXT("At the pumps: filling up (%.1f L)"), D.FuelLitres);
	else if (FMath::Abs(D.Speed) < 2.0 && !D.bCrashed)
		Hint = FString::Printf(TEXT("%s: %s   \u00B7   Esc: menu"), *FTorqueKeys::Text(ETorqueKey::BackToShed),
			Bike->InYard() ? TEXT("park and walk back in") : TEXT("back to the shed"));   // [yard]
	if (!Hint.IsEmpty()) Say(Hint, W * 0.5f, H - 40.0f, 16.0f, Dim(0.75f), TorqueUi::EAlign::Centre);
}

void ATorqueHUD::DrawTellTales(const ATorqueBikePawn* Bike, float RX, float RY, float G)
{
	const FTorqueLights& L = Bike->GetLights();
	const bool bHead = L.HeadOn(), bMain = L.MainOn(), bFogOn = L.FogOn(), bInd = L.Indicating();
	if (!bHead && !bInd) return;
	const FLinearColor Green(0.3f, 1.0f, 0.4f), DarkGreen(0.3f, 1.0f, 0.4f, 0.15f), Blue(0.3f, 0.55f, 1.0f);
	// Godot pixels to the screen's.
	auto P = [G](float X, float Y) { return FVector2D(X * G, Y * G); };
	// A ray: a bar T thick from (X0, Y0) to (X1, Y1).
	auto Ray = [this, &P](float X0, float Y0, float X1, float Y1, float T, const FLinearColor& C) {
		TorqueUi::Polygon(Canvas, {P(X0, Y0 - T), P(X1, Y1 - T), P(X1, Y1 + T), P(X0, Y0 + T)}, C);
	};
	// The headlamp symbol: the lamp's D (its flat face to the left) and its beams out of that face,
	// level for main beam, dipped down for dipped; the fog lamp's with a wavy line through its beams.
	auto Lamp = [this, &P, &Ray](float CX, float CY, bool bLevel, bool bFogLamp, const FLinearColor& C) {
		TArray<FVector2D> D;
		D.Add(P(CX - 1.0f, CY));
		for (int32 I = 0; I <= 12; ++I)
		{
			const float A = UE_PI * (-0.5f + I / 12.0f);
			D.Add(P(CX - 1.0f + 7.5f * FMath::Cos(A), CY + 8.0f * FMath::Sin(A)));
		}
		TorqueUi::Polygon(Canvas, D, C);
		for (int32 I = 0; I < 4; ++I)
		{
			const float RayY = CY - 6.0f + 4.0f * I;
			Ray(CX - 4.0f, RayY, CX - 14.0f, bLevel ? RayY : RayY + 3.5f, 0.9f, C);
		}
		if (bFogLamp)
			for (int32 I = 0; I < 4; ++I)
				Ray(CX - 9.0f + (I % 2 ? 1.5f : -1.5f), CY - 8.0f + 4.0f * I, CX - 9.0f + (I % 2 ? -1.5f : 1.5f), CY - 4.0f + 4.0f * I, 0.8f, C);
	};
	// What's lit, right to left from over the speedo's right edge.
	const float Y = RY - 16.0f;
	float X = RX + 226.0f;
	const float Pad = 6.0f;
	const float W = (bInd ? 80.0f : 0.0f) + (bHead ? 34.0f : 0.0f) + (bFogOn ? 34.0f : 0.0f);
	TorqueUi::Rect(Canvas, (X - W - Pad) * G, (Y - 13.0f) * G, (W + Pad) * G, 26.0f * G, FLinearColor(0.03f, 0.03f, 0.04f, 0.6f));
	if (bInd)
	{
		// hud.gd _draw_indicator_lamp: an arrow each way, the side indicating flashing with the lamps.
		const float MidX = X - 43.0f;
		for (int32 Sd : {-1, 1})
		{
			const float Tx = MidX + 28.0f * Sd;
			TorqueUi::Polygon(Canvas, {P(Tx, Y), P(Tx - 11.0f * Sd, Y - 9.0f), P(Tx - 11.0f * Sd, Y - 3.5f), P(Tx - 22.0f * Sd, Y - 3.5f),
				P(Tx - 22.0f * Sd, Y + 3.5f), P(Tx - 11.0f * Sd, Y + 3.5f), P(Tx - 11.0f * Sd, Y + 9.0f)},
				(Sd < 0 ? L.LeftLit() : L.RightLit()) ? Green : DarkGreen);
		}
		X -= 80.0f;
	}
	if (bFogOn)
	{
		Lamp(X - 12.0f, Y, false, true, Green);
		X -= 34.0f;
	}
	if (bHead) Lamp(X - 12.0f, Y, bMain, false, bMain ? Blue : Green);
}
