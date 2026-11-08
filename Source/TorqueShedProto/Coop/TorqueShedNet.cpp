#include "TorqueShedNet.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
// Bumped when the layout below changes: a game that can't read the host's bays says so in the log
// rather than drawing nonsense.
constexpr int32 Layout = 1;

void Serialize(FArchive& Ar, FTorqueShedBike& B)
{
	Ar << B.bExists;
	Ar << B.bRode;
	Ar << B.Build.Bike;
	Ar << B.Build.Fitted;
	int32 NumAdjust = B.Build.Adjust.Num();
	Ar << NumAdjust;
	if (Ar.IsLoading())
	{
		B.Build.Adjust.Reset();
		for (int32 I = 0; I < NumAdjust && !Ar.IsError(); ++I)
		{
			FName Unit;
			FTorqueFitAdjust A;
			Ar << Unit << A.Slide << A.TurnDeg << A.Hole << A.bFree << A.Free;
			B.Build.Adjust.Add(Unit, A);
		}
	}
	else
	{
		for (TPair<FName, FTorqueFitAdjust>& P : B.Build.Adjust)
		{
			Ar << P.Key << P.Value.Slide << P.Value.TurnDeg << P.Value.Hole << P.Value.bFree << P.Value.Free;
		}
	}
	Ar << B.Dirt;
	Ar << B.Paint;
	int32 NumRemoved = B.Removed.Num();
	Ar << NumRemoved;
	if (Ar.IsLoading())
	{
		B.Removed.Reset();
		B.Removed.SetNum(FMath::Clamp(NumRemoved, 0, 64));
	}
	for (FTorqueRemoved& R : B.Removed) Ar << R.Slot << R.Part;
}
} // namespace

namespace TorqueShedNet
{
TArray<uint8> Write(const FTorqueShedBike& Bike)
{
	TArray<uint8> Out;
	FMemoryWriter Ar(Out);
	int32 Version = Layout;
	Ar << Version;
	Serialize(Ar, const_cast<FTorqueShedBike&>(Bike));
	return Out;
}

bool Read(const TArray<uint8>& Data, FTorqueShedBike& Out)
{
	if (Data.IsEmpty()) return false;
	FMemoryReader Ar(Data);
	int32 Version = 0;
	Ar << Version;
	if (Version != Layout)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque net: the host's shed is from another version of the game (layout %d, this game %d)"), Version, Layout);
		return false;
	}
	FTorqueShedBike B;
	Serialize(Ar, B);
	if (Ar.IsError()) return false;
	Out = MoveTemp(B);
	return true;
}

FTorqueShedRequestNet ToNet(const FTorqueShedRequest& R)
{
	FTorqueShedRequestNet N;
	N.Kind = (uint8)R.Kind;
	N.Bike = R.Bike;
	N.Slot = R.Slot;
	N.Part = R.Part;
	N.BikeId = R.BikeId;
	N.Colour = R.Colour;
	N.Amount = R.Amount;
	return N;
}

FTorqueShedRequest FromNet(const FTorqueShedRequestNet& N)
{
	FTorqueShedRequest R;
	R.Kind = (ETorqueShedAct)FMath::Min<uint8>(N.Kind, (uint8)ETorqueShedAct::RodeOut);
	R.Bike = N.Bike;
	R.Slot = N.Slot;
	R.Part = N.Part;
	R.BikeId = N.BikeId;
	R.Colour = N.Colour;
	R.Amount = FMath::Clamp(N.Amount, 0.0f, 1.0f);
	return R;
}
} // namespace TorqueShedNet
