#include "GeoTerrainEditorModule.h"
#include "UI/GeoTerrainPanel.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "FGeoTerrainEditorModule"

static const FName GeoTerrainTabName(TEXT("GeoTerrain"));

void FGeoTerrainEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        GeoTerrainTabName,
        FOnSpawnTab::CreateRaw(this, &FGeoTerrainEditorModule::OnSpawnPluginTab))
        .SetDisplayName(LOCTEXT("GeoTerrainTabTitle", "GeoTerrain"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGeoTerrainEditorModule::RegisterMenus));
}

void FGeoTerrainEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GeoTerrainTabName);
}

TSharedRef<SDockTab> FGeoTerrainEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SGeoTerrainPanel)
        ];
}

void FGeoTerrainEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");

    FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
        "OpenGeoTerrain",
        LOCTEXT("GeoTerrainMenuItem", "GeoTerrain"),
        LOCTEXT("GeoTerrainMenuItemTooltip", "Open the GeoTerrain terrain generator panel"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("GeoTerrain")));
        }))
    );
    Section.AddEntry(Entry);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGeoTerrainEditorModule, GeoTerrainEditor)
