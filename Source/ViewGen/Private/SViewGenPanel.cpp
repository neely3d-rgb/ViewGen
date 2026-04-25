// Copyright ViewGen. All Rights Reserved.

#include "SViewGenPanel.h"
#include "SWorkflowPreviewPanel.h"
#include "SWorkflowGraphEditor.h"
#include "ViewportCapture.h"
#include "DepthPassRenderer.h"
#include "GenAIHttpClient.h"
#include "GenAISettings.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "EditorStyleSet.h"
#include "Editor.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

// Movie Render Graph — Quick Render
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Graph/MovieGraphQuickRender.h"

// Asset import and level placement
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/PackageName.h"
#include "UObject/GarbageCollection.h"

#define LOCTEXT_NAMESPACE "SViewGenPanel"

void SViewGenPanel::Construct(const FArguments& InArgs)
{
	// Initialize subsystems
	ViewportCapture = MakeUnique<FViewportCapture>();
	DepthRenderer = MakeUnique<FDepthPassRenderer>();
	SegmentationCapture = MakeUnique<FSegmentationCapture>();
	HttpClient = MakeUnique<FGenAIHttpClient>();
	MeshyClient = MakeUnique<FMeshyApiClient>();

	// Bind HTTP callbacks
	HttpClient->OnComplete.BindRaw(this, &SViewGenPanel::OnGenerationComplete);
	HttpClient->OnProgress.BindRaw(this, &SViewGenPanel::OnGenerationProgress);
	HttpClient->OnError.BindRaw(this, &SViewGenPanel::OnGenerationError);
	HttpClient->OnNodeExecuting.BindLambda([this](const FString& NodeId)
	{
		// WebSocket callbacks may arrive on a background thread — dispatch to game thread for Slate updates
		AsyncTask(ENamedThreads::GameThread, [this, NodeId]()
		{
			if (GraphEditor.IsValid())
			{
				if (NodeId.IsEmpty())
				{
					GraphEditor->ClearExecutingNodes();
				}
				else
				{
					GraphEditor->SetExecutingNode(NodeId);
				}
			}
		});
	});

	// Bind SAM3 segmentation callback
	HttpClient->OnSegmentationComplete.BindRaw(this, &SViewGenPanel::OnSAM3SegmentationComplete);

	// Load saved LoRA entries from settings
	const UGenAISettings* Settings = UGenAISettings::Get();
	UILoRAEntries = Settings->LoRAModels;

	// Initialize sampler/scheduler dropdowns
	InitSamplerSchedulerOptions();

	// Initialize generation mode dropdown
	{
		GenModeOptions.Add(MakeShareable(new FString(TEXT("img2img (Viewport + Prompt)"))));
		GenModeOptions.Add(MakeShareable(new FString(TEXT("Depth + Prompt"))));
		GenModeOptions.Add(MakeShareable(new FString(TEXT("Prompt Only (txt2img)"))));
		GenModeOptions.Add(MakeShareable(new FString(TEXT("Gemini (Nano Banana 2)"))));
		GenModeOptions.Add(MakeShareable(new FString(TEXT("Kling (Image 3.0)"))));

		int32 ModeIndex = FMath::Clamp(static_cast<int32>(Settings->GenerationMode), 0, GenModeOptions.Num() - 1);
		SelectedGenMode = GenModeOptions[ModeIndex];
	}

	// Initialize model dropdowns with current settings (will be refreshed from ComfyUI)
	SelectedCheckpoint = MakeShareable(new FString(Settings->CheckpointName));
	CheckpointOptions.Add(SelectedCheckpoint);

	SelectedControlNet = MakeShareable(new FString(Settings->ControlNetModel));
	ControlNetOptions.Add(SelectedControlNet);

	// Initialize Gemini dropdowns with current settings (will be refreshed from ComfyUI)
	SelectedGeminiModel = MakeShareable(new FString(Settings->GeminiModelName));
	GeminiModelOptions.Add(SelectedGeminiModel);
	SelectedGeminiAspect = MakeShareable(new FString(Settings->GeminiAspectRatio));
	GeminiAspectOptions.Add(SelectedGeminiAspect);
	SelectedGeminiResolution = MakeShareable(new FString(Settings->GeminiResolution));
	GeminiResolutionOptions.Add(SelectedGeminiResolution);
	SelectedGeminiModality = MakeShareable(new FString(Settings->GeminiResponseModalities));
	GeminiModalityOptions.Add(SelectedGeminiModality);
	SelectedGeminiThinking = MakeShareable(new FString(Settings->GeminiThinkingLevel));
	GeminiThinkingOptions.Add(SelectedGeminiThinking);

	// Create default brushes (blank/placeholder)
	ViewportThumbnailBrush = MakeShareable(new FSlateBrush());
	DepthThumbnailBrush = MakeShareable(new FSlateBrush());
	PreviewBrush = MakeShareable(new FSlateBrush());

	// Initialize video UI state
	VideoSourceBrush = MakeShareable(new FSlateBrush());
	{
		VideoModeOptions.Add(MakeShareable(new FString(TEXT("Kling Video"))));
		VideoModeOptions.Add(MakeShareable(new FString(TEXT("Wan I2V"))));
		VideoModeOptions.Add(MakeShareable(new FString(TEXT("Google Veo 3"))));

		int32 VModeIndex = FMath::Clamp(static_cast<int32>(Settings->VideoMode), 0, VideoModeOptions.Num() - 1);
		SelectedVideoMode = VideoModeOptions[VModeIndex];

		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v2-master"))));
		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v2-1"))));
		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v2-1-master"))));
		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v2-5-turbo"))));
		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v1-6"))));
		KlingVideoModelOptions.Add(MakeShareable(new FString(TEXT("kling-v1-5"))));
		SelectedKlingVideoModel = MakeShareable(new FString(Settings->KlingVideoModel));

		KlingVideoQualityOptions.Add(MakeShareable(new FString(TEXT("std"))));
		KlingVideoQualityOptions.Add(MakeShareable(new FString(TEXT("pro"))));
		SelectedKlingVideoQuality = MakeShareable(new FString(Settings->KlingVideoQuality));
	}

	// Build the full UI
	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			BuildToolbar()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Tab bar: Image | Video
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]()
				{
					return ActiveTabIndex == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ActiveTabIndex = 0;
					if (TabSwitcher.IsValid()) TabSwitcher->SetActiveWidgetIndex(0);
				})
				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImageTab", "Image Generation"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([this]()
				{
					return ActiveTabIndex == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ActiveTabIndex = 1;
					if (TabSwitcher.IsValid()) TabSwitcher->SetActiveWidgetIndex(1);
					// Auto-set source frame from current preview
					SetVideoSourceFromPreview();
				})
				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("VideoTab", "Video Generation"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Tab content switcher
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TabSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)

			// Tab 0: Image Generation (existing layout)
			+ SWidgetSwitcher::Slot()
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)

				// Top: Thumbnails + Preview image + Result gallery
				+ SSplitter::Slot()
				.Value(0.55f)
				[
					SNew(SBox)
					.Padding(4.0f)
					[
						SNew(SSplitter)
						.Orientation(Orient_Horizontal)

						// Left: Input thumbnails column
						+ SSplitter::Slot()
						.Value(0.20f)
						[
							BuildThumbnailPanel()
						]

						// Center: Preview window with arrow navigation
						+ SSplitter::Slot()
						.Value(0.65f)
						[
							BuildPreviewPanel()
						]

						// Right: Result history gallery
						+ SSplitter::Slot()
						.Value(0.15f)
						[
							BuildResultGalleryPanel()
						]
					]
				]

				// Bottom: Prompt, LoRA, Settings, Workflow Preview (scrollable)
				// Graph editor body + toolbar are pinned below the scroll area
				+ SSplitter::Slot()
				.Value(0.45f)
				[
					SNew(SVerticalBox)

					// Scrollable content area (prompt, LoRA, settings, workflow preview, graph header toggle)
					// Hidden when graph editor is expanded so the graph body gets all available space.
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							return bGraphEditorExpanded ? EVisibility::Collapsed : EVisibility::Visible;
						})
						[
							SNew(SScrollBox)

						+ SScrollBox::Slot()
						.Padding(4.0f)
						[
							BuildPromptPanel()
						]

						+ SScrollBox::Slot()
						[
							SNew(SSeparator)
						]

						+ SScrollBox::Slot()
						.Padding(4.0f)
						[
							BuildLoRAPanel()
						]

						+ SScrollBox::Slot()
						[
							SNew(SSeparator)
						]

						+ SScrollBox::Slot()
						.Padding(4.0f)
						[
							BuildSettingsPanel()
						]

						+ SScrollBox::Slot()
						[
							SNew(SSeparator)
						]

						+ SScrollBox::Slot()
						.Padding(4.0f)
						[
							BuildWorkflowPreviewSection()
						]

						+ SScrollBox::Slot()
						[
							SNew(SSeparator)
						]

						+ SScrollBox::Slot()
						.Padding(4.0f)
						[
							BuildGraphEditorSection()
						]
						]
					]

					// Graph editor toggle header — visible only when graph is expanded (scroll box is hidden)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 2.0f, 4.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							return bGraphEditorExpanded ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							BuildGraphEditorSection()
						]
					]

					// Graph editor body — OUTSIDE the scroll box so it can fill remaining space
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(4.0f, 2.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							return bGraphEditorExpanded ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SSplitter)
							.Orientation(Orient_Horizontal)

							// Left: Graph canvas
							+ SSplitter::Slot()
							.Value(0.70f)
							[
								SAssignNew(GraphEditorInlineContainer, SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
								.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f))
								.Padding(0.0f)
								[
									SAssignNew(GraphEditor, SWorkflowGraphEditor)
									.OnGraphChanged_Lambda([this]()
									{
										RebuildNodeDetailsPanel();

										// Debounced auto-save: wait 2 seconds after last change before saving
										if (GEditor)
										{
											GEditor->GetTimerManager()->ClearTimer(AutoSaveTimer);
											GEditor->GetTimerManager()->SetTimer(AutoSaveTimer, FTimerDelegate::CreateLambda([this]()
											{
												AutoSaveGraph();
											}), 2.0f, false);
										}
									})
									.OnSelectionChanged_Lambda([this]()
									{
										OnGraphSelectionChanged();
									})
								]
							]

							// Right: Node details panel
							+ SSplitter::Slot()
							.Value(0.30f)
							[
								SNew(SBorder)
								.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
								.BorderBackgroundColor(FLinearColor(0.09f, 0.09f, 0.10f))
								.Padding(4.0f)
								[
									SAssignNew(NodeDetailsPanel, SScrollBox)
								]
							]
						]
					]

					// Graph editor toolbar — pinned to the bottom, always visible when graph is expanded
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 4.0f, 4.0f, 2.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							return bGraphEditorExpanded ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Generate from Graph")))
								.ToolTipText(FText::FromString(TEXT("Submit the current node graph directly to ComfyUI for generation")))
								.OnClicked(this, &SViewGenPanel::OnGenerateFromGraphClicked)
								.IsEnabled_Lambda([this]()
								{
									return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0
										&& HttpClient.IsValid() && !HttpClient->IsRequestInProgress();
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Auto-Layout")))
								.ToolTipText(FText::FromString(TEXT("Automatically arrange nodes left-to-right (Ctrl+L)")))
								.OnClicked_Lambda([this]() -> FReply
								{
									if (GraphEditor.IsValid())
									{
										GraphEditor->AutoLayout();
									}
									return FReply::Handled();
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Reset Graph")))
								.ToolTipText(FText::FromString(TEXT("Rebuild the graph from current settings")))
								.OnClicked_Lambda([this]() -> FReply
								{
									if (GraphEditor.IsValid())
									{
										GraphEditor->BuildPresetGraph();
										RefreshLoadImageThumbnails();
									}
									return FReply::Handled();
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Clear")))
								.ToolTipText(FText::FromString(TEXT("Remove all nodes from the graph")))
								.OnClicked_Lambda([this]() -> FReply
								{
									if (GraphEditor.IsValid())
									{
										GraphEditor->ClearGraph();
									}
									return FReply::Handled();
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SSeparator)
								.Orientation(Orient_Vertical)
								.SeparatorImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
								.ColorAndOpacity(FLinearColor(0.3f, 0.3f, 0.3f, 0.3f))
								.Thickness(1.0f)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text_Lambda([this]() -> FText
								{
									if (GraphEditor.IsValid() && GraphEditor->IsDirty())
									{
										return FText::FromString(TEXT("Save*"));
									}
									return FText::FromString(TEXT("Save"));
								})
								.ToolTipText_Lambda([this]() -> FText
								{
									if (GraphEditor.IsValid() && !GraphEditor->GetCurrentFilePath().IsEmpty())
									{
										return FText::FromString(FString::Printf(TEXT("Save to %s"),
											*FPaths::GetCleanFilename(GraphEditor->GetCurrentFilePath())));
									}
									return FText::FromString(TEXT("Save the current workflow graph to a file"));
								})
								.OnClicked(this, &SViewGenPanel::OnSaveGraphClicked)
								.IsEnabled_Lambda([this]()
								{
									return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0;
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Save As")))
								.ToolTipText(FText::FromString(TEXT("Save the workflow graph to a new file")))
								.OnClicked(this, &SViewGenPanel::OnSaveGraphAsClicked)
								.IsEnabled_Lambda([this]()
								{
									return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0;
								})
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Load")))
								.ToolTipText(FText::FromString(TEXT("Load a previously saved workflow graph from a file")))
								.OnClicked(this, &SViewGenPanel::OnLoadGraphClicked)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNullWidget::NullWidget
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("Fullscreen")))
								.ToolTipText(FText::FromString(TEXT("Open the graph editor in a fullscreen window")))
								.OnClicked(this, &SViewGenPanel::OnGraphEditorFullscreen)
							]
						]
					]
				]
			]

			// Tab 1: Video Generation
			+ SWidgetSwitcher::Slot()
			[
				BuildVideoTab()
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Status bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			BuildStatusBar()
		]
	];

	UpdateStatusText(TEXT("Ready - Capture viewport to begin"));

	// Populate preset dropdown from saved files
	RefreshPresetList();

	// Fetch available models from ComfyUI (async — populates dropdowns when done)
	FetchAndPopulateModels();

	// Register PostGC callback to refresh stale TObjectPtr handles in FSlateBrush objects.
	// GC compaction can run mid-frame (e.g. during SavePackage) between Slate Tick and Paint,
	// invalidating packed indices in heap-allocated brushes that aren't tracked by UPROPERTY.
	PostGCDelegateHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &SViewGenPanel::OnPostGarbageCollect);
}

SViewGenPanel::~SViewGenPanel()
{
	// Unregister PostGC callback
	if (PostGCDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCDelegateHandle);
	}

	if (HttpClient.IsValid())
	{
		HttpClient->CancelRequest();
	}

	// Clean up segmentation capture FIRST — it owns textures that may also
	// be referenced in ImageHistory. Clearing it here prevents double-unroot.
	SegmentationCapture.Reset();

	// Unroot all history textures so they can be GC'd.
	// During editor shutdown UObjects may already be destroyed, so check
	// IsValidLowLevel() before touching the object to avoid index assertions.
	for (FHistoryEntry& Entry : ImageHistory)
	{
		if (Entry.Texture && ::IsValid(Entry.Texture) && Entry.Texture->IsRooted())
		{
			Entry.Texture->RemoveFromRoot();
		}
		Entry.Texture = nullptr;
	}
	ImageHistory.Empty();

	// Unroot loaded-from-disk source frame texture
	if (LoadedSourceFrameTexture && ::IsValid(LoadedSourceFrameTexture) && LoadedSourceFrameTexture->IsRooted())
	{
		LoadedSourceFrameTexture->RemoveFromRoot();
	}
	LoadedSourceFrameTexture = nullptr;

	// Clean up SAM3 segmentation textures
	ClearSAM3Segments();

	// Stop Meshy polling
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(MeshyPollTimer);
	}
}

// ============================================================================
// TObjectPtr handle refresh (GC compaction safety)
// ============================================================================

/** Helper: refresh a heap-allocated FSlateBrush's TObjectPtr ResourceObject handle
 *  from a known-valid raw texture pointer. NEVER calls GetResourceObject() because
 *  that resolves the potentially-stale TObjectPtr packed index and would crash.
 *  Instead, uses the raw pointer (stable through GC since textures are rooted)
 *  to create a fresh TObjectPtr handle via SetResourceObject(). */
static void RefreshBrushFromRawTexture(const TSharedPtr<FSlateBrush>& Brush, UObject* RawTexture)
{
	if (!Brush.IsValid()) return;

	if (RawTexture && ::IsValid(RawTexture))
	{
		Brush->SetResourceObject(RawTexture);
	}
	else
	{
		Brush->SetResourceObject(nullptr);
	}
}

void SViewGenPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// NOTE: The heavy lifting for GC safety is done by OnPostGarbageCollect().
	// Tick is kept minimal — no brush handle refresh here because GC can run
	// between Tick and Paint, making any refresh done here useless.
}

void SViewGenPanel::OnPostGarbageCollect()
{
	// After GC compaction, UObject array indices may change. TObjectPtr handles in
	// heap-allocated FSlateBrush objects (not tracked by UPROPERTY) become stale.
	// Re-set ResourceObject from stored raw pointers to create fresh packed handles.
	// This callback fires immediately after GC, before any subsequent Slate paint.

	RefreshBrushFromRawTexture(ViewportThumbnailBrush,
		ViewportCapture.IsValid() ? ViewportCapture->GetCapturedTexture() : nullptr);
	RefreshBrushFromRawTexture(DepthThumbnailBrush,
		DepthRenderer.IsValid() ? DepthRenderer->GetDepthTexture() : nullptr);

	// PreviewBrush shows the current history entry's texture
	UTexture2D* CurrentPreviewTex = nullptr;
	if (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num())
	{
		CurrentPreviewTex = ImageHistory[HistoryIndex].Texture;
	}
	RefreshBrushFromRawTexture(PreviewBrush, CurrentPreviewTex);

	// VideoSourceBrush — uses tracked raw pointer
	RefreshBrushFromRawTexture(VideoSourceBrush, VideoSourceTextureRaw);

	// NodeDetailsThumbnailBrush — shares same brush as a graph node's ThumbnailBrush
	// (refreshed by SWorkflowGraphEditor's own PostGC callback, so skip here)

	// History entry brushes
	for (FHistoryEntry& Entry : ImageHistory)
	{
		RefreshBrushFromRawTexture(Entry.Brush, Entry.Texture);
	}

	// SAM3 segmentation brushes
	for (FSAM3Segment& Seg : SAM3Segments)
	{
		RefreshBrushFromRawTexture(Seg.Brush, Seg.IsolationTexture);
	}
}

// ============================================================================
// Toolbar
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CaptureBtn", "Capture Viewport"))
			.ToolTipText(LOCTEXT("CaptureTip", "Capture the active editor viewport and its depth buffer"))
			.OnClicked(this, &SViewGenPanel::OnCaptureViewportClicked)
			.IsEnabled_Lambda([this]() { return !bIsGenerating; })
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("GenerateBtn", "Generate"))
			.ToolTipText(LOCTEXT("GenerateTip", "Send captured images and prompt to the AI backend"))
			.OnClicked(this, &SViewGenPanel::OnGenerateClicked)
			.IsEnabled_Lambda([this]()
			{
				if (bIsGenerating) return false;
				const EGenMode Mode = UGenAISettings::Get()->GenerationMode;
				switch (Mode)
				{
				case EGenMode::Img2Img:
					return ViewportCapture->HasCapture();
				case EGenMode::DepthAndPrompt:
					return DepthRenderer->HasCapture();
				case EGenMode::PromptOnly:
				case EGenMode::Gemini:
				case EGenMode::Kling:
					return true; // No capture required (Kling uses optional reference)
				default:
					return ViewportCapture->HasCapture();
				}
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CancelBtn", "Cancel"))
			.ToolTipText(LOCTEXT("CancelTip", "Cancel the current generation request"))
			.OnClicked(this, &SViewGenPanel::OnCancelClicked)
			.IsEnabled_Lambda([this]() { return bIsGenerating; })
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SettingsBtn", "Settings"))
			.ToolTipText(LOCTEXT("SettingsTip", "Toggle settings panel"))
			.OnClicked(this, &SViewGenPanel::OnOpenSettingsClicked)
		];
}

// ============================================================================
// Thumbnail Panel (left sidebar)
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildThumbnailPanel()
{
	return SNew(SVerticalBox)

		// Viewport capture thumbnail
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 4.0f, 4.0f, 4.0f)
		[
			BuildThumbnailCard(LOCTEXT("ViewportLabel", "Viewport"), ViewportThumbnailBrush)
		]

		// Depth map thumbnail
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f, 0.0f, 4.0f, 4.0f)
		[
			BuildThumbnailCard(LOCTEXT("DepthLabel", "Depth Map"), DepthThumbnailBrush)
		];
}

TSharedRef<SWidget> SViewGenPanel::BuildThumbnailCard(const FText& Label, TSharedPtr<FSlateBrush>& OutBrush)
{
	TSharedPtr<SImage> ImageWidget;

	TSharedRef<SWidget> Card = SNew(SVerticalBox)

		// Label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		// Image with border - fills available space, aspect ratio maintained by SScaleBox
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(2.0f)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ImageWidget, SImage)
					.Image(OutBrush.Get())
				]
			]
		];

	// Store the image widget reference for later updates
	if (&OutBrush == &ViewportThumbnailBrush)
	{
		ViewportThumbnailImage = ImageWidget;
	}
	else if (&OutBrush == &DepthThumbnailBrush)
	{
		DepthThumbnailImage = ImageWidget;
	}

	return Card;
}

// ============================================================================
// Preview Panel (main area)
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildPreviewPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(4.0f)
		[
			SNew(SOverlay)

			// Main layer: left arrow | preview image | right arrow
			+ SOverlay::Slot()
			[
				SNew(SHorizontalBox)

				// Left arrow (previous)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4.0f, 8.0f))
					.ToolTipText(LOCTEXT("PrevResultTip", "Previous result"))
					.IsEnabled_Lambda([this]()
					{
						return HistoryIndex > 0;
					})
					.OnClicked_Lambda([this]() -> FReply
					{
						ShowPreviousResult();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u25C0")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity_Lambda([this]()
						{
							return HistoryIndex > 0
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f));
						})
					]
				]

				// Center: preview image (fills remaining space)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.Cursor(EMouseCursor::Hand)
						.OnClicked_Lambda([this]() -> FReply
						{
							if (HttpClient.IsValid() && HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num())
							{
								HttpClient->OpenOutputFolder();
							}
							return FReply::Handled();
						})
						.ToolTipText(LOCTEXT("PreviewTooltip", "Click to open the output folder"))
						.Visibility_Lambda([this]()
						{
							return (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num())
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SScaleBox)
							.Stretch(EStretch::ScaleToFit)
							[
								SAssignNew(PreviewImage, SImage)
								.Image(PreviewBrush.Get())
							]
						]
					]

					// Placeholder text when no image
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoPreview", "No generated image yet\nCapture viewport and click Generate"))
						.Justification(ETextJustify::Center)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.7f)))
						.Visibility_Lambda([this]()
						{
							return (HistoryIndex < 0 || HistoryIndex >= ImageHistory.Num())
								? EVisibility::Visible : EVisibility::Collapsed;
						})
					]
				]

				// Right arrow (next)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4.0f, 8.0f))
					.ToolTipText(LOCTEXT("NextResultTip", "Next result"))
					.IsEnabled_Lambda([this]()
					{
						return HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num() - 1;
					})
					.OnClicked_Lambda([this]() -> FReply
					{
						ShowNextResult();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u25B6")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity_Lambda([this]()
						{
							return (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num() - 1)
								? FSlateColor(FLinearColor::White)
								: FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f));
						})
					]
				]
			]

			// History counter overlay (top-right)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 4.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					if (ImageHistory.Num() == 0) return FText::GetEmpty();
					return FText::FromString(FString::Printf(TEXT("%d / %d"),
						HistoryIndex + 1, ImageHistory.Num()));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 0.8f)))
			]

			// Video play button overlay (center, only visible for video results)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.Cursor(EMouseCursor::Hand)
				.Visibility_Lambda([this]()
				{
					if (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num()
						&& !ImageHistory[HistoryIndex].VideoPath.IsEmpty())
					{
						return EVisibility::Visible;
					}
					return EVisibility::Collapsed;
				})
				.ToolTipText_Lambda([this]()
				{
					if (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num()
						&& !ImageHistory[HistoryIndex].VideoPath.IsEmpty())
					{
						return FText::FromString(FString::Printf(TEXT("Play %s"),
							*FPaths::GetCleanFilename(ImageHistory[HistoryIndex].VideoPath)));
					}
					return FText::GetEmpty();
				})
				.OnClicked_Lambda([this]() -> FReply
				{
					if (HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num()
						&& !ImageHistory[HistoryIndex].VideoPath.IsEmpty())
					{
						FString NativePath = ImageHistory[HistoryIndex].VideoPath;
						FPaths::MakePlatformFilename(NativePath);
						FPlatformProcess::LaunchFileInDefaultExternalApplication(*NativePath);
					}
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
					.Padding(FMargin(18.0f, 10.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u25B6  Play Video")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
				]
			]
		];
}

// ============================================================================
// Prompt Panel
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildPromptPanel()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	return SNew(SVerticalBox)

		// Adherence slider — affects generation parameters globally across all modes
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 4.0f)
		[
			SNew(SVerticalBox)

			// Label row: title on left, percentage on right
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([]()
					{
						const EGenMode Mode = UGenAISettings::Get()->GenerationMode;
						switch (Mode)
						{
						case EGenMode::PromptOnly:  return FText::FromString(TEXT("Prompt Adherence"));
						case EGenMode::Gemini:      return FText::FromString(TEXT("Prompt Adherence"));
						default:                    return FText::FromString(TEXT("Ref. Adherence"));
						}
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.70f, 0.35f)))
				]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNullWidget::NullWidget
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([]()
						{
							return FText::FromString(FString::Printf(TEXT("%d%%"),
								FMath::RoundToInt(UGenAISettings::Get()->ReferenceAdherence * 100.0f)));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
					]
				]

				// Slider row
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SSpinBox<float>)
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.Delta(0.01f)
					.Value_Lambda([]() { return UGenAISettings::Get()->ReferenceAdherence; })
					.OnValueChanged_Lambda([this](float NewVal)
					{
						UGenAISettings::Get()->ReferenceAdherence = NewVal;
						ApplyAdherenceToSettings(NewVal);
					})
					.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
					{
						UGenAISettings::Get()->ReferenceAdherence = NewVal;
						ApplyAdherenceToSettings(NewVal);
					})
				]
		]

		// Prompt header with Sync All checkbox
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PromptLabel", "Prompt"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(FText::FromString(TEXT("When enabled, automatically syncs this prompt to the Video tab and all prompt nodes in the Graph Editor")))
				.IsChecked_Lambda([this]() { return bAutoSyncPrompts ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bAutoSyncPrompts = (NewState == ECheckBoxState::Checked);
					if (bAutoSyncPrompts)
					{
						PerformPromptSync();
					}
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Sync All")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(50.0f)
			.MaxDesiredHeight(100.0f)
			[
				SAssignNew(PromptTextBox, SMultiLineEditableTextBox)
				.Text(FText::FromString(Settings->DefaultPrompt))
				.HintText(LOCTEXT("PromptHint", "Describe the desired generative result..."))
				.AutoWrapText(true)
				.OnTextChanged(this, &SViewGenPanel::OnPromptTextChanged)
				.OnTextCommitted_Lambda([](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->DefaultPrompt = NewText.ToString();
					UGenAISettings::Get()->SaveConfig();
				})
			]
		]

		// Negative Prompt
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NegPromptLabel", "Negative Prompt"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.MinDesiredHeight(30.0f)
			.MaxDesiredHeight(60.0f)
			[
				SAssignNew(NegativePromptTextBox, SMultiLineEditableTextBox)
				.Text(FText::FromString(Settings->DefaultNegativePrompt))
				.HintText(LOCTEXT("NegPromptHint", "Things to avoid in the generation..."))
				.AutoWrapText(true)
				.OnTextChanged(this, &SViewGenPanel::OnNegativePromptTextChanged)
				.OnTextCommitted_Lambda([](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->DefaultNegativePrompt = NewText.ToString();
					UGenAISettings::Get()->SaveConfig();
				})
			]
		];
}

// ============================================================================
// LoRA Panel
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildLoRAPanel()
{
	return SNew(SVerticalBox)

		// Header + Add row
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LoRALabel", "LoRA"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SAssignNew(LoRAPathCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&LoRAModelOptions)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					// Selection sets the text for "Add" to use
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						if (LoRAPathCombo.IsValid())
						{
							TSharedPtr<FString> Sel = LoRAPathCombo->GetSelectedItem();
							if (Sel.IsValid()) return FText::FromString(*Sel);
						}
						return LoRAModelOptions.Num() > 0
							? FText::FromString(TEXT("Select a LoRA..."))
							: FText::FromString(TEXT("No LoRAs found"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddLoRA", "+ Add"))
				.OnClicked(this, &SViewGenPanel::OnAddLoRAClicked)
			]
		]

		// LoRA list
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(LoRAListBox, SVerticalBox)
			]
		];
}

// ============================================================================
// Status Bar
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildStatusBar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(LOCTEXT("StatusReady", "Ready"))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ProgressBar, SProgressBar)
			.Percent_Lambda([this]() { return CurrentProgress; })
			.Visibility_Lambda([this]()
			{
				return bIsGenerating ? EVisibility::Visible : EVisibility::Collapsed;
			})
		];
}

// ============================================================================
// Actions
// ============================================================================

FReply SViewGenPanel::OnCaptureViewportClicked()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	UpdateStatusText(TEXT("Capturing viewport..."));

	// Capture color viewport at native viewport resolution for accurate aspect ratio.
	// Generation APIs handle their own resizing; OutputWidth/OutputHeight apply downstream.
	bool bColorSuccess = ViewportCapture->CaptureActiveViewport();

	if (bColorSuccess)
	{
		UpdateThumbnailBrush(ViewportThumbnailBrush,
			ViewportCapture->GetCapturedTexture(), ViewportThumbnailImage);
	}

	// Capture depth pass at matching native viewport resolution
	bool bDepthSuccess = DepthRenderer->CaptureDepth(
		Settings->MaxDepthDistance);

	if (bDepthSuccess)
	{
		UpdateThumbnailBrush(DepthThumbnailBrush,
			DepthRenderer->GetDepthTexture(), DepthThumbnailImage);
	}

	if (bColorSuccess)
	{
		FString StatusMsg = FString::Printf(TEXT("Captured %dx%d viewport"),
			ViewportCapture->GetWidth(), ViewportCapture->GetHeight());
		if (bDepthSuccess)
		{
			StatusMsg += TEXT(" + depth map");
		}
		UpdateStatusText(StatusMsg);
	}
	else
	{
		UpdateStatusText(TEXT("Failed to capture viewport - ensure an editor viewport is open"));
	}

	return FReply::Handled();
}

FReply SViewGenPanel::OnGenerateClicked()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	const EGenMode Mode = Settings->GenerationMode;

	// Validate captures based on mode
	if (Mode == EGenMode::Img2Img && !ViewportCapture->HasCapture())
	{
		UpdateStatusText(TEXT("No viewport captured - click 'Capture Viewport' first"));
		return FReply::Handled();
	}
	if (Mode == EGenMode::DepthAndPrompt && !DepthRenderer->HasCapture())
	{
		UpdateStatusText(TEXT("Depth + Prompt mode requires a depth capture - click 'Capture Viewport' first"));
		return FReply::Handled();
	}

	bIsGenerating = true;
	CurrentProgress = 0.0f;

	FString Prompt = PromptTextBox->GetText().ToString();
	FString NegativePrompt = NegativePromptTextBox->GetText().ToString();
	TArray<FLoRAEntry> ActiveLoRAs = GetActiveLoRAs();

	// Persist the user's raw prompts BEFORE camera injection so they survive
	// editor restarts without accumulating injected camera data.
	UGenAISettings* MutableSettings = UGenAISettings::Get();
	MutableSettings->DefaultPrompt = Prompt;
	MutableSettings->DefaultNegativePrompt = NegativePrompt;
	MutableSettings->SaveConfig();

	// Auto-inject camera/lens description into the prompt (for the API call only).
	// Uses [UE_CAM]...[/UE_CAM] tags to detect and replace stale camera data.
	if (Settings->bAutoCameraPrompt && ViewportCapture->HasCameraData())
	{
		static const FString CamTagOpen = TEXT("[UE_CAM]");
		static const FString CamTagClose = TEXT("[/UE_CAM]");

		FString CameraDesc = ViewportCapture->BuildCameraPromptDescription();
		if (!CameraDesc.IsEmpty())
		{
			FString TaggedCamera = CamTagOpen + CameraDesc + CamTagClose;

			int32 OpenIdx = Prompt.Find(CamTagOpen);
			int32 CloseIdx = Prompt.Find(CamTagClose);

			if (OpenIdx != INDEX_NONE && CloseIdx != INDEX_NONE && CloseIdx > OpenIdx)
			{
				// Camera data already present — replace if changed, leave if identical
				FString ExistingBlock = Prompt.Mid(OpenIdx, CloseIdx - OpenIdx + CamTagClose.Len());
				Prompt = Prompt.Replace(*ExistingBlock, *TaggedCamera);
			}
			else
			{
				// No existing camera data — prepend
				Prompt = TaggedCamera + TEXT(", ") + Prompt;
			}

			UE_LOG(LogTemp, Log, TEXT("ViewGen: Camera prompt injected: %s"), *CameraDesc);
		}
	}

	// Strip camera tags — they're only used for deduplication, not sent to the API
	Prompt.ReplaceInline(TEXT("[UE_CAM]"), TEXT(""));
	Prompt.ReplaceInline(TEXT("[/UE_CAM]"), TEXT(""));

	// Show cost estimate overlay in graph editor upper-left corner
	FString CostEstimate = EstimateGenerationCost();
	if (GraphEditor.IsValid() && !CostEstimate.IsEmpty())
	{
		GraphEditor->SetOverlayText(FString::Printf(TEXT("Est: %s"), *CostEstimate));
	}

	switch (Mode)
	{
	case EGenMode::DepthAndPrompt:
	{
		FString DepthBase64 = DepthRenderer->GetBase64PNG();
		HttpClient->SendDepthOnlyRequest(DepthBase64, Prompt, NegativePrompt, ActiveLoRAs);
		UpdateStatusText(TEXT("Generating (Depth + Prompt)..."));
		break;
	}
	case EGenMode::PromptOnly:
	{
		HttpClient->SendTxt2ImgRequest(Prompt, NegativePrompt, ActiveLoRAs);
		UpdateStatusText(TEXT("Generating (Prompt Only)..."));
		break;
	}
	case EGenMode::Gemini:
	{
		FString ViewportBase64 = ViewportCapture->HasCapture() ? ViewportCapture->GetBase64PNG() : FString();
		FString DepthBase64 = DepthRenderer->HasCapture() ? DepthRenderer->GetBase64PNG() : FString();
		HttpClient->SendGeminiRequest(ViewportBase64, DepthBase64, Prompt);
		UpdateStatusText(TEXT("Generating (Gemini Nano Banana 2)..."));
		break;
	}
	case EGenMode::Kling:
	{
		FString ViewportBase64 = ViewportCapture->HasCapture() ? ViewportCapture->GetBase64PNG() : FString();
		FString NegPrompt = NegativePromptTextBox->GetText().ToString();
		HttpClient->SendKlingRequest(ViewportBase64, Prompt, NegPrompt);
		UpdateStatusText(TEXT("Generating (Kling Image 3.0)..."));
		break;
	}
	case EGenMode::Img2Img:
	default:
	{
		FString ViewportBase64 = ViewportCapture->GetBase64PNG();
		FString DepthBase64 = DepthRenderer->HasCapture() ? DepthRenderer->GetBase64PNG() : FString();
		HttpClient->SendImg2ImgRequest(ViewportBase64, DepthBase64,
			Prompt, NegativePrompt, ActiveLoRAs);
		UpdateStatusText(TEXT("Generating (img2img)..."));
		break;
	}
	}

	// Start progress polling (polls ComfyUI /history endpoint)
	const UGenAISettings* PollSettings = UGenAISettings::Get();
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(ProgressPollTimer, FTimerDelegate::CreateLambda([this]()
		{
			HttpClient->PollProgress();
		}), PollSettings->ProgressPollInterval, true);
	}

	return FReply::Handled();
}

