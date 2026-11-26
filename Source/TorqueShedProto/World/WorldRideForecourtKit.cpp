// [forecourts] The forecourt kit: reading it, and laying a piece in a forecourt. See WorldRideForecourtKit.h.
#include "WorldRideForecourtKit.h"

#include "WorldRide.h"
#include "FitJson.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex ForecourtKitLock;
std::shared_ptr<const ForecourtKit>& ForecourtKitTable()
{
	static std::shared_ptr<const ForecourtKit> Table;
	return Table;
}

struct KitBytes
{
	const std::string& S;
	size_t At = 0;
	bool Bad = false;
	bool Has(size_t N) { if (At + N > S.size()) Bad = true; return !Bad; }
	template <typename T> T Get()
	{
		T V{};
		if (!Has(sizeof(T))) return V;
		std::memcpy(&V, S.data() + At, sizeof(T));
		At += sizeof(T);
		return V;
	}
};

void ColourOf(const TorqueFit::Json* J, float Out[3])
{
	if (J && J->IsArray() && J->A.size() == 3)
		for (size_t K = 0; K < 3; ++K) Out[K] = float(J->A[K].N);
}
} // namespace

bool SetForecourtKit(const std::string& JsonText, const std::string& Bin)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(JsonText, Root) || !Root.IsObject()) return false;
	auto Kit = std::make_shared<ForecourtKit>();
	// (the pieces: "TSFK", version 1, a count, then each: its name, parts, vertices, triangles)
	KitBytes R{Bin};
	if (Bin.size() < 12 || Bin.compare(0, 4, "TSFK") != 0) return false;
	R.At = 4;
	if (R.Get<uint32_t>() != 1) return false;
	const uint32_t Count = R.Get<uint32_t>();
	for (uint32_t I = 0; I < Count && !R.Bad; ++I)
	{
		ForecourtPiece P;
		const uint16_t NameLen = R.Get<uint16_t>();
		if (!R.Has(NameLen)) break;
		const std::string Name(Bin.data() + R.At, NameLen);
		R.At += NameLen;
		const uint16_t Parts = R.Get<uint16_t>();
		for (uint16_t K = 0; K < Parts; ++K)
		{
			ForecourtPiece::Part Pt;
			Pt.Section = R.Get<uint8_t>(); Pt.R = R.Get<uint8_t>(); Pt.G = R.Get<uint8_t>(); Pt.B = R.Get<uint8_t>();
			Pt.Code = R.Get<uint8_t>(); Pt.Lod = R.Get<uint8_t>(); Pt.Slot = R.Get<uint8_t>();
			P.Parts.push_back(Pt);
		}
		const uint32_t Verts = R.Get<uint32_t>(), Tris = R.Get<uint32_t>();
		if (Verts > 65535 || P.Parts.empty() || !R.Has(size_t(Verts) * 10 + size_t(Tris) * 6)) { R.Bad = true; break; }
		P.Pos.resize(size_t(Verts) * 3);
		P.Nrm.resize(size_t(Verts) * 3);
		P.PartOf.resize(Verts);
		for (uint32_t V = 0; V < Verts; ++V)
		{
			for (int K = 0; K < 3; ++K) P.Pos[size_t(V) * 3 + size_t(K)] = R.Get<int16_t>();
			for (int K = 0; K < 3; ++K) P.Nrm[size_t(V) * 3 + size_t(K)] = R.Get<int8_t>();
			P.PartOf[V] = std::min<uint8_t>(R.Get<uint8_t>(), uint8_t(P.Parts.size() - 1));
		}
		P.Idx.resize(size_t(Tris) * 3);
		for (uint16_t& Ix : P.Idx) { Ix = R.Get<uint16_t>(); if (Ix >= Verts) Ix = 0; }
		Kit->Named[Name] = int(Kit->Pieces.size());
		Kit->Pieces.push_back(std::move(P));
	}
	if (R.Bad || Kit->Pieces.size() != Count) return false;
	if (const TorqueFit::Json* Ls = Root.Get("liveries"); Ls && Ls->IsArray())
		for (const TorqueFit::Json& Row : Ls->A)
		{
			ForecourtKit::Livery L;
			ColourOf(Row.Get("main"), L.Main);
			ColourOf(Row.Get("accent"), L.Accent);
			Kit->Liveries.push_back(L);
		}
	if (const TorqueFit::Json* St = Root.Get("styles"); St && St->IsObject()) Kit->BrickShare = std::clamp(St->Num("brick", 0.3), 0.0, 1.0);
	Kit->KioskBay = std::fmax(0.5, Root.Num("kiosk_bay", 2.5));
	// (the pieces a forecourt can't do without; a kit missing one isn't used)
	for (const char* Must : {"canopy_2", "canopy_3", "island", "price_pole", "kiosk_glass_glass", "kiosk_glass_door", "kiosk_glass_end", "kiosk_brick_glass",
			 "kiosk_brick_door", "kiosk_brick_end"})
		if (Kit->Piece(Must) < 0) return false;
	if (Kit->Liveries.empty()) Kit->Liveries.push_back(ForecourtKit::Livery());
	std::lock_guard<std::mutex> Hold(ForecourtKitLock);
	ForecourtKitTable() = std::move(Kit);
	return true;
}

