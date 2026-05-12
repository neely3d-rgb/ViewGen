// ViewGenUVRepack.cpp — UV atlas repacking implementation using xatlas
// Copyright ViewGen. All Rights Reserved.

#include "ViewGenUVRepack.h"
#include "xatlas.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Framework/Application/SlateApplication.h"

// ============================================================================
// glTF Accessor Helpers
// ============================================================================

namespace
{

/** Component type enum from glTF spec. */
enum EComponentType : int32
{
    CT_BYTE           = 5120,
    CT_UNSIGNED_BYTE  = 5121,
    CT_SHORT          = 5122,
    CT_UNSIGNED_SHORT = 5123,
    CT_UNSIGNED_INT   = 5125,
    CT_FLOAT          = 5126
};

/** Get byte size of a glTF component type. */
int32 ComponentSize(int32 CompType)
{
    switch (CompType)
    {
    case CT_BYTE:           return 1;
    case CT_UNSIGNED_BYTE:  return 1;
    case CT_SHORT:          return 2;
    case CT_UNSIGNED_SHORT: return 2;
    case CT_UNSIGNED_INT:   return 4;
    case CT_FLOAT:          return 4;
    default:                return 0;
    }
}

/** Number of components for a glTF type string. */
int32 TypeComponentCount(const FString& Type)
{
    if (Type == TEXT("SCALAR")) return 1;
    if (Type == TEXT("VEC2"))   return 2;
    if (Type == TEXT("VEC3"))   return 3;
    if (Type == TEXT("VEC4"))   return 4;
    if (Type == TEXT("MAT2"))   return 4;
    if (Type == TEXT("MAT3"))   return 9;
    if (Type == TEXT("MAT4"))   return 16;
    return 0;
}

/** Read accessor data as float array from the BIN chunk. */
bool ReadAccessorFloat(
    const TSharedPtr<FJsonObject>& JSON,
    const TArray<uint8>& BinChunk,
    int32 AccessorIndex,
    TArray<float>& OutData,
    int32& OutCount,
    int32& OutComponents)
{
    const TArray<TSharedPtr<FJsonValue>>* Accessors;
    if (!JSON->TryGetArrayField(TEXT("accessors"), Accessors) || !Accessors->IsValidIndex(AccessorIndex))
        return false;

    const TSharedPtr<FJsonObject>& Acc = (*Accessors)[AccessorIndex]->AsObject();
    if (!Acc.IsValid()) return false;

    int32 BVIndex = 0;
    Acc->TryGetNumberField(TEXT("bufferView"), BVIndex);
    int32 ByteOffset = 0;
    Acc->TryGetNumberField(TEXT("byteOffset"), ByteOffset);
    int32 CompType = CT_FLOAT;
    Acc->TryGetNumberField(TEXT("componentType"), CompType);
    int32 Count = 0;
    Acc->TryGetNumberField(TEXT("count"), Count);
    FString Type;
    Acc->TryGetStringField(TEXT("type"), Type);

    int32 NumComponents = TypeComponentCount(Type);
    if (NumComponents == 0 || Count == 0) return false;

    // Get buffer view
    const TArray<TSharedPtr<FJsonValue>>* BufferViews;
    if (!JSON->TryGetArrayField(TEXT("bufferViews"), BufferViews) || !BufferViews->IsValidIndex(BVIndex))
        return false;

    const TSharedPtr<FJsonObject>& BV = (*BufferViews)[BVIndex]->AsObject();
    int32 BVOffset = 0;
    BV->TryGetNumberField(TEXT("byteOffset"), BVOffset);
    int32 BVStride = 0;
    BV->TryGetNumberField(TEXT("byteStride"), BVStride);

    int32 ElemSize = ComponentSize(CompType) * NumComponents;
    if (BVStride == 0) BVStride = ElemSize;

    int32 BaseOffset = BVOffset + ByteOffset;

    OutData.SetNumUninitialized(Count * NumComponents);
    OutCount = Count;
    OutComponents = NumComponents;

    for (int32 i = 0; i < Count; ++i)
    {
        int32 SrcOffset = BaseOffset + i * BVStride;

        for (int32 c = 0; c < NumComponents; ++c)
        {
            int32 CompOffset = SrcOffset + c * ComponentSize(CompType);
            float Value = 0.0f;

            if (CompOffset + ComponentSize(CompType) > BinChunk.Num())
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Accessor read out of bounds at index %d"), i);
                return false;
            }

            switch (CompType)
            {
            case CT_FLOAT:
                FMemory::Memcpy(&Value, &BinChunk[CompOffset], 4);
                break;
            case CT_UNSIGNED_SHORT:
                Value = static_cast<float>(*reinterpret_cast<const uint16*>(&BinChunk[CompOffset]));
                break;
            case CT_UNSIGNED_INT:
                Value = static_cast<float>(*reinterpret_cast<const uint32*>(&BinChunk[CompOffset]));
                break;
            case CT_SHORT:
                Value = static_cast<float>(*reinterpret_cast<const int16*>(&BinChunk[CompOffset]));
                break;
            case CT_UNSIGNED_BYTE:
                Value = static_cast<float>(BinChunk[CompOffset]);
                break;
            case CT_BYTE:
                Value = static_cast<float>(*reinterpret_cast<const int8*>(&BinChunk[CompOffset]));
                break;
            }

            OutData[i * NumComponents + c] = Value;
        }
    }

    return true;
}

