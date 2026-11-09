#include "Shed/TorqueWashJob.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedPick.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "RHI.h"
#include "TextureResource.h"

namespace TorqueWashImpl
{
// The bays whose muck a wash game is drawing (their parts' own grime held off), by world.
struct FHeld
{
	TWeakObjectPtr<const UWorld> World;
	int32 Bay = INDEX_NONE;
};
TArray<FHeld>& Held()
{
	static TArray<FHeld> H;
	return H;
}

// Points on a mesh's surface about Spacing apart (model cm, from its own cm by ToModel), each with the
// way its face points (either way: which is out is worked out later). Its own triangles if it has a
// shape, else its box's faces. (TorqueSprayJob's sampling, the same R2 spread.)
void Sample(const UStaticMeshComponent* C, const FTransform& ToModel, double Spacing, TArray<FVector>& OutP, TArray<FVector>* OutN)
{
	const TArray<FVector>* Pos = nullptr;
	const TArray<int32>* Tris = nullptr;
	const double Area1 = Spacing * Spacing;
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
	for (int32 Axis = 0; Axis < 3; ++Axis)
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Ua = (Axis + 1) % 3, Va = (Axis + 2) % 3;
			const FVector Lo = Box.Min, Hi = Box.Max, Size = Box.GetSize();
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

void Rows(const FTransform& ToModel, FLinearColor& R0, FLinearColor& R1, FLinearColor& R2, FLinearColor& Off)
{
	const FMatrix M = ToModel.ToMatrixWithScale();
	R0 = FLinearColor((float)M.M[0][0], (float)M.M[1][0], (float)M.M[2][0], 0.0f);
	R1 = FLinearColor((float)M.M[0][1], (float)M.M[1][1], (float)M.M[2][1], 0.0f);
	R2 = FLinearColor((float)M.M[0][2], (float)M.M[1][2], (float)M.M[2][2], 0.0f);
	Off = FLinearColor((float)M.M[3][0], (float)M.M[3][1], (float)M.M[3][2], 0.0f);
}

// How much of a part's muck lies at a point (Dirt's usual pattern, roughly): road muck thick low down
// and on the undersides, a film of dust on top. M_WashDirt works the same out per pixel (WASH_HLSL).
float Pattern(double H, const FVector& N)
{
	const double Hn = FMath::Clamp(H, 0.0, 1.0);
	double P = FMath::Lerp(1.35, 0.4, Hn * Hn * (3.0 - 2.0 * Hn));
	if (N.Z < -0.5) P *= 1.45;                    // the undersides catch the spray off the tyres
	else if (N.Z > 0.6) P *= 0.75;                // the tops: settled dust
	return (float)P;
}

ATorqueShedBike* BikeOf(UWorld* World, int32 Bay)
{
	if (!World) return nullptr;
	for (TActorIterator<ATorqueShedBike> It(World); It; ++It)
		if (It->Bay == Bay) return *It;
	return nullptr;
}

void MeshesOf(ATorqueShedBike* Bike, TArray<UStaticMeshComponent*>& Out)
{
	Out.Reset();
	if (!Bike) return;
	for (UStaticMeshComponent* C : Bike->Parts)
		if (C && C->IsVisible() && !TorqueShedModel::SlotTag(C).IsNone()) Out.Add(C);
}
} // namespace TorqueWashImpl

namespace TWJ = TorqueWashImpl;
namespace TW = TorqueWash;   // (qualified, not a using-directive: the spray game's names are alike, and unity builds share them)

namespace TorqueWash
{
FJetShape JetAt(double Dist)
{
	FJetShape J;
	J.Zone = Dist < NearDist ? EZone::Close : (Dist > OutDist ? EZone::Out : (Dist > FarDist ? EZone::Far : EZone::Good));
	J.Radius = JetRadius * FMath::Clamp(Dist / DRef, 0.5, 2.4);
	J.Power = 1.0 - FMath::SmoothStep(FarDist, OutDist, Dist);
	return J;
}

bool HidesGrime(const UObject* WorldContext, int32 Bay)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return false;
	for (const TWJ::FHeld& H : TWJ::Held())
		if (H.Bay == Bay && H.World.Get() == World) return true;
	return false;
}

const TCHAR* ZoneWord(EZone Z)
{
	switch (Z)
	{
	case EZone::Good: return TEXT("GOOD");
	case EZone::Close: return TEXT("VERY CLOSE: a tiny spot");
	case EZone::Far: return TEXT("TOO FAR: losing its bite");
	default: return TEXT("OUT OF REACH");
	}
}
} // namespace TorqueWash

