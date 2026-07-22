using UnrealBuildTool;

public class ThomasEditorPCG : ModuleRules
{
    public ThomasEditorPCG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "PCG", "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "UnrealEd"
        });
    }
}
