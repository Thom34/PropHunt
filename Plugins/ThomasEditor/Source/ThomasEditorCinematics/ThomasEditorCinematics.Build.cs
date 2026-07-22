using UnrealBuildTool;

public class ThomasEditorCinematics : ModuleRules
{
    public ThomasEditorCinematics(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "LevelSequence",
            "MovieScene",
            "MovieSceneTracks",
            "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "BlueprintGraph",
            "CinematicCamera",
            "MovieSceneTools",
            "UnrealEd"
        });
    }
}
