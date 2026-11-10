#include "Shed/TorqueGeometryMode.h"

#include "Fit/BikeGeometry.h"
#include "Fit/BikeLayout.h"
#include "Fit/BikeParts.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorqueWalker.h"
#include "SonyPad.h"
#include "CanvasItem.h"
#include "RenderUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include <string>
#include <vector>

namespace TorqueGeometryModeImpl
{
using TorqueFit::GeoHandle;
using TorqueFit::GeoOffset;
using TorqueFit::GeoSnap;
using GV3 = TorqueFit::V3;
namespace Geo = TorqueFit::Geo;

// Godot's GeometryMode and GeoGizmo numbers (metres there; the camera's in cm here).
constexpr double OrbitSpeed = 0.008;
constexpr double MinDist = 60.0, MaxDist = 600.0;
constexpr double Arm = 1.0, Ring = 0.8, PickPx = 12.0, ScreenSize = 0.16;
const FLinearColor AxisColours[3] = {FLinearColor(0.95f, 0.25f, 0.25f), FLinearColor(0.35f, 0.9f, 0.3f), FLinearColor(0.3f, 0.55f, 1.0f)};
const FLinearColor MiddleColour(0.92f, 0.92f, 0.95f);
const FLinearColor HoverColour(1.0f, 0.85f, 0.2f);
// UITheme: the accent, the panel, the buttons.
const FLinearColor Accent(1.0f, 0.62f, 0.2f);
const FLinearColor PanelBg(0.07f, 0.08f, 0.1f, 0.9f);
const FLinearColor ButtonBg(0.16f, 0.17f, 0.2f), ButtonHover(0.24f, 0.25f, 0.3f), ButtonOn(0.7f, 0.434f, 0.14f);
const FLinearColor TextCol(0.85f, 0.87f, 0.9f), Dim(1.0f, 1.0f, 1.0f, 0.6f), Value(0.85f, 0.95f, 1.0f), StatusCol(1.0f, 0.8f, 0.55f);

std::string TgmStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FString TgmUe(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

TorqueFit::BikeGeo TgmParse(const FString& Text)
{
	TorqueFit::BikeGeo G;
	if (!Text.IsEmpty()) Geo::FromText(TgmStd(Text), G);
	return G;
}
FString TgmText(const TorqueFit::BikeGeo& G) { return G.Empty() ? FString() : TgmUe(Geo::ToText(G)); }

// The bike's own cm (x forward, y right, z up, as TorqueShedCatalogue builds it) and the Godot
// frame geometry mode works in (metres, x right, y up, z back): ours = (-gz, gy, gx), cm = (x, z, y).
FVector TgmUeOf(const GV3& G) { return FVector(-G.z, G.x, G.y) * 100.0; }
GV3 TgmGOf(const FVector& U) { return GV3(U.Y, U.Z, -U.X) / 100.0; }
FVector TgmUeDir(const GV3& G) { return FVector(-G.z, G.x, G.y); }
GV3 TgmGDir(const FVector& U) { return GV3(U.Y, U.Z, -U.X); }

// A slot's offset about its middle as a transform in the bike's own cm.
FTransform TgmSlotXform(const GeoOffset& O, const FVector& PivotCm)
{
	const TorqueFit::GAffine T = Geo::OffsetXform(O, TgmGOf(PivotCm));
	auto Col = [&T](const FVector& E) { return TgmUeDir(T.Apply(TgmGDir(E)) - T.O); };
	const FMatrix M(Col(FVector(1, 0, 0)), Col(FVector(0, 1, 0)), Col(FVector(0, 0, 1)), TgmUeOf(T.O));
	return FTransform(M);
}

int32 TgmAxisOfKey(int32 I) { return I < 0 ? 0 : (I > 3 ? 3 : I); }
} // namespace TorqueGeometryModeImpl

namespace TGMI = TorqueGeometryModeImpl;

// --- The bike's parts at rest, and a fit shown on them ----------------------------------------------

void FTorqueBikeFitView::Capture(const TArray<UStaticMeshComponent*>& Parts)
{
	Rest.Reset();
	Pivots.Reset();
	TMap<FName, FBox> Boxes;
	for (const UStaticMeshComponent* C : Parts)
	{
		Rest.Add(C ? C->GetRelativeTransform() : FTransform::Identity);
		if (!C || !C->GetStaticMesh()) continue;
		// (BikeModel.slot_bounds: each mesh's box as placed, the slot's the box round them.)
		const FBox Box = C->GetStaticMesh()->GetBoundingBox().TransformBy(C->GetRelativeTransform());
		const FName Slot = TorqueShedModel::SlotTag(C);
		if (Slot.IsNone()) continue;
		FBox& Into = Boxes.FindOrAdd(Slot, FBox(ForceInit));
		Into += Box;
	}
	for (const TPair<FName, FBox>& KV : Boxes)
		if (KV.Value.IsValid) Pivots.Add(KV.Key, KV.Value.GetCenter());
}

void FTorqueBikeFitView::Show(const TArray<UStaticMeshComponent*>& Parts, const FString& GeoText) const
{
	TMap<FName, FTransform> Moves;
	TorqueGeometryMode::SlotMoves(GeoText, Pivots, Moves);
	for (int32 I = 0; I < Parts.Num() && I < Rest.Num(); ++I)
	{
		UStaticMeshComponent* C = Parts[I];
		if (!C) continue;
		const FTransform* M = Moves.Find(TorqueShedModel::SlotTag(C));
		// (The part as drawn, then its slot's offset in the bike's frame.)
		C->SetRelativeTransform(M ? Rest[I] * *M : Rest[I]);
	}
}

void TorqueGeometryMode::SlotMoves(const FString& Geo, const TMap<FName, FVector>& Pivots, TMap<FName, FTransform>& Out)
{
	Out.Reset();
	const TorqueFit::BikeGeo G = TGMI::TgmParse(Geo);
	for (const auto& [Slot, Off] : G.Slots)
	{
		const FName Name(*TGMI::TgmUe(Slot));
		if (const FVector* P = Pivots.Find(Name)) Out.Add(Name, TGMI::TgmSlotXform(Off, *P));
	}
}

// --- The host's side --------------------------------------------------------------------------------

FString TorqueGeometryMode::CheckSave(const UTorqueBuildSubsystem& Shed, const FTorqueShedRequest& R)
{
	if (!Shed.Exists(R.Bay)) return TEXT("That bike has gone.");
	if (R.Text.Len() > MaxText) return TEXT("That fit is too big to save.");
	TorqueFit::BikeGeo G;
	if (!R.Text.IsEmpty() && !TorqueFit::Geo::FromText(TGMI::TgmStd(R.Text), G)) return TEXT("There's nothing to save.");
	return FString();
}

FString TorqueGeometryMode::ApplySave(FTorqueShedBike& Bike, const FString& Text)
{
	Bike.Geo = Clean(Text);
	return FString::Printf(TEXT("Saved the fit of %s's parts."), *Bike.Name);
}

FString TorqueGeometryMode::Clean(const FString& Text)
{
	if (Text.IsEmpty() || Text.Len() > MaxText) return FString();
	TorqueFit::BikeGeo G;
	if (!TorqueFit::Geo::FromText(TGMI::TgmStd(Text), G)) return FString();
	return TGMI::TgmText(G);
}

// --- Geometry mode ----------------------------------------------------------------------------------

struct ATorqueGeometryMode::FState
{
	TWeakObjectPtr<ATorqueWalker> Walker;
	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<ATorqueShedBike> Bike;
	int32 Bay = INDEX_NONE;
	uint64 OpenFrame = 0;   // the frame it opened in: the F2 that opened it is still "just pressed" then
	TorqueFit::BikeGeo Draft, Saved;
	TArray<TorqueFit::BikeGeo> Undo;
	FString DrawnRig;                 // the rig handles the bike's drawn with (the draft's, once refit)
	FName Selected;
	TorqueFit::GeoHandle Mode = TorqueFit::GeoHandle::Move;
	int32 Hover = -1;
	bool bSnapAlways = false;
	bool bPartAxes = false;           // (finer) move along the part's own axes
	int32 Axis = 0;                   // (finer) the axis the nudges go along: 0..2, 3 all (scale)
	// The handle being dragged and where it started (Godot frame, the bike's metres).
	bool bDrag = false;
	int32 DragH = -1;
	TorqueFit::GeoHandle DragMode = TorqueFit::GeoHandle::Move;
	FVector2D DragMouse = FVector2D::ZeroVector;
	TGMI::GV3 Center, Dir, V0;
	double T0 = 0.0;
	bool bV0 = false;
	TorqueFit::GeoOffset Start;
	TorqueFit::BikeGeo Before;
	bool bOrbit = false, bPan = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	double Yaw = 0.0, Pitch = -0.35, Dist = 260.0;
	FVector Focus = FVector::ZeroVector;
	// The panel's buttons, rows and sliders as last drawn, for the mouse.
	struct FHit
	{
		FBox2D R;
		TFunction<void()> Click;
		int32 Slider = -1;
	};
	TArray<FHit> Hits;
	FBox2D PanelBox, ListBox;
	int32 Slider = -1;                // the rig slider being dragged
	FBox2D SliderTrack[4];
	bool bConfirm = false;
	FString Status;
	int32 TypeField = -1;             // (finer) the readout number being typed: handle * 3 + axis
	FString Typed;
	TArray<TorqueFit::GeoGap> Gaps;
	std::vector<TorqueFit::FittedPart> FitParts;
	std::vector<TorqueFit::PartMesh> FitMeshes;
	bool bFitMeshes = false;
	FString FitFor;
	int32 ListScroll = 0;
	TArray<FName> List;
	UStaticMeshComponent* SeenFirst = nullptr;
	int32 SeenCount = -1;
	uint32 PrevPad = 0;
	double PadRepeat = 0.0;
	// Your own walker (and what it carries) hidden from your view while it's open, so zooming out
	// past it shows the bike, not your back. Yours only: the others still see you.
	TArray<TWeakObjectPtr<AActor>> Hidden;
};

static bool GExpert = false;   // GeometryMode.expert: free move, for this session

ATorqueGeometryMode::ATorqueGeometryMode()
{
	PrimaryActorTick.bCanEverTick = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("GeometryCamera"));
	Camera->SetFieldOfView(60.0f);
	RootComponent = Camera;
	St = MakeShared<FState>();
}

