#include "Shed/TorqueFluidModels.h"

#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueToolModels.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace TorqueFluidModelsImpl
{
// --- Geometry ---------------------------------------------------------------------------------------------
// Positions and normals in cm (x the vessel's front, y across, z up), UVs, and a label mask per vertex
// (the shell material's printed label where it's 1).
struct FGeo
{
	TArray<FVector> P, N;
	TArray<FVector2f> UV;
	TArray<float> Label;
	TArray<int32> T;
	int32 Add(const FVector& Pos, const FVector& Nrm, const FVector2f& Uv, float L = 0.0f)
	{
		P.Add(Pos);
		N.Add(Nrm);
		UV.Add(Uv);
		Label.Add(L);
		return P.Num() - 1;
	}
	void Tri(int32 A, int32 B, int32 C) { T.Add(A); T.Add(B); T.Add(C); }
};

// One cross-section of a lofted shape: a superellipse (Exp 2 an ellipse, 4-6 a rounded rectangle)
// of half sizes A (along x) and B (along y), at height Z, its centre at Off.
struct FRing
{
	double Z = 0.0, A = 1.0, B = 1.0, Exp = 2.0;
	FVector2D Off = FVector2D::ZeroVector;
};

FVector2D SuperPoint(const FRing& R, double T)
{
	const double C = FMath::Cos(T), S = FMath::Sin(T);
	const double E = 2.0 / FMath::Max(R.Exp, 1.0);
	return R.Off + FVector2D(R.A * FMath::Sign(C) * FMath::Pow(FMath::Abs(C), E), R.B * FMath::Sign(S) * FMath::Pow(FMath::Abs(S), E));
}

// The area of a ring's cross-section (cm^2): 4AB Gamma(1 + 1/n)^2 / Gamma(1 + 2/n), by sampling.
double RingArea(const FRing& R)
{
	const int32 K = 96;
	double Area = 0.0;
	FVector2D Prev = SuperPoint(R, 0.0) - R.Off;
	for (int32 I = 1; I <= K; ++I)
	{
		const FVector2D Q = SuperPoint(R, 2.0 * PI * I / K) - R.Off;
		Area += 0.5 * (Prev.X * Q.Y - Q.X * Prev.Y);
		Prev = Q;
	}
	return FMath::Abs(Area);
}

// A lofted surface through Rings (bottom first), Seg segments round; closed at the bottom and top with
// flat caps if asked. Normals point out (in, if bInside: the inside of an open pan). LabelOf gives a
// vertex's label mask from where it is.
void Loft(FGeo& G, const TArray<FRing>& Rings, int32 Seg, bool bInside, bool bCapBottom, bool bCapTop,
	TFunctionRef<float(const FVector&)> LabelOf)
{
	const int32 NR = Rings.Num();
	if (NR < 2) return;
	const int32 Base = G.P.Num();
	TArray<FVector> Grid;
	Grid.SetNum(NR * Seg);
	for (int32 I = 0; I < NR; ++I)
		for (int32 J = 0; J < Seg; ++J)
		{
			const FVector2D Q = SuperPoint(Rings[I], 2.0 * PI * J / Seg);
			Grid[I * Seg + J] = FVector(Q.X, Q.Y, Rings[I].Z);
		}
	auto At = [&Grid, Seg](int32 I, int32 J) { return Grid[I * Seg + ((J % Seg) + Seg) % Seg]; };
	double Height = FMath::Max(Rings.Last().Z - Rings[0].Z, 1.0);
	for (int32 I = 0; I < NR; ++I)
		for (int32 J = 0; J <= Seg; ++J)   // (the seam twice, for the UVs)
		{
			const FVector Pos = At(I, J);
			const FVector Du = At(I, J + 1) - At(I, J - 1);
			const FVector Dv = At(FMath::Min(I + 1, NR - 1), J) - At(FMath::Max(I - 1, 0), J);
			FVector Nrm = FVector::CrossProduct(Du, Dv).GetSafeNormal();
			if (Nrm.IsNearlyZero()) Nrm = FVector(0.0, 0.0, I == 0 ? -1.0 : 1.0);
			// Out from the ring's middle (a degenerate ring's cross product can come out either way).
			const FVector Mid(Rings[I].Off.X, Rings[I].Off.Y, Pos.Z);
			if (FVector::DotProduct(Nrm, Pos - Mid) < 0.0 && (Pos - Mid).SizeSquared() > 0.01) Nrm = -Nrm;
			if (bInside) Nrm = -Nrm;
			G.Add(Pos, Nrm, FVector2f((float)J / Seg, (float)((Pos.Z - Rings[0].Z) / Height)), LabelOf(Pos));
		}
	const int32 Row = Seg + 1;
	for (int32 I = 0; I + 1 < NR; ++I)
		for (int32 J = 0; J < Seg; ++J)
		{
			const int32 A = Base + I * Row + J, B = A + 1, C = A + Row, D = C + 1;
			G.Tri(A, B, D);
			G.Tri(A, D, C);
		}
	auto Cap = [&](int32 I, double Up)
	{
		const FVector Nrm(0.0, 0.0, bInside ? -Up : Up);
		const FRing& R = Rings[I];
		const int32 Mid = G.Add(FVector(R.Off.X, R.Off.Y, R.Z), Nrm, FVector2f(0.5f, 0.5f), LabelOf(FVector(R.Off.X, R.Off.Y, R.Z)));
		const int32 First = G.P.Num();
		for (int32 J = 0; J < Seg; ++J)
		{
			const FVector Pos = At(I, J);
			G.Add(Pos, Nrm, FVector2f(0.5f + (float)(Pos.X - R.Off.X) * 0.02f, 0.5f + (float)(Pos.Y - R.Off.Y) * 0.02f), LabelOf(Pos));
		}
		for (int32 J = 0; J < Seg; ++J) G.Tri(Mid, First + J, First + (J + 1) % Seg);
	};
	if (bCapBottom) Cap(0, -1.0);
	if (bCapTop) Cap(NR - 1, 1.0);
}

void Loft(FGeo& G, const TArray<FRing>& Rings, int32 Seg, bool bInside = false, bool bCapBottom = true, bool bCapTop = true)
{
	Loft(G, Rings, Seg, bInside, bCapBottom, bCapTop, [](const FVector&) { return 0.0f; });
}

// A round tube of Radius along Points (a handle, a bleed tube), its ends capped.
void Tube(FGeo& G, const TArray<FVector>& Points, double Radius, int32 Sides = 12)
{
	const int32 NP = Points.Num();
	if (NP < 2) return;
	const int32 Base = G.P.Num();
	// Frames carried along the line (parallel transport), so the tube doesn't twist.
	FVector Along = (Points[1] - Points[0]).GetSafeNormal();
	FVector Side = FVector::CrossProduct(Along, FMath::Abs(Along.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	for (int32 I = 0; I < NP; ++I)
	{
		const FVector Next = I + 1 < NP ? Points[I + 1] - Points[I] : Points[I] - Points[I - 1];
		const FVector Prev = I > 0 ? Points[I] - Points[I - 1] : Next;
		const FVector Dir = (Next.GetSafeNormal() + Prev.GetSafeNormal()).GetSafeNormal();
		Side = (Side - Dir * FVector::DotProduct(Side, Dir)).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Dir, Side);
		for (int32 K = 0; K <= Sides; ++K)
		{
			const double A = 2.0 * PI * K / Sides;
			const FVector Nrm = Side * FMath::Cos(A) + Up * FMath::Sin(A);
			G.Add(Points[I] + Nrm * Radius, Nrm, FVector2f((float)K / Sides, (float)I / (NP - 1)));
		}
	}
	const int32 Row = Sides + 1;
	for (int32 I = 0; I + 1 < NP; ++I)
		for (int32 K = 0; K < Sides; ++K)
		{
			const int32 A = Base + I * Row + K, B = A + 1, C = A + Row, D = C + 1;
			G.Tri(A, B, D);
			G.Tri(A, D, C);
		}
	for (int32 End = 0; End < 2; ++End)
	{
		const int32 I = End == 0 ? 0 : NP - 1;
		const FVector Dir = (End == 0 ? Points[0] - Points[1] : Points[NP - 1] - Points[NP - 2]).GetSafeNormal();
		const int32 Mid = G.Add(Points[I], Dir, FVector2f(0.5f, 0.5f));
		const int32 First = G.P.Num();
		for (int32 K = 0; K < Sides; ++K) G.Add(G.P[Base + I * Row + K], Dir, FVector2f(0.0f, 0.0f));
		for (int32 K = 0; K < Sides; ++K) G.Tri(Mid, First + K, First + (K + 1) % Sides);
	}
}

// A plain cylinder (a cap, a neck, a spout) standing on Bottom, Height tall.
void Cylinder(FGeo& G, const FVector& Bottom, double Radius, double Height, int32 Seg = 24, double TopRadius = -1.0)
{
	TArray<FRing> R;
	FRing A;
	A.Z = Bottom.Z;
	A.A = A.B = Radius;
	A.Off = FVector2D(Bottom.X, Bottom.Y);
	FRing B = A;
	B.Z = Bottom.Z + Height;
	if (TopRadius > 0.0) B.A = B.B = TopRadius;
	// A small round on the edges, so they catch the light.
	FRing A1 = A, B1 = B;
	A1.Z += FMath::Min(0.15, Height * 0.1);
	B1.Z -= FMath::Min(0.15, Height * 0.1);
	FRing A0 = A, B2 = B;
	A0.A = A0.B = Radius - 0.12;
	B2.A = B2.B = B.A - 0.12;
	R = {A0, A1, B1, B2};
	Loft(G, R, Seg);
}

// A body with a rounded foot: Foot rings up from the floor round a bevel of radius Bevel, the given
// rings above it.
TArray<FRing> Footed(double A, double B, double Exp, double Bevel, const TArray<FRing>& Above, double Z0 = 0.0)
{
	TArray<FRing> Out;
	for (int32 K = 0; K <= 3; ++K)
	{
		const double Phi = 0.5 * PI * K / 3.0;
		FRing R;
		R.Z = Z0 + Bevel * (1.0 - FMath::Cos(Phi));
		const double In = Bevel * (1.0 - FMath::Sin(Phi));
		R.A = A - In;
		R.B = B - In;
		R.Exp = Exp;
		Out.Add(R);
	}
	Out.Append(Above);
	return Out;
}

// The same rings shrunk by a wall's thickness (the liquid inside them).
TArray<FRing> Inset(const TArray<FRing>& Rings, double Wall, double Bottom, double Top)
{
	TArray<FRing> Out;
	for (const FRing& R0 : Rings)
	{
		if (R0.Z < Bottom - 0.01 || R0.Z > Top + 0.01) continue;
		FRing R = R0;
		R.A = FMath::Max(R.A - Wall, 0.2);
		R.B = FMath::Max(R.B - Wall, 0.2);
		Out.Add(R);
	}
	if (Out.Num() > 0 && Out[0].Z > Bottom + 0.01) { FRing R = Out[0]; R.Z = Bottom; Out.Insert(R, 0); }
	if (Out.Num() > 0 && Out.Last().Z < Top - 0.01) { FRing R = Out.Last(); R.Z = Top; Out.Add(R); }
	return Out;
}

// --- Meshes and materials ---------------------------------------------------------------------------------

UStaticMesh* BuildMesh(const FGeo& G, UMaterialInterface* Material)
{
	if (G.T.Num() < 3) return nullptr;
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(2);
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	const FName SlotName(TEXT("Fluid"));
	SlotNames[Group] = SlotName;
	TArray<FVertexInstanceID> Instances;
	Instances.Reserve(G.P.Num());
	for (int32 I = 0; I < G.P.Num(); ++I)
	{
		const FVertexID V = MD.CreateVertex();
		Positions[V] = FVector3f(G.P[I]);
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		const FVector3f Nrm = FVector3f(G.N[I]).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Normals[VI] = Nrm;
		const FVector3f Across = FMath::Abs(Nrm.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		Tangents[VI] = FVector3f::CrossProduct(Across, Nrm).GetSafeNormal();
		Signs[VI] = 1.0f;
		const float L = G.Label.IsValidIndex(I) ? G.Label[I] : 0.0f;
		Colours[VI] = FVector4f(L, 0.0f, 0.0f, 1.0f);
		UVs.Set(VI, 0, G.UV[I]);
		UVs.Set(VI, 1, FVector2f(0.0f, 0.0f));
		Instances.Add(VI);
	}
	for (int32 T = 0; T + 2 < G.T.Num(); T += 3)
	{
		const int32 A = G.T[T];
		int32 B = G.T[T + 1], C = G.T[T + 2];
		if (A == B || B == C || A == C) continue;
		// Unreal's front face is -(P1 - P0) x (P2 - P0): turn each triangle to face the way its normals do.
		const FVector Face = -FVector::CrossProduct(G.P[B] - G.P[A], G.P[C] - G.P[A]);
		if (Face.SizeSquared() < 1e-10) continue;
		if (FVector::DotProduct(Face, G.N[A] + G.N[B] + G.N[C]) < 0.0) Swap(B, C);
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Instances[A]);
		Tri.Add(Instances[B]);
		Tri.Add(Instances[C]);
		MD.CreateTriangle(Group, Tri);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName));
#endif
	Mesh->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	Mesh->AddToRoot();   // kept for every can drawn after, across map travel
	return Mesh;
}

UMaterialInterface* Master(const TCHAR* Name)
{
	static TMap<FString, UMaterialInterface*> Found;
	if (UMaterialInterface** M = Found.Find(Name)) return *M;
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Shed/Materials/%s.%s"), Name, Name),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (M) M->AddToRoot();
	else UE_LOG(LogTemp, Warning, TEXT("Torque fluids: no %s yet (Tools/ue_build_fluids.py): the cans are drawn opaque"), Name);
	Found.Add(Name, M);
	return M;
}

// How a can is drawn: what kind of vessel, and its shell's look.
enum class EShape : uint8 { JerryCan, Jug, OilBottle, BrakeBottle, BleedBottle, Aerosol, DrainPan };

struct FBuilt
{
	bool bValid = false;
	EShape Shape = EShape::JerryCan;
	UStaticMesh* Shell = nullptr;     // the see-through (or, for the pan and the aerosol, solid) body
	UStaticMesh* Caps = nullptr;      // the caps, in their own solid colour
	UStaticMesh* Trim = nullptr;      // the aerosol's dome and nozzle, the bleed bottle's tube
	UStaticMesh* Liquid = nullptr;
	TArray<float> HeightAt;
	FVector2D Axis = FVector2D::ZeroVector;
	double Freq = 1.6;
};

// The shapes, sized from what they hold (a little more room than the fluid: the air at the top and
// the plastic): x their front, y across, z up, from the floor.
void JerryCan(double Litres, FGeo& Shell, FGeo& Caps, FGeo& Liquid, TArray<FRing>& Inner)
{
	// L:W:H 0.85:0.42:1, a plastic petrol can: flat sides, a bridge handle on top, the spout at the front.
	const double Vol = Litres * 1000.0 * 1.35;
	const double H = FMath::Pow(Vol / (0.85 * 0.42), 1.0 / 3.0), A = 0.85 * H * 0.5, B = 0.42 * H * 0.5;
	TArray<FRing> Up;
	auto Ring = [](double Z, double RA, double RB, double Exp, double OffX = 0.0) { FRing R; R.Z = Z; R.A = RA; R.B = RB; R.Exp = Exp; R.Off = FVector2D(OffX, 0.0); return R; };
	Up.Add(Ring(H * 0.5, A, B, 5.0));
	Up.Add(Ring(H - 3.0, A, B, 5.0));
	Up.Add(Ring(H - 1.3, A - 0.6, B - 0.6, 5.0));
	Up.Add(Ring(H - 0.4, A - 1.6, B - 1.4, 5.0));
	Up.Add(Ring(H, A - 2.8, B - 2.3, 5.0));
	const TArray<FRing> Body = Footed(A, B, 5.0, 1.4, Up);
	Loft(Shell, Body, 64);
	// The bridge handle over the top, towards the back, and the spout's collar at the front corner.
	TArray<FVector> Handle;
	const double HX0 = -A * 0.62, HX1 = A * 0.05, Rise = FMath::Max(3.2, H * 0.12);
	for (int32 K = 0; K <= 10; ++K)
	{
		const double U = K / 10.0;
		const double X = FMath::Lerp(HX0, HX1, U);
		const double Lift = FMath::Sin(PI * U);
		Handle.Add(FVector(X, 0.0, H - 0.6 + Rise * FMath::Pow(Lift, 0.45)));
	}
	Tube(Shell, Handle, FMath::Max(0.9, H * 0.032));
	const FVector Spout(A * 0.58, 0.0, H - 1.2);
	Cylinder(Shell, Spout, FMath::Max(1.6, H * 0.055), 2.6);
	Cylinder(Caps, Spout + FVector(0.0, 0.0, 2.4), FMath::Max(1.9, H * 0.065), 2.2);
	Inner = Inset(Body, 0.3, 0.3, H - 0.8);
	Loft(Liquid, Inner, 64);
}

void Jug(double Litres, FGeo& Shell, FGeo& Caps, FGeo& Liquid, TArray<FRing>& Inner)
{
	// A square race fuel jug (L:W:H 0.7:0.7:1), its neck at the front, a handle moulded in at the back.
	const double Vol = Litres * 1000.0 * 1.3;
	const double H = FMath::Pow(Vol / (0.7 * 0.7), 1.0 / 3.0), A = 0.35 * H, B = 0.35 * H;
	const double NeckX = A * 0.45, NeckR = FMath::Max(2.0, H * 0.06);
	auto Ring = [](double Z, double RA, double RB, double Exp, double OffX) { FRing R; R.Z = Z; R.A = RA; R.B = RB; R.Exp = Exp; R.Off = FVector2D(OffX, 0.0); return R; };
	TArray<FRing> Up;
	Up.Add(Ring(H * 0.6, A, B, 3.5, 0.0));
	Up.Add(Ring(H * 0.78, A, B, 3.5, 0.0));
	Up.Add(Ring(H * 0.86, A * 0.82, B * 0.82, 3.2, NeckX * 0.2));
	Up.Add(Ring(H * 0.93, A * 0.5, B * 0.5, 2.6, NeckX * 0.6));
	Up.Add(Ring(H * 0.97, NeckR + 0.6, NeckR + 0.6, 2.0, NeckX));
	Up.Add(Ring(H, NeckR, NeckR, 2.0, NeckX));
	const TArray<FRing> Body = Footed(A, B, 3.5, 1.5, Up);
	Loft(Shell, Body, 64);
	TArray<FVector> Handle;
	for (int32 K = 0; K <= 10; ++K)
	{
		const double U = K / 10.0;
		const double Ang = PI * U;
		Handle.Add(FVector(-A * 0.55 - FMath::Sin(Ang) * H * 0.12, 0.0, H * 0.62 + (H * 0.28) * U));
	}
	Tube(Shell, Handle, FMath::Max(1.0, H * 0.03));
	Cylinder(Caps, FVector(NeckX, 0.0, H - 0.2), NeckR + 0.35, 2.4);
	Inner = Inset(Body, 0.3, 0.3, H * 0.86);
	Loft(Liquid, Inner, 64);
}

void OilBottle(double Litres, FGeo& Shell, FGeo& Caps, FGeo& Liquid, TArray<FRing>& Inner)
{
	// L:W:H 0.68:0.42:1: a label round the middle with a clear strip down its left side marked with the
	// level, a shoulder rising to the neck at the front, and the handle moulded into the back.
	const double Vol = Litres * 1000.0 * 1.35;
	const double H = FMath::Pow(Vol / (0.68 * 0.42), 1.0 / 3.0), A = 0.34 * H, B = 0.21 * H;
	const double NeckX = A * 0.5, NeckR = FMath::Max(1.6, H * 0.065);
	auto Ring = [](double Z, double RA, double RB, double Exp, double OffX) { FRing R; R.Z = Z; R.A = RA; R.B = RB; R.Exp = Exp; R.Off = FVector2D(OffX, 0.0); return R; };
	TArray<FRing> Up;
	Up.Add(Ring(H * 0.5, A, B, 4.5, 0.0));
	Up.Add(Ring(H * 0.74, A, B, 4.5, 0.0));
	Up.Add(Ring(H * 0.82, A * 0.86, B * 0.9, 4.0, NeckX * 0.15));
	Up.Add(Ring(H * 0.9, A * 0.55, B * 0.7, 3.0, NeckX * 0.5));
	Up.Add(Ring(H * 0.955, NeckR + 0.8, NeckR + 0.6, 2.2, NeckX * 0.92));
	Up.Add(Ring(H, NeckR, NeckR, 2.0, NeckX));
	const TArray<FRing> Body = Footed(A, B, 4.5, 1.2, Up);
	const double LabelLo = H * 0.12, LabelHi = H * 0.68;
	Loft(Shell, Body, 72, false, true, true, [=](const FVector& P)
	{
		if (P.Z < LabelLo || P.Z > LabelHi) return 0.0f;
		// The clear strip: down the left side (-y), a fifth of the bottle's length wide.
		if (P.Y < 0.0 && FMath::Abs(P.X) < A * 0.22) return 0.0f;
		return 1.0f;
	});
	TArray<FVector> Handle;
	for (int32 K = 0; K <= 12; ++K)
	{
		const double U = K / 12.0;
		Handle.Add(FVector(-A + 0.6 - FMath::Sin(PI * U) * H * 0.11, 0.0, H * 0.5 + H * 0.3 * U));
	}
	Tube(Shell, Handle, FMath::Max(1.0, H * 0.034));
	Cylinder(Caps, FVector(NeckX, 0.0, H - 0.3), NeckR + 0.35, 2.4);
	Inner = Inset(Body, 0.25, 0.25, H * 0.82);
	Loft(Liquid, Inner, 72);
}

void RoundBottle(double Litres, double Slim, FGeo& Shell, FGeo& Caps, FGeo& Liquid, TArray<FRing>& Inner, bool bLabel)
{
	// A round bottle (brake fluid, the bleed bottle): H = Slim x its radius.
	const double Vol = Litres * 1000.0 * 1.4;
	const double R = FMath::Pow(Vol / (PI * Slim), 1.0 / 3.0), H = R * Slim;
	const double NeckR = FMath::Max(1.3, R * 0.38);
	auto Ring = [](double Z, double Rad) { FRing Q; Q.Z = Z; Q.A = Q.B = Rad; Q.Exp = 2.0; return Q; };
	TArray<FRing> Up;
	Up.Add(Ring(H * 0.5, R));
	Up.Add(Ring(H * 0.8, R));
	Up.Add(Ring(H * 0.88, R * 0.86));
	Up.Add(Ring(H * 0.94, R * 0.55));
	Up.Add(Ring(H * 0.97, NeckR + 0.2));
	Up.Add(Ring(H, NeckR));
	const TArray<FRing> Body = Footed(R, R, 2.0, FMath::Min(1.0, R * 0.25), Up);
	const double LabelLo = H * 0.18, LabelHi = H * 0.72;
	Loft(Shell, Body, 48, false, true, true, [=](const FVector& P)
	{
		if (!bLabel || P.Z < LabelLo || P.Z > LabelHi) return 0.0f;
		return P.Y < 0.0 && FMath::Abs(P.X) < R * 0.4 ? 0.0f : 1.0f;   // a clear window to the left
	});
	Cylinder(Caps, FVector(0.0, 0.0, H - 0.2), NeckR + 0.3, FMath::Max(1.8, R * 0.55));
	Inner = Inset(Body, 0.2, 0.2, H * 0.88);
	Loft(Liquid, Inner, 48);
}

void Aerosol(double Litres, FGeo& Shell, FGeo& Trim)
{
	// A steel aerosol: the can in its colour, a domed top, the white actuator.
	const double R = 3.3, H = Litres >= 0.35 ? 19.0 : 15.0;
	auto Ring = [](double Z, double Rad) { FRing Q; Q.Z = Z; Q.A = Q.B = Rad; Q.Exp = 2.0; return Q; };
	TArray<FRing> Body;
	Body.Add(Ring(0.0, R - 0.35));
	Body.Add(Ring(0.25, R - 0.05));
	Body.Add(Ring(0.6, R));
	Body.Add(Ring(H - 0.6, R));
	Body.Add(Ring(H - 0.3, R - 0.15));
	Body.Add(Ring(H, R - 0.3));
	Loft(Shell, Body, 40);
	TArray<FRing> Dome;
	for (int32 K = 0; K <= 5; ++K)
	{
		const double Phi = 0.5 * PI * K / 5.0;
		Dome.Add(Ring(H + 1.6 * FMath::Sin(Phi), (R - 0.3) * FMath::Cos(Phi) + 0.6 * (1.0 - FMath::Cos(Phi))));
	}
	Loft(Trim, Dome, 40);
	Cylinder(Trim, FVector(0.0, 0.0, H + 1.4), 1.1, 1.6, 20, 1.0);
}

void DrainPan(double Litres, FGeo& Shell, FGeo& Liquid, TArray<FRing>& Inner)
{
	// A low oval pan with sloping sides, a rolled rim and a pouring lip at the front: open at the top,
	// so it's drawn inside and out.
	const double H = 8.5;
	const double A1 = 24.0, B1 = 18.5, A0 = 21.5, B0 = 16.0, Exp = 2.4, Wall = 0.45;
	auto Ring = [Exp](double Z, double RA, double RB) { FRing Q; Q.Z = Z; Q.A = RA; Q.B = RB; Q.Exp = Exp; return Q; };
	TArray<FRing> Up;
	Up.Add(Ring(H * 0.5, (A0 + A1) * 0.5, (B0 + B1) * 0.5));
	Up.Add(Ring(H, A1, B1));
	const TArray<FRing> Outside = Footed(A0, B0, Exp, 1.0, Up);
	Loft(Shell, Outside, 64, false, true, false);
	TArray<FRing> In;
	for (const FRing& R : Outside)
	{
		if (R.Z < 0.4) continue;
		FRing Q = R;
		Q.A -= Wall;
		Q.B -= Wall;
		Q.Z = FMath::Max(R.Z, Wall);
		In.Add(Q);
	}
	Loft(Shell, In, 64, true, true, false);
	// The rolled rim round the top, joining the two walls.
	TArray<FVector> Rim;
	for (int32 J = 0; J <= 64; ++J)
	{
		FRing Q = Ring(H + 0.15, A1 - Wall * 0.5, B1 - Wall * 0.5);
		const FVector2D P = SuperPoint(Q, 2.0 * PI * J / 64.0);
		Rim.Add(FVector(P.X, P.Y, Q.Z));
	}
	Tube(Shell, Rim, 0.55, 10);
	Inner = Inset(Outside, Wall + 0.05, Wall + 0.05, H - 0.3);
	Loft(Liquid, Inner, 64);
}

// The fill to surface height curve of a lofted inside (equal volumes between samples).
void FillCurve(const TArray<FRing>& Inner, TArray<float>& Out)
{
	Out.Reset();
	if (Inner.Num() < 2) return;
	TArray<double> Z, V;
	double Sum = 0.0;
	Z.Add(Inner[0].Z);
	V.Add(0.0);
	for (int32 I = 1; I < Inner.Num(); ++I)
	{
		const double H = Inner[I].Z - Inner[I - 1].Z;
		if (H <= 0.0) continue;
		Sum += 0.5 * (RingArea(Inner[I]) + RingArea(Inner[I - 1])) * H;
		Z.Add(Inner[I].Z);
		V.Add(Sum);
	}
	const int32 Steps = 32;
	int32 Seg = 1;
	for (int32 S = 0; S <= Steps; ++S)
	{
		const double Want = Sum * S / Steps;
		while (Seg < V.Num() - 1 && V[Seg] < Want) ++Seg;
		const double V0 = V[Seg - 1], V1 = V[FMath::Min(Seg, V.Num() - 1)];
		const double Z0 = Z[Seg - 1], Z1 = Z[FMath::Min(Seg, Z.Num() - 1)];
		const double T = V1 > V0 ? FMath::Clamp((Want - V0) / (V1 - V0), 0.0, 1.0) : 1.0;
		Out.Add((float)FMath::Lerp(Z0, Z1, T));
	}
}

const FBuilt& BuiltFor(FName Part)
{
	static TMap<FName, FBuilt> Cache;
	if (const FBuilt* Have = Cache.Find(Part)) return *Have;
	FBuilt Bt;
	const FName Kind = TorqueShedParts::PartKind(Part);
	const FString Box = TorqueShedParts::ContainerName(Part).ToLower();
	const double Litres = TorqueShedParts::FluidCapacity(Part);
	FGeo Shell, Caps, Trim, Liquid;
	TArray<FRing> Inner;
	if (Kind == FName(TEXT("petrol")))
	{
		Bt.Shape = Box.Contains(TEXT("drum")) ? EShape::Jug : EShape::JerryCan;
		if (Bt.Shape == EShape::Jug) Jug(Litres, Shell, Caps, Liquid, Inner);
		else JerryCan(Litres, Shell, Caps, Liquid, Inner);
	}
	else if (Kind == FName(TEXT("oil")))
	{
		Bt.Shape = Box.Contains(TEXT("pan")) ? EShape::DrainPan : EShape::OilBottle;
		if (Bt.Shape == EShape::DrainPan) DrainPan(Litres, Shell, Liquid, Inner);
		else OilBottle(Litres, Shell, Caps, Liquid, Inner);
	}
	else if (Kind == FName(TEXT("brake_fluid")))
	{
		Bt.Shape = Box.Contains(TEXT("bleed")) ? EShape::BleedBottle : EShape::BrakeBottle;
		RoundBottle(Litres, Bt.Shape == EShape::BleedBottle ? 3.0 : 2.9, Shell, Caps, Liquid, Inner, Bt.Shape == EShape::BrakeBottle);
		if (Bt.Shape == EShape::BleedBottle)
		{
			// Its clear tube, out of the cap and over, to go on a caliper's bleed nipple.
			const double CapTop = Inner.Num() > 0 ? Inner.Last().Z / 0.88 + 2.0 : 16.0;
			TArray<FVector> Line;
			for (int32 K = 0; K <= 12; ++K)
			{
				const double U = K / 12.0;
				Line.Add(FVector(6.0 * FMath::Sin(PI * 0.9 * U), 0.0, CapTop + 9.0 * FMath::Sin(PI * 0.5 * FMath::Min(U * 1.4, 1.0)) - 4.0 * U * U));
			}
			Tube(Trim, Line, 0.35, 8);
		}
	}
	else if (Kind == FName(TEXT("chain_lube")))
	{
		Bt.Shape = EShape::Aerosol;
		Aerosol(Litres, Shell, Trim);
	}
	else
	{
		Cache.Add(Part, Bt);
		return Cache[Part];
	}
	UMaterialInterface* Base = TorqueToolModels::Finish(GetTransientPackage(), FLinearColor::Gray, 0.0f, 0.5f);
	if (Base) Base->AddToRoot();
	Bt.Shell = BuildMesh(Shell, Base);
	Bt.Caps = BuildMesh(Caps, Base);
	Bt.Trim = BuildMesh(Trim, Base);
	Bt.Liquid = BuildMesh(Liquid, Base);
	FillCurve(Inner, Bt.HeightAt);
	if (Inner.Num() > 0) Bt.Axis = Inner[0].Off;
	// Slosh: a wide pan or a big can swings slower than a little bottle (f ~ 1 / sqrt of its size).
	double Width = 10.0;
	for (const FRing& R : Inner) Width = FMath::Max(Width, 2.0 * FMath::Max(R.A, R.B));
	Bt.Freq = FMath::Clamp(5.5 / FMath::Sqrt(Width), 0.7, 2.2);
	Bt.bValid = Bt.Shell != nullptr;
	UE_LOG(LogTemp, Log, TEXT("Torque fluids: %s drawn as a %s (%d + %d + %d tris, liquid %d)"), *Part.ToString(), *Box,
		Shell.T.Num() / 3, Caps.T.Num() / 3, Trim.T.Num() / 3, Liquid.T.Num() / 3);
	Cache.Add(Part, Bt);
	return Cache[Part];
}

UStaticMeshComponent* Place(AActor* Owner, USceneComponent* Holder, UStaticMesh* Mesh, UMaterialInterface* Mat, TArray<UStaticMeshComponent*>& Made)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Holder);
	C->RegisterComponent();
	if (Mat) C->SetMaterial(0, Mat);
	Made.Add(C);
	return C;
}

