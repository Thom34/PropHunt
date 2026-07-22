using UnrealBuildTool;

public class ThomasEditorPaper2D : ModuleRules
{
    public ThomasEditorPaper2D(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Paper2D",
            "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "Paper2DEditor",
            "UnrealEd"
        });
    }
}
