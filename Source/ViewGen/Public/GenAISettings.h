// Copyright ViewGen. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GenAISettings.generated.h"

/**
 * Generation mode determines how the viewport/depth captures are used.
 */
UENUM(BlueprintType)
enum class EGenMode : uint8
{
	/** img2img: Viewport image is encoded to latent, prompt refines it */
	Img2Img			UMETA(DisplayName = "img2img (Viewport + Prompt)"),

	/** Depth + Prompt: Empty latent, ControlNet depth guides structure, prompt drives style */
	DepthAndPrompt	UMETA(DisplayName = "Depth + Prompt"),

	/** Prompt Only: Pure txt2img, no viewport or depth influence */
	PromptOnly		UMETA(DisplayName = "Prompt Only (txt2img)"),

	/** Gemini (Nano Banana 2): Google Gemini image generation with optional depth/viewport reference */
	Gemini			UMETA(DisplayName = "Gemini (Nano Banana 2)"),

	/** Kling 3.0: Kling AI image generation with optional viewport reference */
	Kling			UMETA(DisplayName = "Kling (Image 3.0)")
};

/**
 * Video generation mode — which image-to-video model to use.
 */
UENUM(BlueprintType)
enum class EVideoMode : uint8
{
	/** Kling Video: Kling AI image-to-video */
	KlingVideo		UMETA(DisplayName = "Kling Video"),

	/** Wan Video: Wan 2.1 image-to-video via ComfyUI */
	WanVideo		UMETA(DisplayName = "Wan I2V"),

	/** Veo3: Google Veo 3 image-to-video via ComfyUI API node */
	Veo3			UMETA(DisplayName = "Google Veo 3"),
};

/**
 * A single LoRA entry with a name, file path, and weight.
 */
USTRUCT(BlueprintType)
struct FLoRAEntry
{
	GENERATED_BODY()

	/** Display name for this LoRA */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoRA")
	FString Name;

	/**
	 * Exact LoRA filename as it appears in ComfyUI (e.g. "my_lora.safetensors").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoRA")
	FString PathOrIdentifier;

	/** Weight/strength of this LoRA - applied to both model and clip (typically 0.0 to 1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoRA", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Weight = 0.75f;

	/** Whether this LoRA is active for the current generation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LoRA")
	bool bEnabled = true;
};

/**
 * Plugin settings for ViewGen - configurable via Project Settings > Plugins > ViewGen.
 * Configured for ComfyUI Desktop by default.
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "ViewGen - AI Viewport Generator"))
class UGenAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGenAISettings();

	static UGenAISettings* Get();

	// ---- Connection Settings ----

	/** Base URL of the ComfyUI server */
	UPROPERTY(config, EditAnywhere, Category = "Connection",
		meta = (DisplayName = "ComfyUI URL"))
	FString APIEndpointURL = TEXT("http://127.0.0.1:8188");

	/** Request timeout in seconds (generation can take a while) */
	UPROPERTY(config, EditAnywhere, Category = "Connection",
		meta = (DisplayName = "Timeout (seconds)", ClampMin = "5", ClampMax = "600"))
	float TimeoutSeconds = 120.0f;

	/**
	 * ComfyUI Account API Key for API nodes (Gemini, etc.).
	 * Required when using partner API nodes like Nano Banana 2.
	 * Generate at: https://platform.comfy.org
	 * Format: "comfyui-xxxxxxxxx..."
	 * Leave empty if not using API nodes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Connection",
		meta = (DisplayName = "ComfyUI API Key"))
	FString ComfyUIApiKey;

	/** How often to poll ComfyUI for progress (seconds) */
	UPROPERTY(config, EditAnywhere, Category = "Connection",
		meta = (DisplayName = "Progress Poll Interval", ClampMin = "0.25", ClampMax = "5.0"))
	float ProgressPollInterval = 0.5f;

	// ---- Checkpoint / Model ----

	/** Checkpoint model name as listed in ComfyUI (e.g. "v1-5-pruned-emaonly.safetensors") */
	UPROPERTY(config, EditAnywhere, Category = "Model",
		meta = (DisplayName = "Checkpoint Name"))
	FString CheckpointName = TEXT("v1-5-pruned-emaonly.safetensors");

