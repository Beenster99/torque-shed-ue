// The walker with the pressure washer's lance: Torque Shed's wash mini game (walker.gd's wash, the
// WashJob and wash_hud.gd). The muck coming off is TorqueWashJob, the water TorqueWashFx, the bay's
// mist ATorqueWashMist.
#include "Shed/TorqueWalker.h"

#include "Coop/TorqueUi.h"
#include "Shed/TorqueBikeLift.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueWashFx.h"
#include "Shed/TorqueWashJob.h"
#include "Shed/TorqueWashMist.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

namespace TorqueWalkerWashImpl
{
// wash_hud.gd's colours and timings.
const FLinearColor Good(0.45f, 0.95f, 0.55f);
const FLinearColor WarnC(1.0f, 0.75f, 0.3f);
const FLinearColor Cool(0.45f, 0.7f, 1.0f);
const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.6f);
const FLinearColor Mud(0.62f, 0.47f, 0.3f);
const FLinearColor Bar(0.78f, 0.9f, 1.0f);
constexpr double CueLife = 1.6;
constexpr double BigLife = 3.2;
constexpr double Linger = 1.5;
constexpr double SendEvery = 0.15;     // the shed hears how the wash is going this often
constexpr double JetReach = 400.0;     // how far the look's traced for where the water lands

// A percentage that only shows 100 once it's really done (99 until then).
int32 Pct(float V, bool bDone)
{
	return bDone ? 100 : FMath::Min(99, FMath::RoundToInt(V * 100.0f));
}

FLinearColor ZoneColour(TorqueWash::EZone Z)
{
	switch (Z)
	{
	case TorqueWash::EZone::Good: return Good;
	case TorqueWash::EZone::Close: return WarnC;
	case TorqueWash::EZone::Far: return Cool;
	default: return Dim;
	}
}
} // namespace TorqueWalkerWashImpl

namespace TWWI = TorqueWalkerWashImpl;

int32 ATorqueWalker::WashBayOf() const
{
	const UTorqueBuildSubsystem* B = Shed();
	if (!B) return INDEX_NONE;
	const int32 Mine = Wheeling();
	int32 Best = INDEX_NONE;
	double BestD = TNumericLimits<double>::Max();
	for (int32 Bay = 0; Bay < B->NumBikes(); ++Bay)
	{
		if (!B->Exists(Bay) || Bay == Mine) continue;
		const FVector At = B->BikeTransform(Bay).GetLocation();
		if (!TorqueShedLayout::InWashBay(At)) continue;
		const double D = FVector::DistSquared(At, GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = Bay;
		}
	}
	return Best;
}

void ATorqueWalker::TickWash(float Dt)
{
	bWashFiring = false;
	WashCueAge += Dt;
	WashFlash = FMath::Max(0.0, WashFlash - Dt * 1.2);
	UTorqueBuildSubsystem* B = Shed();
	if (!WashJob.IsValid() || !B) return;
	// The bike's left the bay (wheeled out, ridden off) or gone: its own grime shows again.
	const int32 Bay = WashJob->Bay;
	const bool bStill = B->Exists(Bay) && Wheeling() != Bay && TorqueShedLayout::InWashBay(B->BikeTransform(Bay).GetLocation());
	if (!bStill || !WashJob->Attach(GetWorld()))
	{
		if (bStill) SendWash();
		WashJob->Detach();
		WashJob.Reset();
		return;
	}
	WashJob->Resync(*B);
	WashJob->SetScan(bWashScan && Tool == ETorqueTool::Lance);
	WashJob->Tick(Dt);
	WashLinger = Tool == ETorqueTool::Lance ? TWWI::Linger : FMath::Max(0.0, WashLinger - Dt);
	for (const TorqueWash::FCue& C : WashJob->Cues)
	{
		// A whole bike clean isn't knocked off the screen by its last part.
		if (bWashCueBike && WashCueAge < TWWI::BigLife && !C.bBike) continue;
		bWashCueBike = C.bBike;
		WashCue = C.bBike ? FString(TEXT("SPOTLESS!")) : FString::Printf(TEXT("%s CLEAN!"), *C.Name.ToUpper());
		WashCueAge = 0.0;
		WashFlash = C.bBike ? 0.35 : 0.1;
	}
	WashJob->Cues.Reset();
}

