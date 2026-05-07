// Copyright ViewGen. All Rights Reserved.

#include "ViewGen.h"
#include "ViewGenStyle.h"
#include "ViewGenCommands.h"
#include "SViewGenPanel.h"

#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
// No cross-plugin dependencies — each plugin registers itself in StoryTools independently

#define LOCTEXT_NAMESPACE "FViewGenModule"

const FName FViewGenModule::ViewGenTabName(TEXT("ViewGen"));

void FViewGenModule::StartupModule()
{
	// Initialize style and commands
	FViewGenStyle::Initialize();
	FViewGenCommands::Register();

	// Map the "Open Window" command to actually open our tab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ViewGenTabName,
		FOnSpawnTab::CreateRaw(this, &FViewGenModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "ViewGen"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FViewGenStyle::GetStyleSetName(), "ViewGen.OpenPluginWindow"));

	RegisterMenuExtensions();
}

void FViewGenModule::ShutdownModule()
{
	UnregisterMenuExtensions();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ViewGenTabName);

	FViewGenCommands::Unregister();
	FViewGenStyle::Shutdown();
}

FViewGenModule& FViewGenModule::Get()
{
	return FModuleManager::LoadModuleChecked<FViewGenModule>("ViewGen");
}

bool FViewGenModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("ViewGen");
}

TSharedRef<SDockTab> FViewGenModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SViewGenPanel)
		];
}

void FViewGenModule::RegisterMenuExtensions()
{
	// Bind commands to actions
	TSharedPtr<FUICommandList> PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FViewGenCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(ViewGenTabName);
		}),
		FCanExecuteAction());

	// Register in the Tool Menus system
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([PluginCommands]()
	{
		// Add toolbar button
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FViewGenCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}

		// (No Window-menu entry — ViewGen lives only under StoryTools.)

		// ================================================================
		// StoryTools — top-level menu bar entry
		// Each plugin finds-or-creates this menu and adds only its own
		// entry, so there are zero cross-plugin dependencies.
		// ================================================================
		UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
		if (MainMenu)
		{
			FToolMenuSection& StorySection = MainMenu->FindOrAddSection(
				"StoryTools",
				LOCTEXT("StoryToolsMenuLabel", "StoryTools"));

			// Lambda: add *our* entry to a section of the StoryTools submenu. Hoisted so the
			// same entry is registered both via the seed lambda below AND via the per-plugin
			// dynamic section, mirroring the pattern SceneBreak / CamTracker / StyleKeeper use.
			auto AddViewGenEntry = [](FToolMenuSection& Entries)
			{
				Entries.AddMenuEntry(
					FName("OpenUEGen"),
					LOCTEXT("OpenUEGen", "ViewGen"),
					LOCTEXT("OpenUEGenTooltip", "AI Viewport Generator — ComfyUI workflow editor with viewport capture, video generation, and 3D asset import"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
					FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("ViewGen")));
					})
				);
			};

			// Always AddSubMenu — UE dedupes by name. If our call wins the race, our seed
			// lambda runs; if another plugin's call wins, the dynamic section below ensures
			// we still appear.
			StorySection.AddSubMenu(
				"StoryToolsSubMenu",
				LOCTEXT("StoryToolsLabel", "StoryTools"),
				LOCTEXT("StoryToolsTooltip", "Story-driven creative toolset"),
				FNewToolMenuDelegate::CreateLambda([AddViewGenEntry](UToolMenu* SubMenu)
				{
					FToolMenuSection& Entries = SubMenu->FindOrAddSection(
						"StoryToolsEntries", LOCTEXT("ToolsSection", "Tools"));
					AddViewGenEntry(Entries);
				})
			);

			// Robust path: extend the registered submenu directly with a per-plugin dynamic
			// section. ExtendMenu queues the extension if the menu isn't registered yet, so
			// load order doesn't matter. Each plugin uses its own section name so we don't
			// clobber each other.
			if (UToolMenu* StorySubMenu = UToolMenus::Get()->ExtendMenu(
				"LevelEditor.MainMenu.StoryToolsSubMenu"))
			{
				StorySubMenu->AddDynamicSection(
					"StoryToolsEntries_ViewGen",
					FNewToolMenuDelegate::CreateLambda([AddViewGenEntry](UToolMenu* InMenu)
					{
						FToolMenuSection& Entries = InMenu->FindOrAddSection(
							"StoryToolsEntries", LOCTEXT("ToolsSection", "Tools"));
						AddViewGenEntry(Entries);
					})
				);
			}
		}
	}));
}

void FViewGenModule::UnregisterMenuExtensions()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FViewGenModule, ViewGen)