	// ---- Generation Mode ----

	/** How to generate: img2img uses viewport, Depth+Prompt uses depth map for structure,
	 *  Prompt Only is pure txt2img */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Generation Mode"))
	EGenMode GenerationMode = EGenMode::Img2Img;

	// ---- Reference Adherence ----

	/**
	 * Master slider that simultaneously adjusts multiple parameters to control
	 * generation adherence. For reference-based modes this controls fidelity to
	 * the viewport/depth image; for text-only modes it controls prompt adherence.
	 * 0.0 = Maximum creative freedom
	 * 1.0 = Maximum fidelity / adherence
	 *
	 * Per-mode mapping:
	 *   Img2Img:       DenoisingStrength (inv), CFGScale, Steps, ControlNetWeight, HiResDenoise
	 *   Depth+Prompt:  ControlNetWeight, CFGScale, Steps, HiResDenoise
	 *   PromptOnly:    CFGScale, Steps, HiResDenoise
	 *   Gemini:        ThinkingLevel
	 *   Kling:         KlingImageFidelity, KlingHumanFidelity
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Reference Adherence", ClampMin = "0.0", ClampMax = "1.0"))
	float ReferenceAdherence = 0.5f;

	/**
	 * Automatically prepend camera/lens descriptions to the prompt based on
	 * the viewport camera's FOV, angle, and position at capture time.
	 * Helps the generator match the framing and perspective of the viewport.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Auto Camera Prompt"))
	bool bAutoCameraPrompt = true;

	// ---- Generation Settings ----

	/** Default prompt text */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Default Prompt", MultiLine = true))
	FString DefaultPrompt = TEXT("high quality, detailed, photorealistic");

	/** Default negative prompt */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Default Negative Prompt", MultiLine = true))
	FString DefaultNegativePrompt = TEXT("low quality, blurry, distorted");

	/** Output width for generated images */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Output Width", ClampMin = "64", ClampMax = "2048"))
	int32 OutputWidth = 512;

	/** Output height for generated images */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Output Height", ClampMin = "64", ClampMax = "2048"))
	int32 OutputHeight = 512;

	/** Denoising strength for img2img (0.0 = no change, 1.0 = full generation) */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Denoising Strength", ClampMin = "0.0", ClampMax = "1.0"))
	float DenoisingStrength = 0.55f;

	/** Number of inference steps */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Steps", ClampMin = "1", ClampMax = "150"))
	int32 Steps = 30;

