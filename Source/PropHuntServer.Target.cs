using UnrealBuildTool;
using System.Collections.Generic;

public class PropHuntServerTarget : TargetRules
{
	public PropHuntServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bUseLoggingInShipping = true;
		ExtraModuleNames.Add("PropHunt");
	}
}
