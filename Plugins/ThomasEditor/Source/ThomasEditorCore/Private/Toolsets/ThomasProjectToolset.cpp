#include "Toolsets/ThomasProjectToolset.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "ModuleDescriptor.h"
#include "PluginDescriptor.h"
#include "PluginReferenceDescriptor.h"
#include "ProjectDescriptor.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;

struct FPluginPlan
{
    FString PluginName;
    FString Revision;
    bool bPreviousEnabled = false;
    bool bDesiredEnabled = false;
    double CreatedAtSeconds = 0.0;
};

TMap<FString, FPluginPlan> Plans;

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

FString PluginTypeText(const EPluginType Type)
{
    switch (Type)
    {
    case EPluginType::Engine: return TEXT("Engine");
    case EPluginType::Enterprise: return TEXT("Enterprise");
    case EPluginType::Project: return TEXT("Project");
    case EPluginType::External: return TEXT("External");
    case EPluginType::Mod: return TEXT("Mod");
    default: return TEXT("Unknown");
    }
}

FString ProjectRevision()
{
    const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString ContentHash = LexToString(FMD5Hash::HashFile(*ProjectFile));
    return FString::Printf(
        TEXT("%s:%s:%lld:%lld"),
        *ProjectFile,
        *ContentHash,
        IFileManager::Get().FileSize(*ProjectFile),
        IFileManager::Get().GetTimeStamp(*ProjectFile).ToUnixTimestamp());
}

bool IsCanonicalProjectFile()
{
    const FString Actual = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    const FString Expected = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("PropHunt.uproject"));
    return FPaths::IsSamePath(Actual, Expected)
        && FPaths::GetCleanFilename(Actual) == TEXT("PropHunt.uproject");
}

FPluginDescriptor CurrentPluginDescriptor(const TSharedRef<IPlugin>& Plugin)
{
    FPluginDescriptor Descriptor = Plugin->GetDescriptor();
    FPluginDescriptor DiskDescriptor;
    FText Failure;
    if (DiskDescriptor.Load(Plugin->GetDescriptorFileName(), Failure))
    {
        Descriptor = MoveTemp(DiskDescriptor);
    }
    return Descriptor;
}

TArray<FString> FindRequiredEnabledDependents(const FString& PluginName)
{
    TArray<FString> Dependents;
    for (const TSharedRef<IPlugin>& Candidate : IPluginManager::Get().GetEnabledPlugins())
    {
        if (Candidate->GetName().Equals(PluginName, ESearchCase::IgnoreCase))
        {
            continue;
        }
        const FPluginDescriptor Descriptor = CurrentPluginDescriptor(Candidate);
        const bool bRequiresTarget = Descriptor.Plugins.ContainsByPredicate(
            [&PluginName](const FPluginReferenceDescriptor& Dependency)
            {
                return Dependency.bEnabled
                    && !Dependency.bOptional
                    && Dependency.Name.Equals(PluginName, ESearchCase::IgnoreCase);
            });
        if (bRequiresTarget)
        {
            Dependents.Add(Candidate->GetName());
        }
    }
    Dependents.Sort();
    return Dependents;
}

bool HasExplicitProjectReference(const FString& PluginName)
{
    const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
    return Project && Project->FindPluginReferenceIndex(PluginName) != INDEX_NONE;
}

bool EvaluatePluginMutationPolicy(
    const TSharedRef<IPlugin>& Plugin,
    const bool bDesiredEnabled,
    FString& OutCode,
    FString& OutMessage)
{
    if (!bDesiredEnabled)
    {
        const TArray<FString> Dependents = FindRequiredEnabledDependents(Plugin->GetName());
        if (!Dependents.IsEmpty())
        {
            OutCode = TEXT("required_plugin_disable_denied");
            OutMessage = FString::Printf(
                TEXT("%s is a required non-optional dependency of enabled plugin(s): %s"),
                *Plugin->GetName(),
                *FString::Join(Dependents, TEXT(", ")));
            return false;
        }
        return true;
    }

    if ((Plugin->GetType() == EPluginType::Engine
            || Plugin->GetType() == EPluginType::Enterprise)
        && !HasExplicitProjectReference(Plugin->GetName()))
    {
        OutCode = TEXT("engine_plugin_enable_denied");
        OutMessage = FString::Printf(
            TEXT("%s is an engine plugin without an existing explicit PropHunt.uproject reference."),
            *Plugin->GetName());
        return false;
    }
    return true;
}
}

