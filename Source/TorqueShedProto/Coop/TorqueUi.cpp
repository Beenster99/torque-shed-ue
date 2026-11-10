#include "TorqueUi.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "RenderUtils.h"
#include "Styling/CoreStyle.h"

namespace TorqueUi
{
float Scale(const UCanvas* Canvas)
{
	if (!Canvas || Canvas->ClipY <= 0.0f) return 1.0f;
	// The smaller of the two, so a narrow window still fits the 1920 x 1080 layout.
	return FMath::Max(FMath::Min(Canvas->ClipX / 1920.0f, Canvas->ClipY / 1080.0f), 0.3f);
}

FSlateFontInfo Font(float Px, bool bBold)
{
	// Slate's sizes are points at 96 dpi: a point is 4/3 of a pixel.
	const float Pt = FMath::Max(Px * 0.75f, 4.0f);
	// The engine's Roboto font object (cooked with the game, runtime cached), not Slate's own default
	// composite font: in a packaged game a canvas text item with the latter measures but draws nothing.
	const UFont* Roboto = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (Roboto && Roboto->FontCacheType == EFontCacheType::Runtime)
		return FSlateFontInfo(Roboto, (decltype(FSlateFontInfo::Size))Pt, bBold ? FName("Bold") : FName("Regular"));
	FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", 10);
	F.Size = (decltype(F.Size))Pt;
	return F;
}

FVector2D Measure(const FString& Str, float Px, bool bBold)
{
	if (Str.IsEmpty()) return FVector2D(0.0, Px * 1.2);
	if (FSlateApplication::IsInitialized())
		if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			const FVector2D Size = Renderer->GetFontMeasureService()->Measure(Str, Font(Px, bBold), 1.0f);
			return Size;
		}
	return FVector2D(Str.Len() * Px * 0.52, Px * 1.2);   // (no Slate: a guess)
}

float Text(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, const FLinearColor& Colour, bool bBold, EAlign Align, bool bShadow)
{
	if (!Canvas || Str.IsEmpty()) return 0.0f;
	const FVector2D Size = Measure(Str, Px, bBold);
	float At = X;
	if (Align == EAlign::Centre) At -= 0.5f * (float)Size.X;
	else if (Align == EAlign::Right) At -= (float)Size.X;
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(At), FMath::RoundToFloat(Y)), FText::FromString(Str), Font(Px, bBold), Colour);
	Item.BlendMode = SE_BLEND_Translucent;   // (as the spray HUD, which draws in the packaged game)
	if (bShadow)
	{
		const float Off = FMath::Max(1.0f, FMath::RoundToFloat(Px / 22.0f));
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * Colour.A), FVector2D(Off, Off));
	}
	Canvas->DrawItem(Item);
	return (float)Size.X;
}

float TextFit(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, float MaxW, const FLinearColor& Colour, bool bBold, EAlign Align)
{
	if (Str.IsEmpty() || MaxW <= 0.0f) return 0.0f;
	float W = (float)Measure(Str, Px, bBold).X;
	if (W <= MaxW) return Text(Canvas, Str, X, Y, Px, Colour, bBold, Align);
	// Smaller first (kept on the same middle line), then cut.
	const float Smaller = FMath::Max(Px * MaxW / W, Px * 0.7f);
	const float Y2 = Y + 0.5f * (Px - Smaller);
	FString Cut = Str;
	while (Cut.Len() > 1 && (float)Measure(Cut + (Cut.Len() < Str.Len() ? TEXT("...") : TEXT("")), Smaller, bBold).X > MaxW) Cut.LeftChopInline(1);
	if (Cut.Len() < Str.Len()) Cut = Cut.TrimEnd() + TEXT("...");
	return Text(Canvas, Cut, X, Y2, Smaller, Colour, bBold, Align);
}

