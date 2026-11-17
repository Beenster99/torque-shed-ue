#include "Shed/TorqueDecalMode.h"

#include "Coop/TorqueKeys.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueUi.h"
#include "Fit/BikeDecal.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueDecals.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueProfile.h"
#include "RiderRig.h"
#include "Engine/StaticMesh.h"
#include "SonyPad.h"
#include "CanvasItem.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "TextureResource.h"

#include <string>
#include <vector>

namespace TorqueDecalModeImpl
{
namespace BD = TorqueFit::Decals;
using DmV3 = TorqueSim::V3;

DmV3 DmOf(const FVector& V) { return DmV3(V.X, V.Y, V.Z); }
FVector DmUe(const DmV3& V) { return FVector(V.x, V.y, V.z); }

// The swatches (sRGB), fill and ink alike.
const uint32 Palette[] = {0x0A0A0A, 0xF4F1EA, 0xC81A12, 0x7A0E0E, 0xE36A12, 0xF2C230, 0xC9A23A, 0xA7A9AC,
	0xE8DCC0, 0x2E7D32, 0x0F3D24, 0x1E5BC6, 0x14213D, 0x6A2C91, 0xE05C9A, 0x6B4226};
constexpr int32 PaletteN = UE_ARRAY_COUNT(Palette);

FLinearColor Swatch(uint32 V)
{
	return FLinearColor::FromSRGBColor(FColor((uint8)((V >> 16) & 0xFFu), (uint8)((V >> 8) & 0xFFu), (uint8)(V & 0xFFu), 255));
}

const TCHAR* const TabNames[] = {TEXT("Lettering"), TEXT("Stripes"), TEXT("Artwork"), TEXT("Yours")};
const TCHAR* const TabShelf[] = {TEXT(""), TEXT("shapes"), TEXT("art"), TEXT("")};

FString SlotName(FName Slot)
{
	FString N = Slot.ToString().Replace(TEXT("_"), TEXT(" "));
	return N.Left(1).ToUpper() + N.Mid(1);
}

uint64 GDecalClosedFrame = 0;   // the frame decal mode last closed in
} // namespace TorqueDecalModeImpl

namespace TDMI = TorqueDecalModeImpl;

struct ATorqueDecalMode::FState
{
	TWeakObjectPtr<ATorqueWalker> Walker;
	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<ATorqueShedBike> Bike;
	int32 Bay = INDEX_NONE;
	uint64 OpenFrame = 0;
	// Each decal-taking slot's decals as being edited, and as saved (their text).
	TMap<FName, std::vector<TorqueFit::Decals::Decal>> Draft;
	TMap<FName, FString> Saved;
	TArray<FName> Slots;
	// What's in hand to place (picked off a shelf), and the decal picked on the bike.
	TorqueFit::Decals::Decal Brush;
	bool bBrush = false;
	FName SelSlot;
	int32 Sel = INDEX_NONE;
	bool bDrag = false;
	// Under the mouse on the bike.
	bool bHover = false;
	FName HoverSlot;
	FVector HoverAt = FVector::ZeroVector, HoverN = FVector::UpVector;
	// The panel.
	int32 Tab = 2;
	int32 Scroll = 0;
	FString Typed;
	bool bTyping = false;
	FString FaceId = TEXT("fraktur");
	struct FHit
	{
		FBox2D R;
		TFunction<void()> Click;
	};
	TArray<FHit> Hits;
	FBox2D PanelBox = FBox2D(ForceInit);
	bool bConfirm = false;
	FString Status;
	// The camera.
	bool bOrbit = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	double Yaw = 0.0, Pitch = -0.25, Dist = 200.0;
	FVector Focus = FVector::ZeroVector;
	// What's drawn on each slot (its text), and when the hover preview was last drawn again.
	TMap<FName, FString> Shown;
	double ShownAt = 0.0;
	// The slots' frames and triangles, made again when the bike's drawn again.
	struct FSlotShape
	{
		TArray<UStaticMeshComponent*> Meshes;
		FTransform Frame;
		std::vector<TDMI::DmV3> Tris;
	};
	TMap<FName, FSlotShape> Shapes;
	UStaticMeshComponent* SeenFirst = nullptr;
	int32 SeenCount = -1;
	TArray<TWeakObjectPtr<AActor>> Hidden;
	uint32 PrevPad = 0;
	// The helmet on its stand (stage 4), and where the camera looks at the bike.
	UStaticMeshComponent* Stand = nullptr;
	UStaticMeshComponent* Pole = nullptr;
	bool bAtHelmet = false;
	FVector BikeFocus = FVector::ZeroVector;
	double BikeDist = 200.0;
};

namespace TorqueDecalModeImpl
{
const FName HelmetSlot(TEXT("helmet"));
}

ATorqueDecalMode::ATorqueDecalMode()
{
	PrimaryActorTick.bCanEverTick = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("DecalCamera"));
	Camera->SetFieldOfView(55.0f);
	RootComponent = Camera;
	St = MakeShared<FState>();
}

ATorqueDecalMode::~ATorqueDecalMode() = default;

ATorqueDecalMode* ATorqueDecalMode::For(const AController* Controller)
{
	if (!Controller || !Controller->GetWorld()) return nullptr;
	for (TActorIterator<ATorqueDecalMode> It(Controller->GetWorld()); It; ++It)
		if (It->St && It->St->PC.Get() == Controller && !It->IsActorBeingDestroyed()) return *It;
	return nullptr;
}

bool ATorqueDecalMode::Open(ATorqueWalker* Walker, int32 LookedAtBay, FString& WhyNot)
{
	if (GFrameCounter <= TDMI::GDecalClosedFrame + 1) return true;   // (the F3 that just closed it)
	UWorld* World = Walker ? Walker->GetWorld() : nullptr;
	APlayerController* PC = Walker ? Cast<APlayerController>(Walker->GetController()) : nullptr;
	if (!World || !PC) return false;
	if (For(PC)) return true;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(Walker);
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
		WhyNot = TEXT("Walk up to a bike and look at it to put decals on it.");
		return false;
	}
	bool bAny = false;
	for (UStaticMeshComponent* C : Best->Parts)
		bAny = bAny || (C && TorqueDecals::TakesDecals(TorqueShedModel::SlotTag(C)));
	if (!bAny)
	{
		WhyNot = TEXT("That bike has no tank, fairing, panels or guards to put decals on yet.");
		return false;
	}
	ATorqueDecalMode* M = World->SpawnActor<ATorqueDecalMode>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!M) return false;
	M->Begin(Walker, Best, Best->Bay);
	return true;
}