FTorqueWashJob::FTorqueWashJob() = default;

FTorqueWashJob::~FTorqueWashJob()
{
	Detach();
}

UMaterialInterface* FTorqueWashJob::DirtMaterial()
{
	// M_WashDirt comes from the shed's level script (Tools/ue_build_wash.py, run by ue_build_shed.py).
	static UMaterialInterface* Mat = nullptr;
	static bool bLooked = false;
	if (!bLooked)
	{
		bLooked = true;
		Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_WashDirt.M_WashDirt"));
		if (Mat) Mat->AddToRoot();
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque wash: no M_WashDirt (run Tools/ue_build_shed.py): the muck won't wash off where the jet goes, only part by part"));
		}
	}
	return Mat;
}

bool FTorqueWashJob::Setup(UWorld* World, int32 InBay)
{
	Bay = InBay;
	WorldOf = World;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World);
	ATorqueShedBike* Bike = TWJ::BikeOf(World, Bay);
	if (!B || !Bike) return false;
	TArray<UStaticMeshComponent*> Meshes;
	TWJ::MeshesOf(Bike, Meshes);
	if (Meshes.Num() == 0) return false;
	Model = Bike->GetActorTransform();
	Model.SetScale3D(FVector::OneVector);
	// The parts: one for each slot that has an item in it.
	TMap<FName, int32> PartOf;
	TArray<int32> MeshPart;
	for (UStaticMeshComponent* C : Meshes)
	{
		const FName Slot = TorqueShedModel::SlotTag(C);
		int32* Found = PartOf.Find(Slot);
		if (!Found)
		{
			const int32 Item = B->ItemOn(Bay, Slot);
			const FTorqueItem* It = Item ? B->Item(Item) : nullptr;
			if (!It)
			{
				MeshPart.Add(INDEX_NONE);
				continue;
			}
			const int32 NewPart = Items.Add(Item);
			Slots.Add(Slot);
			Names.Add(TorqueShedModel::SlotLabel(Slot));
			Start.Add(It->Dirt);
			Found = &PartOf.Add(Slot, NewPart);
		}
		MeshPart.Add(*Found);
	}
	if (Items.Num() == 0) return false;
	// The surface, sampled coarser for a big bike so the points stay few enough.
	double Spacing = TW::SampleCell;
	TArray<FVector> RawP, RawN;
	TArray<int32> RawPart;
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		RawP.Reset();
		RawN.Reset();
		RawPart.Reset();
		for (int32 M = 0; M < Meshes.Num(); ++M)
		{
			if (MeshPart[M] == INDEX_NONE) continue;
			const int32 Was = RawP.Num();
			TWJ::Sample(Meshes[M], Meshes[M]->GetComponentTransform().GetRelativeTransform(Model), Spacing, RawP, &RawN);
			for (int32 K = Was; K < RawP.Num(); ++K) RawPart.Add(MeshPart[M]);
		}
		if (RawP.Num() <= TW::MaxPoints) break;
		Spacing *= 1.4;
	}
	if (RawP.Num() == 0) return false;
	FBox Box(ForceInit);
	for (const FVector& P : RawP) Box += P;
	ZLo = Box.Min.Z;
	ZHi = FMath::Max(Box.Max.Z, ZLo + 10.0);
	Vx = FMath::Max(TW::MinVoxel, Box.GetSize().GetMax() / (double)(TW::MaskMaxDim - 6));
	Box = Box.ExpandBy(Vx * 2.0);
	Origin = Box.Min;
	const FVector Size = Box.GetSize();
	Dims = FIntVector(FMath::Clamp(FMath::CeilToInt(Size.X / Vx), 2, TW::MaskMaxDim), FMath::Clamp(FMath::CeilToInt(Size.Y / Vx), 2, TW::MaskMaxDim),
		FMath::Clamp(FMath::CeilToInt(Size.Z / Vx), 2, TW::MaskMaxDim));
	const int32 Cubes = Dims.X * Dims.Y * Dims.Z;
	Washed.SetNumZeroed(Cubes * 6);
	Occupied.SetNumZeroed(Cubes);
	{
		TArray<FVector> Fine;
		for (int32 M = 0; M < Meshes.Num(); ++M)
			TWJ::Sample(Meshes[M], Meshes[M]->GetComponentTransform().GetRelativeTransform(Model), Vx * 0.6, Fine, nullptr);
		for (const FVector& P : Fine)
			if (const int32 I = IndexOf(P); I >= 0) Occupied[I] = 1;
	}
	// Which sides of it the jet can get to (straight out, or from round about), from the bike's own
	// shape: the faces shut away inside it, or tight against another part, are left out.
	static const FVector Tilts[6] = {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1)};
	auto Open = [this](const FVector& P, const FVector& N)
	{
		if (ClearWay(P, N)) return true;
		for (const FVector& T : Tilts)
			if (FMath::Abs(FVector::DotProduct(T, N)) <= 0.9 && ClearWay(P, (N + T * 0.8).GetSafeNormal())) return true;
		return false;
	};
	for (int32 I = 0; I < RawP.Num(); ++I)
	{
		const FVector N = RawN[I].GetSafeNormal();
		if (N.IsNearlyZero()) continue;
		for (const FVector& Side : {N, -N})
			if (Open(RawP[I], Side))
			{
				Points.Add(RawP[I]);
				Normals.Add(Side);
				PointPart.Add(RawPart[I]);
			}
	}
	if (Points.Num() == 0) return false;
	const int32 Parts = Items.Num();
	PartN.SetNumZeroed(Parts);
	PatMean.SetNumZeroed(Parts);
	Vox.SetNum(Points.Num());
	Pat.SetNum(Points.Num());
	FVector Sum = FVector::ZeroVector;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		Vox[I] = IndexOf(Points[I]);
		Pat[I] = TWJ::Pattern((Points[I].Z - ZLo) / (ZHi - ZLo), Normals[I]);
		PartN[PointPart[I]] += 1;
		PatMean[PointPart[I]] += Pat[I];
		Sum += Points[I];
	}
	Mid = Sum / Points.Num();
	for (int32 P = 0; P < Parts; ++P) PatMean[P] = PartN[P] > 0 ? PatMean[P] / PartN[P] : 1.0f;
	// A part's points carry its muck in its pattern, averaging to the shed's figure.
	for (int32 I = 0; I < Points.Num(); ++I) Pat[I] /= FMath::Max(PatMean[PointPart[I]], 0.05f);
	Sent.Init(1.0f, Parts);
	Flash.Init(0.0f, Parts);
	Done.Init(false, Parts);
	Dinged.Init(false, Parts);
	// Parts that are clean already (or the jet can't get at) are done, without a cue.
	for (int32 P = 0; P < Parts; ++P)
		if (PartN[P] == 0 || CleanEnough(P, PartClean(P))) Done[P] = Dinged[P] = true;
	bBikeDone = !Done.Contains(false);
	// The atlases: surfaces in a cube (alpha) with nothing washed off them yet.
	Grid = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt((double)Dims.Z)));
	AtlasSize = FIntPoint(Grid * Dims.X, FMath::DivideAndRoundUp(Dims.Z, Grid) * Dims.Y);
	BytesA.SetNumZeroed(AtlasSize.X * AtlasSize.Y * 4);
	BytesB.SetNumZeroed(AtlasSize.X * AtlasSize.Y * 4);
	for (int32 I = 0; I < Cubes; ++I)
		if (Occupied[I]) Store(I);
	TexA.Reset(TWJ::MakeAtlas(AtlasSize.X, AtlasSize.Y));
	TexB.Reset(TWJ::MakeAtlas(AtlasSize.X, AtlasSize.Y));
	Upload(true);
	UE_LOG(LogTemp, Log, TEXT("Torque wash: bay %d, %d parts, %d points (of %d), mask %dx%dx%d of %.1f cm"),
		Bay, Parts, Points.Num(), RawP.Num(), Dims.X, Dims.Y, Dims.Z, Vx);
	Attach(World);
	return true;
}

