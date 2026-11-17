// [OSM stage 6A] The sign system's lettering (see WorldRideSigns.h).
#include "WorldRideSigns.h"

#include "FitJson.h"

#include <cmath>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex GFontLock;
std::shared_ptr<const SignFont> GFont;
} // namespace

bool SetSignFont(const std::string& Text)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(Text, Root) || !Root.IsObject()) return false;
	const TorqueFit::Json* Glyphs = Root.Get("glyphs");
	if (Glyphs == nullptr || !Glyphs->IsObject()) return false;
	std::shared_ptr<SignFont> Font = std::make_shared<SignFont>();
	Font->Cap = float(Root.Num("cap", 0.714));
	Font->XHeight = float(Root.Num("xh", 0.536));
	Font->Space = float(Root.Num("space", 0.26));
	for (const auto& Entry : Glyphs->O)
	{
		const std::vector<uint32_t> Code = SignText::CodePoints(Entry.first);
		if (Code.size() != 1 || !Entry.second.IsObject()) continue;
		SignGlyph G;
		G.Width = float(Entry.second.Num("w", 0.5));
		if (const TorqueFit::Json* V = Entry.second.Get("v"))
			for (const TorqueFit::Json& N : V->A) G.Points.push_back(float(N.N));
		if (const TorqueFit::Json* I = Entry.second.Get("i"))
			for (const TorqueFit::Json& N : I->A)
				if (N.N >= 0.0 && size_t(N.N) * 2 + 1 < G.Points.size()) G.Index.push_back(uint32_t(N.N));
		G.Index.resize(G.Index.size() / 3 * 3);
		Font->Glyphs[Code[0]] = std::move(G);
	}
	if (Font->Glyphs.empty()) return false;
	std::lock_guard<std::mutex> Hold(GFontLock);
	GFont = Font;
	return true;
}

std::shared_ptr<const SignFont> SignFontNow()
{
	std::lock_guard<std::mutex> Hold(GFontLock);
	return GFont;
}

namespace
{
std::shared_ptr<const Route::Gazetteer> GPlaces;
} // namespace

bool SetSignPlaces(const std::string& Text)
{
	std::shared_ptr<Route::Gazetteer> Places = std::make_shared<Route::Gazetteer>();
	if (!Places->Load(Text) || Places->Count() == 0) return false;
	std::lock_guard<std::mutex> Hold(GFontLock);
	GPlaces = Places;
	return true;
}

std::shared_ptr<const Route::Gazetteer> SignPlacesNow()
{
	std::lock_guard<std::mutex> Hold(GFontLock);
	return GPlaces;
}

