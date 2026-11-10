// The bike fitting trial (see README.md here). A program of its own: it reads the game's parts and
// the game's own fitting code, read only, and writes pictures and reports. It changes nothing.
#include "BikeGeometry.h"
#include "BikeLayout.h"
#include "BikeParts.h"
#include "PartMesh.h"
#include "PartsDb.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace TorqueFit;

static bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(Path, std::ios::binary);
	if (!F) return false;
	std::ostringstream S;
	S << F.rdbuf();
	Out = S.str();
	return true;
}

static double Now()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Rgb { float R = 0.5f, G = 0.5f, B = 0.5f; };

// One slot's drawn triangles (ours: x forward, y up, z right).
struct Tri { V3 A, B, C; Rgb Col; float Metal = 0.0f; };
struct SlotMesh
{
	std::string Slot;
	std::vector<Tri> Tris;
	V3 Lo{1e9, 1e9, 1e9}, Hi{-1e9, -1e9, -1e9};
};
struct Bike
{
	PartsMap Parts;
	Layout Lay;
	std::vector<SlotMesh> Slots;
	SlotMesh* Of(const std::string& Slot)
	{
		for (SlotMesh& S : Slots) if (S.Slot == Slot) return &S;
		Slots.push_back(SlotMesh{Slot, {}, V3(1e9, 1e9, 1e9), V3(-1e9, -1e9, -1e9)});
		return &Slots.back();
	}
};

struct World
{
	std::string Root;
	PartsDb Db;
	BikeLayout Lay;
	Json Index;
	const Json* Known = nullptr;
	std::unique_ptr<BikeParts> Placer;
	std::map<std::string, std::shared_ptr<PartMesh>> Cache;
	std::map<std::string, std::pair<Rgb, float>> TagLook;   // "glb|tag" -> colour, metallic

	bool Load(const std::string& InRoot, std::string& Error)
	{
		Root = InRoot;
		std::string PartsText, LayoutText, IndexText;
		if (!ReadFile(Root + "/Data/Parts/parts.json", PartsText) || !ReadFile(Root + "/Data/Fitment/layout.json", LayoutText)
			|| !ReadFile(Root + "/Data/Parts/Blender/index.json", IndexText)) { Error = "the game's data isn't under " + Root; return false; }
		if (!Db.LoadFromText(PartsText, Error) || !Lay.Load(LayoutText, Db, Error) || !Json::Parse(IndexText, Index, &Error)) return false;
		Known = Index.Get("parts");
		Placer = std::make_unique<BikeParts>(Lay, *Known);
		Placer->SetMeshSource([this](const std::string& Path, PartMesh& M) { const auto P = Glb(Path); if (!P) return false; M = *P; return true; });
		return true;
	}
	std::shared_ptr<PartMesh> Glb(const std::string& Path)
	{
		auto It = Cache.find(Path);
		if (It != Cache.end()) return It->second;
		std::shared_ptr<PartMesh> M = std::make_shared<PartMesh>();
		std::string Bytes, MorphBytes, Err;
		const Json* Ent = Known->Get(Path.substr(0, Path.size() - 4));
		bool Ok = Ent && ReadFile(Root + "/Data/Parts/Blender/" + Path, Bytes) && M->LoadGlb(Bytes, Err);
		if (Ok && Ent->Get("morph")) Ok = ReadFile(Root + "/Data/Parts/Blender/" + Ent->Str("morph"), MorphBytes) && M->LoadMorph(MorphBytes, Ent->Get("shape_keys")->Dump(0), Err);
		if (!Ok) M.reset();
		if (Ok)
			if (const Json* Surf = Ent->Get("surfaces"))
				for (const Json& S : Surf->A)
				{
					Rgb C;
					if (const Json* Col = S.Get("color")) if (Col->A.size() >= 3) C = Rgb{float(Col->A[0].N), float(Col->A[1].N), float(Col->A[2].N)};
					TagLook[Path + "|" + S.Str("tag")] = {C, float(S.Num("metallic"))};
				}
		Cache[Path] = M;
		return M;
	}
	// The part's own paint (parts.json's "color"), if it has one.
	bool PaintOf(const PartsMap& Parts, const std::string& Slot, Rgb& Out) const
	{
		auto It = Parts.find(Slot);
		if (It == Parts.end()) return false;
		const Json* P = Lay.Part(It->second);
		const Json* Col = P ? P->Get("color") : nullptr;
		if (!Col || Col->A.size() < 3) return false;
		Out = Rgb{float(Col->A[0].N), float(Col->A[1].N), float(Col->A[2].N)};
		return true;
	}

	Bike Build(const PartsMap& Parts, const BikeGeo& Fit = BikeGeo())
	{
		Bike B;
		B.Parts = Parts;
		B.Lay = Lay.Solve(Parts, Fit.Rig);
		const std::vector<FittedPart> Placed = Placer->Fit(Parts, B.Lay);
		std::vector<PartMesh> Drawn(Placed.size());
		GeoPivots Pv;
		for (size_t I = 0; I < Placed.size(); ++I)
		{
			if (Placed[I].Hidden) continue;
			if (Placed[I].Made) Drawn[I] = *Placed[I].Made;
			else
			{
				const std::shared_ptr<PartMesh> M = Glb(Placed[I].Glb);
				if (!M) continue;
				Drawn[I] = BikeParts::Draw(*M, Placed[I]);
			}
			Pv.Add(Placed[I], Drawn[I]);
		}
		for (size_t I = 0; I < Placed.size(); ++I)
		{
			const FittedPart& F = Placed[I];
			if (F.Hidden) continue;
			if (!Fit.Slots.empty()) Pv.Bake(Fit, F, Drawn[I]);
			for (const PartSurface& S : Drawn[I].Surfaces)
			{
				std::string Tag = S.Material;
				const size_t Cut = Tag.find("__");
				if (Cut != std::string::npos) Tag = Tag.substr(0, Cut);
				if (std::find(F.HiddenTags.begin(), F.HiddenTags.end(), Tag) != F.HiddenTags.end()) continue;
				const std::string Slot = GeoPivots::SlotOf(F, S);
				Rgb Col;
				float Metal = 0.0f;
				auto Look = TagLook.find(F.Glb + "|" + Tag);
				if (Look != TagLook.end()) { Col = Look->second.first; Metal = Look->second.second; }
				if (Tag == "paint" || Tag == "paint_2") { Rgb P; if (PaintOf(Parts, Slot, P)) Col = Tag == "paint" ? P : Rgb{P.R * 0.6f, P.G * 0.6f, P.B * 0.6f}; }
				SlotMesh* Sm = B.Of(Slot);
				for (size_t T = 0; T + 2 < S.Idx.size(); T += 3)
				{
					Tri Tr{S.Pos[S.Idx[T]], S.Pos[S.Idx[T + 1]], S.Pos[S.Idx[T + 2]], Col, Metal};
					for (const V3* P : {&Tr.A, &Tr.B, &Tr.C})
					{
						Sm->Lo = V3(std::min(Sm->Lo.x, P->x), std::min(Sm->Lo.y, P->y), std::min(Sm->Lo.z, P->z));
						Sm->Hi = V3(std::max(Sm->Hi.x, P->x), std::max(Sm->Hi.y, P->y), std::max(Sm->Hi.z, P->z));
					}
					Sm->Tris.push_back(Tr);
				}
			}
		}
		return B;
	}
};

// --- Pictures --------------------------------------------------------------------------------

struct Image
{
	int W = 0, H = 0;
	std::vector<unsigned char> Px;
	Image(int InW, int InH) : W(InW), H(InH), Px(size_t(InW) * InH * 3, 255) {}
	bool Save(const std::string& Path) const
	{
		std::ofstream F(Path, std::ios::binary);
		if (!F) return false;
		F << "P6\n" << W << " " << H << "\n255\n";
		F.write(reinterpret_cast<const char*>(Px.data()), std::streamsize(Px.size()));
		return bool(F);
	}
};

