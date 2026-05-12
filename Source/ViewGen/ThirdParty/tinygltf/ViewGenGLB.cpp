// ViewGenGLB.cpp - Minimal GLB parser implementation for texture extraction and repacking

#include "ViewGenGLB.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace ViewGenGLB
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32 ReadUint32(const uint8* Data)
{
    // GLB is little-endian
    return static_cast<uint32>(Data[0])
         | (static_cast<uint32>(Data[1]) << 8)
         | (static_cast<uint32>(Data[2]) << 16)
         | (static_cast<uint32>(Data[3]) << 24);
}

static void WriteUint32(uint8* Dest, uint32 Value)
{
    Dest[0] = static_cast<uint8>(Value & 0xFF);
    Dest[1] = static_cast<uint8>((Value >> 8) & 0xFF);
    Dest[2] = static_cast<uint8>((Value >> 16) & 0xFF);
    Dest[3] = static_cast<uint8>((Value >> 24) & 0xFF);
}

/** Align a size up to the nearest multiple of 4. */
static uint32 Align4(uint32 Size)
{
    return (Size + 3) & ~3u;
}

/** Serialize a JSON object to a UTF-8 string. */
static FString JsonToString(const TSharedPtr<FJsonObject>& JsonObject)
{
    FString OutputString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return OutputString;
}

