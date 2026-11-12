#include "Coop/TorqueRoutePlanner.h"

#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueUi.h"
#include "World/TorqueWorldSubsystem.h"
#include "World/WorldFiles.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace TRPI
{
static std::string ToStd(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}
static FString ToF(const std::string& Text)
{
	return FString(UTF8_TO_TCHAR(Text.c_str()));
}
// The styles as the screen calls them, and what each does.
static const TCHAR* StyleLabel(int32 I)
{
	static const TCHAR* L[TorqueWorld::Route::StyleCount] = {TEXT("Quickest"), TEXT("Scenic"), TEXT("Gravel"), TEXT("Curvy")};
	return L[FMath::Clamp(I, 0, TorqueWorld::Route::StyleCount - 1)];
}
static const TCHAR* StyleAbout(int32 I)
{
	static const TCHAR* L[TorqueWorld::Route::StyleCount] = {TEXT("The quick way: main roads where they go"), TEXT("Quiet back roads, away from the big ones"),
		TEXT("Tracks and gravel where there are any"), TEXT("Bends and hills: no towns or dual carriageways where it can help it")};
	return L[FMath::Clamp(I, 0, TorqueWorld::Route::StyleCount - 1)];
}
static UTexture2D* MakeTexture(int32 W, int32 H, const TArray<uint8>& Bgra)
{
	if (W <= 0 || H <= 0 || Bgra.Num() != W * H * 4) return nullptr;
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
	T->SRGB = true;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Px = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!Px) return nullptr;
	FMemory::Memcpy(Px, Bgra.GetData(), Bgra.Num());
	Mip.BulkData.Unlock();
	T->UpdateResource();
	return T;
}
static const double KmSteps[] = {30.0, 45.0, 60.0, 80.0, 100.0, 150.0, 200.0};
} // namespace TRPI

void FTorqueRoutePlanner::Open(UTorqueWorldSubsystem* InWorld)
{
	World = InWorld;
	Tab = 0;
	Search.Reset();
	Searched.Reset();
	Finds.clear();
	bDown = bDragged = false;
	bViewDirty = true;
	if (Want.Pins.empty() && InWorld)
	{
		TorqueWorld::RouteRequest Last;
		FString LastTitle;
		if (InWorld->LastJourney(Last, LastTitle)) SetJourney(Last, LastTitle);
	}
	// (a co-op host's journey already up when the planner opens goes out to everyone too)
	if (OnJourneyChanged && Want.Pins.size() >= 2) OnJourneyChanged(TRPI::ToF(TorqueWorld::Route::CodeOf(Want)), Title);
	if (Want.Pins.empty())
	{
		// (the whole of Great Britain)
		View.Lat = 54.7;
		View.Lon = -3.2;
		View.Zoom = 5.4;
	}
}

void FTorqueRoutePlanner::SetJourney(const TorqueWorld::RouteRequest& NewJourney, const FString& Name)
{
	Want = NewJourney;
	if (!TorqueWorld::Route::IsStyle(Want.Mode)) Want.Mode = "main";
	Title = Name;
	bNamed = !Name.IsEmpty() && Name != MadeTitle();
	LoopOn = -1;
	FitTo(Want.Pins);
	Changed();
}

void FTorqueRoutePlanner::Changed()
{
	++Serial;
	bRideWhenPlanned = false;
	ChangedAt = FPlatformTime::Seconds();
	// (the journey as its code carries it, to the metre, so a friend's game plans the very same road)
	TorqueWorld::RouteRequest AsCoded;
	if (Want.Pins.size() >= 2 && TorqueWorld::Route::FromCode(TorqueWorld::Route::CodeOf(Want), AsCoded)) Want = AsCoded;
	if (!bNamed) Title = MadeTitle();
	if (OnJourneyChanged && Want.Pins.size() >= 2) OnJourneyChanged(TRPI::ToF(TorqueWorld::Route::CodeOf(Want)), Title);
}

bool FTorqueRoutePlanner::PreviewGood() const
{
	const UTorqueWorldSubsystem* W = World.Get();
	return W && PlanFor == Serial && W->PreviewCount() > PlanCountAt && W->PreviewPlan() && !W->IsPlanningRide();
}

FString FTorqueRoutePlanner::MadeTitle() const
{
	if (Want.Pins.empty()) return FString();
	const FString First = Want.Pins.front().Name.empty() ? FString(TEXT("the start")) : TRPI::ToF(Want.Pins.front().Name);
	const FString Last = Want.Pins.back().Name.empty() ? FString(TEXT("the finish")) : TRPI::ToF(Want.Pins.back().Name);
	const bool bLoop = Want.Pins.size() > 2 && TorqueWorld::Route::GroundMetres(Want.Pins.front().Lat, Want.Pins.front().Lon, Want.Pins.back().Lat, Want.Pins.back().Lon) < 200.0;
	if (bLoop) return FString::Printf(TEXT("Round trip from %s"), *First);
	if (Want.Pins.size() == 1) return First;
	return FString::Printf(TEXT("%s to %s"), *First, *Last);
}

FString FTorqueRoutePlanner::NameAt(double Lat, double Lon) const
{
	UTorqueWorldSubsystem* W = World.Get();
	if (!W) return FString();
	const TorqueWorld::Route::Place* P = W->Places().Near(Lat, Lon, 3000.0);
	if (P && TorqueWorld::Route::GroundMetres(Lat, Lon, P->Lat, P->Lon) < 8000.0) return TRPI::ToF(P->Name);
	return FString();
}

void FTorqueRoutePlanner::Say(const FString& Text)
{
	Said = Text;
	SaidUntil = FPlatformTime::Seconds() + 5.0;
}

void FTorqueRoutePlanner::AddPin(double Lat, double Lon, const FString& Name)
{
	if (int32(Want.Pins.size()) >= TorqueWorld::Route::MaxPinsAnyway)
	{
		Say(TEXT("That's as many pins as a journey takes."));
		return;
	}
	FString Called = Name.IsEmpty() ? NameAt(Lat, Lon) : Name;
	if (Called.IsEmpty()) Called = FString::Printf(TEXT("Pin %d"), int32(Want.Pins.size()) + 1);
	Want.Pins.push_back({Lat, Lon, TRPI::ToStd(Called.Left(TorqueWorld::Route::MaxName))});
	LoopOn = -1;
	Changed();
}