ATorqueGeometryMode::~ATorqueGeometryMode() = default;

ATorqueGeometryMode* ATorqueGeometryMode::For(const AController* Controller)
{
	if (!Controller || !Controller->GetWorld()) return nullptr;
	for (TActorIterator<ATorqueGeometryMode> It(Controller->GetWorld()); It; ++It)
		if (It->St && It->St->PC.Get() == Controller && !It->IsActorBeingDestroyed()) return *It;
	return nullptr;
}

namespace { uint64 GGeoClosedFrame = 0; }   // the frame geometry mode last closed in

bool ATorqueGeometryMode::Open(ATorqueWalker* Walker, int32 LookedAtBay, FString& WhyNot)
{
	// The F2 that just closed it (this tick, before the walker's) mustn't open it again at once.
	if (GFrameCounter <= GGeoClosedFrame + 1) return true;
	UWorld* World = Walker ? Walker->GetWorld() : nullptr;
	APlayerController* PC = Walker ? Cast<APlayerController>(Walker->GetController()) : nullptr;
	if (!World || !PC) return false;
	if (For(PC)) return true;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(Walker);
	// (main.gd geometry_bike: the bike looked at, else the nearest within 4 m.)
	ATorqueShedBike* Best = nullptr;
	double BestD = 400.0;
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
	{
		if (!B || !B->Exists(It->Bay)) continue;
		if (It->Bay == LookedAtBay)
		{
			Best = *It;
			break;
		}
		const double D = FVector::Dist(It->GetActorLocation(), Walker->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	if (!Best)
	{
		WhyNot = TEXT("Walk up to a bike and look at it to fit its parts in geometry mode.");
		return false;
	}
	if (Best->Parts.Num() == 0)
	{
		WhyNot = TEXT("That bike can't be edited right now.");
		return false;
	}
	ATorqueGeometryMode* G = World->SpawnActor<ATorqueGeometryMode>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!G) return false;
	G->Begin(Walker, Best, Best->Bay);
	return true;
}

void ATorqueGeometryMode::Begin(ATorqueWalker* Walker, ATorqueShedBike* Bike, int32 Bay)
{
	FState& S = *St;
	S.Walker = Walker;
	S.PC = Cast<APlayerController>(Walker->GetController());
	S.Bike = Bike;
	S.Bay = Bay;
	S.OpenFrame = GFrameCounter;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	S.Saved = TGMI::TgmParse(B ? B->Bike(Bay).Geo : FString());
	S.Draft = S.Saved;
	S.DrawnRig = TGMI::TgmText(S.Saved);
	S.Undo.Reset();
	S.Selected = NAME_None;
	// (GeometryMode.open: the camera orbits the bike from where you stood.)
	S.Focus = Bike->GetActorLocation() + FVector(0.0, 0.0, 55.0);
	FVector From = S.Focus + FVector(150.0, 150.0, 60.0);
	if (Walker->Camera) From = Walker->Camera->GetComponentLocation();
	const FVector V = From - S.Focus;
	S.Dist = FMath::Clamp(V.Size(), 140.0, 320.0);
	S.Yaw = FMath::Atan2(V.Y, V.X);
	S.Pitch = FMath::Clamp(-FMath::Asin(FMath::Clamp(V.Z / FMath::Max(V.Size(), 1.0), -1.0, 1.0)), -1.3, 0.4);
	UpdateCamera();
	if (APlayerController* PC = S.PC.Get())
	{
		PC->SetViewTargetWithBlend(this, 0.25f);
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	HideWalker(true);
	{
		const FSonyPadState Now = Walker->PadState();   // (buttons held as it opens don't count as pressed)
		S.PrevPad = Now.bConnected ? Now.Buttons : 0;
	}
	// A bike already fitted by hand opens with free move on, so its moves show.
	if (!S.Saved.Slots.empty()) GExpert = true;
	if (B)
	{
		B->ReportActivity(FString::Printf(TEXT("fitting parts in geometry mode on bay %d"), Bay + 1));
		TellHandle = B->OnTell.AddLambda([this](int32 Peer, const FString& Text, bool) {
			const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
			if (Sub && Peer == Sub->Me()) Say(Text);
		});
	}
	Refit(true);
}

void ATorqueGeometryMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnTell.Remove(TellHandle);
	HideWalker(false);
	Super::EndPlay(Reason);
}

bool ATorqueGeometryMode::HasRig() const
{
	// (A catalogue bike is laid out by fitting's rig; the Speed Twin model isn't.)
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	TMap<FString, FString> Parts;
	return B && B->LayoutParts(St->Bay, Parts);
}

bool ATorqueGeometryMode::FreeMove() const { return GExpert || !HasRig(); }

void ATorqueGeometryMode::SetExpert(bool bOn)
{
	GExpert = bOn;
	if (!FreeMove()) Select(NAME_None);
}

bool ATorqueGeometryMode::IsDirty() const { return TorqueFit::Geo::Sanitize(St->Draft) != TorqueFit::Geo::Sanitize(St->Saved); }

void ATorqueGeometryMode::Say(const FString& Text) { St->Status = Text; }

void ATorqueGeometryMode::Select(FName Slot)
{
	FState& S = *St;
	if (!FreeMove()) Slot = NAME_None;
	S.Selected = Slot;
	S.bDrag = false;
	S.TypeField = -1;
	UpdateHighlight();
	UpdateGaps();
}

void ATorqueGeometryMode::PushUndo() { St->Undo.Add(St->Draft); }

void ATorqueGeometryMode::UndoOnce()
{
	FState& S = *St;
	if (S.Undo.Num() == 0) return;
	S.Draft = S.Undo.Pop();
	S.bDrag = false;
	Refit(true);
}

void ATorqueGeometryMode::ResetPart()
{
	FState& S = *St;
	if (S.Selected.IsNone() || !S.Draft.Slots.count(TGMI::TgmStd(S.Selected.ToString()))) return;
	PushUndo();
	S.Draft.Set(TGMI::TgmStd(S.Selected.ToString()), TorqueFit::GeoOffset());
	Refit(false);
}

void ATorqueGeometryMode::ResetAll()
{
	FState& S = *St;
	if (S.Draft.Empty()) return;
	PushUndo();
	S.Draft = TorqueFit::BikeGeo();
	Refit(true);
}

void ATorqueGeometryMode::Refit(bool bRig)
{
	// (GeometryMode._refit: the draft on the bike. The rig's handles re-solve it: drawn again.)
	FState& S = *St;
	ATorqueShedBike* Bike = S.Bike.Get();
	if (!Bike) return;
	const FString Text = TGMI::TgmText(S.Draft);
	TorqueFit::BikeGeo RigOnly;
	RigOnly.Rig = S.Draft.Rig;
	const FString Rig = TGMI::TgmText(RigOnly);
	if (bRig && Rig != S.DrawnRig)
	{
		S.DrawnRig = Rig;
		Bike->Preview(&Text);   // drawn again with the draft (and its fit shown)
		S.bFitMeshes = false;
	}
	else
		Bike->FitView.Show(Bike->Parts, Text);
	UpdateHighlight();
	UpdateGaps();
}

void ATorqueGeometryMode::Save()
{
	// (GeometryMode.save: the draft to the host for this bike.)
	FState& S = *St;
	S.Draft = TorqueFit::Geo::Sanitize(S.Draft);
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::GeoSave;
	R.Bay = S.Bay;
	R.Text = TGMI::TgmText(S.Draft);
	FString WhyNot;
	if (B->Request(R, WhyNot)) S.Saved = S.Draft;
	else Say(WhyNot);
}

void ATorqueGeometryMode::RequestExit()
{
	if (IsDirty())
	{
		St->bConfirm = true;
		return;
	}
	Close();
}

void ATorqueGeometryMode::DiscardAndClose()
{
	St->Draft = St->Saved;
	Close();
}

void ATorqueGeometryMode::Close()
{
	FState& S = *St;
	for (const TWeakObjectPtr<UStaticMeshComponent>& C : Lit)
		if (C.IsValid() && C->GetOverlayMaterial() == LitMaterial) C->SetOverlayMaterial(nullptr);
	Lit.Reset();
	// The bike as its saved fit has it (drawn again if the draft's rig handles had it re-formed).
	if (ATorqueShedBike* Bike = S.Bike.Get()) Bike->Preview(nullptr);
	HideWalker(false);
	if (APlayerController* PC = S.PC.Get())
	{
		if (ATorqueWalker* W = S.Walker.Get()) PC->SetViewTargetWithBlend(W, 0.25f);
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->ReportActivity(FString());
	S.PC = nullptr;   // (no longer this player's: For() won't find it)
	GGeoClosedFrame = GFrameCounter;
	Destroy();
}

void ATorqueGeometryMode::HideWalker(bool bHide)
{
	// (APlayerController::HiddenActors: drawn for everyone but this player. The walker's body bits
	// are owner-no-see, which only holds while the walker is the view target; here the camera is
	// this actor, so they'd show, as would the avatar and the tool in its hand.)
	FState& S = *St;
	APlayerController* PC = S.PC.Get();
	if (!bHide)
	{
		if (PC)
			for (const TWeakObjectPtr<AActor>& A : S.Hidden)
				if (AActor* Was = A.Get()) PC->HiddenActors.Remove(Was);
		S.Hidden.Reset();
		return;
	}
	ATorqueWalker* Walker = S.Walker.Get();
	if (!PC || !Walker) return;
	TArray<AActor*> Mine;
	Walker->GetAttachedActors(Mine, true, true);   // the MetaHuman Blueprint, held tools
	Mine.Add(Walker);
	for (AActor* A : Mine)
	{
		if (!A || A == this || S.Hidden.Contains(A)) continue;
		S.Hidden.Add(A);
		PC->HiddenActors.AddUnique(A);
	}
}

void ATorqueGeometryMode::UpdateCamera()
{
	FState& S = *St;
	const FVector D(FMath::Cos(S.Pitch) * FMath::Cos(S.Yaw), FMath::Cos(S.Pitch) * FMath::Sin(S.Yaw), -FMath::Sin(S.Pitch));
	const FVector At = S.Focus + D * S.Dist;
	SetActorLocationAndRotation(At, (S.Focus - At).Rotation());
}

void ATorqueGeometryMode::UpdateHighlight()
{
	// (BikeModel.highlight: the selected part glows, as the walker's look does.)
	FState& S = *St;
	if (!LitMaterial)
	{
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedHighlight.M_ShedHighlight")))
		{
			LitMaterial = UMaterialInstanceDynamic::Create(Glow, this);
			LitMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.6f, 0.15f));
			LitMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
		}
	}
	for (const TWeakObjectPtr<UStaticMeshComponent>& C : Lit)
		if (C.IsValid() && C->GetOverlayMaterial() == LitMaterial) C->SetOverlayMaterial(nullptr);
	Lit.Reset();
	ATorqueShedBike* Bike = S.Bike.Get();
	if (!Bike || S.Selected.IsNone() || !LitMaterial) return;
	for (UStaticMeshComponent* C : Bike->Parts)
		if (C && TorqueShedModel::SlotTag(C) == S.Selected)
		{
			C->SetOverlayMaterial(LitMaterial);
			Lit.Add(C);
		}
}

