// Small double-precision vector and rotation maths for the bike simulation.
//
// The simulation is plain C++ with no Unreal types, so it can be built and tested on its own
// (SimTest/) and behaves the same in the game. It keeps the Godot game's conventions so the port
// can be checked line by line against scripts/bike.gd: metres, +Y up, -Z forward, +X right
// (right-handed). TorqueBikePawn converts to and from Unreal's frame at the boundary.
#pragma once

#include <algorithm>
#include <cmath>

namespace TorqueSim
{
constexpr double Pi = 3.14159265358979323846;
constexpr double TAU = 2.0 * Pi;

inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double Sign(double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); }
inline double DegToRad(double d) { return d * Pi / 180.0; }
inline double MoveToward(double from, double to, double delta)
{
	return std::fabs(to - from) <= delta ? to : from + Sign(to - from) * delta;
}
inline double SmoothStep(double from, double to, double x)
{
	if (from == to) return x < from ? 0.0 : 1.0;
	double t = Clamp((x - from) / (to - from), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

struct V3
{
	double x = 0.0, y = 0.0, z = 0.0;
	V3() = default;
	V3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
	V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
	V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
	V3 operator-() const { return {-x, -y, -z}; }
	V3 operator*(double s) const { return {x * s, y * s, z * s}; }
	V3 operator/(double s) const { return {x / s, y / s, z / s}; }
	V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
	V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	double Dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
	V3 Cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
	double Length() const { return std::sqrt(x * x + y * y + z * z); }
	V3 Normalized() const { double l = Length(); return l > 1e-12 ? *this / l : V3(); }
	// Rotated about a unit axis by an angle (radians, right-handed), as Godot's Vector3.rotated.
	V3 Rotated(const V3& axis, double angle) const
	{
		double c = std::cos(angle), s = std::sin(angle);
		return *this * c + axis.Cross(*this) * s + axis * (axis.Dot(*this) * (1.0 - c));
	}
};
inline V3 operator*(double s, const V3& v) { return v * s; }

const V3 UP(0.0, 1.0, 0.0);
const V3 RIGHT(1.0, 0.0, 0.0);

// A rotation as three column vectors: the body's own x (right), y (up) and z (back) axes in the
// world, as Godot's Basis (b.x, b.y, b.z).
struct M3
{
	V3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
	M3() = default;
	M3(const V3& x, const V3& y, const V3& z) : X(x), Y(y), Z(z) {}
	V3 operator*(const V3& v) const { return X * v.x + Y * v.y + Z * v.z; }
	M3 operator*(const M3& o) const { return {*this * o.X, *this * o.Y, *this * o.Z}; }
	M3 Transposed() const { return {{X.x, Y.x, Z.x}, {X.y, Y.y, Z.y}, {X.z, Y.z, Z.z}}; }
	static M3 AxisAngle(const V3& axis, double angle)
	{
		return {RIGHT.Rotated(axis, angle), UP.Rotated(axis, angle), V3(0, 0, 1).Rotated(axis, angle)};
	}
	void Orthonormalize()
	{
		X = X.Normalized();
		Y = (Y - X * X.Dot(Y)).Normalized();
		Z = X.Cross(Y);
	}
};
} // namespace TorqueSim