int32 FTorqueWashJob::IndexOf(const FVector& P) const
{
	const FVector V = (P - Origin) / Vx;
	const int32 X = FMath::FloorToInt(V.X), Y = FMath::FloorToInt(V.Y), Z = FMath::FloorToInt(V.Z);
	if (X < 0 || Y < 0 || Z < 0 || X >= Dims.X || Y >= Dims.Y || Z >= Dims.Z) return INDEX_NONE;
	return X + Dims.X * (Y + Dims.Y * Z);
}

bool FTorqueWashJob::ClearWay(const FVector& P, const FVector& D) const
{
	const FBox Box(Origin, Origin + FVector(Dims) * Vx);
	for (double T = Vx * 2.5; T < TW::WayOut; T += Vx * 0.6)
	{
		const FVector Q = P + D * T;
		if (!Box.IsInsideOrOn(Q)) return true;
		const int32 I = IndexOf(Q);
		if (I >= 0 && Occupied[I]) return false;
	}
	return true;
}

float FTorqueWashJob::WashedFacing(int32 I, const FVector& N) const
{
	if (I < 0) return 0.0f;
	const float* W = &Washed[I * 6];
	return (float)(N.X * N.X * W[N.X > 0.0 ? 0 : 1] + N.Y * N.Y * W[N.Y > 0.0 ? 2 : 3] + N.Z * N.Z * W[N.Z > 0.0 ? 4 : 5]);
}

