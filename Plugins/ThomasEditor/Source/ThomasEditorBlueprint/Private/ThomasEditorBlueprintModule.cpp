#include "ThomasEditorBlueprintProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/SceneComponent.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EditorUtilityObject.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityWidgetBlueprintFactory.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
constexpr int32 MaxBatchOperations = 200;
constexpr int32 MaxInspectionNodes = 1000;
constexpr int32 MaxInspectionPinsPerNode = 64;
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedPlan
{
    FThomasBlueprintBatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

bool IsAllowedPackagePath(const FString& Path)
{
    return (Path == TEXT("/Game/PropHunt") || Path.StartsWith(TEXT("/Game/PropHunt/")))
        && !Path.Contains(TEXT(".."))
        && !Path.StartsWith(TEXT("/Game/Developers/"));
}

FString NormalizeAssetPath(const FString& Input)
{
    FString Path = Input.TrimStartAndEnd();
    if (Path.Contains(TEXT(".")))
    {
        Path = FPackageName::ObjectPathToPackageName(Path);
    }
    return Path;
}

FString ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

UBlueprint* LoadBlueprint(const FString& AssetPath)
{
    const FString PackageName = NormalizeAssetPath(AssetPath);
    if (!IsAllowedPackagePath(PackageName))
    {
        return nullptr;
    }

    const FSoftObjectPath ObjectPath(ObjectPathForPackage(PackageName));
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(ObjectPath);
    return Asset.IsValid() ? Cast<UBlueprint>(Asset.GetAsset()) : nullptr;
}

FString BuildRevision(const UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return TEXT("missing");
    }
    const UPackage* Package = Blueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        Blueprint->NewVariables.Num(),
        Blueprint->UbergraphPages.Num() + Blueprint->FunctionGraphs.Num());
}

FString CompileStatusText(const UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return TEXT("missing");
    }
    switch (Blueprint->Status)
    {
    case BS_UpToDate: return TEXT("up_to_date");
    case BS_Dirty: return TEXT("dirty");
    case BS_Error: return TEXT("error");
    case BS_Unknown: return TEXT("unknown");
    case BS_BeingCreated: return TEXT("being_created");
    case BS_UpToDateWithWarnings: return TEXT("up_to_date_with_warnings");
    default: return TEXT("other");
    }
}

FString AtomicPinTypeText(
    const FName Category,
    const FName SubCategory,
    const UObject* SubCategoryObject)
{
    FString Result = Category.ToString();
    if (!SubCategory.IsNone())
    {
        Result += TEXT(":") + SubCategory.ToString();
    }
    if (SubCategoryObject)
    {
        Result += TEXT(":") + SubCategoryObject->GetPathName();
    }
    return Result;
}

FString PinTypeText(const FEdGraphPinType& Type)
{
    const FString Primary = AtomicPinTypeText(
        Type.PinCategory,
        Type.PinSubCategory,
        Type.PinSubCategoryObject.Get());
    if (Type.ContainerType == EPinContainerType::Array)
    {
        return FString::Printf(TEXT("array<%s>"), *Primary);
    }
    if (Type.ContainerType == EPinContainerType::Set)
    {
        return FString::Printf(TEXT("set<%s>"), *Primary);
    }
    if (Type.ContainerType == EPinContainerType::Map)
    {
        const FString Value = AtomicPinTypeText(
            Type.PinValueType.TerminalCategory,
            Type.PinValueType.TerminalSubCategory,
            Type.PinValueType.TerminalSubCategoryObject.Get());
        return FString::Printf(TEXT("map<%s,%s>"), *Primary, *Value);
    }
    return Primary;
}

bool ParseAtomicPinType(const FString& Input, FEdGraphPinType& OutType)
{
    const FString Type = Input.TrimStartAndEnd().ToLower();
    if (Type == TEXT("bool")) OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    else if (Type == TEXT("byte")) OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
    else if (Type == TEXT("int")) OutType.PinCategory = UEdGraphSchema_K2::PC_Int;
    else if (Type == TEXT("int64")) OutType.PinCategory = UEdGraphSchema_K2::PC_Int64;
    else if (Type == TEXT("float"))
    {
        OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
        OutType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (Type == TEXT("double"))
    {
        OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
        OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (Type == TEXT("string")) OutType.PinCategory = UEdGraphSchema_K2::PC_String;
    else if (Type == TEXT("name")) OutType.PinCategory = UEdGraphSchema_K2::PC_Name;
    else if (Type == TEXT("text")) OutType.PinCategory = UEdGraphSchema_K2::PC_Text;
    else if (Type.StartsWith(TEXT("object:"))
        || Type.StartsWith(TEXT("class:"))
        || Type.StartsWith(TEXT("interface:"))
        || Type.StartsWith(TEXT("softobject:"))
        || Type.StartsWith(TEXT("softclass:")))
    {
        const bool bClass = Type.StartsWith(TEXT("class:"));
        const bool bInterface = Type.StartsWith(TEXT("interface:"));
        const bool bSoftObject = Type.StartsWith(TEXT("softobject:"));
        const bool bSoftClass = Type.StartsWith(TEXT("softclass:"));
        const FString Original = Input.Mid(Input.Find(TEXT(":")) + 1).TrimStartAndEnd();
        UClass* Class = LoadObject<UClass>(nullptr, *Original);
        if (!Class || (bInterface && !Class->HasAnyClassFlags(CLASS_Interface)))
        {
            return false;
        }
        OutType.PinCategory = bClass
            ? UEdGraphSchema_K2::PC_Class
            : bInterface
                ? UEdGraphSchema_K2::PC_Interface
                : bSoftObject
                    ? UEdGraphSchema_K2::PC_SoftObject
                    : bSoftClass
                        ? UEdGraphSchema_K2::PC_SoftClass
                        : UEdGraphSchema_K2::PC_Object;
        OutType.PinSubCategoryObject = Class;
    }
    else if (Type.StartsWith(TEXT("struct:")))
    {
        const FString Original = Input.Mid(Input.Find(TEXT(":")) + 1).TrimStartAndEnd();
        UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Original);
        if (!Struct)
        {
            return false;
        }
        OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutType.PinSubCategoryObject = Struct;
    }
    else if (Type.StartsWith(TEXT("enum:")))
    {
        const FString Original = Input.Mid(Input.Find(TEXT(":")) + 1).TrimStartAndEnd();
        UEnum* Enum = LoadObject<UEnum>(nullptr, *Original);
        if (!Enum)
        {
            return false;
        }
        OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
        OutType.PinSubCategoryObject = Enum;
    }
    else
    {
        return false;
    }
    return true;
}

bool ParsePinType(
    const FString& Input,
    const FString& ContainerInput,
    const FString& KeyInput,
    FEdGraphPinType& OutType)
{
    const FString Container = ContainerInput.TrimStartAndEnd().ToLower();
    if (Container.IsEmpty() || Container == TEXT("none"))
    {
        return ParseAtomicPinType(Input, OutType);
    }
    if (Container == TEXT("array") || Container == TEXT("set"))
    {
        if (!ParseAtomicPinType(Input, OutType))
        {
            return false;
        }
        OutType.ContainerType = Container == TEXT("array")
            ? EPinContainerType::Array : EPinContainerType::Set;
        return true;
    }
    if (Container == TEXT("map"))
    {
        FEdGraphPinType KeyType;
        FEdGraphPinType ValueType;
        if (!ParseAtomicPinType(KeyInput, KeyType)
            || !ParseAtomicPinType(Input, ValueType))
        {
            return false;
        }
        OutType = KeyType;
        OutType.ContainerType = EPinContainerType::Map;
        OutType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
        return true;
    }
    return false;
}

FString NormalizeBlueprintKind(const FString& Input, const bool bLegacyWidget)
{
    FString Kind = Input.TrimStartAndEnd().ToLower();
    if (Kind.IsEmpty())
    {
        Kind = bLegacyWidget ? TEXT("widget") : TEXT("normal");
    }
    return Kind;
}

bool IsWidgetKind(const FString& Kind)
{
    return Kind == TEXT("widget") || Kind == TEXT("editor_utility_widget");
}

bool IsSupportedBlueprintKind(const FString& Kind)
{
    return Kind == TEXT("normal")
        || Kind == TEXT("widget")
        || Kind == TEXT("interface")
        || Kind == TEXT("editor_utility")
        || Kind == TEXT("editor_utility_widget");
}

FString BlueprintKind(const UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return FString();
    }
    if (Blueprint->BlueprintType == BPTYPE_Interface)
    {
        return TEXT("interface");
    }
    if (Blueprint->IsA<UEditorUtilityWidgetBlueprint>())
    {
        return TEXT("editor_utility_widget");
    }
    if (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UEditorUtilityObject::StaticClass()))
    {
        return TEXT("editor_utility");
    }
    return Blueprint->IsA<UWidgetBlueprint>() ? TEXT("widget") : TEXT("normal");
}

bool ParseColor(const FString& Input, FLinearColor& OutColor)
{
    TArray<FString> Parts;
    Input.ParseIntoArray(Parts, TEXT(","), true);
    if (Parts.Num() != 4)
    {
        return false;
    }
    OutColor = FLinearColor(
        FCString::Atof(*Parts[0]),
        FCString::Atof(*Parts[1]),
        FCString::Atof(*Parts[2]),
        FCString::Atof(*Parts[3]));
    return true;
}

bool SaveBlueprint(UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return false;
    }
    UPackage* Package = Blueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    Args.Error = GError;
    return UPackage::SavePackage(Package, Blueprint, *Filename, Args);
}

UBlueprint* CreateBlueprintAsset(
    const FString& PackageName,
    UClass* ParentClass,
    const FString& AssetKind)
{
    UPackage* Package = CreatePackage(*PackageName);
    const FName AssetName(*FPackageName::GetLongPackageAssetName(PackageName));
    UBlueprint* Blueprint = nullptr;
    if (AssetKind == TEXT("interface"))
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            UInterface::StaticClass(),
            Package,
            AssetName,
            BPTYPE_Interface,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());
    }
    else if (AssetKind == TEXT("editor_utility_widget"))
    {
        if (!ParentClass || !ParentClass->IsChildOf(UEditorUtilityWidget::StaticClass()))
        {
            return nullptr;
        }
        UEditorUtilityWidgetBlueprintFactory* Factory =
            NewObject<UEditorUtilityWidgetBlueprintFactory>();
        Factory->ParentClass = ParentClass;
        Blueprint = Cast<UEditorUtilityWidgetBlueprint>(Factory->FactoryCreateNew(
            UEditorUtilityWidgetBlueprint::StaticClass(),
            Package,
            AssetName,
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn));
    }
    else if (AssetKind == TEXT("widget"))
    {
        if (!ParentClass || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
        {
            return nullptr;
        }
        UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
        Factory->ParentClass = ParentClass;
        Blueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
            UWidgetBlueprint::StaticClass(),
            Package,
            AssetName,
            RF_Public | RF_Standalone | RF_Transactional,
            nullptr,
            GWarn));
    }
    else if (AssetKind == TEXT("editor_utility"))
    {
        if (!ParentClass || !ParentClass->IsChildOf(UEditorUtilityObject::StaticClass()))
        {
            return nullptr;
        }
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            ParentClass,
            Package,
            AssetName,
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());
    }
    else if (ParentClass && FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            ParentClass,
            Package,
            AssetName,
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());
    }
    if (Blueprint)
    {
        FAssetRegistryModule::AssetCreated(Blueprint);
        Blueprint->MarkPackageDirty();
    }
    return Blueprint;
}

