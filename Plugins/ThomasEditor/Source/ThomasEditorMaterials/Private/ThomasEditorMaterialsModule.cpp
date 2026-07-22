#include "ThomasEditorMaterialsProvider.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxMaterialExpressions = 500;
constexpr int32 MaxMaterialOperations = 100;
constexpr int32 MaxMaterialConnections = 2000;
constexpr int32 MaxMaterialDiagnostics = 500;
constexpr double PlanLifetimeSeconds = 300.0;

struct FCachedMaterialPlan
{
    FThomasMaterialPatchRequest Request;
    double CreatedAtSeconds = 0.0;
};

template <typename TResult>
TResult MakeError(const FString& Code, const FString& Message)
{
    TResult Result;
    Result.bOk = false;
    Result.Code = Code;
    Result.Message = Message.Left(1200);
    return Result;
}

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

UMaterial* LoadMaterial(const FString& AssetPath)
{
    const FString PackageName = NormalizeAssetPath(AssetPath);
    if (!IsAllowedPackagePath(PackageName))
    {
        return nullptr;
    }
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(
        FSoftObjectPath(ObjectPathForPackage(PackageName)));
    return Asset.IsValid() ? Cast<UMaterial>(Asset.GetAsset()) : nullptr;
}

FString BuildRevision(const UMaterial* Material)
{
    if (!Material)
    {
        return TEXT("missing");
    }
    const UPackage* Package = Material->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    return FString::Printf(
        TEXT("%s:%lld:%lld:%d:%d"),
        *Package->GetName(),
        IFileManager::Get().FileSize(*Filename),
        IFileManager::Get().GetTimeStamp(*Filename).ToUnixTimestamp(),
        Package->IsDirty() ? 1 : 0,
        UMaterialEditingLibrary::GetNumMaterialExpressions(Material));
}

FString GetExpressionGuid(UMaterialExpression* Expression)
{
    return Expression
        ? Expression->GetMaterialExpressionId().ToString(EGuidFormats::DigitsWithHyphensLower)
        : FString();
}

FString GetExpressionOutputName(UMaterialExpression* Expression, const int32 OutputIndex)
{
    if (!Expression)
    {
        return FString();
    }
    const TArray<FString> Names = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
    return Names.IsValidIndex(OutputIndex) && !Names[OutputIndex].IsEmpty()
        ? Names[OutputIndex]
        : FString::Printf(TEXT("Output%d"), FMath::Max(0, OutputIndex));
}

bool IsRerouteDeclaration(const UMaterialExpression* Expression)
{
    return Expression
        && Expression->GetClass()->GetName().Contains(TEXT("NamedRerouteDeclaration"));
}

UMaterialExpression* FindExpression(UMaterial* Material, const FString& GuidText)
{
    FGuid Guid;
    if (!Material || !FGuid::Parse(GuidText, Guid))
    {
        return nullptr;
    }
    for (UMaterialExpression* Expression : UMaterialEditingLibrary::GetMaterialExpressions(Material))
    {
        if (Expression && Expression->GetMaterialExpressionId() == Guid)
        {
            return Expression;
        }
    }
    return nullptr;
}

UMaterialExpression* ResolveExpression(
    UMaterial* Material,
    const FString& Handle,
    const FString& Guid,
    const TMap<FString, UMaterialExpression*>& Handles)
{
    if (!Handle.IsEmpty())
    {
        UMaterialExpression* const* Expression = Handles.Find(Handle);
        return Expression ? *Expression : nullptr;
    }
    return FindExpression(Material, Guid);
}

bool IsSupportedExpressionProperty(const FProperty* Property)
{
    return Property
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst)
        && (Property->IsA<FBoolProperty>()
            || Property->IsA<FNumericProperty>()
            || Property->IsA<FEnumProperty>()
            || Property->IsA<FNameProperty>()
            || Property->IsA<FStrProperty>()
            || Property->IsA<FTextProperty>()
            || Property->IsA<FObjectPropertyBase>()
            || Property->IsA<FSoftObjectProperty>());
}

FString ExportProperty(const UObject* Object, const FProperty* Property)
{
    FString Value;
    if (Object && Property)
    {
        Property->ExportText_InContainer(
            0, Value, Object, Object, const_cast<UObject*>(Object), PPF_None);
    }
    return Value;
}

