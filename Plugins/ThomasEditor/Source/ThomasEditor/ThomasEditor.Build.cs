using UnrealBuildTool;

public class ThomasEditor : ModuleRules
{
    public ThomasEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "BlueprintEditorLibrary",
            "Json",
            "Kismet",
            "PropHunt",
            "ThomasEditorCore",
            "UnrealEd"
        });
    }
}
