// Copyright ViewGen. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ViewGen : ModuleRules
{
	public ViewGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// MovieRenderPipelineEditor places headers in a Graph/ subdirectory
		// that isn't automatically on the include path. Resolve relative to
		// the engine directory that UBT provides.
		string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		string MovieRenderEditorPublic = Path.Combine(
			EngineDir, "Plugins", "MovieScene", "MovieRenderPipeline",
			"Source", "MovieRenderPipelineEditor", "Public");
		PrivateIncludePaths.Add(MovieRenderEditorPublic);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UMG",
			"RenderCore",
			"RHI",
			"Renderer",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorStyle",
			"EditorFramework",
			"ToolMenus",
			"LevelEditor",
			"Projects",
			"HTTP",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"DesktopPlatform",
			"ApplicationCore",
			"PropertyEditor",
			"EditorWidgets",
			"DeveloperSettings",
			"Landscape",
			"AssetTools",
			"AssetRegistry",
			"InterchangeCore",
			"InterchangeEngine",
			"WebSockets",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieRenderPipelineCore",
			"MovieRenderPipelineEditor",
		});
	}
}
