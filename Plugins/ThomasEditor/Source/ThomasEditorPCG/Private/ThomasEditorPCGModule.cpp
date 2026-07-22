#include "ThomasEditorPCGProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PCGData.h"
#include "PCGDefaultExecutionSource.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "ScopedTransaction.h"
#include "Subsystems/IPCGBaseSubsystem.h"
#include "Subsystems/PCGEngineSubsystem.h"
#include "ThomasEditorAssetsProvider.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;
constexpr double CompletedJobLifetimeSeconds = 900.0;
constexpr int32 MaxOperations = 100;
constexpr int32 MaxExecutionJobs = 32;

struct FPCGCreatePlan
{
    FThomasDomainAssetCreateRequest Request;
    FString ContextRevision;
    double CreatedAtSeconds = 0.0;
};

struct FPCGPatchPlan
{
    FThomasPCGPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FPCGExecutionPlan
{
    FThomasPCGExecutionPlanRequest Request;
    double CreatedAtSeconds = 0.0;
};

struct FPCGExecutionJobState
{
    FString Status = TEXT("running");
    bool bCancelRequested = false;
    int32 TaggedDataCount = 0;
    int32 NullDataCount = 0;
    double CompletedAtSeconds = 0.0;
    TArray<FString> Outputs;
    TArray<FString> Diagnostics;
};

struct FPCGExecutionJob
{
    FThomasPCGExecutionPlanRequest Request;
    TWeakObjectPtr<UPCGDefaultExecutionSource> Source;
    TSharedPtr<FPCGExecutionJobState> State;
    FString TaskId;
    double StartedAtSeconds = 0.0;
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
    return Trimmed.Contains(TEXT("."))
        ? FPackageName::ObjectPathToPackageName(Trimmed) : Trimmed;
}

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool IsAllowedPath(const FString& Path)
{
    FText Reason;
    return FPackageName::IsValidLongPackageName(Path, false, &Reason)
        && Path.StartsWith(TEXT("/Game/PropHunt/"))
        && !Path.Contains(TEXT("/Developers/"));
}

UPCGGraphInterface* FindGraphInterface(const FString& PackageName)
{
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Data = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPath(PackageName)));
    return Data.IsValid() ? Cast<UPCGGraphInterface>(Data.GetAsset()) : nullptr;
}

UPCGGraph* FindGraph(const FString& PackageName)
{
    return Cast<UPCGGraph>(FindGraphInterface(PackageName));
}

bool SaveGraph(UPCGGraphInterface* GraphInterface)
{
    UPackage* Package = GraphInterface ? GraphInterface->GetOutermost() : nullptr;
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
    return UPackage::SavePackage(Package, GraphInterface, *Filename, Args);
}

FString ExportBagValue(
    const FInstancedPropertyBag& Bag,
    const FPropertyBagPropertyDesc& Desc,
    UObject* Owner)
{
    const FProperty* Property = Desc.CachedProperty;
    const uint8* Memory = Bag.GetValue().GetMemory();
    if (!Property || !Memory)
    {
        return FString();
    }
    FString Value;
    Property->ExportTextItem_Direct(
        Value,
        Property->ContainerPtrToValuePtr<void>(Memory),
        nullptr,
        Owner,
        PPF_None);
    return Value;
}

bool ImportBagValue(
    FInstancedPropertyBag& Bag,
    const FName ParameterName,
    const FString& Value,
    UObject* Owner)
{
    const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(ParameterName);
    FProperty* Property = Desc && Desc->CachedProperty
        ? const_cast<FProperty*>(Desc->CachedProperty)
        : nullptr;
    uint8* Memory = Bag.GetMutableValue().GetMemory();
    return Property
        && Memory
        && Property->ImportText_Direct(
            *Value,
            Property->ContainerPtrToValuePtr<void>(Memory),
            Owner,
            PPF_None) != nullptr;
}

const FPropertyBagPropertyDesc* FindBagDescExact(
    const FInstancedPropertyBag& Bag,
    const FString& ParameterName)
{
    const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
    if (!BagStruct)
    {
        return nullptr;
    }
    for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
    {
        if (Desc.Name.ToString() == ParameterName)
        {
            return &Desc;
        }
    }
    return nullptr;
}

FString ParameterTypeName(const FPropertyBagPropertyDesc& Desc)
{
    const UEnum* Enum = StaticEnum<EPropertyBagPropertyType>();
    return Enum
        ? Enum->GetNameStringByValue(static_cast<int64>(Desc.ValueType))
        : TEXT("Unknown");
}

bool ResolveParameterType(
    const FString& InputType,
    const FString& InputTypeObjectPath,
    EPropertyBagPropertyType& OutType,
    const UObject*& OutTypeObject)
{
    const FString Type = InputType.TrimStartAndEnd().ToLower();
    OutTypeObject = nullptr;
    if (Type == TEXT("bool")) OutType = EPropertyBagPropertyType::Bool;
    else if (Type == TEXT("int32")) OutType = EPropertyBagPropertyType::Int32;
    else if (Type == TEXT("int64")) OutType = EPropertyBagPropertyType::Int64;
    else if (Type == TEXT("float")) OutType = EPropertyBagPropertyType::Float;
    else if (Type == TEXT("double")) OutType = EPropertyBagPropertyType::Double;
    else if (Type == TEXT("name")) OutType = EPropertyBagPropertyType::Name;
    else if (Type == TEXT("string")) OutType = EPropertyBagPropertyType::String;
    else if (Type == TEXT("text")) OutType = EPropertyBagPropertyType::Text;
    else if (Type == TEXT("enum")) OutType = EPropertyBagPropertyType::Enum;
    else if (Type == TEXT("struct")) OutType = EPropertyBagPropertyType::Struct;
    else if (Type == TEXT("object")) OutType = EPropertyBagPropertyType::Object;
    else if (Type == TEXT("soft_object")) OutType = EPropertyBagPropertyType::SoftObject;
    else if (Type == TEXT("class")) OutType = EPropertyBagPropertyType::Class;
    else if (Type == TEXT("soft_class")) OutType = EPropertyBagPropertyType::SoftClass;
    else return false;

    if (OutType == EPropertyBagPropertyType::Enum)
    {
        OutTypeObject = LoadObject<UEnum>(nullptr, *InputTypeObjectPath);
    }
    else if (OutType == EPropertyBagPropertyType::Struct)
    {
        OutTypeObject = LoadObject<UScriptStruct>(nullptr, *InputTypeObjectPath);
    }
    else if (OutType == EPropertyBagPropertyType::Object
        || OutType == EPropertyBagPropertyType::SoftObject
        || OutType == EPropertyBagPropertyType::Class
        || OutType == EPropertyBagPropertyType::SoftClass)
    {
        OutTypeObject = InputTypeObjectPath.IsEmpty()
            ? UObject::StaticClass()
            : LoadObject<UClass>(nullptr, *InputTypeObjectPath);
    }

    return (OutType != EPropertyBagPropertyType::Enum
            && OutType != EPropertyBagPropertyType::Struct
            && OutType != EPropertyBagPropertyType::Object
            && OutType != EPropertyBagPropertyType::SoftObject
            && OutType != EPropertyBagPropertyType::Class
            && OutType != EPropertyBagPropertyType::SoftClass)
        || OutTypeObject != nullptr;
}

TOptional<EPCGGraphUsage> ParseGraphUsage(const FString& Input)
{
    const FString Usage = Input.TrimStartAndEnd().ToLower();
    if (Usage == TEXT("standard")) return EPCGGraphUsage::Standard;
    if (Usage == TEXT("asset")) return EPCGGraphUsage::Asset;
    if (Usage == TEXT("level")) return EPCGGraphUsage::Level;
    return TOptional<EPCGGraphUsage>();
}

