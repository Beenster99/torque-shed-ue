// The Route planner (Ride UK stage 4): a full-screen page of the pause menu where a journey on the
// real world's roads is put together before it is ridden.
//
// On the left, a relief map of the UK (UTorqueWorldSubsystem::AskMap draws it from the height
// tiles) with the towns named: drag to move it, the wheel to zoom, a click to drop a pin, a right
// click on a pin to take it away. On the right: a search of the UK's places (type, Enter or a click
// adds one), the pins in order, the style of ride (quickest, scenic, gravel, curvy), a round trip of
// a chosen length from the first pin, the famous roads, and a journey's code to copy, or a code, a
// map link or a GPX file (its text or its path) to paste with Ctrl+V. Once there are two pins the
// journey is planned on its own, and what it is like is shown (how far, the climb, the highest
// point, how twisty, about how long, the fuel); Ride sets off on it.
//
// Plain C++ owned by the player controller, drawn on the canvas with TorqueUi like the other menus.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

#include "World/WorldRideDiary.h"
#include "World/WorldRoute.h"
#include "World/WorldRouteMap.h"

#include <vector>

class APlayerController;
class UCanvas;
class UTorqueWorldSubsystem;

class FTorqueRoutePlanner
{
public:
	void Open(UTorqueWorldSubsystem* InWorld);
	// The keys and the mouse, each frame it is up; false once it is closed (Esc with nothing to clear).
	bool Tick(APlayerController* PC);
	void Draw(UCanvas* Canvas);
	// Called when Ride is chosen, for the owner to close its menus (the ride's level opens itself).
	TFunction<void()> OnRide;
	// [co-op] A host's: told the journey's code and name as it changes (for everyone's game to plan
	// the same road), and asked who is still planning it ("" when everyone is ready to set off).
	TFunction<void(const FString& Code, const FString& Name)> OnJourneyChanged;
	TFunction<FString()> WaitingFor;
	// The journey as it stands, and setting one (a co-op host's, a code): the map goes to it.
	const TorqueWorld::RouteRequest& Journey() const { return Want; }
	void SetJourney(const TorqueWorld::RouteRequest& NewJourney, const FString& Name);

private:
	TWeakObjectPtr<UTorqueWorldSubsystem> World;
	TorqueWorld::RouteRequest Want;
	FString Title;
	bool bNamed = false;                   // the title is a famous road's own, not made from the pins
	int32 Serial = 0;                      // goes up with every change to the journey
	int32 PlanFor = -1, PlanCountAt = 0;   // the change a preview was asked for, and the preview count then
	double ChangedAt = 0.0;
	bool bRideWhenPlanned = false;
	void Changed();
	bool PreviewGood() const;
	FString MadeTitle() const;

	// The map: the view, where it is on the screen (and how many screen pixels a picture's pixel is),
	// the picture shown and the view it was drawn for.
	TorqueWorld::Route::MapView View;
	FVector2D MapPos = FVector2D::ZeroVector, MapSize = FVector2D(1.0, 1.0);
	double PixelScale = 1.0;
	float UiScale = 1.0f;
	TStrongObjectPtr<UTexture2D> MapTex;
	TorqueWorld::Route::MapView TexView;
	bool bHaveTex = false, bViewDirty = true;
	double AskedAt = 0.0;
	FVector2D ToScreen(double Lat, double Lon) const;
	TorqueWorld::Geo ToPlace(const FVector2D& Screen) const;
	bool OnMap(const FVector2D& Screen) const;
	void FitTo(const std::vector<TorqueWorld::RoutePin>& Pins);
	void FitBox(double South, double North, double West, double East);
	// The names on the map (redone when the view moves).
	std::vector<const TorqueWorld::Route::Place*> Labels;
	double LabelLat = 0.0, LabelLon = 0.0, LabelZoom = -1.0;

	// The mouse: held on the map (a drag pans it; let go where it went down, a pin).
	FVector2D Mouse = FVector2D(-1.0, -1.0), DownAt = FVector2D::ZeroVector, LastDrag = FVector2D::ZeroVector;
	bool bDown = false, bDragged = false;
	struct FHit
	{
		FBox2D Box;
		TFunction<void()> Do;
	};
	TArray<FHit> Hits;
	bool Button(UCanvas* Canvas, float X, float Y, float W, float H, const FString& Label, bool bOn, bool bEnabled, TFunction<void()> Do, float TextPx = 0.0f);

	// The panel: the search (typed), its finds, which tab, the famous roads' scroll, round trips.
	FString Search, Searched;
	std::vector<TorqueWorld::Route::Place> Finds;
	int32 Tab = 0;   // 0 the journey, 1 the famous roads, 2 the diary
	int32 FamousFirst = 0;
	double LoopKm = 60.0;
	std::vector<TorqueWorld::RouteRequest> LoopWays;
	int32 LoopOn = -1;
	double LoopLat = 0.0, LoopLon = 0.0, LoopFor = 0.0;
	FString Said;
	double SaidUntil = 0.0;
	void Say(const FString& Text);

	// [stage 7] The ride diary (World/WorldRideDiary): the rides kept in Saved/RideDiary, newest first;
	// the one shown on the map (its road and its photos), and the photo open over the map.
	std::vector<TorqueWorld::DiaryRide> Diaries;
	int32 DiaryOn = -1, DiaryFirst = 0, PhotoOn = -1;
	TStrongObjectPtr<UTexture2D> PhotoTex;
	FString PhotoTexFile;
	void LoadDiary();
	void ShowDiary(int32 Index);
	void ShowPhoto(int32 Index);
	void DrawDiaryMap(UCanvas* Canvas, bool bPhoto);
	float DrawDiaryPanel(UCanvas* Canvas, float X0, float Y, float PW, float BottomY);

	void AddPin(double Lat, double Lon, const FString& Name);
	void RemovePin(int32 Index);
	void Paste(const FString& Text);
	void CopyCode();
	void NextLoop();
	void Ride();
	FString NameAt(double Lat, double Lon) const;
};