/** Read index accessor as uint32 array. */
bool ReadIndices(
    const TSharedPtr<FJsonObject>& JSON,
    const TArray<uint8>& BinChunk,
    int32 AccessorIndex,
    TArray<uint32>& OutIndices)
{
    const TArray<TSharedPtr<FJsonValue>>* Accessors;
    if (!JSON->TryGetArrayField(TEXT("accessors"), Accessors) || !Accessors->IsValidIndex(AccessorIndex))
        return false;

    const TSharedPtr<FJsonObject>& Acc = (*Accessors)[AccessorIndex]->AsObject();
    if (!Acc.IsValid()) return false;

    int32 BVIndex = 0;
    Acc->TryGetNumberField(TEXT("bufferView"), BVIndex);
    int32 ByteOffset = 0;
    Acc->TryGetNumberField(TEXT("byteOffset"), ByteOffset);
    int32 CompType = CT_UNSIGNED_SHORT;
    Acc->TryGetNumberField(TEXT("componentType"), CompType);
    int32 Count = 0;
    Acc->TryGetNumberField(TEXT("count"), Count);

    const TArray<TSharedPtr<FJsonValue>>* BufferViews;
    if (!JSON->TryGetArrayField(TEXT("bufferViews"), BufferViews) || !BufferViews->IsValidIndex(BVIndex))
        return false;

    const TSharedPtr<FJsonObject>& BV = (*BufferViews)[BVIndex]->AsObject();
    int32 BVOffset = 0;
    BV->TryGetNumberField(TEXT("byteOffset"), BVOffset);

    int32 BaseOffset = BVOffset + ByteOffset;
    OutIndices.SetNumUninitialized(Count);

    for (int32 i = 0; i < Count; ++i)
    {
        int32 Offset = BaseOffset + i * ComponentSize(CompType);
        if (Offset + ComponentSize(CompType) > BinChunk.Num()) return false;

        switch (CompType)
        {
        case CT_UNSIGNED_SHORT:
            OutIndices[i] = *reinterpret_cast<const uint16*>(&BinChunk[Offset]);
            break;
        case CT_UNSIGNED_INT:
            OutIndices[i] = *reinterpret_cast<const uint32*>(&BinChunk[Offset]);
            break;
        case CT_UNSIGNED_BYTE:
            OutIndices[i] = BinChunk[Offset];
            break;
        default:
            OutIndices[i] = 0;
            break;
        }
    }

    return true;
}

// ============================================================================
// Texture Sampling & Baking
// ============================================================================

/** Decode a PNG/JPEG image to RGBA8 pixels. Returns true on success. */
bool DecodeImageToRGBA(const TArray<uint8>& ImageData, const FString& MimeType,
    TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

    EImageFormat Format = MimeType.Contains(TEXT("png")) ? EImageFormat::PNG : EImageFormat::JPEG;
    TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);

    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(ImageData.GetData(), ImageData.Num()))
        return false;

    TArray<uint8> Raw;
    // Use RGBA — avoids UE's internal BGRA swizzle which can be inconsistent
    // between PNG and JPEG codecs across engine versions.
    if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw))
        return false;

    OutWidth = Wrapper->GetWidth();
    OutHeight = Wrapper->GetHeight();
    OutPixels = MoveTemp(Raw);
    return true;
}

/** Sample a pixel from RGBA8 image data with bilinear filtering. */
FLinearColor SampleBilinear(const TArray<uint8>& Pixels, int32 Width, int32 Height, float U, float V)
{
    // Wrap UVs
    U = FMath::Frac(U);
    V = FMath::Frac(V);
    if (U < 0) U += 1.0f;
    if (V < 0) V += 1.0f;

    float X = U * (Width - 1);
    float Y = V * (Height - 1);

    int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, Width - 1);
    int32 Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, Height - 1);
    int32 X1 = FMath::Clamp(X0 + 1, 0, Width - 1);
    int32 Y1 = FMath::Clamp(Y0 + 1, 0, Height - 1);

    float FracX = X - X0;
    float FracY = Y - Y0;

    auto GetPixel = [&](int32 PX, int32 PY) -> FLinearColor
    {
        int32 Idx = (PY * Width + PX) * 4;
        return FLinearColor(
            Pixels[Idx + 0] / 255.0f,  // R
            Pixels[Idx + 1] / 255.0f,  // G
            Pixels[Idx + 2] / 255.0f,  // B
            Pixels[Idx + 3] / 255.0f); // A
    };

    FLinearColor C00 = GetPixel(X0, Y0);
    FLinearColor C10 = GetPixel(X1, Y0);
    FLinearColor C01 = GetPixel(X0, Y1);
    FLinearColor C11 = GetPixel(X1, Y1);

    FLinearColor Top = FMath::Lerp(C00, C10, FracX);
    FLinearColor Bot = FMath::Lerp(C01, C11, FracX);
    return FMath::Lerp(Top, Bot, FracY);
}

/** Encode RGBA8 pixels to PNG. */
bool EncodePixelsToPNG(const TArray<uint8>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPNG)
{
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!Wrapper.IsValid()) return false;
    if (!Wrapper->SetRaw(Pixels.GetData(), Pixels.Num(), Width, Height, ERGBFormat::RGBA, 8))
        return false;

    const TArray64<uint8>& Compressed = Wrapper->GetCompressed();
    OutPNG.SetNumUninitialized(Compressed.Num());
    FMemory::Memcpy(OutPNG.GetData(), Compressed.GetData(), Compressed.Num());
    return OutPNG.Num() > 0;
}

/**
 * Resize an RGBA8 pixel buffer using bilinear interpolation.
 * Used as the upres step after UV-repacked textures have been baked.
 */
