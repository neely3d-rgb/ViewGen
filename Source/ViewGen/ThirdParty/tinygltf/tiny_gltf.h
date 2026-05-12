// tiny_gltf.h - PLACEHOLDER
//
// This file is intentionally left as a placeholder. The ViewGen plugin uses
// ViewGenGLB.h/cpp instead, which provides a minimal GLB parser using Unreal
// Engine APIs (FJsonObject, FFileHelper, etc.) rather than the full tinygltf
// library.
//
// The full tinygltf library (https://github.com/syoyo/tinygltf) is not used
// because:
//   1. It depends on stb_image.h and stb_image_write.h (UE has its own STB)
//   2. It uses std:: containers (TArray/FString are preferred in UE)
//   3. We only need GLB binary parsing, not the full glTF feature set
//
// If you need full glTF support in the future, download from:
//   https://raw.githubusercontent.com/syoyo/tinygltf/release/tiny_gltf.h
// and define:
//   TINYGLTF_NO_STB_IMAGE
//   TINYGLTF_NO_STB_IMAGE_WRITE
//   TINYGLTF_NO_EXTERNAL_IMAGE

#pragma once
#error "tiny_gltf.h is a placeholder. Use ViewGenGLB.h instead."