uint32 HashParameters(const UPCGGraphInterface* Interface)
{
    const FInstancedPropertyBag* Bag = Interface ? Interface->GetUserParametersStruct() : nullptr;
    const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
    uint32 Hash = 0;
    if (!Bag || !BagStruct)
    {
        return Hash;
    }
    for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
    {
        FString Signature = FString::Printf(
            TEXT("%s|%s|%s|%s"),
            *Desc.Name.ToString(),
            *ParameterTypeName(Desc),
            Desc.ValueTypeObject ? *Desc.ValueTypeObject->GetPathName() : TEXT("None"),
            *ExportBagValue(*Bag, Desc, const_cast<UPCGGraphInterface*>(Interface)));
        if (const UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface))
        {
            Signature += Instance->IsPropertyOverridden(Desc.CachedProperty)
                ? TEXT("|override")
                : TEXT("|inherited");
        }
        Hash = HashCombine(Hash, GetTypeHash(Signature));
    }
    return Hash;
}

FString ExportProperty(const UObject* Object, const FProperty* Property)
{
    if (!Object || !Property)
    {
        return FString();
    }
    FString Value;
    Property->ExportTextItem_Direct(
        Value,
        Property->ContainerPtrToValuePtr<void>(Object),
        nullptr,
        const_cast<UObject*>(Object),
        PPF_None);
    return Value;
}

bool IsEditableSettingsProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnInstance)
        && !CastField<FArrayProperty>(Property)
        && !CastField<FSetProperty>(Property)
        && !CastField<FMapProperty>(Property);
}

bool ImportProperty(UObject* Object, FProperty* Property, const FString& Value)
{
    if (!IsEditableSettingsProperty(Property))
    {
        return false;
    }
    return Property->ImportText_Direct(
        *Value,
        Property->ContainerPtrToValuePtr<void>(Object),
        Object,
        PPF_None) != nullptr;
}

void AppendGraphNodes(const UPCGGraph* Graph, TArray<const UPCGNode*>& OutNodes)
{
    if (!Graph)
    {
        return;
    }
    if (Graph->GetInputNode()) OutNodes.Add(Graph->GetInputNode());
    for (const UPCGNode* Node : Graph->GetNodes())
    {
        if (Node) OutNodes.Add(Node);
    }
    if (Graph->GetOutputNode()) OutNodes.Add(Graph->GetOutputNode());
}

UPCGNode* ResolveNode(
    UPCGGraph* Graph,
    const FString& Path,
    const FString& Handle,
    const TMap<FString, UPCGNode*>& Handles)
{
    if (!Handle.IsEmpty())
    {
        return Handles.FindRef(Handle);
    }
    if (!Graph)
    {
        return nullptr;
    }
    if (Path.Equals(TEXT("input"), ESearchCase::IgnoreCase)) return Graph->GetInputNode();
    if (Path.Equals(TEXT("output"), ESearchCase::IgnoreCase)) return Graph->GetOutputNode();
    TArray<const UPCGNode*> Nodes;
    AppendGraphNodes(Graph, Nodes);
    for (const UPCGNode* Node : Nodes)
    {
        if (Node && Node->GetPathName() == Path)
        {
            return const_cast<UPCGNode*>(Node);
        }
    }
    return nullptr;
}

FString GraphRevision(const UPCGGraph* Graph)
{
    const UPackage* Package = Graph ? Graph->GetOutermost() : nullptr;
    if (!Package)
    {
        return TEXT("missing");
    }
    uint32 StructuralHash = HashCombine(
        GetTypeHash(static_cast<uint8>(Graph->GetGraphUsage())),
        HashParameters(Graph));
    TArray<const UPCGNode*> Nodes;
    AppendGraphNodes(Graph, Nodes);
    for (const UPCGNode* Node : Nodes)
    {
        if (!Node) continue;
        int32 X = 0;
        int32 Y = 0;
        Node->GetNodePosition(X, Y);
        FString Signature = FString::Printf(
            TEXT("%s|%s|%d|%d"),
            *Node->GetPathName(),
            Node->GetSettings() ? *Node->GetSettings()->GetClass()->GetPathName() : TEXT("None"),
            X,
            Y);
        if (const UPCGSettings* Settings = Node->GetSettings())
        {
            for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
            {
                if (IsEditableSettingsProperty(*It))
                {
                    Signature += TEXT("|") + It->GetName() + TEXT("=")
                        + ExportProperty(Settings, *It);
                }
            }
        }
        for (const UPCGPin* Pin : Node->GetOutputPins())
        {
            if (!Pin) continue;
            for (const UPCGEdge* Edge : Pin->Edges)
            {
                const UPCGPin* Other = Edge ? Edge->GetOtherPin(Pin) : nullptr;
                if (Other && Other->Node)
                {
                    Signature += FString::Printf(
                        TEXT("|%s>%s:%s"),
                        *Pin->Properties.Label.ToString(),
                        *Other->Node->GetPathName(),
                        *Other->Properties.Label.ToString());
                }
            }
        }
        StructuralHash = HashCombine(StructuralHash, GetTypeHash(Signature));
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%u"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        StructuralHash);
}

FString GraphInterfaceRevision(const UPCGGraphInterface* Interface)
{
    if (const UPCGGraph* Graph = Cast<UPCGGraph>(Interface))
    {
        return GraphRevision(Graph);
    }
    const UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface);
    const UPackage* Package = Instance ? Instance->GetOutermost() : nullptr;
    if (!Instance || !Package || !Instance->GetGraph())
    {
        return TEXT("missing");
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    const uint32 InstanceHash = HashCombine(
        HashParameters(Instance),
        HashCombine(
            GetTypeHash(Instance->Graph ? Instance->Graph->GetPathName() : FString(TEXT("None"))),
            GetTypeHash(GraphRevision(Instance->GetGraph()))));
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%u"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        InstanceHash);
}

void AddBounded(
    FThomasSpecializedAssetInspectionResult& Result,
    const FString& Item,
    const int32 MaxItems)
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

bool EdgeExists(const UPCGPin* FromPin, const UPCGPin* ToPin)
{
    if (!FromPin || !ToPin)
    {
        return false;
    }
    return FromPin->Edges.ContainsByPredicate(
        [FromPin, ToPin](const UPCGEdge* Edge)
        {
            return Edge && Edge->GetOtherPin(FromPin) == ToPin;
        });
}
}