/** Try to derive a meaningful name for a texture image from the JSON. */
static FString DeriveTextureName(const TSharedPtr<FJsonObject>& JSON, int32 ImageIndex)
{
    // Check images array for a name
    const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
    if (JSON->TryGetArrayField(TEXT("images"), ImagesArray) && ImagesArray->IsValidIndex(ImageIndex))
    {
        const TSharedPtr<FJsonObject>& ImageObj = (*ImagesArray)[ImageIndex]->AsObject();
        if (ImageObj.IsValid())
        {
            FString Name;
            if (ImageObj->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
            {
                return Name;
            }
        }
    }

    // Check textures array - see if any texture references this image
    const TArray<TSharedPtr<FJsonValue>>* TexturesArray = nullptr;
    if (JSON->TryGetArrayField(TEXT("textures"), TexturesArray))
    {
        for (int32 i = 0; i < TexturesArray->Num(); ++i)
        {
            const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[i]->AsObject();
            if (TexObj.IsValid())
            {
                int32 Source = -1;
                if (TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                {
                    FString TexName;
                    if (TexObj->TryGetStringField(TEXT("name"), TexName) && !TexName.IsEmpty())
                    {
                        return TexName;
                    }
                }
            }
        }
    }

    // Check materials for texture usage hints
    const TArray<TSharedPtr<FJsonValue>>* MaterialsArray = nullptr;
    if (JSON->TryGetArrayField(TEXT("materials"), MaterialsArray))
    {
        for (const auto& MatVal : *MaterialsArray)
        {
            const TSharedPtr<FJsonObject>& MatObj = MatVal->AsObject();
            if (!MatObj.IsValid()) continue;

            // Check PBR metallic roughness
            const TSharedPtr<FJsonObject>* PbrObj = nullptr;
            if (MatObj->TryGetObjectField(TEXT("pbrMetallicRoughness"), PbrObj))
            {
                // Base color texture
                const TSharedPtr<FJsonObject>* BaseColorTex = nullptr;
                if ((*PbrObj)->TryGetObjectField(TEXT("baseColorTexture"), BaseColorTex))
                {
                    int32 TexIndex = -1;
                    if ((*BaseColorTex)->TryGetNumberField(TEXT("index"), TexIndex))
                    {
                        // Resolve texture -> image
                        if (TexturesArray && TexturesArray->IsValidIndex(TexIndex))
                        {
                            const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[TexIndex]->AsObject();
                            int32 Source = -1;
                            if (TexObj.IsValid() && TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                            {
                                return FString::Printf(TEXT("baseColor_%d"), ImageIndex);
                            }
                        }
                    }
                }

                // Metallic roughness texture
                const TSharedPtr<FJsonObject>* MrTex = nullptr;
                if ((*PbrObj)->TryGetObjectField(TEXT("metallicRoughnessTexture"), MrTex))
                {
                    int32 TexIndex = -1;
                    if ((*MrTex)->TryGetNumberField(TEXT("index"), TexIndex))
                    {
                        if (TexturesArray && TexturesArray->IsValidIndex(TexIndex))
                        {
                            const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[TexIndex]->AsObject();
                            int32 Source = -1;
                            if (TexObj.IsValid() && TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                            {
                                return FString::Printf(TEXT("metallicRoughness_%d"), ImageIndex);
                            }
                        }
                    }
                }
            }

            // Normal map
            const TSharedPtr<FJsonObject>* NormalTex = nullptr;
            if (MatObj->TryGetObjectField(TEXT("normalTexture"), NormalTex))
            {
                int32 TexIndex = -1;
                if ((*NormalTex)->TryGetNumberField(TEXT("index"), TexIndex))
                {
                    if (TexturesArray && TexturesArray->IsValidIndex(TexIndex))
                    {
                        const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[TexIndex]->AsObject();
                        int32 Source = -1;
                        if (TexObj.IsValid() && TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                        {
                            return FString::Printf(TEXT("normal_%d"), ImageIndex);
                        }
                    }
                }
            }

            // Emissive
            const TSharedPtr<FJsonObject>* EmissiveTex = nullptr;
            if (MatObj->TryGetObjectField(TEXT("emissiveTexture"), EmissiveTex))
            {
                int32 TexIndex = -1;
                if ((*EmissiveTex)->TryGetNumberField(TEXT("index"), TexIndex))
                {
                    if (TexturesArray && TexturesArray->IsValidIndex(TexIndex))
                    {
                        const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[TexIndex]->AsObject();
                        int32 Source = -1;
                        if (TexObj.IsValid() && TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                        {
                            return FString::Printf(TEXT("emissive_%d"), ImageIndex);
                        }
                    }
                }
            }

            // Occlusion
            const TSharedPtr<FJsonObject>* OcclusionTex = nullptr;
            if (MatObj->TryGetObjectField(TEXT("occlusionTexture"), OcclusionTex))
            {
                int32 TexIndex = -1;
                if ((*OcclusionTex)->TryGetNumberField(TEXT("index"), TexIndex))
                {
                    if (TexturesArray && TexturesArray->IsValidIndex(TexIndex))
                    {
                        const TSharedPtr<FJsonObject>& TexObj = (*TexturesArray)[TexIndex]->AsObject();
                        int32 Source = -1;
                        if (TexObj.IsValid() && TexObj->TryGetNumberField(TEXT("source"), Source) && Source == ImageIndex)
                        {
                            return FString::Printf(TEXT("occlusion_%d"), ImageIndex);
                        }
                    }
                }
            }
        }
    }

    return FString::Printf(TEXT("image_%d"), ImageIndex);
}

// ---------------------------------------------------------------------------
// LoadGLB
// ---------------------------------------------------------------------------

bool LoadGLB(const FString& FilePath, FGLBDocument& OutDoc)
{
    OutDoc.bValid = false;
    OutDoc.Textures.Empty();

    // Load file
    if (!FFileHelper::LoadFileToArray(OutDoc.RawData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: Failed to load file: %s"), *FilePath);
        return false;
    }

    const uint8* Data = OutDoc.RawData.GetData();
    const int64 FileSize = OutDoc.RawData.Num();

    // Validate header
    if (FileSize < GLB_HEADER_SIZE)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: File too small to be a valid GLB: %s"), *FilePath);
        return false;
    }

    const uint32 Magic = ReadUint32(Data);
    const uint32 Version = ReadUint32(Data + 4);
    const uint32 TotalLength = ReadUint32(Data + 8);

    if (Magic != GLB_MAGIC)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: Invalid GLB magic number in: %s (got 0x%08X, expected 0x%08X)"),
            *FilePath, Magic, GLB_MAGIC);
        return false;
    }

    if (Version != GLB_VERSION)
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: GLB version %u (expected %u), attempting to parse anyway: %s"),
            Version, GLB_VERSION, *FilePath);
    }

    if (static_cast<int64>(TotalLength) > FileSize)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: GLB header claims length %u but file is only %lld bytes: %s"),
            TotalLength, FileSize, *FilePath);
        return false;
    }

    // Parse chunks
    uint32 Offset = GLB_HEADER_SIZE;
    bool bFoundJSON = false;
    bool bFoundBIN = false;

    while (Offset + CHUNK_HEADER_SIZE <= TotalLength)
    {
        const uint32 ChunkLength = ReadUint32(Data + Offset);
        const uint32 ChunkType = ReadUint32(Data + Offset + 4);
        const uint32 ChunkDataOffset = Offset + CHUNK_HEADER_SIZE;

        if (ChunkDataOffset + ChunkLength > TotalLength)
        {
            UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: Chunk exceeds file bounds at offset %u"), Offset);
            return false;
        }

        if (ChunkType == CHUNK_TYPE_JSON && !bFoundJSON)
        {
            // Parse JSON chunk
            FString JsonString;
            // GLB JSON is UTF-8, padded with spaces (0x20)
            const auto Utf8Data = reinterpret_cast<const UTF8CHAR*>(Data + ChunkDataOffset);
            FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Utf8Data), ChunkLength);
            JsonString = FString(Converter.Length(), Converter.Get());

            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
            if (!FJsonSerializer::Deserialize(Reader, OutDoc.JSON) || !OutDoc.JSON.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: Failed to parse JSON chunk in: %s"), *FilePath);
                return false;
            }
            bFoundJSON = true;
        }
        else if (ChunkType == CHUNK_TYPE_BIN && !bFoundBIN)
        {
            // Copy BIN chunk
            OutDoc.BinaryChunk.SetNumUninitialized(ChunkLength);
            FMemory::Memcpy(OutDoc.BinaryChunk.GetData(), Data + ChunkDataOffset, ChunkLength);
            bFoundBIN = true;
        }

        // Advance to next chunk (chunks are already 4-byte aligned in a valid GLB)
        Offset = ChunkDataOffset + Align4(ChunkLength);
    }

    if (!bFoundJSON)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB: No JSON chunk found in: %s"), *FilePath);
        return false;
    }

    if (!bFoundBIN)
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: No BIN chunk found in: %s (file may have no binary data)"), *FilePath);
        // Not necessarily an error - some GLBs might have all data in URIs
    }

    // Extract texture images
    const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
    if (OutDoc.JSON->TryGetArrayField(TEXT("images"), ImagesArray))
    {
        const TArray<TSharedPtr<FJsonValue>>* BufferViewsArray = nullptr;
        OutDoc.JSON->TryGetArrayField(TEXT("bufferViews"), BufferViewsArray);

        TSet<int32> ProcessedImageIndices;

        for (int32 i = 0; i < ImagesArray->Num(); ++i)
        {
            const TSharedPtr<FJsonObject>& ImageObj = (*ImagesArray)[i]->AsObject();
            if (!ImageObj.IsValid())
            {
                continue;
            }

            // Check if image uses a URI (external reference) - skip with warning
            FString Uri;
            if (ImageObj->TryGetStringField(TEXT("uri"), Uri))
            {
                // Data URIs (base64 embedded) could be supported but we skip for simplicity
                if (!Uri.StartsWith(TEXT("data:")))
                {
                    UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Image %d uses external URI '%s' - skipping (only embedded images supported)"), i, *Uri);
                    continue;
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Image %d uses data URI - skipping (only bufferView images supported)"), i);
                    continue;
                }
            }

            // Get bufferView index
            int32 BufferViewIndex = -1;
            if (!ImageObj->TryGetNumberField(TEXT("bufferView"), BufferViewIndex))
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Image %d has no bufferView and no URI - skipping"), i);
                continue;
            }

            if (!BufferViewsArray || !BufferViewsArray->IsValidIndex(BufferViewIndex))
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Image %d references invalid bufferView %d - skipping"), i, BufferViewIndex);
                continue;
            }

            // Skip duplicates (multiple textures may reference the same image)
            if (ProcessedImageIndices.Contains(i))
            {
                continue;
            }
            ProcessedImageIndices.Add(i);

            // Get buffer view details
            const TSharedPtr<FJsonObject>& BvObj = (*BufferViewsArray)[BufferViewIndex]->AsObject();
            if (!BvObj.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Invalid bufferView object at index %d - skipping image %d"), BufferViewIndex, i);
                continue;
            }

            int32 ByteOffset = 0;
            BvObj->TryGetNumberField(TEXT("byteOffset"), ByteOffset);
            int32 ByteLength = 0;
            if (!BvObj->TryGetNumberField(TEXT("byteLength"), ByteLength) || ByteLength <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: bufferView %d has invalid byteLength - skipping image %d"), BufferViewIndex, i);
                continue;
            }

            // Validate bounds against the BIN chunk
            if (ByteOffset + ByteLength > OutDoc.BinaryChunk.Num())
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: bufferView %d exceeds BIN chunk bounds (offset=%d, length=%d, bin_size=%d) - skipping image %d"),
                    BufferViewIndex, ByteOffset, ByteLength, OutDoc.BinaryChunk.Num(), i);
                continue;
            }

            // Get MIME type
            FString MimeType;
            if (!ImageObj->TryGetStringField(TEXT("mimeType"), MimeType))
            {
                // Try to detect from magic bytes
                const uint8* ImgData = OutDoc.BinaryChunk.GetData() + ByteOffset;
                if (ByteLength >= 8 && ImgData[0] == 0x89 && ImgData[1] == 'P' && ImgData[2] == 'N' && ImgData[3] == 'G')
                {
                    MimeType = TEXT("image/png");
                }
                else if (ByteLength >= 2 && ImgData[0] == 0xFF && ImgData[1] == 0xD8)
                {
                    MimeType = TEXT("image/jpeg");
                }
                else
                {
                    MimeType = TEXT("application/octet-stream");
                    UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB: Could not determine MIME type for image %d, using octet-stream"), i);
                }
            }

            // Extract image data
            FTextureImage TexImage;
            TexImage.Name = DeriveTextureName(OutDoc.JSON, i);
            TexImage.MimeType = MimeType;
            TexImage.ImageIndex = i;
            TexImage.BufferViewIndex = BufferViewIndex;
            TexImage.ImageData.SetNumUninitialized(ByteLength);
            FMemory::Memcpy(TexImage.ImageData.GetData(), OutDoc.BinaryChunk.GetData() + ByteOffset, ByteLength);

            OutDoc.Textures.Add(MoveTemp(TexImage));
        }
    }

    OutDoc.bValid = true;
    UE_LOG(LogTemp, Log, TEXT("ViewGenGLB: Successfully loaded GLB with %d textures from: %s"), OutDoc.Textures.Num(), *FilePath);
    return true;
}

