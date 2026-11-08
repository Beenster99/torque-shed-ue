using System.IO;
using UnrealBuildTool;

public class TorqueShedProto : ModuleRules
{
	public TorqueShedProto(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// The riding sim (Sim/) is plain C++ shared with the standalone test; keep its files apart.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Json", "RenderCore", "RHI" });
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Sim"));
		// Shed/ and other subfolders include each other as "Shed/X.h" from the module root.
		PrivateIncludePaths.Add(ModuleDirectory);
		// The fitment system (Fit/): plain C++ shared with SimTest's fit_test, plus its Unreal adaptor.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Fit"));
		// SonyPad reads PlayStation pads through Windows HID.
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PublicSystemLibraries.AddRange(new string[] { "hid.lib", "setupapi.lib" });
	}
}
