// The walker at the shed's own kit (TorqueShedKit.h): the switch boxes right of the roller door,
// wheeling the engine stand (hold F on it, F lets go) and the fire extinguisher (F takes it or puts
// it back; the work button sprays).
#include "Shed/TorqueWalker.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedKit.h"
#include "Shed/TorqueShedLayout.h"

bool ATorqueWalker::KitInput(float Dt, bool bF, bool bFDown)
{
	UTorqueBuildSubsystem* B = Shed();
	if (!B) return false;
	const int32 Me = B->Me();
	FString WhyNot;
	// Wheeling the engine stand: F lets go of it where it is.
	if (B->StandPushedBy() == Me)
	{
		bStandArmed = false;
		if (!bF) return false;
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::ReleaseStand;
		R.Place = TorqueShedKit::StandFor(this);
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		else Doing(FString(), 0.0);
		return true;
	}
	// The fire extinguisher in your hands: the work button sprays it (nothing else is worked on while
	// you hold it), F puts it back in its place.
	if (B->ExtinguisherHeldBy() == Me)
	{
		const bool bWant = bWorkHeld && !bF;
		bWorkHeld = bWorkPressed = false;
		if (bWant != bSprayAsked)
		{
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::ExtinguisherSpray;
			R.Item = bWant ? 1 : 0;
			if (B->Request(R, WhyNot))
			{
				bSprayAsked = bWant;
				Doing(bWant ? TEXT("spraying the fire extinguisher") : FString(), bWant ? 30.0 : 0.0);
			}
			else Say(WhyNot, false);
		}
		if (!bF) return false;
		FTorqueShedRequest R;
		R.Kind = ETorqueShedAct::Extinguisher;
		R.Item = 0;
		if (!B->Request(R, WhyNot)) Say(WhyNot, false);
		bSprayAsked = false;
		return true;
	}
	bSprayAsked = false;
	// (F at the switch boxes and the extinguisher's place is Interact's, as at the dyno room's switches.)
	const FTorquePlace* At = Target == ETorqueLook::Place ? TargetPlace : nullptr;
	// The engine stand: hold F on it to take hold of its handle and wheel it; a tap does what F does.
	const bool bAtStand = At && At->Kind == ETorquePlace::Stand && B->HeldBy(Me) == 0 && Wheeling() == INDEX_NONE;
	if (bF && bAtStand)
	{
		bStandArmed = true;
		StandFHeld = 0.0;
		return true;
	}
	if (bStandArmed)
	{
		StandFHeld += Dt;
		if (!bFDown)
		{
			bStandArmed = false;
			Interact();
		}
		else if (StandFHeld >= 0.35)
		{
			bStandArmed = false;
			FTorqueShedRequest R;
			R.Kind = ETorqueShedAct::GrabStand;
			if (!B->Request(R, WhyNot)) Say(WhyNot, false);
			else Doing(TEXT("wheeling the engine stand"), 30.0);
		}
	}
	return false;
}