TOptional<EMaterialProperty> ParseMaterialProperty(const FString& Input)
{
    static const TMap<FString, EMaterialProperty> Values = {
        {TEXT("basecolor"), MP_BaseColor},
        {TEXT("metallic"), MP_Metallic},
        {TEXT("specular"), MP_Specular},
        {TEXT("roughness"), MP_Roughness},
        {TEXT("anisotropy"), MP_Anisotropy},
        {TEXT("emissivecolor"), MP_EmissiveColor},
        {TEXT("opacity"), MP_Opacity},
        {TEXT("opacitymask"), MP_OpacityMask},
        {TEXT("normal"), MP_Normal},
        {TEXT("tangent"), MP_Tangent},
        {TEXT("worldpositionoffset"), MP_WorldPositionOffset},
        {TEXT("subsurfacecolor"), MP_SubsurfaceColor},
        {TEXT("ambientocclusion"), MP_AmbientOcclusion},
        {TEXT("refraction"), MP_Refraction},
        {TEXT("pixeldepthoffset"), MP_PixelDepthOffset},
        {TEXT("shadingmodel"), MP_ShadingModel},
        {TEXT("surfacethickness"), MP_SurfaceThickness}
    };
    const EMaterialProperty* Value = Values.Find(Input.Replace(TEXT("_"), TEXT("")).ToLower());
    return Value ? TOptional<EMaterialProperty>(*Value) : TOptional<EMaterialProperty>();
}

UMaterial* CreateMaterial(const FString& PackageName)
{
    UPackage* Package = CreatePackage(*PackageName);
    UMaterial* Material = NewObject<UMaterial>(
        Package,
        FName(*FPackageName::GetLongPackageAssetName(PackageName)),
        RF_Public | RF_Standalone | RF_Transactional);
    if (Material)
    {
        FAssetRegistryModule::AssetCreated(Material);
        Material->MarkPackageDirty();
    }
    return Material;
}

bool SaveMaterial(UMaterial* Material)
{
    UPackage* Package = Material ? Material->GetOutermost() : nullptr;
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
    return UPackage::SavePackage(Package, Material, *Filename, Args);
}
}

class FThomasEditorMaterialsModule final : public IThomasEditorMaterialsProviderModule
{
public:
    virtual void ShutdownModule() override { Plans.Reset(); }

    virtual FThomasMaterialInspectionResult InspectMaterial(
        const FString& AssetPath,
        const int32 MaxExpressions) override
    {
        const FString PackageName = NormalizeAssetPath(AssetPath);
        if (!IsAllowedPackagePath(PackageName))
        {
            return MakeError<FThomasMaterialInspectionResult>(
                TEXT("path_denied"), TEXT("Material must remain under /Game/PropHunt."));
        }
        if (MaxExpressions < 1 || MaxExpressions > MaxMaterialExpressions)
        {
            return MakeError<FThomasMaterialInspectionResult>(
                TEXT("invalid_limit"), TEXT("MaxExpressions must be between 1 and 500."));
        }
        UMaterial* Material = LoadMaterial(PackageName);
        if (!Material)
        {
            return MakeError<FThomasMaterialInspectionResult>(
                TEXT("material_not_found"), PackageName);
        }

        FThomasMaterialInspectionResult Result;
        Result.bOk = true;
        Result.AssetPath = PackageName;
        Result.Revision = BuildRevision(Material);
        Result.MaterialDomain = StaticEnum<EMaterialDomain>()->GetNameStringByValue(Material->MaterialDomain);
        Result.BlendMode = StaticEnum<EBlendMode>()->GetNameStringByValue(Material->BlendMode);
        Result.ShadingModel = StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(
            Material->GetShadingModels().GetFirstShadingModel());
        Result.bDirty = Material->GetOutermost()->IsDirty();
        Result.bTwoSided = Material->TwoSided;
        const TArray<UMaterialExpression*> Expressions =
            UMaterialEditingLibrary::GetMaterialExpressions(Material);
        Result.ExpressionCount = Expressions.Num();
        TArray<UMaterialExpression*> AnalyzedExpressions;
        for (UMaterialExpression* Expression : Expressions)
        {
            if (!Expression)
            {
                continue;
            }
            if (Result.Expressions.Num() >= MaxExpressions)
            {
                Result.bTruncated = true;
                break;
            }
            FThomasMaterialExpressionRecord Record;
            Record.ExpressionGuid = Expression->GetMaterialExpressionId().ToString(
                EGuidFormats::DigitsWithHyphensLower);
            Record.ObjectName = Expression->GetName();
            Record.ClassPath = Expression->GetClass()->GetPathName();
            Record.Description = Expression->Desc.Left(500);
            Record.PositionX = Expression->MaterialExpressionEditorX;
            Record.PositionY = Expression->MaterialExpressionEditorY;
            Record.Inputs = UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression);
            Record.Outputs = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
            Result.Expressions.Add(MoveTemp(Record));
            AnalyzedExpressions.Add(Expression);
        }

