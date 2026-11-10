// The game's look for menus and HUD text, drawn on the canvas: crisp text at any resolution (Slate's
// Roboto, rendered at the size it's shown, not a small bitmap font scaled up), one palette, and the
// pieces the menus are made of (panels, rows, switches, sliders, keycaps). Sizes are in pixels at
// 1080p; Scale() turns them into the screen's.
//
// Other screens (the shop, the parts computer, the spray booth's words) can draw with it too, so the
// whole game reads as one.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Templates/Function.h"

class UCanvas;

namespace TorqueUi
{
// The palette.
const FLinearColor Ink(0.94f, 0.94f, 0.92f, 1.0f);        // text
const FLinearColor Dim(0.62f, 0.64f, 0.66f, 1.0f);        // secondary text
const FLinearColor Faint(0.42f, 0.44f, 0.46f, 1.0f);      // disabled, hints
const FLinearColor Accent(0.96f, 0.63f, 0.17f, 1.0f);     // Torque Shed amber
const FLinearColor Panel(0.035f, 0.04f, 0.045f, 0.88f);   // panels
const FLinearColor PanelLight(1.0f, 1.0f, 1.0f, 0.045f);  // a row picked, a box
const FLinearColor Line(1.0f, 1.0f, 1.0f, 0.10f);         // dividers
const FLinearColor Warn(1.0f, 0.45f, 0.38f, 1.0f);

// Pixels per 1080p pixel on this canvas.
float Scale(const UCanvas* Canvas);

enum class EAlign : uint8 { Left, Centre, Right };

// The font at Px screen pixels (already scaled).
FSlateFontInfo Font(float Px, bool bBold = false);
// How big text is at Px screen pixels.
FVector2D Measure(const FString& Str, float Px, bool bBold = false);
// Text with its top at Y; returns its width. A gentle shadow keeps it readable over the world.
float Text(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, const FLinearColor& Colour, bool bBold = false,
	EAlign Align = EAlign::Left, bool bShadow = true);
// Text shrunk (down to 70%) to fit MaxW, then cut with "..." if it still doesn't.
float TextFit(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, float MaxW, const FLinearColor& Colour, bool bBold = false,
	EAlign Align = EAlign::Left);
// Text broken into lines no wider than MaxW.
TArray<FString> WrapLines(const FString& Str, float Px, float MaxW, bool bBold = false);
// Text wrapped to MaxW; returns the height used.
float TextWrap(UCanvas* Canvas, const FString& Str, float X, float Y, float Px, float MaxW, const FLinearColor& Colour, float LineGap = 1.35f);

void Rect(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour);
// A filled shape: Points round its edge, every one of them in sight of the first (a convex shape, or
// an arrow from its tip), drawn as a fan of triangles from the first.
void Polygon(UCanvas* Canvas, const TArray<FVector2D>& Points, const FLinearColor& Colour);
// A filled disc (Segments round).
void Disc(UCanvas* Canvas, float CX, float CY, float R, const FLinearColor& Colour, int32 Segments = 24);
// A rectangle's outline, T thick.
void Frame(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour, float T);
// A smooth left-to-right fade of Colour over X..X+W: AlphaAt(t), t 0 at the left edge to 1 at the
// right, times Colour.A. Drawn as one strip of float-precision vertex colours (Segments pieces, each
// interpolated on the GPU), so there are no bands like stacked see-through rectangles leave.
void HGradient(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& Colour, TFunctionRef<float(float)> AlphaAt,
	int32 Segments = 64);
// Darkens the world behind a menu, more at the edges.
void Backdrop(UCanvas* Canvas, float Alpha = 0.6f);
// A keycap ("Esc", "Enter", "Q") and the words after it, for the hints along the bottom; returns the width.
float KeyHint(UCanvas* Canvas, const FString& Key, const FString& What, float X, float Y, float S);
// An on/off switch, W x H at X, Y.
void Switch(UCanvas* Canvas, float X, float Y, float W, float H, bool bOn, bool bHot);
// A slider's track and knob, Frac 0..1 (from the left, or out from the middle when bCentred).
void Slider(UCanvas* Canvas, float X, float Y, float W, float Frac, bool bCentred, bool bHot, float S);
} // namespace TorqueUi
