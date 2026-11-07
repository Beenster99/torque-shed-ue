using UnrealBuildTool;

public class TorqueShedProtoEditorTarget : TargetRules
{
	public TorqueShedProtoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TorqueShedProto");
	}
}
