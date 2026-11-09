#include "Shed/TorqueDynoDesk.h"

#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPower.h"
#include "Coop/TorqueUi.h"
#include "Fit/BikeRideSpec.h"
#include "Fit/BikeTune.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "HAL/PlatformTime.h"

namespace TorqueDynoDeskImpl
{
// The game's UI kit (Coop/TorqueUi.h), as the parts computer uses it.
const FLinearColor Page(TorqueUi::Panel.R, TorqueUi::Panel.G, TorqueUi::Panel.B, 0.94f);
const FLinearColor Column = TorqueUi::PanelLight;
const FLinearColor Button(1.0f, 1.0f, 1.0f, 0.07f);
const FLinearColor ButtonHover(1.0f, 1.0f, 1.0f, 0.15f);
const FLinearColor Picked(TorqueUi::Accent.R, TorqueUi::Accent.G, TorqueUi::Accent.B, 0.88f);
const FLinearColor PickedHover = TorqueUi::Accent;
const FLinearColor Ink(0.03f, 0.03f, 0.035f, 1.0f);      // text on the amber
const FLinearColor Good(0.45f, 0.88f, 0.52f, 1.0f);
const FLinearColor TorqueBlue(0.231f, 0.561f, 0.878f, 1.0f);   // dyno_graph.gd's torque line
const FLinearColor Grid(1.0f, 1.0f, 1.0f, 0.07f);
const FLinearColor Axis(1.0f, 1.0f, 1.0f, 0.25f);
const TCHAR* const WeightTab = TEXT("Weight");

std::string Std(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString Ue(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

void Text(UCanvas* C, const FString& S, float X, float Y, float Px, const FLinearColor& Colour, bool bBold = false,
	TorqueUi::EAlign Align = TorqueUi::EAlign::Left)
{
	TorqueUi::Text(C, S, X, Y, Px, Colour, bBold, Align, Colour != Ink);
}

void Line(UCanvas* C, const FVector2D& A, const FVector2D& B, const FLinearColor& Colour, float Thick)
{
	FCanvasLineItem L(A, B);
	L.SetColor(Colour);
	L.LineThickness = Thick;
	C->DrawItem(L);
}

// What the desk reads of a setting on this bike (tuning_computer.gd _refresh): its value, range,
// why it can't move ("" when it can) and the line under it.
struct FSetting
{
	bool bKnown = false;
	double Value = 0.0, Lo = 0.0, Hi = 1.0;
	bool bBlocked = true, bTouched = false;
	FString Why, Reading;
};

FSetting Read(const UTorqueBuildSubsystem* B, int32 Bay, const FString& Key)
{
	FSetting S;
	if (!B || !B->Exists(Bay)) return S;
	const std::string K = Std(Key);
	std::string Home, Field;
	if (!TorqueFit::TuneHome(K, Home, Field)) return S;
	const bool bPartThere = Home == "bike" || B->ItemOn(Bay, FName(*Ue(Home))) != 0;
	const float StockRedline = B->NumbersOf(Bay).StockRedline;
	const TMap<FName, float> Tune = B->TuneOf(Bay);
	S.bTouched = Key == TEXT("chain") ? !FMath::IsNearlyEqual(B->ChainSlackOf(Bay), (float)TorqueFit::ChainSlackStock) : Tune.Contains(FName(*Key));
	const bool bRead = B->WithTune(Bay, [&](const TorqueFit::BikeTune& T)
	{
		S.bKnown = true;
		std::string Why = T.Block(K);
		if (Why.empty() && !bPartThere) Why = "There's no " + Std(TorqueShedParts::SlotName(FName(*Ue(Home))).ToLower()) + " fitted.";
		if (K == "chain") TorqueFit::TuneLimits(K, S.Lo, S.Hi);
		else T.Range(K, S.Lo, S.Hi);
		if (Why.empty() && S.Lo >= S.Hi) Why = "Can't be changed with this ECU.";
		S.bBlocked = !Why.empty();
		if (Why.empty() && (K == "map" || K == "ignition" || K == "limiter")) Why = T.EcuText(K);
		const std::string Help = TorqueFit::AidHelp(K);
		if (!Help.empty() && !S.bBlocked) Why = Help;
		S.Why = Ue(Why);
		S.Value = K == "chain" ? (double)B->ChainSlackOf(Bay) : T.Value(K);
		S.Reading = Ue(T.ValueText(K, S.Value, StockRedline));
	});
	if (!bRead) S.Why = TEXT("This bike isn't built from the parts catalogue, so the desk can't read it.");
	return S;
}

FString Reading(const UTorqueBuildSubsystem* B, int32 Bay, const FString& Key, double V)
{
	FString Out;
	if (!B) return Out;
	const float StockRedline = B->NumbersOf(Bay).StockRedline;
	B->WithTune(Bay, [&](const TorqueFit::BikeTune& T) { Out = Ue(T.ValueText(Std(Key), V, StockRedline)); });
	return Out;
}

// The aids this bike's ECU has (only those are shown on the Rider aids tab).
bool AidShown(const UTorqueBuildSubsystem* B, int32 Bay, const std::string& Key)
{
	if (Key.rfind("aid_", 0) != 0) return true;
	bool bHas = false;
	B->WithTune(Bay, [&](const TorqueFit::BikeTune& T) { bHas = T.HasAid(Key.substr(4)); });
	return bHas;
}

// "Now: 78.4 hp · 104 Nm · 112 mph in top at the limiter · 213 kg · engine wear x1.0", and what's holding it back.
FString Summary(const UTorqueBuildSubsystem* B, int32 Bay, TArray<FString>& OutNotes)
{
	const FTorqueEngineNumbers E = B->NumbersOf(Bay);
	if (!E.bValid) return TEXT("No engine fitted.");
	OutNotes = E.Notes;
	FString Top;
	TMap<FString, FString> Parts;
	if (const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout())
		if (B->LayoutParts(Bay, Parts))
		{
			TorqueFit::RideSpecInput In;
			for (const TPair<FString, FString>& KV : Parts) In.Parts[Std(KV.Key)] = Std(KV.Value);
			for (const TPair<FName, float>& KV : B->TuneOf(Bay)) In.Tune[Std(KV.Key.ToString())] = KV.Value;
			In.ChainSlack = B->ChainSlackOf(Bay);
			for (const FName& Slot : B->LightenedOf(Bay)) In.Lightened.push_back(Std(Slot.ToString()));
			TorqueSim::BikeSpec S;
			std::vector<std::string> Notes;
			if (TorqueFit::RideSpec(*Lay, In, S, &Notes) && !S.GearRatios.empty())
			{
				// gear_speeds: top gear at the rev limiter.
				const double Overall = S.Primary * S.GearRatios.back() * S.FinalDrive;
				const double Kmh = E.Redline / FMath::Max(Overall, 0.01) * UE_TWO_PI / 60.0 * S.RearRadius * 3.6;
				Top = FString::Printf(TEXT("  ·  %.0f km/h in top at the limiter"), Kmh);
				for (const std::string& N : Notes) OutNotes.Add(Ue(N));
			}
		}
	return FString::Printf(TEXT("Now: %.1f hp  ·  %.0f Nm%s  ·  %.0f kg  ·  engine wear x%.1f"),
		TorqueShedPower::PeakHp(E), E.PeakTorque, *Top, E.Mass, E.Stress);
}
} // namespace TorqueDynoDeskImpl

namespace TDDI = TorqueDynoDeskImpl;

// --- Which bike ---------------------------------------------------------------------------------------

TArray<int32> FTorqueDynoDesk::InReach(const UTorqueBuildSubsystem* B) const
{
	// tunable_bikes: the one on the dyno first, then those within reach, nearest first.
	TArray<int32> Out;
	if (!B) return Out;
	const int32 On = B->DynoBay();
	if (On != INDEX_NONE) Out.Add(On);
	TArray<int32> Near;
	for (int32 I = 0; I < UTorqueBuildSubsystem::Bays; ++I)
		if (I != On && B->Exists(I) && B->Bike(I).Pusher == 0 && FVector::Dist2D(B->BikeTransform(I).GetLocation(), From) < ReachCm) Near.Add(I);
	Near.Sort([B, this](int32 A, int32 C) {
		return FVector::Dist2D(B->BikeTransform(A).GetLocation(), From) < FVector::Dist2D(B->BikeTransform(C).GetLocation(), From);
	});
	Out.Append(Near);
	return Out;
}

void FTorqueDynoDesk::Open(const UTorqueBuildSubsystem* B, const FVector& Where)
{
	Shed = B;
	From = Where;
	Pending.Reset();
	Shown.Reset();
	ShownUntil.Reset();
	Dragging.Reset();
	const TArray<int32> Ids = InReach(B);
	if (B && B->DynoBay() != INDEX_NONE) Bay = B->DynoBay();
	else if (!Ids.Contains(Bay)) Bay = Ids.Num() > 0 ? Ids[0] : INDEX_NONE;
	Relist(B);
}

void FTorqueDynoDesk::PickBike(int32 Step)
{
	const TArray<int32> Ids = InReach(Shed);
	if (Ids.Num() == 0) return;
	const int32 At = Ids.IndexOfByKey(Bay);
	Bay = Ids[((At == INDEX_NONE ? 0 : At + Step) % Ids.Num() + Ids.Num()) % Ids.Num()];
	Focus = 0;
	RowScroll = 0;
	Relist(Shed);
}

void FTorqueDynoDesk::Relist(const UTorqueBuildSubsystem* B)
{
	Rows.Reset();
	LightenItems.Reset();
	const TArray<TorqueFit::TuneTab>& Tabs = TorqueFit::TuneTabs();
	if (Tab < (int32)Tabs.size())
	{
		for (const TorqueFit::TuneRow& R : Tabs[Tab].Rows)
			if (!B || !B->Exists(Bay) || TDDI::AidShown(B, Bay, R.Key)) Rows.Add({TDDI::Ue(R.Key), TDDI::Ue(R.Name), R.Step});
	}
	else if (B && B->Exists(Bay))
	{
		// _refresh_weight: the parts on the bike that can be drilled and trimmed (or are light already).
		const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
		for (const FName& Slot : TorqueShedParts::Slots())
		{
			const int32 Id = B->ItemOn(Bay, Slot);
			const FTorqueItem* It = B->Item(Id);
			if (!It || !Lay || TorqueShedParts::IsStock(It->Part)) continue;
			const std::string PartId = TDDI::Std(It->Part.ToString().ToLower());
			const TorqueFit::Json* P = Lay->Part(PartId);
			if (!P) continue;
			const TorqueFit::BikeTune T(*Lay, TorqueFit::PartsMap(), TorqueFit::TuneMap());
			if (T.LightenCut(PartId) > 0.0 || P->Flag("lightweight")) LightenItems.Add(Id);
		}
	}
	Focus = FMath::Clamp(Focus, 0, FMath::Max(0, (Tab < (int32)Tabs.size() ? Rows.Num() : LightenItems.Num()) - 1));
}

bool FTorqueDynoDesk::PickTab(const FString& Name)
{
	const TArray<TorqueFit::TuneTab>& Tabs = TorqueFit::TuneTabs();
	for (int32 I = 0; I < (int32)Tabs.size(); ++I)
		if (Name.Equals(TDDI::Ue(Tabs[I].Name), ESearchCase::IgnoreCase))
		{
			Tab = I;
			Focus = RowScroll = 0;
			Relist(Shed);
			return true;
		}
	if (Name.Equals(TDDI::WeightTab, ESearchCase::IgnoreCase))
	{
		Tab = (int32)Tabs.size();
		Focus = RowScroll = 0;
		Relist(Shed);
		return true;
	}
	return false;
}

// --- Sending -------------------------------------------------------------------------------------------

FTorqueShedRequest FTorqueDynoDesk::TuneRequest(const FString& Key, float Value, bool bReset) const
{
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::Tune;
	R.Bay = Bay;
	R.Text = Key;
	R.Amount = Value;
	R.Item = bReset ? 1 : 0;
	return R;
}

void FTorqueDynoDesk::Flush(FTorqueDeskOut& Out)
{
	const double Now = FPlatformTime::Seconds();
	for (const TPair<FString, float>& KV : Pending)
	{
		Out.Requests.Add(TuneRequest(KV.Key, KV.Value, false));
		Shown.Add(KV.Key, KV.Value);
		ShownUntil.Add(KV.Key, Now + 0.6);
	}
	Pending.Reset();
	LastSend = Now;
}

FTorqueDeskOut FTorqueDynoDesk::Close()
{
	FTorqueDeskOut Out;
	Dragging.Reset();
	Flush(Out);
	Out.bClose = true;
	return Out;
}

bool FTorqueDynoDesk::ValueOf(const FString& Key, double& Out) const
{
	if (const float* P = Pending.Find(Key)) { Out = *P; return true; }
	const double* Until = ShownUntil.Find(Key);
	if (Until && FPlatformTime::Seconds() < *Until)
		if (const float* S = Shown.Find(Key)) { Out = *S; return true; }
	return false;
}

FTorqueDeskOut FTorqueDynoDesk::TestSet(const FString& Key, float Value, bool bReset)
{
	FTorqueDeskOut Out;
	Out.Requests.Add(TuneRequest(Key, Value, bReset));
	return Out;
}

// --- The mouse and the pad -----------------------------------------------------------------------------

const FTorqueDynoDesk::FHit* FTorqueDynoDesk::HitAt(const FVector2D& Mouse) const
{
	for (int32 I = Hits.Num() - 1; I >= 0; --I)
		if (Hits[I].Box.IsInside(Mouse)) return &Hits[I];
	return nullptr;
}

FTorqueDeskOut FTorqueDynoDesk::Click(const FVector2D& Mouse)
{
	FTorqueDeskOut Out;
	const FHit* H = HitAt(Mouse);
	if (!H) return Out;
	switch (H->What)
	{
	case EHit::Close:
		return Close();
	case EHit::Stock:
		// _reset_all: every setting the bike has back to how it came.
		if (Shed && Shed->Exists(Bay))
		{
			Pending.Reset();
			for (const TPair<FName, float>& KV : Shed->TuneOf(Bay)) Out.Requests.Add(TuneRequest(KV.Key.ToString(), 0.0f, true));
			if (!FMath::IsNearlyEqual(Shed->ChainSlackOf(Bay), (float)TorqueFit::ChainSlackStock)) Out.Requests.Add(TuneRequest(TEXT("chain"), 0.0f, true));
		}
		break;
	case EHit::BikePrev:
	case EHit::BikeNext:
		Flush(Out);
		PickBike(H->What == EHit::BikeNext ? 1 : -1);
		break;
	case EHit::Tab:
		Flush(Out);
		Tab = H->Arg;
		Focus = RowScroll = 0;
		Relist(Shed);
		break;
	case EHit::Reset:
		if (Rows.IsValidIndex(H->Arg))
		{
			Pending.Remove(Rows[H->Arg].Key);
			ShownUntil.Remove(Rows[H->Arg].Key);
			Out.Requests.Add(TuneRequest(Rows[H->Arg].Key, 0.0f, true));
			Focus = H->Arg;
		}
		break;
	case EHit::Slider:
		if (Rows.IsValidIndex(H->Arg))
		{
			// Picked up: Hold moves it with the mouse till it's let go.
			const TDDI::FSetting S = TDDI::Read(Shed, Bay, Rows[H->Arg].Key);
			if (S.bBlocked) break;
			Dragging = Rows[H->Arg].Key;
			DragTrack = H->Box;
			DragLo = S.Lo;
			DragHi = S.Hi;
			DragStep = Rows[H->Arg].Step;
			Focus = H->Arg;
			FTorqueDeskOut Moved = Hold(Mouse, true);
			Out.Requests.Append(Moved.Requests);
		}
		break;
	case EHit::Run:
	{
		Flush(Out);
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::DynoRun;
		R.Bay = Bay;
		Out.Requests.Add(R);
		break;
	}
	case EHit::Lighten:
	{
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Lighten;
		R.Item = H->Arg;
		Out.Requests.Add(R);
		break;
	}
	}
	return Out;
}

FTorqueDeskOut FTorqueDynoDesk::Hold(const FVector2D& Mouse, bool bLeftDown)
{
	FTorqueDeskOut Out;
	if (!Dragging.IsEmpty())
	{
		if (!bLeftDown)
		{
			Dragging.Reset();
			Flush(Out);
			return Out;
		}
		const double W = FMath::Max(DragTrack.Max.X - DragTrack.Min.X, 1.0);
		const double F = FMath::Clamp((Mouse.X - DragTrack.Min.X) / W, 0.0, 1.0);
		double V = FMath::Lerp(DragLo, DragHi, F);
		if (DragStep > 0.0) V = FMath::Clamp(DragLo + FMath::RoundToDouble((V - DragLo) / DragStep) * DragStep, DragLo, DragHi);
		double Was = 0.0;
		if (!ValueOf(Dragging, Was) || !FMath::IsNearlyEqual(Was, V, 1e-6)) Pending.Add(Dragging, (float)V);
	}
	if (Pending.Num() > 0 && FPlatformTime::Seconds() - LastSend >= SendEvery) Flush(Out);
	return Out;
}

void FTorqueDynoDesk::Scroll(int32 Dir)
{
	RowScroll = FMath::Max(0, RowScroll + Dir);
}

FTorqueDeskOut FTorqueDynoDesk::Pad(int32 RowStep, int32 MoveStep, int32 TabStep, bool bReset, bool bRun)
{
	FTorqueDeskOut Out;
	const int32 NumTabs = (int32)TorqueFit::TuneTabs().size() + 1;
	if (TabStep != 0)
	{
		Flush(Out);
		Tab = ((Tab + TabStep) % NumTabs + NumTabs) % NumTabs;
		Focus = RowScroll = 0;
		Relist(Shed);
		return Out;
	}
	const bool bWeight = Tab >= (int32)TorqueFit::TuneTabs().size();
	const int32 Count = bWeight ? LightenItems.Num() : Rows.Num();
	if (RowStep != 0 && Count > 0) Focus = FMath::Clamp(Focus + RowStep, 0, Count - 1);
	if (bRun)
	{
		Flush(Out);
		FTorqueShedRequest R;
		R.Kind = bWeight && LightenItems.IsValidIndex(Focus) ? ETorqueShedAct::Lighten : ETorqueShedAct::DynoRun;
		R.Bay = Bay;
		if (bWeight && LightenItems.IsValidIndex(Focus)) R.Item = LightenItems[Focus];
		Out.Requests.Add(R);
		return Out;
	}
	if (bWeight || !Rows.IsValidIndex(Focus)) return Out;
	const FRow& Row = Rows[Focus];
	if (bReset)
	{
		Pending.Remove(Row.Key);
		ShownUntil.Remove(Row.Key);
		Out.Requests.Add(TuneRequest(Row.Key, 0.0f, true));
		return Out;
	}
	if (MoveStep != 0)
	{
		const TDDI::FSetting S = TDDI::Read(Shed, Bay, Row.Key);
		if (S.bBlocked) return Out;
		double V = S.Value;
		ValueOf(Row.Key, V);
		V = FMath::Clamp(V + MoveStep * Row.Step, S.Lo, S.Hi);
		Pending.Add(Row.Key, (float)V);
		Flush(Out);
	}
	return Out;
}

// --- Drawing -------------------------------------------------------------------------------------------

void FTorqueDynoDesk::Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* B, const FVector2D& Mouse, const FString& Status, bool bStatusOk)
{
	Hits.Reset();
	Shed = B;
	if (!Canvas || !B) return;
	const TArray<int32> Ids = InReach(B);
	if (!Ids.Contains(Bay)) Bay = Ids.Num() > 0 ? Ids[0] : INDEX_NONE;
	Relist(B);   // (the parts can change under you: a co-op mate unbolting the ECU)
	const float S = TorqueUi::Scale(Canvas);
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& C) { TorqueUi::Rect(Canvas, X, Y, W, H, C); };
	auto Over = [&Mouse](float X, float Y, float W, float H) { return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H; };
	auto AddHit = [this](float X, float Y, float W, float H, EHit What, int32 Arg) { Hits.Add({FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), What, Arg}); };
	auto Btn = [&](float X, float Y, float W, float H, const FString& Label, float Px, bool bPicked, bool bOff, EHit What, int32 Arg)
	{
		const bool bOver = !bOff && Over(X, Y, W, H);
		Box(X, Y, W, H, bPicked ? (bOver ? TDDI::PickedHover : TDDI::Picked) : (bOver ? TDDI::ButtonHover : TDDI::Button));
		const FLinearColor Col = bOff ? TorqueUi::Faint : (bPicked ? TDDI::Ink : TorqueUi::Ink);
		const float TW = (float)TorqueUi::Measure(Label, Px, bPicked).X;
		if (TW <= W - 12.0f * S) TDDI::Text(Canvas, Label, X + (W - TW) * 0.5f, Y + (H - Px * 1.2f) * 0.5f, Px, Col, bPicked);
		else TorqueUi::TextFit(Canvas, Label, X + 6.0f * S, Y + (H - Px * 1.2f) * 0.5f, Px, W - 12.0f * S, Col, bPicked);
		if (!bOff) AddHit(X, Y, W, H, What, Arg);
	};

	// The shade and the page (tuning_computer.gd: a 28 px margin, the panel filling the rest).
	TorqueUi::Backdrop(Canvas, 0.6f);
	const float PW = FMath::Min(1320.0f * S, Canvas->ClipX - 56.0f * S), PH = FMath::Min(860.0f * S, Canvas->ClipY - 56.0f * S);
	const float X0 = (Canvas->ClipX - PW) * 0.5f, Y0 = (Canvas->ClipY - PH) * 0.5f;
	const float M = 18.0f * S;
	Box(X0, Y0, PW, PH, TDDI::Page);
	Box(X0, Y0, PW, 3.0f * S, TorqueUi::Accent);

	// The header: the title, the bike (arrows through those in reach), Back to stock, Close.
	float Y = Y0 + M;
	const float BH = 36.0f * S;
	TDDI::Text(Canvas, TEXT("DYNO & TUNING"), X0 + M, Y + 2.0f * S, 28.0f * S, TorqueUi::Accent, true);
	const float BikeX = X0 + M + 260.0f * S, BikeW = 380.0f * S;
	const bool bBike = B->Exists(Bay);
	Btn(BikeX, Y, BH, BH, TEXT("<"), 18.0f * S, false, Ids.Num() < 2, EHit::BikePrev, 0);
	Box(BikeX + BH + 4.0f * S, Y, BikeW, BH, TDDI::Column);
	const FString BikeName = bBike ? B->Bike(Bay).Name + (B->OnDyno(Bay) ? TEXT("  (on the dyno)") : TEXT("")) : FString(TEXT("No bike in reach: wheel one onto the dyno"));
	TorqueUi::TextFit(Canvas, BikeName, BikeX + BH + 14.0f * S, Y + 8.0f * S, 16.0f * S, BikeW - 20.0f * S, bBike ? TorqueUi::Ink : TorqueUi::Dim);
	Btn(BikeX + BH + BikeW + 8.0f * S, Y, BH, BH, TEXT(">"), 18.0f * S, false, Ids.Num() < 2, EHit::BikeNext, 0);
	const float CloseW = 150.0f * S, StockW = 150.0f * S;
	Btn(X0 + PW - M - CloseW, Y, CloseW, BH, TEXT("Close (F / Esc)"), 15.0f * S, false, false, EHit::Close, 0);
	Btn(X0 + PW - M - CloseW - 10.0f * S - StockW, Y, StockW, BH, TEXT("Back to stock"), 15.0f * S, false, !bBike, EHit::Stock, 0);
	Y += BH + 10.0f * S;
	TDDI::Text(Canvas, TEXT("Change a setting, run the dyno and compare: the last run stays on the graph, dashed. Riding uses the same tune."),
		X0 + M, Y, 14.0f * S, TorqueUi::Dim);
	Y += 28.0f * S;

	// The columns: the settings on the left, the dyno on the right.
	const float LW = 560.0f * S, Gap = 16.0f * S;
	const float LX = X0 + M, RX = LX + LW + Gap, RW = X0 + PW - M - RX;
	const float Bottom = Y0 + PH - M;

	// The tabs.
	const TArray<TorqueFit::TuneTab>& Tabs = TorqueFit::TuneTabs();
	{
		float TX = LX;
		const float TH = 32.0f * S;
		for (int32 I = 0; I <= (int32)Tabs.size(); ++I)
		{
			const FString Name = I < (int32)Tabs.size() ? TDDI::Ue(Tabs[I].Name) : FString(TDDI::WeightTab);
			const float TW = (float)TorqueUi::Measure(Name, 14.0f * S, true).X + 18.0f * S;
			Btn(TX, Y, TW, TH, Name, 14.0f * S, I == Tab, false, EHit::Tab, I);
			TX += TW + 4.0f * S;
		}
		Y += TH + 8.0f * S;
	}
	const float SummaryH = 108.0f * S;
	const float ListTop = Y, ListBottom = Bottom - SummaryH;
	Box(LX, ListTop, LW, ListBottom - ListTop, TDDI::Column);

	if (!bBike)
	{
		TorqueUi::TextWrap(Canvas, TEXT("Wheel a bike onto the dyno, or bring one close, to tune it."), LX + 14.0f * S, ListTop + 14.0f * S, 16.0f * S,
			LW - 28.0f * S, TorqueUi::Dim);
	}
	else if (Tab < (int32)Tabs.size())
	{
		float RY = ListTop + 8.0f * S;
		if (Tabs[Tab].Name == "Rider aids")
		{
			FString Note;
			B->WithTune(Bay, [&Note](const TorqueFit::BikeTune& T) { Note = TDDI::Ue(T.AidsNote()); });
			RY += TorqueUi::TextWrap(Canvas, Note, LX + 14.0f * S, RY, 13.0f * S, LW - 28.0f * S, TorqueUi::Dim) + 8.0f * S;
		}
		const float RowH = 92.0f * S;
		const int32 Fits = FMath::Max(1, FMath::FloorToInt((ListBottom - RY) / RowH));
		RowScroll = FMath::Clamp(RowScroll, 0, FMath::Max(0, Rows.Num() - Fits));
		if (Rows.IsValidIndex(Focus) && Focus < RowScroll) RowScroll = Focus;
		if (Rows.IsValidIndex(Focus) && Focus >= RowScroll + Fits) RowScroll = Focus - Fits + 1;
		for (int32 I = RowScroll; I < Rows.Num() && I < RowScroll + Fits; ++I)
		{
			const FRow& R = Rows[I];
			const TDDI::FSetting Set = TDDI::Read(B, Bay, R.Key);
			double V = Set.Value;
			const bool bMoving = ValueOf(R.Key, V);
			const FString Reading = bMoving ? TDDI::Reading(B, Bay, R.Key, V) : Set.Reading;
			const float X = LX + 12.0f * S, W = LW - 24.0f * S;
			if (I == Focus) Box(LX + 4.0f * S, RY - 4.0f * S, LW - 8.0f * S, RowH - 6.0f * S, FLinearColor(1, 1, 1, 0.035f));
			// The name, the reading on the right, Reset.
			const float ResetW = 70.0f * S;
			TDDI::Text(Canvas, R.Name, X, RY, 17.0f * S, Set.bBlocked ? TorqueUi::Dim : TorqueUi::Ink);
			TorqueUi::Text(Canvas, Reading, X + W - ResetW - 10.0f * S, RY + 1.0f * S, 16.0f * S, Set.bBlocked ? TorqueUi::Faint : TorqueUi::Accent, false,
				TorqueUi::EAlign::Right);
			Btn(X + W - ResetW, RY - 2.0f * S, ResetW, 26.0f * S, TEXT("Reset"), 13.0f * S, false, Set.bBlocked || !(Set.bTouched || bMoving), EHit::Reset, I);
			// The slider.
			const float SY = RY + 40.0f * S;
			const bool bCentred = Set.Lo < 0.0 && Set.Hi > 0.0 && FMath::IsNearlyEqual(-Set.Lo, Set.Hi, 1e-3);
			const float Frac = Set.Hi > Set.Lo ? (float)((V - Set.Lo) / (Set.Hi - Set.Lo)) : 0.0f;
			const bool bHot = Dragging == R.Key || (!Set.bBlocked && Over(X, SY - 13.0f * S, W, 26.0f * S));
			if (Set.bBlocked) TorqueUi::Rect(Canvas, X, SY - 2.0f * S, W, 4.0f * S, FLinearColor(1, 1, 1, 0.06f));
			else
			{
				TorqueUi::Slider(Canvas, X, SY, W, Frac, bCentred, bHot, S);
				AddHit(X, SY - 13.0f * S, W, 26.0f * S, EHit::Slider, I);
			}
			// Why it can't move, or what it does.
			TorqueUi::TextFit(Canvas, Set.Why, X, SY + 16.0f * S, 13.0f * S, W, Set.bBlocked ? TorqueUi::Warn * FLinearColor(1, 1, 1, 0.8f) : TorqueUi::Dim);
			RY += RowH;
		}
		if (Rows.Num() > Fits) TDDI::Text(Canvas, TEXT("(wheel for more)"), LX + LW - 130.0f * S, ListBottom - 22.0f * S, 12.0f * S, TorqueUi::Dim);
	}
	else
	{
		// Weight: drill and trim parts, once each.
		float RY = ListTop + 10.0f * S;
		RY += TorqueUi::TextWrap(Canvas, TEXT("Drill and trim parts to save weight. It's done once per part and costs it some condition: a lightened part is a little weaker and wears faster."),
			LX + 14.0f * S, RY, 13.0f * S, LW - 28.0f * S, TorqueUi::Dim) + 10.0f * S;
		const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
		const float RowH = 40.0f * S;
		const int32 Fits = FMath::Max(1, FMath::FloorToInt((ListBottom - RY) / RowH));
		RowScroll = FMath::Clamp(RowScroll, 0, FMath::Max(0, LightenItems.Num() - Fits));
		for (int32 I = RowScroll; I < LightenItems.Num() && I < RowScroll + Fits && Lay; ++I)
		{
			const FTorqueItem* It = B->Item(LightenItems[I]);
			if (!It) continue;
			const std::string PartId = TDDI::Std(It->Part.ToString().ToLower());
			const TorqueFit::Json* P = Lay->Part(PartId);
			const TorqueFit::BikeTune T(*Lay, TorqueFit::PartsMap(), TorqueFit::TuneMap());
			const double Mass = P ? P->Num("mass", 0.0) : 0.0, Cut = T.LightenCut(PartId);
			const bool bLight = P && P->Flag("lightweight");
			FName SlotOf;
			for (const FName& Slot : TorqueShedParts::Slots())
				if (B->ItemOn(Bay, Slot) == LightenItems[I]) SlotOf = Slot;
			if (I == Focus) Box(LX + 4.0f * S, RY - 3.0f * S, LW - 8.0f * S, RowH - 2.0f * S, FLinearColor(1, 1, 1, 0.035f));
			const float BW = 180.0f * S;
			TorqueUi::TextFit(Canvas, FString::Printf(TEXT("%s: %s, %.1f kg"), *TorqueShedParts::SlotName(SlotOf), *TorqueShedParts::PartName(It->Part),
				It->bLightened ? Mass * (1.0 - Cut) : Mass), LX + 14.0f * S, RY + 6.0f * S, 15.0f * S, LW - BW - 40.0f * S, TorqueUi::Ink);
			const FString Label = bLight ? FString(TEXT("Already light")) : (It->bLightened ? FString(TEXT("Lightened"))
				: FString::Printf(TEXT("Lighten (-%.1f kg)"), Mass * Cut));
			Btn(LX + LW - 14.0f * S - BW, RY, BW, RowH - 8.0f * S, Label, 13.0f * S, false, bLight || It->bLightened, EHit::Lighten, LightenItems[I]);
			RY += RowH;
		}
		if (LightenItems.Num() == 0)
			TDDI::Text(Canvas, TEXT("Nothing on this bike can be trimmed."), LX + 14.0f * S, RY, 15.0f * S, TorqueUi::Dim);
	}

	// What the bike makes now, and what's holding it back.
	if (bBike)
	{
		const double Now = FPlatformTime::Seconds();
		if (SummaryBay != Bay || Now - SummaryAt > 0.25)
		{
			SummaryNotes.Reset();
			SummaryText = TDDI::Summary(B, Bay, SummaryNotes);
			SummaryAt = Now;
			SummaryBay = Bay;
		}
		const FString& Sum = SummaryText;
		const TArray<FString>& Notes = SummaryNotes;
		float SY = ListBottom + 10.0f * S;
		TorqueUi::TextFit(Canvas, Sum, LX, SY, 15.0f * S, LW, TorqueUi::Accent);
		SY += 24.0f * S;
		for (int32 I = 0; I < Notes.Num() && I < 3; ++I)
		{
			TorqueUi::TextFit(Canvas, Notes[I], LX, SY, 13.0f * S, LW, TorqueUi::Dim);
			SY += 20.0f * S;
		}
	}

	// The dyno (dyno_graph.gd show_runs): power and torque at the wheel against rpm, the run before
	// dashed, the latest drawn as far as the sweep has got.
	const float RunH = 52.0f * S;
	const float GY = ListTop, GH = Bottom - RunH - 10.0f * S - GY;
	Box(RX, GY, RW, GH, TDDI::Column);
	TArray<const FTorqueDynoRun*> Runs;
	if (bBike)
		for (const FTorqueDynoRun& R : B->Bike(Bay).DynoRuns) Runs.Add(&R);
	float LiveRpm = 0.0f, Progress = 1.0f;
	int32 Gear = 0;
	const bool bLive = bBike && B->OnDyno(Bay) && Hud && ATorqueDynoRoom::Live(Hud, LiveRpm, Gear, Progress);
	if (!bLive) Progress = 1.0f;
	TDDI::Text(Canvas, bBike ? TEXT("DYNO: ") + B->Bike(Bay).Name.ToUpper() : FString(TEXT("DYNO")), RX + 14.0f * S, GY + 10.0f * S, 18.0f * S, TorqueUi::Accent, true);
	if (bLive)
		TorqueUi::Text(Canvas, FString::Printf(TEXT("%5.0f rpm   %s"), LiveRpm, Gear > 0 ? *FString::Printf(TEXT("gear %d"), Gear) : TEXT("N")),
			RX + RW - 14.0f * S, GY + 12.0f * S, 16.0f * S, TorqueUi::Ink, false, TorqueUi::EAlign::Right);
	if (Runs.Num() == 0)
	{
		const FString Msg = !bBike ? FString(TEXT("Wheel a bike onto the dyno, or bring one close, to tune it."))
			: B->OnDyno(Bay) ? FString(TEXT("Press Run the dyno to see its curves.")) : FString(TEXT("Wheel it onto the dyno to measure it. You can still tune it here."));
		TorqueUi::TextWrap(Canvas, Msg, RX + 24.0f * S, GY + GH * 0.45f, 18.0f * S, RW - 48.0f * S, TorqueUi::Dim);
	}
	else
	{
		const FTorqueDynoRun& Latest = *Runs.Last();
		const FTorqueDynoRun* Prev = Runs.Num() >= 2 ? Runs[Runs.Num() - 2] : nullptr;
		// The chart: rpm across, power (left scale) and torque (right scale).
		const float CX = RX + 64.0f * S, CY = GY + 74.0f * S, CW = RW - 128.0f * S, CH = GH - 74.0f * S - 120.0f * S;
		float RpmMax = 1000.0f, HpMax = 1.0f, NmMax = 1.0f;
		for (const FTorqueDynoRun* R : Runs)
		{
			RpmMax = FMath::Max(RpmMax, R->Redline);
			for (float V : R->Hp) HpMax = FMath::Max(HpMax, V);
			for (float V : R->Nm) NmMax = FMath::Max(NmMax, V);
		}
		RpmMax = FMath::CeilToFloat(RpmMax / 1000.0f) * 1000.0f;
		auto Nice = [](float V)
		{
			const float Mag = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, FMath::Max(V, 1.0f))));
			for (float Mm : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
				if (V <= Mm * Mag) return Mm * Mag;
			return 10.0f * Mag;
		};
		HpMax = Nice(HpMax * 1.1f);
		NmMax = Nice(NmMax * 1.1f);
		for (int32 I = 0; I <= 4; ++I)
		{
			const float LY = CY + CH - CH * I / 4.0f;
			TDDI::Line(Canvas, FVector2D(CX, LY), FVector2D(CX + CW, LY), I > 0 ? TDDI::Grid : TDDI::Axis, 1.0f);
			TorqueUi::Text(Canvas, FString::Printf(TEXT("%.0f"), HpMax * I / 4.0f), CX - 8.0f * S, LY - 9.0f * S, 13.0f * S, TorqueUi::Accent, false, TorqueUi::EAlign::Right);
			TorqueUi::Text(Canvas, FString::Printf(TEXT("%.0f"), NmMax * I / 4.0f), CX + CW + 8.0f * S, LY - 9.0f * S, 13.0f * S, TDDI::TorqueBlue);
		}
		TDDI::Text(Canvas, TEXT("hp"), CX - 40.0f * S, CY - 30.0f * S, 13.0f * S, TorqueUi::Accent);
		TDDI::Text(Canvas, TEXT("Nm"), CX + CW + 8.0f * S, CY - 30.0f * S, 13.0f * S, TDDI::TorqueBlue);
		for (int32 K = 0; K <= FMath::RoundToInt(RpmMax / 1000.0f); ++K)
		{
			const float LX2 = CX + CW * (K * 1000.0f) / RpmMax;
			TDDI::Line(Canvas, FVector2D(LX2, CY), FVector2D(LX2, CY + CH), TDDI::Grid, 1.0f);
			TorqueUi::Text(Canvas, FString::Printf(TEXT("%dk"), K), LX2, CY + CH + 6.0f * S, 13.0f * S, TorqueUi::Dim, false, TorqueUi::EAlign::Centre);
		}
		TorqueUi::Text(Canvas, TEXT("rpm"), CX + CW, CY + CH + 24.0f * S, 13.0f * S, TorqueUi::Dim, false, TorqueUi::EAlign::Right);
		auto Curve = [&](const FTorqueDynoRun& R, bool bPower, float Upto, bool bDashed)
		{
			const TArray<float>& Ys = bPower ? R.Hp : R.Nm;
			const FLinearColor Col = (bPower ? TorqueUi::Accent : TDDI::TorqueBlue) * FLinearColor(1, 1, 1, bDashed ? 0.55f : 1.0f);
			const float Max = bPower ? HpMax : NmMax;
			const int32 N = FMath::Min(Ys.Num(), R.Rpm.Num());
			const int32 Last = FMath::Clamp(FMath::CeilToInt(Upto * (N - 1)), 0, N - 1);
			for (int32 I = 1; I <= Last; ++I)
			{
				if (bDashed && I % 2 == 0) continue;
				const FVector2D A(CX + CW * R.Rpm[I - 1] / RpmMax, CY + CH - CH * FMath::Clamp(Ys[I - 1] / Max, 0.0f, 1.0f));
				const FVector2D C2(CX + CW * R.Rpm[I] / RpmMax, CY + CH - CH * FMath::Clamp(Ys[I] / Max, 0.0f, 1.0f));
				TDDI::Line(Canvas, A, C2, Col, (bDashed ? 1.5f : 2.5f) * S);
			}
		};
		if (Prev)
		{
			Curve(*Prev, true, 1.0f, true);
			Curve(*Prev, false, 1.0f, true);
		}
		const float Upto = bLive ? FMath::Clamp(Progress, 0.0f, 1.0f) : 1.0f;
		if (Upto > 0.0f)
		{
			Curve(Latest, true, Upto, false);
			Curve(Latest, false, Upto, false);
		}
		// The legend and the peaks.
		float LY = CY + CH + 46.0f * S;
		auto Peaks = [&](const FTorqueDynoRun& R, const TCHAR* Label, const FLinearColor& Col)
		{
			TorqueUi::TextFit(Canvas, FString::Printf(TEXT("%s run %d:  %.1f hp at %.0f rpm  ·  %.1f Nm at %.0f rpm  ·  %.0f C, fan %s"), Label, R.N,
				R.PeakHp, R.PeakHpRpm, R.PeakNm, R.PeakNmRpm, R.TempC, R.bFan ? TEXT("on") : TEXT("off")), RX + 18.0f * S, LY, 14.0f * S, RW - 36.0f * S, Col);
			LY += 22.0f * S;
		};
		if (!bLive || Upto >= 1.0f) Peaks(Latest, TEXT("Latest"), TorqueUi::Ink);
		else
		{
			TDDI::Text(Canvas, FString::Printf(TEXT("Run %d under way..."), Latest.N), RX + 18.0f * S, LY, 14.0f * S, TorqueUi::Ink);
			LY += 22.0f * S;
		}
		if (Prev)
		{
			Peaks(*Prev, TEXT("Previous (dashed)"), TorqueUi::Dim);
			if (!bLive || Upto >= 1.0f)
			{
				const float Dhp = Latest.PeakHp - Prev->PeakHp, Dnm = Latest.PeakNm - Prev->PeakNm;
				TDDI::Text(Canvas, FString::Printf(TEXT("Change: %+.1f hp, %+.1f Nm"), Dhp, Dnm), RX + 18.0f * S, LY, 14.0f * S,
					Dhp > 0.05f ? TDDI::Good : (Dhp < -0.05f ? TorqueUi::Warn : TorqueUi::Dim));
			}
		}
	}

	// Run the dyno, and what the shed last said.
	{
		const float RY = Bottom - RunH;
		const bool bCan = bBike && B->OnDyno(Bay);
		Btn(RX, RY, 260.0f * S, RunH, bCan ? FString(TEXT("Run the dyno")) : FString(TEXT("Put it on the dyno to run it")), 17.0f * S, bCan, !bCan, EHit::Run, 0);
		if (!Status.IsEmpty())
			TorqueUi::TextFit(Canvas, Status, RX + 276.0f * S, RY + (RunH - 18.0f * S) * 0.5f, 15.0f * S, RW - 280.0f * S, bStatusOk ? TorqueUi::Ink : TorqueUi::Warn);
		else
			TDDI::Text(Canvas, TEXT("Pad: L1 / R1 tabs, up / down a setting, left / right moves it, Square resets, Cross runs"), RX + 276.0f * S,
				RY + (RunH - 14.0f * S) * 0.5f, 12.0f * S, TorqueUi::Faint);
	}
}

FString FTorqueDynoDesk::Describe(const UTorqueBuildSubsystem* B) const
{
	if (!B || !B->Exists(Bay)) return TEXT("dyno desk: no bike in reach");
	const TArray<TorqueFit::TuneTab>& Tabs = TorqueFit::TuneTabs();
	FString Out = FString::Printf(TEXT("dyno desk: %s, %s tab:"), *B->Bike(Bay).Name, Tab < (int32)Tabs.size() ? *TDDI::Ue(Tabs[Tab].Name) : TDDI::WeightTab);
	for (const FRow& R : Rows)
	{
		const TDDI::FSetting S = TDDI::Read(B, Bay, R.Key);
		Out += FString::Printf(TEXT(" %s=%s%s;"), *R.Key, *S.Reading, S.bBlocked ? TEXT(" (blocked)") : TEXT(""));
	}
	const FTorqueEngineNumbers E = B->NumbersOf(Bay);
	Out += FString::Printf(TEXT(" now %.1f hp, %.1f Nm, limiter %.0f"), TorqueShedPower::PeakHp(E), E.PeakTorque, E.Redline);
	return Out;
}
