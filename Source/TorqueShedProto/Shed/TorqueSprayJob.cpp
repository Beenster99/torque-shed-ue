#include "Shed/TorqueSprayJob.h"

#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedPick.h"
#include "Shed/TorquePaintMix.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RHI.h"
#include "TextureResource.h"

namespace TorqueSpray
{
ELayer LayerOf(const FTorqueShedRequest& R)
{
	if (R.Kind == ETorqueShedAct::Prime) return ELayer::Primer;
	if (R.Kind == ETorqueShedAct::Coat) return UTorqueBuildSubsystem::IsLacquer(R.Finish) ? ELayer::Clear : ELayer::Paint;
	return ELayer::Paint;
}

namespace
{
// The old paint, flatted back for the new coat to key to: dull and matte, so a missed bit shows
// against the wet new coat. Masking tape on it stays in sight until it's sprayed over.
FLinearColor Flatted(const FTorqueItem& It)
{
	const FLinearColor Old = UTorqueBuildSubsystem::ShownColour(It);
	FLinearColor Hsv = Old.LinearRGBToHSV();
	Hsv.G *= 0.8f;
	Hsv.B = FMath::Clamp(Hsv.B * 0.8f, 0.0f, 1.0f);
	FLinearColor C = Hsv.HSVToLinearRGB();
	const bool bTaped = It.Tape != 0 && !It.bPrimerTop;
	C.A = bTaped ? 0.0f : 0.85f;
	return C;
}

// Clear lacquer going on over It: a wet gloss over the colour, which stays as it is (stripes,
// two-tones and effects too). Missed bits of a plain colour look a little flat without it.
FLook ClearLook(const FTorqueItem& It, float Rough)
{
	FLook L;
	const FLinearColor Old = UTorqueBuildSubsystem::ShownColour(It);
	L.Colour = Old;
	L.Rough = Rough;
	L.Metal = 0.0f;
	L.bClear = true;
	const bool bPlain = It.MixCode.IsEmpty() && It.Mask == 0 && It.Tape == 0;
	const FLinearColor C = Old * 0.88f;
	L.Under = FLinearColor(C.R, C.G, C.B, bPlain ? 0.35f : 0.0f);
	L.UnderRough = 0.9f;
	L.bKeep = !bPlain;
	return L;
}
}

FLook LookFor(const UTorqueBuildSubsystem& B, int32 Peer, const FTorqueShedRequest& R, const FTorqueItem& It)
{
	FLook L;
	L.Under = Flatted(It);
	L.UnderRough = 0.95f;
	const ELayer Layer = LayerOf(R);
	// Bare metal, or a part as it came, shows as it is round the new coat: only old paint is shown
	// flatted back, and only lightly, so the coat going on is what changes the part.
	if (!UTorqueBuildSubsystem::Repainted(It)) L.Under.A = 0.0f;
	else L.Under.A *= 0.5f;
	if (Layer == ELayer::Primer)
	{
		// Primer goes on over the part as it is: the grey builds up where you spray it.
		L.Under.A = 0.0f;
		L.Colour = UTorqueBuildSubsystem::PrimerColour();
		L.Rough = 0.75f;
		L.Metal = 0.0f;
		return L;
	}
	if (Layer == ELayer::Clear) return ClearLook(It, 0.04f);
	// On primer, the primer is what shows through (the part's drawn in it already).
	if (It.bPrimerTop) L.Under.A = 0.0f;
	if (R.Kind == ETorqueShedAct::Paint && R.Finish == UTorqueBuildSubsystem::CupFinish())
	{
		// Paint mixed in the mixing bay, from the gun's cup: what this coat leaves.
		if (const FTorquePaintCup* Cup = B.PaintBay().Cups.Find(Peer))
		{
			if (Cup->Spec.IsLacquer()) return ClearLook(It, FMath::Max(Cup->Spec.Rough, 0.04f));
			L.Colour = Cup->Spec.Colour;
			L.Rough = Cup->Spec.Rough;
			L.Metal = Cup->Spec.Metal;
		}
		return L;
	}
	// The colour as its finish shows it (bike_model.gd _apply_finish), worked out the way the part
	// will be drawn once it's on.
	FTorqueItem Wet = It;
	Wet.bPrimerTop = false;
	if (R.Kind == ETorqueShedAct::Coat)
	{
		const bool bSilver = R.Finish == FName(TEXT("enamel_silver"));
		Wet.Colour = UTorqueBuildSubsystem::Paints()[FMath::Clamp(bSilver ? 8 : 6, 0, UTorqueBuildSubsystem::Paints().Num() - 1)];
		Wet.Finish = bSilver ? FName(TEXT("metallic")) : FName(TEXT("gloss"));
	}
	else
	{
		Wet.Colour = UTorqueBuildSubsystem::Paints()[FMath::Clamp(R.Colour, 0, UTorqueBuildSubsystem::Paints().Num() - 1)];
		Wet.Finish = UTorqueBuildSubsystem::Finishes().Contains(R.Finish) ? R.Finish : FName(TEXT("gloss"));
	}
	Wet.Coat = NAME_None;
	FLinearColor Shown;
	float Metal = 0.05f, Rough = 0.12f;
	if (UTorqueBuildSubsystem::FinishLook(Wet, Shown, Metal, Rough))
	{
		L.Colour = Shown;
		L.Metal = Metal;
		L.Rough = Rough;
	}
	else L.Colour = Wet.Colour;
	return L;
}

FLinearColor MistColour(const UTorqueBuildSubsystem& B, int32 Peer, const FTorqueShedRequest& R)
{
	switch (R.Kind)
	{
	case ETorqueShedAct::Prime:
		return UTorqueBuildSubsystem::PrimerColour();
	case ETorqueShedAct::Coat:
		if (UTorqueBuildSubsystem::IsLacquer(R.Finish)) return FLinearColor(0.9f, 0.9f, 0.85f);
		return UTorqueBuildSubsystem::Paints()[FMath::Clamp(R.Finish == FName(TEXT("enamel_silver")) ? 8 : 6, 0, UTorqueBuildSubsystem::Paints().Num() - 1)];
	case ETorqueShedAct::Paint:
		if (R.Finish == UTorqueBuildSubsystem::CupFinish())
		{
			const FTorquePaintCup* Cup = B.PaintBay().Cups.Find(Peer);
			return Cup && !Cup->Spec.IsLacquer() ? Cup->Spec.Colour : FLinearColor(0.85f, 0.85f, 0.85f);
		}
		return UTorqueBuildSubsystem::Paints()[FMath::Clamp(R.Colour, 0, UTorqueBuildSubsystem::Paints().Num() - 1)];
	default:
		return FLinearColor(0.8f, 0.8f, 0.8f);
	}
}

void MeshesOf(UWorld* World, int32 Item, int32 Bay, FName Slot, TArray<UStaticMeshComponent*>& Out)
{
	Out.Reset();
	if (!World || Item == 0) return;
	TArray<UStaticMeshComponent*> All;
	if (Bay != INDEX_NONE && !Slot.IsNone())
	{
		for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
			if (It->Bay == Bay)
				for (UStaticMeshComponent* C : It->Parts)
					if (C && C->IsVisible() && TorqueShedModel::SlotTag(C) == Slot) All.Add(C);
	}
	else
	{
		TArray<const UStaticMeshComponent*> Meshes;
		for (TActorIterator<ATorqueShedItems> It(World); It; ++It) It->MeshesOf(Item, Meshes);
		for (const UStaticMeshComponent* C : Meshes)
			if (C && C->IsVisible()) All.Add(const_cast<UStaticMeshComponent*>(C));
	}
	// Only the painted surfaces take the coat (not the rubber, chrome or cables), unless nothing on
	// the part is painted, when the lot does.
	for (UStaticMeshComponent* C : All)
		if (!C->ComponentHasTag(FName(TorqueShedCatalogue::NoPaintTag))) Out.Add(C);
	if (Out.Num() == 0) Out = All;
}

const TCHAR* ZoneWord(EZone Z)
{
	switch (Z)
	{
	case EZone::Good: return TEXT("GOOD");
	case EZone::Close: return TEXT("TOO CLOSE: it'll run");
	case EZone::Far: return TEXT("TOO FAR: it'll go on dry");
	default: return TEXT("OUT OF REACH");
	}
}
} // namespace TorqueSpray

