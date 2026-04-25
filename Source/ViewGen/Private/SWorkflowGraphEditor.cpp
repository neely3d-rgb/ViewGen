// Copyright ViewGen. All Rights Reserved.

#include "SWorkflowGraphEditor.h"
#include "MeshPreviewRenderer.h"
#include "GenAISettings.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GarbageCollection.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Fonts/FontMeasure.h"
#include "Misc/DefaultValueHelper.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SWorkflowGraphEditor"

// ============================================================================
// Constants
// ============================================================================

namespace GraphConstants
{
	static constexpr float NodeMinWidth = 200.0f;
	static constexpr float NodeHeaderHeight = 28.0f;
	static constexpr float PinRowHeight = 22.0f;
	static constexpr float WidgetRowHeight = 20.0f;
	static constexpr float PinRadius = 5.0f;
	static constexpr float PinHitRadius = 10.0f;
	static constexpr float NodePadding = 6.0f;
	static constexpr float MinZoom = 0.2f;
	static constexpr float MaxZoom = 3.0f;
	static constexpr float ZoomStep = 0.1f;
	static constexpr float ColumnSpacing = 280.0f;
	static constexpr float RowSpacing = 30.0f;
	static constexpr float GridSize = 20.0f;
	static constexpr float ThumbnailHeight = 80.0f;
	static constexpr float ThumbnailPadding = 4.0f;
}

// ============================================================================
// Node Colour Palette (matching original SWorkflowPreviewPanel)
// ============================================================================

FLinearColor SWorkflowGraphEditor::GetNodeColor(const FString& ClassType)
{
	// UE source nodes — distinctive Unreal Engine blue
	if (ClassType == UEViewportClassType || ClassType == UEDepthMapClassType)
		return FLinearColor(0.10f, 0.35f, 0.65f);
	if (ClassType == UECameraDataClassType)
		return FLinearColor(0.15f, 0.40f, 0.55f);
	if (ClassType == UESegmentationClassType)
		return FLinearColor(0.20f, 0.50f, 0.45f); // Teal — distinct from other UE blues
	if (ClassType == UEMeshyImportClassType)
		return FLinearColor(0.55f, 0.30f, 0.55f); // Purple — Meshy 3D import
	if (ClassType == UESave3DModelClassType)
		return FLinearColor(0.2f, 0.55f, 0.55f); // Teal — generic 3D model import
	if (ClassType == UE3DLoaderClassType)
		return FLinearColor(0.30f, 0.55f, 0.40f); // Green-teal — 3D model loader
	if (ClassType == UEImageBridgeClassType)
		return FLinearColor(0.45f, 0.50f, 0.20f); // Olive-gold — image save+passthrough
	if (ClassType == UE3DAssetExportClassType)
		return FLinearColor(0.55f, 0.40f, 0.20f); // Warm amber — 3D asset export
	if (ClassType == UEPromptAdherenceClassType)
		return FLinearColor(0.50f, 0.30f, 0.55f); // Purple — prompt adherence control
	if (ClassType == UEImageUpresClassType)
		return FLinearColor(0.35f, 0.55f, 0.65f);
	if (ClassType == UESequenceClassType)
		return FLinearColor(0.65f, 0.50f, 0.15f); // Gold — execution flow
	if (ClassType == UEVideoToImageClassType)
		return FLinearColor(0.50f, 0.28f, 0.58f); // Purple — video frame extraction

	if (ClassType.Contains(TEXT("Checkpoint")) || ClassType.Contains(TEXT("UNET")) || ClassType.Contains(TEXT("CLIP")) || ClassType.Contains(TEXT("VAELoader")))
		return FLinearColor(0.25f, 0.42f, 0.55f);
	if (ClassType.Contains(TEXT("CLIPTextEncode")))
		return FLinearColor(0.30f, 0.55f, 0.30f);
	if (ClassType.Contains(TEXT("KSampler")) || ClassType.Contains(TEXT("Xlabs")))
		return FLinearColor(0.60f, 0.35f, 0.55f);
	if (ClassType.Contains(TEXT("VAE")))
		return FLinearColor(0.55f, 0.50f, 0.25f);
	if (ClassType.Contains(TEXT("ControlNet")) || ClassType.Contains(TEXT("FluxControl")))
		return FLinearColor(0.55f, 0.35f, 0.20f);
	if (ClassType.Contains(TEXT("LoRA")) || ClassType.Contains(TEXT("Lora")))
		return FLinearColor(0.50f, 0.40f, 0.60f);
	if (ClassType.Contains(TEXT("LoadImage")))
		return FLinearColor(0.35f, 0.50f, 0.45f);
	if (ClassType.Contains(TEXT("LoadVideo")))
		return FLinearColor(0.45f, 0.35f, 0.55f); // Purple-tint — video loading
	if (ClassType.Contains(TEXT("SaveImage")) || ClassType.Contains(TEXT("PreviewImage")))
		return FLinearColor(0.20f, 0.55f, 0.20f);
	if (ClassType.Contains(TEXT("EmptyLatent")))
		return FLinearColor(0.45f, 0.45f, 0.45f);
	if (ClassType.Contains(TEXT("LatentUpscale")))
		return FLinearColor(0.50f, 0.50f, 0.30f);
	if (ClassType.Contains(TEXT("Gemini")) || ClassType.Contains(TEXT("NanoBanana")))
		return FLinearColor(0.20f, 0.45f, 0.60f);
	if (ClassType.Contains(TEXT("Kling")))
		return FLinearColor(0.55f, 0.25f, 0.35f);
	if (ClassType.Contains(TEXT("ImageBatch")))
		return FLinearColor(0.40f, 0.50f, 0.50f);
	return FLinearColor(0.35f, 0.35f, 0.40f);
}

// ============================================================================
// Construction
// ============================================================================

void SWorkflowGraphEditor::Construct(const FArguments& InArgs)
{
	OnGraphChanged = InArgs._OnGraphChanged;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	OnRunToNode = InArgs._OnRunToNode;

	// Register PostGC callback to refresh stale TObjectPtr handles in FSlateBrush objects.
	// GC compaction can run mid-frame (e.g. during SavePackage) between Slate Tick and Paint,
	// invalidating packed indices in heap-allocated brushes that aren't tracked by UPROPERTY.
	PostGCDelegateHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &SWorkflowGraphEditor::OnPostGarbageCollect);
}

SWorkflowGraphEditor::~SWorkflowGraphEditor()
{
	// Unregister PostGC callback
	if (PostGCDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCDelegateHandle);
	}

	// Unroot thumbnail textures
	for (FGraphNode& Node : Nodes)
	{
		if (Node.ThumbnailTexture && ::IsValid(Node.ThumbnailTexture) && Node.ThumbnailTexture->IsRooted())
		{
			Node.ThumbnailTexture->RemoveFromRoot();
		}
		Node.ThumbnailTexture = nullptr;
		Node.ThumbnailBrush.Reset();
	}
}

void SWorkflowGraphEditor::OnPostGarbageCollect()
{
	// After GC compaction, UObject array indices may change. TObjectPtr handles in
	// heap-allocated FSlateBrush objects (not tracked by UPROPERTY) become stale.
	// Re-set ResourceObject from stored raw pointers to create fresh packed handles.
	for (FGraphNode& Node : Nodes)
	{
		if (Node.ThumbnailBrush.IsValid() && Node.ThumbnailTexture && ::IsValid(Node.ThumbnailTexture))
		{
			Node.ThumbnailBrush->SetResourceObject(Node.ThumbnailTexture);
		}
		else if (Node.ThumbnailBrush.IsValid())
		{
			Node.ThumbnailBrush->SetResourceObject(nullptr);
		}

		if (Node.MeshPreview.IsValid())
		{
			Node.MeshPreview->RefreshBrushHandle();
		}
	}
}

FVector2D SWorkflowGraphEditor::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Request a generous default size so Slate allocates geometry and routes events to us.
	// The actual rendered size is determined by the parent SBox constraints.
	return FVector2D(600.0f, 400.0f);
}

// ============================================================================
// Coordinate Transform
// ============================================================================

FVector2D SWorkflowGraphEditor::GraphToLocal(FVector2D GraphPos) const
{
	return (GraphPos + ViewOffset) * ZoomLevel;
}

FVector2D SWorkflowGraphEditor::LocalToGraph(FVector2D LocalPos) const
{
	return LocalPos / ZoomLevel - ViewOffset;
}

// ============================================================================
// Graph Manipulation
// ============================================================================

void SWorkflowGraphEditor::ClearGraph()
{
	// Unroot any thumbnail textures to prevent memory leaks
	// Use stored raw pointer (not GetResourceObject) to avoid stale TObjectPtr crash
	for (FGraphNode& Node : Nodes)
	{
		if (Node.ThumbnailTexture && ::IsValid(Node.ThumbnailTexture) && Node.ThumbnailTexture->IsRooted())
		{
			Node.ThumbnailTexture->RemoveFromRoot();
		}
		Node.ThumbnailTexture = nullptr;
		Node.ThumbnailBrush.Reset();
	}

	Nodes.Empty();
	NodeIndexMap.Empty();
	Connections.Empty();
	SelectedNodeIds.Empty();
	NotifySelectionChanged();
	NextAutoNodeId = 100;
	CurrentFilePath.Empty();
	bGraphDirty = false;
}

FString SWorkflowGraphEditor::AddNodeByType(const FString& ClassType, FVector2D GraphPosition)
{
	PushUndoSnapshot();
	FString NodeId = FString::FromInt(NextAutoNodeId++);

	// Check for UE source nodes first
	if (ClassType == UEViewportClassType || ClassType == UEDepthMapClassType || ClassType == UECameraDataClassType || ClassType == UESegmentationClassType || ClassType == UEMeshyImportClassType || ClassType == UESave3DModelClassType || ClassType == UE3DLoaderClassType || ClassType == UEImageBridgeClassType || ClassType == UE3DAssetExportClassType || ClassType == UEPromptAdherenceClassType || ClassType == UEImageUpresClassType || ClassType == UESequenceClassType || ClassType == UEVideoToImageClassType)
	{
		FGraphNode Node = CreateUESourceNode(ClassType, NodeId, GraphPosition);
		int32 Idx = Nodes.Add(MoveTemp(Node));
		NodeIndexMap.Add(NodeId, Idx);
	}
	else
	{
		const FComfyNodeDef* Def = FComfyNodeDatabase::Get().FindNode(ClassType);

		if (Def)
		{
			FGraphNode Node = CreateNodeFromDef(*Def, NodeId, GraphPosition);
			int32 Idx = Nodes.Add(MoveTemp(Node));
			NodeIndexMap.Add(NodeId, Idx);
		}
		else
		{
			// Unknown node type — create a minimal placeholder
			FGraphNode Node;
			Node.Id = NodeId;
			Node.ClassType = ClassType;
			Node.Title = ClassType;
			Node.Position = GraphPosition;
			Node.HeaderColor = GetNodeColor(ClassType);
			Node.Size = FVector2D(GraphConstants::NodeMinWidth, GraphConstants::NodeHeaderHeight + 20.0f);

			int32 Idx = Nodes.Add(MoveTemp(Node));
			NodeIndexMap.Add(NodeId, Idx);
		}
	}

	NotifyGraphChanged();
	return NodeId;
}

void SWorkflowGraphEditor::RemoveNode(const FString& NodeId)
{
	PushUndoSnapshot();

	RemoveConnectionsForNode(NodeId);

	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return;

	int32 RemoveIdx = *IdxPtr;

	// Unroot thumbnail texture to prevent memory leak
	// Use stored raw pointer (not GetResourceObject) to avoid stale TObjectPtr crash
	FGraphNode& NodeToRemove = Nodes[RemoveIdx];
	if (NodeToRemove.ThumbnailTexture && ::IsValid(NodeToRemove.ThumbnailTexture) && NodeToRemove.ThumbnailTexture->IsRooted())
	{
		NodeToRemove.ThumbnailTexture->RemoveFromRoot();
	}
	NodeToRemove.ThumbnailTexture = nullptr;
	NodeToRemove.ThumbnailBrush.Reset();

	Nodes.RemoveAt(RemoveIdx);

	// Rebuild index map
	NodeIndexMap.Empty();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		NodeIndexMap.Add(Nodes[i].Id, i);
	}

	SelectedNodeIds.Remove(NodeId);
	NotifySelectionChanged();
	NotifyGraphChanged();
}

bool SWorkflowGraphEditor::AddConnection(const FString& SourceNodeId, int32 SourceOutputIndex,
	const FString& TargetNodeId, const FString& TargetInputName)
{
	PushUndoSnapshot();

	// Validate nodes exist
	if (!NodeIndexMap.Contains(SourceNodeId) || !NodeIndexMap.Contains(TargetNodeId))
		return false;

	// Prevent self-connections
	if (SourceNodeId == TargetNodeId) return false;

	// Remove any existing connection to this input (each input can only have one source)
	RemoveConnection(TargetNodeId, TargetInputName);

	// Type checking
	const FGraphNode& SourceNode = Nodes[NodeIndexMap[SourceNodeId]];
	const FGraphNode& TargetNode = Nodes[NodeIndexMap[TargetNodeId]];

	FString OutputType;
	if (SourceOutputIndex < SourceNode.OutputPins.Num())
	{
		OutputType = SourceNode.OutputPins[SourceOutputIndex].Type;
	}

	FString InputType;
	for (const auto& Pin : TargetNode.InputPins)
	{
		if (Pin.Name == TargetInputName)
		{
			InputType = Pin.Type;
			break;
		}
	}

	if (!OutputType.IsEmpty() && !InputType.IsEmpty() && !AreTypesCompatible(OutputType, InputType))
	{
		return false;
	}

	FGraphConnection Conn;
	Conn.SourceNodeId = SourceNodeId;
	Conn.SourceOutputIndex = SourceOutputIndex;
	Conn.TargetNodeId = TargetNodeId;
	Conn.TargetInputName = TargetInputName;

	Connections.Add(Conn);
	NotifyGraphChanged();
	return true;
}

void SWorkflowGraphEditor::RemoveConnection(const FString& TargetNodeId, const FString& TargetInputName)
{
	Connections.RemoveAll([&](const FGraphConnection& C)
	{
		return C.TargetNodeId == TargetNodeId && C.TargetInputName == TargetInputName;
	});
}

void SWorkflowGraphEditor::RemoveConnectionsForNode(const FString& NodeId)
{
	Connections.RemoveAll([&](const FGraphConnection& C)
	{
		return C.SourceNodeId == NodeId || C.TargetNodeId == NodeId;
	});
}

bool SWorkflowGraphEditor::IsInputConnected(const FString& NodeId, const FString& InputName) const
{
	for (const auto& C : Connections)
	{
		if (C.TargetNodeId == NodeId && C.TargetInputName == InputName)
			return true;
	}
	return false;
}

bool SWorkflowGraphEditor::AreTypesCompatible(const FString& OutputType, const FString& InputType)
{
	// Wildcard: "*" matches anything
	if (OutputType == TEXT("*") || InputType == TEXT("*")) return true;

	// Exact match
	if (OutputType == InputType) return true;

	// Common compatible pairs
	if (OutputType == TEXT("CONDITIONING") && InputType == TEXT("CONDITIONING")) return true;

	return false;
}

// ============================================================================
// Node Construction from Definition
// ============================================================================

FGraphNode SWorkflowGraphEditor::CreateNodeFromDef(const FComfyNodeDef& Def, const FString& Id, FVector2D Position) const
{
	FGraphNode Node;
	Node.Id = Id;
	Node.ClassType = Def.ClassType;
	Node.Title = Def.DisplayName;
	Node.Position = Position;
	Node.HeaderColor = GetNodeColor(Def.ClassType);

	// Create input pins for link-type inputs
	int32 PinIdx = 0;
	for (const auto& Input : Def.Inputs)
	{
		if (Input.IsLinkType())
		{
			FGraphPin Pin;
			Pin.Name = Input.Name;
			Pin.Type = Input.Type;
			Pin.bIsInput = true;
			Pin.PinIndex = PinIdx++;
			Pin.OwnerNodeId = Id;
			Node.InputPins.Add(Pin);
		}
		else
		{
			// Widget input — store default value and definition
			FComfyInputDef WidgetDef = Input;
			FString DefaultVal;
			if (Input.Type == TEXT("COMBO"))
			{
				if (Input.ComboOptions.Num() > 0)
				{
					DefaultVal = !Input.DefaultString.IsEmpty() ? Input.DefaultString : Input.ComboOptions[0];
				}
				else
				{
					// V3 dynamic combo with no options fetched — provide true/false fallback
					WidgetDef.ComboOptions = { TEXT("true"), TEXT("false") };
					DefaultVal = !Input.DefaultString.IsEmpty() ? Input.DefaultString : TEXT("true");
				}
			}
			else if (Input.Type == TEXT("INT"))
			{
				DefaultVal = FString::FromInt(static_cast<int32>(Input.DefaultNumber));
			}
			else if (Input.Type == TEXT("FLOAT"))
			{
				DefaultVal = FString::SanitizeFloat(Input.DefaultNumber);
			}
			else if (Input.Type == TEXT("STRING"))
			{
				DefaultVal = Input.DefaultString;
			}
			else if (Input.Type == TEXT("BOOLEAN") || Input.Type == TEXT("BOOL"))
			{
				DefaultVal = Input.DefaultBool ? TEXT("true") : TEXT("false");
			}

			Node.WidgetValues.Add(Input.Name, DefaultVal);
			Node.WidgetInputDefs.Add(Input.Name, WidgetDef);
			Node.WidgetOrder.Add(Input.Name);
		}
	}

	// Create output pins
	PinIdx = 0;
	for (const auto& Output : Def.Outputs)
	{
		FGraphPin Pin;
		Pin.Name = Output.Name;
		Pin.Type = Output.Type;
		Pin.bIsInput = false;
		Pin.PinIndex = PinIdx++;
		Pin.OwnerNodeId = Id;
		Node.OutputPins.Add(Pin);
	}

	ComputeNodeSize(Node);
	return Node;
}

FGraphNode SWorkflowGraphEditor::CreateUESourceNode(const FString& UEClassType, const FString& Id, FVector2D Position) const
{
	FGraphNode Node;
	Node.Id = Id;
	Node.ClassType = UEClassType;
	Node.Position = Position;
	Node.HeaderColor = GetNodeColor(UEClassType);

	if (UEClassType == UEViewportClassType)
	{
		Node.Title = TEXT("UE Viewport Capture");

		// Widget: capture resolution multiplier
		Node.WidgetValues.Add(TEXT("resolution"), TEXT("1.0"));
		FComfyInputDef ResDef;
		ResDef.Name = TEXT("resolution");
		ResDef.Type = TEXT("COMBO");
		ResDef.ComboOptions = { TEXT("0.25"), TEXT("0.5"), TEXT("1.0"), TEXT("2.0") };
		Node.WidgetInputDefs.Add(TEXT("resolution"), ResDef);
		Node.WidgetOrder.Add(TEXT("resolution"));

		// Output: IMAGE
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);
	}
	else if (UEClassType == UEDepthMapClassType)
	{
		Node.Title = TEXT("UE Depth Map");

		// Widget: max depth distance
		Node.WidgetValues.Add(TEXT("max_depth"), TEXT("50000"));
		FComfyInputDef DepthDef;
		DepthDef.Name = TEXT("max_depth");
		DepthDef.Type = TEXT("FLOAT");
		DepthDef.DefaultNumber = 50000.0;
		DepthDef.MinValue = 1000.0;
		DepthDef.MaxValue = 200000.0;
		DepthDef.Step = 1000.0;
		Node.WidgetInputDefs.Add(TEXT("max_depth"), DepthDef);
		Node.WidgetOrder.Add(TEXT("max_depth"));

		// Output: IMAGE
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);
	}
	else if (UEClassType == UECameraDataClassType)
	{
		Node.Title = TEXT("UE Camera Data");

		// Widget: active (enable/disable camera data injection)
		Node.WidgetValues.Add(TEXT("active"), TEXT("true"));
		FComfyInputDef ActiveDef;
		ActiveDef.Name = TEXT("active");
		ActiveDef.Type = TEXT("BOOLEAN");
		ActiveDef.DefaultBool = true;
		Node.WidgetInputDefs.Add(TEXT("active"), ActiveDef);
		Node.WidgetOrder.Add(TEXT("active"));

		// Widget: format
		Node.WidgetValues.Add(TEXT("format"), TEXT("Natural Language"));
		FComfyInputDef FmtDef;
		FmtDef.Name = TEXT("format");
		FmtDef.Type = TEXT("COMBO");
		FmtDef.ComboOptions = { TEXT("Natural Language"), TEXT("Technical"), TEXT("Both") };
		Node.WidgetInputDefs.Add(TEXT("format"), FmtDef);
		Node.WidgetOrder.Add(TEXT("format"));

		// Widget: position (append or prepend)
		Node.WidgetValues.Add(TEXT("position"), TEXT("append"));
		FComfyInputDef PosDef;
		PosDef.Name = TEXT("position");
		PosDef.Type = TEXT("COMBO");
		PosDef.ComboOptions = { TEXT("append"), TEXT("prepend") };
		Node.WidgetInputDefs.Add(TEXT("position"), PosDef);
		Node.WidgetOrder.Add(TEXT("position"));

		// Widget: target (which prompts to inject into)
		Node.WidgetValues.Add(TEXT("target"), TEXT("positive only"));
		FComfyInputDef TargetDef;
		TargetDef.Name = TEXT("target");
		TargetDef.Type = TEXT("COMBO");
		TargetDef.ComboOptions = { TEXT("positive only"), TEXT("all prompts") };
		Node.WidgetInputDefs.Add(TEXT("target"), TargetDef);
		Node.WidgetOrder.Add(TEXT("target"));

		// No pins — this node operates globally at export time,
		// injecting camera data into CLIPTextEncode text fields
	}
	else if (UEClassType == UESegmentationClassType)
	{
		Node.Title = TEXT("UE Segmentation Mask");

		// Widget: capture mode
		Node.WidgetValues.Add(TEXT("mode"), TEXT("Actor ID"));
		FComfyInputDef ModeDef;
		ModeDef.Name = TEXT("mode");
		ModeDef.Type = TEXT("COMBO");
		ModeDef.ComboOptions = { TEXT("Actor ID"), TEXT("Stencil") };
		Node.WidgetInputDefs.Add(TEXT("mode"), ModeDef);
		Node.WidgetOrder.Add(TEXT("mode"));

		// Output: IMAGE (the colour-coded segmentation mask)
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);

		// Output: MASK (a secondary output — same image, useful for nodes that expect MASK type)
		FGraphPin OutMask;
		OutMask.Name = TEXT("MASK");
		OutMask.Type = TEXT("MASK");
		OutMask.bIsInput = false;
		OutMask.PinIndex = 1;
		OutMask.OwnerNodeId = Id;
		Node.OutputPins.Add(OutMask);
	}
	else if (UEClassType == UEMeshyImportClassType)
	{
		Node.Title = TEXT("Meshy Import to Level");

		// Input pin: MESHY_TASK_ID — receives the task ID string from a Meshy Image-to-3D result
		FGraphPin InTaskId;
		InTaskId.Name = TEXT("task_id");
		InTaskId.Type = TEXT("STRING");
		InTaskId.bIsInput = true;
		InTaskId.PinIndex = 0;
		InTaskId.OwnerNodeId = Id;
		Node.InputPins.Add(InTaskId);

		// Widget: model_format — which format to download/import
		Node.WidgetValues.Add(TEXT("model_format"), TEXT("GLB"));
		FComfyInputDef FormatDef;
		FormatDef.Name = TEXT("model_format");
		FormatDef.Type = TEXT("COMBO");
		FormatDef.ComboOptions = { TEXT("GLB"), TEXT("FBX"), TEXT("OBJ") };
		Node.WidgetInputDefs.Add(TEXT("model_format"), FormatDef);
		Node.WidgetOrder.Add(TEXT("model_format"));

		// Widget: asset_path — content browser destination path
		Node.WidgetValues.Add(TEXT("asset_path"), TEXT("/Game/ViewGen/MeshyModels"));
		FComfyInputDef PathDef;
		PathDef.Name = TEXT("asset_path");
		PathDef.Type = TEXT("STRING");
		PathDef.DefaultString = TEXT("/Game/ViewGen/MeshyModels");
		Node.WidgetInputDefs.Add(TEXT("asset_path"), PathDef);
		Node.WidgetOrder.Add(TEXT("asset_path"));

		// Widget: asset_name — name for the imported asset (auto-generated if empty)
		Node.WidgetValues.Add(TEXT("asset_name"), TEXT(""));
		FComfyInputDef NameDef;
		NameDef.Name = TEXT("asset_name");
		NameDef.Type = TEXT("STRING");
		NameDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("asset_name"), NameDef);
		Node.WidgetOrder.Add(TEXT("asset_name"));

		// Widget: place_in_level — whether to auto-place the imported mesh into the current level
		Node.WidgetValues.Add(TEXT("place_in_level"), TEXT("true"));
		FComfyInputDef PlaceDef;
		PlaceDef.Name = TEXT("place_in_level");
		PlaceDef.Type = TEXT("BOOLEAN");
		PlaceDef.DefaultBool = true;
		Node.WidgetInputDefs.Add(TEXT("place_in_level"), PlaceDef);
		Node.WidgetOrder.Add(TEXT("place_in_level"));

		// No output pins — this is a terminal/action node
	}
	else if (UEClassType == UESave3DModelClassType)
	{
		Node.Title = TEXT("UE Save 3D Model");
		Node.HeaderColor = FLinearColor(0.2f, 0.55f, 0.55f); // Teal

		// Input pin: mesh — receives the filename/path of a 3D model from ComfyUI output
		// This connects to the output of any ComfyUI node that produces a mesh file
		// (e.g. TripoSR, InstantMesh, Show Any, Save3DModel, etc.)
		FGraphPin InMeshFile;
		InMeshFile.Name = TEXT("mesh");
		InMeshFile.Type = TEXT("*");  // Wildcard — accept STRING, MESH, or any output
		InMeshFile.bIsInput = true;
		InMeshFile.PinIndex = 0;
		InMeshFile.OwnerNodeId = Id;
		Node.InputPins.Add(InMeshFile);

		// Widget: asset_path — content browser destination path
		Node.WidgetValues.Add(TEXT("asset_path"), TEXT("/Game/ViewGen/Models"));
		FComfyInputDef PathDef;
		PathDef.Name = TEXT("asset_path");
		PathDef.Type = TEXT("STRING");
		PathDef.DefaultString = TEXT("/Game/ViewGen/Models");
		Node.WidgetInputDefs.Add(TEXT("asset_path"), PathDef);
		Node.WidgetOrder.Add(TEXT("asset_path"));

		// Widget: asset_name — name for the imported asset (auto-generated from filename if empty)
		Node.WidgetValues.Add(TEXT("asset_name"), TEXT(""));
		FComfyInputDef NameDef;
		NameDef.Name = TEXT("asset_name");
		NameDef.Type = TEXT("STRING");
		NameDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("asset_name"), NameDef);
		Node.WidgetOrder.Add(TEXT("asset_name"));

		// Widget: place_in_level — whether to auto-place the imported mesh into the current level
		Node.WidgetValues.Add(TEXT("place_in_level"), TEXT("true"));
		FComfyInputDef PlaceDef;
		PlaceDef.Name = TEXT("place_in_level");
		PlaceDef.Type = TEXT("BOOLEAN");
		PlaceDef.DefaultBool = true;
		Node.WidgetInputDefs.Add(TEXT("place_in_level"), PlaceDef);
		Node.WidgetOrder.Add(TEXT("place_in_level"));

		// No output pins — this is a terminal/action node
	}
	else if (UEClassType == UE3DLoaderClassType)
	{
		Node.Title = TEXT("UE 3D Loader");

		// Widget: file_path — user selects a 3D model file from disk (browseable in details panel)
		Node.WidgetValues.Add(TEXT("file_path"), TEXT(""));
		FComfyInputDef FilePathDef;
		FilePathDef.Name = TEXT("file_path");
		FilePathDef.Type = TEXT("STRING");
		FilePathDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("file_path"), FilePathDef);
		Node.WidgetOrder.Add(TEXT("file_path"));

		// Widget: asset_path — content browser destination
		Node.WidgetValues.Add(TEXT("asset_path"), TEXT("/Game/ViewGen/Models"));
		FComfyInputDef PathDef;
		PathDef.Name = TEXT("asset_path");
		PathDef.Type = TEXT("STRING");
		PathDef.DefaultString = TEXT("/Game/ViewGen/Models");
		Node.WidgetInputDefs.Add(TEXT("asset_path"), PathDef);
		Node.WidgetOrder.Add(TEXT("asset_path"));

		// Widget: asset_name — optional override (blank = use original filename)
		Node.WidgetValues.Add(TEXT("asset_name"), TEXT(""));
		FComfyInputDef NameDef;
		NameDef.Name = TEXT("asset_name");
		NameDef.Type = TEXT("STRING");
		NameDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("asset_name"), NameDef);
		Node.WidgetOrder.Add(TEXT("asset_name"));

		// Widget: place_in_level — whether to auto-place the imported mesh into the current level
		Node.WidgetValues.Add(TEXT("place_in_level"), TEXT("true"));
		FComfyInputDef PlaceDef;
		PlaceDef.Name = TEXT("place_in_level");
		PlaceDef.Type = TEXT("BOOLEAN");
		PlaceDef.DefaultBool = true;
		Node.WidgetInputDefs.Add(TEXT("place_in_level"), PlaceDef);
		Node.WidgetOrder.Add(TEXT("place_in_level"));

		// Output pin: MESH (provides a reference for downstream nodes)
		FGraphPin OutMesh;
		OutMesh.Name = TEXT("MESH");
		OutMesh.Type = TEXT("*");
		OutMesh.bIsInput = false;
		OutMesh.PinIndex = 0;
		OutMesh.OwnerNodeId = Id;
		Node.OutputPins.Add(OutMesh);
	}
	else if (UEClassType == UEImageBridgeClassType)
	{
		Node.Title = TEXT("UE Image Bridge");
		Node.HeaderColor = FLinearColor(0.45f, 0.50f, 0.20f);

		// Input pin: IMAGE — receives the generated image from an upstream node
		FGraphPin InImage;
		InImage.Name = TEXT("images");
		InImage.Type = TEXT("IMAGE");
		InImage.bIsInput = true;
		InImage.PinIndex = 0;
		InImage.OwnerNodeId = Id;
		Node.InputPins.Add(InImage);

		// Widget: filename_prefix — prefix for the saved image filename
		Node.WidgetValues.Add(TEXT("filename_prefix"), TEXT("ViewGen_Bridge"));
		FComfyInputDef PrefixDef;
		PrefixDef.Name = TEXT("filename_prefix");
		PrefixDef.Type = TEXT("STRING");
		PrefixDef.DefaultString = TEXT("ViewGen_Bridge");
		Node.WidgetInputDefs.Add(TEXT("filename_prefix"), PrefixDef);
		Node.WidgetOrder.Add(TEXT("filename_prefix"));

		// Output pin: IMAGE — passes the saved image through as a loadable reference
		// At export, this becomes a LoadImage node that loads the saved file
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);
	}
	else if (UEClassType == UE3DAssetExportClassType)
	{
		Node.Title = TEXT("UE 3D Asset Export");
		Node.HeaderColor = FLinearColor(0.55f, 0.40f, 0.20f); // Warm amber

		// Input pin: mesh — receives the 3D model output from a ComfyUI 3D generation node
		FGraphPin InMesh;
		InMesh.Name = TEXT("mesh");
		InMesh.Type = TEXT("*");  // Wildcard — accept FILE_3D, MESH, STRING, or any output
		InMesh.bIsInput = true;
		InMesh.PinIndex = 0;
		InMesh.OwnerNodeId = Id;
		Node.InputPins.Add(InMesh);

		// Widget: asset_path — content browser destination path
		Node.WidgetValues.Add(TEXT("asset_path"), TEXT("/Game/ViewGen/Models"));
		FComfyInputDef PathDef;
		PathDef.Name = TEXT("asset_path");
		PathDef.Type = TEXT("STRING");
		PathDef.DefaultString = TEXT("/Game/ViewGen/Models");
		Node.WidgetInputDefs.Add(TEXT("asset_path"), PathDef);
		Node.WidgetOrder.Add(TEXT("asset_path"));

		// Widget: asset_name — name for the imported asset (auto-generated from filename if empty)
		Node.WidgetValues.Add(TEXT("asset_name"), TEXT(""));
		FComfyInputDef NameDef;
		NameDef.Name = TEXT("asset_name");
		NameDef.Type = TEXT("STRING");
		NameDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("asset_name"), NameDef);
		Node.WidgetOrder.Add(TEXT("asset_name"));

		// Widget: collision — collision complexity setting for the imported static mesh
		Node.WidgetValues.Add(TEXT("collision"), TEXT("Project Default"));
		FComfyInputDef CollisionDef;
		CollisionDef.Name = TEXT("collision");
		CollisionDef.Type = TEXT("COMBO");
		CollisionDef.ComboOptions.Add(TEXT("Project Default"));
		CollisionDef.ComboOptions.Add(TEXT("No Collision"));
		CollisionDef.ComboOptions.Add(TEXT("Use Complex As Simple"));
		CollisionDef.ComboOptions.Add(TEXT("Auto Convex"));
		CollisionDef.DefaultString = TEXT("Project Default");
		Node.WidgetInputDefs.Add(TEXT("collision"), CollisionDef);
		Node.WidgetOrder.Add(TEXT("collision"));

		// Widget: nanite — whether to enable Nanite for the imported mesh (UE5 feature)
		Node.WidgetValues.Add(TEXT("nanite"), TEXT("false"));
		FComfyInputDef NaniteDef;
		NaniteDef.Name = TEXT("nanite");
		NaniteDef.Type = TEXT("BOOLEAN");
		NaniteDef.DefaultBool = false;
		Node.WidgetInputDefs.Add(TEXT("nanite"), NaniteDef);
		Node.WidgetOrder.Add(TEXT("nanite"));

		// Widget: place_in_level — whether to auto-place the imported mesh into the current level
		Node.WidgetValues.Add(TEXT("place_in_level"), TEXT("true"));
		FComfyInputDef PlaceDef;
		PlaceDef.Name = TEXT("place_in_level");
		PlaceDef.Type = TEXT("BOOLEAN");
		PlaceDef.DefaultBool = true;
		Node.WidgetInputDefs.Add(TEXT("place_in_level"), PlaceDef);
		Node.WidgetOrder.Add(TEXT("place_in_level"));

		// No output pins — this is a terminal/action node
	}
	else if (UEClassType == UEPromptAdherenceClassType)
	{
		Node.Title = TEXT("UE Prompt Adherence");
		Node.HeaderColor = FLinearColor(0.50f, 0.30f, 0.55f);

		// Single widget: adherence slider (0.0 = maximum AI creativity, 1.0 = strict prompt following)
		Node.WidgetValues.Add(TEXT("adherence"), TEXT("0.5"));
		FComfyInputDef AdhereDef;
		AdhereDef.Name = TEXT("adherence");
		AdhereDef.Type = TEXT("FLOAT");
		AdhereDef.DefaultNumber = 0.5;
		AdhereDef.MinValue = 0.0;
		AdhereDef.MaxValue = 1.0;
		AdhereDef.Step = 0.01;
		Node.WidgetInputDefs.Add(TEXT("adherence"), AdhereDef);
		Node.WidgetOrder.Add(TEXT("adherence"));

		// No pins — this is a global settings node (like Camera Data)
	}
	else if (UEClassType == UEImageUpresClassType)
	{
		Node.Title = TEXT("UE Image Upres");
		Node.HeaderColor = FLinearColor(0.35f, 0.55f, 0.65f);

		// Input pin: IMAGE — receives the image to upscale/convert
		FGraphPin InImage;
		InImage.Name = TEXT("image");
		InImage.Type = TEXT("IMAGE");
		InImage.bIsInput = true;
		InImage.PinIndex = 0;
		InImage.OwnerNodeId = Id;
		Node.InputPins.Add(InImage);

		// Widget: upscale_method — interpolation/upscale algorithm
		Node.WidgetValues.Add(TEXT("upscale_method"), TEXT("lanczos"));
		FComfyInputDef MethodDef;
		MethodDef.Name = TEXT("upscale_method");
		MethodDef.Type = TEXT("COMBO");
		MethodDef.ComboOptions = { TEXT("nearest-exact"), TEXT("bilinear"), TEXT("bicubic"), TEXT("lanczos"), TEXT("area") };
		Node.WidgetInputDefs.Add(TEXT("upscale_method"), MethodDef);
		Node.WidgetOrder.Add(TEXT("upscale_method"));

		// Widget: scale_factor — multiplier for output resolution
		Node.WidgetValues.Add(TEXT("scale_factor"), TEXT("2.0"));
		FComfyInputDef ScaleDef;
		ScaleDef.Name = TEXT("scale_factor");
		ScaleDef.Type = TEXT("FLOAT");
		ScaleDef.DefaultNumber = 2.0;
		ScaleDef.MinValue = 1.0;
		ScaleDef.MaxValue = 8.0;
		ScaleDef.Step = 0.5;
		Node.WidgetInputDefs.Add(TEXT("scale_factor"), ScaleDef);
		Node.WidgetOrder.Add(TEXT("scale_factor"));

		// Widget: output_bit_depth — convert output to 8-bit or 16-bit
		Node.WidgetValues.Add(TEXT("output_bit_depth"), TEXT("16"));
		FComfyInputDef BitDepthDef;
		BitDepthDef.Name = TEXT("output_bit_depth");
		BitDepthDef.Type = TEXT("COMBO");
		BitDepthDef.ComboOptions = { TEXT("8"), TEXT("16") };
		Node.WidgetInputDefs.Add(TEXT("output_bit_depth"), BitDepthDef);
		Node.WidgetOrder.Add(TEXT("output_bit_depth"));

		// Widget: output_format — file format for the exported image
		Node.WidgetValues.Add(TEXT("output_format"), TEXT("EXR"));
		FComfyInputDef FormatDef;
		FormatDef.Name = TEXT("output_format");
		FormatDef.Type = TEXT("COMBO");
		FormatDef.ComboOptions = { TEXT("EXR"), TEXT("PNG") };
		Node.WidgetInputDefs.Add(TEXT("output_format"), FormatDef);
		Node.WidgetOrder.Add(TEXT("output_format"));

		// Widget: save_path — directory where the exported file is saved
		Node.WidgetValues.Add(TEXT("save_path"), TEXT(""));
		FComfyInputDef SavePathDef;
		SavePathDef.Name = TEXT("save_path");
		SavePathDef.Type = TEXT("STRING");
		SavePathDef.DefaultString = TEXT("");
		Node.WidgetInputDefs.Add(TEXT("save_path"), SavePathDef);
		Node.WidgetOrder.Add(TEXT("save_path"));

		// Widget: filename_prefix — prefix for the saved file (timestamp appended automatically)
		Node.WidgetValues.Add(TEXT("filename_prefix"), TEXT("ViewGen_Upres"));
		FComfyInputDef PrefixDef;
		PrefixDef.Name = TEXT("filename_prefix");
		PrefixDef.Type = TEXT("STRING");
		PrefixDef.DefaultString = TEXT("ViewGen_Upres");
		Node.WidgetInputDefs.Add(TEXT("filename_prefix"), PrefixDef);
		Node.WidgetOrder.Add(TEXT("filename_prefix"));

		// Output pin: IMAGE — the upscaled image (passthrough for downstream ComfyUI nodes)
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);
	}

	else if (UEClassType == UESequenceClassType)
	{
		Node.Title = TEXT("UE Sequence");
		Node.HeaderColor = FLinearColor(0.65f, 0.50f, 0.15f); // Gold — execution flow

		// Widget: steps count
		Node.WidgetValues.Add(TEXT("steps"), TEXT("3"));
		FComfyInputDef StepsDef;
		StepsDef.Name = TEXT("steps");
		StepsDef.Type = TEXT("INT");
		StepsDef.DefaultNumber = 3.0;
		StepsDef.MinValue = 2.0;
		StepsDef.MaxValue = 6.0;
		StepsDef.Step = 1.0;
		Node.WidgetInputDefs.Add(TEXT("steps"), StepsDef);
		Node.WidgetOrder.Add(TEXT("steps"));

		// Create output pins: Then 0, Then 1, Then 2 (default 3 steps)
		int32 NumSteps = 3;
		for (int32 i = 0; i < NumSteps; ++i)
		{
			FGraphPin OutPin;
			OutPin.Name = FString::Printf(TEXT("Then %d"), i);
			OutPin.Type = TEXT("*"); // Wildcard — connects to any input type
			OutPin.bIsInput = false;
			OutPin.PinIndex = i;
			OutPin.OwnerNodeId = Id;
			Node.OutputPins.Add(OutPin);
		}
	}
	else if (UEClassType == UEVideoToImageClassType)
	{
		Node.Title = TEXT("UE Video to Image");
		Node.HeaderColor = FLinearColor(0.50f, 0.28f, 0.58f); // Purple — video processing

		// Widget: frame number to extract
		Node.WidgetValues.Add(TEXT("frame"), TEXT("0"));
		FComfyInputDef FrameDef;
		FrameDef.Name = TEXT("frame");
		FrameDef.Type = TEXT("INT");
		FrameDef.DefaultNumber = 0.0;
		FrameDef.MinValue = 0.0;
		FrameDef.MaxValue = 99999.0;
		FrameDef.Step = 1.0;
		Node.WidgetInputDefs.Add(TEXT("frame"), FrameDef);
		Node.WidgetOrder.Add(TEXT("frame"));

		// Output pin: IMAGE — the extracted video frame
		FGraphPin OutImage;
		OutImage.Name = TEXT("IMAGE");
		OutImage.Type = TEXT("IMAGE");
		OutImage.bIsInput = false;
		OutImage.PinIndex = 0;
		OutImage.OwnerNodeId = Id;
		Node.OutputPins.Add(OutImage);
	}

	ComputeNodeSize(Node);
	return Node;
}