void ATorqueGeometryMode::UpdateGaps()
{
	// (Finer: how near the selected part comes to each part round it, with the draft on.)
	FState& S = *St;
	S.Gaps.Reset();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (S.Selected.IsNone() || !B) return;
	TMap<FString, FString> Parts;
	if (!B->LayoutParts(S.Bay, Parts)) return;   // (the catalogue's bikes: the Speed Twin model has no Blender meshes here)
	TorqueFit::BikeGeo RigOnly;
	RigOnly.Rig = S.Draft.Rig;
	const FString Key = TGMI::TgmText(RigOnly) + TEXT("|") + FString::FromInt(Parts.Num());
	if (!S.bFitMeshes || S.FitFor != Key)
	{
		S.FitParts.clear();
		S.FitMeshes.clear();
		S.bFitMeshes = TorqueShedCatalogue::DrawnParts(Parts, TGMI::TgmText(RigOnly), S.FitParts, S.FitMeshes);
		S.FitFor = Key;
	}
	if (!S.bFitMeshes) return;
	TorqueFit::GeoPivots Pv;
	for (size_t I = 0; I < S.FitParts.size(); ++I) Pv.Add(S.FitParts[I], S.FitMeshes[I]);
	std::vector<TorqueFit::PartMesh> Moved = S.FitMeshes;
	std::vector<std::pair<const TorqueFit::FittedPart*, const TorqueFit::PartMesh*>> Meshes;
	for (size_t I = 0; I < S.FitParts.size(); ++I)
	{
		Pv.Bake(S.Draft, S.FitParts[I], Moved[I]);
		Meshes.push_back({&S.FitParts[I], &Moved[I]});
	}
	for (const TorqueFit::GeoGap& G : TorqueFit::Gaps(Meshes, TGMI::TgmStd(S.Selected.ToString()), 0.03)) S.Gaps.Add(G);
}

TArray<FName> ATorqueGeometryMode::PickableSlots() const
{
	// (GeometryMode.pickable_slots: every part fitted and drawn, in the catalogue's slot order.)
	TArray<FName> Out;
	const FState& S = *St;
	const ATorqueShedBike* Bike = S.Bike.Get();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!Bike || !B) return Out;
	TSet<FName> Drawn;
	for (const UStaticMeshComponent* C : Bike->Parts)
		if (C) Drawn.Add(TorqueShedModel::SlotTag(C));
	for (const std::string& Slot : TorqueFit::Geo::Slots())
	{
		const FName Name(*TGMI::TgmUe(Slot));
		if (Drawn.Contains(Name) && B->ItemOn(S.Bay, Name) != 0) Out.Add(Name);
	}
	return Out;
}

// --- For hands-off tests ----------------------------------------------------------------------------

FString ATorqueGeometryMode::TestRig(const FString& Key, double Value)
{
	FState& S = *St;
	if (!HasRig()) return TEXT("this bike has no rig (the Speed Twin model)");
	TorqueFit::LayoutAdjust& A = S.Draft.Rig;
	double* Field = Key == TEXT("tank_up") ? &A.TankUpMm : Key == TEXT("pegs") ? &A.PegsMm : Key == TEXT("hoop_up") ? &A.HoopUpMm :
		Key == TEXT("hoop_deg") ? &A.HoopDeg : nullptr;
	if (!Field) return TEXT("no such rig handle (tank_up, pegs, hoop_up, hoop_deg)");
	PushUndo();
	*Field = Value;
	A = A.Clean();
	Refit(true);
	return FString::Printf(TEXT("%s = %g, the bike re-formed (%d parts drawn)"), *Key, *Field, S.Bike.IsValid() ? S.Bike->Parts.Num() : 0);
}

FString ATorqueGeometryMode::TestMove(FName Slot, const FVector& Cm)
{
	FState& S = *St;
	if (!GExpert && HasRig()) SetExpert(true);
	S.List = PickableSlots();
	if (!S.List.Contains(Slot)) return FString::Printf(TEXT("%s isn't on this bike"), *Slot.ToString());
	Select(Slot);
	const std::string Name = TGMI::TgmStd(Slot.ToString());
	PushUndo();
	TorqueFit::GeoOffset Off = S.Draft.Of(Name);
	Off.P += TGMI::GV3(Cm.X, Cm.Y, Cm.Z) / 100.0;
	S.Draft.Set(Name, Off);
	Refit(false);
	const TorqueFit::GeoOffset Now = S.Draft.Of(Name);
	return FString::Printf(TEXT("%s moved to (%.1f, %.1f, %.1f) cm, %d gaps listed"), *Slot.ToString(), Now.P.x * 100.0, Now.P.y * 100.0, Now.P.z * 100.0,
		S.Gaps.Num());
}

FString ATorqueGeometryMode::TestSelect(FName Slot)
{
	FState& S = *St;
	S.List = PickableSlots();
	if (!S.List.Contains(Slot)) return FString::Printf(TEXT("%s isn't on this bike"), *Slot.ToString());
	Select(Slot);
	return TEXT("selected ") + Slot.ToString();
}

FString ATorqueGeometryMode::TestSave()
{
	Save();
	return TGMI::TgmText(St->Draft);
}

void ATorqueGeometryMode::TestExit()
{
	RequestExit();
	if (St->bConfirm) DiscardAndClose();
}

int32 ATorqueGeometryMode::TestBay() const { return St->Bay; }

// --- Frame by frame ---------------------------------------------------------------------------------

void ATorqueGeometryMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FState& S = *St;
	if (!S.PC.IsValid()) return;
	ATorqueShedBike* Bike = S.Bike.Get();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!Bike || !B || !B->Exists(S.Bay))
	{
		Close();
		return;
	}
	HideWalker(true);   // (the avatar can be made again while it's open: hide the new one too)
	// The shed draws a bike again when anything about it changes: keep the draft on the new one.
	UStaticMeshComponent* First = Bike->Parts.Num() > 0 ? Bike->Parts[0] : nullptr;
	if (First != S.SeenFirst || Bike->Parts.Num() != S.SeenCount)
	{
		S.SeenFirst = First;
		S.SeenCount = Bike->Parts.Num();
		Bike->FitView.Show(Bike->Parts, TGMI::TgmText(S.Draft));
		S.bFitMeshes = false;
		UpdateHighlight();
	}
	S.List = PickableSlots();
	if (!S.Selected.IsNone() && !S.List.Contains(S.Selected)) Select(NAME_None);
	ReadInput(DeltaSeconds);
	if (!S.PC.IsValid()) return;   // (closed)
	UpdateCamera();
}

