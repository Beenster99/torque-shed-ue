// The walker with the spray can or spray gun: Torque Shed's spray mini game (walker.gd's spray
// mode, _spray_step and spray_hud.gd). The coat itself is TorqueSprayJob, the spray you see
// TorqueSprayFx.
#include "Shed/TorqueWalker.h"

#include "Shed/TorqueBoothHooks.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueSprayFx.h"
#include "Shed/TorqueSprayJob.h"
#include "TorqueAudio/TorqueSfx.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

namespace TorqueWalkerSprayImpl
{
using namespace TorqueSpray;

bool Sprays(ETorqueTool T) { return T == ETorqueTool::Spray || T == ETorqueTool::Gun; }

bool OnHook(const FTorqueItem& It)
{
	if (It.At != ETorqueAt::Spot) return false;
	const FTorquePlace* P = TorqueShedLayout::FindPlace(It.Spot);
	return P && P->Kind == ETorquePlace::Hook;
}

// A job's key without its part, so the same coat on the other parts in the booth matches.
FString Tail(const FString& Key)
{
	TArray<FString> Bits;
	Key.ParseIntoArray(Bits, TEXT("|"), false);
	if (Bits.Num() > 1) Bits.RemoveAt(1);
	return FString::Join(Bits, TEXT("|"));
}

// spray_hud.gd's colours.
const FLinearColor Good(0.45f, 0.95f, 0.55f);
const FLinearColor Bad(1.0f, 0.45f, 0.35f);
const FLinearColor Warn(1.0f, 0.75f, 0.3f);
const FLinearColor Cool(0.45f, 0.7f, 1.0f);
const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.6f);
const FLinearColor Accent(1.0f, 0.62f, 0.2f);   // the shed HUD's (TorqueHudCues::Accent)
constexpr double CueLife = 1.8;
constexpr double Linger = 2.2;

FLinearColor ZoneColour(EZone Z)
{
	switch (Z)
	{
	case EZone::Good: return Good;
	case EZone::Close: return Bad;
	case EZone::Far: return Cool;
	default: return Dim;
	}
}
} // namespace TorqueWalkerSprayImpl

namespace TWSI = TorqueWalkerSprayImpl;
using namespace TorqueSpray;

bool ATorqueWalker::IsSprayPlan(const FPlan& P) const
{
	return P.bValid && !P.bComputer && (P.Request.Kind == ETorqueShedAct::Paint || P.Request.Kind == ETorqueShedAct::Prime ||
		P.Request.Kind == ETorqueShedAct::Coat);
}

FString ATorqueWalker::SprayKey(const FTorqueShedRequest& R) const
{
	return FString::Printf(TEXT("%d|%d|%d|%s|%d"), (int32)R.Kind, R.Item, R.Colour, *R.Finish.ToString(), R.bGun ? 1 : 0);
}

bool ATorqueWalker::SprayOwns(const UStaticMeshComponent* C) const
{
	if (!C) return false;
	auto Has = [C](const TSharedPtr<FTorqueSprayJob>& J)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& W : J->Meshes())
			if (W.Get() == C) return true;
		return false;
	};
	for (const TPair<FString, TSharedPtr<FTorqueSprayJob>>& KV : SprayJobs)
		if (KV.Value.IsValid() && Has(KV.Value)) return true;
	for (const TSharedPtr<FTorqueSprayJob>& J : SprayDrying)
		if (J.IsValid() && Has(J)) return true;
	return false;
}

TSharedPtr<FTorqueSprayJob> ATorqueWalker::SprayJobFor(int32 Item, const FTorqueShedRequest& R, int32 Bay, FName Slot)
{
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	const FTorqueItem* It = B ? B->Item(Item) : nullptr;
	if (!It || !World) return nullptr;
	FTorqueShedRequest Req = R;
	Req.Item = Item;
	const FString Key = SprayKey(Req);
	if (const TSharedPtr<FTorqueSprayJob>* Found = SprayJobs.Find(Key)) return *Found;
	if (SprayNoGo.Contains(Key)) return nullptr;
	// A different coat started on it and left half done (another colour, say) comes off first, so
	// the two wet coats can't show through each other.
	for (auto Old = SprayJobs.CreateIterator(); Old; ++Old)
		if (Old->Value->Item == Item)
		{
			if (Spray == Old->Value) Spray.Reset();
			Old->Value->Detach();
			Old.RemoveCurrent();
		}
	TSharedPtr<FTorqueSprayJob> Job = MakeShared<FTorqueSprayJob>();
	// What's not in a spray's way: the walkers, and the parts' own actors (each part blocks itself,
	// from its own surface: see TorqueSprayJob), the booth's hooks and wires.
	TArray<const AActor*> Ignore;
	Ignore.Add(this);
	for (TActorIterator<ATorqueShedItems> A(World); A; ++A) Ignore.Add(*A);
	for (TActorIterator<ATorqueShedBike> A(World); A; ++A) Ignore.Add(*A);
	for (TActorIterator<ATorqueBoothHooks> A(World); A; ++A) Ignore.Add(*A);
	for (TActorIterator<ATorqueWalker> A(World); A; ++A) Ignore.Add(*A);
	const bool bGun = Req.bGun;
	if (!Job->Setup(World, Item, Key, Req, Bay, Slot, LookFor(*B, B->Me(), Req, *It), bGun ? GunRuns : CanRuns, Ignore))
	{
		UE_LOG(LogTemp, Log, TEXT("Torque spray: nothing of item %d drawn to spray (%s): a timed coat instead"), Item, *Key);
		SprayNoGo.Add(Key);
		return nullptr;
	}
	Job->SetScan(bSprayScan);
	SprayJobs.Add(Key, Job);
	return Job;
}

