#include "ThomasEditorDataAIProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AIGraph.h"
#include "AIGraphNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQueryGraph.h"
#include "EnvironmentQueryGraphNode.h"
#include "EnvironmentQueryGraphNode_Option.h"
#include "EnvironmentQueryGraphNode_Root.h"
#include "EnvironmentQueryGraphNode_Test.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/DataTableFactory.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "StateTree.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "ThomasEditorAssetsProvider.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxOperations = 100;
constexpr int32 MaxInspectionItems = 500;
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedPlan
{
    FThomasDataAIPatchRequest Request;
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

bool IsAllowedPath(const FString& Path)
{
    return (Path == TEXT("/Game/PropHunt") || Path.StartsWith(TEXT("/Game/PropHunt/")))
        && !Path.Contains(TEXT(".."))
        && !Path.StartsWith(TEXT("/Game/Developers/"));
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

FString ObjectPath(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

UObject* LoadProjectAsset(const FString& AssetPath)
{
    const FString PackageName = NormalizePath(AssetPath);
    if (!IsAllowedPath(PackageName))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPath(PackageName)));
    return AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
}

UStateTreeEditorData* GetStateTreeEditorData(UStateTree* StateTree)
{
    // UE 5.8 declares UStateTreeEditorData::GetEditorData() without exporting it
    // from the MinimalAPI editor class. Read the exact native editor-data property
    // instead; all structural mutations still use the exported Builder APIs.
    const FObjectPropertyBase* EditorDataProperty = StateTree
        ? FindFProperty<FObjectPropertyBase>(
            StateTree->GetClass(), FName(TEXT("EditorData")))
        : nullptr;
    return EditorDataProperty
        ? Cast<UStateTreeEditorData>(
            EditorDataProperty->GetObjectPropertyValue_InContainer(StateTree))
        : nullptr;
}

FString AssetKind(const UObject* Asset)
{
    if (Asset && Asset->IsA<UBlackboardData>()) return TEXT("blackboard");
    if (Asset && Asset->IsA<UBehaviorTree>()) return TEXT("behavior_tree");
    if (Asset && Asset->IsA<UEnvQuery>()) return TEXT("eqs");
    if (Asset && Asset->GetClass()->GetPathName().Contains(TEXT("StateTree")))
        return TEXT("state_tree");
    if (Asset && Asset->IsA<UDataTable>()) return TEXT("data_table");
    if (Asset && Asset->IsA<UDataAsset>()) return TEXT("data_asset");
    return FString();
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
    int32 StructuralCount = 0;
    if (const UDataTable* Table = Cast<UDataTable>(Asset))
    {
        StructuralCount = Table->GetRowMap().Num();
    }
    else if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
    {
        StructuralCount = Blackboard->Keys.Num();
    }
    else if (const UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
    {
        StructuralCount = Tree->RootNode ? 1 : 0;
    }
    else if (const UEnvQuery* Query = Cast<UEnvQuery>(Asset))
    {
        StructuralCount = Query->GetOptions().Num();
    }
    else if (UStateTree* StateTree = Cast<UStateTree>(const_cast<UObject*>(Asset)))
    {
        if (const UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree))
        {
            EditorData->VisitHierarchy(
                [&StructuralCount](UStateTreeState&, UStateTreeState*)
                {
                    ++StructuralCount;
                    return EStateTreeVisitor::Continue;
                });
            StructuralCount += EditorData->Evaluators.Num() + EditorData->GlobalTasks.Num();
        }
    }
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        StructuralCount);
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

FString ExportTableRow(const UDataTable* Table, const uint8* RowData)
{
    FString Value;
    const UScriptStruct* RowStruct = Table ? Table->GetRowStruct() : nullptr;
    if (RowStruct && RowData)
    {
        RowStruct->ExportText(
            Value,
            RowData,
            nullptr,
            const_cast<UDataTable*>(Table),
            PPF_None,
            nullptr);
    }
    return Value.Left(8192);
}

FBlackboardEntry* FindLocalBlackboardKey(UBlackboardData* Blackboard, const FName Name)
{
    return Blackboard
        ? Blackboard->Keys.FindByPredicate(
            [Name](const FBlackboardEntry& Entry) { return Entry.EntryName == Name; })
        : nullptr;
}

const FBlackboardEntry* FindLocalBlackboardKey(
    const UBlackboardData* Blackboard,
    const FName Name)
{
    return Blackboard
        ? Blackboard->Keys.FindByPredicate(
            [Name](const FBlackboardEntry& Entry) { return Entry.EntryName == Name; })
        : nullptr;
}

bool SetExactProperty(
    UObject* Object,
    const FString& PropertyName,
    const FString& ExpectedValue,
    const FString& Value,
    FString& OutError)
{
    FProperty* Property = Object
        ? FindFProperty<FProperty>(Object->GetClass(), FName(*PropertyName))
        : nullptr;
    if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_Transient))
    {
        OutError = FString::Printf(TEXT("Property is not editable: %s"), *PropertyName);
        return false;
    }
    FString CurrentValue;
    Property->ExportText_InContainer(0, CurrentValue, Object, Object, Object, PPF_None);
    FProperty* ImportProperty = Property;
    void* ImportContainer = Object;
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (StructProperty->Struct
            && StructProperty->Struct->IsChildOf(
                FValueOrBlackboardKeyBase::StaticStruct())
            && !Value.TrimStart().StartsWith(TEXT("(")))
        {
            FProperty* DefaultValueProperty = FindFProperty<FProperty>(
                StructProperty->Struct, FName(TEXT("DefaultValue")));
            if (!DefaultValueProperty)
            {
                OutError = FString::Printf(
                    TEXT("Value-or-Blackboard field has no default value: %s"),
                    *PropertyName);
                return false;
            }
            ImportProperty = DefaultValueProperty;
            ImportContainer = StructProperty->ContainerPtrToValuePtr<void>(Object);
            ImportProperty->ExportText_InContainer(
                0, CurrentValue, ImportContainer, ImportContainer, Object, PPF_None);
        }
    }
    if (!ExpectedValue.IsEmpty() && CurrentValue != ExpectedValue)
    {
        OutError = FString::Printf(
            TEXT("Expected '%s', current '%s'."), *ExpectedValue, *CurrentValue);
        return false;
    }
    Object->Modify();
    if (!ImportProperty->ImportText_InContainer(
            *Value, ImportContainer, Object, PPF_None))
    {
        OutError = FString::Printf(TEXT("Invalid value for %s."), *PropertyName);
        return false;
    }
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
    Object->PostEditChangeProperty(Event);
    return true;
}

struct FFactorySpec
{
    const TCHAR* ModuleName = nullptr;
    const TCHAR* FactoryClassPath = nullptr;
};

TOptional<FFactorySpec> FactoryForAIKind(const FString& Kind)
{
    if (Kind == TEXT("blackboard"))
        return FFactorySpec{TEXT("BehaviorTreeEditor"), TEXT("/Script/BehaviorTreeEditor.BlackboardDataFactory")};
    if (Kind == TEXT("behavior_tree"))
        return FFactorySpec{TEXT("BehaviorTreeEditor"), TEXT("/Script/BehaviorTreeEditor.BehaviorTreeFactory")};
    if (Kind == TEXT("eqs"))
        return FFactorySpec{TEXT("EnvironmentQueryEditor"), TEXT("/Script/EnvironmentQueryEditor.EnvironmentQueryFactory")};
    if (Kind == TEXT("state_tree"))
        return FFactorySpec{TEXT("StateTreeEditorModule"), TEXT("/Script/StateTreeEditorModule.StateTreeFactory")};
    return TOptional<FFactorySpec>();
}

UFactory* CreateAIFactory(
    const FString& Kind,
    const FString& SchemaClassPath,
    FString& OutError)
{
    const TOptional<FFactorySpec> Spec = FactoryForAIKind(Kind);
    if (!Spec.IsSet())
    {
        OutError = TEXT("Unknown AI asset kind.");
        return nullptr;
    }
    if (!FModuleManager::Get().ModuleExists(Spec->ModuleName)
        || !FModuleManager::Get().LoadModule(FName(Spec->ModuleName)))
    {
        OutError = FString::Printf(
            TEXT("Native Editor module is unavailable: %s"), Spec->ModuleName);
        return nullptr;
    }
    UClass* FactoryClass = LoadObject<UClass>(nullptr, Spec->FactoryClassPath);
    UFactory* Factory = FactoryClass
        ? NewObject<UFactory>(GetTransientPackage(), FactoryClass)
        : nullptr;
    if (!Factory || !Factory->SupportedClass)
    {
        OutError = FString::Printf(TEXT("Factory is unavailable: %s"), Spec->FactoryClassPath);
        return nullptr;
    }
    if (Kind == TEXT("state_tree"))
    {
        UClass* SchemaClass = LoadObject<UClass>(nullptr, *SchemaClassPath);
        FObjectPropertyBase* SchemaProperty = FindFProperty<FObjectPropertyBase>(
            Factory->GetClass(), FName(TEXT("StateTreeSchemaClass")));
        if (!SchemaClass || !SchemaProperty)
        {
            OutError = TEXT("StateTree creation requires an exact loaded schema ClassPath.");
            return nullptr;
        }
        SchemaProperty->SetObjectPropertyValue_InContainer(Factory, SchemaClass);
    }
    return Factory;
}

UObject* CreateAsset(
    const FThomasDataAIPatchRequest& Request,
    const FThomasDataAIOperation& CreateOperation,
    FString& OutError)
{
    UPackage* Package = CreatePackage(*Request.AssetPath);
    const FName AssetName(*FPackageName::GetLongPackageAssetName(Request.AssetPath));
    const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
    UObject* Asset = nullptr;

    if (Request.AssetKind == TEXT("data_asset"))
    {
        UClass* DataClass = LoadObject<UClass>(nullptr, *CreateOperation.ClassPath);
        if (!DataClass || !DataClass->IsChildOf(UDataAsset::StaticClass())
            || DataClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
        {
            OutError = TEXT("Data Asset class is invalid.");
            return nullptr;
        }
        UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
        Factory->DataAssetClass = DataClass;
        Asset = Factory->FactoryCreateNew(
            DataClass, Package, AssetName, Flags, nullptr, GWarn);
    }
    else if (Request.AssetKind == TEXT("data_table"))
    {
        UScriptStruct* RowStruct = LoadObject<UScriptStruct>(
            nullptr, *CreateOperation.RowStructPath);
        if (!RowStruct || !RowStruct->IsChildOf(FTableRowBase::StaticStruct()))
        {
            OutError = TEXT("Data Table row struct must inherit FTableRowBase.");
            return nullptr;
        }
        UDataTableFactory* Factory = NewObject<UDataTableFactory>();
        Factory->Struct = RowStruct;
        Asset = Factory->FactoryCreateNew(
            UDataTable::StaticClass(), Package, AssetName, Flags, nullptr, GWarn);
    }
    else
    {
        UFactory* Factory = CreateAIFactory(
            Request.AssetKind, CreateOperation.ClassPath, OutError);
        if (!Factory)
        {
            return nullptr;
        }
        Asset = Factory->FactoryCreateNew(
            Factory->SupportedClass, Package, AssetName, Flags, nullptr, GWarn);
    }

    if (Asset)
    {
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
    }
    else if (OutError.IsEmpty())
    {
        OutError = TEXT("Native asset factory returned null.");
    }
    return Asset;
}

bool IsCreateAction(const FString& Action)
{
    return Action == TEXT("create_data_asset")
        || Action == TEXT("create_data_table")
        || Action == TEXT("create_ai_asset");
}

FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits) : FString();
}

template <typename TEnum>
bool ParseEnum(const FString& Input, TEnum& OutValue)
{
    const UEnum* Enum = StaticEnum<TEnum>();
    if (!Enum || Input.IsEmpty())
    {
        return false;
    }
    const int64 Value = Enum->GetValueByNameString(
        Input.TrimStartAndEnd(), EGetByNameFlags::None);
    if (Value == INDEX_NONE)
    {
        return false;
    }
    OutValue = static_cast<TEnum>(Value);
    return true;
}

