#include "TorqueHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "TorqueBikePawn.h"

void ATorqueHUD::DrawHUD()
{
	Super::DrawHUD();
	const ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(GetOwningPawn());
	if (!Bike || !Canvas || !GEngine) return;
	UFont* Font = GEngine->GetLargeFont();
	const float S = Canvas->ClipY / 1080.0f;     // the same size on screen at 1080p, 1440p and 4K
	float X = 40.0f * S, Y = 36.0f * S;

	// Big speed and gear, then smaller lines under them.
	const TArray<FHudLine>& Lines = Bike->GetHudLines();
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