int32 ATorqueGeometryMode::PickHandle(const FVector2D& Mouse) const
{
	// (GeoGizmo.pick: the handle nearest the mouse on the screen, within 12 px.)
	const FState& S = *St;
	const APlayerController* PC = S.PC.Get();
	const ATorqueShedBike* Bike = S.Bike.Get();
	if (!PC || !Bike || S.Selected.IsNone() || !FreeMove()) return -1;
	const FVector* PivotCm = Bike->FitView.Pivots.Find(S.Selected);
	if (!PivotCm) return -1;
	const FTransform BikeXf = Bike->BikeRoot->GetComponentTransform();
	const TorqueFit::GeoOffset O = S.Draft.Of(TGMI::TgmStd(S.Selected.ToString()));
	const TGMI::GV3 C = TGMI::TgmGOf(*PivotCm) + O.P;
	auto World = [&](const TGMI::GV3& G) { return BikeXf.TransformPosition(TGMI::TgmUeOf(G)); };
	FVector Eye;
	FRotator EyeRot;
	PC->GetPlayerViewPoint(Eye, EyeRot);
	const double Size = FMath::Max(30.0, FVector::Dist(Eye, World(C))) * TGMI::ScreenSize / 100.0;   // metres
	auto Screen = [&](const TGMI::GV3& G, FVector2D& Out) {
		const FVector W = World(G);
		if (FVector::DotProduct(W - Eye, EyeRot.Vector()) <= 1.0) return false;
		return PC->ProjectWorldLocationToScreen(W, Out, false);
	};
	auto SegPx = [&](const TGMI::GV3& A, const TGMI::GV3& Bp) {
		FVector2D Pa, Pb;
		if (!Screen(A, Pa) || !Screen(Bp, Pb)) return 1e9;
		const FVector2D Ab = Pb - Pa;
		const double T = FMath::Clamp(FVector2D::DotProduct(Mouse - Pa, Ab) / FMath::Max(Ab.SizeSquared(), 1e-6), 0.0, 1.0);
		return FVector2D::Distance(Mouse, Pa + Ab * T);
	};
	int32 Best = -1;
	double BestD = TGMI::PickPx;
	for (int32 A = 0; A < 3; ++A)
	{
		TGMI::GV3 Dir;
		(A == 0 ? Dir.x : (A == 1 ? Dir.y : Dir.z)) = 1.0;
		double D = 1e9;
		switch (S.Mode)
		{
		case TorqueFit::GeoHandle::Move:
			D = SegPx(C, C + Dir * (TGMI::Arm * Size));
			break;
		case TorqueFit::GeoHandle::Rotate:
		{
			const TGMI::GV3 Ra = Dir.Cross(std::fabs(Dir.y) < 0.9 ? TGMI::GV3(0, 1, 0) : TGMI::GV3(1, 0, 0)).Normalized();
			const TGMI::GV3 Rb = Dir.Cross(Ra);
			TGMI::GV3 Prev = C + Ra * (TGMI::Ring * Size);
			for (int32 I = 1; I <= 48; ++I)
			{
				const double T = 2.0 * PI * I / 48.0;
				const TGMI::GV3 Pt = C + (Ra * std::cos(T) + Rb * std::sin(T)) * (TGMI::Ring * Size);
				D = FMath::Min(D, SegPx(Prev, Pt));
				Prev = Pt;
			}
			break;
		}
		default:
		{
			const TGMI::GV3 Sd = TorqueFit::Geo::PartAxis(O, A);
			D = SegPx(C + Sd * (TGMI::Arm * Size * 0.2), C + Sd * (TGMI::Arm * Size * 0.95));
			break;
		}
		}
		if (D < BestD)
		{
			BestD = D;
			Best = A;
		}
	}
	FVector2D Mid;
	if (S.Mode == TorqueFit::GeoHandle::Scale && Screen(C, Mid) && FVector2D::Distance(Mid, Mouse) < TGMI::PickPx * 1.4) Best = TorqueFit::Geo::Uniform;
	return Best;
}

FName ATorqueGeometryMode::PickPart(const FVector2D& Mouse) const
{
	// (GeometryMode._pick_part: the part of this bike under the mouse, by its own shape.)
	const FState& S = *St;
	const APlayerController* PC = S.PC.Get();
	const ATorqueShedBike* Bike = S.Bike.Get();
	if (!PC || !Bike) return NAME_None;
	FVector From, Dir;
	if (!PC->DeprojectScreenPositionToWorld(Mouse.X, Mouse.Y, From, Dir)) return NAME_None;
	const FVector To = From + Dir * 3000.0;
	double Best = 2.0;
	FName Slot;
	for (const UStaticMeshComponent* C : Bike->Parts)
	{
		if (!C) continue;
		double T = 0.0;
		const TorqueShedPick::EHit H = TorqueShedPick::Trace(C, From, To, T);
		if (H == TorqueShedPick::EHit::NoShape)
		{
			// (No shape to trace: its box, the hit taken at the box's middle.)
			const FBox Box = C->Bounds.GetBox();
			if (!FMath::LineBoxIntersection(Box, From, To, To - From)) continue;
			T = FVector::DotProduct(Box.GetCenter() - From, Dir) / 3000.0;
		}
		else if (H != TorqueShedPick::EHit::Hit)
			continue;
		if (T < Best)
		{
			Best = T;
			Slot = TorqueShedModel::SlotTag(C);
		}
	}
	return S.List.Contains(Slot) ? Slot : NAME_None;
}

void ATorqueGeometryMode::Press(const FVector2D& Mouse)
{
	FState& S = *St;
	// The panel's controls first.
	for (const FState::FHit& H : S.Hits)
		if (H.R.IsInside(Mouse))
		{
			if (H.Slider >= 0)
			{
				PushUndo();
				S.Slider = H.Slider;
				DragTo(Mouse);
			}
			else if (H.Click)
				H.Click();
			return;
		}
	if (S.bConfirm || S.PanelBox.IsInside(Mouse) || S.ListBox.IsInside(Mouse)) return;
	if (!FreeMove()) return;
	const int32 H = PickHandle(Mouse);
	if (H >= 0)
	{
		// (GeometryMode._start_drag.)
		const ATorqueShedBike* Bike = S.Bike.Get();
		const APlayerController* PC = S.PC.Get();
		const FVector* PivotCm = Bike ? Bike->FitView.Pivots.Find(S.Selected) : nullptr;
		FVector From, Dir;
		if (!PivotCm || !PC || !PC->DeprojectScreenPositionToWorld(Mouse.X, Mouse.Y, From, Dir)) return;
		const FTransform BikeXf = Bike->BikeRoot->GetComponentTransform();
		const TGMI::GV3 Ro = TGMI::TgmGOf(BikeXf.InverseTransformPosition(From));
		const TGMI::GV3 Rd = TGMI::TgmGDir(BikeXf.InverseTransformVector(Dir));
		S.bDrag = true;
		S.DragH = H;
		S.DragMode = S.Mode;
		S.DragMouse = Mouse;
		S.Before = S.Draft;
		S.Start = S.Draft.Of(TGMI::TgmStd(S.Selected.ToString()));
		S.Center = TGMI::TgmGOf(*PivotCm) + S.Start.P;
		S.bV0 = false;
		TGMI::GV3 Unit;
		if (H < 3) (H == 0 ? Unit.x : (H == 1 ? Unit.y : Unit.z)) = 1.0;
		if (H < 3) S.Axis = H;
		switch (S.Mode)
		{
		case TorqueFit::GeoHandle::Move:
			S.Dir = S.bPartAxes ? TorqueFit::Geo::PartAxis(S.Start, H) : Unit;
			S.T0 = TorqueFit::Geo::LineParam(S.Center, S.Dir, Ro, Rd);
			break;
		case TorqueFit::GeoHandle::Rotate:
		{
			S.Dir = Unit;
			TGMI::GV3 Hit;
			// Seen edge on, the ring turns with the mouse going sideways instead.
			if (TorqueFit::Geo::PlanePoint(S.Center, S.Dir, Ro, Rd, Hit) && std::fabs(Rd.Normalized().Dot(S.Dir)) > 0.15)
			{
				S.V0 = Hit - S.Center;
				S.bV0 = true;
			}
			break;
		}
		default:
			if (H != TorqueFit::Geo::Uniform)
			{
				S.Dir = TorqueFit::Geo::PartAxis(S.Start, H);
				S.T0 = TorqueFit::Geo::LineParam(S.Center, S.Dir, Ro, Rd);
			}
			else
				S.Axis = TorqueFit::Geo::Uniform;
			break;
		}
		return;
	}
	Select(PickPart(Mouse));
}

void ATorqueGeometryMode::DragTo(const FVector2D& Mouse)
{
	FState& S = *St;
	APlayerController* PC = S.PC.Get();
	if (!PC) return;
	if (S.Slider >= 0)
	{
		// A rig handle's slider: the value where the mouse is along its track.
		const std::vector<TorqueFit::AdjustRange> Ranges = TorqueFit::BikeLayout::AdjustRanges();
		if (S.Slider >= (int32)Ranges.size()) return;
		const TorqueFit::AdjustRange& R = Ranges[S.Slider];
		const FBox2D& Tr = S.SliderTrack[S.Slider];
		const double F = FMath::Clamp((Mouse.X - Tr.Min.X) / FMath::Max(Tr.Max.X - Tr.Min.X, 1.0), 0.0, 1.0);
		double V = FMath::Lerp(R.Min, R.Max, F);
		V = FMath::RoundToDouble(V / R.Step) * R.Step;
		TorqueFit::LayoutAdjust& A = S.Draft.Rig;
		(R.Key == "tank_up" ? A.TankUpMm : R.Key == "pegs" ? A.PegsMm : R.Key == "hoop_up" ? A.HoopUpMm : A.HoopDeg) = V;
		A = A.Clean();
		return;   // (drawn again when it's let go: re-forming the bike takes a moment)
	}
	if (!S.bDrag) return;
	const ATorqueShedBike* Bike = S.Bike.Get();
	FVector From, Dir;
	if (!Bike || !PC->DeprojectScreenPositionToWorld(Mouse.X, Mouse.Y, From, Dir)) return;
	const FTransform BikeXf = Bike->BikeRoot->GetComponentTransform();
	const TGMI::GV3 Ro = TGMI::TgmGOf(BikeXf.InverseTransformPosition(From));
	const TGMI::GV3 Rd = TGMI::TgmGDir(BikeXf.InverseTransformVector(Dir));
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	// Godot's snap (Ctrl, or Always snap); finer: Ctrl+Shift snaps fine, Shift drags at a tenth.
	const TorqueFit::GeoSnap Snap = bCtrl ? (bShift ? TorqueFit::GeoSnap::Fine : TorqueFit::GeoSnap::Coarse) : (S.bSnapAlways ? TorqueFit::GeoSnap::Coarse : TorqueFit::GeoSnap::Off);
	const double Slow = bShift ? TorqueFit::Geo::SlowDrag : 1.0;
	TorqueFit::GeoOffset Off = S.Start;
	switch (S.DragMode)
	{
	case TorqueFit::GeoHandle::Move:
	{
		const double T = TorqueFit::Geo::LineParam(S.Center, S.Dir, Ro, Rd);
		Off = S.bPartAxes ? TorqueFit::Geo::MovedAlong(S.Start, S.Dir, (T - S.T0) * Slow, Snap) : TorqueFit::Geo::Moved(S.Start, S.DragH, (T - S.T0) * Slow, Snap);
		break;
	}
	case TorqueFit::GeoHandle::Rotate:
	{
		double Deg = (Mouse.X - S.DragMouse.X) * 0.5;
		TGMI::GV3 Hit;
		if (S.bV0 && TorqueFit::Geo::PlanePoint(S.Center, S.Dir, Ro, Rd, Hit)) Deg = TorqueFit::Geo::AngleAbout(S.V0, Hit - S.Center, S.Dir);
		Off = TorqueFit::Geo::Rotated(S.Start, S.DragH, Deg * Slow, Snap);
		break;
	}
	default:
	{
		double Factor = 1.0;
		if (S.DragH == TorqueFit::Geo::Uniform)
		{
			const FVector2D D = Mouse - S.DragMouse;
			Factor = FMath::Max(0.05, 1.0 + (D.X - D.Y) / 200.0);
		}
		else
		{
			const double T = TorqueFit::Geo::LineParam(S.Center, S.Dir, Ro, Rd);
			Factor = std::fabs(S.T0) > 0.001 ? FMath::Max(0.05, T / S.T0) : 1.0;
		}
		Off = TorqueFit::Geo::Scaled(S.Start, S.DragH, 1.0 + (Factor - 1.0) * Slow, Snap);
		break;
	}
	}
	S.Draft.Set(TGMI::TgmStd(S.Selected.ToString()), Off);
	if (ATorqueShedBike* B = S.Bike.Get()) B->FitView.Show(B->Parts, TGMI::TgmText(S.Draft));
}

