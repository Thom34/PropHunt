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
				"HTTP",
				"InputCore",
				"Json",
				"JsonUtilities",
				"OnlineSubsystem",
				"OnlineSubsystemUtils",
				"PhysicsCore",
				"UMG"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreOnline",
				"Slate",
				"SlateCore"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
		}
	}
}