bool ATorqueWalker::SprayWork(float Dt)
{
	bSprayFiring = false;
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World || !TWSI::Sprays(Tool))
	{
		if (Spray.IsValid() && Spray->bLive) SprayLetGo(true);
		return false;
	}
	const int32 Me = B->Me();
	// The spray controls go if the part's gone, you've picked something up, or you've walked off.
	if (SprayItem != 0)
	{
		const FTorqueItem* It = B->Item(SprayItem);
		if (!It || It->bBoxed || It->At == ETorqueAt::Held || B->HeldBy(Me) != 0)
			LeaveSprayMode();
		else if (Spray.IsValid() && Spray->Item == SprayItem &&
			FVector::Dist(Camera->GetComponentLocation(), Spray->ModelTransform().GetLocation()) > Reach + 250.0)
		{
			Say(TEXT("Too far from the part: the spray controls are down."), false);
			LeaveSprayMode();
		}
		else SpraySettle = FMath::Max(0.0, SpraySettle - Dt);
	}
	if (!IsSprayPlan(Plan))
	{
		if (Spray.IsValid() && Spray->bLive) SprayLetGo(true);
		if (SprayItem != 0) Progress = 0.0;
		return SprayItem != 0;
	}
	const bool bPressed = bWorkHeld || ActHeldFor > 0.0;
	if (!bPressed) bSprayNeedRelease = false;
	const bool bSettling = SprayItem != 0 && SpraySettle > 0.0;
	if (bSettling && bPressed) bSprayNeedRelease = true;   // held through getting set: let go first
	bool bFiring = bPressed && !bSprayNeedRelease && !bSettling;
	if (bFiring && !Plan.Blocked.IsEmpty())
	{
		Say(Plan.Blocked, false);
		bSprayNeedRelease = true;
		bFiring = false;
	}
	if (!bFiring && Spray.IsValid() && Spray->bLive) SprayLetGo(true);
	if (!bFiring && SprayItem == 0)
	{
		Progress = 0.0;
		return true;
	}
	// The coat on the part (a bike part by its bay and slot).
	const FTorqueShedRequest& R = Plan.Request;
	const int32 Bay = R.Bay;
	const FName Slot = Bay == INDEX_NONE ? NAME_None : (SprayItem != 0 ? SpraySlot : (Target == ETorqueLook::BikePart ? TargetSlot : NAME_None));
	TSharedPtr<FTorqueSprayJob> Main = SprayJobFor(R.Item, R, Slot.IsNone() ? INDEX_NONE : Bay, Slot);
	if (!Main.IsValid()) return false;   // nothing drawn to paint: the timed hold does it (Work)
	if (Spray != Main)
	{
		if (Spray.IsValid() && Spray->bLive) SprayLetGo(false);
		Spray = Main;
	}
	TArray<TSharedPtr<FTorqueSprayJob>> Jobs;
	Jobs.Add(Main);
	if (R.bGun)
		if (const FTorqueItem* It = B->Item(R.Item); It && TWSI::OnHook(*It))
		{
			// Every other part on the booth's hooks gets its own coat, so one pass can do them all.
			for (const FTorquePlace& P : TorqueShedLayout::Places())
			{
				if (P.Kind != ETorquePlace::Hook) continue;
				const int32 Other = B->ItemAtSpot(P.Name);
				const FTorqueItem* OtherIt = B->Item(Other);
				if (Other == 0 || Other == R.Item || !OtherIt || OtherIt->bBoxed) continue;
				FTorqueShedRequest OtherR = R;
				OtherR.Item = Other;
				OtherR.Bay = INDEX_NONE;
				if (!B->Check(Me, OtherR).IsEmpty()) continue;
				if (TSharedPtr<FTorqueSprayJob> J = SprayJobFor(Other, OtherR, INDEX_NONE, NAME_None)) Jobs.Add(J);
			}
		}
	// The tool: the gun's fan is wider and even across, the can's a tighter cone; with the spray
	// controls up it's careful work and lays on slower, so it takes steady passes.
	const bool bGun = R.bGun;
	double Rate = bGun ? GunRate : CanRate;
	if (SprayItem != 0) Rate *= ModeFlow;
	for (const TSharedPtr<FTorqueSprayJob>& J : Jobs) J->SetTool(bGun ? GunRadius : CanRadius, Rate, bGun ? GunFalloff : CanFalloff, 1.0);
	// Where the spray meets a surface: anything in the way, or the parts themselves (their shapes,
	// not the boxes round them).
	const FVector Eye = Camera->GetComponentLocation();
	const FVector Dir = Camera->GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueSprayAim), false, this);
	FHitResult Hit;
	bool bHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * 320.0, ECC_Visibility, Params);
	FVector At = bHit ? FVector(Hit.ImpactPoint) : Eye + Dir * 320.0;
	FVector Normal = bHit ? FVector(Hit.ImpactNormal) : -Dir;
	const AActor* HitBy = bHit ? Hit.GetActor() : nullptr;
	const bool bOwnBox = HitBy && (HitBy->IsA<ATorqueShedItems>() || HitBy->IsA<ATorqueShedBike>() || HitBy->IsA<ATorqueBoothHooks>());
	double Nearest = TNumericLimits<double>::Max();
	for (const TSharedPtr<FTorqueSprayJob>& J : Jobs)
	{
		FVector SAt, SN;
		double T = 0.0;
		if (!J->SurfaceHit(Eye, Dir, 3.5, SAt, SN, T) || T >= Nearest) continue;
		Nearest = T;
		if (!bHit || bOwnBox || FVector::Dist(Eye, Hit.ImpactPoint) > T - 5.0)
		{
			At = SAt;
			Normal = SN;
			bHit = true;
		}
	}
	for (const TSharedPtr<FTorqueSprayJob>& J : Jobs) J->Step(Dt, Eye, Dir, bHit, At, Normal, bFiring);
	if (!bFiring)
	{
		Progress = 0.0;
		return true;
	}
	bSprayFiring = true;
	Main->bLive = true;
	Progress = FMath::Max(Progress, 0.0) + Dt;   // working, for the sounds and the others' view of your hands
	ProgressKey = Plan.Key();
	Doing(Bay != INDEX_NONE ? FString::Printf(TEXT("%s on bay %d"), *Plan.Verb, Bay + 1) : Plan.Verb, 1.5);
	for (const FCue& C : Main->Cues)
	{
		SprayCue = C;
		SprayCueAge = 0.0;
		if (C.Kind == ECue::Ready)
		{
			SprayFlash = 0.12;
			if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("ting"), At, -6.0f);
		}
	}
	for (const TSharedPtr<FTorqueSprayJob>& J : Jobs) J->Cues.Reset();
	for (const TSharedPtr<FTorqueSprayJob>& J : Jobs)
		if (J->Complete()) FinishSpray(J);
	return true;
}

