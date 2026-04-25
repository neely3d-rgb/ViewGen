// Copyright ViewGen. All Rights Reserved.

#include "GenAIHttpClient.h"
#include "GenAISettings.h"
#include "ComfyNodeDatabase.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpResponse.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"

FGenAIHttpClient::FGenAIHttpClient()
{
	// Generate a unique client ID for this session
	ClientId = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
}

FGenAIHttpClient::~FGenAIHttpClient()
{
	CancelRequest();
}

// ============================================================================
// Model Discovery
// ============================================================================

void FGenAIHttpClient::FetchAvailableModels()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	// Query /object_info for CheckpointLoaderSimple, LoraLoader, and ControlNetLoader
	// These return the available model files in their input definitions.
	// We'll fetch all three node infos from a single endpoint: /object_info
	FString URL = FString::Printf(TEXT("%s/object_info"), *Settings->APIEndpointURL);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(60.0f); // /object_info can be very large with many custom nodes

	Request->OnProcessRequestComplete().BindLambda(
		[this](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
	{
		TArray<FString> Checkpoints;
		TArray<FString> LoRAs;
		TArray<FString> ControlNets;
		FGeminiNodeOptions GeminiOpts;
		FKlingNodeOptions KlingOpts;

		if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to fetch /object_info from ComfyUI"));
			OnModelListsFetched.ExecuteIfBound(Checkpoints, LoRAs, ControlNets, GeminiOpts, KlingOpts);
			return;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to parse /object_info JSON"));
			OnModelListsFetched.ExecuteIfBound(Checkpoints, LoRAs, ControlNets, GeminiOpts, KlingOpts);
			return;
		}

		// Populate the global node database for the graph editor
		UE_LOG(LogTemp, Log, TEXT("ViewGen: /object_info response size: %d bytes, %d top-level entries"),
			Resp->GetContentAsString().Len(), Root->Values.Num());
		FComfyNodeDatabase::Get().ParseObjectInfo(Root);

		// Helper: extract string array from node_info -> input -> required -> field_name
		// Handles both old format: [["option1","option2",...], {...}]
		//           and new format: ["COMBO", {"options": ["option1","option2",...]}]
		auto ExtractOptions = [&Root](const FString& NodeClass, const FString& FieldName, TArray<FString>& OutOptions)
		{
			const TSharedPtr<FJsonObject>* NodeInfo;
			if (!Root->TryGetObjectField(NodeClass, NodeInfo)) return;

			const TSharedPtr<FJsonObject>* InputObj;
			if (!(*NodeInfo)->TryGetObjectField(TEXT("input"), InputObj)) return;

			const TSharedPtr<FJsonObject>* RequiredObj;
			if (!(*InputObj)->TryGetObjectField(TEXT("required"), RequiredObj)) return;

			const TArray<TSharedPtr<FJsonValue>>* FieldArray;
			if (!(*RequiredObj)->TryGetArrayField(FieldName, FieldArray) || FieldArray->Num() == 0) return;

			// Old format: first element is directly an array of option strings
			const TArray<TSharedPtr<FJsonValue>>* OptionsArray;
			if ((*FieldArray)[0]->TryGetArray(OptionsArray))
			{
				for (const auto& Val : *OptionsArray)
				{
					FString Str;
					if (Val->TryGetString(Str))
					{
						OutOptions.Add(Str);
					}
				}
				return;
			}

			// New format: ["COMBO", {"options": [...]}] or ["COMBO", {"multiselect":false, "options":[...]}]
			if (FieldArray->Num() >= 2)
			{
				const TSharedPtr<FJsonObject>* MetaObj;
				if ((*FieldArray)[1]->TryGetObject(MetaObj))
				{
					const TArray<TSharedPtr<FJsonValue>>* ComboOptions;
					if ((*MetaObj)->TryGetArrayField(TEXT("options"), ComboOptions))
					{
						for (const auto& Val : *ComboOptions)
						{
							FString Str;
							if (Val->TryGetString(Str))
							{
								OutOptions.Add(Str);
							}
						}
					}
				}
			}
		};

		ExtractOptions(TEXT("CheckpointLoaderSimple"), TEXT("ckpt_name"), Checkpoints);
		ExtractOptions(TEXT("LoraLoader"), TEXT("lora_name"), LoRAs);
		ExtractOptions(TEXT("ControlNetLoader"), TEXT("control_net_name"), ControlNets);

		// Extract GeminiNanoBanana2 combo options (if the node is installed)
		ExtractOptions(TEXT("GeminiNanoBanana2"), TEXT("model"), GeminiOpts.Models);
		ExtractOptions(TEXT("GeminiNanoBanana2"), TEXT("aspect_ratio"), GeminiOpts.AspectRatios);
		ExtractOptions(TEXT("GeminiNanoBanana2"), TEXT("resolution"), GeminiOpts.Resolutions);
		ExtractOptions(TEXT("GeminiNanoBanana2"), TEXT("response_modalities"), GeminiOpts.ResponseModalities);
		ExtractOptions(TEXT("GeminiNanoBanana2"), TEXT("thinking_level"), GeminiOpts.ThinkingLevels);

		// Extract KlingImageGenerationNode combo options (if the node is installed)
		ExtractOptions(TEXT("KlingImageGenerationNode"), TEXT("model_name"), KlingOpts.Models);
		ExtractOptions(TEXT("KlingImageGenerationNode"), TEXT("aspect_ratio"), KlingOpts.AspectRatios);
		ExtractOptions(TEXT("KlingImageGenerationNode"), TEXT("image_type"), KlingOpts.ImageTypes);

		// Extract KlingImage2VideoNode combo options (if the node is installed)
		FKlingVideoNodeOptions KlingVideoOpts;
		ExtractOptions(TEXT("KlingImage2VideoNode"), TEXT("model_name"), KlingVideoOpts.Models);
		ExtractOptions(TEXT("KlingImage2VideoNode"), TEXT("mode"), KlingVideoOpts.Modes);

		// Extract Veo3VideoGenerationNode combo options (if the node is installed)
		FVeo3NodeOptions Veo3Opts;
		ExtractOptions(TEXT("Veo3VideoGenerationNode"), TEXT("model"), Veo3Opts.Models);
		ExtractOptions(TEXT("Veo3VideoGenerationNode"), TEXT("aspect_ratio"), Veo3Opts.AspectRatios);
		ExtractOptions(TEXT("Veo3VideoGenerationNode"), TEXT("person_generation"), Veo3Opts.PersonGenerations);

		// Extract WanImageToVideoApi combo options (if the node is installed)
		FWanVideoNodeOptions WanVideoOpts;
		ExtractOptions(TEXT("WanImageToVideoApi"), TEXT("model"), WanVideoOpts.Models);
		ExtractOptions(TEXT("WanImageToVideoApi"), TEXT("resolution"), WanVideoOpts.Resolutions);

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Discovered %d checkpoints, %d LoRAs, %d ControlNets, %d Gemini models, %d Kling models, %d KlingVideo models, %d Veo3 models, %d Wan models"),
			Checkpoints.Num(), LoRAs.Num(), ControlNets.Num(), GeminiOpts.Models.Num(), KlingOpts.Models.Num(),
			KlingVideoOpts.Models.Num(), Veo3Opts.Models.Num(), WanVideoOpts.Models.Num());

		OnModelListsFetched.ExecuteIfBound(Checkpoints, LoRAs, ControlNets, GeminiOpts, KlingOpts);
	});

	Request->ProcessRequest();
}

// ============================================================================
// Public API
// ============================================================================

void FGenAIHttpClient::SendImg2ImgRequest(
	const FString& ViewportImageBase64,
	const FString& DepthImageBase64,
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	// Use unique filenames to bust ComfyUI's execution cache
	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Step 1: Upload viewport image to ComfyUI
	FString ViewportFilename;
	FString ViewportUploadName = FString::Printf(TEXT("ue_gen_viewport_%s.png"), *UniqueTag);
	if (!UploadImageToComfyUI(ViewportImageBase64, ViewportUploadName, ViewportFilename))
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("Failed to upload viewport image to ComfyUI"));
		return;
	}

	// Step 2: Upload depth image if available
	FString DepthFilename;
	const UGenAISettings* Settings = UGenAISettings::Get();
	if (Settings->bEnableDepthControlNet && !DepthImageBase64.IsEmpty())
	{
		FString DepthUploadName = FString::Printf(TEXT("ue_gen_depth_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(DepthImageBase64, DepthUploadName, DepthFilename))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to upload depth image, proceeding without ControlNet"));
		}
	}

	// Step 3: Build and submit the workflow
	TSharedPtr<FJsonObject> Workflow = BuildImg2ImgWorkflow(
		ViewportFilename, DepthFilename, Prompt, NegativePrompt, ActiveLoRAs);

	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendTxt2ImgRequest(
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	TSharedPtr<FJsonObject> Workflow = BuildTxt2ImgWorkflow(Prompt, NegativePrompt, ActiveLoRAs);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendDepthOnlyRequest(
	const FString& DepthImageBase64,
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	// Upload depth image to ComfyUI with unique filename to bust cache
	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());
	FString DepthFilename;
	FString DepthUploadName = FString::Printf(TEXT("ue_gen_depth_%s.png"), *UniqueTag);
	if (!UploadImageToComfyUI(DepthImageBase64, DepthUploadName, DepthFilename))
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("Failed to upload depth image to ComfyUI"));
		return;
	}

	// Build and submit the depth-only workflow
	TSharedPtr<FJsonObject> Workflow = BuildDepthOnlyWorkflow(
		DepthFilename, Prompt, NegativePrompt, ActiveLoRAs);

	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendGeminiRequest(
	const FString& ViewportImageBase64,
	const FString& DepthImageBase64,
	const FString& Prompt)
{
	UE_LOG(LogTemp, Log, TEXT("ViewGen: SendGeminiRequest called (bRequestInProgress=%s, CurrentPromptId='%s')"),
		bRequestInProgress ? TEXT("true") : TEXT("false"), *CurrentPromptId);

	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	// Use unique filenames per upload to bust ComfyUI's execution cache.
	// ComfyUI caches node outputs based on input values — if the LoadImage
	// filename string is identical between runs, it skips re-execution entirely.
	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Upload viewport image if available
	FString ViewportFilename;
	if (!ViewportImageBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_viewport_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(ViewportImageBase64, UploadName, ViewportFilename))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to upload viewport image for Gemini, proceeding without it"));
		}
	}

	// Upload depth image if available
	FString DepthFilename;
	if (!DepthImageBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_depth_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(DepthImageBase64, UploadName, DepthFilename))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to upload depth image for Gemini, proceeding without it"));
		}
	}

	TSharedPtr<FJsonObject> Workflow = BuildGeminiWorkflow(ViewportFilename, DepthFilename, Prompt);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendKlingRequest(
	const FString& ViewportImageBase64,
	const FString& Prompt,
	const FString& NegativePrompt)
{
	UE_LOG(LogTemp, Log, TEXT("ViewGen: SendKlingRequest called (bRequestInProgress=%s, CurrentPromptId='%s')"),
		bRequestInProgress ? TEXT("true") : TEXT("false"), *CurrentPromptId);

	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	// Use unique filenames to bust ComfyUI's execution cache
	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Upload viewport image if available (used as optional reference image)
	FString ViewportFilename;
	if (!ViewportImageBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_viewport_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(ViewportImageBase64, UploadName, ViewportFilename))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to upload viewport image for Kling, proceeding without it"));
		}
	}

	TSharedPtr<FJsonObject> Workflow = BuildKlingWorkflow(ViewportFilename, Prompt, NegativePrompt);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendKlingVideoRequest(
	const FString& SourceFrameBase64,
	const FString& Prompt,
	const FString& NegativePrompt)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Upload source frame
	FString SourceFilename;
	if (!SourceFrameBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_video_src_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(SourceFrameBase64, UploadName, SourceFilename))
		{
			bRequestInProgress = false;
			OnError.ExecuteIfBound(TEXT("Failed to upload source frame for Kling Video"));
			return;
		}
	}

	TSharedPtr<FJsonObject> Workflow = BuildKlingVideoWorkflow(SourceFilename, Prompt, NegativePrompt);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendVeo3Request(
	const FString& SourceFrameBase64,
	const FString& Prompt)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Upload source frame
	FString SourceFilename;
	if (!SourceFrameBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_veo3_src_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(SourceFrameBase64, UploadName, SourceFilename))
		{
			bRequestInProgress = false;
			OnError.ExecuteIfBound(TEXT("Failed to upload source frame for Veo3"));
			return;
		}
	}

	TSharedPtr<FJsonObject> Workflow = BuildVeo3Workflow(SourceFilename, Prompt);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::SendWanVideoRequest(
	const FString& SourceFrameBase64,
	const FString& Prompt,
	const FString& NegativePrompt)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;

	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	// Upload source frame
	FString SourceFilename;
	if (!SourceFrameBase64.IsEmpty())
	{
		FString UploadName = FString::Printf(TEXT("ue_gen_wan_src_%s.png"), *UniqueTag);
		if (!UploadImageToComfyUI(SourceFrameBase64, UploadName, SourceFilename))
		{
			bRequestInProgress = false;
			OnError.ExecuteIfBound(TEXT("Failed to upload source frame for Wan I2V"));
			return;
		}
	}

	TSharedPtr<FJsonObject> Workflow = BuildWanVideoWorkflow(SourceFilename, Prompt, NegativePrompt);
	SubmitPrompt(Workflow);
}

void FGenAIHttpClient::CancelRequest()
{
	if (CurrentRequest.IsValid())
	{
		CurrentRequest->CancelRequest();
		CurrentRequest.Reset();
	}

	// If we have a running prompt, tell ComfyUI to interrupt it
	if (!CurrentPromptId.IsEmpty())
	{
		const UGenAISettings* Settings = UGenAISettings::Get();
		FString InterruptURL = Settings->APIEndpointURL / TEXT("interrupt");

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(InterruptURL);
		Req->SetVerb(TEXT("POST"));
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Req->SetContentAsString(TEXT("{}"));
		Req->ProcessRequest(); // Fire and forget
	}

	CurrentPromptId.Empty();
	bRequestInProgress = false;
	bIsSAM3Request = false;
	SAM3SourceTexture = nullptr;
	PendingSegmentation.Reset();
}

void FGenAIHttpClient::PollProgress()
{
	if (!bRequestInProgress || CurrentPromptId.IsEmpty())
	{
		return;
	}

	PollTickCounter++;

	// Every 3rd tick, check /history to see if generation is complete.
	// All other ticks, poll /queue for real-time execution progress.
	if (PollTickCounter % 3 == 0)
	{
		PollHistory();
	}
	else
	{
		PollExecutionProgress();
	}
}

void FGenAIHttpClient::PollExecutionProgress()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString QueueURL = Settings->APIEndpointURL / TEXT("queue");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(QueueURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(5.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FGenAIHttpClient::OnQueueResponseReceived);
	Request->ProcessRequest();
}

