// The shapes the Godot game draws in code rather than from Blender glbs (BikeModel._box,
// _cylinder, _sphere, _torus and _shell over ShapeKit.loft), made here with the same vertices so
// they come out the same size, then placed into a mesh of ours. Godot frame (x right, y up,
// -z forward) until ToOurs.
#pragma once

#include "BikeParts.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TorqueFit
{
namespace CodeShapes
{
inline constexpr double Tau = 6.283185307179586;

// A mesh in a Godot node's own space: points, normals and triangles (wound in ToOurs).
struct GMesh
{
	std::vector<V3> P, N;
	std::vector<double> U, V;
	std::vector<unsigned> Idx;
	unsigned Add(const V3& At, const V3& Nrm, double Uu = 0.0, double Vv = 0.0)
	{
		P.push_back(At);
		N.push_back(Nrm);
		U.push_back(Uu);
		V.push_back(Vv);
		return static_cast<unsigned>(P.size() - 1);
	}
	void Quad(unsigned A, unsigned B, unsigned C, unsigned D) { Idx.insert(Idx.end(), {A, B, C, B, D, C}); }
};

// BoxMesh: size whole, about its centre (no subdivisions: each face its four corners).
inline GMesh Box(const V3& Size)
{
	GMesh M;
	const V3 H = Size * 0.5;
	for (int Axis = 0; Axis < 3; ++Axis)
		for (double S : {-1.0, 1.0})
		{
			V3 Nrm;
			(Axis == 0 ? Nrm.x : Axis == 1 ? Nrm.y : Nrm.z) = S;
			unsigned Corner[4];
			int K = 0;
			for (double A : {-1.0, 1.0})
				for (double B : {-1.0, 1.0})
				{
					V3 C = Axis == 0 ? V3(S, A, B) : Axis == 1 ? V3(A, S, B) : V3(A, B, S);
					Corner[K++] = M.Add(V3(C.x * H.x, C.y * H.y, C.z * H.z), Nrm, A * 0.5 + 0.5, B * 0.5 + 0.5);
				}
			M.Quad(Corner[0], Corner[1], Corner[2], Corner[3]);
		}
	return M;
}

// CylinderMesh along Y (bottom radius R, top Top, Segments round, 4 rings, both caps).
inline GMesh Cylinder(double R, double Height, double Top = -1.0, int Segments = 32)
{
	GMesh M;
	const double Rt = Top < 0.0 ? R : Top;
	const int Rings = 4;
	const double Slope = (R - Rt) / std::max(Height, 1e-6);
	for (int J = 0; J <= Rings + 1; ++J)
	{
		const double V = static_cast<double>(J) / (Rings + 1);
		const double Rad = Rt + (R - Rt) * V, Y = Height * 0.5 - Height * V;
		for (int I = 0; I <= Segments; ++I)
		{
			const double U = static_cast<double>(I) / Segments;
			const double X = std::sin(U * Tau), Z = std::cos(U * Tau);
			M.Add(V3(X * Rad, Y, Z * Rad), V3(X, Slope, Z).Normalized(), U, V);
			if (I > 0 && J > 0)
			{
				const unsigned D = static_cast<unsigned>(M.P.size() - 1), C = D - 1;
				M.Quad(C - (Segments + 1), D - (Segments + 1), C, D);
			}
		}
	}
	for (double S : {1.0, -1.0})
	{
		const double Rad = S > 0.0 ? Rt : R, Y = Height * 0.5 * S;
		const unsigned Mid = M.Add(V3(0.0, Y, 0.0), V3(0.0, S, 0.0), 0.5, 0.5);
		for (int I = 0; I <= Segments; ++I)
		{
			const double U = static_cast<double>(I) / Segments;
			M.Add(V3(std::sin(U * Tau) * Rad, Y, std::cos(U * Tau) * Rad), V3(0.0, S, 0.0));
			if (I > 0) M.Idx.insert(M.Idx.end(), {Mid, Mid + I, Mid + I + 1});
		}
	}
	return M;
}

// SphereMesh (radial_segments 24, rings 12, as BikeModel._sphere): Rings + 2 rows pole to pole.
inline GMesh Sphere(double R, int Radial = 24, int Rings = 12)
{
	GMesh M;
	for (int J = 0; J <= Rings + 1; ++J)
	{
		const double V = static_cast<double>(J) / (Rings + 1);
		const double W = std::sin(3.141592653589793 * V), Y = R * std::cos(3.141592653589793 * V);
		for (int I = 0; I <= Radial; ++I)
		{
			const double U = static_cast<double>(I) / Radial;
			const V3 At(std::sin(U * Tau) * R * W, Y, std::cos(U * Tau) * R * W);
			M.Add(At, At.Length() > 1e-9 ? At.Normalized() : V3(0.0, Y > 0.0 ? 1.0 : -1.0, 0.0), U, V);
			if (I > 0 && J > 0)
			{
				const unsigned D = static_cast<unsigned>(M.P.size() - 1), C = D - 1;
				M.Quad(C - (Radial + 1), D - (Radial + 1), C, D);
			}
		}
	}
	return M;
}

// TorusMesh about Y (BikeModel._torus: Radius out to the middle of a tube Thick in radius; 16
// rings round, 6 segments round the tube).
inline GMesh Torus(double Radius, double Thick, int Rings = 16, int RingSegments = 6)
{
	GMesh M;
	const double Inner = Radius - Thick, Tube = Thick;
	for (int I = 0; I <= Rings; ++I)
	{
		const double Ai = static_cast<double>(I) / Rings * Tau;
		const double Nix = -std::sin(Ai), Niy = -std::cos(Ai);
		for (int J = 0; J <= RingSegments; ++J)
		{
			const double Aj = static_cast<double>(J) / RingSegments * Tau;
			const double Njx = -std::cos(Aj), Njy = std::sin(Aj);
			const double Kx = Njx * Tube + Inner + Tube, Ky = Njy * Tube;
			M.Add(V3(Nix * Kx, Ky, Niy * Kx), V3(Nix * Njx, Njy, Niy * Njx), static_cast<double>(I) / Rings, static_cast<double>(J) / RingSegments);
			if (I > 0 && J > 0)
			{
				const unsigned D = static_cast<unsigned>(M.P.size() - 1), C = D - 1;
				M.Quad(C - (RingSegments + 1), D - (RingSegments + 1), C, D);
			}
		}
	}
	return M;
}

// A loft section as ShapeKit takes it (c, w, up, down, n; bw 1).
struct Sec
{
	V3 C;
	double W = 0.1, Up = 0.1, Down = 0.1, N = 2.4;
	double Bw = 1.0;   // the lower half's width as a share of W (a tank narrowing to its tunnel)
};

// ShapeKit.loft (28 round): closed with flat caps at both ends, as BikeModel._shell draws an arm,
// or Open: only the arc A0 to A1 (radians, 0 at the right side, PI/2 at the top), both faces.
inline GMesh Loft(const std::vector<Sec>& Secs, int Ring = 28, bool Open = false, double A0 = 0.0, double A1 = Tau)
{
	GMesh M;
	const int Count = static_cast<int>(Secs.size());
	if (Count < 2) return M;
	const double Span = Open ? A1 - A0 : Tau;
	const int Steps = Open ? std::max(4, static_cast<int>(std::lround(Ring * Span / Tau))) : Ring;
	const int Width = Steps + (Open ? 1 : 0);
	std::vector<std::vector<V3>> Rows;
	std::vector<V3> Tangents;
	auto Sign = [](double X) { return X > 0.0 ? 1.0 : X < 0.0 ? -1.0 : 0.0; };
	for (int I = 0; I < Count; ++I)
	{
		const Sec& S = Secs[I];
		V3 T = Secs[std::min(I + 1, Count - 1)].C - Secs[std::max(I - 1, 0)].C;
		T.x = 0.0;
		T = T.Length() > 0.0001 ? T.Normalized() : V3(0.0, 0.0, 1.0);
		const V3 Up = T.Cross(V3(1.0, 0.0, 0.0)).Normalized();
		const double E = 2.0 / S.N;
		std::vector<V3> Pts;
		for (int J = 0; J < Width; ++J)
		{
			const double A = Open ? A0 + Span * J / Steps : -3.141592653589793 * 0.5 + Tau * J / Steps;
			const double Ca = std::cos(A), Sa = std::sin(A);
			const double X = Sign(Ca) * std::pow(std::fabs(Ca), E) * S.W * (Sa < 0.0 ? S.Bw : 1.0);
			const double Y = Sign(Sa) * std::pow(std::fabs(Sa), E) * (Sa > 0.0 ? S.Up : S.Down);
			Pts.push_back(S.C + V3(X, 0.0, 0.0) + Up * Y);
		}
		Rows.push_back(Pts);
		Tangents.push_back(T);
	}
	for (int I = 0; I < Count; ++I)
		for (int J = 0; J < Width; ++J)
		{
			const int Jp = Open ? std::min(J + 1, Width - 1) : (J + 1) % Width, Jm = Open ? std::max(J - 1, 0) : (J - 1 + Width) % Width;
			const V3 Across = Rows[I][Jp] - Rows[I][Jm];
			const V3 Along = Rows[std::min(I + 1, Count - 1)][J] - Rows[std::max(I - 1, 0)][J];
			V3 Nrm = Across.Cross(Along).Normalized();
			if (Nrm.Dot(Rows[I][J] - Secs[I].C) < 0.0) Nrm = Nrm * -1.0;
			M.Add(Rows[I][J], Nrm, static_cast<double>(J) / std::max(Width - 1, 1), static_cast<double>(I) / (Count - 1));
		}
	for (int I = 0; I + 1 < Count; ++I)
		for (int J = 0; J < (Open ? Width - 1 : Width); ++J)
			M.Quad(I * Width + J, I * Width + (J + 1) % Width, (I + 1) * Width + J, (I + 1) * Width + (J + 1) % Width);
	if (Open)
	{
		// The inside face, its normals turned in (ToOurs winds each triangle to its normals).
		const size_t Base = M.P.size(), NIdx = M.Idx.size();
		for (size_t K = 0; K < Base; ++K) M.Add(M.P[K], M.N[K] * -1.0, M.U[K], M.V[K]);
		for (size_t K = 0; K < NIdx; ++K) M.Idx.push_back(M.Idx[K] + static_cast<unsigned>(Base));
		return M;
	}
	for (int End : {0, Count - 1})
	{
		const V3 Out = Tangents[End] * (End == 0 ? -1.0 : 1.0);
		const unsigned Mid = M.Add(Secs[End].C, Out, 0.5, 0.5);
		for (int J = 0; J < Width; ++J)
			M.Add(Rows[End][J], Out, 0.5 + 0.5 * std::cos(Tau * J / Width), 0.5 + 0.5 * std::sin(Tau * J / Width));
		for (int J = 0; J < Width; ++J) M.Idx.insert(M.Idx.end(), {Mid, Mid + 1 + J, Mid + 1 + (J + 1) % Width});
	}
	return M;
}

// A tube from A to B as BikeModel._tube draws it in code: a 12-sided cylinder, its node at the
// middle turned from up to the way A to B runs (straight down: half a turn about X).
inline GAffine TubeAt(const V3& A, const V3& B)
{
	const V3 Up(0.0, 1.0, 0.0), Dir = (B - A).Normalized();
	GAffine R;
	const V3 Axis = Up.Cross(Dir);
	if (Axis.Length() > 0.0001) R = GAffine::Rotation(Axis.Normalized(), std::atan2(Axis.Length(), Up.Dot(Dir)));
	else if (Dir.y < 0.0) R = GAffine::Rotation(V3(1.0, 0.0, 0.0), 3.141592653589793);
	R.O = (A + B) * 0.5;
	return R;
}
inline GMesh Tube(const V3& A, const V3& B, double R) { return Cylinder(R, std::max((B - A).Length(), 0.001), -1.0, 12); }

// ShapeKit.smooth: a Catmull-Rom curve through the points, PerSeg steps between each pair.
inline std::vector<V3> Smooth(const std::vector<V3>& P, int PerSeg = 6)
{
	if (P.size() < 3) return P;
	std::vector<V3> Out;
	const int N = static_cast<int>(P.size());
	for (int I = 0; I < N - 1; ++I)
	{
		const V3 P0 = P[std::max(I - 1, 0)], P1 = P[I], P2 = P[I + 1], P3 = P[std::min(I + 2, N - 1)];
		for (int K = 0; K < PerSeg; ++K)
		{
			const double T = static_cast<double>(K) / PerSeg, T2 = T * T, T3 = T2 * T;
			Out.push_back((P1 * 2.0 + (P2 - P0) * T + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P0 * -1.0 + P1 * 3.0 - P2 * 3.0 + P3) * T3) * 0.5);
		}
	}
	Out.push_back(P.back());
	return Out;
}

// ShapeKit.sweep: a round tube R thick bent smoothly through Path, Sides round, capped at both
// ends (parallel-transport frames, so it doesn't twist round the bends).
inline GMesh Sweep(const std::vector<V3>& Path, double R, int Sides = 12, bool Caps = true, int PerSeg = 6)
{
	GMesh M;
	const std::vector<V3> Pts = Smooth(Path, PerSeg);
	const int Count = static_cast<int>(Pts.size());
	if (Count < 2) return M;
	const V3 T0 = (Pts[1] - Pts[0]).Normalized();
	V3 Side = T0.Cross(V3(0.0, 1.0, 0.0));
	if (Side.Length() < 0.01) Side = T0.Cross(V3(1.0, 0.0, 0.0));
	Side = Side.Normalized();
	std::vector<V3> Tangents;
	for (int I = 0; I < Count; ++I)
	{
		const V3 T = (Pts[std::min(I + 1, Count - 1)] - Pts[std::max(I - 1, 0)]).Normalized();
		if (I > 0)
		{
			const V3 Prev = Tangents[I - 1], Axis = Prev.Cross(T);
			if (Axis.Length() > 0.00001) Side = GAffine::Rotation(Axis.Normalized(), std::atan2(Axis.Length(), Prev.Dot(T))).Apply(Side);
		}
		Side = (Side - T * Side.Dot(T)).Normalized();
		Tangents.push_back(T);
		const V3 Up = T.Cross(Side).Normalized();
		for (int J = 0; J <= Sides; ++J)
		{
			const double A = Tau * J / Sides;
			const V3 Nrm = Side * std::cos(A) + Up * std::sin(A);
			M.Add(Pts[I] + Nrm * R, Nrm, static_cast<double>(J) / Sides, static_cast<double>(I) / (Count - 1));
		}
	}
	const unsigned Ring = static_cast<unsigned>(Sides + 1);
	for (unsigned I = 0; I + 1 < static_cast<unsigned>(Count); ++I)
		for (unsigned J = 0; J < static_cast<unsigned>(Sides); ++J)
		{
			const unsigned A = I * Ring + J;
			M.Idx.insert(M.Idx.end(), {A, A + Ring, A + 1, A + 1, A + Ring, A + Ring + 1});
		}
	if (Caps)
		for (int End : {0, Count - 1})
		{
			const V3 Out = Tangents[End] * (End == 0 ? -1.0 : 1.0);
			const unsigned Mid = M.Add(Pts[End], Out, 0.5, 0.5);
			for (int J = 0; J < Sides; ++J) M.Add(M.P[End * Ring + J], Out, 0.5, 0.5);
			for (int J = 0; J < Sides; ++J) M.Idx.insert(M.Idx.end(), {Mid, Mid + 1 + J, Mid + 1 + (J + 1) % Sides});
		}
	return M;
}

// [TFT] A slab with rounded corners lying in X-Z (W across, H along Z), Thick through Y about its
// middle, its front (+Y) edge bevelled by Bevel: a screen's housing, its glass.
inline GMesh Slab(double W, double H, double Thick, double R, double Bevel = 0.0, int Corner = 6)
{
	GMesh M;
	R = std::max(0.0005, std::min(R, std::min(W, H) * 0.5));
	Bevel = std::max(0.0, std::min(Bevel, std::min(R * 0.9, Thick * 0.5)));
	// The outline, a corner at a time (the plain one and the one inside the bevel), with its normals.
	std::vector<V3> Edge, Inner, Nrm;
	for (int C = 0; C < 4; ++C)
	{
		const double Cx = (C == 0 || C == 3 ? 1.0 : -1.0) * (W * 0.5 - R), Cz = (C < 2 ? 1.0 : -1.0) * (H * 0.5 - R);
		for (int I = 0; I <= Corner; ++I)
		{
			const double A = (C + static_cast<double>(I) / Corner) * Tau * 0.25;
			const V3 N(std::cos(A), 0.0, std::sin(A));
			Edge.push_back(V3(Cx, 0.0, Cz) + N * R);
			Inner.push_back(V3(Cx, 0.0, Cz) + N * (R - Bevel));
			Nrm.push_back(N);
		}
	}
	const unsigned Count = static_cast<unsigned>(Edge.size());
	const double Y0 = -Thick * 0.5, Y1 = Thick * 0.5 - Bevel, Y2 = Thick * 0.5;
	// The sides.
	unsigned Base = static_cast<unsigned>(M.P.size());
	for (unsigned I = 0; I < Count; ++I)
	{
		M.Add(Edge[I] + V3(0.0, Y0, 0.0), Nrm[I]);
		M.Add(Edge[I] + V3(0.0, Y1, 0.0), Nrm[I]);
	}
	for (unsigned I = 0; I < Count; ++I)
	{
		const unsigned A = Base + I * 2, B = Base + ((I + 1) % Count) * 2;
		M.Idx.insert(M.Idx.end(), {A, B, A + 1, A + 1, B, B + 1});
	}
	// The bevel.
	if (Bevel > 0.0)
	{
		Base = static_cast<unsigned>(M.P.size());
		for (unsigned I = 0; I < Count; ++I)
		{
			const V3 N = (Nrm[I] + V3(0.0, 1.0, 0.0)).Normalized();
			M.Add(Edge[I] + V3(0.0, Y1, 0.0), N);
			M.Add(Inner[I] + V3(0.0, Y2, 0.0), N);
		}
		for (unsigned I = 0; I < Count; ++I)
		{
			const unsigned A = Base + I * 2, B = Base + ((I + 1) % Count) * 2;
			M.Idx.insert(M.Idx.end(), {A, B, A + 1, A + 1, B, B + 1});
		}
	}
	// The front and the back.
	for (int Side = 0; Side < 2; ++Side)
	{
		const V3 N(0.0, Side == 0 ? 1.0 : -1.0, 0.0);
		const std::vector<V3>& Ring = Side == 0 ? Inner : Edge;
		const double Y = Side == 0 ? Y2 : Y0;
		const unsigned Mid = M.Add(V3(0.0, Y, 0.0), N, 0.5, 0.5);
		for (unsigned I = 0; I < Count; ++I) M.Add(Ring[I] + V3(0.0, Y, 0.0), N, Ring[I].x / W + 0.5, Ring[I].z / H + 0.5);
		for (unsigned I = 0; I < Count; ++I) M.Idx.insert(M.Idx.end(), {Mid, Mid + 1 + I, Mid + 1 + (I + 1) % Count});
	}
	return M;
}

// [TFT] A screen's lit face in X-Z looking along +Y: W across, H along Z (the picture's top at -Z),
// or a disc W across. Its picture's corners are the face's (u across, v down).
inline GMesh ScreenFace(double W, double H, bool Round, int Segments = 48)
{
	GMesh M;
	const V3 N(0.0, 1.0, 0.0);
	if (!Round)
	{
		for (double Z : {-1.0, 1.0})
			for (double X : {-1.0, 1.0}) M.Add(V3(X * W * 0.5, 0.0, Z * H * 0.5), N, X * 0.5 + 0.5, Z * 0.5 + 0.5);
		M.Quad(0, 1, 2, 3);
		return M;
	}
	const unsigned Mid = M.Add(V3(), N, 0.5, 0.5);
	for (int I = 0; I < Segments; ++I)
	{
		const double A = Tau * I / Segments;
		M.Add(V3(std::cos(A) * W * 0.5, 0.0, std::sin(A) * W * 0.5), N, std::cos(A) * 0.5 + 0.5, std::sin(A) * 0.5 + 0.5);
	}
	for (unsigned I = 0; I < static_cast<unsigned>(Segments); ++I) M.Idx.insert(M.Idx.end(), {Mid, Mid + 1 + I, Mid + 1 + (I + 1) % Segments});
	return M;
}

// Godot's names for siblings of the same name: the first as it is, then Name2, Name3...
struct GodotNames
{
	std::map<std::string, int> Seen;
	std::string operator()(const std::string& Name)
	{
		if (Name.empty()) return Name;
		const int K = ++Seen[Name];
		return K > 1 ? Name + std::to_string(K) : Name;
	}
};

// The mesh placed by Xf (a Godot node's transform in the bike's space at rest) into one of our
// surfaces tagged Tag, each triangle wound to face along its normals.
inline PartSurface ToOurs(const GMesh& M, const GAffine& Xf, const std::string& Tag)
{
	PartSurface S;
	S.Material = Tag;
	// (Normals by the inverse transpose, for a scaled node: columns Y x Z, Z x X, X x Y.)
	const V3 Nx = Xf.Y.Cross(Xf.Z), Ny = Xf.Z.Cross(Xf.X), Nz = Xf.X.Cross(Xf.Y);
	for (size_t I = 0; I < M.P.size(); ++I)
	{
		S.Pos.push_back(FromGodot(Xf.Apply(M.P[I])));
		S.Nrm.push_back(FromGodot((Nx * M.N[I].x + Ny * M.N[I].y + Nz * M.N[I].z).Normalized()));
		S.U.push_back(M.U[I]);
		S.V.push_back(M.V[I]);
	}
	for (size_t K = 0; K + 2 < M.Idx.size(); K += 3)
	{
		const unsigned A = M.Idx[K], B = M.Idx[K + 1], C = M.Idx[K + 2];
		const V3 Face = (S.Pos[B] - S.Pos[A]).Cross(S.Pos[C] - S.Pos[A]);
		if (Face.Dot(S.Nrm[A] + S.Nrm[B] + S.Nrm[C]) >= 0.0) S.Idx.insert(S.Idx.end(), {A, B, C});
		else S.Idx.insert(S.Idx.end(), {A, C, B});
	}
	return S;
}
// Puts it on: a FittedPart.Made marked Code, in Slot, moving with Node, Glb a stand-in from the
// same family (so a loader that wants a glb has one), Name Godot's mesh's.
inline void Emit(std::vector<FittedPart>& Out, const std::string& Slot, const std::string& Glb, const std::string& Node, const std::string& Name,
	const GMesh& M, const GAffine& Xf, const std::string& Tag)
{
	auto Made = std::make_shared<PartMesh>();
	Made->Surfaces.push_back(ToOurs(M, Xf, Tag));
	FittedPart F;
	F.Slot = Slot;
	F.Glb = Glb;
	F.Node = Node;
	F.Made = Made;
	F.Code = true;
	F.Name = Name;
	Out.push_back(F);
}
} // namespace CodeShapes
} // namespace TorqueFit