// A view: the bike seen along Fwd with Right and Up across the picture, Scale pixels a metre.
struct View
{
	V3 Right{1, 0, 0}, Up{0, 1, 0}, Fwd{0, 0, -1};
	V3 Centre{0, 0.25, 0};
	double Scale = 300.0;
	static View Side() { return View(); }
	static View Quarter()
	{
		View V;
		const double Yaw = 0.6, Pitch = 0.22;
		V.Fwd = V3(std::sin(Yaw) * std::cos(Pitch), -std::sin(Pitch), -std::cos(Yaw) * std::cos(Pitch)).Normalized();
		V.Right = V3(0, 1, 0).Cross(V.Fwd * -1.0).Normalized();
		V.Up = (V.Fwd * -1.0).Cross(V.Right).Normalized();
		return V;
	}
	static View Top()
	{
		View V;
		V.Fwd = V3(0, -1, 0);
		V.Right = V3(1, 0, 0);
		V.Up = V3(0, 0, -1);
		V.Centre = V3(0, 0.3, 0);
		return V;
	}
};

// Draws the bike into a W by H picture (Ss times oversampled), each slot tinted by Tint if it has one.
static Image Render(const Bike& B, const View& V, int W, int H, const std::map<std::string, Rgb>& Tint = {}, const std::vector<V3>& Marks = {}, int Ss = 2)
{
	const int BW = W * Ss, BH = H * Ss;
	std::vector<float> Depth(size_t(BW) * BH, 1e9f);
	std::vector<float> Buf(size_t(BW) * BH * 3);
	// The backdrop: a soft grey, lighter at the top, and the ground.
	for (int Y = 0; Y < BH; ++Y)
		for (int X = 0; X < BW; ++X)
		{
			const float T = float(Y) / float(BH);
			float* P = &Buf[(size_t(Y) * BW + X) * 3];
			P[0] = P[1] = P[2] = 0.93f - 0.10f * T;
		}
	const double Sc = V.Scale * Ss;
	const V3 Light = (V.Right * -0.35 + V.Up * 0.75 - V.Fwd * 0.55).Normalized();
	const V3 Fill = (V.Right * 0.6 + V.Up * 0.1 - V.Fwd * 0.5).Normalized();
	for (const SlotMesh& S : B.Slots)
	{
		const auto TintIt = Tint.find(S.Slot);
		for (const Tri& T : S.Tris)
		{
			const V3 P[3] = {T.A - V.Centre, T.B - V.Centre, T.C - V.Centre};
			double X[3], Y[3], Z[3];
			for (int I = 0; I < 3; ++I)
			{
				X[I] = BW * 0.5 + P[I].Dot(V.Right) * Sc;
				Y[I] = BH * 0.5 - P[I].Dot(V.Up) * Sc;
				Z[I] = P[I].Dot(V.Fwd);
			}
			const double Area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
			if (std::fabs(Area) < 1e-9) continue;
			V3 N = (T.B - T.A).Cross(T.C - T.A).Normalized();
			if (N.Dot(V.Fwd) > 0.0) N = N * -1.0;   // (two-sided: many shells are open)
			const double Dif = std::max(0.0, N.Dot(Light)), Dif2 = std::max(0.0, N.Dot(Fill));
			const V3 Half = (Light - V.Fwd).Normalized();
			const double Spec = std::pow(std::max(0.0, N.Dot(Half)), 24.0) * (0.25 + 0.6 * T.Metal);
			const double Shade = 0.30 + 0.62 * Dif + 0.18 * Dif2;
			Rgb C = T.Col;
			if (TintIt != Tint.end()) C = Rgb{C.R * 0.25f + TintIt->second.R * 0.75f, C.G * 0.25f + TintIt->second.G * 0.75f, C.B * 0.25f + TintIt->second.B * 0.75f};
			const float Out[3] = {float(std::min(1.0, C.R * Shade + Spec)), float(std::min(1.0, C.G * Shade + Spec)), float(std::min(1.0, C.B * Shade + Spec))};
			const int X0 = std::max(0, int(std::floor(std::min({X[0], X[1], X[2]})))), X1 = std::min(BW - 1, int(std::ceil(std::max({X[0], X[1], X[2]}))));
			const int Y0 = std::max(0, int(std::floor(std::min({Y[0], Y[1], Y[2]})))), Y1 = std::min(BH - 1, int(std::ceil(std::max({Y[0], Y[1], Y[2]}))));
			for (int Py = Y0; Py <= Y1; ++Py)
				for (int Px = X0; Px <= X1; ++Px)
				{
					const double Cx = Px + 0.5, Cy = Py + 0.5;
					const double W0 = ((X[1] - Cx) * (Y[2] - Cy) - (X[2] - Cx) * (Y[1] - Cy)) / Area;
					const double W1 = ((X[2] - Cx) * (Y[0] - Cy) - (X[0] - Cx) * (Y[2] - Cy)) / Area;
					const double W2 = 1.0 - W0 - W1;
					if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0) continue;
					const float D = float(W0 * Z[0] + W1 * Z[1] + W2 * Z[2]);
					const size_t At = size_t(Py) * BW + Px;
					if (D >= Depth[At]) continue;
					Depth[At] = D;
					Buf[At * 3] = Out[0]; Buf[At * 3 + 1] = Out[1]; Buf[At * 3 + 2] = Out[2];
				}
		}
	}
	// Where parts pass through each other: red, on top of everything.
	for (const V3& M : Marks)
	{
		const V3 P = M - V.Centre;
		const int Cx = int(BW * 0.5 + P.Dot(V.Right) * Sc), Cy = int(BH * 0.5 - P.Dot(V.Up) * Sc), Rad = std::max(1, int(0.006 * Sc));
		for (int Dy = -Rad; Dy <= Rad; ++Dy)
			for (int Dx = -Rad; Dx <= Rad; ++Dx)
			{
				const int X = Cx + Dx, Y = Cy + Dy;
				if (X < 0 || Y < 0 || X >= BW || Y >= BH || Dx * Dx + Dy * Dy > Rad * Rad) continue;
				float* Px = &Buf[(size_t(Y) * BW + X) * 3];
				Px[0] = Px[0] * 0.35f + 0.65f * 0.95f; Px[1] *= 0.35f; Px[2] *= 0.35f;
			}
	}
	Image Img(W, H);
	for (int Y = 0; Y < H; ++Y)
		for (int X = 0; X < W; ++X)
			for (int C = 0; C < 3; ++C)
			{
				float Sum = 0.0f;
				for (int Dy = 0; Dy < Ss; ++Dy)
					for (int Dx = 0; Dx < Ss; ++Dx) Sum += Buf[(size_t(Y * Ss + Dy) * BW + X * Ss + Dx) * 3 + C];
				// (a gentle curve, so dark parts keep their shape)
				const float Val = std::pow(Sum / float(Ss * Ss), 1.0f / 1.8f);
				Img.Px[(size_t(Y) * W + X) * 3 + C] = static_cast<unsigned char>(std::min(255.0f, Val * 255.0f + 0.5f));
			}
	return Img;
}


// --- The fit checker ---------------------------------------------------------------------------
// Each slot's parts are measured two ways. As solids: along every 8 mm column across the bike (z),
// where the column is inside the part; two slots' solids overlapping is one passing through the
// other, measured in cubic centimetres. As surfaces: which 12 mm cells its skin touches; the
// nearest another slot's skin comes to it is its gap, and a part with a big gap is floating.

namespace Grid
{
constexpr double H = 0.008, Ox = -1.7, Oy = -0.6;
constexpr int Nx = 425, Ny = 280;
constexpr double Hv = 0.012, Oz = -0.9;
constexpr int Vx = 284, Vy = 187, Vz = 150;
inline int VoxOf(const V3& P)
{
	const int X = int(std::floor((P.x - Ox) / Hv)), Y = int(std::floor((P.y - Oy) / Hv)), Z = int(std::floor((P.z - Oz) / Hv));
	if (X < 0 || Y < 0 || Z < 0 || X >= Vx || Y >= Vy || Z >= Vz) return -1;
	return (Z * Vy + Y) * Vx + X;
}
}

struct Span { int Col; float Z0, Z1; };

struct Solid
{
	std::vector<Span> Spans;   // sorted by column, then z
	std::vector<int> Vox;      // sorted, unique
	double Volume() const { double V = 0.0; for (const Span& S : Spans) V += double(S.Z1 - S.Z0); return V * Grid::H * Grid::H * 1e6; }
};

