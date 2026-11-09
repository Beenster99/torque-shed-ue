#include "BikeBadge.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace TorqueFit
{
namespace BadgeArtImpl
{
using BadgeArt::P2;
using BadgeArt::Ring;
using BadgeArt::Shape;

constexpr double BadgePi = 3.14159265358979323846;

P2 BadgeAngle(double A, double R) { return {std::cos(A) * R, std::sin(A) * R}; }

Ring BadgeCircle(double R, P2 At = P2())
{
	Ring Out;
	for (int I = 0; I < 48; ++I)
	{
		const P2 P = BadgeAngle(2.0 * BadgePi * I / 48.0, R);
		Out.push_back({At.X + P.X, At.Y + P.Y});
	}
	return Out;
}

Ring BadgeStar(int N, double ROut, double RIn, double Turn)
{
	Ring Out;
	for (int I = 0; I < N * 2; ++I) Out.push_back(BadgeAngle(Turn + BadgePi * I / N, I % 2 == 0 ? ROut : RIn));
	return Out;
}

Ring BadgeTurned(const Ring& Pts, double A)
{
	Ring Out;
	const double C = std::cos(A), S = std::sin(A);
	for (const P2& V : Pts) Out.push_back({V.X * C - V.Y * S, V.X * S + V.Y * C});
	return Out;
}

Ring BadgeRect(double X0, double Y0, double X1, double Y1) { return {{X0, Y0}, {X1, Y0}, {X1, Y1}, {X0, Y1}}; }

Ring BadgePts(std::initializer_list<P2> L) { return Ring(L); }

double BadgeClamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
int BadgeClampI(int V, int Lo, int Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

// Godot's roundi (half away from zero) and floori.
int BadgeRound(double V) { return (int)std::lround(V); }
int BadgeFloor(double V) { return (int)std::floor(V); }
} // namespace BadgeArtImpl

namespace BAI = BadgeArtImpl;

namespace BadgeArt
{
void Fill(Mask& M, const std::vector<Ring>& Polys, unsigned char Value)
{
	double Lo = std::numeric_limits<double>::infinity(), Hi = -std::numeric_limits<double>::infinity();
	for (const Ring& Pts : Polys)
		for (const P2& P : Pts)
		{
			Lo = std::min(Lo, P.Y);
			Hi = std::max(Hi, P.Y);
		}
	if (Lo > Hi) return;
	const int YEnd = std::min(M.H, (int)std::ceil(Hi) + 1);
	std::vector<double> Xs;
	for (int Y = std::max(0, BAI::BadgeFloor(Lo)); Y < YEnd; ++Y)
	{
		const double Py = Y + 0.5;
		Xs.clear();
		for (const Ring& Pts : Polys)
		{
			const size_t N = Pts.size();
			for (size_t I = 0; I < N; ++I)
			{
				const P2& A = Pts[I];
				const P2& B = Pts[(I + 1) % N];
				if ((A.Y <= Py) != (B.Y <= Py)) Xs.push_back(A.X + (Py - A.Y) / (B.Y - A.Y) * (B.X - A.X));
			}
		}
		std::sort(Xs.begin(), Xs.end());
		for (size_t K = 0; K + 1 < Xs.size(); K += 2)
		{
			const int X0 = BAI::BadgeClampI(BAI::BadgeRound(Xs[K]), 0, M.W);
			const int X1 = BAI::BadgeClampI(BAI::BadgeRound(Xs[K + 1]), 0, M.W);
			for (int X = X0; X < X1; ++X) M.A[(size_t)Y * (size_t)M.W + (size_t)X] = Value;
		}
	}
}

Mask Shrink(const Mask& M)
{
	Mask Out;
	Out.W = std::max(1, M.W / 2);
	Out.H = std::max(1, M.H / 2);
	Out.A.assign((size_t)Out.W * (size_t)Out.H, 0);
	for (int Y = 0; Y < Out.H; ++Y)
		for (int X = 0; X < Out.W; ++X)
		{
			const int Sum = M.At(X * 2, Y * 2) + M.At(X * 2 + 1, Y * 2) + M.At(X * 2, Y * 2 + 1) + M.At(X * 2 + 1, Y * 2 + 1);
			Out.A[(size_t)Y * (size_t)Out.W + (size_t)X] = (unsigned char)((Sum + 2) / 4);
		}
	return Out;
}

std::vector<Shape> LogoShapes(const std::string& Id)
{
	using BAI::BadgeCircle;
	using BAI::BadgePts;
	using BAI::BadgeRect;
	std::vector<Shape> Out;
	auto Add = [&Out](bool bOn, const Ring& R) { Out.push_back({bOn, R}); };
	if (Id == "star") Add(true, BAI::BadgeStar(5, 0.95, 0.4, -BAI::BadgePi / 2.0));
	else if (Id == "shield")
	{
		const Ring Outer = BadgePts({{-0.8, -0.85}, {0.8, -0.85}, {0.8, 0.0}, {0.45, 0.6}, {0.0, 0.95}, {-0.45, 0.6}, {-0.8, 0.0}});
		Ring Inner;
		for (const P2& V : Outer) Inner.push_back({V.X * 0.78, V.Y * 0.78 - 0.02});
		Add(true, Outer);
		Add(false, Inner);
		Add(true, BadgePts({{-0.55, -0.35}, {0.0, 0.05}, {0.55, -0.35}, {0.55, -0.1}, {0.0, 0.3}, {-0.55, -0.1}}));
	}
	else if (Id == "bolt")
		Add(true, BadgePts({{0.25, -0.95}, {-0.55, 0.12}, {-0.05, 0.12}, {-0.3, 0.95}, {0.6, -0.2}, {0.08, -0.2}, {0.38, -0.95}}));
	else if (Id == "roundel")
	{
		Add(true, BadgeCircle(0.95));
		Add(false, BadgeCircle(0.75));
		Add(true, BadgeCircle(0.5));
		Add(true, BadgePts({{-0.98, -0.16}, {0.98, -0.16}, {0.98, 0.16}, {-0.98, 0.16}}));
	}
	else if (Id == "diamond")
	{
		auto D = [](double R) { return BadgePts({{0.0, -R}, {R * 0.8, 0.0}, {0.0, R}, {-R * 0.8, 0.0}}); };
		Add(true, D(0.97));
		Add(false, D(0.72));
		Add(true, D(0.45));
	}
	else if (Id == "piston")
	{
		Add(true, BadgeRect(-0.6, -0.9, 0.6, -0.15));
		Add(false, BadgeRect(-0.6, -0.72, 0.6, -0.65));
		Add(false, BadgeRect(-0.6, -0.55, 0.6, -0.48));
		Add(true, BadgeRect(-0.14, -0.2, 0.14, 0.75));
		Add(true, BadgeCircle(0.26, {0.0, 0.72}));
		Add(false, BadgeCircle(0.12, {0.0, 0.72}));
	}
	else if (Id == "flame")
	{
		// A flame of three tongues, licked out of its middle.
		Add(true, BadgePts({{0.0, -0.95}, {0.16, -0.5}, {0.4, -0.72}, {0.44, -0.28}, {0.66, -0.42}, {0.62, 0.12}, {0.5, 0.55}, {0.25, 0.86},
			{0.0, 0.95}, {-0.25, 0.86}, {-0.5, 0.55}, {-0.62, 0.12}, {-0.66, -0.42}, {-0.44, -0.28}, {-0.4, -0.72}, {-0.16, -0.5}}));
		Add(false, BadgePts({{0.0, -0.15}, {0.18, 0.25}, {0.16, 0.55}, {0.0, 0.68}, {-0.16, 0.55}, {-0.18, 0.25}}));
	}
	else if (Id == "crown")
	{
		Add(true, BadgePts({{-0.85, 0.65}, {-0.95, -0.55}, {-0.45, -0.05}, {0.0, -0.8}, {0.45, -0.05}, {0.95, -0.55}, {0.85, 0.65}}));
		Add(true, BadgeRect(-0.85, 0.72, 0.85, 0.88));
	}
	else if (Id == "spanners")
	{
		for (const double A : {BAI::BadgePi / 4.0, -BAI::BadgePi / 4.0})
		{
			Add(true, BAI::BadgeTurned(BadgeRect(-0.11, -0.7, 0.11, 0.7), A));
			for (const double End : {-0.72, 0.72})
			{
				Add(true, BAI::BadgeTurned(BadgeCircle(0.24, {0.0, End}), A));
				// The jaw: a slot cut out of the ring's far side.
				const double Y0 = End < 0.0 ? End - 0.3 : End - 0.05;
				Add(false, BAI::BadgeTurned(BadgeRect(-0.09, Y0, 0.09, Y0 + 0.35), A));
			}
		}
	}
	else if (Id == "wings")
	{
		// A round boss with a swept wing either side, feathered underneath.
		const Ring Wing = BadgePts({{0.15, -0.2}, {0.98, -0.42}, {0.92, -0.22}, {0.78, -0.18}, {0.72, -0.02}, {0.56, 0.0}, {0.5, 0.14}, {0.34, 0.14}, {0.15, 0.22}});
		Ring Left;
		for (const P2& V : Wing) Left.push_back({-V.X, V.Y});
		Add(true, Wing);
		Add(true, Left);
		Add(true, BadgeCircle(0.3));
		Add(false, BadgeCircle(0.18));
	}
	return Out;
}

Mask LogoMask(const std::string& Id)
{
	const std::vector<Shape> Shapes = LogoShapes(Id);
	if (Shapes.empty()) return Mask();
	Mask Big;
	Big.W = Big.H = LogoPx * 2;
	Big.A.assign((size_t)Big.W * (size_t)Big.H, 0);
	for (const Shape& S : Shapes)
	{
		Ring Px;
		for (const P2& P : S.Pts) Px.push_back({(P.X + 1.0) * 0.5 * Big.W, (P.Y + 1.0) * 0.5 * Big.H});
		Fill(Big, {Px}, S.On ? 255 : 0);
	}
	return Shrink(Big);
}

const Lettering& FontOf(const std::string& Id)
{
	// The plain one is the game's own font in Godot (Open Sans SemiBold): the same here, from a file.
	static const std::vector<Lettering> L = {{"plain", "OpenSans-SemiBold.ttf", true}, {"bebas", "BebasNeue-Regular.ttf", true},
		{"racing", "RacingSansOne-Regular.ttf", true}, {"russo", "RussoOne-Regular.ttf", true}, {"audiowide", "Audiowide-Regular.ttf", true},
		{"blackops", "BlackOpsOne-Regular.ttf", true}, {"graduate", "Graduate-Regular.ttf", true}, {"righteous", "Righteous-Regular.ttf", false},
		{"playfair", "PlayfairDisplay.ttf", false}, {"lobster", "Lobster-Regular.ttf", false}, {"pacifico", "Pacifico-Regular.ttf", false},
		{"kaushan", "KaushanScript-Regular.ttf", false}};
	for (const Lettering& F : L)
		if (F.Id == Id) return F;
	return L.front();
}

std::string BrandText(const Shop::Badge& B)
{
	std::string T = B.Brand;
	if (FontOf(B.Font.empty() ? std::string("plain") : B.Font).Caps)
		for (char& C : T)
			if (C >= 'a' && C <= 'z') C = (char)(C - 'a' + 'A');
	return T;
}

Box3 BoundsOf(const std::vector<V3>& Faces)
{
	Box3 B;
	if (Faces.empty()) return B;
	B.Min = B.Max = Faces.front();
	for (const V3& P : Faces)
	{
		B.Min = {std::min(B.Min.x, P.x), std::min(B.Min.y, P.y), std::min(B.Min.z, P.z)};
		B.Max = {std::max(B.Max.x, P.x), std::max(B.Max.y, P.y), std::max(B.Max.z, P.z)};
	}
	return B;
}

std::vector<Pic> Place(const Shop::Badge& In, const Box3& Box, double BrandAspect)
{
	std::vector<Pic> Out;
	const Shop::Badge B = Shop::CleanBadge(In);
	const V3 Size = Box.Max - Box.Min;
	if (B.Empty() || Size.x <= 0.01 || Size.z <= 0.05) return Out;
	const double H = Size.y, Length = Size.z;
	const double Y = (Box.Min.y + Box.Max.y) * 0.5 + H * 0.08;
	const bool bLogo = !B.Logo.empty(), bBrand = !B.Brand.empty();
	const double LogoSize = std::min(H * 0.55, Length * 0.32);
	// Along the tank (z, its nose at the smaller z): the logo ahead of the brand, or either alone in
	// the middle, then each sized and moved as asked.
	const double Mid = (Box.Min.z + Box.Max.z) * 0.5;
	const double LogoZ = Mid - (bBrand ? Length * 0.2 : 0.0);
	if (bLogo)
	{
		const double Ls = LogoSize * B.LogoSize;
		const double Z = LogoZ + B.LogoX * Length, Up = Y + B.LogoY * H;
		Out.push_back({true, Z - Ls * 0.5, Up - Ls * 0.5, Ls, Ls});
	}
	if (bBrand)
	{
		const double Aspect = std::max(BrandAspect, 0.01);
		// The letters a fifth of the tank's height, smaller still if that's too long to fit.
		double Tall = H * 0.2 * (1.0 + Border * 2.0);
		const double Room = Length * (bLogo ? 0.5 : 0.8) * (1.0 + Border * 2.0 / Aspect);
		if (Tall * Aspect > Room) Tall = Room / Aspect;
		Tall *= B.BrandSize;
		const double Wide = Tall * Aspect;
		// Its middle just behind where the logo comes (towards the seat: larger z), or the tank's
		// middle; sizing or moving the logo leaves it be.
		const double Bz = bLogo ? LogoZ + LogoSize * 0.5 + Wide * 0.5 : Mid;
		const double Z = Bz + B.BrandX * Length, Up = Y + B.BrandY * H;
		Out.push_back({false, Z - Wide * 0.5, Up - Tall * 0.5, Wide, Tall});
	}
	return Out;
}

bool DecalSkin(const std::vector<V3>& Faces, double Side, double MidX, const Pic& R, Skin& Out)
{
	Out = Skin();
	struct FTri
	{
		V3 P[3];
		V3 N;
	};
	std::vector<FTri> Picked;
	constexpr int Cells = 16;
	std::vector<double> Depth((size_t)(Cells * Cells), -std::numeric_limits<double>::infinity());
	const double CellX = R.W / Cells, CellY = R.H / Cells;
	if (CellX <= 0.0 || CellY <= 0.0) return false;
	const double EndX = R.X + R.W, EndY = R.Y + R.H;
	for (size_t F = 0; F + 2 < Faces.size(); F += 3)
	{
		const V3 &A = Faces[F], &B = Faces[F + 1], &C = Faces[F + 2];
		V3 N = (B - A).Cross(C - A);
		if (N.Dot(N) < 1e-14) continue;
		N = N.Normalized();
		if (std::fabs(N.x) < 0.2 || ((A.x + B.x + C.x) / 3.0 - MidX) * Side <= 0.0) continue;
		const double LoZ = std::min(A.z, std::min(B.z, C.z)), LoY = std::min(A.y, std::min(B.y, C.y));
		const double HiZ = std::max(A.z, std::max(B.z, C.z)), HiY = std::max(A.y, std::max(B.y, C.y));
		if (HiZ < R.X || LoZ > EndX || HiY < R.Y || LoY > EndY) continue;
		const V3 OutN = N * ((N.x > 0.0 ? 1.0 : -1.0) * Side);
		Picked.push_back({{A, B, C}, OutN});
		// How far out the surface comes over each cell it spans, to keep only the outer skin.
		const double Far = std::max(A.x * Side, std::max(B.x * Side, C.x * Side));
		const int I0 = BAI::BadgeClampI(BAI::BadgeFloor((LoZ - R.X) / CellX), 0, Cells - 1);
		const int I1 = BAI::BadgeClampI(BAI::BadgeFloor((HiZ - R.X) / CellX), 0, Cells - 1);
		const int J0 = BAI::BadgeClampI(BAI::BadgeFloor((LoY - R.Y) / CellY), 0, Cells - 1);
		const int J1 = BAI::BadgeClampI(BAI::BadgeFloor((HiY - R.Y) / CellY), 0, Cells - 1);
		for (int I = I0; I <= I1; ++I)
			for (int J = J0; J <= J1; ++J)
			{
				double& D = Depth[(size_t)(J * Cells + I)];
				D = std::max(D, Far);
			}
	}
	if (Picked.empty()) return false;
	// A point (rounded) -> its summed normal, for smooth shading.
	using FKey = std::tuple<long long, long long, long long>;
	auto KeyOf = [](const V3& P) { return FKey((long long)(P.x * 2000.0), (long long)(P.y * 2000.0), (long long)(P.z * 2000.0)); };
	std::map<FKey, V3> Normals;
	std::vector<const FTri*> Keep;
	for (const FTri& T : Picked)
	{
		const V3 MidT = (T.P[0] + T.P[1] + T.P[2]) / 3.0;
		const int I = BAI::BadgeClampI(BAI::BadgeFloor((MidT.z - R.X) / CellX), 0, Cells - 1);
		const int J = BAI::BadgeClampI(BAI::BadgeFloor((MidT.y - R.Y) / CellY), 0, Cells - 1);
		const double Near = std::max(T.P[0].x * Side, std::max(T.P[1].x * Side, T.P[2].x * Side));
		if (Near < Depth[(size_t)(J * Cells + I)] - 0.02) continue;   // something further out covers it
		Keep.push_back(&T);
		for (const V3& P : T.P) Normals[KeyOf(P)] += T.N;
	}
	if (Keep.empty()) return false;
	for (const FTri* T : Keep)
	{
		// Wound so (b - a) x (c - a) points out of the tank, whichever way the tank's own go.
		const bool bAsIs = (T->P[1] - T->P[0]).Cross(T->P[2] - T->P[0]).Dot(T->N) >= 0.0;
		const int Order[3] = {0, bAsIs ? 1 : 2, bAsIs ? 2 : 1};
		for (const int K : Order)
		{
			const V3& P = T->P[K];
			const V3 N = Normals[KeyOf(P)].Normalized();
			// The picture as seen from this side: left to right is towards the nose on the right
			// side (the smaller z) and towards the seat on the left.
			double U = (P.z - R.X) / R.W;
			if (Side > 0.0) U = 1.0 - U;
			Out.Idx.push_back((unsigned)Out.Pos.size());
			Out.Pos.push_back(P + N * Lift);
			Out.Nrm.push_back(N);
			Out.U.push_back(U);
			Out.V.push_back((EndY - P.y) / R.H);
		}
	}
	return true;
}
} // namespace BadgeArt
} // namespace TorqueFit