void ATorqueDecalMode::Begin(ATorqueWalker* Walker, ATorqueShedBike* Bike, int32 Bay)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	S.Walker = Walker;
	S.PC = Cast<APlayerController>(Walker->GetController());
	S.Bike = Bike;
	S.Bay = Bay;
	S.OpenFrame = GFrameCounter;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	for (UStaticMeshComponent* C : Bike->Parts)
	{
		const FName Slot = C ? TorqueShedModel::SlotTag(C) : NAME_None;
		if (!TorqueDecals::TakesDecals(Slot) || S.Slots.Contains(Slot)) continue;
		S.Slots.Add(Slot);
		const FString Text = B ? B->DecalsOf(Bay, Slot) : FString();
		S.Saved.Add(Slot, Text);
		S.Draft.Add(Slot, BD::Parse(TorqueDecals::ToStd(Text)));
	}
	// The brush to start with: the first skull, red and bone.
	S.Brush = BD::Decal();
	S.Brush.Rgb1 = 0xE8DCC0;
	S.Brush.Rgb2 = 0x0A0A0A;
	S.Brush.Flag = BD::Filled;
	// The camera orbits the tank from where you stood.
	S.Focus = Bike->GetActorLocation() + FVector(0.0, 0.0, 80.0);
	S.BikeFocus = S.Focus;
	FVector From = S.Focus + FVector(0.0, 160.0, 30.0);
	if (Walker->Camera) From = Walker->Camera->GetComponentLocation();
	const FVector V = From - S.Focus;
	S.Dist = FMath::Clamp(V.Size(), 90.0, 260.0);
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
		const FSonyPadState Now = Walker->PadState();
		S.PrevPad = Now.bConnected ? Now.Buttons : 0;
	}
	if (B)
	{
		B->ReportActivity(FString::Printf(TEXT("putting decals on bay %d's bike"), Bay + 1));
		TellHandle = B->OnTell.AddLambda([this](int32 Peer, const FString& Text, bool) {
			const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
			if (Sub && Peer == Sub->Me()) Say(Text);
		});
	}
	Say(TEXT("Pick a decal on the left, then click the bike to stick it on."));
}

void ATorqueDecalMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnTell.Remove(TellHandle);
	HideWalker(false);
	Super::EndPlay(Reason);
}

void ATorqueDecalMode::Say(const FString& Text) { St->Status = Text; }

bool ATorqueDecalMode::IsDirty() const
{
	for (const TPair<FName, std::vector<TorqueFit::Decals::Decal>>& KV : St->Draft)
		if (TorqueDecals::ToUe(TorqueFit::Decals::ToText(KV.Value)) != St->Saved.FindRef(KV.Key)) return true;
	return false;
}

void ATorqueDecalMode::Save()
{
	FState& S = *St;
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	int32 Sent = 0;
	for (const TPair<FName, std::vector<TorqueFit::Decals::Decal>>& KV : S.Draft)
	{
		const FString Text = TorqueDecals::ToUe(TorqueFit::Decals::ToText(KV.Value));
		if (Text == S.Saved.FindRef(KV.Key)) continue;
		if (KV.Key == TDMI::HelmetSlot)
		{
			// The helmet's are this player's own (sent to friends with the player, pictures and all).
			TorqueDecals::SetMyHelmet(Text);
			S.Saved.Add(KV.Key, Text);
			++Sent;
			Say(TEXT("Saved the helmet's decals."));
			continue;
		}
		// A joined player's own pictures go to the host first (it passes them on to everyone).
		if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(S.PC.Get()))
			for (const TorqueFit::Decals::Decal& D : KV.Value)
				if (D.K == TorqueFit::Decals::Kind::Image) TPC->SendDecalPicture(TorqueDecals::ToUe(D.Id));
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::DecalSave;
		R.Bay = S.Bay;
		R.Item = B->ItemOn(S.Bay, KV.Key);
		R.Text = Text;
		FString WhyNot;
		if (B->Request(R, WhyNot))
		{
			S.Saved.Add(KV.Key, Text);
			++Sent;
		}
		else
			Say(WhyNot);
	}
	if (Sent == 0 && S.Status.IsEmpty()) Say(TEXT("Nothing new to save."));
}

void ATorqueDecalMode::RequestExit()
{
	if (IsDirty() && !St->bConfirm)
	{
		St->bConfirm = true;
		Say(TEXT("Not saved: Enter saves and leaves, Esc again leaves them off."));
		return;
	}
	Close();
}

void ATorqueDecalMode::Close()
{
	FState& S = *St;
	// The bike as saved (the previews put back).
	if (ATorqueShedBike* Bike = S.Bike.Get())
	{
		UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
		const int32 Bay = S.Bay;
		TorqueDecals::ApplyBike(Bike->Parts, [B, Bay](FName Slot) { return B ? B->DecalsOf(Bay, Slot) : FString(); });
	}
	HideWalker(false);
	if (APlayerController* PC = S.PC.Get())
	{
		if (ATorqueWalker* W = S.Walker.Get()) PC->SetViewTargetWithBlend(W, 0.25f);
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->ReportActivity(FString());
	S.PC = nullptr;
	TDMI::GDecalClosedFrame = GFrameCounter;
	Destroy();
}

void ATorqueDecalMode::HideWalker(bool bHide)
{
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
	Walker->GetAttachedActors(Mine, true, true);
	Mine.Add(Walker);
	for (AActor* A : Mine)
	{
		if (!A || A == this || S.Hidden.Contains(A)) continue;
		S.Hidden.Add(A);
		PC->HiddenActors.AddUnique(A);
	}
}

void ATorqueDecalMode::UpdateCamera()
{
	FState& S = *St;
	const FVector D(FMath::Cos(S.Pitch) * FMath::Cos(S.Yaw), FMath::Cos(S.Pitch) * FMath::Sin(S.Yaw), -FMath::Sin(S.Pitch));
	const FVector At = S.Focus + D * S.Dist;
	SetActorLocationAndRotation(At, (S.Focus - At).Rotation());
}

// --- The bike under the mouse --------------------------------------------------------------------

bool ATorqueDecalMode::HoverAt(const FVector2D& Mouse, FName& OutSlot, FVector& OutAt, FVector& OutN) const
{
	const FState& S = *St;
	const APlayerController* PC = S.PC.Get();
	if (!PC) return false;
	FVector From, Dir;
	if (!PC->DeprojectScreenPositionToWorld(Mouse.X, Mouse.Y, From, Dir)) return false;
	double Best = TNumericLimits<double>::Max();
	bool bFound = false;
	for (const TPair<FName, FState::FSlotShape>& KV : S.Shapes)
	{
		const FTransform& F = KV.Value.Frame;
		TDMI::DmV3 Hit, N;
		if (!TorqueFit::Decals::RayHit(KV.Value.Tris, TDMI::DmOf(F.InverseTransformPosition(From)), TDMI::DmOf(F.InverseTransformVector(Dir)), Hit, N))
			continue;
		const double D = FVector::Dist(F.TransformPosition(TDMI::DmUe(Hit)), From);
		if (D < Best)
		{
			Best = D;
			bFound = true;
			OutSlot = KV.Key;
			OutAt = TDMI::DmUe(Hit);
			OutN = TDMI::DmUe(N);
		}
	}
	return bFound;
}

int32 ATorqueDecalMode::DecalUnder(FName Slot, const FVector& At) const
{
	namespace BD = TorqueFit::Decals;
	const std::vector<BD::Decal>* L = St->Draft.Find(Slot);
	if (!L) return INDEX_NONE;
	for (int32 I = (int32)L->size() - 1; I >= 0; --I)
	{
		const BD::Decal& D0 = (*L)[(size_t)I];
		for (int32 Twin = 0; Twin < (D0.Has(BD::Pair) ? 2 : 1); ++Twin)
		{
			const BD::Decal D = Twin == 0 ? D0 : BD::Mirrored(D0);
			const BD::Frame F = BD::FrameOf(D);
			const TDMI::DmV3 Q = TDMI::DmOf(At) - F.O;
			if (FMath::Abs(Q.Dot(F.R)) <= D.W * 0.5 && FMath::Abs(Q.Dot(F.U)) <= D.H * 0.5 && FMath::Abs(Q.Dot(F.N)) < 3.0) return I;
		}
	}
	return INDEX_NONE;
}

// --- What's done to the decals ---------------------------------------------------------------------

void ATorqueDecalMode::PlaceBrush(FName Slot, const FVector& At, const FVector& N)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	std::vector<BD::Decal>& L = S.Draft.FindOrAdd(Slot);
	if ((int32)L.size() >= BD::MaxPerPart)
	{
		Say(FString::Printf(TEXT("The %s has eight decals already: take one off first."), *TDMI::SlotName(Slot).ToLower()));
		return;
	}
	BD::Decal D = S.Brush;
	D.At = TDMI::DmOf(At);
	D.Normal = TDMI::DmOf(N).Normalized();
	L.push_back(D);
	S.SelSlot = Slot;
	S.Sel = (int32)L.size() - 1;
	S.bConfirm = false;
	Say(FString::Printf(TEXT("Stuck on the %s (%d of 8). Drag to slide it; Shift + wheel sizes, Ctrl + wheel turns."), *TDMI::SlotName(Slot).ToLower(), (int32)L.size()));
}