void ATorqueWalker::SprayLetGo(bool bFinishReady)
{
	if (!Spray.IsValid()) return;
	Spray->bLive = false;
	Progress = 0.0;
	if (!bFinishReady) return;
	// A coat that's on (90% covered) is done; one that isn't waits, just as you left it, for you to
	// come back to it. The same coat on the other parts in the booth too.
	const FString Want = TWSI::Tail(Spray->Key);
	TArray<TSharedPtr<FTorqueSprayJob>> On;
	for (const TPair<FString, TSharedPtr<FTorqueSprayJob>>& KV : SprayJobs)
		if (KV.Value.IsValid() && TWSI::Tail(KV.Key) == Want && KV.Value->Ready()) On.Add(KV.Value);
	for (const TSharedPtr<FTorqueSprayJob>& J : On) FinishSpray(J);
}

void ATorqueWalker::FinishSpray(const TSharedPtr<FTorqueSprayJob>& InJob)
{
	const TSharedPtr<FTorqueSprayJob> Job = InJob;   // (it leaves the map below)
	if (!Job.IsValid()) return;
	FPlan P = Plan;
	P.bValid = true;
	P.bComputer = false;
	P.Request = Job->Result();
	const bool bDone = Do(P);
	const TCHAR* What = Job->Layer == ELayer::Primer ? TEXT("Primer") : (Job->Layer == ELayer::Clear ? TEXT("Lacquer") : TEXT("Colour coat"));
	if (bDone)
	{
		SprayCue = FCue{ECue::Done, FString::Printf(TEXT("%s on: %d%%"), What, FMath::RoundToInt(P.Request.Amount * 100.0f))};
		SprayCueAge = 0.0;
		SprayFlash = 0.25;
		if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("ting"), Job->ModelTransform().GetLocation(), -2.0f);
	}
	Job->Finish(bDone);
	SprayJobs.Remove(Job->Key);
	if (bDone) SprayDrying.Add(Job);
	else Job->Detach();
	if (Spray == Job)
	{
		SprayShown = Job;
		Spray.Reset();
		Progress = 0.0;
		bSprayNeedRelease = bWorkHeld || ActHeldFor > 0.0;
	}
}

void ATorqueWalker::TickSprays(float Dt)
{
	UWorld* World = GetWorld();
	for (auto It = SprayJobs.CreateIterator(); It; ++It)
	{
		const TSharedPtr<FTorqueSprayJob> J = It->Value;
		if (!J.IsValid() || !J->Attach(World))
		{
			// The part's gone (taken, binned, fitted to a bike): its coat with it.
			if (J.IsValid()) J->Detach();
			if (Spray == J) Spray.Reset();
			It.RemoveCurrent();
			continue;
		}
		J->Tick(Dt);
	}
	for (int32 I = SprayDrying.Num() - 1; I >= 0; --I)
	{
		const TSharedPtr<FTorqueSprayJob> J = SprayDrying[I];
		if (J->Attach(World)) J->Tick(Dt);
		if (J->Gone() || !J->Attach(World))
		{
			J->Detach();
			SprayDrying.RemoveAt(I);
		}
	}
	// The readout: the coat going on, staying up a moment after.
	if (Spray.IsValid() && (Spray->bLive || SprayItem != 0))
	{
		SprayShown = Spray;
		SprayLinger = TWSI::Linger;
	}
	else
	{
		SprayLinger = FMath::Max(0.0, SprayLinger - Dt);
		if (SprayLinger <= 0.0) SprayShown.Reset();
	}
	SprayCueAge += Dt;
	SprayFlash = FMath::Max(0.0, SprayFlash - Dt * 1.4);
	// Something you couldn't spray may be drawn by now (unpacked, hung up): try it again now and then.
	static double NoGoClock = 0.0;
	NoGoClock += Dt;
	if (NoGoClock > 3.0)
	{
		NoGoClock = 0.0;
		SprayNoGo.Reset();
	}
}

bool ATorqueWalker::SprayInteract()
{
	if (!TWSI::Sprays(Tool)) return false;
	if (SprayItem != 0)
	{
		LeaveSprayMode();
		return true;
	}
	const UTorqueBuildSubsystem* B = Shed();
	if (!B || B->HeldBy(B->Me()) != 0 || B->PaintBay().HeldTin(B->Me())) return false;
	const FTorqueItem* It = B->Item(TargetItem);
	if (!It || It->bBoxed || !IsSprayPlan(Plan)) return false;   // (F on a box unpacks it)
	EnterSprayMode();
	return true;
}

void ATorqueWalker::EnterSprayMode()
{
	if (!Plan.Blocked.IsEmpty())
	{
		Say(Plan.Blocked, false);
		return;
	}
	SprayItem = TargetItem;
	SprayBay = Target == ETorqueLook::BikePart ? TargetBay : INDEX_NONE;
	SpraySlot = Target == ETorqueLook::BikePart ? TargetSlot : NAME_None;
	SpraySettle = Settle;
	SprayBreath = 1.0;
	SwayClock = 0.0;
	SprayNoGo.Reset();
	bSprayNeedRelease = bWorkHeld;
	Progress = 0.0;
	ProgressKey.Reset();
	Doing(TEXT("getting set to spray"), 2.0);
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("switch"), Camera->GetComponentLocation(), -8.0f);
}

void ATorqueWalker::LeaveSprayMode()
{
	if (SprayItem == 0) return;
	SprayItem = 0;
	SprayBay = INDEX_NONE;
	SpraySlot = NAME_None;
	if (Spray.IsValid() && Spray->bLive) SprayLetGo(true);
	// A coat only aimed at, with no paint on it yet, isn't kept waiting.
	for (auto It = SprayJobs.CreateIterator(); It; ++It)
		if (It->Value.IsValid() && !It->Value->bLive && It->Value->PaintedCount() == 0)
		{
			It->Value->Detach();
			if (Spray == It->Value) Spray.Reset();
			It.RemoveCurrent();
		}
	Progress = 0.0;
	ProgressKey.Reset();
	bSprayNeedRelease = bWorkHeld;
	Doing(FString(), 0.0);
}

void ATorqueWalker::SetSprayScan(bool bOn)
{
	bSprayScan = bOn;
	for (const TPair<FString, TSharedPtr<FTorqueSprayJob>>& KV : SprayJobs)
		if (KV.Value.IsValid()) KV.Value->SetScan(bOn);
	Say(bOn ? TEXT("Scan on: red is missed, amber thin, purple runs, blue dry") : TEXT("Scan off"), true, 2.5);
	if (UTorqueSfx* Sfx = UTorqueSfx::Get(this)) Sfx->PlayAt(TEXT("switch"), Camera->GetComponentLocation(), -6.0f);
}

