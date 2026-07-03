using UnrealBuildTool;

public class ArchipelagoCommand : ModuleRules
{
	public ArchipelagoCommand(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"Json",
			"Water",
			"Niagara"
		});
	}
}
