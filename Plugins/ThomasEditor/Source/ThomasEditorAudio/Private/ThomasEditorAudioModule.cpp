#include "ThomasEditorAudioProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Metasound.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundSource.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "ThomasEditorAssetsProvider.h"
#include "UObject/SavePackage.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedAudioCreatePlan
{
    FThomasAudioCreateRequest Request;
    FString FactoryClassPath;
    double CreatedAtSeconds = 0.0;
};

struct FCachedMetaSoundPatchPlan
{
    FThomasMetaSoundPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

FString NormalizePath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.Contains(TEXT(".")))
    {
        Path = FPackageName::ObjectPathToPackageName(Path);
    }
    return Path;
}

bool IsAllowedPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/PropHunt/"))
        && !Path.StartsWith(TEXT("/Game/Developers/"))
        && !Path.Contains(TEXT(".."));
}

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

UObject* FindAsset(const FString& PackageName)
{
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPath(PackageName)));
    return AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
}

FString BuildRevision(const UObject* Asset)
{
    if (!Asset)
    {
        return TEXT("missing");
    }
    const UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0);
}

FString FactoryClassForKind(const FString& Kind)
{
    if (Kind == TEXT("sound_attenuation"))
    {
        return TEXT("/Script/AudioEditor.SoundAttenuationFactory");
    }
    if (Kind == TEXT("sound_concurrency"))
    {
        return TEXT("/Script/AudioEditor.SoundConcurrencyFactory");
    }
    if (Kind == TEXT("sound_cue"))
    {
        return TEXT("/Script/AudioEditor.SoundCueFactoryNew");
    }
    if (Kind == TEXT("sound_class"))
    {
        return TEXT("/Script/AudioEditor.SoundClassFactory");
    }
    if (Kind == TEXT("sound_mix"))
    {
        return TEXT("/Script/AudioEditor.SoundMixFactory");
    }
    if (Kind == TEXT("sound_submix"))
    {
        return TEXT("/Script/AudioEditor.SoundSubmixFactory");
    }
    if (Kind == TEXT("sound_source_bus"))
    {
        return TEXT("/Script/AudioEditor.SoundSourceBusFactory");
    }
    if (Kind == TEXT("dialogue_voice"))
    {
        return TEXT("/Script/AudioEditor.DialogueVoiceFactory");
    }
    if (Kind == TEXT("dialogue_wave"))
    {
        return TEXT("/Script/AudioEditor.DialogueWaveFactory");
    }
    if (Kind == TEXT("meta_sound_source"))
    {
        return TEXT("/Script/MetasoundEditor.MetaSoundSourceFactory");
    }
    if (Kind == TEXT("meta_sound_patch"))
    {
        return TEXT("/Script/MetasoundEditor.MetaSoundFactory");
    }
    return FString();
}

