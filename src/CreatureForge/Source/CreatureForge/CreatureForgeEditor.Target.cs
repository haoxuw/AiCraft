using UnrealBuildTool;
using System.Collections.Generic;

public class CreatureForgeEditorTarget : TargetRules
{
	public CreatureForgeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("CreatureForge");
	}
}