// The decal picked on the bike (or null).
static TorqueFit::Decals::Decal* PickedDecal(TMap<FName, std::vector<TorqueFit::Decals::Decal>>& Draft, FName Slot, int32 Sel)
{
	std::vector<TorqueFit::Decals::Decal>* L = Draft.Find(Slot);
	return L && Sel >= 0 && Sel < (int32)L->size() ? &(*L)[(size_t)Sel] : nullptr;
}

void ATorqueDecalMode::Resize(float Factor)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	auto Grow = [Factor](BD::Decal& D) {
		const double Fa = FMath::Clamp((double)Factor, BD::MinCm / FMath::Min(D.W, D.H), BD::MaxCm / FMath::Max(D.W, D.H));
		D.W *= Fa;
		D.H *= Fa;
	};
	if (BD::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel)) Grow(*D);
	Grow(S.Brush);
	S.bConfirm = false;
}

void ATorqueDecalMode::Turn(float Degrees)
{
	FState& S = *St;
	if (TorqueFit::Decals::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel)) D->Turn = FMath::Fmod(D->Turn + Degrees, 360.0);
	S.Brush.Turn = FMath::Fmod(S.Brush.Turn + Degrees, 360.0);
	S.bConfirm = false;
}

void ATorqueDecalMode::Remove()
{
	FState& S = *St;
	std::vector<TorqueFit::Decals::Decal>* L = S.Draft.Find(S.SelSlot);
	if (!L || S.Sel < 0 || S.Sel >= (int32)L->size()) return;
	L->erase(L->begin() + S.Sel);
	S.Sel = INDEX_NONE;
	S.bConfirm = false;
	Say(TEXT("Taken off."));
}

void ATorqueDecalMode::SetColour(bool bInk, uint32 Rgb)
{
	FState& S = *St;
	auto Set = [bInk, Rgb](TorqueFit::Decals::Decal& D) { (bInk ? D.Rgb2 : D.Rgb1) = Rgb; };
	if (TorqueFit::Decals::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel)) Set(*D);
	Set(S.Brush);
	S.bConfirm = false;
}

void ATorqueDecalMode::Toggle(uint32 Flag)
{
	FState& S = *St;
	TorqueFit::Decals::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel);
	const bool bOn = !((D ? D->Flag : S.Brush.Flag) & Flag);
	auto Set = [Flag, bOn](TorqueFit::Decals::Decal& Dc) { Dc.Flag = bOn ? (Dc.Flag | Flag) : (Dc.Flag & ~Flag); };
	if (D) Set(*D);
	Set(S.Brush);
	S.bConfirm = false;
}

void ATorqueDecalMode::Arch(float Step)
{
	FState& S = *St;
	TorqueFit::Decals::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel);
	const double Now = FMath::Clamp((D ? D->Arch : S.Brush.Arch) + Step, 0.0, 1.0);
	if (D) D->Arch = Now;
	S.Brush.Arch = Now;
	S.bConfirm = false;
}

void ATorqueDecalMode::PickLibrary(const FString& Id)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	const TorqueDecals::FLibDecal* L = TorqueDecals::Find(Id);
	if (!L) return;
	S.Brush.K = BD::Kind::Library;
	S.Brush.Id = TorqueDecals::ToStd(Id);
	S.Brush.Text.clear();
	S.Brush.Arch = 0.0;
	// Its longer side 16 cm to start with (a long stripe 30).
	const double Aspect = FMath::Max(0.05, (double)L->Aspect());
	const double Long = Aspect > 2.5 ? 30.0 : 16.0;
	S.Brush.W = Aspect >= 1.0 ? Long : Long * Aspect;
	S.Brush.H = Aspect >= 1.0 ? Long / Aspect : Long;
	S.bBrush = true;
	S.Sel = INDEX_NONE;
	Say(FString::Printf(TEXT("%s in hand: click the bike to stick it on (Esc puts it back)."), *L->Name));
}

void ATorqueDecalMode::PickText()
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	const std::string Words = BD::Clean(TorqueDecals::ToStd(S.Typed));
	if (Words.empty())
	{
		S.bTyping = true;
		Say(TEXT("Type the words first."));
		return;
	}
	S.Brush.K = BD::Kind::Text;
	S.Brush.Id = TorqueDecals::ToStd(S.FaceId);
	S.Brush.Text = Words;
	float Aspect = 4.0f;
	bool bFull = false;
	TorqueDecals::Picture(this, S.Brush, Aspect, bFull);
	S.Brush.H = 5.0;
	S.Brush.W = FMath::Clamp(5.0 * (double)Aspect, BD::MinCm, BD::MaxCm);
	S.bBrush = true;
	S.Sel = INDEX_NONE;
	Say(TEXT("Lettering in hand: click the bike to stick it on."));
}