void FGenAIHttpClient::OnQueueResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bRequestInProgress)
	{
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		return;
	}

	// Check queue_running — array of [number, prompt_id, ..., ...] tuples
	const TArray<TSharedPtr<FJsonValue>>* RunningArray;
	bool bOurPromptRunning = false;
	if (JsonResp->TryGetArrayField(TEXT("queue_running"), RunningArray))
	{
		for (const auto& Entry : *RunningArray)
		{
			const TArray<TSharedPtr<FJsonValue>>* Tuple;
			if (Entry->TryGetArray(Tuple) && Tuple->Num() >= 2)
			{
				FString PromptId;
				if ((*Tuple)[1]->TryGetString(PromptId) && PromptId == CurrentPromptId)
				{
					bOurPromptRunning = true;
					break;
				}
			}
		}
	}

	// Check queue_pending for position
	const TArray<TSharedPtr<FJsonValue>>* PendingArray;
	int32 QueuePosition = -1;
	int32 TotalPending = 0;
	if (JsonResp->TryGetArrayField(TEXT("queue_pending"), PendingArray))
	{
		TotalPending = PendingArray->Num();
		for (int32 i = 0; i < PendingArray->Num(); ++i)
		{
			const TArray<TSharedPtr<FJsonValue>>* Tuple;
			if ((*PendingArray)[i]->TryGetArray(Tuple) && Tuple->Num() >= 2)
			{
				FString PromptId;
				if ((*Tuple)[1]->TryGetString(PromptId) && PromptId == CurrentPromptId)
				{
					QueuePosition = i;
					break;
				}
			}
		}
	}

	if (bOurPromptRunning)
	{
		if (!bExecutionStarted)
		{
			bExecutionStarted = true;
		}

		// Our prompt is executing — poll /api/node_status for finer progress
		const UGenAISettings* Settings = UGenAISettings::Get();
		FString NodeStatusURL = Settings->APIEndpointURL / TEXT("api/node_status");

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> StatusRequest = FHttpModule::Get().CreateRequest();
		StatusRequest->SetURL(NodeStatusURL);
		StatusRequest->SetVerb(TEXT("GET"));
		StatusRequest->SetTimeout(5.0f);

		StatusRequest->OnProcessRequestComplete().BindLambda(
			[this](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOK)
			{
				if (!bRequestInProgress) return;

				float Progress = -1.0f; // Sentinel: no fine progress available

				if (bOK && Resp.IsValid() && Resp->GetResponseCode() == 200)
				{
					TSharedPtr<FJsonObject> StatusJson;
					TSharedRef<TJsonReader<>> StatusReader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
					if (FJsonSerializer::Deserialize(StatusReader, StatusJson) && StatusJson.IsValid())
					{
						int32 TotalNodes = 0;
						int32 ExecutedNodes = 0;
						if (StatusJson->TryGetNumberField(TEXT("total_nodes"), TotalNodes) &&
							StatusJson->TryGetNumberField(TEXT("executed_nodes"), ExecutedNodes) &&
							TotalNodes > 0)
						{
							// Map node progress to 0.15 .. 0.85 range
							float NodeProgress = static_cast<float>(ExecutedNodes) / static_cast<float>(TotalNodes);
							Progress = 0.15f + NodeProgress * 0.70f;
						}
					}
				}

				if (Progress < 0.0f)
				{
					// /api/node_status unavailable or didn't return useful data.
					// Gradually advance progress so the bar keeps moving.
					// PollTickCounter increments once per second; creep from 0.15 to 0.85
					// over ~120 ticks so the user sees continuous movement.
					float Elapsed = FMath::Clamp(PollTickCounter / 120.0f, 0.0f, 1.0f);
					Progress = 0.15f + Elapsed * 0.70f;
				}

				OnProgress.ExecuteIfBound(Progress);
			});

		StatusRequest->ProcessRequest();
	}
	else if (QueuePosition >= 0)
	{
		// Still in the pending queue
		OnProgress.ExecuteIfBound(0.05f);
	}
	else if (!bExecutionStarted)
	{
		// Not in running or pending — might be loading models or between states
		OnProgress.ExecuteIfBound(0.1f);
	}
	// else: not found in queue but execution started — likely finishing up, history poll will catch it
}

// ============================================================================
// Image Upload
// ============================================================================

bool FGenAIHttpClient::UploadImageToComfyUI(const FString& Base64PNG, const FString& DesiredFilename, FString& OutServerFilename)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString UploadURL = Settings->APIEndpointURL / TEXT("upload/image");

	// Decode base64 to raw PNG bytes
	TArray<uint8> PNGBytes;
	if (!FBase64::Decode(Base64PNG, PNGBytes))
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to decode base64 for upload"));
		return false;
	}

	// Build multipart/form-data request
	FString Boundary = FString::Printf(TEXT("----UEGenBoundary%s"), *FGuid::NewGuid().ToString());

	TArray<uint8> Payload;
	FString Header = FString::Printf(
		TEXT("--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\nContent-Type: image/png\r\n\r\n"),
		*Boundary, *DesiredFilename);

	FString OverwriteField = FString::Printf(
		TEXT("\r\n--%s\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--%s--\r\n"),
		*Boundary, *Boundary);

	// Assemble payload: header bytes + PNG bytes + overwrite field bytes
	FTCHARToUTF8 HeaderUTF8(*Header);
	Payload.Append((const uint8*)HeaderUTF8.Get(), HeaderUTF8.Length());
	Payload.Append(PNGBytes);
	FTCHARToUTF8 OverwriteUTF8(*OverwriteField);
	Payload.Append((const uint8*)OverwriteUTF8.Get(), OverwriteUTF8.Length());

	// Send synchronously (blocking - we need the filename before building the workflow)
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(UploadURL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	Request->SetContent(Payload);
	Request->SetTimeout(30.0f);

	// Use a synchronous wait pattern
	bool bCompleted = false;
	bool bSuccess = false;
	FString ResponseBody;

	Request->OnProcessRequestComplete().BindLambda(
		[&bCompleted, &bSuccess, &ResponseBody](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			bCompleted = true;
			if (bConnected && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				bSuccess = true;
				ResponseBody = Resp->GetContentAsString();
			}
			else if (Resp.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen Upload: HTTP %d: %s"),
					Resp->GetResponseCode(), *Resp->GetContentAsString().Left(200));
			}
		});

	Request->ProcessRequest();

	// Block until the upload completes
	double StartTime = FPlatformTime::Seconds();
	while (!bCompleted && (FPlatformTime::Seconds() - StartTime) < 30.0)
	{
		FPlatformProcess::Sleep(0.05f);
		FHttpModule::Get().GetHttpManager().Tick(0.0f);
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Image upload failed for %s"), *DesiredFilename);
		return false;
	}

	// Parse response: { "name": "ue_gen_viewport.png", "subfolder": "", "type": "input" }
	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (FJsonSerializer::Deserialize(Reader, JsonResp) && JsonResp.IsValid())
	{
		OutServerFilename = JsonResp->GetStringField(TEXT("name"));
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Uploaded image as '%s'"), *OutServerFilename);
		return true;
	}

	// Fallback: assume the filename we sent
	OutServerFilename = DesiredFilename;
	return true;
}

// ============================================================================
// Workflow Building
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::MakeNode(const FString& ClassType, TSharedPtr<FJsonObject> Inputs) const
{
	TSharedPtr<FJsonObject> Node = MakeShareable(new FJsonObject);
	Node->SetStringField(TEXT("class_type"), ClassType);

	TSharedPtr<FJsonObject> MetaObj = MakeShareable(new FJsonObject);
	MetaObj->SetStringField(TEXT("title"), ClassType);
	Node->SetObjectField(TEXT("_meta"), MetaObj);

	Node->SetObjectField(TEXT("inputs"), Inputs);
	return Node;
}

TSharedPtr<FJsonValue> FGenAIHttpClient::MakeLink(const FString& NodeId, int32 OutputIndex) const
{
	TArray<TSharedPtr<FJsonValue>> LinkArray;
	LinkArray.Add(MakeShareable(new FJsonValueString(NodeId)));
	LinkArray.Add(MakeShareable(new FJsonValueNumber(OutputIndex)));
	return MakeShareable(new FJsonValueArray(LinkArray));
}