bool SWorkflowGraphEditor::HasSequenceNode() const
{
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.ClassType == UESequenceClassType) return true;
	}
	return false;
}

TArray<TSharedPtr<FJsonObject>> SWorkflowGraphEditor::ExportStagedWorkflows(
	bool* OutNeedsViewport, bool* OutNeedsDepth,
	FString* OutCameraDescription, bool* OutNeedsSegmentation) const
{
	TArray<TSharedPtr<FJsonObject>> StagedWorkflows;

	// Find the Sequence node
	const FGraphNode* SeqNode = nullptr;
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.ClassType == UESequenceClassType)
		{
			SeqNode = &Node;
			break;
		}
	}

	if (!SeqNode)
	{
		// No sequence node — just export as a single workflow
		TSharedPtr<FJsonObject> SingleWorkflow = ExportWorkflowJSON(
			OutNeedsViewport, OutNeedsDepth, OutCameraDescription, OutNeedsSegmentation);
		if (SingleWorkflow.IsValid())
		{
			StagedWorkflows.Add(SingleWorkflow);
		}
		return StagedWorkflows;
	}

	// Build an adjacency map: for each node, which nodes does it feed into?
	// (downstream = nodes that consume this node's outputs)
	TMap<FString, TSet<FString>> DownstreamMap; // NodeId -> set of downstream NodeIds
	for (const FGraphConnection& Conn : Connections)
	{
		DownstreamMap.FindOrAdd(Conn.SourceNodeId).Add(Conn.TargetNodeId);
	}

	// For each Sequence output pin, find all nodes reachable downstream (BFS)
	int32 NumSteps = SeqNode->OutputPins.Num();

	// StageNodeIds[i] = set of node IDs belonging to stage i
	TArray<TSet<FString>> StageNodeIds;
	StageNodeIds.SetNum(NumSteps);

	for (int32 StepIdx = 0; StepIdx < NumSteps; ++StepIdx)
	{
		const FString& PinName = SeqNode->OutputPins[StepIdx].Name;

		// Find connections from this Sequence output pin
		TArray<FString> Seeds;
		for (const FGraphConnection& Conn : Connections)
		{
			if (Conn.SourceNodeId == SeqNode->Id && Conn.SourceOutputIndex == StepIdx)
			{
				Seeds.Add(Conn.TargetNodeId);
			}
		}

		// BFS to collect all downstream nodes from seeds
		TSet<FString> Visited;
		TArray<FString> Queue = Seeds;
		while (Queue.Num() > 0)
		{
			FString Current = Queue.Pop(false);
			if (Visited.Contains(Current)) continue;
			Visited.Add(Current);

			// Don't cross into other Sequence pins or the Sequence node itself
			if (Current == SeqNode->Id) continue;

			StageNodeIds[StepIdx].Add(Current);

			// Also add all nodes that this node depends on (upstream) to this stage,
			// so the workflow is self-contained
			for (const FGraphConnection& Conn : Connections)
			{
				if (Conn.TargetNodeId == Current && !Visited.Contains(Conn.SourceNodeId))
				{
					// Only add upstream if it's not the Sequence node and not already in another earlier stage
					if (Conn.SourceNodeId != SeqNode->Id)
					{
						Queue.Add(Conn.SourceNodeId);
					}
				}
				// Also follow downstream connections
				if (Conn.SourceNodeId == Current && !Visited.Contains(Conn.TargetNodeId))
				{
					if (Conn.TargetNodeId != SeqNode->Id)
					{
						Queue.Add(Conn.TargetNodeId);
					}
				}
			}
		}
	}

	// Now export each stage: filter ExportWorkflowJSON to only include nodes in that stage.
	// For simplicity, we'll call the full ExportWorkflowJSON and then strip nodes not in the stage.
	// First, get the full workflow with all UE source node resolution.
	TSharedPtr<FJsonObject> FullWorkflow = ExportWorkflowJSON(
		OutNeedsViewport, OutNeedsDepth, OutCameraDescription, OutNeedsSegmentation);

	if (!FullWorkflow.IsValid())
	{
		return StagedWorkflows;
	}

	for (int32 StepIdx = 0; StepIdx < NumSteps; ++StepIdx)
	{
		if (StageNodeIds[StepIdx].Num() == 0) continue;

		TSharedPtr<FJsonObject> StageWorkflow = MakeShareable(new FJsonObject);

		for (const auto& Pair : FullWorkflow->Values)
		{
			// The key is the node ID in the workflow
			if (StageNodeIds[StepIdx].Contains(Pair.Key))
			{
				StageWorkflow->SetField(Pair.Key, Pair.Value);
			}
		}

		if (StageWorkflow->Values.Num() > 0)
		{
			StagedWorkflows.Add(StageWorkflow);
		}
	}

	return StagedWorkflows;
}

bool SWorkflowGraphEditor::HasUESourceNodes() const
{
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.IsUESourceNode()) return true;
	}
	return false;
}

void SWorkflowGraphEditor::ComputeNodeSize(FGraphNode& Node) const
{
	float Height = GraphConstants::NodeHeaderHeight + GraphConstants::NodePadding;
	Height += Node.InputPins.Num() * GraphConstants::PinRowHeight;
	Height += Node.OutputPins.Num() * GraphConstants::PinRowHeight;
	Height += Node.WidgetValues.Num() * GraphConstants::WidgetRowHeight;

	Height += GraphConstants::NodePadding;
	Height = FMath::Max(Height, GraphConstants::NodeHeaderHeight + 30.0f);

	// Width based on title and pin/widget names
	float Width = GraphConstants::NodeMinWidth;
	// Rough estimate: 7px per character at default font
	Width = FMath::Max(Width, Node.Title.Len() * 7.0f + 40.0f);

	// Also account for widget label + value width
	for (const auto& WidgetPair : Node.WidgetValues)
	{
		float NeededW = (WidgetPair.Key.Len() + FMath::Min(WidgetPair.Value.Len(), 20) + 4) * 6.5f + 30.0f;
		Width = FMath::Max(Width, NeededW);
	}

	// Add thumbnail area for LoadImage / LoadVideo nodes — compute actual height from aspect ratio
	if (Node.ThumbnailBrush.IsValid() && (Node.ClassType == TEXT("LoadImage") || Node.ClassType.Contains(TEXT("LoadVideo")) || Node.ClassType == UEVideoToImageClassType))
	{
		float Pad = GraphConstants::ThumbnailPadding;
		float AvailW = Width - Pad * 2.0f;
		FVector2D ImgSize = Node.ThumbnailBrush->ImageSize;
		float AspectRatio = (ImgSize.Y > 0.0f) ? (ImgSize.X / ImgSize.Y) : 1.0f;
		float ThumbH = AvailW / AspectRatio;
		ThumbH = FMath::Min(ThumbH, GraphConstants::ThumbnailHeight); // cap at max
		Height += ThumbH + Pad * 2.0f;
	}

	// Add 3D mesh preview area
	if (Node.MeshPreview.IsValid() && Node.MeshPreview->HasPreview())
	{
		float Pad = GraphConstants::ThumbnailPadding;
		float PreviewSize = FMath::Min(Width - Pad * 2.0f, 160.0f); // Square, capped at 160px
		Height += PreviewSize + Pad * 2.0f;
	}

	Node.Size = FVector2D(Width, Height);
}

// ============================================================================
// Hit Testing
// ============================================================================

FString SWorkflowGraphEditor::HitTestNode(FVector2D LocalPos) const
{
	// Test in reverse order so topmost (last drawn) nodes are hit first
	for (int32 i = Nodes.Num() - 1; i >= 0; --i)
	{
		const FGraphNode& Node = Nodes[i];
		FVector2D NodeLocalPos = GraphToLocal(Node.Position);
		FVector2D NodeLocalSize = Node.Size * ZoomLevel;

		if (LocalPos.X >= NodeLocalPos.X && LocalPos.X <= NodeLocalPos.X + NodeLocalSize.X &&
			LocalPos.Y >= NodeLocalPos.Y && LocalPos.Y <= NodeLocalPos.Y + NodeLocalSize.Y)
		{
			return Node.Id;
		}
	}
	return FString();
}

bool SWorkflowGraphEditor::HitTestPin(FVector2D LocalPos, FGraphPin& OutPin) const
{
	const float HitRadiusSq = GraphConstants::PinHitRadius * GraphConstants::PinHitRadius * ZoomLevel * ZoomLevel;

	for (const FGraphNode& Node : Nodes)
	{
		// Test input pins
		for (const FGraphPin& Pin : Node.InputPins)
		{
			FVector2D PinPos = GetPinPosition(Node, Pin);
			if ((PinPos - LocalPos).SizeSquared() <= HitRadiusSq)
			{
				OutPin = Pin;
				return true;
			}
		}

		// Test output pins
		for (const FGraphPin& Pin : Node.OutputPins)
		{
			FVector2D PinPos = GetPinPosition(Node, Pin);
			if ((PinPos - LocalPos).SizeSquared() <= HitRadiusSq)
			{
				OutPin = Pin;
				return true;
			}
		}
	}

	return false;
}

FVector2D SWorkflowGraphEditor::GetPinPosition(const FGraphNode& Node, const FGraphPin& Pin) const
{
	// Reroute nodes: pins are at the center-left (input) and center-right (output)
	if (Node.IsReroute())
	{
		FVector2D Center = GraphToLocal(Node.Position + Node.Size * 0.5f);
		float HalfW = Node.Size.X * 0.5f * ZoomLevel;
		if (Pin.bIsInput)
		{
			return FVector2D(Center.X - HalfW, Center.Y);
		}
		else
		{
			return FVector2D(Center.X + HalfW, Center.Y);
		}
	}

	FVector2D NodePos = GraphToLocal(Node.Position);
	FVector2D NodeSize = Node.Size * ZoomLevel;
	float HeaderH = GraphConstants::NodeHeaderHeight * ZoomLevel;
	float RowH = GraphConstants::PinRowHeight * ZoomLevel;
	float PadY = GraphConstants::NodePadding * ZoomLevel;

	if (Pin.bIsInput)
	{
		float Y = NodePos.Y + HeaderH + PadY + Pin.PinIndex * RowH + RowH * 0.5f;
		return FVector2D(NodePos.X, Y);
	}
	else
	{
		// Outputs are drawn below inputs + widgets + thumbnail
		float WidgetOffset = Node.WidgetValues.Num() * GraphConstants::WidgetRowHeight * ZoomLevel;
		float InputOffset = Node.InputPins.Num() * RowH;
		float ThumbOffset = 0.0f;
		if (Node.ThumbnailBrush.IsValid() && (Node.ClassType == TEXT("LoadImage") || Node.ClassType.Contains(TEXT("LoadVideo")) || Node.ClassType == UEVideoToImageClassType))
		{
			float Pad = GraphConstants::ThumbnailPadding;
			float AvailW = Node.Size.X - Pad * 2.0f;
			FVector2D ImgSize = Node.ThumbnailBrush->ImageSize;
			float AspectRatio = (ImgSize.Y > 0.0f) ? (ImgSize.X / ImgSize.Y) : 1.0f;
			float ThumbH = FMath::Min(AvailW / AspectRatio, GraphConstants::ThumbnailHeight);
			ThumbOffset = (ThumbH + Pad * 2.0f) * ZoomLevel;
		}
		float Y = NodePos.Y + HeaderH + PadY + InputOffset + WidgetOffset + ThumbOffset + Pin.PinIndex * RowH + RowH * 0.5f;
		return FVector2D(NodePos.X + NodeSize.X, Y);
	}
}

// ============================================================================
// OnPaint
// ============================================================================

int32 SWorkflowGraphEditor::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Background
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		FCoreStyle::Get().GetBrush("GenericWhiteBox"),
		ESlateDrawEffect::None,
		FLinearColor(0.05f, 0.05f, 0.06f)
	);
	LayerId++;

	// Draw grid
	{
		float GridSpacing = GraphConstants::GridSize * ZoomLevel;
		if (GridSpacing > 4.0f)
		{
			FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
			FVector2D Offset = FVector2D(
				FMath::Fmod(ViewOffset.X * ZoomLevel, GridSpacing),
				FMath::Fmod(ViewOffset.Y * ZoomLevel, GridSpacing)
			);

			FLinearColor GridColor(0.08f, 0.08f, 0.10f);
			const float GridLineThickness = 1.0f;

			for (float X = Offset.X; X < WidgetSize.X; X += GridSpacing)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(GridLineThickness, WidgetSize.Y), FSlateLayoutTransform(FVector2D(X, 0))),
					FCoreStyle::Get().GetBrush("GenericWhiteBox"),
					ESlateDrawEffect::None, GridColor
				);
			}
			for (float Y = Offset.Y; Y < WidgetSize.Y; Y += GridSpacing)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements, LayerId,
					AllottedGeometry.ToPaintGeometry(FVector2D(WidgetSize.X, GridLineThickness), FSlateLayoutTransform(FVector2D(0, Y))),
					FCoreStyle::Get().GetBrush("GenericWhiteBox"),
					ESlateDrawEffect::None, GridColor
				);
			}
		}
	}
	LayerId++;

	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));

	// Draw connections
	for (int32 ConnIdx = 0; ConnIdx < Connections.Num(); ++ConnIdx)
	{
		const FGraphConnection& Conn = Connections[ConnIdx];
		const int32* SrcIdx = NodeIndexMap.Find(Conn.SourceNodeId);
		const int32* DstIdx = NodeIndexMap.Find(Conn.TargetNodeId);
		if (!SrcIdx || !DstIdx) continue;

		const FGraphNode& SrcNode = Nodes[*SrcIdx];
		const FGraphNode& DstNode = Nodes[*DstIdx];

		// Find output pin position
		FVector2D StartPos = FVector2D::ZeroVector;
		if (Conn.SourceOutputIndex < SrcNode.OutputPins.Num())
		{
			StartPos = GetPinPosition(SrcNode, SrcNode.OutputPins[Conn.SourceOutputIndex]);
		}

		// Find input pin position
		FVector2D EndPos = FVector2D::ZeroVector;
		for (const auto& Pin : DstNode.InputPins)
		{
			if (Pin.Name == Conn.TargetInputName)
			{
				EndPos = GetPinPosition(DstNode, Pin);
				break;
			}
		}

		// Get colour from output type
		FLinearColor WireColor(0.6f, 0.6f, 0.6f, 0.7f);
		if (Conn.SourceOutputIndex < SrcNode.OutputPins.Num())
		{
			const FString& OutType = SrcNode.OutputPins[Conn.SourceOutputIndex].Type;
			if (OutType == TEXT("MODEL")) WireColor = FLinearColor(0.4f, 0.6f, 0.8f, 0.8f);
			else if (OutType == TEXT("CLIP")) WireColor = FLinearColor(0.8f, 0.8f, 0.3f, 0.8f);
			else if (OutType == TEXT("CONDITIONING")) WireColor = FLinearColor(0.8f, 0.5f, 0.2f, 0.8f);
			else if (OutType == TEXT("LATENT")) WireColor = FLinearColor(0.8f, 0.2f, 0.8f, 0.8f);
			else if (OutType == TEXT("IMAGE")) WireColor = FLinearColor(0.4f, 0.8f, 0.4f, 0.8f);
			else if (OutType == TEXT("VAE")) WireColor = FLinearColor(0.8f, 0.3f, 0.3f, 0.8f);
		}

		// Brighten hovered connection
		if (ConnIdx == HoveredConnectionIndex)
		{
			WireColor = FLinearColor(
				FMath::Min(WireColor.R + 0.35f, 1.0f),
				FMath::Min(WireColor.G + 0.35f, 1.0f),
				FMath::Min(WireColor.B + 0.35f, 1.0f),
				1.0f);
		}

		DrawConnection(StartPos, EndPos, WireColor, AllottedGeometry, OutDrawElements, LayerId);
	}

	// Draw in-progress connection drag
	if (InteractionMode == EInteractionMode::DraggingConnection)
	{
		const int32* SrcIdx = NodeIndexMap.Find(DragSourceNodeId);
		if (SrcIdx)
		{
			const FGraphNode& SrcNode = Nodes[*SrcIdx];
			FVector2D StartPos = DragConnectionEnd;
			bool bValidPin = false;
			if (bDraggingFromOutput && DragSourcePinIndex < SrcNode.OutputPins.Num())
			{
				StartPos = GetPinPosition(SrcNode, SrcNode.OutputPins[DragSourcePinIndex]);
				bValidPin = true;
			}
			else if (!bDraggingFromOutput && DragSourcePinIndex < SrcNode.InputPins.Num())
			{
				StartPos = GetPinPosition(SrcNode, SrcNode.InputPins[DragSourcePinIndex]);
				bValidPin = true;
			}

			if (bValidPin)
			{
				FLinearColor DragColor(0.8f, 0.8f, 0.2f, 0.8f);
				if (bDraggingFromOutput)
				{
					DrawConnection(StartPos, DragConnectionEnd, DragColor, AllottedGeometry, OutDrawElements, LayerId);
				}
				else
				{
					DrawConnection(DragConnectionEnd, StartPos, DragColor, AllottedGeometry, OutDrawElements, LayerId);
				}
			}
		}
	}
	LayerId++;

	// Draw nodes
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.IsReroute())
		{
			DrawRerouteNode(Node, AllottedGeometry, OutDrawElements, LayerId);
		}
		else
		{
			DrawNode(Node, AllottedGeometry, OutDrawElements, LayerId);
		}
	}
	LayerId += 3;

	// Box selection overlay
	if (InteractionMode == EInteractionMode::BoxSelecting)
	{
		FVector2D MinPt(FMath::Min(BoxSelectStart.X, BoxSelectEnd.X), FMath::Min(BoxSelectStart.Y, BoxSelectEnd.Y));
		FVector2D MaxPt(FMath::Max(BoxSelectStart.X, BoxSelectEnd.X), FMath::Max(BoxSelectStart.Y, BoxSelectEnd.Y));
		FVector2D BoxSize = MaxPt - MinPt;

		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(MinPt)),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"),
			ESlateDrawEffect::None,
			FLinearColor(0.3f, 0.5f, 0.8f, 0.15f)
		);
	}

	// ---- Cost estimate overlay (upper-left corner, above all nodes) ----
	if (!OverlayText.IsEmpty())
	{
		const FSlateFontInfo OverlayFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
		FVector2D TextPos(10.0f, 8.0f);
		FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
			->Measure(OverlayText, OverlayFont);

		// Semi-transparent background pill
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(TextSize.X + 16.0f, TextSize.Y + 8.0f),
				FSlateLayoutTransform(FVector2D(TextPos.X - 4.0f, TextPos.Y - 2.0f))),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"),
			ESlateDrawEffect::None,
			FLinearColor(0.0f, 0.0f, 0.0f, 0.65f)
		);

		// Text
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(TextSize.X + 8.0f, TextSize.Y + 4.0f),
				FSlateLayoutTransform(FVector2D(TextPos.X + 4.0f, TextPos.Y + 2.0f))),
			OverlayText,
			OverlayFont,
			ESlateDrawEffect::None,
			FLinearColor(0.9f, 0.85f, 0.4f) // Gold color for cost
		);
		LayerId += 2;
	}

	OutDrawElements.PopClip();
	return LayerId;
}

// ============================================================================
// DrawNode
// ============================================================================

