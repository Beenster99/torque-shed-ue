#include "Fitment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace TorqueFit
{
namespace
{
constexpr double DegRad = 3.14159265358979323846 / 180.0;

V3 ReadV3(const Json* J, const V3& Default = V3())
{
	if (!J || !J->IsArray() || J->A.size() < 3) return Default;
	return V3(J->A[0].N, J->A[1].N, J->A[2].N);
}

Json WriteV3(const V3& P)
{
	Json J = Json::MakeArray();
	J.Push(Json::MakeNumber(std::round(P.x * 1e6) / 1e6));
	J.Push(Json::MakeNumber(std::round(P.y * 1e6) / 1e6));
	J.Push(Json::MakeNumber(std::round(P.z * 1e6) / 1e6));
	return J;
}

Xf FrameFrom(const V3& At, const V3& XAxis, const V3& YHint)
{
	Xf F;
	V3 Ax = XAxis.Normalized();
	if (Ax.Length() < 0.5) Ax = V3(1, 0, 0);
	V3 Ay = YHint - Ax * Ax.Dot(YHint);
	if (Ay.Length() < 1e-6) Ay = std::fabs(Ax.y) < 0.9 ? V3(0, 1, 0) - Ax * Ax.y : V3(1, 0, 0) - Ax * Ax.x;
	Ay = Ay.Normalized();
	F.R = M3(Ax, Ay, Ax.Cross(Ay));
	F.T = At;
	return F;
}

Xf ReadFrame(const Json& J)
{
	return FrameFrom(ReadV3(J.Get("at")), ReadV3(J.Get("x"), V3(1, 0, 0)), ReadV3(J.Get("y"), V3(0, 1, 0)));
}

Json WriteFrame(const Xf& F)
{
	Json J = Json::MakeObject();
	J.Set("at", WriteV3(F.T));
	J.Set("x", WriteV3(F.R.X));
	J.Set("y", WriteV3(F.R.Y));
	return J;
}

std::string Fmt(const char* Format, double A, double B = 0.0)
{
	char Buf[160];
	std::snprintf(Buf, sizeof(Buf), Format, A, B);
	return Buf;
}

// Closest distance between segments P1Q1 and P2Q2 (Ericson, Real-Time Collision Detection 5.1.9).
double SegSegDistance(const V3& P1, const V3& Q1, const V3& P2, const V3& Q2)
{
	const V3 D1 = Q1 - P1, D2 = Q2 - P2, Rv = P1 - P2;
	const double Aa = D1.Dot(D1), Ee = D2.Dot(D2), Ff = D2.Dot(Rv);
	double Ss = 0.0, Tt = 0.0;
	const double Eps = 1e-12;
	if (Aa <= Eps && Ee <= Eps) return Rv.Length();
	if (Aa <= Eps)
	{
		Tt = TorqueSim::Clamp(Ff / Ee, 0.0, 1.0);
	}
	else
	{
		const double Cc = D1.Dot(Rv);
		if (Ee <= Eps)
		{
			Ss = TorqueSim::Clamp(-Cc / Aa, 0.0, 1.0);
		}
		else
		{
			const double Bb = D1.Dot(D2);
			const double Denom = Aa * Ee - Bb * Bb;
			Ss = Denom > Eps ? TorqueSim::Clamp((Bb * Ff - Cc * Ee) / Denom, 0.0, 1.0) : 0.0;
			Tt = (Bb * Ss + Ff) / Ee;
			if (Tt < 0.0) { Tt = 0.0; Ss = TorqueSim::Clamp(-Cc / Aa, 0.0, 1.0); }
			else if (Tt > 1.0) { Tt = 1.0; Ss = TorqueSim::Clamp((Bb - Cc) / Aa, 0.0, 1.0); }
		}
	}
	return ((P1 + D1 * Ss) - (P2 + D2 * Tt)).Length();
}

// Signed distance from a point to a box (negative inside: how deep).
double PointBoxDistance(const V3& P, const Proxy& Bx)
{
	const V3 D = P - Bx.C;
	const double Lx = D.Dot(Bx.X), Ly = D.Dot(Bx.Y), Lz = D.Dot(Bx.Z);
	const double Ox = std::fabs(Lx) - Bx.H.x, Oy = std::fabs(Ly) - Bx.H.y, Oz = std::fabs(Lz) - Bx.H.z;
	if (Ox <= 0.0 && Oy <= 0.0 && Oz <= 0.0) return std::max(Ox, std::max(Oy, Oz));
	const V3 Out(std::max(Ox, 0.0), std::max(Oy, 0.0), std::max(Oz, 0.0));
	return Out.Length();
}

// How far two proxies overlap (metres; negative: the gap between them).
double Penetration(const Proxy& P, const Proxy& Q)
{
	if (!P.IsBox && !Q.IsBox) return P.R + Q.R - SegSegDistance(P.A, P.B, Q.A, Q.B);
	if (P.IsBox != Q.IsBox)
	{
		const Proxy& Cap = P.IsBox ? Q : P;
		const Proxy& Bx = P.IsBox ? P : Q;
		double Best = 1e9;
		const int N = 12;
		for (int K = 0; K <= N; ++K)
		{
			const V3 S = Cap.A + (Cap.B - Cap.A) * (static_cast<double>(K) / N);
			Best = std::min(Best, PointBoxDistance(S, Bx));
		}
		return Cap.R - Best;
	}
	// Box against box: separating axes; the smallest overlap is the depth.
	const V3 Ap[3] = {P.X, P.Y, P.Z}, Bp[3] = {Q.X, Q.Y, Q.Z};
	const double Ah[3] = {P.H.x, P.H.y, P.H.z}, Bh[3] = {Q.H.x, Q.H.y, Q.H.z};
	const V3 D = Q.C - P.C;
	double Depth = 1e9;
	auto Test = [&](const V3& AxisIn) {
		const double L = AxisIn.Length();
		if (L < 1e-6) return;
		const V3 Ax = AxisIn / L;
		double Ra = 0.0, Rb = 0.0;
		for (int K = 0; K < 3; ++K)
		{
			Ra += Ah[K] * std::fabs(Ap[K].Dot(Ax));
			Rb += Bh[K] * std::fabs(Bp[K].Dot(Ax));
		}
		Depth = std::min(Depth, Ra + Rb - std::fabs(D.Dot(Ax)));
	};
	for (int I = 0; I < 3; ++I)
	{
		Test(Ap[I]);
		Test(Bp[I]);
		for (int J = 0; J < 3; ++J) Test(Ap[I].Cross(Bp[J]));
	}
	return Depth;
}

bool ReadProxy(const Json& J, Proxy& Out)
{
	const std::string Shape = J.Str("shape");
	if (Shape == "capsule")
	{
		Out.IsBox = false;
		Out.A = ReadV3(J.Get("a"));
		Out.B = ReadV3(J.Get("b"));
		Out.R = J.Num("r");
		return true;
	}
	if (Shape == "box")
	{
		Out.IsBox = true;
		Out.C = ReadV3(J.Get("c"));
		Out.X = ReadV3(J.Get("x"), V3(1, 0, 0)).Normalized();
		Out.Y = ReadV3(J.Get("y"), V3(0, 1, 0)).Normalized();
		Out.Z = ReadV3(J.Get("z"), V3(0, 0, 1)).Normalized();
		Out.H = ReadV3(J.Get("h"));
		return true;
	}
	return false;
}

bool ReadSocket(const std::string& SockName, const Json& J, const std::map<std::string, Interface>& Types, Socket& Out, std::string& Error)
{
	Out.Name = SockName;
	Out.Type = J.Str("type");
	const auto It = Types.find(Out.Type);
	if (It == Types.end())
	{
		Error = "socket " + SockName + ": unknown interface type '" + Out.Type + "'";
		return false;
	}
	if (const Json* P = J.Get("path"))
	{
		Out.IsPath = true;
		for (const Json& Pt : P->A) Out.Path.push_back(ReadV3(&Pt));
		Out.Up = ReadV3(J.Get("up"), V3(0, 1, 0));
		if (Out.Path.size() < 2)
		{
			Error = "socket " + SockName + ": a path needs two points";
			return false;
		}
	}
	else
	{
		Out.Frame = ReadFrame(J);
	}
	if (!It->second.SizeKey.empty())
	{
		if (const Json* Sz = J.Get(It->second.SizeKey))
		{
			Out.SizeMm = Sz->N;
			Out.HasSize = true;
		}
	}
	if (const Json* P = J.Get("points"))
		for (const Json& Pt : P->A) Out.Points.push_back(ReadV3(&Pt));
	if (const Json* H = J.Get("holes"))
		for (const Json& Hv : H->A) Out.Holes.push_back(Hv.N);
	return true;
}

bool ReadUnit(const Json& J, const std::map<std::string, Interface>& Types, Unit& Out, std::string& Error)
{
	Out.Id = J.Str("id");
	Out.Group = J.Str("group", "body");
	Out.Slot = J.Str("slot");
	if (const Json* C = J.Get("catalogue"))
	{
		if (C->IsArray() && C->A.size() >= 2)
		{
			Out.CatKind = C->A[0].IsString() ? C->A[0].S : std::string();
			Out.CatId = C->A[1].IsString() ? C->A[1].S : std::string();
		}
	}
	if (const Json* M = J.Get("meshes"))
		for (const Json& Mv : M->A) Out.Meshes.push_back(Mv.S);
	if (const Json* S = J.Get("sockets"))
	{
		for (const auto& KV : S->O)
		{
			Socket Sk;
			if (!ReadSocket(KV.first, KV.second, Types, Sk, Error))
			{
				Error = Out.Id + ": " + Error;
				return false;
			}
			Out.Sockets.push_back(Sk);
		}
	}
	if (const Json* P = J.Get("proxies"))
	{
		for (const Json& Pj : P->A)
		{
			Proxy Px;
			if (ReadProxy(Pj, Px)) Out.Proxies.push_back(Px);
		}
	}
	if (const Json* M = J.Get("mount"))
	{
		Out.HasMount = true;
		const std::string To = M->Str("to");
		const size_t Dot = To.find('.');
		if (Dot == std::string::npos)
		{
			Error = Out.Id + ": mount 'to' should be unit.socket, not '" + To + "'";
			return false;
		}
		Out.Mnt.ParentUnit = To.substr(0, Dot);
		Out.Mnt.ParentSocket = To.substr(Dot + 1);
		Out.Mnt.Type = M->Str("type");
		const auto It = Types.find(Out.Mnt.Type);
		if (It == Types.end())
		{
			Error = Out.Id + ": mount of unknown interface type '" + Out.Mnt.Type + "'";
			return false;
		}
		if (!It->second.SizeKey.empty())
		{
			if (const Json* Sz = M->Get(It->second.SizeKey))
			{
				Out.Mnt.SizeMm = Sz->N;
				Out.Mnt.HasSize = true;
			}
		}
		if (const Json* A = M->Get("along")) { Out.Mnt.HasAlong = true; Out.Mnt.Along = A->N; }
		if (const Json* F = M->Get("from_end")) { Out.Mnt.HasFromEnd = true; Out.Mnt.FromEnd = F->N; }
		if (const Json* P = M->Get("points"))
			for (const Json& Pt : P->A) Out.Mnt.Points.push_back(ReadV3(&Pt));
	}
	if (const Json* C = J.Get("conform"))
	{
		if (const Json* Ends = C->Get("ends"))
		{
			for (const Json& E : Ends->A)
			{
				ConformEnd End;
				End.Unit = E.Str("unit");
				End.At = ReadV3(E.Get("at"));
				Out.Conform.push_back(End);
			}
		}
	}
	return true;
}

double Clamp(double V, double Lo, double Hi) { return TorqueSim::Clamp(V, Lo, Hi); }
} // namespace

// --- Xf, sockets, proxies ---------------------------------------------------------------------------

Xf Xf::Rotate(const V3& Axis, double Angle, const V3& Through)
{
	Xf Out;
	Out.R = M3::AxisAngle(Axis.Normalized(), Angle);
	Out.T = Through - Out.R * Through;
	return Out;
}

double Xf::Drift(const Xf& A, const Xf& B, double Reach)
{
	double Worst = 0.0;
	const V3 Probe[7] = {V3(), V3(Reach, 0, 0), V3(-Reach, 0, 0), V3(0, Reach, 0), V3(0, -Reach, 0), V3(0, 0, Reach), V3(0, 0, -Reach)};
	for (const V3& P : Probe) Worst = std::max(Worst, (A.Point(P) - B.Point(P)).Length());
	return Worst;
}

double Socket::Length() const
{
	double L = 0.0;
	for (size_t K = 1; K < Path.size(); ++K) L += (Path[K] - Path[K - 1]).Length();
	return L;
}

Xf Socket::FrameAt(double S) const
{
	if (!IsPath) return Frame;
	double Left = std::max(S, 0.0);
	for (size_t K = 1; K < Path.size(); ++K)
	{
		const V3 Seg = Path[K] - Path[K - 1];
		const double L = Seg.Length();
		if (Left <= L || K + 1 == Path.size())
		{
			const double F = L > 1e-9 ? Clamp(Left / L, 0.0, 1.0) : 0.0;
			return FrameFrom(Path[K - 1] + Seg * F, Seg, Up);
		}
		Left -= L;
	}
	return FrameFrom(Path.back(), Path.back() - Path.front(), Up);
}

Proxy Proxy::Moved(const Xf& M) const
{
	Proxy Out = *this;
	if (IsBox)
	{
		Out.C = M.Point(C);
		Out.X = M.Dir(X);
		Out.Y = M.Dir(Y);
		Out.Z = M.Dir(Z);
	}
	else
	{
		Out.A = M.Point(A);
		Out.B = M.Point(B);
	}
	return Out;
}

const Socket* Unit::FindSocket(const std::string& SockName) const
{
	for (const Socket& S : Sockets)
		if (S.Name == SockName) return &S;
	return nullptr;
}

const Placed* Assembly::Find(const std::string& Id) const
{
	for (const Placed& P : Units)
		if (P.U.Id == Id) return &P;
	return nullptr;
}

// --- Loading --------------------------------------------------------------------------------------

bool BikeFit::Load(const std::string& FitmentText, const std::string& InterfacesText, std::string& Error)
{
	Json Types, Fit;
	if (!Json::Parse(InterfacesText, Types, &Error)) { Error = "interfaces.json: " + Error; return false; }
	if (!Json::Parse(FitmentText, Fit, &Error)) { Error = "fitment.json: " + Error; return false; }
	Interfaces.clear();
	Units.clear();
	Options.clear();
	SlotOrder.clear();
	Meshes.clear();
	GroupOrigins.clear();
	Touch.clear();
	if (const Json* I = Types.Get("interfaces"))
	{
		for (const auto& KV : I->O)
		{
			Interface F;
			F.Id = KV.first;
			F.Name = KV.second.Str("name", KV.first);
			F.SizeKey = KV.second.Str("size");
			F.Rule = KV.second.Str("rule", "any");
			F.Adjust = KV.second.Str("adjust", "none");
			F.ToleranceMm = KV.second.Num("tolerance_mm", 0.5);
			F.PlateMm = KV.second.Num("plate_mm", 0.0);
			if (const Json* S = KV.second.Get("slide"))
				if (S->A.size() >= 2) { F.SlideMin = S->A[0].N; F.SlideMax = S->A[1].N; }
			if (const Json* T = KV.second.Get("turn_deg"))
				if (T->A.size() >= 2) { F.TurnMinDeg = T->A[0].N; F.TurnMaxDeg = T->A[1].N; }
			Interfaces[F.Id] = F;
		}
	}
	Bike = Fit.Str("bike");
	Name = Fit.Str("name", Bike);
	if (const Json* D = Fit.Get("datums")) Datums = *D;
	if (const Json* C = Fit.Get("clearance"))
	{
		ClearanceData = *C;
		ToleranceMm = C->Num("tolerance_mm", 1.0);
		if (const Json* T = C->Get("touch"))
			for (const Json& Pr : T->A)
				if (Pr.A.size() >= 2) Touch.emplace_back(Pr.A[0].S, Pr.A[1].S);
	}
	if (const Json* G = Fit.Get("group_origins"))
		for (const auto& KV : G->O) GroupOrigins[KV.first] = ReadV3(&KV.second);
	if (const Json* M = Fit.Get("meshes"))
	{
		for (const auto& KV : M->O)
		{
			MeshInfo Mi;
			Mi.Group = KV.second.Str("group", "body");
			Mi.Finish = KV.second.Str("finish");
			Meshes[KV.first] = Mi;
		}
	}
	const Json* U = Fit.Get("units");
	if (!U || !U->IsArray() || U->A.empty())
	{
		Error = "fitment.json: no units";
		return false;
	}
	std::set<std::string> Seen;
	for (const Json& Uj : U->A)
	{
		Unit Un;
		if (!ReadUnit(Uj, Interfaces, Un, Error)) return false;
		if (Un.HasMount && !Seen.count(Un.Mnt.ParentUnit))
		{
			Error = Un.Id + " mounts on " + Un.Mnt.ParentUnit + ", which isn't listed before it";
			return false;
		}
		Seen.insert(Un.Id);
		Units.push_back(Un);
	}
	if (const Json* O = Fit.Get("options"))
	{
		for (const auto& KV : O->O)
		{
			SlotOrder.push_back(KV.first);
			std::vector<Option>& List = Options[KV.first];
			for (const Json& Oj : KV.second.A)
			{
				Option Op;
				Op.Slot = KV.first;
				Op.Id = Oj.Str("id");
				Op.Name = Oj.Str("name", Op.Id);
				Op.Stock = Oj.Flag("stock");
				if (const Json* C = Oj.Get("catalogue"))
				{
					if (C->A.size() >= 2)
					{
						Op.CatKind = C->A[0].IsString() ? C->A[0].S : std::string();
						Op.CatId = C->A[1].IsString() ? C->A[1].S : std::string();
					}
				}
				if (const Json* Ou = Oj.Get("units"))
				{
					for (const Json& Uj : Ou->A)
					{
						Unit Un;
						if (!ReadUnit(Uj, Interfaces, Un, Error)) { Error = KV.first + "/" + Op.Id + ": " + Error; return false; }
						if (!Seen.count(Un.Id)) { Error = KV.first + "/" + Op.Id + ": replaces " + Un.Id + ", which the bike hasn't got"; return false; }
						Op.Units.push_back(Un);
					}
				}
				if (const Json* Om = Oj.Get("meshes"))
				{
					for (const auto& Mv : Om->O)
					{
						MeshInfo Mi;
						Mi.Group = Mv.second.Str("group", "body");
						Mi.Finish = Mv.second.Str("finish");
						Mi.File = Mv.second.Str("file");
						Op.Meshes[Mv.first] = Mi;
					}
				}
				List.push_back(Op);
			}
		}
	}
	return true;
}

bool BikeFit::ApplyFixes(const std::string& FixesText, std::string& Error)
{
	Json Fx;
	if (!Json::Parse(FixesText, Fx, &Error)) { Error = "fitment_fixes.json: " + Error; return false; }
	const Json* List = Fx.Get("fixes");
	if (!List) return true;
	for (const Json& F : List->A)
	{
		const std::string Id = F.Str("unit"), Opt = F.Str("option", "stock"), SlotName = F.Str("slot");
		const Json* Child = F.Get("child");
		if (!Child) continue;
		Unit* Target = nullptr;
		if (Opt == "stock")
		{
			for (Unit& Un : Units)
				if (Un.Id == Id) Target = &Un;
		}
		else
		{
			auto It = Options.find(SlotName);
			if (It != Options.end())
				for (Option& Op : It->second)
					if (Op.Id == Opt)
						for (Unit& Un : Op.Units)
							if (Un.Id == Id) Target = &Un;
		}
		if (!Target || !Target->HasMount) continue;
		Target->Mnt.HasChild = true;
		Target->Mnt.Child = ReadFrame(*Child);
	}
	return true;
}

std::vector<std::string> BikeFit::Slots() const { return SlotOrder; }

const std::vector<Option>* BikeFit::OptionsFor(const std::string& SlotName) const
{
	auto It = Options.find(SlotName);
	return It == Options.end() ? nullptr : &It->second;
}

const Option* BikeFit::FindOption(const std::string& SlotName, const std::string& Id) const
{
	if (const std::vector<Option>* List = OptionsFor(SlotName))
		for (const Option& Op : *List)
			if (Op.Id == Id) return &Op;
	return nullptr;
}

const Interface* BikeFit::FindInterface(const std::string& Id) const
{
	auto It = Interfaces.find(Id);
	return It == Interfaces.end() ? nullptr : &It->second;
}

const MeshInfo* BikeFit::FindMesh(const std::string& MeshName) const
{
	auto It = Meshes.find(MeshName);
	if (It != Meshes.end()) return &It->second;
	for (const auto& KV : Options)
		for (const Option& Op : KV.second)
		{
			auto Ot = Op.Meshes.find(MeshName);
			if (Ot != Op.Meshes.end()) return &Ot->second;
		}
	return nullptr;
}

V3 BikeFit::GroupOrigin(const std::string& Group) const
{
	auto It = GroupOrigins.find(Group);
	return It == GroupOrigins.end() ? V3() : It->second;
}

// --- Matching and assembly ------------------------------------------------------------------------

bool BikeFit::MountMatches(const Unit& Child, const Unit& Parent, std::string& WhyNot) const
{
	const Socket* S = Parent.FindSocket(Child.Mnt.ParentSocket);
	if (!S)
	{
		WhyNot = Child.Id + " mounts on " + Parent.Id + "'s " + Child.Mnt.ParentSocket + ", which this " + Parent.Id + " hasn't got";
		return false;
	}
	if (S->Type != Child.Mnt.Type)
	{
		WhyNot = Child.Id + " needs a " + Child.Mnt.Type + " socket; " + Parent.Id + "'s " + S->Name + " is a " + S->Type;
		return false;
	}
	const Interface* I = FindInterface(S->Type);
	if (!I) return true;
	if (I->Rule == "equal" && S->HasSize && Child.Mnt.HasSize && std::fabs(S->SizeMm - Child.Mnt.SizeMm) > I->ToleranceMm)
	{
		WhyNot = Fmt("%.1f mm ", Child.Mnt.SizeMm) + Child.Id + " won't go in " + Parent.Id + Fmt("'s %.1f mm ", S->SizeMm) + I->Name +
			" (needs an adapter or a different " + Parent.Id + ")";
		return false;
	}
	if (I->Rule == "slip" && S->HasSize && Child.Mnt.HasSize)
	{
		const double Gap = Child.Mnt.SizeMm - S->SizeMm;
		if (Gap < 0.0 || Gap > I->ToleranceMm)
		{
			WhyNot = Child.Id + Fmt(" has a %.1f mm bore; ", Child.Mnt.SizeMm) + Parent.Id + Fmt(" ends at %.1f mm", S->SizeMm) +
				(Gap < 0.0 ? " (too tight)" : " (too loose: needs a reducer)");
			return false;
		}
	}
	if (I->Rule == "pattern")
	{
		if (S->Points.size() != Child.Mnt.Points.size())
		{
			WhyNot = Child.Id + " has a different number of mounting points from " + Parent.Id;
			return false;
		}
		double Worst = 0.0;
		for (size_t K = 0; K < S->Points.size(); ++K) Worst = std::max(Worst, (S->Points[K] - Child.Mnt.Points[K]).Length() * 1000.0);
		if (Worst > I->PlateMm && I->PlateMm > 0.0)
		{
			WhyNot = Child.Id + Fmt("'s mounts are %.0f mm off ", Worst) + Parent.Id + "'s: it won't fit";
			return false;
		}
		if (Worst > I->ToleranceMm) WhyNot = Fmt("fits with plates (mounts up to %.0f mm off)", Worst);
	}
	return true;
}

std::vector<Unit> BikeFit::EffectiveUnits(const Build& B, std::vector<std::pair<std::string, std::string>>& From, std::string& Error) const
{
	std::vector<Unit> Out = Units;
	From.assign(Out.size(), std::make_pair(std::string(), std::string("stock")));
	for (size_t K = 0; K < Out.size(); ++K) From[K].first = Out[K].Slot;
	for (const auto& KV : B.Fitted)
	{
		if (KV.second.empty() || KV.second == "stock") continue;
		const Option* Op = FindOption(KV.first, KV.second);
		if (!Op)
		{
			Error = "no " + KV.second + " option for " + KV.first;
			return {};
		}
		for (const Unit& Un : Op->Units)
		{
			for (size_t K = 0; K < Out.size(); ++K)
			{
				if (Out[K].Id != Un.Id) continue;
				Out[K] = Un;
				From[K] = std::make_pair(KV.first, Op->Id);
			}
		}
	}
	return Out;
}

double BikeFit::PathParam(const Socket& S, const Mount& M) const
{
	const double L = S.Length();
	if (M.HasFromEnd) return Clamp(L - M.FromEnd, 0.0, L);
	if (M.HasAlong) return Clamp(M.Along, 0.0, L);
	return 0.0;
}

bool BikeFit::CanFit(const Build& B, const std::string& SlotName, const std::string& Id, std::string& WhyNot) const
{
	WhyNot.clear();
	const Option* Op = FindOption(SlotName, Id);
	if (!Op)
	{
		WhyNot = "the " + Name + " has no " + Id + " for its " + SlotName;
		return false;
	}
	Build Trial = B;
	Trial.Fitted[SlotName] = Id;
	std::vector<std::pair<std::string, std::string>> From;
	std::string Error;
	const std::vector<Unit> Eff = EffectiveUnits(Trial, From, Error);
	if (Eff.empty())
	{
		WhyNot = Error;
		return false;
	}
	std::map<std::string, const Unit*> ById;
	std::string Note;
	for (const Unit& Un : Eff)
	{
		ById[Un.Id] = &Un;
		if (!Un.HasMount) continue;
		auto It = ById.find(Un.Mnt.ParentUnit);
		if (It == ById.end())
		{
			WhyNot = Un.Id + " has nothing to mount on";
			return false;
		}
		std::string Why;
		if (!MountMatches(Un, *It->second, Why))
		{
			WhyNot = Why;
			return false;
		}
		if (!Why.empty()) Note = Why;
	}
	WhyNot = Note;   // fits, perhaps with a note (plates)
	return true;
}

bool BikeFit::Assemble(const Build& B, Assembly& Out, std::string& Error) const
{
	Out = Assembly();
	std::vector<std::pair<std::string, std::string>> From;
	std::vector<Unit> Eff = EffectiveUnits(B, From, Error);
	if (Eff.empty()) return false;
	std::map<std::string, size_t> Index;          // unit id -> position in Eff
	std::map<std::string, const Unit*> StockById;
	for (const Unit& Un : Units) StockById[Un.Id] = &Un;
	std::vector<Placed> Placements(Eff.size());
	for (size_t K = 0; K < Eff.size(); ++K)
	{
		const Unit& Un = Eff[K];
		Index[Un.Id] = K;
		Placed& P = Placements[K];
		P.Slot = From[K].first;
		P.OptionId = From[K].second;
		if (!Un.HasMount)
		{
			P.World = Xf();
			P.Socket0 = Xf();
			continue;
		}
		auto Pi = Index.find(Un.Mnt.ParentUnit);
		if (Pi == Index.end())
		{
			Error = Un.Id + " has nothing to mount on";
			return false;
		}
		const Unit& Parent = Eff[Pi->second];
		std::string Why;
		if (!MountMatches(Un, Parent, Why))
		{
			Error = Why;
			return false;
		}
		const Socket& S = *Parent.FindSocket(Un.Mnt.ParentSocket);
		const Interface* I = FindInterface(S.Type);
		P.AdjustKind = I ? I->Adjust : std::string("none");
		// Its mount frame on the stock bike: the stock parent's socket where this unit sits.
		Xf Child;
		if (Un.Mnt.HasChild) Child = Un.Mnt.Child;
		else
		{
			const Unit* StockParent = StockById.count(Parent.Id) ? StockById[Parent.Id] : &Parent;
			const Socket* Ss = StockParent->FindSocket(S.Name);
			if (!Ss) Ss = &S;
			Child = Ss->FrameAt(Ss->IsPath ? PathParam(*Ss, Un.Mnt) : 0.0);
		}
		// Where the parent's socket is now, with this unit's adjustment.
		const Xf& ParentWorld = Placements[Pi->second].World;
		double SParam = S.IsPath ? PathParam(S, Un.Mnt) : 0.0;
		P.Socket0 = ParentWorld * S.FrameAt(SParam);
		Adjust Adj;
		auto Ai = B.Adjusts.find(Un.Id);
		if (Ai != B.Adjusts.end()) Adj = Ai->second;
		double Slide = 0.0, Turn = 0.0;
		if (I)
		{
			const std::string& Kind = I->Adjust;
			if (Kind == "slide" || Kind == "slot" || Kind == "slide_turn") Slide = Clamp(Adj.Slide, I->SlideMin, I->SlideMax);
			if (Kind == "turn" || Kind == "slide_turn") Turn = Clamp(Adj.TurnDeg, I->TurnMinDeg, I->TurnMaxDeg) * DegRad;
			if (Kind == "holes" && Adj.Hole >= 0 && Adj.Hole < static_cast<int>(S.Holes.size())) Slide = S.Holes[static_cast<size_t>(Adj.Hole)];
		}
		Xf F;
		if (S.IsPath)
		{
			SParam = Clamp(SParam + Slide, 0.0, S.Length());
			F = ParentWorld * S.FrameAt(SParam);
		}
		else
		{
			F = P.Socket0 * Xf::Translate(V3(Slide, 0, 0));
		}
		if (Turn != 0.0) F = F * Xf::Rotate(V3(1, 0, 0), Turn);
		P.World = F * Child.Inverse();
		if (Adj.Free) P.World = Adj.FreeXf * P.World;
	}
	for (size_t K = 0; K < Eff.size(); ++K) Placements[K].U = Eff[K];
	Out.Units = std::move(Placements);
	// Conforming parts: report ends that moved away from where the part was made.
	for (const Placed& P : Out.Units)
	{
		for (const ConformEnd& E : P.U.Conform)
		{
			const Placed* Other = Out.Find(E.Unit);
			if (!Other) continue;
			const double Moved = (Other->World.Point(E.At) - P.World.Point(E.At)).Length();
			if (Moved > 0.002)
				Out.Notes.push_back(P.U.Id + ": its end at " + E.Unit + Fmt(" moved %.0f mm; the route isn't regenerated yet", Moved * 1000.0));
		}
	}
	return true;
}

// --- Clearance ------------------------------------------------------------------------------------

Xf BikeFit::GroupMotion(const std::string& Group, double SteerDeg, double Front, double Rear) const
{
	if (Group == "body") return Xf();
	const V3 SteerPoint = ReadV3(Datums.Get("steer_point"));
	const V3 SteerDir = ReadV3(Datums.Get("steer_dir"), V3(0, 1, 0)).Normalized();
	// Positive steer is to the right: about an axis pointing up, that's a negative turn.
	const Xf Steer = Xf::Rotate(SteerDir, -SteerDeg * DegRad, SteerPoint);
	if (Group == "steer") return Steer;
	if (Group == "front_lower" || Group == "front_wheel")
		return Steer * Xf::Translate(SteerDir * (Front * Datums.Num("front_travel", 0.12)));
	if (Group == "swingarm" || Group == "rear_wheel")
	{
		const V3 Pivot = ReadV3(Datums.Get("swing_pivot"));
		const V3 Axle = ReadV3(Datums.Get("rear_axle"));
		const double Arm = std::max((Axle - Pivot).Length(), 0.1);
		const double Lift = Clamp(Rear * Datums.Num("rear_travel", 0.12) / Arm, -0.95, 0.95);
		// Up at the axle: the arm turns about the pivot's z axis, towards +y for an axle behind it.
		const double Sign = (Axle.x < Pivot.x) ? -1.0 : 1.0;
		return Xf::Rotate(V3(0, 0, 1), Sign * std::asin(Lift), Pivot);
	}
	return Xf();
}

bool BikeFit::Unmoved(const Placed& P)
{
	return P.OptionId == "stock" && !P.U.Mnt.HasChild && Xf::Drift(P.World, Xf()) < 1e-4;
}

void BikeFit::Depths(const Assembly& A, bool AllPairs, std::map<std::string, double>& Out) const
{
	Out.clear();
	const Json* Cases = ClearanceData.Get("cases");
	if (!Cases) return;
	auto IsTouch = [&](const std::string& Ua, const std::string& Ub) {
		for (const auto& Pr : Touch)
			if ((Pr.first == Ua && Pr.second == Ub) || (Pr.first == Ub && Pr.second == Ua)) return true;
		return false;
	};
	const size_t Count = A.Units.size();
	for (const Json& C : Cases->A)
	{
		const std::string CaseName = C.Str("name");
		const double SteerDeg = C.Num("steer_deg"), Front = C.Num("front"), Rear = C.Num("rear");
		const bool Rest = SteerDeg == 0.0 && Front == 0.0 && Rear == 0.0;
		std::vector<std::vector<Proxy>> Moved(Count);
		for (size_t K = 0; K < Count; ++K)
		{
			const Xf M = GroupMotion(A.Units[K].U.Group, SteerDeg, Front, Rear) * A.Units[K].World;
			for (const Proxy& P : A.Units[K].U.Proxies) Moved[K].push_back(P.Moved(M));
		}
		for (size_t I = 0; I < Count; ++I)
		{
			if (Moved[I].empty()) continue;
			const Placed& Pa = A.Units[I];
			for (size_t J = I + 1; J < Count; ++J)
			{
				if (Moved[J].empty()) continue;
				const Placed& Pb = A.Units[J];
				const Unit& Ua = Pa.U;
				const Unit& Ub = Pb.U;
				if ((Ua.HasMount && Ua.Mnt.ParentUnit == Ub.Id) || (Ub.HasMount && Ub.Mnt.ParentUnit == Ua.Id)) continue;
				if (IsTouch(Ua.Id, Ub.Id)) continue;
				if (Ua.Group == Ub.Group)
				{
					// The same group never moves against itself: one look, at rest, and none at all for
					// two parts still where they were modelled (unless asked for every pair).
					if (!Rest) continue;
					if (!AllPairs && Unmoved(Pa) && Unmoved(Pb)) continue;
				}
				double Depth = -1.0;
				for (const Proxy& Qa : Moved[I])
					for (const Proxy& Qb : Moved[J]) Depth = std::max(Depth, Penetration(Qa, Qb));
				const bool InOrder = Ua.Id < Ub.Id;
				Out[(InOrder ? Ua.Id : Ub.Id) + "|" + (InOrder ? Ub.Id : Ua.Id) + "|" + CaseName] = Depth;
			}
		}
	}
}

std::vector<Clash> BikeFit::Clearance(const Assembly& A, const Assembly* Baseline) const
{
	std::vector<Clash> Out;
	const double Tol = ToleranceMm / 1000.0;
	std::map<std::string, double> Now, Base;
	Depths(A, false, Now);
	if (Baseline) Depths(*Baseline, true, Base);
	std::map<std::string, const Placed*> ById;
	for (const Placed& P : A.Units) ById[P.U.Id] = &P;
	const Json* Cases = ClearanceData.Get("cases");
	// Report in case order, then pair order.
	for (const Json& C : Cases->A)
	{
		const std::string CaseName = C.Str("name");
		const std::string Tail = "|" + CaseName;
		for (const auto& KV : Now)
		{
			const std::string& Key = KV.first;
			if (Key.size() <= Tail.size() || Key.compare(Key.size() - Tail.size(), Tail.size(), Tail) != 0) continue;
			const size_t Bar = Key.find('|');
			const std::string Ia = Key.substr(0, Bar);
			const std::string Ib = Key.substr(Bar + 1, Key.size() - Tail.size() - Bar - 1);
			const Placed* Pa = ById[Ia];
			const Placed* Pb = ById[Ib];
			double Ref = 0.0;
			if (Unmoved(*Pa) && Unmoved(*Pb))
			{
				// Modelled in place: what they overlap at rest is the model's (or the proxies'), not the movement's.
				auto Rest = Now.find(Ia + "|" + Ib + "|rest");
				if (Rest != Now.end()) Ref = std::max(Ref, Rest->second);
			}
			auto Bk = Base.find(Key);
			if (Bk != Base.end()) Ref = std::max(Ref, Bk->second);
			if (KV.second > Ref + Tol)
			{
				Clash Cl;
				Cl.A = Ia;
				Cl.B = Ib;
				Cl.Case = CaseName;
				Cl.DepthMm = (KV.second - Ref) * 1000.0;
				Out.push_back(Cl);
			}
		}
	}
	return Out;
}

// --- Datums ---------------------------------------------------------------------------------------

std::vector<DatumCheck> BikeFit::CheckDatums(const Assembly& A, const PhysicsDatums& P) const
{
	std::vector<DatumCheck> Out;
	auto Add = [&](const char* What, double Model, double Physics, double Tol) {
		DatumCheck D;
		D.Name = What;
		D.Model = Model;
		D.Physics = Physics;
		D.Tolerance = Tol;
		D.Ok = std::fabs(Model - Physics) <= Tol;
		Out.push_back(D);
	};
	// Where the assembled model's axles and pivot actually are.
	V3 FrontAxle = ReadV3(Datums.Get("front_axle")), RearAxle = ReadV3(Datums.Get("rear_axle"));
	if (const Placed* Fw = A.Find("front_wheel")) FrontAxle = Fw->World.Point(FrontAxle);
	if (const Placed* Rw = A.Find("rear_wheel")) RearAxle = Rw->World.Point(RearAxle);
	V3 Pivot = ReadV3(Datums.Get("swing_pivot"));
	if (const Placed* Sw = A.Find("swingarm")) Pivot = Sw->World.Point(Pivot);
	const V3 SteerDir = ReadV3(Datums.Get("steer_dir"), V3(0, 1, 0)).Normalized();
	V3 SteerPoint = ReadV3(Datums.Get("steer_point"));
	if (const Placed* Yk = A.Find("yokes")) SteerPoint = Yk->World.Point(SteerPoint);
	const double Rake = std::atan2(-SteerDir.x, SteerDir.y) / DegRad;
	// Fork offset: the front axle's distance from the steering axis.
	const V3 Rel = FrontAxle - SteerPoint;
	const double Offset = (Rel - SteerDir * Rel.Dot(SteerDir)).Length();
	Add("wheelbase (m)", FrontAxle.x - RearAxle.x, P.Wheelbase, 0.003);
	Add("rake (deg)", Rake, P.RakeDeg, 0.2);
	Add("fork offset (m)", Offset, P.ForkOffset, 0.002);
	Add("front tyre radius (m)", Datums.Num("front_radius"), P.FrontRadius, 0.003);
	Add("rear tyre radius (m)", Datums.Num("rear_radius"), P.RearRadius, 0.003);
	Add("front travel (m)", Datums.Num("front_travel"), P.FrontTravel, 0.002);
	Add("rear travel (m)", Datums.Num("rear_travel"), P.RearTravel, 0.002);
	Add("swingarm pivot x (m)", Pivot.x, P.SwingPivot.x, 0.01);
	Add("swingarm pivot y (m)", Pivot.y, P.SwingPivot.y, 0.01);
	Add("swingarm length (m)", (RearAxle - Pivot).Length(), (V3(-P.Wheelbase / 2, 0, 0) - P.SwingPivot).Length(), 0.01);
	return Out;
}

// --- Geometry mode --------------------------------------------------------------------------------

Adjust BikeFit::ConstrainDrag(const Assembly& A, const std::string& UnitId, const Adjust& Current, const V3& Drag, double TurnDeg) const
{
	Adjust Out = Current;
	const Placed* P = A.Find(UnitId);
	if (!P || !P->U.HasMount) return Out;
	const Interface* I = FindInterface(P->U.Mnt.Type);
	if (!I) return Out;
	// The socket's own axis where the unit sits now.
	const V3 Axis = P->Socket0.R.X;
	const double Along = Drag.Dot(Axis);
	const std::string& Kind = I->Adjust;
	if (Kind == "slide" || Kind == "slot" || Kind == "slide_turn")
		Out.Slide = Clamp(Current.Slide + Along, I->SlideMin, I->SlideMax);
	if (Kind == "turn" || Kind == "slide_turn")
		Out.TurnDeg = Clamp(Current.TurnDeg + TurnDeg, I->TurnMinDeg, I->TurnMaxDeg);
	if (Kind == "holes")
	{
		const Unit* Parent = nullptr;
		if (const Placed* Pp = A.Find(P->U.Mnt.ParentUnit)) Parent = &Pp->U;
		const Socket* S = Parent ? Parent->FindSocket(P->U.Mnt.ParentSocket) : nullptr;
		if (S && !S->Holes.empty())
		{
			const double Now = (Current.Hole >= 0 && Current.Hole < static_cast<int>(S->Holes.size())) ? S->Holes[static_cast<size_t>(Current.Hole)] : 0.0;
			const double Want = Now + Along;
			int Best = 0;
			for (size_t K = 1; K < S->Holes.size(); ++K)
				if (std::fabs(S->Holes[K] - Want) < std::fabs(S->Holes[static_cast<size_t>(Best)] - Want)) Best = static_cast<int>(K);
			Out.Hole = Best;
		}
	}
	return Out;
}

bool BikeFit::AuthorFix(const Assembly& A, const std::string& UnitId, Json& Fix, std::string& Error) const
{
	const Placed* P = A.Find(UnitId);
	if (!P) { Error = "no unit " + UnitId; return false; }
	if (!P->U.HasMount) { Error = UnitId + " is the datum: nothing to fix"; return false; }
	// The mount frame that puts the unit where it is now with no adjustment: World = Socket0 * inv(Child).
	const Xf Child = P->World.Inverse() * P->Socket0;
	Fix = Json::MakeObject();
	Fix.Set("unit", Json::MakeString(UnitId));
	Fix.Set("option", Json::MakeString(P->OptionId.empty() ? std::string("stock") : P->OptionId));
	Fix.Set("slot", Json::MakeString(P->Slot));
	Fix.Set("child", WriteFrame(Child));
	return true;
}
} // namespace TorqueFit
