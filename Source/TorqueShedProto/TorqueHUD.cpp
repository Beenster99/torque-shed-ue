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
}

void ATorqueHUD::DrawLookPanel(const TArray<FHudLine>& Lines, float S)
{
	// hud.gd: what you're looking at goes in a panel at the right, out of the way of the crosshair
	// (a dark box, an accent edge on its left): its title, what it is, then what the buttons do. A
	// message (what just happened, or why not) across the top.
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font || !Canvas) return;
	float LW = 0.0f, LH = 0.0f;
	Canvas->StrLen(Font, TEXT("Hg"), LW, LH);
	auto Wrap = [this, Font](const FString& Text, float Scale, float MaxW)
	{
		TArray<FString> Out, Words;
		Text.ParseIntoArray(Words, TEXT(" "), true);
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			float TW = 0.0f, TH = 0.0f;
			Canvas->StrLen(Font, Try, TW, TH);
			if (!Line.IsEmpty() && TW * Scale > MaxW) { Out.Add(Line); Line = Word; }
			else Line = Try;
		}
		if (!Line.IsEmpty()) Out.Add(Line);
		return Out;
	};
	// The panel: 406 px wide at 1080p, 24 px in from the right, about the screen's middle.
	const float PW = 406.0f * S, Margin = 14.0f * S, TextW = PW - 2.0f * Margin - 3.0f * S;
	struct FDrawn { FString Text; FColor Colour; float Scale; float Gap; };
	TArray<FDrawn> Panel;
	for (const FHudLine& L : Lines)
	{
		if (L.Role < 1 || L.Role > 3) continue;
		const float Scale = S * (L.Role == 1 ? 0.85f : (L.Role == 2 ? 0.6f : 0.68f));
		const FColor Colour = L.Role == 2 ? FColor(220, 220, 220) : L.Colour;
		for (const FString& W : Wrap(L.Text, Scale, TextW)) Panel.Add({W, Colour, Scale, 0.0f});
		if (Panel.Num() > 0) Panel.Last().Gap = 6.0f * S;
	}
	if (Panel.Num() > 0)
	{
		float H = 2.0f * Margin;
		for (const FDrawn& D : Panel) H += LH * D.Scale + 2.0f * S + D.Gap;
		const float X = Canvas->ClipX - 24.0f * S - PW, Y = FMath::Max(80.0f * S, Canvas->ClipY * 0.5f - 170.0f * S);
		DrawRect(FLinearColor(0.04f, 0.05f, 0.07f, 0.72f), X, Y, PW, H);
		DrawRect(FLinearColor(1.0f, 0.75f, 0.35f, 0.5f), X, Y, 3.0f * S, H);
		float TY = Y + Margin;
		for (const FDrawn& D : Panel)
		{
			Canvas->SetDrawColor(D.Colour);
			Canvas->DrawText(Font, D.Text, X + 3.0f * S + Margin, TY, D.Scale, D.Scale);
			TY += LH * D.Scale + 2.0f * S + D.Gap;
		}
	}
	for (const FHudLine& L : Lines)
	{
		if (L.Role != 4) continue;
		const float Scale = S * 0.95f;
		float Y = 40.0f * S;
		for (const FString& W : Wrap(L.Text, Scale, 1000.0f * S))
		{
			float TW = 0.0f, TH = 0.0f;
			Canvas->StrLen(Font, W, TW, TH);
			const float X = (Canvas->ClipX - TW * Scale) * 0.5f;
			Canvas->SetDrawColor(FColor(0, 0, 0, 200));   // hud.gd's outline: a shadow round it
			for (const FVector2D& O : {FVector2D(2, 2), FVector2D(-2, 2), FVector2D(2, -2), FVector2D(-2, -2)})
				Canvas->DrawText(Font, W, X + O.X * S, Y + O.Y * S, Scale, Scale);
			Canvas->SetDrawColor(L.Colour);
			Canvas->DrawText(Font, W, X, Y, Scale, Scale);
			Y += TH * Scale + 4.0f * S;
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
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Walker || !Canvas || !Font) return;
	const FLinearColor Accent = TorqueHudCues::Accent;
	auto Px = [S](float Size) { return S * Size / 20.0f; };
	auto Text = [this, Font](const FString& T, float X, float Y, float Scale, const FLinearColor& C)
	{
		Canvas->SetDrawColor(C.ToFColor(true));
		Canvas->DrawText(Font, T, X, Y, Scale, Scale);
	};
	auto Measure = [this, Font](const FString& T, float Scale)
	{
		float W = 0, H = 0;
		Canvas->StrLen(Font, T, W, H);
		return FVector2D(W * Scale, H * Scale);
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
	const float Done = Walker->WorkFraction();
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

// Settings > Gameplay > Telemetry overlay (F3): Torque Shed's live strips of what the bike is doing,
// down the right of the screen, the last twelve seconds of each.
void ATorqueHUD::DrawTelemetry(const ATorqueBikePawn* Bike, float S)
{
	const TArray<FTorqueTelemetry>& Samples = Bike->GetTelemetry();
	if (Samples.Num() < 2 || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetSmallFont();
	const FTorqueSettings& Set = FTorqueSettings::Get();
	struct FStrip { const TCHAR* Name; float Lo, Hi; FLinearColor Colour; };
	const float TopSpeed = Set.Units == 0 ? 220.0f / 3.6f : 140.0f / 2.23694f;
	const FStrip Strips[] = {
		{TEXT("Speed"), 0.0f, TopSpeed, FLinearColor(0.55f, 0.85f, 1.0f)},
		{TEXT("Revs"), 0.0f, 10000.0f, FLinearColor(1.0f, 0.75f, 0.35f)},
		{TEXT("Throttle"), 0.0f, 1.0f, FLinearColor(0.45f, 1.0f, 0.45f)},
		{TEXT("Front brake"), 0.0f, 1.0f, FLinearColor(1.0f, 0.4f, 0.35f)},
		{TEXT("Rear brake"), 0.0f, 1.0f, FLinearColor(1.0f, 0.6f, 0.6f)},
		{TEXT("Lean"), -60.0f, 60.0f, FLinearColor(0.9f, 0.9f, 0.9f)},
	};
	const float W = 360.0f * S, H = 54.0f * S, Gap = 10.0f * S;
	const float X0 = Canvas->ClipX - W - 40.0f * S;
	float Y0 = 150.0f * S;
	const FTorqueTelemetry& Now = Samples.Last();
	for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Strips); ++I)
	{
		const FStrip& St = Strips[I];
		DrawRect(FLinearColor(0, 0, 0, 0.45f), X0, Y0, W, H);
		auto ValueOf = [I](const FTorqueTelemetry& T) -> float {
			switch (I)
			{
			case 0: return T.Speed;
			case 1: return T.Rpm;
			case 2: return T.Throttle;
			case 3: return T.FrontBrake;
			case 4: return T.RearBrake;
			default: return T.Lean;
			}
		};
		if (St.Lo < 0.0f) DrawRect(FLinearColor(1, 1, 1, 0.2f), X0, Y0 + H * 0.5f, W, 1.0f * S);   // the middle: upright
		const int32 N = Samples.Num();
		for (int32 K = 1; K < N; ++K)
		{
			const float Xa = X0 + W * (K - 1) / 119.0f, Xb = X0 + W * K / 119.0f;
			const float Ya = Y0 + H - H * FMath::Clamp((ValueOf(Samples[K - 1]) - St.Lo) / (St.Hi - St.Lo), 0.0f, 1.0f);
			const float Yb = Y0 + H - H * FMath::Clamp((ValueOf(Samples[K]) - St.Lo) / (St.Hi - St.Lo), 0.0f, 1.0f);
			Draw2DLine(FMath::RoundToInt(Xa), FMath::RoundToInt(Ya), FMath::RoundToInt(Xb), FMath::RoundToInt(Yb), St.Colour.ToFColor(true));
		}
		FString Value;
		switch (I)
		{
		case 0: Value = Set.SpeedText(Now.Speed); break;
		case 1: Value = FString::Printf(TEXT("%.0f rpm  gear %d"), Now.Rpm, Now.Gear); break;
		case 5: Value = FString::Printf(TEXT("%+.0f deg"), Now.Lean); break;
		default: Value = FString::Printf(TEXT("%.0f%%"), ValueOf(Now) * 100.0f); break;
		}
		Canvas->SetDrawColor(St.Colour.ToFColor(true));
		Canvas->DrawText(Font, FString::Printf(TEXT("%s  %s"), St.Name, *Value), X0 + 6.0f * S, Y0 + 3.0f * S, S * 1.1f, S * 1.1f);
		Y0 += H + Gap;
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
	// What to press, along the bottom: starting, or stopped.
	FString Hint;
	if (Bike->IsStarting()) Hint = TEXT("Starting...");
	else if (FMath::Abs(D.Speed) < 2.0 && !D.bCrashed)
		Hint = FString::Printf(TEXT("%s: %s   \u00B7   Esc: menu"), *FTorqueKeys::Text(ETorqueKey::BackToShed),
			Bike->InYard() ? TEXT("park and walk back in") : TEXT("back to the shed"));   // [yard]
	if (!Hint.IsEmpty()) Say(Hint, W * 0.5f, H - 40.0f, 16.0f, Dim(0.75f), TorqueUi::EAlign::Centre);
}