FLinearColor Lighter(const FLinearColor& C, float K)
{
	return FLinearColor(FMath::Lerp(C.R, 1.0f, K), FMath::Lerp(C.G, 1.0f, K), FMath::Lerp(C.B, 1.0f, K), 1.0f);
}
} // namespace TorqueFluidModelsImpl

namespace TFMI = TorqueFluidModelsImpl;

namespace TorqueFluidModels
{
bool Make(AActor* Owner, USceneComponent* Holder, FName Part, TArray<UStaticMeshComponent*>& Made, FVessel& Out)
{
	if (!Owner || !Holder || !TorqueShedParts::IsFluid(Part)) return false;
	const TFMI::FBuilt& Bt = TFMI::BuiltFor(Part);
	if (!Bt.bValid) return false;
	const FLinearColor Colour = TorqueShedParts::DefaultColour(Part);
	UMaterialInterface* ShellBase = TFMI::Master(TEXT("M_FluidShell"));
	UMaterialInterface* LiquidBase = TFMI::Master(TEXT("M_FluidLiquid"));
	// The body: see-through plastic, a solid black pan, or a steel aerosol in its colour.
	UMaterialInterface* ShellMat = nullptr;
	bool bSeeThrough = false;
	switch (Bt.Shape)
	{
	case TFMI::EShape::DrainPan:
		ShellMat = TorqueToolModels::Finish(Owner, FLinearColor(0.035f, 0.035f, 0.038f), 0.0f, 0.55f);
		break;
	case TFMI::EShape::Aerosol:
		ShellMat = TorqueToolModels::Finish(Owner, Colour, 0.6f, 0.3f);
		break;
	default:
		if (ShellBase)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ShellBase, Owner);
			// The plastic: a jerry can's and a jug's in the can's colour, the bottles' a milky natural
			// with the colour on the label and cap; the bleed bottle clear.
			FLinearColor Tint = TFMI::Lighter(Colour, 0.15f);
			float Opacity = 0.5f;
			if (Bt.Shape == TFMI::EShape::OilBottle || Bt.Shape == TFMI::EShape::BrakeBottle)
			{
				Tint = FLinearColor(0.86f, 0.85f, 0.8f);
				Opacity = 0.3f;
			}
			else if (Bt.Shape == TFMI::EShape::BleedBottle)
			{
				Tint = FLinearColor(0.92f, 0.95f, 0.97f);
				Opacity = 0.12f;
			}
			M->SetVectorParameterValue(TEXT("Color"), Tint);
			M->SetVectorParameterValue(TEXT("LabelColor"), Colour);
			M->SetScalarParameterValue(TEXT("Opacity"), Opacity);
			M->SetScalarParameterValue(TEXT("Roughness"), Bt.Shape == TFMI::EShape::BleedBottle ? 0.08f : 0.32f);
			ShellMat = M;
			bSeeThrough = true;
		}
		else ShellMat = TorqueToolModels::Finish(Owner, Colour, 0.0f, 0.4f);
		break;
	}
	TFMI::Place(Owner, Holder, Bt.Shell, ShellMat, Made);
	// The caps (the jerry can's in black, the bottles' in their colour), the aerosol's dome and nozzle,
	// the bleed bottle's tube.
	const FLinearColor CapColour = Bt.Shape == TFMI::EShape::JerryCan || Bt.Shape == TFMI::EShape::Jug || Bt.Shape == TFMI::EShape::BleedBottle
		? FLinearColor(0.03f, 0.03f, 0.03f) : Colour;
	TFMI::Place(Owner, Holder, Bt.Caps, TorqueToolModels::Finish(Owner, CapColour, 0.0f, 0.45f), Made);
	if (Bt.Trim)
	{
		UMaterialInterface* TrimMat = Bt.Shape == TFMI::EShape::Aerosol ? TorqueToolModels::Finish(Owner, FLinearColor(0.8f, 0.8f, 0.82f), 0.9f, 0.25f)
			: (ShellBase && Bt.Shape == TFMI::EShape::BleedBottle ? ShellMat : TorqueToolModels::Finish(Owner, FLinearColor(0.9f, 0.9f, 0.9f), 0.0f, 0.2f));
		TFMI::Place(Owner, Holder, Bt.Trim, TrimMat, Made);
	}
	// The fluid inside: only where you can see it (through the plastic, or in the open pan).
	Out = FVessel();
	if (Bt.Liquid && LiquidBase && (bSeeThrough || Bt.Shape == TFMI::EShape::DrainPan))
	{
		UMaterialInstanceDynamic* LM = UMaterialInstanceDynamic::Create(LiquidBase, Owner);
		UStaticMeshComponent* L = TFMI::Place(Owner, Holder, Bt.Liquid, LM, Made);
		if (L)
		{
			L->SetCastShadow(false);
			L->ComponentTags.Add(FName(TEXT("FluidLiquid")));
			Out.Liquid = L;
			Out.Material = LM;
			Out.HeightAt = Bt.HeightAt;
			Out.Axis = Bt.Axis;
			Out.Freq = Bt.Freq;
		}
	}
	return true;
}