void FGenAIHttpClient::AddFluxLoaderNodes(
	TSharedPtr<FJsonObject> Workflow,
	int32& NextNodeId,
	FString& OutModelId,
	FString& OutClipId,
	FString& OutVAEId) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	OutModelId = FString::FromInt(NextNodeId++);
	OutClipId = FString::FromInt(NextNodeId++);
	OutVAEId = FString::FromInt(NextNodeId++);

	// UNETLoader — loads the Flux diffusion model
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("unet_name"), Settings->FluxModelName);
		Inputs->SetStringField(TEXT("weight_dtype"), Settings->FluxWeightDtype);
		Workflow->SetObjectField(OutModelId, MakeNode(TEXT("UNETLoader"), Inputs));
	}

	// DualCLIPLoader — loads T5-XXL + CLIP-L for Flux text encoding
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("clip_name1"), Settings->FluxCLIPName1);
		Inputs->SetStringField(TEXT("clip_name2"), Settings->FluxCLIPName2);
		Inputs->SetStringField(TEXT("type"), Settings->FluxCLIPType);
		Workflow->SetObjectField(OutClipId, MakeNode(TEXT("DualCLIPLoader"), Inputs));
	}

	// VAELoader — loads the Flux VAE
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("vae_name"), Settings->FluxVAEName);
		Workflow->SetObjectField(OutVAEId, MakeNode(TEXT("VAELoader"), Inputs));
	}
}

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildImg2ImgWorkflow(
	const FString& ViewportFilename,
	const FString& DepthFilename,
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);
	const bool bFluxMode = Settings->bUseFluxControlNet;

	// Node IDs
	FString CheckpointId = TEXT("1");
	FString PosClipId = TEXT("2");
	FString NegClipId = TEXT("3");
	FString LoadImageId = TEXT("4");
	FString VAEEncodeId = TEXT("5");
	FString KSamplerId = TEXT("6");
	FString VAEDecodeId = TEXT("7");
	FString SaveImageId = TEXT("8");

	// Track which node provides MODEL, CLIP, and VAE
	FString ModelSourceId = CheckpointId;
	FString ClipSourceId = CheckpointId;
	FString VAESourceId = CheckpointId;
	int32 ClipOutputIndex = 1;  // CheckpointLoaderSimple: CLIP at output 1
	int32 VAEOutputIndex = 2;   // CheckpointLoaderSimple: VAE at output 2
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		// ===== Flux: use UNETLoader + DualCLIPLoader + VAELoader =====
		FString FluxModelId, FluxClipId, FluxVAEId;
		AddFluxLoaderNodes(Workflow, NextNodeId, FluxModelId, FluxClipId, FluxVAEId);
		ModelSourceId = FluxModelId;
		ClipSourceId = FluxClipId;
		VAESourceId = FluxVAEId;
		ClipOutputIndex = 0;  // DualCLIPLoader: CLIP at output 0
		VAEOutputIndex = 0;   // VAELoader: VAE at output 0
	}
	else
	{
		// ===== Standard SD: CheckpointLoaderSimple =====
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("ckpt_name"), Settings->CheckpointName);
		Workflow->SetObjectField(CheckpointId, MakeNode(TEXT("CheckpointLoaderSimple"), Inputs));
	}

	// -- LoRA chain (if any active LoRAs) --
	for (const FLoRAEntry& LoRA : ActiveLoRAs)
	{
		if (!LoRA.bEnabled || LoRA.PathOrIdentifier.IsEmpty())
		{
			continue;
		}

		FString LoRANodeId = FString::FromInt(NextNodeId++);

		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("lora_name"), LoRA.PathOrIdentifier);
		Inputs->SetNumberField(TEXT("strength_model"), LoRA.Weight);
		Inputs->SetNumberField(TEXT("strength_clip"), LoRA.Weight);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));

		Workflow->SetObjectField(LoRANodeId, MakeNode(TEXT("LoraLoader"), Inputs));

		ModelSourceId = LoRANodeId;
		ClipSourceId = LoRANodeId;
		ClipOutputIndex = 1; // LoraLoader always outputs CLIP at index 1
	}

	// -- CLIPTextEncode (Positive) --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), Prompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(PosClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// -- CLIPTextEncode (Negative) --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), NegativePrompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(NegClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// -- LoadImage (viewport capture) --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("image"), ViewportFilename);
		Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
	}

	// -- VAEEncode (encode the viewport image for img2img) --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("pixels"), MakeLink(LoadImageId, 0));
		Inputs->SetField(TEXT("vae"), MakeLink(VAESourceId, VAEOutputIndex));
		Workflow->SetObjectField(VAEEncodeId, MakeNode(TEXT("VAEEncode"), Inputs));
	}

	// Track what provides the positive conditioning (changes if ControlNet is added)
	FString PositiveCondSourceId = PosClipId;
	FString NegativeCondSourceId = NegClipId;
	bool bUsedFluxSampler = false;

	// -- ControlNet nodes (if depth is available) --
	if (Settings->bEnableDepthControlNet && !DepthFilename.IsEmpty())
	{
		if (bFluxMode)
		{
			// ===== Flux (XLabs) ControlNet path =====
			FString FluxCNLoaderId = FString::FromInt(NextNodeId++);
			FString FluxCNApplyId = FString::FromInt(NextNodeId++);
			FString DepthLoadImageId = FString::FromInt(NextNodeId++);

			// Load the depth image
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetStringField(TEXT("image"), DepthFilename);
				Workflow->SetObjectField(DepthLoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
			}

			// LoadFluxControlNet
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetStringField(TEXT("model_name"), Settings->FluxModelName);
				Inputs->SetStringField(TEXT("controlnet_path"), Settings->ControlNetModel);
				Workflow->SetObjectField(FluxCNLoaderId, MakeNode(TEXT("LoadFluxControlNet"), Inputs));
			}

			// ApplyFluxControlNet
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetField(TEXT("controlnet"), MakeLink(FluxCNLoaderId, 0));
				Inputs->SetField(TEXT("image"), MakeLink(DepthLoadImageId, 0));
				Inputs->SetNumberField(TEXT("strength"), Settings->ControlNetWeight);
				Workflow->SetObjectField(FluxCNApplyId, MakeNode(TEXT("ApplyFluxControlNet"), Inputs));
			}

			// XlabsSampler (replaces KSampler)
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
				Inputs->SetField(TEXT("conditioning"), MakeLink(PosClipId, 0));
				Inputs->SetField(TEXT("neg_conditioning"), MakeLink(NegClipId, 0));
				Inputs->SetField(TEXT("latent_image"), MakeLink(VAEEncodeId, 0));
				Inputs->SetField(TEXT("controlnet_condition"), MakeLink(FluxCNApplyId, 0));
				Inputs->SetNumberField(TEXT("noise_seed"), Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
				Inputs->SetNumberField(TEXT("steps"), Settings->Steps);
				Inputs->SetNumberField(TEXT("true_gs"), Settings->CFGScale);
				Inputs->SetNumberField(TEXT("timestep_to_start_cfg"), 1);
				Inputs->SetNumberField(TEXT("image_to_image_strength"), Settings->DenoisingStrength);
				Inputs->SetNumberField(TEXT("denoise_strength"), Settings->DenoisingStrength);
				Workflow->SetObjectField(KSamplerId, MakeNode(TEXT("XlabsSampler"), Inputs));
			}

			bUsedFluxSampler = true;
		}
		else
		{
			// ===== Standard SD ControlNet path =====
			FString ControlNetLoaderId = FString::FromInt(NextNodeId++);
			FString ControlNetApplyId = FString::FromInt(NextNodeId++);
			FString DepthLoadImageId = FString::FromInt(NextNodeId++);

			// Load the depth image
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetStringField(TEXT("image"), DepthFilename);
				Workflow->SetObjectField(DepthLoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
			}

			// Load ControlNet model
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetStringField(TEXT("control_net_name"), Settings->ControlNetModel);
				Workflow->SetObjectField(ControlNetLoaderId, MakeNode(TEXT("ControlNetLoader"), Inputs));
			}

			// Apply ControlNet
			{
				TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
				Inputs->SetNumberField(TEXT("strength"), Settings->ControlNetWeight);
				Inputs->SetField(TEXT("conditioning"), MakeLink(PosClipId, 0));
				Inputs->SetField(TEXT("control_net"), MakeLink(ControlNetLoaderId, 0));
				Inputs->SetField(TEXT("image"), MakeLink(DepthLoadImageId, 0));
				Workflow->SetObjectField(ControlNetApplyId, MakeNode(TEXT("ControlNetApply"), Inputs));
			}

			PositiveCondSourceId = ControlNetApplyId;
		}
	}

	// -- KSampler (only if Flux sampler wasn't already used) --
	if (!bUsedFluxSampler)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("positive"), MakeLink(PositiveCondSourceId, 0));
		Inputs->SetField(TEXT("negative"), MakeLink(NegativeCondSourceId, 0));
		Inputs->SetField(TEXT("latent_image"), MakeLink(VAEEncodeId, 0));
		Inputs->SetNumberField(TEXT("seed"), Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
		Inputs->SetNumberField(TEXT("steps"), Settings->Steps);
		Inputs->SetNumberField(TEXT("cfg"), Settings->CFGScale);
		Inputs->SetStringField(TEXT("sampler_name"), Settings->SamplerName);
		Inputs->SetStringField(TEXT("scheduler"), Settings->SchedulerName);
		Inputs->SetNumberField(TEXT("denoise"), Settings->DenoisingStrength);
		Workflow->SetObjectField(KSamplerId, MakeNode(TEXT("KSampler"), Inputs));
	}

	// -- VAEDecode --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("samples"), MakeLink(KSamplerId, 0));
		Inputs->SetField(TEXT("vae"), MakeLink(VAESourceId, VAEOutputIndex));
		Workflow->SetObjectField(VAEDecodeId, MakeNode(TEXT("VAEDecode"), Inputs));
	}

	// -- SaveImage --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(VAEDecodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen"));
		Workflow->SetObjectField(SaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	// -- Optional Hi-Res Fix --
	if (Settings->bEnableHiResFix)
	{
		AppendHiResFix(Workflow, KSamplerId, ModelSourceId, PositiveCondSourceId,
			NegativeCondSourceId, VAESourceId, VAEOutputIndex, VAEDecodeId, SaveImageId, NextNodeId);
	}

	return Workflow;
}

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildTxt2ImgWorkflow(
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);
	const bool bFluxMode = Settings->bUseFluxControlNet;

	FString CheckpointId = TEXT("1");
	FString PosClipId = TEXT("2");
	FString NegClipId = TEXT("3");
	FString EmptyLatentId = TEXT("4");
	FString KSamplerId = TEXT("5");
	FString VAEDecodeId = TEXT("6");
	FString SaveImageId = TEXT("7");

	FString ModelSourceId = CheckpointId;
	FString ClipSourceId = CheckpointId;
	FString VAESourceId = CheckpointId;
	int32 ClipOutputIndex = 1;
	int32 VAEOutputIndex = 2;
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		// ===== Flux: use UNETLoader + DualCLIPLoader + VAELoader =====
		FString FluxModelId, FluxClipId, FluxVAEId;
		AddFluxLoaderNodes(Workflow, NextNodeId, FluxModelId, FluxClipId, FluxVAEId);
		ModelSourceId = FluxModelId;
		ClipSourceId = FluxClipId;
		VAESourceId = FluxVAEId;
		ClipOutputIndex = 0;
		VAEOutputIndex = 0;
	}
	else
	{
		// ===== Standard SD: CheckpointLoaderSimple =====
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("ckpt_name"), Settings->CheckpointName);
		Workflow->SetObjectField(CheckpointId, MakeNode(TEXT("CheckpointLoaderSimple"), Inputs));
	}

	// LoRA chain
	for (const FLoRAEntry& LoRA : ActiveLoRAs)
	{
		if (!LoRA.bEnabled || LoRA.PathOrIdentifier.IsEmpty()) continue;

		FString LoRANodeId = FString::FromInt(NextNodeId++);
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("lora_name"), LoRA.PathOrIdentifier);
		Inputs->SetNumberField(TEXT("strength_model"), LoRA.Weight);
		Inputs->SetNumberField(TEXT("strength_clip"), LoRA.Weight);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(LoRANodeId, MakeNode(TEXT("LoraLoader"), Inputs));
		ModelSourceId = LoRANodeId;
		ClipSourceId = LoRANodeId;
		ClipOutputIndex = 1; // LoraLoader always outputs CLIP at index 1
	}

	// Positive prompt
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), Prompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(PosClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// Negative prompt
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), NegativePrompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(NegClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// Empty latent image
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetNumberField(TEXT("width"), Settings->OutputWidth);
		Inputs->SetNumberField(TEXT("height"), Settings->OutputHeight);
		Inputs->SetNumberField(TEXT("batch_size"), 1);
		Workflow->SetObjectField(EmptyLatentId, MakeNode(TEXT("EmptyLatentImage"), Inputs));
	}

	// KSampler
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("positive"), MakeLink(PosClipId, 0));
		Inputs->SetField(TEXT("negative"), MakeLink(NegClipId, 0));
		Inputs->SetField(TEXT("latent_image"), MakeLink(EmptyLatentId, 0));
		Inputs->SetNumberField(TEXT("seed"), Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
		Inputs->SetNumberField(TEXT("steps"), Settings->Steps);
		Inputs->SetNumberField(TEXT("cfg"), Settings->CFGScale);
		Inputs->SetStringField(TEXT("sampler_name"), Settings->SamplerName);
		Inputs->SetStringField(TEXT("scheduler"), Settings->SchedulerName);
		Inputs->SetNumberField(TEXT("denoise"), 1.0);
		Workflow->SetObjectField(KSamplerId, MakeNode(TEXT("KSampler"), Inputs));
	}

	// VAEDecode
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("samples"), MakeLink(KSamplerId, 0));
		Inputs->SetField(TEXT("vae"), MakeLink(VAESourceId, VAEOutputIndex));
		Workflow->SetObjectField(VAEDecodeId, MakeNode(TEXT("VAEDecode"), Inputs));
	}

	// SaveImage
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(VAEDecodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen"));
		Workflow->SetObjectField(SaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	// Optional Hi-Res Fix
	if (Settings->bEnableHiResFix)
	{
		AppendHiResFix(Workflow, KSamplerId, ModelSourceId, PosClipId,
			NegClipId, VAESourceId, VAEOutputIndex, VAEDecodeId, SaveImageId, NextNodeId);
	}

	return Workflow;
}

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildDepthOnlyWorkflow(
	const FString& DepthFilename,
	const FString& Prompt,
	const FString& NegativePrompt,
	const TArray<FLoRAEntry>& ActiveLoRAs) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);
	const bool bFluxMode = Settings->bUseFluxControlNet;

	// Node IDs
	FString CheckpointId = TEXT("1");
	FString PosClipId = TEXT("2");
	FString NegClipId = TEXT("3");
	FString EmptyLatentId = TEXT("4");
	FString KSamplerId = TEXT("5");
	FString VAEDecodeId = TEXT("6");
	FString SaveImageId = TEXT("7");

	FString ModelSourceId = CheckpointId;
	FString ClipSourceId = CheckpointId;
	FString VAESourceId = CheckpointId;
	int32 ClipOutputIndex = 1;
	int32 VAEOutputIndex = 2;
	int32 NextNodeId = 20;

	if (bFluxMode)
	{
		// ===== Flux: use UNETLoader + DualCLIPLoader + VAELoader =====
		FString FluxModelId, FluxClipId, FluxVAEId;
		AddFluxLoaderNodes(Workflow, NextNodeId, FluxModelId, FluxClipId, FluxVAEId);
		ModelSourceId = FluxModelId;
		ClipSourceId = FluxClipId;
		VAESourceId = FluxVAEId;
		ClipOutputIndex = 0;
		VAEOutputIndex = 0;
	}
	else
	{
		// ===== Standard SD: CheckpointLoaderSimple =====
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("ckpt_name"), Settings->CheckpointName);
		Workflow->SetObjectField(CheckpointId, MakeNode(TEXT("CheckpointLoaderSimple"), Inputs));
	}

	// -- LoRA chain --
	for (const FLoRAEntry& LoRA : ActiveLoRAs)
	{
		if (!LoRA.bEnabled || LoRA.PathOrIdentifier.IsEmpty()) continue;

		FString LoRANodeId = FString::FromInt(NextNodeId++);
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("lora_name"), LoRA.PathOrIdentifier);
		Inputs->SetNumberField(TEXT("strength_model"), LoRA.Weight);
		Inputs->SetNumberField(TEXT("strength_clip"), LoRA.Weight);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(LoRANodeId, MakeNode(TEXT("LoraLoader"), Inputs));
		ModelSourceId = LoRANodeId;
		ClipSourceId = LoRANodeId;
		ClipOutputIndex = 1; // LoraLoader always outputs CLIP at index 1
	}

	// -- Positive prompt --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), Prompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(PosClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// -- Negative prompt --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("text"), NegativePrompt);
		Inputs->SetField(TEXT("clip"), MakeLink(ClipSourceId, ClipOutputIndex));
		Workflow->SetObjectField(NegClipId, MakeNode(TEXT("CLIPTextEncode"), Inputs));
	}

	// -- Empty latent (full txt2img generation, no img2img encoding) --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetNumberField(TEXT("width"), Settings->OutputWidth);
		Inputs->SetNumberField(TEXT("height"), Settings->OutputHeight);
		Inputs->SetNumberField(TEXT("batch_size"), 1);
		Workflow->SetObjectField(EmptyLatentId, MakeNode(TEXT("EmptyLatentImage"), Inputs));
	}

	// -- ControlNet depth guidance --
	FString PositiveCondSourceId = PosClipId;

	if (bFluxMode)
	{
		// ===== Flux (XLabs) ControlNet path =====
		// Uses LoadFluxControlNet + ApplyFluxControlNet + XlabsSampler
		FString FluxCNLoaderId = FString::FromInt(NextNodeId++);
		FString FluxCNApplyId = FString::FromInt(NextNodeId++);
		FString DepthLoadImageId = FString::FromInt(NextNodeId++);

		// Load the depth image
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetStringField(TEXT("image"), DepthFilename);
			Workflow->SetObjectField(DepthLoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
		}

		// LoadFluxControlNet
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetStringField(TEXT("model_name"), Settings->FluxModelName);
			Inputs->SetStringField(TEXT("controlnet_path"), Settings->ControlNetModel);
			Workflow->SetObjectField(FluxCNLoaderId, MakeNode(TEXT("LoadFluxControlNet"), Inputs));
		}

		// ApplyFluxControlNet → outputs controlnet_condition
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetField(TEXT("controlnet"), MakeLink(FluxCNLoaderId, 0));
			Inputs->SetField(TEXT("image"), MakeLink(DepthLoadImageId, 0));
			Inputs->SetNumberField(TEXT("strength"), Settings->ControlNetWeight);
			Workflow->SetObjectField(FluxCNApplyId, MakeNode(TEXT("ApplyFluxControlNet"), Inputs));
		}

		// XlabsSampler (replaces KSampler when using Flux ControlNet)
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
			Inputs->SetField(TEXT("conditioning"), MakeLink(PosClipId, 0));
			Inputs->SetField(TEXT("neg_conditioning"), MakeLink(NegClipId, 0));
			Inputs->SetField(TEXT("latent_image"), MakeLink(EmptyLatentId, 0));
			Inputs->SetField(TEXT("controlnet_condition"), MakeLink(FluxCNApplyId, 0));
			Inputs->SetNumberField(TEXT("noise_seed"), Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
			Inputs->SetNumberField(TEXT("steps"), Settings->Steps);
			Inputs->SetNumberField(TEXT("true_gs"), Settings->CFGScale);
			Inputs->SetNumberField(TEXT("timestep_to_start_cfg"), 1);
			Inputs->SetNumberField(TEXT("image_to_image_strength"), 1.0); // Full generation
			Inputs->SetNumberField(TEXT("denoise_strength"), 1.0); // Full denoise — no img2img
			Workflow->SetObjectField(KSamplerId, MakeNode(TEXT("XlabsSampler"), Inputs));
		}
	}
	else
	{
		// ===== Standard SD ControlNet path =====
		FString ControlNetLoaderId = FString::FromInt(NextNodeId++);
		FString ControlNetApplyId = FString::FromInt(NextNodeId++);
		FString DepthLoadImageId = FString::FromInt(NextNodeId++);

		// Load the depth image
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetStringField(TEXT("image"), DepthFilename);
			Workflow->SetObjectField(DepthLoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
		}

		// Load ControlNet model
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetStringField(TEXT("control_net_name"), Settings->ControlNetModel);
			Workflow->SetObjectField(ControlNetLoaderId, MakeNode(TEXT("ControlNetLoader"), Inputs));
		}

		// Apply ControlNet to positive conditioning
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetNumberField(TEXT("strength"), Settings->ControlNetWeight);
			Inputs->SetField(TEXT("conditioning"), MakeLink(PosClipId, 0));
			Inputs->SetField(TEXT("control_net"), MakeLink(ControlNetLoaderId, 0));
			Inputs->SetField(TEXT("image"), MakeLink(DepthLoadImageId, 0));
			Workflow->SetObjectField(ControlNetApplyId, MakeNode(TEXT("ControlNetApply"), Inputs));
		}

		PositiveCondSourceId = ControlNetApplyId;

		// -- KSampler (full denoise = 1.0, since there's no input image) --
		{
			TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
			Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
			Inputs->SetField(TEXT("positive"), MakeLink(PositiveCondSourceId, 0));
			Inputs->SetField(TEXT("negative"), MakeLink(NegClipId, 0));
			Inputs->SetField(TEXT("latent_image"), MakeLink(EmptyLatentId, 0));
			Inputs->SetNumberField(TEXT("seed"), Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
			Inputs->SetNumberField(TEXT("steps"), Settings->Steps);
			Inputs->SetNumberField(TEXT("cfg"), Settings->CFGScale);
			Inputs->SetStringField(TEXT("sampler_name"), Settings->SamplerName);
			Inputs->SetStringField(TEXT("scheduler"), Settings->SchedulerName);
			Inputs->SetNumberField(TEXT("denoise"), 1.0); // Full denoise — no img2img
			Workflow->SetObjectField(KSamplerId, MakeNode(TEXT("KSampler"), Inputs));
		}
	}

	// -- VAEDecode --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("samples"), MakeLink(KSamplerId, 0));
		Inputs->SetField(TEXT("vae"), MakeLink(VAESourceId, VAEOutputIndex));
		Workflow->SetObjectField(VAEDecodeId, MakeNode(TEXT("VAEDecode"), Inputs));
	}

	// -- SaveImage --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(VAEDecodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen"));
		Workflow->SetObjectField(SaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	// Optional Hi-Res Fix
	if (Settings->bEnableHiResFix)
	{
		AppendHiResFix(Workflow, KSamplerId, ModelSourceId, PositiveCondSourceId,
			NegClipId, VAESourceId, VAEOutputIndex, VAEDecodeId, SaveImageId, NextNodeId);
	}

	return Workflow;
}