namespace SignText
{
std::vector<uint32_t> CodePoints(const std::string& Utf8)
{
	std::vector<uint32_t> Out;
	for (size_t I = 0; I < Utf8.size();)
	{
		const unsigned char C = static_cast<unsigned char>(Utf8[I]);
		const int More = C < 0x80 ? 0 : (C & 0xE0) == 0xC0 ? 1 : (C & 0xF0) == 0xE0 ? 2 : (C & 0xF8) == 0xF0 ? 3 : -1;
		if (More < 0 || I + size_t(More) >= Utf8.size())
		{
			Out.push_back(More == 0 ? C : 0xFFFDu);
			++I;
			continue;
		}
		uint32_t Code = More == 0 ? C : More == 1 ? (C & 0x1Fu) : More == 2 ? (C & 0x0Fu) : (C & 0x07u);
		bool Good = true;
		for (int K = 1; K <= More; ++K)
		{
			const unsigned char Cc = static_cast<unsigned char>(Utf8[I + size_t(K)]);
			if ((Cc & 0xC0) != 0x80) { Good = false; break; }
			Code = (Code << 6) | (Cc & 0x3Fu);
		}
		Out.push_back(Good ? Code : 0xFFFDu);
		I += Good ? size_t(More) + 1 : 1;
	}
	return Out;
}

namespace
{
// (the advance of each letter in em, the space's for a space or a letter the alphabet lacks)
bool Advances(const SignFont& Font, const std::vector<uint32_t>& Codes, std::vector<const SignGlyph*>& Glyphs, double& Em)
{
	Em = 0.0;
	Glyphs.clear();
	for (const uint32_t C : Codes)
	{
		if (C == 0x20u || C == 0xA0u)
		{
			Glyphs.push_back(nullptr);
			Em += Font.Space;
			continue;
		}
		auto It = Font.Glyphs.find(C);
		if (It == Font.Glyphs.end())
		{
			// ([roundabouts] beenster, Nov 17: "check that we no longer use 3D letters for signs". A letter
			// the baked alphabet lacks no longer sends the whole sign to a text component, the floating 3D
			// letters: Welsh's w and y with a grave, acute or diaeresis are set as the plain letter, any other
			// as the nearest the alphabet has, a dash for a dash, else left as a space.)
			uint32_t Plain = 0;
			if (C >= 0x1E80u && C <= 0x1E85u) Plain = (C % 2u == 0u) ? uint32_t('W') : uint32_t('w');
			else if (C >= 0x1EF2u && C <= 0x1EF9u) Plain = (C % 2u == 0u) ? uint32_t('Y') : uint32_t('y');
			else if (C == 0x2010u || C == 0x2011u || C == 0x2012u || C == 0x2212u) Plain = uint32_t('-');
			else if (C == 0x2018u || C == 0x2019u || C == 0x02BCu) Plain = uint32_t('\'');
			It = Plain != 0 ? Font.Glyphs.find(Plain) : Font.Glyphs.end();
			if (It == Font.Glyphs.end())
			{
				Glyphs.push_back(nullptr);
				Em += Font.Space;
				continue;
			}
		}
		Glyphs.push_back(&It->second);
		Em += It->second.Width;
	}
	return true;
}
} // namespace

bool Width(const std::string& Utf8, double CapHigh, double& Wide)
{
	Wide = 0.0;
	const std::shared_ptr<const SignFont> Font = SignFontNow();
	if (!Font || Font->Cap <= 0.0f) return false;
	std::vector<const SignGlyph*> Glyphs;
	double Em = 0.0;
	if (!Advances(*Font, CodePoints(Utf8), Glyphs, Em)) return false;
	Wide = Em * CapHigh / Font->Cap;
	return true;
}

bool Set(RideMesh& Into, double X, double Y, double Z, double FaceX, double FaceZ, double CapHigh, const std::string& Utf8, const float Colour[3], int Align)
{
	const std::shared_ptr<const SignFont> Font = SignFontNow();
	if (!Font || Font->Cap <= 0.0f) return false;
	std::vector<const SignGlyph*> Glyphs;
	double Em = 0.0;
	const std::vector<uint32_t> Codes = CodePoints(Utf8);
	if (!Advances(*Font, Codes, Glyphs, Em)) return false;
	const double Scale = CapHigh / Font->Cap;   // (metres to the em)
	// (the reader's right along the face: the reader looks along -Face, and with x east and z south the
	// right of a heading (dx, dz) is (-dz, dx); so a letter's x runs along (Face.z, -Face.x))
	const double Len = std::hypot(FaceX, FaceZ);
	if (Len < 1e-9) return false;
	const double Fx = FaceX / Len, Fz = FaceZ / Len;
	const double Rx = Fz, Rz = -Fx;
	double Pen = Align < 0 ? 0.0 : Align == 0 ? -Em * Scale * 0.5 : -Em * Scale;
	const double Base = Y - CapHigh * 0.5;
	for (const SignGlyph* G : Glyphs)
	{
		if (G == nullptr) { Pen += Font->Space * Scale; continue; }
		const uint32_t V0 = uint32_t(Into.Vertices());
		for (size_t P = 0; P + 1 < G->Points.size(); P += 2)
		{
			const double U = Pen + G->Points[P] * Scale, H = Base + G->Points[P + 1] * Scale;
			Into.Pos.push_back(float(X + Rx * U - Into.OriginX));
			Into.Pos.push_back(float(H));
			Into.Pos.push_back(float(Z + Rz * U - Into.OriginZ));
			Into.Nrm.push_back(float(Fx)); Into.Nrm.push_back(0.0f); Into.Nrm.push_back(float(Fz));
			Into.Uv.push_back(float(U)); Into.Uv.push_back(float(H));
			Into.Col.push_back(Colour[0]); Into.Col.push_back(Colour[1]); Into.Col.push_back(Colour[2]);
		}
		// (a triangle faces where (B - A) x (C - A) points; one anticlockwise in the letter's own x right,
		// y up faces the reader, so each is wound that way whatever order the baker cut it in)
		for (size_t T = 0; T + 2 < G->Index.size(); T += 3)
		{
			const uint32_t A = G->Index[T], B = G->Index[T + 1], C = G->Index[T + 2];
			const double Turn = (G->Points[B * 2] - G->Points[A * 2]) * (G->Points[C * 2 + 1] - G->Points[A * 2 + 1]) - (G->Points[B * 2 + 1] - G->Points[A * 2 + 1]) * (G->Points[C * 2] - G->Points[A * 2]);
			Into.Idx.push_back(V0 + A);
			Into.Idx.push_back(V0 + (Turn >= 0.0 ? B : C));
			Into.Idx.push_back(V0 + (Turn >= 0.0 ? C : B));
		}
		Pen += G->Width * Scale;
	}
	return true;
}
} // namespace SignText