bool ResolveGuid(
    const FString& IdOrAlias,
    const TMap<FString, FGuid>& Aliases,
    FGuid& OutGuid)
{
    const FString Key = IdOrAlias.TrimStartAndEnd();
    if (const FGuid* AliasGuid = Aliases.Find(Key))
    {
        OutGuid = *AliasGuid;
        return OutGuid.IsValid();
    }
    return FGuid::Parse(Key, OutGuid) && OutGuid.IsValid();
}

struct FStateTreeNodeLocation
{
    FStateTreeEditorNode* Node = nullptr;
    UStateTreeState* State = nullptr;
    FStateTreeTransition* Transition = nullptr;
    FString Role;
};

FStateTreeNodeLocation FindStateTreeNode(
    UStateTreeEditorData* EditorData,
    const FGuid& NodeId)
{
    FStateTreeNodeLocation Result;
    if (!EditorData || !NodeId.IsValid())
    {
        return Result;
    }
    for (FStateTreeEditorNode& Node : EditorData->Evaluators)
    {
        if (Node.ID == NodeId)
        {
            Result.Node = &Node;
            Result.Role = TEXT("evaluator");
            return Result;
        }
    }
    for (FStateTreeEditorNode& Node : EditorData->GlobalTasks)
    {
        if (Node.ID == NodeId)
        {
            Result.Node = &Node;
            Result.Role = TEXT("global_task");
            return Result;
        }
    }
    EditorData->VisitHierarchy(
        [&Result, NodeId](UStateTreeState& State, UStateTreeState*)
        {
            auto FindIn = [&Result, &State, NodeId](
                TArray<FStateTreeEditorNode>& Nodes,
                const TCHAR* Role)
            {
                if (FStateTreeEditorNode* Node = Nodes.FindByPredicate(
                        [NodeId](const FStateTreeEditorNode& Candidate)
                        {
                            return Candidate.ID == NodeId;
                        }))
                {
                    Result.Node = Node;
                    Result.State = &State;
                    Result.Role = Role;
                    return true;
                }
                return false;
            };
            if (FindIn(State.EnterConditions, TEXT("enter_condition"))
                || FindIn(State.Tasks, TEXT("state_task"))
                || FindIn(State.Considerations, TEXT("consideration")))
            {
                return EStateTreeVisitor::Break;
            }
            if (State.SingleTask.ID == NodeId)
            {
                Result.Node = &State.SingleTask;
                Result.State = &State;
                Result.Role = TEXT("single_task");
                return EStateTreeVisitor::Break;
            }
            for (FStateTreeTransition& Transition : State.Transitions)
            {
                if (FStateTreeEditorNode* Node = Transition.Conditions.FindByPredicate(
                        [NodeId](const FStateTreeEditorNode& Candidate)
                        {
                            return Candidate.ID == NodeId;
                        }))
                {
                    Result.Node = Node;
                    Result.State = &State;
                    Result.Transition = &Transition;
                    Result.Role = TEXT("transition_condition");
                    return EStateTreeVisitor::Break;
                }
            }
            return EStateTreeVisitor::Continue;
        });
    return Result;
}

FStateTreeTransition* FindStateTreeTransition(
    UStateTreeEditorData* EditorData,
    const FGuid& TransitionId,
    UStateTreeState** OutOwner = nullptr)
{
    FStateTreeTransition* Result = nullptr;
    if (!EditorData || !TransitionId.IsValid())
    {
        return nullptr;
    }
    EditorData->VisitHierarchy(
        [&Result, OutOwner, TransitionId](UStateTreeState& State, UStateTreeState*)
        {
            Result = State.Transitions.FindByPredicate(
                [TransitionId](const FStateTreeTransition& Transition)
                {
                    return Transition.ID == TransitionId;
                });
            if (Result && OutOwner)
            {
                *OutOwner = &State;
            }
            return Result ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
        });
    return Result;
}

bool IsStateTreeStructAllowedForRole(
    const UScriptStruct* Struct,
    const FString& Role)
{
    if (!Struct)
    {
        return false;
    }
    if (Role == TEXT("state_task") || Role == TEXT("global_task"))
    {
        return Struct->IsChildOf(FStateTreeTaskBase::StaticStruct());
    }
    if (Role == TEXT("enter_condition") || Role == TEXT("transition_condition"))
    {
        return Struct->IsChildOf(FStateTreeConditionBase::StaticStruct());
    }
    if (Role == TEXT("evaluator"))
    {
        return Struct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct());
    }
    return false;
}

bool SetExactDataViewProperty(
    const FStateTreeDataView& View,
    const FString& PropertyName,
    const FString& ExpectedValue,
    const FString& Value,
    FString& OutError)
{
    const UStruct* Struct = View.GetStruct();
    void* Memory = View.GetMutableMemory();
    FProperty* Property = Struct
        ? FindFProperty<FProperty>(Struct, FName(*PropertyName)) : nullptr;
    if (!Memory || !Property || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_Transient))
    {
        OutError = FString::Printf(
            TEXT("StateTree node property is not editable: %s"), *PropertyName);
        return false;
    }
    FString CurrentValue;
    Property->ExportTextItem_Direct(
        CurrentValue,
        Property->ContainerPtrToValuePtr<void>(Memory),
        nullptr,
        nullptr,
        PPF_None);
    if (!ExpectedValue.IsEmpty() && CurrentValue != ExpectedValue)
    {
        OutError = FString::Printf(
            TEXT("Expected '%s', current '%s'."), *ExpectedValue, *CurrentValue);
        return false;
    }
    if (!Property->ImportText_Direct(
            *Value,
            Property->ContainerPtrToValuePtr<void>(Memory),
            nullptr,
            PPF_None))
    {
        OutError = FString::Printf(
            TEXT("Invalid StateTree property value for %s."), *PropertyName);
        return false;
    }
    return true;
}

struct FDataAIApplyContext
{
    TMap<FString, FGuid> Aliases;
};

UAIGraph* GetAIEditorGraph(UObject* Asset)
{
    if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
    {
#if WITH_EDITORONLY_DATA
        return Cast<UAIGraph>(Tree->BTGraph);
#else
        return nullptr;
#endif
    }
    if (UEnvQuery* Query = Cast<UEnvQuery>(Asset))
    {
#if WITH_EDITORONLY_DATA
        const FObjectPropertyBase* GraphProperty = FindFProperty<FObjectPropertyBase>(
            Query->GetClass(), FName(TEXT("EdGraph")));
        return GraphProperty
            ? Cast<UAIGraph>(
                GraphProperty->GetObjectPropertyValue_InContainer(Query))
            : nullptr;
#else
        return nullptr;
#endif
    }
    return nullptr;
}

UAIGraph* EnsureAIEditorGraph(UObject* Asset, FString& OutError)
{
#if WITH_EDITORONLY_DATA
    if (UAIGraph* ExistingGraph = GetAIEditorGraph(Asset))
    {
        return ExistingGraph;
    }

    UAIGraph* NewGraph = nullptr;
    if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
    {
        Tree->Modify();
        const UBehaviorTreeGraph* GraphDefaults =
            GetDefault<UBehaviorTreeGraph>();
        const TSubclassOf<UEdGraphSchema> SchemaClass =
            GraphDefaults ? GraphDefaults->Schema : nullptr;
        if (!SchemaClass)
        {
            OutError = TEXT("Behavior Tree graph schema is unavailable.");
            return nullptr;
        }
        Tree->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
            Tree,
            FName(TEXT("Behavior Tree")),
            UBehaviorTreeGraph::StaticClass(),
            SchemaClass);
        NewGraph = Cast<UAIGraph>(Tree->BTGraph);
    }
    else if (UEnvQuery* Query = Cast<UEnvQuery>(Asset))
    {
        Query->Modify();
        const FObjectPropertyBase* GraphProperty = FindFProperty<FObjectPropertyBase>(
            Query->GetClass(), FName(TEXT("EdGraph")));
        if (!GraphProperty)
        {
            OutError = TEXT("EQS EdGraph property is unavailable.");
            return nullptr;
        }
        NewGraph = NewObject<UEnvironmentQueryGraph>(
            Query,
            UEnvironmentQueryGraph::StaticClass(),
            NAME_None,
            RF_Transactional);
        GraphProperty->SetObjectPropertyValue_InContainer(Query, NewGraph);
    }
    else
    {
        return nullptr;
    }

    if (!NewGraph || !NewGraph->GetSchema())
    {
        OutError = TEXT("Native AI editor graph creation failed.");
        return nullptr;
    }
    NewGraph->GetSchema()->CreateDefaultNodesForGraph(*NewGraph);
    NewGraph->OnCreated();
    NewGraph->Initialize();
    return NewGraph;
#else
    OutError = TEXT("AI editor graph authoring requires editor-only data.");
    return nullptr;
#endif
}

UAIGraphNode* FindAIGraphNode(
    UAIGraph* Graph,
    const FString& IdOrAlias,
    const TMap<FString, FGuid>& Aliases)
{
    FGuid NodeId;
    if (!Graph || !ResolveGuid(IdOrAlias, Aliases, NodeId))
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UAIGraphNode* AINode = Cast<UAIGraphNode>(Node))
        {
            if (AINode->NodeGuid == NodeId)
            {
                return AINode;
            }
            for (UAIGraphNode* SubNode : AINode->SubNodes)
            {
                if (SubNode && SubNode->NodeGuid == NodeId)
                {
                    return SubNode;
                }
            }
        }
    }
    return nullptr;
}

FString GetAIGraphNodeRole(const UAIGraphNode* Node)
{
    if (Node->IsA<UBehaviorTreeGraphNode_Root>()
        || Node->IsA<UEnvironmentQueryGraphNode_Root>())
        return TEXT("root");
    if (Node->IsA<UBehaviorTreeGraphNode_Composite>()) return TEXT("composite");
    if (Node->IsA<UBehaviorTreeGraphNode_Task>()) return TEXT("task");
    if (Node->IsA<UBehaviorTreeGraphNode_Decorator>()) return TEXT("decorator");
    if (Node->IsA<UBehaviorTreeGraphNode_Service>()) return TEXT("service");
    if (Node->IsA<UEnvironmentQueryGraphNode_Option>()) return TEXT("generator");
    if (Node->IsA<UEnvironmentQueryGraphNode_Test>()) return TEXT("test");
    return TEXT("unknown");
}

void AppendAIGraphInspection(
    UAIGraph* Graph,
    const int32 MaxItems,
    FThomasAIAssetInspectionResult& Result)
{
    if (!Graph)
    {
        Result.Diagnostics.Add(TEXT("ai_editor_graph_unavailable"));
        return;
    }
    auto AddNodeRecord = [&Result, MaxItems](
        const UAIGraphNode* Node,
        const FString& OwnerId)
    {
        if (!Node || Result.Nodes.Num() >= MaxItems)
        {
            return;
        }
        FThomasAINodeRecord Record;
        Record.Id = GuidString(Node->NodeGuid);
        Record.OwnerId = OwnerId;
        Record.Role = GetAIGraphNodeRole(Node);
        Record.Name = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Left(500);
        Record.InstanceTypePath = Node->NodeInstance
            ? Node->NodeInstance->GetClass()->GetPathName() : FString();
        Record.StructPath = Record.InstanceTypePath;
        if (const UEnvQueryOption* Option = Cast<UEnvQueryOption>(Node->NodeInstance))
        {
            Record.StructPath = Option->Generator
                ? Option->Generator->GetClass()->GetPathName() : FString();
        }
        if (const UEnvironmentQueryGraphNode_Test* TestNode =
                Cast<UEnvironmentQueryGraphNode_Test>(Node))
        {
            Record.bEnabled = TestNode->bTestEnabled;
        }
        Record.PositionX = Node->NodePosX;
        Record.PositionY = Node->NodePosY;
        Result.Nodes.Add(MoveTemp(Record));
    };

    int32 TotalNodeCount = 0;
    for (UEdGraphNode* GraphNode : Graph->Nodes)
    {
        UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode);
        if (!AINode)
        {
            continue;
        }
        ++TotalNodeCount;
        AddNodeRecord(AINode, FString());
        const FString OwnerId = GuidString(AINode->NodeGuid);
        for (UAIGraphNode* SubNode : AINode->SubNodes)
        {
            if (!SubNode)
            {
                continue;
            }
            ++TotalNodeCount;
            AddNodeRecord(SubNode, OwnerId);
        }
        if (UEdGraphPin* OutputPin = AINode->GetOutputPin())
        {
            for (const UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
            {
                const UAIGraphNode* LinkedNode = LinkedPin
                    ? Cast<UAIGraphNode>(LinkedPin->GetOwningNode()) : nullptr;
                if (!LinkedNode || Result.Edges.Num() >= MaxItems)
                {
                    continue;
                }
                FThomasAIGraphEdgeRecord Edge;
                Edge.FromId = OwnerId;
                Edge.ToId = GuidString(LinkedNode->NodeGuid);
                Result.Edges.Add(MoveTemp(Edge));
            }
        }
    }
    Result.GraphNodeCount = TotalNodeCount;
    if (TotalNodeCount > Result.Nodes.Num())
    {
        Result.Diagnostics.Add(TEXT("ai_graph_nodes_truncated"));
    }
}

