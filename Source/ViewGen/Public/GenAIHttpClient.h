// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "IWebSocket.h"

DECLARE_DELEGATE_TwoParams(FOnGenerationComplete, bool /*bSuccess*/, UTexture2D* /*ResultTexture*/);
DECLARE_DELEGATE_OneParam(FOnGenerationProgress, float /*Progress 0-1*/);
DECLARE_DELEGATE_OneParam(FOnGenerationError, const FString& /*ErrorMessage*/);

/** Delegate fired when the currently executing node changes (node ID from the workflow) */
DECLARE_DELEGATE_OneParam(FOnNodeExecuting, const FString& /*NodeId*/);

/** Delegate fired when SAM3 segmentation returns multiple mask images + visualization */
DECLARE_DELEGATE_ThreeParams(FOnSegmentationComplete,
	UTexture2D* /*VisualizationTexture*/,
	const TArray<UTexture2D*>& /*MaskTextures*/,
	UTexture2D* /*OriginalTexture*/);

struct FLoRAEntry;

/** Options discovered from a ComfyUI node's combo inputs */
struct FGeminiNodeOptions
{
	TArray<FString> Models;
	TArray<FString> AspectRatios;
	TArray<FString> Resolutions;
	TArray<FString> ResponseModalities;
	TArray<FString> ThinkingLevels;
};

/** Options discovered from the KlingImageGenerationNode combo inputs */
struct FKlingNodeOptions
{
	TArray<FString> Models;        // kling-v3, kling-v2, kling-v1-5
	TArray<FString> AspectRatios;  // 16:9, 9:16, 1:1, 4:3, 3:4, 3:2, 2:3, 21:9
	TArray<FString> ImageTypes;    // subject, face
};

/** Options discovered from the KlingImage2VideoNode combo inputs */
struct FKlingVideoNodeOptions
{
	TArray<FString> Models;       // kling-v2-master, kling-v2-1, kling-v2-5-turbo, etc.
	TArray<FString> Modes;        // std, pro
};

/** Options discovered from the WanImageToVideoApi combo inputs */
struct FWanVideoNodeOptions
{
	TArray<FString> Models;       // wan2.5-i2v-preview, wan2.6-i2v
	TArray<FString> Resolutions;  // 480P, 720P, 1080P
};

/** Options discovered from the Veo3VideoGenerationNode combo inputs */
struct FVeo3NodeOptions
{
	TArray<FString> Models;            // veo-3.1-generate, veo-3.0-generate-001, etc.
	TArray<FString> AspectRatios;      // 16:9, 9:16
	TArray<FString> PersonGenerations; // ALLOW, DONT_ALLOW
};

/**
 * HTTP client for ComfyUI's native /prompt API.
 *
 * Flow:
 * 1. Upload input images via /upload/image
 * 2. Build a ComfyUI workflow JSON (node graph)
 * 3. Submit via POST /prompt -> get prompt_id
 * 4. Poll GET /history/{prompt_id} until complete
 * 5. Fetch result image via GET /view?filename=...
 */
class FGenAIHttpClient
{
public:
	FGenAIHttpClient();
	~FGenAIHttpClient();

	/**
	 * Send an img2img generation request via ComfyUI.
	 * Uploads images, builds workflow, submits prompt.
	 */
	void SendImg2ImgRequest(
		const FString& ViewportImageBase64,
		const FString& DepthImageBase64,
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs);

	/**
	 * Send a txt2img request (no viewport input).
	 */
	void SendTxt2ImgRequest(
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs);

	/**
	 * Send a depth-guided txt2img request (depth ControlNet + prompt, no viewport img2img).
	 * Generates from prompt only but uses depth map for structural guidance.
	 */
	void SendDepthOnlyRequest(
		const FString& DepthImageBase64,
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs);

	/**
	 * Send a Gemini (Nano Banana 2) generation request.
	 * Uses the NanoBanana2AIO node with optional viewport and depth images as references.
	 */
	void SendGeminiRequest(
		const FString& ViewportImageBase64,
		const FString& DepthImageBase64,
		const FString& Prompt);

	/**
	 * Send a Kling image generation request.
	 * Uses the KlingImageGenerationNode with optional viewport image as reference.
	 */
	void SendKlingRequest(
		const FString& ViewportImageBase64,
		const FString& Prompt,
		const FString& NegativePrompt);

	/**
	 * Send a Kling Video generation request.
	 * Uses KlingImage2VideoNode with source frame as first frame.
	 */
	void SendKlingVideoRequest(
		const FString& SourceFrameBase64,
		const FString& Prompt,
		const FString& NegativePrompt);

