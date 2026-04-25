// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ComfyNodeDatabase.h"
#include "Dom/JsonObject.h"

// Forward declarations
class SEditableTextBox;
class FMeshPreviewRenderer;
class SMultiLineEditableTextBox;
class SMenuAnchor;
class SWindow;

// ============================================================================
// Graph Data Model
// ============================================================================

/** A single pin (input or output) on a graph node instance */
struct FGraphPin
{
	/** Name of this pin (e.g. "model", "clip", "positive") */
	FString Name;

	/** Data type (e.g. "MODEL", "CLIP", "IMAGE", "FLOAT", "STRING") */
	FString Type;

	/** True = input pin, False = output pin */
	bool bIsInput = true;

	/** Index of this pin within the node's input or output list */
	int32 PinIndex = 0;

	/** ID of the owning node */
	FString OwnerNodeId;

	/** Unique ID for this pin: "NodeId.Input.PinName" or "NodeId.Output.PinName" */
	FString GetPinId() const
	{
		return FString::Printf(TEXT("%s.%s.%s"), *OwnerNodeId, bIsInput ? TEXT("In") : TEXT("Out"), *Name);
	}
};

/** A connection between two pins */
struct FGraphConnection
{
	FString SourceNodeId;
	int32 SourceOutputIndex = 0;
	FString TargetNodeId;
	FString TargetInputName;

	bool operator==(const FGraphConnection& Other) const
	{
		return SourceNodeId == Other.SourceNodeId &&
			SourceOutputIndex == Other.SourceOutputIndex &&
			TargetNodeId == Other.TargetNodeId &&
			TargetInputName == Other.TargetInputName;
	}
};

/** Special class_type string for reroute/redirect nodes (visual-only, not sent to ComfyUI) */
static const FString RerouteClassType = TEXT("__Reroute__");

/** Special class_type strings for UE source nodes (resolved to real ComfyUI nodes at export) */
static const FString UEViewportClassType = TEXT("__UE_ViewportCapture__");
static const FString UEDepthMapClassType = TEXT("__UE_DepthMap__");
static const FString UECameraDataClassType = TEXT("__UE_CameraData__");
static const FString UESegmentationClassType = TEXT("__UE_Segmentation__");
static const FString UEMeshyImportClassType = TEXT("__UE_MeshyImport__");
static const FString UESave3DModelClassType = TEXT("__UE_Save3DModel__");
static const FString UE3DLoaderClassType = TEXT("__UE_3DLoader__");
static const FString UEImageBridgeClassType = TEXT("__UE_ImageBridge__");
static const FString UE3DAssetExportClassType = TEXT("__UE_3DAssetExport__");
static const FString UEPromptAdherenceClassType = TEXT("__UE_PromptAdherence__");
static const FString UEImageUpresClassType = TEXT("__UE_ImageUpres__");
static const FString UESequenceClassType = TEXT("__UE_Sequence__");
static const FString UEVideoToImageClassType = TEXT("__UE_VideoToImage__");

/** Marker filenames emitted by ExportWorkflowJSON for UE source nodes.
 *  The caller is responsible for replacing these with real uploaded filenames. */
static const FString UEViewportMarker = TEXT("__UE_VIEWPORT_IMAGE__");
static const FString UEDepthMapMarker = TEXT("__UE_DEPTH_IMAGE__");
static const FString UESegmentationMarker = TEXT("__UE_SEGMENTATION_IMAGE__");
/** Per-node marker prefix for UE Video to Image nodes. Full marker: prefix + nodeId */
static const FString UEVideoFrameMarkerPrefix = TEXT("__UE_VIDEO_FRAME__");

/** A single node instance in the graph editor */
struct FGraphNode
{
	/** Unique ID for this node (used as the key in the ComfyUI workflow JSON) */
	FString Id;

	/** The class_type (e.g. "CheckpointLoaderSimple", "KSampler") */
	FString ClassType;

	/** User-editable display title */
	FString Title;

	/** Position in graph space (user-draggable) */
	FVector2D Position = FVector2D::ZeroVector;

	/** Size computed from content */
	FVector2D Size = FVector2D(200.0f, 80.0f);

	/** Widget input values: input_name -> value_string */
	TMap<FString, FString> WidgetValues;

	/** Widget input definitions (parallel to WidgetValues — stores type, options, min/max) */
	TMap<FString, FComfyInputDef> WidgetInputDefs;

