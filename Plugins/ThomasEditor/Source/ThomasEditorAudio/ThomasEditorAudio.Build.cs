using UnrealBuildTool;

public class ThomasEditorAudio : ModuleRules
{
    public ThomasEditorAudio(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "MetasoundEngine",
            "MetasoundFrontend",
            "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "MetasoundEditor",
            "UnrealEd"
        });
    }
}