	/**
	 * Send a Veo3 video generation request.
	 * Uses the Veo3VideoGenerationNode with source frame as reference image.
	 */
	void SendVeo3Request(
		const FString& SourceFrameBase64,
		const FString& Prompt);

	/**
	 * Send a Wan I2V video generation request.
	 * Uses the WanImageToVideoApi node with source frame as input.
	 */
	void SendWanVideoRequest(
		const FString& SourceFrameBase64,
		const FString& Prompt,
		const FString& NegativePrompt);

	/**
	 * Send a SAM3 segmentation request to ComfyUI.
	 * Uploads the source image, builds a SAM3Grounding workflow, submits it.
	 * Results (masks + visualization) are returned via OnSegmentationComplete.
	 * @param SourceTexture  The generated image to segment
	 * @param TextPrompt     Text describing objects to segment (e.g. "person . car . tree")
	 */
	void SendSAM3SegmentationRequest(UTexture2D* SourceTexture, const FString& TextPrompt);

	/** Cancel any in-flight request */
	void CancelRequest();

	/** Whether a request is currently in progress */
	bool IsRequestInProgress() const { return bRequestInProgress; }

	/** Callbacks */
	FOnGenerationComplete OnComplete;
	FOnGenerationProgress OnProgress;
	FOnGenerationError OnError;
	FOnSegmentationComplete OnSegmentationComplete;
	FOnNodeExecuting OnNodeExecuting;

	/** Disconnect WebSocket (call when generation completes or errors) */
	void DisconnectWebSocket();

	/** Poll ComfyUI for generation progress */
	void PollProgress();

	/**
	 * Callback for when model lists have been fetched from ComfyUI.
	 * Arrays contain model filenames exactly as ComfyUI reports them.
	 */
	DECLARE_DELEGATE_FiveParams(FOnModelListsFetched,
		const TArray<FString>& /*Checkpoints*/,
		const TArray<FString>& /*LoRAs*/,
		const TArray<FString>& /*ControlNets*/,
		const FGeminiNodeOptions& /*GeminiOptions*/,
		const FKlingNodeOptions& /*KlingOptions*/);

	FOnModelListsFetched OnModelListsFetched;

	/**
	 * Query ComfyUI /object_info to discover installed checkpoints, LoRAs, and ControlNet models.
	 * Results arrive asynchronously via OnModelListsFetched.
	 * Also populates the FComfyNodeDatabase singleton with all node definitions.
	 */
	void FetchAvailableModels();

	/**
	 * Submit a pre-built workflow JSON directly to ComfyUI.
	 * Used by the graph editor to send user-constructed workflows.
	 */
	void SubmitWorkflowDirect(TSharedPtr<FJsonObject> Workflow);

	/**
	 * Upload a base64 PNG to ComfyUI via /upload/image.
	 * Public so the graph editor can upload viewport/depth images before submitting.
	 * Returns the server-side filename on success.
	 */
	bool UploadImage(const FString& Base64PNG, const FString& DesiredFilename, FString& OutServerFilename);

	/**
	 * Upload a raw file (video, etc.) to ComfyUI via /upload/image.
	 * Unlike UploadImage, this takes raw file bytes and a content type — no base64/PNG conversion.
	 * Returns the server-side filename on success.
	 */
	bool UploadRawFile(const TArray<uint8>& FileBytes, const FString& DesiredFilename, const FString& ContentType, FString& OutServerFilename);

	/**
	 * Fetch an uploaded/input image from ComfyUI via GET /view as a thumbnail.
	 * Lightweight and independent of the generation pipeline.
	 * @param ImageFilename  The server-side filename (e.g. "my_image.png")
	 * @param OnComplete     Callback with the decoded UTexture2D* (null on failure)
	 */
	void FetchImageThumbnail(const FString& ImageFilename, TFunction<void(UTexture2D*)> OnThumbnailReady);

	/** Download an image from ComfyUI's input folder and return it as base64-encoded PNG.
	 *  @param ImageFilename  The filename in ComfyUI's input folder
	 *  @param OnBase64Ready  Callback with the base64 string (empty on failure)
	 */
	void DownloadComfyUIImageAsBase64(const FString& ImageFilename, TFunction<void(const FString&)> OnBase64Ready);

