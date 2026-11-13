#include "WorldRoadLine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace TorqueWorld
{
namespace RoadLine
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Inf = std::numeric_limits<double>::infinity();

struct ClassRow
{
	const char* Name;
	double Width, Fair, MainCost, Speed;
};
// (route_planner.gd's WIDTH, FAIR and CLASS_COST["main"])
constexpr ClassRow ClassRows[] = {
	{"motorway", 7.4, 18.0, 0.7, 110.0}, {"trunk", 7.4, 16.0, 0.75, 100.0}, {"primary", 7.4, 13.0, 0.8, 95.0}, {"secondary", 6.0, 11.0, 0.9, 80.0},
	{"tertiary", 6.0, 9.0, 1.0, 70.0}, {"unclassified", 4.6, 7.0, 1.15, 60.0}, {"residential", 4.6, 5.0, 1.3, 40.0}, {"living_street", 4.6, 4.0, 1.4, 30.0},
	{"track", 3.3, 6.0, 3.0, 30.0},
	// [OSM stage 4] (service roads, alleys, drives and car park aisles: narrow, slow, a last resort)
	{"service", 3.5, 3.0, 8.0, 25.0}};

const ClassRow* RowOf(const std::string& Class)
{
	for (const ClassRow& R : ClassRows)
		if (Class == R.Name) return &R;
	return nullptr;
}

using Doubles = std::vector<double>;

// One pass of a moving average over C in place, W[i] points either side of point i.
void BoxPass(Doubles& C, const std::vector<int>& W)
{
	const size_t N = C.size();
	if (N < 3) return;
	Doubles Sum(N + 1);
	Sum[0] = 0.0;
	for (size_t I = 0; I < N; ++I) Sum[I + 1] = Sum[I] + C[I];
	for (size_t I = 1; I + 1 < N; ++I)
	{
		const int K = W[I];
		if (K > 0) C[I] = (Sum[I + size_t(K) + 1] - Sum[I - size_t(K)]) / double(2 * K + 1);
	}
}

double AngleBetween(double Ax, double Az, double Bx, double Bz)
{
	const double La = std::sqrt(Ax * Ax + Az * Az), Lb = std::sqrt(Bx * Bx + Bz * Bz);
	return std::acos(std::fmin(1.0, std::fmax(-1.0, (Ax * Bx + Az * Bz) / std::fmax(La * Lb, 1e-9))));
}

// Rounds each corner the map drew (a point where the line turns by more than FilletTurn) into an
// arc, in place: as wide as the road's class would have (FilletRadius times its fairing, no tighter
// than 1.2 MinR), or as wide as the line either side leaves room for, its points 2 m apart.
void Fillet(Doubles& Rx, Doubles& Rz, Doubles& Fair, double MinR)
{
	const size_t N = Rx.size();
	if (N < 3) return;
	Doubles Ox{Rx[0]}, Oz{Rz[0]}, Of{Fair[0]};
	for (size_t I = 1; I + 1 < N; ++I)
	{
		const double Px = Rx[I], Pz = Rz[I];
		const double Dax = Px - Rx[I - 1], Daz = Pz - Rz[I - 1], Dbx = Rx[I + 1] - Px, Dbz = Rz[I + 1] - Pz;
		const double La = std::sqrt(Dax * Dax + Daz * Daz), Lb = std::sqrt(Dbx * Dbx + Dbz * Dbz);
		// (the signed angle from the way in to the way out)
		const double Turn = La > 0.01 && Lb > 0.01 ? std::atan2(Dax * Dbz - Daz * Dbx, Dax * Dbx + Daz * Dbz) : 0.0;
		if (std::fabs(Turn) < FilletTurn || std::fabs(Turn) > Pi - 0.2 || (Fair[I - 1] <= 0.0 && Fair[I] <= 0.0 && Fair[I + 1] <= 0.0))
		{
			Ox.push_back(Px);
			Oz.push_back(Pz);
			Of.push_back(Fair[I]);
			continue;
		}
		double R = std::fmax(MinR * 1.2, Fair[I] * FilletRadius);
		const double Half = std::tan(std::fabs(Turn) * 0.5);
		const double T = std::fmin(R * Half, std::fmin(La, Lb) * 0.5);
		R = T / Half;
		const double Ux = Dax / La, Uz = Daz / La;
		const double Sx = Px - Ux * T, Sz = Pz - Uz * T;
		// The arc's middle is R off the line to the side it turns to.
		const double Sign = Turn > 0.0 ? 1.0 : -1.0;
		const double Cx = Sx + (-Uz) * Sign * R, Cz = Sz + Ux * Sign * R;
		const double Fx = Sx - Cx, Fz = Sz - Cz;
		const int Steps = std::max(2, int(std::ceil(R * std::fabs(Turn) / 2.0)));
		for (int Q = 0; Q <= Steps; ++Q)
		{
			const double A = Turn * double(Q) / double(Steps);
			const double Co = std::cos(A), Si = std::sin(A);
			Ox.push_back(Cx + Fx * Co - Fz * Si);
			Oz.push_back(Cz + Fx * Si + Fz * Co);
			Of.push_back(Fair[I]);
		}
	}
	Ox.push_back(Rx[N - 1]);
	Oz.push_back(Rz[N - 1]);
	Of.push_back(Fair[N - 1]);
	Rx.swap(Ox);
	Rz.swap(Oz);
	Fair.swap(Of);
}

// The fairing of each of N points 2 m apart along the line through (Rx, Rz), from Fair at each of
// those points; at a change of road, the tighter of the two.
Doubles FairAlong(const Doubles& Rx, const Doubles& Rz, const Doubles& Fair, size_t N)
{
	Doubles Out(N, FairDefault);
	if (Fair.size() != Rx.size() || Rx.size() < 2) return Out;
	size_t J = 0;
	double Run = 0.0;
	for (size_t I = 0; I < N; ++I)
	{
		const double At = double(I) * 2.0;
		while (J + 2 < Rx.size())
		{
			const double Len = std::hypot(Rx[J + 1] - Rx[J], Rz[J + 1] - Rz[J]);
			if (!(Run + Len < At)) break;
			Run += Len;
			++J;
		}
		Out[I] = std::fmin(Fair[J], Fair[J + 1]);
	}
	return Out;
}

// Fairs the line through (Px, Pz), 2 m apart, in place: each point moved to a weighted average of
// those round it, near enough a Gaussian Fair[i] m wide. The line is faired whole at each of
// FairLevels (three passes of a moving average) and each point blended between the two levels
// round its own width; the width eases along the road where it changes. Cap, if given, is the most
// each point may be faired.
void FairLine(Doubles& Px, Doubles& Pz, const Doubles& Fair, const Doubles& Cap)
{
	const size_t N = Px.size();
	if (N < 5) return;
	Doubles Sig = Fair;
	std::vector<int> Ease(N);
	for (size_t I = 0; I < N; ++I) Ease[I] = int(std::min<size_t>(size_t(FairEase), std::min(I, N - 1 - I)));
	BoxPass(Sig, Ease);
	if (Cap.size() == N)
		for (size_t I = 0; I < N; ++I) Sig[I] = std::fmin(Sig[I], Cap[I]);
	double Hi = -Inf;
	for (double V : Sig) Hi = std::fmax(Hi, V);
	// (level 0 is the line as it is: a point faired less than the first width blends from there)
	Doubles Levels{0.0};
	for (double L : FairLevels)
	{
		if (Levels.back() >= Hi) break;
		Levels.push_back(L);
	}
	std::vector<Doubles> Lx, Lz;
	for (double L : Levels)
	{
		if (L <= 0.0)
		{
			Lx.push_back(Px);
			Lz.push_back(Pz);
			continue;
		}
		// (three passes of a box K points either side: sigma sqrt(K (K + 1)) points)
		const int K = std::max(int(std::round(std::sqrt(L * L / 4.0 + 0.25) - 0.5)), 0);
		std::vector<int> W(N);
		for (size_t I = 0; I < N; ++I) W[I] = int(std::min<size_t>(size_t(K), std::min(I, N - 1 - I)));
		Doubles Ax = Px, Az = Pz;
		for (int Pass = 0; Pass < 3; ++Pass)
		{
			BoxPass(Ax, W);
			BoxPass(Az, W);
		}
		Lx.push_back(std::move(Ax));
		Lz.push_back(std::move(Az));
	}
	for (size_t I = 1; I + 1 < N; ++I)
	{
		const double V = std::fmin(std::fmax(Sig[I], Levels.front()), Levels.back());
		size_t J = 0;
		while (J + 2 < Levels.size() && Levels[J + 1] < V) ++J;
		const double T = Levels.size() < 2 || Levels[J + 1] <= Levels[J] ? 0.0 : std::fmin(1.0, std::fmax(0.0, (V - Levels[J]) / (Levels[J + 1] - Levels[J])));
		const size_t J1 = std::min(J + 1, Levels.size() - 1);
		Px[I] = Lx[J][I] + (Lx[J1][I] - Lx[J][I]) * T;
		Pz[I] = Lz[J][I] + (Lz[J1][I] - Lz[J][I]) * T;
	}
}

// Fairs the line in place, and where that leaves a bend tighter than Limit (1 / radius) fairs it
// again from the line as it was, more widely there, until no bend is; FairRounds at most.
void FairToRadius(Doubles& Px, Doubles& Pz, const Doubles& Fair, double Limit, const Doubles& Cap)
{
	const size_t N = Px.size();
	const Doubles Ox = Px, Oz = Pz;
	Doubles Sig = Fair;
	for (int Round = 0; Round < FairRounds; ++Round)
	{
		Px = Ox;
		Pz = Oz;
		FairLine(Px, Pz, Sig, Cap);
		Doubles Widen(N, 0.0);
		bool Any = false;
		for (size_t I = 2; I + 2 < N; ++I)
		{
			if (Bend(Px, Pz, I) > Limit)
			{
				Any = true;
				// Widen it over the bend and as far again either side, in points 2 m apart.
				const size_t Reach = size_t(int(Sig[I] * 1.5) + 4);
				const size_t Lo = I > Reach ? I - Reach : 0, Hi = std::min(I + Reach, N - 1);
				for (size_t J = Lo; J <= Hi; ++J) Widen[J] = std::fmax(Widen[J], std::fmin(Sig[I] * FairWiden, FairMost));
			}
		}
		if (!Any) return;
		// Eased in and out, never narrower than it was.
		std::vector<int> W(N);
		for (size_t I = 0; I < N; ++I) W[I] = int(std::min<size_t>(6, std::min(I, N - 1 - I)));
		BoxPass(Widen, W);
		for (size_t I = 0; I < N; ++I) Sig[I] = std::fmax(Sig[I], Widen[I]);
	}
}

// Where the line goes out along a road and comes back the same way, a turning place is made: a
// half circle of Radius round the tip, the way back alongside the way out and easing back onto the
// road over the next 80 m. Turning is 1 for each point of a turning place and its ways in and out.
void OpenReversals(const Doubles& Px, const Doubles& Pz, double Radius, Doubles& Ox, Doubles& Oz, std::vector<char>& Turning)
{
	const int N = int(Px.size());
	Ox.clear();
	Oz.clear();
	Turning.clear();
	int I = 0;
	const int Lead = std::max(int(std::ceil(Radius / 2.0)), 2);
	const int EasePts = 40;
	while (I < N)
	{
		bool Tip = false;
		if (I >= 6 && I < N - 7)
		{
			const double V1x = Px[size_t(I)] - Px[size_t(I - 5)], V1z = Pz[size_t(I)] - Pz[size_t(I - 5)];
			const double V2x = Px[size_t(I + 5)] - Px[size_t(I)], V2z = Pz[size_t(I + 5)] - Pz[size_t(I)];
			const double Fold = std::hypot(Px[size_t(I + 5)] - Px[size_t(I - 5)], Pz[size_t(I + 5)] - Pz[size_t(I - 5)]);
			Tip = Fold < 4.0 && std::hypot(V1x, V1z) > 1.0 && std::hypot(V2x, V2z) > 1.0 && AngleBetween(V1x, V1z, V2x, V2z) > 2.5;
		}
		if (!Tip || int(Ox.size()) < Lead || I - Lead < 4)
		{
			Ox.push_back(Px[size_t(I)]);
			Oz.push_back(Pz[size_t(I)]);
			Turning.push_back(0);
			++I;
			continue;
		}
		// The way in ends Lead points before the tip (those were already written: take them back off).
		const int A = I - Lead;
		Ox.resize(Ox.size() - size_t(Lead));
		Oz.resize(Oz.size() - size_t(Lead));
		Turning.resize(Ox.size());
		for (size_t Q = Turning.size() > size_t(TurnMark) ? Turning.size() - size_t(TurnMark) : 0; Q < Turning.size(); ++Q) Turning[Q] = 1;
		double Dx = Px[size_t(A)] - Px[size_t(A - 4)], Dz = Pz[size_t(A)] - Pz[size_t(A - 4)];
		const double Dl = std::fmax(std::hypot(Dx, Dz), 1e-9);
		Dx /= Dl;
		Dz /= Dl;
		const double Rx = -Dz, Rz = Dx;
		const double Cx = Px[size_t(A)] + Rx * Radius, Cz = Pz[size_t(A)] + Rz * Radius;
		const double Fx = Px[size_t(A)] - Cx, Fz = Pz[size_t(A)] - Cz;
		for (int Q = 0; Q < 13; ++Q)
		{
			const double Ang = Pi * double(Q) / 12.0;
			const double Co = std::cos(Ang), Si = std::sin(Ang);
			Ox.push_back(Cx + Fx * Co - Fz * Si);
			Oz.push_back(Cz + Fx * Si + Fz * Co);
			Turning.push_back(1);
		}
		// The way back, alongside, easing onto the road.
		const int Back = I + Lead;
		const int Until = std::min(Back + EasePts + 1, N);
		for (int J = Back; J < Until; ++J)
		{
			double T = double(J - Back) / double(EasePts);
			T = T * T * (3.0 - 2.0 * T);
			Ox.push_back(Px[size_t(J)] + Rx * 2.0 * Radius * (1.0 - T));
			Oz.push_back(Pz[size_t(J)] + Rz * 2.0 * Radius * (1.0 - T));
			Turning.push_back(1);
		}
		I = Until;
	}
}

// Rounds of pulling the points of any bend tighter than Limit, and Reach * 3 points either side,
// towards the middle of their neighbours Reach points off, until no bend is; the ends stay put.
void Relax(Doubles& Px, Doubles& Pz, double Limit, int Reach)
{
	const int N = int(Px.size());
	const int M = Reach * 3;
	std::vector<int> Cand;
	for (int I = 2; I < N - 2; ++I) Cand.push_back(I);
	int Rounds = 0;
	while (N >= 7 && !Cand.empty() && Rounds < MaxRounds)
	{
		++Rounds;
		std::vector<int> Bad;
		for (int I : Cand)
			if (Bend(Px, Pz, size_t(I)) > Limit) Bad.push_back(I);
		if (Bad.empty()) break;
		std::vector<int> Idx;
		int NextFrom = Reach + 1;
		for (int B : Bad)
		{
			const int Lo = std::max(std::max(B - M, Reach + 1), NextFrom), Hi = std::min(B + M, N - Reach - 2);
			for (int I = Lo; I <= Hi; ++I) Idx.push_back(I);
			NextFrom = std::max(NextFrom, Hi + 1);
		}
		Doubles Nx(Idx.size()), Nz(Idx.size());
		for (size_t K = 0; K < Idx.size(); ++K)
		{
			const size_t I = size_t(Idx[K]), R = size_t(Reach);
			Nx[K] = Px[I] + ((Px[I - R] + Px[I + R]) / 2.0 - Px[I]) * 0.6;
			Nz[K] = Pz[I] + ((Pz[I - R] + Pz[I + R]) / 2.0 - Pz[I]) * 0.6;
		}
		for (size_t K = 0; K < Idx.size(); ++K)
		{
			Px[size_t(Idx[K])] = Nx[K];
			Pz[size_t(Idx[K])] = Nz[K];
		}
		// What may be bad next: the points near any that moved.
		Cand.clear();
		int From = 2;
		for (int I : Idx)
		{
			const int Lo = std::max(std::max(I - 2, 2), From), Hi = std::min(I + 2, N - 3);
			for (int J = Lo; J <= Hi; ++J) Cand.push_back(J);
			From = std::max(From, Hi + 1);
		}
	}
}
} // namespace

