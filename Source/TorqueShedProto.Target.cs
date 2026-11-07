using UnrealBuildTool;

public class TorqueShedProtoTarget : TargetRules
{
	public TorqueShedProtoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TorqueShedProto");
	}
}