	/** Download any file from ComfyUI's output/temp folder and save it to a local path.
	 *  @param Filename       The filename in ComfyUI's output
	 *  @param Subfolder      The subfolder (empty string for root)
	 *  @param FolderType     "output" or "temp"
	 *  @param LocalSavePath  Full local path to save the downloaded file
	 *  @param OnComplete     Callback with the local path on success (empty on failure)
	 */
	void DownloadComfyUIFile(const FString& Filename, const FString& Subfolder,
		const FString& FolderType, const FString& LocalSavePath,
		TFunction<void(const FString&)> OnDownloadComplete);

private:
	// ---- Image Upload ----

	/** Upload a base64 PNG image to ComfyUI via /upload/image. Returns the server filename. */
	bool UploadImageToComfyUI(const FString& Base64PNG, const FString& DesiredFilename, FString& OutServerFilename);

	// ---- Workflow Building ----

	/** Build a ComfyUI workflow for img2img with optional ControlNet and LoRAs */
	TSharedPtr<FJsonObject> BuildImg2ImgWorkflow(
		const FString& ViewportFilename,
		const FString& DepthFilename,
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs) const;

	/** Build a ComfyUI workflow for txt2img with optional LoRAs */
	TSharedPtr<FJsonObject> BuildTxt2ImgWorkflow(
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs) const;

	/** Build a ComfyUI workflow for depth-guided txt2img (ControlNet depth + empty latent) */
	TSharedPtr<FJsonObject> BuildDepthOnlyWorkflow(
		const FString& DepthFilename,
		const FString& Prompt,
		const FString& NegativePrompt,
		const TArray<FLoRAEntry>& ActiveLoRAs) const;

	/** Build a ComfyUI workflow for Gemini (NanoBanana2AIO) with optional reference images */
	TSharedPtr<FJsonObject> BuildGeminiWorkflow(
		const FString& ViewportFilename,
		const FString& DepthFilename,
		const FString& Prompt) const;

	/** Build a ComfyUI workflow for Kling (KlingImageGenerationNode) with optional reference image */
	TSharedPtr<FJsonObject> BuildKlingWorkflow(
		const FString& ViewportFilename,
		const FString& Prompt,
		const FString& NegativePrompt) const;

	/** Build a ComfyUI workflow for Kling Video (KlingImage2VideoNode) with source frame */
	TSharedPtr<FJsonObject> BuildKlingVideoWorkflow(
		const FString& SourceFrameFilename,
		const FString& Prompt,
		const FString& NegativePrompt) const;

	/** Build a ComfyUI workflow for Veo3 (Veo3VideoGenerationNode) with source frame */
	TSharedPtr<FJsonObject> BuildVeo3Workflow(
		const FString& SourceFrameFilename,
		const FString& Prompt) const;

	/** Build a ComfyUI workflow for Wan I2V (WanImageToVideoApi) with source frame */
	TSharedPtr<FJsonObject> BuildWanVideoWorkflow(
		const FString& SourceFrameFilename,
		const FString& Prompt,
		const FString& NegativePrompt) const;

	/**
	 * Append Hi-Res Fix nodes to a workflow: LatentUpscale -> second KSampler -> VAEDecode -> SaveImage.
	 * Replaces the existing VAEDecode and SaveImage nodes with the upscaled versions.
	 * VAESourceId + VAEOutputIndex specify where to get the VAE from (checkpoint output 2 for SD,
	 * or a dedicated VAELoader output 0 for Flux).
	 */
	void AppendHiResFix(
		TSharedPtr<FJsonObject> Workflow,
		const FString& FirstPassKSamplerId,
		const FString& ModelSourceId,
		const FString& PositiveCondId,
		const FString& NegativeCondId,
		const FString& VAESourceId,
		int32 VAEOutputIndex,
		const FString& OldVAEDecodeId,
		const FString& OldSaveImageId,
		int32& NextNodeId) const;

	/** Helper: create a single ComfyUI node JSON object */
	TSharedPtr<FJsonObject> MakeNode(const FString& ClassType, TSharedPtr<FJsonObject> Inputs) const;

	/** Helper: create a node input link [node_id, output_index] */
	TSharedPtr<FJsonValue> MakeLink(const FString& NodeId, int32 OutputIndex = 0) const;

	/**
	 * Add Flux model loader nodes (UNETLoader + DualCLIPLoader + VAELoader) to a workflow.
	 * Sets OutModelId, OutClipId, OutVAEId to the node IDs of the respective loaders.
	 * Model output is at index 0 of UNETLoader, CLIP at index 0 of DualCLIPLoader,
	 * VAE at index 0 of VAELoader.
	 */
	void AddFluxLoaderNodes(
		TSharedPtr<FJsonObject> Workflow,
		int32& NextNodeId,
		FString& OutModelId,
		FString& OutClipId,
		FString& OutVAEId) const;