float FTorqueWashJob::Remaining(int32 Point) const
{
	const int32 P = PointPart[Point];
	if (Done[P]) return 0.0f;
	return Start[P] * Pat[Point] * (1.0f - FMath::Clamp(WashedFacing(Vox[Point], Normals[Point]), 0.0f, 1.0f));
}

float FTorqueWashJob::Credit(int32 Point) const
{
	const float L = Remaining(Point);
	return L <= TW::CleanLevel ? 1.0f : FMath::Clamp(1.0f - (L - TW::CleanLevel) / TW::CreditSpan, 0.0f, 1.0f);
}

float FTorqueWashJob::PartClean(int32 Part) const
{
	if (!PartN.IsValidIndex(Part) || PartN[Part] == 0 || Done[Part]) return 1.0f;
	double Sum = 0.0;
	for (int32 I = 0; I < Points.Num(); ++I)
		if (PointPart[I] == Part) Sum += Credit(I);
	return (float)(Sum / PartN[Part]);
}

bool FTorqueWashJob::CleanEnough(int32 Part, float CleanShare) const
{
	return CleanShare >= TW::DoneShare || (1.0f - CleanShare) * PartN[Part] <= FMath::Max((float)TW::SpecksMin, TW::Specks * PartN[Part]);
}

void FTorqueWashJob::Store(int32 I)
{
	const int32 X = I % Dims.X, Y = (I / Dims.X) % Dims.Y, Z = I / (Dims.X * Dims.Y);
	const int32 Tx = (Z % Grid) * Dims.X + X, Ty = (Z / Grid) * Dims.Y + Y;
	const int32 At = (Ty * AtlasSize.X + Tx) * 4;
	auto Byte = [](float W) { return (uint8)FMath::Clamp(FMath::RoundToInt(W * 255.0f), 0, 255); };
	const float* W = &Washed[I * 6];
	// BGRA in memory. A: r +x, g -x, b +y, a -y. B: r +z, g -z, a 1 where there's surface.
	BytesA[At + 0] = Byte(W[2]);
	BytesA[At + 1] = Byte(W[1]);
	BytesA[At + 2] = Byte(W[0]);
	BytesA[At + 3] = Byte(W[3]);
	BytesB[At + 0] = 0;
	BytesB[At + 1] = Byte(W[5]);
	BytesB[At + 2] = Byte(W[4]);
	BytesB[At + 3] = Occupied[I] ? 255 : 0;
}

void FTorqueWashJob::Upload(bool bAll)
{
	if (!TexA.IsValid() || !TexB.IsValid()) return;
	int32 Lo = DirtyLo, Hi = DirtyHi;
	if (bAll)
	{
		Lo = 0;
		Hi = FMath::DivideAndRoundUp(Dims.Z, Grid) - 1;
	}
	if (Hi < Lo) return;
	const int32 Row0 = Lo * Dims.Y, Count = (Hi - Lo + 1) * Dims.Y;
	TWJ::Send(TexA.Get(), BytesA, AtlasSize.X, Row0, Count);
	TWJ::Send(TexB.Get(), BytesB, AtlasSize.X, Row0, Count);
	DirtyLo = MAX_int32;
	DirtyHi = -1;
	SinceUpload = 0.0;
}

