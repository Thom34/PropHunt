using UnrealBuildTool;

public class ThomasEditorRender : ModuleRules
{
    public ThomasEditorRender(ReadOnlyTargetRules Target) : base(Target)
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
            "StaticMeshEditor",
            "UnrealEd"
        });
    }
}