namespace TorqueSprayImpl
{
using namespace TorqueSpray;

// The ways out from a point that are tried to see if a spray could get to it (all 26 round a cube).
const TArray<FVector>& Ways()
{
	static TArray<FVector> W;
	if (W.Num() == 0)
		for (int32 X = -1; X <= 1; ++X)
			for (int32 Y = -1; Y <= 1; ++Y)
				for (int32 Z = -1; Z <= 1; ++Z)
					if (X || Y || Z) W.Add(FVector(X, Y, Z).GetSafeNormal());
	return W;
}

// Points on a mesh's surface about Spacing apart (model cm, from its own cm by ToModel), each with
// the way its face points (either way: which is out is worked out later). Its own triangles if it
// has a shape, else its box's faces (a grey-box block).
void Sample(const UStaticMeshComponent* C, const FTransform& ToModel, double Spacing, TArray<FVector>& OutP, TArray<FVector>* OutN)
{
	const TArray<FVector>* Pos = nullptr;
	const TArray<int32>* Tris = nullptr;
	const double Area1 = Spacing * Spacing;
	// A low-discrepancy spread over each triangle (the R2 sequence), so the points don't clump.
	constexpr double G = 1.32471795724474602596;
	const double A1 = 1.0 / G, A2 = 1.0 / (G * G);
	int64 K = 0;
	if (TorqueShedPick::Triangles(C, Pos, Tris))
	{
		double Carry = 0.0;
		for (int32 T = 0; T + 2 < Tris->Num(); T += 3)
		{
			const int32 Ia = (*Tris)[T], Ib = (*Tris)[T + 1], Ic = (*Tris)[T + 2];
			if (!Pos->IsValidIndex(Ia) || !Pos->IsValidIndex(Ib) || !Pos->IsValidIndex(Ic)) continue;
			const FVector A = ToModel.TransformPosition((*Pos)[Ia]);
			const FVector B = ToModel.TransformPosition((*Pos)[Ib]);
			const FVector Cc = ToModel.TransformPosition((*Pos)[Ic]);
			const FVector Cross = FVector::CrossProduct(B - A, Cc - A);
			const double Area = Cross.Size() * 0.5;
			if (Area < 1e-6) continue;
			const FVector N = Cross / (Area * 2.0);
			Carry += Area / Area1;
			while (Carry >= 1.0)
			{
				Carry -= 1.0;
				double U = FMath::Frac(0.5 + A1 * K), V = FMath::Frac(0.5 + A2 * K);
				++K;
				if (U + V > 1.0) { U = 1.0 - U; V = 1.0 - V; }
				OutP.Add(A + (B - A) * U + (Cc - A) * V);
				if (OutN) OutN->Add(N);
			}
		}
		return;
	}
	const UStaticMesh* Mesh = C->GetStaticMesh();
	if (!Mesh) return;
	const FBox Box = Mesh->GetBoundingBox();
	if (!Box.IsValid) return;
	// The box's six faces, each on a grid.
	for (int32 Axis = 0; Axis < 3; ++Axis)
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Ua = (Axis + 1) % 3, Va = (Axis + 2) % 3;
			FVector Lo = Box.Min, Hi = Box.Max;
			const FVector Size = Box.GetSize();
			// The face's spacing in the mesh's own cm, so it comes out about Spacing in the model's.
			const FVector Scale = ToModel.GetScale3D().GetAbs();
			const double Su = FMath::Max(Spacing / FMath::Max(Scale[Ua], 1e-3), 0.01), Sv = FMath::Max(Spacing / FMath::Max(Scale[Va], 1e-3), 0.01);
			const int32 Nu = FMath::Clamp(FMath::CeilToInt(Size[Ua] / Su), 1, 200), Nv = FMath::Clamp(FMath::CeilToInt(Size[Va] / Sv), 1, 200);
			FVector N = FVector::ZeroVector;
			N[Axis] = Side ? 1.0 : -1.0;
			const FVector Nm = ToModel.TransformVector(N).GetSafeNormal();
			for (int32 I = 0; I < Nu; ++I)
				for (int32 J = 0; J < Nv; ++J)
				{
					FVector P;
					P[Axis] = Side ? Hi[Axis] : Lo[Axis];
					P[Ua] = Lo[Ua] + Size[Ua] * (I + 0.5) / Nu;
					P[Va] = Lo[Va] + Size[Va] * (J + 0.5) / Nv;
					OutP.Add(ToModel.TransformPosition(P));
					if (OutN) OutN->Add(Nm);
				}
		}
}