void FTorqueWashJob::Step(double Dt, const FVector& InEye, const FVector& Dir, bool bFiring, bool bWorldHit, const FVector& WorldAt, const FVector& WorldNormal)
{
	Eye = InEye;
	Jet = TW::FJet();
	// Where the line meets the bike's own washable surface (a side of it facing back up the line).
	const FVector E = Model.InverseTransformPosition(InEye);
	const FVector D = Model.InverseTransformVectorNoScale(Dir).GetSafeNormal();
	constexpr double Width = 3.0;
	double Best = TNumericLimits<double>::Max();
	int32 Bi = INDEX_NONE;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const FVector V = Points[I] - E;
		const double T = FVector::DotProduct(V, D);
		if (T <= 0.0 || T >= Best || FVector::DotProduct(Normals[I], D) > 0.0) continue;
		if ((V - D * T).SizeSquared() < Width * Width)
		{
			Best = T;
			Bi = I;
		}
	}
	const bool bOnBike = Bi != INDEX_NONE && (!bWorldHit || FVector::Dist(InEye, WorldAt) > Best - 5.0);
	Jet.bHit = bWorldHit || bOnBike;
	Jet.At = WorldAt;
	Jet.Normal = WorldNormal;
	if (bOnBike)
	{
		Jet.bBike = true;
		Jet.At = Model.TransformPosition(E + D * Best);
		Jet.Normal = Model.TransformVectorNoScale(Normals[Bi]).GetSafeNormal();
		Jet.Part = PointPart[Bi];
	}
	Jet.Dist = Jet.bHit ? FVector::Dist(InEye, Jet.At) : TW::OutDist + 40.0;
	const TW::FJetShape Shape = TW::JetAt(Jet.Dist);
	Jet.Zone = Jet.bHit ? Shape.Zone : TW::EZone::Out;
	Jet.Radius = Shape.Radius;
	if (!bFiring || !bOnBike || Shape.Power <= 0.0) return;
	const double HitLevel = Start[PointPart[Bi]] * Pat[Bi];
	Wash(Model.InverseTransformPosition(Jet.At), D, Shape.Radius, Dt * TW::WashRate * Shape.Power / TW::Grip(HitLevel));
}

void FTorqueWashJob::Wash(const FVector& At, const FVector& Along, double Reach, double Amount)
{
	if (Amount <= 0.0 || Reach <= 0.0) return;
	float TakeK[6] = {0, 0, 0, 0, 0, 0};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double F = -Along[Axis];
		TakeK[Axis * 2 + (F > 0.0 ? 0 : 1)] = (float)TW::Take(FMath::Abs(F));
	}
	const double Span = Reach + Vx;
	auto Cl = [](int32 V, int32 Hi) { return FMath::Clamp(V, 0, Hi - 1); };
	const FVector Lo = (At - FVector(Span) - Origin) / Vx, Hi = (At + FVector(Span) - Origin) / Vx;
	const int32 X0 = Cl(FMath::FloorToInt(Lo.X), Dims.X), X1 = Cl(FMath::FloorToInt(Hi.X), Dims.X);
	const int32 Y0 = Cl(FMath::FloorToInt(Lo.Y), Dims.Y), Y1 = Cl(FMath::FloorToInt(Hi.Y), Dims.Y);
	const int32 Z0 = Cl(FMath::FloorToInt(Lo.Z), Dims.Z), Z1 = Cl(FMath::FloorToInt(Hi.Z), Dims.Z);
	TSet<int32> Touched;
	for (int32 Z = Z0; Z <= Z1; ++Z)
	{
		bool bSlice = false;
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 I = X + Dims.X * (Y + Dims.Y * Z);
				if (!Occupied[I]) continue;
				const FVector P = Origin + (FVector(X, Y, Z) + FVector(0.5)) * Vx;
				const FVector V = P - At;
				const double Depth = FVector::DotProduct(V, Along);
				if (Depth < -Vx * 1.5 || Depth > Vx * 3.0) continue;   // the surface it lands on, not the far side of the bike
				const double Off = (V - Along * Depth).Size();
				if (Off >= Reach) continue;
				const float Add = (float)(Amount * (1.0 - TW::JetFalloff * Off / Reach));
				bool bAny = false;
				for (int32 K = 0; K < 6; ++K)
					if (TakeK[K] > 0.0f && Washed[I * 6 + K] < 1.0f)
					{
						Washed[I * 6 + K] = FMath::Min(1.0f, Washed[I * 6 + K] + Add * TakeK[K]);
						bAny = true;
					}
				if (!bAny) continue;
				Store(I);
				bSlice = true;
			}
		if (bSlice)
		{
			const int32 TileRow = Z / Grid;
			DirtyLo = FMath::Min(DirtyLo, TileRow);
			DirtyHi = FMath::Max(DirtyHi, TileRow);
		}
	}
	// Which parts the jet's been on (only those can have come clean).
	const double R2 = FMath::Square(Reach + Vx);
	for (int32 I = 0; I < Points.Num(); ++I)
		if (!Done[PointPart[I]] && FVector::DistSquared(Points[I], At) < R2) Touched.Add(PointPart[I]);
	StatsAge = 1.0;
	CheckDone(Touched);
}

