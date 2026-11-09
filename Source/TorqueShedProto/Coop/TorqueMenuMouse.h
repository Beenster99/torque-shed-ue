// The mouse on the menus (the start menu, the pause menu and Settings), drawn on the canvas: each
// frame's drawing notes where its rows, arrows and tabs are, and the next frame's input finds what
// the pointer is over. Moving the mouse over a row picks it, a click chooses it (or steps a setting:
// its < and > arrows, or anywhere else on it for the next value), the wheel moves up and down, and
// the right button goes back.
#pragma once

#include "CoreMinimal.h"

struct FTorqueMenuHits
{
	enum EKind : uint8 { Row, Less, More, Tab, Badge, Slider };   // [profile] Badge: the profile picture, top right; Slider: a track
	struct FHit
	{
		FBox2f Box;
		int32 Index = 0;   // the row, or the tab
		EKind Kind = Row;
	};
	TArray<FHit> Hits;
	void Reset() { Hits.Reset(); }
	void Add(float X0, float Y0, float X1, float Y1, int32 Index, EKind Kind)
	{
		FHit H;
		H.Box = FBox2f(FVector2f(X0, Y0), FVector2f(X1, Y1));
		H.Index = Index;
		H.Kind = Kind;
		Hits.Add(H);
	}
	// What's under the pointer: arrows and tabs before the rows they sit on.
	const FHit* At(const FVector2D& P) const
	{
		const FVector2f Q((float)P.X, (float)P.Y);
		const FHit* Found = nullptr;
		for (const FHit& H : Hits)
			if (H.Box.IsInside(Q) && (!Found || H.Kind != Row)) Found = &H;
		return Found;
	}
};