bool ApplyWidgetSpec(
    UWidgetBlueprint* WidgetBlueprint,
    const FThomasWidgetElementSpec& Spec,
    TMap<FString, UWidget*>& Widgets,
    FString& OutError)
{
    UClass* WidgetClass = LoadObject<UClass>(nullptr, *Spec.ClassPath);
    if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Invalid widget class: %s"), *Spec.ClassPath);
        return false;
    }
    UWidget* Widget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(
        WidgetClass, FName(*Spec.Name));
    if (!Widget)
    {
        OutError = FString::Printf(TEXT("Could not create widget: %s"), *Spec.Name);
        return false;
    }
    Widget->bIsVariable = Spec.bVariable;

    FLinearColor Color;
    if (!ParseColor(Spec.Color, Color))
    {
        OutError = FString::Printf(TEXT("Invalid color for %s"), *Spec.Name);
        return false;
    }
    if (UTextBlock* Text = Cast<UTextBlock>(Widget))
    {
        Text->SetText(FText::FromString(Spec.Text));
        Text->SetColorAndOpacity(FSlateColor(Color));
    }
    if (UProgressBar* Progress = Cast<UProgressBar>(Widget))
    {
        Progress->SetFillColorAndOpacity(Color);
    }
    if (UBorder* Border = Cast<UBorder>(Widget))
    {
        Border->SetBrushColor(Color);
        Border->SetPadding(FMargin(Spec.Padding));
    }
    if (USizeBox* SizeBox = Cast<USizeBox>(Widget))
    {
        SizeBox->SetWidthOverride(Spec.SizeX);
        SizeBox->SetHeightOverride(Spec.SizeY);
    }

    if (Spec.ParentName.IsEmpty())
    {
        if (WidgetBlueprint->WidgetTree->RootWidget)
        {
            OutError = TEXT("Widget batch must contain exactly one root.");
            return false;
        }
        WidgetBlueprint->WidgetTree->RootWidget = Widget;
    }
    else
    {
        UWidget* const* ParentPtr = Widgets.Find(Spec.ParentName);
        UPanelWidget* Parent = ParentPtr ? Cast<UPanelWidget>(*ParentPtr) : nullptr;
        if (!Parent || !Parent->AddChild(Widget))
        {
            OutError = FString::Printf(
                TEXT("Parent %s cannot accept child %s."), *Spec.ParentName, *Spec.Name);
            return false;
        }
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
        {
            CanvasSlot->SetAnchors(FAnchors(
                Spec.AnchorMinX, Spec.AnchorMinY, Spec.AnchorMaxX, Spec.AnchorMaxY));
            CanvasSlot->SetAlignment(FVector2D(Spec.AlignmentX, Spec.AlignmentY));
            CanvasSlot->SetPosition(FVector2D(Spec.PositionX, Spec.PositionY));
            CanvasSlot->SetSize(FVector2D(Spec.SizeX, Spec.SizeY));
            CanvasSlot->SetZOrder(Spec.ZOrder);
        }
    }
    Widgets.Add(Spec.Name, Widget);
    return true;
}

TArray<UEdGraph*> GetBlueprintGraphs(UBlueprint* Blueprint)
{
    TArray<UEdGraph*> Graphs;
    if (Blueprint)
    {
        Graphs.Append(Blueprint->UbergraphPages);
        Graphs.Append(Blueprint->FunctionGraphs);
        Graphs.Append(Blueprint->MacroGraphs);
        Graphs.Append(Blueprint->DelegateSignatureGraphs);
    }
    return Graphs;
}

UEdGraph* FindBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    for (UEdGraph* Graph : GetBlueprintGraphs(Blueprint))
    {
        if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::CaseSensitive))
        {
            return Graph;
        }
    }
    return nullptr;
}

UEdGraphNode* FindBlueprintNode(UBlueprint* Blueprint, const FString& NodeGuid)
{
    FGuid Guid;
    if (!FGuid::Parse(NodeGuid, Guid))
    {
        return nullptr;
    }
    for (UEdGraph* Graph : GetBlueprintGraphs(Blueprint))
    {
        if (Graph)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->NodeGuid == Guid)
                {
                    return Node;
                }
            }
        }
    }
    return nullptr;
}

UEdGraphNode* ResolveOperationNode(
    UBlueprint* Blueprint,
    const FString& Handle,
    const FString& NodeGuid,
    const TMap<FString, UEdGraphNode*>& Handles)
{
    if (!Handle.IsEmpty())
    {
        UEdGraphNode* const* Node = Handles.Find(Handle);
        return Node ? *Node : nullptr;
    }
    return FindBlueprintNode(Blueprint, NodeGuid);
}

TOptional<EEdGraphPinDirection> ParsePinDirection(const FString& Input)
{
    if (Input.IsEmpty() || Input.Equals(TEXT("any"), ESearchCase::IgnoreCase))
    {
        return TOptional<EEdGraphPinDirection>();
    }
    if (Input.Equals(TEXT("input"), ESearchCase::IgnoreCase))
    {
        return EGPD_Input;
    }
    if (Input.Equals(TEXT("output"), ESearchCase::IgnoreCase))
    {
        return EGPD_Output;
    }
    return EGPD_MAX;
}

UEdGraphPin* FindNodePin(
    UEdGraphNode* Node,
    const FString& PinName,
    const FString& Direction)
{
    if (!Node)
    {
        return nullptr;
    }
    const TOptional<EEdGraphPinDirection> ParsedDirection = ParsePinDirection(Direction);
    if (ParsedDirection.IsSet() && ParsedDirection.GetValue() == EGPD_MAX)
    {
        return nullptr;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::CaseSensitive)
            && (!ParsedDirection.IsSet() || Pin->Direction == ParsedDirection.GetValue()))
        {
            return Pin;
        }
    }
    return nullptr;
}

template <typename TNode, typename TConfigure>
TNode* AddConfiguredNode(
    UEdGraph* Graph,
    const int32 PositionX,
    const int32 PositionY,
    TConfigure&& Configure)
{
    FGraphNodeCreator<TNode> Creator(*Graph);
    TNode* Node = Creator.CreateNode();
    Node->NodePosX = PositionX;
    Node->NodePosY = PositionY;
    Configure(Node);
    Creator.Finalize();
    return Node;
}

UK2Node_CustomEvent* AddCustomEventNode(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FThomasBlueprintOperation& Operation)
{
    if (!Blueprint || !Graph || !FBlueprintEditorUtils::IsEventGraph(Graph))
    {
        return nullptr;
    }
    UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(Graph);
    Node->CreateNewGuid();
    Node->NodePosX = Operation.PositionX;
    Node->NodePosY = Operation.PositionY;
    Node->CustomFunctionName = FName(*Operation.Name);
    Node->bIsEditable = true;
    Node->SetFlags(RF_Transactional);
    Node->AllocateDefaultPins();
    Node->PostPlacedNewNode();
    Graph->Modify();
    Graph->AddNode(Node, true, false);
    return Node;
}

USCS_Node* FindLocalComponentNode(UBlueprint* Blueprint, const FString& Name)
{
    return Blueprint && Blueprint->SimpleConstructionScript
        ? Blueprint->SimpleConstructionScript->FindSCSNode(FName(*Name))
        : nullptr;
}

UClass* LoadInterfaceClass(const FString& ClassPath)
{
    UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
    return Class && Class->HasAnyClassFlags(CLASS_Interface) ? Class : nullptr;
}

UWidgetAnimation* FindWidgetAnimation(
    UWidgetBlueprint* WidgetBlueprint,
    const FString& AnimationName)
{
    if (!WidgetBlueprint || AnimationName.IsEmpty())
    {
        return nullptr;
    }
    const TObjectPtr<UWidgetAnimation>* Found =
        WidgetBlueprint->Animations.FindByPredicate(
        [&AnimationName](const TObjectPtr<UWidgetAnimation>& Animation)
        {
            return Animation
                && (Animation->GetName() == AnimationName
                    || Animation->GetDisplayLabel() == AnimationName);
        });
    return Found ? Found->Get() : nullptr;
}

FGuid EnsureWidgetAnimationBinding(
    UWidgetBlueprint* WidgetBlueprint,
    UWidgetAnimation* Animation,
    UWidget* Widget)
{
    for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
    {
        if (Binding.WidgetName == Widget->GetFName() && Binding.SlotWidgetName.IsNone())
        {
            return Binding.AnimationGuid;
        }
    }

    Animation->Modify();
    Animation->MovieScene->Modify();
    const FGuid Guid = Animation->MovieScene->AddPossessable(
        Widget->GetName(), Widget->GetClass());
    FWidgetAnimationBinding& Binding = Animation->AnimationBindings.AddDefaulted_GetRef();
    Binding.AnimationGuid = Guid;
    Binding.WidgetName = Widget->GetFName();
    Binding.bIsRootWidget = WidgetBlueprint->WidgetTree->RootWidget == Widget;
    return Guid;
}

UMovieSceneFloatTrack* FindWidgetOpacityTrack(
    UMovieScene* MovieScene,
    const FGuid& BindingGuid)
{
    const FMovieSceneBinding* Binding = MovieScene
        ? MovieScene->FindBinding(BindingGuid) : nullptr;
    if (!Binding)
    {
        return nullptr;
    }
    for (UMovieSceneTrack* Track : Binding->GetTracks())
    {
        UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
        if (FloatTrack && FloatTrack->GetPropertyPath() == TEXT("RenderOpacity"))
        {
            return FloatTrack;
        }
    }
    return nullptr;
}

bool BlueprintImplementsInterface(const UBlueprint* Blueprint, const UClass* InterfaceClass)
{
    return Blueprint && InterfaceClass
        && Blueprint->ImplementedInterfaces.ContainsByPredicate(
            [InterfaceClass](const FBPInterfaceDescription& Description)
            {
                return Description.Interface == InterfaceClass;
            });
}

