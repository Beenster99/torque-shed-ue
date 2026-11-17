// Decals (Fit/BikeDecal): a part's list as text and back, the skin cut from a tank-like shape under
// a decal, its mirror twin, an arch, and picking the surface under the mouse.
#include "BikeDecal.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace TorqueFit::Decals;
using TorqueSim::V3;

static int Fails = 0;
static void Check(bool Ok, const char* What)
{
	std::printf("%s %s\n", Ok ? "PASS" : "FAIL", What);
	if (!Ok) ++Fails;
}

// An ellipsoid "tank" (cm): 60 long (x), 34 wide (y), 26 tall (z), as triangles.
static std::vector<V3> Tank(int Nu = 64, int Nv = 32)
{
	std::vector<V3> T;
	auto P = [](double U, double V) {
		const double A = U * 2.0 * 3.14159265358979, B = (V - 0.5) * 3.14159265358979;
		return V3(30.0 * std::cos(B) * std::cos(A), 17.0 * std::cos(B) * std::sin(A), 13.0 * std::sin(B));
	};
	for (int I = 0; I < Nu; ++I)
		for (int J = 0; J < Nv; ++J)
		{
			const double U0 = (double)I / Nu, U1 = (double)(I + 1) / Nu, V0 = (double)J / Nv, V1 = (double)(J + 1) / Nv;
			T.push_back(P(U0, V0)); T.push_back(P(U1, V0)); T.push_back(P(U1, V1));
			T.push_back(P(U0, V0)); T.push_back(P(U1, V1)); T.push_back(P(U0, V1));
		}
	return T;
}

