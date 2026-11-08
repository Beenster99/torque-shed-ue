using System.IO;
using UnrealBuildTool;

public class TorqueShedProto : ModuleRules
{
	public TorqueShedProto(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The riding sim (Sim/) is plain C++ shared with the standalone test; keep its files apart.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Json", "RenderCore", "RHI",
			// Co-op (Coop/): sessions through the online subsystem (Null now, EOS later), this PC's address,
			// pasting an address into the menu.
			"OnlineSubsystem", "OnlineSubsystemUtils", "Sockets", "Networking", "ApplicationCore" });
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Sim"));
		// The co-op smoothing (Coop/NetSmoother.h) is plain C++ shared with SimTest too.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Coop"));
		// SonyPad reads PlayStation pads through Windows HID.
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PublicSystemLibraries.AddRange(new string[] { "hid.lib", "setupapi.lib" });
	}
}