bool ResizeImageBilinear(
    const TArray<uint8>& SrcPixels, int32 SrcWidth, int32 SrcHeight,
    int32 DstWidth, int32 DstHeight,
    TArray<uint8>& OutPixels)
{
    if (SrcWidth <= 0 || SrcHeight <= 0 || DstWidth <= 0 || DstHeight <= 0)
        return false;

    OutPixels.SetNumUninitialized(DstWidth * DstHeight * 4);

    for (int32 DY = 0; DY < DstHeight; ++DY)
    {
        float V = (DY + 0.5f) / DstHeight;
        for (int32 DX = 0; DX < DstWidth; ++DX)
        {
            float U = (DX + 0.5f) / DstWidth;

            // Use SampleBilinear to get filtered source color
            FLinearColor Color = SampleBilinear(SrcPixels, SrcWidth, SrcHeight, U, V);

            int32 Idx = (DY * DstWidth + DX) * 4;
            OutPixels[Idx + 0] = FMath::Clamp(FMath::RoundToInt(Color.R * 255.0f), 0, 255);
            OutPixels[Idx + 1] = FMath::Clamp(FMath::RoundToInt(Color.G * 255.0f), 0, 255);
            OutPixels[Idx + 2] = FMath::Clamp(FMath::RoundToInt(Color.B * 255.0f), 0, 255);
            OutPixels[Idx + 3] = FMath::Clamp(FMath::RoundToInt(Color.A * 255.0f), 0, 255);
        }
    }
    return true;
}

/**
 * Bake a texture from old UVs to new UVs using software rasterization.
 * For each triangle, sample the source texture using old UVs and write
 * to the target atlas using new UVs.
 *
 * XAtlasWidth/XAtlasHeight are the dimensions xatlas reported (used to
 * normalize xatlas pixel-space UVs to [0,1]).
 * BakeWidth/BakeHeight are the actual output texture dimensions (power-of-2).
 */