void ATorqueDecalMode::PickOwn(const FString& Name)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	// Kept as a shared copy (512 across, named by what's in it) so friends can be sent it.
	const FString Id = TorqueDecals::ShareOwn(Name);
	S.Brush.K = BD::Kind::Image;
	S.Brush.Id = TorqueDecals::ToStd(Id);
	S.Brush.Text.clear();
	float Aspect = 1.0f;
	bool bFull = true;
	if (Id.IsEmpty() || !TorqueDecals::Picture(this, S.Brush, Aspect, bFull))
	{
		Say(TEXT("That picture didn't load (a PNG, please)."));
		return;
	}
	S.Brush.W = Aspect >= 1.0f ? 16.0 : 16.0 * Aspect;
	S.Brush.H = Aspect >= 1.0f ? 16.0 / Aspect : 16.0;
	S.bBrush = true;
	S.Sel = INDEX_NONE;
	Say(FString::Printf(TEXT("%s in hand: click the bike to stick it on."), *Name));
}

// --- The helmet on its stand ---------------------------------------------------------------------------

void ATorqueDecalMode::AddHelmetShape()
{
	FState& S = *St;
	if (!S.Stand) return;
	FState::FSlotShape& H = S.Shapes.FindOrAdd(TDMI::HelmetSlot);
	H.Meshes = {S.Stand};
	H.Frame = TorqueDecals::PartFrame(H.Meshes, true);
	H.Tris = TorqueDecals::PartTriangles(H.Meshes, H.Frame);
}

void ATorqueDecalMode::ShowHelmet(bool bHelmet)
{
	FState& S = *St;
	ATorqueShedBike* Bike = S.Bike.Get();
	if (bHelmet && !S.Stand && Bike)
	{
		FTransform Fit;
		UStaticMesh* Mesh = TorqueDecals::HelmetMesh(Fit);
		if (!Mesh)
		{
			Say(TEXT("There's no helmet to put decals on in this build."));
			return;
		}
		// Beside the bike, on the side the camera's on, at about chest height.
		const FVector Right = Bike->GetActorRightVector();
		const FVector Side = Right * (FVector::DotProduct(GetActorLocation() - S.BikeFocus, Right) >= 0.0 ? 1.0 : -1.0);
		const FVector Floor = Bike->GetActorLocation() + Side * 95.0;
		const FVector At = Floor + FVector(0.0, 0.0, 110.0);
		S.Stand = NewObject<UStaticMeshComponent>(this);
		S.Stand->SetStaticMesh(Mesh);
		S.Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S.Stand->SetupAttachment(RootComponent);
		S.Stand->RegisterComponent();
		S.Stand->SetWorldTransform(Fit * FTransform(Bike->GetActorRotation(), At));
		TorqueHelmetLook::ApplyPaint(S.Stand, FTorqueProfile::Mine(), nullptr);   // (the player's own paint)
		// Its middle at At, on a pole down to the floor.
		S.Stand->UpdateBounds();
		const FBox Box = S.Stand->Bounds.GetBox();
		S.Stand->SetWorldLocation(S.Stand->GetComponentLocation() + (At - Box.GetCenter()));
		if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
		{
			const double Low = At.Z - 0.5 * Box.GetSize().Z;
			const double Tall = FMath::Max(10.0, Low - Floor.Z);
			S.Pole = NewObject<UStaticMeshComponent>(this);
			S.Pole->SetStaticMesh(Cyl);
			S.Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			S.Pole->SetupAttachment(RootComponent);
			S.Pole->RegisterComponent();
			S.Pole->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(At.X, At.Y, Floor.Z + 0.5 * Tall), FVector(0.05, 0.05, Tall / 100.0)));
		}
		if (!TorqueDecals::ReadyHelmet(S.Stand))
		{
			Say(TEXT("This helmet can't take decals yet (it needs the next build)."));
			S.Stand->DestroyComponent();
			S.Stand = nullptr;
			if (S.Pole) S.Pole->DestroyComponent();
			S.Pole = nullptr;
			return;
		}
		const FString Mine = TorqueDecals::MyHelmet();
		S.Slots.AddUnique(TDMI::HelmetSlot);
		S.Saved.Add(TDMI::HelmetSlot, Mine);
		S.Draft.Add(TDMI::HelmetSlot, TorqueFit::Decals::Parse(TorqueDecals::ToStd(Mine)));
		AddHelmetShape();
	}
	if (bHelmet == S.bAtHelmet || (bHelmet && !S.Stand)) return;
	S.bAtHelmet = bHelmet;
	if (bHelmet)
	{
		S.BikeDist = S.Dist;
		S.Focus = S.Stand->Bounds.Origin;
		S.Dist = 75.0;
		Say(TEXT("Your helmet: its decals go with you onto every bike."));
	}
	else
	{
		S.Focus = S.BikeFocus;
		S.Dist = S.BikeDist;
	}
	S.Sel = INDEX_NONE;
}

// --- Drawing the drafts on the bike ----------------------------------------------------------------

void ATorqueDecalMode::Refresh(bool bForce)
{
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	ATorqueShedBike* Bike = S.Bike.Get();
	if (!Bike) return;
	// The slots' shapes, made again when the bike's been drawn again.
	UStaticMeshComponent* First = Bike->Parts.Num() > 0 ? Bike->Parts[0] : nullptr;
	if (First != S.SeenFirst || Bike->Parts.Num() != S.SeenCount)
	{
		S.SeenFirst = First;
		S.SeenCount = Bike->Parts.Num();
		S.Shapes.Reset();
		S.Shown.Reset();
		for (UStaticMeshComponent* C : Bike->Parts)
		{
			const FName Slot = C ? TorqueShedModel::SlotTag(C) : NAME_None;
			if (S.Slots.Contains(Slot) && Slot != TDMI::HelmetSlot) S.Shapes.FindOrAdd(Slot).Meshes.Add(C);
		}
		for (TPair<FName, FState::FSlotShape>& KV : S.Shapes)
		{
			KV.Value.Frame = TorqueDecals::PartFrame(KV.Value.Meshes);
			KV.Value.Tris = TorqueDecals::PartTriangles(KV.Value.Meshes, KV.Value.Frame);
		}
		AddHelmetShape();
	}
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	for (TPair<FName, FState::FSlotShape>& KV : S.Shapes)
	{
		std::vector<BD::Decal> L = S.Draft.FindRef(KV.Key);
		bool bPreview = false;
		if (S.bBrush && S.bHover && S.HoverSlot == KV.Key && !S.bDrag && (int32)L.size() < BD::MaxPerPart)
		{
			BD::Decal D = S.Brush;
			D.At = TDMI::DmOf(S.HoverAt);
			D.Normal = TDMI::DmOf(S.HoverN).Normalized();
			L.push_back(D);
			bPreview = true;
		}
		const FString Text = TorqueDecals::ToUe(BD::ToText(L));
		if (Text == S.Shown.FindRef(KV.Key)) continue;
		// (The preview under the mouse is drawn again ten times a second at most.)
		if (!bForce && (bPreview || S.bDrag) && Now - S.ShownAt < 0.1) continue;
		TorqueDecals::Apply(KV.Value.Meshes, Text, KV.Key == TDMI::HelmetSlot);
		S.Shown.Add(KV.Key, Text);
		S.ShownAt = Now;
	}
}