FReply SViewGenPanel::OnCancelClicked()
{
	HttpClient->CancelRequest();
	bIsGenerating = false;
	CurrentProgress = 0.0f;

	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(ProgressPollTimer);
	}

	UpdateStatusText(TEXT("Generation cancelled"));
	return FReply::Handled();
}

FReply SViewGenPanel::OnAddLoRAClicked()
{
	FString Path;
	if (LoRAPathCombo.IsValid())
	{
		TSharedPtr<FString> Sel = LoRAPathCombo->GetSelectedItem();
		if (Sel.IsValid())
		{
			Path = *Sel;
		}
	}

	if (Path.IsEmpty())
	{
		return FReply::Handled();
	}

	FLoRAEntry NewEntry;
	NewEntry.Name = FPaths::GetBaseFilename(Path);
	NewEntry.PathOrIdentifier = Path;
	NewEntry.Weight = 0.75f;
	NewEntry.bEnabled = true;

	UILoRAEntries.Add(NewEntry);
	LoRAPathCombo->ClearSelection();

	// Rebuild LoRA list UI
	LoRAListBox->ClearChildren();
	for (int32 i = 0; i < UILoRAEntries.Num(); i++)
	{
		const int32 Index = i;
		FLoRAEntry& Entry = UILoRAEntries[i];

		LoRAListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			// Enabled checkbox
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, Index]()
				{
					return UILoRAEntries.IsValidIndex(Index) && UILoRAEntries[Index].bEnabled
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState NewState)
				{
					if (UILoRAEntries.IsValidIndex(Index))
					{
						UILoRAEntries[Index].bEnabled = (NewState == ECheckBoxState::Checked);
					}
				})
			]

			// Name
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.Name))
			]

			// Weight spinner
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WeightLabel", "Weight:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(60.0f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.0f)
					.MaxValue(2.0f)
					.Delta(0.05f)
					.Value_Lambda([this, Index]()
					{
						return UILoRAEntries.IsValidIndex(Index) ? UILoRAEntries[Index].Weight : 0.75f;
					})
					.OnValueChanged_Lambda([this, Index](float NewValue)
					{
						if (UILoRAEntries.IsValidIndex(Index))
						{
							UILoRAEntries[Index].Weight = NewValue;
						}
					})
				]
			]

			// Remove button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveLoRA", "Remove"))
				.OnClicked(this, &SViewGenPanel::OnRemoveLoRA, Index)
			]
		];
	}

	// Force Slate to re-layout after modifying the LoRA list
	LoRAListBox->Invalidate(EInvalidateWidgetReason::Layout);

	return FReply::Handled();
}

FReply SViewGenPanel::OnRemoveLoRA(int32 Index)
{
	if (UILoRAEntries.IsValidIndex(Index))
	{
		UILoRAEntries.RemoveAt(Index);
		// Trigger a rebuild by simulating an add with empty text
		// (OnAddLoRAClicked rebuilds the list; we just need to rebuild)
		LoRAListBox->ClearChildren();

		// Re-add all entries to the UI
		TArray<FLoRAEntry> TempEntries = UILoRAEntries;
		UILoRAEntries.Empty();
		for (const FLoRAEntry& Entry : TempEntries)
		{
			UILoRAEntries.Add(Entry);
		}

		// Rebuild the visual list
		for (int32 i = 0; i < UILoRAEntries.Num(); i++)
		{
			// Trigger the LoRA list rebuild (reuse the pattern from OnAddLoRAClicked)
			// For brevity, we'll call the add function pattern
		}

		// Full rebuild by calling OnAddLoRAClicked with an already-populated list
		// Since we just need the UI to reflect UILoRAEntries, we manually rebuild:
		for (int32 i = 0; i < UILoRAEntries.Num(); i++)
		{
			const int32 Idx = i;
			FLoRAEntry& Entry = UILoRAEntries[i];

			LoRAListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, Idx]()
					{
						return UILoRAEntries.IsValidIndex(Idx) && UILoRAEntries[Idx].bEnabled
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, Idx](ECheckBoxState NewState)
					{
						if (UILoRAEntries.IsValidIndex(Idx))
						{
							UILoRAEntries[Idx].bEnabled = (NewState == ECheckBoxState::Checked);
						}
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Name))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("WeightLabel2", "Weight:"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(60.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.0f)
						.MaxValue(2.0f)
						.Delta(0.05f)
						.Value_Lambda([this, Idx]()
						{
							return UILoRAEntries.IsValidIndex(Idx) ? UILoRAEntries[Idx].Weight : 0.75f;
						})
						.OnValueChanged_Lambda([this, Idx](float NewValue)
						{
							if (UILoRAEntries.IsValidIndex(Idx))
							{
								UILoRAEntries[Idx].Weight = NewValue;
							}
						})
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveLoRA2", "Remove"))
					.OnClicked(this, &SViewGenPanel::OnRemoveLoRA, Idx)
				]
			];
		}

		// Force Slate to re-layout after modifying the LoRA list
		LoRAListBox->Invalidate(EInvalidateWidgetReason::Layout);
	}

	return FReply::Handled();
}

FReply SViewGenPanel::OnOpenSettingsClicked()
{
	bSettingsExpanded = !bSettingsExpanded;
	return FReply::Handled();
}

// ============================================================================
// Settings Panel (inline, collapsible)
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::MakeSettingsRow(const FText& Label, TSharedRef<SWidget> ValueWidget)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(140.0f)
			[
				SNew(STextBlock)
				.Text(Label)
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			ValueWidget
		];
}

void SViewGenPanel::InitSamplerSchedulerOptions()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	// All ComfyUI KSampler sampler names
	static const TCHAR* Samplers[] = {
		TEXT("euler"),
		TEXT("euler_ancestral"),
		TEXT("euler_cfg_pp"),
		TEXT("heun"),
		TEXT("heunpp2"),
		TEXT("dpm_2"),
		TEXT("dpm_2_ancestral"),
		TEXT("lms"),
		TEXT("dpm_fast"),
		TEXT("dpm_adaptive"),
		TEXT("dpmpp_2s_ancestral"),
		TEXT("dpmpp_sde"),
		TEXT("dpmpp_sde_gpu"),
		TEXT("dpmpp_2m"),
		TEXT("dpmpp_2m_sde"),
		TEXT("dpmpp_2m_sde_gpu"),
		TEXT("dpmpp_3m_sde"),
		TEXT("dpmpp_3m_sde_gpu"),
		TEXT("ddpm"),
		TEXT("lcm"),
		TEXT("ipndm"),
		TEXT("ipndm_v"),
		TEXT("deis"),
		TEXT("ddim"),
		TEXT("uni_pc"),
		TEXT("uni_pc_bh2"),
	};

	for (const TCHAR* S : Samplers)
	{
		TSharedPtr<FString> Item = MakeShareable(new FString(S));
		SamplerOptions.Add(Item);
		if (Settings->SamplerName == S)
		{
			SelectedSampler = Item;
		}
	}

	// If the saved sampler isn't in our list, add it as a custom entry
	if (!SelectedSampler.IsValid() && !Settings->SamplerName.IsEmpty())
	{
		SelectedSampler = MakeShareable(new FString(Settings->SamplerName));
		SamplerOptions.Insert(SelectedSampler, 0);
	}
	else if (!SelectedSampler.IsValid())
	{
		SelectedSampler = SamplerOptions[0];
	}

	// All ComfyUI scheduler names
	static const TCHAR* Schedulers[] = {
		TEXT("normal"),
		TEXT("karras"),
		TEXT("exponential"),
		TEXT("sgm_uniform"),
		TEXT("simple"),
		TEXT("ddim_uniform"),
		TEXT("beta"),
	};

	for (const TCHAR* S : Schedulers)
	{
		TSharedPtr<FString> Item = MakeShareable(new FString(S));
		SchedulerOptions.Add(Item);
		if (Settings->SchedulerName == S)
		{
			SelectedScheduler = Item;
		}
	}

	if (!SelectedScheduler.IsValid() && !Settings->SchedulerName.IsEmpty())
	{
		SelectedScheduler = MakeShareable(new FString(Settings->SchedulerName));
		SchedulerOptions.Insert(SelectedScheduler, 0);
	}
	else if (!SelectedScheduler.IsValid())
	{
		SelectedScheduler = SchedulerOptions[0];
	}
}

TSharedPtr<FString> SViewGenPanel::FindOrAddOption(TArray<TSharedPtr<FString>>& Options, const FString& Value)
{
	for (const auto& Opt : Options)
	{
		if (*Opt == Value)
		{
			return Opt;
		}
	}
	// Not found — add it as a custom entry at the front
	TSharedPtr<FString> NewOpt = MakeShareable(new FString(Value));
	Options.Insert(NewOpt, 0);
	return NewOpt;
}

void SViewGenPanel::FetchAndPopulateModels()
{
	if (!HttpClient.IsValid()) return;

	HttpClient->OnModelListsFetched.BindRaw(this, &SViewGenPanel::OnModelsReceived);
	HttpClient->FetchAvailableModels();
}

SViewGenPanel::EModelArch SViewGenPanel::DetectArchitecture(const FString& ModelName)
{
	FString Lower = ModelName.ToLower();

	// Flux detection — check first since some Flux models contain "xl" in their name
	if (Lower.Contains(TEXT("flux")))
	{
		return EModelArch::Flux;
	}

	// SDXL detection
	if (Lower.Contains(TEXT("sdxl")) || Lower.Contains(TEXT("sd_xl"))
		|| Lower.Contains(TEXT("_xl_")) || Lower.Contains(TEXT("-xl-"))
		|| Lower.EndsWith(TEXT("_xl.safetensors")) || Lower.EndsWith(TEXT("-xl.safetensors"))
		|| Lower.Contains(TEXT("juggernautxl")) || Lower.Contains(TEXT("realvisxl"))
		|| Lower.Contains(TEXT("dreamshaperxl")) || Lower.Contains(TEXT("proteus"))
		|| Lower.Contains(TEXT("ponyxl")) || Lower.Contains(TEXT("copax")))
	{
		return EModelArch::SDXL;
	}

	// SD 1.5 detection
	if (Lower.Contains(TEXT("v1-5")) || Lower.Contains(TEXT("v1.5")) || Lower.Contains(TEXT("sd15"))
		|| Lower.Contains(TEXT("sd1.5")) || Lower.Contains(TEXT("sd_1_5"))
		|| Lower.Contains(TEXT("v1-4")) || Lower.Contains(TEXT("v1.4"))
		|| Lower.Contains(TEXT("dreamshaper")) || Lower.Contains(TEXT("deliberate"))
		|| Lower.Contains(TEXT("realisticvision")) || Lower.Contains(TEXT("revanimated"))
		|| Lower.Contains(TEXT("control_v11")) || Lower.Contains(TEXT("control_sd15"))
		|| Lower.Contains(TEXT("t2i-adapter")))
	{
		return EModelArch::SD15;
	}

	return EModelArch::Unknown;
}

bool SViewGenPanel::IsCompatibleWithMode(const FString& ModelName, bool bFluxMode)
{
	EModelArch Arch = DetectArchitecture(ModelName);

	if (Arch == EModelArch::Unknown)
	{
		// Don't hide unrecognized models — always show them
		return true;
	}

	if (bFluxMode)
	{
		return Arch == EModelArch::Flux;
	}
	else
	{
		// SD mode: show SD 1.5 and SDXL, hide Flux
		return Arch != EModelArch::Flux;
	}
}

void SViewGenPanel::FilterModelsForCurrentMode()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	const bool bFluxMode = Settings->bUseFluxControlNet;

	// ---- Checkpoints: always show all (user explicitly picks) ----
	CheckpointOptions.Empty();
	for (const FString& Name : AllCheckpoints)
	{
		CheckpointOptions.Add(MakeShareable(new FString(Name)));
	}
	SelectedCheckpoint = FindOrAddOption(CheckpointOptions, Settings->CheckpointName);
	if (CheckpointCombo.IsValid())
	{
		CheckpointCombo->RefreshOptions();
		CheckpointCombo->SetSelectedItem(SelectedCheckpoint);
	}

	// ---- LoRAs: filter by architecture ----
	LoRAModelOptions.Empty();
	for (const FString& Name : AllLoRAs)
	{
		if (IsCompatibleWithMode(Name, bFluxMode))
		{
			LoRAModelOptions.Add(MakeShareable(new FString(Name)));
		}
	}
	if (LoRAPathCombo.IsValid())
	{
		LoRAPathCombo->RefreshOptions();
	}

	// ---- ControlNets: filter by architecture ----
	ControlNetOptions.Empty();
	for (const FString& Name : AllControlNets)
	{
		if (IsCompatibleWithMode(Name, bFluxMode))
		{
			ControlNetOptions.Add(MakeShareable(new FString(Name)));
		}
	}
	SelectedControlNet = FindOrAddOption(ControlNetOptions, Settings->ControlNetModel);
	if (ControlNetCombo.IsValid())
	{
		ControlNetCombo->RefreshOptions();
		ControlNetCombo->SetSelectedItem(SelectedControlNet);
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Filtered for %s — showing %d LoRAs, %d ControlNets (from %d / %d total)"),
		bFluxMode ? TEXT("Flux") : TEXT("SD"),
		LoRAModelOptions.Num(), ControlNetOptions.Num(),
		AllLoRAs.Num(), AllControlNets.Num());
}

void SViewGenPanel::OnModelsReceived(const TArray<FString>& Checkpoints, const TArray<FString>& LoRAs, const TArray<FString>& ControlNets, const FGeminiNodeOptions& GeminiOptions, const FKlingNodeOptions& KlingOptions)
{
	// Store raw lists for re-filtering when mode changes
	AllCheckpoints = Checkpoints;
	AllLoRAs = LoRAs;
	AllControlNets = ControlNets;

	// Apply architecture-based filtering
	FilterModelsForCurrentMode();

	// Populate Gemini dropdowns from ComfyUI node info
	PopulateGeminiDropdowns(GeminiOptions);

	// Populate Kling dropdowns from ComfyUI node info
	PopulateKlingDropdowns(KlingOptions);

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Model dropdowns updated — %d checkpoints, %d LoRAs, %d ControlNets, %d Gemini models"),
		AllCheckpoints.Num(), AllLoRAs.Num(), AllControlNets.Num(), GeminiModelOptions.Num());
}

void SViewGenPanel::PopulateGeminiDropdowns(const FGeminiNodeOptions& Options)
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	auto PopulateCombo = [this](
		const TArray<FString>& Source,
		TArray<TSharedPtr<FString>>& OptionArray,
		TSharedPtr<FString>& Selected,
		TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo,
		const FString& CurrentValue)
	{
		OptionArray.Empty();
		for (const FString& Item : Source)
		{
			OptionArray.Add(MakeShareable(new FString(Item)));
		}
		Selected = FindOrAddOption(OptionArray, CurrentValue);
		if (Combo.IsValid())
		{
			Combo->RefreshOptions();
			Combo->SetSelectedItem(Selected);
		}
	};

	PopulateCombo(Options.Models, GeminiModelOptions, SelectedGeminiModel, GeminiModelCombo, Settings->GeminiModelName);
	PopulateCombo(Options.AspectRatios, GeminiAspectOptions, SelectedGeminiAspect, GeminiAspectCombo, Settings->GeminiAspectRatio);
	PopulateCombo(Options.Resolutions, GeminiResolutionOptions, SelectedGeminiResolution, GeminiResolutionCombo, Settings->GeminiResolution);
	PopulateCombo(Options.ResponseModalities, GeminiModalityOptions, SelectedGeminiModality, GeminiModalityCombo, Settings->GeminiResponseModalities);
	PopulateCombo(Options.ThinkingLevels, GeminiThinkingOptions, SelectedGeminiThinking, GeminiThinkingCombo, Settings->GeminiThinkingLevel);
}

void SViewGenPanel::PopulateKlingDropdowns(const FKlingNodeOptions& Options)
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	auto Populate = [this](TArray<TSharedPtr<FString>>& Arr, const TArray<FString>& Src, const FString& CurrentVal,
		TSharedPtr<FString>& Selected, TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo)
	{
		Arr.Empty();
		for (const FString& S : Src) Arr.Add(MakeShareable(new FString(S)));
		Selected = FindOrAddOption(Arr, CurrentVal);
		if (Combo.IsValid()) Combo->RefreshOptions();
		if (Combo.IsValid()) Combo->SetSelectedItem(Selected);
	};

	Populate(KlingModelOptions, Options.Models, Settings->KlingModelName, SelectedKlingModel, KlingModelCombo);
	Populate(KlingAspectOptions, Options.AspectRatios, Settings->KlingAspectRatio, SelectedKlingAspect, KlingAspectCombo);
	Populate(KlingImageTypeOptions, Options.ImageTypes, Settings->KlingImageType, SelectedKlingImageType, KlingImageTypeCombo);
}

TSharedRef<SWidget> SViewGenPanel::BuildCollapsibleSection(const FText& Title, bool& bExpandedFlag, TSharedRef<SWidget> Content)
{
	return SNew(SVerticalBox)

		// Section header toggle
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([&bExpandedFlag]() -> FReply
			{
				bExpandedFlag = !bExpandedFlag;
				return FReply::Handled();
			})
			.Content()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([&bExpandedFlag]()
					{
						return bExpandedFlag
							? FText::FromString(TEXT("\x25BC"))   // ▼
							: FText::FromString(TEXT("\x25B6"));  // ▶
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Title)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]
			]
		]

		// Section body (collapsible)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f, 2.0f, 0.0f, 4.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([&bExpandedFlag]()
			{
				return bExpandedFlag ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				Content
			]
		];
}

TSharedRef<SWidget> SViewGenPanel::BuildSettingsPanel()
{
	UGenAISettings* Settings = UGenAISettings::Get();

	// ---- Connection section content ----
	TSharedRef<SWidget> ConnectionContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("ComfyURLLabel", "ComfyUI URL"),
				SAssignNew(ComfyUIURLInput, SEditableTextBox)
				.Text(FText::FromString(Settings->APIEndpointURL))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->APIEndpointURL = NewText.ToString();
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("TimeoutLabel", "Timeout (sec)"),
				SNew(SSpinBox<float>)
				.MinValue(5.0f)
				.MaxValue(600.0f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->TimeoutSeconds; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->TimeoutSeconds = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->TimeoutSeconds = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("ApiKeyLabel", "ComfyUI API Key"),
				SAssignNew(ComfyUIApiKeyInput, SEditableTextBox)
				.Text(FText::FromString(Settings->ComfyUIApiKey))
				.IsPassword(true)
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->ComfyUIApiKey = NewText.ToString();
					ApplySettingsToConfig();
				})
				.ToolTipText(LOCTEXT("ApiKeyTip",
					"ComfyUI Account API Key for partner API nodes (Gemini, etc.).\n"
					"Generate at: https://platform.comfy.org\n"
					"Required for Nano Banana 2 and other API nodes."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("MeshyApiKeyLabel", "Meshy API Key"),
				SAssignNew(MeshyApiKeyInput, SEditableTextBox)
				.Text(FText::FromString(Settings->MeshyApiKey))
				.IsPassword(true)
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->MeshyApiKey = NewText.ToString();
					ApplySettingsToConfig();
				})
				.ToolTipText(LOCTEXT("MeshyApiKeyTip",
					"Meshy API Key for Image-to-3D conversion.\n"
					"Get yours at: https://www.meshy.ai/api\n"
					"Format: msy-XXXX"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			)
		];

	// ---- Model section content ----
	TSharedRef<SWidget> ModelContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeSettingsRow(
					LOCTEXT("CheckpointLabel", "Checkpoint"),
					SAssignNew(CheckpointCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&CheckpointOptions)
					.InitiallySelectedItem(SelectedCheckpoint)
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
					{
						if (NewValue.IsValid())
						{
							SelectedCheckpoint = NewValue;
							UGenAISettings::Get()->CheckpointName = *NewValue;
							ApplySettingsToConfig();
						}
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock)
							.Text(FText::FromString(*Item))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
					})
					.Content()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return SelectedCheckpoint.IsValid()
								? FText::FromString(*SelectedCheckpoint)
								: FText::GetEmpty();
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshModels", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshModelsTooltip", "Re-fetch available models from ComfyUI"))
				.OnClicked_Lambda([this]() -> FReply
				{
					FetchAndPopulateModels();
					return FReply::Handled();
				})
			]
		];

	// ---- Generation section content ----
	TSharedRef<SWidget> GenerationContent = SNew(SVerticalBox)

		// Generation Mode dropdown
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GenModeLabel", "Mode"),
				SAssignNew(GenModeCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&GenModeOptions)
				.InitiallySelectedItem(SelectedGenMode)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					if (NewValue.IsValid())
					{
						SelectedGenMode = NewValue;
						int32 Index = GenModeOptions.IndexOfByKey(NewValue);
						if (Index != INDEX_NONE)
						{
							UGenAISettings::Get()->GenerationMode = static_cast<EGenMode>(Index);
							ApplySettingsToConfig();
						}
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedGenMode.IsValid()
							? FText::FromString(*SelectedGenMode)
							: FText::FromString(TEXT("img2img (Viewport + Prompt)"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("WidthLabel", "Output Width"),
				SNew(SSpinBox<int32>)
				.MinValue(64)
				.MaxValue(2048)
				.Delta(64)
				.Value_Lambda([this]() { return UGenAISettings::Get()->OutputWidth; })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->OutputWidth = NewVal;
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->OutputWidth = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("HeightLabel", "Output Height"),
				SNew(SSpinBox<int32>)
				.MinValue(64)
				.MaxValue(2048)
				.Delta(64)
				.Value_Lambda([this]() { return UGenAISettings::Get()->OutputHeight; })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->OutputHeight = NewVal;
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->OutputHeight = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("DenoiseLabel", "Denoising Strength"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Delta(0.05f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->DenoisingStrength; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->DenoisingStrength = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->DenoisingStrength = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("StepsLabel", "Steps"),
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(150)
				.Value_Lambda([this]() { return UGenAISettings::Get()->Steps; })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->Steps = NewVal;
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->Steps = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("CFGLabel", "CFG Scale"),
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(30.0f)
				.Delta(0.5f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->CFGScale; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->CFGScale = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->CFGScale = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("SeedLabel", "Seed (0=random)"),
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(2147483647)
				.Value_Lambda([this]() { return static_cast<int32>(UGenAISettings::Get()->Seed); })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->Seed = static_cast<int64>(NewVal);
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->Seed = static_cast<int64>(NewVal);
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("SamplerLabel", "Sampler"),
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&SamplerOptions)
				.InitiallySelectedItem(SelectedSampler)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					if (NewValue.IsValid())
					{
						SelectedSampler = NewValue;
						UGenAISettings::Get()->SamplerName = *NewValue;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedSampler.IsValid()
							? FText::FromString(*SelectedSampler)
							: FText::FromString(TEXT("euler_ancestral"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("SchedulerLabel", "Scheduler"),
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&SchedulerOptions)
				.InitiallySelectedItem(SelectedScheduler)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					if (NewValue.IsValid())
					{
						SelectedScheduler = NewValue;
						UGenAISettings::Get()->SchedulerName = *NewValue;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedScheduler.IsValid()
							? FText::FromString(*SelectedScheduler)
							: FText::FromString(TEXT("normal"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("AutoCameraPromptLabel", "Auto Camera Prompt"),
				SNew(SCheckBox)
				.IsChecked_Lambda([]()
				{
					return UGenAISettings::Get()->bAutoCameraPrompt
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					UGenAISettings::Get()->bAutoCameraPrompt = (NewState == ECheckBoxState::Checked);
					ApplySettingsToConfig();
				})
				.ToolTipText(LOCTEXT("AutoCameraPromptTip",
					"Automatically prepend camera lens and framing info to the prompt\n"
					"based on the viewport camera at capture time (FOV, angle, height)."))
			)
		];

	// ---- Depth / ControlNet section content ----
	TSharedRef<SWidget> DepthContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("EnableDepthLabel", "Enable Depth ControlNet"),
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return UGenAISettings::Get()->bEnableDepthControlNet
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					UGenAISettings::Get()->bEnableDepthControlNet = (NewState == ECheckBoxState::Checked);
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("UseFluxCNLabel", "Use Flux ControlNet (XLabs)"),
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return UGenAISettings::Get()->bUseFluxControlNet
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					UGenAISettings::Get()->bUseFluxControlNet = (NewState == ECheckBoxState::Checked);
					ApplySettingsToConfig();
					// Re-filter model dropdowns for the new architecture mode
					FilterModelsForCurrentMode();
				})
				.ToolTipText(LOCTEXT("UseFluxCNTip",
					"Enable for Flux models with XLabs ControlNet.\n"
					"Requires x-flux-comfyui custom nodes installed in ComfyUI.\n"
					"Uses LoadFluxControlNet + ApplyFluxControlNet + XlabsSampler."))
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("FluxModelNameLabel", "Flux Model Name"),
				SAssignNew(FluxModelNameInput, SEditableTextBox)
				.Text(FText::FromString(Settings->FluxModelName))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->FluxModelName = NewText.ToString();
					ApplySettingsToConfig();
				})
				.ToolTipText(LOCTEXT("FluxModelNameTip",
					"Flux variant: flux-dev, flux-dev-fp8, or flux-schnell"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("ControlNetModelLabel", "ControlNet Model"),
				SAssignNew(ControlNetCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ControlNetOptions)
				.InitiallySelectedItem(SelectedControlNet)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					if (NewValue.IsValid())
					{
						SelectedControlNet = NewValue;
						UGenAISettings::Get()->ControlNetModel = *NewValue;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				.Content()
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return SelectedControlNet.IsValid()
							? FText::FromString(*SelectedControlNet)
							: FText::GetEmpty();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("ControlNetStrengthLabel", "ControlNet Strength"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(2.0f)
				.Delta(0.05f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->ControlNetWeight; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->ControlNetWeight = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->ControlNetWeight = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("MaxDepthLabel", "Max Depth Distance (UU)"),
				SNew(SSpinBox<float>)
				.MinValue(100.0f)
				.MaxValue(500000.0f)
				.Delta(1000.0f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->MaxDepthDistance; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->MaxDepthDistance = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->MaxDepthDistance = NewVal;
					ApplySettingsToConfig();
				})
			)
		];

	// ---- Hi-Res Fix section content ----
	TSharedRef<SWidget> HiResContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("EnableHiResLabel", "Enable Hi-Res Fix"),
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return UGenAISettings::Get()->bEnableHiResFix
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					UGenAISettings::Get()->bEnableHiResFix = (NewState == ECheckBoxState::Checked);
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("HiResUpscaleLabel", "Upscale Factor"),
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(4.0f)
				.Delta(0.25f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->HiResUpscaleFactor; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->HiResUpscaleFactor = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->HiResUpscaleFactor = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("HiResDenoiseLabel", "Hi-Res Denoise"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Delta(0.05f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->HiResDenoise; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->HiResDenoise = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->HiResDenoise = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("HiResStepsLabel", "Hi-Res Steps"),
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(150)
				.Value_Lambda([this]() { return UGenAISettings::Get()->HiResSteps; })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->HiResSteps = NewVal;
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->HiResSteps = NewVal;
					ApplySettingsToConfig();
				})
			)
		];

	// ---- Gemini (Nano Banana 2) section content ----
	// Helper lambda to build a Gemini combo box row
	auto MakeGeminiCombo = [this](
		TSharedPtr<SComboBox<TSharedPtr<FString>>>& OutCombo,
		TArray<TSharedPtr<FString>>& OptionsSource,
		TSharedPtr<FString>& SelectedItem,
		TFunction<void(const FString&)> OnChanged) -> TSharedRef<SWidget>
	{
		return SAssignNew(OutCombo, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&OptionsSource)
			.InitiallySelectedItem(SelectedItem)
			.OnSelectionChanged_Lambda([this, &SelectedItem, OnChanged](TSharedPtr<FString> NewValue, ESelectInfo::Type)
			{
				if (NewValue.IsValid())
				{
					SelectedItem = NewValue;
					OnChanged(*NewValue);
					ApplySettingsToConfig();
				}
			})
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
			{
				return SNew(STextBlock)
					.Text(FText::FromString(*Item))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([&SelectedItem]()
				{
					return SelectedItem.IsValid()
						? FText::FromString(*SelectedItem)
						: FText::GetEmpty();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			];
	};

	TSharedRef<SWidget> GeminiContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiModelLabel", "Model"),
				MakeGeminiCombo(GeminiModelCombo, GeminiModelOptions, SelectedGeminiModel,
					[](const FString& Val) { UGenAISettings::Get()->GeminiModelName = Val; })
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiAspectLabel", "Aspect Ratio"),
				MakeGeminiCombo(GeminiAspectCombo, GeminiAspectOptions, SelectedGeminiAspect,
					[](const FString& Val) { UGenAISettings::Get()->GeminiAspectRatio = Val; })
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiResLabel", "Resolution"),
				MakeGeminiCombo(GeminiResolutionCombo, GeminiResolutionOptions, SelectedGeminiResolution,
					[](const FString& Val) { UGenAISettings::Get()->GeminiResolution = Val; })
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiModalitiesLabel", "Response Modalities"),
				MakeGeminiCombo(GeminiModalityCombo, GeminiModalityOptions, SelectedGeminiModality,
					[](const FString& Val) { UGenAISettings::Get()->GeminiResponseModalities = Val; })
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiThinkingLabel", "Thinking Level"),
				MakeGeminiCombo(GeminiThinkingCombo, GeminiThinkingOptions, SelectedGeminiThinking,
					[](const FString& Val) { UGenAISettings::Get()->GeminiThinkingLevel = Val; })
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiSeedLabel", "Seed (0=random)"),
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(2147483647)
				.Value_Lambda([this]() { return static_cast<int32>(UGenAISettings::Get()->GeminiSeed); })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->GeminiSeed = static_cast<int64>(NewVal);
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->GeminiSeed = static_cast<int64>(NewVal);
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("GeminiSysPromptLabel", "System Prompt"),
				SAssignNew(GeminiSystemPromptInput, SEditableTextBox)
				.Text(FText::FromString(Settings->GeminiSystemPrompt))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					UGenAISettings::Get()->GeminiSystemPrompt = NewText.ToString();
					ApplySettingsToConfig();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			)
		];

	// ---- Kling section content ----
	TSharedRef<SWidget> KlingContent = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingModelLabel", "Model"),
				SAssignNew(KlingModelCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&KlingModelOptions)
				.InitiallySelectedItem(SelectedKlingModel)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewVal, ESelectInfo::Type) {
					if (NewVal.IsValid()) {
						SelectedKlingModel = NewVal;
						UGenAISettings::Get()->KlingModelName = *NewVal;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget> {
					return SNew(STextBlock).Text(FText::FromString(*Item));
				})
				[
					SNew(STextBlock).Text_Lambda([this]() {
						return SelectedKlingModel.IsValid() ? FText::FromString(*SelectedKlingModel) : FText::GetEmpty();
					})
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingAspectLabel", "Aspect Ratio"),
				SAssignNew(KlingAspectCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&KlingAspectOptions)
				.InitiallySelectedItem(SelectedKlingAspect)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewVal, ESelectInfo::Type) {
					if (NewVal.IsValid()) {
						SelectedKlingAspect = NewVal;
						UGenAISettings::Get()->KlingAspectRatio = *NewVal;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget> {
					return SNew(STextBlock).Text(FText::FromString(*Item));
				})
				[
					SNew(STextBlock).Text_Lambda([this]() {
						return SelectedKlingAspect.IsValid() ? FText::FromString(*SelectedKlingAspect) : FText::GetEmpty();
					})
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingImageTypeLabel", "Image Type"),
				SAssignNew(KlingImageTypeCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&KlingImageTypeOptions)
				.InitiallySelectedItem(SelectedKlingImageType)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewVal, ESelectInfo::Type) {
					if (NewVal.IsValid()) {
						SelectedKlingImageType = NewVal;
						UGenAISettings::Get()->KlingImageType = *NewVal;
						ApplySettingsToConfig();
					}
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget> {
					return SNew(STextBlock).Text(FText::FromString(*Item));
				})
				[
					SNew(STextBlock).Text_Lambda([this]() {
						return SelectedKlingImageType.IsValid() ? FText::FromString(*SelectedKlingImageType) : FText::GetEmpty();
					})
				]
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingImageFidelityLabel", "Image Fidelity (0-1)"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Delta(0.01f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->KlingImageFidelity; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->KlingImageFidelity = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->KlingImageFidelity = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingHumanFidelityLabel", "Human Fidelity (0-1)"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Delta(0.01f)
				.Value_Lambda([this]() { return UGenAISettings::Get()->KlingHumanFidelity; })
				.OnValueChanged_Lambda([this](float NewVal)
				{
					UGenAISettings::Get()->KlingHumanFidelity = NewVal;
				})
				.OnValueCommitted_Lambda([this](float NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->KlingHumanFidelity = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingImageCountLabel", "Image Count (1-9)"),
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(9)
				.Value_Lambda([this]() { return UGenAISettings::Get()->KlingImageCount; })
				.OnValueChanged_Lambda([this](int32 NewVal)
				{
					UGenAISettings::Get()->KlingImageCount = NewVal;
				})
				.OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->KlingImageCount = NewVal;
					ApplySettingsToConfig();
				})
			)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			MakeSettingsRow(
				LOCTEXT("KlingSeedLabel", "Seed (min 0)"),
				SNew(SSpinBox<int64>)
				.MinValue(0)
				.MaxValue(9223372036854775807LL)
				.Value_Lambda([this]() { return UGenAISettings::Get()->KlingSeed; })
				.OnValueChanged_Lambda([this](int64 NewVal)
				{
					UGenAISettings::Get()->KlingSeed = NewVal;
				})
				.OnValueCommitted_Lambda([this](int64 NewVal, ETextCommit::Type)
				{
					UGenAISettings::Get()->KlingSeed = NewVal;
					ApplySettingsToConfig();
				})
			)
		];

	// ---- Assemble the full settings panel ----
	return SNew(SVerticalBox)

		// Main "Settings" header row: toggle + Defaults button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this]() -> FReply
				{
					bSettingsExpanded = !bSettingsExpanded;
					return FReply::Handled();
				})
				.Content()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return bSettingsExpanded
								? FText::FromString(TEXT("\x25BC"))   // ▼
								: FText::FromString(TEXT("\x25B6"));  // ▶
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SettingsHeader", "Settings"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ResetDefaults", "Defaults"))
				.ToolTipText(LOCTEXT("ResetDefaultsTip", "Reset all settings to their default values"))
				.OnClicked_Lambda([this]() -> FReply
				{
					ResetSettingsToDefaults();
					return FReply::Handled();
				})
				.Visibility_Lambda([this]()
				{
					return bSettingsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		]

		// Presets row: [dropdown] [name input] [Save] [Load] [Delete]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return bSettingsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SHorizontalBox)

				// Preset dropdown
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SAssignNew(PresetCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&PresetOptions)
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type SelectType)
					{
						SelectedPreset = NewValue;
						// Fill the name input with the selected preset for easy overwrite
						if (NewValue.IsValid() && PresetNameInput.IsValid())
						{
							PresetNameInput->SetText(FText::FromString(*NewValue));
						}
						// Auto-load when user selects a preset from the dropdown
						if (SelectType != ESelectInfo::Direct && NewValue.IsValid() && !NewValue->IsEmpty())
						{
							LoadSelectedPreset();
						}
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock)
							.Text(FText::FromString(*Item))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
					})
					.Content()
					[
						SNew(STextBlock)
						.Text_Lambda([this]() -> FText
						{
							return SelectedPreset.IsValid()
								? FText::FromString(*SelectedPreset)
								: LOCTEXT("NoPreset", "Select preset...");
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]

				// Preset name text input
				+ SHorizontalBox::Slot()
				.FillWidth(0.35f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SAssignNew(PresetNameInput, SEditableTextBox)
					.HintText(LOCTEXT("PresetNameHint", "Preset name..."))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]

				// Save button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SavePreset", "Save"))
					.ToolTipText(LOCTEXT("SavePresetTip", "Save current settings as a preset"))
					.OnClicked_Lambda([this]() -> FReply
					{
						SaveCurrentPreset();
						return FReply::Handled();
					})
				]

				// Load button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("LoadPreset", "Load"))
					.ToolTipText(LOCTEXT("LoadPresetTip", "Load the selected preset"))
					.OnClicked_Lambda([this]() -> FReply
					{
						LoadSelectedPreset();
						return FReply::Handled();
					})
					.IsEnabled_Lambda([this]() -> bool
					{
						return SelectedPreset.IsValid() && !SelectedPreset->IsEmpty();
					})
				]

				// Delete button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("DeletePreset", "Del"))
					.ToolTipText(LOCTEXT("DeletePresetTip", "Delete the selected preset"))
					.OnClicked_Lambda([this]() -> FReply
					{
						DeleteSelectedPreset();
						return FReply::Handled();
					})
					.IsEnabled_Lambda([this]() -> bool
					{
						return SelectedPreset.IsValid() && !SelectedPreset->IsEmpty();
					})
				]
			]
		]

		// Collapsible settings body wrapped in a scroll box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(400.0f)
		.Padding(8.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return bSettingsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)

				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildCollapsibleSection(
							LOCTEXT("ConnectionSection", "Connection"),
							bConnectionExpanded,
							ConnectionContent)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						BuildCollapsibleSection(
							LOCTEXT("ModelSection", "Model"),
							bModelExpanded,
							ModelContent)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						BuildCollapsibleSection(
							LOCTEXT("GenerationSection", "Generation"),
							bGenerationExpanded,
							GenerationContent)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						BuildCollapsibleSection(
							LOCTEXT("DepthSection", "Depth / ControlNet"),
							bDepthExpanded,
							DepthContent)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						BuildCollapsibleSection(
							LOCTEXT("HiResSection", "Hi-Res Fix"),
							bHiResExpanded,
							HiResContent)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						BuildCollapsibleSection(
							LOCTEXT("GeminiSection", "Gemini (Nano Banana 2)"),
							bGeminiExpanded,
							GeminiContent)
					]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					BuildCollapsibleSection(
						LOCTEXT("KlingSection", "Kling (Image 3.0)"),
						bKlingExpanded,
						KlingContent)
				]
				]
			]
		];
}

