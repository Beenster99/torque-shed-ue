// [warehouses] The warehouse kit: reading it, choosing a design for a shed, laying it on the outline. See
// WorldRideWarehouses.h.
#include "WorldRideWarehouses.h"

#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "FitJson.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex WarehouseKitLock;
std::shared_ptr<const WarehouseKit>& WarehouseKitTable()
{
	static std::shared_ptr<const WarehouseKit> Table;
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

std::vector<std::string> WordsOf(const TorqueFit::Json* J)
{
	std::vector<std::string> Out;
	if (J && J->IsArray())
		for (const TorqueFit::Json& E : J->A) Out.push_back(E.S);
	return Out;
}
} // namespace

std::string ExpandBays(const std::string& Pattern, int N)
{
	std::string Out;
	int Star = -1;
	for (char C : Pattern)
	{
		if (C == ' ') continue;
		if (C == '*') { if (!Out.empty()) Star = int(Out.size()) - 1; }
		else Out.push_back(C);
	}
	if (Star >= 0)
	{
		const char Fill = Out[size_t(Star)];
		while (int(Out.size()) < N) Out.insert(Out.begin() + Star, Fill);
		while (int(Out.size()) > N && std::count(Out.begin(), Out.end(), Fill) > 1) Out.erase(Out.begin() + Star);
	}
	while (int(Out.size()) < N) Out.push_back('.');
	Out.resize(size_t(std::max(0, N)));
	return Out;
}

int WarehouseKit::Pick(const std::string& Class, double Long, double Short, double Seed) const
{
	// (those that suit its class and size; of them, the ones whose depth is near its own, by its seed)
	std::vector<std::pair<int, double>> Fits;
	double Best = 1e9;
	const double Area = Long * Short;
	for (size_t I = 0; I < Designs.size(); ++I)
	{
		const WarehouseDesign& D = Designs[I];
		if (std::find(D.Classes.begin(), D.Classes.end(), Class) == D.Classes.end()) continue;
		if (Short < D.ShortLo || Short > D.ShortHi || Area < D.AreaLo || Area > D.AreaHi || Long < D.Ratio * Short || D.Weight <= 0.0) continue;
		if (Short / D.W < StretchLo || Short / D.W > StretchHi) continue;
		const double Off = std::fabs(std::log(Short / D.W));
		Fits.push_back({int(I), Off});
		Best = std::fmin(Best, Off);
	}
	double Sum = 0.0;
	for (const auto& F : Fits) if (F.second <= Best + 0.35) Sum += Designs[size_t(F.first)].Weight;
	double At = Seed * Sum;
	for (const auto& F : Fits)
	{
		if (F.second > Best + 0.35) continue;
		At -= Designs[size_t(F.first)].Weight;
		if (At <= 0.0) return F.first;
	}
	return Fits.empty() ? -1 : Fits.back().first;
}

