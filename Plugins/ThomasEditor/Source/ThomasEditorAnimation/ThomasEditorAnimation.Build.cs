using UnrealBuildTool;

public class ThomasEditorAnimation : ModuleRules
{
    public ThomasEditorAnimation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "ThomasEditorCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AnimationBlueprintLibrary", "AnimationCore", "AnimGraph", "AnimGraphRuntime", "AssetRegistry", "BlueprintGraph",
            "ControlRig", "ControlRigDeveloper", "ControlRigEditor", "IKRig", "IKRigEditor", "Kismet",
            "RigVM", "RigVMDeveloper",
            "Slate", "SlateCore", "UnrealEd"
        });
    }
}
