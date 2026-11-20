// [garage] The ridden bike's side of the forecourt garage (World/TorqueForecourtGarage.h; beenster, Nov 17
// 2026: "When you get to a petrol station there is a garage unit that you can ride your bike into, make
// room for 3 bikes, then when in this room you can use your spanner and a shop computer mounted on a
// wall"; "You can't spanner your bike on the roadside"). Off the bike inside a garage, with the bike
// parked in it too:
//   - look at a part of the bike and hold Work (left mouse, Cross / A): the spanner takes it off, and it
//     goes back to the shed's delivery area; with a part in your hands, the spanner fits it there and
//     bolts it up (the slot looked at if it goes there, else where it goes);
//   - look at the wall computer and press Work: the parts computer's page, as the shed's; ordering a
//     part buys it straight into your hands, unboxed;
//   - AltWork (right mouse / R, Square / X) sends the part in your hands back to the shed.
// The jobs are the shed's requests (GarageBuy, GarageFit, GarageOff, GarageStow: TorqueGarageActs.cpp),
// so the host decides and co-op shares them; the bike is drawn again from the shed when its parts change.
// Anywhere else the spanner stays in its pocket.
#include "TorqueBikePawn.h"

#include "Coop/TorqueKeys.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueUi.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorquePartsComputer.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueToolModels.h"
#include "Camera/CameraComponent.h"
#include "World/TorqueForecourtGarage.h"
#include "World/TorqueRideWorld.h"
#include "World/WorldRide.h"
#include "TorqueTft.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

#include <cmath>

using namespace TorqueSim;

namespace TorqueBikeGarageImpl
{
constexpr double Reach = 230.0;     // the spanner's and the computer's reach from the eyes (cm)
constexpr double SpannerTime = 1.2; // Work held this long does the job (s)
constexpr double EyeUp = 165.0;

// (as TorqueBikePawn.cpp's: sim metres to Unreal cm, the sim's basis to Unreal's turn)
FVector ToUE(const V3& P) { return FVector(P.x * 100.0, P.z * 100.0, P.y * 100.0); }
FQuat BasisToUE(const M3& B)
{
	return FRotationMatrix::MakeFromXZ(FVector(-B.Z.x, -B.Z.z, -B.Z.y), FVector(B.Y.x, B.Y.z, B.Y.y)).ToQuat();
}

// The ray's way into a box (its distance along Dir from Start), or -1 when it misses it within Len.
double Into(const FBox& Box, const FVector& Start, const FVector& Dir, double Len)
{
	double T0 = 0.0, T1 = Len;
	for (int32 A = 0; A < 3; ++A)
	{
		const double S = Start[A], D = Dir[A], Lo = Box.Min[A], Hi = Box.Max[A];
		if (FMath::Abs(D) < 1e-9)
		{
			if (S < Lo || S > Hi) return -1.0;
			continue;
		}
		double Ta = (Lo - S) / D, Tb = (Hi - S) / D;
		if (Ta > Tb) Swap(Ta, Tb);
		T0 = FMath::Max(T0, Ta);
		T1 = FMath::Min(T1, Tb);
		if (T0 > T1) return -1.0;
	}
	return T0;
}

FName SlotOf(const UStaticMeshComponent* C)
{
	for (const FName& Tag : C->ComponentTags)
	{
		const FString T = Tag.ToString();
		if (T.StartsWith(TEXT("Slot:"))) return FName(*T.Mid(5));
	}
	return NAME_None;
}
} // namespace TorqueBikeGarageImpl
namespace TBGI = TorqueBikeGarageImpl;

bool ATorqueBikePawn::InGarage() const
{
	return bOnFoot && GarageHere.IsValid();
}

bool ATorqueBikePawn::GarageComputerUpOrJustClosed() const
{
	const UWorld* World = GetWorld();
	return bGarageComputer || (World && World->GetTimeSeconds() - GarageComputerShut < 0.25);
}