        const TSet<UMaterialExpression*> AllExpressionSet(Expressions);
        const TSet<UMaterialExpression*> AnalyzedExpressionSet(AnalyzedExpressions);
        TMap<UMaterialExpression*, TArray<UMaterialExpression*>> InputsByExpression;
        TMap<UMaterialExpression*, int32> DownstreamUseCount;
        TSet<UMaterialExpression*> MaterialRoots;
        TMap<FString, TArray<UMaterialExpression*>> ExpressionsByPosition;

        auto AddDiagnostic = [&Result](
            const FString& Severity,
            const FString& Code,
            UMaterialExpression* Expression,
            const FString& Message)
        {
            if (Result.Diagnostics.Num() >= MaxMaterialDiagnostics)
            {
                Result.bTruncated = true;
                return;
            }
            FThomasMaterialGraphDiagnostic Diagnostic;
            Diagnostic.Severity = Severity;
            Diagnostic.Code = Code;
            Diagnostic.ExpressionGuid = GetExpressionGuid(Expression);
            Diagnostic.ObjectName = Expression ? Expression->GetName() : FString();
            Diagnostic.Message = Message.Left(800);
            Result.Diagnostics.Add(MoveTemp(Diagnostic));
        };

        auto AddConnection = [&Result](FThomasMaterialConnectionRecord&& Connection)
        {
            ++Result.ConnectionCount;
            if (Result.Connections.Num() < MaxMaterialConnections)
            {
                Result.Connections.Add(MoveTemp(Connection));
            }
            else
            {
                Result.bTruncated = true;
            }
        };