void SWorkflowGraphEditor::DrawNode(const FGraphNode& Node, const FGeometry& Geom,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FVector2D Pos = GraphToLocal(Node.Position);
	const FVector2D Size = Node.Size * ZoomLevel;
	const float HeaderH = GraphConstants::NodeHeaderHeight * ZoomLevel;
	const float PadY = GraphConstants::NodePadding * ZoomLevel;
	const float RowH = GraphConstants::PinRowHeight * ZoomLevel;
	const float WidgetH = GraphConstants::WidgetRowHeight * ZoomLevel;
	const float PinR = GraphConstants::PinRadius * ZoomLevel;

	const FSlateBrush* WhiteBox = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	// Execution highlight — bright yellow/amber glow when node is being processed
	if (Node.bIsExecuting)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(Size + FVector2D(8, 8), FSlateLayoutTransform(Pos - FVector2D(4, 4))),
			WhiteBox, ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.75f, 0.0f, 0.3f)
		);
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(Size + FVector2D(4, 4), FSlateLayoutTransform(Pos - FVector2D(2, 2))),
			WhiteBox, ESlateDrawEffect::None,
			FLinearColor(1.0f, 0.8f, 0.0f, 0.6f)
		);
	}
	// Selection highlight
	else if (Node.bSelected || SelectedNodeIds.Contains(Node.Id))
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(Size + FVector2D(4, 4), FSlateLayoutTransform(Pos - FVector2D(2, 2))),
			WhiteBox, ESlateDrawEffect::None,
			FLinearColor(0.3f, 0.6f, 1.0f, 0.5f)
		);
	}

	// Shadow
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		Geom.ToPaintGeometry(Size + FVector2D(3, 3), FSlateLayoutTransform(Pos + FVector2D(2, 2))),
		WhiteBox, ESlateDrawEffect::None,
		FLinearColor(0.0f, 0.0f, 0.0f, 0.3f)
	);

	// Node body
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)),
		WhiteBox, ESlateDrawEffect::None,
		FLinearColor(0.13f, 0.13f, 0.15f)
	);

	// Border
	{
		FLinearColor BorderColor = (Node.bSelected || SelectedNodeIds.Contains(Node.Id))
			? FLinearColor(0.3f, 0.6f, 1.0f, 0.8f)
			: FLinearColor(0.22f, 0.22f, 0.24f, 0.6f);
		float BorderW = 1.0f;
		// Top
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(Size.X, BorderW), FSlateLayoutTransform(Pos)),
			WhiteBox, ESlateDrawEffect::None, BorderColor);
		// Bottom
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(Size.X, BorderW), FSlateLayoutTransform(FVector2D(Pos.X, Pos.Y + Size.Y - BorderW))),
			WhiteBox, ESlateDrawEffect::None, BorderColor);
		// Left
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(BorderW, Size.Y), FSlateLayoutTransform(Pos)),
			WhiteBox, ESlateDrawEffect::None, BorderColor);
		// Right
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			Geom.ToPaintGeometry(FVector2D(BorderW, Size.Y), FSlateLayoutTransform(FVector2D(Pos.X + Size.X - BorderW, Pos.Y))),
			WhiteBox, ESlateDrawEffect::None, BorderColor);
	}

	// Header bar
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(Size.X, HeaderH), FSlateLayoutTransform(Pos)),
		WhiteBox, ESlateDrawEffect::None,
		Node.HeaderColor
	);

	// Title text — leave room for play button on runnable nodes
	const float PlayBtnSize = Node.IsRunnable() ? (HeaderH * 0.7f) : 0.0f;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", static_cast<int32>(FMath::Max(7.0f, 10.0f * ZoomLevel)));
	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId + 2,
		Geom.ToPaintGeometry(FVector2D(Size.X - 12.0f - PlayBtnSize, HeaderH), FSlateLayoutTransform(Pos + FVector2D(6.0f, 3.0f * ZoomLevel))),
		Node.Title,
		TitleFont,
		ESlateDrawEffect::None,
		FLinearColor::White
	);

	// Class type subtitle (smaller, dimmer) — UE source nodes show "Unreal Engine" badge
	{
		FString SubtitleText;
		FLinearColor SubtitleColor = FLinearColor(0.8f, 0.8f, 0.8f, 0.5f);

		if (Node.IsUESourceNode())
		{
			SubtitleText = TEXT("Unreal Engine");
			SubtitleColor = FLinearColor(0.4f, 0.7f, 1.0f, 0.7f);
		}
		else if (Node.Title != Node.ClassType)
		{
			SubtitleText = Node.ClassType;
		}

		if (!SubtitleText.IsEmpty())
		{
			const FSlateFontInfo SubFont = FCoreStyle::GetDefaultFontStyle("Regular", static_cast<int32>(FMath::Max(5.0f, 6.5f * ZoomLevel)));
			FSlateDrawElement::MakeText(
				OutDrawElements, LayerId + 2,
				Geom.ToPaintGeometry(FVector2D(Size.X - 12.0f, HeaderH), FSlateLayoutTransform(Pos + FVector2D(6.0f, 14.0f * ZoomLevel))),
				SubtitleText,
				SubFont,
				ESlateDrawEffect::None,
				SubtitleColor
			);
		}
	}

	// Play button on runnable nodes (right side of header)
	if (Node.IsRunnable() && ZoomLevel > 0.3f)
	{
		const float BtnSize = HeaderH * 0.6f;
		const float BtnX = Pos.X + Size.X - BtnSize - 4.0f * ZoomLevel;
		const float BtnY = Pos.Y + (HeaderH - BtnSize) * 0.5f;

		// Determine button state
		bool bHovered = (HoveredPlayButtonNodeId == Node.Id);
		bool bPressed = (PressedPlayButtonNodeId == Node.Id);

		// Button background — changes with state
		FLinearColor BgColor;
		if (bPressed)
			BgColor = FLinearColor(0.1f, 0.4f, 0.1f, 0.7f);   // Dark green pressed
		else if (bHovered)
			BgColor = FLinearColor(0.15f, 0.15f, 0.18f, 0.8f); // Lighter on hover
		else
			BgColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.3f);    // Subtle default

		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(BtnSize, BtnSize), FSlateLayoutTransform(FVector2D(BtnX, BtnY))),
			WhiteBox, ESlateDrawEffect::None,
			BgColor
		);

		// Hover border
		if (bHovered || bPressed)
		{
			FLinearColor BorderCol = bPressed
				? FLinearColor(0.2f, 0.9f, 0.2f, 0.9f)
				: FLinearColor(0.3f, 0.8f, 0.3f, 0.6f);
			float Bw = 1.0f;
			// Top
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
				Geom.ToPaintGeometry(FVector2D(BtnSize, Bw), FSlateLayoutTransform(FVector2D(BtnX, BtnY))),
				WhiteBox, ESlateDrawEffect::None, BorderCol);
			// Bottom
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
				Geom.ToPaintGeometry(FVector2D(BtnSize, Bw), FSlateLayoutTransform(FVector2D(BtnX, BtnY + BtnSize - Bw))),
				WhiteBox, ESlateDrawEffect::None, BorderCol);
			// Left
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
				Geom.ToPaintGeometry(FVector2D(Bw, BtnSize), FSlateLayoutTransform(FVector2D(BtnX, BtnY))),
				WhiteBox, ESlateDrawEffect::None, BorderCol);
			// Right
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
				Geom.ToPaintGeometry(FVector2D(Bw, BtnSize), FSlateLayoutTransform(FVector2D(BtnX + BtnSize - Bw, BtnY))),
				WhiteBox, ESlateDrawEffect::None, BorderCol);
		}

		// Play triangle — brighter on hover/press
		FLinearColor PlayColor;
		if (bPressed)
			PlayColor = FLinearColor(0.4f, 1.0f, 0.4f, 1.0f);  // Bright green
		else if (bHovered)
			PlayColor = FLinearColor(0.3f, 0.9f, 0.3f, 1.0f);  // Bright on hover
		else
			PlayColor = FLinearColor(0.2f, 0.7f, 0.2f, 0.8f);  // Muted default

		TArray<FVector2D> TriVerts;
		TriVerts.Add(FVector2D(BtnX + BtnSize * 0.25f, BtnY + BtnSize * 0.15f));
		TriVerts.Add(FVector2D(BtnX + BtnSize * 0.25f, BtnY + BtnSize * 0.85f));
		TriVerts.Add(FVector2D(BtnX + BtnSize * 0.8f, BtnY + BtnSize * 0.5f));
		// Close the triangle
		TriVerts.Add(FVector2D(BtnX + BtnSize * 0.25f, BtnY + BtnSize * 0.15f));

		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId + 4,
			Geom.ToPaintGeometry(),
			TriVerts,
			ESlateDrawEffect::None,
			PlayColor,
			true, // bAntialias
			FMath::Max(1.5f, 2.0f * ZoomLevel)
		);
	}

	float YOffset = HeaderH + PadY;
	const FSlateFontInfo PinFont = FCoreStyle::GetDefaultFontStyle("Regular", static_cast<int32>(FMath::Max(6.0f, 8.0f * ZoomLevel)));

	// Input pins
	for (const FGraphPin& Pin : Node.InputPins)
	{
		FVector2D PinCenter(Pos.X, Pos.Y + YOffset + RowH * 0.5f);
		bool bConnected = IsInputConnected(Node.Id, Pin.Name);
		DrawPin(PinCenter, PinR, bConnected, true, Pin.Type, Geom, OutDrawElements, LayerId + 2);

		// Pin label
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(Size.X * 0.45f, RowH), FSlateLayoutTransform(FVector2D(Pos.X + PinR * 2.5f, Pos.Y + YOffset + 1.0f * ZoomLevel))),
			Pin.Name,
			PinFont,
			ESlateDrawEffect::None,
			FLinearColor(0.75f, 0.75f, 0.80f)
		);

		YOffset += RowH;
	}

	// Widget values (compact read-only display — editing happens in the Details panel)
	const FSlateFontInfo WidgetFont = FCoreStyle::GetDefaultFontStyle("Regular", static_cast<int32>(FMath::Max(5.0f, 6.5f * ZoomLevel)));

	// Use WidgetOrder if available, otherwise fall back to map iteration
	TArray<FString> WidgetNames;
	if (Node.WidgetOrder.Num() > 0)
	{
		WidgetNames = Node.WidgetOrder;
	}
	else
	{
		Node.WidgetValues.GetKeys(WidgetNames);
	}

	for (const FString& WidgetName : WidgetNames)
	{
		const FString* ValuePtr = Node.WidgetValues.Find(WidgetName);
		if (!ValuePtr) continue;

		FString Value = *ValuePtr;
		FString DisplayValue = Value;
		if (DisplayValue.Len() > 25)
		{
			DisplayValue = DisplayValue.Left(22) + TEXT("...");
		}

		const FComfyInputDef* WidgetDef = Node.WidgetInputDefs.Find(WidgetName);
		bool bIsBool = WidgetDef && (WidgetDef->Type == TEXT("BOOLEAN") || WidgetDef->Type == TEXT("BOOL"));

		float FieldX = Pos.X + 6.0f;
		float FieldW = Size.X - 12.0f;
		float FieldY = Pos.Y + YOffset;

		// Compact: label on the left, value on the right, no field styling
		float LabelW = FieldW * 0.38f;
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(LabelW, WidgetH),
				FSlateLayoutTransform(FVector2D(FieldX + 2.0f, FieldY))),
			WidgetName,
			WidgetFont,
			ESlateDrawEffect::None,
			FLinearColor(0.45f, 0.45f, 0.48f)
		);

		float ValueX = FieldX + LabelW + 2.0f;
		float ValueW = FieldW - LabelW - 4.0f;

		FLinearColor ValueColor = bIsBool
			? (Value == TEXT("true") ? FLinearColor(0.3f, 0.7f, 0.3f, 0.7f) : FLinearColor(0.6f, 0.3f, 0.3f, 0.7f))
			: FLinearColor(0.6f, 0.6f, 0.65f);

		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(ValueW - 4.0f, WidgetH),
				FSlateLayoutTransform(FVector2D(ValueX + 2.0f, FieldY))),
			bIsBool ? (Value == TEXT("true") ? TEXT("True") : TEXT("False")) : DisplayValue,
			WidgetFont,
			ESlateDrawEffect::None,
			ValueColor
		);

		YOffset += WidgetH;
	}

	// Thumbnail for LoadImage / LoadVideo nodes
	if (Node.ThumbnailBrush.IsValid() && Node.ThumbnailTexture
		&& ::IsValid(Node.ThumbnailTexture) && (Node.ClassType == TEXT("LoadImage") || Node.ClassType.Contains(TEXT("LoadVideo")) || Node.ClassType == UEVideoToImageClassType))
	{
		float ThumbMaxH = GraphConstants::ThumbnailHeight * ZoomLevel;
		float ThumbPad = GraphConstants::ThumbnailPadding * ZoomLevel;
		float AvailW = Size.X - ThumbPad * 2.0f;

		// Compute aspect-ratio-preserving size
		FVector2D ImgSize = Node.ThumbnailBrush->ImageSize;
		float AspectRatio = (ImgSize.Y > 0.0f) ? (ImgSize.X / ImgSize.Y) : 1.0f;
		float ThumbW = AvailW;
		float ThumbH = ThumbW / AspectRatio;
		if (ThumbH > ThumbMaxH)
		{
			ThumbH = ThumbMaxH;
			ThumbW = ThumbH * AspectRatio;
		}

		// Centre horizontally within the node
		float ThumbX = Pos.X + (Size.X - ThumbW) * 0.5f;
		float ThumbY = Pos.Y + YOffset + ThumbPad;

		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(ThumbW, ThumbH), FSlateLayoutTransform(FVector2D(ThumbX, ThumbY))),
			Node.ThumbnailBrush.Get(),
			ESlateDrawEffect::None,
			FLinearColor::White
		);

		YOffset += ThumbH + ThumbPad * 2.0f;
	}

	// 3D mesh preview area
	if (Node.MeshPreview.IsValid() && Node.MeshPreview->HasPreview())
	{
		// Render if dirty (camera moved)
		Node.MeshPreview->RenderIfDirty();

		// Check texture via raw pointer (not GetResourceObject) to avoid stale TObjectPtr crash
		UTexture2D* MeshPreviewTex = Node.MeshPreview->GetPreviewTexture();
		if (MeshPreviewTex && ::IsValid(MeshPreviewTex))
		{
			// Refresh the brush's TObjectPtr from the raw pointer before painting
			Node.MeshPreview->RefreshBrushHandle();

			TSharedPtr<FSlateBrush> MeshBrush = Node.MeshPreview->GetPreviewBrush();
			if (MeshBrush.IsValid())
			{
				float MeshPad = GraphConstants::ThumbnailPadding * ZoomLevel;
				float AvailW = Size.X - MeshPad * 2.0f;
				float PreviewSz = FMath::Min(AvailW, 160.0f * ZoomLevel);

				// Centre horizontally
				float PreviewX = Pos.X + (Size.X - PreviewSz) * 0.5f;
				float PreviewY = Pos.Y + YOffset + MeshPad;

				// Dark background for the preview area
				FSlateDrawElement::MakeBox(
					OutDrawElements, LayerId + 1,
					Geom.ToPaintGeometry(FVector2D(PreviewSz, PreviewSz), FSlateLayoutTransform(FVector2D(PreviewX, PreviewY))),
					FCoreStyle::Get().GetBrush("GenericWhiteBox"),
					ESlateDrawEffect::None,
					FLinearColor(0.08f, 0.08f, 0.08f)
				);

				// The mesh preview image
				FSlateDrawElement::MakeBox(
					OutDrawElements, LayerId + 2,
					Geom.ToPaintGeometry(FVector2D(PreviewSz, PreviewSz), FSlateLayoutTransform(FVector2D(PreviewX, PreviewY))),
					MeshBrush.Get(),
					ESlateDrawEffect::None,
					FLinearColor::White
				);

				YOffset += PreviewSz + MeshPad * 2.0f;
			}
		}
	}

	// Output pins
	for (const FGraphPin& Pin : Node.OutputPins)
	{
		FVector2D PinCenter(Pos.X + Size.X, Pos.Y + YOffset + RowH * 0.5f);
		DrawPin(PinCenter, PinR, false, false, Pin.Type, Geom, OutDrawElements, LayerId + 2);

		// Pin label (right-aligned)
		float TextWidth = Size.X * 0.45f;
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			Geom.ToPaintGeometry(FVector2D(TextWidth, RowH),
				FSlateLayoutTransform(FVector2D(Pos.X + Size.X - TextWidth - PinR * 2.5f, Pos.Y + YOffset + 1.0f * ZoomLevel))),
			Pin.Name,
			PinFont,
			ESlateDrawEffect::None,
			FLinearColor(0.75f, 0.75f, 0.80f)
		);

		YOffset += RowH;
	}

	// Border is now integrated into the rounded body brush above — no separate edge lines needed
}

// ============================================================================
// DrawPin
// ============================================================================

void SWorkflowGraphEditor::DrawPin(FVector2D Center, float Radius, bool bConnected, bool bIsInput,
	const FString& Type, const FGeometry& Geom,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	// Type-based colour
	FLinearColor PinColor(0.5f, 0.5f, 0.5f);
	if (Type == TEXT("MODEL")) PinColor = FLinearColor(0.4f, 0.6f, 0.8f);
	else if (Type == TEXT("CLIP")) PinColor = FLinearColor(0.8f, 0.8f, 0.3f);
	else if (Type == TEXT("CONDITIONING")) PinColor = FLinearColor(0.8f, 0.5f, 0.2f);
	else if (Type == TEXT("LATENT")) PinColor = FLinearColor(0.8f, 0.2f, 0.8f);
	else if (Type == TEXT("IMAGE")) PinColor = FLinearColor(0.4f, 0.8f, 0.4f);
	else if (Type == TEXT("VAE")) PinColor = FLinearColor(0.8f, 0.3f, 0.3f);
	else if (Type == TEXT("CONTROL_NET")) PinColor = FLinearColor(0.7f, 0.4f, 0.2f);
	else if (Type == TEXT("MASK")) PinColor = FLinearColor(0.9f, 0.9f, 0.9f);

	// Draw a filled circle (approximated with a small box for now)
	FVector2D TopLeft = Center - FVector2D(Radius, Radius);
	FVector2D PinSize(Radius * 2.0f, Radius * 2.0f);

	if (bConnected)
	{
		// Filled pin
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(PinSize, FSlateLayoutTransform(TopLeft)),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"),
			ESlateDrawEffect::None,
			PinColor
		);
	}
	else
	{
		// Hollow pin (border only)
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(PinSize, FSlateLayoutTransform(TopLeft)),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"),
			ESlateDrawEffect::None,
			PinColor * FLinearColor(1, 1, 1, 0.4f)
		);
	}
}

// ============================================================================
// DrawConnection (bezier curve)
// ============================================================================

void SWorkflowGraphEditor::DrawConnection(FVector2D Start, FVector2D End, FLinearColor Color,
	const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	// Use FSlateDrawElement::MakeCubicBezierSpline for clean anti-aliased vector curves
	const float Thickness = FMath::Max(2.0f * ZoomLevel, 1.0f);

	float TangentLength = FMath::Abs(End.X - Start.X) * 0.5f;
	TangentLength = FMath::Clamp(TangentLength, 40.0f, 200.0f);

	FVector2D P0 = Start;
	FVector2D P1 = Start + FVector2D(TangentLength, 0.0f);
	FVector2D P2 = End - FVector2D(TangentLength, 0.0f);
	FVector2D P3 = End;

	FSlateDrawElement::MakeCubicBezierSpline(
		OutDrawElements,
		LayerId,
		Geom.ToPaintGeometry(),
		P0, P1, P2, P3,
		Thickness,
		ESlateDrawEffect::None,
		Color
	);
}

// ============================================================================
// Mouse Interaction
// ============================================================================

FReply SWorkflowGraphEditor::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// ALT + LMB: delete connection under cursor
		if (MouseEvent.IsAltDown())
		{
			int32 HitConnIdx = HitTestConnection(LocalPos);
			if (HitConnIdx >= 0 && HitConnIdx < Connections.Num())
			{
				PushUndoSnapshot();
				Connections.RemoveAt(HitConnIdx);
				HoveredConnectionIndex = -1;
				NotifyGraphChanged();
				return FReply::Handled();
			}
			// Alt-click on empty space — fall through to normal handling
		}

		// Check for pin hit first
		FGraphPin HitPin;
		if (HitTestPin(LocalPos, HitPin))
		{
			InteractionMode = EInteractionMode::DraggingConnection;
			bDraggingFromOutput = !HitPin.bIsInput;
			DragSourceNodeId = HitPin.OwnerNodeId;
			DragSourcePinIndex = HitPin.PinIndex;
			DragSourcePinType = HitPin.Type;
			DragConnectionEnd = LocalPos;

			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		// Check for node hit
		FString HitNodeId = HitTestNode(LocalPos);
		if (!HitNodeId.IsEmpty())
		{
			const int32* IdxPtr = NodeIndexMap.Find(HitNodeId);
			if (IdxPtr)
			{
				FGraphNode& HitNode = Nodes[*IdxPtr];

				// Check if clicking the play button on a runnable node
				if (HitNode.IsRunnable() && ZoomLevel > 0.3f)
				{
					FVector2D NodeScreenPos = GraphToLocal(HitNode.Position);
					float PlayHeaderH = GraphConstants::NodeHeaderHeight * ZoomLevel;
					float BtnSize = PlayHeaderH * 0.6f;
					float BtnX = NodeScreenPos.X + HitNode.Size.X * ZoomLevel - BtnSize - 4.0f * ZoomLevel;
					float BtnY = NodeScreenPos.Y + (PlayHeaderH - BtnSize) * 0.5f;

					if (LocalPos.X >= BtnX && LocalPos.X <= BtnX + BtnSize &&
						LocalPos.Y >= BtnY && LocalPos.Y <= BtnY + BtnSize)
					{
						// Play button pressed — track pressed state, fire on release
						PressedPlayButtonNodeId = HitNode.Id;
						OnRunToNode.ExecuteIfBound(HitNode.Id);
						return FReply::Handled();
					}
				}

				// Check if clicking on the mesh preview area — start orbit
				if (HitNode.MeshPreview.IsValid() && HitNode.MeshPreview->HasPreview())
				{
					FVector2D NodeScreenPos = GraphToLocal(HitNode.Position);
					FVector2D NodeScreenSize = HitNode.Size * ZoomLevel;
					// Mesh preview is the square area at the bottom of the node (before output pins)
					float MeshPad = GraphConstants::ThumbnailPadding * ZoomLevel;
					float AvailW = NodeScreenSize.X - MeshPad * 2.0f;
					float PreviewSz = FMath::Min(AvailW, 160.0f * ZoomLevel);
					float PreviewX = NodeScreenPos.X + (NodeScreenSize.X - PreviewSz) * 0.5f;
					// Preview Y: node bottom minus output pins height minus preview area
					float OutputPinsH = HitNode.OutputPins.Num() * GraphConstants::PinRowHeight * ZoomLevel;
					float PreviewY = NodeScreenPos.Y + NodeScreenSize.Y - OutputPinsH - PreviewSz - MeshPad;

					if (LocalPos.X >= PreviewX && LocalPos.X <= PreviewX + PreviewSz &&
						LocalPos.Y >= PreviewY && LocalPos.Y <= PreviewY + PreviewSz)
					{
						MeshPreviewInteractNodeId = HitNodeId;
						MeshPreviewLastMousePos = LocalPos;
						bMeshPreviewOrbiting = true;
						bMeshPreviewPanning = false;
						return FReply::Handled().CaptureMouse(SharedThis(this));
					}
				}

				// Check header area for dragging
				FVector2D NodePos = GraphToLocal(HitNode.Position);

				if (!MouseEvent.IsShiftDown() && !SelectedNodeIds.Contains(HitNodeId))
				{
					SelectedNodeIds.Empty();
				}
				SelectedNodeIds.Add(HitNodeId);
				NotifySelectionChanged();

				PushUndoSnapshot();
				InteractionMode = EInteractionMode::DraggingNode;
				DraggedNodeId = HitNodeId;
				DragOffset = LocalToGraph(LocalPos) - HitNode.Position;

				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}
		else
		{
			// Click on empty space — start box select or clear selection
			if (!MouseEvent.IsShiftDown())
			{
				SelectedNodeIds.Empty();
				NotifySelectionChanged();
			}

			InteractionMode = EInteractionMode::BoxSelecting;
			BoxSelectStart = LocalPos;
			BoxSelectEnd = LocalPos;
			return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Check if clicking on empty space (for context menu) vs starting pan
		FString HitNodeId = HitTestNode(LocalPos);
		if (HitNodeId.IsEmpty())
		{
			InteractionMode = EInteractionMode::Panning;
			LastMousePos = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		else
		{
			// Right-click on node — could show node-specific context menu
			if (!SelectedNodeIds.Contains(HitNodeId))
			{
				SelectedNodeIds.Empty();
				SelectedNodeIds.Add(HitNodeId);
				NotifySelectionChanged();
			}

			// For now, start panning
			InteractionMode = EInteractionMode::Panning;
			LastMousePos = MouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		// Check if MMB is on a mesh preview area — start pan
		FString HitNodeId = HitTestNode(LocalPos);
		if (!HitNodeId.IsEmpty())
		{
			const int32* IdxPtr = NodeIndexMap.Find(HitNodeId);
			if (IdxPtr)
			{
				FGraphNode& HitNode = Nodes[*IdxPtr];
				if (HitNode.MeshPreview.IsValid() && HitNode.MeshPreview->HasPreview())
				{
					// Simple check: if we hit this node and it has a mesh preview, start pan
					MeshPreviewInteractNodeId = HitNodeId;
					MeshPreviewLastMousePos = LocalPos;
					bMeshPreviewPanning = true;
					bMeshPreviewOrbiting = false;
					return FReply::Handled().CaptureMouse(SharedThis(this));
				}
			}
		}

		InteractionMode = EInteractionMode::Panning;
		LastMousePos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Handled().SetUserFocus(SharedThis(this));
}

FReply SWorkflowGraphEditor::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Clear play button pressed state
	PressedPlayButtonNodeId.Empty();

	// End mesh preview interaction
	if (bMeshPreviewOrbiting || bMeshPreviewPanning)
	{
		bMeshPreviewOrbiting = false;
		bMeshPreviewPanning = false;
		MeshPreviewInteractNodeId.Empty();
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (InteractionMode == EInteractionMode::DraggingConnection)
	{
		// Try to complete the connection
		FGraphPin HitPin;
		if (HitTestPin(LocalPos, HitPin))
		{
			if (bDraggingFromOutput && HitPin.bIsInput)
			{
				// Dragged from output to input
				AddConnection(DragSourceNodeId, DragSourcePinIndex, HitPin.OwnerNodeId, HitPin.Name);
			}
			else if (!bDraggingFromOutput && !HitPin.bIsInput)
			{
				// Dragged from input to output
				AddConnection(HitPin.OwnerNodeId, HitPin.PinIndex, DragSourceNodeId,
					Nodes[NodeIndexMap[DragSourceNodeId]].InputPins[DragSourcePinIndex].Name);
			}
		}
		else
		{
			// Dropped on empty space — show a filtered node menu and auto-connect
			FVector2D GraphPos = LocalToGraph(LocalPos);
			FVector2D ScreenPos = MouseEvent.GetScreenSpacePosition();

			// Capture drag state before it gets cleared
			FString CapturedSourceNodeId = DragSourceNodeId;
			int32 CapturedSourcePinIndex = DragSourcePinIndex;
			FString CapturedSourcePinType = DragSourcePinType;
			bool bCapturedFromOutput = bDraggingFromOutput;

			TSharedRef<SWidget> FilteredMenu = BuildFilteredNodeMenu(
				GraphPos, CapturedSourcePinType, bCapturedFromOutput,
				CapturedSourceNodeId, CapturedSourcePinIndex);

			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				FilteredMenu,
				ScreenPos,
				FPopupTransitionEffect::ContextMenu
			);
		}
	}
	else if (InteractionMode == EInteractionMode::BoxSelecting)
	{
		// Select all nodes within the box
		FVector2D MinLocal(FMath::Min(BoxSelectStart.X, BoxSelectEnd.X), FMath::Min(BoxSelectStart.Y, BoxSelectEnd.Y));
		FVector2D MaxLocal(FMath::Max(BoxSelectStart.X, BoxSelectEnd.X), FMath::Max(BoxSelectStart.Y, BoxSelectEnd.Y));

		for (const FGraphNode& Node : Nodes)
		{
			FVector2D NodeLocalPos = GraphToLocal(Node.Position);
			FVector2D NodeLocalEnd = NodeLocalPos + Node.Size * ZoomLevel;

			if (NodeLocalPos.X < MaxLocal.X && NodeLocalEnd.X > MinLocal.X &&
				NodeLocalPos.Y < MaxLocal.Y && NodeLocalEnd.Y > MinLocal.Y)
			{
				SelectedNodeIds.Add(Node.Id);
			}
		}
		NotifySelectionChanged();
	}
	else if (InteractionMode == EInteractionMode::Panning && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// If barely moved, show context menu
		FVector2D Delta = MouseEvent.GetScreenSpacePosition() - LastMousePos;
		if (Delta.SizeSquared() < 25.0f)  // Less than 5px movement
		{
			FVector2D GraphPos = LocalToGraph(LocalPos);

			// Check if right-clicking on a node
			FString HitNodeId = HitTestNode(LocalPos);
			if (!HitNodeId.IsEmpty())
			{
				// Node context menu — for now just delete option
				FMenuBuilder MenuBuilder(true, nullptr);
				MenuBuilder.AddMenuEntry(
					FText::FromString(TEXT("Delete Node")),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, HitNodeId]()
					{
						RemoveNode(HitNodeId);
					}))
				);

				MenuBuilder.AddMenuEntry(
					FText::FromString(TEXT("Duplicate Node")),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, HitNodeId, GraphPos]()
					{
						const int32* Idx = NodeIndexMap.Find(HitNodeId);
						if (Idx)
						{
							FString NewId = AddNodeByType(Nodes[*Idx].ClassType, GraphPos + FVector2D(30, 30));
							// Copy widget values
							const int32* NewIdx = NodeIndexMap.Find(NewId);
							if (NewIdx && Idx)
							{
								Nodes[*NewIdx].WidgetValues = Nodes[*Idx].WidgetValues;
							}
						}
					}))
				);

				FSlateApplication::Get().PushMenu(
					SharedThis(this),
					FWidgetPath(),
					MenuBuilder.MakeWidget(),
					MouseEvent.GetScreenSpacePosition(),
					FPopupTransitionEffect::ContextMenu
				);
			}
			else
			{
				// Empty space — show add node menu
				ShowAddNodeMenu(MouseEvent.GetScreenSpacePosition(), GraphPos);
			}
		}
	}

	InteractionMode = EInteractionMode::None;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SWorkflowGraphEditor::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Handle mesh preview orbit/pan (independent of InteractionMode)
	if ((bMeshPreviewOrbiting || bMeshPreviewPanning) && !MeshPreviewInteractNodeId.IsEmpty())
	{
		const int32* IdxPtr = NodeIndexMap.Find(MeshPreviewInteractNodeId);
		if (IdxPtr && Nodes[*IdxPtr].MeshPreview.IsValid())
		{
			FVector2D Delta = LocalPos - MeshPreviewLastMousePos;
			MeshPreviewLastMousePos = LocalPos;

			FMeshPreviewRenderer& Renderer = *Nodes[*IdxPtr].MeshPreview;
			if (bMeshPreviewOrbiting)
			{
				Renderer.Orbit(Delta.X * 0.5f, Delta.Y * 0.5f);
			}
			else if (bMeshPreviewPanning)
			{
				float PanScale = Renderer.OrbitDistance > 0.0f ? Renderer.OrbitDistance * 0.003f : 0.5f;
				Renderer.Pan(-Delta.X * PanScale, Delta.Y * PanScale);
			}
		}
		return FReply::Handled();
	}

	switch (InteractionMode)
	{
	case EInteractionMode::Panning:
	{
		FVector2D CurrentPos = MouseEvent.GetScreenSpacePosition();
		FVector2D Delta = (CurrentPos - LastMousePos) / ZoomLevel;
		ViewOffset += Delta;
		LastMousePos = CurrentPos;
		return FReply::Handled();
	}

	case EInteractionMode::DraggingNode:
	{
		FVector2D GraphPos = LocalToGraph(LocalPos) - DragOffset;

		// Snap to grid
		GraphPos.X = FMath::RoundToFloat(GraphPos.X / GraphConstants::GridSize) * GraphConstants::GridSize;
		GraphPos.Y = FMath::RoundToFloat(GraphPos.Y / GraphConstants::GridSize) * GraphConstants::GridSize;

		const int32* IdxPtr = NodeIndexMap.Find(DraggedNodeId);
		if (IdxPtr)
		{
			FVector2D Delta = GraphPos - Nodes[*IdxPtr].Position;
			Nodes[*IdxPtr].Position = GraphPos;

			// Move all other selected nodes by the same delta
			for (const FString& SelId : SelectedNodeIds)
			{
				if (SelId != DraggedNodeId)
				{
					const int32* SelIdx = NodeIndexMap.Find(SelId);
					if (SelIdx)
					{
						Nodes[*SelIdx].Position += Delta;
					}
				}
			}
		}
		return FReply::Handled();
	}

	case EInteractionMode::DraggingConnection:
	{
		DragConnectionEnd = LocalPos;
		return FReply::Handled();
	}

	case EInteractionMode::BoxSelecting:
	{
		BoxSelectEnd = LocalPos;
		return FReply::Handled();
	}

	default:
	{
		// Update connection hover highlight when idle
		int32 NewHovered = HitTestConnection(LocalPos);
		if (NewHovered != HoveredConnectionIndex)
		{
			HoveredConnectionIndex = NewHovered;
		}

		// Update play button hover state
		FString NewHoveredPlayBtn;
		for (const FGraphNode& Node : Nodes)
		{
			if (!Node.IsRunnable() || ZoomLevel <= 0.3f) continue;

			FVector2D NodeScreenPos = GraphToLocal(Node.Position);
			float PlayHeaderH = GraphConstants::NodeHeaderHeight * ZoomLevel;
			float BtnSize = PlayHeaderH * 0.6f;
			float BtnX = NodeScreenPos.X + Node.Size.X * ZoomLevel - BtnSize - 4.0f * ZoomLevel;
			float BtnY = NodeScreenPos.Y + (PlayHeaderH - BtnSize) * 0.5f;

			if (LocalPos.X >= BtnX && LocalPos.X <= BtnX + BtnSize &&
				LocalPos.Y >= BtnY && LocalPos.Y <= BtnY + BtnSize)
			{
				NewHoveredPlayBtn = Node.Id;
				break;
			}
		}
		HoveredPlayButtonNodeId = NewHoveredPlayBtn;

		break;
	}
	}

	return FReply::Unhandled();
}

