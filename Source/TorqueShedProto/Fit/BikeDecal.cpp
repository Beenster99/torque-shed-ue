#include "BikeDecal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <tuple>

namespace TorqueFit
{
namespace Decals
{
namespace BikeDecalImpl
{
constexpr double DecalPi = 3.14159265358979323846;

std::vector<std::string> SplitOn(const std::string& S, char C)
{
	std::vector<std::string> Out;
	std::string Cur;
	for (const char Ch : S)
	{
		if (Ch == C)
		{
			Out.push_back(Cur);
			Cur.clear();
		}
		else
			Cur.push_back(Ch);
	}
	Out.push_back(Cur);
	return Out;
}

bool ReadNum(const std::string& S, double& Out)
{
	if (S.empty() || S.size() > 32) return false;
	char* End = nullptr;
	const double V = std::strtod(S.c_str(), &End);
	if (End == S.c_str() || *End != '\0' || !std::isfinite(V)) return false;
	Out = V;
	return true;
}

bool ReadHex(const std::string& S, uint32_t& Out)
{
	if (S.empty() || S.size() > 8) return false;
	char* End = nullptr;
	const unsigned long V = std::strtoul(S.c_str(), &End, 16);
	if (End == S.c_str() || *End != '\0') return false;
	Out = (uint32_t)(V & 0xFFFFFFu);
	return true;
}

std::string Num(double V, int Places)
{
	char Buf[48];
	std::snprintf(Buf, sizeof(Buf), "%.*f", Places, V);
	std::string S(Buf);
	// "12.500" -> "12.5", "3.000" -> "3"
	if (S.find('.') != std::string::npos)
	{
		while (!S.empty() && S.back() == '0') S.pop_back();
		if (!S.empty() && S.back() == '.') S.pop_back();
	}
	if (S == "-0") S = "0";
	return S;
}

std::string Hex(uint32_t V)
{
	char Buf[16];
	std::snprintf(Buf, sizeof(Buf), "%06x", (unsigned)(V & 0xFFFFFFu));
	return Buf;
}

bool IdOk(const std::string& S)
{
	if (S.empty() || S.size() > 64) return false;
	for (const char C : S)
		if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-' || C == '.')) return false;
	return true;
}

double ClampD(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

// A unit vector across the surface to call "up" for a decal facing N: the world's up laid flat on it,
// or forward on a surface facing up or down.
V3 UpOn(const V3& N)
{
	const V3 Ref = std::fabs(N.z) > 0.92 ? V3(1.0, 0.0, 0.0) : V3(0.0, 0.0, 1.0);
	return (Ref - N * Ref.Dot(N)).Normalized();
}
} // namespace BikeDecalImpl

namespace BDI = BikeDecalImpl;

std::string Clean(const std::string& Typed)
{
	// Printable text only, no separators, at most MaxTextChars characters (UTF-8 kept whole).
	std::string Out;
	int Chars = 0;
	for (size_t I = 0; I < Typed.size() && Chars < MaxTextChars; ++I)
	{
		const unsigned char C = (unsigned char)Typed[I];
		if (C < 0x20 || C == 0x7F || C == '|' || C == ';') continue;
		if (C >= 0x80)
		{
			// A whole UTF-8 sequence, or nothing.
			const size_t Len = (C & 0xE0) == 0xC0 ? 2 : ((C & 0xF0) == 0xE0 ? 3 : ((C & 0xF8) == 0xF0 ? 4 : 0));
			if (Len == 0 || I + Len > Typed.size()) continue;
			bool bOk = true;
			for (size_t K = 1; K < Len; ++K) bOk = bOk && (((unsigned char)Typed[I + K]) & 0xC0) == 0x80;
			if (!bOk) continue;
			Out.append(Typed, I, Len);
			I += Len - 1;
		}
		else
			Out.push_back((char)C);
		++Chars;
	}
	// No spaces at the ends.
	while (!Out.empty() && Out.back() == ' ') Out.pop_back();
	size_t Lead = 0;
	while (Lead < Out.size() && Out[Lead] == ' ') ++Lead;
	return Out.substr(Lead);
}

std::string ToText(const std::vector<Decal>& L)
{
	std::string Out = "decal1";
	int N = 0;
	for (const Decal& D : L)
	{
		if (N >= MaxPerPart) break;
		++N;
		const char K = D.K == Kind::Text ? 'T' : (D.K == Kind::Image ? 'I' : 'L');
		Out += ";";
		Out += K;
		Out += "|" + D.Id + "|" + Clean(D.Text);
		Out += "|" + BDI::Num(D.At.x, 2) + "|" + BDI::Num(D.At.y, 2) + "|" + BDI::Num(D.At.z, 2);
		Out += "|" + BDI::Num(D.Normal.x, 4) + "|" + BDI::Num(D.Normal.y, 4) + "|" + BDI::Num(D.Normal.z, 4);
		Out += "|" + BDI::Num(D.Turn, 1) + "|" + BDI::Num(D.W, 2) + "|" + BDI::Num(D.H, 2);
		Out += "|" + BDI::Hex(D.Rgb1) + "|" + BDI::Hex(D.Rgb2) + "|" + std::to_string(D.Flag & 63u) + "|" + BDI::Num(D.Arch, 2);
	}
	return N == 0 ? std::string() : Out;
}

std::vector<Decal> Parse(const std::string& Text)
{
	std::vector<Decal> Out;
	if (Text.size() > 8192) return Out;
	const std::vector<std::string> Parts = BDI::SplitOn(Text, ';');
	if (Parts.empty() || Parts[0] != "decal1") return Out;
	for (size_t I = 1; I < Parts.size() && (int)Out.size() < MaxPerPart; ++I)
	{
		const std::vector<std::string> F = BDI::SplitOn(Parts[I], '|');
		if (F.size() != 16 || F[0].size() != 1) continue;
		Decal D;
		const char K = F[0][0];
		if (K == 'T') D.K = Kind::Text;
		else if (K == 'I') D.K = Kind::Image;
		else if (K == 'L') D.K = Kind::Library;
		else continue;
		if (!BDI::IdOk(F[1])) continue;
		D.Id = F[1];
		D.Text = Clean(F[2]);
		if (D.K == Kind::Text && D.Text.empty()) continue;
		double V[10];
		bool bOk = true;
		for (int J = 0; J < 9; ++J) bOk = bOk && BDI::ReadNum(F[(size_t)(3 + J)], V[J]);
		bOk = bOk && BDI::ReadNum(F[15], V[9]);
		uint32_t C1 = 0, C2 = 0;
		bOk = bOk && BDI::ReadHex(F[12], C1) && BDI::ReadHex(F[13], C2);
		double Fl = 0.0;
		bOk = bOk && BDI::ReadNum(F[14], Fl);
		if (!bOk) continue;
		D.At = V3(BDI::ClampD(V[0], -500.0, 500.0), BDI::ClampD(V[1], -500.0, 500.0), BDI::ClampD(V[2], -500.0, 500.0));
		D.Normal = V3(V[3], V[4], V[5]).Normalized();
		if (D.Normal.Length() < 0.5) continue;
		D.Turn = std::fmod(V[6], 360.0);
		D.W = BDI::ClampD(V[7], MinCm, MaxCm);
		D.H = BDI::ClampD(V[8], MinCm, MaxCm);
		D.Rgb1 = C1;
		D.Rgb2 = C2;
		D.Flag = (uint32_t)BDI::ClampD(Fl, 0.0, 63.0);
		D.Arch = BDI::ClampD(V[9], 0.0, 1.0);
		Out.push_back(D);
	}
	return Out;
}

std::string Sanitize(const std::string& Text) { return ToText(Parse(Text)); }

Frame FrameOf(const Decal& D)
{
	Frame F;
	F.O = D.At;
	F.N = D.Normal.Normalized();
	const V3 Up0 = BDI::UpOn(F.N);
	// The picture's right as seen from outside (looking along -N): Up x (-N).
	const V3 R0 = Up0.Cross(-F.N).Normalized();
	const double A = D.Turn * BDI::DecalPi / 180.0;
	F.R = (R0 * std::cos(A) + Up0 * std::sin(A)).Normalized();
	F.U = (Up0 * std::cos(A) - R0 * std::sin(A)).Normalized();
	return F;
}

Decal Mirrored(const Decal& D)
{
	Decal M = D;
	M.At.y = -D.At.y;
	M.Normal.y = -D.Normal.y;
	M.Turn = -D.Turn;
	if (D.K != Kind::Text) M.Flag ^= Flip;
	M.Flag &= ~(uint32_t)Pair;
	return M;
}

void ArchPoint(double U, double V, double W, double H, double Arch, double& X, double& Y)
{
	if (Arch <= 1e-3)
	{
		X = (U - 0.5) * W;
		Y = (0.5 - V) * H;
		return;
	}
	const double Rr = W / (Arch * BDI::DecalPi);
	const double Th = (U - 0.5) * W / Rr;
	const double R = Rr + (0.5 - V) * H;
	X = R * std::sin(Th);
	Y = R * std::cos(Th) - Rr;
}

bool MakeSkin(const std::vector<V3>& Tris, const Decal& D, Skin& Out)
{
	Out = Skin();
	const Frame F = FrameOf(D);
	const bool bFlip = D.Has(Flip);
	// The rectangle the picture covers on the surface (an arch's bend makes it bigger).
	double X0 = -D.W * 0.5, X1 = D.W * 0.5, Y0 = -D.H * 0.5, Y1 = D.H * 0.5;
	if (D.Arch > 1e-3)
	{
		X0 = Y0 = 1e9;
		X1 = Y1 = -1e9;
		for (int I = 0; I <= 16; ++I)
			for (const double Edge : {0.0, 1.0})
			{
				double X = 0.0, Y = 0.0;
				const double T = I / 16.0;
				for (int Side = 0; Side < 2; ++Side)
				{
					if (Side == 0) ArchPoint(T, Edge, D.W, D.H, D.Arch, X, Y);
					else ArchPoint(Edge, T, D.W, D.H, D.Arch, X, Y);
					X0 = std::min(X0, X);
					X1 = std::max(X1, X);
					Y0 = std::min(Y0, Y);
					Y1 = std::max(Y1, Y);
				}
			}
	}
	const double Reach = std::max(X1 - X0, Y1 - Y0) * 0.6 + 3.0;   // how far in or out of the surface it looks
	struct FTri
	{
		V3 P[3];
		V3 N;      // facing out (N's way)
		double S[3], T[3], Dp[3];
	};
	std::vector<FTri> Picked;
	constexpr int Cells = 32;
	std::vector<double> Depth((size_t)(Cells * Cells), -std::numeric_limits<double>::infinity());
	const double CellX = (X1 - X0) / Cells, CellY = (Y1 - Y0) / Cells;
	if (CellX <= 0.0 || CellY <= 0.0) return false;
	auto CellI = [&](double S) { return std::max(0, std::min(Cells - 1, (int)std::floor((S - X0) / CellX))); };
	auto CellJ = [&](double T) { return std::max(0, std::min(Cells - 1, (int)std::floor((T - Y0) / CellY))); };
	for (size_t K = 0; K + 2 < Tris.size(); K += 3)
	{
		FTri T;
		for (int J = 0; J < 3; ++J)
		{
			T.P[J] = Tris[K + (size_t)J];
			const V3 Q = T.P[J] - F.O;
			T.S[J] = Q.Dot(F.R);
			T.T[J] = Q.Dot(F.U);
			T.Dp[J] = Q.Dot(F.N);
		}
		const double LoS = std::min(T.S[0], std::min(T.S[1], T.S[2])), HiS = std::max(T.S[0], std::max(T.S[1], T.S[2]));
		const double LoT = std::min(T.T[0], std::min(T.T[1], T.T[2])), HiT = std::max(T.T[0], std::max(T.T[1], T.T[2]));
		if (HiS < X0 || LoS > X1 || HiT < Y0 || LoT > Y1) continue;
		const double LoD = std::min(T.Dp[0], std::min(T.Dp[1], T.Dp[2])), HiD = std::max(T.Dp[0], std::max(T.Dp[1], T.Dp[2]));
		if (HiD < -Reach || LoD > Reach) continue;
		V3 N = (T.P[1] - T.P[0]).Cross(T.P[2] - T.P[0]);
		if (N.Dot(N) < 1e-12) continue;
		N = N.Normalized();
		if (N.Dot(F.N) < 0.0) N = -N;
		if (N.Dot(F.N) < 0.2) continue;   // edge on to it, or facing away
		T.N = N;
		Picked.push_back(T);
		for (int I = CellI(LoS); I <= CellI(HiS); ++I)
			for (int J = CellJ(LoT); J <= CellJ(HiT); ++J)
			{
				double& Dd = Depth[(size_t)(J * Cells + I)];
				Dd = std::max(Dd, HiD);
			}
	}
	if (Picked.empty()) return false;
	// Only the outermost skin: a triangle under something further out is left out.
	using FKey = std::tuple<long long, long long, long long>;
	auto KeyOf = [](const V3& P) { return FKey((long long)std::llround(P.x * 20.0), (long long)std::llround(P.y * 20.0), (long long)std::llround(P.z * 20.0)); };
	std::map<FKey, V3> Normals;
	std::vector<const FTri*> Keep;
	for (const FTri& T : Picked)
	{
		const double MidS = (T.S[0] + T.S[1] + T.S[2]) / 3.0, MidT = (T.T[0] + T.T[1] + T.T[2]) / 3.0;
		const double Near = std::max(T.Dp[0], std::max(T.Dp[1], T.Dp[2]));
		if (Near < Depth[(size_t)(CellJ(MidT) * Cells + CellI(MidS))] - 1.5) continue;
		Keep.push_back(&T);
		if ((int)Keep.size() > MaxSkinTris) break;
		for (const V3& P : T.P) Normals[KeyOf(P)] += T.N;
	}
	if (Keep.empty()) return false;
	const double Rr = D.Arch > 1e-3 ? D.W / (D.Arch * BDI::DecalPi) : 0.0;
	for (const FTri* T : Keep)
	{
		const bool bAsIs = (T->P[1] - T->P[0]).Cross(T->P[2] - T->P[0]).Dot(T->N) >= 0.0;
		const int Order[3] = {0, bAsIs ? 1 : 2, bAsIs ? 2 : 1};
		for (const int K : Order)
		{
			const V3& P = T->P[K];
			V3 N = Normals[KeyOf(P)].Normalized();
			if (N.Length() < 0.5) N = T->N;
			double U = 0.0, V = 0.0;
			if (Rr > 0.0)
			{
				const double Dy = T->T[K] + Rr;
				const double R = std::sqrt(T->S[K] * T->S[K] + Dy * Dy);
				const double Th = std::atan2(T->S[K], Dy);
				U = 0.5 + Th * Rr / D.W;
				V = 0.5 - (R - Rr) / D.H;
			}
			else
			{
				U = 0.5 + T->S[K] / D.W;
				V = 0.5 - T->T[K] / D.H;
			}
			if (bFlip) U = 1.0 - U;
			Out.Idx.push_back((unsigned)Out.Pos.size());
			Out.Pos.push_back(P + N * Lift);
			Out.Nrm.push_back(N);
			Out.U.push_back(U);
			Out.V.push_back(V);
		}
	}
	return true;
}

bool RayHit(const std::vector<V3>& Tris, const V3& From, const V3& Dir, V3& Hit, V3& Normal)
{
	double Best = std::numeric_limits<double>::infinity();
	const V3 Dn = Dir.Normalized();
	for (size_t K = 0; K + 2 < Tris.size(); K += 3)
	{
		// Moller-Trumbore, both faces.
		const V3 &A = Tris[K], &B = Tris[K + 1], &C = Tris[K + 2];
		const V3 E1 = B - A, E2 = C - A;
		const V3 Pv = Dn.Cross(E2);
		const double Det = E1.Dot(Pv);
		if (std::fabs(Det) < 1e-12) continue;
		const double Inv = 1.0 / Det;
		const V3 Tv = From - A;
		const double U = Tv.Dot(Pv) * Inv;
		if (U < 0.0 || U > 1.0) continue;
		const V3 Qv = Tv.Cross(E1);
		const double V = Dn.Dot(Qv) * Inv;
		if (V < 0.0 || U + V > 1.0) continue;
		const double T = E2.Dot(Qv) * Inv;
		if (T <= 0.0 || T >= Best) continue;
		Best = T;
		Hit = From + Dn * T;
		V3 N = E1.Cross(E2).Normalized();
		if (N.Dot(Dn) > 0.0) N = -N;
		Normal = N;
	}
	return std::isfinite(Best);
}
} // namespace Decals
} // namespace TorqueFit