double WidthOf(const std::string& Class)
{
	const ClassRow* R = RowOf(Class);
	return R ? R->Width : 4.6;
}

double FairOf(const std::string& Class)
{
	const ClassRow* R = RowOf(Class);
	return R ? R->Fair : FairDefault;
}

double MainCostOf(const std::string& Class)
{
	const ClassRow* R = RowOf(Class);
	return R ? R->MainCost : 1.0;
}

double DesignSpeedOf(const std::string& Class)
{
	const ClassRow* R = RowOf(Class);
	return R ? R->Speed : 60.0;
}

std::vector<double> BankOf(const std::vector<P2>& Line, const std::vector<double>& Speed)
{
	const size_t N = Line.size();
	Doubles Out(N, 0.0);
	if (N < 5 || Speed.empty()) return Out;
	for (size_t I = 2; I + 2 < N; ++I)
	{
		const P2 A = Line[I - 2], B = Line[I], C = Line[I + 2];
		const double Ab = A.DistanceTo(B), Bc = B.DistanceTo(C), Ac = A.DistanceTo(C);
		// (+ turning from east towards south: to the right, with x east and z south)
		const double Cross = (B.X - A.X) * (C.Z - B.Z) - (B.Z - A.Z) * (C.X - B.X);
		const double Curve = 2.0 * Cross / std::fmax(Ab * Bc * Ac, 1e-9);   // 1 / radius, signed
		const double V = Speed.size() == N ? Speed[I] : Speed[0];
		Out[I] = std::fmin(MaxBank, std::fmax(-MaxBank, V * V * Curve / 282.8));
	}
	// Eased in and out: twice averaged over 6 knots either side (about 25 m of transition).
	std::vector<int> W(N);
	for (size_t I = 0; I < N; ++I) W[I] = int(std::min<size_t>(6, std::min(I, N - 1 - I)));
	BoxPass(Out, W);
	BoxPass(Out, W);
	Out.front() = 0.0;
	Out.back() = 0.0;
	return Out;
}

