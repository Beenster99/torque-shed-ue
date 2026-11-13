#include "TftScreen.h"

#include "TftMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace TorqueTft
{
namespace
{
const Rgba Bg(0.012f, 0.014f, 0.018f);
const Rgba PanelBg(0.055f, 0.060f, 0.070f);
const Rgba Ink(0.96f, 0.96f, 0.95f);
const Rgba Dim(0.62f, 0.64f, 0.66f);
const Rgba Faint(0.30f, 0.31f, 0.33f);
const Rgba Accent(0.96f, 0.63f, 0.17f);
const Rgba Red(0.93f, 0.16f, 0.13f);
const Rgba Green(0.30f, 0.90f, 0.40f);
const Rgba Blue(0.30f, 0.55f, 1.0f);
const Rgba Amber(1.0f, 0.72f, 0.15f);
const Rgba Dark(0.02f, 0.02f, 0.025f);

const char* Deg = "\xC2\xB0";

float Clamp01(double V) { return float(std::min(std::max(V, 0.0), 1.0)); }
bool BlinkOn(const State& St, double Rate = 2.0) { return (long long)(std::floor(St.Blink * Rate * 2.0)) % 2 == 0; }

std::string GearText(const State& St) { return St.Gear <= 0 ? std::string("N") : Whole(double(St.Gear)); }
Rgba GearColour(const State& St) { return St.Gear <= 0 ? Green : Ink; }
const char* SpeedUnit(const State& St) { return St.Metric ? "km/h" : "mph"; }
std::string Distance(const State& St, double Km, int Decimals) { return Fixed(St.Metric ? Km : Km / 1.609344, Decimals); }
const char* DistanceUnit(const State& St) { return St.Metric ? "km" : "mi"; }
std::string Economy(const State& St)
{
	if (St.KmPerLitre <= 0.0) return "--";
	return St.Metric ? Fixed(100.0 / St.KmPerLitre, 1) : Whole(St.KmPerLitre / 1.609344 * 4.54609);
}
const char* EconomyUnit(const State& St) { return St.Metric ? "L/100" : "mpg"; }
std::string Pressure(const State& St, double Bar) { return St.Metric ? Fixed(Bar, 1) : Whole(Bar * 14.5038); }
const char* PressureUnit(const State& St) { return St.Metric ? "bar" : "psi"; }
std::string Clock(const State& St)
{
	char B[16];
	std::snprintf(B, sizeof(B), "%02d:%02d", St.Hour % 24, St.Minute % 60);
	return B;
}
// The rev counter's colour at a share of its scale: white, amber near the red line, red past it.
Rgba RevColour(const State& St, double Rpm)
{
	if (Rpm >= St.Redline) return Red;
	if (Rpm >= St.Redline * 0.86) return Amber;
	return Ink;
}

// The petrol: a pump mark and a bar of ten blocks, the last two red.
void FuelBar(Paint& P, const State& St, float X, float Y, float W, float H)
{
	// (the pump: a box, its hose a line up the side)
	P.Rect(X, Y + H * 0.15f, H * 0.55f, H * 0.85f, Ink);
	P.Rect(X + H * 0.1f, Y + H * 0.28f, H * 0.35f, H * 0.25f, Bg);
	P.Rect(X + H * 0.62f, Y + H * 0.3f, H * 0.1f, H * 0.6f, Ink);
	const float BX = X + H * 1.0f, BW = W - H * 1.0f;
	const int Blocks = 10;
	const float Gap = 2.0f, Each = (BW - Gap * float(Blocks - 1)) / float(Blocks);
	const int Lit = int(std::ceil(Clamp01(St.Fuel) * float(Blocks) - 0.001f));
	for (int I = 0; I < Blocks; ++I)
	{
		const bool On = I < Lit;
		const Rgba C = !On ? Faint : (Lit <= 2 ? (BlinkOn(St) ? Red : Faint) : Ink);
		P.Rect(BX + float(I) * (Each + Gap), Y + H * 0.2f, Each, H * 0.6f, C);
	}
}

// The next turn as an arrow in a box S across: up the stem and off the way the road goes; a flag at the finish.
void TurnArrow(Paint& P, float X, float Y, float S, int Turn, const Rgba& C)
{
	const float T = S * 0.14f;
	const P2 Base(X + S * 0.5f, Y + S * 0.95f), Elbow(X + S * 0.5f, Y + S * 0.5f);
	if (Turn == 9)
	{
		P.Line(Base, P2(Base.X, Y + S * 0.08f), T, C, true);
		P.Fan({P2(Base.X, Y + S * 0.08f), P2(Base.X + S * 0.42f, Y + S * 0.22f), P2(Base.X, Y + S * 0.38f)}, Accent);
		return;
	}
	const float A = float(Turn) * 45.0f * 0.01745329252f;
	const P2 Dir(std::sin(A), -std::cos(A));
	const P2 Tip = Elbow + Dir * (S * 0.36f);
	P.Line(Base, Elbow, T, C, true);
	P.Line(Elbow, Tip, T, C, true);
	const P2 Sd(-Dir.Y, Dir.X);
	P.Fan({Tip + Dir * (S * 0.16f), Tip - Dir * (S * 0.06f) + Sd * (S * 0.2f), Tip - Dir * (S * 0.06f) - Sd * (S * 0.2f)}, C);
}

// The road's limit as its sign: a red ring and the number, or the national limit's white disc and bar.
void LimitSign(Paint& P, const State& St, float CX, float CY, float R)
{
	if (St.Limit <= 0) return;
	const float A = St.LimitAssumed ? 0.7f : 1.0f;
	P.Disc(CX, CY, R + 2.0f, Rgba(0.0f, 0.0f, 0.0f, 0.35f));
	if (St.LimitNational)
	{
		P.Disc(CX, CY, R, Rgba(1.0f, 1.0f, 1.0f, A));
		const float C = R * 0.62f;
		P.Line(P2(CX + C, CY - C), P2(CX - C, CY + C), R * 0.24f, Rgba(0.03f, 0.03f, 0.03f, A));
		return;
	}
	P.Disc(CX, CY, R, Rgba(0.80f, 0.07f, 0.07f, A));
	P.Disc(CX, CY, R * 0.76f, Rgba(1.0f, 1.0f, 1.0f, A));
	const float Px = R * (St.Limit >= 100 ? 0.72f : 0.92f);
	P.Text(Whole(double(St.Limit)), CX, CY + Px * 0.36f, Px, Rgba(0.03f, 0.03f, 0.03f, A), Align::Centre, true);
}

// The lamps in a row centred on CX: indicators, main beam, lights, neutral, low fuel, a hot engine, ABS off.
void TellTales(Paint& P, const State& St, float CX, float Y, float S)
{
	const float Gap = S * 1.5f;
	float X = CX - Gap * 3.0f;
	auto Arrow = [&](float AX, int Way, bool Lit) {
		const Rgba C = Lit ? Green : Faint.Fade(0.6f);
		P.Fan({P2(AX + float(Way) * S * 0.5f, Y), P2(AX, Y - S * 0.45f), P2(AX, Y + S * 0.45f)}, C);
		P.Rect(AX - (Way > 0 ? S * 0.45f : 0.0f), Y - S * 0.18f, S * 0.45f, S * 0.36f, C);
	};
	Arrow(X, -1, St.IndLeft && BlinkOn(St, 1.5));
	X += Gap;
	// Main beam (blue) or dipped (green): a D and its rays.
	{
		const Rgba C = St.HighBeam ? Blue : St.Lights ? Green : Faint.Fade(0.6f);
		P.Pie(X + S * 0.1f, Y, S * 0.36f, -90.0f, 90.0f, C);
		for (int I = 0; I < 3; ++I)
		{
			const float RY = Y - S * 0.28f + S * 0.28f * float(I);
			P.Line(P2(X - S * 0.05f, RY), P2(X - S * 0.45f, RY + (St.HighBeam ? 0.0f : S * 0.14f)), S * 0.09f, C);
		}
	}
	X += Gap;
	P.Text("N", X, Y + S * 0.36f, S, St.Gear <= 0 ? Green : Faint.Fade(0.6f), Align::Centre, true);
	X += Gap;
	// Low fuel: a pump, amber.
	{
		const Rgba C = St.LowFuel ? (BlinkOn(St) ? Amber : Faint) : Faint.Fade(0.6f);
		P.Rect(X - S * 0.25f, Y - S * 0.35f, S * 0.4f, S * 0.75f, C);
		P.Rect(X + S * 0.2f, Y - S * 0.15f, S * 0.09f, S * 0.5f, C);
	}
	X += Gap;
	// A hot engine: a thermometer, red.
	{
		const Rgba C = St.Hot ? (BlinkOn(St) ? Red : Faint) : Faint.Fade(0.6f);
		P.Rect(X - S * 0.07f, Y - S * 0.42f, S * 0.14f, S * 0.6f, C);
		P.Disc(X, Y + S * 0.26f, S * 0.18f, C);
		P.Rect(X + S * 0.07f, Y - S * 0.34f, S * 0.18f, S * 0.07f, C);
		P.Rect(X + S * 0.07f, Y - S * 0.14f, S * 0.18f, S * 0.07f, C);
	}
	X += Gap;
	// ABS off: a ring with the letters.
	{
		const Rgba C = St.AbsOff ? Amber : Faint.Fade(0.6f);
		P.Ring(X, Y, S * 0.42f, S * 0.5f, C);
		P.Text("ABS", X, Y + S * 0.13f, S * 0.36f, C, Align::Centre, true);
	}
	X += Gap;
	Arrow(X, 1, St.IndRight && BlinkOn(St, 1.5));
}

// A rev counter round (CX, CY): its ring, the red end, a tick every quarter step, numbers in
// thousands, what's lit so far, and the needle.
void RevRing(Paint& P, const State& St, float CX, float CY, float R, float A0, float Sweep, bool Numbers, bool Needle)
{
	const double Max = std::max(St.RevMax, 1000.0);
	const float Frac = Clamp01(St.Rpm / Max), RedAt = Clamp01(St.Redline / Max);
	P.Arc(CX, CY, R - 5.0f, R, A0, A0 + Sweep, Faint.Fade(0.7f));
	P.Arc(CX, CY, R - 9.0f, R, A0 + Sweep * RedAt, A0 + Sweep, Red);
	if (Frac > 0.002f) P.Arc(CX, CY, R - 5.0f, R, A0, A0 + Sweep * std::min(Frac, RedAt), Ink);
	const double Step = std::max(St.RevStep, 250.0) / 4.0;
	int I = 0;
	for (double Rpm = 0.0; Rpm <= Max + 1.0; Rpm += Step, ++I)
	{
		const float A = A0 + Sweep * float(Rpm / Max);
		const bool Major = I % 4 == 0;
		const Rgba C = Rpm >= St.Redline - 1.0 ? Red : Ink;
		P.Line(Paint::On(CX, CY, R - (Major ? R * 0.13f : R * 0.07f), A), Paint::On(CX, CY, R - 1.0f, A), Major ? R * 0.018f + 1.0f : 1.4f, C);
		if (Major && Numbers)
		{
			const float Px = R * 0.15f;
			const P2 At = Paint::On(CX, CY, R * 0.74f, A);
			P.Text(Whole(Rpm / 1000.0), At.X, At.Y + Px * 0.36f, Px, C, Align::Centre, true);
		}
	}
	if (Needle)
	{
		const float A = A0 + Sweep * Frac;
		P.Line(Paint::On(CX, CY, R * 0.40f, A), Paint::On(CX, CY, R + 2.0f, A), R * 0.022f + 1.5f, Red, true);
	}
}

// The same as blocks that light one by one (the Sweep design).
void RevBlocks(Paint& P, const State& St, float CX, float CY, float R0, float R1, float A0, float Sweep, int Blocks)
{
	const double Max = std::max(St.RevMax, 1000.0);
	const float Frac = Clamp01(St.Rpm / Max);
	const float Each = Sweep / float(Blocks);
	for (int I = 0; I < Blocks; ++I)
	{
		const double At = Max * (double(I) + 0.5) / double(Blocks);
		const bool On = float(I) + 0.5f <= Frac * float(Blocks);
		const Rgba Lit = RevColour(St, At);
		P.Arc(CX, CY, R0, R1, A0 + Each * float(I) + Each * 0.12f, A0 + Each * float(I + 1) - Each * 0.12f, On ? Lit : Lit.Mix(Bg, 0.82f));
	}
}

// An analogue clock face with numbers, for the Classic design: Value of Max over the sweep.
void ClassicDial(Paint& P, float CX, float CY, float R, double Value, double Max, double Step, double RedFrom, const char* Unit, bool Thousands)
{
	const float A0 = 135.0f, Sweep = 270.0f;
	P.Disc(CX, CY, R, Rgba(0.05f, 0.055f, 0.06f));
	P.Ring(CX, CY, R - 3.0f, R, Rgba(0.55f, 0.56f, 0.58f));
	const double Top = std::max(Max, Step);
	if (RedFrom > 0.0 && RedFrom < Top) P.Arc(CX, CY, R * 0.86f, R * 0.93f, A0 + Sweep * float(RedFrom / Top), A0 + Sweep, Red);
	int I = 0;
	const double Small = Step / (Thousands ? 4.0 : 2.0);
	for (double V = 0.0; V <= Top + 0.001; V += Small, ++I)
	{
		const float A = A0 + Sweep * float(V / Top);
		const bool Major = I % (Thousands ? 4 : 2) == 0;
		P.Line(Paint::On(CX, CY, R * (Major ? 0.80f : 0.87f), A), Paint::On(CX, CY, R * 0.94f, A), Major ? 3.0f : 1.5f, Ink);
		if (Major)
		{
			const float Px = R * 0.14f;
			const P2 At = Paint::On(CX, CY, R * 0.66f, A);
			P.Text(Whole(Thousands ? V / 1000.0 : V), At.X, At.Y + Px * 0.36f, Px, Ink, Align::Centre, true);
		}
	}
	P.Text(Unit, CX, CY - R * 0.30f, R * 0.10f, Dim, Align::Centre);
	const float A = A0 + Sweep * Clamp01(Value / Top);
	P.Line(Paint::On(CX, CY, R * 0.14f, A + 180.0f), Paint::On(CX, CY, R * 0.84f, A), R * 0.03f + 1.5f, Accent, true);
	P.Disc(CX, CY, R * 0.09f, Rgba(0.16f, 0.16f, 0.17f));
	P.Disc(CX, CY, R * 0.04f, Accent);
}

// The aids' boxes in a column or a row.
void AidColumn(Paint& P, const State& St, float X, float Y, float W, float RowH)
{
	const size_t N = std::min<size_t>(St.Aids.size(), 4);
	if (N == 0) return;
	P.Frame(X, Y, W, RowH * float(N) + 8.0f, 1.5f, Dim);
	for (size_t I = 0; I < N; ++I)
	{
		const AidBox& A = St.Aids[I];
		const float Base = Y + 4.0f + RowH * float(I) + RowH * 0.78f;
		const Rgba C = A.Active && BlinkOn(St, 4.0) ? Amber : Ink;
		P.Text(A.Label, X + 10.0f, Base, RowH * 0.72f, C, Align::Left, true);
		P.Text(A.Level <= 0 ? std::string("OFF") : Whole(double(A.Level)), X + W - 10.0f, Base, RowH * 0.72f, A.Level <= 0 ? Amber : C, Align::Right, true);
	}
}
void AidRow(Paint& P, const State& St, float CX, float Y, float BoxW, float BoxH)
{
	const size_t N = std::min<size_t>(St.Aids.size(), 4);
	if (N == 0) return;
	const float Gap = 10.0f;
	float X = CX - (BoxW * float(N) + Gap * float(N - 1)) * 0.5f;
	for (size_t I = 0; I < N; ++I)
	{
		const AidBox& A = St.Aids[I];
		const Rgba C = A.Active && BlinkOn(St, 4.0) ? Amber : Ink;
		P.Frame(X, Y, BoxW, BoxH, 1.5f, Dim);
		P.Text(A.Label, X + 8.0f, Y + BoxH * 0.70f, BoxH * 0.48f, Dim, Align::Left, true);
		P.Text(A.Level <= 0 ? std::string("OFF") : Whole(double(A.Level)), X + BoxW - 8.0f, Y + BoxH * 0.70f, BoxH * 0.48f, A.Level <= 0 ? Amber : C, Align::Right, true);
		X += BoxW + Gap;
	}
}

// The trip box's three rows for the page it's on: label, value, unit.
struct TripRow { std::string Label, Value, Unit; };
std::vector<TripRow> TripRows(const State& St)
{
	std::vector<TripRow> R;
	switch (((St.TripPage % 3) + 3) % 3)
	{
	case 0:
		R.push_back({"Trip", Distance(St, St.TripKm, 1), DistanceUnit(St)});
		R.push_back({"Range", Distance(St, St.RangeKm, 0), DistanceUnit(St)});
		R.push_back({"Economy", Economy(St), EconomyUnit(St)});
		break;
	case 1:
		R.push_back({"Front tyre", Pressure(St, St.TyreBar[0]), PressureUnit(St)});
		R.push_back({"Rear tyre", Pressure(St, St.TyreBar[1]), PressureUnit(St)});
		R.push_back({"Tyres", Whole(St.TyreC[0]) + " / " + Whole(St.TyreC[1]), std::string(Deg) + "C"});
		break;
	default:
		R.push_back({"Total", Distance(St, St.TotalKm, 0), DistanceUnit(St)});
		R.push_back({"Top speed", Whole(St.Metric ? St.TopKmh : St.TopKmh / 1.609344), SpeedUnit(St)});
		R.push_back({"Engine", Whole(St.EngineC), std::string(Deg) + "C"});
		break;
	}
	return R;
}
void TripBox(Paint& P, const State& St, float X, float Y, float W, float RowH)
{
	// (a bracket down its left, with a mark for the page)
	const float H = RowH * 3.0f + 10.0f;
	P.Rect(X, Y, 2.0f, H, Dim);
	P.Rect(X, Y, 10.0f, 2.0f, Dim);
	P.Rect(X, Y + H - 2.0f, 10.0f, 2.0f, Dim);
	P.Rect(X - 3.0f, Y + 6.0f + (H - 12.0f) / 3.0f * float(((St.TripPage % 3) + 3) % 3), 5.0f, (H - 12.0f) / 3.0f, Accent);
	const std::vector<TripRow> Rows = TripRows(St);
	for (size_t I = 0; I < Rows.size(); ++I)
	{
		const float Base = Y + 5.0f + RowH * float(I) + RowH * 0.76f;
		P.Text(Rows[I].Label, X + 16.0f, Base, RowH * 0.68f, Ink);
		P.Text(Rows[I].Value, X + W - 62.0f, Base, RowH * 0.68f, Ink, Align::Right, true);
		P.Text(Rows[I].Unit, X + W - 54.0f, Base, RowH * 0.60f, Dim);
	}
}

// The sat nav's next turn in a box: the arrow, how far, onto what.
void TurnBox(Paint& P, const State& St, float X, float Y, float W, float H, bool Solid)
{
	if (!St.Nav) return;
	if (Solid) P.RoundRect(X, Y, W, H, 8.0f, Rgba(0.04f, 0.05f, 0.06f, 0.92f));
	else P.Frame(X, Y, W, H, 1.5f, Dim);
	if (St.NavOff)
	{
		P.Text("OFF ROUTE", X + 14.0f, Y + H * 0.46f, H * 0.32f, Accent, Align::Left, true);
		P.Text("Back to the ride's road", X + 14.0f, Y + H * 0.82f, H * 0.22f, Dim);
		return;
	}
	TurnArrow(P, X + 8.0f, Y + H * 0.08f, H * 0.84f, St.NavTurn, Ink);
	P.Text(St.NavDistance, X + H + 8.0f, Y + H * 0.48f, H * 0.40f, Ink, Align::Left, true);
	P.Text(St.NavOnto.substr(0, size_t(std::max(8.0f, (W - H) / (H * 0.13f)))), X + H + 8.0f, Y + H * 0.84f, H * 0.24f, Dim);
}

// The bars along the top and the foot of a landscape screen W wide: petrol, the lamps, the clock; the
// air, a notice or the place, the engine.
void Chrome(Paint& P, const State& St, float W, float H)
{
	FuelBar(P, St, 16.0f, 10.0f, 178.0f, 24.0f);
	TellTales(P, St, W * 0.5f, 22.0f, 20.0f);
	P.Text(Clock(St), W - 16.0f, 32.0f, 26.0f, Ink, Align::Right, true);
	const float Base = H - 12.0f;
	P.Text("Air", 16.0f, Base, 18.0f, Dim);
	P.Text(Whole(St.AirC) + Deg + "C", 54.0f, Base, 22.0f, Ink, Align::Left, true);
	const char* Temp = St.Hot ? "HOT" : St.EngineC < 55.0 ? "Low" : "OK";
	P.Text(Temp, W - 16.0f, Base, 22.0f, St.Hot ? Red : Ink, Align::Right, true);
	P.Text("Engine", W - 70.0f, Base, 18.0f, Dim, Align::Right);
	if (!St.Notice.empty()) P.Text(St.Notice, W * 0.5f, Base, 20.0f, Amber, Align::Centre, true);
	else if (St.Cruise) P.Text("CRUISE " + Whole(St.CruiseSpeed) + " " + SpeedUnit(St), W * 0.5f, Base, 20.0f, Green, Align::Centre, true);
	else if (!St.NavRoad.empty()) P.Text(St.NavRoad, W * 0.5f, Base, 18.0f, Dim, Align::Centre);
}

// The mode's name in a tag.
void ModeTag(Paint& P, const State& St, float X, float Y, float W, float H)
{
	P.Frame(X, Y, W, H, 2.0f, Ink);
	P.Text(St.Mode, X + W * 0.5f, Y + H * 0.74f, H * 0.62f, Ink, Align::Centre, true);
}

// ---- The ride view, each design, on the 800 x 480 screen.

void RideDial(Paint& P, const State& St)
{
	const float CX = 560.0f, CY = 244.0f, R = 180.0f;
	if (St.HasRevs) RevRing(P, St, CX, CY, R, 100.0f, 272.0f, true, true);
	P.Ring(CX, CY, 64.0f, 67.0f, Ink);
	P.Text(GearText(St), CX, CY + 22.0f, 92.0f, GearColour(St), Align::Centre, true);
	P.Text("Gear", CX, CY + 48.0f, 16.0f, Dim, Align::Centre);
	if (St.HasRevs) P.Text("rpm x1000", CX + 6.0f, CY + 112.0f, 13.0f, Dim, Align::Centre);
	ModeTag(P, St, 262.0f, CY - 17.0f, 112.0f, 34.0f);
	P.Text(Whole(St.Speed), 150.0f, 268.0f, 128.0f, Ink, Align::Centre, true);
	P.Text(SpeedUnit(St), 150.0f, 296.0f, 20.0f, Dim, Align::Centre);
	if (St.Nav) TurnBox(P, St, 20.0f, 56.0f, 250.0f, 72.0f, false);
	else if (!St.NavPlace.empty()) P.Text(St.NavPlace, 20.0f, 84.0f, 20.0f, Dim);
	TripBox(P, St, 22.0f, 322.0f, 300.0f, 30.0f);
	AidColumn(P, St, 672.0f, 340.0f, 112.0f, 23.0f);
	if (St.Nav && St.Limit > 0) LimitSign(P, St, 330.0f, 96.0f, 30.0f);
}

void RideSweep(Paint& P, const State& St)
{
	// The rev band arches over the screen: a great circle whose top is just under the lamps.
	const float CX = 400.0f, CY = 640.0f;
	if (St.HasRevs)
	{
		RevBlocks(P, St, CX, CY, 540.0f, 582.0f, 222.0f, 96.0f, 40);
		const double Max = std::max(St.RevMax, 1000.0);
		for (double Rpm = 0.0; Rpm <= Max + 1.0; Rpm += std::max(St.RevStep, 250.0))
		{
			const P2 At = Paint::On(CX, CY, 522.0f, 222.0f + 96.0f * float(Rpm / Max));
			P.Text(Whole(Rpm / 1000.0), At.X, At.Y + 7.0f, 17.0f, Rpm >= St.Redline ? Red : Dim, Align::Centre, true);
		}
	}
	P.Text(Whole(St.Speed), 388.0f, 318.0f, 168.0f, Ink, Align::Centre, true);
	P.Text(SpeedUnit(St), 388.0f, 350.0f, 22.0f, Dim, Align::Centre);
	P.RoundRect(596.0f, 216.0f, 112.0f, 124.0f, 10.0f, PanelBg);
	P.Text(GearText(St), 652.0f, 316.0f, 104.0f, GearColour(St), Align::Centre, true);
	P.Rect(60.0f, 300.0f, 150.0f, 40.0f, Accent);
	P.Text(St.Mode, 135.0f, 329.0f, 24.0f, Dark, Align::Centre, true);
	if (St.Nav) TurnBox(P, St, 40.0f, 216.0f, 216.0f, 64.0f, false);
	// Along the foot: range, trip, economy, and the aids.
	const std::vector<TripRow> Rows = TripRows(St);
	float X = 40.0f;
	for (const TripRow& Tr : Rows)
	{
		P.Text(Tr.Label, X, 392.0f, 15.0f, Dim);
		P.Text(Tr.Value, X, 422.0f, 26.0f, Ink, Align::Left, true);
		P.Text(Tr.Unit, X + 62.0f, 422.0f, 15.0f, Dim);
		X += 128.0f;
	}
	AidRow(P, St, 616.0f, 384.0f, 78.0f, 40.0f);
	if (St.Nav && St.Limit > 0) LimitSign(P, St, 752.0f, 336.0f, 26.0f);
}

void RideClassic(Paint& P, const State& St)
{
	ClassicDial(P, 208.0f, 246.0f, 168.0f, St.Speed, St.SpeedMax, St.SpeedStep, 0.0, SpeedUnit(St), false);
	if (St.HasRevs) ClassicDial(P, 592.0f, 246.0f, 168.0f, St.Rpm, St.RevMax, St.RevStep, St.Redline, "rpm x1000", true);
	// A window in each face: the trip in the speedo's, the gear in the rev counter's.
	P.RoundRect(150.0f, 318.0f, 116.0f, 34.0f, 4.0f, Rgba(0.0f, 0.0f, 0.0f));
	P.Text(Distance(St, St.TripKm, 1), 240.0f, 344.0f, 24.0f, Ink, Align::Right, true);
	P.Text(DistanceUnit(St), 246.0f, 344.0f, 13.0f, Dim);
	const float GX = St.HasRevs ? 592.0f : 592.0f;
	P.RoundRect(GX - 36.0f, 306.0f, 72.0f, 62.0f, 4.0f, Rgba(0.0f, 0.0f, 0.0f));
	P.Text(GearText(St), GX, 356.0f, 54.0f, GearColour(St), Align::Centre, true);
	// Between the faces: the mode, and the next turn or the range.
	P.Text(St.Mode, 400.0f, 82.0f, 20.0f, Accent, Align::Centre, true);
	if (St.Nav && !St.NavOff)
	{
		TurnArrow(P, 376.0f, 372.0f, 48.0f, St.NavTurn, Ink);
		P.Text(St.NavDistance, 400.0f, 438.0f, 18.0f, Ink, Align::Centre, true);
	}
	else
	{
		P.Text("Range", 400.0f, 404.0f, 14.0f, Dim, Align::Centre);
		P.Text(Distance(St, St.RangeKm, 0) + " " + DistanceUnit(St), 400.0f, 428.0f, 20.0f, Ink, Align::Centre, true);
	}
}

void RideTrack(Paint& P, const State& St)
{
	// Shift lights: twelve, lighting from 60% of the red line, all blue at it.
	const double From = St.Redline * 0.6, To = St.Redline * 0.98;
	const int Lit = St.HasShiftLight ? int(std::floor(Clamp01((St.Rpm - From) / std::max(To - From, 1.0)) * 12.0f + 0.001f)) : 0;
	const bool Flash = St.HasShiftLight && St.Rpm >= To;
	for (int I = 0; I < 12; ++I)
	{
		const Rgba C = I < 4 ? Green : I < 8 ? Amber : Red;
		const bool On = Flash ? BlinkOn(St, 6.0) : I < Lit;
		P.Disc(400.0f + (float(I) - 5.5f) * 46.0f, 76.0f, 15.0f, On ? (Flash ? Blue : C) : C.Mix(Bg, 0.86f));
	}
	if (St.HasRevs)
	{
		const float Frac = Clamp01(St.Rpm / std::max(St.RevMax, 1000.0));
		P.Rect(130.0f, 104.0f, 540.0f, 8.0f, Faint.Fade(0.6f));
		P.Rect(130.0f, 104.0f, 540.0f * Frac, 8.0f, RevColour(St, St.Rpm));
		P.Text(Whole(std::floor(St.Rpm / 50.0 + 0.5) * 50.0), 690.0f, 178.0f, 44.0f, Ink, Align::Right, true);
		P.Text("rpm", 698.0f, 178.0f, 18.0f, Dim);
	}
	P.Text(GearText(St), 400.0f, 362.0f, 250.0f, GearColour(St), Align::Centre, true);
	P.Text(Whole(St.Speed), 190.0f, 250.0f, 96.0f, Ink, Align::Right, true);
	P.Text(SpeedUnit(St), 198.0f, 250.0f, 20.0f, Dim);
	P.Text("TOP", 60.0f, 300.0f, 16.0f, Dim);
	P.Text(Whole(St.Metric ? St.TopKmh : St.TopKmh / 1.609344), 190.0f, 304.0f, 30.0f, Ink, Align::Right, true);
	P.Text("TRIP", 60.0f, 340.0f, 16.0f, Dim);
	P.Text(Distance(St, St.TripKm, 1), 190.0f, 344.0f, 30.0f, Ink, Align::Right, true);
	P.Rect(600.0f, 222.0f, 150.0f, 38.0f, Accent);
	P.Text(St.Mode, 675.0f, 250.0f, 23.0f, Dark, Align::Centre, true);
	P.Text("Range", 600.0f, 300.0f, 16.0f, Dim);
	P.Text(Distance(St, St.RangeKm, 0) + " " + DistanceUnit(St), 750.0f, 304.0f, 26.0f, Ink, Align::Right, true);
	if (St.Nav && !St.NavOff)
	{
		TurnArrow(P, 600.0f, 316.0f, 40.0f, St.NavTurn, Ink);
		P.Text(St.NavDistance, 750.0f, 346.0f, 24.0f, Ink, Align::Right, true);
	}
	AidRow(P, St, 400.0f, 388.0f, 96.0f, 40.0f);
}

void RideWide(Paint& P, const State& St)
{
	Chrome(P, St, 800.0f, 480.0f);
	switch (St.Look)
	{
	case Design::Sweep: RideSweep(P, St); break;
	case Design::Classic: RideClassic(P, St); break;
	case Design::Track: RideTrack(P, St); break;
	default: RideDial(P, St); break;
	}
}

// ---- The map view.

// The map with what lies over it: the turn banner, the limit, how far and when, the zoom marks.
void MapPanel(Paint& P, const State& St, const MapScene* Map, float X, float Y, float W, float H, bool Banner)
{
	if (!Map || !Map->Data)
	{
		P.Rect(X, Y, W, H, PanelBg);
		P.Text("No map here", X + W * 0.5f, Y + H * 0.5f, 22.0f, Dim, Align::Centre, true);
		P.Text("The sat nav has no roads for this place yet", X + W * 0.5f, Y + H * 0.5f + 28.0f, 15.0f, Faint, Align::Centre);
		return;
	}
	DrawMap(P, X, Y, W, H, *Map, 0.68f);
	const float U = std::min(W, H);
	if (Banner && St.Nav) TurnBox(P, St, X + 10.0f, Y + 10.0f, std::min(W - 60.0f, 300.0f), std::min(std::max(52.0f, U * 0.19f), 70.0f), true);
	if (St.Limit > 0) LimitSign(P, St, X + W - U * 0.11f - 8.0f, Y + H - U * 0.11f - 8.0f, U * 0.10f);
	if (St.Nav && !St.NavLeft.empty())
	{
		const float BH = std::max(26.0f, U * 0.085f);
		const std::string Line = St.NavEta.empty() ? St.NavLeft : St.NavLeft + "  \xC2\xB7  " + St.NavEta;
		P.RoundRect(X + 10.0f, Y + H - BH - 10.0f, BH * 6.4f, BH, 6.0f, Rgba(0.04f, 0.05f, 0.06f, 0.88f));
		P.Text(Line, X + 22.0f, Y + H - 10.0f - BH * 0.28f, BH * 0.58f, Ink, Align::Left, true);
	}
	// North: a small needle, top right.
	{
		const float CX = X + W - 26.0f, CY = Y + 26.0f, R = 16.0f;
		const float A = Map->NorthUp ? 0.0f : float(-Map->Heading);
		const float C = std::cos(A), S = std::sin(A);
		auto Rot = [&](float UU, float VV) { return P2(CX + UU * C - VV * S, CY + UU * S + VV * C); };
		P.Disc(CX, CY, R, Rgba(0.04f, 0.05f, 0.06f, 0.82f));
		P.Fan({Rot(0.0f, -R * 0.78f), Rot(R * 0.3f, 0.0f), Rot(-R * 0.3f, 0.0f)}, Red);
		P.Fan({Rot(0.0f, R * 0.78f), Rot(-R * 0.3f, 0.0f), Rot(R * 0.3f, 0.0f)}, Ink);
	}
}

void MapWide(Paint& P, const State& St, const MapScene* Map, float W)
{
	// A strip over the map: petrol and mode, speed and gear, the clock and the revs.
	FuelBar(P, St, 16.0f, 8.0f, 150.0f, 22.0f);
	P.Frame(16.0f, 38.0f, 118.0f, 28.0f, 2.0f, Ink);
	P.Text(St.Mode, 75.0f, 59.0f, 18.0f, Ink, Align::Centre, true);
	const float Mid = W * 0.5f;
	P.Text(Whole(St.Speed), Mid - 40.0f, 52.0f, 54.0f, Ink, Align::Right, true);
	P.Text(SpeedUnit(St), Mid - 40.0f, 68.0f, 13.0f, Dim, Align::Right);
	P.Text(GearText(St), Mid + 50.0f, 52.0f, 54.0f, GearColour(St), Align::Centre, true);
	P.Text("Gear", Mid + 50.0f, 68.0f, 13.0f, Dim, Align::Centre);
	TellTales(P, St, Mid + 190.0f, 22.0f, 15.0f);
	P.Text(Clock(St), W - 16.0f, 30.0f, 24.0f, Ink, Align::Right, true);
	if (St.HasRevs)
	{
		P.Text(Whole(std::floor(St.Rpm / 50.0 + 0.5) * 50.0), W - 54.0f, 64.0f, 26.0f, Ink, Align::Right, true);
		P.Text("rpm", W - 16.0f, 64.0f, 15.0f, Dim, Align::Right);
	}
	MapPanel(P, St, Map, 14.0f, 76.0f, W - 28.0f, 360.0f, true);
	const float Base = 480.0f - 12.0f;
	P.Text("Air", 16.0f, Base, 18.0f, Dim);
	P.Text(Whole(St.AirC) + Deg + "C", 54.0f, Base, 22.0f, Ink, Align::Left, true);
	if (!St.Notice.empty()) P.Text(St.Notice, Mid, Base, 20.0f, Amber, Align::Centre, true);
	else if (!St.NavRoad.empty()) P.Text(St.NavRoad, Mid, Base, 19.0f, Ink, Align::Centre);
	P.Text(St.Hot ? "HOT" : St.EngineC < 55.0 ? "Low" : "OK", W - 16.0f, Base, 22.0f, St.Hot ? Red : Ink, Align::Right, true);
	P.Text("Engine", W - 70.0f, Base, 18.0f, Dim, Align::Right);
}

// ---- The mode picker and the settings.

void Pips(Paint& P, float X, float Y, float W, float H, int Level, int Levels, const Rgba& On)
{
	if (Levels <= 0 || Level < 0) return;
	const float Gap = 4.0f, Each = (W - Gap * float(Levels - 1)) / float(Levels);
	for (int I = 0; I < Levels; ++I) P.Rect(X + float(I) * (Each + Gap), Y, Each, H, I < Level ? On : Faint.Fade(0.7f));
}

void ModesWide(Paint& P, const State& St)
{
	P.Text("RIDE MODE", 28.0f, 40.0f, 18.0f, Dim, Align::Left, true);
	// The modes on a curve down the left, the one the bar's on in the middle.
	P.Arc(-250.0f, 250.0f, 596.0f, 598.0f, -24.0f, 24.0f, Ink);
	const int N = int(St.Modes.size());
	for (int I = 0; I < N; ++I)
	{
		const int Off = I - St.ModeCursor;
		if (Off < -2 || Off > 2) continue;
		const float Y = 250.0f + float(Off) * 62.0f;
		const float X = 46.0f - float(Off * Off) * 9.0f;
		const bool Here = Off == 0;
		const float Px = Here ? 34.0f : Off == 1 || Off == -1 ? 26.0f : 19.0f;
		if (Here)
		{
			P.Rect(X - 12.0f, Y - 30.0f, 262.0f, 52.0f, Ink);
			P.Text(St.Modes[size_t(I)], X, Y + 8.0f, Px, Dark, Align::Left, true);
		}
		else P.Text(St.Modes[size_t(I)], X, Y + 8.0f, Px, Off == 1 || Off == -1 ? Ink : Dim, Align::Left, true);
		if (I == St.ModeNow) P.Disc(X - 26.0f, Y - 4.0f, 6.0f, Green);
	}
	// What the mode holds.
	float Y = 250.0f - float(St.ModeRows.size()) * 19.0f + 26.0f;
	for (const Row& R : St.ModeRows)
	{
		P.Text(R.Label, 372.0f, Y, 22.0f, Dim);
		P.Text(R.Value, 566.0f, Y, 22.0f, Ink, Align::Left, true);
		Y += 38.0f;
	}
	P.Text(Whole(St.Speed), 742.0f, 232.0f, 56.0f, Ink, Align::Centre, true);
	P.Text(SpeedUnit(St), 742.0f, 252.0f, 14.0f, Dim, Align::Centre);
	P.Text(GearText(St), 742.0f, 336.0f, 56.0f, GearColour(St), Align::Centre, true);
	P.Text("Gear", 742.0f, 356.0f, 14.0f, Dim, Align::Centre);
	const std::string Foot = !St.Notice.empty() ? St.Notice : std::string("Up / down: choose     Right: use this mode");
	P.Text(Foot, 400.0f, 458.0f, 18.0f, St.Notice.empty() ? Dim : Amber, Align::Centre, !St.Notice.empty());
}

void SettingsWide(Paint& P, const State& St)
{
	P.Text("SETTINGS", 28.0f, 40.0f, 18.0f, Dim, Align::Left, true);
	P.Text(St.Mode, 772.0f, 40.0f, 18.0f, Accent, Align::Right, true);
	const int N = int(St.Settings.size());
	// Seven rows show, the bar kept in the window.
	const int First = std::max(0, std::min(St.SetCursor - 3, N - 7));
	for (int I = First; I < N && I < First + 7; ++I)
	{
		const Row& R = St.Settings[size_t(I)];
		const float Y = 66.0f + float(I - First) * 50.0f;
		const bool Here = I == St.SetCursor;
		if (Here) P.Rect(20.0f, Y, 430.0f, 44.0f, St.SetEditing ? Accent : Ink);
		const Rgba C = Here ? Dark : R.Locked ? Faint : Ink;
		P.Text(R.Label, 34.0f, Y + 31.0f, 23.0f, C, Align::Left, Here);
		P.Text(R.Value, 436.0f, Y + 31.0f, 23.0f, Here ? Dark : R.Locked ? Faint : Dim, Align::Right, true);
	}
	if (N > 7)
	{
		// (how far down the list the window is)
		P.Rect(458.0f, 66.0f, 3.0f, 344.0f, Faint.Fade(0.6f));
		P.Rect(458.0f, 66.0f + 344.0f * float(First) / float(N), 3.0f, 344.0f * 7.0f / float(N), Dim);
	}
	if (St.SetCursor >= 0 && St.SetCursor < N)
	{
		const Row& R = St.Settings[size_t(St.SetCursor)];
		P.Text(R.Label, 630.0f, 150.0f, 24.0f, Dim, Align::Centre);
		P.Text(R.Value, 630.0f, 228.0f, 58.0f, R.Locked ? Faint : Ink, Align::Centre, true);
		if (R.Levels > 0) Pips(P, 530.0f, 256.0f, 200.0f, 12.0f, R.Level, R.Levels, St.SetEditing ? Accent : Ink);
		if (St.SetEditing)
		{
			P.Fan({P2(630.0f, 96.0f), P2(646.0f, 116.0f), P2(614.0f, 116.0f)}, Accent);
			P.Fan({P2(630.0f, 306.0f), P2(614.0f, 286.0f), P2(646.0f, 286.0f)}, Accent);
		}
		// The help, broken at about 34 letters a line.
		std::string Left = St.SetHelp;
		float Y = 340.0f;
		while (!Left.empty() && Y < 420.0f)
		{
			size_t Cut = Left.size();
			if (Cut > 36)
			{
				Cut = Left.rfind(' ', 36);
				if (Cut == std::string::npos || Cut == 0) Cut = 36;
			}
			P.Text(Left.substr(0, Cut), 630.0f, Y, 16.0f, Dim, Align::Centre);
			Left = Cut >= Left.size() ? std::string() : Left.substr(Cut + 1);
			Y += 22.0f;
		}
	}
	const std::string Foot = !St.Notice.empty() ? St.Notice
		: St.SetEditing ? std::string("Up / down: change     Left: done") : std::string("Up / down: choose     Right: change");
	P.Text(Foot, 400.0f, 458.0f, 18.0f, St.Notice.empty() ? Dim : Amber, Align::Centre, !St.Notice.empty());
}

// ---- The widescreen's side panel on the ride view: the trip computer and the tyres.

void InfoPanel(Paint& P, const State& St)
{
	P.Rect(0.0f, 20.0f, 1.5f, 440.0f, Faint);
	auto Card = [&](float Y, const char* Title) {
		P.Text(Title, 28.0f, Y + 22.0f, 16.0f, Accent, Align::Left, true);
		P.Rect(28.0f, Y + 30.0f, 424.0f, 1.0f, Faint);
	};
	Card(18.0f, "TRIP");
	auto Pair = [&](float X, float Y, const std::string& Label, const std::string& Value, const std::string& Unit) {
		P.Text(Label, X, Y, 15.0f, Dim);
		P.Text(Value, X, Y + 34.0f, 32.0f, Ink, Align::Left, true);
		P.Text(Unit, X + 96.0f, Y + 34.0f, 15.0f, Dim);
	};
	Pair(28.0f, 76.0f, "Trip", Distance(St, St.TripKm, 1), DistanceUnit(St));
	Pair(180.0f, 76.0f, "Range", Distance(St, St.RangeKm, 0), DistanceUnit(St));
	Pair(332.0f, 76.0f, "Economy", Economy(St), EconomyUnit(St));
	Card(140.0f, "TYRES");
	Pair(28.0f, 198.0f, "Front", Pressure(St, St.TyreBar[0]), PressureUnit(St));
	Pair(180.0f, 198.0f, "Rear", Pressure(St, St.TyreBar[1]), PressureUnit(St));
	Pair(332.0f, 198.0f, "Temperature", Whole(St.TyreC[0]) + "/" + Whole(St.TyreC[1]), std::string(Deg) + "C");
	Card(262.0f, St.Nav ? "NEXT" : "BIKE");
	if (St.Nav) TurnBox(P, St, 28.0f, 308.0f, 424.0f, 84.0f, false);
	else
	{
		Pair(28.0f, 320.0f, "Engine", Whole(St.EngineC), std::string(Deg) + "C");
		Pair(180.0f, 320.0f, "Top speed", Whole(St.Metric ? St.TopKmh : St.TopKmh / 1.609344), SpeedUnit(St));
		Pair(332.0f, 320.0f, "Total", Distance(St, St.TotalKm, 0), DistanceUnit(St));
	}
	if (!St.NavPlace.empty()) P.Text(St.NavPlace, 28.0f, 440.0f, 17.0f, Dim);
}

// ---- The round screen (480 across).

void RideRound(Paint& P, const State& St)
{
	const float C = 240.0f, R = 224.0f;
	const float A0 = 135.0f, Sweep = 270.0f;
	if (St.Look == Design::Classic)
	{
		// One clock: the speed on a needle, the gear and the revs in a window.
		ClassicDial(P, C, C, 232.0f, St.Speed, St.SpeedMax, St.SpeedStep, 0.0, SpeedUnit(St), false);
		P.RoundRect(C - 74.0f, 300.0f, 148.0f, 74.0f, 6.0f, Rgba(0.0f, 0.0f, 0.0f));
		P.Text(GearText(St), C - 40.0f, 358.0f, 52.0f, GearColour(St), Align::Centre, true);
		P.Text(St.Mode, C + 28.0f, 328.0f, 13.0f, Accent, Align::Centre, true);
		if (St.HasRevs)
		{
			const float Frac = Clamp01(St.Rpm / std::max(St.RevMax, 1000.0));
			P.Rect(C - 8.0f, 342.0f, 72.0f, 8.0f, Faint);
			P.Rect(C - 8.0f, 342.0f, 72.0f * Frac, 8.0f, RevColour(St, St.Rpm));
			P.Text(Whole(std::floor(St.Rpm / 100.0 + 0.5) * 100.0), C + 28.0f, 368.0f, 13.0f, Dim, Align::Centre);
		}
		TellTales(P, St, C, 404.0f, 13.0f);
		return;
	}
	if (St.Look == Design::Track)
	{
		const double From = St.Redline * 0.6, To = St.Redline * 0.98;
		const int Lit = St.HasShiftLight ? int(std::floor(Clamp01((St.Rpm - From) / std::max(To - From, 1.0)) * 11.0f + 0.001f)) : 0;
		const bool Flash = St.HasShiftLight && St.Rpm >= To;
		for (int I = 0; I < 11; ++I)
		{
			const P2 At = Paint::On(C, C, 204.0f, 195.0f + 15.0f * float(I));
			const Rgba Col = I < 4 ? Green : I < 8 ? Amber : Red;
			const bool On = Flash ? BlinkOn(St, 6.0) : I < Lit;
			P.Disc(At.X, At.Y, 13.0f, On ? (Flash ? Blue : Col) : Col.Mix(Bg, 0.86f));
		}
		P.Text(GearText(St), C, 318.0f, 210.0f, GearColour(St), Align::Centre, true);
		P.Text(Whole(St.Speed), C - 6.0f, 384.0f, 50.0f, Ink, Align::Right, true);
		P.Text(SpeedUnit(St), C + 2.0f, 384.0f, 16.0f, Dim);
		P.Text(St.Mode, C, 420.0f, 15.0f, Accent, Align::Centre, true);
		return;
	}
	// Dial and Sweep: the revs round the rim, the speed in the middle, the gear in the gap at the foot.
	if (St.HasRevs)
	{
		if (St.Look == Design::Sweep) RevBlocks(P, St, C, C, R - 22.0f, R, A0, Sweep, 36);
		else RevRing(P, St, C, C, R, A0, Sweep, true, true);
	}
	TellTales(P, St, C, 112.0f, 13.0f);
	P.Text(St.Mode, C, 150.0f, 17.0f, Accent, Align::Centre, true);
	P.Text(Whole(St.Speed), C, 268.0f, 112.0f, Ink, Align::Centre, true);
	P.Text(SpeedUnit(St), C, 292.0f, 16.0f, Dim, Align::Centre);
	if (St.Nav && !St.NavOff)
	{
		TurnArrow(P, C - 62.0f, 300.0f, 34.0f, St.NavTurn, Ink);
		P.Text(St.NavDistance, C - 22.0f, 326.0f, 20.0f, Ink, Align::Left, true);
	}
	else
	{
		// The petrol as a short bar.
		const float W = 110.0f;
		P.Rect(C - W * 0.5f, 312.0f, W, 7.0f, Faint);
		P.Rect(C - W * 0.5f, 312.0f, W * Clamp01(St.Fuel), 7.0f, St.Fuel < 0.15 ? Red : Ink);
		P.Text(Distance(St, St.RangeKm, 0) + " " + DistanceUnit(St), C, 338.0f, 14.0f, Dim, Align::Centre);
	}
	P.Ring(C, 404.0f, 38.0f, 40.0f, Dim);
	P.Disc(C, 404.0f, 38.0f, Bg);
	P.Text(GearText(St), C, 424.0f, 56.0f, GearColour(St), Align::Centre, true);
}

void ListRound(Paint& P, const State& St, bool ModesView)
{
	const float C = 240.0f;
	P.Text(ModesView ? "RIDE MODE" : "SETTINGS", C, 74.0f, 15.0f, Dim, Align::Centre, true);
	const int N = ModesView ? int(St.Modes.size()) : int(St.Settings.size());
	const int Cursor = ModesView ? St.ModeCursor : St.SetCursor;
	for (int I = 0; I < N; ++I)
	{
		const int Off = I - Cursor;
		if (Off < -2 || Off > 2) continue;
		const float Y = 236.0f + float(Off) * 56.0f;
		const bool Here = Off == 0;
		const float Px = Here ? 28.0f : 20.0f;
		const float Half = 190.0f - float(Off * Off) * 22.0f;
		if (Here) P.Rect(C - Half, Y - 30.0f, Half * 2.0f, 46.0f, !ModesView && St.SetEditing ? Accent : Ink);
		if (ModesView)
		{
			P.Text(St.Modes[size_t(I)], C, Y + 2.0f, Px, Here ? Dark : Dim, Align::Centre, true);
			if (I == St.ModeNow) P.Disc(C - Half + 16.0f, Y - 7.0f, 5.0f, Green);
		}
		else
		{
			const Row& R = St.Settings[size_t(I)];
			P.Text(R.Label, C - Half + 14.0f, Y + 2.0f, Px * 0.8f, Here ? Dark : R.Locked ? Faint : Dim, Align::Left, Here);
			P.Text(R.Value, C + Half - 14.0f, Y + 2.0f, Px * 0.8f, Here ? Dark : R.Locked ? Faint : Ink, Align::Right, true);
		}
	}
	if (!St.Notice.empty()) P.Text(St.Notice, C, 420.0f, 14.0f, Amber, Align::Centre, true);
}

// Black outside the round screen's glass.
void RoundMask(Paint& P)
{
	P.Arc(240.0f, 240.0f, 238.0f, 360.0f, 0.0f, 360.0f, Rgba(0.0f, 0.0f, 0.0f));
}

// ---- The phone: the map, upright.

void PhoneScreen(Paint& P, const State& St, const MapScene* Map)
{
	const float W = 360.0f, H = 640.0f;
	if (!Map || !Map->Data)
	{
		P.Rect(0.0f, 0.0f, W, H, PanelBg);
		P.Text("No map here", W * 0.5f, H * 0.5f, 22.0f, Dim, Align::Centre, true);
		return;
	}
	DrawMap(P, 0.0f, 0.0f, W, H, *Map, 0.70f);
	if (St.Nav)
	{
		// The turn across the top, in the green of a phone's nav app.
		P.RoundRect(10.0f, 10.0f, W - 20.0f, 92.0f, 12.0f, St.NavOff ? Rgba(0.55f, 0.20f, 0.12f) : Rgba(0.06f, 0.45f, 0.30f));
		if (St.NavOff) P.Text("Off route", 28.0f, 66.0f, 30.0f, Ink, Align::Left, true);
		else
		{
			TurnArrow(P, 20.0f, 20.0f, 72.0f, St.NavTurn, Ink);
			P.Text(St.NavDistance, 104.0f, 56.0f, 34.0f, Ink, Align::Left, true);
			P.Text(St.NavOnto.substr(0, 26), 104.0f, 84.0f, 18.0f, Ink.Fade(0.85f));
		}
	}
	// The speed in a chip, the limit beside it, and how far and when along the foot.
	P.RoundRect(12.0f, H - 150.0f, 84.0f, 70.0f, 10.0f, Rgba(1.0f, 1.0f, 1.0f, 0.94f));
	P.Text(Whole(St.Speed), 54.0f, H - 104.0f, 38.0f, Rgba(0.1f, 0.1f, 0.12f), Align::Centre, true);
	P.Text(SpeedUnit(St), 54.0f, H - 88.0f, 12.0f, Rgba(0.4f, 0.4f, 0.42f), Align::Centre);
	if (St.Limit > 0) LimitSign(P, St, 136.0f, H - 115.0f, 30.0f);
	P.Rect(0.0f, H - 64.0f, W, 64.0f, Rgba(1.0f, 1.0f, 1.0f, 0.96f));
	if (St.Nav)
	{
		P.Text(St.NavEta, 18.0f, H - 24.0f, 26.0f, Rgba(0.08f, 0.45f, 0.25f), Align::Left, true);
		P.Text(St.NavLeft, W - 18.0f, H - 24.0f, 20.0f, Rgba(0.3f, 0.3f, 0.32f), Align::Right);
	}
	else P.Text(St.NavRoad.empty() ? St.NavPlace : St.NavRoad, 18.0f, H - 26.0f, 20.0f, Rgba(0.2f, 0.2f, 0.22f));
}
} // namespace

void SizeOf(Shape S, float& W, float& H)
{
	switch (S)
	{
	case Shape::Ultra: W = 1280.0f; H = 480.0f; break;
	case Shape::Round: W = 480.0f; H = 480.0f; break;
	case Shape::Phone: W = 360.0f; H = 640.0f; break;
	default: W = 800.0f; H = 480.0f; break;
	}
}

const char* DesignName(Design D)
{
	switch (D)
	{
	case Design::Sweep: return "Sweep";
	case Design::Classic: return "Classic";
	case Design::Track: return "Track";
	default: return "Dial";
	}
}

std::string Whole(double V)
{
	char B[32];
	std::snprintf(B, sizeof(B), "%ld", std::lround(V));
	return B;
}

std::string Fixed(double V, int Decimals)
{
	char B[32];
	std::snprintf(B, sizeof(B), "%.*f", std::max(0, Decimals), V);
	return B;
}

void Draw(Paint& P, Shape S, const State& St, const MapScene* Map)
{
	SizeOf(S, P.W, P.H);
	P.Origin(0.0f, 0.0f);
	P.NoClip();
	P.Rect(0.0f, 0.0f, P.W, P.H, Bg);
	if (!St.EngineOn && St.Now == View::Ride && S != Shape::Phone)
	{
		// Ignition off: the screen sleeps, but for the clock.
		P.Text(Clock(St), P.W * 0.5f, P.H * 0.5f + 14.0f, 44.0f, Faint, Align::Centre, true);
		if (S == Shape::Round) RoundMask(P);
		return;
	}
	if (S == Shape::Phone)
	{
		PhoneScreen(P, St, Map);
		return;
	}
	if (S == Shape::Round)
	{
		switch (St.Now)
		{
		case View::Map:
		case View::Split:
			if (Map && Map->Data) DrawMap(P, 0.0f, 0.0f, 480.0f, 480.0f, *Map, 0.64f);
			else MapPanel(P, St, Map, 0.0f, 0.0f, 480.0f, 480.0f, false);
			if (St.Limit > 0) LimitSign(P, St, 408.0f, 240.0f, 30.0f);
			if (St.Nav && !St.NavOff)
			{
				P.RoundRect(150.0f, 28.0f, 180.0f, 50.0f, 10.0f, Rgba(0.04f, 0.05f, 0.06f, 0.92f));
				TurnArrow(P, 158.0f, 32.0f, 42.0f, St.NavTurn, Ink);
				P.Text(St.NavDistance, 208.0f, 62.0f, 24.0f, Ink, Align::Left, true);
			}
			P.RoundRect(176.0f, 396.0f, 128.0f, 48.0f, 10.0f, Rgba(0.04f, 0.05f, 0.06f, 0.92f));
			P.Text(Whole(St.Speed), 232.0f, 432.0f, 34.0f, Ink, Align::Right, true);
			P.Text(GearText(St), 276.0f, 432.0f, 34.0f, GearColour(St), Align::Centre, true);
			break;
		case View::Modes: ListRound(P, St, true); break;
		case View::Settings: ListRound(P, St, false); break;
		default: RideRound(P, St); break;
		}
		RoundMask(P);
		return;
	}
	if (S == Shape::Ultra)
	{
		switch (St.Now)
		{
		case View::Map: MapWide(P, St, Map, 1280.0f); break;
		case View::Split:
			RideWide(P, St);
			MapPanel(P, St, Map, 812.0f, 12.0f, 456.0f, 456.0f, true);
			break;
		case View::Modes:
			P.Origin(240.0f, 0.0f);
			ModesWide(P, St);
			break;
		case View::Settings:
			P.Origin(240.0f, 0.0f);
			SettingsWide(P, St);
			break;
		default:
			RideWide(P, St);
			P.Origin(800.0f, 0.0f);
			InfoPanel(P, St);
			break;
		}
		P.Origin(0.0f, 0.0f);
		return;
	}
	switch (St.Now)
	{
	case View::Map:
	case View::Split: MapWide(P, St, Map, 800.0f); break;
	case View::Modes: ModesWide(P, St); break;
	case View::Settings: SettingsWide(P, St); break;
	default: RideWide(P, St); break;
	}
}

void DrawMapTile(Paint& P, const State& St, const MapScene* Map)
{
	P.Origin(0.0f, 0.0f);
	P.NoClip();
	MapPanel(P, St, Map, 0.0f, 0.0f, P.W, P.H, true);
	if (!St.NavRoad.empty() && !St.Nav)
	{
		P.RoundRect(10.0f, P.H - 40.0f, std::min(P.W - 20.0f, 300.0f), 30.0f, 6.0f, Rgba(0.04f, 0.05f, 0.06f, 0.88f));
		P.Text(St.NavRoad, 22.0f, P.H - 19.0f, 17.0f, Ink, Align::Left, true);
	}
}
} // namespace TorqueTft