void FTorqueWashJob::CheckDone(const TSet<int32>& Touched)
{
	for (int32 P : Touched)
	{
		if (Done[P] || !CleanEnough(P, PartClean(P))) continue;
		Done[P] = true;
		ToCallClean.AddUnique(P);
		Flash[P] = 1.0f;
		if (!Dinged[P])
		{
			Dinged[P] = true;
			Cues.Add({false, Names[P]});
		}
	}
	if (!bBikeDone && !Done.Contains(false))
	{
		bBikeDone = true;
		// The bits the jet can't get at go with the rest, so the shed calls the whole bike clean.
		for (int32 P = 0; P < Items.Num(); ++P) ToCallClean.AddUnique(P);
		for (float& F : Flash) F = 1.0f;
		Cues.Add({true, FString()});
	}
}

bool FTorqueWashJob::TakeUpdate(FTorqueShedRequest& Out)
{
	Out.Items.Reset();
	Out.Amounts.Reset();
	// Each part's muck left now, as a share of what it was, against what the shed last heard.
	TArray<double> Left;
	Left.SetNumZeroed(Items.Num());
	TArray<double> Whole;
	Whole.SetNumZeroed(Items.Num());
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const int32 P = PointPart[I];
		Whole[P] += Pat[I];
		Left[P] += Pat[I] * (1.0 - FMath::Clamp(WashedFacing(Vox[I], Normals[I]), 0.0f, 1.0f));
	}
	for (int32 P = 0; P < Items.Num(); ++P)
	{
		if (Start[P] <= 0.0f) continue;
		const bool bCall = ToCallClean.Contains(P);
		const float Share = bCall || Done[P] ? 0.0f : (float)(Whole[P] > 0.0 ? Left[P] / Whole[P] : 1.0);
		if (!bCall && Share > Sent[P] - 0.01f) continue;
		Out.Items.Add(Items[P]);
		Out.Amounts.Add(Share <= 0.0f ? 0.0f : FMath::Clamp(Share / FMath::Max(Sent[P], 1e-4f), 0.0f, 1.0f));
		Sent[P] = Share;
	}
	ToCallClean.Reset();
	return Out.Items.Num() > 0;
}

void FTorqueWashJob::Resync(const UTorqueBuildSubsystem& B)
{
	bool bChanged = false;
	for (int32 P = 0; P < Items.Num(); ++P)
	{
		const FTorqueItem* It = B.Item(Items[P]);
		if (!It || Done[P]) continue;
		// What the shed has, against what this wash makes of it: less (someone else washed it, the
		// parts washer, the brush) evens the start down to match.
		const float Mine = Start[P] * Sent[P];
		if (It->Dirt < Mine - 0.02f)
		{
			Start[P] = It->Dirt / FMath::Max(Sent[P], 0.02f);
			bChanged = true;
			if (It->Dirt <= 0.0f)
			{
				Done[P] = Dinged[P] = true;
				Start[P] = 0.0f;
			}
		}
	}
	if (!bChanged) return;
	TSet<int32> All;
	for (int32 P = 0; P < Items.Num(); ++P) All.Add(P);
	CheckDone(All);
	StatsAge = 1.0;
	for (FShown& S : Shown)
		if (S.M.IsValid() && Start.IsValidIndex(S.Part)) S.M->SetScalarParameterValue(TEXT("Grime"), Done[S.Part] ? 0.0f : Start[S.Part]);
}