double CrossHeight(double Off, double Half, double Bank)
{
	const double Side = std::fmin(std::fmax(Off, -Half), Half);
	// Each half's own slope outwards: the crown's fall, turned up to the bank on the outside of a bend
	// (the fall taken out first) and steepened to it on the inside.
	const double Tilt = Side >= 0.0 ? Bank : -Bank;   // + this half is the bend's outside
	const double Share = std::fmin(std::fabs(Tilt) / Crown, 1.0);
	const double Slope = Tilt >= 0.0 ? -Crown + (Tilt + Crown) * Share : -std::fmax(Crown, -Tilt);
	return std::fabs(Side) * Slope;
}

void ResampleXz(const std::vector<double>& Xs, const std::vector<double>& Zs, double StepM, bool Exact, std::vector<double>& Ox, std::vector<double>& Oz, double& Total)
{
	Doubles KeepX, KeepZ, Cum;
	Total = 0.0;
	for (size_t I = 0; I < Xs.size(); ++I)
	{
		if (I > 0)
		{
			const double Seg = std::hypot(Xs[I] - KeepX.back(), Zs[I] - KeepZ.back());
			if (Seg <= 1e-9) continue;
			Total += Seg;
		}
		KeepX.push_back(Xs[I]);
		KeepZ.push_back(Zs[I]);
		Cum.push_back(Total);
	}
	Doubles SList;
	for (double S = 0.0; S < Total; S += StepM) SList.push_back(S);
	if (Exact)
	{
		if (!SList.empty() && Total - SList.back() < 1e-6) SList.pop_back();
	}
	else if (!SList.empty() && Total - SList.back() > StepM * 0.25) SList.push_back(Total);
	else if (!SList.empty()) SList.back() = Total;
	else SList.push_back(Total);
	Ox.assign(SList.size(), 0.0);
	Oz.assign(SList.size(), 0.0);
	if (Cum.empty()) { Ox.clear(); Oz.clear(); return; }
	size_t J = 0;
	for (size_t I = 0; I < SList.size(); ++I)
	{
		const double V = SList[I];
		while (J + 2 < Cum.size() && Cum[J + 1] < V) ++J;
		const double Span = J + 1 < Cum.size() ? Cum[J + 1] - Cum[J] : 0.0;
		const double T = Span <= 0.0 ? 0.0 : std::fmin(1.0, std::fmax(0.0, (V - Cum[J]) / Span));
		const size_t J2 = std::min(J + 1, Cum.size() - 1);
		Ox[I] = KeepX[J] + (KeepX[J2] - KeepX[J]) * T;
		Oz[I] = KeepZ[J] + (KeepZ[J2] - KeepZ[J]) * T;
	}
}