void FTorqueRoutePlanner::RemovePin(int32 Index)
{
	if (Index < 0 || Index >= int32(Want.Pins.size())) return;
	Want.Pins.erase(Want.Pins.begin() + Index);
	if (Want.Pins.empty()) bNamed = false;
	LoopOn = -1;
	Changed();
}

void FTorqueRoutePlanner::Paste(const FString& Text)
{
	using namespace TorqueWorld;
	const FString Clean = Text.TrimStartAndEnd().TrimQuotes();
	const std::string Raw = TRPI::ToStd(Clean);
	RouteRequest Got;
	if (Route::FromCode(Raw, Got))
	{
		SetJourney(Got, FString());
		Say(FString::Printf(TEXT("A journey's code: %d pins, %s."), int32(Got.Pins.size()), TRPI::StyleLabel(int32(std::find(Route::Styles, Route::Styles + Route::StyleCount, Got.Mode) - Route::Styles))));
		return;
	}
	// A GPX file: its text, or where it is.
	FString Gpx;
	if (Clean.Contains(TEXT("<gpx")))
		Gpx = Clean;
	else if (Clean.EndsWith(TEXT(".gpx"), ESearchCase::IgnoreCase) && FPaths::FileExists(Clean))
		FFileHelper::LoadFileToString(Gpx, *Clean);
	if (!Gpx.IsEmpty())
	{
		if (Route::FromGpx(TRPI::ToStd(Gpx), Got))
		{
			for (size_t I = 0; I < Got.Pins.size(); ++I)
				if (Got.Pins[I].Name.empty() && (I == 0 || I + 1 == Got.Pins.size())) Got.Pins[I].Name = TRPI::ToStd(NameAt(Got.Pins[I].Lat, Got.Pins[I].Lon));
			Got.Mode = Want.Mode;
			SetJourney(Got, FString());
			Say(FString::Printf(TEXT("A GPX file: its way as %d pins."), int32(Got.Pins.size())));
		}
		else
			Say(TEXT("That GPX file has no route or track in it."));
		return;
	}
	const std::vector<RoutePin> Linked = Route::PlacesInLink(Raw);
	if (!Linked.empty())
	{
		for (const RoutePin& P : Linked) AddPin(P.Lat, P.Lon, TRPI::ToF(P.Name));
		FitTo(Want.Pins);
		Say(Linked.size() == 1 ? FString(TEXT("A map link: its place is pinned.")) : FString::Printf(TEXT("A map link: its %d stops are pinned."), int32(Linked.size())));
		return;
	}
	// Anything else is a search.
	Search = Clean.Left(40);
}

void FTorqueRoutePlanner::CopyCode()
{
	if (Want.Pins.size() < 2)
	{
		Say(TEXT("A journey needs two pins before it has a code."));
		return;
	}
	const FString Code = TRPI::ToF(TorqueWorld::Route::CodeOf(Want));
	FPlatformApplicationMisc::ClipboardCopy(*Code);
	Say(Want.Pins.size() > size_t(TorqueWorld::Route::MaxPins)
		? FString::Printf(TEXT("Code copied (its first %d pins: a code carries no more)."), TorqueWorld::Route::MaxPins)
		: FString(TEXT("Code copied: paste it to a friend, here or in Torque Shed.")));
}

void FTorqueRoutePlanner::NextLoop()
{
	if (Want.Pins.empty())
	{
		Say(TEXT("Pin the start first: a round trip sets off from it."));
		return;
	}
	const TorqueWorld::RoutePin Start = Want.Pins.front();
	if (LoopWays.empty() || LoopLat != Start.Lat || LoopLon != Start.Lon || LoopFor != LoopKm)
	{
		LoopWays = TorqueWorld::Route::Loops(Start.Lat, Start.Lon, LoopKm, Want.Mode, 3);
		LoopLat = Start.Lat;
		LoopLon = Start.Lon;
		LoopFor = LoopKm;
		LoopOn = -1;
	}
	if (LoopWays.empty()) return;
	const int32 Next = (LoopOn + 1) % int32(LoopWays.size());
	TorqueWorld::RouteRequest Way = LoopWays[size_t(Next)];
	Way.Mode = Want.Mode;
	for (size_t I = 0; I < Way.Pins.size(); ++I)
		Way.Pins[I].Name = (I == 0 || I + 1 == Way.Pins.size()) ? Start.Name : TRPI::ToStd(NameAt(Way.Pins[I].Lat, Way.Pins[I].Lon));
	SetJourney(Way, FString());
	LoopOn = Next;
	Say(FString::Printf(TEXT("Round trip %d of %d: about %.0f km. Again for another way round."), Next + 1, int32(LoopWays.size()), LoopKm));
}

void FTorqueRoutePlanner::Ride()
{
	UTorqueWorldSubsystem* W = World.Get();
	if (!W || Want.Pins.size() < 2) return;
	if (WaitingFor)
	{
		const FString Who = WaitingFor();
		if (!Who.IsEmpty())
		{
			Say(FString::Printf(TEXT("Waiting for %s to plan the road: Ride again once they're ready."), *Who));
			return;
		}
	}
	W->KeepJourney(Want, Title);
	bool bGoing = false;
	if (PreviewGood())
		bGoing = W->RidePreview();
	else if (!W->IsPlanningRide())
		bGoing = W->StartJourney(Want, Title, true);
	if (!bGoing)
	{
		if (W->IsPlanningRide() && PlanFor == Serial)
		{
			// (this journey's road is being planned now: off as soon as it is)
			bRideWhenPlanned = true;
			Say(TEXT("Setting off as soon as the road is planned..."));
		}
		else
			Say(W->IsPlanningRide() ? FString(TEXT("Still planning: Ride again in a moment.")) : W->RideNote());
		return;
	}
	if (OnRide) OnRide();
}

// --- the map ----------------------------------------------------------------------------------------

FVector2D FTorqueRoutePlanner::ToScreen(double Lat, double Lon) const
{
	const TorqueWorld::P2 P = View.PixelOf(Lat, Lon);
	return MapPos + FVector2D(P.X, P.Z) * PixelScale;
}