bool IsBehaviorTreeClassForRole(const UClass* Class, const FString& Role)
{
    if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
    {
        return false;
    }
    if (Role == TEXT("composite")) return Class->IsChildOf(UBTCompositeNode::StaticClass());
    if (Role == TEXT("task")) return Class->IsChildOf(UBTTaskNode::StaticClass());
    if (Role == TEXT("decorator")) return Class->IsChildOf(UBTDecorator::StaticClass());
    if (Role == TEXT("service")) return Class->IsChildOf(UBTService::StaticClass());
    return false;
}

UClass* BehaviorGraphNodeClassForRole(const FString& Role)
{
    if (Role == TEXT("composite")) return UBehaviorTreeGraphNode_Composite::StaticClass();
    if (Role == TEXT("task")) return UBehaviorTreeGraphNode_Task::StaticClass();
    if (Role == TEXT("decorator")) return UBehaviorTreeGraphNode_Decorator::StaticClass();
    if (Role == TEXT("service")) return UBehaviorTreeGraphNode_Service::StaticClass();
    return nullptr;
}

void ConfigureAIGraphNode(
    UAIGraphNode* Node,
    const UClass* RuntimeClass,
    const int32 PositionX,
    const int32 PositionY)
{
    UAIGraphNode::UpdateNodeClassDataFrom(
        const_cast<UClass*>(RuntimeClass), Node->ClassData);
    Node->NodePosX = PositionX;
    Node->NodePosY = PositionY;
}

bool ConnectAIGraphNodes(
    UAIGraph* Graph,
    UAIGraphNode* Source,
    UAIGraphNode* Target,
    FString& OutError)
{
    UEdGraphPin* OutputPin = Source ? Source->GetOutputPin() : nullptr;
    UEdGraphPin* InputPin = Target ? Target->GetInputPin() : nullptr;
    const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
    if (!OutputPin || !InputPin || !Schema
        || !Schema->TryCreateConnection(OutputPin, InputPin))
    {
        OutError = TEXT("AI graph connection is invalid, cyclic, or violates the native schema.");
        return false;
    }
    return true;
}

bool DisconnectAIGraphNodes(
    UAIGraphNode* Source,
    UAIGraphNode* Target,
    FString& OutError)
{
    UEdGraphPin* OutputPin = Source ? Source->GetOutputPin() : nullptr;
    UEdGraphPin* InputPin = Target ? Target->GetInputPin() : nullptr;
    if (!OutputPin || !InputPin || !OutputPin->LinkedTo.Contains(InputPin))
    {
        OutError = TEXT("AI graph connection was not found.");
        return false;
    }
    OutputPin->BreakLinkTo(InputPin);
    return true;
}

bool ApplyOperation(
    UObject* Asset,
    const FThomasDataAIOperation& Operation,
    FDataAIApplyContext& ApplyContext,
    FString& OutError)
{
    const FString Action = Operation.Action.ToLower();
    if (IsCreateAction(Action))
    {
        return true;
    }
    if (Action == TEXT("set_table_row"))
    {
        UDataTable* Table = Cast<UDataTable>(Asset);
        const UScriptStruct* RowStruct = Table ? Table->GetRowStruct() : nullptr;
        if (!Table || !RowStruct || Operation.Name.IsEmpty())
        {
            OutError = TEXT("set_table_row requires a Data Table, row struct and Name.");
            return false;
        }
        if (const uint8* const* Current = Table->GetRowMap().Find(FName(*Operation.Name)))
        {
            const FString CurrentValue = ExportTableRow(Table, *Current);
            if (!Operation.ExpectedValue.IsEmpty() && CurrentValue != Operation.ExpectedValue)
            {
                OutError = TEXT("Data Table row value changed after planning.");
                return false;
            }
        }
        FStructOnScope RowScope(RowStruct);
        if (!RowStruct->ImportText(
                *Operation.Value,
                RowScope.GetStructMemory(),
                Table,
                PPF_None,
                GWarn,
                RowStruct->GetName()))
        {
            OutError = FString::Printf(TEXT("Invalid row value: %s"), *Operation.Name);
            return false;
        }
        Table->Modify();
        Table->RemoveRow(FName(*Operation.Name));
        Table->AddRow(
            FName(*Operation.Name), RowScope.GetStructMemory(), RowStruct);
        Table->HandleDataTableChanged(FName(*Operation.Name));
        return true;
    }
    if (Action == TEXT("remove_table_row"))
    {
        UDataTable* Table = Cast<UDataTable>(Asset);
        if (!Table || !Table->GetRowMap().Contains(FName(*Operation.Name)))
        {
            OutError = FString::Printf(TEXT("Data Table row not found: %s"), *Operation.Name);
            return false;
        }
        Table->Modify();
        Table->RemoveRow(FName(*Operation.Name));
        Table->HandleDataTableChanged(FName(*Operation.Name));
        return true;
    }
    if (Action == TEXT("set_behavior_tree_blackboard"))
    {
        UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
        UBlackboardData* Blackboard = Operation.ObjectPath.IsEmpty()
            ? nullptr : Cast<UBlackboardData>(LoadProjectAsset(Operation.ObjectPath));
        const FString Current = Tree && Tree->BlackboardAsset
            ? NormalizePath(Tree->BlackboardAsset->GetOutermost()->GetName()) : FString();
        if (!Tree || (!Operation.ObjectPath.IsEmpty() && !Blackboard)
            || (!Operation.ExpectedValue.IsEmpty()
                && Current != NormalizePath(Operation.ExpectedValue)))
        {
            OutError = TEXT("Behavior Tree Blackboard reference is invalid or changed.");
            return false;
        }
        Tree->Modify();
        Tree->BlackboardAsset = Blackboard;
        Tree->PostEditChange();
        return true;
    }
    if (Action == TEXT("set_blackboard_parent"))
    {
        UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset);
        UBlackboardData* Parent = Operation.ObjectPath.IsEmpty()
            ? nullptr : Cast<UBlackboardData>(LoadProjectAsset(Operation.ObjectPath));
        const FString Current = Blackboard && Blackboard->Parent
            ? NormalizePath(Blackboard->Parent->GetOutermost()->GetName()) : FString();
        if (!Blackboard || Parent == Blackboard
            || (!Operation.ObjectPath.IsEmpty() && !Parent)
            || (Parent && Parent->IsChildOf(*Blackboard))
            || (!Operation.ExpectedValue.IsEmpty()
                && Current != NormalizePath(Operation.ExpectedValue)))
        {
            OutError = TEXT("Blackboard parent is invalid, cyclic, or changed.");
            return false;
        }
        Blackboard->Modify();
        Blackboard->Parent = Parent;
        Blackboard->UpdateParentKeys();
        Blackboard->UpdateKeyIDs();
        Blackboard->PostEditChange();
        return true;
    }
    if (Action == TEXT("add_blackboard_key"))
    {
        UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset);
        UClass* KeyClass = LoadObject<UClass>(nullptr, *Operation.KeyTypeClassPath);
        if (!Blackboard || Operation.Name.IsEmpty()
            || FindLocalBlackboardKey(Blackboard, FName(*Operation.Name))
            || !KeyClass || !KeyClass->IsChildOf(UBlackboardKeyType::StaticClass())
            || KeyClass->HasAnyClassFlags(CLASS_Abstract))
        {
            OutError = FString::Printf(TEXT("Invalid Blackboard key: %s"), *Operation.Name);
            return false;
        }
        Blackboard->Modify();
        FBlackboardEntry Entry;
        Entry.EntryName = FName(*Operation.Name);
#if WITH_EDITORONLY_DATA
        Entry.EntryDescription = Operation.Description.Left(1200);
        Entry.EntryCategory = FName(*Operation.Category);