FThomasPluginDetailsResult UThomasProjectToolset::GetPluginDetails(
    const FString& PluginName)
{
    if (PluginName.IsEmpty() || PluginName.Len() > 128)
    {
        return MakeError<FThomasPluginDetailsResult>(
            TEXT("invalid_plugin_name"), PluginName);
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
    if (!Plugin.IsValid())
    {
        return MakeError<FThomasPluginDetailsResult>(
            TEXT("plugin_not_found"), PluginName);
    }

    const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
    FThomasPluginDetailsResult Result;
    Result.bOk = true;
    Result.Name = Plugin->GetName();
    Result.FriendlyName = Plugin->GetFriendlyName();
    Result.VersionName = Descriptor.VersionName;
    Result.Type = PluginTypeText(Plugin->GetType());
    Result.DescriptorFile = Plugin->GetDescriptorFileName();
    Result.bEnabled = Plugin->IsEnabled();
    Result.bMounted = Plugin->IsMounted();
    Result.bEnabledByDefault = Plugin->IsEnabledByDefault(true);
    Result.bCanContainContent = Plugin->CanContainContent();
    Result.bExplicitlyLoaded = Descriptor.bExplicitlyLoaded;
    Result.bBeta = Descriptor.bIsBetaVersion;
    Result.bExperimental = Descriptor.bIsExperimentalVersion;
    Result.bInstalled = Descriptor.bInstalled;
    Result.bHidden = Plugin->IsHidden();
    Result.bSealed = Descriptor.bIsSealed;

    for (const FModuleDescriptor& Module : Descriptor.Modules)
    {
        Result.Modules.Add(FString::Printf(
            TEXT("%s|%s|%s"),
            *Module.Name.ToString(),
            EHostType::ToString(Module.Type),
            ELoadingPhase::ToString(Module.LoadingPhase)));
    }
    for (const FPluginReferenceDescriptor& Dependency : Descriptor.Plugins)
    {
        Result.Dependencies.Add(FString::Printf(
            TEXT("%s|enabled=%s|optional=%s"),
            *Dependency.Name,
            Dependency.bEnabled ? TEXT("true") : TEXT("false"),
            Dependency.bOptional ? TEXT("true") : TEXT("false")));
    }

    if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
    {
        const int32 Index = Project->FindPluginReferenceIndex(PluginName);
        if (Project->Plugins.IsValidIndex(Index))
        {
            Result.bExplicitProjectReference = true;
            Result.bProjectReferenceEnabled = Project->Plugins[Index].bEnabled;
        }
    }
    return Result;
}

FThomasPluginChangePlanResult UThomasProjectToolset::PlanPluginEnabled(
    const FString& PluginName,
    const bool bEnabled,
    const bool bConfirmProjectFileMutation)
{
    if (!bConfirmProjectFileMutation)
    {
        return MakeError<FThomasPluginChangePlanResult>(
            TEXT("confirmation_required"),
            TEXT("Confirm the R2 PropHunt.uproject mutation explicitly."));
    }
    if (!IsCanonicalProjectFile())
    {
        return MakeError<FThomasPluginChangePlanResult>(
            TEXT("canonical_project_mismatch"), FPaths::GetProjectFilePath());
    }
    if (PluginName == TEXT("ThomasEditor"))
    {
        return MakeError<FThomasPluginChangePlanResult>(
            TEXT("self_mutation_denied"),
            TEXT("ThomasEditor cannot disable or rewrite its own project reference."));
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
    if (!Plugin.IsValid())
    {
        return MakeError<FThomasPluginChangePlanResult>(
            TEXT("plugin_not_found"), PluginName);
    }
    if (Plugin->IsEnabled() == bEnabled)
    {
        return MakeError<FThomasPluginChangePlanResult>(
            TEXT("no_change"), bEnabled ? TEXT("Already enabled.") : TEXT("Already disabled."));
    }
    FString PolicyCode;
    FString PolicyMessage;
    if (!EvaluatePluginMutationPolicy(
            Plugin.ToSharedRef(), bEnabled, PolicyCode, PolicyMessage))
    {
        return MakeError<FThomasPluginChangePlanResult>(PolicyCode, PolicyMessage);
    }

    FThomasPluginChangePlanResult Result;
    Result.bOk = true;
    Result.PluginName = PluginName;
    Result.ProjectRevision = ProjectRevision();
    Result.bCurrentEnabled = Plugin->IsEnabled();
    Result.bDesiredEnabled = bEnabled;
    Result.Warnings.Add(TEXT("PropHunt.uproject will be rewritten."));
    Result.Warnings.Add(TEXT("The effective plugin set changes only after restarting Unreal Editor."));
    Result.Warnings.Add(TEXT("Required plugin dependencies are derived from current .uplugin descriptors and checked again at Apply."));

    Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FPluginPlan Plan;
    Plan.PluginName = PluginName;
    Plan.Revision = Result.ProjectRevision;
    Plan.bPreviousEnabled = Result.bCurrentEnabled;
    Plan.bDesiredEnabled = bEnabled;
    Plan.CreatedAtSeconds = FPlatformTime::Seconds();
    Plans.Add(Result.PlanId, MoveTemp(Plan));
    return Result;
}

FThomasPluginChangeApplyResult UThomasProjectToolset::ApplyPluginPlan(
    const FString& PlanId)
{
    FPluginPlan Plan;
    if (FPluginPlan* Found = Plans.Find(PlanId))
    {
        Plan = MoveTemp(*Found);
        Plans.Remove(PlanId);
    }
    else
    {
        return MakeError<FThomasPluginChangeApplyResult>(
            TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
    }
    if (FPlatformTime::Seconds() - Plan.CreatedAtSeconds > PlanLifetimeSeconds)
    {
        return MakeError<FThomasPluginChangeApplyResult>(
            TEXT("plan_expired"), TEXT("Create a fresh plan."));
    }
    if (!IsCanonicalProjectFile() || ProjectRevision() != Plan.Revision)
    {
        return MakeError<FThomasPluginChangeApplyResult>(
            TEXT("revision_conflict"), ProjectRevision());
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Plan.PluginName);
    if (!Plugin.IsValid() || Plugin->IsEnabled() != Plan.bPreviousEnabled)
    {
        return MakeError<FThomasPluginChangeApplyResult>(
            TEXT("plugin_state_conflict"), Plan.PluginName);
    }
    FString PolicyCode;
    FString PolicyMessage;
    if (!EvaluatePluginMutationPolicy(
            Plugin.ToSharedRef(), Plan.bDesiredEnabled, PolicyCode, PolicyMessage))
    {
        return MakeError<FThomasPluginChangeApplyResult>(PolicyCode, PolicyMessage);
    }

    FThomasPluginChangeApplyResult Result;
    Result.PluginName = Plan.PluginName;
    Result.bRestartRequired = true;

    FText Failure;
    if (!IProjectManager::Get().SetPluginEnabled(
            Plan.PluginName, Plan.bDesiredEnabled, Failure))
    {
        return MakeError<FThomasPluginChangeApplyResult>(
            TEXT("set_plugin_failed"), Failure.ToString());
    }
    if (!IProjectManager::Get().SaveCurrentProjectToDisk(Failure))
    {
        FText RollbackFailure;
        const bool bRestored = IProjectManager::Get().SetPluginEnabled(
            Plan.PluginName, Plan.bPreviousEnabled, RollbackFailure);
        const bool bRollbackSaved = bRestored
            && IProjectManager::Get().SaveCurrentProjectToDisk(RollbackFailure);
        Result.Code = bRollbackSaved
            ? TEXT("save_failed_rolled_back")
            : TEXT("rollback_failed");
        Result.Message = Failure.ToString();
        Result.bRolledBack = bRollbackSaved;
        Result.bEnabled = Plan.bPreviousEnabled;
        Result.ProjectRevision = ProjectRevision();
        return Result;
    }

    Result.bOk = true;
    Result.bEnabled = Plan.bDesiredEnabled;
    Result.bSaved = true;
    Result.ProjectRevision = ProjectRevision();
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
bool UThomasProjectToolset::ExpirePluginPlanForTests(const FString& PlanId)
{
    if (FPluginPlan* Plan = Plans.Find(PlanId))
    {
        Plan->CreatedAtSeconds -= PlanLifetimeSeconds + 1.0;
        return true;
    }
    return false;
}
#endif