bool ATorqueWalker::WashWork(float Dt)
{
	if (Tool != ETorqueTool::Lance) return false;
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World) return false;
	if (!WashJob.IsValid())
	{
		const int32 Bay = WashBayOf();
		if (Bay == INDEX_NONE) return false;   // no bike in the bay: the lance's timed pass on what's looked at (Work)
		static double RetryAt = 0.0;
		const double Now = World->GetTimeSeconds();
		if (Now < RetryAt) return false;
		TSharedPtr<FTorqueWashJob> Job = MakeShared<FTorqueWashJob>();
		if (!Job->Setup(World, Bay))
		{
			UE_LOG(LogTemp, Log, TEXT("Torque wash: nothing of bay %d's bike drawn to wash: timed passes instead"), Bay);
			RetryAt = Now + 3.0;
			return false;
		}
		WashJob = Job;
	}
	// Where the jet goes: straight out along the look, landing on the bike (its own surface) or whatever's there.
	const FVector Eye = Camera->GetComponentLocation();
	const FVector Dir = Camera->GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWashAim), false, this);
	for (TActorIterator<ATorqueWalker> It(World); It; ++It) Params.AddIgnoredActor(*It);
	FHitResult Hit;
	const bool bWorldHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * TWWI::JetReach, ECC_Visibility, Params);
	const bool bPressed = bWorkHeld || ActHeldFor > 0.0;
	WashJob->Step(Dt, Eye, Dir, bPressed, bWorldHit, Hit.ImpactPoint, Hit.ImpactNormal);
	// On a part that isn't on the bike (on the floor, a bench): the lance's timed pass does that (Work).
	if (!WashJob->Jet.bBike && Plan.bValid && Plan.Request.Bay == INDEX_NONE && Plan.Request.Kind == ETorqueShedAct::Wash) return false;
	bWashFiring = bPressed;
	Progress = bPressed ? FMath::Max(Progress, 0.01) : 0.0;   // "at work", for the sounds and the others' view of you
	if (bPressed && !Plan.Blocked.IsEmpty() && bWorkPressed && !WashJob->Jet.bBike) Say(Plan.Blocked, false);
	WashSendIn -= Dt;
	if (WashSendIn <= 0.0)
	{
		WashSendIn = TWWI::SendEvery;
		SendWash();
	}
	return true;
}

void ATorqueWalker::SendWash()
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B || !WashJob.IsValid()) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::Wash;
	R.Bay = WashJob->Bay;
	R.Place = FTransform(B->BikeTransform(WashJob->Bay).GetLocation());
	if (!WashJob->TakeUpdate(R)) return;
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		UE_LOG(LogTemp, Verbose, TEXT("Torque wash: the shed said no (%s)"), *WhyNot);
	}
}

bool ATorqueWalker::WashOwns(const UStaticMeshComponent* C) const
{
	return WashJob.IsValid() && WashJob->Owns(C);
}

bool ATorqueWalker::WashJetting() const
{
	if (Tool != ETorqueTool::Lance || Progress <= 0.0) return false;
	const FVector Ahead = Camera->GetComponentLocation() + Camera->GetForwardVector() * 150.0;
	return TorqueShedLayout::InWashBay(GetActorLocation()) || TorqueShedLayout::InWashBay(Ahead);
}