FReply SWorkflowGraphEditor::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Check if scrolling over a mesh preview — zoom the 3D view instead of the graph
	FString HitNodeId = HitTestNode(LocalPos);
	if (!HitNodeId.IsEmpty())
	{
		const int32* IdxPtr = NodeIndexMap.Find(HitNodeId);
		if (IdxPtr)
		{
			FGraphNode& HitNode = Nodes[*IdxPtr];
			if (HitNode.MeshPreview.IsValid() && HitNode.MeshPreview->HasPreview())
			{
				float ZoomDelta = MouseEvent.GetWheelDelta() * 20.0f;
				HitNode.MeshPreview->Zoom(ZoomDelta);
				return FReply::Handled();
			}
		}
	}

	FVector2D GraphPosBefore = LocalToGraph(LocalPos);

	float Delta = MouseEvent.GetWheelDelta() * GraphConstants::ZoomStep;
	ZoomLevel = FMath::Clamp(ZoomLevel + Delta, GraphConstants::MinZoom, GraphConstants::MaxZoom);

	// Zoom toward cursor
	FVector2D GraphPosAfter = LocalToGraph(LocalPos);
	ViewOffset += (GraphPosAfter - GraphPosBefore);

	return FReply::Handled();
}

FReply SWorkflowGraphEditor::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Double-click on a node — select it
	FString HitNodeId = HitTestNode(LocalPos);
	if (!HitNodeId.IsEmpty())
	{
		SelectedNodeIds.Empty();
		SelectedNodeIds.Add(HitNodeId);
			NotifySelectionChanged();
		return FReply::Handled();
	}

	// Double-click on a connection curve — insert a reroute node
	int32 ConnIdx = HitTestConnection(LocalPos);
	if (ConnIdx >= 0 && ConnIdx < Connections.Num())
	{
		FVector2D GraphPos = LocalToGraph(LocalPos);
		// Snap to grid
		GraphPos.X = FMath::RoundToFloat(GraphPos.X / GraphConstants::GridSize) * GraphConstants::GridSize;
		GraphPos.Y = FMath::RoundToFloat(GraphPos.Y / GraphConstants::GridSize) * GraphConstants::GridSize;

		FString NewId = InsertRerouteNode(ConnIdx, GraphPos);
		if (!NewId.IsEmpty())
		{
			SelectedNodeIds.Empty();
			SelectedNodeIds.Add(NewId);
				NotifySelectionChanged();
		}
		return FReply::Handled();
	}

	return FReply::Handled();
}

FReply SWorkflowGraphEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Undo: Ctrl+Z
	if (InKeyEvent.GetKey() == EKeys::Z && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown())
	{
		Undo();
		return FReply::Handled();
	}

	// Redo: Ctrl+Y or Ctrl+Shift+Z
	if ((InKeyEvent.GetKey() == EKeys::Y && InKeyEvent.IsControlDown()) ||
		(InKeyEvent.GetKey() == EKeys::Z && InKeyEvent.IsControlDown() && InKeyEvent.IsShiftDown()))
	{
		Redo();
		return FReply::Handled();
	}

	// Copy: Ctrl+C
	if (InKeyEvent.GetKey() == EKeys::C && InKeyEvent.IsControlDown())
	{
		CopySelectedNodes();
		return FReply::Handled();
	}

	// Paste: Ctrl+V
	if (InKeyEvent.GetKey() == EKeys::V && InKeyEvent.IsControlDown())
	{
		PasteNodes();
		return FReply::Handled();
	}

	// Cut: Ctrl+X
	if (InKeyEvent.GetKey() == EKeys::X && InKeyEvent.IsControlDown())
	{
		CutSelectedNodes();
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		if (SelectedNodeIds.Num() > 0)
		{
			// Push a single undo snapshot before deleting all selected nodes
			PushUndoSnapshot();
			TArray<FString> ToDelete = SelectedNodeIds.Array();
			SelectedNodeIds.Empty();
			NotifySelectionChanged();
			bIsRestoringSnapshot = true; // Suppress per-node undo pushes
			for (const FString& NodeId : ToDelete)
			{
				RemoveNode(NodeId);
			}
			bIsRestoringSnapshot = false;
		}
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::A && InKeyEvent.IsControlDown())
	{
		// Select all
		SelectedNodeIds.Empty();
		for (const FGraphNode& Node : Nodes)
		{
			SelectedNodeIds.Add(Node.Id);
		}
		NotifySelectionChanged();
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::L && InKeyEvent.IsControlDown())
	{
		// Auto-layout
		PushUndoSnapshot();
		AutoLayout();
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::D && InKeyEvent.IsControlDown())
	{
		// Duplicate selected nodes
		PushUndoSnapshot();
		if (SelectedNodeIds.Num() > 0)
		{
			// Map from original node ID -> new cloned node ID
			TMap<FString, FString> IdRemap;
			TSet<FString> NewSelectedIds;
			const FVector2D DuplicateOffset(40.0f, 40.0f);

			// Clone each selected node
			TArray<FString> OriginalIds = SelectedNodeIds.Array();
			for (const FString& OrigId : OriginalIds)
			{
				const int32* IdxPtr = NodeIndexMap.Find(OrigId);
				if (!IdxPtr) continue;

				const FGraphNode& OrigNode = Nodes[*IdxPtr];
				FString NewId = FString::FromInt(NextAutoNodeId++);

				FGraphNode NewNode;
				NewNode.Id = NewId;
				NewNode.ClassType = OrigNode.ClassType;
				NewNode.Title = OrigNode.Title;
				NewNode.Position = OrigNode.Position + DuplicateOffset;
				NewNode.Size = OrigNode.Size;
				NewNode.WidgetValues = OrigNode.WidgetValues;
				NewNode.WidgetInputDefs = OrigNode.WidgetInputDefs;
				NewNode.WidgetOrder = OrigNode.WidgetOrder;
				NewNode.InputPins = OrigNode.InputPins;
				for (FGraphPin& Pin : NewNode.InputPins) { Pin.OwnerNodeId = NewId; }
				NewNode.OutputPins = OrigNode.OutputPins;
				for (FGraphPin& Pin : NewNode.OutputPins) { Pin.OwnerNodeId = NewId; }
				NewNode.HeaderColor = OrigNode.HeaderColor;
				NewNode.bSelected = false;
				NewNode.NodeDef = OrigNode.NodeDef;
				// ThumbnailBrush is NOT duplicated — the clone starts without a thumbnail

				int32 NewIdx = Nodes.Add(MoveTemp(NewNode));
				NodeIndexMap.Add(NewId, NewIdx);

				IdRemap.Add(OrigId, NewId);
				NewSelectedIds.Add(NewId);
			}

			// Duplicate connections that exist entirely within the selected set
			int32 ConnCount = Connections.Num();
			for (int32 i = 0; i < ConnCount; ++i)
			{
				const FGraphConnection& Conn = Connections[i];
				const FString* NewSourceId = IdRemap.Find(Conn.SourceNodeId);
				const FString* NewTargetId = IdRemap.Find(Conn.TargetNodeId);
				if (NewSourceId && NewTargetId)
				{
					FGraphConnection NewConn;
					NewConn.SourceNodeId = *NewSourceId;
					NewConn.SourceOutputIndex = Conn.SourceOutputIndex;
					NewConn.TargetNodeId = *NewTargetId;
					NewConn.TargetInputName = Conn.TargetInputName;
					Connections.Add(NewConn);
				}
			}

			// Select the new duplicates
			SelectedNodeIds = MoveTemp(NewSelectedIds);
			NotifySelectionChanged();
			NotifyGraphChanged();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

// ============================================================================
// Widget Editing
// ============================================================================

void SWorkflowGraphEditor::CommitWidgetValue(const FString& NodeId, const FString& WidgetName, const FString& NewValue)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return;

	FGraphNode& Node = Nodes[*IdxPtr];
	FString* ValPtr = Node.WidgetValues.Find(WidgetName);
	if (!ValPtr) return;

	// Validate numeric inputs
	const FComfyInputDef* WidgetDef = Node.WidgetInputDefs.Find(WidgetName);
	FString FinalValue = NewValue;

	if (WidgetDef)
	{
		if (WidgetDef->Type == TEXT("INT"))
		{
			int32 IntVal;
			if (FDefaultValueHelper::ParseInt(NewValue, IntVal))
			{
				IntVal = FMath::Clamp(IntVal, static_cast<int32>(WidgetDef->MinValue), static_cast<int32>(WidgetDef->MaxValue));
				FinalValue = FString::FromInt(IntVal);
			}
			// else keep old value
		}
		else if (WidgetDef->Type == TEXT("FLOAT"))
		{
			double FloatVal;
			if (FDefaultValueHelper::ParseDouble(NewValue, FloatVal))
			{
				FloatVal = FMath::Clamp(FloatVal, WidgetDef->MinValue, WidgetDef->MaxValue);
				FinalValue = FString::SanitizeFloat(FloatVal);
			}
		}
	}

	*ValPtr = FinalValue;

	NotifyGraphChanged();
}

void SWorkflowGraphEditor::CommitWidgetValueSilent(const FString& NodeId, const FString& WidgetName, const FString& NewValue)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return;

	FGraphNode& Node = Nodes[*IdxPtr];
	FString* ValPtr = Node.WidgetValues.Find(WidgetName);
	if (!ValPtr) return;

	// Validate numeric inputs (same as CommitWidgetValue)
	const FComfyInputDef* WidgetDef = Node.WidgetInputDefs.Find(WidgetName);
	FString FinalValue = NewValue;

	if (WidgetDef)
	{
		if (WidgetDef->Type == TEXT("INT"))
		{
			int32 IntVal;
			if (FDefaultValueHelper::ParseInt(NewValue, IntVal))
			{
				IntVal = FMath::Clamp(IntVal, static_cast<int32>(WidgetDef->MinValue), static_cast<int32>(WidgetDef->MaxValue));
				FinalValue = FString::FromInt(IntVal);
			}
		}
		else if (WidgetDef->Type == TEXT("FLOAT"))
		{
			double FloatVal;
			if (FDefaultValueHelper::ParseDouble(NewValue, FloatVal))
			{
				FloatVal = FMath::Clamp(FloatVal, WidgetDef->MinValue, WidgetDef->MaxValue);
				FinalValue = FString::SanitizeFloat(FloatVal);
			}
		}
	}

	*ValPtr = FinalValue;

	// Only mark dirty — do NOT fire OnGraphChanged delegate.
	// This prevents the details panel from being rebuilt mid-callback,
	// which would destroy the active widget and can close the fullscreen window.
	bGraphDirty = true;
}

void SWorkflowGraphEditor::SetNodeThumbnail(const FString& NodeId, UTexture2D* Texture)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return;

	FGraphNode& Node = Nodes[*IdxPtr];

	// Unroot previous thumbnail texture to prevent memory leak
	// Use stored raw pointer (not GetResourceObject) to avoid stale TObjectPtr crash
	if (Node.ThumbnailTexture && Node.ThumbnailTexture != Texture
		&& ::IsValid(Node.ThumbnailTexture) && Node.ThumbnailTexture->IsRooted())
	{
		Node.ThumbnailTexture->RemoveFromRoot();
	}

	if (!Texture || !::IsValid(Texture))
	{
		Node.ThumbnailBrush.Reset();
		Node.ThumbnailTexture = nullptr;
		ComputeNodeSize(Node);
		return;
	}

	if (!Node.ThumbnailBrush.IsValid())
	{
		Node.ThumbnailBrush = MakeShareable(new FSlateBrush());
	}

	// Defensively ensure texture is rooted (prevents GC during paint)
	if (!Texture->IsRooted())
	{
		Texture->AddToRoot();
	}

	Node.ThumbnailTexture = Texture;
	Node.ThumbnailBrush->SetResourceObject(Texture);
	Node.ThumbnailBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Node.ThumbnailBrush->DrawAs = ESlateBrushDrawType::Image;

	ComputeNodeSize(Node);
}

void SWorkflowGraphEditor::SetNodeMeshPreview(const FString& NodeId, UStaticMesh* Mesh)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return;

	FGraphNode& Node = Nodes[*IdxPtr];

	if (!Mesh || !::IsValid(Mesh))
	{
		Node.MeshPreview.Reset();
		ComputeNodeSize(Node);
		return;
	}

	if (!Node.MeshPreview.IsValid())
	{
		Node.MeshPreview = MakeShareable(new FMeshPreviewRenderer());
	}

	Node.MeshPreview->SetMesh(Mesh);
	ComputeNodeSize(Node);
}

void SWorkflowGraphEditor::SetExecutingNode(const FString& NodeId)
{
	bool bChanged = false;
	for (FGraphNode& Node : Nodes)
	{
		bool bShouldExecute = (Node.Id == NodeId);
		if (Node.bIsExecuting != bShouldExecute)
		{
			Node.bIsExecuting = bShouldExecute;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: SetExecutingNode('%s') — highlighting updated"), *NodeId);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWorkflowGraphEditor::ClearExecutingNodes()
{
	bool bChanged = false;
	for (FGraphNode& Node : Nodes)
	{
		if (Node.bIsExecuting)
		{
			Node.bIsExecuting = false;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SWorkflowGraphEditor::SetNodeWidgetValue(const FString& NodeId, const FString& WidgetName, const FString& NewValue)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (!IdxPtr) return false;

	FGraphNode& Node = Nodes[*IdxPtr];
	FString* ValPtr = Node.WidgetValues.Find(WidgetName);
	if (!ValPtr) return false;

	*ValPtr = NewValue;
	NotifyGraphChanged();
	return true;
}

int32 SWorkflowGraphEditor::SetAllPromptTexts(const FString& PositivePrompt, const FString& NegativePrompt)
{
	// Widget names that are considered "prompt" fields across all node types.
	// This covers CLIPTextEncode ("text"), Nano Banana 2 ("prompt"), and other
	// custom nodes that use common naming conventions for their text inputs.
	static const TArray<FString> PromptWidgetNames = {
		TEXT("text"),
		TEXT("prompt"),
		TEXT("positive"),
		TEXT("negative"),
		TEXT("positive_prompt"),
		TEXT("negative_prompt"),
		TEXT("pos_prompt"),
		TEXT("neg_prompt"),
		TEXT("string"),			// some nodes use generic "string" for prompt input
	};

	// Widget names that are inherently negative (regardless of node title)
	static const TArray<FString> NegativeWidgetNames = {
		TEXT("negative"),
		TEXT("negative_prompt"),
		TEXT("neg_prompt"),
	};

	int32 UpdatedCount = 0;

	for (FGraphNode& Node : Nodes)
	{
		// Skip UE source nodes and reroute nodes — they don't have prompt fields
		if (Node.IsUESourceNode() || Node.IsReroute())
		{
			continue;
		}

		// Check each widget on the node for prompt-like names
		for (const FString& WidgetName : Node.WidgetOrder)
		{
			FString LowerWidget = WidgetName.ToLower();
			bool bIsPromptField = false;
			for (const FString& PromptName : PromptWidgetNames)
			{
				if (LowerWidget == PromptName)
				{
					bIsPromptField = true;
					break;
				}
			}

			if (!bIsPromptField)
			{
				continue;
			}

			// Verify this widget exists and is a STRING type (not a number or combo
			// that happens to share a name). Check the input def if available.
			const FComfyInputDef* InputDef = Node.WidgetInputDefs.Find(WidgetName);
			if (InputDef && InputDef->Type != TEXT("STRING"))
			{
				continue;
			}

			FString* ValPtr = Node.WidgetValues.Find(WidgetName);
			if (!ValPtr)
			{
				continue;
			}

			// Determine positive vs negative from widget name first, then node title
			bool bIsNegative = false;
			for (const FString& NegName : NegativeWidgetNames)
			{
				if (LowerWidget == NegName)
				{
					bIsNegative = true;
					break;
				}
			}
			if (!bIsNegative)
			{
				// Fall back to title heuristic
				FString LowerTitle = Node.Title.ToLower();
				bIsNegative = LowerTitle.Contains(TEXT("negative")) || LowerTitle.Contains(TEXT("neg"));
			}

			if (bIsNegative)
			{
				*ValPtr = NegativePrompt;
				UpdatedCount++;
			}
			else
			{
				*ValPtr = PositivePrompt;
				UpdatedCount++;
			}
		}
	}

	if (UpdatedCount > 0)
	{
		NotifyGraphChanged();
	}
	return UpdatedCount;
}

// ============================================================================
// Context Menu — Add Node
// ============================================================================

void SWorkflowGraphEditor::ShowAddNodeMenu(FVector2D ScreenPos, FVector2D GraphPos)
{
	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();
	if (!DB.IsPopulated())
	{
		// Show a simple "no nodes available" message
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("No node data loaded — connect to ComfyUI first")),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction()
		);

		FSlateApplication::Get().PushMenu(
			SharedThis(this),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			ScreenPos,
			FPopupTransitionEffect::ContextMenu
		);
		return;
	}

	// Build the category menu and push it as a popup.
	// FMenuBuilder provides a built-in search bar automatically.
	TSharedRef<SWidget> MenuContent = BuildNodeCategoryMenu(GraphPos);

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		MenuContent,
		ScreenPos,
		FPopupTransitionEffect::ContextMenu
	);
}

TSharedRef<SWidget> SWorkflowGraphEditor::BuildNodeCategoryMenu(FVector2D GraphPos)
{
	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();

	FMenuBuilder MenuBuilder(true, nullptr);

	// Category menu content
	// Group by category
	TArray<FString> Categories = DB.GetCategories();

	// Unreal Engine source nodes section
	MenuBuilder.BeginSection("UENodes", FText::FromString(TEXT("Unreal Engine")));
	{
		struct FUENodeEntry { FString ClassType; FString DisplayName; FString Tooltip; };
		TArray<FUENodeEntry> UENodes = {
			{ UEViewportClassType, TEXT("UE Viewport Capture"), TEXT("Captures the active editor viewport as an IMAGE") },
			{ UEDepthMapClassType, TEXT("UE Depth Map"), TEXT("Captures a linearized depth map from the viewport as an IMAGE") },
			{ UECameraDataClassType, TEXT("UE Camera Data"), TEXT("Outputs camera info (FOV, position, rotation, focal length) as a STRING for prompt enrichment") },
			{ UESegmentationClassType, TEXT("UE Segmentation Mask"), TEXT("Captures a colour-coded segmentation mask from the viewport — each actor gets a unique colour for element isolation") },
			{ UEMeshyImportClassType, TEXT("Meshy Import to Level"), TEXT("Downloads a Meshy Image-to-3D result, imports it as a UE asset, and optionally places it in the current level") },
			{ UESave3DModelClassType, TEXT("UE Save 3D Model"), TEXT("Downloads a 3D model (GLB/OBJ/FBX) from any ComfyUI mesh generation node and imports it into the UE Content Browser") },
			{ UE3DLoaderClassType, TEXT("UE 3D Loader"), TEXT("Browse for a local 3D model file (GLB/OBJ/FBX), import it into the UE Content Browser, and optionally place it in the level") },
			{ UEImageBridgeClassType, TEXT("UE Image Bridge"), TEXT("Saves the upstream image to disk (SaveImage) and outputs it as a loadable reference (LoadImage). Creates a disk-based handoff between image generation and downstream nodes like Tripo.") },
			{ UE3DAssetExportClassType, TEXT("UE 3D Asset Export"), TEXT("Downloads a generated 3D model and imports it into the UE Content Browser as a .uasset with collision, Nanite, and auto-placement options") },
			{ UEPromptAdherenceClassType, TEXT("UE Prompt Adherence"), TEXT("Controls how closely the generation follows the prompt vs. AI creativity. Lower values = closer to prompt, higher values = more creative. Overrides CFG/steps/denoise on all KSampler nodes in the graph.") },
			{ UEImageUpresClassType, TEXT("UE Image Upres"), TEXT("Upscales an image using configurable interpolation (nearest, bilinear, bicubic, lanczos, area) and converts between 8-bit and 16-bit colour depth. Maps to ImageScaleBy + a bit-depth conversion pass in ComfyUI.") },
			{ UESequenceClassType, TEXT("UE Sequence"), TEXT("Executes downstream nodes in stages — all nodes connected to Then 0 complete before Then 1 starts, and so on. Like Blueprint Sequence nodes.") },
			{ UEVideoToImageClassType, TEXT("UE Video to Image"), TEXT("Extracts a single frame from a video file (using FFmpeg) and outputs it as an IMAGE. Browse for a video, choose the frame number, and connect downstream to any node expecting an image input.") }
		};

		for (const FUENodeEntry& Entry : UENodes)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(Entry.DisplayName),
				FText::FromString(Entry.Tooltip),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, ClassType = Entry.ClassType, GraphPos]()
				{
					AddNodeByType(ClassType, GraphPos);
				}))
			);
		}
	}
	MenuBuilder.EndSection();

	// Also add a "Frequently Used" section at the top
	MenuBuilder.BeginSection("FrequentNodes", FText::FromString(TEXT("Frequently Used")));
	{
		TArray<FString> FrequentTypes = {
			TEXT("CheckpointLoaderSimple"), TEXT("CLIPTextEncode"), TEXT("KSampler"),
			TEXT("VAEDecode"), TEXT("SaveImage"), TEXT("EmptyLatentImage"),
			TEXT("LoadImage"), TEXT("LoraLoader"), TEXT("VAEEncode"),
			TEXT("ControlNetLoader"), TEXT("ControlNetApply")
		};

		for (const FString& ClassType : FrequentTypes)
		{
			const FComfyNodeDef* Def = DB.FindNode(ClassType);
			if (Def)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(Def->DisplayName),
					FText::FromString(Def->ClassType),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, ClassType, GraphPos]()
					{
						AddNodeByType(ClassType, GraphPos);
					}))
				);
			}
		}
	}
	MenuBuilder.EndSection();

	// Build a category tree from slash-separated paths so that
	// "loaders/checkpoints" nests inside a "loaders" parent submenu.
	struct FCategoryTreeNode
	{
		FString Name;                                  // Segment name (e.g. "checkpoints")
		TMap<FString, TSharedPtr<FCategoryTreeNode>> Children;
		TArray<const FComfyNodeDef*> Nodes;            // Leaf nodes at this level
	};

	TSharedPtr<FCategoryTreeNode> Root = MakeShareable(new FCategoryTreeNode());

	for (const FString& Category : Categories)
	{
		TArray<const FComfyNodeDef*> NodesInCat = DB.GetNodesInCategory(Category);
		if (NodesInCat.Num() == 0) continue;

		// Walk the path segments, creating tree nodes as needed
		TArray<FString> Segments;
		Category.ParseIntoArray(Segments, TEXT("/"), true);

		TSharedPtr<FCategoryTreeNode> Current = Root;
		for (const FString& Seg : Segments)
		{
			TSharedPtr<FCategoryTreeNode>& Child = Current->Children.FindOrAdd(Seg);
			if (!Child.IsValid())
			{
				Child = MakeShareable(new FCategoryTreeNode());
				Child->Name = Seg;
			}
			Current = Child;
		}

		// Attach the nodes to the deepest (leaf) tree node
		Current->Nodes.Append(NodesInCat);
	}

	// Recursive lambda to populate menus from the tree.
	// Wrapped in TSharedRef so deferred submenu delegates can safely capture it by value.
	TSharedRef<TFunction<void(FMenuBuilder&, TSharedPtr<FCategoryTreeNode>)>> PopulateMenuPtr =
		MakeShareable(new TFunction<void(FMenuBuilder&, TSharedPtr<FCategoryTreeNode>)>());

	*PopulateMenuPtr = [this, GraphPos, PopulateMenuPtr](FMenuBuilder& Menu, TSharedPtr<FCategoryTreeNode> TreeNode)
	{
		// Sort children alphabetically
		TArray<FString> ChildKeys;
		TreeNode->Children.GetKeys(ChildKeys);
		ChildKeys.Sort();

		for (const FString& Key : ChildKeys)
		{
			TSharedPtr<FCategoryTreeNode> Child = TreeNode->Children[Key];

			Menu.AddSubMenu(
				FText::FromString(Key),
				FText::GetEmpty(),
				FNewMenuDelegate::CreateLambda([this, Child, GraphPos, PopulateMenuPtr](FMenuBuilder& SubMenu)
				{
					// First, add any direct nodes at this level
					for (const FComfyNodeDef* Def : Child->Nodes)
					{
						SubMenu.AddMenuEntry(
							FText::FromString(Def->DisplayName),
							FText::FromString(Def->ClassType),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, ClassType = Def->ClassType, GraphPos]()
							{
								AddNodeByType(ClassType, GraphPos);
							}))
						);
					}

					// Then, add child category submenus
					(*PopulateMenuPtr)(SubMenu, Child);
				})
			);
		}

		// Add any direct nodes at the root level (categories without a slash)
		for (const FComfyNodeDef* Def : TreeNode->Nodes)
		{
			Menu.AddMenuEntry(
				FText::FromString(Def->DisplayName),
				FText::FromString(Def->ClassType),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, ClassType = Def->ClassType, GraphPos]()
				{
					AddNodeByType(ClassType, GraphPos);
				}))
			);
		}
	};

	MenuBuilder.BeginSection("ComfyNodes", FText::FromString(TEXT("ComfyUI Nodes")));
	(*PopulateMenuPtr)(MenuBuilder, Root);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SWorkflowGraphEditor::BuildFilteredNodeMenu(FVector2D GraphPos,
	const FString& PinType, bool bFromOutput,
	const FString& SourceNodeId, int32 SourcePinIndex)
{
	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();
	FMenuBuilder MenuBuilder(true, nullptr);

	if (!DB.IsPopulated())
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("No node data loaded")),
			FText::GetEmpty(), FSlateIcon(), FUIAction());
		return MenuBuilder.MakeWidget();
	}

	// Collect all compatible node defs, grouped by category.
	// If we dragged FROM an output, we need nodes that have a matching INPUT.
	// If we dragged FROM an input, we need nodes that have a matching OUTPUT.
	struct FCompatibleEntry
	{
		const FComfyNodeDef* Def;
		FString MatchedPinName; // The pin on the new node to auto-connect
		int32 MatchedPinIndex;
	};

	// Also include UE source nodes in the filtered pin-drag menu.
	// OutputType = what the node produces (empty if none), InputType = what it accepts (empty if none).
	struct FUENodeEntry { FString ClassType; FString DisplayName; FString OutputType; FString InputType; };
	TArray<FUENodeEntry> UENodes = {
		{ UEViewportClassType, TEXT("UE Viewport Capture"), TEXT("IMAGE"), TEXT("") },
		{ UEDepthMapClassType, TEXT("UE Depth Map"), TEXT("IMAGE"), TEXT("") },
		{ UECameraDataClassType, TEXT("UE Camera Data"), TEXT("STRING"), TEXT("") },
		{ UESegmentationClassType, TEXT("UE Segmentation Mask"), TEXT("IMAGE"), TEXT("") },
		{ UEMeshyImportClassType, TEXT("Meshy Import to Level"), TEXT("*"), TEXT("") },
		{ UESave3DModelClassType, TEXT("UE Save 3D Model"), TEXT("*"), TEXT("*") },
		{ UE3DLoaderClassType, TEXT("UE 3D Loader"), TEXT("*"), TEXT("") },
		{ UEImageBridgeClassType, TEXT("UE Image Bridge"), TEXT("IMAGE"), TEXT("IMAGE") },
		{ UE3DAssetExportClassType, TEXT("UE 3D Asset Export"), TEXT("*"), TEXT("*") },
		{ UEPromptAdherenceClassType, TEXT("UE Prompt Adherence"), TEXT(""), TEXT("") },
		{ UEImageUpresClassType, TEXT("UE Image Upres"), TEXT("IMAGE"), TEXT("IMAGE") },
		{ UESequenceClassType, TEXT("UE Sequence"), TEXT("*"), TEXT("") },
		{ UEVideoToImageClassType, TEXT("UE Video to Image"), TEXT(""), TEXT("IMAGE") }
	};

	bool bHasUEEntries = false;
	for (const FUENodeEntry& UEEntry : UENodes)
	{
		bool bCompatible = false;
		if (bFromOutput)
		{
			// Dragged from output — check if this UE node has a compatible input
			if (!UEEntry.InputType.IsEmpty() && AreTypesCompatible(PinType, UEEntry.InputType))
			{
				bCompatible = true;
			}
		}
		else
		{
			// Dragged from input — looking for nodes with matching outputs
			if (!UEEntry.OutputType.IsEmpty() && AreTypesCompatible(UEEntry.OutputType, PinType))
			{
				bCompatible = true;
			}
		}

		if (bCompatible && !bHasUEEntries)
		{
			MenuBuilder.BeginSection("UENodes", FText::FromString(TEXT("Unreal Engine")));
			bHasUEEntries = true;
		}
		if (bCompatible)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(UEEntry.DisplayName),
				FText::FromString(UEEntry.ClassType),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[this, ClassType = UEEntry.ClassType, GraphPos,
					 SourceNodeId, SourcePinIndex, bFromOutput]()
				{
					FString NewNodeId = AddNodeByType(ClassType, GraphPos);
					if (NewNodeId.IsEmpty()) return;

					const int32* NewIdx = NodeIndexMap.Find(NewNodeId);
					if (!NewIdx) return;
					const FGraphNode& NewNode = Nodes[*NewIdx];

					if (bFromOutput)
					{
						// New node's first compatible input
						for (const FGraphPin& Pin : NewNode.InputPins)
						{
							AddConnection(SourceNodeId, SourcePinIndex, NewNodeId, Pin.Name);
							break;
						}
					}
					else
					{
						// New node's first output → source's input
						const int32* SrcIdx = NodeIndexMap.Find(SourceNodeId);
						if (SrcIdx && SourcePinIndex < Nodes[*SrcIdx].InputPins.Num())
						{
							FString InputName = Nodes[*SrcIdx].InputPins[SourcePinIndex].Name;
							if (NewNode.OutputPins.Num() > 0)
							{
								AddConnection(NewNodeId, 0, SourceNodeId, InputName);
							}
						}
					}
				}))
			);
		}
	}
	if (bHasUEEntries)
	{
		MenuBuilder.EndSection();
	}

	// Gather compatible ComfyUI nodes and build a category tree (same structure as right-click menu)
	struct FFilteredCategoryNode
	{
		FString Name;
		TMap<FString, TSharedPtr<FFilteredCategoryNode>> Children;
		TArray<FCompatibleEntry> Nodes;
	};

	TSharedPtr<FFilteredCategoryNode> FilteredRoot = MakeShareable(new FFilteredCategoryNode());
	int32 TotalCompatible = 0;
	TSet<FString> SeenDisplayNames; // Deduplicate nodes with identical display names

	const TMap<FString, FComfyNodeDef>& AllNodes = DB.GetAllNodes();
	for (const auto& Pair : AllNodes)
	{
		const FComfyNodeDef* Def = &Pair.Value;

		// Skip duplicate display names (some ComfyUI nodes register aliases)
		FString DedupeKey = Def->DisplayName + TEXT("::") + Def->Category;
		if (SeenDisplayNames.Contains(DedupeKey))
		{
			continue;
		}

		FCompatibleEntry Entry;
		Entry.Def = Def;
		Entry.MatchedPinIndex = -1;

		if (bFromOutput)
		{
			// We need a node with a compatible INPUT
			TArray<const FComfyInputDef*> LinkInputs = Def->GetLinkInputs();
			for (int32 i = 0; i < LinkInputs.Num(); ++i)
			{
				if (AreTypesCompatible(PinType, LinkInputs[i]->Type))
				{
					Entry.MatchedPinName = LinkInputs[i]->Name;
					Entry.MatchedPinIndex = i;
					break;
				}
			}
		}
		else
		{
			// We need a node with a compatible OUTPUT
			for (int32 i = 0; i < Def->Outputs.Num(); ++i)
			{
				if (AreTypesCompatible(Def->Outputs[i].Type, PinType))
				{
					Entry.MatchedPinName = Def->Outputs[i].Name;
					Entry.MatchedPinIndex = i;
					break;
				}
			}
		}

		if (Entry.MatchedPinIndex >= 0)
		{
			SeenDisplayNames.Add(DedupeKey);

			FString Cat = Def->Category.IsEmpty() ? TEXT("Other") : Def->Category;
			TArray<FString> Segments;
			Cat.ParseIntoArray(Segments, TEXT("/"), true);

			TSharedPtr<FFilteredCategoryNode> Current = FilteredRoot;
			for (const FString& Seg : Segments)
			{
				TSharedPtr<FFilteredCategoryNode>& Child = Current->Children.FindOrAdd(Seg);
				if (!Child.IsValid())
				{
					Child = MakeShareable(new FFilteredCategoryNode());
					Child->Name = Seg;
				}
				Current = Child;
			}
			Current->Nodes.Add(Entry);
			++TotalCompatible;
		}
	}

	// Recursive lambda to populate the filtered category tree into menus.
	// Uses TSharedRef to avoid dangling reference when Slate opens submenus lazily.
	using FFilteredPopulateFn = TFunction<void(FMenuBuilder&, TSharedPtr<FFilteredCategoryNode>)>;
	TSharedRef<FFilteredPopulateFn> PopulateFilteredPtr =
		MakeShareable(new FFilteredPopulateFn());

	*PopulateFilteredPtr = [this, GraphPos, SourceNodeId, SourcePinIndex, bFromOutput, PopulateFilteredPtr]
		(FMenuBuilder& Menu, TSharedPtr<FFilteredCategoryNode> TreeNode)
	{
		// Sort children alphabetically
		TArray<FString> ChildKeys;
		TreeNode->Children.GetKeys(ChildKeys);
		ChildKeys.Sort();

		for (const FString& Key : ChildKeys)
		{
			TSharedPtr<FFilteredCategoryNode> Child = TreeNode->Children[Key];

			Menu.AddSubMenu(
				FText::FromString(Key),
				FText::GetEmpty(),
				FNewMenuDelegate::CreateLambda(
					[this, Child, GraphPos, SourceNodeId, SourcePinIndex, bFromOutput, PopulateFilteredPtr]
					(FMenuBuilder& SubMenu)
				{
					// Add direct nodes at this level
					for (const FCompatibleEntry& E : Child->Nodes)
					{
						FString ClassType = E.Def->ClassType;
						FString MatchedPin = E.MatchedPinName;
						int32 MatchedIdx = E.MatchedPinIndex;

						SubMenu.AddMenuEntry(
							FText::FromString(E.Def->DisplayName),
							FText::FromString(ClassType),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda(
								[this, ClassType, GraphPos, SourceNodeId, SourcePinIndex,
								 bFromOutput, MatchedPin, MatchedIdx]()
							{
								FString NewNodeId = AddNodeByType(ClassType, GraphPos);
								if (NewNodeId.IsEmpty()) return;

								if (bFromOutput)
								{
									AddConnection(SourceNodeId, SourcePinIndex, NewNodeId, MatchedPin);
								}
								else
								{
									const int32* SrcIdx = NodeIndexMap.Find(SourceNodeId);
									if (SrcIdx && SourcePinIndex < Nodes[*SrcIdx].InputPins.Num())
									{
										FString InputName = Nodes[*SrcIdx].InputPins[SourcePinIndex].Name;
										AddConnection(NewNodeId, MatchedIdx, SourceNodeId, InputName);
									}
								}
							}))
						);
					}

					// Then recurse into child categories
					(*PopulateFilteredPtr)(SubMenu, Child);
				})
			);
		}

		// Add any direct nodes at this tree level (uncategorized at this depth)
		for (const FCompatibleEntry& E : TreeNode->Nodes)
		{
			FString ClassType = E.Def->ClassType;
			FString MatchedPin = E.MatchedPinName;
			int32 MatchedIdx = E.MatchedPinIndex;

			Menu.AddMenuEntry(
				FText::FromString(E.Def->DisplayName),
				FText::FromString(ClassType),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(
					[this, ClassType, GraphPos, SourceNodeId, SourcePinIndex,
					 bFromOutput, MatchedPin, MatchedIdx]()
				{
					FString NewNodeId = AddNodeByType(ClassType, GraphPos);
					if (NewNodeId.IsEmpty()) return;

					if (bFromOutput)
					{
						AddConnection(SourceNodeId, SourcePinIndex, NewNodeId, MatchedPin);
					}
					else
					{
						const int32* SrcIdx = NodeIndexMap.Find(SourceNodeId);
						if (SrcIdx && SourcePinIndex < Nodes[*SrcIdx].InputPins.Num())
						{
							FString InputName = Nodes[*SrcIdx].InputPins[SourcePinIndex].Name;
							AddConnection(NewNodeId, MatchedIdx, SourceNodeId, InputName);
						}
					}
				}))
			);
		}
	};

	MenuBuilder.BeginSection("ComfyNodes", FText::FromString(TEXT("Compatible ComfyUI Nodes")));
	(*PopulateFilteredPtr)(MenuBuilder, FilteredRoot);
	MenuBuilder.EndSection();

	if (TotalCompatible == 0 && !bHasUEEntries)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("No compatible nodes for type: %s"), *PinType)),
			FText::GetEmpty(), FSlateIcon(), FUIAction());
	}

	return MenuBuilder.MakeWidget();
}