// ============================================================================
// Gemini (Nano Banana 2) Workflow
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildGeminiWorkflow(
	const FString& ViewportFilename,
	const FString& DepthFilename,
	const FString& Prompt) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	int32 NextNodeId = 1;
	FString NanoBananaId = FString::FromInt(NextNodeId++);
	FString SaveImageId = FString::FromInt(NextNodeId++);

	// -- GeminiNanoBanana2 node --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("prompt"), Prompt);
		Inputs->SetStringField(TEXT("model"), Settings->GeminiModelName);
		Inputs->SetNumberField(TEXT("seed"), static_cast<double>(Settings->GeminiSeed));
		Inputs->SetStringField(TEXT("aspect_ratio"), Settings->GeminiAspectRatio);
		Inputs->SetStringField(TEXT("resolution"), Settings->GeminiResolution);
		Inputs->SetStringField(TEXT("response_modalities"), Settings->GeminiResponseModalities);
		Inputs->SetStringField(TEXT("thinking_level"), Settings->GeminiThinkingLevel);

		// Optional system prompt
		if (!Settings->GeminiSystemPrompt.IsEmpty())
		{
			Inputs->SetStringField(TEXT("system_prompt"), Settings->GeminiSystemPrompt);
		}

		// Connect reference images via the optional "images" input.
		// If both viewport and depth are available, batch them with ImageBatch.
		// If only one is available, connect it directly.
		bool bHasViewport = !ViewportFilename.IsEmpty();
		bool bHasDepth = !DepthFilename.IsEmpty();

		FString ViewportLoadId, DepthLoadId;

		if (bHasViewport)
		{
			ViewportLoadId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), ViewportFilename);
			Workflow->SetObjectField(ViewportLoadId, MakeNode(TEXT("LoadImage"), LoadInputs));
		}

		if (bHasDepth)
		{
			DepthLoadId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), DepthFilename);
			Workflow->SetObjectField(DepthLoadId, MakeNode(TEXT("LoadImage"), LoadInputs));
		}

		if (bHasViewport && bHasDepth)
		{
			// Batch both images together into the single "images" input
			FString BatchId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> BatchInputs = MakeShareable(new FJsonObject);
			BatchInputs->SetField(TEXT("image1"), MakeLink(ViewportLoadId, 0));
			BatchInputs->SetField(TEXT("image2"), MakeLink(DepthLoadId, 0));
			Workflow->SetObjectField(BatchId, MakeNode(TEXT("ImageBatch"), BatchInputs));

			Inputs->SetField(TEXT("images"), MakeLink(BatchId, 0));
		}
		else if (bHasViewport)
		{
			Inputs->SetField(TEXT("images"), MakeLink(ViewportLoadId, 0));
		}
		else if (bHasDepth)
		{
			Inputs->SetField(TEXT("images"), MakeLink(DepthLoadId, 0));
		}

		Workflow->SetObjectField(NanoBananaId, MakeNode(TEXT("GeminiNanoBanana2"), Inputs));
	}

	// -- SaveImage --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(NanoBananaId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_Gemini"));
		Workflow->SetObjectField(SaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	return Workflow;
}

// ============================================================================
// Kling Workflow
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildKlingWorkflow(
	const FString& ViewportFilename,
	const FString& Prompt,
	const FString& NegativePrompt) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	int32 NextNodeId = 1;
	FString KlingNodeId = FString::FromInt(NextNodeId++);
	FString SaveImageId = FString::FromInt(NextNodeId++);

	// -- KlingImageGenerationNode --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("prompt"), Prompt);
		Inputs->SetStringField(TEXT("negative_prompt"), NegativePrompt);
		Inputs->SetStringField(TEXT("image_type"), Settings->KlingImageType);
		Inputs->SetNumberField(TEXT("image_fidelity"), Settings->KlingImageFidelity);
		Inputs->SetNumberField(TEXT("human_fidelity"), Settings->KlingHumanFidelity);
		Inputs->SetStringField(TEXT("model_name"), Settings->KlingModelName);
		Inputs->SetStringField(TEXT("aspect_ratio"), Settings->KlingAspectRatio);
		Inputs->SetNumberField(TEXT("n"), Settings->KlingImageCount);

		// Optional seed
		if (Settings->KlingSeed > 0)
		{
			Inputs->SetNumberField(TEXT("seed"), static_cast<double>(Settings->KlingSeed));
		}

		// Optional reference image from viewport capture
		if (!ViewportFilename.IsEmpty())
		{
			FString LoadImageId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), ViewportFilename);
			Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), LoadInputs));

			Inputs->SetField(TEXT("image"), MakeLink(LoadImageId, 0));
		}

		Workflow->SetObjectField(KlingNodeId, MakeNode(TEXT("KlingImageGenerationNode"), Inputs));
	}

	// -- SaveImage --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(KlingNodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_Kling"));
		Workflow->SetObjectField(SaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	return Workflow;
}

// ============================================================================
// Kling Video Workflow
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildKlingVideoWorkflow(
	const FString& SourceFrameFilename,
	const FString& Prompt,
	const FString& NegativePrompt) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	int32 NextNodeId = 1;
	FString KlingVideoId = FString::FromInt(NextNodeId++);
	FString SaveVideoId = FString::FromInt(NextNodeId++);

	// -- KlingImage2VideoNode ("Kling Image(First Frame) to Video") --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("prompt"), Prompt);
		Inputs->SetStringField(TEXT("negative_prompt"), NegativePrompt);
		// Migrate stale "kling-v2" config value to "kling-v2-master"
		FString KlingModel = Settings->KlingVideoModel;
		if (KlingModel == TEXT("kling-v2"))
		{
			KlingModel = TEXT("kling-v2-master");
		}
		Inputs->SetStringField(TEXT("model_name"), KlingModel);
		Inputs->SetStringField(TEXT("mode"), Settings->KlingVideoQuality);
		// duration is a COMBO (string "5" or "10"), not a number
		Inputs->SetStringField(TEXT("duration"), FString::Printf(TEXT("%d"), FMath::Clamp(static_cast<int32>(Settings->VideoDuration), 5, 10)));
		// cfg_scale is 0.0–1.0 (default 0.8)
		Inputs->SetNumberField(TEXT("cfg_scale"), FMath::Clamp(Settings->VideoCFG, 0.0f, 1.0f));
		Inputs->SetStringField(TEXT("aspect_ratio"), TEXT("16:9"));

		// Source frame as start_frame input
		if (!SourceFrameFilename.IsEmpty())
		{
			FString LoadImageId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), SourceFrameFilename);
			Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), LoadInputs));

			Inputs->SetField(TEXT("start_frame"), MakeLink(LoadImageId, 0));
		}

		Workflow->SetObjectField(KlingVideoId, MakeNode(TEXT("KlingImage2VideoNode"), Inputs));
	}

	// -- SaveVideo to save the VIDEO output --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("video"), MakeLink(KlingVideoId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_KlingVideo"));
		Inputs->SetStringField(TEXT("format"), TEXT("auto"));
		Inputs->SetStringField(TEXT("codec"), TEXT("auto"));
		Workflow->SetObjectField(SaveVideoId, MakeNode(TEXT("SaveVideo"), Inputs));
	}

	return Workflow;
}

// ============================================================================
// Veo3 Workflow
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildVeo3Workflow(
	const FString& SourceFrameFilename,
	const FString& Prompt) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	int32 NextNodeId = 1;
	FString Veo3NodeId = FString::FromInt(NextNodeId++);
	FString SaveVideoId = FString::FromInt(NextNodeId++);

	// -- Veo3VideoGenerationNode ("Google Veo 3 Video Generation") --
	// Required: prompt, aspect_ratio
	// Optional: model, generate_audio, person_generation, duration_seconds (fixed at 8),
	//           enhance_prompt (deprecated), seed, negative_prompt, image
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		// Required
		Inputs->SetStringField(TEXT("prompt"), Prompt);
		Inputs->SetStringField(TEXT("aspect_ratio"), Settings->Veo3AspectRatio);
		// Optional — migrate stale config values
		FString Veo3Model = Settings->Veo3ModelName;
		if (Veo3Model == TEXT("veo-3.0-generate-preview"))
		{
			Veo3Model = TEXT("veo-3.0-generate-001");
		}
		Inputs->SetStringField(TEXT("model"), Veo3Model);
		Inputs->SetBoolField(TEXT("generate_audio"), Settings->bVeo3GenerateAudio);
		FString PersonGen = Settings->Veo3PersonGeneration;
		if (PersonGen == TEXT("allow_adult"))
		{
			PersonGen = TEXT("ALLOW");
		}
		else if (PersonGen == TEXT("dont_allow"))
		{
			PersonGen = TEXT("BLOCK");
		}
		Inputs->SetStringField(TEXT("person_generation"), PersonGen);
		Inputs->SetNumberField(TEXT("duration_seconds"), 8); // Veo3 only supports 8 seconds
		Inputs->SetNumberField(TEXT("seed"), static_cast<double>(Settings->VideoSeed));

		// Source frame as reference image
		if (!SourceFrameFilename.IsEmpty())
		{
			FString LoadImageId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), SourceFrameFilename);
			Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), LoadInputs));

			Inputs->SetField(TEXT("image"), MakeLink(LoadImageId, 0));
		}

		Workflow->SetObjectField(Veo3NodeId, MakeNode(TEXT("Veo3VideoGenerationNode"), Inputs));
	}

	// -- SaveVideo to save the VIDEO output --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("video"), MakeLink(Veo3NodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_Veo3"));
		Inputs->SetStringField(TEXT("format"), TEXT("auto"));
		Inputs->SetStringField(TEXT("codec"), TEXT("auto"));
		Workflow->SetObjectField(SaveVideoId, MakeNode(TEXT("SaveVideo"), Inputs));
	}

	return Workflow;
}

// ============================================================================
// Wan I2V Workflow
// ============================================================================

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildWanVideoWorkflow(
	const FString& SourceFrameFilename,
	const FString& Prompt,
	const FString& NegativePrompt) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	int32 NextNodeId = 1;
	FString WanNodeId = FString::FromInt(NextNodeId++);
	FString SaveVideoId = FString::FromInt(NextNodeId++);

	// -- WanImageToVideoApi --
	// Required: model, image, prompt
	// Optional: negative_prompt, resolution, duration (5/10/15), seed,
	//           generate_audio, prompt_extend, watermark, shot_type
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		// Required
		Inputs->SetStringField(TEXT("prompt"), Prompt);
		// Migrate stale config value from local checkpoint name to API model name
		FString WanModel = Settings->WanModelName;
		if (WanModel.Contains(TEXT(".safetensors")) || WanModel.Contains(TEXT("wan2.1")))
		{
			WanModel = TEXT("wan2.6-i2v");
		}
		Inputs->SetStringField(TEXT("model"), WanModel);
		// Optional
		if (!NegativePrompt.IsEmpty())
		{
			Inputs->SetStringField(TEXT("negative_prompt"), NegativePrompt);
		}
		Inputs->SetStringField(TEXT("resolution"), TEXT("720P"));
		// Duration: 5, 10, or 15 (step 5). Clamp and round to nearest 5.
		int32 Dur = FMath::Clamp(FMath::RoundToInt(Settings->VideoDuration), 5, 15);
		Dur = FMath::RoundToInt(static_cast<float>(Dur) / 5.0f) * 5; // snap to 5/10/15
		Inputs->SetNumberField(TEXT("duration"), static_cast<double>(Dur));
		Inputs->SetNumberField(TEXT("seed"), static_cast<double>(Settings->VideoSeed));
		Inputs->SetBoolField(TEXT("prompt_extend"), true);
		Inputs->SetBoolField(TEXT("watermark"), false);

		// Source frame as image input
		if (!SourceFrameFilename.IsEmpty())
		{
			FString LoadImageId = FString::FromInt(NextNodeId++);
			TSharedPtr<FJsonObject> LoadInputs = MakeShareable(new FJsonObject);
			LoadInputs->SetStringField(TEXT("image"), SourceFrameFilename);
			Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), LoadInputs));

			Inputs->SetField(TEXT("image"), MakeLink(LoadImageId, 0));
		}

		Workflow->SetObjectField(WanNodeId, MakeNode(TEXT("WanImageToVideoApi"), Inputs));
	}

	// -- SaveVideo to save the VIDEO output --
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("video"), MakeLink(WanNodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_Wan"));
		Inputs->SetStringField(TEXT("format"), TEXT("auto"));
		Inputs->SetStringField(TEXT("codec"), TEXT("auto"));
		Workflow->SetObjectField(SaveVideoId, MakeNode(TEXT("SaveVideo"), Inputs));
	}

	return Workflow;
}

// ============================================================================
// Hi-Res Fix
// ============================================================================

void FGenAIHttpClient::AppendHiResFix(
	TSharedPtr<FJsonObject> Workflow,
	const FString& FirstPassKSamplerId,
	const FString& ModelSourceId,
	const FString& PositiveCondId,
	const FString& NegativeCondId,
	const FString& VAESourceId,
	int32 VAEOutputIndex,
	const FString& OldVAEDecodeId,
	const FString& OldSaveImageId,
	int32& NextNodeId) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	FString LatentUpscaleId = FString::FromInt(NextNodeId++);
	FString HiResKSamplerId = FString::FromInt(NextNodeId++);
	FString HiResVAEDecodeId = FString::FromInt(NextNodeId++);
	FString HiResSaveImageId = FString::FromInt(NextNodeId++);

	// Remove old VAEDecode and SaveImage — we'll replace them with upscaled versions
	Workflow->RemoveField(OldVAEDecodeId);
	Workflow->RemoveField(OldSaveImageId);

	// LatentUpscale: upscale the first-pass latent
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("samples"), MakeLink(FirstPassKSamplerId, 0));
		Inputs->SetStringField(TEXT("upscale_method"), TEXT("nearest-exact"));
		Inputs->SetNumberField(TEXT("width"),
			FMath::RoundToInt(Settings->OutputWidth * Settings->HiResUpscaleFactor));
		Inputs->SetNumberField(TEXT("height"),
			FMath::RoundToInt(Settings->OutputHeight * Settings->HiResUpscaleFactor));
		Inputs->SetStringField(TEXT("crop"), TEXT("disabled"));
		Workflow->SetObjectField(LatentUpscaleId, MakeNode(TEXT("LatentUpscale"), Inputs));
	}

	// Second-pass KSampler: refine the upscaled latent at lower denoise
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("model"), MakeLink(ModelSourceId, 0));
		Inputs->SetField(TEXT("positive"), MakeLink(PositiveCondId, 0));
		Inputs->SetField(TEXT("negative"), MakeLink(NegativeCondId, 0));
		Inputs->SetField(TEXT("latent_image"), MakeLink(LatentUpscaleId, 0));
		Inputs->SetNumberField(TEXT("seed"),
			Settings->Seed > 0 ? Settings->Seed : FMath::RandRange(0, 2147483647));
		Inputs->SetNumberField(TEXT("steps"), Settings->HiResSteps);
		Inputs->SetNumberField(TEXT("cfg"), Settings->CFGScale);
		Inputs->SetStringField(TEXT("sampler_name"), Settings->SamplerName);
		Inputs->SetStringField(TEXT("scheduler"), Settings->SchedulerName);
		Inputs->SetNumberField(TEXT("denoise"), Settings->HiResDenoise);
		Workflow->SetObjectField(HiResKSamplerId, MakeNode(TEXT("KSampler"), Inputs));
	}

	// VAEDecode the hi-res result (uses VAESourceId which may be a VAELoader or checkpoint)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("samples"), MakeLink(HiResKSamplerId, 0));
		Inputs->SetField(TEXT("vae"), MakeLink(VAESourceId, VAEOutputIndex));
		Workflow->SetObjectField(HiResVAEDecodeId, MakeNode(TEXT("VAEDecode"), Inputs));
	}

	// SaveImage from hi-res output
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(HiResVAEDecodeId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_HiRes"));
		Workflow->SetObjectField(HiResSaveImageId, MakeNode(TEXT("SaveImage"), Inputs));
	}
}

// ============================================================================
// Prompt Submission
// ============================================================================