namespace SignPlate
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
uint32_t Vert(RideMesh& M, const Frame& F, double U, double V, double Proud, const float C[3], double Facing)
{
	const uint32_t I = uint32_t(M.Vertices());
	// (the reader's right is (Face.z, -Face.x), as SignText::Set's)
	M.Pos.push_back(float(F.X + F.Fz * U + F.Fx * Proud - M.OriginX));
	M.Pos.push_back(float(F.Y + V));
	M.Pos.push_back(float(F.Z - F.Fx * U + F.Fz * Proud - M.OriginZ));
	M.Nrm.push_back(float(F.Fx * Facing)); M.Nrm.push_back(0.0f); M.Nrm.push_back(float(F.Fz * Facing));
	M.Uv.push_back(float(U)); M.Uv.push_back(float(V));
	M.Col.push_back(C[0]); M.Col.push_back(C[1]); M.Col.push_back(C[2]);
	if (!M.Alpha.empty()) M.Alpha.push_back(0.0f);
	return I;
}
// (a triangle anticlockwise in u right, v up faces the reader: each wound so, or the other way for a back)
void Tri(RideMesh& M, uint32_t A, uint32_t B, uint32_t C, const double* Ua, const double* Ub, const double* Uc, double Facing)
{
	const double Turn = (Ub[0] - Ua[0]) * (Uc[1] - Ua[1]) - (Ub[1] - Ua[1]) * (Uc[0] - Ua[0]);
	if (std::fabs(Turn) < 1e-14) return;
	const bool Keep = (Turn > 0.0) == (Facing > 0.0);
	M.Idx.push_back(A);
	M.Idx.push_back(Keep ? B : C);
	M.Idx.push_back(Keep ? C : B);
}
void FanOf(RideMesh& M, const Frame& F, const std::vector<double>& Ring, double Proud, const float Colour[3], double Facing)
{
	const size_t N = Ring.size() / 2;
	if (N < 3) return;
	double Mid[2] = {0.0, 0.0};
	for (size_t K = 0; K < N; ++K) { Mid[0] += Ring[K * 2]; Mid[1] += Ring[K * 2 + 1]; }
	Mid[0] /= double(N); Mid[1] /= double(N);
	const uint32_t C = Vert(M, F, Mid[0], Mid[1], Proud, Colour, Facing);
	const uint32_t V0 = uint32_t(M.Vertices());
	for (size_t K = 0; K < N; ++K) Vert(M, F, Ring[K * 2], Ring[K * 2 + 1], Proud, Colour, Facing);
	for (size_t K = 0; K < N; ++K)
	{
		const size_t L = (K + 1) % N;
		Tri(M, C, V0 + uint32_t(K), V0 + uint32_t(L), Mid, &Ring[K * 2], &Ring[L * 2], Facing);
	}
}
std::vector<double> CircleAt(double U, double V, double R, int N)
{
	std::vector<double> Out;
	for (int K = 0; K < N; ++K)
	{
		Out.push_back(U + R * std::cos(2.0 * Pi * K / N));
		Out.push_back(V + R * std::sin(2.0 * Pi * K / N));
	}
	return Out;
}
} // namespace

Shape Roundel(double Radius)
{
	Shape S;
	S.Corners = {0.0, 0.0};
	S.Round = Radius;
	return S;
}

Shape Triangle(double Side, bool PointUp)
{
	Shape S;
	S.Round = Side * 0.07;
	// (the corners' centres: a triangle whose rounded outline is the sharp one's, its middle the centroid)
	const double Out = 2.0 * (Side / (2.0 * std::sqrt(3.0)) - S.Round);
	for (int K = 0; K < 3; ++K)
	{
		const double A = (PointUp ? 90.0 : -90.0) * Pi / 180.0 + K * 2.0 * Pi / 3.0;
		S.Corners.push_back(Out * std::cos(A));
		S.Corners.push_back(Out * std::sin(A));
	}
	return S;
}

Shape Rect(double W, double H, double Round)
{
	Shape S;
	S.Round = std::fmin(Round, std::fmin(W, H) * 0.5);
	const double A = W * 0.5 - S.Round, B = H * 0.5 - S.Round;
	S.Corners = {-A, -B, A, -B, A, B, -A, B};
	return S;
}

Shape Flag(double W, double H, int Point, double Tip, double Round)
{
	Shape S;
	S.Round = std::fmin(Round, H * 0.25);
	const double R = S.Round, Hw = W * 0.5, Hh = H * 0.5;
	// (pointing right; the tip's corner centre back from the tip by its rounding over the point's half-angle)
	const double Half = std::atan2(Hh, std::fmax(Tip, 1e-3));
	const double Back = R / std::sin(Half);
	std::vector<double> C = {-Hw + R, -Hh + R, Hw - Tip, -Hh + R, Hw - Back, 0.0, Hw - Tip, Hh - R, -Hw + R, Hh - R};
	if (Point < 0)
	{
		std::vector<double> M;
		for (size_t K = C.size() / 2; K-- > 0;) { M.push_back(-C[K * 2]); M.push_back(C[K * 2 + 1]); }
		C = M;
	}
	S.Corners = C;
	return S;
}