// ============================================================================
// Workflow JSON Export
// ============================================================================

TSharedPtr<FJsonObject> SWorkflowGraphEditor::ExportWorkflowJSON(bool* OutNeedsViewport,
	bool* OutNeedsDepth, FString* OutCameraDescription, bool* OutNeedsSegmentation) const
{
	if (OutNeedsViewport) *OutNeedsViewport = false;
	if (OutNeedsDepth) *OutNeedsDepth = false;
	if (OutCameraDescription) OutCameraDescription->Empty();
	if (OutNeedsSegmentation) *OutNeedsSegmentation = false;

	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	// Bridge bypass map: when an Image Bridge node is exported, downstream nodes that
	// referenced the bridge as their source need to be rewritten to point at the bridge's
	// upstream source instead. Key = bridge node ID, Value = (upstream source ID, output pin).
	TMap<FString, TPair<FString, int32>> BridgeBypassMap;

	// First pass: find UE Prompt Adherence node (operates globally, no connections needed).
	// If present, it overrides CFG/steps/denoise on all KSampler nodes in the exported JSON.
	float AdherenceOverride = -1.0f; // Negative = no override
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.ClassType == UEPromptAdherenceClassType)
		{
			const FString* Val = Node.WidgetValues.Find(TEXT("adherence"));
			if (Val)
			{
				AdherenceOverride = FCString::Atof(**Val);
				AdherenceOverride = FMath::Clamp(AdherenceOverride, 0.0f, 1.0f);
			}
			break; // Only one adherence node supported
		}
	}

	// First pass: find UE Camera Data node settings (operates globally, no connections needed).
	// If active, it injects camera info into CLIPTextEncode text fields at export time.
	FString CameraFormat = TEXT("Natural Language");
	FString CameraPosition = TEXT("append");
	FString CameraTarget = TEXT("positive only");
	bool bHasCameraNode = false;

	for (const FGraphNode& Node : Nodes)
	{
		if (Node.ClassType == UECameraDataClassType)
		{
			const FString* ActiveVal = Node.WidgetValues.Find(TEXT("active"));
			if (ActiveVal && *ActiveVal == TEXT("false"))
			{
				continue; // Camera data node is disabled
			}

			bHasCameraNode = true;
			const FString* Fmt = Node.WidgetValues.Find(TEXT("format"));
			if (Fmt) CameraFormat = *Fmt;
			const FString* Pos = Node.WidgetValues.Find(TEXT("position"));
			if (Pos) CameraPosition = *Pos;
			const FString* Tgt = Node.WidgetValues.Find(TEXT("target"));
			if (Tgt) CameraTarget = *Tgt;

			// Signal to caller that camera data is needed
			if (OutCameraDescription)
			{
				*OutCameraDescription = TEXT("__PENDING__");
			}
			break; // Only one camera data node supported
		}
	}

	// Identify which nodes are "positive" CLIP nodes (title contains "Positive" or is the first CLIP)
	// so we can selectively inject camera data
	TSet<FString> PositiveClipNodeIds;
	if (bHasCameraNode)
	{
		for (const FGraphNode& Node : Nodes)
		{
			if (Node.ClassType == TEXT("CLIPTextEncode"))
			{
				if (CameraTarget == TEXT("all prompts"))
				{
					PositiveClipNodeIds.Add(Node.Id);
				}
				else
				{
					// "positive only" — check title for hints
					FString LowerTitle = Node.Title.ToLower();
					if (LowerTitle.Contains(TEXT("positive")) || LowerTitle.Contains(TEXT("pos"))
						|| (!LowerTitle.Contains(TEXT("negative")) && !LowerTitle.Contains(TEXT("neg"))))
					{
						PositiveClipNodeIds.Add(Node.Id);
					}
				}
			}
		}
	}

	// Second pass: emit all nodes
	for (const FGraphNode& Node : Nodes)
	{
		// Skip reroute nodes — they are visual-only and get collapsed during export
		if (Node.IsReroute())
		{
			continue;
		}

		// Skip camera data nodes — they are resolved into target node text fields
		if (Node.ClassType == UECameraDataClassType)
		{
			continue;
		}

		// Skip prompt adherence nodes — they are resolved as a post-pass over KSampler nodes
		if (Node.ClassType == UEPromptAdherenceClassType)
		{
			continue;
		}

		// Skip Meshy Import nodes — they are UE-side action nodes handled post-generation
		if (Node.ClassType == UEMeshyImportClassType)
		{
			continue;
		}

		// Skip Sequence nodes — they are UE-side flow control handled by staged execution
		if (Node.ClassType == UESequenceClassType)
		{
			continue;
		}

		// UE Video to Image → emit as LoadImage with a marker filename.
		// The caller resolves the marker before submission by extracting
		// the video frame via FFmpeg and uploading the resulting PNG.
		if (Node.ClassType == UEVideoToImageClassType)
		{
			FString MarkerFilename = UEVideoFrameMarkerPrefix + Node.Id;

			TSharedPtr<FJsonObject> LoadObj = MakeShareable(new FJsonObject);
			LoadObj->SetStringField(TEXT("class_type"), TEXT("LoadImage"));

			TSharedPtr<FJsonObject> LoadMeta = MakeShareable(new FJsonObject);
			LoadMeta->SetStringField(TEXT("title"), Node.Title);
			LoadObj->SetObjectField(TEXT("_meta"), LoadMeta);

			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), MarkerFilename);
			LoadObj->SetObjectField(TEXT("inputs"), LoadInputs);

			Workflow->SetObjectField(Node.Id, LoadObj);
			continue;
		}

		// UE Image Bridge → emit as SaveImage (saves to disk as a side-effect)
		// and rewrite downstream connections to bypass the bridge, pointing directly
		// to the bridge's upstream source. This acts as a "tee": the image gets saved
		// AND flows through to the next node (e.g. Tripo) in a single workflow execution.
		if (Node.ClassType == UEImageBridgeClassType)
		{
			// Get the filename prefix from the node's widget
			FString Prefix = TEXT("ViewGen_Bridge");
			const FString* PrefixVal = Node.WidgetValues.Find(TEXT("filename_prefix"));
			if (PrefixVal && !PrefixVal->IsEmpty()) Prefix = *PrefixVal;

			// Find the upstream source node that feeds the bridge's IMAGE input
			FString UpstreamSourceId;
			int32 UpstreamOutputIndex = 0;
			for (const FGraphConnection& Conn : Connections)
			{
				if (Conn.TargetNodeId == Node.Id && Conn.TargetInputName == TEXT("images"))
				{
					UpstreamSourceId = Conn.SourceNodeId;
					UpstreamOutputIndex = Conn.SourceOutputIndex;
					ResolveRerouteSource(Conn.SourceNodeId, UpstreamSourceId, UpstreamOutputIndex);
					break;
				}
			}

			// Emit SaveImage node (uses the bridge node's ID for the save side-effect)
			TSharedPtr<FJsonObject> SaveObj = MakeShareable(new FJsonObject);
			SaveObj->SetStringField(TEXT("class_type"), TEXT("SaveImage"));

			TSharedPtr<FJsonObject> SaveMeta = MakeShareable(new FJsonObject);
			SaveMeta->SetStringField(TEXT("title"), Node.Title + TEXT(" (Save)"));
			SaveMeta->SetStringField(TEXT("ue_image_bridge"), TEXT("true"));
			SaveObj->SetObjectField(TEXT("_meta"), SaveMeta);

			TSharedPtr<FJsonObject> SaveInputs = MakeShareable(new FJsonObject);
			SaveInputs->SetStringField(TEXT("filename_prefix"), Prefix);

			if (!UpstreamSourceId.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> LinkArray;
				LinkArray.Add(MakeShareable(new FJsonValueString(UpstreamSourceId)));
				LinkArray.Add(MakeShareable(new FJsonValueNumber(UpstreamOutputIndex)));
				SaveInputs->SetField(TEXT("images"), MakeShareable(new FJsonValueArray(LinkArray)));
			}
			SaveObj->SetObjectField(TEXT("inputs"), SaveInputs);
			Workflow->SetObjectField(Node.Id, SaveObj);

			// Store the bypass mapping: any downstream node that references this bridge
			// as a source should instead reference the bridge's upstream source.
			// We'll apply this in a post-pass after all nodes are emitted.
			if (!UpstreamSourceId.IsEmpty())
			{
				BridgeBypassMap.Add(Node.Id, TPair<FString, int32>(UpstreamSourceId, UpstreamOutputIndex));
			}

			UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Image Bridge '%s' → SaveImage (prefix='%s'), downstream bypasses to node '%s' pin %d"),
				*Node.Id, *Prefix, *UpstreamSourceId, UpstreamOutputIndex);

			continue;
		}

		// Replace Save 3D Model nodes with a proxy output node to capture the model file path.
		// UE Save 3D Model is a UE-only node, but its upstream ComfyUI node (e.g. Tripo)
		// may NOT be a ComfyUI OUTPUT_NODE. Without a consumer, ComfyUI won't execute it
		// and its results won't appear in the history API. The proxy ensures execution and
		// captures the model_file path so we can download it after generation.
		if (Node.ClassType == UESave3DModelClassType)
		{
			// Find the mesh input connection to determine which upstream node provides the 3D data
			for (const FGraphConnection& Conn : Connections)
			{
				if (Conn.TargetNodeId == Node.Id && Conn.TargetInputName == TEXT("mesh"))
				{
					FString RealSourceId = Conn.SourceNodeId;
					int32 RealOutputIndex = Conn.SourceOutputIndex;
					ResolveRerouteSource(Conn.SourceNodeId, RealSourceId, RealOutputIndex);

					// Inject a proxy OUTPUT_NODE to ensure the upstream 3D node executes and
					// its result is saved to disk / captured in the /history API.
					// Priority: SaveGLB (built-in, saves GLB to disk) > Save3DModel (built-in) > ShowAny_UTK (text capture fallback)
					// We look up the first link-type input name from the database to avoid hardcoding.
					FString ProxyClassType;
					FString ProxyInputName;
					bool bProxySavesToDisk = false;

					// Candidate proxy nodes in priority order — prefer nodes that save to disk.
					// TripoGLBViewer is from the Tripo plugin and accepts FILE_3D directly.
					// SaveGLB/Save3DModel are built-in ComfyUI nodes that save meshes to disk.
					// ShowAny_UTK is a text-capture fallback.
					struct FProxyCandidate { const TCHAR* ClassType; bool bSavesToDisk; };
					const FProxyCandidate Candidates[] = {
						{ TEXT("TripoGLBViewer"), true },
						{ TEXT("SaveGLB"), true },
						{ TEXT("Save3DModel"), true },
						{ TEXT("ShowAny_UTK"), false },
						{ TEXT("ShowText|pysssss"), false }
					};

					for (const FProxyCandidate& Candidate : Candidates)
					{
						const FComfyNodeDef* ProxyDef = FComfyNodeDatabase::Get().FindNode(Candidate.ClassType);
						if (!ProxyDef) continue;

						// Find the first link-type input — that's the pin we connect the upstream node to
						TArray<const FComfyInputDef*> LinkInputs = ProxyDef->GetLinkInputs();
						if (LinkInputs.Num() > 0)
						{
							ProxyClassType = Candidate.ClassType;
							ProxyInputName = LinkInputs[0]->Name;
							bProxySavesToDisk = Candidate.bSavesToDisk;

							UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Selected '%s' as proxy (input='%s', saves_to_disk=%s)"),
								Candidate.ClassType, *ProxyInputName, bProxySavesToDisk ? TEXT("yes") : TEXT("no"));
							break;
						}
					}

					if (!ProxyClassType.IsEmpty())
					{
						// Use a high numeric ID to avoid collisions
						int32 MaxId = 90000;
						for (const FGraphNode& N : Nodes)
						{
							int32 NId = FCString::Atoi(*N.Id);
							if (NId >= MaxId) MaxId = NId + 1;
						}
						FString ProxyId = FString::FromInt(MaxId);

						TSharedPtr<FJsonObject> ProxyObj = MakeShareable(new FJsonObject);
						ProxyObj->SetStringField(TEXT("class_type"), ProxyClassType);

						TSharedPtr<FJsonObject> ProxyMeta = MakeShareable(new FJsonObject);
						ProxyMeta->SetStringField(TEXT("title"), TEXT("ViewGen 3D Model Capture"));
						ProxyMeta->SetStringField(TEXT("ue_3d_model_proxy"), TEXT("true"));
						ProxyObj->SetObjectField(TEXT("_meta"), ProxyMeta);

						TSharedPtr<FJsonObject> ProxyInputs = MakeShareable(new FJsonObject);

						// Wire the upstream 3D node's output to the proxy's input
						TArray<TSharedPtr<FJsonValue>> LinkArray;
						LinkArray.Add(MakeShareable(new FJsonValueString(RealSourceId)));
						LinkArray.Add(MakeShareable(new FJsonValueNumber(RealOutputIndex)));
						ProxyInputs->SetField(ProxyInputName, MakeShareable(new FJsonValueArray(LinkArray)));

						// SaveGLB / Save3DModel: also set a filename prefix so we can find the file
						if (bProxySavesToDisk)
						{
							ProxyInputs->SetStringField(TEXT("filename_prefix"), TEXT("mesh/ViewGen_Model"));
						}

						ProxyObj->SetObjectField(TEXT("inputs"), ProxyInputs);
						Workflow->SetObjectField(ProxyId, ProxyObj);

						UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Injected '%s' proxy (id=%s, saves_to_disk=%s) to capture model output from node '%s' pin %d"),
							*ProxyClassType, *ProxyId, bProxySavesToDisk ? TEXT("yes") : TEXT("no"), *RealSourceId, RealOutputIndex);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("ViewGen Export: No SaveGLB, Save3DModel, or ShowAny_UTK node available in ComfyUI to capture 3D model output. "
							"The 3D model may not be downloadable."));
					}
					break;
				}
			}
			continue;
		}

		// UE 3D Asset Export — same proxy injection approach as Save 3D Model.
		// The node is UE-side only; we inject a proxy to capture the upstream 3D output.
		if (Node.ClassType == UE3DAssetExportClassType)
		{
			for (const FGraphConnection& Conn : Connections)
			{
				if (Conn.TargetNodeId == Node.Id && Conn.TargetInputName == TEXT("mesh"))
				{
					FString RealSourceId = Conn.SourceNodeId;
					int32 RealOutputIndex = Conn.SourceOutputIndex;
					ResolveRerouteSource(Conn.SourceNodeId, RealSourceId, RealOutputIndex);

					// Reuse the same proxy-selection strategy as Save 3D Model
					FString ProxyClassType2;
					FString ProxyInputName2;
					bool bProxySavesToDisk2 = false;

					struct FProxyCandidate2 { const TCHAR* ClassType; bool bSavesToDisk; };
					const FProxyCandidate2 Candidates2[] = {
						{ TEXT("TripoGLBViewer"), true },
						{ TEXT("SaveGLB"), true },
						{ TEXT("Save3DModel"), true },
						{ TEXT("ShowAny_UTK"), false },
						{ TEXT("ShowText|pysssss"), false }
					};

					for (const FProxyCandidate2& Candidate : Candidates2)
					{
						const FComfyNodeDef* ProxyDef = FComfyNodeDatabase::Get().FindNode(Candidate.ClassType);
						if (!ProxyDef) continue;
						TArray<const FComfyInputDef*> LinkInputs = ProxyDef->GetLinkInputs();
						if (LinkInputs.Num() > 0)
						{
							ProxyClassType2 = Candidate.ClassType;
							ProxyInputName2 = LinkInputs[0]->Name;
							bProxySavesToDisk2 = Candidate.bSavesToDisk;
							break;
						}
					}

					if (!ProxyClassType2.IsEmpty())
					{
						int32 MaxId2 = 90100;
						for (const FGraphNode& N : Nodes)
						{
							int32 NId = FCString::Atoi(*N.Id);
							if (NId >= MaxId2) MaxId2 = NId + 1;
						}
						FString ProxyId2 = FString::FromInt(MaxId2);

						TSharedPtr<FJsonObject> ProxyObj2 = MakeShareable(new FJsonObject);
						ProxyObj2->SetStringField(TEXT("class_type"), ProxyClassType2);

						TSharedPtr<FJsonObject> ProxyMeta2 = MakeShareable(new FJsonObject);
						ProxyMeta2->SetStringField(TEXT("title"), TEXT("ViewGen 3D Asset Export Capture"));
						ProxyMeta2->SetStringField(TEXT("ue_3d_asset_export_proxy"), TEXT("true"));
						ProxyObj2->SetObjectField(TEXT("_meta"), ProxyMeta2);

						TSharedPtr<FJsonObject> ProxyInputs2 = MakeShareable(new FJsonObject);
						TArray<TSharedPtr<FJsonValue>> LinkArray2;
						LinkArray2.Add(MakeShareable(new FJsonValueString(RealSourceId)));
						LinkArray2.Add(MakeShareable(new FJsonValueNumber(RealOutputIndex)));
						ProxyInputs2->SetField(ProxyInputName2, MakeShareable(new FJsonValueArray(LinkArray2)));
						if (bProxySavesToDisk2)
						{
							ProxyInputs2->SetStringField(TEXT("filename_prefix"), TEXT("mesh/ViewGen_Export"));
						}
						ProxyObj2->SetObjectField(TEXT("inputs"), ProxyInputs2);
						Workflow->SetObjectField(ProxyId2, ProxyObj2);

						UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Injected '%s' proxy (id=%s) for 3D Asset Export from node '%s' pin %d"),
							*ProxyClassType2, *ProxyId2, *RealSourceId, RealOutputIndex);
					}
					break;
				}
			}
			continue;
		}

		// UE Image Upres → emit as ImageScaleBy (upscale) using the user's chosen
		// interpolation method and scale factor. The 8→16 bit conversion and EXR
		// export are handled UE-side in SViewGenPanel::ExecuteImageUpresExport()
		// after the generation completes — no third-party ComfyUI nodes needed.
		if (Node.ClassType == UEImageUpresClassType)
		{
			// Read widget values
			FString UpscaleMethod = TEXT("lanczos");
			float ScaleFactor = 2.0f;
			const FString* MethodVal = Node.WidgetValues.Find(TEXT("upscale_method"));
			if (MethodVal) UpscaleMethod = *MethodVal;
			const FString* ScaleVal = Node.WidgetValues.Find(TEXT("scale_factor"));
			if (ScaleVal) ScaleFactor = FCString::Atof(**ScaleVal);

			// Find upstream image source
			FString UpstreamSourceId;
			int32 UpstreamOutputIndex = 0;
			for (const FGraphConnection& Conn : Connections)
			{
				if (Conn.TargetNodeId == Node.Id && Conn.TargetInputName == TEXT("image"))
				{
					UpstreamSourceId = Conn.SourceNodeId;
					UpstreamOutputIndex = Conn.SourceOutputIndex;
					ResolveRerouteSource(Conn.SourceNodeId, UpstreamSourceId, UpstreamOutputIndex);
					break;
				}
			}

			// Emit ImageScaleBy node using the UE node's ID so downstream connections resolve
			TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
			ScaleObj->SetStringField(TEXT("class_type"), TEXT("ImageScaleBy"));

			TSharedPtr<FJsonObject> ScaleMeta = MakeShareable(new FJsonObject);
			ScaleMeta->SetStringField(TEXT("title"), Node.Title);
			ScaleObj->SetObjectField(TEXT("_meta"), ScaleMeta);

			TSharedPtr<FJsonObject> ScaleInputs = MakeShareable(new FJsonObject);
			ScaleInputs->SetStringField(TEXT("upscale_method"), UpscaleMethod);
			ScaleInputs->SetNumberField(TEXT("scale_by"), ScaleFactor);

			if (!UpstreamSourceId.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> LinkArray;
				LinkArray.Add(MakeShareable(new FJsonValueString(UpstreamSourceId)));
				LinkArray.Add(MakeShareable(new FJsonValueNumber(UpstreamOutputIndex)));
				ScaleInputs->SetField(TEXT("image"), MakeShareable(new FJsonValueArray(LinkArray)));
			}
			ScaleObj->SetObjectField(TEXT("inputs"), ScaleInputs);
			Workflow->SetObjectField(Node.Id, ScaleObj);

			// Emit a dedicated SaveImage node after the scaler so the upscaled
			// result always appears in ComfyUI's history outputs with a known
			// filename prefix.  OnHistoryResponseReceived will prefer this output
			// over any other SaveImage in the workflow, ensuring that the image
			// fetched back into UE is the upscaled version (not a pre-upscale
			// output from e.g. an Image Bridge node).
			FString UpresSaveId = Node.Id + TEXT("_upres_save");
			TSharedPtr<FJsonObject> UpresSaveObj = MakeShareable(new FJsonObject);
			UpresSaveObj->SetStringField(TEXT("class_type"), TEXT("SaveImage"));

			TSharedPtr<FJsonObject> UpresSaveMeta = MakeShareable(new FJsonObject);
			UpresSaveMeta->SetStringField(TEXT("title"), Node.Title + TEXT(" (Upres Save)"));
			UpresSaveObj->SetObjectField(TEXT("_meta"), UpresSaveMeta);

			TSharedPtr<FJsonObject> UpresSaveInputs = MakeShareable(new FJsonObject);
			UpresSaveInputs->SetStringField(TEXT("filename_prefix"), TEXT("__UE_Upres_Export__"));

			// Wire the SaveImage input to the ImageScaleBy output (pin 0)
			{
				TArray<TSharedPtr<FJsonValue>> SaveLinkArray;
				SaveLinkArray.Add(MakeShareable(new FJsonValueString(Node.Id)));
				SaveLinkArray.Add(MakeShareable(new FJsonValueNumber(0)));
				UpresSaveInputs->SetField(TEXT("images"), MakeShareable(new FJsonValueArray(SaveLinkArray)));
			}
			UpresSaveObj->SetObjectField(TEXT("inputs"), UpresSaveInputs);
			Workflow->SetObjectField(UpresSaveId, UpresSaveObj);

			UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Image Upres '%s' → ImageScaleBy (method=%s, scale=%.1f) "
				"+ SaveImage '%s'. Bit-depth conversion and EXR export handled UE-side post-generation."),
				*Node.Id, *UpscaleMethod, ScaleFactor, *UpresSaveId);

			continue;
		}

		// Skip 3D Loader nodes — they are UE-side only (load from local disk, not ComfyUI)
		if (Node.ClassType == UE3DLoaderClassType)
		{
			continue;
		}

		// Skip MeshyImageToModelNode — the UE plugin handles the Meshy API call directly
		// using the user's own Meshy API key rather than ComfyUI's partner credentials
		if (Node.ClassType == TEXT("MeshyImageToModelNode"))
		{
			continue;
		}

		// UE Viewport Capture -> emit as LoadImage with marker filename
		if (Node.ClassType == UEViewportClassType)
		{
			if (OutNeedsViewport) *OutNeedsViewport = true;

			TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
			NodeObj->SetStringField(TEXT("class_type"), TEXT("LoadImage"));

			TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
			MetaObj->SetStringField(TEXT("title"), Node.Title);
			NodeObj->SetObjectField(TEXT("_meta"), MetaObj);

			TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);
			InputsObj->SetStringField(TEXT("image"), UEViewportMarker);

			// Store the resolution multiplier in meta so the panel can read it
			const FString* ResVal = Node.WidgetValues.Find(TEXT("resolution"));
			if (ResVal) MetaObj->SetStringField(TEXT("ue_resolution"), *ResVal);

			NodeObj->SetObjectField(TEXT("inputs"), InputsObj);
			Workflow->SetObjectField(Node.Id, NodeObj);
			continue;
		}

		// UE Depth Map -> emit as LoadImage with marker filename
		if (Node.ClassType == UEDepthMapClassType)
		{
			if (OutNeedsDepth) *OutNeedsDepth = true;

			TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
			NodeObj->SetStringField(TEXT("class_type"), TEXT("LoadImage"));

			TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
			MetaObj->SetStringField(TEXT("title"), Node.Title);
			NodeObj->SetObjectField(TEXT("_meta"), MetaObj);

			TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);
			InputsObj->SetStringField(TEXT("image"), UEDepthMapMarker);

			// Store max_depth in meta so the panel can use it
			const FString* DepthVal = Node.WidgetValues.Find(TEXT("max_depth"));
			if (DepthVal) MetaObj->SetStringField(TEXT("ue_max_depth"), *DepthVal);

			NodeObj->SetObjectField(TEXT("inputs"), InputsObj);
			Workflow->SetObjectField(Node.Id, NodeObj);
			continue;
		}

		// UE Segmentation Mask -> emit as LoadImage with marker filename
		if (Node.ClassType == UESegmentationClassType)
		{
			if (OutNeedsSegmentation) *OutNeedsSegmentation = true;

			TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
			NodeObj->SetStringField(TEXT("class_type"), TEXT("LoadImage"));

			TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
			MetaObj->SetStringField(TEXT("title"), Node.Title);
			NodeObj->SetObjectField(TEXT("_meta"), MetaObj);

			TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);
			InputsObj->SetStringField(TEXT("image"), UESegmentationMarker);

			// Store capture mode in meta
			const FString* ModeVal = Node.WidgetValues.Find(TEXT("mode"));
			if (ModeVal) MetaObj->SetStringField(TEXT("ue_segmentation_mode"), *ModeVal);

			NodeObj->SetObjectField(TEXT("inputs"), InputsObj);
			Workflow->SetObjectField(Node.Id, NodeObj);
			continue;
		}

		// Regular node
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("class_type"), Node.ClassType);

		// Meta
		TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
		MetaObj->SetStringField(TEXT("title"), Node.Title);
		NodeObj->SetObjectField(TEXT("_meta"), MetaObj);

		// Inputs
		TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);

		// Mark CLIPTextEncode nodes that should receive camera data injection
		if (bHasCameraNode && PositiveClipNodeIds.Contains(Node.Id)
			&& Node.ClassType == TEXT("CLIPTextEncode"))
		{
			MetaObj->SetStringField(TEXT("ue_camera_target_field"), TEXT("text"));
			MetaObj->SetStringField(TEXT("ue_camera_format"), CameraFormat);
			MetaObj->SetStringField(TEXT("ue_camera_position"), CameraPosition);
		}

		// Widget values (literal values)
		for (const auto& Widget : Node.WidgetValues)
		{
			FString Value = Widget.Value;

			// Try to detect numeric values
			if (Value.IsNumeric())
			{
				double NumVal = FCString::Atod(*Value);
				// Check if it's an integer
				if (Value.Contains(TEXT(".")))
				{
					InputsObj->SetNumberField(Widget.Key, NumVal);
				}
				else
				{
					InputsObj->SetNumberField(Widget.Key, (int64)NumVal);
				}
			}
			else if (Value == TEXT("true") || Value == TEXT("false"))
			{
				InputsObj->SetBoolField(Widget.Key, Value == TEXT("true"));
			}
			else
			{
				InputsObj->SetStringField(Widget.Key, Value);
			}
		}

		// Hardcoded fixup for MeshyImageToModelNode V3 conditional inputs.
		// should_remesh and should_texture are COMFY_DYNAMICCOMBO_V3 types whose
		// conditional sub-fields use DOT NOTATION keys: "should_remesh.target_polycount"
		if (Node.ClassType == TEXT("MeshyImageToModelNode"))
		{
			// should_remesh + conditional sub-fields (dot-notation keys)
			{
				FString RemeshVal = TEXT("true");
				const FString* UserVal = Node.WidgetValues.Find(TEXT("should_remesh"));
				if (UserVal && !UserVal->IsEmpty()) RemeshVal = *UserVal;
				InputsObj->SetStringField(TEXT("should_remesh"), RemeshVal);

				if (RemeshVal == TEXT("true"))
				{
					if (!InputsObj->HasField(TEXT("should_remesh.target_polycount")))
					{
						const FString* V = Node.WidgetValues.Find(TEXT("target_polycount"));
						if (!V) V = Node.WidgetValues.Find(TEXT("should_remesh.target_polycount"));
						InputsObj->SetNumberField(TEXT("should_remesh.target_polycount"), (V && !V->IsEmpty()) ? FCString::Atoi(**V) : 30000);
					}
					if (!InputsObj->HasField(TEXT("should_remesh.topology")))
					{
						const FString* V = Node.WidgetValues.Find(TEXT("topology"));
						if (!V) V = Node.WidgetValues.Find(TEXT("should_remesh.topology"));
						InputsObj->SetStringField(TEXT("should_remesh.topology"), (V && !V->IsEmpty()) ? *V : TEXT("triangle"));
					}
				}
			}
			// should_texture + conditional sub-fields (dot-notation keys)
			{
				FString TextureVal = TEXT("true");
				const FString* UserVal = Node.WidgetValues.Find(TEXT("should_texture"));
				if (UserVal && !UserVal->IsEmpty()) TextureVal = *UserVal;
				InputsObj->SetStringField(TEXT("should_texture"), TextureVal);

				if (TextureVal == TEXT("true"))
				{
					if (!InputsObj->HasField(TEXT("should_texture.enable_pbr")))
					{
						const FString* V = Node.WidgetValues.Find(TEXT("enable_pbr"));
						if (!V) V = Node.WidgetValues.Find(TEXT("should_texture.enable_pbr"));
						InputsObj->SetStringField(TEXT("should_texture.enable_pbr"), (V && !V->IsEmpty()) ? *V : TEXT("false"));
					}
					if (!InputsObj->HasField(TEXT("should_texture.texture_prompt")))
					{
						const FString* V = Node.WidgetValues.Find(TEXT("texture_prompt"));
						if (!V) V = Node.WidgetValues.Find(TEXT("should_texture.texture_prompt"));
						InputsObj->SetStringField(TEXT("should_texture.texture_prompt"), (V && !V->IsEmpty()) ? *V : TEXT(""));
					}
				}
			}
			UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Ensured all MeshyImageToModelNode V3 conditional inputs (id=%s)"), *Node.Id);
		}

		// Safety net #1: check the node's own input pins for V3 dynamic combo types
		// that were incorrectly classified as link-type pins. These need to be emitted
		// as widget values even though they're stored as pins on the node.
		// This works even when the node database hasn't been fetched yet.
		for (const FGraphPin& Pin : Node.InputPins)
		{
			if (Pin.Type.StartsWith(TEXT("COMFY_")) && !InputsObj->HasField(Pin.Name))
			{
				// Check if this pin has a connection — if so, it's being fed by another node
				bool bHasConnection = false;
				for (const FGraphConnection& Conn : Connections)
				{
					if (Conn.TargetNodeId == Node.Id && Conn.TargetInputName == Pin.Name)
					{
						bHasConnection = true;
						break;
					}
				}

				if (!bHasConnection)
				{
					// Unconnected V3 pin with no widget value — emit a default
					// Look for a widget value first (in case migration partially ran)
					const FString* WidgetVal = Node.WidgetValues.Find(Pin.Name);
					if (WidgetVal && !WidgetVal->IsEmpty())
					{
						if (*WidgetVal == TEXT("true") || *WidgetVal == TEXT("false"))
							InputsObj->SetBoolField(Pin.Name, *WidgetVal == TEXT("true"));
						else
							InputsObj->SetStringField(Pin.Name, *WidgetVal);
					}
					else
					{
						// No value at all — default to true for boolean-like V3 combos
						InputsObj->SetBoolField(Pin.Name, true);
						UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Emitting default 'true' for missing V3 input '%s' on node '%s'"),
							*Pin.Name, *Node.ClassType);
					}
				}
			}
		}

		// Safety net #2: check for required widget inputs from the node DB that are missing
		// from this node's WidgetValues (covers cases beyond just V3 pins).
		const FComfyNodeDef* ExportDef = FComfyNodeDatabase::Get().FindNode(Node.ClassType);
		if (ExportDef)
		{
			for (const FComfyInputDef& DefInput : ExportDef->Inputs)
			{
				if (!DefInput.IsLinkType() && !Node.WidgetValues.Contains(DefInput.Name))
				{
					// This widget input exists in the DB but is missing from the node.
					// Check if it's also absent from the InputsObj (not already set as a link or V3 pin).
					if (!InputsObj->HasField(DefInput.Name))
					{
						// Emit a default value
						if (DefInput.Type == TEXT("COMBO") && DefInput.ComboOptions.Num() > 0)
						{
							FString DefVal = !DefInput.DefaultString.IsEmpty() ? DefInput.DefaultString : DefInput.ComboOptions[0];
							if (DefVal == TEXT("true") || DefVal == TEXT("false"))
								InputsObj->SetBoolField(DefInput.Name, DefVal == TEXT("true"));
							else
								InputsObj->SetStringField(DefInput.Name, DefVal);
						}
						else if (DefInput.Type == TEXT("COMBO"))
						{
							// V3 combo with no known options — default to true
							InputsObj->SetBoolField(DefInput.Name, true);
						}
						else if (DefInput.Type == TEXT("BOOLEAN") || DefInput.Type == TEXT("BOOL"))
						{
							InputsObj->SetBoolField(DefInput.Name, DefInput.DefaultBool);
						}
						else if (DefInput.Type == TEXT("INT"))
						{
							InputsObj->SetNumberField(DefInput.Name, (int64)DefInput.DefaultNumber);
						}
						else if (DefInput.Type == TEXT("FLOAT"))
						{
							InputsObj->SetNumberField(DefInput.Name, DefInput.DefaultNumber);
						}
						else if (DefInput.Type == TEXT("STRING"))
						{
							InputsObj->SetStringField(DefInput.Name, DefInput.DefaultString);
						}

						UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Emitting DB default for missing widget '%s' on node '%s'"),
							*DefInput.Name, *Node.ClassType);
					}
				}
			}
		}

		// Link inputs (from connections), resolving through reroute nodes
		for (const FGraphConnection& Conn : Connections)
		{
			if (Conn.TargetNodeId == Node.Id)
			{
				// Resolve through any reroute chain to find the real source
				FString RealSourceId = Conn.SourceNodeId;
				int32 RealOutputIndex = Conn.SourceOutputIndex;
				ResolveRerouteSource(Conn.SourceNodeId, RealSourceId, RealOutputIndex);



				// ComfyUI link format: [source_node_id, source_output_index]
				TArray<TSharedPtr<FJsonValue>> LinkArray;
				LinkArray.Add(MakeShareable(new FJsonValueString(RealSourceId)));
				LinkArray.Add(MakeShareable(new FJsonValueNumber(RealOutputIndex)));
				InputsObj->SetField(Conn.TargetInputName, MakeShareable(new FJsonValueArray(LinkArray)));
			}
		}

		NodeObj->SetObjectField(TEXT("inputs"), InputsObj);
		Workflow->SetObjectField(Node.Id, NodeObj);
	}

	// Post-pass: rewrite downstream links that reference Image Bridge nodes.
	// After the bridge is emitted as SaveImage, any node that was connected to
	// the bridge's output needs to point to the bridge's upstream source instead.
	if (BridgeBypassMap.Num() > 0)
	{
		for (auto& WfPair : Workflow->Values)
		{
			const TSharedPtr<FJsonObject>* NodeObj;
			if (!WfPair.Value->TryGetObject(NodeObj)) continue;

			const TSharedPtr<FJsonObject>* InputsObj;
			if (!(*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj)) continue;

			// Collect rewrites to avoid modifying the map while iterating
			TArray<TPair<FString, TSharedPtr<FJsonValue>>> Rewrites;

			for (const auto& InputPair : (*InputsObj)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* LinkArr;
				if (!InputPair.Value->TryGetArray(LinkArr)) continue;
				if (LinkArr->Num() < 2) continue;

				FString SourceId;
				(*LinkArr)[0]->TryGetString(SourceId);
				const TPair<FString, int32>* Bypass = BridgeBypassMap.Find(SourceId);
				if (Bypass)
				{
					TArray<TSharedPtr<FJsonValue>> NewLink;
					NewLink.Add(MakeShareable(new FJsonValueString(Bypass->Key)));
					NewLink.Add(MakeShareable(new FJsonValueNumber(Bypass->Value)));
					Rewrites.Add(TPair<FString, TSharedPtr<FJsonValue>>(InputPair.Key, MakeShareable(new FJsonValueArray(NewLink))));

					UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Bridge bypass — node '%s' input '%s': [%s, *] → [%s, %d]"),
						*WfPair.Key, *InputPair.Key, *SourceId, *Bypass->Key, Bypass->Value);
				}
			}

			for (const auto& Rewrite : Rewrites)
			{
				(*InputsObj)->SetField(Rewrite.Key, Rewrite.Value);
			}
		}
	}

	// Post-pass: apply UE Prompt Adherence overrides to all KSampler-type nodes.
	// Maps the 0.0–1.0 adherence value to CFG scale, steps, and denoise strength,
	// using the same ranges as the panel's ApplyAdherenceToSettings function.
	// This only affects the exported JSON — the global settings are NOT touched.
	if (AdherenceOverride >= 0.0f)
	{
		float Adh = AdherenceOverride;
		float OverrideCFG = FMath::Lerp(3.0f, 14.0f, Adh);
		int32 OverrideSteps = FMath::RoundToInt(FMath::Lerp(12.0f, 45.0f, Adh));
		float OverrideDenoise = FMath::Lerp(0.95f, 0.15f, Adh); // Inverted: low adherence = high denoise

		for (auto& WfPair : Workflow->Values)
		{
			const TSharedPtr<FJsonObject>* NodeObjPtr;
			if (!WfPair.Value->TryGetObject(NodeObjPtr)) continue;

			FString ClassType;
			if (!(*NodeObjPtr)->TryGetStringField(TEXT("class_type"), ClassType)) continue;

			// Apply to any KSampler variant (KSampler, KSamplerAdvanced, etc.)
			if (!ClassType.Contains(TEXT("KSampler"))) continue;

			const TSharedPtr<FJsonObject>* InputsObjPtr;
			if (!(*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObjPtr)) continue;

			// Override cfg, steps, and denoise if they exist as widget values (not links)
			if ((*InputsObjPtr)->HasField(TEXT("cfg")))
			{
				const TSharedPtr<FJsonValue>& CfgVal = (*InputsObjPtr)->Values.FindChecked(TEXT("cfg"));
				if (CfgVal->Type != EJson::Array) // Not a link
				{
					(*InputsObjPtr)->SetNumberField(TEXT("cfg"), OverrideCFG);
				}
			}
			if ((*InputsObjPtr)->HasField(TEXT("steps")))
			{
				const TSharedPtr<FJsonValue>& StepsVal = (*InputsObjPtr)->Values.FindChecked(TEXT("steps"));
				if (StepsVal->Type != EJson::Array)
				{
					(*InputsObjPtr)->SetNumberField(TEXT("steps"), (int64)OverrideSteps);
				}
			}
			if ((*InputsObjPtr)->HasField(TEXT("denoise")))
			{
				const TSharedPtr<FJsonValue>& DenoiseVal = (*InputsObjPtr)->Values.FindChecked(TEXT("denoise"));
				if (DenoiseVal->Type != EJson::Array)
				{
					(*InputsObjPtr)->SetNumberField(TEXT("denoise"), OverrideDenoise);
				}
			}

			UE_LOG(LogTemp, Log, TEXT("ViewGen Export: Prompt Adherence override (%.0f%%) → node '%s': cfg=%.1f, steps=%d, denoise=%.2f"),
				Adh * 100.0f, *WfPair.Key, OverrideCFG, OverrideSteps, OverrideDenoise);
		}
	}

	return Workflow;
}