void FGenAIHttpClient::SubmitPrompt(TSharedPtr<FJsonObject> Workflow)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString PromptURL = Settings->APIEndpointURL / TEXT("prompt");

	// Wrap workflow in the /prompt payload: { "prompt": {...workflow...}, "client_id": "...", "extra_data": {...} }
	TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
	Payload->SetObjectField(TEXT("prompt"), Workflow);
	Payload->SetStringField(TEXT("client_id"), ClientId);

	// Include ComfyUI API key in extra_data if configured (required for partner API nodes like Gemini)
	if (!Settings->ComfyUIApiKey.IsEmpty())
	{
		TSharedPtr<FJsonObject> ExtraData = MakeShareable(new FJsonObject);
		ExtraData->SetStringField(TEXT("api_key_comfy_org"), Settings->ComfyUIApiKey);
		Payload->SetObjectField(TEXT("extra_data"), ExtraData);
	}

	// Hardcoded fixup for known V3 API nodes whose COMFY_DYNAMICCOMBO_V3 inputs
	// get misclassified as connection pins. This runs unconditionally, with zero
	// DB dependency, so it works even if /object_info hasn't been fetched yet.
	{
		for (const auto& NodePair : Workflow->Values)
		{
			TSharedPtr<FJsonObject> NodeObj = NodePair.Value->AsObject();
			if (!NodeObj.IsValid()) continue;

			FString ClassType;
			if (!NodeObj->TryGetStringField(TEXT("class_type"), ClassType)) continue;

			const TSharedPtr<FJsonObject>* InputsPtr;
			if (!NodeObj->TryGetObjectField(TEXT("inputs"), InputsPtr) || !InputsPtr) continue;
			TSharedPtr<FJsonObject> Inputs = *InputsPtr;

			if (ClassType == TEXT("MeshyImageToModelNode"))
			{
				// Ensure should_remesh/should_texture are strings
				if (!Inputs->HasTypedField<EJson::String>(TEXT("should_remesh")))
					Inputs->SetStringField(TEXT("should_remesh"), TEXT("true"));
				if (!Inputs->HasTypedField<EJson::String>(TEXT("should_texture")))
					Inputs->SetStringField(TEXT("should_texture"), TEXT("true"));

				// V3 conditional sub-fields use DOT NOTATION keys
				FString RemeshStr;
				if (Inputs->TryGetStringField(TEXT("should_remesh"), RemeshStr) && RemeshStr == TEXT("true"))
				{
					if (!Inputs->HasField(TEXT("should_remesh.target_polycount")))
						Inputs->SetNumberField(TEXT("should_remesh.target_polycount"), 30000);
					if (!Inputs->HasField(TEXT("should_remesh.topology")))
						Inputs->SetStringField(TEXT("should_remesh.topology"), TEXT("triangle"));
				}
				FString TextureStr;
				if (Inputs->TryGetStringField(TEXT("should_texture"), TextureStr) && TextureStr == TEXT("true"))
				{
					if (!Inputs->HasField(TEXT("should_texture.enable_pbr")))
						Inputs->SetStringField(TEXT("should_texture.enable_pbr"), TEXT("false"));
					if (!Inputs->HasField(TEXT("should_texture.texture_prompt")))
						Inputs->SetStringField(TEXT("should_texture.texture_prompt"), TEXT(""));
				}
				UE_LOG(LogTemp, Log, TEXT("ViewGen: Ensured all MeshyImageToModelNode V3 conditional inputs (id=%s)"), *NodePair.Key);
			}
			// Add other known V3 nodes here as needed
		}
	}

	// DB-dependent fixup: ensure all nodes have their required inputs.
	// Some ComfyUI V3 API inputs (like COMFY_DYNAMICCOMBO_V3) may have been
	// misclassified as connection pins and thus omitted from the workflow JSON.
	// Query the node database and inject any missing required widget-type inputs.
	{
		const FComfyNodeDatabase& DB = FComfyNodeDatabase::Get();
		if (DB.IsPopulated())
		{
			for (const auto& NodePair : Workflow->Values)
			{
				TSharedPtr<FJsonObject> NodeObj = NodePair.Value->AsObject();
				if (!NodeObj.IsValid()) continue;

				FString ClassType;
				if (!NodeObj->TryGetStringField(TEXT("class_type"), ClassType)) continue;

				const FComfyNodeDef* Def = DB.FindNode(ClassType);
				if (!Def) continue;

				const TSharedPtr<FJsonObject>* InputsPtr;
				if (!NodeObj->TryGetObjectField(TEXT("inputs"), InputsPtr) || !InputsPtr) continue;
				TSharedPtr<FJsonObject> Inputs = *InputsPtr;

				for (const FComfyInputDef& DefInput : Def->Inputs)
				{
					if (DefInput.IsLinkType()) continue;           // Skip connection-type inputs
					if (Inputs->HasField(DefInput.Name)) continue; // Already present

					// Missing widget input — emit a default
					if (DefInput.Type == TEXT("COMBO") && DefInput.ComboOptions.Num() > 0)
					{
						FString Val = !DefInput.DefaultString.IsEmpty() ? DefInput.DefaultString : DefInput.ComboOptions[0];
						if (Val == TEXT("true") || Val == TEXT("false"))
							Inputs->SetBoolField(DefInput.Name, Val == TEXT("true"));
						else
							Inputs->SetStringField(DefInput.Name, Val);
					}
					else if (DefInput.Type == TEXT("COMBO"))
					{
						Inputs->SetBoolField(DefInput.Name, true);
					}
					else if (DefInput.Type == TEXT("BOOLEAN") || DefInput.Type == TEXT("BOOL"))
					{
						Inputs->SetBoolField(DefInput.Name, DefInput.DefaultBool);
					}
					else if (DefInput.Type == TEXT("INT"))
					{
						Inputs->SetNumberField(DefInput.Name, (int64)DefInput.DefaultNumber);
					}
					else if (DefInput.Type == TEXT("FLOAT"))
					{
						Inputs->SetNumberField(DefInput.Name, DefInput.DefaultNumber);
					}
					else if (DefInput.Type == TEXT("STRING"))
					{
						Inputs->SetStringField(DefInput.Name, DefInput.DefaultString);
					}

					UE_LOG(LogTemp, Warning, TEXT("ViewGen: Injected missing input '%s' for node '%s' (id=%s)"),
						*DefInput.Name, *ClassType, *NodePair.Key);
				}
			}
		}
	}

	FString PayloadString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

	// Debug: log the workflow JSON so we can inspect what's being sent
	UE_LOG(LogTemp, Log, TEXT("ViewGen: Submitting workflow JSON:\n%s"), *PayloadString);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(PromptURL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(PayloadString);
	Request->SetTimeout(Settings->TimeoutSeconds);

	Request->OnProcessRequestComplete().BindRaw(this, &FGenAIHttpClient::OnPromptResponseReceived);

	CurrentRequest = Request;

	if (!Request->ProcessRequest())
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("Failed to send prompt request to ComfyUI"));
	}
}

void FGenAIHttpClient::OnPromptResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	CurrentRequest.Reset();

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("Connection to ComfyUI failed - is ComfyUI Desktop running?"));
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode != 200)
	{
		bRequestInProgress = false;
		FString ErrorMsg = FString::Printf(TEXT("ComfyUI returned HTTP %d: %s"),
			ResponseCode, *Response->GetContentAsString().Left(500));
		OnError.ExecuteIfBound(ErrorMsg);
		return;
	}

	// Parse prompt_id from response: { "prompt_id": "xxx-xxx-xxx", ... }
	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("Failed to parse ComfyUI /prompt response"));
		return;
	}

	CurrentPromptId = JsonResp->GetStringField(TEXT("prompt_id"));
	if (CurrentPromptId.IsEmpty())
	{
		bRequestInProgress = false;
		OnError.ExecuteIfBound(TEXT("ComfyUI returned empty prompt_id"));
		return;
	}

	// Reset progress tracking state for this new generation
	bExecutionStarted = false;
	PollTickCounter = 0;

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Prompt queued with ID: %s"), *CurrentPromptId);

	// Connect WebSocket for real-time node execution tracking
	ConnectWebSocket();

	// Progress polling is driven by the panel's timer calling PollProgress()
	OnProgress.ExecuteIfBound(0.05f);
}

// ============================================================================
// History Polling
// ============================================================================

void FGenAIHttpClient::PollHistory()
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString HistoryURL = FString::Printf(TEXT("%s/history/%s"),
		*Settings->APIEndpointURL, *CurrentPromptId);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(HistoryURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(10.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FGenAIHttpClient::OnHistoryResponseReceived);
	Request->ProcessRequest();
}

