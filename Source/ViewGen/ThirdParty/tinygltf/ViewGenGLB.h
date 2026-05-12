#pragma once

// ViewGenGLB.h - Minimal GLB parser for texture extraction and repacking
// Only handles binary GLB format (not separate .gltf + .bin files)
//
// GLB Binary Format (glTF 2.0):
//   Header:  magic(4) + version(4) + length(4) = 12 bytes
//   Chunk 0: chunkLength(4) + chunkType(4) + JSON data (padded to 4-byte alignment with spaces)
//   Chunk 1: chunkLength(4) + chunkType(4) + BIN data  (padded to 4-byte alignment with 0x00)

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ViewGenGLB
{
    /** Represents a single texture image extracted from a GLB file. */
    struct FTextureImage
    {
        FString Name;               // e.g. "baseColor", "normal", "metallicRoughness"
        TArray<uint8> ImageData;    // Raw PNG or JPEG bytes
        FString MimeType;           // "image/png" or "image/jpeg"
        int32 ImageIndex;           // Index in the glTF images array
        int32 BufferViewIndex;      // Index of the bufferView for this image
    };

    /** Represents a parsed GLB document with its JSON and binary chunks. */
    struct FGLBDocument
    {
        TArray<uint8> RawData;              // The entire GLB file
        TSharedPtr<FJsonObject> JSON;       // Parsed JSON chunk
        TArray<uint8> BinaryChunk;          // The BIN chunk
        TArray<FTextureImage> Textures;     // Extracted textures
        bool bValid = false;
    };

    // GLB constants
    static constexpr uint32 GLB_MAGIC       = 0x46546C67; // "glTF"
    static constexpr uint32 GLB_VERSION     = 2;
    static constexpr uint32 CHUNK_TYPE_JSON = 0x4E4F534A; // "JSON"
    static constexpr uint32 CHUNK_TYPE_BIN  = 0x004E4942; // "BIN\0"
    static constexpr uint32 GLB_HEADER_SIZE = 12;
    static constexpr uint32 CHUNK_HEADER_SIZE = 8;

    /** Lightweight texture metadata returned by ProbeGLBTextures (no pixel decode). */
    struct FTextureInfo
    {
        FString Name;       // e.g. "baseColor", "normal", "metallicRoughness"
        int32 Width = 0;
        int32 Height = 0;
        int32 DataSize = 0; // Compressed image data size in bytes
        FString MimeType;   // "image/png" or "image/jpeg"
    };

    /**
     * Probe a GLB file for texture metadata without fully loading pixel data.
     * Parses the JSON and BIN chunks, identifies all textures, and reads
     * image dimensions from PNG/JPEG headers via IImageWrapper.
     *
     * @param FilePath   Absolute path to the .glb file
     * @param OutInfos   Populated with metadata for each texture found
     * @param OutFirstThumbnail  If non-null, decodes the first texture to RGBA pixels for thumbnail display
     * @param OutThumbW  Width of the decoded thumbnail (only valid if OutFirstThumbnail is non-null)
     * @param OutThumbH  Height of the decoded thumbnail
     * @return true if the file was parsed successfully
     */
    bool ProbeGLBTextures(const FString& FilePath, TArray<FTextureInfo>& OutInfos,
        TArray<uint8>* OutFirstThumbnail = nullptr, int32* OutThumbW = nullptr, int32* OutThumbH = nullptr);

    /**
     * Load a GLB file and extract all texture images.
     * @param FilePath  Absolute path to the .glb file
     * @param OutDoc    Populated document on success
     * @return true if the file was loaded and parsed successfully
     */
    bool LoadGLB(const FString& FilePath, FGLBDocument& OutDoc);

    /**
     * Replace a texture's image data in the document.
     * @param Doc           The loaded GLB document
     * @param TextureIndex  Index into Doc.Textures
     * @param NewImageData  New PNG or JPEG bytes
     * @param NewMimeType   MIME type of the new image data
     * @return true if the replacement was successful
     */
    bool ReplaceTexture(FGLBDocument& Doc, int32 TextureIndex, const TArray<uint8>& NewImageData, const FString& NewMimeType);

    /**
     * Write the modified document back to a GLB file.
     * Rebuilds BIN chunk from current texture data and updates all JSON offsets.
     * @param Doc        The GLB document (may have been modified via ReplaceTexture)
     * @param OutputPath Absolute path for the output .glb file
     * @return true if the file was written successfully
     */
    bool SaveGLB(const FGLBDocument& Doc, const FString& OutputPath);
}