// ============================================================================
// Workflow JSON Import
// ============================================================================

FString SWorkflowGraphEditor::GetMeshySourceImageFilename() const
{
	// Find the MeshyImageToModelNode
	const FGraphNode* MeshyNode = nullptr;
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.ClassType == TEXT("MeshyImageToModelNode"))
		{
			MeshyNode = &Node;
			break;
		}
	}
	if (!MeshyNode) return FString();

	// Trace the "image" input connection back to its source node
	FString SourceNodeId;
	for (const FGraphConnection& Conn : Connections)
	{
		if (Conn.TargetNodeId == MeshyNode->Id && Conn.TargetInputName == TEXT("image"))
		{
			SourceNodeId = Conn.SourceNodeId;
			break;
		}
	}
	if (SourceNodeId.IsEmpty()) return FString();

	// Find the source node and get its "image" widget value (LoadImage stores filename there)
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.Id == SourceNodeId)
		{
			// LoadImage node stores the filename in the "image" widget
			const FString* ImageVal = Node.WidgetValues.Find(TEXT("image"));
			if (ImageVal && !ImageVal->IsEmpty())
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: Meshy source image resolved to '%s' from node '%s'"),
					**ImageVal, *Node.ClassType);
				return *ImageVal;
			}

			// If it's a UE Viewport Capture, return a marker
			if (Node.ClassType == UEViewportClassType)
			{
				return UEViewportMarker;
			}

			break;
		}
	}

	return FString();
}

/**
 * Detect whether a JSON object is a ComfyUI Web UI export (contains "nodes" array
 * and "links" array) vs. the API format (flat dictionary of node_id -> {class_type, inputs}).
 * If it's Web UI format, convert it to API format so the import logic can handle both.
 */
static TSharedPtr<FJsonObject> ConvertWebUIToAPIFormat(TSharedPtr<FJsonObject> Root,
	TMap<FString, FVector2D>& OutPositions)
{
	// Web UI format detection: has "nodes" array and "links" array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	const TArray<TSharedPtr<FJsonValue>>* LinksArray;
	if (!Root->TryGetArrayField(TEXT("nodes"), NodesArray) ||
		!Root->TryGetArrayField(TEXT("links"), LinksArray))
	{
		return nullptr; // Not Web UI format
	}

	// Build a lookup: link_id -> {origin_id, origin_slot}
	struct FLinkInfo { int64 OriginNodeId; int32 OriginSlot; FString Type; };
	TMap<int64, FLinkInfo> LinkMap;
	for (const auto& LinkVal : *LinksArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* LinkArr;
		if (!LinkVal->TryGetArray(LinkArr) || LinkArr->Num() < 5) continue;

		int64 LinkId = static_cast<int64>((*LinkArr)[0]->AsNumber());
		FLinkInfo Info;
		Info.OriginNodeId = static_cast<int64>((*LinkArr)[1]->AsNumber());
		Info.OriginSlot = static_cast<int32>((*LinkArr)[2]->AsNumber());
		Info.Type = (*LinkArr)[4]->AsString();
		LinkMap.Add(LinkId, Info);
	}

	// Build a lookup: node_id -> outputs array (to resolve output slot index to name)
	// Also build the API-format JSON
	TSharedPtr<FJsonObject> APIRoot = MakeShareable(new FJsonObject);

	for (const auto& NodeVal : *NodesArray)
	{
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!NodeVal->TryGetObject(NodeObjPtr)) continue;
		TSharedPtr<FJsonObject> NodeObj = *NodeObjPtr;

		int64 NodeId = static_cast<int64>(NodeObj->GetNumberField(TEXT("id")));
		FString NodeIdStr = FString::Printf(TEXT("%lld"), NodeId);
		FString ClassType = NodeObj->GetStringField(TEXT("type"));

		// Skip special Web UI nodes that have no functional role in the workflow
		if (ClassType.StartsWith(TEXT("Reroute")) ||
			ClassType == TEXT("Note") ||
			ClassType == TEXT("MarkdownNote") ||
			ClassType == TEXT("PrimitiveNode") ||
			ClassType == TEXT("GroupNode"))
		{
			continue;
		}

		// Extract position
		const TArray<TSharedPtr<FJsonValue>>* PosArr;
		if (NodeObj->TryGetArrayField(TEXT("pos"), PosArr) && PosArr->Num() >= 2)
		{
			float X = static_cast<float>((*PosArr)[0]->AsNumber());
			float Y = static_cast<float>((*PosArr)[1]->AsNumber());
			OutPositions.Add(NodeIdStr, FVector2D(X, Y));
		}

		// Build the API-format node object
		TSharedPtr<FJsonObject> APINode = MakeShareable(new FJsonObject);
		APINode->SetStringField(TEXT("class_type"), ClassType);

		// _meta with title
		FString Title;
		if (NodeObj->TryGetStringField(TEXT("title"), Title) && !Title.IsEmpty())
		{
			TSharedPtr<FJsonObject> Meta = MakeShareable(new FJsonObject);
			Meta->SetStringField(TEXT("title"), Title);
			APINode->SetObjectField(TEXT("_meta"), Meta);
		}

		// Parse widget values from the "widgets_values" array and inputs from "inputs" array
		TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);

		// Widget values: these are positional, matching the node's input order.
		// We'll store them as-is and let the existing import logic match them.
		const TArray<TSharedPtr<FJsonValue>>* WidgetVals;
		if (NodeObj->TryGetArrayField(TEXT("widgets_values"), WidgetVals))
		{
			// Widget values are positional — we can't reliably map them to input names
			// without the node definition. Store them with indexed keys for now;
			// the import logic will override with proper names from the node def.
			// For nodes with known defs, we try to match by position later.
		}

		// Inputs array: each has "name", "type", and optionally "link" (link_id)
		const TArray<TSharedPtr<FJsonValue>>* InputsArr;
		if (NodeObj->TryGetArrayField(TEXT("inputs"), InputsArr))
		{
			for (const auto& InputVal : *InputsArr)
			{
				const TSharedPtr<FJsonObject>* InputObjPtr;
				if (!InputVal->TryGetObject(InputObjPtr)) continue;

				FString InputName = (*InputObjPtr)->GetStringField(TEXT("name"));
				int64 LinkId = 0;

				// "link" can be null (no connection) or a number (link_id)
				const TSharedPtr<FJsonValue>* LinkField = (*InputObjPtr)->Values.Find(TEXT("link"));
				if (LinkField && !(*LinkField)->IsNull())
				{
					LinkId = static_cast<int64>((*LinkField)->AsNumber());

					// Convert to API format: input_name -> [source_node_id_string, source_slot]
					FLinkInfo* Info = LinkMap.Find(LinkId);
					if (Info)
					{
						TArray<TSharedPtr<FJsonValue>> LinkRef;
						LinkRef.Add(MakeShareable(new FJsonValueString(
							FString::Printf(TEXT("%lld"), Info->OriginNodeId))));
						LinkRef.Add(MakeShareable(new FJsonValueNumber(Info->OriginSlot)));
						InputsObj->SetArrayField(InputName, LinkRef);
					}
				}
			}
		}

		// Widget values: map them to input names using the node definition.
		// ComfyUI's widgets_values array may contain frontend-only values
		// (e.g. "control_after_generate" after seed inputs) that don't exist
		// in the node definition from /object_info. We walk both arrays with
		// separate indices — advancing the widget value index for each match
		// and also for values that look like known frontend-only controls.
		if (WidgetVals && WidgetVals->Num() > 0)
		{
			const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();
			const FComfyNodeDef* Def = DB.FindNode(ClassType);
			if (Def)
			{
				// Collect widget (non-link) input definitions in order
				TArray<const FComfyInputDef*> WidgetDefs;
				for (const FComfyInputDef& InputDef : Def->Inputs)
				{
					if (!InputDef.IsLinkType())
					{
						WidgetDefs.Add(&InputDef);
					}
				}

				// Known frontend-only widget values that ComfyUI inserts
				// but don't appear in /object_info
				static const TSet<FString> FrontendOnlyValues = {
					TEXT("fixed"), TEXT("increment"), TEXT("decrement"),
					TEXT("randomize"), TEXT("random")
				};

				// Walk widgets_values, consuming def entries as we match
				int32 DefIdx = 0;
				for (int32 ValIdx = 0; ValIdx < WidgetVals->Num() && DefIdx < WidgetDefs.Num(); ValIdx++)
				{
					TSharedPtr<FJsonValue> Val = (*WidgetVals)[ValIdx];
					if (Val->IsNull()) continue;

					const FComfyInputDef* CurDef = WidgetDefs[DefIdx];

					// Check if this value is a frontend-only control
					// (typically follows a seed/noise_seed INT input)
					FString StrVal;
					if (Val->TryGetString(StrVal) && FrontendOnlyValues.Contains(StrVal.ToLower()))
					{
						// This is a control_after_generate or similar — skip it,
						// don't advance the def index
						continue;
					}

					// Type validation: check if the value is compatible with the expected input type
					bool bTypeMatch = true;
					double NumVal;
					if (CurDef->Type == TEXT("INT") || CurDef->Type == TEXT("FLOAT"))
					{
						// Should be a number
						if (!Val->TryGetNumber(NumVal))
						{
							// Value is a string but we expect a number — likely a frontend widget, skip
							if (Val->TryGetString(StrVal))
							{
								bTypeMatch = false;
							}
						}
					}

					if (!bTypeMatch)
					{
						// Value doesn't match expected type — skip this value (frontend-only)
						continue;
					}

					// Don't overwrite link-type inputs already set
					if (!InputsObj->HasField(CurDef->Name))
					{
						InputsObj->SetField(CurDef->Name, Val);
					}
					DefIdx++;
				}
			}
		}

		APINode->SetObjectField(TEXT("inputs"), InputsObj);

		// Preserve output pin info for unknown nodes (not in the database)
		const TArray<TSharedPtr<FJsonValue>>* OutputsArr;
		if (NodeObj->TryGetArrayField(TEXT("outputs"), OutputsArr))
		{
			TArray<TSharedPtr<FJsonValue>> OutputDefs;
			for (const auto& OutVal : *OutputsArr)
			{
				const TSharedPtr<FJsonObject>* OutObjPtr;
				if (!OutVal->TryGetObject(OutObjPtr)) continue;

				TSharedPtr<FJsonObject> OutDef = MakeShareable(new FJsonObject);
				OutDef->SetStringField(TEXT("name"), (*OutObjPtr)->GetStringField(TEXT("name")));
				OutDef->SetStringField(TEXT("type"), (*OutObjPtr)->GetStringField(TEXT("type")));
				OutputDefs.Add(MakeShareable(new FJsonValueObject(OutDef)));
			}
			if (OutputDefs.Num() > 0)
			{
				APINode->SetArrayField(TEXT("_outputs"), OutputDefs);
			}
		}

		APIRoot->SetObjectField(NodeIdStr, APINode);
	}

	return APIRoot;
}