std::vector<double> Outline(const Shape& S, double Inset)
{
	std::vector<double> Out;
	const double R = S.Round - Inset;
	const size_t N = S.Corners.size() / 2;
	if (N == 1) return CircleAt(S.Corners[0], S.Corners[1], std::fmax(0.0, R), std::max(8, S.Segments));
	for (size_t K = 0; K < N; ++K)
	{
		const size_t P = (K + N - 1) % N, Q = (K + 1) % N;
		const double Cx = S.Corners[K * 2], Cy = S.Corners[K * 2 + 1];
		// (the outward normals of the edges in and out: an anticlockwise edge (dx, dy) has (dy, -dx))
		double Ix = Cy - S.Corners[P * 2 + 1], Iy = -(Cx - S.Corners[P * 2]);
		double Ox = S.Corners[Q * 2 + 1] - Cy, Oy = -(S.Corners[Q * 2] - Cx);
		const double Il = std::hypot(Ix, Iy), Ol = std::hypot(Ox, Oy);
		if (Il < 1e-12 || Ol < 1e-12) continue;
		Ix /= Il; Iy /= Il; Ox /= Ol; Oy /= Ol;
		const double A0 = std::atan2(Iy, Ix);
		double A1 = std::atan2(Oy, Ox);
		while (A1 < A0) A1 += 2.0 * Pi;
		const int Steps = std::max(1, int(std::ceil((A1 - A0) / (Pi / 10.0))));
		for (int J = 0; J <= Steps; ++J)
		{
			if (R >= 0.0)
			{
				const double A = A0 + (A1 - A0) * J / Steps;
				Out.push_back(Cx + R * std::cos(A));
				Out.push_back(Cy + R * std::sin(A));
			}
			else
			{
				// (inset past the rounding: a sharp corner, moved in along its mitre)
				const double Dot = Ix * Ox + Iy * Oy;
				const double M = R / std::fmax(0.2, 1.0 + Dot);
				Out.push_back(Cx + (Ix + Ox) * M);
				Out.push_back(Cy + (Iy + Oy) * M);
			}
		}
	}
	return Out;
}

void Fill(RideMesh& Into, const Frame& At, const Shape& S, double Inset, double Proud, const float Colour[3])
{
	FanOf(Into, At, Outline(S, Inset), Proud, Colour, 1.0);
}

void Band(RideMesh& Into, const Frame& At, const Shape& S, double Inset, double Wide, double Proud, const float Colour[3])
{
	const std::vector<double> A = Outline(S, Inset), B = Outline(S, Inset + Wide);
	const size_t N = A.size() / 2;
	if (N < 3 || B.size() != A.size()) return;
	const uint32_t V0 = uint32_t(Into.Vertices());
	for (size_t K = 0; K < N; ++K) Vert(Into, At, A[K * 2], A[K * 2 + 1], Proud, Colour, 1.0);
	for (size_t K = 0; K < N; ++K) Vert(Into, At, B[K * 2], B[K * 2 + 1], Proud, Colour, 1.0);
	const uint32_t Nn = uint32_t(N);
	for (size_t K = 0; K < N; ++K)
	{
		const size_t L = (K + 1) % N;
		const uint32_t Ak = V0 + uint32_t(K), Al = V0 + uint32_t(L), Bk = Ak + Nn, Bl = Al + Nn;
		Tri(Into, Ak, Al, Bl, &A[K * 2], &A[L * 2], &B[L * 2], 1.0);
		Tri(Into, Ak, Bl, Bk, &A[K * 2], &B[L * 2], &B[K * 2], 1.0);
	}
}

void Plate(RideMesh& Into, RideMesh& Back, const Frame& At, const Shape& S, const float Ground[3], double Rim, const float Edge[3], const float BackColour[3], double Proud)
{
	FanOf(Back, At, Outline(S, 0.0), Proud - 0.02, BackColour, -1.0);
	if (Rim > 0.0) Band(Into, At, S, 0.0, Rim, Proud, Edge);
	Fill(Into, At, S, Rim, Proud, Ground);
}

void Poly(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Proud, const float Colour[3])
{
	const size_t N = Uv.size() / 2;
	if (N < 3) return;
	const uint32_t V0 = uint32_t(Into.Vertices());
	for (size_t K = 0; K < N; ++K) Vert(Into, At, Uv[K * 2], Uv[K * 2 + 1], Proud, Colour, 1.0);
	for (size_t K = 1; K + 1 < N; ++K) Tri(Into, V0, V0 + uint32_t(K), V0 + uint32_t(K + 1), &Uv[0], &Uv[K * 2], &Uv[K * 2 + 2], 1.0);
}

void Stroke(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Wide, double Proud, const float Colour[3])
{
	const size_t N = Uv.size() / 2;
	if (N < 2) return;
	std::vector<double> Side;   // (the left edge's points then the right's, u, v each)
	for (size_t K = 0; K < N; ++K)
	{
		const size_t P = K > 0 ? K - 1 : 0, Q = K + 1 < N ? K + 1 : N - 1;
		// (the left of each piece either side, the mitre between them, no longer than twice the half)
		double Nx = 0.0, Ny = 0.0;
		for (const std::pair<size_t, size_t>& Piece : {std::make_pair(P, K), std::make_pair(K, Q)})
		{
			const double Dx = Uv[Piece.second * 2] - Uv[Piece.first * 2], Dy = Uv[Piece.second * 2 + 1] - Uv[Piece.first * 2 + 1];
			const double L = std::hypot(Dx, Dy);
			if (L > 1e-12) { Nx += -Dy / L; Ny += Dx / L; }
		}
		const double Nl = std::hypot(Nx, Ny);
		if (Nl < 1e-12) { Nx = 0.0; Ny = 1.0; } else { Nx /= Nl; Ny /= Nl; }
		double Cos = 1.0;
		if (K > 0 && K + 1 < N)
		{
			const double Dx = Uv[Q * 2] - Uv[K * 2], Dy = Uv[Q * 2 + 1] - Uv[K * 2 + 1];
			const double L = std::hypot(Dx, Dy);
			if (L > 1e-12) Cos = std::fmax(0.5, (-Dy * Nx + Dx * Ny) / L);
		}
		const double H = Wide * 0.5 / Cos;
		Side.push_back(Uv[K * 2] + Nx * H); Side.push_back(Uv[K * 2 + 1] + Ny * H);
		Side.push_back(Uv[K * 2] - Nx * H); Side.push_back(Uv[K * 2 + 1] - Ny * H);
	}
	const uint32_t V0 = uint32_t(Into.Vertices());
	for (size_t K = 0; K < N * 2; ++K) Vert(Into, At, Side[K * 2], Side[K * 2 + 1], Proud, Colour, 1.0);
	for (size_t K = 0; K + 1 < N; ++K)
	{
		const uint32_t L0 = V0 + uint32_t(K * 2), R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
		const double* Pl0 = &Side[K * 4];
		const double* Pr0 = &Side[K * 4 + 2];
		const double* Pl1 = &Side[K * 4 + 4];
		const double* Pr1 = &Side[K * 4 + 6];
		Tri(Into, R0, R1, L1, Pr0, Pr1, Pl1, 1.0);
		Tri(Into, R0, L1, L0, Pr0, Pl1, Pl0, 1.0);
	}
}