class FThomasEditorPCGModule final : public IThomasEditorPCGProviderModule
{
public:
    virtual void ShutdownModule() override
    {
        for (TPair<FString, FPCGExecutionJob>& Pair : ExecutionJobs)
        {
            if (UPCGDefaultExecutionSource* Source = Pair.Value.Source.Get())
            {
                if (Source->GetExecutionState().IsGenerating())
                {
                    Source->GetExecutionState().Cancel();
                }
            }
        }
        CreatePlans.Reset();
        PatchPlans.Reset();
        ExecutionPlans.Reset();
        ExecutionJobs.Reset();
    }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            return MakeError<FThomasDomainInventoryResult>(TEXT("assets_provider_unavailable"));
        }
        return Assets->GetDomainInventory(
            TEXT("PCG"),
            PackageRoot,
            {TEXT("/Script/PCG.PCGGraph"), TEXT("/Script/PCG.PCGGraphInstance")},
            MaxResults);
    }

    virtual FThomasSpecializedAssetInspectionResult InspectPCGGraph(
        const FString& InputPath,
        const int32 InputMaxItems) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("PCG");
        Result.AssetPath = NormalizePath(InputPath);
        const int32 MaxItems = FMath::Clamp(InputMaxItems, 1, 1000);
        UPCGGraphInterface* Interface = FindGraphInterface(Result.AssetPath);
        UPCGGraph* Graph = Interface ? Interface->GetMutablePCGGraph() : nullptr;
        if (!Interface || !Graph)
        {
            Result.Code = TEXT("pcg_graph_interface_not_found");
            return Result;
        }
        Result.bOk = true;
        Result.ClassPath = Interface->GetClass()->GetPathName();
        Result.AssetKind = Interface->IsA<UPCGGraphInstance>()
            ? TEXT("pcg_graph_instance")
            : TEXT("pcg_graph");
        Result.Revision = GraphInterfaceRevision(Interface);
        Result.bDirty = Interface->GetOutermost()->IsDirty();

        const FInstancedPropertyBag* Parameters = Interface->GetUserParametersStruct();
        const UPropertyBag* ParameterStruct = Parameters ? Parameters->GetPropertyBagStruct() : nullptr;
        int32 ParameterCount = 0;
        int32 OverrideCount = 0;
        if (Parameters && ParameterStruct)
        {
            for (const FPropertyBagPropertyDesc& Desc : ParameterStruct->GetPropertyDescs())
            {
                const UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface);
                const bool bOverridden = Instance
                    && Desc.CachedProperty
                    && Instance->IsPropertyOverridden(Desc.CachedProperty);
                ++ParameterCount;
                OverrideCount += bOverridden ? 1 : 0;
                AddBounded(Result, FString::Printf(
                    TEXT("GraphParameter Name=%s Type=%s TypeObject=%s Value=%s Overridden=%s"),
                    *Desc.Name.ToString(),
                    *ParameterTypeName(Desc),
                    Desc.ValueTypeObject ? *Desc.ValueTypeObject->GetPathName() : TEXT("None"),
                    *ExportBagValue(*Parameters, Desc, Interface),
                    bOverridden ? TEXT("true") : TEXT("false")),
                    MaxItems);
            }
        }

        TArray<const UPCGNode*> Nodes;
        AppendGraphNodes(Graph, Nodes);
        int32 EdgeCount = 0;
        for (const UPCGNode* Node : Nodes)
        {
            if (!Node) continue;
            int32 X = 0;
            int32 Y = 0;
            Node->GetNodePosition(X, Y);
            AddBounded(Result, FString::Printf(
                TEXT("Node Path=%s Title=%s SettingsClass=%s Position=%d,%d InputPins=%d OutputPins=%d"),
                *Node->GetPathName(),
                *Node->GetAuthoredTitleName().ToString(),
                Node->GetSettings() ? *Node->GetSettings()->GetClass()->GetPathName() : TEXT("None"),
                X,
                Y,
                Node->GetInputPins().Num(),
                Node->GetOutputPins().Num()), MaxItems);
            if (const UPCGSettings* Settings = Node->GetSettings())
            {
                for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
                {
                    const FProperty* Property = *It;
                    if (IsEditableSettingsProperty(Property))
                    {
                        AddBounded(Result, FString::Printf(
                            TEXT("SettingProperty Node=%s Name=%s Type=%s Value=%s"),
                            *Node->GetPathName(),
                            *Property->GetName(),
                            *Property->GetClass()->GetName(),
                            *ExportProperty(Settings, Property)), MaxItems);
                    }
                }
            }
            for (const UPCGPin* Pin : Node->GetInputPins())
            {
                if (Pin)
                {
                    AddBounded(Result, FString::Printf(
                        TEXT("InputPin Node=%s Label=%s Edges=%d Multiple=%s"),
                        *Node->GetPathName(),
                        *Pin->Properties.Label.ToString(),
                        Pin->EdgeCount(),
                        Pin->AllowsMultipleConnections() ? TEXT("true") : TEXT("false")), MaxItems);
                }
            }
            for (const UPCGPin* Pin : Node->GetOutputPins())
            {
                if (!Pin) continue;
                AddBounded(Result, FString::Printf(
                    TEXT("OutputPin Node=%s Label=%s Edges=%d Multiple=%s"),
                    *Node->GetPathName(),
                    *Pin->Properties.Label.ToString(),
                    Pin->EdgeCount(),
                    Pin->AllowsMultipleConnections() ? TEXT("true") : TEXT("false")), MaxItems);
                EdgeCount += Pin->EdgeCount();
                for (const UPCGEdge* Edge : Pin->Edges)
                {
                    const UPCGPin* Other = Edge ? Edge->GetOtherPin(Pin) : nullptr;
                    if (Other && Other->Node)
                    {
                        AddBounded(Result, FString::Printf(
                            TEXT("Edge FromNode=%s FromPin=%s ToNode=%s ToPin=%s"),
                            *Node->GetPathName(),
                            *Pin->Properties.Label.ToString(),
                            *Other->Node->GetPathName(),
                            *Other->Properties.Label.ToString()), MaxItems);
                    }
                }
            }
        }
        Result.Metrics = {
            FString::Printf(TEXT("InterfacePath=%s"), *Interface->GetPathName()),
            FString::Printf(TEXT("BaseInterface=%s"),
                Cast<UPCGGraphInstance>(Interface) && Cast<UPCGGraphInstance>(Interface)->Graph
                    ? *Cast<UPCGGraphInstance>(Interface)->Graph->GetPathName()
                    : TEXT("None")),
            FString::Printf(TEXT("ParameterCount=%d"), ParameterCount),
            FString::Printf(TEXT("OverrideCount=%d"), OverrideCount),
            FString::Printf(TEXT("NodeCount=%d"), Nodes.Num()),
            FString::Printf(TEXT("UserNodeCount=%d"), Graph->GetNodes().Num()),
            FString::Printf(TEXT("EdgeCount=%d"), EdgeCount),
            FString::Printf(TEXT("GraphUsage=%s"), *UEnum::GetValueAsString(Graph->GetGraphUsage()))
        };
        return Result;
    }

    virtual FThomasDomainAssetCreatePlanResult PlanPCGAssetCreate(
        const FThomasDomainAssetCreateRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasDomainAssetCreateRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.ContextAssetPath = NormalizePath(Request.ContextAssetPath);
        Request.AssetKind = Request.AssetKind.ToLower();
        if (Request.AssetKind != TEXT("pcg_graph")
            && Request.AssetKind != TEXT("pcg_graph_instance"))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("unsupported_asset_kind"), Request.AssetKind);
        }
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("asset_path_not_allowed"), Request.AssetPath);
        }
        if (Request.ExpectedRevision != TEXT("missing") || FindGraphInterface(Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("asset_exists_or_revision_conflict"), Request.AssetPath);
        }
        UPCGGraphInterface* ContextInterface = nullptr;
        FString ContextRevision;
        if (Request.AssetKind == TEXT("pcg_graph_instance"))
        {
            if (!IsAllowedPath(Request.ContextAssetPath)
                || Request.ContextAssetPath == Request.AssetPath)
            {
                return MakeError<FThomasDomainAssetCreatePlanResult>(
                    TEXT("invalid_graph_instance_context"), Request.ContextAssetPath);
            }
            ContextInterface = FindGraphInterface(Request.ContextAssetPath);
            if (!ContextInterface || !ContextInterface->GetGraph())
            {
                return MakeError<FThomasDomainAssetCreatePlanResult>(
                    TEXT("graph_instance_context_not_found"), Request.ContextAssetPath);
            }
            if (ContextInterface->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasDomainAssetCreatePlanResult>(
                    TEXT("dirty_context_denied"), Request.ContextAssetPath);
            }
            ContextRevision = GraphInterfaceRevision(ContextInterface);
        }

        FThomasDomainAssetCreatePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.AssetKind = Request.AssetKind;
        Result.ExpectedRevision = TEXT("missing");
        Result.FactoryClassPath = Request.AssetKind == TEXT("pcg_graph")
            ? TEXT("native_new_object:/Script/PCG.PCGGraph")
            : TEXT("native_new_object:/Script/PCG.PCGGraphInstance");
        Result.ContextAssetPath = Request.ContextAssetPath;
        FPCGCreatePlan& Plan = CreatePlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.ContextRevision = ContextRevision;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasDomainAssetCreateApplyResult ApplyPCGAssetCreate(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FPCGCreatePlan Plan;
        if (!CreatePlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FindGraphInterface(Plan.Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("asset_exists"), Plan.Request.AssetPath);
        }
        UPCGGraphInterface* ContextInterface = nullptr;
        if (Plan.Request.AssetKind == TEXT("pcg_graph_instance"))
        {
            ContextInterface = FindGraphInterface(Plan.Request.ContextAssetPath);
            if (!ContextInterface
                || GraphInterfaceRevision(ContextInterface) != Plan.ContextRevision)
            {
                return MakeError<FThomasDomainAssetCreateApplyResult>(
                    TEXT("context_revision_conflict"),
                    ContextInterface ? GraphInterfaceRevision(ContextInterface) : TEXT("missing"));
            }
        }

        UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
        const FName AssetName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath));
        UPCGGraphInterface* CreatedInterface = nullptr;
        if (Package && Plan.Request.AssetKind == TEXT("pcg_graph"))
        {
            CreatedInterface = NewObject<UPCGGraph>(
                Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
        }
        else if (Package)
        {
            UPCGGraphInstance* Instance = NewObject<UPCGGraphInstance>(
                Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
            if (Instance)
            {
                Instance->SetGraph(ContextInterface);
                CreatedInterface = Instance;
            }
        }
        if (!CreatedInterface || !CreatedInterface->GetGraph())
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("create_failed"), Plan.Request.AssetPath);
        }
        FAssetRegistryModule::AssetCreated(CreatedInterface);
        Package->MarkPackageDirty();

        FThomasDomainAssetCreateApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.ClassPath = CreatedInterface->GetClass()->GetPathName();
        Result.bCreated = true;
        if (bSave)
        {
            Result.bSaved = SaveGraph(CreatedInterface);
            if (!Result.bSaved)
            {
                Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({CreatedInterface}) == 1;
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("PCG Graph save failed; the new asset was removed through Unreal.");
                return Result;
            }
        }
        Result.bOk = true;
        Result.RevisionAfter = GraphInterfaceRevision(CreatedInterface);
        return Result;
    }

    virtual FThomasSpecializedAssetInspectionResult SearchPCGSettingsClasses(
        const FString& InputQuery,
        const int32 InputMaxResults) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("PCG");
        Result.AssetKind = TEXT("settings_class_search");
        const int32 MaxResults = FMath::Clamp(InputMaxResults, 1, 500);
        const FString Query = InputQuery.TrimStartAndEnd().ToLower();
        TArray<FString> Paths;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Class = *It;
            if (!Class
                || !Class->IsChildOf(UPCGSettings::StaticClass())
                || Class == UPCGSettings::StaticClass()
                || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }
            const FString Path = Class->GetPathName();
            if (Query.IsEmpty() || Path.ToLower().Contains(Query))
            {
                Paths.Add(Path);
            }
        }
        Paths.Sort();
        Result.Metrics.Add(FString::Printf(TEXT("MatchedClassCount=%d"), Paths.Num()));
        for (const FString& Path : Paths)
        {
            AddBounded(Result, TEXT("SettingsClass=") + Path, MaxResults);
        }
        Result.bTruncated = Paths.Num() > MaxResults;
        Result.bOk = true;
        return Result;
    }

    virtual FThomasPCGPlanResult PlanPCGPatch(
        const FThomasPCGPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasPCGPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        UPCGGraphInterface* Interface = FindGraphInterface(Request.AssetPath);
        UPCGGraph* Graph = Interface ? Interface->GetMutablePCGGraph() : nullptr;
        if (!Interface || !Graph)
        {
            return MakeError<FThomasPCGPlanResult>(
                TEXT("pcg_graph_interface_not_found"), Request.AssetPath);
        }
        const FString CurrentRevision = GraphInterfaceRevision(Interface);
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasPCGPlanResult>(TEXT("revision_conflict"), CurrentRevision);
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > MaxOperations)
        {
            return MakeError<FThomasPCGPlanResult>(
                TEXT("invalid_operation_count"), TEXT("Operations must contain between 1 and 100 entries."));
        }

        bool bDestructive = false;
        TSet<FString> Handles;
        TArray<FString> Preview;
        const TMap<FString, UPCGNode*> EmptyHandles;
        const bool bGraphInstance = Interface->IsA<UPCGGraphInstance>();
        FInstancedPropertyBag SimulatedParameters = *Interface->GetUserParametersStruct();
        TSet<FString> SimulatedOverrides;
        if (const UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface))
        {
            const UPropertyBag* BagStruct = SimulatedParameters.GetPropertyBagStruct();
            if (BagStruct)
            {
                for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
                {
                    if (Desc.CachedProperty && Instance->IsPropertyOverridden(Desc.CachedProperty))
                    {
                        SimulatedOverrides.Add(Desc.Name.ToString());
                    }
                }
            }
        }
        EPCGGraphUsage SimulatedUsage = Graph->GetGraphUsage();
        for (FThomasPCGOperation& Operation : Request.Operations)
        {
            Operation.Action = Operation.Action.TrimStartAndEnd().ToLower();
            Operation.ParameterName = Operation.ParameterName.TrimStartAndEnd();
            Operation.NewName = Operation.NewName.TrimStartAndEnd();
            Operation.ParameterType = Operation.ParameterType.TrimStartAndEnd().ToLower();
            Operation.TypeObjectPath = Operation.TypeObjectPath.TrimStartAndEnd();
            Operation.GraphUsage = Operation.GraphUsage.TrimStartAndEnd().ToLower();

            const bool bNodeOperation = Operation.Action == TEXT("add_node")
                || Operation.Action == TEXT("set_node_property")
                || Operation.Action == TEXT("set_node_position")
                || Operation.Action == TEXT("set_node_title")
                || Operation.Action == TEXT("connect")
                || Operation.Action == TEXT("disconnect")
                || Operation.Action == TEXT("remove_node");
            if (bGraphInstance && bNodeOperation)
            {
                return MakeError<FThomasPCGPlanResult>(
                    TEXT("graph_instance_node_edit_denied"),
                    TEXT("Node structure belongs to the base PCG Graph; patch the base graph explicitly."));
            }

            if (Operation.Action == TEXT("add_node"))
            {
                if (Operation.Handle.IsEmpty() || Handles.Contains(Operation.Handle))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("invalid_or_duplicate_handle"), Operation.Handle);
                }
                UClass* Class = LoadObject<UClass>(nullptr, *Operation.SettingsClassPath);
                if (!Class
                    || !Class->IsChildOf(UPCGSettings::StaticClass())
                    || Class == UPCGSettings::StaticClass()
                    || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("settings_class_invalid"), Operation.SettingsClassPath);
                }
                Handles.Add(Operation.Handle);
                Preview.Add(TEXT("Add PCG node ") + Operation.SettingsClassPath);
            }
            else if (Operation.Action == TEXT("set_node_property"))
            {
                UPCGNode* Node = ResolveNode(
                    Graph, Operation.NodePath, Operation.Handle, EmptyHandles);
                UClass* SettingsClass = Node && Node->GetSettings()
                    ? Node->GetSettings()->GetClass()
                    : LoadObject<UClass>(nullptr, *Operation.SettingsClassPath);
                if ((!Node && (Operation.Handle.IsEmpty() || !Handles.Contains(Operation.Handle)))
                    || !SettingsClass
                    || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
                {
                    return MakeError<FThomasPCGPlanResult>(TEXT("node_not_found"), Operation.NodePath);
                }
                FProperty* Property = FindFProperty<FProperty>(
                    SettingsClass, FName(*Operation.PropertyName));
                if (!IsEditableSettingsProperty(Property))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("settings_property_not_editable"), Operation.PropertyName);
                }
                const UObject* Source = Node ? Node->GetSettings() : SettingsClass->GetDefaultObject();
                const FString Current = ExportProperty(Source, Property);
                if (Operation.ExpectedValue.IsEmpty() || Operation.ExpectedValue != Current)
                {
                    return MakeError<FThomasPCGPlanResult>(TEXT("property_conflict"), Current);
                }
                UObject* Candidate = DuplicateObject(Source, GetTransientPackage());
                if (!Candidate || !ImportProperty(Candidate, Property, Operation.Value))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("property_value_invalid"), Operation.Value);
                }
                Preview.Add(TEXT("Set PCG settings property ") + Operation.PropertyName);
            }
            else if (Operation.Action == TEXT("set_node_position")
                || Operation.Action == TEXT("set_node_title"))
            {
                UPCGNode* Node = ResolveNode(
                    Graph, Operation.NodePath, Operation.Handle, EmptyHandles);
                if (!Node && (Operation.Handle.IsEmpty() || !Handles.Contains(Operation.Handle)))
                {
                    return MakeError<FThomasPCGPlanResult>(TEXT("node_not_found"), Operation.NodePath);
                }
                Preview.Add(Operation.Action + TEXT(" ")
                    + (Operation.Handle.IsEmpty() ? Operation.NodePath : Operation.Handle));
            }
            else if (Operation.Action == TEXT("connect")
                || Operation.Action == TEXT("disconnect"))
            {
                UPCGNode* From = ResolveNode(
                    Graph, Operation.NodePath, Operation.Handle, EmptyHandles);
                UPCGNode* To = ResolveNode(
                    Graph, Operation.OtherNodePath, Operation.OtherHandle, EmptyHandles);
                const bool bFromDeferred = !Operation.Handle.IsEmpty() && Handles.Contains(Operation.Handle);
                const bool bToDeferred = !Operation.OtherHandle.IsEmpty() && Handles.Contains(Operation.OtherHandle);
                if ((!From && !bFromDeferred) || (!To && !bToDeferred)
                    || Operation.FromPin.IsEmpty() || Operation.ToPin.IsEmpty())
                {
                    return MakeError<FThomasPCGPlanResult>(TEXT("edge_endpoint_invalid"), Operation.Action);
                }
                if (From && To)
                {
                    const UPCGPin* FromPin = From->GetOutputPin(FName(*Operation.FromPin));
                    const UPCGPin* ToPin = To->GetInputPin(FName(*Operation.ToPin));
                    if (!FromPin || !ToPin)
                    {
                        return MakeError<FThomasPCGPlanResult>(TEXT("pin_not_found"), Operation.Action);
                    }
                    const bool bExists = EdgeExists(FromPin, ToPin);
                    if ((Operation.Action == TEXT("connect") && (bExists || !FromPin->CanConnect(ToPin)))
                        || (Operation.Action == TEXT("disconnect") && !bExists))
                    {
                        return MakeError<FThomasPCGPlanResult>(TEXT("edge_state_conflict"), Operation.Action);
                    }
                }
                bDestructive |= Operation.Action == TEXT("disconnect");
                Preview.Add(Operation.Action + TEXT(" PCG edge"));
            }
            else if (Operation.Action == TEXT("remove_node"))
            {
                UPCGNode* Node = ResolveNode(
                    Graph, Operation.NodePath, Operation.Handle, EmptyHandles);
                if (!Node
                    || Node == Graph->GetInputNode()
                    || Node == Graph->GetOutputNode())
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("node_not_removable"), Operation.NodePath);
                }
                bDestructive = true;
                Preview.Add(TEXT("Remove PCG node ") + Operation.NodePath);
            }
            else if (Operation.Action == TEXT("set_graph_usage"))
            {
                if (bGraphInstance)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_instance_usage_edit_denied"),
                        TEXT("Graph usage belongs to the base PCG Graph."));
                }
                const TOptional<EPCGGraphUsage> Usage = ParseGraphUsage(Operation.GraphUsage);
                if (!Usage.IsSet())
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_usage_invalid"), Operation.GraphUsage);
                }
                if (Usage.GetValue() == SimulatedUsage)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_usage_conflict"), Operation.GraphUsage);
                }
                SimulatedUsage = Usage.GetValue();
                bDestructive = true;
                Preview.Add(TEXT("Set PCG graph usage to ") + Operation.GraphUsage);
            }
            else if (Operation.Action == TEXT("add_graph_parameter"))
            {
                if (bGraphInstance)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_instance_schema_edit_denied"),
                        TEXT("Parameter schema belongs to the base PCG Graph."));
                }
                if (Operation.ParameterName.Len() > 64
                    || !FInstancedPropertyBag::IsPropertyNameValid(Operation.ParameterName)
                    || FindBagDescExact(SimulatedParameters, Operation.ParameterName))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_name_invalid_or_exists"), Operation.ParameterName);
                }
                EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
                const UObject* ValueTypeObject = nullptr;
                if (!ResolveParameterType(
                        Operation.ParameterType,
                        Operation.TypeObjectPath,
                        ValueType,
                        ValueTypeObject))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_type_invalid"),
                        Operation.ParameterType + TEXT("|") + Operation.TypeObjectPath);
                }
                if (SimulatedParameters.AddProperty(
                        FName(*Operation.ParameterName),
                        ValueType,
                        ValueTypeObject,
                        false) != EPropertyBagAlterationResult::Success
                    || !ImportBagValue(
                        SimulatedParameters,
                        FName(*Operation.ParameterName),
                        Operation.Value,
                        Interface))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_default_invalid"), Operation.Value);
                }
                Preview.Add(FString::Printf(
                    TEXT("Add PCG graph parameter %s (%s)"),
                    *Operation.ParameterName,
                    *Operation.ParameterType));
            }
            else if (Operation.Action == TEXT("rename_graph_parameter"))
            {
                if (bGraphInstance)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_instance_schema_edit_denied"),
                        TEXT("Parameter schema belongs to the base PCG Graph."));
                }
                if (!FindBagDescExact(SimulatedParameters, Operation.ParameterName))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_not_found"), Operation.ParameterName);
                }
                if (Operation.NewName.Len() > 64
                    || !FInstancedPropertyBag::IsPropertyNameValid(Operation.NewName)
                    || FindBagDescExact(SimulatedParameters, Operation.NewName)
                    || SimulatedParameters.RenameProperty(
                        FName(*Operation.ParameterName),
                        FName(*Operation.NewName)) != EPropertyBagAlterationResult::Success)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_rename_invalid"), Operation.NewName);
                }
                bDestructive = true;
                Preview.Add(TEXT("Rename PCG graph parameter ")
                    + Operation.ParameterName + TEXT(" to ") + Operation.NewName);
            }
            else if (Operation.Action == TEXT("remove_graph_parameter"))
            {
                if (bGraphInstance)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("graph_instance_schema_edit_denied"),
                        TEXT("Parameter schema belongs to the base PCG Graph."));
                }
                const FPropertyBagPropertyDesc* Desc =
                    FindBagDescExact(SimulatedParameters, Operation.ParameterName);
                if (!Desc)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_not_found"), Operation.ParameterName);
                }
                if (SimulatedParameters.RemovePropertyByName(Desc->Name)
                    != EPropertyBagAlterationResult::Success)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_remove_invalid"), Operation.ParameterName);
                }
                SimulatedOverrides.Remove(Operation.ParameterName);
                bDestructive = true;
                Preview.Add(TEXT("Remove PCG graph parameter ") + Operation.ParameterName);
            }
            else if (Operation.Action == TEXT("set_graph_parameter"))
            {
                const FPropertyBagPropertyDesc* Desc =
                    FindBagDescExact(SimulatedParameters, Operation.ParameterName);
                if (!Desc)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_not_found"), Operation.ParameterName);
                }
                const FString Current = ExportBagValue(
                    SimulatedParameters, *Desc, Interface);
                if (Current != Operation.ExpectedValue)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_value_conflict"), Current);
                }
                if (!ImportBagValue(
                        SimulatedParameters,
                        Desc->Name,
                        Operation.Value,
                        Interface))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_value_invalid"), Operation.Value);
                }
                if (bGraphInstance)
                {
                    SimulatedOverrides.Add(Operation.ParameterName);
                }
                Preview.Add(TEXT("Set PCG graph parameter ") + Operation.ParameterName);
            }
            else if (Operation.Action == TEXT("reset_graph_parameter_override"))
            {
                const UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface);
                const FPropertyBagPropertyDesc* Desc =
                    FindBagDescExact(SimulatedParameters, Operation.ParameterName);
                if (!Instance || !Desc)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        Instance ? TEXT("parameter_not_found") : TEXT("graph_instance_required"),
                        Operation.ParameterName);
                }
                const FString Current = ExportBagValue(
                    SimulatedParameters, *Desc, Interface);
                if (Current != Operation.ExpectedValue)
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_value_conflict"), Current);
                }
                if (!SimulatedOverrides.Contains(Operation.ParameterName))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_not_overridden"), Operation.ParameterName);
                }
                const FInstancedPropertyBag* ParentParameters = Instance->Graph
                    ? Instance->Graph->GetUserParametersStruct()
                    : nullptr;
                const FPropertyBagPropertyDesc* ParentDesc = ParentParameters
                    ? FindBagDescExact(*ParentParameters, Operation.ParameterName)
                    : nullptr;
                if (!ParentParameters
                    || !ParentDesc
                    || !ImportBagValue(
                        SimulatedParameters,
                        Desc->Name,
                        ExportBagValue(*ParentParameters, *ParentDesc, Instance->Graph),
                        Interface))
                {
                    return MakeError<FThomasPCGPlanResult>(
                        TEXT("parameter_default_unavailable"), Operation.ParameterName);
                }
                SimulatedOverrides.Remove(Operation.ParameterName);
                Preview.Add(TEXT("Reset PCG graph parameter override ")
                    + Operation.ParameterName);
            }
            else
            {
                return MakeError<FThomasPCGPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }
        if (bDestructive && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasPCGPlanResult>(
                TEXT("confirmation_required"),
                TEXT("Disconnect/remove, graph-usage, and parameter-schema changes require bConfirmDestructive=true."));
        }

        FThomasPCGPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = CurrentRevision;
        Result.OperationCount = Request.Operations.Num();
        Result.Risk = bDestructive ? TEXT("R2") : TEXT("R1");
        Result.Preview = MoveTemp(Preview);
        FPCGPatchPlan& Plan = PatchPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasPCGApplyResult ApplyPCGPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FPCGPatchPlan Plan;
        if (!PatchPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasPCGApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UPCGGraphInterface* Interface = FindGraphInterface(Plan.Request.AssetPath);
        UPCGGraph* Graph = Interface ? Interface->GetMutablePCGGraph() : nullptr;
        const FString CurrentRevision = GraphInterfaceRevision(Interface);
        if (!Interface || !Graph || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasPCGApplyResult>(TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasPCGApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = CurrentRevision;
        TMap<FString, UPCGNode*> Handles;
        bool bApplied = true;
        FString Error;
        {
            const FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyPCGPatch", "ThomasEditor PCG Graph Patch"));
            Interface->Modify();
            Interface->GetOutermost()->Modify();
            Graph->Modify();
            Graph->GetOutermost()->Modify();
            for (const FThomasPCGOperation& Operation : Plan.Request.Operations)
            {
                if (Operation.Action == TEXT("add_node"))
                {
                    UClass* Class = LoadObject<UClass>(nullptr, *Operation.SettingsClassPath);
                    UPCGSettings* Settings = nullptr;
                    UPCGNode* Node = Class ? Graph->AddNodeOfType(Class, Settings) : nullptr;
                    bApplied = Node && Settings;
                    if (bApplied)
                    {
                        Node->SetNodePosition(Operation.PositionX, Operation.PositionY);
                        if (!Operation.Title.IsEmpty())
                        {
                            Node->SetNodeTitle(FName(*Operation.Title));
                        }
                        Handles.Add(Operation.Handle, Node);
                        Result.CreatedNodePaths.Add(Node->GetPathName());
                    }
                }
                else if (Operation.Action == TEXT("set_node_property"))
                {
                    UPCGNode* Node = ResolveNode(
                        Graph, Operation.NodePath, Operation.Handle, Handles);
                    UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
                    FProperty* Property = Settings
                        ? FindFProperty<FProperty>(Settings->GetClass(), FName(*Operation.PropertyName))
                        : nullptr;
                    bApplied = Settings
                        && IsEditableSettingsProperty(Property)
                        && ExportProperty(Settings, Property) == Operation.ExpectedValue;
                    if (bApplied)
                    {
                        Settings->Modify();
                        bApplied = ImportProperty(Settings, Property, Operation.Value);
                        if (bApplied)
                        {
                            Settings->PostEditChange();
                            Node->UpdateAfterSettingsChangeDuringCreation();
                        }
                    }
                }
                else if (Operation.Action == TEXT("set_node_position"))
                {
                    UPCGNode* Node = ResolveNode(
                        Graph, Operation.NodePath, Operation.Handle, Handles);
                    bApplied = Node != nullptr;
                    if (bApplied)
                    {
                        Node->Modify();
                        Node->SetNodePosition(Operation.PositionX, Operation.PositionY);
                    }
                }
                else if (Operation.Action == TEXT("set_node_title"))
                {
                    UPCGNode* Node = ResolveNode(
                        Graph, Operation.NodePath, Operation.Handle, Handles);
                    bApplied = Node && Node->SetNodeTitle(FName(*Operation.Title));
                }
                else if (Operation.Action == TEXT("connect")
                    || Operation.Action == TEXT("disconnect"))
                {
                    UPCGNode* From = ResolveNode(
                        Graph, Operation.NodePath, Operation.Handle, Handles);
                    UPCGNode* To = ResolveNode(
                        Graph, Operation.OtherNodePath, Operation.OtherHandle, Handles);
                    if (Operation.Action == TEXT("connect"))
                    {
                        bApplied = From && To;
                        if (bApplied)
                        {
                            Graph->AddEdge(
                                From,
                                FName(*Operation.FromPin),
                                To,
                                FName(*Operation.ToPin));
                            bApplied = EdgeExists(
                                From->GetOutputPin(FName(*Operation.FromPin)),
                                To->GetInputPin(FName(*Operation.ToPin)));
                        }
                    }
                    else
                    {
                        bApplied = From && To && Graph->RemoveEdge(
                            From,
                            FName(*Operation.FromPin),
                            To,
                            FName(*Operation.ToPin));
                    }
                }
                else if (Operation.Action == TEXT("remove_node"))
                {
                    UPCGNode* Node = ResolveNode(
                        Graph, Operation.NodePath, Operation.Handle, Handles);
                    bApplied = Node
                        && Node != Graph->GetInputNode()
                        && Node != Graph->GetOutputNode();
                    if (bApplied)
                    {
                        Graph->RemoveNode(Node);
                    }
                }
                else if (Operation.Action == TEXT("set_graph_usage"))
                {
                    const TOptional<EPCGGraphUsage> Usage = ParseGraphUsage(Operation.GraphUsage);
                    bApplied = Interface == Graph
                        && Usage.IsSet()
                        && Graph->GraphUsageContext != Usage.GetValue();
                    if (bApplied)
                    {
                        Graph->GraphUsageContext = Usage.GetValue();
                        Graph->PostEditChange();
                    }
                }
                else if (Operation.Action == TEXT("add_graph_parameter"))
                {
                    EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
                    const UObject* ValueTypeObject = nullptr;
                    bApplied = Interface == Graph
                        && !FindBagDescExact(
                            *Graph->GetUserParametersStruct(), Operation.ParameterName)
                        && ResolveParameterType(
                            Operation.ParameterType,
                            Operation.TypeObjectPath,
                            ValueType,
                            ValueTypeObject)
                        && Graph->AddUserParameters({FPropertyBagPropertyDesc(
                            FName(*Operation.ParameterName), ValueType, ValueTypeObject)})
                            == EPropertyBagAlterationResult::Success;
                    if (bApplied)
                    {
                        Graph->UpdateUserParametersStruct(
                            [&Operation, Graph, &bApplied](FInstancedPropertyBag& Parameters)
                            {
                                bApplied = ImportBagValue(
                                    Parameters,
                                    FName(*Operation.ParameterName),
                                    Operation.Value,
                                    Graph);
                            });
                    }
                }
                else if (Operation.Action == TEXT("rename_graph_parameter"))
                {
                    bApplied = Interface == Graph
                        && FindBagDescExact(
                            *Graph->GetUserParametersStruct(), Operation.ParameterName)
                        && !FindBagDescExact(
                            *Graph->GetUserParametersStruct(), Operation.NewName)
                        && Graph->RenameUserParameter(
                            FName(*Operation.ParameterName),
                            FName(*Operation.NewName))
                            == EPropertyBagAlterationResult::Success;
                }
                else if (Operation.Action == TEXT("remove_graph_parameter"))
                {
                    const FPropertyBagPropertyDesc* Desc = Interface == Graph
                        ? FindBagDescExact(
                            *Graph->GetUserParametersStruct(), Operation.ParameterName)
                        : nullptr;
                    bApplied = Desc != nullptr;
                    if (bApplied)
                    {
                        const FName ParameterName = Desc->Name;
                        Graph->UpdateUserParametersStruct(
                            [ParameterName, &bApplied](FInstancedPropertyBag& Parameters)
                            {
                                bApplied = Parameters.RemovePropertyByName(ParameterName)
                                    == EPropertyBagAlterationResult::Success;
                            });
                    }
                }
                else if (Operation.Action == TEXT("set_graph_parameter"))
                {
                    const FInstancedPropertyBag* CurrentParameters =
                        Interface->GetUserParametersStruct();
                    const FPropertyBagPropertyDesc* Desc = CurrentParameters
                        ? FindBagDescExact(*CurrentParameters, Operation.ParameterName)
                        : nullptr;
                    bApplied = Desc
                        && ExportBagValue(*CurrentParameters, *Desc, Interface)
                            == Operation.ExpectedValue;
                    if (bApplied && Interface == Graph)
                    {
                        Graph->UpdateUserParametersStruct(
                            [&Operation, Graph, &bApplied](FInstancedPropertyBag& Parameters)
                            {
                                bApplied = ImportBagValue(
                                    Parameters,
                                    FName(*Operation.ParameterName),
                                    Operation.Value,
                                    Graph);
                            });
                    }
                    else if (bApplied)
                    {
                        UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface);
                        bApplied = Instance && Desc->CachedProperty;
                        if (bApplied)
                        {
                            Instance->UpdatePropertyOverride(Desc->CachedProperty, true);
                            bApplied = ImportBagValue(
                                *Instance->GetMutableUserParametersStruct_Unsafe(),
                                FName(*Operation.ParameterName),
                                Operation.Value,
                                Instance);
                            if (bApplied)
                            {
                                Instance->OnGraphParametersChanged(
                                    EPCGGraphParameterEvent::ValueModifiedLocally,
                                    FName(*Operation.ParameterName));
                            }
                        }
                    }
                }
                else if (Operation.Action == TEXT("reset_graph_parameter_override"))
                {
                    UPCGGraphInstance* Instance = Cast<UPCGGraphInstance>(Interface);
                    const FInstancedPropertyBag* CurrentParameters = Instance
                        ? Instance->GetUserParametersStruct()
                        : nullptr;
                    const FPropertyBagPropertyDesc* Desc = CurrentParameters
                        ? FindBagDescExact(*CurrentParameters, Operation.ParameterName)
                        : nullptr;
                    bApplied = Instance
                        && Desc
                        && Desc->CachedProperty
                        && Instance->IsPropertyOverridden(Desc->CachedProperty)
                        && ExportBagValue(*CurrentParameters, *Desc, Instance)
                            == Operation.ExpectedValue;
                    if (bApplied)
                    {
                        Instance->UpdatePropertyOverride(Desc->CachedProperty, false);
                        const FPropertyBagPropertyDesc* UpdatedDesc = FindBagDescExact(
                            *Instance->GetUserParametersStruct(), Operation.ParameterName);
                        bApplied = UpdatedDesc
                            && !Instance->IsPropertyOverridden(UpdatedDesc->CachedProperty);
                    }
                }
                if (!bApplied)
                {
                    Error = Operation.Action + TEXT(" failed for ")
                        + (!Operation.ParameterName.IsEmpty()
                            ? Operation.ParameterName
                            : (Operation.NodePath.IsEmpty() ? Operation.Handle : Operation.NodePath));
                    break;
                }
                ++Result.AppliedOperationCount;
            }
            if (bApplied)
            {
                Interface->MarkPackageDirty();
            }
        }
        if (!bApplied)
        {
            Result.bRolledBack = GEditor && GEditor->UndoTransaction();
            Result.Code = TEXT("apply_failed");
            Result.Message = Error.Left(1200);
            Result.RevisionAfter = GraphInterfaceRevision(Interface);
            return Result;
        }
        if (bSave)
        {
            Result.bSaved = SaveGraph(Interface);
            if (!Result.bSaved)
            {
                Result.bRolledBack = GEditor && GEditor->UndoTransaction();
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("PCG Graph Interface save failed; the transaction was undone.");
                Result.RevisionAfter = GraphInterfaceRevision(Interface);
                return Result;
            }
        }
        Result.bOk = true;
        Result.RevisionAfter = GraphInterfaceRevision(Interface);
        return Result;
    }

    virtual FThomasPCGExecutionPlanResult PlanPCGExecution(
        const FThomasPCGExecutionPlanRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasPCGExecutionPlanRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("asset_path_not_allowed"), Request.AssetPath);
        }
        UPCGGraphInterface* Interface = FindGraphInterface(Request.AssetPath);
        if (!Interface || !Interface->GetGraph())
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("pcg_graph_interface_not_found"), Request.AssetPath);
        }
        const FString CurrentRevision = GraphInterfaceRevision(Interface);
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (Interface->GetOutermost()->IsDirty())
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("dirty_graph_denied"),
                TEXT("Save the PCG Graph Interface before standalone execution."));
        }
        if (Interface->GetGraphUsage() != EPCGGraphUsage::Asset)
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("asset_graph_usage_required"),
                TEXT("Standalone execution is limited to PCG graphs whose usage is Asset."));
        }
        if (!Request.bConfirmExecution)
        {
            return MakeError<FThomasPCGExecutionPlanResult>(
                TEXT("confirmation_required"),
                TEXT("Standalone PCG execution requires bConfirmExecution=true."));
        }
        for (const TPair<FString, FPCGExecutionJob>& Pair : ExecutionJobs)
        {
            if (Pair.Value.Request.AssetPath == Request.AssetPath
                && Pair.Value.State.IsValid()
                && (Pair.Value.State->Status == TEXT("running")
                    || Pair.Value.State->Status == TEXT("cancel_requested")))
            {
                return MakeError<FThomasPCGExecutionPlanResult>(
                    TEXT("execution_already_running"), Pair.Key);
            }
        }

        FThomasPCGExecutionPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = CurrentRevision;
        Result.Seed = Request.Seed;
        Result.Risk = TEXT("R2");
        Result.Preview = {
            TEXT("Execute standalone PCG Asset graph without a world or PCG component."),
            FString::Printf(TEXT("Seed=%d"), Request.Seed),
            TEXT("Results are returned as bounded class/pin summaries; generated asset side effects are node-defined.")
        };
        FPCGExecutionPlan& Plan = ExecutionPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasPCGExecutionStartResult StartPCGExecution(
        const FString& PlanId) override
    {
        PurgeExpiredPlans();
        FPCGExecutionPlan Plan;
        if (!ExecutionPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasPCGExecutionStartResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UPCGGraphInterface* Interface = FindGraphInterface(Plan.Request.AssetPath);
        const FString CurrentRevision = GraphInterfaceRevision(Interface);
        if (!Interface
            || !Interface->GetGraph()
            || CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasPCGExecutionStartResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }
        if (Interface->GetOutermost()->IsDirty()
            || Interface->GetGraphUsage() != EPCGGraphUsage::Asset)
        {
            return MakeError<FThomasPCGExecutionStartResult>(
                TEXT("execution_precondition_changed"),
                TEXT("The interface must still be saved and use Asset graph usage."));
        }
        if (ExecutionJobs.Num() >= MaxExecutionJobs)
        {
            return MakeError<FThomasPCGExecutionStartResult>(
                TEXT("execution_job_limit"), FString::FromInt(MaxExecutionJobs));
        }
        for (const TPair<FString, FPCGExecutionJob>& Pair : ExecutionJobs)
        {
            if (Pair.Value.Request.AssetPath == Plan.Request.AssetPath
                && Pair.Value.State.IsValid()
                && (Pair.Value.State->Status == TEXT("running")
                    || Pair.Value.State->Status == TEXT("cancel_requested")))
            {
                return MakeError<FThomasPCGExecutionStartResult>(
                    TEXT("execution_already_running"), Pair.Key);
            }
        }

        const TSharedPtr<FPCGExecutionJobState> State = MakeShared<FPCGExecutionJobState>();
        FPCGDefaultExecutionSourceParams Params;
        Params.GraphInterface = Interface;
        Params.Seed = Plan.Request.Seed;
        Params.bFireAndForgetExecution = true;
        Params.PostProcessCallback = FPCGGenerationPostProcessCallback::CreateLambda(
            [State](const FPCGDataCollection& Collection)
            {
                TMap<FString, int32> Counts;
                State->TaggedDataCount = Collection.TaggedData.Num();
                State->NullDataCount = 0;
                for (const FPCGTaggedData& TaggedData : Collection.TaggedData)
                {
                    const UPCGData* Data = TaggedData.Data.Get();
                    if (!Data)
                    {
                        ++State->NullDataCount;
                        continue;
                    }
                    const FString Key = FString::Printf(
                        TEXT("Class=%s Pin=%s"),
                        *Data->GetClass()->GetPathName(),
                        *TaggedData.Pin.ToString());
                    ++Counts.FindOrAdd(Key);
                }
                TArray<FString> Keys;
                Counts.GetKeys(Keys);
                Keys.Sort();
                for (const FString& Key : Keys)
                {
                    if (State->Outputs.Num() >= 64)
                    {
                        State->Diagnostics.Add(TEXT("output_summary_truncated"));
                        break;
                    }
                    State->Outputs.Add(FString::Printf(
                        TEXT("%s Count=%d"), *Key, Counts.FindChecked(Key)));
                }
            });
        Params.GenerationCallback = FPCGOnEditorGenerationDone::CreateLambda(
            [State](IPCGGraphExecutionSource*, const EPCGGenerationStatus Status)
            {
                State->Status = Status == EPCGGenerationStatus::Completed
                    ? TEXT("completed") : TEXT("aborted");
                State->CompletedAtSeconds = FPlatformTime::Seconds();
            });

        UPCGDefaultExecutionSource* Source =
            IPCGBaseSubsystem::CreateExecutionSource<UPCGDefaultExecutionSource>(Params);
        if (!Source)
        {
            return MakeError<FThomasPCGExecutionStartResult>(
                TEXT("execution_source_create_failed"), Plan.Request.AssetPath);
        }

        FThomasPCGExecutionStartResult Result;
        Result.bOk = true;
        Result.JobId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Plan.Request.AssetPath;
        Result.TaskId = FString::Printf(
            TEXT("%llu"),
            static_cast<unsigned long long>(Source->GetCurrentGenerationTask()));
        Result.Status = State->Status;
        FPCGExecutionJob& Job = ExecutionJobs.Add(Result.JobId);
        Job.Request = MoveTemp(Plan.Request);
        Job.Source = Source;
        Job.State = State;
        Job.TaskId = Result.TaskId;
        Job.StartedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasPCGExecutionStatusResult GetPCGExecutionStatus(
        const FString& JobId) override
    {
        PurgeExpiredPlans();
        FPCGExecutionJob* Job = ExecutionJobs.Find(JobId);
        if (!Job || !Job->State.IsValid())
        {
            return MakeError<FThomasPCGExecutionStatusResult>(
                TEXT("job_not_found"), JobId);
        }
        if (IsRunningCommandlet() || FApp::IsUnattended())
        {
            if (UPCGEngineSubsystem* Subsystem = UPCGEngineSubsystem::Get())
            {
                static_cast<FTickableGameObject*>(Subsystem)->Tick(0.0f);
            }
        }
        if ((Job->State->Status == TEXT("running")
                || Job->State->Status == TEXT("cancel_requested"))
            && !Job->Source.IsValid())
        {
            Job->State->Status = TEXT("source_lost");
            Job->State->CompletedAtSeconds = FPlatformTime::Seconds();
            Job->State->Diagnostics.Add(TEXT("Execution source became unavailable before completion."));
        }

        FThomasPCGExecutionStatusResult Result;
        Result.bOk = true;
        Result.JobId = JobId;
        Result.AssetPath = Job->Request.AssetPath;
        Result.TaskId = Job->TaskId;
        Result.Status = Job->State->Status;
        Result.bRunning = Result.Status == TEXT("running")
            || Result.Status == TEXT("cancel_requested");
        Result.bCancelRequested = Job->State->bCancelRequested;
        Result.Seed = Job->Request.Seed;
        Result.TaggedDataCount = Job->State->TaggedDataCount;
        Result.NullDataCount = Job->State->NullDataCount;
        Result.StartedAtSeconds = Job->StartedAtSeconds;
        Result.CompletedAtSeconds = Job->State->CompletedAtSeconds;
        Result.Outputs = Job->State->Outputs;
        Result.Diagnostics = Job->State->Diagnostics;
        return Result;
    }

    virtual FThomasPCGExecutionCancelResult CancelPCGExecution(
        const FString& JobId) override
    {
        PurgeExpiredPlans();
        FPCGExecutionJob* Job = ExecutionJobs.Find(JobId);
        if (!Job || !Job->State.IsValid())
        {
            return MakeError<FThomasPCGExecutionCancelResult>(
                TEXT("job_not_found"), JobId);
        }
        if (Job->State->Status != TEXT("running"))
        {
            return MakeError<FThomasPCGExecutionCancelResult>(
                TEXT("job_not_running"), Job->State->Status);
        }
        UPCGDefaultExecutionSource* Source = Job->Source.Get();
        if (!Source || !Source->GetExecutionState().IsGenerating())
        {
            return MakeError<FThomasPCGExecutionCancelResult>(
                TEXT("job_not_running"), Job->State->Status);
        }
        Job->State->bCancelRequested = true;
        Job->State->Status = TEXT("cancel_requested");
        Source->GetExecutionState().Cancel();

        FThomasPCGExecutionCancelResult Result;
        Result.bOk = true;
        Result.JobId = JobId;
        Result.Status = Job->State->Status;
        return Result;
    }

private:
    void PurgeExpiredPlans()
    {
        const double Now = FPlatformTime::Seconds();
        for (auto It = CreatePlans.CreateIterator(); It; ++It)
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
        for (auto It = ExecutionPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
        for (auto It = ExecutionJobs.CreateIterator(); It; ++It)
        {
            const TSharedPtr<FPCGExecutionJobState>& State = It.Value().State;
            if (State.IsValid()
                && State->CompletedAtSeconds > 0.0
                && Now - State->CompletedAtSeconds > CompletedJobLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FPCGCreatePlan> CreatePlans;
    TMap<FString, FPCGPatchPlan> PatchPlans;
    TMap<FString, FPCGExecutionPlan> ExecutionPlans;
    TMap<FString, FPCGExecutionJob> ExecutionJobs;
};

IMPLEMENT_MODULE(FThomasEditorPCGModule, ThomasEditorPCG)