UFactory* CreateFactory(const FString& FactoryClassPath, FString& OutError)
{
    const FName RequiredModule = FactoryClassPath.Contains(TEXT("MetasoundEditor"))
        ? FName(TEXT("MetasoundEditor"))
        : FName(TEXT("AudioEditor"));
    if (!FModuleManager::Get().ModuleExists(*RequiredModule.ToString())
        || !FModuleManager::Get().LoadModule(RequiredModule))
    {
        OutError = FString::Printf(TEXT("%s module is unavailable."), *RequiredModule.ToString());
        return nullptr;
    }
    UClass* FactoryClass = LoadObject<UClass>(nullptr, *FactoryClassPath);
    UFactory* Factory = FactoryClass
        ? NewObject<UFactory>(GetTransientPackage(), FactoryClass)
        : nullptr;
    if (!Factory || !Factory->SupportedClass)
    {
        OutError = FString::Printf(TEXT("Factory is unavailable: %s"), *FactoryClassPath);
        return nullptr;
    }
    return Factory;
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

bool IsMetaSoundAsset(UObject* Asset)
{
    return Asset && Cast<IMetaSoundDocumentInterface>(Asset) != nullptr;
}

TScriptInterface<IMetaSoundDocumentInterface> AsMetaSoundDocument(UObject* Asset)
{
    TScriptInterface<IMetaSoundDocumentInterface> Document;
    Document.SetObject(Asset);
    Document.SetInterface(Asset ? Cast<IMetaSoundDocumentInterface>(Asset) : nullptr);
    return Document;
}

TArray<FString> SplitValues(const FString& Input)
{
    TArray<FString> Values;
    Input.ParseIntoArray(Values, TEXT(","), true);
    for (FString& Value : Values)
    {
        Value.TrimStartAndEndInline();
    }
    return Values;
}

bool BuildMetaSoundLiteral(
    const FString& InputType,
    const FString& Value,
    FMetasoundFrontendLiteral& OutLiteral,
    FString& OutError)
{
    const FString Type = InputType.TrimStartAndEnd().ToLower();
    const FString Trimmed = Value.TrimStartAndEnd();
    if (Type.IsEmpty() || Type == TEXT("default"))
    {
        OutLiteral.Set(FMetasoundFrontendLiteral::FDefault{});
        return true;
    }
    if (Type == TEXT("bool"))
    {
        const FString Lower = Trimmed.ToLower();
        if (Lower != TEXT("true") && Lower != TEXT("false") && Lower != TEXT("1") && Lower != TEXT("0"))
        {
            OutError = TEXT("bool literal must be true, false, 1, or 0.");
            return false;
        }
        OutLiteral.Set(Lower == TEXT("true") || Lower == TEXT("1"));
        return true;
    }
    if (Type == TEXT("int") || Type == TEXT("int32"))
    {
        if (!Trimmed.IsNumeric())
        {
            OutError = TEXT("int literal is not numeric.");
            return false;
        }
        OutLiteral.Set(FCString::Atoi(*Trimmed));
        return true;
    }
    if (Type == TEXT("float"))
    {
        if (!Trimmed.IsNumeric())
        {
            OutError = TEXT("float literal is not numeric.");
            return false;
        }
        OutLiteral.Set(FCString::Atof(*Trimmed));
        return true;
    }
    if (Type == TEXT("string"))
    {
        OutLiteral.Set(Trimmed);
        return true;
    }
    if (Type == TEXT("object"))
    {
        UObject* Object = Trimmed.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Trimmed);
        if (!Trimmed.IsEmpty() && !Object)
        {
            OutError = TEXT("object literal could not load: ") + Trimmed;
            return false;
        }
        OutLiteral.Set(Object);
        return true;
    }

    const TArray<FString> Values = SplitValues(Trimmed);
    if (Type == TEXT("default_array"))
    {
        const int32 Count = Trimmed.IsEmpty() ? 0 : FCString::Atoi(*Trimmed);
        if (Count < 0)
        {
            OutError = TEXT("default_array count cannot be negative.");
            return false;
        }
        OutLiteral.Set(FMetasoundFrontendLiteral::FDefaultArray{Count});
        return true;
    }
    if (Type == TEXT("bool_array"))
    {
        TArray<bool> Parsed;
        for (const FString& Item : Values)
        {
            const FString Lower = Item.ToLower();
            if (Lower != TEXT("true") && Lower != TEXT("false") && Lower != TEXT("1") && Lower != TEXT("0"))
            {
                OutError = TEXT("bool_array contains an invalid value: ") + Item;
                return false;
            }
            Parsed.Add(Lower == TEXT("true") || Lower == TEXT("1"));
        }
        OutLiteral.Set(MoveTemp(Parsed));
        return true;
    }
    if (Type == TEXT("int_array"))
    {
        TArray<int32> Parsed;
        for (const FString& Item : Values)
        {
            if (!Item.IsNumeric())
            {
                OutError = TEXT("int_array contains a non-numeric value: ") + Item;
                return false;
            }
            Parsed.Add(FCString::Atoi(*Item));
        }
        OutLiteral.Set(MoveTemp(Parsed));
        return true;
    }
    if (Type == TEXT("float_array"))
    {
        TArray<float> Parsed;
        for (const FString& Item : Values)
        {
            if (!Item.IsNumeric())
            {
                OutError = TEXT("float_array contains a non-numeric value: ") + Item;
                return false;
            }
            Parsed.Add(FCString::Atof(*Item));
        }
        OutLiteral.Set(MoveTemp(Parsed));
        return true;
    }
    if (Type == TEXT("string_array"))
    {
        OutLiteral.Set(Values);
        return true;
    }
    if (Type == TEXT("object_array"))
    {
        TArray<UObject*> Objects;
        for (const FString& Item : Values)
        {
            UObject* Object = Item.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Item);
            if (!Item.IsEmpty() && !Object)
            {
                OutError = TEXT("object_array could not load: ") + Item;
                return false;
            }
            Objects.Add(Object);
        }
        OutLiteral.Set(MoveTemp(Objects));
        return true;
    }
    OutError = TEXT("Unsupported MetaSound literal type: ") + InputType;
    return false;
}

bool ResolveNodeHandle(
    UMetaSoundBuilderBase& Builder,
    const TMap<FString, FMetaSoundNodeHandle>& Aliases,
    const FString& Input,
    FMetaSoundNodeHandle& OutHandle)
{
    if (const FMetaSoundNodeHandle* Alias = Aliases.Find(Input))
    {
        OutHandle = *Alias;
        return Builder.ContainsNode(OutHandle);
    }
    FGuid NodeId;
    if (!FGuid::Parse(Input, NodeId))
    {
        return false;
    }
    OutHandle = FMetaSoundNodeHandle(NodeId);
    return Builder.ContainsNode(OutHandle);
}
}