void Arrow(RideMesh& Into, const Frame& At, const std::vector<double>& Uv, double Wide, double Head, double Proud, const float Colour[3])
{
	const size_t N = Uv.size() / 2;
	if (N < 2) return;
	Stroke(Into, At, Uv, Wide, Proud, Colour);
	const double Ex = Uv[N * 2 - 2], Ey = Uv[N * 2 - 1];
	double Dx = Ex - Uv[N * 2 - 4], Dy = Ey - Uv[N * 2 - 3];
	const double L = std::hypot(Dx, Dy);
	if (L < 1e-12) return;
	Dx /= L; Dy /= L;
	const double W = Head * 0.85;
	Poly(Into, At, {Ex + Dx * Head, Ey + Dy * Head, Ex - Dy * W - Dx * Head * 0.15, Ey + Dx * W - Dy * Head * 0.15, Ex + Dy * W - Dx * Head * 0.15, Ey - Dx * W - Dy * Head * 0.15}, Proud, Colour);
}

void Warning(RideMesh& Into, RideMesh& Back, const Frame& At, double Side, int Symbol, const std::string& Figure)
{
	static const float White[3] = {0.75f, 0.75f, 0.74f}, Red[3] = {0.45f, 0.012f, 0.01f}, Black[3] = {0.012f, 0.012f, 0.012f}, Grey[3] = {0.16f, 0.16f, 0.165f},
		Amber[3] = {0.6f, 0.3f, 0.01f}, Green[3] = {0.01f, 0.35f, 0.12f};
	Plate(Into, Back, At, Triangle(Side, true), White, Side * 0.1, Red, Grey);
	const double S = Side, Up = 0.004;   // (the symbol a little proud of the white)
	const double Main = S * 0.05, Lesser = S * 0.034, Head = S * 0.07;
	const auto Mirror = [](std::vector<double> Uv, bool Do)
	{
		if (Do)
			for (size_t K = 0; K < Uv.size(); K += 2) Uv[K] = -Uv[K];
		return Uv;
	};
	switch (Symbol)
	{
	case SymBendLeft:
	case SymBendRight:
	{
		// (up from the foot, then round to the right, the head pointing on up and to the right)
		std::vector<double> Uv = {-0.05 * S, -0.17 * S};
		const double Cx = 0.07 * S, Cy = -0.03 * S, R = 0.12 * S;
		for (int K = 0; K <= 8; ++K)
		{
			const double A = Pi - (Pi - 110.0 * Pi / 180.0) * K / 8.0;
			Uv.push_back(Cx + R * std::cos(A)); Uv.push_back(Cy + R * std::sin(A));
		}
		Arrow(Into, At, Mirror(Uv, Symbol == SymBendLeft), Main, Head, Up, Black);
		break;
	}
	case SymDoubleBendLeft:
	case SymDoubleBendRight:
	{
		// (first to the left: from the right at the foot, over to the left, back to the right at the head)
		std::vector<double> Uv;
		for (int K = 0; K <= 16; ++K)
		{
			const double T = K / 16.0;
			Uv.push_back(0.05 * S * std::cos(2.0 * Pi * T)); Uv.push_back(-0.17 * S + 0.29 * S * T);
		}
		Arrow(Into, At, Mirror(Uv, Symbol == SymDoubleBendRight), Main, Head, Up, Black);
		break;
	}
	case SymCrossroads:
		Stroke(Into, At, {0.0, -0.17 * S, 0.0, 0.2 * S}, Main, Up, Black);
		Stroke(Into, At, {-0.15 * S, -0.02 * S, 0.15 * S, -0.02 * S}, Lesser, Up, Black);
		break;
	case SymSideLeft:
	case SymSideRight:
		Stroke(Into, At, {0.0, -0.17 * S, 0.0, 0.2 * S}, Main, Up, Black);
		Stroke(Into, At, Mirror({0.0, -0.01 * S, 0.15 * S, -0.01 * S}, Symbol == SymSideLeft), Lesser, Up, Black);
		break;
	case SymTJunction:
		Stroke(Into, At, {-0.16 * S, 0.02 * S, 0.16 * S, 0.02 * S}, Main, Up, Black);
		Stroke(Into, At, {0.0, -0.17 * S, 0.0, 0.02 * S}, Lesser, Up, Black);
		break;
	case SymRoundabout:
	{
		// (three arrows round, clockwise as Britain's go; anticlockwise where the traffic keeps right is the
		// caller's: it mirrors the frame)
		const double R = 0.1 * S, Cy = -0.02 * S;
		for (int Arm = 0; Arm < 3; ++Arm)
		{
			std::vector<double> Uv;
			const double A0 = (150.0 - 120.0 * Arm) * Pi / 180.0;
			for (int K = 0; K <= 8; ++K)
			{
				const double A = A0 - (70.0 * Pi / 180.0) * K / 8.0;
				Uv.push_back(R * std::cos(A)); Uv.push_back(Cy + R * std::sin(A));
			}
			Arrow(Into, At, Uv, Lesser, S * 0.055, Up, Black);
		}
		break;
	}
	case SymSignals:
	{
		Fill(Into, At, Rect(0.12 * S, 0.3 * S, 0.02 * S), 0.0, Up, Black);
		const float* Lamps[3] = {Red, Amber, Green};
		for (int K = 0; K < 3; ++K) Poly(Into, At, CircleAt(0.0, (0.09 - 0.09 * K) * S, 0.036 * S, 16), Up * 2.0, Lamps[K]);
		break;
	}
	case SymSteepDown:
	case SymSteepUp:
	{
		// (the hill falling away to the right, its gradient over it)
		Poly(Into, At, Mirror({-0.21 * S, -0.15 * S, 0.21 * S, -0.15 * S, -0.21 * S, -0.03 * S}, Symbol == SymSteepUp), Up, Black);
		if (!Figure.empty())
		{
			const double U = (Symbol == SymSteepUp ? -0.03 : 0.03) * S, V = 0.04 * S, P = Up;
			SignText::Set(Into, At.X + At.Fz * U + At.Fx * P, At.Y + V, At.Z - At.Fx * U + At.Fz * P, At.Fx, At.Fz, 0.075 * S, Figure, Black, 0);
		}
		break;
	}
	case SymHumps:
		Stroke(Into, At, {-0.17 * S, -0.13 * S, 0.17 * S, -0.13 * S}, S * 0.025, Up, Black);
		for (double U : {-0.08, 0.08})
		{
			std::vector<double> Uv;
			for (int K = 0; K <= 10; ++K)
			{
				Uv.push_back(U * S + 0.06 * S * std::cos(Pi * K / 10.0));
				Uv.push_back(-0.13 * S + 0.06 * S * std::sin(Pi * K / 10.0));
			}
			Poly(Into, At, Uv, Up, Black);
		}
		break;
	case SymNarrows:
		for (bool M : {false, true}) Stroke(Into, At, Mirror({-0.11 * S, -0.17 * S, -0.11 * S, -0.06 * S, -0.045 * S, 0.06 * S, -0.045 * S, 0.17 * S}, M), Lesser, Up, Black);
		break;
	default:
		// (other danger: the exclamation mark)
		Poly(Into, At, {-0.028 * S, 0.17 * S, 0.028 * S, 0.17 * S, 0.014 * S, -0.06 * S, -0.014 * S, -0.06 * S}, Up, Black);
		Poly(Into, At, CircleAt(0.0, -0.12 * S, 0.028 * S, 14), Up, Black);
		break;
	}
}

