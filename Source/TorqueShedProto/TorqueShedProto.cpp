#include "TorqueShedProto.h"
#include "Modules/ModuleManager.h"
#include "TorqueReports.h"
#include "Coop/TorqueNetVersion.h"

// The game's module: it starts the crash and freeze reports (TorqueReports.h) in a played game, and
// [co-op] makes the network version the build's own, so only the same build can join (TorqueNetVersion.h).
class FTorqueShedProtoModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		TorqueNetVersion::Install();
		TorqueReports::Start();
	}
	virtual void ShutdownModule() override { TorqueReports::Stop(); }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FTorqueShedProtoModule, TorqueShedProto, "TorqueShedProto");