class FThomasEditorAudioModule final : public IThomasEditorAudioProviderModule
{
public:
    virtual void ShutdownModule() override
    {
        Plans.Reset();
        MetaSoundPatchPlans.Reset();
    }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            return MakeError<FThomasDomainInventoryResult>(
                TEXT("assets_provider_unavailable"), FString());
        }
        return Assets->GetDomainInventory(
            TEXT("AudioMetaSound"),
            PackageRoot,
            {
                TEXT("/Script/Engine.SoundWave"),
                TEXT("/Script/Engine.SoundCue"),
                TEXT("/Script/Engine.SoundClass"),
                TEXT("/Script/Engine.SoundMix"),
                TEXT("/Script/Engine.SoundAttenuation"),
                TEXT("/Script/Engine.SoundConcurrency"),
                TEXT("/Script/Engine.DialogueVoice"),
                TEXT("/Script/Engine.DialogueWave"),
                TEXT("/Script/Engine.SoundSubmix"),
                TEXT("/Script/Engine.SoundSourceBus"),
                TEXT("/Script/Engine.SoundEffectSourcePreset"),
                TEXT("/Script/Engine.SoundEffectSubmixPreset"),
                TEXT("/Script/MetasoundEngine.MetaSoundSource"),
                TEXT("/Script/MetasoundEngine.MetaSoundPatch"),
                TEXT("/Script/MetasoundEngine.MetaSoundPreset")
            },
            MaxResults);
    }

    virtual FThomasSpecializedAssetInspectionResult InspectAudioAsset(
        const FString& InputPath,
        const int32 InputMaxItems) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("AudioMetaSound");
        Result.AssetPath = NormalizePath(InputPath);
        UObject* Asset = FindAsset(Result.AssetPath);
        if (!Asset)
        {
            Result.Code = TEXT("asset_not_found");
            return Result;
        }
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.Revision = BuildRevision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();
        const int32 MaxItems = FMath::Clamp(InputMaxItems, 1, 1000);

        UEdGraph* Graph = nullptr;
        if (const UMetaSoundSource* Source = Cast<UMetaSoundSource>(Asset))
        {
            Result.AssetKind = TEXT("meta_sound_source");
            Graph = Source->GetGraph();
        }
        else if (const UMetaSoundPatch* Patch = Cast<UMetaSoundPatch>(Asset))
        {
            Result.AssetKind = TEXT("meta_sound_patch");
            Graph = Patch->GetGraph();
        }
        else
        {
            Result.AssetKind = Asset->GetClass()->GetName();
            Result.Metrics.Add(TEXT("ConfigurationSurface=InspectAssetDetails"));
            Result.Metrics.Add(TEXT("MutationSurface=PlanAssetDetailsPatch"));
            Result.bOk = true;
            return Result;
        }

        const IMetaSoundDocumentInterface* DocumentInterface = Cast<IMetaSoundDocumentInterface>(Asset);
        if (!DocumentInterface)
        {
            Result.Code = TEXT("meta_sound_document_unavailable");
            return Result;
        }
        const FMetasoundFrontendDocument& Document = DocumentInterface->GetConstDocument();
        const FMetasoundFrontendClassInterface& RootInterface = Document.RootGraph.GetDefaultInterface();
        int32 FrontendNodeCount = 0;
        int32 FrontendEdgeCount = 0;
        int32 FrontendVariableCount = 0;
        Document.RootGraph.IterateGraphPages([&](const FMetasoundFrontendGraph& Page)
        {
            FrontendNodeCount += Page.Nodes.Num();
            FrontendEdgeCount += Page.Edges.Num();
            FrontendVariableCount += Page.Variables.Num();
        });
        Result.Metrics.Add(FString::Printf(TEXT("Graph=%s"), Graph ? *Graph->GetPathName() : TEXT("None")));
        Result.Metrics.Add(FString::Printf(TEXT("NodeCount=%d"), FrontendNodeCount));
        Result.Metrics.Add(FString::Printf(TEXT("VisualNodeCount=%d"), Graph ? Graph->Nodes.Num() : 0));
        Result.Metrics.Add(FString::Printf(TEXT("EdgeCount=%d"), FrontendEdgeCount));
        Result.Metrics.Add(FString::Printf(TEXT("InputCount=%d"), RootInterface.Inputs.Num()));
        Result.Metrics.Add(FString::Printf(TEXT("OutputCount=%d"), RootInterface.Outputs.Num()));
        Result.Metrics.Add(FString::Printf(TEXT("VariableCount=%d"), FrontendVariableCount));
        Result.Metrics.Add(FString::Printf(TEXT("InterfaceCount=%d"), Document.Interfaces.Num()));
        auto AddItem = [&](const FString& Item)
        {
            if (Result.Items.Num() < MaxItems)
            {
                Result.Items.Add(Item.Left(1200));
            }
            else
            {
                Result.bTruncated = true;
            }
        };
        for (const FMetasoundFrontendVersion& InterfaceVersion : Document.Interfaces)
        {
            AddItem(TEXT("Interface ") + InterfaceVersion.ToString());
        }
        for (const FMetasoundFrontendClassInput& Input : RootInterface.Inputs)
        {
            AddItem(FString::Printf(TEXT("GraphInput Name=%s Type=%s Node=%s Access=%s Defaults=%d"),
                *Input.Name.ToString(), *Input.TypeName.ToString(),
                *Input.NodeID.ToString(EGuidFormats::DigitsWithHyphens),
                *StaticEnum<EMetasoundFrontendVertexAccessType>()->GetNameStringByValue(
                    static_cast<int64>(Input.AccessType)), Input.GetDefaults().Num()));
        }
        for (const FMetasoundFrontendClassOutput& Output : RootInterface.Outputs)
        {
            AddItem(FString::Printf(TEXT("GraphOutput Name=%s Type=%s Node=%s Access=%s"),
                *Output.Name.ToString(), *Output.TypeName.ToString(),
                *Output.NodeID.ToString(EGuidFormats::DigitsWithHyphens),
                *StaticEnum<EMetasoundFrontendVertexAccessType>()->GetNameStringByValue(
                    static_cast<int64>(Output.AccessType))));
        }
        Document.RootGraph.IterateGraphPages([&](const FMetasoundFrontendGraph& Page)
        {
            for (const FMetasoundFrontendVariable& Variable : Page.Variables)
            {
                AddItem(FString::Printf(TEXT("Variable Page=%s Id=%s Name=%s Type=%s Default=%s"),
                    *Page.PageID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Variable.ID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Variable.Name.ToString(), *Variable.TypeName.ToString(),
                    *Variable.Literal.ToString()));
            }
            for (const FMetasoundFrontendNode& Node : Page.Nodes)
            {
                FVector2D Location = FVector2D::ZeroVector;
#if WITH_EDITORONLY_DATA
                if (Node.Style.Display.Locations.Num() > 0)
                {
                    Location = Node.Style.Display.Locations.CreateConstIterator().Value();
                }
#endif
                AddItem(FString::Printf(
                    TEXT("Node Id=%s Page=%s Name=%s ClassId=%s Position=(%.1f,%.1f) Inputs=%d Outputs=%d Defaults=%d"),
                    *Node.GetID().ToString(EGuidFormats::DigitsWithHyphens),
                    *Page.PageID.ToString(EGuidFormats::DigitsWithHyphens), *Node.Name.ToString(),
                    *Node.ClassID.ToString(EGuidFormats::DigitsWithHyphens), Location.X, Location.Y,
                    Node.Interface.Inputs.Num(), Node.Interface.Outputs.Num(), Node.InputLiterals.Num()));
                for (const FMetasoundFrontendVertex& Input : Node.Interface.Inputs)
                {
                    AddItem(FString::Printf(TEXT("Pin Node=%s Direction=Input Id=%s Name=%s Type=%s"),
                        *Node.GetID().ToString(EGuidFormats::DigitsWithHyphens),
                        *Input.VertexID.ToString(EGuidFormats::DigitsWithHyphens),
                        *Input.Name.ToString(), *Input.TypeName.ToString()));
                }
                for (const FMetasoundFrontendVertex& Output : Node.Interface.Outputs)
                {
                    AddItem(FString::Printf(TEXT("Pin Node=%s Direction=Output Id=%s Name=%s Type=%s"),
                        *Node.GetID().ToString(EGuidFormats::DigitsWithHyphens),
                        *Output.VertexID.ToString(EGuidFormats::DigitsWithHyphens),
                        *Output.Name.ToString(), *Output.TypeName.ToString()));
                }
            }
            for (const FMetasoundFrontendEdge& Edge : Page.Edges)
            {
                AddItem(FString::Printf(TEXT("Edge Page=%s From=%s:%s To=%s:%s"),
                    *Page.PageID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Edge.FromNodeID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Edge.FromVertexID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Edge.ToNodeID.ToString(EGuidFormats::DigitsWithHyphens),
                    *Edge.ToVertexID.ToString(EGuidFormats::DigitsWithHyphens)));
            }
        });

        // The editor graph is a secondary presentation cache. Keep its class/editor GUID
        // information when synchronized, but never use it as the authoring source of truth.
        if (Graph)
        {
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }
                if (Result.Items.Num() >= MaxItems)
                {
                    Result.bTruncated = true;
                    break;
                }
                const UMetasoundEditorGraphNode* MetaSoundNode = Cast<UMetasoundEditorGraphNode>(Node);
                const FGuid FrontendNodeId = MetaSoundNode ? MetaSoundNode->GetNodeID() : Node->NodeGuid;
                Result.Items.Add(FString::Printf(TEXT("Node Id=%s EditorGuid=%s Class=%s Title=%s Position=(%d,%d) Pins=%d"),
                    *FrontendNodeId.ToString(EGuidFormats::DigitsWithHyphens),
                    *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), *Node->GetClass()->GetPathName(),
                    *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), Node->NodePosX, Node->NodePosY,
                    Node->Pins.Num()).Left(1200));
                for (const UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }
                    if (Result.Items.Num() >= MaxItems)
                    {
                        Result.bTruncated = true;
                        break;
                    }
                    Result.Items.Add(FString::Printf(TEXT("Pin Node=%s Direction=%s Name=%s Type=%s Links=%d Default=%s"),
                        *FrontendNodeId.ToString(EGuidFormats::DigitsWithHyphens),
                        Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"),
                        *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), Pin->LinkedTo.Num(),
                        *Pin->DefaultValue).Left(1200));
                }
            }
        }
        Result.bOk = true;
        return Result;
    }

    virtual FThomasSpecializedAssetInspectionResult SearchMetaSoundNodeClasses(
        const FString& InputQuery,
        const int32 InputMaxResults) override
    {
        using namespace Metasound::Frontend;
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("MetaSoundNodeSearch");
        Result.AssetKind = TEXT("meta_sound_node_class");
        const FString Query = InputQuery.TrimStartAndEnd();
        const int32 MaxResults = FMath::Clamp(InputMaxResults, 1, 500);
        ISearchEngine& SearchEngine = ISearchEngine::Get();
        SearchEngine.Prime();
        TArray<FMetaSoundClassInfo> Classes = SearchEngine.FindAllClasses(
            ISearchEngine::EResultVersion::Highest, true);
        Classes.Sort([](const FMetaSoundClassInfo& A, const FMetaSoundClassInfo& B)
        {
            return A.ClassName.ToString() < B.ClassName.ToString();
        });
        int32 Matched = 0;
        for (const FMetaSoundClassInfo& ClassInfo : Classes)
        {
            const FString ClassName = ClassInfo.ClassName.ToString();
            if (!Query.IsEmpty() && !ClassName.Contains(Query, ESearchCase::IgnoreCase))
            {
                continue;
            }
            ++Matched;
            if (Result.Items.Num() < MaxResults)
            {
                Result.Items.Add(FString::Printf(
                    TEXT("Class Name=%s Version=%d.%d Type=%s RegistryValid=%s"),
                    *ClassName, ClassInfo.Version.Major, ClassInfo.Version.Minor,
                    *StaticEnum<EMetasoundFrontendClassType>()->GetNameStringByValue(
                        static_cast<int64>(ClassInfo.ClassType)),
                    ClassInfo.bIsValid ? TEXT("true") : TEXT("false")).Left(1200));
            }
        }
        Result.Metrics = {
            FString::Printf(TEXT("MatchedCount=%d"), Matched),
            FString::Printf(TEXT("ReturnedCount=%d"), Result.Items.Num())
        };
        Result.bTruncated = Matched > Result.Items.Num();
        Result.bOk = true;
        return Result;
    }

    virtual FThomasMetaSoundPlanResult PlanMetaSoundPatch(
        const FThomasMetaSoundPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasMetaSoundPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasMetaSoundPlanResult>(
                TEXT("path_denied"), TEXT("MetaSound asset must remain under /Game/PropHunt."));
        }
        UObject* Asset = FindAsset(Request.AssetPath);
        if (!IsMetaSoundAsset(Asset))
        {
            return MakeError<FThomasMetaSoundPlanResult>(TEXT("meta_sound_not_found"), Request.AssetPath);
        }
        const FString CurrentRevision = BuildRevision(Asset);
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasMetaSoundPlanResult>(TEXT("revision_conflict"), CurrentRevision);
        }
        UPackage* Package = Asset->GetOutermost();
        if (Package->IsDirty())
        {
            return MakeError<FThomasMetaSoundPlanResult>(
                TEXT("dirty_package_denied"), TEXT("Save or revert the MetaSound before planning."));
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
        {
            return MakeError<FThomasMetaSoundPlanResult>(TEXT("read_only_denied"), Filename);
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > 128)
        {
            return MakeError<FThomasMetaSoundPlanResult>(
                TEXT("invalid_operation_count"), FString::FromInt(Request.Operations.Num()));
        }

        const TSet<FString> Supported = {
            TEXT("add_interface"), TEXT("remove_interface"),
            TEXT("add_graph_input"), TEXT("add_graph_output"), TEXT("add_graph_variable"),
            TEXT("remove_graph_input"), TEXT("remove_graph_output"), TEXT("remove_graph_variable"),
            TEXT("add_node_by_class"), TEXT("add_node_from_asset"), TEXT("remove_node"),
            TEXT("connect_nodes"), TEXT("disconnect_nodes"),
            TEXT("connect_graph_input"), TEXT("connect_graph_output"),
            TEXT("set_node_input_default"), TEXT("set_graph_input_default"),
            TEXT("set_node_location")
        };
        FThomasMetaSoundPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = CurrentRevision;
        TSet<FString> Aliases;
        for (FThomasMetaSoundOperation& Operation : Request.Operations)
        {
            Operation.Action = Operation.Action.TrimStartAndEnd().ToLower();
            Operation.ReferencedAssetPath = NormalizePath(Operation.ReferencedAssetPath);
            if (!Supported.Contains(Operation.Action))
            {
                return MakeError<FThomasMetaSoundPlanResult>(TEXT("unsupported_operation"), Operation.Action);
            }
            const bool bDestructive = Operation.Action.StartsWith(TEXT("remove_"))
                || Operation.Action == TEXT("disconnect_nodes");
            if (bDestructive && !Request.bConfirmDestructive)
            {
                return MakeError<FThomasMetaSoundPlanResult>(TEXT("confirmation_required"), Operation.Action);
            }
            if (bDestructive)
            {
                Result.Risk = TEXT("R2");
            }
            if ((Operation.Action == TEXT("add_graph_input")
                    || Operation.Action == TEXT("add_graph_output")
                    || Operation.Action == TEXT("add_graph_variable"))
                && (Operation.Name.IsEmpty() || Operation.DataType.IsEmpty()))
            {
                return MakeError<FThomasMetaSoundPlanResult>(TEXT("invalid_graph_member"), Operation.Name);
            }
            if (Operation.Action == TEXT("add_node_by_class"))
            {
                FMetasoundFrontendClassName ParsedClass;
                if (!FMetasoundFrontendClassName::Parse(Operation.ClassName, ParsedClass)
                    || !ParsedClass.IsValid() || Operation.MajorVersion < 1)
                {
                    return MakeError<FThomasMetaSoundPlanResult>(
                        TEXT("invalid_node_class"), Operation.ClassName);
                }
            }
            if (Operation.Action == TEXT("add_node_from_asset")
                && !IsMetaSoundAsset(FindAsset(Operation.ReferencedAssetPath)))
            {
                return MakeError<FThomasMetaSoundPlanResult>(
                    TEXT("referenced_meta_sound_not_found"), Operation.ReferencedAssetPath);
            }
            if ((Operation.Action.StartsWith(TEXT("add_node"))
                    || Operation.Action == TEXT("add_graph_input")
                    || Operation.Action == TEXT("add_graph_output"))
                && !Operation.ResultId.IsEmpty() && Aliases.Contains(Operation.ResultId))
            {
                return MakeError<FThomasMetaSoundPlanResult>(TEXT("duplicate_result_id"), Operation.ResultId);
            }
            if (!Operation.ResultId.IsEmpty())
            {
                Aliases.Add(Operation.ResultId);
            }
            if (Operation.Action.StartsWith(TEXT("add_graph_"))
                || Operation.Action == TEXT("set_node_input_default")
                || Operation.Action == TEXT("set_graph_input_default"))
            {
                FMetasoundFrontendLiteral Literal;
                FString Error;
                if (!BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, Error))
                {
                    return MakeError<FThomasMetaSoundPlanResult>(TEXT("invalid_literal"), Error);
                }
            }
            Result.Preview.Add((Operation.Action + TEXT(":") + Operation.Name + Operation.ClassName).Left(500));
        }

        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedMetaSoundPatchPlan& Plan = MetaSoundPatchPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasMetaSoundApplyResult ApplyMetaSoundPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedMetaSoundPatchPlan Plan;
        if (!MetaSoundPatchPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasMetaSoundApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UObject* Asset = FindAsset(Plan.Request.AssetPath);
        const FString RevisionBefore = BuildRevision(Asset);
        if (!IsMetaSoundAsset(Asset) || RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasMetaSoundApplyResult>(TEXT("revision_conflict"), RevisionBefore);
        }
        UPackage* Package = Asset->GetOutermost();
        FThomasMetaSoundApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = RevisionBefore;
        const FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "MetaSoundPatch", "ThomasEditor MetaSound Patch"));
        Asset->Modify();
        Package->Modify();
        auto Rollback = [&]()
        {
            Result.bRolledBack = UPackageTools::ReloadPackages({Package});
        };
        auto Fail = [&](const FString& Code, const FString& Message) -> FThomasMetaSoundApplyResult
        {
            Rollback();
            Result.Code = Code;
            Result.Message = Message.Left(1200);
            return Result;
        };

        TScriptInterface<IMetaSoundDocumentInterface> Document = AsMetaSoundDocument(Asset);
        EMetaSoundBuilderResult BuilderResult = EMetaSoundBuilderResult::Failed;
        UMetaSoundEditorSubsystem& EditorSubsystem = UMetaSoundEditorSubsystem::GetChecked();
        UMetaSoundBuilderBase* Builder = EditorSubsystem.FindOrBeginBuilding(Document, BuilderResult);
        if (!Builder || BuilderResult != EMetaSoundBuilderResult::Succeeded)
        {
            return Fail(TEXT("builder_unavailable"), Plan.Request.AssetPath);
        }

        TMap<FString, FMetaSoundNodeHandle> Aliases;
        for (const FThomasMetaSoundOperation& Operation : Plan.Request.Operations)
        {
            BuilderResult = EMetaSoundBuilderResult::Failed;
            FMetaSoundNodeHandle Node;
            FMetaSoundNodeHandle OtherNode;
            FMetaSoundNodeHandle CreatedNode;
            FMetasoundFrontendLiteral Literal;
            FString LiteralError;
            const FString& Action = Operation.Action;

            if (Action == TEXT("add_interface"))
            {
                Builder->AddInterface(FName(Operation.InterfaceName), BuilderResult);
            }
            else if (Action == TEXT("remove_interface"))
            {
                Builder->RemoveInterface(FName(Operation.InterfaceName), BuilderResult);
            }
            else if (Action == TEXT("add_graph_input"))
            {
                BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, LiteralError);
                const FMetaSoundBuilderNodeOutputHandle Handle = Builder->AddGraphInputNode(
                    FName(Operation.Name), FName(Operation.DataType), Literal, BuilderResult, Operation.bConstructor);
                CreatedNode = FMetaSoundNodeHandle(Handle.NodeID);
            }
            else if (Action == TEXT("add_graph_output"))
            {
                BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, LiteralError);
                const FMetaSoundBuilderNodeInputHandle Handle = Builder->AddGraphOutputNode(
                    FName(Operation.Name), FName(Operation.DataType), Literal, BuilderResult, Operation.bConstructor);
                CreatedNode = FMetaSoundNodeHandle(Handle.NodeID);
            }
            else if (Action == TEXT("add_graph_variable"))
            {
                BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, LiteralError);
                Builder->AddGraphVariable(
                    FName(Operation.Name), FName(Operation.DataType), Literal, BuilderResult);
            }
            else if (Action == TEXT("remove_graph_input"))
            {
                Builder->RemoveGraphInput(FName(Operation.Name), BuilderResult);
            }
            else if (Action == TEXT("remove_graph_output"))
            {
                Builder->RemoveGraphOutput(FName(Operation.Name), BuilderResult);
            }
            else if (Action == TEXT("remove_graph_variable"))
            {
                Builder->RemoveGraphVariable(FName(Operation.Name), BuilderResult);
            }
            else if (Action == TEXT("add_node_by_class"))
            {
                FMetasoundFrontendClassName ClassName;
                FMetasoundFrontendClassName::Parse(Operation.ClassName, ClassName);
                CreatedNode = Builder->AddNodeByClassName(ClassName, BuilderResult, Operation.MajorVersion);
            }
            else if (Action == TEXT("add_node_from_asset"))
            {
                UObject* ReferencedAsset = FindAsset(Operation.ReferencedAssetPath);
                CreatedNode = Builder->AddNode(AsMetaSoundDocument(ReferencedAsset), BuilderResult);
            }
            else if (Action == TEXT("remove_node"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId);
                }
                Builder->RemoveNode(Node, BuilderResult, true);
            }
            else if (Action == TEXT("connect_nodes") || Action == TEXT("disconnect_nodes"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node)
                    || !ResolveNodeHandle(*Builder, Aliases, Operation.OtherNodeId, OtherNode))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId + TEXT("/") + Operation.OtherNodeId);
                }
                const FMetaSoundBuilderNodeOutputHandle Output = Builder->FindNodeOutputByName(
                    Node, FName(Operation.OutputName), BuilderResult);
                if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
                {
                    return Fail(TEXT("node_output_not_found"), Operation.OutputName);
                }
                const FMetaSoundBuilderNodeInputHandle Input = Builder->FindNodeInputByName(
                    OtherNode, FName(Operation.InputName), BuilderResult);
                if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
                {
                    return Fail(TEXT("node_input_not_found"), Operation.InputName);
                }
                if (Action == TEXT("connect_nodes"))
                {
                    Builder->ConnectNodes(Output, Input, BuilderResult);
                }
                else
                {
                    Builder->DisconnectNodes(Output, Input, BuilderResult);
                }
            }
            else if (Action == TEXT("connect_graph_input"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId);
                }
                Builder->ConnectGraphInputToNode(
                    FName(Operation.Name), Node, FName(Operation.InputName), BuilderResult);
            }
            else if (Action == TEXT("connect_graph_output"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId);
                }
                Builder->ConnectNodeToGraphOutput(
                    Node, FName(Operation.OutputName), FName(Operation.Name), BuilderResult);
            }
            else if (Action == TEXT("set_node_input_default"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId);
                }
                const FMetaSoundBuilderNodeInputHandle Input = Builder->FindNodeInputByName(
                    Node, FName(Operation.InputName), BuilderResult);
                if (BuilderResult != EMetaSoundBuilderResult::Succeeded
                    || !BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, LiteralError))
                {
                    return Fail(TEXT("node_input_default_failed"), LiteralError + Operation.InputName);
                }
                Builder->SetNodeInputDefault(Input, Literal, BuilderResult);
            }
            else if (Action == TEXT("set_graph_input_default"))
            {
                if (!BuildMetaSoundLiteral(Operation.LiteralType, Operation.Value, Literal, LiteralError))
                {
                    return Fail(TEXT("invalid_literal"), LiteralError);
                }
                Builder->SetGraphInputDefault(FName(Operation.Name), Literal, BuilderResult);
            }
            else if (Action == TEXT("set_node_location"))
            {
                if (!ResolveNodeHandle(*Builder, Aliases, Operation.NodeId, Node))
                {
                    return Fail(TEXT("node_not_found"), Operation.NodeId);
                }
                Builder->SetNodeLocation(
                    Node, FVector2D(Operation.LocationX, Operation.LocationY), BuilderResult);
            }

            if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
            {
                return Fail(TEXT("meta_sound_operation_failed"), Action);
            }
            if (CreatedNode.IsSet())
            {
                const FString CreatedId = CreatedNode.NodeID.ToString(EGuidFormats::DigitsWithHyphens);
                Result.CreatedNodeIds.Add(CreatedId);
                if (!Operation.ResultId.IsEmpty())
                {
                    Aliases.Add(Operation.ResultId, CreatedNode);
                    Result.Diagnostics.Add(Operation.ResultId + TEXT("=") + CreatedId);
                }
            }
            ++Result.AppliedOperationCount;
        }

        Builder->ConformObjectToDocument();
        EditorSubsystem.RegisterGraphWithFrontend(*Asset, true);
        Result.bRegistered = true;
        Asset->PostEditChange();
        Package->MarkPackageDirty();
        if (bSave && !SaveAsset(Asset))
        {
            return Fail(TEXT("save_failed_rolled_back"), Plan.Request.AssetPath);
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Asset);
        return Result;
    }

    virtual FThomasAudioCreatePlanResult PlanAudioAssetCreate(
        const FThomasAudioCreateRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasAudioCreateRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.AssetKind = Request.AssetKind.TrimStartAndEnd().ToLower();
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasAudioCreatePlanResult>(
                TEXT("path_denied"), TEXT("Audio asset must remain under /Game/PropHunt."));
        }
        const FString FactoryClassPath = FactoryClassForKind(Request.AssetKind);
        if (FactoryClassPath.IsEmpty())
        {
            return MakeError<FThomasAudioCreatePlanResult>(
                TEXT("invalid_asset_kind"), Request.AssetKind);
        }
        if (Request.ExpectedRevision != TEXT("missing") || FindAsset(Request.AssetPath))
        {
            return MakeError<FThomasAudioCreatePlanResult>(
                TEXT("asset_exists_or_revision_conflict"), BuildRevision(FindAsset(Request.AssetPath)));
        }
        FString FactoryError;
        if (!CreateFactory(FactoryClassPath, FactoryError))
        {
            return MakeError<FThomasAudioCreatePlanResult>(
                TEXT("audio_factory_unavailable"), FactoryError);
        }

        FThomasAudioCreatePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.AssetKind = Request.AssetKind;
        Result.ExpectedRevision = TEXT("missing");
        Result.FactoryClassPath = FactoryClassPath;
        FCachedAudioCreatePlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.FactoryClassPath = FactoryClassPath;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasAudioCreateApplyResult ApplyAudioAssetCreate(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedAudioCreatePlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasAudioCreateApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FindAsset(Plan.Request.AssetPath))
        {
            return MakeError<FThomasAudioCreateApplyResult>(
                TEXT("revision_conflict"), TEXT("Asset now exists."));
        }
        FString FactoryError;
        UFactory* Factory = CreateFactory(Plan.FactoryClassPath, FactoryError);
        if (!Factory)
        {
            return MakeError<FThomasAudioCreateApplyResult>(
                TEXT("audio_factory_unavailable"), FactoryError);
        }

        UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
        UObject* Asset = Factory->FactoryCreateNew(
            Factory->SupportedClass,
            Package,
            FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn);
        if (!Asset)
        {
            return MakeError<FThomasAudioCreateApplyResult>(
                TEXT("create_failed"), Plan.Request.AssetPath);
        }
        FAssetRegistryModule::AssetCreated(Asset);
        Package->MarkPackageDirty();

        FThomasAudioCreateApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.bCreated = true;
        if (bSave && !SaveAsset(Asset))
        {
            Result.Code = TEXT("save_failed_rolled_back");
            Result.Message = TEXT("The new audio asset could not be saved.");
            Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            return Result;
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Asset);
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
        for (auto It = MetaSoundPatchPlans.CreateIterator(); It; ++It)
        {
            if (Now - It.Value().CreatedAtSeconds > PlanLifetimeSeconds)
            {
                It.RemoveCurrent();
            }
        }
    }

    TMap<FString, FCachedAudioCreatePlan> Plans;
    TMap<FString, FCachedMetaSoundPatchPlan> MetaSoundPatchPlans;
};

IMPLEMENT_MODULE(FThomasEditorAudioModule, ThomasEditorAudio)
