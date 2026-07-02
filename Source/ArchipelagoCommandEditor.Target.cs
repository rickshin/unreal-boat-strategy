using UnrealBuildTool;

public class ArchipelagoCommandEditorTarget : TargetRules
{
	public ArchipelagoCommandEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ArchipelagoCommand");
	}
}