// ---------------------------------------------------------------------------
// ProbeGLBTextures — lightweight metadata extraction
// ---------------------------------------------------------------------------

bool ProbeGLBTextures(const FString& FilePath, TArray<FTextureInfo>& OutInfos,
    TArray<uint8>* OutFirstThumbnail, int32* OutThumbW, int32* OutThumbH)
{
    OutInfos.Empty();

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB Probe: Failed to read file: %s"), *FilePath);
        return false;
    }

    if (FileData.Num() < (int32)GLB_HEADER_SIZE)
        return false;

    const uint8* Data = FileData.GetData();
    uint32 Magic = ReadUint32(Data);
    if (Magic != GLB_MAGIC)
        return false;

    uint32 TotalLength = ReadUint32(Data + 8);
    if ((int32)TotalLength > FileData.Num())
        return false;

    // Parse chunks
    uint32 Offset = GLB_HEADER_SIZE;
    const uint8* JsonData = nullptr;
    uint32 JsonLength = 0;
    const uint8* BinData = nullptr;
    uint32 BinLength = 0;

    while (Offset + CHUNK_HEADER_SIZE <= TotalLength)
    {
        uint32 ChunkLen = ReadUint32(Data + Offset);
        uint32 ChunkType = ReadUint32(Data + Offset + 4);
        const uint8* ChunkData = Data + Offset + CHUNK_HEADER_SIZE;

        if (ChunkType == CHUNK_TYPE_JSON)
        {
            JsonData = ChunkData;
            JsonLength = ChunkLen;
        }
        else if (ChunkType == CHUNK_TYPE_BIN)
        {
            BinData = ChunkData;
            BinLength = ChunkLen;
        }
        Offset += CHUNK_HEADER_SIZE + Align4(ChunkLen);
    }

    if (!JsonData || !BinData)
        return false;

    // Parse JSON
    FString JsonString;
    FFileHelper::BufferToString(JsonString, JsonData, JsonLength);
    TSharedPtr<FJsonObject> JSON;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JSON) || !JSON.IsValid())
        return false;

    // Get images array
    const TArray<TSharedPtr<FJsonValue>>* ImagesArr = nullptr;
    if (!JSON->TryGetArrayField(TEXT("images"), ImagesArr) || ImagesArr->Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("ViewGenGLB Probe: No images in GLB"));
        return true; // Valid GLB, just no textures
    }

    // Get bufferViews array
    const TArray<TSharedPtr<FJsonValue>>* BVArr = nullptr;
    JSON->TryGetArrayField(TEXT("bufferViews"), BVArr);

    // Load IImageWrapper module for dimension reading
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

    bool bFirstThumbnailDone = false;

    for (int32 i = 0; i < ImagesArr->Num(); ++i)
    {
        TSharedPtr<FJsonObject> ImgObj = (*ImagesArr)[i]->AsObject();
        if (!ImgObj.IsValid()) continue;

        FTextureInfo Info;
        Info.Name = DeriveTextureName(JSON, i);
        ImgObj->TryGetStringField(TEXT("mimeType"), Info.MimeType);

        // Get image data from bufferView
        int32 BVIndex = -1;
        if (!ImgObj->TryGetNumberField(TEXT("bufferView"), BVIndex) || !BVArr || !BVArr->IsValidIndex(BVIndex))
            continue;

        TSharedPtr<FJsonObject> BV = (*BVArr)[BVIndex]->AsObject();
        if (!BV.IsValid()) continue;

        int32 ByteOffset = 0, ByteLength = 0;
        BV->TryGetNumberField(TEXT("byteOffset"), ByteOffset);
        BV->TryGetNumberField(TEXT("byteLength"), ByteLength);

        if (ByteOffset + ByteLength > (int32)BinLength || ByteLength <= 0)
            continue;

        Info.DataSize = ByteLength;

        // Read dimensions from image header using IImageWrapper
        const uint8* ImgBytes = BinData + ByteOffset;
        EImageFormat Format = EImageFormat::PNG;
        if (Info.MimeType.Contains(TEXT("jpeg")) || Info.MimeType.Contains(TEXT("jpg")))
            Format = EImageFormat::JPEG;

        TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
        if (Wrapper.IsValid() && Wrapper->SetCompressed(ImgBytes, ByteLength))
        {
            Info.Width = Wrapper->GetWidth();
            Info.Height = Wrapper->GetHeight();

            // Optionally decode first texture as thumbnail
            if (OutFirstThumbnail && !bFirstThumbnailDone)
            {
                TArray<uint8> Raw;
                if (Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw))
                {
                    *OutFirstThumbnail = MoveTemp(Raw);
                    if (OutThumbW) *OutThumbW = Info.Width;
                    if (OutThumbH) *OutThumbH = Info.Height;
                    bFirstThumbnailDone = true;
                }
            }
        }

        OutInfos.Add(Info);
    }

    UE_LOG(LogTemp, Log, TEXT("ViewGenGLB Probe: Found %d textures in %s"), OutInfos.Num(), *FilePath);
    return true;
}