void SViewGenPanel::ResetSettingsToDefaults()
{
	UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings) return;

	// Use a temporary CDO to get compiled default values
	const UGenAISettings* Defaults = GetDefault<UGenAISettings>();

	// Connection (keep URL as-is since it's environment-specific, but reset timeout)
	Settings->TimeoutSeconds = Defaults->TimeoutSeconds;
	Settings->ProgressPollInterval = Defaults->ProgressPollInterval;
	// Don't reset ComfyUIApiKey or MeshyApiKey — user would have to re-enter them

	// Model
	Settings->CheckpointName = Defaults->CheckpointName;

	// Reference Adherence
	Settings->ReferenceAdherence = Defaults->ReferenceAdherence;

	// Generation
	Settings->GenerationMode = Defaults->GenerationMode;
	Settings->OutputWidth = Defaults->OutputWidth;
	Settings->OutputHeight = Defaults->OutputHeight;
	Settings->DenoisingStrength = Defaults->DenoisingStrength;
	Settings->Steps = Defaults->Steps;
	Settings->CFGScale = Defaults->CFGScale;
	Settings->Seed = Defaults->Seed;
	Settings->SamplerName = Defaults->SamplerName;
	Settings->SchedulerName = Defaults->SchedulerName;
	Settings->bAutoCameraPrompt = Defaults->bAutoCameraPrompt;

	// Depth / ControlNet
	Settings->bEnableDepthControlNet = Defaults->bEnableDepthControlNet;
	Settings->bUseFluxControlNet = Defaults->bUseFluxControlNet;
	Settings->FluxModelName = Defaults->FluxModelName;
	Settings->FluxWeightDtype = Defaults->FluxWeightDtype;
	Settings->FluxCLIPName1 = Defaults->FluxCLIPName1;
	Settings->FluxCLIPName2 = Defaults->FluxCLIPName2;
	Settings->FluxCLIPType = Defaults->FluxCLIPType;
	Settings->FluxVAEName = Defaults->FluxVAEName;
	Settings->ControlNetModel = Defaults->ControlNetModel;
	Settings->ControlNetWeight = Defaults->ControlNetWeight;
	Settings->MaxDepthDistance = Defaults->MaxDepthDistance;

	// Hi-Res Fix
	Settings->bEnableHiResFix = Defaults->bEnableHiResFix;
	Settings->HiResUpscaleFactor = Defaults->HiResUpscaleFactor;
	Settings->HiResDenoise = Defaults->HiResDenoise;
	Settings->HiResSteps = Defaults->HiResSteps;

	// Gemini
	Settings->GeminiModelName = Defaults->GeminiModelName;
	Settings->GeminiAspectRatio = Defaults->GeminiAspectRatio;
	Settings->GeminiResolution = Defaults->GeminiResolution;
	Settings->GeminiResponseModalities = Defaults->GeminiResponseModalities;
	Settings->GeminiThinkingLevel = Defaults->GeminiThinkingLevel;
	Settings->GeminiSeed = Defaults->GeminiSeed;
	Settings->GeminiSystemPrompt = Defaults->GeminiSystemPrompt;

	// Kling
	Settings->KlingModelName = Defaults->KlingModelName;
	Settings->KlingAspectRatio = Defaults->KlingAspectRatio;
	Settings->KlingImageType = Defaults->KlingImageType;
	Settings->KlingImageFidelity = Defaults->KlingImageFidelity;
	Settings->KlingHumanFidelity = Defaults->KlingHumanFidelity;
	Settings->KlingImageCount = Defaults->KlingImageCount;
	Settings->KlingSeed = Defaults->KlingSeed;

	// Video
	Settings->VideoMode = Defaults->VideoMode;
	Settings->VideoMotionAdherence = Defaults->VideoMotionAdherence;
	Settings->VideoPrompt = Defaults->VideoPrompt;
	Settings->VideoNegativePrompt = Defaults->VideoNegativePrompt;
	Settings->VideoDuration = Defaults->VideoDuration;
	Settings->VideoFPS = Defaults->VideoFPS;
	Settings->VideoCFG = Defaults->VideoCFG;
	Settings->VideoSteps = Defaults->VideoSteps;
	Settings->VideoSeed = Defaults->VideoSeed;
	Settings->KlingVideoModel = Defaults->KlingVideoModel;
	Settings->KlingVideoQuality = Defaults->KlingVideoQuality;
	Settings->WanModelName = Defaults->WanModelName;
	Settings->Veo3ModelName = Defaults->Veo3ModelName;
	Settings->Veo3AspectRatio = Defaults->Veo3AspectRatio;
	Settings->bVeo3GenerateAudio = Defaults->bVeo3GenerateAudio;
	Settings->Veo3PersonGeneration = Defaults->Veo3PersonGeneration;

	// Prompts
	Settings->DefaultPrompt = Defaults->DefaultPrompt;
	Settings->DefaultNegativePrompt = Defaults->DefaultNegativePrompt;

	// LoRAs
	Settings->LoRAModels = Defaults->LoRAModels;

	Settings->SaveConfig();

	RefreshUIFromSettings();
	UE_LOG(LogTemp, Log, TEXT("ViewGen: Settings reset to defaults"));
}

void SViewGenPanel::RefreshUIFromSettings()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings) return;

	// Generation mode dropdown
	{
		int32 ModeIndex = FMath::Clamp(static_cast<int32>(Settings->GenerationMode), 0, GenModeOptions.Num() - 1);
		SelectedGenMode = GenModeOptions[ModeIndex];
		if (GenModeCombo.IsValid()) GenModeCombo->SetSelectedItem(SelectedGenMode);
	}

	// Sampler / Scheduler
	SelectedSampler = FindOrAddOption(SamplerOptions, Settings->SamplerName);
	SelectedScheduler = FindOrAddOption(SchedulerOptions, Settings->SchedulerName);

	// Checkpoint / ControlNet (re-filter also handles selection)
	FilterModelsForCurrentMode();

	// Gemini combos
	SelectedGeminiModel = FindOrAddOption(GeminiModelOptions, Settings->GeminiModelName);
	if (GeminiModelCombo.IsValid()) GeminiModelCombo->SetSelectedItem(SelectedGeminiModel);
	SelectedGeminiAspect = FindOrAddOption(GeminiAspectOptions, Settings->GeminiAspectRatio);
	if (GeminiAspectCombo.IsValid()) GeminiAspectCombo->SetSelectedItem(SelectedGeminiAspect);
	SelectedGeminiResolution = FindOrAddOption(GeminiResolutionOptions, Settings->GeminiResolution);
	if (GeminiResolutionCombo.IsValid()) GeminiResolutionCombo->SetSelectedItem(SelectedGeminiResolution);
	SelectedGeminiModality = FindOrAddOption(GeminiModalityOptions, Settings->GeminiResponseModalities);
	if (GeminiModalityCombo.IsValid()) GeminiModalityCombo->SetSelectedItem(SelectedGeminiModality);
	SelectedGeminiThinking = FindOrAddOption(GeminiThinkingOptions, Settings->GeminiThinkingLevel);
	if (GeminiThinkingCombo.IsValid()) GeminiThinkingCombo->SetSelectedItem(SelectedGeminiThinking);

	// Kling combos
	SelectedKlingModel = FindOrAddOption(KlingModelOptions, Settings->KlingModelName);
	if (KlingModelCombo.IsValid()) KlingModelCombo->SetSelectedItem(SelectedKlingModel);
	SelectedKlingAspect = FindOrAddOption(KlingAspectOptions, Settings->KlingAspectRatio);
	if (KlingAspectCombo.IsValid()) KlingAspectCombo->SetSelectedItem(SelectedKlingAspect);
	SelectedKlingImageType = FindOrAddOption(KlingImageTypeOptions, Settings->KlingImageType);
	if (KlingImageTypeCombo.IsValid()) KlingImageTypeCombo->SetSelectedItem(SelectedKlingImageType);

	// Prompt text boxes
	if (PromptTextBox.IsValid()) PromptTextBox->SetText(FText::FromString(Settings->DefaultPrompt));
	if (NegativePromptTextBox.IsValid()) NegativePromptTextBox->SetText(FText::FromString(Settings->DefaultNegativePrompt));

	// Text input fields
	if (ComfyUIURLInput.IsValid()) ComfyUIURLInput->SetText(FText::FromString(Settings->APIEndpointURL));
	if (ComfyUIApiKeyInput.IsValid()) ComfyUIApiKeyInput->SetText(FText::FromString(Settings->ComfyUIApiKey));
	if (MeshyApiKeyInput.IsValid()) MeshyApiKeyInput->SetText(FText::FromString(Settings->MeshyApiKey));
	if (FluxModelNameInput.IsValid()) FluxModelNameInput->SetText(FText::FromString(Settings->FluxModelName));
	if (GeminiSystemPromptInput.IsValid()) GeminiSystemPromptInput->SetText(FText::FromString(Settings->GeminiSystemPrompt));

	// LoRA list — update internal state and rebuild the visual list
	UILoRAEntries = Settings->LoRAModels;
	if (LoRAListBox.IsValid())
	{
		LoRAListBox->ClearChildren();
		for (int32 i = 0; i < UILoRAEntries.Num(); ++i)
		{
			const FLoRAEntry& Entry = UILoRAEntries[i];
			int32 Index = i;

			LoRAListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, Index]()
					{
						return UILoRAEntries.IsValidIndex(Index) && UILoRAEntries[Index].bEnabled
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState NewState)
					{
						if (UILoRAEntries.IsValidIndex(Index))
						{
							UILoRAEntries[Index].bEnabled = (NewState == ECheckBoxState::Checked);
						}
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Name))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Weight:")))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(60.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.0f)
						.MaxValue(2.0f)
						.Delta(0.05f)
						.Value_Lambda([this, Index]()
						{
							return UILoRAEntries.IsValidIndex(Index) ? UILoRAEntries[Index].Weight : 0.75f;
						})
						.OnValueChanged_Lambda([this, Index](float NewValue)
						{
							if (UILoRAEntries.IsValidIndex(Index))
							{
								UILoRAEntries[Index].Weight = NewValue;
							}
						})
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Remove")))
					.OnClicked(this, &SViewGenPanel::OnRemoveLoRA, Index)
				]
			];
		}
		LoRAListBox->Invalidate(EInvalidateWidgetReason::Layout);
	}

	// Video tab
	{
		int32 VModeIndex = FMath::Clamp(static_cast<int32>(Settings->VideoMode), 0, VideoModeOptions.Num() - 1);
		SelectedVideoMode = VideoModeOptions[VModeIndex];
		if (VideoModeCombo.IsValid()) VideoModeCombo->SetSelectedItem(SelectedVideoMode);
	}
	if (VideoPromptTextBox.IsValid()) VideoPromptTextBox->SetText(FText::FromString(Settings->VideoPrompt));
	if (VideoNegativePromptTextBox.IsValid()) VideoNegativePromptTextBox->SetText(FText::FromString(Settings->VideoNegativePrompt));

	// Refresh workflow preview after UI update from loaded settings
	RefreshWorkflowPreview();
}

// ============================================================================
// Presets
// ============================================================================

void SViewGenPanel::RefreshPresetList()
{
	PresetOptions.Empty();
	TArray<FString> Names = UGenAISettings::GetSavedPresetNames();
	for (const FString& Name : Names)
	{
		PresetOptions.Add(MakeShareable(new FString(Name)));
	}

	// Try to keep current selection if it still exists
	if (SelectedPreset.IsValid())
	{
		bool bFound = false;
		for (const TSharedPtr<FString>& Opt : PresetOptions)
		{
			if (*Opt == *SelectedPreset)
			{
				SelectedPreset = Opt;
				bFound = true;
				break;
			}
		}
		if (!bFound) SelectedPreset.Reset();
	}

	if (PresetCombo.IsValid())
	{
		PresetCombo->RefreshOptions();
		if (SelectedPreset.IsValid())
		{
			PresetCombo->SetSelectedItem(SelectedPreset);
		}
	}
}

void SViewGenPanel::SaveCurrentPreset()
{
	if (!PresetNameInput.IsValid()) return;
	FString Name = PresetNameInput->GetText().ToString().TrimStartAndEnd();
	if (Name.IsEmpty()) return;

	// Sanitize: only allow alphanumeric, spaces, hyphens, underscores
	FString Sanitized;
	for (TCHAR Ch : Name)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT(' ') || Ch == TEXT('-') || Ch == TEXT('_'))
		{
			Sanitized.AppendChar(Ch);
		}
	}
	if (Sanitized.IsEmpty()) return;

	if (UGenAISettings::SavePreset(Sanitized))
	{
		SelectedPreset = MakeShareable(new FString(Sanitized));
		RefreshPresetList();
		UpdateStatusText(FString::Printf(TEXT("Preset \"%s\" saved"), *Sanitized));
	}
}

void SViewGenPanel::LoadSelectedPreset()
{
	if (!SelectedPreset.IsValid() || SelectedPreset->IsEmpty()) return;

	if (UGenAISettings::LoadPreset(*SelectedPreset))
	{
		RefreshUIFromSettings();
		UpdateStatusText(FString::Printf(TEXT("Preset \"%s\" loaded"), **SelectedPreset));
	}
}

void SViewGenPanel::DeleteSelectedPreset()
{
	if (!SelectedPreset.IsValid() || SelectedPreset->IsEmpty()) return;

	FString Name = *SelectedPreset;
	if (UGenAISettings::DeletePreset(Name))
	{
		SelectedPreset.Reset();
		RefreshPresetList();
		UpdateStatusText(FString::Printf(TEXT("Preset \"%s\" deleted"), *Name));
	}
}

// ============================================================================
// Reference Adherence Mapping
// ============================================================================

void SViewGenPanel::ApplyAdherenceToSettings(float Adherence)
{
	UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings) return;

	Adherence = FMath::Clamp(Adherence, 0.0f, 1.0f);

	UE_LOG(LogTemp, Log, TEXT("ViewGen: ApplyAdherence %.2f, Mode=%d"), Adherence, static_cast<int32>(Settings->GenerationMode));

	// ========================================================================
	// Global parameters — always updated regardless of mode
	// ========================================================================

	// CFG Scale: controls how strictly the output follows the prompt
	Settings->CFGScale = FMath::Lerp(3.0f, 14.0f, Adherence);

	// Steps: more steps = more refined output
	Settings->Steps = FMath::RoundToInt(FMath::Lerp(12.0f, 45.0f, Adherence));

	// Denoising Strength: inverted — high adherence = low denoising (stay close to input)
	Settings->DenoisingStrength = FMath::Lerp(0.95f, 0.15f, Adherence);

	// ControlNet weight (if depth is enabled, relevant to any mode that uses it)
	if (Settings->bEnableDepthControlNet)
	{
		Settings->ControlNetWeight = FMath::Lerp(0.3f, 1.4f, Adherence);
	}

	// Hi-Res Fix denoise (if enabled): tighter at high adherence
	if (Settings->bEnableHiResFix)
	{
		Settings->HiResDenoise = FMath::Lerp(0.60f, 0.25f, Adherence);
	}

	// ========================================================================
	// Mode-specific parameters — layered on top of the globals
	// ========================================================================

	switch (Settings->GenerationMode)
	{
	case EGenMode::Img2Img:
	case EGenMode::DepthAndPrompt:
	case EGenMode::PromptOnly:
		// All covered by the globals above — no extra mode-specific work needed
		break;

	case EGenMode::Gemini:
	{
		// Thinking level: MINIMAL below 50%, HIGH at 50%+
		Settings->GeminiThinkingLevel = (Adherence >= 0.5f) ? TEXT("HIGH") : TEXT("MINIMAL");
		if (GeminiThinkingCombo.IsValid())
		{
			SelectedGeminiThinking = FindOrAddOption(GeminiThinkingOptions, Settings->GeminiThinkingLevel);
			GeminiThinkingCombo->SetSelectedItem(SelectedGeminiThinking);
		}

		// Resolution tier: 1K below 33%, 2K at 33-66%, 4K above 66%
		if (Adherence < 0.33f)
			Settings->GeminiResolution = TEXT("1K");
		else if (Adherence < 0.66f)
			Settings->GeminiResolution = TEXT("2K");
		else
			Settings->GeminiResolution = TEXT("4K");

		if (GeminiResolutionCombo.IsValid())
		{
			SelectedGeminiResolution = FindOrAddOption(GeminiResolutionOptions, Settings->GeminiResolution);
			GeminiResolutionCombo->SetSelectedItem(SelectedGeminiResolution);
		}

		// Response modalities: IMAGE only at low adherence, IMAGE+TEXT at high
		Settings->GeminiResponseModalities = (Adherence >= 0.7f) ? TEXT("IMAGE+TEXT") : TEXT("IMAGE");
		if (GeminiModalityCombo.IsValid())
		{
			SelectedGeminiModality = FindOrAddOption(GeminiModalityOptions, Settings->GeminiResponseModalities);
			GeminiModalityCombo->SetSelectedItem(SelectedGeminiModality);
		}
		break;
	}

	case EGenMode::Kling:
	{
		// Image Fidelity: direct mapping
		Settings->KlingImageFidelity = FMath::Lerp(0.1f, 0.95f, Adherence);

		// Human Fidelity: follows slightly lower than image
		Settings->KlingHumanFidelity = FMath::Lerp(0.05f, 0.85f, Adherence);
		break;
	}
	}

	ApplySettingsToConfig();
}

void SViewGenPanel::ApplySettingsToConfig()
{
	UGenAISettings* Settings = UGenAISettings::Get();
	if (Settings)
	{
		Settings->SaveConfig();
	}

	// Update workflow preview graph when settings change
	RefreshWorkflowPreview();
}

// ============================================================================
// Callbacks
// ============================================================================

void SViewGenPanel::OnGenerationComplete(bool bSuccess, UTexture2D* ResultTexture)
{
	// ---- Staged (Sequence) execution: advance to next stage if applicable ----
	if (CurrentStageIndex >= 0 && CurrentStageIndex < TotalStages - 1 && bSuccess)
	{
		// Current stage finished successfully — disconnect WebSocket, then submit next
		if (HttpClient.IsValid())
		{
			HttpClient->DisconnectWebSocket();
		}
		if (GEditor)
		{
			GEditor->GetTimerManager()->ClearTimer(ProgressPollTimer);
		}
		CurrentProgress = 0.0f;

		UE_LOG(LogTemp, Log, TEXT("ViewGen Sequence: Stage %d/%d complete, advancing..."),
			CurrentStageIndex + 1, TotalStages);

		SubmitNextStagedWorkflow();
		return;
	}

	// If we were in staged mode and just finished the last stage (or failed), clean up
	if (CurrentStageIndex >= 0)
	{
		StagedWorkflowQueue.Empty();
		CurrentStageIndex = -1;
		TotalStages = 0;
	}

	bIsGenerating = false;
	bIsGeneratingVideo = false;
	CurrentProgress = 1.0f;

	// Clear execution highlighting and cost overlay on all nodes
	if (GraphEditor.IsValid())
	{
		GraphEditor->ClearExecutingNodes();
		GraphEditor->SetOverlayText(FString());
	}

	// Disconnect WebSocket (no longer needed until next generation)
	if (HttpClient.IsValid())
	{
		HttpClient->DisconnectWebSocket();
	}

	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(ProgressPollTimer);
	}

	if (bSuccess && ResultTexture)
	{
		// Image result — add to history and update preview
		// ResultTexture is already rooted by DecodeImageToTexture

		// Create a brush for this history entry
		TSharedPtr<FSlateBrush> NewBrush = MakeShareable(new FSlateBrush());
		NewBrush->SetResourceObject(ResultTexture);
		NewBrush->ImageSize = FVector2D(ResultTexture->GetSizeX(), ResultTexture->GetSizeY());
		NewBrush->DrawAs = ESlateBrushDrawType::Image;

		// Evict oldest entry if at capacity
		if (ImageHistory.Num() >= MaxHistoryEntries)
		{
			FHistoryEntry& Oldest = ImageHistory[0];
			if (Oldest.Texture && Oldest.Texture->IsRooted())
			{
				Oldest.Texture->RemoveFromRoot();
			}
			ImageHistory.RemoveAt(0);
			// Adjust current index since we shifted everything down
			if (HistoryIndex > 0) HistoryIndex--;
		}

		// Push new entry and navigate to it
		FHistoryEntry Entry;
		Entry.Brush = NewBrush;
		Entry.Texture = ResultTexture;
		ImageHistory.Add(MoveTemp(Entry));
		HistoryIndex = ImageHistory.Num() - 1;

		// Update the main preview to show the new result
		PreviewBrush->SetResourceObject(ResultTexture);
		PreviewBrush->ImageSize = FVector2D(ResultTexture->GetSizeX(), ResultTexture->GetSizeY());
		PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
		if (PreviewImage.IsValid())
		{
			PreviewImage->SetImage(PreviewBrush.Get());
		}

		// Rebuild the thumbnail gallery
		RebuildResultGallery();

		// Segmentation disabled - pending RMBG integration
		// TODO: Re-enable with ComfyUI-RMBG SAM2Segment node
#if 0
		if (SegmentationCapture.IsValid() && SegmentationCapture->HasMaskData())
		{
			if (SegmentationCapture->SegmentGeneratedImage(ResultTexture))
			{
				PopulateSegmentationGallery();
			}
		}
#endif

		// Check if the graph has an Image Upres node — if so, export the result as EXR/PNG
		if (GraphEditor.IsValid())
		{
			for (const FGraphNode& Node : GraphEditor->GetNodes())
			{
				if (Node.ClassType == UEImageUpresClassType)
				{
					UE_LOG(LogTemp, Log, TEXT("ViewGen: Image Upres node detected, triggering UE-side export"));
					ExecuteImageUpresExport();
					break;
				}
			}
		}

		// Also check if a 3D model was detected alongside the image
		if (HttpClient.IsValid() && HttpClient->Has3DModelResult())
		{
			bool bHasSave3DModelNode = false;
			bool bHas3DAssetExportNode = false;
			if (GraphEditor.IsValid())
			{
				for (const FGraphNode& Node : GraphEditor->GetNodes())
				{
					if (Node.ClassType == UESave3DModelClassType) { bHasSave3DModelNode = true; }
					if (Node.ClassType == UE3DAssetExportClassType) { bHas3DAssetExportNode = true; }
				}
			}
			if (bHas3DAssetExportNode)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: 3D model detected alongside image, triggering 3D Asset Export"));
				Execute3DAssetExportNodes();
			}
			else if (bHasSave3DModelNode)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: 3D model also detected alongside image, triggering Save3DModel import"));
				ExecuteSave3DModelNodes();
			}
		}

		UpdateStatusText(TEXT("Generation complete!"));
	}
	else if (bSuccess && !ResultTexture)
	{
		// Non-image result — could be video, Meshy 3D model, or ComfyUI 3D model generation

		// Check if the HttpClient detected a 3D model file and the graph has a Save3DModel or 3D Asset Export node
		bool bHasSave3DModelNode = false;
		bool bHas3DAssetExportNode = false;
		bool bHasMeshyNode = false;
		if (GraphEditor.IsValid())
		{
			for (const FGraphNode& Node : GraphEditor->GetNodes())
			{
				if (Node.ClassType == UESave3DModelClassType)
				{
					bHasSave3DModelNode = true;
				}
				if (Node.ClassType == UE3DAssetExportClassType)
				{
					bHas3DAssetExportNode = true;
				}
				if (Node.ClassType == TEXT("MeshyImageToModelNode"))
				{
					bHasMeshyNode = true;
				}
			}
		}

		if (bHas3DAssetExportNode && HttpClient.IsValid() && HttpClient->Has3DModelResult())
		{
			UpdateStatusText(TEXT("3D model generation complete! Exporting as UAsset..."));
			UE_LOG(LogTemp, Log, TEXT("ViewGen: 3D model detected in workflow output, triggering 3D Asset Export"));
			Execute3DAssetExportNodes();
		}
		else if (bHasSave3DModelNode && HttpClient.IsValid() && HttpClient->Has3DModelResult())
		{
			UpdateStatusText(TEXT("3D model generation complete! Importing..."));
			UE_LOG(LogTemp, Log, TEXT("ViewGen: 3D model detected in workflow output, triggering Save3DModel import"));
			ExecuteSave3DModelNodes();
		}
		else if (bHasMeshyNode)
		{
			UpdateStatusText(TEXT("Meshy: 3D model generation complete! Processing..."));
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Meshy workflow completed, triggering import flow"));
			ExecuteMeshyImportNodes();
		}
		else if (bHasSave3DModelNode || bHas3DAssetExportNode)
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Graph has 3D model node but no 3D model was detected in workflow output. "
				"The upstream 3D generation node may need to be an OUTPUT_NODE in ComfyUI, or its output format is unrecognized."));
			UpdateStatusText(TEXT("3D model generation completed but no model file was detected. Check the Output Log for details."));
		}
		else
		{
			// Check if this is a video result (HttpClient stored the filename)
			bool bIsVideoResult = false;
			if (HttpClient.IsValid())
			{
				FString ResultFile = HttpClient->GetLastResultFilename().ToLower();
				bIsVideoResult = ResultFile.EndsWith(TEXT(".mp4")) || ResultFile.EndsWith(TEXT(".webm"))
					|| ResultFile.EndsWith(TEXT(".gif")) || ResultFile.EndsWith(TEXT(".mov"))
					|| ResultFile.EndsWith(TEXT(".avi")) || ResultFile.EndsWith(TEXT(".mkv"));
			}

			if (bIsVideoResult)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: Video result detected: %s — downloading and extracting thumbnail"),
					*HttpClient->GetLastResultFilename());
				HandleVideoResult();
			}
			else
			{
				UpdateStatusText(TEXT("Generation complete!"));
			}
		}
	}
	else
	{
		UpdateStatusText(TEXT("Generation failed"));
	}
}

void SViewGenPanel::OnGenerationProgress(float Progress)
{
	CurrentProgress = Progress;

	FString ProgressMsg = FString::Printf(TEXT("Generating... %d%%"),
		FMath::RoundToInt(Progress * 100.0f));
	UpdateStatusText(ProgressMsg);
}

void SViewGenPanel::OnGenerationError(const FString& ErrorMessage)
{
	bIsGenerating = false;
	bIsGeneratingVideo = false;
	CurrentProgress = 0.0f;

	// Clear execution highlighting and cost overlay
	if (GraphEditor.IsValid())
	{
		GraphEditor->ClearExecutingNodes();
		GraphEditor->SetOverlayText(FString());
	}

	// Disconnect WebSocket
	if (HttpClient.IsValid())
	{
		HttpClient->DisconnectWebSocket();
	}

	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(ProgressPollTimer);
	}

	UpdateStatusText(FString::Printf(TEXT("Error: %s"), *ErrorMessage));
	UE_LOG(LogTemp, Error, TEXT("ViewGen: %s"), *ErrorMessage);
}

// ============================================================================
// Helpers
// ============================================================================

void SViewGenPanel::UpdateThumbnailBrush(TSharedPtr<FSlateBrush>& Brush,
	UTexture2D* Texture, TSharedPtr<SImage>& ImageWidget)
{
	if (!Texture || !Brush.IsValid())
	{
		return;
	}

	// NOTE: Do NOT unroot old resource here. Preview textures are shared with
	// ImageHistory entries which manage their own rooting/unrooting lifecycle.
	// Unrooting here would allow GC to collect textures still visible in the gallery.
	// All textures are now rooted at creation time by their respective factories.

	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Brush->DrawAs = ESlateBrushDrawType::Image;

	if (ImageWidget.IsValid())
	{
		ImageWidget->SetImage(Brush.Get());
	}
}

void SViewGenPanel::UpdateStatusText(const FString& Message)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

TArray<FLoRAEntry> SViewGenPanel::GetActiveLoRAs() const
{
	TArray<FLoRAEntry> Active;
	for (const FLoRAEntry& Entry : UILoRAEntries)
	{
		if (Entry.bEnabled)
		{
			Active.Add(Entry);
		}
	}
	return Active;
}

FString SViewGenPanel::EstimateGenerationCost() const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings->bShowCostEstimates)
	{
		return FString();
	}

	const EGenMode Mode = Settings->GenerationMode;

	switch (Mode)
	{
	case EGenMode::Img2Img:
	case EGenMode::DepthAndPrompt:
	case EGenMode::PromptOnly:
		return TEXT("Local (no API cost)");

	case EGenMode::Gemini:
	{
		float Cost = Settings->GeminiCost1K; // default
		if (Settings->GeminiResolution == TEXT("2K"))
		{
			Cost = Settings->GeminiCost2K;
		}
		else if (Settings->GeminiResolution == TEXT("4K"))
		{
			Cost = Settings->GeminiCost4K;
		}
		return FString::Printf(TEXT("~$%.3f (Gemini %s)"), Cost, *Settings->GeminiResolution);
	}

	case EGenMode::Kling:
	{
		float Cost = Settings->KlingImageCost * Settings->KlingImageCount;
		if (Settings->KlingImageCount > 1)
		{
			return FString::Printf(TEXT("~$%.3f (%d images x $%.3f)"),
				Cost, Settings->KlingImageCount, Settings->KlingImageCost);
		}
		return FString::Printf(TEXT("~$%.3f (Kling Image)"), Cost);
	}

	default:
		return FString();
	}
}

FString SViewGenPanel::EstimateVideoCost() const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings->bShowCostEstimates)
	{
		return FString();
	}

	const float Duration = Settings->VideoDuration;

	switch (Settings->VideoMode)
	{
	case EVideoMode::KlingVideo:
	{
		bool bIsPro = Settings->KlingVideoQuality == TEXT("pro");
		float CostPerSec = bIsPro ? Settings->KlingVideoProCostPerSec : Settings->KlingVideoStdCostPerSec;
		float Total = CostPerSec * Duration;
		return FString::Printf(TEXT("~$%.2f (%.0fs x $%.3f/s %s)"),
			Total, Duration, CostPerSec, bIsPro ? TEXT("Pro") : TEXT("Std"));
	}

	case EVideoMode::Veo3:
	{
		float Total = Settings->Veo3CostPerSec * Duration;
		FString AudioNote = Settings->bVeo3GenerateAudio ? TEXT("+audio") : TEXT("");
		return FString::Printf(TEXT("~$%.2f (%.0fs x $%.2f/s%s)"),
			Total, Duration, Settings->Veo3CostPerSec, *AudioNote);
	}

	case EVideoMode::WanVideo:
		return TEXT("Local (no API cost)");

	default:
		return FString();
	}
}

// ============================================================================
// Workflow Preview
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildWorkflowPreviewSection()
{
	return SNew(SVerticalBox)

		// Header toggle button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this]() -> FReply
			{
				bWorkflowPreviewExpanded = !bWorkflowPreviewExpanded;
				if (bWorkflowPreviewExpanded)
				{
					RefreshWorkflowPreview();
				}
				return FReply::Handled();
			})
			.Content()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return bWorkflowPreviewExpanded
							? FText::FromString(TEXT("\x25BC"))
							: FText::FromString(TEXT("\x25B6"));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("WorkflowPreview", "Workflow Preview"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.70f, 0.35f)))
				]
			]
		]

		// Preview body (collapsible)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				return bWorkflowPreviewExpanded ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.MinDesiredHeight(300.0f)
			.MaxDesiredHeight(500.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f))
				.Padding(0.0f)
				[
					SAssignNew(WorkflowPreview, SWorkflowPreviewPanel)
				]
			]
		];
}

void SViewGenPanel::RefreshWorkflowPreview()
{
	if (WorkflowPreview.IsValid() && bWorkflowPreviewExpanded)
	{
		WorkflowPreview->RefreshGraph();
	}
}

// ============================================================================
// Graph Editor (Interactive ComfyUI-style editor)
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildGraphEditorSection()
{
	// Returns ONLY the collapsible header toggle.
	// The graph editor body (SSplitter with canvas + details panel) is placed in a
	// separate FillHeight slot outside the SScrollBox so it can expand to fill all
	// available space down to the toolbar.
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.OnClicked_Lambda([this]() -> FReply
		{
			bGraphEditorExpanded = !bGraphEditorExpanded;
			if (bGraphEditorExpanded && GraphEditor.IsValid())
			{
				// Restore last-edited graph if empty, fall back to preset graph
				if (GraphEditor->GetNodes().Num() == 0)
				{
					if (!RestoreLastGraph())
					{
						GraphEditor->BuildPresetGraph();
					}
				}
				RefreshLoadImageThumbnails();
			}
			return FReply::Handled();
		})
		.Content()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return bGraphEditorExpanded
						? FText::FromString(TEXT("\x25BC"))
						: FText::FromString(TEXT("\x25B6"));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Graph Editor")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.70f, 0.85f)))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Right-click to add nodes | Del to delete | Ctrl+L auto-layout")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.Visibility_Lambda([this]()
				{
					return bGraphEditorExpanded ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		];
}

void SViewGenPanel::OnGraphSelectionChanged()
{
	RebuildNodeDetailsPanel();
}

