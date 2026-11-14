// [build 147] What the game thread spends on a ride, by system, so a slow town says where the time goes
// (hub, build 146: York 44 fps, game thread bound at 34 ms a frame). Each system's work adds its time
// (game thread only); ATorqueRideWorld logs the sums every 5 seconds as "Ride costs:".
#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

namespace RideCosts
{
enum EWhich : int32 { Land, Plants, Animals, Props, Buildings, Lights, Words, Water, Roads, Update, Grow, Tft, MapTile, Count };
inline const TCHAR* Name(int32 Which)
{
	static const TCHAR* Names[] = {TEXT("land"), TEXT("plants"), TEXT("animals"), TEXT("props"), TEXT("buildings"), TEXT("lights"), TEXT("signs"),
		TEXT("water"), TEXT("roads"), TEXT("squares"), TEXT("grow"), TEXT("tft"), TEXT("maptile")};
	return Which >= 0 && Which < Count ? Names[Which] : TEXT("?");
}
struct FSums
{
	double Ms[Count] = {};
	int32 Calls[Count] = {};
	double Worst[Count] = {};
};
inline FSums& Sums()
{
	static FSums S;
	return S;
}
inline void Add(int32 Which, double Ms)
{
	FSums& S = Sums();
	S.Ms[Which] += Ms;
	++S.Calls[Which];
	S.Worst[Which] = FMath::Max(S.Worst[Which], Ms);
}
// Times its scope into Which.
struct FScope
{
	int32 Which;
	double T0;
	explicit FScope(int32 InWhich) : Which(InWhich), T0(FPlatformTime::Seconds()) {}
	~FScope() { Add(Which, (FPlatformTime::Seconds() - T0) * 1000.0); }
};
} // namespace RideCosts