int32 ATorqueWalker::SprayHook() const
{
	const UTorqueBuildSubsystem* B = Shed();
	const FTorqueItem* It = B && SprayItem != 0 ? B->Item(SprayItem) : nullptr;
	if (!It || It->At != ETorqueAt::Spot) return INDEX_NONE;
	const FTorquePlace* P = TorqueShedLayout::FindPlace(It->Spot);
	return P && P->Kind == ETorquePlace::Hook ? P->Index : INDEX_NONE;
}

void ATorqueWalker::SprayWind(float Dt, int32 Step, int32 Held)
{
	UTorqueBuildSubsystem* B = Shed();
	const int32 Hook = SprayHook();
	if (!B || Hook == INDEX_NONE)
	{
		SprayHookSet = -1.0f;
		SprayHookHeld = 0.0;
		return;
	}
	if (Step == 0 && Held == 0)
	{
		// Let go: where it was wound to goes to the shed if the last step hadn't yet.
		if (SprayHookSet >= 0.0f && FMath::Abs(SprayHookSet - B->HookTipOf(Hook)) >= 0.5f)
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::PaintHook;
			R.Item = Hook;
			R.Amount = SprayHookSet;
			FString WhyNot;
			B->Request(R, WhyNot);
		}
		SprayHookSet = -1.0f;
		SprayHookHeld = 0.0;
		return;
	}
	if (SprayHookSet < 0.0f) SprayHookSet = B->HookTipOf(Hook);
	const float Was = SprayHookSet;
	// A click moves it 8 cm; held past a moment it winds on at the hook's own pace.
	if (Step != 0) SprayHookSet += 8.0f * Step;
	SprayHookHeld = Held != 0 ? SprayHookHeld + Dt : 0.0;
	if (Held != 0 && SprayHookHeld > 0.3) SprayHookSet += 45.0f * Held * Dt;
	SprayHookSet = FMath::Clamp(SprayHookSet, FTorquePaintBay::HookLow, FTorquePaintBay::HookHigh);
	SprayHookSend -= Dt;
	if (SprayHookSend > 0.0 && Step == 0) return;
	if (FMath::Abs(SprayHookSet - B->HookTipOf(Hook)) < 0.5f && Was == SprayHookSet) return;
	SprayHookSend = 0.15;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::PaintHook;
	R.Item = Hook;
	R.Amount = SprayHookSet;
	FString WhyNot;
	if (!B->Request(R, WhyNot)) Say(WhyNot, false);
	else Doing(FString::Printf(TEXT("hook %d at %d cm"), Hook + 1, FMath::RoundToInt(SprayHookSet)), 1.0);
}

void ATorqueWalker::UpdateSway(float Dt)
{
	// The slight sway of your hand with the spray controls up: more as you move, much less while you
	// hold your breath, which lasts a few seconds and comes back as you breathe again. It moves the
	// view, so it moves where the fan lands.
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;
	FRotator Want = FRotator::ZeroRotator;
	if (SprayItem != 0)
	{
		bSpraySteady = bSprayBreathHeld && SprayBreath > 0.0;
		SprayBreath = FMath::Clamp(SprayBreath + (bSpraySteady ? -1.0 / Breath : 0.35) * Dt, 0.0, 1.0);
		SwayClock += Dt;
		const double Move = GetVelocity().Size2D() / 100.0;
		const double Amp = Sway * (1.0 + Move * 2.5) * (bSpraySteady ? 0.25 : ((SprayBreath > 0.05 || !bSprayBreathHeld) ? 1.0 : 1.8));
		const double T = SwayClock;
		Want.Yaw = FMath::RadiansToDegrees((FMath::Sin(T * 1.3) + 0.6 * FMath::Sin(T * 2.9 + 1.0)) * Amp);
		Want.Pitch = FMath::RadiansToDegrees((FMath::Sin(T * 1.7 + 0.4) + 0.5 * FMath::Sin(T * 3.3)) * Amp);
	}
	else
	{
		SprayBreath = 1.0;
		bSpraySteady = false;
	}
	const double K = FMath::Clamp(Dt * 6.0, 0.0, 1.0);
	const FRotator New(FMath::Lerp(SwayNow.Pitch, Want.Pitch, K), FMath::Lerp(SwayNow.Yaw, Want.Yaw, K), 0.0);
	if (!New.Equals(SwayNow, 1e-6))
	{
		FRotator R = PC->GetControlRotation();
		R.Pitch += New.Pitch - SwayNow.Pitch;
		R.Yaw += New.Yaw - SwayNow.Yaw;
		PC->SetControlRotation(R);
		SwayNow = New;
	}
}

void ATorqueWalker::UpdateSprayFx(float Dt)
{
	const bool bTool = TWSI::Sprays(Tool);
	if (!bTool && !(SprayFx && SprayFx->Busy())) return;
	if (!SprayFx)
	{
		SprayFx = NewObject<UTorqueSprayFx>(this);
		SprayFx->SetMesh(PlaneMesh);
		SprayFx->SetupAttachment(GetRootComponent());
		SprayFx->RegisterComponent();
	}
	FTorqueSprayShot Shot;
	Shot.bGun = Tool == ETorqueTool::Gun;
	Shot.Dir = Camera->GetForwardVector();
	Shot.Nozzle = ToolHolder && ToolHolder->IsVisible() ? (FTransform(ToolNozzle) * ToolHolder->GetComponentTransform()).GetLocation()
		: Camera->GetComponentLocation() + Shot.Dir * 50.0;
	Shot.Colour = SprayMistColour(&Shot.bClear);
	Shot.Radius = Shot.bGun ? GunRadius : CanRadius;
	// Where it lands, when there's no coat to say (another player's spray, or a coat done by the hold).
	auto Trace = [this, &Shot]()
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueSprayFxAim), false, this);
		FHitResult Hit;
		const FVector Eye = Camera->GetComponentLocation();
		Shot.bHit = GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Shot.Dir * 320.0, ECC_Visibility, Params);
		if (!Shot.bHit) return;
		Shot.At = Hit.ImpactPoint;
		Shot.Normal = Hit.ImpactNormal;
		const double D = FVector::Dist(Eye, Shot.At);
		Shot.Zone = D < Near ? EZone::Close : (D > Far + OutPast ? EZone::Out : (D > Far ? EZone::Far : EZone::Good));
	};
	if (IsLocallyControlled())
	{
		Shot.bFiring = bSprayFiring || (Progress > 0.0 && IsSprayPlan(Plan) && !Spray.IsValid());
		Shot.bAiming = SprayItem != 0 && !Shot.bFiring;
		if (Spray.IsValid() && (bSprayFiring || SprayItem != 0))
		{
			const FJet& J = Spray->Jet;
			Shot.bHit = J.bHit;
			Shot.At = J.At;
			Shot.Normal = J.Normal;
			Shot.Radius = J.Radius;
			Shot.Zone = J.bHit ? J.Zone : EZone::Out;
			Shot.Dry = J.Dry;
		}
		else if (Shot.bFiring) Trace();
	}
	else
	{
		Shot.bFiring = bTool && Progress > 0.0 && ToolHolder && ToolHolder->IsVisible();
		if (Shot.bFiring) Trace();
	}
	// Seen from this machine's eye (another player's spray too).
	FVector Eye = Camera->GetComponentLocation();
	if (const UWorld* World = GetWorld())
		if (const APlayerController* Local = World->GetFirstPlayerController(); Local && Local->PlayerCameraManager)
			Eye = Local->PlayerCameraManager->GetCameraLocation();
	SprayFx->Update(Dt, Shot, Eye);
}