static Solid Solidify(const std::vector<Tri>& Tris)
{
	struct Hit { int Col; float Z; int Sign; };
	std::vector<Hit> Hits;
	Solid Out;
	for (const Tri& T : Tris)
	{
		const double Ax = (T.A.x - Grid::Ox) / Grid::H, Ay = (T.A.y - Grid::Oy) / Grid::H, Bx = (T.B.x - Grid::Ox) / Grid::H, By = (T.B.y - Grid::Oy) / Grid::H,
			Cx = (T.C.x - Grid::Ox) / Grid::H, Cy = (T.C.y - Grid::Oy) / Grid::H;
		const double Area = (Bx - Ax) * (Cy - Ay) - (Cx - Ax) * (By - Ay);
		if (std::fabs(Area) > 1e-9)
		{
			const int X0 = std::max(0, int(std::floor(std::min({Ax, Bx, Cx}) - 0.5))), X1 = std::min(Grid::Nx - 1, int(std::ceil(std::max({Ax, Bx, Cx}))));
			const int Y0 = std::max(0, int(std::floor(std::min({Ay, By, Cy}) - 0.5))), Y1 = std::min(Grid::Ny - 1, int(std::ceil(std::max({Ay, By, Cy}))));
			for (int Y = Y0; Y <= Y1; ++Y)
				for (int X = X0; X <= X1; ++X)
				{
					// (a hair off the middle, so a column never runs exactly down a shared edge)
					const double Px = X + 0.5 + 1.7e-5, Py = Y + 0.5 + 3.1e-5;
					const double W0 = ((Bx - Px) * (Cy - Py) - (Cx - Px) * (By - Py)) / Area;
					const double W1 = ((Cx - Px) * (Ay - Py) - (Ax - Px) * (Cy - Py)) / Area;
					const double W2 = 1.0 - W0 - W1;
					if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0) continue;
					Hits.push_back(Hit{Y * Grid::Nx + X, float(W0 * T.A.z + W1 * T.B.z + W2 * T.C.z), Area > 0.0 ? -1 : 1});
				}
		}
		// Its skin's cells.
		const double Edge = std::max({(T.B - T.A).Length(), (T.C - T.A).Length(), (T.C - T.B).Length()});
		const int N = std::max(1, int(std::ceil(Edge / (Grid::Hv * 0.7))));
		for (int I = 0; I <= N; ++I)
			for (int J = 0; I + J <= N; ++J)
			{
				const double U = double(I) / N, Vv = double(J) / N;
				const int Id = Grid::VoxOf(T.A + (T.B - T.A) * U + (T.C - T.A) * Vv);
				if (Id >= 0) Out.Vox.push_back(Id);
			}
	}
	std::sort(Out.Vox.begin(), Out.Vox.end());
	Out.Vox.erase(std::unique(Out.Vox.begin(), Out.Vox.end()), Out.Vox.end());
	std::sort(Hits.begin(), Hits.end(), [](const Hit& A, const Hit& B) { return A.Col != B.Col ? A.Col < B.Col : A.Z < B.Z; });
	for (size_t I = 0; I < Hits.size();)
	{
		size_t J = I;
		int Wind = 0;
		float Start = 0.0f;
		for (; J < Hits.size() && Hits[J].Col == Hits[I].Col; ++J)
		{
			const int Before = Wind;
			Wind += Hits[J].Sign;
			if (Before == 0 && Wind != 0) Start = Hits[J].Z;
			if (Before != 0 && Wind == 0 && Hits[J].Z - Start > 0.0005f)
			{
				// (two pieces of one slot that meet end to end read as one)
				if (!Out.Spans.empty() && Out.Spans.back().Col == Hits[I].Col && Start - Out.Spans.back().Z1 < 0.0005f) Out.Spans.back().Z1 = Hits[J].Z;
				else Out.Spans.push_back(Span{Hits[I].Col, Start, Hits[J].Z});
			}
		}
		I = J;
	}
	return Out;
}

// How much of two slots' solids is the same space, in cubic centimetres, counting only where one
// is at least Deep inside the other; Marks (if given) takes the middle of each such overlap.
static double Overlap(const Solid& A, const Solid& B, double Deep = 0.004, std::vector<V3>* Marks = nullptr)
{
	double Sum = 0.0;
	size_t I = 0, J = 0;
	while (I < A.Spans.size() && J < B.Spans.size())
	{
		const Span& Sa = A.Spans[I];
		const Span& Sb = B.Spans[J];
		if (Sa.Col < Sb.Col) { ++I; continue; }
		if (Sb.Col < Sa.Col) { ++J; continue; }
		const float Lo = std::max(Sa.Z0, Sb.Z0), Hi = std::min(Sa.Z1, Sb.Z1);
		if (Hi - Lo > Deep)
		{
			Sum += double(Hi - Lo);
			if (Marks) Marks->push_back(V3(Grid::Ox + (Sa.Col % Grid::Nx + 0.5) * Grid::H, Grid::Oy + (Sa.Col / Grid::Nx + 0.5) * Grid::H, 0.5 * (Lo + Hi)));
		}
		if (Sa.Z1 < Sb.Z1) ++I; else ++J;
	}
	return Sum * Grid::H * Grid::H * 1e6;
}

// The skins of every slot of a bike on one grid: a bit a slot in each cell.
struct SkinGrid
{
	std::vector<unsigned long long> Cell;
	SkinGrid() : Cell(size_t(Grid::Vx) * Grid::Vy * Grid::Vz, 0ULL) {}
	void Put(const Solid& S, int Bit, bool On)
	{
		const unsigned long long M = 1ULL << Bit;
		for (int Id : S.Vox) { if (On) Cell[size_t(Id)] |= M; else Cell[size_t(Id)] &= ~M; }
	}
	// The nearest any slot of Others' skin comes to slot Bit's, in metres, up to Far (then Far).
	double Gap(const Solid& S, int Bit, unsigned long long Others, double Far = 0.084) const
	{
		(void)Bit;
		int Best = int(std::lround(Far / Grid::Hv));
		for (int Id : S.Vox)
		{
			if (Best == 0) break;
			const int X = Id % Grid::Vx, Y = (Id / Grid::Vx) % Grid::Vy, Z = Id / (Grid::Vx * Grid::Vy);
			if (Cell[size_t(Id)] & Others) { Best = 0; break; }
			for (int R = 1; R < Best; ++R)
			{
				bool Found = false;
				for (int Dz = -R; Dz <= R && !Found; ++Dz)
					for (int Dy = -R; Dy <= R && !Found; ++Dy)
					{
						const bool Shell = std::abs(Dz) == R || std::abs(Dy) == R;
						for (int Dx = -R; Dx <= R; Dx += Shell ? 1 : 2 * R)
						{
							const int Xx = X + Dx, Yy = Y + Dy, Zz = Z + Dz;
							if (Xx < 0 || Yy < 0 || Zz < 0 || Xx >= Grid::Vx || Yy >= Grid::Vy || Zz >= Grid::Vz) continue;
							if (Cell[(size_t(Zz) * Grid::Vy + Yy) * Grid::Vx + Xx] & Others) { Found = true; break; }
						}
					}
				if (Found) { Best = R; break; }
			}
		}
		return Best * Grid::Hv;
	}
};

using SlotPair = std::pair<std::string, std::string>;

struct Report
{
	std::map<SlotPair, double> Clash;      // cm3 of overlap, pairs over a little
	std::map<std::string, double> Gap;     // metres to the nearest other slot's skin
	std::vector<V3> Marks;
};

struct Checked
{
	std::vector<Solid> Solids;   // as Bike::Slots
	Report Rep;
};

static bool BoxesNear(const SlotMesh& A, const SlotMesh& B, double Pad)
{
	return A.Lo.x <= B.Hi.x + Pad && B.Lo.x <= A.Hi.x + Pad && A.Lo.y <= B.Hi.y + Pad && B.Lo.y <= A.Hi.y + Pad && A.Lo.z <= B.Hi.z + Pad && B.Lo.z <= A.Hi.z + Pad;
}

