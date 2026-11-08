#include "TorqueShedNet.h"

#include "Shed/TorqueBuildSubsystem.h"

namespace TorqueShedNet
{
FTorqueShedRequestNet ToNet(const FTorqueShedRequest& R)
{
	FTorqueShedRequestNet N;
	N.Kind = (uint8)R.Kind;
	N.Bay = R.Bay;
	N.Engine = R.Engine;
	N.Slot = R.Slot;
	N.Item = R.Item;
	N.Items = R.Items;
	N.Spot = R.Spot;
	N.Part = R.Part;
	N.Bin = R.Bin;
	N.Finish = R.Finish;
	N.Colour = R.Colour;
	N.Place = R.Place;
	N.bCrouched = R.bCrouched;
	N.Text = R.Text;
	N.bGun = R.bGun;   // [shed]
	N.Amount = R.Amount;   // [shed]
	N.Tilt = R.Tilt;   // [shed]
	return N;
}

FTorqueShedRequest FromNet(const FTorqueShedRequestNet& N)
{
	FTorqueShedRequest R;
	R.Kind = (ETorqueShedAct)FMath::Min<uint8>(N.Kind, (uint8)ETorqueShedAct::Count);   // Count: refused by Check
	R.Bay = N.Bay;
	R.Engine = N.Engine;
	R.Slot = N.Slot;
	R.Item = N.Item;
	R.Items = N.Items;
	if (R.Items.Num() > 256) R.Items.SetNum(256);
	R.Spot = N.Spot;
	R.Part = N.Part;
	R.Bin = N.Bin;
	R.Finish = N.Finish;
	R.Colour = N.Colour;
	R.Place = N.Place;
	R.bCrouched = N.bCrouched;
	R.Text = N.Text;
	R.bGun = N.bGun;   // [shed]
	R.Amount = N.Amount;   // [shed]
	R.Tilt = N.Tilt;   // [shed]
	return R;
}
} // namespace TorqueShedNet
