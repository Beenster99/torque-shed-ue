#include "TorqueHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "TorqueBikePawn.h"
#include "Shed/TorqueWalker.h"
#include "Coop/TorqueMenuPawn.h"
#include "Coop/TorqueSession.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"

void ATorqueHUD::DrawHUD()
{
	Super::DrawHUD();
	if (ATorqueMenuPawn* Menu = Cast<ATorqueMenuPawn>(GetOwningPawn()))
	{
		Menu->DrawMenu(this, Canvas);
		return;
	}
	DrawRiderNames();
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
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const APawn* P = *It;
		if (P == Mine || P->IsA<ATorqueMenuPawn>()) continue;
		const APlayerState* PS = P->GetPlayerState();
		if (!PS) continue;
		const FVector Head = P->GetActorLocation() + FVector(0.0, 0.0, P->IsA<ATorqueBikePawn>() ? 185.0 : 110.0);
		const FVector Screen = Project(Head, true);
		if (Screen.Z <= 0.0) continue;   // behind the camera
		const double Metres = FVector::Dist(Eye, Head) / 100.0;
		const FString Text = Metres < 30.0 ? PS->GetPlayerName() : FString::Printf(TEXT("%s  %.0f m"), *PS->GetPlayerName(), Metres);
		const float K = Sc * (Metres < 100.0 ? 1.0f : 0.8f);
		float W = 0, H = 0;
		Canvas->StrLen(Font, Text, W, H);
		const float X = Screen.X - 0.5f * W * K, Y = Screen.Y - H * K;
		DrawRect(FLinearColor(0, 0, 0, 0.35f), X - 6.0f * Sc, Y - 3.0f * Sc, W * K + 12.0f * Sc, H * K + 6.0f * Sc);
		Canvas->SetDrawColor(FColor(255, 230, 150));
		Canvas->DrawText(Font, Text, X, Y, K, K);
	}
}
