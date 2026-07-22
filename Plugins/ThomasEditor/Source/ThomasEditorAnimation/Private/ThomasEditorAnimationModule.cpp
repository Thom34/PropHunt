#include "ThomasEditorAnimationProvider.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "K2Node_TransitionRuleGetter.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ControlRig.h"
#include "ControlRigAssetFactory.h"
#include "ControlRigEditorAsset.h"
#include "ControlRigGizmoLibrary.h"
#include "ControlRigRuntimeAsset.h"
#include "Curves/CurveFloat.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "EulerTransform.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/Solvers/IKRigLimbSolver.h"
#include "Rig/Solvers/IKRigSolverBase.h"
#include "RigEditor/IKRigController.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetOverrides.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "RigVMCore/RigVMDispatchFactory.h"
#include "RigVMCore/RigVMFunction.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMModel/Nodes/RigVMDispatchNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/RigVMBuildData.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/UObjectIterator.h"
#include "ScopedTransaction.h"
#include "ThomasEditorAssetsProvider.h"
#include "TransformNoScale.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr double PlanLifetimeSeconds = 300.0;
constexpr int32 MaxControlRigMetadataArrayItems = 16;
constexpr int32 MaxControlRigShapeLibraries = 16;
constexpr int32 MaxStateGraphPropertyRefs = 128;
constexpr int32 MaxStateGraphArrayRefs = 32;
constexpr int32 MaxStateGraphPropertyDepth = 6;
constexpr int32 MaxStateGraphArrayItems = 16;

struct FCachedAnimationCreatePlan
{
    FThomasDomainAssetCreateRequest Request;
    FString FactoryClassPath;
    double CreatedAtSeconds = 0.0;
};

struct FCachedAnimationPatchPlan
{
    FThomasAnimationPatchRequest Request;
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
    return Trimmed.Contains(TEXT("."))
        ? FPackageName::ObjectPathToPackageName(Trimmed)
        : Trimmed;
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

bool IsSafeGraphName(const FString& Name)
{
    return !Name.IsEmpty()
        && Name.Len() <= 64
        && !Name.Contains(TEXT("."))
        && !Name.Contains(TEXT("/"))
        && !Name.Contains(TEXT("\\"))
        && !Name.Contains(TEXT(":"));
}

FString TransitionKey(const FString& FromState, const FString& ToState)
{
    return FromState + FString::Chr(0x1f) + ToState;
}

FString FactoryClassForKind(const FString& Kind)
{
    if (Kind == TEXT("anim_sequence"))
    {
        return TEXT("/Script/UnrealEd.AnimSequenceFactory");
    }
    if (Kind == TEXT("anim_montage"))
    {
        return TEXT("/Script/UnrealEd.AnimMontageFactory");
    }
    if (Kind == TEXT("blend_space"))
    {
        return TEXT("/Script/UnrealEd.BlendSpaceFactoryNew");
    }
    if (Kind == TEXT("blend_space_1d"))
    {
        return TEXT("/Script/UnrealEd.BlendSpaceFactory1D");
    }
    if (Kind == TEXT("anim_blueprint"))
    {
        return TEXT("/Script/UnrealEd.AnimBlueprintFactory");
    }
    if (Kind == TEXT("ik_rig") || Kind == TEXT("ik_retargeter"))
    {
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("IKRig"));
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("IKRigEditor"));
        UClass* AssetClass = LoadObject<UClass>(
            nullptr,
            Kind == TEXT("ik_rig")
                ? TEXT("/Script/IKRig.IKRigDefinition")
                : TEXT("/Script/IKRig.IKRetargeter"));
        if (!AssetClass)
        {
            return FString();
        }
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Candidate = *It;
            if (!Candidate
                || Candidate->HasAnyClassFlags(CLASS_Abstract)
                || !Candidate->IsChildOf(UFactory::StaticClass()))
            {
                continue;
            }
            const UFactory* Factory = Cast<UFactory>(
                Candidate->GetDefaultObject());
            if (Factory && Factory->SupportedClass == AssetClass)
            {
                return Candidate->GetPathName();
            }
        }
    }
    if (Kind == TEXT("control_rig"))
    {
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("ControlRig"));
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("ControlRigDeveloper"));
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("ControlRigEditor"));
        UClass* AssetClass = LoadObject<UClass>(
            nullptr, TEXT("/Script/ControlRig.ControlRigRuntimeAsset"));
        if (!AssetClass)
        {
            return FString();
        }
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Candidate = *It;
            if (!Candidate
                || Candidate->HasAnyClassFlags(CLASS_Abstract)
                || !Candidate->IsChildOf(UFactory::StaticClass()))
            {
                continue;
            }
            const UFactory* Factory = Cast<UFactory>(
                Candidate->GetDefaultObject());
            if (Factory && Factory->SupportedClass == AssetClass)
            {
                return Candidate->GetPathName();
            }
        }
    }
    return FString();
}

USkeleton* ResolveSkeleton(UObject* Context)
{
    if (USkeleton* Skeleton = Cast<USkeleton>(Context))
    {
        return Skeleton;
    }
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Context))
    {
        return Mesh->GetSkeleton();
    }
    return nullptr;
}

UFactory* CreateFactory(
    const FString& FactoryClassPath,
    UObject* Context,
    const FString& AssetKind)
{
    UClass* FactoryClass = LoadObject<UClass>(nullptr, *FactoryClassPath);
    UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
    if (!Factory || !Factory->SupportedClass || !Context)
    {
        return nullptr;
    }
    if (AssetKind == TEXT("ik_rig"))
    {
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(Context);
        if (!Mesh)
        {
            return nullptr;
        }
        if (FObjectPropertyBase* TargetMesh =
                FindFProperty<FObjectPropertyBase>(
                    Factory->GetClass(), TEXT("TargetSkeletalMesh")))
        {
            if (!TargetMesh->PropertyClass
                || !Mesh->IsA(TargetMesh->PropertyClass))
            {
                return nullptr;
            }
            TargetMesh->SetObjectPropertyValue_InContainer(Factory, Mesh);
        }
        return Factory;
    }
    if (AssetKind == TEXT("ik_retargeter"))
    {
        return Cast<UIKRigDefinition>(Context) ? Factory : nullptr;
    }
    if (AssetKind == TEXT("control_rig"))
    {
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(Context);
        UControlRigAssetFactory* ControlRigFactory =
            Cast<UControlRigAssetFactory>(Factory);
        if (!Mesh || !ControlRigFactory)
        {
            return nullptr;
        }
        ControlRigFactory->ParentClass = UControlRig::StaticClass();
        ControlRigFactory->bCreateAsControlRigModule = false;
        return ControlRigFactory;
    }
    USkeleton* Skeleton = ResolveSkeleton(Context);
    FObjectPropertyBase* TargetSkeleton = FindFProperty<FObjectPropertyBase>(
        Factory->GetClass(), TEXT("TargetSkeleton"));
    if (!TargetSkeleton || !Skeleton)
    {
        return nullptr;
    }
    TargetSkeleton->SetObjectPropertyValue_InContainer(Factory, Skeleton);
    return Factory;
}

bool SetIKRigPreviewSkeletalMesh(
    UObject* Asset,
    USkeletalMesh* Mesh)
{
    UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Asset);
    UIKRigController* Controller = IKRig
        ? UIKRigController::GetController(IKRig) : nullptr;
    return Controller && Mesh
        && Controller->SetSkeletalMesh(Mesh)
        && Controller->GetSkeletalMesh() == Mesh
        && Controller->GetIKRigSkeleton().SkeletalMesh == Mesh
        && Controller->GetIKRigSkeleton().BoneNames.Num() > 0;
}

bool SetIKRetargeterSourceRig(UObject* Asset, UIKRigDefinition* SourceRig)
{
    UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Asset);
    UIKRetargeterController* Controller = Retargeter
        ? UIKRetargeterController::GetController(Retargeter) : nullptr;
    if (!Controller || !SourceRig)
    {
        return false;
    }
    Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
    return Controller->GetIKRig(ERetargetSourceOrTarget::Source) == SourceRig;
}

bool InitializeControlRigFromSkeletalMesh(
    UObject* Asset,
    USkeletalMesh* Mesh)
{
    UControlRigRuntimeAsset* ControlRigAsset =
        Cast<UControlRigRuntimeAsset>(Asset);
    if (!ControlRigAsset || !Mesh)
    {
        return false;
    }
    ControlRigAsset->SetPreviewSkeletalMesh(
        TSoftObjectPtr<USkeletalMesh>(Mesh));
    URigHierarchy* Hierarchy = ControlRigAsset->GetHierarchy();
    URigHierarchyController* Controller = Hierarchy
        ? Hierarchy->GetController(true) : nullptr;
    if (!Hierarchy || !Controller)
    {
        return false;
    }
    const TArray<FRigElementKey> ImportedBones =
        Controller->ImportBonesFromSkeletalMesh(
            Mesh,
            NAME_None,
            true,
            true,
            false,
            true,
            false);
    const TArray<FRigElementKey> HierarchyBones =
        Hierarchy->GetAllKeys(true, ERigElementType::Bone);
    return !ImportedBones.IsEmpty()
        && HierarchyBones.Num() == Mesh->GetRefSkeleton().GetNum()
        && ControlRigAsset->GetPreviewSkeletalMesh().Get() == Mesh;
}

UControlRigEditorAsset* GetControlRigEditorAsset(
    UControlRigRuntimeAsset* RuntimeAsset)
{
    return RuntimeAsset
        ? Cast<UControlRigEditorAsset>(RuntimeAsset->GetEditorAsset())
        : nullptr;
}

TArray<URigVMGraph*> CollectControlRigModels(
    UControlRigEditorAsset* EditorAsset)
{
    TArray<URigVMGraph*> Models = EditorAsset
        ? EditorAsset->GetAllModels()
        : TArray<URigVMGraph*>();
    if (!EditorAsset)
    {
        return Models;
    }

    Models.AddUnique(EditorAsset->GetDefaultModel());
    if (URigVMFunctionLibrary* FunctionLibrary =
            EditorAsset->GetLocalFunctionLibrary())
    {
        for (URigVMLibraryNode* Function : FunctionLibrary->GetFunctions())
        {
            if (Function && Function->GetContainedGraph())
            {
                Models.AddUnique(Function->GetContainedGraph());
            }
        }
    }

    for (int32 ModelIndex = 0; ModelIndex < Models.Num(); ++ModelIndex)
    {
        URigVMGraph* Model = Models[ModelIndex];
        if (!Model)
        {
            continue;
        }
        for (URigVMNode* Node : Model->GetNodes())
        {
            if (URigVMCollapseNode* CollapseNode =
                    Cast<URigVMCollapseNode>(Node))
            {
                Models.AddUnique(CollapseNode->GetContainedGraph());
            }
        }
    }
    Models.RemoveAll([](const URigVMGraph* Model)
    {
        return Model == nullptr;
    });
    return Models;
}

URigVMGraph* ResolveControlRigModel(
    UControlRigEditorAsset* EditorAsset,
    const FString& GraphName)
{
    if (!EditorAsset)
    {
        return nullptr;
    }
    if (GraphName.IsEmpty())
    {
        return EditorAsset->GetDefaultModel();
    }
    if (URigVMGraph* Model = EditorAsset->GetModel(GraphName))
    {
        return Model;
    }
    for (URigVMGraph* Model : CollectControlRigModels(EditorAsset))
    {
        if (Model
            && (Model->GetName() == GraphName
                || Model->GetGraphName() == GraphName
                || Model->GetNodePath() == GraphName))
        {
            return Model;
        }
    }
    return nullptr;
}

FString RigVMGraphIdentifier(const URigVMGraph* Graph)
{
    if (!Graph)
    {
        return TEXT("None");
    }
    const FString NodePath = Graph->GetNodePath();
    return NodePath.IsEmpty() ? Graph->GetGraphName() : NodePath;
}

FString RigVMNodePositionString(const URigVMNode* Node)
{
    if (!Node)
    {
        return TEXT("Missing");
    }
    const FVector2D Position = Node->GetPosition();
    return FString::Printf(
        TEXT("X=%.6f,Y=%.6f"), Position.X, Position.Y);
}

FString RigVMVector2DString(const FVector2D& Value)
{
    return FString::Printf(
        TEXT("X=%.6f,Y=%.6f"), Value.X, Value.Y);
}

FString RigVMNodeSizeString(const URigVMNode* Node)
{
    return Node ? RigVMVector2DString(Node->GetSize()) : TEXT("Missing");
}

FString RigVMColorString(const FLinearColor& Color)
{
    return FString::Printf(
        TEXT("R=%.6f,G=%.6f,B=%.6f,A=%.6f"),
        static_cast<double>(Color.R),
        static_cast<double>(Color.G),
        static_cast<double>(Color.B),
        static_cast<double>(Color.A));
}

FString RigVMNodeColorString(const URigVMNode* Node)
{
    return Node ? RigVMColorString(Node->GetNodeColor()) : TEXT("Missing");
}

FString CanonicalRigVMCommentText(const FString& Text)
{
    FString Canonical = Text;
    Canonical.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Canonical.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Canonical.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Canonical.ReplaceInline(TEXT("|"), TEXT("\\|"));
    return Canonical;
}

FString RigVMCommentPropertiesString(
    const FString& Text,
    const int32 FontSize,
    const bool bBubbleVisible,
    const bool bColorBubble)
{
    return TEXT("Text=") + CanonicalRigVMCommentText(Text)
        + FString::Printf(
            TEXT("|FontSize=%d|BubbleVisible=%s|ColorBubble=%s"),
            FontSize,
            bBubbleVisible ? TEXT("true") : TEXT("false"),
            bColorBubble ? TEXT("true") : TEXT("false"));
}

FString RigVMCommentPropertiesString(const URigVMCommentNode* CommentNode)
{
    if (!CommentNode)
    {
        return TEXT("Missing");
    }
    return RigVMCommentPropertiesString(
        CommentNode->GetCommentText(),
        CommentNode->GetCommentFontSize(),
        CommentNode->GetCommentBubbleVisible(),
        CommentNode->GetCommentColorBubble());
}

bool IsRigVMWildcardPin(const URigVMPin* Pin)
{
    return Pin
        && (Pin->IsWildCard()
            || Pin->GetCPPType().Contains(TEXT("FRigVMUnknownType")));
}

struct FBoundedRigVMTypeSpec
{
    FString CPPType;
    UObject* CPPTypeObject = nullptr;
};

TOptional<FBoundedRigVMTypeSpec> ResolveBoundedRigVMType(
    const FString& Alias,
    const bool bArray)
{
    FBoundedRigVMTypeSpec Spec;
    if (Alias == TEXT("bool"))
    {
        Spec.CPPType = TEXT("bool");
    }
    else if (Alias == TEXT("int32"))
    {
        Spec.CPPType = TEXT("int32");
    }
    else if (Alias == TEXT("float"))
    {
        Spec.CPPType = TEXT("float");
    }
    else if (Alias == TEXT("double"))
    {
        Spec.CPPType = TEXT("double");
    }
    else if (Alias == TEXT("name"))
    {
        Spec.CPPType = TEXT("FName");
    }
    else if (Alias == TEXT("string"))
    {
        Spec.CPPType = TEXT("FString");
    }
    else if (Alias == TEXT("vector2d"))
    {
        Spec.CPPType = TEXT("FVector2D");
        Spec.CPPTypeObject = TBaseStructure<FVector2D>::Get();
    }
    else if (Alias == TEXT("vector"))
    {
        Spec.CPPType = TEXT("FVector");
        Spec.CPPTypeObject = TBaseStructure<FVector>::Get();
    }
    else if (Alias == TEXT("vector4"))
    {
        Spec.CPPType = TEXT("FVector4");
        Spec.CPPTypeObject = TBaseStructure<FVector4>::Get();
    }
    else if (Alias == TEXT("quat"))
    {
        Spec.CPPType = TEXT("FQuat");
        Spec.CPPTypeObject = TBaseStructure<FQuat>::Get();
    }
    else if (Alias == TEXT("rotator"))
    {
        Spec.CPPType = TEXT("FRotator");
        Spec.CPPTypeObject = TBaseStructure<FRotator>::Get();
    }
    else if (Alias == TEXT("transform"))
    {
        Spec.CPPType = TEXT("FTransform");
        Spec.CPPTypeObject = TBaseStructure<FTransform>::Get();
    }
    else if (Alias == TEXT("linear_color"))
    {
        Spec.CPPType = TEXT("FLinearColor");
        Spec.CPPTypeObject = TBaseStructure<FLinearColor>::Get();
    }
    else
    {
        return TOptional<FBoundedRigVMTypeSpec>();
    }
    if (bArray)
    {
        Spec.CPPType = TEXT("TArray<") + Spec.CPPType + TEXT(">");
    }
    return Spec;
}

FString RigVMPinTypeStateString(const URigVMPin* Pin)
{
    if (!Pin)
    {
        return TEXT("Missing");
    }
    const UObject* CPPTypeObject = Pin->GetCPPTypeObject();
    return FString::Printf(
        TEXT("CPPType=%s|CPPTypeObject=%s|IsWildCard=%s"),
        *Pin->GetCPPType(),
        CPPTypeObject ? *CPPTypeObject->GetPathName() : TEXT("None"),
        IsRigVMWildcardPin(Pin) ? TEXT("true") : TEXT("false"));
}

FString RigVMMemberVariableStateString(
    const FRigVMGraphVariableDescription& Variable)
{
    return TEXT("CPPType=") + Variable.CPPType
        + TEXT("|CPPTypeObject=")
        + (Variable.CPPTypeObject
            ? Variable.CPPTypeObject->GetPathName() : TEXT("None"))
        + TEXT("|Default=") + Variable.DefaultValue
        + TEXT("|Guid=") + Variable.Guid.ToString(
            EGuidFormats::DigitsWithHyphensLower);
}

bool FindRigVMMemberVariable(
    UControlRigEditorAsset* EditorAsset,
    const FName VariableName,
    FRigVMGraphVariableDescription& OutVariable)
{
    if (!EditorAsset)
    {
        return false;
    }
    for (const FRigVMGraphVariableDescription& Variable :
         EditorAsset->GetAssetVariables())
    {
        if (Variable.Name == VariableName)
        {
            OutVariable = Variable;
            return true;
        }
    }
    return false;
}

bool FindRigVMLocalVariable(
    const URigVMGraph* Model,
    const FName VariableName,
    FRigVMGraphVariableDescription& OutVariable)
{
    if (!Model)
    {
        return false;
    }
    for (const FRigVMGraphVariableDescription& Variable :
         Model->GetLocalVariables(false))
    {
        if (Variable.Name == VariableName)
        {
            OutVariable = Variable;
            return true;
        }
    }
    return false;
}

int32 FindRigVMLocalVariableIndex(
    const URigVMGraph* Model,
    const FName VariableName,
    FRigVMGraphVariableDescription& OutVariable)
{
    if (!Model)
    {
        return INDEX_NONE;
    }
    const TArray<FRigVMGraphVariableDescription> Variables =
        Model->GetLocalVariables(false);
    for (int32 Index = 0; Index < Variables.Num(); ++Index)
    {
        if (Variables[Index].Name == VariableName)
        {
            OutVariable = Variables[Index];
            return Index;
        }
    }
    return INDEX_NONE;
}

FString RigVMLocalVariableStateString(
    const FRigVMGraphVariableDescription& Variable,
    const int32 Index)
{
    return RigVMMemberVariableStateString(Variable)
        + FString::Printf(TEXT("|Index=%d"), Index);
}

URigVMLibraryNode* FindRigVMLocalFunctionForGraph(
    UControlRigEditorAsset* EditorAsset,
    const URigVMGraph* Model)
{
    URigVMFunctionLibrary* FunctionLibrary = EditorAsset
        ? EditorAsset->GetLocalFunctionLibrary() : nullptr;
    if (!FunctionLibrary || !Model)
    {
        return nullptr;
    }
    for (URigVMLibraryNode* Function : FunctionLibrary->GetFunctions())
    {
        if (Function && Function->GetContainedGraph() == Model)
        {
            return Function;
        }
    }
    return nullptr;
}

URigVMNode* RigVMAggregateDefinitionNode(URigVMNode* Node)
{
    if (URigVMAggregateNode* AggregateNode =
        Cast<URigVMAggregateNode>(Node))
    {
        return AggregateNode->GetFirstInnerNode();
    }
    return Node;
}

bool HasRigVMAggregateState(URigVMNode* Node)
{
    URigVMNode* DefinitionNode = RigVMAggregateDefinitionNode(Node);
    return Node && DefinitionNode
        && (Cast<URigVMAggregateNode>(Node)
            || DefinitionNode->IsAggregate());
}

TArray<URigVMPin*> GetEditableRigVMAggregatePins(URigVMNode* Node)
{
    URigVMNode* DefinitionNode = RigVMAggregateDefinitionNode(Node);
    if (!HasRigVMAggregateState(Node) || !DefinitionNode)
    {
        return {};
    }

    const bool bInputAggregate = DefinitionNode->IsInputAggregate();
    const TArray<URigVMPin*> BasePins = bInputAggregate
        ? DefinitionNode->GetAggregateInputs()
        : DefinitionNode->GetAggregateOutputs();
    if (!Cast<URigVMAggregateNode>(Node))
    {
        return BasePins;
    }

    TArray<URigVMPin*> Pins;
    Pins.Reserve(64);
    for (const URigVMPin* BasePin : BasePins)
    {
        URigVMPin* Pin = BasePin
            ? Node->FindRootPinByName(BasePin->GetFName()) : nullptr;
        if (!Pin)
        {
            return {};
        }
        Pins.Add(Pin);
    }
    while (!Pins.IsEmpty() && Pins.Num() < 64)
    {
        const FName NextName = DefinitionNode->GetNextAggregateName(
            Pins.Last()->GetFName());
        URigVMPin* NextPin = NextName.IsNone()
            ? nullptr : Node->FindRootPinByName(NextName);
        if (!NextPin)
        {
            break;
        }
        Pins.Add(NextPin);
    }
    return Pins;
}

TArray<URigVMPin*> GetRigVMAggregatePins(
    URigVMNode* Node,
    bool bInputPins)
{
    URigVMNode* DefinitionNode = RigVMAggregateDefinitionNode(Node);
    if (!HasRigVMAggregateState(Node) || !DefinitionNode)
    {
        return {};
    }
    if (DefinitionNode->IsInputAggregate() == bInputPins)
    {
        return GetEditableRigVMAggregatePins(Node);
    }

    const TArray<URigVMPin*> BasePins = bInputPins
        ? DefinitionNode->GetAggregateInputs()
        : DefinitionNode->GetAggregateOutputs();
    if (!Cast<URigVMAggregateNode>(Node))
    {
        return BasePins;
    }
    TArray<URigVMPin*> Pins;
    Pins.Reserve(BasePins.Num());
    for (const URigVMPin* BasePin : BasePins)
    {
        URigVMPin* Pin = BasePin
            ? Node->FindRootPinByName(BasePin->GetFName()) : nullptr;
        if (Pin)
        {
            Pins.Add(Pin);
        }
    }
    return Pins;
}

FString RigVMNodeStateString(const URigVMNode* Node)
{
    if (!Node)
    {
        return TEXT("Missing");
    }
    FString State = TEXT("Class=") + Node->GetClass()->GetPathName()
        + TEXT("|Position=") + RigVMNodePositionString(Node)
        + TEXT("|Size=") + RigVMNodeSizeString(Node)
        + TEXT("|Color=") + RigVMNodeColorString(Node);
    URigVMNode* MutableNode = const_cast<URigVMNode*>(Node);
    URigVMNode* AggregateDefinition =
        RigVMAggregateDefinitionNode(MutableNode);
    const bool bIsAggregate = HasRigVMAggregateState(MutableNode);
    State += TEXT("|Aggregate=")
        + FString(bIsAggregate ? TEXT("true") : TEXT("false"));
    if (bIsAggregate)
    {
        const TArray<URigVMPin*> AggregateInputs =
            GetRigVMAggregatePins(MutableNode, true);
        const TArray<URigVMPin*> AggregateOutputs =
            GetRigVMAggregatePins(MutableNode, false);
        const bool bInputAggregate = AggregateDefinition
            && AggregateDefinition->IsInputAggregate();
        const TArray<URigVMPin*>& EditablePins = bInputAggregate
            ? AggregateInputs : AggregateOutputs;
        const auto PinNamesString = [](const TArray<URigVMPin*>& Pins)
        {
            TArray<FString> Names;
            Names.Reserve(Pins.Num());
            for (const URigVMPin* Pin : Pins)
            {
                Names.Add(Pin ? Pin->GetName() : TEXT("Missing"));
            }
            return FString::Join(Names, TEXT(","));
        };
        const FName LastAggregateName = EditablePins.IsEmpty()
            || !EditablePins.Last()
                ? NAME_None : EditablePins.Last()->GetFName();
        const FName NextAggregateName = LastAggregateName.IsNone()
            ? NAME_None
            : AggregateDefinition->GetNextAggregateName(LastAggregateName);
        State += TEXT("|AggregateInput=")
            + FString(bInputAggregate ? TEXT("true") : TEXT("false"));
        State += TEXT("|AggregateInputs=")
            + PinNamesString(AggregateInputs);
        State += TEXT("|AggregateOutputs=")
            + PinNamesString(AggregateOutputs);
        State += TEXT("|AggregatePins=")
            + PinNamesString(EditablePins);
        State += FString::Printf(
            TEXT("|AggregatePinCount=%d|AggregateNext=%s"),
            EditablePins.Num(),
            *NextAggregateName.ToString());
    }
    if (const URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
    {
        URigVMUnitNode* MutableUnitNode =
            const_cast<URigVMUnitNode*>(UnitNode);
        State += TEXT("|Struct=")
            + (UnitNode->GetScriptStruct()
                ? UnitNode->GetScriptStruct()->GetPathName()
                : TEXT("None"));
        State += TEXT("|Method=") + UnitNode->GetMethodName().ToString();
        const bool bIsEvent = MutableUnitNode->IsEvent();
        State += TEXT("|Event=")
            + FString(bIsEvent ? TEXT("true") : TEXT("false"));
        if (bIsEvent)
        {
            State += TEXT("|EventName=")
                + MutableUnitNode->GetEventName().ToString();
            State += TEXT("|CanOnlyExistOnce=")
                + FString(MutableUnitNode->CanOnlyExistOnce()
                    ? TEXT("true") : TEXT("false"));
        }
    }
    if (URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(
            const_cast<URigVMNode*>(Node)))
    {
        State += TEXT("|TemplateNotation=")
            + TemplateNode->GetNotation().ToString();
        const bool bHasRegisteredTemplate =
            TemplateNode->GetTemplate() != nullptr;
        State += TEXT("|TemplateRegistered=")
            + FString(bHasRegisteredTemplate
                ? TEXT("true") : TEXT("false"));
        if (bHasRegisteredTemplate)
        {
            State += TEXT("|TemplateResolved=")
                + FString(TemplateNode->IsResolved()
                    ? TEXT("true") : TEXT("false"));
            State += TEXT("|TemplateFullyUnresolved=")
                + FString(TemplateNode->IsFullyUnresolved()
                    ? TEXT("true") : TEXT("false"));
            State += FString::Printf(
                TEXT("|TemplatePermutations=%d"),
                TemplateNode->GetResolvedPermutationIndices(false).Num());
        }
    }
    if (URigVMDispatchNode* DispatchNode = Cast<URigVMDispatchNode>(
            const_cast<URigVMNode*>(Node)))
    {
        const FRigVMDispatchFactory* Factory = DispatchNode->GetFactory();
        State += TEXT("|Dispatch=true|DispatchFactory=")
            + (Factory
                ? Factory->GetFactoryName().ToString()
                : TEXT("None"));
    }
    if (const URigVMVariableNode* VariableNode =
        Cast<URigVMVariableNode>(Node))
    {
        const UObject* CPPTypeObject = VariableNode->GetCPPTypeObject();
        State += TEXT("|Variable=true|VariableName=")
            + VariableNode->GetVariableName().ToString();
        State += TEXT("|CPPType=") + VariableNode->GetCPPType();
        State += TEXT("|CPPTypeObject=")
            + (CPPTypeObject ? CPPTypeObject->GetPathName() : TEXT("None"));
        State += TEXT("|Getter=")
            + FString(VariableNode->IsGetter()
                ? TEXT("true") : TEXT("false"));
        State += TEXT("|External=")
            + FString(VariableNode->IsExternalVariable()
                ? TEXT("true") : TEXT("false"));
        State += TEXT("|Local=")
            + FString(VariableNode->IsLocalVariable()
                ? TEXT("true") : TEXT("false"));
        State += TEXT("|Input=")
            + FString(VariableNode->IsInputVariable()
                ? TEXT("true") : TEXT("false"));
        State += TEXT("|Default=") + VariableNode->GetDefaultValue();
    }
    if (const URigVMFunctionReferenceNode* FunctionReference =
        Cast<URigVMFunctionReferenceNode>(Node))
    {
        const FRigVMGraphFunctionHeader& Header =
            FunctionReference->GetReferencedFunctionHeader();
        State += TEXT("|FunctionReference=true|FunctionName=")
            + Header.Name.ToString();
        State += TEXT("|FunctionMutable=")
            + FString(Header.IsMutable() ? TEXT("true") : TEXT("false"));
        State += TEXT("|FunctionHost=")
            + Header.LibraryPointer.HostObject.ToString();
        State += TEXT("|FunctionLibraryPath=")
            + Header.LibraryPointer.GetLibraryNodePath();
    }
    if (const URigVMCollapseNode* CollapseNode =
        Cast<URigVMCollapseNode>(Node))
    {
        const URigVMGraph* ContainedGraph =
            CollapseNode->GetContainedGraph();
        State += TEXT("|Collapse=true|ContainedGraph=")
            + RigVMGraphIdentifier(ContainedGraph);
        State += FString::Printf(
            TEXT("|ContainedNodeCount=%d|ContainedLinkCount=%d|GraphFunction=%s"),
            ContainedGraph ? ContainedGraph->GetNodes().Num() : 0,
            ContainedGraph ? ContainedGraph->GetLinks().Num() : 0,
            CollapseNode->IsGraphFunctionDefinition()
                ? TEXT("true") : TEXT("false"));
    }
    if (const URigVMCommentNode* CommentNode =
        Cast<URigVMCommentNode>(Node))
    {
        State += TEXT("|Comment=")
            + RigVMCommentPropertiesString(CommentNode);
    }
    if (const URigVMRerouteNode* RerouteNode =
        Cast<URigVMRerouteNode>(Node))
    {
        const URigVMPin* ValuePin = nullptr;
        for (const URigVMPin* Pin : RerouteNode->GetPins())
        {
            if (Pin && Pin->IsRootPin())
            {
                ValuePin = Pin;
                break;
            }
        }
        State += TEXT("|Reroute=true|RerouteType=")
            + (ValuePin ? ValuePin->GetCPPType() : TEXT("Missing"));
        State += TEXT("|RerouteTypeObject=")
            + (ValuePin && ValuePin->GetCPPTypeObject()
                ? ValuePin->GetCPPTypeObject()->GetPathName()
                : TEXT("None"));
        State += TEXT("|RerouteConstant=")
            + FString(ValuePin && ValuePin->IsDefinedAsConstant()
                ? TEXT("true") : TEXT("false"));
        State += TEXT("|RerouteWidget=")
            + (ValuePin
                ? ValuePin->GetCustomWidgetName().ToString()
                : TEXT("None"));
        State += TEXT("|RerouteDefault=")
            + (ValuePin ? ValuePin->GetDefaultValue() : TEXT("Missing"));
    }
    return State;
}

int32 CountRigVMLocalFunctionReferences(
    UControlRigEditorAsset* EditorAsset,
    const FName FunctionName)
{
    int32 Count = 0;
    if (!EditorAsset)
    {
        return Count;
    }
    for (const URigVMGraph* Model : EditorAsset->GetAllModels())
    {
        if (!Model)
        {
            continue;
        }
        for (const URigVMNode* Node : Model->GetNodes())
        {
            const URigVMFunctionReferenceNode* FunctionReference =
                Cast<URigVMFunctionReferenceNode>(Node);
            if (FunctionReference
                && FunctionReference->GetReferencedFunctionHeader().Name
                    == FunctionName)
            {
                ++Count;
            }
        }
    }
    return Count;
}

int32 CountRigVMFunctionReferences(
    UControlRigRuntimeAsset* RuntimeAsset,
    UControlRigEditorAsset* EditorAsset,
    URigVMLibraryNode* Function)
{
    if (!RuntimeAsset || !EditorAsset || !Function)
    {
        return 0;
    }
    const int32 LoadedReferenceCount =
        CountRigVMLocalFunctionReferences(
            EditorAsset, Function->GetFName());
    if (!EditorAsset->IsFunctionPublic(Function->GetFName()))
    {
        return LoadedReferenceCount;
    }
    const TScriptInterface<IRigVMGraphFunctionHost> FunctionHost =
        EditorAsset->GetRigVMGraphFunctionHost();
    IRigVMGraphFunctionHost* FunctionHostInterface =
        FunctionHost.GetInterface();
    URigVMBuildData* BuildData = URigVMBuildData::Get();
    if (!FunctionHostInterface || !BuildData)
    {
        return LoadedReferenceCount;
    }
    BuildData->InitializeIfNeeded();
    const FRigVMGraphFunctionHeader Header =
        Function->GetFunctionHeader(FunctionHostInterface);
    const FRigVMFunctionReferenceArray* References =
        Header.LibraryPointer.IsValid()
            ? BuildData->FindFunctionReferences(Header.LibraryPointer)
            : nullptr;
    return FMath::Max(
        LoadedReferenceCount,
        References ? References->Num() : 0);
}

FString RigVMLocalFunctionStateString(
    URigVMLibraryNode* Function,
    UControlRigRuntimeAsset* RuntimeAsset,
    UControlRigEditorAsset* EditorAsset)
{
    if (!Function)
    {
        return TEXT("Missing");
    }
    URigVMGraph* ContainedGraph = Function->GetContainedGraph();
    return TEXT("Class=") + Function->GetClass()->GetPathName()
        + TEXT("|Mutable=")
        + FString(Function->IsMutable() ? TEXT("true") : TEXT("false"))
        + TEXT("|Public=")
        + FString(EditorAsset && EditorAsset->IsFunctionPublic(
            Function->GetFName()) ? TEXT("true") : TEXT("false"))
        + TEXT("|Category=") + Function->GetNodeCategory()
        + TEXT("|Keywords=") + Function->GetNodeKeywords()
        + TEXT("|Description=") + Function->GetNodeDescription()
        + TEXT("|Position=") + RigVMNodePositionString(Function)
        + TEXT("|ContainedGraph=") + RigVMGraphIdentifier(ContainedGraph)
        + FString::Printf(
            TEXT("|NodeCount=%d|LinkCount=%d|LocalVariableCount=%d|ReferenceCount=%d"),
            ContainedGraph ? ContainedGraph->GetNodes().Num() : 0,
            ContainedGraph ? ContainedGraph->GetLinks().Num() : 0,
            ContainedGraph
                ? ContainedGraph->GetLocalVariables(false).Num() : 0,
            CountRigVMFunctionReferences(
                RuntimeAsset, EditorAsset, Function));
}

FString RigVMLinkStateString(
    const FString& SourcePinPath,
    const FString& TargetPinPath)
{
    return SourcePinPath + TEXT(" -> ") + TargetPinPath;
}

FString RigVMPinDirectionString(const ERigVMPinDirection Direction)
{
    switch (Direction)
    {
    case ERigVMPinDirection::Input: return TEXT("Input");
    case ERigVMPinDirection::Output: return TEXT("Output");
    case ERigVMPinDirection::IO: return TEXT("IO");
    case ERigVMPinDirection::Visible: return TEXT("Visible");
    case ERigVMPinDirection::Hidden: return TEXT("Hidden");
    default: return TEXT("Invalid");
    }
}

TOptional<ERigVMPinDirection> ParseRigVMFunctionPinDirection(
    const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("input"))
    {
        return ERigVMPinDirection::Input;
    }
    if (Value == TEXT("output"))
    {
        return ERigVMPinDirection::Output;
    }
    return {};
}

FString RigVMFunctionPinStateString(
    const URigVMLibraryNode* Function,
    const FName PinName)
{
    const URigVMPin* Pin = Function
        ? Function->FindRootPinByName(PinName) : nullptr;
    if (!Pin)
    {
        return TEXT("Missing");
    }
    const int32 Index = Function->GetPins().IndexOfByKey(Pin);
    return TEXT("Direction=")
        + RigVMPinDirectionString(Pin->GetDirection())
        + TEXT("|CPPType=") + Pin->GetCPPType()
        + TEXT("|CPPTypeObject=")
        + (Pin->GetCPPTypeObject()
            ? Pin->GetCPPTypeObject()->GetPathName() : TEXT("None"))
        + TEXT("|Default=") + Pin->GetDefaultValue()
        + FString::Printf(TEXT("|Index=%d"), Index);
}

FString RigVMArrayPinStateString(const URigVMPin* ArrayPin)
{
    if (!ArrayPin || !ArrayPin->IsDynamicArray())
    {
        return TEXT("Missing");
    }
    TArray<FString> Defaults;
    for (const URigVMPin* ElementPin : ArrayPin->GetSubPins())
    {
        Defaults.Add(ElementPin
            ? ElementPin->GetDefaultValue()
            : TEXT("Missing"));
    }
    return FString::Printf(TEXT("Size=%d|Defaults=%s"),
        ArrayPin->GetSubPins().Num(),
        *FString::Join(Defaults, TEXT(";")));
}

TArray<FString> RigVMStructDynamicArrayPins(const UScriptStruct* ScriptStruct)
{
    TArray<FString> Names;
    if (!ScriptStruct)
    {
        return Names;
    }
    for (TFieldIterator<FProperty> PropertyIt(ScriptStruct);
         PropertyIt; ++PropertyIt)
    {
        if (CastField<FArrayProperty>(*PropertyIt))
        {
            Names.AddUnique(PropertyIt->GetName());
        }
    }
    Names.Sort();
    return Names;
}

TArray<FString> RigVMStructEditableDynamicArrayPins(
    const UScriptStruct* ScriptStruct)
{
    TArray<FString> Names;
    if (!ScriptStruct)
    {
        return Names;
    }
    for (TFieldIterator<FProperty> PropertyIt(ScriptStruct);
         PropertyIt; ++PropertyIt)
    {
        FProperty* Property = *PropertyIt;
        if (CastField<FArrayProperty>(Property)
            && (Property->HasMetaData(TEXT("Input"))
                || Property->HasMetaData(TEXT("Visible"))
                || !Property->HasMetaData(TEXT("Output"))))
        {
            Names.AddUnique(Property->GetName());
        }
    }
    Names.Sort();
    return Names;
}

struct FRigVMStructAggregatePinNames
{
    TArray<FString> Inputs;
    TArray<FString> Outputs;

    bool IsAggregate() const
    {
        return (Inputs.Num() >= 2 && Outputs.Num() >= 1)
            || (Outputs.Num() >= 2 && Inputs.Num() >= 1);
    }
};

FRigVMStructAggregatePinNames RigVMStructAggregatePins(
    const UScriptStruct* ScriptStruct)
{
    FRigVMStructAggregatePinNames Names;
    if (!ScriptStruct)
    {
        return Names;
    }
    for (TFieldIterator<FProperty> PropertyIt(ScriptStruct);
         PropertyIt; ++PropertyIt)
    {
        FProperty* Property = *PropertyIt;
        if (!Property || !Property->HasMetaData(TEXT("Aggregate")))
        {
            continue;
        }
        if (Property->HasMetaData(TEXT("Output"))
            && !Property->HasMetaData(TEXT("Input")))
        {
            Names.Outputs.AddUnique(Property->GetName());
        }
        else
        {
            Names.Inputs.AddUnique(Property->GetName());
        }
    }
    Names.Inputs.Sort();
    Names.Outputs.Sort();
    return Names;
}

bool IsRegisteredControlRigUnitStruct(const UScriptStruct* ScriptStruct)
{
    if (!ScriptStruct
        || !ScriptStruct->IsChildOf(FRigVMStruct::StaticStruct()))
    {
        return false;
    }
    return URigVMController::GetRegisteredUnitStructs().Contains(
        const_cast<UScriptStruct*>(ScriptStruct));
}

struct FRegisteredRigVMUnitFunctionSpec
{
    UScriptStruct* ScriptStruct = nullptr;
    FName MethodName = NAME_None;
    FString FunctionName;
};

TArray<FRegisteredRigVMUnitFunctionSpec>
CollectRegisteredRigVMUnitFunctions(
    const TArray<UScriptStruct*>& RegisteredUnitStructs)
{
    TSet<UScriptStruct*> RegisteredUnitSet;
    for (UScriptStruct* ScriptStruct : RegisteredUnitStructs)
    {
        if (ScriptStruct
            && ScriptStruct->IsChildOf(FRigVMStruct::StaticStruct()))
        {
            RegisteredUnitSet.Add(ScriptStruct);
        }
    }

    TSet<FString> SeenPairs;
    TArray<FRegisteredRigVMUnitFunctionSpec> Specs;
    const TChunkedArray<FRigVMFunction>& RegisteredFunctions =
        FRigVMRegistry::Get().GetFunctions();
    for (const FRigVMFunction& Function : RegisteredFunctions)
    {
        UScriptStruct* ScriptStruct = Function.Struct;
        const FName MethodName = Function.GetMethodName();
        if (!RegisteredUnitSet.Contains(ScriptStruct)
            || MethodName.IsNone())
        {
            continue;
        }
        const FString PairKey = ScriptStruct->GetPathName()
            + TEXT("\x1f") + MethodName.ToString();
        if (SeenPairs.Contains(PairKey))
        {
            continue;
        }
        SeenPairs.Add(PairKey);
        FRegisteredRigVMUnitFunctionSpec& Spec =
            Specs.AddDefaulted_GetRef();
        Spec.ScriptStruct = ScriptStruct;
        Spec.MethodName = MethodName;
        Spec.FunctionName = Function.GetName();
    }
    Specs.Sort([](
        const FRegisteredRigVMUnitFunctionSpec& A,
        const FRegisteredRigVMUnitFunctionSpec& B)
    {
        const FString APath = A.ScriptStruct
            ? A.ScriptStruct->GetPathName() : FString();
        const FString BPath = B.ScriptStruct
            ? B.ScriptStruct->GetPathName() : FString();
        if (APath != BPath)
        {
            return APath < BPath;
        }
        return A.MethodName.LexicalLess(B.MethodName);
    });
    return Specs;
}

bool IsRegisteredControlRigUnitFunction(
    UScriptStruct* ScriptStruct,
    const FName MethodName)
{
    if (!IsRegisteredControlRigUnitStruct(ScriptStruct)
        || MethodName.IsNone())
    {
        return false;
    }
    const TChunkedArray<FRigVMFunction>& RegisteredFunctions =
        FRigVMRegistry::Get().GetFunctions();
    for (const FRigVMFunction& Function : RegisteredFunctions)
    {
        if (Function.Struct == ScriptStruct
            && Function.GetMethodName() == MethodName)
        {
            return true;
        }
    }
    return false;
}

struct FRegisteredRigVMEventSpec
{
    FName EventName = NAME_None;
    bool bCanOnlyExistOnce = false;
};

TOptional<FRegisteredRigVMEventSpec> GetRegisteredRigVMEventSpec(
    UScriptStruct* ScriptStruct)
{
    if (!IsRegisteredControlRigUnitStruct(ScriptStruct)
        || !ScriptStruct->IsChildOf(FRigVMStruct::StaticStruct()))
    {
        return {};
    }
    FStructOnScope Defaults(ScriptStruct);
    FRigVMStruct* RigVMStruct = reinterpret_cast<FRigVMStruct*>(
        Defaults.GetStructMemory());
    if (!RigVMStruct)
    {
        return {};
    }
    FRegisteredRigVMEventSpec Spec;
    Spec.EventName = RigVMStruct->GetEventName();
    Spec.bCanOnlyExistOnce = RigVMStruct->CanOnlyExistOnce();
    return Spec.EventName.IsNone()
        ? TOptional<FRegisteredRigVMEventSpec>()
        : TOptional<FRegisteredRigVMEventSpec>(Spec);
}

struct FControlRigGraphPreflightError
{
    FString Code;
    FString Message;
};

struct FResolvedControlRigFunctionDefinition
{
    UControlRigRuntimeAsset* RuntimeAsset = nullptr;
    UControlRigEditorAsset* EditorAsset = nullptr;
    URigVMLibraryNode* Function = nullptr;
    FString HostAssetPath;
    bool bExternal = false;
};

TOptional<FControlRigGraphPreflightError>
ResolveControlRigFunctionDefinition(
    const FThomasAnimationOperation& Operation,
    UControlRigRuntimeAsset* CurrentRuntimeAsset,
    UControlRigEditorAsset* CurrentEditorAsset,
    FResolvedControlRigFunctionDefinition& OutDefinition)
{
    OutDefinition = {};
    if (!CurrentRuntimeAsset || !CurrentEditorAsset)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_function_library_unavailable"), TEXT("None")};
    }
    UControlRigRuntimeAsset* HostRuntimeAsset = CurrentRuntimeAsset;
    UControlRigEditorAsset* HostEditorAsset = CurrentEditorAsset;
    const bool bExternal = !Operation.ReferencedAssetPath.IsEmpty();
    if (bExternal)
    {
        if (!IsAllowedPath(Operation.ReferencedAssetPath))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_external_function_asset"),
                Operation.ReferencedAssetPath};
        }
        HostRuntimeAsset = LoadObject<UControlRigRuntimeAsset>(
            nullptr, *ObjectPath(Operation.ReferencedAssetPath));
        HostEditorAsset = GetControlRigEditorAsset(HostRuntimeAsset);
        if (!HostRuntimeAsset || !HostEditorAsset
            || HostEditorAsset == CurrentEditorAsset)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_external_function_asset"),
                Operation.ReferencedAssetPath};
        }
        if (HostRuntimeAsset->GetOutermost()->IsDirty())
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_external_function_asset_dirty"),
                Operation.ReferencedAssetPath};
        }
    }
    URigVMFunctionLibrary* FunctionLibrary =
        HostEditorAsset->GetLocalFunctionLibrary();
    URigVMLibraryNode* Function = FunctionLibrary
        ? FunctionLibrary->FindFunction(
            FName(*Operation.RigVMFunctionName))
        : nullptr;
    if (!Function)
    {
        return FControlRigGraphPreflightError{
            bExternal
                ? TEXT("control_rig_external_function_not_found")
                : TEXT("invalid_control_rig_function_reference_node"),
            Operation.RigVMFunctionName};
    }
    if (bExternal
        && !HostEditorAsset->IsFunctionPublic(Function->GetFName()))
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_external_function_not_public"),
            Operation.RigVMFunctionName};
    }
    OutDefinition.RuntimeAsset = HostRuntimeAsset;
    OutDefinition.EditorAsset = HostEditorAsset;
    OutDefinition.Function = Function;
    OutDefinition.HostAssetPath =
        HostRuntimeAsset->GetOutermost()->GetName();
    OutDefinition.bExternal = bExternal;
    return {};
}

bool ControlRigFunctionReferenceMatches(
    const URigVMFunctionReferenceNode* Node,
    const FResolvedControlRigFunctionDefinition& Definition)
{
    if (!Node || !Definition.Function)
    {
        return false;
    }
    const FRigVMGraphFunctionHeader& Header =
        Node->GetReferencedFunctionHeader();
    return Header.Name == Definition.Function->GetFName()
        && NormalizePath(Header.LibraryPointer.HostObject.ToString())
            == Definition.HostAssetPath;
}

bool IsControlRigModelAction(const FString& Action)
{
    return Action == TEXT("add_control_rig_graph")
        || Action == TEXT("rename_control_rig_graph")
        || Action == TEXT("remove_control_rig_graph");
}

URigVMGraph* ResolveTopLevelControlRigModel(
    UControlRigEditorAsset* EditorAsset,
    const FString& GraphName)
{
    if (!EditorAsset || GraphName.IsEmpty())
    {
        return nullptr;
    }
    URigVMGraph* DefaultModel = EditorAsset->GetDefaultModel();
    URigVMFunctionLibrary* FunctionLibrary =
        EditorAsset->GetLocalFunctionLibrary();
    for (URigVMGraph* Model : EditorAsset->GetAllModels())
    {
        if (!Model || Model == DefaultModel || Model == FunctionLibrary)
        {
            continue;
        }
        if (Model->GetName() == GraphName
            || Model->GetGraphName() == GraphName
            || Model->GetNodePath() == GraphName)
        {
            return Model;
        }
    }
    return nullptr;
}

FString RequestedControlRigTopLevelGraphName(
    UControlRigEditorAsset* EditorAsset,
    const FString& RequestedName)
{
    const URigVMGraph* DefaultModel = EditorAsset
        ? EditorAsset->GetDefaultModel() : nullptr;
    return DefaultModel
        ? DefaultModel->GetGraphName() + TEXT(" ") + RequestedName
        : RequestedName;
}

FString RigVMTopLevelGraphStateString(const URigVMGraph* Model)
{
    if (!Model)
    {
        return TEXT("Missing");
    }
    TArray<FString> ContentParts;
    for (const URigVMNode* Node : Model->GetNodes())
    {
        if (Node)
        {
            ContentParts.Add(TEXT("Node=")
                + Node->GetName() + TEXT("|")
                + RigVMNodeStateString(Node));
        }
    }
    for (const URigVMLink* Link : Model->GetLinks())
    {
        if (Link && Link->GetSourcePin() && Link->GetTargetPin())
        {
            ContentParts.Add(TEXT("Link=")
                + RigVMLinkStateString(
                    Link->GetSourcePin()->GetPinPath(),
                    Link->GetTargetPin()->GetPinPath()));
        }
    }
    const TArray<FRigVMGraphVariableDescription> LocalVariables =
        Model->GetLocalVariables(false);
    for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
    {
        ContentParts.Add(TEXT("Local=")
            + RigVMLocalVariableStateString(
                LocalVariables[Index], Index));
    }
    ContentParts.Sort();
    const uint32 ContentHash = FCrc::StrCrc32(
        *FString::Join(ContentParts, TEXT("\n")));
    return FString::Printf(
        TEXT("Identifier=%s|ObjectName=%s|GraphName=%s|NodeCount=%d|LinkCount=%d|LocalVariableCount=%d|ContentHash=%08X"),
        *RigVMGraphIdentifier(Model),
        *Model->GetName(),
        *Model->GetGraphName(),
        Model->GetNodes().Num(),
        Model->GetLinks().Num(),
        LocalVariables.Num(),
        ContentHash);
}

FORCENOINLINE TOptional<FControlRigGraphPreflightError>
ValidateControlRigModelOperation(
    const FThomasAnimationOperation& Operation,
    UControlRigEditorAsset* EditorAsset)
{
    if (!EditorAsset)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_graph_unavailable"), FString()};
    }
    if (Operation.Action == TEXT("add_control_rig_graph"))
    {
        if (!IsSafeGraphName(Operation.Name)
            || !Operation.RigVMGraphName.IsEmpty())
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_graph"), Operation.Name};
        }
        const FString GeneratedGraphName =
            RequestedControlRigTopLevelGraphName(
                EditorAsset, Operation.Name);
        if (ResolveControlRigModel(EditorAsset, GeneratedGraphName))
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_graph_exists"), Operation.Name};
        }
        if (EditorAsset->GetAllModels().Num() >= 32)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_graph_limit"), TEXT("32")};
        }
        return {};
    }

    URigVMGraph* Model = ResolveTopLevelControlRigModel(
        EditorAsset, Operation.RigVMGraphName);
    if (!Model)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_graph_not_found"),
            Operation.RigVMGraphName};
    }
    const FString CurrentState = RigVMTopLevelGraphStateString(Model);
    if (Operation.ExpectedValue.IsEmpty()
        || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_graph_conflict"), CurrentState};
    }
    if (Operation.Action == TEXT("rename_control_rig_graph"))
    {
        if (!IsSafeGraphName(Operation.NewName))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_graph"), Operation.NewName};
        }
        const FString RequestedGraphName =
            RequestedControlRigTopLevelGraphName(
                EditorAsset, Operation.NewName);
        if (ResolveTopLevelControlRigModel(EditorAsset, Operation.NewName)
            || (RequestedGraphName != Operation.NewName
                && ResolveTopLevelControlRigModel(
                    EditorAsset, RequestedGraphName)))
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_graph_exists"), Operation.NewName};
        }
    }
    return {};
}

FORCENOINLINE TOptional<FControlRigGraphPreflightError>
ValidateBasicControlRigGraphCreation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    bool& bOutHandled)
{
    bOutHandled = true;
    if (Operation.Action == TEXT("add_control_rig_unit_node"))
    {
        UScriptStruct* UnitStruct = LoadObject<UScriptStruct>(
            nullptr, *Operation.ClassPath);
        if (!IsSafeGraphName(Operation.Name)
            || !IsSafeGraphName(Operation.RigVMMethodName)
            || Model->FindNodeByName(FName(*Operation.Name))
            || !IsRegisteredControlRigUnitStruct(UnitStruct)
            || !IsRegisteredControlRigUnitFunction(
                UnitStruct, FName(*Operation.RigVMMethodName))
            || GetRegisteredRigVMEventSpec(UnitStruct).IsSet()
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_unit_node"),
                Operation.ClassPath};
        }
        return {};
    }
    if (Operation.Action == TEXT("add_control_rig_template_node"))
    {
        const TArray<FString> RegisteredTemplates =
            URigVMController::GetRegisteredTemplates();
        if (!IsSafeGraphName(Operation.Name)
            || Model->FindNodeByName(FName(*Operation.Name))
            || Operation.RigVMTemplateNotation.IsEmpty()
            || Operation.RigVMTemplateNotation.Len() > 1000
            || Operation.RigVMTemplateNotation.Contains(TEXT("\r"))
            || Operation.RigVMTemplateNotation.Contains(TEXT("\n"))
            || !RegisteredTemplates.Contains(
                Operation.RigVMTemplateNotation)
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_template_node"),
                Operation.RigVMTemplateNotation};
        }
        return {};
    }
    if (Operation.Action == TEXT("add_control_rig_comment_node"))
    {
        const FVector2D& Size = Operation.RigVMNodeSize;
        const FLinearColor& Color = Operation.Color;
        const bool bValidSize = FMath::IsFinite(Size.X)
            && FMath::IsFinite(Size.Y)
            && Size.X >= 1.0 && Size.X <= 100000.0
            && Size.Y >= 1.0 && Size.Y <= 100000.0;
        const bool bValidColor = FMath::IsFinite(Color.R)
            && FMath::IsFinite(Color.G)
            && FMath::IsFinite(Color.B)
            && FMath::IsFinite(Color.A)
            && Color.R >= 0.0f && Color.R <= 1.0f
            && Color.G >= 0.0f && Color.G <= 1.0f
            && Color.B >= 0.0f && Color.B <= 1.0f
            && Color.A >= 0.0f && Color.A <= 1.0f;
        if (!IsSafeGraphName(Operation.Name)
            || Model->FindNodeByName(FName(*Operation.Name))
            || Operation.RigVMCommentText.Len() > 1000
            || !bValidSize || !bValidColor
            || Operation.RigVMCommentFontSize < 6
            || Operation.RigVMCommentFontSize > 96
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_comment_node"),
                Operation.Name};
        }
        return {};
    }
    bOutHandled = false;
    return {};
}

bool IsR22ControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("add_control_rig_event_node")
        || Action == TEXT("remove_control_rig_event_node")
        || Action == TEXT("add_control_rig_member_variable")
        || Action == TEXT("remove_control_rig_member_variable")
        || Action == TEXT("add_control_rig_variable_node")
        || Action == TEXT("remove_control_rig_variable_node");
}

bool IsControlRigGraphAction(const FString& Action)
{
    static const TSet<FString> Actions = {
        TEXT("add_control_rig_unit_node"),
        TEXT("remove_control_rig_unit_node"),
        TEXT("add_control_rig_event_node"),
        TEXT("remove_control_rig_event_node"),
        TEXT("add_control_rig_member_variable"),
        TEXT("remove_control_rig_member_variable"),
        TEXT("add_control_rig_variable_node"),
        TEXT("remove_control_rig_variable_node"),
        TEXT("add_control_rig_function"),
        TEXT("remove_control_rig_function"),
        TEXT("add_control_rig_function_reference_node"),
        TEXT("remove_control_rig_function_reference_node"),
        TEXT("add_control_rig_local_variable"),
        TEXT("remove_control_rig_local_variable"),
        TEXT("rename_control_rig_local_variable"),
        TEXT("set_default_control_rig_local_variable"),
        TEXT("set_type_control_rig_local_variable"),
        TEXT("set_index_control_rig_local_variable"),
        TEXT("add_control_rig_function_pin"),
        TEXT("remove_control_rig_function_pin"),
        TEXT("rename_control_rig_function_pin"),
        TEXT("set_type_control_rig_function_pin"),
        TEXT("set_index_control_rig_function_pin"),
        TEXT("set_public_control_rig_function"),
        TEXT("set_mutable_control_rig_function"),
        TEXT("set_category_control_rig_function"),
        TEXT("set_keywords_control_rig_function"),
        TEXT("set_description_control_rig_function"),
        TEXT("add_control_rig_template_node"),
        TEXT("remove_control_rig_template_node"),
        TEXT("resolve_control_rig_wildcard_pin"),
        TEXT("unresolve_control_rig_template_node"),
        TEXT("add_control_rig_comment_node"),
        TEXT("rename_control_rig_comment_node"),
        TEXT("set_control_rig_comment"),
        TEXT("remove_control_rig_comment_node"),
        TEXT("set_control_rig_node_position"),
        TEXT("set_control_rig_node_size"),
        TEXT("set_control_rig_node_color"),
        TEXT("set_control_rig_pin_default"),
        TEXT("set_control_rig_array_pin_size"),
        TEXT("add_control_rig_array_pin"),
        TEXT("duplicate_control_rig_array_pin"),
        TEXT("remove_control_rig_array_pin"),
        TEXT("add_control_rig_aggregate_pin"),
        TEXT("remove_control_rig_aggregate_pin"),
        TEXT("add_control_rig_link"),
        TEXT("remove_control_rig_link"),
        TEXT("add_control_rig_free_reroute_node"),
        TEXT("add_control_rig_reroute_node_on_link"),
        TEXT("remove_control_rig_reroute_node"),
        TEXT("set_control_rig_template_pin_type"),
        TEXT("collapse_control_rig_nodes"),
        TEXT("expand_control_rig_library_node")
    };
    return Actions.Contains(Action);
}

TOptional<FControlRigGraphPreflightError> ValidateR22ControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    UControlRigEditorAsset* EditorAsset)
{
    const FString& Action = Operation.Action;
    if (Action == TEXT("add_control_rig_event_node"))
    {
        UScriptStruct* EventStruct = LoadObject<UScriptStruct>(
            nullptr, *Operation.ClassPath);
        const TOptional<FRegisteredRigVMEventSpec> EventSpec =
            GetRegisteredRigVMEventSpec(EventStruct);
        if (!IsSafeGraphName(Operation.Name)
            || Model->FindNodeByName(FName(*Operation.Name))
            || !EventSpec.IsSet()
            || !IsSafeGraphName(Operation.RigVMMethodName)
            || !IsRegisteredControlRigUnitFunction(
                EventStruct, FName(*Operation.RigVMMethodName))
            || Model->GetEventNames().Contains(EventSpec->EventName)
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_event_node"),
                Operation.ClassPath};
        }
        return {};
    }
    if (Action == TEXT("add_control_rig_variable_node"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType, Operation.bRigVMTypeArray);
        FRigVMGraphVariableDescription MemberVariable;
        FRigVMGraphVariableDescription LocalVariable;
        const bool bHasCompatibleMemberVariable =
            FindRigVMMemberVariable(
                EditorAsset,
                FName(*Operation.RigVMVariableName),
                MemberVariable)
            && TypeSpec.IsSet()
            && MemberVariable.CPPType == TypeSpec->CPPType
            && (!TypeSpec->CPPTypeObject
                || MemberVariable.CPPTypeObject
                    == TypeSpec->CPPTypeObject)
            && MemberVariable.DefaultValue
                == Operation.RigVMPinDefaultValue;
        const bool bHasCompatibleLocalVariable =
            FindRigVMLocalVariable(
                Model,
                FName(*Operation.RigVMVariableName),
                LocalVariable)
            && TypeSpec.IsSet()
            && LocalVariable.CPPType == TypeSpec->CPPType
            && (!TypeSpec->CPPTypeObject
                || LocalVariable.CPPTypeObject
                    == TypeSpec->CPPTypeObject)
            && LocalVariable.DefaultValue
                == Operation.RigVMPinDefaultValue;
        bool bExistingVariableCompatible = true;
        for (const URigVMNode* ExistingNode : Model->GetNodes())
        {
            const URigVMVariableNode* VariableNode =
                Cast<URigVMVariableNode>(ExistingNode);
            if (VariableNode
                && VariableNode->GetVariableName()
                    == FName(*Operation.RigVMVariableName)
                && (!TypeSpec.IsSet()
                    || VariableNode->GetCPPType() != TypeSpec->CPPType
                    || (TypeSpec->CPPTypeObject
                        && VariableNode->GetCPPTypeObject()
                            != TypeSpec->CPPTypeObject)))
            {
                bExistingVariableCompatible = false;
                break;
            }
        }
        if (!IsSafeGraphName(Operation.Name)
            || !IsSafeGraphName(Operation.RigVMVariableName)
            || Model->FindNodeByName(FName(*Operation.Name))
            || !TypeSpec.IsSet()
            || (!bHasCompatibleMemberVariable
                && !bHasCompatibleLocalVariable)
            || !bExistingVariableCompatible
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n"))
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_variable_node"),
                Operation.RigVMVariableName};
        }
        return {};
    }
    if (Action == TEXT("add_control_rig_member_variable"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType, Operation.bRigVMTypeArray);
        FRigVMGraphVariableDescription ExistingVariable;
        if (!IsSafeGraphName(Operation.RigVMVariableName)
            || !TypeSpec.IsSet()
            || FindRigVMMemberVariable(
                EditorAsset,
                FName(*Operation.RigVMVariableName),
                ExistingVariable)
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_member_variable"),
                Operation.RigVMVariableName};
        }
        return {};
    }
    if (Action == TEXT("remove_control_rig_member_variable"))
    {
        FRigVMGraphVariableDescription MemberVariable;
        const bool bFound = FindRigVMMemberVariable(
            EditorAsset,
            FName(*Operation.RigVMVariableName),
            MemberVariable);
        const FString CurrentState = bFound
            ? RigVMMemberVariableStateString(MemberVariable)
            : TEXT("Missing");
        bool bReferenced = false;
        for (const URigVMGraph* CandidateModel :
             CollectControlRigModels(EditorAsset))
        {
            if (!CandidateModel)
            {
                continue;
            }
            for (const URigVMNode* ExistingNode : CandidateModel->GetNodes())
            {
                const URigVMVariableNode* VariableNode =
                    Cast<URigVMVariableNode>(ExistingNode);
                if (VariableNode
                    && VariableNode->GetVariableName()
                        == FName(*Operation.RigVMVariableName))
                {
                    bReferenced = true;
                    break;
                }
            }
            if (bReferenced)
            {
                break;
            }
        }
        if (!bFound || bReferenced
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_member_variable_conflict"),
                CurrentState};
        }
        return {};
    }
    if (Action == TEXT("remove_control_rig_event_node"))
    {
        URigVMUnitNode* Node = Cast<URigVMUnitNode>(
            Model->FindNodeByName(FName(*Operation.Name)));
        const FString CurrentState = RigVMNodeStateString(Node);
        if (!Node || !Node->IsEvent()
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_event_node_conflict"), CurrentState};
        }
        return {};
    }
    URigVMVariableNode* Node = Cast<URigVMVariableNode>(
        Model->FindNodeByName(FName(*Operation.Name)));
    const FString CurrentState = RigVMNodeStateString(Node);
    if (!Node || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_variable_node_conflict"), CurrentState};
    }
    return {};
}

bool ApplyR22ControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    URigVMController* Controller,
    UControlRigEditorAsset* EditorAsset)
{
    const FString& Action = Operation.Action;
    if (Action == TEXT("remove_control_rig_event_node")
        || Action == TEXT("remove_control_rig_variable_node"))
    {
        return Controller->RemoveNodeByName(
                FName(*Operation.Name), true, false)
            && !Model->FindNodeByName(FName(*Operation.Name));
    }
    if (Action == TEXT("remove_control_rig_member_variable"))
    {
        const FName VariableName(*Operation.RigVMVariableName);
        FRigVMGraphVariableDescription ExistingVariable;
        return EditorAsset->RemoveMemberVariable(VariableName)
            && !FindRigVMMemberVariable(
                EditorAsset, VariableName, ExistingVariable);
    }
    if (Action == TEXT("add_control_rig_member_variable"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType, Operation.bRigVMTypeArray);
        if (!TypeSpec.IsSet())
        {
            return false;
        }
        const FName AddedName = EditorAsset->AddMemberVariable(
            FName(*Operation.RigVMVariableName),
            TypeSpec->CPPType,
            true,
            false,
            Operation.RigVMPinDefaultValue);
        FRigVMGraphVariableDescription Readback;
        return AddedName == FName(*Operation.RigVMVariableName)
            && FindRigVMMemberVariable(
                EditorAsset,
                FName(*Operation.RigVMVariableName),
                Readback)
            && Readback.Name == FName(*Operation.RigVMVariableName)
            && Readback.CPPType == TypeSpec->CPPType
            && (!TypeSpec->CPPTypeObject
                || Readback.CPPTypeObject == TypeSpec->CPPTypeObject)
            && Readback.DefaultValue
                == Operation.RigVMPinDefaultValue;
    }
    if (Action == TEXT("add_control_rig_event_node"))
    {
        UScriptStruct* EventStruct = LoadObject<UScriptStruct>(
            nullptr, *Operation.ClassPath);
        const TOptional<FRegisteredRigVMEventSpec> EventSpec =
            GetRegisteredRigVMEventSpec(EventStruct);
        URigVMUnitNode* AddedNode = EventStruct && EventSpec.IsSet()
            ? Controller->AddUnitNode(
                EventStruct,
                FName(*Operation.RigVMMethodName),
                FVector2D(Operation.PositionX, Operation.PositionY),
                Operation.Name,
                true,
                false)
            : nullptr;
        return AddedNode && EventSpec.IsSet()
            && AddedNode->GetName() == Operation.Name
            && AddedNode->GetScriptStruct() == EventStruct
            && AddedNode->GetMethodName()
                == FName(*Operation.RigVMMethodName)
            && AddedNode->IsEvent()
            && AddedNode->GetEventName() == EventSpec->EventName
            && RigVMNodePositionString(AddedNode) == FString::Printf(
                TEXT("X=%.6f,Y=%.6f"),
                static_cast<double>(Operation.PositionX),
                static_cast<double>(Operation.PositionY));
    }
    const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
        ResolveBoundedRigVMType(
            Operation.RigVMType, Operation.bRigVMTypeArray);
    URigVMVariableNode* AddedNode = TypeSpec.IsSet()
        ? Controller->AddVariableNode(
            FName(*Operation.RigVMVariableName),
            TypeSpec->CPPType,
            TypeSpec->CPPTypeObject,
            Operation.bRigVMVariableGetter,
            Operation.RigVMPinDefaultValue,
            FVector2D(Operation.PositionX, Operation.PositionY),
            Operation.Name,
            true,
            false)
        : nullptr;
    return AddedNode && TypeSpec.IsSet()
        && AddedNode->GetName() == Operation.Name
        && AddedNode->GetVariableName()
            == FName(*Operation.RigVMVariableName)
        && AddedNode->GetCPPType() == TypeSpec->CPPType
        && (!TypeSpec->CPPTypeObject
            || AddedNode->GetCPPTypeObject() == TypeSpec->CPPTypeObject)
        && AddedNode->IsGetter() == Operation.bRigVMVariableGetter
        && AddedNode->GetDefaultValue()
            == Operation.RigVMPinDefaultValue
        && RigVMNodePositionString(AddedNode) == FString::Printf(
            TEXT("X=%.6f,Y=%.6f"),
            static_cast<double>(Operation.PositionX),
            static_cast<double>(Operation.PositionY));
}

bool IsR25ControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("set_index_control_rig_function_pin")
        || Action == TEXT("set_public_control_rig_function")
        || Action == TEXT("set_mutable_control_rig_function")
        || Action == TEXT("set_category_control_rig_function")
        || Action == TEXT("set_keywords_control_rig_function")
        || Action == TEXT("set_description_control_rig_function");
}

void NormalizeR25ControlRigOperation(FThomasAnimationOperation& Operation)
{
    Operation.RigVMFunctionCategory =
        Operation.RigVMFunctionCategory.TrimStartAndEnd();
    Operation.RigVMFunctionKeywords =
        Operation.RigVMFunctionKeywords.TrimStartAndEnd();
    Operation.RigVMFunctionDescription =
        Operation.RigVMFunctionDescription.TrimStartAndEnd();
    Operation.RigVMPinDirection =
        Operation.RigVMPinDirection.TrimStartAndEnd().ToLower();
}

bool IsR26ControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("collapse_control_rig_nodes")
        || Action == TEXT("expand_control_rig_library_node");
}

void NormalizeR26ControlRigOperation(FThomasAnimationOperation& Operation)
{
    for (FString& NodeName : Operation.RigVMNodeNames)
    {
        NodeName = NodeName.TrimStartAndEnd();
    }
}

bool IsR27ControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("add_control_rig_free_reroute_node")
        || Action == TEXT("set_control_rig_template_pin_type");
}

bool IsR31ControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("add_control_rig_aggregate_pin")
        || Action == TEXT("remove_control_rig_aggregate_pin");
}

TOptional<FControlRigGraphPreflightError> ValidateR31ControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model)
{
    URigVMNode* Node = Model
        ? Model->FindNodeByName(FName(*Operation.Name)) : nullptr;
    const FString CurrentState = RigVMNodeStateString(Node);
    const TArray<URigVMPin*> AggregatePins =
        GetEditableRigVMAggregatePins(Node);
    if (!Node || !IsSafeGraphName(Operation.Name)
        || !HasRigVMAggregateState(Node)
        || AggregatePins.Num() < 2
        || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_aggregate_node_conflict"), CurrentState};
    }

    if (Operation.Action == TEXT("add_control_rig_aggregate_pin"))
    {
        URigVMNode* DefinitionNode = RigVMAggregateDefinitionNode(Node);
        const URigVMPin* LastPin = AggregatePins.Last();
        const FName NextPinName = LastPin && DefinitionNode
            ? DefinitionNode->GetNextAggregateName(LastPin->GetFName())
            : NAME_None;
        const FString RequestedPinName = Operation.NewName.IsEmpty()
            ? NextPinName.ToString() : Operation.NewName;
        if (AggregatePins.Num() >= 64
            || NextPinName.IsNone()
            || RequestedPinName != NextPinName.ToString()
            || !IsSafeGraphName(RequestedPinName)
            || Node->FindRootPinByName(NextPinName)
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_aggregate_pin"),
                RequestedPinName};
        }
        return {};
    }

    URigVMPin* Pin = Model->FindPin(Operation.RigVMPinPath);
    const int32 PinIndex = AggregatePins.IndexOfByKey(Pin);
    if (!Pin || Pin->GetTypedOuter<URigVMNode>() != Node
        || Operation.RigVMPinPath.Len() > 512
        || PinIndex < 2
        || !Pin->GetSourceLinks(true).IsEmpty()
        || !Pin->GetTargetLinks(true).IsEmpty())
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_aggregate_pin_conflict"), CurrentState};
    }
    return {};
}

void NormalizeR27ControlRigOperation(FThomasAnimationOperation& Operation)
{
    Operation.RigVMCustomWidgetName =
        Operation.RigVMCustomWidgetName.TrimStartAndEnd();
}

TOptional<FControlRigGraphPreflightError> ValidateR27ControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model)
{
    if (!Model)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_graph_unavailable"), TEXT("None")};
    }
    if (Operation.Action == TEXT("add_control_rig_free_reroute_node"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType,
                Operation.bRigVMTypeArray);
        const bool bHasSource =
            !Operation.RigVMSourcePinPath.IsEmpty();
        const bool bHasTarget =
            !Operation.RigVMTargetPinPath.IsEmpty();
        URigVMPin* AnchorPin = bHasSource != bHasTarget
            ? Model->FindPin(bHasSource
                ? Operation.RigVMSourcePinPath
                : Operation.RigVMTargetPinPath)
            : nullptr;
        const FString CurrentAnchorState =
            RigVMPinTypeStateString(AnchorPin);
        const bool bValidDirection = AnchorPin
            && (bHasSource
                ? AnchorPin->GetDirection() == ERigVMPinDirection::Output
                    || AnchorPin->GetDirection() == ERigVMPinDirection::IO
                : AnchorPin->GetDirection() == ERigVMPinDirection::Input
                    || AnchorPin->GetDirection() == ERigVMPinDirection::IO);
        if (!IsSafeGraphName(Operation.Name)
            || Model->FindNodeByName(FName(*Operation.Name))
            || !TypeSpec.IsSet()
            || !AnchorPin || !AnchorPin->IsRootPin()
            || IsRigVMWildcardPin(AnchorPin)
            || !bValidDirection
            || AnchorPin->GetCPPType() != TypeSpec->CPPType
            || AnchorPin->GetCPPTypeObject()
                != TypeSpec->CPPTypeObject
            || (!bHasSource
                && !AnchorPin->GetSourceLinks(true).IsEmpty())
            || (bHasSource && Operation.bRigVMRerouteConstant)
            || Operation.ExpectedValue != CurrentAnchorState
            || Operation.RigVMSourcePinPath.Len() > 512
            || Operation.RigVMTargetPinPath.Len() > 512
            || (!Operation.RigVMCustomWidgetName.IsEmpty()
                && !IsSafeGraphName(Operation.RigVMCustomWidgetName))
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n"))
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_free_reroute_node"),
                Operation.Name};
        }
        return {};
    }

    URigVMPin* Pin = Model->FindPin(Operation.RigVMPinPath);
    URigVMTemplateNode* TemplateNode = Pin
        ? Cast<URigVMTemplateNode>(Pin->GetTypedOuter<URigVMNode>())
        : nullptr;
    const FRigVMTemplate* Template = TemplateNode
        ? TemplateNode->GetTemplate() : nullptr;
    const FString CurrentState = RigVMPinTypeStateString(Pin);
    const bool bArray = Pin
        && (Pin->IsDynamicArray() || Pin->IsFixedSizeArray());
    const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
        ResolveBoundedRigVMType(Operation.RigVMType, bArray);
    bool bTemplateSupportsType = false;
    if (Pin && Pin->IsRootPin() && TypeSpec.IsSet() && Template)
    {
        const TRigVMTypeIndex TypeIndex =
            FRigVMRegistry::Get().GetTypeIndexFromCPPType(
                TypeSpec->CPPType);
        bTemplateSupportsType = Template->ArgumentSupportsTypeIndex(
            Pin->GetFName(), TypeIndex, nullptr);
    }
    bool bNodeHasLinks = false;
    if (TemplateNode)
    {
        for (const URigVMPin* NodePin : TemplateNode->GetPins())
        {
            if (NodePin && NodePin->IsLinked(true))
            {
                bNodeHasLinks = true;
                break;
            }
        }
    }
    if (!Pin || !Pin->IsRootPin() || IsRigVMWildcardPin(Pin)
        || !TemplateNode || !Template
        || TemplateNode->IsFullyUnresolved()
        || TemplateNode->GetResolvedPermutationIndices(false).Num() != 1
        || !TypeSpec.IsSet() || !bTemplateSupportsType
        || bNodeHasLinks || Operation.RigVMPinPath.Len() > 512
        || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_template_pin_type_conflict"),
            CurrentState};
    }
    if (Pin->GetCPPType() == TypeSpec->CPPType
        && Pin->GetCPPTypeObject() == TypeSpec->CPPTypeObject)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_template_pin_type_noop"),
            CurrentState};
    }
    return {};
}

TArray<URigVMNode*> GetRigVMCollapseContentNodes(
    URigVMCollapseNode* CollapseNode)
{
    TArray<URigVMNode*> Nodes;
    URigVMGraph* ContainedGraph = CollapseNode
        ? CollapseNode->GetContainedGraph() : nullptr;
    if (!ContainedGraph)
    {
        return Nodes;
    }
    URigVMNode* EntryNode = ContainedGraph->GetEntryNode();
    URigVMNode* ReturnNode = ContainedGraph->GetReturnNode();
    for (URigVMNode* Node : ContainedGraph->GetNodes())
    {
        if (Node && Node != EntryNode && Node != ReturnNode)
        {
            Nodes.Add(Node);
        }
    }
    return Nodes;
}

TOptional<FControlRigGraphPreflightError> ValidateR26ControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    UControlRigEditorAsset* EditorAsset)
{
    URigVMFunctionLibrary* FunctionLibrary = EditorAsset
        ? EditorAsset->GetLocalFunctionLibrary() : nullptr;
    if (!Model || Model == FunctionLibrary)
    {
        return FControlRigGraphPreflightError{
            TEXT("invalid_control_rig_collapse_graph"),
            RigVMGraphIdentifier(Model)};
    }
    if (!IsSafeGraphName(Operation.Name)
        || Operation.RigVMNodeNames.Num() < 2
        || Operation.RigVMNodeNames.Num() > 32
        || Operation.RigVMExpectedNodeStates.Num()
            != Operation.RigVMNodeNames.Num())
    {
        return FControlRigGraphPreflightError{
            TEXT("invalid_control_rig_collapse_nodes"), Operation.Name};
    }

    if (Operation.Action == TEXT("collapse_control_rig_nodes"))
    {
        if (Model->FindNodeByName(FName(*Operation.Name)))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_collapse_nodes"),
                Operation.Name};
        }
        TSet<FName> UniqueNames;
        for (int32 Index = 0;
             Index < Operation.RigVMNodeNames.Num(); ++Index)
        {
            const FString& NodeName = Operation.RigVMNodeNames[Index];
            URigVMNode* Node = IsSafeGraphName(NodeName)
                ? Model->FindNodeByName(FName(*NodeName)) : nullptr;
            const FString CurrentState = RigVMNodeStateString(Node);
            if (!Node || Node == Model->GetEntryNode()
                || Node == Model->GetReturnNode()
                || UniqueNames.Contains(Node->GetFName())
                || Operation.RigVMExpectedNodeStates[Index]
                    != CurrentState)
            {
                return FControlRigGraphPreflightError{
                    TEXT("control_rig_collapse_node_conflict"),
                    NodeName + TEXT("=") + CurrentState};
            }
            UniqueNames.Add(Node->GetFName());
        }
        return {};
    }

    URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(
        Model->FindNodeByName(FName(*Operation.Name)));
    const FString CurrentState = RigVMNodeStateString(CollapseNode);
    if (!CollapseNode || CollapseNode->IsGraphFunctionDefinition()
        || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_collapse_node_conflict"), CurrentState};
    }
    const TArray<URigVMNode*> ContentNodes =
        GetRigVMCollapseContentNodes(CollapseNode);
    if (ContentNodes.Num() != Operation.RigVMNodeNames.Num())
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_collapse_content_conflict"), CurrentState};
    }
    TMap<FName, URigVMNode*> ContentByName;
    for (URigVMNode* Node : ContentNodes)
    {
        if (!Node || ContentByName.Contains(Node->GetFName()))
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_collapse_content_conflict"),
                CurrentState};
        }
        ContentByName.Add(Node->GetFName(), Node);
    }
    TSet<FName> UniqueNames;
    for (int32 Index = 0;
         Index < Operation.RigVMNodeNames.Num(); ++Index)
    {
        const FString& NodeName = Operation.RigVMNodeNames[Index];
        const FName NodeFName(*NodeName);
        URigVMNode* const* Node = IsSafeGraphName(NodeName)
            ? ContentByName.Find(NodeFName) : nullptr;
        const FString ContentState = Node && *Node
            ? RigVMNodeStateString(*Node) : TEXT("Missing");
        if (!Node || !*Node || UniqueNames.Contains(NodeFName)
            || Model->FindNodeByName(NodeFName)
            || Operation.RigVMExpectedNodeStates[Index]
                != ContentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_collapse_content_conflict"),
                NodeName + TEXT("=") + ContentState};
        }
        UniqueNames.Add(NodeFName);
    }
    return {};
}

bool IsAdvancedControlRigGraphAction(const FString& Action)
{
    return Action == TEXT("add_control_rig_function")
        || Action == TEXT("remove_control_rig_function")
        || Action == TEXT("add_control_rig_function_reference_node")
        || Action == TEXT("remove_control_rig_function_reference_node")
        || Action == TEXT("add_control_rig_local_variable")
        || Action == TEXT("remove_control_rig_local_variable")
        || Action == TEXT("rename_control_rig_local_variable")
        || Action == TEXT("set_default_control_rig_local_variable")
        || Action == TEXT("set_type_control_rig_local_variable")
        || Action == TEXT("set_index_control_rig_local_variable")
        || Action == TEXT("add_control_rig_function_pin")
        || Action == TEXT("remove_control_rig_function_pin")
        || Action == TEXT("rename_control_rig_function_pin")
        || Action == TEXT("set_type_control_rig_function_pin")
        || IsR25ControlRigGraphAction(Action)
        || IsR26ControlRigGraphAction(Action)
        || IsR27ControlRigGraphAction(Action)
        || IsR31ControlRigGraphAction(Action);
}

TOptional<FControlRigGraphPreflightError> ValidateAdvancedControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    UControlRigRuntimeAsset* RuntimeAsset,
    UControlRigEditorAsset* EditorAsset)
{
    if (IsR27ControlRigGraphAction(Operation.Action))
    {
        return ValidateR27ControlRigGraphOperation(Operation, Model);
    }
    if (IsR31ControlRigGraphAction(Operation.Action))
    {
        return ValidateR31ControlRigGraphOperation(Operation, Model);
    }
    if (IsR26ControlRigGraphAction(Operation.Action))
    {
        return ValidateR26ControlRigGraphOperation(
            Operation, Model, EditorAsset);
    }
    URigVMFunctionLibrary* FunctionLibrary = EditorAsset
        ? EditorAsset->GetLocalFunctionLibrary() : nullptr;
    if (!FunctionLibrary)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_function_library_unavailable"), TEXT("None")};
    }

    const FString& Action = Operation.Action;
    if (Action == TEXT("add_control_rig_function")
        || Action == TEXT("remove_control_rig_function")
        || Action == TEXT("add_control_rig_function_reference_node")
        || Action == TEXT("remove_control_rig_function_reference_node")
        || Action == TEXT("set_public_control_rig_function")
        || Action == TEXT("set_mutable_control_rig_function")
        || Action == TEXT("set_category_control_rig_function")
        || Action == TEXT("set_keywords_control_rig_function")
        || Action == TEXT("set_description_control_rig_function")
        || Action.EndsWith(TEXT("control_rig_function_pin")))
    {
        if (!IsSafeGraphName(Operation.RigVMFunctionName))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function"),
                Operation.RigVMFunctionName};
        }
    }

    const FName FunctionName(*Operation.RigVMFunctionName);
    URigVMLibraryNode* Function =
        FunctionLibrary->FindFunction(FunctionName);
    if (Action == TEXT("add_control_rig_function"))
    {
        if (Function
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function"),
                Operation.RigVMFunctionName};
        }
        return {};
    }
    if (Action == TEXT("remove_control_rig_function"))
    {
        const FString CurrentState =
            RigVMLocalFunctionStateString(
                Function, RuntimeAsset, EditorAsset);
        if (!Function
            || CountRigVMFunctionReferences(
                RuntimeAsset, EditorAsset, Function) != 0
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_function_conflict"), CurrentState};
        }
        return {};
    }
    if (Action == TEXT("add_control_rig_function_reference_node"))
    {
        FResolvedControlRigFunctionDefinition Definition;
        const TOptional<FControlRigGraphPreflightError> ResolveError =
            ResolveControlRigFunctionDefinition(
                Operation,
                RuntimeAsset,
                EditorAsset,
                Definition);
        if (ResolveError.IsSet())
        {
            return ResolveError;
        }
        if (!IsSafeGraphName(Operation.Name)
            || Model == FunctionLibrary
            || (!Definition.bExternal
                && Model == Definition.Function->GetContainedGraph())
            || Model->FindNodeByName(FName(*Operation.Name))
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_reference_node"),
                Operation.RigVMFunctionName};
        }
        return {};
    }
    if (Action == TEXT("remove_control_rig_function_reference_node"))
    {
        URigVMFunctionReferenceNode* Node =
            Cast<URigVMFunctionReferenceNode>(
                Model->FindNodeByName(FName(*Operation.Name)));
        const FString CurrentState = RigVMNodeStateString(Node);
        const FString ExpectedHostPath =
            Operation.ReferencedAssetPath.IsEmpty()
                ? (RuntimeAsset
                    ? RuntimeAsset->GetOutermost()->GetName()
                    : FString())
                : Operation.ReferencedAssetPath;
        const FRigVMGraphFunctionHeader* Header = Node
            ? &Node->GetReferencedFunctionHeader() : nullptr;
        if (!Node
            || !Header
            || Header->Name != FunctionName
            || NormalizePath(
                    Header->LibraryPointer.HostObject.ToString())
                != ExpectedHostPath
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_function_reference_node_conflict"),
                CurrentState};
        }
        return {};
    }

    if (Action == TEXT("set_public_control_rig_function")
        || Action == TEXT("set_mutable_control_rig_function")
        || Action == TEXT("set_category_control_rig_function")
        || Action == TEXT("set_keywords_control_rig_function")
        || Action == TEXT("set_description_control_rig_function"))
    {
        URigVMLibraryNode* OwningFunction =
            FindRigVMLocalFunctionForGraph(EditorAsset, Model);
        const FString CurrentState =
            RigVMLocalFunctionStateString(
                Function, RuntimeAsset, EditorAsset);
        if (!Function || OwningFunction != Function
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_function_conflict"), CurrentState};
        }
        if (Action == TEXT("set_public_control_rig_function"))
        {
            if (EditorAsset->IsFunctionPublic(FunctionName)
                == Operation.bRigVMFunctionPublic)
            {
                return FControlRigGraphPreflightError{
                    TEXT("control_rig_function_noop"), CurrentState};
            }
            return {};
        }
        if (Action == TEXT("set_mutable_control_rig_function"))
        {
            if (Function->IsMutable()
                == Operation.bRigVMFunctionMutable)
            {
                return FControlRigGraphPreflightError{
                    TEXT("control_rig_function_noop"), CurrentState};
            }
            return {};
        }
        const FString* RequestedText =
            Action == TEXT("set_category_control_rig_function")
                ? &Operation.RigVMFunctionCategory
                : Action == TEXT("set_keywords_control_rig_function")
                    ? &Operation.RigVMFunctionKeywords
                    : &Operation.RigVMFunctionDescription;
        const int32 MaxLength =
            Action == TEXT("set_category_control_rig_function")
                ? 256
                : Action == TEXT("set_keywords_control_rig_function")
                    ? 512 : 1200;
        const FString CurrentText =
            Action == TEXT("set_category_control_rig_function")
                ? Function->GetNodeCategory()
                : Action == TEXT("set_keywords_control_rig_function")
                    ? Function->GetNodeKeywords()
                    : Function->GetNodeDescription();
        if (RequestedText->Len() > MaxLength
            || RequestedText->Contains(TEXT("\r"))
            || RequestedText->Contains(TEXT("\n"))
            || RequestedText->Contains(TEXT("|")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_metadata"),
                RequestedText->Left(500)};
        }
        if (*RequestedText == CurrentText)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_function_noop"), CurrentState};
        }
        return {};
    }

    if (Action.EndsWith(TEXT("control_rig_function_pin")))
    {
        URigVMLibraryNode* OwningFunction =
            FindRigVMLocalFunctionForGraph(EditorAsset, Model);
        const FName PinName(*Operation.Name);
        URigVMPin* ExistingPin = Function
            ? Function->FindRootPinByName(PinName) : nullptr;
        const TOptional<FBoundedRigVMTypeSpec> PinTypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType, Operation.bRigVMTypeArray);
        const TOptional<ERigVMPinDirection> PinDirection =
            ParseRigVMFunctionPinDirection(
                Operation.RigVMPinDirection);
        if (!Function || OwningFunction != Function
            || !Model->GetEntryNode() || !Model->GetReturnNode())
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_pin"),
                Operation.RigVMFunctionName};
        }
        if (Action == TEXT("add_control_rig_function_pin"))
        {
            if (!IsSafeGraphName(Operation.Name)
                || ExistingPin || !PinTypeSpec.IsSet()
                || !PinDirection.IsSet()
                || Operation.RigVMPinDefaultValue.Len() > 1200
                || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
                || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
            {
                return FControlRigGraphPreflightError{
                    TEXT("invalid_control_rig_function_pin"),
                    Operation.Name};
            }
            return {};
        }
        const FString CurrentState =
            RigVMFunctionPinStateString(Function, PinName);
        if (!ExistingPin
            || (ExistingPin->GetDirection()
                    != ERigVMPinDirection::Input
                && ExistingPin->GetDirection()
                    != ERigVMPinDirection::Output)
            || Operation.ExpectedValue != CurrentState)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_function_pin_conflict"),
                CurrentState};
        }
        if (Action == TEXT("rename_control_rig_function_pin")
            && (!IsSafeGraphName(Operation.NewName)
                || Operation.NewName == Operation.Name
                || Function->FindRootPinByName(
                    FName(*Operation.NewName))))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_pin"),
                Operation.NewName};
        }
        if (Action == TEXT("set_type_control_rig_function_pin")
            && (!PinTypeSpec.IsSet()
                || (ExistingPin->GetCPPType()
                        == PinTypeSpec->CPPType
                    && ExistingPin->GetCPPTypeObject()
                        == PinTypeSpec->CPPTypeObject)))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_pin_type"),
                Operation.RigVMType};
        }
        if (Action == TEXT("set_index_control_rig_function_pin")
            && (Operation.Index < 0
                || Operation.Index >= Function->GetPins().Num()
                || Operation.Index
                    == Function->GetPins().IndexOfByKey(ExistingPin)))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_function_pin_index"),
                FString::FromInt(Operation.Index)};
        }
        return {};
    }

    const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
        ResolveBoundedRigVMType(
            Operation.RigVMType, Operation.bRigVMTypeArray);
    FRigVMGraphVariableDescription LocalVariable;
    const int32 LocalVariableIndex = FindRigVMLocalVariableIndex(
        Model, FName(*Operation.RigVMVariableName), LocalVariable);
    const bool bFound = LocalVariableIndex != INDEX_NONE;
    const bool bFunctionGraph =
        FindRigVMLocalFunctionForGraph(EditorAsset, Model) != nullptr;
    if (Action == TEXT("add_control_rig_local_variable"))
    {
        if (!bFunctionGraph
            || !IsSafeGraphName(Operation.RigVMVariableName)
            || !TypeSpec.IsSet()
            || bFound
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_local_variable"),
                Operation.RigVMVariableName};
        }
        return {};
    }

    const FString CurrentState = bFound
        ? RigVMLocalVariableStateString(
            LocalVariable, LocalVariableIndex)
        : TEXT("Missing");
    bool bReferenced = false;
    for (const URigVMNode* ExistingNode : Model->GetNodes())
    {
        const URigVMVariableNode* VariableNode =
            Cast<URigVMVariableNode>(ExistingNode);
        if (VariableNode && VariableNode->IsLocalVariable()
            && VariableNode->GetVariableName()
                == FName(*Operation.RigVMVariableName))
        {
            bReferenced = true;
            break;
        }
    }
    if (!bFunctionGraph || !bFound
        || Operation.ExpectedValue != CurrentState)
    {
        return FControlRigGraphPreflightError{
            TEXT("control_rig_local_variable_conflict"), CurrentState};
    }
    if (Action == TEXT("remove_control_rig_local_variable"))
    {
        if (bReferenced)
        {
            return FControlRigGraphPreflightError{
                TEXT("control_rig_local_variable_conflict"), CurrentState};
        }
        return {};
    }
    if (Action == TEXT("rename_control_rig_local_variable"))
    {
        FRigVMGraphVariableDescription Duplicate;
        if (!IsSafeGraphName(Operation.NewName)
            || Operation.NewName == Operation.RigVMVariableName
            || FindRigVMLocalVariable(
                Model, FName(*Operation.NewName), Duplicate))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_local_variable"),
                Operation.NewName};
        }
        return {};
    }
    if (Action == TEXT("set_default_control_rig_local_variable"))
    {
        if (Operation.RigVMPinDefaultValue == LocalVariable.DefaultValue
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_local_variable_default"),
                Operation.RigVMPinDefaultValue};
        }
        return {};
    }
    if (Action == TEXT("set_type_control_rig_local_variable"))
    {
        if (!TypeSpec.IsSet()
            || (LocalVariable.CPPType == TypeSpec->CPPType
                && LocalVariable.CPPTypeObject
                    == TypeSpec->CPPTypeObject)
            || Operation.RigVMPinDefaultValue.Len() > 1200
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\r"))
            || Operation.RigVMPinDefaultValue.Contains(TEXT("\n")))
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_local_variable_type"),
                Operation.RigVMType};
        }
        return {};
    }
    if (Action == TEXT("set_index_control_rig_local_variable"))
    {
        const int32 VariableCount =
            Model->GetLocalVariables(false).Num();
        if (Operation.Index < 0 || Operation.Index >= VariableCount
            || Operation.Index == LocalVariableIndex)
        {
            return FControlRigGraphPreflightError{
                TEXT("invalid_control_rig_local_variable_index"),
                FString::FromInt(Operation.Index)};
        }
        return {};
    }
    return FControlRigGraphPreflightError{
        TEXT("unsupported_control_rig_graph_action"), Action};
}

bool ApplyAdvancedControlRigGraphOperation(
    const FThomasAnimationOperation& Operation,
    URigVMGraph* Model,
    URigVMController* Controller,
    UControlRigRuntimeAsset* RuntimeAsset,
    UControlRigEditorAsset* EditorAsset)
{
    const FString& Action = Operation.Action;
    if (IsR31ControlRigGraphAction(Action))
    {
        URigVMNode* Node = Model->FindNodeByName(FName(*Operation.Name));
        const int32 PreviousPinCount =
            GetEditableRigVMAggregatePins(Node).Num();
        if (!Node || PreviousPinCount < 2)
        {
            return false;
        }
        if (Action == TEXT("add_control_rig_aggregate_pin"))
        {
            const TArray<URigVMPin*> AggregatePins =
                GetEditableRigVMAggregatePins(Node);
            URigVMNode* DefinitionNode =
                RigVMAggregateDefinitionNode(Node);
            const URigVMPin* LastPin = AggregatePins.Last();
            const FName NextPinName = LastPin && DefinitionNode
                ? DefinitionNode->GetNextAggregateName(LastPin->GetFName())
                : NAME_None;
            const FString RequestedPinName = Operation.NewName.IsEmpty()
                ? NextPinName.ToString() : Operation.NewName;
            Controller->AddAggregatePin(
                Node,
                RequestedPinName,
                Operation.RigVMPinDefaultValue,
                true,
                false);
            Node = Model->FindNodeByName(FName(*Operation.Name));
            URigVMPin* AddedPin = Node
                ? Node->FindRootPinByName(FName(*RequestedPinName))
                : nullptr;
            return Node && HasRigVMAggregateState(Node) && AddedPin
                && AddedPin->GetFName() == FName(*RequestedPinName)
                && (Operation.RigVMPinDefaultValue.IsEmpty()
                    || AddedPin->GetDefaultValue()
                        == Operation.RigVMPinDefaultValue)
                && GetEditableRigVMAggregatePins(Node).Num()
                    == PreviousPinCount + 1;
        }

        const FString RemovedPinPath = Operation.RigVMPinPath;
        if (!Controller->RemoveAggregatePin(
                RemovedPinPath, true, false))
        {
            return false;
        }
        Node = Model->FindNodeByName(FName(*Operation.Name));
        return Node && !Model->FindPin(RemovedPinPath)
            && GetEditableRigVMAggregatePins(Node).Num()
                == PreviousPinCount - 1;
    }
    if (Action == TEXT("add_control_rig_free_reroute_node"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType,
                Operation.bRigVMTypeArray);
        URigVMRerouteNode* AddedNode = TypeSpec.IsSet()
            ? Controller->AddFreeRerouteNode(
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject
                    ? FName(*TypeSpec->CPPTypeObject->GetPathName())
                    : NAME_None,
                Operation.bRigVMRerouteConstant,
                FName(*Operation.RigVMCustomWidgetName),
                Operation.RigVMPinDefaultValue,
                FVector2D(Operation.PositionX, Operation.PositionY),
                Operation.Name,
                true)
            : nullptr;
        URigVMPin* ValuePin = AddedNode
            ? Model->FindPin(Operation.Name + TEXT(".Value"))
            : nullptr;
        if (!AddedNode || !ValuePin
            || AddedNode->GetName() != Operation.Name
            || ValuePin->GetCPPType() != TypeSpec->CPPType
            || ValuePin->GetCPPTypeObject() != TypeSpec->CPPTypeObject
            || ValuePin->IsDefinedAsConstant()
                != Operation.bRigVMRerouteConstant
            || ValuePin->GetCustomWidgetName()
                != FName(*Operation.RigVMCustomWidgetName)
            || ValuePin->GetDefaultValue()
                != Operation.RigVMPinDefaultValue
            || RigVMNodePositionString(AddedNode) != FString::Printf(
                TEXT("X=%.6f,Y=%.6f"),
                static_cast<double>(Operation.PositionX),
                static_cast<double>(Operation.PositionY)))
        {
            return false;
        }
        const bool bHasSource =
            !Operation.RigVMSourcePinPath.IsEmpty();
        const FString SourcePinPath = bHasSource
            ? Operation.RigVMSourcePinPath
            : Operation.Name + TEXT(".Value");
        const FString TargetPinPath = bHasSource
            ? Operation.Name + TEXT(".Value")
            : Operation.RigVMTargetPinPath;
        const FString LinkState = RigVMLinkStateString(
            SourcePinPath, TargetPinPath);
        return Controller->AddLink(
                SourcePinPath,
                TargetPinPath,
                true,
                false,
                ERigVMPinDirection::Output,
                false)
            && Model->ContainsLink(LinkState);
    }
    if (Action == TEXT("set_control_rig_template_pin_type"))
    {
        URigVMPin* Pin = Model->FindPin(Operation.RigVMPinPath);
        URigVMTemplateNode* TemplateNode = Pin
            ? Cast<URigVMTemplateNode>(
                Pin->GetTypedOuter<URigVMNode>()) : nullptr;
        const bool bArray = Pin
            && (Pin->IsDynamicArray() || Pin->IsFixedSizeArray());
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(Operation.RigVMType, bArray);
        if (!Pin || !TemplateNode || !TypeSpec.IsSet()
            || !Controller->UnresolveTemplateNodes(
                {TemplateNode->GetFName()}, true, false))
        {
            return false;
        }
        Pin = Model->FindPin(Operation.RigVMPinPath);
        if (!Pin || !IsRigVMWildcardPin(Pin)
            || !Controller->ResolveWildCardPin(
                Operation.RigVMPinPath,
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject
                    ? FName(*TypeSpec->CPPTypeObject->GetPathName())
                    : NAME_None,
                true,
                false))
        {
            return false;
        }
        Pin = Model->FindPin(Operation.RigVMPinPath);
        TemplateNode = Pin
            ? Cast<URigVMTemplateNode>(
                Pin->GetTypedOuter<URigVMNode>()) : nullptr;
        return Pin && TemplateNode
            && !TemplateNode->IsFullyUnresolved()
            && TemplateNode->GetResolvedPermutationIndices(false).Num() == 1
            && !IsRigVMWildcardPin(Pin)
            && Pin->GetCPPType() == TypeSpec->CPPType
            && Pin->GetCPPTypeObject() == TypeSpec->CPPTypeObject;
    }
    if (Action == TEXT("collapse_control_rig_nodes"))
    {
        TArray<FName> NodeNames;
        for (const FString& NodeName : Operation.RigVMNodeNames)
        {
            NodeNames.Add(FName(*NodeName));
        }
        URigVMCollapseNode* CollapseNode = Controller->CollapseNodes(
            NodeNames, Operation.Name, true, false, false);
        if (!CollapseNode
            || CollapseNode->GetName() != Operation.Name
            || Model->FindNodeByName(CollapseNode->GetFName())
                != CollapseNode
            || !CollapseNode->GetContainedGraph())
        {
            return false;
        }
        URigVMGraph* ContainedGraph = CollapseNode->GetContainedGraph();
        for (const FName NodeName : NodeNames)
        {
            if (Model->FindNodeByName(NodeName)
                || !ContainedGraph->FindNodeByName(NodeName))
            {
                return false;
            }
        }
        return GetRigVMCollapseContentNodes(CollapseNode).Num()
            == NodeNames.Num();
    }
    if (Action == TEXT("expand_control_rig_library_node"))
    {
        TArray<URigVMNode*> ExpandedNodes =
            Controller->ExpandLibraryNode(
                FName(*Operation.Name), true, false);
        TSet<FName> ExpandedNames;
        for (URigVMNode* ExpandedNode : ExpandedNodes)
        {
            if (ExpandedNode)
            {
                ExpandedNames.Add(ExpandedNode->GetFName());
            }
        }
        if (Model->FindNodeByName(FName(*Operation.Name))
            || ExpandedNames.Num() != Operation.RigVMNodeNames.Num())
        {
            return false;
        }
        for (const FString& NodeName : Operation.RigVMNodeNames)
        {
            const FName NodeFName(*NodeName);
            if (!ExpandedNames.Contains(NodeFName)
                || !Model->FindNodeByName(NodeFName))
            {
                return false;
            }
        }
        return true;
    }

    URigVMFunctionLibrary* FunctionLibrary = EditorAsset
        ? EditorAsset->GetLocalFunctionLibrary() : nullptr;
    URigVMController* FunctionLibraryController =
        EditorAsset && FunctionLibrary
            ? EditorAsset->GetOrCreateController(FunctionLibrary)
            : nullptr;
    if (!FunctionLibrary || !FunctionLibraryController)
    {
        return false;
    }
    FunctionLibrary->Modify();
    FunctionLibraryController->Modify();

    const FName FunctionName(*Operation.RigVMFunctionName);
    if (Action == TEXT("add_control_rig_function"))
    {
        URigVMLibraryNode* AddedFunction =
            FunctionLibraryController->AddFunctionToLibrary(
                FunctionName,
                Operation.bRigVMFunctionMutable,
                FVector2D(Operation.PositionX, Operation.PositionY),
                true,
                false);
        return AddedFunction
            && AddedFunction->GetFName() == FunctionName
            && AddedFunction->IsMutable()
                == Operation.bRigVMFunctionMutable
            && AddedFunction->GetContainedGraph()
            && FunctionLibrary->FindFunction(FunctionName)
                == AddedFunction;
    }
    if (Action == TEXT("remove_control_rig_function"))
    {
        return FunctionLibraryController->RemoveFunctionFromLibrary(
                FunctionName, true, false)
            && !FunctionLibrary->FindFunction(FunctionName);
    }
    if (Action == TEXT("add_control_rig_function_reference_node"))
    {
        FResolvedControlRigFunctionDefinition Definition;
        const TOptional<FControlRigGraphPreflightError> ResolveError =
            ResolveControlRigFunctionDefinition(
                Operation,
                RuntimeAsset,
                EditorAsset,
                Definition);
        URigVMFunctionReferenceNode* AddedNode =
            !ResolveError.IsSet() && Definition.Function
            ? Controller->AddFunctionReferenceNode(
                Definition.Function,
                FVector2D(Operation.PositionX, Operation.PositionY),
                Operation.Name,
                true,
                false)
            : nullptr;
        return AddedNode
            && AddedNode->GetName() == Operation.Name
            && ControlRigFunctionReferenceMatches(
                AddedNode, Definition)
            && RigVMNodePositionString(AddedNode) == FString::Printf(
                TEXT("X=%.6f,Y=%.6f"),
                static_cast<double>(Operation.PositionX),
                static_cast<double>(Operation.PositionY));
    }
    if (Action == TEXT("remove_control_rig_function_reference_node"))
    {
        return Controller->RemoveNodeByName(
                FName(*Operation.Name), true, false)
            && !Model->FindNodeByName(FName(*Operation.Name));
    }

    URigVMLibraryNode* Function =
        FunctionLibrary->FindFunction(FunctionName);
    if (Action == TEXT("set_public_control_rig_function"))
    {
        return Function
            && FunctionLibraryController->MarkFunctionAsPublic(
                FunctionName,
                Operation.bRigVMFunctionPublic,
                true,
                false)
            && EditorAsset->IsFunctionPublic(FunctionName)
                == Operation.bRigVMFunctionPublic;
    }
    if (Action == TEXT("set_mutable_control_rig_function"))
    {
        return Function
            && Function->GetContainedGraph() == Model
            && FunctionLibraryController->SetFunctionIsPure(
                FunctionName,
                !Operation.bRigVMFunctionMutable,
                true,
                false)
            && Function->IsMutable()
                == Operation.bRigVMFunctionMutable;
    }
    if (Action == TEXT("set_category_control_rig_function"))
    {
        return Function
            && Function->GetContainedGraph() == Model
            && FunctionLibraryController->SetNodeCategory(
                CastChecked<URigVMCollapseNode>(Function),
                Operation.RigVMFunctionCategory,
                true,
                false,
                false)
            && Function->GetNodeCategory()
                == Operation.RigVMFunctionCategory;
    }
    if (Action == TEXT("set_keywords_control_rig_function"))
    {
        return Function
            && Function->GetContainedGraph() == Model
            && FunctionLibraryController->SetNodeKeywords(
                CastChecked<URigVMCollapseNode>(Function),
                Operation.RigVMFunctionKeywords,
                true,
                false,
                false)
            && Function->GetNodeKeywords()
                == Operation.RigVMFunctionKeywords;
    }
    if (Action == TEXT("set_description_control_rig_function"))
    {
        return Function
            && Function->GetContainedGraph() == Model
            && FunctionLibraryController->SetNodeDescription(
                CastChecked<URigVMCollapseNode>(Function),
                Operation.RigVMFunctionDescription,
                true,
                false,
                false)
            && Function->GetNodeDescription()
                == Operation.RigVMFunctionDescription;
    }

    if (Action.EndsWith(TEXT("control_rig_function_pin")))
    {
        const FName PinName(*Operation.Name);
        if (!Function || Function->GetContainedGraph() != Model)
        {
            return false;
        }
        if (Action == TEXT("add_control_rig_function_pin"))
        {
            const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
                ResolveBoundedRigVMType(
                    Operation.RigVMType,
                    Operation.bRigVMTypeArray);
            const TOptional<ERigVMPinDirection> Direction =
                ParseRigVMFunctionPinDirection(
                    Operation.RigVMPinDirection);
            if (!TypeSpec.IsSet() || !Direction.IsSet())
            {
                return false;
            }
            const FName AddedName = Controller->AddExposedPin(
                PinName,
                Direction.GetValue(),
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject
                    ? FName(*TypeSpec->CPPTypeObject->GetPathName())
                    : NAME_None,
                Operation.RigVMPinDefaultValue,
                true,
                false,
                false);
            URigVMPin* AddedPin =
                Function->FindRootPinByName(PinName);
            return AddedName == PinName && AddedPin
                && AddedPin->GetDirection() == Direction.GetValue()
                && AddedPin->GetCPPType() == TypeSpec->CPPType
                && AddedPin->GetCPPTypeObject()
                    == TypeSpec->CPPTypeObject
                && AddedPin->GetDefaultValue()
                    == Operation.RigVMPinDefaultValue;
        }
        if (Action == TEXT("remove_control_rig_function_pin"))
        {
            return Controller->RemoveExposedPin(
                    PinName, true, false)
                && !Function->FindRootPinByName(PinName);
        }
        if (Action == TEXT("rename_control_rig_function_pin"))
        {
            const FName NewPinName(*Operation.NewName);
            return Controller->RenameExposedPin(
                    PinName, NewPinName, true, false)
                && !Function->FindRootPinByName(PinName)
                && Function->FindRootPinByName(NewPinName);
        }
        if (Action == TEXT("set_index_control_rig_function_pin"))
        {
            if (!Controller->SetExposedPinIndex(
                    PinName, Operation.Index, true, false))
            {
                return false;
            }
            URigVMPin* IndexedPin =
                Function->FindRootPinByName(PinName);
            return IndexedPin
                && Function->GetPins().IndexOfByKey(IndexedPin)
                    == Operation.Index;
        }
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType,
                Operation.bRigVMTypeArray);
        bool bSetupUndoRedo = true;
        if (!TypeSpec.IsSet()
            || !Controller->ChangeExposedPinType(
                PinName,
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject
                    ? FName(*TypeSpec->CPPTypeObject->GetPathName())
                    : NAME_None,
                bSetupUndoRedo,
                true,
                false))
        {
            return false;
        }
        URigVMPin* ChangedPin =
            Function->FindRootPinByName(PinName);
        return ChangedPin
            && ChangedPin->GetCPPType() == TypeSpec->CPPType
            && ChangedPin->GetCPPTypeObject()
                == TypeSpec->CPPTypeObject;
    }

    const FName VariableName(*Operation.RigVMVariableName);
    if (Action == TEXT("add_control_rig_local_variable"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType, Operation.bRigVMTypeArray);
        if (!TypeSpec.IsSet())
        {
            return false;
        }
        const FRigVMGraphVariableDescription AddedVariable =
            Controller->AddLocalVariable(
                VariableName,
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject,
                Operation.RigVMPinDefaultValue,
                true,
                false);
        FRigVMGraphVariableDescription Readback;
        return AddedVariable.Name == VariableName
            && FindRigVMLocalVariable(Model, VariableName, Readback)
            && Readback.CPPType == TypeSpec->CPPType
            && (!TypeSpec->CPPTypeObject
                || Readback.CPPTypeObject == TypeSpec->CPPTypeObject)
            && Readback.DefaultValue == Operation.RigVMPinDefaultValue;
    }
    FRigVMGraphVariableDescription ExistingVariable;
    const int32 ExistingIndex = FindRigVMLocalVariableIndex(
        Model, VariableName, ExistingVariable);
    if (ExistingIndex == INDEX_NONE)
    {
        return false;
    }
    if (Action == TEXT("remove_control_rig_local_variable"))
    {
        return Controller->RemoveLocalVariable(
                VariableName, true, false)
            && !FindRigVMLocalVariable(
                Model, VariableName, ExistingVariable);
    }
    if (Action == TEXT("rename_control_rig_local_variable"))
    {
        const FName NewName(*Operation.NewName);
        FRigVMGraphVariableDescription RenamedVariable;
        return Controller->RenameLocalVariable(
                VariableName, NewName, true, false)
            && !FindRigVMLocalVariable(
                Model, VariableName, RenamedVariable)
            && FindRigVMLocalVariable(
                Model, NewName, RenamedVariable)
            && RenamedVariable.Guid == ExistingVariable.Guid;
    }
    if (Action == TEXT("set_default_control_rig_local_variable"))
    {
        FRigVMGraphVariableDescription Readback;
        return Controller->SetLocalVariableDefaultValue(
                VariableName,
                Operation.RigVMPinDefaultValue,
                true,
                false)
            && FindRigVMLocalVariable(
                Model, VariableName, Readback)
            && Readback.Guid == ExistingVariable.Guid
            && Readback.DefaultValue
                == Operation.RigVMPinDefaultValue;
    }
    if (Action == TEXT("set_type_control_rig_local_variable"))
    {
        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
            ResolveBoundedRigVMType(
                Operation.RigVMType,
                Operation.bRigVMTypeArray);
        if (!TypeSpec.IsSet()
            || !Controller->SetLocalVariableType(
                VariableName,
                TypeSpec->CPPType,
                TypeSpec->CPPTypeObject,
                true,
                false))
        {
            return false;
        }
        FRigVMGraphVariableDescription TypedVariable;
        if (!FindRigVMLocalVariable(
                Model, VariableName, TypedVariable)
            || TypedVariable.CPPType != TypeSpec->CPPType
            || TypedVariable.CPPTypeObject
                != TypeSpec->CPPTypeObject
            || !Controller->SetLocalVariableDefaultValue(
                VariableName,
                Operation.RigVMPinDefaultValue,
                true,
                false))
        {
            return false;
        }
        FRigVMGraphVariableDescription Readback;
        return FindRigVMLocalVariable(
                Model, VariableName, Readback)
            && Readback.CPPType == TypeSpec->CPPType
            && Readback.CPPTypeObject
                == TypeSpec->CPPTypeObject
            && Readback.DefaultValue
                == Operation.RigVMPinDefaultValue;
    }
    if (Action == TEXT("set_index_control_rig_local_variable"))
    {
        FRigVMGraphVariableDescription Readback;
        return Controller->SetLocalVariableIndex(
                ExistingVariable.Guid,
                Operation.Index,
                true,
                false)
            && FindRigVMLocalVariableIndex(
                Model, VariableName, Readback)
                == Operation.Index
            && Readback.Guid == ExistingVariable.Guid;
    }
    return false;
}

TOptional<ERetargetSourceOrTarget> ParseRetargetSide(const FString& Input)
{
    if (Input == TEXT("source"))
    {
        return ERetargetSourceOrTarget::Source;
    }
    if (Input == TEXT("target"))
    {
        return ERetargetSourceOrTarget::Target;
    }
    return {};
}

TOptional<ERigControlType> ParseControlRigControlType(const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("bool")) return ERigControlType::Bool;
    if (Value == TEXT("float")) return ERigControlType::Float;
    if (Value == TEXT("integer") || Value == TEXT("int"))
    {
        return ERigControlType::Integer;
    }
    if (Value == TEXT("vector2d")) return ERigControlType::Vector2D;
    if (Value == TEXT("position")) return ERigControlType::Position;
    if (Value == TEXT("scale")) return ERigControlType::Scale;
    if (Value == TEXT("rotator")) return ERigControlType::Rotator;
    if (Value == TEXT("transform")) return ERigControlType::Transform;
    if (Value == TEXT("transform_no_scale")
        || Value == TEXT("transformnoscale"))
    {
        return ERigControlType::TransformNoScale;
    }
    if (Value == TEXT("euler_transform")
        || Value == TEXT("eulertransform"))
    {
        return ERigControlType::EulerTransform;
    }
    if (Value == TEXT("scale_float") || Value == TEXT("scalefloat"))
    {
        return ERigControlType::ScaleFloat;
    }
    return {};
}

TOptional<ERigElementType> ParseControlRigElementType(
    const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("bone")) return ERigElementType::Bone;
    if (Value == TEXT("null")) return ERigElementType::Null;
    if (Value == TEXT("control")) return ERigElementType::Control;
    if (Value == TEXT("curve")) return ERigElementType::Curve;
    if (Value == TEXT("reference")) return ERigElementType::Reference;
    if (Value == TEXT("connector")) return ERigElementType::Connector;
    if (Value == TEXT("socket")) return ERigElementType::Socket;
    return {};
}

TOptional<ERigElementType> ParseControlRigTransformElementType(
    const FString& Input)
{
    const TOptional<ERigElementType> Type =
        ParseControlRigElementType(Input);
    if (Type.IsSet()
        && Type.GetValue() != ERigElementType::Curve
        && Type.GetValue() != ERigElementType::Connector)
    {
        return Type;
    }
    return {};
}

TOptional<ERigMetadataType> ParseControlRigMetadataType(
    const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("bool")) return ERigMetadataType::Bool;
    if (Value == TEXT("bool_array")) return ERigMetadataType::BoolArray;
    if (Value == TEXT("float")) return ERigMetadataType::Float;
    if (Value == TEXT("float_array")) return ERigMetadataType::FloatArray;
    if (Value == TEXT("integer") || Value == TEXT("int"))
    {
        return ERigMetadataType::Int32;
    }
    if (Value == TEXT("integer_array") || Value == TEXT("int_array")
        || Value == TEXT("int32_array"))
    {
        return ERigMetadataType::Int32Array;
    }
    if (Value == TEXT("name")) return ERigMetadataType::Name;
    if (Value == TEXT("name_array")) return ERigMetadataType::NameArray;
    if (Value == TEXT("vector")) return ERigMetadataType::Vector;
    if (Value == TEXT("vector_array")) return ERigMetadataType::VectorArray;
    if (Value == TEXT("rotator")) return ERigMetadataType::Rotator;
    if (Value == TEXT("rotator_array"))
    {
        return ERigMetadataType::RotatorArray;
    }
    if (Value == TEXT("quat") || Value == TEXT("quaternion"))
    {
        return ERigMetadataType::Quat;
    }
    if (Value == TEXT("quat_array") || Value == TEXT("quaternion_array"))
    {
        return ERigMetadataType::QuatArray;
    }
    if (Value == TEXT("transform")) return ERigMetadataType::Transform;
    if (Value == TEXT("transform_array"))
    {
        return ERigMetadataType::TransformArray;
    }
    if (Value == TEXT("color")) return ERigMetadataType::LinearColor;
    if (Value == TEXT("color_array")
        || Value == TEXT("linear_color_array"))
    {
        return ERigMetadataType::LinearColorArray;
    }
    if (Value == TEXT("element_key"))
    {
        return ERigMetadataType::RigElementKey;
    }
    if (Value == TEXT("element_key_array"))
    {
        return ERigMetadataType::RigElementKeyArray;
    }
    return {};
}

bool IsValidControlRigTransform(const FThomasAnimationOperation& Operation)
{
    const FTransform Transform(
        Operation.Rotation,
        Operation.Location,
        Operation.Scale);
    return Transform.IsValid()
        && Operation.Location.GetAbsMax() <= 1000000.0
        && Operation.Rotation.Euler().GetAbsMax() <= 360000.0
        && Operation.Scale.GetAbsMax() <= 10000.0
        && Operation.Scale.GetAbsMin() >= 0.0001;
}

bool IsValidControlRigColor(const FLinearColor& Color)
{
    return FMath::IsFinite(Color.R)
        && FMath::IsFinite(Color.G)
        && FMath::IsFinite(Color.B)
        && FMath::IsFinite(Color.A)
        && Color.R >= 0.0f && Color.R <= 1.0f
        && Color.G >= 0.0f && Color.G <= 1.0f
        && Color.B >= 0.0f && Color.B <= 1.0f
        && Color.A >= 0.0f && Color.A <= 1.0f;
}

bool IsSafeControlRigDescription(const FString& Description)
{
    return Description.Len() <= 256
        && !Description.Contains(TEXT("|"))
        && !Description.Contains(TEXT("\r"))
        && !Description.Contains(TEXT("\n"));
}

FString ControlRigControlValueString(
    const URigHierarchy* Hierarchy,
    const FRigElementKey& Key,
    const ERigControlType ControlType,
    const ERigControlValueType ValueType)
{
    if (!Hierarchy || !Key.IsValid())
    {
        return FString();
    }
    switch (ControlType)
    {
    case ERigControlType::Bool:
        return Hierarchy->GetControlValue<bool>(Key, ValueType)
            ? TEXT("True") : TEXT("False");
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return FString::SanitizeFloat(
            Hierarchy->GetControlValue<float>(Key, ValueType));
    case ERigControlType::Integer:
        return FString::FromInt(
            Hierarchy->GetControlValue<int32>(Key, ValueType));
    case ERigControlType::Vector2D:
        return URigHierarchy::GetVector2DFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType)).ToString();
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return URigHierarchy::GetVectorFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType)).ToString();
    case ERigControlType::Rotator:
        return URigHierarchy::GetRotatorFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType)).ToString();
    case ERigControlType::Transform:
        return URigHierarchy::GetTransformFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType)).ToString();
    case ERigControlType::TransformNoScale:
        return URigHierarchy::GetTransformNoScaleFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType))
            .ToFTransform().ToString();
    case ERigControlType::EulerTransform:
        return URigHierarchy::GetEulerTransformFromControlValue(
            Hierarchy->GetControlValue(Key, ValueType))
            .ToFTransform().ToString();
    default:
        return TEXT("Unsupported");
    }
}

FString ControlRigControlShapeSettingsString(
    const FRigControlSettings& Settings)
{
    return FString::Printf(
        TEXT("ShapeName=%s|ShapeColor=%s|ShapeVisible=%s"),
        *Settings.ShapeName.ToString(),
        *Settings.ShapeColor.ToString(),
        Settings.IsVisible() ? TEXT("True") : TEXT("False"));
}

bool IsControlRigShapeAvailable(
    UControlRigRuntimeAsset* ControlRigAsset,
    const FName ShapeName)
{
    if (!ControlRigAsset || ShapeName.IsNone())
    {
        return ShapeName.IsNone();
    }
    const TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& Libraries =
        ControlRigAsset->GetShapeLibraries();
    if (Libraries.IsEmpty()
        || Libraries.Num() > MaxControlRigShapeLibraries)
    {
        return false;
    }
    for (const TSoftObjectPtr<UControlRigShapeLibrary>& Reference : Libraries)
    {
        UControlRigShapeLibrary* Library = Reference.LoadSynchronous();
        if (Library && Library->GetShapeByName(ShapeName, false))
        {
            return true;
        }
    }
    return false;
}

void GetControlRigShapeLibraryInspection(
    UControlRigRuntimeAsset* ControlRigAsset,
    int32& OutLoadedLibraryCount,
    int32& OutShapeCount,
    TArray<FString>& OutItems)
{
    OutLoadedLibraryCount = 0;
    OutShapeCount = 0;
    OutItems.Reset();
    if (!ControlRigAsset)
    {
        return;
    }
    const TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& Libraries =
        ControlRigAsset->GetShapeLibraries();
    const int32 LibraryCount = FMath::Min(
        Libraries.Num(), MaxControlRigShapeLibraries);
    for (int32 Index = 0; Index < LibraryCount; ++Index)
    {
        const TSoftObjectPtr<UControlRigShapeLibrary>& Reference =
            Libraries[Index];
        UControlRigShapeLibrary* Library = Reference.LoadSynchronous();
        const FString Path = Reference.IsNull()
            ? TEXT("None")
            : Reference.ToSoftObjectPath().ToString();
        if (!Library)
        {
            OutItems.Add(FString::Printf(
                TEXT("ControlRigShapeLibrary Index=%d Path=%s Loaded=False ShapeCount=0 Shapes="),
                Index,
                *Path));
            continue;
        }
        ++OutLoadedLibraryCount;
        TSet<FName> UniqueNames;
        if (!Library->DefaultShape.ShapeName.IsNone())
        {
            UniqueNames.Add(Library->DefaultShape.ShapeName);
        }
        for (const FControlRigShapeDefinition& Shape : Library->Shapes)
        {
            if (!Shape.ShapeName.IsNone())
            {
                UniqueNames.Add(Shape.ShapeName);
            }
        }
        TArray<FName> Names = UniqueNames.Array();
        Names.Sort([](const FName& A, const FName& B)
        {
            return A.LexicalLess(B);
        });
        OutShapeCount += Names.Num();
        TArray<FString> ShapeNames;
        ShapeNames.Reserve(Names.Num());
        for (const FName Name : Names)
        {
            ShapeNames.Add(Name.ToString());
        }
        OutItems.Add(FString::Printf(
            TEXT("ControlRigShapeLibrary Index=%d Path=%s Loaded=True Default=%s ShapeCount=%d Shapes=%s"),
            Index,
            *Path,
            *Library->DefaultShape.ShapeName.ToString(),
            Names.Num(),
            *FString::Join(ShapeNames, TEXT(","))));
    }
    if (Libraries.Num() > LibraryCount)
    {
        OutItems.Add(FString::Printf(
            TEXT("ControlRigShapeLibrary Truncated=%d"),
            Libraries.Num() - LibraryCount));
    }
}

bool IsValidControlRigControlValue(
    const FThomasAnimationOperation& Operation,
    const ERigControlType ControlType)
{
    switch (ControlType)
    {
    case ERigControlType::Bool:
    case ERigControlType::Integer:
        return true;
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return FMath::IsFinite(Operation.Value)
            && FMath::Abs(Operation.Value) <= 1000000.0f;
    case ERigControlType::Vector2D:
        return !Operation.ControlVector2DValue.ContainsNaN()
            && Operation.ControlVector2DValue.GetAbsMax() <= 1000000.0;
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return !Operation.ControlVectorValue.ContainsNaN()
            && Operation.ControlVectorValue.GetAbsMax() <= 1000000.0;
    case ERigControlType::Rotator:
        return !Operation.Rotation.ContainsNaN()
            && Operation.Rotation.Euler().GetAbsMax() <= 360000.0;
    case ERigControlType::Transform:
    case ERigControlType::EulerTransform:
    {
        const FTransform Transform(
            Operation.Rotation,
            Operation.Location,
            Operation.Scale);
        return Transform.IsValid()
            && Operation.Location.GetAbsMax() <= 1000000.0
            && Operation.Rotation.Euler().GetAbsMax() <= 360000.0
            && Operation.Scale.GetAbsMax() <= 10000.0;
    }
    case ERigControlType::TransformNoScale:
    {
        const FTransform Transform(
            Operation.Rotation,
            Operation.Location,
            FVector::OneVector);
        return Transform.IsValid()
            && Operation.Location.GetAbsMax() <= 1000000.0
            && Operation.Rotation.Euler().GetAbsMax() <= 360000.0
            && Operation.Scale.Equals(
                FVector::OneVector, KINDA_SMALL_NUMBER);
    }
    default:
        return false;
    }
}

FRigControlValue MakeControlRigControlValue(
    const FThomasAnimationOperation& Operation,
    const ERigControlType ControlType)
{
    switch (ControlType)
    {
    case ERigControlType::Bool:
        return URigHierarchy::MakeControlValueFromBool(
            Operation.bControlBoolValue);
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return URigHierarchy::MakeControlValueFromFloat(Operation.Value);
    case ERigControlType::Integer:
        return URigHierarchy::MakeControlValueFromInt(
            Operation.ControlIntegerValue);
    case ERigControlType::Vector2D:
        return URigHierarchy::MakeControlValueFromVector2D(
            Operation.ControlVector2DValue);
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return URigHierarchy::MakeControlValueFromVector(
            Operation.ControlVectorValue);
    case ERigControlType::Rotator:
        return URigHierarchy::MakeControlValueFromRotator(
            Operation.Rotation);
    case ERigControlType::Transform:
        return URigHierarchy::MakeControlValueFromTransform(
            FTransform(Operation.Rotation, Operation.Location, Operation.Scale));
    case ERigControlType::TransformNoScale:
        return URigHierarchy::MakeControlValueFromTransformNoScale(
            FTransformNoScale(
                Operation.Location,
                Operation.Rotation.Quaternion()));
    case ERigControlType::EulerTransform:
        return URigHierarchy::MakeControlValueFromEulerTransform(
            FEulerTransform(
                Operation.Location,
                Operation.Rotation,
                Operation.Scale));
    default:
        return FRigControlValue();
    }
}

FRigControlValue MakeControlRigControlValue(
    const FThomasControlRigValueSpec& Value,
    const ERigControlType ControlType)
{
    switch (ControlType)
    {
    case ERigControlType::Bool:
        return URigHierarchy::MakeControlValueFromBool(Value.bBoolValue);
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return URigHierarchy::MakeControlValueFromFloat(Value.FloatValue);
    case ERigControlType::Integer:
        return URigHierarchy::MakeControlValueFromInt(Value.IntegerValue);
    case ERigControlType::Vector2D:
        return URigHierarchy::MakeControlValueFromVector2D(
            Value.Vector2DValue);
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return URigHierarchy::MakeControlValueFromVector(
            Value.VectorValue);
    case ERigControlType::Rotator:
        return URigHierarchy::MakeControlValueFromRotator(Value.Rotation);
    case ERigControlType::Transform:
        return URigHierarchy::MakeControlValueFromTransform(
            FTransform(Value.Rotation, Value.Location, Value.Scale));
    case ERigControlType::TransformNoScale:
        return URigHierarchy::MakeControlValueFromTransformNoScale(
            FTransformNoScale(Value.Location, Value.Rotation.Quaternion()));
    case ERigControlType::EulerTransform:
        return URigHierarchy::MakeControlValueFromEulerTransform(
            FEulerTransform(Value.Location, Value.Rotation, Value.Scale));
    default:
        return FRigControlValue();
    }
}

FString ControlRigControlValueString(
    const FRigControlValue& Value,
    const ERigControlType ControlType)
{
    switch (ControlType)
    {
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return FString::SanitizeFloat(
            URigHierarchy::GetFloatFromControlValue(Value));
    case ERigControlType::Integer:
        return FString::FromInt(
            URigHierarchy::GetIntFromControlValue(Value));
    case ERigControlType::Vector2D:
        return URigHierarchy::GetVector2DFromControlValue(Value)
            .ToString();
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return URigHierarchy::GetVectorFromControlValue(Value)
            .ToString();
    case ERigControlType::Rotator:
        return URigHierarchy::GetRotatorFromControlValue(Value)
            .ToString();
    case ERigControlType::Transform:
        return URigHierarchy::GetTransformFromControlValue(Value)
            .ToString();
    case ERigControlType::TransformNoScale:
        return URigHierarchy::GetTransformNoScaleFromControlValue(Value)
            .ToFTransform().ToString();
    case ERigControlType::EulerTransform:
        return URigHierarchy::GetEulerTransformFromControlValue(Value)
            .ToFTransform().ToString();
    default:
        return TEXT("Unsupported");
    }
}

bool IsValidControlRigControlValue(
    const FThomasControlRigValueSpec& Value,
    const ERigControlType ControlType)
{
    switch (ControlType)
    {
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return FMath::IsFinite(Value.FloatValue)
            && FMath::Abs(Value.FloatValue) <= 1000000.0f;
    case ERigControlType::Integer:
        return true;
    case ERigControlType::Vector2D:
        return !Value.Vector2DValue.ContainsNaN()
            && Value.Vector2DValue.GetAbsMax() <= 1000000.0;
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return !Value.VectorValue.ContainsNaN()
            && Value.VectorValue.GetAbsMax() <= 1000000.0;
    case ERigControlType::Rotator:
        return !Value.Rotation.ContainsNaN()
            && Value.Rotation.Euler().GetAbsMax() <= 360000.0;
    case ERigControlType::Transform:
    case ERigControlType::EulerTransform:
    {
        const FTransform Transform(
            Value.Rotation, Value.Location, Value.Scale);
        return Transform.IsValid()
            && Value.Location.GetAbsMax() <= 1000000.0
            && Value.Rotation.Euler().GetAbsMax() <= 360000.0
            && Value.Scale.GetAbsMax() <= 10000.0;
    }
    case ERigControlType::TransformNoScale:
    {
        const FTransform Transform(
            Value.Rotation, Value.Location, FVector::OneVector);
        return Transform.IsValid()
            && Value.Location.GetAbsMax() <= 1000000.0
            && Value.Rotation.Euler().GetAbsMax() <= 360000.0
            && Value.Scale.Equals(
                FVector::OneVector, KINDA_SMALL_NUMBER);
    }
    default:
        return false;
    }
}

bool AreControlRigLimitsOrdered(
    const FThomasControlRigValueSpec& Minimum,
    const FThomasControlRigValueSpec& Maximum,
    const ERigControlType ControlType)
{
    auto OrderedVector = [](const FVector& A, const FVector& B)
    {
        return A.X <= B.X && A.Y <= B.Y && A.Z <= B.Z;
    };
    switch (ControlType)
    {
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return Minimum.FloatValue <= Maximum.FloatValue;
    case ERigControlType::Integer:
        return Minimum.IntegerValue <= Maximum.IntegerValue;
    case ERigControlType::Vector2D:
        return Minimum.Vector2DValue.X <= Maximum.Vector2DValue.X
            && Minimum.Vector2DValue.Y <= Maximum.Vector2DValue.Y;
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return OrderedVector(Minimum.VectorValue, Maximum.VectorValue);
    case ERigControlType::Rotator:
        return Minimum.Rotation.Pitch <= Maximum.Rotation.Pitch
            && Minimum.Rotation.Yaw <= Maximum.Rotation.Yaw
            && Minimum.Rotation.Roll <= Maximum.Rotation.Roll;
    case ERigControlType::Transform:
    case ERigControlType::EulerTransform:
        return OrderedVector(Minimum.Location, Maximum.Location)
            && Minimum.Rotation.Pitch <= Maximum.Rotation.Pitch
            && Minimum.Rotation.Yaw <= Maximum.Rotation.Yaw
            && Minimum.Rotation.Roll <= Maximum.Rotation.Roll
            && OrderedVector(Minimum.Scale, Maximum.Scale);
    case ERigControlType::TransformNoScale:
        return OrderedVector(Minimum.Location, Maximum.Location)
            && Minimum.Rotation.Pitch <= Maximum.Rotation.Pitch
            && Minimum.Rotation.Yaw <= Maximum.Rotation.Yaw
            && Minimum.Rotation.Roll <= Maximum.Rotation.Roll;
    default:
        return false;
    }
}

void SetupControlRigLimitArrayForType(
    FRigControlSettings& Settings,
    const ERigControlType ControlType)
{
    const bool bLimitTranslation =
        ControlType == ERigControlType::Float
        || ControlType == ERigControlType::Integer
        || ControlType == ERigControlType::Vector2D
        || ControlType == ERigControlType::Position
        || ControlType == ERigControlType::Transform
        || ControlType == ERigControlType::TransformNoScale
        || ControlType == ERigControlType::EulerTransform;
    const bool bLimitRotation =
        ControlType == ERigControlType::Rotator
        || ControlType == ERigControlType::Transform
        || ControlType == ERigControlType::TransformNoScale
        || ControlType == ERigControlType::EulerTransform;
    const bool bLimitScale =
        ControlType == ERigControlType::Scale
        || ControlType == ERigControlType::Transform
        || ControlType == ERigControlType::EulerTransform
        || ControlType == ERigControlType::ScaleFloat;
    Settings.ControlType = ControlType;
    Settings.SetupLimitArrayForType(
        bLimitTranslation, bLimitRotation, bLimitScale);
}

bool HasValidControlRigLimitChannelCounts(
    const FThomasAnimationOperation& Operation,
    const ERigControlType ControlType,
    int32& OutChannelCount)
{
    FRigControlSettings RequestedSettings;
    SetupControlRigLimitArrayForType(RequestedSettings, ControlType);
    OutChannelCount = RequestedSettings.LimitEnabled.Num();
    const bool bHasPerChannelFlags =
        !Operation.ControlMinimumLimitEnabledPerChannel.IsEmpty()
        || !Operation.ControlMaximumLimitEnabledPerChannel.IsEmpty();
    return !bHasPerChannelFlags
        || (Operation.ControlMinimumLimitEnabledPerChannel.Num()
                == OutChannelCount
            && Operation.ControlMaximumLimitEnabledPerChannel.Num()
                == OutChannelCount);
}

FString ControlRigLimitChannelNamesString(
    const ERigControlType ControlType,
    const int32 ChannelCount)
{
    TArray<FString> Names;
    switch (ControlType)
    {
    case ERigControlType::Float:
    case ERigControlType::Integer:
        Names = {TEXT("Value")};
        break;
    case ERigControlType::ScaleFloat:
        Names = {TEXT("Scale")};
        break;
    case ERigControlType::Vector2D:
        Names = {TEXT("X"), TEXT("Y")};
        break;
    case ERigControlType::Position:
    case ERigControlType::Scale:
        Names = {TEXT("X"), TEXT("Y"), TEXT("Z")};
        break;
    case ERigControlType::Rotator:
        Names = {TEXT("Roll"), TEXT("Pitch"), TEXT("Yaw")};
        break;
    case ERigControlType::Transform:
    case ERigControlType::EulerTransform:
        Names = {
            TEXT("LocationX"), TEXT("LocationY"), TEXT("LocationZ"),
            TEXT("RotationRoll"), TEXT("RotationPitch"),
            TEXT("RotationYaw"), TEXT("ScaleX"), TEXT("ScaleY"),
            TEXT("ScaleZ")};
        break;
    case ERigControlType::TransformNoScale:
        Names = {
            TEXT("LocationX"), TEXT("LocationY"), TEXT("LocationZ"),
            TEXT("RotationRoll"), TEXT("RotationPitch"),
            TEXT("RotationYaw")};
        break;
    default:
        break;
    }
    if (Names.Num() != ChannelCount)
    {
        Names.Reset(ChannelCount);
        for (int32 Index = 0; Index < ChannelCount; ++Index)
        {
            Names.Add(FString::Printf(TEXT("Channel%d"), Index));
        }
    }
    return FString::Join(Names, TEXT(","));
}

FString ControlRigControlLimitSettingsString(
    const FRigControlSettings& Settings)
{
    TArray<FString> Flags;
    for (const FRigControlLimitEnabled& Limit : Settings.LimitEnabled)
    {
        Flags.Add(FString::Printf(TEXT("%d:%d"),
            Limit.bMinimum ? 1 : 0,
            Limit.bMaximum ? 1 : 0));
    }
    return FString::Printf(
        TEXT("Channels=%s|Draw=%s|Flags=%s|Minimum=%s|Maximum=%s"),
        *ControlRigLimitChannelNamesString(
            Settings.ControlType, Settings.LimitEnabled.Num()),
        Settings.bDrawLimits ? TEXT("True") : TEXT("False"),
        *FString::Join(Flags, TEXT(",")),
        *ControlRigControlValueString(
            Settings.MinimumValue, Settings.ControlType),
        *ControlRigControlValueString(
            Settings.MaximumValue, Settings.ControlType));
}

FString ControlRigAvailableSpacesString(
    const FRigControlSettings& Settings)
{
    const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
    TArray<FString> Values;
    for (int32 Index = 0;
         Index < Settings.Customization.AvailableSpaces.Num();
         ++Index)
    {
        const FRigElementKeyWithLabel& Space =
            Settings.Customization.AvailableSpaces[Index];
        Values.Add(FString::Printf(
            TEXT("%d=%s:%s:%s"),
            Index,
            *Space.Key.Name.ToString(),
            ElementTypeEnum
                ? *ElementTypeEnum->GetNameStringByValue(
                    static_cast<int64>(Space.Key.Type))
                : TEXT("Unknown"),
            *Space.Label.ToString()));
    }
    return FString::Join(Values, TEXT(","));
}

FString ControlRigSocketSettingsString(
    const URigHierarchy* Hierarchy,
    const FRigSocketElement* Socket)
{
    if (!Hierarchy || !Socket)
    {
        return FString();
    }
    return FString::Printf(
        TEXT("Color=%s|Description=%s"),
        *Socket->GetColor(Hierarchy).ToString(),
        *Socket->GetDescription(Hierarchy));
}

FString ControlRigSocketStateString(
    const URigHierarchy* Hierarchy,
    const FRigElementKey& Key)
{
    const FRigSocketElement* Socket = Hierarchy
        ? Hierarchy->Find<FRigSocketElement>(Key) : nullptr;
    if (!Hierarchy || !Socket)
    {
        return FString();
    }
    const FRigElementKey Parent = Hierarchy->GetFirstParent(Key);
    const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
    const FString ParentType = Parent.IsValid() && ElementTypeEnum
        ? ElementTypeEnum->GetNameStringByValue(
            static_cast<int64>(Parent.Type))
        : TEXT("None");
    return FString::Printf(
        TEXT("Parent=%s:%s|InitialLocal=%s|%s"),
        *ParentType,
        Parent.IsValid() ? *Parent.Name.ToString() : TEXT("None"),
        *Hierarchy->GetLocalTransform(Key, true).ToString(),
        *ControlRigSocketSettingsString(Hierarchy, Socket));
}

bool IsValidControlRigMetadataArraySize(const int32 Num)
{
    return Num >= 0 && Num <= MaxControlRigMetadataArrayItems;
}

bool IsValidControlRigMetadataQuat(const FQuat& Value)
{
    return FMath::IsFinite(Value.X)
        && FMath::IsFinite(Value.Y)
        && FMath::IsFinite(Value.Z)
        && FMath::IsFinite(Value.W)
        && Value.SizeSquared() > SMALL_NUMBER;
}

FQuat CanonicalControlRigMetadataQuat(const FQuat& Value)
{
    return IsValidControlRigMetadataQuat(Value)
        ? Value.GetNormalized()
        : FQuat::Identity;
}

bool IsValidControlRigMetadataTransform(const FTransform& Value)
{
    return Value.IsValid()
        && Value.GetLocation().GetAbsMax() <= 1000000.0
        && Value.Rotator().Euler().GetAbsMax() <= 360000.0
        && Value.GetScale3D().GetAbsMax() <= 10000.0
        && Value.GetScale3D().GetAbsMin() >= 0.0001;
}

bool IsValidControlRigMetadataColor(const FLinearColor& Value)
{
    return FMath::IsFinite(Value.R)
        && FMath::IsFinite(Value.G)
        && FMath::IsFinite(Value.B)
        && FMath::IsFinite(Value.A)
        && FMath::Abs(Value.R) <= 1000000.0f
        && FMath::Abs(Value.G) <= 1000000.0f
        && FMath::Abs(Value.B) <= 1000000.0f
        && FMath::Abs(Value.A) <= 1000000.0f;
}

bool IsValidControlRigMetadataElementKey(
    const URigHierarchy* Hierarchy,
    const FString& ElementName,
    const FString& ElementType)
{
    const TOptional<ERigElementType> ParsedType =
        ParseControlRigElementType(ElementType);
    return Hierarchy
        && ParsedType.IsSet()
        && IsSafeGraphName(ElementName)
        && Hierarchy->GetIndex(
            FRigElementKey(FName(*ElementName), ParsedType.GetValue()),
            true) != INDEX_NONE;
}

template <typename TValue, typename TFormatter>
FString ControlRigMetadataArrayString(
    const TArray<TValue>& Values,
    TFormatter&& Formatter)
{
    TArray<FString> Parts;
    Parts.Reserve(Values.Num());
    for (const TValue& Value : Values)
    {
        Parts.Add(Formatter(Value));
    }
    return TEXT("[") + FString::Join(Parts, TEXT(";")) + TEXT("]");
}

TArray<FName> ControlRigMetadataNameArray(
    const FThomasControlRigValueSpec& Value)
{
    TArray<FName> Result;
    Result.Reserve(Value.NameArrayValue.Num());
    for (const FString& Name : Value.NameArrayValue)
    {
        Result.Add(FName(*Name));
    }
    return Result;
}

TArray<FRigElementKey> ControlRigMetadataElementKeyArray(
    const FThomasControlRigValueSpec& Value)
{
    TArray<FRigElementKey> Result;
    Result.Reserve(Value.ElementNames.Num());
    for (int32 Index = 0; Index < Value.ElementNames.Num(); ++Index)
    {
        Result.Add(FRigElementKey(
            FName(*Value.ElementNames[Index]),
            ParseControlRigElementType(
                Value.ElementTypes[Index]).GetValue()));
    }
    return Result;
}

void NormalizeControlRigMetadataValue(FThomasControlRigValueSpec& Value)
{
    Value.NameValue = Value.NameValue.TrimStartAndEnd();
    Value.ElementName = Value.ElementName.TrimStartAndEnd();
    Value.ElementType = Value.ElementType.TrimStartAndEnd().ToLower();
    for (FString& Name : Value.NameArrayValue)
    {
        Name = Name.TrimStartAndEnd();
    }
    for (FString& Name : Value.ElementNames)
    {
        Name = Name.TrimStartAndEnd();
    }
    for (FString& Type : Value.ElementTypes)
    {
        Type = Type.TrimStartAndEnd().ToLower();
    }
}

bool IsValidControlRigMetadataValue(
    const URigHierarchy* Hierarchy,
    const FThomasControlRigValueSpec& Value,
    const ERigMetadataType Type)
{
    switch (Type)
    {
    case ERigMetadataType::Bool:
    case ERigMetadataType::Int32:
        return true;
    case ERigMetadataType::BoolArray:
        return IsValidControlRigMetadataArraySize(
            Value.BoolArrayValue.Num());
    case ERigMetadataType::Float:
        return FMath::IsFinite(Value.FloatValue)
            && FMath::Abs(Value.FloatValue) <= 1000000.0f;
    case ERigMetadataType::FloatArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.FloatArrayValue.Num()))
        {
            return false;
        }
        for (const float Item : Value.FloatArrayValue)
        {
            if (!FMath::IsFinite(Item)
                || FMath::Abs(Item) > 1000000.0f)
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::Int32Array:
        return IsValidControlRigMetadataArraySize(
            Value.IntegerArrayValue.Num());
    case ERigMetadataType::Name:
        return IsSafeGraphName(Value.NameValue);
    case ERigMetadataType::NameArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.NameArrayValue.Num()))
        {
            return false;
        }
        for (const FString& Item : Value.NameArrayValue)
        {
            if (!IsSafeGraphName(Item))
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::Vector:
        return !Value.VectorValue.ContainsNaN()
            && Value.VectorValue.GetAbsMax() <= 1000000.0;
    case ERigMetadataType::VectorArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.VectorArrayValue.Num()))
        {
            return false;
        }
        for (const FVector& Item : Value.VectorArrayValue)
        {
            if (Item.ContainsNaN() || Item.GetAbsMax() > 1000000.0)
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::Rotator:
        return !Value.Rotation.ContainsNaN()
            && Value.Rotation.Euler().GetAbsMax() <= 360000.0;
    case ERigMetadataType::RotatorArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.RotatorArrayValue.Num()))
        {
            return false;
        }
        for (const FRotator& Item : Value.RotatorArrayValue)
        {
            if (Item.ContainsNaN()
                || Item.Euler().GetAbsMax() > 360000.0)
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::Quat:
        return IsValidControlRigMetadataQuat(Value.Quaternion);
    case ERigMetadataType::QuatArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.QuaternionArrayValue.Num()))
        {
            return false;
        }
        for (const FQuat& Item : Value.QuaternionArrayValue)
        {
            if (!IsValidControlRigMetadataQuat(Item))
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::Transform:
        return IsValidControlRigMetadataTransform(FTransform(
            Value.Rotation, Value.Location, Value.Scale));
    case ERigMetadataType::TransformArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.TransformArrayValue.Num()))
        {
            return false;
        }
        for (const FTransform& Item : Value.TransformArrayValue)
        {
            if (!IsValidControlRigMetadataTransform(Item))
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::LinearColor:
        return IsValidControlRigMetadataColor(Value.ColorValue);
    case ERigMetadataType::LinearColorArray:
        if (!IsValidControlRigMetadataArraySize(
                Value.ColorArrayValue.Num()))
        {
            return false;
        }
        for (const FLinearColor& Item : Value.ColorArrayValue)
        {
            if (!IsValidControlRigMetadataColor(Item))
            {
                return false;
            }
        }
        return true;
    case ERigMetadataType::RigElementKey:
        return IsValidControlRigMetadataElementKey(
            Hierarchy, Value.ElementName, Value.ElementType);
    case ERigMetadataType::RigElementKeyArray:
        if (Value.ElementNames.Num() != Value.ElementTypes.Num()
            || !IsValidControlRigMetadataArraySize(
                Value.ElementNames.Num()))
        {
            return false;
        }
        for (int32 Index = 0; Index < Value.ElementNames.Num(); ++Index)
        {
            if (!IsValidControlRigMetadataElementKey(
                    Hierarchy,
                    Value.ElementNames[Index],
                    Value.ElementTypes[Index]))
            {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

FString ControlRigMetadataValueString(
    const FThomasControlRigValueSpec& Value,
    const ERigMetadataType Type)
{
    switch (Type)
    {
    case ERigMetadataType::Bool:
        return Value.bBoolValue ? TEXT("True") : TEXT("False");
    case ERigMetadataType::BoolArray:
        return ControlRigMetadataArrayString(
            Value.BoolArrayValue,
            [](const bool Item)
            {
                return Item ? FString(TEXT("True"))
                    : FString(TEXT("False"));
            });
    case ERigMetadataType::Float:
        return FString::SanitizeFloat(Value.FloatValue);
    case ERigMetadataType::FloatArray:
        return ControlRigMetadataArrayString(
            Value.FloatArrayValue,
            [](const float Item)
            {
                return FString::SanitizeFloat(Item);
            });
    case ERigMetadataType::Int32:
        return FString::FromInt(Value.IntegerValue);
    case ERigMetadataType::Int32Array:
        return ControlRigMetadataArrayString(
            Value.IntegerArrayValue,
            [](const int32 Item)
            {
                return FString::FromInt(Item);
            });
    case ERigMetadataType::Name:
        return Value.NameValue;
    case ERigMetadataType::NameArray:
        return ControlRigMetadataArrayString(
            Value.NameArrayValue,
            [](const FString& Item)
            {
                return Item;
            });
    case ERigMetadataType::Vector:
        return Value.VectorValue.ToString();
    case ERigMetadataType::VectorArray:
        return ControlRigMetadataArrayString(
            Value.VectorArrayValue,
            [](const FVector& Item)
            {
                return Item.ToString();
            });
    case ERigMetadataType::Rotator:
        return Value.Rotation.ToString();
    case ERigMetadataType::RotatorArray:
        return ControlRigMetadataArrayString(
            Value.RotatorArrayValue,
            [](const FRotator& Item)
            {
                return Item.ToString();
            });
    case ERigMetadataType::Quat:
        return CanonicalControlRigMetadataQuat(
            Value.Quaternion).ToString();
    case ERigMetadataType::QuatArray:
        return ControlRigMetadataArrayString(
            Value.QuaternionArrayValue,
            [](const FQuat& Item)
            {
                return CanonicalControlRigMetadataQuat(Item).ToString();
            });
    case ERigMetadataType::Transform:
        return FTransform(
            Value.Rotation, Value.Location, Value.Scale).ToString();
    case ERigMetadataType::TransformArray:
        return ControlRigMetadataArrayString(
            Value.TransformArrayValue,
            [](const FTransform& Item)
            {
                return Item.ToString();
            });
    case ERigMetadataType::LinearColor:
        return Value.ColorValue.ToString();
    case ERigMetadataType::LinearColorArray:
        return ControlRigMetadataArrayString(
            Value.ColorArrayValue,
            [](const FLinearColor& Item)
            {
                return Item.ToString();
            });
    case ERigMetadataType::RigElementKey:
    {
        const TOptional<ERigElementType> ElementType =
            ParseControlRigElementType(Value.ElementType);
        const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
        return FString::Printf(
            TEXT("%s:%s"),
            ElementType.IsSet() && ElementTypeEnum
                ? *ElementTypeEnum->GetNameStringByValue(
                    static_cast<int64>(ElementType.GetValue()))
                : TEXT("Unknown"),
            *Value.ElementName);
    }
    case ERigMetadataType::RigElementKeyArray:
    {
        const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
        return ControlRigMetadataArrayString(
            ControlRigMetadataElementKeyArray(Value),
            [ElementTypeEnum](const FRigElementKey& Item)
            {
                return FString::Printf(
                    TEXT("%s:%s"),
                    ElementTypeEnum
                        ? *ElementTypeEnum->GetNameStringByValue(
                            static_cast<int64>(Item.Type))
                        : TEXT("Unknown"),
                    *Item.Name.ToString());
            });
    }
    default:
        return TEXT("Unsupported");
    }
}

FString ControlRigMetadataEntryString(
    const URigHierarchy* Hierarchy,
    const FRigElementKey& Key,
    const FName MetadataName)
{
    if (!Hierarchy || !Key.IsValid())
    {
        return TEXT("Missing");
    }
    const ERigMetadataType Type = Hierarchy->GetMetadataType(
        Key, MetadataName);
    const UEnum* MetadataTypeEnum = StaticEnum<ERigMetadataType>();
    FString Value;
    switch (Type)
    {
    case ERigMetadataType::Bool:
        Value = Hierarchy->GetBoolMetadata(Key, MetadataName, false)
            ? TEXT("True") : TEXT("False");
        break;
    case ERigMetadataType::BoolArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetBoolArrayMetadata(
                Key, MetadataName),
            [](const bool Item)
            {
                return Item ? FString(TEXT("True"))
                    : FString(TEXT("False"));
            });
        break;
    case ERigMetadataType::Float:
        Value = FString::SanitizeFloat(
            Hierarchy->GetFloatMetadata(Key, MetadataName, 0.0f));
        break;
    case ERigMetadataType::FloatArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetFloatArrayMetadata(
                Key, MetadataName),
            [](const float Item)
            {
                return FString::SanitizeFloat(Item);
            });
        break;
    case ERigMetadataType::Int32:
        Value = FString::FromInt(
            Hierarchy->GetInt32Metadata(Key, MetadataName, 0));
        break;
    case ERigMetadataType::Int32Array:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetInt32ArrayMetadata(
                Key, MetadataName),
            [](const int32 Item)
            {
                return FString::FromInt(Item);
            });
        break;
    case ERigMetadataType::Name:
        Value = Hierarchy->GetNameMetadata(
            Key, MetadataName, NAME_None).ToString();
        break;
    case ERigMetadataType::NameArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetNameArrayMetadata(
                Key, MetadataName),
            [](const FName Item)
            {
                return Item.ToString();
            });
        break;
    case ERigMetadataType::Vector:
        Value = Hierarchy->GetVectorMetadata(
            Key, MetadataName, FVector::ZeroVector).ToString();
        break;
    case ERigMetadataType::VectorArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetVectorArrayMetadata(
                Key, MetadataName),
            [](const FVector& Item)
            {
                return Item.ToString();
            });
        break;
    case ERigMetadataType::Rotator:
        Value = Hierarchy->GetRotatorMetadata(
            Key, MetadataName, FRotator::ZeroRotator).ToString();
        break;
    case ERigMetadataType::RotatorArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetRotatorArrayMetadata(
                Key, MetadataName),
            [](const FRotator& Item)
            {
                return Item.ToString();
            });
        break;
    case ERigMetadataType::Quat:
        Value = CanonicalControlRigMetadataQuat(
            Hierarchy->GetQuatMetadata(
                Key, MetadataName, FQuat::Identity)).ToString();
        break;
    case ERigMetadataType::QuatArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetQuatArrayMetadata(
                Key, MetadataName),
            [](const FQuat& Item)
            {
                return CanonicalControlRigMetadataQuat(Item).ToString();
            });
        break;
    case ERigMetadataType::Transform:
        Value = Hierarchy->GetTransformMetadata(
            Key, MetadataName, FTransform::Identity).ToString();
        break;
    case ERigMetadataType::TransformArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetTransformArrayMetadata(
                Key, MetadataName),
            [](const FTransform& Item)
            {
                return Item.ToString();
            });
        break;
    case ERigMetadataType::LinearColor:
        Value = Hierarchy->GetLinearColorMetadata(
            Key, MetadataName, FLinearColor::Transparent).ToString();
        break;
    case ERigMetadataType::LinearColorArray:
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetLinearColorArrayMetadata(
                Key, MetadataName),
            [](const FLinearColor& Item)
            {
                return Item.ToString();
            });
        break;
    case ERigMetadataType::RigElementKey:
    {
        const FRigElementKey MetadataKey =
            Hierarchy->GetRigElementKeyMetadata(
                Key, MetadataName, FRigElementKey());
        const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
        Value = FString::Printf(
            TEXT("%s:%s"),
            ElementTypeEnum
                ? *ElementTypeEnum->GetNameStringByValue(
                    static_cast<int64>(MetadataKey.Type))
                : TEXT("Unknown"),
            *MetadataKey.Name.ToString());
        break;
    }
    case ERigMetadataType::RigElementKeyArray:
    {
        const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
        Value = ControlRigMetadataArrayString(
            Hierarchy->GetRigElementKeyArrayMetadata(
                Key, MetadataName),
            [ElementTypeEnum](const FRigElementKey& Item)
            {
                return FString::Printf(
                    TEXT("%s:%s"),
                    ElementTypeEnum
                        ? *ElementTypeEnum->GetNameStringByValue(
                            static_cast<int64>(Item.Type))
                        : TEXT("Unknown"),
                    *Item.Name.ToString());
            });
        break;
    }
    default:
        return TEXT("Missing");
    }
    return FString::Printf(
        TEXT("Type=%s|Value=%s"),
        MetadataTypeEnum
            ? *MetadataTypeEnum->GetNameStringByValue(
                static_cast<int64>(Type))
            : TEXT("Unknown"),
        *Value);
}

FString ControlRigMetadataListString(
    const URigHierarchy* Hierarchy,
    const FRigElementKey& Key)
{
    if (!Hierarchy || !Key.IsValid())
    {
        return FString();
    }
    TArray<FName> Names = Hierarchy->GetMetadataNames(Key);
    Names.Sort([](const FName& A, const FName& B)
    {
        return A.LexicalLess(B);
    });
    TArray<FString> Entries;
    for (const FName Name : Names)
    {
        Entries.Add(FString::Printf(
            TEXT("%s{%s}"),
            *Name.ToString(),
            *ControlRigMetadataEntryString(Hierarchy, Key, Name)));
    }
    return FString::Join(Entries, TEXT(","));
}

bool SetControlRigMetadataValue(
    URigHierarchy* Hierarchy,
    const FRigElementKey& Key,
    const FName MetadataName,
    const ERigMetadataType Type,
    const FThomasControlRigValueSpec& Value)
{
    if (!Hierarchy)
    {
        return false;
    }
    switch (Type)
    {
    case ERigMetadataType::Bool:
        return Hierarchy->SetBoolMetadata(
            Key, MetadataName, Value.bBoolValue);
    case ERigMetadataType::BoolArray:
        return Hierarchy->SetBoolArrayMetadata(
            Key, MetadataName, Value.BoolArrayValue);
    case ERigMetadataType::Float:
        return Hierarchy->SetFloatMetadata(
            Key, MetadataName, Value.FloatValue);
    case ERigMetadataType::FloatArray:
        return Hierarchy->SetFloatArrayMetadata(
            Key, MetadataName, Value.FloatArrayValue);
    case ERigMetadataType::Int32:
        return Hierarchy->SetInt32Metadata(
            Key, MetadataName, Value.IntegerValue);
    case ERigMetadataType::Int32Array:
        return Hierarchy->SetInt32ArrayMetadata(
            Key, MetadataName, Value.IntegerArrayValue);
    case ERigMetadataType::Name:
        return Hierarchy->SetNameMetadata(
            Key, MetadataName, FName(*Value.NameValue));
    case ERigMetadataType::NameArray:
        return Hierarchy->SetNameArrayMetadata(
            Key, MetadataName, ControlRigMetadataNameArray(Value));
    case ERigMetadataType::Vector:
        return Hierarchy->SetVectorMetadata(
            Key, MetadataName, Value.VectorValue);
    case ERigMetadataType::VectorArray:
        return Hierarchy->SetVectorArrayMetadata(
            Key, MetadataName, Value.VectorArrayValue);
    case ERigMetadataType::Rotator:
        return Hierarchy->SetRotatorMetadata(
            Key, MetadataName, Value.Rotation);
    case ERigMetadataType::RotatorArray:
        return Hierarchy->SetRotatorArrayMetadata(
            Key, MetadataName, Value.RotatorArrayValue);
    case ERigMetadataType::Quat:
        return Hierarchy->SetQuatMetadata(
            Key,
            MetadataName,
            CanonicalControlRigMetadataQuat(Value.Quaternion));
    case ERigMetadataType::QuatArray:
    {
        TArray<FQuat> Quaternions;
        Quaternions.Reserve(Value.QuaternionArrayValue.Num());
        for (const FQuat& Item : Value.QuaternionArrayValue)
        {
            Quaternions.Add(CanonicalControlRigMetadataQuat(Item));
        }
        return Hierarchy->SetQuatArrayMetadata(
            Key, MetadataName, Quaternions);
    }
    case ERigMetadataType::Transform:
        return Hierarchy->SetTransformMetadata(
            Key,
            MetadataName,
            FTransform(Value.Rotation, Value.Location, Value.Scale));
    case ERigMetadataType::TransformArray:
        return Hierarchy->SetTransformArrayMetadata(
            Key, MetadataName, Value.TransformArrayValue);
    case ERigMetadataType::LinearColor:
        return Hierarchy->SetLinearColorMetadata(
            Key, MetadataName, Value.ColorValue);
    case ERigMetadataType::LinearColorArray:
        return Hierarchy->SetLinearColorArrayMetadata(
            Key, MetadataName, Value.ColorArrayValue);
    case ERigMetadataType::RigElementKey:
        return Hierarchy->SetRigElementKeyMetadata(
            Key,
            MetadataName,
            FRigElementKey(
                FName(*Value.ElementName),
                ParseControlRigElementType(
                    Value.ElementType).GetValue()));
    case ERigMetadataType::RigElementKeyArray:
        return Hierarchy->SetRigElementKeyArrayMetadata(
            Key,
            MetadataName,
            ControlRigMetadataElementKeyArray(Value));
    default:
        return false;
    }
}

struct FControlRigTypedControlPreflightError
{
    FString Code;
    FString Message;
};

TOptional<FControlRigTypedControlPreflightError>
ValidateControlRigTypedControlOperation(
    const FThomasAnimationOperation& Operation,
    UControlRigRuntimeAsset* ControlRigAsset,
    URigHierarchy* Hierarchy,
    URigHierarchyController* Controller,
    const TArray<FRigElementKey>& Existing,
    const TArray<FRigElementKey>& AllKeys,
    const bool bAdd,
    const bool bFloatControlAction)
{
    const FString Action = Operation.Action.TrimStartAndEnd().ToLower();
    const bool bShapeSettingsAction = Action
        == TEXT("set_control_rig_control_shape_settings");
    const bool bShapeTransformAction = Action
        == TEXT("set_control_rig_control_shape_transform");
    const bool bLimitSettingsAction = Action
        == TEXT("set_control_rig_control_limits");
    const TOptional<ERigControlType> RequestedControlType =
        bFloatControlAction
            ? TOptional<ERigControlType>(ERigControlType::Float)
            : ParseControlRigControlType(Operation.ControlType);
    if (!RequestedControlType.IsSet())
    {
        return FControlRigTypedControlPreflightError{
            TEXT("invalid_control_rig_control_type"),
            Operation.ControlType};
    }
    if ((bAdd || bShapeSettingsAction)
        && (!FMath::IsFinite(Operation.Color.R)
            || !FMath::IsFinite(Operation.Color.G)
            || !FMath::IsFinite(Operation.Color.B)
            || !FMath::IsFinite(Operation.Color.A)
            || Operation.Color.R < 0.0f
            || Operation.Color.R > 1.0f
            || Operation.Color.G < 0.0f
            || Operation.Color.G > 1.0f
            || Operation.Color.B < 0.0f
            || Operation.Color.B > 1.0f
            || Operation.Color.A < 0.0f
            || Operation.Color.A > 1.0f))
    {
        return FControlRigTypedControlPreflightError{
            TEXT("invalid_control_rig_shape_color"),
            Operation.Color.ToString()};
    }
    if (bShapeSettingsAction
        && !Operation.ControlShapeName.IsEmpty()
        && !IsSafeGraphName(Operation.ControlShapeName))
    {
        return FControlRigTypedControlPreflightError{
            TEXT("invalid_control_rig_shape_name"),
            Operation.ControlShapeName};
    }
    if (bShapeSettingsAction
        && !Operation.ControlShapeName.IsEmpty()
        && !IsControlRigShapeAvailable(
            ControlRigAsset,
            FName(*Operation.ControlShapeName)))
    {
        return FControlRigTypedControlPreflightError{
            TEXT("control_rig_shape_not_found"),
            Operation.ControlShapeName};
    }
    if (bShapeTransformAction)
    {
        const FTransform ShapeTransform(
            Operation.Rotation,
            Operation.Location,
            Operation.Scale);
        if (!ShapeTransform.IsValid()
            || Operation.Location.GetAbsMax() > 1000000.0
            || Operation.Rotation.Euler().GetAbsMax() > 360000.0
            || Operation.Scale.GetAbsMax() > 10000.0
            || Operation.Scale.GetAbsMin() < 0.0001)
        {
            return FControlRigTypedControlPreflightError{
                TEXT("invalid_control_rig_shape_transform"),
                ShapeTransform.ToString()};
        }
    }
    if (bLimitSettingsAction)
    {
        int32 ChannelCount = 0;
        if (!HasValidControlRigLimitChannelCounts(
                Operation,
                RequestedControlType.GetValue(),
                ChannelCount))
        {
            return FControlRigTypedControlPreflightError{
                TEXT("invalid_control_rig_limit_channels"),
                FString::Printf(
                    TEXT("Expected=%d Minimum=%d Maximum=%d"),
                    ChannelCount,
                    Operation.ControlMinimumLimitEnabledPerChannel.Num(),
                    Operation.ControlMaximumLimitEnabledPerChannel.Num())};
        }
    }
    if (bLimitSettingsAction
        && (!IsValidControlRigControlValue(
                Operation.ControlMinimumValue,
                RequestedControlType.GetValue())
            || !IsValidControlRigControlValue(
                Operation.ControlMaximumValue,
                RequestedControlType.GetValue())
            || !AreControlRigLimitsOrdered(
                Operation.ControlMinimumValue,
                Operation.ControlMaximumValue,
                RequestedControlType.GetValue())))
    {
        return FControlRigTypedControlPreflightError{
            TEXT("invalid_control_rig_control_limits"),
            Operation.ControlType};
    }
    if ((bAdd
            || Action == TEXT("set_control_rig_float_value")
            || Action == TEXT("set_control_rig_control_value"))
        && !IsValidControlRigControlValue(
            Operation,
            RequestedControlType.GetValue()))
    {
        return FControlRigTypedControlPreflightError{
            TEXT("invalid_control_rig_control_value"),
            Operation.ControlType};
    }
    if (bAdd && !Operation.ParentName.IsEmpty())
    {
        TArray<FRigElementKey> ParentMatches;
        for (const FRigElementKey& Key : AllKeys)
        {
            if (Key.Name == FName(*Operation.ParentName)
                && (Key.Type == ERigElementType::Bone
                    || Key.Type == ERigElementType::Null))
            {
                ParentMatches.Add(Key);
            }
        }
        if (ParentMatches.Num() != 1)
        {
            return FControlRigTypedControlPreflightError{
                TEXT("invalid_or_ambiguous_control_rig_parent"),
                Operation.ParentName};
        }
    }
    if (!bAdd)
    {
        const FRigControlSettings Settings =
            Controller->GetControlSettings(Existing[0]);
        FString CurrentValue;
        if (bShapeSettingsAction)
        {
            CurrentValue = ControlRigControlShapeSettingsString(Settings);
        }
        else if (bLimitSettingsAction)
        {
            CurrentValue = ControlRigControlLimitSettingsString(Settings);
        }
        else if (bShapeTransformAction)
        {
            FRigControlElement* ControlElement =
                Hierarchy->Find<FRigControlElement>(Existing[0]);
            if (!ControlElement)
            {
                return FControlRigTypedControlPreflightError{
                    TEXT("invalid_control_rig_element"),
                    Operation.Name};
            }
            CurrentValue = Hierarchy->GetControlShapeTransform(
                ControlElement,
                ERigTransformType::InitialLocal).ToString();
        }
        else
        {
            CurrentValue = ControlRigControlValueString(
                Hierarchy,
                Existing[0],
                Settings.ControlType,
                ERigControlValueType::Initial);
        }
        if (Settings.ControlType != RequestedControlType.GetValue()
            || Operation.ExpectedValue != CurrentValue)
        {
            return FControlRigTypedControlPreflightError{
                TEXT("control_rig_value_conflict"),
                CurrentValue};
        }
    }
    return {};
}

bool ControlRigControlValueMatches(
    const URigHierarchy* Hierarchy,
    const FRigElementKey& Key,
    const ERigControlType ControlType,
    const ERigControlValueType ValueType,
    const FThomasAnimationOperation& Operation)
{
    if (!Hierarchy || !Key.IsValid())
    {
        return false;
    }
    switch (ControlType)
    {
    case ERigControlType::Bool:
        return Hierarchy->GetControlValue<bool>(Key, ValueType)
            == Operation.bControlBoolValue;
    case ERigControlType::Float:
    case ERigControlType::ScaleFloat:
        return FMath::IsNearlyEqual(
            Hierarchy->GetControlValue<float>(Key, ValueType),
            Operation.Value);
    case ERigControlType::Integer:
        return Hierarchy->GetControlValue<int32>(Key, ValueType)
            == Operation.ControlIntegerValue;
    case ERigControlType::Vector2D:
        return URigHierarchy::GetVector2DFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType))
            .Equals(Operation.ControlVector2DValue, KINDA_SMALL_NUMBER);
    case ERigControlType::Position:
    case ERigControlType::Scale:
        return URigHierarchy::GetVectorFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType))
            .Equals(Operation.ControlVectorValue, KINDA_SMALL_NUMBER);
    case ERigControlType::Rotator:
    {
        const FRotator Readback =
            URigHierarchy::GetRotatorFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType));
        return Readback.Equals(Operation.Rotation, 0.01);
    }
    case ERigControlType::Transform:
        return URigHierarchy::GetTransformFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType))
            .Equals(
                FTransform(
                    Operation.Rotation,
                    Operation.Location,
                    Operation.Scale),
                KINDA_SMALL_NUMBER);
    case ERigControlType::TransformNoScale:
        return URigHierarchy::GetTransformNoScaleFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType))
            .ToFTransform()
            .Equals(
                FTransform(
                    Operation.Rotation,
                    Operation.Location,
                    FVector::OneVector),
                KINDA_SMALL_NUMBER);
    case ERigControlType::EulerTransform:
        return URigHierarchy::GetEulerTransformFromControlValue(
                Hierarchy->GetControlValue(Key, ValueType))
            .Equals(
                FEulerTransform(
                    Operation.Location,
                    Operation.Rotation,
                    Operation.Scale),
                0.01);
    default:
        return false;
    }
}

FString RetargetSideName(const ERetargetSourceOrTarget Side)
{
    return Side == ERetargetSourceOrTarget::Source
        ? TEXT("source") : TEXT("target");
}

FString RetargetRigPath(
    const UIKRetargeterController* Controller,
    const ERetargetSourceOrTarget Side)
{
    const UIKRigDefinition* Rig = Controller
        ? Controller->GetIKRig(Side) : nullptr;
    return Rig ? Rig->GetOutermost()->GetName() : TEXT("None");
}

TArray<FString> AvailableIKRetargetOpTypePaths()
{
    TArray<FString> Result;
    for (TObjectIterator<UScriptStruct> It; It; ++It)
    {
        UScriptStruct* Candidate = *It;
        if (Candidate
            && Candidate != FIKRetargetOpBase::StaticStruct()
            && Candidate->IsChildOf(FIKRetargetOpBase::StaticStruct())
            && Candidate->GetPathName().StartsWith(TEXT("/Script/IKRig.")))
        {
            Result.AddUnique(Candidate->GetPathName());
        }
    }
    Result.Sort();
    return Result;
}

const UScriptStruct* ResolveIKRetargetOpType(const FString& TypePath)
{
    if (TypePath.IsEmpty() || TypePath.Len() > 200
        || !AvailableIKRetargetOpTypePaths().Contains(TypePath))
    {
        return nullptr;
    }
    UScriptStruct* Type = LoadObject<UScriptStruct>(nullptr, *TypePath);
    return Type && Type->IsChildOf(FIKRetargetOpBase::StaticStruct())
        ? Type : nullptr;
}

TArray<FString> AvailableIKRigSolverTypePaths()
{
    TArray<FString> Result;
    for (TObjectIterator<UScriptStruct> It; It; ++It)
    {
        UScriptStruct* Candidate = *It;
        if (Candidate
            && Candidate != FIKRigSolverBase::StaticStruct()
            && Candidate->IsChildOf(FIKRigSolverBase::StaticStruct())
            && Candidate->GetPathName().StartsWith(TEXT("/Script/IKRig.")))
        {
            Result.AddUnique(Candidate->GetPathName());
        }
    }
    Result.Sort();
    return Result;
}

bool IsAvailableIKRigSolverTypePath(const FString& TypePath)
{
    return !TypePath.IsEmpty()
        && TypePath.Len() <= 200
        && AvailableIKRigSolverTypePaths().Contains(TypePath);
}

TOptional<EAxis::Type> ParseIKAxis(const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("x")) return EAxis::X;
    if (Value == TEXT("y")) return EAxis::Y;
    if (Value == TEXT("z")) return EAxis::Z;
    return TOptional<EAxis::Type>();
}

FString IKAxisName(const EAxis::Type Axis)
{
    switch (Axis)
    {
    case EAxis::X: return TEXT("x");
    case EAxis::Y: return TEXT("y");
    case EAxis::Z: return TEXT("z");
    default: return TEXT("none");
    }
}

enum class EIKRigSettingScope : uint8
{
    Solver,
    Goal,
    Bone
};

struct FIKRigSettingsSnapshot
{
    TSharedPtr<FStructOnScope> Data;
    const UScriptStruct* Type = nullptr;
};

void AddBounded(
    FThomasSpecializedAssetInspectionResult& Result,
    const FString& Item,
    int32 MaxItems);

FName IKSettingsGetterName(const EIKRigSettingScope Scope)
{
    switch (Scope)
    {
    case EIKRigSettingScope::Solver: return TEXT("GetSolverSettings");
    case EIKRigSettingScope::Goal: return TEXT("GetGoalSettings");
    case EIKRigSettingScope::Bone: return TEXT("GetBoneSettings");
    default: return NAME_None;
    }
}

FName IKSettingsSetterName(const EIKRigSettingScope Scope)
{
    switch (Scope)
    {
    case EIKRigSettingScope::Solver: return TEXT("SetSolverSettings");
    case EIKRigSettingScope::Goal: return TEXT("SetGoalSettings");
    case EIKRigSettingScope::Bone: return TEXT("SetBoneSettings");
    default: return NAME_None;
    }
}

FString IKSettingScopeName(const EIKRigSettingScope Scope)
{
    switch (Scope)
    {
    case EIKRigSettingScope::Solver: return TEXT("solver");
    case EIKRigSettingScope::Goal: return TEXT("goal");
    case EIKRigSettingScope::Bone: return TEXT("bone");
    default: return TEXT("unknown");
    }
}

bool InvokeIKRigSettingsGetter(
    UObject* SolverController,
    const EIKRigSettingScope Scope,
    const FName Subject,
    FIKRigSettingsSnapshot& OutSnapshot)
{
    OutSnapshot = FIKRigSettingsSnapshot();
    UFunction* Function = SolverController
        ? SolverController->FindFunction(IKSettingsGetterName(Scope)) : nullptr;
    if (!Function)
    {
        return false;
    }
    FNameProperty* SubjectProperty = nullptr;
    FStructProperty* ReturnProperty = nullptr;
    int32 InputCount = 0;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property->HasAnyPropertyFlags(CPF_Parm))
        {
            continue;
        }
        if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            ReturnProperty = CastField<FStructProperty>(Property);
            if (!ReturnProperty)
            {
                return false;
            }
            continue;
        }
        ++InputCount;
        SubjectProperty = CastField<FNameProperty>(Property);
        if (!SubjectProperty)
        {
            return false;
        }
    }
    const bool bNeedsSubject = Scope != EIKRigSettingScope::Solver;
    if ((bNeedsSubject && (InputCount != 1 || !SubjectProperty || Subject.IsNone()))
        || (!bNeedsSubject && InputCount != 0)
        || !ReturnProperty || !ReturnProperty->Struct)
    {
        return false;
    }
    FStructOnScope Params(Function);
    if (SubjectProperty)
    {
        SubjectProperty->SetPropertyValue_InContainer(
            Params.GetStructMemory(), Subject);
    }
    SolverController->ProcessEvent(Function, Params.GetStructMemory());
    OutSnapshot.Type = ReturnProperty->Struct;
    OutSnapshot.Data = MakeShared<FStructOnScope>(ReturnProperty->Struct);
    ReturnProperty->Struct->CopyScriptStruct(
        OutSnapshot.Data->GetStructMemory(),
        ReturnProperty->ContainerPtrToValuePtr<void>(
            Params.GetStructMemory()));
    return true;
}

bool InvokeIKRigSettingsSetter(
    UObject* SolverController,
    const EIKRigSettingScope Scope,
    const FName Subject,
    const FIKRigSettingsSnapshot& Snapshot)
{
    UFunction* Function = SolverController
        ? SolverController->FindFunction(IKSettingsSetterName(Scope)) : nullptr;
    if (!Function || !Snapshot.Type || !Snapshot.Data.IsValid())
    {
        return false;
    }
    FNameProperty* SubjectProperty = nullptr;
    FStructProperty* SettingsProperty = nullptr;
    int32 InputCount = 0;
    for (TFieldIterator<FProperty> It(Function); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property->HasAnyPropertyFlags(CPF_Parm)
            || Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            continue;
        }
        ++InputCount;
        if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            if (SubjectProperty)
            {
                return false;
            }
            SubjectProperty = NameProperty;
        }
        else if (FStructProperty* StructProperty =
                     CastField<FStructProperty>(Property))
        {
            if (SettingsProperty || StructProperty->Struct != Snapshot.Type)
            {
                return false;
            }
            SettingsProperty = StructProperty;
        }
        else
        {
            return false;
        }
    }
    const bool bNeedsSubject = Scope != EIKRigSettingScope::Solver;
    const int32 ExpectedInputCount = bNeedsSubject ? 2 : 1;
    if (InputCount != ExpectedInputCount || !SettingsProperty
        || (bNeedsSubject && (!SubjectProperty || Subject.IsNone()))
        || (!bNeedsSubject && SubjectProperty))
    {
        return false;
    }
    FStructOnScope Params(Function);
    if (SubjectProperty)
    {
        SubjectProperty->SetPropertyValue_InContainer(
            Params.GetStructMemory(), Subject);
    }
    Snapshot.Type->CopyScriptStruct(
        SettingsProperty->ContainerPtrToValuePtr<void>(
            Params.GetStructMemory()),
        Snapshot.Data->GetStructMemory());
    SolverController->ProcessEvent(Function, Params.GetStructMemory());
    return true;
}

bool IsSupportedIKRigSettingProperty(const FProperty* Property)
{
    if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_EditConst | CPF_Deprecated))
    {
        return false;
    }
    if (Property->IsA<FBoolProperty>()
        || Property->IsA<FNumericProperty>()
        || Property->IsA<FEnumProperty>()
        || Property->IsA<FNameProperty>()
        || Property->IsA<FStrProperty>())
    {
        return true;
    }
    const FStructProperty* StructProperty =
        CastField<const FStructProperty>(Property);
    if (!StructProperty || !StructProperty->Struct)
    {
        return false;
    }
    static const TSet<FName> AllowedStructs = {
        TEXT("Vector"), TEXT("Vector2D"), TEXT("Vector4"),
        TEXT("Rotator"), TEXT("Quat"), TEXT("LinearColor"),
        TEXT("Color"), TEXT("Transform")
    };
    return AllowedStructs.Contains(StructProperty->Struct->GetFName());
}

FProperty* FindIKRigSettingProperty(
    const FIKRigSettingsSnapshot& Snapshot,
    const FString& PropertyName)
{
    if (!Snapshot.Type || PropertyName.IsEmpty() || PropertyName.Len() > 100
        || PropertyName.Contains(TEXT("."))
        || PropertyName.Contains(TEXT("[")))
    {
        return nullptr;
    }
    FProperty* Property = FindFProperty<FProperty>(
        Snapshot.Type, FName(*PropertyName));
    return IsSupportedIKRigSettingProperty(Property) ? Property : nullptr;
}

FString ExportIKRigSettingValue(
    const FIKRigSettingsSnapshot& Snapshot,
    const FProperty* Property)
{
    FString Value;
    if (Snapshot.Data.IsValid() && Property)
    {
        Property->ExportTextItem_Direct(
            Value,
            Property->ContainerPtrToValuePtr<void>(
                Snapshot.Data->GetStructMemory()),
            nullptr, nullptr, PPF_None);
    }
    return Value;
}

bool IsFiniteIKRigSettingValue(
    const FProperty* Property,
    const void* PropertyValue)
{
    const FNumericProperty* Numeric =
        CastField<const FNumericProperty>(Property);
    if (!Numeric || Numeric->IsInteger())
    {
        return true;
    }
    return FMath::IsFinite(
        Numeric->GetFloatingPointPropertyValue(PropertyValue));
}

bool PassesIKRigSettingClamp(
    const FProperty* Property,
    const void* PropertyValue)
{
    const FNumericProperty* Numeric =
        CastField<const FNumericProperty>(Property);
    if (!Numeric)
    {
        return true;
    }
    const double NumericValue = Numeric->IsInteger()
        ? (Property->IsA<FByteProperty>()
            || Property->IsA<FUInt16Property>()
            || Property->IsA<FUInt32Property>()
            || Property->IsA<FUInt64Property>()
                ? static_cast<double>(
                    Numeric->GetUnsignedIntPropertyValue(PropertyValue))
                : static_cast<double>(
                    Numeric->GetSignedIntPropertyValue(PropertyValue)))
        : Numeric->GetFloatingPointPropertyValue(PropertyValue);
    auto PassesBound = [&](const TCHAR* MetadataName, const bool bMinimum)
    {
        const FString BoundText = Property->GetMetaData(MetadataName);
        double Bound = 0.0;
        return BoundText.IsEmpty()
            || (LexTryParseString(Bound, *BoundText)
                && (bMinimum ? NumericValue >= Bound : NumericValue <= Bound));
    };
    return PassesBound(TEXT("ClampMin"), true)
        && PassesBound(TEXT("ClampMax"), false);
}

bool ImportIKRigSettingValue(
    FIKRigSettingsSnapshot& Snapshot,
    FProperty* Property,
    const FString& Input,
    FString& OutCanonicalValue)
{
    if (!Snapshot.Data.IsValid() || !Property || Input.Len() > 512
        || Input.Contains(TEXT("nan"), ESearchCase::IgnoreCase)
        || Input.Contains(TEXT("inf"), ESearchCase::IgnoreCase))
    {
        return false;
    }
    void* PropertyValue = Property->ContainerPtrToValuePtr<void>(
        Snapshot.Data->GetStructMemory());
    const TCHAR* ImportEnd = Property->ImportText_Direct(
        *Input, PropertyValue, nullptr, PPF_None);
    if (!ImportEnd
        || !FString(ImportEnd).TrimStartAndEnd().IsEmpty()
        || !IsFiniteIKRigSettingValue(Property, PropertyValue)
        || !PassesIKRigSettingClamp(Property, PropertyValue))
    {
        return false;
    }
    OutCanonicalValue = ExportIKRigSettingValue(Snapshot, Property);
    return OutCanonicalValue.Len() <= 512;
}

bool GetIKRetargetOpSettingsSnapshot(
    UIKRetargeterController* Controller,
    const FName OpName,
    FIKRigSettingsSnapshot& OutSnapshot)
{
    OutSnapshot = FIKRigSettingsSnapshot();
    FIKRetargetOpBase* Op = Controller
        ? Controller->GetRetargetOpByName(OpName) : nullptr;
    const UScriptStruct* SettingsType = Op ? Op->GetSettingsType() : nullptr;
    const FIKRetargetOpSettingsBase* Settings = Op ? Op->GetSettings() : nullptr;
    if (!SettingsType || !Settings)
    {
        return false;
    }
    OutSnapshot.Type = SettingsType;
    OutSnapshot.Data = MakeShared<FStructOnScope>(SettingsType);
    SettingsType->CopyScriptStruct(
        OutSnapshot.Data->GetStructMemory(), Settings);
    return true;
}

FString CurrentIKRetargetPropertyValue(
    UIKRetargeterController* Controller,
    const FName OverrideSetName,
    const FName OpName,
    const FString& PropertyName,
    const FIKRigSettingsSnapshot& BaseSettings,
    const FProperty* Property)
{
    const FRetargetOpPropertyOverride* Existing = nullptr;
    if (Controller)
    {
        if (const FRetargetOverrideSet* OverrideSet =
                Controller->GetAllOverrideSets().Find(OverrideSetName))
        {
            for (const FRetargetOpOverrides& OpOverrides :
                 OverrideSet->OpOverrides)
            {
                if (OpOverrides.OpName != OpName)
                {
                    continue;
                }
                for (const FRetargetOpPropertyOverride& PropertyOverride :
                     OpOverrides.PropertyOverrides)
                {
                    if (PropertyOverride.GetPropertyPath() == PropertyName)
                    {
                        Existing = &PropertyOverride;
                        break;
                    }
                }
                break;
            }
        }
    }
    if (Existing)
    {
        return Existing->GetValueString();
    }
    return ExportIKRigSettingValue(BaseSettings, Property);
}

bool ApplyIKRetargetPropertyOverride(
    UIKRetargeterController* Controller,
    const FName OverrideSetName,
    const FName OpName,
    const FString& PropertyName,
    const FString& ExpectedValue,
    const FString& NewValue)
{
    FIKRigSettingsSnapshot Snapshot;
    if (!Controller
        || !GetIKRetargetOpSettingsSnapshot(Controller, OpName, Snapshot))
    {
        return false;
    }
    FProperty* Property = FindIKRigSettingProperty(Snapshot, PropertyName);
    if (!Property
        || CurrentIKRetargetPropertyValue(
            Controller,
            OverrideSetName,
            OpName,
            PropertyName,
            Snapshot,
            Property) != ExpectedValue)
    {
        return false;
    }
    FString CanonicalValue;
    if (!ImportIKRigSettingValue(
            Snapshot, Property, NewValue, CanonicalValue))
    {
        return false;
    }
    if (!Controller->HasPropertyOverride(
            OverrideSetName, OpName, PropertyName)
        && !Controller->AddPropertyOverrideToOp(
            OverrideSetName, OpName, PropertyName))
    {
        return false;
    }
    if (!Controller->UpdateOverrideValue(
            OverrideSetName,
            OpName,
            PropertyName,
            *Snapshot.Data,
            false))
    {
        return false;
    }
    FIKRigSettingsSnapshot ReadbackSettings;
    FProperty* ReadbackProperty = nullptr;
    return GetIKRetargetOpSettingsSnapshot(
            Controller, OpName, ReadbackSettings)
        && (ReadbackProperty = FindIKRigSettingProperty(
                ReadbackSettings, PropertyName))
        && CurrentIKRetargetPropertyValue(
                Controller,
                OverrideSetName,
                OpName,
                PropertyName,
                ReadbackSettings,
                ReadbackProperty) == CanonicalValue;
}

void AddIKRetargetOpSettingsInspection(
    FThomasSpecializedAssetInspectionResult& Result,
    UIKRetargeterController* Controller,
    const int32 OpIndex,
    const int32 MaxItems)
{
    const FName OpName = Controller
        ? Controller->GetOpName(OpIndex) : NAME_None;
    FIKRigSettingsSnapshot Snapshot;
    if (!GetIKRetargetOpSettingsSnapshot(
            Controller, OpName, Snapshot))
    {
        return;
    }
    for (TFieldIterator<FProperty> It(Snapshot.Type); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsSupportedIKRigSettingProperty(Property))
        {
            continue;
        }
        AddBounded(Result, FString::Printf(
            TEXT("IKRetargetOpSetting Index=%d Op=%s Type=%s Property=%s Value=%s"),
            OpIndex,
            *OpName.ToString(),
            *Snapshot.Type->GetPathName(),
            *Property->GetName(),
            *ExportIKRigSettingValue(Snapshot, Property)),
            MaxItems);
    }
}

bool GetIKRigSettingsSnapshot(
    UIKRigController* Controller,
    const EIKRigSettingScope Scope,
    const int32 SolverIndex,
    const FName Subject,
    FIKRigSettingsSnapshot& OutSnapshot)
{
    UObject* SolverController = Controller
        ? Controller->GetSolverController(SolverIndex) : nullptr;
    if (!SolverController)
    {
        return false;
    }
    if (Scope == EIKRigSettingScope::Goal
        && !Controller->IsGoalConnectedToSolver(Subject, SolverIndex))
    {
        return false;
    }
    if (Scope == EIKRigSettingScope::Bone
        && !Controller->CanRemoveBoneSetting(Subject, SolverIndex))
    {
        return false;
    }
    return InvokeIKRigSettingsGetter(
        SolverController, Scope, Subject, OutSnapshot);
}

bool ApplyIKRigSetting(
    UIKRigController* Controller,
    const EIKRigSettingScope Scope,
    const int32 SolverIndex,
    const FName Subject,
    const FString& PropertyName,
    const FString& ExpectedValue,
    const FString& NewValue)
{
    FIKRigSettingsSnapshot Snapshot;
    if (!GetIKRigSettingsSnapshot(
            Controller, Scope, SolverIndex, Subject, Snapshot))
    {
        return false;
    }
    FProperty* Property = FindIKRigSettingProperty(
        Snapshot, PropertyName);
    if (!Property
        || ExportIKRigSettingValue(Snapshot, Property) != ExpectedValue)
    {
        return false;
    }
    FString CanonicalValue;
    if (!ImportIKRigSettingValue(
            Snapshot, Property, NewValue, CanonicalValue))
    {
        return false;
    }
    UObject* SolverController = Controller->GetSolverController(SolverIndex);
    if (!InvokeIKRigSettingsSetter(
            SolverController, Scope, Subject, Snapshot))
    {
        return false;
    }
    FIKRigSettingsSnapshot Readback;
    FProperty* ReadbackProperty = nullptr;
    return GetIKRigSettingsSnapshot(
            Controller, Scope, SolverIndex, Subject, Readback)
        && (ReadbackProperty = FindIKRigSettingProperty(
                Readback, PropertyName))
        && ExportIKRigSettingValue(Readback, ReadbackProperty)
            == CanonicalValue;
}

void AddIKRigSettingsInspection(
    FThomasSpecializedAssetInspectionResult& Result,
    UIKRigController* Controller,
    const EIKRigSettingScope Scope,
    const int32 SolverIndex,
    const FName Subject,
    const int32 MaxItems)
{
    FIKRigSettingsSnapshot Snapshot;
    if (!GetIKRigSettingsSnapshot(
            Controller, Scope, SolverIndex, Subject, Snapshot))
    {
        return;
    }
    for (TFieldIterator<FProperty> It(Snapshot.Type); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsSupportedIKRigSettingProperty(Property))
        {
            continue;
        }
        AddBounded(Result, FString::Printf(
            TEXT("IKRigSetting Scope=%s Index=%d Subject=%s Type=%s Property=%s Value=%s"),
            *IKSettingScopeName(Scope),
            SolverIndex,
            Subject.IsNone() ? TEXT("-") : *Subject.ToString(),
            *Snapshot.Type->GetPathName(),
            *Property->GetName(),
            *ExportIKRigSettingValue(Snapshot, Property)),
            MaxItems);
    }
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

TOptional<ERootMotionRootLock::Type> ParseRootMotionLock(const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("ref_pose")) return ERootMotionRootLock::RefPose;
    if (Value == TEXT("anim_first_frame")) return ERootMotionRootLock::AnimFirstFrame;
    if (Value == TEXT("zero")) return ERootMotionRootLock::Zero;
    return TOptional<ERootMotionRootLock::Type>();
}

TOptional<EAdditiveAnimationType> ParseAdditiveType(const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("none")) return AAT_None;
    if (Value == TEXT("local_space")) return AAT_LocalSpaceBase;
    if (Value == TEXT("mesh_space_rotation")) return AAT_RotationOffsetMeshSpace;
    return TOptional<EAdditiveAnimationType>();
}

TOptional<EAdditiveBasePoseType> ParseAdditiveBasePoseType(const FString& Input)
{
    const FString Value = Input.TrimStartAndEnd().ToLower();
    if (Value == TEXT("none")) return ABPT_None;
    if (Value == TEXT("ref_pose")) return ABPT_RefPose;
    if (Value == TEXT("anim_scaled")) return ABPT_AnimScaled;
    if (Value == TEXT("anim_frame")) return ABPT_AnimFrame;
    if (Value == TEXT("local_anim_frame")) return ABPT_LocalAnimFrame;
    return TOptional<EAdditiveBasePoseType>();
}

FString AdditiveTypeName(const EAdditiveAnimationType Type)
{
    switch (Type)
    {
    case AAT_None: return TEXT("none");
    case AAT_LocalSpaceBase: return TEXT("local_space");
    case AAT_RotationOffsetMeshSpace: return TEXT("mesh_space_rotation");
    default: return TEXT("unknown");
    }
}

FString AdditiveBasePoseTypeName(const EAdditiveBasePoseType Type)
{
    switch (Type)
    {
    case ABPT_None: return TEXT("none");
    case ABPT_RefPose: return TEXT("ref_pose");
    case ABPT_AnimScaled: return TEXT("anim_scaled");
    case ABPT_AnimFrame: return TEXT("anim_frame");
    case ABPT_LocalAnimFrame: return TEXT("local_anim_frame");
    default: return TEXT("unknown");
    }
}

int32 FindMontageSection(const UAnimMontage& Montage, const FString& Name)
{
    return Montage.GetSectionIndex(FName(Name));
}

int32 FindMontageSlot(const UAnimMontage& Montage, const FString& Name)
{
    return Montage.SlotAnimTracks.IndexOfByPredicate([&](const FSlotAnimationTrack& Slot)
    {
        return Slot.SlotName == FName(Name);
    });
}

UAnimationGraph* FindRootAnimGraph(UAnimBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return nullptr;
    }
    TArray<UEdGraph*> RootGraphs;
    RootGraphs.Append(Blueprint->FunctionGraphs);
    RootGraphs.Append(Blueprint->UbergraphPages);
    RootGraphs.Append(Blueprint->MacroGraphs);
    for (UEdGraph* Graph : RootGraphs)
    {
        if (Graph && Graph->GetClass() == UAnimationGraph::StaticClass())
        {
            return Cast<UAnimationGraph>(Graph);
        }
    }
    return nullptr;
}

void GetStateMachines(UAnimBlueprint* Blueprint, TArray<UAnimGraphNode_StateMachine*>& OutMachines)
{
    OutMachines.Reset();
    if (UAnimationGraph* AnimGraph = FindRootAnimGraph(Blueprint))
    {
        AnimGraph->GetNodesOfClass(OutMachines);
    }
}

UAnimGraphNode_StateMachine* FindStateMachine(UAnimBlueprint* Blueprint, const FString& Name)
{
    TArray<UAnimGraphNode_StateMachine*> Machines;
    GetStateMachines(Blueprint, Machines);
    UAnimGraphNode_StateMachine** Found = Machines.FindByPredicate([&](UAnimGraphNode_StateMachine* Machine)
    {
        return Machine && Machine->GetStateMachineName().Equals(Name, ESearchCase::CaseSensitive);
    });
    return Found ? *Found : nullptr;
}

UAnimStateNode* FindState(UAnimGraphNode_StateMachine* Machine, const FString& Name)
{
    if (!Machine || !Machine->EditorStateMachineGraph)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Machine->EditorStateMachineGraph->Nodes)
    {
        UAnimStateNode* State = Cast<UAnimStateNode>(Node);
        if (State && State->GetStateName().Equals(Name, ESearchCase::CaseSensitive))
        {
            return State;
        }
    }
    return nullptr;
}

UAnimStateTransitionNode* FindTransition(
    UAnimGraphNode_StateMachine* Machine,
    const FString& FromState,
    const FString& ToState)
{
    if (!Machine || !Machine->EditorStateMachineGraph)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Machine->EditorStateMachineGraph->Nodes)
    {
        UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
        const UAnimStateNodeBase* Previous = Transition ? Transition->GetPreviousState() : nullptr;
        const UAnimStateNodeBase* Next = Transition ? Transition->GetNextState() : nullptr;
        if (Previous && Next
            && Previous->GetStateName().Equals(FromState, ESearchCase::CaseSensitive)
            && Next->GetStateName().Equals(ToState, ESearchCase::CaseSensitive))
        {
            return Transition;
        }
    }
    return nullptr;
}

UAnimStateNode* FindStateInTransitionMachine(
    UAnimStateTransitionNode* Transition,
    const FString& Name)
{
    UEdGraph* MachineGraph = Transition ? Transition->GetGraph() : nullptr;
    if (!MachineGraph)
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : MachineGraph->Nodes)
    {
        UAnimStateNode* State = Cast<UAnimStateNode>(Node);
        if (State
            && State->GetStateName().Equals(
                Name, ESearchCase::CaseSensitive))
        {
            return State;
        }
    }
    return nullptr;
}

void GetTransitionMachineStateNames(
    UAnimStateTransitionNode* Transition,
    TSet<FString>& OutNames)
{
    OutNames.Reset();
    UEdGraph* MachineGraph = Transition ? Transition->GetGraph() : nullptr;
    if (!MachineGraph)
    {
        return;
    }
    for (const UEdGraphNode* Node : MachineGraph->Nodes)
    {
        if (const UAnimStateNode* State = Cast<UAnimStateNode>(Node))
        {
            OutNames.Add(State->GetStateName());
        }
    }
}

UAnimGraphNode_StateMachine* AddStateMachineNode(
    UAnimBlueprint* Blueprint,
    const FThomasAnimationOperation& Operation)
{
    UAnimationGraph* AnimGraph = FindRootAnimGraph(Blueprint);
    if (!AnimGraph)
    {
        return nullptr;
    }
    FGraphNodeCreator<UAnimGraphNode_StateMachine> Creator(*AnimGraph);
    UAnimGraphNode_StateMachine* Machine = Creator.CreateNode();
    Machine->NodePosX = Operation.PositionX;
    Machine->NodePosY = Operation.PositionY;
    Creator.Finalize();
    if (!Machine->EditorStateMachineGraph)
    {
        Machine->DestroyNode();
        return nullptr;
    }
    FBlueprintEditorUtils::RenameGraph(Machine->EditorStateMachineGraph, Operation.MachineName);
    return Machine;
}

UAnimStateNode* AddStateNode(
    UAnimGraphNode_StateMachine* Machine,
    const FThomasAnimationOperation& Operation)
{
    if (!Machine || !Machine->EditorStateMachineGraph)
    {
        return nullptr;
    }
    FGraphNodeCreator<UAnimStateNode> Creator(*Machine->EditorStateMachineGraph);
    UAnimStateNode* State = Creator.CreateNode();
    State->NodePosX = Operation.PositionX;
    State->NodePosY = Operation.PositionY;
    State->bAlwaysResetOnEntry = Operation.bAlwaysResetOnEntry;
    Creator.Finalize();
    State->OnRenameNode(Operation.StateName);
    return State;
}

bool SetEntryState(UAnimGraphNode_StateMachine* Machine, UAnimStateNode* State)
{
    UAnimationStateMachineGraph* Graph = Machine ? Machine->EditorStateMachineGraph : nullptr;
    UAnimStateEntryNode* Entry = Graph ? Graph->EntryNode : nullptr;
    if (!Entry || !State)
    {
        return false;
    }
    UEdGraphPin* EntryOutput = nullptr;
    for (UEdGraphPin* Pin : Entry->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output)
        {
            EntryOutput = Pin;
            break;
        }
    }
    UEdGraphPin* StateInput = State->GetInputPin();
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!EntryOutput || !StateInput || !Schema)
    {
        return false;
    }
    EntryOutput->BreakAllPinLinks(true);
    return Schema->TryCreateConnection(EntryOutput, StateInput);
}

UAnimStateTransitionNode* AddTransitionNode(
    UAnimGraphNode_StateMachine* Machine,
    UAnimStateNode* FromState,
    UAnimStateNode* ToState,
    const FThomasAnimationOperation& Operation)
{
    if (!Machine || !Machine->EditorStateMachineGraph || !FromState || !ToState)
    {
        return nullptr;
    }
    FGraphNodeCreator<UAnimStateTransitionNode> Creator(*Machine->EditorStateMachineGraph);
    UAnimStateTransitionNode* Transition = Creator.CreateNode();
    Transition->NodePosX = Operation.PositionX;
    Transition->NodePosY = Operation.PositionY;
    Creator.Finalize();
    Transition->CreateConnections(FromState, ToState);
    Transition->CrossfadeDuration = Operation.Duration;
    Transition->PriorityOrder = Operation.PriorityOrder;
    Transition->bAutomaticRuleBasedOnSequencePlayerInState = Operation.bAutomaticTransitionRule;
    Transition->bDisabled = !Operation.bEnabled;
    return Transition;
}

UAnimGraphNode_SequencePlayer* FindStateSequencePlayer(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_SequencePlayer*> Players;
    Graph->GetNodesOfClass(Players);
    return Players.IsEmpty() ? nullptr : Players[0];
}

UAnimGraphNode_BlendListByBool* FindStateBlendByBool(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_BlendListByBool*> Blends;
    Graph->GetNodesOfClass(Blends);
    return Blends.Num() == 1 ? Blends[0] : nullptr;
}

bool SetBoolBlendTimes(
    UAnimGraphNode_BlendListByBool* Blend,
    const float BlendTime)
{
    FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByBool::StaticStruct(), TEXT("BlendTime"));
    FFloatProperty* FloatProperty = ArrayProperty
        ? CastField<FFloatProperty>(ArrayProperty->Inner) : nullptr;
    if (!Blend || !ArrayProperty || !FloatProperty)
    {
        return false;
    }
    void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node);
    FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddress);
    ArrayHelper.Resize(2);
    FloatProperty->SetFloatingPointPropertyValue(
        ArrayHelper.GetRawPtr(0), BlendTime);
    FloatProperty->SetFloatingPointPropertyValue(
        ArrayHelper.GetRawPtr(1), BlendTime);
    return true;
}

float GetBoolBlendTime(const UAnimGraphNode_BlendListByBool* Blend)
{
    FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByBool::StaticStruct(), TEXT("BlendTime"));
    const FFloatProperty* FloatProperty = ArrayProperty
        ? CastField<FFloatProperty>(ArrayProperty->Inner) : nullptr;
    if (!Blend || !ArrayProperty || !FloatProperty)
    {
        return 0.0f;
    }
    const void* ArrayAddress =
        ArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node);
    FScriptArrayHelper ArrayHelper(
        ArrayProperty, const_cast<void*>(ArrayAddress));
    return ArrayHelper.IsValidIndex(0)
        ? static_cast<float>(FloatProperty->GetFloatingPointPropertyValue(
            ArrayHelper.GetRawPtr(0)))
        : 0.0f;
}

bool ConfigureIntBlendArrays(
    UAnimGraphNode_BlendListByInt* Blend,
    const TArray<float>& BlendTimes)
{
    FArrayProperty* PoseArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByInt::StaticStruct(), TEXT("BlendPose"));
    FArrayProperty* TimeArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByInt::StaticStruct(), TEXT("BlendTime"));
    FFloatProperty* FloatProperty = TimeArrayProperty
        ? CastField<FFloatProperty>(TimeArrayProperty->Inner) : nullptr;
    if (!Blend || !PoseArrayProperty || !TimeArrayProperty || !FloatProperty
        || BlendTimes.Num() < 2)
    {
        return false;
    }
    FScriptArrayHelper PoseHelper(
        PoseArrayProperty,
        PoseArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node));
    FScriptArrayHelper TimeHelper(
        TimeArrayProperty,
        TimeArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node));
    PoseHelper.Resize(BlendTimes.Num());
    TimeHelper.Resize(BlendTimes.Num());
    for (int32 Index = 0; Index < BlendTimes.Num(); ++Index)
    {
        FloatProperty->SetFloatingPointPropertyValue(
            TimeHelper.GetRawPtr(Index), BlendTimes[Index]);
    }
    return true;
}

int32 GetIntBlendPoseCount(const UAnimGraphNode_BlendListByInt* Blend)
{
    FArrayProperty* PoseArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByInt::StaticStruct(), TEXT("BlendPose"));
    if (!Blend || !PoseArrayProperty)
    {
        return 0;
    }
    const void* ArrayAddress =
        PoseArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node);
    FScriptArrayHelper PoseHelper(
        PoseArrayProperty, const_cast<void*>(ArrayAddress));
    return PoseHelper.Num();
}

float GetIntBlendTime(
    const UAnimGraphNode_BlendListByInt* Blend,
    const int32 Index)
{
    FArrayProperty* TimeArrayProperty = FindFProperty<FArrayProperty>(
        FAnimNode_BlendListByInt::StaticStruct(), TEXT("BlendTime"));
    const FFloatProperty* FloatProperty = TimeArrayProperty
        ? CastField<FFloatProperty>(TimeArrayProperty->Inner) : nullptr;
    if (!Blend || !TimeArrayProperty || !FloatProperty)
    {
        return 0.0f;
    }
    const void* ArrayAddress =
        TimeArrayProperty->ContainerPtrToValuePtr<void>(&Blend->Node);
    FScriptArrayHelper TimeHelper(
        TimeArrayProperty, const_cast<void*>(ArrayAddress));
    return TimeHelper.IsValidIndex(Index)
        ? static_cast<float>(FloatProperty->GetFloatingPointPropertyValue(
            TimeHelper.GetRawPtr(Index)))
        : 0.0f;
}

UEdGraphPin* FindNamedPin(
    UEdGraphNode* Node,
    const EEdGraphPinDirection Direction,
    const FName PrimaryName,
    const FName SecondaryName = NAME_None)
{
    if (!Node)
    {
        return nullptr;
    }
    if (UEdGraphPin* Pin = Node->FindPin(PrimaryName, Direction))
    {
        return Pin;
    }
    return SecondaryName.IsNone()
        ? nullptr : Node->FindPin(SecondaryName, Direction);
}

bool GetStateBoolBlendParts(
    UAnimStateNode* State,
    UAnimGraphNode_BlendListByBool*& OutBlend,
    UAnimGraphNode_SequencePlayer*& OutFalsePlayer,
    UAnimGraphNode_SequencePlayer*& OutTruePlayer,
    UK2Node_VariableGet*& OutGetter)
{
    OutBlend = FindStateBlendByBool(State);
    OutFalsePlayer = nullptr;
    OutTruePlayer = nullptr;
    OutGetter = nullptr;
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    if (!Graph || !Sink || !OutBlend)
    {
        return false;
    }
    UEdGraphPin* FalseInput = FindNamedPin(
        OutBlend, EGPD_Input, TEXT("BlendPose_0"));
    UEdGraphPin* TrueInput = FindNamedPin(
        OutBlend, EGPD_Input, TEXT("BlendPose_1"));
    UEdGraphPin* ActiveInput = FindNamedPin(
        OutBlend, EGPD_Input, TEXT("bActiveValue"), TEXT("ActiveValue"));
    UEdGraphPin* BlendPose = FindNamedPin(
        OutBlend, EGPD_Output, TEXT("Pose"));
    if (!FalseInput || !TrueInput || !ActiveInput || !BlendPose
        || FalseInput->LinkedTo.Num() != 1
        || TrueInput->LinkedTo.Num() != 1
        || ActiveInput->LinkedTo.Num() != 1
        || Sink->LinkedTo.Num() != 1
        || !Sink->LinkedTo.Contains(BlendPose))
    {
        return false;
    }
    OutFalsePlayer = Cast<UAnimGraphNode_SequencePlayer>(
        FalseInput->LinkedTo[0]->GetOwningNode());
    OutTruePlayer = Cast<UAnimGraphNode_SequencePlayer>(
        TrueInput->LinkedTo[0]->GetOwningNode());
    OutGetter = Cast<UK2Node_VariableGet>(
        ActiveInput->LinkedTo[0]->GetOwningNode());
    if (!OutFalsePlayer || !OutTruePlayer || OutFalsePlayer == OutTruePlayer
        || !OutGetter)
    {
        return false;
    }
    int32 ResultCount = 0;
    int32 PlayerCount = 0;
    int32 BlendCount = 0;
    int32 GetterCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>()) ++ResultCount;
        else if (Node->IsA<UAnimGraphNode_SequencePlayer>()) ++PlayerCount;
        else if (Node->IsA<UAnimGraphNode_BlendListByBool>()) ++BlendCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++GetterCount;
        else return false;
    }
    return ResultCount == 1 && PlayerCount == 2
        && BlendCount == 1 && GetterCount == 1;
}

UAnimGraphNode_BlendListByInt* FindStateBlendListByInt(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_BlendListByInt*> Blends;
    Graph->GetNodesOfClass(Blends);
    return Blends.Num() == 1 ? Blends[0] : nullptr;
}

bool GetStateIntBlendParts(
    UAnimStateNode* State,
    UAnimGraphNode_BlendListByInt*& OutBlend,
    TArray<UAnimGraphNode_SequencePlayer*>& OutPlayers,
    UK2Node_VariableGet*& OutGetter)
{
    OutBlend = FindStateBlendListByInt(State);
    OutPlayers.Reset();
    OutGetter = nullptr;
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    const int32 PoseCount = GetIntBlendPoseCount(OutBlend);
    UEdGraphPin* BlendPose = FindNamedPin(
        OutBlend, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* IndexInput = FindNamedPin(
        OutBlend, EGPD_Input, TEXT("ActiveChildIndex"), TEXT("ActiveIndex"));
    if (!Graph || !Sink || !OutBlend || PoseCount < 2
        || !BlendPose || !IndexInput || IndexInput->LinkedTo.Num() != 1
        || Sink->LinkedTo.Num() != 1 || !Sink->LinkedTo.Contains(BlendPose))
    {
        return false;
    }
    OutGetter = Cast<UK2Node_VariableGet>(
        IndexInput->LinkedTo[0]->GetOwningNode());
    if (!OutGetter)
    {
        return false;
    }
    for (int32 Index = 0; Index < PoseCount; ++Index)
    {
        UEdGraphPin* PoseInput = FindNamedPin(
            OutBlend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendPose_%d"), Index)));
        if (!PoseInput || PoseInput->LinkedTo.Num() != 1)
        {
            return false;
        }
        UAnimGraphNode_SequencePlayer* Player =
            Cast<UAnimGraphNode_SequencePlayer>(
                PoseInput->LinkedTo[0]->GetOwningNode());
        if (!Player || OutPlayers.Contains(Player))
        {
            return false;
        }
        OutPlayers.Add(Player);
    }
    int32 ResultCount = 0;
    int32 PlayerCount = 0;
    int32 BlendCount = 0;
    int32 GetterCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>()) ++ResultCount;
        else if (Node->IsA<UAnimGraphNode_SequencePlayer>()) ++PlayerCount;
        else if (Node->IsA<UAnimGraphNode_BlendListByInt>()) ++BlendCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++GetterCount;
        else return false;
    }
    return ResultCount == 1 && PlayerCount == PoseCount
        && BlendCount == 1 && GetterCount == 1;
}

UAnimGraphNode_ApplyAdditive* FindStateApplyAdditive(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_ApplyAdditive*> Nodes;
    Graph->GetNodesOfClass(Nodes);
    return Nodes.Num() == 1 ? Nodes[0] : nullptr;
}

bool GetStateApplyAdditiveParts(
    UAnimStateNode* State,
    UAnimGraphNode_ApplyAdditive*& OutApplyNode,
    UAnimGraphNode_SequencePlayer*& OutBasePlayer,
    UAnimGraphNode_SequencePlayer*& OutAdditivePlayer,
    UK2Node_VariableGet*& OutAlphaGetter)
{
    OutApplyNode = FindStateApplyAdditive(State);
    OutBasePlayer = nullptr;
    OutAdditivePlayer = nullptr;
    OutAlphaGetter = nullptr;
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UEdGraphPin* BaseInput = FindNamedPin(
        OutApplyNode, EGPD_Input, TEXT("Base"));
    UEdGraphPin* AdditiveInput = FindNamedPin(
        OutApplyNode, EGPD_Input, TEXT("Additive"));
    UEdGraphPin* AlphaInput = FindNamedPin(
        OutApplyNode, EGPD_Input, TEXT("Alpha"));
    UEdGraphPin* Pose = FindNamedPin(
        OutApplyNode, EGPD_Output, TEXT("Pose"));
    if (!Graph || !Sink || !OutApplyNode || !BaseInput || !AdditiveInput
        || !AlphaInput || !Pose || BaseInput->LinkedTo.Num() != 1
        || AdditiveInput->LinkedTo.Num() != 1
        || AlphaInput->LinkedTo.Num() != 1
        || Sink->LinkedTo.Num() != 1 || !Sink->LinkedTo.Contains(Pose))
    {
        return false;
    }
    OutBasePlayer = Cast<UAnimGraphNode_SequencePlayer>(
        BaseInput->LinkedTo[0]->GetOwningNode());
    OutAdditivePlayer = Cast<UAnimGraphNode_SequencePlayer>(
        AdditiveInput->LinkedTo[0]->GetOwningNode());
    OutAlphaGetter = Cast<UK2Node_VariableGet>(
        AlphaInput->LinkedTo[0]->GetOwningNode());
    if (!OutBasePlayer || !OutAdditivePlayer
        || OutBasePlayer == OutAdditivePlayer || !OutAlphaGetter)
    {
        return false;
    }
    int32 ResultCount = 0;
    int32 PlayerCount = 0;
    int32 ApplyCount = 0;
    int32 GetterCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>()) ++ResultCount;
        else if (Node->IsA<UAnimGraphNode_SequencePlayer>()) ++PlayerCount;
        else if (Node->IsA<UAnimGraphNode_ApplyAdditive>()) ++ApplyCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++GetterCount;
        else return false;
    }
    return ResultCount == 1 && PlayerCount == 2
        && ApplyCount == 1 && GetterCount == 1;
}

UAnimGraphNode_LayeredBoneBlend* FindStateLayeredBlendPerBone(
    UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_LayeredBoneBlend*> Nodes;
    Graph->GetNodesOfClass(Nodes);
    return Nodes.Num() == 1 ? Nodes[0] : nullptr;
}

bool ConfigureLayeredBlendPerBone(
    UAnimGraphNode_LayeredBoneBlend* Blend,
    const FThomasAnimationOperation& Operation)
{
    const int32 LayerCount = Operation.ReferencedAssetPaths.Num();
    if (!Blend || LayerCount < 1
        || Operation.LayerBoneNames.Num() != LayerCount
        || Operation.LayerBlendDepths.Num() != LayerCount)
    {
        return false;
    }
    Blend->Node.BlendPoses.SetNum(LayerCount);
    Blend->Node.BlendWeights.Init(1.0f, LayerCount);
    Blend->Node.LayerSetup.SetNum(LayerCount);
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        FInputBlendPose& Layer = Blend->Node.LayerSetup[LayerIndex];
        Layer.BranchFilters.Reset();
        FBranchFilter& Filter = Layer.BranchFilters.AddDefaulted_GetRef();
        Filter.BoneName = FName(*Operation.LayerBoneNames[LayerIndex]);
        Filter.BlendDepth = Operation.LayerBlendDepths[LayerIndex];
    }
    Blend->Node.bMeshSpaceRotationBlend = Operation.bMeshSpaceRotationBlend;
    Blend->Node.bMeshSpaceScaleBlend = Operation.bMeshSpaceScaleBlend;
    Blend->Node.bBlendRootMotionBasedOnRootBone =
        Operation.bBlendRootMotionBasedOnRootBone;
    return true;
}

bool GetStateLayeredBlendPerBoneParts(
    UAnimStateNode* State,
    UAnimGraphNode_LayeredBoneBlend*& OutBlend,
    UAnimGraphNode_SequencePlayer*& OutBasePlayer,
    TArray<UAnimGraphNode_SequencePlayer*>& OutLayerPlayers,
    TArray<UK2Node_VariableGet*>& OutAlphaGetters)
{
    OutBlend = FindStateLayeredBlendPerBone(State);
    OutBasePlayer = nullptr;
    OutLayerPlayers.Reset();
    OutAlphaGetters.Reset();
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    const int32 LayerCount = OutBlend ? OutBlend->Node.BlendPoses.Num() : 0;
    UEdGraphPin* BaseInput = FindNamedPin(
        OutBlend, EGPD_Input, TEXT("BasePose"), TEXT("Base"));
    UEdGraphPin* Pose = FindNamedPin(OutBlend, EGPD_Output, TEXT("Pose"));
    if (!Graph || !Sink || !OutBlend || LayerCount < 1
        || OutBlend->Node.BlendWeights.Num() != LayerCount
        || OutBlend->Node.LayerSetup.Num() != LayerCount
        || !BaseInput || BaseInput->LinkedTo.Num() != 1 || !Pose
        || Sink->LinkedTo.Num() != 1 || !Sink->LinkedTo.Contains(Pose))
    {
        return false;
    }
    OutBasePlayer = Cast<UAnimGraphNode_SequencePlayer>(
        BaseInput->LinkedTo[0]->GetOwningNode());
    if (!OutBasePlayer)
    {
        return false;
    }
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        UEdGraphPin* LayerInput = FindNamedPin(
            OutBlend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendPoses_%d"), LayerIndex)),
            FName(*FString::Printf(TEXT("BlendPose_%d"), LayerIndex)));
        UEdGraphPin* WeightInput = FindNamedPin(
            OutBlend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendWeights_%d"), LayerIndex)),
            FName(*FString::Printf(TEXT("BlendWeight_%d"), LayerIndex)));
        if (!LayerInput || LayerInput->LinkedTo.Num() != 1
            || !WeightInput || WeightInput->LinkedTo.Num() != 1
            || OutBlend->Node.LayerSetup[LayerIndex].BranchFilters.Num() != 1)
        {
            return false;
        }
        UAnimGraphNode_SequencePlayer* LayerPlayer =
            Cast<UAnimGraphNode_SequencePlayer>(
                LayerInput->LinkedTo[0]->GetOwningNode());
        UK2Node_VariableGet* AlphaGetter = Cast<UK2Node_VariableGet>(
            WeightInput->LinkedTo[0]->GetOwningNode());
        if (!LayerPlayer || LayerPlayer == OutBasePlayer
            || OutLayerPlayers.Contains(LayerPlayer) || !AlphaGetter
            || OutAlphaGetters.Contains(AlphaGetter))
        {
            return false;
        }
        OutLayerPlayers.Add(LayerPlayer);
        OutAlphaGetters.Add(AlphaGetter);
    }
    int32 ResultCount = 0;
    int32 PlayerCount = 0;
    int32 BlendCount = 0;
    int32 GetterCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>()) ++ResultCount;
        else if (Node->IsA<UAnimGraphNode_SequencePlayer>()) ++PlayerCount;
        else if (Node->IsA<UAnimGraphNode_LayeredBoneBlend>()) ++BlendCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++GetterCount;
        else return false;
    }
    return ResultCount == 1 && PlayerCount == LayerCount + 1
        && BlendCount == 1 && GetterCount == LayerCount;
}

UAnimGraphNode_BlendSpacePlayer* FindStateBlendSpacePlayer(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    if (!Graph)
    {
        return nullptr;
    }
    TArray<UAnimGraphNode_BlendSpacePlayer*> Players;
    Graph->GetNodesOfClass(Players);
    return Players.Num() == 1 ? Players[0] : nullptr;
}

bool GetStateBlendSpaceParts(
    UAnimStateNode* State,
    UAnimGraphNode_BlendSpacePlayer*& OutPlayer,
    UK2Node_VariableGet*& OutXGetter,
    UK2Node_VariableGet*& OutYGetter,
    bool& bOutOneDimensional)
{
    OutPlayer = FindStateBlendSpacePlayer(State);
    OutXGetter = nullptr;
    OutYGetter = nullptr;
    bOutOneDimensional = false;
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UBlendSpace* BlendSpace = OutPlayer
        ? Cast<UBlendSpace>(OutPlayer->GetAnimationAsset()) : nullptr;
    if (!Graph || !Sink || !OutPlayer || !BlendSpace)
    {
        return false;
    }
    bOutOneDimensional = BlendSpace->IsA<UBlendSpace1D>();
    UEdGraphPin* Pose = FindNamedPin(OutPlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* XInput = FindNamedPin(OutPlayer, EGPD_Input, TEXT("X"));
    UEdGraphPin* YInput = FindNamedPin(OutPlayer, EGPD_Input, TEXT("Y"));
    if (!Pose || !XInput || XInput->LinkedTo.Num() != 1
        || Sink->LinkedTo.Num() != 1 || !Sink->LinkedTo.Contains(Pose)
        || (!bOutOneDimensional && (!YInput || YInput->LinkedTo.Num() != 1)))
    {
        return false;
    }
    OutXGetter = Cast<UK2Node_VariableGet>(
        XInput->LinkedTo[0]->GetOwningNode());
    OutYGetter = !bOutOneDimensional && YInput
        ? Cast<UK2Node_VariableGet>(YInput->LinkedTo[0]->GetOwningNode())
        : nullptr;
    if (!OutXGetter || (!bOutOneDimensional && !OutYGetter))
    {
        return false;
    }
    int32 ResultCount = 0;
    int32 PlayerCount = 0;
    int32 GetterCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>()) ++ResultCount;
        else if (Node->IsA<UAnimGraphNode_BlendSpacePlayer>()) ++PlayerCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++GetterCount;
        else return false;
    }
    return ResultCount == 1 && PlayerCount == 1
        && GetterCount == (bOutOneDimensional ? 1 : 2);
}

bool IsGenericStateGraphAction(const FString& Action)
{
    return Action == TEXT("add_state_graph_node")
        || Action == TEXT("remove_state_graph_node")
        || Action == TEXT("set_state_graph_node_property")
        || Action == TEXT("resize_state_graph_node_array")
        || Action == TEXT("resize_state_graph_pose_array")
        || Action == TEXT("replace_state_graph_constraint_array")
        || Action == TEXT("set_state_graph_pin_default")
        || Action == TEXT("add_state_graph_link")
        || Action == TEXT("remove_state_graph_link");
}

bool IsSupportedStateGraphNodeClass(const UClass* NodeClass)
{
    return NodeClass
        && NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass())
        && NodeClass->HasAllClassFlags(CLASS_Native)
        && !NodeClass->HasAnyClassFlags(
            CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        && !NodeClass->IsChildOf(UAnimGraphNode_Root::StaticClass())
        && !NodeClass->IsChildOf(UAnimGraphNode_StateMachine::StaticClass())
        && !NodeClass->IsChildOf(UAnimGraphNode_StateResult::StaticClass())
        && !NodeClass->IsChildOf(UAnimGraphNode_TransitionResult::StaticClass());
}

bool IsStateGraphBlendListByEnumClass(const UClass* NodeClass)
{
    return NodeClass
        && NodeClass->GetPathName()
            == TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum");
}

bool ResolveStateGraphEnum(
    const FString& Input,
    UEnum*& OutEnum)
{
    OutEnum = nullptr;
    const FString Trimmed = Input.TrimStartAndEnd();
    if (Trimmed.IsEmpty() || Trimmed.Len() > 512)
    {
        return false;
    }
    if (Trimmed.StartsWith(TEXT("/Script/")))
    {
        OutEnum = FindObject<UEnum>(nullptr, *Trimmed);
        return OutEnum && OutEnum->GetPathName() == Trimmed
            && OutEnum->NumEnums() >= 2;
    }
    const FString PackagePath = NormalizePath(Trimmed);
    if (!IsAllowedPath(PackagePath))
    {
        return false;
    }
    const FString ExactObjectPath = Trimmed.Contains(TEXT("."))
        ? Trimmed : ObjectPath(PackagePath);
    OutEnum = LoadObject<UEnum>(nullptr, *ExactObjectPath);
    return OutEnum && OutEnum->GetPathName() == ExactObjectPath
        && OutEnum->NumEnums() >= 2;
}

TArray<UClass*> GetAvailableStateGraphNodeClasses()
{
    TArray<UClass*> Result;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* NodeClass = *It;
        if (IsSupportedStateGraphNodeClass(NodeClass))
        {
            Result.Add(NodeClass);
        }
    }
    Result.Sort([](const UClass& A, const UClass& B)
    {
        return A.GetPathName() < B.GetPathName();
    });
    return Result;
}

UAnimationStateGraph* ResolveStateGraph(
    UAnimBlueprint* Blueprint,
    const FString& MachineName,
    const FString& StateName)
{
    UAnimGraphNode_StateMachine* Machine =
        FindStateMachine(Blueprint, MachineName);
    UAnimStateNode* State = FindState(Machine, StateName);
    return State ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
}

FString EncodeStateGraphValue(const FString& Value)
{
    return Value
        .Replace(TEXT("%"), TEXT("%25"))
        .Replace(TEXT("|"), TEXT("%7C"))
        .Replace(TEXT("\r"), TEXT("%0D"))
        .Replace(TEXT("\n"), TEXT("%0A"));
}

FString StateGraphPinDirectionString(
    const EEdGraphPinDirection Direction)
{
    return Direction == EGPD_Input ? TEXT("Input") : TEXT("Output");
}

struct FStateGraphNodePropertyRef
{
    const FStructProperty* RuntimeNodeProperty = nullptr;
    FProperty* Property = nullptr;
    FProperty* RootProperty = nullptr;
    const void* RuntimeNodeMemory = nullptr;
    const void* ValueMemory = nullptr;
    FString Path;
};

struct FStateGraphNodeArrayRef
{
    const FStructProperty* RuntimeNodeProperty = nullptr;
    FArrayProperty* ArrayProperty = nullptr;
    FProperty* RootProperty = nullptr;
    const void* RuntimeNodeMemory = nullptr;
    const void* ValueMemory = nullptr;
    FString Path;
    bool bResizable = false;
};

struct FStateGraphPoseArrayRef
{
    FStateGraphNodeArrayRef PoseArray;
    FStateGraphNodeArrayRef CompanionArray;
    const FObjectPropertyBase* BoundEnumProperty = nullptr;
    UEnum* BoundEnum = nullptr;
    FArrayProperty* EnumEntriesProperty = nullptr;
    void* EnumEntriesMemory = nullptr;
    FString Kind;
    FString PosePinPrefix;
    FString CompanionPinPrefix;
    bool bCompanionUnitInterval = false;
};

struct FStateGraphConstraintArrayRef
{
    FStateGraphNodeArrayRef ConstraintArray;
    FStateGraphNodeArrayRef WeightArray;
    const FStructProperty* ConstraintProperty = nullptr;
    const FStructProperty* TargetBoneProperty = nullptr;
    const FNameProperty* BoneNameProperty = nullptr;
    FString Kind;
};

const FStructProperty* FindStateGraphRuntimeNodeProperty(
    const UClass* NodeClass)
{
    if (!NodeClass)
    {
        return nullptr;
    }
    for (TFieldIterator<FStructProperty> It(
             NodeClass, EFieldIteratorFlags::IncludeSuper);
         It;
         ++It)
    {
        FStructProperty* Property = *It;
        if (Property && Property->Struct
            && Property->Struct->IsChildOf(
                FAnimNode_Base::StaticStruct()))
        {
            return Property;
        }
    }
    return nullptr;
}

const FStructProperty* FindStateGraphRuntimeNodeProperty(
    const UAnimGraphNode_Base* Node)
{
    return FindStateGraphRuntimeNodeProperty(
        Node ? Node->GetClass() : nullptr);
}

bool StateGraphNodeHasPinNamed(
    const UAnimGraphNode_Base* Node,
    const FName PropertyName)
{
    if (!Node)
    {
        return false;
    }
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->PinName == PropertyName)
        {
            return true;
        }
    }
    return false;
}

bool HasEditableStateGraphPropertyFlags(const FProperty* Property)
{
    return Property && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_EditConst | CPF_Deprecated);
}

bool IsStateGraphAtomicStruct(const UScriptStruct* Struct)
{
    if (!Struct)
    {
        return false;
    }
    static const TSet<FName> AllowedStructs = {
        TEXT("Vector"), TEXT("Vector2D"), TEXT("Vector4"),
        TEXT("Rotator"), TEXT("Quat"), TEXT("LinearColor"),
        TEXT("Color"), TEXT("Transform")
    };
    return AllowedStructs.Contains(Struct->GetFName());
}

bool IsStateGraphNestedStruct(const UScriptStruct* Struct)
{
    if (!Struct)
    {
        return false;
    }
    static const TSet<FName> AllowedStructs = {
        TEXT("InputScaleBias"),
        TEXT("InputScaleBiasClamp"),
        TEXT("InputScaleBiasClampConstants"),
        TEXT("InputAlphaBoolBlend"),
        TEXT("InputRange"),
        TEXT("BoneReference"),
        TEXT("SocketReference"),
        TEXT("BoneSocketTarget"),
        TEXT("Axis"),
        TEXT("ReferenceBoneFrame"),
        TEXT("RuntimeFloatCurve"),
        TEXT("RotationRetargetingInfo"),
        TEXT("AnimPhysSimSpaceSettings"),
        TEXT("SimSpaceSettings"),
        TEXT("RBFParams"),
        TEXT("AlphaBlend"),
        TEXT("BranchFilter"),
        TEXT("InputBlendPose")
    };
    return AllowedStructs.Contains(Struct->GetFName());
}

FString StateGraphTopologyPropertyKind(const FProperty* Property)
{
    const FStructProperty* StructProperty =
        HasEditableStateGraphPropertyFlags(Property)
            ? CastField<const FStructProperty>(Property)
            : nullptr;
    const FName StructName = StructProperty && StructProperty->Struct
        ? StructProperty->Struct->GetFName() : NAME_None;
    if (StructName == TEXT("PoseLink"))
    {
        return TEXT("pose_link");
    }
    if (StructName == TEXT("ComponentSpacePoseLink"))
    {
        return TEXT("component_space_pose_link");
    }
    const FArrayProperty* ArrayProperty =
        HasEditableStateGraphPropertyFlags(Property)
            ? CastField<const FArrayProperty>(Property)
            : nullptr;
    const FStructProperty* InnerStruct = ArrayProperty
        ? CastField<const FStructProperty>(ArrayProperty->Inner)
        : nullptr;
    const FName InnerStructName = InnerStruct && InnerStruct->Struct
        ? InnerStruct->Struct->GetFName() : NAME_None;
    if (InnerStructName == TEXT("PoseLink"))
    {
        return TEXT("pose_link_array");
    }
    if (InnerStructName == TEXT("ComponentSpacePoseLink"))
    {
        return TEXT("component_space_pose_link_array");
    }
    return FString();
}

bool IsSupportedStateGraphObjectProperty(const FProperty* Property)
{
    const FObjectPropertyBase* ObjectProperty =
        CastField<const FObjectPropertyBase>(Property);
    const UClass* PropertyClass = ObjectProperty
        ? ObjectProperty->PropertyClass : nullptr;
    return Property && Property->IsA<FObjectProperty>() && PropertyClass
        && (PropertyClass->IsChildOf(UAnimationAsset::StaticClass())
            || PropertyClass->IsChildOf(USkeleton::StaticClass())
            || PropertyClass->IsChildOf(USkeletalMesh::StaticClass())
            || PropertyClass->IsChildOf(UCurveFloat::StaticClass())
            || PropertyClass->IsChildOf(UPhysicsAsset::StaticClass())
            || PropertyClass->IsChildOf(UMirrorDataTable::StaticClass())
            || PropertyClass->IsChildOf(UBlendProfile::StaticClass())
            || PropertyClass->IsChildOf(UIKRigDefinition::StaticClass())
            || PropertyClass->IsChildOf(UIKRetargeter::StaticClass())
            || PropertyClass->GetPathName()
                == TEXT("/Script/PhysicsControl.PhysicsControlAsset"));
}

bool ResolveStateGraphObjectPropertyValue(
    const FObjectPropertyBase* ObjectProperty,
    const FString& Input,
    UObject*& OutObject)
{
    OutObject = nullptr;
    const FString Trimmed = Input.TrimStartAndEnd();
    if (!ObjectProperty || !ObjectProperty->PropertyClass
        || Trimmed.IsEmpty())
    {
        return false;
    }
    if (Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return true;
    }

    const FString PackagePath = NormalizePath(Trimmed);
    if (!IsAllowedPath(PackagePath))
    {
        return false;
    }
    UObject* Asset = FindAsset(PackagePath);
    if (!Asset)
    {
        return false;
    }

    if (ObjectProperty->PropertyClass->IsChildOf(
            UBlendProfile::StaticClass()))
    {
        USkeleton* Skeleton = Cast<USkeleton>(Asset);
        if (!Skeleton || !Trimmed.Contains(TEXT(".")))
        {
            return false;
        }
        for (UBlendProfile* Profile : Skeleton->BlendProfiles)
        {
            if (Profile
                && Profile->IsA(ObjectProperty->PropertyClass)
                && Profile->GetSkeleton() == Skeleton
                && Profile->GetPathName() == Trimmed)
            {
                OutObject = Profile;
                return true;
            }
        }
        return false;
    }

    if (!Asset->IsA(ObjectProperty->PropertyClass)
        || (Trimmed.Contains(TEXT("."))
            && Asset->GetPathName() != Trimmed))
    {
        return false;
    }
    OutObject = Asset;
    return true;
}

bool IsSupportedStateGraphClassProperty(const FProperty* Property)
{
    const FClassProperty* ClassProperty =
        CastField<const FClassProperty>(Property);
    return ClassProperty && ClassProperty->MetaClass
        && ClassProperty->MetaClass->IsChildOf(
            UAnimInstance::StaticClass());
}

bool ResolveStateGraphAnimInstanceClass(
    const FClassProperty* ClassProperty,
    const FString& Input,
    UClass*& OutClass)
{
    OutClass = nullptr;
    const FString Trimmed = Input.TrimStartAndEnd();
    if (!ClassProperty
        || !IsSupportedStateGraphClassProperty(ClassProperty))
    {
        return false;
    }
    if (Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (!Trimmed.Contains(TEXT("."))
        || !Trimmed.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
    {
        return false;
    }
    const FString PackagePath = NormalizePath(Trimmed);
    UAnimBlueprint* Blueprint = IsAllowedPath(PackagePath)
        ? Cast<UAnimBlueprint>(FindAsset(PackagePath))
        : nullptr;
    UClass* GeneratedClass = Blueprint
        ? Blueprint->GeneratedClass : nullptr;
    if (!GeneratedClass
        || GeneratedClass->GetPathName() != Trimmed
        || !GeneratedClass->IsChildOf(ClassProperty->MetaClass))
    {
        return false;
    }
    OutClass = GeneratedClass;
    return true;
}

bool IsStateGraphAssociativeScalarType(const FProperty* Property)
{
    return Property
        && (Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FStrProperty>());
}

bool IsSupportedStateGraphSetProperty(const FProperty* Property)
{
    const FSetProperty* SetProperty =
        CastField<const FSetProperty>(Property);
    return SetProperty
        && IsStateGraphAssociativeScalarType(
            SetProperty->ElementProp);
}

bool IsSupportedStateGraphMapProperty(const FProperty* Property)
{
    const FMapProperty* MapProperty =
        CastField<const FMapProperty>(Property);
    return MapProperty
        && IsStateGraphAssociativeScalarType(MapProperty->KeyProp)
        && IsStateGraphAssociativeScalarType(MapProperty->ValueProp);
}

bool IsStateGraphLeafPropertyType(const FProperty* Property)
{
    if (!Property)
    {
        return false;
    }
    if (Property->IsA<FBoolProperty>()
        || Property->IsA<FNumericProperty>()
        || Property->IsA<FEnumProperty>()
        || Property->IsA<FNameProperty>()
        || Property->IsA<FStrProperty>()
        || IsSupportedStateGraphObjectProperty(Property)
        || IsSupportedStateGraphClassProperty(Property)
        || IsSupportedStateGraphSetProperty(Property)
        || IsSupportedStateGraphMapProperty(Property))
    {
        return true;
    }
    const FStructProperty* StructProperty =
        CastField<const FStructProperty>(Property);
    return StructProperty
        && IsStateGraphAtomicStruct(StructProperty->Struct);
}

bool IsSupportedStateGraphArrayInner(const FProperty* Inner)
{
    if (IsStateGraphLeafPropertyType(Inner))
    {
        return true;
    }
    const FStructProperty* StructProperty =
        CastField<const FStructProperty>(Inner);
    return StructProperty
        && IsStateGraphNestedStruct(StructProperty->Struct);
}

bool IsSupportedStateGraphWholeArray(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    if (!RuntimeStruct || !ArrayProperty
        || !HasEditableStateGraphPropertyFlags(ArrayProperty)
        || !IsSupportedStateGraphArrayInner(ArrayProperty->Inner))
    {
        return false;
    }
    static const TSet<FString> AllowedArrays = {
        TEXT("AnimNode_DeadBlending.ExtrapolationFilteredCurves"),
        TEXT("AnimNode_DeadBlending.FilteredBones"),
        TEXT("AnimNode_DeadBlending.FilteredCurves"),
        TEXT("AnimNode_Inertialization.FilteredBones"),
        TEXT("AnimNode_Inertialization.FilteredCurves"),
        TEXT("AnimNode_HandIKRetargeting.IKBonesToMove"),
        TEXT("AnimNode_PoseDriver.OnlyDriveBones"),
        TEXT("AnimNode_PoseDriver.SourceBones"),
        TEXT("AnimNode_ControlRig.InputBonesToTransfer"),
        TEXT("AnimNode_ControlRig.OutputBonesToTransfer"),
        TEXT("AnimNode_RetargetPoseFromMesh.OverrideSetsToApply")
    };
    return AllowedArrays.Contains(
        RuntimeStruct->GetName() + TEXT(".")
            + ArrayProperty->GetName());
}

FString StateGraphArrayKey(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    return RuntimeStruct && ArrayProperty
        ? RuntimeStruct->GetName() + TEXT(".")
            + ArrayProperty->GetName()
        : FString();
}

FString StateGraphStructFieldSchema(const UScriptStruct* Struct)
{
    if (!Struct)
    {
        return FString();
    }
    TArray<FString> Fields;
    for (TFieldIterator<FProperty> It(
             Struct,
             EFieldIteratorFlags::IncludeSuper);
         It && Fields.Num() < 24;
         ++It)
    {
        const FProperty* Field = *It;
        if (Field)
        {
            Fields.Add(Field->GetName() + TEXT(":")
                + Field->GetCPPType());
        }
    }
    return FString::Join(Fields, TEXT(","));
}

int32 StateGraphDefaultArraySize(
    const UClass* NodeClass,
    const FStructProperty* RuntimeProperty,
    const FArrayProperty* ArrayProperty)
{
    const UObject* DefaultNode = NodeClass
        ? NodeClass->GetDefaultObject() : nullptr;
    const void* RuntimeMemory = DefaultNode && RuntimeProperty
        ? RuntimeProperty->ContainerPtrToValuePtr<void>(DefaultNode)
        : nullptr;
    const void* ArrayMemory = RuntimeMemory && ArrayProperty
        ? ArrayProperty->ContainerPtrToValuePtr<void>(RuntimeMemory)
        : nullptr;
    if (!ArrayProperty || !ArrayMemory)
    {
        return INDEX_NONE;
    }
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(ArrayMemory));
    return ArrayHelper.Num();
}

FString StateGraphDedicatedArrayKind(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    if (!RuntimeStruct || !ArrayProperty
        || !HasEditableStateGraphPropertyFlags(ArrayProperty))
    {
        return FString();
    }
    const FString Key = StateGraphArrayKey(RuntimeStruct, ArrayProperty);
    if (Key == TEXT("AnimNode_BlendListByBool.BlendPose")
        || Key == TEXT("AnimNode_BlendListByBool.BlendTime"))
    {
        return TEXT("bool_blend");
    }
    if (Key == TEXT("AnimNode_BlendListByInt.BlendPose")
        || Key == TEXT("AnimNode_BlendListByInt.BlendTime"))
    {
        return TEXT("int_blend");
    }
    if (Key == TEXT("AnimNode_LayeredBoneBlend.BlendPoses")
        || Key == TEXT("AnimNode_LayeredBoneBlend.BlendWeights")
        || Key == TEXT("AnimNode_LayeredBoneBlend.LayerSetup"))
    {
        return TEXT("layered_blend_per_bone");
    }
    return FString();
}

FString StateGraphAssetBoundArrayKind(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    if (!RuntimeStruct || !ArrayProperty
        || !HasEditableStateGraphPropertyFlags(ArrayProperty))
    {
        return FString();
    }
    return StateGraphArrayKey(RuntimeStruct, ArrayProperty)
            == TEXT("AnimNode_RetargetPoseFromMesh.OverrideSetsToApply")
        ? TEXT("ik_retarget_override_sets")
        : FString();
}

FString StateGraphCoupledPoseArrayKind(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    if (!RuntimeStruct || !ArrayProperty
        || !HasEditableStateGraphPropertyFlags(ArrayProperty))
    {
        return FString();
    }
    const FString Key = StateGraphArrayKey(RuntimeStruct, ArrayProperty);
    if (Key == TEXT("AnimNode_BlendListByEnum.BlendPose")
        || Key == TEXT("AnimNode_BlendListByEnum.BlendTime"))
    {
        return TEXT("blend_list_by_enum");
    }
    if (Key == TEXT("AnimNode_MultiWayBlend.Poses")
        || Key == TEXT("AnimNode_MultiWayBlend.DesiredAlphas"))
    {
        return TEXT("multi_way_blend");
    }
    return FString();
}

FString StateGraphCoupledConstraintArrayKind(
    const UScriptStruct* RuntimeStruct,
    const FArrayProperty* ArrayProperty)
{
    if (!RuntimeStruct || !ArrayProperty
        || !HasEditableStateGraphPropertyFlags(ArrayProperty))
    {
        return FString();
    }
    const FString Key = StateGraphArrayKey(RuntimeStruct, ArrayProperty);
    return Key == TEXT("AnimNode_Constraint.ConstraintSetup")
            || Key == TEXT("AnimNode_Constraint.ConstraintWeights")
        ? TEXT("constraint_setup_weights")
        : FString();
}

FString StateGraphUnsupportedPropertyKind(
    const FProperty* Property,
    const UScriptStruct* RuntimeStruct = nullptr)
{
    if (!Property || !HasEditableStateGraphPropertyFlags(Property))
    {
        return FString();
    }
    if (!StateGraphTopologyPropertyKind(Property).IsEmpty())
    {
        return FString();
    }
    if (Property->IsA<FSoftClassProperty>())
    {
        return TEXT("soft_class");
    }
    if (Property->IsA<FClassProperty>())
    {
        return IsSupportedStateGraphClassProperty(Property)
            ? FString() : TEXT("class");
    }
    if (Property->IsA<FSoftObjectProperty>())
    {
        return TEXT("soft_object");
    }
    if (Property->IsA<FWeakObjectProperty>())
    {
        return TEXT("weak_object");
    }
    if (Property->IsA<FLazyObjectProperty>())
    {
        return TEXT("lazy_object");
    }
    if (Property->IsA<FSetProperty>())
    {
        return IsSupportedStateGraphSetProperty(Property)
            ? FString() : TEXT("set");
    }
    if (Property->IsA<FMapProperty>())
    {
        return IsSupportedStateGraphMapProperty(Property)
            ? FString() : TEXT("map");
    }
    if (Property->IsA<FArrayProperty>())
    {
        const FArrayProperty* ArrayProperty =
            CastField<const FArrayProperty>(Property);
        return IsSupportedStateGraphWholeArray(
                RuntimeStruct,
                ArrayProperty)
                || !StateGraphDedicatedArrayKind(
                        RuntimeStruct,
                        ArrayProperty).IsEmpty()
                || !StateGraphCoupledPoseArrayKind(
                        RuntimeStruct,
                        ArrayProperty).IsEmpty()
                || !StateGraphCoupledConstraintArrayKind(
                        RuntimeStruct,
                        ArrayProperty).IsEmpty()
            ? FString() : TEXT("structural_array");
    }
    if (const FStructProperty* StructProperty =
            CastField<const FStructProperty>(Property))
    {
        return IsStateGraphAtomicStruct(StructProperty->Struct)
                || IsStateGraphNestedStruct(StructProperty->Struct)
            ? FString()
            : TEXT("struct:")
                + (StructProperty->Struct
                    ? StructProperty->Struct->GetName()
                    : TEXT("Missing"));
    }
    if (Property->IsA<FObjectPropertyBase>()
        && !IsSupportedStateGraphObjectProperty(Property))
    {
        return TEXT("object");
    }
    return IsStateGraphLeafPropertyType(Property)
        ? FString() : TEXT("other");
}

bool IsResizableStateGraphArrayPath(
    const FString& Path,
    const FArrayProperty* ArrayProperty,
    const int32 CurrentSize)
{
    TArray<FString> Segments;
    Path.ParseIntoArray(Segments, TEXT("."), true);
    return ArrayProperty && IsSupportedStateGraphArrayInner(ArrayProperty->Inner)
        && CurrentSize >= 0 && CurrentSize <= MaxStateGraphArrayItems
        && Segments.Num() >= 3;
}

void GatherStateGraphPropertyValue(
    const FStructProperty* RuntimeNodeProperty,
    const void* RuntimeNodeMemory,
    FProperty* RootProperty,
    FProperty* Property,
    const void* ValueMemory,
    const FString& Path,
    const int32 Depth,
    const bool bRequireEditableFlag,
    TArray<FStateGraphNodePropertyRef>* OutProperties,
    TArray<FStateGraphNodeArrayRef>* OutArrays)
{
    if (!RuntimeNodeProperty || !RuntimeNodeMemory || !RootProperty
        || !Property || !ValueMemory || Path.Len() > 240
        || Depth > MaxStateGraphPropertyDepth
        || (bRequireEditableFlag
            && !HasEditableStateGraphPropertyFlags(Property)))
    {
        return;
    }
    if (const FArrayProperty* WholeArrayProperty =
            CastField<const FArrayProperty>(Property);
        IsSupportedStateGraphWholeArray(
            RuntimeNodeProperty->Struct,
            WholeArrayProperty))
    {
        if (!OutProperties
            || OutProperties->Num() >= MaxStateGraphPropertyRefs)
        {
            return;
        }
        FStateGraphNodePropertyRef& Reference =
            OutProperties->AddDefaulted_GetRef();
        Reference.RuntimeNodeProperty = RuntimeNodeProperty;
        Reference.Property = Property;
        Reference.RootProperty = RootProperty;
        Reference.RuntimeNodeMemory = RuntimeNodeMemory;
        Reference.ValueMemory = ValueMemory;
        Reference.Path = Path;
        return;
    }
    if (IsStateGraphLeafPropertyType(Property))
    {
        if (!OutProperties
            || OutProperties->Num() >= MaxStateGraphPropertyRefs)
        {
            return;
        }
        FStateGraphNodePropertyRef& Reference =
            OutProperties->AddDefaulted_GetRef();
        Reference.RuntimeNodeProperty = RuntimeNodeProperty;
        Reference.Property = Property;
        Reference.RootProperty = RootProperty;
        Reference.RuntimeNodeMemory = RuntimeNodeMemory;
        Reference.ValueMemory = ValueMemory;
        Reference.Path = Path;
        return;
    }
    if (const FStructProperty* StructProperty =
            CastField<const FStructProperty>(Property))
    {
        if (!IsStateGraphNestedStruct(StructProperty->Struct))
        {
            return;
        }
        for (TFieldIterator<FProperty> It(
                 StructProperty->Struct,
                 EFieldIteratorFlags::IncludeSuper);
             It;
             ++It)
        {
            FProperty* Child = *It;
            if (!Child)
            {
                continue;
            }
            const int32 ArrayDim = FMath::Max(Child->ArrayDim, 1);
            for (int32 Index = 0; Index < ArrayDim; ++Index)
            {
                const FString ChildPath = Path + TEXT(".")
                    + Child->GetName()
                    + (ArrayDim > 1
                        ? FString::Printf(TEXT("[%d]"), Index)
                        : FString());
                GatherStateGraphPropertyValue(
                    RuntimeNodeProperty,
                    RuntimeNodeMemory,
                    RootProperty,
                    Child,
                    Child->ContainerPtrToValuePtr<void>(ValueMemory, Index),
                    ChildPath,
                    Depth + 1,
                    true,
                    OutProperties,
                    OutArrays);
            }
        }
        return;
    }
    if (const FArrayProperty* ArrayProperty =
            CastField<const FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayHelper(
            ArrayProperty, const_cast<void*>(ValueMemory));
        if (OutArrays && OutArrays->Num() < MaxStateGraphArrayRefs)
        {
            FStateGraphNodeArrayRef& Reference =
                OutArrays->AddDefaulted_GetRef();
            Reference.RuntimeNodeProperty = RuntimeNodeProperty;
            Reference.ArrayProperty =
                const_cast<FArrayProperty*>(ArrayProperty);
            Reference.RootProperty = RootProperty;
            Reference.RuntimeNodeMemory = RuntimeNodeMemory;
            Reference.ValueMemory = ValueMemory;
            Reference.Path = Path;
            Reference.bResizable = IsResizableStateGraphArrayPath(
                Path, ArrayProperty, ArrayHelper.Num());
        }
        if (ArrayHelper.Num() > MaxStateGraphArrayItems)
        {
            return;
        }
        for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
        {
            GatherStateGraphPropertyValue(
                RuntimeNodeProperty,
                RuntimeNodeMemory,
                RootProperty,
                ArrayProperty->Inner,
                ArrayHelper.GetRawPtr(Index),
                Path + FString::Printf(TEXT("[%d]"), Index),
                Depth + 1,
                false,
                OutProperties,
                OutArrays);
        }
    }
}

TArray<FStateGraphNodePropertyRef> GetStateGraphNodeProperties(
    const UAnimGraphNode_Base* Node,
    const void* OverrideRuntimeNodeMemory = nullptr)
{
    TArray<FStateGraphNodePropertyRef> Result;
    const FStructProperty* RuntimeNodeProperty =
        FindStateGraphRuntimeNodeProperty(Node);
    const void* RuntimeNodeMemory = OverrideRuntimeNodeMemory
        ? OverrideRuntimeNodeMemory
        : (RuntimeNodeProperty
            ? RuntimeNodeProperty->ContainerPtrToValuePtr<void>(Node)
            : nullptr);
    if (!RuntimeNodeProperty || !RuntimeNodeProperty->Struct
        || !RuntimeNodeMemory)
    {
        return Result;
    }
    for (TFieldIterator<FProperty> It(
             RuntimeNodeProperty->Struct,
             EFieldIteratorFlags::IncludeSuper);
         It;
         ++It)
    {
        FProperty* Property = *It;
        if (!HasEditableStateGraphPropertyFlags(Property)
            || StateGraphNodeHasPinNamed(Node, Property->GetFName()))
        {
            continue;
        }
        const int32 ArrayDim = FMath::Max(Property->ArrayDim, 1);
        for (int32 Index = 0; Index < ArrayDim; ++Index)
        {
            GatherStateGraphPropertyValue(
                RuntimeNodeProperty,
                RuntimeNodeMemory,
                Property,
                Property,
                Property->ContainerPtrToValuePtr<void>(
                    RuntimeNodeMemory, Index),
                RuntimeNodeProperty->GetName() + TEXT(".")
                    + Property->GetName()
                    + (ArrayDim > 1
                        ? FString::Printf(TEXT("[%d]"), Index)
                        : FString()),
                0,
                true,
                &Result,
                nullptr);
        }
    }
    Result.Sort([](
        const FStateGraphNodePropertyRef& A,
        const FStateGraphNodePropertyRef& B)
    {
        return A.Path < B.Path;
    });
    return Result;
}

TArray<FStateGraphNodeArrayRef> GetStateGraphNodeArrays(
    const UAnimGraphNode_Base* Node,
    const void* OverrideRuntimeNodeMemory = nullptr)
{
    TArray<FStateGraphNodeArrayRef> Result;
    const FStructProperty* RuntimeNodeProperty =
        FindStateGraphRuntimeNodeProperty(Node);
    const void* RuntimeNodeMemory = OverrideRuntimeNodeMemory
        ? OverrideRuntimeNodeMemory
        : (RuntimeNodeProperty
            ? RuntimeNodeProperty->ContainerPtrToValuePtr<void>(Node)
            : nullptr);
    if (!RuntimeNodeProperty || !RuntimeNodeProperty->Struct
        || !RuntimeNodeMemory)
    {
        return Result;
    }
    for (TFieldIterator<FProperty> It(
             RuntimeNodeProperty->Struct,
             EFieldIteratorFlags::IncludeSuper);
         It;
         ++It)
    {
        FProperty* Property = *It;
        if (!HasEditableStateGraphPropertyFlags(Property)
            || StateGraphNodeHasPinNamed(Node, Property->GetFName()))
        {
            continue;
        }
        const int32 ArrayDim = FMath::Max(Property->ArrayDim, 1);
        for (int32 Index = 0; Index < ArrayDim; ++Index)
        {
            GatherStateGraphPropertyValue(
                RuntimeNodeProperty,
                RuntimeNodeMemory,
                Property,
                Property,
                Property->ContainerPtrToValuePtr<void>(
                    RuntimeNodeMemory, Index),
                RuntimeNodeProperty->GetName() + TEXT(".")
                    + Property->GetName()
                    + (ArrayDim > 1
                        ? FString::Printf(TEXT("[%d]"), Index)
                        : FString()),
                0,
                true,
                nullptr,
                &Result);
        }
    }
    Result.Sort([](
        const FStateGraphNodeArrayRef& A,
        const FStateGraphNodeArrayRef& B)
    {
        return A.Path < B.Path;
    });
    return Result;
}

bool ResolveStateGraphNodeProperty(
    const UAnimGraphNode_Base* Node,
    const FString& PropertyPath,
    const void* OverrideRuntimeNodeMemory,
    FStateGraphNodePropertyRef& OutReference)
{
    OutReference = FStateGraphNodePropertyRef();
    if (PropertyPath.IsEmpty() || PropertyPath.Len() > 240)
    {
        return false;
    }
    for (const FStateGraphNodePropertyRef& Reference :
         GetStateGraphNodeProperties(Node, OverrideRuntimeNodeMemory))
    {
        if (Reference.Path == PropertyPath)
        {
            OutReference = Reference;
            return true;
        }
    }
    return false;
}

bool ResolveStateGraphNodeProperty(
    const UAnimGraphNode_Base* Node,
    const FString& PropertyPath,
    FStateGraphNodePropertyRef& OutReference)
{
    return ResolveStateGraphNodeProperty(
        Node, PropertyPath, nullptr, OutReference);
}

bool ResolveStateGraphNodeArray(
    const UAnimGraphNode_Base* Node,
    const FString& PropertyPath,
    const void* OverrideRuntimeNodeMemory,
    FStateGraphNodeArrayRef& OutReference)
{
    OutReference = FStateGraphNodeArrayRef();
    if (PropertyPath.IsEmpty() || PropertyPath.Len() > 240)
    {
        return false;
    }
    for (const FStateGraphNodeArrayRef& Reference :
         GetStateGraphNodeArrays(Node, OverrideRuntimeNodeMemory))
    {
        if (Reference.Path == PropertyPath)
        {
            OutReference = Reference;
            return true;
        }
    }
    return false;
}

bool ResolveStateGraphNodeArray(
    const UAnimGraphNode_Base* Node,
    const FString& PropertyPath,
    FStateGraphNodeArrayRef& OutReference)
{
    return ResolveStateGraphNodeArray(
        Node, PropertyPath, nullptr, OutReference);
}

bool ResolveStateGraphPoseArray(
    const UAnimGraphNode_Base* Node,
    const FString& RequestedPosePath,
    FStateGraphPoseArrayRef& OutReference)
{
    OutReference = FStateGraphPoseArrayRef();
    if (!Node || !Node->GetClass())
    {
        return false;
    }
    FString PosePath;
    FString CompanionPath;
    const FString ClassPath = Node->GetClass()->GetPathName();
    if (ClassPath
        == TEXT("/Script/AnimGraph.AnimGraphNode_BlendListByEnum"))
    {
        OutReference.Kind = TEXT("blend_list_by_enum");
        PosePath = TEXT("Node.BlendPose");
        CompanionPath = TEXT("Node.BlendTime");
        OutReference.PosePinPrefix = TEXT("BlendPose_");
        OutReference.CompanionPinPrefix = TEXT("BlendTime_");
    }
    else if (ClassPath
        == TEXT("/Script/AnimGraph.AnimGraphNode_MultiWayBlend"))
    {
        OutReference.Kind = TEXT("multi_way_blend");
        PosePath = TEXT("Node.Poses");
        CompanionPath = TEXT("Node.DesiredAlphas");
        OutReference.PosePinPrefix = TEXT("Poses_");
        OutReference.CompanionPinPrefix = TEXT("DesiredAlphas_");
        OutReference.bCompanionUnitInterval = true;
    }
    else
    {
        return false;
    }
    if (!RequestedPosePath.IsEmpty()
        && RequestedPosePath != PosePath)
    {
        return false;
    }
    if (!ResolveStateGraphNodeArray(
            Node, PosePath, OutReference.PoseArray)
        || !ResolveStateGraphNodeArray(
            Node, CompanionPath, OutReference.CompanionArray))
    {
        return false;
    }
    const FStructProperty* PoseInner = OutReference.PoseArray.ArrayProperty
        ? CastField<const FStructProperty>(
            OutReference.PoseArray.ArrayProperty->Inner)
        : nullptr;
    if (!PoseInner || !PoseInner->Struct
        || PoseInner->Struct->GetFName() != TEXT("PoseLink")
        || !OutReference.CompanionArray.ArrayProperty
        || !OutReference.CompanionArray.ArrayProperty->Inner
            ->IsA<FFloatProperty>())
    {
        return false;
    }
    if (OutReference.Kind == TEXT("blend_list_by_enum"))
    {
        OutReference.BoundEnumProperty =
            FindFProperty<FObjectPropertyBase>(
                Node->GetClass(), TEXT("BoundEnum"));
        OutReference.BoundEnum = OutReference.BoundEnumProperty
            ? Cast<UEnum>(
                OutReference.BoundEnumProperty
                    ->GetObjectPropertyValue_InContainer(Node))
            : nullptr;
        OutReference.EnumEntriesProperty = FindFProperty<FArrayProperty>(
            Node->GetClass(), TEXT("VisibleEnumEntries"));
        const FNameProperty* EntryProperty =
            OutReference.EnumEntriesProperty
                ? CastField<const FNameProperty>(
                    OutReference.EnumEntriesProperty->Inner)
                : nullptr;
        OutReference.EnumEntriesMemory =
            OutReference.EnumEntriesProperty
                ? OutReference.EnumEntriesProperty
                    ->ContainerPtrToValuePtr<void>(
                        const_cast<UAnimGraphNode_Base*>(Node))
                : nullptr;
        if (!OutReference.BoundEnumProperty
            || !OutReference.BoundEnum
            || !OutReference.EnumEntriesProperty
            || !EntryProperty
            || !OutReference.EnumEntriesMemory)
        {
            return false;
        }
    }
    return true;
}

bool ResolveStateGraphConstraintArray(
    const UAnimGraphNode_Base* Node,
    const FString& RequestedConstraintPath,
    const void* OverrideRuntimeNodeMemory,
    FStateGraphConstraintArrayRef& OutReference)
{
    OutReference = FStateGraphConstraintArrayRef();
    if (!Node || !Node->GetClass()
        || Node->GetClass()->GetPathName()
            != TEXT("/Script/AnimGraph.AnimGraphNode_Constraint"))
    {
        return false;
    }
    const FString ConstraintPath = TEXT("Node.ConstraintSetup");
    const FString WeightPath = TEXT("Node.ConstraintWeights");
    if (!RequestedConstraintPath.IsEmpty()
        && RequestedConstraintPath != ConstraintPath)
    {
        return false;
    }
    if (!ResolveStateGraphNodeArray(
            Node,
            ConstraintPath,
            OverrideRuntimeNodeMemory,
            OutReference.ConstraintArray)
        || !ResolveStateGraphNodeArray(
            Node,
            WeightPath,
            OverrideRuntimeNodeMemory,
            OutReference.WeightArray))
    {
        return false;
    }
    OutReference.ConstraintProperty =
        OutReference.ConstraintArray.ArrayProperty
        ? CastField<const FStructProperty>(
            OutReference.ConstraintArray.ArrayProperty->Inner)
        : nullptr;
    const FFloatProperty* WeightProperty =
        OutReference.WeightArray.ArrayProperty
        ? CastField<const FFloatProperty>(
            OutReference.WeightArray.ArrayProperty->Inner)
        : nullptr;
    OutReference.TargetBoneProperty =
        OutReference.ConstraintProperty
            && OutReference.ConstraintProperty->Struct
        ? FindFProperty<FStructProperty>(
            OutReference.ConstraintProperty->Struct,
            TEXT("TargetBone"))
        : nullptr;
    OutReference.BoneNameProperty =
        OutReference.TargetBoneProperty
            && OutReference.TargetBoneProperty->Struct
        ? FindFProperty<FNameProperty>(
            OutReference.TargetBoneProperty->Struct,
            TEXT("BoneName"))
        : nullptr;
    if (!OutReference.ConstraintProperty
        || !OutReference.ConstraintProperty->Struct
        || OutReference.ConstraintProperty->Struct->GetFName()
            != TEXT("Constraint")
        || !WeightProperty
        || !OutReference.TargetBoneProperty
        || !OutReference.TargetBoneProperty->Struct
        || OutReference.TargetBoneProperty->Struct->GetFName()
            != TEXT("BoneReference")
        || !OutReference.BoneNameProperty)
    {
        return false;
    }
    OutReference.Kind = TEXT("constraint_setup_weights");
    return true;
}

bool ResolveStateGraphConstraintArray(
    const UAnimGraphNode_Base* Node,
    const FString& RequestedConstraintPath,
    FStateGraphConstraintArrayRef& OutReference)
{
    return ResolveStateGraphConstraintArray(
        Node,
        RequestedConstraintPath,
        nullptr,
        OutReference);
}

FString ExportStateGraphNodePropertyValue(
    const FStateGraphNodePropertyRef& Reference)
{
    FString Value;
    if (Reference.Property && Reference.ValueMemory)
    {
        if (const FArrayProperty* ArrayProperty =
                CastField<const FArrayProperty>(Reference.Property))
        {
            FScriptArrayHelper ArrayHelper(
                ArrayProperty,
                const_cast<void*>(Reference.ValueMemory));
            TArray<FString> Elements;
            Elements.Reserve(ArrayHelper.Num());
            for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
            {
                FString Element;
                ArrayProperty->Inner->ExportTextItem_Direct(
                    Element,
                    ArrayHelper.GetRawPtr(Index),
                    nullptr,
                    nullptr,
                    PPF_None);
                Elements.Add(Element);
            }
            Value = TEXT("(") + FString::Join(Elements, TEXT(","))
                + TEXT(")");
        }
        else if (const FSetProperty* SetProperty =
                CastField<const FSetProperty>(Reference.Property))
        {
            FScriptSetHelper SetHelper(
                SetProperty,
                const_cast<void*>(Reference.ValueMemory));
            TArray<FString> Elements;
            for (int32 Index = 0;
                 Index < SetHelper.GetMaxIndex();
                 ++Index)
            {
                if (!SetHelper.IsValidIndex(Index))
                {
                    continue;
                }
                FString Element;
                SetProperty->ElementProp->ExportTextItem_Direct(
                    Element,
                    SetHelper.GetElementPtr(Index),
                    nullptr,
                    nullptr,
                    PPF_None);
                Elements.Add(Element);
            }
            Elements.Sort();
            Value = TEXT("(") + FString::Join(Elements, TEXT(","))
                + TEXT(")");
        }
        else if (const FMapProperty* MapProperty =
                     CastField<const FMapProperty>(Reference.Property))
        {
            FScriptMapHelper MapHelper(
                MapProperty,
                const_cast<void*>(Reference.ValueMemory));
            TArray<FString> Pairs;
            for (int32 Index = 0;
                 Index < MapHelper.GetMaxIndex();
                 ++Index)
            {
                if (!MapHelper.IsValidIndex(Index))
                {
                    continue;
                }
                FString Key;
                FString MapValue;
                MapProperty->KeyProp->ExportTextItem_Direct(
                    Key,
                    MapHelper.GetKeyPtr(Index),
                    nullptr,
                    nullptr,
                    PPF_None);
                MapProperty->ValueProp->ExportTextItem_Direct(
                    MapValue,
                    MapHelper.GetValuePtr(Index),
                    nullptr,
                    nullptr,
                    PPF_None);
                Pairs.Add(TEXT("(") + Key + TEXT(",")
                    + MapValue + TEXT(")"));
            }
            Pairs.Sort();
            Value = TEXT("(") + FString::Join(Pairs, TEXT(","))
                + TEXT(")");
        }
        else if (const FObjectPropertyBase* ObjectProperty =
                CastField<const FObjectPropertyBase>(Reference.Property))
        {
            const UObject* Object = ObjectProperty->GetObjectPropertyValue(
                Reference.ValueMemory);
            Value = Object ? Object->GetPathName() : TEXT("None");
        }
        else
        {
            Reference.Property->ExportTextItem_Direct(
                Value,
                Reference.ValueMemory,
                nullptr,
                nullptr,
                PPF_None);
        }
    }
    return Value;
}

FString StateGraphNodePropertyStateString(
    const FStateGraphNodePropertyRef& Reference)
{
    return FString::Printf(
        TEXT("Path=%s|Type=%s|Value=%s"),
        *EncodeStateGraphValue(Reference.Path),
        Reference.Property
            ? *EncodeStateGraphValue(
                Reference.Property->GetCPPType())
            : TEXT("Missing"),
        *EncodeStateGraphValue(
            ExportStateGraphNodePropertyValue(Reference)));
}

int32 StateGraphNodeArraySize(const FStateGraphNodeArrayRef& Reference)
{
    if (!Reference.ArrayProperty || !Reference.ValueMemory)
    {
        return INDEX_NONE;
    }
    FScriptArrayHelper ArrayHelper(
        Reference.ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    return ArrayHelper.Num();
}

FStateGraphNodePropertyRef StateGraphArrayPropertyReference(
    const FStateGraphNodeArrayRef& ArrayReference)
{
    FStateGraphNodePropertyRef Reference;
    Reference.RuntimeNodeProperty = ArrayReference.RuntimeNodeProperty;
    Reference.Property = ArrayReference.ArrayProperty;
    Reference.RootProperty = ArrayReference.RootProperty;
    Reference.RuntimeNodeMemory = ArrayReference.RuntimeNodeMemory;
    Reference.ValueMemory = ArrayReference.ValueMemory;
    Reference.Path = ArrayReference.Path;
    return Reference;
}

FString StateGraphNodeArrayStateString(
    const FStateGraphNodeArrayRef& Reference)
{
    return FString::Printf(
        TEXT("Path=%s|ElementType=%s|Num=%d|Max=%d|Resizable=%s"),
        *EncodeStateGraphValue(Reference.Path),
        Reference.ArrayProperty && Reference.ArrayProperty->Inner
            ? *EncodeStateGraphValue(
                Reference.ArrayProperty->Inner->GetCPPType())
            : TEXT("Missing"),
        StateGraphNodeArraySize(Reference),
        MaxStateGraphArrayItems,
        Reference.bResizable ? TEXT("true") : TEXT("false"));
}

bool GetStateGraphPoseArrayCompanionValues(
    const FStateGraphPoseArrayRef& Reference,
    TArray<float>& OutValues)
{
    OutValues.Reset();
    const FArrayProperty* ArrayProperty =
        Reference.CompanionArray.ArrayProperty;
    const FFloatProperty* FloatProperty = ArrayProperty
        ? CastField<const FFloatProperty>(ArrayProperty->Inner)
        : nullptr;
    if (!ArrayProperty || !FloatProperty
        || !Reference.CompanionArray.ValueMemory)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(Reference.CompanionArray.ValueMemory));
    OutValues.Reserve(ArrayHelper.Num());
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        OutValues.Add(FloatProperty->GetPropertyValue(
            ArrayHelper.GetRawPtr(Index)));
    }
    return true;
}

bool GetStateGraphFloatArrayValues(
    const FStateGraphNodeArrayRef& Reference,
    TArray<float>& OutValues)
{
    OutValues.Reset();
    const FArrayProperty* ArrayProperty = Reference.ArrayProperty;
    const FFloatProperty* FloatProperty = ArrayProperty
        ? CastField<const FFloatProperty>(ArrayProperty->Inner)
        : nullptr;
    if (!ArrayProperty || !FloatProperty || !Reference.ValueMemory)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    OutValues.Reserve(ArrayHelper.Num());
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        OutValues.Add(FloatProperty->GetPropertyValue(
            ArrayHelper.GetRawPtr(Index)));
    }
    return true;
}

bool GetStateGraphPoseArrayEnumEntries(
    const FStateGraphPoseArrayRef& Reference,
    TArray<FString>& OutEntries)
{
    OutEntries.Reset();
    if (Reference.Kind != TEXT("blend_list_by_enum"))
    {
        return true;
    }
    const FNameProperty* NameProperty =
        Reference.EnumEntriesProperty
            ? CastField<const FNameProperty>(
                Reference.EnumEntriesProperty->Inner)
            : nullptr;
    if (!Reference.EnumEntriesProperty || !NameProperty
        || !Reference.EnumEntriesMemory)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        Reference.EnumEntriesProperty,
        Reference.EnumEntriesMemory);
    OutEntries.Reserve(ArrayHelper.Num());
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        OutEntries.Add(NameProperty->GetPropertyValue(
            ArrayHelper.GetRawPtr(Index)).ToString());
    }
    return true;
}

int32 CountStateGraphIndexedPins(
    const UAnimGraphNode_Base* Node,
    const FString& Prefix)
{
    int32 Count = 0;
    if (!Node || Prefix.IsEmpty())
    {
        return Count;
    }
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Input)
        {
            continue;
        }
        const FString PinName = Pin->PinName.ToString();
        const FString Suffix = PinName.StartsWith(Prefix)
            ? PinName.Mid(Prefix.Len()) : FString();
        if (!Suffix.IsEmpty() && Suffix.IsNumeric())
        {
            ++Count;
        }
    }
    return Count;
}

void BreakStateGraphIndexedPinLinksFrom(
    UAnimGraphNode_Base* Node,
    const TArray<FString>& Prefixes,
    const int32 FirstRemovedIndex)
{
    if (!Node || FirstRemovedIndex < 0)
    {
        return;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Input)
        {
            continue;
        }
        const FString PinName = Pin->PinName.ToString();
        for (const FString& Prefix : Prefixes)
        {
            if (!PinName.StartsWith(Prefix))
            {
                continue;
            }
            const FString Suffix = PinName.Mid(Prefix.Len());
            if (!Suffix.IsEmpty() && Suffix.IsNumeric()
                && FCString::Atoi(*Suffix) >= FirstRemovedIndex)
            {
                Pin->BreakAllPinLinks(true);
            }
            break;
        }
    }
}

void RemoveUnlinkedStateGraphOrphanPins(UAnimGraphNode_Base* Node)
{
    if (!Node)
    {
        return;
    }
    for (int32 Index = Node->Pins.Num() - 1; Index >= 0; --Index)
    {
        UEdGraphPin* Pin = Node->Pins[Index];
        if (Pin && Pin->bOrphanedPin && Pin->LinkedTo.IsEmpty())
        {
            Node->RemovePin(Pin);
        }
    }
}

FString StateGraphPoseArrayStateString(
    const UAnimGraphNode_Base* Node,
    const FStateGraphPoseArrayRef& Reference)
{
    TArray<float> Values;
    TArray<FString> ValueStrings;
    TArray<FString> EnumEntries;
    if (GetStateGraphPoseArrayCompanionValues(Reference, Values))
    {
        for (const float Value : Values)
        {
            ValueStrings.Add(FString::Printf(
                TEXT("%.6f"), static_cast<double>(Value)));
        }
    }
    GetStateGraphPoseArrayEnumEntries(Reference, EnumEntries);
    return FString::Printf(
        TEXT("Kind=%s|PosePath=%s|CompanionPath=%s|EnumPath=%s|EnumEntries=(%s)|Num=%d|CompanionNum=%d|CompanionValues=(%s)|PosePinCount=%d|CompanionPinCount=%d|Min=2|Max=%d"),
        *Reference.Kind,
        *EncodeStateGraphValue(Reference.PoseArray.Path),
        *EncodeStateGraphValue(Reference.CompanionArray.Path),
        Reference.BoundEnum
            ? *EncodeStateGraphValue(
                Reference.BoundEnum->GetPathName())
            : TEXT("None"),
        *FString::Join(EnumEntries, TEXT(",")),
        StateGraphNodeArraySize(Reference.PoseArray),
        StateGraphNodeArraySize(Reference.CompanionArray),
        *FString::Join(ValueStrings, TEXT(",")),
        CountStateGraphIndexedPins(Node, Reference.PosePinPrefix),
        CountStateGraphIndexedPins(Node, Reference.CompanionPinPrefix),
        MaxStateGraphArrayItems);
}

FString StateGraphConstraintArrayStateString(
    const UAnimGraphNode_Base* Node,
    const FStateGraphConstraintArrayRef& Reference)
{
    TArray<float> Weights;
    TArray<FString> WeightStrings;
    if (GetStateGraphFloatArrayValues(Reference.WeightArray, Weights))
    {
        for (const float Weight : Weights)
        {
            WeightStrings.Add(FString::Printf(
                TEXT("%.6f"), static_cast<double>(Weight)));
        }
    }
    return FString::Printf(
        TEXT("Kind=%s|ConstraintPath=%s|WeightPath=%s|ConstraintValues=%s|Num=%d|WeightNum=%d|Weights=(%s)|ConstraintPinCount=%d|WeightPinCount=%d|Min=0|Max=%d"),
        *Reference.Kind,
        *EncodeStateGraphValue(Reference.ConstraintArray.Path),
        *EncodeStateGraphValue(Reference.WeightArray.Path),
        *EncodeStateGraphValue(ExportStateGraphNodePropertyValue(
            StateGraphArrayPropertyReference(
                Reference.ConstraintArray))),
        StateGraphNodeArraySize(Reference.ConstraintArray),
        StateGraphNodeArraySize(Reference.WeightArray),
        *FString::Join(WeightStrings, TEXT(",")),
        CountStateGraphIndexedPins(Node, TEXT("ConstraintSetup_")),
        CountStateGraphIndexedPins(Node, TEXT("ConstraintWeights_")),
        MaxStateGraphArrayItems);
}

int32 FindStateGraphEnumEntryIndex(
    const UEnum* Enum,
    const FString& Entry)
{
    if (!Enum || Entry.IsEmpty())
    {
        return INDEX_NONE;
    }
    const FName EntryName(*Entry);
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        if (Enum->GetNameStringByIndex(Index) == Entry
            || Enum->GetNameByIndex(Index) == EntryName)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

bool ValidateStateGraphPoseArrayValues(
    const FStateGraphPoseArrayRef& Reference,
    const int32 TargetSize,
    const TArray<float>& Values,
    const TArray<FString>& EnumEntries)
{
    if (TargetSize < 2 || TargetSize > MaxStateGraphArrayItems
        || Values.Num() != TargetSize)
    {
        return false;
    }
    if (Reference.Kind == TEXT("blend_list_by_enum"))
    {
        if (!Reference.BoundEnum
            || EnumEntries.Num() != TargetSize - 1)
        {
            return false;
        }
        TSet<FName> UniqueEntries;
        for (const FString& Entry : EnumEntries)
        {
            const int32 EnumIndex = FindStateGraphEnumEntryIndex(
                Reference.BoundEnum, Entry);
            const FName CanonicalEntry = EnumIndex != INDEX_NONE
                ? Reference.BoundEnum->GetNameByIndex(EnumIndex)
                : NAME_None;
            if (EnumIndex == INDEX_NONE
                || Reference.BoundEnum->HasMetaData(
                    TEXT("Hidden"), EnumIndex)
                || Entry.EndsWith(TEXT("_MAX"))
                || Entry.EndsWith(TEXT("::MAX"))
                || UniqueEntries.Contains(CanonicalEntry))
            {
                return false;
            }
            UniqueEntries.Add(CanonicalEntry);
        }
    }
    else if (!EnumEntries.IsEmpty())
    {
        return false;
    }
    for (const float Value : Values)
    {
        if (!FMath::IsFinite(Value)
            || (Reference.bCompanionUnitInterval
                ? Value < 0.0f || Value > 1.0f
                : Value < 0.0f || Value > 60.0f))
        {
            return false;
        }
    }
    return true;
}

bool ValidateStateGraphPropertyData(
    const FProperty* Property,
    const void* ValueMemory,
    const int32 Depth = 0)
{
    if (!Property || !ValueMemory || Depth > MaxStateGraphPropertyDepth)
    {
        return false;
    }
    if (const FNumericProperty* Numeric =
            CastField<const FNumericProperty>(Property))
    {
        return (Numeric->IsInteger()
                || FMath::IsFinite(
                    Numeric->GetFloatingPointPropertyValue(ValueMemory)))
            && PassesIKRigSettingClamp(Property, ValueMemory);
    }
    if (const FArrayProperty* ArrayProperty =
            CastField<const FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayHelper(
            ArrayProperty, const_cast<void*>(ValueMemory));
        if (ArrayHelper.Num() > MaxStateGraphArrayItems)
        {
            return false;
        }
        for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
        {
            if (!ValidateStateGraphPropertyData(
                    ArrayProperty->Inner,
                    ArrayHelper.GetRawPtr(Index),
                    Depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    if (const FSetProperty* SetProperty =
            CastField<const FSetProperty>(Property))
    {
        if (!IsSupportedStateGraphSetProperty(SetProperty))
        {
            return false;
        }
        FScriptSetHelper SetHelper(
            SetProperty, const_cast<void*>(ValueMemory));
        if (SetHelper.Num() > MaxStateGraphArrayItems)
        {
            return false;
        }
        for (int32 Index = 0;
             Index < SetHelper.GetMaxIndex();
             ++Index)
        {
            if (SetHelper.IsValidIndex(Index)
                && !ValidateStateGraphPropertyData(
                    SetProperty->ElementProp,
                    SetHelper.GetElementPtr(Index),
                    Depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    if (const FMapProperty* MapProperty =
            CastField<const FMapProperty>(Property))
    {
        if (!IsSupportedStateGraphMapProperty(MapProperty))
        {
            return false;
        }
        FScriptMapHelper MapHelper(
            MapProperty, const_cast<void*>(ValueMemory));
        if (MapHelper.Num() > MaxStateGraphArrayItems)
        {
            return false;
        }
        for (int32 Index = 0;
             Index < MapHelper.GetMaxIndex();
             ++Index)
        {
            if (!MapHelper.IsValidIndex(Index))
            {
                continue;
            }
            if (!ValidateStateGraphPropertyData(
                    MapProperty->KeyProp,
                    MapHelper.GetKeyPtr(Index),
                    Depth + 1)
                || !ValidateStateGraphPropertyData(
                    MapProperty->ValueProp,
                    MapHelper.GetValuePtr(Index),
                    Depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    if (const FStructProperty* StructProperty =
            CastField<const FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> It(
                 StructProperty->Struct,
                 EFieldIteratorFlags::IncludeSuper);
             It;
             ++It)
        {
            const FProperty* Child = *It;
            if (!Child)
            {
                continue;
            }
            const int32 ArrayDim = FMath::Max(Child->ArrayDim, 1);
            for (int32 Index = 0; Index < ArrayDim; ++Index)
            {
                if (!ValidateStateGraphPropertyData(
                        Child,
                        Child->ContainerPtrToValuePtr<void>(
                            ValueMemory, Index),
                        Depth + 1))
                {
                    return false;
                }
            }
        }
        return true;
    }
    if (const FObjectPropertyBase* ObjectProperty =
            CastField<const FObjectPropertyBase>(Property))
    {
        const UObject* Object = ObjectProperty->GetObjectPropertyValue(
            ValueMemory);
        if (const FClassProperty* ClassProperty =
                CastField<const FClassProperty>(Property))
        {
            if (!Object)
            {
                return true;
            }
            UClass* ResolvedClass = nullptr;
            return ResolveStateGraphAnimInstanceClass(
                    ClassProperty,
                    Object->GetPathName(),
                    ResolvedClass)
                && ResolvedClass == Object;
        }
        if (!Object)
        {
            return true;
        }
        UObject* ResolvedObject = nullptr;
        return ResolveStateGraphObjectPropertyValue(
                ObjectProperty,
                Object->GetPathName(),
                ResolvedObject)
            && ResolvedObject == Object;
    }
    return true;
}

bool ImportStateGraphNodePropertyValue(
    const FStateGraphNodePropertyRef& Reference,
    const FString& Input,
    FString& OutCanonicalValue)
{
    if (!Reference.Property || !Reference.ValueMemory
        || Input.Len() > 512
        || Input.Contains(TEXT("nan"), ESearchCase::IgnoreCase)
        || Input.Contains(TEXT("inf"), ESearchCase::IgnoreCase))
    {
        return false;
    }
    void* PropertyValue = const_cast<void*>(Reference.ValueMemory);
    if (const FNumericProperty* Numeric =
            CastField<const FNumericProperty>(Reference.Property))
    {
        const FString Trimmed = Input.TrimStartAndEnd();
        const FByteProperty* ByteProperty =
            CastField<const FByteProperty>(Reference.Property);
        const bool bByteEnum = ByteProperty && ByteProperty->Enum;
        bool bValidNumericText = bByteEnum;
        if (!bByteEnum && Numeric->IsInteger())
        {
            if (Reference.Property->IsA<FByteProperty>()
                || Reference.Property->IsA<FUInt16Property>()
                || Reference.Property->IsA<FUInt32Property>()
                || Reference.Property->IsA<FUInt64Property>())
            {
                uint64 ParsedValue = 0;
                bValidNumericText = LexTryParseString(
                    ParsedValue, *Trimmed);
            }
            else
            {
                int64 ParsedValue = 0;
                bValidNumericText = LexTryParseString(
                    ParsedValue, *Trimmed);
            }
        }
        else if (!bByteEnum)
        {
            double ParsedValue = 0.0;
            bValidNumericText = LexTryParseString(
                ParsedValue, *Trimmed)
                && FMath::IsFinite(ParsedValue);
        }
        if (!bValidNumericText)
        {
            return false;
        }
    }
    if (FClassProperty* ClassProperty =
            CastField<FClassProperty>(Reference.Property))
    {
        UClass* ClassValue = nullptr;
        if (!ResolveStateGraphAnimInstanceClass(
                ClassProperty, Input, ClassValue))
        {
            return false;
        }
        ClassProperty->SetObjectPropertyValue(
            PropertyValue, ClassValue);
    }
    else if (FObjectPropertyBase* ObjectProperty =
            CastField<FObjectPropertyBase>(Reference.Property))
    {
        UObject* Object = nullptr;
        if (!ResolveStateGraphObjectPropertyValue(
                ObjectProperty, Input, Object))
        {
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(PropertyValue, Object);
    }
    else
    {
        const TCHAR* ImportEnd = Reference.Property->ImportText_Direct(
            *Input,
            PropertyValue,
            nullptr,
            PPF_None);
        if (!ImportEnd
            || !FString(ImportEnd).TrimStartAndEnd().IsEmpty())
        {
            return false;
        }
    }
    if (!ValidateStateGraphPropertyData(
            Reference.Property, PropertyValue))
    {
        return false;
    }
    OutCanonicalValue = ExportStateGraphNodePropertyValue(Reference);
    return OutCanonicalValue.Len() <= 512;
}

bool ValidateStateGraphBoneReferenceArray(
    const UAnimGraphNode_Base* Node,
    const FStateGraphNodePropertyRef& Reference)
{
    const FArrayProperty* ArrayProperty =
        CastField<const FArrayProperty>(Reference.Property);
    const FStructProperty* InnerStruct = ArrayProperty
        ? CastField<const FStructProperty>(ArrayProperty->Inner)
        : nullptr;
    if (!InnerStruct || !InnerStruct->Struct
        || InnerStruct->Struct->GetFName() != TEXT("BoneReference"))
    {
        return true;
    }
    if (!Reference.ValueMemory)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    if (ArrayHelper.Num() == 0)
    {
        return true;
    }
    const UAnimBlueprint* AnimBlueprint = Node
        ? Cast<UAnimBlueprint>(
            FBlueprintEditorUtils::FindBlueprintForNode(Node))
        : nullptr;
    const USkeleton* Skeleton = AnimBlueprint
        ? AnimBlueprint->TargetSkeleton : nullptr;
    const FNameProperty* BoneNameProperty =
        FindFProperty<FNameProperty>(
            InnerStruct->Struct,
            TEXT("BoneName"));
    if (!Skeleton || !BoneNameProperty)
    {
        return false;
    }
    const FReferenceSkeleton& ReferenceSkeleton =
        Skeleton->GetReferenceSkeleton();
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        const void* ElementMemory = ArrayHelper.GetRawPtr(Index);
        const void* BoneNameMemory = BoneNameProperty
            ->ContainerPtrToValuePtr<void>(ElementMemory);
        const FName BoneName = BoneNameProperty->GetPropertyValue(
            BoneNameMemory);
        if (!BoneName.IsNone()
            && ReferenceSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
        {
            return false;
        }
    }
    return true;
}

bool ValidateStateGraphRetargetOverrideSetArray(
    const UAnimGraphNode_Base* Node,
    const FStateGraphNodePropertyRef& Reference)
{
    const FArrayProperty* ArrayProperty =
        CastField<const FArrayProperty>(Reference.Property);
    if (StateGraphAssetBoundArrayKind(
            Reference.RuntimeNodeProperty
                ? Reference.RuntimeNodeProperty->Struct
                : nullptr,
            ArrayProperty) != TEXT("ik_retarget_override_sets"))
    {
        return true;
    }
    const FNameProperty* NameProperty = ArrayProperty
        ? CastField<const FNameProperty>(ArrayProperty->Inner)
        : nullptr;
    if (!Node || !NameProperty || !Reference.ValueMemory)
    {
        return false;
    }
    FStateGraphNodePropertyRef AssetReference;
    const FObjectPropertyBase* AssetProperty =
        ResolveStateGraphNodeProperty(
            Node,
            TEXT("Node.IKRetargeterAsset"),
            AssetReference)
        ? CastField<const FObjectPropertyBase>(AssetReference.Property)
        : nullptr;
    const UIKRetargeter* Retargeter = AssetProperty
        ? Cast<UIKRetargeter>(
            AssetProperty->GetObjectPropertyValue(
                AssetReference.ValueMemory))
        : nullptr;
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    if (ArrayHelper.Num() > 0
        && (!Retargeter
            || !IsAllowedPath(
                NormalizePath(Retargeter->GetPathName()))))
    {
        return false;
    }
    TSet<FName> UniqueNames;
    const TMap<FName, FRetargetOverrideSet>* AvailableSets = Retargeter
        ? &Retargeter->GetOverrideSets()
        : nullptr;
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        const FName Name = NameProperty->GetPropertyValue(
            ArrayHelper.GetRawPtr(Index));
        if (Name.IsNone() || UniqueNames.Contains(Name)
            || !AvailableSets || !AvailableSets->Contains(Name))
        {
            return false;
        }
        UniqueNames.Add(Name);
    }
    return true;
}

bool ValidateStateGraphNodePropertyValue(
    const UAnimGraphNode_Base* Node,
    const FStateGraphNodePropertyRef& Reference,
    const FString& Input,
    FString& OutCanonicalValue)
{
    if (!Reference.RuntimeNodeProperty
        || !Reference.RuntimeNodeProperty->Struct
        || !Reference.Property
        || !Reference.RuntimeNodeMemory
        || !Node)
    {
        return false;
    }
    FIKRigSettingsSnapshot Snapshot;
    Snapshot.Type = Reference.RuntimeNodeProperty->Struct;
    Snapshot.Data = MakeShared<FStructOnScope>(Snapshot.Type);
    Snapshot.Type->CopyScriptStruct(
        Snapshot.Data->GetStructMemory(),
        Reference.RuntimeNodeMemory);
    FStateGraphNodePropertyRef SnapshotReference;
    if (!ResolveStateGraphNodeProperty(
            Node,
            Reference.Path,
            Snapshot.Data->GetStructMemory(),
            SnapshotReference)
        || !ImportStateGraphNodePropertyValue(
            SnapshotReference,
            Input,
            OutCanonicalValue))
    {
        return false;
    }
    return ValidateStateGraphBoneReferenceArray(
            Node,
            SnapshotReference)
        && ValidateStateGraphRetargetOverrideSetArray(
            Node,
            SnapshotReference);
}

bool SetStateGraphFloatArrayValues(
    const FStateGraphNodeArrayRef& Reference,
    const TArray<float>& Values)
{
    const FArrayProperty* ArrayProperty = Reference.ArrayProperty;
    const FFloatProperty* FloatProperty = ArrayProperty
        ? CastField<const FFloatProperty>(ArrayProperty->Inner)
        : nullptr;
    if (!ArrayProperty || !FloatProperty || !Reference.ValueMemory
        || Values.Num() > MaxStateGraphArrayItems)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    ArrayHelper.Resize(Values.Num());
    for (int32 Index = 0; Index < Values.Num(); ++Index)
    {
        FloatProperty->SetPropertyValue(
            ArrayHelper.GetRawPtr(Index), Values[Index]);
    }
    return ArrayHelper.Num() == Values.Num();
}

bool ValidateStateGraphConstraintBones(
    const UAnimGraphNode_Base* Node,
    const FStateGraphConstraintArrayRef& Reference)
{
    if (!Node || !Reference.ConstraintArray.ArrayProperty
        || !Reference.ConstraintArray.ValueMemory
        || !Reference.TargetBoneProperty
        || !Reference.BoneNameProperty)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        Reference.ConstraintArray.ArrayProperty,
        const_cast<void*>(Reference.ConstraintArray.ValueMemory));
    if (ArrayHelper.Num() == 0)
    {
        return true;
    }
    const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(
        FBlueprintEditorUtils::FindBlueprintForNode(Node));
    const USkeleton* Skeleton = AnimBlueprint
        ? AnimBlueprint->TargetSkeleton : nullptr;
    if (!Skeleton)
    {
        return false;
    }
    const FReferenceSkeleton& ReferenceSkeleton =
        Skeleton->GetReferenceSkeleton();
    for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
    {
        const void* ElementMemory = ArrayHelper.GetRawPtr(Index);
        const void* TargetBoneMemory = Reference.TargetBoneProperty
            ->ContainerPtrToValuePtr<void>(ElementMemory);
        const void* BoneNameMemory = Reference.BoneNameProperty
            ->ContainerPtrToValuePtr<void>(TargetBoneMemory);
        const FName BoneName = Reference.BoneNameProperty
            ->GetPropertyValue(BoneNameMemory);
        if (BoneName.IsNone()
            || ReferenceSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
        {
            return false;
        }
    }
    return true;
}

bool BuildStateGraphConstraintArraySnapshot(
    const UAnimGraphNode_Base* Node,
    const FStateGraphConstraintArrayRef& Reference,
    const int32 TargetSize,
    const FString& ConstraintValues,
    const TArray<float>& Weights,
    TSharedPtr<FStructOnScope>& OutSnapshot,
    FString& OutCanonicalConstraintValues)
{
    OutSnapshot.Reset();
    OutCanonicalConstraintValues.Reset();
    if (!Node || !Reference.ConstraintArray.RuntimeNodeProperty
        || !Reference.ConstraintArray.RuntimeNodeProperty->Struct
        || !Reference.ConstraintArray.RuntimeNodeMemory
        || TargetSize < 0 || TargetSize > MaxStateGraphArrayItems
        || Weights.Num() != TargetSize
        || ConstraintValues.Len() > 8192
        || ConstraintValues.Contains(TEXT("\r"))
        || ConstraintValues.Contains(TEXT("\n"))
        || ConstraintValues.Contains(TEXT("nan"), ESearchCase::IgnoreCase)
        || ConstraintValues.Contains(TEXT("inf"), ESearchCase::IgnoreCase))
    {
        return false;
    }
    for (const float Weight : Weights)
    {
        if (!FMath::IsFinite(Weight) || Weight < 0.0f || Weight > 1.0f)
        {
            return false;
        }
    }
    OutSnapshot = MakeShared<FStructOnScope>(
        Reference.ConstraintArray.RuntimeNodeProperty->Struct);
    Reference.ConstraintArray.RuntimeNodeProperty->Struct->CopyScriptStruct(
        OutSnapshot->GetStructMemory(),
        Reference.ConstraintArray.RuntimeNodeMemory);
    FStateGraphConstraintArrayRef SnapshotReference;
    if (!ResolveStateGraphConstraintArray(
            Node,
            Reference.ConstraintArray.Path,
            OutSnapshot->GetStructMemory(),
            SnapshotReference))
    {
        OutSnapshot.Reset();
        return false;
    }
    FStateGraphNodePropertyRef ConstraintPropertyReference =
        StateGraphArrayPropertyReference(
            SnapshotReference.ConstraintArray);
    const TCHAR* ImportEnd = ConstraintPropertyReference.Property
        ->ImportText_Direct(
            *ConstraintValues,
            const_cast<void*>(ConstraintPropertyReference.ValueMemory),
            nullptr,
            PPF_None);
    if (!ImportEnd
        || !FString(ImportEnd).TrimStartAndEnd().IsEmpty()
        || StateGraphNodeArraySize(SnapshotReference.ConstraintArray)
            != TargetSize
        || !SetStateGraphFloatArrayValues(
            SnapshotReference.WeightArray, Weights)
        || StateGraphNodeArraySize(SnapshotReference.WeightArray)
            != TargetSize
        || !ValidateStateGraphPropertyData(
            SnapshotReference.ConstraintArray.ArrayProperty,
            SnapshotReference.ConstraintArray.ValueMemory)
        || !ValidateStateGraphPropertyData(
            SnapshotReference.WeightArray.ArrayProperty,
            SnapshotReference.WeightArray.ValueMemory)
        || !ValidateStateGraphConstraintBones(Node, SnapshotReference))
    {
        OutSnapshot.Reset();
        return false;
    }
    OutCanonicalConstraintValues = ExportStateGraphNodePropertyValue(
        ConstraintPropertyReference);
    if (OutCanonicalConstraintValues.Len() > 8192)
    {
        OutSnapshot.Reset();
        OutCanonicalConstraintValues.Reset();
        return false;
    }
    return true;
}

bool StateGraphConstraintArrayMatches(
    const FStateGraphConstraintArrayRef& Reference,
    const int32 TargetSize,
    const FString& CanonicalConstraintValues,
    const TArray<float>& Weights)
{
    if (StateGraphNodeArraySize(Reference.ConstraintArray) != TargetSize
        || StateGraphNodeArraySize(Reference.WeightArray) != TargetSize
        || ExportStateGraphNodePropertyValue(
            StateGraphArrayPropertyReference(
                Reference.ConstraintArray))
            != CanonicalConstraintValues)
    {
        return false;
    }
    TArray<float> CurrentWeights;
    if (!GetStateGraphFloatArrayValues(
            Reference.WeightArray, CurrentWeights)
        || CurrentWeights.Num() != Weights.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Weights.Num(); ++Index)
    {
        if (!FMath::IsNearlyEqual(CurrentWeights[Index], Weights[Index]))
        {
            return false;
        }
    }
    return true;
}

bool ResizeStateGraphNodeArrayValue(
    const FStateGraphNodeArrayRef& Reference,
    const int32 TargetSize)
{
    if (!Reference.ArrayProperty || !Reference.ValueMemory
        || !Reference.bResizable || TargetSize < 0
        || TargetSize > MaxStateGraphArrayItems)
    {
        return false;
    }
    FScriptArrayHelper ArrayHelper(
        Reference.ArrayProperty,
        const_cast<void*>(Reference.ValueMemory));
    ArrayHelper.Resize(TargetSize);
    return ArrayHelper.Num() == TargetSize
        && ValidateStateGraphPropertyData(
            Reference.ArrayProperty,
            Reference.ValueMemory);
}

bool StateGraphPoseArrayMatches(
    const FStateGraphPoseArrayRef& Reference,
    const int32 TargetSize,
    const TArray<float>& Values,
    const TArray<FString>& EnumEntries)
{
    if (StateGraphNodeArraySize(Reference.PoseArray) != TargetSize
        || StateGraphNodeArraySize(Reference.CompanionArray) != TargetSize)
    {
        return false;
    }
    TArray<float> CurrentValues;
    if (!GetStateGraphPoseArrayCompanionValues(
            Reference, CurrentValues)
        || CurrentValues.Num() != Values.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Values.Num(); ++Index)
    {
        if (!FMath::IsNearlyEqual(CurrentValues[Index], Values[Index]))
        {
            return false;
        }
    }
    TArray<FString> CurrentEnumEntries;
    if (!GetStateGraphPoseArrayEnumEntries(
            Reference, CurrentEnumEntries)
        || CurrentEnumEntries.Num() != EnumEntries.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < EnumEntries.Num(); ++Index)
    {
        const int32 CurrentEnumIndex = FindStateGraphEnumEntryIndex(
            Reference.BoundEnum, CurrentEnumEntries[Index]);
        const int32 RequestedEnumIndex = FindStateGraphEnumEntryIndex(
            Reference.BoundEnum, EnumEntries[Index]);
        if (CurrentEnumIndex == INDEX_NONE
            || RequestedEnumIndex == INDEX_NONE
            || CurrentEnumIndex != RequestedEnumIndex)
        {
            return false;
        }
    }
    return true;
}

bool SetStateGraphPoseArrayValues(
    const FStateGraphPoseArrayRef& Reference,
    const int32 TargetSize,
    const TArray<float>& Values,
    const TArray<FString>& EnumEntries)
{
    if (!ValidateStateGraphPoseArrayValues(
            Reference, TargetSize, Values, EnumEntries)
        || !Reference.PoseArray.ArrayProperty
        || !Reference.PoseArray.ValueMemory
        || !Reference.CompanionArray.ArrayProperty
        || !Reference.CompanionArray.ValueMemory)
    {
        return false;
    }
    FScriptArrayHelper PoseHelper(
        Reference.PoseArray.ArrayProperty,
        const_cast<void*>(Reference.PoseArray.ValueMemory));
    FScriptArrayHelper CompanionHelper(
        Reference.CompanionArray.ArrayProperty,
        const_cast<void*>(Reference.CompanionArray.ValueMemory));
    const FFloatProperty* FloatProperty = CastField<const FFloatProperty>(
        Reference.CompanionArray.ArrayProperty->Inner);
    if (!FloatProperty)
    {
        return false;
    }
    PoseHelper.Resize(TargetSize);
    CompanionHelper.Resize(TargetSize);
    for (int32 Index = 0; Index < TargetSize; ++Index)
    {
        FloatProperty->SetPropertyValue(
            CompanionHelper.GetRawPtr(Index), Values[Index]);
    }
    if (Reference.Kind == TEXT("blend_list_by_enum"))
    {
        const FNameProperty* NameProperty =
            Reference.EnumEntriesProperty
                ? CastField<const FNameProperty>(
                    Reference.EnumEntriesProperty->Inner)
                : nullptr;
        if (!Reference.EnumEntriesProperty || !NameProperty
            || !Reference.EnumEntriesMemory)
        {
            return false;
        }
        FScriptArrayHelper EnumHelper(
            Reference.EnumEntriesProperty,
            Reference.EnumEntriesMemory);
        EnumHelper.Resize(EnumEntries.Num());
        for (int32 Index = 0; Index < EnumEntries.Num(); ++Index)
        {
            NameProperty->SetPropertyValue(
                EnumHelper.GetRawPtr(Index), FName(*EnumEntries[Index]));
        }
    }
    return PoseHelper.Num() == TargetSize
        && CompanionHelper.Num() == TargetSize
        && StateGraphPoseArrayMatches(
            Reference, TargetSize, Values, EnumEntries);
}

bool ValidateStateGraphNodeArrayResize(
    const UAnimGraphNode_Base* Node,
    const FStateGraphNodeArrayRef& Reference,
    const int32 TargetSize)
{
    if (!Reference.RuntimeNodeProperty
        || !Reference.RuntimeNodeProperty->Struct
        || !Reference.ArrayProperty
        || !Reference.RuntimeNodeMemory
        || !Reference.bResizable
        || !Node)
    {
        return false;
    }
    FIKRigSettingsSnapshot Snapshot;
    Snapshot.Type = Reference.RuntimeNodeProperty->Struct;
    Snapshot.Data = MakeShared<FStructOnScope>(Snapshot.Type);
    Snapshot.Type->CopyScriptStruct(
        Snapshot.Data->GetStructMemory(),
        Reference.RuntimeNodeMemory);
    FStateGraphNodeArrayRef SnapshotReference;
    return ResolveStateGraphNodeArray(
            Node,
            Reference.Path,
            Snapshot.Data->GetStructMemory(),
            SnapshotReference)
        && ResizeStateGraphNodeArrayValue(
            SnapshotReference, TargetSize);
}

FString StateGraphPinStateString(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return TEXT("Missing");
    }
    TArray<FString> LinkedPinIds;
    for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
    {
        if (LinkedPin)
        {
            LinkedPinIds.Add(LinkedPin->PinId.ToString(
                EGuidFormats::DigitsWithHyphensLower));
        }
    }
    LinkedPinIds.Sort();
    const UObject* TypeObject = Pin->PinType.PinSubCategoryObject.Get();
    return FString::Printf(
        TEXT("PinGuid=%s|Name=%s|Direction=%s|Category=%s|SubCategory=%s|TypeObject=%s|Container=%d|Reference=%s|Const=%s|Weak=%s|Default=%s|DefaultObject=%s|DefaultText=%s|AutogeneratedDefault=%s|Links=%s"),
        *Pin->PinId.ToString(EGuidFormats::DigitsWithHyphensLower),
        *EncodeStateGraphValue(Pin->PinName.ToString()),
        *StateGraphPinDirectionString(Pin->Direction),
        *EncodeStateGraphValue(Pin->PinType.PinCategory.ToString()),
        *EncodeStateGraphValue(Pin->PinType.PinSubCategory.ToString()),
        TypeObject ? *TypeObject->GetPathName() : TEXT("None"),
        static_cast<int32>(Pin->PinType.ContainerType),
        Pin->PinType.bIsReference ? TEXT("true") : TEXT("false"),
        Pin->PinType.bIsConst ? TEXT("true") : TEXT("false"),
        Pin->PinType.bIsWeakPointer ? TEXT("true") : TEXT("false"),
        *EncodeStateGraphValue(Pin->DefaultValue),
        Pin->DefaultObject ? *Pin->DefaultObject->GetPathName() : TEXT("None"),
        *EncodeStateGraphValue(Pin->DefaultTextValue.ToString()),
        *EncodeStateGraphValue(Pin->AutogeneratedDefaultValue),
        *FString::Join(LinkedPinIds, TEXT(",")));
}

FString StateGraphNodeStateString(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return TEXT("Missing");
    }
    TArray<FString> PinStates;
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin)
        {
            PinStates.Add(StateGraphPinStateString(Pin));
        }
    }
    PinStates.Sort();
    const uint32 PinHash = FCrc::StrCrc32(
        *FString::Join(PinStates, TEXT("\n")));
    TArray<FString> PropertyStates;
    if (const UAnimGraphNode_Base* AnimGraphNode =
            Cast<UAnimGraphNode_Base>(Node))
    {
        for (const FStateGraphNodePropertyRef& Reference :
             GetStateGraphNodeProperties(AnimGraphNode))
        {
            PropertyStates.Add(
                StateGraphNodePropertyStateString(Reference));
        }
    }
    PropertyStates.Sort();
    const uint32 PropertyHash = FCrc::StrCrc32(
        *FString::Join(PropertyStates, TEXT("\n")));
    TArray<FString> ArrayStates;
    if (const UAnimGraphNode_Base* AnimGraphNode =
            Cast<UAnimGraphNode_Base>(Node))
    {
        for (const FStateGraphNodeArrayRef& Reference :
             GetStateGraphNodeArrays(AnimGraphNode))
        {
            ArrayStates.Add(
                StateGraphNodeArrayStateString(Reference));
        }
    }
    ArrayStates.Sort();
    const uint32 ArrayHash = FCrc::StrCrc32(
        *FString::Join(ArrayStates, TEXT("\n")));
    return FString::Printf(
        TEXT("Guid=%s|Class=%s|Position=X=%d,Y=%d|PinCount=%d|PinHash=%08X|PropertyCount=%d|PropertyHash=%08X|ArrayCount=%d|ArrayHash=%08X"),
        *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower),
        *Node->GetClass()->GetPathName(),
        Node->NodePosX,
        Node->NodePosY,
        PinStates.Num(),
        PinHash,
        PropertyStates.Num(),
        PropertyHash,
        ArrayStates.Num(),
        ArrayHash);
}

FString StateGraphStateString(const UAnimationStateGraph* Graph)
{
    if (!Graph)
    {
        return TEXT("Missing");
    }
    TArray<FString> NodeStates;
    int32 PinCount = 0;
    int32 LinkCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        NodeStates.Add(StateGraphNodeStateString(Node));
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            ++PinCount;
            if (Pin->Direction == EGPD_Output)
            {
                LinkCount += Pin->LinkedTo.Num();
            }
        }
    }
    NodeStates.Sort();
    const uint32 ContentHash = FCrc::StrCrc32(
        *FString::Join(NodeStates, TEXT("\n")));
    return FString::Printf(
        TEXT("NodeCount=%d|PinCount=%d|LinkCount=%d|ContentHash=%08X"),
        NodeStates.Num(),
        PinCount,
        LinkCount,
        ContentHash);
}

UEdGraphNode* FindStateGraphNodeByGuid(
    UAnimationStateGraph* Graph,
    const FString& GuidString)
{
    FGuid Guid;
    if (!Graph || !FGuid::Parse(GuidString, Guid))
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid == Guid)
        {
            return Node;
        }
    }
    return nullptr;
}

UEdGraphPin* FindStateGraphPinByGuid(
    UAnimationStateGraph* Graph,
    const FString& GuidString)
{
    FGuid Guid;
    if (!Graph || !FGuid::Parse(GuidString, Guid))
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->PinId == Guid)
            {
                return Pin;
            }
        }
    }
    return nullptr;
}

struct FStateGraphPreflightError
{
    FString Code;
    FString Message;
};

bool IsStateGraphR2PropertyOperation(
    const FThomasAnimationOperation& Operation,
    UAnimBlueprint* Blueprint)
{
    if (Operation.Action != TEXT("set_state_graph_node_property"))
    {
        return false;
    }
    UAnimationStateGraph* Graph = ResolveStateGraph(
        Blueprint, Operation.MachineName, Operation.StateName);
    UAnimGraphNode_Base* Node = Graph
        ? Cast<UAnimGraphNode_Base>(FindStateGraphNodeByGuid(
            Graph, Operation.StateGraphNodeGuid))
        : nullptr;
    FStateGraphNodePropertyRef Reference;
    const FObjectPropertyBase* ObjectProperty = nullptr;
    if (Node
        && ResolveStateGraphNodeProperty(
            Node, Operation.PropertyName, Reference))
    {
        ObjectProperty = CastField<const FObjectPropertyBase>(
            Reference.Property);
    }
    return Node
        && Reference.Property
        && (Reference.Property->IsA<FArrayProperty>()
            || Reference.Property->IsA<FSetProperty>()
            || Reference.Property->IsA<FMapProperty>()
            || IsSupportedStateGraphClassProperty(
                Reference.Property)
            || (ObjectProperty && ObjectProperty->PropertyClass
                && (ObjectProperty->PropertyClass->IsChildOf(
                            UIKRigDefinition::StaticClass())
                    || ObjectProperty->PropertyClass->IsChildOf(
                            UIKRetargeter::StaticClass()))));
}

TOptional<FStateGraphPreflightError> ValidateGenericStateGraphOperation(
    const FThomasAnimationOperation& Operation,
    UAnimBlueprint* Blueprint)
{
    UAnimationStateGraph* Graph = ResolveStateGraph(
        Blueprint, Operation.MachineName, Operation.StateName);
    if (!Graph)
    {
        return FStateGraphPreflightError{
            TEXT("state_graph_not_found"),
            Operation.MachineName + TEXT("/") + Operation.StateName};
    }
    const FString CurrentGraphState = StateGraphStateString(Graph);
    if (Operation.ExpectedValue.IsEmpty()
        || Operation.ExpectedValue != CurrentGraphState)
    {
        return FStateGraphPreflightError{
            TEXT("state_graph_conflict"), CurrentGraphState};
    }
    if (Operation.Action == TEXT("add_state_graph_node"))
    {
        UClass* NodeClass = FindObject<UClass>(nullptr, *Operation.ClassPath);
        if (!IsSupportedStateGraphNodeClass(NodeClass))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_class_unavailable"),
                Operation.ClassPath};
        }
        UEnum* BoundEnum = nullptr;
        if ((IsStateGraphBlendListByEnumClass(NodeClass)
                && !ResolveStateGraphEnum(
                    Operation.StateGraphEnumPath, BoundEnum))
            || (!IsStateGraphBlendListByEnumClass(NodeClass)
                && !Operation.StateGraphEnumPath.IsEmpty()))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_enum"),
                Operation.StateGraphEnumPath};
        }
        if (Graph->Nodes.Num() >= 128
            || FMath::Abs(Operation.PositionX) > 1000000
            || FMath::Abs(Operation.PositionY) > 1000000)
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_node"),
                Operation.ClassPath};
        }
        return {};
    }
    if (Operation.Action == TEXT("remove_state_graph_node"))
    {
        UEdGraphNode* Node = FindStateGraphNodeByGuid(
            Graph, Operation.StateGraphNodeGuid);
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_not_found"),
                Operation.StateGraphNodeGuid};
        }
        return {};
    }
    if (Operation.Action == TEXT("set_state_graph_node_property"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_not_found"),
                Operation.StateGraphNodeGuid};
        }
        FStateGraphNodePropertyRef Reference;
        if (!ResolveStateGraphNodeProperty(
                Node, Operation.PropertyName, Reference))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_property_not_found"),
                Operation.PropertyName};
        }
        FString CanonicalValue;
        if (!ValidateStateGraphNodePropertyValue(
                Node,
                Reference,
                Operation.SettingValue,
                CanonicalValue))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_node_property_value"),
                Operation.SettingValue};
        }
        if (CanonicalValue
            == ExportStateGraphNodePropertyValue(Reference))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_property_unchanged"),
                Operation.PropertyName};
        }
        return {};
    }
    if (Operation.Action == TEXT("resize_state_graph_node_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_not_found"),
                Operation.StateGraphNodeGuid};
        }
        FStateGraphNodeArrayRef Reference;
        if (!ResolveStateGraphNodeArray(
                Node, Operation.PropertyName, Reference))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_array_not_found"),
                Operation.PropertyName};
        }
        if (!Reference.bResizable)
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_array_not_resizable"),
                Operation.PropertyName};
        }
        if (Operation.StateGraphContainerSize < 0
            || Operation.StateGraphContainerSize
                > MaxStateGraphArrayItems)
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_node_array_size"),
                FString::FromInt(Operation.StateGraphContainerSize)};
        }
        if (StateGraphNodeArraySize(Reference)
            == Operation.StateGraphContainerSize)
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_array_unchanged"),
                Operation.PropertyName};
        }
        if (!ValidateStateGraphNodeArrayResize(
                Node,
                Reference,
                Operation.StateGraphContainerSize))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_node_array_resize"),
                Operation.PropertyName};
        }
        return {};
    }
    if (Operation.Action == TEXT("resize_state_graph_pose_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_not_found"),
                Operation.StateGraphNodeGuid};
        }
        FStateGraphPoseArrayRef Reference;
        if (!ResolveStateGraphPoseArray(
                Node, Operation.PropertyName, Reference))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_pose_array_not_found"),
                Operation.PropertyName};
        }
        if (Operation.StateGraphContainerSize < 2
            || Operation.StateGraphContainerSize
                > MaxStateGraphArrayItems)
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_pose_array_size"),
                FString::FromInt(Operation.StateGraphContainerSize)};
        }
        if (!ValidateStateGraphPoseArrayValues(
                Reference,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_pose_array_values"),
                Operation.PropertyName};
        }
        if (StateGraphPoseArrayMatches(
                Reference,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_pose_array_unchanged"),
                Operation.PropertyName};
        }
        return {};
    }
    if (Operation.Action == TEXT("replace_state_graph_constraint_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_node_not_found"),
                Operation.StateGraphNodeGuid};
        }
        FStateGraphConstraintArrayRef Reference;
        if (!ResolveStateGraphConstraintArray(
                Node, Operation.PropertyName, Reference))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_constraint_array_not_found"),
                Operation.PropertyName};
        }
        if (Operation.StateGraphContainerSize < 0
            || Operation.StateGraphContainerSize
                > MaxStateGraphArrayItems)
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_constraint_array_size"),
                FString::FromInt(Operation.StateGraphContainerSize)};
        }
        TSharedPtr<FStructOnScope> Snapshot;
        FString CanonicalConstraintValues;
        if (!BuildStateGraphConstraintArraySnapshot(
                Node,
                Reference,
                Operation.StateGraphContainerSize,
                Operation.SettingValue,
                Operation.StateGraphCompanionValues,
                Snapshot,
                CanonicalConstraintValues))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_constraint_array_values"),
                Operation.PropertyName};
        }
        if (StateGraphConstraintArrayMatches(
                Reference,
                Operation.StateGraphContainerSize,
                CanonicalConstraintValues,
                Operation.StateGraphCompanionValues))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_constraint_array_unchanged"),
                Operation.PropertyName};
        }
        return {};
    }
    if (Operation.Action == TEXT("set_state_graph_pin_default"))
    {
        UEdGraphPin* Pin = FindStateGraphPinByGuid(
            Graph, Operation.StateGraphPinGuid);
        UEdGraphNode* Owner = Pin ? Pin->GetOwningNode() : nullptr;
        if (!Pin || !Owner
            || !IsSupportedStateGraphNodeClass(Owner->GetClass()))
        {
            return FStateGraphPreflightError{
                TEXT("state_graph_pin_not_found"),
                Operation.StateGraphPinGuid};
        }
        if (Pin->Direction != EGPD_Input
            || !Pin->LinkedTo.IsEmpty()
            || Pin->PinType.PinCategory == TEXT("exec")
            || Operation.StateGraphPinDefaultValue.Len() > 4096
            || Operation.StateGraphPinDefaultValue.Contains(TEXT("\r"))
            || Operation.StateGraphPinDefaultValue.Contains(TEXT("\n")))
        {
            return FStateGraphPreflightError{
                TEXT("invalid_state_graph_pin_default"),
                Operation.StateGraphPinGuid};
        }
        return {};
    }
    UEdGraphPin* SourcePin = FindStateGraphPinByGuid(
        Graph, Operation.StateGraphSourcePinGuid);
    UEdGraphPin* TargetPin = FindStateGraphPinByGuid(
        Graph, Operation.StateGraphTargetPinGuid);
    if (!SourcePin || !TargetPin
        || SourcePin->Direction != EGPD_Output
        || TargetPin->Direction != EGPD_Input
        || SourcePin->GetOwningNode() == TargetPin->GetOwningNode())
    {
        return FStateGraphPreflightError{
            TEXT("invalid_state_graph_link"),
            Operation.StateGraphSourcePinGuid + TEXT("->")
                + Operation.StateGraphTargetPinGuid};
    }
    const bool bLinked = SourcePin->LinkedTo.Contains(TargetPin)
        && TargetPin->LinkedTo.Contains(SourcePin);
    if (Operation.Action == TEXT("remove_state_graph_link"))
    {
        return bLinked
            ? TOptional<FStateGraphPreflightError>()
            : TOptional<FStateGraphPreflightError>(
                FStateGraphPreflightError{
                    TEXT("state_graph_link_not_found"),
                    Operation.StateGraphSourcePinGuid + TEXT("->")
                        + Operation.StateGraphTargetPinGuid});
    }
    if (bLinked)
    {
        return FStateGraphPreflightError{
            TEXT("state_graph_link_exists"),
            Operation.StateGraphSourcePinGuid + TEXT("->")
                + Operation.StateGraphTargetPinGuid};
    }
    if (!SourcePin->LinkedTo.IsEmpty() || !TargetPin->LinkedTo.IsEmpty())
    {
        return FStateGraphPreflightError{
            TEXT("state_graph_link_would_replace_existing"),
            Operation.StateGraphSourcePinGuid + TEXT("->")
                + Operation.StateGraphTargetPinGuid};
    }
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        return FStateGraphPreflightError{
            TEXT("state_graph_schema_unavailable"), FString()};
    }
    const FPinConnectionResponse Connection =
        Schema->CanCreateConnection(SourcePin, TargetPin);
    if (Connection.Response != CONNECT_RESPONSE_MAKE)
    {
        return FStateGraphPreflightError{
            TEXT("incompatible_state_graph_link"),
            Connection.Message.ToString()};
    }
    return {};
}

bool ApplyGenericStateGraphOperation(
    const FThomasAnimationOperation& Operation,
    UAnimBlueprint* Blueprint)
{
    UAnimationStateGraph* Graph = ResolveStateGraph(
        Blueprint, Operation.MachineName, Operation.StateName);
    if (!Graph || StateGraphStateString(Graph) != Operation.ExpectedValue)
    {
        return false;
    }
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        return false;
    }
    if (Operation.Action == TEXT("add_state_graph_node"))
    {
        UClass* NodeClass = FindObject<UClass>(nullptr, *Operation.ClassPath);
        if (!IsSupportedStateGraphNodeClass(NodeClass))
        {
            return false;
        }
        UEnum* BoundEnum = nullptr;
        const FObjectPropertyBase* BoundEnumProperty = nullptr;
        if (IsStateGraphBlendListByEnumClass(NodeClass))
        {
            if (!ResolveStateGraphEnum(
                    Operation.StateGraphEnumPath, BoundEnum))
            {
                return false;
            }
            BoundEnumProperty = FindFProperty<FObjectPropertyBase>(
                NodeClass, TEXT("BoundEnum"));
            if (!BoundEnumProperty
                || !BoundEnumProperty->PropertyClass
                || !BoundEnum->IsA(BoundEnumProperty->PropertyClass))
            {
                return false;
            }
        }
        UAnimGraphNode_Base* AddedNode = NewObject<UAnimGraphNode_Base>(
            Graph, NodeClass, NAME_None, RF_Transactional);
        if (!AddedNode)
        {
            return false;
        }
        if (BoundEnumProperty)
        {
            BoundEnumProperty->SetObjectPropertyValue_InContainer(
                AddedNode, BoundEnum);
        }
        Graph->AddNode(AddedNode, false, false);
        AddedNode->NodePosX = Operation.PositionX;
        AddedNode->NodePosY = Operation.PositionY;
        if (!AddedNode->NodeGuid.IsValid())
        {
            AddedNode->CreateNewGuid();
        }
        AddedNode->PostPlacedNewNode();
        if (AddedNode->Pins.IsEmpty())
        {
            AddedNode->AllocateDefaultPins();
        }
        Graph->NotifyGraphChanged();
        return AddedNode->GetGraph() == Graph
            && AddedNode->GetClass() == NodeClass
            && AddedNode->NodeGuid.IsValid()
            && AddedNode->NodePosX == Operation.PositionX
            && AddedNode->NodePosY == Operation.PositionY
            && (!BoundEnumProperty
                || BoundEnumProperty->GetObjectPropertyValue_InContainer(
                    AddedNode) == BoundEnum);
    }
    if (Operation.Action == TEXT("remove_state_graph_node"))
    {
        UEdGraphNode* Node = FindStateGraphNodeByGuid(
            Graph, Operation.StateGraphNodeGuid);
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass())
            || !Schema->SafeDeleteNodeFromGraph(Graph, Node))
        {
            return false;
        }
        Graph->NotifyGraphChanged();
        return !FindStateGraphNodeByGuid(
            Graph, Operation.StateGraphNodeGuid);
    }
    if (Operation.Action == TEXT("set_state_graph_node_property"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        FStateGraphNodePropertyRef Reference;
        FString CanonicalValue;
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass())
            || !ResolveStateGraphNodeProperty(
                Node, Operation.PropertyName, Reference)
            || !ValidateStateGraphNodePropertyValue(
                Node,
                Reference,
                Operation.SettingValue,
                CanonicalValue)
            || CanonicalValue
                == ExportStateGraphNodePropertyValue(Reference))
        {
            return false;
        }
        Node->Modify();
        Node->PreEditChange(Reference.Property);
        FString AppliedCanonicalValue;
        if (!ImportStateGraphNodePropertyValue(
                Reference,
                Operation.SettingValue,
                AppliedCanonicalValue)
            || AppliedCanonicalValue != CanonicalValue)
        {
            return false;
        }
        FPropertyChangedEvent ChangeEvent(
            Reference.Property,
            EPropertyChangeType::ValueSet);
        ChangeEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.RuntimeNodeProperty));
        Node->PostEditChangeProperty(ChangeEvent);
        Graph->NotifyNodeChanged(Node);
        FStateGraphNodePropertyRef Readback;
        return ResolveStateGraphNodeProperty(
                Node, Operation.PropertyName, Readback)
            && ExportStateGraphNodePropertyValue(Readback)
                == CanonicalValue;
    }
    if (Operation.Action == TEXT("resize_state_graph_node_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        FStateGraphNodeArrayRef Reference;
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass())
            || !ResolveStateGraphNodeArray(
                Node, Operation.PropertyName, Reference)
            || !Reference.bResizable
            || StateGraphNodeArraySize(Reference)
                == Operation.StateGraphContainerSize
            || !ValidateStateGraphNodeArrayResize(
                Node,
                Reference,
                Operation.StateGraphContainerSize))
        {
            return false;
        }
        const int32 PreviousSize = StateGraphNodeArraySize(Reference);
        Node->Modify();
        Node->PreEditChange(Reference.RootProperty);
        if (!ResizeStateGraphNodeArrayValue(
                Reference, Operation.StateGraphContainerSize))
        {
            return false;
        }
        FPropertyChangedEvent ChangeEvent(
            Reference.RootProperty,
            Operation.StateGraphContainerSize > PreviousSize
                ? EPropertyChangeType::ArrayAdd
                : EPropertyChangeType::ArrayRemove);
        ChangeEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.RuntimeNodeProperty));
        Node->PostEditChangeProperty(ChangeEvent);
        Graph->NotifyNodeChanged(Node);
        FStateGraphNodeArrayRef Readback;
        return ResolveStateGraphNodeArray(
                Node, Operation.PropertyName, Readback)
            && Readback.bResizable
            && StateGraphNodeArraySize(Readback)
                == Operation.StateGraphContainerSize;
    }
    if (Operation.Action == TEXT("resize_state_graph_pose_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        FStateGraphPoseArrayRef Reference;
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass())
            || !ResolveStateGraphPoseArray(
                Node, Operation.PropertyName, Reference)
            || !ValidateStateGraphPoseArrayValues(
                Reference,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries)
            || StateGraphPoseArrayMatches(
                Reference,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries))
        {
            return false;
        }
        const int32 PreviousSize =
            StateGraphNodeArraySize(Reference.PoseArray);
        Node->Modify();
        Node->PreEditChange(Reference.PoseArray.RootProperty);
        Node->PreEditChange(Reference.CompanionArray.RootProperty);
        if (Reference.EnumEntriesProperty)
        {
            Node->PreEditChange(Reference.EnumEntriesProperty);
        }
        if (Operation.StateGraphContainerSize < PreviousSize)
        {
            BreakStateGraphIndexedPinLinksFrom(
                Node,
                {Reference.PosePinPrefix,
                    Reference.CompanionPinPrefix},
                Operation.StateGraphContainerSize);
        }
        if (!SetStateGraphPoseArrayValues(
                Reference,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries))
        {
            return false;
        }
        if (Reference.EnumEntriesProperty)
        {
            FPropertyChangedEvent EnumEntriesEvent(
                Reference.EnumEntriesProperty,
                EPropertyChangeType::ValueSet);
            Node->PostEditChangeProperty(EnumEntriesEvent);
            FStateGraphPoseArrayRef PostEnumReference;
            if (!ResolveStateGraphPoseArray(
                    Node,
                    Operation.PropertyName,
                    PostEnumReference)
                || !SetStateGraphPoseArrayValues(
                    PostEnumReference,
                    Operation.StateGraphContainerSize,
                    Operation.StateGraphCompanionValues,
                    Operation.StateGraphEnumEntries))
            {
                return false;
            }
            Reference = PostEnumReference;
        }
        FPropertyChangedEvent CompanionEvent(
            Reference.CompanionArray.RootProperty,
            EPropertyChangeType::ValueSet);
        CompanionEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.CompanionArray.RuntimeNodeProperty));
        Node->PostEditChangeProperty(CompanionEvent);
        FPropertyChangedEvent PoseEvent(
            Reference.PoseArray.RootProperty,
            Operation.StateGraphContainerSize == PreviousSize
                ? EPropertyChangeType::ValueSet
                : Operation.StateGraphContainerSize > PreviousSize
                    ? EPropertyChangeType::ArrayAdd
                    : EPropertyChangeType::ArrayRemove);
        PoseEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.PoseArray.RuntimeNodeProperty));
        Node->PostEditChangeProperty(PoseEvent);
        Node->ReconstructNode();
        RemoveUnlinkedStateGraphOrphanPins(Node);
        Graph->NotifyNodeChanged(Node);
        FStateGraphPoseArrayRef Readback;
        return ResolveStateGraphPoseArray(
                Node, Operation.PropertyName, Readback)
            && StateGraphPoseArrayMatches(
                Readback,
                Operation.StateGraphContainerSize,
                Operation.StateGraphCompanionValues,
                Operation.StateGraphEnumEntries)
            && CountStateGraphIndexedPins(
                Node, Readback.PosePinPrefix)
                == Operation.StateGraphContainerSize;
    }
    if (Operation.Action == TEXT("replace_state_graph_constraint_array"))
    {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(
            FindStateGraphNodeByGuid(
                Graph, Operation.StateGraphNodeGuid));
        FStateGraphConstraintArrayRef Reference;
        TSharedPtr<FStructOnScope> Snapshot;
        FString CanonicalConstraintValues;
        if (!Node || !IsSupportedStateGraphNodeClass(Node->GetClass())
            || !ResolveStateGraphConstraintArray(
                Node, Operation.PropertyName, Reference)
            || !BuildStateGraphConstraintArraySnapshot(
                Node,
                Reference,
                Operation.StateGraphContainerSize,
                Operation.SettingValue,
                Operation.StateGraphCompanionValues,
                Snapshot,
                CanonicalConstraintValues)
            || StateGraphConstraintArrayMatches(
                Reference,
                Operation.StateGraphContainerSize,
                CanonicalConstraintValues,
                Operation.StateGraphCompanionValues))
        {
            return false;
        }
        FStateGraphConstraintArrayRef SnapshotReference;
        if (!ResolveStateGraphConstraintArray(
                Node,
                Operation.PropertyName,
                Snapshot->GetStructMemory(),
                SnapshotReference))
        {
            return false;
        }
        const int32 PreviousSize =
            StateGraphNodeArraySize(Reference.ConstraintArray);
        Node->Modify();
        Node->PreEditChange(Reference.ConstraintArray.RootProperty);
        Node->PreEditChange(Reference.WeightArray.RootProperty);
        if (Operation.StateGraphContainerSize < PreviousSize)
        {
            BreakStateGraphIndexedPinLinksFrom(
                Node,
                {TEXT("ConstraintSetup_"), TEXT("ConstraintWeights_")},
                Operation.StateGraphContainerSize);
        }
        Reference.ConstraintArray.ArrayProperty->CopyCompleteValue(
            const_cast<void*>(Reference.ConstraintArray.ValueMemory),
            SnapshotReference.ConstraintArray.ValueMemory);
        Reference.WeightArray.ArrayProperty->CopyCompleteValue(
            const_cast<void*>(Reference.WeightArray.ValueMemory),
            SnapshotReference.WeightArray.ValueMemory);
        FPropertyChangedEvent WeightEvent(
            Reference.WeightArray.RootProperty,
            EPropertyChangeType::ValueSet);
        WeightEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.WeightArray.RuntimeNodeProperty));
        Node->PostEditChangeProperty(WeightEvent);
        FPropertyChangedEvent ConstraintEvent(
            Reference.ConstraintArray.RootProperty,
            EPropertyChangeType::ValueSet);
        ConstraintEvent.SetActiveMemberProperty(
            const_cast<FStructProperty*>(
                Reference.ConstraintArray.RuntimeNodeProperty));
        Node->PostEditChangeProperty(ConstraintEvent);
        Node->ReconstructNode();
        RemoveUnlinkedStateGraphOrphanPins(Node);
        Graph->NotifyNodeChanged(Node);
        FStateGraphConstraintArrayRef Readback;
        return ResolveStateGraphConstraintArray(
                Node, Operation.PropertyName, Readback)
            && StateGraphConstraintArrayMatches(
                Readback,
                Operation.StateGraphContainerSize,
                CanonicalConstraintValues,
                Operation.StateGraphCompanionValues)
            && ValidateStateGraphConstraintBones(Node, Readback);
    }
    if (Operation.Action == TEXT("set_state_graph_pin_default"))
    {
        UEdGraphPin* Pin = FindStateGraphPinByGuid(
            Graph, Operation.StateGraphPinGuid);
        if (!Pin || Pin->Direction != EGPD_Input || !Pin->LinkedTo.IsEmpty())
        {
            return false;
        }
        Schema->TrySetDefaultValue(
            *Pin, Operation.StateGraphPinDefaultValue, true);
        Graph->NotifyGraphChanged();
        return Pin->DefaultValue == Operation.StateGraphPinDefaultValue
            && Schema->IsCurrentPinDefaultValid(Pin).IsEmpty();
    }
    UEdGraphPin* SourcePin = FindStateGraphPinByGuid(
        Graph, Operation.StateGraphSourcePinGuid);
    UEdGraphPin* TargetPin = FindStateGraphPinByGuid(
        Graph, Operation.StateGraphTargetPinGuid);
    if (!SourcePin || !TargetPin)
    {
        return false;
    }
    if (Operation.Action == TEXT("add_state_graph_link"))
    {
        if (!Schema->TryCreateConnection(SourcePin, TargetPin))
        {
            return false;
        }
        Graph->NotifyGraphChanged();
        return SourcePin->LinkedTo.Contains(TargetPin)
            && TargetPin->LinkedTo.Contains(SourcePin);
    }
    if (Operation.Action == TEXT("remove_state_graph_link"))
    {
        if (!SourcePin->LinkedTo.Contains(TargetPin)
            || !TargetPin->LinkedTo.Contains(SourcePin))
        {
            return false;
        }
        Schema->BreakSinglePinLink(SourcePin, TargetPin);
        Graph->NotifyGraphChanged();
        return !SourcePin->LinkedTo.Contains(TargetPin)
            && !TargetPin->LinkedTo.Contains(SourcePin);
    }
    return false;
}

bool HasUnsupportedStatePoseContent(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    if (!Graph || !Sink)
    {
        return true;
    }
    UAnimGraphNode_BlendListByBool* Blend = nullptr;
    UAnimGraphNode_SequencePlayer* FalsePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* TruePlayer = nullptr;
    UK2Node_VariableGet* Getter = nullptr;
    if (GetStateBoolBlendParts(
            State, Blend, FalsePlayer, TruePlayer, Getter))
    {
        return false;
    }
    UAnimGraphNode_BlendListByInt* IntBlend = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> IntBlendPlayers;
    UK2Node_VariableGet* IndexGetter = nullptr;
    if (GetStateIntBlendParts(
            State, IntBlend, IntBlendPlayers, IndexGetter))
    {
        return false;
    }
    UAnimGraphNode_ApplyAdditive* ApplyAdditive = nullptr;
    UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* AdditivePlayer = nullptr;
    UK2Node_VariableGet* AlphaGetter = nullptr;
    if (GetStateApplyAdditiveParts(
            State,
            ApplyAdditive,
            BasePlayer,
            AdditivePlayer,
            AlphaGetter))
    {
        return false;
    }
    UAnimGraphNode_LayeredBoneBlend* LayeredBlend = nullptr;
    UAnimGraphNode_SequencePlayer* LayeredBasePlayer = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> LayeredPlayers;
    TArray<UK2Node_VariableGet*> LayeredAlphaGetters;
    if (GetStateLayeredBlendPerBoneParts(
            State,
            LayeredBlend,
            LayeredBasePlayer,
            LayeredPlayers,
            LayeredAlphaGetters))
    {
        return false;
    }
    UAnimGraphNode_BlendSpacePlayer* BlendSpacePlayer = nullptr;
    UK2Node_VariableGet* XGetter = nullptr;
    UK2Node_VariableGet* YGetter = nullptr;
    bool bOneDimensional = false;
    if (GetStateBlendSpaceParts(
            State, BlendSpacePlayer, XGetter, YGetter, bOneDimensional))
    {
        return false;
    }
    int32 PlayerCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_StateResult>())
        {
            continue;
        }
        if (Node->IsA<UAnimGraphNode_SequencePlayer>())
        {
            ++PlayerCount;
            continue;
        }
        return true;
    }
    if (PlayerCount > 1)
    {
        return true;
    }
    if (Sink->LinkedTo.IsEmpty())
    {
        return false;
    }
    return !Sink->LinkedTo[0]
        || !Sink->LinkedTo[0]->GetOwningNode()->IsA<UAnimGraphNode_SequencePlayer>();
}

bool SetStateSequencePlayer(
    UAnimStateNode* State,
    UAnimSequenceBase* Sequence,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    if (!Graph || !Sink || !Sequence || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }
    Graph->Modify();
    State->Modify();
    UAnimGraphNode_SequencePlayer* Player = FindStateSequencePlayer(State);
    if (!Player)
    {
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Graph);
        Player = Creator.CreateNode();
        Player->SetAnimationAsset(Sequence);
        Player->Node.SetPlayRate(Operation.PlayRate);
        Player->Node.SetLoopAnimation(Operation.bLoopAnimation);
        Player->NodePosX = Operation.PositionX;
        Player->NodePosY = Operation.PositionY;
        Creator.Finalize();
    }
    else
    {
        Player->Modify();
        Player->SetAnimationAsset(Sequence);
        Player->Node.SetPlayRate(Operation.PlayRate);
        Player->Node.SetLoopAnimation(Operation.bLoopAnimation);
        Player->NodePosX = Operation.PositionX;
        Player->NodePosY = Operation.PositionY;
        Player->ReconstructNode();
    }
    UEdGraphPin* Pose = Player->FindPin(TEXT("Pose"), EGPD_Output);
    if (!Pose)
    {
        return false;
    }
    if (!Sink->LinkedTo.Contains(Pose))
    {
        if (!Sink->LinkedTo.IsEmpty())
        {
            return false;
        }
        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (!Schema || !Schema->TryCreateConnection(Pose, Sink))
        {
            return false;
        }
    }
    State->StateType = AST_SingleAnimation;
    Graph->NotifyGraphChanged();
    return Player->Node.GetSequence() == Sequence
        && FMath::IsNearlyEqual(Player->Node.GetPlayRate(), Operation.PlayRate)
        && Player->Node.IsLooping() == Operation.bLoopAnimation;
}

bool RemoveStateSequencePlayer(UAnimStateNode* State)
{
    if (FindStateBlendByBool(State))
    {
        return false;
    }
    UAnimGraphNode_SequencePlayer* Player = FindStateSequencePlayer(State);
    if (!Player)
    {
        return false;
    }
    if (UEdGraph* Graph = Player->GetGraph())
    {
        Graph->Modify();
    }
    Player->Modify();
    Player->DestroyNode();
    return FindStateSequencePlayer(State) == nullptr;
}

bool SetStateBlendByBool(
    UAnimStateNode* State,
    UAnimBlueprint* Blueprint,
    UAnimSequenceBase* FalseSequence,
    UAnimSequenceBase* TrueSequence,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FBoolProperty* Property = SkeletonClass
        ? FindFProperty<FBoolProperty>(SkeletonClass, FName(*Operation.Name))
        : nullptr;
    if (!Graph || !Sink || !FalseSequence || !TrueSequence || !Property
        || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }

    Graph->Modify();
    State->Modify();
    UAnimGraphNode_BlendListByBool* Blend = nullptr;
    UAnimGraphNode_SequencePlayer* FalsePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* TruePlayer = nullptr;
    UK2Node_VariableGet* Getter = nullptr;
    const bool bExistingBlend = GetStateBoolBlendParts(
        State, Blend, FalsePlayer, TruePlayer, Getter);
    if (!bExistingBlend)
    {
        FalsePlayer = FindStateSequencePlayer(State);
        if (!FalsePlayer)
        {
            FGraphNodeCreator<UAnimGraphNode_SequencePlayer> FalseCreator(*Graph);
            FalsePlayer = FalseCreator.CreateNode();
            FalseCreator.Finalize();
        }
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> TrueCreator(*Graph);
        TruePlayer = TrueCreator.CreateNode();
        TrueCreator.Finalize();
        FGraphNodeCreator<UAnimGraphNode_BlendListByBool> BlendCreator(*Graph);
        Blend = BlendCreator.CreateNode();
        BlendCreator.Finalize();
        FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*Graph);
        Getter = GetterCreator.CreateNode();
        Getter->VariableReference.SetSelfMember(Property->GetFName());
        GetterCreator.Finalize();
    }
    if (!Blend || !FalsePlayer || !TruePlayer || !Getter)
    {
        return false;
    }

    FalsePlayer->Modify();
    FalsePlayer->SetAnimationAsset(FalseSequence);
    FalsePlayer->Node.SetPlayRate(Operation.PlayRate);
    FalsePlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
    FalsePlayer->NodePosX = Operation.PositionX - 450;
    FalsePlayer->NodePosY = Operation.PositionY - 140;
    FalsePlayer->ReconstructNode();
    TruePlayer->Modify();
    TruePlayer->SetAnimationAsset(TrueSequence);
    TruePlayer->Node.SetPlayRate(Operation.SecondaryPlayRate);
    TruePlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
    TruePlayer->NodePosX = Operation.PositionX - 450;
    TruePlayer->NodePosY = Operation.PositionY + 140;
    TruePlayer->ReconstructNode();
    Blend->Modify();
    if (!SetBoolBlendTimes(Blend, Operation.Duration))
    {
        return false;
    }
    Blend->NodePosX = Operation.PositionX;
    Blend->NodePosY = Operation.PositionY;
    Blend->ReconstructNode();
    Getter->Modify();
    Getter->VariableReference.SetSelfMember(Property->GetFName());
    Getter->NodePosX = Operation.PositionX - 450;
    Getter->NodePosY = Operation.PositionY + 380;
    Getter->ReconstructNode();

    UEdGraphPin* FalsePose = FindNamedPin(
        FalsePlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* TruePose = FindNamedPin(
        TruePlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* FalseInput = FindNamedPin(
        Blend, EGPD_Input, TEXT("BlendPose_0"));
    UEdGraphPin* TrueInput = FindNamedPin(
        Blend, EGPD_Input, TEXT("BlendPose_1"));
    UEdGraphPin* ActiveInput = FindNamedPin(
        Blend, EGPD_Input, TEXT("bActiveValue"), TEXT("ActiveValue"));
    UEdGraphPin* BlendPose = FindNamedPin(
        Blend, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* GetterValue = Getter->GetValuePin();
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!FalsePose || !TruePose || !FalseInput || !TrueInput
        || !ActiveInput || !BlendPose || !GetterValue || !Schema)
    {
        return false;
    }
    FalseInput->BreakAllPinLinks(true);
    TrueInput->BreakAllPinLinks(true);
    ActiveInput->BreakAllPinLinks(true);
    Sink->BreakAllPinLinks(true);
    if (!Schema->TryCreateConnection(FalsePose, FalseInput)
        || !Schema->TryCreateConnection(TruePose, TrueInput)
        || !Schema->TryCreateConnection(GetterValue, ActiveInput)
        || !Schema->TryCreateConnection(BlendPose, Sink))
    {
        return false;
    }
    State->StateType = AST_BlendGraph;
    Graph->NotifyGraphChanged();
    UAnimGraphNode_BlendListByBool* VerifiedBlend = nullptr;
    UAnimGraphNode_SequencePlayer* VerifiedFalse = nullptr;
    UAnimGraphNode_SequencePlayer* VerifiedTrue = nullptr;
    UK2Node_VariableGet* VerifiedGetter = nullptr;
    return GetStateBoolBlendParts(
            State, VerifiedBlend, VerifiedFalse, VerifiedTrue, VerifiedGetter)
        && VerifiedGetter->GetVarName() == Property->GetFName()
        && VerifiedFalse->Node.GetSequence() == FalseSequence
        && VerifiedTrue->Node.GetSequence() == TrueSequence;
}

bool RemoveStateBlendByBool(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UAnimGraphNode_BlendListByBool* Blend = nullptr;
    UAnimGraphNode_SequencePlayer* FalsePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* TruePlayer = nullptr;
    UK2Node_VariableGet* Getter = nullptr;
    if (!Graph || !GetStateBoolBlendParts(
            State, Blend, FalsePlayer, TruePlayer, Getter))
    {
        return false;
    }
    Graph->Modify();
    Blend->Modify();
    FalsePlayer->Modify();
    TruePlayer->Modify();
    Getter->Modify();
    Blend->DestroyNode();
    FalsePlayer->DestroyNode();
    TruePlayer->DestroyNode();
    Getter->DestroyNode();
    Graph->NotifyGraphChanged();
    return FindStateBlendByBool(State) == nullptr
        && FindStateSequencePlayer(State) == nullptr;
}

bool SetStateBlendListByInt(
    UAnimStateNode* State,
    UAnimBlueprint* Blueprint,
    const TArray<UAnimSequenceBase*>& Sequences,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FIntProperty* IndexProperty = SkeletonClass
        ? FindFProperty<FIntProperty>(
            SkeletonClass, FName(*Operation.InputIndexVariableName))
        : nullptr;
    if (!Graph || !Sink || !IndexProperty || Sequences.Num() < 2
        || Sequences.Num() != Operation.PlayRates.Num()
        || Sequences.Num() != Operation.BlendTimes.Num()
        || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }
    for (UAnimSequenceBase* Sequence : Sequences)
    {
        if (!Sequence)
        {
            return false;
        }
    }

    Graph->Modify();
    State->Modify();
    UAnimGraphNode_BlendListByInt* Blend = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> Players;
    UK2Node_VariableGet* Getter = nullptr;
    const bool bExisting = GetStateIntBlendParts(
        State, Blend, Players, Getter);
    if (bExisting && Players.Num() != Sequences.Num())
    {
        return false;
    }
    if (!bExisting)
    {
        if (FindStateSequencePlayer(State) || FindStateBlendByBool(State)
            || FindStateBlendListByInt(State)
            || FindStateBlendSpacePlayer(State))
        {
            return false;
        }
        FGraphNodeCreator<UAnimGraphNode_BlendListByInt> BlendCreator(*Graph);
        Blend = BlendCreator.CreateNode();
        if (!ConfigureIntBlendArrays(Blend, Operation.BlendTimes))
        {
            return false;
        }
        BlendCreator.Finalize();
        for (int32 Index = 0; Index < Sequences.Num(); ++Index)
        {
            FGraphNodeCreator<UAnimGraphNode_SequencePlayer> PlayerCreator(*Graph);
            UAnimGraphNode_SequencePlayer* Player = PlayerCreator.CreateNode();
            Player->SetAnimationAsset(Sequences[Index]);
            PlayerCreator.Finalize();
            Players.Add(Player);
        }
        FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*Graph);
        Getter = GetterCreator.CreateNode();
        Getter->VariableReference.SetSelfMember(IndexProperty->GetFName());
        GetterCreator.Finalize();
    }
    if (!Blend || Players.Num() != Sequences.Num() || !Getter)
    {
        return false;
    }

    if (!ConfigureIntBlendArrays(Blend, Operation.BlendTimes))
    {
        return false;
    }
    Blend->Modify();
    Blend->NodePosX = Operation.PositionX;
    Blend->NodePosY = Operation.PositionY;
    Blend->ReconstructNode();
    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        UAnimGraphNode_SequencePlayer* Player = Players[Index];
        Player->Modify();
        Player->SetAnimationAsset(Sequences[Index]);
        Player->Node.SetPlayRate(Operation.PlayRates[Index]);
        Player->Node.SetLoopAnimation(Operation.bLoopAnimation);
        Player->NodePosX = Operation.PositionX - 500;
        Player->NodePosY = Operation.PositionY
            + (Index - Players.Num() / 2) * 180;
        Player->ReconstructNode();
    }
    Getter->Modify();
    Getter->VariableReference.SetSelfMember(IndexProperty->GetFName());
    Getter->NodePosX = Operation.PositionX - 500;
    Getter->NodePosY = Operation.PositionY + Players.Num() * 120;
    Getter->ReconstructNode();

    const UEdGraphSchema* Schema = Graph->GetSchema();
    UEdGraphPin* BlendPose = FindNamedPin(Blend, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* IndexInput = FindNamedPin(
        Blend, EGPD_Input, TEXT("ActiveChildIndex"), TEXT("ActiveIndex"));
    UEdGraphPin* GetterValue = Getter->GetValuePin();
    if (!Schema || !BlendPose || !IndexInput || !GetterValue)
    {
        return false;
    }
    IndexInput->BreakAllPinLinks(true);
    Sink->BreakAllPinLinks(true);
    if (!Schema->TryCreateConnection(GetterValue, IndexInput))
    {
        return false;
    }
    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        UEdGraphPin* PlayerPose = FindNamedPin(
            Players[Index], EGPD_Output, TEXT("Pose"));
        UEdGraphPin* PoseInput = FindNamedPin(
            Blend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendPose_%d"), Index)));
        if (!PlayerPose || !PoseInput)
        {
            return false;
        }
        PoseInput->BreakAllPinLinks(true);
        if (!Schema->TryCreateConnection(PlayerPose, PoseInput))
        {
            return false;
        }
    }
    if (!Schema->TryCreateConnection(BlendPose, Sink))
    {
        return false;
    }
    State->StateType = AST_BlendGraph;
    Graph->NotifyGraphChanged();
    UAnimGraphNode_BlendListByInt* VerifiedBlend = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> VerifiedPlayers;
    UK2Node_VariableGet* VerifiedGetter = nullptr;
    if (!GetStateIntBlendParts(
            State, VerifiedBlend, VerifiedPlayers, VerifiedGetter)
        || VerifiedPlayers.Num() != Sequences.Num()
        || VerifiedGetter->GetVarName() != IndexProperty->GetFName())
    {
        return false;
    }
    for (int32 Index = 0; Index < VerifiedPlayers.Num(); ++Index)
    {
        if (VerifiedPlayers[Index]->Node.GetSequence() != Sequences[Index]
            || !FMath::IsNearlyEqual(
                VerifiedPlayers[Index]->Node.GetPlayRate(),
                Operation.PlayRates[Index])
            || !FMath::IsNearlyEqual(
                GetIntBlendTime(VerifiedBlend, Index),
                Operation.BlendTimes[Index]))
        {
            return false;
        }
    }
    return true;
}

bool RemoveStateBlendListByInt(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UAnimGraphNode_BlendListByInt* Blend = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> Players;
    UK2Node_VariableGet* Getter = nullptr;
    if (!Graph || !GetStateIntBlendParts(State, Blend, Players, Getter))
    {
        return false;
    }
    Graph->Modify();
    Blend->Modify();
    Getter->Modify();
    Blend->DestroyNode();
    Getter->DestroyNode();
    for (UAnimGraphNode_SequencePlayer* Player : Players)
    {
        Player->Modify();
        Player->DestroyNode();
    }
    Graph->NotifyGraphChanged();
    return FindStateBlendListByInt(State) == nullptr;
}

bool SetStateApplyAdditive(
    UAnimStateNode* State,
    UAnimBlueprint* Blueprint,
    UAnimSequenceBase* BaseSequence,
    UAnimSequence* AdditiveSequence,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FFloatProperty* AlphaProperty = SkeletonClass
        ? FindFProperty<FFloatProperty>(
            SkeletonClass, FName(*Operation.InputAlphaVariableName))
        : nullptr;
    if (!Graph || !Sink || !BaseSequence || !AdditiveSequence
        || AdditiveSequence->AdditiveAnimType == AAT_None || !AlphaProperty
        || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }
    Graph->Modify();
    State->Modify();
    UAnimGraphNode_ApplyAdditive* ApplyNode = nullptr;
    UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* AdditivePlayer = nullptr;
    UK2Node_VariableGet* AlphaGetter = nullptr;
    const bool bExisting = GetStateApplyAdditiveParts(
        State, ApplyNode, BasePlayer, AdditivePlayer, AlphaGetter);
    if (!bExisting)
    {
        if (FindStateSequencePlayer(State) || FindStateBlendByBool(State)
            || FindStateBlendListByInt(State) || FindStateApplyAdditive(State)
            || FindStateBlendSpacePlayer(State))
        {
            return false;
        }
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> BaseCreator(*Graph);
        BasePlayer = BaseCreator.CreateNode();
        BasePlayer->SetAnimationAsset(BaseSequence);
        BaseCreator.Finalize();
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> AdditiveCreator(*Graph);
        AdditivePlayer = AdditiveCreator.CreateNode();
        AdditivePlayer->SetAnimationAsset(AdditiveSequence);
        AdditiveCreator.Finalize();
        FGraphNodeCreator<UAnimGraphNode_ApplyAdditive> ApplyCreator(*Graph);
        ApplyNode = ApplyCreator.CreateNode();
        ApplyCreator.Finalize();
        FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*Graph);
        AlphaGetter = GetterCreator.CreateNode();
        AlphaGetter->VariableReference.SetSelfMember(AlphaProperty->GetFName());
        GetterCreator.Finalize();
    }
    if (!ApplyNode || !BasePlayer || !AdditivePlayer || !AlphaGetter)
    {
        return false;
    }
    BasePlayer->Modify();
    BasePlayer->SetAnimationAsset(BaseSequence);
    BasePlayer->Node.SetPlayRate(Operation.PlayRate);
    BasePlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
    BasePlayer->NodePosX = Operation.PositionX - 450;
    BasePlayer->NodePosY = Operation.PositionY - 140;
    BasePlayer->ReconstructNode();
    AdditivePlayer->Modify();
    AdditivePlayer->SetAnimationAsset(AdditiveSequence);
    AdditivePlayer->Node.SetPlayRate(Operation.SecondaryPlayRate);
    AdditivePlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
    AdditivePlayer->NodePosX = Operation.PositionX - 450;
    AdditivePlayer->NodePosY = Operation.PositionY + 140;
    AdditivePlayer->ReconstructNode();
    ApplyNode->Modify();
    ApplyNode->NodePosX = Operation.PositionX;
    ApplyNode->NodePosY = Operation.PositionY;
    ApplyNode->ReconstructNode();
    AlphaGetter->Modify();
    AlphaGetter->VariableReference.SetSelfMember(AlphaProperty->GetFName());
    AlphaGetter->NodePosX = Operation.PositionX - 450;
    AlphaGetter->NodePosY = Operation.PositionY + 380;
    AlphaGetter->ReconstructNode();

    UEdGraphPin* BasePose = FindNamedPin(BasePlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* AdditivePose = FindNamedPin(
        AdditivePlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* BaseInput = FindNamedPin(ApplyNode, EGPD_Input, TEXT("Base"));
    UEdGraphPin* AdditiveInput = FindNamedPin(
        ApplyNode, EGPD_Input, TEXT("Additive"));
    UEdGraphPin* AlphaInput = FindNamedPin(
        ApplyNode, EGPD_Input, TEXT("Alpha"));
    UEdGraphPin* Pose = FindNamedPin(ApplyNode, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* AlphaValue = AlphaGetter->GetValuePin();
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!BasePose || !AdditivePose || !BaseInput || !AdditiveInput
        || !AlphaInput || !Pose || !AlphaValue || !Schema)
    {
        return false;
    }
    BaseInput->BreakAllPinLinks(true);
    AdditiveInput->BreakAllPinLinks(true);
    AlphaInput->BreakAllPinLinks(true);
    Sink->BreakAllPinLinks(true);
    if (!Schema->TryCreateConnection(BasePose, BaseInput)
        || !Schema->TryCreateConnection(AdditivePose, AdditiveInput)
        || !Schema->TryCreateConnection(AlphaValue, AlphaInput)
        || !Schema->TryCreateConnection(Pose, Sink))
    {
        return false;
    }
    State->StateType = AST_BlendGraph;
    Graph->NotifyGraphChanged();
    UAnimGraphNode_ApplyAdditive* VerifiedApply = nullptr;
    UAnimGraphNode_SequencePlayer* VerifiedBase = nullptr;
    UAnimGraphNode_SequencePlayer* VerifiedAdditive = nullptr;
    UK2Node_VariableGet* VerifiedAlpha = nullptr;
    return GetStateApplyAdditiveParts(
            State,
            VerifiedApply,
            VerifiedBase,
            VerifiedAdditive,
            VerifiedAlpha)
        && VerifiedBase->Node.GetSequence() == BaseSequence
        && VerifiedAdditive->Node.GetSequence() == AdditiveSequence
        && VerifiedAlpha->GetVarName() == AlphaProperty->GetFName();
}

bool RemoveStateApplyAdditive(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UAnimGraphNode_ApplyAdditive* ApplyNode = nullptr;
    UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
    UAnimGraphNode_SequencePlayer* AdditivePlayer = nullptr;
    UK2Node_VariableGet* AlphaGetter = nullptr;
    if (!Graph || !GetStateApplyAdditiveParts(
            State, ApplyNode, BasePlayer, AdditivePlayer, AlphaGetter))
    {
        return false;
    }
    Graph->Modify();
    ApplyNode->Modify();
    BasePlayer->Modify();
    AdditivePlayer->Modify();
    AlphaGetter->Modify();
    ApplyNode->DestroyNode();
    BasePlayer->DestroyNode();
    AdditivePlayer->DestroyNode();
    AlphaGetter->DestroyNode();
    Graph->NotifyGraphChanged();
    return FindStateApplyAdditive(State) == nullptr;
}

bool SetStateLayeredBlendPerBone(
    UAnimStateNode* State,
    UAnimBlueprint* Blueprint,
    UAnimSequenceBase* BaseSequence,
    const TArray<UAnimSequenceBase*>& LayerSequences,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    USkeleton* TargetSkeleton = Blueprint ? Blueprint->TargetSkeleton : nullptr;
    const int32 LayerCount = LayerSequences.Num();
    if (!Graph || !Sink || !SkeletonClass || !TargetSkeleton || !BaseSequence
        || LayerCount < 1 || LayerCount > 8
        || Operation.ReferencedAssetPaths.Num() != LayerCount
        || Operation.PlayRates.Num() != LayerCount
        || Operation.LayerAlphaVariableNames.Num() != LayerCount
        || Operation.LayerBoneNames.Num() != LayerCount
        || Operation.LayerBlendDepths.Num() != LayerCount
        || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        if (!LayerSequences[LayerIndex]
            || !FindFProperty<FFloatProperty>(
                SkeletonClass,
                FName(*Operation.LayerAlphaVariableNames[LayerIndex]))
            || TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(
                FName(*Operation.LayerBoneNames[LayerIndex])) == INDEX_NONE)
        {
            return false;
        }
    }

    Graph->Modify();
    State->Modify();
    UAnimGraphNode_LayeredBoneBlend* Blend = nullptr;
    UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> LayerPlayers;
    TArray<UK2Node_VariableGet*> AlphaGetters;
    const bool bExisting = GetStateLayeredBlendPerBoneParts(
        State, Blend, BasePlayer, LayerPlayers, AlphaGetters);
    if (bExisting && LayerPlayers.Num() != LayerCount)
    {
        return false;
    }
    if (!bExisting)
    {
        if (FindStateSequencePlayer(State) || FindStateBlendByBool(State)
            || FindStateBlendListByInt(State) || FindStateApplyAdditive(State)
            || FindStateLayeredBlendPerBone(State)
            || FindStateBlendSpacePlayer(State))
        {
            return false;
        }
        FGraphNodeCreator<UAnimGraphNode_SequencePlayer> BaseCreator(*Graph);
        BasePlayer = BaseCreator.CreateNode();
        BasePlayer->SetAnimationAsset(BaseSequence);
        BaseCreator.Finalize();
        FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> BlendCreator(*Graph);
        Blend = BlendCreator.CreateNode();
        if (!ConfigureLayeredBlendPerBone(Blend, Operation))
        {
            return false;
        }
        BlendCreator.Finalize();
        for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
        {
            FGraphNodeCreator<UAnimGraphNode_SequencePlayer> PlayerCreator(*Graph);
            UAnimGraphNode_SequencePlayer* LayerPlayer =
                PlayerCreator.CreateNode();
            LayerPlayer->SetAnimationAsset(LayerSequences[LayerIndex]);
            PlayerCreator.Finalize();
            LayerPlayers.Add(LayerPlayer);

            FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*Graph);
            UK2Node_VariableGet* AlphaGetter = GetterCreator.CreateNode();
            AlphaGetter->VariableReference.SetSelfMember(
                FName(*Operation.LayerAlphaVariableNames[LayerIndex]));
            GetterCreator.Finalize();
            AlphaGetters.Add(AlphaGetter);
        }
    }
    if (!Blend || !BasePlayer || LayerPlayers.Num() != LayerCount
        || AlphaGetters.Num() != LayerCount
        || !ConfigureLayeredBlendPerBone(Blend, Operation))
    {
        return false;
    }

    BasePlayer->Modify();
    BasePlayer->SetAnimationAsset(BaseSequence);
    BasePlayer->Node.SetPlayRate(Operation.PlayRate);
    BasePlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
    BasePlayer->NodePosX = Operation.PositionX - 550;
    BasePlayer->NodePosY = Operation.PositionY - 220;
    BasePlayer->ReconstructNode();
    Blend->Modify();
    Blend->NodePosX = Operation.PositionX;
    Blend->NodePosY = Operation.PositionY;
    Blend->ReconstructNode();
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        UAnimGraphNode_SequencePlayer* LayerPlayer = LayerPlayers[LayerIndex];
        LayerPlayer->Modify();
        LayerPlayer->SetAnimationAsset(LayerSequences[LayerIndex]);
        LayerPlayer->Node.SetPlayRate(Operation.PlayRates[LayerIndex]);
        LayerPlayer->Node.SetLoopAnimation(Operation.bLoopAnimation);
        LayerPlayer->NodePosX = Operation.PositionX - 550;
        LayerPlayer->NodePosY = Operation.PositionY + LayerIndex * 190;
        LayerPlayer->ReconstructNode();

        UK2Node_VariableGet* AlphaGetter = AlphaGetters[LayerIndex];
        AlphaGetter->Modify();
        AlphaGetter->VariableReference.SetSelfMember(
            FName(*Operation.LayerAlphaVariableNames[LayerIndex]));
        AlphaGetter->NodePosX = Operation.PositionX - 300;
        AlphaGetter->NodePosY = Operation.PositionY + 260 + LayerIndex * 190;
        AlphaGetter->ReconstructNode();
    }

    const UEdGraphSchema* Schema = Graph->GetSchema();
    UEdGraphPin* BasePose = FindNamedPin(
        BasePlayer, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* BaseInput = FindNamedPin(
        Blend, EGPD_Input, TEXT("BasePose"), TEXT("Base"));
    UEdGraphPin* BlendPose = FindNamedPin(
        Blend, EGPD_Output, TEXT("Pose"));
    if (!Schema || !BasePose || !BaseInput || !BlendPose)
    {
        return false;
    }
    BaseInput->BreakAllPinLinks(true);
    Sink->BreakAllPinLinks(true);
    if (!Schema->TryCreateConnection(BasePose, BaseInput))
    {
        return false;
    }
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        UEdGraphPin* LayerPose = FindNamedPin(
            LayerPlayers[LayerIndex], EGPD_Output, TEXT("Pose"));
        UEdGraphPin* LayerInput = FindNamedPin(
            Blend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendPoses_%d"), LayerIndex)),
            FName(*FString::Printf(TEXT("BlendPose_%d"), LayerIndex)));
        UEdGraphPin* WeightInput = FindNamedPin(
            Blend,
            EGPD_Input,
            FName(*FString::Printf(TEXT("BlendWeights_%d"), LayerIndex)),
            FName(*FString::Printf(TEXT("BlendWeight_%d"), LayerIndex)));
        UEdGraphPin* AlphaValue = AlphaGetters[LayerIndex]->GetValuePin();
        if (!LayerPose || !LayerInput || !WeightInput || !AlphaValue)
        {
            return false;
        }
        LayerInput->BreakAllPinLinks(true);
        WeightInput->BreakAllPinLinks(true);
        if (!Schema->TryCreateConnection(LayerPose, LayerInput)
            || !Schema->TryCreateConnection(AlphaValue, WeightInput))
        {
            return false;
        }
    }
    if (!Schema->TryCreateConnection(BlendPose, Sink))
    {
        return false;
    }
    State->StateType = AST_BlendGraph;
    Graph->NotifyGraphChanged();

    UAnimGraphNode_LayeredBoneBlend* VerifiedBlend = nullptr;
    UAnimGraphNode_SequencePlayer* VerifiedBase = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> VerifiedLayers;
    TArray<UK2Node_VariableGet*> VerifiedAlphas;
    if (!GetStateLayeredBlendPerBoneParts(
            State,
            VerifiedBlend,
            VerifiedBase,
            VerifiedLayers,
            VerifiedAlphas)
        || VerifiedBase->Node.GetSequence() != BaseSequence
        || VerifiedLayers.Num() != LayerCount
        || VerifiedAlphas.Num() != LayerCount)
    {
        return false;
    }
    for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
    {
        const FBranchFilter& Filter =
            VerifiedBlend->Node.LayerSetup[LayerIndex].BranchFilters[0];
        if (VerifiedLayers[LayerIndex]->Node.GetSequence()
                != LayerSequences[LayerIndex]
            || VerifiedAlphas[LayerIndex]->GetVarName()
                != FName(*Operation.LayerAlphaVariableNames[LayerIndex])
            || Filter.BoneName != FName(*Operation.LayerBoneNames[LayerIndex])
            || Filter.BlendDepth != Operation.LayerBlendDepths[LayerIndex])
        {
            return false;
        }
    }
    return true;
}

bool RemoveStateLayeredBlendPerBone(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UAnimGraphNode_LayeredBoneBlend* Blend = nullptr;
    UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
    TArray<UAnimGraphNode_SequencePlayer*> LayerPlayers;
    TArray<UK2Node_VariableGet*> AlphaGetters;
    if (!Graph || !GetStateLayeredBlendPerBoneParts(
            State, Blend, BasePlayer, LayerPlayers, AlphaGetters))
    {
        return false;
    }
    Graph->Modify();
    Blend->Modify();
    BasePlayer->Modify();
    Blend->DestroyNode();
    BasePlayer->DestroyNode();
    for (UAnimGraphNode_SequencePlayer* LayerPlayer : LayerPlayers)
    {
        LayerPlayer->Modify();
        LayerPlayer->DestroyNode();
    }
    for (UK2Node_VariableGet* AlphaGetter : AlphaGetters)
    {
        AlphaGetter->Modify();
        AlphaGetter->DestroyNode();
    }
    Graph->NotifyGraphChanged();
    return FindStateLayeredBlendPerBone(State) == nullptr;
}

bool SetStateBlendSpacePlayer(
    UAnimStateNode* State,
    UAnimBlueprint* Blueprint,
    UBlendSpace* BlendSpace,
    const FThomasAnimationOperation& Operation)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FFloatProperty* XProperty = SkeletonClass
        ? FindFProperty<FFloatProperty>(
            SkeletonClass, FName(*Operation.InputXVariableName))
        : nullptr;
    const bool bOneDimensional = BlendSpace && BlendSpace->IsA<UBlendSpace1D>();
    FFloatProperty* YProperty = !bOneDimensional && SkeletonClass
        ? FindFProperty<FFloatProperty>(
            SkeletonClass, FName(*Operation.InputYVariableName))
        : nullptr;
    if (!Graph || !Sink || !BlendSpace || !XProperty
        || (!bOneDimensional && !YProperty)
        || HasUnsupportedStatePoseContent(State))
    {
        return false;
    }

    Graph->Modify();
    State->Modify();
    UAnimGraphNode_BlendSpacePlayer* Player = nullptr;
    UK2Node_VariableGet* XGetter = nullptr;
    UK2Node_VariableGet* YGetter = nullptr;
    bool bExistingOneDimensional = false;
    const bool bExisting = GetStateBlendSpaceParts(
        State, Player, XGetter, YGetter, bExistingOneDimensional);
    if (bExisting && bExistingOneDimensional != bOneDimensional)
    {
        return false;
    }
    if (!bExisting)
    {
        if (FindStateSequencePlayer(State) || FindStateBlendByBool(State)
            || FindStateBlendSpacePlayer(State))
        {
            return false;
        }
        FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> PlayerCreator(*Graph);
        Player = PlayerCreator.CreateNode();
        Player->SetAnimationAsset(BlendSpace);
        PlayerCreator.Finalize();
        FGraphNodeCreator<UK2Node_VariableGet> XCreator(*Graph);
        XGetter = XCreator.CreateNode();
        XGetter->VariableReference.SetSelfMember(XProperty->GetFName());
        XCreator.Finalize();
        if (!bOneDimensional)
        {
            FGraphNodeCreator<UK2Node_VariableGet> YCreator(*Graph);
            YGetter = YCreator.CreateNode();
            YGetter->VariableReference.SetSelfMember(YProperty->GetFName());
            YCreator.Finalize();
        }
    }
    if (!Player || !XGetter || (!bOneDimensional && !YGetter))
    {
        return false;
    }

    Player->Modify();
    Player->SetAnimationAsset(BlendSpace);
    Player->Node.SetPlayRate(Operation.PlayRate);
    Player->Node.SetLoop(Operation.bLoopAnimation);
    Player->NodePosX = Operation.PositionX;
    Player->NodePosY = Operation.PositionY;
    Player->ReconstructNode();
    XGetter->Modify();
    XGetter->VariableReference.SetSelfMember(XProperty->GetFName());
    XGetter->NodePosX = Operation.PositionX - 400;
    XGetter->NodePosY = Operation.PositionY - 120;
    XGetter->ReconstructNode();
    if (YGetter && YProperty)
    {
        YGetter->Modify();
        YGetter->VariableReference.SetSelfMember(YProperty->GetFName());
        YGetter->NodePosX = Operation.PositionX - 400;
        YGetter->NodePosY = Operation.PositionY + 120;
        YGetter->ReconstructNode();
    }

    UEdGraphPin* Pose = FindNamedPin(Player, EGPD_Output, TEXT("Pose"));
    UEdGraphPin* XInput = FindNamedPin(Player, EGPD_Input, TEXT("X"));
    UEdGraphPin* YInput = FindNamedPin(Player, EGPD_Input, TEXT("Y"));
    UEdGraphPin* XValue = XGetter->GetValuePin();
    UEdGraphPin* YValue = YGetter ? YGetter->GetValuePin() : nullptr;
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Pose || !XInput || !XValue || !Schema
        || (!bOneDimensional && (!YInput || !YValue)))
    {
        return false;
    }
    XInput->BreakAllPinLinks(true);
    if (YInput) YInput->BreakAllPinLinks(true);
    Sink->BreakAllPinLinks(true);
    if (!Schema->TryCreateConnection(XValue, XInput)
        || (!bOneDimensional && !Schema->TryCreateConnection(YValue, YInput))
        || !Schema->TryCreateConnection(Pose, Sink))
    {
        return false;
    }
    State->StateType = AST_BlendGraph;
    Graph->NotifyGraphChanged();
    UAnimGraphNode_BlendSpacePlayer* VerifiedPlayer = nullptr;
    UK2Node_VariableGet* VerifiedX = nullptr;
    UK2Node_VariableGet* VerifiedY = nullptr;
    bool bVerifiedOneDimensional = false;
    return GetStateBlendSpaceParts(
            State,
            VerifiedPlayer,
            VerifiedX,
            VerifiedY,
            bVerifiedOneDimensional)
        && VerifiedPlayer->GetAnimationAsset() == BlendSpace
        && VerifiedX->GetVarName() == XProperty->GetFName()
        && (bOneDimensional
            || VerifiedY->GetVarName() == YProperty->GetFName());
}

bool RemoveStateBlendSpacePlayer(UAnimStateNode* State)
{
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UAnimGraphNode_BlendSpacePlayer* Player = nullptr;
    UK2Node_VariableGet* XGetter = nullptr;
    UK2Node_VariableGet* YGetter = nullptr;
    bool bOneDimensional = false;
    if (!Graph || !GetStateBlendSpaceParts(
            State, Player, XGetter, YGetter, bOneDimensional))
    {
        return false;
    }
    Graph->Modify();
    Player->Modify();
    XGetter->Modify();
    Player->DestroyNode();
    XGetter->DestroyNode();
    if (YGetter)
    {
        YGetter->Modify();
        YGetter->DestroyNode();
    }
    Graph->NotifyGraphChanged();
    return FindStateBlendSpacePlayer(State) == nullptr;
}

bool SetTransitionRuleConstant(
    UAnimStateTransitionNode* Transition,
    const bool bValue)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input) : nullptr;
    if (!Graph || !RulePin || !RulePin->LinkedTo.IsEmpty())
    {
        return false;
    }
    Graph->Modify();
    ResultNode->Modify();
    RulePin->Modify();
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        return false;
    }
    Schema->TrySetDefaultValue(*RulePin, bValue ? TEXT("true") : TEXT("false"));
    Graph->NotifyGraphChanged();
    return RulePin->DefaultValue.Equals(
        bValue ? TEXT("true") : TEXT("false"),
        ESearchCase::IgnoreCase);
}

UK2Node_VariableGet* FindTransitionRuleVariableGetter(
    UAnimStateTransitionNode* Transition)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input) : nullptr;
    if (!RulePin || RulePin->LinkedTo.Num() != 1 || !RulePin->LinkedTo[0])
    {
        return nullptr;
    }
    return Cast<UK2Node_VariableGet>(RulePin->LinkedTo[0]->GetOwningNode());
}

bool SetTransitionRuleVariable(
    UAnimStateTransitionNode* Transition,
    UAnimBlueprint* Blueprint,
    const FThomasAnimationOperation& Operation)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input) : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FBoolProperty* Property = SkeletonClass
        ? FindFProperty<FBoolProperty>(SkeletonClass, FName(*Operation.Name))
        : nullptr;
    UK2Node_VariableGet* Getter = FindTransitionRuleVariableGetter(Transition);
    if (!Graph || !RulePin || !Property
        || (!RulePin->LinkedTo.IsEmpty() && !Getter))
    {
        return false;
    }
    Graph->Modify();
    ResultNode->Modify();
    RulePin->Modify();
    if (!Getter)
    {
        FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
        Getter = Creator.CreateNode();
        Getter->VariableReference.SetSelfMember(Property->GetFName());
        Getter->NodePosX = Operation.PositionX;
        Getter->NodePosY = Operation.PositionY;
        Creator.Finalize();
    }
    else
    {
        Getter->Modify();
        Getter->VariableReference.SetSelfMember(Property->GetFName());
        Getter->NodePosX = Operation.PositionX;
        Getter->NodePosY = Operation.PositionY;
        Getter->ReconstructNode();
    }
    UEdGraphPin* ValuePin = Getter->GetValuePin();
    if (!ValuePin)
    {
        return false;
    }
    RulePin->BreakAllPinLinks(true);
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema || !Schema->TryCreateConnection(ValuePin, RulePin))
    {
        return false;
    }
    Graph->NotifyGraphChanged();
    return FindTransitionRuleVariableGetter(Transition) == Getter
        && Getter->GetVarName() == Property->GetFName();
}

bool RemoveTransitionRuleVariable(UAnimStateTransitionNode* Transition)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input) : nullptr;
    UK2Node_VariableGet* Getter = FindTransitionRuleVariableGetter(Transition);
    if (!Graph || !RulePin || !Getter)
    {
        return false;
    }
    Graph->Modify();
    RulePin->Modify();
    RulePin->BreakAllPinLinks(true);
    Getter->Modify();
    Getter->DestroyNode();
    if (const UEdGraphSchema* Schema = Graph->GetSchema())
    {
        Schema->TrySetDefaultValue(*RulePin, TEXT("false"));
    }
    Graph->NotifyGraphChanged();
    return FindTransitionRuleVariableGetter(Transition) == nullptr
        && RulePin->LinkedTo.IsEmpty();
}

FName TransitionComparisonFunction(const FString& ComparisonOperator)
{
    if (ComparisonOperator == TEXT("less"))
    {
        return TEXT("Less_DoubleDouble");
    }
    if (ComparisonOperator == TEXT("less_equal"))
    {
        return TEXT("LessEqual_DoubleDouble");
    }
    if (ComparisonOperator == TEXT("greater"))
    {
        return TEXT("Greater_DoubleDouble");
    }
    if (ComparisonOperator == TEXT("greater_equal"))
    {
        return TEXT("GreaterEqual_DoubleDouble");
    }
    if (ComparisonOperator == TEXT("equal"))
    {
        return TEXT("EqualEqual_DoubleDouble");
    }
    if (ComparisonOperator == TEXT("not_equal"))
    {
        return TEXT("NotEqual_DoubleDouble");
    }
    return NAME_None;
}

FString TransitionComparisonName(const FName FunctionName)
{
    if (FunctionName == TEXT("Less_FloatFloat")
        || FunctionName == TEXT("Less_DoubleDouble")) return TEXT("less");
    if (FunctionName == TEXT("LessEqual_FloatFloat")
        || FunctionName == TEXT("LessEqual_DoubleDouble")) return TEXT("less_equal");
    if (FunctionName == TEXT("Greater_FloatFloat")
        || FunctionName == TEXT("Greater_DoubleDouble")) return TEXT("greater");
    if (FunctionName == TEXT("GreaterEqual_FloatFloat")
        || FunctionName == TEXT("GreaterEqual_DoubleDouble")) return TEXT("greater_equal");
    if (FunctionName == TEXT("EqualEqual_FloatFloat")
        || FunctionName == TEXT("EqualEqual_DoubleDouble")) return TEXT("equal");
    if (FunctionName == TEXT("NotEqual_FloatFloat")
        || FunctionName == TEXT("NotEqual_DoubleDouble")) return TEXT("not_equal");
    return TEXT("unsupported");
}

FName TransitionNumericBinaryFunction(const FString& Operator)
{
    if (Operator == TEXT("add")) return TEXT("Add_DoubleDouble");
    if (Operator == TEXT("subtract")) return TEXT("Subtract_DoubleDouble");
    if (Operator == TEXT("multiply")) return TEXT("Multiply_DoubleDouble");
    if (Operator == TEXT("divide")) return TEXT("Divide_DoubleDouble");
    if (Operator == TEXT("min")) return TEXT("FMin");
    if (Operator == TEXT("max")) return TEXT("FMax");
    return NAME_None;
}

FString TransitionNumericBinaryName(const FName FunctionName)
{
    if (FunctionName == TEXT("Add_FloatFloat")
        || FunctionName == TEXT("Add_DoubleDouble")) return TEXT("add");
    if (FunctionName == TEXT("Subtract_FloatFloat")
        || FunctionName == TEXT("Subtract_DoubleDouble")) return TEXT("subtract");
    if (FunctionName == TEXT("Multiply_FloatFloat")
        || FunctionName == TEXT("Multiply_DoubleDouble")) return TEXT("multiply");
    if (FunctionName == TEXT("Divide_FloatFloat")
        || FunctionName == TEXT("Divide_DoubleDouble")) return TEXT("divide");
    if (FunctionName == TEXT("FMin")) return TEXT("min");
    if (FunctionName == TEXT("FMax")) return TEXT("max");
    return TEXT("unsupported");
}

FName TransitionNumericUnaryFunction(const FString& Operator)
{
    return Operator == TEXT("abs") ? FName(TEXT("Abs")) : NAME_None;
}

FString TransitionNumericUnaryName(const FName FunctionName)
{
    return FunctionName == TEXT("Abs")
        ? TEXT("abs") : TEXT("unsupported");
}

FName TransitionLogicalFunction(const FString& LogicalOperator)
{
    if (LogicalOperator == TEXT("and")) return TEXT("BooleanAND");
    if (LogicalOperator == TEXT("or")) return TEXT("BooleanOR");
    return NAME_None;
}

FString TransitionLogicalName(const FName FunctionName)
{
    if (FunctionName == TEXT("BooleanAND")) return TEXT("and");
    if (FunctionName == TEXT("BooleanOR")) return TEXT("or");
    return TEXT("unsupported");
}

TOptional<ETransitionGetter::Type> ParseTransitionGetterType(
    const FString& GetterName)
{
    if (GetterName == TEXT("current_time"))
    {
        return ETransitionGetter::AnimationAsset_GetCurrentTime;
    }
    if (GetterName == TEXT("length"))
    {
        return ETransitionGetter::AnimationAsset_GetLength;
    }
    if (GetterName == TEXT("current_time_fraction"))
    {
        return ETransitionGetter::AnimationAsset_GetCurrentTimeFraction;
    }
    if (GetterName == TEXT("time_remaining"))
    {
        return ETransitionGetter::AnimationAsset_GetTimeFromEnd;
    }
    if (GetterName == TEXT("time_remaining_fraction"))
    {
        return ETransitionGetter::AnimationAsset_GetTimeFromEndFraction;
    }
    if (GetterName == TEXT("state_elapsed_time"))
    {
        return ETransitionGetter::CurrentState_ElapsedTime;
    }
    if (GetterName == TEXT("state_blend_weight"))
    {
        return ETransitionGetter::CurrentState_GetBlendWeight;
    }
    if (GetterName == TEXT("transition_duration"))
    {
        return ETransitionGetter::CurrentTransitionDuration;
    }
    if (GetterName == TEXT("arbitrary_state_blend_weight"))
    {
        return ETransitionGetter::ArbitraryState_GetBlendWeight;
    }
    return TOptional<ETransitionGetter::Type>();
}

FString TransitionGetterTypeName(const ETransitionGetter::Type GetterType)
{
    if (GetterType == ETransitionGetter::AnimationAsset_GetCurrentTime)
    {
        return TEXT("current_time");
    }
    if (GetterType == ETransitionGetter::AnimationAsset_GetLength)
    {
        return TEXT("length");
    }
    if (GetterType
        == ETransitionGetter::AnimationAsset_GetCurrentTimeFraction)
    {
        return TEXT("current_time_fraction");
    }
    if (GetterType == ETransitionGetter::AnimationAsset_GetTimeFromEnd)
    {
        return TEXT("time_remaining");
    }
    if (GetterType
        == ETransitionGetter::AnimationAsset_GetTimeFromEndFraction)
    {
        return TEXT("time_remaining_fraction");
    }
    if (GetterType == ETransitionGetter::CurrentState_ElapsedTime)
    {
        return TEXT("state_elapsed_time");
    }
    if (GetterType == ETransitionGetter::CurrentState_GetBlendWeight)
    {
        return TEXT("state_blend_weight");
    }
    if (GetterType == ETransitionGetter::CurrentTransitionDuration)
    {
        return TEXT("transition_duration");
    }
    if (GetterType == ETransitionGetter::ArbitraryState_GetBlendWeight)
    {
        return TEXT("arbitrary_state_blend_weight");
    }
    return TEXT("unsupported");
}

bool TransitionGetterRequiresAnimAssetPlayer(
    const ETransitionGetter::Type GetterType)
{
    return GetterType == ETransitionGetter::AnimationAsset_GetCurrentTime
        || GetterType == ETransitionGetter::AnimationAsset_GetLength
        || GetterType
            == ETransitionGetter::AnimationAsset_GetCurrentTimeFraction
        || GetterType == ETransitionGetter::AnimationAsset_GetTimeFromEnd
        || GetterType
            == ETransitionGetter::AnimationAsset_GetTimeFromEndFraction;
}

bool TransitionGetterRequiresStateContext(
    const ETransitionGetter::Type GetterType)
{
    return GetterType == ETransitionGetter::ArbitraryState_GetBlendWeight;
}

bool IsTransitionTimeRemainingGetterType(
    const ETransitionGetter::Type GetterType)
{
    return GetterType == ETransitionGetter::AnimationAsset_GetTimeFromEnd
        || GetterType
            == ETransitionGetter::AnimationAsset_GetTimeFromEndFraction;
}

UEdGraphPin* FindFirstDataOutput(UEdGraphNode* Node)
{
    if (!Node)
    {
        return nullptr;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output
            && Pin->PinType.PinCategory != TEXT("exec"))
        {
            return Pin;
        }
    }
    return nullptr;
}

bool GetDirectStateSequencePlayer(
    UAnimStateNode* State,
    UAnimGraphNode_SequencePlayer*& OutPlayer)
{
    OutPlayer = nullptr;
    UAnimationStateGraph* Graph = State
        ? Cast<UAnimationStateGraph>(State->BoundGraph) : nullptr;
    UEdGraphPin* Sink = State ? State->GetPoseSinkPinInsideState() : nullptr;
    if (!Graph || !Sink || Graph->Nodes.Num() != 2)
    {
        return false;
    }
    OutPlayer = FindStateSequencePlayer(State);
    UEdGraphPin* Pose = FindNamedPin(
        OutPlayer, EGPD_Output, TEXT("Pose"));
    return OutPlayer && Pose && Sink->LinkedTo.Num() == 1
        && Sink->LinkedTo.Contains(Pose);
}

FName TransitionUnaryFunction(const FString& Operator)
{
    return Operator == TEXT("not") ? FName(TEXT("Not_PreBool")) : NAME_None;
}

FString TransitionUnaryName(const FName FunctionName)
{
    return FunctionName == TEXT("Not_PreBool")
        ? TEXT("not") : TEXT("unsupported");
}

FString CanonicalTransitionNumber(const double Value)
{
    return FString::Printf(TEXT("%.9g"), Value);
}

FString TransitionExpressionTokenText(
    const FThomasAnimationTransitionToken& Token)
{
    if (Token.Kind == TEXT("bool_variable")
        || Token.Kind == TEXT("float_variable")
        || Token.Kind == TEXT("transition_getter"))
    {
        FString Text = Token.Kind + TEXT(":") + Token.Name;
        if (Token.Kind == TEXT("transition_getter")
            && !Token.ContextName.IsEmpty())
        {
            FString EncodedContext = Token.ContextName;
            EncodedContext.ReplaceInline(TEXT("%"), TEXT("%25"));
            EncodedContext.ReplaceInline(TEXT(":"), TEXT("%3A"));
            EncodedContext.ReplaceInline(TEXT("|"), TEXT("%7C"));
            Text += TEXT(":") + EncodedContext;
        }
        return Text;
    }
    if (Token.Kind == TEXT("number"))
    {
        return Token.Kind + TEXT(":")
            + CanonicalTransitionNumber(Token.Value);
    }
    if (Token.Kind == TEXT("compare"))
    {
        return Token.Kind + TEXT(":") + Token.Operator;
    }
    return Token.Kind;
}

FString TransitionExpressionCanonical(
    const TArray<FThomasAnimationTransitionToken>& Tokens)
{
    TArray<FString> Parts;
    Parts.Reserve(Tokens.Num());
    for (const FThomasAnimationTransitionToken& Token : Tokens)
    {
        Parts.Add(TransitionExpressionTokenText(Token));
    }
    return FString::Join(Parts, TEXT("|"));
}

enum class ETransitionExpressionValueType : uint8
{
    Bool,
    Number
};

bool ValidateTransitionExpression(
    const TArray<FThomasAnimationTransitionToken>& Tokens,
    const UAnimBlueprint* Blueprint,
    const bool bHasDirectSequencePlayer,
    const TSet<FString>* AvailableStateNames,
    FString& OutError)
{
    OutError.Reset();
    if (Tokens.IsEmpty() || Tokens.Num() > 64)
    {
        OutError = TEXT("token_count");
        return false;
    }
    const UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    TArray<ETransitionExpressionValueType> Stack;
    Stack.Reserve(Tokens.Num());
    for (const FThomasAnimationTransitionToken& Token : Tokens)
    {
        if (Token.Kind != TEXT("transition_getter")
            && !Token.ContextName.IsEmpty())
        {
            OutError = TEXT("context:") + Token.Kind;
            return false;
        }
        if (Token.Kind == TEXT("bool_variable"))
        {
            if (Token.Name.IsEmpty()
                || !FindFProperty<FBoolProperty>(
                    SkeletonClass, FName(*Token.Name)))
            {
                OutError = TEXT("bool_variable:") + Token.Name;
                return false;
            }
            Stack.Add(ETransitionExpressionValueType::Bool);
        }
        else if (Token.Kind == TEXT("float_variable"))
        {
            if (Token.Name.IsEmpty()
                || !FindFProperty<FFloatProperty>(
                    SkeletonClass, FName(*Token.Name)))
            {
                OutError = TEXT("float_variable:") + Token.Name;
                return false;
            }
            Stack.Add(ETransitionExpressionValueType::Number);
        }
        else if (Token.Kind == TEXT("transition_getter"))
        {
            const TOptional<ETransitionGetter::Type> GetterType =
                ParseTransitionGetterType(Token.Name);
            const bool bRequiresStateContext = GetterType.IsSet()
                && TransitionGetterRequiresStateContext(
                    GetterType.GetValue());
            if (!GetterType.IsSet()
                || (TransitionGetterRequiresAnimAssetPlayer(
                        GetterType.GetValue())
                    && !bHasDirectSequencePlayer)
                || (bRequiresStateContext
                    && (Token.ContextName.IsEmpty()
                        || !AvailableStateNames
                        || !AvailableStateNames->Contains(
                            Token.ContextName)))
                || (!bRequiresStateContext
                    && !Token.ContextName.IsEmpty()))
            {
                OutError = TEXT("transition_getter:") + Token.Name
                    + (Token.ContextName.IsEmpty()
                        ? FString()
                        : TEXT(":") + Token.ContextName);
                return false;
            }
            Stack.Add(ETransitionExpressionValueType::Number);
        }
        else if (Token.Kind == TEXT("number"))
        {
            if (!FMath::IsFinite(Token.Value))
            {
                OutError = TEXT("number");
                return false;
            }
            Stack.Add(ETransitionExpressionValueType::Number);
        }
        else if (Token.Kind == TEXT("compare"))
        {
            if (TransitionComparisonFunction(Token.Operator).IsNone()
                || Stack.Num() < 2
                || Stack[Stack.Num() - 1]
                    != ETransitionExpressionValueType::Number
                || Stack[Stack.Num() - 2]
                    != ETransitionExpressionValueType::Number)
            {
                OutError = TEXT("compare:") + Token.Operator;
                return false;
            }
            Stack.SetNum(Stack.Num() - 2);
            Stack.Add(ETransitionExpressionValueType::Bool);
        }
        else if (!TransitionNumericBinaryFunction(Token.Kind).IsNone())
        {
            if (Stack.Num() < 2
                || Stack[Stack.Num() - 1]
                    != ETransitionExpressionValueType::Number
                || Stack[Stack.Num() - 2]
                    != ETransitionExpressionValueType::Number)
            {
                OutError = Token.Kind;
                return false;
            }
            Stack.SetNum(Stack.Num() - 2);
            Stack.Add(ETransitionExpressionValueType::Number);
        }
        else if (!TransitionNumericUnaryFunction(Token.Kind).IsNone())
        {
            if (Stack.IsEmpty()
                || Stack.Last() != ETransitionExpressionValueType::Number)
            {
                OutError = Token.Kind;
                return false;
            }
        }
        else if (Token.Kind == TEXT("and") || Token.Kind == TEXT("or"))
        {
            if (Stack.Num() < 2
                || Stack[Stack.Num() - 1]
                    != ETransitionExpressionValueType::Bool
                || Stack[Stack.Num() - 2]
                    != ETransitionExpressionValueType::Bool)
            {
                OutError = Token.Kind;
                return false;
            }
            Stack.SetNum(Stack.Num() - 2);
            Stack.Add(ETransitionExpressionValueType::Bool);
        }
        else if (Token.Kind == TEXT("not"))
        {
            if (Stack.IsEmpty()
                || Stack.Last() != ETransitionExpressionValueType::Bool)
            {
                OutError = TEXT("not");
                return false;
            }
        }
        else
        {
            OutError = TEXT("kind:") + Token.Kind;
            return false;
        }
    }
    if (Stack.Num() != 1
        || Stack[0] != ETransitionExpressionValueType::Bool)
    {
        OutError = TEXT("result");
        return false;
    }
    return true;
}

struct FTransitionExpressionRuleParts
{
    TArray<FThomasAnimationTransitionToken> Tokens;
    TArray<UEdGraphNode*> Nodes;
    FString Canonical;
};

bool GetTransitionExpressionRuleParts(
    UAnimStateTransitionNode* Transition,
    UAnimBlueprint* Blueprint,
    FTransitionExpressionRuleParts& OutParts)
{
    OutParts = {};
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    if (!Graph || !RulePin || RulePin->LinkedTo.Num() != 1)
    {
        return false;
    }
    UAnimStateNode* PreviousState = Transition
        ? Cast<UAnimStateNode>(Transition->GetPreviousState()) : nullptr;
    UAnimGraphNode_SequencePlayer* PreviousPlayer = nullptr;
    const bool bHasDirectPlayer = GetDirectStateSequencePlayer(
        PreviousState, PreviousPlayer);
    TSet<FString> AvailableStateNames;
    GetTransitionMachineStateNames(Transition, AvailableStateNames);
    const UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    TSet<UEdGraphNode*> Visited;
    TFunction<bool(UEdGraphPin*)> ParseOutput;
    auto ParseNumericInput = [&](UEdGraphPin* InputPin) -> bool
    {
        if (!InputPin || InputPin->LinkedTo.Num() > 1)
        {
            return false;
        }
        if (InputPin->LinkedTo.Num() == 1)
        {
            return ParseOutput(InputPin->LinkedTo[0]);
        }
        double Value = 0.0;
        if (!LexTryParseString(Value, *InputPin->DefaultValue)
            || !FMath::IsFinite(Value))
        {
            return false;
        }
        FThomasAnimationTransitionToken& Token = OutParts.Tokens.AddDefaulted_GetRef();
        Token.Kind = TEXT("number");
        Token.Value = Value;
        return true;
    };
    ParseOutput = [&](UEdGraphPin* OutputPin) -> bool
    {
        UEdGraphNode* Node = OutputPin && OutputPin->Direction == EGPD_Output
            ? OutputPin->GetOwningNode() : nullptr;
        if (!Node || Visited.Contains(Node))
        {
            return false;
        }
        Visited.Add(Node);
        OutParts.Nodes.Add(Node);
        if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
        {
            if (Getter->GetValuePin() != OutputPin)
            {
                return false;
            }
            const FName VariableName = Getter->GetVarName();
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            if (FindFProperty<FBoolProperty>(SkeletonClass, VariableName))
            {
                Token.Kind = TEXT("bool_variable");
            }
            else if (FindFProperty<FFloatProperty>(SkeletonClass, VariableName))
            {
                Token.Kind = TEXT("float_variable");
            }
            else
            {
                return false;
            }
            Token.Name = VariableName.ToString();
            return true;
        }
        if (UK2Node_TransitionRuleGetter* Getter =
                Cast<UK2Node_TransitionRuleGetter>(Node))
        {
            const FString GetterName = TransitionGetterTypeName(
                Getter->GetterType);
            const bool bRequiresPlayer =
                TransitionGetterRequiresAnimAssetPlayer(Getter->GetterType);
            const bool bRequiresStateContext =
                TransitionGetterRequiresStateContext(Getter->GetterType);
            UAnimStateNode* AssociatedState = Getter->AssociatedStateNode;
            if (GetterName == TEXT("unsupported")
                || (bRequiresPlayer
                    && (!bHasDirectPlayer
                        || Getter->AssociatedAnimAssetPlayerNode
                            != PreviousPlayer))
                || (!bRequiresPlayer
                    && Getter->AssociatedAnimAssetPlayerNode != nullptr)
                || (bRequiresStateContext
                    && (!AssociatedState
                        || FindStateInTransitionMachine(
                            Transition,
                            AssociatedState->GetStateName())
                            != AssociatedState))
                || (!bRequiresStateContext && AssociatedState != nullptr)
                || FindFirstDataOutput(Getter) != OutputPin)
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = TEXT("transition_getter");
            Token.Name = GetterName;
            Token.ContextName = AssociatedState
                ? AssociatedState->GetStateName() : FString();
            return true;
        }
        UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
        if (!Call || FindNamedPin(Call, EGPD_Output, TEXT("ReturnValue"))
            != OutputPin)
        {
            return false;
        }
        const FName FunctionName = Call->FunctionReference.GetMemberName();
        const FString Comparison = TransitionComparisonName(FunctionName);
        if (Comparison != TEXT("unsupported"))
        {
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!ParseNumericInput(A) || !ParseNumericInput(B))
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = TEXT("compare");
            Token.Operator = Comparison;
            return true;
        }
        const FString NumericBinary =
            TransitionNumericBinaryName(FunctionName);
        if (NumericBinary != TEXT("unsupported"))
        {
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!ParseNumericInput(A) || !ParseNumericInput(B))
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = NumericBinary;
            return true;
        }
        const FString NumericUnary =
            TransitionNumericUnaryName(FunctionName);
        if (NumericUnary != TEXT("unsupported"))
        {
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            if (!A || A->LinkedTo.Num() > 1
                || (A->LinkedTo.Num() == 1
                    ? !ParseOutput(A->LinkedTo[0])
                    : !ParseNumericInput(A)))
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = NumericUnary;
            return true;
        }
        const FString Logical = TransitionLogicalName(FunctionName);
        if (Logical != TEXT("unsupported"))
        {
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!A || A->LinkedTo.Num() != 1
                || !B || B->LinkedTo.Num() != 1
                || !ParseOutput(A->LinkedTo[0])
                || !ParseOutput(B->LinkedTo[0]))
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = Logical;
            return true;
        }
        if (TransitionUnaryName(FunctionName) == TEXT("not"))
        {
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            if (!A || A->LinkedTo.Num() != 1
                || !ParseOutput(A->LinkedTo[0]))
            {
                return false;
            }
            FThomasAnimationTransitionToken& Token =
                OutParts.Tokens.AddDefaulted_GetRef();
            Token.Kind = TEXT("not");
            return true;
        }
        return false;
    };
    if (!ParseOutput(RulePin->LinkedTo[0])
        || Graph->Nodes.Num() != OutParts.Nodes.Num() + 1)
    {
        return false;
    }
    FString ValidationError;
    if (!ValidateTransitionExpression(
            OutParts.Tokens, Blueprint, bHasDirectPlayer,
            &AvailableStateNames,
            ValidationError))
    {
        return false;
    }
    OutParts.Canonical = TransitionExpressionCanonical(OutParts.Tokens);
    return true;
}

bool SetTransitionRuleExpression(
    UAnimStateTransitionNode* Transition,
    UAnimBlueprint* Blueprint,
    const FThomasAnimationOperation& Operation)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    UAnimStateNode* PreviousState = Transition
        ? Cast<UAnimStateNode>(Transition->GetPreviousState()) : nullptr;
    UAnimGraphNode_SequencePlayer* PreviousPlayer = nullptr;
    const bool bHasDirectPlayer = GetDirectStateSequencePlayer(
        PreviousState, PreviousPlayer);
    TSet<FString> AvailableStateNames;
    GetTransitionMachineStateNames(Transition, AvailableStateNames);
    FString ValidationError;
    FTransitionExpressionRuleParts Existing;
    const bool bExisting = GetTransitionExpressionRuleParts(
        Transition, Blueprint, Existing);
    if (!Graph || !RulePin
        || !ValidateTransitionExpression(
            Operation.TransitionExpressionTokens,
            Blueprint,
            bHasDirectPlayer,
            &AvailableStateNames,
            ValidationError)
        || (!RulePin->LinkedTo.IsEmpty() && !bExisting))
    {
        return false;
    }

    Graph->Modify();
    ResultNode->Modify();
    RulePin->Modify();
    RulePin->BreakAllPinLinks(true);
    for (UEdGraphNode* Node : Existing.Nodes)
    {
        Node->Modify();
        Node->DestroyNode();
    }

    struct FOperand
    {
        UEdGraphPin* Pin = nullptr;
        FString Literal;
    };
    TArray<FOperand> Stack;
    TArray<UEdGraphNode*> CreatedNodes;
    const UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        return false;
    }
    auto PositionNode = [&](UEdGraphNode* Node, const int32 TokenIndex)
    {
        Node->NodePosX = Operation.PositionX
            - (Operation.TransitionExpressionTokens.Num() - TokenIndex) * 190;
        Node->NodePosY = Operation.PositionY
            + ((TokenIndex % 3) - 1) * 160;
    };
    auto CreateCall = [&](const FName FunctionName, const int32 TokenIndex)
        -> UK2Node_CallFunction*
    {
        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
        UK2Node_CallFunction* Call = Creator.CreateNode();
        Call->FunctionReference.SetExternalMember(
            FunctionName, UKismetMathLibrary::StaticClass());
        PositionNode(Call, TokenIndex);
        Creator.Finalize();
        CreatedNodes.Add(Call);
        return Call;
    };
    auto ConnectOperand = [&](const FOperand& Operand, UEdGraphPin* Input)
    {
        if (!Input)
        {
            return false;
        }
        if (Operand.Pin)
        {
            return Schema->TryCreateConnection(Operand.Pin, Input);
        }
        Schema->TrySetDefaultValue(*Input, Operand.Literal);
        return true;
    };

    for (int32 TokenIndex = 0;
         TokenIndex < Operation.TransitionExpressionTokens.Num();
         ++TokenIndex)
    {
        const FThomasAnimationTransitionToken& Token =
            Operation.TransitionExpressionTokens[TokenIndex];
        if (Token.Kind == TEXT("number"))
        {
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Literal = CanonicalTransitionNumber(Token.Value);
        }
        else if (Token.Kind == TEXT("bool_variable")
            || Token.Kind == TEXT("float_variable"))
        {
            FProperty* Property = FindFProperty<FProperty>(
                SkeletonClass, FName(*Token.Name));
            FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
            UK2Node_VariableGet* Getter = Creator.CreateNode();
            Getter->VariableReference.SetSelfMember(Property->GetFName());
            PositionNode(Getter, TokenIndex);
            Creator.Finalize();
            CreatedNodes.Add(Getter);
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = Getter->GetValuePin();
            if (!Operand.Pin)
            {
                return false;
            }
        }
        else if (Token.Kind == TEXT("transition_getter"))
        {
            FGraphNodeCreator<UK2Node_TransitionRuleGetter> Creator(*Graph);
            UK2Node_TransitionRuleGetter* Getter = Creator.CreateNode();
            Getter->GetterType = ParseTransitionGetterType(Token.Name).GetValue();
            Getter->AssociatedAnimAssetPlayerNode =
                TransitionGetterRequiresAnimAssetPlayer(Getter->GetterType)
                    ? PreviousPlayer : nullptr;
            Getter->AssociatedStateNode =
                TransitionGetterRequiresStateContext(Getter->GetterType)
                    ? FindStateInTransitionMachine(
                        Transition, Token.ContextName)
                    : nullptr;
            PositionNode(Getter, TokenIndex);
            Creator.Finalize();
            CreatedNodes.Add(Getter);
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindFirstDataOutput(Getter);
            if (!Operand.Pin)
            {
                return false;
            }
        }
        else if (Token.Kind == TEXT("compare"))
        {
            const FOperand Right = Stack.Pop(EAllowShrinking::No);
            const FOperand Left = Stack.Pop(EAllowShrinking::No);
            UK2Node_CallFunction* Call = CreateCall(
                TransitionComparisonFunction(Token.Operator), TokenIndex);
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!ConnectOperand(Left, A) || !ConnectOperand(Right, B))
            {
                return false;
            }
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindNamedPin(
                Call, EGPD_Output, TEXT("ReturnValue"));
        }
        else if (!TransitionNumericBinaryFunction(Token.Kind).IsNone())
        {
            const FOperand Right = Stack.Pop(EAllowShrinking::No);
            const FOperand Left = Stack.Pop(EAllowShrinking::No);
            UK2Node_CallFunction* Call = CreateCall(
                TransitionNumericBinaryFunction(Token.Kind), TokenIndex);
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!ConnectOperand(Left, A) || !ConnectOperand(Right, B))
            {
                return false;
            }
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindNamedPin(
                Call, EGPD_Output, TEXT("ReturnValue"));
        }
        else if (!TransitionNumericUnaryFunction(Token.Kind).IsNone())
        {
            const FOperand Input = Stack.Pop(EAllowShrinking::No);
            UK2Node_CallFunction* Call = CreateCall(
                TransitionNumericUnaryFunction(Token.Kind), TokenIndex);
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            if (!ConnectOperand(Input, A))
            {
                return false;
            }
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindNamedPin(
                Call, EGPD_Output, TEXT("ReturnValue"));
        }
        else if (Token.Kind == TEXT("and") || Token.Kind == TEXT("or"))
        {
            const FOperand Right = Stack.Pop(EAllowShrinking::No);
            const FOperand Left = Stack.Pop(EAllowShrinking::No);
            UK2Node_CallFunction* Call = CreateCall(
                TransitionLogicalFunction(Token.Kind), TokenIndex);
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            UEdGraphPin* B = FindNamedPin(Call, EGPD_Input, TEXT("B"));
            if (!ConnectOperand(Left, A) || !ConnectOperand(Right, B))
            {
                return false;
            }
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindNamedPin(
                Call, EGPD_Output, TEXT("ReturnValue"));
        }
        else
        {
            const FOperand Input = Stack.Pop(EAllowShrinking::No);
            UK2Node_CallFunction* Call = CreateCall(
                TransitionUnaryFunction(Token.Kind), TokenIndex);
            UEdGraphPin* A = FindNamedPin(Call, EGPD_Input, TEXT("A"));
            if (!ConnectOperand(Input, A))
            {
                return false;
            }
            FOperand& Operand = Stack.AddDefaulted_GetRef();
            Operand.Pin = FindNamedPin(
                Call, EGPD_Output, TEXT("ReturnValue"));
        }
        if (Stack.IsEmpty()
            || (!Stack.Last().Pin && Stack.Last().Literal.IsEmpty()))
        {
            return false;
        }
    }
    if (Stack.Num() != 1 || !Stack[0].Pin
        || !Schema->TryCreateConnection(Stack[0].Pin, RulePin))
    {
        return false;
    }
    Graph->NotifyGraphChanged();
    FTransitionExpressionRuleParts Verified;
    return GetTransitionExpressionRuleParts(
            Transition, Blueprint, Verified)
        && Verified.Canonical == TransitionExpressionCanonical(
            Operation.TransitionExpressionTokens);
}

bool RemoveTransitionRuleExpression(
    UAnimStateTransitionNode* Transition,
    UAnimBlueprint* Blueprint)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    FTransitionExpressionRuleParts Parts;
    if (!Graph || !RulePin
        || !GetTransitionExpressionRuleParts(
            Transition, Blueprint, Parts))
    {
        return false;
    }
    Graph->Modify();
    RulePin->Modify();
    RulePin->BreakAllPinLinks(true);
    for (UEdGraphNode* Node : Parts.Nodes)
    {
        Node->Modify();
        Node->DestroyNode();
    }
    if (const UEdGraphSchema* Schema = Graph->GetSchema())
    {
        Schema->TrySetDefaultValue(*RulePin, TEXT("false"));
    }
    Graph->NotifyGraphChanged();
    FTransitionExpressionRuleParts Removed;
    return !GetTransitionExpressionRuleParts(
            Transition, Blueprint, Removed)
        && RulePin->LinkedTo.IsEmpty()
        && Graph->Nodes.Num() == 1;
}

struct FTransitionTimeRemainingRuleParts
{
    UK2Node_TransitionRuleGetter* TimeGetter = nullptr;
    UK2Node_CallFunction* Comparison = nullptr;
    UK2Node_VariableGet* GuardGetter = nullptr;
    UK2Node_CallFunction* Logical = nullptr;
};

bool GetTransitionTimeRemainingRuleParts(
    UAnimStateTransitionNode* Transition,
    FTransitionTimeRemainingRuleParts& OutParts)
{
    OutParts = {};
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    if (!Graph || !RulePin || RulePin->LinkedTo.Num() != 1
        || Graph->Nodes.Num() != 5)
    {
        return false;
    }
    OutParts.Logical = Cast<UK2Node_CallFunction>(
        RulePin->LinkedTo[0]->GetOwningNode());
    const FName LogicalFunction = OutParts.Logical
        ? OutParts.Logical->FunctionReference.GetMemberName() : NAME_None;
    UEdGraphPin* LogicalA = FindNamedPin(
        OutParts.Logical, EGPD_Input, TEXT("A"));
    UEdGraphPin* LogicalB = FindNamedPin(
        OutParts.Logical, EGPD_Input, TEXT("B"));
    if (TransitionLogicalName(LogicalFunction) == TEXT("unsupported")
        || !LogicalA || LogicalA->LinkedTo.Num() != 1
        || !LogicalB || LogicalB->LinkedTo.Num() != 1)
    {
        return false;
    }
    OutParts.Comparison = Cast<UK2Node_CallFunction>(
        LogicalA->LinkedTo[0]->GetOwningNode());
    OutParts.GuardGetter = Cast<UK2Node_VariableGet>(
        LogicalB->LinkedTo[0]->GetOwningNode());
    const FName ComparisonFunction = OutParts.Comparison
        ? OutParts.Comparison->FunctionReference.GetMemberName() : NAME_None;
    UEdGraphPin* ComparisonA = FindNamedPin(
        OutParts.Comparison, EGPD_Input, TEXT("A"));
    UEdGraphPin* ComparisonB = FindNamedPin(
        OutParts.Comparison, EGPD_Input, TEXT("B"));
    if (!OutParts.GuardGetter
        || TransitionComparisonName(ComparisonFunction) == TEXT("unsupported")
        || !ComparisonA || ComparisonA->LinkedTo.Num() != 1
        || !ComparisonB || !ComparisonB->LinkedTo.IsEmpty())
    {
        return false;
    }
    OutParts.TimeGetter = Cast<UK2Node_TransitionRuleGetter>(
        ComparisonA->LinkedTo[0]->GetOwningNode());
    if (!OutParts.TimeGetter
        || !IsTransitionTimeRemainingGetterType(
            OutParts.TimeGetter->GetterType))
    {
        return false;
    }
    int32 ResultCount = 0;
    int32 TimeGetterCount = 0;
    int32 VariableGetterCount = 0;
    int32 CallCount = 0;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UAnimGraphNode_TransitionResult>()) ++ResultCount;
        else if (Node->IsA<UK2Node_TransitionRuleGetter>())
            ++TimeGetterCount;
        else if (Node->IsA<UK2Node_VariableGet>()) ++VariableGetterCount;
        else if (Node->IsA<UK2Node_CallFunction>()) ++CallCount;
        else return false;
    }
    return ResultCount == 1 && TimeGetterCount == 1
        && VariableGetterCount == 1 && CallCount == 2;
}

bool SetTransitionRuleTimeRemaining(
    UAnimStateTransitionNode* Transition,
    UAnimBlueprint* Blueprint,
    const FThomasAnimationOperation& Operation)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    UClass* SkeletonClass = Blueprint
        ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
    FBoolProperty* GuardProperty = SkeletonClass
        ? FindFProperty<FBoolProperty>(SkeletonClass, FName(*Operation.Name))
        : nullptr;
    const TOptional<ETransitionGetter::Type> GetterType =
        ParseTransitionGetterType(Operation.TransitionGetter);
    const FName ComparisonFunction =
        TransitionComparisonFunction(Operation.ComparisonOperator);
    const FName LogicalFunction =
        TransitionLogicalFunction(Operation.LogicalOperator);
    UAnimStateNode* PreviousState = Transition
        ? Cast<UAnimStateNode>(Transition->GetPreviousState()) : nullptr;
    UAnimGraphNode_SequencePlayer* PreviousPlayer = nullptr;
    FTransitionTimeRemainingRuleParts Parts;
    const bool bExisting = GetTransitionTimeRemainingRuleParts(
        Transition, Parts);
    if (!Graph || !RulePin || !GuardProperty || !GetterType.IsSet()
        || !IsTransitionTimeRemainingGetterType(GetterType.GetValue())
        || ComparisonFunction.IsNone() || LogicalFunction.IsNone()
        || !GetDirectStateSequencePlayer(PreviousState, PreviousPlayer)
        || (!RulePin->LinkedTo.IsEmpty() && !bExisting))
    {
        return false;
    }
    Graph->Modify();
    ResultNode->Modify();
    RulePin->Modify();
    if (!bExisting)
    {
        FGraphNodeCreator<UK2Node_TransitionRuleGetter>
            GetterCreator(*Graph);
        Parts.TimeGetter = GetterCreator.CreateNode();
        Parts.TimeGetter->GetterType = GetterType.GetValue();
        Parts.TimeGetter->AssociatedAnimAssetPlayerNode = PreviousPlayer;
        GetterCreator.Finalize();

        FGraphNodeCreator<UK2Node_CallFunction> ComparisonCreator(*Graph);
        Parts.Comparison = ComparisonCreator.CreateNode();
        Parts.Comparison->FunctionReference.SetExternalMember(
            ComparisonFunction, UKismetMathLibrary::StaticClass());
        ComparisonCreator.Finalize();

        FGraphNodeCreator<UK2Node_VariableGet> GuardCreator(*Graph);
        Parts.GuardGetter = GuardCreator.CreateNode();
        Parts.GuardGetter->VariableReference.SetSelfMember(
            GuardProperty->GetFName());
        GuardCreator.Finalize();

        FGraphNodeCreator<UK2Node_CallFunction> LogicalCreator(*Graph);
        Parts.Logical = LogicalCreator.CreateNode();
        Parts.Logical->FunctionReference.SetExternalMember(
            LogicalFunction, UKismetMathLibrary::StaticClass());
        LogicalCreator.Finalize();
    }
    Parts.TimeGetter->Modify();
    Parts.TimeGetter->GetterType = GetterType.GetValue();
    Parts.TimeGetter->AssociatedAnimAssetPlayerNode = PreviousPlayer;
    Parts.TimeGetter->NodePosX = Operation.PositionX - 650;
    Parts.TimeGetter->NodePosY = Operation.PositionY - 120;
    Parts.TimeGetter->ReconstructNode();
    Parts.Comparison->Modify();
    Parts.Comparison->FunctionReference.SetExternalMember(
        ComparisonFunction, UKismetMathLibrary::StaticClass());
    Parts.Comparison->NodePosX = Operation.PositionX - 400;
    Parts.Comparison->NodePosY = Operation.PositionY - 120;
    Parts.Comparison->ReconstructNode();
    Parts.GuardGetter->Modify();
    Parts.GuardGetter->VariableReference.SetSelfMember(
        GuardProperty->GetFName());
    Parts.GuardGetter->NodePosX = Operation.PositionX - 400;
    Parts.GuardGetter->NodePosY = Operation.PositionY + 120;
    Parts.GuardGetter->ReconstructNode();
    Parts.Logical->Modify();
    Parts.Logical->FunctionReference.SetExternalMember(
        LogicalFunction, UKismetMathLibrary::StaticClass());
    Parts.Logical->NodePosX = Operation.PositionX - 150;
    Parts.Logical->NodePosY = Operation.PositionY;
    Parts.Logical->ReconstructNode();

    UEdGraphPin* TimeValue = FindFirstDataOutput(Parts.TimeGetter);
    UEdGraphPin* ComparisonA = FindNamedPin(
        Parts.Comparison, EGPD_Input, TEXT("A"));
    UEdGraphPin* ComparisonB = FindNamedPin(
        Parts.Comparison, EGPD_Input, TEXT("B"));
    UEdGraphPin* ComparisonResult = FindNamedPin(
        Parts.Comparison, EGPD_Output, TEXT("ReturnValue"));
    UEdGraphPin* GuardValue = Parts.GuardGetter->GetValuePin();
    UEdGraphPin* LogicalA = FindNamedPin(
        Parts.Logical, EGPD_Input, TEXT("A"));
    UEdGraphPin* LogicalB = FindNamedPin(
        Parts.Logical, EGPD_Input, TEXT("B"));
    UEdGraphPin* LogicalResult = FindNamedPin(
        Parts.Logical, EGPD_Output, TEXT("ReturnValue"));
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!TimeValue || !ComparisonA || !ComparisonB || !ComparisonResult
        || !GuardValue || !LogicalA || !LogicalB || !LogicalResult || !Schema)
    {
        return false;
    }
    ComparisonA->BreakAllPinLinks(true);
    ComparisonB->BreakAllPinLinks(true);
    LogicalA->BreakAllPinLinks(true);
    LogicalB->BreakAllPinLinks(true);
    RulePin->BreakAllPinLinks(true);
    Schema->TrySetDefaultValue(
        *ComparisonB, FString::SanitizeFloat(Operation.Value));
    const bool bTimeConnected =
        Schema->TryCreateConnection(TimeValue, ComparisonA);
    const bool bComparisonConnected =
        Schema->TryCreateConnection(ComparisonResult, LogicalA);
    const bool bGuardConnected =
        Schema->TryCreateConnection(GuardValue, LogicalB);
    const bool bRuleConnected =
        Schema->TryCreateConnection(LogicalResult, RulePin);
    if (!bTimeConnected || !bComparisonConnected
        || !bGuardConnected || !bRuleConnected)
    {
        return false;
    }
    Graph->NotifyGraphChanged();
    FTransitionTimeRemainingRuleParts Verified;
    return GetTransitionTimeRemainingRuleParts(
        Transition, Verified)
        && Verified.TimeGetter->GetterType == GetterType.GetValue()
        && Verified.TimeGetter->AssociatedAnimAssetPlayerNode == PreviousPlayer
        && Verified.GuardGetter->GetVarName() == GuardProperty->GetFName()
        && TransitionComparisonName(
            Verified.Comparison->FunctionReference.GetMemberName())
            == Operation.ComparisonOperator
        && Verified.Logical->FunctionReference.GetMemberName()
            == LogicalFunction;
}

bool RemoveTransitionRuleTimeRemaining(
    UAnimStateTransitionNode* Transition)
{
    UAnimationTransitionGraph* Graph = Transition
        ? Cast<UAnimationTransitionGraph>(Transition->BoundGraph) : nullptr;
    UAnimGraphNode_TransitionResult* ResultNode = Graph
        ? Graph->GetResultNode() : nullptr;
    UEdGraphPin* RulePin = ResultNode
        ? ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input)
        : nullptr;
    FTransitionTimeRemainingRuleParts Parts;
    if (!Graph || !RulePin
        || !GetTransitionTimeRemainingRuleParts(Transition, Parts))
    {
        return false;
    }
    Graph->Modify();
    RulePin->Modify();
    RulePin->BreakAllPinLinks(true);
    Parts.TimeGetter->Modify();
    Parts.Comparison->Modify();
    Parts.GuardGetter->Modify();
    Parts.Logical->Modify();
    Parts.TimeGetter->DestroyNode();
    Parts.Comparison->DestroyNode();
    Parts.GuardGetter->DestroyNode();
    Parts.Logical->DestroyNode();
    if (const UEdGraphSchema* Schema = Graph->GetSchema())
    {
        Schema->TrySetDefaultValue(*RulePin, TEXT("false"));
    }
    Graph->NotifyGraphChanged();
    FTransitionTimeRemainingRuleParts RemovedParts;
    return !GetTransitionTimeRemainingRuleParts(Transition, RemovedParts)
        && RulePin->LinkedTo.IsEmpty();
}
}

class FThomasEditorAnimationModule final : public IThomasEditorAnimationProviderModule
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
        return Assets->GetDomainInventory(TEXT("AnimationCharacter"), PackageRoot,
            {
                TEXT("/Script/Engine.AnimBlueprint"), TEXT("/Script/Engine.AnimSequence"),
                TEXT("/Script/Engine.AnimMontage"), TEXT("/Script/Engine.BlendSpace"),
                TEXT("/Script/Engine.Skeleton"), TEXT("/Script/Engine.SkeletalMesh"),
                TEXT("/Script/ControlRigDeveloper.ControlRigBlueprint"),
                TEXT("/Script/ControlRig.ControlRigRuntimeAsset"),
                TEXT("/Script/IKRig.IKRigDefinition"), TEXT("/Script/IKRig.IKRetargeter")
            }, MaxResults);
    }

    virtual FThomasSpecializedAssetInspectionResult InspectAnimationAsset(
        const FString& InputPath,
        const int32 InputMaxItems) override
    {
        FThomasSpecializedAssetInspectionResult Result;
        Result.Domain = TEXT("AnimationCharacter");
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

        if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
        {
            Result.AssetKind = TEXT("anim_sequence");
            const IAnimationDataModel* DataModel = Sequence->GetDataModel();
            Result.Metrics = {
                FString::Printf(TEXT("Skeleton=%s"), Sequence->GetSkeleton() ? *Sequence->GetSkeleton()->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("PlayLength=%.6f"), Sequence->GetPlayLength()),
                FString::Printf(TEXT("SampledKeys=%d"), Sequence->GetNumberOfSampledKeys()),
                FString::Printf(TEXT("SamplingRate=%s"), *Sequence->GetSamplingFrameRate().ToPrettyText().ToString()),
                FString::Printf(TEXT("NotifyCount=%d"), Sequence->Notifies.Num()),
                FString::Printf(TEXT("NotifyTrackCount=%d"), Sequence->AnimNotifyTracks.Num()),
                FString::Printf(TEXT("FloatCurveCount=%d"), DataModel ? DataModel->GetFloatCurves().Num() : 0),
                FString::Printf(TEXT("RootMotionEnabled=%s"), Sequence->bEnableRootMotion ? TEXT("true") : TEXT("false")),
                FString::Printf(TEXT("ForceRootLock=%s"), Sequence->bForceRootLock ? TEXT("true") : TEXT("false")),
                FString::Printf(TEXT("RootMotionLock=%s"),
                    *StaticEnum<ERootMotionRootLock::Type>()->GetNameStringByValue(Sequence->RootMotionRootLock)),
                FString::Printf(TEXT("AdditiveType=%s"),
                    *AdditiveTypeName(Sequence->AdditiveAnimType)),
                FString::Printf(TEXT("AdditiveBasePoseType=%s"),
                    *AdditiveBasePoseTypeName(Sequence->RefPoseType)),
                FString::Printf(TEXT("AdditiveBasePoseAsset=%s"),
                    Sequence->RefPoseSeq ? *Sequence->RefPoseSeq->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("AdditiveBaseFrame=%d"), Sequence->RefFrameIndex)
            };
            for (int32 TrackIndex = 0; TrackIndex < Sequence->AnimNotifyTracks.Num(); ++TrackIndex)
            {
                const FAnimNotifyTrack& Track = Sequence->AnimNotifyTracks[TrackIndex];
                AddBounded(Result, FString::Printf(TEXT("NotifyTrack[%d] Name=%s Color=%s"),
                    TrackIndex, *Track.TrackName.ToString(), *Track.TrackColor.ToString()), MaxItems);
            }
            for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
            {
                const FAnimNotifyEvent& Notify = Sequence->Notifies[Index];
                AddBounded(Result, FString::Printf(TEXT("Notify[%d] Name=%s Time=%.6f Duration=%.6f"),
                    Index, *Notify.GetNotifyEventName().ToString(), Notify.GetTriggerTime(), Notify.GetDuration()), MaxItems);
            }
            if (DataModel)
            {
                for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
                {
                    AddBounded(Result, FString::Printf(TEXT("Curve Name=%s Keys=%d"),
                        *Curve.GetName().ToString(), Curve.FloatCurve.GetNumKeys()), MaxItems);
                    for (auto KeyIterator = Curve.FloatCurve.GetKeyIterator(); KeyIterator; ++KeyIterator)
                    {
                        const FRichCurveKey& Key = *KeyIterator;
                        AddBounded(Result, FString::Printf(TEXT("CurveKey Curve=%s Time=%.6f Value=%.6f"),
                            *Curve.GetName().ToString(), Key.Time, Key.Value), MaxItems);
                    }
                }
            }
        }
        else if (const UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
        {
            Result.AssetKind = TEXT("anim_montage");
            Result.Metrics = {
                FString::Printf(TEXT("Skeleton=%s"), Montage->GetSkeleton() ? *Montage->GetSkeleton()->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("PlayLength=%.6f"), Montage->GetPlayLength()),
                FString::Printf(TEXT("SectionCount=%d"), Montage->GetNumSections()),
                FString::Printf(TEXT("SlotTrackCount=%d"), Montage->SlotAnimTracks.Num()),
                FString::Printf(TEXT("NotifyCount=%d"), Montage->Notifies.Num())
            };
            for (int32 Index = 0; Index < Montage->GetNumSections(); ++Index)
            {
                const FCompositeSection& Section = Montage->GetAnimCompositeSection(Index);
                AddBounded(Result, FString::Printf(TEXT("Section[%d] Name=%s Time=%.6f Next=%s"),
                    Index, *Section.SectionName.ToString(), Section.GetTime(),
                    *Section.NextSectionName.ToString()), MaxItems);
            }
            for (int32 Index = 0; Index < Montage->SlotAnimTracks.Num(); ++Index)
            {
                AddBounded(Result, FString::Printf(TEXT("Slot[%d]=%s Segments=%d"), Index,
                    *Montage->SlotAnimTracks[Index].SlotName.ToString(),
                    Montage->SlotAnimTracks[Index].AnimTrack.AnimSegments.Num()), MaxItems);
                const TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[Index].AnimTrack.AnimSegments;
                for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
                {
                    const FAnimSegment& Segment = Segments[SegmentIndex];
                    AddBounded(Result, FString::Printf(
                        TEXT("Segment Slot=%d Index=%d Asset=%s Start=%.6f AnimRange=(%.6f,%.6f) Rate=%.6f Loops=%d"),
                        Index, SegmentIndex,
                        Segment.GetAnimReference() ? *Segment.GetAnimReference()->GetPathName() : TEXT("None"),
                        Segment.StartPos, Segment.AnimStartTime, Segment.AnimEndTime,
                        Segment.AnimPlayRate, Segment.LoopingCount), MaxItems);
                }
            }
        }
        else if (UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Asset))
        {
            Result.AssetKind = TEXT("anim_blueprint");
            Result.Metrics.Add(FString::Printf(TEXT("TargetSkeleton=%s"),
                Blueprint->TargetSkeleton ? *Blueprint->TargetSkeleton->GetPathName() : TEXT("None")));
            Result.Metrics.Add(FString::Printf(TEXT("GeneratedClass=%s"),
                Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetPathName() : TEXT("None")));
            Result.Metrics.Add(FString::Printf(TEXT("CompileStatus=%d"),
                static_cast<int32>(Blueprint->Status)));
            TArray<UAnimGraphNode_StateMachine*> Machines;
            GetStateMachines(Blueprint, Machines);
            int32 StateCount = 0;
            int32 TransitionCount = 0;
            int32 StatePlayerCount = 0;
            int32 StateBoolBlendCount = 0;
            int32 StateBlendSpacePlayerCount = 0;
            int32 StateIntBlendCount = 0;
            int32 StateApplyAdditiveCount = 0;
            int32 StateLayeredBlendPerBoneCount = 0;
            int32 TransitionRuleCount = 0;
            int32 TransitionVariableRuleCount = 0;
            int32 TransitionTimeRemainingRuleCount = 0;
            int32 TransitionExpressionRuleCount = 0;
            int32 StateGraphNodeCount = 0;
            int32 StateGraphNodePropertyCount = 0;
            int32 StateGraphNodeArrayCount = 0;
            int32 StateGraphPoseArrayCount = 0;
            int32 StateGraphConstraintArrayCount = 0;
            int32 StateGraphPinCount = 0;
            int32 StateGraphLinkCount = 0;
            const TArray<UClass*> AvailableStateGraphNodeClasses =
                GetAvailableStateGraphNodeClasses();
            TMap<FString, int32> StateGraphPropertyGapCounts;
            TArray<FString> StateGraphPropertyGaps;
            TMap<FString, int32> StateGraphTopologyPropertyCounts;
            TMap<FString, int32> StateGraphDedicatedArrayCounts;
            TMap<FString, int32> StateGraphCoupledPoseArrayCounts;
            TMap<FString, int32> StateGraphCoupledConstraintArrayCounts;
            TMap<FString, int32> StateGraphAssetBoundArrayCounts;
            TSet<FString> StateGraphResolvedStructuralArrays;
            int32 StateGraphWholeArrayPropertyCount = 0;
            for (const UClass* NodeClass : AvailableStateGraphNodeClasses)
            {
                const FStructProperty* RuntimeProperty =
                    FindStateGraphRuntimeNodeProperty(NodeClass);
                if (!NodeClass || !RuntimeProperty
                    || !RuntimeProperty->Struct)
                {
                    continue;
                }
                for (TFieldIterator<FProperty> It(
                         RuntimeProperty->Struct,
                         EFieldIteratorFlags::IncludeSuper);
                     It;
                     ++It)
                {
                    const FProperty* Property = *It;
                    const FArrayProperty* ArrayProperty =
                        CastField<const FArrayProperty>(Property);
                    const FString AssetBoundArrayKind =
                        StateGraphAssetBoundArrayKind(
                            RuntimeProperty->Struct,
                            ArrayProperty);
                    if (IsSupportedStateGraphWholeArray(
                            RuntimeProperty->Struct,
                            ArrayProperty))
                    {
                        if (AssetBoundArrayKind.IsEmpty())
                        {
                            ++StateGraphWholeArrayPropertyCount;
                        }
                        StateGraphResolvedStructuralArrays.Add(
                            StateGraphArrayKey(
                                RuntimeProperty->Struct,
                                ArrayProperty));
                    }
                    if (!AssetBoundArrayKind.IsEmpty())
                    {
                        ++StateGraphAssetBoundArrayCounts.FindOrAdd(
                            AssetBoundArrayKind);
                    }
                    const FString DedicatedArrayKind =
                        StateGraphDedicatedArrayKind(
                            RuntimeProperty->Struct,
                            ArrayProperty);
                    if (!DedicatedArrayKind.IsEmpty())
                    {
                        ++StateGraphDedicatedArrayCounts.FindOrAdd(
                            DedicatedArrayKind);
                        StateGraphResolvedStructuralArrays.Add(
                            StateGraphArrayKey(
                                RuntimeProperty->Struct,
                                ArrayProperty));
                    }
                    const FString CoupledPoseArrayKind =
                        StateGraphCoupledPoseArrayKind(
                            RuntimeProperty->Struct,
                            ArrayProperty);
                    if (!CoupledPoseArrayKind.IsEmpty())
                    {
                        ++StateGraphCoupledPoseArrayCounts.FindOrAdd(
                            CoupledPoseArrayKind);
                        StateGraphResolvedStructuralArrays.Add(
                            StateGraphArrayKey(
                                RuntimeProperty->Struct,
                                ArrayProperty));
                    }
                    const FString CoupledConstraintArrayKind =
                        StateGraphCoupledConstraintArrayKind(
                            RuntimeProperty->Struct,
                            ArrayProperty);
                    if (!CoupledConstraintArrayKind.IsEmpty())
                    {
                        ++StateGraphCoupledConstraintArrayCounts.FindOrAdd(
                            CoupledConstraintArrayKind);
                        StateGraphResolvedStructuralArrays.Add(
                            StateGraphArrayKey(
                                RuntimeProperty->Struct,
                                ArrayProperty));
                    }
                    const FString TopologyKind =
                        StateGraphTopologyPropertyKind(Property);
                    if (!TopologyKind.IsEmpty())
                    {
                        ++StateGraphTopologyPropertyCounts.FindOrAdd(
                            TopologyKind);
                        if (ArrayProperty)
                        {
                            StateGraphResolvedStructuralArrays.Add(
                                StateGraphArrayKey(
                                    RuntimeProperty->Struct,
                                    ArrayProperty));
                        }
                        continue;
                    }
                    const FString Kind =
                        StateGraphUnsupportedPropertyKind(
                            Property,
                            RuntimeProperty->Struct);
                    if (Kind.IsEmpty())
                    {
                        continue;
                    }
                    ++StateGraphPropertyGapCounts.FindOrAdd(Kind);
                    const FString ArrayInnerType =
                        ArrayProperty && ArrayProperty->Inner
                        ? ArrayProperty->Inner->GetCPPType()
                        : FString();
                    const FStructProperty* ArrayInnerStruct = ArrayProperty
                        ? CastField<const FStructProperty>(
                            ArrayProperty->Inner)
                        : nullptr;
                    const FStructProperty* GapStruct =
                        CastField<const FStructProperty>(Property);
                    FString PropertyDetail;
                    if (!ArrayInnerType.IsEmpty())
                    {
                        PropertyDetail = FString::Printf(
                            TEXT(" InnerType=%s DefaultNum=%d"),
                            *ArrayInnerType,
                            StateGraphDefaultArraySize(
                                NodeClass,
                                RuntimeProperty,
                                ArrayProperty));
                        const FString InnerFields =
                            StateGraphStructFieldSchema(
                                ArrayInnerStruct
                                    ? ArrayInnerStruct->Struct
                                    : nullptr);
                        if (!InnerFields.IsEmpty())
                        {
                            PropertyDetail += TEXT(" InnerFields=(")
                                + InnerFields + TEXT(")");
                        }
                    }
                    else if (GapStruct)
                    {
                        const FString Fields = StateGraphStructFieldSchema(
                            GapStruct->Struct);
                        if (!Fields.IsEmpty())
                        {
                            PropertyDetail = TEXT(" Fields=(")
                                + Fields + TEXT(")");
                        }
                    }
                    StateGraphPropertyGaps.Add(FString::Printf(
                        TEXT("StateGraphPropertyGap Class=%s Path=%s.%s Kind=%s Type=%s%s"),
                        *NodeClass->GetPathName(),
                        *RuntimeProperty->GetName(),
                        Property ? *Property->GetName() : TEXT("Missing"),
                        *Kind,
                        Property ? *Property->GetCPPType() : TEXT("Missing"),
                        *PropertyDetail));
                }
            }
            StateGraphPropertyGaps.Sort();
            for (UAnimGraphNode_StateMachine* Machine : Machines)
            {
                if (!Machine || !Machine->EditorStateMachineGraph)
                {
                    continue;
                }
                int32 MachineStateCount = 0;
                int32 MachineTransitionCount = 0;
                FString EntryState = TEXT("None");
                if (UAnimStateEntryNode* Entry = Machine->EditorStateMachineGraph->EntryNode)
                {
                    for (UEdGraphPin* Pin : Entry->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Output && !Pin->LinkedTo.IsEmpty())
                        {
                            if (const UAnimStateNode* State =
                                    Cast<UAnimStateNode>(Pin->LinkedTo[0]->GetOwningNode()))
                            {
                                EntryState = State->GetStateName();
                            }
                            break;
                        }
                    }
                }
                for (UEdGraphNode* Node : Machine->EditorStateMachineGraph->Nodes)
                {
                    if (const UAnimStateNode* State = Cast<UAnimStateNode>(Node))
                    {
                        ++MachineStateCount;
                        AddBounded(Result, FString::Printf(
                            TEXT("State Machine=%s Name=%s Guid=%s Position=(%d,%d) AlwaysReset=%s Graph=%s"),
                            *Machine->GetStateMachineName(),
                            *State->GetStateName(),
                            *State->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
                            State->NodePosX,
                            State->NodePosY,
                            State->bAlwaysResetOnEntry ? TEXT("true") : TEXT("false"),
                            State->BoundGraph ? *State->BoundGraph->GetPathName() : TEXT("None")),
                            MaxItems);
                        if (const UAnimationStateGraph* StateGraph =
                                Cast<UAnimationStateGraph>(State->BoundGraph))
                        {
                            AddBounded(Result, FString::Printf(
                                TEXT("StateGraph Machine=%s State=%s State=%s"),
                                *Machine->GetStateMachineName(),
                                *State->GetStateName(),
                                *StateGraphStateString(StateGraph)),
                                MaxItems);
                            for (const UEdGraphNode* StateGraphNode :
                                 StateGraph->Nodes)
                            {
                                if (!StateGraphNode)
                                {
                                    continue;
                                }
                                ++StateGraphNodeCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateGraphNode Machine=%s State=%s State=%s"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *StateGraphNodeStateString(StateGraphNode)),
                                    MaxItems);
                                if (const UAnimGraphNode_Base* AnimGraphNode =
                                        Cast<UAnimGraphNode_Base>(
                                            StateGraphNode))
                                {
                                    for (const FStateGraphNodePropertyRef&
                                             Reference :
                                         GetStateGraphNodeProperties(
                                             AnimGraphNode))
                                    {
                                        ++StateGraphNodePropertyCount;
                                        AddBounded(Result, FString::Printf(
                                            TEXT("StateGraphNodeProperty Machine=%s State=%s NodeGuid=%s Class=%s State=%s"),
                                            *Machine->GetStateMachineName(),
                                            *State->GetStateName(),
                                            *StateGraphNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *StateGraphNode->GetClass()
                                                ->GetPathName(),
                                            *StateGraphNodePropertyStateString(
                                                Reference)),
                                            MaxItems);
                                    }
                                    for (const FStateGraphNodeArrayRef&
                                             Reference :
                                         GetStateGraphNodeArrays(
                                             AnimGraphNode))
                                    {
                                        ++StateGraphNodeArrayCount;
                                        AddBounded(Result, FString::Printf(
                                            TEXT("StateGraphNodeArray Machine=%s State=%s NodeGuid=%s Class=%s State=%s"),
                                            *Machine->GetStateMachineName(),
                                            *State->GetStateName(),
                                            *StateGraphNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *StateGraphNode->GetClass()
                                                ->GetPathName(),
                                            *StateGraphNodeArrayStateString(
                                                Reference)),
                                            MaxItems);
                                    }
                                    FStateGraphPoseArrayRef PoseArray;
                                    if (ResolveStateGraphPoseArray(
                                            AnimGraphNode,
                                            FString(),
                                            PoseArray))
                                    {
                                        ++StateGraphPoseArrayCount;
                                        AddBounded(Result, FString::Printf(
                                            TEXT("StateGraphPoseArray Machine=%s State=%s NodeGuid=%s Class=%s State=%s"),
                                            *Machine->GetStateMachineName(),
                                            *State->GetStateName(),
                                            *StateGraphNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *StateGraphNode->GetClass()
                                                ->GetPathName(),
                                            *StateGraphPoseArrayStateString(
                                                AnimGraphNode,
                                                PoseArray)),
                                            MaxItems);
                                    }
                                    FStateGraphConstraintArrayRef
                                        ConstraintArray;
                                    if (ResolveStateGraphConstraintArray(
                                            AnimGraphNode,
                                            FString(),
                                            ConstraintArray))
                                    {
                                        ++StateGraphConstraintArrayCount;
                                        AddBounded(Result, FString::Printf(
                                            TEXT("StateGraphConstraintArray Machine=%s State=%s NodeGuid=%s Class=%s State=%s"),
                                            *Machine->GetStateMachineName(),
                                            *State->GetStateName(),
                                            *StateGraphNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *StateGraphNode->GetClass()
                                                ->GetPathName(),
                                            *StateGraphConstraintArrayStateString(
                                                AnimGraphNode,
                                                ConstraintArray)),
                                            MaxItems);
                                    }
                                }
                                for (const UEdGraphPin* Pin :
                                     StateGraphNode->Pins)
                                {
                                    if (!Pin)
                                    {
                                        continue;
                                    }
                                    ++StateGraphPinCount;
                                    AddBounded(Result, FString::Printf(
                                        TEXT("StateGraphPin Machine=%s State=%s NodeGuid=%s State=%s"),
                                        *Machine->GetStateMachineName(),
                                        *State->GetStateName(),
                                        *StateGraphNode->NodeGuid.ToString(
                                            EGuidFormats::DigitsWithHyphensLower),
                                        *StateGraphPinStateString(Pin)),
                                        MaxItems);
                                    if (Pin->Direction != EGPD_Output)
                                    {
                                        continue;
                                    }
                                    for (const UEdGraphPin* LinkedPin :
                                         Pin->LinkedTo)
                                    {
                                        const UEdGraphNode* LinkedNode =
                                            LinkedPin
                                                ? LinkedPin->GetOwningNode()
                                                : nullptr;
                                        if (!LinkedPin || !LinkedNode)
                                        {
                                            continue;
                                        }
                                        ++StateGraphLinkCount;
                                        AddBounded(Result, FString::Printf(
                                            TEXT("StateGraphLink Machine=%s State=%s SourceNodeGuid=%s SourcePinGuid=%s SourcePinName=%s TargetNodeGuid=%s TargetPinGuid=%s TargetPinName=%s"),
                                            *Machine->GetStateMachineName(),
                                            *State->GetStateName(),
                                            *StateGraphNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *Pin->PinId.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *EncodeStateGraphValue(
                                                Pin->PinName.ToString()),
                                            *LinkedNode->NodeGuid.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *LinkedPin->PinId.ToString(
                                                EGuidFormats::DigitsWithHyphensLower),
                                            *EncodeStateGraphValue(
                                                LinkedPin->PinName.ToString())),
                                            MaxItems);
                                    }
                                }
                            }
                            for (const UEdGraphNode* StateGraphNode : StateGraph->Nodes)
                            {
                                const UAnimGraphNode_SequencePlayer* Player =
                                    Cast<UAnimGraphNode_SequencePlayer>(StateGraphNode);
                                if (!Player)
                                {
                                    continue;
                                }
                                ++StatePlayerCount;
                                const UEdGraphPin* Sink = State->GetPoseSinkPinInsideState();
                                const UEdGraphPin* Pose = Player->FindPin(
                                    TEXT("Pose"), EGPD_Output);
                                AddBounded(Result, FString::Printf(
                                    TEXT("StatePlayer Machine=%s State=%s Guid=%s Asset=%s PlayRate=%.6f Loop=%s Position=(%d,%d) PoseLinked=%s"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *Player->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
                                    Player->Node.GetSequence()
                                        ? *Player->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    Player->Node.GetPlayRate(),
                                    Player->Node.IsLooping() ? TEXT("true") : TEXT("false"),
                                    Player->NodePosX,
                                    Player->NodePosY,
                                    Sink && Pose && Sink->LinkedTo.Contains(Pose)
                                        ? TEXT("true") : TEXT("false")),
                                    MaxItems);
                            }
                            UAnimGraphNode_BlendListByBool* BoolBlend = nullptr;
                            UAnimGraphNode_SequencePlayer* FalsePlayer = nullptr;
                            UAnimGraphNode_SequencePlayer* TruePlayer = nullptr;
                            UK2Node_VariableGet* BoolGetter = nullptr;
                            if (GetStateBoolBlendParts(
                                    const_cast<UAnimStateNode*>(State),
                                    BoolBlend,
                                    FalsePlayer,
                                    TruePlayer,
                                    BoolGetter))
                            {
                                ++StateBoolBlendCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateBoolBlend Machine=%s State=%s Guid=%s Variable=%s FalseAsset=%s FalseRate=%.6f TrueAsset=%s TrueRate=%.6f BlendTime=%.6f PoseLinked=true"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *BoolBlend->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphens),
                                    *BoolGetter->GetVarName().ToString(),
                                    FalsePlayer->Node.GetSequence()
                                        ? *FalsePlayer->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    FalsePlayer->Node.GetPlayRate(),
                                    TruePlayer->Node.GetSequence()
                                        ? *TruePlayer->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    TruePlayer->Node.GetPlayRate(),
                                    GetBoolBlendTime(BoolBlend)),
                                    MaxItems);
                            }
                            UAnimGraphNode_BlendListByInt* IntBlend = nullptr;
                            TArray<UAnimGraphNode_SequencePlayer*> IntBlendPlayers;
                            UK2Node_VariableGet* IndexGetter = nullptr;
                            if (GetStateIntBlendParts(
                                    const_cast<UAnimStateNode*>(State),
                                    IntBlend,
                                    IntBlendPlayers,
                                    IndexGetter))
                            {
                                ++StateIntBlendCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateIntBlend Machine=%s State=%s Guid=%s PoseCount=%d IndexVariable=%s PoseLinked=true"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *IntBlend->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphens),
                                    IntBlendPlayers.Num(),
                                    *IndexGetter->GetVarName().ToString()),
                                    MaxItems);
                                for (int32 PoseIndex = 0;
                                     PoseIndex < IntBlendPlayers.Num();
                                     ++PoseIndex)
                                {
                                    UAnimGraphNode_SequencePlayer* PosePlayer =
                                        IntBlendPlayers[PoseIndex];
                                    AddBounded(Result, FString::Printf(
                                        TEXT("StateIntBlendPose Machine=%s State=%s Index=%d Asset=%s PlayRate=%.6f Loop=%s BlendTime=%.6f Linked=true"),
                                        *Machine->GetStateMachineName(),
                                        *State->GetStateName(),
                                        PoseIndex,
                                        PosePlayer->Node.GetSequence()
                                            ? *PosePlayer->Node.GetSequence()->GetPathName()
                                            : TEXT("None"),
                                        PosePlayer->Node.GetPlayRate(),
                                        PosePlayer->Node.IsLooping()
                                            ? TEXT("true") : TEXT("false"),
                                        GetIntBlendTime(IntBlend, PoseIndex)),
                                    MaxItems);
                                }
                            }
                            UAnimGraphNode_ApplyAdditive* ApplyAdditive = nullptr;
                            UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
                            UAnimGraphNode_SequencePlayer* AdditivePlayer = nullptr;
                            UK2Node_VariableGet* AlphaGetter = nullptr;
                            if (GetStateApplyAdditiveParts(
                                    const_cast<UAnimStateNode*>(State),
                                    ApplyAdditive,
                                    BasePlayer,
                                    AdditivePlayer,
                                    AlphaGetter))
                            {
                                ++StateApplyAdditiveCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateApplyAdditive Machine=%s State=%s Guid=%s BaseAsset=%s BaseRate=%.6f AdditiveAsset=%s AdditiveRate=%.6f AlphaVariable=%s Loop=%s PoseLinked=true"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *ApplyAdditive->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphens),
                                    BasePlayer->Node.GetSequence()
                                        ? *BasePlayer->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    BasePlayer->Node.GetPlayRate(),
                                    AdditivePlayer->Node.GetSequence()
                                        ? *AdditivePlayer->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    AdditivePlayer->Node.GetPlayRate(),
                                    *AlphaGetter->GetVarName().ToString(),
                                    BasePlayer->Node.IsLooping()
                                        && AdditivePlayer->Node.IsLooping()
                                        ? TEXT("true") : TEXT("false")),
                                    MaxItems);
                            }
                            UAnimGraphNode_LayeredBoneBlend* LayeredBlend = nullptr;
                            UAnimGraphNode_SequencePlayer* LayeredBasePlayer = nullptr;
                            TArray<UAnimGraphNode_SequencePlayer*> LayeredPlayers;
                            TArray<UK2Node_VariableGet*> LayeredAlphaGetters;
                            if (GetStateLayeredBlendPerBoneParts(
                                    const_cast<UAnimStateNode*>(State),
                                    LayeredBlend,
                                    LayeredBasePlayer,
                                    LayeredPlayers,
                                    LayeredAlphaGetters))
                            {
                                ++StateLayeredBlendPerBoneCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateLayeredBlendPerBone Machine=%s State=%s Guid=%s BaseAsset=%s BaseRate=%.6f LayerCount=%d MeshRotation=%s MeshScale=%s RootMotionRootBone=%s PoseLinked=true"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *LayeredBlend->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphens),
                                    LayeredBasePlayer->Node.GetSequence()
                                        ? *LayeredBasePlayer->Node.GetSequence()->GetPathName()
                                        : TEXT("None"),
                                    LayeredBasePlayer->Node.GetPlayRate(),
                                    LayeredPlayers.Num(),
                                    LayeredBlend->Node.bMeshSpaceRotationBlend
                                        ? TEXT("true") : TEXT("false"),
                                    LayeredBlend->Node.bMeshSpaceScaleBlend
                                        ? TEXT("true") : TEXT("false"),
                                    LayeredBlend->Node.bBlendRootMotionBasedOnRootBone
                                        ? TEXT("true") : TEXT("false")),
                                    MaxItems);
                                for (int32 LayerIndex = 0;
                                     LayerIndex < LayeredPlayers.Num();
                                     ++LayerIndex)
                                {
                                    const FBranchFilter& Filter = LayeredBlend
                                        ->Node.LayerSetup[LayerIndex]
                                        .BranchFilters[0];
                                    AddBounded(Result, FString::Printf(
                                        TEXT("StateLayeredBlendLayer Machine=%s State=%s Index=%d Asset=%s PlayRate=%.6f AlphaVariable=%s Bone=%s BlendDepth=%d Loop=%s Linked=true"),
                                        *Machine->GetStateMachineName(),
                                        *State->GetStateName(),
                                        LayerIndex,
                                        LayeredPlayers[LayerIndex]->Node.GetSequence()
                                            ? *LayeredPlayers[LayerIndex]
                                                ->Node.GetSequence()->GetPathName()
                                            : TEXT("None"),
                                        LayeredPlayers[LayerIndex]
                                            ->Node.GetPlayRate(),
                                        *LayeredAlphaGetters[LayerIndex]
                                            ->GetVarName().ToString(),
                                        *Filter.BoneName.ToString(),
                                        Filter.BlendDepth,
                                        LayeredPlayers[LayerIndex]
                                            ->Node.IsLooping()
                                            ? TEXT("true") : TEXT("false")),
                                        MaxItems);
                                }
                            }
                            UAnimGraphNode_BlendSpacePlayer* BlendSpacePlayer = nullptr;
                            UK2Node_VariableGet* XGetter = nullptr;
                            UK2Node_VariableGet* YGetter = nullptr;
                            bool bOneDimensional = false;
                            if (GetStateBlendSpaceParts(
                                    const_cast<UAnimStateNode*>(State),
                                    BlendSpacePlayer,
                                    XGetter,
                                    YGetter,
                                    bOneDimensional))
                            {
                                ++StateBlendSpacePlayerCount;
                                AddBounded(Result, FString::Printf(
                                    TEXT("StateBlendSpacePlayer Machine=%s State=%s Guid=%s Asset=%s Dimension=%s XVariable=%s YVariable=%s PlayRate=%.6f Loop=%s PoseLinked=true"),
                                    *Machine->GetStateMachineName(),
                                    *State->GetStateName(),
                                    *BlendSpacePlayer->NodeGuid.ToString(
                                        EGuidFormats::DigitsWithHyphens),
                                    BlendSpacePlayer->GetAnimationAsset()
                                        ? *BlendSpacePlayer->GetAnimationAsset()->GetPathName()
                                        : TEXT("None"),
                                    bOneDimensional ? TEXT("1D") : TEXT("2D"),
                                    *XGetter->GetVarName().ToString(),
                                    YGetter ? *YGetter->GetVarName().ToString() : TEXT("None"),
                                    BlendSpacePlayer->Node.GetPlayRate(),
                                    BlendSpacePlayer->Node.IsLooping()
                                        ? TEXT("true") : TEXT("false")),
                                    MaxItems);
                            }
                        }
                    }
                    else if (UAnimStateTransitionNode* Transition =
                                 Cast<UAnimStateTransitionNode>(Node))
                    {
                        ++MachineTransitionCount;
                        const UAnimStateNodeBase* Previous = Transition->GetPreviousState();
                        const UAnimStateNodeBase* Next = Transition->GetNextState();
                        FString RuleDefault = TEXT("None");
                        FString RuleVariable = TEXT("None");
                        FString RuleGetter = TEXT("None");
                        FString RuleComparison = TEXT("None");
                        FString RuleThreshold = TEXT("None");
                        FString RuleGuard = TEXT("None");
                        FString RuleLogical = TEXT("None");
                        FString RuleExpression = TEXT("None");
                        int32 RuleExpressionTokens = 0;
                        int32 RuleLinked = 0;
                        int32 RuleNodeCount = 0;
                        if (const UAnimationTransitionGraph* TransitionGraph =
                                Cast<UAnimationTransitionGraph>(Transition->BoundGraph))
                        {
                            RuleNodeCount = TransitionGraph->Nodes.Num();
                            if (const UAnimGraphNode_TransitionResult* RuleResult =
                                    TransitionGraph->MyResultNode)
                            {
                                if (const UEdGraphPin* RulePin = RuleResult->FindPin(
                                        TEXT("bCanEnterTransition"), EGPD_Input))
                                {
                                    RuleDefault = RulePin->DefaultValue;
                                    RuleLinked = RulePin->LinkedTo.Num();
                                    ++TransitionRuleCount;
                                    if (UK2Node_VariableGet* Getter =
                                            FindTransitionRuleVariableGetter(Transition))
                                    {
                                        RuleVariable = Getter->GetVarName().ToString();
                                        ++TransitionVariableRuleCount;
                                    }
                                    FTransitionTimeRemainingRuleParts TimeParts;
                                    if (GetTransitionTimeRemainingRuleParts(
                                            Transition, TimeParts))
                                    {
                                        ++TransitionTimeRemainingRuleCount;
                                        RuleGetter = TransitionGetterTypeName(
                                            TimeParts.TimeGetter->GetterType);
                                        RuleComparison = TransitionComparisonName(
                                            TimeParts.Comparison
                                                ->FunctionReference.GetMemberName());
                                        RuleGuard = TimeParts.GuardGetter
                                            ->GetVarName().ToString();
                                        RuleLogical = TransitionLogicalName(
                                            TimeParts.Logical
                                                ->FunctionReference.GetMemberName());
                                        if (const UEdGraphPin* ThresholdPin =
                                                FindNamedPin(
                                                    TimeParts.Comparison,
                                                    EGPD_Input,
                                                    TEXT("B")))
                                        {
                                            RuleThreshold =
                                                ThresholdPin->DefaultValue;
                                        }
                                    }
                                    FTransitionExpressionRuleParts ExpressionParts;
                                    if (GetTransitionExpressionRuleParts(
                                            Transition,
                                            Blueprint,
                                            ExpressionParts))
                                    {
                                        ++TransitionExpressionRuleCount;
                                        RuleExpression =
                                            ExpressionParts.Canonical;
                                        RuleExpressionTokens =
                                            ExpressionParts.Tokens.Num();
                                    }
                                }
                            }
                        }
                        AddBounded(Result, FString::Printf(
                            TEXT("Transition Machine=%s From=%s To=%s Guid=%s Duration=%.6f Priority=%d Automatic=%s Enabled=%s RuleDefault=%s RuleVariable=%s RuleGetter=%s RuleComparison=%s RuleThreshold=%s RuleGuard=%s RuleLogical=%s RuleExpression=%s RuleExpressionTokens=%d RuleLinked=%d RuleNodes=%d"),
                            *Machine->GetStateMachineName(),
                            Previous ? *Previous->GetStateName() : TEXT("None"),
                            Next ? *Next->GetStateName() : TEXT("None"),
                            *Transition->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
                            Transition->CrossfadeDuration,
                            Transition->PriorityOrder,
                            Transition->bAutomaticRuleBasedOnSequencePlayerInState ? TEXT("true") : TEXT("false"),
                            Transition->bDisabled ? TEXT("false") : TEXT("true"),
                            *RuleDefault,
                            *RuleVariable,
                            *RuleGetter,
                            *RuleComparison,
                            *RuleThreshold,
                            *RuleGuard,
                            *RuleLogical,
                            *RuleExpression,
                            RuleExpressionTokens,
                            RuleLinked,
                            RuleNodeCount),
                            MaxItems);
                    }
                }
                StateCount += MachineStateCount;
                TransitionCount += MachineTransitionCount;
                AddBounded(Result, FString::Printf(
                    TEXT("StateMachine Name=%s Guid=%s Position=(%d,%d) States=%d Transitions=%d Entry=%s Graph=%s"),
                    *Machine->GetStateMachineName(),
                    *Machine->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
                    Machine->NodePosX,
                    Machine->NodePosY,
                    MachineStateCount,
                    MachineTransitionCount,
                    *EntryState,
                    *Machine->EditorStateMachineGraph->GetPathName()),
                    MaxItems);
            }
            Result.Metrics.Add(FString::Printf(TEXT("StateMachineCount=%d"), Machines.Num()));
            Result.Metrics.Add(FString::Printf(TEXT("StateCount=%d"), StateCount));
            Result.Metrics.Add(FString::Printf(TEXT("TransitionCount=%d"), TransitionCount));
            Result.Metrics.Add(FString::Printf(TEXT("StatePlayerCount=%d"), StatePlayerCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateBoolBlendCount=%d"), StateBoolBlendCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateBlendSpacePlayerCount=%d"),
                StateBlendSpacePlayerCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateIntBlendCount=%d"), StateIntBlendCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateApplyAdditiveCount=%d"), StateApplyAdditiveCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateLayeredBlendPerBoneCount=%d"),
                StateLayeredBlendPerBoneCount));
            Result.Metrics.Add(FString::Printf(TEXT("TransitionRuleCount=%d"), TransitionRuleCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("TransitionVariableRuleCount=%d"), TransitionVariableRuleCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("TransitionTimeRemainingRuleCount=%d"),
                TransitionTimeRemainingRuleCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("TransitionExpressionRuleCount=%d"),
                TransitionExpressionRuleCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphNodeCount=%d"), StateGraphNodeCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphNodePropertyCount=%d"),
                StateGraphNodePropertyCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphNodeArrayCount=%d"),
                StateGraphNodeArrayCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphPoseArrayCount=%d"),
                StateGraphPoseArrayCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphConstraintArrayCount=%d"),
                StateGraphConstraintArrayCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphPinCount=%d"), StateGraphPinCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphLinkCount=%d"), StateGraphLinkCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("AvailableStateGraphNodeClassCount=%d"),
                AvailableStateGraphNodeClasses.Num()));
            TArray<FString> StateGraphPropertyGapKinds;
            StateGraphPropertyGapCounts.GetKeys(
                StateGraphPropertyGapKinds);
            StateGraphPropertyGapKinds.Sort();
            int32 StateGraphPropertyGapCount = 0;
            for (const FString& Kind : StateGraphPropertyGapKinds)
            {
                StateGraphPropertyGapCount +=
                    StateGraphPropertyGapCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphPropertyGap.%s=%d"),
                    *Kind,
                    StateGraphPropertyGapCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphPropertyGapCount=%d"),
                StateGraphPropertyGapCount));
            TArray<FString> StateGraphTopologyPropertyKinds;
            StateGraphTopologyPropertyCounts.GetKeys(
                StateGraphTopologyPropertyKinds);
            StateGraphTopologyPropertyKinds.Sort();
            int32 StateGraphTopologyPropertyCount = 0;
            for (const FString& Kind : StateGraphTopologyPropertyKinds)
            {
                StateGraphTopologyPropertyCount +=
                    StateGraphTopologyPropertyCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphTopologyProperty.%s=%d"),
                    *Kind,
                    StateGraphTopologyPropertyCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphTopologyPropertyCount=%d"),
                StateGraphTopologyPropertyCount));
            TArray<FString> StateGraphDedicatedArrayKinds;
            StateGraphDedicatedArrayCounts.GetKeys(
                StateGraphDedicatedArrayKinds);
            StateGraphDedicatedArrayKinds.Sort();
            int32 StateGraphDedicatedArrayCount = 0;
            for (const FString& Kind : StateGraphDedicatedArrayKinds)
            {
                StateGraphDedicatedArrayCount +=
                    StateGraphDedicatedArrayCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphDedicatedArray.%s=%d"),
                    *Kind,
                    StateGraphDedicatedArrayCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphDedicatedArrayCount=%d"),
                StateGraphDedicatedArrayCount));
            TArray<FString> StateGraphCoupledPoseArrayKinds;
            StateGraphCoupledPoseArrayCounts.GetKeys(
                StateGraphCoupledPoseArrayKinds);
            StateGraphCoupledPoseArrayKinds.Sort();
            int32 StateGraphCoupledPoseArrayPropertyCount = 0;
            for (const FString& Kind : StateGraphCoupledPoseArrayKinds)
            {
                StateGraphCoupledPoseArrayPropertyCount +=
                    StateGraphCoupledPoseArrayCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphCoupledPoseArray.%s=%d"),
                    *Kind,
                    StateGraphCoupledPoseArrayCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphCoupledPoseArrayPropertyCount=%d"),
                StateGraphCoupledPoseArrayPropertyCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphCoupledPoseArrayLifecycleCount=%d"),
                StateGraphCoupledPoseArrayKinds.Num()));
            TArray<FString> StateGraphCoupledConstraintArrayKinds;
            StateGraphCoupledConstraintArrayCounts.GetKeys(
                StateGraphCoupledConstraintArrayKinds);
            StateGraphCoupledConstraintArrayKinds.Sort();
            int32 StateGraphCoupledConstraintArrayPropertyCount = 0;
            for (const FString& Kind :
                 StateGraphCoupledConstraintArrayKinds)
            {
                StateGraphCoupledConstraintArrayPropertyCount +=
                    StateGraphCoupledConstraintArrayCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphCoupledConstraintArray.%s=%d"),
                    *Kind,
                    StateGraphCoupledConstraintArrayCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphCoupledConstraintArrayPropertyCount=%d"),
                StateGraphCoupledConstraintArrayPropertyCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphCoupledConstraintArrayLifecycleCount=%d"),
                StateGraphCoupledConstraintArrayKinds.Num()));
            TArray<FString> StateGraphAssetBoundArrayKinds;
            StateGraphAssetBoundArrayCounts.GetKeys(
                StateGraphAssetBoundArrayKinds);
            StateGraphAssetBoundArrayKinds.Sort();
            int32 StateGraphAssetBoundArrayPropertyCount = 0;
            for (const FString& Kind : StateGraphAssetBoundArrayKinds)
            {
                StateGraphAssetBoundArrayPropertyCount +=
                    StateGraphAssetBoundArrayCounts.FindChecked(Kind);
                Result.Metrics.Add(FString::Printf(
                    TEXT("StateGraphAssetBoundArray.%s=%d"),
                    *Kind,
                    StateGraphAssetBoundArrayCounts.FindChecked(Kind)));
            }
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphAssetBoundArrayPropertyCount=%d"),
                StateGraphAssetBoundArrayPropertyCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphAssetBoundArrayLifecycleCount=%d"),
                StateGraphAssetBoundArrayKinds.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphWholeArrayPropertyCount=%d"),
                StateGraphWholeArrayPropertyCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("StateGraphResolvedStructuralArrayCount=%d"),
                StateGraphResolvedStructuralArrays.Num()));
            for (const UClass* NodeClass : AvailableStateGraphNodeClasses)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("StateGraphNodeClass Path=%s"),
                    NodeClass ? *NodeClass->GetPathName() : TEXT("None")),
                    MaxItems);
            }
            for (const FString& Gap : StateGraphPropertyGaps)
            {
                AddBounded(Result, Gap, MaxItems);
            }
            AddBounded(Result,
                TEXT("Use InspectBlueprint for variables, event graphs, pins, and generic Blueprint structure."),
                MaxItems);
        }
        else if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
        {
            Result.AssetKind = TEXT("skeletal_mesh");
            Result.Metrics = {
                FString::Printf(TEXT("Skeleton=%s"), Mesh->GetSkeleton() ? *Mesh->GetSkeleton()->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("LODCount=%d"), Mesh->GetLODNum()),
                FString::Printf(TEXT("BoneCount=%d"), Mesh->GetRefSkeleton().GetNum()),
                FString::Printf(TEXT("MaterialSlotCount=%d"), Mesh->GetMaterials().Num())
            };
        }
        else if (const USkeleton* Skeleton = Cast<USkeleton>(Asset))
        {
            Result.AssetKind = TEXT("skeleton");
            Result.Metrics = {
                FString::Printf(TEXT("BoneCount=%d"), Skeleton->GetReferenceSkeleton().GetNum()),
                FString::Printf(TEXT("SocketCount=%d"), Skeleton->Sockets.Num())
            };
            for (int32 Index = 0; Index < Skeleton->Sockets.Num(); ++Index)
            {
                if (Skeleton->Sockets[Index])
                {
                    AddBounded(Result, FString::Printf(TEXT("Socket[%d]=%s Bone=%s"), Index,
                        *Skeleton->Sockets[Index]->SocketName.ToString(),
                        *Skeleton->Sockets[Index]->BoneName.ToString()), MaxItems);
                    AddBounded(Result, FString::Printf(TEXT("SocketTransform[%d] Location=%s Rotation=%s Scale=%s"),
                        Index, *Skeleton->Sockets[Index]->RelativeLocation.ToString(),
                        *Skeleton->Sockets[Index]->RelativeRotation.ToString(),
                        *Skeleton->Sockets[Index]->RelativeScale.ToString()), MaxItems);
                }
            }
        }
        else if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
        {
            Result.AssetKind = TEXT("blend_space");
            Result.Metrics.Add(FString::Printf(TEXT("Skeleton=%s"),
                BlendSpace->GetSkeleton() ? *BlendSpace->GetSkeleton()->GetPathName() : TEXT("None")));
            Result.Metrics.Add(FString::Printf(TEXT("SampleCount=%d"), BlendSpace->GetNumberOfBlendSamples()));
        }
        else if (UControlRigRuntimeAsset* ControlRigAsset =
                     Cast<UControlRigRuntimeAsset>(Asset))
        {
            Result.AssetKind = TEXT("control_rig");
            URigHierarchy* Hierarchy = ControlRigAsset->GetHierarchy();
            if (!Hierarchy)
            {
                Result.Code = TEXT("control_rig_hierarchy_unavailable");
                return Result;
            }
            const TArray<FRigElementKey> Keys =
                Hierarchy->GetAllKeys(true, ERigElementType::All);
            int32 BoneCount = 0;
            int32 ControlCount = 0;
            int32 NullCount = 0;
            int32 CurveCount = 0;
            int32 SocketCount = 0;
            for (const FRigElementKey& Key : Keys)
            {
                BoneCount += Key.Type == ERigElementType::Bone ? 1 : 0;
                ControlCount += Key.Type == ERigElementType::Control ? 1 : 0;
                NullCount += Key.Type == ERigElementType::Null ? 1 : 0;
                CurveCount += Key.Type == ERigElementType::Curve ? 1 : 0;
                SocketCount += Key.Type == ERigElementType::Socket ? 1 : 0;
            }
            const TSoftObjectPtr<USkeletalMesh> PreviewMesh =
                ControlRigAsset->GetPreviewSkeletalMesh();
            Result.Metrics = {
                TEXT("PreviewMesh=")
                    + (PreviewMesh.IsNull()
                        ? TEXT("None")
                        : PreviewMesh.ToSoftObjectPath().GetLongPackageName()),
                FString::Printf(TEXT("ElementCount=%d"), Keys.Num()),
                FString::Printf(TEXT("BoneCount=%d"), BoneCount),
                FString::Printf(TEXT("ControlCount=%d"), ControlCount),
                FString::Printf(TEXT("NullCount=%d"), NullCount),
                FString::Printf(TEXT("CurveCount=%d"), CurveCount),
                FString::Printf(TEXT("SocketCount=%d"), SocketCount)
            };
            int32 LoadedShapeLibraryCount = 0;
            int32 AvailableShapeCount = 0;
            TArray<FString> ShapeLibraryItems;
            GetControlRigShapeLibraryInspection(
                ControlRigAsset,
                LoadedShapeLibraryCount,
                AvailableShapeCount,
                ShapeLibraryItems);
            Result.Metrics.Add(FString::Printf(
                TEXT("ShapeLibraryReferenceCount=%d"),
                ControlRigAsset->GetShapeLibraries().Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("LoadedShapeLibraryCount=%d"),
                LoadedShapeLibraryCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("AvailableShapeCount=%d"), AvailableShapeCount));
            for (const FString& ShapeLibraryItem : ShapeLibraryItems)
            {
                AddBounded(Result, ShapeLibraryItem, MaxItems);
            }
            UControlRigEditorAsset* EditorAsset =
                GetControlRigEditorAsset(ControlRigAsset);
            URigVMFunctionLibrary* LocalFunctionLibrary = EditorAsset
                ? EditorAsset->GetLocalFunctionLibrary() : nullptr;
            TArray<URigVMLibraryNode*> LocalFunctions = LocalFunctionLibrary
                ? LocalFunctionLibrary->GetFunctions()
                : TArray<URigVMLibraryNode*>();
            LocalFunctions.RemoveAll([](const URigVMLibraryNode* Function)
            {
                return Function == nullptr;
            });
            LocalFunctions.Sort([](
                const URigVMLibraryNode& A,
                const URigVMLibraryNode& B)
            {
                return A.GetFName().LexicalLess(B.GetFName());
            });
            TArray<URigVMGraph*> RigVMModels =
                CollectControlRigModels(EditorAsset);
            RigVMModels.Sort([](
                const URigVMGraph& A,
                const URigVMGraph& B)
            {
                return RigVMGraphIdentifier(&A)
                    < RigVMGraphIdentifier(&B);
            });
            int32 RigVMNodeCount = 0;
            int32 RigVMLinkCount = 0;
            int32 RigVMCollapseNodeCount = 0;
            int32 RigVMLocalVariableCount = 0;
            int32 RigVMFunctionPinCount = 0;
            int32 RigVMFunctionReferenceCount = 0;
            int32 RigVMExternalFunctionReferenceCount = 0;
            const FString CurrentControlRigPath =
                ControlRigAsset->GetOutermost()->GetName();
            for (const URigVMGraph* Model : RigVMModels)
            {
                RigVMNodeCount += Model->GetNodes().Num();
                RigVMLinkCount += Model->GetLinks().Num();
                RigVMLocalVariableCount +=
                    Model->GetLocalVariables(false).Num();
                for (const URigVMNode* Node : Model->GetNodes())
                {
                    const URigVMCollapseNode* CollapseNode =
                        Cast<URigVMCollapseNode>(Node);
                    if (CollapseNode
                        && !CollapseNode->IsGraphFunctionDefinition())
                    {
                        ++RigVMCollapseNodeCount;
                    }
                    const URigVMFunctionReferenceNode* FunctionReference =
                        Cast<URigVMFunctionReferenceNode>(Node);
                    if (FunctionReference)
                    {
                        ++RigVMFunctionReferenceCount;
                        const FString FunctionHostPath = NormalizePath(
                            FunctionReference->GetReferencedFunctionHeader()
                                .LibraryPointer.HostObject.ToString());
                        if (FunctionHostPath != CurrentControlRigPath)
                        {
                            ++RigVMExternalFunctionReferenceCount;
                        }
                    }
                }
            }
            for (URigVMLibraryNode* Function : LocalFunctions)
            {
                if (!Function)
                {
                    continue;
                }
                for (const URigVMPin* Pin : Function->GetPins())
                {
                    if (Pin && (Pin->GetDirection()
                            == ERigVMPinDirection::Input
                        || Pin->GetDirection()
                            == ERigVMPinDirection::Output))
                    {
                        ++RigVMFunctionPinCount;
                    }
                }
            }
            Result.Metrics.Add(TEXT("RigVMEditorAsset=")
                + FString(EditorAsset ? TEXT("Available") : TEXT("Unavailable")));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMGraphCount=%d"), RigVMModels.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMNodeCount=%d"), RigVMNodeCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMLinkCount=%d"), RigVMLinkCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMCollapseNodeCount=%d"),
                RigVMCollapseNodeCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMLocalFunctionCount=%d"),
                LocalFunctions.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMLocalVariableCount=%d"),
                RigVMLocalVariableCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMFunctionPinCount=%d"),
                RigVMFunctionPinCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMFunctionReferenceCount=%d"),
                RigVMFunctionReferenceCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMExternalFunctionReferenceCount=%d"),
                RigVMExternalFunctionReferenceCount));
            TArray<FRigVMGraphVariableDescription> MemberVariables =
                EditorAsset
                    ? EditorAsset->GetAssetVariables()
                    : TArray<FRigVMGraphVariableDescription>();
            MemberVariables.Sort([](
                const FRigVMGraphVariableDescription& A,
                const FRigVMGraphVariableDescription& B)
            {
                return A.Name.LexicalLess(B.Name);
            });
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMMemberVariableCount=%d"),
                MemberVariables.Num()));
            Result.Metrics.Add(TEXT("RigVMRuntimeAsset=Available"));
            for (URigVMLibraryNode* Function : LocalFunctions)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("ControlRigFunction Name=%s State=%s"),
                    *Function->GetName(),
                    *RigVMLocalFunctionStateString(
                        Function, ControlRigAsset, EditorAsset)), MaxItems);
                for (const URigVMPin* Pin : Function->GetPins())
                {
                    if (!Pin || (Pin->GetDirection()
                            != ERigVMPinDirection::Input
                        && Pin->GetDirection()
                            != ERigVMPinDirection::Output))
                    {
                        continue;
                    }
                    AddBounded(Result, FString::Printf(
                        TEXT("ControlRigFunctionPin Function=%s Name=%s State=%s"),
                        *Function->GetName(),
                        *Pin->GetName(),
                        *RigVMFunctionPinStateString(
                            Function, Pin->GetFName())), MaxItems);
                }
            }
            for (const FRigVMGraphVariableDescription& Variable :
                 MemberVariables)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("ControlRigMemberVariable Name=%s State=%s"),
                    *Variable.Name.ToString(),
                    *RigVMMemberVariableStateString(Variable)), MaxItems);
            }

            TArray<UScriptStruct*> RegisteredUnitStructs =
                URigVMController::GetRegisteredUnitStructs();
            RegisteredUnitStructs.RemoveAll(
                [](const UScriptStruct* ScriptStruct)
                {
                    return ScriptStruct == nullptr;
                });
            RegisteredUnitStructs.Sort([](
                const UScriptStruct& A,
                const UScriptStruct& B)
            {
                return A.GetPathName() < B.GetPathName();
            });
            const TArray<FRegisteredRigVMUnitFunctionSpec>
                RegisteredUnitFunctions =
                    CollectRegisteredRigVMUnitFunctions(
                        RegisteredUnitStructs);
            struct FRegisteredUnitDiscovery
            {
                bool bHasDynamicArray = false;
                bool bHasAggregatePins = false;
                bool bOutsideLegacyModules = false;
                FString Item;
            };
            TArray<FRegisteredUnitDiscovery> UnitDiscoveries;
            TArray<FString> EventDiscoveries;
            int32 AggregateUnitCount = 0;
            for (const FRegisteredRigVMUnitFunctionSpec& FunctionSpec :
                 RegisteredUnitFunctions)
            {
                UScriptStruct* UnitStruct = FunctionSpec.ScriptStruct;
                if (!IsRegisteredControlRigUnitStruct(UnitStruct))
                {
                    continue;
                }
                const TArray<FString> ArrayPins =
                    RigVMStructDynamicArrayPins(UnitStruct);
                const TArray<FString> EditableArrayPins =
                    RigVMStructEditableDynamicArrayPins(UnitStruct);
                const TOptional<FRegisteredRigVMEventSpec> EventSpec =
                    GetRegisteredRigVMEventSpec(UnitStruct);
                const FRigVMStructAggregatePinNames AggregatePins =
                    RigVMStructAggregatePins(UnitStruct);
                FRegisteredUnitDiscovery& Discovery =
                    UnitDiscoveries.AddDefaulted_GetRef();
                Discovery.bHasDynamicArray =
                    !EditableArrayPins.IsEmpty();
                Discovery.bHasAggregatePins = AggregatePins.IsAggregate();
                AggregateUnitCount += Discovery.bHasAggregatePins ? 1 : 0;
                const FString UnitStructPath = UnitStruct->GetPathName();
                Discovery.bOutsideLegacyModules =
                    !UnitStructPath.StartsWith(TEXT("/Script/ControlRig."))
                    && !UnitStructPath.StartsWith(TEXT("/Script/RigVM."));
                Discovery.Item = FString::Printf(
                    TEXT("ControlRigAvailableUnit Struct=%s Method=%s Function=%s Template=%s ArrayPins=%s EditableArrayPins=%s AggregateInputs=%s AggregateOutputs=%s IsAggregate=%s IsEvent=%s EventName=%s CanOnlyExistOnce=%s"),
                    *UnitStruct->GetPathName(),
                    *FunctionSpec.MethodName.ToString(),
                    *FunctionSpec.FunctionName,
                    *URigVMController::GetTemplateForUnitStruct(
                        UnitStruct,
                        *FunctionSpec.MethodName.ToString()),
                    ArrayPins.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(ArrayPins, TEXT(",")),
                    EditableArrayPins.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(
                            EditableArrayPins, TEXT(",")),
                    AggregatePins.Inputs.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(
                            AggregatePins.Inputs, TEXT(",")),
                    AggregatePins.Outputs.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(
                            AggregatePins.Outputs, TEXT(",")),
                    AggregatePins.IsAggregate()
                        ? TEXT("true") : TEXT("false"),
                    EventSpec.IsSet() ? TEXT("true") : TEXT("false"),
                    EventSpec.IsSet()
                        ? *EventSpec->EventName.ToString() : TEXT("None"),
                    EventSpec.IsSet() && EventSpec->bCanOnlyExistOnce
                        ? TEXT("true") : TEXT("false"));
                if (EventSpec.IsSet())
                {
                    EventDiscoveries.AddUnique(FString::Printf(
                        TEXT("ControlRigAvailableEvent Struct=%s Method=%s Function=%s EventName=%s CanOnlyExistOnce=%s"),
                        *UnitStruct->GetPathName(),
                        *FunctionSpec.MethodName.ToString(),
                        *FunctionSpec.FunctionName,
                        *EventSpec->EventName.ToString(),
                        EventSpec->bCanOnlyExistOnce
                            ? TEXT("true") : TEXT("false")));
                }
            }
            UnitDiscoveries.Sort([](
                const FRegisteredUnitDiscovery& A,
                const FRegisteredUnitDiscovery& B)
            {
                if (A.bHasAggregatePins != B.bHasAggregatePins)
                {
                    return A.bHasAggregatePins;
                }
                if (A.bOutsideLegacyModules
                    != B.bOutsideLegacyModules)
                {
                    return A.bOutsideLegacyModules;
                }
                if (A.bHasDynamicArray != B.bHasDynamicArray)
                {
                    return A.bHasDynamicArray;
                }
                return A.Item < B.Item;
            });
            EventDiscoveries.Sort();

            TArray<FString> RegisteredTemplateNotations =
                URigVMController::GetRegisteredTemplates();
            RegisteredTemplateNotations.Sort();
            struct FRegisteredTemplateDiscovery
            {
                bool bHasDynamicArray = false;
                bool bHasAggregatePins = false;
                bool bDispatch = false;
                FString Item;
            };
            TArray<FRegisteredTemplateDiscovery> TemplateDiscoveries;
            TArray<FString> DispatchDiscoveries;
            int32 DispatchTemplateCount = 0;
            int32 GenericTemplateCount = 0;
            int32 AggregateTemplateCount = 0;
            for (const FString& Notation : RegisteredTemplateNotations)
            {
                TArray<UScriptStruct*> UnitStructs =
                    URigVMController::GetUnitStructsForTemplate(
                        FName(*Notation));
                UnitStructs.RemoveAll([](const UScriptStruct* ScriptStruct)
                {
                    return ScriptStruct == nullptr;
                });
                TArray<FString> ArrayPins;
                TArray<FString> EditableArrayPins;
                for (const UScriptStruct* UnitStruct : UnitStructs)
                {
                    for (const FString& ArrayPin :
                         RigVMStructDynamicArrayPins(UnitStruct))
                    {
                        ArrayPins.AddUnique(ArrayPin);
                    }
                    for (const FString& ArrayPin :
                         RigVMStructEditableDynamicArrayPins(UnitStruct))
                    {
                        EditableArrayPins.AddUnique(ArrayPin);
                    }
                }
                ArrayPins.Sort();
                EditableArrayPins.Sort();
                const FRigVMTemplate* RegisteredTemplate =
                    FRigVMRegistry::Get().FindTemplate(
                        FName(*Notation), false, true);
                const bool bDispatch = RegisteredTemplate
                    && RegisteredTemplate->UsesDispatch();
                const FRigVMDispatchFactory* DispatchFactory = bDispatch
                    ? RegisteredTemplate->GetDispatchFactory()
                    : nullptr;
                TArray<FName> AggregateInputArguments;
                TArray<FName> AggregateOutputArguments;
                if (DispatchFactory)
                {
                    FRigVMDispatchFactory* MutableDispatchFactory =
                        const_cast<FRigVMDispatchFactory*>(DispatchFactory);
                    AggregateInputArguments = MutableDispatchFactory
                        ->GetAggregateInputArguments();
                    AggregateOutputArguments = MutableDispatchFactory
                        ->GetAggregateOutputArguments();
                }
                const auto NamesString = [](const TArray<FName>& Names)
                {
                    TArray<FString> Values;
                    Values.Reserve(Names.Num());
                    for (const FName Name : Names)
                    {
                        Values.Add(Name.ToString());
                    }
                    return Values.IsEmpty()
                        ? FString(TEXT("None"))
                        : FString::Join(Values, TEXT(","));
                };
                const bool bGeneric = UnitStructs.IsEmpty() && !bDispatch;
                DispatchTemplateCount += bDispatch ? 1 : 0;
                GenericTemplateCount += bGeneric ? 1 : 0;
                FRegisteredTemplateDiscovery& Discovery =
                    TemplateDiscoveries.AddDefaulted_GetRef();
                Discovery.bHasDynamicArray =
                    !EditableArrayPins.IsEmpty();
                Discovery.bHasAggregatePins =
                    !AggregateInputArguments.IsEmpty()
                    || !AggregateOutputArguments.IsEmpty();
                AggregateTemplateCount +=
                    Discovery.bHasAggregatePins ? 1 : 0;
                Discovery.bDispatch = bDispatch;
                Discovery.Item = FString::Printf(
                    TEXT("ControlRigAvailableTemplate Notation=%s Kind=%s DispatchFactory=%s UnitStructs=%d ArrayPins=%s EditableArrayPins=%s AggregateInputs=%s AggregateOutputs=%s"),
                    *Notation,
                    bDispatch
                        ? TEXT("Dispatch")
                        : bGeneric
                            ? TEXT("Generic")
                            : TEXT("UnitTemplate"),
                    DispatchFactory
                        ? *DispatchFactory->GetFactoryName().ToString()
                        : TEXT("None"),
                    UnitStructs.Num(),
                    ArrayPins.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(ArrayPins, TEXT(",")),
                    EditableArrayPins.IsEmpty()
                        ? TEXT("None")
                        : *FString::Join(
                            EditableArrayPins, TEXT(",")),
                    *NamesString(AggregateInputArguments),
                    *NamesString(AggregateOutputArguments));
                if (bDispatch)
                {
                    DispatchDiscoveries.Add(FString::Printf(
                        TEXT("ControlRigAvailableDispatch Notation=%s Factory=%s AggregateInputs=%s AggregateOutputs=%s"),
                        *Notation,
                        DispatchFactory
                            ? *DispatchFactory->GetFactoryName().ToString()
                            : TEXT("None"),
                        *NamesString(AggregateInputArguments),
                        *NamesString(AggregateOutputArguments)));
                }
            }
            TemplateDiscoveries.Sort([](
                const FRegisteredTemplateDiscovery& A,
                const FRegisteredTemplateDiscovery& B)
            {
                if (A.bHasAggregatePins != B.bHasAggregatePins)
                {
                    return A.bHasAggregatePins;
                }
                if (A.bHasDynamicArray != B.bHasDynamicArray)
                {
                    return A.bHasDynamicArray;
                }
                if (A.bDispatch != B.bDispatch)
                {
                    return A.bDispatch;
                }
                return A.Item < B.Item;
            });
            DispatchDiscoveries.Sort();
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredUnitCount=%d"),
                UnitDiscoveries.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredUnitStructCount=%d"),
                RegisteredUnitStructs.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredUnitFunctionCount=%d"),
                RegisteredUnitFunctions.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredAggregateUnitCount=%d"),
                AggregateUnitCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredEventCount=%d"),
                EventDiscoveries.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredTemplateCount=%d"),
                TemplateDiscoveries.Num()));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredAggregateTemplateCount=%d"),
                AggregateTemplateCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredDispatchOrGenericTemplateCount=%d"),
                DispatchTemplateCount + GenericTemplateCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredDispatchTemplateCount=%d"),
                DispatchTemplateCount));
            Result.Metrics.Add(FString::Printf(
                TEXT("RigVMRegisteredGenericTemplateCount=%d"),
                GenericTemplateCount));
            const int32 RegistryItemLimit = 64;
            for (const FString& EventDiscovery : EventDiscoveries)
            {
                AddBounded(Result, EventDiscovery, MaxItems);
            }
            for (int32 Index = 0;
                 Index < UnitDiscoveries.Num()
                    && Index < RegistryItemLimit;
                 ++Index)
            {
                AddBounded(Result, UnitDiscoveries[Index].Item, MaxItems);
            }
            for (int32 Index = 0;
                 Index < TemplateDiscoveries.Num()
                    && Index < RegistryItemLimit;
                 ++Index)
            {
                AddBounded(Result, TemplateDiscoveries[Index].Item, MaxItems);
            }
            for (const FString& DispatchDiscovery : DispatchDiscoveries)
            {
                AddBounded(Result, DispatchDiscovery, MaxItems);
            }
            for (URigVMGraph* Model : RigVMModels)
            {
                const FString GraphIdentifier =
                    RigVMGraphIdentifier(Model);
                AddBounded(Result, FString::Printf(
                    TEXT("ControlRigGraph Name=%s NodeCount=%d LinkCount=%d LocalVariableCount=%d State=%s"),
                    *GraphIdentifier,
                    Model->GetNodes().Num(),
                    Model->GetLinks().Num(),
                    Model->GetLocalVariables(false).Num(),
                    *RigVMTopLevelGraphStateString(Model)), MaxItems);

                const TArray<FRigVMGraphVariableDescription> LocalVariables =
                    Model->GetLocalVariables(false);
                for (int32 VariableIndex = 0;
                     VariableIndex < LocalVariables.Num();
                     ++VariableIndex)
                {
                    const FRigVMGraphVariableDescription& Variable =
                        LocalVariables[VariableIndex];
                    AddBounded(Result, FString::Printf(
                        TEXT("ControlRigLocalVariable Graph=%s Name=%s State=%s"),
                        *GraphIdentifier,
                        *Variable.Name.ToString(),
                        *RigVMLocalVariableStateString(
                            Variable, VariableIndex)), MaxItems);
                }

                TArray<URigVMNode*> Nodes = Model->GetNodes();
                Nodes.Sort([](const URigVMNode& A, const URigVMNode& B)
                {
                    return A.GetFName().LexicalLess(B.GetFName());
                });
                for (URigVMNode* Node : Nodes)
                {
                    if (!Node)
                    {
                        continue;
                    }
                    AddBounded(Result, FString::Printf(
                        TEXT("ControlRigNode Graph=%s Name=%s State=%s"),
                        *GraphIdentifier,
                        *Node->GetName(),
                        *RigVMNodeStateString(Node)), MaxItems);
                    TFunction<void(URigVMPin*)> AddPin =
                        [&](URigVMPin* Pin)
                    {
                        if (!Pin)
                        {
                            return;
                        }
                        AddBounded(Result, FString::Printf(
                            TEXT("ControlRigPin Graph=%s Path=%s Direction=%s CPPType=%s Default=%s SourceLinks=%d TargetLinks=%d IsDynamicArray=%s IsFixedSizeArray=%s IsWildCard=%s ArraySize=%d TypeState=%s ArrayState=%s"),
                            *GraphIdentifier,
                            *Pin->GetPinPath(false),
                            *RigVMPinDirectionString(Pin->GetDirection()),
                            *Pin->GetCPPType(),
                            *Pin->GetDefaultValue(),
                            Pin->GetSourceLinks(true).Num(),
                            Pin->GetTargetLinks(true).Num(),
                            Pin->IsDynamicArray() ? TEXT("true") : TEXT("false"),
                            Pin->IsFixedSizeArray() ? TEXT("true") : TEXT("false"),
                            IsRigVMWildcardPin(Pin)
                                ? TEXT("true") : TEXT("false"),
                            Pin->IsDynamicArray()
                                ? Pin->GetSubPins().Num() : INDEX_NONE,
                            *RigVMPinTypeStateString(Pin),
                            *RigVMArrayPinStateString(Pin)), MaxItems);
                        for (URigVMPin* SubPin : Pin->GetSubPins())
                        {
                            AddPin(SubPin);
                        }
                    };
                    for (URigVMPin* Pin : Node->GetPins())
                    {
                        AddPin(Pin);
                    }
                }

                TArray<URigVMLink*> Links = Model->GetLinks();
                Links.Sort([](const URigVMLink& A, const URigVMLink& B)
                {
                    return A.GetPinPathRepresentation()
                        < B.GetPinPathRepresentation();
                });
                for (const URigVMLink* Link : Links)
                {
                    if (Link)
                    {
                        AddBounded(Result, FString::Printf(
                            TEXT("ControlRigLink Graph=%s Source=%s Target=%s"),
                            *GraphIdentifier,
                            *Link->GetSourcePinPath(),
                            *Link->GetTargetPinPath()), MaxItems);
                    }
                }
            }
            const UEnum* ElementTypeEnum = StaticEnum<ERigElementType>();
            const UEnum* ControlTypeEnum = StaticEnum<ERigControlType>();
            URigHierarchyController* HierarchyController =
                Hierarchy->GetController(true);
            for (const FRigElementKey& Key : Keys)
            {
                const FRigElementKey Parent = Hierarchy->GetFirstParent(Key);
                const FString Metadata =
                    ControlRigMetadataListString(Hierarchy, Key);
                FString ValueSuffix;
                if (Key.Type == ERigElementType::Curve)
                {
                    ValueSuffix += TEXT(" Value=")
                        + FString::SanitizeFloat(
                            Hierarchy->GetCurveValue(Key));
                }
                else if (Key.Type == ERigElementType::Control
                    && HierarchyController)
                {
                    const FRigControlSettings Settings =
                        HierarchyController->GetControlSettings(Key);
                    ValueSuffix += TEXT(" ControlType=")
                        + (ControlTypeEnum
                            ? ControlTypeEnum->GetNameStringByValue(
                                static_cast<int64>(
                                    Settings.ControlType))
                            : TEXT("Unknown"));
                    FRigControlElement* ControlElement =
                        Hierarchy->Find<FRigControlElement>(Key);
                    ValueSuffix += TEXT(" AvailableSpaces=")
                        + ControlRigAvailableSpacesString(Settings);
                    ValueSuffix += TEXT(" ShapeSettings=")
                        + ControlRigControlShapeSettingsString(Settings);
                    ValueSuffix += TEXT(" LimitSettings=")
                        + ControlRigControlLimitSettingsString(Settings);
                    if (ControlElement)
                    {
                        ValueSuffix += TEXT(" ShapeInitialLocal=")
                            + Hierarchy->GetControlShapeTransform(
                                ControlElement,
                                ERigTransformType::InitialLocal).ToString();
                        ValueSuffix += TEXT(" ShapeCurrentLocal=")
                            + Hierarchy->GetControlShapeTransform(
                                ControlElement,
                                ERigTransformType::CurrentLocal).ToString();
                    }
                    const FString InitialValue =
                        ControlRigControlValueString(
                            Hierarchy,
                            Key,
                            Settings.ControlType,
                            ERigControlValueType::Initial);
                    const FString CurrentValue =
                        ControlRigControlValueString(
                            Hierarchy,
                            Key,
                            Settings.ControlType,
                            ERigControlValueType::Current);
                    if (InitialValue != TEXT("Unsupported")
                        && CurrentValue != TEXT("Unsupported"))
                    {
                        ValueSuffix += TEXT(" InitialValue=")
                            + InitialValue;
                        ValueSuffix += TEXT(" CurrentValue=")
                            + CurrentValue;
                    }
                }
                else if (Key.Type == ERigElementType::Socket)
                {
                    ValueSuffix += TEXT(" SocketState=")
                        + ControlRigSocketStateString(Hierarchy, Key);
                }
                AddBounded(Result, FString::Printf(
                    TEXT("ControlRigElement Name=%s Type=%s Parent=%s Metadata=%s InitialLocal=%s%s"),
                    *Key.Name.ToString(),
                    ElementTypeEnum
                        ? *ElementTypeEnum->GetNameStringByValue(
                            static_cast<int64>(Key.Type))
                        : TEXT("Unknown"),
                    Parent.IsValid() ? *Parent.Name.ToString() : TEXT("None"),
                    *Metadata,
                    *Hierarchy->GetLocalTransform(Key, true).ToString(),
                    *ValueSuffix),
                    MaxItems);
            }
        }
        else if (const UIKRetargeter* Retargeter =
                     Cast<UIKRetargeter>(Asset))
        {
            Result.AssetKind = TEXT("ik_retargeter");
            UIKRetargeterController* RetargetController =
                UIKRetargeterController::GetController(Retargeter);
            if (!RetargetController)
            {
                Result.Code = TEXT("ik_retargeter_controller_unavailable");
                return Result;
            }
            const TArray<FString> OpTypePaths =
                AvailableIKRetargetOpTypePaths();
            const TMap<FName, FRetargetOverrideSet>& OverrideSets =
                Retargeter->GetOverrideSets();
            const int32 ActiveOverrideSetCount =
                Retargeter->GetOverrideSetsToApply().Num();
            const TMap<FName, FIKRetargetPose>& SourcePoses =
                RetargetController->GetRetargetPoses(
                    ERetargetSourceOrTarget::Source);
            const TMap<FName, FIKRetargetPose>& TargetPoses =
                RetargetController->GetRetargetPoses(
                    ERetargetSourceOrTarget::Target);
            const USkeletalMesh* SourcePreview =
                RetargetController->GetPreviewMesh(
                    ERetargetSourceOrTarget::Source);
            const USkeletalMesh* TargetPreview =
                RetargetController->GetPreviewMesh(
                    ERetargetSourceOrTarget::Target);
            Result.Metrics = {
                TEXT("SourceIKRig=") + RetargetRigPath(
                    RetargetController,
                    ERetargetSourceOrTarget::Source),
                TEXT("TargetIKRig=") + RetargetRigPath(
                    RetargetController,
                    ERetargetSourceOrTarget::Target),
                FString::Printf(TEXT("SourcePreviewMesh=%s"),
                    SourcePreview ? *SourcePreview->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("TargetPreviewMesh=%s"),
                    TargetPreview ? *TargetPreview->GetPathName() : TEXT("None")),
                FString::Printf(TEXT("RetargetOpCount=%d"),
                    RetargetController->GetNumRetargetOps()),
                FString::Printf(TEXT("AvailableRetargetOpTypeCount=%d"),
                    OpTypePaths.Num()),
                FString::Printf(TEXT("OverrideSetCount=%d"),
                    OverrideSets.Num()),
                FString::Printf(TEXT("ActiveOverrideSetCount=%d"),
                    ActiveOverrideSetCount),
                FString::Printf(TEXT("SourcePoseCount=%d"),
                    SourcePoses.Num()),
                FString::Printf(TEXT("TargetPoseCount=%d"),
                    TargetPoses.Num()),
                FString::Printf(TEXT("SourceCurrentPose=%s"),
                    *RetargetController->GetCurrentRetargetPoseName(
                        ERetargetSourceOrTarget::Source).ToString()),
                FString::Printf(TEXT("TargetCurrentPose=%s"),
                    *RetargetController->GetCurrentRetargetPoseName(
                        ERetargetSourceOrTarget::Target).ToString()),
                FString::Printf(TEXT("SourceCurrentPoseRootOffset=%s"),
                    *RetargetController->GetRootOffsetInRetargetPose(
                        ERetargetSourceOrTarget::Source).ToString()),
                FString::Printf(TEXT("TargetCurrentPoseRootOffset=%s"),
                    *RetargetController->GetRootOffsetInRetargetPose(
                        ERetargetSourceOrTarget::Target).ToString())
            };
            for (const FString& OpTypePath : OpTypePaths)
            {
                AddBounded(Result,
                    TEXT("IKRetargetOpType Path=") + OpTypePath,
                    MaxItems);
            }
            for (int32 OpIndex = 0;
                 OpIndex < RetargetController->GetNumRetargetOps();
                 ++OpIndex)
            {
                const FInstancedStruct* Op =
                    RetargetController->GetRetargetOpStructAtIndex(OpIndex);
                const UScriptStruct* OpType = Op
                    ? Op->GetScriptStruct() : nullptr;
                AddBounded(Result, FString::Printf(
                    TEXT("IKRetargetOp Index=%d Name=%s Type=%s Enabled=%s ParentIndex=%d"),
                    OpIndex,
                    *RetargetController->GetOpName(OpIndex).ToString(),
                    OpType ? *OpType->GetPathName() : TEXT("None"),
                    RetargetController->GetRetargetOpEnabled(OpIndex)
                        ? TEXT("true") : TEXT("false"),
                    RetargetController->GetParentOpIndex(OpIndex)),
                    MaxItems);
                AddIKRetargetOpSettingsInspection(
                    Result, RetargetController, OpIndex, MaxItems);
            }
            TArray<FName> OverrideSetNames;
            OverrideSets.GetKeys(OverrideSetNames);
            OverrideSetNames.Sort(FNameLexicalLess());
            for (const FName OverrideSetName : OverrideSetNames)
            {
                const FRetargetOverrideSet& OverrideSet =
                    OverrideSets.FindChecked(OverrideSetName);
                int32 PropertyOverrideCount = 0;
                for (const FRetargetOpOverrides& OpOverrides :
                     OverrideSet.OpOverrides)
                {
                    PropertyOverrideCount +=
                        OpOverrides.PropertyOverrides.Num();
                }
                AddBounded(Result, FString::Printf(
                    TEXT("IKRetargetOverrideSet Name=%s Parent=%s Active=%s OpOverrideCount=%d PropertyOverrideCount=%d"),
                    *OverrideSetName.ToString(),
                    *OverrideSet.ParentName.ToString(),
                    OverrideSet.bActiveByDefault
                        ? TEXT("true") : TEXT("false"),
                    OverrideSet.OpOverrides.Num(),
                    PropertyOverrideCount),
                    MaxItems);
                for (const FRetargetOpOverrides& OpOverrides :
                     OverrideSet.OpOverrides)
                {
                    for (const FRetargetOpPropertyOverride& PropertyOverride :
                         OpOverrides.PropertyOverrides)
                    {
                        AddBounded(Result, FString::Printf(
                            TEXT("IKRetargetPropertyOverride Set=%s Op=%s Property=%s Value=%s Curve=%s Variable=%s"),
                            *OverrideSetName.ToString(),
                            *OpOverrides.OpName.ToString(),
                            *PropertyOverride.GetPropertyPath(),
                            *PropertyOverride.GetValueString(),
                            *PropertyOverride.GetBoundCurveName().ToString(),
                            *PropertyOverride.GetBoundVariableName().ToString()),
                            MaxItems);
                    }
                }
            }
            auto AddPoses = [&](const ERetargetSourceOrTarget Side,
                                const TMap<FName, FIKRetargetPose>& Poses)
            {
                TArray<FName> PoseNames;
                Poses.GetKeys(PoseNames);
                PoseNames.Sort(FNameLexicalLess());
                const FName CurrentPose =
                    RetargetController->GetCurrentRetargetPoseName(Side);
                for (const FName PoseName : PoseNames)
                {
                    AddBounded(Result, FString::Printf(
                        TEXT("IKRetargetPose Side=%s Name=%s Current=%s"),
                        *RetargetSideName(Side),
                        *PoseName.ToString(),
                        PoseName == CurrentPose ? TEXT("true") : TEXT("false")),
                        MaxItems);
                }
            };
            AddPoses(ERetargetSourceOrTarget::Source, SourcePoses);
            AddPoses(ERetargetSourceOrTarget::Target, TargetPoses);
        }
        else if (const UIKRigDefinition* IKRig =
                     Cast<UIKRigDefinition>(Asset))
        {
            Result.AssetKind = TEXT("ik_rig");
            UIKRigController* IKController =
                UIKRigController::GetController(
                    const_cast<UIKRigDefinition*>(IKRig));
            const FIKRigSkeleton& IKSkeleton = IKRig->GetSkeleton();
            const TArray<UIKRigEffectorGoal*>& Goals =
                IKRig->GetGoalArray();
            const TArray<FBoneChain>& Chains =
                IKRig->GetRetargetChains();
            const TArray<FString> SolverTypePaths =
                AvailableIKRigSolverTypePaths();
            Result.Metrics = {
                FString::Printf(TEXT("PreviewSkeletalMesh=%s"),
                    *IKRig->PreviewSkeletalMesh.ToSoftObjectPath().ToString()),
                FString::Printf(TEXT("BoneCount=%d"),
                    IKSkeleton.BoneNames.Num()),
                FString::Printf(TEXT("ExcludedBoneCount=%d"),
                    IKSkeleton.ExcludedBones.Num()),
                FString::Printf(TEXT("GoalCount=%d"), Goals.Num()),
                FString::Printf(TEXT("SolverCount=%d"),
                    IKRig->GetSolverStructs().Num()),
                FString::Printf(TEXT("RetargetChainCount=%d"),
                    Chains.Num()),
                FString::Printf(TEXT("RetargetRoot=%s"),
                    *IKRig->GetPelvis().ToString()),
                FString::Printf(TEXT("RootMotionBone=%s"),
                    *IKRig->GetRoot().ToString()),
                FString::Printf(TEXT("AvailableSolverTypeCount=%d"),
                    SolverTypePaths.Num())
            };
            for (const FString& SolverTypePath : SolverTypePaths)
            {
                AddBounded(Result,
                    TEXT("IKRigSolverType Path=") + SolverTypePath,
                    MaxItems);
            }
            if (IKController)
            {
                for (int32 SolverIndex = 0;
                     SolverIndex < IKController->GetNumSolvers();
                     ++SolverIndex)
                {
                    const FInstancedStruct* SolverStruct =
                        IKController->GetSolverStructAtIndex(SolverIndex);
                    const UScriptStruct* SolverType = SolverStruct
                        ? SolverStruct->GetScriptStruct() : nullptr;
                    TArray<FString> ConnectedGoals;
                    for (const UIKRigEffectorGoal* Goal : Goals)
                    {
                        if (Goal && IKController->IsGoalConnectedToSolver(
                                Goal->GoalName, SolverIndex))
                        {
                            ConnectedGoals.Add(Goal->GoalName.ToString());
                        }
                    }
                    ConnectedGoals.Sort();
                    AddBounded(Result, FString::Printf(
                        TEXT("IKRigSolver Index=%d Type=%s Enabled=%s StartBone=%s EndBone=%s Goals=%s"),
                        SolverIndex,
                        SolverType ? *SolverType->GetPathName() : TEXT("None"),
                        IKController->GetSolverEnabled(SolverIndex)
                            ? TEXT("true") : TEXT("false"),
                        *IKController->GetStartBone(SolverIndex).ToString(),
                        *IKController->GetEndBone(SolverIndex).ToString(),
                        *FString::Join(ConnectedGoals, TEXT(","))),
                        MaxItems);
                    if (UIKRigLimbSolverController* LimbController =
                            Cast<UIKRigLimbSolverController>(
                                IKController->GetSolverController(
                                    SolverIndex)))
                    {
                        const FIKRigLimbSolverSettings Settings =
                            LimbController->GetSolverSettings();
                        AddBounded(Result, FString::Printf(
                            TEXT("IKRigLimbSettings Index=%d ReachPrecision=%.6f MaxIterations=%d EnableLimit=%s MinRotationAngle=%.6f AveragePull=%s PullDistribution=%.6f ReachStepAlpha=%.6f EnableTwistCorrection=%s HingeAxis=%s EndAxis=%s"),
                            SolverIndex,
                            Settings.ReachPrecision,
                            Settings.MaxIterations,
                            Settings.bEnableLimit
                                ? TEXT("true") : TEXT("false"),
                            Settings.MinRotationAngle,
                            Settings.bAveragePull
                                ? TEXT("true") : TEXT("false"),
                            Settings.PullDistribution,
                            Settings.ReachStepAlpha,
                            Settings.bEnableTwistCorrection
                                ? TEXT("true") : TEXT("false"),
                            *IKAxisName(Settings.HingeRotationAxis),
                            *IKAxisName(Settings.EndBoneForwardAxis)),
                            MaxItems);
                    }
                    AddIKRigSettingsInspection(
                        Result,
                        IKController,
                        EIKRigSettingScope::Solver,
                        SolverIndex,
                        NAME_None,
                        MaxItems);
                    for (const UIKRigEffectorGoal* Goal : Goals)
                    {
                        if (Goal && IKController->IsGoalConnectedToSolver(
                                Goal->GoalName, SolverIndex))
                        {
                            AddIKRigSettingsInspection(
                                Result,
                                IKController,
                                EIKRigSettingScope::Goal,
                                SolverIndex,
                                Goal->GoalName,
                                MaxItems);
                        }
                    }
                    for (const FName BoneName : IKSkeleton.BoneNames)
                    {
                        if (IKController->CanRemoveBoneSetting(
                                BoneName, SolverIndex))
                        {
                            AddIKRigSettingsInspection(
                                Result,
                                IKController,
                                EIKRigSettingScope::Bone,
                                SolverIndex,
                                BoneName,
                                MaxItems);
                        }
                    }
                }
            }
            for (int32 BoneIndex = 0;
                 BoneIndex < IKSkeleton.BoneNames.Num();
                 ++BoneIndex)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("IKRigBone Index=%d Name=%s ParentIndex=%d Excluded=%s"),
                    BoneIndex,
                    *IKSkeleton.BoneNames[BoneIndex].ToString(),
                    IKSkeleton.ParentIndices.IsValidIndex(BoneIndex)
                        ? IKSkeleton.ParentIndices[BoneIndex] : INDEX_NONE,
                    IKSkeleton.IsBoneExcluded(BoneIndex)
                        ? TEXT("true") : TEXT("false")),
                    MaxItems);
            }
            for (const UIKRigEffectorGoal* Goal : Goals)
            {
                if (Goal)
                {
                    AddBounded(Result, FString::Printf(
                        TEXT("IKRigGoal Name=%s Bone=%s PositionAlpha=%.6f RotationAlpha=%.6f"),
                        *Goal->GoalName.ToString(),
                        *Goal->BoneName.ToString(),
                        Goal->PositionAlpha,
                        Goal->RotationAlpha),
                        MaxItems);
                }
            }
            for (const FBoneChain& Chain : Chains)
            {
                AddBounded(Result, FString::Printf(
                    TEXT("IKRigRetargetChain Name=%s Start=%s End=%s Goal=%s"),
                    *Chain.ChainName.ToString(),
                    *Chain.StartBone.BoneName.ToString(),
                    *Chain.EndBone.BoneName.ToString(),
                    *Chain.IKGoalName.ToString()),
                    MaxItems);
            }
        }
        else
        {
            Result.Code = TEXT("unsupported_animation_class");
            Result.Message = Result.ClassPath;
            return Result;
        }
        Result.bOk = true;
        return Result;
    }

    virtual FThomasDomainAssetCreatePlanResult PlanAnimationAssetCreate(
        const FThomasDomainAssetCreateRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasDomainAssetCreateRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        Request.ContextAssetPath = NormalizePath(Request.ContextAssetPath);
        Request.AssetKind = Request.AssetKind.TrimStartAndEnd().ToLower();
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("path_denied"), TEXT("Animation asset must remain under /Game/PropHunt."));
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
        UObject* Context = FindAsset(Request.ContextAssetPath);
        const bool bValidContext = Request.AssetKind == TEXT("ik_rig")
            ? Cast<USkeletalMesh>(Context) != nullptr
            : (Request.AssetKind == TEXT("ik_retargeter")
                ? Cast<UIKRigDefinition>(Context) != nullptr
                : (Request.AssetKind == TEXT("control_rig")
                    ? Cast<USkeletalMesh>(Context) != nullptr
                    : ResolveSkeleton(Context) != nullptr));
        if (!bValidContext)
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                Request.AssetKind == TEXT("ik_rig")
                    ? TEXT("invalid_skeletal_mesh_context")
                    : (Request.AssetKind == TEXT("ik_retargeter")
                        ? TEXT("invalid_ik_rig_context")
                        : (Request.AssetKind == TEXT("control_rig")
                            ? TEXT("invalid_skeletal_mesh_context")
                            : TEXT("invalid_skeleton_context"))),
                Request.ContextAssetPath);
        }
        if (!CreateFactory(
                FactoryClassPath, Context, Request.AssetKind))
        {
            return MakeError<FThomasDomainAssetCreatePlanResult>(
                TEXT("animation_factory_unavailable"), FactoryClassPath);
        }

        FThomasDomainAssetCreatePlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.AssetKind = Request.AssetKind;
        Result.ExpectedRevision = TEXT("missing");
        Result.FactoryClassPath = FactoryClassPath;
        Result.ContextAssetPath = Request.ContextAssetPath;
        FCachedAnimationCreatePlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.FactoryClassPath = FactoryClassPath;
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasAnimationPlanResult PlanAnimationPatch(
        const FThomasAnimationPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasAnimationPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizePath(Request.AssetPath);
        if (!IsAllowedPath(Request.AssetPath))
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("path_denied"), TEXT("Animation asset must remain under /Game/PropHunt."));
        }
        UObject* Asset = FindAsset(Request.AssetPath);
        UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
        UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
        UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(Asset);
        USkeleton* Skeleton = Cast<USkeleton>(Asset);
        UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset);
        UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Asset);
        UIKRetargeter* IKRetargeter = Cast<UIKRetargeter>(Asset);
        UControlRigRuntimeAsset* ControlRigAsset =
            Cast<UControlRigRuntimeAsset>(Asset);
        if (!SequenceBase && !Skeleton && !AnimBlueprint && !IKRig
            && !IKRetargeter && !ControlRigAsset)
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("unsupported_animation_asset"), Asset ? Asset->GetClass()->GetPathName() : TEXT("missing"));
        }
        const FString CurrentRevision = Revision(Asset);
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != CurrentRevision)
        {
            return MakeError<FThomasAnimationPlanResult>(TEXT("revision_conflict"), CurrentRevision);
        }
        UPackage* Package = Asset->GetOutermost();
        if (Package->IsDirty())
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("dirty_package_denied"), TEXT("Save or revert the Animation asset before planning."));
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(), FPackageName::GetAssetPackageExtension());
        if (IFileManager::Get().FileExists(*Filename) && IFileManager::Get().IsReadOnly(*Filename))
        {
            return MakeError<FThomasAnimationPlanResult>(TEXT("read_only_denied"), Filename);
        }
        if (Request.Operations.IsEmpty() || Request.Operations.Num() > 128)
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("invalid_operation_count"), FString::FromInt(Request.Operations.Num()));
        }

        const TSet<FString> Supported = {
            TEXT("set_root_motion"), TEXT("set_additive_settings"),
            TEXT("add_notify_track"), TEXT("remove_notify_track"),
            TEXT("add_notify"), TEXT("add_notify_state"), TEXT("remove_notify_by_name"),
            TEXT("remove_notify_by_track"), TEXT("add_curve"), TEXT("remove_curve"),
            TEXT("set_curve_key"), TEXT("remove_curve_key"),
            TEXT("add_montage_section"), TEXT("remove_montage_section"),
            TEXT("rename_montage_section"), TEXT("set_next_montage_section"),
            TEXT("add_montage_slot"), TEXT("remove_montage_slot"),
            TEXT("add_montage_segment"), TEXT("remove_montage_segment"),
            TEXT("add_socket"), TEXT("remove_socket"),
            TEXT("add_state_machine"), TEXT("rename_state_machine"), TEXT("remove_state_machine"),
            TEXT("add_state"), TEXT("rename_state"), TEXT("remove_state"),
            TEXT("set_entry_state"), TEXT("add_transition"), TEXT("set_transition"),
            TEXT("remove_transition"), TEXT("set_state_sequence_player"),
            TEXT("remove_state_sequence_player"), TEXT("set_state_blend_by_bool"),
            TEXT("remove_state_blend_by_bool"), TEXT("set_state_blend_space_player"),
            TEXT("remove_state_blend_space_player"), TEXT("set_state_blend_list_by_int"),
            TEXT("remove_state_blend_list_by_int"), TEXT("set_state_apply_additive"),
            TEXT("remove_state_apply_additive"),
            TEXT("set_state_layered_blend_per_bone"),
            TEXT("remove_state_layered_blend_per_bone"),
            TEXT("add_state_graph_node"),
            TEXT("remove_state_graph_node"),
            TEXT("set_state_graph_node_property"),
            TEXT("resize_state_graph_node_array"),
            TEXT("resize_state_graph_pose_array"),
            TEXT("replace_state_graph_constraint_array"),
            TEXT("set_state_graph_pin_default"),
            TEXT("add_state_graph_link"),
            TEXT("remove_state_graph_link"),
            TEXT("set_transition_rule_constant"),
            TEXT("set_transition_rule_variable"), TEXT("remove_transition_rule_variable"),
            TEXT("set_transition_rule_time_remaining"),
            TEXT("remove_transition_rule_time_remaining"),
            TEXT("set_transition_rule_expression"),
            TEXT("remove_transition_rule_expression"),
            TEXT("set_ik_retarget_root"),
            TEXT("add_ik_goal"), TEXT("remove_ik_goal"),
            TEXT("add_ik_retarget_chain"),
            TEXT("remove_ik_retarget_chain"),
            TEXT("add_ik_solver"), TEXT("remove_ik_solver"),
            TEXT("set_ik_solver_enabled"),
            TEXT("set_ik_solver_start_bone"),
            TEXT("set_ik_solver_end_bone"),
            TEXT("move_ik_solver"),
            TEXT("add_ik_bone_setting"),
            TEXT("remove_ik_bone_setting"),
            TEXT("connect_ik_goal_to_solver"),
            TEXT("disconnect_ik_goal_from_solver"),
            TEXT("set_ik_root_motion_bone"),
            TEXT("set_ik_bone_excluded"),
            TEXT("set_ik_limb_solver_settings"),
            TEXT("set_ik_solver_setting"),
            TEXT("set_ik_goal_setting"),
            TEXT("set_ik_bone_setting"),
            TEXT("set_ik_retargeter_rig"),
            TEXT("add_default_ik_retarget_ops"),
            TEXT("add_ik_retarget_op"),
            TEXT("remove_ik_retarget_op"),
            TEXT("rename_ik_retarget_op"),
            TEXT("move_ik_retarget_op"),
            TEXT("set_ik_retarget_op_enabled"),
            TEXT("add_ik_retarget_override_set"),
            TEXT("rename_ik_retarget_override_set"),
            TEXT("remove_ik_retarget_override_set"),
            TEXT("set_ik_retarget_override_set_active"),
            TEXT("set_ik_retarget_override_set_parent"),
            TEXT("set_ik_retarget_property_override"),
            TEXT("remove_ik_retarget_property_override"),
            TEXT("add_ik_retarget_pose"),
            TEXT("rename_ik_retarget_pose"),
            TEXT("remove_ik_retarget_pose"),
            TEXT("set_current_ik_retarget_pose"),
            TEXT("set_ik_retarget_pose_root_offset"),
            TEXT("add_control_rig_null"),
            TEXT("rename_control_rig_null"),
            TEXT("set_control_rig_null_transform"),
            TEXT("remove_control_rig_null"),
            TEXT("add_control_rig_curve"),
            TEXT("rename_control_rig_curve"),
            TEXT("set_control_rig_curve_value"),
            TEXT("remove_control_rig_curve"),
            TEXT("add_control_rig_float_control"),
            TEXT("rename_control_rig_float_control"),
            TEXT("set_control_rig_float_value"),
            TEXT("remove_control_rig_float_control"),
            TEXT("add_control_rig_control"),
            TEXT("rename_control_rig_control"),
            TEXT("set_control_rig_control_value"),
            TEXT("remove_control_rig_control"),
            TEXT("set_control_rig_control_shape_settings"),
            TEXT("set_control_rig_control_shape_transform"),
            TEXT("set_control_rig_control_limits"),
            TEXT("add_control_rig_socket"),
            TEXT("rename_control_rig_socket"),
            TEXT("set_control_rig_socket_transform"),
            TEXT("set_control_rig_socket_settings"),
            TEXT("remove_control_rig_socket"),
            TEXT("add_control_rig_available_space"),
            TEXT("set_control_rig_available_space_label"),
            TEXT("set_control_rig_available_space_index"),
            TEXT("remove_control_rig_available_space"),
            TEXT("set_control_rig_metadata"),
            TEXT("remove_control_rig_metadata"),
            TEXT("add_control_rig_graph"),
            TEXT("rename_control_rig_graph"),
            TEXT("remove_control_rig_graph"),
            TEXT("add_control_rig_unit_node"),
            TEXT("remove_control_rig_unit_node"),
            TEXT("add_control_rig_event_node"),
            TEXT("remove_control_rig_event_node"),
            TEXT("add_control_rig_member_variable"),
            TEXT("remove_control_rig_member_variable"),
            TEXT("add_control_rig_variable_node"),
            TEXT("remove_control_rig_variable_node"),
            TEXT("add_control_rig_function"),
            TEXT("remove_control_rig_function"),
            TEXT("add_control_rig_function_reference_node"),
            TEXT("remove_control_rig_function_reference_node"),
            TEXT("add_control_rig_local_variable"),
            TEXT("remove_control_rig_local_variable"),
            TEXT("rename_control_rig_local_variable"),
            TEXT("set_default_control_rig_local_variable"),
            TEXT("set_type_control_rig_local_variable"),
            TEXT("set_index_control_rig_local_variable"),
            TEXT("add_control_rig_function_pin"),
            TEXT("remove_control_rig_function_pin"),
            TEXT("rename_control_rig_function_pin"),
            TEXT("set_type_control_rig_function_pin"),
            TEXT("add_control_rig_template_node"),
            TEXT("remove_control_rig_template_node"),
            TEXT("resolve_control_rig_wildcard_pin"),
            TEXT("unresolve_control_rig_template_node"),
            TEXT("add_control_rig_comment_node"),
            TEXT("rename_control_rig_comment_node"),
            TEXT("set_control_rig_comment"),
            TEXT("remove_control_rig_comment_node"),
            TEXT("set_control_rig_node_position"),
            TEXT("set_control_rig_node_size"),
            TEXT("set_control_rig_node_color"),
            TEXT("set_control_rig_pin_default"),
            TEXT("set_control_rig_array_pin_size"),
            TEXT("add_control_rig_array_pin"),
            TEXT("duplicate_control_rig_array_pin"),
            TEXT("remove_control_rig_array_pin"),
            TEXT("add_control_rig_aggregate_pin"),
            TEXT("remove_control_rig_aggregate_pin"),
            TEXT("add_control_rig_link"),
            TEXT("remove_control_rig_link"),
            TEXT("add_control_rig_reroute_node_on_link"),
            TEXT("remove_control_rig_reroute_node")
        };
        FThomasAnimationPlanResult Result;
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = CurrentRevision;
        TSet<FName> PlannedCurves;
        if (Sequence && Sequence->GetDataModel())
        {
            for (const FFloatCurve& Curve : Sequence->GetDataModel()->GetFloatCurves())
            {
                PlannedCurves.Add(Curve.GetName());
            }
        }
        TSet<FName> PlannedSections;
        TSet<FName> PlannedSlots;
        if (Montage)
        {
            for (const FCompositeSection& Section : Montage->CompositeSections)
            {
                PlannedSections.Add(Section.SectionName);
            }
            for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
            {
                PlannedSlots.Add(Slot.SlotName);
            }
        }
        TMap<FString, TSet<FString>> PlannedStates;
        TMap<FString, TSet<FString>> PlannedTransitions;
        TMap<FString, TSet<FString>> PlannedStatePlayers;
        TMap<FString, TSet<FString>> PlannedStateBoolBlends;
        TMap<FString, TMap<FString, int32>> PlannedStateBlendSpaceDimensions;
        TMap<FString, TMap<FString, int32>> PlannedStateIntBlendCounts;
        TMap<FString, TSet<FString>> PlannedStateApplyAdditives;
        TMap<FString, TMap<FString, int32>> PlannedStateLayeredBlendCounts;
        TMap<FString, TSet<FString>> PlannedUnsupportedStatePose;
        TMap<FString, TSet<FString>> PlannedLinkedTransitionRules;
        TMap<FString, TMap<FString, FString>> PlannedTransitionRuleVariables;
        TMap<FString, TSet<FString>> PlannedTransitionTimeRemainingRules;
        TMap<FString, TSet<FString>> PlannedTransitionExpressionRules;
        TSet<FName> PlannedIKGoals;
        TSet<FName> PlannedIKRetargetChains;
        TMap<FName, FName> PlannedIKChainGoals;
        TSet<FName> PlannedIKBones;
        UIKRigController* PlannedIKController = IKRig
            ? UIKRigController::GetController(IKRig) : nullptr;
        TArray<TSet<FName>> PlannedIKSolverGoals;
        bool bPlannedIKSolverStackChanged = false;
        if (IKRig)
        {
            for (const FName BoneName : IKRig->GetSkeleton().BoneNames)
            {
                PlannedIKBones.Add(BoneName);
            }
            for (const UIKRigEffectorGoal* Goal : IKRig->GetGoalArray())
            {
                if (Goal)
                {
                    PlannedIKGoals.Add(Goal->GoalName);
                }
            }
            for (const FBoneChain& Chain : IKRig->GetRetargetChains())
            {
                PlannedIKRetargetChains.Add(Chain.ChainName);
                PlannedIKChainGoals.Add(
                    Chain.ChainName, Chain.IKGoalName);
            }
            if (PlannedIKController)
            {
                PlannedIKSolverGoals.SetNum(
                    PlannedIKController->GetNumSolvers());
                for (int32 SolverIndex = 0;
                     SolverIndex < PlannedIKSolverGoals.Num();
                     ++SolverIndex)
                {
                    for (const FName GoalName : PlannedIKGoals)
                    {
                        if (PlannedIKController->IsGoalConnectedToSolver(
                                GoalName, SolverIndex))
                        {
                            PlannedIKSolverGoals[SolverIndex].Add(
                                GoalName);
                        }
                    }
                }
            }
        }
        UIKRetargeterController* PlannedRetargetController = IKRetargeter
            ? UIKRetargeterController::GetController(IKRetargeter) : nullptr;
        int32 PlannedRetargetOpCount = PlannedRetargetController
            ? PlannedRetargetController->GetNumRetargetOps() : 0;
        TArray<FName> PlannedRetargetOpNames;
        TSet<FName> PlannedOverrideSets;
        TMap<FName, FName> PlannedOverrideSetParents;
        TSet<FName> PlannedSourcePoses;
        TSet<FName> PlannedTargetPoses;
        FName PlannedSourceCurrentPose = NAME_None;
        FName PlannedTargetCurrentPose = NAME_None;
        bool bPlannedRetargetOpStackChanged = false;
        if (PlannedRetargetController)
        {
            for (int32 OpIndex = 0;
                 OpIndex < PlannedRetargetOpCount;
                 ++OpIndex)
            {
                PlannedRetargetOpNames.Add(
                    PlannedRetargetController->GetOpName(OpIndex));
            }
            for (const TPair<FName, FRetargetOverrideSet>& Pair :
                 PlannedRetargetController->GetAllOverrideSets())
            {
                PlannedOverrideSets.Add(Pair.Key);
                PlannedOverrideSetParents.Add(
                    Pair.Key, Pair.Value.ParentName);
            }
            for (const TPair<FName, FIKRetargetPose>& Pair :
                 PlannedRetargetController->GetRetargetPoses(
                     ERetargetSourceOrTarget::Source))
            {
                PlannedSourcePoses.Add(Pair.Key);
            }
            for (const TPair<FName, FIKRetargetPose>& Pair :
                 PlannedRetargetController->GetRetargetPoses(
                     ERetargetSourceOrTarget::Target))
            {
                PlannedTargetPoses.Add(Pair.Key);
            }
            PlannedSourceCurrentPose =
                PlannedRetargetController->GetCurrentRetargetPoseName(
                    ERetargetSourceOrTarget::Source);
            PlannedTargetCurrentPose =
                PlannedRetargetController->GetCurrentRetargetPoseName(
                    ERetargetSourceOrTarget::Target);
        }
        URigHierarchy* PlannedControlRigHierarchy = ControlRigAsset
            ? ControlRigAsset->GetHierarchy() : nullptr;
        URigHierarchyController* PlannedControlRigController =
            PlannedControlRigHierarchy
                ? PlannedControlRigHierarchy->GetController(true)
                : nullptr;
        UControlRigEditorAsset* PlannedControlRigEditorAsset =
            GetControlRigEditorAsset(ControlRigAsset);
        if (ControlRigAsset
            && (!PlannedControlRigHierarchy || !PlannedControlRigController))
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("control_rig_hierarchy_unavailable"),
                Request.AssetPath);
        }
        if (ControlRigAsset && Request.Operations.Num() != 1)
        {
            return MakeError<FThomasAnimationPlanResult>(
                TEXT("control_rig_single_operation_required"),
                TEXT("Exactly one bounded Control Rig hierarchy mutation is accepted per plan."));
        }
        if (AnimBlueprint)
        {
            TArray<UAnimGraphNode_StateMachine*> Machines;
            GetStateMachines(AnimBlueprint, Machines);
            for (UAnimGraphNode_StateMachine* Machine : Machines)
            {
                if (!Machine || !Machine->EditorStateMachineGraph)
                {
                    continue;
                }
                const FString MachineName = Machine->GetStateMachineName();
                TSet<FString>& States = PlannedStates.Add(MachineName);
                TSet<FString>& Transitions = PlannedTransitions.Add(MachineName);
                TSet<FString>& StatePlayers = PlannedStatePlayers.Add(MachineName);
                TSet<FString>& StateBoolBlends =
                    PlannedStateBoolBlends.Add(MachineName);
                TMap<FString, int32>& StateBlendSpaceDimensions =
                    PlannedStateBlendSpaceDimensions.Add(MachineName);
                TMap<FString, int32>& StateIntBlendCounts =
                    PlannedStateIntBlendCounts.Add(MachineName);
                TSet<FString>& StateApplyAdditives =
                    PlannedStateApplyAdditives.Add(MachineName);
                TMap<FString, int32>& StateLayeredBlendCounts =
                    PlannedStateLayeredBlendCounts.Add(MachineName);
                TSet<FString>& UnsupportedStatePose =
                    PlannedUnsupportedStatePose.Add(MachineName);
                TSet<FString>& LinkedTransitionRules =
                    PlannedLinkedTransitionRules.Add(MachineName);
                TMap<FString, FString>& TransitionRuleVariables =
                    PlannedTransitionRuleVariables.Add(MachineName);
                TSet<FString>& TransitionTimeRemainingRules =
                    PlannedTransitionTimeRemainingRules.Add(MachineName);
                TSet<FString>& TransitionExpressionRules =
                    PlannedTransitionExpressionRules.Add(MachineName);
                for (UEdGraphNode* Node : Machine->EditorStateMachineGraph->Nodes)
                {
                    if (const UAnimStateNode* State = Cast<UAnimStateNode>(Node))
                    {
                        const FString StateName = State->GetStateName();
                        States.Add(StateName);
                        if (FindStateSequencePlayer(const_cast<UAnimStateNode*>(State)))
                        {
                            StatePlayers.Add(StateName);
                        }
                        if (FindStateBlendByBool(const_cast<UAnimStateNode*>(State)))
                        {
                            StateBoolBlends.Add(StateName);
                        }
                        UAnimGraphNode_BlendSpacePlayer* BlendSpacePlayer = nullptr;
                        UK2Node_VariableGet* XGetter = nullptr;
                        UK2Node_VariableGet* YGetter = nullptr;
                        bool bOneDimensional = false;
                        if (GetStateBlendSpaceParts(
                                const_cast<UAnimStateNode*>(State),
                                BlendSpacePlayer,
                                XGetter,
                                YGetter,
                                bOneDimensional))
                        {
                            StateBlendSpaceDimensions.Add(
                                StateName, bOneDimensional ? 1 : 2);
                        }
                        UAnimGraphNode_BlendListByInt* IntBlend = nullptr;
                        TArray<UAnimGraphNode_SequencePlayer*> IntPlayers;
                        UK2Node_VariableGet* IndexGetter = nullptr;
                        if (GetStateIntBlendParts(
                                const_cast<UAnimStateNode*>(State),
                                IntBlend,
                                IntPlayers,
                                IndexGetter))
                        {
                            StateIntBlendCounts.Add(StateName, IntPlayers.Num());
                        }
                        UAnimGraphNode_ApplyAdditive* ApplyAdditive = nullptr;
                        UAnimGraphNode_SequencePlayer* BasePlayer = nullptr;
                        UAnimGraphNode_SequencePlayer* AdditivePlayer = nullptr;
                        UK2Node_VariableGet* AlphaGetter = nullptr;
                        if (GetStateApplyAdditiveParts(
                                const_cast<UAnimStateNode*>(State),
                                ApplyAdditive,
                                BasePlayer,
                                AdditivePlayer,
                                AlphaGetter))
                        {
                            StateApplyAdditives.Add(StateName);
                        }
                        UAnimGraphNode_LayeredBoneBlend* LayeredBlend = nullptr;
                        UAnimGraphNode_SequencePlayer* LayeredBasePlayer = nullptr;
                        TArray<UAnimGraphNode_SequencePlayer*> LayeredPlayers;
                        TArray<UK2Node_VariableGet*> LayeredAlphaGetters;
                        if (GetStateLayeredBlendPerBoneParts(
                                const_cast<UAnimStateNode*>(State),
                                LayeredBlend,
                                LayeredBasePlayer,
                                LayeredPlayers,
                                LayeredAlphaGetters))
                        {
                            StateLayeredBlendCounts.Add(
                                StateName, LayeredPlayers.Num());
                        }
                        if (HasUnsupportedStatePoseContent(
                                const_cast<UAnimStateNode*>(State)))
                        {
                            UnsupportedStatePose.Add(StateName);
                        }
                    }
                    else if (UAnimStateTransitionNode* Transition =
                                 Cast<UAnimStateTransitionNode>(Node))
                    {
                        const UAnimStateNodeBase* Previous = Transition->GetPreviousState();
                        const UAnimStateNodeBase* Next = Transition->GetNextState();
                        if (Previous && Next)
                        {
                            const FString Key = TransitionKey(
                                Previous->GetStateName(), Next->GetStateName());
                            Transitions.Add(Key);
                            const UAnimationTransitionGraph* RuleGraph =
                                Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
                            const UAnimGraphNode_TransitionResult* RuleResult =
                                RuleGraph ? RuleGraph->MyResultNode : nullptr;
                            const UEdGraphPin* RulePin = RuleResult
                                ? RuleResult->FindPin(
                                    TEXT("bCanEnterTransition"), EGPD_Input)
                                : nullptr;
                            if (RulePin && !RulePin->LinkedTo.IsEmpty())
                            {
                                LinkedTransitionRules.Add(Key);
                                if (UK2Node_VariableGet* Getter =
                                        FindTransitionRuleVariableGetter(Transition))
                                {
                                    TransitionRuleVariables.Add(
                                        Key, Getter->GetVarName().ToString());
                                }
                                FTransitionTimeRemainingRuleParts TimeParts;
                                if (GetTransitionTimeRemainingRuleParts(
                                        Transition, TimeParts))
                                {
                                    TransitionTimeRemainingRules.Add(Key);
                                }
                                FTransitionExpressionRuleParts ExpressionParts;
                                if (GetTransitionExpressionRuleParts(
                                        Transition,
                                        AnimBlueprint,
                                        ExpressionParts))
                                {
                                    TransitionExpressionRules.Add(Key);
                                }
                            }
                        }
                    }
                }
            }
        }
        for (FThomasAnimationOperation& Operation : Request.Operations)
        {
            Operation.Action = Operation.Action.TrimStartAndEnd().ToLower();
            Operation.ControlType =
                Operation.ControlType.TrimStartAndEnd().ToLower();
            Operation.ControlShapeName =
                Operation.ControlShapeName.TrimStartAndEnd();
            Operation.SpaceName = Operation.SpaceName.TrimStartAndEnd();
            Operation.SpaceType =
                Operation.SpaceType.TrimStartAndEnd().ToLower();
            Operation.DisplayLabel =
                Operation.DisplayLabel.TrimStartAndEnd();
            Operation.Description = Operation.Description.TrimStartAndEnd();
            Operation.ElementType =
                Operation.ElementType.TrimStartAndEnd().ToLower();
            Operation.MetadataName =
                Operation.MetadataName.TrimStartAndEnd();
            Operation.MetadataType =
                Operation.MetadataType.TrimStartAndEnd().ToLower();
            NormalizeControlRigMetadataValue(Operation.MetadataValue);
            Operation.RigVMGraphName =
                Operation.RigVMGraphName.TrimStartAndEnd();
            Operation.RigVMMethodName =
                Operation.RigVMMethodName.TrimStartAndEnd();
            Operation.RigVMPinPath =
                Operation.RigVMPinPath.TrimStartAndEnd();
            Operation.RigVMSourcePinPath =
                Operation.RigVMSourcePinPath.TrimStartAndEnd();
            Operation.RigVMTargetPinPath =
                Operation.RigVMTargetPinPath.TrimStartAndEnd();
            Operation.MetadataValue.NameValue =
                Operation.MetadataValue.NameValue.TrimStartAndEnd();
            Operation.MetadataValue.ElementName =
                Operation.MetadataValue.ElementName.TrimStartAndEnd();
            Operation.MetadataValue.ElementType =
                Operation.MetadataValue.ElementType
                    .TrimStartAndEnd().ToLower();
            Operation.ReferencedAssetPath = NormalizePath(Operation.ReferencedAssetPath);
            Operation.SecondaryAssetPath = NormalizePath(Operation.SecondaryAssetPath);
            Operation.AdditiveBasePoseAssetPath =
                NormalizePath(Operation.AdditiveBasePoseAssetPath);
            Operation.InputXVariableName =
                Operation.InputXVariableName.TrimStartAndEnd();
            Operation.InputYVariableName =
                Operation.InputYVariableName.TrimStartAndEnd();
            Operation.InputIndexVariableName =
                Operation.InputIndexVariableName.TrimStartAndEnd();
            Operation.InputAlphaVariableName =
                Operation.InputAlphaVariableName.TrimStartAndEnd();
            Operation.TransitionGetter =
                Operation.TransitionGetter.TrimStartAndEnd().ToLower();
            Operation.ComparisonOperator =
                Operation.ComparisonOperator.TrimStartAndEnd().ToLower();
            Operation.LogicalOperator =
                Operation.LogicalOperator.TrimStartAndEnd().ToLower();
            for (FThomasAnimationTransitionToken& Token :
                 Operation.TransitionExpressionTokens)
            {
                Token.Kind = Token.Kind.TrimStartAndEnd().ToLower();
                Token.Name = Token.Name.TrimStartAndEnd();
                Token.ContextName = Token.ContextName.TrimStartAndEnd();
                Token.Operator =
                    Token.Operator.TrimStartAndEnd().ToLower();
                if (Token.Kind == TEXT("transition_getter"))
                {
                    Token.Name = Token.Name.ToLower();
                }
            }
            for (FString& ReferencedPath : Operation.ReferencedAssetPaths)
            {
                ReferencedPath = NormalizePath(ReferencedPath);
            }
            for (FString& VariableName : Operation.LayerAlphaVariableNames)
            {
                VariableName = VariableName.TrimStartAndEnd();
            }
            for (FString& BoneName : Operation.LayerBoneNames)
            {
                BoneName = BoneName.TrimStartAndEnd();
            }
            Operation.MachineName = Operation.MachineName.TrimStartAndEnd();
            Operation.StateName = Operation.StateName.TrimStartAndEnd();
            Operation.StateGraphNodeGuid =
                Operation.StateGraphNodeGuid.TrimStartAndEnd().ToLower();
            Operation.StateGraphPinGuid =
                Operation.StateGraphPinGuid.TrimStartAndEnd().ToLower();
            Operation.StateGraphSourcePinGuid =
                Operation.StateGraphSourcePinGuid.TrimStartAndEnd().ToLower();
            Operation.StateGraphTargetPinGuid =
                Operation.StateGraphTargetPinGuid.TrimStartAndEnd().ToLower();
            Operation.FromStateName = Operation.FromStateName.TrimStartAndEnd();
            Operation.ToStateName = Operation.ToStateName.TrimStartAndEnd();
            Operation.StartBoneName = Operation.StartBoneName.TrimStartAndEnd();
            Operation.EndBoneName = Operation.EndBoneName.TrimStartAndEnd();
            Operation.GoalName = Operation.GoalName.TrimStartAndEnd();
            Operation.RetargetOpName =
                Operation.RetargetOpName.TrimStartAndEnd();
            Operation.RetargetSide =
                Operation.RetargetSide.TrimStartAndEnd().ToLower();
            Operation.ParentName = Operation.ParentName.TrimStartAndEnd();
            Operation.PropertyName = Operation.PropertyName.TrimStartAndEnd();
            Operation.ClassPath = Operation.ClassPath.TrimStartAndEnd();
            Operation.RigVMGraphName =
                Operation.RigVMGraphName.TrimStartAndEnd();
            Operation.RigVMMethodName =
                Operation.RigVMMethodName.TrimStartAndEnd();
            Operation.RigVMTemplateNotation =
                Operation.RigVMTemplateNotation.TrimStartAndEnd();
            Operation.RigVMType =
                Operation.RigVMType.TrimStartAndEnd().ToLower();
            Operation.RigVMVariableName =
                Operation.RigVMVariableName.TrimStartAndEnd();
            Operation.RigVMFunctionName =
                Operation.RigVMFunctionName.TrimStartAndEnd();
            NormalizeR25ControlRigOperation(Operation);
            NormalizeR26ControlRigOperation(Operation);
            NormalizeR27ControlRigOperation(Operation);
            Operation.RigVMPinPath =
                Operation.RigVMPinPath.TrimStartAndEnd();
            Operation.RigVMSourcePinPath =
                Operation.RigVMSourcePinPath.TrimStartAndEnd();
            Operation.RigVMTargetPinPath =
                Operation.RigVMTargetPinPath.TrimStartAndEnd();
            Operation.IKHingeRotationAxis =
                Operation.IKHingeRotationAxis.TrimStartAndEnd().ToLower();
            Operation.IKEndBoneForwardAxis =
                Operation.IKEndBoneForwardAxis.TrimStartAndEnd().ToLower();
            Operation.NewName = Operation.NewName.TrimStartAndEnd();
            const FString& Action = Operation.Action;
            if (!Supported.Contains(Action)
                && !IsR25ControlRigGraphAction(Action)
                && !IsR26ControlRigGraphAction(Action)
                && !IsR27ControlRigGraphAction(Action)
                && !IsR31ControlRigGraphAction(Action))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("unsupported_operation"), Action);
            }
            const bool bDestructive = Action.StartsWith(TEXT("remove_"))
                || Action.StartsWith(TEXT("rename_"))
                || Action.StartsWith(TEXT("disconnect_"))
                || Action == TEXT("add_control_rig_reroute_node_on_link")
                || Action == TEXT("set_control_rig_array_pin_size")
                || Action == TEXT("resize_state_graph_node_array")
                || Action == TEXT("resize_state_graph_pose_array")
                || Action == TEXT("replace_state_graph_constraint_array")
                || IsStateGraphR2PropertyOperation(
                    Operation, AnimBlueprint)
                || Action == TEXT("unresolve_control_rig_template_node")
                || Action == TEXT("set_type_control_rig_function_pin")
                || Action == TEXT("set_mutable_control_rig_function")
                || Action == TEXT("set_type_control_rig_local_variable")
                || Action == TEXT("set_control_rig_template_pin_type")
                || IsR26ControlRigGraphAction(Action);
            if (bDestructive && !Request.bConfirmDestructive)
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("confirmation_required"), Action);
            }
            if (bDestructive)
            {
                Result.Risk = TEXT("R2");
            }
            const bool bIKRigAction = Action == TEXT("set_ik_retarget_root")
                || Action == TEXT("add_ik_goal")
                || Action == TEXT("remove_ik_goal")
                || Action == TEXT("add_ik_retarget_chain")
                || Action == TEXT("remove_ik_retarget_chain")
                || Action == TEXT("add_ik_solver")
                || Action == TEXT("remove_ik_solver")
                || Action == TEXT("set_ik_solver_enabled")
                || Action == TEXT("set_ik_solver_start_bone")
                || Action == TEXT("set_ik_solver_end_bone")
                || Action == TEXT("move_ik_solver")
                || Action == TEXT("add_ik_bone_setting")
                || Action == TEXT("remove_ik_bone_setting")
                || Action == TEXT("connect_ik_goal_to_solver")
                || Action == TEXT("disconnect_ik_goal_from_solver")
                || Action == TEXT("set_ik_root_motion_bone")
                || Action == TEXT("set_ik_bone_excluded")
                || Action == TEXT("set_ik_limb_solver_settings")
                || Action == TEXT("set_ik_solver_setting")
                || Action == TEXT("set_ik_goal_setting")
                || Action == TEXT("set_ik_bone_setting");
            const bool bIKRetargeterAction =
                Action == TEXT("set_ik_retargeter_rig")
                || Action.Contains(TEXT("ik_retarget_op"))
                || Action.Contains(TEXT("ik_retarget_override_set"))
                || Action.Contains(TEXT("ik_retarget_property_override"))
                || Action.Contains(TEXT("ik_retarget_pose"));
            const bool bControlRigModelAction =
                IsControlRigModelAction(Action);
            const bool bControlRigGraphAction =
                IsControlRigGraphAction(Action);
            const bool bControlRigAction = bControlRigModelAction
                || bControlRigGraphAction
                ||
                Action.Contains(TEXT("control_rig_null"))
                || Action.Contains(TEXT("control_rig_curve"))
                || Action.Contains(TEXT("control_rig_float"))
                || Action.Contains(TEXT("control_rig_control"))
                || Action.Contains(TEXT("control_rig_socket"))
                || Action.Contains(TEXT("control_rig_available_space"))
                || Action.Contains(TEXT("control_rig_metadata"));
            if (bControlRigAction)
            {
                if (bControlRigModelAction)
                {
                    const TOptional<FControlRigGraphPreflightError> Error =
                        ValidateControlRigModelOperation(
                            Operation,
                            PlannedControlRigEditorAsset);
                    if (Error.IsSet())
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            Error->Code, Error->Message);
                    }
                    Result.Preview.Add((Action + TEXT(":")
                        + Operation.Name + Operation.RigVMGraphName)
                            .Left(500));
                    continue;
                }
                else if (bControlRigGraphAction)
                {
                    URigVMGraph* Model = ResolveControlRigModel(
                        PlannedControlRigEditorAsset,
                        Operation.RigVMGraphName);
                    URigVMController* Controller =
                        PlannedControlRigEditorAsset && Model
                            ? PlannedControlRigEditorAsset
                                ->GetOrCreateController(Model)
                            : nullptr;
                    if (!ControlRigAsset || !PlannedControlRigEditorAsset
                        || !Model || !Controller)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("control_rig_graph_unavailable"),
                            Operation.RigVMGraphName);
                    }
                    if (Operation.RigVMGraphName.Len() > 256
                        || Operation.RigVMGraphName.Contains(TEXT("\r"))
                        || Operation.RigVMGraphName.Contains(TEXT("\n")))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_graph_name"),
                            Operation.RigVMGraphName);
                    }
                    const auto IsValidNodeSize = [](const FVector2D& Size)
                    {
                        return FMath::IsFinite(Size.X)
                            && FMath::IsFinite(Size.Y)
                            && Size.X >= 1.0 && Size.X <= 100000.0
                            && Size.Y >= 1.0 && Size.Y <= 100000.0;
                    };
                    const auto IsValidNodeColor = [](const FLinearColor& Color)
                    {
                        return FMath::IsFinite(Color.R)
                            && FMath::IsFinite(Color.G)
                            && FMath::IsFinite(Color.B)
                            && FMath::IsFinite(Color.A)
                            && Color.R >= 0.0f && Color.R <= 1.0f
                            && Color.G >= 0.0f && Color.G <= 1.0f
                            && Color.B >= 0.0f && Color.B <= 1.0f
                            && Color.A >= 0.0f && Color.A <= 1.0f;
                    };
                    bool bBasicCreationHandled = false;
                    const TOptional<FControlRigGraphPreflightError>
                        BasicCreationError =
                            ValidateBasicControlRigGraphCreation(
                                Operation,
                                Model,
                                bBasicCreationHandled);
                    if (bBasicCreationHandled)
                    {
                        if (BasicCreationError.IsSet())
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                BasicCreationError->Code,
                                BasicCreationError->Message);
                        }
                    }
                    else if (IsAdvancedControlRigGraphAction(Action))
                    {
                        const TOptional<FControlRigGraphPreflightError> Error =
                            ValidateAdvancedControlRigGraphOperation(
                                Operation,
                                Model,
                                ControlRigAsset,
                                PlannedControlRigEditorAsset);
                        if (Error.IsSet())
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                Error->Code, Error->Message);
                        }
                    }
                    else if (IsR22ControlRigGraphAction(Action))
                    {
                        const TOptional<FControlRigGraphPreflightError> Error =
                            ValidateR22ControlRigGraphOperation(
                                Operation,
                                Model,
                                PlannedControlRigEditorAsset);
                        if (Error.IsSet())
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                Error->Code, Error->Message);
                        }
                    }
                    else if (Action
                        == TEXT("rename_control_rig_comment_node"))
                    {
                        URigVMCommentNode* Node = Cast<URigVMCommentNode>(
                            Model->FindNodeByName(FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || !IsSafeGraphName(Operation.NewName)
                            || Model->FindNodeByName(
                                FName(*Operation.NewName))
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_comment_node_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action == TEXT("set_control_rig_comment"))
                    {
                        URigVMCommentNode* Node = Cast<URigVMCommentNode>(
                            Model->FindNodeByName(FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        const FString RequestedProperties =
                            RigVMCommentPropertiesString(
                                Operation.RigVMCommentText,
                                Operation.RigVMCommentFontSize,
                                Operation.bRigVMCommentBubbleVisible,
                                Operation.bRigVMCommentColorBubble);
                        if (!Node || Operation.RigVMCommentText.Len() > 1000
                            || Operation.RigVMCommentFontSize < 6
                            || Operation.RigVMCommentFontSize > 96
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_comment_node_conflict"),
                                CurrentState);
                        }
                        if (RequestedProperties
                            == RigVMCommentPropertiesString(Node))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_comment_node_noop"),
                                RequestedProperties);
                        }
                    }
                    else if (Action
                        == TEXT("remove_control_rig_comment_node"))
                    {
                        URigVMCommentNode* Node = Cast<URigVMCommentNode>(
                            Model->FindNodeByName(FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_comment_node_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action
                        == TEXT("remove_control_rig_unit_node"))
                    {
                        URigVMUnitNode* Node = Cast<URigVMUnitNode>(
                            Model->FindNodeByName(FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || Node->IsEvent()
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action
                        == TEXT("remove_control_rig_template_node"))
                    {
                        URigVMTemplateNode* Node =
                            Cast<URigVMTemplateNode>(
                                Model->FindNodeByName(
                                    FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || !Node->GetTemplate()
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_template_node_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action
                        == TEXT("unresolve_control_rig_template_node"))
                    {
                        URigVMTemplateNode* Node =
                            Cast<URigVMTemplateNode>(
                                Model->FindNodeByName(
                                    FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || !Node->GetTemplate()
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_template_node_conflict"),
                                CurrentState);
                        }
                        if (Node->IsFullyUnresolved())
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_template_node_noop"),
                                CurrentState);
                        }
                    }
                    else if (Action
                        == TEXT("set_control_rig_node_position"))
                    {
                        URigVMNode* Node = Model->FindNodeByName(
                            FName(*Operation.Name));
                        const FString CurrentPosition =
                            RigVMNodePositionString(Node);
                        const FString RequestedPosition = FString::Printf(
                            TEXT("X=%.6f,Y=%.6f"),
                            static_cast<double>(Operation.PositionX),
                            static_cast<double>(Operation.PositionY));
                        if (!Node
                            || FMath::Abs(Operation.PositionX) > 1000000
                            || FMath::Abs(Operation.PositionY) > 1000000
                            || Operation.ExpectedValue != CurrentPosition)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_position_conflict"),
                                CurrentPosition);
                        }
                        if (CurrentPosition == RequestedPosition)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_position_noop"),
                                CurrentPosition);
                        }
                    }
                    else if (Action
                        == TEXT("set_control_rig_node_size"))
                    {
                        URigVMNode* Node = Model->FindNodeByName(
                            FName(*Operation.Name));
                        const FString CurrentSize =
                            RigVMNodeSizeString(Node);
                        const FString RequestedSize =
                            RigVMVector2DString(Operation.RigVMNodeSize);
                        if (!Node || !IsValidNodeSize(Operation.RigVMNodeSize)
                            || Operation.ExpectedValue != CurrentSize)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_size_conflict"),
                                CurrentSize);
                        }
                        if (CurrentSize == RequestedSize)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_size_noop"),
                                CurrentSize);
                        }
                    }
                    else if (Action
                        == TEXT("set_control_rig_node_color"))
                    {
                        URigVMNode* Node = Model->FindNodeByName(
                            FName(*Operation.Name));
                        const FString CurrentColor =
                            RigVMNodeColorString(Node);
                        const FString RequestedColor =
                            RigVMColorString(Operation.Color);
                        if (!Node || !IsValidNodeColor(Operation.Color)
                            || Operation.ExpectedValue != CurrentColor)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_color_conflict"),
                                CurrentColor);
                        }
                        if (CurrentColor == RequestedColor)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_node_color_noop"),
                                CurrentColor);
                        }
                    }
                    else if (Action
                        == TEXT("remove_control_rig_reroute_node"))
                    {
                        URigVMRerouteNode* Node = Cast<URigVMRerouteNode>(
                            Model->FindNodeByName(FName(*Operation.Name)));
                        const FString CurrentState =
                            RigVMNodeStateString(Node);
                        if (!Node || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_reroute_node_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action
                        == TEXT("set_control_rig_pin_default"))
                    {
                        URigVMPin* Pin = Model->FindPin(
                            Operation.RigVMPinPath);
                        const FString CurrentValue = Pin
                            ? Pin->GetDefaultValue() : TEXT("Missing");
                        if (!Pin
                            || !Pin->CanProvideDefaultValue()
                            || !Pin->GetSourceLinks(true).IsEmpty()
                            || Operation.RigVMPinPath.Len() > 512
                            || Operation.RigVMPinDefaultValue.Len() > 1200
                            || Operation.RigVMPinDefaultValue.Contains(
                                TEXT("\r"))
                            || Operation.RigVMPinDefaultValue.Contains(
                                TEXT("\n"))
                            || Operation.ExpectedValue != CurrentValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_pin_default_conflict"),
                                CurrentValue);
                        }
                        if (Operation.RigVMPinDefaultValue == CurrentValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_pin_default_noop"),
                                CurrentValue);
                        }
                    }
                    else if (Action
                        == TEXT("resolve_control_rig_wildcard_pin"))
                    {
                        URigVMPin* Pin = Model->FindPin(
                            Operation.RigVMPinPath);
                        const FString CurrentState =
                            RigVMPinTypeStateString(Pin);
                        const bool bArray = Pin
                            && (Pin->IsDynamicArray()
                                || Pin->IsFixedSizeArray());
                        const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
                            ResolveBoundedRigVMType(
                                Operation.RigVMType, bArray);
                        URigVMTemplateNode* TemplateNode = Pin
                            ? Cast<URigVMTemplateNode>(
                                Pin->GetTypedOuter<URigVMNode>())
                            : nullptr;
                        const FRigVMTemplate* Template = TemplateNode
                            ? TemplateNode->GetTemplate() : nullptr;
                        bool bTemplateSupportsType = false;
                        if (Pin && Pin->IsRootPin() && TypeSpec.IsSet()
                            && Template)
                        {
                            const TRigVMTypeIndex TypeIndex =
                                FRigVMRegistry::Get()
                                    .GetTypeIndexFromCPPType(
                                        TypeSpec->CPPType);
                            bTemplateSupportsType =
                                Template->ArgumentSupportsTypeIndex(
                                    Pin->GetFName(), TypeIndex, nullptr);
                        }
                        if (!Pin || !Pin->IsRootPin()
                            || !IsRigVMWildcardPin(Pin)
                            || !TemplateNode || !Template
                            || !TypeSpec.IsSet()
                            || !bTemplateSupportsType
                            || !Pin->GetSourceLinks(true).IsEmpty()
                            || !Pin->GetTargetLinks(true).IsEmpty()
                            || Operation.RigVMPinPath.Len() > 512
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_wildcard_pin_conflict"),
                                CurrentState);
                        }
                    }
                    else if (Action
                            == TEXT("set_control_rig_array_pin_size")
                        || Action == TEXT("add_control_rig_array_pin")
                        || Action
                            == TEXT("duplicate_control_rig_array_pin")
                        || Action == TEXT("remove_control_rig_array_pin"))
                    {
                        URigVMPin* RequestedPin = Model->FindPin(
                            Operation.RigVMPinPath);
                        const bool bElementAction =
                            Action
                                == TEXT("duplicate_control_rig_array_pin")
                            || Action
                                == TEXT("remove_control_rig_array_pin");
                        URigVMPin* ArrayPin = bElementAction
                            && RequestedPin
                                ? RequestedPin->GetParentPin()
                                : RequestedPin;
                        const FString CurrentState =
                            RigVMArrayPinStateString(ArrayPin);
                        const int32 CurrentSize = ArrayPin
                            ? ArrayPin->GetSubPins().Num() : INDEX_NONE;
                        if (!RequestedPin || !ArrayPin
                            || !ArrayPin->IsDynamicArray()
                            || (bElementAction
                                && RequestedPin->GetParentPin()
                                    != ArrayPin)
                            || Operation.RigVMPinPath.Len() > 512
                            || Operation.ExpectedValue != CurrentState)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_array_pin_conflict"),
                                CurrentState);
                        }
                        const bool bUsesDefault =
                            Action
                                == TEXT("set_control_rig_array_pin_size")
                            || Action == TEXT("add_control_rig_array_pin");
                        if (bUsesDefault
                            && (Operation.RigVMPinDefaultValue.Len() > 1200
                                || Operation.RigVMPinDefaultValue.Contains(
                                    TEXT("\r"))
                                || Operation.RigVMPinDefaultValue.Contains(
                                    TEXT("\n"))))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_control_rig_array_default"),
                                Operation.RigVMPinPath);
                        }
                        if (Action
                            == TEXT("set_control_rig_array_pin_size"))
                        {
                            if (Operation.RigVMArraySize < 0
                                || Operation.RigVMArraySize > 64)
                            {
                                return MakeError<
                                    FThomasAnimationPlanResult>(
                                    TEXT("invalid_control_rig_array_size"),
                                    FString::FromInt(
                                        Operation.RigVMArraySize));
                            }
                            if (Operation.RigVMArraySize == CurrentSize)
                            {
                                return MakeError<
                                    FThomasAnimationPlanResult>(
                                    TEXT("control_rig_array_pin_noop"),
                                    CurrentState);
                            }
                        }
                        else if ((Action
                                == TEXT("add_control_rig_array_pin")
                                || Action
                                    == TEXT(
                                        "duplicate_control_rig_array_pin"))
                            && CurrentSize >= 64)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_array_size_limit"),
                                CurrentState);
                        }
                        else if (Action
                                == TEXT("remove_control_rig_array_pin")
                            && CurrentSize <= 0)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_array_pin_conflict"),
                                CurrentState);
                        }
                    }
                    else
                    {
                        URigVMPin* SourcePin = Model->FindPin(
                            Operation.RigVMSourcePinPath);
                        URigVMPin* TargetPin = Model->FindPin(
                            Operation.RigVMTargetPinPath);
                        const FString LinkState = RigVMLinkStateString(
                            Operation.RigVMSourcePinPath,
                            Operation.RigVMTargetPinPath);
                        const bool bLinkExists =
                            Model->ContainsLink(LinkState);
                        if (!SourcePin || !TargetPin
                            || Operation.RigVMSourcePinPath.Len() > 512
                            || Operation.RigVMTargetPinPath.Len() > 512
                            || (SourcePin->GetDirection()
                                    != ERigVMPinDirection::Output
                                && SourcePin->GetDirection()
                                    != ERigVMPinDirection::IO)
                            || (TargetPin->GetDirection()
                                    != ERigVMPinDirection::Input
                                && TargetPin->GetDirection()
                                    != ERigVMPinDirection::IO))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_control_rig_link"),
                                LinkState);
                        }
                        if (Action == TEXT("add_control_rig_link")
                            && (bLinkExists
                                || !TargetPin->GetSourceLinks(true)
                                    .IsEmpty()))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_link_conflict"),
                                bLinkExists ? LinkState : TEXT("TargetLinked"));
                        }
                        if (Action == TEXT("remove_control_rig_link")
                            && (!bLinkExists
                                || Operation.ExpectedValue != LinkState))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_link_conflict"),
                                bLinkExists ? LinkState : TEXT("Missing"));
                        }
                        if (Action
                                == TEXT("add_control_rig_reroute_node_on_link")
                            && (!bLinkExists
                                || Operation.ExpectedValue != LinkState
                                || !IsSafeGraphName(Operation.Name)
                                || Model->FindNodeByName(
                                    FName(*Operation.Name))
                                || FMath::Abs(Operation.PositionX) > 1000000
                                || FMath::Abs(Operation.PositionY) > 1000000))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_reroute_link_conflict"),
                                bLinkExists ? LinkState : TEXT("Missing"));
                        }
                    }
                    Result.Preview.Add((Action + TEXT(":")
                        + RigVMGraphIdentifier(Model) + TEXT(":")
                        + Operation.Name + Operation.RigVMFunctionName
                        + Operation.RigVMVariableName
                        + Operation.RigVMPinPath
                        + Operation.RigVMSourcePinPath + TEXT("->")
                        + Operation.RigVMTargetPinPath).Left(500));
                    continue;
                }
                if (!ControlRigAsset || !PlannedControlRigHierarchy
                    || !PlannedControlRigController)
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("operation_requires_control_rig"), Action);
                }
                const TArray<FRigElementKey> AllKeys =
                    PlannedControlRigHierarchy->GetAllKeys(
                        true, ERigElementType::All);
                auto KeysNamed = [&AllKeys](
                    const FString& Name,
                    const ERigElementType AllowedTypes)
                {
                    TArray<FRigElementKey> Matches;
                    for (const FRigElementKey& Key : AllKeys)
                    {
                        if (Key.Name == FName(*Name)
                            && (AllowedTypes == ERigElementType::All
                                || Key.Type == AllowedTypes))
                        {
                            Matches.Add(Key);
                        }
                    }
                    return Matches;
                };
                const bool bNullAction =
                    Action.Contains(TEXT("control_rig_null"));
                const bool bCurveAction =
                    Action.Contains(TEXT("control_rig_curve"));
                const bool bFloatControlAction =
                    Action.Contains(TEXT("control_rig_float"));
                const bool bGenericControlAction =
                    Action.Contains(TEXT("control_rig_control"));
                const bool bSocketAction =
                    Action.Contains(TEXT("control_rig_socket"));
                const bool bAvailableSpaceAction =
                    Action.Contains(TEXT("control_rig_available_space"));
                const bool bMetadataAction =
                    Action.Contains(TEXT("control_rig_metadata"));
                const bool bTypedControlAction =
                    bFloatControlAction || bGenericControlAction;
                const TOptional<ERigElementType> MetadataElementType =
                    bMetadataAction
                        ? ParseControlRigElementType(
                            Operation.ElementType)
                        : TOptional<ERigElementType>();
                if (bMetadataAction && !MetadataElementType.IsSet())
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("invalid_control_rig_element_type"),
                        Operation.ElementType);
                }
                const ERigElementType ElementType = bNullAction
                    ? ERigElementType::Null
                    : bCurveAction
                        ? ERigElementType::Curve
                        : bSocketAction
                            ? ERigElementType::Socket
                            : bMetadataAction
                                ? MetadataElementType.GetValue()
                                : ERigElementType::Control;
                const TArray<FRigElementKey> Existing =
                    KeysNamed(Operation.Name, ElementType);
                const bool bAdd = Action.StartsWith(TEXT("add_"));
                const bool bAddElement = bAdd && !bAvailableSpaceAction;
                if (!IsSafeGraphName(Operation.Name)
                    || (bAddElement && !Existing.IsEmpty())
                    || (!bAddElement && Existing.Num() != 1))
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("invalid_control_rig_element"), Operation.Name);
                }
                if (bAddElement && !KeysNamed(
                        Operation.Name, ERigElementType::All).IsEmpty())
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("duplicate_control_rig_element_name"),
                        Operation.Name);
                }
                if (Action.StartsWith(TEXT("rename_")))
                {
                    if (!IsSafeGraphName(Operation.NewName)
                        || !KeysNamed(
                            Operation.NewName,
                            ERigElementType::All).IsEmpty())
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_new_name"),
                            Operation.NewName);
                    }
                }
                if (bNullAction)
                {
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    const bool bTransformAction = bAdd
                        || Action == TEXT("set_control_rig_null_transform");
                    if (bTransformAction
                        && (!RequestedTransform.IsValid()
                            || Operation.Location.GetAbsMax() > 1000000.0
                            || Operation.Rotation.Euler().GetAbsMax() > 360000.0
                            || Operation.Scale.GetAbsMax() > 10000.0
                            || Operation.Scale.GetAbsMin() < 0.0001))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_null_transform"),
                            RequestedTransform.ToString());
                    }
                    if (bAdd && !Operation.ParentName.IsEmpty())
                    {
                        TArray<FRigElementKey> ParentMatches;
                        for (const FRigElementKey& Key : AllKeys)
                        {
                            if (Key.Name == FName(*Operation.ParentName)
                                && (Key.Type == ERigElementType::Bone
                                    || Key.Type == ERigElementType::Null))
                            {
                                ParentMatches.Add(Key);
                            }
                        }
                        if (ParentMatches.Num() != 1)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_or_ambiguous_control_rig_parent"),
                                Operation.ParentName);
                        }
                    }
                    if (!bAdd)
                    {
                        const FString CurrentValue =
                            PlannedControlRigHierarchy->GetLocalTransform(
                                Existing[0], true).ToString();
                        if (Operation.ExpectedValue != CurrentValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_value_conflict"),
                                CurrentValue);
                        }
                    }
                }
                else if (bCurveAction)
                {
                    if ((bAdd
                            || Action
                                == TEXT("set_control_rig_curve_value"))
                        && (!FMath::IsFinite(Operation.Value)
                            || FMath::Abs(Operation.Value) > 1000000.0f))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_curve_value"),
                            FString::SanitizeFloat(Operation.Value));
                    }
                    if (!bAdd)
                    {
                        const FString CurrentValue =
                            FString::SanitizeFloat(
                                PlannedControlRigHierarchy->GetCurveValue(
                                    Existing[0]));
                        if (Operation.ExpectedValue != CurrentValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_value_conflict"),
                                CurrentValue);
                        }
                    }
                }
                else if (bSocketAction)
                {
                    const bool bTransformAction = bAdd
                        || Action
                            == TEXT("set_control_rig_socket_transform");
                    const bool bSettingsAction = bAdd
                        || Action
                            == TEXT("set_control_rig_socket_settings");
                    if (bTransformAction
                        && !IsValidControlRigTransform(Operation))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_socket_transform"),
                            Operation.Name);
                    }
                    if (bSettingsAction
                        && (!IsValidControlRigColor(Operation.Color)
                            || !IsSafeControlRigDescription(
                                Operation.Description)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_socket_settings"),
                            Operation.Name);
                    }
                    if (bAdd && !Operation.ParentName.IsEmpty())
                    {
                        TArray<FRigElementKey> ParentMatches;
                        for (const FRigElementKey& Key : AllKeys)
                        {
                            if (Key.Name == FName(*Operation.ParentName)
                                && PlannedControlRigHierarchy
                                    ->Find<FRigTransformElement>(Key))
                            {
                                ParentMatches.Add(Key);
                            }
                        }
                        if (ParentMatches.Num() != 1)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_or_ambiguous_control_rig_parent"),
                                Operation.ParentName);
                        }
                    }
                    if (!bAdd)
                    {
                        FString CurrentValue;
                        if (Action
                            == TEXT("set_control_rig_socket_transform"))
                        {
                            CurrentValue = PlannedControlRigHierarchy
                                ->GetLocalTransform(Existing[0], true)
                                .ToString();
                        }
                        else if (Action
                            == TEXT("set_control_rig_socket_settings"))
                        {
                            CurrentValue =
                                ControlRigSocketSettingsString(
                                    PlannedControlRigHierarchy,
                                    PlannedControlRigHierarchy
                                        ->Find<FRigSocketElement>(
                                            Existing[0]));
                        }
                        else
                        {
                            CurrentValue = ControlRigSocketStateString(
                                PlannedControlRigHierarchy, Existing[0]);
                        }
                        if (Operation.ExpectedValue != CurrentValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("control_rig_value_conflict"),
                                CurrentValue);
                        }
                    }
                }
                else if (bAvailableSpaceAction)
                {
                    const TOptional<ERigElementType> SpaceType =
                        ParseControlRigTransformElementType(
                            Operation.SpaceType);
                    if (!SpaceType.IsSet()
                        || !IsSafeGraphName(Operation.SpaceName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_available_space"),
                            Operation.SpaceName);
                    }
                    const FRigElementKey SpaceKey(
                        FName(*Operation.SpaceName),
                        SpaceType.GetValue());
                    const FRigTransformElement* SpaceElement =
                        PlannedControlRigHierarchy
                            ->Find<FRigTransformElement>(SpaceKey);
                    FRigControlElement* ControlElement =
                        PlannedControlRigHierarchy
                            ->Find<FRigControlElement>(Existing[0]);
                    if (!SpaceElement || !ControlElement
                        || SpaceKey == Existing[0])
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_available_space"),
                            Operation.SpaceName);
                    }
                    const FRigControlSettings Settings =
                        PlannedControlRigController->GetControlSettings(
                            Existing[0]);
                    const FString CurrentValue =
                        ControlRigAvailableSpacesString(Settings);
                    if (Operation.ExpectedValue != CurrentValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("control_rig_available_spaces_conflict"),
                            CurrentValue);
                    }
                    const int32 ExistingSpaceIndex =
                        Settings.Customization.AvailableSpaces
                            .IndexOfByKey(SpaceKey);
                    if ((Action
                            == TEXT("add_control_rig_available_space")
                            && (ExistingSpaceIndex != INDEX_NONE
                                || PlannedControlRigHierarchy
                                    ->GetParents(Existing[0])
                                    .Contains(SpaceKey)))
                        || (Action
                                != TEXT("add_control_rig_available_space")
                            && ExistingSpaceIndex == INDEX_NONE))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_available_space_state"),
                            Operation.SpaceName);
                    }
                    const bool bLabelAction = Action
                            == TEXT("add_control_rig_available_space")
                        || Action
                            == TEXT("set_control_rig_available_space_label");
                    if (bLabelAction
                        && !Operation.DisplayLabel.IsEmpty()
                        && !IsSafeGraphName(Operation.DisplayLabel))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_available_space_label"),
                            Operation.DisplayLabel);
                    }
                    if (Action
                        == TEXT("set_control_rig_available_space_index"))
                    {
                        if (Operation.Index < 0
                            || Operation.Index
                                >= Settings.Customization
                                    .AvailableSpaces.Num()
                            || Operation.Index == ExistingSpaceIndex)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_control_rig_available_space_index"),
                                FString::FromInt(Operation.Index));
                        }
                    }
                    if (Action
                            == TEXT("set_control_rig_available_space_label")
                        && Settings.Customization.AvailableSpaces[
                                ExistingSpaceIndex].Label
                            == FName(*Operation.DisplayLabel))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("control_rig_available_space_noop"),
                            Operation.DisplayLabel);
                    }
                }
                else if (bMetadataAction)
                {
                    const TOptional<ERigMetadataType> MetadataType =
                        ParseControlRigMetadataType(
                            Operation.MetadataType);
                    const FName MetadataName(*Operation.MetadataName);
                    if (!MetadataType.IsSet()
                        || !IsSafeGraphName(Operation.MetadataName)
                        || MetadataName == FRigSocketElement::ColorMetaName
                        || MetadataName
                            == FRigSocketElement::DescriptionMetaName
                        || MetadataName
                            == FRigSocketElement::DesiredParentMetaName)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_metadata"),
                            Operation.MetadataName);
                    }
                    const ERigMetadataType CurrentType =
                        PlannedControlRigHierarchy->GetMetadataType(
                            Existing[0], MetadataName);
                    const FString CurrentValue =
                        ControlRigMetadataEntryString(
                            PlannedControlRigHierarchy,
                            Existing[0],
                            MetadataName);
                    if (Operation.ExpectedValue != CurrentValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("control_rig_metadata_conflict"),
                            CurrentValue);
                    }
                    if (Action == TEXT("set_control_rig_metadata"))
                    {
                        if ((CurrentType != ERigMetadataType::Invalid
                                && CurrentType
                                    != MetadataType.GetValue())
                            || !IsValidControlRigMetadataValue(
                                PlannedControlRigHierarchy,
                                Operation.MetadataValue,
                                MetadataType.GetValue()))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_control_rig_metadata_value"),
                                Operation.MetadataType);
                        }
                    }
                    else if (CurrentType != MetadataType.GetValue())
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_control_rig_metadata_state"),
                            CurrentValue);
                    }
                }
                else if (bTypedControlAction)
                {
                    const TOptional<FControlRigTypedControlPreflightError>
                        PreflightError = ValidateControlRigTypedControlOperation(
                            Operation,
                            ControlRigAsset,
                            PlannedControlRigHierarchy,
                            PlannedControlRigController,
                            Existing,
                            AllKeys,
                            bAdd,
                            bFloatControlAction);
                    if (PreflightError.IsSet())
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            PreflightError->Code,
                            PreflightError->Message);
                    }
                }
                Result.Preview.Add((Action + TEXT(":")
                    + Operation.Name + TEXT("->") + Operation.NewName
                    + TEXT(":") + Operation.ExpectedValue).Left(500));
                continue;
            }
            if (bIKRigAction)
            {
                if (!IKRig || PlannedIKBones.IsEmpty())
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("operation_requires_initialized_ik_rig"),
                        Action);
                }
                if (Action == TEXT("set_ik_root_motion_bone")
                    || Action == TEXT("set_ik_bone_excluded"))
                {
                    if (!PlannedIKBones.Contains(
                            FName(*Operation.BoneName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_bone_not_found"),
                            Operation.BoneName);
                    }
                }
                else if (Action == TEXT("set_ik_limb_solver_settings"))
                {
                    UIKRigLimbSolverController* LimbController =
                        PlannedIKController
                            && !bPlannedIKSolverStackChanged
                        ? Cast<UIKRigLimbSolverController>(
                            PlannedIKController->GetSolverController(
                                Operation.Index))
                        : nullptr;
                    const bool bValidSettings = LimbController
                        && FMath::IsFinite(Operation.IKReachPrecision)
                        && Operation.IKReachPrecision >= 0.001f
                        && Operation.IKReachPrecision <= 100.0f
                        && Operation.IKMaxIterations >= 1
                        && Operation.IKMaxIterations <= 64
                        && FMath::IsFinite(
                            Operation.IKMinRotationAngle)
                        && Operation.IKMinRotationAngle >= 0.0f
                        && Operation.IKMinRotationAngle <= 180.0f
                        && FMath::IsFinite(
                            Operation.IKPullDistribution)
                        && Operation.IKPullDistribution >= 0.0f
                        && Operation.IKPullDistribution <= 1.0f
                        && FMath::IsFinite(
                            Operation.IKReachStepAlpha)
                        && Operation.IKReachStepAlpha >= 0.0f
                        && Operation.IKReachStepAlpha <= 1.0f
                        && ParseIKAxis(
                            Operation.IKHingeRotationAxis).IsSet()
                        && ParseIKAxis(
                            Operation.IKEndBoneForwardAxis).IsSet();
                    if (!bValidSettings)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_limb_solver_settings"),
                            FString::FromInt(Operation.Index));
                    }
                }
                else if (Action == TEXT("set_ik_solver_setting")
                    || Action == TEXT("set_ik_goal_setting")
                    || Action == TEXT("set_ik_bone_setting"))
                {
                    const EIKRigSettingScope Scope =
                        Action == TEXT("set_ik_solver_setting")
                            ? EIKRigSettingScope::Solver
                            : (Action == TEXT("set_ik_goal_setting")
                                ? EIKRigSettingScope::Goal
                                : EIKRigSettingScope::Bone);
                    const FName Subject = Scope == EIKRigSettingScope::Goal
                        ? FName(*Operation.GoalName)
                        : (Scope == EIKRigSettingScope::Bone
                            ? FName(*Operation.BoneName)
                            : NAME_None);
                    FIKRigSettingsSnapshot Snapshot;
                    FProperty* Property = nullptr;
                    FString CanonicalValue;
                    if (bPlannedIKSolverStackChanged
                        || !GetIKRigSettingsSnapshot(
                            PlannedIKController,
                            Scope,
                            Operation.Index,
                            Subject,
                            Snapshot)
                        || !(Property = FindIKRigSettingProperty(
                            Snapshot, Operation.PropertyName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_setting_not_found"),
                            Operation.PropertyName);
                    }
                    const FString CurrentValue =
                        ExportIKRigSettingValue(Snapshot, Property);
                    if (CurrentValue != Operation.ExpectedValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_setting_precondition_failed"),
                            CurrentValue);
                    }
                    if (!ImportIKRigSettingValue(
                            Snapshot,
                            Property,
                            Operation.SettingValue,
                            CanonicalValue))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_setting_value"),
                            Operation.SettingValue);
                    }
                }
                else if (Action == TEXT("add_ik_solver"))
                {
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !IsAvailableIKRigSolverTypePath(
                            Operation.ClassPath))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_solver_type"),
                            Operation.ClassPath);
                    }
                    bPlannedIKSolverStackChanged = true;
                }
                else if (Action == TEXT("remove_ik_solver"))
                {
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.Index))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_solver_not_found"),
                            FString::FromInt(Operation.Index));
                    }
                    PlannedIKSolverGoals.RemoveAt(
                        Operation.Index, 1, EAllowShrinking::No);
                    bPlannedIKSolverStackChanged = true;
                }
                else if (Action == TEXT("set_ik_solver_enabled"))
                {
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.Index))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_solver_not_found"),
                            FString::FromInt(Operation.Index));
                    }
                }
                else if (Action == TEXT("set_ik_solver_start_bone"))
                {
                    FIKRigSolverBase* Solver = PlannedIKController
                        && !bPlannedIKSolverStackChanged
                        ? PlannedIKController->GetSolverAtIndex(
                            Operation.Index) : nullptr;
                    if (!Solver || !Solver->UsesStartBone()
                        || !PlannedIKBones.Contains(
                            FName(*Operation.BoneName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_solver_start_bone"),
                            Operation.BoneName);
                    }
                }
                else if (Action == TEXT("set_ik_solver_end_bone"))
                {
                    FIKRigSolverBase* Solver = PlannedIKController
                        && !bPlannedIKSolverStackChanged
                        ? PlannedIKController->GetSolverAtIndex(
                            Operation.Index) : nullptr;
                    if (!Solver || !Solver->UsesEndBone()
                        || !PlannedIKBones.Contains(
                            FName(*Operation.BoneName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_solver_end_bone"),
                            Operation.BoneName);
                    }
                }
                else if (Action == TEXT("move_ik_solver"))
                {
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.Index)
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.TargetIndex)
                        || Operation.Index == Operation.TargetIndex)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_solver_move"),
                            FString::Printf(TEXT("%d:%d"),
                                Operation.Index,
                                Operation.TargetIndex));
                    }
                    bPlannedIKSolverStackChanged = true;
                }
                else if (Action == TEXT("add_ik_bone_setting"))
                {
                    FText Error;
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !PlannedIKBones.Contains(
                            FName(*Operation.BoneName))
                        || !PlannedIKController->CanAddBoneSetting(
                            FName(*Operation.BoneName),
                            Operation.Index,
                            &Error))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_bone_setting_add"),
                            Error.ToString());
                    }
                }
                else if (Action == TEXT("remove_ik_bone_setting"))
                {
                    if (!PlannedIKController
                        || bPlannedIKSolverStackChanged
                        || !PlannedIKController->CanRemoveBoneSetting(
                            FName(*Operation.BoneName),
                            Operation.Index))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_bone_setting_not_found"),
                            Operation.BoneName);
                    }
                }
                else if (Action == TEXT("connect_ik_goal_to_solver"))
                {
                    const FName GoalName(*Operation.GoalName);
                    if (bPlannedIKSolverStackChanged
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.Index)
                        || !PlannedIKGoals.Contains(GoalName)
                        || PlannedIKSolverGoals[Operation.Index].Contains(
                            GoalName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_solver_goal_connection"),
                            Operation.GoalName);
                    }
                    PlannedIKSolverGoals[Operation.Index].Add(GoalName);
                }
                else if (Action == TEXT("disconnect_ik_goal_from_solver"))
                {
                    const FName GoalName(*Operation.GoalName);
                    if (bPlannedIKSolverStackChanged
                        || !PlannedIKSolverGoals.IsValidIndex(
                            Operation.Index)
                        || !PlannedIKSolverGoals[Operation.Index].Remove(
                            GoalName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_solver_goal_connection_not_found"),
                            Operation.GoalName);
                    }
                }
                else if (Action == TEXT("set_ik_retarget_root"))
                {
                    if (!PlannedIKBones.Contains(FName(*Operation.BoneName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_bone_not_found"), Operation.BoneName);
                    }
                }
                else if (Action == TEXT("add_ik_goal"))
                {
                    const FName GoalName(*Operation.Name);
                    if (!IsSafeGraphName(Operation.Name)
                        || PlannedIKGoals.Contains(GoalName)
                        || !PlannedIKBones.Contains(
                            FName(*Operation.BoneName)))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_or_duplicate_ik_goal"),
                            Operation.Name);
                    }
                    PlannedIKGoals.Add(GoalName);
                }
                else if (Action == TEXT("remove_ik_goal"))
                {
                    const FName GoalName(*Operation.Name);
                    if (!PlannedIKGoals.Contains(GoalName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_goal_not_found"), Operation.Name);
                    }
                    if (PlannedIKChainGoals.FindKey(GoalName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_goal_still_used_by_chain"),
                            Operation.Name);
                    }
                    if (PlannedIKSolverGoals.ContainsByPredicate(
                            [&](const TSet<FName>& SolverGoals)
                            {
                                return SolverGoals.Contains(GoalName);
                            }))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_goal_still_used_by_solver"),
                            Operation.Name);
                    }
                    PlannedIKGoals.Remove(GoalName);
                }
                else if (Action == TEXT("add_ik_retarget_chain"))
                {
                    const FName ChainName(*Operation.Name);
                    const FName StartBone(*Operation.StartBoneName);
                    const FName EndBone(*Operation.EndBoneName);
                    const FName GoalName(*Operation.GoalName);
                    const bool bGoalValid = GoalName.IsNone()
                        || PlannedIKGoals.Contains(GoalName);
                    if (!IsSafeGraphName(Operation.Name)
                        || PlannedIKRetargetChains.Contains(ChainName)
                        || !PlannedIKBones.Contains(StartBone)
                        || !PlannedIKBones.Contains(EndBone)
                        || !bGoalValid
                        || !IKRig->GetSkeleton().IsBoneInDirectLineage(
                            EndBone, StartBone))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_or_duplicate_ik_retarget_chain"),
                            Operation.Name);
                    }
                    PlannedIKRetargetChains.Add(ChainName);
                    PlannedIKChainGoals.Add(ChainName, GoalName);
                }
                else
                {
                    const FName ChainName(*Operation.Name);
                    if (!PlannedIKRetargetChains.Remove(ChainName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_retarget_chain_not_found"),
                            Operation.Name);
                    }
                    PlannedIKChainGoals.Remove(ChainName);
                }
                Result.Preview.Add(Action + TEXT(":") + Operation.Name
                    + Operation.BoneName + Operation.StartBoneName
                    + Operation.EndBoneName + Operation.GoalName
                    + Operation.ClassPath + Operation.PropertyName
                    + Operation.ExpectedValue + Operation.SettingValue
                    + FString::FromInt(Operation.Index)
                    + TEXT(":") + FString::FromInt(Operation.TargetIndex));
                continue;
            }
            if (bIKRetargeterAction)
            {
                if (!IKRetargeter || !PlannedRetargetController)
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("operation_requires_ik_retargeter"), Action);
                }
                if (Action == TEXT("set_ik_retargeter_rig"))
                {
                    const TOptional<ERetargetSourceOrTarget> Side =
                        ParseRetargetSide(Operation.RetargetSide);
                    UIKRigDefinition* ReferencedRig =
                        Cast<UIKRigDefinition>(
                            FindAsset(Operation.ReferencedAssetPath));
                    if (!Side.IsSet()
                        || !IsAllowedPath(Operation.ReferencedAssetPath)
                        || !ReferencedRig
                        || ReferencedRig->GetSkeleton().BoneNames.IsEmpty()
                        || RetargetRigPath(
                            PlannedRetargetController,
                            Side.GetValue()) != Operation.ExpectedValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retargeter_rig_edit"),
                            Operation.ReferencedAssetPath);
                    }
                }
                else if (Action == TEXT("add_default_ik_retarget_ops"))
                {
                    if (bPlannedRetargetOpStackChanged
                        || PlannedRetargetOpCount != 0
                        || Operation.ExpectedValue != TEXT("0"))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_retarget_default_ops_require_empty_stack"),
                            FString::FromInt(PlannedRetargetOpCount));
                    }
                    PlannedRetargetOpCount = 5;
                    bPlannedRetargetOpStackChanged = true;
                }
                else if (Action == TEXT("add_ik_retarget_op"))
                {
                    if (bPlannedRetargetOpStackChanged
                        || !ResolveIKRetargetOpType(Operation.ClassPath))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_op_type"),
                            Operation.ClassPath);
                    }
                    ++PlannedRetargetOpCount;
                    bPlannedRetargetOpStackChanged = true;
                }
                else if (Action == TEXT("remove_ik_retarget_op")
                    || Action == TEXT("rename_ik_retarget_op")
                    || Action == TEXT("move_ik_retarget_op")
                    || Action == TEXT("set_ik_retarget_op_enabled"))
                {
                    if (bPlannedRetargetOpStackChanged
                        || !PlannedRetargetOpNames.IsValidIndex(
                            Operation.Index))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_retarget_op_not_found"),
                            FString::FromInt(Operation.Index));
                    }
                    if (Action == TEXT("rename_ik_retarget_op"))
                    {
                        const FName NewName(*Operation.NewName);
                        if (!IsSafeGraphName(Operation.NewName)
                            || PlannedRetargetOpNames.Contains(NewName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_ik_retarget_op_name"),
                                Operation.NewName);
                        }
                        PlannedRetargetOpNames[Operation.Index] = NewName;
                    }
                    else if (Action == TEXT("move_ik_retarget_op"))
                    {
                        if (!PlannedRetargetOpNames.IsValidIndex(
                                Operation.TargetIndex)
                            || Operation.Index == Operation.TargetIndex)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_ik_retarget_op_move"),
                                FString::Printf(TEXT("%d:%d"),
                                    Operation.Index,
                                    Operation.TargetIndex));
                        }
                        bPlannedRetargetOpStackChanged = true;
                    }
                    else if (Action == TEXT("remove_ik_retarget_op"))
                    {
                        --PlannedRetargetOpCount;
                        bPlannedRetargetOpStackChanged = true;
                    }
                }
                else if (Action == TEXT("add_ik_retarget_override_set"))
                {
                    const FName SetName(*Operation.Name);
                    if (!IsSafeGraphName(Operation.Name)
                        || PlannedOverrideSets.Contains(SetName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_or_duplicate_ik_retarget_override_set"),
                            Operation.Name);
                    }
                    PlannedOverrideSets.Add(SetName);
                    PlannedOverrideSetParents.Add(SetName, NAME_None);
                }
                else if (Action == TEXT("rename_ik_retarget_override_set"))
                {
                    const FName SetName(*Operation.Name);
                    const FName NewName(*Operation.NewName);
                    if (!PlannedOverrideSets.Contains(SetName)
                        || !IsSafeGraphName(Operation.NewName)
                        || PlannedOverrideSets.Contains(NewName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_override_set_rename"),
                            Operation.Name);
                    }
                    const FName Parent =
                        PlannedOverrideSetParents.FindRef(SetName);
                    PlannedOverrideSets.Remove(SetName);
                    PlannedOverrideSetParents.Remove(SetName);
                    PlannedOverrideSets.Add(NewName);
                    PlannedOverrideSetParents.Add(NewName, Parent);
                    for (TPair<FName, FName>& Pair :
                         PlannedOverrideSetParents)
                    {
                        if (Pair.Value == SetName)
                        {
                            Pair.Value = NewName;
                        }
                    }
                }
                else if (Action == TEXT("remove_ik_retarget_override_set"))
                {
                    const FName SetName(*Operation.Name);
                    if (!PlannedOverrideSets.Contains(SetName)
                        || PlannedOverrideSetParents.FindKey(SetName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_retarget_override_set_missing_or_parent"),
                            Operation.Name);
                    }
                    PlannedOverrideSets.Remove(SetName);
                    PlannedOverrideSetParents.Remove(SetName);
                }
                else if (Action == TEXT("set_ik_retarget_override_set_active"))
                {
                    const FName SetName(*Operation.Name);
                    const FString CurrentValue =
                        PlannedRetargetController
                            ->GetOverrideSetActiveByDefault(SetName)
                        ? TEXT("true") : TEXT("false");
                    if (!PlannedOverrideSets.Contains(SetName)
                        || !PlannedRetargetController
                            ->GetAllOverrideSets().Contains(SetName)
                        || CurrentValue != Operation.ExpectedValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_override_set_active"),
                            CurrentValue);
                    }
                }
                else if (Action == TEXT("set_ik_retarget_override_set_parent"))
                {
                    const FName SetName(*Operation.Name);
                    const FName ParentName = Operation.ParentName.IsEmpty()
                        ? NAME_None : FName(*Operation.ParentName);
                    const FName CurrentParent =
                        PlannedOverrideSetParents.FindRef(SetName);
                    if (!PlannedOverrideSets.Contains(SetName)
                        || (!ParentName.IsNone()
                            && (!PlannedOverrideSets.Contains(ParentName)
                                || ParentName == SetName))
                        || CurrentParent.ToString() != Operation.ExpectedValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_override_set_parent"),
                            CurrentParent.ToString());
                    }
                    PlannedOverrideSetParents.FindOrAdd(SetName) = ParentName;
                }
                else if (Action == TEXT("set_ik_retarget_property_override"))
                {
                    const FName SetName(*Operation.Name);
                    const FName OpName(*Operation.RetargetOpName);
                    FIKRigSettingsSnapshot Snapshot;
                    FProperty* Property = nullptr;
                    FString CanonicalValue;
                    if (bPlannedRetargetOpStackChanged
                        || !PlannedRetargetController
                            ->GetAllOverrideSets().Contains(SetName)
                        || !GetIKRetargetOpSettingsSnapshot(
                            PlannedRetargetController, OpName, Snapshot)
                        || !(Property = FindIKRigSettingProperty(
                            Snapshot, Operation.PropertyName))
                        || CurrentIKRetargetPropertyValue(
                            PlannedRetargetController,
                            SetName,
                            OpName,
                            Operation.PropertyName,
                            Snapshot,
                            Property) != Operation.ExpectedValue
                        || !ImportIKRigSettingValue(
                            Snapshot,
                            Property,
                            Operation.SettingValue,
                            CanonicalValue))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_property_override"),
                            Operation.PropertyName);
                    }
                }
                else if (Action == TEXT("remove_ik_retarget_property_override"))
                {
                    const FName SetName(*Operation.Name);
                    const FName OpName(*Operation.RetargetOpName);
                    FIKRigSettingsSnapshot Snapshot;
                    FProperty* Property = nullptr;
                    if (!PlannedRetargetController->HasPropertyOverride(
                            SetName, OpName, Operation.PropertyName)
                        || !GetIKRetargetOpSettingsSnapshot(
                            PlannedRetargetController, OpName, Snapshot)
                        || !(Property = FindIKRigSettingProperty(
                            Snapshot, Operation.PropertyName))
                        || CurrentIKRetargetPropertyValue(
                            PlannedRetargetController,
                            SetName,
                            OpName,
                            Operation.PropertyName,
                            Snapshot,
                            Property) != Operation.ExpectedValue)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("ik_retarget_property_override_not_found"),
                            Operation.PropertyName);
                    }
                }
                else
                {
                    const TOptional<ERetargetSourceOrTarget> Side =
                        ParseRetargetSide(Operation.RetargetSide);
                    if (!Side.IsSet())
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_ik_retarget_side"),
                            Operation.RetargetSide);
                    }
                    TSet<FName>& Poses =
                        Side.GetValue() == ERetargetSourceOrTarget::Source
                            ? PlannedSourcePoses : PlannedTargetPoses;
                    FName& CurrentPose =
                        Side.GetValue() == ERetargetSourceOrTarget::Source
                            ? PlannedSourceCurrentPose
                            : PlannedTargetCurrentPose;
                    const FName PoseName(*Operation.Name);
                    const FName NewName(*Operation.NewName);
                    if (Action == TEXT("add_ik_retarget_pose"))
                    {
                        if (!IsSafeGraphName(Operation.Name)
                            || Poses.Contains(PoseName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_or_duplicate_ik_retarget_pose"),
                                Operation.Name);
                        }
                        Poses.Add(PoseName);
                    }
                    else if (Action == TEXT("rename_ik_retarget_pose"))
                    {
                        if (!Poses.Contains(PoseName)
                            || PoseName == UIKRetargeter::GetDefaultPoseName()
                            || !IsSafeGraphName(Operation.NewName)
                            || Poses.Contains(NewName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_ik_retarget_pose_rename"),
                                Operation.Name);
                        }
                        Poses.Remove(PoseName);
                        Poses.Add(NewName);
                        if (CurrentPose == PoseName)
                        {
                            CurrentPose = NewName;
                        }
                    }
                    else if (Action == TEXT("remove_ik_retarget_pose"))
                    {
                        if (!Poses.Contains(PoseName)
                            || PoseName == UIKRetargeter::GetDefaultPoseName())
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("ik_retarget_pose_not_found_or_default"),
                                Operation.Name);
                        }
                        Poses.Remove(PoseName);
                        if (CurrentPose == PoseName)
                        {
                            CurrentPose = UIKRetargeter::GetDefaultPoseName();
                        }
                    }
                    else if (Action == TEXT("set_current_ik_retarget_pose"))
                    {
                        if (!Poses.Contains(PoseName)
                            || !IKRetargeter->GetRetargetPoseByName(
                                Side.GetValue(), PoseName)
                            || CurrentPose.ToString() != Operation.ExpectedValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_current_ik_retarget_pose"),
                                CurrentPose.ToString());
                        }
                        CurrentPose = PoseName;
                    }
                    else if (Action == TEXT("set_ik_retarget_pose_root_offset"))
                    {
                        const FString CurrentOffset =
                            PlannedRetargetController
                                ->GetRootOffsetInRetargetPose(
                                    Side.GetValue()).ToString();
                        if (CurrentPose.IsNone()
                            || Operation.Location.ContainsNaN()
                            || !FMath::IsNearlyZero(Operation.Location.X)
                            || !FMath::IsNearlyZero(Operation.Location.Y)
                            || CurrentOffset != Operation.ExpectedValue)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_ik_retarget_pose_root_offset"),
                                FString::Printf(
                                    TEXT("current=%s; only the controller-persisted Z axis is supported (X and Y must be zero)"),
                                    *CurrentOffset));
                        }
                    }
                }
                Result.Preview.Add((Action + TEXT(":")
                    + Operation.RetargetSide + TEXT(":")
                    + Operation.Name + TEXT(":")
                    + Operation.RetargetOpName + TEXT(":")
                    + Operation.PropertyName + TEXT(":")
                    + Operation.ExpectedValue + TEXT("->")
                    + Operation.SettingValue + Operation.ReferencedAssetPath
                    + FString::FromInt(Operation.Index) + TEXT(":")
                    + FString::FromInt(Operation.TargetIndex)).Left(500));
                continue;
            }
            if (IKRig)
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("operation_not_supported_for_ik_rig"), Action);
            }
            if (IKRetargeter)
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("operation_not_supported_for_ik_retargeter"), Action);
            }
            if (ControlRigAsset)
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("operation_not_supported_for_control_rig"), Action);
            }
            const bool bStateMachineAction = Action.Contains(TEXT("state_machine"))
                || Action == TEXT("add_state")
                || Action == TEXT("rename_state")
                || Action == TEXT("remove_state")
                || Action == TEXT("set_entry_state")
                || Action == TEXT("add_transition")
                || Action == TEXT("set_transition")
                || Action == TEXT("remove_transition")
                || Action == TEXT("set_state_sequence_player")
                || Action == TEXT("remove_state_sequence_player")
                || Action == TEXT("set_state_blend_by_bool")
                || Action == TEXT("remove_state_blend_by_bool")
                || Action == TEXT("set_state_blend_space_player")
                || Action == TEXT("remove_state_blend_space_player")
                || Action == TEXT("set_state_blend_list_by_int")
                || Action == TEXT("remove_state_blend_list_by_int")
                || Action == TEXT("set_state_apply_additive")
                || Action == TEXT("remove_state_apply_additive")
                || Action == TEXT("set_state_layered_blend_per_bone")
                || Action == TEXT("remove_state_layered_blend_per_bone")
                || Action == TEXT("set_transition_rule_constant")
                || Action == TEXT("set_transition_rule_variable")
                || Action == TEXT("remove_transition_rule_variable")
                || Action == TEXT("set_transition_rule_time_remaining")
                || Action == TEXT("remove_transition_rule_time_remaining")
                || Action == TEXT("set_transition_rule_expression")
                || Action == TEXT("remove_transition_rule_expression")
                || IsGenericStateGraphAction(Action);
            if (bStateMachineAction)
            {
                if (!AnimBlueprint)
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("operation_requires_anim_blueprint"), Action);
                }
                if (!IsSafeGraphName(Operation.MachineName))
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("invalid_state_machine_name"), Operation.MachineName);
                }
                if (IsGenericStateGraphAction(Action))
                {
                    if (Request.Operations.Num() != 1)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("state_graph_single_operation_required"),
                            FString::FromInt(Request.Operations.Num()));
                    }
                    if (!IsSafeGraphName(Operation.StateName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_state_name"), Operation.StateName);
                    }
                    if (const TOptional<FStateGraphPreflightError> Error =
                            ValidateGenericStateGraphOperation(
                                Operation, AnimBlueprint))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            Error->Code, Error->Message);
                    }
                    Result.Preview.Add((Action + TEXT(":")
                        + Operation.MachineName + TEXT(":")
                        + Operation.StateName + TEXT(":")
                        + Operation.ClassPath + TEXT(":")
                        + Operation.StateGraphNodeGuid + TEXT(":")
                        + Operation.PropertyName + TEXT(":")
                        + Operation.SettingValue + TEXT(":")
                        + FString::FromInt(
                            Operation.StateGraphContainerSize) + TEXT(":")
                        + Operation.StateGraphPinGuid + TEXT(":")
                        + Operation.StateGraphSourcePinGuid + TEXT("->")
                        + Operation.StateGraphTargetPinGuid).Left(500));
                    continue;
                }
                if (Action == TEXT("add_state_machine"))
                {
                    if (PlannedStates.Contains(Operation.MachineName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("state_machine_already_exists"), Operation.MachineName);
                    }
                    PlannedStates.Add(Operation.MachineName);
                    PlannedTransitions.Add(Operation.MachineName);
                    PlannedStatePlayers.Add(Operation.MachineName);
                    PlannedStateBoolBlends.Add(Operation.MachineName);
                    PlannedStateBlendSpaceDimensions.Add(Operation.MachineName);
                    PlannedStateIntBlendCounts.Add(Operation.MachineName);
                    PlannedStateApplyAdditives.Add(Operation.MachineName);
                    PlannedStateLayeredBlendCounts.Add(Operation.MachineName);
                    PlannedUnsupportedStatePose.Add(Operation.MachineName);
                    PlannedLinkedTransitionRules.Add(Operation.MachineName);
                    PlannedTransitionRuleVariables.Add(Operation.MachineName);
                    PlannedTransitionTimeRemainingRules.Add(
                        Operation.MachineName);
                    PlannedTransitionExpressionRules.Add(
                        Operation.MachineName);
                }
                else if (Action == TEXT("rename_state_machine"))
                {
                    if (!PlannedStates.Contains(Operation.MachineName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("state_machine_not_found"), Operation.MachineName);
                    }
                    if (!IsSafeGraphName(Operation.NewName)
                        || PlannedStates.Contains(Operation.NewName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("invalid_new_state_machine_name"), Operation.NewName);
                    }
                    TSet<FString> States = MoveTemp(PlannedStates.FindChecked(Operation.MachineName));
                    TSet<FString> Transitions = MoveTemp(
                        PlannedTransitions.FindChecked(Operation.MachineName));
                    TSet<FString> StatePlayers = MoveTemp(
                        PlannedStatePlayers.FindChecked(Operation.MachineName));
                    TSet<FString> StateBoolBlends = MoveTemp(
                        PlannedStateBoolBlends.FindChecked(Operation.MachineName));
                    TMap<FString, int32> StateBlendSpaceDimensions = MoveTemp(
                        PlannedStateBlendSpaceDimensions.FindChecked(
                            Operation.MachineName));
                    TMap<FString, int32> StateIntBlendCounts = MoveTemp(
                        PlannedStateIntBlendCounts.FindChecked(
                            Operation.MachineName));
                    TSet<FString> StateApplyAdditives = MoveTemp(
                        PlannedStateApplyAdditives.FindChecked(
                            Operation.MachineName));
                    TMap<FString, int32> StateLayeredBlendCounts = MoveTemp(
                        PlannedStateLayeredBlendCounts.FindChecked(
                            Operation.MachineName));
                    TSet<FString> UnsupportedStatePose = MoveTemp(
                        PlannedUnsupportedStatePose.FindChecked(Operation.MachineName));
                    TSet<FString> LinkedTransitionRules = MoveTemp(
                        PlannedLinkedTransitionRules.FindChecked(Operation.MachineName));
                    TMap<FString, FString> TransitionRuleVariables = MoveTemp(
                        PlannedTransitionRuleVariables.FindChecked(Operation.MachineName));
                    TSet<FString> TransitionTimeRemainingRules = MoveTemp(
                        PlannedTransitionTimeRemainingRules.FindChecked(
                            Operation.MachineName));
                    TSet<FString> TransitionExpressionRules = MoveTemp(
                        PlannedTransitionExpressionRules.FindChecked(
                            Operation.MachineName));
                    PlannedStates.Remove(Operation.MachineName);
                    PlannedTransitions.Remove(Operation.MachineName);
                    PlannedStatePlayers.Remove(Operation.MachineName);
                    PlannedStateBoolBlends.Remove(Operation.MachineName);
                    PlannedStateBlendSpaceDimensions.Remove(Operation.MachineName);
                    PlannedStateIntBlendCounts.Remove(Operation.MachineName);
                    PlannedStateApplyAdditives.Remove(Operation.MachineName);
                    PlannedStateLayeredBlendCounts.Remove(Operation.MachineName);
                    PlannedUnsupportedStatePose.Remove(Operation.MachineName);
                    PlannedLinkedTransitionRules.Remove(Operation.MachineName);
                    PlannedTransitionRuleVariables.Remove(Operation.MachineName);
                    PlannedTransitionTimeRemainingRules.Remove(
                        Operation.MachineName);
                    PlannedTransitionExpressionRules.Remove(
                        Operation.MachineName);
                    PlannedStates.Add(Operation.NewName, MoveTemp(States));
                    PlannedTransitions.Add(Operation.NewName, MoveTemp(Transitions));
                    PlannedStatePlayers.Add(Operation.NewName, MoveTemp(StatePlayers));
                    PlannedStateBoolBlends.Add(
                        Operation.NewName, MoveTemp(StateBoolBlends));
                    PlannedStateBlendSpaceDimensions.Add(
                        Operation.NewName, MoveTemp(StateBlendSpaceDimensions));
                    PlannedStateIntBlendCounts.Add(
                        Operation.NewName, MoveTemp(StateIntBlendCounts));
                    PlannedStateApplyAdditives.Add(
                        Operation.NewName, MoveTemp(StateApplyAdditives));
                    PlannedStateLayeredBlendCounts.Add(
                        Operation.NewName, MoveTemp(StateLayeredBlendCounts));
                    PlannedUnsupportedStatePose.Add(
                        Operation.NewName, MoveTemp(UnsupportedStatePose));
                    PlannedLinkedTransitionRules.Add(
                        Operation.NewName, MoveTemp(LinkedTransitionRules));
                    PlannedTransitionRuleVariables.Add(
                        Operation.NewName, MoveTemp(TransitionRuleVariables));
                    PlannedTransitionTimeRemainingRules.Add(
                        Operation.NewName,
                        MoveTemp(TransitionTimeRemainingRules));
                    PlannedTransitionExpressionRules.Add(
                        Operation.NewName,
                        MoveTemp(TransitionExpressionRules));
                }
                else if (Action == TEXT("remove_state_machine"))
                {
                    if (!PlannedStates.Contains(Operation.MachineName))
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("state_machine_not_found"), Operation.MachineName);
                    }
                    PlannedStates.Remove(Operation.MachineName);
                    PlannedTransitions.Remove(Operation.MachineName);
                    PlannedStatePlayers.Remove(Operation.MachineName);
                    PlannedStateBoolBlends.Remove(Operation.MachineName);
                    PlannedStateBlendSpaceDimensions.Remove(Operation.MachineName);
                    PlannedStateIntBlendCounts.Remove(Operation.MachineName);
                    PlannedStateApplyAdditives.Remove(Operation.MachineName);
                    PlannedStateLayeredBlendCounts.Remove(Operation.MachineName);
                    PlannedUnsupportedStatePose.Remove(Operation.MachineName);
                    PlannedLinkedTransitionRules.Remove(Operation.MachineName);
                    PlannedTransitionRuleVariables.Remove(Operation.MachineName);
                    PlannedTransitionTimeRemainingRules.Remove(
                        Operation.MachineName);
                    PlannedTransitionExpressionRules.Remove(
                        Operation.MachineName);
                }
                else
                {
                    TSet<FString>* States = PlannedStates.Find(Operation.MachineName);
                    TSet<FString>* Transitions = PlannedTransitions.Find(Operation.MachineName);
                    TSet<FString>* StatePlayers =
                        PlannedStatePlayers.Find(Operation.MachineName);
                    TSet<FString>* StateBoolBlends =
                        PlannedStateBoolBlends.Find(Operation.MachineName);
                    TMap<FString, int32>* StateBlendSpaceDimensions =
                        PlannedStateBlendSpaceDimensions.Find(Operation.MachineName);
                    TMap<FString, int32>* StateIntBlendCounts =
                        PlannedStateIntBlendCounts.Find(Operation.MachineName);
                    TSet<FString>* StateApplyAdditives =
                        PlannedStateApplyAdditives.Find(Operation.MachineName);
                    TMap<FString, int32>* StateLayeredBlendCounts =
                        PlannedStateLayeredBlendCounts.Find(
                            Operation.MachineName);
                    TSet<FString>* UnsupportedStatePose =
                        PlannedUnsupportedStatePose.Find(Operation.MachineName);
                    TSet<FString>* LinkedTransitionRules =
                        PlannedLinkedTransitionRules.Find(Operation.MachineName);
                    TMap<FString, FString>* TransitionRuleVariables =
                        PlannedTransitionRuleVariables.Find(Operation.MachineName);
                    TSet<FString>* TransitionTimeRemainingRules =
                        PlannedTransitionTimeRemainingRules.Find(
                            Operation.MachineName);
                    TSet<FString>* TransitionExpressionRules =
                        PlannedTransitionExpressionRules.Find(
                            Operation.MachineName);
                    if (!States || !Transitions || !StatePlayers || !StateBoolBlends
                        || !StateBlendSpaceDimensions
                        || !StateIntBlendCounts
                        || !StateApplyAdditives
                        || !StateLayeredBlendCounts
                        || !UnsupportedStatePose || !LinkedTransitionRules
                        || !TransitionRuleVariables
                        || !TransitionTimeRemainingRules
                        || !TransitionExpressionRules)
                    {
                        return MakeError<FThomasAnimationPlanResult>(
                            TEXT("state_machine_not_found"), Operation.MachineName);
                    }
                    if (Action == TEXT("add_state"))
                    {
                        if (!IsSafeGraphName(Operation.StateName)
                            || States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_or_duplicate_state"), Operation.StateName);
                        }
                        States->Add(Operation.StateName);
                    }
                    else if (Action == TEXT("rename_state"))
                    {
                        if (!States->Contains(Operation.StateName)
                            || !IsSafeGraphName(Operation.NewName)
                            || States->Contains(Operation.NewName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_state_rename"), Operation.StateName);
                        }
                        States->Remove(Operation.StateName);
                        States->Add(Operation.NewName);
                        if (StatePlayers->Remove(Operation.StateName))
                        {
                            StatePlayers->Add(Operation.NewName);
                        }
                        if (StateBoolBlends->Remove(Operation.StateName))
                        {
                            StateBoolBlends->Add(Operation.NewName);
                        }
                        if (int32* Dimension =
                                StateBlendSpaceDimensions->Find(Operation.StateName))
                        {
                            const int32 SavedDimension = *Dimension;
                            StateBlendSpaceDimensions->Remove(Operation.StateName);
                            StateBlendSpaceDimensions->Add(
                                Operation.NewName, SavedDimension);
                        }
                        if (int32* PoseCount =
                                StateIntBlendCounts->Find(Operation.StateName))
                        {
                            const int32 SavedPoseCount = *PoseCount;
                            StateIntBlendCounts->Remove(Operation.StateName);
                            StateIntBlendCounts->Add(
                                Operation.NewName, SavedPoseCount);
                        }
                        if (StateApplyAdditives->Remove(Operation.StateName))
                        {
                            StateApplyAdditives->Add(Operation.NewName);
                        }
                        if (const int32* LayerCount =
                                StateLayeredBlendCounts->Find(
                                    Operation.StateName))
                        {
                            const int32 SavedLayerCount = *LayerCount;
                            StateLayeredBlendCounts->Remove(Operation.StateName);
                            StateLayeredBlendCounts->Add(
                                Operation.NewName, SavedLayerCount);
                        }
                        if (UnsupportedStatePose->Remove(Operation.StateName))
                        {
                            UnsupportedStatePose->Add(Operation.NewName);
                        }
                        TSet<FString> RenamedTransitions;
                        TSet<FString> RenamedLinkedTransitionRules;
                        TMap<FString, FString> RenamedTransitionRuleVariables;
                        TSet<FString> RenamedTransitionTimeRemainingRules;
                        TSet<FString> RenamedTransitionExpressionRules;
                        for (const FString& Key : *Transitions)
                        {
                            FString From;
                            FString To;
                            if (Key.Split(FString::Chr(0x1f), &From, &To))
                            {
                                if (From == Operation.StateName) From = Operation.NewName;
                                if (To == Operation.StateName) To = Operation.NewName;
                                RenamedTransitions.Add(TransitionKey(From, To));
                            }
                        }
                        for (const FString& Key : *LinkedTransitionRules)
                        {
                            FString From;
                            FString To;
                            if (Key.Split(FString::Chr(0x1f), &From, &To))
                            {
                                if (From == Operation.StateName) From = Operation.NewName;
                                if (To == Operation.StateName) To = Operation.NewName;
                                RenamedLinkedTransitionRules.Add(
                                    TransitionKey(From, To));
                            }
                        }
                        for (const TPair<FString, FString>& Pair : *TransitionRuleVariables)
                        {
                            FString From;
                            FString To;
                            if (Pair.Key.Split(FString::Chr(0x1f), &From, &To))
                            {
                                if (From == Operation.StateName) From = Operation.NewName;
                                if (To == Operation.StateName) To = Operation.NewName;
                                RenamedTransitionRuleVariables.Add(
                                    TransitionKey(From, To), Pair.Value);
                            }
                        }
                        for (const FString& Key :
                             *TransitionTimeRemainingRules)
                        {
                            FString From;
                            FString To;
                            if (Key.Split(FString::Chr(0x1f), &From, &To))
                            {
                                if (From == Operation.StateName)
                                    From = Operation.NewName;
                                if (To == Operation.StateName)
                                    To = Operation.NewName;
                                RenamedTransitionTimeRemainingRules.Add(
                                    TransitionKey(From, To));
                            }
                        }
                        for (const FString& Key :
                             *TransitionExpressionRules)
                        {
                            FString From;
                            FString To;
                            if (Key.Split(FString::Chr(0x1f), &From, &To))
                            {
                                if (From == Operation.StateName)
                                    From = Operation.NewName;
                                if (To == Operation.StateName)
                                    To = Operation.NewName;
                                RenamedTransitionExpressionRules.Add(
                                    TransitionKey(From, To));
                            }
                        }
                        *Transitions = MoveTemp(RenamedTransitions);
                        *LinkedTransitionRules = MoveTemp(
                            RenamedLinkedTransitionRules);
                        *TransitionRuleVariables = MoveTemp(
                            RenamedTransitionRuleVariables);
                        *TransitionTimeRemainingRules = MoveTemp(
                            RenamedTransitionTimeRemainingRules);
                        *TransitionExpressionRules = MoveTemp(
                            RenamedTransitionExpressionRules);
                    }
                    else if (Action == TEXT("remove_state"))
                    {
                        if (!States->Remove(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        StatePlayers->Remove(Operation.StateName);
                        StateBoolBlends->Remove(Operation.StateName);
                        StateBlendSpaceDimensions->Remove(Operation.StateName);
                        StateIntBlendCounts->Remove(Operation.StateName);
                        StateApplyAdditives->Remove(Operation.StateName);
                        StateLayeredBlendCounts->Remove(Operation.StateName);
                        UnsupportedStatePose->Remove(Operation.StateName);
                        for (auto It = Transitions->CreateIterator(); It; ++It)
                        {
                            FString From;
                            FString To;
                            It->Split(FString::Chr(0x1f), &From, &To);
                            if (From == Operation.StateName || To == Operation.StateName)
                            {
                                It.RemoveCurrent();
                            }
                        }
                        for (auto It = LinkedTransitionRules->CreateIterator(); It; ++It)
                        {
                            FString From;
                            FString To;
                            It->Split(FString::Chr(0x1f), &From, &To);
                            if (From == Operation.StateName || To == Operation.StateName)
                            {
                                It.RemoveCurrent();
                            }
                        }
                        for (auto It = TransitionRuleVariables->CreateIterator(); It; ++It)
                        {
                            FString From;
                            FString To;
                            It.Key().Split(FString::Chr(0x1f), &From, &To);
                            if (From == Operation.StateName || To == Operation.StateName)
                            {
                                It.RemoveCurrent();
                            }
                        }
                        for (auto It =
                                 TransitionTimeRemainingRules->CreateIterator();
                             It;
                             ++It)
                        {
                            FString From;
                            FString To;
                            It->Split(FString::Chr(0x1f), &From, &To);
                            if (From == Operation.StateName
                                || To == Operation.StateName)
                            {
                                It.RemoveCurrent();
                            }
                        }
                        for (auto It =
                                 TransitionExpressionRules->CreateIterator();
                             It;
                             ++It)
                        {
                            FString From;
                            FString To;
                            It->Split(FString::Chr(0x1f), &From, &To);
                            if (From == Operation.StateName
                                || To == Operation.StateName)
                            {
                                It.RemoveCurrent();
                            }
                        }
                    }
                    else if (Action == TEXT("set_entry_state"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_sequence_player")
                        || Action == TEXT("remove_state_sequence_player"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (StateBoolBlends->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_bool_blend_requires_remove"),
                                Operation.StateName);
                        }
                        if (StateBlendSpaceDimensions->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_blend_space_requires_remove"),
                                Operation.StateName);
                        }
                        if (StateIntBlendCounts->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_int_blend_requires_remove"),
                                Operation.StateName);
                        }
                        if (StateApplyAdditives->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_apply_additive_requires_remove"),
                                Operation.StateName);
                        }
                        if (StateLayeredBlendCounts->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_layered_blend_requires_remove"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_sequence_player"))
                        {
                            UAnimSequenceBase* PlayerSequence =
                                Cast<UAnimSequenceBase>(
                                    FindAsset(Operation.ReferencedAssetPath));
                            if (!PlayerSequence || PlayerSequence->IsA<UAnimMontage>()
                                || !AnimBlueprint->TargetSkeleton
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    PlayerSequence->GetSkeleton())
                                || !FMath::IsFinite(Operation.PlayRate))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_sequence_player"),
                                    Operation.ReferencedAssetPath);
                            }
                            StatePlayers->Add(Operation.StateName);
                        }
                        else if (!StatePlayers->Remove(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_sequence_player_not_found"),
                                Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_blend_by_bool")
                        || Action == TEXT("remove_state_blend_by_bool"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_blend_by_bool"))
                        {
                            if (StateBlendSpaceDimensions->Contains(
                                    Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_blend_space_requires_remove"),
                                    Operation.StateName);
                            }
                            if (StateIntBlendCounts->Contains(
                                    Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_int_blend_requires_remove"),
                                    Operation.StateName);
                            }
                            if (StateApplyAdditives->Contains(Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_apply_additive_requires_remove"),
                                    Operation.StateName);
                            }
                            if (StateLayeredBlendCounts->Contains(
                                    Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_layered_blend_requires_remove"),
                                    Operation.StateName);
                            }
                            UAnimSequenceBase* FalseSequence =
                                Cast<UAnimSequenceBase>(
                                    FindAsset(Operation.ReferencedAssetPath));
                            UAnimSequenceBase* TrueSequence =
                                Cast<UAnimSequenceBase>(
                                    FindAsset(Operation.SecondaryAssetPath));
                            const UClass* SkeletonClass = AnimBlueprint
                                ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                            const FString VariableName = Operation.Name.TrimStartAndEnd();
                            if (!FalseSequence || FalseSequence->IsA<UAnimMontage>()
                                || !TrueSequence || TrueSequence->IsA<UAnimMontage>()
                                || !AnimBlueprint->TargetSkeleton
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    FalseSequence->GetSkeleton())
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    TrueSequence->GetSkeleton())
                                || !FindFProperty<FBoolProperty>(
                                    SkeletonClass, FName(*VariableName))
                                || !FMath::IsFinite(Operation.PlayRate)
                                || !FMath::IsFinite(Operation.SecondaryPlayRate)
                                || !FMath::IsFinite(Operation.Duration)
                                || Operation.Duration < 0.0f)
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_blend_by_bool"),
                                    Operation.StateName);
                            }
                            Operation.Name = VariableName;
                            StatePlayers->Add(Operation.StateName);
                            StateBoolBlends->Add(Operation.StateName);
                        }
                        else
                        {
                            if (!StateBoolBlends->Remove(Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_bool_blend_not_found"),
                                    Operation.StateName);
                            }
                            StatePlayers->Remove(Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_blend_space_player")
                        || Action == TEXT("remove_state_blend_space_player"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_blend_space_player"))
                        {
                            UBlendSpace* BlendSpace = Cast<UBlendSpace>(
                                FindAsset(Operation.ReferencedAssetPath));
                            const bool bOneDimensional =
                                BlendSpace && BlendSpace->IsA<UBlendSpace1D>();
                            const int32 Dimension = bOneDimensional ? 1 : 2;
                            const UClass* SkeletonClass = AnimBlueprint
                                ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                            const FFloatProperty* XProperty = SkeletonClass
                                ? FindFProperty<FFloatProperty>(
                                    SkeletonClass,
                                    FName(*Operation.InputXVariableName))
                                : nullptr;
                            const FFloatProperty* YProperty =
                                !bOneDimensional && SkeletonClass
                                ? FindFProperty<FFloatProperty>(
                                    SkeletonClass,
                                    FName(*Operation.InputYVariableName))
                                : nullptr;
                            if (!BlendSpace || !AnimBlueprint->TargetSkeleton
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    BlendSpace->GetSkeleton())
                                || !XProperty || (!bOneDimensional && !YProperty)
                                || !FMath::IsFinite(Operation.PlayRate))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_blend_space_player"),
                                    Operation.StateName);
                            }
                            if (StatePlayers->Contains(Operation.StateName)
                                || StateBoolBlends->Contains(Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_pose_requires_remove"),
                                    Operation.StateName);
                            }
                            if (const int32* ExistingDimension =
                                    StateBlendSpaceDimensions->Find(
                                        Operation.StateName))
                            {
                                if (*ExistingDimension != Dimension)
                                {
                                    return MakeError<FThomasAnimationPlanResult>(
                                        TEXT("state_blend_space_dimension_change_requires_remove"),
                                        Operation.StateName);
                                }
                            }
                            StateBlendSpaceDimensions->Add(
                                Operation.StateName, Dimension);
                        }
                        else if (!StateBlendSpaceDimensions->Remove(
                            Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_blend_space_player_not_found"),
                                Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_blend_list_by_int")
                        || Action == TEXT("remove_state_blend_list_by_int"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_blend_list_by_int"))
                        {
                            const int32 PoseCount =
                                Operation.ReferencedAssetPaths.Num();
                            const UClass* SkeletonClass = AnimBlueprint
                                ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                            const FIntProperty* IndexProperty = SkeletonClass
                                ? FindFProperty<FIntProperty>(
                                    SkeletonClass,
                                    FName(*Operation.InputIndexVariableName))
                                : nullptr;
                            if (PoseCount < 2 || PoseCount > 8
                                || Operation.PlayRates.Num() != PoseCount
                                || Operation.BlendTimes.Num() != PoseCount
                                || !IndexProperty || !AnimBlueprint->TargetSkeleton)
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_blend_list_by_int"),
                                    Operation.StateName);
                            }
                            for (int32 PoseIndex = 0;
                                 PoseIndex < PoseCount;
                                 ++PoseIndex)
                            {
                                UAnimSequenceBase* CandidateSequence =
                                    Cast<UAnimSequenceBase>(FindAsset(
                                        Operation.ReferencedAssetPaths[PoseIndex]));
                                if (!CandidateSequence
                                    || CandidateSequence->IsA<UAnimMontage>()
                                    || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                        CandidateSequence->GetSkeleton())
                                    || !FMath::IsFinite(
                                        Operation.PlayRates[PoseIndex])
                                    || !FMath::IsFinite(
                                        Operation.BlendTimes[PoseIndex])
                                    || Operation.BlendTimes[PoseIndex] < 0.0f)
                                {
                                    return MakeError<FThomasAnimationPlanResult>(
                                        TEXT("invalid_state_blend_list_pose"),
                                        FString::FromInt(PoseIndex));
                                }
                            }
                            if (StateBoolBlends->Contains(Operation.StateName)
                                || StateBlendSpaceDimensions->Contains(
                                    Operation.StateName)
                                || (StatePlayers->Contains(Operation.StateName)
                                    && !StateIntBlendCounts->Contains(
                                        Operation.StateName)))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_pose_requires_remove"),
                                    Operation.StateName);
                            }
                            if (const int32* ExistingPoseCount =
                                    StateIntBlendCounts->Find(Operation.StateName))
                            {
                                if (*ExistingPoseCount != PoseCount)
                                {
                                    return MakeError<FThomasAnimationPlanResult>(
                                        TEXT("state_int_blend_pose_count_change_requires_remove"),
                                        Operation.StateName);
                                }
                            }
                            StatePlayers->Add(Operation.StateName);
                            StateIntBlendCounts->Add(
                                Operation.StateName, PoseCount);
                        }
                        else
                        {
                            if (!StateIntBlendCounts->Remove(Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_int_blend_not_found"),
                                    Operation.StateName);
                            }
                            StatePlayers->Remove(Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_apply_additive")
                        || Action == TEXT("remove_state_apply_additive"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_apply_additive"))
                        {
                            UAnimSequenceBase* BaseSequence =
                                Cast<UAnimSequenceBase>(
                                    FindAsset(Operation.ReferencedAssetPath));
                            UAnimSequence* AdditiveSequence = Cast<UAnimSequence>(
                                FindAsset(Operation.SecondaryAssetPath));
                            const UClass* SkeletonClass = AnimBlueprint
                                ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                            const FFloatProperty* AlphaProperty = SkeletonClass
                                ? FindFProperty<FFloatProperty>(
                                    SkeletonClass,
                                    FName(*Operation.InputAlphaVariableName))
                                : nullptr;
                            if (!BaseSequence || BaseSequence->IsA<UAnimMontage>()
                                || !AdditiveSequence
                                || AdditiveSequence->AdditiveAnimType == AAT_None
                                || !AnimBlueprint->TargetSkeleton
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    BaseSequence->GetSkeleton())
                                || !AnimBlueprint->TargetSkeleton->IsCompatibleForEditor(
                                    AdditiveSequence->GetSkeleton())
                                || !AlphaProperty
                                || !FMath::IsFinite(Operation.PlayRate)
                                || !FMath::IsFinite(Operation.SecondaryPlayRate))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_apply_additive"),
                                    Operation.StateName);
                            }
                            if (StateBoolBlends->Contains(Operation.StateName)
                                || StateBlendSpaceDimensions->Contains(
                                    Operation.StateName)
                                || StateIntBlendCounts->Contains(Operation.StateName)
                                || (StatePlayers->Contains(Operation.StateName)
                                    && !StateApplyAdditives->Contains(
                                        Operation.StateName)))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_pose_requires_remove"),
                                    Operation.StateName);
                            }
                            StatePlayers->Add(Operation.StateName);
                            StateApplyAdditives->Add(Operation.StateName);
                        }
                        else
                        {
                            if (!StateApplyAdditives->Remove(Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_apply_additive_not_found"),
                                    Operation.StateName);
                            }
                            StatePlayers->Remove(Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_state_layered_blend_per_bone")
                        || Action == TEXT("remove_state_layered_blend_per_bone"))
                    {
                        if (!States->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("state_not_found"), Operation.StateName);
                        }
                        if (UnsupportedStatePose->Contains(Operation.StateName))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("unsupported_state_pose_content"),
                                Operation.StateName);
                        }
                        if (Action == TEXT("set_state_layered_blend_per_bone"))
                        {
                            const int32 LayerCount =
                                Operation.ReferencedAssetPaths.Num();
                            UAnimSequenceBase* BaseSequence =
                                Cast<UAnimSequenceBase>(
                                    FindAsset(Operation.ReferencedAssetPath));
                            const UClass* SkeletonClass = AnimBlueprint
                                ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                            if (!BaseSequence || BaseSequence->IsA<UAnimMontage>()
                                || LayerCount < 1 || LayerCount > 8
                                || Operation.PlayRates.Num() != LayerCount
                                || Operation.LayerAlphaVariableNames.Num()
                                    != LayerCount
                                || Operation.LayerBoneNames.Num() != LayerCount
                                || Operation.LayerBlendDepths.Num() != LayerCount
                                || !AnimBlueprint->TargetSkeleton
                                || !AnimBlueprint->TargetSkeleton
                                    ->IsCompatibleForEditor(
                                        BaseSequence->GetSkeleton())
                                || !FMath::IsFinite(Operation.PlayRate))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("invalid_state_layered_blend"),
                                    Operation.StateName);
                            }
                            for (int32 LayerIndex = 0;
                                 LayerIndex < LayerCount;
                                 ++LayerIndex)
                            {
                                UAnimSequenceBase* LayerSequence =
                                    Cast<UAnimSequenceBase>(FindAsset(
                                        Operation.ReferencedAssetPaths[
                                            LayerIndex]));
                                const FString& VariableName =
                                    Operation.LayerAlphaVariableNames[
                                        LayerIndex];
                                const FString& BoneName =
                                    Operation.LayerBoneNames[LayerIndex];
                                const int32 BlendDepth =
                                    Operation.LayerBlendDepths[LayerIndex];
                                if (!LayerSequence
                                    || LayerSequence->IsA<UAnimMontage>()
                                    || !AnimBlueprint->TargetSkeleton
                                        ->IsCompatibleForEditor(
                                            LayerSequence->GetSkeleton())
                                    || !FMath::IsFinite(
                                        Operation.PlayRates[LayerIndex])
                                    || VariableName.IsEmpty()
                                    || !FindFProperty<FFloatProperty>(
                                        SkeletonClass,
                                        FName(*VariableName))
                                    || BoneName.IsEmpty()
                                    || AnimBlueprint->TargetSkeleton
                                        ->GetReferenceSkeleton().FindBoneIndex(
                                            FName(*BoneName)) == INDEX_NONE
                                    || BlendDepth < -1 || BlendDepth > 1024)
                                {
                                    return MakeError<FThomasAnimationPlanResult>(
                                        TEXT("invalid_state_layered_blend_layer"),
                                        FString::FromInt(LayerIndex));
                                }
                            }
                            if (StateBoolBlends->Contains(Operation.StateName)
                                || StateBlendSpaceDimensions->Contains(
                                    Operation.StateName)
                                || StateIntBlendCounts->Contains(
                                    Operation.StateName)
                                || StateApplyAdditives->Contains(
                                    Operation.StateName)
                                || (StatePlayers->Contains(Operation.StateName)
                                    && !StateLayeredBlendCounts->Contains(
                                        Operation.StateName)))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_pose_requires_remove"),
                                    Operation.StateName);
                            }
                            if (const int32* ExistingLayerCount =
                                    StateLayeredBlendCounts->Find(
                                        Operation.StateName))
                            {
                                if (*ExistingLayerCount != LayerCount)
                                {
                                    return MakeError<FThomasAnimationPlanResult>(
                                        TEXT("state_layered_blend_layer_count_change_requires_remove"),
                                        Operation.StateName);
                                }
                            }
                            StatePlayers->Add(Operation.StateName);
                            StateLayeredBlendCounts->Add(
                                Operation.StateName, LayerCount);
                        }
                        else
                        {
                            if (!StateLayeredBlendCounts->Remove(
                                    Operation.StateName))
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("state_layered_blend_not_found"),
                                    Operation.StateName);
                            }
                            StatePlayers->Remove(Operation.StateName);
                        }
                    }
                    else if (Action == TEXT("set_transition_rule_constant"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (LinkedTransitionRules->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("linked_transition_rule_unsupported"), Key);
                        }
                    }
                    else if (Action == TEXT("set_transition_rule_variable"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        const FString VariableName = Operation.Name.TrimStartAndEnd();
                        const UClass* SkeletonClass = AnimBlueprint
                            ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (VariableName.IsEmpty()
                            || !FindFProperty<FBoolProperty>(
                                SkeletonClass, FName(*VariableName)))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_rule_bool_variable_not_found"),
                                VariableName);
                        }
                        if (LinkedTransitionRules->Contains(Key)
                            && !TransitionRuleVariables->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("linked_transition_rule_unsupported"), Key);
                        }
                        Operation.Name = VariableName;
                        LinkedTransitionRules->Add(Key);
                        TransitionRuleVariables->Add(Key, VariableName);
                        TransitionExpressionRules->Add(Key);
                    }
                    else if (Action == TEXT("remove_transition_rule_variable"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (!TransitionRuleVariables->Remove(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_rule_variable_not_found"), Key);
                        }
                        LinkedTransitionRules->Remove(Key);
                        TransitionExpressionRules->Remove(Key);
                    }
                    else if (Action == TEXT("set_transition_rule_time_remaining"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        const FString GuardVariable =
                            Operation.Name.TrimStartAndEnd();
                        const UClass* SkeletonClass = AnimBlueprint
                            ? AnimBlueprint->SkeletonGeneratedClass.Get() : nullptr;
                        const bool bPreviousStateHasDirectPlayer =
                            StatePlayers->Contains(Operation.FromStateName)
                            && !StateBoolBlends->Contains(
                                Operation.FromStateName)
                            && !StateBlendSpaceDimensions->Contains(
                                Operation.FromStateName)
                            && !StateIntBlendCounts->Contains(
                                Operation.FromStateName)
                            && !StateApplyAdditives->Contains(
                                Operation.FromStateName)
                            && !StateLayeredBlendCounts->Contains(
                                Operation.FromStateName);
                        const bool bFractionGetter =
                            Operation.TransitionGetter
                                == TEXT("time_remaining_fraction");
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (GuardVariable.IsEmpty()
                            || !FindFProperty<FBoolProperty>(
                                SkeletonClass, FName(*GuardVariable))
                            || (Operation.TransitionGetter
                                    != TEXT("time_remaining")
                                && Operation.TransitionGetter
                                    != TEXT("time_remaining_fraction"))
                            || TransitionComparisonFunction(
                                Operation.ComparisonOperator).IsNone()
                            || TransitionLogicalFunction(
                                Operation.LogicalOperator).IsNone()
                            || !FMath::IsFinite(Operation.Value)
                            || Operation.Value < 0.0f
                            || (bFractionGetter && Operation.Value > 1.0f)
                            || !bPreviousStateHasDirectPlayer)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_transition_time_remaining_rule"),
                                Key);
                        }
                        if (LinkedTransitionRules->Contains(Key)
                            && !TransitionTimeRemainingRules->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("linked_transition_rule_unsupported"), Key);
                        }
                        Operation.Name = GuardVariable;
                        LinkedTransitionRules->Add(Key);
                        TransitionTimeRemainingRules->Add(Key);
                        TransitionExpressionRules->Add(Key);
                    }
                    else if (Action
                        == TEXT("remove_transition_rule_time_remaining"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (!TransitionTimeRemainingRules->Remove(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_time_remaining_rule_not_found"),
                                Key);
                        }
                        LinkedTransitionRules->Remove(Key);
                        TransitionExpressionRules->Remove(Key);
                    }
                    else if (Action
                        == TEXT("set_transition_rule_expression"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        const bool bPreviousStateHasDirectPlayer =
                            StatePlayers->Contains(Operation.FromStateName)
                            && !StateBoolBlends->Contains(
                                Operation.FromStateName)
                            && !StateBlendSpaceDimensions->Contains(
                                Operation.FromStateName)
                            && !StateIntBlendCounts->Contains(
                                Operation.FromStateName)
                            && !StateApplyAdditives->Contains(
                                Operation.FromStateName)
                            && !StateLayeredBlendCounts->Contains(
                                Operation.FromStateName);
                        FString ValidationError;
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (!ValidateTransitionExpression(
                                Operation.TransitionExpressionTokens,
                                AnimBlueprint,
                                bPreviousStateHasDirectPlayer,
                                States,
                                ValidationError))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_transition_expression"),
                                ValidationError);
                        }
                        if (LinkedTransitionRules->Contains(Key)
                            && !TransitionExpressionRules->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("linked_transition_rule_unsupported"), Key);
                        }
                        LinkedTransitionRules->Add(Key);
                        TransitionExpressionRules->Add(Key);
                        TransitionRuleVariables->Remove(Key);
                        TransitionTimeRemainingRules->Remove(Key);
                    }
                    else if (Action
                        == TEXT("remove_transition_rule_expression"))
                    {
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        if (!Transitions->Contains(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        if (!TransitionExpressionRules->Remove(Key))
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_expression_rule_not_found"),
                                Key);
                        }
                        LinkedTransitionRules->Remove(Key);
                        TransitionRuleVariables->Remove(Key);
                        TransitionTimeRemainingRules->Remove(Key);
                    }
                    else
                    {
                        if (!States->Contains(Operation.FromStateName)
                            || !States->Contains(Operation.ToStateName)
                            || Operation.FromStateName == Operation.ToStateName)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_transition_states"),
                                Operation.FromStateName + TEXT("->") + Operation.ToStateName);
                        }
                        if (Operation.Duration < 0.0f || Operation.PriorityOrder < 0)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("invalid_transition_settings"), Action);
                        }
                        const FString Key = TransitionKey(
                            Operation.FromStateName, Operation.ToStateName);
                        const bool bExists = Transitions->Contains(Key);
                        if (Action == TEXT("add_transition"))
                        {
                            if (bExists)
                            {
                                return MakeError<FThomasAnimationPlanResult>(
                                    TEXT("transition_already_exists"), Key);
                            }
                            Transitions->Add(Key);
                        }
                        else if (!bExists)
                        {
                            return MakeError<FThomasAnimationPlanResult>(
                                TEXT("transition_not_found"), Key);
                        }
                        else if (Action == TEXT("remove_transition"))
                        {
                            Transitions->Remove(Key);
                            LinkedTransitionRules->Remove(Key);
                            TransitionRuleVariables->Remove(Key);
                            TransitionTimeRemainingRules->Remove(Key);
                            TransitionExpressionRules->Remove(Key);
                        }
                    }
                }
                Result.Preview.Add((Action + TEXT(":") + Operation.MachineName
                    + TEXT(":") + Operation.StateName + Operation.FromStateName
                    + TEXT("->") + Operation.ToStateName).Left(500));
                continue;
            }
            if (Action == TEXT("set_root_motion")
                && (!Sequence || !ParseRootMotionLock(Operation.RootMotionLock).IsSet()))
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("invalid_root_motion_spec"), Operation.RootMotionLock);
            }
            if (Action == TEXT("set_additive_settings"))
            {
                const TOptional<EAdditiveAnimationType> AdditiveType =
                    ParseAdditiveType(Operation.AdditiveType);
                const TOptional<EAdditiveBasePoseType> BasePoseType =
                    ParseAdditiveBasePoseType(Operation.AdditiveBasePoseType);
                UAnimSequence* BasePoseSequence = Operation.AdditiveBasePoseAssetPath.IsEmpty()
                    ? nullptr
                    : Cast<UAnimSequence>(
                        FindAsset(Operation.AdditiveBasePoseAssetPath));
                const bool bRequiresBaseSequence = BasePoseType.IsSet()
                    && BasePoseType.GetValue() != ABPT_None
                    && BasePoseType.GetValue() != ABPT_RefPose;
                if (!Sequence || !AdditiveType.IsSet() || !BasePoseType.IsSet()
                    || Operation.AdditiveBaseFrame < 0
                    || (bRequiresBaseSequence
                        && (!BasePoseSequence || !Sequence->GetSkeleton()
                            || !Sequence->GetSkeleton()->IsCompatibleForEditor(
                                BasePoseSequence->GetSkeleton()))))
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("invalid_additive_settings"), Operation.AdditiveType);
                }
            }
            if (Action.Contains(TEXT("notify")) && !SequenceBase)
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("notify_requires_sequence_base"), Action);
            }
            if ((Action == TEXT("add_notify_track") || Action == TEXT("remove_notify_track")
                    || Action == TEXT("remove_notify_by_track")) && Operation.TrackName.IsEmpty())
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("notify_track_name_required"), Action);
            }
            if (Action == TEXT("remove_notify_by_name") && Operation.Name.IsEmpty())
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("notify_name_required"), Action);
            }
            if ((Action == TEXT("add_notify") || Action == TEXT("add_notify_state")))
            {
                UClass* NotifyClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                const UClass* RequiredClass = Action == TEXT("add_notify")
                    ? UAnimNotify::StaticClass() : UAnimNotifyState::StaticClass();
                if (!NotifyClass || !NotifyClass->IsChildOf(RequiredClass)
                    || NotifyClass->HasAnyClassFlags(CLASS_Abstract)
                    || Operation.TrackName.IsEmpty() || Operation.Time < 0.0f
                    || (Action == TEXT("add_notify_state") && Operation.Duration <= 0.0f))
                {
                    return MakeError<FThomasAnimationPlanResult>(TEXT("invalid_notify_spec"), Operation.ClassPath);
                }
            }
            if (Action.Contains(TEXT("curve")) && (!Sequence || Operation.Name.IsEmpty()))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("curve_requires_sequence"), Operation.Name);
            }
            if (Action.Contains(TEXT("curve")) && Sequence)
            {
                const FAnimationCurveIdentifier CurveId(
                    FName(Operation.Name), ERawCurveTrackTypes::RCT_Float);
                const bool bCurveExists = PlannedCurves.Contains(CurveId.CurveName);
                if ((Action == TEXT("add_curve") && bCurveExists)
                    || (Action != TEXT("add_curve") && !bCurveExists))
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        bCurveExists ? TEXT("curve_already_exists") : TEXT("curve_not_found"), Operation.Name);
                }
                if ((Action == TEXT("set_curve_key") || Action == TEXT("remove_curve_key"))
                    && Operation.Time < 0.0f)
                {
                    return MakeError<FThomasAnimationPlanResult>(TEXT("invalid_curve_time"), Operation.Name);
                }
                if (Action == TEXT("add_curve"))
                {
                    PlannedCurves.Add(CurveId.CurveName);
                }
                else if (Action == TEXT("remove_curve"))
                {
                    PlannedCurves.Remove(CurveId.CurveName);
                }
            }
            if ((Action.Contains(TEXT("montage_section")) || Action.Contains(TEXT("montage_slot"))
                    || Action.Contains(TEXT("montage_segment"))) && !Montage)
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("operation_requires_montage"), Action);
            }
            if (Action == TEXT("add_montage_section")
                && (Operation.SectionName.IsEmpty() || PlannedSections.Contains(FName(Operation.SectionName))
                    || Operation.Time < 0.0f))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("invalid_montage_section"), Operation.SectionName);
            }
            if ((Action == TEXT("remove_montage_section") || Action == TEXT("rename_montage_section")
                    || Action == TEXT("set_next_montage_section"))
                && !PlannedSections.Contains(FName(Operation.SectionName)))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("montage_section_not_found"), Operation.SectionName);
            }
            if (Action == TEXT("rename_montage_section")
                && (Operation.NewName.IsEmpty()
                    || PlannedSections.Contains(FName(Operation.NewName))))
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("invalid_new_montage_section"), Operation.NewName);
            }
            if (Action == TEXT("set_next_montage_section")
                && !Operation.NextSectionName.IsEmpty()
                && !PlannedSections.Contains(FName(Operation.NextSectionName)))
            {
                return MakeError<FThomasAnimationPlanResult>(
                    TEXT("next_montage_section_not_found"), Operation.NextSectionName);
            }
            if (Action == TEXT("add_montage_section"))
            {
                PlannedSections.Add(FName(Operation.SectionName));
            }
            else if (Action == TEXT("remove_montage_section"))
            {
                PlannedSections.Remove(FName(Operation.SectionName));
            }
            else if (Action == TEXT("rename_montage_section"))
            {
                PlannedSections.Remove(FName(Operation.SectionName));
                PlannedSections.Add(FName(Operation.NewName));
            }
            if (Action == TEXT("add_montage_slot")
                && (Operation.SlotName.IsEmpty() || PlannedSlots.Contains(FName(Operation.SlotName))))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("invalid_montage_slot"), Operation.SlotName);
            }
            if ((Action == TEXT("remove_montage_slot") || Action == TEXT("add_montage_segment")
                    || Action == TEXT("remove_montage_segment"))
                && !PlannedSlots.Contains(FName(Operation.SlotName)))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("montage_slot_not_found"), Operation.SlotName);
            }
            if (Action == TEXT("add_montage_slot"))
            {
                PlannedSlots.Add(FName(Operation.SlotName));
            }
            else if (Action == TEXT("remove_montage_slot"))
            {
                PlannedSlots.Remove(FName(Operation.SlotName));
            }
            if (Action == TEXT("add_montage_segment"))
            {
                UAnimSequenceBase* Referenced = Cast<UAnimSequenceBase>(FindAsset(Operation.ReferencedAssetPath));
                if (!Referenced || Referenced->GetSkeleton() != Montage->GetSkeleton()
                    || Operation.PlayRate == 0.0f || Operation.LoopCount < 1
                    || Operation.Time < 0.0f || Operation.Duration < 0.0f
                    || Operation.EndTime <= Operation.Duration
                    || Operation.EndTime > Referenced->GetPlayLength())
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("invalid_montage_segment"), Operation.ReferencedAssetPath);
                }
            }
            if (Action == TEXT("remove_montage_segment"))
            {
                const int32 SlotIndex = FindMontageSlot(*Montage, Operation.SlotName);
                if (!Montage->SlotAnimTracks.IsValidIndex(SlotIndex)
                    || !Montage->SlotAnimTracks[SlotIndex].AnimTrack.AnimSegments.IsValidIndex(Operation.Index))
                {
                    return MakeError<FThomasAnimationPlanResult>(
                        TEXT("montage_segment_not_found"), FString::FromInt(Operation.Index));
                }
            }
            if ((Action == TEXT("add_socket") || Action == TEXT("remove_socket")) && !Skeleton)
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("operation_requires_skeleton"), Action);
            }
            if (Action == TEXT("add_socket")
                && (Operation.Name.IsEmpty()
                    || Skeleton->FindSocket(FName(Operation.Name))
                    || Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(Operation.BoneName)) == INDEX_NONE))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("invalid_socket_spec"), Operation.Name);
            }
            if (Action == TEXT("remove_socket") && !Skeleton->FindSocket(FName(Operation.Name)))
            {
                return MakeError<FThomasAnimationPlanResult>(TEXT("socket_not_found"), Operation.Name);
            }
            Result.Preview.Add((Action + TEXT(":") + Operation.Name + Operation.SectionName + Operation.SlotName).Left(500));
        }
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.OperationCount = Request.Operations.Num();
        FCachedAnimationPatchPlan& Plan = PatchPlans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasAnimationApplyResult ApplyAnimationPlan(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedAnimationPatchPlan Plan;
        if (!PatchPlans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasAnimationApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        UObject* Asset = FindAsset(Plan.Request.AssetPath);
        const FString RevisionBefore = Revision(Asset);
        if (!Asset || RevisionBefore != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasAnimationApplyResult>(TEXT("revision_conflict"), RevisionBefore);
        }
        UPackage* Package = Asset->GetOutermost();
        FThomasAnimationApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = RevisionBefore;
        FScopedTransaction Transaction(
            NSLOCTEXT("ThomasEditor", "AnimationPatch", "ThomasEditor Animation Patch"));
        Asset->Modify();
        Package->Modify();
        auto Rollback = [&]()
        {
            const FString PackageName = Package->GetName();
            FText ReloadError;
            Result.bRolledBack = UPackageTools::ReloadPackages(
                {Package},
                ReloadError,
                EReloadPackagesInteractionMode::AssumePositive);
            if (Result.bRolledBack)
            {
                if (UPackage* ReloadedPackage =
                    FindPackage(nullptr, *PackageName))
                {
                    ReloadedPackage->SetDirtyFlag(false);
                }
            }
        };
        auto Fail = [&](const FString& Code, const FString& Message) -> FThomasAnimationApplyResult
        {
            Transaction.Cancel();
            Rollback();
            Result.Code = Code;
            Result.Message = Message.Left(1200);
            return Result;
        };

        UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
        UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
        UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(Asset);
        USkeleton* Skeleton = Cast<USkeleton>(Asset);
        UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset);
        UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Asset);
        UIKRigController* IKController = IKRig
            ? UIKRigController::GetController(IKRig) : nullptr;
        UIKRetargeter* IKRetargeter = Cast<UIKRetargeter>(Asset);
        UIKRetargeterController* RetargetController = IKRetargeter
            ? UIKRetargeterController::GetController(IKRetargeter) : nullptr;
        UControlRigRuntimeAsset* ControlRigAsset =
            Cast<UControlRigRuntimeAsset>(Asset);
        UControlRigEditorAsset* ControlRigEditorAsset =
            GetControlRigEditorAsset(ControlRigAsset);
        URigHierarchy* ControlRigHierarchy = ControlRigAsset
            ? ControlRigAsset->GetHierarchy() : nullptr;
        URigHierarchyController* ControlRigController = ControlRigHierarchy
            ? ControlRigHierarchy->GetController(true) : nullptr;
        if (ControlRigHierarchy)
        {
            ControlRigHierarchy->Modify();
        }
        if (ControlRigEditorAsset)
        {
            static_cast<UObject*>(ControlRigEditorAsset)->Modify();
        }
        bool bBlueprintStructureChanged = false;
        bool bControlRigGraphChanged = false;
        for (const FThomasAnimationOperation& Operation : Plan.Request.Operations)
        {
            const FString& Action = Operation.Action;
            bool bOperationOk = true;
            const bool bControlRigModelAction =
                IsControlRigModelAction(Action);
            const bool bControlRigGraphAction =
                IsControlRigGraphAction(Action);
            if (bControlRigModelAction)
            {
                bOperationOk = ControlRigAsset && ControlRigEditorAsset;
                if (bOperationOk
                    && Action == TEXT("add_control_rig_graph"))
                {
                    URigVMGraph* AddedModel =
                        ControlRigEditorAsset->AddModel(
                            Operation.Name,
                            true,
                            false);
                    URigVMGraph* ResolvedModel =
                        ResolveTopLevelControlRigModel(
                            ControlRigEditorAsset,
                            AddedModel
                                ? AddedModel->GetGraphName()
                                : FString());
                    const FString GeneratedGraphName =
                        RequestedControlRigTopLevelGraphName(
                            ControlRigEditorAsset,
                            Operation.Name);
                    bOperationOk = AddedModel
                        && ResolvedModel == AddedModel
                        && AddedModel->GetGraphName()
                            == GeneratedGraphName
                        && AddedModel->GetNodes().IsEmpty()
                        && AddedModel->GetLinks().IsEmpty()
                        && AddedModel->GetLocalVariables(false).IsEmpty();
                }
                else if (bOperationOk
                    && Action == TEXT("rename_control_rig_graph"))
                {
                    URigVMGraph* ExistingModel =
                        ResolveTopLevelControlRigModel(
                            ControlRigEditorAsset,
                            Operation.RigVMGraphName);
                    const FString OldGraphName = ExistingModel
                        ? ExistingModel->GetGraphName() : FString();
                    const FString OldNodePath = ExistingModel
                        ? ExistingModel->GetNodePath() : FString();
                    const FString RequestedGraphName =
                        RequestedControlRigTopLevelGraphName(
                            ControlRigEditorAsset,
                            Operation.NewName);
                    bOperationOk = ExistingModel
                        && RigVMTopLevelGraphStateString(ExistingModel)
                            == Operation.ExpectedValue;
                    if (bOperationOk)
                    {
                        ControlRigEditorAsset->RenameGraph(
                            OldNodePath,
                            FName(*Operation.NewName));
                        URigVMGraph* RenamedModel =
                            ResolveTopLevelControlRigModel(
                                ControlRigEditorAsset,
                                Operation.NewName);
                        if (!RenamedModel
                            && RequestedGraphName != Operation.NewName)
                        {
                            RenamedModel = ResolveTopLevelControlRigModel(
                                ControlRigEditorAsset,
                                RequestedGraphName);
                        }
                        bOperationOk = RenamedModel == ExistingModel
                            && !ResolveTopLevelControlRigModel(
                                ControlRigEditorAsset,
                                OldGraphName);
                    }
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_graph"))
                {
                    URigVMGraph* ExistingModel =
                        ResolveTopLevelControlRigModel(
                            ControlRigEditorAsset,
                            Operation.RigVMGraphName);
                    const FString ModelName = ExistingModel
                        ? ExistingModel->GetGraphName() : FString();
                    bOperationOk = ExistingModel
                        && RigVMTopLevelGraphStateString(ExistingModel)
                            == Operation.ExpectedValue
                        && ControlRigEditorAsset->RemoveModel(
                            ModelName,
                            true,
                            false)
                        && !ResolveTopLevelControlRigModel(
                            ControlRigEditorAsset,
                            Operation.RigVMGraphName);
                }
                else if (bOperationOk)
                {
                    bOperationOk = false;
                }
                bControlRigGraphChanged = bOperationOk;
            }
            else if (bControlRigGraphAction)
            {
                URigVMGraph* Model = ResolveControlRigModel(
                    ControlRigEditorAsset,
                    Operation.RigVMGraphName);
                URigVMController* Controller =
                    ControlRigEditorAsset && Model
                        ? ControlRigEditorAsset->GetOrCreateController(Model)
                        : nullptr;
                bOperationOk = ControlRigAsset
                    && ControlRigEditorAsset && Model && Controller;
                if (bOperationOk)
                {
                    Model->Modify();
                    Controller->Modify();
                }
                if (bOperationOk && IsAdvancedControlRigGraphAction(Action))
                {
                    bOperationOk = ApplyAdvancedControlRigGraphOperation(
                        Operation,
                        Model,
                        Controller,
                        ControlRigAsset,
                        ControlRigEditorAsset);
                }
                else if (bOperationOk && IsR22ControlRigGraphAction(Action))
                {
                    bOperationOk = ApplyR22ControlRigGraphOperation(
                        Operation,
                        Model,
                        Controller,
                        ControlRigEditorAsset);
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_unit_node"))
                {
                    UScriptStruct* UnitStruct = LoadObject<UScriptStruct>(
                        nullptr, *Operation.ClassPath);
                    URigVMUnitNode* AddedNode = UnitStruct
                        ? Controller->AddUnitNode(
                            UnitStruct,
                            FName(*Operation.RigVMMethodName),
                            FVector2D(
                                Operation.PositionX,
                                Operation.PositionY),
                            Operation.Name,
                            true,
                            false)
                        : nullptr;
                    bOperationOk = AddedNode
                        && AddedNode->GetName() == Operation.Name
                        && AddedNode->GetScriptStruct() == UnitStruct
                        && AddedNode->GetMethodName()
                            == FName(*Operation.RigVMMethodName)
                        && RigVMNodePositionString(AddedNode)
                            == FString::Printf(
                                TEXT("X=%.6f,Y=%.6f"),
                                static_cast<double>(Operation.PositionX),
                                static_cast<double>(Operation.PositionY));
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_template_node"))
                {
                    URigVMTemplateNode* AddedNode =
                        Controller->AddTemplateNode(
                            FName(*Operation.RigVMTemplateNotation),
                            FVector2D(
                                Operation.PositionX,
                                Operation.PositionY),
                            Operation.Name,
                            true,
                            false);
                    bOperationOk = AddedNode
                        && AddedNode->GetName() == Operation.Name
                        && AddedNode->GetNotation()
                            == FName(*Operation.RigVMTemplateNotation)
                        && RigVMNodePositionString(AddedNode)
                            == FString::Printf(
                                TEXT("X=%.6f,Y=%.6f"),
                                static_cast<double>(Operation.PositionX),
                                static_cast<double>(Operation.PositionY));
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_comment_node"))
                {
                    URigVMCommentNode* AddedNode =
                        Controller->AddCommentNode(
                            Operation.RigVMCommentText,
                            FVector2D(
                                Operation.PositionX,
                                Operation.PositionY),
                            Operation.RigVMNodeSize,
                            Operation.Color,
                            Operation.Name,
                            true,
                            false);
                    bOperationOk = AddedNode
                        && AddedNode->GetName() == Operation.Name
                        && RigVMNodePositionString(AddedNode)
                            == FString::Printf(
                                TEXT("X=%.6f,Y=%.6f"),
                                static_cast<double>(Operation.PositionX),
                                static_cast<double>(Operation.PositionY))
                        && RigVMNodeSizeString(AddedNode)
                            == RigVMVector2DString(
                                Operation.RigVMNodeSize)
                        && RigVMNodeColorString(AddedNode)
                            == RigVMColorString(Operation.Color)
                        && AddedNode->GetCommentText()
                            == Operation.RigVMCommentText;
                    if (bOperationOk
                        && (AddedNode->GetCommentFontSize()
                                != Operation.RigVMCommentFontSize
                            || AddedNode->GetCommentBubbleVisible()
                                != Operation.bRigVMCommentBubbleVisible
                            || AddedNode->GetCommentColorBubble()
                                != Operation.bRigVMCommentColorBubble))
                    {
                        bOperationOk = Controller->SetCommentText(
                                AddedNode,
                                Operation.RigVMCommentText,
                                Operation.RigVMCommentFontSize,
                                Operation.bRigVMCommentBubbleVisible,
                                Operation.bRigVMCommentColorBubble,
                                true,
                                false)
                            && RigVMCommentPropertiesString(AddedNode)
                                == RigVMCommentPropertiesString(
                                    Operation.RigVMCommentText,
                                    Operation.RigVMCommentFontSize,
                                    Operation.bRigVMCommentBubbleVisible,
                                    Operation.bRigVMCommentColorBubble);
                    }
                    bOperationOk = bOperationOk
                        && RigVMCommentPropertiesString(AddedNode)
                            == RigVMCommentPropertiesString(
                                Operation.RigVMCommentText,
                                Operation.RigVMCommentFontSize,
                                Operation.bRigVMCommentBubbleVisible,
                                Operation.bRigVMCommentColorBubble);
                }
                else if (bOperationOk
                    && Action == TEXT("rename_control_rig_comment_node"))
                {
                    URigVMCommentNode* Node = Cast<URigVMCommentNode>(
                        Model->FindNodeByName(FName(*Operation.Name)));
                    bOperationOk = Node
                        && Controller->RenameNode(
                            Node, FName(*Operation.NewName), true, false)
                        && !Model->FindNodeByName(FName(*Operation.Name))
                        && Cast<URigVMCommentNode>(Model->FindNodeByName(
                            FName(*Operation.NewName)));
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_comment"))
                {
                    URigVMCommentNode* Node = Cast<URigVMCommentNode>(
                        Model->FindNodeByName(FName(*Operation.Name)));
                    bOperationOk = Node
                        && Controller->SetCommentText(
                            Node,
                            Operation.RigVMCommentText,
                            Operation.RigVMCommentFontSize,
                            Operation.bRigVMCommentBubbleVisible,
                            Operation.bRigVMCommentColorBubble,
                            true,
                            false)
                        && RigVMCommentPropertiesString(Node)
                            == RigVMCommentPropertiesString(
                                Operation.RigVMCommentText,
                                Operation.RigVMCommentFontSize,
                                Operation.bRigVMCommentBubbleVisible,
                                Operation.bRigVMCommentColorBubble);
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_comment_node"))
                {
                    bOperationOk = Controller->RemoveNodeByName(
                            FName(*Operation.Name), true, false)
                        && !Model->FindNodeByName(FName(*Operation.Name));
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_reroute_node"))
                {
                    bOperationOk = Controller->RemoveNodeByName(
                            FName(*Operation.Name), true, false)
                        && !Model->FindNodeByName(FName(*Operation.Name));
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_unit_node"))
                {
                    bOperationOk = Controller->RemoveNodeByName(
                            FName(*Operation.Name), true, false)
                        && !Model->FindNodeByName(FName(*Operation.Name));
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_template_node"))
                {
                    bOperationOk = Controller->RemoveNodeByName(
                            FName(*Operation.Name), true, false)
                        && !Model->FindNodeByName(FName(*Operation.Name));
                }
                else if (bOperationOk
                    && Action
                        == TEXT("unresolve_control_rig_template_node"))
                {
                    bOperationOk = Controller->UnresolveTemplateNodes(
                            {FName(*Operation.Name)}, true, false);
                    URigVMTemplateNode* Node =
                        Cast<URigVMTemplateNode>(
                            Model->FindNodeByName(
                                FName(*Operation.Name)));
                    bOperationOk = bOperationOk && Node
                        && Node->GetTemplate()
                        && Node->IsFullyUnresolved();
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_node_position"))
                {
                    bOperationOk = Controller->SetNodePositionByName(
                            FName(*Operation.Name),
                            FVector2D(
                                Operation.PositionX,
                                Operation.PositionY),
                            true,
                            false,
                            false)
                        && RigVMNodePositionString(
                            Model->FindNodeByName(FName(*Operation.Name)))
                            == FString::Printf(
                                TEXT("X=%.6f,Y=%.6f"),
                                static_cast<double>(Operation.PositionX),
                                static_cast<double>(Operation.PositionY));
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_node_size"))
                {
                    bOperationOk = Controller->SetNodeSizeByName(
                            FName(*Operation.Name),
                            Operation.RigVMNodeSize,
                            true,
                            false,
                            false)
                        && RigVMNodeSizeString(
                            Model->FindNodeByName(FName(*Operation.Name)))
                            == RigVMVector2DString(
                                Operation.RigVMNodeSize);
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_node_color"))
                {
                    bOperationOk = Controller->SetNodeColorByName(
                            FName(*Operation.Name),
                            Operation.Color,
                            true,
                            false)
                        && RigVMNodeColorString(
                            Model->FindNodeByName(FName(*Operation.Name)))
                            == RigVMColorString(Operation.Color);
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_pin_default"))
                {
                    bOperationOk = Controller->SetPinDefaultValue(
                            Operation.RigVMPinPath,
                            Operation.RigVMPinDefaultValue,
                            false,
                            true,
                            false,
                            false,
                            false)
                        && Model->FindPin(Operation.RigVMPinPath)
                        && Model->FindPin(Operation.RigVMPinPath)
                                ->GetDefaultValue()
                            == Operation.RigVMPinDefaultValue;
                }
                else if (bOperationOk
                    && Action
                        == TEXT("resolve_control_rig_wildcard_pin"))
                {
                    URigVMPin* Pin = Model->FindPin(
                        Operation.RigVMPinPath);
                    const bool bArray = Pin
                        && (Pin->IsDynamicArray()
                            || Pin->IsFixedSizeArray());
                    const TOptional<FBoundedRigVMTypeSpec> TypeSpec =
                        ResolveBoundedRigVMType(
                            Operation.RigVMType, bArray);
                    bOperationOk = Pin && TypeSpec.IsSet()
                        && Controller->ResolveWildCardPin(
                            Operation.RigVMPinPath,
                            TypeSpec->CPPType,
                            TypeSpec->CPPTypeObject
                                ? FName(*TypeSpec->CPPTypeObject
                                    ->GetPathName())
                                : NAME_None,
                            true,
                            false);
                    Pin = Model->FindPin(Operation.RigVMPinPath);
                    bOperationOk = bOperationOk && Pin
                        && !IsRigVMWildcardPin(Pin)
                        && Pin->GetCPPType() == TypeSpec->CPPType
                        && (!TypeSpec->CPPTypeObject
                            || Pin->GetCPPTypeObject()
                                == TypeSpec->CPPTypeObject);
                }
                else if (bOperationOk
                    && (Action
                            == TEXT("set_control_rig_array_pin_size")
                        || Action == TEXT("add_control_rig_array_pin")
                        || Action
                            == TEXT("duplicate_control_rig_array_pin")
                        || Action == TEXT("remove_control_rig_array_pin")))
                {
                    URigVMPin* RequestedPin = Model->FindPin(
                        Operation.RigVMPinPath);
                    const bool bElementAction =
                        Action == TEXT("duplicate_control_rig_array_pin")
                        || Action == TEXT("remove_control_rig_array_pin");
                    URigVMPin* ArrayPin = bElementAction
                        && RequestedPin
                            ? RequestedPin->GetParentPin()
                            : RequestedPin;
                    const FString ArrayPinPath = ArrayPin
                        ? ArrayPin->GetPinPath(false) : FString();
                    const int32 PreviousSize = ArrayPin
                        ? ArrayPin->GetSubPins().Num() : INDEX_NONE;
                    if (!ArrayPin)
                    {
                        bOperationOk = false;
                    }
                    else if (Action
                        == TEXT("set_control_rig_array_pin_size"))
                    {
                        bOperationOk = Controller->SetArrayPinSize(
                                ArrayPinPath,
                                Operation.RigVMArraySize,
                                Operation.RigVMPinDefaultValue,
                                true,
                                false)
                            && Model->FindPin(ArrayPinPath)
                            && Model->FindPin(ArrayPinPath)
                                    ->GetSubPins().Num()
                                == Operation.RigVMArraySize;
                    }
                    else if (Action
                        == TEXT("add_control_rig_array_pin"))
                    {
                        const FString AddedPinPath =
                            Controller->AddArrayPin(
                                ArrayPinPath,
                                Operation.RigVMPinDefaultValue,
                                true,
                                false);
                        bOperationOk = !AddedPinPath.IsEmpty()
                            && Model->FindPin(AddedPinPath)
                            && Model->FindPin(ArrayPinPath)
                            && Model->FindPin(ArrayPinPath)
                                    ->GetSubPins().Num()
                                == PreviousSize + 1;
                    }
                    else if (Action
                        == TEXT("duplicate_control_rig_array_pin"))
                    {
                        const FString DuplicatedPinPath =
                            Controller->DuplicateArrayPin(
                                Operation.RigVMPinPath,
                                true,
                                false);
                        bOperationOk = !DuplicatedPinPath.IsEmpty()
                            && Model->FindPin(DuplicatedPinPath)
                            && Model->FindPin(ArrayPinPath)
                            && Model->FindPin(ArrayPinPath)
                                    ->GetSubPins().Num()
                                == PreviousSize + 1;
                    }
                    else
                    {
                        bOperationOk = Controller->RemoveArrayPin(
                                Operation.RigVMPinPath,
                                true,
                                false)
                            && Model->FindPin(ArrayPinPath)
                            && Model->FindPin(ArrayPinPath)
                                    ->GetSubPins().Num()
                                == PreviousSize - 1;
                    }
                }
                else if (bOperationOk
                    && Action
                        == TEXT("add_control_rig_reroute_node_on_link"))
                {
                    URigVMLink* DirectLink = nullptr;
                    for (URigVMLink* Link : Model->GetLinks())
                    {
                        if (Link
                            && Link->GetSourcePinPath()
                                == Operation.RigVMSourcePinPath
                            && Link->GetTargetPinPath()
                                == Operation.RigVMTargetPinPath)
                        {
                            DirectLink = Link;
                            break;
                        }
                    }
                    URigVMRerouteNode* AddedNode = DirectLink
                        ? Controller->AddRerouteNodeOnLink(
                            DirectLink,
                            FVector2D(
                                Operation.PositionX,
                                Operation.PositionY),
                            Operation.Name,
                            true,
                            false)
                        : nullptr;
                    int32 RerouteLinkCount = 0;
                    if (AddedNode)
                    {
                        const FString PinPrefix = Operation.Name + TEXT(".");
                        for (const URigVMLink* Link : Model->GetLinks())
                        {
                            if (Link
                                && (Link->GetSourcePinPath().StartsWith(
                                        PinPrefix)
                                    || Link->GetTargetPinPath().StartsWith(
                                        PinPrefix)))
                            {
                                ++RerouteLinkCount;
                            }
                        }
                    }
                    const FString DirectLinkState = RigVMLinkStateString(
                        Operation.RigVMSourcePinPath,
                        Operation.RigVMTargetPinPath);
                    bOperationOk = AddedNode
                        && AddedNode->GetName() == Operation.Name
                        && RigVMNodePositionString(AddedNode)
                            == FString::Printf(
                                TEXT("X=%.6f,Y=%.6f"),
                                static_cast<double>(Operation.PositionX),
                                static_cast<double>(Operation.PositionY))
                        && !Model->ContainsLink(DirectLinkState)
                        && RerouteLinkCount == 2;
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_link"))
                {
                    const FString LinkState = RigVMLinkStateString(
                        Operation.RigVMSourcePinPath,
                        Operation.RigVMTargetPinPath);
                    bOperationOk = Controller->AddLink(
                            Operation.RigVMSourcePinPath,
                            Operation.RigVMTargetPinPath,
                            true,
                            false,
                            ERigVMPinDirection::Output,
                            false)
                        && Model->ContainsLink(LinkState);
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_link"))
                {
                    const FString LinkState = RigVMLinkStateString(
                        Operation.RigVMSourcePinPath,
                        Operation.RigVMTargetPinPath);
                    bOperationOk = Controller->BreakLink(
                            Operation.RigVMSourcePinPath,
                            Operation.RigVMTargetPinPath,
                            true,
                            false)
                        && !Model->ContainsLink(LinkState);
                }
                else if (bOperationOk)
                {
                    bOperationOk = false;
                }
                bControlRigGraphChanged = bOperationOk;
            }
            else if (Action.Contains(TEXT("control_rig_null"))
                || Action.Contains(TEXT("control_rig_curve"))
                || Action.Contains(TEXT("control_rig_float"))
                || Action.Contains(TEXT("control_rig_control"))
                || Action.Contains(TEXT("control_rig_socket"))
                || Action.Contains(TEXT("control_rig_available_space"))
                || Action.Contains(TEXT("control_rig_metadata")))
            {
                bOperationOk = ControlRigHierarchy && ControlRigController;
                const ERigElementType ElementType =
                    Action.Contains(TEXT("control_rig_null"))
                        ? ERigElementType::Null
                        : Action.Contains(TEXT("control_rig_curve"))
                            ? ERigElementType::Curve
                            : Action.Contains(TEXT("control_rig_socket"))
                                ? ERigElementType::Socket
                                : Action.Contains(
                                    TEXT("control_rig_metadata"))
                                    ? ParseControlRigElementType(
                                        Operation.ElementType).GetValue()
                                    : ERigElementType::Control;
                const FRigElementKey ExistingKey(
                    FName(*Operation.Name), ElementType);
                if (bOperationOk
                    && Action == TEXT("add_control_rig_null"))
                {
                    FRigElementKey ParentKey;
                    if (!Operation.ParentName.IsEmpty())
                    {
                        for (const FRigElementKey& Key :
                             ControlRigHierarchy->GetAllKeys(
                                 true, ERigElementType::All))
                        {
                            if (Key.Name == FName(*Operation.ParentName)
                                && (Key.Type == ERigElementType::Bone
                                    || Key.Type == ERigElementType::Null))
                            {
                                ParentKey = Key;
                                break;
                            }
                        }
                    }
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    const FRigElementKey AddedKey =
                        ControlRigController->AddNull(
                            FName(*Operation.Name),
                            ParentKey,
                            RequestedTransform,
                            false,
                            true,
                            false);
                    bOperationOk = AddedKey == ExistingKey
                        && ControlRigHierarchy->GetIndex(
                            AddedKey, true) != INDEX_NONE
                        && ControlRigHierarchy->GetLocalTransform(
                            AddedKey, true).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER)
                        && (Operation.ParentName.IsEmpty()
                            || ControlRigHierarchy->GetFirstParent(
                                AddedKey).Name
                                == FName(*Operation.ParentName));
                }
                else if (bOperationOk
                    && Action == TEXT("rename_control_rig_null"))
                {
                    const FRigElementKey RenamedKey =
                        ControlRigController->RenameElement(
                            ExistingKey,
                            FName(*Operation.NewName),
                            true,
                            false,
                            true);
                    bOperationOk = RenamedKey
                            == FRigElementKey(
                                FName(*Operation.NewName),
                                ERigElementType::Null)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE
                        && ControlRigHierarchy->GetIndex(
                            RenamedKey, true) != INDEX_NONE;
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_null_transform"))
                {
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    ControlRigHierarchy->SetLocalTransform(
                        ExistingKey,
                        RequestedTransform,
                        true,
                        true,
                        true,
                        false);
                    ControlRigHierarchy->SetLocalTransform(
                        ExistingKey,
                        RequestedTransform,
                        false,
                        true,
                        true,
                        false);
                    bOperationOk = ControlRigHierarchy->GetLocalTransform(
                            ExistingKey, true).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER)
                        && ControlRigHierarchy->GetLocalTransform(
                            ExistingKey, false).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER);
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_null"))
                {
                    bOperationOk = ControlRigController->RemoveElement(
                            ExistingKey, true, false)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE;
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_curve"))
                {
                    const FRigElementKey AddedKey =
                        ControlRigController->AddCurve(
                            FName(*Operation.Name),
                            Operation.Value,
                            true,
                            false);
                    if (AddedKey.IsValid())
                    {
                        ControlRigHierarchy->SetCurveValue(
                            AddedKey, Operation.Value, true);
                    }
                    bOperationOk = AddedKey == ExistingKey
                        && ControlRigHierarchy->GetIndex(
                            AddedKey, true) != INDEX_NONE
                        && FMath::IsNearlyEqual(
                            ControlRigHierarchy->GetCurveValue(AddedKey),
                            Operation.Value);
                }
                else if (bOperationOk
                    && Action == TEXT("rename_control_rig_curve"))
                {
                    const FRigElementKey RenamedKey =
                        ControlRigController->RenameElement(
                            ExistingKey,
                            FName(*Operation.NewName),
                            true,
                            false,
                            true);
                    bOperationOk = RenamedKey
                            == FRigElementKey(
                                FName(*Operation.NewName),
                                ERigElementType::Curve)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE
                        && ControlRigHierarchy->GetIndex(
                            RenamedKey, true) != INDEX_NONE;
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_curve_value"))
                {
                    ControlRigHierarchy->SetCurveValue(
                        ExistingKey, Operation.Value, true);
                    bOperationOk = FMath::IsNearlyEqual(
                        ControlRigHierarchy->GetCurveValue(ExistingKey),
                        Operation.Value);
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_curve"))
                {
                    bOperationOk = ControlRigController->RemoveElement(
                            ExistingKey, true, false)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE;
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_metadata"))
                {
                    const ERigMetadataType MetadataType =
                        ParseControlRigMetadataType(
                            Operation.MetadataType).GetValue();
                    const FName MetadataName(*Operation.MetadataName);
                    bOperationOk = SetControlRigMetadataValue(
                        ControlRigHierarchy,
                        ExistingKey,
                        MetadataName,
                        MetadataType,
                        Operation.MetadataValue);
                    const UEnum* MetadataTypeEnum =
                        StaticEnum<ERigMetadataType>();
                    const FString ExpectedReadback = FString::Printf(
                        TEXT("Type=%s|Value=%s"),
                        MetadataTypeEnum
                            ? *MetadataTypeEnum->GetNameStringByValue(
                                static_cast<int64>(MetadataType))
                            : TEXT("Unknown"),
                        *ControlRigMetadataValueString(
                            Operation.MetadataValue,
                            MetadataType));
                    bOperationOk = bOperationOk
                        && ControlRigMetadataEntryString(
                            ControlRigHierarchy,
                            ExistingKey,
                            MetadataName) == ExpectedReadback;
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_metadata"))
                {
                    const FName MetadataName(*Operation.MetadataName);
                    bOperationOk = ControlRigHierarchy->RemoveMetadata(
                            ExistingKey, MetadataName)
                        && ControlRigMetadataEntryString(
                            ControlRigHierarchy,
                            ExistingKey,
                            MetadataName) == TEXT("Missing");
                }
                else if (bOperationOk
                    && Action == TEXT("add_control_rig_socket"))
                {
                    FRigElementKey ParentKey;
                    if (!Operation.ParentName.IsEmpty())
                    {
                        for (const FRigElementKey& Key :
                             ControlRigHierarchy->GetAllKeys(
                                 true, ERigElementType::All))
                        {
                            if (Key.Name == FName(*Operation.ParentName)
                                && ControlRigHierarchy
                                    ->Find<FRigTransformElement>(Key))
                            {
                                ParentKey = Key;
                                break;
                            }
                        }
                    }
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    const FRigElementKey AddedKey =
                        ControlRigController->AddSocket(
                            FName(*Operation.Name),
                            ParentKey,
                            RequestedTransform,
                            false,
                            Operation.Color,
                            Operation.Description,
                            true,
                            false);
                    const FRigSocketElement* AddedSocket =
                        AddedKey.IsValid()
                            ? ControlRigHierarchy
                                ->Find<FRigSocketElement>(AddedKey)
                            : nullptr;
                    bOperationOk = AddedKey == ExistingKey
                        && AddedSocket
                        && ControlRigHierarchy->GetLocalTransform(
                            AddedKey, true).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER)
                        && AddedSocket->GetColor(ControlRigHierarchy)
                            .Equals(Operation.Color)
                        && AddedSocket->GetDescription(
                            ControlRigHierarchy) == Operation.Description
                        && (Operation.ParentName.IsEmpty()
                            || ControlRigHierarchy->GetFirstParent(
                                AddedKey).Name
                                == FName(*Operation.ParentName));
                }
                else if (bOperationOk
                    && Action == TEXT("rename_control_rig_socket"))
                {
                    const FRigElementKey RenamedKey =
                        ControlRigController->RenameElement(
                            ExistingKey,
                            FName(*Operation.NewName),
                            true,
                            false,
                            true);
                    bOperationOk = RenamedKey
                            == FRigElementKey(
                                FName(*Operation.NewName),
                                ERigElementType::Socket)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE
                        && ControlRigHierarchy->GetIndex(
                            RenamedKey, true) != INDEX_NONE;
                }
                else if (bOperationOk
                    && Action
                        == TEXT("set_control_rig_socket_transform"))
                {
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    ControlRigHierarchy->SetLocalTransform(
                        ExistingKey,
                        RequestedTransform,
                        true,
                        true,
                        true,
                        false);
                    ControlRigHierarchy->SetLocalTransform(
                        ExistingKey,
                        RequestedTransform,
                        false,
                        true,
                        true,
                        false);
                    bOperationOk = ControlRigHierarchy->GetLocalTransform(
                            ExistingKey, true).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER)
                        && ControlRigHierarchy->GetLocalTransform(
                            ExistingKey, false).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER);
                }
                else if (bOperationOk
                    && Action
                        == TEXT("set_control_rig_socket_settings"))
                {
                    FRigSocketElement* Socket = ControlRigHierarchy
                        ->Find<FRigSocketElement>(ExistingKey);
                    if (Socket)
                    {
                        Socket->SetColor(
                            Operation.Color,
                            ControlRigHierarchy,
                            true);
                        Socket->SetDescription(
                            Operation.Description,
                            ControlRigHierarchy,
                            true);
                    }
                    bOperationOk = Socket
                        && Socket->GetColor(ControlRigHierarchy)
                            .Equals(Operation.Color)
                        && Socket->GetDescription(ControlRigHierarchy)
                            == Operation.Description;
                }
                else if (bOperationOk
                    && Action == TEXT("remove_control_rig_socket"))
                {
                    bOperationOk = ControlRigController->RemoveElement(
                            ExistingKey, true, false)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE;
                }
                else if (bOperationOk
                    && Action.Contains(
                        TEXT("control_rig_available_space")))
                {
                    const FRigElementKey SpaceKey(
                        FName(*Operation.SpaceName),
                        ParseControlRigTransformElementType(
                            Operation.SpaceType).GetValue());
                    if (Action
                        == TEXT("add_control_rig_available_space"))
                    {
                        bOperationOk = ControlRigController
                            ->AddAvailableSpace(
                                ExistingKey,
                                SpaceKey,
                                FName(*Operation.DisplayLabel),
                                true,
                                false);
                    }
                    else if (Action
                        == TEXT("set_control_rig_available_space_label"))
                    {
                        bOperationOk = ControlRigController
                            ->SetAvailableSpaceLabel(
                                ExistingKey,
                                SpaceKey,
                                FName(*Operation.DisplayLabel),
                                true,
                                false);
                    }
                    else if (Action
                        == TEXT("set_control_rig_available_space_index"))
                    {
                        bOperationOk = ControlRigController
                            ->SetAvailableSpaceIndex(
                                ExistingKey,
                                SpaceKey,
                                Operation.Index,
                                true,
                                false);
                    }
                    else if (Action
                        == TEXT("remove_control_rig_available_space"))
                    {
                        bOperationOk = ControlRigController
                            ->RemoveAvailableSpace(
                                ExistingKey,
                                SpaceKey,
                                true,
                                false);
                    }
                    const FRigControlSettings ReadbackSettings =
                        ControlRigController->GetControlSettings(
                            ExistingKey);
                    const int32 ReadbackIndex =
                        ReadbackSettings.Customization.AvailableSpaces
                            .IndexOfByKey(SpaceKey);
                    if (bOperationOk
                        && Action
                            == TEXT("add_control_rig_available_space"))
                    {
                        bOperationOk = ReadbackIndex != INDEX_NONE
                            && ReadbackSettings.Customization
                                .AvailableSpaces[ReadbackIndex].Label
                                == FName(*Operation.DisplayLabel);
                    }
                    else if (bOperationOk
                        && Action
                            == TEXT("set_control_rig_available_space_label"))
                    {
                        bOperationOk = ReadbackIndex != INDEX_NONE
                            && ReadbackSettings.Customization
                                .AvailableSpaces[ReadbackIndex].Label
                                == FName(*Operation.DisplayLabel);
                    }
                    else if (bOperationOk
                        && Action
                            == TEXT("set_control_rig_available_space_index"))
                    {
                        bOperationOk = ReadbackIndex == Operation.Index;
                    }
                    else if (bOperationOk
                        && Action
                            == TEXT("remove_control_rig_available_space"))
                    {
                        bOperationOk = ReadbackIndex == INDEX_NONE;
                    }
                }
                else if (bOperationOk
                    && (Action == TEXT("add_control_rig_float_control")
                        || Action == TEXT("add_control_rig_control")))
                {
                    const ERigControlType ControlType =
                        Action == TEXT("add_control_rig_float_control")
                            ? ERigControlType::Float
                            : ParseControlRigControlType(
                                Operation.ControlType).GetValue();
                    FRigElementKey ParentKey;
                    if (!Operation.ParentName.IsEmpty())
                    {
                        for (const FRigElementKey& Key :
                             ControlRigHierarchy->GetAllKeys(
                                 true, ERigElementType::All))
                        {
                            if (Key.Name == FName(*Operation.ParentName)
                                && (Key.Type == ERigElementType::Bone
                                    || Key.Type == ERigElementType::Null))
                            {
                                ParentKey = Key;
                                break;
                            }
                        }
                    }
                    FRigControlSettings Settings;
                    Settings.ControlType = ControlType;
                    Settings.AnimationType =
                        ERigControlAnimationType::AnimationControl;
                    Settings.DisplayName = FName(*Operation.Name);
                    Settings.ShapeColor = Operation.Color;
                    const FRigControlValue RequestedValue =
                        MakeControlRigControlValue(
                            Operation, ControlType);
                    const FRigElementKey AddedKey =
                        ControlRigController->AddControl(
                            FName(*Operation.Name),
                            ParentKey,
                            Settings,
                            RequestedValue,
                            FTransform::Identity,
                            FTransform::Identity,
                            true,
                            false);
                    FRigControlElement* AddedControl =
                        AddedKey.IsValid()
                            ? ControlRigHierarchy
                                ->Find<FRigControlElement>(AddedKey)
                            : nullptr;
                    if (AddedControl)
                    {
                        ControlRigHierarchy->SetControlValue(
                            AddedControl,
                            RequestedValue,
                            ERigControlValueType::Initial,
                            true,
                            true,
                            false,
                            true);
                        ControlRigHierarchy->SetControlValue(
                            AddedControl,
                            RequestedValue,
                            ERigControlValueType::Current,
                            true,
                            true,
                            false,
                            true);
                    }
                    const FRigControlSettings ReadbackSettings =
                        AddedKey.IsValid()
                            ? ControlRigController->GetControlSettings(
                                AddedKey)
                            : FRigControlSettings();
                    bOperationOk = AddedKey == ExistingKey
                        && ControlRigHierarchy->GetIndex(
                            AddedKey, true) != INDEX_NONE
                        && ReadbackSettings.ControlType
                            == ControlType
                        && ControlRigControlValueMatches(
                            ControlRigHierarchy,
                            AddedKey,
                            ControlType,
                            ERigControlValueType::Initial,
                            Operation)
                        && ControlRigControlValueMatches(
                            ControlRigHierarchy,
                            AddedKey,
                            ControlType,
                            ERigControlValueType::Current,
                            Operation)
                        && (Operation.ParentName.IsEmpty()
                            || ControlRigHierarchy->GetFirstParent(
                                AddedKey).Name
                                == FName(*Operation.ParentName));
                }
                else if (bOperationOk
                    && (Action == TEXT("rename_control_rig_float_control")
                        || Action == TEXT("rename_control_rig_control")))
                {
                    const FRigElementKey RenamedKey =
                        ControlRigController->RenameElement(
                            ExistingKey,
                            FName(*Operation.NewName),
                            true,
                            false,
                            true);
                    bOperationOk = RenamedKey
                            == FRigElementKey(
                                FName(*Operation.NewName),
                                ERigElementType::Control)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE
                        && ControlRigHierarchy->GetIndex(
                            RenamedKey, true) != INDEX_NONE;
                }
                else if (bOperationOk
                    && Action
                        == TEXT("set_control_rig_control_shape_settings"))
                {
                    FRigControlElement* ExistingControl =
                        ControlRigHierarchy
                            ->Find<FRigControlElement>(ExistingKey);
                    FRigControlSettings Settings =
                        ControlRigController->GetControlSettings(
                            ExistingKey);
                    Settings.ShapeName = FName(*Operation.ControlShapeName);
                    Settings.ShapeColor = Operation.Color;
                    Settings.SetVisible(
                        Operation.bControlShapeVisible, true);
                    if (ExistingControl)
                    {
                        ControlRigHierarchy->SetControlSettings(
                            ExistingControl,
                            Settings,
                            true,
                            true,
                            false);
                    }
                    const FRigControlSettings ReadbackSettings =
                        ControlRigController->GetControlSettings(
                            ExistingKey);
                    bOperationOk = ExistingControl
                        && ControlRigControlShapeSettingsString(
                            ReadbackSettings)
                            == ControlRigControlShapeSettingsString(
                                Settings);
                }
                else if (bOperationOk
                    && Action
                        == TEXT("set_control_rig_control_shape_transform"))
                {
                    FRigControlElement* ExistingControl =
                        ControlRigHierarchy
                            ->Find<FRigControlElement>(ExistingKey);
                    const FTransform RequestedTransform(
                        Operation.Rotation,
                        Operation.Location,
                        Operation.Scale);
                    if (ExistingControl)
                    {
                        ControlRigHierarchy->SetControlShapeTransform(
                            ExistingControl,
                            RequestedTransform,
                            ERigTransformType::InitialLocal,
                            true,
                            true,
                            false);
                        ControlRigHierarchy->SetControlShapeTransform(
                            ExistingControl,
                            RequestedTransform,
                            ERigTransformType::CurrentLocal,
                            true,
                            true,
                            false);
                    }
                    bOperationOk = ExistingControl
                        && ControlRigHierarchy->GetControlShapeTransform(
                            ExistingControl,
                            ERigTransformType::InitialLocal).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER)
                        && ControlRigHierarchy->GetControlShapeTransform(
                            ExistingControl,
                            ERigTransformType::CurrentLocal).Equals(
                                RequestedTransform,
                                KINDA_SMALL_NUMBER);
                }
                else if (bOperationOk
                    && Action == TEXT("set_control_rig_control_limits"))
                {
                    const ERigControlType ControlType =
                        ParseControlRigControlType(
                            Operation.ControlType).GetValue();
                    FRigControlElement* ExistingControl =
                        ControlRigHierarchy
                            ->Find<FRigControlElement>(ExistingKey);
                    FRigControlSettings Settings =
                        ControlRigController->GetControlSettings(
                            ExistingKey);
                    SetupControlRigLimitArrayForType(
                        Settings, ControlType);
                    const bool bHasPerChannelFlags =
                        !Operation
                            .ControlMinimumLimitEnabledPerChannel
                            .IsEmpty()
                        || !Operation
                            .ControlMaximumLimitEnabledPerChannel
                            .IsEmpty();
                    for (int32 Index = 0;
                         Index < Settings.LimitEnabled.Num();
                         ++Index)
                    {
                        Settings.LimitEnabled[Index].Set(
                            bHasPerChannelFlags
                                ? Operation
                                    .ControlMinimumLimitEnabledPerChannel[
                                        Index]
                                : Operation
                                    .bControlMinimumLimitEnabled,
                            bHasPerChannelFlags
                                ? Operation
                                    .ControlMaximumLimitEnabledPerChannel[
                                        Index]
                                : Operation
                                    .bControlMaximumLimitEnabled);
                    }
                    Settings.MinimumValue = MakeControlRigControlValue(
                        Operation.ControlMinimumValue, ControlType);
                    Settings.MaximumValue = MakeControlRigControlValue(
                        Operation.ControlMaximumValue, ControlType);
                    Settings.bDrawLimits = Operation.bDrawControlLimits;
                    if (ExistingControl)
                    {
                        ControlRigHierarchy->SetControlSettings(
                            ExistingControl,
                            Settings,
                            true,
                            true,
                            false);
                    }
                    const FRigControlSettings ReadbackSettings =
                        ControlRigController->GetControlSettings(
                            ExistingKey);
                    bOperationOk = ExistingControl
                        && ControlRigControlLimitSettingsString(
                            ReadbackSettings)
                            == ControlRigControlLimitSettingsString(
                                Settings);
                }
                else if (bOperationOk
                    && (Action == TEXT("set_control_rig_float_value")
                        || Action
                            == TEXT("set_control_rig_control_value")))
                {
                    const ERigControlType ControlType =
                        Action == TEXT("set_control_rig_float_value")
                            ? ERigControlType::Float
                            : ParseControlRigControlType(
                                Operation.ControlType).GetValue();
                    const FRigControlValue RequestedValue =
                        MakeControlRigControlValue(
                            Operation, ControlType);
                    FRigControlElement* ExistingControl =
                        ControlRigHierarchy
                            ->Find<FRigControlElement>(ExistingKey);
                    if (ExistingControl)
                    {
                        ControlRigHierarchy->SetControlValue(
                            ExistingControl,
                            RequestedValue,
                            ERigControlValueType::Initial,
                            true,
                            true,
                            false,
                            true);
                        ControlRigHierarchy->SetControlValue(
                            ExistingControl,
                            RequestedValue,
                            ERigControlValueType::Current,
                            true,
                            true,
                            false,
                            true);
                    }
                    bOperationOk = ExistingControl
                        && ControlRigControlValueMatches(
                            ControlRigHierarchy,
                            ExistingKey,
                            ControlType,
                            ERigControlValueType::Initial,
                            Operation)
                        && ControlRigControlValueMatches(
                            ControlRigHierarchy,
                            ExistingKey,
                            ControlType,
                            ERigControlValueType::Current,
                            Operation);
                }
                else if (bOperationOk
                    && (Action == TEXT("remove_control_rig_float_control")
                        || Action == TEXT("remove_control_rig_control")))
                {
                    bOperationOk = ControlRigController->RemoveElement(
                            ExistingKey, true, false)
                        && ControlRigHierarchy->GetIndex(
                            ExistingKey, true) == INDEX_NONE;
                }
                else
                {
                    bOperationOk = false;
                }
            }
            else if (Action == TEXT("set_ik_retargeter_rig"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                UIKRigDefinition* ReferencedRig = Cast<UIKRigDefinition>(
                    FindAsset(Operation.ReferencedAssetPath));
                bOperationOk = RetargetController && Side.IsSet()
                    && ReferencedRig;
                if (bOperationOk)
                {
                    RetargetController->SetIKRig(
                        Side.GetValue(), ReferencedRig);
                    RetargetController->AssignIKRigToAllOps(
                        Side.GetValue(), ReferencedRig);
                    bOperationOk = RetargetController->GetIKRig(
                        Side.GetValue()) == ReferencedRig;
                }
            }
            else if (Action == TEXT("add_default_ik_retarget_ops"))
            {
                bOperationOk = RetargetController
                    && RetargetController->GetNumRetargetOps() == 0;
                if (bOperationOk)
                {
                    RetargetController->AddDefaultOps();
                    bOperationOk =
                        RetargetController->GetNumRetargetOps() == 5;
                }
            }
            else if (Action == TEXT("add_ik_retarget_op"))
            {
                const UScriptStruct* OpType =
                    ResolveIKRetargetOpType(Operation.ClassPath);
                const int32 NewIndex = RetargetController && OpType
                    ? RetargetController->AddRetargetOp(OpType, NAME_None)
                    : INDEX_NONE;
                const FInstancedStruct* AddedOp =
                    RetargetController && NewIndex != INDEX_NONE
                        ? RetargetController->GetRetargetOpStructAtIndex(
                            NewIndex)
                        : nullptr;
                bOperationOk = AddedOp
                    && AddedOp->GetScriptStruct() == OpType;
            }
            else if (Action == TEXT("remove_ik_retarget_op"))
            {
                const int32 CountBefore = RetargetController
                    ? RetargetController->GetNumRetargetOps() : 0;
                bOperationOk = RetargetController
                    && RetargetController->RemoveRetargetOp(Operation.Index)
                    && RetargetController->GetNumRetargetOps()
                        < CountBefore;
            }
            else if (Action == TEXT("rename_ik_retarget_op"))
            {
                bOperationOk = RetargetController
                    && RetargetController->SetOpName(
                        FName(*Operation.NewName), Operation.Index)
                        == FName(*Operation.NewName)
                    && RetargetController->GetOpName(Operation.Index)
                        == FName(*Operation.NewName);
            }
            else if (Action == TEXT("move_ik_retarget_op"))
            {
                const FName MovedName = RetargetController
                    ? RetargetController->GetOpName(Operation.Index)
                    : NAME_None;
                const FInstancedStruct* MovedOp = RetargetController
                    ? RetargetController->GetRetargetOpStructAtIndex(
                        Operation.Index)
                    : nullptr;
                const UScriptStruct* MovedType = MovedOp
                    ? MovedOp->GetScriptStruct() : nullptr;
                bOperationOk = RetargetController && MovedType
                    && RetargetController->MoveRetargetOpInStack(
                        Operation.Index, Operation.TargetIndex)
                    && RetargetController->GetOpName(Operation.TargetIndex)
                        == MovedName
                    && RetargetController->GetRetargetOpStructAtIndex(
                        Operation.TargetIndex)
                    && RetargetController->GetRetargetOpStructAtIndex(
                        Operation.TargetIndex)->GetScriptStruct() == MovedType;
            }
            else if (Action == TEXT("set_ik_retarget_op_enabled"))
            {
                bOperationOk = RetargetController
                    && RetargetController->SetRetargetOpEnabled(
                        Operation.Index, Operation.bEnabled)
                    && RetargetController->GetRetargetOpEnabled(
                        Operation.Index) == Operation.bEnabled;
            }
            else if (Action == TEXT("add_ik_retarget_override_set"))
            {
                bOperationOk = RetargetController
                    && RetargetController->AddNewRetargetOverrideSet(
                        FName(*Operation.Name)) == FName(*Operation.Name)
                    && RetargetController->GetAllOverrideSets().Contains(
                        FName(*Operation.Name));
            }
            else if (Action == TEXT("rename_ik_retarget_override_set"))
            {
                bOperationOk = RetargetController
                    && RetargetController->RenameRetargetOverrideSet(
                        FName(*Operation.Name), FName(*Operation.NewName))
                    && !RetargetController->GetAllOverrideSets().Contains(
                        FName(*Operation.Name))
                    && RetargetController->GetAllOverrideSets().Contains(
                        FName(*Operation.NewName));
            }
            else if (Action == TEXT("remove_ik_retarget_override_set"))
            {
                bOperationOk = RetargetController
                    && RetargetController->RemoveRetargetOverrideSet(
                        FName(*Operation.Name))
                    && !RetargetController->GetAllOverrideSets().Contains(
                        FName(*Operation.Name));
            }
            else if (Action == TEXT("set_ik_retarget_override_set_active"))
            {
                bOperationOk = RetargetController
                    && RetargetController->SetOverrideSetActiveByDefault(
                        FName(*Operation.Name), Operation.bEnabled)
                    && RetargetController->GetOverrideSetActiveByDefault(
                        FName(*Operation.Name)) == Operation.bEnabled;
            }
            else if (Action == TEXT("set_ik_retarget_override_set_parent"))
            {
                const FName ParentName = Operation.ParentName.IsEmpty()
                    ? NAME_None : FName(*Operation.ParentName);
                bOperationOk = RetargetController
                    && RetargetController->SetParentOverrideSet(
                        FName(*Operation.Name), ParentName)
                    && RetargetController->GetParentOverrideSet(
                        FName(*Operation.Name)) == ParentName;
            }
            else if (Action == TEXT("set_ik_retarget_property_override"))
            {
                bOperationOk = ApplyIKRetargetPropertyOverride(
                    RetargetController,
                    FName(*Operation.Name),
                    FName(*Operation.RetargetOpName),
                    Operation.PropertyName,
                    Operation.ExpectedValue,
                    Operation.SettingValue);
            }
            else if (Action == TEXT("remove_ik_retarget_property_override"))
            {
                bOperationOk = RetargetController
                    && RetargetController->RemovePropertyOverrideFromOp(
                        FName(*Operation.Name),
                        FName(*Operation.RetargetOpName),
                        Operation.PropertyName)
                    && !RetargetController->HasPropertyOverride(
                        FName(*Operation.Name),
                        FName(*Operation.RetargetOpName),
                        Operation.PropertyName);
            }
            else if (Action == TEXT("add_ik_retarget_pose"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                bOperationOk = RetargetController && Side.IsSet()
                    && RetargetController->CreateRetargetPose(
                        FName(*Operation.Name), Side.GetValue())
                        == FName(*Operation.Name)
                    && IKRetargeter->GetRetargetPoseByName(
                        Side.GetValue(), FName(*Operation.Name));
            }
            else if (Action == TEXT("rename_ik_retarget_pose"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                bOperationOk = RetargetController && Side.IsSet()
                    && RetargetController->RenameRetargetPose(
                        FName(*Operation.Name),
                        FName(*Operation.NewName),
                        Side.GetValue())
                    && !IKRetargeter->GetRetargetPoseByName(
                        Side.GetValue(), FName(*Operation.Name))
                    && IKRetargeter->GetRetargetPoseByName(
                        Side.GetValue(), FName(*Operation.NewName));
            }
            else if (Action == TEXT("remove_ik_retarget_pose"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                bOperationOk = RetargetController && Side.IsSet()
                    && RetargetController->RemoveRetargetPose(
                        FName(*Operation.Name), Side.GetValue())
                    && !IKRetargeter->GetRetargetPoseByName(
                        Side.GetValue(), FName(*Operation.Name));
            }
            else if (Action == TEXT("set_current_ik_retarget_pose"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                bOperationOk = RetargetController && Side.IsSet()
                    && RetargetController->SetCurrentRetargetPose(
                        FName(*Operation.Name), Side.GetValue())
                    && RetargetController->GetCurrentRetargetPoseName(
                        Side.GetValue()) == FName(*Operation.Name);
            }
            else if (Action == TEXT("set_ik_retarget_pose_root_offset"))
            {
                const TOptional<ERetargetSourceOrTarget> Side =
                    ParseRetargetSide(Operation.RetargetSide);
                bOperationOk = RetargetController && Side.IsSet();
                if (bOperationOk)
                {
                    RetargetController->SetRootOffsetInRetargetPose(
                        Operation.Location, Side.GetValue());
                    const FVector Readback = RetargetController
                        ->GetRootOffsetInRetargetPose(Side.GetValue());
                    Result.Diagnostics.Add(FString::Printf(
                        TEXT("retarget_pose_root_offset requested=%s readback=%s"),
                        *Operation.Location.ToString(),
                        *Readback.ToString()));
                    bOperationOk = Readback.Equals(
                        Operation.Location, KINDA_SMALL_NUMBER);
                }
            }
            else if (Action == TEXT("set_ik_root_motion_bone"))
            {
                bOperationOk = IKController
                    && IKController->SetRootMotionBone(
                        FName(*Operation.BoneName))
                    && IKController->GetRootMotionBone()
                        == FName(*Operation.BoneName);
            }
            else if (Action == TEXT("set_ik_bone_excluded"))
            {
                bOperationOk = IKController
                    && IKController->SetBoneExcluded(
                        FName(*Operation.BoneName),
                        Operation.bIKBoneExcluded)
                    && IKController->GetBoneExcluded(
                        FName(*Operation.BoneName))
                        == Operation.bIKBoneExcluded;
            }
            else if (Action == TEXT("set_ik_solver_setting")
                || Action == TEXT("set_ik_goal_setting")
                || Action == TEXT("set_ik_bone_setting"))
            {
                const EIKRigSettingScope Scope =
                    Action == TEXT("set_ik_solver_setting")
                        ? EIKRigSettingScope::Solver
                        : (Action == TEXT("set_ik_goal_setting")
                            ? EIKRigSettingScope::Goal
                            : EIKRigSettingScope::Bone);
                const FName Subject = Scope == EIKRigSettingScope::Goal
                    ? FName(*Operation.GoalName)
                    : (Scope == EIKRigSettingScope::Bone
                        ? FName(*Operation.BoneName)
                        : NAME_None);
                bOperationOk = ApplyIKRigSetting(
                    IKController,
                    Scope,
                    Operation.Index,
                    Subject,
                    Operation.PropertyName,
                    Operation.ExpectedValue,
                    Operation.SettingValue);
            }
            else if (Action == TEXT("set_ik_limb_solver_settings"))
            {
                UIKRigLimbSolverController* LimbController = IKController
                    ? Cast<UIKRigLimbSolverController>(
                        IKController->GetSolverController(
                            Operation.Index))
                    : nullptr;
                if (LimbController)
                {
                    FIKRigLimbSolverSettings Settings =
                        LimbController->GetSolverSettings();
                    Settings.ReachPrecision =
                        Operation.IKReachPrecision;
                    Settings.MaxIterations =
                        Operation.IKMaxIterations;
                    Settings.bEnableLimit =
                        Operation.bIKEnableRotationLimit;
                    Settings.MinRotationAngle =
                        Operation.IKMinRotationAngle;
                    Settings.bAveragePull = Operation.bIKAveragePull;
                    Settings.PullDistribution =
                        Operation.IKPullDistribution;
                    Settings.ReachStepAlpha =
                        Operation.IKReachStepAlpha;
                    Settings.bEnableTwistCorrection =
                        Operation.bIKEnableTwistCorrection;
                    Settings.HingeRotationAxis =
                        ParseIKAxis(Operation.IKHingeRotationAxis)
                            .GetValue();
                    Settings.EndBoneForwardAxis =
                        ParseIKAxis(Operation.IKEndBoneForwardAxis)
                            .GetValue();
                    LimbController->SetSolverSettings(Settings);
                    const FIKRigLimbSolverSettings Readback =
                        LimbController->GetSolverSettings();
                    bOperationOk = FMath::IsNearlyEqual(
                            Readback.ReachPrecision,
                            Operation.IKReachPrecision)
                        && Readback.MaxIterations
                            == Operation.IKMaxIterations
                        && Readback.bEnableLimit
                            == Operation.bIKEnableRotationLimit
                        && FMath::IsNearlyEqual(
                            Readback.MinRotationAngle,
                            Operation.IKMinRotationAngle)
                        && Readback.bAveragePull
                            == Operation.bIKAveragePull
                        && FMath::IsNearlyEqual(
                            Readback.PullDistribution,
                            Operation.IKPullDistribution)
                        && FMath::IsNearlyEqual(
                            Readback.ReachStepAlpha,
                            Operation.IKReachStepAlpha)
                        && Readback.bEnableTwistCorrection
                            == Operation.bIKEnableTwistCorrection
                        && Readback.HingeRotationAxis
                            == ParseIKAxis(
                                Operation.IKHingeRotationAxis).GetValue()
                        && Readback.EndBoneForwardAxis
                            == ParseIKAxis(
                                Operation.IKEndBoneForwardAxis).GetValue();
                }
                else
                {
                    bOperationOk = false;
                }
            }
            else if (Action == TEXT("set_ik_retarget_root"))
            {
                bOperationOk = IKController
                    && IKController->SetRetargetRoot(
                        FName(*Operation.BoneName));
            }
            else if (Action == TEXT("add_ik_goal"))
            {
                bOperationOk = IKController
                    && IKController->AddNewGoal(
                        FName(*Operation.Name),
                        FName(*Operation.BoneName))
                        == FName(*Operation.Name);
            }
            else if (Action == TEXT("remove_ik_goal"))
            {
                bOperationOk = IKController
                    && IKController->RemoveGoal(
                        FName(*Operation.Name));
            }
            else if (Action == TEXT("add_ik_retarget_chain"))
            {
                bOperationOk = IKController
                    && IKController->AddRetargetChain(
                        FName(*Operation.Name),
                        FName(*Operation.StartBoneName),
                        FName(*Operation.EndBoneName),
                        FName(*Operation.GoalName))
                        == FName(*Operation.Name);
            }
            else if (Action == TEXT("remove_ik_retarget_chain"))
            {
                bOperationOk = IKController
                    && IKController->RemoveRetargetChain(
                        FName(*Operation.Name));
            }
            else if (Action == TEXT("add_ik_solver"))
            {
                bOperationOk = IKController
                    && IKController->AddSolver(Operation.ClassPath)
                        != INDEX_NONE;
            }
            else if (Action == TEXT("remove_ik_solver"))
            {
                bOperationOk = IKController
                    && IKController->RemoveSolver(Operation.Index);
            }
            else if (Action == TEXT("set_ik_solver_enabled"))
            {
                bOperationOk = IKController
                    && IKController->SetSolverEnabled(
                        Operation.Index, Operation.bEnabled);
            }
            else if (Action == TEXT("set_ik_solver_start_bone"))
            {
                bOperationOk = IKController
                    && IKController->SetStartBone(
                        FName(*Operation.BoneName), Operation.Index)
                    && IKController->GetStartBone(Operation.Index)
                        == FName(*Operation.BoneName);
            }
            else if (Action == TEXT("set_ik_solver_end_bone"))
            {
                bOperationOk = IKController
                    && IKController->SetEndBone(
                        FName(*Operation.BoneName), Operation.Index)
                    && IKController->GetEndBone(Operation.Index)
                        == FName(*Operation.BoneName);
            }
            else if (Action == TEXT("move_ik_solver"))
            {
                const FInstancedStruct* Solver = IKController
                    ? IKController->GetSolverStructAtIndex(Operation.Index)
                    : nullptr;
                const UScriptStruct* SolverType = Solver
                    ? Solver->GetScriptStruct() : nullptr;
                bOperationOk = SolverType && IKController->MoveSolverInStack(
                        Operation.Index, Operation.TargetIndex)
                    && IKController->GetSolverStructAtIndex(
                        Operation.TargetIndex)
                    && IKController->GetSolverStructAtIndex(
                        Operation.TargetIndex)->GetScriptStruct()
                        == SolverType;
            }
            else if (Action == TEXT("add_ik_bone_setting"))
            {
                bOperationOk = IKController
                    && IKController->AddBoneSetting(
                        FName(*Operation.BoneName), Operation.Index)
                    && IKController->CanRemoveBoneSetting(
                        FName(*Operation.BoneName), Operation.Index);
            }
            else if (Action == TEXT("remove_ik_bone_setting"))
            {
                bOperationOk = IKController
                    && IKController->RemoveBoneSetting(
                        FName(*Operation.BoneName), Operation.Index)
                    && !IKController->CanRemoveBoneSetting(
                        FName(*Operation.BoneName), Operation.Index);
            }
            else if (Action == TEXT("connect_ik_goal_to_solver"))
            {
                bOperationOk = IKController
                    && IKController->ConnectGoalToSolver(
                        FName(*Operation.GoalName), Operation.Index);
            }
            else if (Action == TEXT("disconnect_ik_goal_from_solver"))
            {
                bOperationOk = IKController
                    && IKController->DisconnectGoalFromSolver(
                        FName(*Operation.GoalName), Operation.Index);
            }
            else if (Action == TEXT("add_state_machine"))
            {
                bOperationOk = AddStateMachineNode(AnimBlueprint, Operation) != nullptr;
                bBlueprintStructureChanged = bOperationOk;
            }
            else if (Action == TEXT("rename_state_machine"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = Machine && Machine->EditorStateMachineGraph;
                if (bOperationOk)
                {
                    FBlueprintEditorUtils::RenameGraph(
                        Machine->EditorStateMachineGraph, Operation.NewName);
                    bOperationOk = Machine->GetStateMachineName() == Operation.NewName;
                    bBlueprintStructureChanged = bOperationOk;
                }
            }
            else if (Action == TEXT("remove_state_machine"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = Machine != nullptr;
                if (Machine)
                {
                    Machine->DestroyNode();
                    bBlueprintStructureChanged = true;
                }
            }
            else if (Action == TEXT("add_state"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = AddStateNode(Machine, Operation) != nullptr;
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("rename_state"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimStateNode* State = FindState(Machine, Operation.StateName);
                bOperationOk = State != nullptr;
                if (State)
                {
                    State->OnRenameNode(Operation.NewName);
                    bOperationOk = State->GetStateName() == Operation.NewName;
                    bBlueprintStructureChanged |= bOperationOk;
                }
            }
            else if (Action == TEXT("remove_state"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimStateNode* State = FindState(Machine, Operation.StateName);
                bOperationOk = State != nullptr;
                if (State)
                {
                    TArray<UAnimStateTransitionNode*> ConnectedTransitions;
                    State->GetTransitionList(ConnectedTransitions, false);
                    for (UAnimStateTransitionNode* Transition : ConnectedTransitions)
                    {
                        if (Transition)
                        {
                            Transition->DestroyNode();
                        }
                    }
                    State->DestroyNode();
                    bBlueprintStructureChanged = true;
                }
            }
            else if (Action == TEXT("set_entry_state"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = SetEntryState(Machine, FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (IsGenericStateGraphAction(Action))
            {
                bOperationOk = ApplyGenericStateGraphOperation(
                    Operation, AnimBlueprint);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_sequence_player"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimSequenceBase* PlayerSequence = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.ReferencedAssetPath));
                bOperationOk = SetStateSequencePlayer(
                    FindState(Machine, Operation.StateName),
                    PlayerSequence,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_sequence_player"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateSequencePlayer(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_blend_by_bool"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimSequenceBase* FalseSequence = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.ReferencedAssetPath));
                UAnimSequenceBase* TrueSequence = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.SecondaryAssetPath));
                bOperationOk = SetStateBlendByBool(
                    FindState(Machine, Operation.StateName),
                    AnimBlueprint,
                    FalseSequence,
                    TrueSequence,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_blend_by_bool"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateBlendByBool(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_blend_space_player"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UBlendSpace* BlendSpace = Cast<UBlendSpace>(
                    FindAsset(Operation.ReferencedAssetPath));
                bOperationOk = SetStateBlendSpacePlayer(
                    FindState(Machine, Operation.StateName),
                    AnimBlueprint,
                    BlendSpace,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_blend_space_player"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateBlendSpacePlayer(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_blend_list_by_int"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                TArray<UAnimSequenceBase*> Sequences;
                for (const FString& ReferencedPath :
                     Operation.ReferencedAssetPaths)
                {
                    Sequences.Add(Cast<UAnimSequenceBase>(
                        FindAsset(ReferencedPath)));
                }
                bOperationOk = SetStateBlendListByInt(
                    FindState(Machine, Operation.StateName),
                    AnimBlueprint,
                    Sequences,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_blend_list_by_int"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateBlendListByInt(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_apply_additive"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimSequenceBase* BaseSequence = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.ReferencedAssetPath));
                UAnimSequence* AdditiveSequence = Cast<UAnimSequence>(
                    FindAsset(Operation.SecondaryAssetPath));
                bOperationOk = SetStateApplyAdditive(
                    FindState(Machine, Operation.StateName),
                    AnimBlueprint,
                    BaseSequence,
                    AdditiveSequence,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_apply_additive"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateApplyAdditive(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_state_layered_blend_per_bone"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimSequenceBase* BaseSequence = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.ReferencedAssetPath));
                TArray<UAnimSequenceBase*> LayerSequences;
                for (const FString& ReferencedPath :
                     Operation.ReferencedAssetPaths)
                {
                    LayerSequences.Add(Cast<UAnimSequenceBase>(
                        FindAsset(ReferencedPath)));
                }
                bOperationOk = SetStateLayeredBlendPerBone(
                    FindState(Machine, Operation.StateName),
                    AnimBlueprint,
                    BaseSequence,
                    LayerSequences,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_state_layered_blend_per_bone"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveStateLayeredBlendPerBone(
                    FindState(Machine, Operation.StateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("add_transition"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = AddTransitionNode(
                    Machine,
                    FindState(Machine, Operation.FromStateName),
                    FindState(Machine, Operation.ToStateName),
                    Operation) != nullptr;
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_transition_rule_constant"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = SetTransitionRuleConstant(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName),
                    Operation.bRuleValue);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_transition_rule_variable"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = SetTransitionRuleVariable(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName),
                    AnimBlueprint,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_transition_rule_variable"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveTransitionRuleVariable(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_transition_rule_time_remaining"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = SetTransitionRuleTimeRemaining(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName),
                    AnimBlueprint,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_transition_rule_time_remaining"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveTransitionRuleTimeRemaining(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName));
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_transition_rule_expression"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = SetTransitionRuleExpression(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName),
                    AnimBlueprint,
                    Operation);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("remove_transition_rule_expression"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                bOperationOk = RemoveTransitionRuleExpression(
                    FindTransition(
                        Machine,
                        Operation.FromStateName,
                        Operation.ToStateName),
                    AnimBlueprint);
                bBlueprintStructureChanged |= bOperationOk;
            }
            else if (Action == TEXT("set_transition"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimStateTransitionNode* Transition = FindTransition(
                    Machine, Operation.FromStateName, Operation.ToStateName);
                bOperationOk = Transition != nullptr;
                if (Transition)
                {
                    Transition->Modify();
                    Transition->CrossfadeDuration = Operation.Duration;
                    Transition->PriorityOrder = Operation.PriorityOrder;
                    Transition->bAutomaticRuleBasedOnSequencePlayerInState =
                        Operation.bAutomaticTransitionRule;
                    Transition->bDisabled = !Operation.bEnabled;
                    Transition->PostEditChange();
                    bBlueprintStructureChanged = true;
                }
            }
            else if (Action == TEXT("remove_transition"))
            {
                UAnimGraphNode_StateMachine* Machine =
                    FindStateMachine(AnimBlueprint, Operation.MachineName);
                UAnimStateTransitionNode* Transition = FindTransition(
                    Machine, Operation.FromStateName, Operation.ToStateName);
                bOperationOk = Transition != nullptr;
                if (Transition)
                {
                    Transition->DestroyNode();
                    bBlueprintStructureChanged = true;
                }
            }
            else if (Action == TEXT("set_root_motion"))
            {
                Sequence->bEnableRootMotion = Operation.bEnabled;
                Sequence->RootMotionRootLock = ParseRootMotionLock(Operation.RootMotionLock).GetValue();
                Sequence->bForceRootLock = Operation.bForceRootLock;
            }
            else if (Action == TEXT("set_additive_settings"))
            {
                Sequence->AdditiveAnimType =
                    ParseAdditiveType(Operation.AdditiveType).GetValue();
                Sequence->RefPoseType =
                    ParseAdditiveBasePoseType(
                        Operation.AdditiveBasePoseType).GetValue();
                Sequence->RefPoseSeq = Operation.AdditiveBasePoseAssetPath.IsEmpty()
                    ? nullptr
                    : Cast<UAnimSequence>(
                        FindAsset(Operation.AdditiveBasePoseAssetPath));
                Sequence->RefFrameIndex = Operation.AdditiveBaseFrame;
                Sequence->PostEditChange();
            }
            else if (Action == TEXT("add_notify_track"))
            {
                UAnimationBlueprintLibrary::AddAnimationNotifyTrack(
                    SequenceBase, FName(Operation.TrackName), Operation.Color);
                bOperationOk = UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(
                    SequenceBase, FName(Operation.TrackName));
            }
            else if (Action == TEXT("remove_notify_track"))
            {
                UAnimationBlueprintLibrary::RemoveAnimationNotifyTrack(SequenceBase, FName(Operation.TrackName));
                bOperationOk = !UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(
                    SequenceBase, FName(Operation.TrackName));
            }
            else if (Action == TEXT("add_notify"))
            {
                UClass* NotifyClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                bOperationOk = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
                    SequenceBase, FName(Operation.TrackName), Operation.Time, NotifyClass) != nullptr;
            }
            else if (Action == TEXT("add_notify_state"))
            {
                UClass* NotifyClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                bOperationOk = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
                    SequenceBase, FName(Operation.TrackName), Operation.Time,
                    Operation.Duration, NotifyClass) != nullptr;
            }
            else if (Action == TEXT("remove_notify_by_name"))
            {
                bOperationOk = UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByName(
                    SequenceBase, FName(Operation.Name)) > 0;
            }
            else if (Action == TEXT("remove_notify_by_track"))
            {
                bOperationOk = UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByTrack(
                    SequenceBase, FName(Operation.TrackName)) > 0;
            }
            else if (Action.Contains(TEXT("curve")))
            {
                IAnimationDataController& Controller = Sequence->GetController();
                const FAnimationCurveIdentifier CurveId(
                    FName(Operation.Name), ERawCurveTrackTypes::RCT_Float);
                if (Action == TEXT("add_curve"))
                {
                    bOperationOk = Controller.AddCurve(CurveId, AACF_DefaultCurve, false);
                }
                else if (Action == TEXT("remove_curve"))
                {
                    bOperationOk = Controller.RemoveCurve(CurveId, false);
                }
                else if (Action == TEXT("set_curve_key"))
                {
                    bOperationOk = Controller.SetCurveKey(
                        CurveId, FRichCurveKey(Operation.Time, Operation.Value), false);
                }
                else
                {
                    bOperationOk = Controller.RemoveCurveKey(CurveId, Operation.Time, false);
                }
            }
            else if (Action == TEXT("add_montage_section"))
            {
                bOperationOk = Montage->AddAnimCompositeSection(
                    FName(Operation.SectionName), Operation.Time) != INDEX_NONE;
            }
            else if (Action == TEXT("remove_montage_section"))
            {
                bOperationOk = Montage->DeleteAnimCompositeSection(
                    FindMontageSection(*Montage, Operation.SectionName));
            }
            else if (Action == TEXT("rename_montage_section"))
            {
                const FName OldName(Operation.SectionName);
                FCompositeSection& Section = Montage->GetAnimCompositeSection(
                    FindMontageSection(*Montage, Operation.SectionName));
                Section.SectionName = FName(Operation.NewName);
                for (FCompositeSection& OtherSection : Montage->CompositeSections)
                {
                    if (OtherSection.NextSectionName == OldName)
                    {
                        OtherSection.NextSectionName = Section.SectionName;
                    }
                }
            }
            else if (Action == TEXT("set_next_montage_section"))
            {
                FCompositeSection& Section = Montage->GetAnimCompositeSection(
                    FindMontageSection(*Montage, Operation.SectionName));
                Section.NextSectionName = FName(Operation.NextSectionName);
            }
            else if (Action == TEXT("add_montage_slot"))
            {
                FSlotAnimationTrack Slot;
                Slot.SlotName = FName(Operation.SlotName);
                Montage->SlotAnimTracks.Add(MoveTemp(Slot));
            }
            else if (Action == TEXT("remove_montage_slot"))
            {
                Montage->SlotAnimTracks.RemoveAt(
                    FindMontageSlot(*Montage, Operation.SlotName), 1, EAllowShrinking::No);
            }
            else if (Action == TEXT("add_montage_segment"))
            {
                UAnimSequenceBase* Referenced = Cast<UAnimSequenceBase>(
                    FindAsset(Operation.ReferencedAssetPath));
                FAnimSegment Segment;
                Segment.SetAnimReference(Referenced, true);
                Segment.StartPos = Operation.Time;
                Segment.AnimStartTime = Operation.Duration;
                Segment.AnimEndTime = Operation.EndTime;
                Segment.AnimPlayRate = Operation.PlayRate;
                Segment.LoopingCount = Operation.LoopCount;
#if WITH_EDITOR
                Segment.UpdateCachedPlayLength();
#endif
                Montage->SlotAnimTracks[FindMontageSlot(*Montage, Operation.SlotName)]
                    .AnimTrack.AnimSegments.Add(MoveTemp(Segment));
            }
            else if (Action == TEXT("remove_montage_segment"))
            {
                TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[
                    FindMontageSlot(*Montage, Operation.SlotName)].AnimTrack.AnimSegments;
                Segments.RemoveAt(Operation.Index, 1, EAllowShrinking::No);
            }
            else if (Action == TEXT("add_socket"))
            {
                USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(
                    Skeleton, NAME_None, RF_Transactional);
                Socket->SocketName = FName(Operation.Name);
                Socket->BoneName = FName(Operation.BoneName);
                Socket->RelativeLocation = Operation.Location;
                Socket->RelativeRotation = Operation.Rotation;
                Socket->RelativeScale = Operation.Scale;
                Skeleton->Sockets.Add(Socket);
            }
            else if (Action == TEXT("remove_socket"))
            {
                bOperationOk = Skeleton->Sockets.RemoveAll([&](const TObjectPtr<USkeletalMeshSocket>& Socket)
                {
                    return Socket && Socket->SocketName == FName(Operation.Name);
                }) > 0;
            }
            if (!bOperationOk)
            {
                return Fail(TEXT("animation_operation_failed"), Action);
            }
            ++Result.AppliedOperationCount;
        }
        if (AnimBlueprint && bBlueprintStructureChanged)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
            FCompilerResultsLog CompilerLog;
            CompilerLog.bSilentMode = true;
            FKismetEditorUtilities::CompileBlueprint(
                AnimBlueprint, EBlueprintCompileOptions::None, &CompilerLog);
            Result.Diagnostics.Add(FString::Printf(
                TEXT("compile_errors=%d compile_warnings=%d"),
                CompilerLog.NumErrors,
                CompilerLog.NumWarnings));
            if (CompilerLog.NumErrors > 0 || AnimBlueprint->Status == BS_Error)
            {
                return Fail(TEXT("anim_blueprint_compile_failed"),
                    TEXT("Anim Blueprint compile failed; package reload rollback was attempted."));
            }
        }
        if (bControlRigGraphChanged && ControlRigEditorAsset)
        {
            ControlRigEditorAsset->RecompileVM();
            Result.Diagnostics.Add(TEXT("control_rig_vm_recompiled=true"));
        }
        if (Montage)
        {
#if WITH_EDITOR
            Montage->UpdateLinkableElements();
#endif
            Montage->CalculateSequenceLength();
            Montage->RefreshCacheData();
        }
        Asset->PostEditChange();
        Package->MarkPackageDirty();
        if (bSave && !SaveAsset(Asset))
        {
            return Fail(TEXT("save_failed_rolled_back"), Plan.Request.AssetPath);
        }
        Result.bSaved = bSave;
        Result.bOk = true;
        Result.RevisionAfter = Revision(Asset);
        return Result;
    }

    virtual FThomasDomainAssetCreateApplyResult ApplyAnimationAssetCreate(
        const FString& PlanId,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedAnimationCreatePlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (FindAsset(Plan.Request.AssetPath))
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(TEXT("revision_conflict"), TEXT("Asset now exists."));
        }
        UObject* Context = FindAsset(Plan.Request.ContextAssetPath);
        UFactory* Factory = CreateFactory(
            Plan.FactoryClassPath, Context, Plan.Request.AssetKind);
        if (!Factory)
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("animation_factory_unavailable"), Plan.FactoryClassPath);
        }
        UPackage* Package = CreatePackage(*Plan.Request.AssetPath);
        UObject* Asset = Factory->FactoryCreateNew(Factory->SupportedClass, Package,
            FName(*FPackageName::GetLongPackageAssetName(Plan.Request.AssetPath)),
            RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
        if (!Asset)
        {
            return MakeError<FThomasDomainAssetCreateApplyResult>(TEXT("create_failed"), Plan.Request.AssetPath);
        }
        const FString ExpectedCreatedClass =
            Plan.Request.AssetKind == TEXT("ik_rig")
                ? TEXT("/Script/IKRig.IKRigDefinition")
                : (Plan.Request.AssetKind == TEXT("ik_retargeter")
                    ? TEXT("/Script/IKRig.IKRetargeter")
                    : (Plan.Request.AssetKind == TEXT("control_rig")
                        ? TEXT("/Script/ControlRig.ControlRigRuntimeAsset")
                        : FString()));
        if (!ExpectedCreatedClass.IsEmpty()
            && Asset->GetClass()->GetPathName() != ExpectedCreatedClass)
        {
            ObjectTools::DeleteObjectsUnchecked({Asset});
            return MakeError<FThomasDomainAssetCreateApplyResult>(
                TEXT("unexpected_created_class"),
                Asset->GetClass()->GetPathName());
        }
        FAssetRegistryModule::AssetCreated(Asset);
        if (Plan.Request.AssetKind == TEXT("ik_rig")
            && !SetIKRigPreviewSkeletalMesh(
                Asset, Cast<USkeletalMesh>(Context)))
        {
            FThomasDomainAssetCreateApplyResult Failed;
            Failed.AssetPath = Plan.Request.AssetPath;
            Failed.ClassPath = Asset->GetClass()->GetPathName();
            Failed.Code = TEXT("ik_rig_preview_mesh_failed");
            Failed.bRolledBack =
                ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            return Failed;
        }
        if (Plan.Request.AssetKind == TEXT("ik_retargeter")
            && !SetIKRetargeterSourceRig(
                Asset, Cast<UIKRigDefinition>(Context)))
        {
            FThomasDomainAssetCreateApplyResult Failed;
            Failed.AssetPath = Plan.Request.AssetPath;
            Failed.ClassPath = Asset->GetClass()->GetPathName();
            Failed.Code = TEXT("ik_retargeter_source_rig_failed");
            Failed.bRolledBack =
                ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            return Failed;
        }
        if (Plan.Request.AssetKind == TEXT("control_rig")
            && !InitializeControlRigFromSkeletalMesh(
                Asset, Cast<USkeletalMesh>(Context)))
        {
            FThomasDomainAssetCreateApplyResult Failed;
            Failed.AssetPath = Plan.Request.AssetPath;
            Failed.ClassPath = Asset->GetClass()->GetPathName();
            Failed.Code = TEXT("control_rig_skeletal_mesh_import_failed");
            Failed.bRolledBack =
                ObjectTools::DeleteObjectsUnchecked({Asset}) == 1;
            return Failed;
        }
        Asset->PostEditChange();
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

    TMap<FString, FCachedAnimationCreatePlan> Plans;
    TMap<FString, FCachedAnimationPatchPlan> PatchPlans;
};

IMPLEMENT_MODULE(FThomasEditorAnimationModule, ThomasEditorAnimation)