static Checked CheckBike(const Bike& B, SkinGrid& Skin, bool WantMarks = false)
{
	Checked C;
	const size_t N = std::min<size_t>(B.Slots.size(), 64);
	C.Solids.resize(B.Slots.size());
	for (size_t I = 0; I < N; ++I)
	{
		C.Solids[I] = Solidify(B.Slots[I].Tris);
		Skin.Put(C.Solids[I], int(I), true);
	}
	for (size_t I = 0; I < N; ++I)
	{
		for (size_t J = I + 1; J < N; ++J)
		{
			if (!BoxesNear(B.Slots[I], B.Slots[J], 0.0)) continue;
			const double V = Overlap(C.Solids[I], C.Solids[J]);
			if (V > 0.5) C.Rep.Clash[SlotPair(std::min(B.Slots[I].Slot, B.Slots[J].Slot), std::max(B.Slots[I].Slot, B.Slots[J].Slot))] = V;
		}
		C.Rep.Gap[B.Slots[I].Slot] = Skin.Gap(C.Solids[I], int(I), ~(1ULL << I));
	}
	(void)WantMarks;
	for (size_t I = 0; I < N; ++I) Skin.Put(C.Solids[I], int(I), false);
	return C;
}


// --- What counts as wrong: more than any of the 14 designed bikes shows -------------------------

struct Norms
{
	std::map<SlotPair, double> Clash;
	std::map<std::string, double> Gap;
	double ClashAllowed(const SlotPair& P) const { auto It = Clash.find(P); return (It != Clash.end() ? It->second * 1.3 : 0.0) + 25.0; }
	double GapAllowed(const std::string& S) const { auto It = Gap.find(S); return (It != Gap.end() ? It->second : 0.0) + 0.0125; }
};

struct Fault
{
	std::string A, B;   // a clash of two slots; or A floating (B empty)
	double Amount = 0.0, Allowed = 0.0;   // cm3, or metres
};

static std::vector<Fault> FaultsOf(const Report& R, const Norms& N)
{
	std::vector<Fault> Out;
	for (const auto& C : R.Clash)
		if (C.second > N.ClashAllowed(C.first)) Out.push_back(Fault{C.first.first, C.first.second, C.second, N.ClashAllowed(C.first)});
	for (const auto& G : R.Gap)
		if (G.second > N.GapAllowed(G.first)) Out.push_back(Fault{G.first, "", G.second, N.GapAllowed(G.first)});
	return Out;
}

// One number for how wrong a bike is: each clash's cubic centimetres over what is allowed, and 60
// for every 12 mm a floating part hangs clear.
static double Wrongness(const std::vector<Fault>& F)
{
	double Sum = 0.0;
	for (const Fault& X : F) Sum += X.B.empty() ? 60.0 * (X.Amount - X.Allowed + 0.012) / 0.012 : X.Amount - X.Allowed;
	return Sum;
}

// --- The trial solver ----------------------------------------------------------------------------
// It does what a patient builder does in geometry mode, and nothing else: the rig's four handles
// (tank mounts, pegs, seat hoop height and tilt), then each loose or clashing part moved, tipped or
// sized a little, always the smallest change that mends the most. It never touches the frame, the
// engine, the forks, the wheels or the drive.

struct Move
{
	V3 D;                 // metres, ours
	double Scale = 1.0;   // about the part's middle
	double Pitch = 0.0;   // degrees about the bike's sideways axis, nose up
	bool None() const { return D.Length() < 1e-9 && std::fabs(Scale - 1.0) < 1e-9 && std::fabs(Pitch) < 1e-9; }
	double Cost() const { return D.Length() * 100.0 * 4.0 + std::fabs(Scale - 1.0) * 100.0 * 5.0 + std::fabs(Pitch) * 3.0; }
};

static std::vector<Tri> Moved(const SlotMesh& S, const V3& Pivot, const Move& M)
{
	std::vector<Tri> Out = S.Tris;
	const double A = M.Pitch * 3.14159265358979 / 180.0, Co = std::cos(A), Si = std::sin(A);
	for (Tri& T : Out)
		for (V3* P : {&T.A, &T.B, &T.C})
		{
			V3 Q = (*P - Pivot) * M.Scale;
			Q = V3(Q.x * Co - Q.y * Si, Q.x * Si + Q.y * Co, Q.z);
			*P = Pivot + Q + M.D;
		}
	return Out;
}

static const std::map<std::string, std::string>& MoveKinds()
{
	// What each slot may do: m move (along and up), z sideways too, s size, p pitch.
	static const std::map<std::string, std::string> K = {
		{"tank", "msp"}, {"saddle", "msp"}, {"tail", "msp"}, {"fairing", "msp"}, {"side_panels", "ms"}, {"exhaust", "mzp"}, {"air_filter", "mzs"},
		{"oil_filter", "mz"}, {"cooling", "ms"}, {"headlight", "m"}, {"dashboard", "m"}, {"horn", "m"}, {"indicators", "m"}, {"mirrors", "m"},
		{"front_mudguard", "msp"}, {"panniers", "ms"}, {"top_box", "ms"}, {"tank_bag", "ms"}, {"aux_tank", "m"}, {"toolbox", "m"}, {"ecu", "m"},
		{"fog_lights", "m"}, {"number_boards", "m"}, {"guards", "ms"}, {"rearsets", "m"}, {"side_stand", "m"}, {"centre_stand", "m"}, {"satnav", "m"},
		{"steering_damper", "m"}, {"rear_master", "m"}, {"lever_guards", "m"}};
	return K;
}
// What rides with a part when it moves.
static std::vector<std::string> Riders(const std::string& Slot)
{
	if (Slot == "tank") return {"tank_bag"};
	if (Slot == "tail") return {"number_boards", "top_box"};
	return {};
}

struct Solved
{
	BikeGeo Rig;                         // the rig's handles it chose
	std::map<std::string, Move> Moves;   // each moved slot
	std::vector<std::string> Words;      // what it did, in words
	int Tries = 0;
};

struct Fitted
{
	Bike B;
	Checked C;
	std::vector<Fault> Faults;
	double Wrong = 0.0;
};

static Fitted Measure(World& Wd, const PartsMap& Parts, const BikeGeo& Rig, const Norms& N, SkinGrid& Skin)
{
	Fitted F;
	F.B = Wd.Build(Parts, Rig);
	F.C = CheckBike(F.B, Skin);
	F.Faults = FaultsOf(F.C.Rep, N);
	F.Wrong = Wrongness(F.Faults);
	return F;
}

static std::string Cm(double Metres) { char Buf[32]; std::snprintf(Buf, sizeof(Buf), "%.0f cm", std::fabs(Metres) * 100.0); return Buf; }