void SWorkflowGraphEditor::ImportWorkflowJSON(TSharedPtr<FJsonObject> Workflow)
{
	if (!Workflow.IsValid()) return;

	// Auto-detect Web UI format and convert if needed
	TMap<FString, FVector2D> WebUIPositions;
	TSharedPtr<FJsonObject> ConvertedWorkflow = ConvertWebUIToAPIFormat(Workflow, WebUIPositions);
	bool bHasPositions = WebUIPositions.Num() > 0;
	if (ConvertedWorkflow.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Detected ComfyUI Web UI format — converting to API format (%d nodes)"),
			ConvertedWorkflow->Values.Num());
		Workflow = ConvertedWorkflow;
	}

	ClearGraph();

	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();

	// First pass: create all nodes
	for (const auto& Pair : Workflow->Values)
	{
		const FString& NodeId = Pair.Key;
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!Pair.Value->TryGetObject(NodeObjPtr)) continue;

		FString ClassType;
		if (!(*NodeObjPtr)->TryGetStringField(TEXT("class_type"), ClassType) || ClassType.IsEmpty())
		{
			continue;
		}
		FString Title = ClassType;

		// Check _meta for custom title
		const TSharedPtr<FJsonObject>* MetaObj;
		if ((*NodeObjPtr)->TryGetObjectField(TEXT("_meta"), MetaObj))
		{
			FString MetaTitle;
			if ((*MetaObj)->TryGetStringField(TEXT("title"), MetaTitle) && !MetaTitle.IsEmpty())
			{
				Title = MetaTitle;
			}
		}

		const FComfyNodeDef* Def = DB.FindNode(ClassType);
		FGraphNode Node;

		if (Def)
		{
			Node = CreateNodeFromDef(*Def, NodeId, FVector2D::ZeroVector);
			Node.Title = Title;
		}
		else
		{
			// Unknown node — create representation with whatever info we have
			Node.Id = NodeId;
			Node.ClassType = ClassType;
			Node.Title = Title;
			Node.HeaderColor = GetNodeColor(ClassType);
			Node.Size = FVector2D(GraphConstants::NodeMinWidth, GraphConstants::NodeHeaderHeight + 40.0f);

			// For unknown nodes, try to create pins from the API-format inputs.
			// Link-type inputs (arrays) become input pins; scalar values become widgets.
			const TSharedPtr<FJsonObject>* UnkInputsObj;
			if ((*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), UnkInputsObj))
			{
				for (const auto& InputPair : (*UnkInputsObj)->Values)
				{
					const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
					if (InputPair.Value->TryGetArray(ArrayVal))
					{
						// Link input — create an input pin
						FGraphPin Pin;
						Pin.Name = InputPair.Key;
						Pin.Type = TEXT("*"); // Wildcard type
						Pin.bIsInput = true;
						Pin.OwnerNodeId = NodeId;
						Node.InputPins.Add(Pin);
					}
				}
			}

			// Create output pins from _outputs (preserved during Web UI conversion)
			const TArray<TSharedPtr<FJsonValue>>* OutputDefs;
			if ((*NodeObjPtr)->TryGetArrayField(TEXT("_outputs"), OutputDefs))
			{
				for (const auto& OutVal : *OutputDefs)
				{
					const TSharedPtr<FJsonObject>* OutObjPtr;
					if (!OutVal->TryGetObject(OutObjPtr)) continue;

					FGraphPin Pin;
					Pin.Name = (*OutObjPtr)->GetStringField(TEXT("name"));
					Pin.Type = (*OutObjPtr)->GetStringField(TEXT("type"));
					Pin.bIsInput = false;
					Pin.OwnerNodeId = NodeId;
					Node.OutputPins.Add(Pin);
				}
			}
		}

		// Parse widget values from inputs
		const TSharedPtr<FJsonObject>* InputsObj;
		if ((*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObj))
		{
			for (const auto& InputPair : (*InputsObj)->Values)
			{
				// Skip array values (those are links)
				const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
				if (InputPair.Value->TryGetArray(ArrayVal))
				{
					continue; // This is a link, handled in second pass
				}

				// It's a widget value
				FString StrVal;
				double NumVal;
				bool BoolVal;
				FString ParsedVal;

				if (InputPair.Value->TryGetString(StrVal))
				{
					ParsedVal = StrVal;
				}
				else if (InputPair.Value->TryGetNumber(NumVal))
				{
					ParsedVal = FString::SanitizeFloat(NumVal);
				}
				else if (InputPair.Value->TryGetBool(BoolVal))
				{
					ParsedVal = BoolVal ? TEXT("true") : TEXT("false");
				}
				else
				{
					continue;
				}

				// Update or add the widget value
				if (Node.WidgetValues.Contains(InputPair.Key))
				{
					Node.WidgetValues[InputPair.Key] = ParsedVal;
				}
				else
				{
					Node.WidgetValues.Add(InputPair.Key, ParsedVal);
					if (!Node.WidgetOrder.Contains(InputPair.Key))
					{
						Node.WidgetOrder.Add(InputPair.Key);
					}
				}
			}
		}

		// Update node ID tracking
		int32 IdNum;
		if (FDefaultValueHelper::ParseInt(NodeId, IdNum))
		{
			NextAutoNodeId = FMath::Max(NextAutoNodeId, IdNum + 1);
		}

		ComputeNodeSize(Node);
		int32 Idx = Nodes.Add(MoveTemp(Node));
		NodeIndexMap.Add(NodeId, Idx);
	}

	// Second pass: create connections
	for (const auto& Pair : Workflow->Values)
	{
		const FString& NodeId = Pair.Key;
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!Pair.Value->TryGetObject(NodeObjPtr)) continue;

		const TSharedPtr<FJsonObject>* InputsObj;
		if (!(*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObj)) continue;

		for (const auto& InputPair : (*InputsObj)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* LinkArray;
			if (!InputPair.Value->TryGetArray(LinkArray) || LinkArray->Num() < 2)
			{
				continue;
			}

			FString SourceNodeId;
			(*LinkArray)[0]->TryGetString(SourceNodeId);
			int32 SourceOutputIndex = static_cast<int32>((*LinkArray)[1]->AsNumber());

			FGraphConnection Conn;
			Conn.SourceNodeId = SourceNodeId;
			Conn.SourceOutputIndex = SourceOutputIndex;
			Conn.TargetNodeId = NodeId;
			Conn.TargetInputName = InputPair.Key;

			Connections.Add(Conn);
		}
	}

	// Apply positions: use Web UI positions if available, otherwise auto-layout
	if (bHasPositions)
	{
		for (FGraphNode& Node : Nodes)
		{
			FVector2D* Pos = WebUIPositions.Find(Node.Id);
			if (Pos)
			{
				Node.Position = *Pos;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Applied %d node positions from Web UI layout"), WebUIPositions.Num());
	}
	else
	{
		// API format has no position data — auto-layout
		AutoLayout();
	}

	NotifyGraphChanged();
}

// ============================================================================
// Graph Save / Load
// ============================================================================

TSharedPtr<FJsonObject> SWorkflowGraphEditor::SerializeGraph() const
{
	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);

	// Version for future compatibility
	Root->SetNumberField(TEXT("version"), 1);

	// View state
	TSharedPtr<FJsonObject> ViewObj = MakeShareable(new FJsonObject);
	ViewObj->SetNumberField(TEXT("offset_x"), ViewOffset.X);
	ViewObj->SetNumberField(TEXT("offset_y"), ViewOffset.Y);
	ViewObj->SetNumberField(TEXT("zoom"), ZoomLevel);
	Root->SetObjectField(TEXT("view"), ViewObj);

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (const FGraphNode& Node : Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("id"), Node.Id);
		NodeObj->SetStringField(TEXT("class_type"), Node.ClassType);
		NodeObj->SetStringField(TEXT("title"), Node.Title);
		NodeObj->SetNumberField(TEXT("pos_x"), Node.Position.X);
		NodeObj->SetNumberField(TEXT("pos_y"), Node.Position.Y);

		// Local file path (for LoadVideo nodes — persists Play button across graph reloads)
		if (!Node.LocalFilePath.IsEmpty())
		{
			NodeObj->SetStringField(TEXT("local_file_path"), Node.LocalFilePath);
		}

		// Widget values
		TSharedPtr<FJsonObject> WidgetsObj = MakeShareable(new FJsonObject);
		for (const auto& Widget : Node.WidgetValues)
		{
			WidgetsObj->SetStringField(Widget.Key, Widget.Value);
		}
		NodeObj->SetObjectField(TEXT("widgets"), WidgetsObj);

		// Widget order
		TArray<TSharedPtr<FJsonValue>> OrderArray;
		for (const FString& Name : Node.WidgetOrder)
		{
			OrderArray.Add(MakeShareable(new FJsonValueString(Name)));
		}
		NodeObj->SetArrayField(TEXT("widget_order"), OrderArray);

		// Input pins (for nodes without DB definitions, e.g. UE source nodes or unknown)
		TArray<TSharedPtr<FJsonValue>> InputPinArray;
		for (const FGraphPin& Pin : Node.InputPins)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
			PinObj->SetStringField(TEXT("name"), Pin.Name);
			PinObj->SetStringField(TEXT("type"), Pin.Type);
			InputPinArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
		}
		NodeObj->SetArrayField(TEXT("input_pins"), InputPinArray);

		// Output pins
		TArray<TSharedPtr<FJsonValue>> OutputPinArray;
		for (const FGraphPin& Pin : Node.OutputPins)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
			PinObj->SetStringField(TEXT("name"), Pin.Name);
			PinObj->SetStringField(TEXT("type"), Pin.Type);
			OutputPinArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
		}
		NodeObj->SetArrayField(TEXT("output_pins"), OutputPinArray);

		NodeArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}
	Root->SetArrayField(TEXT("nodes"), NodeArray);

	// Connections
	TArray<TSharedPtr<FJsonValue>> ConnArray;
	for (const FGraphConnection& Conn : Connections)
	{
		TSharedPtr<FJsonObject> ConnObj = MakeShareable(new FJsonObject);
		ConnObj->SetStringField(TEXT("source_node"), Conn.SourceNodeId);
		ConnObj->SetNumberField(TEXT("source_output"), Conn.SourceOutputIndex);
		ConnObj->SetStringField(TEXT("target_node"), Conn.TargetNodeId);
		ConnObj->SetStringField(TEXT("target_input"), Conn.TargetInputName);
		ConnArray.Add(MakeShareable(new FJsonValueObject(ConnObj)));
	}
	Root->SetArrayField(TEXT("connections"), ConnArray);

	return Root;
}

bool SWorkflowGraphEditor::DeserializeGraph(TSharedPtr<FJsonObject> GraphJson)
{
	if (!GraphJson.IsValid()) return false;

	ClearGraph();

	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();

	// View state
	const TSharedPtr<FJsonObject>* ViewObj;
	if (GraphJson->TryGetObjectField(TEXT("view"), ViewObj))
	{
		ViewOffset.X = (*ViewObj)->GetNumberField(TEXT("offset_x"));
		ViewOffset.Y = (*ViewObj)->GetNumberField(TEXT("offset_y"));
		ZoomLevel = (*ViewObj)->GetNumberField(TEXT("zoom"));
		ZoomLevel = FMath::Clamp(ZoomLevel, GraphConstants::MinZoom, GraphConstants::MaxZoom);
	}

	// Nodes
	const TArray<TSharedPtr<FJsonValue>>* NodeArray;
	if (GraphJson->TryGetArrayField(TEXT("nodes"), NodeArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeVal : *NodeArray)
		{
			const TSharedPtr<FJsonObject>* NodeObjPtr;
			if (!NodeVal->TryGetObject(NodeObjPtr)) continue;

			FString NodeId = (*NodeObjPtr)->GetStringField(TEXT("id"));
			FString ClassType = (*NodeObjPtr)->GetStringField(TEXT("class_type"));
			FString Title = (*NodeObjPtr)->GetStringField(TEXT("title"));
			double PosX = (*NodeObjPtr)->GetNumberField(TEXT("pos_x"));
			double PosY = (*NodeObjPtr)->GetNumberField(TEXT("pos_y"));
			FVector2D Position(PosX, PosY);

			FGraphNode Node;

			// Check if this is a UE source node
			if (ClassType == UEViewportClassType || ClassType == UEDepthMapClassType || ClassType == UECameraDataClassType || ClassType == UESegmentationClassType || ClassType == UEMeshyImportClassType || ClassType == UESave3DModelClassType || ClassType == UE3DLoaderClassType || ClassType == UEImageBridgeClassType || ClassType == UE3DAssetExportClassType || ClassType == UEPromptAdherenceClassType || ClassType == UEImageUpresClassType || ClassType == UESequenceClassType || ClassType == UEVideoToImageClassType)
			{
				Node = CreateUESourceNode(ClassType, NodeId, Position);
				Node.Title = Title;
			}
			else
			{
				// Try to create from database definition
				const FComfyNodeDef* Def = DB.FindNode(ClassType);
				if (Def)
				{
					Node = CreateNodeFromDef(*Def, NodeId, Position);
					Node.Title = Title;
				}
				else
				{
					// Unknown node — create from saved pin data
					Node.Id = NodeId;
					Node.ClassType = ClassType;
					Node.Title = Title;
					Node.Position = Position;
					Node.HeaderColor = GetNodeColor(ClassType);

					// Restore input pins from saved data
					const TArray<TSharedPtr<FJsonValue>>* InputPins;
					if ((*NodeObjPtr)->TryGetArrayField(TEXT("input_pins"), InputPins))
					{
						int32 PinIdx = 0;
						for (const TSharedPtr<FJsonValue>& PinVal : *InputPins)
						{
							const TSharedPtr<FJsonObject>* PinObj;
							if (PinVal->TryGetObject(PinObj))
							{
								FString PinType = (*PinObj)->GetStringField(TEXT("type"));
								FString PinName = (*PinObj)->GetStringField(TEXT("name"));

								// Migration: V3 dynamic combo types were previously saved as
								// input_pins but should be widgets. Convert them on load.
								if (PinType.StartsWith(TEXT("COMFY_DYNAMICCOMBO")) || PinType.StartsWith(TEXT("COMFY_")))
								{
									// Treat as a COMBO widget instead of a pin.
									// Try to get a proper default from the node DB, otherwise use a fallback.
									if (!Node.WidgetValues.Contains(PinName))
									{
										FString DefaultVal;
										FComfyInputDef WidgetDef;
										WidgetDef.Name = PinName;
										WidgetDef.Type = TEXT("COMBO");

										// Look up the real node definition for proper options/defaults
										const FComfyNodeDef* RealDef = FComfyNodeDatabase::Get().FindNode(ClassType);
										if (RealDef)
										{
											for (const FComfyInputDef& RealInput : RealDef->Inputs)
											{
												if (RealInput.Name == PinName)
												{
													WidgetDef = RealInput;
													if (WidgetDef.ComboOptions.Num() > 0)
													{
														DefaultVal = !WidgetDef.DefaultString.IsEmpty()
															? WidgetDef.DefaultString
															: WidgetDef.ComboOptions[0];
													}
													break;
												}
											}
										}

										// Fallback: if the DB didn't provide options (e.g. not yet fetched),
										// use "true"/"false" options for known boolean-like V3 combo fields
										if (DefaultVal.IsEmpty() && WidgetDef.ComboOptions.Num() == 0)
										{
											WidgetDef.ComboOptions = { TEXT("true"), TEXT("false") };
											DefaultVal = TEXT("true");
										}

										Node.WidgetValues.Add(PinName, DefaultVal);
										Node.WidgetInputDefs.Add(PinName, WidgetDef);
										Node.WidgetOrder.Add(PinName);
									}
									continue; // Don't add as a pin
								}

								FGraphPin Pin;
								Pin.Name = PinName;
								Pin.Type = PinType;
								Pin.bIsInput = true;
								Pin.PinIndex = PinIdx++;
								Pin.OwnerNodeId = NodeId;
								Node.InputPins.Add(Pin);
							}
						}
					}

					// Restore output pins from saved data
					const TArray<TSharedPtr<FJsonValue>>* OutputPins;
					if ((*NodeObjPtr)->TryGetArrayField(TEXT("output_pins"), OutputPins))
					{
						int32 PinIdx = 0;
						for (const TSharedPtr<FJsonValue>& PinVal : *OutputPins)
						{
							const TSharedPtr<FJsonObject>* PinObj;
							if (PinVal->TryGetObject(PinObj))
							{
								FGraphPin Pin;
								Pin.Name = (*PinObj)->GetStringField(TEXT("name"));
								Pin.Type = (*PinObj)->GetStringField(TEXT("type"));
								Pin.bIsInput = false;
								Pin.PinIndex = PinIdx++;
								Pin.OwnerNodeId = NodeId;
								Node.OutputPins.Add(Pin);
							}
						}
					}
				}
			}

			// Restore local file path (for LoadVideo Play button persistence)
			FString SavedLocalFilePath;
			if ((*NodeObjPtr)->TryGetStringField(TEXT("local_file_path"), SavedLocalFilePath) && !SavedLocalFilePath.IsEmpty())
			{
				Node.LocalFilePath = SavedLocalFilePath;
			}

			// Restore widget values from saved data (overwrite defaults)
			const TSharedPtr<FJsonObject>* WidgetsObj;
			if ((*NodeObjPtr)->TryGetObjectField(TEXT("widgets"), WidgetsObj))
			{
				for (const auto& WidgetPair : (*WidgetsObj)->Values)
				{
					FString StrVal;
					if (WidgetPair.Value->TryGetString(StrVal))
					{
						Node.WidgetValues.FindOrAdd(WidgetPair.Key) = StrVal;
					}
				}
			}

			// Restore widget order if present
			const TArray<TSharedPtr<FJsonValue>>* OrderArray;
			if ((*NodeObjPtr)->TryGetArrayField(TEXT("widget_order"), OrderArray))
			{
				Node.WidgetOrder.Empty();
				for (const TSharedPtr<FJsonValue>& OrderVal : *OrderArray)
				{
					FString Name;
					if (OrderVal->TryGetString(Name))
					{
						Node.WidgetOrder.Add(Name);
					}
				}
			}

			// Update auto ID counter
			int32 IdNum;
			if (FDefaultValueHelper::ParseInt(NodeId, IdNum))
			{
				NextAutoNodeId = FMath::Max(NextAutoNodeId, IdNum + 1);
			}

			ComputeNodeSize(Node);
			int32 Idx = Nodes.Add(MoveTemp(Node));
			NodeIndexMap.Add(NodeId, Idx);
		}
	}

	// Connections
	const TArray<TSharedPtr<FJsonValue>>* ConnArray;
	if (GraphJson->TryGetArrayField(TEXT("connections"), ConnArray))
	{
		for (const TSharedPtr<FJsonValue>& ConnVal : *ConnArray)
		{
			const TSharedPtr<FJsonObject>* ConnObj;
			if (!ConnVal->TryGetObject(ConnObj)) continue;

			FGraphConnection Conn;
			Conn.SourceNodeId = (*ConnObj)->GetStringField(TEXT("source_node"));
			Conn.SourceOutputIndex = static_cast<int32>((*ConnObj)->GetNumberField(TEXT("source_output")));
			Conn.TargetNodeId = (*ConnObj)->GetStringField(TEXT("target_node"));
			Conn.TargetInputName = (*ConnObj)->GetStringField(TEXT("target_input"));
			Connections.Add(Conn);
		}
	}

	bGraphDirty = false;
	NotifyGraphChanged();
	// Reset dirty flag since we just loaded — NotifyGraphChanged sets it
	bGraphDirty = false;

	return true;
}

bool SWorkflowGraphEditor::SaveGraphToFile(const FString& FilePath)
{
	TSharedPtr<FJsonObject> GraphJson = SerializeGraph();
	if (!GraphJson.IsValid()) return false;

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(GraphJson.ToSharedRef(), Writer))
	{
		return false;
	}

	if (!FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		return false;
	}

	CurrentFilePath = FilePath;
	bGraphDirty = false;
	return true;
}

bool SWorkflowGraphEditor::LoadGraphFromFile(const FString& FilePath)
{
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> GraphJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, GraphJson) || !GraphJson.IsValid())
	{
		return false;
	}

	if (!DeserializeGraph(GraphJson))
	{
		return false;
	}

	CurrentFilePath = FilePath;
	bGraphDirty = false;
	return true;
}

// ============================================================================
// Auto-Layout
// ============================================================================

TMap<FString, int32> SWorkflowGraphEditor::ComputeNodeDepths() const
{
	TMap<FString, int32> Depths;
	for (const FGraphNode& Node : Nodes)
	{
		Depths.Add(Node.Id, 0);
	}

	bool bChanged = true;
	int32 Safety = 0;
	while (bChanged && Safety++ < 100)
	{
		bChanged = false;
		for (const FGraphConnection& Conn : Connections)
		{
			if (Depths.Contains(Conn.SourceNodeId) && Depths.Contains(Conn.TargetNodeId))
			{
				int32 NewDepth = Depths[Conn.SourceNodeId] + 1;
				if (NewDepth > Depths[Conn.TargetNodeId])
				{
					Depths[Conn.TargetNodeId] = NewDepth;
					bChanged = true;
				}
			}
		}
	}

	return Depths;
}

void SWorkflowGraphEditor::AutoLayout()
{
	if (Nodes.Num() == 0) return;

	TMap<FString, int32> Depths = ComputeNodeDepths();

	int32 MaxDepth = 0;
	for (const auto& Pair : Depths)
	{
		MaxDepth = FMath::Max(MaxDepth, Pair.Value);
	}

	// Group by column
	TArray<TArray<int32>> Columns;
	Columns.SetNum(MaxDepth + 1);

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		int32 Depth = Depths.FindRef(Nodes[i].Id);
		Columns[Depth].Add(i);
	}

	// Position
	for (int32 Col = 0; Col <= MaxDepth; ++Col)
	{
		float X = Col * GraphConstants::ColumnSpacing;
		float TotalHeight = 0.0f;

		for (int32 Idx : Columns[Col])
		{
			TotalHeight += Nodes[Idx].Size.Y + GraphConstants::RowSpacing;
		}
		TotalHeight -= GraphConstants::RowSpacing;

		float Y = -TotalHeight * 0.5f;
		for (int32 Idx : Columns[Col])
		{
			// Snap to grid
			float SnappedX = FMath::RoundToFloat(X / GraphConstants::GridSize) * GraphConstants::GridSize;
			float SnappedY = FMath::RoundToFloat(Y / GraphConstants::GridSize) * GraphConstants::GridSize;
			Nodes[Idx].Position = FVector2D(SnappedX, SnappedY);
			Y += Nodes[Idx].Size.Y + GraphConstants::RowSpacing;
		}
	}

	// Center the view
	ViewOffset = FVector2D(100.0f, 200.0f);
}

// ============================================================================
// Build Preset Graph (backward compatibility with UGenAISettings)
// ============================================================================

void SWorkflowGraphEditor::BuildPresetGraph()
{
	// This rebuilds the graph from settings, similar to the old SWorkflowPreviewPanel
	// but now as editable FGraphNodes.
	// The actual graph content will come from the generation workflow JSON
	// built by GenAIHttpClient, which we can import.
	ClearGraph();

	// For now, build a simple txt2img graph as default
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (!Settings) return;

	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();
	if (!DB.IsPopulated())
	{
		// Database not loaded yet — create minimal hardcoded graph
		FGraphNode CkptNode;
		CkptNode.Id = TEXT("1");
		CkptNode.ClassType = TEXT("CheckpointLoaderSimple");
		CkptNode.Title = TEXT("Load Checkpoint");
		CkptNode.Position = FVector2D(0, 0);
		CkptNode.HeaderColor = GetNodeColor(TEXT("Checkpoint"));
		CkptNode.WidgetValues.Add(TEXT("ckpt_name"), Settings->CheckpointName);
		CkptNode.WidgetOrder.Add(TEXT("ckpt_name"));

		FGraphPin OutModel; OutModel.Name = TEXT("MODEL"); OutModel.Type = TEXT("MODEL"); OutModel.bIsInput = false; OutModel.PinIndex = 0; OutModel.OwnerNodeId = TEXT("1");
		FGraphPin OutClip; OutClip.Name = TEXT("CLIP"); OutClip.Type = TEXT("CLIP"); OutClip.bIsInput = false; OutClip.PinIndex = 1; OutClip.OwnerNodeId = TEXT("1");
		FGraphPin OutVAE; OutVAE.Name = TEXT("VAE"); OutVAE.Type = TEXT("VAE"); OutVAE.bIsInput = false; OutVAE.PinIndex = 2; OutVAE.OwnerNodeId = TEXT("1");
		CkptNode.OutputPins = { OutModel, OutClip, OutVAE };
		ComputeNodeSize(CkptNode);
		NodeIndexMap.Add(TEXT("1"), Nodes.Add(MoveTemp(CkptNode)));

		// Minimal KSampler
		FGraphNode SamplerNode;
		SamplerNode.Id = TEXT("5");
		SamplerNode.ClassType = TEXT("KSampler");
		SamplerNode.Title = TEXT("KSampler");
		SamplerNode.Position = FVector2D(300, 0);
		SamplerNode.HeaderColor = GetNodeColor(TEXT("KSampler"));
		SamplerNode.WidgetValues.Add(TEXT("steps"), FString::FromInt(Settings->Steps));
		SamplerNode.WidgetValues.Add(TEXT("cfg"), FString::SanitizeFloat(Settings->CFGScale));
		SamplerNode.WidgetOrder.Add(TEXT("steps"));
		SamplerNode.WidgetOrder.Add(TEXT("cfg"));

		FGraphPin InModel; InModel.Name = TEXT("model"); InModel.Type = TEXT("MODEL"); InModel.bIsInput = true; InModel.PinIndex = 0; InModel.OwnerNodeId = TEXT("5");
		FGraphPin InPos; InPos.Name = TEXT("positive"); InPos.Type = TEXT("CONDITIONING"); InPos.bIsInput = true; InPos.PinIndex = 1; InPos.OwnerNodeId = TEXT("5");
		FGraphPin InNeg; InNeg.Name = TEXT("negative"); InNeg.Type = TEXT("CONDITIONING"); InNeg.bIsInput = true; InNeg.PinIndex = 2; InNeg.OwnerNodeId = TEXT("5");
		FGraphPin InLatent; InLatent.Name = TEXT("latent_image"); InLatent.Type = TEXT("LATENT"); InLatent.bIsInput = true; InLatent.PinIndex = 3; InLatent.OwnerNodeId = TEXT("5");
		SamplerNode.InputPins = { InModel, InPos, InNeg, InLatent };

		FGraphPin SamplerOut; SamplerOut.Name = TEXT("LATENT"); SamplerOut.Type = TEXT("LATENT"); SamplerOut.bIsInput = false; SamplerOut.PinIndex = 0; SamplerOut.OwnerNodeId = TEXT("5");
		SamplerNode.OutputPins = { SamplerOut };
		ComputeNodeSize(SamplerNode);
		NodeIndexMap.Add(TEXT("5"), Nodes.Add(MoveTemp(SamplerNode)));

		// Add connection
		AddConnection(TEXT("1"), 0, TEXT("5"), TEXT("model"));

		return;
	}

	// With database available, build from defs
	auto AddNodeFromDB = [&](const FString& ClassType, const FString& Id, FVector2D Pos) -> FString
	{
		const FComfyNodeDef* Def = DB.FindNode(ClassType);
		if (!Def) return FString();

		FGraphNode Node = CreateNodeFromDef(*Def, Id, Pos);
		int32 Idx = Nodes.Add(MoveTemp(Node));
		NodeIndexMap.Add(Id, Idx);

		int32 IdNum;
		if (FDefaultValueHelper::ParseInt(Id, IdNum))
		{
			NextAutoNodeId = FMath::Max(NextAutoNodeId, IdNum + 1);
		}

		return Id;
	};

	// Build based on current mode
	FString CkptId = AddNodeFromDB(TEXT("CheckpointLoaderSimple"), TEXT("1"), FVector2D(0, 0));
	if (!CkptId.IsEmpty())
	{
		int32* Idx = NodeIndexMap.Find(CkptId);
		if (Idx) Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("ckpt_name")) = Settings->CheckpointName;
	}

	FString PosClipId = AddNodeFromDB(TEXT("CLIPTextEncode"), TEXT("2"), FVector2D(300, -80));
	if (!PosClipId.IsEmpty())
	{
		int32* Idx = NodeIndexMap.Find(PosClipId);
		if (Idx)
		{
			Nodes[*Idx].Title = TEXT("CLIP (Positive)");
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("text")) = Settings->DefaultPrompt;
		}
	}

	FString NegClipId = AddNodeFromDB(TEXT("CLIPTextEncode"), TEXT("3"), FVector2D(300, 80));
	if (!NegClipId.IsEmpty())
	{
		int32* Idx = NodeIndexMap.Find(NegClipId);
		if (Idx)
		{
			Nodes[*Idx].Title = TEXT("CLIP (Negative)");
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("text")) = Settings->DefaultNegativePrompt;
		}
	}

	FString EmptyLatentId = AddNodeFromDB(TEXT("EmptyLatentImage"), TEXT("4"), FVector2D(300, 200));
	if (!EmptyLatentId.IsEmpty())
	{
		int32* Idx = NodeIndexMap.Find(EmptyLatentId);
		if (Idx)
		{
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("width")) = FString::FromInt(Settings->OutputWidth);
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("height")) = FString::FromInt(Settings->OutputHeight);
		}
	}

	FString KSamplerId = AddNodeFromDB(TEXT("KSampler"), TEXT("5"), FVector2D(600, 0));
	if (!KSamplerId.IsEmpty())
	{
		int32* Idx = NodeIndexMap.Find(KSamplerId);
		if (Idx)
		{
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("steps")) = FString::FromInt(Settings->Steps);
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("cfg")) = FString::SanitizeFloat(Settings->CFGScale);
			Nodes[*Idx].WidgetValues.FindOrAdd(TEXT("sampler_name")) = Settings->SamplerName;
		}
	}

	FString VaeDecId = AddNodeFromDB(TEXT("VAEDecode"), TEXT("6"), FVector2D(900, 0));
	FString SaveId = AddNodeFromDB(TEXT("SaveImage"), TEXT("7"), FVector2D(1200, 0));

	// Wire up connections
	if (!CkptId.IsEmpty())
	{
		AddConnection(TEXT("1"), 1, TEXT("2"), TEXT("clip"));
		AddConnection(TEXT("1"), 1, TEXT("3"), TEXT("clip"));
		AddConnection(TEXT("1"), 0, TEXT("5"), TEXT("model"));
	}
	AddConnection(TEXT("2"), 0, TEXT("5"), TEXT("positive"));
	AddConnection(TEXT("3"), 0, TEXT("5"), TEXT("negative"));
	AddConnection(TEXT("4"), 0, TEXT("5"), TEXT("latent_image"));
	AddConnection(TEXT("5"), 0, TEXT("6"), TEXT("samples"));
	if (!CkptId.IsEmpty())
	{
		AddConnection(TEXT("1"), 2, TEXT("6"), TEXT("vae"));
	}
	AddConnection(TEXT("6"), 0, TEXT("7"), TEXT("images"));

	AutoLayout();
}

// ============================================================================
// Helpers
// ============================================================================

void SWorkflowGraphEditor::NotifyGraphChanged()
{
	bGraphDirty = true;
	OnGraphChanged.ExecuteIfBound();
}

// ============================================================================
// Undo / Redo
// ============================================================================

void SWorkflowGraphEditor::PushUndoSnapshot()
{
	// Saves the CURRENT state as a snapshot that can be restored later.
	// Must be called BEFORE making a change.
	if (bIsRestoringSnapshot) return;

	TSharedPtr<FJsonObject> Snapshot = SerializeGraph();
	if (!Snapshot.IsValid()) return;

	UndoStack.Add(Snapshot);

	// Cap the stack size
	while (UndoStack.Num() > MaxUndoLevels)
	{
		UndoStack.RemoveAt(0);
	}

	// Any new action invalidates the redo stack
	RedoStack.Empty();
}

void SWorkflowGraphEditor::RestoreSnapshot(TSharedPtr<FJsonObject> Snapshot)
{
	if (!Snapshot.IsValid()) return;

	bIsRestoringSnapshot = true;
	DeserializeGraph(Snapshot);
	bIsRestoringSnapshot = false;
}

void SWorkflowGraphEditor::Undo()
{
	if (UndoStack.Num() == 0) return;

	// Save current state to redo stack before restoring
	TSharedPtr<FJsonObject> CurrentState = SerializeGraph();
	if (CurrentState.IsValid())
	{
		RedoStack.Add(CurrentState);
	}

	// Pop and restore the last undo snapshot
	TSharedPtr<FJsonObject> Snapshot = UndoStack.Pop();
	RestoreSnapshot(Snapshot);

	bGraphDirty = true;
	OnGraphChanged.ExecuteIfBound();
}