TW::FStats FTorqueWashJob::Stats() const
{
	if (StatsAge < 0.1 && Cached.Parts.Num() > 0) return Cached;
	StatsAge = 0.0;
	TW::FStats S;
	FVector2D Toward = FVector2D::ZeroVector;
	if (Eye.X < TNumericLimits<double>::Max() * 0.5)
	{
		const FVector T = Eye - Model.TransformPosition(Mid);
		Toward = FVector2D(T.X, T.Y).GetSafeNormal();
	}
	double SideN[4] = {0, 0, 0, 0}, SideC[4] = {0, 0, 0, 0}, Total = 0.0;
	TArray<double> PartSum;
	PartSum.SetNumZeroed(Items.Num());
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const float C = Credit(I);
		Total += C;
		PartSum[PointPart[I]] += C;
		const FVector Nw = Model.TransformVectorNoScale(Normals[I]);
		int32 Side = 0;
		if (Nw.Z > 0.6) Side = 2;
		else if (Nw.Z < -0.6) Side = 3;
		else if (!Toward.IsNearlyZero() && FVector2D::DotProduct(FVector2D(Nw.X, Nw.Y), Toward) < 0.0) Side = 1;
		SideN[Side] += 1.0;
		SideC[Side] += C;
	}
	S.Clean = bBikeDone ? 1.0f : (float)(Total / FMath::Max(Points.Num(), 1));
	for (int32 K = 0; K < 4; ++K) S.Sides[K] = SideN[K] > 0.0 ? (float)(SideC[K] / SideN[K]) : -1.0f;
	for (int32 P = 0; P < Items.Num(); ++P)
	{
		if (PartN[P] == 0) continue;
		TW::FPartStat Ps;
		Ps.Part = P;
		Ps.Name = Names[P];
		Ps.bDone = Done[P];
		Ps.Clean = Done[P] ? 1.0f : (float)(PartSum[P] / PartN[P]);
		S.Parts.Add(Ps);
	}
	S.Parts.Sort([](const TW::FPartStat& A, const TW::FPartStat& B) { return A.Clean < B.Clean; });
	Cached = S;
	return S;
}

FVector FTorqueWashJob::Middle() const
{
	return Model.TransformPosition(Mid);
}

bool FTorqueWashJob::Owns(const UStaticMeshComponent* C) const
{
	if (!C) return false;
	for (const FShown& S : Shown)
		if (S.C.Get() == C) return true;
	return false;
}

UMaterialInstanceDynamic* FTorqueWashJob::MidFor(UStaticMeshComponent* C, int32 Part)
{
	UMaterialInterface* Base = DirtMaterial();
	if (!Base || !C) return nullptr;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	FLinearColor R0, R1, R2, Off;
	TWJ::Rows(C->GetComponentTransform().GetRelativeTransform(Model), R0, R1, R2, Off);
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
	M->SetVectorParameterValue(TEXT("ZRange"), FLinearColor((float)ZLo, (float)ZHi, 0.0f, 0.0f));
	M->SetScalarParameterValue(TEXT("Grime"), Start.IsValidIndex(Part) && !Done[Part] ? Start[Part] : 0.0f);
	M->SetScalarParameterValue(TEXT("PatScale"), PatMean.IsValidIndex(Part) ? 1.0f / FMath::Max(PatMean[Part], 0.05f) : 1.0f);
	return M;
}

void FTorqueWashJob::ApplyParams()
{
	for (FShown& S : Shown)
	{
		if (!S.M.IsValid()) continue;
		S.M->SetScalarParameterValue(TEXT("Scan"), (float)ScanAmt);
		S.M->SetScalarParameterValue(TEXT("ScanT"), (float)ScanT);
		S.M->SetScalarParameterValue(TEXT("Flash"), Flash.IsValidIndex(S.Part) ? Flash[S.Part] : 0.0f);
		if (Done.IsValidIndex(S.Part) && Done[S.Part]) S.M->SetScalarParameterValue(TEXT("Grime"), 0.0f);
	}
}

void FTorqueWashJob::Relook()
{
	UWorld* World = WorldOf.Get();
	if (ATorqueShedBike* Bike = TWJ::BikeOf(World, Bay)) TorqueShedModel::ApplyLooks(World, Bay, Bike->Parts);
}

void FTorqueWashJob::Hold(bool bOn)
{
	if (bOn == bHolding) return;
	bHolding = bOn;
	TArray<TWJ::FHeld>& H = TWJ::Held();
	const UWorld* World = WorldOf.Get();
	if (bOn) H.Add({World, Bay});
	else
	{
		for (int32 I = H.Num() - 1; I >= 0; --I)
			if (H[I].Bay == Bay && (H[I].World.Get() == World || !H[I].World.IsValid()))
			{
				H.RemoveAt(I);
				break;
			}
	}
	Relook();   // the parts' own grime off (the overlay draws it) or back on
}