FLinearColor AgedColour(FName Kind, const FLinearColor& Colour, float Age)
{
	const float A = FMath::Clamp(Age, 0.0f, 1.0f);
	if (Kind == FName(TEXT("oil")))   // fresh amber to black
		return FMath::Lerp(Colour, FLinearColor(0.025f, 0.02f, 0.015f), FMath::Min(1.0f, A * 1.15f));
	if (Kind == FName(TEXT("brake_fluid")))   // straw to dark tea as it takes on water
		return FMath::Lerp(Colour, FLinearColor(0.28f, 0.16f, 0.06f), A * 0.85f);
	return FMath::Lerp(Colour, Colour * FLinearColor(0.55f, 0.45f, 0.3f), A * 0.7f);   // stale petrol browns
}

void Update(FVessel& V, float Fill, const FLinearColor& Colour, float Age, float Dt)
{
	UStaticMeshComponent* L = V.Liquid.Get();
	UMaterialInstanceDynamic* M = V.Material.Get();
	if (!L || !M || V.HeightAt.Num() < 2) return;
	const float Want = FMath::Clamp(Fill, 0.0f, 1.0f);
	// The level follows what's in it, quickly (a pour takes a second or two, the shed's told in steps).
	V.Shown = V.Shown < 0.0f ? Want : FMath::FInterpTo(V.Shown, Want, Dt, 3.0f);
	if (V.Shown < 0.004f)
	{
		L->SetVisibility(false);
		return;
	}
	L->SetVisibility(true);
	const FTransform Xf = L->GetComponentTransform();
	const FVector Pos = Xf.GetLocation();
	const double Step = FMath::Clamp((double)Dt, 1.0 / 240.0, 0.05);
	V.Clock += Step;
	if (!V.bStarted)
	{
		V.bStarted = true;
		V.LastPos = Pos;
		V.LastVel = FVector::ZeroVector;
	}
	// Feel it accelerate: the surface leans against it (its normal along gravity less the acceleration),
	// a damped swing that overshoots and settles.
	const FVector Vel = (Pos - V.LastPos) / Step;
	FVector Acc = (Vel - V.LastVel) / Step;
	V.LastPos = Pos;
	V.LastVel = Vel;
	if (Vel.Size() > 2000.0) Acc = FVector::ZeroVector;   // teleported (put down somewhere, picked up across the shed)
	Acc = Acc.GetClampedToMaxSize(1500.0);
	const FVector2D Target(Acc.X / 980.0, Acc.Y / 980.0);
	const double W = 2.0 * PI * V.Freq;
	const double Damp = 2.0 * 0.11 * W;
	V.TiltRate += (W * W * (Target - V.Tilt) - Damp * V.TiltRate) * Step;
	V.Tilt += V.TiltRate * Step;
	V.Tilt = V.Tilt.ClampAxes(-0.7, 0.7);
	// A faint shiver on the surface while it settles.
	const double Shiver = FMath::Min(V.TiltRate.Size(), 1.0) * 0.015;
	const FVector2D Wobble(FMath::Sin(V.Clock * 9.1) * Shiver, FMath::Cos(V.Clock * 7.3) * Shiver);
	const FVector Normal = FVector(V.Tilt.X + Wobble.X, V.Tilt.Y + Wobble.Y, 1.0).GetSafeNormal();
	// The surface's height for this fill, through the liquid's middle.
	const float F = V.Shown * (V.HeightAt.Num() - 1);
	const int32 I = FMath::Clamp(FMath::FloorToInt(F), 0, V.HeightAt.Num() - 2);
	const float H = FMath::Lerp(V.HeightAt[I], V.HeightAt[I + 1], F - I);
	const FVector Point = Xf.TransformPosition(FVector(V.Axis.X, V.Axis.Y, H));
	M->SetVectorParameterValue(TEXT("PlaneP"), FLinearColor((float)Point.X, (float)Point.Y, (float)Point.Z, 0.0f));
	M->SetVectorParameterValue(TEXT("PlaneN"), FLinearColor((float)Normal.X, (float)Normal.Y, (float)Normal.Z, 0.0f));
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	M->SetVectorParameterValue(TEXT("SurfaceColor"), TFMI::Lighter(Colour, 0.12f));
	M->SetScalarParameterValue(TEXT("Roughness"), 0.2f + 0.2f * FMath::Clamp(Age, 0.0f, 1.0f));
}
}
