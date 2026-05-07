// Copyright ViewGen. All Rights Reserved.

#include "GenAISettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

UGenAISettings::UGenAISettings()
{
	EnsureDefaultModelPaths();
}

void UGenAISettings::EnsureDefaultModelPaths()
{
	auto SetDefault = [this](const FString& Key, const FString& Path)
	{
		if (!ModelPaths.Contains(Key))
		{
			ModelPaths.Add(Key, Path);
		}
	};

	SetDefault(TEXT("Checkpoint"),    TEXT("models/checkpoints/"));
	SetDefault(TEXT("LoRA"),          TEXT("models/loras/"));
	SetDefault(TEXT("VAE"),           TEXT("models/vae/"));
	SetDefault(TEXT("ControlNet"),    TEXT("models/controlnet/"));
	SetDefault(TEXT("CLIP"),          TEXT("models/clip/"));
	SetDefault(TEXT("CLIPVision"),    TEXT("models/clip_vision/"));
	SetDefault(TEXT("TextEncoder"),   TEXT("models/text_encoders/"));
	SetDefault(TEXT("UNET"),          TEXT("models/unet/ or models/diffusion_models/"));
	SetDefault(TEXT("Upscale"),       TEXT("models/upscale_models/"));
	SetDefault(TEXT("Embedding"),     TEXT("models/embeddings/"));
	SetDefault(TEXT("Hypernetwork"),  TEXT("models/hypernetworks/"));
}

FString UGenAISettings::GetModelPath(const FString& Key) const
{
	if (const FString* Found = ModelPaths.Find(Key))
	{
		return *Found;
	}
	return FString::Printf(TEXT("models/%s/"), *Key.ToLower());
}

UGenAISettings* UGenAISettings::Get()
{
	return GetMutableDefault<UGenAISettings>();
}

// ============================================================================
// Presets
// ============================================================================

FString UGenAISettings::GetPresetsDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("ViewGen") / TEXT("Presets");
}

TArray<FString> UGenAISettings::GetSavedPresetNames()
{
	TArray<FString> Names;
	const FString Dir = GetPresetsDirectory();

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);

	for (const FString& File : Files)
	{
		Names.Add(FPaths::GetBaseFilename(File));
	}
	Names.Sort();
	return Names;
}

