#include "Shed/TorqueShedPick.h"

#include "Shed/TorqueShedBike.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TorqueShedPickImpl
{
// A mesh's shape: the cells (mesh-local cm) its triangles go through.
struct FShape
{
	bool bValid = false;
	double Cell = 4.0;
	FVector Origin = FVector::ZeroVector;
	FBox Box = FBox(ForceInit);
	TSet<FIntVector> Cells;

	FIntVector CellOf(const FVector& P) const
	{
		return FIntVector(FMath::FloorToInt((P.X - Origin.X) / Cell), FMath::FloorToInt((P.Y - Origin.Y) / Cell), FMath::FloorToInt((P.Z - Origin.Z) / Cell));
	}
	FVector CentreOf(const FIntVector& I) const { return Origin + (FVector(I) + FVector(0.5)) * Cell; }
	void Mark(const FVector& P) { Cells.Add(CellOf(P)); }
};

uint32 ReadU32(const TArray<uint8>& B, int64 At)
{
	return At + 4 <= B.Num() ? (uint32)B[At] | ((uint32)B[At + 1] << 8) | ((uint32)B[At + 2] << 16) | ((uint32)B[At + 3] << 24) : 0;
}

float ReadF32(const TArray<uint8>& B, int64 At)
{
	const uint32 U = ReadU32(B, At);
	float F = 0.0f;
	FMemory::Memcpy(&F, &U, sizeof(F));
	return F;
}

// Where an accessor's data starts in the binary chunk, its stride, count and component type.
bool AccessorAt(const FJsonObject& Json, int32 Index, int64 BinStart, int64& OutAt, int64& OutStride, int32& OutCount, int32& OutType)
{
	const TArray<TSharedPtr<FJsonValue>>* Accessors = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
	if (!Json.TryGetArrayField(TEXT("accessors"), Accessors) || !Json.TryGetArrayField(TEXT("bufferViews"), Views)) return false;
	if (!Accessors->IsValidIndex(Index)) return false;
	const TSharedPtr<FJsonObject> A = (*Accessors)[Index]->AsObject();
	if (!A.IsValid()) return false;
	int32 ViewIndex = 0;
	if (!A->TryGetNumberField(TEXT("bufferView"), ViewIndex) || !Views->IsValidIndex(ViewIndex)) return false;
	const TSharedPtr<FJsonObject> V = (*Views)[ViewIndex]->AsObject();
	if (!V.IsValid()) return false;
	int64 ViewOffset = 0, AccOffset = 0, Stride = 0;
	V->TryGetNumberField(TEXT("byteOffset"), ViewOffset);
	A->TryGetNumberField(TEXT("byteOffset"), AccOffset);
	V->TryGetNumberField(TEXT("byteStride"), Stride);
	OutCount = 0;
	OutType = 0;
	A->TryGetNumberField(TEXT("count"), OutCount);
	A->TryGetNumberField(TEXT("componentType"), OutType);
	OutAt = BinStart + ViewOffset + AccOffset;
	OutStride = Stride;
	return true;
}

// The positions (metres, the file's frame) and triangles of every primitive in a .glb.
bool ReadGlb(const FString& Path, TArray<FVector>& OutPos, TArray<int32>& OutTris)
{
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *Path, FILEREAD_Silent) || B.Num() < 28) return false;
	if (ReadU32(B, 0) != 0x46546C67u) return false;   // "glTF"
	const uint32 JsonLen = ReadU32(B, 12);
	if (ReadU32(B, 16) != 0x4E4F534Au || 20 + (int64)JsonLen + 8 > B.Num()) return false;   // "JSON"
	const int64 BinHeader = 20 + (int64)JsonLen;
	if (ReadU32(B, BinHeader + 4) != 0x004E4942u) return false;   // "BIN\0"
	const int64 BinStart = BinHeader + 8;
	FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(B.GetData() + 20), (int32)JsonLen);
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Json) || !Json.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* Meshes = nullptr;
	if (!Json->TryGetArrayField(TEXT("meshes"), Meshes)) return false;
	for (const TSharedPtr<FJsonValue>& MeshValue : *Meshes)
	{
		const TSharedPtr<FJsonObject> MeshObj = MeshValue->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Prims = nullptr;
		if (!MeshObj.IsValid() || !MeshObj->TryGetArrayField(TEXT("primitives"), Prims)) continue;
		for (const TSharedPtr<FJsonValue>& PrimValue : *Prims)
		{
			const TSharedPtr<FJsonObject> Prim = PrimValue->AsObject();
			const TSharedPtr<FJsonObject>* Attributes = nullptr;
			int32 PosIndex = -1;
			if (!Prim.IsValid() || !Prim->TryGetObjectField(TEXT("attributes"), Attributes) || !(*Attributes)->TryGetNumberField(TEXT("POSITION"), PosIndex)) continue;
			int64 At = 0, Stride = 0;
			int32 Count = 0, Type = 0;
			if (!AccessorAt(*Json, PosIndex, BinStart, At, Stride, Count, Type) || Type != 5126) continue;
			if (Stride == 0) Stride = 12;
			if (At + Stride * (int64)FMath::Max(Count - 1, 0) + 12 > B.Num()) continue;
			const int32 Base = OutPos.Num();
			for (int32 I = 0; I < Count; ++I)
			{
				const int64 P = At + Stride * I;
				OutPos.Add(FVector(ReadF32(B, P), ReadF32(B, P + 4), ReadF32(B, P + 8)));
			}
			int32 IndicesIndex = -1;
			if (Prim->TryGetNumberField(TEXT("indices"), IndicesIndex))
			{
				int64 IAt = 0, IStride = 0;
				int32 ICount = 0, IType = 0;
				if (!AccessorAt(*Json, IndicesIndex, BinStart, IAt, IStride, ICount, IType)) continue;
				const int32 Size = IType == 5121 ? 1 : (IType == 5123 ? 2 : 4);
				if (IStride == 0) IStride = Size;
				if (IAt + IStride * (int64)FMath::Max(ICount - 1, 0) + Size > B.Num()) continue;
				for (int32 I = 0; I + 2 < ICount; I += 3)
					for (int32 K = 0; K < 3; ++K)
					{
						const int64 P = IAt + IStride * (I + K);
						const int32 V = Size == 1 ? B[P] : (Size == 2 ? (int32)(B[P] | (B[P + 1] << 8)) : (int32)ReadU32(B, P));
						OutTris.Add(Base + FMath::Clamp(V, 0, Count - 1));
					}
			}
			else
				for (int32 I = 0; I + 2 < Count; I += 3)
					for (int32 K = 0; K < 3; ++K) OutTris.Add(Base + I + K);
		}
	}
	return OutPos.Num() > 0 && OutTris.Num() >= 3;
}