void ATorqueWalker::UpdateWashFx(float Dt)
{
	const bool bLance = Tool == ETorqueTool::Lance;
	if (!bLance && !(WashFx && WashFx->Busy())) return;
	if (!WashFx)
	{
		WashFx = NewObject<UTorqueWashFx>(this);
		WashFx->SetMesh(PlaneMesh);
		WashFx->SetupAttachment(GetRootComponent());
		WashFx->RegisterComponent();
	}
	FTorqueWashShot Shot;
	Shot.Dir = Camera->GetForwardVector();
	Shot.Nozzle = ToolHolder && ToolHolder->IsVisible() ? (FTransform(ToolNozzle) * ToolHolder->GetComponentTransform()).GetLocation()
		: Camera->GetComponentLocation() + Shot.Dir * 60.0;
	auto Trace = [this, &Shot]()
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueWashFxAim), false, this);
		FHitResult Hit;
		const FVector Eye = Camera->GetComponentLocation();
		Shot.bHit = GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Shot.Dir * TWWI::JetReach, ECC_Visibility, Params);
		if (!Shot.bHit) return;
		Shot.At = Hit.ImpactPoint;
		Shot.Normal = Hit.ImpactNormal;
		const TorqueWash::FJetShape J = TorqueWash::JetAt(FVector::Dist(Eye, Shot.At));
		Shot.Zone = J.Zone;
		Shot.Radius = J.Radius;
	};
	if (IsLocallyControlled())
	{
		// The wash game's jet (aimed or firing), else the lance's timed pass on a loose part (Work).
		Shot.bFiring = bLance && (bWashFiring || Progress > 0.0);
		const bool bGame = bLance && WashJob.IsValid() && !bComputer && !bBench && (bWashFiring || Progress <= 0.0);
		if (bGame)
		{
			const TorqueWash::FJet& J = WashJob->Jet;
			Shot.bHit = J.bHit;
			Shot.At = J.At;
			Shot.Normal = J.Normal;
			Shot.Radius = J.Radius;
			Shot.Zone = J.Zone;
			Shot.bRing = J.bBike;
			Shot.bMuddy = J.bBike && !WashJob->PartDone(J.Part);
		}
		else if (Shot.bFiring) Trace();
	}
	else
	{
		Shot.bFiring = bLance && Progress > 0.0 && ToolHolder && ToolHolder->IsVisible();
		if (Shot.bFiring) Trace();
	}
	if (Shot.bFiring) ATorqueWashMist::Ensure(GetWorld(), PlaneMesh);   // the bay's mist builds while anyone washes
	FVector Eye = Camera->GetComponentLocation();
	if (const UWorld* World = GetWorld())
		if (const APlayerController* Local = World->GetFirstPlayerController(); Local && Local->PlayerCameraManager)
			Eye = Local->PlayerCameraManager->GetCameraLocation();
	WashFx->Update(Dt, Shot, Eye);
}

FString ATorqueWalker::WashControlsText() const
{
	return TEXT("Hold left mouse (Cross) and sweep it over the muck; go round the bike for the far side, raise it on the lift and crouch for underneath   ")
		TEXT("X (D-pad up): show the muck still to do");
}

FString ATorqueWalker::TestWashState() const
{
	if (!WashJob.IsValid()) return FString();
	const TorqueWash::FStats S = WashJob->Stats();
	int32 DoneN = 0;
	for (const TorqueWash::FPartStat& P : S.Parts) DoneN += P.bDone ? 1 : 0;
	FString Worst;
	for (int32 I = 0; I < FMath::Min(3, S.Parts.Num()); ++I)
		Worst += FString::Printf(TEXT("%s%s %d%%"), I ? TEXT(", ") : TEXT(""), *S.Parts[I].Name, TWWI::Pct(S.Parts[I].Clean, S.Parts[I].bDone));
	return FString::Printf(TEXT("washing bay %d: %d%% clean, %d of %d parts done (dirtiest: %s), jet %s at %.0f cm"), WashJob->Bay + 1,
		TWWI::Pct(S.Clean, WashJob->bBikeDone), DoneN, S.Parts.Num(), *Worst, WashJob->Jet.bBike ? TEXT("on the bike") : TEXT("off it"), WashJob->Jet.Dist);
}

