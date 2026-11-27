// [warehouses] The warehouse kit: reading it, choosing a design for a shed, laying it on the outline. See
// WorldRideWarehouses.h.
#include "WorldRideWarehouses.h"

#include "WorldRide.h"
#include "WorldRideSceneBits.h"
#include "WorldRideSigns.h"
#include "FitJson.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>

namespace TorqueWorld
{
namespace
{
std::mutex WarehouseKitLock;
std::atomic<bool> WarehouseInstancingOn{false};   // [round 175 fps]
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

std::vector<WarehouseSign> SignsIn(const TorqueFit::Json* J)
{
	std::vector<WarehouseSign> Out;
	if (!J || !J->IsArray()) return Out;
	for (const TorqueFit::Json& E : J->A)
	{
		if (!E.IsArray() || E.A.size() < 8) continue;
		WarehouseSign S;
		S.X = E.A[0].N; S.Y = E.A[1].N; S.Z = E.A[2].N; S.Fx = E.A[3].N; S.Fz = E.A[4].N; S.W = E.A[5].N; S.H = E.A[6].N; S.Kind = E.A[7].S;
		Out.push_back(S);
	}
	return Out;
}
} // namespace

bool WarehouseDesign::Retail() const
{
	return Front.find_first_of("SMUG") != std::string::npos;
}

int WarehouseKit::Prop(const std::string& Name) const
{
	for (size_t I = 0; I < Props.size(); ++I)
		if (Props[I].Name == Name) return Props[I].Piece >= 0 ? int(I) : -1;
	return -1;
}

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

bool ReadKitPieces(const std::string& Bin, std::vector<WarehousePiece>& Pieces, std::map<std::string, int>& Named)
{
	// (the pieces: "TSWK", version 1, a count, then each: its name, parts, vertices, triangles)
	KitBytes R{Bin};
	if (Bin.size() < 12 || Bin.compare(0, 4, "TSWK") != 0) return false;
	R.At = 4;
	if (R.Get<uint32_t>() != 1) return false;
	const uint32_t Count = R.Get<uint32_t>();
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
		for (uint32_t V = 0; V < Verts; ++V)
		{
			P.MinX = std::fmin(P.MinX, P.Pos[size_t(V) * 3] * 0.01);
			P.MaxX = std::fmax(P.MaxX, P.Pos[size_t(V) * 3] * 0.01);
		}
		Named[Name] = int(Pieces.size());
		Pieces.push_back(std::move(P));
	}
	return !R.Bad && Pieces.size() == Count;
}

std::shared_ptr<WarehouseKit> ReadWarehouseKit(const std::string& JsonText, const std::string& Bin, bool Yards)
{
	TorqueFit::Json Root;
	if (!TorqueFit::Json::Parse(JsonText, Root) || !Root.IsObject()) return nullptr;
	auto Kit = std::make_shared<WarehouseKit>();
	Kit->Yards = Yards;
	if (const TorqueFit::Json* S = Root.Get("stretch"); S && S->IsArray() && S->A.size() == 2) { Kit->StretchLo = S->A[0].N; Kit->StretchHi = S->A[1].N; }
	std::map<std::string, int> Named;
	if (!ReadKitPieces(Bin, Kit->Pieces, Named)) return nullptr;

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
		if (const TorqueFit::Json* Ss = Row.Get("signs"); Ss && Ss->IsObject())   // [stage 3]
			for (const auto& KV : Ss->O) D.Signs[std::string(KV.first)] = SignsIn(&KV.second);
		if (D.W <= 1.0 || D.Bay <= 0.5 || D.GableBay <= 0.5 || D.Piece("long:.") < 0 || D.Piece("roof:P") < 0) continue;
		Kit->Designs.push_back(std::move(D));
	}
	if (Kit->Designs.empty()) return nullptr;
	if (const TorqueFit::Json* Ps = Root.Get("props"); Ps && Ps->IsArray())   // [stage 3]
		for (const TorqueFit::Json& Row : Ps->A)
		{
			WarehouseProp P;
			P.Name = Row.Str("name");
			const auto It = Named.find("prop/" + P.Name);
			P.Piece = It == Named.end() ? -1 : It->second;
			if (const TorqueFit::Json* S = Row.Get("size"); S && S->IsArray() && S->A.size() == 3) { P.L = S->A[0].N; P.D = S->A[1].N; P.H = S->A[2].N; }
			P.Signs = SignsIn(Row.Get("signs"));
			Kit->Props.push_back(std::move(P));
		}
	return Kit;
}