FString ATorqueBikePawn::PartsKey() const
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	TMap<FString, FString> Parts;
	if (!B || RideBay < 0 || !B->LayoutParts(RideBay, Parts)) return FString();
	Parts.KeySort(TLess<FString>());
	FString Out;
	for (const TPair<FString, FString>& KV : Parts) Out += KV.Key + TEXT("=") + KV.Value + TEXT(";");
	return Out;
}

void ATorqueBikePawn::BindGarage()
{
	if (bGarageBound || bYard) return;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	bGarageBound = true;
	BuiltParts = PartsKey();
	B->OnChanged.AddUObject(this, &ATorqueBikePawn::OnShedChanged);
	B->OnTell.AddUObject(this, &ATorqueBikePawn::OnShedTell);
}

void ATorqueBikePawn::OnShedChanged(int32 Bay)
{
	if (Bay != RideBay || !bCatalogueModel) return;
	if (PartsKey() != BuiltParts) bModelStale = true;   // (drawn again once it's parked: Tick)
}

void ATorqueBikePawn::OnShedTell(int32 Peer, const FString& Text, bool bOk)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B || Peer != B->Me() || !IsLocallyControlled()) return;
	if (GestureNow() - GarageAskedAt > 6.0) return;   // (only what answers our own garage jobs)
	GarageSaid = Text;
	bGarageSaidOk = bOk;
	GarageSaidFor = 5.0;
	++GarageTells;
}

