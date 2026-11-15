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
// (the advance of each letter in em, the space's for a space; false if one is missing)
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
		const auto It = Font.Glyphs.find(C);
		if (It == Font.Glyphs.end()) return false;
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
} // namespace TorqueWorld
