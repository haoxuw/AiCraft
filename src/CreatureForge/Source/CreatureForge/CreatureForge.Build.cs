using UnrealBuildTool;

public class CreatureForge : ModuleRules
{
	public CreatureForge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			// Procedural geometry — spine → implicit surface → dynamic mesh
			"GeometryCore",
			"GeometryFramework",
			"DynamicMesh",
			"GeometryScriptingCore",
			// Runtime rig + control rig for procedural gait
			"ControlRig",
			"RigVM",
			// UMG for editor UI
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
		});
	}
}
