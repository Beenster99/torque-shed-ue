// The scenery's own small helpers (shared by WorldRideScenery.cpp and WorldRideBuildings.cpp): its
// 256 m squares' keys, a hash of a place, a point, a colour, and flat-shaded quads, discs and boxes
// made into a RideMesh. Internal to the ride's planning; include only from World/WorldRide*.cpp.
#pragma once

#include "WorldRide.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace TorqueWorld
{
namespace RideSceneBits
{
inline constexpr double SceneCell = 256.0;

inline long long SceneKey(double X, double Z)
{
	return static_cast<long long>(std::floor(X / SceneCell)) * 4000037LL + static_cast<long long>(std::floor(Z / SceneCell));
}

// A number from 0 to 1 that is always the same for the same place (and Salt).
inline double Chance(double X, double Z, int Salt)
{
	uint32_t H = static_cast<uint32_t>(static_cast<int32_t>(std::floor(X * 8.0))) * 73856093u ^ static_cast<uint32_t>(static_cast<int32_t>(std::floor(Z * 8.0))) * 19349663u
		^ static_cast<uint32_t>(Salt) * 83492791u;
	H ^= H >> 13;
	H *= 0x5bd1e995u;
	H ^= H >> 15;
	return double(H & 0xffffffu) / double(0x1000000);
}

struct P3
{
	double X = 0.0, Y = 0.0, Z = 0.0;
};

inline P3 At3(double X, double Y, double Z)
{
	P3 Q;
	Q.X = X; Q.Y = Y; Q.Z = Z;
	return Q;
}

struct Rgb
{
	float R = 0.0f, G = 0.0f, B = 0.0f;
};

inline Rgb Shade(const Rgb& C, double By)
{
	Rgb Out;
	Out.R = float(C.R * By); Out.G = float(C.G * By); Out.B = float(C.B * By);
	return Out;
}

// A colour the map gives (OpenStreetMap's colour tags: "#rrggbb", "#rgb" or a plain name), as a
// linear colour toned to what paint and weathered materials really look like: mappers' "#FF0000"
// is a red brick or a red roof, not a postbox. False if it says nothing usable.
inline bool BuildingTagColour(const std::string& Word, Rgb& Out)
{
	if (Word.empty()) return false;
	double C[3] = {0.0, 0.0, 0.0};
	const auto Hex = [](char Ch) { return Ch >= '0' && Ch <= '9' ? Ch - '0' : Ch >= 'a' && Ch <= 'f' ? Ch - 'a' + 10 : Ch >= 'A' && Ch <= 'F' ? Ch - 'A' + 10 : -1; };
	if (Word[0] == '#' && (Word.size() == 7 || Word.size() == 4))
	{
		const bool Short = Word.size() == 4;
		for (int K = 0; K < 3; ++K)
		{
			const int Hi = Hex(Word[size_t(Short ? 1 + K : 1 + K * 2)]), Lo = Hex(Word[size_t(Short ? 1 + K : 2 + K * 2)]);
			if (Hi < 0 || Lo < 0) return false;
			C[K] = (Hi * 16 + Lo) / 255.0;
		}
	}
	else
	{
		struct Named { const char* Name; double R, G, B; };
		static const Named Names[] = {{"white", 0.95, 0.94, 0.90}, {"black", 0.12, 0.12, 0.12}, {"grey", 0.55, 0.55, 0.55}, {"gray", 0.55, 0.55, 0.55},
			{"red", 0.62, 0.25, 0.20}, {"brown", 0.45, 0.30, 0.22}, {"yellow", 0.85, 0.75, 0.45}, {"cream", 0.90, 0.85, 0.70}, {"beige", 0.82, 0.75, 0.62},
			{"green", 0.35, 0.45, 0.32}, {"blue", 0.35, 0.42, 0.55}, {"orange", 0.75, 0.45, 0.25}, {"pink", 0.85, 0.65, 0.62}, {"silver", 0.70, 0.70, 0.70}};
		std::string Low;
		for (char Ch : Word) Low += char(Ch >= 'A' && Ch <= 'Z' ? Ch - 'A' + 'a' : Ch);
		bool Found = false;
		for (const Named& N : Names)
			if (Low == N.Name)
			{
				C[0] = N.R; C[1] = N.G; C[2] = N.B;
				Found = true;
			}
		if (!Found) return false;
	}
	// (to linear, then halfway to its own grey and no brighter than limewash: weathered, not printed)
	for (double& V : C) V = V <= 0.04045 ? V / 12.92 : std::pow((V + 0.055) / 1.055, 2.4);
	const double Grey = 0.2126 * C[0] + 0.7152 * C[1] + 0.0722 * C[2];
	for (double& V : C) V = std::fmin(0.62, Grey + (V - Grey) * 0.5);
	Out.R = float(C[0]); Out.G = float(C[1]); Out.B = float(C[2]);
	return true;
}

// Four points as a quad that faces Toward (or both ways), flat shaded. Its UVs are metres of its own
// surface (across and up a wall, east and south on a flat top); Moss, 0 or more, is its vertex alpha.
inline void Quad(RideMesh& Out, const P3& Q0, const P3& Q1, const P3& Q2, const P3& Q3, const P3& Toward, const Rgb& Colour, bool BothSides = false, float Moss = -1.0f)
{
	const double Ax = Q2.X - Q0.X, Ay = Q2.Y - Q0.Y, Az = Q2.Z - Q0.Z, Bx = Q3.X - Q1.X, By = Q3.Y - Q1.Y, Bz = Q3.Z - Q1.Z;
	double Nx = Ay * Bz - Az * By, Ny = Az * Bx - Ax * Bz, Nz = Ax * By - Ay * Bx;
	const double Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
	if (Len < 1e-9) return;
	const bool Flip = Nx * (Toward.X - Q0.X) + Ny * (Toward.Y - Q0.Y) + Nz * (Toward.Z - Q0.Z) < 0.0;
	const double Sign = (Flip ? -1.0 : 1.0) / Len;
	Nx *= Sign; Ny *= Sign; Nz *= Sign;
	const uint32_t V = uint32_t(Out.Vertices());
	const bool Flat = std::fabs(Ny) > 0.7;
	const double Hl = std::sqrt(Nx * Nx + Nz * Nz);
	const double Tx = Hl > 1e-6 ? -Nz / Hl : 1.0, Tz = Hl > 1e-6 ? Nx / Hl : 0.0;   // (along a wall's face, level)
	for (const P3* Q : {&Q0, &Q1, &Q2, &Q3})
	{
		Out.Pos.push_back(float(Q->X - Out.OriginX)); Out.Pos.push_back(float(Q->Y)); Out.Pos.push_back(float(Q->Z - Out.OriginZ));
		Out.Nrm.push_back(float(Nx)); Out.Nrm.push_back(float(Ny)); Out.Nrm.push_back(float(Nz));
		// (kept small: a float's metres of UV stay sharp near the square, not 100 km from the cell's corner)
		const double Lx = Q->X - Out.OriginX, Lz = Q->Z - Out.OriginZ;
		Out.Uv.push_back(float(Flat ? Lx : Lx * Tx + Lz * Tz)); Out.Uv.push_back(float(Flat ? Lz : -Q->Y));
		Out.Col.push_back(Colour.R); Out.Col.push_back(Colour.G); Out.Col.push_back(Colour.B);
		if (Moss >= 0.0f) Out.Alpha.push_back(Moss);
	}
	if (!Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 1, V + 2, V, V + 2, V + 3});
	if (Flip || BothSides) Out.Idx.insert(Out.Idx.end(), {V, V + 2, V + 1, V, V + 3, V + 2});
}

// A flat disc of a sign facing Face (level, unit), its middle at C.
inline void Disc(RideMesh& Out, const P3& C, const P2& Face, double Radius, const Rgb& Colour, int Segments = 20)
{
	const uint32_t V = uint32_t(Out.Vertices());
	const P2 U(Face.Z, -Face.X);
	for (int K = -1; K < Segments; ++K)
	{
		const double A = 6.283185307179586 * K / Segments;
		const double X = K < 0 ? C.X : C.X + U.X * std::cos(A) * Radius, Y = K < 0 ? C.Y : C.Y + std::sin(A) * Radius, Z = K < 0 ? C.Z : C.Z + U.Z * std::cos(A) * Radius;
		Out.Pos.push_back(float(X - Out.OriginX)); Out.Pos.push_back(float(Y)); Out.Pos.push_back(float(Z - Out.OriginZ));
		Out.Nrm.push_back(float(Face.X)); Out.Nrm.push_back(0.0f); Out.Nrm.push_back(float(Face.Z));
		Out.Uv.push_back(float(X - Out.OriginX)); Out.Uv.push_back(float(-Y));
		Out.Col.push_back(Colour.R); Out.Col.push_back(Colour.G); Out.Col.push_back(Colour.B);
		if (!Out.Alpha.empty()) Out.Alpha.push_back(0.0f);
	}
	// (wound as Quad's are, to face the way (B - A) x (C - A) points: U x up is Face)
	for (int K = 0; K < Segments; ++K)
	{
		const uint32_t A = V + 1 + uint32_t(K), B = V + 1 + uint32_t((K + 1) % Segments);
		Out.Idx.insert(Out.Idx.end(), {V, A, B});
	}
}

// An upright box: its foot's middle at (X, Z), Along its length (unit), half Long by half Wide, from
// Lo to Hi over Base; its top too.
inline void Box(RideMesh& Out, double X, double Z, const P2& Along, double Long, double Wide, double Lo, double Hi, const Rgb& Colour)
{
	const P2 Across(Along.Z, -Along.X);
	const auto Corner = [&](double Su, double Sv, double Y) { return At3(X + Along.X * Su * Long + Across.X * Sv * Wide, Y, Z + Along.Z * Su * Long + Across.Z * Sv * Wide); };
	const P3 Mid = At3(X, (Lo + Hi) * 0.5, Z);
	const auto Away = [&Mid](const P3& A, const P3& C) { return At3(Mid.X + ((A.X + C.X) * 0.5 - Mid.X) * 50.0, Mid.Y, Mid.Z + ((A.Z + C.Z) * 0.5 - Mid.Z) * 50.0); };
	const double S[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
	for (int K = 0; K < 4; ++K)
	{
		const P3 A = Corner(S[K][0], S[K][1], Lo), B = Corner(S[K + 1][0], S[K + 1][1], Lo), C = Corner(S[K + 1][0], S[K + 1][1], Hi), D = Corner(S[K][0], S[K][1], Hi);
		Quad(Out, A, B, C, D, Away(A, C), Shade(Colour, K % 2 == 0 ? 1.0 : 0.85));
	}
	Quad(Out, Corner(-1, -1, Hi), Corner(1, -1, Hi), Corner(1, 1, Hi), Corner(-1, 1, Hi), At3(X, Hi + 100.0, Z), Shade(Colour, 1.1));
}
} // namespace RideSceneBits
} // namespace TorqueWorld