double Bend(const std::vector<double>& Px, const std::vector<double>& Pz, size_t I)
{
	const double Ax = Px[I - 2], Az = Pz[I - 2], Bx = Px[I], Bz = Pz[I], Cx = Px[I + 2], Cz = Pz[I + 2];
	const double Ab = std::hypot(Bx - Ax, Bz - Az), Bc = std::hypot(Cx - Bx, Cz - Bz), Ac = std::hypot(Cx - Ax, Cz - Az);
	const double Cross = std::fabs((Bx - Ax) * (Cz - Az) - (Bz - Az) * (Cx - Ax));
	return 2.0 * Cross / std::fmax(Ab * Bc * Ac, 1e-9);
}

std::vector<P2> SmoothLine(const std::vector<P2>& Pts, double MinR, const std::vector<double>& Fair)
{
	Doubles Rx, Rz, Rf;
	for (size_t I = 0; I < Pts.size(); ++I)
	{
		Rx.push_back(Pts[I].X);
		Rz.push_back(Pts[I].Z);
		Rf.push_back(Fair.size() == Pts.size() ? Fair[I] : FairDefault);
	}
	Fillet(Rx, Rz, Rf, MinR);
	Doubles FineX, FineZ;
	double Total = 0.0;
	ResampleXz(Rx, Rz, 2.0, false, FineX, FineZ, Total);
	Doubles Px, Pz;
	std::vector<char> Turning;
	OpenReversals(FineX, FineZ, MinR, Px, Pz, Turning);
	const size_t N = Px.size();
	const double Limit = 1.0 / MinR;
	const Doubles Along = FairAlong(Rx, Rz, Rf, N);
	// The most each point may be faired: not at all where it is to be kept as it is (fair 0), and
	// only lightly round a turning place.
	Doubles Cap(N);
	for (size_t I = 0; I < N; ++I) Cap[I] = Along[I] <= 0.0 ? 0.0 : (Turning[I] != 0 ? FairLevels[0] : Inf);
	FairToRadius(Px, Pz, Along, Limit, Cap);
	// What is left too tight is pulled open.
	Relax(Px, Pz, Limit * 3.0, 3);
	Relax(Px, Pz, Limit, 2);
	Relax(Px, Pz, Limit, 1);
	// And faired lightly once more, so a bend that had to be pulled open flows like the rest.
	const Doubles Light(N, FairLevels[0]);
	FairLine(Px, Pz, Light, Cap);
	Doubles Kx, Kz;
	ResampleXz(Px, Pz, Step, true, Kx, Kz, Total);
	std::vector<P2> Out;
	Out.reserve(Kx.size());
	for (size_t I = 0; I < Kx.size(); ++I) Out.emplace_back(Kx[I], Kz[I]);
	return Out;
}

std::vector<double> SmoothHeights(const std::vector<double>& H, int Window)
{
	const int K = Window / 2, N = int(H.size());
	Doubles Out(H.size());
	for (int I = 0; I < N; ++I)
	{
		double Sum = 0.0;
		for (int J = I - K; J <= I + K; ++J) Sum += H[size_t(std::min(std::max(J, 0), N - 1))];
		Out[size_t(I)] = Sum / double(Window);
	}
	return Out;
}

std::vector<double> GradeLimit(const std::vector<double>& E, double Grade)
{
	Doubles Out = E;
	const double Lim = Grade * Step;
	for (int Round = 0; Round < 3; ++Round)
	{
		for (size_t I = 1; I < Out.size(); ++I) Out[I] = std::fmin(std::fmax(Out[I], Out[I - 1] - Lim), Out[I - 1] + Lim);
		for (size_t I = Out.size(); I-- > 1;) Out[I - 1] = std::fmin(std::fmax(Out[I - 1], Out[I] - Lim), Out[I] + Lim);
	}
	return Out;
}
} // namespace RoadLine
} // namespace TorqueWorld
