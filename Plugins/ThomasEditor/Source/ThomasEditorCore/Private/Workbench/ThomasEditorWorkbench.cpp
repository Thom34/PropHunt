#include "Workbench/ThomasEditorWorkbench.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "ThomasEditorCoreModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ThomasEditorWorkbench"

namespace
{
const FName WorkbenchTabName(TEXT("ThomasEditorWorkbench"));
FDelegateHandle MenuStartupHandle;

FText BuildStatusText()
{
    const FThomasCoreStatus Status = IThomasEditorCoreModule::Get().GetStatus();
    FString Text = FString::Printf(
        TEXT("Project: %s\nEngine: %s\nThomasEditor: %s\nMCP: http://127.0.0.1:8000/mcp\nTool search: %s\n\nProviders (%d loaded):\n"),
        *Status.ProjectName,
        *Status.EngineVersion,
        *Status.ArchitectureVersion,
        Status.bToolSearchRequired ? TEXT("required / compact 3-meta-tool surface") : TEXT("disabled"),
        Status.LoadedProviderCount);

    for (const FThomasProviderStatus& Provider : Status.Providers)
    {
        Text += FString::Printf(
            TEXT("  %-18s  available=%s  loaded=%s  module=%s\n"),
            *Provider.Name,
            Provider.bAvailable ? TEXT("yes") : TEXT("no"),
            Provider.bLoaded ? TEXT("yes") : TEXT("no"),
            *Provider.ModuleName);
    }

    Text += TEXT("\nCurrent Editor context:\n");
    if (GEditor)
    {
        if (const UWorld* World = GEditor->GetEditorWorldContext().World())
        {
            Text += FString::Printf(TEXT("  Level: %s\n"), *World->GetOutermost()->GetName());
            Text += FString::Printf(
                TEXT("  Dirty: %s\n"),
                World->GetOutermost()->IsDirty() ? TEXT("yes") : TEXT("no"));
        }
        Text += FString::Printf(
            TEXT("  PIE: %s\n"), GEditor->PlayWorld ? TEXT("running") : TEXT("stopped"));
        Text += FString::Printf(
            TEXT("  Selected actors: %d\n"),
            GEditor->GetSelectedActorCount());
    }
    return FText::FromString(Text);
}

TSharedRef<SDockTab> SpawnWorkbench(const FSpawnTabArgs& Args)
{
    TSharedRef<STextBlock> StatusBlock = SNew(STextBlock)
        .Text(BuildStatusText())
        .Font(FAppStyle::GetFontStyle(TEXT("MonoFont")));

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SBorder)
            .Padding(16.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Title", "Thomas Workbench"))
                    .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraLarge")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "Subtitle",
                        "Native UE 5.8 MCP, lazy providers and the same guarded C++ services used by Codex."))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Refresh", "Refresh status"))
                    .OnClicked_Lambda([StatusBlock]()
                    {
                        StatusBlock->SetText(BuildStatusText());
                        return FReply::Handled();
                    })
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        StatusBlock
                    ]
                ]
            ]
        ];
}

void OpenWorkbench()
{
    FGlobalTabmanager::Get()->TryInvokeTab(WorkbenchTabName);
}

void RegisterMenus()
{
    FToolMenuOwnerScoped Owner(TEXT("ThomasEditorWorkbench"));
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ThomasEditor"));
    Section.AddMenuEntry(
        TEXT("OpenThomasWorkbench"),
        LOCTEXT("MenuLabel", "Thomas Workbench"),
        LOCTEXT("MenuTooltip", "Open the ThomasEditor MCP/provider status and diagnostics workbench."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Tool")),
        FUIAction(FExecuteAction::CreateStatic(&OpenWorkbench)));
}
}

void FThomasEditorWorkbench::Register()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        WorkbenchTabName,
        FOnSpawnTab::CreateStatic(&SpawnWorkbench))
        .SetDisplayName(LOCTEXT("TabTitle", "Thomas Workbench"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    MenuStartupHandle = UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));
}

void FThomasEditorWorkbench::Unregister()
{
    if (UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::UnRegisterStartupCallback(MenuStartupHandle);
        UToolMenus::UnregisterOwner(TEXT("ThomasEditorWorkbench"));
    }
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorkbenchTabName);
}

#undef LOCTEXT_NAMESPACE
