using UnrealBuildTool;

public class ThomasEditorValidation : ModuleRules
{
    public ThomasEditorValidation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "DataValidation",
            "Engine",
            "MessageLog",
            "UnrealEd"
        });
    }
}
