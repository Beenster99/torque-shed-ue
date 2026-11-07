#include "TorqueGameMode.h"
#include "TorqueBikePawn.h"
#include "TorqueHUD.h"

ATorqueGameMode::ATorqueGameMode()
{
	DefaultPawnClass = ATorqueBikePawn::StaticClass();
	HUDClass = ATorqueHUD::StaticClass();
}