void SViewGenPanel::RebuildNodeDetailsPanel()
{
	if (!NodeDetailsPanel.IsValid() || !GraphEditor.IsValid())
	{
		return;
	}

	// Clear old combo option arrays — shared pointers keep them alive if a combo still references one
	DetailsPanelComboOptions.Empty();
	NodeDetailsPanel->ClearChildren();

	FString SelectedId = GraphEditor->GetPrimarySelectedNodeId();
	FGraphNode* Node = SelectedId.IsEmpty() ? nullptr : GraphEditor->FindNodeById(SelectedId);

	if (!Node)
	{
		// No selection — show hint
		NodeDetailsPanel->AddSlot()
		.Padding(8.0f, 20.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Select a node to view its properties.")))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			.AutoWrapText(true)
		];
		return;
	}

	// --- Node Title (editable) ---
	NodeDetailsPanel->AddSlot()
	.Padding(0.0f, 0.0f, 0.0f, 2.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Node->Title))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		.ColorAndOpacity(FSlateColor(Node->HeaderColor))
	];

	// Subtitle: class type
	if (Node->IsUESourceNode())
	{
		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Unreal Engine")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
		];
	}
	else
	{
		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Node->ClassType))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}

	// Separator
	NodeDetailsPanel->AddSlot()
	.Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		SNew(SSeparator)
	];

	// --- Title editing field ---
	{
		FString CapturedNodeId = Node->Id;
		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Title")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.55f)))
		];

		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(Node->Title))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.OnTextCommitted_Lambda([this, CapturedNodeId](const FText& NewText, ETextCommit::Type)
			{
				if (GraphEditor.IsValid())
				{
					FGraphNode* N = GraphEditor->FindNodeById(CapturedNodeId);
					if (N)
					{
						N->Title = NewText.ToString();
					}
				}
			})
		];
	}

	// --- Widget parameters ---
	const TArray<FString>& WidgetOrder = Node->WidgetOrder;
	for (const FString& WidgetName : WidgetOrder)
	{
		const FString* ValuePtr = Node->WidgetValues.Find(WidgetName);
		if (!ValuePtr) continue;

		const FComfyInputDef* InputDef = Node->WidgetInputDefs.Find(WidgetName);
		FString WidgetType = InputDef ? InputDef->Type : TEXT("STRING");

		FString CapturedNodeId = Node->Id;
		FString CapturedWidgetName = WidgetName;

		// Label
		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(WidgetName))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.55f)))
		];

		// --- COMBO (dropdown) ---
		if (WidgetType == TEXT("COMBO") && InputDef && InputDef->ComboOptions.Num() > 0)
		{
			// Build options as a shared array so the SComboBox keeps it alive
			TSharedPtr<TArray<TSharedPtr<FString>>> OptionsPtr = MakeShareable(new TArray<TSharedPtr<FString>>());
			DetailsPanelComboOptions.Add(OptionsPtr);

			TSharedPtr<FString> CurrentSelection;
			for (const FString& Opt : InputDef->ComboOptions)
			{
				TSharedPtr<FString> Item = MakeShareable(new FString(Opt));
				OptionsPtr->Add(Item);
				if (Opt == *ValuePtr)
				{
					CurrentSelection = Item;
				}
			}
			// If the current value isn't in the combo options (e.g. a browsed upload),
			// add it so the dropdown can display and re-select it
			if (!CurrentSelection.IsValid() && !ValuePtr->IsEmpty())
			{
				TSharedPtr<FString> ExtraItem = MakeShareable(new FString(*ValuePtr));
				OptionsPtr->Insert(ExtraItem, 0);
				CurrentSelection = ExtraItem;
			}
			if (!CurrentSelection.IsValid() && OptionsPtr->Num() > 0)
			{
				CurrentSelection = (*OptionsPtr)[0];
			}

			// For LoadImage "image" widget: combo + Browse button side by side
			bool bIsLoadImageWidget = (Node->ClassType == TEXT("LoadImage") && WidgetName == TEXT("image"));

			// For LoadVideo nodes (VHS_LoadVideo, etc.): combo + Browse button for video files
			bool bIsLoadVideoWidget = (Node->ClassType.Contains(TEXT("LoadVideo")) && WidgetName == TEXT("file"));

			TSharedRef<SWidget> ComboWidget =
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(OptionsPtr.Get())
				.InitiallySelectedItem(CurrentSelection)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
				{
					return SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
				})
				.OnSelectionChanged_Lambda([this, CapturedNodeId, CapturedWidgetName](TSharedPtr<FString> Selected, ESelectInfo::Type)
				{
					if (Selected.IsValid() && GraphEditor.IsValid())
					{
						GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, *Selected);
					}
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this, CapturedNodeId, CapturedWidgetName]() -> FText
					{
						if (GraphEditor.IsValid())
						{
							FGraphNode* N = GraphEditor->FindNodeById(CapturedNodeId);
							if (N)
							{
								const FString* V = N->WidgetValues.Find(CapturedWidgetName);
								if (V) return FText::FromString(*V);
							}
						}
						return FText::GetEmpty();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				];

			if (bIsLoadImageWidget || bIsLoadVideoWidget)
			{
				// Dropdown + Browse button
				NodeDetailsPanel->AddSlot()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						ComboWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Browse...")))
						.ToolTipText(FText::FromString(
							bIsLoadVideoWidget
								? TEXT("Browse for a video file on disk and upload it to the ComfyUI server")
								: TEXT("Browse for an image file on disk and upload it to the ComfyUI server")))
						.OnClicked_Lambda([this, CapturedNodeId, CapturedWidgetName, bIsLoadVideoWidget]() -> FReply
						{
							if (bIsLoadVideoWidget)
								OnBrowseNodeVideo(CapturedNodeId, CapturedWidgetName);
							else
								OnBrowseNodeImage(CapturedNodeId, CapturedWidgetName);
							return FReply::Handled();
						})
					]
				];
			}
			else
			{
				// Standard combo dropdown
				NodeDetailsPanel->AddSlot()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					ComboWidget
				];
			}
		}
		// --- BOOLEAN (checkbox) ---
		else if (WidgetType == TEXT("BOOLEAN") || WidgetType == TEXT("BOOL"))
		{
			bool bCurrentVal = (*ValuePtr == TEXT("true"));
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SCheckBox)
				.IsChecked(bCurrentVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, CapturedNodeId, CapturedWidgetName](ECheckBoxState NewState)
				{
					if (GraphEditor.IsValid())
					{
						FString Val = (NewState == ECheckBoxState::Checked) ? TEXT("true") : TEXT("false");
						GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, Val);
					}
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(bCurrentVal ? TEXT("Enabled") : TEXT("Disabled")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			];
		}
		// --- INT ---
		else if (WidgetType == TEXT("INT"))
		{
			int32 CurrentVal = FCString::Atoi(**ValuePtr);
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSpinBox<int32>)
				.Value(CurrentVal)
				.MinValue(InputDef ? static_cast<int32>(InputDef->MinValue) : -999999)
				.MaxValue(InputDef ? static_cast<int32>(InputDef->MaxValue) : 999999)
				.Delta(InputDef ? static_cast<int32>(FMath::Max(1.0f, InputDef->Step)) : 1)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.OnValueCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](int32 NewVal, ETextCommit::Type)
				{
					if (GraphEditor.IsValid())
					{
						GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, FString::FromInt(NewVal));
					}
				})
			];
		}
		// --- FLOAT ---
		else if (WidgetType == TEXT("FLOAT"))
		{
			float CurrentVal = FCString::Atof(**ValuePtr);
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSpinBox<float>)
				.Value(CurrentVal)
				.MinValue(InputDef ? InputDef->MinValue : -999999.0f)
				.MaxValue(InputDef ? InputDef->MaxValue : 999999.0f)
				.Delta(InputDef ? InputDef->Step : 0.01f)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.OnValueCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](float NewVal, ETextCommit::Type)
				{
					if (GraphEditor.IsValid())
					{
						GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName,
							FString::Printf(TEXT("%.6f"), NewVal));
					}
				})
			];
		}
		// --- STRING: "asset_path" on UE source nodes gets a text box + Content Browser folder Browse ---
		else if (WidgetName == TEXT("asset_path") && Node->IsUESourceNode())
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.ToolTipText(FText::FromString(TEXT("Browse for a Content Browser folder to save imported assets into")))
					.OnClicked_Lambda([this, CapturedNodeId, CapturedWidgetName]() -> FReply
					{
						OnBrowseAssetPath(CapturedNodeId, CapturedWidgetName);
						return FReply::Handled();
					})
				]
			];
		}
		// --- STRING: path/directory widgets get a text box + disk folder Browse ---
		// Matches known path-like widget names on both ComfyUI and UE source nodes.
		// Also matches filename_prefix on SaveImage nodes (lets the user browse for an output folder).
		else if (
			WidgetName == TEXT("output_dir") || WidgetName == TEXT("save_path") ||
			WidgetName == TEXT("directory") || WidgetName == TEXT("folder") ||
			WidgetName == TEXT("output_path") || WidgetName == TEXT("path") ||
			(WidgetName == TEXT("filename_prefix") && Node->ClassType.Contains(TEXT("SaveImage"))))
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.ToolTipText(FText::FromString(TEXT("Browse for a folder on disk")))
					.OnClicked_Lambda([this, CapturedNodeId, CapturedWidgetName]() -> FReply
					{
						OnBrowseDiskFolder(CapturedNodeId, CapturedWidgetName);
						return FReply::Handled();
					})
				]
			];
		}
		// --- STRING: UE 3D Loader "file_path" gets a text box + Browse 3D Model button ---
		else if (Node->ClassType == UE3DLoaderClassType && WidgetName == TEXT("file_path"))
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.ToolTipText(FText::FromString(TEXT("Browse for a 3D model file (GLB, OBJ, FBX) to import into Unreal Engine")))
					.OnClicked_Lambda([this, CapturedNodeId]() -> FReply
					{
						OnBrowse3DModelFile(CapturedNodeId);
						return FReply::Handled();
					})
				]
			];
		}
		// --- STRING: LoadImage "image" widget gets a text box + Browse button ---
		else if (Node->ClassType == TEXT("LoadImage") && WidgetName == TEXT("image"))
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.ToolTipText(FText::FromString(TEXT("Browse for an image file and upload it to the ComfyUI server")))
					.OnClicked_Lambda([this, CapturedNodeId, CapturedWidgetName]() -> FReply
					{
						OnBrowseNodeImage(CapturedNodeId, CapturedWidgetName);
						return FReply::Handled();
					})
				]
			];
		}
		// --- STRING: LoadVideo "file" widget gets a text box + Browse button ---
		else if (Node->ClassType.Contains(TEXT("LoadVideo")) && WidgetName == TEXT("file"))
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Browse...")))
					.ToolTipText(FText::FromString(TEXT("Browse for a video file and upload it to the ComfyUI server")))
					.OnClicked_Lambda([this, CapturedNodeId, CapturedWidgetName]() -> FReply
					{
						OnBrowseNodeVideo(CapturedNodeId, CapturedWidgetName);
						return FReply::Handled();
					})
				]
			];
		}
		// --- STRING (multi-line text box — great for prompts) ---
		else
		{
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(40.0f)
				.MaxDesiredHeight(120.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(FText::FromString(*ValuePtr))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.AutoWrapText(true)
					.OnTextCommitted_Lambda([this, CapturedNodeId, CapturedWidgetName](const FText& NewText, ETextCommit::Type)
					{
						if (GraphEditor.IsValid())
						{
							GraphEditor->CommitWidgetValueSilent(CapturedNodeId, CapturedWidgetName, NewText.ToString());
						}
					})
				]
			];
		}
	}

	// --- LoadImage / LoadVideo / VideoToImage thumbnail preview ---
	if ((Node->ClassType == TEXT("LoadImage") || Node->ClassType.Contains(TEXT("LoadVideo")) || Node->ClassType == UEVideoToImageClassType) && Node->ThumbnailBrush.IsValid())
	{
		// Separator label
		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Preview")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.55f)))
		];

		// Store the brush for the details panel (so it stays alive)
		NodeDetailsThumbnailBrush = Node->ThumbnailBrush;

		NodeDetailsPanel->AddSlot()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBox)
			.MaxDesiredHeight(300.0f)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				.StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image(NodeDetailsThumbnailBrush.Get())
				]
			]
		];
	}

	// --- LoadImage: resolution info ---
	if (Node->ClassType == TEXT("LoadImage") && Node->ThumbnailBrush.IsValid())
	{
		FVector2D ImgSize = Node->ThumbnailBrush->ImageSize;
		if (ImgSize.X > 0 && ImgSize.Y > 0)
		{
			FString ResolutionText = FString::Printf(TEXT("%dx%d"), (int32)ImgSize.X, (int32)ImgSize.Y);

			// Compute megapixels for context
			float Megapixels = (ImgSize.X * ImgSize.Y) / 1000000.0f;
			if (Megapixels >= 0.1f)
			{
				ResolutionText += FString::Printf(TEXT("  (%.1f MP)"), Megapixels);
			}

			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(ResolutionText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.65f)))
			];
		}
	}

	// --- UE Video to Image: Browse button + video info + thumbnail ---
	if (Node->ClassType == UEVideoToImageClassType)
	{
		bool bHasLocalFile = !Node->LocalFilePath.IsEmpty() && FPaths::FileExists(Node->LocalFilePath);

		// Browse button
		{
			FString CapturedNodeId = Node->Id;
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 4.0f, 0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(bHasLocalFile
					? FString::Printf(TEXT("Change Video  (%s)"), *FPaths::GetCleanFilename(Node->LocalFilePath))
					: FString(TEXT("Browse Video..."))))
				.ToolTipText(FText::FromString(TEXT("Browse for a video file to extract a frame from")))
				.OnClicked_Lambda([this, CapturedNodeId]() -> FReply
				{
					// Reuse the video browse flow — the node stores LocalFilePath
					OnBrowseNodeVideo(CapturedNodeId, TEXT("file"));
					return FReply::Handled();
				})
			];
		}

		// Video info + Play / Open Folder
		if (bHasLocalFile)
		{
			int64 FileSize = IFileManager::Get().FileSize(*Node->LocalFilePath);
			FString InfoText = FPaths::GetCleanFilename(Node->LocalFilePath);
			if (FileSize > 0)
			{
				InfoText += FString::Printf(TEXT("  (%s)"), *FText::AsMemory(FileSize).ToString());
			}

			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 2.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InfoText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.65f)))
			];

			FString CapturedPath = Node->LocalFilePath;
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 2.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("\u25B6  Play Video")))
					.OnClicked_Lambda([CapturedPath]() -> FReply
					{
						FString NativePath = CapturedPath;
						FPaths::MakePlatformFilename(NativePath);
						FPlatformProcess::LaunchFileInDefaultExternalApplication(*NativePath);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Open Folder")))
					.OnClicked_Lambda([CapturedPath]() -> FReply
					{
						FString NativePath = CapturedPath;
						FPaths::MakePlatformFilename(NativePath);
						FPlatformProcess::ExploreFolder(*NativePath);
						return FReply::Handled();
					})
				]
			];
		}

		// Quick Render button for VideoToImage nodes too
		{
			FString CapturedNodeId = Node->Id;
			bool bIsRendering = !QuickRenderTargetNodeId.IsEmpty();

			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
				.ToolTipText(FText::FromString(TEXT("Render the active Level Sequence and use the output as the source video")))
				.IsEnabled(!bIsRendering)
				.OnClicked_Lambda([this, CapturedNodeId]() -> FReply
				{
					OnQuickRenderForNode(CapturedNodeId);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("LevelEditor.Tabs.Cinematics"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(bIsRendering ? TEXT("Rendering...") : TEXT("Quick Render (Sequencer)")))
					]
				]
			];
		}
	}

	// --- LoadVideo: Play button + video info ---
	if (Node->ClassType.Contains(TEXT("LoadVideo")))
	{
		// Check if we have a local file path stored (from browse or deserialized)
		bool bHasLocalFile = !Node->LocalFilePath.IsEmpty() && FPaths::FileExists(Node->LocalFilePath);

		// Get the ComfyUI server filename for download fallback
		FString ServerFilename;
		const FString* FileVal = Node->WidgetValues.Find(TEXT("file"));
		if (FileVal && !FileVal->IsEmpty())
		{
			ServerFilename = *FileVal;
		}

		// Video info label
		{
			FString VideoInfo = ServerFilename;
			if (bHasLocalFile)
			{
				int64 FileSize = IFileManager::Get().FileSize(*Node->LocalFilePath);
				if (FileSize > 0)
				{
					VideoInfo += FString::Printf(TEXT("  (%s)"), *FText::AsMemory(FileSize).ToString());
				}
			}

			if (!VideoInfo.IsEmpty())
			{
				NodeDetailsPanel->AddSlot()
				.Padding(0.0f, 4.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Video")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.55f)))
				];

				NodeDetailsPanel->AddSlot()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(VideoInfo))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.75f)))
					.AutoWrapText(true)
				];
			}
		}

		// Play / Open Folder buttons
		if (bHasLocalFile)
		{
			FString CapturedPath = Node->LocalFilePath;
			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 2.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("\u25B6  Play Video")))
					.ToolTipText(FText::FromString(FString::Printf(TEXT("Open %s in your default video player"), *FPaths::GetCleanFilename(CapturedPath))))
					.OnClicked_Lambda([CapturedPath]() -> FReply
					{
						FString NativePath = CapturedPath;
						FPaths::MakePlatformFilename(NativePath);
						UE_LOG(LogTemp, Log, TEXT("ViewGen: Play Video clicked — launching: %s"), *NativePath);
						FPlatformProcess::LaunchFileInDefaultExternalApplication(*NativePath);
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Open Folder")))
					.ToolTipText(FText::FromString(TEXT("Open the folder containing this video file")))
					.OnClicked_Lambda([CapturedPath]() -> FReply
					{
						FString NativePath = CapturedPath;
						FPaths::MakePlatformFilename(NativePath);
						FString FolderPath = FPaths::GetPath(NativePath);
						UE_LOG(LogTemp, Log, TEXT("ViewGen: Open Folder clicked — exploring: %s"), *FolderPath);
						FPlatformProcess::ExploreFolder(*NativePath);
						return FReply::Handled();
					})
				]
			];
		}
		else if (!ServerFilename.IsEmpty())
		{
			// No local file — offer to download from ComfyUI then play
			FString CapturedNodeId = Node->Id;
			FString CapturedServerFilename = ServerFilename;

			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 2.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("\u25B6  Download & Play")))
					.ToolTipText(FText::FromString(TEXT("Download the video from ComfyUI and open it in your default video player")))
					.OnClicked_Lambda([this, CapturedNodeId, CapturedServerFilename]() -> FReply
					{
						if (!HttpClient.IsValid()) return FReply::Handled();

						FString VideoDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoCache");
						IFileManager::Get().MakeDirectory(*VideoDir, true);
						FString LocalPath = VideoDir / CapturedServerFilename;

						UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloading video '%s' from ComfyUI for playback..."), *CapturedServerFilename);
						UpdateStatusText(FString::Printf(TEXT("Downloading %s..."), *CapturedServerFilename));

						HttpClient->DownloadComfyUIFile(CapturedServerFilename, TEXT(""), TEXT("input"), LocalPath,
							[this, CapturedNodeId, LocalPath, CapturedServerFilename](const FString& DownloadedPath)
						{
							if (DownloadedPath.IsEmpty())
							{
								UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to download video '%s' from ComfyUI"), *CapturedServerFilename);
								UpdateStatusText(TEXT("Failed to download video — check Output Log"));
								return;
							}

							// Store the local path on the node so Play works next time
							if (GraphEditor.IsValid())
							{
								FGraphNode* NodePtr = GraphEditor->FindNodeById(CapturedNodeId);
								if (NodePtr)
								{
									NodePtr->LocalFilePath = DownloadedPath;
								}
							}

							// Extract thumbnail now that we have the file
							ExtractVideoThumbnail(DownloadedPath, CapturedNodeId);

							// Launch the video
							FString NativeDLPath = DownloadedPath;
							FPaths::MakePlatformFilename(NativeDLPath);
							UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloaded video, launching: %s"), *NativeDLPath);
							FPlatformProcess::LaunchFileInDefaultExternalApplication(*NativeDLPath);

							UpdateStatusText(FString::Printf(TEXT("Playing %s"), *FPaths::GetCleanFilename(DownloadedPath)));
							RebuildNodeDetailsPanel();
						});

						return FReply::Handled();
					})
				]
			];
		}

		// Quick Render button — always shown for LoadVideo nodes
		{
			FString CapturedNodeId = Node->Id;
			bool bIsRendering = !QuickRenderTargetNodeId.IsEmpty();

			NodeDetailsPanel->AddSlot()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
				.ToolTipText(FText::FromString(TEXT("Render the active Level Sequence via Movie Render Graph and load the output video into this node")))
				.IsEnabled(!bIsRendering)
				.OnClicked_Lambda([this, CapturedNodeId]() -> FReply
				{
					OnQuickRenderForNode(CapturedNodeId);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("LevelEditor.Tabs.Cinematics"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(bIsRendering ? TEXT("Rendering...") : TEXT("Quick Render (Sequencer)")))
					]
				]
			];
		}
	}
}

void SViewGenPanel::RefreshLoadImageThumbnails()
{
	if (!GraphEditor.IsValid() || !HttpClient.IsValid()) return;

	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	for (const FGraphNode& Node : GraphNodes)
	{
		// Only process LoadImage nodes (and UE source nodes that export as LoadImage are handled separately)
		if (Node.ClassType != TEXT("LoadImage")) continue;

		const FString* ImageVal = Node.WidgetValues.Find(TEXT("image"));
		if (!ImageVal || ImageVal->IsEmpty()) continue;

		// Skip marker filenames (UE source node markers are resolved at export time)
		if (ImageVal->StartsWith(TEXT("__UE_"))) continue;

		// Skip if this node already has a thumbnail
		if (Node.ThumbnailTexture != nullptr) continue;

		FString NodeId = Node.Id;
		FString ImageFilename = *ImageVal;

		HttpClient->FetchImageThumbnail(ImageFilename, [this, NodeId](UTexture2D* Tex)
		{
			if (Tex && GraphEditor.IsValid())
			{
				GraphEditor->SetNodeThumbnail(NodeId, Tex);
				// Texture was AddToRoot'd by FetchImageThumbnail; the graph editor brush now owns it.
			}
		});
	}

	// Also refresh video thumbnails (extracts first frame via ffmpeg for LoadVideo nodes)
	RefreshLoadVideoThumbnails();
}

void SViewGenPanel::RefreshLoadVideoThumbnails()
{
	if (!GraphEditor.IsValid()) return;

	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	for (const FGraphNode& Node : GraphNodes)
	{
		if (!Node.ClassType.Contains(TEXT("LoadVideo")) && Node.ClassType != UEVideoToImageClassType) continue;

		// Skip if already has a thumbnail
		if (Node.ThumbnailTexture != nullptr) continue;

		// Need a local file to extract a thumbnail from
		if (Node.LocalFilePath.IsEmpty() || !FPaths::FileExists(Node.LocalFilePath)) continue;

		ExtractVideoThumbnail(Node.LocalFilePath, Node.Id);
	}
}

void SViewGenPanel::OnBrowseNodeImage(FString NodeId, FString WidgetName)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT);

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Image for LoadImage Node"),
		DefaultPath,
		TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.exr;*.tga)|*.png;*.jpg;*.jpeg;*.bmp;*.exr;*.tga|All Files (*.*)|*.*"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0) return;

	const FString FilePath = OutFiles[0];
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(FilePath));

	// Load the file bytes
	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
		return;
	}

	// Determine desired filename for ComfyUI server (use original name)
	FString DesiredFilename = FPaths::GetCleanFilename(FilePath);

	// Need to convert to PNG if not already PNG — for simplicity, if the file IS a PNG or JPG
	// we can send it directly. ComfyUI's upload endpoint accepts raw file bytes as multipart form data.
	// However, our UploadImage() expects base64 PNG. Let's decode/re-encode through IImageWrapper.
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	// Detect format from extension
	FString Ext = FPaths::GetExtension(FilePath).ToLower();
	EImageFormat SourceFormat = EImageFormat::PNG;
	if (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) SourceFormat = EImageFormat::JPEG;
	else if (Ext == TEXT("bmp")) SourceFormat = EImageFormat::BMP;
	else if (Ext == TEXT("exr")) SourceFormat = EImageFormat::EXR;

	// Decompress source image
	TSharedPtr<IImageWrapper> SourceWrapper = ImageWrapperModule.CreateImageWrapper(SourceFormat);
	if (!SourceWrapper->SetCompressed(FileBytes.GetData(), FileBytes.Num()))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to decode image: %s"), *DesiredFilename));
		return;
	}

	// Get raw RGBA data
	TArray<uint8> RawRGBA;
	if (!SourceWrapper->GetRaw(ERGBFormat::BGRA, 8, RawRGBA))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to decompress image pixels: %s"), *DesiredFilename));
		return;
	}

	int32 ImgWidth = SourceWrapper->GetWidth();
	int32 ImgHeight = SourceWrapper->GetHeight();

	// Re-encode as PNG
	TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	PNGWrapper->SetRaw(RawRGBA.GetData(), RawRGBA.Num(), ImgWidth, ImgHeight, ERGBFormat::BGRA, 8);

	TArray64<uint8> PNGData = PNGWrapper->GetCompressed(100);
	if (PNGData.Num() == 0)
	{
		UpdateStatusText(TEXT("Failed to encode image as PNG"));
		return;
	}

	// Base64-encode
	TArray<uint8> PNGCompat;
	PNGCompat.SetNumUninitialized(PNGData.Num());
	FMemory::Memcpy(PNGCompat.GetData(), PNGData.GetData(), PNGData.Num());
	FString Base64PNG = FBase64::Encode(PNGCompat);

	// Ensure desired filename has .png extension since we re-encoded
	DesiredFilename = FPaths::GetBaseFilename(DesiredFilename) + TEXT(".png");

	UpdateStatusText(FString::Printf(TEXT("Uploading %s to ComfyUI..."), *DesiredFilename));

	// Upload to ComfyUI server
	if (!HttpClient.IsValid())
	{
		UpdateStatusText(TEXT("No HTTP client available — cannot upload image"));
		return;
	}

	FString ServerFilename;
	if (!HttpClient->UploadImage(Base64PNG, DesiredFilename, ServerFilename))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to upload %s to ComfyUI server"), *DesiredFilename));
		return;
	}

	// Update the node's widget value with the server filename
	if (GraphEditor.IsValid())
	{
		GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, ServerFilename);
	}

	// Create a UTexture2D from the decoded pixels for the node thumbnail
	if (GraphEditor.IsValid())
	{
		UTexture2D* ThumbTex = UTexture2D::CreateTransient(ImgWidth, ImgHeight, PF_B8G8R8A8);
		if (ThumbTex)
		{
			void* MipData = ThumbTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(MipData, RawRGBA.GetData(), RawRGBA.Num());
			ThumbTex->GetPlatformData()->Mips[0].BulkData.Unlock();
			ThumbTex->UpdateResource();
			ThumbTex->AddToRoot(); // prevent GC

			GraphEditor->SetNodeThumbnail(NodeId, ThumbTex);
		}
	}

	// Rebuild details panel to show the new filename and preview
	RebuildNodeDetailsPanel();

	UpdateStatusText(FString::Printf(TEXT("Uploaded %s (%dx%d) — ready to use"), *ServerFilename, ImgWidth, ImgHeight));
}

void SViewGenPanel::OnBrowseNodeVideo(FString NodeId, FString WidgetName)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT);

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Video File"),
		DefaultPath,
		TEXT(""),
		TEXT("Video Files (*.mp4;*.mov;*.avi;*.webm;*.mkv;*.gif)|*.mp4;*.mov;*.avi;*.webm;*.mkv;*.gif|All Files (*.*)|*.*"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0) return;

	const FString FilePath = OutFiles[0];
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(FilePath));

	// Load the raw file bytes
	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to read video file: %s"), *FilePath));
		return;
	}

	FString DesiredFilename = FPaths::GetCleanFilename(FilePath);

	// Determine content type from extension
	FString Ext = FPaths::GetExtension(FilePath).ToLower();
	FString ContentType = TEXT("application/octet-stream");
	if (Ext == TEXT("mp4")) ContentType = TEXT("video/mp4");
	else if (Ext == TEXT("mov")) ContentType = TEXT("video/quicktime");
	else if (Ext == TEXT("avi")) ContentType = TEXT("video/x-msvideo");
	else if (Ext == TEXT("webm")) ContentType = TEXT("video/webm");
	else if (Ext == TEXT("mkv")) ContentType = TEXT("video/x-matroska");
	else if (Ext == TEXT("gif")) ContentType = TEXT("image/gif");

	UpdateStatusText(FString::Printf(TEXT("Uploading %s to ComfyUI... (%s)"),
		*DesiredFilename, *FText::AsMemory(FileBytes.Num()).ToString()));

	if (!HttpClient.IsValid())
	{
		UpdateStatusText(TEXT("No HTTP client available — cannot upload video"));
		return;
	}

	FString ServerFilename;
	if (!HttpClient->UploadRawFile(FileBytes, DesiredFilename, ContentType, ServerFilename))
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to upload %s to ComfyUI server"), *DesiredFilename));
		return;
	}

	// Update the node's widget value and store local path for playback
	if (GraphEditor.IsValid())
	{
		GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, ServerFilename);

		// Store local file path on the node for Play button
		FGraphNode* Node = GraphEditor->FindNodeById(NodeId);
		if (Node)
		{
			Node->LocalFilePath = FilePath;
		}
	}

	// Extract first frame as thumbnail using ffmpeg (if available)
	ExtractVideoThumbnail(FilePath, NodeId);

	// Rebuild details panel to show the new filename and play button
	RebuildNodeDetailsPanel();

	UpdateStatusText(FString::Printf(TEXT("Uploaded video %s — ready to use"), *ServerFilename));
}

// ============================================================================
// Quick Render (Movie Render Graph)
// ============================================================================

void SViewGenPanel::OnQuickRenderForNode(FString NodeId)
{
	// Get the currently opened Level Sequence from the Sequencer editor
	ULevelSequence* ActiveSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (!ActiveSequence)
	{
		UpdateStatusText(TEXT("No Level Sequence is open in the Sequencer — open one first"));
		return;
	}

	// Determine the default output directory for Movie Render output
	// MRG typically outputs to {Project}/Saved/MovieRenders/
	QuickRenderOutputDir = FPaths::ProjectSavedDir() / TEXT("MovieRenders");
	IFileManager::Get().MakeDirectory(*QuickRenderOutputDir, true);

	// Store the target node and start time so the poll timer knows what to look for
	QuickRenderTargetNodeId = NodeId;
	QuickRenderStartTime = FDateTime::UtcNow();

	// Get the Quick Render subsystem via the proper editor subsystem API
	UMovieGraphQuickRenderSubsystem* QuickRenderSubsystem =
		GEditor ? GEditor->GetEditorSubsystem<UMovieGraphQuickRenderSubsystem>() : nullptr;
	if (!QuickRenderSubsystem)
	{
		UpdateStatusText(TEXT("Movie Render Graph Quick Render subsystem not available — is Movie Render Graph plugin enabled?"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Starting Quick Render for sequence '%s', output to '%s'"),
		*ActiveSequence->GetName(), *QuickRenderOutputDir);

	UpdateStatusText(FString::Printf(TEXT("Quick Render: Rendering '%s'..."), *ActiveSequence->GetName()));

	// Create a default settings object — passing nullptr causes an access violation inside the engine.
	// The engine dereferences this pointer internally, so it must be valid.
	UMovieGraphQuickRenderModeSettings* RenderSettings = NewObject<UMovieGraphQuickRenderModeSettings>(GetTransientPackage());

	// Trigger the Quick Render using CurrentViewport mode with default settings
	QuickRenderSubsystem->BeginQuickRender(EMovieGraphQuickRenderMode::CurrentViewport, RenderSettings);

	// Set up a poll timer to watch for the rendered output file
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(QuickRenderPollTimer, FTimerDelegate::CreateRaw(this, &SViewGenPanel::PollQuickRenderOutput),
			1.0f, true); // Poll every 1 second
	}
}

void SViewGenPanel::PollQuickRenderOutput()
{
	if (QuickRenderTargetNodeId.IsEmpty())
	{
		// Cancelled or already handled
		if (GEditor)
		{
			GEditor->GetTimerManager()->ClearTimer(QuickRenderPollTimer);
		}
		return;
	}

	// Safety timeout: 10 minutes
	FTimespan Elapsed = FDateTime::UtcNow() - QuickRenderStartTime;
	if (Elapsed.GetTotalMinutes() > 10.0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Quick Render timed out after 10 minutes"));
		UpdateStatusText(TEXT("Quick Render timed out — check Movie Render Graph output settings"));
		QuickRenderTargetNodeId.Empty();
		if (GEditor) GEditor->GetTimerManager()->ClearTimer(QuickRenderPollTimer);
		return;
	}

	// Scan the output directory for video files created after our start time
	TArray<FString> FoundFiles;
	IFileManager& FM = IFileManager::Get();

	// Recursively find video files in the output directory
	TArray<FString> AllFiles;
	FM.FindFilesRecursive(AllFiles, *QuickRenderOutputDir, TEXT("*.*"), true, false);

	FString NewestVideoPath;
	FDateTime NewestTime = FDateTime::MinValue();

	for (const FString& FilePath : AllFiles)
	{
		FString Ext = FPaths::GetExtension(FilePath).ToLower();
		bool bIsVideo = (Ext == TEXT("avi") || Ext == TEXT("mp4") || Ext == TEXT("mov") || Ext == TEXT("mkv") || Ext == TEXT("webm"));
		if (!bIsVideo) continue;

		FDateTime FileTime = FM.GetTimeStamp(*FilePath);
		if (FileTime > QuickRenderStartTime && FileTime > NewestTime)
		{
			// Verify the file is not still being written (size stable check)
			int64 FileSize = FM.FileSize(*FilePath);
			if (FileSize > 0)
			{
				NewestVideoPath = FilePath;
				NewestTime = FileTime;
			}
		}
	}

	if (NewestVideoPath.IsEmpty())
	{
		return; // Still waiting — poll again
	}

	// Wait one extra poll cycle to make sure the file is fully written
	// (check if size changed since last poll)
	static FString LastSeenPath;
	static int64 LastSeenSize = 0;

	int64 CurrentSize = FM.FileSize(*NewestVideoPath);
	if (NewestVideoPath == LastSeenPath && CurrentSize == LastSeenSize && CurrentSize > 0)
	{
		// File size stable — render is complete!
		LastSeenPath.Empty();
		LastSeenSize = 0;

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Quick Render complete, output: %s (%s)"),
			*NewestVideoPath, *FText::AsMemory(CurrentSize).ToString());

		// Stop polling
		if (GEditor) GEditor->GetTimerManager()->ClearTimer(QuickRenderPollTimer);

		FString TargetNodeId = QuickRenderTargetNodeId;
		QuickRenderTargetNodeId.Empty();

		// Upload the rendered video to ComfyUI and wire it into the LoadVideo node
		TArray<uint8> VideoBytes;
		if (!FFileHelper::LoadFileToArray(VideoBytes, *NewestVideoPath))
		{
			UpdateStatusText(TEXT("Quick Render: Failed to read rendered video file"));
			return;
		}

		FString DesiredFilename = FPaths::GetCleanFilename(NewestVideoPath);
		FString Ext = FPaths::GetExtension(NewestVideoPath).ToLower();
		FString ContentType = TEXT("application/octet-stream");
		if (Ext == TEXT("avi")) ContentType = TEXT("video/x-msvideo");
		else if (Ext == TEXT("mp4")) ContentType = TEXT("video/mp4");
		else if (Ext == TEXT("mov")) ContentType = TEXT("video/quicktime");

		if (!HttpClient.IsValid())
		{
			UpdateStatusText(TEXT("Quick Render: No HTTP client — cannot upload to ComfyUI"));
			return;
		}

		FString ServerFilename;
		if (!HttpClient->UploadRawFile(VideoBytes, DesiredFilename, ContentType, ServerFilename))
		{
			UpdateStatusText(TEXT("Quick Render: Failed to upload video to ComfyUI"));
			return;
		}

		// Wire into the LoadVideo node
		if (GraphEditor.IsValid())
		{
			GraphEditor->CommitWidgetValueSilent(TargetNodeId, TEXT("file"), ServerFilename);

			FGraphNode* Node = GraphEditor->FindNodeById(TargetNodeId);
			if (Node)
			{
				Node->LocalFilePath = NewestVideoPath;
			}
		}

		// Extract thumbnail
		ExtractVideoThumbnail(NewestVideoPath, TargetNodeId);

		// Rebuild details panel
		RebuildNodeDetailsPanel();

		UpdateStatusText(FString::Printf(TEXT("Quick Render complete! Loaded '%s' into node"),
			*FPaths::GetCleanFilename(NewestVideoPath)));
	}
	else
	{
		// File still being written — update tracking
		LastSeenPath = NewestVideoPath;
		LastSeenSize = CurrentSize;
	}
}

// ============================================================================