bool WordPlate(RideMesh& Into, RideMesh& Back, const Frame& At, double CapHigh, const std::string& Utf8, const float Ground[3], const float Ink[3], double& W, double& H)
{
	static const float Grey[3] = {0.16f, 0.16f, 0.165f};
	double Wide = 0.0;
	if (!SignText::Width(Utf8, CapHigh, Wide)) return false;
	W = Wide + CapHigh * 2.0;
	H = CapHigh * 2.4;
	const double Rim = CapHigh * 0.16;
	Plate(Into, Back, At, Rect(W, H, Rim * 1.5), Ground, Rim, Ink, Grey);
	return SignText::Set(Into, At.X + At.Fx * 0.004, At.Y, At.Z + At.Fz * 0.004, At.Fx, At.Fz, CapHigh, Utf8, Ink, 0);
}
namespace
{
void SchemeColours(int Scheme, const float*& Ground, const float*& Ink)
{
	static const float White[3] = {0.75f, 0.75f, 0.74f}, Black[3] = {0.012f, 0.012f, 0.012f}, Green[3] = {0.0f, 0.13f, 0.05f}, Blue[3] = {0.0f, 0.05f, 0.25f},
		Brown[3] = {0.16f, 0.055f, 0.02f};
	Ground = Scheme == SchemePrimary ? Green : Scheme == SchemeMotorway ? Blue : Scheme == SchemeBrown ? Brown : White;
	Ink = Scheme == SchemeLocal ? Black : White;
}
std::vector<std::string> Split(const std::string& Text, char By)
{
	std::vector<std::string> Out;
	size_t From = 0;
	for (;;)
	{
		const size_t At = Text.find(By, From);
		Out.push_back(Text.substr(From, At == std::string::npos ? std::string::npos : At - From));
		if (At == std::string::npos) break;
		From = At + 1;
	}
	return Out;
}
// (words set at (U, V) on a frame's plate, a little proud)
bool SetOn(RideMesh& Into, const Frame& At, double U, double V, double Cap, const std::string& Words, const float Ink[3], int Align)
{
	const double P = 0.004;
	return SignText::Set(Into, At.X + At.Fz * U + At.Fx * P, At.Y + V, At.Z - At.Fx * U + At.Fz * P, At.Fx, At.Fz, Cap, Words, Ink, Align);
}
} // namespace