bool SetWarehouseKit(const std::string& JsonText, const std::string& Bin, bool Yards)
{
	std::shared_ptr<WarehouseKit> Kit = ReadWarehouseKit(JsonText, Bin, Yards);
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

// [round 175 fps] A piece put down: laid into the square's meshes, or, while the game draws the kit's pieces as
// instances (SetWarehouseInstancing) and the kit is the warehouse kit itself (Instance; the farm kit's pieces
// are always laid), handed over as one (Into[0]->KitParts). Its rise above Pivot stretched SAbove
// becomes a stretch of the whole piece about Pivot (the few points of a roof below it move a hand's width at
// most). A mirrored frame (x, up, out left-handed: a far end's corner flashing or roof overhang, a fence's far
// side) can't be an instance's turn: the piece, symmetric along its x, is turned the other way about its middle.
size_t PlacePiece(const WarehouseKit& Kit, int Piece, double Ox, double Oz, double Ax, double Az, double Nx, double Nz, double Sx, double Sz, double Y,
	double Pivot, double SAbove, int Need, bool Instance, RideMesh* const* Into)
{
	if (Piece < 0 || size_t(Piece) >= Kit.Pieces.size()) return 0;
	const WarehousePiece& P = Kit.Pieces[size_t(Piece)];
	if (!Instance || Need >= 2) return LayPiece(P, Ox, Oz, Ax, Az, Nx, Nz, Sx, Sz, Y, Pivot, SAbove, Need, Into);
	RideMesh& Out = *Into[0];
	if (Ax * Nz - Az * Nx < 0.0)
	{
		const double Mid = (P.MinX + P.MaxX) * 0.5;
		Ox += 2.0 * Mid * Sx * Ax;
		Oz += 2.0 * Mid * Sx * Az;
		Ax = -Ax;
		Az = -Az;
	}
	RideKitPart K;
	K.Piece = Piece;
	K.Near = Need <= 0;
	K.X = float(Ox - Out.OriginX);
	K.Z = float(Oz - Out.OriginZ);
	K.Y = float(Pivot < 1e8 ? Y + Pivot * (1.0 - SAbove) : Y);
	K.Sy = float(Pivot < 1e8 ? SAbove : 1.0);
	K.Ax = float(Ax); K.Az = float(Az); K.Nx = float(Nx); K.Nz = float(Nz);
	K.Sx = float(Sx); K.Sz = float(Sz);
	Out.KitParts.push_back(K);
	return P.Idx.size() / 3;
}

// [stage 3] A sign's made-up name, as big as fits its board (no more than half its height), on its face.
void WriteSign(RideMesh& Out, double X, double Y, double Z, double Fx, double Fz, double W, double H, const std::string& Name, const float Colour[3])
{
	double Wide = 0.0;
	if (Name.empty() || !SignText::Width(Name, 1.0, Wide) || Wide <= 1e-6) return;
	const double Cap = std::fmin(H * 0.5, W * 0.86 / Wide);
	if (Cap < 0.12) return;
	const double Fl = std::hypot(Fx, Fz);
	if (Fl < 1e-6) return;
	SignText::Set(Out.SignsOf(), X + Fx / Fl * 0.015, Y, Z + Fz / Fl * 0.015, Fx / Fl, Fz / Fl, Cap, Name, Colour, 0);
}

struct SignSpot { double X, Y, Z, Fx, Fz, W, H; const std::string* Kind; };
} // namespace