void FGenAIHttpClient::OnHistoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	if (!bRequestInProgress)
	{
		return; // Was cancelled
	}

	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		// Don't error out on poll failures - just try again next tick
		return;
	}

	TSharedPtr<FJsonObject> JsonResp;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResp) || !JsonResp.IsValid())
	{
		return;
	}

	// Check if our prompt_id has a result in the history
	const TSharedPtr<FJsonObject>* PromptResult;
	if (!JsonResp->TryGetObjectField(CurrentPromptId, PromptResult))
	{
		// Not in history yet — queue/node_status polls provide real-time progress
		return;
	}

	// Check for execution errors (e.g. OOM, node failures, LoRA load errors).
	// Note: some errors are non-fatal (e.g. Windows stderr flush [Errno 22]) and
	// ComfyUI may still produce valid outputs despite reporting status "error".
	// We capture the error details but proceed to check for outputs first.
	bool bHasError = false;
	FString ErrorDetail = TEXT("ComfyUI execution error");
	const TSharedPtr<FJsonObject>* StatusObj;
	if ((*PromptResult)->TryGetObjectField(TEXT("status"), StatusObj))
	{
		FString StatusStr;
		if ((*StatusObj)->TryGetStringField(TEXT("status_str"), StatusStr))
		{
			if (StatusStr == TEXT("error"))
			{
				bHasError = true;

				const TArray<TSharedPtr<FJsonValue>>* MessagesArray;
				if ((*StatusObj)->TryGetArrayField(TEXT("messages"), MessagesArray))
				{
					for (const auto& MsgVal : *MessagesArray)
					{
						const TArray<TSharedPtr<FJsonValue>>* MsgPair;
						// Messages are arrays of [type, {data}]
						if (MsgVal->TryGetArray(MsgPair) && MsgPair->Num() >= 2)
						{
							FString MsgType;
							(*MsgPair)[0]->TryGetString(MsgType);
							if (MsgType == TEXT("execution_error"))
							{
								TSharedPtr<FJsonObject> MsgData = (*MsgPair)[1]->AsObject();
								if (MsgData.IsValid())
								{
									FString ExMsg, NodeInfo;
									MsgData->TryGetStringField(TEXT("exception_message"), ExMsg);
									if (!MsgData->TryGetStringField(TEXT("class_type"), NodeInfo))
									{
										MsgData->TryGetStringField(TEXT("node_type"), NodeInfo);
									}
									if (NodeInfo.IsEmpty())
									{
										MsgData->TryGetStringField(TEXT("node_id"), NodeInfo);
									}
									if (!ExMsg.IsEmpty())
									{
										ErrorDetail = FString::Printf(TEXT("Node '%s' failed: %s"),
											*NodeInfo, *ExMsg.Left(300));
									}

									// Log Python traceback from ComfyUI error
									const TArray<TSharedPtr<FJsonValue>>* TBArray;
									if (MsgData->TryGetArrayField(TEXT("traceback"), TBArray))
									{
										FString FullTB;
										for (const auto& TBLine : *TBArray)
										{
											FString TBStr;
											if (TBLine->TryGetString(TBStr)) FullTB += TBStr;
										}
										UE_LOG(LogTemp, Warning, TEXT("ViewGen: Python traceback:\n%s"), *FullTB.Left(3000));
									}
								}
							}
						}
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("ViewGen: %s (checking for partial outputs...)"), *ErrorDetail);
			}
		}
	}

	// Look for outputs -> images (even if status was "error" — outputs may still be valid)
	const TSharedPtr<FJsonObject>* OutputsObj;
	if (!(*PromptResult)->TryGetObjectField(TEXT("outputs"), OutputsObj))
	{
		if (bHasError)
		{
			// Error with no outputs at all — report the error
			bRequestInProgress = false;
			bIsSAM3Request = false;
			SAM3SourceTexture = nullptr;
			PendingSegmentation.Reset();
			CurrentPromptId.Empty();
			UE_LOG(LogTemp, Error, TEXT("ViewGen: %s"), *ErrorDetail);
			OnError.ExecuteIfBound(ErrorDetail);
			return;
		}
		// In history but no outputs yet — actively executing
		OnProgress.ExecuteIfBound(0.3f);
		return;
	}

	// Debug: always log what output nodes we found
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Outputs object has %d node entries"), (*OutputsObj)->Values.Num());
		for (auto& Pair : (*OutputsObj)->Values)
		{
			FString ValStr;
			TSharedRef<TJsonWriter<>> DbgWriter = TJsonWriterFactory<>::Create(&ValStr);
			FJsonSerializer::Serialize(Pair.Value->AsObject().ToSharedRef(), DbgWriter);
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Output node '%s': %s"), *Pair.Key, *ValStr.Left(2000));
		}
	}

	// ---- SAM3 Segmentation: collect ALL images from ALL output nodes ----
	if (bIsSAM3Request)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen SAM3: History found for prompt %s — collecting outputs..."), *CurrentPromptId);
		PendingSegmentation = MakeShareable(new FPendingSegFetch());

		for (auto& Pair : (*OutputsObj)->Values)
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen SAM3: Output node '%s'"), *Pair.Key);
			const TSharedPtr<FJsonObject>* NodeOutput;
			if (!Pair.Value->TryGetObject(NodeOutput)) continue;

			const TArray<TSharedPtr<FJsonValue>>* ImagesArray;
			if (!(*NodeOutput)->TryGetArrayField(TEXT("images"), ImagesArray)) continue;

			// Determine if this output node is the visualization (filename prefix "ViewGen_SAM3_vis")
			for (const auto& ImgVal : *ImagesArray)
			{
				TSharedPtr<FJsonObject> ImgInfo = ImgVal->AsObject();
				if (!ImgInfo.IsValid()) continue;

				FPendingSegFetch::FImageRef Ref;
				Ref.Filename = ImgInfo->GetStringField(TEXT("filename"));
				Ref.Subfolder = ImgInfo->GetStringField(TEXT("subfolder"));
				Ref.FolderType = ImgInfo->GetStringField(TEXT("type"));
				Ref.bIsVisualization = Ref.Filename.Contains(TEXT("SAM3_vis"));

				PendingSegmentation->ImagesToFetch.Add(MoveTemp(Ref));
			}
		}

		if (PendingSegmentation->ImagesToFetch.Num() == 0)
		{
			bRequestInProgress = false;
			bIsSAM3Request = false;
			CurrentPromptId.Empty();
			PendingSegmentation.Reset();
			OnError.ExecuteIfBound(TEXT("SAM3 produced no output images"));
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("ViewGen SAM3: Fetching %d output images"), PendingSegmentation->ImagesToFetch.Num());
		OnProgress.ExecuteIfBound(0.9f);
		CurrentPromptId.Empty(); // prevent duplicate polling

		FetchNextSegmentationImage();
		return;
	}

	// ---- Pre-scan: detect 3D model files in any output node ----
	// We do this before image/video handling so the flag is set even if the workflow also produces images.
	// ComfyUI 3D nodes can output files in several ways:
	//   1. Standard file object: {"filename": "xxx.glb", "subfolder": "", "type": "output"}
	//   2. Under a specific key like "model_3d" or "mesh" with file objects
	//   3. As a string path (text output)
	bHas3DModelResult = false;
	{
		// Helper lambda to check if a string looks like a 3D model filename
		auto IsMeshFilename = [](const FString& Name) -> bool
		{
			FString Lower = Name.ToLower();
			return Lower.EndsWith(TEXT(".glb")) || Lower.EndsWith(TEXT(".gltf"))
				|| Lower.EndsWith(TEXT(".obj")) || Lower.EndsWith(TEXT(".fbx"))
				|| Lower.EndsWith(TEXT(".stl")) || Lower.EndsWith(TEXT(".ply"));
		};

		for (const auto& Pair : (*OutputsObj)->Values)
		{
			const TSharedPtr<FJsonObject>* NodeOutput;
			if (!Pair.Value->TryGetObject(NodeOutput)) continue;

			for (const auto& Field : (*NodeOutput)->Values)
			{
				FString FieldKey = Field.Key;

				// Case 1: Array of file objects (standard ComfyUI output format)
				const TArray<TSharedPtr<FJsonValue>>* FileArray;
				if (Field.Value->TryGetArray(FileArray))
				{
					for (const auto& Item : *FileArray)
					{
						// Sub-case 1a: File object with filename field
						const TSharedPtr<FJsonObject>* FileObj;
						if (Item->TryGetObject(FileObj))
						{
							FString Filename;
							(*FileObj)->TryGetStringField(TEXT("filename"), Filename);
							if (!Filename.IsEmpty() && IsMeshFilename(Filename))
							{
								Last3DModelFilename = Filename;
								(*FileObj)->TryGetStringField(TEXT("subfolder"), Last3DModelSubfolder);
								(*FileObj)->TryGetStringField(TEXT("type"), Last3DModelFolderType);
								if (Last3DModelFolderType.IsEmpty()) Last3DModelFolderType = TEXT("output");
								bHas3DModelResult = true;

								UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' field '%s' produced 3D model file object: %s (subfolder=%s, type=%s)"),
									*Pair.Key, *FieldKey, *Filename, *Last3DModelSubfolder, *Last3DModelFolderType);
								break;
							}
						}

						// Sub-case 1b: Plain string path in the array (skip if it was an object)
						FString StringVal;
						if (Item->TryGetString(StringVal) && !StringVal.IsEmpty() && IsMeshFilename(StringVal))
						{
							// Could be a full path or just a filename
							Last3DModelFilename = FPaths::GetCleanFilename(StringVal);
							Last3DModelSubfolder = TEXT("");
							Last3DModelFolderType = TEXT("output");
							bHas3DModelResult = true;

							UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' field '%s' produced 3D model string path: %s"),
								*Pair.Key, *FieldKey, *StringVal);
							break;
						}
					}
					if (bHas3DModelResult) break;
					continue;
				}

				// Case 2: Direct string value (some nodes output file paths as plain strings)
				FString StrVal;
				if (Field.Value->TryGetString(StrVal) && !StrVal.IsEmpty() && IsMeshFilename(StrVal))
				{
					Last3DModelFilename = FPaths::GetCleanFilename(StrVal);
					Last3DModelSubfolder = TEXT("");
					Last3DModelFolderType = TEXT("output");
					bHas3DModelResult = true;

					UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' field '%s' has 3D model string: %s"),
						*Pair.Key, *FieldKey, *StrVal);
					break;
				}

				// Case 3: Single file object (not wrapped in an array)
				const TSharedPtr<FJsonObject>* SingleObj;
				if (Field.Value->TryGetObject(SingleObj))
				{
					FString Filename;
					(*SingleObj)->TryGetStringField(TEXT("filename"), Filename);
					if (!Filename.IsEmpty() && IsMeshFilename(Filename))
					{
						Last3DModelFilename = Filename;
						(*SingleObj)->TryGetStringField(TEXT("subfolder"), Last3DModelSubfolder);
						(*SingleObj)->TryGetStringField(TEXT("type"), Last3DModelFolderType);
						if (Last3DModelFolderType.IsEmpty()) Last3DModelFolderType = TEXT("output");
						bHas3DModelResult = true;

						UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' field '%s' has 3D model object: %s (subfolder=%s, type=%s)"),
							*Pair.Key, *FieldKey, *Filename, *Last3DModelSubfolder, *Last3DModelFolderType);
						break;
					}
				}
			}
			if (bHas3DModelResult) break;
		}

		// Fallback: check text outputs for mesh file paths or references
		if (!bHas3DModelResult)
		{
			for (const auto& Pair : (*OutputsObj)->Values)
			{
				const TSharedPtr<FJsonObject>* NodeOutput;
				if (!Pair.Value->TryGetObject(NodeOutput)) continue;

				const TArray<TSharedPtr<FJsonValue>>* TextArray;
				if ((*NodeOutput)->TryGetArrayField(TEXT("text"), TextArray) && TextArray->Num() > 0)
				{
					FString TextResult;
					if (!(*TextArray)[0]->TryGetString(TextResult) || TextResult.IsEmpty()) continue;

					// Case 1: Direct mesh filename or full path ending in a 3D extension
					if (IsMeshFilename(TextResult))
					{
						Last3DModelFilename = FPaths::GetCleanFilename(TextResult);
						Last3DModelSubfolder = TEXT("");
						// If it's a full path containing "/temp/", try temp folder first
						if (TextResult.Contains(TEXT("/temp/")) || TextResult.Contains(TEXT("\\temp\\")))
						{
							Last3DModelFolderType = TEXT("temp");
						}
						else
						{
							Last3DModelFolderType = TEXT("output");
						}
						bHas3DModelResult = true;
						UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' produced 3D model path in text: %s (type=%s)"),
							*Pair.Key, *TextResult, *Last3DModelFolderType);
						break;
					}

					// Case 2: File3D(<stream>, format='glb') — Tripo and similar nodes
					// This indicates the node produced a GLB in memory but didn't save it with a detectable filename.
					// Check if the text matches "File3D(...format='XXX')" and extract format info.
					if (TextResult.StartsWith(TEXT("File3D(")) && TextResult.Contains(TEXT("format=")))
					{
						UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' produced File3D stream: %s — "
							"looking for downloadable model file in other outputs"), *Pair.Key, *TextResult);
						// We can't download a stream directly, but the source node may have saved
						// the file to temp. Continue searching other outputs.
					}
				}
			}
		}
	}

	// If ONLY a 3D model was found and no image/video, complete immediately
	if (bHas3DModelResult)
	{
		// Still continue to check for image/video outputs below — if we find one,
		// the image flow runs normally and the panel can also handle the 3D model.
		// But if nothing else is found, the 3D model detection is the primary result.
	}

	// Helper to check if filename is a 3D model (reuse same logic as pre-scan)
	auto IsMeshFile = [](const FString& Name) -> bool
	{
		FString Lower = Name.ToLower();
		return Lower.EndsWith(TEXT(".glb")) || Lower.EndsWith(TEXT(".gltf"))
			|| Lower.EndsWith(TEXT(".obj")) || Lower.EndsWith(TEXT(".fbx"))
			|| Lower.EndsWith(TEXT(".stl")) || Lower.EndsWith(TEXT(".ply"));
	};

	auto IsVideoFile = [](const FString& Name) -> bool
	{
		FString Lower = Name.ToLower();
		return Lower.EndsWith(TEXT(".mp4")) || Lower.EndsWith(TEXT(".mov"))
			|| Lower.EndsWith(TEXT(".avi")) || Lower.EndsWith(TEXT(".webm"))
			|| Lower.EndsWith(TEXT(".mkv")) || Lower.EndsWith(TEXT(".gif"));
	};

	// ---- Normal (non-SAM3): find the best image/video output ----
	// When a UE Image Upres node is present the workflow contains multiple
	// SaveImage nodes (e.g. an Image Bridge pre-upscale AND the dedicated
	// __UE_Upres_Export__ post-upscale).  TMap iteration order is hash-based
	// so the first hit may be the pre-upscale output.  We scan ALL outputs
	// and prefer the upres image when available.
	struct FImageCandidate
	{
		FString Filename;
		FString Subfolder;
		FString FolderType;
	};
	FImageCandidate FirstImage;
	FImageCandidate UpresImage;
	bool bFoundFirstImage = false;
	bool bFoundUpresImage = false;

	// Also track video/gif (first-found is fine for these)
	struct FVideoCandidate { FString Filename; FString Subfolder; FString FolderType; bool bIsGif = false; };
	FVideoCandidate FirstVideo;
	bool bFoundVideo = false;

	for (auto& Pair : (*OutputsObj)->Values)
	{
		const TSharedPtr<FJsonObject>* NodeOutput;
		if (!Pair.Value->TryGetObject(NodeOutput))
		{
			continue;
		}

		// Check for image outputs (SaveImage)
		const TArray<TSharedPtr<FJsonValue>>* ImagesArray;
		if ((*NodeOutput)->TryGetArrayField(TEXT("images"), ImagesArray) && ImagesArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> ImageInfo = (*ImagesArray)[0]->AsObject();
			if (ImageInfo.IsValid())
			{
				FString Filename = ImageInfo->GetStringField(TEXT("filename"));
				FString Subfolder = ImageInfo->GetStringField(TEXT("subfolder"));
				FString FolderType = ImageInfo->GetStringField(TEXT("type"));

				// Skip 3D model files that ended up under the "images" key —
				// some nodes (e.g. Tripo) output mesh files this way
				if (IsMeshFile(Filename))
				{
					UE_LOG(LogTemp, Log, TEXT("ViewGen: Skipping 3D model file '%s' found under 'images' key (node %s)"), *Filename, *Pair.Key);
					continue;
				}

				// Redirect video files that ended up under the "images" key
				// (some ComfyUI nodes output .mp4/.webm here instead of "videos")
				if (IsVideoFile(Filename) && !bFoundVideo)
				{
					FirstVideo.Filename = Filename;
					FirstVideo.Subfolder = Subfolder;
					FirstVideo.FolderType = FolderType;
					FirstVideo.bIsGif = Filename.ToLower().EndsWith(TEXT(".gif"));
					bFoundVideo = true;
					UE_LOG(LogTemp, Log, TEXT("ViewGen: Found video file '%s' under 'images' key (node %s) — treating as video"), *Filename, *Pair.Key);
					continue;
				}

				// Check if this is the dedicated UE Image Upres output
				if (Filename.StartsWith(TEXT("__UE_Upres_Export__")))
				{
					UpresImage = { Filename, Subfolder, FolderType };
					bFoundUpresImage = true;
					UE_LOG(LogTemp, Log, TEXT("ViewGen: Found upres output image: %s (node %s)"), *Filename, *Pair.Key);
				}
				else if (!bFoundFirstImage)
				{
					FirstImage = { Filename, Subfolder, FolderType };
					bFoundFirstImage = true;
					UE_LOG(LogTemp, Log, TEXT("ViewGen: Found image output: %s (node %s)"), *Filename, *Pair.Key);
				}
			}
		}

		// Check for video outputs (SaveVideo) — first-found
		if (!bFoundVideo)
		{
			const TArray<TSharedPtr<FJsonValue>>* VideosArray;
			if ((*NodeOutput)->TryGetArrayField(TEXT("videos"), VideosArray) && VideosArray->Num() > 0)
			{
				TSharedPtr<FJsonObject> VideoInfo = (*VideosArray)[0]->AsObject();
				if (VideoInfo.IsValid())
				{
					FirstVideo.Filename = VideoInfo->GetStringField(TEXT("filename"));
					FirstVideo.Subfolder = VideoInfo->GetStringField(TEXT("subfolder"));
					FirstVideo.FolderType = VideoInfo->GetStringField(TEXT("type"));
					FirstVideo.bIsGif = false;
					bFoundVideo = true;
				}
			}

			// Check for "gifs" key as well (some video nodes use this)
			if (!bFoundVideo)
			{
				const TArray<TSharedPtr<FJsonValue>>* GifsArray;
				if ((*NodeOutput)->TryGetArrayField(TEXT("gifs"), GifsArray) && GifsArray->Num() > 0)
				{
					TSharedPtr<FJsonObject> GifInfo = (*GifsArray)[0]->AsObject();
					if (GifInfo.IsValid())
					{
						FirstVideo.Filename = GifInfo->GetStringField(TEXT("filename"));
						FirstVideo.Subfolder = GifInfo->GetStringField(TEXT("subfolder"));
						FirstVideo.FolderType = GifInfo->GetStringField(TEXT("type"));
						FirstVideo.bIsGif = true;
						bFoundVideo = true;
					}
				}
			}
		}
	}

	// Prefer the upres image if available, otherwise use the first-found image
	if (bFoundUpresImage || bFoundFirstImage)
	{
		const FImageCandidate& Chosen = bFoundUpresImage ? UpresImage : FirstImage;

		LastResultFilename = Chosen.Filename;
		LastResultSubfolder = Chosen.Subfolder;
		LastResultFolderType = Chosen.FolderType;

		if (bFoundUpresImage)
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Generation complete, fetching UPRES image: %s"), *Chosen.Filename);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Generation complete, fetching image: %s"), *Chosen.Filename);
		}
		OnProgress.ExecuteIfBound(0.9f);

		// Clear prompt ID before fetching to prevent the poll timer from
		// seeing the same completed result and triggering a duplicate fetch
		CurrentPromptId.Empty();

		FetchResultImage(Chosen.Filename, Chosen.Subfolder, Chosen.FolderType);
		return;
	}

	if (bFoundVideo)
	{
		LastResultFilename = FirstVideo.Filename;
		LastResultSubfolder = FirstVideo.Subfolder;
		LastResultFolderType = FirstVideo.FolderType;

		UE_LOG(LogTemp, Log, TEXT("ViewGen: Video generation complete%s: %s"),
			FirstVideo.bIsGif ? TEXT(" (gif)") : TEXT(""), *FirstVideo.Filename);

		bRequestInProgress = false;
		CurrentPromptId.Empty();
		OnComplete.ExecuteIfBound(true, nullptr);
		return;
	}

	// If we detected a 3D model in the pre-scan, complete now — the panel will handle the import.
	// Do this BEFORE the text/catch-all scan so the 3D model flag is preserved properly.
	if (bHas3DModelResult)
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: 3D model detected as sole workflow output, completing"));
		bRequestInProgress = false;
		CurrentPromptId.Empty();
		OnComplete.ExecuteIfBound(true, nullptr);
		return;
	}

	// No image/video/3D outputs found — check for text/string outputs (e.g. Meshy task results)
	// or other non-visual node outputs that still constitute a successful completion.
	for (auto& Pair : (*OutputsObj)->Values)
	{
		const TSharedPtr<FJsonObject>* NodeOutput;
		if (!Pair.Value->TryGetObject(NodeOutput)) continue;

		// Check for text outputs (some nodes return {"text": ["value"]})
		const TArray<TSharedPtr<FJsonValue>>* TextArray;
		if ((*NodeOutput)->TryGetArrayField(TEXT("text"), TextArray) && TextArray->Num() > 0)
		{
			FString TextResult;
			(*TextArray)[0]->TryGetString(TextResult);
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' produced text output: %s"), *Pair.Key, *TextResult.Left(500));

			bRequestInProgress = false;
			CurrentPromptId.Empty();
			OnComplete.ExecuteIfBound(true, nullptr);
			return;
		}

		// Check for any non-empty output at all (catch-all for V3 API nodes like Meshy)
		if ((*NodeOutput)->Values.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Node '%s' produced non-visual output (%d fields)"), *Pair.Key, (*NodeOutput)->Values.Num());

			bRequestInProgress = false;
			CurrentPromptId.Empty();
			OnComplete.ExecuteIfBound(true, nullptr);
			return;
		}
	}

	// No outputs found — if we had an error, report it now
	if (bHasError)
	{
		bRequestInProgress = false;
		bIsSAM3Request = false;
		SAM3SourceTexture = nullptr;
		PendingSegmentation.Reset();
		CurrentPromptId.Empty();
		UE_LOG(LogTemp, Error, TEXT("ViewGen: %s"), *ErrorDetail);
		OnError.ExecuteIfBound(ErrorDetail);
		return;
	}

	// Has outputs object but no images/videos found — check if status is completed
	if (StatusObj)
	{
		bool bCompleted = (*StatusObj)->GetBoolField(TEXT("completed"));
		if (bCompleted)
		{
			bRequestInProgress = false;
			bIsSAM3Request = false;
			SAM3SourceTexture = nullptr;
			PendingSegmentation.Reset();
			CurrentPromptId.Empty();

			// If the workflow completed without errors but produced no visual outputs,
			// treat it as a success. This handles non-visual nodes like MeshyImageToModelNode
			// which do their work (API calls) but don't produce images/videos.
			if (!bHasError)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: Workflow completed with no visual outputs — signaling success"));
				OnComplete.ExecuteIfBound(true, nullptr);
			}
			else
			{
				OnError.ExecuteIfBound(TEXT("Generation completed but no output found"));
			}
		}
	}
}