// The model's frame (metres, x forward, y up, z right) to bike-local cm, as TorqueShedBike draws it.
FVector ModelToLocal(const FVector& P) { return FVector(P.X * 100.0, P.Z * 100.0, P.Y * 100.0); }

// Drops triangles (mesh-local cm) into the shape's cells.
void Fill(FShape& S, const TArray<FVector>& Pos, const TArray<int32>& Tris)
{
	FBox Box(ForceInit);
	for (const FVector& P : Pos) Box += P;
	if (!Box.IsValid) return;
	S.Box = Box;
	S.Cell = FMath::Clamp(Box.GetSize().GetMax() / 48.0, 1.5, 4.0);
	S.Origin = Box.Min - FVector(S.Cell);
	// Every triangle, sampled finer than a cell.
	for (int32 T = 0; T + 2 < Tris.Num(); T += 3)
	{
		if (!Pos.IsValidIndex(Tris[T]) || !Pos.IsValidIndex(Tris[T + 1]) || !Pos.IsValidIndex(Tris[T + 2])) continue;
		const FVector A = Pos[Tris[T]], B = Pos[Tris[T + 1]], C = Pos[Tris[T + 2]];
		const double Edge = FMath::Max3((B - A).Size(), (C - B).Size(), (A - C).Size());
		const int32 N = FMath::Clamp(FMath::CeilToInt(Edge / (S.Cell * 0.5)), 1, 200);
		for (int32 I = 0; I <= N; ++I)
			for (int32 J = 0; J <= N - I; ++J)
			{
				const double U = (double)I / N, V = (double)J / N;
				S.Mark(A + (B - A) * U + (C - A) * V);
			}
	}
	S.bValid = S.Cells.Num() > 0;
}

// Shapes of meshes built at run time, by the mesh (gone meshes are dropped as new ones come).
TMap<TWeakObjectPtr<const UStaticMesh>, TSharedPtr<FShape>>& RuntimeShapes()
{
	static TMap<TWeakObjectPtr<const UStaticMesh>, TSharedPtr<FShape>> Shapes;
	return Shapes;
}