TorqueWorld::Geo FTorqueRoutePlanner::ToPlace(const FVector2D& Screen) const
{
	const FVector2D P = (Screen - MapPos) / PixelScale;
	return View.PlaceAt(P.X, P.Y);
}

bool FTorqueRoutePlanner::OnMap(const FVector2D& Screen) const
{
	return Screen.X >= MapPos.X && Screen.Y >= MapPos.Y && Screen.X < MapPos.X + MapSize.X && Screen.Y < MapPos.Y + MapSize.Y;
}

void FTorqueRoutePlanner::FitTo(const std::vector<TorqueWorld::RoutePin>& Pins)
{
	if (Pins.empty()) return;
	double S = 90.0, N = -90.0, Wst = 180.0, E = -180.0;
	for (const TorqueWorld::RoutePin& P : Pins)
	{
		S = FMath::Min(S, P.Lat);
		N = FMath::Max(N, P.Lat);
		Wst = FMath::Min(Wst, P.Lon);
		E = FMath::Max(E, P.Lon);
	}
	TorqueWorld::Route::MapView At0 = View;
	At0.Zoom = 0.0;
	const TorqueWorld::P2 A = At0.PixelOf(N, Wst), B = At0.PixelOf(S, E);
	const double Wide = FMath::Max(1e-9, B.X - A.X), High = FMath::Max(1e-9, B.Z - A.Z);
	const double Fit = std::log2(FMath::Min(View.W * 0.7 / Wide, View.H * 0.7 / High));
	View.Zoom = FMath::Clamp(Fit, TorqueWorld::Route::MapView::MinZoom, 11.5);
	View.Lat = 0.5 * (S + N);
	View.Lon = 0.5 * (Wst + E);
	bViewDirty = true;
}

bool FTorqueRoutePlanner::Button(UCanvas* Canvas, float X, float Y, float W, float H, const FString& Label, bool bOn, bool bEnabled, TFunction<void()> Do, float TextPx)
{
	namespace Ui = TorqueUi;
	const float S = Ui::Scale(Canvas);
	const bool bOver = bEnabled && Mouse.X >= X && Mouse.X < X + W && Mouse.Y >= Y && Mouse.Y < Y + H;
	Ui::Rect(Canvas, X, Y, W, H, bOn ? FLinearColor(Ui::Accent.R, Ui::Accent.G, Ui::Accent.B, 0.22f) : (bOver ? FLinearColor(1, 1, 1, 0.10f) : Ui::PanelLight));
	if (bOn) Ui::Rect(Canvas, X, Y + H - FMath::Max(2.0f, 2.0f * S), W, FMath::Max(2.0f, 2.0f * S), Ui::Accent);
	const float Px = TextPx > 0.0f ? TextPx : 16.0f * S;
	Ui::TextFit(Canvas, Label, X + W * 0.5f, Y + (H - Px * 1.25f) * 0.5f, Px, W - 10.0f * S, bEnabled ? (bOn || bOver ? Ui::Ink : Ui::Dim) : Ui::Faint, bOn, Ui::EAlign::Centre);
	if (bEnabled && Do)
	{
		FHit Hit;
		Hit.Box = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		Hit.Do = MoveTemp(Do);
		Hits.Add(MoveTemp(Hit));
	}
	return bOver;
}

// --- input ------------------------------------------------------------------------------------------