bool UGenAISettings::SavePreset(const FString& PresetName)
{
	if (PresetName.IsEmpty()) return false;

	const UGenAISettings* S = Get();
	if (!S) return false;

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);

	// Connection (save timeout/poll but NOT url/api key — those are environment-specific)
	Root->SetNumberField(TEXT("TimeoutSeconds"), S->TimeoutSeconds);
	Root->SetNumberField(TEXT("ProgressPollInterval"), S->ProgressPollInterval);

	// Model
	Root->SetStringField(TEXT("CheckpointName"), S->CheckpointName);

	// Reference Adherence
	Root->SetNumberField(TEXT("ReferenceAdherence"), S->ReferenceAdherence);

	// Generation
	Root->SetNumberField(TEXT("GenerationMode"), static_cast<int32>(S->GenerationMode));
	Root->SetStringField(TEXT("DefaultPrompt"), S->DefaultPrompt);
	Root->SetStringField(TEXT("DefaultNegativePrompt"), S->DefaultNegativePrompt);
	Root->SetNumberField(TEXT("OutputWidth"), S->OutputWidth);
	Root->SetNumberField(TEXT("OutputHeight"), S->OutputHeight);
	Root->SetNumberField(TEXT("DenoisingStrength"), S->DenoisingStrength);
	Root->SetNumberField(TEXT("Steps"), S->Steps);
	Root->SetNumberField(TEXT("CFGScale"), S->CFGScale);
	Root->SetNumberField(TEXT("Seed"), static_cast<double>(S->Seed));
	Root->SetStringField(TEXT("SamplerName"), S->SamplerName);
	Root->SetStringField(TEXT("SchedulerName"), S->SchedulerName);
	Root->SetBoolField(TEXT("bAutoCameraPrompt"), S->bAutoCameraPrompt);

	// Depth / ControlNet
	Root->SetBoolField(TEXT("bEnableDepthControlNet"), S->bEnableDepthControlNet);
	Root->SetBoolField(TEXT("bUseFluxControlNet"), S->bUseFluxControlNet);
	Root->SetStringField(TEXT("FluxModelName"), S->FluxModelName);
	Root->SetStringField(TEXT("FluxWeightDtype"), S->FluxWeightDtype);
	Root->SetStringField(TEXT("FluxCLIPName1"), S->FluxCLIPName1);
	Root->SetStringField(TEXT("FluxCLIPName2"), S->FluxCLIPName2);
	Root->SetStringField(TEXT("FluxCLIPType"), S->FluxCLIPType);
	Root->SetStringField(TEXT("FluxVAEName"), S->FluxVAEName);
	Root->SetStringField(TEXT("ControlNetModel"), S->ControlNetModel);
	Root->SetNumberField(TEXT("ControlNetWeight"), S->ControlNetWeight);
	Root->SetNumberField(TEXT("MaxDepthDistance"), S->MaxDepthDistance);

	// Hi-Res Fix
	Root->SetBoolField(TEXT("bEnableHiResFix"), S->bEnableHiResFix);
	Root->SetNumberField(TEXT("HiResUpscaleFactor"), S->HiResUpscaleFactor);
	Root->SetNumberField(TEXT("HiResDenoise"), S->HiResDenoise);
	Root->SetNumberField(TEXT("HiResSteps"), S->HiResSteps);

	// Gemini
	Root->SetStringField(TEXT("GeminiModelName"), S->GeminiModelName);
	Root->SetStringField(TEXT("GeminiAspectRatio"), S->GeminiAspectRatio);
	Root->SetStringField(TEXT("GeminiResolution"), S->GeminiResolution);
	Root->SetStringField(TEXT("GeminiResponseModalities"), S->GeminiResponseModalities);
	Root->SetStringField(TEXT("GeminiThinkingLevel"), S->GeminiThinkingLevel);
	Root->SetNumberField(TEXT("GeminiSeed"), static_cast<double>(S->GeminiSeed));
	Root->SetStringField(TEXT("GeminiSystemPrompt"), S->GeminiSystemPrompt);

	// Kling
	Root->SetStringField(TEXT("KlingModelName"), S->KlingModelName);
	Root->SetStringField(TEXT("KlingAspectRatio"), S->KlingAspectRatio);
	Root->SetStringField(TEXT("KlingImageType"), S->KlingImageType);
	Root->SetNumberField(TEXT("KlingImageFidelity"), S->KlingImageFidelity);
	Root->SetNumberField(TEXT("KlingHumanFidelity"), S->KlingHumanFidelity);
	Root->SetNumberField(TEXT("KlingImageCount"), S->KlingImageCount);
	Root->SetNumberField(TEXT("KlingSeed"), static_cast<double>(S->KlingSeed));

	// Video (Image-to-Video)
	Root->SetNumberField(TEXT("VideoMode"), static_cast<int32>(S->VideoMode));
	Root->SetNumberField(TEXT("VideoMotionAdherence"), S->VideoMotionAdherence);
	Root->SetStringField(TEXT("VideoPrompt"), S->VideoPrompt);
	Root->SetStringField(TEXT("VideoNegativePrompt"), S->VideoNegativePrompt);
	Root->SetNumberField(TEXT("VideoDuration"), S->VideoDuration);
	Root->SetNumberField(TEXT("VideoFPS"), S->VideoFPS);
	Root->SetNumberField(TEXT("VideoCFG"), S->VideoCFG);
	Root->SetNumberField(TEXT("VideoSteps"), S->VideoSteps);
	Root->SetNumberField(TEXT("VideoSeed"), static_cast<double>(S->VideoSeed));
	Root->SetStringField(TEXT("KlingVideoModel"), S->KlingVideoModel);
	Root->SetStringField(TEXT("KlingVideoQuality"), S->KlingVideoQuality);
	Root->SetStringField(TEXT("WanModelName"), S->WanModelName);
	Root->SetStringField(TEXT("Veo3ModelName"), S->Veo3ModelName);
	Root->SetStringField(TEXT("Veo3AspectRatio"), S->Veo3AspectRatio);
	Root->SetBoolField(TEXT("bVeo3GenerateAudio"), S->bVeo3GenerateAudio);
	Root->SetStringField(TEXT("Veo3PersonGeneration"), S->Veo3PersonGeneration);

	// LoRAs
	TArray<TSharedPtr<FJsonValue>> LoRAArray;
	for (const FLoRAEntry& Entry : S->LoRAModels)
	{
		TSharedPtr<FJsonObject> LoRA = MakeShareable(new FJsonObject);
		LoRA->SetStringField(TEXT("Name"), Entry.Name);
		LoRA->SetStringField(TEXT("Path"), Entry.PathOrIdentifier);
		LoRA->SetNumberField(TEXT("Weight"), Entry.Weight);
		LoRA->SetBoolField(TEXT("Enabled"), Entry.bEnabled);
		LoRAArray.Add(MakeShareable(new FJsonValueObject(LoRA)));
	}
	Root->SetArrayField(TEXT("LoRAs"), LoRAArray);

	// Model Paths (ComfyUI directory hints)
	TSharedPtr<FJsonObject> PathsObj = MakeShareable(new FJsonObject);
	for (const auto& Pair : S->ModelPaths)
	{
		PathsObj->SetStringField(Pair.Key, Pair.Value);
	}
	Root->SetObjectField(TEXT("ModelPaths"), PathsObj);

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	// Ensure directory exists
	const FString Dir = GetPresetsDirectory();
	IFileManager::Get().MakeDirectory(*Dir, true);

	const FString FilePath = Dir / (PresetName + TEXT(".json"));
	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Preset saved: %s"), *FilePath);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("ViewGen: Failed to save preset: %s"), *FilePath);
	return false;
}