TSharedPtr<FShape> MakeShape(const UStaticMesh* Mesh)
{
	TSharedPtr<FShape> S = MakeShared<FShape>();
	const FString Name = Mesh->GetName();
	const FString Folder = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Bike/SpeedTwin"));
	TArray<FVector> Pos;
	TArray<int32> Tris;
	if (!ReadGlb(FPaths::Combine(Folder, Name + TEXT(".glb")), Pos, Tris) &&
		!ReadGlb(FPaths::Combine(Folder, TEXT("Options"), Name + TEXT(".glb")), Pos, Tris))
		return S;
	// The importer's frame: the mesh's own cm are the bike's turned back by the frame fix.
	FTransform Fix;
	TMap<FName, FVector> Origins;
	if (!TorqueShedModel::LoadFrame(Fix, Origins)) return S;
	FBox Box(ForceInit);
	for (FVector& P : Pos)
	{
		P = Fix.InverseTransformPosition(ModelToLocal(P));
		Box += P;
	}
	// It must be the mesh the level script imported: the same box.
	const FBox Want = Mesh->GetBoundingBox();
	const double Err = (Box.Min - Want.Min).GetAbsMax() + (Box.Max - Want.Max).GetAbsMax();
	if (Err > 2.0 + 0.02 * Want.GetSize().GetMax())
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: %s.glb doesn't match its mesh (%.1f cm off): pointing at its box"), *Name, Err);
		return S;
	}
	Fill(*S, Pos, Tris);
	return S;
}

const FShape* ShapeOf(const UStaticMeshComponent* C)
{
	const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
	if (!Mesh) return nullptr;
	if (const TSharedPtr<FShape>* Built = RuntimeShapes().Find(TWeakObjectPtr<const UStaticMesh>(Mesh)))
		return (*Built)->bValid ? Built->Get() : nullptr;
	static TMap<FString, TSharedPtr<FShape>> Cache;
	const FString Key = Mesh->GetPathName();
	if (const TSharedPtr<FShape>* Found = Cache.Find(Key)) return (*Found)->bValid ? Found->Get() : nullptr;
	// Only the bike's own meshes have a .glb (/Game/Bike/...): not the engine's cubes and cylinders.
	TSharedPtr<FShape> S = Key.StartsWith(TEXT("/Game/Bike/")) ? MakeShape(Mesh) : MakeShared<FShape>();
	Cache.Add(Key, S);
	return S->bValid ? S.Get() : nullptr;
}
} // namespace TorqueShedPickImpl

namespace TSPI = TorqueShedPickImpl;

namespace TorqueShedPick
{
void AddShape(const UStaticMesh* Mesh, const TArray<FVector>& Pos, const TArray<int32>& Tris)
{
	if (!Mesh) return;
	TMap<TWeakObjectPtr<const UStaticMesh>, TSharedPtr<TSPI::FShape>>& Shapes = TSPI::RuntimeShapes();
	for (auto It = Shapes.CreateIterator(); It; ++It)
		if (!It->Key.IsValid()) It.RemoveCurrent();
	TSharedPtr<TSPI::FShape> S = MakeShared<TSPI::FShape>();
	TSPI::Fill(*S, Pos, Tris);
	Shapes.Add(TWeakObjectPtr<const UStaticMesh>(Mesh), S);
}

EHit Trace(const UStaticMeshComponent* C, const FVector& Start, const FVector& End, double& OutTime)
{
	const TSPI::FShape* S = TSPI::ShapeOf(C);
	if (!S) return EHit::NoShape;
	const FTransform& T = C->GetComponentTransform();
	const FVector A = T.InverseTransformPosition(Start), B = T.InverseTransformPosition(End);
	// Through the shape's box, a half cell at a time.
	FVector HitAt, HitNormal;
	float Enter = 0.0f;
	const FBox Box = S->Box.ExpandBy(S->Cell);
	if (!Box.IsInside(A) && !FMath::LineExtentBoxIntersection(Box, A, B, FVector::ZeroVector, HitAt, HitNormal, Enter)) return EHit::Miss;
	const double Length = (B - A).Size();
	if (Length < KINDA_SMALL_NUMBER) return EHit::Miss;
	const double Step = S->Cell * 0.5 / Length;
	for (double U = Enter; U <= 1.0; U += Step)
	{
		const FVector P = A + (B - A) * U;
		if (!Box.IsInside(P) && U > Enter + Step) break;   // out the far side
		if (S->Cells.Contains(S->CellOf(P)))
		{
			OutTime = U;
			return EHit::Hit;
		}
	}
	return EHit::Miss;
}

bool SurfacePoint(const UStaticMeshComponent* C, const FVector& Side, FVector& Out)
{
	const TSPI::FShape* S = TSPI::ShapeOf(C);
	if (!S) return false;
	const FTransform& T = C->GetComponentTransform();
	// Of the cells nearest the shape's middle height, the one furthest along Side.
	const FVector Mid = T.TransformPosition(S->Box.GetCenter());
	double Best = -TNumericLimits<double>::Max();
	for (const FIntVector& I : S->Cells)
	{
		const FVector P = T.TransformPosition(S->CentreOf(I));
		const double Score = FVector::DotProduct(P - Mid, Side) - 0.5 * FMath::Abs(P.Z - Mid.Z);
		if (Score > Best)
		{
			Best = Score;
			Out = P;
		}
	}
	return Best > -TNumericLimits<double>::Max();
}
}