bool FTorqueRoutePlanner::Tick(APlayerController* PC)
{
	UTorqueWorldSubsystem* W = World.Get();
	if (!PC || !W) return false;
	const double Now = FPlatformTime::Seconds();
	float MX = 0.0f, MY = 0.0f;
	Mouse = PC->GetMousePosition(MX, MY) ? FVector2D(MX, MY) : FVector2D(-1.0, -1.0);
	const auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);

	if (Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right))
	{
		if (!Search.IsEmpty()) Search.Reset();
		else if (Tab != 0) Tab = 0;
		else return false;
	}
	// Typing: a search, or Ctrl+V a code, a link or a GPX.
	if (bCtrl && Just(EKeys::V))
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		Paste(Clip);
	}
	else if (bCtrl && Just(EKeys::C))
		CopyCode();
	else if (!bCtrl && Tab == 0)
		ATorquePlayerController::TypeText(PC, Search, 40);
	if (Search != Searched)
	{
		Searched = Search;
		Finds = Search.TrimStartAndEnd().IsEmpty() ? std::vector<TorqueWorld::Route::Place>() : W->Places().Search(TRPI::ToStd(Search.TrimStartAndEnd()), 6);
	}
	if (Just(EKeys::Enter) && !Finds.empty())
	{
		const TorqueWorld::Route::Place P = Finds.front();
		AddPin(P.Lat, P.Lon, TRPI::ToF(P.Name));
		Search.Reset();
		FitTo(Want.Pins);
	}
	// The map: the arrows and Page Up / Down move and zoom it, as do the mouse's drag and wheel.
	const double Step = 80.0 / FMath::Max(PixelScale, 1e-3);
	if (PC->IsInputKeyDown(EKeys::Left)) { View = View.Panned(-Step * 0.15, 0.0); bViewDirty = true; }
	if (PC->IsInputKeyDown(EKeys::Right)) { View = View.Panned(Step * 0.15, 0.0); bViewDirty = true; }
	if (PC->IsInputKeyDown(EKeys::Up)) { View = View.Panned(0.0, -Step * 0.15); bViewDirty = true; }
	if (PC->IsInputKeyDown(EKeys::Down)) { View = View.Panned(0.0, Step * 0.15); bViewDirty = true; }
	if (Just(EKeys::PageUp)) { View = View.Zoomed(1.0, View.W * 0.5, View.H * 0.5); bViewDirty = true; }
	if (Just(EKeys::PageDown)) { View = View.Zoomed(-1.0, View.W * 0.5, View.H * 0.5); bViewDirty = true; }
	const int32 Wheel = (Just(EKeys::MouseScrollUp) ? 1 : 0) - (Just(EKeys::MouseScrollDown) ? 1 : 0);
	if (Wheel != 0)
	{
		if (OnMap(Mouse))
		{
			const FVector2D At = (Mouse - MapPos) / PixelScale;
			View = View.Zoomed(0.5 * Wheel, At.X, At.Y);
			bViewDirty = true;
		}
		else if (Tab == 1)
			FamousFirst = FMath::Max(0, FamousFirst - Wheel);
	}
	const bool bHeld = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (Just(EKeys::LeftMouseButton))
	{
		bool bHit = false;
		for (const FHit& H : Hits)
			if (H.Box.IsInside(Mouse))
			{
				H.Do();   // (it may change the panel; the hits are drawn again next frame)
				bHit = true;
				break;
			}
		if (!bHit && OnMap(Mouse))
		{
			bDown = true;
			bDragged = false;
			DownAt = LastDrag = Mouse;
		}
	}
	else if (bDown && bHeld)
	{
		if (!bDragged && FVector2D::Distance(Mouse, DownAt) > 5.0) bDragged = true;
		if (bDragged && Mouse != LastDrag)
		{
			const FVector2D D = (LastDrag - Mouse) / PixelScale;
			View = View.Panned(D.X, D.Y);
			LastDrag = Mouse;
			bViewDirty = true;
		}
	}
	else if (bDown && !bHeld)
	{
		bDown = false;
		if (!bDragged && OnMap(Mouse))
		{
			const TorqueWorld::Geo G = ToPlace(Mouse);
			AddPin(G.Lat, G.Lon, FString());
		}
	}
	if (Just(EKeys::RightMouseButton) && OnMap(Mouse))
	{
		// (the pin under the pointer, the last drawn on top)
		for (int32 I = int32(Want.Pins.size()) - 1; I >= 0; --I)
			if (FVector2D::Distance(ToScreen(Want.Pins[size_t(I)].Lat, Want.Pins[size_t(I)].Lon), Mouse) < 16.0 * UiScale + 4.0)
			{
				RemovePin(I);
				break;
			}
	}

	// The map's picture: asked for as the view changes (a few times a second at most), taken when drawn.
	if (bViewDirty && Now - AskedAt > 0.12)
	{
		W->AskMap(View);
		AskedAt = Now;
		bViewDirty = false;
	}
	TorqueWorld::Route::MapView Drawn;
	TArray<uint8> Px;
	if (W->TakeMap(Drawn, Px))
		if (UTexture2D* T = TRPI::MakeTexture(Drawn.W, Drawn.H, Px))
		{
			MapTex.Reset(T);
			TexView = Drawn;
			bHaveTex = true;
		}
	if (bRideWhenPlanned && (PlanFor != Serial || !W->IsPlanningRide()))
	{
		bRideWhenPlanned = false;
		if (PreviewGood()) Ride();
	}
	// The journey planned on its own once it has stood still a moment, for its summary and its line.
	if (Want.Pins.size() >= 2 && PlanFor != Serial && Now - ChangedAt > 0.8 && !W->IsPlanningRide())
	{
		W->ForgetPreview();
		PlanCountAt = W->PreviewCount();
		if (W->StartJourney(Want, Title, false)) PlanFor = Serial;
	}
	return true;
}

// --- drawing ----------------------------------------------------------------------------------------