TArray<FString> WrapLines(const FString& Str, float Px, float MaxW, bool bBold)
{
	TArray<FString> Lines, Words;
	Str.ParseIntoArray(Words, TEXT(" "), true);
	FString Cur;
	for (const FString& Word : Words)
	{
		const FString Next = Cur.IsEmpty() ? Word : Cur + TEXT(" ") + Word;
		if (!Cur.IsEmpty() && (float)Measure(Next, Px, bBold).X > MaxW)
		{
			Lines.Add(Cur);
			Cur = Word;
		}
		else
			Cur = Next;
	}
	if (!Cur.IsEmpty()) Lines.Add(Cur);
	return Lines;
}

float TextWrap(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, float MaxW, const FLinearColor& Colour, float LineGap)
{
	float At = Y;
	for (const FString& One : WrapLines(Str, Px, MaxW))
	{
		Text(Canvas, One, X, At, Px, Colour);
		At += Px * LineGap;
	}
	return At - Y;
}

void Rect(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour)
{
	if (!Canvas || W <= 0.0f || H <= 0.0f || Colour.A <= 0.0f) return;
	FCanvasTileItem Item(FVector2D(X, Y), FVector2D(W, H), Colour);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void Polygon(UCanvas* Canvas, const TArray<FVector2D>& Points, const FLinearColor& Colour)
{
	if (!Canvas || Points.Num() < 3 || Colour.A <= 0.0f) return;
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Points.Num() - 2);
	for (int32 I = 1; I + 1 < Points.Num(); ++I)
	{
		FCanvasUVTri T;
		T.V0_Pos = Points[0];     T.V0_Color = Colour; T.V0_UV = FVector2D(0, 0);
		T.V1_Pos = Points[I];     T.V1_Color = Colour; T.V1_UV = FVector2D(1, 0);
		T.V2_Pos = Points[I + 1]; T.V2_Color = Colour; T.V2_UV = FVector2D(1, 1);
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void Disc(UCanvas* Canvas, float CX, float CY, float R, const FLinearColor& Colour, int32 Segments)
{
	TArray<FVector2D> Points;
	Segments = FMath::Clamp(Segments, 6, 96);
	for (int32 I = 0; I < Segments; ++I)
	{
		const float A = 2.0f * UE_PI * I / Segments;
		Points.Add(FVector2D(CX + R * FMath::Cos(A), CY + R * FMath::Sin(A)));
	}
	Polygon(Canvas, Points, Colour);
}

void Frame(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour, float T)
{
	Rect(Canvas, X, Y, W, T, Colour);
	Rect(Canvas, X, Y + H - T, W, T, Colour);
	Rect(Canvas, X, Y + T, T, H - 2.0f * T, Colour);
	Rect(Canvas, X + W - T, Y + T, T, H - 2.0f * T, Colour);
}

void HGradient(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour, TFunctionRef<float(float)> AlphaAt,
	int32 Segments)
{
	if (!Canvas || W <= 0.0f || H <= 0.0f || Colour.A <= 0.0f) return;
	Segments = FMath::Clamp(Segments, 1, 512);
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Segments * 2);
	auto At = [&](int32 I) {
		FLinearColor C = Colour;
		C.A = Colour.A * FMath::Clamp(AlphaAt((float)I / Segments), 0.0f, 1.0f);
		return C;
	};
	FLinearColor CA = At(0);
	for (int32 I = 0; I < Segments; ++I)
	{
		const FLinearColor CB = At(I + 1);
		const float X0 = X + W * (float)I / Segments, X1 = X + W * (float)(I + 1) / Segments;
		FCanvasUVTri T0, T1;
		T0.V0_Pos = FVector2D(X0, Y);     T0.V0_Color = CA; T0.V0_UV = FVector2D(0, 0);
		T0.V1_Pos = FVector2D(X1, Y);     T0.V1_Color = CB; T0.V1_UV = FVector2D(1, 0);
		T0.V2_Pos = FVector2D(X1, Y + H); T0.V2_Color = CB; T0.V2_UV = FVector2D(1, 1);
		T1.V0_Pos = FVector2D(X0, Y);     T1.V0_Color = CA; T1.V0_UV = FVector2D(0, 0);
		T1.V1_Pos = FVector2D(X1, Y + H); T1.V1_Color = CB; T1.V1_UV = FVector2D(1, 1);
		T1.V2_Pos = FVector2D(X0, Y + H); T1.V2_Color = CA; T1.V2_UV = FVector2D(0, 1);
		Tris.Add(T0);
		Tris.Add(T1);
		CA = CB;
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void Backdrop(UCanvas* Canvas, float Alpha)
{
	if (!Canvas) return;
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	Rect(Canvas, 0, 0, W, H, FLinearColor(0.0f, 0.0f, 0.0f, Alpha * 0.75f));
	// A soft vignette, darker towards each side (as the 12 stacked bands it replaces, but smooth).
	const float Edge = W * 0.22f;
	const float Keep = 1.0f - Alpha * 0.06f;
	HGradient(Canvas, 0, 0, Edge, H, FLinearColor::Black, [Keep](float T) { return 1.0f - FMath::Pow(Keep, 12.0f * (1.0f - T)); });
	HGradient(Canvas, W - Edge, 0, Edge, H, FLinearColor::Black, [Keep](float T) { return 1.0f - FMath::Pow(Keep, 12.0f * T); });
}

float KeyHint(UCanvas* Canvas, const FString& Key, const FString& What, float X, float Y, float S)
{
	const float Px = 16.0f * S, Pad = 8.0f * S, H = 26.0f * S;
	const float KW = FMath::Max((float)Measure(Key, Px, true).X + 2.0f * Pad, H);
	Rect(Canvas, X, Y, KW, H, FLinearColor(1, 1, 1, 0.10f));
	Frame(Canvas, X, Y, KW, H, FLinearColor(1, 1, 1, 0.28f), FMath::Max(1.0f, S));
	Text(Canvas, Key, X + 0.5f * KW, Y + 0.5f * (H - Px * 1.2f), Px, Ink, true, EAlign::Centre, false);
	const float TW = Text(Canvas, What, X + KW + 10.0f * S, Y + 0.5f * (H - 17.0f * S * 1.2f), 17.0f * S, Dim);
	return KW + 10.0f * S + TW;
}

void Switch(UCanvas* Canvas, float X, float Y, float W, float H, bool bOn, bool bHot)
{
	const float T = FMath::Max(1.0f, H / 14.0f);
	Rect(Canvas, X, Y, W, H, bOn ? FLinearColor(Accent.R, Accent.G, Accent.B, bHot ? 0.95f : 0.75f) : FLinearColor(1, 1, 1, bHot ? 0.12f : 0.07f));
	if (!bOn) Frame(Canvas, X, Y, W, H, FLinearColor(1, 1, 1, bHot ? 0.45f : 0.25f), T);
	const float K = H - 6.0f * T;
	const float KX = bOn ? X + W - K - 3.0f * T : X + 3.0f * T;
	Rect(Canvas, KX, Y + 3.0f * T, K, K, bOn ? FLinearColor(0.06f, 0.05f, 0.04f, 1.0f) : (bHot ? Ink : Dim));
}

void Slider(UCanvas* Canvas, float X, float Y, float W, float Frac, bool bCentred, bool bHot, float S)
{
	const float F = FMath::Clamp(Frac, 0.0f, 1.0f);
	const float TH = 4.0f * S, TY = Y - 0.5f * TH;
	Rect(Canvas, X, TY, W, TH, FLinearColor(1, 1, 1, 0.14f));
	const float Knob = X + W * F;
	const float From = bCentred ? X + 0.5f * W : X;
	const FLinearColor Fill(Accent.R, Accent.G, Accent.B, bHot ? 1.0f : 0.7f);
	Rect(Canvas, FMath::Min(From, Knob), TY, FMath::Abs(Knob - From), TH, Fill);
	if (bCentred) Rect(Canvas, X + 0.5f * W - 1.0f * S, Y - 8.0f * S, 2.0f * S, 16.0f * S, FLinearColor(1, 1, 1, 0.35f));
	const float KW = 10.0f * S, KH = 22.0f * S;
	Rect(Canvas, Knob - 0.5f * KW, Y - 0.5f * KH, KW, KH, bHot ? Ink : FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
}
} // namespace TorqueUi
