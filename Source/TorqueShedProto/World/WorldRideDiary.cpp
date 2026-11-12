// The ride diary (Ride UK stage 7): see WorldRideDiary.h.
#include "WorldRideDiary.h"

#include "FitJson.h"

#include <cmath>
#include <cstdio>

namespace TorqueWorld
{
namespace RideDiaryBits
{
double MetresBetween(const Geo& A, const Geo& B)
{
	const Frame F(A.Lat, A.Lon);
	const P2 D = F.FromGeo(B.Lat, B.Lon);
	return std::sqrt(D.X * D.X + D.Z * D.Z);
}
} // namespace RideDiaryBits

void DiaryRide::Step(double Lat, double Lon, double Speed, double Dt, long long Now)
{
	using namespace RideDiaryBits;
	const Geo Here{Lat, Lon};
	if (Started == 0) Started = Now;
	Ended = Now;
	if (bLast)
	{
		const double Gone = MetresBetween(Last, Here);
		if (Gone < JumpM) Metres += Gone;
	}
	Last = Here;
	bLast = true;
	if (std::fabs(Speed) > 1.0) Moving += Dt;
	Top = std::fmax(Top, std::fabs(Speed));
	if (Track.empty() || MetresBetween(Track.back(), Here) >= TrackStep) Track.push_back(Here);
}

void DiaryRide::AddPhoto(const std::string& File, long long Now)
{
	DiaryPhoto P;
	P.Lat = bLast ? Last.Lat : (Track.empty() ? 0.0 : Track.back().Lat);
	P.Lon = bLast ? Last.Lon : (Track.empty() ? 0.0 : Track.back().Lon);
	P.Metres = Metres;
	P.When = Now;
	P.File = File;
	Photos.push_back(P);
}

std::string DiaryRide::ToJson() const
{
	using TorqueFit::Json;
	Json J = Json::MakeObject();
	J.Set("id", Json::MakeString(Id));
	J.Set("title", Json::MakeString(Title));
	J.Set("code", Json::MakeString(Code));
	J.Set("started", Json::MakeNumber(double(Started)));
	J.Set("ended", Json::MakeNumber(double(Ended)));
	J.Set("metres", Json::MakeNumber(std::round(Metres)));
	J.Set("top", Json::MakeNumber(std::round(Top * 100.0) / 100.0));
	J.Set("moving", Json::MakeNumber(std::round(Moving)));
	Json T = Json::MakeArray();
	for (const Geo& G : Track)
	{
		Json Pt = Json::MakeArray();
		Pt.Push(Json::MakeNumber(std::round(G.Lat * 1e6) / 1e6));
		Pt.Push(Json::MakeNumber(std::round(G.Lon * 1e6) / 1e6));
		T.Push(Pt);
	}
	J.Set("track", T);
	Json Ps = Json::MakeArray();
	for (const DiaryPhoto& P : Photos)
	{
		Json O = Json::MakeObject();
		O.Set("lat", Json::MakeNumber(std::round(P.Lat * 1e6) / 1e6));
		O.Set("lon", Json::MakeNumber(std::round(P.Lon * 1e6) / 1e6));
		O.Set("metres", Json::MakeNumber(std::round(P.Metres)));
		O.Set("when", Json::MakeNumber(double(P.When)));
		O.Set("file", Json::MakeString(P.File));
		Ps.Push(O);
	}
	J.Set("photos", Ps);
	return J.Dump(0);
}

bool DiaryRide::FromJson(const std::string& Text, DiaryRide& Out)
{
	TorqueFit::Json J;
	if (!TorqueFit::Json::Parse(Text, J) || !J.IsObject()) return false;
	Out = DiaryRide();
	Out.Id = J.Str("id");
	Out.Title = J.Str("title");
	Out.Code = J.Str("code");
	Out.Started = (long long)J.Num("started");
	Out.Ended = (long long)J.Num("ended");
	Out.Metres = J.Num("metres");
	Out.Top = J.Num("top");
	Out.Moving = J.Num("moving");
	if (const TorqueFit::Json* T = J.Get("track"))
		for (const TorqueFit::Json& Pt : T->A)
			if (Pt.IsArray() && Pt.A.size() >= 2) Out.Track.push_back(Geo{Pt.A[0].N, Pt.A[1].N});
	if (const TorqueFit::Json* Ps = J.Get("photos"))
		for (const TorqueFit::Json& O : Ps->A)
		{
			DiaryPhoto P;
			P.Lat = O.Num("lat");
			P.Lon = O.Num("lon");
			P.Metres = O.Num("metres");
			P.When = (long long)O.Num("when");
			P.File = O.Str("file");
			Out.Photos.push_back(P);
		}
	return !Out.Id.empty();
}

std::string DiaryRide::IdFor(long long Seconds)
{
	// (civil from days: Howard Hinnant's)
	const long long Days = Seconds >= 0 ? Seconds / 86400 : (Seconds - 86399) / 86400;
	const long long Secs = Seconds - Days * 86400;
	const long long Z = Days + 719468, Era = (Z >= 0 ? Z : Z - 146096) / 146097;
	const long long Doe = Z - Era * 146097, Yoe = (Doe - Doe / 1460 + Doe / 36524 - Doe / 146096) / 365;
	const long long Doy = Doe - (365 * Yoe + Yoe / 4 - Yoe / 100), Mp = (5 * Doy + 2) / 153;
	const long long D = Doy - (153 * Mp + 2) / 5 + 1, M = Mp < 10 ? Mp + 3 : Mp - 9, Y = Yoe + Era * 400 + (M <= 2 ? 1 : 0);
	char Buf[96];
	std::snprintf(Buf, sizeof Buf, "%04lld%02lld%02lld-%02lld%02lld%02lld", Y, M, D, Secs / 3600, (Secs / 60) % 60, Secs % 60);
	return Buf;
}
} // namespace TorqueWorld
