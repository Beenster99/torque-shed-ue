using System.IO;
using UnrealBuildTool;

public class TorqueShedProto : ModuleRules
{
	public TorqueShedProto(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The riding sim (Sim/) is plain C++ shared with the standalone test; keep its files apart.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Json" });
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Sim"));
		// SonyPad reads PlayStation pads through Windows HID.
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PublicSystemLibraries.AddRange(new string[] { "hid.lib", "setupapi.lib" });
	}
}