bool BakeTexture(
    const TArray<uint8>& SrcPixels, int32 SrcWidth, int32 SrcHeight,
    const TArray<float>& OldUVs,       // Original UV coords (2 floats per vertex)
    const TArray<float>& NewUVs,       // xatlas-generated UV coords (2 floats per new vertex)
    const TArray<uint32>& OldIndices,   // Original triangle indices
    const xatlas::Mesh& XMesh,          // xatlas output mesh (remapped indices + vertices)
    int32 XAtlasWidth, int32 XAtlasHeight, // xatlas reported dimensions (for UV normalization)
    int32 BakeWidth, int32 BakeHeight,     // Target output dimensions (power-of-2)
    int32 DilationIterations,           // Number of edge-padding dilation passes
    TArray<uint8>& OutPixels)
{
    // Initialize output to black with zero alpha (alpha=0 marks empty pixels)
    int32 TotalPixels = BakeWidth * BakeHeight;
    OutPixels.SetNumZeroed(TotalPixels * 4);

    // Coverage mask — tracks which pixels were filled by rasterization
    TArray<uint8> Mask;
    Mask.SetNumZeroed(TotalPixels);

    // For each triangle in the xatlas output
    for (uint32 TriIdx = 0; TriIdx < XMesh.indexCount / 3; ++TriIdx)
    {
        // Get the three xatlas output vertices
        uint32 XI0 = XMesh.indexArray[TriIdx * 3 + 0];
        uint32 XI1 = XMesh.indexArray[TriIdx * 3 + 1];
        uint32 XI2 = XMesh.indexArray[TriIdx * 3 + 2];

        const xatlas::Vertex& V0 = XMesh.vertexArray[XI0];
        const xatlas::Vertex& V1 = XMesh.vertexArray[XI1];
        const xatlas::Vertex& V2 = XMesh.vertexArray[XI2];

        if (V0.atlasIndex < 0 || V1.atlasIndex < 0 || V2.atlasIndex < 0)
            continue;

        // New UVs — normalize from xatlas pixel space to [0,1] using xatlas dimensions
        FVector2f NewUV0(V0.uv[0] / XAtlasWidth, V0.uv[1] / XAtlasHeight);
        FVector2f NewUV1(V1.uv[0] / XAtlasWidth, V1.uv[1] / XAtlasHeight);
        FVector2f NewUV2(V2.uv[0] / XAtlasWidth, V2.uv[1] / XAtlasHeight);

        // Original UVs — trace back through xref to get old vertex indices
        uint32 OrigIdx0 = V0.xref;
        uint32 OrigIdx1 = V1.xref;
        uint32 OrigIdx2 = V2.xref;

        if (OrigIdx0 * 2 + 1 >= (uint32)OldUVs.Num() ||
            OrigIdx1 * 2 + 1 >= (uint32)OldUVs.Num() ||
            OrigIdx2 * 2 + 1 >= (uint32)OldUVs.Num())
            continue;

        FVector2f OldUV0(OldUVs[OrigIdx0 * 2], OldUVs[OrigIdx0 * 2 + 1]);
        FVector2f OldUV1(OldUVs[OrigIdx1 * 2], OldUVs[OrigIdx1 * 2 + 1]);
        FVector2f OldUV2(OldUVs[OrigIdx2 * 2], OldUVs[OrigIdx2 * 2 + 1]);

        // Rasterize the triangle in bake pixel space
        float MinX = FMath::Min3(NewUV0.X, NewUV1.X, NewUV2.X) * BakeWidth;
        float MaxX = FMath::Max3(NewUV0.X, NewUV1.X, NewUV2.X) * BakeWidth;
        float MinY = FMath::Min3(NewUV0.Y, NewUV1.Y, NewUV2.Y) * BakeHeight;
        float MaxY = FMath::Max3(NewUV0.Y, NewUV1.Y, NewUV2.Y) * BakeHeight;

        int32 PxMinX = FMath::Max(0, FMath::FloorToInt(MinX));
        int32 PxMaxX = FMath::Min(BakeWidth - 1, FMath::CeilToInt(MaxX));
        int32 PxMinY = FMath::Max(0, FMath::FloorToInt(MinY));
        int32 PxMaxY = FMath::Min(BakeHeight - 1, FMath::CeilToInt(MaxY));

        // Edge vectors for barycentric computation
        FVector2f E0 = NewUV1 - NewUV0;
        FVector2f E1 = NewUV2 - NewUV0;
        float Denom = E0.X * E1.Y - E0.Y * E1.X;
        if (FMath::Abs(Denom) < 1e-10f) continue;
        float InvDenom = 1.0f / Denom;

        for (int32 PY = PxMinY; PY <= PxMaxY; ++PY)
        {
            for (int32 PX = PxMinX; PX <= PxMaxX; ++PX)
            {
                FVector2f P((PX + 0.5f) / BakeWidth, (PY + 0.5f) / BakeHeight);
                FVector2f D = P - NewUV0;

                float U = (D.X * E1.Y - D.Y * E1.X) * InvDenom;
                float V = (E0.X * D.Y - E0.Y * D.X) * InvDenom;

                if (U < -0.001f || V < -0.001f || (U + V) > 1.002f)
                    continue;

                // Interpolate old UVs using barycentric coordinates
                FVector2f SampleUV = OldUV0 * (1.0f - U - V) + OldUV1 * U + OldUV2 * V;

                // Sample source texture
                FLinearColor Color = SampleBilinear(SrcPixels, SrcWidth, SrcHeight, SampleUV.X, SampleUV.Y);

                // Write to atlas (RGBA order)
                int32 PixIdx = (PY * BakeWidth + PX) * 4;
                OutPixels[PixIdx + 0] = FMath::Clamp(FMath::RoundToInt(Color.R * 255.0f), 0, 255); // R
                OutPixels[PixIdx + 1] = FMath::Clamp(FMath::RoundToInt(Color.G * 255.0f), 0, 255); // G
                OutPixels[PixIdx + 2] = FMath::Clamp(FMath::RoundToInt(Color.B * 255.0f), 0, 255); // B
                OutPixels[PixIdx + 3] = 255; // A

                Mask[PY * BakeWidth + PX] = 1;
            }
        }
    }

    // ---- Edge padding / dilation ----
    // Smear filled pixels outward into empty neighbors to prevent UV seam bleed.
    // Each iteration expands the border by 1 pixel in all 8 directions.
    if (DilationIterations > 0)
    {
        TArray<uint8> TempPixels;
        TArray<uint8> TempMask;

        // 8-connected neighbor offsets
        static const int32 DX[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int32 DY[] = { -1, -1, -1, 0, 0, 1, 1, 1 };

        for (int32 Iter = 0; Iter < DilationIterations; ++Iter)
        {
            TempPixels = OutPixels;
            TempMask = Mask;

            for (int32 Y = 0; Y < BakeHeight; ++Y)
            {
                for (int32 X = 0; X < BakeWidth; ++X)
                {
                    int32 Idx = Y * BakeWidth + X;
                    if (Mask[Idx] != 0) continue; // Already filled

                    // Check 8 neighbors for filled pixels
                    int32 SumR = 0, SumG = 0, SumB = 0;
                    int32 FilledCount = 0;

                    for (int32 N = 0; N < 8; ++N)
                    {
                        int32 NX = X + DX[N];
                        int32 NY = Y + DY[N];
                        if (NX < 0 || NX >= BakeWidth || NY < 0 || NY >= BakeHeight) continue;

                        int32 NIdx = NY * BakeWidth + NX;
                        if (Mask[NIdx] == 0) continue;

                        int32 NPx = NIdx * 4;
                        SumR += OutPixels[NPx + 0]; // R
                        SumG += OutPixels[NPx + 1]; // G
                        SumB += OutPixels[NPx + 2]; // B
                        ++FilledCount;
                    }

                    if (FilledCount > 0)
                    {
                        int32 PxIdx = Idx * 4;
                        TempPixels[PxIdx + 0] = SumR / FilledCount;
                        TempPixels[PxIdx + 1] = SumG / FilledCount;
                        TempPixels[PxIdx + 2] = SumB / FilledCount;
                        TempPixels[PxIdx + 3] = 255;
                        TempMask[Idx] = 1;
                    }
                }
            }

            OutPixels = MoveTemp(TempPixels);
            Mask = MoveTemp(TempMask);
        }
    }

    // Set alpha to 255 on all pixels (some renderers expect fully opaque textures)
    for (int32 i = 0; i < TotalPixels; ++i)
    {
        OutPixels[i * 4 + 3] = 255;
    }

    return true;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

// xatlas progress callback — logs progress to Output Log (thread-safe).
// NOTE: FScopedSlowTask::EnterProgressFrame() is NOT thread-safe and must only
// be called from the game thread. xatlas fires this callback from internal
// worker threads, so we only use UE_LOG here.
static bool XAtlasProgressCallback(xatlas::ProgressCategory Category, int Progress, void* UserData)
{
    const TCHAR* CategoryName = TEXT("Processing");
    switch (Category)
    {
    case xatlas::ProgressCategory::AddMesh:            CategoryName = TEXT("Adding mesh"); break;
    case xatlas::ProgressCategory::ComputeCharts:      CategoryName = TEXT("Computing UV charts"); break;
    case xatlas::ProgressCategory::PackCharts:          CategoryName = TEXT("Packing UV charts"); break;
    case xatlas::ProgressCategory::BuildOutputMeshes:   CategoryName = TEXT("Building output mesh"); break;
    default: break;
    }

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: xatlas %s — %d%%"), CategoryName, Progress);
    return true; // Return false to cancel
}

bool ViewGenUVRepack::RepackUVs(ViewGenGLB::FGLBDocument& Doc, const FRepackOptions& Options)
{
    if (!Doc.bValid || !Doc.JSON.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: Invalid GLB document"));
        return false;
    }

    // Total progress: 10 (parse) + 30 (xatlas) + 30 (bake textures) + 20 (upres) + 10 (rebuild GLB)
    FScopedSlowTask SlowTask(100.0f, NSLOCTEXT("ViewGen", "UVRepack", "UV Repack: Initializing..."));
    SlowTask.MakeDialog(false, false); // Not cancellable for now

    // Force Slate to repaint after progress updates — without this, the dialog
    // never visually updates because heavy computation blocks the game thread.
    auto FlushSlate = []()
    {
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().Tick();
        }
    };

    FlushSlate(); // Render the initial dialog

    // ---- Step 1: Find the first mesh primitive with position + UV + indices ----
    SlowTask.EnterProgressFrame(5.0f, NSLOCTEXT("ViewGen", "UVRepackParse", "UV Repack: Reading mesh data..."));
    FlushSlate();
    const TArray<TSharedPtr<FJsonValue>>* MeshesArr;
    if (!Doc.JSON->TryGetArrayField(TEXT("meshes"), MeshesArr) || MeshesArr->Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: No meshes in GLB"));
        return false;
    }

    // Process first mesh, first primitive
    const TSharedPtr<FJsonObject>& MeshObj = (*MeshesArr)[0]->AsObject();
    const TArray<TSharedPtr<FJsonValue>>* PrimitivesArr;
    if (!MeshObj->TryGetArrayField(TEXT("primitives"), PrimitivesArr) || PrimitivesArr->Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: No primitives in mesh"));
        return false;
    }

    const TSharedPtr<FJsonObject>& Prim = (*PrimitivesArr)[0]->AsObject();
    const TSharedPtr<FJsonObject>* AttrsObj;
    if (!Prim->TryGetObjectField(TEXT("attributes"), AttrsObj))
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: No attributes in primitive"));
        return false;
    }

    // Get accessor indices
    int32 PosAccessor = -1, NormAccessor = -1, UVAccessor = -1, IdxAccessor = -1;
    (*AttrsObj)->TryGetNumberField(TEXT("POSITION"), PosAccessor);
    (*AttrsObj)->TryGetNumberField(TEXT("NORMAL"), NormAccessor);
    (*AttrsObj)->TryGetNumberField(TEXT("TEXCOORD_0"), UVAccessor);
    Prim->TryGetNumberField(TEXT("indices"), IdxAccessor);

    if (PosAccessor < 0 || UVAccessor < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Missing POSITION or TEXCOORD_0 accessor"));
        return false;
    }

    // ---- Step 2: Read mesh data ----
    TArray<float> Positions, Normals, OldUVs;
    TArray<uint32> Indices;
    int32 PosCount = 0, PosComp = 0;
    int32 NormCount = 0, NormComp = 0;
    int32 UVCount = 0, UVComp = 0;

    if (!ReadAccessorFloat(Doc.JSON, Doc.BinaryChunk, PosAccessor, Positions, PosCount, PosComp))
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: Failed to read position data"));
        return false;
    }
    if (!ReadAccessorFloat(Doc.JSON, Doc.BinaryChunk, UVAccessor, OldUVs, UVCount, UVComp))
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: Failed to read UV data"));
        return false;
    }
    if (NormAccessor >= 0)
    {
        ReadAccessorFloat(Doc.JSON, Doc.BinaryChunk, NormAccessor, Normals, NormCount, NormComp);
    }
    if (IdxAccessor >= 0)
    {
        if (!ReadIndices(Doc.JSON, Doc.BinaryChunk, IdxAccessor, Indices))
        {
            UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: Failed to read index data"));
            return false;
        }
    }
    else
    {
        // Non-indexed geometry — generate sequential indices
        Indices.SetNumUninitialized(PosCount);
        for (int32 i = 0; i < PosCount; ++i) Indices[i] = i;
    }

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Mesh has %d vertices, %d triangles, %d textures"),
        PosCount, Indices.Num() / 3, Doc.Textures.Num());

    SlowTask.EnterProgressFrame(5.0f, FText::Format(
        NSLOCTEXT("ViewGen", "UVRepackMeshInfo", "UV Repack: {0} vertices, {1} triangles — generating atlas..."),
        FText::AsNumber(PosCount), FText::AsNumber(Indices.Num() / 3)));
    FlushSlate();

    // ---- Step 3: Run xatlas ----
    xatlas::Atlas* Atlas = xatlas::Create();

    xatlas::MeshDecl MeshDecl;
    MeshDecl.vertexPositionData = Positions.GetData();
    MeshDecl.vertexPositionStride = sizeof(float) * 3;
    MeshDecl.vertexCount = PosCount;

    if (Normals.Num() > 0)
    {
        MeshDecl.vertexNormalData = Normals.GetData();
        MeshDecl.vertexNormalStride = sizeof(float) * 3;
    }

    // Provide old UVs as hints
    MeshDecl.vertexUvData = OldUVs.GetData();
    MeshDecl.vertexUvStride = sizeof(float) * 2;

    MeshDecl.indexData = Indices.GetData();
    MeshDecl.indexCount = Indices.Num();
    MeshDecl.indexFormat = xatlas::IndexFormat::UInt32;

    xatlas::AddMeshError MeshErr = xatlas::AddMesh(Atlas, MeshDecl);
    if (MeshErr != xatlas::AddMeshError::Success)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: xatlas AddMesh failed: %s"),
            ANSI_TO_TCHAR(xatlas::StringForEnum(MeshErr)));
        xatlas::Destroy(Atlas);
        return false;
    }

    // Configure chart generation
    xatlas::ChartOptions ChartOpts;
    ChartOpts.maxIterations = 4; // Higher quality charts

    // Configure packing
    xatlas::PackOptions PackOpts;
    PackOpts.resolution = Options.AtlasResolution;
    PackOpts.padding = Options.ChartPadding;
    PackOpts.bilinear = true;
    PackOpts.bruteForce = false; // Speed vs quality tradeoff
    PackOpts.createImage = false;

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Running xatlas (resolution=%d, padding=%d)..."),
        Options.AtlasResolution, Options.ChartPadding);

    // Hook xatlas progress callback for logging (userData not used — thread-safe only)
    xatlas::SetProgressCallback(Atlas, XAtlasProgressCallback, nullptr);

    SlowTask.EnterProgressFrame(30.0f, NSLOCTEXT("ViewGen", "UVRepackXAtlas", "UV Repack: Running xatlas UV generation..."));
    FlushSlate();

    xatlas::Generate(Atlas, ChartOpts, PackOpts);

    if (Atlas->meshCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: xatlas produced no output meshes"));
        xatlas::Destroy(Atlas);
        return false;
    }

    const xatlas::Mesh& XMesh = Atlas->meshes[0];
    int32 XAtlasW = Atlas->width;   // xatlas-reported dimensions (may not be power-of-2)
    int32 XAtlasH = Atlas->height;

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: xatlas done — %d charts, atlas %dx%d, %d output verts, %d output tris"),
        Atlas->chartCount, XAtlasW, XAtlasH, XMesh.vertexCount, XMesh.indexCount / 3);

    if (XAtlasW == 0 || XAtlasH == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ViewGenUVRepack: xatlas produced zero-size atlas"));
        xatlas::Destroy(Atlas);
        return false;
    }

    // Force power-of-2 bake dimensions at base atlas resolution
    int32 BakeW = Options.AtlasResolution;
    int32 BakeH = Options.AtlasResolution;

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Baking at %dx%d (xatlas reported %dx%d)"),
        BakeW, BakeH, XAtlasW, XAtlasH);

    // ---- Step 4: Bake textures from old UVs to new UVs ----
    float BakeProgressPerTex = (Doc.Textures.Num() > 0) ? 30.0f / Doc.Textures.Num() : 30.0f;
    for (int32 TexIdx = 0; TexIdx < Doc.Textures.Num(); ++TexIdx)
    {
        ViewGenGLB::FTextureImage& TexImage = Doc.Textures[TexIdx];

        SlowTask.EnterProgressFrame(BakeProgressPerTex, FText::Format(
            NSLOCTEXT("ViewGen", "UVRepackBake", "UV Repack: Baking texture {0} of {1} ({2})..."),
            FText::AsNumber(TexIdx + 1), FText::AsNumber(Doc.Textures.Num()), FText::FromString(TexImage.Name)));
        FlushSlate();

        TArray<uint8> SrcPixels;
        int32 SrcW, SrcH;
        if (!DecodeImageToRGBA(TexImage.ImageData, TexImage.MimeType, SrcPixels, SrcW, SrcH))
        {
            UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Failed to decode texture '%s', skipping"), *TexImage.Name);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Baking texture '%s' (%dx%d -> %dx%d)..."),
            *TexImage.Name, SrcW, SrcH, BakeW, BakeH);

        TArray<uint8> BakedPixels;
        if (!BakeTexture(SrcPixels, SrcW, SrcH, OldUVs, TArray<float>(), Indices, XMesh, XAtlasW, XAtlasH, BakeW, BakeH, Options.DilationPixels, BakedPixels))
        {
            UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Failed to bake texture '%s'"), *TexImage.Name);
            continue;
        }

        // Encode to PNG and replace
        TArray<uint8> NewPNG;
        if (EncodePixelsToPNG(BakedPixels, BakeW, BakeH, NewPNG))
        {
            TexImage.ImageData = MoveTemp(NewPNG);
            TexImage.MimeType = TEXT("image/png");
            UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Baked texture '%s' -> %d bytes PNG"), *TexImage.Name, TexImage.ImageData.Num());
        }
    }

    // ---- Step 5: Upres baked textures (if scale > 1x) ----
    int32 FinalW = BakeW;
    int32 FinalH = BakeH;

    if (Options.UpresScale > 1)
    {
        FinalW = BakeW * Options.UpresScale;
        FinalH = BakeH * Options.UpresScale;

        float UpresProgressPerTex = (Doc.Textures.Num() > 0) ? 20.0f / Doc.Textures.Num() : 20.0f;

        UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Upscaling %d textures from %dx%d -> %dx%d (%dx)"),
            Doc.Textures.Num(), BakeW, BakeH, FinalW, FinalH, Options.UpresScale);

        for (int32 TexIdx = 0; TexIdx < Doc.Textures.Num(); ++TexIdx)
        {
            ViewGenGLB::FTextureImage& TexImage = Doc.Textures[TexIdx];

            SlowTask.EnterProgressFrame(UpresProgressPerTex, FText::Format(
                NSLOCTEXT("ViewGen", "UVRepackUpres", "UV Repack: Upscaling texture {0} of {1} ({2}) — {3}x..."),
                FText::AsNumber(TexIdx + 1), FText::AsNumber(Doc.Textures.Num()),
                FText::FromString(TexImage.Name), FText::AsNumber(Options.UpresScale)));
            FlushSlate();

            // Decode the baked PNG back to pixels
            TArray<uint8> BakedPixels;
            int32 BakedW, BakedH;
            if (!DecodeImageToRGBA(TexImage.ImageData, TexImage.MimeType, BakedPixels, BakedW, BakedH))
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Failed to decode baked texture '%s' for upres, skipping"), *TexImage.Name);
                continue;
            }

            // Bilinear upscale
            TArray<uint8> UpscaledPixels;
            if (!ResizeImageBilinear(BakedPixels, BakedW, BakedH, FinalW, FinalH, UpscaledPixels))
            {
                UE_LOG(LogTemp, Warning, TEXT("ViewGenUVRepack: Failed to upscale texture '%s'"), *TexImage.Name);
                continue;
            }

            // Re-encode to PNG at upscaled resolution
            TArray<uint8> UpscaledPNG;
            if (EncodePixelsToPNG(UpscaledPixels, FinalW, FinalH, UpscaledPNG))
            {
                TexImage.ImageData = MoveTemp(UpscaledPNG);
                TexImage.MimeType = TEXT("image/png");
                UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: Upscaled texture '%s' %dx%d -> %dx%d (%d bytes PNG)"),
                    *TexImage.Name, BakedW, BakedH, FinalW, FinalH, TexImage.ImageData.Num());
            }
        }
    }
    else
    {
        // Skip upres — consume the progress budget so the bar stays accurate
        SlowTask.EnterProgressFrame(20.0f, NSLOCTEXT("ViewGen", "UVRepackNoUpres", "UV Repack: Upres skipped (1x)..."));
        FlushSlate();
    }

    // ---- Step 6: Rebuild mesh + pack into GLB ----
    SlowTask.EnterProgressFrame(10.0f, NSLOCTEXT("ViewGen", "UVRepackRebuild", "UV Repack: Rebuilding GLB..."));
    FlushSlate();

    // Build new vertex arrays from xatlas output
    int32 NewVertCount = XMesh.vertexCount;
    TArray<float> NewPositions, NewNormals, NewUVData;
    NewPositions.SetNumUninitialized(NewVertCount * 3);
    NewUVData.SetNumUninitialized(NewVertCount * 2);
    if (Normals.Num() > 0) NewNormals.SetNumUninitialized(NewVertCount * 3);

    for (uint32 i = 0; i < XMesh.vertexCount; ++i)
    {
        const xatlas::Vertex& XV = XMesh.vertexArray[i];
        uint32 OldIdx = XV.xref;

        // Copy position from original vertex
        NewPositions[i * 3 + 0] = Positions[OldIdx * 3 + 0];
        NewPositions[i * 3 + 1] = Positions[OldIdx * 3 + 1];
        NewPositions[i * 3 + 2] = Positions[OldIdx * 3 + 2];

        // Copy normal from original vertex
        if (Normals.Num() > 0)
        {
            NewNormals[i * 3 + 0] = Normals[OldIdx * 3 + 0];
            NewNormals[i * 3 + 1] = Normals[OldIdx * 3 + 1];
            NewNormals[i * 3 + 2] = Normals[OldIdx * 3 + 2];
        }

        // New UVs — normalize from xatlas pixel space to [0,1]
        NewUVData[i * 2 + 0] = XV.uv[0] / XAtlasW;
        NewUVData[i * 2 + 1] = XV.uv[1] / XAtlasH;
    }

    // New indices
    TArray<uint32> NewIndices;
    NewIndices.SetNumUninitialized(XMesh.indexCount);
    FMemory::Memcpy(NewIndices.GetData(), XMesh.indexArray, XMesh.indexCount * sizeof(uint32));

    // Done with xatlas
    xatlas::Destroy(Atlas);

    // Rebuild BIN chunk with new mesh data + existing textures
    TArray<uint8> NewBin;
    TSharedPtr<FJsonObject> JSON = Doc.JSON;

    // We'll rebuild buffer views and accessors for the mesh data
    // Keep track of where we write each piece
    auto AppendAligned = [&NewBin](const void* Data, int32 Size) -> int32
    {
        int32 Offset = NewBin.Num();
        NewBin.Append(static_cast<const uint8*>(Data), Size);
        // Align to 4 bytes
        while (NewBin.Num() % 4 != 0) NewBin.Add(0);
        return Offset;
    };

    // Write position data
    int32 PosOffset = AppendAligned(NewPositions.GetData(), NewPositions.Num() * sizeof(float));
    int32 PosSize = NewPositions.Num() * sizeof(float);

    // Write normal data
    int32 NormOffset = -1, NormSize = 0;
    if (NewNormals.Num() > 0)
    {
        NormOffset = AppendAligned(NewNormals.GetData(), NewNormals.Num() * sizeof(float));
        NormSize = NewNormals.Num() * sizeof(float);
    }

    // Write UV data
    int32 UVOffset = AppendAligned(NewUVData.GetData(), NewUVData.Num() * sizeof(float));
    int32 UVSize = NewUVData.Num() * sizeof(float);

    // Write index data
    int32 IdxOffset = AppendAligned(NewIndices.GetData(), NewIndices.Num() * sizeof(uint32));
    int32 IdxSize = NewIndices.Num() * sizeof(uint32);

    // Copy texture image data
    TArray<TPair<int32, int32>> TexOffsets; // offset, size for each texture
    for (const auto& Tex : Doc.Textures)
    {
        int32 Off = AppendAligned(Tex.ImageData.GetData(), Tex.ImageData.Num());
        TexOffsets.Add(TPair<int32, int32>(Off, Tex.ImageData.Num()));
    }

    // ---- Step 7: Update JSON ----
    // Rebuild bufferViews array
    TArray<TSharedPtr<FJsonValue>> NewBVArray;
    int32 BVIdx = 0;

    auto MakeBV = [&](int32 Offset, int32 Size, int32 Target = 0) -> int32
    {
        TSharedPtr<FJsonObject> BV = MakeShareable(new FJsonObject);
        BV->SetNumberField(TEXT("buffer"), 0);
        BV->SetNumberField(TEXT("byteOffset"), Offset);
        BV->SetNumberField(TEXT("byteLength"), Size);
        if (Target > 0) BV->SetNumberField(TEXT("target"), Target);
        NewBVArray.Add(MakeShareable(new FJsonValueObject(BV)));
        return BVIdx++;
    };

    int32 PosBV = MakeBV(PosOffset, PosSize, 34962); // ARRAY_BUFFER
    int32 NormBV = (NormOffset >= 0) ? MakeBV(NormOffset, NormSize, 34962) : -1;
    int32 UVBV = MakeBV(UVOffset, UVSize, 34962);
    int32 IdxBV = MakeBV(IdxOffset, IdxSize, 34963); // ELEMENT_ARRAY_BUFFER

    // Texture buffer views
    TArray<int32> TexBVIndices;
    for (const auto& TexOff : TexOffsets)
    {
        TexBVIndices.Add(MakeBV(TexOff.Key, TexOff.Value));
    }

    JSON->SetArrayField(TEXT("bufferViews"), NewBVArray);

    // Rebuild accessors
    TArray<TSharedPtr<FJsonValue>> NewAccArray;
    int32 AccIdx = 0;

    auto MakeAccessor = [&](int32 BufView, int32 CompType, int32 Count, const FString& Type) -> int32
    {
        TSharedPtr<FJsonObject> Acc = MakeShareable(new FJsonObject);
        Acc->SetNumberField(TEXT("bufferView"), BufView);
        Acc->SetNumberField(TEXT("byteOffset"), 0);
        Acc->SetNumberField(TEXT("componentType"), CompType);
        Acc->SetNumberField(TEXT("count"), Count);
        Acc->SetStringField(TEXT("type"), Type);
        NewAccArray.Add(MakeShareable(new FJsonValueObject(Acc)));
        return AccIdx++;
    };

    int32 PosAcc = MakeAccessor(PosBV, CT_FLOAT, NewVertCount, TEXT("VEC3"));

    // Add min/max for position accessor (required by spec)
    {
        float MinPos[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
        float MaxPos[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (int32 i = 0; i < NewVertCount; ++i)
        {
            for (int32 c = 0; c < 3; ++c)
            {
                MinPos[c] = FMath::Min(MinPos[c], NewPositions[i * 3 + c]);
                MaxPos[c] = FMath::Max(MaxPos[c], NewPositions[i * 3 + c]);
            }
        }
        TSharedPtr<FJsonObject> PosAccObj = NewAccArray.Last()->AsObject();
        TArray<TSharedPtr<FJsonValue>> MinArr, MaxArr;
        for (int32 c = 0; c < 3; ++c)
        {
            MinArr.Add(MakeShareable(new FJsonValueNumber(MinPos[c])));
            MaxArr.Add(MakeShareable(new FJsonValueNumber(MaxPos[c])));
        }
        PosAccObj->SetArrayField(TEXT("min"), MinArr);
        PosAccObj->SetArrayField(TEXT("max"), MaxArr);
    }

    int32 NormAcc = (NormBV >= 0) ? MakeAccessor(NormBV, CT_FLOAT, NewVertCount, TEXT("VEC3")) : -1;
    int32 UVAcc = MakeAccessor(UVBV, CT_FLOAT, NewVertCount, TEXT("VEC2"));
    int32 IdxAcc = MakeAccessor(IdxBV, CT_UNSIGNED_INT, NewIndices.Num(), TEXT("SCALAR"));

    JSON->SetArrayField(TEXT("accessors"), NewAccArray);

    // Update primitive attributes
    TSharedPtr<FJsonObject> NewAttrs = MakeShareable(new FJsonObject);
    NewAttrs->SetNumberField(TEXT("POSITION"), PosAcc);
    if (NormAcc >= 0) NewAttrs->SetNumberField(TEXT("NORMAL"), NormAcc);
    NewAttrs->SetNumberField(TEXT("TEXCOORD_0"), UVAcc);

    // Get the primitive and update it
    TArray<TSharedPtr<FJsonValue>>* MutablePrims = const_cast<TArray<TSharedPtr<FJsonValue>>*>(PrimitivesArr);
    TSharedPtr<FJsonObject> MutablePrim = (*MutablePrims)[0]->AsObject();
    MutablePrim->SetObjectField(TEXT("attributes"), NewAttrs);
    MutablePrim->SetNumberField(TEXT("indices"), IdxAcc);

    // Update image buffer view references
    const TArray<TSharedPtr<FJsonValue>>* ImagesArr;
    if (JSON->TryGetArrayField(TEXT("images"), ImagesArr))
    {
        for (int32 i = 0; i < ImagesArr->Num() && i < TexBVIndices.Num(); ++i)
        {
            TSharedPtr<FJsonObject> ImgObj = (*ImagesArr)[i]->AsObject();
            ImgObj->SetNumberField(TEXT("bufferView"), TexBVIndices[i]);
            ImgObj->SetStringField(TEXT("mimeType"), TEXT("image/png"));
            // Update the texture's buffer view index in our document too
            if (i < Doc.Textures.Num())
            {
                Doc.Textures[i].BufferViewIndex = TexBVIndices[i];
            }
        }
    }

    // Update buffer total size
    const TArray<TSharedPtr<FJsonValue>>* BuffersArr;
    if (JSON->TryGetArrayField(TEXT("buffers"), BuffersArr) && BuffersArr->Num() > 0)
    {
        (*BuffersArr)[0]->AsObject()->SetNumberField(TEXT("byteLength"), NewBin.Num());
    }

    // Replace BIN chunk
    Doc.BinaryChunk = MoveTemp(NewBin);

    UE_LOG(LogTemp, Log, TEXT("ViewGenUVRepack: UV repack complete — %d verts, %d tris, %d textures at %dx%d (upres=%dx)"),
        NewVertCount, NewIndices.Num() / 3, Doc.Textures.Num(), FinalW, FinalH, Options.UpresScale);

    return true;
}