void SViewGenPanel::ExtractVideoThumbnail(const FString& VideoFilePath, const FString& NodeId)
{
	if (!GraphEditor.IsValid()) return;

	// Use the user-configured FFmpeg path, falling back to bare "ffmpeg" (system PATH)
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString FFmpegPath = (Settings && !Settings->FFmpegPath.IsEmpty()) ? Settings->FFmpegPath : TEXT("ffmpeg");

	// Build a temp output path for the extracted frame
	FString TempDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoThumbs");
	IFileManager::Get().MakeDirectory(*TempDir, true);
	FString TempPNG = TempDir / FString::Printf(TEXT("thumb_%s.png"), *FPaths::GetBaseFilename(VideoFilePath));

	// Run ffmpeg to extract frame 0 as PNG:  ffmpeg -y -i "video.mp4" -vframes 1 -f image2 "thumb.png"
	FString Args = FString::Printf(TEXT("-y -i \"%s\" -vframes 1 -f image2 \"%s\""), *VideoFilePath, *TempPNG);

	int32 ReturnCode = -1;
	FPlatformProcess::ExecProcess(*FFmpegPath, *Args, &ReturnCode, nullptr, nullptr);

	if (ReturnCode != 0 || !FPaths::FileExists(TempPNG))
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: ffmpeg not available or failed to extract video thumbnail (return code %d). "
			"Video nodes will not show a preview. Install ffmpeg and add it to PATH for video thumbnails."), ReturnCode);
		return;
	}

	// Load the extracted PNG
	TArray<uint8> PNGBytes;
	if (!FFileHelper::LoadFileToArray(PNGBytes, *TempPNG))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to read extracted thumbnail: %s"), *TempPNG);
		return;
	}

	// Decode PNG to RGBA pixels
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PNGWrapper->SetCompressed(PNGBytes.GetData(), PNGBytes.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to decode video thumbnail PNG"));
		return;
	}

	TArray<uint8> RawRGBA;
	if (!PNGWrapper->GetRaw(ERGBFormat::BGRA, 8, RawRGBA))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to decompress video thumbnail pixels"));
		return;
	}

	int32 ImgWidth = PNGWrapper->GetWidth();
	int32 ImgHeight = PNGWrapper->GetHeight();

	// Create UTexture2D for the node thumbnail
	UTexture2D* ThumbTex = UTexture2D::CreateTransient(ImgWidth, ImgHeight, PF_B8G8R8A8);
	if (ThumbTex)
	{
		void* MipData = ThumbTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, RawRGBA.GetData(), RawRGBA.Num());
		ThumbTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		ThumbTex->UpdateResource();
		ThumbTex->AddToRoot(); // prevent GC

		GraphEditor->SetNodeThumbnail(NodeId, ThumbTex);

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Set video thumbnail for node %s (%dx%d)"), *NodeId, ImgWidth, ImgHeight);
	}

	// Clean up temp file
	IFileManager::Get().Delete(*TempPNG, false, true);
}

void SViewGenPanel::HandleVideoResult()
{
	if (!HttpClient.IsValid()) return;

	FString VideoFilename = HttpClient->GetLastResultFilename();
	FString VideoSubfolder = HttpClient->GetLastResultSubfolder();
	FString VideoFolderType = HttpClient->GetLastResultFolderType();

	if (VideoFilename.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: HandleVideoResult called but no result filename available"));
		return;
	}

	// Build local save path
	FString VideoDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoResults");
	IFileManager::Get().MakeDirectory(*VideoDir, true);
	FString LocalVideoPath = VideoDir / VideoFilename;

	// Capture for lambda
	FString CapturedFilename = VideoFilename;
	FString CapturedSubfolder = VideoSubfolder;
	FString CapturedFolderType = VideoFolderType;
	FString CapturedLocalPath = LocalVideoPath;

	UpdateStatusText(FString::Printf(TEXT("Downloading video: %s..."), *VideoFilename));

	HttpClient->DownloadComfyUIFile(CapturedFilename, CapturedSubfolder, CapturedFolderType, CapturedLocalPath,
		[this, CapturedLocalPath, CapturedFilename](const FString& DownloadedPath)
	{
		if (DownloadedPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to download video result: %s"), *CapturedFilename);
			UpdateStatusText(TEXT("Video generation complete (download failed — check Output Log)"));
			return;
		}

		LastVideoResultPath = DownloadedPath;
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Video downloaded to: %s"), *DownloadedPath);

		// Extract first-frame thumbnail using ffmpeg
		const UGenAISettings* Settings = UGenAISettings::Get();
		FString FFmpegExe = (Settings && !Settings->FFmpegPath.IsEmpty()) ? Settings->FFmpegPath : TEXT("ffmpeg");

		FString ThumbDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoThumbs");
		IFileManager::Get().MakeDirectory(*ThumbDir, true);
		FString ThumbPNG = ThumbDir / FString::Printf(TEXT("result_%s.png"), *FPaths::GetBaseFilename(CapturedFilename));

		FString Args = FString::Printf(TEXT("-y -i \"%s\" -vframes 1 -f image2 \"%s\""), *DownloadedPath, *ThumbPNG);
		int32 ReturnCode = -1;
		FPlatformProcess::ExecProcess(*FFmpegExe, *Args, &ReturnCode, nullptr, nullptr);

		if (ReturnCode != 0 || !FPaths::FileExists(ThumbPNG))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: ffmpeg failed to extract video thumbnail (code %d). "
				"Set the FFmpeg Path in Video settings to enable video previews."), ReturnCode);
			UpdateStatusText(FString::Printf(TEXT("Video complete: %s (no thumbnail — FFmpeg not found)"),
				*FPaths::GetCleanFilename(DownloadedPath)));
			return;
		}

		// Load and decode the thumbnail PNG
		TArray<uint8> PNGBytes;
		if (!FFileHelper::LoadFileToArray(PNGBytes, *ThumbPNG))
		{
			UpdateStatusText(FString::Printf(TEXT("Video complete: %s"), *FPaths::GetCleanFilename(DownloadedPath)));
			return;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!PNGWrapper->SetCompressed(PNGBytes.GetData(), PNGBytes.Num()))
		{
			UpdateStatusText(FString::Printf(TEXT("Video complete: %s"), *FPaths::GetCleanFilename(DownloadedPath)));
			return;
		}

		TArray<uint8> RawRGBA;
		if (!PNGWrapper->GetRaw(ERGBFormat::BGRA, 8, RawRGBA))
		{
			UpdateStatusText(FString::Printf(TEXT("Video complete: %s"), *FPaths::GetCleanFilename(DownloadedPath)));
			return;
		}

		int32 ImgWidth = PNGWrapper->GetWidth();
		int32 ImgHeight = PNGWrapper->GetHeight();

		UTexture2D* ThumbTex = UTexture2D::CreateTransient(ImgWidth, ImgHeight, PF_B8G8R8A8);
		if (!ThumbTex)
		{
			UpdateStatusText(FString::Printf(TEXT("Video complete: %s"), *FPaths::GetCleanFilename(DownloadedPath)));
			return;
		}

		void* MipData = ThumbTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, RawRGBA.GetData(), RawRGBA.Num());
		ThumbTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		ThumbTex->UpdateResource();
		ThumbTex->AddToRoot(); // prevent GC

		// Add to image history as a video result
		FHistoryEntry VideoEntry;
		VideoEntry.Texture = ThumbTex;
		VideoEntry.VideoPath = DownloadedPath;

		TSharedPtr<FSlateBrush> ThumbBrush = MakeShareable(new FSlateBrush());
		ThumbBrush->SetResourceObject(ThumbTex);
		ThumbBrush->ImageSize = FVector2D(ImgWidth, ImgHeight);
		ThumbBrush->DrawAs = ESlateBrushDrawType::Image;
		VideoEntry.Brush = ThumbBrush;

		// Trim history if needed
		if (ImageHistory.Num() >= MaxHistoryEntries)
		{
			ImageHistory.RemoveAt(0);
		}
		ImageHistory.Add(MoveTemp(VideoEntry));
		HistoryIndex = ImageHistory.Num() - 1;

		// Update the main preview
		PreviewBrush->SetResourceObject(ThumbTex);
		PreviewBrush->ImageSize = FVector2D(ImgWidth, ImgHeight);
		PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
		if (PreviewImage.IsValid())
		{
			PreviewImage->SetImage(PreviewBrush.Get());
		}

		RebuildResultGallery();

		// Clean up temp thumbnail
		IFileManager::Get().Delete(*ThumbPNG, false, true);

		UpdateStatusText(FString::Printf(TEXT("Video complete: %s — click preview to play"),
			*FPaths::GetCleanFilename(DownloadedPath)));

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Video result added to history with thumbnail (%dx%d): %s"),
			ImgWidth, ImgHeight, *DownloadedPath);
	});
}

void SViewGenPanel::OnBrowse3DModelFile(FString NodeId)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT);

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select 3D Model"),
		DefaultPath,
		TEXT(""),
		TEXT("3D Model Files (*.glb;*.gltf;*.obj;*.fbx;*.stl;*.ply)|*.glb;*.gltf;*.obj;*.fbx;*.stl;*.ply|All Files (*.*)|*.*"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0) return;

	const FString FilePath = OutFiles[0];
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(FilePath));

	if (!GraphEditor.IsValid()) return;

	// Read the node's current settings
	const FGraphNode* FoundNode = nullptr;
	for (const FGraphNode& N : GraphEditor->GetNodes())
	{
		if (N.Id == NodeId) { FoundNode = &N; break; }
	}
	if (!FoundNode) return;
	const FGraphNode& Node = *FoundNode;

	FString AssetPath = TEXT("/Game/ViewGen/Models");
	FString AssetName;
	bool bPlaceInLevel = true;

	if (const FString* Val = Node.WidgetValues.Find(TEXT("asset_path")))
	{
		if (!Val->IsEmpty()) AssetPath = *Val;
	}
	if (const FString* Val = Node.WidgetValues.Find(TEXT("asset_name")))
	{
		AssetName = *Val;
	}
	if (const FString* Val = Node.WidgetValues.Find(TEXT("place_in_level")))
	{
		bPlaceInLevel = (*Val == TEXT("true"));
	}

	// Use original filename if no asset name override specified
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(FilePath);
	}

	// Update the file_path widget to show the selected file
	GraphEditor->CommitWidgetValueSilent(NodeId, TEXT("file_path"), FilePath);

	UpdateStatusText(FString::Printf(TEXT("Importing %s..."), *FPaths::GetCleanFilename(FilePath)));

	// Import into the content browser
	FString ImportedAssetPath = ImportModelFileToContentBrowser(FilePath, AssetPath, AssetName);
	if (ImportedAssetPath.IsEmpty())
	{
		UpdateStatusText(FString::Printf(TEXT("Failed to import model: %s"), *FPaths::GetCleanFilename(FilePath)));
		RebuildNodeDetailsPanel();
		return;
	}

	UpdateStatusText(FString::Printf(TEXT("Imported to %s"), *ImportedAssetPath));

	// Optionally place in the current level
	if (bPlaceInLevel)
	{
		AActor* SpawnedActor = PlaceAssetInLevel(ImportedAssetPath);
		if (SpawnedActor)
		{
			UpdateStatusText(FString::Printf(TEXT("Imported and placed '%s' in level"), *AssetName));
		}
	}

	// Load the static mesh and show 3D preview on the node
	UStaticMesh* LoadedMesh = LoadObject<UStaticMesh>(nullptr, *ImportedAssetPath);
	if (LoadedMesh)
	{
		GraphEditor->SetNodeMeshPreview(NodeId, LoadedMesh);
	}

	RebuildNodeDetailsPanel();
}

void SViewGenPanel::OnBrowseAssetPath(FString NodeId, FString WidgetName)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !GraphEditor.IsValid()) return;

	// Start browsing from the project's Content directory
	FString ContentDir = FPaths::ProjectContentDir();
	FPaths::NormalizeDirectoryName(ContentDir);

	FString SelectedFolder;
	const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Asset Folder"),
		ContentDir,
		SelectedFolder
	);

	if (!bOpened || SelectedFolder.IsEmpty()) return;

	// Convert the absolute disk path to a Content Browser path (/Game/...)
	FString GamePath;
	if (FPackageName::TryConvertFilenameToLongPackageName(SelectedFolder, GamePath))
	{
		// TryConvertFilenameToLongPackageName should give us /Game/... format
		GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, GamePath);
		RebuildNodeDetailsPanel();
	}
	else
	{
		// If the selected folder is under Content/, do a manual conversion
		FString AbsContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FPaths::NormalizeDirectoryName(AbsContentDir);
		FString AbsSelected = FPaths::ConvertRelativePathToFull(SelectedFolder);
		FPaths::NormalizeDirectoryName(AbsSelected);

		if (AbsSelected.StartsWith(AbsContentDir))
		{
			FString RelativePath = AbsSelected.Mid(AbsContentDir.Len());
			if (!RelativePath.StartsWith(TEXT("/")))
			{
				RelativePath = TEXT("/") + RelativePath;
			}
			GamePath = TEXT("/Game") + RelativePath;
			GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, GamePath);
			RebuildNodeDetailsPanel();
		}
		else
		{
			// Selected folder is outside the Content directory — just use as-is with a warning
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Selected folder '%s' is outside the Content directory"), *SelectedFolder);
			GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, SelectedFolder);
			RebuildNodeDetailsPanel();
		}
	}
}

void SViewGenPanel::OnBrowseDiskFolder(FString NodeId, FString WidgetName)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !GraphEditor.IsValid()) return;

	// Use the current value as the starting directory when it looks like a path
	FString DefaultPath = FPaths::ProjectSavedDir();
	FGraphNode* Node = GraphEditor->FindNodeById(NodeId);
	if (Node)
	{
		const FString* CurrentVal = Node->WidgetValues.Find(WidgetName);
		if (CurrentVal && !CurrentVal->IsEmpty() && FPaths::DirectoryExists(*CurrentVal))
		{
			DefaultPath = *CurrentVal;
		}
	}
	FPaths::NormalizeDirectoryName(DefaultPath);

	FString SelectedFolder;
	const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Output Folder"),
		DefaultPath,
		SelectedFolder
	);

	if (!bOpened || SelectedFolder.IsEmpty()) return;

	// For SaveImage's filename_prefix: ComfyUI uses subdirectories in the prefix
	// to organise outputs (e.g. "subfolder/prefix"). Convert the disk path
	// into a relative subfolder under ComfyUI's output directory, or use the
	// leaf folder name as the prefix subdirectory.
	if (Node && WidgetName == TEXT("filename_prefix") && Node->ClassType.Contains(TEXT("SaveImage")))
	{
		FString FolderName = FPaths::GetCleanFilename(SelectedFolder);
		if (FolderName.IsEmpty()) FolderName = TEXT("output");
		GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, FolderName + TEXT("/ComfyUI"));
	}
	else
	{
		GraphEditor->CommitWidgetValueSilent(NodeId, WidgetName, SelectedFolder);
	}
	RebuildNodeDetailsPanel();
}

void SViewGenPanel::OnPromptTextChanged(const FText& NewText)
{
	if (bAutoSyncPrompts && !bSyncInProgress)
	{
		PerformPromptSync();
	}
}

void SViewGenPanel::OnNegativePromptTextChanged(const FText& NewText)
{
	if (bAutoSyncPrompts && !bSyncInProgress)
	{
		PerformPromptSync();
	}
}

void SViewGenPanel::PerformPromptSync()
{
	// Re-entrancy guard: setting VideoPromptTextBox->SetText fires OnTextChanged
	// on those text boxes, which could cascade back here.
	if (bSyncInProgress)
	{
		return;
	}
	bSyncInProgress = true;

	FString Prompt;
	FString NegativePrompt;

	// Read from the main prompt text boxes
	if (PromptTextBox.IsValid())
	{
		Prompt = PromptTextBox->GetText().ToString();
	}
	if (NegativePromptTextBox.IsValid())
	{
		NegativePrompt = NegativePromptTextBox->GetText().ToString();
	}

	// 1. Sync to video prompt text boxes
	if (VideoPromptTextBox.IsValid())
	{
		VideoPromptTextBox->SetText(FText::FromString(Prompt));
	}
	if (VideoNegativePromptTextBox.IsValid())
	{
		VideoNegativePromptTextBox->SetText(FText::FromString(NegativePrompt));
	}

	// 2. Sync to all prompt-bearing nodes in the graph editor
	if (GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0)
	{
		GraphEditor->SetAllPromptTexts(Prompt, NegativePrompt);
	}

	// 3. Persist to settings (guard against GC'd settings during shutdown)
	if (UGenAISettings* Settings = UGenAISettings::Get())
	{
		if (IsValid(Settings))
		{
			Settings->VideoPrompt = Prompt;
			Settings->VideoNegativePrompt = NegativePrompt;
			Settings->SaveConfig();
		}
	}

	bSyncInProgress = false;
}

FReply SViewGenPanel::OnGenerateFromGraphClicked()
{
	if (!GraphEditor.IsValid() || !HttpClient.IsValid())
	{
		UpdateStatusText(TEXT("Graph editor or HTTP client not available"));
		return FReply::Handled();
	}

	if (HttpClient->IsRequestInProgress())
	{
		UpdateStatusText(TEXT("A generation is already in progress"));
		return FReply::Handled();
	}

	// Check for Sequence node — if present, use staged execution
	if (GraphEditor->HasSequenceNode())
	{
		bool bNeedsViewport = false;
		bool bNeedsDepth = false;
		bool bNeedsSegmentation = false;
		FString CameraDescription;

		StagedWorkflowQueue = GraphEditor->ExportStagedWorkflows(
			&bNeedsViewport, &bNeedsDepth, &CameraDescription, &bNeedsSegmentation);

		if (StagedWorkflowQueue.Num() == 0)
		{
			UpdateStatusText(TEXT("Sequence node found but no stages have connected nodes"));
			return FReply::Handled();
		}

		// Resolve UE source markers on all staged workflows
		if (bNeedsViewport && ViewportCapture.IsValid())
		{
			if (!ViewportCapture->HasCapture())
			{
				ViewportCapture->CaptureActiveViewport();
				UpdateThumbnailBrush(ViewportThumbnailBrush,
					ViewportCapture->GetCapturedTexture(), ViewportThumbnailImage);
			}
			FString ViewportBase64 = ViewportCapture->GetBase64PNG();
			FString ServerFilename;
			if (HttpClient->UploadImage(ViewportBase64, TEXT("ue_viewport.png"), ServerFilename))
			{
				for (auto& StageWF : StagedWorkflowQueue)
				{
					ResolveUEImageMarker(StageWF, UEViewportMarker, ServerFilename);
				}
			}
		}
		if (bNeedsDepth && DepthRenderer.IsValid())
		{
			if (!DepthRenderer->HasCapture())
			{
				DepthRenderer->CaptureDepth();
			}
			FString DepthBase64 = DepthRenderer->GetBase64PNG();
			FString ServerFilename;
			if (HttpClient->UploadImage(DepthBase64, TEXT("ue_depth.png"), ServerFilename))
			{
				for (auto& StageWF : StagedWorkflowQueue)
				{
					ResolveUEImageMarker(StageWF, UEDepthMapMarker, ServerFilename);
				}
			}
		}
		if (!CameraDescription.IsEmpty() && ViewportCapture.IsValid() && ViewportCapture->HasCameraData())
		{
			FString CamDesc = ViewportCapture->BuildCameraPromptDescription();
			for (auto& StageWF : StagedWorkflowQueue)
			{
				ResolveUECameraData(StageWF, CamDesc);
			}
		}

		// Resolve UE Video to Image markers for staged workflows
		for (const FGraphNode& VNode : GraphEditor->GetNodes())
		{
			if (VNode.ClassType != UEVideoToImageClassType) continue;
			if (VNode.LocalFilePath.IsEmpty() || !FPaths::FileExists(VNode.LocalFilePath))
			{
				UpdateStatusText(FString::Printf(TEXT("UE Video to Image node '%s' has no video file — browse for one first"), *VNode.Title));
				return FReply::Handled();
			}

			int32 FrameNum = 0;
			const FString* FrameVal = VNode.WidgetValues.Find(TEXT("frame"));
			if (FrameVal) FrameNum = FCString::Atoi(**FrameVal);

			const UGenAISettings* Settings = UGenAISettings::Get();
			FString FFmpegExe = (Settings && !Settings->FFmpegPath.IsEmpty()) ? Settings->FFmpegPath : TEXT("ffmpeg");

			FString TempDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoFrames");
			IFileManager::Get().MakeDirectory(*TempDir, true);
			FString TempPNG = TempDir / FString::Printf(TEXT("frame_%s_%d.png"), *VNode.Id, FrameNum);

			FString Args;
			if (FrameNum == 0)
			{
				Args = FString::Printf(TEXT("-y -i \"%s\" -vframes 1 -f image2 \"%s\""), *VNode.LocalFilePath, *TempPNG);
			}
			else
			{
				Args = FString::Printf(TEXT("-y -i \"%s\" -vf \"select=eq(n\\,%d)\" -vframes 1 -f image2 \"%s\""),
					*VNode.LocalFilePath, FrameNum, *TempPNG);
			}

			int32 ReturnCode = -1;
			FPlatformProcess::ExecProcess(*FFmpegExe, *Args, &ReturnCode, nullptr, nullptr);

			if (ReturnCode != 0 || !FPaths::FileExists(TempPNG))
			{
				UpdateStatusText(FString::Printf(TEXT("FFmpeg failed to extract frame %d (staged). Check FFmpeg Path in settings."), FrameNum));
				return FReply::Handled();
			}

			TArray<uint8> FrameBytes;
			if (!FFileHelper::LoadFileToArray(FrameBytes, *TempPNG))
			{
				UpdateStatusText(TEXT("Failed to read extracted video frame (staged)"));
				return FReply::Handled();
			}

			FString ServerFilename;
			FString DesiredName = FString::Printf(TEXT("ue_videoframe_%s_%d.png"), *FPaths::GetBaseFilename(VNode.LocalFilePath), FrameNum);
			if (!HttpClient->UploadRawFile(FrameBytes, DesiredName, TEXT("image/png"), ServerFilename))
			{
				UpdateStatusText(TEXT("Failed to upload video frame to ComfyUI (staged)"));
				return FReply::Handled();
			}

			FString Marker = UEVideoFrameMarkerPrefix + VNode.Id;
			for (auto& StageWF : StagedWorkflowQueue)
			{
				ResolveUEImageMarker(StageWF, Marker, ServerFilename);
			}
		}

		// Start staged execution
		TotalStages = StagedWorkflowQueue.Num();
		CurrentStageIndex = -1; // Will be incremented to 0 by SubmitNextStagedWorkflow
		bIsGenerating = true;

		UpdateStatusText(FString::Printf(TEXT("Sequence: Starting stage 1 of %d..."), TotalStages));

		SubmitNextStagedWorkflow();
		return FReply::Handled();
	}

	// ---- Standard (non-sequenced) export ----

	// Export the graph as ComfyUI workflow JSON, with UE source node resolution
	bool bNeedsViewport = false;
	bool bNeedsDepth = false;
	bool bNeedsSegmentation = false;
	FString CameraDescription;

	TSharedPtr<FJsonObject> Workflow = GraphEditor->ExportWorkflowJSON(
		&bNeedsViewport, &bNeedsDepth, &CameraDescription, &bNeedsSegmentation);

	if (!Workflow.IsValid() || Workflow->Values.Num() == 0)
	{
		UpdateStatusText(TEXT("Graph is empty — add some nodes first"));
		return FReply::Handled();
	}

	// ---- Resolve UE source node markers ----

	// Capture and upload viewport image if needed
	if (bNeedsViewport && ViewportCapture.IsValid())
	{
		// Reuse existing viewport capture if available (from thumbnail),
		// otherwise capture fresh from the viewport.
		if (!ViewportCapture->HasCapture())
		{
			UpdateStatusText(TEXT("Capturing viewport..."));

			if (!ViewportCapture->CaptureActiveViewport())
			{
				UpdateStatusText(TEXT("Failed to capture viewport — is a level editor viewport open?"));
				return FReply::Handled();
			}

			// Update the thumbnail with the fresh capture
			UpdateThumbnailBrush(ViewportThumbnailBrush,
				ViewportCapture->GetCapturedTexture(), ViewportThumbnailImage);
		}
		else
		{
			UpdateStatusText(TEXT("Reusing existing viewport capture..."));
		}

		FString ViewportBase64 = ViewportCapture->GetBase64PNG();
		if (ViewportBase64.IsEmpty())
		{
			UpdateStatusText(TEXT("Viewport capture produced empty image"));
			return FReply::Handled();
		}

		FString ServerFilename;
		if (!HttpClient->UploadImage(ViewportBase64, TEXT("ue_viewport.png"), ServerFilename))
		{
			UpdateStatusText(TEXT("Failed to upload viewport image to ComfyUI"));
			return FReply::Handled();
		}

		// Replace the marker in all LoadImage nodes that reference it
		ResolveUEImageMarker(Workflow, UEViewportMarker, ServerFilename);
	}

	// Capture and upload depth map if needed
	if (bNeedsDepth && DepthRenderer.IsValid())
	{
		// Try to read max_depth from the workflow meta
		float MaxDepth = 50000.0f;
		for (const auto& Pair : Workflow->Values)
		{
			TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
			if (NodeObj.IsValid())
			{
				TSharedPtr<FJsonObject> Meta = NodeObj->GetObjectField(TEXT("_meta"));
				if (Meta.IsValid() && Meta->HasField(TEXT("ue_max_depth")))
				{
					FString DepthStr = Meta->GetStringField(TEXT("ue_max_depth"));
					MaxDepth = FCString::Atof(*DepthStr);
				}
			}
		}

		// Reuse existing depth capture if available and max_depth hasn't changed,
		// otherwise re-capture from the viewport.
		bool bNeedsNewCapture = !DepthRenderer->HasCapture()
			|| !FMath::IsNearlyEqual(DepthRenderer->GetMaxDepthDistance(), MaxDepth, 0.1f);

		if (bNeedsNewCapture)
		{
			UpdateStatusText(TEXT("Capturing depth map..."));

			if (!DepthRenderer->CaptureDepth(MaxDepth))
			{
				UpdateStatusText(TEXT("Failed to capture depth map"));
				return FReply::Handled();
			}

			// Update the thumbnail with the fresh capture
			UpdateThumbnailBrush(DepthThumbnailBrush,
				DepthRenderer->GetDepthTexture(), DepthThumbnailImage);
		}
		else
		{
			UpdateStatusText(TEXT("Reusing existing depth capture..."));
		}

		FString DepthBase64 = DepthRenderer->GetBase64PNG();
		if (DepthBase64.IsEmpty())
		{
			UpdateStatusText(TEXT("Depth capture produced empty image"));
			return FReply::Handled();
		}

		FString ServerFilename;
		if (!HttpClient->UploadImage(DepthBase64, TEXT("ue_depth.png"), ServerFilename))
		{
			UpdateStatusText(TEXT("Failed to upload depth map to ComfyUI"));
			return FReply::Handled();
		}

		ResolveUEImageMarker(Workflow, UEDepthMapMarker, ServerFilename);
	}

	// Capture and upload segmentation mask if needed
	if (bNeedsSegmentation && SegmentationCapture.IsValid())
	{
		UpdateStatusText(TEXT("Capturing segmentation mask..."));

		// Read the mode from workflow metadata
		FString SegMode = TEXT("Actor ID");
		for (const auto& Pair : Workflow->Values)
		{
			TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
			if (NodeObj.IsValid())
			{
				TSharedPtr<FJsonObject> Meta = NodeObj->GetObjectField(TEXT("_meta"));
				if (Meta.IsValid() && Meta->HasField(TEXT("ue_segmentation_mode")))
				{
					SegMode = Meta->GetStringField(TEXT("ue_segmentation_mode"));
				}
			}
		}

		if (!SegmentationCapture->CaptureSegmentation(SegMode))
		{
			UpdateStatusText(TEXT("Failed to capture segmentation mask"));
			return FReply::Handled();
		}

		FString SegBase64 = SegmentationCapture->GetBase64PNG();
		if (SegBase64.IsEmpty())
		{
			UpdateStatusText(TEXT("Segmentation capture produced empty image"));
			return FReply::Handled();
		}

		FString ServerFilename;
		if (!HttpClient->UploadImage(SegBase64, TEXT("ue_segmentation.png"), ServerFilename))
		{
			UpdateStatusText(TEXT("Failed to upload segmentation mask to ComfyUI"));
			return FReply::Handled();
		}

		ResolveUEImageMarker(Workflow, UESegmentationMarker, ServerFilename);

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Segmentation mask uploaded (%d segments found)"),
			SegmentationCapture->GetSegmentCount());
	}

	// Inject camera data into target text fields
	if (!CameraDescription.IsEmpty() && ViewportCapture.IsValid())
	{
		// Build the camera description from the viewport capture
		if (!ViewportCapture->HasCameraData())
		{
			// Capture viewport just for camera data if we haven't already
			ViewportCapture->CaptureActiveViewport();
		}

		if (ViewportCapture->HasCameraData())
		{
			FString CamDesc = ViewportCapture->BuildCameraPromptDescription();
			ResolveUECameraData(Workflow, CamDesc);
		}
	}

	// Resolve UE Video to Image markers — extract frames via FFmpeg and upload
	if (GraphEditor.IsValid())
	{
		for (const FGraphNode& VNode : GraphEditor->GetNodes())
		{
			if (VNode.ClassType != UEVideoToImageClassType) continue;
			if (VNode.LocalFilePath.IsEmpty() || !FPaths::FileExists(VNode.LocalFilePath))
			{
				UpdateStatusText(FString::Printf(TEXT("UE Video to Image node '%s' has no video file — browse for one first"), *VNode.Title));
				return FReply::Handled();
			}

			// Determine which frame to extract
			int32 FrameNum = 0;
			const FString* FrameVal = VNode.WidgetValues.Find(TEXT("frame"));
			if (FrameVal) FrameNum = FCString::Atoi(**FrameVal);

			// Extract frame via FFmpeg
			const UGenAISettings* Settings = UGenAISettings::Get();
			FString FFmpegExe = (Settings && !Settings->FFmpegPath.IsEmpty()) ? Settings->FFmpegPath : TEXT("ffmpeg");

			FString TempDir = FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("VideoFrames");
			IFileManager::Get().MakeDirectory(*TempDir, true);
			FString TempPNG = TempDir / FString::Printf(TEXT("frame_%s_%d.png"), *VNode.Id, FrameNum);

			// Build FFmpeg command: select specific frame using video filter
			FString Args;
			if (FrameNum == 0)
			{
				Args = FString::Printf(TEXT("-y -i \"%s\" -vframes 1 -f image2 \"%s\""), *VNode.LocalFilePath, *TempPNG);
			}
			else
			{
				Args = FString::Printf(TEXT("-y -i \"%s\" -vf \"select=eq(n\\,%d)\" -vframes 1 -f image2 \"%s\""),
					*VNode.LocalFilePath, FrameNum, *TempPNG);
			}

			int32 ReturnCode = -1;
			FPlatformProcess::ExecProcess(*FFmpegExe, *Args, &ReturnCode, nullptr, nullptr);

			if (ReturnCode != 0 || !FPaths::FileExists(TempPNG))
			{
				UpdateStatusText(FString::Printf(TEXT("FFmpeg failed to extract frame %d from video (return code %d). Check FFmpeg Path in settings."), FrameNum, ReturnCode));
				return FReply::Handled();
			}

			// Read the extracted frame and upload to ComfyUI
			TArray<uint8> FrameBytes;
			if (!FFileHelper::LoadFileToArray(FrameBytes, *TempPNG))
			{
				UpdateStatusText(TEXT("Failed to read extracted video frame"));
				return FReply::Handled();
			}

			FString ServerFilename;
			FString DesiredName = FString::Printf(TEXT("ue_videoframe_%s_%d.png"), *FPaths::GetBaseFilename(VNode.LocalFilePath), FrameNum);
			if (!HttpClient->UploadRawFile(FrameBytes, DesiredName, TEXT("image/png"), ServerFilename))
			{
				UpdateStatusText(TEXT("Failed to upload extracted video frame to ComfyUI"));
				return FReply::Handled();
			}

			// Replace the marker in the workflow
			FString Marker = UEVideoFrameMarkerPrefix + VNode.Id;
			ResolveUEImageMarker(Workflow, Marker, ServerFilename);

			UE_LOG(LogTemp, Log, TEXT("ViewGen: Video to Image node '%s': extracted frame %d from '%s' → uploaded as '%s'"),
				*VNode.Id, FrameNum, *FPaths::GetCleanFilename(VNode.LocalFilePath), *ServerFilename);
		}
	}

	// Check if the graph has a Meshy node — if so, prepare for UE-side Meshy API call
	PendingMeshyImageFilename.Empty();
	PendingMeshyImageBase64.Empty();

	FString MeshySourceImage = GraphEditor->GetMeshySourceImageFilename();
	if (!MeshySourceImage.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Meshy node detected, source image: %s"), *MeshySourceImage);

		if (MeshySourceImage == UEViewportMarker)
		{
			// The Meshy node's input is a viewport capture — we already have it
			if (ViewportCapture.IsValid())
			{
				PendingMeshyImageBase64 = ViewportCapture->GetBase64PNG();
			}
		}
		else
		{
			// The Meshy node's input is a ComfyUI image file — store the filename
			// so we can download it after the workflow finishes
			PendingMeshyImageFilename = MeshySourceImage;
		}
	}

	// If the workflow is empty after removing Meshy nodes (only LoadImage nodes remain
	// that feed into nothing), skip ComfyUI submission and go straight to Meshy
	bool bWorkflowHasComfyUIWork = false;
	for (const auto& Pair : Workflow->Values)
	{
		TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
		if (NodeObj.IsValid())
		{
			FString ClassType;
			if (NodeObj->TryGetStringField(TEXT("class_type"), ClassType))
			{
				if (ClassType != TEXT("LoadImage"))
				{
					bWorkflowHasComfyUIWork = true;
					break;
				}
			}
		}
	}

	if (!bWorkflowHasComfyUIWork && !PendingMeshyImageFilename.IsEmpty())
	{
		// No real ComfyUI work — skip submission and go straight to Meshy
		UE_LOG(LogTemp, Log, TEXT("ViewGen: No ComfyUI processing needed, triggering Meshy directly"));
		bIsGenerating = true;
		UpdateStatusText(TEXT("Starting Meshy 3D generation..."));

		// Signal completion immediately to trigger the Meshy flow
		OnGenerationComplete(true, nullptr);
		return FReply::Handled();
	}

	if (!bWorkflowHasComfyUIWork && !PendingMeshyImageBase64.IsEmpty())
	{
		// No real ComfyUI work, but we have the viewport image ready
		UE_LOG(LogTemp, Log, TEXT("ViewGen: No ComfyUI processing needed, triggering Meshy directly from viewport"));
		bIsGenerating = true;
		UpdateStatusText(TEXT("Starting Meshy 3D generation..."));
		OnGenerationComplete(true, nullptr);
		return FReply::Handled();
	}

	UpdateStatusText(TEXT("Submitting graph workflow to ComfyUI..."));

	// Submit directly
	HttpClient->SubmitWorkflowDirect(Workflow);

	// Start progress polling
	if (!ProgressPollTimer.IsValid())
	{
		GEditor->GetTimerManager()->SetTimer(
			ProgressPollTimer,
			FTimerDelegate::CreateLambda([this]()
			{
				if (HttpClient.IsValid())
				{
					HttpClient->PollProgress();
				}
			}),
			1.0f,
			true
		);
	}

	bIsGenerating = true;
	return FReply::Handled();
}

