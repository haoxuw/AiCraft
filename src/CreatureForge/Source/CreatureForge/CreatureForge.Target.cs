using UnrealBuildTool;
using System.Collections.Generic;

public class CreatureForgeTarget : TargetRules
{
	public CreatureForgeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("CreatureForge");
	}
}