bool SetComponentTemplateProperty(
    UBlueprint* Blueprint,
    const FThomasBlueprintOperation& Operation,
    FString& OutError)
{
    USCS_Node* Node = FindLocalComponentNode(Blueprint, Operation.Name);
    UActorComponent* Template = Node ? Node->ComponentTemplate.Get() : nullptr;
    FProperty* Property = Template
        ? FindFProperty<FProperty>(Template->GetClass(), FName(*Operation.PropertyName))
        : nullptr;
    if (!Template || !Property || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate))
    {
        OutError = FString::Printf(
            TEXT("Component property is not editable: %s.%s"),
            *Operation.Name,
            *Operation.PropertyName);
        return false;
    }

    FString CurrentValue;
    Property->ExportText_InContainer(0, CurrentValue, Template, Template, Template, PPF_None);
    if (!Operation.ExpectedValue.IsEmpty() && CurrentValue != Operation.ExpectedValue)
    {
        OutError = FString::Printf(
            TEXT("Component property conflict: expected '%s', current '%s'."),
            *Operation.ExpectedValue,
            *CurrentValue);
        return false;
    }

    Template->Modify();
    if (!Property->ImportText_InContainer(*Operation.Value, Template, Template, PPF_None))
    {
        OutError = FString::Printf(
            TEXT("Invalid component property value for %s.%s."),
            *Operation.Name,
            *Operation.PropertyName);
        return false;
    }
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
    Template->PostEditChangeProperty(Event);
    return true;
}