static Solved Solve(World& Wd, const PartsMap& Parts, const Norms& N, SkinGrid& Skin, Fitted& F)
{
	Solved S;
	if (F.Faults.empty()) return S;
	// 1. The rig's handles, one at a time, where a part they move is at fault.
	bool Top = false;
	for (const Fault& X : F.Faults)
		for (const std::string& Sl : {X.A, X.B})
			if (Sl == "tank" || Sl == "saddle" || Sl == "tail" || Sl == "rearsets") Top = true;
	if (Top)
	{
		struct Handle { double LayoutAdjust::* Field; std::vector<double> Tries; const char* Words; };
		const std::vector<Handle> Handles = {{&LayoutAdjust::TankUpMm, {15.0, 30.0}, "tank mounts raised %.0f mm"}, {&LayoutAdjust::HoopUpMm, {-20.0, 20.0, 40.0}, "seat hoop moved %+.0f mm"},
			{&LayoutAdjust::HoopDeg, {-6.0, 4.0, 8.0}, "seat hoop tipped %+.0f degrees"}, {&LayoutAdjust::PegsMm, {-40.0, 40.0}, "pegs moved %+.0f mm"}};
		for (const Handle& H : Handles)
		{
			double BestV = 0.0;
			Fitted Best;
			bool Better = false;
			for (double V : H.Tries)
			{
				BikeGeo G = S.Rig;
				G.Rig.*(H.Field) = V;
				Fitted T = Measure(Wd, Parts, G, N, Skin);
				++S.Tries;
				// (a handle has to earn its keep: 2 for every millimetre or half degree)
				if (T.Wrong + std::fabs(V) * 2.0 < (Better ? Best.Wrong + std::fabs(BestV) * 2.0 : F.Wrong) - 10.0) { Best = std::move(T); BestV = V; Better = true; }
			}
			if (Better)
			{
				S.Rig.Rig.*(H.Field) = BestV;
				F = std::move(Best);
				char Buf[80];
				std::snprintf(Buf, sizeof(Buf), H.Words, BestV);
				S.Words.push_back(Buf);
			}
		}
	}
	// 2. Each part at fault that may move: the best small change, again and again.
	const size_t Count = std::min<size_t>(F.B.Slots.size(), 64);
	std::vector<V3> Pivot(Count);
	std::vector<SlotMesh> Rest(F.B.Slots.begin(), F.B.Slots.begin() + long(Count));   // as the rig left them
	for (size_t I = 0; I < Count; ++I) Pivot[I] = (Rest[I].Lo + Rest[I].Hi) * 0.5;
	auto IndexOf = [&](const std::string& Slot) { for (size_t I = 0; I < Count; ++I) if (Rest[I].Slot == Slot) return int(I); return -1; };
	for (size_t I = 0; I < Count; ++I) Skin.Put(F.C.Solids[I], int(I), true);
	// The wrongness a slot (with what rides on it) is part of, given its solid as it would be.
	auto Local = [&](const std::vector<int>& Group, const std::vector<Solid>& Sol, const std::vector<SlotMesh>& Mesh, std::map<SlotPair, double>* ClashOut, std::map<std::string, double>* GapOut)
	{
		double Sum = 0.0;
		for (int Gi : Group)
		{
			const std::string& Slot = Rest[size_t(Gi)].Slot;
			for (size_t J = 0; J < Count; ++J)
			{
				if (int(J) == Gi || (std::find(Group.begin(), Group.end(), int(J)) != Group.end() && int(J) < Gi)) continue;
				const SlotPair Key(std::min(Slot, Rest[J].Slot), std::max(Slot, Rest[J].Slot));
				const SlotMesh& Other = std::find(Group.begin(), Group.end(), int(J)) != Group.end() ? Mesh[J] : F.B.Slots[J];
				double V = 0.0;
				if (BoxesNear(Mesh[size_t(Gi)], Other, 0.0)) V = Overlap(Sol[size_t(Gi)], Sol[J]);
				if (ClashOut) { if (V > 0.5) (*ClashOut)[Key] = V; else ClashOut->erase(Key); }
				if (V > N.ClashAllowed(Key)) Sum += V - N.ClashAllowed(Key);
			}
			const double G = Skin.Gap(Sol[size_t(Gi)], Gi, ~(1ULL << Gi));
			if (GapOut) (*GapOut)[Slot] = G;
			if (G > N.GapAllowed(Slot)) Sum += 60.0 * (G - N.GapAllowed(Slot) + 0.012) / 0.012;
		}
		return Sum;
	};
	for (int Round = 0; Round < 3; ++Round)
	{
		// Slots at fault, the worst first.
		std::map<std::string, double> Blame;
		for (const Fault& X : F.Faults)
		{
			const double Wt = X.B.empty() ? 60.0 * (X.Amount - X.Allowed + 0.012) / 0.012 : X.Amount - X.Allowed;
			Blame[X.A] += Wt;
			if (!X.B.empty()) Blame[X.B] += Wt;
		}
		std::vector<std::pair<double, std::string>> Order;
		for (const auto& Bl : Blame) if (MoveKinds().count(Bl.first)) Order.push_back({-Bl.second, Bl.first});
		std::sort(Order.begin(), Order.end());
		bool Any = false;
		for (const auto& Od : Order)
		{
			const std::string& Slot = Od.second;
			const int Si = IndexOf(Slot);
			if (Si < 0) continue;
			const std::string& Kinds = MoveKinds().at(Slot);
			std::vector<int> Group = {Si};
			for (const std::string& Rd : Riders(Slot)) { const int Ri = IndexOf(Rd); if (Ri >= 0) Group.push_back(Ri); }
			Move Cur = S.Moves.count(Slot) ? S.Moves[Slot] : Move();
			std::vector<Solid> Sol = F.C.Solids;
			std::vector<SlotMesh> Mesh(F.B.Slots.begin(), F.B.Slots.begin() + long(Count));
			double Have = Local(Group, Sol, Mesh, nullptr, nullptr) + Cur.Cost();
			for (int Step = 0; Step < 8; ++Step)
			{
				std::vector<Move> Cand;
				for (double D : {0.01, 0.02, 0.04})
					for (double Sg : {-1.0, 1.0})
					{
						Move M = Cur; M.D.x += D * Sg; Cand.push_back(M);
						M = Cur; M.D.y += D * Sg; Cand.push_back(M);
						if (Kinds.find('z') != std::string::npos) { M = Cur; M.D.z += D * Sg * (Pivot[size_t(Si)].z >= 0.0 ? 1.0 : -1.0); Cand.push_back(M); }
					}
				if (Kinds.find('s') != std::string::npos) for (double Sc : {0.95, 1.05}) { Move M = Cur; M.Scale *= Sc; Cand.push_back(M); }
				if (Kinds.find('p') != std::string::npos) for (double P : {-2.0, 2.0}) { Move M = Cur; M.Pitch += P; Cand.push_back(M); }
				double BestCost = Have;
				Move BestM = Cur;
				std::vector<Solid> BestSol;
				std::vector<SlotMesh> BestMesh;
				for (const Move& M : Cand)
				{
					if (M.D.Length() > 0.121 || M.Scale < 0.84 || M.Scale > 1.16 || std::fabs(M.Pitch) > 8.1) continue;
					std::vector<Solid> TrySol = Sol;
					std::vector<SlotMesh> TryMesh = Mesh;
					for (int Gi : Group)
					{
						// (what rides on it turns about its middle, not its own)
						SlotMesh Sm = Rest[size_t(Gi)];
						Sm.Tris = Moved(Rest[size_t(Gi)], Pivot[size_t(Si)], M);
						Sm.Lo = V3(1e9, 1e9, 1e9); Sm.Hi = V3(-1e9, -1e9, -1e9);
						for (const Tri& T : Sm.Tris)
							for (const V3* P : {&T.A, &T.B, &T.C})
							{
								Sm.Lo = V3(std::min(Sm.Lo.x, P->x), std::min(Sm.Lo.y, P->y), std::min(Sm.Lo.z, P->z));
								Sm.Hi = V3(std::max(Sm.Hi.x, P->x), std::max(Sm.Hi.y, P->y), std::max(Sm.Hi.z, P->z));
							}
						Skin.Put(Sol[size_t(Gi)], Gi, false);
						TrySol[size_t(Gi)] = Solidify(Sm.Tris);
						TryMesh[size_t(Gi)] = std::move(Sm);
					}
					for (int Gi : Group) Skin.Put(TrySol[size_t(Gi)], Gi, true);
					const double Cost = Local(Group, TrySol, TryMesh, nullptr, nullptr) + M.Cost();
					for (int Gi : Group) Skin.Put(TrySol[size_t(Gi)], Gi, false);
					for (int Gi : Group) Skin.Put(Sol[size_t(Gi)], Gi, true);
					++S.Tries;
					if (Cost < BestCost - 1.0) { BestCost = Cost; BestM = M; BestSol = std::move(TrySol); BestMesh = std::move(TryMesh); }
				}
				if (BestSol.empty()) break;
				for (int Gi : Group) Skin.Put(Sol[size_t(Gi)], Gi, false);
				Sol = std::move(BestSol);
				Mesh = std::move(BestMesh);
				for (int Gi : Group) Skin.Put(Sol[size_t(Gi)], Gi, true);
				Cur = BestM;
				Have = BestCost;
				Any = true;
			}
			if (!Cur.None())
			{
				S.Moves[Slot] = Cur;
				for (int Gi : Group) { F.C.Solids[size_t(Gi)] = Sol[size_t(Gi)]; F.B.Slots[size_t(Gi)] = Mesh[size_t(Gi)]; }
			}
		}
		// The whole bike measured again as it now stands.
		Report R;
		for (size_t I = 0; I < Count; ++I)
		{
			for (size_t J = I + 1; J < Count; ++J)
			{
				if (!BoxesNear(F.B.Slots[I], F.B.Slots[J], 0.0)) continue;
				const double V = Overlap(F.C.Solids[I], F.C.Solids[J]);
				if (V > 0.5) R.Clash[SlotPair(std::min(F.B.Slots[I].Slot, F.B.Slots[J].Slot), std::max(F.B.Slots[I].Slot, F.B.Slots[J].Slot))] = V;
			}
			R.Gap[F.B.Slots[I].Slot] = Skin.Gap(F.C.Solids[I], int(I), ~(1ULL << I));
		}
		F.C.Rep = R;
		F.Faults = FaultsOf(R, N);
		F.Wrong = Wrongness(F.Faults);
		if (!Any || F.Faults.empty()) break;
	}
	for (size_t I = 0; I < Count; ++I) Skin.Put(F.C.Solids[I], int(I), false);
	for (const auto& Mv : S.Moves)
	{
		const Move& M = Mv.second;
		std::string W = Mv.first + ":";
		if (std::fabs(M.D.y) > 1e-6) W += std::string(M.D.y > 0 ? " up " : " down ") + Cm(M.D.y);
		if (std::fabs(M.D.x) > 1e-6) W += std::string(M.D.x > 0 ? " forward " : " back ") + Cm(M.D.x);
		if (std::fabs(M.D.z) > 1e-6) W += " sideways " + Cm(M.D.z);
		if (std::fabs(M.Scale - 1.0) > 1e-6) { char Buf[40]; std::snprintf(Buf, sizeof(Buf), " %+.0f%% size", (M.Scale - 1.0) * 100.0); W += Buf; }
		if (std::fabs(M.Pitch) > 1e-6) { char Buf[40]; std::snprintf(Buf, sizeof(Buf), " tipped %+.0f deg", M.Pitch); W += Buf; }
		S.Words.push_back(W);
	}
	return S;
}

