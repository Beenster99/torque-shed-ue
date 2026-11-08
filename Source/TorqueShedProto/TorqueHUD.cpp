#include "TorqueHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "TorqueBikePawn.h"
#include "Shed/TorqueWalker.h"
#include "Coop/TorqueMenuPawn.h"
#include "Coop/TorqueSession.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorquePlayerState.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"

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
		// A small dot in the middle to point at parts with.
		const float D = 4.0f * S;
		DrawRect(FLinearColor(0, 0, 0, 0.5f), Canvas->ClipX * 0.5f - D, Canvas->ClipY * 0.5f - D, 2.0f * D, 2.0f * D);
		DrawRect(FLinearColor(1, 1, 1, 0.9f), Canvas->ClipX * 0.5f - 0.5f * D, Canvas->ClipY * 0.5f - 0.5f * D, D, D);
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