void ATorqueBikePawn::RebuildBikeModel()
{
	bModelStale = false;
	if (!bCatalogueModel) return;
	// Kept as it is: where the bike stands, its engine and gear, the petrol in it.
	const bool bEngine = Sim->EngineOn;
	const int32 GearWas = Sim->Gear;
	const double FuelWas = Dash.FuelLitres;
	// Down: every drawn part, and the holder they were drawn under (renamed first: the new one takes its name).
	TArray<USceneComponent*> Old;
	for (UStaticMeshComponent* C : ModelParts)
		if (C) Old.AddUnique(C);
	TArray<USceneComponent*> Mine;
	GetComponents(Mine);
	for (USceneComponent* C : Mine)
		if (C && C->GetFName() == FName(TEXT("CatalogueRoot")))
		{
			TArray<USceneComponent*> Under;
			C->GetChildrenComponents(true, Under);
			for (USceneComponent* U : Under) Old.AddUnique(U);
			C->Rename(*MakeUniqueObjectName(this, C->GetClass(), TEXT("CatalogueRootOld")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
			Old.AddUnique(C);
		}
	for (USceneComponent* C : Old)
		if (IsValid(C)) C->DestroyComponent();
	ModelParts.Reset();
	for (int32& N : GroupParts) N = 0;
	for (int32 S = 0; S < 2; ++S)
	{
		CatLever[S] = nullptr;
		bCatLeverRest[S] = false;
	}
	bCatalogueModel = false;
	BuildModel();
	BuiltParts = PartsKey();
	// The sim took the new spec (its weight, power, wheels...): put back what's kept, stood where it was.
	Sim->EngineOn = bEngine;
	Sim->Gear = GearWas;
	Dash.FuelLitres = FMath::Clamp(FuelWas, 0.0, Dash.FuelCapacity);
	if (bOnFoot || bParkedLook)
	{
		ChooseStand();
		HoldParked();
		PrevPos = CurPos = TBGI::ToUE(Sim->Position);
		PrevRot = CurRot = TBGI::BasisToUE(Sim->Basis);
		SetParkedLook(true);
	}
	bRiderFitted = false;   // (sat on it again, on its new seat and bars)
	UE_LOG(LogTemp, Log, TEXT("Torque garage: bay %d's bike drawn again (%d parts)"), RideBay + 1, ModelParts.Num());
}

void ATorqueBikePawn::GarageAim()
{
	GarageSlot = NAME_None;
	bGarageLookComputer = false;
	const ATorqueForecourtGarage* G = GarageHere.Get();
	if (!G) return;
	const FVector Eye = FootPos + FVector(0.0, 0.0, TBGI::EyeUp);
	const FVector Dir = FRotator(FootPitch, FootYaw, 0.0).Vector();
	// The wall computer: close by and looked at.
	{
		const FVector To = G->ComputerAt() - Eye;
		const double D = To.Size();
		bGarageLookComputer = D < TBGI::Reach && FVector::DotProduct(To / FMath::Max(D, 1.0), Dir) > 0.9;
	}
	// The bike's parts the look meets: the nearest, or a smaller one just past it (a part's box takes in
	// more than the part, and the big ones' boxes take in the small ones on them).
	struct FLook { FName Slot; double At, Size; };
	TArray<FLook> Hits;
	double Nearest = 1e9;
	for (const UStaticMeshComponent* C : ModelParts)
	{
		if (!C || !C->IsRegistered() || !C->IsVisible() || !C->GetStaticMesh()) continue;
		const FName Slot = TBGI::SlotOf(C);
		if (Slot.IsNone()) continue;
		const FBox Box = C->Bounds.GetBox();
		const double At = TBGI::Into(Box.ExpandBy(1.0), Eye, Dir, TBGI::Reach);
		if (At < 0.0) continue;
		Hits.Add({Slot, At, Box.GetVolume()});
		Nearest = FMath::Min(Nearest, At);
	}
	double Smallest = 1e30;
	for (const FLook& H : Hits)
		if (H.At <= Nearest + 25.0 && H.Size < Smallest)
		{
			Smallest = H.Size;
			GarageSlot = H.Slot;
		}
	if (!GarageSlot.IsNone()) bGarageLookComputer = false;
}

bool ATorqueBikePawn::GarageRequest(uint8 Kind, FName Slot, FName Part, const FString& Doing)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return false;
	FTorqueShedRequest R;
	R.Kind = (ETorqueShedAct)Kind;
	R.Bay = RideBay;
	R.Slot = Slot;
	R.Part = Part;
	GarageAskedAt = GestureNow();
	const int32 TellsWas = GarageTells;
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		GarageSaid = WhyNot;
		bGarageSaidOk = false;
		GarageSaidFor = 4.0;
		return false;
	}
	if (GarageTells == TellsWas)
	{
		// (a client: the host's answer comes back with the shed)
		GarageSaid = Doing;
		bGarageSaidOk = true;
		GarageSaidFor = 3.0;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque garage: asked %d (bay %d, slot %s, part %s): %s"), (int32)Kind, RideBay + 1, *Slot.ToString(), *Part.ToString(), *GarageSaid);
	return true;
}

void ATorqueBikePawn::OpenGarageComputer(APlayerController* PC)
{
	if (!GarageComputer) GarageComputer = new FTorquePartsComputer();
	GarageComputer->Open();
	bGarageComputer = true;
	bGarageWorkArmed = false;
	if (PC)
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	UE_LOG(LogTemp, Log, TEXT("Torque garage: the wall computer opened"));
}

void ATorqueBikePawn::CloseGarageComputer(APlayerController* PC)
{
	if (!bGarageComputer) return;
	bGarageComputer = false;
	GarageComputerShut = GestureNow();
	if (PC)
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ATorqueBikePawn::GarageComputerDo(const FTorqueComputerAction& A)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	switch (A.Kind)
	{
	case FTorqueComputerAction::EKind::None:
		break;
	case FTorqueComputerAction::EKind::Close:
		CloseGarageComputer(PC);
		break;
	case FTorqueComputerAction::EKind::OrderPart:
		if (GarageRequest((uint8)ETorqueShedAct::GarageBuy, NAME_None, A.Part, FString::Printf(TEXT("Buying %s..."), *TorqueShedParts::PartName(A.Part))))
			CloseGarageComputer(PC);   // (it's in your hands: on to the bike)
		break;
	default:
		GarageSaid = TEXT("The garage's computer sells bike parts: bikes, kit and paint are ordered from the shed's computer.");
		bGarageSaidOk = false;
		GarageSaidFor = 4.0;
		break;
	}
}

bool ATorqueBikePawn::TickGarage(APlayerController* PC, float Dt, uint32 SonyDown, uint32 SonyPressed)
{
	GarageSaidFor = FMath::Max(GarageSaidFor - Dt, 0.0);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	ATorqueForecourtGarage* G = bYard ? nullptr : ATorqueForecourtGarage::Inside(GetWorld(), FootPos + FVector(0.0, 0.0, 50.0));
	GarageHere = G;
	const bool bWorkJust = FTorqueKeys::Just(PC, ETorqueKey::Work) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || (SonyPressed & ESonyButton::Cross);
	const bool bWorkDown = FTorqueKeys::Down(PC, ETorqueKey::Work) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom) || (SonyDown & ESonyButton::Cross);
	const bool bAltJust = FTorqueKeys::Just(PC, ETorqueKey::AltWork) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left) || (SonyPressed & ESonyButton::Square);
	if (!G || !B)
	{
		CloseGarageComputer(PC);
		GarageSlot = NAME_None;
		bGarageLookComputer = false;
		bGarageWorkArmed = false;
		// (beenster: "You can't spanner your bike on the roadside")
		if (bWorkJust && !bYard && !bPushing && FVector::Dist2D(FootPos, GetActorLocation()) <= 250.0)
		{
			FootNote = TEXT("The spanner stays in your pocket out here: ride into a garage unit at a petrol station to work on the bike.");
			FootNoteFor = 3.0;
		}
		return false;
	}
	if (bGarageComputer)
	{
		// The parts computer's page (as the shed walker works it): the mouse clicks its buttons and the
		// wheel scrolls; on a pad the D-pad picks, L1 / R1 the kind, Cross buys. F, Esc or Circle / B steps back.
		FVector2D Mouse(-1.0, -1.0);
		float Mx = 0.0f, My = 0.0f;
		if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
		auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
		if (GarageComputer->Typing())
		{
			FTorqueBikeConfigurator& Cfg = GarageComputer->NewBike();
			if (Just(EKeys::Enter) || Just(EKeys::Tab) || Just(EKeys::Escape)) Cfg.StopTyping();
			else
			{
				ATorquePlayerController::TypeText(PC, Cfg.TypingText(), 64);
				Cfg.Typed();
			}
			GarageComputer->Hold(Mouse, PC->IsInputKeyDown(EKeys::LeftMouseButton));
			if (Just(EKeys::LeftMouseButton)) GarageComputerDo(GarageComputer->Click(Mouse));
			return true;
		}
		const bool bF = FTorqueKeys::Just(PC, ETorqueKey::Use);
		if (bF || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (SonyPressed & ESonyButton::Circle))
		{
			if (!bF && GarageComputer->Back()) return true;
			CloseGarageComputer(PC);
			return true;
		}
		GarageComputer->Hold(Mouse, PC->IsInputKeyDown(EKeys::LeftMouseButton));
		if (Just(EKeys::RightMouseButton)) GarageComputer->RightClick(Mouse);
		if (Just(EKeys::LeftMouseButton))
		{
			GarageComputerDo(GarageComputer->Click(Mouse));
			return true;
		}
		if (Just(EKeys::MouseScrollDown)) GarageComputer->Scroll(Mouse, 1);
		if (Just(EKeys::MouseScrollUp)) GarageComputer->Scroll(Mouse, -1);
		int32 Step = 0;
		if (Just(EKeys::W) || Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (SonyPressed & ESonyButton::DUp)) Step = -1;
		if (Just(EKeys::S) || Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (SonyPressed & ESonyButton::DDown)) Step = 1;
		const bool bPadNext = Just(EKeys::E) || Just(EKeys::Gamepad_RightShoulder) || (SonyPressed & ESonyButton::R1);
		const bool bPadPrev = Just(EKeys::Q) || Just(EKeys::Gamepad_LeftShoulder) || (SonyPressed & ESonyButton::L1);
		const bool bChoose = Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (SonyPressed & ESonyButton::Cross);
		GarageComputerDo(GarageComputer->Pad(Step, bPadNext ? 1 : (bPadPrev ? -1 : 0), bChoose));
		return true;
	}
	GarageAim();
	const bool bHolding = B->HeldBy(B->Me()) != 0;
	if (bGarageLookComputer && bWorkJust)
	{
		OpenGarageComputer(PC);
		return true;
	}
	if (bAltJust && bHolding) GarageRequest((uint8)ETorqueShedAct::GarageStow, NAME_None, NAME_None, TEXT("Sending it back to the shed..."));
	const bool bBikeIn = !bPushing && G->Holds(GetActorLocation());
	if (!GarageSlot.IsNone() && bWorkJust && !bBikeIn)
	{
		GarageSaid = TEXT("Park the bike inside the garage to work on it.");
		bGarageSaidOk = false;
		GarageSaidFor = 3.0;
	}
	if (!GarageSlot.IsNone() && bBikeIn)
	{
		if (bWorkJust)
		{
			bGarageWorkArmed = true;
			GarageHeld = 0.0;
		}
		if (bGarageWorkArmed)
		{
			if (!bWorkDown) bGarageWorkArmed = false;
			else if ((GarageHeld += Dt) >= TBGI::SpannerTime)
			{
				bGarageWorkArmed = false;
				if (bHolding) GarageRequest((uint8)ETorqueShedAct::GarageFit, GarageSlot, NAME_None, TEXT("Fitting it..."));
				else GarageRequest((uint8)ETorqueShedAct::GarageOff, GarageSlot, NAME_None, TEXT("Taking it off..."));
			}
		}
	}
	else
		bGarageWorkArmed = false;
	return false;
}