void ATorqueGeometryMode::Release()
{
	FState& S = *St;
	if (S.Slider >= 0)
	{
		S.Slider = -1;
		if (S.Undo.Num() > 0 && TorqueFit::Geo::Sanitize(S.Undo.Last()) == TorqueFit::Geo::Sanitize(S.Draft)) S.Undo.Pop();
		Refit(true);
		return;
	}
	if (!S.bDrag) return;
	S.bDrag = false;
	if (TorqueFit::Geo::Sanitize(S.Before) != TorqueFit::Geo::Sanitize(S.Draft)) S.Undo.Add(S.Before);
	Refit(false);
}

void ATorqueGeometryMode::ReadInput(float Dt)
{
	FState& S = *St;
	APlayerController* PC = S.PC.Get();
	if (!PC) return;
	// Spawned from the walker's tick, this ticks in the same frame: the F2 that opened it would read as
	// pressed again here and close it at once (beenster: "F2 does nothing"). Keys count from the next frame.
	if (GFrameCounter <= S.OpenFrame) return;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	auto Up = [PC](const FKey& K) { return PC->WasInputKeyJustReleased(K); };
	const ATorqueWalker* Walker = S.Walker.Get();
	const FSonyPadState Sony = Walker ? Walker->PadState() : FSonyPadState();
	const uint32 PadDown = Sony.bConnected ? Sony.Buttons : 0;
	const uint32 PadHit = PadDown & ~S.PrevPad;
	S.PrevPad = PadDown;
	FVector2D Mouse = S.LastMouse;
	float Mx = 0.0f, My = 0.0f;
	if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
	const FVector2D Moved = Mouse - S.LastMouse;
	S.LastMouse = Mouse;
	const bool bCtrl = Down(EKeys::LeftControl) || Down(EKeys::RightControl);

	// (Finer) typing a number into the readout: digits, a point and a minus; Enter, Esc.
	if (S.TypeField >= 0)
	{
		static const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
		static const FKey NumPad[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
			EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
		for (int32 I = 0; I < 10; ++I)
			if (Just(Digits[I]) || Just(NumPad[I])) S.Typed += FString::FromInt(I);
		if (Just(EKeys::Period) || Just(EKeys::Decimal)) S.Typed += TEXT(".");
		if (Just(EKeys::Hyphen) || Just(EKeys::Subtract)) S.Typed = S.Typed.StartsWith(TEXT("-")) ? S.Typed.RightChop(1) : TEXT("-") + S.Typed;
		if (Just(EKeys::BackSpace)) S.Typed = S.Typed.LeftChop(1);
		if (Just(EKeys::Escape)) S.TypeField = -1;
		if (Just(EKeys::Enter) || Just(EKeys::Tab))
		{
			if (!S.Typed.IsEmpty() && !S.Selected.IsNone())
			{
				PushUndo();
				const std::string Slot = TGMI::TgmStd(S.Selected.ToString());
				const TorqueFit::GeoHandle H = (TorqueFit::GeoHandle)(S.TypeField / 3);
				S.Draft.Set(Slot, TorqueFit::Geo::WithValue(S.Draft.Of(Slot), H, S.TypeField % 3, FCString::Atod(*S.Typed)));
				Refit(false);
			}
			S.TypeField = -1;
		}
		if (Just(EKeys::LeftMouseButton))
		{
			S.TypeField = -1;
			Press(Mouse);
		}
		return;
	}

	// Leaving: Esc or F2 (Circle / B on a pad), asking first if there are changes not saved.
	if (Just(EKeys::Escape) || Just(EKeys::F2) || Just(EKeys::Gamepad_FaceButton_Right) || (PadHit & ESonyButton::Circle))
	{
		if (S.bConfirm) S.bConfirm = false;
		else RequestExit();
		return;
	}
	if (S.bConfirm)
	{
		// The pad on the question: Cross saves and leaves, Square discards.
		if (Just(EKeys::Gamepad_FaceButton_Bottom) || (PadHit & ESonyButton::Cross)) { Save(); Close(); return; }
		if (Just(EKeys::Gamepad_FaceButton_Left) || (PadHit & ESonyButton::Square)) { DiscardAndClose(); return; }
		if (Just(EKeys::LeftMouseButton)) Press(Mouse);
		return;
	}

	// The mouse: work the panel, the gizmo or pick a part; right-drag orbits, middle pans, the wheel zooms.
	if (Just(EKeys::LeftMouseButton)) Press(Mouse);
	else if (Up(EKeys::LeftMouseButton)) Release();
	else if (Down(EKeys::LeftMouseButton) && !Moved.IsNearlyZero()) DragTo(Mouse);
	if (Just(EKeys::RightMouseButton)) S.bOrbit = true;
	if (Up(EKeys::RightMouseButton)) S.bOrbit = false;
	if (Just(EKeys::MiddleMouseButton)) S.bPan = true;
	if (Up(EKeys::MiddleMouseButton)) S.bPan = false;
	if (S.bOrbit && !S.bDrag)
	{
		S.Yaw += Moved.X * TGMI::OrbitSpeed;
		S.Pitch = FMath::Clamp(S.Pitch + Moved.Y * TGMI::OrbitSpeed, -1.4, 0.6);
	}
	else if (S.bPan && !S.bDrag)
	{
		const FRotator R = GetActorRotation();
		const FVector Right = FRotationMatrix(R).GetUnitAxis(EAxis::Y), UpV = FRotationMatrix(R).GetUnitAxis(EAxis::Z);
		S.Focus += (-Right * Moved.X + UpV * Moved.Y) * S.Dist * 0.0015;
	}
	const bool bOverList = S.ListBox.IsInside(Mouse);
	if (Just(EKeys::MouseScrollUp)) { if (bOverList) S.ListScroll = FMath::Max(0, S.ListScroll - 1); else S.Dist = FMath::Clamp(S.Dist * 0.9, TGMI::MinDist, TGMI::MaxDist); }
	if (Just(EKeys::MouseScrollDown)) { if (bOverList) ++S.ListScroll; else S.Dist = FMath::Clamp(S.Dist * 1.1, TGMI::MinDist, TGMI::MaxDist); }
	if (!S.bDrag && S.Slider < 0) S.Hover = PickHandle(Mouse);

	// The pad (finer: Godot's geometry mode is the mouse's): the right stick orbits, L2 / R2 zoom.
	auto Dead = [](float V) { return FMath::Abs(V) < 0.15f ? 0.0f : V; };
	const float Rx = Sony.bConnected ? Dead(Sony.RX) : Dead(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX));
	const float Ry = Sony.bConnected ? Dead(Sony.RY) : Dead(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY));
	S.Yaw += Rx * 2.0 * Dt;
	S.Pitch = FMath::Clamp(S.Pitch - Ry * 1.5 * Dt, -1.4, 0.6);
	const float Zoom = (Sony.bConnected ? Sony.R2 - Sony.L2 : PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) - PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis));
	if (FMath::Abs(Zoom) > 0.1f) S.Dist = FMath::Clamp(S.Dist * (1.0 - Zoom * Dt), TGMI::MinDist, TGMI::MaxDist);

	// The keys: W / E / R the handles (Triangle / Y turns through them), Z undo, Backspace resets.
	if (Just(EKeys::W)) S.Mode = TorqueFit::GeoHandle::Move;
	if (Just(EKeys::E)) S.Mode = TorqueFit::GeoHandle::Rotate;
	if (Just(EKeys::R)) S.Mode = TorqueFit::GeoHandle::Scale;
	if (Just(EKeys::Gamepad_FaceButton_Top) || (PadHit & ESonyButton::Triangle)) S.Mode = (TorqueFit::GeoHandle)(((int32)S.Mode + 1) % 3);
	if (S.Mode != TorqueFit::GeoHandle::Scale && S.Axis == TorqueFit::Geo::Uniform) S.Axis = 0;
	if (Just(EKeys::Z)) UndoOnce();
	if (Just(EKeys::BackSpace)) ResetPart();
	if (Just(EKeys::Gamepad_Special_Right) || (PadHit & ESonyButton::Options)) Save();
	// (Finer) L: along the part's own axes. L1 / R1 (or Tab): the part before or after in the list.
	if (Just(EKeys::L)) S.bPartAxes = !S.bPartAxes;
	if (FreeMove() && S.List.Num() > 0)
	{
		int32 Step = 0;
		if (Just(EKeys::Tab) || Just(EKeys::Gamepad_RightShoulder) || (PadHit & ESonyButton::R1)) Step = 1;
		if (Just(EKeys::Gamepad_LeftShoulder) || (PadHit & ESonyButton::L1)) Step = -1;
		if (Step != 0)
		{
			const int32 At = S.List.IndexOfByKey(S.Selected);
			Select(S.List[((At == INDEX_NONE ? (Step > 0 ? -1 : 0) : At) + Step + S.List.Num()) % S.List.Num()]);
		}
	}
	// (Finer) nudges: Up / Down pick the axis, Left / Right nudge along or about it a fine step
	// (Ctrl: Godot's snap step), as do the D-pad's (Square held: the bigger steps). Held, they repeat.
	const int32 Axes = S.Mode == TorqueFit::GeoHandle::Scale ? 4 : 3;
	if (Just(EKeys::Up) || Just(EKeys::Gamepad_DPad_Up) || (PadHit & ESonyButton::DUp)) S.Axis = (S.Axis + Axes - 1) % Axes;
	if (Just(EKeys::Down) || Just(EKeys::Gamepad_DPad_Down) || (PadHit & ESonyButton::DDown)) S.Axis = (S.Axis + 1) % Axes;
	int32 Nudge = 0;
	const bool bLeft = Down(EKeys::Left) || Down(EKeys::Gamepad_DPad_Left) || (PadDown & ESonyButton::DLeft);
	const bool bRight = Down(EKeys::Right) || Down(EKeys::Gamepad_DPad_Right) || (PadDown & ESonyButton::DRight);
	if (bLeft != bRight)
	{
		const bool bFirst = Just(EKeys::Left) || Just(EKeys::Right) || Just(EKeys::Gamepad_DPad_Left) || Just(EKeys::Gamepad_DPad_Right) ||
			(PadHit & (ESonyButton::DLeft | ESonyButton::DRight));
		if (bFirst) { Nudge = bRight ? 1 : -1; S.PadRepeat = 0.35; }
		else if ((S.PadRepeat -= Dt) <= 0.0) { Nudge = bRight ? 1 : -1; S.PadRepeat = 0.06; }
	}
	if (Nudge != 0 && FreeMove() && !S.Selected.IsNone() && !S.bDrag)
	{
		const bool bBig = bCtrl || Down(EKeys::Gamepad_FaceButton_Left) || (PadDown & ESonyButton::Square);
		const std::string Slot = TGMI::TgmStd(S.Selected.ToString());
		PushUndo();
		S.Draft.Set(Slot, TorqueFit::Geo::Nudged(S.Draft.Of(Slot), S.Mode, TGMI::TgmAxisOfKey(S.Axis), Nudge, !bBig, S.bPartAxes));
		Refit(false);
	}
}