FString ATorqueWalker::TestWashPasses(int32 Passes, double Distance)
{
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World) return TEXT("no shed");
	if (!WashJob.IsValid())
	{
		const int32 Bay = WashBayOf();
		if (Bay == INDEX_NONE) return TEXT("no bike in the wash bay");
		TSharedPtr<FTorqueWashJob> Job = MakeShared<FTorqueWashJob>();
		if (!Job->Setup(World, Bay)) return TEXT("nothing of the bike drawn to wash");
		WashJob = Job;
	}
	const bool bUnder = ATorqueBikeLift::HeightAt(this, B->BikeTransform(WashJob->Bay).GetLocation()) > 50.0;
	const int32 Landed = WashJob->TestSweep(FMath::Max(Passes, 1), Distance, bUnder);
	SendWash();
	return FString::Printf(TEXT("%d steps of the jet on the bike%s; %s"), Landed, bUnder ? TEXT(" (underneath too)") : TEXT(""), *TestWashState());
}

// --- The readout (wash_hud.gd) ----------------------------------------------------------------------

bool ATorqueWalker::WashHudUp() const
{
	return (WashJob.IsValid() && Tool == ETorqueTool::Lance && !bComputer && !bBench) || WashCueAge < TWWI::BigLife || WashFlash > 0.0;
}