void SViewGenPanel::SubmitNextStagedWorkflow()
{
	CurrentStageIndex++;

	if (CurrentStageIndex >= TotalStages || CurrentStageIndex >= StagedWorkflowQueue.Num())
	{
		// All stages complete
		StagedWorkflowQueue.Empty();
		CurrentStageIndex = -1;
		TotalStages = 0;

		bIsGenerating = false;
		CurrentProgress = 1.0f;

		if (GraphEditor.IsValid())
		{
			GraphEditor->ClearExecutingNodes();
			GraphEditor->SetOverlayText(FString());
		}

		UpdateStatusText(TEXT("Sequence: All stages complete!"));
		return;
	}

	TSharedPtr<FJsonObject> StageWorkflow = StagedWorkflowQueue[CurrentStageIndex];
	if (!StageWorkflow.IsValid() || StageWorkflow->Values.Num() == 0)
	{
		// Empty stage — skip to next
		UE_LOG(LogTemp, Log, TEXT("ViewGen Sequence: Stage %d is empty, skipping"), CurrentStageIndex + 1);
		SubmitNextStagedWorkflow();
		return;
	}

	UpdateStatusText(FString::Printf(TEXT("Sequence: Running stage %d of %d..."),
		CurrentStageIndex + 1, TotalStages));

	if (GraphEditor.IsValid())
	{
		GraphEditor->SetOverlayText(FString::Printf(TEXT("Stage %d / %d"),
			CurrentStageIndex + 1, TotalStages));
	}

	// Submit this stage's workflow
	HttpClient->SubmitWorkflowDirect(StageWorkflow);

	// Start progress polling if not already running
	if (!ProgressPollTimer.IsValid() && GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(
			ProgressPollTimer,
			FTimerDelegate::CreateLambda([this]()
			{
				if (HttpClient.IsValid())
				{
					HttpClient->PollProgress();
				}
			}),
			1.0f,
			true
		);
	}
}

void SViewGenPanel::ResolveUEImageMarker(TSharedPtr<FJsonObject> Workflow, const FString& Marker, const FString& ServerFilename)
{
	for (const auto& Pair : Workflow->Values)
	{
		TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
		if (!NodeObj.IsValid()) continue;

		TSharedPtr<FJsonObject> InputsObj = NodeObj->GetObjectField(TEXT("inputs"));
		if (!InputsObj.IsValid()) continue;

		if (InputsObj->HasField(TEXT("image")))
		{
			FString ImageVal = InputsObj->GetStringField(TEXT("image"));
			if (ImageVal == Marker)
			{
				InputsObj->SetStringField(TEXT("image"), ServerFilename);
			}
		}
	}
}

void SViewGenPanel::ResolveUECameraData(TSharedPtr<FJsonObject> Workflow, const FString& CameraDescription)
{
	// Delimiters used to identify previously injected camera data in prompts.
	// This prevents duplicate injection and allows updating stale camera info.
	static const FString CamTagOpen = TEXT("[UE_CAM]");
	static const FString CamTagClose = TEXT("[/UE_CAM]");
	const FString TaggedCamera = CamTagOpen + CameraDescription + CamTagClose;

	for (const auto& Pair : Workflow->Values)
	{
		TSharedPtr<FJsonObject> NodeObj = Pair.Value->AsObject();
		if (!NodeObj.IsValid()) continue;

		TSharedPtr<FJsonObject> Meta = NodeObj->GetObjectField(TEXT("_meta"));
		if (!Meta.IsValid() || !Meta->HasField(TEXT("ue_camera_target_field")))
		{
			continue;
		}

		FString TargetField = Meta->GetStringField(TEXT("ue_camera_target_field"));
		FString Position = Meta->GetStringField(TEXT("ue_camera_position"));

		TSharedPtr<FJsonObject> InputsObj = NodeObj->GetObjectField(TEXT("inputs"));
		if (!InputsObj.IsValid()) continue;

		FString CurrentText;
		if (InputsObj->HasField(TargetField))
		{
			CurrentText = InputsObj->GetStringField(TargetField);
		}

		// Check if camera data is already present
		int32 OpenIdx = CurrentText.Find(CamTagOpen);
		int32 CloseIdx = CurrentText.Find(CamTagClose);

		if (OpenIdx != INDEX_NONE && CloseIdx != INDEX_NONE && CloseIdx > OpenIdx)
		{
			// Extract the existing camera block (including tags)
			FString ExistingBlock = CurrentText.Mid(OpenIdx, CloseIdx - OpenIdx + CamTagClose.Len());
			FString ExistingCamera = CurrentText.Mid(OpenIdx + CamTagOpen.Len(),
				CloseIdx - OpenIdx - CamTagOpen.Len());

			if (ExistingCamera == CameraDescription)
			{
				// Identical camera data already present — leave the text as-is
				InputsObj->SetStringField(TargetField, CurrentText);
			}
			else
			{
				// Camera data changed (camera moved) — replace the old block
				FString NewText = CurrentText.Replace(*ExistingBlock, *TaggedCamera);
				InputsObj->SetStringField(TargetField, NewText);
			}
		}
		else
		{
			// No existing camera data — inject fresh
			FString NewText;
			if (Position == TEXT("prepend"))
			{
				NewText = TaggedCamera + TEXT(", ") + CurrentText;
			}
			else
			{
				// Default: append
				NewText = CurrentText.IsEmpty() ? TaggedCamera : CurrentText + TEXT(", ") + TaggedCamera;
			}
			InputsObj->SetStringField(TargetField, NewText);
		}

		// Strip camera tags from the final text — they're only used for deduplication
		FString FinalText = InputsObj->GetStringField(TargetField);
		FinalText.ReplaceInline(*CamTagOpen, TEXT(""));
		FinalText.ReplaceInline(*CamTagClose, TEXT(""));
		InputsObj->SetStringField(TargetField, FinalText);

		// Clean up UE-specific meta fields so they don't get sent to ComfyUI
		Meta->RemoveField(TEXT("ue_camera_target_field"));
		Meta->RemoveField(TEXT("ue_camera_format"));
		Meta->RemoveField(TEXT("ue_camera_position"));
	}
}

// ============================================================================
// Graph Save / Load
// ============================================================================

FString SViewGenPanel::GetWorkflowsDirectory() const
{
	// Try the project Plugins directory first
	FString WorkflowDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ViewGen"), TEXT("Workflows"));

	// If the plugin isn't in the project Plugins dir, try the engine Plugins dir
	if (!IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ViewGen"))))
	{
		// Fallback to Saved/ViewGen/Workflows in the project directory
		WorkflowDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ViewGen"), TEXT("Workflows"));
	}

	// Ensure the directory exists
	IFileManager::Get().MakeDirectory(*WorkflowDir, true);
	return WorkflowDir;
}

FReply SViewGenPanel::OnSaveGraphClicked()
{
	if (!GraphEditor.IsValid()) return FReply::Handled();

	// If we already have a file path, save directly
	if (!GraphEditor->GetCurrentFilePath().IsEmpty())
	{
		if (GraphEditor->SaveGraphToFile(GraphEditor->GetCurrentFilePath()))
		{
			FString Filename = FPaths::GetCleanFilename(GraphEditor->GetCurrentFilePath());
			UpdateStatusText(FString::Printf(TEXT("Graph saved: %s"), *Filename));
		}
		else
		{
			UpdateStatusText(TEXT("Failed to save graph"));
		}
		return FReply::Handled();
	}

	// No existing path — fall through to Save As
	return OnSaveGraphAsClicked();
}

FReply SViewGenPanel::OnSaveGraphAsClicked()
{
	if (!GraphEditor.IsValid()) return FReply::Handled();

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	FString DefaultDir = GetWorkflowsDirectory();
	TArray<FString> OutFiles;

	bool bSaved = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Save Workflow Graph"),
		DefaultDir,
		TEXT("workflow.json"),
		TEXT("Workflow Graph (*.json)|*.json"),
		0,
		OutFiles
	);

	if (bSaved && OutFiles.Num() > 0)
	{
		FString FilePath = OutFiles[0];

		// Ensure .json extension
		if (!FilePath.EndsWith(TEXT(".json")))
		{
			FilePath += TEXT(".json");
		}

		if (GraphEditor->SaveGraphToFile(FilePath))
		{
			FString Filename = FPaths::GetCleanFilename(FilePath);
			UpdateStatusText(FString::Printf(TEXT("Graph saved: %s"), *Filename));
		}
		else
		{
			UpdateStatusText(TEXT("Failed to save graph"));
		}
	}

	return FReply::Handled();
}

FReply SViewGenPanel::OnLoadGraphClicked()
{
	if (!GraphEditor.IsValid()) return FReply::Handled();

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	FString DefaultDir = GetWorkflowsDirectory();
	TArray<FString> OutFiles;

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Load Workflow Graph"),
		DefaultDir,
		TEXT(""),
		TEXT("Workflow Graph (*.json)|*.json|All Files (*.*)|*.*"),
		0,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *OutFiles[0]))
		{
			UpdateStatusText(TEXT("Failed to read file"));
		}
		else
		{
			TSharedPtr<FJsonObject> JsonRoot;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
			if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
			{
				UpdateStatusText(TEXT("Failed to load — file is not valid JSON"));
			}
			else
			{
				FString Filename = FPaths::GetCleanFilename(OutFiles[0]);

				// Detect format: ComfyUI Web UI export has "nodes" array with objects containing "type"
				// ComfyUI API format has top-level keys with "class_type" objects
				// ViewGen native format has "version" number and "nodes" array with "class_type" objects
				const TArray<TSharedPtr<FJsonValue>>* NodesArray;
				bool bIsComfyWebUI = false;
				bool bIsComfyAPI = false;

				if (JsonRoot->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray->Num() > 0)
				{
					// Check if the first node has "type" (Web UI) vs "class_type" (ViewGen native)
					const TSharedPtr<FJsonObject>* FirstNode;
					if ((*NodesArray)[0]->TryGetObject(FirstNode))
					{
						FString TypeField;
						if ((*FirstNode)->TryGetStringField(TEXT("type"), TypeField) &&
							!(*FirstNode)->HasField(TEXT("class_type")))
						{
							bIsComfyWebUI = true;
						}
					}
				}

				if (!bIsComfyWebUI)
				{
					// Check for API format: any top-level key with a "class_type" child
					for (const auto& Pair : JsonRoot->Values)
					{
						const TSharedPtr<FJsonObject>* NodeObj;
						if (Pair.Value->TryGetObject(NodeObj))
						{
							if ((*NodeObj)->HasField(TEXT("class_type")))
							{
								bIsComfyAPI = true;
								break;
							}
						}
					}
				}

				if (bIsComfyWebUI || bIsComfyAPI)
				{
					// ComfyUI format (Web UI or API) — use ImportWorkflowJSON which handles both
					GraphEditor->ImportWorkflowJSON(JsonRoot);
					RefreshLoadImageThumbnails();
					AutoSaveGraph();
					UpdateStatusText(FString::Printf(TEXT("Imported ComfyUI workflow: %s"), *Filename));
				}
				else if (GraphEditor->LoadGraphFromFile(OutFiles[0]))
				{
					// ViewGen native graph format
					RefreshLoadImageThumbnails();
					AutoSaveGraph();
					UpdateStatusText(FString::Printf(TEXT("Graph loaded: %s"), *Filename));
				}
				else
				{
					UpdateStatusText(TEXT("Failed to load — unrecognized file format"));
				}
			}
		}
	}

	return FReply::Handled();
}

// ============================================================================
// Graph Editor Fullscreen
// ============================================================================

FReply SViewGenPanel::OnGraphEditorFullscreen()
{
	if (!GraphEditor.IsValid()) return FReply::Handled();

	// If already fullscreen, bring it to front
	if (GraphEditorFullscreenWindow.IsValid())
	{
		GraphEditorFullscreenWindow->BringToFront();
		return FReply::Handled();
	}

	// Restore last-edited graph if empty, fall back to preset graph
	if (GraphEditor->GetNodes().Num() == 0)
	{
		if (!RestoreLastGraph())
		{
			GraphEditor->BuildPresetGraph();
		}
	}
	RefreshLoadImageThumbnails();

	// Remove the graph editor from the inline container before reparenting it
	if (GraphEditorInlineContainer.IsValid())
	{
		GraphEditorInlineContainer->SetContent(SNullWidget::NullWidget);
	}

	// Get primary monitor work area for sizing
	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetDisplayMetrics(DisplayMetrics);
	float ScreenWidth = static_cast<float>(DisplayMetrics.PrimaryDisplayWidth);
	float ScreenHeight = static_cast<float>(DisplayMetrics.PrimaryDisplayHeight);

	// Create a standalone window
	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("ViewGen - Graph Editor")))
		.ClientSize(FVector2D(ScreenWidth * 0.85f, ScreenHeight * 0.85f))
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		.SizingRule(ESizingRule::UserSized);

	GraphEditorFullscreenWindow = NewWindow;

	// Build the fullscreen content: graph editor + toolbar
	TSharedRef<SWidget> FullscreenContent = SNew(SVerticalBox)

		// SSplitter: graph canvas + details panel
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(0.75f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.06f))
				.Padding(0.0f)
				[
					GraphEditor.ToSharedRef()
				]
			]

			+ SSplitter::Slot()
			.Value(0.25f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.09f, 0.09f, 0.10f))
				.Padding(4.0f)
				[
					NodeDetailsPanel.IsValid() ? StaticCastSharedRef<SWidget>(NodeDetailsPanel.ToSharedRef()) : StaticCastSharedRef<SWidget>(SNew(SScrollBox))
				]
			]
		]

		// Bottom toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Generate from Graph")))
				.OnClicked(this, &SViewGenPanel::OnGenerateFromGraphClicked)
				.IsEnabled_Lambda([this]()
				{
					return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0
						&& HttpClient.IsValid() && !HttpClient->IsRequestInProgress();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Auto-Layout")))
				.OnClicked_Lambda([this]() -> FReply
				{
					if (GraphEditor.IsValid()) GraphEditor->AutoLayout();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Reset Graph")))
				.OnClicked_Lambda([this]() -> FReply
				{
					if (GraphEditor.IsValid())
					{
						GraphEditor->BuildPresetGraph();
						RefreshLoadImageThumbnails();
						AutoSaveGraph(); // Persist reset state
					}
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Clear")))
				.OnClicked_Lambda([this]() -> FReply
				{
					if (GraphEditor.IsValid()) GraphEditor->ClearGraph();
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Vertical)
				.SeparatorImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.ColorAndOpacity(FLinearColor(0.3f, 0.3f, 0.3f, 0.3f))
				.Thickness(1.0f)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text_Lambda([this]() -> FText
				{
					if (GraphEditor.IsValid() && GraphEditor->IsDirty())
					{
						return FText::FromString(TEXT("Save*"));
					}
					return FText::FromString(TEXT("Save"));
				})
				.OnClicked(this, &SViewGenPanel::OnSaveGraphClicked)
				.IsEnabled_Lambda([this]()
				{
					return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0;
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Save As")))
				.OnClicked(this, &SViewGenPanel::OnSaveGraphAsClicked)
				.IsEnabled_Lambda([this]()
				{
					return GraphEditor.IsValid() && GraphEditor->GetNodes().Num() > 0;
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Load")))
				.OnClicked(this, &SViewGenPanel::OnLoadGraphClicked)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Right-click: add nodes  |  Del: delete  |  Ctrl+L: auto-layout  |  Double-click wire: add reroute")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Close")))
				.OnClicked_Lambda([this]() -> FReply
				{
					CloseGraphEditorFullscreen();
					return FReply::Handled();
				})
			]
		];

	NewWindow->SetContent(FullscreenContent);

	// Handle window close (user clicks X) — reparent graph editor back to inline.
	// Capture a weak reference to the panel so the callback safely no-ops if
	// the panel has already been destroyed (e.g. during editor shutdown).
	TWeakPtr<SViewGenPanel> WeakPanel = SharedThis(this);
	NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda(
		[WeakPanel](const TSharedRef<SWindow>&)
		{
			TSharedPtr<SViewGenPanel> Panel = WeakPanel.Pin();
			if (!Panel.IsValid())
			{
				return; // Panel destroyed (editor shutdown) — nothing to reparent
			}
			if (Panel->GraphEditorInlineContainer.IsValid() && Panel->GraphEditor.IsValid())
			{
				Panel->GraphEditorInlineContainer->SetContent(Panel->GraphEditor.ToSharedRef());
			}
			Panel->GraphEditorFullscreenWindow.Reset();
		}
	));

	FSlateApplication::Get().AddWindow(NewWindow);

	return FReply::Handled();
}

void SViewGenPanel::CloseGraphEditorFullscreen()
{
	if (GraphEditorFullscreenWindow.IsValid())
	{
		// Return the graph editor to the inline container before destroying the window
		if (GraphEditorInlineContainer.IsValid() && GraphEditor.IsValid())
		{
			GraphEditorInlineContainer->SetContent(GraphEditor.ToSharedRef());
		}

		GraphEditorFullscreenWindow->RequestDestroyWindow();
		GraphEditorFullscreenWindow.Reset();
	}
}

// ============================================================================
// Video Generation Tab
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildVideoTab()
{
	return SNew(SSplitter)
		.Orientation(Orient_Vertical)

		// Top: Source frame preview
		+ SSplitter::Slot()
		.Value(0.45f)
		[
			SNew(SBox)
			.Padding(4.0f)
			[
				SNew(SHorizontalBox)

				// Source frame (left, larger)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(4.0f)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SScaleBox)
							.Stretch(EStretch::ScaleToFit)
							[
								SAssignNew(VideoSourceImage, SImage)
								.Image(VideoSourceBrush.Get())
							]
						]

						// Placeholder text
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NoVideoSource", "No source frame\nGenerate an image or click Load Image..."))
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.7f)))
							.Visibility_Lambda([this]()
							{
								return VideoSourceTextureRaw == nullptr
									? EVisibility::Visible : EVisibility::Collapsed;
							})
						]

						// "Source Frame" label overlay
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(6.0f, 4.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SourceFrameLabel", "Source Frame"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.7f, 0.3f)))
						]

						// "Load Image" button overlay (bottom-right)
						+ SOverlay::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						.Padding(6.0f, 4.0f)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
							.ContentPadding(FMargin(8.0f, 4.0f))
							.OnClicked(this, &SViewGenPanel::OnLoadSourceFrameClicked)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("LoadSourceFrame", "Load Image..."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
							]
						]
					]
				]
			]
		]

		// Bottom: Video controls (scrollable)
		+ SSplitter::Slot()
		.Value(0.55f)
		[
			SNew(SScrollBox)

			// Video mode + Generate button row
			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SAssignNew(VideoModeCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&VideoModeOptions)
					.InitiallySelectedItem(SelectedVideoMode)
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
					{
						SelectedVideoMode = NewValue;
						if (NewValue.IsValid())
						{
							UGenAISettings* S = UGenAISettings::Get();
							int32 Idx = VideoModeOptions.IndexOfByKey(NewValue);
							if (Idx != INDEX_NONE)
							{
								S->VideoMode = static_cast<EVideoMode>(Idx);
								S->SaveConfig();
							}
						}
					})
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
					{
						return SNew(STextBlock).Text(FText::FromString(*Item));
					})
					.Content()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return SelectedVideoMode.IsValid()
								? FText::FromString(*SelectedVideoMode) : FText::GetEmpty();
						})
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.ContentPadding(FMargin(12.0f, 4.0f))
					.IsEnabled_Lambda([this]()
					{
						return !bIsGeneratingVideo && VideoSourceTextureRaw != nullptr;
					})
					.OnClicked(this, &SViewGenPanel::OnAnimateClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GenerateVideo", "Generate Video"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					]
				]
			]

			+ SScrollBox::Slot()
			[
				SNew(SSeparator)
			]

			// Motion Adherence slider
			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MotionAdherence", "Motion Adherence"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.7f, 0.3f)))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.Delta(0.01f)
						.Value_Lambda([this]()
						{
							return UGenAISettings::Get()->VideoMotionAdherence;
						})
						.OnValueChanged_Lambda([this](float NewValue)
						{
							UGenAISettings::Get()->VideoMotionAdherence = NewValue;
							ApplyVideoAdherenceToSettings(NewValue);
						})
						.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
						{
							UGenAISettings::Get()->VideoMotionAdherence = NewValue;
							ApplyVideoAdherenceToSettings(NewValue);
							UGenAISettings::Get()->SaveConfig();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const float Val = UGenAISettings::Get()->VideoMotionAdherence;
							if (Val < 0.25f) return FText::FromString(TEXT("Subtle"));
							if (Val < 0.50f) return FText::FromString(TEXT("Gentle"));
							if (Val < 0.75f) return FText::FromString(TEXT("Dynamic"));
							return FText::FromString(TEXT("Dramatic"));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					]
				]
			]

			+ SScrollBox::Slot()
			[
				SNew(SSeparator)
			]

			// Video Prompt
			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("VideoPromptLabel", "Video Prompt:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(VideoPromptTextBox, SMultiLineEditableTextBox)
					.Text(FText::FromString(UGenAISettings::Get()->VideoPrompt))
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("VideoNegPromptLabel", "Negative:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(VideoNegativePromptTextBox, SMultiLineEditableTextBox)
					.Text(FText::FromString(UGenAISettings::Get()->VideoNegativePrompt))
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]

			+ SScrollBox::Slot()
			[
				SNew(SSeparator)
			]

			// Video settings (collapsible)
			+ SScrollBox::Slot()
			.Padding(4.0f)
			[
				BuildVideoSettingsPanel()
			]
		];
}

TSharedRef<SWidget> SViewGenPanel::BuildVideoSettingsPanel()
{
	TSharedRef<SWidget> SettingsContent = SNew(SVerticalBox)

		// Duration
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeSettingsRow(
				LOCTEXT("VideoDuration", "Duration (s)"),
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(10.0f)
				.Delta(0.5f)
				.Value_Lambda([]() { return UGenAISettings::Get()->VideoDuration; })
				.OnValueChanged_Lambda([](float V) { UGenAISettings::Get()->VideoDuration = V; })
				.OnValueCommitted_Lambda([](float V, ETextCommit::Type) { UGenAISettings::Get()->VideoDuration = V; UGenAISettings::Get()->SaveConfig(); })
			)
		]

		// FPS
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeSettingsRow(
				LOCTEXT("VideoFPS", "FPS"),
				SNew(SSpinBox<float>)
				.MinValue(6.0f)
				.MaxValue(30.0f)
				.Delta(1.0f)
				.Value_Lambda([]() { return static_cast<float>(UGenAISettings::Get()->VideoFPS); })
				.OnValueChanged_Lambda([](float V) { UGenAISettings::Get()->VideoFPS = FMath::RoundToInt(V); })
				.OnValueCommitted_Lambda([](float V, ETextCommit::Type) { UGenAISettings::Get()->VideoFPS = FMath::RoundToInt(V); UGenAISettings::Get()->SaveConfig(); })
			)
		]

		// CFG
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeSettingsRow(
				LOCTEXT("VideoCFGLabel", "CFG Scale"),
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(20.0f)
				.Delta(0.5f)
				.Value_Lambda([]() { return UGenAISettings::Get()->VideoCFG; })
				.OnValueChanged_Lambda([](float V) { UGenAISettings::Get()->VideoCFG = V; })
				.OnValueCommitted_Lambda([](float V, ETextCommit::Type) { UGenAISettings::Get()->VideoCFG = V; UGenAISettings::Get()->SaveConfig(); })
			)
		]

		// Steps
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeSettingsRow(
				LOCTEXT("VideoStepsLabel", "Steps"),
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(100.0f)
				.Delta(1.0f)
				.Value_Lambda([]() { return static_cast<float>(UGenAISettings::Get()->VideoSteps); })
				.OnValueChanged_Lambda([](float V) { UGenAISettings::Get()->VideoSteps = FMath::RoundToInt(V); })
				.OnValueCommitted_Lambda([](float V, ETextCommit::Type) { UGenAISettings::Get()->VideoSteps = FMath::RoundToInt(V); UGenAISettings::Get()->SaveConfig(); })
			)
		]

		// Seed
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			MakeSettingsRow(
				LOCTEXT("VideoSeedLabel", "Seed"),
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(999999999.0f)
				.Delta(1.0f)
				.Value_Lambda([]() { return static_cast<float>(UGenAISettings::Get()->VideoSeed); })
				.OnValueChanged_Lambda([](float V) { UGenAISettings::Get()->VideoSeed = static_cast<int64>(V); })
				.OnValueCommitted_Lambda([](float V, ETextCommit::Type) { UGenAISettings::Get()->VideoSeed = static_cast<int64>(V); UGenAISettings::Get()->SaveConfig(); })
			)
		]

		// Veo3 Generate Audio (visible when Veo3 mode)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return UGenAISettings::Get()->VideoMode == EVideoMode::Veo3
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MakeSettingsRow(
					LOCTEXT("Veo3Audio", "Generate Audio"),
					SNew(SCheckBox)
					.IsChecked_Lambda([]()
					{
						return UGenAISettings::Get()->bVeo3GenerateAudio
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([](ECheckBoxState State)
					{
						UGenAISettings::Get()->bVeo3GenerateAudio = (State == ECheckBoxState::Checked);
						UGenAISettings::Get()->SaveConfig();
					})
				)
			]
		]

		// Veo3 Aspect Ratio (visible when Veo3 mode)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return UGenAISettings::Get()->VideoMode == EVideoMode::Veo3
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MakeSettingsRow(
					LOCTEXT("Veo3Aspect", "Veo3 Aspect Ratio"),
					SNew(SEditableTextBox)
					.Text_Lambda([]() { return FText::FromString(UGenAISettings::Get()->Veo3AspectRatio); })
					.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type)
					{
						UGenAISettings::Get()->Veo3AspectRatio = Text.ToString();
						UGenAISettings::Get()->SaveConfig();
					})
				)
			]
		]

		// Veo3 Person Generation (visible when Veo3 mode)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([]()
			{
				return UGenAISettings::Get()->VideoMode == EVideoMode::Veo3
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				MakeSettingsRow(
					LOCTEXT("Veo3Person", "Person Generation"),
					SNew(SEditableTextBox)
					.Text_Lambda([]() { return FText::FromString(UGenAISettings::Get()->Veo3PersonGeneration); })
					.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type)
					{
						UGenAISettings::Get()->Veo3PersonGeneration = Text.ToString();
						UGenAISettings::Get()->SaveConfig();
					})
				)
			]
		];

	return BuildCollapsibleSection(
		LOCTEXT("VideoSettingsHeader", "Video Settings"),
		bVideoSettingsExpanded,
		SettingsContent);
}

void SViewGenPanel::ApplyVideoAdherenceToSettings(float Adherence)
{
	UGenAISettings* Settings = UGenAISettings::Get();

	// MODE-SPECIFIC adjustments based on actual node parameter ranges
	switch (Settings->VideoMode)
	{
	case EVideoMode::KlingVideo:
		// cfg_scale: 0.0–1.0 (higher = more adherence to prompt)
		Settings->VideoCFG = FMath::Lerp(0.3f, 1.0f, Adherence);
		// duration: "5" or "10" — use 10 at high adherence
		Settings->VideoDuration = Adherence > 0.5f ? 10.0f : 5.0f;
		// Higher adherence → pro quality
		Settings->KlingVideoQuality = Adherence > 0.6f ? TEXT("pro") : TEXT("std");
		break;

	case EVideoMode::WanVideo:
		// WanImageToVideoApi: duration 5/10/15 (step 5), no CFG control
		// Low adherence → 5s, mid → 10s, high → 15s
		if (Adherence < 0.33f) Settings->VideoDuration = 5.0f;
		else if (Adherence < 0.66f) Settings->VideoDuration = 10.0f;
		else Settings->VideoDuration = 15.0f;
		break;

	case EVideoMode::Veo3:
		// Veo3 duration is fixed at 8 seconds, no CFG control
		Settings->VideoDuration = 8.0f;
		// Enable audio at high adherence levels
		Settings->bVeo3GenerateAudio = Adherence > 0.7f;
		break;
	}

	Settings->SaveConfig();
}

void SViewGenPanel::SetVideoSourceFromPreview()
{
	UTexture2D* Tex = nullptr;

	// Prefer the most recent generation in history
	if (ImageHistory.Num() > 0)
	{
		Tex = ImageHistory.Last().Texture;
	}

	// Fall back to current preview — use stored history texture instead of GetResourceObject()
	// to avoid stale TObjectPtr crash
	if (!Tex && HistoryIndex >= 0 && HistoryIndex < ImageHistory.Num())
	{
		Tex = ImageHistory[HistoryIndex].Texture;
	}

	if (Tex && VideoSourceBrush.IsValid())
	{
		VideoSourceTextureRaw = Tex;
		VideoSourceBrush->SetResourceObject(Tex);
		VideoSourceBrush->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
		VideoSourceBrush->DrawAs = ESlateBrushDrawType::Image;

		if (VideoSourceImage.IsValid())
		{
			VideoSourceImage->SetImage(VideoSourceBrush.Get());
		}
	}
}

void SViewGenPanel::SetVideoSourceFromTexture(UTexture2D* Tex)
{
	if (!Tex || !VideoSourceBrush.IsValid()) return;

	VideoSourceTextureRaw = Tex;
	VideoSourceBrush->SetResourceObject(Tex);
	VideoSourceBrush->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
	VideoSourceBrush->DrawAs = ESlateBrushDrawType::Image;

	if (VideoSourceImage.IsValid())
	{
		VideoSourceImage->SetImage(VideoSourceBrush.Get());
	}
}

FReply SViewGenPanel::OnLoadSourceFrameClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	// Use the editor's last-used directory as the default path
	const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT);

	TArray<FString> OutFiles;
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Source Frame Image"),
		DefaultPath,
		TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.exr;*.tga)|*.png;*.jpg;*.jpeg;*.bmp;*.exr;*.tga|All Files (*.*)|*.*"),
		EFileDialogFlags::None,
		OutFiles
	);

	if (!bOpened || OutFiles.Num() == 0) return FReply::Handled();

	const FString& FilePath = OutFiles[0];

	// Remember directory for next time
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(FilePath));

	// Load the raw file bytes
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to load source frame from: %s"), *FilePath);
		UpdateStatusText(FString::Printf(TEXT("Failed to load image: %s"), *FPaths::GetCleanFilename(FilePath)));
		return FReply::Handled();
	}

	// Decompress to raw RGBA using the ImageWrapper module
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	// Detect format from extension
	EImageFormat Format = EImageFormat::PNG;
	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	if (Ext == TEXT("jpg") || Ext == TEXT("jpeg"))
	{
		Format = EImageFormat::JPEG;
	}
	else if (Ext == TEXT("bmp"))
	{
		Format = EImageFormat::BMP;
	}
	else if (Ext == TEXT("exr"))
	{
		Format = EImageFormat::EXR;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Image decode failed for: %s"), *FilePath);
		UpdateStatusText(FString::Printf(TEXT("Failed to decode image: %s"), *FPaths::GetCleanFilename(FilePath)));
		return FReply::Handled();
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to get raw pixel data from: %s"), *FilePath);
		UpdateStatusText(TEXT("Failed to decompress image pixels"));
		return FReply::Handled();
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	// Create a UTexture2D from the raw data
	UTexture2D* NewTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!NewTexture)
	{
		UpdateStatusText(TEXT("Failed to create texture from loaded image"));
		return FReply::Handled();
	}

	// Copy pixel data into mip 0
	void* MipData = NewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, RawData.GetData(), RawData.Num());
	NewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	NewTexture->UpdateResource();

	// Root the texture so GC doesn't collect it
	NewTexture->AddToRoot();

	// Unroot previous loaded-from-disk texture if any
	if (LoadedSourceFrameTexture && LoadedSourceFrameTexture->IsRooted())
	{
		LoadedSourceFrameTexture->RemoveFromRoot();
	}
	LoadedSourceFrameTexture = NewTexture;

	// Set it as the video source frame
	SetVideoSourceFromTexture(NewTexture);

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Loaded source frame from: %s (%dx%d)"), *FilePath, Width, Height);
	UpdateStatusText(FString::Printf(TEXT("Loaded source frame: %s (%dx%d)"), *FPaths::GetCleanFilename(FilePath), Width, Height));

	return FReply::Handled();
}

FString SViewGenPanel::TextureToBase64PNG(UTexture2D* Texture)
{
	if (!Texture) return FString();

	// Read back pixels from the texture
	const int32 Width = Texture->GetSizeX();
	const int32 Height = Texture->GetSizeY();

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);

	// Lock mip 0 and read pixels
	const void* MipData = Texture->GetPlatformData()->Mips[0].BulkData.LockReadOnly();
	if (!MipData)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: TextureToBase64PNG — could not lock texture data"));
		return FString();
	}
	FMemory::Memcpy(Pixels.GetData(), MipData, Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	// Compress to PNG (UE 5.7 uses TArray64<uint8>)
	TArray64<uint8> PNGData;
	FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PNGData);

	if (PNGData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: TextureToBase64PNG — PNG compression failed"));
		return FString();
	}

	// FBase64::Encode expects TArray<uint8>, copy from TArray64
	TArray<uint8> PNGDataCompat;
	PNGDataCompat.SetNumUninitialized(static_cast<int32>(PNGData.Num()));
	FMemory::Memcpy(PNGDataCompat.GetData(), PNGData.GetData(), PNGData.Num());
	return FBase64::Encode(PNGDataCompat);
}

FReply SViewGenPanel::OnAnimateClicked()
{
	if (bIsGeneratingVideo) return FReply::Handled();
	if (!VideoSourceBrush.IsValid() || VideoSourceTextureRaw == nullptr)
	{
		UpdateStatusText(TEXT("No source frame — generate an image first"));
		return FReply::Handled();
	}

	// Save video prompts to settings
	UGenAISettings* Settings = UGenAISettings::Get();
	if (VideoPromptTextBox.IsValid())
	{
		Settings->VideoPrompt = VideoPromptTextBox->GetText().ToString();
	}
	if (VideoNegativePromptTextBox.IsValid())
	{
		Settings->VideoNegativePrompt = VideoNegativePromptTextBox->GetText().ToString();
	}
	Settings->SaveConfig();

	// Encode the source frame to base64 PNG
	UTexture2D* SourceTex = VideoSourceTextureRaw;
	FString SourceBase64 = TextureToBase64PNG(SourceTex);
	if (SourceBase64.IsEmpty())
	{
		UpdateStatusText(TEXT("Failed to encode source frame"));
		return FReply::Handled();
	}

	bIsGeneratingVideo = true;
	bIsGenerating = true; // Re-use image generation flag so progress polling works
	CurrentProgress = 0.0f;

	FString Prompt = VideoPromptTextBox.IsValid() ? VideoPromptTextBox->GetText().ToString() : Settings->VideoPrompt;
	FString NegativePrompt = VideoNegativePromptTextBox.IsValid() ? VideoNegativePromptTextBox->GetText().ToString() : Settings->VideoNegativePrompt;

	// Show cost estimate overlay in graph editor upper-left corner
	FString VideoCostEstimate = EstimateVideoCost();
	if (GraphEditor.IsValid() && !VideoCostEstimate.IsEmpty())
	{
		GraphEditor->SetOverlayText(FString::Printf(TEXT("Est: %s"), *VideoCostEstimate));
	}

	switch (Settings->VideoMode)
	{
	case EVideoMode::KlingVideo:
		HttpClient->SendKlingVideoRequest(SourceBase64, Prompt, NegativePrompt);
		UpdateStatusText(TEXT("Generating video (Kling Video)..."));
		break;

	case EVideoMode::Veo3:
		HttpClient->SendVeo3Request(SourceBase64, Prompt);
		UpdateStatusText(TEXT("Generating video (Google Veo 3)..."));
		break;

	case EVideoMode::WanVideo:
		HttpClient->SendWanVideoRequest(SourceBase64, Prompt, NegativePrompt);
		UpdateStatusText(TEXT("Generating video (Wan I2V)..."));
		break;
	}

	// Start progress polling (same mechanism as image generation)
	if (GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(
			ProgressPollTimer,
			FTimerDelegate::CreateRaw(this, &SViewGenPanel::PollVideoProgress),
			UGenAISettings::Get()->ProgressPollInterval,
			true);
	}

	return FReply::Handled();
}

void SViewGenPanel::PollVideoProgress()
{
	if (HttpClient.IsValid())
	{
		HttpClient->PollProgress();
	}
}

// ============================================================================
// Image History Navigation
// ============================================================================

void SViewGenPanel::ShowPreviousResult()
{
	if (HistoryIndex > 0)
	{
		ShowHistoryEntry(HistoryIndex - 1);
	}
}

void SViewGenPanel::ShowNextResult()
{
	if (HistoryIndex < ImageHistory.Num() - 1)
	{
		ShowHistoryEntry(HistoryIndex + 1);
	}
}

