using UnrealBuildTool;

public class ThomasEditorBlueprint : ModuleRules
{
    public ThomasEditorBlueprint(ReadOnlyTargetRules Target) : base(Target)
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
            "Blutility",
            "BlueprintGraph",
            "Json",
            "Kismet",
            "MovieScene",
            "MovieSceneTracks",
            "SlateCore",
            "UMG",
            "UMGEditor",
            "UnrealEd"
        });
    }
}