// What a part's meshes hang off (the item's holder, or the bike), whose frame is the job's.
FTransform FrameOf(const TArray<UStaticMeshComponent*>& Meshes)
{
	for (const UStaticMeshComponent* C : Meshes)
		if (C)
		{
			const USceneComponent* Parent = C->GetAttachParent();
			FTransform T = Parent ? Parent->GetComponentTransform() : C->GetComponentTransform();
			T.SetScale3D(FVector::OneVector);
			return T;
		}
	return FTransform::Identity;
}

UTexture2D* MakeAtlas(int32 W, int32 H)
{
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!T) return nullptr;
	T->SRGB = false;
	T->Filter = TF_Bilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->NeverStream = true;
	T->UpdateResource();
	return T;
}

// Rows Row0 .. Row0 + Rows of an atlas up to the GPU (a copy goes, freed once it's there).
void Send(UTexture2D* T, const TArray<uint8>& Bytes, int32 W, int32 Row0, int32 Rows)
{
	if (!T || Rows <= 0) return;
	const SIZE_T Pitch = (SIZE_T)W * 4;
	uint8* Copy = (uint8*)FMemory::Malloc(Pitch * Rows);
	FMemory::Memcpy(Copy, Bytes.GetData() + Pitch * Row0, Pitch * Rows);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, Row0, 0, 0, W, Rows);
	T->UpdateTextureRegions(0, 1, Region, (uint32)Pitch, 4, Copy,
		[](uint8* Src, const FUpdateTextureRegion2D* R)
		{
			FMemory::Free(Src);
			delete R;
		});
}

// A matrix's rows for the material: model = (R0 . p, R1 . p, R2 . p) + Off, p in the mesh's own cm.
void Rows(const FTransform& ToModel, FLinearColor& R0, FLinearColor& R1, FLinearColor& R2, FLinearColor& Off)
{
	const FMatrix M = ToModel.ToMatrixWithScale();   // row vectors: p' = p * M
	R0 = FLinearColor((float)M.M[0][0], (float)M.M[1][0], (float)M.M[2][0], 0.0f);
	R1 = FLinearColor((float)M.M[0][1], (float)M.M[1][1], (float)M.M[2][1], 0.0f);
	R2 = FLinearColor((float)M.M[0][2], (float)M.M[1][2], (float)M.M[2][2], 0.0f);
	Off = FLinearColor((float)M.M[3][0], (float)M.M[3][1], (float)M.M[3][2], 0.0f);
}
} // namespace TorqueSprayImpl

namespace TSJ = TorqueSprayImpl;
using namespace TorqueSpray;

FTorqueSprayJob::FTorqueSprayJob() = default;

FTorqueSprayJob::~FTorqueSprayJob()
{
	Detach();
}

void FTorqueSprayJob::Detach()
{
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : Shown)
		if (UStaticMeshComponent* C = W.Get())
			for (const TStrongObjectPtr<UMaterialInstanceDynamic>& M : Mids)
				if (C->GetOverlayMaterial() == M.Get()) C->SetOverlayMaterial(nullptr);
}

UMaterialInterface* FTorqueSprayJob::CoatMaterial()
{
	// M_SprayCoat comes from the shed's level script (Tools/ue_build_spray.py, run by ue_build_shed.py).
	static UMaterialInterface* Mat = nullptr;
	static bool bLooked = false;
	if (!bLooked)
	{
		bLooked = true;
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_SprayCoat.M_SprayCoat"));
		if (Mat) Mat->AddToRoot();
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque spray: no M_SprayCoat (run Tools/ue_build_shed.py): the coat won't show going on"));
		}
	}
	return Mat;
}