void ATorqueWalker::DrawWash(AHUD* Hud, UCanvas* Canvas) const
{
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	const UTorqueBuildSubsystem* B = Shed();
	if (!Hud || !Canvas || !Font || !B) return;
	const float S = Canvas->ClipY / 1080.0f;
	auto Px = [S](float Size) { return S * Size / 20.0f; };
	auto Rect = [Hud](const FLinearColor& C, float X, float Y, float W, float H) { Hud->DrawRect(C, X, Y, W, H); };
	auto Frame = [&Rect](const FLinearColor& C, float X, float Y, float W, float H, float T)
	{
		Rect(C, X, Y, W, T);
		Rect(C, X, Y + H - T, W, T);
		Rect(C, X, Y, T, H);
		Rect(C, X + W - T, Y, T, H);
	};
	// Crisp lettering (Coop/TorqueUi.h), sized as the engine's large font scaled by Size: the scaled
	// bitmap font with four shadow copies drew it blurred (beenster, Nov 10: "text at the top centre
	// which is blurred").
	const float Em = (float)FMath::Max(Font->LegacyFontSize, 8);
	auto Text = [Canvas, Em](const FString& T, float X, float Y, float Size, const FLinearColor& C, int32 Align = 0, float Width = 0.0f, bool bShadow = false)
	{
		const TorqueUi::EAlign A = Align == 1 ? TorqueUi::EAlign::Centre : (Align == 2 ? TorqueUi::EAlign::Right : TorqueUi::EAlign::Left);
		const float AX = Align == 1 ? X + Width * 0.5f : (Align == 2 ? X + Width : X);
		TorqueUi::Text(Canvas, T, AX, Y, Em * Size, C, false, A, bShadow);
	};
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	if (WashFlash > 0.0) Rect(FLinearColor(0.9f, 0.97f, 1.0f, (float)WashFlash * 0.5f), 0.0f, 0.0f, CW, CH);
	if (WashJob.IsValid() && Tool == ETorqueTool::Lance && !bComputer && !bBench)
	{
		const TorqueWash::FStats St = WashJob->Stats();
		const TorqueWash::FJet& Jet = WashJob->Jet;
		const bool bAll = WashJob->bBikeDone;
		const float W = FMath::Min(660.0f * S, CW - 48.0f * S);
		const float X0 = (CW - W) * 0.5f, Y0 = 96.0f * S, H0 = 214.0f * S;
		const float In = 16.0f * S, Inner = W - 2.0f * In;
		Rect(FLinearColor(0.03f, 0.04f, 0.06f, 0.8f), X0, Y0, W, H0);
		Rect(TWWI::Cool, X0, Y0, W, 3.0f * S);
		const FString BikeName = B->Exists(WashJob->Bay) && !B->Bike(WashJob->Bay).Name.IsEmpty() ? B->Bike(WashJob->Bay).Name.ToUpper() : FString(TEXT("THE BIKE"));
		Text(FString::Printf(TEXT("PRESSURE WASHING  -  %s"), *BikeName), X0 + In, Y0 + 12.0f * S, Px(18.0f), TWWI::Cool);
		Text(FString::Printf(TEXT("%d%% CLEAN"), TWWI::Pct(St.Clean, bAll)), X0 + In, Y0 + 8.0f * S, Px(24.0f), bAll ? TWWI::Good : FLinearColor::White, 2, Inner);
		// The whole bike: clean against the muck still on it.
		const float BarY = Y0 + 40.0f * S, BarH = 16.0f * S;
		Rect(FLinearColor(TWWI::Mud.R, TWWI::Mud.G, TWWI::Mud.B, 0.55f), X0 + In, BarY, Inner, BarH);
		Rect(bAll ? TWWI::Good : TWWI::Bar, X0 + In, BarY, Inner * FMath::Clamp(St.Clean, 0.0f, 1.0f), BarH);
		Frame(FLinearColor(1, 1, 1, 0.25f), X0 + In, BarY, Inner, BarH, 1.0f);
		// The part the jet's on.
		float Y = Y0 + 66.0f * S;
		if (Jet.bBike && Jet.Part != INDEX_NONE)
		{
			const bool bDone = WashJob->PartDone(Jet.Part);
			const float Pc = WashJob->PartClean(Jet.Part);
			Text(FString::Printf(TEXT("%s %s  %d%% clean"), bDone ? TEXT("OK") : TEXT(">"), *WashJob->PartName(Jet.Part), TWWI::Pct(Pc, bDone)),
				X0 + In, Y, Px(17.0f), bDone ? TWWI::Good : FLinearColor::White);
			const float PbX = X0 + In + Inner * 0.62f, PbW = Inner * 0.38f, PbH = 12.0f * S;
			Rect(FLinearColor(TWWI::Mud.R, TWWI::Mud.G, TWWI::Mud.B, 0.55f), PbX, Y + 3.0f * S, PbW, PbH);
			Rect(bDone ? TWWI::Good : TWWI::Bar, PbX, Y + 3.0f * S, PbW * FMath::Clamp(Pc, 0.0f, 1.0f), PbH);
			Frame(FLinearColor(1, 1, 1, 0.25f), PbX, Y + 3.0f * S, PbW, PbH, 1.0f);
		}
		else Text(TEXT("Aim the lance at the bike"), X0 + In, Y, Px(16.0f), TWWI::Dim);
		// How far off you're holding the lance: too close (a tiny spot), the sweet spot, losing its bite, out of reach.
		Y += 30.0f * S;
		{
			const double Span = TorqueWash::OutDist + 40.0;
			const TorqueWash::EZone Z = Jet.bHit ? Jet.Zone : TorqueWash::EZone::Out;
			const double D = Jet.bHit ? Jet.Dist : Span;
			Text(TEXT("DISTANCE"), X0 + In, Y, Px(13.0f), TWWI::Dim);
			Text(Z == TorqueWash::EZone::Out ? FString(TorqueWash::ZoneWord(Z)) : FString::Printf(TEXT("%.2f m  %s"), D / 100.0, TorqueWash::ZoneWord(Z)),
				X0 + In, Y, Px(13.0f), TWWI::ZoneColour(Z), 2, Inner);
			const float GY = Y + 20.0f * S, GH = 10.0f * S;
			auto At = [&](double Cm) { return X0 + In + Inner * (float)FMath::Clamp(Cm / Span, 0.0, 1.0); };
			Rect(FLinearColor(TWWI::WarnC.R, TWWI::WarnC.G, TWWI::WarnC.B, 0.6f), X0 + In, GY, At(TorqueWash::NearDist) - (X0 + In), GH);
			Rect(FLinearColor(TWWI::Good.R, TWWI::Good.G, TWWI::Good.B, 0.8f), At(TorqueWash::NearDist), GY, At(TorqueWash::FarDist) - At(TorqueWash::NearDist), GH);
			Rect(FLinearColor(TWWI::Cool.R, TWWI::Cool.G, TWWI::Cool.B, 0.6f), At(TorqueWash::FarDist), GY, At(TorqueWash::OutDist) - At(TorqueWash::FarDist), GH);
			Rect(FLinearColor(0.3f, 0.3f, 0.3f, 0.6f), At(TorqueWash::OutDist), GY, X0 + In + Inner - At(TorqueWash::OutDist), GH);
			Frame(FLinearColor(1, 1, 1, 0.25f), X0 + In, GY, Inner, GH, 1.0f);
			const float Mx = At(D);
			Rect(FLinearColor::White, Mx - 1.0f * S, GY - 5.0f * S, 2.0f * S, GH + 10.0f * S);
		}
		// How clean each side is: this side, the far side, the top, underneath (the worst still to do lit).
		Y += 46.0f * S;
		{
			static const TCHAR* Names[4] = {TEXT("THIS SIDE"), TEXT("FAR SIDE"), TEXT("TOP"), TEXT("UNDERNEATH")};
			TArray<int32> Shown;
			int32 Worst = INDEX_NONE;
			for (int32 K = 0; K < 4; ++K)
				if (St.Sides[K] >= 0.0f)
				{
					Shown.Add(K);
					if (Worst == INDEX_NONE || St.Sides[K] < St.Sides[Worst]) Worst = K;
				}
			const float Cw = Shown.Num() > 0 ? Inner / Shown.Num() : Inner;
			for (int32 I = 0; I < Shown.Num(); ++I)
			{
				const float V = St.Sides[Shown[I]];
				const FLinearColor C = V >= TorqueWash::DoneShare ? TWWI::Good : (Shown[I] == Worst ? TWWI::WarnC : FLinearColor::White);
				Text(FString::Printf(TEXT("%s %d%%"), Names[Shown[I]], TWWI::Pct(V, V >= 0.9995f)), X0 + In + I * Cw, Y, Px(14.0f), C, 1, Cw);
			}
		}
		// The dirtiest parts still to do.
		TArray<FString> Todo;
		for (const TorqueWash::FPartStat& P : St.Parts)
			if (!P.bDone && Todo.Num() < 4) Todo.Add(FString::Printf(TEXT("%s %d%%"), *P.Name, TWWI::Pct(P.Clean, false)));
		const FString Left = Todo.Num() > 0 ? FString(TEXT("Still dirty: ")) + FString::Join(Todo, TEXT("  -  ")) : FString(TEXT("Every part's clean!"));
		Text(Left, X0 + In, Y0 + H0 - 28.0f * S, Px(14.0f), Todo.Num() > 0 ? TWWI::WarnC : TWWI::Good);
		// The controls, low in the middle.
		Text(WashControlsText(), 0.0f, CH - 120.0f * S, Px(15.0f), FLinearColor(1, 1, 1, 0.8f), 1, CW, true);
	}
	// A part come clean: its name pops up in the middle; the whole bike, a bigger moment.
	if (WashCueAge < (bWashCueBike ? TWWI::BigLife : TWWI::CueLife) && !WashCue.IsEmpty())
	{
		const double Life = bWashCueBike ? TWWI::BigLife : TWWI::CueLife;
		const float Pop = 1.0f + (bWashCueBike ? 0.45f : 0.3f) * FMath::Max(0.0f, 1.0f - (float)WashCueAge * 5.0f);
		const float A = FMath::Clamp((float)((Life - WashCueAge) / 0.5), 0.0f, 1.0f);
		const FLinearColor C = bWashCueBike ? FLinearColor(1.0f, 0.88f, 0.35f, A) : FLinearColor(TWWI::Good.R, TWWI::Good.G, TWWI::Good.B, A);
		const float Size = Px((bWashCueBike ? 64.0f : 38.0f) * Pop);
		const float Mid = FMath::Max(CH * 0.42f, 360.0f * S);
		Text(WashCue, 0.0f, Mid, Size, C, 1, CW, true);
		if (bWashCueBike && WashJob.IsValid() && B->Exists(WashJob->Bay))
			Text(FString::Printf(TEXT("%s is clean as a whistle"), B->Bike(WashJob->Bay).Name.IsEmpty() ? TEXT("The bike") : *B->Bike(WashJob->Bay).Name),
				0.0f, Mid + Size * 40.0f, Px(22.0f), FLinearColor(1, 1, 1, A), 1, CW, true);
	}
}