std::shared_ptr<WarehouseKit> ReadWarehouseKit(const std::string& JsonText, const std::string& Bin)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(JsonText, Root) || !Root.IsObject()) return nullptr;
	auto Kit = std::make_shared<WarehouseKit>();
	if (const TorqueFit::Json* S = Root.Get("stretch"); S && S->IsArray() && S->A.size() == 2) { Kit->StretchLo = S->A[0].N; Kit->StretchHi = S->A[1].N; }
	// (the pieces: "TSWK", version 1, a count, then each: its name, parts, vertices, triangles)
	KitBytes R{Bin};
	if (Bin.size() < 12 || Bin.compare(0, 4, "TSWK") != 0) return nullptr;
	R.At = 4;
	if (R.Get<uint32_t>() != 1) return nullptr;
	const uint32_t Count = R.Get<uint32_t>();
	std::map<std::string, int> Named;
	for (uint32_t I = 0; I < Count && !R.Bad; ++I)
	{
		WarehousePiece P;
		const uint16_t NameLen = R.Get<uint16_t>();
		if (!R.Has(NameLen)) break;
		const std::string Name(Bin.data() + R.At, NameLen);
		R.At += NameLen;
		const uint16_t Parts = R.Get<uint16_t>();
		for (uint16_t K = 0; K < Parts; ++K)
		{
			WarehousePiece::Part Pt;
			Pt.Section = R.Get<uint8_t>(); Pt.R = R.Get<uint8_t>(); Pt.G = R.Get<uint8_t>(); Pt.B = R.Get<uint8_t>(); Pt.Code = R.Get<uint8_t>(); Pt.Lod = R.Get<uint8_t>();
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
		Named[Name] = int(Kit->Pieces.size());
		Kit->Pieces.push_back(std::move(P));
	}
	if (R.Bad || Kit->Pieces.size() != Count) return nullptr;
	const TorqueFit::Json* Rows = Root.Get("designs");
	if (!Rows || !Rows->IsArray()) return nullptr;
	for (const TorqueFit::Json& Row : Rows->A)
	{
		WarehouseDesign D;
		D.Name = Row.Str("name");
		D.Label = Row.Str("label");
		D.Roof = Row.Str("roof", "pitch");
		D.L = Row.Num("L"); D.W = Row.Num("W"); D.H = Row.Num("H");
		D.Top = Row.Num("top", D.H); D.GableTop = Row.Num("gable_top", D.H);
		D.Bay = Row.Num("bay", 7.5); D.GableBay = Row.Num("gable_bay", 6.0);
		D.Spans = std::max(1, int(Row.Num("spans", 1.0)));
		D.Front = Row.Str("front", ".*"); D.Back = Row.Str("back", ".*"); D.Right = Row.Str("right", ".*"); D.Left = Row.Str("left", ".*");
		if (const TorqueFit::Json* B = Row.Get("solar")) D.Solar = B->B;
		if (const TorqueFit::Json* B = Row.Get("dock_floor")) D.DockFloor = B->B;
		if (const TorqueFit::Json* S = Row.Get("skirt"); S && S->IsArray() && S->A.size() == 3)
			for (size_t K = 0; K < 3; ++K) D.Skirt[K] = float(S->A[K].N);
		D.Classes = WordsOf(Row.Get("classes"));
		if (const TorqueFit::Json* S = Row.Get("short"); S && S->IsArray() && S->A.size() == 2) { D.ShortLo = S->A[0].N; D.ShortHi = S->A[1].N; }
		if (const TorqueFit::Json* S = Row.Get("area"); S && S->IsArray() && S->A.size() == 2) { D.AreaLo = S->A[0].N; D.AreaHi = S->A[1].N; }
		D.Ratio = Row.Num("ratio", 0.0);
		D.Weight = Row.Num("weight", 1.0);
		if (const TorqueFit::Json* Ps = Row.Get("pieces"); Ps && Ps->IsObject())
			for (const auto& KV : Ps->O)
			{
				const auto It = Named.find(D.Name + "/" + std::string(KV.first));
				if (It != Named.end()) D.Pieces[std::string(KV.first)] = It->second;
			}
		if (D.W <= 1.0 || D.Bay <= 0.5 || D.GableBay <= 0.5 || D.Piece("long:.") < 0 || D.Piece("roof:P") < 0) continue;
		Kit->Designs.push_back(std::move(D));
	}
	if (Kit->Designs.empty()) return nullptr;
	return Kit;
}

bool SetWarehouseKit(const std::string& JsonText, const std::string& Bin)
{
	std::shared_ptr<WarehouseKit> Kit = ReadWarehouseKit(JsonText, Bin);
	if (!Kit) return false;
	std::lock_guard<std::mutex> Hold(WarehouseKitLock);
	WarehouseKitTable() = std::move(Kit);
	return true;
}

std::shared_ptr<const WarehouseKit> WarehouseKitNow()
{
	std::lock_guard<std::mutex> Hold(WarehouseKitLock);
	return WarehouseKitTable();
}

