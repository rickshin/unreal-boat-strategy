using UnrealBuildTool;

public class ArchipelagoCommandTarget : TargetRules
{
	public ArchipelagoCommandTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ArchipelagoCommand");
	}
}