// The red marks for a bike's faults, and the slots to tint (floating: amber).
static void MarksOf(const Fitted& F, std::vector<V3>& Marks, std::map<std::string, Rgb>& Tint)
{
	for (const Fault& X : F.Faults)
	{
		if (X.B.empty()) { Tint[X.A] = Rgb{1.0f, 0.62f, 0.0f}; continue; }
		const Solid* A = nullptr;
		const Solid* Bs = nullptr;
		for (size_t I = 0; I < F.B.Slots.size() && I < F.C.Solids.size(); ++I)
		{
			if (F.B.Slots[I].Slot == X.A) A = &F.C.Solids[I];
			if (F.B.Slots[I].Slot == X.B) Bs = &F.C.Solids[I];
		}
		if (A && Bs) Overlap(*A, *Bs, 0.004, &Marks);
	}
}

static std::string Esc(const std::string& S)
{
	std::string O;
	for (char C : S) { if (C == '"' || C == '\\') O += '\\'; O += C; }
	return O;
}
static std::string FaultsJson(const std::vector<Fault>& Fs)
{
	std::string O = "[";
	for (size_t I = 0; I < Fs.size(); ++I)
	{
		char Buf[200];
		std::snprintf(Buf, sizeof(Buf), "%s{\"a\":\"%s\",\"b\":\"%s\",\"amount\":%.4f,\"allowed\":%.4f}", I ? "," : "", Fs[I].A.c_str(), Fs[I].B.c_str(), Fs[I].Amount, Fs[I].Allowed);
		O += Buf;
	}
	return O + "]";
}


// --- Shape: does it look like a bike of its type? (round 2) -------------------------------------
// Clashes aren't the whole of it. These measure what an eye reads on a bike seen from the side: how
// full the frame is of engine, whether the seat is carried by a subframe, how the tail and guards
// sit to the seat and tyres, whether thin parts run through others, how a fairing meets the bike,
// and the bike's proportions. Each type's own designed bike says what the numbers should be.

struct Outline
{
	std::vector<int> Lo, Hi;            // per column along the bike: lowest and highest cell (or -1)
	std::vector<unsigned char> On;      // Vx by Vy
	Outline() : Lo(Grid::Vx, -1), Hi(Grid::Vx, -1), On(size_t(Grid::Vx) * Grid::Vy, 0) {}
	bool Any = false;
};

static const Solid* SolidOf(const Fitted& F, const std::string& Slot)
{
	for (size_t I = 0; I < F.B.Slots.size() && I < F.C.Solids.size(); ++I) if (F.B.Slots[I].Slot == Slot) return &F.C.Solids[I];
	return nullptr;
}
static const SlotMesh* MeshOf(const Fitted& F, const std::string& Slot)
{
	for (const SlotMesh& S : F.B.Slots) if (S.Slot == Slot) return &S;
	return nullptr;
}
static int BitOf(const Fitted& F, const std::string& Slot)
{
	for (size_t I = 0; I < F.B.Slots.size() && I < 64; ++I) if (F.B.Slots[I].Slot == Slot) return int(I);
	return -1;
}
static unsigned long long MaskOf(const Fitted& F, const std::vector<std::string>& Slots)
{
	unsigned long long M = 0;
	for (const std::string& S : Slots) { const int B = BitOf(F, S); if (B >= 0) M |= 1ULL << B; }
	return M;
}

static Outline OutlineOf(const Fitted& F, const std::vector<std::string>& Slots)
{
	Outline O;
	for (const std::string& Sl : Slots)
	{
		const Solid* S = SolidOf(F, Sl);
		if (!S) continue;
		for (int Id : S->Vox)
		{
			const int X = Id % Grid::Vx, Y = (Id / Grid::Vx) % Grid::Vy;
			O.On[size_t(Y) * Grid::Vx + X] = 1;
			if (O.Lo[size_t(X)] < 0 || Y < O.Lo[size_t(X)]) O.Lo[size_t(X)] = Y;
			if (Y > O.Hi[size_t(X)]) O.Hi[size_t(X)] = Y;
			O.Any = true;
		}
	}
	return O;
}

// How much of A's skin is inside B's solid (0..1).
static double InsideFraction(const Solid* A, const Solid* B, double Deep = 0.004)
{
	if (!A || !B || A->Vox.empty() || B->Spans.empty()) return 0.0;
	size_t In = 0;
	for (int Id : A->Vox)
	{
		const int X = Id % Grid::Vx, Y = (Id / Grid::Vx) % Grid::Vy, Z = Id / (Grid::Vx * Grid::Vy);
		const double Px = Grid::Ox + (X + 0.5) * Grid::Hv, Py = Grid::Oy + (Y + 0.5) * Grid::Hv, Pz = Grid::Oz + (Z + 0.5) * Grid::Hv;
		const int Col = int((Py - Grid::Oy) / Grid::H) * Grid::Nx + int((Px - Grid::Ox) / Grid::H);
		auto It = std::lower_bound(B->Spans.begin(), B->Spans.end(), Col, [](const Span& S, int C) { return S.Col < C; });
		for (; It != B->Spans.end() && It->Col == Col; ++It)
			if (Pz > It->Z0 + Deep && Pz < It->Z1 - Deep) { ++In; break; }
	}
	return double(In) / double(A->Vox.size());
}

