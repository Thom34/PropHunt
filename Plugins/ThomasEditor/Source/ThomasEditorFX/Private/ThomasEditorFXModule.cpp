#include "ThomasEditorFXProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraParameterCollectionFactoryNew.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraDecalRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraRendererMeshes.h"
#include "NiagaraRendererRibbons.h"
#include "NiagaraRendererSprites.h"
#include "NiagaraRendererVolumes.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "ThomasEditorAssetsProvider.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedFxCreatePlan
{
    FThomasDomainAssetCreateRequest Request;
    FString FactoryClassPath;
    double CreatedAtSeconds = 0.0;
};

struct FCachedNiagaraPatchPlan
{
    FThomasNiagaraPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message = FString())
{
    TResult Result;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

FString NormalizePath(const FString& Input)
{
    const FString Trimmed = Input.TrimStartAndEnd();
    return Trimmed.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Trimmed) : Trimmed;
}

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool IsAllowedPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/PropHunt/"))
        && !Path.StartsWith(TEXT("/Game/Developers/"))
        && !Path.Contains(TEXT(".."));
}

FString FactoryClassForKind(const FString& Kind)
{
    if (Kind == TEXT("niagara_system"))
    {
        return TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew");
    }
    if (Kind == TEXT("niagara_emitter"))
    {
        return TEXT("/Script/NiagaraEditor.NiagaraEmitterFactoryNew");
    }
    if (Kind == TEXT("niagara_parameter_collection"))
    {
        return TEXT("/Script/NiagaraEditor.NiagaraParameterCollectionFactoryNew");
    }
    return FString();
}

UFactory* CreateFactory(const FString& FactoryClassPath)
{
    if (!FModuleManager::Get().LoadModule(TEXT("NiagaraEditor")))
    {
        return nullptr;
    }
    UClass* FactoryClass = LoadObject<UClass>(nullptr, *FactoryClassPath);
    UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
    return Factory && Factory->SupportedClass ? Factory : nullptr;
}

bool SaveAsset(UObject* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    if (!Package)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

UObject* FindAsset(const FString& PackageName)
{
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath(PackageName)));
    return Data.IsValid() ? Data.GetAsset() : nullptr;
}