	// ---- Prompt Submission ----

	/** Submit a workflow to ComfyUI POST /prompt */
	void SubmitPrompt(TSharedPtr<FJsonObject> Workflow);

	/** Handle the /prompt response (extract prompt_id, start polling) */
	void OnPromptResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	// ---- Progress & History Polling ----

	/** Poll /queue to check execution state, and /api/node_status for step-level progress */
	void PollExecutionProgress();

	/** Handle /queue response */
	void OnQueueResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** Poll /history/{prompt_id} to check if generation is done */
	void PollHistory();

	/** Handle /history response */
	void OnHistoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** Track whether our prompt has started executing (moved from queue_pending to queue_running) */
	bool bExecutionStarted = false;

	/** Counter for alternating between queue/progress polls and history polls */
	int32 PollTickCounter = 0;

	// ---- WebSocket for real-time execution tracking ----

	/** Connect to ComfyUI WebSocket for execution status messages */
	void ConnectWebSocket();

	/** Handle incoming WebSocket text message */
	void OnWebSocketMessage(const FString& Message);

	/** The WebSocket connection to ComfyUI */
	TSharedPtr<IWebSocket> WebSocket;

	/** Whether the WebSocket is connected */
	bool bWebSocketConnected = false;

	// ---- SAM3 Workflow ----

	/** Build a ComfyUI workflow for SAM3 text-grounded segmentation */
	TSharedPtr<FJsonObject> BuildSAM3Workflow(const FString& ImageFilename) const;

	/** Whether the current request is a SAM3 segmentation (affects result handling) */
	bool bIsSAM3Request = false;

	/** The original source texture being segmented (kept alive during segmentation) */
	UTexture2D* SAM3SourceTexture = nullptr;

	/** Pending multi-image fetch state for SAM3 segmentation results */
	struct FPendingSegFetch
	{
		struct FImageRef
		{
			FString Filename;
			FString Subfolder;
			FString FolderType;
			bool bIsVisualization = false; // true for vis node, false for mask
		};
		TArray<FImageRef> ImagesToFetch;
		TArray<UTexture2D*> FetchedMaskTextures;
		UTexture2D* VisualizationTexture = nullptr;
		int32 CurrentFetchIndex = 0;
	};
	TSharedPtr<FPendingSegFetch> PendingSegmentation;

	/** Fetch the next image in the SAM3 segmentation batch */
	void FetchNextSegmentationImage();

	/** Handle a single segmentation image fetch */
	void OnSegmentationImageFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	// ---- Image Fetching ----

	/** Fetch the generated image from ComfyUI via GET /view */
	void FetchResultImage(const FString& Filename, const FString& Subfolder, const FString& FolderType);

	/** Handle /view response (image bytes) */
	void OnImageFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** Decode raw image bytes into a UTexture2D */
	UTexture2D* DecodeImageToTexture(const TArray<uint8>& ImageData);

	// ---- State ----
	FString CurrentPromptId;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ModelFetchRequest;
	bool bRequestInProgress = false;

	/** Unique client ID for this session */
	FString ClientId;

	/** Last generated result filename and subfolder (for opening output folder) */
	FString LastResultFilename;
	FString LastResultSubfolder;
	FString LastResultFolderType;

	/** Last 3D model result info (populated when a mesh file is detected in outputs) */
	FString Last3DModelFilename;
	FString Last3DModelSubfolder;
	FString Last3DModelFolderType;
	bool bHas3DModelResult = false;

public:
	/** Get the last generated result filename */
	const FString& GetLastResultFilename() const { return LastResultFilename; }
	const FString& GetLastResultSubfolder() const { return LastResultSubfolder; }
	const FString& GetLastResultFolderType() const { return LastResultFolderType; }

	/** Whether the last completed workflow produced a 3D model file */
	bool Has3DModelResult() const { return bHas3DModelResult; }
	const FString& GetLast3DModelFilename() const { return Last3DModelFilename; }
	const FString& GetLast3DModelSubfolder() const { return Last3DModelSubfolder; }
	const FString& GetLast3DModelFolderType() const { return Last3DModelFolderType; }
	void Clear3DModelResult() { bHas3DModelResult = false; Last3DModelFilename.Empty(); Last3DModelSubfolder.Empty(); Last3DModelFolderType.Empty(); }

	/**
	 * Query ComfyUI /system_stats to get the base output directory, then
	 * open it in the OS file explorer. Falls back to the ComfyUI /view URL.
	 */
	void OpenOutputFolder();
};