        for (UMaterialExpression* Expression : AnalyzedExpressions)
        {
            ExpressionsByPosition.FindOrAdd(FString::Printf(
                TEXT("%d:%d"),
                Expression->MaterialExpressionEditorX,
                Expression->MaterialExpressionEditorY)).Add(Expression);

            const TArray<FString> InputNames =
                UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression);
            for (int32 InputIndex = 0; InputIndex < InputNames.Num(); ++InputIndex)
            {
                const FExpressionInput* Input = Expression->GetInput(InputIndex);
                UMaterialExpression* Source = Input ? Input->Expression : nullptr;
                if (!Source)
                {
                    continue;
                }
                if (!AllExpressionSet.Contains(Source))
                {
                    AddDiagnostic(
                        TEXT("error"),
                        TEXT("dangling_connection"),
                        Expression,
                        FString::Printf(
                            TEXT("Input %s references an expression outside this material."),
                            *InputNames[InputIndex]));
                    continue;
                }
                InputsByExpression.FindOrAdd(Expression).AddUnique(Source);
                DownstreamUseCount.FindOrAdd(Source)++;
                if (!AnalyzedExpressionSet.Contains(Source))
                {
                    Result.bTruncated = true;
                    continue;
                }
                FThomasMaterialConnectionRecord Connection;
                Connection.FromExpressionGuid = GetExpressionGuid(Source);
                Connection.FromObjectName = Source->GetName();
                Connection.FromOutput = GetExpressionOutputName(Source, Input->OutputIndex);
                Connection.TargetKind = TEXT("expression");
                Connection.ToExpressionGuid = GetExpressionGuid(Expression);
                Connection.ToObjectName = Expression->GetName();
                Connection.ToInput = InputNames[InputIndex].IsEmpty()
                    ? FString::Printf(TEXT("Input%d"), InputIndex)
                    : InputNames[InputIndex];
                AddConnection(MoveTemp(Connection));
            }
        }

        for (int32 PropertyIndex = 0; PropertyIndex < static_cast<int32>(MP_MAX); ++PropertyIndex)
        {
            const EMaterialProperty Property = static_cast<EMaterialProperty>(PropertyIndex);
            const FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
            UMaterialExpression* Source = Input ? Input->Expression : nullptr;
            if (!Source)
            {
                continue;
            }
            ++Result.MaterialRootConnectionCount;
            MaterialRoots.Add(Source);
            DownstreamUseCount.FindOrAdd(Source)++;
            if (!AllExpressionSet.Contains(Source))
            {
                AddDiagnostic(
                    TEXT("error"),
                    TEXT("dangling_connection"),
                    Source,
                    TEXT("A material output references an expression outside this material."));
                continue;
            }
            if (!AnalyzedExpressionSet.Contains(Source))
            {
                Result.bTruncated = true;
                continue;
            }
            FThomasMaterialConnectionRecord Connection;
            Connection.FromExpressionGuid = GetExpressionGuid(Source);
            Connection.FromObjectName = Source->GetName();
            Connection.FromOutput = GetExpressionOutputName(Source, Input->OutputIndex);
            Connection.TargetKind = TEXT("material");
            Connection.MaterialProperty = StaticEnum<EMaterialProperty>()->GetNameStringByValue(Property);
            AddConnection(MoveTemp(Connection));
        }

        TSet<UMaterialExpression*> ReachableExpressions;
        TArray<UMaterialExpression*> Pending = MaterialRoots.Array();
        while (!Pending.IsEmpty())
        {
            UMaterialExpression* Expression = Pending.Pop(EAllowShrinking::No);
            if (!Expression || ReachableExpressions.Contains(Expression))
            {
                continue;
            }
            ReachableExpressions.Add(Expression);
            if (const TArray<UMaterialExpression*>* Inputs = InputsByExpression.Find(Expression))
            {
                Pending.Append(*Inputs);
            }
        }

        for (UMaterialExpression* Expression : AnalyzedExpressions)
        {
            const bool bReachable = ReachableExpressions.Contains(Expression);
            if (bReachable)
            {
                ++Result.ReachableExpressionCount;
                continue;
            }
            ++Result.UnreachableExpressionCount;
            const bool bOrphan = DownstreamUseCount.FindRef(Expression) == 0
                && !IsRerouteDeclaration(Expression);
            if (bOrphan)
            {
                ++Result.OrphanExpressionCount;
                AddDiagnostic(
                    TEXT("warning"),
                    TEXT("orphan_expression"),
                    Expression,
                    TEXT("Expression has no downstream consumer and does not feed a material output."));
            }
            else
            {
                AddDiagnostic(
                    TEXT("info"),
                    TEXT("unreachable_expression"),
                    Expression,
                    TEXT("Expression belongs to a subgraph that does not reach any material output."));
            }
        }

        for (const TPair<FString, TArray<UMaterialExpression*>>& Pair : ExpressionsByPosition)
        {
            if (Pair.Value.Num() < 2)
            {
                continue;
            }
            ++Result.OverlapGroupCount;
            TArray<FString> Names;
            for (UMaterialExpression* Expression : Pair.Value)
            {
                Names.Add(Expression->GetName());
            }
            AddDiagnostic(
                TEXT("warning"),
                TEXT("overlapping_nodes"),
                Pair.Value[0],
                FString::Printf(
                    TEXT("%d expressions share editor position %s: %s"),
                    Pair.Value.Num(),
                    *Pair.Key,
                    *FString::Join(Names, TEXT(", "))));
        }

        if (Result.MaterialRootConnectionCount == 0)
        {
            AddDiagnostic(
                TEXT("error"),
                TEXT("material_has_no_output"),
                nullptr,
                TEXT("No expression is connected to a material output."));
        }
        return Result;
    }

    virtual FThomasMaterialPlanResult PlanMaterialPatch(
        const FThomasMaterialPatchRequest& InputRequest) override
    {
        PurgeExpiredPlans();
        FThomasMaterialPatchRequest Request = InputRequest;
        Request.AssetPath = NormalizeAssetPath(Request.AssetPath);
        if (!IsAllowedPackagePath(Request.AssetPath))
        {
            return MakeError<FThomasMaterialPlanResult>(
                TEXT("path_denied"), TEXT("Material must remain under /Game/PropHunt."));
        }
        if (Request.Operations.Num() < 1 || Request.Operations.Num() > MaxMaterialOperations)
        {
            return MakeError<FThomasMaterialPlanResult>(
                TEXT("invalid_batch_size"), TEXT("Material batch must contain 1 to 100 operations."));
        }
        UMaterial* Existing = LoadMaterial(Request.AssetPath);
        const FString Revision = BuildRevision(Existing);
        if (Existing)
        {
            if (Request.bCreateIfMissing)
            {
                return MakeError<FThomasMaterialPlanResult>(TEXT("asset_exists"), Request.AssetPath);
            }
            if (Existing->GetOutermost()->IsDirty())
            {
                return MakeError<FThomasMaterialPlanResult>(
                    TEXT("asset_dirty"), TEXT("Save or revert the material before planning mutations."));
            }
        }
        else if (!Request.bCreateIfMissing)
        {
            return MakeError<FThomasMaterialPlanResult>(TEXT("material_not_found"), Request.AssetPath);
        }
        if (Request.ExpectedRevision.IsEmpty() || Request.ExpectedRevision != Revision)
        {
            return MakeError<FThomasMaterialPlanResult>(TEXT("revision_conflict"), Revision);
        }

        TSet<FString> Handles;
        bool bDestructive = false;
        for (const FThomasMaterialOperation& Operation : Request.Operations)
        {
            const FString Action = Operation.Action.ToLower();
            if (Action == TEXT("create_expression"))
            {
                UClass* ExpressionClass = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                if (Operation.Handle.IsEmpty() || Handles.Contains(Operation.Handle)
                    || !ExpressionClass
                    || !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())
                    || ExpressionClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
                {
                    return MakeError<FThomasMaterialPlanResult>(
                        TEXT("invalid_expression_create"), Operation.Handle);
                }
                Handles.Add(Operation.Handle);
            }
            else if (Action == TEXT("set_expression_property")
                || Action == TEXT("delete_expression")
                || Action == TEXT("connect_material_property")
                || Action == TEXT("disconnect_expression"))
            {
                const bool bKnownHandle = !Operation.Handle.IsEmpty()
                    && Handles.Contains(Operation.Handle);
                UMaterialExpression* ExistingExpression = Existing
                    ? FindExpression(Existing, Operation.ExpressionGuid)
                    : nullptr;
                if (!bKnownHandle && !ExistingExpression)
                {
                    return MakeError<FThomasMaterialPlanResult>(
                        TEXT("expression_not_found"), Operation.ExpressionGuid);
                }
                if (Action == TEXT("set_expression_property") && ExistingExpression)
                {
                    FProperty* Property = FindFProperty<FProperty>(
                        ExistingExpression->GetClass(), FName(*Operation.PropertyName));
                    const FString CurrentValue = ExportProperty(ExistingExpression, Property);
                    if (!IsSupportedExpressionProperty(Property)
                        || Operation.ExpectedValue.IsEmpty()
                        || Operation.ExpectedValue != CurrentValue)
                    {
                        return MakeError<FThomasMaterialPlanResult>(
                            TEXT("property_conflict_or_unsupported"), CurrentValue);
                    }
                }
                if (Action == TEXT("connect_material_property")
                    && !ParseMaterialProperty(Operation.MaterialProperty).IsSet())
                {
                    return MakeError<FThomasMaterialPlanResult>(
                        TEXT("invalid_material_property"), Operation.MaterialProperty);
                }
                bDestructive |= Action == TEXT("delete_expression");
            }
            else if (Action == TEXT("connect_expressions"))
            {
                const bool bFrom = (!Operation.Handle.IsEmpty() && Handles.Contains(Operation.Handle))
                    || (Existing && FindExpression(Existing, Operation.ExpressionGuid));
                const bool bTo = (!Operation.OtherHandle.IsEmpty() && Handles.Contains(Operation.OtherHandle))
                    || (Existing && FindExpression(Existing, Operation.OtherExpressionGuid));
                if (!bFrom || !bTo)
                {
                    return MakeError<FThomasMaterialPlanResult>(
                        TEXT("expression_endpoint_not_found"), Operation.ToInput);
                }
            }
            else if (Action == TEXT("disconnect_material_property"))
            {
                if (!ParseMaterialProperty(Operation.MaterialProperty).IsSet())
                {
                    return MakeError<FThomasMaterialPlanResult>(
                        TEXT("invalid_material_property"), Operation.MaterialProperty);
                }
            }
            else
            {
                return MakeError<FThomasMaterialPlanResult>(
                    TEXT("unsupported_operation"), Operation.Action);
            }
        }
        if (bDestructive && !Request.bConfirmDestructive)
        {
            return MakeError<FThomasMaterialPlanResult>(
                TEXT("confirmation_required"),
                TEXT("Expression deletion requires bConfirmDestructive=true."));
        }

        FThomasMaterialPlanResult Result;
        Result.bOk = true;
        Result.PlanId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Result.AssetPath = Request.AssetPath;
        Result.ExpectedRevision = Revision;
        Result.OperationCount = Request.Operations.Num();
        Result.Risk = bDestructive ? TEXT("R2") : TEXT("R1");
        Result.Preview.Add(Existing
            ? TEXT("Modify the existing native material graph in one transaction.")
            : TEXT("Create a native material and remove it if validation fails."));
        Result.Preview.Add(TEXT("Compile and save are explicit Apply flags."));
        FCachedMaterialPlan& Plan = Plans.Add(Result.PlanId);
        Plan.Request = MoveTemp(Request);
        Plan.CreatedAtSeconds = FPlatformTime::Seconds();
        return Result;
    }

    virtual FThomasMaterialApplyResult ApplyMaterialPlan(
        const FString& PlanId,
        const bool bCompile,
        const bool bSave) override
    {
        PurgeExpiredPlans();
        FCachedMaterialPlan Plan;
        if (!Plans.RemoveAndCopyValue(PlanId, Plan))
        {
            return MakeError<FThomasMaterialApplyResult>(
                TEXT("plan_not_found"), TEXT("Plan is missing, expired, or already consumed."));
        }
        if (bSave && !bCompile)
        {
            return MakeError<FThomasMaterialApplyResult>(
                TEXT("compile_required"), TEXT("Saving a material requires shader compilation."));
        }
        UMaterial* Material = LoadMaterial(Plan.Request.AssetPath);
        const FString Revision = BuildRevision(Material);
        if (Revision != Plan.Request.ExpectedRevision)
        {
            return MakeError<FThomasMaterialApplyResult>(TEXT("revision_conflict"), Revision);
        }

        FThomasMaterialApplyResult Result;
        Result.AssetPath = Plan.Request.AssetPath;
        Result.RevisionBefore = Revision;
        if (!Material)
        {
            Material = CreateMaterial(Plan.Request.AssetPath);
            if (!Material)
            {
                return MakeError<FThomasMaterialApplyResult>(
                    TEXT("create_failed"), Plan.Request.AssetPath);
            }
            Result.bCreated = true;
        }

        bool bApplied = true;
        FString Error;
        {
            const FScopedTransaction Transaction(
                NSLOCTEXT("ThomasEditor", "ApplyMaterialPatch", "ThomasEditor Material Patch"));
            Material->Modify();
            TMap<FString, UMaterialExpression*> Handles;
            for (const FThomasMaterialOperation& Operation : Plan.Request.Operations)
            {
                const FString Action = Operation.Action.ToLower();
                if (Action == TEXT("create_expression"))
                {
                    UClass* Class = LoadObject<UClass>(nullptr, *Operation.ClassPath);
                    UMaterialExpression* Expression = Class
                        ? UMaterialEditingLibrary::CreateMaterialExpression(
                            Material, Class, Operation.PositionX, Operation.PositionY)
                        : nullptr;
                    if (!Expression || Operation.Handle.IsEmpty() || Handles.Contains(Operation.Handle))
                    {
                        bApplied = false;
                    }
                    else
                    {
                        Handles.Add(Operation.Handle, Expression);
                    }
                }
                else
                {
                    UMaterialExpression* Expression = ResolveExpression(
                        Material, Operation.Handle, Operation.ExpressionGuid, Handles);
                    if (Action == TEXT("set_expression_property"))
                    {
                        FProperty* Property = Expression
                            ? FindFProperty<FProperty>(
                                Expression->GetClass(), FName(*Operation.PropertyName))
                            : nullptr;
                        const FString Current = ExportProperty(Expression, Property);
                        if (!Expression || !IsSupportedExpressionProperty(Property)
                            || (!Operation.ExpectedValue.IsEmpty()
                                && Operation.ExpectedValue != Current))
                        {
                            bApplied = false;
                        }
                        else
                        {
                            Expression->Modify();
                            bApplied = Property->ImportText_InContainer(
                                *Operation.Value, Expression, Expression, PPF_None) != nullptr;
                            if (bApplied)
                            {
                                Expression->PostEditChange();
                            }
                        }
                    }
                    else if (Action == TEXT("connect_expressions"))
                    {
                        UMaterialExpression* Other = ResolveExpression(
                            Material,
                            Operation.OtherHandle,
                            Operation.OtherExpressionGuid,
                            Handles);
                        bApplied = Expression && Other
                            && UMaterialEditingLibrary::ConnectMaterialExpressions(
                                Expression, Operation.FromOutput, Other, Operation.ToInput);
                    }
                    else if (Action == TEXT("connect_material_property"))
                    {
                        const TOptional<EMaterialProperty> Property =
                            ParseMaterialProperty(Operation.MaterialProperty);
                        bApplied = Expression && Property.IsSet()
                            && UMaterialEditingLibrary::ConnectMaterialProperty(
                                Expression, Operation.FromOutput, Property.GetValue());
                    }
                    else if (Action == TEXT("disconnect_expression"))
                    {
                        bApplied = Expression
                            && UMaterialEditingLibrary::DisconnectMaterialExpressions(
                                Expression, Operation.ToInput);
                    }
                    else if (Action == TEXT("disconnect_material_property"))
                    {
                        const TOptional<EMaterialProperty> Property =
                            ParseMaterialProperty(Operation.MaterialProperty);
                        bApplied = Property.IsSet()
                            && UMaterialEditingLibrary::DisconnectMaterialProperty(
                                Material, Property.GetValue());
                    }
                    else if (Action == TEXT("delete_expression"))
                    {
                        if (Expression)
                        {
                            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
                        }
                        else
                        {
                            bApplied = false;
                        }
                    }
                }
                if (!bApplied)
                {
                    Error = Operation.Action + TEXT(" failed.");
                    break;
                }
                ++Result.AppliedOperationCount;
            }
        }
        if (!bApplied)
        {
            Rollback(Material, Result);
            Result.Code = TEXT("apply_failed");
            Result.Message = Error;
            return Result;
        }

        if (bCompile)
        {
            Result.Diagnostics = UMaterialEditingLibrary::RecompileMaterial(Material);
            Result.bCompiled = Result.Diagnostics.IsEmpty();
            if (!Result.bCompiled)
            {
                Rollback(Material, Result);
                Result.Code = TEXT("compile_failed");
                Result.Message = TEXT("Material compiler returned errors; changes were rolled back.");
                return Result;
            }
            UMaterialEditingLibrary::RefreshMaterialEditor(Material);
        }
        if (bSave)
        {
            Result.bSaved = SaveMaterial(Material);
            if (!Result.bSaved)
            {
                Rollback(Material, Result);
                Result.Code = TEXT("save_failed");
                Result.Message = TEXT("Material save failed; rollback was attempted.");
                return Result;
            }
        }
        Result.bOk = true;
        Result.RevisionAfter = BuildRevision(Material);
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

    static void Rollback(UMaterial* Material, FThomasMaterialApplyResult& Result)
    {
        if (Result.bCreated)
        {
            Result.bRolledBack =
                Material && ObjectTools::DeleteObjectsUnchecked({Material}) == 1;
            Result.RevisionAfter = TEXT("missing");
        }
        else
        {
            Result.bRolledBack = GEditor && GEditor->UndoTransaction();
            if (Material)
            {
                UMaterialEditingLibrary::RecompileMaterial(Material);
                Result.RevisionAfter = BuildRevision(Material);
            }
        }
    }

    TMap<FString, FCachedMaterialPlan> Plans;
};

IMPLEMENT_MODULE(FThomasEditorMaterialsModule, ThomasEditorMaterials)