void SViewGenPanel::ShowHistoryEntry(int32 Index)
{
	if (!ImageHistory.IsValidIndex(Index)) return;

	HistoryIndex = Index;
	const FHistoryEntry& Entry = ImageHistory[Index];

	if (Entry.Texture && PreviewBrush.IsValid())
	{
		PreviewBrush->SetResourceObject(Entry.Texture);
		PreviewBrush->ImageSize = FVector2D(Entry.Texture->GetSizeX(), Entry.Texture->GetSizeY());
		PreviewBrush->DrawAs = ESlateBrushDrawType::Image;

		if (PreviewImage.IsValid())
		{
			PreviewImage->SetImage(PreviewBrush.Get());
		}
	}
}

// ============================================================================
// Result Gallery (thumbnail strip)
// ============================================================================

TSharedRef<SWidget> SViewGenPanel::BuildResultGalleryPanel()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(2.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f, 2.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GalleryLabel", "Results"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.7f, 0.3f)))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ResultGalleryBox, SScrollBox)
				.Orientation(Orient_Vertical)
			]

			// ---- Segmentation Section (HIDDEN - pending RMBG integration) ----
			// TODO: Re-enable with ComfyUI-RMBG SAM2Segment node
#if 0
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 6.0f, 2.0f, 2.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f, 2.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SegmentLabel", "Segmentation"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.8f, 0.5f)))
			]

			// SAM3 text prompt input
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 0.0f, 2.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SAM3PromptInput, SEditableTextBox)
					.Text(FText::FromString(Settings->SAM3TextPrompt))
					.HintText(LOCTEXT("SAM3Hint", "objects (e.g. person . car . tree)"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SegmentBtn", "Segment"))
					.ToolTipText(LOCTEXT("SegmentTip", "Run SAM3 segmentation on the current preview image"))
					.IsEnabled_Lambda([this]()
					{
						return !bIsGenerating && ImageHistory.Num() > 0 && HistoryIndex >= 0
							&& !HttpClient->IsRequestInProgress();
					})
					.OnClicked(this, &SViewGenPanel::OnSegmentWithSAM3Clicked)
				]
			]

			// Segmentation Gallery (populated after SAM3 results arrive)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(300.0f)
			[
				SAssignNew(SegmentationGalleryBox, SScrollBox)
				.Orientation(Orient_Vertical)
			]

			// Convert All Selected button (visible when segments exist)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f, 2.0f, 4.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ConvertAllBtn", "Convert Selected to 3D"))
				.ToolTipText(LOCTEXT("ConvertAllTip", "Send all selected segments to Meshy for Image-to-3D conversion"))
				.HAlign(HAlign_Center)
				.Visibility_Lambda([this]()
				{
					return SAM3Segments.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.IsEnabled_Lambda([this]()
				{
					const UGenAISettings* S = UGenAISettings::Get();
					return !S->MeshyApiKey.IsEmpty();
				})
				.OnClicked(this, &SViewGenPanel::OnConvertAllSelectedClicked)
			]
#endif
		];
}

void SViewGenPanel::RebuildResultGallery()
{
	if (!ResultGalleryBox.IsValid()) return;

	ResultGalleryBox->ClearChildren();

	// Add thumbnails in reverse chronological order (newest at top)
	for (int32 i = ImageHistory.Num() - 1; i >= 0; --i)
	{
		const int32 EntryIndex = i;
		const FHistoryEntry& Entry = ImageHistory[i];
		if (!Entry.Brush.IsValid()) continue;

		ResultGalleryBox->AddSlot()
		.Padding(2.0f)
		[
			SNew(SBox)
			.HeightOverride(80.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.Cursor(EMouseCursor::Hand)
				.OnClicked_Lambda([this, EntryIndex]() -> FReply
				{
					ShowHistoryEntry(EntryIndex);
					// Also update the video source frame so switching to Video tab uses this image
					if (ImageHistory.IsValidIndex(EntryIndex))
					{
						SetVideoSourceFromTexture(ImageHistory[EntryIndex].Texture);
					}
					return FReply::Handled();
				})
				.ToolTipText(FText::FromString(FString::Printf(TEXT("Result %d"), EntryIndex + 1)))
				[
					SNew(SOverlay)

					// Thumbnail image
					+ SOverlay::Slot()
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage)
							.Image(Entry.Brush.Get())
						]
					]

					// Dim overlay for unselected thumbnails
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor_Lambda([this, EntryIndex]()
						{
							return HistoryIndex == EntryIndex
								? FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)   // Selected: fully transparent (no dim)
								: FLinearColor(0.0f, 0.0f, 0.0f, 0.45f); // Unselected: dark overlay
						})
						.Padding(0.0f)
						.Visibility(EVisibility::HitTestInvisible)
					]

					// Selection highlight border
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor_Lambda([this, EntryIndex]()
						{
							return HistoryIndex == EntryIndex
								? FLinearColor(0.9f, 0.7f, 0.1f, 0.8f)
								: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
						})
						.Padding(0.0f)
					]

					// Video badge (bottom-left, only for video results)
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Bottom)
					.Padding(4.0f, 0.0f, 0.0f, 4.0f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))
						.Padding(FMargin(4.0f, 1.0f))
						.Visibility(Entry.VideoPath.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("\u25B6")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]
			]
		];
	}
}

void SViewGenPanel::PopulateSegmentationGallery()
{
	if (!SegmentationCapture.IsValid() || SegmentationCapture->GetSegmentedActors().Num() == 0)
	{
		return;
	}

	const TArray<FSegmentedActor>& Segments = SegmentationCapture->GetSegmentedActors();

	// Also add the colour-coded mask as a gallery entry so the user can inspect it
	UTexture2D* MaskTexture = SegmentationCapture->GetSegmentationTexture();
	if (MaskTexture)
	{
		if (ImageHistory.Num() >= MaxHistoryEntries)
		{
			FHistoryEntry& Old = ImageHistory[0];
			if (Old.Texture && Old.Texture->IsRooted())
			{
				Old.Texture->RemoveFromRoot();
			}
			ImageHistory.RemoveAt(0);
			if (HistoryIndex > 0) HistoryIndex--;
		}

		FHistoryEntry MaskEntry;
		MaskEntry.Texture = MaskTexture;
		TSharedPtr<FSlateBrush> MaskBrush = MakeShareable(new FSlateBrush());
		MaskBrush->SetResourceObject(MaskTexture);
		MaskBrush->ImageSize = FVector2D(MaskTexture->GetSizeX(), MaskTexture->GetSizeY());
		MaskBrush->DrawAs = ESlateBrushDrawType::Image;
		MaskEntry.Brush = MaskBrush;
		ImageHistory.Add(MoveTemp(MaskEntry));
	}

	// Push each per-actor isolation image into the history gallery
	for (const FSegmentedActor& Seg : Segments)
	{
		if (!Seg.IsolationTexture) continue;

		// Trim history if at capacity
		if (ImageHistory.Num() >= MaxHistoryEntries)
		{
			FHistoryEntry& Old = ImageHistory[0];
			if (Old.Texture && Old.Texture->IsRooted())
			{
				Old.Texture->RemoveFromRoot();
			}
			ImageHistory.RemoveAt(0);
			if (HistoryIndex > 0) HistoryIndex--;
		}

		FHistoryEntry Entry;
		Entry.Texture = Seg.IsolationTexture;

		TSharedPtr<FSlateBrush> NewBrush = MakeShareable(new FSlateBrush());
		NewBrush->SetResourceObject(Seg.IsolationTexture);
		NewBrush->ImageSize = FVector2D(
			Seg.IsolationTexture->GetSizeX(),
			Seg.IsolationTexture->GetSizeY());
		NewBrush->DrawAs = ESlateBrushDrawType::Image;
		Entry.Brush = NewBrush;

		ImageHistory.Add(MoveTemp(Entry));
	}

	// Keep the generated image in the main preview (it was already set by OnGenerationComplete).
	// The user can click any segmented actor thumbnail in the gallery to view it.
	// Set the history index to the generated image (the one before the mask).
	int32 GeneratedIdx = ImageHistory.Num() - Segments.Num() - (MaskTexture ? 1 : 0) - 1;
	if (GeneratedIdx >= 0)
	{
		HistoryIndex = GeneratedIdx;
	}

	RebuildResultGallery();

	UpdateStatusText(FString::Printf(TEXT("Generation complete — %d actors segmented"),
		Segments.Num()));
}

// ============================================================================
// SAM3 Segmentation
// ============================================================================

FReply SViewGenPanel::OnSegmentWithSAM3Clicked()
{
	if (!ImageHistory.IsValidIndex(HistoryIndex))
	{
		UpdateStatusText(TEXT("No image to segment — generate an image first"));
		return FReply::Handled();
	}

	UTexture2D* SourceTexture = ImageHistory[HistoryIndex].Texture;
	if (!SourceTexture)
	{
		UpdateStatusText(TEXT("Selected image has no texture data"));
		return FReply::Handled();
	}

	// Get the text prompt from the input box
	FString TextPrompt = TEXT("objects");
	if (SAM3PromptInput.IsValid())
	{
		TextPrompt = SAM3PromptInput->GetText().ToString();
		if (TextPrompt.IsEmpty())
		{
			TextPrompt = TEXT("objects");
		}
		// Save back to settings
		UGenAISettings* Settings = UGenAISettings::Get();
		Settings->SAM3TextPrompt = TextPrompt;
		Settings->SaveConfig();
	}

	UpdateStatusText(FString::Printf(TEXT("Segmenting with SAM3: \"%s\"..."), *TextPrompt));

	// Root the source texture so it survives until results arrive
	if (!SourceTexture->IsRooted())
	{
		SourceTexture->AddToRoot();
	}

	HttpClient->SendSAM3SegmentationRequest(SourceTexture, TextPrompt);

	// Start the progress poll timer so PollHistory() discovers the SAM3 result
	const UGenAISettings* PollSettings = UGenAISettings::Get();
	if (GEditor && !ProgressPollTimer.IsValid())
	{
		GEditor->GetTimerManager()->SetTimer(ProgressPollTimer, FTimerDelegate::CreateLambda([this]()
		{
			if (HttpClient.IsValid())
			{
				HttpClient->PollProgress();
			}
		}), PollSettings->ProgressPollInterval, true);
	}

	return FReply::Handled();
}

void SViewGenPanel::OnSAM3SegmentationComplete(
	UTexture2D* VisualizationTexture,
	const TArray<UTexture2D*>& MaskTextures,
	UTexture2D* OriginalTexture)
{
	// Stop the progress poll timer — segmentation is done
	if (GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(ProgressPollTimer);
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen: SAM3 returned %d masks + visualization"), MaskTextures.Num());

	// Store visualization
	SAM3VisualizationTexture = VisualizationTexture;

	// Add visualization to the result history
	if (VisualizationTexture)
	{
		if (ImageHistory.Num() >= MaxHistoryEntries)
		{
			FHistoryEntry& Old = ImageHistory[0];
			if (Old.Texture && Old.Texture->IsRooted()) Old.Texture->RemoveFromRoot();
			ImageHistory.RemoveAt(0);
			if (HistoryIndex > 0) HistoryIndex--;
		}

		FHistoryEntry VisEntry;
		VisEntry.Texture = VisualizationTexture;
		VisEntry.Brush = MakeShareable(new FSlateBrush());
		VisEntry.Brush->SetResourceObject(VisualizationTexture);
		VisEntry.Brush->ImageSize = FVector2D(VisualizationTexture->GetSizeX(), VisualizationTexture->GetSizeY());
		VisEntry.Brush->DrawAs = ESlateBrushDrawType::Image;
		ImageHistory.Add(MoveTemp(VisEntry));

		// Show the visualization in the main preview
		HistoryIndex = ImageHistory.Num() - 1;
		ShowHistoryEntry(HistoryIndex);
	}

	RebuildResultGallery();

	// Apply masks to the original image to produce isolated segments
	if (OriginalTexture && MaskTextures.Num() > 0)
	{
		ApplyMasksToOriginal(OriginalTexture, MaskTextures);
	}
	else
	{
		UpdateStatusText(TEXT("SAM3 segmentation complete but no masks returned"));
	}
}

void SViewGenPanel::ApplyMasksToOriginal(UTexture2D* OriginalTexture, const TArray<UTexture2D*>& MaskTextures)
{
	ClearSAM3Segments();

	// Read original image pixels
	int32 OrigW = OriginalTexture->GetSizeX();
	int32 OrigH = OriginalTexture->GetSizeY();
	FTexturePlatformData* OrigPD = OriginalTexture->GetPlatformData();
	if (!OrigPD || OrigPD->Mips.Num() == 0)
	{
		UpdateStatusText(TEXT("Failed to read original image pixels"));
		return;
	}

	TArray<FColor> OrigPixels;
	OrigPixels.SetNum(OrigW * OrigH);
	{
		const void* Data = OrigPD->Mips[0].BulkData.LockReadOnly();
		FMemory::Memcpy(OrigPixels.GetData(), Data, OrigW * OrigH * sizeof(FColor));
		OrigPD->Mips[0].BulkData.Unlock();
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	for (int32 MaskIdx = 0; MaskIdx < MaskTextures.Num(); ++MaskIdx)
	{
		UTexture2D* MaskTex = MaskTextures[MaskIdx];
		if (!MaskTex) continue;

		int32 MaskW = MaskTex->GetSizeX();
		int32 MaskH = MaskTex->GetSizeY();
		FTexturePlatformData* MaskPD = MaskTex->GetPlatformData();
		if (!MaskPD || MaskPD->Mips.Num() == 0) continue;

		TArray<FColor> MaskPixels;
		MaskPixels.SetNum(MaskW * MaskH);
		{
			const void* Data = MaskPD->Mips[0].BulkData.LockReadOnly();
			FMemory::Memcpy(MaskPixels.GetData(), Data, MaskW * MaskH * sizeof(FColor));
			MaskPD->Mips[0].BulkData.Unlock();
		}

		// Build isolation image: original pixels where mask is white, white background elsewhere
		TArray<FColor> IsolationPixels;
		IsolationPixels.SetNum(OrigW * OrigH);

		bool bHadPixels = false;
		for (int32 y = 0; y < OrigH; ++y)
		{
			for (int32 x = 0; x < OrigW; ++x)
			{
				// Scale mask coordinates if sizes differ
				int32 MaskX = (MaskW == OrigW) ? x : FMath::Clamp(x * MaskW / OrigW, 0, MaskW - 1);
				int32 MaskY = (MaskH == OrigH) ? y : FMath::Clamp(y * MaskH / OrigH, 0, MaskH - 1);

				const FColor& MaskPx = MaskPixels[MaskY * MaskW + MaskX];
				// SAM3 masks are grayscale — white = foreground
				bool bForeground = (MaskPx.R > 127 || MaskPx.G > 127 || MaskPx.B > 127);

				int32 OrigIdx = y * OrigW + x;
				if (bForeground)
				{
					IsolationPixels[OrigIdx] = OrigPixels[OrigIdx];
					bHadPixels = true;
				}
				else
				{
					IsolationPixels[OrigIdx] = FColor(255, 255, 255, 255);
				}
			}
		}

		if (!bHadPixels) continue;

		// Create texture
		UTexture2D* IsoTex = UTexture2D::CreateTransient(OrigW, OrigH, PF_B8G8R8A8);
		if (!IsoTex) continue;

		IsoTex->MipGenSettings = TMGS_NoMipmaps;
		IsoTex->SRGB = true;
		IsoTex->NeverStream = true;
		void* TexData = IsoTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(TexData, IsolationPixels.GetData(), IsolationPixels.Num() * sizeof(FColor));
		IsoTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		IsoTex->UpdateResource();
		IsoTex->AddToRoot();

		// Encode to PNG + base64 for Meshy upload
		TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		PngWrapper->SetRaw(IsolationPixels.GetData(), IsolationPixels.Num() * sizeof(FColor),
			OrigW, OrigH, ERGBFormat::BGRA, 8);
		const TArray64<uint8>& PngData = PngWrapper->GetCompressed();
		FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());

		FSAM3Segment Seg;
		Seg.IsolationTexture = IsoTex;
		Seg.Brush = MakeShareable(new FSlateBrush());
		Seg.Brush->SetResourceObject(IsoTex);
		Seg.Brush->ImageSize = FVector2D(OrigW, OrigH);
		Seg.Brush->DrawAs = ESlateBrushDrawType::Image;
		Seg.Base64PNG = MoveTemp(Base64);
		Seg.bSelected = true;

		SAM3Segments.Add(MoveTemp(Seg));
	}

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Created %d isolated segment images"), SAM3Segments.Num());

	RebuildSegmentationGallery();
	UpdateStatusText(FString::Printf(TEXT("SAM3 segmentation complete — %d segments found"), SAM3Segments.Num()));
}

void SViewGenPanel::RebuildSegmentationGallery()
{
	if (!SegmentationGalleryBox.IsValid()) return;

	SegmentationGalleryBox->ClearChildren();

	for (int32 i = 0; i < SAM3Segments.Num(); ++i)
	{
		const int32 SegIdx = i;
		FSAM3Segment& Seg = SAM3Segments[i];
		if (!Seg.Brush.IsValid()) continue;

		SegmentationGalleryBox->AddSlot()
		.Padding(2.0f, 1.0f)
		[
			SNew(SHorizontalBox)

			// Checkbox
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, SegIdx]()
				{
					return SAM3Segments.IsValidIndex(SegIdx) && SAM3Segments[SegIdx].bSelected
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, SegIdx](ECheckBoxState NewState)
				{
					if (SAM3Segments.IsValidIndex(SegIdx))
					{
						SAM3Segments[SegIdx].bSelected = (NewState == ECheckBoxState::Checked);
					}
				})
			]

			// Thumbnail
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(64.0f)
				.HeightOverride(64.0f)
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage)
						.Image(Seg.Brush.Get())
					]
				]
			]

			// Label + status
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("Segment %d"), i + 1)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([this, SegIdx]() -> FText
					{
						if (!SAM3Segments.IsValidIndex(SegIdx)) return FText();
						const FSAM3Segment& S = SAM3Segments[SegIdx];
						if (S.MeshyStatus == TEXT("SUCCEEDED"))
							return FText::FromString(TEXT("3D model ready"));
						if (S.MeshyStatus == TEXT("IN_PROGRESS") || S.MeshyStatus == TEXT("PENDING"))
							return FText::FromString(FString::Printf(TEXT("Converting... %d%%"), S.MeshyProgress));
						if (S.MeshyStatus == TEXT("FAILED"))
							return FText::FromString(TEXT("Conversion failed"));
						return FText();
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]

			// Per-segment "To 3D" button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("To3DBtn", "To 3D"))
				.ToolTipText(LOCTEXT("To3DTip", "Convert this segment to a 3D model via Meshy"))
				.IsEnabled_Lambda([this, SegIdx]()
				{
					if (!SAM3Segments.IsValidIndex(SegIdx)) return false;
					const FSAM3Segment& S = SAM3Segments[SegIdx];
					const UGenAISettings* Cfg = UGenAISettings::Get();
					return !Cfg->MeshyApiKey.IsEmpty() && S.MeshyTaskId.IsEmpty();
				})
				.OnClicked_Lambda([this, SegIdx]() -> FReply
				{
					ConvertSegmentToMesh(SegIdx);
					return FReply::Handled();
				})
			]
		];
	}
}

void SViewGenPanel::ClearSAM3Segments()
{
	for (FSAM3Segment& Seg : SAM3Segments)
	{
		if (Seg.IsolationTexture && ::IsValid(Seg.IsolationTexture) && Seg.IsolationTexture->IsRooted())
		{
			Seg.IsolationTexture->RemoveFromRoot();
		}
		Seg.IsolationTexture = nullptr;
	}
	SAM3Segments.Empty();

	if (SAM3VisualizationTexture && ::IsValid(SAM3VisualizationTexture) && SAM3VisualizationTexture->IsRooted())
	{
		SAM3VisualizationTexture->RemoveFromRoot();
	}
	SAM3VisualizationTexture = nullptr;
}

// ============================================================================
// Meshy 3D Conversion
// ============================================================================

void SViewGenPanel::ConvertSegmentToMesh(int32 SegmentIndex)
{
	if (!SAM3Segments.IsValidIndex(SegmentIndex))
	{
		return;
	}

	FSAM3Segment& Seg = SAM3Segments[SegmentIndex];
	if (Seg.Base64PNG.IsEmpty())
	{
		UpdateStatusText(TEXT("Segment has no image data for conversion"));
		return;
	}

	const UGenAISettings* Settings = UGenAISettings::Get();

	// Bind callbacks with segment index context
	MeshyClient->OnTaskCreated.BindLambda([this, SegmentIndex](bool bSuccess, const FString& TaskIdOrError)
	{
		OnMeshyTaskCreated(bSuccess, TaskIdOrError, SegmentIndex);
	});

	MeshyClient->CreateImageTo3DTask(Seg.Base64PNG, Settings->bMeshyEnablePBR, Settings->bMeshyRemesh);

	Seg.MeshyStatus = TEXT("PENDING");
	UpdateStatusText(FString::Printf(TEXT("Sending segment %d to Meshy..."), SegmentIndex + 1));
	RebuildSegmentationGallery();
}

FReply SViewGenPanel::OnConvertAllSelectedClicked()
{
	int32 ConvertCount = 0;
	for (int32 i = 0; i < SAM3Segments.Num(); ++i)
	{
		if (SAM3Segments[i].bSelected && SAM3Segments[i].MeshyTaskId.IsEmpty())
		{
			ConvertSegmentToMesh(i);
			ConvertCount++;
			// Note: Meshy rate limits apply — for multiple segments we'd ideally
			// queue them, but for now we submit them in sequence
			break; // Submit one at a time to respect rate limits
		}
	}

	if (ConvertCount == 0)
	{
		UpdateStatusText(TEXT("No segments selected for conversion, or all already submitted"));
	}

	return FReply::Handled();
}

void SViewGenPanel::OnMeshyTaskCreated(bool bSuccess, const FString& TaskIdOrError, int32 SegmentIndex)
{
	if (!bSuccess)
	{
		UpdateStatusText(FString::Printf(TEXT("Meshy error: %s"), *TaskIdOrError));
		if (SAM3Segments.IsValidIndex(SegmentIndex))
		{
			SAM3Segments[SegmentIndex].MeshyStatus = TEXT("FAILED");
			SAM3Segments[SegmentIndex].MeshyTaskId.Empty();
		}
		RebuildSegmentationGallery();
		return;
	}

	FString TaskId = TaskIdOrError;
	UE_LOG(LogTemp, Log, TEXT("ViewGen Meshy: Task created for segment %d: %s"), SegmentIndex, *TaskId);

	if (SAM3Segments.IsValidIndex(SegmentIndex))
	{
		SAM3Segments[SegmentIndex].MeshyTaskId = TaskId;
		SAM3Segments[SegmentIndex].MeshyStatus = TEXT("PENDING");
	}

	UpdateStatusText(FString::Printf(TEXT("Meshy task created: %s"), *TaskId));
	RebuildSegmentationGallery();

	// Start polling timer if not already running
	if (!MeshyPollTimer.IsValid() && GEditor)
	{
		GEditor->GetTimerManager()->SetTimer(MeshyPollTimer, FTimerDelegate::CreateRaw(this, &SViewGenPanel::PollMeshyTasks),
			5.0f, true); // Poll every 5 seconds
	}

	// Check if there are more selected segments to submit
	for (int32 i = SegmentIndex + 1; i < SAM3Segments.Num(); ++i)
	{
		if (SAM3Segments[i].bSelected && SAM3Segments[i].MeshyTaskId.IsEmpty())
		{
			// Queue the next one after a short delay
			if (GEditor)
			{
				FTimerHandle TempHandle;
				int32 NextIdx = i;
				GEditor->GetTimerManager()->SetTimer(TempHandle,
					FTimerDelegate::CreateLambda([this, NextIdx]()
					{
						ConvertSegmentToMesh(NextIdx);
					}),
					2.0f, false);
			}
			break;
		}
	}
}

void SViewGenPanel::PollMeshyTasks()
{
	bool bAnyActive = false;

	for (FSAM3Segment& Seg : SAM3Segments)
	{
		if (Seg.MeshyTaskId.IsEmpty()) continue;
		if (Seg.MeshyStatus == TEXT("SUCCEEDED") || Seg.MeshyStatus == TEXT("FAILED") || Seg.MeshyStatus == TEXT("CANCELED"))
		{
			continue;
		}

		bAnyActive = true;

		// Poll this task
		MeshyClient->OnTaskProgress.BindLambda([this](const FMeshyTaskResult& Result)
		{
			OnMeshyTaskProgress(Result);
		});
		MeshyClient->OnTaskComplete.BindLambda([this](const FMeshyTaskResult& Result)
		{
			OnMeshyTaskComplete(Result);
		});

		MeshyClient->PollTask(Seg.MeshyTaskId);
		break; // Poll one at a time
	}

	if (!bAnyActive && GEditor)
	{
		GEditor->GetTimerManager()->ClearTimer(MeshyPollTimer);
	}
}

void SViewGenPanel::OnMeshyTaskProgress(const FMeshyTaskResult& Result)
{
	for (FSAM3Segment& Seg : SAM3Segments)
	{
		if (Seg.MeshyTaskId == Result.TaskId)
		{
			Seg.MeshyStatus = Result.Status;
			Seg.MeshyProgress = Result.Progress;
			break;
		}
	}

	UpdateStatusText(FString::Printf(TEXT("Meshy: %s %d%%"), *Result.Status, Result.Progress));
	RebuildSegmentationGallery();
}

void SViewGenPanel::OnMeshyTaskComplete(const FMeshyTaskResult& Result)
{
	for (FSAM3Segment& Seg : SAM3Segments)
	{
		if (Seg.MeshyTaskId == Result.TaskId)
		{
			Seg.MeshyStatus = Result.Status;
			Seg.MeshyProgress = Result.Progress;
			break;
		}
	}

	if (Result.Status == TEXT("SUCCEEDED") && !Result.GLBUrl.IsEmpty())
	{
		UpdateStatusText(FString::Printf(TEXT("Meshy: 3D model ready! Downloading GLB...")));

		// Download the GLB to the project's Saved directory
		FString SaveDir = FPaths::ProjectSavedDir() / TEXT("MeshyModels");
		IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*SaveDir);
		FString SavePath = SaveDir / FString::Printf(TEXT("%s.glb"), *Result.TaskId);

		MeshyClient->OnModelDownloaded.BindRaw(this, &SViewGenPanel::OnMeshyModelDownloaded);
		MeshyClient->DownloadModel(Result.GLBUrl, SavePath);
	}
	else if (Result.Status == TEXT("FAILED"))
	{
		UpdateStatusText(TEXT("Meshy: 3D conversion failed"));
	}

	RebuildSegmentationGallery();
}

void SViewGenPanel::OnMeshyModelDownloaded(const FString& LocalFilePath)
{
	if (LocalFilePath.IsEmpty())
	{
		UpdateStatusText(TEXT("Meshy: Failed to download 3D model"));
		return;
	}

	UpdateStatusText(FString::Printf(TEXT("Meshy: Model saved to %s"), *LocalFilePath));
	UE_LOG(LogTemp, Log, TEXT("ViewGen Meshy: GLB downloaded to %s"), *LocalFilePath);

	// Check if there are MeshyImport graph nodes that want to auto-import this model
	ExecuteMeshyImportNodes();
}

// ============================================================================
// Meshy Import-to-Level (Graph Node Execution)
// ============================================================================

void SViewGenPanel::ExecuteSave3DModelNodes()
{
	if (!GraphEditor.IsValid() || !HttpClient.IsValid()) return;

	// Check if the HttpClient detected a 3D model in the last workflow output
	if (!HttpClient->Has3DModelResult())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Save3DModel: No 3D model detected in workflow output"));
		UpdateStatusText(TEXT("No 3D model found in workflow output"));
		return;
	}

	// Collect settings from the first __UE_Save3DModel__ node in the graph
	FString AssetPath = TEXT("/Game/ViewGen/Models");
	FString AssetName;
	bool bPlaceInLevel = true;

	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	for (const FGraphNode& Node : GraphNodes)
	{
		if (Node.ClassType != UESave3DModelClassType) continue;

		const FString* AssetPathVal = Node.WidgetValues.Find(TEXT("asset_path"));
		const FString* AssetNameVal = Node.WidgetValues.Find(TEXT("asset_name"));
		const FString* PlaceVal = Node.WidgetValues.Find(TEXT("place_in_level"));

		if (AssetPathVal && !AssetPathVal->IsEmpty()) AssetPath = *AssetPathVal;
		if (AssetNameVal) AssetName = *AssetNameVal;
		if (PlaceVal) bPlaceInLevel = (*PlaceVal == TEXT("true"));
		break; // Use first node's settings
	}

	// Get the detected model file info
	FString ModelFilename = HttpClient->GetLast3DModelFilename();
	FString ModelSubfolder = HttpClient->GetLast3DModelSubfolder();
	FString ModelFolderType = HttpClient->GetLast3DModelFolderType();

	// Determine file extension and asset name
	FString FileExtension = FPaths::GetExtension(ModelFilename, true); // includes dot
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(ModelFilename);
		// Clean up the name for UE asset compatibility
		AssetName = AssetName.Replace(TEXT(" "), TEXT("_"));
		AssetName = AssetName.Replace(TEXT("-"), TEXT("_"));
	}

	// Download to a temp location
	FString SaveDir = FPaths::ProjectSavedDir() / TEXT("ComfyUI3DModels");
	FString LocalFilePath = SaveDir / (AssetName + FileExtension);

	UpdateStatusText(FString::Printf(TEXT("Downloading 3D model: %s"), *ModelFilename));
	UE_LOG(LogTemp, Log, TEXT("ViewGen Save3DModel: Downloading '%s' from ComfyUI (subfolder=%s, type=%s)"),
		*ModelFilename, *ModelSubfolder, *ModelFolderType);

	// Capture variables for the async callback
	FString CapturedAssetPath = AssetPath;
	FString CapturedAssetName = AssetName;
	bool bCapturedPlace = bPlaceInLevel;

	// Try downloading with the detected folder type first; if it fails, retry with alternates.
	// Many ComfyUI 3D nodes store files in 'temp' rather than 'output'.
	FString CapturedModelFilename = ModelFilename;
	FString CapturedModelSubfolder = ModelSubfolder;
	FString CapturedLocalFilePath = LocalFilePath;

	HttpClient->DownloadComfyUIFile(ModelFilename, ModelSubfolder, ModelFolderType, LocalFilePath,
		[this, CapturedAssetPath, CapturedAssetName, bCapturedPlace,
		 CapturedModelFilename, CapturedModelSubfolder, CapturedLocalFilePath, ModelFolderType](const FString& DownloadedPath)
		{
			// If initial download failed, retry with alternate folder types
			if (DownloadedPath.IsEmpty())
			{
				TArray<FString> AlternateTypes;
				if (ModelFolderType != TEXT("temp")) AlternateTypes.Add(TEXT("temp"));
				if (ModelFolderType != TEXT("output")) AlternateTypes.Add(TEXT("output"));
				if (ModelFolderType != TEXT("input")) AlternateTypes.Add(TEXT("input"));

				if (AlternateTypes.Num() > 0 && HttpClient.IsValid())
				{
					FString RetryType = AlternateTypes[0];
					UE_LOG(LogTemp, Log, TEXT("ViewGen Save3DModel: Retrying download with type='%s'"), *RetryType);
					UpdateStatusText(FString::Printf(TEXT("Retrying download with type=%s..."), *RetryType));

					HttpClient->DownloadComfyUIFile(CapturedModelFilename, CapturedModelSubfolder, RetryType, CapturedLocalFilePath,
						[this, CapturedAssetPath, CapturedAssetName, bCapturedPlace,
						 CapturedModelFilename, CapturedModelSubfolder, CapturedLocalFilePath, AlternateTypes](const FString& RetryPath)
						{
							if (RetryPath.IsEmpty() && AlternateTypes.Num() > 1 && HttpClient.IsValid())
							{
								// Try second alternate
								FString RetryType2 = AlternateTypes[1];
								UE_LOG(LogTemp, Log, TEXT("ViewGen Save3DModel: Retrying download with type='%s'"), *RetryType2);

								HttpClient->DownloadComfyUIFile(CapturedModelFilename, CapturedModelSubfolder, RetryType2, CapturedLocalFilePath,
									[this, CapturedAssetPath, CapturedAssetName, bCapturedPlace](const FString& FinalPath)
									{
										if (FinalPath.IsEmpty())
										{
											UpdateStatusText(TEXT("Save 3D Model: Download failed after all retries"));
											return;
										}
										Handle3DModelImport(FinalPath, CapturedAssetPath, CapturedAssetName, bCapturedPlace);
									});
								return;
							}
							if (RetryPath.IsEmpty())
							{
								UpdateStatusText(TEXT("Save 3D Model: Download failed after retry"));
								return;
							}
							Handle3DModelImport(RetryPath, CapturedAssetPath, CapturedAssetName, bCapturedPlace);
						});
					return;
				}

				UpdateStatusText(TEXT("Save 3D Model: Download failed"));
				return;
			}

			Handle3DModelImport(DownloadedPath, CapturedAssetPath, CapturedAssetName, bCapturedPlace);
		});
}

void SViewGenPanel::Handle3DModelImport(const FString& DownloadedPath, const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel)
{
	UE_LOG(LogTemp, Log, TEXT("ViewGen Save3DModel: Model downloaded to %s"), *DownloadedPath);

	// Defer to the next main-loop tick via FTSTicker (game thread, outside task graph).
	// Interchange GLB import pumps the task graph internally — running inside
	// AsyncTask(GameThread) causes a RecursionGuard assertion crash.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, DownloadedPath, AssetPath, AssetName, bPlaceInLevel](float) -> bool
	{
		// Import into the content browser (reusing existing Meshy import infrastructure)
		FString ImportedAssetPath = ImportModelFileToContentBrowser(DownloadedPath, AssetPath, AssetName);
		if (ImportedAssetPath.IsEmpty())
		{
			UpdateStatusText(TEXT("Save 3D Model: Failed to import into content browser"));
			return false;
		}

		UpdateStatusText(FString::Printf(TEXT("3D Model imported: %s"), *ImportedAssetPath));

		if (bPlaceInLevel)
		{
			AActor* SpawnedActor = PlaceAssetInLevel(ImportedAssetPath);
			if (SpawnedActor)
			{
				UpdateStatusText(FString::Printf(TEXT("3D Model '%s' placed in level"), *AssetName));
			}
			else
			{
				UpdateStatusText(FString::Printf(TEXT("Imported '%s' but failed to place in level"), *AssetName));
			}
		}

		// Set the mesh preview on the Save3DModel node(s)
		if (GraphEditor.IsValid())
		{
			UStaticMesh* ImportedMesh = LoadObject<UStaticMesh>(nullptr, *ImportedAssetPath);
			if (!ImportedMesh)
			{
				// Try with package.object format
				FString PackagePath = FPackageName::ObjectPathToPackageName(ImportedAssetPath);
				FString ObjectName = FPackageName::ObjectPathToObjectName(ImportedAssetPath);
				if (ObjectName.IsEmpty())
				{
					int32 LastSlash;
					if (ImportedAssetPath.FindLastChar('/', LastSlash))
					{
						ObjectName = ImportedAssetPath.Mid(LastSlash + 1);
					}
				}
				FString FullObjectPath = PackagePath + TEXT(".") + ObjectName;
				ImportedMesh = LoadObject<UStaticMesh>(nullptr, *FullObjectPath);
			}

			if (ImportedMesh)
			{
				for (const FGraphNode& Node : GraphEditor->GetNodes())
				{
					if (Node.ClassType == UESave3DModelClassType)
					{
						GraphEditor->SetNodeMeshPreview(Node.Id, ImportedMesh);
					}
				}
			}
		}

		// Clear the 3D model result so it doesn't trigger again
		if (HttpClient.IsValid())
		{
			HttpClient->Clear3DModelResult();
		}

		return false; // One-shot
	}));
}

