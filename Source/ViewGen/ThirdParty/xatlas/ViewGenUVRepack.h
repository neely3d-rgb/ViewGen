#pragma once

// ViewGenUVRepack.h — UV atlas repacking for GLB meshes using xatlas
// Unwraps mesh UVs into a clean atlas and bakes textures onto the new layout.

#include "CoreMinimal.h"
#include "ViewGenGLB.h"

namespace ViewGenUVRepack
{
    /** Options for UV repacking. */
    struct FRepackOptions
    {
        /** Target atlas resolution (width and height). Textures are baked to this size. */
        int32 AtlasResolution = 2048;

        /** Padding between UV charts in texels. Also determines dilation. */
        int32 ChartPadding = 8;

        /** Number of dilation (edge-bleed) iterations when baking textures.
         *  Each iteration expands chart edges by 1 pixel into empty space,
         *  preventing UV seam artifacts from bilinear filtering.
         *  Should be >= ChartPadding for best results. */
        int32 DilationPixels = 16;

        /** If true, use bilinear filtering when sampling the original texture during bake. */
        bool bBilinearSample = true;

        /** Upres multiplier applied after UV repack. 1 = original resolution,
         *  2 = 2x upscale (e.g. 4096 -> 8192), 4 = 4x upscale (e.g. 4096 -> 16384).
         *  Bake dimensions = AtlasResolution * UpresScale. */
        int32 UpresScale = 1;
    };

    /**
     * Re-UV a GLB mesh and bake textures onto the new UV layout.
     *
     * This function:
     *  1. Extracts vertex positions, normals, UVs, and indices from the GLB
     *  2. Runs xatlas to generate a clean UV atlas
     *  3. Bakes each texture (diffuse, normal, ORM, etc.) from old UVs to new UVs
     *  4. Writes the new UVs and baked textures back into the GLB document
     *
     * @param Doc      The loaded GLB document (modified in place)
     * @param Options  Repacking parameters
     * @return true on success
     */
    bool RepackUVs(ViewGenGLB::FGLBDocument& Doc, const FRepackOptions& Options = FRepackOptions());
}