bool FTorqueSprayJob::Setup(UWorld* World, int32 InItem, const FString& InKey, const FTorqueShedRequest& R, int32 InBay, FName InSlot,
	const FLook& InLook, double InRunLevel, const TArray<const AActor*>& Ignore)
{
	Item = InItem;
	Key = InKey;
	Request = R;
	Bay = InBay;
	Slot = InSlot;
	Look = InLook;
	RunLevel = InRunLevel;
	Layer = LayerOf(R);
	TArray<UStaticMeshComponent*> Parts;
	MeshesOf(World, Item, Bay, Slot, Parts);
	if (Parts.Num() == 0) return false;
	Model = TSJ::FrameOf(Parts);
	// The surface, sampled coarser for a big part so the points stay few enough.
	double Spacing = Cell;
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		Points.Reset();
		Normals.Reset();
		for (const UStaticMeshComponent* C : Parts)
			TSJ::Sample(C, C->GetComponentTransform().GetRelativeTransform(Model), Spacing, Points, &Normals);
		if (Points.Num() <= MaxPoints) break;
		Spacing *= 1.5;
	}
	if (Points.Num() == 0) return false;
	// The mask round it: a big part (a whole frame) gets bigger cubes, so it still fits.
	FBox Box(ForceInit);
	for (const FVector& P : Points) Box += P;
	Vx = FMath::Max(Voxel, Box.GetSize().GetMax() / (double)(MaxDim - 6));
	Box = Box.ExpandBy(Vx * 2.0);
	Origin = Box.Min;
	const FVector Size = Box.GetSize();
	Dims = FIntVector(FMath::Clamp(FMath::CeilToInt(Size.X / Vx), 2, MaxDim), FMath::Clamp(FMath::CeilToInt(Size.Y / Vx), 2, MaxDim),
		FMath::Clamp(FMath::CeilToInt(Size.Z / Vx), 2, MaxDim));
	const int32 Cubes = Dims.X * Dims.Y * Dims.Z;
	Coats.SetNumZeroed(Cubes * 6);
	DryAmt.SetNumZeroed(Cubes);
	Landed.SetNumZeroed(Cubes);
	// The slices side by side in a grid.
	Grid = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt((double)Dims.Z)));
	AtlasSize = FIntPoint(Grid * Dims.X, FMath::DivideAndRoundUp(Dims.Z, Grid) * Dims.Y);
	BytesA.SetNumZeroed(AtlasSize.X * AtlasSize.Y * 4);
	BytesB.SetNumZeroed(AtlasSize.X * AtlasSize.Y * 4);
	TexA.Reset(TSJ::MakeAtlas(AtlasSize.X, AtlasSize.Y));
	TexB.Reset(TSJ::MakeAtlas(AtlasSize.X, AtlasSize.Y));
	Upload(true);   // (starts clear: a transient texture's first contents aren't)
	// Which cubes the part's surface passes through (sampled finer than the cubes, so the shell has
	// no holes for a line of sight to slip through).
	Occupied.SetNumZeroed(Cubes);
	{
		TArray<FVector> Fine;
		for (const UStaticMeshComponent* C : Parts)
			TSJ::Sample(C, C->GetComponentTransform().GetRelativeTransform(Model), Vx * 0.6, Fine, nullptr);
		for (const FVector& P : Fine)
			if (const int32 I = IndexOf(P); I >= 0) Occupied[I] = 1;
	}
	// Which of it a spray could get to, and which way each bit of it faces out (a mesh's faces don't
	// all wind the same way). A thin sheet open both sides is two points, one for each side; the
	// faces down on the floor or shut inside the part are left out.
	TArray<FVector> OutP, OutN;
	const int32 Count = Points.Num();
	for (int32 I = 0; I < Count; ++I)
	{
		const FVector P = Points[I];
		const FVector N = Normals[I].GetSafeNormal();
		if (N.IsNearlyZero()) continue;
		const bool bFront = OpenSide(World, P, N, true, Ignore);
		// Straight through the other way first: inside a closed shell it meets the far wall.
		const bool bBack = OpenSide(World, P, -N, !bFront, Ignore);
		if (bFront) { OutP.Add(P); OutN.Add(N); }
		if (bBack) { OutP.Add(P); OutN.Add(-N); }
	}
	Dropped = Count - FMath::Min(Count, OutP.Num());
	Points = MoveTemp(OutP);
	Normals = MoveTemp(OutN);
	if (Points.Num() == 0) return false;
	Vox.SetNum(Points.Num());
	FVector Sum = FVector::ZeroVector;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		Vox[I] = IndexOf(Points[I]);
		Sum += Points[I];
	}
	Middle = Sum / Points.Num();
	int32 Boxed = 0;
	for (const UStaticMeshComponent* C : Parts)
	{
		const TArray<FVector>* Pos = nullptr;
		const TArray<int32>* Tris = nullptr;
		if (!TorqueShedPick::Triangles(C, Pos, Tris)) ++Boxed;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque spray: %s on item %d: %d meshes (%d by their box, no shape), %d points (%d out of reach), mask %dx%dx%d of %.1f cm, atlas %dx%d, coat material %s"),
		*Key, Item, Parts.Num(), Boxed, Points.Num(), Dropped, Dims.X, Dims.Y, Dims.Z, Vx, AtlasSize.X, AtlasSize.Y,
		CoatMaterial() ? *CoatMaterial()->GetName() : TEXT("MISSING"));
	Attach(World);
	return true;
}

int32 FTorqueSprayJob::IndexOf(const FVector& P) const
{
	const FVector V = (P - Origin) / Vx;
	const int32 X = FMath::FloorToInt(V.X), Y = FMath::FloorToInt(V.Y), Z = FMath::FloorToInt(V.Z);
	if (X < 0 || Y < 0 || Z < 0 || X >= Dims.X || Y >= Dims.Y || Z >= Dims.Z) return INDEX_NONE;
	return X + Dims.X * (Y + Dims.Y * Z);
}

bool FTorqueSprayJob::ClearWay(const FVector& P, const FVector& D) const
{
	// A walk out along the line through the cubes the part's surface is in, to the mask's edge (or
	// WayOut, past which there's room for a spray to come in at it).
	const double StepCm = Vx * 0.6;
	const FBox Box(Origin, Origin + FVector(Dims) * Vx);
	for (double T = Vx * 2.5; T < WayOut; T += StepCm)
	{
		const FVector Q = P + D * T;
		if (!Box.IsInsideOrOn(Q)) return true;
		const int32 I = IndexOf(Q);
		if (I >= 0 && Occupied[I]) return false;
	}
	return true;
}