FString ATorqueWalker::SprayControlsText() const
{
	return SprayHook() != INDEX_NONE
		? FString(TEXT("Hold left mouse (Cross): sweep in steady passes   Right mouse (L2): steady your hand   Q / E or wheel (L1 / R1): hook down / up   X (D-pad up): what's missed   F (Triangle): step back"))
		: FString(TEXT("Hold left mouse (Cross): sweep in steady passes; go round it and crouch for underneath   Right mouse (L2): steady your hand   X (D-pad up): what's missed   F (Triangle): step back"));
}

// --- The readout (spray_hud.gd) ---------------------------------------------------------------------

bool ATorqueWalker::SprayHudUp() const
{
	return (SprayShown.IsValid() && (SprayLinger > 0.0 || SprayItem != 0)) || SprayItem != 0 || SprayCueAge < TWSI::CueLife || SprayFlash > 0.0;
}

void ATorqueWalker::DrawSpray(AHUD* Hud, UCanvas* Canvas) const
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
	// The lettering drawn at its real size from the engine's font (not a small bitmap scaled up, which
	// goes soft), with a thin dark outline where it sits over the view.
	auto FontFor = [Font](float Size, bool bOutline)
	{
		FSlateFontInfo Info(Font, FMath::Max(1, FMath::RoundToInt((float)FMath::Max(Font->LegacyFontSize, 8) * Size * 0.75f)));
		if (bOutline)
		{
			Info.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(Size * 1.6f));
			Info.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.85f);
		}
		return Info;
	};
	// (An offline, bitmap font can't be drawn that way: then it's scaled as before.)
	const bool bCrisp = Font->FontCacheType == EFontCacheType::Runtime && FSlateApplication::IsInitialized();
	auto Measure = [&FontFor, Canvas, Font, bCrisp](const FString& T, float Scale) -> FVector2D
	{
		if (bCrisp)
		{
			const FVector2D M(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(T, FontFor(Scale, false)));
			return M;
		}
		float W = 0.0f, H = 0.0f;
		Canvas->StrLen(Font, T, W, H);
		return FVector2D(W * Scale, H * Scale);
	};
	// Text by its top left; Align 0 left, 1 centred in Width, 2 right-aligned to X + Width.
	auto Text = [Canvas, Font, bCrisp, &FontFor, &Measure](const FString& T, float X, float Y, float Size, const FLinearColor& C, int32 Align = 0, float Width = 0.0f, bool bShadow = false)
	{
		const FVector2D M = Measure(T, Size);
		float TX = X;
		if (Align == 1) TX = X + (Width - (float)M.X) * 0.5f;
		else if (Align == 2) TX = X + Width - (float)M.X;
		if (!bCrisp)
		{
			Canvas->SetDrawColor(C.ToFColor(true));
			Canvas->DrawText(Font, T, TX, Y, Size, Size);
			return;
		}
		FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(TX), FMath::RoundToFloat(Y)), FText::FromString(T), FontFor(Size, bShadow), C);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	};
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	// The flash when a coat's on, and when it's done.
	if (SprayFlash > 0.0) Rect(FLinearColor(1.0f, 1.0f, 0.96f, (float)SprayFlash * 0.5f), 0.0f, 0.0f, CW, CH);
	const TSharedPtr<FTorqueSprayJob> Job = SprayShown.IsValid() ? SprayShown : Spray;
	if (Job.IsValid())
	{
		const FStats St = Job->Last.Seen > 0 ? Job->Last : Job->Stats();
		const bool bReady = St.Covered >= Done;
		const FJet& Jet = Job->Jet;
		// Across the top: how much is covered (with the mark where the coat counts as on).
		const float W = FMath::Min(660.0f * S, CW - 48.0f * S);
		const float X0 = (CW - W) * 0.5f, Y0 = 96.0f * S, H0 = 220.0f * S;
		Rect(FLinearColor(0.03f, 0.04f, 0.06f, 0.8f), X0, Y0, W, H0);
		Rect(TWSI::Accent, X0, Y0, W, 3.0f * S);
		const FString Load = SprayFinishText().ToUpper();
		const FString What = Job->Layer == ELayer::Primer ? TEXT("PRIMER") : (Job->Layer == ELayer::Clear ? TEXT("LACQUER") : TEXT("COLOUR"));
		FString Title = FString::Printf(TEXT("SPRAYING %s"), *What);
		if (Job->Request.Kind == ETorqueShedAct::Paint)
			Title = Job->Request.Finish == UTorqueBuildSubsystem::CupFinish() ? FString(TEXT("SPRAYING  -  FROM THE CUP"))
				: FString::Printf(TEXT("SPRAYING  -  %s %s"), *Load, *UTorqueBuildSubsystem::PaintName(Job->Request.Colour).ToUpper());
		const float In = 16.0f * S, Inner = W - 2.0f * In;
		Text(Title, X0 + In, Y0 + 12.0f * S, Px(18.0f), TWSI::Accent);
		Text(FString::Printf(TEXT("%d%% COVERED"), FMath::RoundToInt(St.Covered * 100.0f)), X0 + In, Y0 + 8.0f * S, Px(24.0f),
			bReady ? TWSI::Good : FLinearColor::White, 2, Inner);
		const float BarY = Y0 + 40.0f * S, BarH = 16.0f * S;
		Rect(FLinearColor(0, 0, 0, 0.55f), X0 + In, BarY, Inner, BarH);
		Rect(bReady ? TWSI::Good : FLinearColor(0.78f, 0.86f, 0.95f), X0 + In, BarY, Inner * FMath::Clamp(St.Covered, 0.0f, 1.0f), BarH);
		Rect(FLinearColor::White, X0 + In + Inner * Done - 1.0f * S, BarY - 3.0f * S, 2.0f * S, BarH + 6.0f * S);
		Frame(FLinearColor(1, 1, 1, 0.25f), X0 + In, BarY, Inner, BarH, 1.0f);
		Text(B->ItemTitle(Job->Item), X0 + In, Y0 + 62.0f * S, Px(14.0f), TWSI::Dim);
		Text(bReady ? FString(TEXT("Let go to finish the coat")) : FString::Printf(TEXT("On at %d%%"), FMath::RoundToInt(Done * 100.0f)),
			X0 + In, Y0 + 62.0f * S, Px(14.0f), bReady ? TWSI::Good : TWSI::Dim, 2, Inner);
		// Evenness, and what's spoiling it.
		float Y = Y0 + 88.0f * S;
		Text(TEXT("EVENNESS"), X0 + In, Y, Px(13.0f), TWSI::Dim);
		const float EX = X0 + In + 90.0f * S, EW = Inner * 0.42f, EH = 12.0f * S;
		Rect(FLinearColor(0, 0, 0, 0.55f), EX, Y + 2.0f * S, EW, EH);
		Rect(FMath::Lerp(TWSI::Bad, TWSI::Good, FMath::Clamp((St.Even - 0.5f) * 2.0f, 0.0f, 1.0f)), EX, Y + 2.0f * S, EW * FMath::Clamp(St.Even, 0.0f, 1.0f), EH);
		Frame(FLinearColor(1, 1, 1, 0.25f), EX, Y + 2.0f * S, EW, EH, 1.0f);
		Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(St.Even * 100.0f)), EX + EW + 8.0f * S, Y, Px(14.0f), FLinearColor::White);
		const FLinearColor FaultC = St.Runs > 0.02f ? TWSI::Bad : (St.Dry > 0.03f ? TWSI::Cool : TWSI::Dim);
		Text(FString::Printf(TEXT("Runs %d%%   Dry %d%%   Thin %d%%"), FMath::RoundToInt(St.Runs * 100.0f), FMath::RoundToInt(St.Dry * 100.0f),
			FMath::RoundToInt(St.Thin * 100.0f)), X0 + In, Y, Px(14.0f), FaultC, 2, Inner);
		// How far off you're holding it, and how fast you're sweeping: gauges with a marker.
		Y += 30.0f * S;
		const float Half = (Inner - 20.0f * S) * 0.5f;
		auto Marker = [&Rect, S](float X, float GY, float GH)
		{
			for (int32 K = 0; K < 6; ++K) Rect(FLinearColor::White, X - (6 - K) * S, GY - 7.0f * S + K * S, (12 - 2 * K) * S, 1.0f * S);
			Rect(FLinearColor::White, X - 1.0f * S, GY, 2.0f * S, GH);
		};
		{
			// Distance: too close (red), the sweet spot (green), too far (blue, dry), out of reach.
			const FVector2D Sw = Job->Sweet();
			const double Span = Sw.Y + OutPast + 30.0;
			const double Dist = Jet.bHit ? Jet.Dist : Span;
			const EZone Z = Jet.bHit ? Jet.Zone : EZone::Out;
			const float GX = X0 + In, GY = Y + 20.0f * S, GW = Half, GH = 12.0f * S;
			auto At = [GX, GW, Span](double M) { return GX + GW * (float)FMath::Clamp(M / Span, 0.0, 1.0); };
			Text(TEXT("DISTANCE"), GX, Y, Px(13.0f), TWSI::Dim);
			const FString Label = !Jet.bHit ? FString(TEXT("OFF THE PART")) : (Z == EZone::Out ? FString(ZoneWord(Z)) : FString::Printf(TEXT("%.2f m  %s"), Dist / 100.0, ZoneWord(Z)));
			Text(Label, GX, Y, Px(13.0f), TWSI::ZoneColour(Z), 2, GW);
			Rect(FLinearColor(TWSI::Bad.R, TWSI::Bad.G, TWSI::Bad.B, 0.7f), GX, GY, At(Sw.X) - GX, GH);
			Rect(FLinearColor(TWSI::Good.R, TWSI::Good.G, TWSI::Good.B, 0.8f), At(Sw.X), GY, At(Sw.Y) - At(Sw.X), GH);
			Rect(FLinearColor(TWSI::Cool.R, TWSI::Cool.G, TWSI::Cool.B, 0.6f), At(Sw.Y), GY, At(Sw.Y + OutPast) - At(Sw.Y), GH);
			Rect(FLinearColor(0.3f, 0.3f, 0.3f, 0.6f), At(Sw.Y + OutPast), GY, GX + GW - At(Sw.Y + OutPast), GH);
			Frame(FLinearColor(1, 1, 1, 0.25f), GX, GY, GW, GH, 1.0f);
			Marker(At(Dist), GY, GH);
			Text(TEXT("close"), GX, GY + GH + 2.0f * S, Px(11.0f), TWSI::Dim);
			Text(TEXT("far"), GX, GY + GH + 2.0f * S, Px(11.0f), TWSI::Dim, 2, GW);
		}
		{
			// Pace: lingering (amber, it piles on), steady (green), too fast (blue, it goes on thin).
			const double Span = Fast * 1.6;
			const float GX = X0 + In + Half + 20.0f * S, GY = Y + 20.0f * S, GW = Half, GH = 12.0f * S;
			auto At = [GX, GW, Span](double V) { return GX + GW * (float)FMath::Clamp(V / Span, 0.0, 1.0); };
			Text(TEXT("PACE"), GX, Y, Px(13.0f), TWSI::Dim);
			FString Label = TEXT("-");
			FLinearColor PC = TWSI::Dim;
			if (Jet.bHit)
			{
				Label = FString::Printf(TEXT("%.1f m/s  %s"), Jet.Speed / 100.0,
					Jet.Pace == EPace::Good ? TEXT("STEADY") : (Jet.Pace == EPace::Slow ? TEXT("LINGERING: it'll run") : TEXT("TOO FAST: it'll go on thin")));
				PC = Jet.Pace == EPace::Good ? TWSI::Good : (Jet.Pace == EPace::Slow ? TWSI::Warn : TWSI::Cool);
			}
			Text(Label, GX, Y, Px(13.0f), PC, 2, GW);
			Rect(FLinearColor(TWSI::Warn.R, TWSI::Warn.G, TWSI::Warn.B, 0.7f), GX, GY, At(Slow) - GX, GH);
			Rect(FLinearColor(TWSI::Good.R, TWSI::Good.G, TWSI::Good.B, 0.8f), At(Slow), GY, At(Fast) - At(Slow), GH);
			Rect(FLinearColor(TWSI::Cool.R, TWSI::Cool.G, TWSI::Cool.B, 0.6f), At(Fast), GY, GX + GW - At(Fast), GH);
			Frame(FLinearColor(1, 1, 1, 0.25f), GX, GY, GW, GH, 1.0f);
			Marker(At(Jet.Speed), GY, GH);
			Text(TEXT("slow"), GX, GY + GH + 2.0f * S, Px(11.0f), TWSI::Dim);
			Text(TEXT("fast"), GX, GY + GH + 2.0f * S, Px(11.0f), TWSI::Dim, 2, GW);
		}
		{
			// Each side of the part: this side, the far side, the top and underneath (paint only lands
			// on what faces the gun). The worst still to do is lit.
			const TCHAR* Names[4] = {TEXT("THIS SIDE"), TEXT("FAR SIDE"), TEXT("TOP"), TEXT("UNDERNEATH")};
			TArray<int32> Shown;
			int32 Worst = INDEX_NONE;
			float Low = 2.0f;
			for (int32 K = 0; K < 4; ++K)
			{
				if (St.Sides[K] < 0.0f) continue;
				Shown.Add(K);
				if (St.Sides[K] < Low)
				{
					Low = St.Sides[K];
					Worst = K;
				}
			}
			const float SY = Y0 + H0 - 28.0f * S;
			const float SW = Shown.Num() > 0 ? Inner / Shown.Num() : Inner;
			for (int32 I = 0; I < Shown.Num(); ++I)
			{
				const int32 K = Shown[I];
				const FLinearColor C = St.Sides[K] >= Done ? TWSI::Good : (K == Worst ? TWSI::Warn : FLinearColor::White);
				Text(FString::Printf(TEXT("%s %d%%"), Names[K], FMath::RoundToInt(St.Sides[K] * 100.0f)), X0 + In + I * SW, SY, Px(14.0f), C, 1, SW);
			}
		}
		// The part's coats in order under the panel: primer (optional), colour, lacquer (optional).
		{
			const TArray<FStep> Steps = Job->Steps(*B);
			const float Gap = 10.0f * S, SY = Y0 + H0 + 8.0f * S, SH = 34.0f * S;
			const float BW = Steps.Num() > 0 ? (W - Gap * (Steps.Num() - 1)) / Steps.Num() : W;
			for (int32 I = 0; I < Steps.Num(); ++I)
			{
				const FStep& St2 = Steps[I];
				const float BX = X0 + I * (BW + Gap);
				FLinearColor Edge(1, 1, 1, 0.2f), Col = TWSI::Dim;
				FString Sub = St2.bOptional ? FString(TEXT("optional")) : FString();
				FString Name = St2.Name.ToUpper();
				switch (St2.State)
				{
				case FStep::Now:
					Edge = TWSI::Accent;
					Col = FLinearColor::White;
					Sub = FString::Printf(TEXT("now: %d%%"), FMath::RoundToInt(St.Covered * 100.0f));
					Name = TEXT("> ") + Name;
					break;
				case FStep::DoneStep:
					Edge = TWSI::Good;
					Col = TWSI::Good;
					Sub = FString::Printf(TEXT("done %d%%"), FMath::RoundToInt(St2.Q * 100.0f));
					Name = TEXT("+ ") + Name;
					break;
				case FStep::Skip:
					Sub = TEXT("skipped");
					break;
				default:
					break;
				}
				Rect(FLinearColor(0.03f, 0.04f, 0.06f, 0.75f), BX, SY, BW, SH);
				Frame(Edge, BX, SY, BW, SH, (St2.State == FStep::Now ? 2.0f : 1.0f) * S);
				Text(Name, BX + 10.0f * S, SY + 8.0f * S, Px(15.0f), Col);
				Text(Sub, BX, SY + 9.0f * S, Px(13.0f), St2.State == FStep::Todo ? TWSI::Dim : Col, 2, BW - 10.0f * S);
			}
		}
		// How to work it, along the bottom.
		const FString Help = SprayControlsText();
		Text(Help, 0.0f, CH - 130.0f * S, Px(15.0f), FLinearColor(1, 1, 1, 0.8f), 1, CW, true);
	}
	// Getting set before the trigger works, then how long you can hold your hand steady.
	if (SprayItem != 0)
	{
		const float MidY = CH * 0.5f + 70.0f * S, MidX = CW * 0.5f;
		if (SpraySettle > 0.0)
		{
			const float F = 1.0f - (float)(SpraySettle / Settle);
			Text(TEXT("Get set..."), 0.0f, MidY - 30.0f * S, Px(26.0f), FLinearColor::White, 1, CW, true);
			Rect(FLinearColor(0, 0, 0, 0.55f), MidX - 90.0f * S, MidY + 6.0f * S, 180.0f * S, 6.0f * S);
			Rect(TWSI::Accent, MidX - 90.0f * S, MidY + 6.0f * S, 180.0f * S * F, 6.0f * S);
		}
		else
		{
			if (!(Spray.IsValid() && Spray->bLive))
				Text(TEXT("Ready: hold to spray"), 0.0f, MidY - 22.0f * S, Px(18.0f), TWSI::Good, 1, CW, true);
			const float BW = 120.0f * S;
			Rect(FLinearColor(0, 0, 0, 0.5f), MidX - BW * 0.5f, MidY + 10.0f * S, BW, 5.0f * S);
			const FLinearColor BC = SprayBreath > 0.2 ? (bSpraySteady ? TWSI::Good : TWSI::Cool) : TWSI::Warn;
			Rect(BC, MidX - BW * 0.5f, MidY + 10.0f * S, BW * (float)SprayBreath, 5.0f * S);
			Text(bSpraySteady ? TEXT("steady") : TEXT("breath"), 0.0f, MidY + 18.0f * S, Px(11.0f), TWSI::Dim, 1, CW);
		}
	}
	// Runs, dry spray, the coat being on and the coat done pop up in the middle of the screen.
	if (SprayCueAge < TWSI::CueLife)
	{
		const float Age = (float)SprayCueAge;
		const float Pop = 1.0f + 0.3f * FMath::Max(0.0f, 1.0f - Age * 5.0f);
		const float A = FMath::Clamp(((float)TWSI::CueLife - Age) / 0.5f, 0.0f, 1.0f);
		FLinearColor C = FLinearColor::White;
		switch (SprayCue.Kind)
		{
		case ECue::Runs: C = TWSI::Bad; break;
		case ECue::Dry: C = TWSI::Cool; break;
		case ECue::Ready: C = TWSI::Good; break;
		case ECue::Done: C = FLinearColor(1.0f, 0.85f, 0.3f); break;
		}
		C.A = A;
		const float Big = (SprayCue.Kind == ECue::Done ? 46.0f : 36.0f) * Pop;
		const float MidY = FMath::Max(CH * 0.45f, 390.0f * S);
		Text(SprayCue.Text, 0.0f, MidY - Px(Big) * 10.0f, Px(Big), C, 1, CW, true);
	}
}

