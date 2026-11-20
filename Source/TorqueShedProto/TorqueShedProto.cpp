#include "TorqueShedProto.h"
#include "Modules/ModuleManager.h"
#include "TorqueReports.h"

// The game's module: it starts the crash and freeze reports (TorqueReports.h) in a played game.
class FTorqueShedProtoModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override { TorqueReports::Start(); }
	virtual void ShutdownModule() override { TorqueReports::Stop(); }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FTorqueShedProtoModule, TorqueShedProto, "TorqueShedProto");