bool ApplyBlueprintOperation(
    UBlueprint* Blueprint,
    const FThomasBlueprintOperation& Operation,
    TMap<FString, UEdGraphNode*>& Handles,
    FString& OutError)
{
    const FString Action = Operation.Action.ToLower();
    const FName Name(*Operation.Name);
    if (Action == TEXT("add_variable"))
    {
        FEdGraphPinType PinType;
        if (!ParsePinType(
                Operation.Type,
                Operation.ContainerType,
                Operation.KeyType,
                PinType)
            || !FBlueprintEditorUtils::AddMemberVariable(
                Blueprint, Name, PinType, Operation.Value))
        {
            OutError = FString::Printf(TEXT("Could not add variable %s."), *Operation.Name);
            return false;
        }
        if (Operation.bExposeOnSpawn)
        {
            FBlueprintEditorUtils::SetBlueprintVariableMetaData(
                Blueprint,
                Name,
                nullptr,
                FBlueprintMetadata::MD_ExposeOnSpawn,
                TEXT("true"));
        }
        const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
            [Name](const FBPVariableDescription& Variable) { return Variable.VarName == Name; });
        if (Index != INDEX_NONE && Operation.bInstanceEditable)
        {
            Blueprint->NewVariables[Index].PropertyFlags &= ~CPF_DisableEditOnInstance;
        }
        return true;
    }
    if (Action == TEXT("remove_variable"))
    {
        const bool bExists = Blueprint->NewVariables.ContainsByPredicate(
            [Name](const FBPVariableDescription& Variable) { return Variable.VarName == Name; });
        if (!bExists)
        {
            OutError = FString::Printf(TEXT("Variable not found: %s"), *Operation.Name);
            return false;
        }
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
        return true;
    }
    if (Action == TEXT("rename_variable"))
    {
        if (Operation.NewName.IsEmpty())
        {
            OutError = TEXT("rename_variable requires NewName.");
            return false;
        }
        FBlueprintEditorUtils::RenameMemberVariable(
            Blueprint, Name, FName(*Operation.NewName));
        return true;
    }
    if (Action == TEXT("add_interface"))
    {
        UClass* InterfaceClass = LoadInterfaceClass(Operation.ClassPath);
        if (!InterfaceClass || BlueprintImplementsInterface(Blueprint, InterfaceClass)
            || !FBlueprintEditorUtils::ImplementNewInterface(
                Blueprint, InterfaceClass->GetClassPathName()))
        {
            OutError = FString::Printf(
                TEXT("Could not implement interface %s."), *Operation.ClassPath);
            return false;
        }
        return true;
    }
    if (Action == TEXT("remove_interface"))
    {
        UClass* InterfaceClass = LoadInterfaceClass(Operation.ClassPath);
        if (!InterfaceClass || !BlueprintImplementsInterface(Blueprint, InterfaceClass))
        {
            OutError = FString::Printf(
                TEXT("Interface is not implemented: %s."), *Operation.ClassPath);
            return false;
        }
        FBlueprintEditorUtils::RemoveInterface(
            Blueprint,
            InterfaceClass->GetClassPathName(),
            Operation.bPreserveFunctions);
        return true;
    }
    if (Action == TEXT("add_component"))
    {
        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        UClass* ComponentClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
        if (!SCS || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass())
            || !ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass())
            || FindLocalComponentNode(Blueprint, Operation.Name))
        {
            OutError = FString::Printf(
                TEXT("Could not add component %s (%s)."),
                *Operation.Name,
                *Operation.ClassPath);
            return false;
        }
        SCS->Modify();
        USCS_Node* Node = SCS->CreateNode(ComponentClass, Name);
        if (!Node)
        {
            OutError = FString::Printf(TEXT("Could not create component %s."), *Operation.Name);
            return false;
        }
        if (!Operation.ParentName.IsEmpty())
        {
            USCS_Node* Parent = FindLocalComponentNode(Blueprint, Operation.ParentName);
            if (!Parent || !Node->ComponentTemplate->IsA<USceneComponent>()
                || !Parent->ComponentTemplate->IsA<USceneComponent>())
            {
                OutError = FString::Printf(
                    TEXT("Invalid scene-component parent %s."), *Operation.ParentName);
                return false;
            }
            Parent->AddChildNode(Node);
        }
        else
        {
            SCS->AddNode(Node);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("remove_component"))
    {
        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
        USCS_Node* Node = FindLocalComponentNode(Blueprint, Operation.Name);
        if (!SCS || !Node)
        {
            OutError = FString::Printf(TEXT("Component not found: %s."), *Operation.Name);
            return false;
        }
        SCS->Modify();
        SCS->RemoveNodeAndPromoteChildren(Node);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("set_component_property"))
    {
        return SetComponentTemplateProperty(Blueprint, Operation, OutError);
    }
    if (Action == TEXT("set_cdo_property"))
    {
        UObject* CDO = Blueprint->GeneratedClass
            ? Blueprint->GeneratedClass->GetDefaultObject()
            : nullptr;
        FProperty* Property = CDO
            ? FindFProperty<FProperty>(CDO->GetClass(), Name)
            : nullptr;
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate))
        {
            OutError = FString::Printf(TEXT("CDO property is not editable: %s"), *Operation.Name);
            return false;
        }
        CDO->Modify();
        if (!Property->ImportText_InContainer(
                *Operation.Value, CDO, CDO, PPF_None))
        {
            OutError = FString::Printf(TEXT("Invalid value for %s."), *Operation.Name);
            return false;
        }
        CDO->PostEditChange();
        CDO->MarkPackageDirty();
        return true;
    }
    if (Action == TEXT("add_function_graph"))
    {
        if (FindBlueprintGraph(Blueprint, Operation.Name))
        {
            OutError = FString::Printf(TEXT("Graph already exists: %s"), *Operation.Name);
            return false;
        }
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint,
            FName(*Operation.Name),
            UEdGraph::StaticClass(),
            UEdGraphSchema_K2::StaticClass());
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("Could not create function graph %s."), *Operation.Name);
            return false;
        }
        FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
        return true;
    }
    if (Action == TEXT("remove_function_graph"))
    {
        UEdGraph* Graph = FindBlueprintGraph(Blueprint, Operation.Name);
        if (!Graph || !Blueprint->FunctionGraphs.Contains(Graph))
        {
            OutError = FString::Printf(TEXT("Function graph not found: %s"), *Operation.Name);
            return false;
        }
        FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
        return true;
    }
    if (Action == TEXT("add_widget_animation"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        if (!WidgetBlueprint || Operation.AnimationName.IsEmpty()
            || FindWidgetAnimation(WidgetBlueprint, Operation.AnimationName)
            || Operation.StartFrame < 0 || Operation.EndFrame <= Operation.StartFrame
            || Operation.DisplayRateNumerator < 1 || Operation.DisplayRateDenominator < 1)
        {
            OutError = FString::Printf(
                TEXT("Could not add Widget animation %s."), *Operation.AnimationName);
            return false;
        }
        WidgetBlueprint->Modify();
        UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(
            WidgetBlueprint,
            FName(*Operation.AnimationName),
            RF_Transactional);
        if (!Animation)
        {
            OutError = TEXT("Could not allocate Widget animation.");
            return false;
        }
        Animation->SetDisplayLabel(Operation.AnimationName);
        Animation->MovieScene = NewObject<UMovieScene>(
            Animation,
            FName(*Operation.AnimationName),
            RF_Transactional);
        if (!Animation->MovieScene)
        {
            OutError = TEXT("Could not allocate the Widget animation MovieScene.");
            return false;
        }
        const FFrameRate Rate(
            Operation.DisplayRateNumerator,
            Operation.DisplayRateDenominator);
        Animation->MovieScene->SetTickResolutionDirectly(Rate);
        Animation->MovieScene->SetDisplayRate(Rate);
        Animation->MovieScene->SetPlaybackRange(
            Operation.StartFrame,
            Operation.EndFrame - Operation.StartFrame);
        Animation->MovieScene->GetEditorData().WorkStart =
            Rate.AsSeconds(FFrameTime(Operation.StartFrame));
        Animation->MovieScene->GetEditorData().WorkEnd =
            Rate.AsSeconds(FFrameTime(Operation.EndFrame));
        WidgetBlueprint->Animations.Add(Animation);
        WidgetBlueprint->OnVariableAdded(Animation->GetFName());
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("remove_widget_animation"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        UWidgetAnimation* Animation = FindWidgetAnimation(
            WidgetBlueprint, Operation.AnimationName);
        if (!WidgetBlueprint || !Animation)
        {
            OutError = FString::Printf(
                TEXT("Widget animation not found: %s."), *Operation.AnimationName);
            return false;
        }
        WidgetBlueprint->Modify();
        Animation->Modify();
        const FName RemovedName = Animation->GetFName();
        Animation->Rename(nullptr, GetTransientPackage());
        WidgetBlueprint->Animations.Remove(Animation);
        WidgetBlueprint->OnVariableRemoved(RemovedName);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("set_widget_animation_opacity_key"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        UWidgetAnimation* Animation = FindWidgetAnimation(
            WidgetBlueprint, Operation.AnimationName);
        UWidget* Widget = WidgetBlueprint && WidgetBlueprint->WidgetTree
            ? WidgetBlueprint->WidgetTree->FindWidget(FName(*Operation.WidgetName))
            : nullptr;
        double ParsedValue = 0.0;
        if (!Animation || !Animation->MovieScene || !Widget
            || !LexTryParseString(ParsedValue, *Operation.Value)
            || ParsedValue < 0.0 || ParsedValue > 1.0)
        {
            OutError = FString::Printf(
                TEXT("Invalid Widget opacity key for %s.%s."),
                *Operation.AnimationName,
                *Operation.WidgetName);
            return false;
        }
        const FGuid BindingGuid = EnsureWidgetAnimationBinding(
            WidgetBlueprint, Animation, Widget);
        UMovieSceneFloatTrack* Track = FindWidgetOpacityTrack(
            Animation->MovieScene, BindingGuid);
        if (!Track)
        {
            Track = Animation->MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
            if (!Track)
            {
                OutError = TEXT("Could not add the RenderOpacity animation track.");
                return false;
            }
            Track->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
        }
        Track->Modify();
        UMovieSceneFloatSection* Section = Track->GetAllSections().IsEmpty()
            ? nullptr
            : Cast<UMovieSceneFloatSection>(Track->GetAllSections()[0]);
        if (!Section)
        {
            Section = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
            if (!Section)
            {
                OutError = TEXT("Could not add the RenderOpacity animation section.");
                return false;
            }
            Section->SetRange(Animation->MovieScene->GetPlaybackRange());
            Track->AddSection(*Section);
        }
        Section->Modify();
        FMovieSceneFloatValue Key(static_cast<float>(ParsedValue));
        const FString Mode = Operation.Interpolation.ToLower();
        Key.InterpMode = Mode == TEXT("constant") ? RCIM_Constant
            : Mode == TEXT("linear") ? RCIM_Linear : RCIM_Cubic;
        Section->GetChannel().GetData().UpdateOrAddKey(
            FFrameNumber(Operation.Frame), Key);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }
    if (Action == TEXT("add_widget_function_binding"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree
            || !WidgetBlueprint->WidgetTree->FindWidget(FName(*Operation.WidgetName)))
        {
            OutError = FString::Printf(
                TEXT("Widget binding target not found: %s."), *Operation.WidgetName);
            return false;
        }
        FDelegateEditorBinding Binding;
        Binding.ObjectName = Operation.WidgetName;
        Binding.PropertyName = FName(*Operation.PropertyName);
        Binding.FunctionName = FName(*Operation.FunctionName);
        Binding.Kind = EBindingKind::Function;
        FCompilerResultsLog ValidationLog;
        ValidationLog.bSilentMode = true;
        UClass* GeneratedClass = WidgetBlueprint->GeneratedClass
            ? WidgetBlueprint->GeneratedClass.Get()
            : WidgetBlueprint->SkeletonGeneratedClass.Get();
        if (!GeneratedClass
            || WidgetBlueprint->Bindings.Contains(Binding)
            || !Binding.IsBindingValid(GeneratedClass, WidgetBlueprint, ValidationLog))
        {
            OutError = FString::Printf(
                TEXT("Invalid Widget function binding %s.%s -> %s."),
                *Operation.WidgetName,
                *Operation.PropertyName,
                *Operation.FunctionName);
            return false;
        }
        WidgetBlueprint->Modify();
        WidgetBlueprint->Bindings.Add(MoveTemp(Binding));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("add_widget_property_binding"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        UClass* SkeletonClass = WidgetBlueprint
            ? WidgetBlueprint->SkeletonGeneratedClass.Get() : nullptr;
        UClass* GeneratedClass = WidgetBlueprint
            ? (WidgetBlueprint->GeneratedClass
                ? WidgetBlueprint->GeneratedClass.Get() : SkeletonClass)
            : nullptr;
        FProperty* SourceProperty = SkeletonClass
            ? FindFProperty<FProperty>(
                SkeletonClass, FName(*Operation.SourcePropertyName))
            : nullptr;
        if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree
            || !WidgetBlueprint->WidgetTree->FindWidget(FName(*Operation.WidgetName))
            || !SourceProperty || !GeneratedClass)
        {
            OutError = FString::Printf(
                TEXT("Widget property binding endpoint not found: %s.%s <- %s."),
                *Operation.WidgetName,
                *Operation.PropertyName,
                *Operation.SourcePropertyName);
            return false;
        }
        FDelegateEditorBinding Binding;
        Binding.ObjectName = Operation.WidgetName;
        Binding.PropertyName = FName(*Operation.PropertyName);
        Binding.SourceProperty = SourceProperty->GetFName();
        TArray<FFieldVariant> FieldChain;
        FieldChain.Add(FFieldVariant(SourceProperty));
        Binding.SourcePath = FEditorPropertyPath(FieldChain);
        UBlueprint::GetGuidFromClassByFieldName<FProperty>(
            SkeletonClass,
            SourceProperty->GetFName(),
            Binding.MemberGuid);
        Binding.Kind = EBindingKind::Property;
        FCompilerResultsLog ValidationLog;
        ValidationLog.bSilentMode = true;
        if (WidgetBlueprint->Bindings.Contains(Binding)
            || !Binding.IsBindingValid(
                GeneratedClass, WidgetBlueprint, ValidationLog))
        {
            OutError = FString::Printf(
                TEXT("Invalid Widget property binding %s.%s <- %s."),
                *Operation.WidgetName,
                *Operation.PropertyName,
                *Operation.SourcePropertyName);
            return false;
        }
        WidgetBlueprint->Modify();
        WidgetBlueprint->Bindings.Add(MoveTemp(Binding));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    if (Action == TEXT("remove_widget_function_binding")
        || Action == TEXT("remove_widget_binding"))
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        if (WidgetBlueprint)
        {
            WidgetBlueprint->Modify();
        }
        const int32 Removed = WidgetBlueprint
            ? WidgetBlueprint->Bindings.RemoveAll(
                [&Operation](const FDelegateEditorBinding& Binding)
                {
                    return Binding.ObjectName == Operation.WidgetName
                        && Binding.PropertyName == FName(*Operation.PropertyName);
                })
            : 0;
        if (Removed != 1)
        {
            OutError = FString::Printf(
                TEXT("Widget function binding not found: %s.%s."),
                *Operation.WidgetName,
                *Operation.PropertyName);
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    UEdGraph* Graph = Operation.GraphName.IsEmpty()
        ? nullptr
        : FindBlueprintGraph(Blueprint, Operation.GraphName);
    UEdGraphNode* CreatedNode = nullptr;
    if (Action == TEXT("add_custom_event"))
    {
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("Graph not found: %s"), *Operation.GraphName);
            return false;
        }
        CreatedNode = AddCustomEventNode(Blueprint, Graph, Operation);
        if (!CreatedNode)
        {
            OutError = TEXT("Custom events can only be added to an event graph.");
            return false;
        }
    }
    else if (Action == TEXT("add_call_function"))
    {
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("Graph not found: %s"), *Operation.GraphName);
            return false;
        }
        UClass* OwnerClass = Operation.ClassPath.IsEmpty()
            ? (Blueprint->SkeletonGeneratedClass
                ? Blueprint->SkeletonGeneratedClass.Get()
                : Blueprint->GeneratedClass.Get())
            : LoadObject<UClass>(nullptr, *Operation.ClassPath);
        UFunction* Function = OwnerClass
            ? OwnerClass->FindFunctionByName(FName(*Operation.FunctionName))
            : nullptr;
        if (!Function)
        {
            OutError = FString::Printf(
                TEXT("Function not found: %s on %s"),
                *Operation.FunctionName,
                *Operation.ClassPath);
            return false;
        }
        CreatedNode = AddConfiguredNode<UK2Node_CallFunction>(
            Graph, Operation.PositionX, Operation.PositionY,
            [Function, OwnerClass, &Operation](UK2Node_CallFunction* Node)
            {
                if (Operation.ClassPath.IsEmpty())
                {
                    Node->FunctionReference.SetSelfMember(Function->GetFName());
                }
                else
                {
                    Node->FunctionReference.SetExternalMember(Function->GetFName(), OwnerClass);
                }
                Node->SetFromFunction(Function);
            });
    }
    else if (Action == TEXT("add_variable_get") || Action == TEXT("add_variable_set"))
    {
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("Graph not found: %s"), *Operation.GraphName);
            return false;
        }
        const FName VariableName(*Operation.Name);
        if (Action == TEXT("add_variable_get"))
        {
            CreatedNode = AddConfiguredNode<UK2Node_VariableGet>(
                Graph, Operation.PositionX, Operation.PositionY,
                [VariableName](UK2Node_VariableGet* Node)
                {
                    Node->VariableReference.SetSelfMember(VariableName);
                });
        }
        else
        {
            CreatedNode = AddConfiguredNode<UK2Node_VariableSet>(
                Graph, Operation.PositionX, Operation.PositionY,
                [VariableName](UK2Node_VariableSet* Node)
                {
                    Node->VariableReference.SetSelfMember(VariableName);
                });
        }
    }
    else if (Action == TEXT("add_branch"))
    {
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("Graph not found: %s"), *Operation.GraphName);
            return false;
        }
        CreatedNode = AddConfiguredNode<UK2Node_IfThenElse>(
            Graph, Operation.PositionX, Operation.PositionY,
            [](UK2Node_IfThenElse*) {});
    }

    if (CreatedNode)
    {
        if (Operation.Handle.IsEmpty() || Handles.Contains(Operation.Handle))
        {
            OutError = FString::Printf(TEXT("Invalid or duplicate node handle: %s"), *Operation.Handle);
            return false;
        }
        Handles.Add(Operation.Handle, CreatedNode);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    if (Action == TEXT("set_pin_default"))
    {
        UEdGraphNode* Node = ResolveOperationNode(
            Blueprint, Operation.Handle, Operation.NodeGuid, Handles);
        UEdGraphPin* Pin = FindNodePin(Node, Operation.PinName, Operation.PinDirection);
        if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0)
        {
            OutError = FString::Printf(TEXT("Unconnected input pin not found: %s"), *Operation.PinName);
            return false;
        }
        const UEdGraphSchema* Schema = Pin->GetSchema();
        const FString ValidationError = Schema
            ? Schema->IsPinDefaultValid(Pin, Operation.Value, nullptr, FText::GetEmpty())
            : TEXT("Pin has no schema.");
        if (!ValidationError.IsEmpty())
        {
            OutError = ValidationError;
            return false;
        }
        Schema->TrySetDefaultValue(*Pin, Operation.Value);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }
    if (Action == TEXT("connect_pins") || Action == TEXT("disconnect_pins"))
    {
        UEdGraphNode* Node = ResolveOperationNode(
            Blueprint, Operation.Handle, Operation.NodeGuid, Handles);
        UEdGraphNode* OtherNode = ResolveOperationNode(
            Blueprint, Operation.OtherHandle, Operation.OtherNodeGuid, Handles);
        UEdGraphPin* Pin = FindNodePin(Node, Operation.PinName, Operation.PinDirection);
        UEdGraphPin* OtherPin = FindNodePin(
            OtherNode, Operation.OtherPinName, Operation.OtherPinDirection);
        if (!Pin || !OtherPin || Pin == OtherPin || Pin->GetSchema() != OtherPin->GetSchema())
        {
            OutError = TEXT("Pin endpoint not found or schema mismatch.");
            return false;
        }
        const UEdGraphSchema* Schema = Pin->GetSchema();
        const bool bChanged = Action == TEXT("connect_pins")
            ? Schema->TryCreateConnection(Pin, OtherPin)
            : (Pin->LinkedTo.Contains(OtherPin)
                ? (Schema->BreakSinglePinLink(Pin, OtherPin), true)
                : false);
        if (!bChanged)
        {
            OutError = Action == TEXT("connect_pins")
                ? TEXT("Schema rejected the pin connection.")
                : TEXT("The requested pin link does not exist.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }
    if (Action == TEXT("remove_node"))
    {
        UEdGraphNode* Node = ResolveOperationNode(
            Blueprint, Operation.Handle, Operation.NodeGuid, Handles);
        if (!Node || !Node->CanUserDeleteNode())
        {
            OutError = TEXT("Node not found or protected from deletion.");
            return false;
        }
        Node->Modify();
        Node->DestroyNode();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
    OutError = FString::Printf(TEXT("Unsupported Blueprint action: %s"), *Operation.Action);
    return false;
}
}

class FThomasEditorBlueprintModule final : public IThomasEditorBlueprintProviderModule
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override { Plans.Reset(); }

    virtual FThomasBlueprintInspectionResult InspectBlueprint(
        const FString& AssetPath,
        const bool bIncludeNodes,
        const int32 MaxNodes) override
    {
        const FString PackageName = NormalizeAssetPath(AssetPath);
        if (!IsAllowedPackagePath(PackageName))
        {
            return MakeError<FThomasBlueprintInspectionResult>(
                TEXT("path_denied"), TEXT("Blueprint must remain under /Game/PropHunt."));
        }
        if (MaxNodes < 1 || MaxNodes > MaxInspectionNodes)
        {
            return MakeError<FThomasBlueprintInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxNodes must be between 1 and 1000."));
        }
        UBlueprint* Blueprint = LoadBlueprint(PackageName);
        if (!Blueprint)
        {
            return MakeError<FThomasBlueprintInspectionResult>(
                TEXT("blueprint_not_found"), PackageName);
        }

        FThomasBlueprintInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.ParentClassPath = Blueprint->ParentClass
            ? Blueprint->ParentClass->GetPathName() : FString();
        Result.GeneratedClassPath = Blueprint->GeneratedClass
            ? Blueprint->GeneratedClass->GetPathName() : FString();
        Result.Revision = BuildRevision(Blueprint);
        Result.CompileStatus = CompileStatusText(Blueprint);
        Result.AssetKind = BlueprintKind(Blueprint);
        Result.bDirty = Blueprint->GetOutermost()->IsDirty();
        Result.bWidgetBlueprint = Blueprint->IsA<UWidgetBlueprint>();

        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            if (Interface.Interface)
            {
                Result.Interfaces.Add(Interface.Interface->GetPathName());
            }
        }
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            FThomasBlueprintVariableRecord Item;
            Item.Name = Variable.VarName.ToString();
            Item.Type = PinTypeText(Variable.VarType);
            Item.DefaultValue = Variable.DefaultValue.Left(1200);
            Item.bInstanceEditable =
                (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0;
            Item.bExposeOnSpawn = Variable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
            Result.Variables.Add(MoveTemp(Item));
        }
        if (Blueprint->SimpleConstructionScript)
        {
            for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (!Node)
                {
                    continue;
                }
                const FString ComponentName = Node->GetVariableName().ToString();
                Result.Components.Add(ComponentName);
                FThomasBlueprintComponentRecord Item;
                Item.Name = ComponentName;
                Item.ClassPath = Node->ComponentTemplate
                    ? Node->ComponentTemplate->GetClass()->GetPathName() : FString();
                Item.TemplatePath = Node->ComponentTemplate
                    ? Node->ComponentTemplate->GetPathName() : FString();
                Item.bSceneComponent = Node->ComponentTemplate
                    && Node->ComponentTemplate->IsA<USceneComponent>();
                if (const USCS_Node* Parent = Blueprint->SimpleConstructionScript->FindParentNode(
                        const_cast<USCS_Node*>(Node)))
                {
                    Item.ParentName = Parent->GetVariableName().ToString();
                }
                Result.ComponentDetails.Add(MoveTemp(Item));
            }
        }

        if (bIncludeNodes)
        {
            TArray<UEdGraph*> Graphs;
            Graphs.Append(Blueprint->UbergraphPages);
            Graphs.Append(Blueprint->FunctionGraphs);
            Graphs.Append(Blueprint->MacroGraphs);
            Graphs.Append(Blueprint->DelegateSignatureGraphs);
            for (UEdGraph* Graph : Graphs)
            {
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Result.Nodes.Num() >= MaxNodes)
                    {
                        Result.bTruncated = true;
                        break;
                    }
                    FThomasBlueprintNodeRecord Item;
                    Item.GraphName = Graph->GetName();
                    Item.NodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
                    Item.ClassPath = Node->GetClass()->GetPathName();
                    Item.Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(300);
                    Item.PositionX = Node->NodePosX;
                    Item.PositionY = Node->NodePosY;
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (!Pin)
                        {
                            continue;
                        }
                        if (Item.Pins.Num() >= MaxInspectionPinsPerNode)
                        {
                            Result.bTruncated = true;
                            break;
                        }
                        FThomasBlueprintPinRecord PinItem;
                        PinItem.Name = Pin->PinName.ToString();
                        PinItem.Direction = Pin->Direction == EGPD_Input
                            ? TEXT("input") : TEXT("output");
                        PinItem.Type = PinTypeText(Pin->PinType);
                        PinItem.DefaultValue = Pin->DefaultValue.Left(1200);
                        if (PinItem.DefaultValue.IsEmpty() && !Pin->DefaultTextValue.IsEmpty())
                        {
                            PinItem.DefaultValue = Pin->DefaultTextValue.ToString().Left(1200);
                        }
                        if (PinItem.DefaultValue.IsEmpty() && Pin->DefaultObject)
                        {
                            PinItem.DefaultValue = Pin->DefaultObject->GetPathName();
                        }
                        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (LinkedPin && LinkedPin->GetOwningNode())
                            {
                                PinItem.LinkedTo.Add(FString::Printf(
                                    TEXT("%s:%s:%s"),
                                    *LinkedPin->GetOwningNode()->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphensLower),
                                    LinkedPin->Direction == EGPD_Input
                                        ? TEXT("input") : TEXT("output"),
                                    *LinkedPin->PinName.ToString()));
                            }
                        }
                        Item.Pins.Add(MoveTemp(PinItem));
                    }
                    Result.Nodes.Add(MoveTemp(Item));
                }
                if (Result.bTruncated) break;
            }
        }

        if (const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
        {
            WidgetBlueprint->WidgetTree->ForEachWidget([&Result](UWidget* Widget)
            {
                FThomasWidgetRecord Item;
                Item.Name = Widget->GetName();
                Item.ClassPath = Widget->GetClass()->GetPathName();
                Item.bVariable = Widget->bIsVariable;
                if (Widget->Slot)
                {
                    Item.SlotClassPath = Widget->Slot->GetClass()->GetPathName();
                    if (Widget->Slot->Parent)
                    {
                        Item.ParentName = Widget->Slot->Parent->GetName();
                    }
                }
                Result.Widgets.Add(MoveTemp(Item));
            });
            for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
            {
                if (!Animation || !Animation->MovieScene)
                {
                    continue;
                }
                FThomasWidgetAnimationRecord AnimationRecord;
                AnimationRecord.Name = Animation->GetName();
                AnimationRecord.DisplayLabel = Animation->GetDisplayLabel();
                const FFrameRate DisplayRate = Animation->MovieScene->GetDisplayRate();
                AnimationRecord.DisplayRateNumerator = DisplayRate.Numerator;
                AnimationRecord.DisplayRateDenominator = DisplayRate.Denominator;
                const TRange<FFrameNumber> PlaybackRange =
                    Animation->MovieScene->GetPlaybackRange();
                AnimationRecord.PlaybackStartFrame = PlaybackRange.HasLowerBound()
                    ? PlaybackRange.GetLowerBoundValue().Value : 0;
                AnimationRecord.PlaybackEndFrame = PlaybackRange.HasUpperBound()
                    ? PlaybackRange.GetUpperBoundValue().Value : 0;
                for (const FWidgetAnimationBinding& Binding : Animation->GetBindings())
                {
                    FThomasWidgetAnimationBindingRecord BindingRecord;
                    BindingRecord.Guid = Binding.AnimationGuid.ToString(
                        EGuidFormats::DigitsWithHyphensLower);
                    BindingRecord.WidgetName = Binding.WidgetName.ToString();
                    BindingRecord.bRootWidget = Binding.bIsRootWidget;
                    if (const UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(
                            Binding.WidgetName))
                    {
                        BindingRecord.ClassPath = Widget->GetClass()->GetPathName();
                    }
                    const FMovieSceneBinding* MovieSceneBinding =
                        Animation->MovieScene->FindBinding(Binding.AnimationGuid);
                    BindingRecord.TrackCount = MovieSceneBinding
                        ? MovieSceneBinding->GetTracks().Num() : 0;
                    AnimationRecord.Bindings.Add(MoveTemp(BindingRecord));

                    if (!MovieSceneBinding)
                    {
                        continue;
                    }
                    for (const UMovieSceneTrack* Track : MovieSceneBinding->GetTracks())
                    {
                        const UMovieSceneFloatTrack* FloatTrack =
                            Cast<UMovieSceneFloatTrack>(Track);
                        if (!FloatTrack
                            || FloatTrack->GetPropertyPath() != TEXT("RenderOpacity"))
                        {
                            continue;
                        }
                        for (const UMovieSceneSection* BaseSection : FloatTrack->GetAllSections())
                        {
                            const UMovieSceneFloatSection* Section =
                                Cast<UMovieSceneFloatSection>(BaseSection);
                            if (!Section)
                            {
                                continue;
                            }
                            const auto Data = Section->GetChannel().GetData();
                            const auto Times = Data.GetTimes();
                            const auto Values = Data.GetValues();
                            for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
                            {
                                FThomasWidgetAnimationKeyRecord KeyRecord;
                                KeyRecord.WidgetName = Binding.WidgetName.ToString();
                                KeyRecord.PropertyName = TEXT("RenderOpacity");
                                KeyRecord.Frame = Times[KeyIndex].Value;
                                KeyRecord.Value = Values[KeyIndex].Value;
                                KeyRecord.Interpolation =
                                    Values[KeyIndex].InterpMode == RCIM_Constant
                                        ? TEXT("constant")
                                        : Values[KeyIndex].InterpMode == RCIM_Linear
                                            ? TEXT("linear")
                                            : TEXT("cubic");
                                AnimationRecord.Keys.Add(MoveTemp(KeyRecord));
                            }
                        }
                    }
                }
                Result.WidgetAnimations.Add(MoveTemp(AnimationRecord));
            }
            for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
            {
                FThomasWidgetFunctionBindingRecord BindingRecord;
                BindingRecord.WidgetName = Binding.ObjectName;
                BindingRecord.PropertyName = Binding.PropertyName.ToString();
                BindingRecord.FunctionName = Binding.FunctionName.ToString();
                BindingRecord.SourcePropertyName = Binding.SourceProperty.ToString();
                BindingRecord.Kind = Binding.Kind == EBindingKind::Function
                    ? TEXT("function") : TEXT("property");
                Result.WidgetBindings.Add(MoveTemp(BindingRecord));
            }
        }
        return Result;
    }

    virtual FThomasBlueprintPlanResult PlanBlueprintBatch(
        const FThomasBlueprintBatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasBlueprintBatchRequest Request = InputRequest;
        Request.AssetPath = NormalizeAssetPath(Request.AssetPath);
        Request.AssetKind = NormalizeBlueprintKind(
            Request.AssetKind, Request.bWidgetBlueprint);
        Request.bWidgetBlueprint = IsWidgetKind(Request.AssetKind);
        if (!IsAllowedPackagePath(Request.AssetPath))
        {
            return MakeError<FThomasBlueprintPlanResult>(
                TEXT("path_denied"), TEXT("Blueprint must remain under /Game/PropHunt."));
        }
        if (Request.Operations.Num() + Request.WidgetElements.Num() < 1
            || Request.Operations.Num() + Request.WidgetElements.Num() > MaxBatchOperations)
        {
            return MakeError<FThomasBlueprintPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Batch must contain 1 to 200 operations/elements."));
        }
        if (!IsSupportedBlueprintKind(Request.AssetKind))
        {
            return MakeError<FThomasBlueprintPlanResult>(
                TEXT("invalid_asset_kind"), Request.AssetKind);
        }

        UBlueprint* Existing = LoadBlueprint(Request.AssetPath);
        const FString CurrentRevision = BuildRevision(Existing);
        if (Existing)
        {
            if (Request.ExpectedRevision.IsEmpty()
                || Request.ExpectedRevision != CurrentRevision)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("revision_conflict"), CurrentRevision);
            }
            if (Request.bCreateIfMissing)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("asset_exists"), Request.AssetPath);
            }
            if (Request.AssetKind != BlueprintKind(Existing))
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("asset_kind_mismatch"), Request.AssetPath);
            }
            if (Existing->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("asset_dirty"),
                    TEXT("Save or revert the Blueprint before creating a mutation plan."));
            }
        }
        else
        {
            if (!Request.bCreateIfMissing)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("blueprint_not_found"), Request.AssetPath);
            }
            if (!Request.ExpectedRevision.IsEmpty()
                && Request.ExpectedRevision != TEXT("missing"))
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("revision_conflict"), TEXT("missing"));
            }
            if (Request.AssetKind == TEXT("interface") && Request.ParentClassPath.IsEmpty())
            {
                Request.ParentClassPath = UInterface::StaticClass()->GetPathName();
            }
            UClass* ParentClass = LoadObject<UClass>(nullptr, *Request.ParentClassPath);
            const bool bValidParent = ParentClass
                && (Request.AssetKind == TEXT("interface")
                    || (Request.AssetKind == TEXT("widget")
                        && ParentClass->IsChildOf(UUserWidget::StaticClass()))
                    || (Request.AssetKind == TEXT("editor_utility_widget")
                        && ParentClass->IsChildOf(UEditorUtilityWidget::StaticClass()))
                    || (Request.AssetKind == TEXT("editor_utility")
                        && ParentClass->IsChildOf(UEditorUtilityObject::StaticClass()))
                    || (Request.AssetKind == TEXT("normal")
                        && FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass)));
            if (!bValidParent)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("invalid_parent_class"), Request.ParentClassPath);
            }
            Request.ExpectedRevision = TEXT("missing");
        }

        TSet<FString> VariableNames;
        TSet<FString> GraphNames;
        TSet<FString> FunctionGraphNames;
        TSet<FString> InterfacePaths;
        TSet<FString> ComponentNames;
        TSet<FString> KnownNodeGuids;
        TSet<FString> WidgetNames;
        TSet<FString> WidgetAnimationNames;
        TMap<FString, TPair<int32, int32>> WidgetAnimationRanges;
        TSet<FString> WidgetFunctionBindings;
        bool bHasDestructiveOperation = false;
        if (Existing)
        {
            for (const FBPVariableDescription& Variable : Existing->NewVariables)
            {
                VariableNames.Add(Variable.VarName.ToString());
            }
            for (const FBPInterfaceDescription& Interface : Existing->ImplementedInterfaces)
            {
                if (Interface.Interface)
                {
                    InterfacePaths.Add(Interface.Interface->GetPathName());
                }
            }
            if (Existing->SimpleConstructionScript)
            {
                for (const USCS_Node* Node : Existing->SimpleConstructionScript->GetAllNodes())
                {
                    if (Node)
                    {
                        ComponentNames.Add(Node->GetVariableName().ToString());
                    }
                }
            }
            for (UEdGraph* Graph : GetBlueprintGraphs(Existing))
            {
                if (!Graph)
                {
                    continue;
                }
                GraphNames.Add(Graph->GetName());
                if (Existing->FunctionGraphs.Contains(Graph))
                {
                    FunctionGraphNames.Add(Graph->GetName());
                }
                for (const UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node)
                    {
                        KnownNodeGuids.Add(Node->NodeGuid.ToString(
                            EGuidFormats::DigitsWithHyphensLower));
                    }
                }
            }
            if (const UWidgetBlueprint* WidgetBlueprint =
                    Cast<UWidgetBlueprint>(Existing))
            {
                WidgetBlueprint->WidgetTree->ForEachWidget(
                    [&WidgetNames](UWidget* Widget)
                    {
                        WidgetNames.Add(Widget->GetName());
                    });
                for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
                {
                    if (!Animation || !Animation->MovieScene)
                    {
                        continue;
                    }
                    WidgetAnimationNames.Add(Animation->GetName());
                    WidgetAnimationNames.Add(Animation->GetDisplayLabel());
                    const TRange<FFrameNumber> Range =
                        Animation->MovieScene->GetPlaybackRange();
                    WidgetAnimationRanges.Add(
                        Animation->GetName(),
                        TPair<int32, int32>(
                            Range.HasLowerBound()
                                ? Range.GetLowerBoundValue().Value : 0,
                            Range.HasUpperBound()
                                ? Range.GetUpperBoundValue().Value : 0));
                    WidgetAnimationRanges.Add(
                        Animation->GetDisplayLabel(),
                        WidgetAnimationRanges[Animation->GetName()]);
                }
                for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
                {
                    WidgetFunctionBindings.Add(
                        Binding.ObjectName + TEXT("\x1f")
                        + Binding.PropertyName.ToString());
                }
            }
        }
        else
        {
            if (Request.AssetKind != TEXT("interface"))
            {
                // New normal and widget Blueprints created by UE have their standard event graph.
                GraphNames.Add(TEXT("EventGraph"));
            }
        }
        TSet<FString> KnownHandles;
        auto HasNodeReference = [&KnownHandles, &KnownNodeGuids](
            const FString& Handle, const FString& Guid)
        {
            return (!Handle.IsEmpty() && KnownHandles.Contains(Handle))
                || (!Guid.IsEmpty() && KnownNodeGuids.Contains(Guid.ToLower()));
        };
        for (const FThomasBlueprintOperation& Operation : Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("add_variable"))
            {
                FEdGraphPinType Type;
                if (Operation.Name.IsEmpty() || VariableNames.Contains(Operation.Name)
                    || !ParsePinType(
                        Operation.Type,
                        Operation.ContainerType,
                        Operation.KeyType,
                        Type))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_variable_add"), Operation.Name);
                }
                VariableNames.Add(Operation.Name);
            }
            else if (Action == TEXT("remove_variable"))
            {
                if (Operation.Name.IsEmpty() || !VariableNames.Remove(Operation.Name))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("variable_not_found"), Operation.Name);
                }
            }
            else if (Action == TEXT("rename_variable"))
            {
                if (!VariableNames.Remove(Operation.Name)
                    || Operation.NewName.IsEmpty()
                    || VariableNames.Contains(Operation.NewName))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_variable_rename"), Operation.Name);
                }
                VariableNames.Add(Operation.NewName);
            }
            else if (Action == TEXT("add_interface"))
            {
                UClass* InterfaceClass = LoadInterfaceClass(Operation.ClassPath);
                const FString InterfacePath = InterfaceClass
                    ? InterfaceClass->GetPathName() : FString();
                if (!InterfaceClass || InterfacePaths.Contains(InterfacePath))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_interface_add"), Operation.ClassPath);
                }
                InterfacePaths.Add(InterfacePath);
            }
            else if (Action == TEXT("remove_interface"))
            {
                UClass* InterfaceClass = LoadInterfaceClass(Operation.ClassPath);
                const FString InterfacePath = InterfaceClass
                    ? InterfaceClass->GetPathName() : FString();
                if (!Request.bConfirmDestructive || !InterfaceClass
                    || !InterfacePaths.Remove(InterfacePath))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("interface_not_found")
                            : TEXT("confirmation_required"),
                        Operation.ClassPath);
                }
            }
            else if (Action == TEXT("add_component"))
            {
                UClass* ParentClass = Existing
                    ? Existing->ParentClass.Get()
                    : LoadObject<UClass>(nullptr, *Request.ParentClassPath);
                UClass* ComponentClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (Operation.Name.IsEmpty() || ComponentNames.Contains(Operation.Name)
                    || !ParentClass || !ParentClass->IsChildOf(AActor::StaticClass())
                    || !ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass())
                    || (!Operation.ParentName.IsEmpty()
                        && !ComponentNames.Contains(Operation.ParentName)))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_component_add"), Operation.Name);
                }
                ComponentNames.Add(Operation.Name);
            }
            else if (Action == TEXT("remove_component"))
            {
                if (!Request.bConfirmDestructive || Operation.Name.IsEmpty()
                    || !ComponentNames.Remove(Operation.Name))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("component_not_found")
                            : TEXT("confirmation_required"),
                        Operation.Name);
                }
            }
            else if (Action == TEXT("set_component_property"))
            {
                if (!Existing || Operation.Name.IsEmpty()
                    || !ComponentNames.Contains(Operation.Name)
                    || Operation.PropertyName.IsEmpty())
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_component_property"), Operation.Name);
                }
                USCS_Node* Node = FindLocalComponentNode(Existing, Operation.Name);
                UActorComponent* Template = Node ? Node->ComponentTemplate.Get() : nullptr;
                FProperty* Property = Template
                    ? FindFProperty<FProperty>(
                        Template->GetClass(), FName(*Operation.PropertyName))
                    : nullptr;
                if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
                    || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("component_property_denied"), Operation.PropertyName);
                }
                FString CurrentValue;
                Property->ExportText_InContainer(
                    0, CurrentValue, Template, Template, Template, PPF_None);
                if (!Operation.ExpectedValue.IsEmpty()
                    && CurrentValue != Operation.ExpectedValue)
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("value_conflict"), CurrentValue);
                }
            }
            else if (Action == TEXT("set_cdo_property"))
            {
                if (Operation.Name.IsEmpty())
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_operation"), TEXT("set_cdo_property requires Name."));
                }
            }
            else if (Action == TEXT("add_function_graph"))
            {
                if (Operation.Name.IsEmpty() || GraphNames.Contains(Operation.Name))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_function_graph_add"), Operation.Name);
                }
                GraphNames.Add(Operation.Name);
                FunctionGraphNames.Add(Operation.Name);
            }
            else if (Action == TEXT("remove_function_graph"))
            {
                if (Operation.Name.IsEmpty() || !FunctionGraphNames.Remove(Operation.Name))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("function_graph_not_found"), Operation.Name);
                }
                GraphNames.Remove(Operation.Name);
            }
            else if (Action == TEXT("add_widget_animation"))
            {
                if (!Existing || !Existing->IsA<UWidgetBlueprint>()
                    || Operation.AnimationName.IsEmpty()
                    || WidgetAnimationNames.Contains(Operation.AnimationName)
                    || Operation.StartFrame < 0
                    || Operation.EndFrame <= Operation.StartFrame
                    || Operation.DisplayRateNumerator < 1
                    || Operation.DisplayRateDenominator < 1)
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_widget_animation_add"),
                        Operation.AnimationName);
                }
                WidgetAnimationNames.Add(Operation.AnimationName);
                WidgetAnimationRanges.Add(
                    Operation.AnimationName,
                    TPair<int32, int32>(
                        Operation.StartFrame,
                        Operation.EndFrame));
            }
            else if (Action == TEXT("remove_widget_animation"))
            {
                if (!Request.bConfirmDestructive
                    || Operation.AnimationName.IsEmpty()
                    || !WidgetAnimationNames.Remove(Operation.AnimationName))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("widget_animation_not_found")
                            : TEXT("confirmation_required"),
                        Operation.AnimationName);
                }
                WidgetAnimationRanges.Remove(Operation.AnimationName);
                bHasDestructiveOperation = true;
            }
            else if (Action == TEXT("set_widget_animation_opacity_key"))
            {
                double ParsedValue = 0.0;
                const TPair<int32, int32>* Range =
                    WidgetAnimationRanges.Find(Operation.AnimationName);
                const FString Mode = Operation.Interpolation.ToLower();
                if (!Existing || !Existing->IsA<UWidgetBlueprint>()
                    || !Range || !WidgetNames.Contains(Operation.WidgetName)
                    || Operation.Frame < Range->Key
                    || Operation.Frame >= Range->Value
                    || !LexTryParseString(ParsedValue, *Operation.Value)
                    || ParsedValue < 0.0 || ParsedValue > 1.0
                    || (Mode != TEXT("constant")
                        && Mode != TEXT("linear")
                        && Mode != TEXT("cubic")))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_widget_animation_key"),
                        Operation.AnimationName + TEXT("/") + Operation.WidgetName);
                }
            }
            else if (Action == TEXT("add_widget_function_binding"))
            {
                UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Existing);
                const FString BindingKey = Operation.WidgetName + TEXT("\x1f")
                    + Operation.PropertyName;
                FDelegateEditorBinding Binding;
                Binding.ObjectName = Operation.WidgetName;
                Binding.PropertyName = FName(*Operation.PropertyName);
                Binding.FunctionName = FName(*Operation.FunctionName);
                Binding.Kind = EBindingKind::Function;
                FCompilerResultsLog ValidationLog;
                ValidationLog.bSilentMode = true;
                UClass* GeneratedClass = WidgetBlueprint
                    ? (WidgetBlueprint->GeneratedClass
                        ? WidgetBlueprint->GeneratedClass.Get()
                        : WidgetBlueprint->SkeletonGeneratedClass.Get())
                    : nullptr;
                if (!WidgetBlueprint || !GeneratedClass
                    || Operation.WidgetName.IsEmpty()
                    || Operation.PropertyName.IsEmpty()
                    || Operation.FunctionName.IsEmpty()
                    || !WidgetNames.Contains(Operation.WidgetName)
                    || WidgetFunctionBindings.Contains(BindingKey)
                    || !Binding.IsBindingValid(
                        GeneratedClass, WidgetBlueprint, ValidationLog))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_widget_function_binding"),
                        Operation.WidgetName + TEXT(".") + Operation.PropertyName);
                }
                WidgetFunctionBindings.Add(BindingKey);
            }
            else if (Action == TEXT("add_widget_property_binding"))
            {
                UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Existing);
                UClass* SkeletonClass = WidgetBlueprint
                    ? WidgetBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                UClass* GeneratedClass = WidgetBlueprint
                    ? (WidgetBlueprint->GeneratedClass
                        ? WidgetBlueprint->GeneratedClass.Get() : SkeletonClass)
                    : nullptr;
                FProperty* SourceProperty = SkeletonClass
                    ? FindFProperty<FProperty>(
                        SkeletonClass, FName(*Operation.SourcePropertyName))
                    : nullptr;
                const FString BindingKey = Operation.WidgetName + TEXT("\x1f")
                    + Operation.PropertyName;
                FDelegateEditorBinding Binding;
                Binding.ObjectName = Operation.WidgetName;
                Binding.PropertyName = FName(*Operation.PropertyName);
                Binding.SourceProperty = SourceProperty
                    ? SourceProperty->GetFName() : NAME_None;
                if (SourceProperty)
                {
                    TArray<FFieldVariant> FieldChain;
                    FieldChain.Add(FFieldVariant(SourceProperty));
                    Binding.SourcePath = FEditorPropertyPath(FieldChain);
                    UBlueprint::GetGuidFromClassByFieldName<FProperty>(
                        SkeletonClass,
                        SourceProperty->GetFName(),
                        Binding.MemberGuid);
                }
                Binding.Kind = EBindingKind::Property;
                FCompilerResultsLog ValidationLog;
                ValidationLog.bSilentMode = true;
                if (!WidgetBlueprint || !GeneratedClass || !SourceProperty
                    || Operation.WidgetName.IsEmpty()
                    || Operation.PropertyName.IsEmpty()
                    || Operation.SourcePropertyName.IsEmpty()
                    || !WidgetNames.Contains(Operation.WidgetName)
                    || WidgetFunctionBindings.Contains(BindingKey)
                    || !Binding.IsBindingValid(
                        GeneratedClass, WidgetBlueprint, ValidationLog))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_widget_property_binding"),
                        Operation.WidgetName + TEXT(".") + Operation.PropertyName
                            + TEXT(" <- ") + Operation.SourcePropertyName);
                }
                WidgetFunctionBindings.Add(BindingKey);
            }
            else if (Action == TEXT("remove_widget_function_binding")
                || Action == TEXT("remove_widget_binding"))
            {
                const FString BindingKey = Operation.WidgetName + TEXT("\x1f")
                    + Operation.PropertyName;
                if (!Request.bConfirmDestructive
                    || !WidgetFunctionBindings.Remove(BindingKey))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        Request.bConfirmDestructive
                            ? TEXT("widget_function_binding_not_found")
                            : TEXT("confirmation_required"),
                        Operation.WidgetName + TEXT(".") + Operation.PropertyName);
                }
                bHasDestructiveOperation = true;
            }
            else if (Action == TEXT("add_custom_event")
                || Action == TEXT("add_call_function")
                || Action == TEXT("add_variable_get")
                || Action == TEXT("add_variable_set")
                || Action == TEXT("add_branch"))
            {
                if (!GraphNames.Contains(Operation.GraphName)
                    || Operation.Handle.IsEmpty()
                    || KnownHandles.Contains(Operation.Handle))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_node_add"), Operation.Handle);
                }
                if (Action == TEXT("add_custom_event") && Operation.Name.IsEmpty())
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_custom_event"), TEXT("Custom event Name is required."));
                }
                if ((Action == TEXT("add_variable_get") || Action == TEXT("add_variable_set"))
                    && !VariableNames.Contains(Operation.Name))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("variable_not_found"), Operation.Name);
                }
                if (Action == TEXT("add_call_function"))
                {
                    UClass* OwnerClass = Operation.ClassPath.IsEmpty()
                        ? (Existing
                            ? (Existing->SkeletonGeneratedClass
                                ? Existing->SkeletonGeneratedClass.Get()
                                : Existing->GeneratedClass.Get())
                            : LoadObject<UClass>(nullptr, *Request.ParentClassPath))
                        : LoadObject<UClass>(nullptr, *Operation.ClassPath);
                    if (Operation.FunctionName.IsEmpty() || !OwnerClass
                        || !OwnerClass->FindFunctionByName(FName(*Operation.FunctionName)))
                    {
                        return MakeError<FThomasBlueprintPlanResult>(
                            TEXT("function_not_found"), Operation.FunctionName);
                    }
                }
                KnownHandles.Add(Operation.Handle);
            }
            else if (Action == TEXT("set_pin_default") || Action == TEXT("remove_node"))
            {
                if (!HasNodeReference(Operation.Handle, Operation.NodeGuid)
                    || (Action == TEXT("set_pin_default") && Operation.PinName.IsEmpty()))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_node_reference"), Operation.Handle + Operation.NodeGuid);
                }
                if (ParsePinDirection(Operation.PinDirection).IsSet()
                    && ParsePinDirection(Operation.PinDirection).GetValue() == EGPD_MAX)
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_pin_direction"), Operation.PinDirection);
                }
            }
            else if (Action == TEXT("connect_pins") || Action == TEXT("disconnect_pins"))
            {
                if (!HasNodeReference(Operation.Handle, Operation.NodeGuid)
                    || !HasNodeReference(Operation.OtherHandle, Operation.OtherNodeGuid)
                    || Operation.PinName.IsEmpty() || Operation.OtherPinName.IsEmpty())
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_pin_endpoints"), Operation.PinName);
                }
                const TOptional<EEdGraphPinDirection> Direction =
                    ParsePinDirection(Operation.PinDirection);
                const TOptional<EEdGraphPinDirection> OtherDirection =
                    ParsePinDirection(Operation.OtherPinDirection);
                if ((Direction.IsSet() && Direction.GetValue() == EGPD_MAX)
                    || (OtherDirection.IsSet() && OtherDirection.GetValue() == EGPD_MAX))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_pin_direction"),
                        Operation.PinDirection + TEXT("/") + Operation.OtherPinDirection);
                }
            }
            else
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }

        if (!Request.WidgetElements.IsEmpty())
        {
            if (!Request.bWidgetBlueprint)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("not_widget_blueprint"), Request.AssetPath);
            }
            if (Existing && !Request.bReplaceWidgetTree)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("replace_confirmation_required"),
                    TEXT("Existing Widget Tree changes require bReplaceWidgetTree=true."));
            }
            TSet<FString> RequestedWidgetNames;
            int32 RootCount = 0;
            for (const FThomasWidgetElementSpec& Spec : Request.WidgetElements)
            {
                UClass* WidgetClass = LoadObject<UClass>(nullptr, *Spec.ClassPath);
                if (Spec.Name.IsEmpty() || RequestedWidgetNames.Contains(Spec.Name)
                    || !WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass())
                    || (!Spec.ParentName.IsEmpty()
                        && !RequestedWidgetNames.Contains(Spec.ParentName)))
                {
                    return MakeError<FThomasBlueprintPlanResult>(
                        TEXT("invalid_widget_spec"), Spec.Name);
                }
                RootCount += Spec.ParentName.IsEmpty() ? 1 : 0;
                RequestedWidgetNames.Add(Spec.Name);
            }
            if (RootCount != 1)
            {
                return MakeError<FThomasBlueprintPlanResult>(
                    TEXT("invalid_widget_root"), TEXT("Exactly one Widget Tree root is required."));
            }
        }

        FThomasBlueprintPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Request.ExpectedRevision;
        Result.OperationCount = Request.Operations.Num() + Request.WidgetElements.Num();
        Result.Risk = (Existing && Request.bReplaceWidgetTree)
                || bHasDestructiveOperation
            ? TEXT("R2") : TEXT("R1");
        Result.Preview.Add(Existing
            ? TEXT("Modify existing Blueprint under one scoped transaction.")
            : TEXT("Create a new Blueprint asset; delete it on failed validation."));
        if (Request.bReplaceWidgetTree)
        {
            Result.Preview.Add(TEXT("Replace the complete Widget Tree."));
        }
        Result.Preview.Add(TEXT("Compile/save occur only in ApplyBlueprintPlan."));

        FCachedPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasBlueprintApplyResult ApplyBlueprintPlan(
        const FString& PlanId,
        const bool bCompile,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasBlueprintApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (bSave && !bCompile)
        {
            return MakeError<FThomasBlueprintApplyResult>(
                TEXT("compile_required"), TEXT("Saving a Blueprint requires compilation."));
        }

        UBlueprint* Blueprint = LoadBlueprint(Plan.Request.AssetPath);
        const FString CurrentRevision = BuildRevision(Blueprint);
        if (CurrentRevision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasBlueprintApplyResult>(
                TEXT("revision_conflict"), CurrentRevision);
        }

        FThomasBlueprintApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = CurrentRevision;
        if (!Blueprint)
        {
            UClass* ParentClass = LoadObject<UClass>(
                nullptr, *Plan.Request.ParentClassPath);
            Blueprint = CreateBlueprintAsset(
                Plan.Request.AssetPath,
                ParentClass,
                Plan.Request.AssetKind);
            if (!Blueprint)
            {
                return MakeError<FThomasBlueprintApplyResult>(
                    TEXT("create_failed"), Plan.Request.AssetPath);
            }
            Result.bCreated = true;
        }

        bool bApplySucceeded = true;
        FString ApplyError;
        {
            FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyBlueprintBatch", "ThomasEditor Blueprint Batch"));
            Blueprint->Modify();
            TMap<FString, UEdGraphNode*> NodeHandles;
            for (const FThomasBlueprintOperation& Operation : Plan.Request.Operations)
            {
                if (!ApplyBlueprintOperation(Blueprint, Operation, NodeHandles, ApplyError))
                {
                    bApplySucceeded = false;
                    break;
                }
                ++Result.AppliedOperationCount;
            }

            if (bApplySucceeded && !Plan.Request.WidgetElements.IsEmpty())
            {
                UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
                if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
                {
                    ApplyError = TEXT("Widget Blueprint has no WidgetTree.");
                    bApplySucceeded = false;
                }
                else
                {
                    WidgetBlueprint->WidgetTree->Modify();
                    if (Plan.Request.bReplaceWidgetTree)
                    {
                        TArray<UWidget*> ExistingWidgets;
                        WidgetBlueprint->WidgetTree->GetAllWidgets(ExistingWidgets);
                        for (UWidget* ExistingWidget : ExistingWidgets)
                        {
                            WidgetBlueprint->WidgetTree->RemoveWidget(ExistingWidget);
                        }
                        WidgetBlueprint->WidgetTree->RootWidget = nullptr;
                    }
                    TMap<FString, UWidget*> Widgets;
                    for (const FThomasWidgetElementSpec& Spec : Plan.Request.WidgetElements)
                    {
                        if (!ApplyWidgetSpec(WidgetBlueprint, Spec, Widgets, ApplyError))
                        {
                            bApplySucceeded = false;
                            break;
                        }
                        ++Result.AppliedOperationCount;
                    }
                    if (bApplySucceeded)
                    {
                        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    }
                }
            }
            if (!bApplySucceeded)
            {
                Transaction.Cancel();
            }
        }

        if (!bApplySucceeded)
        {
            Rollback(
                Blueprint,
                Plan.Request.AssetPath,
                Result.bCreated,
                false,
                Result);
            Result.Code = TEXT("apply_failed");
            Result.Message = ApplyError.Left(1200);
            return Result;
        }

        if (bCompile)
        {
            FCompilerResultsLog CompilerLog;
            CompilerLog.bSilentMode = true;
            FKismetEditorUtilities::CompileBlueprint(
                Blueprint, EBlueprintCompileOptions::None, &CompilerLog);
            Result.bCompiled = CompilerLog.NumErrors == 0 && Blueprint->Status != BS_Error;
            Result.Diagnostics.Add(FString::Printf(
                TEXT("compile_errors=%d compile_warnings=%d"),
                CompilerLog.NumErrors,
                CompilerLog.NumWarnings));
            if (!Result.bCompiled)
            {
                Rollback(
                    Blueprint,
                    Plan.Request.AssetPath,
                    Result.bCreated,
                    true,
                    Result);
                Result.Code = TEXT("compile_failed");
                Result.Message = TEXT("Blueprint compile failed; changes were rolled back.");
                return Result;
            }
        }

        if (bSave)
        {
            Result.bSaved = SaveBlueprint(Blueprint);
            if (!Result.bSaved)
            {
                Rollback(
                    Blueprint,
                    Plan.Request.AssetPath,
                    Result.bCreated,
                    true,
                    Result);
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("Blueprint save failed; rollback was attempted.");
                return Result;
            }
        }

        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Blueprint);
        return Result;
    }

    virtual FThomasBlueprintValidationResult ValidateBlueprint(
        const FString& AssetPath,
        const TArray<FString>& RequiredWidgetNames) override
    {
        UBlueprint* Blueprint = LoadBlueprint(AssetPath);
        if (!Blueprint)
        {
            return MakeError<FThomasBlueprintValidationResult>(
                TEXT("blueprint_not_found"), NormalizeAssetPath(AssetPath));
        }
        FThomasBlueprintValidationResult Result;
        Result.AssetPath = NormalizeAssetPath(AssetPath);
        Result.Revision = BuildRevision(Blueprint);
        Result.CompileStatus = CompileStatusText(Blueprint);
        Result.bOk = Blueprint->Status != BS_Error;
        if (!RequiredWidgetNames.IsEmpty())
        {
            const UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
            if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
            {
                Result.bOk = false;
                Result.Code = TEXT("not_widget_blueprint");
                return Result;
            }
            for (const FString& Name : RequiredWidgetNames)
            {
                if (!WidgetBlueprint->WidgetTree->FindWidget(FName(*Name)))
                {
                    Result.MissingWidgets.Add(Name);
                }
            }
            Result.bOk = Result.bOk && Result.MissingWidgets.IsEmpty();
        }
        if (!Result.bOk && Result.Code.IsEmpty())
        {
            Result.Code = TEXT("validation_failed");
            Result.Message = TEXT("Compile status or required Widget Tree contract failed.");
        }
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

    static void Rollback(
        UBlueprint* Blueprint,
        const FString& AssetPath,
        const bool bCreated,
        const bool bTransactionCommitted,
        FThomasBlueprintApplyResult& Result)
    {
        if (bCreated)
        {
            Result.bRolledBack =
                Blueprint && ObjectTools::DeleteObjectsUnchecked({Blueprint}) == 1;
        }
        else if (bTransactionCommitted && GEditor)
        {
            Result.bRolledBack = GEditor->UndoTransaction();
            if (Blueprint)
            {
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
            }
        }
        else if (Blueprint)
        {
            FText ReloadError;
            Result.bRolledBack = UPackageTools::ReloadPackages(
                {Blueprint->GetOutermost()},
                ReloadError,
                EReloadPackagesInteractionMode::AssumePositive);
            if (!Result.bRolledBack && !ReloadError.IsEmpty())
            {
                Result.Diagnostics.Add(ReloadError.ToString().Left(1200));
            }
        }
        Result.RevisionAfter = bCreated
            ? TEXT("missing")
            : BuildRevision(LoadBlueprint(AssetPath));
    }

    TMap<FString, FCachedPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorBlueprintModule, ThomasEditorBlueprint)