namespace
{
std::atomic<bool> RoadsideWanted{true};
}

void SetRoadsideOn(bool On)
{
	RoadsideWanted.store(On);
}

bool RoadsideOn()
{
	return RoadsideWanted.load();
}

std::shared_ptr<const ForecourtKit> ForecourtKitNow()
{
	std::lock_guard<std::mutex> Hold(ForecourtKitLock);
	return ForecourtKitTable();
}

size_t LayForecourtPiece(const ForecourtPiece& P, const ForecourtFrame& F, const ForecourtPlace& At, const std::function<double(double, double)>& Ground,
	const ForecourtKit::Livery& L, int Need, RideMesh& Out)
{
	// (the night's lights: the soffit's panels, the shop's glow, the price digits, a lit sign)
	static const float Lit[4][3] = {{2.6f, 2.6f, 2.4f}, {2.2f, 1.9f, 1.5f}, {2.4f, 1.3f, 0.2f}, {2.3f, 2.3f, 2.2f}};
	RideMesh* Into[6] = {&Out, &Out, &Out.RoofingOf(), &Out.BrickworkOf(), &Out.RenderOf(), &Out.NightOf()};
	for (RideMesh* Mi : Into)
		if (Mi->Alpha.size() < Mi->Vertices()) Mi->Alpha.resize(Mi->Vertices(), 1.0f);
	// (the normals under the plan's turn and stretch: its inverse, transposed)
	const double Det = At.Xu * At.Yv - At.Yu * At.Xv;
	const double Ia = At.Yv / Det, Ib = -At.Xv / Det, Ic = -At.Yu / Det, Id = At.Xu / Det;   // (n_u = Ia nx + Ib ny, n_v = Ic nx + Id ny)
	std::vector<uint32_t> Where(P.Vertices(), UINT32_MAX);
	std::vector<double> World(P.Vertices() * 3), Normal(P.Vertices() * 3);
	std::vector<uint8_t> Mesh(P.Vertices(), 0);
	for (size_t V = 0; V < P.Vertices(); ++V)
	{
		const ForecourtPiece::Part& Pt = P.Parts[P.PartOf[V]];
		if (Pt.Lod < Need) continue;
		const int S = Pt.Slot >= 10 ? 5 : Pt.Section < 5 ? Pt.Section : 0;
		Mesh[V] = uint8_t(S);
		RideMesh& Mi = *Into[S];
		const double Lx = P.Pos[V * 3] * 0.01, Ly = P.Pos[V * 3 + 1] * 0.01, Lz = P.Pos[V * 3 + 2] * 0.01;
		const double Au = At.Ou + At.Xu * Lx + At.Yu * Ly, Av = At.Ov + At.Xv * Lx + At.Yv * Ly;
		const double Wx = F.Ex + F.Ux * Au + F.Vx * Av, Wz = F.Ez + F.Uz * Au + F.Vz * Av;
		const double Wy = Lz > 0.001 ? At.Base + Lz : Ground(Au, Av) + Lz;
		const double Nx0 = P.Nrm[V * 3] / 127.0, Ny0 = P.Nrm[V * 3 + 1] / 127.0, Nz0 = P.Nrm[V * 3 + 2] / 127.0;
		const double Nu = Ia * Nx0 + Ib * Ny0, Nv = Ic * Nx0 + Id * Ny0;
		double Gx = F.Ux * Nu + F.Vx * Nv, Gy = Nz0, Gz = F.Uz * Nu + F.Vz * Nv;
		const double Nl = std::sqrt(Gx * Gx + Gy * Gy + Gz * Gz);
		if (Nl > 1e-9) { Gx /= Nl; Gy /= Nl; Gz /= Nl; } else { Gx = 0.0; Gy = 1.0; Gz = 0.0; }
		World[V * 3] = Wx; World[V * 3 + 1] = Wy; World[V * 3 + 2] = Wz;
		Normal[V * 3] = Gx; Normal[V * 3 + 1] = Gy; Normal[V * 3 + 2] = Gz;
		Where[V] = uint32_t(Mi.Vertices());
		const double Px = Wx - Mi.OriginX, Pz = Wz - Mi.OriginZ;
		Mi.Pos.push_back(float(Px)); Mi.Pos.push_back(float(Wy)); Mi.Pos.push_back(float(Pz));
		Mi.Nrm.push_back(float(Gx)); Mi.Nrm.push_back(float(Gy)); Mi.Nrm.push_back(float(Gz));
		// (UVs metres of surface: a wall's along it level and down it, a flat top's east and south, a roof's along
		// the eaves and up the slope)
		const double Hl = std::sqrt(Gx * Gx + Gz * Gz);
		const double Tx = Hl > 1e-6 ? -Gz / Hl : 1.0, Tz = Hl > 1e-6 ? Gx / Hl : 0.0;
		double U, W;
		if (S == 2 && std::fabs(Gy) < 0.98)
		{
			double Sx2 = Gy * Tz, Sy2 = Gz * Tx - Gx * Tz, Sz2 = -Gy * Tx;   // (N x T)
			if (Sy2 < 0.0) { Sx2 = -Sx2; Sy2 = -Sy2; Sz2 = -Sz2; }
			U = Px * Tx + Pz * Tz;
			W = -(Px * Sx2 + Wy * Sy2 + Pz * Sz2);
		}
		else if (std::fabs(Gy) > 0.7) { U = Px; W = Pz; }
		else { U = Px * Tx + Pz * Tz; W = -Wy; }
		Mi.Uv.push_back(float(U)); Mi.Uv.push_back(float(W));
		float C[3] = {Pt.R / 255.0f, Pt.G / 255.0f, Pt.B / 255.0f};
		if (Pt.Slot == 1) std::memcpy(C, L.Main, sizeof(C));
		else if (Pt.Slot == 2) std::memcpy(C, L.Accent, sizeof(C));
		else if (Pt.Slot >= 10 && Pt.Slot <= 13) std::memcpy(C, Lit[Pt.Slot - 10], sizeof(C));
		Mi.Col.push_back(C[0]); Mi.Col.push_back(C[1]); Mi.Col.push_back(C[2]);
		// (alpha: the roofing or the dirt; 0 on the night's glow, dark glass by day)
		Mi.Alpha.push_back(S == 5 ? 0.0f : Pt.Code / 255.0f);
	}
	size_t Laid = 0;
	for (size_t T = 0; T + 2 < P.Idx.size(); T += 3)
	{
		const uint16_t A = P.Idx[T], B = P.Idx[T + 1], C = P.Idx[T + 2];
		if (Where[A] == UINT32_MAX || Where[B] == UINT32_MAX || Where[C] == UINT32_MAX) continue;
		RideMesh& Mi = *Into[Mesh[A]];
		// (wound to face the way its corners' normals do, whichever way a mirror or stretch left it)
		const double* Pa = &World[size_t(A) * 3];
		const double* Pb = &World[size_t(B) * 3];
		const double* Pc = &World[size_t(C) * 3];
		const double E1[3] = {Pb[0] - Pa[0], Pb[1] - Pa[1], Pb[2] - Pa[2]}, E2[3] = {Pc[0] - Pa[0], Pc[1] - Pa[1], Pc[2] - Pa[2]};
		const double Fn[3] = {E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]};
		double Dot = 0.0;
		for (const uint16_t V : {A, B, C})
			for (int K = 0; K < 3; ++K) Dot += Fn[K] * Normal[size_t(V) * 3 + size_t(K)];
		if (Dot >= 0.0) Mi.Idx.insert(Mi.Idx.end(), {Where[A], Where[B], Where[C]});
		else Mi.Idx.insert(Mi.Idx.end(), {Where[A], Where[C], Where[B]});
		++Laid;
	}
	return Laid;
}
} // namespace TorqueWorld
