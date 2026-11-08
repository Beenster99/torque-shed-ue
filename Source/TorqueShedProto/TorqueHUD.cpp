#include "TorqueHUD.h"

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
#include "EngineUtils.h"
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
	DrawRiderNames();
	// Hosting: what to tell friends, for the first minute (then it's in the Esc menu).
	if (const UTorqueSession* Session = UTorqueSession::Get(this); Session && Canvas && GEngine && GetWorld() &&
		Session->IsHosting() && GetWorld()->GetTimeSeconds() < 60.0)
	{
		UFont* F = GEngine->GetLargeFont();
		const float HS = Canvas->ClipY / 1080.0f * 0.75f;
		float HY = 90.0f * Canvas->ClipY / 1080.0f;
		TArray<FString> Lines = Session->JoinInstructions();
		Lines.Add(TEXT("(Esc shows this again)"));
		for (const FString& L : Lines)
		{
			float W = 0, H = 0;
			Canvas->StrLen(F, L, W, H);
			const float HX = Canvas->ClipX - W * HS - 50.0f * HS;
			DrawRect(FLinearColor(0, 0, 0, 0.35f), HX - 8.0f * HS, HY - 3.0f * HS, W * HS + 16.0f * HS, H * HS + 6.0f * HS);
			Canvas->SetDrawColor(FColor(170, 230, 255));
			Canvas->DrawText(F, L, HX, HY, HS, HS);
			HY += H * HS + 8.0f * HS;
		}
	}
	// Hosting with EOS: the code friends join with, top right.
	if (const UTorqueSession* Session = UTorqueSession::Get(this); Session && !Session->FriendCode.IsEmpty() && Canvas && GEngine)
	{
		UFont* CodeFont = GEngine->GetLargeFont();
		const float CS = Canvas->ClipY / 1080.0f;
		const FString Text = FString::Printf(TEXT("Friend code: %s"), *Session->FriendCode);
		float W = 0, H = 0;
		Canvas->StrLen(CodeFont, Text, W, H);
		const float CX = Canvas->ClipX - W * CS - 50.0f * CS, CY = 36.0f * CS;
		DrawRect(FLinearColor(0, 0, 0, 0.35f), CX - 10.0f * CS, CY - 4.0f * CS, W * CS + 20.0f * CS, H * CS + 8.0f * CS);
		Canvas->SetDrawColor(FColor(255, 220, 120));
		Canvas->DrawText(CodeFont, Text, CX, CY, CS, CS);
	}
	const ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetOwningPawn());
	const ATorqueWalker* Walker = Cast<ATorqueWalker>(GetOwningPawn());   // on foot in the shed
	if ((!Bike && !Walker) || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetLargeFont();
	const float S = Canvas->ClipY / 1080.0f;     // the same size on screen at 1080p, 1440p and 4K
	float X = 40.0f * S, Y = 36.0f * S;

	// Big speed and gear, then smaller lines under them.
	if (Walker && Walker->ShowsCrosshair())
	{
		DrawWalkerCues(Walker, S);
	}
	const TArray<FHudLine>& Lines = Bike ? Bike->GetHudLines() : Walker->GetHudLines();
	for (const FHudLine& L : Lines)
	{
		const float Scale = S * L.Size;
		float W = 0, H = 0;
		Canvas->StrLen(Font, L.Text, W, H);
		DrawRect(FLinearColor(0, 0, 0, 0.35f), X - 10.0f * S, Y - 4.0f * S, W * Scale + 20.0f * S, H * Scale + 8.0f * S);
		Canvas->SetDrawColor(L.Colour);
		Canvas->DrawText(Font, L.Text, X, Y, Scale, Scale);
		Y += H * Scale + 12.0f * S;
	}
	// At the mixing bench: the mix as it'd spray, beside the colour to match (paint_bench.gd's swatches).
	FLinearColor Mix, Want;
	bool bWant = false;
	if (Walker && Walker->BenchSwatches(Mix, bWant, Want))
	{
		auto AsShown = [](const FLinearColor& C) { return FLinearColor(FColor((uint8)(FMath::Clamp(C.R, 0.0f, 1.0f) * 255.0f),
			(uint8)(FMath::Clamp(C.G, 0.0f, 1.0f) * 255.0f), (uint8)(FMath::Clamp(C.B, 0.0f, 1.0f) * 255.0f))); };
		const float Sw = 150.0f * S, SX = Canvas->ClipX - 2.0f * Sw - 80.0f * S, SY = 60.0f * S;
		DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, 0.75f), SX - 14.0f * S, SY - 40.0f * S, 2.0f * Sw + 48.0f * S, Sw + 54.0f * S);
		DrawRect(AsShown(Mix), SX, SY, Sw, Sw);
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawText(Font, TEXT("THE MIX"), SX, SY - 32.0f * S, 0.8f * S, 0.8f * S);
		if (bWant)
		{
			DrawRect(AsShown(Want), SX + Sw + 20.0f * S, SY, Sw, Sw);
			Canvas->DrawText(Font, TEXT("TO MATCH"), SX + Sw + 20.0f * S, SY - 32.0f * S, 0.8f * S, 0.8f * S);
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
	static const TCHAR* Names[] = {TEXT("Hands"), TEXT("Wrench"), TEXT("Brush"), TEXT("Grinder"), TEXT("Spray can"), TEXT("Pressure wash"), TEXT("Spray gun")};
	const int32 N = UE_ARRAY_COUNT(Names);
	const float Gap = 8.0f * S;
	const float BW = FMath::Min(120.0f * S, (Canvas->ClipX - 40.0f * S) / N - Gap), BH = 46.0f * S;
	const float X0 = CX - N * (BW + Gap) * 0.5f + Gap * 0.5f;
	const float BarY = Canvas->ClipY - 70.0f * S;
	for (int32 I = 0; I < N; ++I)
	{
		const bool bOn = I == Walker->ToolIndex();
		const float BX = X0 + I * (BW + Gap);
		DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, bOn ? 0.75f : 0.45f), BX, BarY, BW, BH);
		if (bOn) Frame(BX, BarY, BW, BH, Accent, 2.0f * S);
		Text(FString::FromInt(I + 1), BX + 8.0f * S, BarY + 4.0f * S, Px(13.0f), FLinearColor(1, 1, 1, 0.5f));
		const FString Label = Names[I];
		const FVector2D L = Measure(Label, Px(16.0f));
		Text(Label, BX + 0.5f * (BW - L.X), BarY + BH - L.Y - 5.0f * S, Px(16.0f), bOn ? FLinearColor::White : FLinearColor(1, 1, 1, 0.65f));
	}

	// The spray can's or gun's colours and loads above the bar while it's in your hand (_draw_spray_picker).
	const bool bSprayTool = Walker->ToolIndex() == 4 || Walker->ToolIndex() == 6;
	if (bSprayTool)
	{
		const TArray<FLinearColor>& Paints = UTorqueBuildSubsystem::Paints();
		const float Sw = 30.0f * S, SwGap = 6.0f * S;
		const float Total = FMath::Max(1, Paints.Num()) * (Sw + SwGap) - SwGap;
		const float PX0 = CX - Total * 0.5f;
		const float PY = BarY - 120.0f * S;
		DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, 0.7f), PX0 - 14.0f * S, PY - 32.0f * S, Total + 28.0f * S, 140.0f * S);
		const int32 Pick = Walker->SprayColourIndex();
		FString Heading = Walker->ToolIndex() == 6 ? FString(TEXT("SPRAY GUN  -  ")) : FString(TEXT("SPRAY CAN  -  "));
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