bool FTorqueWashJob::Attach(UWorld* World)
{
	ATorqueShedBike* Bike = TWJ::BikeOf(World, Bay);
	if (!Bike) return false;
	TArray<UStaticMeshComponent*> Meshes;
	TWJ::MeshesOf(Bike, Meshes);
	if (Meshes.Num() == 0) return false;
	Model = Bike->GetActorTransform();
	Model.SetScale3D(FVector::OneVector);
	if (!DirtMaterial()) return true;   // no overlay to draw it: the shed's dirt shows, part by part
	bool bSame = Meshes.Num() == Shown.Num();
	for (int32 I = 0; bSame && I < Meshes.Num(); ++I) bSame = Shown[I].C.Get() == Meshes[I];
	if (!bSame)
	{
		// Redrawn (the shed changed): the muck goes on the new meshes, as far as the wash has got.
		for (const FShown& S : Shown)
			if (UStaticMeshComponent* C = S.C.Get())
				if (C->GetOverlayMaterial() == S.M.Get()) C->SetOverlayMaterial(nullptr);
		Shown.Reset();
		for (UStaticMeshComponent* C : Meshes)
		{
			const int32 Part = Slots.IndexOfByKey(TorqueShedModel::SlotTag(C));
			FShown S;
			S.C = C;
			S.Part = Part;
			if (Part != INDEX_NONE) S.M.Reset(MidFor(C, Part));
			Shown.Add(MoveTemp(S));
		}
		ApplyParams();
	}
	for (const FShown& S : Shown)
		if (UStaticMeshComponent* C = S.C.Get())
			if (S.M.IsValid() && C->GetOverlayMaterial() != S.M.Get()) C->SetOverlayMaterial(S.M.Get());
	Hold(true);
	return true;
}

void FTorqueWashJob::Detach()
{
	for (const FShown& S : Shown)
		if (UStaticMeshComponent* C = S.C.Get())
			if (S.M.IsValid() && C->GetOverlayMaterial() == S.M.Get()) C->SetOverlayMaterial(nullptr);
	Shown.Reset();
	Hold(false);
}

void FTorqueWashJob::Tick(double Dt)
{
	const double Want = bScanOn ? 1.0 : 0.0;
	ScanAmt = Want > ScanAmt ? FMath::Min(Want, ScanAmt + Dt * 3.0) : FMath::Max(Want, ScanAmt - Dt * 3.0);
	ScanT += Dt;
	for (float& F : Flash) F = FMath::Max(0.0f, F - (float)Dt * 1.2f);
	ApplyParams();
	SinceUpload += Dt;
	if (DirtyHi >= DirtyLo && SinceUpload >= 1.0 / 30.0) Upload(false);
}

int32 FTorqueWashJob::TestSweep(int32 Passes, double Distance, bool bUnder)
{
	if (Points.Num() == 0) return 0;
	FBox Box(ForceInit);
	for (const FVector& P : Points) Box += P;
	const FVector C = Box.GetCenter(), Ext = Box.GetExtent();
	// The sides looked from (model axes: x along the bike, y across, z up).
	TArray<FVector> Sides = {FVector(0, 1, 0), FVector(0, -1, 0), FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0.3, 0, 1).GetSafeNormal()};
	if (bUnder) Sides.Add(FVector(0.2, 0, -1).GetSafeNormal());
	const double RowStep = TW::JetAt(Distance).Radius;   // rows half overlapping
	const double Dt = 1.0 / 60.0;
	int32 Landed = 0;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
		for (const FVector& S : Sides)
		{
			FVector U, V;
			S.FindBestAxisVectors(U, V);
			const double Ru = FMath::Abs(FVector::DotProduct(Ext, U.GetAbs())) + RowStep, Rv = FMath::Abs(FVector::DotProduct(Ext, V.GetAbs())) + RowStep;
			const double Reach = FMath::Abs(FVector::DotProduct(Ext, S.GetAbs()));
			for (double B = -Rv; B <= Rv; B += RowStep)
				for (double A = -Ru; A <= Ru; A += RowStep * 0.5)   // a steady sweep along the row
				{
					const FVector Target = C + U * A + V * B;
					const FVector EyeM = Target + S * (Reach + Distance);
					const FVector EyeW = Model.TransformPosition(EyeM);
					const FVector DirW = Model.TransformVectorNoScale(-S);
					for (int32 K = 0; K < 2; ++K)
					{
						Step(Dt, EyeW, DirW, true, false, FVector::ZeroVector, FVector::UpVector);
						Landed += Jet.bBike ? 1 : 0;
					}
				}
		}
	return Landed;
}
