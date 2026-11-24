#include "Coop/TorqueNetVersion.h"

#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
int32 GBuildRound = 0;
FString GBuildCommit;
bool GBuildRead = false;

void ReadBuild()
{
	if (GBuildRead) return;
	GBuildRead = true;
	// {"round": 169, "commit": "40635ad...", "built": "2026-11-23 19:46"}, written by the PC's packaging step.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/build.json")))) return;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
	Root->TryGetNumberField(TEXT("round"), GBuildRound);
	Root->TryGetStringField(TEXT("commit"), GBuildCommit);
	GBuildCommit = GBuildCommit.Left(7);
}
} // namespace

int32 TorqueNetVersion::Round()
{
	ReadBuild();
	return GBuildRound;
}

FString TorqueNetVersion::BuildName()
{
	ReadBuild();
	if (GBuildRound > 0) return FString::Printf(TEXT("Build %d"), GBuildRound);
	return GBuildCommit.IsEmpty() ? FString(TEXT("a dev build")) : FString::Printf(TEXT("dev build %s"), *GBuildCommit);
}

void TorqueNetVersion::Install()
{
	ReadBuild();
	if (GBuildRound <= 0 && GBuildCommit.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Torque net: no Data/build.json: Unreal's own network version (any copy of this dev build can join)"));
		return;
	}
	// Unreal's own parts of the check (the engine's network changelist and the project's name), and the build.
	const FString Key = FString::Printf(TEXT("%s|%u|%d|%s"), FApp::GetProjectName(), FNetworkVersion::GetNetworkCompatibleChangelist(), GBuildRound, *GBuildCommit);
	const uint32 Version = FCrc::StrCrc32(*Key);
	FNetworkVersion::GetLocalNetworkVersionOverride.BindLambda([Version]() { return Version; });
	FNetworkVersion::InvalidateNetworkChecksum();
	UE_LOG(LogTemp, Log, TEXT("Torque net: network version %u (%s, commit %s): only the same build can join"), Version, *BuildName(),
		GBuildCommit.IsEmpty() ? TEXT("none") : *GBuildCommit);
}
