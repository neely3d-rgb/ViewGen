// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "GenAISettings.h"
#include "ViewportCapture.h"
#include "DepthPassRenderer.h"
#include "SegmentationCapture.h"
#include "GenAIHttpClient.h"
#include "MeshyApiClient.h"

class SWorkflowPreviewPanel;
class SWorkflowGraphEditor;
class UStaticMesh;

/**
 * Main Slate panel widget for ViewGen.
 *
 * Layout:
 * ┌────────────────────────────────────────────┐
 * │ [Capture Viewport]   [Generate]   [Cancel] │
 * ├──────────────┬─────────────────────────────┤
 * │  Thumbnails  │       Preview Window        │
 * │ ┌──────────┐ │                             │
 * │ │ Viewport │ │     (Generated result or    │
 * │ │ Capture  │ │      "No image" placeholder)│
 * │ └──────────┘ │                             │
 * │ ┌──────────┐ │                             │
 * │ │  Depth   │ │                             │
 * │ │   Map    │ │                             │
 * │ └──────────┘ │                             │
 * ├──────────────┴─────────────────────────────┤
 * │ Prompt:                                    │
 * │ [Multi-line text box                     ] │
 * │ Negative Prompt:                           │
 * │ [Multi-line text box                     ] │
 * ├────────────────────────────────────────────┤
 * │ LoRA:  [Dropdown ▼] Weight: [0.75] [+Add] │
 * │  ☑ my_lora_1 (0.75)  [Remove]             │
 * │  ☑ my_lora_2 (0.50)  [Remove]             │
 * ├────────────────────────────────────────────┤
 * │ Denoising: [═══●════]  Steps: [30]        │
 * │ Status: Ready / Generating... [████░░] 45% │
 * └────────────────────────────────────────────┘
 */
class SViewGenPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SViewGenPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SViewGenPanel();

	/** Refresh TObjectPtr handles in heap-allocated FSlateBrush objects before Slate painting.
	 *  GC compaction (triggered by save operations) can invalidate packed indices in
	 *  TObjectPtr members of FSlateBrush objects that are not part of a UPROPERTY chain. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// ---- GC Safety ----

	/** Handle for PostGarbageCollect delegate */
	FDelegateHandle PostGCDelegateHandle;

	/** Refresh all FSlateBrush ResourceObject handles from stored raw texture pointers.
	 *  Called immediately after GC to prevent stale TObjectPtr packed indices. */
	void OnPostGarbageCollect();
	// ---- Actions ----
	FReply OnCaptureViewportClicked();
	FReply OnGenerateClicked();
	FReply OnCancelClicked();
	FReply OnAddLoRAClicked();
	FReply OnRemoveLoRA(int32 Index);
	FReply OnOpenSettingsClicked();

	// ---- UI Builders ----
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildThumbnailPanel();
	TSharedRef<SWidget> BuildPreviewPanel();
	TSharedRef<SWidget> BuildResultGalleryPanel();
	TSharedRef<SWidget> BuildPromptPanel();
	TSharedRef<SWidget> BuildLoRAPanel();
	TSharedRef<SWidget> BuildSettingsPanel();
	TSharedRef<SWidget> BuildStatusBar();

	/** Helper: build a labeled row with a label on the left and a widget on the right */
	TSharedRef<SWidget> MakeSettingsRow(const FText& Label, TSharedRef<SWidget> ValueWidget);

	/** Write current UI settings back to UGenAISettings and save config */
	void ApplySettingsToConfig();

	/** Build a single thumbnail card (viewport or depth) */
	TSharedRef<SWidget> BuildThumbnailCard(const FText& Label, TSharedPtr<FSlateBrush>& OutBrush);

	// ---- Callbacks ----
	void OnGenerationComplete(bool bSuccess, UTexture2D* ResultTexture);
	void OnGenerationProgress(float Progress);
	void OnGenerationError(const FString& ErrorMessage);

	/** Update thumbnail brush from a captured texture */
	void UpdateThumbnailBrush(TSharedPtr<FSlateBrush>& Brush, UTexture2D* Texture, TSharedPtr<SImage>& ImageWidget);

	// ---- State ----
	void UpdateStatusText(const FString& Message);

	/** Get active LoRA entries from the UI state */
	TArray<FLoRAEntry> GetActiveLoRAs() const;

	/** Calculate and return a human-readable cost estimate string for the current settings */
	FString EstimateGenerationCost() const;

	/** Calculate and return a human-readable cost estimate for video generation */
	FString EstimateVideoCost() const;

	// ---- Subsystems ----
	TUniquePtr<FViewportCapture> ViewportCapture;
	TUniquePtr<FDepthPassRenderer> DepthRenderer;
	TUniquePtr<FSegmentationCapture> SegmentationCapture;
	TUniquePtr<FGenAIHttpClient> HttpClient;

	// ---- UI State ----
	TSharedPtr<FSlateBrush> ViewportThumbnailBrush;
	TSharedPtr<FSlateBrush> DepthThumbnailBrush;
	TSharedPtr<FSlateBrush> PreviewBrush;

	TSharedPtr<SImage> ViewportThumbnailImage;
	TSharedPtr<SImage> DepthThumbnailImage;
	TSharedPtr<SImage> PreviewImage;

	TSharedPtr<SMultiLineEditableTextBox> PromptTextBox;
	TSharedPtr<SMultiLineEditableTextBox> NegativePromptTextBox;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SVerticalBox> LoRAListBox;

	// ---- Settings UI Widgets ----
	TSharedPtr<SEditableTextBox> ComfyUIURLInput;
	TSharedPtr<SEditableTextBox> ComfyUIApiKeyInput;
	TSharedPtr<SEditableTextBox> MeshyApiKeyInput;
	TSharedPtr<SEditableTextBox> FluxModelNameInput;
	TSharedPtr<SEditableTextBox> GeminiSystemPromptInput;

	// Model combo boxes (dynamically populated from ComfyUI)
	TArray<TSharedPtr<FString>> CheckpointOptions;
	TArray<TSharedPtr<FString>> LoRAModelOptions;
	TArray<TSharedPtr<FString>> ControlNetOptions;
	TSharedPtr<FString> SelectedCheckpoint;
	TSharedPtr<FString> SelectedControlNet;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> CheckpointCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ControlNetCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LoRAPathCombo;

	// Raw unfiltered model lists from ComfyUI (used to re-filter when mode changes)
	TArray<FString> AllCheckpoints;
	TArray<FString> AllLoRAs;
	TArray<FString> AllControlNets;

	/** Model architecture classification */
	enum class EModelArch : uint8 { SD15, SDXL, Flux, Unknown };

	/** Guess model architecture from filename */
	static EModelArch DetectArchitecture(const FString& ModelName);

	/** Returns true if the model is compatible with the current Flux/SD mode */
	static bool IsCompatibleWithMode(const FString& ModelName, bool bFluxMode);

	/** Re-filter all dropdown options based on the current Flux/SD mode selection */
	void FilterModelsForCurrentMode();

	// Generation Mode combo box data
	TArray<TSharedPtr<FString>> GenModeOptions;
	TSharedPtr<FString> SelectedGenMode;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GenModeCombo;

	// Sampler/Scheduler combo box data
	TArray<TSharedPtr<FString>> SamplerOptions;
	TArray<TSharedPtr<FString>> SchedulerOptions;
	TSharedPtr<FString> SelectedSampler;
	TSharedPtr<FString> SelectedScheduler;

	/** Initialize the sampler/scheduler option arrays and set defaults from settings */
	void InitSamplerSchedulerOptions();

	/** Fetch model lists from ComfyUI and populate combo boxes */
	void FetchAndPopulateModels();

	/** Called when ComfyUI returns available models */
	void OnModelsReceived(const TArray<FString>& Checkpoints, const TArray<FString>& LoRAs, const TArray<FString>& ControlNets, const FGeminiNodeOptions& GeminiOptions, const FKlingNodeOptions& KlingOptions);

	// Gemini combo box data (dynamically populated from ComfyUI)
	TArray<TSharedPtr<FString>> GeminiModelOptions;
	TArray<TSharedPtr<FString>> GeminiAspectOptions;
	TArray<TSharedPtr<FString>> GeminiResolutionOptions;
	TArray<TSharedPtr<FString>> GeminiModalityOptions;
	TArray<TSharedPtr<FString>> GeminiThinkingOptions;

	TSharedPtr<FString> SelectedGeminiModel;
	TSharedPtr<FString> SelectedGeminiAspect;
	TSharedPtr<FString> SelectedGeminiResolution;
	TSharedPtr<FString> SelectedGeminiModality;
	TSharedPtr<FString> SelectedGeminiThinking;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> GeminiModelCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GeminiAspectCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GeminiResolutionCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GeminiModalityCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GeminiThinkingCombo;

	/** Populate Gemini dropdown arrays from fetched options */
	void PopulateGeminiDropdowns(const FGeminiNodeOptions& Options);

	// Kling combo box data (dynamically populated from ComfyUI)
	TArray<TSharedPtr<FString>> KlingModelOptions;
	TArray<TSharedPtr<FString>> KlingAspectOptions;
	TArray<TSharedPtr<FString>> KlingImageTypeOptions;

	TSharedPtr<FString> SelectedKlingModel;
	TSharedPtr<FString> SelectedKlingAspect;
	TSharedPtr<FString> SelectedKlingImageType;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> KlingModelCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> KlingAspectCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> KlingImageTypeCombo;

	/** Populate Kling dropdown arrays from fetched options */
	void PopulateKlingDropdowns(const FKlingNodeOptions& Options);

	/** Helper to find or add a string option in an options array */
	TSharedPtr<FString> FindOrAddOption(TArray<TSharedPtr<FString>>& Options, const FString& Value);

	bool bSettingsExpanded = false;
	bool bWorkflowPreviewExpanded = false;

	/** Workflow preview panel widget (legacy read-only preview) */
	TSharedPtr<SWorkflowPreviewPanel> WorkflowPreview;

	/** Interactive graph editor widget */
	TSharedPtr<SWorkflowGraphEditor> GraphEditor;

	/** Container that hosts the graph editor inline (used to swap it in/out for fullscreen) */
	TSharedPtr<SBorder> GraphEditorInlineContainer;

	/** Scrollable details panel showing the selected node's parameters */
	TSharedPtr<SScrollBox> NodeDetailsPanel;

	/** Rebuild the details panel contents when selection changes */
	void RebuildNodeDetailsPanel();

	/** Called when the graph editor's selection changes */
	void OnGraphSelectionChanged();

	/** Combo box option arrays for the details panel.
	 *  Stored as shared pointers so that SComboBox widgets keep the arrays alive
	 *  even after the details panel is rebuilt (avoiding dangling OptionsSource pointers). */
	TArray<TSharedPtr<TArray<TSharedPtr<FString>>>> DetailsPanelComboOptions;

	/** Browse for an image file and upload it to the ComfyUI server for a LoadImage node */
	void OnBrowseNodeImage(FString NodeId, FString WidgetName);

	/** Browse for a video file and upload it to the ComfyUI server for a LoadVideo node */
	void OnBrowseNodeVideo(FString NodeId, FString WidgetName);

	/** Extract the first frame of a video file as a thumbnail using ffmpeg.
	 *  Sets the thumbnail on the graph node if successful. */
	void ExtractVideoThumbnail(const FString& VideoFilePath, const FString& NodeId);

	/** Download the generated video from ComfyUI, extract a thumbnail, and
	 *  set up the preview (thumbnail + play button) in the main panel. */
	void HandleVideoResult();

	/** Last downloaded video result path (for Play button after generation) */
	FString LastVideoResultPath;

	/** Trigger Movie Render Graph Quick Render for the active Level Sequence,
	 *  then load the rendered video into the specified LoadVideo node. */
	void OnQuickRenderForNode(FString NodeId);

	/** Poll for Quick Render output completion */
	void PollQuickRenderOutput();

	/** Node ID awaiting Quick Render output */
	FString QuickRenderTargetNodeId;

	/** Directory being watched for Quick Render output */
	FString QuickRenderOutputDir;

	/** Timestamp when Quick Render started (to find new files) */
	FDateTime QuickRenderStartTime;

	/** Timer handle for polling Quick Render output */
	FTimerHandle QuickRenderPollTimer;

	/** Browse for a 3D model file and import it for a UE 3D Loader node */
	void OnBrowse3DModelFile(FString NodeId);

	/** Browse for a Content Browser folder path (for asset_path widgets on save/import nodes) */
	void OnBrowseAssetPath(FString NodeId, FString WidgetName);

	/** Browse for a disk folder (for output_dir, save_path, etc. on ComfyUI save nodes) */
	void OnBrowseDiskFolder(FString NodeId, FString WidgetName);

	/** Fetch thumbnails from ComfyUI for all LoadImage nodes that have an image filename set.
	 *  Called after loading a graph from file, preset, or reset. */
	void RefreshLoadImageThumbnails();

	/** Extract first-frame thumbnails for all LoadVideo nodes that have a local file path.
	 *  Called alongside RefreshLoadImageThumbnails after graph load. */
	void RefreshLoadVideoThumbnails();

	/** Thumbnail brush for the LoadImage node preview in the details panel */
	TSharedPtr<FSlateBrush> NodeDetailsThumbnailBrush;

	/** Build the workflow preview collapsible section */
	TSharedRef<SWidget> BuildWorkflowPreviewSection();

	/** Build the interactive graph editor section */
	TSharedRef<SWidget> BuildGraphEditorSection();

	/** Refresh the workflow preview graph (call after any setting change) */
	void RefreshWorkflowPreview();

	/** Whether to show the graph editor instead of read-only preview */
	bool bUseGraphEditor = true;

	/** Whether the graph editor section is expanded */
	bool bGraphEditorExpanded = false;

	/** Perform a one-shot prompt sync to the Video tab and all graph editor prompt nodes */
	void PerformPromptSync();

	/** Called whenever the main positive prompt text changes (auto-sync when enabled) */
	void OnPromptTextChanged(const FText& NewText);

	/** Called whenever the main negative prompt text changes (auto-sync when enabled) */
	void OnNegativePromptTextChanged(const FText& NewText);

	/** Whether to auto-sync the main prompt to Video tab and graph nodes on every edit */
	bool bAutoSyncPrompts = false;

	/** Re-entrancy guard — prevents cascading text-change events during sync */
	bool bSyncInProgress = false;

	/** Submit the graph editor's workflow directly to ComfyUI */
	FReply OnGenerateFromGraphClicked();

	/** Run the graph up to a specific node (partial execution from play button) */
	void OnRunToNodeRequested(const FString& TargetNodeId);

	/** Replace UE image marker filenames in the workflow with real uploaded filenames */
	void ResolveUEImageMarker(TSharedPtr<FJsonObject> Workflow, const FString& Marker, const FString& ServerFilename);

	/** Inject camera description text into workflow nodes marked with ue_camera_target_field */
	void ResolveUECameraData(TSharedPtr<FJsonObject> Workflow, const FString& CameraDescription);

	/** Save the current graph to a file (uses current path or prompts Save As) */
	FReply OnSaveGraphClicked();

	/** Save the current graph, always prompting for a file name */
	FReply OnSaveGraphAsClicked();

	/** Load a graph from a file, replacing the current graph */
	FReply OnLoadGraphClicked();

	/** Get the default Workflows directory inside the plugin folder */
	FString GetWorkflowsDirectory() const;

	/** Open the graph editor in a standalone fullscreen window */
	FReply OnGraphEditorFullscreen();

	/** Close the fullscreen graph editor window and return the editor to inline */
	void CloseGraphEditorFullscreen();

	/** The fullscreen window hosting the graph editor (null when inline) */
	TSharedPtr<SWindow> GraphEditorFullscreenWindow;

	/** Per-section collapse state within Settings */
	bool bConnectionExpanded = true;
	bool bModelExpanded = true;
	bool bGenerationExpanded = true;
	bool bDepthExpanded = false;
	bool bHiResExpanded = false;
	bool bGeminiExpanded = false;
	bool bKlingExpanded = false;

	/** Helper: build a collapsible section header + body for the settings panel */
	TSharedRef<SWidget> BuildCollapsibleSection(const FText& Title, bool& bExpandedFlag, TSharedRef<SWidget> Content);

	/** Reset all settings to their compiled defaults and refresh the UI */
	void ResetSettingsToDefaults();

	/**
	 * Apply the Reference Adherence slider value to the relevant per-mode
	 * settings (DenoisingStrength, ControlNetWeight, KlingImageFidelity, etc.)
	 */
	void ApplyAdherenceToSettings(float Adherence);

	// ---- Presets ----
	TArray<TSharedPtr<FString>> PresetOptions;
	TSharedPtr<FString> SelectedPreset;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PresetCombo;
	TSharedPtr<SEditableTextBox> PresetNameInput;

	/** Refresh the preset dropdown from disk */
	void RefreshPresetList();

	/** Save current settings as a preset (uses the name in PresetNameInput) */
	void SaveCurrentPreset();

	/** Load the currently selected preset */
	void LoadSelectedPreset();

	/** Delete the currently selected preset */
	void DeleteSelectedPreset();

	/** Refresh all UI widgets from the current UGenAISettings values */
	void RefreshUIFromSettings();

	/** Currently active LoRA entries in the UI (independent of saved settings) */
	TArray<FLoRAEntry> UILoRAEntries;

	float CurrentProgress = 0.0f;
	bool bIsGenerating = false;

	/** Timer handle for progress polling */
	FTimerHandle ProgressPollTimer;

	// ---- Staged (Sequence) Workflow Execution ----

	/** Queue of staged workflows to execute sequentially (populated when a Sequence node is present) */
	TArray<TSharedPtr<FJsonObject>> StagedWorkflowQueue;

	/** Index of the currently executing stage (0-based). -1 = not in staged mode. */
	int32 CurrentStageIndex = -1;

	/** Total number of stages for the current staged execution */
	int32 TotalStages = 0;

	/** Submit the next staged workflow in the queue, or finish if all done */
	void SubmitNextStagedWorkflow();

	// ---- Tab State ----

	/** 0 = Image Generation tab, 1 = Video Generation tab */
	int32 ActiveTabIndex = 0;

	/** Switcher widget to toggle between Image and Video content */
	TSharedPtr<SWidgetSwitcher> TabSwitcher;

	// ---- Video Tab UI ----

	/** Build the complete video generation tab content */
	TSharedRef<SWidget> BuildVideoTab();

	/** Build the video settings collapsible section */
	TSharedRef<SWidget> BuildVideoSettingsPanel();

	/** Source frame thumbnail brush (copy of the image being animated) */
	TSharedPtr<FSlateBrush> VideoSourceBrush;
	TSharedPtr<SImage> VideoSourceImage;

	/** Raw texture pointer for VideoSourceBrush — kept in sync for GC-safe refresh.
	 *  Used by OnPostGarbageCollect to refresh the brush's TObjectPtr without calling
	 *  GetResourceObject() (which would crash on a stale packed index). */
	UTexture2D* VideoSourceTextureRaw = nullptr;

	/** Video prompt text boxes */
	TSharedPtr<SMultiLineEditableTextBox> VideoPromptTextBox;
	TSharedPtr<SMultiLineEditableTextBox> VideoNegativePromptTextBox;

	/** Video mode combo */
	TArray<TSharedPtr<FString>> VideoModeOptions;
	TSharedPtr<FString> SelectedVideoMode;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> VideoModeCombo;

	/** Kling video model combo */
	TArray<TSharedPtr<FString>> KlingVideoModelOptions;
	TSharedPtr<FString> SelectedKlingVideoModel;

	/** Kling video quality combo */
	TArray<TSharedPtr<FString>> KlingVideoQualityOptions;
	TSharedPtr<FString> SelectedKlingVideoQuality;

	/** Apply the motion adherence slider to video settings */
	void ApplyVideoAdherenceToSettings(float Adherence);

	/** Generate video button handler */
	FReply OnAnimateClicked();

	/** Open a file dialog to load an image from disk as the video source frame */
	FReply OnLoadSourceFrameClicked();

	/** Texture loaded from disk (rooted; cleaned up in destructor) */
	UTexture2D* LoadedSourceFrameTexture = nullptr;

	/** Auto-set source frame from latest history entry (or current preview as fallback) */
	void SetVideoSourceFromPreview();

	/** Explicitly set the video source frame from a specific texture (e.g. gallery click) */
	void SetVideoSourceFromTexture(UTexture2D* Tex);

	/** Encode a UTexture2D to base64 PNG for uploading to ComfyUI */
	static FString TextureToBase64PNG(UTexture2D* Texture);

	/** Poll progress for video generation (delegates to HttpClient) */
	void PollVideoProgress();

	bool bVideoSettingsExpanded = true;
	bool bIsGeneratingVideo = false;

	// ---- Image History ----

	/** Entry for a single generation result */
	struct FHistoryEntry
	{
		TSharedPtr<FSlateBrush> Brush;
		UTexture2D* Texture = nullptr;

		/** Non-empty when this result is a video (local disk path for playback) */
		FString VideoPath;
	};

	/** All generated images in chronological order */
	TArray<FHistoryEntry> ImageHistory;

	/** Currently viewed index in ImageHistory (-1 = none) */
	int32 HistoryIndex = -1;

	/** Max number of results to keep in history */
	static constexpr int32 MaxHistoryEntries = 64;

	/** Navigate history: show the previous/next result */
	void ShowPreviousResult();
	void ShowNextResult();

	/** Jump to a specific history index and update the preview */
	void ShowHistoryEntry(int32 Index);

	/** Gallery scroll box (rebuilt when history changes) */
	TSharedPtr<SScrollBox> ResultGalleryBox;

	/** Rebuild the clickable thumbnail gallery from ImageHistory */
	void RebuildResultGallery();

	/** Populate the preview + gallery with segmentation results (mask + per-actor isolations) */
	void PopulateSegmentationGallery();

	// ---- SAM3 Segmentation ----

	/** Trigger SAM3 segmentation on the current preview image */
	FReply OnSegmentWithSAM3Clicked();

	/** Called when SAM3 segmentation results are received from ComfyUI */
	void OnSAM3SegmentationComplete(UTexture2D* VisualizationTexture,
		const TArray<UTexture2D*>& MaskTextures,
		UTexture2D* OriginalTexture);

	/** Apply SAM3 mask textures to the original image to produce isolated segments */
	void ApplyMasksToOriginal(UTexture2D* OriginalTexture, const TArray<UTexture2D*>& MaskTextures);

	/** A single SAM3 segmented object */
	struct FSAM3Segment
	{
		/** Isolated image (object on white background) */
		UTexture2D* IsolationTexture = nullptr;
		TSharedPtr<FSlateBrush> Brush;
		/** Base64 PNG of the isolation image for Meshy upload */
		FString Base64PNG;
		/** Whether the user has selected this segment for 3D conversion */
		bool bSelected = true;
		/** Meshy task ID if conversion is in progress */
		FString MeshyTaskId;
		/** Meshy task status */
		FString MeshyStatus;
		int32 MeshyProgress = 0;
	};

	/** SAM3 segmentation results */
	TArray<FSAM3Segment> SAM3Segments;

	/** Visualization overlay from SAM3 */
	UTexture2D* SAM3VisualizationTexture = nullptr;

	/** Scrollbox for the segmentation gallery */
	TSharedPtr<SScrollBox> SegmentationGalleryBox;

	/** Build / rebuild the segmentation gallery with checkboxes */
	void RebuildSegmentationGallery();

	/** Clean up SAM3 segment textures */
	void ClearSAM3Segments();

	// ---- Meshy 3D Conversion ----

	/** Meshy API client instance */
	TUniquePtr<FMeshyApiClient> MeshyClient;

	/** Convert a single selected segment to 3D via Meshy */
	void ConvertSegmentToMesh(int32 SegmentIndex);

	/** Convert all selected segments to 3D */
	FReply OnConvertAllSelectedClicked();

	/** Called when a Meshy task is created */
	void OnMeshyTaskCreated(bool bSuccess, const FString& TaskIdOrError, int32 SegmentIndex);

	/** Called when a Meshy task reports progress */
	void OnMeshyTaskProgress(const FMeshyTaskResult& Result);

	/** Called when a Meshy task completes */
	void OnMeshyTaskComplete(const FMeshyTaskResult& Result);

	/** Called when a GLB model is downloaded (from segmentation flow) */
	void OnMeshyModelDownloaded(const FString& LocalFilePath);

	// ---- Generic 3D Model Import (Graph Node) ----

	/** Execute all Save3DModel nodes found in the current graph.
	 *  Downloads the 3D model file from ComfyUI output and imports it into UE. */
	void ExecuteSave3DModelNodes();

	/** Shared logic for importing a downloaded 3D model into UE (used by ExecuteSave3DModelNodes and retries) */
	void Handle3DModelImport(const FString& DownloadedPath, const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel);

	/** Execute all 3D Asset Export nodes found in the current graph.
	 *  Downloads the 3D model, imports as .uasset with collision/Nanite settings, registers in asset browser. */
	void Execute3DAssetExportNodes();

	/** Execute all Image Upres nodes found in the current graph.
	 *  Fetches the upscaled result image from ComfyUI, converts to 16-bit if requested,
	 *  and saves to disk as EXR or PNG. Handles the full 8→16 bit pipeline UE-side. */
	void ExecuteImageUpresExport();

	/** Shared logic for importing and configuring a downloaded 3D model as a .uasset */
	void Handle3DAssetExport(const FString& DownloadedPath, const FString& AssetPath,
		const FString& AssetName, const FString& CollisionMode, bool bEnableNanite, bool bPlaceInLevel);

	/** Apply post-import settings (collision, Nanite) to an imported static mesh.
	 *  Called after the mesh is imported into the content browser. */
	void Apply3DAssetExportSettings(UStaticMesh* Mesh, const FString& CollisionMode, bool bEnableNanite);

	// ---- Meshy Import-to-Level (Graph Node) ----

	/** Execute all MeshyImport nodes found in the current graph.
	 *  Called after a Meshy task completes (when a task ID is available). */
	void ExecuteMeshyImportNodes();

	/** Fetch full task details from the Meshy API by task ID, then import the model. */
	void FetchAndImportMeshyTask(const FString& TaskId, const FString& ModelFormat,
		const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel);

	/** Launch a Meshy Image-to-3D task with a base64 PNG image, poll for completion,
	 *  then download and import the model using the given settings. */
	void LaunchMeshyTaskFromImage(const FString& Base64PNG, const FString& ModelFormat,
		const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel);

	/** Download the model from a completed Meshy task result, import it into the content browser,
	 *  and optionally place it in the current level.
	 *  @param TaskResult     The completed Meshy task result with model URLs
	 *  @param ModelFormat    "GLB", "FBX", or "OBJ"
	 *  @param AssetPath      Content browser destination path (e.g. "/Game/ViewGen/MeshyModels")
	 *  @param AssetName      Desired asset name (empty = auto-generate from task ID)
	 *  @param bPlaceInLevel  Whether to spawn the imported mesh at world origin in the current level
	 */
	void ImportMeshyModelToLevel(const FMeshyTaskResult& TaskResult, const FString& ModelFormat,
		const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel);

	/** Import a downloaded model file into the UE content browser using AssetTools.
	 *  Returns the path of the imported asset, or empty on failure. */
	FString ImportModelFileToContentBrowser(const FString& LocalFilePath, const FString& DestAssetPath, const FString& DesiredAssetName);

	/** Spawn a static mesh from a content browser asset path into the current level at world origin.
	 *  Returns the spawned actor, or null on failure. */
	AActor* PlaceAssetInLevel(const FString& AssetPath);

	/** Timer handle for Meshy polling */
	FTimerHandle MeshyPollTimer;

	/** Poll all active Meshy tasks */
	void PollMeshyTasks();

	// ---- Graph Auto-Persistence ----

	/** Save the current graph state to the auto-save file for restoration on next launch */
	void AutoSaveGraph();

	/** Try to restore the last-edited graph from the auto-save file.
	 *  Returns true if a graph was restored, false if no auto-save was found. */
	bool RestoreLastGraph();

	/** Get the file path for the auto-save graph file */
	static FString GetAutoSaveGraphPath();

	/** Debounce timer for auto-save (avoids saving on every tiny change) */
	FTimerHandle AutoSaveTimer;

	// ---- Pending Meshy Work ----

	/** When the graph contains a MeshyImageToModelNode, this stores the ComfyUI
	 *  filename of the source image so the UE-side Meshy call can use it after
	 *  the rest of the workflow completes. Empty if no Meshy work is pending. */
	FString PendingMeshyImageFilename;

	/** Base64 PNG of the image to send to Meshy (populated from ComfyUI or viewport) */
	FString PendingMeshyImageBase64;

	/** Text prompt input for SAM3 segmentation */
	TSharedPtr<SEditableTextBox> SAM3PromptInput;
};