bool FTorqueSprayJob::OpenSide(UWorld* World, const FVector& P, const FVector& N, bool bLookRound, const TArray<const AActor*>& Ignore) const
{
	TArray<FVector, TInlineAllocator<16>> Tries;
	Tries.Add(N);
	if (bLookRound)
		for (const FVector& W : TSJ::Ways())
			if (FVector::DotProduct(W, N) >= 0.45) Tries.Add(W);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueSprayOpen), false);
	Params.AddIgnoredActors(Ignore);
	int32 WorldTries = 0;
	for (const FVector& D : Tries)
	{
		if (!ClearWay(P, D)) continue;
		if (!World) return true;
		// Nothing else in the way for a good way out (the floor, a bench, the rest of a bike).
		const FVector Wp = Model.TransformPosition(P), Wd = Model.TransformVectorNoScale(D);
		if (!World->LineTraceTestByChannel(Wp + Wd * 2.5, Wp + Wd * 70.0, ECC_Visibility, Params)) return true;
		if (++WorldTries >= 3) return false;
	}
	return false;
}

UMaterialInstanceDynamic* FTorqueSprayJob::MidFor(UStaticMeshComponent* C)
{
	UMaterialInterface* Base = CoatMaterial();
	if (!Base || !C) return nullptr;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	FLinearColor R0, R1, R2, Off;
	TSJ::Rows(C->GetComponentTransform().GetRelativeTransform(Model), R0, R1, R2, Off);
	M->SetVectorParameterValue(TEXT("Row0"), R0);
	M->SetVectorParameterValue(TEXT("Row1"), R1);
	M->SetVectorParameterValue(TEXT("Row2"), R2);
	M->SetVectorParameterValue(TEXT("RowOffset"), Off);
	M->SetTextureParameterValue(TEXT("MaskA"), TexA.Get());
	M->SetTextureParameterValue(TEXT("MaskB"), TexB.Get());
	M->SetVectorParameterValue(TEXT("MaskOrigin"), FLinearColor((float)Origin.X, (float)Origin.Y, (float)Origin.Z, 0.0f));
	M->SetVectorParameterValue(TEXT("MaskDims"), FLinearColor((float)Dims.X, (float)Dims.Y, (float)Dims.Z, (float)Grid));
	M->SetVectorParameterValue(TEXT("AtlasSize"), FLinearColor((float)AtlasSize.X, (float)AtlasSize.Y, 0.0f, 0.0f));
	M->SetScalarParameterValue(TEXT("Voxel"), (float)Vx);
	M->SetScalarParameterValue(TEXT("RunLevel"), (float)RunLevel);
	M->SetVectorParameterValue(TEXT("CoatColor"), Look.Colour);
	M->SetScalarParameterValue(TEXT("CoatRough"), Look.Rough);
	M->SetScalarParameterValue(TEXT("CoatMetal"), Look.Metal);
	M->SetScalarParameterValue(TEXT("ClearLayer"), Look.bClear ? 1.0f : 0.0f);
	M->SetScalarParameterValue(TEXT("KeepUnder"), Look.bKeep ? 1.0f : 0.0f);
	M->SetVectorParameterValue(TEXT("UnderColor"), Look.Under);
	M->SetScalarParameterValue(TEXT("UnderRough"), Look.UnderRough);
	return M;
}

void FTorqueSprayJob::ApplyParams(bool bAll)
{
	// Which way is up in the part's frame (drips run down), and the coat's state.
	const FVector Up = Model.InverseTransformVectorNoScale(FVector::UpVector);
	for (const TStrongObjectPtr<UMaterialInstanceDynamic>& M : Mids)
	{
		if (!M.IsValid()) continue;
		M->SetVectorParameterValue(TEXT("UpInModel"), FLinearColor((float)Up.X, (float)Up.Y, (float)Up.Z, 0.0f));
		M->SetScalarParameterValue(TEXT("Wet"), (float)Wet);
		M->SetScalarParameterValue(TEXT("Scan"), (float)ScanAmt);
		M->SetScalarParameterValue(TEXT("ScanT"), (float)ScanT);
		M->SetScalarParameterValue(TEXT("Flash"), (float)FlashAmt);
		M->SetScalarParameterValue(TEXT("Begun"), bBegun ? 1.0f : 0.0f);
	}
}

bool FTorqueSprayJob::Attach(UWorld* World)
{
	TArray<UStaticMeshComponent*> Parts;
	MeshesOf(World, Item, Bay, Slot, Parts);
	if (Parts.Num() == 0) return false;
	Model = TSJ::FrameOf(Parts);
	bool bSame = Parts.Num() == Shown.Num();
	for (int32 I = 0; bSame && I < Parts.Num(); ++I) bSame = Shown[I].Get() == Parts[I];
	if (!bSame)
	{
		// Redrawn (the shed changed): the coat goes on the new meshes, as far as it's got.
		for (const TWeakObjectPtr<UStaticMeshComponent>& W : Shown)
			if (UStaticMeshComponent* C = W.Get())
				for (const TStrongObjectPtr<UMaterialInstanceDynamic>& M : Mids)
					if (C->GetOverlayMaterial() == M.Get()) C->SetOverlayMaterial(nullptr);
		Shown.Reset();
		Mids.Reset();
		for (UStaticMeshComponent* C : Parts)
		{
			Shown.Add(C);
			Mids.Emplace(MidFor(C));
		}
		ApplyParams(true);
	}
	for (int32 I = 0; I < Shown.Num(); ++I)
		if (UStaticMeshComponent* C = Shown[I].Get())
			if (Mids.IsValidIndex(I) && Mids[I].IsValid() && C->GetOverlayMaterial() != Mids[I].Get()) C->SetOverlayMaterial(Mids[I].Get());
	return true;
}

