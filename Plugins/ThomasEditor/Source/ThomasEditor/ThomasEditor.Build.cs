using UnrealBuildTool;

public class ThomasEditor : ModuleRules
{
    public ThomasEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
            "RemoteControlCommon",
            "UnrealEd"
        });
    }
}
