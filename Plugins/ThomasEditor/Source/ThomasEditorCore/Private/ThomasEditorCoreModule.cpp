#include "ThomasEditorCoreModule.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "IModelContextProtocolModule.h"
#include "Resources/ThomasEditorResourceProvider.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Toolsets/ThomasAssetsToolset.h"
#include "Toolsets/ThomasBlueprintToolset.h"
#include "Toolsets/ThomasCompatibilityToolset.h"
#include "Toolsets/ThomasDiscoveryToolset.h"
#include "Toolsets/ThomasDomainsToolset.h"
#include "Toolsets/ThomasMaterialsToolset.h"
#include "Toolsets/ThomasProjectToolset.h"
#include "Toolsets/ThomasRenderToolset.h"
#include "Toolsets/ThomasWorldToolset.h"
#include "Toolsets/ThomasValidationToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Workbench/ThomasEditorWorkbench.h"

namespace
{
struct FProviderDefinition
{
    const TCHAR* Name;
    const TCHAR* ModuleName;
};

constexpr FProviderDefinition ProviderDefinitions[] = {
    { TEXT("AssetsDetails"), TEXT("ThomasEditorAssets") },
    { TEXT("BlueprintUMG"), TEXT("ThomasEditorBlueprint") },
    { TEXT("Animation"), TEXT("ThomasEditorAnimation") },
    { TEXT("Cinematics"), TEXT("ThomasEditorCinematics") },
    { TEXT("Paper2D"), TEXT("ThomasEditorPaper2D") },
    { TEXT("GameplayData"), TEXT("ThomasEditorGameplayData") },
    { TEXT("DataAI"), TEXT("ThomasEditorDataAI") },
    { TEXT("World"), TEXT("ThomasEditorWorld") },
    { TEXT("Render"), TEXT("ThomasEditorRender") },
    { TEXT("Materials"), TEXT("ThomasEditorMaterials") },
    { TEXT("FX"), TEXT("ThomasEditorFX") },
    { TEXT("Audio"), TEXT("ThomasEditorAudio") },
    { TEXT("PCG"), TEXT("ThomasEditorPCG") },
    { TEXT("Validation"), TEXT("ThomasEditorValidation") }
};

FString CompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Output;
    const auto Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object, Writer);
    return Output;
}
}

class FThomasEditorCoreModule final : public IThomasEditorCoreModule, public FOutputDevice
{
public:
    virtual void StartupModule() override
    {
        bAssetsProviderLoadedAtStartup =
            FModuleManager::Get().IsModuleLoaded(TEXT("ThomasEditorAssets"));

        UToolsetRegistry::RegisterToolsetClass(UThomasDiscoveryToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasAssetsToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasBlueprintToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasCompatibilityToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasProjectToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasWorldToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasRenderToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasMaterialsToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasDomainsToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UThomasValidationToolset::StaticClass());

        ResourceProvider = MakeShared<FThomasEditorResourceProvider>();
        IModelContextProtocolModule::GetChecked().AddResourceProvider(
            ResourceProvider.ToSharedRef());

        if (GLog)
        {
            GLog->AddOutputDevice(this);
        }
        FThomasEditorWorkbench::Register();
    }

    virtual void ShutdownModule() override
    {
        FThomasEditorWorkbench::Unregister();
        if (GLog)
        {
            GLog->RemoveOutputDevice(this);
        }

        if (ResourceProvider.IsValid())
        {
            if (IModelContextProtocolModule* McpModule = IModelContextProtocolModule::Get())
            {
                McpModule->RemoveResourceProvider(ResourceProvider.ToSharedRef());
            }
            ResourceProvider.Reset();
        }

        if (UObjectInitialized() && UToolsetRegistry::IsAvailable())
        {
            UToolsetRegistry::UnregisterToolsetClass(UThomasValidationToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasDomainsToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasMaterialsToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasRenderToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasWorldToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasProjectToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasCompatibilityToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasBlueprintToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasAssetsToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UThomasDiscoveryToolset::StaticClass());
        }
    }

    virtual FThomasCoreStatus GetStatus() const override
    {
        FThomasCoreStatus Result;
        Result.ProjectName = FApp::GetProjectName();
        Result.EngineVersion = FEngineVersion::Current().ToString();
        Result.bAssetsProviderLoadedAtCoreStartup = bAssetsProviderLoadedAtStartup;

        for (const FProviderDefinition& Definition : ProviderDefinitions)
        {
            FThomasProviderStatus Provider;
            Provider.Name = Definition.Name;
            Provider.ModuleName = Definition.ModuleName;
            Provider.bAvailable = FModuleManager::Get().ModuleExists(Definition.ModuleName);
            Provider.bLoaded = FModuleManager::Get().IsModuleLoaded(Definition.ModuleName);
            Result.LoadedProviderCount += Provider.bLoaded ? 1 : 0;
            Result.Providers.Add(MoveTemp(Provider));
        }
        return Result;
    }

    virtual void Serialize(
        const TCHAR* Message,
        ELogVerbosity::Type Verbosity,
        const FName& Category) override
    {
        if (!Message || Verbosity > ELogVerbosity::Warning)
        {
            return;
        }

        FBufferedMessage Entry;
        Entry.Level = Verbosity <= ELogVerbosity::Error ? TEXT("error") : TEXT("warning");
        Entry.Category = Category.ToString();
        Entry.Text = FString(Message).Left(1200);

        FScopeLock Lock(&MessageMutex);
        Messages.Add(MoveTemp(Entry));
        if (Messages.Num() > 200)
        {
            Messages.RemoveAt(0, Messages.Num() - 200, EAllowShrinking::No);
        }
    }

    virtual FString GetRecentMessagesJson(
        const int32 Limit,
        const FString& Filter) const override
    {
        TArray<TSharedPtr<FJsonValue>> Items;
        const int32 SafeLimit = FMath::Clamp(Limit, 1, 50);

        FScopeLock Lock(&MessageMutex);
        for (int32 Index = Messages.Num() - 1;
             Index >= 0 && Items.Num() < SafeLimit;
             --Index)
        {
            const FBufferedMessage& Entry = Messages[Index];
            if (!Filter.IsEmpty()
                && !Entry.Category.Contains(Filter, ESearchCase::IgnoreCase)
                && !Entry.Text.Contains(Filter, ESearchCase::IgnoreCase))
            {
                continue;
            }

            TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("level"), Entry.Level);
            Item->SetStringField(TEXT("category"), Entry.Category);
            Item->SetStringField(TEXT("message"), Entry.Text);
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        Algo::Reverse(Items);

        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetNumberField(TEXT("count"), Items.Num());
        Result->SetArrayField(TEXT("items"), Items);
        return CompactJson(Result);
    }

private:
    struct FBufferedMessage
    {
        FString Level;
        FString Category;
        FString Text;
    };

    bool bAssetsProviderLoadedAtStartup = false;
    mutable FCriticalSection MessageMutex;
    TArray<FBufferedMessage> Messages;
    TSharedPtr<IModelContextProtocolResourceProvider> ResourceProvider;
};

IMPLEMENT_MODULE(FThomasEditorCoreModule, ThomasEditorCore)