void FTorqueRoutePlanner::Draw(UCanvas* Canvas)
{
	namespace Ui = TorqueUi;
	UTorqueWorldSubsystem* W = World.Get();
	if (!Canvas || !W) return;
	Hits.Reset();
	const float S = Ui::Scale(Canvas);
	UiScale = S;
	const float SW = Canvas->ClipX, SH = Canvas->ClipY;
	const float Hair = FMath::Max(1.0f, FMath::RoundToFloat(S));
	const float Gap = 24.0f * S, PanelW = 600.0f * S, Foot = 56.0f * S;
	const FLinearColor Back(0.03f, 0.033f, 0.037f, 1.0f);

	// Where the map is, and the size of picture it is drawn at (a big screen's map drawn at no more
	// than 1400 pixels across and stretched: the relief is soft anyway).
	MapPos = FVector2D(Gap, Gap);
	MapSize = FVector2D(FMath::Max(100.0f, SW - PanelW - 3.0f * Gap), FMath::Max(100.0f, SH - Gap - Foot));
	PixelScale = FMath::Max(1.0, MapSize.X / 1400.0);
	const int32 WantW = FMath::RoundToInt32(MapSize.X / PixelScale), WantH = FMath::RoundToInt32(MapSize.Y / PixelScale);
	if (WantW != View.W || WantH != View.H)
	{
		const bool bFirst = View.W <= 0 || !bHaveTex;
		View.W = WantW;
		View.H = WantH;
		bViewDirty = true;
		if (bFirst && !Want.Pins.empty()) FitTo(Want.Pins);
	}

	// The map: its picture (moved and scaled to where the view is now, while a new one draws).
	Ui::Rect(Canvas, float(MapPos.X), float(MapPos.Y), float(MapSize.X), float(MapSize.Y), FLinearColor(0.11f, 0.12f, 0.13f, 1.0f));
	if (bHaveTex && MapTex.IsValid())
	{
		const TorqueWorld::Geo G0 = TexView.PlaceAt(0.0, 0.0), G1 = TexView.PlaceAt(double(TexView.W), double(TexView.H));
		const FVector2D A = ToScreen(G0.Lat, G0.Lon), B = ToScreen(G1.Lat, G1.Lon);
		const FVector2D Lo(FMath::Max(A.X, MapPos.X), FMath::Max(A.Y, MapPos.Y)), Hi(FMath::Min(B.X, MapPos.X + MapSize.X), FMath::Min(B.Y, MapPos.Y + MapSize.Y));
		if (Hi.X > Lo.X && Hi.Y > Lo.Y && B.X > A.X && B.Y > A.Y)
		{
			const FVector2D Span = B - A;
			FCanvasTileItem Tile(Lo, MapTex->GetResource(), Hi - Lo, FVector2D((Lo.X - A.X) / Span.X, (Lo.Y - A.Y) / Span.Y), FVector2D((Hi.X - A.X) / Span.X, (Hi.Y - A.Y) / Span.Y), FLinearColor::White);
			Tile.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Tile);
		}
	}
	// The planned road, else straight lines between the pins.
	const bool bPlanned = PreviewGood();
	if (bPlanned)
	{
		const std::vector<TorqueWorld::Geo>& Line = W->PreviewSummary().Line;
		for (int32 Pass = 0; Pass < 2; ++Pass)
			for (size_t I = 1; I < Line.size(); ++I)
			{
				const FVector2D A = ToScreen(Line[I - 1].Lat, Line[I - 1].Lon), B = ToScreen(Line[I].Lat, Line[I].Lon);
				if (!OnMap(A) && !OnMap(B)) continue;
				Canvas->K2_DrawLine(A, B, (Pass == 0 ? 7.0f : 3.5f) * S, Pass == 0 ? FLinearColor(0.05f, 0.03f, 0.0f, 0.8f) : Ui::Accent);
			}
	}
	else
		for (size_t I = 1; I < Want.Pins.size(); ++I)
		{
			const FVector2D A = ToScreen(Want.Pins[I - 1].Lat, Want.Pins[I - 1].Lon), B = ToScreen(Want.Pins[I].Lat, Want.Pins[I].Lon);
			// (dashed: as the crow flies, not the road yet)
			const double Len = FVector2D::Distance(A, B);
			const int32 Dashes = FMath::Clamp(int32(Len / (14.0 * S)), 1, 400);
			for (int32 K = 0; K < Dashes; K += 2)
			{
				const FVector2D P0 = A + (B - A) * (double(K) / Dashes), P1 = A + (B - A) * (double(K + 1) / Dashes);
				if (OnMap(P0) || OnMap(P1)) Canvas->K2_DrawLine(P0, P1, 2.5f * S, FLinearColor(0.1f, 0.08f, 0.05f, 0.85f));
			}
		}
	// The names of the places, the biggest first, none over another.
	if (View.Lat != LabelLat || View.Lon != LabelLon || View.Zoom != LabelZoom)
	{
		LabelLat = View.Lat;
		LabelLon = View.Lon;
		LabelZoom = View.Zoom;
		const TorqueWorld::Geo NW = View.PlaceAt(0.0, 0.0), SE = View.PlaceAt(double(View.W), double(View.H));
		const char* Kinds = View.Zoom < 6.6 ? "city" : View.Zoom < 8.2 ? "city town" : View.Zoom < 10.2 ? "city town village" : "city town village hamlet peak pass lake castle";
		Labels = W->Places().InBox(SE.Lat, NW.Lon, NW.Lat, SE.Lon, Kinds, 400);
	}
	{
		TArray<FBox2D> Taken;
		int32 Shown = 0;
		for (const TorqueWorld::Route::Place* P : Labels)
		{
			if (Shown >= 70) break;
			const FVector2D At = ToScreen(P->Lat, P->Lon);
			const bool bBig = P->Kind == "city";
			const float Px = (bBig ? 15.0f : (P->Kind == "town" ? 13.5f : 12.0f)) * S;
			const FString Name = TRPI::ToF(P->Name);
			const FVector2D Size = Ui::Measure(Name, Px, bBig);
			const FBox2D Box(At + FVector2D(-Size.X * 0.5 - 4.0 * S, -Size.Y - 6.0 * S), At + FVector2D(Size.X * 0.5 + 4.0 * S, 4.0 * S));
			if (Box.Min.X < MapPos.X || Box.Min.Y < MapPos.Y || Box.Max.X > MapPos.X + MapSize.X || Box.Max.Y > MapPos.Y + MapSize.Y) continue;
			bool bClear = true;
			for (const FBox2D& T : Taken) bClear = bClear && !T.Intersect(Box);
			if (!bClear) continue;
			Taken.Add(Box);
			++Shown;
			const bool bLand = P->Kind == "peak" || P->Kind == "pass" || P->Kind == "lake";
			Ui::Disc(Canvas, float(At.X), float(At.Y), (bBig ? 3.5f : 2.5f) * S, FLinearColor(0.1f, 0.1f, 0.1f, 0.9f), 10);
			Ui::Text(Canvas, Name, float(At.X), float(At.Y - Size.Y) - 3.0f * S, Px, bLand ? FLinearColor(0.16f, 0.2f, 0.32f, 1.0f) : FLinearColor(0.08f, 0.08f, 0.08f, 1.0f), bBig, Ui::EAlign::Centre, false);
		}
	}
	// The pins, numbered.
	for (size_t I = 0; I < Want.Pins.size(); ++I)
	{
		const bool bLoopEnd = I + 1 == Want.Pins.size() && I > 0 && TorqueWorld::Route::GroundMetres(Want.Pins.front().Lat, Want.Pins.front().Lon, Want.Pins[I].Lat, Want.Pins[I].Lon) < 50.0;
		if (bLoopEnd) continue;
		const FVector2D At = ToScreen(Want.Pins[I].Lat, Want.Pins[I].Lon);
		if (!OnMap(At)) continue;
		const float R = 12.0f * S;
		Ui::Disc(Canvas, float(At.X), float(At.Y), R + 2.0f * S, FLinearColor(0.05f, 0.04f, 0.02f, 0.9f), 20);
		Ui::Disc(Canvas, float(At.X), float(At.Y), R, I == 0 ? FLinearColor(0.35f, 0.78f, 0.42f, 1.0f) : Ui::Accent, 20);
		Ui::Text(Canvas, FString::FromInt(int32(I) + 1), float(At.X), float(At.Y) - 9.5f * S, 14.0f * S, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f), true, Ui::EAlign::Centre, false);
	}
	// The map's scale, bottom left.
	{
		const double Mpp = View.MetresPerPixel() / PixelScale;
		const double Steps[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000};
		double Bar = Steps[0];
		for (double St : Steps)
			if (St / Mpp < 160.0 * S) Bar = St;
		const float BX = float(MapPos.X) + 14.0f * S, BY = float(MapPos.Y + MapSize.Y) - 20.0f * S, BW = float(Bar / Mpp);
		Ui::Rect(Canvas, BX - 6.0f * S, BY - 24.0f * S, BW + 12.0f * S, 34.0f * S, FLinearColor(1, 1, 1, 0.65f));
		Ui::Rect(Canvas, BX, BY, BW, 3.0f * S, FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
		Ui::Text(Canvas, Bar >= 1000.0 ? FString::Printf(TEXT("%.0f km"), Bar / 1000.0) : FString::Printf(TEXT("%.0f m"), Bar), BX, BY - 20.0f * S, 13.0f * S,
			FLinearColor(0.1f, 0.1f, 0.1f, 1.0f), false, Ui::EAlign::Left, false);
	}
	if (W->IsDrawingMap() && !bHaveTex)
		Ui::Text(Canvas, TEXT("Drawing the map..."), float(MapPos.X + MapSize.X * 0.5), float(MapPos.Y + MapSize.Y * 0.5), 18.0f * S, Ui::Ink, false, Ui::EAlign::Centre);

	// Round the map, the page (which also hides what was drawn past the map's edges).
	Ui::Rect(Canvas, 0.0f, 0.0f, SW, float(MapPos.Y), Back);
	Ui::Rect(Canvas, 0.0f, float(MapPos.Y + MapSize.Y), SW, SH - float(MapPos.Y + MapSize.Y), Back);
	Ui::Rect(Canvas, 0.0f, 0.0f, float(MapPos.X), SH, Back);
	const float PX0 = float(MapPos.X + MapSize.X);
	Ui::Rect(Canvas, PX0, 0.0f, SW - PX0, SH, Back);
	Ui::Frame(Canvas, float(MapPos.X), float(MapPos.Y), float(MapSize.X), float(MapSize.Y), Ui::Line, Hair);

	// The panel.
	const float X0 = PX0 + Gap, PW = SW - X0 - Gap;
	float Y = Gap;
	Ui::Rect(Canvas, X0, Y, 120.0f * S, 3.0f * S, Ui::Accent);
	Ui::Text(Canvas, TEXT("RIDE UK"), X0, Y + 10.0f * S, 15.0f * S, Ui::Accent, true);
	Ui::Text(Canvas, TEXT("ROUTE PLANNER"), X0, Y + 30.0f * S, 34.0f * S, Ui::Ink, true);
	Y += 84.0f * S;
	Button(Canvas, X0, Y, PW * 0.5f - 4.0f * S, 36.0f * S, TEXT("Journey"), Tab == 0, true, [this]() { Tab = 0; });
	Button(Canvas, X0 + PW * 0.5f + 4.0f * S, Y, PW * 0.5f - 4.0f * S, 36.0f * S, TEXT("Famous roads"), Tab == 1, true, [this]() { Tab = 1; });
	Y += 50.0f * S;
	const float BottomY = SH - Foot - 64.0f * S;

	if (Tab == 1)
	{
		const std::vector<TorqueWorld::Route::FamousRoad>& Roads = W->FamousRoads();
		const float RowH = 50.0f * S;
		const int32 Fits = FMath::Max(1, int32((BottomY - Y) / RowH));
		FamousFirst = FMath::Clamp(FamousFirst, 0, FMath::Max(0, int32(Roads.size()) - Fits));
		for (int32 I = FamousFirst; I < int32(Roads.size()) && I < FamousFirst + Fits; ++I)
		{
			const TorqueWorld::Route::FamousRoad& F = Roads[size_t(I)];
			const bool bOver = Mouse.X >= X0 && Mouse.X < X0 + PW && Mouse.Y >= Y && Mouse.Y < Y + RowH - 4.0f * S;
			Ui::Rect(Canvas, X0, Y, PW, RowH - 4.0f * S, bOver ? FLinearColor(1, 1, 1, 0.10f) : Ui::PanelLight);
			Ui::TextFit(Canvas, TRPI::ToF(F.Name), X0 + 12.0f * S, Y + 5.0f * S, 17.0f * S, PW - 24.0f * S, Ui::Ink, true);
			Ui::TextFit(Canvas, TRPI::ToF(F.About), X0 + 12.0f * S, Y + 26.0f * S, 13.0f * S, PW - 24.0f * S, Ui::Dim);
			FHit Hit;
			Hit.Box = FBox2D(FVector2D(X0, Y), FVector2D(X0 + PW, Y + RowH - 4.0f * S));
			const int32 Which = I;
			Hit.Do = [this, W, Which]()
			{
				const std::vector<TorqueWorld::Route::FamousRoad>& All = W->FamousRoads();
				if (Which < 0 || Which >= int32(All.size())) return;
				SetJourney(All[size_t(Which)].Journey, TRPI::ToF(All[size_t(Which)].Name));
				Tab = 0;
			};
			Hits.Add(MoveTemp(Hit));
			Y += RowH;
		}
		if (int32(Roads.size()) > Fits)
			Ui::Text(Canvas, FString::Printf(TEXT("%d to %d of %d: the wheel scrolls"), FamousFirst + 1, FMath::Min(int32(Roads.size()), FamousFirst + Fits), int32(Roads.size())), X0, Y + 4.0f * S, 13.0f * S, Ui::Faint);
	}
	else
	{
		// The search.
		const float BoxH = 40.0f * S;
		Ui::Rect(Canvas, X0, Y, PW, BoxH, FLinearColor(1, 1, 1, 0.07f));
		Ui::Rect(Canvas, X0, Y + BoxH - Hair, PW, Hair, Ui::Accent);
		const bool bCaret = FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.55;
		if (Search.IsEmpty())
			Ui::Text(Canvas, TEXT("Search the UK's places, or click the map"), X0 + 12.0f * S, Y + 10.0f * S, 16.0f * S, Ui::Faint);
		else
			Ui::TextFit(Canvas, Search + (bCaret ? TEXT("|") : TEXT("")), X0 + 12.0f * S, Y + 9.0f * S, 17.0f * S, PW - 24.0f * S, Ui::Ink);
		Y += BoxH + 6.0f * S;
		for (size_t I = 0; I < Finds.size(); ++I)
		{
			const TorqueWorld::Route::Place P = Finds[I];
			const float RowH = 30.0f * S;
			const bool bOver = Mouse.X >= X0 && Mouse.X < X0 + PW && Mouse.Y >= Y && Mouse.Y < Y + RowH;
			Ui::Rect(Canvas, X0, Y, PW, RowH - 2.0f * S, bOver || (I == 0) ? FLinearColor(1, 1, 1, bOver ? 0.10f : 0.05f) : FLinearColor(0, 0, 0, 0));
			Ui::TextFit(Canvas, TRPI::ToF(P.Name), X0 + 12.0f * S, Y + 5.0f * S, 16.0f * S, PW * 0.6f, Ui::Ink);
			const FString Kind = TRPI::ToF(P.Kind) + (P.Population > 0 ? FString::Printf(TEXT(", %s people"), *FText::AsNumber(P.Population).ToString()) : FString());
			Ui::TextFit(Canvas, Kind, X0 + PW - 12.0f * S, Y + 7.0f * S, 13.0f * S, PW * 0.38f, Ui::Dim, false, Ui::EAlign::Right);
			FHit Hit;
			Hit.Box = FBox2D(FVector2D(X0, Y), FVector2D(X0 + PW, Y + RowH));
			Hit.Do = [this, P]()
			{
				AddPin(P.Lat, P.Lon, TRPI::ToF(P.Name));
				Search.Reset();
				FitTo(Want.Pins);
			};
			Hits.Add(MoveTemp(Hit));
			Y += RowH;
		}
		if (Finds.empty() && !Search.TrimStartAndEnd().IsEmpty()) { Ui::Text(Canvas, TEXT("No place by that name"), X0 + 12.0f * S, Y + 4.0f * S, 14.0f * S, Ui::Faint); Y += 28.0f * S; }
		Y += 10.0f * S;

		// The pins.
		Ui::Text(Canvas, Want.Pins.empty() ? FString(TEXT("NO PINS YET")) : FString::Printf(TEXT("PINS  (%d)"), int32(Want.Pins.size())), X0, Y, 13.0f * S, Ui::Dim, true);
		if (Want.Pins.size() >= 2)
			Button(Canvas, X0 + PW - 200.0f * S, Y - 4.0f * S, 96.0f * S, 24.0f * S, TEXT("Reverse"), false, true, [this]() { std::reverse(Want.Pins.begin(), Want.Pins.end()); LoopOn = -1; Changed(); }, 13.0f * S);
		if (!Want.Pins.empty())
			Button(Canvas, X0 + PW - 96.0f * S, Y - 4.0f * S, 96.0f * S, 24.0f * S, TEXT("Clear"), false, true, [this]() { Want.Pins.clear(); bNamed = false; LoopOn = -1; Changed(); }, 13.0f * S);
		Y += 26.0f * S;
		const int32 MaxRows = 7;
		const int32 PinCount = int32(Want.Pins.size());
		for (int32 I = 0; I < PinCount && I < MaxRows; ++I)
		{
			// (a long journey shows its first pins and its last)
			const int32 Pin = (PinCount > MaxRows && I == MaxRows - 1) ? PinCount - 1 : I;
			const float RowH = 28.0f * S;
			if (PinCount > MaxRows && I == MaxRows - 2)
			{
				Ui::Text(Canvas, FString::Printf(TEXT("... and %d more"), PinCount - MaxRows + 1), X0 + 36.0f * S, Y + 5.0f * S, 14.0f * S, Ui::Faint);
				Y += RowH;
				continue;
			}
			Ui::Disc(Canvas, X0 + 12.0f * S, Y + 13.0f * S, 10.0f * S, Pin == 0 ? FLinearColor(0.35f, 0.78f, 0.42f, 1.0f) : Ui::Accent, 16);
			Ui::Text(Canvas, FString::FromInt(Pin + 1), X0 + 12.0f * S, Y + 5.0f * S, 12.0f * S, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f), true, Ui::EAlign::Centre, false);
			const TorqueWorld::RoutePin& P = Want.Pins[size_t(Pin)];
			Ui::TextFit(Canvas, P.Name.empty() ? FString::Printf(TEXT("%.4f, %.4f"), P.Lat, P.Lon) : TRPI::ToF(P.Name), X0 + 32.0f * S, Y + 4.0f * S, 16.0f * S, PW - 110.0f * S, Ui::Ink);
			if (Pin > 0)
				Button(Canvas, X0 + PW - 64.0f * S, Y + 1.0f * S, 28.0f * S, 24.0f * S, TEXT("^"), false, true, [this, Pin]() { std::swap(Want.Pins[size_t(Pin)], Want.Pins[size_t(Pin) - 1]); LoopOn = -1; Changed(); }, 14.0f * S);
			Button(Canvas, X0 + PW - 30.0f * S, Y + 1.0f * S, 28.0f * S, 24.0f * S, TEXT("x"), false, true, [this, Pin]() { RemovePin(Pin); }, 14.0f * S);
			Y += RowH;
		}
		Y += 10.0f * S;

		// The style.
		const float StyleW = (PW - 3.0f * 6.0f * S) / 4.0f;
		int32 StyleOn = 0;
		for (int32 I = 0; I < TorqueWorld::Route::StyleCount; ++I)
			if (Want.Mode == TorqueWorld::Route::Styles[I]) StyleOn = I;
		for (int32 I = 0; I < TorqueWorld::Route::StyleCount; ++I)
			Button(Canvas, X0 + I * (StyleW + 6.0f * S), Y, StyleW, 36.0f * S, TRPI::StyleLabel(I), I == StyleOn, true, [this, I]()
			{
				Want.Mode = TorqueWorld::Route::Styles[I];
				LoopWays.clear();
				Changed();
			});
		Y += 40.0f * S;
		Ui::TextFit(Canvas, TRPI::StyleAbout(StyleOn), X0, Y, 13.0f * S, PW, Ui::Dim);
		Y += 26.0f * S;

		// A round trip, and codes.
		const float Third = (PW - 12.0f * S) / 3.0f;
		Button(Canvas, X0, Y, 36.0f * S, 36.0f * S, TEXT("<"), false, true, [this]()
		{
			for (int32 K = int32(UE_ARRAY_COUNT(TRPI::KmSteps)) - 1; K >= 0; --K)
				if (TRPI::KmSteps[K] < LoopKm - 1.0) { LoopKm = TRPI::KmSteps[K]; break; }
		});
		Ui::Text(Canvas, FString::Printf(TEXT("%.0f km"), LoopKm), X0 + 36.0f * S + (Third - 72.0f * S) * 0.5f, Y + 9.0f * S, 15.0f * S, Ui::Ink, false, Ui::EAlign::Centre);
		Button(Canvas, X0 + Third - 36.0f * S, Y, 36.0f * S, 36.0f * S, TEXT(">"), false, true, [this]()
		{
			for (double K : TRPI::KmSteps)
				if (K > LoopKm + 1.0) { LoopKm = K; break; }
		});
		Button(Canvas, X0 + Third + 6.0f * S, Y, Third, 36.0f * S, LoopOn >= 0 ? FString::Printf(TEXT("Round trip %d/3"), LoopOn + 1) : FString(TEXT("Round trip")), LoopOn >= 0, !Want.Pins.empty(), [this]() { NextLoop(); });
		Button(Canvas, X0 + 2.0f * (Third + 6.0f * S), Y, Third * 0.5f - 3.0f * S, 36.0f * S, TEXT("Copy"), false, Want.Pins.size() >= 2, [this]() { CopyCode(); });
		Button(Canvas, X0 + 2.0f * (Third + 6.0f * S) + Third * 0.5f + 3.0f * S, Y, Third * 0.5f - 3.0f * S, 36.0f * S, TEXT("Paste"), false, true, [this]()
		{
			FString Clip;
			FPlatformApplicationMisc::ClipboardPaste(Clip);
			Paste(Clip);
		});
		Y += 46.0f * S;

		// What the journey is like.
		Ui::Rect(Canvas, X0, Y, PW, Hair, Ui::Line);
		Y += 10.0f * S;
		if (!Title.IsEmpty()) Ui::TextFit(Canvas, Title, X0, Y, 20.0f * S, PW, Ui::Ink, true);
		Y += 30.0f * S;
		if (bPlanned)
		{
			const TorqueWorld::RideSummary& Sum = W->PreviewSummary();
			const double Km = Sum.Metres / 1000.0;
			const FString Bendy = Sum.Twist < 60.0 ? TEXT("mostly straight") : Sum.Twist < 150.0 ? TEXT("some bends") : Sum.Twist < 300.0 ? TEXT("twisty") : TEXT("very twisty");
			const int32 Mins = FMath::RoundToInt32(Sum.Minutes);
			const FString Time = Mins >= 60 ? FString::Printf(TEXT("%d h %02d min"), Mins / 60, Mins % 60) : FString::Printf(TEXT("%d min"), FMath::Max(1, Mins));
			const FString Cells[6][2] = {
				{TEXT("Distance"), FString::Printf(TEXT("%.1f km  (%.1f mi)"), Km, Km / 1.609344)},
				{TEXT("About"), Time},
				{TEXT("Climb"), FString::Printf(TEXT("%.0f m"), Sum.Climb)},
				{TEXT("Highest"), FString::Printf(TEXT("%.0f m"), Sum.Highest)},
				{TEXT("Bends"), FString::Printf(TEXT("%s, %.0f° a km"), *Bendy, Sum.Twist)},
				{TEXT("Fuel"), FString::Printf(TEXT("about %.1f l"), Sum.Litres(5.5))}};
			for (int32 I = 0; I < 6; ++I)
			{
				const float CX = X0 + (I % 2) * (PW * 0.5f), CY = Y + (I / 2) * 44.0f * S;
				Ui::Text(Canvas, Cells[I][0].ToUpper(), CX, CY, 11.5f * S, Ui::Dim, true);
				Ui::TextFit(Canvas, Cells[I][1], CX, CY + 15.0f * S, 17.0f * S, PW * 0.5f - 8.0f * S, Ui::Ink);
			}
		}
		else
		{
			FString Why = W->RideNote();
			if (Why.IsEmpty() || (!W->IsPlanningRide() && PlanFor != Serial))
				Why = Want.Pins.size() < 2 ? FString(TEXT("Two pins or more make a journey: a start and a finish, and anywhere to go through.")) : FString(TEXT("Planning the road..."));
			Ui::TextWrap(Canvas, Why, X0, Y, 15.0f * S, PW, Ui::Dim);
		}
	}

	// Ride.
	{
		const float BY = SH - Foot - 54.0f * S;
		const bool bCan = Want.Pins.size() >= 2;
		const FString Who = WaitingFor ? WaitingFor() : FString();
		const FString Label = !Who.IsEmpty() ? FString::Printf(TEXT("Waiting for %s"), *Who)
			: bPlanned ? FString(WaitingFor ? TEXT("Ride it (everyone)") : TEXT("Ride it"))
			: (W->IsPlanningRide() && PlanFor == Serial ? FString(TEXT("Ride it (once planned)")) : FString(TEXT("Ride it")));
		Button(Canvas, X0, BY, PW, 48.0f * S, Label, bCan && Who.IsEmpty(), bCan, [this]() { Ride(); }, 20.0f * S);
	}
	// Notes, and the keys.
	if (FPlatformTime::Seconds() < SaidUntil && !Said.IsEmpty())
		Ui::TextFit(Canvas, Said, float(MapPos.X + MapSize.X * 0.5), float(MapPos.Y) + 14.0f * S, 17.0f * S, float(MapSize.X) - 40.0f * S, Ui::Ink, true, Ui::EAlign::Centre);
	float FX = Gap;
	const float FY = SH - Foot + 16.0f * S;
	FX += Ui::KeyHint(Canvas, TEXT("Click"), TEXT("Pin a place"), FX, FY, S) + 26.0f * S;
	FX += Ui::KeyHint(Canvas, TEXT("Right click"), TEXT("Remove a pin"), FX, FY, S) + 26.0f * S;
	FX += Ui::KeyHint(Canvas, TEXT("Drag / Wheel"), TEXT("Move / zoom"), FX, FY, S) + 26.0f * S;
	FX += Ui::KeyHint(Canvas, TEXT("Ctrl+V"), TEXT("Paste a code, map link or GPX"), FX, FY, S) + 26.0f * S;
	Ui::KeyHint(Canvas, TEXT("Esc"), TEXT("Back"), FX, FY, S);
	// Who made the map, as their licences ask.
	Ui::Text(Canvas, TEXT("Relief: Mapterhorn. Places: GeoNames (CC BY 4.0). Roads: © OpenStreetMap contributors."), SW - Gap, FY + 4.0f * S, 11.5f * S, Ui::Faint, false, Ui::EAlign::Right);
}
