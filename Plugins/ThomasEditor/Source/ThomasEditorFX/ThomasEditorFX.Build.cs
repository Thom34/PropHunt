using UnrealBuildTool;

public class ThomasEditorFX : ModuleRules
{
    public ThomasEditorFX(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "Niagara", "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "NiagaraEditor", "UnrealEd"
        });
    }
}