FString Revision(const UObject* Asset)
{
    const UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    if (!Package)
    {
        return TEXT("missing");
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(TEXT("%s:%lld:%lld:%d"), *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0);
}

void AddBounded(FThomasSpecializedAssetInspectionResult& Result, const FString& Item, const int32 MaxItems)
{
    if (Result.Items.Num() < MaxItems)
    {
        Result.Items.Add(Item.Left(1200));
    }
    else
    {
        Result.bTruncated = true;
    }
}

FString VariableSummary(const FNiagaraVariable& Variable)
{
    return FString::Printf(TEXT("Parameter Name=%s Type=%s"),
        *Variable.GetName().ToString(), *Variable.GetType().GetNameText().ToString());
}

bool ParseGuid(const FString& Text, FGuid& OutGuid)
{
    return Text.IsEmpty() || FGuid::Parse(Text, OutGuid);
}

FString ExternalScriptName(const FString& Input)
{
    const FString Usage = Input.TrimStartAndEnd().ToLower();
    if (Usage == TEXT("system_spawn")) return TEXT("SystemSpawnScript");
    if (Usage == TEXT("system_update")) return TEXT("SystemUpdateScript");
    if (Usage == TEXT("emitter_spawn")) return TEXT("EmitterSpawnScript");
    if (Usage == TEXT("emitter_update")) return TEXT("EmitterUpdateScript");
    if (Usage == TEXT("particle_spawn")) return TEXT("ParticleSpawnScript");
    if (Usage == TEXT("particle_update")) return TEXT("ParticleUpdateScript");
    return FString();
}

TOptional<ENiagaraScriptUsage> ScriptUsage(const FString& Input)
{
    const FString Usage = Input.TrimStartAndEnd().ToLower();
    if (Usage == TEXT("system_spawn")) return ENiagaraScriptUsage::SystemSpawnScript;
    if (Usage == TEXT("system_update")) return ENiagaraScriptUsage::SystemUpdateScript;
    if (Usage == TEXT("emitter_spawn")) return ENiagaraScriptUsage::EmitterSpawnScript;
    if (Usage == TEXT("emitter_update")) return ENiagaraScriptUsage::EmitterUpdateScript;
    if (Usage == TEXT("particle_spawn")) return ENiagaraScriptUsage::ParticleSpawnScript;
    if (Usage == TEXT("particle_update")) return ENiagaraScriptUsage::ParticleUpdateScript;
    return TOptional<ENiagaraScriptUsage>();
}

const FNiagaraTypeDefinition* NiagaraType(const FString& Input)
{
    const FString Type = Input.TrimStartAndEnd().ToLower();
    if (Type == TEXT("float")) return &FNiagaraTypeDefinition::GetFloatDef();
    if (Type == TEXT("int") || Type == TEXT("int32")) return &FNiagaraTypeDefinition::GetIntDef();
    if (Type == TEXT("bool")) return &FNiagaraTypeDefinition::GetBoolDef();
    if (Type == TEXT("vec2") || Type == TEXT("vector2")) return &FNiagaraTypeDefinition::GetVec2Def();
    if (Type == TEXT("vec3") || Type == TEXT("vector")) return &FNiagaraTypeDefinition::GetVec3Def();
    if (Type == TEXT("vec4") || Type == TEXT("vector4")) return &FNiagaraTypeDefinition::GetVec4Def();
    if (Type == TEXT("color")) return &FNiagaraTypeDefinition::GetColorDef();
    if (Type == TEXT("position")) return &FNiagaraTypeDefinition::GetPositionDef();
    if (Type == TEXT("quat")) return &FNiagaraTypeDefinition::GetQuatDef();
    if (Type == TEXT("matrix4")) return &FNiagaraTypeDefinition::GetMatrix4Def();
    return nullptr;
}

bool BuildNiagaraVariant(
    const FString& TypeName,
    const FString& ValueText,
    FNiagaraTypeDefinition& OutType,
    FNiagaraVariant& OutVariant,
    FString& OutError)
{
    const FNiagaraTypeDefinition* Type = NiagaraType(TypeName);
    if (!Type)
    {
        OutError = TEXT("Unsupported Niagara value type: ") + TypeName;
        return false;
    }
    OutType = *Type;
    const FString Value = ValueText.TrimStartAndEnd();
    const FString Lower = Value.ToLower();
    if (*Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        const float Parsed = Value.IsEmpty() ? 0.0f : FCString::Atof(*Value);
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetIntDef())
    {
        const int32 Parsed = Value.IsEmpty() ? 0 : FCString::Atoi(*Value);
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        if (!Value.IsEmpty() && Lower != TEXT("true") && Lower != TEXT("false")
            && Lower != TEXT("1") && Lower != TEXT("0"))
        {
            OutError = TEXT("Boolean Niagara value must be true, false, 1, or 0.");
            return false;
        }
        const FNiagaraBool Parsed(Lower == TEXT("true") || Lower == TEXT("1"));
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        FVector2f Parsed(0.0f);
        if (!Value.IsEmpty() && !Parsed.InitFromString(Value))
        {
            OutError = TEXT("vec2 must use X=... Y=....");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetVec3Def()
        || *Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        FVector3f Parsed(0.0f);
        if (!Value.IsEmpty() && !Parsed.InitFromString(Value))
        {
            OutError = TEXT("vec3/position must use X=... Y=... Z=....");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        FVector4f Parsed(0.0f);
        if (!Value.IsEmpty() && !Parsed.InitFromString(Value))
        {
            OutError = TEXT("vec4 must use X=... Y=... Z=... W=....");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetColorDef())
    {
        FLinearColor Parsed = FLinearColor::Black;
        if (!Value.IsEmpty() && !Parsed.InitFromString(Value))
        {
            OutError = TEXT("color must use R=... G=... B=... A=....");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else if (*Type == FNiagaraTypeDefinition::GetQuatDef())
    {
        FQuat4f Parsed = FQuat4f::Identity;
        if (!Value.IsEmpty() && !Parsed.InitFromString(Value))
        {
            OutError = TEXT("quat must use X=... Y=... Z=... W=....");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    else
    {
        FMatrix44f Parsed = FMatrix44f::Identity;
        if (!Value.IsEmpty())
        {
            OutError = TEXT("matrix4 currently accepts only an empty value for identity.");
            return false;
        }
        OutVariant.SetBytesValue(*Type, Parsed);
    }
    return true;
}

UClass* RendererClass(const FString& Input)
{
    const FString Kind = Input.TrimStartAndEnd().ToLower();
    if (Kind == TEXT("sprite")) return UNiagaraSpriteRendererProperties::StaticClass();
    if (Kind == TEXT("mesh")) return UNiagaraMeshRendererProperties::StaticClass();
    if (Kind == TEXT("ribbon")) return UNiagaraRibbonRendererProperties::StaticClass();
    if (Kind == TEXT("light")) return UNiagaraLightRendererProperties::StaticClass();
    if (Kind == TEXT("decal")) return UNiagaraDecalRendererProperties::StaticClass();
    if (Kind == TEXT("component")) return UNiagaraComponentRendererProperties::StaticClass();
    if (Kind == TEXT("volume")) return UNiagaraVolumeRendererProperties::StaticClass();
    return nullptr;
}

FNiagaraEmitterHandle* FindEmitter(UNiagaraSystem& System, const FString& IdOrName)
{
    FGuid Id;
    const bool bGuid = FGuid::Parse(IdOrName, Id);
    return System.GetEmitterHandles().FindByPredicate([&](const FNiagaraEmitterHandle& Handle)
    {
        return bGuid ? Handle.GetId() == Id : Handle.GetName().ToString() == IdOrName;
    });
}

FString JoinContextErrors(const FNiagaraExternalEditContext& Context)
{
    TArray<FString> Errors;
    for (const FText& Error : Context.Errors)
    {
        Errors.Add(Error.ToString());
    }
    return FString::Join(Errors, TEXT(" | ")).Left(1200);
}

void AddStackItems(
    FThomasSpecializedAssetInspectionResult& Result,
    const FString& EmitterId,
    const FNiagaraExt_ScriptStackTopology& Stack,
    const int32 MaxItems)
{
    for (int32 Index = 0; Index < Stack.Modules.Num(); ++Index)
    {
        const FNiagaraExt_ModuleTopology& Module = Stack.Modules[Index];
        AddBounded(Result, FString::Printf(
            TEXT("Module Emitter=%s Stack=%s Index=%d Name=%s Enabled=%s Script=%s Inputs=%d SetParameters=%s"),
            *EmitterId, *Stack.ScriptName.ToString(), Index, *Module.ModuleName.ToString(),
            Module.Enabled ? TEXT("true") : TEXT("false"),
            Module.ModuleScript ? *Module.ModuleScript->GetPathName() : TEXT("None"),
            Module.Inputs.Num(), Module.bIsSetParametersModule ? TEXT("true") : TEXT("false")), MaxItems);
        for (const FNiagaraExt_StackInputTopology& Input : Module.Inputs)
        {
            AddBounded(Result, FString::Printf(
                TEXT("Input Emitter=%s Stack=%s Module=%s Name=%s Type=%s Visible=%s Editable=%s Dynamic=%s StaticSwitch=%s"),
                *EmitterId, *Stack.ScriptName.ToString(), *Module.ModuleName.ToString(),
                *Input.Name.ToString(), *Input.Type.GetNameText().ToString(),
                Input.bIsVisible ? TEXT("true") : TEXT("false"),
                Input.bIsEditable ? TEXT("true") : TEXT("false"),
                Input.bIsDynamic ? TEXT("true") : TEXT("false"),
                Input.bIsStaticSwitch ? TEXT("true") : TEXT("false")), MaxItems);
        }
    }
}
}

class FThomasEditorFXModule final : public IThomasEditorFXProviderModule
{
public:
    virtual void ShutdownModule() override
    {
        Plans.Reset();
        PatchPlans.Reset();
    }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            FThomasDomainInventoryResult Result;
            Result.Code = TEXT("assets_provider_unavailable");
            return Result;
        }
        return Assets->GetDomainInventory(TEXT("NiagaraFX"), PackageRoot,
            {
                TEXT("/Script/Niagara.NiagaraSystem"), TEXT("/Script/Niagara.NiagaraEmitter"),
                TEXT("/Script/Niagara.NiagaraParameterCollection"), TEXT("/Script/Niagara.NiagaraScript")
            }, MaxResults);
    }

    virtual FThomasSpecializedAssetInspectionResult InspectFxAsset(
        const FString& InputPath,
        const int32 InputMaxItems) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("NiagaraFX");
        Result.AssetPath = NormalizePath(InputPath);
        const int32 MaxItems = FMath::Clamp(InputMaxItems, 1, 1000);
        UObject* Asset = FindAsset(Result.AssetPath);
        if (!Asset)
        {
            Result.Code = TEXT("asset_not_found");
            return Result;
        }
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.Revision = Revision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();

        if (const UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
        {
            Result.AssetKind = TEXT("niagara_system");
            TArray<FNiagaraVariable> Parameters;
            System->GetExposedParameters().GetParameters(Parameters);
            Result.Metrics = {
                FString::Printf(TEXT("EmitterCount=%d"), System->GetEmitterHandles().Num()),
                FString::Printf(TEXT("ExposedParameterCount=%d"), Parameters.Num()),
                FString::Printf(TEXT("SystemSpawnScript=%s"), System->GetSystemSpawnScript() ? *System->GetSystemSpawnScript()->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("SystemUpdateScript=%s"), System->GetSystemUpdateScript() ? *System->GetSystemUpdateScript()->GetPathName() : TEXT("None"))
            };
            for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
            {
                const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Index];
                AddBounded(Result, FString::Printf(TEXT("Emitter[%d] Name=%s Id=%s Enabled=%s Instance=%s"),
                    Index, *Handle.GetName().ToString(), *Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens),
                    Handle.GetIsEnabled() ? TEXT("true") : TEXT("false"),
                    Handle.GetInstance().Emitter ? *Handle.GetInstance().Emitter->GetPathName() : TEXT("None")), MaxItems);
            }
            for (const FNiagaraVariable& Parameter : Parameters)
            {
                AddBounded(Result, VariableSummary(Parameter), MaxItems);
            }

            FNiagaraExternalEditContext Context(const_cast<UNiagaraSystem*>(System));
            for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
            {
                FNiagaraExt_StackItemReference Ref(const_cast<UNiagaraSystem*>(System), Handle.GetName());
                FNiagaraExt_EmitterTopology Topology;
                UNiagaraExternalEditUtilities::GetEmitterTopology(Ref, Topology, Context);
                const FString EmitterId = Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens);
                AddStackItems(Result, EmitterId, Topology.EmitterSpawnScript, MaxItems);
                AddStackItems(Result, EmitterId, Topology.EmitterUpdateScript, MaxItems);
                AddStackItems(Result, EmitterId, Topology.ParticleSpawnScript, MaxItems);
                AddStackItems(Result, EmitterId, Topology.ParticleUpdateScript, MaxItems);
                for (const FNiagaraExt_RendererRef& Renderer : Topology.Renderers)
                {
                    AddBounded(Result, FString::Printf(
                        TEXT("Renderer Emitter=%s Index=%d Class=%s"), *EmitterId,
                        Renderer.RendererIndex,
                        Renderer.RendererClass ? *Renderer.RendererClass->GetPathName() : TEXT("None")), MaxItems);
                }
            }
            FNiagaraExt_SystemCompileState CompileState;
            UNiagaraExternalEditUtilities::GetSystemCompileState(
                const_cast<UNiagaraSystem*>(System), CompileState, Context);
            Result.Metrics.Add(FString::Printf(TEXT("CompileStatus=%s"),
                *StaticEnum<ENiagaraExt_ScriptCompileStatus>()->GetNameStringByValue(
                    static_cast<int64>(CompileState.AggregateStatus))));
            Result.Metrics.Add(FString::Printf(TEXT("CompileIsStale=%s"),
                CompileState.bIsStale ? TEXT("true") : TEXT("false")));
            Result.Metrics.Add(FString::Printf(TEXT("CompileHasErrors=%s"),
                CompileState.bHasErrors ? TEXT("true") : TEXT("false")));
            Result.Metrics.Add(FString::Printf(TEXT("CompileHasWarnings=%s"),
                CompileState.bHasWarnings ? TEXT("true") : TEXT("false")));
            for (const FNiagaraExt_ScriptCompileInfo& ScriptInfo : CompileState.Scripts)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("Compile Emitter=%s Script=%s Status=%s Events=%d Error=%s"),
                    *ScriptInfo.EmitterName.ToString(), *ScriptInfo.ScriptName.ToString(),
                    *StaticEnum<ENiagaraExt_ScriptCompileStatus>()->GetNameStringByValue(
                        static_cast<int64>(ScriptInfo.LastCompileStatus)),
                    ScriptInfo.CompileEvents.Num(), *ScriptInfo.ErrorSummary), MaxItems);
            }
            if (Context.HasErrors())
            {
                Result.Metrics.Add(TEXT("TopologyWarning=") + JoinContextErrors(Context));
            }
        }
        else if (const UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
        {
            Result.AssetKind = TEXT("niagara_emitter");
            Result.Metrics = {
                FString::Printf(TEXT("VersionCount=%d"), Emitter->GetAllAvailableVersions().Num()),
                FString::Printf(TEXT("ExposedVersion=%s"), *Emitter->GetExposedVersion().VersionGuid.ToString(EGuidFormats::DigitsWithHyphens))
            };
        }
        else if (const UNiagaraParameterCollection* Collection = Cast<UNiagaraParameterCollection>(Asset))
        {
            Result.AssetKind = TEXT("niagara_parameter_collection");
            Result.Metrics.Add(FString::Printf(TEXT("ParameterCount=%d"), Collection->GetParameters().Num()));
            for (const FNiagaraVariable& Parameter : Collection->GetParameters())
            {
                AddBounded(Result, VariableSummary(Parameter), MaxItems);
            }
        }
        else if (const UNiagaraScript* Script = Cast<UNiagaraScript>(Asset))
        {
            Result.AssetKind = TEXT("niagara_script");
            Result.Metrics.Add(FString::Printf(TEXT("Usage=%s"),
                *StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage()))));
            Result.Metrics.Add(FString::Printf(TEXT("UsageId=%s"),
                *Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens)));
        }
        else
        {
            Result.Code = TEXT("unsupported_fx_class");
            Result.Message = Result.ClassPath;
            return Result;
        }
        Result.bOk = true;
        return Result;
    }

    virtual FThomasSpecializedAssetInspectionResult SearchNiagaraModules(
        const FString& InputQuery,
        const FString& TargetUsage,
        const int32 InputMaxResults) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("NiagaraModuleSearch");
        Result.AssetKind = TEXT("niagara_module_script");
        const int32 MaxResults = FMath::Clamp(InputMaxResults, 1, 500);
        const FString Query = InputQuery.TrimStartAndEnd();
        FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Options;
        Options.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
        if (!TargetUsage.TrimStartAndEnd().IsEmpty())
        {
            const TOptional<ENiagaraScriptUsage> ParsedUsage = ScriptUsage(TargetUsage);
            if (!ParsedUsage.IsSet())
            {
                Result.Code = TEXT("invalid_target_usage");
                Result.Message = TargetUsage;
                return Result;
            }
            Options.TargetUsageToMatch = ParsedUsage.GetValue();
        }
        Options.bIncludeDeprecatedScripts = false;
        Options.bIncludeNonLibraryScripts = true;
        TArray<FAssetData> Assets;
        FNiagaraEditorUtilities::GetFilteredScriptAssets(Options, Assets);
        Assets.Sort([](const FAssetData& A, const FAssetData& B)
        {
            return A.GetObjectPathString() < B.GetObjectPathString();
        });
        int32 Matched = 0;
        for (const FAssetData& Data : Assets)
        {
            const FString Haystack = Data.AssetName.ToString() + TEXT(" ") + Data.GetObjectPathString();
            if (!Query.IsEmpty() && !Haystack.Contains(Query, ESearchCase::IgnoreCase))
            {
                continue;
            }
            ++Matched;
            AddBounded(Result, FString::Printf(TEXT("Module Name=%s Path=%s Class=%s"),
                *Data.AssetName.ToString(), *Data.GetObjectPathString(), *Data.AssetClassPath.ToString()), MaxResults);
        }
        Result.Metrics = {
            FString::Printf(TEXT("MatchedCount=%d"), Matched),
            FString::Printf(TEXT("ReturnedCount=%d"), Result.Items.Num()),
            FString::Printf(TEXT("TargetUsage=%s"), *TargetUsage)
        };
        Result.bTruncated |= Matched > Result.Items.Num();
        Result.bOk = true;
        return Result;
    }

    virtual FThomasNiagaraPlanResult PlanNiagaraPatch(
        const FThomasNiagaraPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasNiagaraPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasNiagaraPlanResult>(
                TEXT("path_denied"), TEXT("Niagara System must remain under /Game/PropHunt."));
        }
        UNiagaraSystem* System = Cast<UNiagaraSystem>(FindAsset(Request.AssetPath));
        if (!System)
        {
            return MakeError<FThomasNiagaraPlanResult>(TEXT("niagara_system_not_found"), Request.AssetPath);
        }
        const FString CurrentRevision = Revision(System);
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasNiagaraPlanResult>(TEXT("revision_conflict"), CurrentRevision);
        }
        if (System->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasNiagaraPlanResult>(
                TEXT("dirty_package_denied"), TEXT("Save or revert the Niagara System before planning."));
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(
            System->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
        {
            return MakeError<FThomasNiagaraPlanResult>(TEXT("read_only_denied"), Filename);
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > 128)
        {
            return MakeError<FThomasNiagaraPlanResult>(TEXT("invalid_operation_count"),
                FString::FromInt(Request.Operations.Num()));
        }

        FThomasNiagaraPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = CurrentRevision;
        const TSet<FString> Supported = {
            TEXT("add_emitter"), TEXT("remove_emitter"), TEXT("rename_emitter"),
            TEXT("set_emitter_enabled"), TEXT("add_user_parameter"),
            TEXT("remove_user_parameter"), TEXT("rename_user_parameter"),
            TEXT("set_user_parameter"), TEXT("add_module"), TEXT("remove_module"),
            TEXT("set_module_enabled"), TEXT("add_renderer"), TEXT("remove_renderer"),
            TEXT("set_renderer_enabled"), TEXT("compile")
        };
        for (FThomasNiagaraOperation& Operation : Request.Operations)
        {
            Operation.Action = Operation.Action.TrimStartAndEnd().ToLower();
            if (!Supported.Contains(Operation.Action))
            {
                return MakeError<FThomasNiagaraPlanResult>(TEXT("unsupported_operation"), Operation.Action);
            }
            const bool bDestructive = Operation.Action == TEXT("remove_emitter")
                || Operation.Action == TEXT("remove_user_parameter")
                || Operation.Action == TEXT("remove_module")
                || Operation.Action == TEXT("remove_renderer");
            if (bDestructive && !Request.bConfirmDestructive)
            {
                return MakeError<FThomasNiagaraPlanResult>(TEXT("confirmation_required"), Operation.Action);
            }
            if (bDestructive)
            {
                Result.Risk = TEXT("R2");
            }
            if ((Operation.Action.Contains(TEXT("user_parameter"))) && !NiagaraType(Operation.Type))
            {
                return MakeError<FThomasNiagaraPlanResult>(TEXT("invalid_parameter_type"), Operation.Type);
            }
            if (Operation.Action.Contains(TEXT("user_parameter")))
            {
                const FString ParameterName = Operation.Name.IsEmpty() ? Operation.NewName : Operation.Name;
                if (!ParameterName.StartsWith(TEXT("User.")))
                {
                    return MakeError<FThomasNiagaraPlanResult>(
                        TEXT("invalid_user_parameter_name"), TEXT("User parameters must start with User."));
                }
                if (Operation.Action == TEXT("rename_user_parameter")
                    && !Operation.NewName.StartsWith(TEXT("User.")))
                {
                    return MakeError<FThomasNiagaraPlanResult>(
                        TEXT("invalid_user_parameter_name"), TEXT("Renamed user parameters must start with User."));
                }
            }
            if (Operation.Action == TEXT("add_module")
                && (!ScriptUsage(Operation.ScriptUsage).IsSet() || Operation.ScriptAssetPath.IsEmpty()))
            {
                return MakeError<FThomasNiagaraPlanResult>(TEXT("invalid_module_spec"), Operation.ScriptUsage);
            }
            if (Operation.Action == TEXT("add_module") && Operation.ModuleIndex != INDEX_NONE)
            {
                return MakeError<FThomasNiagaraPlanResult>(
                    TEXT("module_index_not_supported"),
                    TEXT("Use ModuleNodeId as the predecessor module name; exact numeric insertion is not stable."));
            }
            if (Operation.Action == TEXT("add_renderer") && !RendererClass(Operation.RendererKind))
            {
                return MakeError<FThomasNiagaraPlanResult>(TEXT("invalid_renderer_kind"), Operation.RendererKind);
            }
            Result.Preview.Add((Operation.Action + TEXT(":") + Operation.Name).Left(500));
        }
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedNiagaraPatchPlan& Plan = PatchPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasNiagaraApplyResult ApplyNiagaraPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedNiagaraPatchPlan Plan;
        if (!PatchPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasNiagaraApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UNiagaraSystem* System = Cast<UNiagaraSystem>(FindAsset(Plan.Request.AssetPath));
        const FString RevisionBefore = Revision(System);
        if (!System || RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasNiagaraApplyResult>(TEXT("revision_conflict"), RevisionBefore);
        }
        UPackage* Package = System->GetOutermost();
        FThomasNiagaraApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = RevisionBefore;
        const FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "NiagaraPatch", "ThomasEditor Niagara Patch"));
        System->Modify();
        Package->Modify();
        auto Rollback = [&]()
        {
            Result.bRolledBack = UPackageTools::ReloadPackages({Package});
        };
        auto Fail = [&](const FString& Code, const FString& Message) -> FThomasNiagaraApplyResult
        {
            Rollback();
            Result.Code = Code;
            Result.Message = Message.Left(1200);
            return Result;
        };

        for (const FThomasNiagaraOperation& Operation : Plan.Request.Operations)
        {
            FNiagaraExternalEditContext Context(System);
            const FString& Action = Operation.Action;
            FNiagaraEmitterHandle* Handle = Operation.EmitterHandleId.IsEmpty()
                ? nullptr : FindEmitter(*System, Operation.EmitterHandleId);
            if (!Operation.EmitterHandleId.IsEmpty() && !Handle
                && Action != TEXT("add_emitter") && Action != TEXT("compile"))
            {
                return Fail(TEXT("emitter_not_found"), Operation.EmitterHandleId);
            }

            if (Action == TEXT("add_emitter"))
            {
                UNiagaraEmitter* Template = Cast<UNiagaraEmitter>(FindAsset(NormalizePath(Operation.EmitterAssetPath)));
                if (!Template)
                {
                    return Fail(TEXT("emitter_asset_not_found"), Operation.EmitterAssetPath);
                }
                FNiagaraExt_EmitterTopology Added;
                UNiagaraExternalEditUtilities::AddEmitter(
                    Template, FName(Operation.Name.IsEmpty() ? Template->GetName() : Operation.Name), Added, Context);
            }
            else if (Action == TEXT("remove_emitter"))
            {
                FNiagaraExt_StackItemReference Ref(System, Handle->GetName());
                UNiagaraExternalEditUtilities::RemoveEmitter(Ref, Context);
            }
            else if (Action == TEXT("rename_emitter"))
            {
                Handle->SetName(FName(Operation.NewName), *System);
            }
            else if (Action == TEXT("set_emitter_enabled"))
            {
                Handle->SetIsEnabled(Operation.bEnabled, *System, false);
            }
            else if (Action == TEXT("add_user_parameter") || Action == TEXT("set_user_parameter"))
            {
                FNiagaraTypeDefinition Type;
                FNiagaraVariant Variant;
                FString Error;
                if (!BuildNiagaraVariant(Operation.Type, Operation.Value, Type, Variant, Error))
                {
                    return Fail(TEXT("invalid_parameter_value"), Error);
                }
                FNiagaraExt_UserVariable Variable;
                Variable.Name = FName(Operation.Name);
                Variable.Type = Type;
                Variable.DefaultValue.Set(Type, Variant);
                UNiagaraExternalEditUtilities::AddUserVariable(System, Variable, Context);
            }
            else if (Action == TEXT("remove_user_parameter"))
            {
                FNiagaraExt_Variable Variable;
                Variable.Name = FName(Operation.Name);
                Variable.Type = *NiagaraType(Operation.Type);
                UNiagaraExternalEditUtilities::RemoveUserVariable(System, Variable, Context);
            }
            else if (Action == TEXT("rename_user_parameter"))
            {
                const FNiagaraVariableBase Existing(*NiagaraType(Operation.Type), FName(Operation.Name));
                if (System->GetExposedParameters().IndexOf(Existing) == INDEX_NONE)
                {
                    return Fail(TEXT("parameter_not_found"), Operation.Name);
                }
                System->GetExposedParameters().RenameParameter(Existing, FName(Operation.NewName));
            }
            else if (Action == TEXT("add_module"))
            {
                const FString StackName = ExternalScriptName(Operation.ScriptUsage);
                const FName EmitterName = Handle ? Handle->GetName() : NAME_None;
                FNiagaraExt_StackItemReference Ref(System, EmitterName, FName(StackName));
                if (!Operation.ModuleNodeId.IsEmpty())
                {
                    Ref.ModuleName = FName(Operation.ModuleNodeId);
                }
                UNiagaraScript* Module = Cast<UNiagaraScript>(FindAsset(NormalizePath(Operation.ScriptAssetPath)));
                if (!Module)
                {
                    return Fail(TEXT("module_asset_not_found"), Operation.ScriptAssetPath);
                }
                FNiagaraExt_ModuleTopology Added;
                UNiagaraExternalEditUtilities::AddModule(Ref, Module, Added, Context);
                if (!Context.HasErrors())
                {
                    Result.Diagnostics.Add(TEXT("AddedModule=") + Added.ModuleName.ToString());
                }
            }
            else if (Action == TEXT("remove_module") || Action == TEXT("set_module_enabled"))
            {
                const FString StackName = ExternalScriptName(Operation.ScriptUsage);
                if (StackName.IsEmpty() || Operation.ModuleNodeId.IsEmpty())
                {
                    return Fail(TEXT("invalid_module_reference"), Operation.ModuleNodeId);
                }
                const FName EmitterName = Handle ? Handle->GetName() : NAME_None;
                FNiagaraExt_StackItemReference Ref(
                    System, EmitterName, FName(StackName), FName(Operation.ModuleNodeId));
                if (Action == TEXT("remove_module"))
                {
                    UNiagaraExternalEditUtilities::RemoveModule(Ref, Context);
                }
                else
                {
                    UNiagaraExternalEditUtilities::SetModuleEnabled(Ref, Operation.bEnabled, Context);
                }
            }
            else if (Action == TEXT("add_renderer"))
            {
                FNiagaraExt_StackItemReference Ref(System, Handle->GetName());
                Ref.RendererIndex = Operation.RendererIndex;
                FNiagaraExt_RendererRef Added;
                UNiagaraExternalEditUtilities::AddRenderer(Ref, RendererClass(Operation.RendererKind), Added, Context);
                if (!Context.HasErrors())
                {
                    Result.Diagnostics.Add(FString::Printf(TEXT("AddedRendererIndex=%d"), Added.RendererIndex));
                }
            }
            else if (Action == TEXT("remove_renderer"))
            {
                FNiagaraExt_StackItemReference Ref(System, Handle->GetName());
                Ref.RendererIndex = Operation.RendererIndex;
                UNiagaraExternalEditUtilities::RemoveRenderer(Ref, Context);
            }
            else if (Action == TEXT("set_renderer_enabled"))
            {
                FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
                UNiagaraRendererProperties* Renderer = Data ? Data->GetRenderer(Operation.RendererIndex) : nullptr;
                if (!Renderer)
                {
                    return Fail(TEXT("renderer_not_found"), FString::FromInt(Operation.RendererIndex));
                }
                Renderer->Modify();
                Renderer->SetIsEnabled(Operation.bEnabled);
            }
            // compile is handled once after the atomic operation batch.

            if (Context.HasErrors())
            {
                return Fail(TEXT("niagara_operation_failed"), JoinContextErrors(Context));
            }
            ++Result.AppliedOperationCount;
        }

        System->PostEditChange();
        Package->MarkPackageDirty();
        System->RequestCompile(true);
        System->WaitForCompilationComplete(false, false);
        Result.bCompiled = true;
        FNiagaraExternalEditContext ValidationContext(System);
        FNiagaraExt_SystemCompileState CompileState;
        UNiagaraExternalEditUtilities::GetSystemCompileState(System, CompileState, ValidationContext);
        Result.Diagnostics.Add(FString::Printf(TEXT("CompileStatus=%s"),
            *StaticEnum<ENiagaraExt_ScriptCompileStatus>()->GetNameStringByValue(
                static_cast<int64>(CompileState.AggregateStatus))));
        for (const FNiagaraExt_ScriptCompileInfo& ScriptInfo : CompileState.Scripts)
        {
            for (const FNiagaraExt_CompileEvent& Event : ScriptInfo.CompileEvents)
            {
                if (Event.Severity == ENiagaraExt_CompileEventSeverity::Error
                    || Event.Severity == ENiagaraExt_CompileEventSeverity::Warning)
                {
                    Result.Diagnostics.Add(FString::Printf(TEXT("%s/%s: %s"),
                        *ScriptInfo.EmitterName.ToString(), *ScriptInfo.ScriptName.ToString(),
                        *Event.Message).Left(1200));
                }
            }
        }
        if (CompileState.bHasErrors || ValidationContext.HasErrors())
        {
            return Fail(TEXT("compile_failed_rolled_back"),
                ValidationContext.HasErrors() ? JoinContextErrors(ValidationContext) : TEXT("Niagara compile has errors."));
        }
        if (bSave && !SaveAsset(System))
        {
            return Fail(TEXT("save_failed_rolled_back"), Plan.Request.AssetPath);
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = Revision(System);
        return Result;
    }

    virtual FThomasDomainAssetCreatePlanResult PlanFxAssetCreate(
        const FThomasDomainAssetCreateRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasDomainAssetCreateRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.AssetKind = Request.AssetKind.TrimStartAndEnd().ToLower();
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("path_denied"), TEXT("Niagara asset must remain under /Game/PropHunt."));
        }
        const FString FactoryClassPath = FactoryClassForKind(Request.AssetKind);
        if (FactoryClassPath.IsEmpty())
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(TEXT("invalid_asset_kind"), Request.AssetKind);
        }
        if (Request.ExpectedRevision != TEXT("missing") || FindAsset(Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("asset_exists_or_revision_conflict"), Revision(FindAsset(Request.AssetPath)));
        }
        if (!CreateFactory(FactoryClassPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("fx_factory_unavailable"), FactoryClassPath);
        }

        FThomasDomainAssetCreatePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.AssetKind = Request.AssetKind;
        Result.ExpectedRevision = TEXT("missing");
        Result.FactoryClassPath = FactoryClassPath;
        FCachedFxCreatePlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.FactoryClassPath = FactoryClassPath;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasDomainAssetCreateApplyResult ApplyFxAssetCreate(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedFxCreatePlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FindAsset(Plan.Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(TEXT("revision_conflict"), TEXT("Asset now exists."));
        }
        UFactory* Factory = CreateFactory(Plan.FactoryClassPath);
        if (!Factory)
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(TEXT("fx_factory_unavailable"), Plan.FactoryClassPath);
        }
        UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
        UObject* Asset = Factory->FactoryCreateNew(Factory->SupportedClass, Package,
            FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
            RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
        if (!Asset)
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(TEXT("create_failed"), Plan.Request.AssetPath);
        }
        FAssetRegistryModule::AssetCreated(Asset);
        Package->MarkPackageDirty();

        FThomasDomainAssetCreateApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.bCreated = true;
        if (bSave && !SaveAsset(Asset))
        {
            Result.Code = TEXT("save_failed_rolled_back");
            Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            return Result;
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = Revision(Asset);
        return Result;
    }

private:
    void PurgeExpiredPlans()
    {
        const double Now = FPlatformTime::Seconds();
        for (auto It = Plans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = PatchPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FCachedFxCreatePlan> Plans;
    TMap<FString, FCachedNiagaraPatchPlan> PatchPlans;
};

IMPLEMENT_MODULE(FThomasEditorFXModule, ThomasEditorFX)
