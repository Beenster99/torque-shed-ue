// Helpers the Blender part placements share (Fit/BikeParts.cpp, Fit/BikeFrame.cpp): Godot's
// basis builders and ShapeKit's curve smoothing, in the Godot frame (x right, y up, -z forward).
#pragma once

#include "BikeParts.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace TorqueFit
{
namespace PartsKit
{
inline std::string PartIn(const PartsMap& P, const std::string& Slot)
{
	auto It = P.find(Slot);
	return It == P.end() || It->second.empty() ? std::string("none") : It->second;
}

inline const V3 GodotUp(0.0, 1.0, 0.0);

inline double AngleTo(const V3& A, const V3& B)
{
	return std::atan2(A.Cross(B).Length(), A.Dot(B));
}

inline double PartNum(const Json* P, const char* Key, double Default)
{
	return P ? P->Num(Key, Default) : Default;
}

inline std::string PartStr(const Json* P, const char* Key, const std::string& Default)
{
	return P ? P->Str(Key, Default) : Default;
}

// Godot's Basis(x, y, z) from its columns, and a transform at an origin.
inline GAffine BasisCols(const V3& X, const V3& Y, const V3& Z, const V3& O = V3())
{
	GAffine A;
	A.X = X;
	A.Y = Y;
	A.Z = Z;
	A.O = O;
	return A;
}

inline GAffine AtWith(const GAffine& Basis, const V3& Origin)
{
	GAffine A = Basis;
	A.O = Origin;
	return A;
}

// BikeModel._basis_up: turned from straight up to `Up`.
inline GAffine BasisUp(const V3& Up)
{
	const V3 Axis = GodotUp.Cross(Up);
	if (Axis.Length() < 0.0001) return GAffine();
	return GAffine::Rotation(Axis.Normalized(), AngleTo(GodotUp, Up));
}

inline const V3 GodotRight(1.0, 0.0, 0.0);

inline PartMesh::Warp GodotWarpToOurs(std::function<V3(const V3&)> GodotWarp)
{
	return [GodotWarp](const V3& Ours) { return FromGodot(GodotWarp(ToGodot(Ours))); };
}
// BikeModel.rim_width_of, tread_pattern and sprocket_radius.
inline double RimWidthOf(const Json* Wheel) { return PartNum(Wheel, "rim_width", 3.5) * 0.0254; }
inline std::string TreadPattern(const Json* Tyre)
{
	const std::string Style = PartStr(Tyre, "style", "road");
	if (Style == "slick") return "";
	return PartStr(Tyre, "tread", Style == "offroad" ? "knobs" : "sport");
}
// ShapeKit.smooth and smooth_floats: a Catmull-Rom curve through the points, PerSeg to a span.
inline std::vector<V3> SmoothCurve(const std::vector<V3>& P, int PerSeg)
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
			Out.push_back((P1 * 2.0 + (P2 - P0) * T + (P0 * 2.0 - P1 * 5.0 + P2 * 4.0 - P3) * T2 + (P1 * 3.0 - P0 - P2 * 3.0 + P3) * T3) * 0.5);
		}
	}
	Out.push_back(P.back());
	return Out;
}

inline std::vector<double> SmoothFloats(const std::vector<double>& V, int PerSeg)
{
	if (V.size() < 2) return V;
	std::vector<double> Out;
	for (size_t I = 0; I + 1 < V.size(); ++I)
		for (int K = 0; K < PerSeg; ++K) Out.push_back(TorqueSim::Lerp(V[I], V[I + 1], static_cast<double>(K) / PerSeg));
	Out.push_back(V.back());
	return Out;
}

inline V3 MixV(const V3& A, const V3& B, double F) { return A + (B - A) * F; }

// BikeModel._basis_along: +Z along Dir, +Y as near straight up as that leaves.
inline GAffine BasisAlong(const V3& Dir)
{
	const V3 Z = Dir.Normalized();
	V3 X = GodotUp.Cross(Z);
	X = X.Length() < 0.001 ? GodotRight : X.Normalized();
	return BasisCols(X, Z.Cross(X).Normalized(), Z);
}
} // namespace PartsKit
} // namespace TorqueFit