namespace
{
// One piece laid on a wall: its x along (Ax, Az) from (Ox, Oz) stretched Sx, its z out along (Nx, Nz)
// stretched Sz, its y up from Y, stretched SAbove above Pivot (a roof's or a gable's rise with its depth).
size_t LayPiece(const WarehousePiece& P, double Ox, double Oz, double Ax, double Az, double Nx, double Nz, double Sx, double Sz, double Y,
	double Pivot, double SAbove, int Need, RideMesh* const* Into)
{
	for (int K = 0; K < 5; ++K)
		if (Into[K]->Alpha.size() < Into[K]->Vertices()) Into[K]->Alpha.resize(Into[K]->Vertices(), 1.0f);
	std::vector<uint32_t> Where(P.Vertices(), UINT32_MAX);
	std::vector<double> World(P.Vertices() * 3), Normal(P.Vertices() * 3);
	for (size_t V = 0; V < P.Vertices(); ++V)
	{
		const WarehousePiece::Part& Pt = P.Parts[P.PartOf[V]];
		if (Pt.Lod < Need) continue;
		const int S = Pt.Section < 5 ? Pt.Section : 0;
		RideMesh& Mi = *Into[S];
		const double Lx = P.Pos[V * 3] * 0.01, Ly0 = P.Pos[V * 3 + 1] * 0.01, Lz = P.Pos[V * 3 + 2] * 0.01;
		const bool Over = Ly0 > Pivot;
		const double Ly = Over ? Pivot + (Ly0 - Pivot) * SAbove : Ly0;
		const double Wx = Ox + Lx * Sx * Ax + Lz * Sz * Nx, Wy = Y + Ly, Wz = Oz + Lx * Sx * Az + Lz * Sz * Nz;
		// (the normal under the stretch: over each way's stretch, then turned)
		const double N0 = P.Nrm[V * 3] / Sx, N1 = P.Nrm[V * 3 + 1] / (Over ? SAbove : 1.0), N2 = P.Nrm[V * 3 + 2] / Sz;
		double Gx = N0 * Ax + N2 * Nx, Gy = N1, Gz = N0 * Az + N2 * Nz;
		const double Nl = std::sqrt(Gx * Gx + Gy * Gy + Gz * Gz);
		if (Nl > 1e-9) { Gx /= Nl; Gy /= Nl; Gz /= Nl; } else { Gx = 0.0; Gy = 1.0; Gz = 0.0; }
		World[V * 3] = Wx; World[V * 3 + 1] = Wy; World[V * 3 + 2] = Wz;
		Normal[V * 3] = Gx; Normal[V * 3 + 1] = Gy; Normal[V * 3 + 2] = Gz;
		Where[V] = uint32_t(Mi.Vertices());
		const double Px = Wx - Mi.OriginX, Pz = Wz - Mi.OriginZ;
		Mi.Pos.push_back(float(Px)); Mi.Pos.push_back(float(Wy)); Mi.Pos.push_back(float(Pz));
		Mi.Nrm.push_back(float(Gx)); Mi.Nrm.push_back(float(Gy)); Mi.Nrm.push_back(float(Gz));
		// (UVs metres of surface, as the church kit's: a sheet's along it level and up it; a flat top's east and south)
		const double Hl = std::sqrt(Gx * Gx + Gz * Gz);
		const double Tx = Hl > 1e-6 ? -Gz / Hl : 1.0, Tz = Hl > 1e-6 ? Gx / Hl : 0.0;
		double U, W;
		if (S == 2)
		{
			double Sx2 = Gy * Tz, Sy2 = Gz * Tx - Gx * Tz, Sz2 = -Gy * Tx;   // (N x T)
			if (Sy2 < 0.0) { Sx2 = -Sx2; Sy2 = -Sy2; Sz2 = -Sz2; }
			U = Px * Tx + Pz * Tz;
			W = -(Px * Sx2 + Wy * Sy2 + Pz * Sz2);
		}
		else if (std::fabs(Gy) > 0.7) { U = Px; W = Pz; }
		else { U = Px * Tx + Pz * Tz; W = -Wy; }
		Mi.Uv.push_back(float(U)); Mi.Uv.push_back(float(W));
		Mi.Col.push_back(Pt.R / 255.0f); Mi.Col.push_back(Pt.G / 255.0f); Mi.Col.push_back(Pt.B / 255.0f);
		Mi.Alpha.push_back(Pt.Code / 255.0f);
	}
	size_t Laid = 0;
	for (size_t T = 0; T + 2 < P.Idx.size(); T += 3)
	{
		const uint16_t A = P.Idx[T], B = P.Idx[T + 1], C = P.Idx[T + 2];
		if (Where[A] == UINT32_MAX || Where[B] == UINT32_MAX || Where[C] == UINT32_MAX) continue;
		const int S = P.Parts[P.PartOf[A]].Section < 5 ? P.Parts[P.PartOf[A]].Section : 0;
		// (wound to face the way its corners' normals do, whichever way the wall's turn and the stretch left it)
		const double* Pa = &World[size_t(A) * 3];
		const double* Pb = &World[size_t(B) * 3];
		const double* Pc = &World[size_t(C) * 3];
		const double E1[3] = {Pb[0] - Pa[0], Pb[1] - Pa[1], Pb[2] - Pa[2]}, E2[3] = {Pc[0] - Pa[0], Pc[1] - Pa[1], Pc[2] - Pa[2]};
		const double F[3] = {E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]};
		double Dot = 0.0;
		for (const uint16_t V : {A, B, C})
			for (int K = 0; K < 3; ++K) Dot += F[K] * Normal[size_t(V) * 3 + size_t(K)];
		if (Dot >= 0.0) Into[S]->Idx.insert(Into[S]->Idx.end(), {Where[A], Where[B], Where[C]});
		else Into[S]->Idx.insert(Into[S]->Idx.end(), {Where[A], Where[C], Where[B]});
		++Laid;
	}
	return Laid;
}
} // namespace