#endif
        Entry.bInstanceSynced = Operation.bInstanceSynced;
        Entry.KeyType = NewObject<UBlackboardKeyType>(
            Blackboard, KeyClass, NAME_None, RF_Transactional);
        if (!Entry.KeyType)
        {
            OutError = TEXT("Could not create Blackboard key type.");
            return false;
        }
        if (!Operation.KeyTypePropertyName.IsEmpty()
            && !SetExactProperty(
                Entry.KeyType,
                Operation.KeyTypePropertyName,
                FString(),
                Operation.KeyTypePropertyValue,
                OutError))
        {
            return false;
        }
        Blackboard->Keys.Add(MoveTemp(Entry));
        Blackboard->UpdateParentKeys();
        Blackboard->UpdateKeyIDs();
        Blackboard->UpdateIfHasSynchronizedKeys();
        Blackboard->PostEditChange();
        return true;
    }
    if (Action == TEXT("set_blackboard_key_property"))
    {
        UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset);
        FBlackboardEntry* Entry = FindLocalBlackboardKey(
            Blackboard, FName(*Operation.Name));
        if (!Entry || !Entry->KeyType
            || !SetExactProperty(
                Entry->KeyType,
                Operation.KeyTypePropertyName,
                Operation.ExpectedValue,
                Operation.KeyTypePropertyValue,
                OutError))
        {
            return false;
        }
        Blackboard->Modify();
        Blackboard->PostEditChange();
        return true;
    }
    if (Action == TEXT("remove_blackboard_key"))
    {
        UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset);
        const int32 Index = Blackboard
            ? Blackboard->Keys.IndexOfByPredicate(
                [&Operation](const FBlackboardEntry& Entry)
                {
                    return Entry.EntryName == FName(*Operation.Name);
                })
            : INDEX_NONE;
        if (!Blackboard || Index == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("Blackboard key not found: %s"), *Operation.Name);
            return false;
        }
        Blackboard->Modify();
        Blackboard->Keys.RemoveAt(Index);
        Blackboard->UpdateParentKeys();
        Blackboard->UpdateKeyIDs();
        Blackboard->UpdateIfHasSynchronizedKeys();
        Blackboard->PostEditChange();
        return true;
    }
    if (Action == TEXT("add_state"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        EStateTreeStateType StateType = EStateTreeStateType::State;
        if (!EditorData || Operation.Name.IsEmpty()
            || !ParseEnum(Operation.StateType, StateType)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("add_state requires a StateTree, Name, valid StateType, and unique ResultId.");
            return false;
        }
        UStateTreeState* Parent = nullptr;
        if (!Operation.ParentId.IsEmpty())
        {
            FGuid ParentId;
            if (!ResolveGuid(Operation.ParentId, ApplyContext.Aliases, ParentId))
            {
                OutError = TEXT("StateTree parent ID or alias is invalid.");
                return false;
            }
            Parent = EditorData->GetMutableStateByID(ParentId);
            if (!Parent)
            {
                OutError = TEXT("StateTree parent state was not found.");
                return false;
            }
        }
        EditorData->Modify();
        UStateTreeState& NewState = Parent
            ? Parent->AddChildState(FName(*Operation.Name), StateType)
            : EditorData->AddSubTree(FName(*Operation.Name), StateType);
        NewState.Modify();
        if (!Operation.Description.IsEmpty())
        {
            NewState.Description = Operation.Description.Left(1200);
        }
        if (!Operation.SelectionBehavior.IsEmpty())
        {
            EStateTreeStateSelectionBehavior SelectionBehavior =
                EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
            if (!ParseEnum(Operation.SelectionBehavior, SelectionBehavior))
            {
                OutError = TEXT("StateTree SelectionBehavior is invalid.");
                return false;
            }
            NewState.SelectionBehavior = SelectionBehavior;
        }
        NewState.bEnabled = Operation.bEnabled;
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, NewState.ID);
        }
        return true;
    }
    if (Action == TEXT("remove_state"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid StateId;
        if (!EditorData
            || !ResolveGuid(Operation.StateId, ApplyContext.Aliases, StateId))
        {
            OutError = TEXT("remove_state requires an exact StateTree state GUID or alias.");
            return false;
        }
        UStateTreeState* State = EditorData->GetMutableStateByID(StateId);
        if (!State)
        {
            OutError = TEXT("StateTree state was not found.");
            return false;
        }
        bool bHasIncomingTransition = false;
        EditorData->VisitHierarchy(
            [&bHasIncomingTransition, StateId](UStateTreeState& Candidate, UStateTreeState*)
            {
                bHasIncomingTransition = Candidate.Transitions.ContainsByPredicate(
                    [StateId](const FStateTreeTransition& Transition)
                    {
                        return Transition.State.ID == StateId;
                    });
                return bHasIncomingTransition
                    ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
            });
        if (bHasIncomingTransition)
        {
            OutError = TEXT("Remove incoming StateTree transitions before removing their target state.");
            return false;
        }
        EditorData->Modify();
        TArray<TObjectPtr<UStateTreeState>>& OwnerArray = State->Parent
            ? State->Parent->Children : EditorData->SubTrees;
        if (OwnerArray.Remove(State) != 1)
        {
            OutError = TEXT("StateTree state ownership changed after planning.");
            return false;
        }
        EditorData->ReparentStates();
        return true;
    }
    if (Action == TEXT("rename_state")
        || Action == TEXT("set_state_description")
        || Action == TEXT("set_state_enabled")
        || Action == TEXT("set_state_selection_behavior"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid StateId;
        UStateTreeState* State = EditorData
            && ResolveGuid(Operation.StateId, ApplyContext.Aliases, StateId)
            ? EditorData->GetMutableStateByID(StateId) : nullptr;
        if (!State)
        {
            OutError = TEXT("StateTree state was not found.");
            return false;
        }
        State->Modify();
        if (Action == TEXT("rename_state"))
        {
            if (Operation.Name.IsEmpty())
            {
                OutError = TEXT("rename_state requires Name.");
                return false;
            }
            State->Name = FName(*Operation.Name);
        }
        else if (Action == TEXT("set_state_description"))
        {
            State->Description = Operation.Description.Left(1200);
        }
        else if (Action == TEXT("set_state_enabled"))
        {
            State->bEnabled = Operation.bEnabled;
        }
        else
        {
            EStateTreeStateSelectionBehavior SelectionBehavior =
                EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
            if (!ParseEnum(Operation.SelectionBehavior, SelectionBehavior))
            {
                OutError = TEXT("StateTree SelectionBehavior is invalid.");
                return false;
            }
            State->SelectionBehavior = SelectionBehavior;
        }
        return true;
    }
    if (Action == TEXT("add_state_task")
        || Action == TEXT("add_enter_condition")
        || Action == TEXT("add_global_task")
        || Action == TEXT("add_evaluator")
        || Action == TEXT("add_transition_condition"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        const FString Role = Action == TEXT("add_state_task") ? TEXT("state_task")
            : Action == TEXT("add_enter_condition") ? TEXT("enter_condition")
            : Action == TEXT("add_global_task") ? TEXT("global_task")
            : Action == TEXT("add_evaluator") ? TEXT("evaluator")
            : TEXT("transition_condition");
        UScriptStruct* NodeStruct = LoadObject<UScriptStruct>(nullptr, *Operation.ClassPath);
        if (!EditorData || !IsStateTreeStructAllowedForRole(NodeStruct, Role)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("StateTree node struct, role, or ResultId is invalid.");
            return false;
        }
        FStateTreeEditorNode* NewNode = nullptr;
        UObject* NodeOuter = EditorData;
        if (Role == TEXT("global_task"))
        {
            EditorData->Modify();
            NewNode = &EditorData->GlobalTasks.AddDefaulted_GetRef();
        }
        else if (Role == TEXT("evaluator"))
        {
            EditorData->Modify();
            NewNode = &EditorData->Evaluators.AddDefaulted_GetRef();
        }
        else if (Role == TEXT("transition_condition"))
        {
            FGuid TransitionId;
            UStateTreeState* Owner = nullptr;
            FStateTreeTransition* Transition =
                ResolveGuid(Operation.ItemId, ApplyContext.Aliases, TransitionId)
                ? FindStateTreeTransition(EditorData, TransitionId, &Owner) : nullptr;
            if (!Transition || !Owner)
            {
                OutError = TEXT("StateTree transition was not found for its condition.");
                return false;
            }
            Owner->Modify();
            NewNode = &Transition->Conditions.AddDefaulted_GetRef();
            NodeOuter = Owner;
        }
        else
        {
            FGuid StateId;
            UStateTreeState* State =
                ResolveGuid(Operation.StateId, ApplyContext.Aliases, StateId)
                ? EditorData->GetMutableStateByID(StateId) : nullptr;
            if (!State)
            {
                OutError = TEXT("StateTree state was not found for its node.");
                return false;
            }
            State->Modify();
            NewNode = Role == TEXT("state_task")
                ? &State->Tasks.AddDefaulted_GetRef()
                : &State->EnterConditions.AddDefaulted_GetRef();
            NodeOuter = State;
        }
        NewNode->InitializeAs(NodeOuter, NodeStruct);
        if (!Operation.Name.IsEmpty())
        {
            NewNode->SetNodeName(FName(*Operation.Name));
        }
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, NewNode->ID);
        }
        return true;
    }
    if (Action == TEXT("remove_state_tree_node"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid NodeId;
        FStateTreeNodeLocation Location = EditorData
            && ResolveGuid(Operation.ItemId, ApplyContext.Aliases, NodeId)
            ? FindStateTreeNode(EditorData, NodeId) : FStateTreeNodeLocation();
        if (!Location.Node)
        {
            OutError = TEXT("StateTree node was not found.");
            return false;
        }
        auto RemoveNode = [NodeId](TArray<FStateTreeEditorNode>& Nodes)
        {
            return Nodes.RemoveAll(
                [NodeId](const FStateTreeEditorNode& Node) { return Node.ID == NodeId; });
        };
        int32 Removed = 0;
        if (Location.Role == TEXT("evaluator"))
        {
            EditorData->Modify();
            Removed = RemoveNode(EditorData->Evaluators);
        }
        else if (Location.Role == TEXT("global_task"))
        {
            EditorData->Modify();
            Removed = RemoveNode(EditorData->GlobalTasks);
        }
        else if (Location.Role == TEXT("transition_condition"))
        {
            Location.State->Modify();
            Removed = RemoveNode(Location.Transition->Conditions);
        }
        else
        {
            Location.State->Modify();
            TArray<FStateTreeEditorNode>* Nodes = Location.Role == TEXT("state_task")
                ? &Location.State->Tasks
                : Location.Role == TEXT("enter_condition")
                    ? &Location.State->EnterConditions : &Location.State->Considerations;
            Removed = RemoveNode(*Nodes);
        }
        if (Removed != 1)
        {
            OutError = TEXT("StateTree node ownership changed after planning.");
            return false;
        }
        return true;
    }
    if (Action == TEXT("set_state_tree_node_property"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid NodeId;
        FStateTreeNodeLocation Location = EditorData
            && ResolveGuid(Operation.ItemId, ApplyContext.Aliases, NodeId)
            ? FindStateTreeNode(EditorData, NodeId) : FStateTreeNodeLocation();
        if (!Location.Node || Operation.KeyTypePropertyName.IsEmpty())
        {
            OutError = TEXT("StateTree node or property was not found.");
            return false;
        }
        if (Location.State)
        {
            Location.State->Modify();
        }
        else
        {
            EditorData->Modify();
        }
        const FString Scope = Operation.PropertyScope.ToLower();
        FStateTreeDataView View;
        if (Scope == TEXT("node"))
        {
            View = FStateTreeDataView(
                Location.Node->Node.GetScriptStruct(),
                Location.Node->Node.GetMutableMemory());
        }
        else if (Scope == TEXT("instance"))
        {
            View = Location.Node->GetInstance();
        }
        else if (Scope == TEXT("execution_runtime"))
        {
            View = Location.Node->GetExecutionRuntimeData();
        }
        else
        {
            OutError = TEXT("PropertyScope must be node, instance, or execution_runtime.");
            return false;
        }
        return SetExactDataViewProperty(
            View,
            Operation.KeyTypePropertyName,
            Operation.ExpectedValue,
            Operation.KeyTypePropertyValue,
            OutError);
    }
    if (Action == TEXT("add_state_transition"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid OwnerId;
        UStateTreeState* Owner = EditorData
            && ResolveGuid(Operation.StateId, ApplyContext.Aliases, OwnerId)
            ? EditorData->GetMutableStateByID(OwnerId) : nullptr;
        EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::None;
        EStateTreeTransitionType Type = EStateTreeTransitionType::None;
        if (!Owner || !ParseEnum(Operation.TransitionTrigger, Trigger)
            || !ParseEnum(Operation.TransitionType, Type)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("StateTree transition owner, trigger, type, or ResultId is invalid.");
            return false;
        }
        UStateTreeState* Target = nullptr;
        if (Type == EStateTreeTransitionType::GotoState)
        {
            FGuid TargetId;
            Target = ResolveGuid(
                Operation.TargetStateId, ApplyContext.Aliases, TargetId)
                ? EditorData->GetMutableStateByID(TargetId) : nullptr;
            if (!Target)
            {
                OutError = TEXT("GotoState requires an exact target state GUID or alias.");
                return false;
            }
        }
        Owner->Modify();
        FStateTreeTransition& Transition = Owner->AddTransition(Trigger, Type, Target);
        Transition.bTransitionEnabled = Operation.bEnabled;
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, Transition.ID);
        }
        return true;
    }
    if (Action == TEXT("remove_state_transition"))
    {
        UStateTree* StateTree = Cast<UStateTree>(Asset);
        UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
        FGuid TransitionId;
        UStateTreeState* Owner = nullptr;
        FStateTreeTransition* Transition = EditorData
            && ResolveGuid(Operation.ItemId, ApplyContext.Aliases, TransitionId)
            ? FindStateTreeTransition(EditorData, TransitionId, &Owner) : nullptr;
        if (!Transition || !Owner)
        {
            OutError = TEXT("StateTree transition was not found.");
            return false;
        }
        Owner->Modify();
        if (Owner->Transitions.RemoveAll(
                [TransitionId](const FStateTreeTransition& Candidate)
                {
                    return Candidate.ID == TransitionId;
                }) != 1)
        {
            OutError = TEXT("StateTree transition ownership changed after planning.");
            return false;
        }
        return true;
    }
    if (Action == TEXT("add_behavior_tree_node"))
    {
        UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
        UAIGraph* Graph = GetAIEditorGraph(Tree);
        const FString Role = Operation.NodeRole.ToLower();
        UClass* RuntimeClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
        UClass* GraphNodeClass = BehaviorGraphNodeClassForRole(Role);
        if (!Tree || !Graph || (Role != TEXT("composite") && Role != TEXT("task"))
            || !GraphNodeClass || !IsBehaviorTreeClassForRole(RuntimeClass, Role)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("Behavior Tree main node role, class, graph, or ResultId is invalid.");
            return false;
        }
        Graph->Modify();
        UBehaviorTreeGraphNode* Node = nullptr;
        {
            FGraphNodeCreator<UBehaviorTreeGraphNode> NodeCreator(*Graph);
            Node = NodeCreator.CreateNode(false, GraphNodeClass);
            ConfigureAIGraphNode(
                Node, RuntimeClass, Operation.PositionX, Operation.PositionY);
            NodeCreator.Finalize();
        }
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, Node->NodeGuid);
        }
        return true;
    }
    if (Action == TEXT("add_behavior_tree_aux_node"))
    {
        UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset);
        UAIGraph* Graph = GetAIEditorGraph(Tree);
        const FString Role = Operation.NodeRole.ToLower();
        UClass* RuntimeClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
        UClass* GraphNodeClass = BehaviorGraphNodeClassForRole(Role);
        UBehaviorTreeGraphNode* Parent = Cast<UBehaviorTreeGraphNode>(
            FindAIGraphNode(Graph, Operation.ParentId, ApplyContext.Aliases));
        if (!Tree || !Graph || (Role != TEXT("decorator") && Role != TEXT("service"))
            || !Parent || GetAIGraphNodeRole(Parent) == TEXT("root")
            || !GraphNodeClass || !IsBehaviorTreeClassForRole(RuntimeClass, Role)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("Behavior Tree auxiliary node owner, role, class, or ResultId is invalid.");
            return false;
        }
        UBehaviorTreeGraphNode* Node = NewObject<UBehaviorTreeGraphNode>(
            Graph, GraphNodeClass, NAME_None, RF_Transactional);
        UAIGraphNode::UpdateNodeClassDataFrom(RuntimeClass, Node->ClassData);
        Parent->AddSubNode(Node, Graph);
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, Node->NodeGuid);
        }
        return true;
    }
    if (Action == TEXT("connect_ai_graph_nodes")
        || Action == TEXT("disconnect_ai_graph_nodes"))
    {
        UAIGraph* Graph = GetAIEditorGraph(Asset);
        UAIGraphNode* Source = FindAIGraphNode(
            Graph, Operation.SourceId, ApplyContext.Aliases);
        UAIGraphNode* Target = FindAIGraphNode(
            Graph, Operation.TargetId, ApplyContext.Aliases);
        if (!Graph || !Source || !Target || Source->IsSubNode() || Target->IsSubNode())
        {
            OutError = TEXT("AI graph SourceId or TargetId is invalid.");
            return false;
        }
        Graph->Modify();
        Source->Modify();
        Target->Modify();
        return Action == TEXT("connect_ai_graph_nodes")
            ? ConnectAIGraphNodes(Graph, Source, Target, OutError)
            : DisconnectAIGraphNodes(Source, Target, OutError);
    }
    if (Action == TEXT("add_eqs_option"))
    {
        UEnvQuery* Query = Cast<UEnvQuery>(Asset);
        UAIGraph* Graph = GetAIEditorGraph(Query);
        UClass* GeneratorClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
        if (!Query || !Graph || !GeneratorClass
            || !GeneratorClass->IsChildOf(UEnvQueryGenerator::StaticClass())
            || GeneratorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("EQS generator class, graph, or ResultId is invalid.");
            return false;
        }
        Graph->Modify();
        UEnvironmentQueryGraphNode_Option* Node = nullptr;
        {
            FGraphNodeCreator<UEnvironmentQueryGraphNode_Option> NodeCreator(*Graph);
            Node = NodeCreator.CreateNode(false);
            ConfigureAIGraphNode(
                Node, GeneratorClass, Operation.PositionX, Operation.PositionY);
            NodeCreator.Finalize();
        }
        UEnvironmentQueryGraphNode_Root* Root = nullptr;
        for (UEdGraphNode* Candidate : Graph->Nodes)
        {
            if ((Root = Cast<UEnvironmentQueryGraphNode_Root>(Candidate)))
            {
                break;
            }
        }
        if (!Root || !ConnectAIGraphNodes(Graph, Root, Node, OutError))
        {
            Node->DestroyNode();
            return false;
        }
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, Node->NodeGuid);
        }
        return true;
    }
    if (Action == TEXT("add_eqs_test"))
    {
        UEnvQuery* Query = Cast<UEnvQuery>(Asset);
        UAIGraph* Graph = GetAIEditorGraph(Query);
        UEnvironmentQueryGraphNode_Option* Parent =
            Cast<UEnvironmentQueryGraphNode_Option>(
                FindAIGraphNode(Graph, Operation.ParentId, ApplyContext.Aliases));
        UClass* TestClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
        if (!Query || !Graph || !Parent || !TestClass
            || !TestClass->IsChildOf(UEnvQueryTest::StaticClass())
            || TestClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
            || (!Operation.ResultId.IsEmpty()
                && ApplyContext.Aliases.Contains(Operation.ResultId)))
        {
            OutError = TEXT("EQS test owner, class, graph, or ResultId is invalid.");
            return false;
        }
        UEnvironmentQueryGraphNode_Test* Node =
            NewObject<UEnvironmentQueryGraphNode_Test>(
                Graph, UEnvironmentQueryGraphNode_Test::StaticClass(),
                NAME_None, RF_Transactional);
        UAIGraphNode::UpdateNodeClassDataFrom(TestClass, Node->ClassData);
        Parent->AddSubNode(Node, Graph);
        Node->Modify();
        Node->bTestEnabled = Operation.bEnabled;
        if (!Operation.ResultId.IsEmpty())
        {
            ApplyContext.Aliases.Add(Operation.ResultId, Node->NodeGuid);
        }
        return true;
    }
    if (Action == TEXT("set_eqs_test_enabled"))
    {
        UEnvironmentQueryGraphNode_Test* Node =
            Cast<UEnvironmentQueryGraphNode_Test>(FindAIGraphNode(
                GetAIEditorGraph(Asset), Operation.ItemId, ApplyContext.Aliases));
        if (!Node)
        {
            OutError = TEXT("EQS test graph node was not found.");
            return false;
        }
        Node->Modify();
        Node->bTestEnabled = Operation.bEnabled;
        return true;
    }
    if (Action == TEXT("set_ai_graph_node_property"))
    {
        UAIGraphNode* Node = FindAIGraphNode(
            GetAIEditorGraph(Asset), Operation.ItemId, ApplyContext.Aliases);
        UObject* PropertyObject = Node ? Node->NodeInstance.Get() : nullptr;
        if (Operation.PropertyScope.ToLower() == TEXT("generator"))
        {
            const UEnvQueryOption* Option = PropertyObject
                ? Cast<UEnvQueryOption>(PropertyObject) : nullptr;
            PropertyObject = Option ? Option->Generator.Get() : nullptr;
        }
        else if (Operation.PropertyScope.ToLower() != TEXT("instance"))
        {
            OutError = TEXT("AI graph PropertyScope must be instance or generator.");
            return false;
        }
        if (!Node || !PropertyObject || Operation.KeyTypePropertyName.IsEmpty())
        {
            OutError = TEXT("AI graph node instance or property was not found.");
            return false;
        }
        Node->Modify();
        return SetExactProperty(
            PropertyObject,
            Operation.KeyTypePropertyName,
            Operation.ExpectedValue,
            Operation.KeyTypePropertyValue,
            OutError);
    }
    if (Action == TEXT("remove_ai_graph_node"))
    {
        UAIGraph* Graph = GetAIEditorGraph(Asset);
        UAIGraphNode* Node = FindAIGraphNode(
            Graph, Operation.ItemId, ApplyContext.Aliases);
        if (!Graph || !Node || GetAIGraphNodeRole(Node) == TEXT("root"))
        {
            OutError = TEXT("AI graph node was not found or is the protected root.");
            return false;
        }
        Graph->Modify();
        Node->Modify();
        Node->DestroyNode();
        return true;
    }
    OutError = FString::Printf(TEXT("Unsupported Data/AI action: %s"), *Operation.Action);
    return false;
}
}

