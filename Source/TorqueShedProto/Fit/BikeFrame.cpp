// The frame's Blender parts on any laid-out bike, as the Godot game's BikeModel._draw_frame draws
// them in its Rig built look: every round tube and bend swept from the Blender tube section
// (frame/tube/round.glb), the welds where tubes butt into each other, the steering head's
// bearing cups, a cast spar beam, the battery box, the footpegs and the neck gussets. The frame's
// shells (perimeter beams, pivot plates, trestle castings) and the engine mounting plates are
// code-drawn in Godot and aren't here. Kept in the Godot frame inside, as bike_model.gd is.
#include "BikeParts.h"
#include "BikePartsKit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

namespace TorqueFit
{
namespace
{
using namespace PartsKit;

constexpr double FrameInf = std::numeric_limits<double>::infinity();
constexpr double BayDownR = 0.019;   // BikeModel.BAY_DOWN_R
constexpr double BayClear = 0.012;
constexpr double BayRailX = 0.1;
constexpr double BayRise = 0.11;
constexpr double SparFrontX = 0.065;
constexpr double SparDeepest = 0.065;
const std::string FrameTube = "frame/tube/round.glb";

// One of BikeModel._rails: a frame tube's line, its radius and whether it's round.
struct FrameRail
{
	std::vector<V3> Pts;
	double R = 0.0;
	bool Round = true;
	int Mat = 0;   // its material: the frame's (0), or another one's (each side's rack has its own)
};

// BikeModel._rail_distance.
double RailDistance(const FrameRail& Rail, const V3& P)
{
	double Best = FrameInf;
	for (size_t K = 0; K + 1 < Rail.Pts.size(); ++K)
	{
		const V3 A = Rail.Pts[K], Ab = Rail.Pts[K + 1] - A;
		const double F = TorqueSim::Clamp((P - A).Dot(Ab) / std::max(Ab.Dot(Ab), 1e-9), 0.0, 1.0);
		Best = std::min(Best, (P - (A + Ab * F)).Length());
	}
	return Best;
}

// BikeModel._cut_corners.
std::vector<V3> CutCorners(const std::vector<V3>& Path)
{
	if (Path.size() < 3) return Path;
	std::vector<V3> Out = {Path[0]};
	for (size_t I = 1; I + 1 < Path.size(); ++I)
	{
		Out.push_back(MixV(Path[I], Path[I - 1], 0.25));
		Out.push_back(MixV(Path[I], Path[I + 1], 0.25));
	}
	Out.push_back(Path.back());
	return Out;
}

// A corner of _engine_corners: the point, and its piece's least and greatest |x|.
struct EngineCorner { V3 P; double Near, Far; };

void AddBoxCorners(std::vector<EngineCorner>& Out, const V3& C, const GAffine& B, const V3& H)
{
	V3 Corners[8];
	double Lo = FrameInf, Hi = -FrameInf;
	for (int I = 0; I < 8; ++I)
	{
		Corners[I] = C + B.Apply(V3(H.x * ((I & 1) ? 1.0 : -1.0), H.y * ((I & 2) ? 1.0 : -1.0), H.z * ((I & 4) ? 1.0 : -1.0)));
		Lo = std::min(Lo, Corners[I].x);
		Hi = std::max(Hi, Corners[I].x);
	}
	const double Near = (Lo <= 0.0 && Hi >= 0.0) ? 0.0 : std::min(std::fabs(Lo), std::fabs(Hi));
	const double Far = std::max(std::fabs(Lo), std::fabs(Hi));
	for (const V3& P : Corners) Out.push_back({P, Near, Far});
}

double FrameSmoothStep(double A, double B, double X)
{
	const double T = TorqueSim::Clamp((X - A) / (B - A), 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
} // namespace

void BikeParts::Frame(const PartsMap& Parts, const Layout& L, std::vector<FittedPart>& Out, double* CodeHalfW) const
{
	if (CodeHalfW) *CodeHalfW = 0.0;
	const Json* FramePart = Lay.Part(PartIn(Parts, "frame"));
	const Json* Look = FramePart ? FramePart->Get("look") : nullptr;
	const std::string Style = PartStr(FramePart, "style", "cradle");
	const bool Carbon = PartStr(FramePart, "material", "") == "carbon";
	const Json* Engine = Lay.Part(PartIn(Parts, "engine"));
	const std::string EngLayout = PartStr(Engine, "layout", "parallel");
	const int CylCount = static_cast<int>(PartNum(Engine, "cylinders", 2.0));
	const FrameRig& Frig = L.Frig;
	const double G = Frig.Gauge;
	const V3 Head = ToGodot(L.Head), Hb = ToGodot(L.HeadBottom), Axis = ToGodot(L.SteerAxis);
	const V3 Pivot = ToGodot(L.Pivot), SeatFront = ToGodot(L.SeatFront), Tail = ToGodot(L.Tail), Peg = ToGodot(L.Peg);
	const V3 E = ToGodot(L.Crank);
	const double S = L.Hard.Scale;
	const double BlockW = L.Hard.CaseHalfW * 2.0;
	const double CaseHalfW = L.Hard.CaseHalfW;
	const V3 CaseFront = ToGodot(L.Hard.CaseFront), CaseRear = ToGodot(L.Hard.CaseRear), Sump = ToGodot(L.Hard.Sump);
	const V3 MountFU = ToGodot(L.Hard.MountFU);
	const double TopY = L.Hard.TopY;
	const double HeadK = L.Hard.HeadK;
	const double BoreR = TorqueSim::Clamp(PartNum(Engine, "bore", 80.0) * 0.0006, 0.022, 0.062);
	std::vector<V3> Spine;
	for (const V3& P : L.Spine) Spine.push_back(ToGodot(P));
	const double SpineR = L.SpineR;
	const std::vector<Cylinder> Cyls = Cylinders(Parts, L);
	std::vector<FrameRail> Rails;

	// (_tube and _bend in the frame: the tube section swept along, and the line kept for the welds.)
	auto Tube = [&](const V3& A, const V3& B, double R, int Mat = 0) {
		Sweep(Out, "frame", FrameTube, {A, B}, {R}, "");
		Rails.push_back({{A, B}, R, true, Mat});
	};
	auto Bend = [&](const std::vector<V3>& Path, double R, int Mat = 0) {
		Sweep(Out, "frame", FrameTube, Path, {R}, "");
		Rails.push_back({SmoothCurve(Path, 6), R, true, Mat});
	};

	// (_rig_low, _behind_front_tyre.)
	auto RigLow = [&](double R) { return E.y + Sump.y - R * G - 0.004; };
	auto BehindFrontTyre = [&](const V3& P, double R, double Limit) {
		const V3 FAxle(0.0, L.Stats.FrontRadius - L.Stats.RearRadius, -L.Stats.Wheelbase * 0.5);
		const double Reach = L.Stats.FrontRadius + R + 0.03;
		const double Dy = P.y - FAxle.y;
		if (std::fabs(Dy) >= Reach) return P;
		return V3(P.x, P.y, std::min(std::max(P.z, FAxle.z + std::sqrt(Reach * Reach - Dy * Dy)), std::max(Limit, P.z)));
	};

	// (_engine_corners: the cases, barrels, heads and plugs as the engine is drawn.)
	auto EngineCorners = [&]() {
		std::vector<EngineCorner> C;
		const double Half = BlockW * 0.5;
		AddBoxCorners(C, E, GAffine(), V3(Half, 0.13 * S, 0.17 * S));
		if (EngLayout == "2stroke")
			AddBoxCorners(C, E + V3(0.0, -0.16 * S, 0.02), GAffine(), V3(BlockW * 0.35, 0.03, 0.12 * S));
		else if (EngLayout != "vtwin")   // (a Rig built V-twin is dry sump)
		{
			const double SumpW = std::min(BlockW * 0.55, 0.16);
			AddBoxCorners(C, V3(E.x, E.y - 0.13 * S - 0.029, E.z - 0.01), GAffine(), V3(SumpW * 0.5, 0.03, 0.11 * S));
		}
		AddBoxCorners(C, E + V3(-Half - 0.025, -0.015 * S, -0.06 * S), GAffine(), V3(0.025, 0.085 * S, 0.085 * S));
		for (const Cylinder& Cy : Cyls)
		{
			const double Fin = BoreR * 1.55;
			AddBoxCorners(C, Cy.Base - Cy.Up * 0.005, BasisUp(Cy.Up), V3(Fin, 0.095 * HeadK, Fin));
		}
		if (EngLayout != "boxer")
			for (const Cylinder& Cy : Cyls) AddBoxCorners(C, Cy.Base + Cy.Up * 0.225 + V3(0.0, 0.0, 0.02), BasisUp(Cy.Up), V3(0.068, 0.035, 0.025));
		double Reach = 0.215, HalfW = 0.06, HalfD = 0.075;
		if (EngLayout == "2stroke") { Reach = 0.145; HalfW = BoreR * 1.8; HalfD = BoreR * 1.8; }
		else if (EngLayout == "boxer") { Reach = 0.235; HalfW = 0.08; HalfD = 0.085; }
		else if (EngLayout == "vtwin") { Reach = 0.215; HalfW = 0.075; HalfD = 0.085; }
		const double Lo = 0.09, Hi = Reach;
		if (Cyls.size() > 1 && EngLayout != "2stroke" && EngLayout != "boxer" && EngLayout != "vtwin")
		{
			const Cylinder& First = Cyls.front();
			const Cylinder& Last = Cyls.back();
			const V3 Mid = (First.Base + Last.Base) * 0.5;
			const double Span = std::fabs(Last.Base.x - First.Base.x) * 0.5 + 0.08;
			AddBoxCorners(C, Mid + First.Up * ((Lo + Hi) * 0.5), BasisUp(First.Up), V3(Span, (Hi - Lo) * 0.5 * HeadK, HalfD));
			return C;
		}
		for (const Cylinder& Cy : Cyls)
			AddBoxCorners(C, Cy.Base + Cy.Up * ((Lo + Hi) * 0.5), BasisUp(Cy.Up), V3(HalfW, (Hi - Lo) * 0.5 * HeadK, HalfD));
		return C;
	};
	(void)CylCount;

	// (_bay_front: how far forward the downtubes come down for the engine to sit behind them.)
	auto BayFront = [&](const std::vector<EngineCorner>& Pts, double Dz, double Low) {
		const double TopZ = Hb.z + 0.02, TopYv = Hb.y - 0.01;
		const double FrontY = E.y - 0.02;
		double FrontZ = E.z + Dz - 0.17 * S - 0.075;
		const double UnderZ = E.z + Dz - 0.2 * S, UnderY = Low;
		for (const EngineCorner& C : Pts)
		{
			if (C.Near > BayRailX + BayDownR + 0.02) continue;
			const V3 P = C.P;
			const double Z = P.z + Dz - BayDownR - BayClear;
			if (P.y >= FrontY && P.y <= TopYv)
			{
				const double F = (P.y - FrontY) / std::max(TopYv - FrontY, 0.001);
				if (F > 0.999)
				{
					if (TopZ > Z) return -FrameInf;
					continue;
				}
				FrontZ = std::min(FrontZ, (Z - F * TopZ) / (1.0 - F));
			}
			else if (P.y < FrontY && P.y >= UnderY)
			{
				const double F = (P.y - UnderY) / std::max(FrontY - UnderY, 0.001);
				if (F < 0.001)
				{
					if (UnderZ > Z) return -FrameInf;
					continue;
				}
				FrontZ = std::min(FrontZ, (Z - (1.0 - F) * UnderZ) / F);
			}
		}
		return FrontZ;
	};

	// (_hanger_line: from under the neck down to the lug on the front of the cases.)
	auto HangerLine = [&](double Side) {
		const V3 Lug = E + MountFU;
		const double Face = E.z + CaseFront.z;
		const double X = std::min(CaseHalfW * 0.6, 0.08) * Side;
		const double Front = BayFront(EngineCorners(), 0.0, RigLow(0.02));
		const double KneeZ = Front > -FrameInf ? std::min(Face - 0.03, Front) : Face - 0.03;
		const V3 End = BehindFrontTyre(V3(X, Lug.y, KneeZ), 0.02, KneeZ);
		const V3 HalfWay = MixV(Hb, End, 0.5);
		const V3 Mid = BehindFrontTyre(HalfWay + V3(X * 0.5, 0.0, 0.0), 0.02, HalfWay.z + 0.03);
		return std::vector<V3>{Hb + V3(0.035 * Side, -0.01, 0.02), Mid, End};
	};

	// Whether a full race fairing's fitted (BikeModel._race_fairing_fitted).
	const Json* Fairing = Lay.Part(PartIn(Parts, "fairing"));
	const bool RaceFairing = Fairing && PartStr(Fairing, "style", "") == "race" && PartStr(Fairing->Get("look"), "shape", "race") != "seeley";

	if (Style == "spar")
	{
		V3 Half = Carbon ? V3(0.048, 0.063, 0.0) : V3(0.045, 0.063, 0.0);
		Half = V3(Half.x * Frig.Wide, Half.y * Frig.Deep, 0.0);
		double Cast = 0.0;
		const std::string Beam = "frame/spar_beam.glb";
		if (HasGlb(Beam))
		{
			Cast = Carbon ? 0.6 : 0.4;
			Half.y = std::min(Half.y, SparDeepest) / (1.0 + 0.5 * Cast);
		}
		// (_spar_over: where a straight beam would run into the top end, the point it bends up over.)
		auto SparOver = [&](const V3& A, const V3& B, double HalfH) {
			std::vector<std::pair<double, double>> Need;   // z, y
			const double Top = E.y + TopY;
			for (const Cylinder& C : Cyls)
			{
				const V3 Tip = C.Base + C.Up * 0.215;
				for (double Dz : {-0.08, 0.0, 0.08, 0.12}) Need.push_back({Tip.z + Dz, std::max(Tip.y, Top) + 0.03 + 0.02 + HalfH});
			}
			double Worst = 0.0;
			V3 Over;
			bool Any = false;
			for (const auto& [Nz, Ny] : Need)
			{
				const double F = TorqueSim::Clamp((Nz - A.z) / std::max(B.z - A.z, 0.01), 0.0, 1.0);
				const double Short = Ny - TorqueSim::Lerp(A.y, B.y, F);
				if (Short > Worst)
				{
					Worst = Short;
					Over = V3(0.0, Ny, Nz);
					Any = true;
				}
			}
			if (!Any) return std::make_pair(false, V3());
			for (const auto& [Nz, Ny] : Need)
			{
				if (Nz > Over.z && Nz < B.z)
				{
					const double Gf = (Nz - Over.z) / (B.z - Over.z);
					if (Gf < 0.999) Over.y = std::max(Over.y, (Ny - B.y * Gf) / (1.0 - Gf));
				}
				else if (Nz < Over.z && Nz > A.z)
				{
					const double Gf = (Over.z - Nz) / (Over.z - A.z);
					if (Gf < 0.999) Over.y = std::max(Over.y, (Ny - A.y * Gf) / (1.0 - Gf));
				}
			}
			return std::make_pair(true, Over);
		};
		for (double Side : {-1.0, 1.0})
		{
			const V3 Front = Hb + V3(SparFrontX * Side, 0.05, 0.0);
			const V3 Back = Pivot + V3(0.12 * Side, 0.1, 0.0);
			const auto [HasOver, Over] = SparOver(Front, Back, Half.y * (1.0 + 0.5 * Cast));
			if (Cast > 0.0)
			{
				// (_spar_beam: the Blender beam bent from the head stock to the pivot plate, deeper in
				// the middle, its "arch" key lifting it over the top end.)
				const V3 Dir = (Back - Front).Normalized();
				const V3 Up = (GodotUp - Dir * Dir.Dot(GodotUp)).Normalized();
				const V3 Across = Up.Cross(Dir);
				double Peak = 0.5, Lift = 0.0;
				if (HasOver)
				{
					Peak = TorqueSim::Clamp((Over.z - Front.z) / std::max(Back.z - Front.z, 0.01), 0.15, 0.85);
					Lift = Over.y - TorqueSim::Lerp(Front.y, Back.y, Peak);
				}
				auto Warp = [Front, Back, Across, Up, Half, Peak](const V3& Q) {
					double U = Q.z;
					U = U < 0.5 ? U / 0.5 * Peak : Peak + (U - 0.5) / 0.5 * (1.0 - Peak);
					return MixV(Front, Back, U) + Across * (Q.x * Half.x) + Up * (Q.y * Half.y);
				};
				FittedPart P;
				P.Slot = "frame";
				P.Glb = Beam;
				P.Weights = {{"deep", Cast}, {"arch", Lift / Half.y}, {"taper", 1.0}};
				P.Warp = GodotWarpToOurs(Warp);
				Out.push_back(P);
				const double LiftK = Lift / Half.y;
				for (int I = 0; I < 12; ++I)
				{
					const double U0 = I / 12.0, U1 = (I + 1) / 12.0;
					const double Deep = 1.0 + 0.5 * Cast * std::max(std::sin(TorqueSim::Pi * U0), std::sin(TorqueSim::Pi * U1));
					Rails.push_back({{Warp(V3(0.0, LiftK * std::sin(TorqueSim::Pi * U0), U0)), Warp(V3(0.0, LiftK * std::sin(TorqueSim::Pi * U1), U1))},
						std::max(Half.x, Half.y * Deep), false});
				}
			}
			else if (HasOver)
			{
				V3 O = Over;
				O.x = TorqueSim::Lerp(Front.x, Back.x, 0.5);
				Rails.push_back({{Front, O}, 0.045 * std::max(Half.x / 0.045, Half.y / 0.045), false});
				Rails.push_back({{O, Back}, 0.045 * std::max(Half.x / 0.045, Half.y / 0.045), false});
			}
			else
				Rails.push_back({{Front, Back}, 0.045 * std::max(Half.x / 0.045, Half.y / 0.045), false});
			Tube(Pivot + V3(0.12 * Side, 0.1, 0.0), Pivot + V3(0.12 * Side, -0.12, 0.02), 0.04);
			Tube(SeatFront + V3(0.08 * Side, 0.0, 0.0), Tail + V3(0.06 * Side, 0.0, 0.0), 0.016 * G);
			Tube(Pivot + V3(0.1 * Side, 0.12, 0.0), Tail + V3(0.06 * Side, -0.02, -0.05), 0.014 * G);
		}
	}
	else if (Style == "adv")
	{
		// (_draw_rig_trellis: a steel trellis, the engine a stressed member in it.)
		const size_t N = Spine.size();
		const double R = 0.014 * G;
		for (double Side : {-1.0, 1.0})
		{
			const V3 PlateTop = Pivot + V3(0.1 * Side, 0.17, -0.06);
			const double Over = E.y + TopY + 0.02 + R + 0.006;
			std::vector<V3> Top = {Head + V3(0.04 * Side, -0.03, 0.03), Spine[static_cast<size_t>(static_cast<double>(N) * 0.3)] + V3(0.09 * Side, 0.0, 0.0),
				Spine[static_cast<size_t>(static_cast<double>(N) * 0.6)] + V3(0.11 * Side, 0.0, 0.0), PlateTop};
			for (int I : {1, 2}) Top[I].y = std::max(Top[I].y, Over);
			Top = CutCorners(Top);
			Bend(Top, R);
			std::vector<V3> Low = HangerLine(Side);
			V3 End = Low[2];
			End.y = std::max(End.y, E.y + TopY * 0.45);
			Low[2] = End;
			Low[1] = MixV(Low[0], End, 0.5);
			Bend(Low, R);
			const std::vector<V3> TopLine = SmoothCurve(Top, 6), LowLine = SmoothCurve(Low, 6);
			auto At = [](const std::vector<V3>& Line, double F) {
				const int I = std::clamp(static_cast<int>(F * static_cast<double>(Line.size() - 1)), 0, static_cast<int>(Line.size()) - 1);
				return Line[I];
			};
			const V3 LowEnd = LowLine.back();
			size_t Reach = 0;
			while (Reach + 1 < TopLine.size() && TopLine[Reach + 1].z <= LowEnd.z + 0.01) ++Reach;
			const double F = static_cast<double>(Reach) / static_cast<double>(TopLine.size() - 1);
			Tube(At(LowLine, 0.5), At(TopLine, F * 0.45), R * 0.8);
			Tube(At(LowLine, 1.0), At(TopLine, F * 0.6), R * 0.8);
			Tube(At(LowLine, 1.0), At(TopLine, F), R * 0.8);
			Tube(PlateTop, Pivot + V3(0.1 * Side, -0.06, 0.0), 0.022 * G);
			Tube(Pivot + V3(0.1 * Side, 0.06, -0.02), At(TopLine, 0.7), R * 0.8);
			Tube(SeatFront + V3(0.07 * Side, 0.0, 0.0), Tail + V3(0.06 * Side, 0.0, 0.0), 0.012 * G);
			Tube(PlateTop + V3(0.0, -0.02, 0.02), Tail + V3(0.06 * Side, -0.03, -0.05), 0.01 * G);
			Tube(At(TopLine, 0.85), SeatFront + V3(0.07 * Side, -0.01, 0.02), 0.011 * G);
			// The rack out past the tail.
			const int Rack = Side < 0.0 ? 1 : 2;
			Bend({Tail + V3(0.09 * Side, 0.02, -0.25), Tail + V3(0.1 * Side, 0.03, 0.05), Tail + V3(0.08 * Side, 0.03, 0.22), Tail + V3(0.0, 0.03, 0.26)}, 0.009, Rack);
			Tube(Tail + V3(0.1 * Side, 0.03, 0.05), Tail + V3(0.12 * Side, -0.16, 0.0), 0.008, Rack);
		}
		Tube(Pivot + V3(-0.1, 0.0, 0.0), Pivot + V3(0.1, 0.0, 0.0), 0.02);
	}
	else if (Style == "trestle")
	{
		// (_draw_trestle: a short trestle from the head stock to the heads; the engine's the frame.)
		V3 Fwd = E + V3(0.0, 0.2 * S, -0.1 * S), Aft = E + V3(0.0, 0.17 * S, 0.13 * S);
		if (EngLayout != "boxer" && !Cyls.empty())
		{
			std::vector<V3> Tops;
			for (const Cylinder& C : Cyls) Tops.push_back(C.Base + C.Up * (0.17 * S));
			std::sort(Tops.begin(), Tops.end(), [](const V3& A, const V3& B) { return A.z < B.z; });
			const V3 First = Tops.front(), Last = Tops.back();
			Fwd = V3(0.0, First.y, First.z - 0.03);
			Aft = Last.z - First.z < 0.1 ? V3(0.0, Last.y - 0.03, Last.z + 0.1 * S) : V3(0.0, Last.y, Last.z);
		}
		double X = TorqueSim::Clamp(BlockW * 0.32, 0.06, 0.12);
		for (const Cylinder& C : Cyls) X = std::max(X, std::fabs(C.Base.x) + 0.075 + 0.02);
		const bool Cast = Look && Look->Flag("cast");
		// (Its lugs on the engine, code-drawn, stand out to X + 0.021 each side.)
		if (CodeHalfW) *CodeHalfW = X + 0.021;
		for (double Side : {-1.0, 1.0})
		{
			const V3 U0 = Head + V3(0.045 * Side, -0.02, 0.03);
			const V3 L0 = Hb + V3(0.045 * Side, 0.01, 0.02);
			const V3 F = Fwd + V3(X * Side, 0.0, 0.0);
			const V3 A = Aft + V3(X * Side, 0.0, 0.0);
			V3 U1 = MixV(U0, A, 0.5) + V3(0.012 * Side, 0.025, 0.0);
			U1.x = A.x;
			if (!Cast)
				for (const auto& [P0, P1] : {std::make_pair(U0, U1), std::make_pair(U1, A), std::make_pair(L0, F), std::make_pair(L0, U1), std::make_pair(F, U1), std::make_pair(F, A)})
					Tube(P0, P1, 0.012 * G);
			const double Px = 0.11 * Side;
			Bend({A + V3(0.0, 0.01, 0.01), SeatFront + V3(0.075 * Side, -0.02, 0.02), Tail + V3(0.055 * Side, -0.01, 0.0)}, 0.012 * G);
			Tube(Pivot + V3(Px, 0.1, -0.03), Tail + V3(0.05 * Side, -0.05, -0.08), 0.011 * G);
		}
	}
	else if (Style == "backbone")
	{
		Tube(Head, Pivot + V3(0.0, 0.22, 0.0), 0.045 * G);
		Tube(Pivot + V3(0.0, 0.22, 0.0), Pivot, 0.035 * G);
		Tube(SeatFront, Tail, 0.018 * G);
		Tube(Pivot + V3(0.0, 0.2, 0.0), Tail + V3(0.0, -0.03, -0.05), 0.016 * G);
		for (double Side : {-1.0, 1.0}) Bend(HangerLine(Side), 0.018 * G);   // (_engine_hangers)
	}
	else if (Style == "softail" || Style == "chopper" || Style == "cruiser")
	{
		// The backbone the rig drew, the downtubes and cradle rails round the engine (_fit_bay's
		// Rig built line), the seat post behind the gearbox, and the rails out to the tail.
		Bend(Spine, SpineR);
		const double LowY = RigLow(BayDownR);
		double DownZ = E.z - 0.17 * S - 0.075;
		const double BayF = BayFront(EngineCorners(), 0.0, LowY);
		if (BayF > -FrameInf) DownZ = std::min(DownZ, BayF);
		for (double Side : {-1.0, 1.0})
		{
			const double X = BayRailX * Side;
			const V3 Down = BehindFrontTyre(V3(X, E.y - 0.02, DownZ), BayDownR * G, DownZ);
			std::vector<V3> Cradle = {Hb + V3(0.045 * Side, -0.01, 0.02), Down, V3(X, LowY, E.z - 0.2 * S), V3(X, LowY, Pivot.z - BayRise),
				V3(X, Pivot.y - 0.03, Pivot.z - 0.02)};
			Cradle.insert(Cradle.begin() + 2, V3(X, LowY + 0.035, Down.z));
			Cradle.insert(Cradle.begin() + 3, V3(X, LowY, Down.z + 0.04));
			const double CaseBack = E.z + CaseRear.z + BayDownR * G + 0.012;
			Cradle[5] = V3(X, LowY, TorqueSim::Clamp(CaseBack, Pivot.z - BayRise, Pivot.z - 0.05));
			Bend(CutCorners(Cradle), BayDownR * G);
			Bend({SeatFront + V3(0.07 * Side, -0.02, -0.04), MixV(SeatFront, Tail, 0.5) + V3(0.075 * Side, -0.02, 0.0), Tail + V3(0.07 * Side, -0.03, 0.02)}, 0.015 * G);
		}
		V3 PostMid = MixV(SeatFront, Pivot, 0.5) + V3(0.0, 0.0, -0.05);
		V3 PostLow = Pivot + V3(0.0, 0.05, -0.04);
		const double Behind = E.z + CaseRear.z + 0.026 + 0.015;
		PostMid.z = std::max(PostMid.z, Behind);
		PostLow.z = std::max(PostLow.z, Behind + 0.01);
		Bend({SeatFront + V3(0.0, -0.04, -0.02), PostMid, PostLow}, 0.026 * G);
		Tube(Pivot + V3(-0.1, 0.0, 0.0), Pivot + V3(0.1, 0.0, 0.0), 0.022);
		if (Style == "cruiser")
		{
			const double ArmRestZ = L.Stats.Wheelbase * 0.5 - Pivot.z;
			for (double Side : {-1.0, 1.0})
				Bend({Pivot + V3(0.09 * Side, 0.08, -0.04), Pivot + V3(0.12 * Side, 0.3, ArmRestZ - 0.2), Tail + V3(0.075 * Side, -0.05, 0.0)}, 0.014 * G);
		}
	}
	else if (Style == "perimeter")
	{
		// (Its cast beams and pivot plates are code-drawn shells; the subframe's tubes are here.)
		double Wide = std::max(0.15, BlockW * 0.5 + 0.05);
		const double BeamW = RaceFairing ? std::min(Wide, 0.2) : Wide;
		if (BeamW < Wide) Wide = std::min(Wide, 0.25);
		// (Its pivot plates stand out to Wide + 0.002 each side: what a full fairing keeps outside.)
		if (CodeHalfW) *CodeHalfW = Wide + 0.002;
		for (double Side : {-1.0, 1.0})
		{
			Bend({Pivot + V3((Wide - 0.04) * Side, 0.14, -0.04), SeatFront + V3(0.075 * Side, -0.02, 0.02), Tail + V3(0.05 * Side, -0.01, -0.02)}, 0.014 * G);
			Tube(Pivot + V3((Wide - 0.04) * Side, 0.06, 0.0), Tail + V3(0.05 * Side, -0.05, -0.08), 0.012 * G);
		}
	}
	else if (Style == "mx")
	{
		V3 Split(0.0, E.y - 0.06, E.z - 0.22 * S - 0.05);
		Split = BehindFrontTyre(Split, 0.022 * G, Split.z);
		Bend({Hb + V3(0.0, -0.01, 0.02), MixV(Hb, Split, 0.55) + V3(0.0, 0.0, -0.01), Split}, 0.022 * G);
		const double LowY = RigLow(0.016);
		const V3 Top = Pivot + V3(0.0, 0.22, -0.06);
		V3 Sub = SeatFront;
		Sub.z = std::max(Sub.z, Top.z + 0.08);
		const double RiseZ = std::max(Pivot.z - 0.14, E.z + CaseRear.z + 0.03);
		for (double Side : {-1.0, 1.0})
		{
			const double X = Frig.Loop * Side;
			std::vector<V3> Loop = {Split, V3(X, LowY + 0.02, Split.z + 0.06), V3(X, LowY, E.z + 0.05), V3(X + 0.01 * Side, LowY + 0.01, RiseZ),
				V3(0.08 * Side, Pivot.y - 0.05, std::max(Pivot.z - 0.03, RiseZ + 0.02)), Pivot + V3(0.09 * Side, 0.08, -0.04)};
			Bend(CutCorners(Loop), 0.016 * G);
			const double Px = std::max(0.09, CaseHalfW + 0.032);
			Bend({Top, Pivot + V3(Px * Side, 0.16, -0.06), Pivot + V3(Px * Side, 0.02, -0.03)}, 0.02 * G);
			Bend({Top + V3(0.05 * Side, 0.02, 0.02), Sub + V3(0.07 * Side, 0.0, 0.06), Tail + V3(0.06 * Side, -0.02, 0.02)}, 0.012 * G);
			Tube(Pivot + V3(0.08 * Side, 0.06, 0.0), MixV(Sub, Tail, 0.6) + V3(0.065 * Side, -0.03, 0.0), 0.011 * G);
		}
		Bend({Head + V3(0.0, -0.02, 0.02), MixV(Head, Top, 0.5) + V3(0.0, 0.03, 0.0), Top}, 0.034 * G);
	}
	else
	{
		// (_draw_rig_cradle: the backbone or twin top rails, the downtubes ahead of the cases, the
		// cradle rails under the sump up to the pivot, the seat post and the seat rails.)
		const double R = SpineR;
		const double Top = Frig.Top, Twin = Frig.Twin;
		const double Low = RigLow(0.017);
		const double Cf = E.z - 0.17 * S - 0.045;
		const V3 SeatEnd = Spine.back();
		if (Top > 0.0)
			for (double Side : {-1.0, 1.0})
			{
				std::vector<V3> Rail;
				for (size_t I = 0; I < Spine.size(); ++I)
				{
					const double F = static_cast<double>(I) / static_cast<double>(Spine.size() - 1);
					Rail.push_back(Spine[I] + V3(TorqueSim::Lerp(0.035, Top, FrameSmoothStep(0.0, 0.3, F)) * Side, 0.0, 0.0));
				}
				Bend(Rail, R * 0.8);
			}
		else
			Bend(Spine, R);
		const double BackX = std::min(Twin, 0.075);
		const bool SplitDown = Frig.Split && Twin > 0.0;
		const double Front = BayFront(EngineCorners(), 0.0, Low);
		V3 Knee(0.0, E.y - 0.02, Front > -FrameInf ? Front : Cf - 0.03);
		Knee.z = std::min(Knee.z, TorqueSim::Lerp(Hb.z, Cf, (Hb.y - Knee.y) / std::max(Hb.y - Low, 0.01)));
		Knee = BehindFrontTyre(Knee, 0.024 * G, Knee.z);
		const V3 HalfWay = MixV(Hb, Knee, 0.5);
		const V3 Mid = BehindFrontTyre(HalfWay, 0.024 * G, HalfWay.z + 0.03);
		const V3 Fork = Knee;
		if (SplitDown) Bend({Hb + V3(0.0, -0.01, 0.02), Mid, Fork}, 0.024 * G);
		for (double Side : Twin > 0.0 ? std::vector<double>{-1.0, 1.0} : std::vector<double>{0.0})
		{
			const double X = Twin * Side, Bx = BackX * Side;
			const V3 Foot = BehindFrontTyre(V3(X, Low, std::min(Cf, Knee.z + 0.02)), 0.022 * G, Cf);
			std::vector<V3> Path = {Hb + V3(0.035 * Side, -0.01, 0.02), Mid + V3(X * 0.4, 0.0, 0.0), Knee + V3(X * 0.8, 0.0, 0.0),
				V3(X, Low + 0.035, Foot.z), V3(X, Low, Foot.z + 0.04), V3(TorqueSim::Lerp(X, Bx, 0.5), Low, Pivot.z - 0.14),
				V3(Bx, Low + 0.01, Pivot.z - 0.08), V3(Bx, Low + 0.05, Pivot.z - 0.04), V3(Bx, Pivot.y - 0.03, Pivot.z - 0.02)};
			if (SplitDown)
			{
				std::vector<V3> P2 = {Fork + V3(0.0, 0.015, -0.005), MixV(Fork, Foot, 0.4) + V3(X * 0.7, 0.0, 0.0)};
				P2.insert(P2.end(), Path.begin() + 3, Path.end());
				Path = P2;
			}
			Bend(CutCorners(Path), 0.017 * G);
		}
		if (Twin > 0.0) Tube(V3(-BackX, Low + 0.01, Pivot.z - 0.08), V3(BackX, Low + 0.01, Pivot.z - 0.08), 0.016 * G);
		for (double Side : Top > 0.0 ? std::vector<double>{-1.0, 1.0} : std::vector<double>{0.0})
		{
			const V3 Up = SeatEnd + V3(Top * Side, 0.0, 0.0);
			const V3 At = Pivot + V3(Top > 0.0 ? BackX * Side : 0.0, 0.02, -0.01);
			Tube(Up, At, (Top > 0.0 ? 0.02 : 0.022) * G);
		}
		Tube(Pivot + V3(-0.1, 0.0, 0.0), Pivot + V3(0.1, 0.0, 0.0), 0.02);
		for (double Side : {-1.0, 1.0})
		{
			const double X = 0.07 * Side;
			Bend({V3(X, SeatEnd.y + 0.01, SeatEnd.z - 0.02), MixV(SeatFront, Tail, 0.35) + V3(X, -0.01, 0.0), Tail + V3(0.065 * Side, -0.01, 0.0)}, 0.016 * G);
			Tube(Pivot + V3(BackX > 0.0 ? BackX * Side : 0.06 * Side, 0.04, 0.0), Tail + V3(0.065 * Side, -0.03, -0.05), 0.014 * G);
		}
	}
	// The head stock.
	Tube(Head - Axis * 0.02, Hb, 0.038);

	// (_blender_welds: a bead round each tube's end where it butts into a thicker one.)
	{
		std::map<int, std::vector<GAffine>> Spots;   // by material, a mesh each (BlenderParts.copies)
		for (size_t Ai = 0; Ai < Rails.size(); ++Ai)
		{
			const FrameRail& A = Rails[Ai];
			if (!A.Round) continue;
			for (size_t End : {static_cast<size_t>(0), A.Pts.size() - 1})
			{
				const V3 At = A.Pts[End];
				V3 Inward;
				const int Step = End == 0 ? 1 : -1;
				for (int K = static_cast<int>(End) + Step; K >= 0 && K < static_cast<int>(A.Pts.size()); K += Step)
				{
					Inward = A.Pts[K] - At;
					if (Inward.Length() >= 0.01) break;
				}
				const double Room = Inward.Length();
				if (Room < 0.01) continue;
				Inward = Inward / Room;
				int Best = -1;
				double BestD = FrameInf;
				for (size_t Bi = 0; Bi < Rails.size(); ++Bi)
				{
					if (Bi == Ai) continue;
					const double D = RailDistance(Rails[Bi], At);
					if (D < BestD)
					{
						BestD = D;
						Best = static_cast<int>(Bi);
					}
				}
				if (Best < 0) continue;
				const double Rb = Rails[Best].R;
				if (BestD > Rb + A.R * 0.5 || A.R > Rb * 1.05) continue;
				double T = 0.0;
				while (T < std::min(Room * 0.5, 0.12) && RailDistance(Rails[Best], At + Inward * T) < Rb) T += 0.002;
				const V3 Across = Inward.Cross(std::fabs(Inward.y) < 0.9 ? GodotUp : GodotRight).Normalized();
				Spots[A.Mat].push_back(BasisCols(Inward.Cross(Across) * A.R, Inward * A.R, Across * A.R, At + Inward * T));
			}
		}
		if (HasGlb("frame/weld.glb"))
			for (const auto& [Mat, Xfs] : Spots)
			{
				FittedPart P;
				P.Slot = "frame";
				P.Glb = "frame/weld.glb";
				for (const GAffine& Xf : Xfs) P.Copies.push_back(Xf.Warp());
				Out.push_back(P);
			}
	}
	// The steering head bearings' cups in the ends of the head stock.
	for (const auto& [At, OutDir] : {std::make_pair(Head - Axis * 0.02, Axis), std::make_pair(Hb, -Axis)})
	{
		const V3 Across = OutDir.Cross(GodotRight).Normalized();
		Put(Out, "frame", "frame/bearing_cup.glb", BasisCols(OutDir.Cross(Across), OutDir, Across, At), "");
	}
	if (Style != "softail" && Style != "chopper")
		Put(Out, "frame", "frame/battery_box.glb", AtWith(GAffine::Scale(V3(0.18, 0.14, 0.14)), V3(0.0, 0.06, TorqueSim::Lerp(E.z, Pivot.z, 0.6))), "");
	// The frame's own footpegs, unless rearsets are bolted on in their place.
	if (PartIn(Parts, "rearsets") == "none")
		for (double Side : {-1.0, 1.0})
		{
			const V3 P(Peg.x * Side, Peg.y, Peg.z);
			Put(Out, "frame", "frame/footpeg.glb", AtWith(GAffine::Scale(V3(Side, 1.0, 1.0)), P), "");
			if (Peg.z < Pivot.z - 0.2) Tube(V3(0.08 * Side, Peg.y + 0.01, Peg.z + 0.02), P, 0.011);   // forward controls' stalks
		}
	// (_draw_rig_fittings: the neck gussets are Blender-made; the engine plates are code-drawn.)
	if (Frig.Gusset && HasGlb("frame/gusset.glb"))
	{
		struct Leg { V3 Start, Dir; double R; };
		std::vector<Leg> Groups[3];
		for (const FrameRail& Rail : Rails)
		{
			const V3 Start = Rail.Pts.front();
			if ((Start - Hb).Length() > 0.14 && (Start - Head).Length() > 0.14) continue;
			V3 Far = Start;
			for (const V3& P : Rail.Pts)
			{
				Far = P;
				if ((P - Start).Length() >= 0.1) break;
			}
			if ((Far - Start).Length() < 0.05) continue;
			const int Sx = std::fabs(Start.x) < 0.012 ? 0 : (Start.x > 0.0 ? 1 : -1);
			Groups[Sx + 1].push_back({Start, (Far - Start).Normalized(), Rail.R});
		}
		for (int Gi : {0, 1, 2})
		{
			std::vector<Leg>& Gr = Groups[Gi];
			if (Gr.size() < 2) continue;
			std::sort(Gr.begin(), Gr.end(), [](const Leg& A, const Leg& B) { return A.Dir.y > B.Dir.y; });
			const Leg& Up = Gr.front();
			const Leg& Down = Gr.back();
			if (AngleTo(Up.Dir, Down.Dir) < TorqueSim::DegToRad(12.0) || Up.Dir.z < 0.2) continue;
			const V3 Corner = (Up.Start + Down.Start) * 0.5;
			const V3 N = Up.Dir.Cross(Down.Dir).Normalized();
			const double Thick = std::min(Up.R, Down.R) * 0.35;
			Put(Out, "frame", "frame/gusset.glb", BasisCols(Up.Dir * 0.12, Down.Dir * 0.12, N * Thick, Corner), "");
		}
	}
}
} // namespace TorqueFit