// --- Tick and input ---------------------------------------------------------------------------------

void ATorqueDecalMode::Tick(float DeltaSeconds)
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
	HideWalker(true);
	ReadInput(DeltaSeconds);
	if (!S.PC.IsValid()) return;   // (closed)
	Refresh(false);
	UpdateCamera();
}

void ATorqueDecalMode::Press(const FVector2D& Mouse)
{
	FState& S = *St;
	for (const FState::FHit& H : S.Hits)
		if (H.R.IsInside(Mouse))
		{
			if (H.Click) H.Click();
			return;
		}
	if (S.PanelBox.bIsValid && S.PanelBox.IsInside(Mouse)) return;
	S.bTyping = false;
	FName Slot;
	FVector At, N;
	if (!HoverAt(Mouse, Slot, At, N))
	{
		S.Sel = INDEX_NONE;
		return;
	}
	const int32 Under = DecalUnder(Slot, At);
	if (Under != INDEX_NONE && !S.bBrush)
	{
		S.SelSlot = Slot;
		S.Sel = Under;
		S.bDrag = true;
		const TorqueFit::Decals::Decal& D = S.Draft[Slot][(size_t)Under];
		// What's picked becomes what's in hand's settings too (its colours and switches).
		S.Brush.Rgb1 = D.Rgb1;
		S.Brush.Rgb2 = D.Rgb2;
		S.Brush.Flag = D.Flag;
		Say(TEXT("Picked: drag to slide it, Shift + wheel sizes, Ctrl + wheel turns, Delete takes it off."));
		return;
	}
	if (S.bBrush) PlaceBrush(Slot, At, N);
	else S.Sel = INDEX_NONE;
}

void ATorqueDecalMode::ReadInput(float Dt)
{
	FState& S = *St;
	APlayerController* PC = S.PC.Get();
	if (!PC || GFrameCounter <= S.OpenFrame) return;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	auto Just = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	FVector2D Mouse = S.LastMouse;
	float Mx = 0.0f, My = 0.0f;
	if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My);
	const FVector2D Moved = Mouse - S.LastMouse;
	S.LastMouse = Mouse;
	const bool bShift = Down(EKeys::LeftShift) || Down(EKeys::RightShift);
	const bool bCtrl = Down(EKeys::LeftControl) || Down(EKeys::RightControl);
	const bool bOverPanel = S.PanelBox.bIsValid && S.PanelBox.IsInside(Mouse);

	// Typing the lettering's words.
	if (S.bTyping)
	{
		const FString Was = S.Typed;
		ATorquePlayerController::TypeText(PC, S.Typed, TorqueFit::Decals::MaxTextChars);
		if (Just(EKeys::Enter) || Just(EKeys::Escape))
		{
			S.bTyping = false;
			if (Just(EKeys::Enter)) PickText();
			return;
		}
		if (S.Typed != Was && S.bBrush && S.Brush.K == TorqueFit::Decals::Kind::Text) PickText();
		if (Just(EKeys::LeftMouseButton)) Press(Mouse);
		return;
	}
	// Leaving.
	if (S.bConfirm && Just(EKeys::Enter))
	{
		Save();
		Close();
		return;
	}
	if (Just(EKeys::Escape) || FTorqueKeys::Just(PC, ETorqueKey::DecalMode))
	{
		if (S.bBrush && !S.bConfirm && !FTorqueKeys::Just(PC, ETorqueKey::DecalMode))
		{
			S.bBrush = false;
			Say(TEXT("Put back on the shelf."));
			return;
		}
		RequestExit();
		return;
	}
	// The mouse.
	S.bHover = !bOverPanel && HoverAt(Mouse, S.HoverSlot, S.HoverAt, S.HoverN);
	if (Just(EKeys::LeftMouseButton)) Press(Mouse);
	if (!Down(EKeys::LeftMouseButton)) S.bDrag = false;
	if (S.bDrag && S.bHover && S.HoverSlot == S.SelSlot && !Moved.IsNearlyZero())
		if (TorqueFit::Decals::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel))
		{
			D->At = TDMI::DmOf(S.HoverAt);
			D->Normal = TDMI::DmOf(S.HoverN).Normalized();
			S.bConfirm = false;
		}
	if (Just(EKeys::RightMouseButton)) S.bOrbit = true;
	if (!Down(EKeys::RightMouseButton)) S.bOrbit = false;
	if (S.bOrbit)
	{
		S.Yaw += Moved.X * 0.006;
		S.Pitch = FMath::Clamp(S.Pitch + Moved.Y * 0.005, -1.35, 0.6);
	}
	const float Wheel = (Just(EKeys::MouseScrollUp) ? 1.0f : 0.0f) - (Just(EKeys::MouseScrollDown) ? 1.0f : 0.0f);
	if (Wheel != 0.0f)
	{
		if (bOverPanel) S.Scroll = FMath::Max(0, S.Scroll - (int32)Wheel);
		else if (bShift) Resize(Wheel > 0.0f ? 1.08f : 1.0f / 1.08f);
		else if (bCtrl) Turn(Wheel * 5.0f);
		else S.Dist = FMath::Clamp(S.Dist * (Wheel > 0.0f ? 0.9 : 1.0 / 0.9), 40.0, 400.0);
	}
	// The keys.
	if (Just(EKeys::RightBracket) || Just(EKeys::Equals)) Resize(1.08f);
	if (Just(EKeys::LeftBracket) || Just(EKeys::Hyphen)) Resize(1.0f / 1.08f);
	if (Just(EKeys::Period)) Turn(-5.0f);
	if (Just(EKeys::Comma)) Turn(5.0f);
	if (Just(EKeys::Delete) || Just(EKeys::BackSpace)) Remove();
	if (Just(EKeys::M)) Toggle(TorqueFit::Decals::Pair);
	if (bCtrl && Just(EKeys::S)) Save();
	// The pad: the right stick orbits, the triggers zoom, Circle leaves.
	if (const ATorqueWalker* W = S.Walker.Get())
	{
		const FSonyPadState Pad = W->PadState();
		const uint32 PadDown = Pad.bConnected ? Pad.Buttons : 0;
		const uint32 PadHit = PadDown & ~S.PrevPad;
		S.PrevPad = PadDown;
		if ((PadHit & ESonyButton::Circle) || Just(EKeys::Gamepad_FaceButton_Right)) RequestExit();
	}
	const float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), RY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	if (FMath::Abs(RX) > 0.15f) S.Yaw += RX * 1.8 * Dt;
	if (FMath::Abs(RY) > 0.15f) S.Pitch = FMath::Clamp(S.Pitch - RY * 1.4 * Dt, -1.35, 0.6);
}