	/** Ordered list of widget names (preserves insertion order for layout) */
	TArray<FString> WidgetOrder;

	/** Input pins (link-type inputs from the node def) */
	TArray<FGraphPin> InputPins;

	/** Output pins */
	TArray<FGraphPin> OutputPins;

	/** Colour for the header bar */
	FLinearColor HeaderColor = FLinearColor(0.4f, 0.4f, 0.4f);

	/** Whether this node is currently selected */
	bool bSelected = false;

	/** Cached pointer to the node definition (may be null if unknown type) */
	const FComfyNodeDef* NodeDef = nullptr;

	/** Thumbnail brush for LoadImage nodes (rendered on the node and in details) */
	TSharedPtr<FSlateBrush> ThumbnailBrush;

	/** Raw texture pointer for ThumbnailBrush — used to refresh TObjectPtr handles
	 *  after GC compaction without calling GetResourceObject() (which would crash
	 *  on a stale packed index). Always kept in sync with ThumbnailBrush. */
	UTexture2D* ThumbnailTexture = nullptr;

	/** 3D mesh preview renderer for Save3DModel and similar nodes */
	TSharedPtr<FMeshPreviewRenderer> MeshPreview;

	/** Local disk path of the last browsed file (for video nodes — enables "Play" button) */
	FString LocalFilePath;

	/** Whether this node is currently executing in ComfyUI */
	bool bIsExecuting = false;

	/** Whether this is a reroute/redirect node (visual-only passthrough) */
	bool IsReroute() const { return ClassType == RerouteClassType; }

	/** Whether this is a UE source node (viewport capture, depth map, camera data, or segmentation) */
	bool IsUESourceNode() const
	{
		return ClassType == UEViewportClassType
			|| ClassType == UEDepthMapClassType
			|| ClassType == UECameraDataClassType
			|| ClassType == UESegmentationClassType
			|| ClassType == UEMeshyImportClassType
			|| ClassType == UESave3DModelClassType
			|| ClassType == UE3DLoaderClassType
			|| ClassType == UEImageBridgeClassType
			|| ClassType == UE3DAssetExportClassType
			|| ClassType == UEPromptAdherenceClassType
			|| ClassType == UEImageUpresClassType
			|| ClassType == UESequenceClassType
			|| ClassType == UEVideoToImageClassType;
	}
};

// ============================================================================
// Graph Editor Widget
// ============================================================================

/**
 * Full interactive ComfyUI-style graph editor.
 *
 * Supports:
 * - Node dragging and repositioning
 * - Connection wire dragging between pins
 * - Right-click context menu to add/delete nodes
 * - External details panel for parameter editing (via OnSelectionChanged)
 * - Exporting the graph as ComfyUI workflow JSON
 * - Pan (right-drag / middle-drag) and zoom (scroll wheel)
 */
class SWorkflowGraphEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorkflowGraphEditor) {}
		/** Called whenever the graph changes (node added/removed, connection changed, value edited) */
		SLATE_EVENT(FSimpleDelegate, OnGraphChanged)
		/** Called whenever the node selection changes (for external details panel) */
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWorkflowGraphEditor();

	// ---- Public API ----

	/** Clear the graph and start fresh */
	void ClearGraph();

	/** Add a node to the graph by class_type. Returns the new node's ID. */
	FString AddNodeByType(const FString& ClassType, FVector2D GraphPosition);

	/** Remove a node and all its connections */
	void RemoveNode(const FString& NodeId);

	/** Add a connection between two pins */
	bool AddConnection(const FString& SourceNodeId, int32 SourceOutputIndex,
		const FString& TargetNodeId, const FString& TargetInputName);

	/** Remove a specific connection */
	void RemoveConnection(const FString& TargetNodeId, const FString& TargetInputName);

	/** Remove all connections involving a specific node */
	void RemoveConnectionsForNode(const FString& NodeId);

	/** Export the current graph as a ComfyUI workflow JSON object.
	 *  UE source nodes are resolved:
	 *  - UE_ViewportCapture -> LoadImage with image="__UE_VIEWPORT_IMAGE__"
	 *  - UE_DepthMap -> LoadImage with image="__UE_DEPTH_IMAGE__"
	 *  - UE_CameraData -> its CameraDescription string is appended to connected text inputs
	 *  The caller should replace marker filenames with real uploaded filenames before submitting.
	 *  @param OutNeedsViewport      Set to true if the workflow requires a viewport capture
	 *  @param OutNeedsDepth         Set to true if the workflow requires a depth map capture
	 *  @param OutCameraDescription  Filled with the camera prompt string if a CameraData node exists
	 *  @param OutNeedsSegmentation  Set to true if the workflow requires a segmentation mask capture
	 */
	TSharedPtr<FJsonObject> ExportWorkflowJSON(bool* OutNeedsViewport = nullptr,
		bool* OutNeedsDepth = nullptr, FString* OutCameraDescription = nullptr,
		bool* OutNeedsSegmentation = nullptr) const;

	/** Import a ComfyUI workflow JSON and rebuild the graph */
	void ImportWorkflowJSON(TSharedPtr<FJsonObject> Workflow);

	/** Get all graph nodes (read-only) */
	const TArray<FGraphNode>& GetNodes() const { return Nodes; }

	/** Get all connections (read-only) */
	const TArray<FGraphConnection>& GetConnections() const { return Connections; }

	/** Check if the graph contains a MeshyImageToModelNode and return the filename
	 *  of the image connected to its input pin.
	 *  Returns empty string if no Meshy node or no image source found. */
	FString GetMeshySourceImageFilename() const;

	/** Check if the graph contains any UE source nodes */
	bool HasUESourceNodes() const;

	/** Check if the graph contains a UE Sequence node */
	bool HasSequenceNode() const;

	/** Export the graph as multiple staged workflows (one per Sequence step).
	 *  Each step's workflow contains only the nodes downstream of that step's output pin.
	 *  Nodes not connected to any Sequence output go into stage 0 (or the single workflow if
	 *  no Sequence node exists).
	 *  The caller is responsible for submitting each workflow sequentially. */
	TArray<TSharedPtr<FJsonObject>> ExportStagedWorkflows(
		bool* OutNeedsViewport, bool* OutNeedsDepth,
		FString* OutCameraDescription, bool* OutNeedsSegmentation) const;

	/** Get the ID of the primary selected node (empty if nothing selected) */
	FString GetPrimarySelectedNodeId() const;

	/** Get a pointer to a node by ID (mutable — for details panel editing). Returns null if not found. */
	FGraphNode* FindNodeById(const FString& NodeId);

	/** Set a widget value on a specific node (public wrapper around CommitWidgetValue).
	 *  Returns true if the node and widget exist and were updated. */
	bool SetNodeWidgetValue(const FString& NodeId, const FString& WidgetName, const FString& NewValue);

	/** Commit a new value to a widget (called by the external details panel).
	 *  Handles type coercion (clamping, combo validation) before storing. */
	void CommitWidgetValue(const FString& NodeId, const FString& WidgetName, const FString& NewValue);

	/** Set a thumbnail texture on a node (used for LoadImage preview).
	 *  Creates a FSlateBrush from the texture and recomputes the node size. */
	void SetNodeThumbnail(const FString& NodeId, UTexture2D* Texture);

	/** Set a 3D mesh preview on a node. Creates a FMeshPreviewRenderer if needed,
	 *  renders the mesh, and recomputes the node size. */
	void SetNodeMeshPreview(const FString& NodeId, UStaticMesh* Mesh);

	/** Set which node is currently executing (empty string clears all).
	 *  The active node gets a yellow highlight in the graph. */
	void SetExecutingNode(const FString& NodeId);

	/** Clear execution highlight from all nodes */
	void ClearExecutingNodes();

	/** Set overlay text drawn in the upper-left corner of the graph (e.g. cost estimate).
	 *  Pass empty string to clear. */
	void SetOverlayText(const FString& Text) { OverlayText = Text; }

	/** Commit a widget value silently — validates and stores the value and marks the graph dirty,
	 *  but does NOT fire the OnGraphChanged delegate.  Use this from the details panel to avoid
	 *  a rebuild→focus-loss cycle that can close the fullscreen window or cause flickering. */
	void CommitWidgetValueSilent(const FString& NodeId, const FString& WidgetName, const FString& NewValue);

	/** Update prompt-like widgets across ALL nodes in the graph.
	 *  Targets widgets named: text, prompt, positive, negative, positive_prompt,
	 *  negative_prompt, pos_prompt, neg_prompt, string (STRING type only).
	 *  Uses the widget name and node title to determine positive vs negative.
	 *  @return The number of widgets updated. */
	int32 SetAllPromptTexts(const FString& PositivePrompt, const FString& NegativePrompt);

	// ---- Save / Load ----

	/** Serialize the entire editor graph (nodes, connections, positions, UE source nodes) to a JSON file.
	 *  Returns true on success. */
	bool SaveGraphToFile(const FString& FilePath);

	/** Load a previously saved editor graph from a JSON file.
	 *  Returns true on success. */
	bool LoadGraphFromFile(const FString& FilePath);

	/** Serialize the editor graph to a JSON object (for embedding or programmatic use) */
	TSharedPtr<FJsonObject> SerializeGraph() const;

	/** Deserialize and rebuild the graph from a previously-serialized JSON object */
	bool DeserializeGraph(TSharedPtr<FJsonObject> GraphJson);

	/** Get the currently loaded file path (empty if unsaved) */
	const FString& GetCurrentFilePath() const { return CurrentFilePath; }

	/** Whether the graph has been modified since last save */
	bool IsDirty() const { return bGraphDirty; }

	/** Build a preset graph from the current UGenAISettings (backward compat) */
	void BuildPresetGraph();

	/** Auto-layout the current graph (topological left-to-right) */
	void AutoLayout();

	/** Insert a reroute node into the connection at the given graph position */
	FString InsertRerouteNode(int32 ConnectionIndex, FVector2D GraphPosition);

	// ---- SWidget Overrides ----
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual bool IsInteractable() const override { return true; }