void FTorqueSprayJob::SetTool(double InRadius, double InRate, double InFalloff, double InBand)
{
	Radius = InRadius;
	Rate = InRate;
	Falloff = InFalloff;
	Band = FMath::Max(InBand, 0.5);
}

FVector2D FTorqueSprayJob::Sweet() const
{
	return FVector2D(Near / Band, Far * Band);
}

FFan FTorqueSprayJob::FanAt(double Dist) const
{
	const FVector2D S = Sweet();
	const double D = FMath::Max(Dist, 5.0);
	FFan F;
	// The fan spreads with distance, and thins as it spreads; closer than the sweet spot it piles on
	// fast in a tight spot.
	F.Radius = Radius * FMath::Clamp(D / DRef, 0.6, 1.8);
	F.Flow = Rate * Flow * FMath::Clamp(DRef / D * (1.0 + 3.0 * FMath::Max(0.0, S.X - D) / S.X), 0.3, 4.0);
	F.Dry = FMath::SmoothStep(S.Y, S.Y + DrySpan, D);
	F.Flow *= 1.0 - FMath::SmoothStep(S.Y + DrySpan * 0.6, S.Y + OutPast, D);
	F.Zone = D < S.X ? EZone::Close : (D > S.Y + OutPast ? EZone::Out : (D > S.Y ? EZone::Far : EZone::Good));
	return F;
}

bool FTorqueSprayJob::SurfaceHit(const FVector& InEye, const FVector& Dir, double Width, FVector& OutAt, FVector& OutNormal, double& OutT) const
{
	if (Points.Num() == 0) return false;
	const FVector E = Model.InverseTransformPosition(InEye);
	const FVector D = Model.InverseTransformVectorNoScale(Dir).GetSafeNormal();
	double Best = TNumericLimits<double>::Max();
	int32 Bi = INDEX_NONE;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector V = Points[I] - E;
		const double T = FVector::DotProduct(V, D);
		if (T <= 0.0 || T >= Best) continue;
		if ((V - D * T).SizeSquared() < Width * Width)
		{
			Best = T;
			Bi = I;
		}
	}
	if (Bi == INDEX_NONE) return false;
	FVector N = Model.TransformVectorNoScale(Normals[Bi]).GetSafeNormal();
	if (FVector::DotProduct(N, Dir) > 0.0) N = -N;   // towards you
	OutAt = Model.TransformPosition(E + D * Best);
	OutNormal = N;
	OutT = Best;
	return true;
}

int32 FTorqueSprayJob::Step(double Dt, const FVector& InEye, const FVector& Dir, bool bHit, const FVector& At, const FVector& Normal, bool bFiring)
{
	Jet = FJet();
	Jet.bHit = bHit;
	Jet.At = At;
	Jet.Normal = Normal;
	Jet.Dist = FVector::Dist(InEye, At);
	Jet.Radius = Radius;
	Jet.Speed = Speed;
	Eye = InEye;
	if (!bHit || Points.Num() == 0)
	{
		LastAt = FVector(TNumericLimits<double>::Max());
		Speed = 0.0;
		Jet.Speed = 0.0;
		return 0;
	}
	const FFan Fan = FanAt(Jet.Dist);
	// How fast the spray's going over the surface, smoothed.
	if (LastAt.X < TNumericLimits<double>::Max() * 0.5 && Dt > 0.0)
	{
		const double Moved = FVector::Dist(LastAt, At);
		if (Moved < 40.0) Speed = FMath::Lerp(Speed, Moved / Dt, FMath::Clamp(Dt * 6.0, 0.0, 1.0));
	}
	LastAt = At;
	Jet.Zone = Fan.Zone;
	Jet.Radius = Fan.Radius;
	Jet.Speed = Speed;
	Jet.Pace = Speed < Slow ? EPace::Slow : (Speed > Fast ? EPace::Fast : EPace::Good);
	Jet.Dry = Fan.Dry;
	if (!bFiring) return 0;
	const FVector LocalAt = Model.InverseTransformPosition(At);
	const FVector Along = Model.InverseTransformVectorNoScale(Dir).GetSafeNormal();
	const int32 N = Paint(LocalAt, Along, Fan.Radius, Dt * Fan.Flow, Fan.Dry, Falloff);
	Last = Stats();
	if (N > 0) Note();
	return N;
}