// --- Tests ------------------------------------------------------------------------------------------

FString ATorqueDecalMode::TestPlace(FName Slot, const FString& What, int32 Side)
{
	FState& S = *St;
	if (Slot == TDMI::HelmetSlot) ShowHelmet(true);
	Refresh(true);
	const FState::FSlotShape* Shape = S.Shapes.Find(Slot);
	if (!Shape || Shape->Tris.empty()) return FString::Printf(TEXT("decal: no %s to put it on"), *Slot.ToString());
	if (What.StartsWith(TEXT("text:")))
	{
		TArray<FString> P;
		What.ParseIntoArray(P, TEXT(":"), false);
		S.FaceId = P.Num() > 1 ? P[1] : FString(TEXT("fraktur"));
		S.Typed = P.Num() > 2 ? P[2] : FString(TEXT("Torque"));
		PickText();
	}
	else
		PickLibrary(What);
	if (!S.bBrush) return FString::Printf(TEXT("decal: nothing called %s"), *What);
	// The surface at the slot's middle, seen from that side.
	TDMI::DmV3 Hit, N;
	if (!TorqueFit::Decals::RayHit(Shape->Tris, TDMI::DmV3(0.0, 200.0 * (double)Side, 0.0), TDMI::DmV3(0.0, -(double)Side, 0.0), Hit, N))
		return TEXT("decal: no surface there");
	PlaceBrush(Slot, TDMI::DmUe(Hit), TDMI::DmUe(N));
	S.bBrush = false;
	Refresh(true);
	return FString::Printf(TEXT("decal: %s on the %s"), *What, *Slot.ToString());
}

FString ATorqueDecalMode::TestSave()
{
	St->Status.Reset();
	Save();
	return St->Status.IsEmpty() ? FString(TEXT("decal: saved")) : TEXT("decal: ") + St->Status;
}

void ATorqueDecalMode::TestExit()
{
	St->bConfirm = true;
	Close();
}

// --- The panel ----------------------------------------------------------------------------------------

