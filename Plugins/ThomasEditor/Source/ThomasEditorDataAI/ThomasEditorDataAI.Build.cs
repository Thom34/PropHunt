using UnrealBuildTool;

public class ThomasEditorDataAI : ModuleRules
{
    public ThomasEditorDataAI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "ThomasEditorCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AIModule",
            "AIGraph",
            "AssetRegistry",
            "BehaviorTreeEditor",
            "EnvironmentQueryEditor",
            "Kismet",
            "PropertyBindingUtils",
            "StateTreeEditorModule",
            "StateTreeModule",
            "UnrealEd"
        });
    }
}
