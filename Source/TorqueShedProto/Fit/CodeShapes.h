// The shapes the Godot game draws in code rather than from Blender glbs (BikeModel._box,
// _cylinder, _sphere, _torus and _shell over ShapeKit.loft), made here with the same vertices so
// they come out the same size, then placed into a mesh of ours. Godot frame (x right, y up,
// -z forward) until ToOurs.
#pragma once

#include "BikeParts.h"

#include <cmath>
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
};

// ShapeKit.loft closed (28 round, flat caps at both ends), as BikeModel._shell draws an arm.
inline GMesh Loft(const std::vector<Sec>& Secs, int Ring = 28)
{
	GMesh M;
	const int Count = static_cast<int>(Secs.size());
	if (Count < 2) return M;
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
		for (int J = 0; J < Ring; ++J)
		{
			const double A = -3.141592653589793 * 0.5 + Tau * J / Ring;
			const double Ca = std::cos(A), Sa = std::sin(A);
			const double X = Sign(Ca) * std::pow(std::fabs(Ca), E) * S.W;
			const double Y = Sign(Sa) * std::pow(std::fabs(Sa), E) * (Sa > 0.0 ? S.Up : S.Down);
			Pts.push_back(S.C + V3(X, 0.0, 0.0) + Up * Y);
		}
		Rows.push_back(Pts);
		Tangents.push_back(T);
	}
	for (int I = 0; I < Count; ++I)
		for (int J = 0; J < Ring; ++J)
		{
			const V3 Across = Rows[I][(J + 1) % Ring] - Rows[I][(J - 1 + Ring) % Ring];
			const V3 Along = Rows[std::min(I + 1, Count - 1)][J] - Rows[std::max(I - 1, 0)][J];
			V3 Nrm = Across.Cross(Along).Normalized();
			if (Nrm.Dot(Rows[I][J] - Secs[I].C) < 0.0) Nrm = Nrm * -1.0;
			M.Add(Rows[I][J], Nrm, static_cast<double>(J) / (Ring - 1), static_cast<double>(I) / (Count - 1));
		}
	for (int I = 0; I + 1 < Count; ++I)
		for (int J = 0; J < Ring; ++J)
			M.Quad(I * Ring + J, I * Ring + (J + 1) % Ring, (I + 1) * Ring + J, (I + 1) * Ring + (J + 1) % Ring);
	for (int End : {0, Count - 1})
	{
		const V3 Out = Tangents[End] * (End == 0 ? -1.0 : 1.0);
		const unsigned Mid = M.Add(Secs[End].C, Out, 0.5, 0.5);
		for (int J = 0; J < Ring; ++J)
			M.Add(Rows[End][J], Out, 0.5 + 0.5 * std::cos(Tau * J / Ring), 0.5 + 0.5 * std::sin(Tau * J / Ring));
		for (int J = 0; J < Ring; ++J) M.Idx.insert(M.Idx.end(), {Mid, Mid + 1 + J, Mid + 1 + (J + 1) % Ring});
	}
	return M;
}

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
} // namespace CodeShapes
} // namespace TorqueFit