void ATorqueDecalMode::DrawPanel(AHUD* Hud, UCanvas* Canvas)
{
	namespace UI = TorqueUi;
	namespace BD = TorqueFit::Decals;
	FState& S = *St;
	if (!Hud || !Canvas || !S.PC.IsValid()) return;
	const float Sc = UI::Scale(Canvas);
	S.Hits.Reset();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const FVector2D Mouse = S.LastMouse;
	auto Hot = [&Mouse](float X, float Y, float W, float H) { return Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H; };
	auto Button = [&](const FString& Label, float X, float Y, float W, float H, bool bOn, TFunction<void()> Click) {
		const bool bHot = Hot(X, Y, W, H);
		UI::Rect(Canvas, X, Y, W, H, bOn ? FLinearColor(UI::Accent.R, UI::Accent.G, UI::Accent.B, 0.85f) : FLinearColor(1, 1, 1, bHot ? 0.16f : 0.08f));
		UI::TextFit(Canvas, Label, X + 0.5f * W, Y + 0.5f * (H - 17.0f * Sc * 1.2f), 17.0f * Sc, W - 8.0f * Sc, bOn ? FLinearColor(0.05f, 0.04f, 0.03f, 1.0f) : UI::Ink,
			bOn, UI::EAlign::Centre);
		FState::FHit Hh;
		Hh.R = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
		Hh.Click = MoveTemp(Click);
		S.Hits.Add(MoveTemp(Hh));
	};
	auto Tile = [&](UTexture* Tex, float X, float Y, float W, float H) {
		if (!Tex || !Tex->GetResource()) return;
		FCanvasTileItem T(FVector2D(X, Y), Tex->GetResource(), FVector2D(W, H), FLinearColor::White);
		T.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(T);
	};

	// --- The picked decal's outline on the bike.
	if (const BD::Decal* D = PickedDecal(S.Draft, S.SelSlot, S.Sel))
		if (const FState::FSlotShape* Shape = S.Shapes.Find(S.SelSlot))
		{
			const BD::Frame F = BD::FrameOf(*D);
			FVector2D P[4];
			bool bAll = true;
			const double Cx[4] = {-0.5, 0.5, 0.5, -0.5}, Cy[4] = {-0.5, -0.5, 0.5, 0.5};
			for (int32 I = 0; I < 4; ++I)
			{
				const TDMI::DmV3 Q = F.O + F.R * (Cx[I] * D->W) + F.U * (Cy[I] * D->H) + F.N * 0.5;
				bAll = bAll && S.PC->ProjectWorldLocationToScreen(Shape->Frame.TransformPosition(TDMI::DmUe(Q)), P[I], false);
			}
			if (bAll)
				for (int32 I = 0; I < 4; ++I) Canvas->K2_DrawLine(P[I], P[(I + 1) % 4], 2.0f * Sc, UI::Accent);
		}

	// --- The panel down the left.
	const float X0 = 18.0f * Sc, Y0 = 18.0f * Sc, PW = 470.0f * Sc, PH = Canvas->ClipY - 36.0f * Sc;
	S.PanelBox = FBox2D(FVector2D(X0, Y0), FVector2D(X0 + PW, Y0 + PH));
	UI::Rect(Canvas, X0, Y0, PW, PH, UI::Panel);
	UI::Rect(Canvas, X0, Y0, 3.0f * Sc, PH, UI::Accent);
	const float M = 18.0f * Sc, IX = X0 + M, IW = PW - 2.0f * M;
	float Y = Y0 + 14.0f * Sc;
	const FString BikeName = B && B->Exists(S.Bay) ? B->Bike(S.Bay).Name : FString();
	UI::Text(Canvas, TEXT("DECALS"), IX, Y, 24.0f * Sc, UI::Accent, true);
	UI::TextFit(Canvas, BikeName, IX + 120.0f * Sc, Y + 4.0f * Sc, 18.0f * Sc, IW - 120.0f * Sc, UI::Dim);
	Y += 38.0f * Sc;
	// On the bike or on the helmet.
	{
		const float HW = 0.5f * (IW - 6.0f * Sc), HH = 30.0f * Sc;
		Button(TEXT("Bike"), IX, Y, HW, HH, !S.bAtHelmet, [this]() { ShowHelmet(false); });
		Button(TEXT("Helmet"), IX + HW + 6.0f * Sc, Y, HW, HH, S.bAtHelmet, [this]() { ShowHelmet(true); });
		Y += HH + 10.0f * Sc;
	}
	// How many on each part.
	{
		FString Counts;
		for (const FName& Slot : S.Slots)
			Counts += FString::Printf(TEXT("%s%s %d/8"), Counts.IsEmpty() ? TEXT("") : TEXT("   "), *TDMI::SlotName(Slot), (int32)S.Draft.FindRef(Slot).size());
		Y += UI::TextWrap(Canvas, Counts, IX, Y, 15.0f * Sc, IW, UI::Dim) + 8.0f * Sc;
	}
	// The shelves.
	{
		const float TW = (IW - 3.0f * 6.0f * Sc) / 4.0f, TH = 34.0f * Sc;
		for (int32 T = 0; T < 4; ++T)
			Button(TDMI::TabNames[T], IX + T * (TW + 6.0f * Sc), Y, TW, TH, S.Tab == T, [this, T]() {
				St->Tab = T;
				St->Scroll = 0;
			});
		Y += TH + 12.0f * Sc;
	}
	const float ShelfH = FMath::Max(200.0f * Sc, PH * 0.42f);
	const float ShelfY = Y;
	if (S.Tab == 0)
	{
		// The words, then the faces.
		const float FH = 44.0f * Sc;
		UI::Rect(Canvas, IX, Y, IW, FH, UI::PanelLight);
		UI::Frame(Canvas, IX, Y, IW, FH, S.bTyping ? UI::Accent : UI::Line, FMath::Max(1.0f, 1.5f * Sc));
		float Typed = 0.0f;
		if (S.Typed.IsEmpty()) UI::Text(Canvas, TEXT("Click and type a name or number..."), IX + 10.0f * Sc, Y + 11.0f * Sc, 19.0f * Sc, UI::Faint, false, UI::EAlign::Left, false);
		else Typed = UI::Text(Canvas, S.Typed, IX + 10.0f * Sc, Y + 11.0f * Sc, 19.0f * Sc, UI::Ink);
		if (S.bTyping && FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.6) UI::Rect(Canvas, IX + 12.0f * Sc + Typed, Y + 8.0f * Sc, 2.0f * Sc, FH - 16.0f * Sc, UI::Accent);
		{
			FState::FHit Hh;
			Hh.R = FBox2D(FVector2D(IX, Y), FVector2D(IX + IW, Y + FH));
			Hh.Click = [this]() { St->bTyping = true; };
			S.Hits.Add(MoveTemp(Hh));
		}
		Y += FH + 8.0f * Sc;
		const TArray<TorqueDecals::FFace>& Faces = TorqueDecals::Faces();
		const float RH = 30.0f * Sc;
		const int32 Rows = FMath::Max(1, (int32)((ShelfY + ShelfH - Y) / RH));
		S.Scroll = FMath::Clamp(S.Scroll, 0, FMath::Max(0, Faces.Num() - Rows));
		for (int32 I = S.Scroll; I < Faces.Num() && I < S.Scroll + Rows; ++I)
		{
			const TorqueDecals::FFace& F = Faces[I];
			const FString Id = F.Id;
			Button(FString::Printf(TEXT("%s  (%s)"), *F.Name, *F.Style), IX, Y, IW, RH - 4.0f * Sc, S.FaceId == Id, [this, Id]() {
				St->FaceId = Id;
				PickText();
			});
			Y += RH;
		}
	}
	else if (S.Tab == 1 || S.Tab == 2)
	{
		// The pictures on that shelf, four across.
		TArray<const TorqueDecals::FLibDecal*> Shelf;
		for (const TorqueDecals::FLibDecal& L : TorqueDecals::Library())
			if (L.Shelf == TDMI::TabShelf[S.Tab]) Shelf.Add(&L);
		const int32 Cols = 4;
		const float Cell = (IW - (Cols - 1) * 6.0f * Sc) / Cols;
		const int32 Rows = FMath::Max(1, (int32)(ShelfH / (Cell + 6.0f * Sc)));
		const int32 RowsAll = (Shelf.Num() + Cols - 1) / Cols;
		S.Scroll = FMath::Clamp(S.Scroll, 0, FMath::Max(0, RowsAll - Rows));
		for (int32 I = S.Scroll * Cols; I < Shelf.Num() && I < (S.Scroll + Rows) * Cols; ++I)
		{
			const int32 R = I / Cols - S.Scroll, C = I % Cols;
			const float CX = IX + C * (Cell + 6.0f * Sc), CY = ShelfY + R * (Cell + 6.0f * Sc);
			const bool bOn = S.bBrush && S.Brush.K == BD::Kind::Library && TorqueDecals::ToUe(S.Brush.Id) == Shelf[I]->Id;
			UI::Rect(Canvas, CX, CY, Cell, Cell, bOn ? UI::Accent : FLinearColor(0.93f, 0.91f, 0.86f, Hot(CX, CY, Cell, Cell) ? 1.0f : 0.85f));
			if (UTexture* Th = TorqueDecals::Thumb(Shelf[I]->Id))
			{
				const float Asp = (float)Th->GetSurfaceWidth() / FMath::Max(1.0f, (float)Th->GetSurfaceHeight());
				const float TW = Asp >= 1.0f ? Cell - 10.0f * Sc : (Cell - 10.0f * Sc) * Asp, TH = Asp >= 1.0f ? (Cell - 10.0f * Sc) / Asp : Cell - 10.0f * Sc;
				Tile(Th, CX + 0.5f * (Cell - TW), CY + 0.5f * (Cell - TH), TW, TH);
			}
			const FString Id = Shelf[I]->Id;
			FState::FHit Hh;
			Hh.R = FBox2D(FVector2D(CX, CY), FVector2D(CX + Cell, CY + Cell));
			Hh.Click = [this, Id]() { PickLibrary(Id); };
			S.Hits.Add(MoveTemp(Hh));
		}
		for (int32 I = 0; I < Shelf.Num(); ++I)
		{
			const int32 R = I / Cols - S.Scroll, C = I % Cols;
			const float CX = IX + C * (Cell + 6.0f * Sc), CY = ShelfY + R * (Cell + 6.0f * Sc);
			if (R >= 0 && R < Rows && Hot(CX, CY, Cell, Cell))
				UI::TextFit(Canvas, Shelf[I]->Name, IX, ShelfY + ShelfH - 18.0f * Sc, 16.0f * Sc, IW, UI::Ink);
		}
	}
	else
	{
		// Your own pictures (Saved/Decals).
		const TArray<FString> Own = TorqueDecals::OwnPictures();
		if (Own.Num() == 0)
			UI::TextWrap(Canvas, FString::Printf(TEXT("Put PNG pictures in %s and they show here. They're shrunk to 512 pixels across and sent to friends you ride with."),
				*TorqueDecals::OwnFolder()), IX, Y, 16.0f * Sc, IW, UI::Dim);
		const int32 Cols = 4;
		const float Cell = (IW - (Cols - 1) * 6.0f * Sc) / Cols;
		for (int32 I = 0; I < Own.Num() && I < 16; ++I)
		{
			const float CX = IX + (I % Cols) * (Cell + 6.0f * Sc), CY = ShelfY + (I / Cols) * (Cell + 6.0f * Sc);
			UI::Rect(Canvas, CX, CY, Cell, Cell, FLinearColor(0.5f, 0.5f, 0.5f, Hot(CX, CY, Cell, Cell) ? 0.6f : 0.35f));
			Tile(TorqueDecals::OwnPicture(Own[I]), CX + 5.0f * Sc, CY + 5.0f * Sc, Cell - 10.0f * Sc, Cell - 10.0f * Sc);
			const FString Name = Own[I];
			FState::FHit Hh;
			Hh.R = FBox2D(FVector2D(CX, CY), FVector2D(CX + Cell, CY + Cell));
			Hh.Click = [this, Name]() { PickOwn(Name); };
			S.Hits.Add(MoveTemp(Hh));
		}
	}
	Y = ShelfY + ShelfH + 10.0f * Sc;
	UI::Rect(Canvas, IX, Y, IW, FMath::Max(1.0f, Sc), UI::Line);
	Y += 10.0f * Sc;

	// --- Colours and switches (for what's picked, and what's in hand).
	const BD::Decal* Picked = PickedDecal(S.Draft, S.SelSlot, S.Sel);
	const BD::Decal& Cur = Picked ? *Picked : S.Brush;
	for (int32 Row = 0; Row < 2; ++Row)
	{
		UI::Text(Canvas, Row == 0 ? TEXT("Fill") : TEXT("Ink"), IX, Y + 4.0f * Sc, 16.0f * Sc, UI::Dim);
		const float SW = (IW - 50.0f * Sc) / TDMI::PaletteN;
		for (int32 I = 0; I < TDMI::PaletteN; ++I)
		{
			const float SX = IX + 50.0f * Sc + I * SW;
			const uint32 Rgb = TDMI::Palette[I];
			UI::Rect(Canvas, SX + 1.0f * Sc, Y, SW - 2.0f * Sc, 24.0f * Sc, TDMI::Swatch(Rgb));
			if ((Row == 0 ? Cur.Rgb1 : Cur.Rgb2) == Rgb) UI::Frame(Canvas, SX, Y - 1.0f * Sc, SW, 26.0f * Sc, UI::Accent, 2.0f * Sc);
			const bool bInk = Row == 1;
			FState::FHit Hh;
			Hh.R = FBox2D(FVector2D(SX, Y), FVector2D(SX + SW, Y + 24.0f * Sc));
			Hh.Click = [this, bInk, Rgb]() { SetColour(bInk, Rgb); };
			S.Hits.Add(MoveTemp(Hh));
		}
		Y += 32.0f * Sc;
	}
	{
		struct FSw
		{
			const TCHAR* Label;
			uint32 Flag;
		};
		const FSw Sws[] = {{TEXT("Filled"), BD::Filled}, {TEXT("Under lacquer"), BD::UnderLacquer}, {TEXT("Pair (both sides)"), BD::Pair},
			{TEXT("Outline"), BD::Outline}, {TEXT("Shadow"), BD::Shadow}};
		const float BW = (IW - 2.0f * 6.0f * Sc) / 3.0f, BH = 30.0f * Sc;
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Sws); ++I)
		{
			const uint32 Flag = Sws[I].Flag;
			Button(Sws[I].Label, IX + (I % 3) * (BW + 6.0f * Sc), Y + (I / 3) * (BH + 6.0f * Sc), BW, BH, Cur.Has(Flag), [this, Flag]() { Toggle(Flag); });
		}
		Button(FString::Printf(TEXT("Arch %d%%"), FMath::RoundToInt(Cur.Arch * 100.0)), IX + 2.0f * (BW + 6.0f * Sc), Y + BH + 6.0f * Sc, BW * 0.5f - 3.0f * Sc, BH, false,
			[this]() { Arch(0.1f); });
		Button(TEXT("Flat"), IX + 2.0f * (BW + 6.0f * Sc) + BW * 0.5f + 3.0f * Sc, Y + BH + 6.0f * Sc, BW * 0.5f - 3.0f * Sc, BH, false, [this]() { Arch(-1.0f); });
		Y += 2.0f * (BH + 6.0f * Sc) + 6.0f * Sc;
	}
	// --- What's picked.
	if (Picked)
	{
		UI::TextFit(Canvas, FString::Printf(TEXT("Picked on the %s: %.0f x %.0f cm, turned %.0f"), *TDMI::SlotName(S.SelSlot).ToLower(), Picked->W, Picked->H, Picked->Turn),
			IX, Y, 16.0f * Sc, IW, UI::Ink);
		Y += 24.0f * Sc;
		const float BW = (IW - 4.0f * 6.0f * Sc) / 5.0f, BH = 30.0f * Sc;
		Button(TEXT("Smaller"), IX, Y, BW, BH, false, [this]() { Resize(1.0f / 1.08f); });
		Button(TEXT("Bigger"), IX + (BW + 6.0f * Sc), Y, BW, BH, false, [this]() { Resize(1.08f); });
		Button(TEXT("Turn left"), IX + 2.0f * (BW + 6.0f * Sc), Y, BW, BH, false, [this]() { Turn(5.0f); });
		Button(TEXT("Turn right"), IX + 3.0f * (BW + 6.0f * Sc), Y, BW, BH, false, [this]() { Turn(-5.0f); });
		Button(TEXT("Take off"), IX + 4.0f * (BW + 6.0f * Sc), Y, BW, BH, false, [this]() { Remove(); });
		Y += BH + 8.0f * Sc;
	}
	// --- Save and leave, the status, the keys.
	const float FY = Y0 + PH - 120.0f * Sc;
	if (!S.Status.IsEmpty()) UI::TextWrap(Canvas, S.Status, IX, FY - 46.0f * Sc, 16.0f * Sc, IW, S.bConfirm ? UI::Warn : UI::Ink);
	const float HalfW = 0.5f * (IW - 6.0f * Sc);
	Button(IsDirty() ? TEXT("Save to this bike") : TEXT("Saved"), IX, FY, HalfW, 38.0f * Sc, IsDirty(), [this]() { Save(); });
	Button(TEXT("Leave"), IX + HalfW + 6.0f * Sc, FY, HalfW, 38.0f * Sc, false, [this]() { RequestExit(); });
	float HX = IX;
	const float HY = FY + 50.0f * Sc;
	HX += UI::KeyHint(Canvas, TEXT("RMB"), TEXT("orbit"), HX, HY, Sc) + 14.0f * Sc;
	HX += UI::KeyHint(Canvas, TEXT("Wheel"), TEXT("zoom"), HX, HY, Sc) + 14.0f * Sc;
	UI::KeyHint(Canvas, TEXT("Esc"), S.bBrush ? TEXT("put back") : TEXT("leave"), HX, HY, Sc);
	HX = IX;
	HX += UI::KeyHint(Canvas, TEXT("Shift+Wheel"), TEXT("size"), HX, HY + 32.0f * Sc, Sc) + 14.0f * Sc;
	UI::KeyHint(Canvas, TEXT("Ctrl+Wheel"), TEXT("turn"), HX, HY + 32.0f * Sc, Sc);
}