private:
	// ---- Overlay ----
	FString OverlayText;

	// ---- Graph Data ----
	TArray<FGraphNode> Nodes;
	TMap<FString, int32> NodeIndexMap;  // Id -> index in Nodes
	TArray<FGraphConnection> Connections;
	int32 NextAutoNodeId = 100;

	// ---- Undo / Redo ----
	/** Snapshot-based undo stack. Each entry is a serialized graph JSON. */
	TArray<TSharedPtr<FJsonObject>> UndoStack;
	TArray<TSharedPtr<FJsonObject>> RedoStack;
	static constexpr int32 MaxUndoLevels = 50;
	/** True while restoring a snapshot — suppresses pushing to the undo stack. */
	bool bIsRestoringSnapshot = false;
	/** Push the current graph state onto the undo stack. Called by NotifyGraphChanged. */
	void PushUndoSnapshot();
	/** Restore the graph from a JSON snapshot without triggering a new undo push. */
	void RestoreSnapshot(TSharedPtr<FJsonObject> Snapshot);
public:
	/** Undo the last graph change. */
	void Undo();
	/** Redo the last undone change. */
	void Redo();
	/** Returns true if there are undo states available. */
	bool CanUndo() const { return UndoStack.Num() > 0; }
	/** Returns true if there are redo states available. */
	bool CanRedo() const { return RedoStack.Num() > 0; }