class FThomasEditorDataAIModule final : public IThomasEditorDataAIProviderModule
{
public:
    virtual void ShutdownModule() override { Plans.Reset(); }

    virtual FThomasDomainInventoryResult GetInventory(
        const FString& PackageRoot,
        const int32 MaxResults) override
    {
        IThomasEditorAssetsProviderModule* Assets =
            FModuleManager::LoadModulePtr<IThomasEditorAssetsProviderModule>(
                TEXT("ThomasEditorAssets"));
        if (!Assets)
        {
            return MakeError<FThomasDomainInventoryResult>(
                TEXT("assets_provider_unavailable"), FString());
        }
        return Assets->GetDomainInventory(
            TEXT("DataAI"),
            PackageRoot,
            {
                TEXT("/Script/AIModule.BlackboardData"),
                TEXT("/Script/AIModule.BehaviorTree"),
                TEXT("/Script/AIModule.EnvQuery"),
                TEXT("/Script/StateTreeModule.StateTree")
            },
            MaxResults);
    }

    virtual FThomasDataAssetInspectionResult InspectDataAsset(
        const FString& AssetPath,
        const int32 MaxRows) override
    {
        const FString PackageName = NormalizePath(AssetPath);
        if (!IsAllowedPath(PackageName))
        {
            return MakeError<FThomasDataAssetInspectionResult>(
                TEXT("path_denied"), TEXT("Data asset must remain under /Game/PropHunt."));
        }
        if (MaxRows < 1 || MaxRows > MaxInspectionItems)
        {
            return MakeError<FThomasDataAssetInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxRows must be between 1 and 500."));
        }
        UObject* Asset = LoadProjectAsset(PackageName);
        const FString Kind = AssetKind(Asset);
        if (!Asset || (Kind != TEXT("data_asset") && Kind != TEXT("data_table")))
        {
            return MakeError<FThomasDataAssetInspectionResult>(
                TEXT("data_asset_not_found"), PackageName);
        }
        FThomasDataAssetInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.AssetKind = Kind;
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.Revision = BuildRevision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();
        if (const UDataTable* Table = Cast<UDataTable>(Asset))
        {
            Result.RowStructPath = Table->GetRowStruct()
                ? Table->GetRowStruct()->GetPathName() : FString();
            Result.RowCount = Table->GetRowMap().Num();
            TArray<FName> Names;
            Table->GetRowMap().GetKeys(Names);
            Names.Sort(FNameLexicalLess());
            for (const FName Name : Names)
            {
                if (Result.Rows.Num() >= MaxRows)
                {
                    Result.bTruncated = true;
                    break;
                }
                FThomasDataRowRecord Row;
                Row.Name = Name.ToString();
                Row.Value = ExportTableRow(Table, Table->GetRowMap().FindChecked(Name));
                Result.Rows.Add(MoveTemp(Row));
            }
        }
        return Result;
    }

