// [eaves, Nov 24] Fascias, soffits, bargeboards, eaves courses, cornices, gutters and downpipes on Ride UK's
// buildings: see WorldRideEaves.h.
#include "WorldRideEaves.h"

#include <algorithm>
#include <cmath>

namespace TorqueWorld
{
namespace RideEaves
{
using namespace RideSceneBits;

// (named, not anonymous: the game's unity build puts several of these files into one)
namespace EavesBits
{
// (sizes, metres)
constexpr double FasciaDeep = 0.2;      // (a fascia board's face, and a bargeboard's)
constexpr double GutterWide = 0.11, GutterDeep = 0.075;
constexpr double PipeHalf = 0.034, PipeOff = 0.05;   // (a downpipe's half width; its middle off the wall)

struct Frame
{
	const House& H;
	P3 At(double Along, double Across, double Y) const
	{
		const double Lx = H.LongX ? Along : Across, Lz = H.LongX ? Across : Along;
		return At3(H.X + Lx * H.Cs + Lz * H.Sn, H.Base + Y, H.Z - Lx * H.Sn + Lz * H.Cs);
	}
	// (a quad by four local points (along, across, up), facing toward a local point)
	void Q(RideMesh& Out, const double (&A)[3], const double (&B)[3], const double (&C)[3], const double (&D)[3], const double (&T)[3], const Rgb& Col) const
	{
		Quad(Out, At(A[0], A[1], A[2]), At(B[0], B[1], B[2]), At(C[0], C[1], C[2]), At(D[0], D[1], D[2]), At(T[0], T[1], T[2]), Col);
	}
};

// A level run along the building (Along A0..A1) at Across C (outward Sg), on a long side: a band standing D
// out of the wall from Y0 to Y1 (its face, its underside and its top).
void Band(const Frame& F, RideMesh& Out, double Sg, double A0, double A1, double C, double D, double Y0, double Y1, const Rgb& Col, bool Top)
{
	const double Co = Sg * (C + D), Ci = Sg * C;
	F.Q(Out, {A0, Co, Y0}, {A1, Co, Y0}, {A1, Co, Y1}, {A0, Co, Y1}, {0.0, Sg * (C + 60.0), (Y0 + Y1) * 0.5}, Col);
	F.Q(Out, {A0, Ci, Y0}, {A1, Ci, Y0}, {A1, Co, Y0}, {A0, Co, Y0}, {0.0, Co, Y0 - 60.0}, Shade(Col, 0.8));
	if (Top) F.Q(Out, {A0, Ci, Y1}, {A1, Ci, Y1}, {A1, Co, Y1}, {A0, Co, Y1}, {0.0, Co, Y1 + 60.0}, Shade(Col, 1.05));
}

// The same run across an end (Across -C1..C1 at Along E, outward Sg).
void EndBand(const Frame& F, RideMesh& Out, double Sg, double C0, double C1, double E, double D, double Y0, double Y1, const Rgb& Col, bool Top)
{
	const double Eo = Sg * (E + D), Ei = Sg * E;
	F.Q(Out, {Eo, C0, Y0}, {Eo, C1, Y0}, {Eo, C1, Y1}, {Eo, C0, Y1}, {Sg * (E + 60.0), 0.0, (Y0 + Y1) * 0.5}, Col);
	F.Q(Out, {Ei, C0, Y0}, {Ei, C1, Y0}, {Eo, C1, Y0}, {Eo, C0, Y0}, {Eo, 0.0, Y0 - 60.0}, Shade(Col, 0.8));
	if (Top) F.Q(Out, {Ei, C0, Y1}, {Ei, C1, Y1}, {Eo, C1, Y1}, {Eo, C0, Y1}, {Eo, 0.0, Y1 + 60.0}, Shade(Col, 1.05));
}

// A gutter along a long side, Along A0..A1, its back at Across C (outward Sg), its rim at Y: a half-round's
// outer face, its rounded bottom as one sloped face back to the fascia, and the dark trough seen from above;
// a stop end where it ends free (not at a party wall, where the next house's carries on).
void GutterRun(const Frame& F, RideMesh& Out, double Sg, double A0, double A1, double C, double Y, const Rgb& Col, bool End0, bool End1)
{
	static const Rgb Trough = {0.03f, 0.03f, 0.03f};
	const double Ci = Sg * C, Co = Sg * (C + GutterWide), Yb = Y - GutterDeep, Ym = Y - GutterDeep * 0.45;
	const double Out1 = Sg * (C + 60.0);
	for (const double Ae : {A0, A1})
		if (Ae == A0 ? End0 : End1)
			F.Q(Out, {Ae, Ci, Yb}, {Ae, Co, Ym}, {Ae, Co, Y}, {Ae, Ci, Y}, {Ae + (Ae > (A0 + A1) * 0.5 ? 60.0 : -60.0), (Ci + Co) * 0.5, Y}, Shade(Col, 0.9));
	F.Q(Out, {A0, Co, Ym}, {A1, Co, Ym}, {A1, Co, Y}, {A0, Co, Y}, {0.0, Out1, Y}, Col);
	F.Q(Out, {A0, Ci, Yb}, {A1, Ci, Yb}, {A1, Co, Ym}, {A0, Co, Ym}, {0.0, Out1, Yb - 60.0}, Shade(Col, 0.78));
	F.Q(Out, {A0, Ci, Y - 0.012}, {A1, Ci, Y - 0.012}, {A1, Co, Y - 0.012}, {A0, Co, Y - 0.012}, {0.0, (Ci + Co) * 0.5, Y + 60.0}, Trough);
}

// The same along an end (hipped roofs: Across C0..C1 at Along E, outward Sg), running into the long sides'.
void GutterEnd(const Frame& F, RideMesh& Out, double Sg, double C0, double C1, double E, double Y, const Rgb& Col)
{
	static const Rgb Trough = {0.03f, 0.03f, 0.03f};
	const double Ei = Sg * E, Eo = Sg * (E + GutterWide), Yb = Y - GutterDeep, Ym = Y - GutterDeep * 0.45;
	const double Out1 = Sg * (E + 60.0);
	F.Q(Out, {Eo, C0, Ym}, {Eo, C1, Ym}, {Eo, C1, Y}, {Eo, C0, Y}, {Out1, 0.0, Y}, Col);
	F.Q(Out, {Ei, C0, Yb}, {Ei, C1, Yb}, {Eo, C1, Ym}, {Eo, C0, Ym}, {Out1, 0.0, Yb - 60.0}, Shade(Col, 0.78));
	F.Q(Out, {Ei, C0, Y - 0.012}, {Ei, C1, Y - 0.012}, {Eo, C1, Y - 0.012}, {Eo, C0, Y - 0.012}, {(Ei + Eo) * 0.5, 0.0, Y + 60.0}, Trough);
}

// A square pipe from local point P to Q (its three faces that show: the one to the wall left out when it
// runs down the wall), half Hw across.
void Pipe(const Frame& F, RideMesh& Out, double Sg, const double (&P)[3], const double (&Qp)[3], double Hw, const Rgb& Col, bool OnWall)
{
	// (its faces: out from the wall, and the two along it; the inner one only off the wall)
	const double Pa = P[0], Pc = P[1], Py = P[2], Qa = Qp[0], Qc = Qp[1], Qy = Qp[2];
	// out face (across + Sg*Hw)
	F.Q(Out, {Pa - Hw, Pc + Sg * Hw, Py}, {Pa + Hw, Pc + Sg * Hw, Py}, {Qa + Hw, Qc + Sg * Hw, Qy}, {Qa - Hw, Qc + Sg * Hw, Qy}, {(Pa + Qa) * 0.5, Sg * 80.0, (Py + Qy) * 0.5}, Col);
	// side faces (along +-Hw)
	for (double S : {-1.0, 1.0})
		F.Q(Out, {Pa + S * Hw, Pc - Sg * Hw, Py}, {Pa + S * Hw, Pc + Sg * Hw, Py}, {Qa + S * Hw, Qc + Sg * Hw, Qy}, {Qa + S * Hw, Qc - Sg * Hw, Qy},
			{(Pa + Qa) * 0.5 + S * 80.0, (Pc + Qc) * 0.5, (Py + Qy) * 0.5}, Shade(Col, 0.82));
	if (!OnWall)
		F.Q(Out, {Pa - Hw, Pc - Sg * Hw, Py}, {Pa + Hw, Pc - Sg * Hw, Py}, {Qa + Hw, Qc - Sg * Hw, Qy}, {Qa - Hw, Qc - Sg * Hw, Qy}, {(Pa + Qa) * 0.5, -Sg * 80.0, (Py + Qy) * 0.5 - 40.0}, Shade(Col, 0.7));
}

// The roof's surface height over a point Across c of a pitched roof (gabled or hipped long faces): Low at
// the eaves' outer edge (Co), Top on the ridge.
double RoofY(const House& H, double C)
{
	const double Co = H.HalfShort + H.Over, Low = H.Eaves - H.Drop, Top = H.Eaves + H.Rise;
	if (H.Shape == 3)   // (skillion: low at +Co, high at -Co)
		return Low + (Top + H.Drop - Low) * (Co - C) / (2.0 * Co);
	return Top - (Top - Low) * std::fabs(C) / Co;
}

double InnerOf(const House& H, double Fb)
{
	// (the soffit's edge on the wall: level with the fascia's foot, raised to clear the top windows' heads)
	return std::clamp(std::fmax(Fb, H.HeadTop + 0.05), Fb, H.Eaves - 0.03);
}
} // namespace EavesBits
using namespace EavesBits;

double TrimUnder(const House& H)
{
	const double Low = H.Eaves - H.Drop;
	switch (H.Style)
	{
	case EStyle::Boxed: return std::fmin(H.Eaves, InnerOf(H, Low - FasciaDeep));
	case EStyle::StoneCourse: return H.Course ? H.Eaves - 0.24 : std::fmin(H.Eaves - 0.06, H.Eaves - H.Drop - GutterDeep - 0.04);
	case EStyle::Cornice: return H.Shape == 2 ? H.Eaves - 0.36 : std::fmax(H.HeadTop + 0.03, H.Eaves - 0.36);
	case EStyle::FeltTrim: return H.Eaves - 0.16;
	case EStyle::GutterOnly: return Low - GutterDeep - 0.02;
	default: return H.Eaves;
	}
}

void Draw(const House& H, int EavesLevel, RideMesh& Out)
{
	if (EavesLevel <= 0 || H.Style == EStyle::None || H.HalfLong < 0.8 || H.HalfShort < 0.8) return;
	const Frame F{H};
	const bool Gutters = H.Gutters && EavesLevel >= 2, Pipes = Gutters && H.Pipes;
	const double HL = H.HalfLong, HS = H.HalfShort, Low = H.Eaves - H.Drop, Top = H.Eaves + H.Rise;
	const bool Pitched = H.Shape != 2;
	const double Co = HS + H.Over;
	const double A0 = -(HL + H.OverMinus), A1 = HL + H.OverPlus;   // (the eaves' run, past the gables by the verges)

	// The eaves' sides that drain (a skillion's low side only; a flat roof's back, the side away from its door).
	double Sides[2] = {1.0, -1.0};
	int NSides = 2;
	if (H.Shape == 3) NSides = 1;
	else if (!Pitched) { Sides[0] = -H.FrontSide; NSides = 1; }

	double GutterBack = Co, GutterY = Low - 0.015;   // (where each long side's gutter hangs: its back, its rim)
	double PipeTopY = Low - FasciaDeep - 0.1;        // (where the swan neck meets the wall pipe)

	switch (H.Style)
	{
	case EStyle::Boxed:
	{
		const double Fb = Low - FasciaDeep, Inner = InnerOf(H, Fb);
		PipeTopY = Fb - 0.28;
		for (double Sg : {1.0, -1.0})
		{
			const bool Eav = H.Shape != 3 || Sg > 0.0;
			// (a skillion's high side: its fascia at the top, a soffit back to its wall)
			const double Ly = Eav ? Low : Top + H.Drop, Fby = Ly - FasciaDeep;
			const double In = Eav ? Inner : std::clamp(Fby + 0.05, Fby, Top - 0.03);
			F.Q(Out, {A0, Sg * HS, In}, {A1, Sg * HS, In}, {A1, Sg * Co, Fby}, {A0, Sg * Co, Fby}, {0.0, Sg * (HS + Co) * 0.5, Fby - 60.0}, H.Soffit);
			F.Q(Out, {A0, Sg * Co, Fby}, {A1, Sg * Co, Fby}, {A1, Sg * Co, Ly + 0.012}, {A0, Sg * Co, Ly + 0.012}, {0.0, Sg * (Co + 60.0), Ly}, H.Fascia);
			if (H.Shape == 1) continue;
			// (the box's ends at the verges, closing it under the bargeboard)
			for (double Se : {1.0, -1.0})
			{
				if (Se > 0.0 ? H.PartyPlus : H.PartyMinus) continue;
				const double E = Se > 0.0 ? A1 : A0;
				const double Wy = Eav ? H.Eaves : Top;
				F.Q(Out, {E, Sg * HS, In}, {E, Sg * Co, Fby}, {E, Sg * Co, Ly}, {E, Sg * HS, Wy}, {E + Se * 60.0, Sg * HS, Ly}, Shade(H.Fascia, 0.92));
			}
		}
		if (H.Shape == 1)
		{
			// (a hipped roof's eaves round its ends too, its corners under the long sides' boxes)
			for (double Se : {1.0, -1.0})
			{
				if (Se > 0.0 ? H.PartyPlus : H.PartyMinus) continue;
				const double Eo = Se * (HL + H.Over), Ei = Se * HL;
				F.Q(Out, {Ei, -HS, Inner}, {Ei, HS, Inner}, {Eo, HS, Fb}, {Eo, -HS, Fb}, {(Ei + Eo) * 0.5, 0.0, Fb - 60.0}, H.Soffit);
				// (where its soffit, sloping out from the end wall, meets the long sides' sloping across: a
				// triangle each corner closes the step between them)
				if (Inner - Fb > 0.01)
					for (double Sv : {1.0, -1.0})
						for (double Side : {-1.0, 1.0})   // (a face each way, 2 mm apart)
						{
							const double At0 = Sv * (HS + Side * 0.002);
							Quad(Out, F.At(Ei, At0, Inner), F.At(Eo, At0, Inner), F.At(Eo, At0, Fb), F.At(Ei, At0, Inner), F.At((Ei + Eo) * 0.5, Sv * (HS + Side * 50.0), Fb), H.Soffit);
						}
				F.Q(Out, {Eo, -Co, Fb}, {Eo, Co, Fb}, {Eo, Co, Low + 0.012}, {Eo, -Co, Low + 0.012}, {Se * (HL + 60.0), 0.0, Low}, H.Fascia);
				if (Gutters) GutterEnd(F, Out, Se, -Co - GutterWide, Co + GutterWide, HL + H.Over, GutterY, H.Gutter);
			}
		}
		else
		{
			// (the verges: a bargeboard up each slope of a gable, its own soffit back to the gable wall)
			for (double Se : {1.0, -1.0})
			{
				const double OE = Se > 0.0 ? H.OverPlus : H.OverMinus;
				if (OE < 0.03) continue;
				const double Eo = Se * (HL + OE), Ei = Se * HL;
				const double Slopes[2] = {1.0, -1.0};
				for (int K = 0; K < (H.Shape == 3 ? 1 : 2); ++K)
				{
					const double T = Slopes[K];
					// (from the eaves' corner up to the ridge; a skillion's whole slope)
					const double Ca = H.Shape == 3 ? Co : T * Co, Cb = H.Shape == 3 ? -Co : 0.0;
					const double Ya = RoofY(H, Ca), Yb = RoofY(H, Cb);
					F.Q(Out, {Eo, Ca, Ya - FasciaDeep}, {Eo, Cb, Yb - FasciaDeep}, {Eo, Cb, Yb + 0.012}, {Eo, Ca, Ya + 0.012}, {Se * (HL + 60.0), (Ca + Cb) * 0.5, (Ya + Yb) * 0.5}, H.Fascia);
					const double Wa = H.Shape == 3 ? HS : T * HS, Wb = H.Shape == 3 ? -HS : 0.0;
					const double Ywa = RoofY(H, Wa) - FasciaDeep, Ywb = RoofY(H, Wb) - FasciaDeep;
					F.Q(Out, {Ei, Wa, Ywa}, {Eo, Wa, Ywa}, {Eo, Wb, Ywb}, {Ei, Wb, Ywb}, {(Ei + Eo) * 0.5, (Wa + Wb) * 0.5, std::fmin(Ywa, Ywb) - 60.0}, H.Soffit);
				}
			}
		}
		break;
	}
	case EStyle::StoneCourse:
	{
		// (a dressed course under the eaves, the roof sailing just past it; the gutter on its iron brackets)
		const double D = 0.075;
		if (H.Course)
			for (int K = 0; K < NSides; ++K)
				Band(F, Out, Sides[K], -HL - (H.PartyMinus ? 0.0 : D), HL + (H.PartyPlus ? 0.0 : D), HS, D, H.Eaves - 0.24, H.Eaves - 0.02, H.Trim, false);
		GutterBack = std::fmax(HS + (H.Course ? D + 0.01 : 0.03), Co - 0.05);
		GutterY = std::fmin(Low - 0.01, H.Eaves - 0.03);
		PipeTopY = H.Eaves - 0.5;
		break;
	}
	case EStyle::Cornice:
	{
		if (!Pitched)
		{
			// (a flat roof's: a moulded cornice round all four walls under its parapet)
			const double Y2 = H.Eaves - 0.04, Y1 = H.Eaves - 0.2, Y0 = H.Eaves - 0.34;
			for (double Sg : {1.0, -1.0})
			{
				Band(F, Out, Sg, -HL - 0.2, HL + 0.2, HS, 0.2, Y1, Y2, H.Trim, true);
				Band(F, Out, Sg, -HL - 0.08, HL + 0.08, HS, 0.08, Y0, Y1, Shade(H.Trim, 0.95), false);
				EndBand(F, Out, Sg, -HS - 0.2, HS + 0.2, HL, 0.2, Y1, Y2, H.Trim, true);
				EndBand(F, Out, Sg, -HS - 0.08, HS + 0.08, HL, 0.08, Y0, Y1, Shade(H.Trim, 0.95), false);
			}
			GutterY = -1.0;   // (it drains inside its parapet)
			PipeTopY = H.Eaves - 0.4;
			break;
		}
		// (a pitched roof's: a corona out to under the roof's edge on a bed mould, the gutter on its top)
		const double Dc = std::clamp(H.Over - 0.04, 0.14, 0.26), Y2 = std::fmin(H.Eaves, Low + 0.03);
		const double Y1 = std::fmax(H.HeadTop + 0.03, std::fmin(Y2 - 0.12, H.Eaves - 0.2)), Y0 = std::fmax(H.HeadTop + 0.01, Y1 - 0.13);
		for (int K = 0; K < NSides; ++K)
		{
			const double Sg = Sides[K];
			Band(F, Out, Sg, -HL - (H.PartyMinus ? 0.0 : Dc), HL + (H.PartyPlus ? 0.0 : Dc), HS, Dc, Y1, Y2, H.Trim, false);
			if (Y1 - Y0 > 0.04) Band(F, Out, Sg, -HL - (H.PartyMinus ? 0.0 : 0.08), HL + (H.PartyPlus ? 0.0 : 0.08), HS, 0.08, Y0, Y1, Shade(H.Trim, 0.94), false);
			// (the corona's ends at an open gable)
			for (double Se : {1.0, -1.0})
				if (!(Se > 0.0 ? H.PartyPlus : H.PartyMinus))
				{
					const double E = Se * (HL + Dc);
					F.Q(Out, {E, Sg * HS, Y1}, {E, Sg * (HS + Dc), Y1}, {E, Sg * (HS + Dc), Y2}, {E, Sg * HS, Y2}, {E + Se * 60.0, Sg * HS, Y2}, Shade(H.Trim, 0.9));
				}
		}
		GutterBack = HS + Dc - 0.02;
		GutterY = Y2 + 0.06;
		PipeTopY = Y0 - 0.25;
		break;
	}
	case EStyle::FeltTrim:
	{
		// (a flat-roofed garage's or extension's: a fascia board round the felt's edge)
		for (double Sg : {1.0, -1.0})
		{
			Band(F, Out, Sg, -HL - 0.05, HL + 0.05, HS, 0.05, H.Eaves - 0.16, H.Eaves + 0.06, H.Fascia, true);
			EndBand(F, Out, Sg, -HS - 0.05, HS + 0.05, HL, 0.05, H.Eaves - 0.16, H.Eaves + 0.06, H.Fascia, true);
		}
		GutterBack = HS + 0.05;
		GutterY = H.Eaves + 0.02;
		PipeTopY = H.Eaves - 0.45;
		break;
	}
	case EStyle::GutterOnly:
		GutterBack = std::fmax(HS + 0.02, Co - 0.04);
		PipeTopY = Low - 0.45;
		break;
	default:
		return;
	}
	// (a gable's flush verge on a stone, cornice or works roof: its thin edge, so the roof doesn't read as
	// paper; a boxed roof's bargeboards were drawn above)
	if (H.Style != EStyle::Boxed && Pitched && H.Shape != 1)
	{
		for (double Se : {1.0, -1.0})
		{
			const double OE = Se > 0.0 ? H.OverPlus : H.OverMinus;
			if (Se > 0.0 ? H.PartyPlus : H.PartyMinus) continue;
			const double Eo = Se * (HL + OE);
			for (int K = 0; K < (H.Shape == 3 ? 1 : 2); ++K)
			{
				const double T = K == 0 ? 1.0 : -1.0;
				const double Ca = H.Shape == 3 ? Co : T * Co, Cb = H.Shape == 3 ? -Co : 0.0;
				const double Ya = RoofY(H, Ca), Yb = RoofY(H, Cb);
				F.Q(Out, {Eo, Ca, Ya - 0.07}, {Eo, Cb, Yb - 0.07}, {Eo, Cb, Yb + 0.01}, {Eo, Ca, Ya + 0.01}, {Se * (HL + 60.0), (Ca + Cb) * 0.5, (Ya + Yb) * 0.5}, H.Verge);
			}
		}
	}
	if (!Gutters || GutterY < 0.0) return;

	// The gutters, along each side that drains, the length of its eaves.
	const double G0 = H.Style == EStyle::Boxed || H.Style == EStyle::GutterOnly ? A0 : -HL - (H.PartyMinus ? 0.0 : 0.06);
	const double G1 = H.Style == EStyle::Boxed || H.Style == EStyle::GutterOnly ? A1 : HL + (H.PartyPlus ? 0.0 : 0.06);
	for (int K = 0; K < NSides; ++K)
		GutterRun(F, Out, Sides[K], G0, G1, GutterBack, GutterY, H.Gutter, !H.PartyMinus && H.Shape != 1, !H.PartyPlus && H.Shape != 1);
	if (!Pipes) return;

	// The downpipes: one at an end of a house (the end away from its door, else either), both ends of a long
	// one; along a terrace one every pair of bays, on the party line between them.
	for (int K = 0; K < NSides; ++K)
	{
		const double Sg = Sides[K];
		if (H.NoFrontPipe && Sg == H.FrontSide) continue;
		double At[16];
		int N = 0;
		if (H.Bays > 1)
		{
			const double BayW = HL * 2.0 / H.Bays;
			for (int Bk = 1; Bk < H.Bays && N < 14; Bk += 2) At[N++] = -HL + Bk * BayW;
			if (H.Bays % 2 == 1 && N < 15) At[N++] = HL - 0.18;
		}
		else
		{
			const bool Party = H.PartyPlus || H.PartyMinus;
			// (a row house's at its door's other end; a free house's at one end, both ends if over 11 m)
			const double End = Party ? (H.DoorPlus ? -1.0 : 1.0) : (Chance(H.X, H.Z, 171 + int(Sg > 0.0)) < 0.5 ? -1.0 : 1.0);
			const bool AtParty = End > 0.0 ? H.PartyPlus : H.PartyMinus;
			At[N++] = End * (HL - (AtParty ? 0.08 : 0.18));
			if (!Party && HL * 2.0 > 11.0) At[N++] = -End * (HL - 0.18);
		}
		for (int I = 0; I < N; ++I)
		{
			const double U = At[I];
			// (its foot on the ground, as the corners' heights have it along the wall)
			const double T = std::clamp((U + HL) / (2.0 * HL), 0.0, 1.0);
			const int Kv = Sg > 0.0 ? 1 : 0;
			const double Gy = H.CornerUp[0][Kv] + (H.CornerUp[1][Kv] - H.CornerUp[0][Kv]) * T;
			const double FootY = std::fmin(Gy - 0.03, PipeTopY - 0.5);
			const double Wall = Sg * (HS + PipeOff), Mid = Sg * (GutterBack + GutterWide * 0.5);
			const double OutY = GutterY - GutterDeep, NeckY = PipeTopY + 0.12;
			if (std::fabs(Mid - Wall) > 0.04)
			{
				// (the swan neck from the gutter's outlet back to the wall, under the soffit)
				Pipe(F, Out, Sg, {U, Mid, OutY}, {U, Wall, NeckY}, PipeHalf, H.Pipe, true);
				Pipe(F, Out, Sg, {U, Wall, NeckY}, {U, Wall, FootY}, PipeHalf, H.Pipe, true);
			}
			else Pipe(F, Out, Sg, {U, Wall, OutY}, {U, Wall, FootY}, PipeHalf, H.Pipe, true);
		}
	}
}

Pick Choose(bool Pitched, bool Flat, bool Thatch, bool Sheet, bool Ruin, bool Glass, bool Big, bool Works, bool IsBarn, bool IsShed, bool Garage,
	bool IsHouse, bool IsShop, bool IsBlock, bool IsCivic, bool InTown, bool StoneWall, bool Rendered, double LookOver, int PeriodKind,
	const Rgb& Joinery, double RollRow, double RollOwn)
{
	static const Rgb White = {0.68f, 0.68f, 0.66f}, Brown = {0.085f, 0.05f, 0.032f}, Black = {0.022f, 0.022f, 0.024f}, Anthracite = {0.055f, 0.06f, 0.065f},
		Galv = {0.2f, 0.21f, 0.21f}, Timber = {0.09f, 0.06f, 0.04f};
	Pick P;
	const double R = 0.65 * RollRow + 0.35 * RollOwn;
	if (Ruin || Glass || Thatch) return P;
	const auto All = [&P](const Rgb& Board, const Rgb& Gut)
	{
		P.Fascia = Board;
		P.Soffit = Shade(Board, 0.94);
		P.Gutter = Gut;
		P.Pipe = Gut;
	};
	if (Works || (Big && Sheet))
	{
		// (a works or a barn under sheeting: grey or black plastic, or galvanised steel, no box)
		P.Style = Pitched ? EStyle::GutterOnly : EStyle::None;
		All(Galv, RollOwn < 0.45 ? Galv : RollOwn < 0.8 ? Anthracite : Black);
		P.Gutters = Pitched;
		return P;
	}
	if (IsBarn || Big)
	{
		// (an old stone barn: half with a cast-iron gutter on its course, half with none at all)
		if (!Pitched) return P;
		P.Style = StoneWall ? EStyle::StoneCourse : EStyle::GutterOnly;
		All(Black, Black);
		P.Gutters = RollOwn < 0.5;
		return P;
	}
	if (Flat)
	{
		// (a garage's or a shed's felt roof: its fascia, a gutter at the back; a block's or a civic
		// building's or a town shop's, a cornice under its parapet, unless it was built since the war)
		if (Garage || IsShed || (IsHouse && !InTown))
		{
			P.Style = EStyle::FeltTrim;
			All(RollOwn < 0.6 ? White : RollOwn < 0.85 ? Brown : Black, RollOwn < 0.6 ? White : RollOwn < 0.85 ? Brown : Black);
		}
		else if ((IsCivic || IsBlock || IsShop || IsHouse) && PeriodKind != 2 && (IsCivic || InTown) && RollOwn < 0.75)
		{
			P.Style = EStyle::Cornice;
			All(White, Black);
		}
		return P;
	}
	if (IsShed)
	{
		// (a shed's own: a plain dark board, now and then a gutter)
		P.Style = EStyle::Boxed;
		All(Timber, Black);
		P.Gutters = RollOwn < 0.4;
		return P;
	}
	// (old stone houses where the place builds with a flush verge and little overhang: a stone eaves
	// course and cast iron; a Georgian or Victorian town house half the time a moulded cornice)
	const bool Old = PeriodKind == 0 || PeriodKind < 0;
	// (and the whitewashed and harled cottages among them: the same flush eaves and iron gutter, no course)
	if ((StoneWall || Rendered) && LookOver <= 0.21 && Old && !Sheet && !(IsShop && InTown))
	{
		P.Style = EStyle::StoneCourse;
		P.Course = StoneWall;
		All(Black, R < 0.85 ? Black : White);
		return P;
	}
	if (PeriodKind == 0 && (InTown || IsShop || IsCivic || IsBlock) && R < 0.5)
	{
		P.Style = EStyle::Cornice;
		All(Joinery, R < 0.42 ? Black : White);
		return P;
	}
	P.Style = EStyle::Boxed;
	if (PeriodKind == 2 || (PeriodKind < 0 && !StoneWall && !Rendered && IsHouse))
	{
		// (post-war and modern: uPVC, mostly white, some brown, now and then black or anthracite)
		P.MinOver = 0.3;
		if (R < 0.66) All(White, White);
		else if (R < 0.84) All(Brown, Brown);
		else if (R < 0.93) { All(White, Black); }
		else All(Anthracite, Anthracite);
		return P;
	}
	if (PeriodKind == 1)
	{
		// (inter-war: white boards, cast iron or black plastic, some brown replacements)
		if (R < 0.5) All(White, White);
		else if (R < 0.85) All(White, Black);
		else All(Brown, Brown);
		return P;
	}
	// (older: painted timber in the house's joinery colour, cast-iron gutters, some white plastic)
	All(Joinery, R < 0.7 ? Black : White);
	return P;
}
} // namespace RideEaves
} // namespace TorqueWorld
