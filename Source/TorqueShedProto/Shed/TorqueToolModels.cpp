#include "Shed/TorqueToolModels.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace TorqueToolModelsImpl
{
// The models are written as tool_models.gd writes them, in Godot's frame (metres: x right, y up,
// z back, so forward is -z) and turned into Unreal's (cm: x forward, y right, z up) at the end.
struct FM3
{
	double M[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	FVector operator*(const FVector& V) const
	{
		return FVector(M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z, M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
			M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
	}
	FM3 operator*(const FM3& O) const
	{
		FM3 R;
		for (int32 I = 0; I < 3; ++I)
			for (int32 J = 0; J < 3; ++J)
				R.M[I][J] = M[I][0] * O.M[0][J] + M[I][1] * O.M[1][J] + M[I][2] * O.M[2][J];
		return R;
	}
};

FM3 RotX(double A) { FM3 R; const double C = FMath::Cos(A), S = FMath::Sin(A); R.M[1][1] = C; R.M[1][2] = -S; R.M[2][1] = S; R.M[2][2] = C; return R; }
FM3 RotY(double A) { FM3 R; const double C = FMath::Cos(A), S = FMath::Sin(A); R.M[0][0] = C; R.M[0][2] = S; R.M[2][0] = -S; R.M[2][2] = C; return R; }
FM3 RotZ(double A) { FM3 R; const double C = FMath::Cos(A), S = FMath::Sin(A); R.M[0][0] = C; R.M[0][1] = -S; R.M[1][0] = S; R.M[1][1] = C; return R; }
// A Node3D's rotation (Euler, applied Y then X then Z: Godot's YXZ order).
FM3 Euler(const FVector& R) { return RotY(R.Y) * RotX(R.X) * RotZ(R.Z); }

// Godot's frame to Unreal's: x' = -z, y' = x, z' = y (a mirror, as the two frames' hands differ).
FVector ToUe(const FVector& G) { return FVector(-G.Z, G.X, G.Y); }

// A node's place: P' = R (S P) + T.
struct FXf
{
	FM3 R;
	FVector S = FVector::OneVector, T = FVector::ZeroVector;
	FXf() = default;
	FXf(const FVector& Pos, const FVector& Rot = FVector::ZeroVector, const FVector& Scale = FVector::OneVector) : R(Euler(Rot)), S(Scale), T(Pos) {}
	FVector Point(const FVector& P) const { return R * (P * S) + T; }
	FVector Normal(const FVector& N) const { return (R * (N / S)).GetSafeNormal(); }
	// This node's transform inside a parent's.
	FXf Under(const FXf& Parent) const
	{
		// Only uniform-scale parents here (the spanner's ends): fold the parent's in.
		FXf Out;
		Out.R = Parent.R * R;
		Out.S = S * Parent.S.X;
		Out.T = Parent.Point(T);
		return Out;
	}
};

// One material's pieces, already in Unreal's frame (cm).
struct FGeo
{
	TArray<FVector> P, N;
	TArray<int32> T;
};

// A primitive being made in its own frame (Godot's), then placed into a material's FGeo.
struct FPrim
{
	TArray<FVector> P, N;
	TArray<int32> T;
	int32 V(const FVector& Pos, const FVector& Nrm)
	{
		P.Add(Pos);
		N.Add(Nrm.GetSafeNormal());
		return P.Num() - 1;
	}
	void Tri(int32 A, int32 B, int32 C) { T.Add(A); T.Add(B); T.Add(C); }
	void Quad(int32 A, int32 B, int32 C, int32 D) { Tri(A, B, C); Tri(A, C, D); }
};

FPrim Box(const FVector& Size)
{
	FPrim O;
	const FVector H = Size * 0.5;
	const FVector Axes[3] = {FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)};
	for (int32 A = 0; A < 3; ++A)
		for (const double Sgn : {-1.0, 1.0})
		{
			const FVector Nrm = Axes[A] * Sgn;
			const FVector U = Axes[(A + 1) % 3], W = Axes[(A + 2) % 3];
			const FVector C = FVector(Nrm.X * H.X, Nrm.Y * H.Y, Nrm.Z * H.Z);
			const FVector Hu = FVector(U.X * H.X, U.Y * H.Y, U.Z * H.Z), Hw = FVector(W.X * H.X, W.Y * H.Y, W.Z * H.Z);
			const int32 I0 = O.V(C - Hu - Hw, Nrm), I1 = O.V(C + Hu - Hw, Nrm), I2 = O.V(C + Hu + Hw, Nrm), I3 = O.V(C - Hu + Hw, Nrm);
			O.Quad(I0, I1, I2, I3);
		}
	return O;
}

// Along y, centred; top radius at +y.
FPrim Cyl(double Top, double Bottom, double Height, int32 Seg = 20)
{
	FPrim O;
	const double Hy = Height * 0.5;
	const double Slope = (Bottom - Top) / FMath::Max(Height, 1e-6);
	for (int32 I = 0; I < Seg; ++I)
	{
		const double A0 = UE_TWO_PI * I / Seg, A1 = UE_TWO_PI * (I + 1) / Seg;
		const FVector D0(FMath::Cos(A0), 0, FMath::Sin(A0)), D1(FMath::Cos(A1), 0, FMath::Sin(A1));
		const FVector N0 = D0 + FVector(0, Slope, 0), N1 = D1 + FVector(0, Slope, 0);
		const int32 B0 = O.V(D0 * Bottom - FVector(0, Hy, 0), N0), B1 = O.V(D1 * Bottom - FVector(0, Hy, 0), N1);
		const int32 T1 = O.V(D1 * Top + FVector(0, Hy, 0), N1), T0 = O.V(D0 * Top + FVector(0, Hy, 0), N0);
		O.Quad(B0, B1, T1, T0);
		if (Top > 0.0) O.Tri(O.V(FVector(0, Hy, 0), FVector(0, 1, 0)), O.V(D0 * Top + FVector(0, Hy, 0), FVector(0, 1, 0)), O.V(D1 * Top + FVector(0, Hy, 0), FVector(0, 1, 0)));
		if (Bottom > 0.0) O.Tri(O.V(FVector(0, -Hy, 0), FVector(0, -1, 0)), O.V(D1 * Bottom - FVector(0, Hy, 0), FVector(0, -1, 0)), O.V(D0 * Bottom - FVector(0, Hy, 0), FVector(0, -1, 0)));
	}
	return O;
}

// Godot's SphereMesh: Radius across, Height top to bottom (an ellipsoid if not 2 Radius); a
// hemisphere is the top half, its height Height.
FPrim Sphere(double Radius, double Height, bool bHemi = false, int32 Seg = 20, int32 Rings = 10)
{
	FPrim O;
	const double Yr = bHemi ? Height : Height * 0.5;
	const double Lo = bHemi ? 0.0 : -UE_HALF_PI;
	TArray<int32> Prev;
	for (int32 R = 0; R <= Rings; ++R)
	{
		const double Lat = Lo + (UE_HALF_PI - Lo) * R / Rings;
		TArray<int32> Row;
		for (int32 I = 0; I <= Seg; ++I)
		{
			const double Lon = UE_TWO_PI * I / Seg;
			const FVector D(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Sin(Lat), FMath::Cos(Lat) * FMath::Sin(Lon));
			const FVector Pos(D.X * Radius, D.Y * Yr, D.Z * Radius);
			const FVector Nrm(D.X / FMath::Max(Radius, 1e-6), D.Y / FMath::Max(Yr, 1e-6), D.Z / FMath::Max(Radius, 1e-6));
			Row.Add(O.V(Pos, Nrm));
		}
		if (Prev.Num() > 0)
			for (int32 I = 0; I < Seg; ++I) O.Quad(Prev[I], Prev[I + 1], Row[I + 1], Row[I]);
		Prev = Row;
	}
	return O;
}

// Godot's CapsuleMesh: along y, Height end to end.
FPrim Capsule(double Radius, double Height, int32 Seg = 16)
{
	FPrim O = Cyl(Radius, Radius, FMath::Max(Height - 2.0 * Radius, 0.0), Seg);
	const double Hy = FMath::Max(Height * 0.5 - Radius, 0.0);
	for (const double Sgn : {1.0, -1.0})
	{
		const FPrim Cap = Sphere(Radius, Radius, true, Seg, 6);
		const int32 Base = O.P.Num();
		for (int32 I = 0; I < Cap.P.Num(); ++I)
		{
			O.P.Add(FVector(Cap.P[I].X, Cap.P[I].Y * Sgn + Hy * Sgn, Cap.P[I].Z));
			O.N.Add(FVector(Cap.N[I].X, Cap.N[I].Y * Sgn, Cap.N[I].Z));
		}
		for (const int32 Ix : Cap.T) O.T.Add(Base + Ix);
	}
	return O;
}

// Lying in the xz plane round y, from Inner to Outer.
FPrim Torus(double Inner, double Outer, int32 Rings = 24, int32 Sides = 8)
{
	FPrim O;
	const double Rc = (Inner + Outer) * 0.5, Rt = (Outer - Inner) * 0.5;
	TArray<int32> Grid;
	for (int32 I = 0; I <= Rings; ++I)
		for (int32 J = 0; J <= Sides; ++J)
		{
			const double U = UE_TWO_PI * I / Rings, W = UE_TWO_PI * J / Sides;
			const FVector D(FMath::Cos(U), 0, FMath::Sin(U));
			const FVector Nrm = D * FMath::Cos(W) + FVector(0, FMath::Sin(W), 0);
			Grid.Add(O.V(D * Rc + Nrm * Rt, Nrm));
		}
	for (int32 I = 0; I < Rings; ++I)
		for (int32 J = 0; J < Sides; ++J)
		{
			const int32 A = I * (Sides + 1) + J, B = (I + 1) * (Sides + 1) + J;
			O.Quad(Grid[A], Grid[B], Grid[B + 1], Grid[A + 1]);
		}
	return O;
}

// A flat ring segment in the xy plane, Thick along z, from angle A0 to A1 (0 = +x, towards +y).
FPrim RingSolid(double RIn, double ROut, double Thick, double A0, double A1, int32 Seg = 24)
{
	FPrim O;
	const double Hz = Thick * 0.5;
	const FVector F(0, 0, Hz), Bk(0, 0, -Hz);
	for (int32 I = 0; I < Seg; ++I)
	{
		const double T0 = A0 + (A1 - A0) * I / Seg, T1 = A0 + (A1 - A0) * (I + 1) / Seg;
		const FVector D0(FMath::Cos(T0), FMath::Sin(T0), 0), D1(FMath::Cos(T1), FMath::Sin(T1), 0);
		O.Quad(O.V(D0 * RIn + F, FVector(0, 0, 1)), O.V(D0 * ROut + F, FVector(0, 0, 1)), O.V(D1 * ROut + F, FVector(0, 0, 1)), O.V(D1 * RIn + F, FVector(0, 0, 1)));
		O.Quad(O.V(D0 * RIn + Bk, FVector(0, 0, -1)), O.V(D1 * RIn + Bk, FVector(0, 0, -1)), O.V(D1 * ROut + Bk, FVector(0, 0, -1)), O.V(D0 * ROut + Bk, FVector(0, 0, -1)));
		O.Quad(O.V(D0 * ROut + F, D0), O.V(D0 * ROut + Bk, D0), O.V(D1 * ROut + Bk, D1), O.V(D1 * ROut + F, D1));
		O.Quad(O.V(D0 * RIn + F, -D0), O.V(D1 * RIn + F, -D1), O.V(D1 * RIn + Bk, -D1), O.V(D0 * RIn + Bk, -D0));
	}
	if (FMath::Abs(A1 - A0) < UE_TWO_PI - 0.001)
		for (const double A : {A0, A1})
		{
			const FVector D(FMath::Cos(A), FMath::Sin(A), 0);
			const FVector Nrm = FVector(-D.Y, D.X, 0) * (A == A0 ? -1.0 : 1.0);
			O.Quad(O.V(D * RIn + Bk, Nrm), O.V(D * ROut + Bk, Nrm), O.V(D * ROut + F, Nrm), O.V(D * RIn + F, Nrm));
		}
	return O;
}

// The materials of tool_models.gd _mat: colour, metallic, roughness.
struct FFinish
{
	FLinearColor Colour;
	float Metallic = 0.0f, Roughness = 0.5f;
};

FFinish NamedFinish(const FString& Key)
{
	struct FRow { const TCHAR* Key; float R, G, B, M, Ro; };
	static const FRow Rows[] = {
		{TEXT("chrome"), 0.88f, 0.89f, 0.92f, 0.72f, 0.18f}, {TEXT("steel"), 0.66f, 0.67f, 0.7f, 0.65f, 0.38f},
		{TEXT("alu"), 0.7f, 0.71f, 0.72f, 0.7f, 0.45f}, {TEXT("dark_steel"), 0.2f, 0.2f, 0.22f, 0.7f, 0.45f},
		{TEXT("rubber"), 0.06f, 0.06f, 0.065f, 0.0f, 0.8f}, {TEXT("plastic_black"), 0.08f, 0.08f, 0.09f, 0.0f, 0.45f},
		{TEXT("wood"), 0.5f, 0.31f, 0.15f, 0.0f, 0.62f}, {TEXT("wood_dark"), 0.3f, 0.17f, 0.08f, 0.0f, 0.7f},
		{TEXT("wire"), 0.75f, 0.73f, 0.68f, 0.8f, 0.5f}, {TEXT("brass"), 0.85f, 0.66f, 0.3f, 0.9f, 0.3f},
		{TEXT("grinder_body"), 0.08f, 0.36f, 0.62f, 0.0f, 0.35f}, {TEXT("red"), 0.78f, 0.1f, 0.07f, 0.0f, 0.4f},
		{TEXT("orange"), 0.95f, 0.42f, 0.05f, 0.0f, 0.4f}, {TEXT("yellow"), 0.97f, 0.76f, 0.06f, 0.0f, 0.35f},
		{TEXT("white"), 0.94f, 0.94f, 0.92f, 0.0f, 0.3f}, {TEXT("abrasive"), 0.22f, 0.21f, 0.2f, 0.1f, 0.95f},
		{TEXT("ink"), 0.06f, 0.06f, 0.07f, 0.0f, 0.5f}, {TEXT("hole"), 0.02f, 0.02f, 0.02f, 0.0f, 1.0f},
	};
	for (const FRow& R : Rows)
		if (Key == R.Key) return {FLinearColor(R.R, R.G, R.B), R.M, R.Ro};
	return {FLinearColor(1.0f, 0.0f, 1.0f), 0.0f, 0.5f};
}

// A tool being drawn: its pieces by finish ("chrome", or "tint:<r>,<g>,<b>,<m>,<ro>").
struct FBuilder
{
	TMap<FString, FGeo> Geos;
	TMap<FString, FFinish> Finishes;
	FVector Nozzle = FVector::ZeroVector;   // Godot's frame

	static FString Tint(const FLinearColor& C, float M, float Ro)
	{
		return FString::Printf(TEXT("tint:%.3f,%.3f,%.3f,%.2f,%.2f"), C.R, C.G, C.B, M, Ro);
	}
	void Add(const FPrim& Prim, const FString& Mat, const FXf& Xf)
	{
		if (!Finishes.Contains(Mat))
		{
			FFinish F;
			if (Mat.StartsWith(TEXT("tint:")))
			{
				TArray<FString> Bits;
				Mat.Mid(5).ParseIntoArray(Bits, TEXT(","), true);
				if (Bits.Num() == 5)
				{
					F.Colour = FLinearColor(FCString::Atof(*Bits[0]), FCString::Atof(*Bits[1]), FCString::Atof(*Bits[2]));
					F.Metallic = FCString::Atof(*Bits[3]);
					F.Roughness = FCString::Atof(*Bits[4]);
				}
			}
			else F = NamedFinish(Mat);
			Finishes.Add(Mat, F);
		}
		FGeo& G = Geos.FindOrAdd(Mat);
		const int32 Base = G.P.Num();
		for (int32 I = 0; I < Prim.P.Num(); ++I)
		{
			G.P.Add(ToUe(Xf.Point(Prim.P[I])) * 100.0);
			G.N.Add(ToUe(Xf.Normal(Prim.N[I])).GetSafeNormal());
		}
		for (const int32 Ix : Prim.T) G.T.Add(Base + Ix);
	}
};

const double Pi = UE_DOUBLE_PI;
double Deg(double D) { return FMath::DegreesToRadians(D); }

// tool_models.gd _spanner: a chrome combination spanner, long axis y, open jaw at the top.
void Spanner(FBuilder& B)
{
	B.Add(Box(FVector(0.013, 0.15, 0.005)), TEXT("steel"), FXf(FVector(0, 0, 0)));
	for (const double X : {-0.0072, 0.0072}) B.Add(Box(FVector(0.0035, 0.152, 0.0085)), TEXT("chrome"), FXf(FVector(X, 0, 0)));
	B.Add(Cyl(0.0105, 0.0085, 0.03, 4), TEXT("chrome"), FXf(FVector(0, 0.08, 0), FVector(0, Pi * 0.25, 0), FVector(1.0, 1.0, 0.42)));
	B.Add(Cyl(0.0085, 0.0115, 0.026, 4), TEXT("chrome"), FXf(FVector(0, -0.082, 0), FVector(0, Pi * 0.25, 0), FVector(1.0, 1.0, 0.42)));
	const FXf Open(FVector(0, 0.106, 0), FVector(0, 0, Deg(15.0)));
	const double Gap = Deg(64.0);
	B.Add(RingSolid(0.0095, 0.0215, 0.0085, Pi * 0.5 + Gap * 0.5, Pi * 2.5 - Gap * 0.5, 28), TEXT("chrome"), FXf().Under(Open));
	for (const double S : {-1.0, 1.0})
		B.Add(Box(FVector(0.006, 0.016, 0.0085)), TEXT("chrome"), FXf(FVector(S * 0.0128, 0.012, 0), FVector(0, 0, -S * 0.12)).Under(Open));
	const FXf Ring(FVector(0, -0.105, 0.0015), FVector(Deg(-10.0), 0, 0));
	B.Add(RingSolid(0.0085, 0.0185, 0.0095, 0.0, Pi * 2.0, 32), TEXT("chrome"), FXf().Under(Ring));
	B.Add(RingSolid(0.0078, 0.0088, 0.0085, 0.0, Pi * 2.0, 12), TEXT("dark_steel"), FXf().Under(Ring));
}

// _brush: a wooden wire brush, handle down, wire tufts facing forward (-z).
void Brush(FBuilder& B)
{
	B.Add(Capsule(0.0125, 0.15), TEXT("wood"), FXf(FVector(0, -0.075, 0.004), FVector::ZeroVector, FVector(1.25, 1.0, 0.75)));
	B.Add(Cyl(0.0045, 0.0045, 0.022, 12), TEXT("hole"), FXf(FVector(0, -0.135, 0.004), FVector(Pi * 0.5, 0, 0)));
	B.Add(Box(FVector(0.04, 0.12, 0.02)), TEXT("wood"), FXf(FVector(0, 0.055, 0)));
	B.Add(Cyl(0.02, 0.02, 0.02, 20), TEXT("wood"), FXf(FVector(0, 0.115, 0), FVector(Pi * 0.5, 0, 0)));
	B.Add(Box(FVector(0.036, 0.13, 0.004)), TEXT("wood_dark"), FXf(FVector(0, 0.06, -0.0095)));
	const FPrim Tuft = Cyl(0.0036, 0.0022, 0.028, 6);
	for (int32 Row = 0; Row < 4; ++Row)
		for (int32 Col = 0; Col < 12; ++Col)
		{
			const double X = (Row - 1.5) * 0.0095;
			const double Y = 0.008 + Col * 0.0098 + (Row % 2 == 1 ? 0.0049 : 0.0);
			B.Add(Tuft, TEXT("wire"), FXf(FVector(X, Y, -0.025), FVector(Pi * 0.5, 0, 0)));
		}
	B.Add(Box(FVector(0.042, 0.008, 0.022)), TEXT("steel"), FXf(FVector(0, -0.004, 0)));
}

// _grinder: motor body along y, gear head on top, disc and guard to the front (-z).
void Grinder(FBuilder& B)
{
	B.Add(Cyl(0.028, 0.026, 0.18, 24), TEXT("grinder_body"), FXf(FVector(0, -0.05, 0)));
	B.Add(Cyl(0.0285, 0.0272, 0.075, 24), TEXT("rubber"), FXf(FVector(0, -0.095, 0)));
	B.Add(Sphere(0.026, 0.03, false, 20), TEXT("grinder_body"), FXf(FVector(0, -0.14, 0)));
	B.Add(Cyl(0.0065, 0.0045, 0.04, 10), TEXT("rubber"), FXf(FVector(0, -0.168, 0)));
	B.Add(Cyl(0.0035, 0.0035, 0.04, 8), TEXT("rubber"), FXf(FVector(0, -0.205, 0)));
	for (const double Z : {-1.0, 1.0})
		for (int32 I = 0; I < 4; ++I) B.Add(Box(FVector(0.004, 0.018, 0.004)), TEXT("hole"), FXf(FVector(-0.0135 + I * 0.009, 0.015, Z * 0.0262)));
	B.Add(Box(FVector(0.012, 0.03, 0.006)), TEXT("plastic_black"), FXf(FVector(0, -0.02, 0.0275)));
	B.Add(Box(FVector(0.009, 0.01, 0.005)), TEXT("red"), FXf(FVector(0, -0.012, 0.031)));
	B.Add(Box(FVector(0.05, 0.04, 0.05)), TEXT("alu"), FXf(FVector(0, 0.055, -0.004)));
	B.Add(Cyl(0.026, 0.026, 0.05, 24), TEXT("alu"), FXf(FVector(0, 0.068, -0.01), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.008, 0.008, 0.006, 12), TEXT("dark_steel"), FXf(FVector(0, 0.068, 0.017), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.006, 0.006, 0.02, 10), TEXT("steel"), FXf(FVector(0.033, 0.068, -0.01), FVector(0, 0, Pi * 0.5)));
	B.Add(Cyl(0.011, 0.012, 0.085, 16), TEXT("rubber"), FXf(FVector(0.083, 0.068, -0.01), FVector(0, 0, Pi * 0.5)));
	for (int32 I = 0; I < 4; ++I) B.Add(Torus(0.0105, 0.0135, 16), TEXT("rubber"), FXf(FVector(0.055 + I * 0.018, 0.068, -0.01), FVector(0, 0, Pi * 0.5)));
	const FVector C(0, 0.068, 0);
	B.Add(Cyl(0.006, 0.006, 0.012, 12), TEXT("steel"), FXf(C + FVector(0, 0, -0.04), FVector(Pi * 0.5, 0, 0)));
	B.Add(RingSolid(0.015, 0.064, 0.0016, Pi * 0.85, Pi * 2.15, 28), TEXT("steel"), FXf(C + FVector(0, 0, -0.04)));
	B.Add(RingSolid(0.062, 0.0645, 0.013, Pi * 0.85, Pi * 2.15, 28), TEXT("steel"), FXf(C + FVector(0, 0, -0.0465)));
	B.Add(Cyl(0.058, 0.058, 0.003, 36), TEXT("abrasive"), FXf(C + FVector(0, 0, -0.049), FVector(Pi * 0.5, 0, 0)));
	for (const double Z : {-0.0471, -0.0509}) B.Add(Cyl(0.022, 0.022, 0.0006, 24), TEXT("red"), FXf(C + FVector(0, 0, Z), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.011, 0.011, 0.005, 6), TEXT("chrome"), FXf(C + FVector(0, 0, -0.053), FVector(Pi * 0.5, 0, 0)));
	// The sparks come off the disc's lower edge.
	B.Nozzle = C + FVector(0, -0.05, -0.05);
}

// _spray_can: steel can, label band in the paint colour, domed top, nozzle forward (-z).
void SprayCan(FBuilder& B, const FLinearColor& Can)
{
	const double R = 0.032;
	const FLinearColor Dark = Can * 0.85f;
	const FString Body = FBuilder::Tint(Dark, 0.55f, 0.3f), Band = FBuilder::Tint(Can, 0.15f, 0.35f);
	B.Add(Cyl(R, R, 0.15, 28), Body, FXf(FVector(0, -0.02, 0)));
	B.Add(Cyl(R + 0.0006, R + 0.0006, 0.085, 28), Band, FXf(FVector(0, -0.02, 0)));
	for (const double Y : {-0.0625, 0.0225}) B.Add(Cyl(R + 0.0008, R + 0.0008, 0.005, 28), TEXT("white"), FXf(FVector(0, Y, 0)));
	B.Add(Cyl(R + 0.0009, R + 0.0009, 0.016, 28), TEXT("ink"), FXf(FVector(0, -0.02, 0)));
	B.Add(Torus(R - 0.003, R + 0.0015, 28), TEXT("chrome"), FXf(FVector(0, -0.095, 0)));
	B.Add(Torus(R - 0.003, R + 0.0015, 28), TEXT("chrome"), FXf(FVector(0, 0.055, 0)));
	B.Add(Sphere(R - 0.001, R - 0.001, true, 28), TEXT("chrome"), FXf(FVector(0, 0.055, 0), FVector::ZeroVector, FVector(1, 0.5, 1)));
	B.Add(Torus(0.008, 0.0115, 20), TEXT("chrome"), FXf(FVector(0, 0.072, 0)));
	B.Add(Cyl(0.0085, 0.0085, 0.004, 16), TEXT("steel"), FXf(FVector(0, 0.073, 0)));
	B.Add(Cyl(0.0075, 0.0085, 0.014, 16), TEXT("plastic_black"), FXf(FVector(0, 0.081, 0)));
	B.Add(Sphere(0.0075, 0.006, false, 16), TEXT("plastic_black"), FXf(FVector(0, 0.088, 0)));
	B.Add(Cyl(0.0022, 0.0022, 0.004, 10), TEXT("hole"), FXf(FVector(0, 0.083, -0.0076), FVector(Pi * 0.5, 0, 0)));
	B.Nozzle = FVector(0, 0.083, -0.009);
}

// _lance: the pressure washer's trigger gun and lance, nozzle at (0, 0, -0.6).
void Lance(FBuilder& B)
{
	B.Add(Box(FVector(0.034, 0.045, 0.12)), TEXT("yellow"), FXf(FVector(0, 0, -0.02)));
	B.Add(Cyl(0.0225, 0.0225, 0.034, 20), TEXT("yellow"), FXf(FVector(0, 0, 0.04), FVector(0, 0, Pi * 0.5)));
	B.Add(Box(FVector(0.036, 0.01, 0.1)), TEXT("plastic_black"), FXf(FVector(0, 0.024, -0.025)));
	B.Add(Capsule(0.017, 0.12), TEXT("plastic_black"), FXf(FVector(0, -0.07, 0.055), FVector(Deg(-18.0), 0, 0), FVector(0.9, 1.0, 1.15)));
	B.Add(Box(FVector(0.012, 0.075, 0.012)), TEXT("yellow"), FXf(FVector(0, -0.052, 0.012), FVector(Deg(-14.0), 0, 0)));
	B.Add(RingSolid(0.05, 0.057, 0.008, Pi * 1.05, Pi * 1.5, 12), TEXT("plastic_black"), FXf(FVector(0, -0.012, 0.05), FVector(0, Pi * 0.5, 0)));
	B.Add(Cyl(0.009, 0.009, 0.02, 12), TEXT("brass"), FXf(FVector(0, -0.135, 0.077), FVector(Deg(-18.0), 0, 0)));
	B.Add(Cyl(0.0085, 0.0085, 0.07, 14), TEXT("rubber"), FXf(FVector(0, -0.17, 0.1), FVector(Deg(-40.0), 0, 0)));
	B.Add(Cyl(0.011, 0.011, 0.02, 14), TEXT("brass"), FXf(FVector(0, 0, -0.088), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.0065, 0.0065, 0.47, 12), TEXT("chrome"), FXf(FVector(0, 0, -0.33), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.0135, 0.0135, 0.11, 16), TEXT("rubber"), FXf(FVector(0, 0, -0.22), FVector(Pi * 0.5, 0, 0)));
	for (const double Z : {-0.165, -0.275}) B.Add(Cyl(0.015, 0.0135, 0.006, 16), TEXT("rubber"), FXf(FVector(0, 0, Z), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.0105, 0.013, 0.04, 16), TEXT("plastic_black"), FXf(FVector(0, 0, -0.572), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.0085, 0.0105, 0.012, 16), TEXT("orange"), FXf(FVector(0, 0, -0.592), FVector(Pi * 0.5, 0, 0)));
	B.Add(Cyl(0.003, 0.003, 0.004, 8), TEXT("brass"), FXf(FVector(0, 0, -0.598), FVector(Pi * 0.5, 0, 0)));
	B.Nozzle = FVector(0, 0, -0.6);
}

// One material's pieces as a static mesh. Each triangle is wound to face the way its normals do
// (Unreal's face: -(P1 - P0) x (P2 - P0)), so the primitives needn't agree on a winding.
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
	const FName SlotName(TEXT("Tool"));
	SlotNames[Group] = SlotName;
	TArray<FVertexInstanceID> Instances;
	Instances.Reserve(G.P.Num());
	for (int32 I = 0; I < G.P.Num(); ++I)
	{
		const FVertexID V = MD.CreateVertex();
		Positions[V] = FVector3f(G.P[I]);
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		const FVector3f N = FVector3f(G.N[I]).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Normals[VI] = N;
		const FVector3f Across = FMath::Abs(N.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		Tangents[VI] = FVector3f::CrossProduct(Across, N).GetSafeNormal();
		Signs[VI] = 1.0f;
		Colours[VI] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		UVs.Set(VI, 0, FVector2f((float)(G.P[I].X + G.P[I].Y) * 0.1f, (float)G.P[I].Z * 0.1f));
		UVs.Set(VI, 1, FVector2f(0.0f, 0.0f));
		Instances.Add(VI);
	}
	for (int32 T = 0; T + 2 < G.T.Num(); T += 3)
	{
		const int32 A = G.T[T];
		int32 B = G.T[T + 1], C = G.T[T + 2];
		if (A == B || B == C || A == C) continue;
		const FVector Face = -FVector::CrossProduct(G.P[B] - G.P[A], G.P[C] - G.P[A]);
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
	return Mesh;
}

UMaterialInterface* BaseMaterial()
{
	static UMaterialInterface* M = nullptr;
	if (!M)
	{
		M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bike/Materials/MI_steel_brushed.MI_steel_brushed"));
		if (M) M->AddToRoot();   // kept across map travel
	}
	return M;
}
} // namespace TorqueToolModelsImpl

namespace TTMI = TorqueToolModelsImpl;

namespace TorqueToolModels
{
UMaterialInstanceDynamic* Finish(UObject* Outer, const FLinearColor& Colour, float Metallic, float Roughness, float Emissive)
{
	UMaterialInterface* Base = TTMI::BaseMaterial();
	if (!Base) return nullptr;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Outer);
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	M->SetVectorParameterValue(TEXT("Color2"), Colour);
	for (const TCHAR* P : {TEXT("Metallic"), TEXT("Metallic2")}) M->SetScalarParameterValue(P, Metallic);
	for (const TCHAR* P : {TEXT("Roughness"), TEXT("Roughness2")}) M->SetScalarParameterValue(P, Roughness);
	M->SetScalarParameterValue(TEXT("RoughVar"), 0.0f);
	M->SetScalarParameterValue(TEXT("NormalStrength"), 0.0f);
	M->SetScalarParameterValue(TEXT("Grime"), 0.0f);
	M->SetScalarParameterValue(TEXT("Emissive"), Emissive);
	return M;
}

int32 Make(AActor* Owner, USceneComponent* Parent, int32 Tool, const FLinearColor& CanColour,
	TArray<UStaticMeshComponent*>& Out, FVector& OutNozzle)
{
	OutNozzle = FVector::ZeroVector;
	if (!Owner || !Parent) return 0;
	// Built once for each tool (and can colour) and kept.
	struct FMade
	{
		UStaticMesh* Mesh = nullptr;
		TTMI::FFinish Finish;
	};
	struct FTool
	{
		TArray<FMade> Parts;
		FVector Nozzle = FVector::ZeroVector;
	};
	static TMap<FString, FTool> Cache;
	const FString Key = FString::Printf(TEXT("%d:%s"), Tool, Tool == Spray ? *CanColour.ToString() : TEXT(""));
	FTool* Found = Cache.Find(Key);
	if (!Found)
	{
		TTMI::FBuilder Bld;
		switch (Tool)
		{
		case Wrench: TTMI::Spanner(Bld); break;
		case Brush: TTMI::Brush(Bld); break;
		case Repair: TTMI::Grinder(Bld); break;
		case Spray: TTMI::SprayCan(Bld, CanColour); break;
		case Lance: TTMI::Lance(Bld); break;
		default: break;
		}
		FTool T;
		T.Nozzle = TTMI::ToUe(Bld.Nozzle) * 100.0;
		for (const TPair<FString, TTMI::FGeo>& KV : Bld.Geos)
		{
			FMade M;
			M.Mesh = TTMI::BuildMesh(KV.Value, TTMI::BaseMaterial());
			if (!M.Mesh) continue;
			M.Mesh->AddToRoot();   // kept in the cache across map travel
			M.Finish = Bld.Finishes.FindRef(KV.Key);
			T.Parts.Add(M);
		}
		Found = &Cache.Add(Key, T);
	}
	for (const FMade& M : Found->Parts)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetStaticMesh(M.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetupAttachment(Parent);
		C->RegisterComponent();
		C->SetMaterial(0, Finish(C, M.Finish.Colour, M.Finish.Metallic, M.Finish.Roughness));
		Out.Add(C);
	}
	OutNozzle = Found->Nozzle;
	return Found->Parts.Num();
}

FTransform HoldOf(int32 Tool)
{
	// walker.gd _set_tool_view, in the camera's frame (Godot: x right, y up, -z ahead).
	FVector Pos(0.28, -0.28, -0.5), Rot(0.25, -0.3, 0.0);
	if (Tool == Lance)
	{
		Pos = FVector(0.22, -0.26, -0.3);
		Rot = FVector(0.05, -0.12, 0.0);
	}
	// A rotation R in Godot's frame is M R M^T in Unreal's (M: the frame swap); FMatrix rows are
	// the images of Unreal's axes, so each row is M R M^T applied to an axis.
	const TTMI::FM3 R = TTMI::Euler(Rot);
	auto Image = [&R](const FVector& UeAxis)
	{
		const FVector G(UeAxis.Y, UeAxis.Z, -UeAxis.X);   // M^T: Unreal to Godot
		return TTMI::ToUe(R * G);
	};
	const FMatrix Mx(Image(FVector(1, 0, 0)), Image(FVector(0, 1, 0)), Image(FVector(0, 0, 1)), TTMI::ToUe(Pos) * 100.0);
	return FTransform(Mx);
}
}