void ATorqueBikePawn::GarageHudLines()
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!InGarage() || !B || bGarageComputer) return;
	const FColor ColTitle(255, 220, 120), ColBody(200, 230, 200), ColBad(255, 150, 110);
	const FString Work = FTorqueKeys::Text(ETorqueKey::Work), Alt = FTorqueKeys::Text(ETorqueKey::AltWork);
	const int32 Held = B->HeldBy(B->Me());
	if (GarageSaidFor > 0.0) HudLines.Add({GarageSaid, bGarageSaidOk ? ColTitle : ColBad, 1.15f});
	if (Held != 0)
		HudLines.Add({FString::Printf(TEXT("In your hands: %s.   Hold %s (Cross / A) on the bike to fit it;   %s (Square / X) sends it back to the shed."),
			*B->ItemTitle(Held), *Work, *Alt), ColBody, 1.0f});
	const ATorqueForecourtGarage* G = GarageHere.Get();
	if (G && !G->Holds(GetActorLocation()))
		HudLines.Add({TEXT("The garage: park your bike in here to work on it with the spanner."), ColBody, 1.0f});
	if (bGarageLookComputer)
		HudLines.Add({FString::Printf(TEXT("Parts computer:   %s (Cross / A) to buy a part"), *Work), ColBody, 1.0f});
	else if (!GarageSlot.IsNone())
	{
		const int32 On = B->ItemOn(RideBay, GarageSlot);
		FString Says = TorqueShedParts::SlotName(GarageSlot) + (On ? TEXT(": ") + B->ItemTitle(On) : FString(TEXT(": nothing fitted")));
		// What the spanner would do, or why it can't (the shed's own check).
		FTorqueShedRequest R;
		R.Kind = Held ? ETorqueShedAct::GarageFit : ETorqueShedAct::GarageOff;
		R.Bay = RideBay;
		R.Slot = GarageSlot;
		const FString Why = B->Check(B->Me(), R);
		if (!Why.IsEmpty()) Says += TEXT("   (") + Why + TEXT(")");
		else if (bGarageWorkArmed) Says += FString::Printf(TEXT("   spanner %d%%"), FMath::RoundToInt(100.0 * GarageHeld / TBGI::SpannerTime));
		else Says += FString::Printf(TEXT("   hold %s (Cross / A): %s"), *Work, Held ? TEXT("fit and bolt up what's in your hands") : TEXT("take it off (it goes back to the shed)"));
		HudLines.Add({Says, Why.IsEmpty() ? ColBody : ColBad, 1.0f});
	}
}