void SViewGenPanel::Execute3DAssetExportNodes()
{
	if (!GraphEditor.IsValid() || !HttpClient.IsValid()) return;

	if (!HttpClient->Has3DModelResult())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen 3DAssetExport: No 3D model detected in workflow output"));
		UpdateStatusText(TEXT("No 3D model found in workflow output"));
		return;
	}

	// Collect settings from the first __UE_3DAssetExport__ node in the graph
	FString AssetPath = TEXT("/Game/ViewGen/Models");
	FString AssetName;
	FString CollisionMode = TEXT("Project Default");
	bool bEnableNanite = false;
	bool bPlaceInLevel = true;

	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	for (const FGraphNode& Node : GraphNodes)
	{
		if (Node.ClassType != UE3DAssetExportClassType) continue;

		const FString* AssetPathVal = Node.WidgetValues.Find(TEXT("asset_path"));
		const FString* AssetNameVal = Node.WidgetValues.Find(TEXT("asset_name"));
		const FString* CollisionVal = Node.WidgetValues.Find(TEXT("collision"));
		const FString* NaniteVal = Node.WidgetValues.Find(TEXT("nanite"));
		const FString* PlaceVal = Node.WidgetValues.Find(TEXT("place_in_level"));

		if (AssetPathVal && !AssetPathVal->IsEmpty()) AssetPath = *AssetPathVal;
		if (AssetNameVal) AssetName = *AssetNameVal;
		if (CollisionVal && !CollisionVal->IsEmpty()) CollisionMode = *CollisionVal;
		if (NaniteVal) bEnableNanite = (*NaniteVal == TEXT("true"));
		if (PlaceVal) bPlaceInLevel = (*PlaceVal == TEXT("true"));
		break; // Use first node's settings
	}

	// Get the detected model file info
	FString ModelFilename = HttpClient->GetLast3DModelFilename();
	FString ModelSubfolder = HttpClient->GetLast3DModelSubfolder();
	FString ModelFolderType = HttpClient->GetLast3DModelFolderType();

	FString FileExtension = FPaths::GetExtension(ModelFilename, true);
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(ModelFilename);
		AssetName = AssetName.Replace(TEXT(" "), TEXT("_"));
		AssetName = AssetName.Replace(TEXT("-"), TEXT("_"));
	}

	FString SaveDir = FPaths::ProjectSavedDir() / TEXT("ComfyUI3DModels");
	FString LocalFilePath = SaveDir / (AssetName + FileExtension);

	UpdateStatusText(FString::Printf(TEXT("Downloading 3D model: %s"), *ModelFilename));
	UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Downloading '%s' from ComfyUI (subfolder=%s, type=%s)"),
		*ModelFilename, *ModelSubfolder, *ModelFolderType);

	FString CapturedAssetPath = AssetPath;
	FString CapturedAssetName = AssetName;
	FString CapturedCollisionMode = CollisionMode;
	bool bCapturedNanite = bEnableNanite;
	bool bCapturedPlace = bPlaceInLevel;
	FString CapturedModelFilename = ModelFilename;
	FString CapturedModelSubfolder = ModelSubfolder;
	FString CapturedLocalFilePath = LocalFilePath;

	// Download and import — reuses the same retry strategy as Save3DModel
	HttpClient->DownloadComfyUIFile(ModelFilename, ModelSubfolder, ModelFolderType, LocalFilePath,
		[this, CapturedAssetPath, CapturedAssetName, CapturedCollisionMode, bCapturedNanite, bCapturedPlace,
		 CapturedModelFilename, CapturedModelSubfolder, CapturedLocalFilePath, ModelFolderType](const FString& DownloadedPath)
		{
			if (DownloadedPath.IsEmpty())
			{
				// Retry with alternate folder types
				TArray<FString> AlternateTypes;
				if (ModelFolderType != TEXT("temp")) AlternateTypes.Add(TEXT("temp"));
				if (ModelFolderType != TEXT("output")) AlternateTypes.Add(TEXT("output"));
				if (ModelFolderType != TEXT("input")) AlternateTypes.Add(TEXT("input"));

				if (AlternateTypes.Num() > 0 && HttpClient.IsValid())
				{
					FString RetryType = AlternateTypes[0];
					UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Retrying download with type='%s'"), *RetryType);

					HttpClient->DownloadComfyUIFile(CapturedModelFilename, CapturedModelSubfolder, RetryType, CapturedLocalFilePath,
						[this, CapturedAssetPath, CapturedAssetName, CapturedCollisionMode, bCapturedNanite, bCapturedPlace](const FString& RetryPath)
						{
							if (RetryPath.IsEmpty())
							{
								UpdateStatusText(TEXT("3D Asset Export: Download failed"));
								return;
							}
							Handle3DAssetExport(RetryPath, CapturedAssetPath, CapturedAssetName, CapturedCollisionMode, bCapturedNanite, bCapturedPlace);
						});
					return;
				}
				UpdateStatusText(TEXT("3D Asset Export: Download failed"));
				return;
			}
			Handle3DAssetExport(DownloadedPath, CapturedAssetPath, CapturedAssetName, CapturedCollisionMode, bCapturedNanite, bCapturedPlace);
		});
}

void SViewGenPanel::Handle3DAssetExport(const FString& DownloadedPath, const FString& AssetPath,
	const FString& AssetName, const FString& CollisionMode, bool bEnableNanite, bool bPlaceInLevel)
{
	UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Model downloaded to %s"), *DownloadedPath);

	// ALL UObject/Slate operations must happen on the game thread, and critically
	// NOT inside the task graph — the Interchange GLB importer pumps the task graph
	// internally, which causes a RecursionGuard assertion if we're already in a task.
	// FTSTicker runs during the main loop tick, safely outside the task graph.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, DownloadedPath, AssetPath, AssetName, CollisionMode, bEnableNanite, bPlaceInLevel](float) -> bool
	{
		// Import into the content browser
		FString ImportedAssetPath = ImportModelFileToContentBrowser(DownloadedPath, AssetPath, AssetName);
		if (ImportedAssetPath.IsEmpty())
		{
			UpdateStatusText(TEXT("3D Asset Export: Failed to import into content browser"));
			return false;
		}

		// Load the imported static mesh so we can apply post-import settings
		UStaticMesh* ImportedMesh = LoadObject<UStaticMesh>(nullptr, *ImportedAssetPath);
		if (!ImportedMesh)
		{
			// Try package.object format
			FString PackagePath = FPackageName::ObjectPathToPackageName(ImportedAssetPath);
			FString ObjectName = FPackageName::ObjectPathToObjectName(ImportedAssetPath);
			if (ObjectName.IsEmpty())
			{
				int32 LastSlash;
				if (ImportedAssetPath.FindLastChar('/', LastSlash))
				{
					ObjectName = ImportedAssetPath.Mid(LastSlash + 1);
				}
			}
			FString FullObjectPath = PackagePath + TEXT(".") + ObjectName;
			ImportedMesh = LoadObject<UStaticMesh>(nullptr, *FullObjectPath);
		}

		if (ImportedMesh)
		{
			Apply3DAssetExportSettings(ImportedMesh, CollisionMode, bEnableNanite);

			// Set mesh preview on the 3D Asset Export node(s)
			if (GraphEditor.IsValid())
			{
				for (const FGraphNode& Node : GraphEditor->GetNodes())
				{
					if (Node.ClassType == UE3DAssetExportClassType)
					{
						GraphEditor->SetNodeMeshPreview(Node.Id, ImportedMesh);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen 3DAssetExport: Could not load imported mesh at '%s' for post-import settings"), *ImportedAssetPath);
		}

		UpdateStatusText(FString::Printf(TEXT("3D Asset exported: %s"), *ImportedAssetPath));

		if (bPlaceInLevel && ImportedMesh)
		{
			AActor* SpawnedActor = PlaceAssetInLevel(ImportedAssetPath);
			if (SpawnedActor)
			{
				UpdateStatusText(FString::Printf(TEXT("3D Asset '%s' exported and placed in level"), *AssetName));
			}
		}

		// Clear so it doesn't trigger again
		if (HttpClient.IsValid())
		{
			HttpClient->Clear3DModelResult();
		}

		return false; // One-shot, don't repeat
	}));
}

void SViewGenPanel::ExecuteImageUpresExport()
{
	if (!GraphEditor.IsValid()) return;

	// Find the first Image Upres node in the graph
	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	const FGraphNode* UpresNode = nullptr;
	for (const FGraphNode& Node : GraphNodes)
	{
		if (Node.ClassType == UEImageUpresClassType)
		{
			UpresNode = &Node;
			break;
		}
	}
	if (!UpresNode) return;

	// Read settings from the node
	FString OutputFormat = TEXT("EXR");
	FString BitDepth = TEXT("16");
	FString SavePath;
	FString FilenamePrefix = TEXT("ViewGen_Upres");

	const FString* FormatVal = UpresNode->WidgetValues.Find(TEXT("output_format"));
	if (FormatVal) OutputFormat = *FormatVal;
	const FString* BitVal = UpresNode->WidgetValues.Find(TEXT("output_bit_depth"));
	if (BitVal) BitDepth = *BitVal;
	const FString* PathVal = UpresNode->WidgetValues.Find(TEXT("save_path"));
	if (PathVal && !PathVal->IsEmpty()) SavePath = *PathVal;
	const FString* PrefixVal = UpresNode->WidgetValues.Find(TEXT("filename_prefix"));
	if (PrefixVal && !PrefixVal->IsEmpty()) FilenamePrefix = *PrefixVal;

	// Use ComfyUI output directory if no save_path specified
	if (SavePath.IsEmpty())
	{
		SavePath = FPaths::ProjectSavedDir() / TEXT("ViewGen_Upres");
	}

	// Get the result texture from the latest history entry
	if (ImageHistory.Num() == 0 || HistoryIndex < 0 || HistoryIndex >= ImageHistory.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: No result image available for export"));
		UpdateStatusText(TEXT("Image Upres: No result image to export"));
		return;
	}

	UTexture2D* SourceTexture = ImageHistory[HistoryIndex].Texture;
	if (!SourceTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: Result texture is null"));
		UpdateStatusText(TEXT("Image Upres: Result texture is null"));
		return;
	}

	// Read the pixel data from the texture via LockReadOnly on mip 0.
	// Same approach as TextureToBase64PNG — works for transient textures
	// created by DecodeImageToTexture since bulk data is still CPU-resident.
	const int32 Width = SourceTexture->GetSizeX();
	const int32 Height = SourceTexture->GetSizeY();
	const int32 NumPixels = Width * Height;

	UE_LOG(LogTemp, Log, TEXT("ViewGen ImageUpres: Exporting %dx%d (%s %s-bit) from history texture"),
		Width, Height, *OutputFormat, *BitDepth);

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(NumPixels);

	FTexture2DMipMap& Mip = SourceTexture->GetPlatformData()->Mips[0];
	const void* PixelData = Mip.BulkData.LockReadOnly();
	if (!PixelData)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: Failed to lock texture bulk data for reading"));
		UpdateStatusText(TEXT("Image Upres: Failed to read texture data"));
		return;
	}
	FMemory::Memcpy(Pixels.GetData(), PixelData, NumPixels * sizeof(FColor));
	Mip.BulkData.Unlock();

	// Ensure output directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*SavePath);

	// Build filename with timestamp
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString Extension = (OutputFormat == TEXT("EXR")) ? TEXT(".exr") : TEXT(".png");
	FString OutputFilename = FString::Printf(TEXT("%s_%s%s"), *FilenamePrefix, *Timestamp, *Extension);
	FString FullOutputPath = SavePath / OutputFilename;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	if (OutputFormat == TEXT("EXR"))
	{
		// EXR is a floating-point format. UE's ExrImageWrapper expects FFloat16Color
		// (half-float RGBA) data when using 16-bit. We convert FColor (8-bit BGRA)
		// to FFloat16Color, which gives us true 16-bit half-float precision in the EXR.
		int32 BitDepthInt = FCString::Atoi(*BitDepth);

		TSharedPtr<IImageWrapper> EXRWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);

		// UE's ExrImageWrapper only accepts ERGBFormat::RGBAF (or GrayF) with
		// BitDepth 16 (FFloat16) or 32 (float). All other format/depth combos
		// fail CanSetRawFormat() silently, leaving RawData empty.
		if (BitDepthInt == 16)
		{
			// Convert FColor (8-bit BGRA) → FFloat16Color (half-float RGBA)
			TArray<FFloat16Color> HalfPixels;
			HalfPixels.SetNumUninitialized(NumPixels);

			for (int32 i = 0; i < NumPixels; ++i)
			{
				const FColor& C = Pixels[i];
				FLinearColor Linear = C.ReinterpretAsLinear();
				HalfPixels[i] = FFloat16Color(Linear);
			}

			bool bSetOk = EXRWrapper->SetRaw(HalfPixels.GetData(), HalfPixels.Num() * sizeof(FFloat16Color),
				Width, Height, ERGBFormat::RGBAF, 16);
			if (!bSetOk)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: EXR SetRaw failed (16-bit RGBAF, %dx%d, %lld bytes)"),
					Width, Height, (int64)HalfPixels.Num() * sizeof(FFloat16Color));
				UpdateStatusText(TEXT("Image Upres: EXR encoding setup failed"));
				return;
			}
		}
		else
		{
			// 8-bit requested but EXR only supports float formats —
			// convert to 32-bit float RGBAF for maximum compatibility
			TArray<FLinearColor> FloatPixels;
			FloatPixels.SetNumUninitialized(NumPixels);
			for (int32 i = 0; i < NumPixels; ++i)
			{
				FloatPixels[i] = Pixels[i].ReinterpretAsLinear();
			}

			bool bSetOk = EXRWrapper->SetRaw(FloatPixels.GetData(), FloatPixels.Num() * sizeof(FLinearColor),
				Width, Height, ERGBFormat::RGBAF, 32);
			if (!bSetOk)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: EXR SetRaw failed (32-bit RGBAF, %dx%d, %lld bytes)"),
					Width, Height, (int64)FloatPixels.Num() * sizeof(FLinearColor));
				UpdateStatusText(TEXT("Image Upres: EXR encoding setup failed"));
				return;
			}
		}

		TArray64<uint8> CompressedData = EXRWrapper->GetCompressed(100);
		if (CompressedData.Num() > 0)
		{
			TArray<uint8> FileBytes;
			FileBytes.SetNumUninitialized(CompressedData.Num());
			FMemory::Memcpy(FileBytes.GetData(), CompressedData.GetData(), CompressedData.Num());

			if (FFileHelper::SaveArrayToFile(FileBytes, *FullOutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen ImageUpres: Saved %d-bit EXR (%dx%d) to %s"),
					BitDepthInt, Width, Height, *FullOutputPath);
				UpdateStatusText(FString::Printf(TEXT("Image Upres: Saved %d-bit EXR → %s"), BitDepthInt, *OutputFilename));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: Failed to write file: %s"), *FullOutputPath);
				UpdateStatusText(TEXT("Image Upres: Failed to save EXR file"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: EXR compression returned empty data"));
			UpdateStatusText(TEXT("Image Upres: EXR encoding failed"));
		}
	}
	else
	{
		// PNG output — FColor is already BGRA which IImageWrapper handles natively
		TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		PNGWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
			Width, Height, ERGBFormat::BGRA, 8);

		TArray64<uint8> CompressedData = PNGWrapper->GetCompressed(100);
		if (CompressedData.Num() > 0)
		{
			TArray<uint8> FileBytes;
			FileBytes.SetNumUninitialized(CompressedData.Num());
			FMemory::Memcpy(FileBytes.GetData(), CompressedData.GetData(), CompressedData.Num());

			if (FFileHelper::SaveArrayToFile(FileBytes, *FullOutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen ImageUpres: Saved PNG (%dx%d) to %s"), Width, Height, *FullOutputPath);
				UpdateStatusText(FString::Printf(TEXT("Image Upres: Saved PNG → %s"), *OutputFilename));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: Failed to write file: %s"), *FullOutputPath);
				UpdateStatusText(TEXT("Image Upres: Failed to save PNG file"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen ImageUpres: PNG compression returned empty data"));
			UpdateStatusText(TEXT("Image Upres: PNG encoding failed"));
		}
	}
}

void SViewGenPanel::Apply3DAssetExportSettings(UStaticMesh* Mesh, const FString& CollisionMode, bool bEnableNanite)
{
	if (!Mesh) return;

	UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Applying settings — collision='%s', nanite=%s"),
		*CollisionMode, bEnableNanite ? TEXT("true") : TEXT("false"));

	bool bModified = false;

	// --- Collision ---
	if (CollisionMode != TEXT("Project Default"))
	{
		UBodySetup* BodySetup = Mesh->GetBodySetup();
		if (BodySetup)
		{
			if (CollisionMode == TEXT("No Collision"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseDefault;
				BodySetup->RemoveSimpleCollision();
				bModified = true;
			}
			else if (CollisionMode == TEXT("Use Complex As Simple"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
				bModified = true;
			}
			else if (CollisionMode == TEXT("Auto Convex"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseDefault;
				bModified = true;
			}
		}
	}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7
	// --- Nanite ---
	// GetNaniteSettings / SetNaniteSettings were added in UE 5.7.
	if (bEnableNanite)
	{
		// Check if Nanite is already enabled (Interchange GLB importer often builds
		// Nanite data during import). Only modify if not already set.
		FMeshNaniteSettings CurrentNaniteSettings = Mesh->GetNaniteSettings();
		if (!CurrentNaniteSettings.bEnabled)
		{
			FMeshNaniteSettings NewNaniteSettings;
			NewNaniteSettings.bEnabled = true;
			Mesh->SetNaniteSettings(NewNaniteSettings);
			bModified = true;
			UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Nanite enabled for mesh"));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen 3DAssetExport: Nanite already enabled by importer, skipping"));
		}
	}
#endif

	if (bModified)
	{
		// Only mark the package dirty — do NOT call PostEditChange().
		// The Interchange GLB importer already performs a full mesh build including
		// Nanite data generation. Calling PostEditChange on an Interchange-managed mesh
		// triggers a redundant rebuild that creates internal UObjects which conflict
		// with Interchange's object lifecycle, causing Index >= 0 assertions when the
		// mesh editor preview world is later destroyed.
		Mesh->MarkPackageDirty();
	}
}

void SViewGenPanel::ExecuteMeshyImportNodes()
{
	if (!GraphEditor.IsValid()) return;

	// Collect the first __UE_MeshyImport__ node's settings
	FString ModelFormat = TEXT("GLB");
	FString AssetPath = TEXT("/Game/ViewGen/MeshyModels");
	FString AssetName;
	bool bPlaceInLevel = true;
	bool bFoundImportNode = false;

	const TArray<FGraphNode>& GraphNodes = GraphEditor->GetNodes();
	for (const FGraphNode& Node : GraphNodes)
	{
		if (Node.ClassType != UEMeshyImportClassType) continue;

		bFoundImportNode = true;

		const FString* FormatVal = Node.WidgetValues.Find(TEXT("model_format"));
		const FString* AssetPathVal = Node.WidgetValues.Find(TEXT("asset_path"));
		const FString* AssetNameVal = Node.WidgetValues.Find(TEXT("asset_name"));
		const FString* PlaceVal = Node.WidgetValues.Find(TEXT("place_in_level"));

		if (FormatVal) ModelFormat = *FormatVal;
		if (AssetPathVal) AssetPath = *AssetPathVal;
		if (AssetNameVal) AssetName = *AssetNameVal;
		if (PlaceVal) bPlaceInLevel = (*PlaceVal == TEXT("true"));
		break; // Use first import node's settings
	}

	// If no import node, use defaults but still proceed (the Meshy workflow completed)
	if (!bFoundImportNode)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: No __UE_MeshyImport__ node found, using defaults"));
	}

	// Strategy 1: Check SAM3 segments for a completed task ID
	for (const FSAM3Segment& Seg : SAM3Segments)
	{
		if (Seg.MeshyStatus == TEXT("SUCCEEDED") && !Seg.MeshyTaskId.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Using SAM3 segment task %s"), *Seg.MeshyTaskId);
			FetchAndImportMeshyTask(Seg.MeshyTaskId, ModelFormat, AssetPath, AssetName, bPlaceInLevel);
			return;
		}
	}

	// Strategy 2: UE-side Meshy API call with the pending image
	// The MeshyImageToModelNode was skipped during export; we handle it here.
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (Settings->MeshyApiKey.IsEmpty())
	{
		UpdateStatusText(TEXT("Meshy Import: Meshy API key is not set. Configure it in Project Settings > Plugins > ViewGen."));
		return;
	}

	// Get the image data — either from a pending base64 or by downloading from ComfyUI
	if (!PendingMeshyImageBase64.IsEmpty())
	{
		// We already have the image (from viewport capture)
		UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Using pending viewport image for Meshy API call"));
		LaunchMeshyTaskFromImage(PendingMeshyImageBase64, ModelFormat, AssetPath, AssetName, bPlaceInLevel);
		PendingMeshyImageBase64.Empty();
	}
	else if (!PendingMeshyImageFilename.IsEmpty())
	{
		// Download the raw image from ComfyUI's input folder as base64
		UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Downloading '%s' from ComfyUI for Meshy API call"), *PendingMeshyImageFilename);
		UpdateStatusText(TEXT("Meshy Import: Fetching source image from ComfyUI..."));

		FString CapturedFormat = ModelFormat;
		FString CapturedAssetPath = AssetPath;
		FString CapturedAssetName = AssetName;
		bool bCapturedPlace = bPlaceInLevel;
		FString CapturedFilename = PendingMeshyImageFilename;
		PendingMeshyImageFilename.Empty();

		HttpClient->DownloadComfyUIImageAsBase64(CapturedFilename, [this, CapturedFormat, CapturedAssetPath, CapturedAssetName, bCapturedPlace](const FString& Base64PNG)
		{
			if (Base64PNG.IsEmpty())
			{
				UpdateStatusText(TEXT("Meshy Import: Failed to download source image from ComfyUI"));
				return;
			}

			UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Image downloaded, launching Meshy task"));
			LaunchMeshyTaskFromImage(Base64PNG, CapturedFormat, CapturedAssetPath, CapturedAssetName, bCapturedPlace);
		});
	}
	else
	{
		UpdateStatusText(TEXT("Meshy Import: No source image available for 3D generation"));
		UE_LOG(LogTemp, Warning, TEXT("ViewGen MeshyImport: No pending image and no SAM3 segments"));
	}
}

void SViewGenPanel::FetchAndImportMeshyTask(const FString& TaskId, const FString& ModelFormat,
	const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel)
{
	// Poll the specific task to get the full result with model URLs
	TSharedPtr<FMeshyApiClient> PollClient = MakeShareable(new FMeshyApiClient());

	FString CapturedFormat = ModelFormat;
	FString CapturedAssetPath = AssetPath;
	FString CapturedAssetName = AssetName;
	bool bCapturedPlace = bPlaceInLevel;

	PollClient->OnTaskComplete.BindLambda([this, CapturedFormat, CapturedAssetPath, CapturedAssetName, bCapturedPlace, PollClient](const FMeshyTaskResult& Result)
	{
		if (Result.Status == TEXT("SUCCEEDED"))
		{
			ImportMeshyModelToLevel(Result, CapturedFormat, CapturedAssetPath, CapturedAssetName, bCapturedPlace);
		}
		else
		{
			UpdateStatusText(FString::Printf(TEXT("Meshy Import: Task %s status: %s"), *Result.TaskId, *Result.Status));
		}
	});

	PollClient->OnTaskProgress.BindLambda([this](const FMeshyTaskResult& Result)
	{
		UpdateStatusText(FString::Printf(TEXT("Meshy Import: Task %s still processing (%d%%)..."),
			*Result.TaskId, Result.Progress));
	});

	PollClient->PollTask(TaskId);
}

void SViewGenPanel::LaunchMeshyTaskFromImage(const FString& Base64PNG, const FString& ModelFormat,
	const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel)
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	UpdateStatusText(TEXT("Meshy: Creating Image-to-3D task..."));
	UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Launching Meshy task (image size: %d chars base64)"), Base64PNG.Len());

	TSharedPtr<FMeshyApiClient> TaskClient = MakeShareable(new FMeshyApiClient());

	FString CapturedFormat = ModelFormat;
	FString CapturedAssetPath = AssetPath;
	FString CapturedAssetName = AssetName;
	bool bCapturedPlace = bPlaceInLevel;

	TaskClient->OnTaskCreated.BindLambda([this, CapturedFormat, CapturedAssetPath, CapturedAssetName, bCapturedPlace, TaskClient](bool bSuccess, const FString& TaskIdOrError)
	{
		if (!bSuccess)
		{
			UpdateStatusText(FString::Printf(TEXT("Meshy: Failed to create task: %s"), *TaskIdOrError.Left(200)));
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Task created: %s — polling for completion..."), *TaskIdOrError);
		UpdateStatusText(FString::Printf(TEXT("Meshy: Task %s created, waiting for 3D model..."), *TaskIdOrError.Left(12)));

		// Start polling for completion
		FString TaskId = TaskIdOrError;
		FString CF = CapturedFormat;
		FString CP = CapturedAssetPath;
		FString CN = CapturedAssetName;
		bool bCP = bCapturedPlace;

		// Set up a poll timer
		if (GEditor)
		{
			GEditor->GetTimerManager()->SetTimer(MeshyPollTimer,
				FTimerDelegate::CreateLambda([this, TaskId, CF, CP, CN, bCP, TaskClient]()
				{
					TaskClient->OnTaskComplete.BindLambda([this, CF, CP, CN, bCP](const FMeshyTaskResult& Result)
					{
						if (GEditor) GEditor->GetTimerManager()->ClearTimer(MeshyPollTimer);

						if (Result.Status == TEXT("SUCCEEDED"))
						{
							UpdateStatusText(TEXT("Meshy: 3D model ready! Downloading..."));
							ImportMeshyModelToLevel(Result, CF, CP, CN, bCP);
						}
						else
						{
							UpdateStatusText(FString::Printf(TEXT("Meshy: Task %s — %s"), *Result.TaskId, *Result.Status));
						}
					});

					TaskClient->OnTaskProgress.BindLambda([this](const FMeshyTaskResult& Result)
					{
						UpdateStatusText(FString::Printf(TEXT("Meshy: Generating 3D model... %d%%"), Result.Progress));
					});

					TaskClient->PollTask(TaskId);
				}),
				5.0f,  // Poll every 5 seconds
				true   // Repeat
			);
		}
	});

	TaskClient->CreateImageTo3DTask(Base64PNG, Settings->bMeshyEnablePBR, Settings->bMeshyRemesh);
}

void SViewGenPanel::ImportMeshyModelToLevel(const FMeshyTaskResult& TaskResult, const FString& ModelFormat,
	const FString& AssetPath, const FString& AssetName, bool bPlaceInLevel)
{
	// Determine which URL to use based on the selected format
	FString ModelUrl;
	FString FileExtension;
	if (ModelFormat == TEXT("FBX") && !TaskResult.FBXUrl.IsEmpty())
	{
		ModelUrl = TaskResult.FBXUrl;
		FileExtension = TEXT(".fbx");
	}
	else if (ModelFormat == TEXT("OBJ") && !TaskResult.OBJUrl.IsEmpty())
	{
		ModelUrl = TaskResult.OBJUrl;
		FileExtension = TEXT(".obj");
	}
	else if (!TaskResult.GLBUrl.IsEmpty())
	{
		ModelUrl = TaskResult.GLBUrl;
		FileExtension = TEXT(".glb");
	}
	else
	{
		// Fallback: try any available URL
		if (!TaskResult.GLBUrl.IsEmpty()) { ModelUrl = TaskResult.GLBUrl; FileExtension = TEXT(".glb"); }
		else if (!TaskResult.FBXUrl.IsEmpty()) { ModelUrl = TaskResult.FBXUrl; FileExtension = TEXT(".fbx"); }
		else if (!TaskResult.OBJUrl.IsEmpty()) { ModelUrl = TaskResult.OBJUrl; FileExtension = TEXT(".obj"); }
	}

	if (ModelUrl.IsEmpty())
	{
		UpdateStatusText(TEXT("Meshy Import: No model URL available for selected format"));
		return;
	}

	// Determine asset name
	FString FinalAssetName = AssetName;
	if (FinalAssetName.IsEmpty())
	{
		FinalAssetName = FString::Printf(TEXT("Meshy_%s"), *TaskResult.TaskId.Left(12));
	}

	// Download the model to a temp location
	FString SaveDir = FPaths::ProjectSavedDir() / TEXT("MeshyModels");
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*SaveDir);
	FString LocalFilePath = SaveDir / (FinalAssetName + FileExtension);

	UpdateStatusText(FString::Printf(TEXT("Meshy Import: Downloading %s model..."), *ModelFormat));

	// Capture parameters for the download completion lambda
	FString CapturedAssetPath = AssetPath;
	FString CapturedAssetName = FinalAssetName;
	bool bCapturedPlace = bPlaceInLevel;

	// Create a dedicated client for this download
	TSharedPtr<FMeshyApiClient> DownloadClient = MakeShareable(new FMeshyApiClient());

	DownloadClient->OnModelDownloaded.BindLambda([this, CapturedAssetPath, CapturedAssetName, bCapturedPlace, DownloadClient](const FString& DownloadedPath)
	{
		if (DownloadedPath.IsEmpty())
		{
			UpdateStatusText(TEXT("Meshy Import: Download failed"));
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Model downloaded to %s"), *DownloadedPath);

		// Defer to next main-loop tick via FTSTicker (game thread, outside task graph).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[this, DownloadedPath, CapturedAssetPath, CapturedAssetName, bCapturedPlace](float) -> bool
		{
			// Import into the content browser
			FString ImportedAssetPath = ImportModelFileToContentBrowser(DownloadedPath, CapturedAssetPath, CapturedAssetName);
			if (ImportedAssetPath.IsEmpty())
			{
				UpdateStatusText(TEXT("Meshy Import: Failed to import model into content browser"));
				return false;
			}

			UpdateStatusText(FString::Printf(TEXT("Meshy Import: Asset imported to %s"), *ImportedAssetPath));

			// Optionally place in the current level
			if (bCapturedPlace)
			{
				AActor* SpawnedActor = PlaceAssetInLevel(ImportedAssetPath);
				if (SpawnedActor)
				{
					UpdateStatusText(FString::Printf(TEXT("Meshy Import: Placed '%s' in level at world origin"), *CapturedAssetName));
				}
				else
				{
					UpdateStatusText(FString::Printf(TEXT("Meshy Import: Imported '%s' but failed to place in level"), *CapturedAssetName));
				}
			}

			return false; // One-shot
		}));
	});

	DownloadClient->DownloadModel(ModelUrl, LocalFilePath);
}

FString SViewGenPanel::ImportModelFileToContentBrowser(const FString& LocalFilePath, const FString& DestAssetPath, const FString& DesiredAssetName)
{
	// Use UE's AssetTools to import the file
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Ensure the destination directory exists in the content browser
	FString FullAssetPath = DestAssetPath;
	if (!FullAssetPath.EndsWith(TEXT("/")))
	{
		FullAssetPath += TEXT("/");
	}

	// Import the file
	TArray<FString> FilesToImport;
	FilesToImport.Add(LocalFilePath);

	TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(FilesToImport, FullAssetPath);

	if (ImportedAssets.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen Import: AssetTools.ImportAssets returned no results for %s"), *LocalFilePath);
		return FString();
	}

	// Interchange (GLB/glTF) imports create multiple assets: Materials, Textures,
	// StaticMeshes, etc. in subfolders. We need to find the StaticMesh specifically.
	UStaticMesh* FoundMesh = nullptr;
	UObject* FallbackObj = nullptr;

	for (UObject* Asset : ImportedAssets)
	{
		if (!Asset) continue;

		UE_LOG(LogTemp, Log, TEXT("ViewGen Import: Imported asset [%s]: %s"),
			*Asset->GetClass()->GetName(), *Asset->GetPathName());

		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
		{
			FoundMesh = Mesh;
			break;
		}

		if (!FallbackObj)
		{
			FallbackObj = Asset;
		}
	}

	// If no StaticMesh found directly, search the asset registry for meshes
	// created under the import folder (Interchange creates them in subfolders)
	if (!FoundMesh)
	{
		FString BaseName = FPaths::GetBaseFilename(LocalFilePath);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> FoundAssets;
		AssetRegistry.GetAssetsByPath(FName(*FullAssetPath), FoundAssets, true /* bRecursive */);

		for (const FAssetData& AssetData : FoundAssets)
		{
			// Check asset class name before loading to avoid loading every asset
			FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
			if (ClassName == TEXT("StaticMesh"))
			{
				UObject* LoadedAsset = AssetData.GetAsset();
				FoundMesh = Cast<UStaticMesh>(LoadedAsset);
				if (FoundMesh)
				{
					UE_LOG(LogTemp, Log, TEXT("ViewGen Import: Found StaticMesh via registry: %s"), *AssetData.GetObjectPathString());
					break;
				}
			}
		}
	}

	if (FoundMesh)
	{
		FString MeshPath = FoundMesh->GetPathName();
		UE_LOG(LogTemp, Log, TEXT("ViewGen Import: Using StaticMesh: %s"), *MeshPath);
		// Do NOT rename Interchange-imported assets — they live in a folder hierarchy
		// and renaming individual sub-assets can corrupt cross-references and crash.
		return MeshPath;
	}

	// Fallback: return whatever was imported first (legacy non-Interchange path)
	if (FallbackObj)
	{
		FString ImportedPath = FallbackObj->GetPathName();
		UE_LOG(LogTemp, Log, TEXT("ViewGen Import: No StaticMesh found, using fallback: %s"), *ImportedPath);
		return ImportedPath;
	}

	UE_LOG(LogTemp, Warning, TEXT("ViewGen Import: No valid assets imported from %s"), *LocalFilePath);
	return FString();
}

AActor* SViewGenPanel::PlaceAssetInLevel(const FString& AssetContentPath)
{
	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen MeshyImport: No editor world available for placement"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	// Try to load the asset as a Static Mesh first
	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *AssetContentPath);
	if (!StaticMesh)
	{
		// The import may have created the asset with a slightly different path; try finding it
		FString PackagePath = FPackageName::ObjectPathToPackageName(AssetContentPath);
		FString ObjectName = FPackageName::ObjectPathToObjectName(AssetContentPath);
		if (ObjectName.IsEmpty())
		{
			// Try extracting from the path
			int32 LastSlash;
			if (AssetContentPath.FindLastChar('/', LastSlash))
			{
				ObjectName = AssetContentPath.Mid(LastSlash + 1);
			}
		}

		FString FullObjectPath = PackagePath + TEXT(".") + ObjectName;
		StaticMesh = LoadObject<UStaticMesh>(nullptr, *FullObjectPath);
	}

	if (StaticMesh)
	{
		// Spawn a StaticMeshActor at world origin
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParams
		);

		if (MeshActor && MeshActor->GetStaticMeshComponent())
		{
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
			MeshActor->SetActorLabel(StaticMesh->GetName());

			UE_LOG(LogTemp, Log, TEXT("ViewGen MeshyImport: Placed StaticMeshActor '%s' in level at origin"), *MeshActor->GetName());
			return MeshActor;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ViewGen MeshyImport: Could not load or place asset at path: %s"), *AssetContentPath);
	return nullptr;
}

// ============================================================================
// Graph Auto-Persistence
// ============================================================================

FString SViewGenPanel::GetAutoSaveGraphPath()
{
	return FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("LastGraph.json");
}

void SViewGenPanel::AutoSaveGraph()
{
	if (!GraphEditor.IsValid()) return;

	// Only auto-save if the graph has content
	if (GraphEditor->GetNodes().Num() == 0) return;

	TSharedPtr<FJsonObject> GraphJson = GraphEditor->SerializeGraph();
	if (!GraphJson.IsValid()) return;

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(GraphJson.ToSharedRef(), Writer);

	const FString FilePath = GetAutoSaveGraphPath();
	const FString Dir = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	FFileHelper::SaveStringToFile(OutputString, *FilePath);
}

bool SViewGenPanel::RestoreLastGraph()
{
	if (!GraphEditor.IsValid()) return false;

	const FString FilePath = GetAutoSaveGraphPath();

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> GraphJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to parse auto-save graph file"));
		return false;
	}

	if (GraphEditor->DeserializeGraph(GraphJson))
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Restored last-edited graph from auto-save"));
		RefreshLoadImageThumbnails();
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
                                                                                                                                                                                                                                                                                                                                                                                                                                                         