int main()
{
	const std::vector<V3> T = Tank();
	// Text round trip.
	Decal A;
	A.K = Kind::Text;
	A.Id = "fraktur";
	A.Text = "Death | Rider;";
	A.At = V3(0.0, 17.0, 0.0);
	A.Normal = V3(0.0, 1.0, 0.0);
	A.W = 24.0;
	A.H = 6.0;
	A.Turn = 10.0;
	A.Flag = Filled | Outline | Pair;
	A.Arch = 0.3;
	Decal B;
	B.Id = "skull_front";
	B.At = V3(5.0, -16.0, 2.0);
	B.Normal = V3(0.0, -1.0, 0.1);
	const std::string S = ToText({A, B});
	const std::vector<Decal> L = Parse(S);
	Check(L.size() == 2, "two decals read back");
	Check(L.size() == 2 && L[0].Text == "Death  Rider" && L[0].K == Kind::Text && L[0].Id == "fraktur", "lettering kept, separators taken out");
	Check(L.size() == 2 && std::fabs(L[0].W - 24.0) < 1e-6 && std::fabs(L[0].Turn - 10.0) < 1e-6 && L[0].Has(Pair) && std::fabs(L[0].Arch - 0.3) < 1e-6, "size, turn, flags, arch kept");
	Check(ToText(Parse(ToText(L))) == ToText(L) && !S.empty(), "written again the same");
	Check(Parse("rubbish").empty() && Parse("decal1;X|a|b").empty() && Parse("decal1;L|../etc|x|0|0|0|0|1|0|0|5|5|ff0000|000000|1|0").empty(), "nonsense refused");
	{
		std::vector<Decal> Many(12, B);
		Check(Parse(ToText(Many)).size() == (size_t)MaxPerPart, "at most eight a part");
	}
	// The skin on the right side.
	Skin K;
	Decal Side = B;
	Side.At = V3(0.0, 17.0, 0.0);
	Side.Normal = V3(0.0, 1.0, 0.0);
	Side.W = 16.0;
	Side.H = 10.0;
	Check(MakeSkin(T, Side, K) && K.Tris() > 10, "a skin under the decal");
	bool bOut = true, bNear = true, bWound = true, bUv = false;
	for (size_t I = 0; I < K.Pos.size(); ++I)
	{
		const V3& P = K.Pos[I];
		bOut = bOut && P.y > 0.0;
		const double E = (P.x / 30.0) * (P.x / 30.0) + (P.y / 17.0) * (P.y / 17.0) + (P.z / 13.0) * (P.z / 13.0);
		bNear = bNear && std::fabs(std::sqrt(E) - 1.0) < 0.03;
		if (K.U[I] > 0.4 && K.U[I] < 0.6 && K.V[I] > 0.4 && K.V[I] < 0.6) bUv = true;
	}
	for (size_t I = 0; I + 2 < K.Idx.size(); I += 3)
	{
		const V3 &P0 = K.Pos[K.Idx[I]], &P1 = K.Pos[K.Idx[I + 1]], &P2 = K.Pos[K.Idx[I + 2]];
		bWound = bWound && (P1 - P0).Cross(P2 - P0).Dot(K.Nrm[K.Idx[I]]) > 0.0;
	}
	Check(bOut, "only the right side's skin (not the far side)");
	Check(bNear, "the skin sits on the surface");
	Check(bWound, "wound facing out");
	Check(bUv, "the picture's middle at the decal's middle");
	// Picture right on the right side is towards the nose (x+): U grows with x.
	{
		double Lo = 1e9, Hi = -1e9, ULo = 0.0, UHi = 0.0;
		for (size_t I = 0; I < K.Pos.size(); ++I)
		{
			if (K.Pos[I].x < Lo) { Lo = K.Pos[I].x; ULo = K.U[I]; }
			if (K.Pos[I].x > Hi) { Hi = K.Pos[I].x; UHi = K.U[I]; }
		}
		Check(UHi > ULo, "reads left to right towards the nose on the right side");
		double ZLo = 1e9, ZHi = -1e9, VLo = 0.0, VHi = 0.0;
		for (size_t I = 0; I < K.Pos.size(); ++I)
		{
			if (K.Pos[I].z < ZLo) { ZLo = K.Pos[I].z; VLo = K.V[I]; }
			if (K.Pos[I].z > ZHi) { ZHi = K.Pos[I].z; VHi = K.V[I]; }
		}
		Check(VHi < VLo, "picture's top is up");
	}
	// The mirror twin on the left reads towards the tail, and lettering isn't flipped.
	{
		const Decal M = Mirrored(Side);
		Skin KM;
		Check(MakeSkin(T, M, KM) && KM.Tris() == K.Tris(), "the twin's skin as big");
		bool bLeft = true;
		for (const V3& P : KM.Pos) bLeft = bLeft && P.y < 0.0;
		Check(bLeft, "the twin on the left side");
		Check(M.Has(Flip) && !Mirrored(A).Has(Flip), "pictures flip, lettering doesn't");
	}
	// Turned 90 degrees: picture's up along the tank.
	{
		Decal R = Side;
		R.Turn = 90.0;
		const Frame F = FrameOf(R);
		Check(std::fabs(std::fabs(F.U.x) - 1.0) < 1e-6, "turning turns it");
	}
	// An arch: the middle of the top edge higher than its ends.
	{
		double X = 0.0, Ymid = 0.0, Yend = 0.0;
		ArchPoint(0.5, 0.0, 30.0, 6.0, 0.5, X, Ymid);
		ArchPoint(0.0, 0.0, 30.0, 6.0, 0.5, X, Yend);
		Check(Ymid > Yend + 1.0, "an arch bends");
		Decal Ar = Side;
		Ar.Arch = 0.5;
		Skin KA;
		Check(MakeSkin(T, Ar, KA) && KA.Tris() > K.Tris(), "an arch's skin reaches further");
	}
	// The top of the tank.
	{
		Decal Top = Side;
		Top.At = V3(0.0, 0.0, 13.0);
		Top.Normal = V3(0.0, 0.0, 1.0);
		Skin KT;
		bool bTop = MakeSkin(T, Top, KT);
		for (const V3& P : KT.Pos) bTop = bTop && P.z > 0.0;
		Check(bTop, "on top: only the top skin");
	}
	// Picking the surface.
	{
		V3 Hit, N;
		Check(RayHit(T, V3(0.0, 100.0, 0.0), V3(0.0, -1.0, 0.0), Hit, N) && std::fabs(Hit.y - 17.0) < 0.3 && N.y > 0.9, "the mouse finds the near surface");
		Check(!RayHit(T, V3(0.0, 100.0, 50.0), V3(0.0, -1.0, 0.0), Hit, N), "and nothing over the top");
	}
	// A set moved onto a bigger part: onto its surface, along its own normal.
	{
		Decal Far = Side;
		Far.At = V3(0.0, 17.0, 0.0);
		std::vector<V3> Big;
		for (const V3& P : T) Big.push_back(V3(P.x * 1.2, P.y * 1.2, P.z * 1.2));
		const std::vector<Decal> Moved = Snapped(Big, {Far});
		Check(Moved.size() == 1 && std::fabs(Moved[0].At.y - 17.0 * 1.2) < 0.4, "a set snaps onto another part");
		Decal Lost = Side;
		Lost.At = V3(0.0, 0.0, 500.0);
		Lost.Normal = V3(1.0, 0.0, 0.0);
		Check(Snapped(Big, {Lost}).size() == 1, "found again from the part's middle");
	}
	std::printf("%s (%d failed)\n", Fails == 0 ? "ALL PASS" : "SOME FAILED", Fails);
	return Fails == 0 ? 0 : 1;
}
