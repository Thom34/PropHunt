using UnrealBuildTool;

public class ThomasEditorWorld : ModuleRules
{
    public ThomasEditorWorld(ReadOnlyTargetRules Target) : base(Target)
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
            "AssetRegistry",
            "DataLayerEditor",
            "Foliage",
            "Landscape",
            "NavigationSystem",
            "UnrealEd"
        });
    }
}