int32 FTorqueSprayJob::Paint(const FVector& At, const FVector& Along, double Reach, double Amount, double Dryness, double InFalloff)
{
	if (Amount <= 0.0 || Reach <= 0.0 || !Reaches(Model.TransformPosition(At), Reach + 12.0)) return 0;
	const FVector A = Along.GetSafeNormal();
	// How much lands on each way a surface can face: those facing back towards the gun.
	float Take[6] = {0, 0, 0, 0, 0, 0};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double F = -A[Axis];
		Take[Axis * 2 + (F > 0.0 ? 0 : 1)] = (float)FMath::Clamp((FMath::Abs(F) - Skim) / (FaceOn - Skim), 0.0, 1.0);
	}
	const double Span = Reach + 12.0;
	auto Cl = [](int32 V, int32 Hi) { return FMath::Clamp(V, 0, Hi - 1); };
	const FVector Lo = (At - FVector(Span) - Origin) / Vx, Hi = (At + FVector(Span) - Origin) / Vx;
	const int32 X0 = Cl(FMath::FloorToInt(Lo.X), Dims.X), X1 = Cl(FMath::FloorToInt(Hi.X), Dims.X);
	const int32 Y0 = Cl(FMath::FloorToInt(Lo.Y), Dims.Y), Y1 = Cl(FMath::FloorToInt(Hi.Y), Dims.Y);
	const int32 Z0 = Cl(FMath::FloorToInt(Lo.Z), Dims.Z), Z1 = Cl(FMath::FloorToInt(Hi.Z), Dims.Z);
	int32 N = 0;
	for (int32 Z = Z0; Z <= Z1; ++Z)
	{
		bool bSlice = false;
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector P = Origin + (FVector(X, Y, Z) + FVector(0.5)) * Vx;
				const FVector V = P - At;
				const double Depth = FVector::DotProduct(V, A);
				if (Depth < -5.0 || Depth > 11.0) continue;
				const double D = (V - A * Depth).Size();
				if (D >= Reach) continue;
				const int32 I = X + Dims.X * (Y + Dims.Y * Z);
				const float Add = (float)(Amount * (1.0 - InFalloff * D / Reach));
				for (int32 K = 0; K < 6; ++K)
					if (Take[K] > 0.0f) Coats[I * 6 + K] += Add * Take[K];
				Landed[I] += Add;
				DryAmt[I] += Add * (float)Dryness;
				Store(I);
				++N;
				bSlice = true;
			}
		if (bSlice)
		{
			const int32 TileRow = Z / Grid;
			DirtyLo = FMath::Min(DirtyLo, TileRow);
			DirtyHi = FMath::Max(DirtyHi, TileRow);
		}
	}
	if (N > 0 && !bBegun)
	{
		// Only once the coat's going on is the old surface shown flatted back for it: aiming with the
		// spray controls up the part looks just as it is.
		bBegun = true;
		ApplyParams(false);
	}
	return N;
}

void FTorqueSprayJob::Store(int32 I)
{
	const int32 X = I % Dims.X, Y = (I / Dims.X) % Dims.Y, Z = I / (Dims.X * Dims.Y);
	const int32 Tx = (Z % Grid) * Dims.X + X, Ty = (Z / Grid) * Dims.Y + Y;
	const int32 At = (Ty * AtlasSize.X + Tx) * 4;
	auto Byte = [](float C) { return (uint8)FMath::Clamp(FMath::RoundToInt(C / CoatMax * 255.0f), 0, 255); };
	const float* C = &Coats[I * 6];
	// BGRA in memory. A: r +x, g -x, b +y, a -y. B: r +z, g -z, b how much landed dry.
	BytesA[At + 0] = Byte(C[2]);
	BytesA[At + 1] = Byte(C[1]);
	BytesA[At + 2] = Byte(C[0]);
	BytesA[At + 3] = Byte(C[3]);
	BytesB[At + 0] = (uint8)FMath::Clamp(FMath::RoundToInt(DryOf(I) * 255.0f), 0, 255);
	BytesB[At + 1] = Byte(C[5]);
	BytesB[At + 2] = Byte(C[4]);
	BytesB[At + 3] = 255;
}

float FTorqueSprayJob::CoatsFacing(int32 I, const FVector& N) const
{
	if (I < 0) return 0.0f;
	const float* C = &Coats[I * 6];
	return (float)(N.X * N.X * C[N.X > 0.0 ? 0 : 1] + N.Y * N.Y * C[N.Y > 0.0 ? 2 : 3] + N.Z * N.Z * C[N.Z > 0.0 ? 4 : 5]);
}

float FTorqueSprayJob::DryOf(int32 I) const
{
	return I >= 0 && Landed[I] > 0.0f ? DryAmt[I] / Landed[I] : 0.0f;
}

bool FTorqueSprayJob::Reaches(const FVector& WorldAt, double Margin) const
{
	const FVector P = Model.InverseTransformPosition(WorldAt);
	return FBox(Origin, Origin + FVector(Dims) * Vx).ExpandBy(Margin).IsInsideOrOn(P);
}

void FTorqueSprayJob::Note()
{
	// Runs and dry spray as they happen, and the coat being on.
	const float* Was = Told.Find((int32)ECue::Runs);
	if (Last.Runs > 0.02f && (!Was || Last.Runs > *Was + 0.04f))
	{
		Told.Add((int32)ECue::Runs, Last.Runs);
		Cues.Add({ECue::Runs, Jet.Zone != EZone::Close ? FString(TEXT("Runs! Keep it moving")) : FString(TEXT("Runs! Too close"))});
	}
	Was = Told.Find((int32)ECue::Dry);
	if (Last.Dry > 0.04f && (!Was || Last.Dry > *Was + 0.06f))
	{
		Told.Add((int32)ECue::Dry, Last.Dry);
		Cues.Add({ECue::Dry, TEXT("Dry spray! Get closer")});
	}
	if (Last.Covered >= Done && !Told.Contains((int32)ECue::Ready))
	{
		Told.Add((int32)ECue::Ready, 1.0f);
		Cues.Add({ECue::Ready, TEXT("Coat on! Let go to finish, or fill the last gaps")});
	}
}

