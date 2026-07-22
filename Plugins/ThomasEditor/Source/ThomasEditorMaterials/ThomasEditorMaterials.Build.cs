using UnrealBuildTool;

public class ThomasEditorMaterials : ModuleRules
{
    public ThomasEditorMaterials(ReadOnlyTargetRules Target) : base(Target)
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
            "MaterialEditor",
            "UnrealEd"
        });
    }
}
