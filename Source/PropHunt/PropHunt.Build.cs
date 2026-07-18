using UnrealBuildTool;

public class PropHunt : ModuleRules
{
	public PropHunt(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
				"PhysicsCore",
				"UMG"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux)
		{
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
		}
	}
}