// --- A test hook ----------------------------------------------------------------------------------

FString ATorqueWalker::TestSprayPasses(int32 Passes, double Distance)
{
	UTorqueBuildSubsystem* B = Shed();
	UWorld* World = GetWorld();
	if (!B || !World) return TEXT("no shed");
	if (!TWSI::Sprays(Tool)) return TEXT("not holding the can or gun");
	ReAim();
	FindTarget();
	MakePlan();
	if (SprayItem == 0 && !SprayInteract()) return FString::Printf(TEXT("no spray controls: %s"), Plan.Blocked.IsEmpty() ? TEXT("nothing to spray there") : *Plan.Blocked);
	MakePlan();
	if (!IsSprayPlan(Plan)) return TEXT("nothing to spray");
	if (!Plan.Blocked.IsEmpty()) return Plan.Blocked;
	const FTorqueShedRequest R = Plan.Request;
	const FName Slot = SpraySlot;
	TSharedPtr<FTorqueSprayJob> Job = SprayJobFor(R.Item, R, Slot.IsNone() ? INDEX_NONE : R.Bay, Slot);
	if (!Job.IsValid()) return TEXT("the part isn't drawn to spray");
	Spray = Job;
	const bool bGun = R.bGun;
	Job->SetTool(bGun ? GunRadius : CanRadius, (bGun ? GunRate : CanRate) * ModeFlow, bGun ? GunFalloff : CanFalloff, 1.0);
	// Steady passes across the part from where the walker stands, row over row half a fan apart, then
	// the same from the other three sides and from above, as a player walking round it would.
	FBox Box(ForceInit);
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : Job->Meshes())
		if (const UStaticMeshComponent* C = W.Get()) Box += C->Bounds.GetBox();
	if (!Box.IsValid) return TEXT("no box to sweep");
	const FVector Mid = Box.GetCenter(), Ext = Box.GetExtent();
	const FVector Toward = (Camera->GetComponentLocation() - Mid).GetSafeNormal2D();
	const FVector Sides[5] = {Toward, -Toward, FVector::CrossProduct(Toward, FVector::UpVector), -FVector::CrossProduct(Toward, FVector::UpVector), FVector::UpVector};
	const double Dt = 1.0 / 60.0, Speed = 90.0, Fan = (bGun ? GunRadius : CanRadius) * FMath::Clamp(Distance / DRef, 0.6, 1.8);
	int32 Frames = 0;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		const FVector Facing = Sides[Pass % 5];
		FVector Across = FVector::CrossProduct(Facing, FVector::UpVector).GetSafeNormal();
		if (Across.IsNearlyZero()) Across = FVector::ForwardVector;
		const FVector Down = FVector::CrossProduct(Across, Facing).GetSafeNormal();
		const double HalfW = FMath::Abs(FVector::DotProduct(Ext, Across.GetAbs())) + Fan;
		const double HalfH = FMath::Abs(FVector::DotProduct(Ext, Down.GetAbs())) + Fan * 0.5;
		const double Depth = FMath::Abs(FVector::DotProduct(Ext, Facing.GetAbs()));
		int32 Row = 0;
		for (double V = -HalfH; V <= HalfH; V += Fan * 0.9, ++Row)
		{
			const double Way = (Row % 2) ? -1.0 : 1.0;
			for (double U = -HalfW; U <= HalfW; U += Speed * Dt)
			{
				const FVector Aim = Mid + Across * (U * Way) + Down * V;
				const FVector Eye = Aim + Facing * (Depth + Distance);
				FVector SAt, SN;
				double T = 0.0;
				const bool bHit = Job->SurfaceHit(Eye, -Facing, 3.5, SAt, SN, T);
				Job->Step(Dt, Eye, -Facing, bHit, bHit ? SAt : Aim, bHit ? SN : Facing, true);
				++Frames;
				if (Job->Complete()) break;
			}
			if (Job->Complete()) break;
		}
		if (Job->Complete()) break;
	}
	const FStats St = Job->Stats();
	FString Said = FString::Printf(TEXT("[%s, %d cubes painted, overlay on %d meshes] "), *Job->Key, Job->PaintedCount(), Job->Meshes().Num()) + FString::Printf(TEXT("%d frames of spraying: %d%% covered, even %d%%, runs %d%%, dry %d%%, thin %d%% (sides %d/%d/%d/%d)"), Frames,
		FMath::RoundToInt(St.Covered * 100.0f), FMath::RoundToInt(St.Even * 100.0f), FMath::RoundToInt(St.Runs * 100.0f), FMath::RoundToInt(St.Dry * 100.0f),
		FMath::RoundToInt(St.Thin * 100.0f), FMath::RoundToInt(St.Sides[0] * 100.0f), FMath::RoundToInt(St.Sides[1] * 100.0f),
		FMath::RoundToInt(St.Sides[2] * 100.0f), FMath::RoundToInt(St.Sides[3] * 100.0f));
	Job->Tick(0.1);   // (up to the GPU, for a shot)
	if (Job->Ready())
	{
		FinishSpray(Job);
		Said += TEXT("; let go: ") + Note;
	}
	else Said += TEXT("; not on yet, left wet for another pass");
	return Said;
}