void Countdown(RideMesh& Into, RideMesh& Back, const Frame& At, int Bars)
{
	static const float Blue[3] = {0.0f, 0.05f, 0.25f}, White[3] = {0.75f, 0.75f, 0.74f}, Grey[3] = {0.16f, 0.16f, 0.165f};
	const double W = 0.9, H = 1.2;
	Plate(Into, Back, At, Rect(W, H, 0.04), Blue, 0.0, White, Grey);
	const int N = std::clamp(Bars, 1, 3);
	const double Band = 0.13, Pitch = H / 3.4;
	for (int K = 0; K < N; ++K)
	{
		const double V = (double(N) - 1.0) * Pitch * 0.5 - double(K) * Pitch;
		// (from the lower right up to the upper left, inside the plate's edge)
		Poly(Into, At, {W * 0.4, V - 0.2, W * 0.4, V - 0.2 + Band, -W * 0.4, V + 0.2 + Band, -W * 0.4, V + 0.2}, 0.003, White);
	}
}

std::string Miles(double Metres)
{
	const double M = Metres / 1609.344;
	if (M < 0.25) return std::string();
	if (M < 2.75)
	{
		const int Halves = std::max(1, int(std::lround(M * 2.0)));
		const std::string Whole = Halves / 2 > 0 ? std::to_string(Halves / 2) : std::string();
		return Halves % 2 ? Whole + "\xC2\xBD" : Whole;
	}
	return std::to_string(int(std::lround(M)));
}

bool AdvanceBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Arms, double& W, double& H)
{
	W = H = 0.0;
	std::vector<std::string> Ahead, Left, Right;
	for (const std::string& Arm : Split(Arms, '|'))
	{
		if (Arm.size() < 2) continue;
		std::vector<std::string>& To = Arm[0] == '^' ? Ahead : Arm[0] == '<' ? Left : Right;
		for (const std::string& P : Split(Arm.substr(1), ';'))
			if (!P.empty()) To.push_back(P);
	}
	if (Left.empty() && Right.empty()) return false;
	const double C = CapHigh, Pitch = C * 1.9, Stem = C * 0.9, Side = C * 0.65, Reach = C * 2.2, Gap = C * 1.4, Margin = C * 1.1;
	const auto Widest = [C](const std::vector<std::string>& L, double& Most)
	{
		Most = 0.0;
		for (const std::string& P : L)
		{
			double Wd = 0.0;
			if (!SignText::Width(P, C, Wd)) return false;
			Most = std::fmax(Most, Wd);
		}
		return true;
	};
	double Wa = 0.0, Wl = 0.0, Wr = 0.0;
	if (!Widest(Ahead, Wa) || !Widest(Left, Wl) || !Widest(Right, Wr)) return false;
	// (about the junction on the stem: the sides' words beside their arms' ends, the ahead's over the stem)
	const double SideTall = std::fmax(double(Left.size()), double(Right.size())) * Pitch;
	const double StemTop = std::fmax(SideTall * 0.5, C * 1.2) + C * 0.9;
	const double StemFoot = -std::fmax(SideTall * 0.5, C * 0.6) - C * 1.6;
	const double AheadFoot = StemTop + C * 2.0;   // (over the arrow's head, C * 1.1 past the stem's top)
	const double Top = Ahead.empty() ? StemTop + C * 1.2 : AheadFoot + double(Ahead.size()) * Pitch - (Pitch - C) * 0.5;
	const double LeftEdge = -std::fmax(Left.empty() ? Stem : Reach + Gap + Wl, Wa * 0.5), RightEdge = std::fmax(Right.empty() ? Stem : Reach + Gap + Wr, Wa * 0.5);
	const double U0 = LeftEdge - Margin, U1 = RightEdge + Margin, V0 = StemFoot - Margin * 0.4, V1 = Top + Margin * 0.7;
	W = U1 - U0;
	H = V1 - V0;
	if (Into == nullptr || Back == nullptr) return true;
	const float* Ground = nullptr;
	const float* Ink = nullptr;
	SchemeColours(Scheme, Ground, Ink);
	static const float Grey[3] = {0.16f, 0.16f, 0.165f};
	// (the board about its middle, so everything shifts by its middle's place about the junction)
	const double Mu = (U0 + U1) * 0.5, Mv = (V0 + V1) * 0.5;
	Plate(*Into, *Back, At, Rect(W, H, C * 0.6), Ground, C * 0.22, Ink, Grey);
	const auto Line = [&](std::initializer_list<double> Uv) { std::vector<double> Out; bool X = true; for (double D : Uv) { Out.push_back(D - (X ? Mu : Mv)); X = !X; } return Out; };
	Arrow(*Into, At, Line({0.0, StemFoot, 0.0, StemTop}), Stem, C * 1.1, 0.003, Ink);
	if (!Left.empty()) Arrow(*Into, At, Line({0.0, 0.0, -Reach, 0.0}), Side, C * 0.9, 0.003, Ink);
	if (!Right.empty()) Arrow(*Into, At, Line({0.0, 0.0, Reach, 0.0}), Side, C * 0.9, 0.003, Ink);
	bool Ok = true;
	for (size_t K = 0; K < Left.size(); ++K) Ok = SetOn(*Into, At, -Reach - Gap - Mu, (double(Left.size()) - 1.0) * Pitch * 0.5 - double(K) * Pitch - Mv, C, Left[K], Ink, 1) && Ok;
	for (size_t K = 0; K < Right.size(); ++K) Ok = SetOn(*Into, At, Reach + Gap - Mu, (double(Right.size()) - 1.0) * Pitch * 0.5 - double(K) * Pitch - Mv, C, Right[K], Ink, -1) && Ok;
	for (size_t K = 0; K < Ahead.size(); ++K) Ok = SetOn(*Into, At, -Mu, AheadFoot + (double(Ahead.size()) - 1.0 - double(K)) * Pitch - Mv, C, Ahead[K], Ink, 0) && Ok;
	return Ok;
}