std::string WarehouseName(const std::string& Kind, double Roll, double Roll2)
{
	static const char* const Families[] = {"Ashworth", "Barraclough", "Brearley", "Calvert", "Crowther", "Dewhurst", "Dobson", "Fairbairn",
		"Fawcett", "Garside", "Haigh", "Hepworth", "Hirst", "Holroyd", "Ingham", "Jowett", "Kershaw", "Kitson", "Lumb", "Mallinson",
		"Marsden", "Metcalfe", "Nuttall", "Openshaw", "Ormerod", "Pollard", "Rushworth", "Sutcliffe", "Tillotson", "Whiteley", "Wainwright",
		"Thornton", "Pickering", "Holdsworth", "Cockcroft", "Bramley", "Ackroyd", "Bairstow", "Greenwood", "Midgley"};
	static const char* const Places[] = {"Oakfield", "Riverside", "Brookway", "Millbrook", "Kingsmead", "Westgate", "Ashgrove", "Fairfield",
		"Moorside", "Northbank", "Stonebridge", "Larkfield", "Cherrywood", "Copperfield", "Greenacres", "Hawthorn", "Elmtree", "Beacon",
		"Meadowbank", "Foxhills", "Heronsgate", "Willowbrook", "Station", "Canalside", "Ironbridge", "Southmead", "Eastfield", "Brindley"};
	static const char* const Shops[] = {"Furniture", "Beds", "Carpets", "Pets", "Home Store", "Sports", "Outdoor", "Electricals", "Toys",
		"Tiles", "Kitchens", "Bathrooms", "Lighting", "Garden Centre", "Discount Store", "Paints", "Baby & Nursery", "Office Supplies",
		"Cycles", "Fabrics", "Flooring", "Sofas"};
	static const char* const Trades[] = {"Plumbing", "Timber", "Tool Hire", "Electrical", "Tyres", "Paints", "Motor Factors",
		"Building Supplies", "Tiles", "Fasteners", "Glass", "Roofing", "Heating", "Joinery"};
	static const char* const Firms[] = {"Distribution", "Logistics", "Freight", "Haulage", "Cold Store", "Engineering", "Packaging",
		"Storage", "Fabrications", "Print"};
	static const char* const Estates[] = {"Business Park", "Industrial Estate", "Trade Park", "Enterprise Park", "Trading Estate"};
	const auto Of = [](const char* const* List, int N, double R) { return std::string(List[std::min(N - 1, std::max(0, int(R * N)))]); };
	const double R2 = std::fmod(Roll * 7.31 + Roll2, 1.0);
	const std::string Family = Of(Families, 40, Roll), Place = Of(Places, 28, R2);
	if (Kind == "shop")
	{
		const std::string Shop = Of(Shops, 22, Roll2);
		if (R2 < 0.55) return Family + " " + Shop;
		if (R2 < 0.8) return Family + "'s " + Shop;
		return Of(Places, 28, Roll) + " " + Shop;
	}
	if (Kind == "store")
	{
		if (Roll2 < 0.35) return Family + "'s Foodstore";
		if (Roll2 < 0.6) return Family + "'s Supermarket";
		if (Roll2 < 0.8) return Family + " Fresh Market";
		return Place + " Foodhall";
	}
	if (Kind == "trade") return Family + " " + Of(Trades, 14, Roll2);
	if (Kind == "firm")
	{
		const std::string Firm = Of(Firms, 10, Roll2);
		return R2 < 0.6 ? Family + " " + Firm : Place + " " + Firm;
	}
	if (Kind == "retailpark") return Place + (Roll2 < 0.6 ? " Retail Park" : " Shopping Park");
	return Place + " " + Of(Estates, 5, Roll2);
}

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
	const bool Instance = WarehouseInstancingOn.load() && WarehouseKitNow().get() == &Kit;   // [round 175 fps]
	const auto Lay = [&](int Piece, double Ox, double Oz, double Ax, double Az, double Mx, double Mz, double Sx, double Sz2, double Pivot, double SAbove)
	{
		if (Piece >= 0 && size_t(Piece) < Kit.Pieces.size()) Laid += PlacePiece(Kit, Piece, Ox, Oz, Ax, Az, Mx, Mz, Sx, Sz2, Y, Pivot, SAbove, Need, Instance, Into);
	};
	// The four walls: each its start, its way along and its way out (the front's left end, seen from outside, first).
	struct WallRun { double X, Z, Ax, Az, Nx, Nz, Len; bool IsLong; const std::string* Pattern; };
	std::vector<SignSpot> Spots;
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
			std::string Key = Kind + Letters[size_t(I)];
			int Piece = D.Piece(Key);
			if (Piece < 0) { Key = Kind + "."; Piece = D.Piece(Key); }
			if (Piece < 0) { Key = "long:."; Piece = D.Piece(Key); }
			const double Ox = Wr.X + Wr.Ax * Bw * I, Oz = Wr.Z + Wr.Az * Bw * I;
			Lay(Piece, Ox, Oz, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, Bw / Bay, 1.0, 1e9, 1.0);
			// [stage 3] (its sign boards' faces, for their names; near only)
			const auto Sg = D.Signs.find(Key);
			if (Detail >= 2 && Sg != D.Signs.end())
				for (const WarehouseSign& S : Sg->second)
				{
					const double Sx = Bw / Bay;
					Spots.push_back({Ox + S.X * Sx * Wr.Ax + S.Z * Wr.Nx, Y + S.Y, Oz + S.X * Sx * Wr.Az + S.Z * Wr.Nz,
						S.Fx * Wr.Ax + S.Fz * Wr.Nx, S.Fx * Wr.Az + S.Fz * Wr.Nz, S.W * Sx, S.H, &S.Kind});
				}
		}
		// (its corner flashings, at both ends)
		const int Corner = D.Piece("corner");
		Lay(Corner, Wr.X, Wr.Z, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, 1.0, 1.0, 1e9, 1.0);
		Lay(Corner, Wr.X + Wr.Ax * Wr.Len, Wr.Z + Wr.Az * Wr.Len, -Wr.Ax, -Wr.Az, Wr.Nx, Wr.Nz, 1.0, 1.0, 1e9, 1.0);
		// (the gable above the eaves, right across)
		if (!Wr.IsLong) Lay(D.Piece("gabletop"), Wr.X, Wr.Z, Wr.Ax, Wr.Az, Wr.Nx, Wr.Nz, Short / D.W, 1.0, D.GableTop, Rise);
	}
	// [stage 3] Its signs' made-up names: the store's or the firm's its own, each shop and trade counter its own.
	{
		static const float Ink[3] = {0.025f, 0.028f, 0.032f};
		for (const SignSpot& S : Spots)
		{
			const bool Own = *S.Kind == "store" || *S.Kind == "firm";
			const double Ra = Own ? Chance(CornerX, CornerZ, 153) : Chance(S.X, S.Z, 151), Rb = Own ? Chance(CornerX, CornerZ, 154) : Chance(S.X, S.Z, 152);
			WriteSign(*Into[0], S.X, S.Y, S.Z, S.Fx, S.Fz, S.W, S.H, WarehouseName(*S.Kind, Ra, Rb), Ink);
		}
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

namespace
{
// [stage 3] A prop's footprint on the ground: its middle, its long way (unit) and its half length and depth.
struct Footprint { double X, Z, Ax, Az, Hl, Hd; };
bool Overlap(const Footprint& A, const Footprint& B)
{
	// (two turned rectangles: apart if any of their four axes separates them)
	const double Dx = B.X - A.X, Dz = B.Z - A.Z;
	const double Axes[4][2] = {{A.Ax, A.Az}, {-A.Az, A.Ax}, {B.Ax, B.Az}, {-B.Az, B.Ax}};
	for (const auto& Ax : Axes)
	{
		const auto Reach = [&](const Footprint& F) {
			return F.Hl * std::fabs(F.Ax * Ax[0] + F.Az * Ax[1]) + F.Hd * std::fabs(-F.Az * Ax[0] + F.Ax * Ax[1]);
		};
		if (std::fabs(Dx * Ax[0] + Dz * Ax[1]) > Reach(A) + Reach(B)) return false;
	}
	return true;
}
} // namespace

size_t DressYard(const WarehouseKit& Kit, const WarehouseDesign& D, double CornerX, double CornerZ, double Ux, double Uz, double Nx, double Nz,
	double Long, double Short, int Detail, const std::function<bool(double, double)>& Clear, const std::function<double(double, double)>& Ground,
	const std::function<double(double, double)>& RoadDist, long long& Budget, RideMesh& Out)
{
	using namespace RideSceneBits;
	if (!Kit.Yards || Detail < 1 || Kit.Props.empty() || Budget <= 0) return 0;
	const int Need = Detail >= 2 ? 0 : 1;
	size_t Laid = 0;
	const bool Instance = WarehouseInstancingOn.load() && WarehouseKitNow().get() == &Kit;   // [round 175 fps]
	std::vector<Footprint> Taken;
	// A prop by name, centred at (X, Z), its length along (Ax, Az), its front towards (Fx, Fz); none where its
	// footprint isn't clear ground, overlaps one laid, or the budget is spent.
	const auto Put = [&](const std::string& Name, double X, double Z, double Ax, double Az, double Fx, double Fz, double Pad = 0.3) -> bool
	{
		const int Ix = Kit.Prop(Name);
		if (Ix < 0) return false;
		const WarehouseProp& P = Kit.Props[size_t(Ix)];
		const WarehousePiece& Pc = Kit.Pieces[size_t(P.Piece)];
		if (Budget < (long long)(Pc.Idx.size() / 3)) return false;
		const Footprint F{X, Z, Ax, Az, P.L * 0.5 + Pad, std::fmax(0.05, P.D * 0.5 + Pad)};
		for (const double Su : {-1.0, 0.0, 1.0})
			for (const double Sd : {-1.0, 1.0})
				if (!Clear(X + Ax * Su * F.Hl - Az * Sd * F.Hd, Z + Az * Su * F.Hl + Ax * Sd * F.Hd)) return false;
		for (const Footprint& T : Taken)
			if (Overlap(F, T)) return false;
		const double Y = Ground(X, Z);
		if (std::isnan(Y)) return false;
		// (its local z, its front, is the way (Fx, Fz) goes: LayPiece's "out of the wall")
		RideMesh* const Into[5] = {&Out, &Out, &Out.RoofingOf(), &Out.BrickworkOf(), &Out.RenderOf()};
		const size_t N = PlacePiece(Kit, P.Piece, X, Z, Ax, Az, Fx, Fz, 1.0, 1.0, Y, 1e9, 1.0, Need, Instance, Into);
		Budget -= (long long)N;
		Laid += N;
		Taken.push_back(F);
		if (Detail >= 2)
			for (const WarehouseSign& S : P.Signs)
			{
				static const float Paper[3] = {0.78f, 0.78f, 0.76f};
				const double Wx = X + S.X * Ax + S.Z * Fx, Wz = Z + S.X * Az + S.Z * Fz;
				const int Cx = int(std::floor(X / 600.0)), Cz = int(std::floor(Z / 600.0));
				WriteSign(Out, Wx, Y + S.Y, Wz, Fx, Fz, S.W, S.H,
					WarehouseName(D.Retail() ? "retailpark" : "estate", Chance(Cx * 600.0, Cz * 600.0, 181), Chance(Cx * 600.0, Cz * 600.0, 182)), Paper);
			}
		return true;
	};
	struct WallRun { double X, Z, Ax, Az, Nx, Nz, Len; bool IsLong; const std::string* Pattern; };
	const WallRun Walls[4] = {
		{CornerX, CornerZ, Ux, Uz, Nx, Nz, Long, true, &D.Front},
		{CornerX + Ux * Long - Nx * Short, CornerZ + Uz * Long - Nz * Short, -Ux, -Uz, -Nx, -Nz, Long, true, &D.Back},
		{CornerX + Ux * Long, CornerZ + Uz * Long, -Nx, -Nz, Ux, Uz, Short, false, &D.Right},
		{CornerX - Nx * Short, CornerZ - Nz * Short, Nx, Nz, -Ux, -Uz, Short, false, &D.Left}};
	const auto At = [](const WallRun& W, double S, double V, double& X, double& Z) { X = W.X + W.Ax * S + W.Nx * V; Z = W.Z + W.Az * S + W.Nz * V; };
	// Each wall's doors as laid (the same bays as BuildWarehouse's): trailers backed up to the docks, a van by a
	// roller door now and then; the doors kept for what stands beside them.
	std::vector<std::pair<int, double>> Rollers, Doors;   // (wall, along)
	bool Docks = false;
	double ApronFrom[4] = {1e9, 1e9, 1e9, 1e9}, ApronTo[4] = {-1e9, -1e9, -1e9, -1e9}, ApronDeep[4] = {0.0, 0.0, 0.0, 0.0};
	for (int K = 0; K < 4; ++K)
	{
		const WallRun& W = Walls[K];
		const double Bay = W.IsLong ? D.Bay : D.GableBay;
		const int N = D.Roof == "multi" && !W.IsLong ? 2 * D.Spans : std::max(1, int(std::lround(W.Len / Bay)));
		const double Bw = W.Len / N;
		const std::string Letters = ExpandBays(*W.Pattern, N);
		for (int I = 0; I < N; ++I)
		{
			const double S = Bw * (I + 0.5);
			double X, Z;
			const char Letter = Letters[size_t(I)];
			if (Letter == 'D' || Letter == 'R' || Letter == 'H')
			{
				ApronFrom[K] = std::fmin(ApronFrom[K], S - Bw * 0.5 - 3.0);
				ApronTo[K] = std::fmax(ApronTo[K], S + Bw * 0.5 + 3.0);
				ApronDeep[K] = std::fmax(ApronDeep[K], Letter == 'D' ? 20.0 : 12.0);
			}
			switch (Letter)
			{
			case 'D':
			{
				Docks = true;
				At(W, S, 0.95 + 6.82, X, Z);
				if (Chance(X, Z, 171) < (K == 0 ? 0.6 : 0.45))
				{
					const double Pick = Chance(X, Z, 172);
					const char* Trailer = Pick < 0.35 ? "trailer_white" : Pick < 0.6 ? "trailer_navy" : Pick < 0.8 ? "trailer_grey" : "trailer_fridge";
					Put(Trailer, X, Z, -W.Nx, -W.Nz, W.Ax, W.Az);   // (its rear, +x, to the dock)
				}
				break;
			}
			case 'R':
			case 'H':
				Rollers.push_back({K, S});
				At(W, S + Bw * 0.5 - 0.3, 4.2, X, Z);
				if (Chance(X, Z, 173) < 0.35)
				{
					const double Pick = Chance(X, Z, 174);
					Put(Pick < 0.5 ? "van_white" : Pick < 0.8 ? "van_silver" : "van_blue", X, Z, W.Ax, W.Az, W.Nx, W.Nz);
				}
				break;
			case 'P':
				Doors.push_back({K, S});
				break;
			default: break;
			}
		}
	}
	// A concrete apron before the loading doors, cell by cell where the ground is clear (concrete under the
	// trailers and vans, not the grass), a little over the ground so it never sinks in.
	for (int K = 0; K < 4; ++K)
	{
		if (ApronDeep[K] <= 0.0 || Budget < 400) continue;
		const WallRun& W = Walls[K];
		const double From = std::fmax(-2.0, ApronFrom[K]), To = std::fmin(W.Len + 2.0, ApronTo[K]);
		const int Nu = std::max(1, int(std::ceil((To - From) / 8.0))), Nv = std::max(1, int(std::ceil(ApronDeep[K] / 5.0)));
		static const Rgb Concrete = {0.34f, 0.335f, 0.32f};
		const auto Corner = [&](int Iu, int Iv) {
			const double S = From + (To - From) * Iu / Nu, V = 0.15 + (ApronDeep[K] - 0.15) * Iv / Nv;
			const double Px = W.X + W.Ax * S + W.Nx * V, Pz = W.Z + W.Az * S + W.Nz * V;
			// (over the highest of the ground round the corner, so the land never shows through between)
			double Gy = Ground(Px, Pz);
			for (const double Ox : {-2.0, 2.0})
				for (const double Oz : {-2.0, 2.0}) Gy = std::fmax(Gy, Ground(Px + Ox, Pz + Oz));
			return P3{Px, Gy + 0.1, Pz};
		};
		for (int Iv = 0; Iv < Nv; ++Iv)
		{
			bool Stop = true;
			for (int Iu = 0; Iu < Nu; ++Iu)
			{
				const P3 A = Corner(Iu, Iv), B = Corner(Iu + 1, Iv), C = Corner(Iu + 1, Iv + 1), E = Corner(Iu, Iv + 1);
				if (std::isnan(A.Y + B.Y + C.Y + E.Y) || !Clear(A.X, A.Z) || !Clear(B.X, B.Z) || !Clear(C.X, C.Z) || !Clear(E.X, E.Z)) continue;
				Quad(Out.RenderOf(), A, B, C, E, P3{A.X, A.Y + 100.0, A.Z}, Concrete, false, 0.08f);
				Laid += 2;
				Budget -= 2;
				Stop = false;
			}
			if (Stop) break;   // (a row with no room: the yard ends there)
		}
	}
	const double Seed = Chance(CornerX, CornerZ, 161);
	double X = 0.0, Z = 0.0;
	if (D.Retail())
	{
		// A supermarket's trolley shelters out in its car park, either side of its lobby.
		const size_t M = D.Front.find('M');
		if (M != std::string::npos)
		{
			const WallRun& W = Walls[0];
			const int N = std::max(1, int(std::lround(W.Len / D.Bay)));
			const std::string Letters = ExpandBays(D.Front, N);
			const size_t Lobby = Letters.find('M');
			const double S = W.Len / N * (double(Lobby) + 0.5);
			static const double Shelters[4][2] = {{-15.0, 24.0}, {16.0, 24.0}, {30.0, 40.0}, {-28.0, 40.0}};
			for (const auto& Spot : Shelters)
			{
				At(W, S + Spot[0], Spot[1], X, Z);
				Put("trolley_shelter", X, Z, W.Ax, W.Az, W.Nx, W.Nz);
			}
		}
	}
	else
	{
		// An industrial shed's yard before it: how deep the clear ground runs (to a road, a building, a railway);
		// with room, a palisade fence across it and down its sides, a sliding gate where the road is nearest,
		// the estate's name board by the gate and lamps in the yard.
		const WallRun& F = Walls[0];
		double Yard = 46.0;
		for (double S = 0.0; S <= Long + 0.01; S += std::fmax(5.0, Long / 12.0))
			for (double V = 4.0; V < Yard; V += 2.0)
			{
				At(F, S, V, X, Z);
				if (!Clear(X, Z)) { Yard = V; break; }
			}
		const bool Fenced = (Docks || !Rollers.empty()) && Yard >= (Docks ? 22.0 : 16.0) && Seed < 0.7;
		if (Fenced)
		{
			const double Fv = Yard - 3.0;
			const bool Green = Chance(CornerX, CornerZ, 162) < 0.7;
			const std::string Panel = Green ? "palisade_green" : "palisade_grey", Slide = Green ? "gate_green" : "gate_grey";
			// (the gate: the front run's point nearest a road)
			double GateS = Long * 0.5, Best = 1e18;
			for (double S = 6.0; S <= Long - 6.0; S += 3.0)
			{
				At(F, S, Fv + 4.0, X, Z);
				const double Rd = RoadDist(X, Z);
				if (Rd < Best) { Best = Rd; GateS = S; }
			}
			At(F, GateS, Fv, X, Z);
			const bool Gate = Put(Slide, X, Z, F.Ax, F.Az, F.Nx, F.Nz);
			if (Gate && Chance(X, Z, 163) < 0.65)
			{
				At(F, GateS + 6.5, Fv + 2.0, X, Z);
				Put("estate_board", X, Z, F.Ax, F.Az, F.Nx, F.Nz);
			}
			const double Run = 2.75;
			// (the front run, then each side's back to the shed's corners)
			const double From = -4.0, To = Long + 4.0;
			const int Nf = std::max(1, int(std::ceil((To - From) / Run)));
			for (int I = 0; I < Nf; ++I)
			{
				const double S = From + (I + 0.5) * (To - From) / Nf;
				if (Gate && std::fabs(S - GateS) < 3.0 + Run * 0.5) continue;
				At(F, S, Fv, X, Z);
				Put(Panel, X, Z, F.Ax, F.Az, F.Nx, F.Nz, -0.06);
			}
			for (const double S : {From, To})
			{
				const int Ns = std::max(1, int(std::ceil(Fv / Run)));
				for (int I = 0; I < Ns; ++I)
				{
					const double V = (I + 0.5) * Fv / Ns;
					At(F, S, V, X, Z);
					const double Side = S < 0.0 ? -1.0 : 1.0;   // (facing out of the yard)
					Put(Panel, X, Z, F.Nx, F.Nz, F.Ax * Side, F.Az * Side, -0.06);
				}
			}
			for (double S = 12.0; S < Long - 6.0; S += 36.0)
			{
				At(F, S, Fv - 1.5, X, Z);
				Put("yard_lamp", X, Z, F.Ax, F.Az, -F.Nx, -F.Nz);   // (its head over the yard)
			}
		}
		// Pallets by a roller door; a container down a gable; a gas cage by a side door.
		if (!Rollers.empty() && Chance(CornerX, CornerZ, 164) < 0.6)
		{
			const auto& R = Rollers[size_t(Chance(CornerX, CornerZ, 165) * double(Rollers.size())) % Rollers.size()];
			const WallRun& W = Walls[R.first];
			At(W, R.second + 5.0, 1.3, X, Z);
			if (!Put("pallets", X, Z, W.Ax, W.Az, W.Nx, W.Nz))
			{
				At(W, R.second - 5.0, 1.3, X, Z);
				Put("pallets", X, Z, W.Ax, W.Az, W.Nx, W.Nz);
			}
		}
		if (Chance(CornerX, CornerZ, 166) < 0.4)
		{
			const WallRun& W = Walls[2];
			const double Pick = Chance(CornerX, CornerZ, 167);
			const char* Box = Pick < 0.4 ? "container_red" : Pick < 0.7 ? "container_blue" : "container_green";
			for (const double S : {W.Len * 0.3, W.Len * 0.7})
			{
				bool ByDoor = false;
				for (const auto& R : Rollers) ByDoor = ByDoor || (R.first == 2 && std::fabs(R.second - S) < 7.0);
				for (const auto& R : Doors) ByDoor = ByDoor || (R.first == 2 && std::fabs(R.second - S) < 5.0);
				At(W, S, 2.2, X, Z);
				if (!ByDoor && Put(Box, X, Z, W.Ax, W.Az, W.Nx, W.Nz)) break;
			}
		}
		for (const auto& P : Doors)
			if (P.first >= 2 && Chance(CornerX + P.second, CornerZ, 168) < 0.35)
			{
				At(Walls[P.first], P.second + 2.4, 0.95, X, Z);
				if (Put("gascage", X, Z, Walls[P.first].Ax, Walls[P.first].Az, Walls[P.first].Nx, Walls[P.first].Nz)) break;
			}
	}
	// A skip by the back corner, and big bins by a back or side door (shops and sheds alike).
	if (Seed > 0.25)
	{
		const WallRun& W = Walls[1];
		At(W, 6.0, 3.2, X, Z);
		const double Pick = Chance(CornerX, CornerZ, 169);
		Put(Pick < 0.6 ? "skip_yellow" : "skip_green", X, Z, W.Ax, W.Az, W.Nx, W.Nz);
	}
	for (const auto& P : Doors)
		if (P.first >= 1)
		{
			At(Walls[P.first], P.second - 2.6, 0.9, X, Z);
			if (Put("bins", X, Z, Walls[P.first].Ax, Walls[P.first].Az, Walls[P.first].Nx, Walls[P.first].Nz)) break;
		}
	return Laid;
}
void SetWarehouseInstancing(bool On)
{
	WarehouseInstancingOn.store(On);
}

bool WarehouseInstancing()
{
	return WarehouseInstancingOn.load();
}

size_t WarehousePieceMesh(const WarehouseKit& Kit, int Piece, bool Near, RideMesh& Out)
{
	if (Piece < 0 || size_t(Piece) >= Kit.Pieces.size()) return 0;
	RideMesh* const Into[5] = {&Out, &Out, &Out.RoofingOf(), &Out.BrickworkOf(), &Out.RenderOf()};
	return LayPiece(Kit.Pieces[size_t(Piece)], Out.OriginX, Out.OriginZ, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1e9, 1.0, Near ? 0 : 1, Into);
}
} // namespace TorqueWorld