// --- Drawing ----------------------------------------------------------------------------------------

namespace TorqueGeometryModeImpl
{
struct FTgmDraw
{
	AHUD* Hud = nullptr;
	UCanvas* C = nullptr;
	UFont* Font = nullptr;
	float S = 1.0f;       // 1080p pixels to the screen's
	float H0 = 20.0f;     // the font's height at scale 1
	FVector2D Mouse = FVector2D::ZeroVector;
	float Px(float Size) const { return Size * S / H0; }
	FVector2D Measure(const FString& T, float Size) const
	{
		float W = 0.0f, H = 0.0f;
		C->StrLen(Font, T, W, H);
		return FVector2D(W, H) * Px(Size);
	}
	void Text(const FString& T, float X, float Y, float Size, const FLinearColor& Col) const
	{
		C->SetDrawColor(Col.ToFColor(true));
		C->DrawText(Font, T, X, Y, Px(Size), Px(Size));
	}
	// Word-wrapped into Width: returns the height used.
	float Wrapped(const FString& T, float X, float Y, float Width, float Size, const FLinearColor& Col) const
	{
		TArray<FString> Words;
		T.ParseIntoArray(Words, TEXT(" "));
		FString Cur;
		float At = Y;
		const float Lh = Size * S * 1.25f;
		for (const FString& Word : Words)
		{
			const FString Try = Cur.IsEmpty() ? Word : Cur + TEXT(" ") + Word;
			if (!Cur.IsEmpty() && Measure(Try, Size).X > Width)
			{
				Text(Cur, X, At, Size, Col);
				At += Lh;
				Cur = Word;
			}
			else
				Cur = Try;
		}
		if (!Cur.IsEmpty())
		{
			Text(Cur, X, At, Size, Col);
			At += Lh;
		}
		return At - Y;
	}
	void Rect(float X, float Y, float W, float H, const FLinearColor& Col) const { Hud->DrawRect(Col, X, Y, W, H); }
	void Line(const FVector2D& A, const FVector2D& B, float Thick, const FLinearColor& Col) const { Hud->DrawLine(A.X, A.Y, B.X, B.Y, Col, Thick); }
};
} // namespace TorqueGeometryModeImpl