bool ConfirmBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Lines, double& W, double& H)
{
	W = H = 0.0;
	std::vector<std::pair<std::string, std::string>> Rows;
	for (const std::string& L : Split(Lines, '|'))
	{
		const std::vector<std::string> Two = Split(L, '\t');
		if (!Two[0].empty()) Rows.emplace_back(Two[0], Two.size() > 1 ? Two[1] : std::string());
	}
	if (Rows.empty()) return false;
	const double C = CapHigh, Pitch = C * 1.9, Margin = C * 1.1;
	double Wn = 0.0, Wd = 0.0;
	for (const auto& R : Rows)
	{
		double A = 0.0, B = 0.0;
		if (!SignText::Width(R.first, C, A) || !SignText::Width(R.second, C, B)) return false;
		Wn = std::fmax(Wn, A);
		Wd = std::fmax(Wd, B);
	}
	W = Wn + (Wd > 0.0 ? C * 1.5 + Wd : 0.0) + Margin * 2.0;
	H = double(Rows.size()) * Pitch + Margin * 0.6;
	if (Into == nullptr || Back == nullptr) return true;
	const float* Ground = nullptr;
	const float* Ink = nullptr;
	SchemeColours(Scheme, Ground, Ink);
	static const float Grey[3] = {0.16f, 0.16f, 0.165f};
	Plate(*Into, *Back, At, Rect(W, H, C * 0.6), Ground, C * 0.22, Ink, Grey);
	bool Ok = true;
	for (size_t K = 0; K < Rows.size(); ++K)
	{
		const double V = (double(Rows.size()) - 1.0) * Pitch * 0.5 - double(K) * Pitch;
		Ok = SetOn(*Into, At, -W * 0.5 + Margin, V, C, Rows[K].first, Ink, -1) && Ok;
		if (!Rows[K].second.empty()) Ok = SetOn(*Into, At, W * 0.5 - Margin, V, C, Rows[K].second, Ink, 1) && Ok;
	}
	return Ok;
}

bool FlagBoard(RideMesh* Into, RideMesh* Back, const Frame& At, double CapHigh, int Scheme, const std::string& Words, double& W, double& H)
{
	W = H = 0.0;
	if (Words.size() < 2 || (Words[0] != '<' && Words[0] != '>')) return false;
	const int Point = Words[0] == '<' ? -1 : 1;
	const std::vector<std::string> Two = Split(Words.substr(1), '\t');
	const double C = CapHigh, Tip = C * 1.6, Margin = C * 0.9;
	double A = 0.0, B = 0.0;
	const std::string Far = Two.size() > 1 ? Two[1] : std::string();
	if (!SignText::Width(Two[0], C, A) || !SignText::Width(Far, C, B)) return false;
	W = A + (B > 0.0 ? C * 1.0 + B : 0.0) + Margin * 2.0 + Tip;
	H = C * 2.4;
	if (Into == nullptr || Back == nullptr) return true;
	const float* Ground = nullptr;
	const float* Ink = nullptr;
	SchemeColours(Scheme, Ground, Ink);
	static const float Grey[3] = {0.16f, 0.16f, 0.165f};
	Plate(*Into, *Back, At, Flag(W, H, Point, Tip, C * 0.3), Ground, C * 0.2, Ink, Grey);
	// (the place from the flag's square end, its miles by the point)
	const double Square = -Point * W * 0.5, Pointed = Point * (W * 0.5 - Tip);
	bool Ok = SetOn(*Into, At, Square + Point * Margin, 0.0, C, Two[0], Ink, Point > 0 ? -1 : 1);
	if (!Far.empty()) Ok = SetOn(*Into, At, Pointed - Point * Margin * 0.3, 0.0, C, Far, Ink, Point > 0 ? 1 : -1) && Ok;
	return Ok;
}
} // namespace SignPlate
} // namespace TorqueWorld
