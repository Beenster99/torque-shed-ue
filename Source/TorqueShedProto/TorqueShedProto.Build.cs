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
			// pasting an address into the menu, and asking the router to open the port (UPnP over HTTP).
			"OnlineSubsystem", "OnlineSubsystemUtils", "Sockets", "Networking", "ApplicationCore", "HTTP",
			// The shed draws catalogue bikes as static meshes built at run time (Shed/TorqueShedCatalogue).
			"MeshDescription", "StaticMeshDescription",
			// AudioMixer: USynthComponent (TorqueAudio). The shed radio decodes its stream itself
			// (TorqueAudio/RadioStream, minimp3), not through the media player, which froze the PC.
			"AudioMixer",
			// Settings > Graphics > Monitor: the game's window moved to a screen.
			"Slate", "SlateCore",
			// The shed TVs' YouTube channel: YouTube's embedded player in the engine's web browser
			// (CEF), drawn into a texture (Shed/TorqueShedScreens).
			"WebBrowser", "UMG",
			// The real world's ground and road, made a piece at a time as you ride (World/TorqueRideWorld).
			"ProceduralMeshComponent" });
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Sim"));
		// Shed/ and other subfolders include each other as "Shed/X.h" from the module root.
		PrivateIncludePaths.Add(ModuleDirectory);
		// The co-op smoothing (Coop/NetSmoother.h) is plain C++ shared with SimTest too.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Coop"));
		// The fitment system (Fit/): plain C++ shared with SimTest's fit_test, plus its Unreal adaptor.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Fit"));
		// SonyPad reads PlayStation pads through Windows HID.
		if (Target.Platform == UnrealTargetPlatform.Win64)
			PublicSystemLibraries.AddRange(new string[] { "hid.lib", "setupapi.lib" });
	}
}