static std::map<std::string, double> ShapeOf(Fitted& F, SkinGrid& Skin, const BikeLayout& Lay, std::vector<std::string>& Misfit)
{
	std::map<std::string, double> M;
	const size_t Count = std::min<size_t>(F.B.Slots.size(), 64);
	const Layout& L = F.B.Lay;
	const double Wb = std::max(0.5, L.Stats.Wheelbase);
	// 1. The engine in the frame: the room over it, and how much of its bay it fills.
	{
		const Outline G = OutlineOf(F, {"engine", "head", "fuel", "spark_plugs", "gearbox"});
		const Outline C = OutlineOf(F, {"tank", "frame", "saddle"});
		double Sum = 0.0, Worst = 0.0, Fill = 0.0, Bay = 0.0;
		int N = 0;
		for (int X = 0; X < Grid::Vx; ++X)
		{
			if (G.Hi[size_t(X)] < 0) continue;
			int Y = G.Hi[size_t(X)] + 1;
			const int Cap = std::min(Grid::Vy - 1, G.Hi[size_t(X)] + 25);
			while (Y <= Cap && !C.On[size_t(Y) * Grid::Vx + X]) ++Y;
			const double Gap = (Y - G.Hi[size_t(X)] - 1) * Grid::Hv;
			Sum += Gap; Worst = std::max(Worst, Gap); ++N;
			Fill += G.Hi[size_t(X)] - G.Lo[size_t(X)] + 1;
			Bay += Y - G.Lo[size_t(X)];
		}
		if (N > 0) { M["engine_headroom"] = Sum / N; M["engine_headroom_max"] = Worst; M["engine_bay_fill"] = Fill / std::max(1.0, Bay); }
	}
	// 2. The seat on its subframe: how far under the seat the nearest structure is, and how much of its length is carried.
	{
		const Outline S = OutlineOf(F, {"saddle"});
		const Outline U = OutlineOf(F, {"frame", "tail", "side_panels", "shock", "tank", "swingarm"});
		double Sum = 0.0;
		int N = 0, Held = 0;
		for (int X = 0; X < Grid::Vx; ++X)
		{
			if (S.Lo[size_t(X)] < 0) continue;
			int Y = std::min(Grid::Vy - 1, S.Lo[size_t(X)] + 2);
			const int Floor = std::max(0, S.Lo[size_t(X)] - 25);
			while (Y >= Floor && !U.On[size_t(Y) * Grid::Vx + X]) --Y;
			const double Gap = std::max(0, S.Lo[size_t(X)] - Y) * Grid::Hv;
			Sum += Gap; ++N;
			if (Gap <= 0.045) ++Held;
		}
		if (N > 0) { M["seat_gap"] = Sum / N; M["seat_carried"] = double(Held) / N; }
	}
	for (size_t I = 0; I < Count; ++I) Skin.Put(F.C.Solids[I], int(I), true);
	auto GapTo = [&](const std::string& A, const std::vector<std::string>& Others, const char* Name)
	{
		const int Bit = BitOf(F, A);
		const unsigned long long Mask = MaskOf(F, Others);
		if (Bit < 0 || !Mask) return;
		M[Name] = Skin.Gap(F.C.Solids[size_t(Bit)], Bit, Mask, 0.12);
	};
	// 3. What hangs: the nearest each of these comes to what should carry it.
	GapTo("tail", {"frame", "saddle", "swingarm", "shock", "side_panels"}, "tail_hang");
	GapTo("saddle", {"frame", "tank", "tail", "shock"}, "seat_hang");
	GapTo("fairing", {"frame", "fork", "handlebars", "headlight"}, "fairing_hang");
	GapTo("fairing", {"tank"}, "fairing_to_tank");
	GapTo("front_mudguard", {"fork"}, "front_guard_hang");
	GapTo("headlight", {"fork", "fairing", "handlebars", "frame"}, "headlight_hang");
	GapTo("exhaust", {"engine", "head"}, "exhaust_hang");
	GapTo("tank", {"frame"}, "tank_hang");
	for (size_t I = 0; I < Count; ++I) Skin.Put(F.C.Solids[I], int(I), false);
	// 4. What runs through what: how much of one's skin is inside the other.
	const std::vector<std::pair<const char*, const char*>> Through = {{"chain", "swingarm"}, {"final_drive", "swingarm"}, {"tail", "saddle"}, {"saddle", "tail"},
		{"tail", "rear_tyre"}, {"saddle", "tank"}, {"saddle", "rear_tyre"}, {"fairing", "front_tyre"}, {"fairing", "fork"}, {"fairing", "tank"}, {"tank", "fairing"},
		{"fairing", "engine"}, {"engine", "fairing"}, {"fairing", "frame"}, {"front_mudguard", "front_tyre"}, {"exhaust", "swingarm"}, {"exhaust", "rear_tyre"},
		{"exhaust", "frame"}, {"tank", "engine"}, {"tank", "head"}, {"head", "tank"}, {"handlebars", "tank"}, {"handlebars", "fairing"}, {"panniers", "rear_tyre"},
		{"shock", "rear_tyre"}, {"swingarm", "rear_tyre"}, {"chain", "rear_tyre"}, {"chain", "frame"}};
	for (const auto& P : Through)
	{
		const Solid* A = SolidOf(F, P.first);
		const Solid* B = SolidOf(F, P.second);
		if (A && B) M[std::string(P.first) + "_in_" + P.second] = InsideFraction(A, B);
	}
	// 5. The guards over the tyres: how far off the tread they ride.
	auto Guard = [&](const char* Slot, const V3& Axle, double Radius, const char* Name)
	{
		const Solid* S = SolidOf(F, Slot);
		if (!S) return;
		std::vector<double> Clear;
		for (int Id : S->Vox)
		{
			const int X = Id % Grid::Vx, Y = (Id / Grid::Vx) % Grid::Vy, Z = Id / (Grid::Vx * Grid::Vy);
			const double Px = Grid::Ox + (X + 0.5) * Grid::Hv, Py = Grid::Oy + (Y + 0.5) * Grid::Hv, Pz = Grid::Oz + (Z + 0.5) * Grid::Hv;
			if (std::fabs(Pz) > 0.05) continue;
			const double R = std::hypot(Px - Axle.x, Py - Axle.y);
			if (R < Radius + 0.25 && Py > Axle.y - 0.05) Clear.push_back(R - Radius);
		}
		if (Clear.size() < 20) return;
		std::sort(Clear.begin(), Clear.end());
		M[std::string(Name) + "_min"] = Clear[Clear.size() / 20];
		M[std::string(Name) + "_mid"] = Clear[Clear.size() / 2];
	};
	Guard("tail", L.RearAxle, L.Stats.RearRadius, "rear_guard_clear");
	Guard("front_mudguard", L.FrontAxle, L.Stats.FrontRadius, "front_guard_clear");
	// 6. Proportions, against the wheelbase.
	M["wheelbase"] = Wb;
	M["rake"] = L.Stats.RakeDeg;
	M["seat_height"] = L.Stats.SeatHeight;
	auto Span1 = [&](const char* Slot, const char* Name)
	{
		const SlotMesh* S = MeshOf(F, Slot);
		if (!S || S->Tris.empty()) return;
		M[std::string(Name) + "_len"] = (S->Hi.x - S->Lo.x) / Wb;
		M[std::string(Name) + "_top"] = (S->Hi.y - L.Ground()) / Wb;
		M[std::string(Name) + "_rear"] = (S->Lo.x - L.RearAxle.x) / Wb;
		M[std::string(Name) + "_front"] = (S->Hi.x - L.FrontAxle.x) / Wb;
	};
	Span1("tank", "tank"); Span1("saddle", "seat"); Span1("tail", "tail"); Span1("handlebars", "bars"); Span1("fairing", "fairing"); Span1("exhaust", "exhaust");
	if (const SlotMesh* E = MeshOf(F, "engine")) if (const SlotMesh* Fr = MeshOf(F, "frame"))
	{
		M["engine_width_vs_frame"] = (E->Hi.z - E->Lo.z) / std::max(0.05, Fr->Hi.z - Fr->Lo.z);
		M["engine_len"] = (E->Hi.x - E->Lo.x) / Wb;
		M["engine_low"] = (E->Lo.y - L.Ground()) / Wb;
	}
	// 7. Parts the catalogue says don't suit this type of bike.
	for (const char* Slot : {"frame", "fork", "engine", "tank", "saddle", "tail", "handlebars", "exhaust", "fairing", "swingarm", "front_wheel", "rear_wheel",
		"front_tyre", "rear_tyre", "headlight", "front_mudguard", "shock"})
	{
		auto It = F.B.Parts.find(Slot);
		if (It == F.B.Parts.end() || It->second == "none") continue;
		const std::vector<std::string> St = Lay.StylesOf(It->second);
		if (!St.empty() && std::find(St.begin(), St.end(), L.Style) == St.end()) Misfit.push_back(Slot);
	}
	return M;
}