    virtual FThomasAIAssetInspectionResult InspectAIAsset(
        const FString& AssetPath,
        const int32 MaxItems) override
    {
        const FString PackageName = NormalizePath(AssetPath);
        if (!IsAllowedPath(PackageName))
        {
            return MakeError<FThomasAIAssetInspectionResult>(
                TEXT("path_denied"), TEXT("AI asset must remain under /Game/PropHunt."));
        }
        if (MaxItems < 1 || MaxItems > MaxInspectionItems)
        {
            return MakeError<FThomasAIAssetInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxItems must be between 1 and 500."));
        }
        UObject* Asset = LoadProjectAsset(PackageName);
        const FString Kind = AssetKind(Asset);
        if (!Asset || (Kind != TEXT("blackboard") && Kind != TEXT("behavior_tree")
                && Kind != TEXT("eqs") && Kind != TEXT("state_tree")))
        {
            return MakeError<FThomasAIAssetInspectionResult>(
                TEXT("ai_asset_not_found"), PackageName);
        }
        FThomasAIAssetInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.AssetKind = Kind;
        Result.ClassPath = Asset->GetClass()->GetPathName();
        Result.Revision = BuildRevision(Asset);
        Result.bDirty = Asset->GetOutermost()->IsDirty();

        if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
        {
            Result.ParentBlackboardPath = Blackboard->Parent
                ? Blackboard->Parent->GetOutermost()->GetName() : FString();
#if WITH_EDITORONLY_DATA
            for (const FBlackboardEntry& Entry : Blackboard->ParentKeys)
            {
                if (Result.BlackboardKeys.Num() >= MaxItems) break;
                FThomasBlackboardKeyRecord Item;
                Item.Name = Entry.EntryName.ToString();
                Item.TypeClassPath = Entry.KeyType
                    ? Entry.KeyType->GetClass()->GetPathName() : FString();
                Item.Description = Entry.EntryDescription.Left(1200);
                Item.Category = Entry.EntryCategory.ToString();
                Item.bInstanceSynced = Entry.bInstanceSynced;
                Item.bInherited = true;
                Result.BlackboardKeys.Add(MoveTemp(Item));
            }
#endif
            for (const FBlackboardEntry& Entry : Blackboard->Keys)
            {
                if (Result.BlackboardKeys.Num() >= MaxItems) break;
                FThomasBlackboardKeyRecord Item;
                Item.Name = Entry.EntryName.ToString();
                Item.TypeClassPath = Entry.KeyType
                    ? Entry.KeyType->GetClass()->GetPathName() : FString();
#if WITH_EDITORONLY_DATA
                Item.Description = Entry.EntryDescription.Left(1200);
                Item.Category = Entry.EntryCategory.ToString();
#endif
                Item.bInstanceSynced = Entry.bInstanceSynced;
                Result.BlackboardKeys.Add(MoveTemp(Item));
            }
            if (!Blackboard->IsValid())
            {
                Result.Diagnostics.Add(TEXT("blackboard_key_conflict"));
            }
        }
        else if (const UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
        {
            Result.BlackboardPath = Tree->BlackboardAsset
                ? Tree->BlackboardAsset->GetOutermost()->GetName() : FString();
            Result.RootNodeClassPath = Tree->RootNode
                ? Tree->RootNode->GetClass()->GetPathName() : FString();
#if WITH_EDITORONLY_DATA
            AppendAIGraphInspection(
                Cast<UAIGraph>(Tree->BTGraph), MaxItems, Result);
#endif
            if (!Tree->RootNode)
            {
                Result.Diagnostics.Add(TEXT("behavior_tree_has_no_root"));
            }
        }
        else if (const UEnvQuery* Query = Cast<UEnvQuery>(Asset))
        {
            Result.OptionCount = Query->GetOptions().Num();
#if WITH_EDITORONLY_DATA
            AppendAIGraphInspection(
                GetAIEditorGraph(const_cast<UEnvQuery*>(Query)), MaxItems, Result);
#endif
            if (Result.OptionCount == 0)
            {
                Result.Diagnostics.Add(TEXT("eqs_has_no_options"));
            }
        }
        else if (UStateTree* StateTree = Cast<UStateTree>(Asset))
        {
            UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
            Result.RootNodeClassPath = StateTree->GetSchema()
                ? StateTree->GetSchema()->GetClass()->GetPathName() : FString();
            if (!EditorData)
            {
                Result.Diagnostics.Add(TEXT("state_tree_editor_data_unavailable"));
                return Result;
            }
            int32 ItemCount = 0;
            auto AddNode = [&Result, &ItemCount, MaxItems](
                const FStateTreeEditorNode& Node,
                const FString& OwnerId,
                const FString& Role)
            {
                if (ItemCount >= MaxItems)
                {
                    return;
                }
                FThomasAINodeRecord Record;
                Record.Id = GuidString(Node.ID);
                Record.OwnerId = OwnerId;
                Record.Role = Role;
                Record.Name = Node.GetName().ToString();
                Record.StructPath = Node.Node.GetScriptStruct()
                    ? Node.Node.GetScriptStruct()->GetPathName() : FString();
                const FStateTreeDataView Instance = Node.GetInstance();
                Record.InstanceTypePath = Instance.GetStruct()
                    ? Instance.GetStruct()->GetPathName() : FString();
                Result.Nodes.Add(MoveTemp(Record));
                ++ItemCount;
            };
            for (const FStateTreeEditorNode& Node : EditorData->Evaluators)
            {
                AddNode(Node, FString(), TEXT("evaluator"));
            }
            for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
            {
                AddNode(Node, FString(), TEXT("global_task"));
            }
            EditorData->VisitHierarchy(
                [&Result, &ItemCount, MaxItems, &AddNode](
                    UStateTreeState& State,
                    UStateTreeState* Parent)
                {
                    if (ItemCount >= MaxItems)
                    {
                        return EStateTreeVisitor::Break;
                    }
                    FThomasAIStateRecord StateRecord;
                    StateRecord.Id = GuidString(State.ID);
                    StateRecord.ParentId = Parent ? GuidString(Parent->ID) : FString();
                    StateRecord.Name = State.Name.ToString();
                    StateRecord.Description = State.Description.Left(1200);
                    StateRecord.StateType = StaticEnum<EStateTreeStateType>()
                        ->GetNameStringByValue(static_cast<int64>(State.Type));
                    StateRecord.SelectionBehavior =
                        StaticEnum<EStateTreeStateSelectionBehavior>()
                            ->GetNameStringByValue(
                                static_cast<int64>(State.SelectionBehavior));
                    StateRecord.bEnabled = State.bEnabled;
                    StateRecord.ChildCount = State.Children.Num();
                    StateRecord.EnterConditionCount = State.EnterConditions.Num();
                    StateRecord.TaskCount = State.Tasks.Num()
                        + (State.SingleTask.ID.IsValid() ? 1 : 0);
                    StateRecord.TransitionCount = State.Transitions.Num();
                    Result.States.Add(MoveTemp(StateRecord));
                    ++ItemCount;

                    const FString OwnerId = GuidString(State.ID);
                    for (const FStateTreeEditorNode& Node : State.EnterConditions)
                    {
                        AddNode(Node, OwnerId, TEXT("enter_condition"));
                    }
                    for (const FStateTreeEditorNode& Node : State.Tasks)
                    {
                        AddNode(Node, OwnerId, TEXT("state_task"));
                    }
                    for (const FStateTreeEditorNode& Node : State.Considerations)
                    {
                        AddNode(Node, OwnerId, TEXT("consideration"));
                    }
                    if (State.SingleTask.ID.IsValid())
                    {
                        AddNode(State.SingleTask, OwnerId, TEXT("single_task"));
                    }
                    for (const FStateTreeTransition& Transition : State.Transitions)
                    {
                        if (ItemCount >= MaxItems)
                        {
                            break;
                        }
                        FThomasAITransitionRecord TransitionRecord;
                        TransitionRecord.Id = GuidString(Transition.ID);
                        TransitionRecord.OwnerStateId = OwnerId;
                        TransitionRecord.TargetStateId = GuidString(Transition.State.ID);
                        TransitionRecord.Trigger =
                            StaticEnum<EStateTreeTransitionTrigger>()
                                ->GetNameStringByValue(
                                    static_cast<int64>(Transition.Trigger));
                        TransitionRecord.TransitionType =
                            StaticEnum<EStateTreeTransitionType>()
                                ->GetNameStringByValue(
                                    static_cast<int64>(Transition.State.LinkType));
                        TransitionRecord.bEnabled = Transition.bTransitionEnabled;
                        TransitionRecord.ConditionCount = Transition.Conditions.Num();
                        Result.Transitions.Add(MoveTemp(TransitionRecord));
                        ++ItemCount;
                        for (const FStateTreeEditorNode& Node : Transition.Conditions)
                        {
                            AddNode(Node, GuidString(Transition.ID),
                                TEXT("transition_condition"));
                        }
                    }
                    return ItemCount >= MaxItems
                        ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
                });
            Result.GraphNodeCount = Result.States.Num() + Result.Nodes.Num();
            Result.OptionCount = Result.Transitions.Num();
            if (Result.States.IsEmpty())
            {
                Result.Diagnostics.Add(TEXT("state_tree_has_no_states"));
            }
        }
        return Result;
    }

    virtual FThomasDataAIPlanResult PlanDataAIPatch(
        const FThomasDataAIPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasDataAIPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.AssetKind = Request.AssetKind.TrimStartAndEnd().ToLower();
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasDataAIPlanResult>(
                TEXT("path_denied"), TEXT("Data/AI asset must remain under /Game/PropHunt."));
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > MaxOperations)
        {
            return MakeError<FThomasDataAIPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Data/AI batch must contain 1 to 100 operations."));
        }
        const TSet<FString> SupportedKinds = {
            TEXT("data_asset"), TEXT("data_table"), TEXT("blackboard"),
            TEXT("behavior_tree"), TEXT("eqs"), TEXT("state_tree")};
        if (!SupportedKinds.Contains(Request.AssetKind))
        {
            return MakeError<FThomasDataAIPlanResult>(
                TEXT("invalid_asset_kind"), Request.AssetKind);
        }

        UObject* Existing = LoadProjectAsset(Request.AssetPath);
        const FString Revision = BuildRevision(Existing);
        if (Existing)
        {
            if (Request.bCreateIfMissing || Request.ExpectedRevision != Revision)
            {
                return MakeError<FThomasDataAIPlanResult>(
                    Request.bCreateIfMissing ? TEXT("asset_exists") : TEXT("revision_conflict"),
                    Revision);
            }
            if (Existing->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("asset_dirty"), TEXT("Save or revert the asset before planning."));
            }
            if (AssetKind(Existing) != Request.AssetKind)
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("asset_kind_mismatch"), AssetKind(Existing));
            }
        }
        else
        {
            if (!Request.bCreateIfMissing
                || (!Request.ExpectedRevision.IsEmpty()
                    && Request.ExpectedRevision != TEXT("missing")))
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("asset_not_found"), Request.AssetPath);
            }
            Request.ExpectedRevision = TEXT("missing");
            const FString CreateAction = Request.Operations[0].Action.ToLower();
            const FString ExpectedCreateAction = Request.AssetKind == TEXT("data_asset")
                ? TEXT("create_data_asset")
                : Request.AssetKind == TEXT("data_table")
                    ? TEXT("create_data_table") : TEXT("create_ai_asset");
            if (CreateAction != ExpectedCreateAction)
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("create_operation_required"), ExpectedCreateAction);
            }
            FString FactoryError;
            if (Request.AssetKind == TEXT("data_asset"))
            {
                UClass* Class = LoadObject<UClass>(
                    nullptr, *Request.Operations[0].ClassPath);
                if (!Class || !Class->IsChildOf(UDataAsset::StaticClass())
                    || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_data_asset_class"), Request.Operations[0].ClassPath);
                }
            }
            else if (Request.AssetKind == TEXT("data_table"))
            {
                UScriptStruct* Struct = LoadObject<UScriptStruct>(
                    nullptr, *Request.Operations[0].RowStructPath);
                if (!Struct || !Struct->IsChildOf(FTableRowBase::StaticStruct()))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_row_struct"), Request.Operations[0].RowStructPath);
                }
            }
            else if (!CreateAIFactory(
                Request.AssetKind, Request.Operations[0].ClassPath, FactoryError))
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("ai_factory_unavailable"), FactoryError);
            }
        }

        TSet<FString> RowNames;
        TMap<FString, FString> BlackboardKeyClasses;
        if (const UDataTable* Table = Cast<UDataTable>(Existing))
        {
            for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
                RowNames.Add(Pair.Key.ToString());
        }
        if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Existing))
        {
            for (const FBlackboardEntry& Entry : Blackboard->Keys)
            {
                BlackboardKeyClasses.Add(
                    Entry.EntryName.ToString(),
                    Entry.KeyType ? Entry.KeyType->GetClass()->GetPathName() : FString());
            }
        }

        TSet<FString> StateTreeStateIds;
        TSet<FString> StateTreeNodeIds;
        TSet<FString> StateTreeTransitionIds;
        if (UStateTree* StateTree = Cast<UStateTree>(Existing))
        {
            if (UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree))
            {
                for (const FStateTreeEditorNode& Node : EditorData->Evaluators)
                {
                    StateTreeNodeIds.Add(GuidString(Node.ID));
                }
                for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
                {
                    StateTreeNodeIds.Add(GuidString(Node.ID));
                }
                EditorData->VisitHierarchy(
                    [&StateTreeStateIds, &StateTreeNodeIds, &StateTreeTransitionIds](
                        UStateTreeState& State,
                        UStateTreeState*)
                    {
                        StateTreeStateIds.Add(GuidString(State.ID));
                        auto AddNodes = [&StateTreeNodeIds](
                            const TArray<FStateTreeEditorNode>& Nodes)
                        {
                            for (const FStateTreeEditorNode& Node : Nodes)
                            {
                                StateTreeNodeIds.Add(GuidString(Node.ID));
                            }
                        };
                        AddNodes(State.EnterConditions);
                        AddNodes(State.Tasks);
                        AddNodes(State.Considerations);
                        if (State.SingleTask.ID.IsValid())
                        {
                            StateTreeNodeIds.Add(GuidString(State.SingleTask.ID));
                        }
                        for (const FStateTreeTransition& Transition : State.Transitions)
                        {
                            StateTreeTransitionIds.Add(GuidString(Transition.ID));
                            AddNodes(Transition.Conditions);
                        }
                        return EStateTreeVisitor::Continue;
                });
            }
        }
        TSet<FString> AIGraphNodeIds;
        TMap<FString, FString> AIGraphNodeRoles;
        if (UAIGraph* Graph = GetAIEditorGraph(Existing))
        {
            for (UEdGraphNode* GraphNode : Graph->Nodes)
            {
                UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode);
                if (!AINode)
                {
                    continue;
                }
                const FString Id = GuidString(AINode->NodeGuid);
                AIGraphNodeIds.Add(Id);
                AIGraphNodeRoles.Add(Id, GetAIGraphNodeRole(AINode));
                for (UAIGraphNode* SubNode : AINode->SubNodes)
                {
                    if (!SubNode)
                    {
                        continue;
                    }
                    const FString SubId = GuidString(SubNode->NodeGuid);
                    AIGraphNodeIds.Add(SubId);
                    AIGraphNodeRoles.Add(SubId, GetAIGraphNodeRole(SubNode));
                }
            }
        }
        TSet<FString> AIGraphAliases;
        TMap<FString, FString> AIGraphAliasRoles;
        TSet<FString> StateTreeStateAliases;
        TSet<FString> StateTreeNodeAliases;
        TSet<FString> StateTreeTransitionAliases;
        auto HasAnyStateTreeAlias = [&StateTreeStateAliases, &StateTreeNodeAliases,
                                     &StateTreeTransitionAliases](const FString& Alias)
        {
            return StateTreeStateAliases.Contains(Alias)
                || StateTreeNodeAliases.Contains(Alias)
                || StateTreeTransitionAliases.Contains(Alias);
        };
        auto HasPlannedId = [](
            const FString& Input,
            const TSet<FString>& ExistingIds,
            const TSet<FString>& Aliases)
        {
            const FString Key = Input.TrimStartAndEnd();
            if (Aliases.Contains(Key) || ExistingIds.Contains(Key))
            {
                return true;
            }
            FGuid Guid;
            return FGuid::Parse(Key, Guid)
                && ExistingIds.Contains(GuidString(Guid));
        };
        auto GetPlannedGraphRole = [&AIGraphNodeRoles, &AIGraphAliasRoles](
            const FString& Input)
        {
            const FString Key = Input.TrimStartAndEnd();
            if (const FString* AliasRole = AIGraphAliasRoles.Find(Key))
            {
                return *AliasRole;
            }
            if (const FString* Role = AIGraphNodeRoles.Find(Key))
            {
                return *Role;
            }
            FGuid Guid;
            return FGuid::Parse(Key, Guid)
                ? AIGraphNodeRoles.FindRef(GuidString(Guid)) : FString();
        };

        FThomasDataAIPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Request.ExpectedRevision;
        Result.AssetKind = Request.AssetKind;
        for (const FThomasDataAIOperation& Operation : Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            if (IsCreateAction(Action))
            {
                if (Existing || &Operation != &Request.Operations[0])
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_create_operation"), Action);
                }
            }
            else if (Action == TEXT("set_table_row"))
            {
                if (Request.AssetKind != TEXT("data_table")
                    || Operation.Name.IsEmpty() || Operation.Value.IsEmpty())
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_table_row"), Operation.Name);
                }
                if (const UDataTable* Table = Cast<UDataTable>(Existing))
                {
                    if (const uint8* const* Current =
                            Table->GetRowMap().Find(FName(*Operation.Name)))
                    {
                        const FString CurrentValue = ExportTableRow(Table, *Current);
                        if (!Operation.ExpectedValue.IsEmpty()
                            && CurrentValue != Operation.ExpectedValue)
                        {
                            return MakeError<FThomasDataAIPlanResult>(
                                TEXT("value_conflict"), CurrentValue);
                        }
                    }
                }
                RowNames.Add(Operation.Name);
            }
            else if (Action == TEXT("remove_table_row"))
            {
                if (Request.AssetKind != TEXT("data_table")
                    || !Request.bConfirmDestructive || !RowNames.Remove(Operation.Name))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("row_not_found") : TEXT("confirmation_required"),
                        Operation.Name);
                }
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("set_behavior_tree_blackboard"))
            {
                if (Request.AssetKind != TEXT("behavior_tree")
                    || (!Operation.ObjectPath.IsEmpty()
                        && !Cast<UBlackboardData>(LoadProjectAsset(Operation.ObjectPath))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_blackboard_reference"), Operation.ObjectPath);
                }
            }
            else if (Action == TEXT("set_blackboard_parent"))
            {
                if (Request.AssetKind != TEXT("blackboard")
                    || (!Operation.ObjectPath.IsEmpty()
                        && !Cast<UBlackboardData>(LoadProjectAsset(Operation.ObjectPath))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_blackboard_parent"), Operation.ObjectPath);
                }
            }
            else if (Action == TEXT("add_blackboard_key"))
            {
                UClass* KeyClass = LoadObject<UClass>(nullptr, *Operation.KeyTypeClassPath);
                if (Request.AssetKind != TEXT("blackboard") || Operation.Name.IsEmpty()
                    || BlackboardKeyClasses.Contains(Operation.Name)
                    || !KeyClass || !KeyClass->IsChildOf(UBlackboardKeyType::StaticClass())
                    || KeyClass->HasAnyClassFlags(CLASS_Abstract))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_blackboard_key"), Operation.Name);
                }
                BlackboardKeyClasses.Add(Operation.Name, Operation.KeyTypeClassPath);
            }
            else if (Action == TEXT("set_blackboard_key_property"))
            {
                const FString* KeyClassPath = BlackboardKeyClasses.Find(Operation.Name);
                UClass* KeyClass = KeyClassPath
                    ? LoadObject<UClass>(nullptr, **KeyClassPath) : nullptr;
                FProperty* Property = KeyClass
                    ? FindFProperty<FProperty>(
                        KeyClass, FName(*Operation.KeyTypePropertyName))
                    : nullptr;
                if (Request.AssetKind != TEXT("blackboard") || !Property
                    || !Property->HasAnyPropertyFlags(CPF_Edit)
                    || Property->HasAnyPropertyFlags(CPF_Transient))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("blackboard_key_property_denied"),
                        Operation.KeyTypePropertyName);
                }
            }
            else if (Action == TEXT("remove_blackboard_key"))
            {
                if (Request.AssetKind != TEXT("blackboard")
                    || !Request.bConfirmDestructive
                    || BlackboardKeyClasses.Remove(Operation.Name) == 0)
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("blackboard_key_not_found")
                            : TEXT("confirmation_required"),
                        Operation.Name);
                }
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("add_state"))
            {
                EStateTreeStateType StateType = EStateTreeStateType::State;
                if (Request.AssetKind != TEXT("state_tree")
                    || Operation.Name.IsEmpty()
                    || !ParseEnum(Operation.StateType, StateType)
                    || (!Operation.ParentId.IsEmpty()
                        && !HasPlannedId(Operation.ParentId, StateTreeStateIds, StateTreeStateAliases))
                    || (!Operation.ResultId.IsEmpty()
                        && (HasAnyStateTreeAlias(Operation.ResultId)
                            || StateTreeStateIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_state_tree_state"), Operation.Name);
                }
                if (!Operation.SelectionBehavior.IsEmpty())
                {
                    EStateTreeStateSelectionBehavior SelectionBehavior =
                        EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
                    if (!ParseEnum(Operation.SelectionBehavior, SelectionBehavior))
                    {
                        return MakeError<FThomasDataAIPlanResult>(
                            TEXT("invalid_state_selection_behavior"),
                            Operation.SelectionBehavior);
                    }
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    StateTreeStateAliases.Add(Operation.ResultId);
                }
            }
            else if (Action == TEXT("remove_state"))
            {
                if (Request.AssetKind != TEXT("state_tree")
                    || !Request.bConfirmDestructive
                    || !HasPlannedId(Operation.StateId, StateTreeStateIds, StateTreeStateAliases))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("state_tree_state_not_found")
                            : TEXT("confirmation_required"),
                        Operation.StateId);
                }
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("rename_state")
                || Action == TEXT("set_state_description")
                || Action == TEXT("set_state_enabled")
                || Action == TEXT("set_state_selection_behavior"))
            {
                if (Request.AssetKind != TEXT("state_tree")
                    || !HasPlannedId(Operation.StateId, StateTreeStateIds, StateTreeStateAliases)
                    || (Action == TEXT("rename_state") && Operation.Name.IsEmpty()))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("state_tree_state_not_found"), Operation.StateId);
                }
                if (Action == TEXT("set_state_selection_behavior"))
                {
                    EStateTreeStateSelectionBehavior SelectionBehavior =
                        EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
                    if (!ParseEnum(Operation.SelectionBehavior, SelectionBehavior))
                    {
                        return MakeError<FThomasDataAIPlanResult>(
                            TEXT("invalid_state_selection_behavior"),
                            Operation.SelectionBehavior);
                    }
                }
            }
            else if (Action == TEXT("add_state_task")
                || Action == TEXT("add_enter_condition")
                || Action == TEXT("add_global_task")
                || Action == TEXT("add_evaluator")
                || Action == TEXT("add_transition_condition"))
            {
                const FString Role = Action == TEXT("add_state_task") ? TEXT("state_task")
                    : Action == TEXT("add_enter_condition") ? TEXT("enter_condition")
                    : Action == TEXT("add_global_task") ? TEXT("global_task")
                    : Action == TEXT("add_evaluator") ? TEXT("evaluator")
                    : TEXT("transition_condition");
                UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Operation.ClassPath);
                const bool bOwnerValid = Role == TEXT("global_task")
                    || Role == TEXT("evaluator")
                    || (Role == TEXT("transition_condition")
                        ? HasPlannedId(Operation.ItemId, StateTreeTransitionIds, StateTreeTransitionAliases)
                        : HasPlannedId(Operation.StateId, StateTreeStateIds, StateTreeStateAliases));
                if (Request.AssetKind != TEXT("state_tree") || !bOwnerValid
                    || !IsStateTreeStructAllowedForRole(Struct, Role)
                    || (!Operation.ResultId.IsEmpty()
                        && (HasAnyStateTreeAlias(Operation.ResultId)
                            || StateTreeNodeIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_state_tree_node"), Operation.ClassPath);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    StateTreeNodeAliases.Add(Operation.ResultId);
                }
            }
            else if (Action == TEXT("remove_state_tree_node"))
            {
                if (Request.AssetKind != TEXT("state_tree")
                    || !Request.bConfirmDestructive
                    || !HasPlannedId(Operation.ItemId, StateTreeNodeIds, StateTreeNodeAliases))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("state_tree_node_not_found")
                            : TEXT("confirmation_required"),
                        Operation.ItemId);
                }
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("set_state_tree_node_property"))
            {
                if (Request.AssetKind != TEXT("state_tree")
                    || !HasPlannedId(Operation.ItemId, StateTreeNodeIds, StateTreeNodeAliases)
                    || Operation.KeyTypePropertyName.IsEmpty()
                    || (Operation.PropertyScope.ToLower() != TEXT("node")
                        && Operation.PropertyScope.ToLower() != TEXT("instance")
                        && Operation.PropertyScope.ToLower() != TEXT("execution_runtime")))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_state_tree_node_property"),
                        Operation.KeyTypePropertyName);
                }
            }
            else if (Action == TEXT("add_state_transition"))
            {
                EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::None;
                EStateTreeTransitionType Type = EStateTreeTransitionType::None;
                const bool bEnumsValid = ParseEnum(Operation.TransitionTrigger, Trigger)
                    && ParseEnum(Operation.TransitionType, Type);
                const bool bTargetValid = bEnumsValid
                    && (Type != EStateTreeTransitionType::GotoState
                        || HasPlannedId(Operation.TargetStateId, StateTreeStateIds, StateTreeStateAliases));
                if (Request.AssetKind != TEXT("state_tree")
                    || !HasPlannedId(Operation.StateId, StateTreeStateIds, StateTreeStateAliases)
                    || !bTargetValid
                    || (!Operation.ResultId.IsEmpty()
                        && (HasAnyStateTreeAlias(Operation.ResultId)
                            || StateTreeTransitionIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_state_tree_transition"),
                        Operation.TargetStateId);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    StateTreeTransitionAliases.Add(Operation.ResultId);
                }
            }
            else if (Action == TEXT("remove_state_transition"))
            {
                if (Request.AssetKind != TEXT("state_tree")
                    || !Request.bConfirmDestructive
                    || !HasPlannedId(Operation.ItemId, StateTreeTransitionIds, StateTreeTransitionAliases))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("state_tree_transition_not_found")
                            : TEXT("confirmation_required"),
                        Operation.ItemId);
                }
                Result.Risk = TEXT("R2");
            }
            else if (Action == TEXT("add_behavior_tree_node"))
            {
                const FString Role = Operation.NodeRole.ToLower();
                UClass* Class = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (Request.AssetKind != TEXT("behavior_tree")
                    || (Role != TEXT("composite") && Role != TEXT("task"))
                    || !IsBehaviorTreeClassForRole(Class, Role)
                    || (!Operation.ResultId.IsEmpty()
                        && (AIGraphAliases.Contains(Operation.ResultId)
                            || AIGraphNodeIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_behavior_tree_node"), Operation.ClassPath);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    AIGraphAliases.Add(Operation.ResultId);
                    AIGraphAliasRoles.Add(Operation.ResultId, Role);
                }
            }
            else if (Action == TEXT("add_behavior_tree_aux_node"))
            {
                const FString Role = Operation.NodeRole.ToLower();
                UClass* Class = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                const FString ParentRole = GetPlannedGraphRole(Operation.ParentId);
                if (Request.AssetKind != TEXT("behavior_tree")
                    || (Role != TEXT("decorator") && Role != TEXT("service"))
                    || (ParentRole != TEXT("composite") && ParentRole != TEXT("task"))
                    || !IsBehaviorTreeClassForRole(Class, Role)
                    || (!Operation.ResultId.IsEmpty()
                        && (AIGraphAliases.Contains(Operation.ResultId)
                            || AIGraphNodeIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_behavior_tree_aux_node"), Operation.ClassPath);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    AIGraphAliases.Add(Operation.ResultId);
                    AIGraphAliasRoles.Add(Operation.ResultId, Role);
                }
            }
            else if (Action == TEXT("connect_ai_graph_nodes")
                || Action == TEXT("disconnect_ai_graph_nodes"))
            {
                const bool bDisconnect = Action == TEXT("disconnect_ai_graph_nodes");
                if ((Request.AssetKind != TEXT("behavior_tree")
                        && Request.AssetKind != TEXT("eqs"))
                    || !HasPlannedId(Operation.SourceId, AIGraphNodeIds, AIGraphAliases)
                    || !HasPlannedId(Operation.TargetId, AIGraphNodeIds, AIGraphAliases)
                    || (bDisconnect && !Request.bConfirmDestructive))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        bDisconnect && !Request.bConfirmDestructive
                            ? TEXT("confirmation_required")
                            : TEXT("invalid_ai_graph_connection"),
                        Operation.SourceId + TEXT("->") + Operation.TargetId);
                }
                if (bDisconnect)
                {
                    Result.Risk = TEXT("R2");
                }
            }
            else if (Action == TEXT("add_eqs_option"))
            {
                UClass* Class = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (Request.AssetKind != TEXT("eqs") || !Class
                    || !Class->IsChildOf(UEnvQueryGenerator::StaticClass())
                    || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
                    || (!Operation.ResultId.IsEmpty()
                        && (AIGraphAliases.Contains(Operation.ResultId)
                            || AIGraphNodeIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_eqs_generator"), Operation.ClassPath);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    AIGraphAliases.Add(Operation.ResultId);
                    AIGraphAliasRoles.Add(Operation.ResultId, TEXT("generator"));
                }
            }
            else if (Action == TEXT("add_eqs_test"))
            {
                UClass* Class = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (Request.AssetKind != TEXT("eqs")
                    || GetPlannedGraphRole(Operation.ParentId) != TEXT("generator")
                    || !Class || !Class->IsChildOf(UEnvQueryTest::StaticClass())
                    || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
                    || (!Operation.ResultId.IsEmpty()
                        && (AIGraphAliases.Contains(Operation.ResultId)
                            || AIGraphNodeIds.Contains(Operation.ResultId))))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_eqs_test"), Operation.ClassPath);
                }
                if (!Operation.ResultId.IsEmpty())
                {
                    AIGraphAliases.Add(Operation.ResultId);
                    AIGraphAliasRoles.Add(Operation.ResultId, TEXT("test"));
                }
            }
            else if (Action == TEXT("set_eqs_test_enabled"))
            {
                if (Request.AssetKind != TEXT("eqs")
                    || GetPlannedGraphRole(Operation.ItemId) != TEXT("test"))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("eqs_test_not_found"), Operation.ItemId);
                }
            }
            else if (Action == TEXT("set_ai_graph_node_property"))
            {
                const FString Scope = Operation.PropertyScope.ToLower();
                const FString Role = GetPlannedGraphRole(Operation.ItemId);
                if ((Request.AssetKind != TEXT("behavior_tree")
                        && Request.AssetKind != TEXT("eqs"))
                    || Role.IsEmpty() || Role == TEXT("root")
                    || Operation.KeyTypePropertyName.IsEmpty()
                    || (Scope != TEXT("instance") && Scope != TEXT("generator"))
                    || (Scope == TEXT("generator") && Role != TEXT("generator")))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        TEXT("invalid_ai_graph_node_property"),
                        Operation.KeyTypePropertyName);
                }
            }
            else if (Action == TEXT("remove_ai_graph_node"))
            {
                const FString Role = GetPlannedGraphRole(Operation.ItemId);
                if ((Request.AssetKind != TEXT("behavior_tree")
                        && Request.AssetKind != TEXT("eqs"))
                    || !Request.bConfirmDestructive
                    || Role.IsEmpty() || Role == TEXT("root"))
                {
                    return MakeError<FThomasDataAIPlanResult>(
                        !Request.bConfirmDestructive
                            ? TEXT("confirmation_required")
                            : TEXT("ai_graph_node_not_found"),
                        Operation.ItemId);
                }
                Result.Risk = TEXT("R2");
            }
            else
            {
                return MakeError<FThomasDataAIPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
            Result.Preview.Add(Operation.Action + TEXT(":") + Operation.Name);
        }
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasDataAIApplyResult ApplyDataAIPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasDataAIApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UObject* Asset = LoadProjectAsset(Plan.Request.AssetPath);
        const FString CurrentRevision = BuildRevision(Asset);
        if (CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasDataAIApplyResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasDataAIApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = CurrentRevision;
        if (!Asset)
        {
            FString CreateError;
            Asset = CreateAsset(Plan.Request, Plan.Request.Operations[0], CreateError);
            if (!Asset)
            {
                return MakeError<FThomasDataAIApplyResult>(
                    TEXT("create_failed"), CreateError);
            }
            Result.bCreated = true;
        }

        bool bApplied = true;
        FString ApplyError;
        FDataAIApplyContext ApplyContext;
        {
            FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyDataAIPatch", "ThomasEditor Data and AI Patch"));
            Asset->Modify();
            if ((Asset->IsA<UBehaviorTree>() || Asset->IsA<UEnvQuery>())
                && !EnsureAIEditorGraph(Asset, ApplyError))
            {
                bApplied = false;
            }
            for (const FThomasDataAIOperation& Operation : Plan.Request.Operations)
            {
                if (!bApplied)
                {
                    break;
                }
                if (!ApplyOperation(Asset, Operation, ApplyContext, ApplyError))
                {
                    bApplied = false;
                    break;
                }
                ++Result.AppliedOperationCount;
            }
            if (bApplied)
            {
                if (UAIGraph* Graph = GetAIEditorGraph(Asset))
                {
                    Graph->UpdateAsset();
                    Graph->UpdateErrorMessages();
                    for (UEdGraphNode* GraphNode : Graph->Nodes)
                    {
                        if (UAIGraphNode* AINode = Cast<UAIGraphNode>(GraphNode))
                        {
                            if (AINode->HasErrors())
                            {
                                bApplied = false;
                                ApplyError = AINode->ErrorMessage.IsEmpty()
                                    ? FString::Printf(
                                        TEXT("AI graph node is invalid: %s"),
                                        *GuidString(AINode->NodeGuid))
                                    : AINode->ErrorMessage;
                                break;
                            }
                        }
                    }
                    if (bApplied)
                    {
                        if (UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
                        {
                            const bool bHasAuthoredMainNode = Graph->Nodes.ContainsByPredicate(
                                [](const UEdGraphNode* Node)
                                {
                                    const UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
                                    const FString Role = AINode
                                        ? GetAIGraphNodeRole(AINode) : FString();
                                    return Role == TEXT("composite")
                                        || Role == TEXT("task");
                                });
                            if (bHasAuthoredMainNode && !Tree->RootNode)
                            {
                                bApplied = false;
                                ApplyError = TEXT("Behavior Tree graph did not compile a runtime root.");
                            }
                        }
                        else if (UEnvQuery* Query = Cast<UEnvQuery>(Asset))
                        {
                            int32 GraphOptionCount = 0;
                            for (const UEdGraphNode* Node : Graph->Nodes)
                            {
                                GraphOptionCount +=
                                    Node->IsA<UEnvironmentQueryGraphNode_Option>() ? 1 : 0;
                            }
                            if (Query->GetOptions().Num() != GraphOptionCount)
                            {
                                bApplied = false;
                                ApplyError = TEXT("EQS graph/runtime option count mismatch.");
                            }
                        }
                    }
                    Graph->NotifyGraphChanged();
                }
            }
            if (bApplied)
            {
                if (UStateTree* StateTree = Cast<UStateTree>(Asset))
                {
                    UStateTreeEditorData* EditorData = GetStateTreeEditorData(StateTree);
                    if (!EditorData)
                    {
                        bApplied = false;
                        ApplyError = TEXT("StateTree EditorData is unavailable.");
                    }
                    else
                    {
                        EditorData->ReparentStates();
                        EditorData->FixDuplicateIDs();
                        EditorData->UpdateBindings();
                        EditorData->RemoveInvalidBindings();
                        StateTree->PostEditChange();
                        FStateTreeCompilerLog CompilerLog;
                        FStateTreeCompiler Compiler(CompilerLog);
                        const bool bCompiled = Compiler.Compile(*StateTree);
                        for (const TSharedRef<FTokenizedMessage>& Message
                            : CompilerLog.ToTokenizedMessages())
                        {
                            Result.Diagnostics.Add(Message->ToText().ToString().Left(1200));
                        }
                        if (!bCompiled)
                        {
                            bApplied = false;
                            ApplyError = Result.Diagnostics.IsEmpty()
                                ? TEXT("StateTree native compilation failed.")
                                : Result.Diagnostics[0];
                        }
                    }
                }
            }
            if (!bApplied)
            {
                Transaction.Cancel();
            }
        }

        if (!bApplied)
        {
            if (Result.bCreated)
            {
                Result.bRolledBack = ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            }
            else
            {
                FText ReloadError;
                Result.bRolledBack = UPackageTools::ReloadPackages(
                    {Asset->GetOutermost()},
                    ReloadError,
                    EReloadPackagesInteractionMode::AssumePositive);
                if (!ReloadError.IsEmpty())
                    Result.Diagnostics.Add(ReloadError.ToString().Left(1200));
            }
            Result.Code = TEXT("apply_failed");
            Result.Message = ApplyError.Left(1200);
            Result.RevisionAfter = BuildRevision(LoadProjectAsset(Plan.Request.AssetPath));
            return Result;
        }

        if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
        {
            if (!Blackboard->IsValid())
            {
                Result.bRolledBack = Result.bCreated
                    ? ObjectTools::DeleteObjectsUnchecked({Asset}) == 1
                    : (GEditor && GEditor->UndoTransaction());
                Result.Code = TEXT("validation_failed");
                Result.Message = TEXT("Blackboard key validation failed; rollback was attempted.");
                Result.RevisionAfter = BuildRevision(
                    LoadProjectAsset(Plan.Request.AssetPath));
                return Result;
            }
        }

        if (bSave)
        {
            Result.bSaved = SaveAsset(Asset);
            if (!Result.bSaved)
            {
                Result.bRolledBack = Result.bCreated
                    ? ObjectTools::DeleteObjectsUnchecked({Asset}) == 1
                    : (GEditor && GEditor->UndoTransaction());
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("Save failed; rollback was attempted.");
                Result.RevisionAfter = BuildRevision(
                    LoadProjectAsset(Plan.Request.AssetPath));
                return Result;
            }
        }

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
    }

    TMap<FString, FCachedPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorDataAIModule, ThomasEditorDataAI)