// ---------------------------------------------------------------------------
// ReplaceTexture
// ---------------------------------------------------------------------------

bool ReplaceTexture(FGLBDocument& Doc, int32 TextureIndex, const TArray<uint8>& NewImageData, const FString& NewMimeType)
{
    if (!Doc.bValid)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::ReplaceTexture: Document is not valid"));
        return false;
    }

    if (!Doc.Textures.IsValidIndex(TextureIndex))
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::ReplaceTexture: Invalid texture index %d (have %d textures)"),
            TextureIndex, Doc.Textures.Num());
        return false;
    }

    if (NewImageData.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::ReplaceTexture: NewImageData is empty"));
        return false;
    }

    FTextureImage& Texture = Doc.Textures[TextureIndex];
    Texture.ImageData = NewImageData;
    Texture.MimeType = NewMimeType;

    // Update the MIME type in the JSON images array
    const TArray<TSharedPtr<FJsonValue>>* ImagesArray = nullptr;
    if (Doc.JSON->TryGetArrayField(TEXT("images"), ImagesArray))
    {
        if (ImagesArray->IsValidIndex(Texture.ImageIndex))
        {
            TSharedPtr<FJsonObject> ImageObj = (*ImagesArray)[Texture.ImageIndex]->AsObject();
            if (ImageObj.IsValid())
            {
                ImageObj->SetStringField(TEXT("mimeType"), NewMimeType);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("ViewGenGLB::ReplaceTexture: Replaced texture %d (%s) with %d bytes of %s"),
        TextureIndex, *Texture.Name, NewImageData.Num(), *NewMimeType);
    return true;
}

// ---------------------------------------------------------------------------
// SaveGLB
// ---------------------------------------------------------------------------

bool SaveGLB(const FGLBDocument& Doc, const FString& OutputPath)
{
    if (!Doc.bValid || !Doc.JSON.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::SaveGLB: Document is not valid"));
        return false;
    }

    // Build a map from bufferView index to texture index for quick lookup
    TMap<int32, int32> BufferViewToTextureIdx;
    for (int32 i = 0; i < Doc.Textures.Num(); ++i)
    {
        BufferViewToTextureIdx.Add(Doc.Textures[i].BufferViewIndex, i);
    }

    // Get the bufferViews array
    const TArray<TSharedPtr<FJsonValue>>* BufferViewsArray = nullptr;
    if (!Doc.JSON->TryGetArrayField(TEXT("bufferViews"), BufferViewsArray) || !BufferViewsArray)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::SaveGLB: No bufferViews in JSON"));
        return false;
    }

    // Rebuild the BIN chunk
    // Strategy: Rebuild the entire BIN chunk, updating offsets for image bufferViews
    // while preserving non-image bufferView data at their relative positions.
    //
    // We need to:
    // 1. Collect all bufferViews, determine which are images and which are geometry/other
    // 2. Rebuild BIN chunk with updated image data
    // 3. Update byteOffset for each bufferView in the JSON

    struct FBufferViewInfo
    {
        int32 Index;
        int32 OriginalOffset;
        int32 OriginalLength;
        bool bIsImage;
        int32 TextureIdx;       // Only valid if bIsImage
        int32 NewOffset;        // Computed during rebuild
        int32 NewLength;        // Computed during rebuild
    };

    TArray<FBufferViewInfo> BufferViewInfos;
    BufferViewInfos.Reserve(BufferViewsArray->Num());

    for (int32 i = 0; i < BufferViewsArray->Num(); ++i)
    {
        const TSharedPtr<FJsonObject>& BvObj = (*BufferViewsArray)[i]->AsObject();
        FBufferViewInfo Info;
        Info.Index = i;
        Info.OriginalOffset = 0;
        BvObj->TryGetNumberField(TEXT("byteOffset"), Info.OriginalOffset);
        Info.OriginalLength = 0;
        BvObj->TryGetNumberField(TEXT("byteLength"), Info.OriginalLength);

        const int32* TexIdx = BufferViewToTextureIdx.Find(i);
        Info.bIsImage = (TexIdx != nullptr);
        Info.TextureIdx = Info.bIsImage ? *TexIdx : -1;
        Info.NewOffset = 0;
        Info.NewLength = Info.bIsImage ? Doc.Textures[*TexIdx].ImageData.Num() : Info.OriginalLength;

        BufferViewInfos.Add(Info);
    }

    // Sort by original offset to maintain relative ordering
    BufferViewInfos.Sort([](const FBufferViewInfo& A, const FBufferViewInfo& B)
    {
        return A.OriginalOffset < B.OriginalOffset;
    });

    // Compute new offsets (pack sequentially, respecting alignment if bufferView has byteStride)
    uint32 CurrentOffset = 0;
    for (FBufferViewInfo& Info : BufferViewInfos)
    {
        // Some bufferViews (especially for accessors) need specific alignment
        // The glTF spec recommends 4-byte alignment for bufferViews
        CurrentOffset = Align4(CurrentOffset);
        Info.NewOffset = CurrentOffset;
        CurrentOffset += Info.NewLength;
    }

    // Build new BIN chunk
    const uint32 NewBinSize = Align4(CurrentOffset);
    TArray<uint8> NewBinChunk;
    NewBinChunk.SetNumZeroed(NewBinSize);

    for (const FBufferViewInfo& Info : BufferViewInfos)
    {
        if (Info.bIsImage)
        {
            const TArray<uint8>& ImgData = Doc.Textures[Info.TextureIdx].ImageData;
            FMemory::Memcpy(NewBinChunk.GetData() + Info.NewOffset, ImgData.GetData(), ImgData.Num());
        }
        else
        {
            // Copy original data from the original BIN chunk
            if (Info.OriginalOffset + Info.OriginalLength <= Doc.BinaryChunk.Num())
            {
                FMemory::Memcpy(NewBinChunk.GetData() + Info.NewOffset,
                    Doc.BinaryChunk.GetData() + Info.OriginalOffset,
                    Info.OriginalLength);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenGLB::SaveGLB: bufferView %d original data exceeds BIN chunk, zeroing"), Info.Index);
            }
        }
    }

    // Update JSON bufferViews with new offsets and lengths
    // We need a mutable copy of the JSON
    TSharedPtr<FJsonObject> MutableJSON = Doc.JSON;

    // Re-get the array (it's the same pointer, we just need to modify the objects)
    for (const FBufferViewInfo& Info : BufferViewInfos)
    {
        TSharedPtr<FJsonObject> BvObj = (*BufferViewsArray)[Info.Index]->AsObject();
        if (BvObj.IsValid())
        {
            BvObj->SetNumberField(TEXT("byteOffset"), Info.NewOffset);
            BvObj->SetNumberField(TEXT("byteLength"), Info.NewLength);
        }
    }

    // Update buffers[0].byteLength
    const TArray<TSharedPtr<FJsonValue>>* BuffersArray = nullptr;
    if (MutableJSON->TryGetArrayField(TEXT("buffers"), BuffersArray) && BuffersArray->Num() > 0)
    {
        TSharedPtr<FJsonObject> Buffer0 = (*BuffersArray)[0]->AsObject();
        if (Buffer0.IsValid())
        {
            Buffer0->SetNumberField(TEXT("byteLength"), static_cast<double>(NewBinChunk.Num()));
        }
    }

    // Serialize JSON to UTF-8
    FString JsonString = JsonToString(MutableJSON);
    FTCHARToUTF8 Utf8Json(*JsonString);
    const uint32 JsonByteLength = static_cast<uint32>(Utf8Json.Length());
    const uint32 JsonPaddedLength = Align4(JsonByteLength);

    // Build the complete GLB file
    const uint32 BinPaddedLength = Align4(static_cast<uint32>(NewBinChunk.Num()));
    const uint32 TotalLength = GLB_HEADER_SIZE
        + CHUNK_HEADER_SIZE + JsonPaddedLength
        + (NewBinChunk.Num() > 0 ? CHUNK_HEADER_SIZE + BinPaddedLength : 0);

    TArray<uint8> OutputData;
    OutputData.SetNumZeroed(TotalLength);
    uint8* Out = OutputData.GetData();

    // Write GLB header
    WriteUint32(Out + 0, GLB_MAGIC);
    WriteUint32(Out + 4, GLB_VERSION);
    WriteUint32(Out + 8, TotalLength);

    uint32 WriteOffset = GLB_HEADER_SIZE;

    // Write JSON chunk
    WriteUint32(Out + WriteOffset, JsonPaddedLength);
    WriteUint32(Out + WriteOffset + 4, CHUNK_TYPE_JSON);
    FMemory::Memcpy(Out + WriteOffset + CHUNK_HEADER_SIZE, Utf8Json.Get(), JsonByteLength);
    // Pad with spaces (0x20) per GLB spec
    for (uint32 i = JsonByteLength; i < JsonPaddedLength; ++i)
    {
        Out[WriteOffset + CHUNK_HEADER_SIZE + i] = 0x20;
    }
    WriteOffset += CHUNK_HEADER_SIZE + JsonPaddedLength;

    // Write BIN chunk (if we have binary data)
    if (NewBinChunk.Num() > 0)
    {
        WriteUint32(Out + WriteOffset, BinPaddedLength);
        WriteUint32(Out + WriteOffset + 4, CHUNK_TYPE_BIN);
        FMemory::Memcpy(Out + WriteOffset + CHUNK_HEADER_SIZE, NewBinChunk.GetData(), NewBinChunk.Num());
        // Pad with zeros (0x00) per GLB spec - already zeroed from SetNumZeroed
        WriteOffset += CHUNK_HEADER_SIZE + BinPaddedLength;
    }

    // Write to disk
    if (!FFileHelper::SaveArrayToFile(OutputData, *OutputPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenGLB::SaveGLB: Failed to write file: %s"), *OutputPath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("ViewGenGLB::SaveGLB: Wrote %u bytes to: %s"), TotalLength, *OutputPath);
    return true;
}

} // namespace ViewGenGLB