void SWorkflowGraphEditor::Redo()
{
	if (RedoStack.Num() == 0) return;

	// Save current state to undo stack before restoring
	TSharedPtr<FJsonObject> CurrentState = SerializeGraph();
	if (CurrentState.IsValid())
	{
		UndoStack.Add(CurrentState);
	}

	// Pop and restore the last redo snapshot
	TSharedPtr<FJsonObject> Snapshot = RedoStack.Pop();
	RestoreSnapshot(Snapshot);

	bGraphDirty = true;
	OnGraphChanged.ExecuteIfBound();
}

// ============================================================================
// Copy / Paste / Cut
// ============================================================================

void SWorkflowGraphEditor::CopySelectedNodes()
{
	if (SelectedNodeIds.Num() == 0) return;

	Clipboard = MakeShareable(new FClipboardData());

	// Calculate center of selection for paste offset
	FVector2D Center = FVector2D::ZeroVector;
	int32 Count = 0;
	for (const FString& NodeId : SelectedNodeIds)
	{
		const int32* IdxPtr = NodeIndexMap.Find(NodeId);
		if (!IdxPtr) continue;
		Center += Nodes[*IdxPtr].Position;
		Count++;
	}
	if (Count > 0) Center /= Count;
	Clipboard->CenterOffset = Center;

	// Copy selected nodes (deep copy, strip selection state)
	for (const FString& NodeId : SelectedNodeIds)
	{
		const int32* IdxPtr = NodeIndexMap.Find(NodeId);
		if (!IdxPtr) continue;

		FGraphNode Copy = Nodes[*IdxPtr];
		Copy.bSelected = false;
		Copy.ThumbnailBrush.Reset();
		Copy.ThumbnailTexture = nullptr;
		Copy.MeshPreview = nullptr;
		Clipboard->Nodes.Add(MoveTemp(Copy));
	}

	// Copy connections that exist entirely within the selection
	for (const FGraphConnection& Conn : Connections)
	{
		if (SelectedNodeIds.Contains(Conn.SourceNodeId) &&
			SelectedNodeIds.Contains(Conn.TargetNodeId))
		{
			Clipboard->Connections.Add(Conn);
		}
	}
}

void SWorkflowGraphEditor::PasteNodes()
{
	// If internal clipboard is empty, try the system clipboard for ComfyUI node data
	if (!Clipboard.IsValid() || Clipboard->Nodes.Num() == 0)
	{
		PasteFromSystemClipboard();
		return;
	}

	PushUndoSnapshot();
	const FVector2D PasteOffset(60.0f, 60.0f);

	// Map from clipboard node ID -> new node ID
	TMap<FString, FString> IdRemap;
	TSet<FString> NewSelectedIds;

	for (const FGraphNode& ClipNode : Clipboard->Nodes)
	{
		FString NewId = FString::FromInt(NextAutoNodeId++);
		IdRemap.Add(ClipNode.Id, NewId);

		FGraphNode NewNode = ClipNode;
		NewNode.Id = NewId;
		NewNode.Position = ClipNode.Position - Clipboard->CenterOffset + Clipboard->CenterOffset + PasteOffset;
		NewNode.bSelected = false;

		// Update pin ownership to the new node ID
		for (FGraphPin& Pin : NewNode.InputPins) { Pin.OwnerNodeId = NewId; }
		for (FGraphPin& Pin : NewNode.OutputPins) { Pin.OwnerNodeId = NewId; }

		// Don't carry over thumbnails or mesh previews
		NewNode.ThumbnailBrush.Reset();
		NewNode.ThumbnailTexture = nullptr;
		NewNode.MeshPreview = nullptr;

		int32 NewIdx = Nodes.Add(MoveTemp(NewNode));
		NodeIndexMap.Add(NewId, NewIdx);
		NewSelectedIds.Add(NewId);
	}

	// Recreate internal connections with remapped IDs
	for (const FGraphConnection& Conn : Clipboard->Connections)
	{
		const FString* NewSourceId = IdRemap.Find(Conn.SourceNodeId);
		const FString* NewTargetId = IdRemap.Find(Conn.TargetNodeId);
		if (NewSourceId && NewTargetId)
		{
			FGraphConnection NewConn;
			NewConn.SourceNodeId = *NewSourceId;
			NewConn.SourceOutputIndex = Conn.SourceOutputIndex;
			NewConn.TargetNodeId = *NewTargetId;
			NewConn.TargetInputName = Conn.TargetInputName;
			Connections.Add(NewConn);
		}
	}

	// Shift the clipboard center so repeated pastes cascade
	Clipboard->CenterOffset -= PasteOffset;

	// Select pasted nodes
	SelectedNodeIds = MoveTemp(NewSelectedIds);
	NotifySelectionChanged();
	NotifyGraphChanged();
}

void SWorkflowGraphEditor::PasteFromSystemClipboard()
{
	// Read the system clipboard
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	if (ClipboardText.IsEmpty()) return;

	// Try to parse as JSON
	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ClipboardText);
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		return; // Not valid JSON — ignore
	}

	// Detect format: ComfyUI Web UI clipboard has "nodes" array and optionally "links" array
	// ComfyUI API format has string keys with "class_type" objects
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	bool bIsWebUI = JsonRoot->TryGetArrayField(TEXT("nodes"), NodesArray);

	// Also check for API format: any top-level key with a "class_type" child
	bool bIsAPI = false;
	if (!bIsWebUI)
	{
		for (const auto& Pair : JsonRoot->Values)
		{
			const TSharedPtr<FJsonObject>* NodeObj;
			if (Pair.Value->TryGetObject(NodeObj))
			{
				FString ClassType;
				if ((*NodeObj)->TryGetStringField(TEXT("class_type"), ClassType))
				{
					bIsAPI = true;
					break;
				}
			}
		}
	}

	if (!bIsWebUI && !bIsAPI) return; // Not ComfyUI data

	PushUndoSnapshot();

	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();

	// Convert Web UI format to API format if needed
	TMap<FString, FVector2D> WebUIPositions;
	TSharedPtr<FJsonObject> APIWorkflow;
	if (bIsWebUI)
	{
		APIWorkflow = ConvertWebUIToAPIFormat(JsonRoot, WebUIPositions);
		if (!APIWorkflow.IsValid() || APIWorkflow->Values.Num() == 0) return;
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Pasting %d nodes from ComfyUI Web UI clipboard"), APIWorkflow->Values.Num());
	}
	else
	{
		APIWorkflow = JsonRoot;
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Pasting %d nodes from ComfyUI API clipboard"), APIWorkflow->Values.Num());
	}

	// Calculate the center of the current view for paste positioning
	FVector2D ViewCenter = -ViewOffset / ZoomLevel;

	// Create nodes from the API-format workflow (append to existing graph, don't clear)
	TMap<FString, FString> IdRemap; // Old ID -> New ID
	TSet<FString> NewSelectedIds;

	for (const auto& Pair : APIWorkflow->Values)
	{
		const FString& OldNodeId = Pair.Key;
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!Pair.Value->TryGetObject(NodeObjPtr)) continue;

		FString ClassType;
		if (!(*NodeObjPtr)->TryGetStringField(TEXT("class_type"), ClassType) || ClassType.IsEmpty())
		{
			continue;
		}

		FString NewId = FString::FromInt(NextAutoNodeId++);
		IdRemap.Add(OldNodeId, NewId);

		FString Title = ClassType;
		const TSharedPtr<FJsonObject>* MetaObj;
		if ((*NodeObjPtr)->TryGetObjectField(TEXT("_meta"), MetaObj))
		{
			FString MetaTitle;
			if ((*MetaObj)->TryGetStringField(TEXT("title"), MetaTitle) && !MetaTitle.IsEmpty())
			{
				Title = MetaTitle;
			}
		}

		// Determine position
		FVector2D NodePos = ViewCenter;
		FVector2D* WebPos = WebUIPositions.Find(OldNodeId);
		if (WebPos)
		{
			NodePos = *WebPos;
		}

		const FComfyNodeDef* Def = DB.FindNode(ClassType);
		FGraphNode Node;

		if (Def)
		{
			Node = CreateNodeFromDef(*Def, NewId, NodePos);
			Node.Title = Title;
		}
		else
		{
			// Unknown node
			Node.Id = NewId;
			Node.ClassType = ClassType;
			Node.Title = Title;
			Node.Position = NodePos;
			Node.HeaderColor = GetNodeColor(ClassType);
			Node.Size = FVector2D(GraphConstants::NodeMinWidth, GraphConstants::NodeHeaderHeight + 40.0f);

			// Create pins from API inputs/outputs
			const TSharedPtr<FJsonObject>* InputsObj;
			if ((*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObj))
			{
				for (const auto& InputPair : (*InputsObj)->Values)
				{
					const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
					if (InputPair.Value->TryGetArray(ArrayVal))
					{
						FGraphPin Pin;
						Pin.Name = InputPair.Key;
						Pin.Type = TEXT("*");
						Pin.bIsInput = true;
						Pin.OwnerNodeId = NewId;
						Node.InputPins.Add(Pin);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* OutputDefs;
			if ((*NodeObjPtr)->TryGetArrayField(TEXT("_outputs"), OutputDefs))
			{
				for (const auto& OutVal : *OutputDefs)
				{
					const TSharedPtr<FJsonObject>* OutObjPtr;
					if (!OutVal->TryGetObject(OutObjPtr)) continue;
					FGraphPin Pin;
					Pin.Name = (*OutObjPtr)->GetStringField(TEXT("name"));
					Pin.Type = (*OutObjPtr)->GetStringField(TEXT("type"));
					Pin.bIsInput = false;
					Pin.OwnerNodeId = NewId;
					Node.OutputPins.Add(Pin);
				}
			}
		}

		// Apply widget values from inputs
		const TSharedPtr<FJsonObject>* InputsObj;
		if ((*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObj))
		{
			for (const auto& InputPair : (*InputsObj)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
				if (InputPair.Value->TryGetArray(ArrayVal)) continue; // Skip links

				FString StrVal;
				double NumVal;
				bool BoolVal;
				FString ParsedVal;

				if (InputPair.Value->TryGetString(StrVal))
					ParsedVal = StrVal;
				else if (InputPair.Value->TryGetNumber(NumVal))
					ParsedVal = FString::SanitizeFloat(NumVal);
				else if (InputPair.Value->TryGetBool(BoolVal))
					ParsedVal = BoolVal ? TEXT("true") : TEXT("false");
				else
					continue;

				if (Node.WidgetValues.Contains(InputPair.Key))
					Node.WidgetValues[InputPair.Key] = ParsedVal;
				else
				{
					Node.WidgetValues.Add(InputPair.Key, ParsedVal);
					if (!Node.WidgetOrder.Contains(InputPair.Key))
						Node.WidgetOrder.Add(InputPair.Key);
				}
			}
		}

		int32 IdNum;
		if (FDefaultValueHelper::ParseInt(NewId, IdNum))
		{
			NextAutoNodeId = FMath::Max(NextAutoNodeId, IdNum + 1);
		}

		ComputeNodeSize(Node);
		int32 Idx = Nodes.Add(MoveTemp(Node));
		NodeIndexMap.Add(NewId, Idx);
		NewSelectedIds.Add(NewId);
	}

	// Create connections with remapped IDs
	for (const auto& Pair : APIWorkflow->Values)
	{
		const FString& OldNodeId = Pair.Key;
		const TSharedPtr<FJsonObject>* NodeObjPtr;
		if (!Pair.Value->TryGetObject(NodeObjPtr)) continue;

		const TSharedPtr<FJsonObject>* InputsObj;
		if (!(*NodeObjPtr)->TryGetObjectField(TEXT("inputs"), InputsObj)) continue;

		const FString* NewTargetId = IdRemap.Find(OldNodeId);
		if (!NewTargetId) continue;

		for (const auto& InputPair : (*InputsObj)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* LinkArray;
			if (!InputPair.Value->TryGetArray(LinkArray) || LinkArray->Num() < 2) continue;

			FString OldSourceId;
			(*LinkArray)[0]->TryGetString(OldSourceId);
			int32 SourceOutputIndex = static_cast<int32>((*LinkArray)[1]->AsNumber());

			const FString* NewSourceId = IdRemap.Find(OldSourceId);
			if (!NewSourceId) continue; // Source not in pasted set — skip

			FGraphConnection Conn;
			Conn.SourceNodeId = *NewSourceId;
			Conn.SourceOutputIndex = SourceOutputIndex;
			Conn.TargetNodeId = *NewTargetId;
			Conn.TargetInputName = InputPair.Key;
			Connections.Add(Conn);
		}
	}

	// If no Web UI positions, auto-layout just the pasted nodes around the view center
	if (WebUIPositions.Num() == 0 && NewSelectedIds.Num() > 0)
	{
		float YOffset = 0.0f;
		for (const FString& NodeId : NewSelectedIds)
		{
			const int32* IdxPtr = NodeIndexMap.Find(NodeId);
			if (!IdxPtr) continue;
			Nodes[*IdxPtr].Position = ViewCenter + FVector2D(0.0f, YOffset);
			YOffset += Nodes[*IdxPtr].Size.Y + 20.0f;
		}
	}

	// Select the pasted nodes
	SelectedNodeIds = MoveTemp(NewSelectedIds);
	NotifySelectionChanged();
	NotifyGraphChanged();

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Pasted %d nodes from system clipboard"), IdRemap.Num());
}

void SWorkflowGraphEditor::CutSelectedNodes()
{
	CopySelectedNodes();

	// Push a single undo snapshot before deleting all cut nodes
	PushUndoSnapshot();
	TArray<FString> ToDelete = SelectedNodeIds.Array();
	SelectedNodeIds.Empty();
	NotifySelectionChanged();
	bIsRestoringSnapshot = true; // Suppress per-node undo pushes
	for (const FString& NodeId : ToDelete)
	{
		RemoveNode(NodeId);
	}
	bIsRestoringSnapshot = false;
}

// ============================================================================
// Partial Execution — Run To Node
// ============================================================================

TSet<FString> SWorkflowGraphEditor::CollectUpstreamNodes(const FString& TargetNodeId) const
{
	TSet<FString> Result;
	TArray<FString> Queue;
	Queue.Add(TargetNodeId);

	while (Queue.Num() > 0)
	{
		FString Current = Queue.Pop();
		if (Result.Contains(Current)) continue;
		Result.Add(Current);

		// Find all connections where this node is the target
		for (const FGraphConnection& Conn : Connections)
		{
			if (Conn.TargetNodeId == Current)
			{
				if (!Result.Contains(Conn.SourceNodeId))
				{
					Queue.Add(Conn.SourceNodeId);
				}
			}
		}
	}

	return Result;
}

TSharedPtr<FJsonObject> SWorkflowGraphEditor::ExportPartialWorkflow(const FString& TargetNodeId,
	bool* OutNeedsViewport, bool* OutNeedsDepth,
	FString* OutCameraDescription, bool* OutNeedsSegmentation) const
{
	// Collect the subgraph
	TSet<FString> RequiredNodes = CollectUpstreamNodes(TargetNodeId);

	if (RequiredNodes.Num() == 0) return nullptr;

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Partial execution — %d nodes upstream of '%s'"),
		RequiredNodes.Num(), *TargetNodeId);

	// Initialize output flags
	if (OutNeedsViewport) *OutNeedsViewport = false;
	if (OutNeedsDepth) *OutNeedsDepth = false;
	if (OutNeedsSegmentation) *OutNeedsSegmentation = false;

	const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();

	TSharedPtr<FJsonObject> PromptRoot = MakeShareable(new FJsonObject);

	for (const FString& NodeId : RequiredNodes)
	{
		const int32* IdxPtr = NodeIndexMap.Find(NodeId);
		if (!IdxPtr) continue;
		const FGraphNode& Node = Nodes[*IdxPtr];

		// Skip UE source nodes in the JSON — they're handled by the panel
		if (Node.IsUESourceNode())
		{
			if (OutNeedsViewport && Node.ClassType == UEViewportClassType) *OutNeedsViewport = true;
			if (OutNeedsDepth && Node.ClassType == UEDepthMapClassType) *OutNeedsDepth = true;
			if (OutNeedsSegmentation && Node.ClassType == UESegmentationClassType) *OutNeedsSegmentation = true;
			if (OutCameraDescription && Node.ClassType == UECameraDataClassType)
			{
				// Camera description is computed at submission time by the panel
			}
			continue;
		}
		if (Node.IsReroute()) continue;

		// Build the node JSON
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("class_type"), Node.ClassType);

		TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
		MetaObj->SetStringField(TEXT("title"), Node.Title);
		NodeObj->SetObjectField(TEXT("_meta"), MetaObj);

		TSharedPtr<FJsonObject> InputsObj = MakeShareable(new FJsonObject);

		// Widget values
		for (const FString& WidgetName : Node.WidgetOrder)
		{
			const FString* Val = Node.WidgetValues.Find(WidgetName);
			if (!Val) continue;

			// Try to preserve numeric types
			const FComfyInputDef* InputDef = Node.WidgetInputDefs.Find(WidgetName);
			if (InputDef)
			{
				if (InputDef->Type == TEXT("INT"))
				{
					InputsObj->SetNumberField(WidgetName, FCString::Atoi(**Val));
				}
				else if (InputDef->Type == TEXT("FLOAT"))
				{
					InputsObj->SetNumberField(WidgetName, FCString::Atof(**Val));
				}
				else if (InputDef->Type == TEXT("BOOLEAN") || InputDef->Type == TEXT("BOOL"))
				{
					InputsObj->SetBoolField(WidgetName, Val->Equals(TEXT("true"), ESearchCase::IgnoreCase));
				}
				else
				{
					InputsObj->SetStringField(WidgetName, *Val);
				}
			}
			else
			{
				InputsObj->SetStringField(WidgetName, *Val);
			}
		}

		// Link inputs (connections)
		for (const FGraphConnection& Conn : Connections)
		{
			if (Conn.TargetNodeId != NodeId) continue;

			// Resolve through reroute nodes
			FString RealSourceId = Conn.SourceNodeId;
			int32 RealOutputIdx = Conn.SourceOutputIndex;
			ResolveRerouteSource(RealSourceId, RealSourceId, RealOutputIdx);

			TArray<TSharedPtr<FJsonValue>> LinkRef;
			LinkRef.Add(MakeShareable(new FJsonValueString(RealSourceId)));
			LinkRef.Add(MakeShareable(new FJsonValueNumber(RealOutputIdx)));
			InputsObj->SetArrayField(Conn.TargetInputName, LinkRef);
		}

		NodeObj->SetObjectField(TEXT("inputs"), InputsObj);
		PromptRoot->SetObjectField(NodeId, NodeObj);
	}

	return PromptRoot;
}

void SWorkflowGraphEditor::NotifySelectionChanged()
{
	OnSelectionChanged.ExecuteIfBound();
}

FString SWorkflowGraphEditor::GetPrimarySelectedNodeId() const
{
	if (SelectedNodeIds.Num() == 0)
	{
		return FString();
	}
	// Return the first element
	for (const FString& Id : SelectedNodeIds)
	{
		return Id;
	}
	return FString();
}

FGraphNode* SWorkflowGraphEditor::FindNodeById(const FString& NodeId)
{
	const int32* IdxPtr = NodeIndexMap.Find(NodeId);
	if (IdxPtr && Nodes.IsValidIndex(*IdxPtr))
	{
		return &Nodes[*IdxPtr];
	}
	return nullptr;
}

// ============================================================================
// Reroute / Redirect Nodes
// ============================================================================

FGraphNode SWorkflowGraphEditor::CreateRerouteNode(const FString& Id, const FString& DataType, FVector2D Position) const
{
	FGraphNode Node;
	Node.Id = Id;
	Node.ClassType = RerouteClassType;
	Node.Title = TEXT("");
	Node.Position = Position;
	Node.HeaderColor = FLinearColor(0.5f, 0.5f, 0.5f);

	// One input pin
	FGraphPin InPin;
	InPin.Name = TEXT("in");
	InPin.Type = DataType;
	InPin.bIsInput = true;
	InPin.PinIndex = 0;
	InPin.OwnerNodeId = Id;
	Node.InputPins.Add(InPin);

	// One output pin (same type)
	FGraphPin OutPin;
	OutPin.Name = TEXT("out");
	OutPin.Type = DataType;
	OutPin.bIsInput = false;
	OutPin.PinIndex = 0;
	OutPin.OwnerNodeId = Id;
	Node.OutputPins.Add(OutPin);

	// Compact size — just a small diamond
	Node.Size = FVector2D(24.0f, 24.0f);

	return Node;
}

FString SWorkflowGraphEditor::InsertRerouteNode(int32 ConnectionIndex, FVector2D GraphPosition)
{
	if (ConnectionIndex < 0 || ConnectionIndex >= Connections.Num())
	{
		return FString();
	}

	// Capture the existing connection before modifying anything
	FGraphConnection OldConn = Connections[ConnectionIndex];

	// Determine the data type from the source output pin
	FString DataType = TEXT("*");
	const int32* SrcIdx = NodeIndexMap.Find(OldConn.SourceNodeId);
	if (SrcIdx && OldConn.SourceOutputIndex < Nodes[*SrcIdx].OutputPins.Num())
	{
		DataType = Nodes[*SrcIdx].OutputPins[OldConn.SourceOutputIndex].Type;
	}

	// Create the reroute node
	FString RerouteId = FString::FromInt(NextAutoNodeId++);
	FGraphNode RerouteNode = CreateRerouteNode(RerouteId, DataType, GraphPosition);
	int32 Idx = Nodes.Add(MoveTemp(RerouteNode));
	NodeIndexMap.Add(RerouteId, Idx);

	// Remove the old connection
	Connections.RemoveAt(ConnectionIndex);

	// Wire: OldSource -> Reroute input
	FGraphConnection ConnIn;
	ConnIn.SourceNodeId = OldConn.SourceNodeId;
	ConnIn.SourceOutputIndex = OldConn.SourceOutputIndex;
	ConnIn.TargetNodeId = RerouteId;
	ConnIn.TargetInputName = TEXT("in");
	Connections.Add(ConnIn);

	// Wire: Reroute output -> OldTarget
	FGraphConnection ConnOut;
	ConnOut.SourceNodeId = RerouteId;
	ConnOut.SourceOutputIndex = 0;
	ConnOut.TargetNodeId = OldConn.TargetNodeId;
	ConnOut.TargetInputName = OldConn.TargetInputName;
	Connections.Add(ConnOut);

	NotifyGraphChanged();
	return RerouteId;
}

void SWorkflowGraphEditor::DrawRerouteNode(const FGraphNode& Node, const FGeometry& Geom,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FVector2D Center = GraphToLocal(Node.Position + Node.Size * 0.5f);
	const float HalfSize = 10.0f * ZoomLevel;

	// Get the type colour from the pin
	FLinearColor PinColor(0.5f, 0.5f, 0.5f);
	if (Node.InputPins.Num() > 0)
	{
		const FString& Type = Node.InputPins[0].Type;
		if (Type == TEXT("MODEL")) PinColor = FLinearColor(0.4f, 0.6f, 0.8f);
		else if (Type == TEXT("CLIP")) PinColor = FLinearColor(0.8f, 0.8f, 0.3f);
		else if (Type == TEXT("CONDITIONING")) PinColor = FLinearColor(0.8f, 0.5f, 0.2f);
		else if (Type == TEXT("LATENT")) PinColor = FLinearColor(0.8f, 0.2f, 0.8f);
		else if (Type == TEXT("IMAGE")) PinColor = FLinearColor(0.4f, 0.8f, 0.4f);
		else if (Type == TEXT("VAE")) PinColor = FLinearColor(0.8f, 0.3f, 0.3f);
		else if (Type == TEXT("CONTROL_NET")) PinColor = FLinearColor(0.7f, 0.4f, 0.2f);
	}

	const FSlateBrush* WhiteBox = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	// Selection highlight ring
	bool bIsSelected = SelectedNodeIds.Contains(Node.Id);
	if (bIsSelected)
	{
		float HighlightSize = HalfSize + 3.0f * ZoomLevel;
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId,
			Geom.ToPaintGeometry(FVector2D(HighlightSize * 2, HighlightSize * 2),
				FSlateLayoutTransform(Center - FVector2D(HighlightSize, HighlightSize))),
			WhiteBox, ESlateDrawEffect::None,
			FLinearColor(0.3f, 0.6f, 1.0f, 0.6f)
		);
	}

	// Center filled box for the reroute node
	float CircleSize = HalfSize * 1.4f;
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId + 1,
		Geom.ToPaintGeometry(FVector2D(CircleSize, CircleSize),
			FSlateLayoutTransform(Center - FVector2D(CircleSize * 0.5f, CircleSize * 0.5f))),
		WhiteBox, ESlateDrawEffect::None,
		PinColor
	);

	// Dark inner dot to show it's a passthrough
	float DotR = 3.0f * ZoomLevel;
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId + 2,
		Geom.ToPaintGeometry(FVector2D(DotR * 2, DotR * 2),
			FSlateLayoutTransform(Center - FVector2D(DotR, DotR))),
		WhiteBox, ESlateDrawEffect::None,
		FLinearColor(0.05f, 0.05f, 0.06f)
	);
}

int32 SWorkflowGraphEditor::HitTestConnection(FVector2D LocalPos, float Tolerance) const
{
	const float ToleranceSq = Tolerance * Tolerance * ZoomLevel * ZoomLevel;

	for (int32 ConnIdx = 0; ConnIdx < Connections.Num(); ++ConnIdx)
	{
		const FGraphConnection& Conn = Connections[ConnIdx];

		const int32* SrcIdx = NodeIndexMap.Find(Conn.SourceNodeId);
		const int32* DstIdx = NodeIndexMap.Find(Conn.TargetNodeId);
		if (!SrcIdx || !DstIdx) continue;

		const FGraphNode& SrcNode = Nodes[*SrcIdx];
		const FGraphNode& DstNode = Nodes[*DstIdx];

		// Get start position (output pin)
		FVector2D StartPos = FVector2D::ZeroVector;
		if (Conn.SourceOutputIndex < SrcNode.OutputPins.Num())
		{
			StartPos = GetPinPosition(SrcNode, SrcNode.OutputPins[Conn.SourceOutputIndex]);
		}
		else
		{
			continue;
		}

		// Get end position (input pin)
		FVector2D EndPos = FVector2D::ZeroVector;
		bool bFoundPin = false;
		for (const auto& Pin : DstNode.InputPins)
		{
			if (Pin.Name == Conn.TargetInputName)
			{
				EndPos = GetPinPosition(DstNode, Pin);
				bFoundPin = true;
				break;
			}
		}
		if (!bFoundPin) continue;

		// Sample the bezier curve and check distance to the local pos
		float TangentLength = FMath::Abs(EndPos.X - StartPos.X) * 0.5f;
		TangentLength = FMath::Clamp(TangentLength, 40.0f, 200.0f);

		FVector2D P0 = StartPos;
		FVector2D P1 = StartPos + FVector2D(TangentLength, 0.0f);
		FVector2D P2 = EndPos - FVector2D(TangentLength, 0.0f);
		FVector2D P3 = EndPos;

		const int32 NumSamples = 32;
		for (int32 i = 0; i <= NumSamples; ++i)
		{
			float t = static_cast<float>(i) / NumSamples;
			float u = 1.0f - t;
			FVector2D Pt = u * u * u * P0 + 3.0f * u * u * t * P1 + 3.0f * u * t * t * P2 + t * t * t * P3;

			if ((Pt - LocalPos).SizeSquared() <= ToleranceSq)
			{
				return ConnIdx;
			}
		}
	}

	return -1;
}

FVector2D SWorkflowGraphEditor::EvalConnectionBezier(const FGraphConnection& Conn, float t) const
{
	const int32* SrcIdx = NodeIndexMap.Find(Conn.SourceNodeId);
	const int32* DstIdx = NodeIndexMap.Find(Conn.TargetNodeId);
	if (!SrcIdx || !DstIdx) return FVector2D::ZeroVector;

	const FGraphNode& SrcNode = Nodes[*SrcIdx];
	const FGraphNode& DstNode = Nodes[*DstIdx];

	FVector2D StartPos = FVector2D::ZeroVector;
	if (Conn.SourceOutputIndex < SrcNode.OutputPins.Num())
	{
		StartPos = GetPinPosition(SrcNode, SrcNode.OutputPins[Conn.SourceOutputIndex]);
	}

	FVector2D EndPos = FVector2D::ZeroVector;
	for (const auto& Pin : DstNode.InputPins)
	{
		if (Pin.Name == Conn.TargetInputName)
		{
			EndPos = GetPinPosition(DstNode, Pin);
			break;
		}
	}

	float TangentLength = FMath::Abs(EndPos.X - StartPos.X) * 0.5f;
	TangentLength = FMath::Clamp(TangentLength, 40.0f, 200.0f);

	FVector2D P0 = StartPos;
	FVector2D P1 = StartPos + FVector2D(TangentLength, 0.0f);
	FVector2D P2 = EndPos - FVector2D(TangentLength, 0.0f);
	FVector2D P3 = EndPos;

	float u = 1.0f - t;
	return u * u * u * P0 + 3.0f * u * u * t * P1 + 3.0f * u * t * t * P2 + t * t * t * P3;
}

void SWorkflowGraphEditor::ResolveRerouteSource(const FString& NodeId, FString& OutRealSourceId, int32& OutRealOutputIndex) const
{
	// Walk upstream through reroute chains (with a safety limit to prevent infinite loops)
	FString CurrentId = NodeId;
	int32 Safety = 0;

	while (Safety++ < 50)
	{
		const int32* Idx = NodeIndexMap.Find(CurrentId);
		if (!Idx) break;

		const FGraphNode& Node = Nodes[*Idx];
		if (!Node.IsReroute())
		{
			// Found a real node — this is the actual source
			OutRealSourceId = CurrentId;
			// OutRealOutputIndex stays as whatever the connection into this was
			return;
		}

		// This is a reroute — find what connects to its input
		bool bFoundUpstream = false;
		for (const FGraphConnection& Conn : Connections)
		{
			if (Conn.TargetNodeId == CurrentId && Conn.TargetInputName == TEXT("in"))
			{
				CurrentId = Conn.SourceNodeId;
				OutRealOutputIndex = Conn.SourceOutputIndex;
				bFoundUpstream = true;
				break;
			}
		}

		if (!bFoundUpstream) break;
	}

	// Fallback: couldn't resolve, keep whatever we have
	OutRealSourceId = CurrentId;
}

// ============================================================================
// Reroute pin positions (centered on the node)
// ============================================================================

// Override GetPinPosition behavior for reroute nodes is handled by the existing
// method — reroute nodes have size 24x24 so pins land at the left/right center.

#undef LOCTEXT_NAMESPACE