size_t BuildWarehouse(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, double Y, double Low, int Detail, RideMesh& Out)
{
	RideMesh* const Into[5] = {&Out, &Out, &Out.RoofingOf(), &Out.BrickworkOf(), &Out.RenderOf()};
	return BuildWarehouse(Kit, D, CornerX, CornerZ, Ux, Uz, Nx, Nz, Long, Short, Y, Low, Detail, Into);
}

size_t BuildWarehouse(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, double Y, double Low, int Detail, RideMesh* const Into[5])
{
	using namespace RideSceneBits;
	const int Need = Detail >= 2 ? 0 : Detail == 1 ? 1 : 2;
	const double Rise = std::clamp(Short / D.W, 0.6, 1.6);   // (a roof's and a gable's rise, with the depth)
	const double Sz = Short / D.W;
	size_t Laid = 0;
	const auto Lay = [&](int Piece, double Ox, double Oz, double Ax, double Az, double Mx, double Mz, double Sx, double Sz2, double Pivot, double SAbove)
	{
		if (Piece >= 0 && size_t(Piece) < Kit.Pieces.size()) Laid += LayPiece(Kit.Pieces[size_t(Piece)], Ox, Oz, Ax, Az, Mx, Mz, Sx, Sz2, Y, Pivot, SAbove, Need, Into);
	};
	// The four walls: each its start, its way along and its way out (the front's left end, seen from outside, first).
	struct WallRun { double X, Z, Ax, Az, Nx, Nz, Len; bool IsLong; const std::string* Pattern; };
	const WallRun Walls[4] = {
		{CornerX, CornerZ, Ux, Uz, Nx, Nz, Long, true, &D.Front},
		{CornerX + Ux * Long - Nx * Short, CornerZ + Uz * Long - Nz * Short, -Ux, -Uz, -Nx, -Nz, Long, true, &D.Back},
		{CornerX + Ux * Long, CornerZ + Uz * Long, -Nx, -Nz, Ux, Uz, Short, false, &D.Right},
		{CornerX - Nx * Short, CornerZ - Nz * Short, Nx, Nz, -Ux, -Uz, Short, false, &D.Left}};
	for (const WallRun& Wr : Walls)
	{
		const double Bay = Wr.IsLong ? D.Bay : D.GableBay;
		const int N = D.Roof == "multi" && !Wr.IsLong ? 2 * D.Spans : std::max(1, int(std::lround(Wr.Len / Bay)));
		const double Bw = Wr.Len / N;
		const std::string Letters = ExpandBays(*Wr.Pattern, N);
		const std::string Kind = Wr.IsLong ? "long:" : "gable:";
		for (int I = 0; I < N; ++I)
		{
			int Piece = D.Piece(Kind + Letters[size_t(I)]);
			if (Piece < 0) Piece = D.Piece(Kind + ".");
			if (Piece < 0) Piece = D.Piece("long:.");
			Lay(Piece, Wr.X + Wr.Ax * Bw * I, Wr.Z + Wr.Az * Bw * I, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, Bw / Bay, 1.0, 1e9, 1.0);
		}
		// (its corner flashings, at both ends)
		const int Corner = D.Piece("corner");
		Lay(Corner, Wr.X, Wr.Z, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, 1.0, 1.0, 1e9, 1.0);
		Lay(Corner, Wr.X + Wr.Ax * Wr.Len, Wr.Z + Wr.Az * Wr.Len, -Wr.Ax, -Wr.Az, Wr.Nx, Wr.Nz, 1.0, 1.0, 1e9, 1.0);
		// (the gable above the eaves, right across)
		if (!Wr.IsLong) Lay(D.Piece("gabletop"), Wr.X, Wr.Z, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, Short / D.W, 1.0, D.GableTop, Rise);
	}
	// The roof, a long bay at a time: rooflights in every other bay, solar over the middle ones where it has them.
	{
		const int N = std::max(1, int(std::lround(Long / D.Bay)));
		const double Bw = Long / N;
		for (int I = 0; I < N; ++I)
		{
			const bool End = I == 0 || I == N - 1, Lit = !End && (I % 2) == 1, Sun = D.Solar && !End;
			int Piece = D.Piece(Sun ? (Lit ? "roof:SL" : "roof:S") : (Lit ? "roof:L" : "roof:P"));
			if (Piece < 0) Piece = D.Piece("roof:P");
			Lay(Piece, CornerX + Ux * Bw * I, CornerZ + Uz * Bw * I, Ux, Uz, Nx, Nz, Bw / D.Bay, Sz, D.Top, Rise);
		}
		const int Eave = D.Piece("roofend");
		Lay(Eave, CornerX, CornerZ, Ux, Uz, Nx, Nz, 1.0, Sz, D.Top, Rise);
		Lay(Eave, CornerX + Ux * Long, CornerZ + Uz * Long, -Ux, -Uz, Nx, Nz, 1.0, Sz, D.Top, Rise);
	}
	// Its office annex on the left gable, where the design has one and the shed is deep enough to carry it.
	if (Short >= 20.0)
		Lay(D.Piece("annex"), Walls[3].X, Walls[3].Z, Walls[3].Ax, Walls[3].Az, Walls[3].Nx, Walls[3].Nz, Short / D.W, 1.0, 1e9, 1.0);
	// On a slope, its plinth run down to the ground's lowest round it.
	if (Y - Low > 0.3)
	{
		const Rgb Skirt = {D.Skirt[0], D.Skirt[1], D.Skirt[2]};
		const double Mx = CornerX + Ux * Long * 0.5 - Nx * Short * 0.5, Mz = CornerZ + Uz * Long * 0.5 - Nz * Short * 0.5;
		for (const WallRun& Wr : Walls)
		{
			const double Ex = Wr.X + Wr.Ax * Wr.Len, Ez = Wr.Z + Wr.Az * Wr.Len;
			const P3 Away = At3(Mx + (Wr.X + Ex - 2.0 * Mx) * 25.0, Y, Mz + (Wr.Z + Ez - 2.0 * Mz) * 25.0);
			Quad(*Into[4], At3(Wr.X + Wr.Nx * 0.05, Low - 0.4, Wr.Z + Wr.Nz * 0.05), At3(Ex + Wr.Nx * 0.05, Low - 0.4, Ez + Wr.Nz * 0.05),
				At3(Ex + Wr.Nx * 0.05, Y - 0.55, Ez + Wr.Nz * 0.05), At3(Wr.X + Wr.Nx * 0.05, Y - 0.55, Wr.Z + Wr.Nz * 0.05), Away, Skirt, false, 0.08f);
			Laid += 2;
		}
	}
	return Laid;
}
} // namespace TorqueWorld