void ATorqueGeometryMode::DrawPanel(AHUD* Hud, UCanvas* Canvas)
{
	FState& S = *St;
	if (!Hud || !Canvas || !GEngine || !S.PC.IsValid()) return;
	TGMI::FTgmDraw D;
	D.Hud = Hud;
	D.C = Canvas;
	D.Font = GEngine->GetLargeFont();
	D.S = Canvas->ClipY / 1080.0f;
	float W0 = 0.0f, H0 = 0.0f;
	Canvas->StrLen(D.Font, TEXT("Ag"), W0, H0);
	D.H0 = FMath::Max(H0, 1.0f);
	D.Mouse = S.LastMouse;
	const float Sc = D.S;
	S.Hits.Reset();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const ATorqueShedBike* Bike = S.Bike.Get();
	APlayerController* PC = S.PC.Get();
	const bool bFree = FreeMove();

	// --- The gizmo (GeoGizmo), over the selected part, drawn over everything as Godot's is.
	const FVector* PivotCm = Bike && !S.Selected.IsNone() ? Bike->FitView.Pivots.Find(S.Selected) : nullptr;
	if (bFree && PivotCm && PC && !S.bConfirm)
	{
		const FTransform BikeXf = Bike->BikeRoot->GetComponentTransform();
		const TorqueFit::GeoOffset O = S.Draft.Of(TGMI::TgmStd(S.Selected.ToString()));
		const TGMI::GV3 Cg = TGMI::TgmGOf(*PivotCm) + O.P;
		auto World = [&](const TGMI::GV3& G) { return BikeXf.TransformPosition(TGMI::TgmUeOf(G)); };
		FVector Eye;
		FRotator EyeRot;
		PC->GetPlayerViewPoint(Eye, EyeRot);
		const double Size = FMath::Max(30.0, FVector::Dist(Eye, World(Cg))) * TGMI::ScreenSize / 100.0;
		auto Scr = [&](const TGMI::GV3& G, FVector2D& Out) {
			const FVector Wp = World(G);
			if (FVector::DotProduct(Wp - Eye, EyeRot.Vector()) <= 1.0) return false;
			return PC->ProjectWorldLocationToScreen(Wp, Out, false);
		};
		auto Seg = [&](const TGMI::GV3& A, const TGMI::GV3& Bp, float Thick, const FLinearColor& Col) {
			FVector2D Pa, Pb;
			if (Scr(A, Pa) && Scr(Bp, Pb)) D.Line(Pa, Pb, Thick * Sc, Col);
		};
		auto Cube = [&](const TGMI::GV3& At, const TGMI::GV3 Ax[3], double Half, const FLinearColor& Col) {
			for (int32 I = 0; I < 12; ++I)
			{
				// The 12 edges: along each axis, at the four corners of the other two.
				const int32 A = I / 4, Bc = (A + 1) % 3, Cc = (A + 2) % 3;
				const double Sb = (I & 1) ? 1.0 : -1.0, Scc = (I & 2) ? 1.0 : -1.0;
				const TGMI::GV3 Base = At + Ax[Bc] * (Sb * Half) + Ax[Cc] * (Scc * Half);
				Seg(Base - Ax[A] * Half, Base + Ax[A] * Half, 2.0f, Col);
			}
		};
		// A solid little box, as Torque Shed's BoxMesh handles: its six faces filled, far ones first, shaded by
		// axis so it reads as a box, then its edges outlined.
		auto SolidBox = [&](const TGMI::GV3& At, const TGMI::GV3 Ax[3], double Half, const FLinearColor& Col) {
			struct FBoxFace { double Dist = 0.0; FVector2D Pt[4]; float Shade = 1.0f; };
			TArray<FBoxFace> BoxFaces;
			for (int32 Fa = 0; Fa < 3; ++Fa)
				for (int32 Sgn = -1; Sgn <= 1; Sgn += 2)
				{
					const int32 Fb = (Fa + 1) % 3, Fc = (Fa + 2) % 3;
					const TGMI::GV3 Mid = At + Ax[Fa] * (Sgn * Half);
					const TGMI::GV3 Corners[4] = {Mid - Ax[Fb] * Half - Ax[Fc] * Half, Mid + Ax[Fb] * Half - Ax[Fc] * Half,
						Mid + Ax[Fb] * Half + Ax[Fc] * Half, Mid - Ax[Fb] * Half + Ax[Fc] * Half};
					FBoxFace Face;
					bool bOnScreen = true;
					for (int32 K = 0; K < 4; ++K) bOnScreen = Scr(Corners[K], Face.Pt[K]) && bOnScreen;
					if (!bOnScreen) continue;
					Face.Dist = FVector::Dist(World(Mid), Eye);
					Face.Shade = Fa == 0 ? 1.0f : (Fa == 1 ? 0.82f : 0.66f);
					BoxFaces.Add(Face);
				}
			BoxFaces.Sort([](const FBoxFace& L, const FBoxFace& R) { return L.Dist > R.Dist; });
			TArray<FCanvasUVTri> BoxTris;
			for (const FBoxFace& Face : BoxFaces)
			{
				const FLinearColor Fill(Col.R * Face.Shade, Col.G * Face.Shade, Col.B * Face.Shade, 1.0f);
				FCanvasUVTri T0, T1;
				T0.V0_Pos = Face.Pt[0]; T0.V1_Pos = Face.Pt[1]; T0.V2_Pos = Face.Pt[2];
				T1.V0_Pos = Face.Pt[0]; T1.V1_Pos = Face.Pt[2]; T1.V2_Pos = Face.Pt[3];
				T0.V0_Color = T0.V1_Color = T0.V2_Color = Fill;
				T1.V0_Color = T1.V1_Color = T1.V2_Color = Fill;
				BoxTris.Add(T0);
				BoxTris.Add(T1);
			}
			if (BoxTris.Num() > 0)
			{
				FCanvasTriangleItem Item(BoxTris, GWhiteTexture);
				Item.BlendMode = SE_BLEND_Opaque;
				Canvas->DrawItem(Item);
			}
			Cube(At, Ax, Half, FLinearColor(Col.R * 0.45f, Col.G * 0.45f, Col.B * 0.45f, 1.0f));
		};
		const int32 LitH = S.bDrag ? S.DragH : S.Hover;
		for (int32 A = 0; A < 3; ++A)
		{
			const FLinearColor Col = A == LitH ? TGMI::HoverColour : TGMI::AxisColours[A];
			TGMI::GV3 Dir;
			(A == 0 ? Dir.x : (A == 1 ? Dir.y : Dir.z)) = 1.0;
			if (S.Mode == TorqueFit::GeoHandle::Move && S.bPartAxes) Dir = TorqueFit::Geo::PartAxis(O, A);
			const TGMI::GV3 Side = Dir.Cross(std::fabs(Dir.y) < 0.9 ? TGMI::GV3(0, 1, 0) : TGMI::GV3(1, 0, 0)).Normalized();
			const TGMI::GV3 Side2 = Dir.Cross(Side);
			switch (S.Mode)
			{
			case TorqueFit::GeoHandle::Move:
			{
				// A shaft along the axis with a little box on its end (beenster: boxes, not arrow heads).
				Seg(Cg, Cg + Dir * (TGMI::Arm * 0.85 * Size), 3.0f, Col);
				const TGMI::GV3 BoxAx[3] = {Dir, Side, Side2};
				SolidBox(Cg + Dir * (TGMI::Arm * 0.9 * Size), BoxAx, 0.065 * Size, Col);
				break;
			}
			case TorqueFit::GeoHandle::Rotate:
			{
				TGMI::GV3 Prev = Cg + Side * (TGMI::Ring * Size);
				for (int32 K = 1; K <= 48; ++K)
				{
					const double T = 2.0 * PI * K / 48.0;
					const TGMI::GV3 Pt = Cg + (Side * std::cos(T) + Side2 * std::sin(T)) * (TGMI::Ring * Size);
					Seg(Prev, Pt, 3.0f, Col);
					Prev = Pt;
				}
				break;
			}
			default:
			{
				// A stalk with a box on its end, along the part's own (turned) axis.
				const TGMI::GV3 Ax[3] = {TorqueFit::Geo::PartAxis(O, 0), TorqueFit::Geo::PartAxis(O, 1), TorqueFit::Geo::PartAxis(O, 2)};
				Seg(Cg, Cg + Ax[A] * (TGMI::Arm * 0.85 * Size), 2.5f, Col);
				SolidBox(Cg + Ax[A] * (TGMI::Arm * 0.9 * Size), Ax, 0.065 * Size, Col);
				if (A == 2) SolidBox(Cg, Ax, 0.08 * Size, LitH == TorqueFit::Geo::Uniform ? TGMI::HoverColour : TGMI::MiddleColour);
				break;
			}
			}
		}
	}

	// --- The panel on the right (GeometryMode._ready's column).
	const float PW = 404.0f * Sc, PX = Canvas->ClipX - 16.0f * Sc - PW, Pad = 18.0f * Sc, IW = PW - 2.0f * Pad;
	float Y = 16.0f * Sc + Pad;
	const float Top = 16.0f * Sc;
	// (Laid out twice: once to measure, once to draw over its background.)
	auto Button = [&](float X, float By, float Bw, const FString& Label, bool bOn, TFunction<void()> Click) {
		const float Bh = 38.0f * Sc;
		const FBox2D R(FVector2D(X, By), FVector2D(X + Bw, By + Bh));
		D.Rect(X, By, Bw, Bh, bOn ? TGMI::ButtonOn : (R.IsInside(D.Mouse) ? TGMI::ButtonHover : TGMI::ButtonBg));
		const FVector2D Tm = D.Measure(Label, 16.0f);
		D.Text(Label, X + FMath::Max(8.0f * Sc, (Bw - Tm.X) * 0.5f), By + (Bh - Tm.Y) * 0.5f, 16.0f, FLinearColor::White);
		S.Hits.Add({R, MoveTemp(Click), -1});
		return Bh;
	};
	auto Check = [&](float X, float By, const FString& Label, bool bOn, TFunction<void()> Click) {
		const float Box = 18.0f * Sc;
		D.Rect(X, By + 2.0f * Sc, Box, Box, TGMI::ButtonBg);
		if (bOn) D.Rect(X + 4.0f * Sc, By + 6.0f * Sc, Box - 8.0f * Sc, Box - 8.0f * Sc, TGMI::Accent);
		D.Text(Label, X + Box + 8.0f * Sc, By, 16.0f, TGMI::TextCol);
		const FVector2D Tm = D.Measure(Label, 16.0f);
		S.Hits.Add({FBox2D(FVector2D(X, By), FVector2D(X + Box + 8.0f * Sc + Tm.X, By + 24.0f * Sc)), MoveTemp(Click), -1});
		return 26.0f * Sc;
	};
	// The background first, the height of what was drawn last frame (it barely changes).
	static float LastPanelH = 700.0f;
	D.Rect(PX, Top, PW, LastPanelH * Sc, TGMI::PanelBg);
	S.PanelBox = FBox2D(FVector2D(PX, Top), FVector2D(PX + PW, Top + LastPanelH * Sc));
	const float X = PX + Pad;
	D.Text(TEXT("GEOMETRY MODE"), X, Y, 24.0f, TGMI::Accent);
	Y += 32.0f * Sc;
	if (B && B->Exists(S.Bay)) D.Text(B->Bike(S.Bay).Name, X, Y, 17.0f, FLinearColor::White);
	Y += 26.0f * Sc;
	Y += D.Wrapped(TEXT("Tick Expert for the rig handles (the bike re-forms round them). With Expert on, click a part of the bike (or pick it in the list on the left) to "
		"select it, then drag the handles. W move, E rotate, R scale (the middle box sizes it all over). Hold Ctrl to snap, Z undo, Backspace resets the part. "
		"Right-drag to orbit, middle-drag to pan, wheel to zoom. Only this bike changes."), X, Y, IW, 14.0f, TGMI::Dim) + 8.0f * Sc;
	// The rig's range handles (catalogue bikes only).
	const bool bRig = HasRig();
	if (bRig)
	{
		// The rig handles (tank mounts, pegs, seat hoop) only show in Expert (beenster, Nov 10).
		const std::vector<TorqueFit::AdjustRange> Ranges = GExpert ? TorqueFit::BikeLayout::AdjustRanges() : std::vector<TorqueFit::AdjustRange>();
		if (GExpert)
		{
			D.Text(TEXT("RIG HANDLES: the bike re-forms round them"), X, Y, 14.0f, TGMI::Accent);
			Y += 24.0f * Sc;
		}
		for (int32 K = 0; K < (int32)Ranges.size() && K < 4; ++K)
		{
			const TorqueFit::AdjustRange& R = Ranges[K];
			const TorqueFit::LayoutAdjust& A = S.Draft.Rig;
			const double V = R.Key == "tank_up" ? A.TankUpMm : R.Key == "pegs" ? A.PegsMm : R.Key == "hoop_up" ? A.HoopUpMm : A.HoopDeg;
			D.Text(TGMI::TgmUe(R.Name), X, Y, 14.0f, FLinearColor(1, 1, 1, 0.7f));
			const float Tx = X + 190.0f * Sc, Tw = IW - 190.0f * Sc - 70.0f * Sc;
			const float Ty = Y + 10.0f * Sc;
			D.Rect(Tx, Ty - 2.0f * Sc, Tw, 4.0f * Sc, TGMI::ButtonHover);
			const float F = (float)((V - R.Min) / FMath::Max(R.Max - R.Min, 1e-6));
			D.Rect(Tx + F * Tw - 6.0f * Sc, Ty - 9.0f * Sc, 12.0f * Sc, 18.0f * Sc, S.Slider == K ? TGMI::HoverColour : TGMI::Accent);
			S.SliderTrack[K] = FBox2D(FVector2D(Tx, Ty - 12.0f * Sc), FVector2D(Tx + Tw, Ty + 12.0f * Sc));
			S.Hits.Add({S.SliderTrack[K], TFunction<void()>(), K});
			const FString Val = R.Unit == "deg" ? FString::Printf(TEXT("%+.1f\u00B0"), V) : FString::Printf(TEXT("%+.0f %s"), V, *TGMI::TgmUe(R.Unit));
			D.Text(Val, Tx + Tw + 10.0f * Sc, Y, 14.0f, TGMI::Value);
			Y += 26.0f * Sc;
		}
		Y += Check(X, Y, TEXT("Expert: free move (any part, any way)"), GExpert, [this]() { SetExpert(!GExpert); }) + 6.0f * Sc;
	}
	if (bFree)
	{
		// The handles, Always snap, and the selected part's readout.
		const TCHAR* Names[3] = {TEXT("Move (W)"), TEXT("Rotate (E)"), TEXT("Scale (R)")};
		const float Bw = (IW - 12.0f * Sc) / 3.0f;
		for (int32 M = 0; M < 3; ++M)
			Button(X + M * (Bw + 6.0f * Sc), Y, Bw, Names[M], (int32)S.Mode == M, [this, M]() { St->Mode = (TorqueFit::GeoHandle)M; });
		Y += 46.0f * Sc;
		Y += Check(X, Y, TEXT("Always snap (or hold Ctrl)"), S.bSnapAlways, [this]() { St->bSnapAlways = !St->bSnapAlways; });
		// (Finer) along the bike's axes or the part's own.
		Y += Check(X, Y, TEXT("Move along the part's own axes (L)"), S.bPartAxes, [this]() { St->bPartAxes = !St->bPartAxes; }) + 4.0f * Sc;
		if (S.Selected.IsNone())
		{
			int32 Moved = (int32)S.Draft.Slots.size();
			D.Text(TEXT("No part selected: click one"), X, Y, 17.0f, TGMI::Accent);
			Y += 26.0f * Sc;
			D.Text(FString::Printf(TEXT("%d part%s moved on this bike%s"), Moved, Moved == 1 ? TEXT("") : TEXT("s"), IsDirty() ? TEXT("  (not saved)") : TEXT("")), X, Y, 15.0f, TGMI::Value);
			Y += 24.0f * Sc;
		}
		else
		{
			const int32 Item = B ? B->ItemOn(S.Bay, S.Selected) : 0;
			D.Text(TorqueShedParts::SlotName(S.Selected) + TEXT(": ") + (Item != 0 ? B->ItemTitle(Item) : FString(TEXT("(empty)"))), X, Y, 17.0f, TGMI::Accent);
			Y += 26.0f * Sc;
			// The readout, each number a field to click and type into (finer).
			const TorqueFit::GeoOffset O = S.Draft.Of(TGMI::TgmStd(S.Selected.ToString()));
			const TCHAR* Rows[3] = {TEXT("Move"), TEXT("Rotate"), TEXT("Scale")};
			const TCHAR* Units[3] = {TEXT("cm"), TEXT("deg"), TEXT("%")};
			const double Vals[3][3] = {{O.P.x * 100.0, O.P.y * 100.0, O.P.z * 100.0}, {O.R.x, O.R.y, O.R.z}, {O.S.x * 100.0, O.S.y * 100.0, O.S.z * 100.0}};
			const TCHAR* AxisNames[3] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
			for (int32 Rw = 0; Rw < 3; ++Rw)
			{
				D.Text(Rows[Rw], X, Y, 15.0f, TGMI::Value);
				for (int32 Ax = 0; Ax < 3; ++Ax)
				{
					const int32 Field = Rw * 3 + Ax;
					const float Fx = X + 70.0f * Sc + Ax * 92.0f * Sc;
					const bool bTyping = S.TypeField == Field;
					const bool bAxisLit = (int32)S.Mode == Rw && (S.Axis == Ax || (S.Axis == TorqueFit::Geo::Uniform && Rw == 2));
					const FString Num = bTyping ? S.Typed + TEXT("_") : (Rw == 2 ? FString::Printf(TEXT("%5.0f"), Vals[Rw][Ax]) : FString::Printf(TEXT("%+6.1f"), Vals[Rw][Ax]));
					const FBox2D R(FVector2D(Fx, Y), FVector2D(Fx + 88.0f * Sc, Y + 22.0f * Sc));
					if (bTyping || R.IsInside(D.Mouse)) D.Rect(Fx - 2.0f * Sc, Y, 88.0f * Sc, 22.0f * Sc, TGMI::ButtonHover);
					D.Text(FString(AxisNames[Ax]) + TEXT(" ") + Num, Fx, Y, 15.0f, bAxisLit ? TGMI::HoverColour : TGMI::Value);
					S.Hits.Add({R, [this, Field]() { St->TypeField = Field; St->Typed.Reset(); }, -1});
				}
				D.Text(Units[Rw], X + 70.0f * Sc + 3 * 92.0f * Sc, Y, 15.0f, TGMI::Value);
				Y += 22.0f * Sc;
			}
			if (IsDirty())
			{
				D.Text(TEXT("(not saved)"), X, Y, 15.0f, TGMI::Value);
				Y += 22.0f * Sc;
			}
			// (Finer) the gaps to the parts round it.
			if (S.Gaps.Num() > 0)
			{
				FString G = TEXT("Gaps:");
				for (int32 I = 0; I < S.Gaps.Num() && I < 5; ++I)
				{
					const double Mm = S.Gaps[I].Gap * 1000.0;
					G += FString::Printf(TEXT(" %s %s%s"), *TorqueShedParts::SlotName(FName(*TGMI::TgmUe(S.Gaps[I].Slot))).ToLower(),
						Mm < 0.5 ? TEXT("touching") : *FString::Printf(TEXT("%.0f mm"), Mm), I + 1 < FMath::Min(S.Gaps.Num(), 5) ? TEXT(",") : TEXT(""));
				}
				Y += D.Wrapped(G, X, Y, IW, 14.0f, TGMI::Value);
			}
		}
		Y += D.Wrapped(TEXT("Finer: Ctrl+Shift snaps to 1 mm, 0.5\u00B0 and 1%; Shift drags slowly; Left / Right nudge the lit axis (Up / Down picks it, Ctrl for "
			"bigger steps); click a number to type it."), X, Y, IW, 13.0f, TGMI::Dim) + 6.0f * Sc;
	}
	{
		const float Bw = (IW - 12.0f * Sc) / 3.0f;
		Button(X, Y, Bw, TEXT("Undo (Z)"), false, [this]() { UndoOnce(); });
		Button(X + Bw + 6.0f * Sc, Y, Bw, TEXT("Reset part"), false, [this]() { ResetPart(); });
		Button(X + 2.0f * (Bw + 6.0f * Sc), Y, Bw, TEXT("Reset all"), false, [this]() { ResetAll(); });
		Y += 46.0f * Sc;
	}
	Y += Button(X, Y, IW, TEXT("Save to this bike"), false, [this]() { Save(); }) + 8.0f * Sc;
	Y += Button(X, Y, IW, TEXT("Exit (F2 / Esc)"), false, [this]() { RequestExit(); }) + 8.0f * Sc;
	if (!S.Status.IsEmpty()) Y += D.Wrapped(S.Status, X, Y, IW, 14.0f, TGMI::StatusCol);
	LastPanelH = (Y - Top + Pad) / Sc;

	// --- The parts list on the left: click one to select it, as clicking it on the bike.
	if (bFree)
	{
		const float LX = 16.0f * Sc, LW = 320.0f * Sc, LTop = 16.0f * Sc, LH = Canvas->ClipY - 32.0f * Sc;
		D.Rect(LX, LTop, LW, LH, TGMI::PanelBg);
		S.ListBox = FBox2D(FVector2D(LX, LTop), FVector2D(LX + LW, LTop + LH));
		float Ly = LTop + Pad;
		D.Text(TEXT("PARTS"), LX + Pad, Ly, 20.0f, TGMI::Accent);
		Ly += 28.0f * Sc;
		D.Text(TEXT("Click one to select it."), LX + Pad, Ly, 14.0f, TGMI::Dim);
		Ly += 24.0f * Sc;
		const float RowH = 34.0f * Sc;
		const int32 Fits = FMath::Max(1, (int32)((LTop + LH - Pad - Ly) / RowH));
		S.ListScroll = FMath::Clamp(S.ListScroll, 0, FMath::Max(0, S.List.Num() - Fits));
		if (S.List.Num() == 0) D.Text(TEXT("No parts on this bike."), LX + Pad, Ly, 14.0f, FLinearColor(1, 1, 1, 0.5f));
		for (int32 I = S.ListScroll; I < S.List.Num() && I < S.ListScroll + Fits; ++I)
		{
			const FName Slot = S.List[I];
			const int32 Item = B ? B->ItemOn(S.Bay, Slot) : 0;
			FString Row = (S.Draft.Slots.count(TGMI::TgmStd(Slot.ToString())) ? TEXT("* ") : TEXT("")) + TorqueShedParts::SlotName(Slot) + TEXT(": ") +
				(Item != 0 ? B->ItemTitle(Item) : FString(TEXT("(empty)")));
			while (Row.Len() > 4 && D.Measure(Row, 14.0f).X > LW - 2.0f * Pad - 16.0f * Sc) Row = Row.LeftChop(4) + TEXT("...");
			const FBox2D R(FVector2D(LX + Pad, Ly), FVector2D(LX + LW - Pad, Ly + RowH - 4.0f * Sc));
			D.Rect(R.Min.X, R.Min.Y, R.Max.X - R.Min.X, R.Max.Y - R.Min.Y, Slot == S.Selected ? TGMI::ButtonOn : (R.IsInside(D.Mouse) ? TGMI::ButtonHover : TGMI::ButtonBg));
			D.Text(Row, R.Min.X + 8.0f * Sc, R.Min.Y + 6.0f * Sc, 14.0f, FLinearColor::White);
			S.Hits.Add({R, [this, Slot]() { Select(Slot); }, -1});
			Ly += RowH;
		}
	}
	else
		S.ListBox = FBox2D(ForceInit);

	// --- Save before leaving? (in the middle, over the rest)
	if (S.bConfirm)
	{
		const float CW = 520.0f * Sc, CX = (Canvas->ClipX - CW) * 0.5f, CY = Canvas->ClipY * 0.5f - 80.0f * Sc;
		D.Rect(CX, CY, CW, 150.0f * Sc, FLinearColor(0.07f, 0.08f, 0.1f, 0.97f));
		const FString Name = B && B->Exists(S.Bay) ? B->Bike(S.Bay).Name : FString(TEXT("this bike"));
		const float Th = D.Wrapped(FString::Printf(TEXT("You've changed %s and not saved it. Save the changes to this bike?"), *Name), CX + Pad, CY + Pad, CW - 2.0f * Pad, 17.0f, FLinearColor::White);
		// (The question's own buttons win over the panel's under it.)
		S.Hits.Reset();
		const float Bw = (CW - 2.0f * Pad - 12.0f * Sc) / 3.0f, By = CY + Pad + Th + 10.0f * Sc;
		Button(CX + Pad, By, Bw, TEXT("Save and exit"), false, [this]() { Save(); Close(); });
		Button(CX + Pad + Bw + 6.0f * Sc, By, Bw, TEXT("Discard changes"), false, [this]() { DiscardAndClose(); });
		Button(CX + Pad + 2.0f * (Bw + 6.0f * Sc), By, Bw, TEXT("Keep editing"), false, [this]() { St->bConfirm = false; });
	}
}