FStats FTorqueSprayJob::Stats() const
{
	FStats S;
	if (Points.Num() == 0 || Coats.Num() == 0) return S;
	int32 Covered = 0, Thin = 0, Run = 0, DryN = 0, Good = 0, Touched = 0;
	double Total = 0.0;
	// Which side of the part each point's on, seen from where you last sprayed from.
	FVector2D Toward = FVector2D::ZeroVector;
	if (Eye.X < TNumericLimits<double>::Max() * 0.5)
	{
		const FVector T = Eye - Model.TransformPosition(Middle);
		Toward = FVector2D(T.X, T.Y).GetSafeNormal();
	}
	int32 SideN[4] = {0, 0, 0, 0}, SideC[4] = {0, 0, 0, 0};   // this side, far side, top, underneath
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const float C = CoatsFacing(Vox[I], Normals[I]);
		const float D = C > 0.0f ? DryOf(Vox[I]) : 0.0f;
		Total += C;
		if (C > 0.05f) ++Touched;
		const FVector Nw = Model.TransformVectorNoScale(Normals[I]);
		int32 Side = 0;
		if (Nw.Z > 0.6) Side = 2;
		else if (Nw.Z < -0.6) Side = 3;
		else if (!Toward.IsNearlyZero() && FVector2D::DotProduct(FVector2D(Nw.X, Nw.Y), Toward) < 0.0) Side = 1;
		++SideN[Side];
		if (C >= Full)
		{
			++Covered;
			++SideC[Side];
			if (C < RunLevel && D < 0.5f) ++Good;
			if (D >= 0.5f) ++DryN;
		}
		else if (C > 0.05f) ++Thin;
		if (C >= RunLevel) ++Run;
	}
	const float N = (float)FMath::Max(Points.Num(), 1);
	S.Covered = Covered / N;
	S.Thin = Thin / N;
	S.Runs = Run / N;
	S.Dry = DryN / N;
	S.Even = Touched > 0 ? (float)Good / Touched : 1.0f;
	S.Seen = Points.Num();
	S.Coats = (float)(Total / N);
	for (int32 K = 0; K < 4; ++K) S.Sides[K] = SideN[K] > 0 ? (float)SideC[K] / SideN[K] : -1.0f;
	return S;
}

float FTorqueSprayJob::Quality(const FStats& S) const
{
	return FMath::Clamp(S.Covered * (1.0f - S.Runs * 0.6f - S.Dry * 0.35f), 0.0f, 1.0f);
}

bool FTorqueSprayJob::Ready() const
{
	if (Points.Num() == 0) return false;
	const FStats S = Last.Seen == 0 ? Stats() : Last;
	return S.Covered >= Done;
}

bool FTorqueSprayJob::Complete() const
{
	return Last.Seen > 0 && Last.Covered >= AutoDone;
}

FTorqueShedRequest FTorqueSprayJob::Result() const
{
	const FStats S = Stats();
	FTorqueShedRequest R = Request;
	// The host reads a sprayed coat's quality from Amount (0: none sent, a hand coat) and its runs from Tilt.
	R.Amount = FMath::Max(0.001f, Quality(S));
	R.Tilt = S.Runs;
	return R;
}

void FTorqueSprayJob::Finish(bool bFlash)
{
	bLive = false;
	if (bFlash)
	{
		FlashAmt = 0.7;
		bDrying = true;
		Upload(true);
	}
	else bGone = true;
}

int32 FTorqueSprayJob::PaintedCount() const
{
	int32 N = 0;
	for (float L : Landed) N += L > 0.05f ? 1 : 0;
	return N;
}

void FTorqueSprayJob::Upload(bool bAll)
{
	if (!TexA.IsValid() || !TexB.IsValid()) return;
	int32 Lo = DirtyLo, Hi = DirtyHi;
	if (bAll)
	{
		Lo = 0;
		Hi = FMath::DivideAndRoundUp(Dims.Z, Grid) - 1;
	}
	if (Hi < Lo) return;
	const int32 Row0 = Lo * Dims.Y, Rows = (Hi - Lo + 1) * Dims.Y;
	TSJ::Send(TexA.Get(), BytesA, AtlasSize.X, Row0, Rows);
	TSJ::Send(TexB.Get(), BytesB, AtlasSize.X, Row0, Rows);
	DirtyLo = MAX_int32;
	DirtyHi = -1;
	SinceUpload = 0.0;
}

void FTorqueSprayJob::Tick(double Dt)
{
	const double Want = bScanOn ? 1.0 : 0.0;
	if (ScanAmt != Want || bScanOn)
	{
		ScanAmt = Want > ScanAmt ? FMath::Min(Want, ScanAmt + Dt * 3.0) : FMath::Max(Want, ScanAmt - Dt * 3.0);
		ScanT += Dt;
	}
	if (FlashAmt > 0.0) FlashAmt = FMath::Max(0.0, FlashAmt - Dt * 1.6);
	if (bDrying)
	{
		Wet = FMath::Max(0.0, Wet - Dt * 0.8);
		// The finished coat is the part's own look from here on: the wet pass comes off so it can't
		// linger over it looking different from how it's drawn everywhere else.
		Dried += Dt;
		if (Dried >= DryFor) bGone = true;
	}
	ApplyParams(false);
	SinceUpload += Dt;
	if (DirtyHi >= DirtyLo && SinceUpload >= 1.0 / 30.0) Upload(false);
}

TArray<FStep> FTorqueSprayJob::Steps(const UTorqueBuildSubsystem& B) const
{
	TArray<FStep> Out;
	const FTorqueItem* It = B.Item(Item);
	if (!It) return Out;
	const bool bPrimed = It->Primer > 0.0f;
	const bool bPainted = It->PaintQ >= 0.0f && !It->bPrimerTop;
	const bool bLacquered = UTorqueBuildSubsystem::IsLacquer(It->Coat);
	FStep P;
	P.Name = TEXT("Primer");
	P.bOptional = true;
	P.Q = It->Primer;
	P.State = Layer == ELayer::Primer ? FStep::Now : (bPrimed ? FStep::DoneStep : (bPainted || Layer != ELayer::Primer ? FStep::Skip : FStep::Todo));
	Out.Add(P);
	FStep C;
	C.Name = TEXT("Colour");
	C.Q = FMath::Max(It->PaintQ, 0.0f);
	C.State = Layer == ELayer::Paint ? FStep::Now : (bPainted ? FStep::DoneStep : FStep::Todo);
	Out.Add(C);
	FStep L;
	L.Name = TEXT("Lacquer");
	L.bOptional = true;
	L.Q = It->CoatQ;
	L.State = Layer == ELayer::Clear ? FStep::Now : (bLacquered ? FStep::DoneStep : FStep::Todo);
	Out.Add(L);
	return Out;
}