	/** CFG Scale (classifier-free guidance) */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "CFG Scale", ClampMin = "1.0", ClampMax = "30.0"))
	float CFGScale = 7.0f;

	/** Seed value (0 or negative for random) */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Seed"))
	int64 Seed = 0;

	/** KSampler sampler name (e.g. "euler", "euler_ancestral", "dpmpp_2m", "dpmpp_sde") */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Sampler"))
	FString SamplerName = TEXT("euler_ancestral");

	/** KSampler scheduler (e.g. "normal", "karras", "exponential", "sgm_uniform") */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (DisplayName = "Scheduler"))
	FString SchedulerName = TEXT("normal");

	// ---- Depth / ControlNet Settings ----

	/** Whether to include the depth map as a ControlNet input */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Enable Depth ControlNet"))
	bool bEnableDepthControlNet = false;

	/**
	 * Use Flux (XLabs) ControlNet pipeline instead of standard SD ControlNet.
	 * Requires the x-flux-comfyui custom node package installed in ComfyUI.
	 * When enabled, uses LoadFluxControlNet + ApplyFluxControlNet + XlabsSampler
	 * instead of ControlNetLoader + ControlNetApply + KSampler.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Use Flux ControlNet (XLabs)",
			EditCondition = "bEnableDepthControlNet"))
	bool bUseFluxControlNet = false;

	/**
	 * Flux UNET model name for UNETLoader (e.g. "flux1-dev.safetensors", "flux-dev-fp8.safetensors").
	 * Used as the diffusion model when Flux mode is enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux Model Name",
			EditCondition = "bUseFluxControlNet"))
	FString FluxModelName = TEXT("flux1-dev.safetensors");

	/**
	 * Weight data type for the Flux UNET model.
	 * Use "default" for full precision, "fp8_e4m3fn" or "fp8_e5m2" for quantized models.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux Weight Dtype",
			EditCondition = "bUseFluxControlNet"))
	FString FluxWeightDtype = TEXT("default");

	/**
	 * First CLIP model for DualCLIPLoader (typically "t5xxl_fp16.safetensors" or "t5xxl_fp8_e4m3fn.safetensors").
	 * This is the T5-XXL text encoder used by Flux.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux CLIP Name 1 (T5-XXL)",
			EditCondition = "bUseFluxControlNet"))
	FString FluxCLIPName1 = TEXT("t5xxl_fp16.safetensors");

	/**
	 * Second CLIP model for DualCLIPLoader (typically "clip_l.safetensors").
	 * This is the CLIP-L text encoder used by Flux.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux CLIP Name 2 (CLIP-L)",
			EditCondition = "bUseFluxControlNet"))
	FString FluxCLIPName2 = TEXT("clip_l.safetensors");

	/**
	 * CLIP type for DualCLIPLoader. Use "flux" for Flux models.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux CLIP Type",
			EditCondition = "bUseFluxControlNet"))
	FString FluxCLIPType = TEXT("flux");

	/**
	 * VAE model name for Flux (e.g. "ae.safetensors").
	 * Loaded via VAELoader instead of from the checkpoint.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Flux VAE Name",
			EditCondition = "bUseFluxControlNet"))
	FString FluxVAEName = TEXT("ae.safetensors");

	/** ControlNet model name as listed in ComfyUI (e.g. "control_v11f1p_sd15_depth.pth") */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "ControlNet Model", EditCondition = "bEnableDepthControlNet"))
	FString ControlNetModel = TEXT("control_v11f1p_sd15_depth.pth");

	/** ControlNet strength */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "ControlNet Strength", ClampMin = "0.0", ClampMax = "2.0",
			EditCondition = "bEnableDepthControlNet"))
	float ControlNetWeight = 1.0f;

	/** Maximum depth distance in Unreal units for depth normalization */
	UPROPERTY(config, EditAnywhere, Category = "Depth",
		meta = (DisplayName = "Max Depth Distance (UU)", ClampMin = "100.0", ClampMax = "500000.0"))
	float MaxDepthDistance = 50000.0f;

	// ---- Hi-Res Fix Settings ----

	/** Enable a second-pass upscale + re-sample for sharper, more detailed results */
	UPROPERTY(config, EditAnywhere, Category = "HiResFix",
		meta = (DisplayName = "Enable Hi-Res Fix"))
	bool bEnableHiResFix = false;

	/** Scale factor for the Hi-Res pass (e.g. 1.5 = 1.5x the base resolution) */
	UPROPERTY(config, EditAnywhere, Category = "HiResFix",
		meta = (DisplayName = "Upscale Factor", ClampMin = "1.0", ClampMax = "4.0",
			EditCondition = "bEnableHiResFix"))
	float HiResUpscaleFactor = 1.5f;

	/** Denoising strength for the Hi-Res second pass (typically 0.3-0.6) */
	UPROPERTY(config, EditAnywhere, Category = "HiResFix",
		meta = (DisplayName = "Hi-Res Denoise", ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "bEnableHiResFix"))
	float HiResDenoise = 0.45f;

	/** Number of steps for the Hi-Res second pass */
	UPROPERTY(config, EditAnywhere, Category = "HiResFix",
		meta = (DisplayName = "Hi-Res Steps", ClampMin = "1", ClampMax = "150",
			EditCondition = "bEnableHiResFix"))
	int32 HiResSteps = 20;

	// ---- Gemini (Nano Banana 2) Settings ----

	/**
	 * Gemini model variant for the GeminiNanoBanana2 node.
	 * Available: "Nano Banana 2 (Gemini 3.1 Flash Image)"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Gemini Model",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiModelName = TEXT("Nano Banana 2 (Gemini 3.1 Flash Image)");

	/**
	 * Aspect ratio for Gemini output.
	 * Options: "auto", "1:1", "2:3", "3:2", "3:4", "4:3", "4:5", "5:4", "9:16", "16:9", "21:9"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Aspect Ratio",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiAspectRatio = TEXT("16:9");

	/**
	 * Output image resolution tier.
	 * Options: "1K", "2K", "4K"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Resolution",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiResolution = TEXT("1K");

	/**
	 * Response modalities: IMAGE returns images only, IMAGE+TEXT returns both.
	 * Options: "IMAGE", "IMAGE+TEXT"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Response Modalities",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiResponseModalities = TEXT("IMAGE");

	/**
	 * Thinking level for Gemini: MINIMAL is faster, HIGH produces more considered outputs.
	 * Options: "MINIMAL", "HIGH"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Thinking Level",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiThinkingLevel = TEXT("MINIMAL");

	/** Seed for Gemini generation (0 = random) */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "Seed", ClampMin = "0",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	int64 GeminiSeed = 0;

	/** Optional system prompt to guide Gemini's behavior */
	UPROPERTY(config, EditAnywhere, Category = "Gemini",
		meta = (DisplayName = "System Prompt",
			EditCondition = "GenerationMode == EGenMode::Gemini"))
	FString GeminiSystemPrompt = TEXT("You are an expert image-generation engine. You must ALWAYS produce an image.");

	// ---- Kling (Image 3.0) Settings ----

	/**
	 * Kling model variant for the KlingImageGenerationNode.
	 * Options: "kling-v3", "kling-v2", "kling-v1-5"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Model",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	FString KlingModelName = TEXT("kling-v3");

	/**
	 * Aspect ratio for Kling output.
	 * Options: "16:9", "9:16", "1:1", "4:3", "3:4", "3:2", "2:3", "21:9"
	 */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Aspect Ratio",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	FString KlingAspectRatio = TEXT("16:9");

	/**
	 * Reference image type for Kling.
	 * "subject" for general reference, "face" for face-focused generation.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Image Type",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	FString KlingImageType = TEXT("subject");

	/**
	 * Reference intensity for user-uploaded images (0.0 to 1.0).
	 * Higher values follow the reference image more closely.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Image Fidelity", ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	float KlingImageFidelity = 0.5f;

	/**
	 * Subject reference similarity (0.0 to 1.0).
	 * Controls how closely the output matches the human subject in the reference.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Human Fidelity", ClampMin = "0.0", ClampMax = "1.0",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	float KlingHumanFidelity = 0.45f;

	/** Number of images to generate per request (1-9) */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Image Count", ClampMin = "1", ClampMax = "9",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	int32 KlingImageCount = 1;

	/** Seed for Kling generation (0 = random) */
	UPROPERTY(config, EditAnywhere, Category = "Kling",
		meta = (DisplayName = "Seed", ClampMin = "0",
			EditCondition = "GenerationMode == EGenMode::Kling"))
	int64 KlingSeed = 0;

	// ---- Video Generation (Image-to-Video) Settings ----

	/** Which image-to-video model to use */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video Mode"))
	EVideoMode VideoMode = EVideoMode::KlingVideo;

	/**
	 * Master slider that simultaneously adjusts motion-related parameters.
	 * 0.0 = Minimal motion / maximum fidelity to source frame
	 * 1.0 = Maximum motion / creative freedom
	 *
	 * Per-mode mapping:
	 *   Kling Video:  KlingVideoCFG (inv), KlingVideoDuration, KlingVideoQuality
	 *   Wan 2.1:      Steps, CFG (inv), Duration
	 *   Veo3:         Duration, generate_audio, aspect_ratio
	 */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Motion Adherence", ClampMin = "0.0", ClampMax = "1.0"))
	float VideoMotionAdherence = 0.5f;

	/** Prompt describing the desired motion / scene (shared across modes) */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video Prompt", MultiLine = true))
	FString VideoPrompt = TEXT("smooth cinematic motion");

	/** Negative prompt for video generation */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video Negative Prompt", MultiLine = true))
	FString VideoNegativePrompt = TEXT("jittery, flickering, low quality, distorted");

	/** Video duration in seconds */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Duration (seconds)", ClampMin = "1.0", ClampMax = "10.0"))
	float VideoDuration = 5.0f;

	/** Frames per second for the output video */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "FPS", ClampMin = "6", ClampMax = "30"))
	int32 VideoFPS = 24;

	/** CFG scale for video generation (Kling: 0.0–1.0, default 0.8) */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video CFG", ClampMin = "0.0", ClampMax = "1.0"))
	float VideoCFG = 0.8f;

	/** Inference steps for video generation */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video Steps", ClampMin = "1", ClampMax = "100"))
	int32 VideoSteps = 30;

	/** Seed for video generation (0 = random) */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Video Seed", ClampMin = "0"))
	int64 VideoSeed = 0;

	// -- Kling Video specific --

	/** Kling video model variant */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Kling Video Model"))
	FString KlingVideoModel = TEXT("kling-v2-master");

	/** Kling video mode: "std" (standard) or "pro" (higher quality) */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Kling Video Quality"))
	FString KlingVideoQuality = TEXT("std");

	// -- Wan 2.1 specific --

	/** Wan I2V model for the WanImageToVideoApi node */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Wan Model Name"))
	FString WanModelName = TEXT("wan2.6-i2v");

	// -- Veo3 specific --

	/** Veo3 model name for the ComfyUI node */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Veo3 Model"))
	FString Veo3ModelName = TEXT("veo-3.0-generate-001");

	/** Veo3 aspect ratio */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Veo3 Aspect Ratio"))
	FString Veo3AspectRatio = TEXT("16:9");

	/** Whether to generate audio alongside video */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Veo3 Generate Audio"))
	bool bVeo3GenerateAudio = false;

	/** Person generation preference: "ALLOW" or "DONT_ALLOW" */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "Veo3 Person Generation"))
	FString Veo3PersonGeneration = TEXT("ALLOW");

	// ---- FFmpeg Settings ----

	/**
	 * Path to the ffmpeg executable. Used for extracting video thumbnails.
	 * Leave empty to search the system PATH. Set this to a full path
	 * (e.g. C:/ffmpeg/bin/ffmpeg.exe) if ffmpeg is not on PATH.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Video",
		meta = (DisplayName = "FFmpeg Path"))
	FString FFmpegPath;

	// ---- SAM3 Segmentation Settings ----

	/** Confidence threshold for SAM3 text-grounded segmentation (lower = more detections) */
	UPROPERTY(config, EditAnywhere, Category = "Segmentation",
		meta = (DisplayName = "SAM3 Confidence Threshold", ClampMin = "0.05", ClampMax = "1.0"))
	float SAM3ConfidenceThreshold = 0.25f;

	/** Default text prompt for SAM3 grounding (e.g. "objects", "person . car . tree") */
	UPROPERTY(config, EditAnywhere, Category = "Segmentation",
		meta = (DisplayName = "SAM3 Text Prompt"))
	FString SAM3TextPrompt = TEXT("objects");

	/** SAM3 model precision */
	UPROPERTY(config, EditAnywhere, Category = "Segmentation",
		meta = (DisplayName = "SAM3 Precision"))
	FString SAM3Precision = TEXT("auto");

	/** Maximum detections for SAM3 (-1 = unlimited) */
	UPROPERTY(config, EditAnywhere, Category = "Segmentation",
		meta = (DisplayName = "SAM3 Max Detections", ClampMin = "-1", ClampMax = "50"))
	int32 SAM3MaxDetections = -1;

	// ---- Meshy API Settings ----

	/** Meshy API key for Image-to-3D conversion (format: msy-XXXX) */
	UPROPERTY(config, EditAnywhere, Category = "Meshy",
		meta = (DisplayName = "Meshy API Key"))
	FString MeshyApiKey;

	/** Enable PBR textures on generated 3D models */
	UPROPERTY(config, EditAnywhere, Category = "Meshy",
		meta = (DisplayName = "Enable PBR Textures"))
	bool bMeshyEnablePBR = true;

	/** Enable remeshing for cleaner topology */
	UPROPERTY(config, EditAnywhere, Category = "Meshy",
		meta = (DisplayName = "Enable Remesh"))
	bool bMeshyRemesh = true;

	// ---- LoRA Settings ----

	/** List of available LoRA models */
	UPROPERTY(config, EditAnywhere, Category = "LoRA",
		meta = (DisplayName = "LoRA Models"))
	TArray<FLoRAEntry> LoRAModels;

	// ---- Cost Estimation ----

	/**
	 * Show estimated API cost in the status bar before generation starts.
	 * Local ComfyUI modes (img2img, Depth+Prompt, Prompt Only) show "Local (no API cost)".
	 * External APIs (Gemini, Kling, Veo3, Meshy) show a dollar estimate based on the
	 * pricing table below. Prices change over time — update these values as needed.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Show Cost Estimates"))
	bool bShowCostEstimates = true;

	// -- Gemini / Nano Banana 2 per-image pricing by resolution tier --

	/** Cost per image at 1K resolution (Nano Banana 2) */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Gemini 1K Cost ($)", ClampMin = "0.0"))
	float GeminiCost1K = 0.067f;

	/** Cost per image at 2K resolution (Nano Banana 2) */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Gemini 2K Cost ($)", ClampMin = "0.0"))
	float GeminiCost2K = 0.101f;

	/** Cost per image at 4K resolution (Nano Banana 2) */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Gemini 4K Cost ($)", ClampMin = "0.0"))
	float GeminiCost4K = 0.151f;

	// -- Kling Image per-image cost --

	/** Cost per Kling image generation (per image) */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Kling Image Cost ($)", ClampMin = "0.0"))
	float KlingImageCost = 0.04f;

	// -- Kling Video per-second pricing --

	/** Cost per second for Kling Video Standard quality */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Kling Video Std ($/sec)", ClampMin = "0.0"))
	float KlingVideoStdCostPerSec = 0.168f;

	/** Cost per second for Kling Video Pro quality */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Kling Video Pro ($/sec)", ClampMin = "0.0"))
	float KlingVideoProCostPerSec = 0.224f;

	// -- Veo3 per-second pricing --

	/** Cost per second for Veo3 video generation */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Veo3 Cost ($/sec)", ClampMin = "0.0"))
	float Veo3CostPerSec = 0.40f;

	// -- Meshy credits --

	/** Credits consumed per Meshy Image-to-3D task (with texture) */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Meshy Credits Per Task", ClampMin = "0"))
	int32 MeshyCreditsCost = 15;

	/** Credits consumed per Tripo Image-to-3D task */
	UPROPERTY(config, EditAnywhere, Category = "CostEstimation",
		meta = (DisplayName = "Tripo Credits Per Task", ClampMin = "0"))
	int32 TripoCreditsCost = 20;

	// ---- ComfyUI Model Paths ----
	// Maps a model category key to its ComfyUI subdirectory.
	// These are shown as tooltips on settings labels and persist in presets.
	// Users can customize paths if their ComfyUI install uses non-standard directories.

	UPROPERTY(config, EditAnywhere, Category = "ModelPaths",
		meta = (DisplayName = "Model Directory Paths"))
	TMap<FString, FString> ModelPaths;

	/** Returns the ComfyUI model path for a given key, or a fallback default */
	FString GetModelPath(const FString& Key) const;

	/** Populate ModelPaths with defaults for any keys that are missing */
	void EnsureDefaultModelPaths();

	// ---- Presets ----

	/** Get the directory where presets are stored */
	static FString GetPresetsDirectory();

	/** List all saved preset names (filenames without extension) */
	static TArray<FString> GetSavedPresetNames();

	/** Save current settings to a named preset JSON file */
	static bool SavePreset(const FString& PresetName);

	/** Load a named preset JSON file into the current settings */
	static bool LoadPreset(const FString& PresetName);

	/** Delete a named preset file */
	static bool DeletePreset(const FString& PresetName);

	// ---- Category / Section ----

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("ViewGen")); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		SaveConfig();
	}
#endif
};