int main(int Argc, char** Argv)
{
	if (Argc < 3) { std::printf("usage: fit_trial <repo root> one <style> <out.ppm>\n"); return 2; }
	World Wd;
	std::string Error;
	if (!Wd.Load(Argv[1], Error)) { std::printf("%s\n", Error.c_str()); return 1; }
	const std::string Mode = Argv[2];
	if (Mode == "one")
	{
		const double T0 = Now();
		Bike B = Wd.Build(Wd.Lay.ReferenceBuild(Argv[3]));
		const double T1 = Now();
		size_t Tris = 0;
		for (const SlotMesh& S : B.Slots) Tris += S.Tris.size();
		Render(B, View::Side(), 900, 560).Save(std::string(Argv[4]) + "_side.ppm");
		Render(B, View::Quarter(), 900, 560).Save(std::string(Argv[4]) + "_q.ppm");
		for (const SlotMesh& S : B.Slots) std::printf("  %-16s %-26s %7zu  x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n", S.Slot.c_str(), B.Parts.count(S.Slot) ? B.Parts[S.Slot].c_str() : "", S.Tris.size(), S.Lo.x, S.Hi.x, S.Lo.y, S.Hi.y, S.Lo.z, S.Hi.z);
		std::printf("%s: %zu slots, %zu triangles; built in %.2f s, drawn in %.2f s; fit %s\n", Argv[3], B.Slots.size(), Tris, T1 - T0, Now() - T1, B.Lay.Fit.Tier.c_str());
	}
	if (Mode == "refs")
	{
		// What the 14 designed bikes measure: how much each pair of slots overlaps and each slot's gap.
		SkinGrid Skin;
		std::map<SlotPair, std::vector<double>> Pairs;
		std::map<std::string, std::vector<double>> Gaps;
		for (const std::string& St : Wd.Lay.Styles())
		{
			const double T0 = Now();
			Bike B = Wd.Build(Wd.Lay.ReferenceBuild(St));
			const double T1 = Now();
			const Checked C = CheckBike(B, Skin);
			std::printf("%s: built %.2f s, checked %.2f s\n", St.c_str(), T1 - T0, Now() - T1);
			for (const auto& P : C.Rep.Clash) Pairs[P.first].push_back(P.second);
			for (const auto& G : C.Rep.Gap) Gaps[G.first].push_back(G.second);
		}
		for (const auto& P : Pairs)
		{
			std::printf("%-16s %-16s n=%2zu  max %8.1f :", P.first.first.c_str(), P.first.second.c_str(), P.second.size(), *std::max_element(P.second.begin(), P.second.end()));
			for (double V : P.second) std::printf(" %.0f", V);
			std::printf("\n");
		}
		for (const auto& G : Gaps)
		{
			std::printf("gap %-16s n=%2zu max %3.0f mm :", G.first.c_str(), G.second.size(), *std::max_element(G.second.begin(), G.second.end()) * 1000.0);
			for (double V : G.second) std::printf(" %.0f", V * 1000.0);
			std::printf("\n");
		}
	}
	if (Mode == "grid" && Argc >= 5)
	{
		// fit_trial <root> grid <builds.json> <out folder> [first] [stride]
		std::string Text;
		Json Doc;
		if (!ReadFile(Argv[3], Text) || !Json::Parse(Text, Doc, &Error)) { std::printf("can't read %s\n", Argv[3]); return 1; }
		const std::string OutDir = Argv[4];
		const std::vector<Json>& Builds = Doc.Get("builds")->A;
		const size_t First = Argc > 5 ? size_t(std::atoi(Argv[5])) : 0, Stride = Argc > 6 ? size_t(std::atoi(Argv[6])) : 1;
		const size_t Last = Argc > 7 ? size_t(std::atoi(Argv[7])) : Builds.size();
		SkinGrid Skin;
		Norms N;
		for (const std::string& St : Wd.Lay.Styles())
		{
			Bike B = Wd.Build(Wd.Lay.ReferenceBuild(St));
			const Checked C = CheckBike(B, Skin);
			for (const auto& P : C.Rep.Clash) N.Clash[P.first] = std::max(N.Clash[P.first], P.second);
			for (const auto& G : C.Rep.Gap) N.Gap[G.first] = std::max(N.Gap[G.first], G.second);
		}
		View Vw = View::Side();
		Vw.Centre = V3(0.0, 0.40, 0.0);
		Vw.Scale = 172.0;
		// (from above: the front to the right, as the side view, the bike's right side at the bottom)
		View Tv = View::Top();
		Tv.Up = V3(0, 0, -1);
		Tv.Centre = V3(0.0, 0.30, 0.0);
		Tv.Scale = 172.0;
		// A side view over a top view, as one picture.
		auto Both = [&](const Bike& Bk, const std::map<std::string, Rgb>& Tint, const std::vector<V3>& Marks, const std::string& Path)
		{
			const Image Sd = Render(Bk, Vw, 480, 300, Tint, Marks), Tp = Render(Bk, Tv, 480, 180, Tint, Marks);
			Image Out(480, 480);
			std::copy(Sd.Px.begin(), Sd.Px.end(), Out.Px.begin());
			std::copy(Tp.Px.begin(), Tp.Px.end(), Out.Px.begin() + long(Sd.Px.size()));
			Out.Save(Path);
		};
		std::ofstream Res(OutDir + "/results_" + std::to_string(First) + ".jsonl");
		for (size_t I = First; I < Builds.size() && I < Last; I += Stride)
		{
			const Json& Bd = Builds[I];
			PartsMap Parts;
			for (const auto& Kv : Bd.Get("parts")->O) Parts[Kv.first] = Kv.second.S;
			const double T0 = Now();
			Fitted F = Measure(Wd, Parts, BikeGeo(), N, Skin);
			const std::vector<Fault> Before = F.Faults;
			const double WrongBefore = F.Wrong;
			const std::string Tier = F.B.Lay.Fit.Tier;
			char Name[64];
			std::snprintf(Name, sizeof(Name), "%05zu", I);
			{
				std::vector<V3> Marks;
				std::map<std::string, Rgb> Tint;
				MarksOf(F, Marks, Tint);
				Both(F.B, Tint, Marks, OutDir + "/" + Name + "_a.ppm");
			}
			Solved Sv;
			if (!Before.empty())
			{
				Sv = Solve(Wd, Parts, N, Skin, F);
				std::vector<V3> Marks;
				std::map<std::string, Rgb> Tint;
				MarksOf(F, Marks, Tint);
				Both(F.B, Tint, Marks, OutDir + "/" + Name + "_b.ppm");
			}
			std::string Words = "[";
			for (size_t K = 0; K < Sv.Words.size(); ++K) Words += std::string(K ? "," : "") + "\"" + Esc(Sv.Words[K]) + "\"";
			Words += "]";
			char Head[400];
			std::snprintf(Head, sizeof(Head), "{\"n\":%zu,\"id\":\"%s\",\"tier\":\"%s\",\"wrong_before\":%.1f,\"wrong_after\":%.1f,\"tries\":%d,\"secs\":%.2f,", I, Esc(Bd.Str("id")).c_str(), Tier.c_str(), WrongBefore, F.Wrong, Sv.Tries, Now() - T0);
			Res << Head << "\"before\":" << FaultsJson(Before) << ",\"after\":" << FaultsJson(F.Faults) << ",\"did\":" << Words << "}\n";
			Res.flush();
			std::printf("%s %s: %.0f -> %.0f (%d tries, %.1f s)\n", Name, Bd.Str("id").c_str(), WrongBefore, F.Wrong, Sv.Tries, Now() - T0);
			std::fflush(stdout);
		}
	}
	if (Mode == "shape" && Argc >= 5)
	{
		// fit_trial <root> shape <builds.json> <out folder> [first] [stride]: each build's shape numbers, as built.
		std::string Text;
		Json Doc;
		if (!ReadFile(Argv[3], Text) || !Json::Parse(Text, Doc, &Error)) { std::printf("can't read %s\n", Argv[3]); return 1; }
		const std::vector<Json>& Builds = Doc.Get("builds")->A;
		const size_t First = Argc > 5 ? size_t(std::atoi(Argv[5])) : 0, Stride = Argc > 6 ? size_t(std::atoi(Argv[6])) : 1;
		SkinGrid Skin;
		Norms N;
		std::ofstream Res(std::string(Argv[4]) + "/shape_" + std::to_string(First) + ".jsonl");
		for (size_t I = First; I < Builds.size(); I += Stride)
		{
			PartsMap Parts;
			for (const auto& Kv : Builds[I].Get("parts")->O) Parts[Kv.first] = Kv.second.S;
			Fitted F = Measure(Wd, Parts, BikeGeo(), N, Skin);
			std::vector<std::string> Misfit;
			const std::map<std::string, double> M = ShapeOf(F, Skin, Wd.Lay, Misfit);
			Res << "{\"n\":" << I << ",\"style\":\"" << F.B.Lay.Style << "\",\"tier\":\"" << F.B.Lay.Fit.Tier << "\",\"m\":{";
			bool Sep = false;
			for (const auto& Kv : M) { char Buf[96]; std::snprintf(Buf, sizeof(Buf), "%s\"%s\":%.4f", Sep ? "," : "", Kv.first.c_str(), Kv.second); Res << Buf; Sep = true; }
			Res << "},\"misfit\":[";
			for (size_t K = 0; K < Misfit.size(); ++K) Res << (K ? "," : "") << "\"" << Misfit[K] << "\"";
			Res << "]}\n";
			Res.flush();
		}
	}
	return 0;
}
