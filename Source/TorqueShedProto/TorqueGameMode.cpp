#include "TorqueGameMode.h"
#include "TorqueBikePawn.h"

ATorqueGameMode::ATorqueGameMode()
{
	DefaultPawnClass = ATorqueBikePawn::StaticClass();
}