bool UGenAISettings::LoadPreset(const FString& PresetName)
{
	if (PresetName.IsEmpty()) return false;

	const FString FilePath = GetPresetsDirectory() / (PresetName + TEXT(".json"));

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("ViewGen: Preset file not found: %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ViewGen: Failed to parse preset: %s"), *FilePath);
		return false;
	}

	UGenAISettings* S = Get();
	if (!S) return false;

	// Helper lambdas for safe reads (skip fields that don't exist in older presets)
	auto ReadString = [&](const FString& Key, FString& Out)
	{
		if (Root->HasField(Key)) Out = Root->GetStringField(Key);
	};
	auto ReadFloat = [&](const FString& Key, float& Out)
	{
		if (Root->HasField(Key)) Out = static_cast<float>(Root->GetNumberField(Key));
	};
	auto ReadInt32 = [&](const FString& Key, int32& Out)
	{
		if (Root->HasField(Key)) Out = static_cast<int32>(Root->GetNumberField(Key));
	};
	auto ReadInt64 = [&](const FString& Key, int64& Out)
	{
		if (Root->HasField(Key)) Out = static_cast<int64>(Root->GetNumberField(Key));
	};
	auto ReadBool = [&](const FString& Key, bool& Out)
	{
		if (Root->HasField(Key)) Out = Root->GetBoolField(Key);
	};

	// Connection
	ReadFloat(TEXT("TimeoutSeconds"), S->TimeoutSeconds);
	ReadFloat(TEXT("ProgressPollInterval"), S->ProgressPollInterval);

	// Model
	ReadString(TEXT("CheckpointName"), S->CheckpointName);

	// Reference Adherence
	ReadFloat(TEXT("ReferenceAdherence"), S->ReferenceAdherence);

	// Generation
	if (Root->HasField(TEXT("GenerationMode")))
	{
		S->GenerationMode = static_cast<EGenMode>(
			FMath::Clamp(static_cast<int32>(Root->GetNumberField(TEXT("GenerationMode"))), 0, 4));
	}
	ReadString(TEXT("DefaultPrompt"), S->DefaultPrompt);
	ReadString(TEXT("DefaultNegativePrompt"), S->DefaultNegativePrompt);
	ReadInt32(TEXT("OutputWidth"), S->OutputWidth);
	ReadInt32(TEXT("OutputHeight"), S->OutputHeight);
	ReadFloat(TEXT("DenoisingStrength"), S->DenoisingStrength);
	ReadInt32(TEXT("Steps"), S->Steps);
	ReadFloat(TEXT("CFGScale"), S->CFGScale);
	ReadInt64(TEXT("Seed"), S->Seed);
	ReadString(TEXT("SamplerName"), S->SamplerName);
	ReadString(TEXT("SchedulerName"), S->SchedulerName);
	ReadBool(TEXT("bAutoCameraPrompt"), S->bAutoCameraPrompt);

	// Depth / ControlNet
	ReadBool(TEXT("bEnableDepthControlNet"), S->bEnableDepthControlNet);
	ReadBool(TEXT("bUseFluxControlNet"), S->bUseFluxControlNet);
	ReadString(TEXT("FluxModelName"), S->FluxModelName);
	ReadString(TEXT("FluxWeightDtype"), S->FluxWeightDtype);
	ReadString(TEXT("FluxCLIPName1"), S->FluxCLIPName1);
	ReadString(TEXT("FluxCLIPName2"), S->FluxCLIPName2);
	ReadString(TEXT("FluxCLIPType"), S->FluxCLIPType);
	ReadString(TEXT("FluxVAEName"), S->FluxVAEName);
	ReadString(TEXT("ControlNetModel"), S->ControlNetModel);
	ReadFloat(TEXT("ControlNetWeight"), S->ControlNetWeight);
	ReadFloat(TEXT("MaxDepthDistance"), S->MaxDepthDistance);

	// Hi-Res Fix
	ReadBool(TEXT("bEnableHiResFix"), S->bEnableHiResFix);
	ReadFloat(TEXT("HiResUpscaleFactor"), S->HiResUpscaleFactor);
	ReadFloat(TEXT("HiResDenoise"), S->HiResDenoise);
	ReadInt32(TEXT("HiResSteps"), S->HiResSteps);

	// Gemini
	ReadString(TEXT("GeminiModelName"), S->GeminiModelName);
	ReadString(TEXT("GeminiAspectRatio"), S->GeminiAspectRatio);
	ReadString(TEXT("GeminiResolution"), S->GeminiResolution);
	ReadString(TEXT("GeminiResponseModalities"), S->GeminiResponseModalities);
	ReadString(TEXT("GeminiThinkingLevel"), S->GeminiThinkingLevel);
	ReadInt64(TEXT("GeminiSeed"), S->GeminiSeed);
	ReadString(TEXT("GeminiSystemPrompt"), S->GeminiSystemPrompt);

	// Kling
	ReadString(TEXT("KlingModelName"), S->KlingModelName);
	ReadString(TEXT("KlingAspectRatio"), S->KlingAspectRatio);
	ReadString(TEXT("KlingImageType"), S->KlingImageType);
	ReadFloat(TEXT("KlingImageFidelity"), S->KlingImageFidelity);
	ReadFloat(TEXT("KlingHumanFidelity"), S->KlingHumanFidelity);
	ReadInt32(TEXT("KlingImageCount"), S->KlingImageCount);
	ReadInt64(TEXT("KlingSeed"), S->KlingSeed);

	// Video (Image-to-Video)
	if (Root->HasField(TEXT("VideoMode")))
	{
		S->VideoMode = static_cast<EVideoMode>(
			FMath::Clamp(static_cast<int32>(Root->GetNumberField(TEXT("VideoMode"))), 0, 2));
	}
	ReadFloat(TEXT("VideoMotionAdherence"), S->VideoMotionAdherence);
	ReadString(TEXT("VideoPrompt"), S->VideoPrompt);
	ReadString(TEXT("VideoNegativePrompt"), S->VideoNegativePrompt);
	ReadFloat(TEXT("VideoDuration"), S->VideoDuration);
	ReadInt32(TEXT("VideoFPS"), S->VideoFPS);
	ReadFloat(TEXT("VideoCFG"), S->VideoCFG);
	ReadInt32(TEXT("VideoSteps"), S->VideoSteps);
	ReadInt64(TEXT("VideoSeed"), S->VideoSeed);
	ReadString(TEXT("KlingVideoModel"), S->KlingVideoModel);
	ReadString(TEXT("KlingVideoQuality"), S->KlingVideoQuality);
	ReadString(TEXT("WanModelName"), S->WanModelName);
	ReadString(TEXT("Veo3ModelName"), S->Veo3ModelName);
	ReadString(TEXT("Veo3AspectRatio"), S->Veo3AspectRatio);
	ReadBool(TEXT("bVeo3GenerateAudio"), S->bVeo3GenerateAudio);
	ReadString(TEXT("Veo3PersonGeneration"), S->Veo3PersonGeneration);

	// LoRAs
	if (Root->HasField(TEXT("LoRAs")))
	{
		S->LoRAModels.Empty();
		const TArray<TSharedPtr<FJsonValue>>& LoRAArray = Root->GetArrayField(TEXT("LoRAs"));
		for (const TSharedPtr<FJsonValue>& Val : LoRAArray)
		{
			const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FLoRAEntry Entry;
			Entry.Name = Obj->GetStringField(TEXT("Name"));
			Entry.PathOrIdentifier = Obj->GetStringField(TEXT("Path"));
			Entry.Weight = static_cast<float>(Obj->GetNumberField(TEXT("Weight")));
			Entry.bEnabled = Obj->GetBoolField(TEXT("Enabled"));
			S->LoRAModels.Add(Entry);
		}
	}

	// Model Paths (ComfyUI directory hints)
	if (Root->HasField(TEXT("ModelPaths")))
	{
		const TSharedPtr<FJsonObject>& PathsObj = Root->GetObjectField(TEXT("ModelPaths"));
		if (PathsObj.IsValid())
		{
			S->ModelPaths.Empty();
			for (const auto& Pair : PathsObj->Values)
			{
				FString Val;
				if (Pair.Value->TryGetString(Val))
				{
					S->ModelPaths.Add(Pair.Key, Val);
				}
			}
		}
	}
	// Always ensure defaults are present for any keys missing from the preset
	S->EnsureDefaultModelPaths();

	S->SaveConfig();
	UE_LOG(LogTemp, Log, TEXT("ViewGen: Preset loaded: %s"), *PresetName);
	return true;
}

bool UGenAISettings::DeletePreset(const FString& PresetName)
{
	if (PresetName.IsEmpty()) return false;

	const FString FilePath = GetPresetsDirectory() / (PresetName + TEXT(".json"));
	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogTemp, Log, TEXT("ViewGen: Preset deleted: %s"), *PresetName);
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("ViewGen: Failed to delete preset: %s"), *FilePath);
	return false;
}