void ATorqueBikePawn::UpdateGarageTool(float Dt)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const bool bShow = IsLocallyControlled() && InGarage() && !bGarageComputer && !bPushing && B && B->HeldBy(B->Me()) == 0;
	if (!bShow && !GarageToolHolder) return;
	if (!GarageToolHolder && Camera)
	{
		GarageToolHolder = NewObject<USceneComponent>(this, TEXT("GarageToolHolder"));
		GarageToolHolder->SetupAttachment(Camera);
		GarageToolHolder->RegisterComponent();
		FVector Nozzle = FVector::ZeroVector;
		TorqueToolModels::Make(this, GarageToolHolder, TorqueToolModels::Wrench, FLinearColor::Black, GarageToolParts, Nozzle);
		for (UStaticMeshComponent* C : GarageToolParts)
			if (C)
			{
				C->SetCastShadow(false);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		UE_LOG(LogTemp, Log, TEXT("Torque garage: the spanner in hand (%d meshes)"), GarageToolParts.Num());
	}
	if (!GarageToolHolder) return;
	GarageToolHolder->SetVisibility(bShow, true);
	if (!bShow) return;
	// As the shed walker's spanner (TorqueWalker UpdateToolView): Torque Shed's hold, and while the spanner's
	// on a bolt, a quarter swing about the view, out and back, pushed onto the job.
	GarageWorkBlend = FMath::FInterpTo(GarageWorkBlend, bGarageWorkArmed ? 1.0 : 0.0, (double)Dt, 12.0);
	GarageToolClock += Dt;
	const double T = GarageToolClock, W = GarageWorkBlend;
	FTransform Hold = TorqueToolModels::HoldOf(TorqueToolModels::Wrench);
	const FRotator Turn(0.0, 0.0, (FMath::Sin(T * 8.0) * 0.5 + 0.5) * 35.0 * W);
	Hold.SetRotation(Turn.Quaternion() * Hold.GetRotation());
	Hold.AddToTranslation(FVector(6.0, -4.0, 3.0) * W);
	GarageToolHolder->SetRelativeTransform(Hold);
}