// ============================================================================
// Image Fetching
// ============================================================================

void FGenAIHttpClient::FetchResultImage(const FString& Filename, const FString& Subfolder, const FString& FolderType)
{
	// Guard against duplicate fetches (e.g. from overlapping poll responses)
	if (CurrentRequest.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: FetchResultImage skipped — a fetch is already in flight"));
		return;
	}

	const UGenAISettings* Settings = UGenAISettings::Get();

	FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
		*Settings->APIEndpointURL, *Filename, *Subfolder, *FolderType);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ViewURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(30.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FGenAIHttpClient::OnImageFetchComplete);

	CurrentRequest = Request;
	Request->ProcessRequest();
}

void FGenAIHttpClient::OnImageFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	CurrentRequest.Reset();
	bRequestInProgress = false;
	CurrentPromptId.Empty();
	UE_LOG(LogTemp, Log, TEXT("ViewGen: OnImageFetchComplete (connected=%s, bRequestInProgress now=false)"),
		bConnectedSuccessfully ? TEXT("true") : TEXT("false"));

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		OnError.ExecuteIfBound(TEXT("Failed to fetch generated image from ComfyUI"));
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		FString ErrorMsg = FString::Printf(TEXT("Image fetch returned HTTP %d"),
			Response->GetResponseCode());
		OnError.ExecuteIfBound(ErrorMsg);
		return;
	}

	const TArray<uint8>& ImageData = Response->GetContent();
	UTexture2D* ResultTexture = DecodeImageToTexture(ImageData);

	if (!ResultTexture)
	{
		OnError.ExecuteIfBound(TEXT("Failed to decode fetched image data"));
		return;
	}

	OnComplete.ExecuteIfBound(true, ResultTexture);
}

UTexture2D* FGenAIHttpClient::DecodeImageToTexture(const TArray<uint8>& ImageData)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	// Try PNG first, then JPEG
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
	{
		ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen: Could not decode image (tried PNG and JPEG)"));
			return nullptr;
		}
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		return nullptr;
	}

	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	Texture->UpdateResource();
	Texture->AddToRoot(); // Prevent GC — callers must RemoveFromRoot when done

	return Texture;
}

// ============================================================================
// SAM3 Segmentation
// ============================================================================

void FGenAIHttpClient::SendSAM3SegmentationRequest(UTexture2D* SourceTexture, const FString& TextPrompt)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A request is already in progress"));
		return;
	}

	if (!SourceTexture)
	{
		OnError.ExecuteIfBound(TEXT("No source texture for segmentation"));
		return;
	}

	bRequestInProgress = true;
	bIsSAM3Request = true;
	SAM3SourceTexture = SourceTexture;

	// Encode the source texture to base64 PNG
	FString Base64PNG;
	{
		// Read pixels from texture
		int32 W = SourceTexture->GetSizeX();
		int32 H = SourceTexture->GetSizeY();
		FTexturePlatformData* PlatformData = SourceTexture->GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() == 0)
		{
			bRequestInProgress = false;
			bIsSAM3Request = false;
			OnError.ExecuteIfBound(TEXT("Could not read source texture data"));
			return;
		}

		const void* RawData = PlatformData->Mips[0].BulkData.LockReadOnly();
		TArray<FColor> Pixels;
		Pixels.SetNum(W * H);
		FMemory::Memcpy(Pixels.GetData(), RawData, W * H * sizeof(FColor));
		PlatformData->Mips[0].BulkData.Unlock();

		// Encode to PNG (BGRA → the LoadImage node + pil_image.convert("RGB") handles channel conversion)
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		PngWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8);
		const TArray64<uint8>& PngData = PngWrapper->GetCompressed();
		Base64PNG = FBase64::Encode(PngData.GetData(), PngData.Num());
	}

	// Upload to ComfyUI
	FString UniqueTag = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());
	FString UploadName = FString::Printf(TEXT("ue_gen_sam3_source_%s.png"), *UniqueTag);
	FString ServerFilename;
	if (!UploadImageToComfyUI(Base64PNG, UploadName, ServerFilename))
	{
		bRequestInProgress = false;
		bIsSAM3Request = false;
		OnError.ExecuteIfBound(TEXT("Failed to upload source image for SAM3 segmentation"));
		return;
	}

	// Build and submit SAM3 workflow
	TSharedPtr<FJsonObject> Workflow = BuildSAM3Workflow(ServerFilename);
	SubmitPrompt(Workflow);
}

TSharedPtr<FJsonObject> FGenAIHttpClient::BuildSAM3Workflow(const FString& ImageFilename) const
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	TSharedPtr<FJsonObject> Workflow = MakeShareable(new FJsonObject);

	// Node layout (comfyui_sam3 by wouterverweirder):
	//
	// LoadImage ──► SAM3Segmentation ──► segmented_image (idx 0) ──► SaveImage (vis)
	//                                ├── masks          (idx 1) ──► MaskToImage ──► SaveImage (masks)
	//                                ├── mask_combined  (idx 2)
	//                                └── segs           (idx 3)
	//
	// SAM3Segmentation inputs:
	//   image (IMAGE), prompt (STRING), threshold (FLOAT 0-1),
	//   min_width_pixels (INT), min_height_pixels (INT),
	//   use_video_model (BOOLEAN), unload_after_run (BOOLEAN)
	//
	// SAM3Segmentation outputs:
	//   0: segmented_image (IMAGE) — visualization with masks + boxes + scores
	//   1: masks (MASK)            — individual per-object binary masks (batch)
	//   2: mask_combined (MASK)    — all masks merged into one
	//   3: segs (SEGS)             — structured segment data

	int32 NextNodeId = 1;
	FString LoadImageId = FString::FromInt(NextNodeId++);
	FString SAM3Id = FString::FromInt(NextNodeId++);
	FString MaskToImageId = FString::FromInt(NextNodeId++);
	FString SaveMasksId = FString::FromInt(NextNodeId++);
	FString SaveVisId = FString::FromInt(NextNodeId++);

	// Node 1: LoadImage (the source image to segment)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetStringField(TEXT("image"), ImageFilename);
		Workflow->SetObjectField(LoadImageId, MakeNode(TEXT("LoadImage"), Inputs));
	}

	// Node 2: SAM3Segmentation (text-prompted segmentation — loads model internally)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("image"), MakeLink(LoadImageId, 0));
		Inputs->SetStringField(TEXT("prompt"), Settings->SAM3TextPrompt.IsEmpty() ? TEXT("objects") : Settings->SAM3TextPrompt);
		Inputs->SetNumberField(TEXT("threshold"), Settings->SAM3ConfidenceThreshold);
		Inputs->SetNumberField(TEXT("min_width_pixels"), 0);
		Inputs->SetNumberField(TEXT("min_height_pixels"), 0);
		Inputs->SetBoolField(TEXT("use_video_model"), false);
		Inputs->SetBoolField(TEXT("unload_after_run"), false);
		Workflow->SetObjectField(SAM3Id, MakeNode(TEXT("SAM3Segmentation"), Inputs));
	}

	// Node 3: MaskToImage (convert per-object MASK batch to IMAGE batch for saving)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("mask"), MakeLink(SAM3Id, 1)); // masks at output index 1
		Workflow->SetObjectField(MaskToImageId, MakeNode(TEXT("MaskToImage"), Inputs));
	}

	// Node 4: SaveImage (save individual mask images — one per detected object)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(MaskToImageId, 0));
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_SAM3_mask"));
		Workflow->SetObjectField(SaveMasksId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	// Node 5: SaveImage (save visualization — annotated image with boxes + scores)
	{
		TSharedPtr<FJsonObject> Inputs = MakeShareable(new FJsonObject);
		Inputs->SetField(TEXT("images"), MakeLink(SAM3Id, 0)); // segmented_image at output index 0
		Inputs->SetStringField(TEXT("filename_prefix"), TEXT("ViewGen_SAM3_vis"));
		Workflow->SetObjectField(SaveVisId, MakeNode(TEXT("SaveImage"), Inputs));
	}

	return Workflow;
}

void FGenAIHttpClient::FetchNextSegmentationImage()
{
	if (!PendingSegmentation.IsValid())
	{
		return;
	}

	if (PendingSegmentation->CurrentFetchIndex >= PendingSegmentation->ImagesToFetch.Num())
	{
		// All images fetched — deliver results
		bRequestInProgress = false;
		bIsSAM3Request = false;
		CurrentPromptId.Empty();

		OnSegmentationComplete.ExecuteIfBound(
			PendingSegmentation->VisualizationTexture,
			PendingSegmentation->FetchedMaskTextures,
			SAM3SourceTexture);

		SAM3SourceTexture = nullptr;
		PendingSegmentation.Reset();
		return;
	}

	const FPendingSegFetch::FImageRef& Ref = PendingSegmentation->ImagesToFetch[PendingSegmentation->CurrentFetchIndex];

	const UGenAISettings* Settings = UGenAISettings::Get();
	FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
		*Settings->APIEndpointURL, *Ref.Filename, *Ref.Subfolder, *Ref.FolderType);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ViewURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(30.0f);

	Request->OnProcessRequestComplete().BindRaw(this, &FGenAIHttpClient::OnSegmentationImageFetchComplete);
	CurrentRequest = Request;
	Request->ProcessRequest();
}

void FGenAIHttpClient::OnSegmentationImageFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	CurrentRequest.Reset();

	if (!PendingSegmentation.IsValid())
	{
		return;
	}

	int32 Idx = PendingSegmentation->CurrentFetchIndex;
	bool bIsVis = PendingSegmentation->ImagesToFetch[Idx].bIsVisualization;

	if (bConnectedSuccessfully && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		UTexture2D* Tex = DecodeImageToTexture(Response->GetContent());
		if (Tex)
		{
			// Tex already rooted by DecodeImageToTexture
			if (bIsVis)
			{
				PendingSegmentation->VisualizationTexture = Tex;
			}
			else
			{
				PendingSegmentation->FetchedMaskTextures.Add(Tex);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ViewGen SAM3: Failed to decode image %d"), Idx);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen SAM3: Failed to fetch image %d (HTTP error)"), Idx);
	}

	PendingSegmentation->CurrentFetchIndex++;
	FetchNextSegmentationImage();
}

// ============================================================================
// Open Output Folder
// ============================================================================

void FGenAIHttpClient::OpenOutputFolder()
{
	const UGenAISettings* Settings = UGenAISettings::Get();

	if (LastResultFilename.IsEmpty())
	{
		// No result yet - open ComfyUI web UI instead
		FPlatformProcess::LaunchURL(*Settings->APIEndpointURL, nullptr, nullptr);
		return;
	}

	// Query ComfyUI /api/settings to discover the output directory
	FString SettingsURL = FString::Printf(TEXT("%s/api/settings"), *Settings->APIEndpointURL);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(SettingsURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(5.0f);

	Request->OnProcessRequestComplete().BindLambda(
		[this, Settings](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
	{
		FString OutputDir;

		// Try to extract output directory from ComfyUI settings
		if (bConnected && Resp.IsValid() && Resp->GetResponseCode() == 200)
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				// ComfyUI settings may contain "Comfy.General.output_directory"
				Root->TryGetStringField(TEXT("Comfy.General.output_directory"), OutputDir);

				if (OutputDir.IsEmpty())
				{
					// Also check for nested format
					const TSharedPtr<FJsonObject>* ComfyObj;
					if (Root->TryGetObjectField(TEXT("Comfy"), ComfyObj))
					{
						const TSharedPtr<FJsonObject>* GeneralObj;
						if ((*ComfyObj)->TryGetObjectField(TEXT("General"), GeneralObj))
						{
							(*GeneralObj)->TryGetStringField(TEXT("output_directory"), OutputDir);
						}
					}
				}
			}
		}

		if (!OutputDir.IsEmpty())
		{
			// Append subfolder if present
			if (!LastResultSubfolder.IsEmpty())
			{
				OutputDir = FPaths::Combine(OutputDir, LastResultSubfolder);
			}

			FString FullPath = FPaths::Combine(OutputDir, LastResultFilename);
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Opening output folder: %s"), *OutputDir);

			// Open explorer and select the file
			FPlatformProcess::ExploreFolder(*FullPath);
		}
		else
		{
			// Fallback: open the result image in the browser via ComfyUI /view
			FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
				*Settings->APIEndpointURL,
				*LastResultFilename,
				*LastResultSubfolder,
				*LastResultFolderType);

			UE_LOG(LogTemp, Log, TEXT("ViewGen: Could not find output dir, opening in browser: %s"), *ViewURL);
			FPlatformProcess::LaunchURL(*ViewURL, nullptr, nullptr);
		}
	});

	Request->ProcessRequest();
}

// ============================================================================
// Public Wrappers for Graph Editor Integration
// ============================================================================

void FGenAIHttpClient::SubmitWorkflowDirect(TSharedPtr<FJsonObject> Workflow)
{
	if (bRequestInProgress)
	{
		OnError.ExecuteIfBound(TEXT("A generation request is already in progress"));
		return;
	}

	bRequestInProgress = true;
	SubmitPrompt(Workflow);
}

bool FGenAIHttpClient::UploadImage(const FString& Base64PNG, const FString& DesiredFilename, FString& OutServerFilename)
{
	return UploadImageToComfyUI(Base64PNG, DesiredFilename, OutServerFilename);
}