private:

	// ---- Interaction State ----
	enum class EInteractionMode : uint8
	{
		None,
		Panning,
		DraggingNode,
		DraggingConnection,
		BoxSelecting
	};

	EInteractionMode InteractionMode = EInteractionMode::None;

	// Pan/Zoom
	mutable FVector2D ViewOffset = FVector2D::ZeroVector;
	mutable float ZoomLevel = 1.0f;
	FVector2D LastMousePos = FVector2D::ZeroVector;

	// Node dragging
	FString DraggedNodeId;
	FVector2D DragOffset = FVector2D::ZeroVector;

	// Connection dragging
	bool bDraggingFromOutput = true;
	FString DragSourceNodeId;
	int32 DragSourcePinIndex = 0;
	FString DragSourcePinType;
	mutable FVector2D DragConnectionEnd = FVector2D::ZeroVector;

	// Box selection
	FVector2D BoxSelectStart = FVector2D::ZeroVector;
	mutable FVector2D BoxSelectEnd = FVector2D::ZeroVector;

	// Connection hover highlight (-1 = none)
	mutable int32 HoveredConnectionIndex = -1;

	// Mesh preview interaction state
	FString MeshPreviewInteractNodeId;   // Node whose preview is being orbited/zoomed/panned
	FVector2D MeshPreviewLastMousePos;    // Last mouse position during preview interaction
	bool bMeshPreviewOrbiting = false;
	bool bMeshPreviewPanning = false;

	// Selection
	TSet<FString> SelectedNodeIds;

	// Save/Load state
	FString CurrentFilePath;  // Path of the currently loaded/saved file
	bool bGraphDirty = false; // Whether the graph has unsaved changes

	// Callbacks
	FSimpleDelegate OnGraphChanged;
	FSimpleDelegate OnSelectionChanged;

	// ---- GC Safety ----

	/** Handle for PostGarbageCollect delegate — used to refresh stale TObjectPtr handles
	 *  in heap-allocated FSlateBrush objects after GC compaction changes UObject indices. */
	FDelegateHandle PostGCDelegateHandle;

	/** Refresh all FSlateBrush ResourceObject handles from stored raw pointers.
	 *  Called after GC to prevent stale TObjectPtr packed indices from crashing Slate paint. */
	void OnPostGarbageCollect();

	// ---- Coordinate Helpers ----
	FVector2D GraphToLocal(FVector2D GraphPos) const;
	FVector2D LocalToGraph(FVector2D LocalPos) const;

	// ---- Hit Testing ----

	/** Returns the node ID at the given local position, or empty string if none */
	FString HitTestNode(FVector2D LocalPos) const;

	/** Returns the pin at the given local position, populating OutPin. Returns false if no pin hit. */
	bool HitTestPin(FVector2D LocalPos, FGraphPin& OutPin) const;

	/** Compute the local-space position of a pin */
	FVector2D GetPinPosition(const FGraphNode& Node, const FGraphPin& Pin) const;

	// ---- Node Construction ----

	/** Create a graph node from a ComfyNodeDef, populating pins and default widget values */
	FGraphNode CreateNodeFromDef(const FComfyNodeDef& Def, const FString& Id, FVector2D Position) const;

	/** Create a UE source node (Viewport Capture, Depth Map, or Camera Data) */
	FGraphNode CreateUESourceNode(const FString& UEClassType, const FString& Id, FVector2D Position) const;

	/** Compute the rendered size of a node based on its content */
	void ComputeNodeSize(FGraphNode& Node) const;

	/** Get the semantic header colour for a class_type */
	static FLinearColor GetNodeColor(const FString& ClassType);

	// ---- Drawing Helpers ----
	void DrawNode(const FGraphNode& Node, const FGeometry& Geom,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	void DrawConnection(FVector2D Start, FVector2D End, FLinearColor Color,
		const FGeometry& Geom, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	void DrawPin(FVector2D Center, float Radius, bool bConnected, bool bIsInput,
		const FString& Type, const FGeometry& Geom,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	// ---- Reroute Node Helpers ----

	/** Create a reroute node with the given data type */
	FGraphNode CreateRerouteNode(const FString& Id, const FString& DataType, FVector2D Position) const;

	/** Draw a compact reroute node (small diamond instead of full box) */
	void DrawRerouteNode(const FGraphNode& Node, const FGeometry& Geom,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Hit-test a connection bezier curve. Returns the connection index or -1 if none hit. */
	int32 HitTestConnection(FVector2D LocalPos, float Tolerance = 8.0f) const;

	/** Compute a point on the bezier curve for a connection at parameter t */
	FVector2D EvalConnectionBezier(const FGraphConnection& Conn, float t) const;

	/** Resolve reroute chains: given a reroute node, follow reroute links upstream
	 *  to find the real source node/output. Used during JSON export. */
	void ResolveRerouteSource(const FString& NodeId, FString& OutRealSourceId, int32& OutRealOutputIndex) const;

	// ---- Context Menu ----
	void ShowAddNodeMenu(FVector2D ScreenPos, FVector2D GraphPos);

	/** Build a hierarchical context menu from the node database categories */
	TSharedRef<SWidget> BuildNodeCategoryMenu(FVector2D GraphPos);

	/** Build a filtered node menu showing only nodes with a compatible pin.
	 *  Used when a connection drag is dropped on empty space. The newly created
	 *  node is auto-connected to the source pin. */
	TSharedRef<SWidget> BuildFilteredNodeMenu(FVector2D GraphPos,
		const FString& PinType, bool bFromOutput,
		const FString& SourceNodeId, int32 SourcePinIndex);

	/** Fire the OnSelectionChanged delegate */
	void NotifySelectionChanged();

	// ---- Helpers ----
	void NotifyGraphChanged();

	/** Check if an input pin already has a connection */
	bool IsInputConnected(const FString& NodeId, const FString& InputName) const;

	/** Check if two types are compatible for connection */
	static bool AreTypesCompatible(const FString& OutputType, const FString& InputType);

	/** Topological sort helper */
	TMap<FString, int32> ComputeNodeDepths() const;
};