void ATorqueBikePawn::DrawGarageComputer(AHUD* Hud, UCanvas* Canvas) const
{
	if (!GarageComputer || !bGarageComputer) return;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	FVector2D Mouse(-1.0, -1.0);
	float Mx = 0.0f, My = 0.0f;
	if (PC && PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
	GarageComputer->Draw(Hud, Canvas, UTorqueBuildSubsystem::Of(this), Mouse, GarageSaidFor > 0.0 ? GarageSaid : FString(), bGarageSaidOk);
}

FString ATorqueBikePawn::TestGarage(const FString& What)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return TEXT("no shed");
	FString Verb = What, Arg;
	What.Split(TEXT(":"), &Verb, &Arg);
	if (Verb.IsEmpty()) Verb = What;
	if (Verb == TEXT("goto"))
	{
		// Our bike put in the nearest garage (its middle bay, facing in), off it, looking at it.
		const ATorqueRideWorld* Ride = nullptr;
		for (TActorIterator<ATorqueRideWorld> It(GetWorld()); It; ++It) { Ride = *It; break; }
		const TorqueWorld::RidePlan* Plan = Ride ? Ride->GetPlan() : nullptr;
		if (!Plan) return TEXT("no garage: not on Ride UK");
		const FVector Here = GetActorLocation();
		std::vector<TorqueWorld::RidePlan::GarageSpot> Spots;
		Plan->GaragesNear(Here.X / 100.0, Here.Y / 100.0, 20000.0, Spots);
		if (Spots.empty()) return TEXT("no garage within 20 km");
		const TorqueWorld::RidePlan::GarageSpot* Best = &Spots[0];
		for (const TorqueWorld::RidePlan::GarageSpot& S : Spots)
			if (FVector2D::Distance(FVector2D(S.Middle.X, S.Middle.Z), FVector2D(Here.X, Here.Y) / 100.0) <
				FVector2D::Distance(FVector2D(Best->Middle.X, Best->Middle.Z), FVector2D(Here.X, Here.Y) / 100.0)) Best = &S;
		if (bOnFoot) TestFoot(TEXT("on"));
		const double Yaw = FMath::RadiansToDegrees(std::atan2(-Best->Door.Z, -Best->Door.X));   // (facing in, away from the door)
		ATorqueForecourtGarage::Keep(const_cast<ATorqueRideWorld*>(Ride), *Plan, FVector2D(Best->Middle.X, Best->Middle.Z));
		const FString Placed = TestPlaceAt(Best->Middle.X * 100.0, Best->Middle.Z * 100.0, Yaw);
		const FString Off = TestFoot(TEXT("off"));
		return FString::Printf(TEXT("forecourt %d's garage: %s; %s"), Best->Court, *Placed, *Off);
	}
	if (Verb == TEXT("computer"))
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (bGarageComputer) CloseGarageComputer(PC);
		else if (InGarage()) OpenGarageComputer(PC);
		else return TEXT("not in a garage");
		return bGarageComputer ? TEXT("the wall computer is up") : TEXT("the wall computer is shut");
	}
	bool bAsked = true;
	if (Verb == TEXT("buy")) bAsked = GarageRequest((uint8)ETorqueShedAct::GarageBuy, NAME_None, FName(*Arg), TEXT("buying"));
	else if (Verb == TEXT("fit")) bAsked = GarageRequest((uint8)ETorqueShedAct::GarageFit, Arg.IsEmpty() ? GarageSlot : FName(*Arg), NAME_None, TEXT("fitting"));
	else if (Verb == TEXT("off")) bAsked = GarageRequest((uint8)ETorqueShedAct::GarageOff, FName(*Arg), NAME_None, TEXT("taking off"));
	else if (Verb == TEXT("stow")) bAsked = GarageRequest((uint8)ETorqueShedAct::GarageStow, NAME_None, NAME_None, TEXT("sending home"));
	else if (Verb != TEXT("state")) return FString::Printf(TEXT("garage: no such step '%s'"), *What);
	const int32 Held = B->HeldBy(B->Me());
	const ATorqueForecourtGarage* G = GarageHere.Get();
	return FString::Printf(TEXT("%s%s; %s; bike %s; looking at %s; holding %s; %d parts drawn%s"),
		bAsked ? TEXT("") : TEXT("REFUSED: "), *GarageSaid,
		G ? *FString::Printf(TEXT("on foot in forecourt %d's garage"), G->Court()) : bOnFoot ? TEXT("on foot, not in a garage") : TEXT("riding"),
		G && G->Holds(GetActorLocation()) ? TEXT("inside") : TEXT("not inside"),
		bGarageLookComputer ? TEXT("the computer") : GarageSlot.IsNone() ? TEXT("nothing") : *GarageSlot.ToString(),
		Held ? *B->ItemTitle(Held) : TEXT("nothing"), ModelParts.Num(), bModelStale ? TEXT(" (to be drawn again)") : TEXT(""));
}