bool FGenAIHttpClient::UploadRawFile(const TArray<uint8>& FileBytes, const FString& DesiredFilename, const FString& ContentType, FString& OutServerFilename)
{
	const UGenAISettings* Settings = UGenAISettings::Get();
	FString UploadURL = Settings->APIEndpointURL / TEXT("upload/image");

	// Build multipart/form-data request
	FString Boundary = FString::Printf(TEXT("----UEGenBoundary%s"), *FGuid::NewGuid().ToString());

	TArray<uint8> Payload;
	FString Header = FString::Printf(
		TEXT("--%s\r\nContent-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n"),
		*Boundary, *DesiredFilename, *ContentType);

	FString OverwriteField = FString::Printf(
		TEXT("\r\n--%s\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--%s--\r\n"),
		*Boundary, *Boundary);

	// Assemble payload: header bytes + file bytes + overwrite field bytes
	FTCHARToUTF8 HeaderUTF8(*Header);
	Payload.Append((const uint8*)HeaderUTF8.Get(), HeaderUTF8.Length());
	Payload.Append(FileBytes);
	FTCHARToUTF8 OverwriteUTF8(*OverwriteField);
	Payload.Append((const uint8*)OverwriteUTF8.Get(), OverwriteUTF8.Length());

	// Send synchronously
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(UploadURL);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	Request->SetContent(Payload);
	Request->SetTimeout(120.0f); // longer timeout for large video files

	bool bCompleted = false;
	bool bSuccess = false;
	FString ResponseBody;

	Request->OnProcessRequestComplete().BindLambda(
		[&bCompleted, &bSuccess, &ResponseBody](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			bCompleted = true;
			if (bConnected && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				bSuccess = true;
				ResponseBody = Resp->GetContentAsString();
			}
			else if (Resp.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen UploadRawFile: HTTP %d: %s"),
					Resp->GetResponseCode(), *Resp->GetContentAsString());
			}
		});

	Request->ProcessRequest();

	// Block until response
	double StartTime = FPlatformTime::Seconds();
	while (!bCompleted && (FPlatformTime::Seconds() - StartTime) < 120.0)
	{
		FPlatformProcess::Sleep(0.05f);
		FHttpModule::Get().GetHttpManager().Tick(0.05f);
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to upload file %s"), *DesiredFilename);
		return false;
	}

	// Parse response — ComfyUI returns {"name":"filename.ext","subfolder":"","type":"input"}
	TSharedPtr<FJsonObject> ResponseJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (FJsonSerializer::Deserialize(Reader, ResponseJson) && ResponseJson.IsValid())
	{
		OutServerFilename = ResponseJson->GetStringField(TEXT("name"));
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("ViewGen: Could not parse upload response for %s"), *DesiredFilename);
	return false;
}

void FGenAIHttpClient::FetchImageThumbnail(const FString& ImageFilename, TFunction<void(UTexture2D*)> OnThumbnailReady)
{
	if (ImageFilename.IsEmpty())
	{
		if (OnThumbnailReady) OnThumbnailReady(nullptr);
		return;
	}

	const UGenAISettings* Settings = UGenAISettings::Get();

	// ComfyUI uploaded images live in the "input" folder
	FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=&type=input"),
		*Settings->APIEndpointURL, *ImageFilename);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ViewURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(15.0f);

	// Use a shared callback so the TFunction is safely captured
	Request->OnProcessRequestComplete().BindLambda(
		[this, OnThumbnailReady, ImageFilename](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to fetch thumbnail for '%s' (HTTP %d)"),
					*ImageFilename, Resp.IsValid() ? Resp->GetResponseCode() : 0);
				if (OnThumbnailReady) OnThumbnailReady(nullptr);
				return;
			}

			UTexture2D* Tex = DecodeImageToTexture(Resp->GetContent());
			// Tex is already rooted by DecodeImageToTexture
			if (OnThumbnailReady) OnThumbnailReady(Tex);
		});

	Request->ProcessRequest();
}

void FGenAIHttpClient::DownloadComfyUIImageAsBase64(const FString& ImageFilename, TFunction<void(const FString&)> OnBase64Ready)
{
	if (ImageFilename.IsEmpty())
	{
		if (OnBase64Ready) OnBase64Ready(FString());
		return;
	}

	const UGenAISettings* Settings = UGenAISettings::Get();

	FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=&type=input"),
		*Settings->APIEndpointURL, *ImageFilename);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ViewURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(30.0f);

	Request->OnProcessRequestComplete().BindLambda(
		[OnBase64Ready, ImageFilename](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to download image '%s' from ComfyUI (HTTP %d)"),
					*ImageFilename, Resp.IsValid() ? Resp->GetResponseCode() : 0);
				if (OnBase64Ready) OnBase64Ready(FString());
				return;
			}

			const TArray<uint8>& RawBytes = Resp->GetContent();
			FString Base64 = FBase64::Encode(RawBytes);
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloaded '%s' from ComfyUI (%d bytes, %d chars base64)"),
				*ImageFilename, RawBytes.Num(), Base64.Len());
			if (OnBase64Ready) OnBase64Ready(Base64);
		});

	Request->ProcessRequest();
}

// ============================================================================
// WebSocket — real-time node execution tracking
// ============================================================================

void FGenAIHttpClient::ConnectWebSocket()
{
	// Don't reconnect if already connected
	if (bWebSocketConnected && WebSocket.IsValid() && WebSocket->IsConnected())
	{
		return;
	}

	DisconnectWebSocket();

	const UGenAISettings* Settings = UGenAISettings::Get();
	FString ServerURL = Settings->APIEndpointURL;

	// Convert http(s)://host:port to ws(s)://host:port/ws?clientId=xxx
	FString WsURL = ServerURL;
	if (WsURL.StartsWith(TEXT("https://")))
	{
		WsURL = TEXT("wss://") + WsURL.Mid(8);
	}
	else if (WsURL.StartsWith(TEXT("http://")))
	{
		WsURL = TEXT("ws://") + WsURL.Mid(7);
	}
	else
	{
		WsURL = TEXT("ws://") + WsURL;
	}

	// Remove trailing slash
	if (WsURL.EndsWith(TEXT("/")))
	{
		WsURL = WsURL.LeftChop(1);
	}

	WsURL += FString::Printf(TEXT("/ws?clientId=%s"), *ClientId);

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Connecting WebSocket to %s"), *WsURL);

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
	{
		FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	}

	WebSocket = FWebSocketsModule::Get().CreateWebSocket(WsURL, TEXT("ws"));
	if (!WebSocket.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to create WebSocket"));
		return;
	}

	WebSocket->OnConnected().AddLambda([this]()
	{
		bWebSocketConnected = true;
		UE_LOG(LogTemp, Log, TEXT("ViewGen: WebSocket connected"));
	});

	WebSocket->OnConnectionError().AddLambda([this](const FString& Error)
	{
		bWebSocketConnected = false;
		UE_LOG(LogTemp, Warning, TEXT("ViewGen: WebSocket connection error: %s"), *Error);
	});

	WebSocket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		bWebSocketConnected = false;
		UE_LOG(LogTemp, Log, TEXT("ViewGen: WebSocket closed (code=%d, reason=%s)"), StatusCode, *Reason);
	});

	WebSocket->OnMessage().AddLambda([this](const FString& Message)
	{
		OnWebSocketMessage(Message);
	});

	WebSocket->Connect();
}

void FGenAIHttpClient::DisconnectWebSocket()
{
	if (WebSocket.IsValid())
	{
		if (WebSocket->IsConnected())
		{
			WebSocket->Close();
		}
		WebSocket.Reset();
	}
	bWebSocketConnected = false;
}

void FGenAIHttpClient::OnWebSocketMessage(const FString& Message)
{
	// ComfyUI WebSocket messages are JSON:
	// {"type": "executing", "data": {"node": "node_id", "prompt_id": "xxx"}}
	// {"type": "progress", "data": {"value": 5, "max": 20, "prompt_id": "xxx", "node": "node_id"}}
	// {"type": "executed", "data": {"node": "node_id", "prompt_id": "xxx"}}
	// {"type": "execution_cached", "data": {"nodes": ["id1", "id2"], "prompt_id": "xxx"}}

	TSharedPtr<FJsonObject> JsonMsg;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonMsg) || !JsonMsg.IsValid())
	{
		return;
	}

	FString MsgType;
	if (!JsonMsg->TryGetStringField(TEXT("type"), MsgType))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* DataObj;
	if (!JsonMsg->TryGetObjectField(TEXT("data"), DataObj))
	{
		return;
	}

	// Only process messages for our current prompt
	FString PromptId;
	(*DataObj)->TryGetStringField(TEXT("prompt_id"), PromptId);
	if (!PromptId.IsEmpty() && PromptId != CurrentPromptId)
	{
		return;
	}

	if (MsgType == TEXT("executing"))
	{
		FString NodeId;
		if ((*DataObj)->TryGetStringField(TEXT("node"), NodeId) && !NodeId.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("ViewGen: WS executing node '%s' (prompt=%s)"), *NodeId, *PromptId);
			OnNodeExecuting.ExecuteIfBound(NodeId);
		}
		else
		{
			// "node" is null → execution finished for this prompt
			UE_LOG(LogTemp, Log, TEXT("ViewGen: WS execution complete (node=null, prompt=%s)"), *PromptId);
			OnNodeExecuting.ExecuteIfBound(FString());
		}
	}
	else if (MsgType == TEXT("executed"))
	{
		// A node just finished — check if it produced a 3D model file we should capture.
		// This is critical for nodes that are NOT OUTPUT_NODEs in ComfyUI, as their
		// results won't appear in the /history API but DO appear in WebSocket "executed" messages.
		FString NodeId;
		(*DataObj)->TryGetStringField(TEXT("node"), NodeId);

		const TSharedPtr<FJsonObject>* OutputObj;
		if ((*DataObj)->TryGetObjectField(TEXT("output"), OutputObj) && !bHas3DModelResult)
		{
			// Scan the output for 3D model filenames
			auto IsMeshFilename = [](const FString& Name) -> bool
			{
				FString Lower = Name.ToLower();
				return Lower.EndsWith(TEXT(".glb")) || Lower.EndsWith(TEXT(".gltf"))
					|| Lower.EndsWith(TEXT(".obj")) || Lower.EndsWith(TEXT(".fbx"))
					|| Lower.EndsWith(TEXT(".stl")) || Lower.EndsWith(TEXT(".ply"));
			};

			for (const auto& Field : (*OutputObj)->Values)
			{
				// Check arrays of file objects or strings
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (Field.Value->TryGetArray(Arr))
				{
					for (const auto& Item : *Arr)
					{
						const TSharedPtr<FJsonObject>* FileObj;
						if (Item->TryGetObject(FileObj))
						{
							FString Filename;
							(*FileObj)->TryGetStringField(TEXT("filename"), Filename);
							if (!Filename.IsEmpty() && IsMeshFilename(Filename))
							{
								Last3DModelFilename = Filename;
								(*FileObj)->TryGetStringField(TEXT("subfolder"), Last3DModelSubfolder);
								(*FileObj)->TryGetStringField(TEXT("type"), Last3DModelFolderType);
								if (Last3DModelFolderType.IsEmpty()) Last3DModelFolderType = TEXT("output");
								bHas3DModelResult = true;
								UE_LOG(LogTemp, Log, TEXT("ViewGen: WS executed node '%s' produced 3D model: %s (from output object)"), *NodeId, *Filename);
								break;
							}
						}
						FString StrVal;
						if (Item->TryGetString(StrVal) && !StrVal.IsEmpty() && IsMeshFilename(StrVal))
						{
							Last3DModelFilename = FPaths::GetCleanFilename(StrVal);
							Last3DModelSubfolder = TEXT("");
							Last3DModelFolderType = TEXT("output");
							bHas3DModelResult = true;
							UE_LOG(LogTemp, Log, TEXT("ViewGen: WS executed node '%s' produced 3D model path: %s"), *NodeId, *StrVal);
							break;
						}
					}
				}

				// Check direct string value
				if (!bHas3DModelResult)
				{
					FString StrVal;
					if (Field.Value->TryGetString(StrVal) && !StrVal.IsEmpty() && IsMeshFilename(StrVal))
					{
						Last3DModelFilename = FPaths::GetCleanFilename(StrVal);
						Last3DModelSubfolder = TEXT("");
						Last3DModelFolderType = TEXT("output");
						bHas3DModelResult = true;
						UE_LOG(LogTemp, Log, TEXT("ViewGen: WS executed node '%s' field '%s' has 3D model: %s"), *NodeId, *Field.Key, *StrVal);
					}
				}

				if (bHas3DModelResult) break;
			}
		}
	}
}

void FGenAIHttpClient::DownloadComfyUIFile(const FString& Filename, const FString& Subfolder,
	const FString& FolderType, const FString& LocalSavePath,
	TFunction<void(const FString&)> OnDownloadComplete)
{
	if (Filename.IsEmpty() || LocalSavePath.IsEmpty())
	{
		if (OnDownloadComplete) OnDownloadComplete(FString());
		return;
	}

	const UGenAISettings* Settings = UGenAISettings::Get();

	// URL-encode parameters to handle spaces, special characters, and subdirectory paths
	auto UrlEncode = [](const FString& Str) -> FString
	{
		FString Encoded;
		for (int32 i = 0; i < Str.Len(); ++i)
		{
			TCHAR Ch = Str[i];
			if ((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9')
				|| Ch == '-' || Ch == '_' || Ch == '.' || Ch == '~')
			{
				Encoded += Ch;
			}
			else if (Ch == ' ')
			{
				Encoded += TEXT("%20");
			}
			else
			{
				Encoded += FString::Printf(TEXT("%%%02X"), (uint8)Ch);
			}
		}
		return Encoded;
	};

	FString ViewURL = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
		*Settings->APIEndpointURL, *UrlEncode(Filename), *UrlEncode(Subfolder), *UrlEncode(FolderType));

	UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloading file from: %s"), *ViewURL);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ViewURL);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(120.0f); // 3D models can be large

	Request->OnProcessRequestComplete().BindLambda(
		[OnDownloadComplete, Filename, LocalSavePath](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen: Failed to download file '%s' from ComfyUI (HTTP %d)"),
					*Filename, Resp.IsValid() ? Resp->GetResponseCode() : 0);
				if (OnDownloadComplete) OnDownloadComplete(FString());
				return;
			}

			const TArray<uint8>& RawBytes = Resp->GetContent();
			if (RawBytes.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("ViewGen: Downloaded file '%s' is empty"), *Filename);
				if (OnDownloadComplete) OnDownloadComplete(FString());
				return;
			}

			// Log content info for diagnostics
			FString ContentType = Resp->GetHeader(TEXT("Content-Type"));
			UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloaded '%s': %d bytes, Content-Type: %s"),
				*Filename, RawBytes.Num(), *ContentType);

			// Log first bytes to help diagnose format issues (e.g. GLB should start with 'glTF')
			if (RawBytes.Num() >= 4)
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X ('%c%c%c%c')"),
					RawBytes[0], RawBytes[1], RawBytes[2], RawBytes[3],
					(TCHAR)(RawBytes[0] >= 32 ? RawBytes[0] : '.'),
					(TCHAR)(RawBytes[1] >= 32 ? RawBytes[1] : '.'),
					(TCHAR)(RawBytes[2] >= 32 ? RawBytes[2] : '.'),
					(TCHAR)(RawBytes[3] >= 32 ? RawBytes[3] : '.'));
			}

			// Check if the response is actually an error page (HTML/JSON instead of binary)
			if (ContentType.Contains(TEXT("text")) || ContentType.Contains(TEXT("json")))
			{
				FString ResponseText = Resp->GetContentAsString();
				UE_LOG(LogTemp, Warning, TEXT("ViewGen: Downloaded file '%s' appears to be text, not binary: %s"),
					*Filename, *ResponseText.Left(500));
				if (OnDownloadComplete) OnDownloadComplete(FString());
				return;
			}

			// Ensure directory exists
			FString Dir = FPaths::GetPath(LocalSavePath);
			IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*Dir);

			// Save to disk
			if (FFileHelper::SaveArrayToFile(RawBytes, *LocalSavePath))
			{
				UE_LOG(LogTemp, Log, TEXT("ViewGen: Downloaded '%s' from ComfyUI (%d bytes) -> %s"),
					*Filename, RawBytes.Num(), *LocalSavePath);
				if (OnDownloadComplete) OnDownloadComplete(LocalSavePath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ViewGen: Failed to save downloaded file to %s"), *LocalSavePath);
				if (OnDownloadComplete) OnDownloadComplete(FString());
			}
		});

	Request->ProcessRequest();
}
