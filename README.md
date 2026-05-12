# ViewGen — AI Viewport Generator for Unreal Engine

An Unreal Engine 5.7 editor plugin that captures the viewport and depth buffer as inputs for generative AI workflows. Supports multiple AI backends (ComfyUI, Gemini, Kling, Meshy) with a streamlined tabbed interface, visual node-graph editor, LoRA support, and real-time preview.

![ViewGen Screenshot](Docs/screenshot_placeholder.png)

## Features

- **Viewport & Depth Capture** — Capture the editor viewport and engine depth buffer in one click, ready for img2img and ControlNet pipelines.
- **Multiple Generation Modes** — img2img (viewport + prompt), Depth + Prompt (ControlNet), Prompt Only (txt2img), Gemini (Nano Banana 2), and Kling (Image 3.0).
- **ComfyUI Integration** — Visual node-graph workflow editor that maps directly to ComfyUI API workflows. Ships with 30+ ready-to-use workflow templates. Auto-detects Flux models and switches to the correct split-loader path.
- **Gemini & Kling Support** — Direct integration with Google Gemini and Kling AI image generation, with mode-specific settings for model selection, aspect ratio, and image fidelity.
- **Meshy 3D Generation** — Text-to-3D and image-to-3D model generation via the Meshy API, with in-editor preview.
- **GLB Extract + Repack Pipeline** — Load AI-generated GLB models, extract textures for upscaling through ComfyUI workflows, and repack into a new GLB with replaced textures. Supports DIFFUSE, ORM (channel-packed Occlusion/Roughness/Metallic), and NORMAL map pins.
- **UV Repack (xatlas)** — Optional xatlas-powered UV atlas repacking for AI-generated 3D models. Generates clean UV layouts, bakes textures from old UVs to new UVs via software rasterization with edge-padding dilation to prevent seam artifacts. Includes a progress dialog with per-step feedback.
- **Segmentation Capture** — Object and material segmentation masks for targeted inpainting and compositing.
- **LoRA Support** — Select and apply LoRA models from your ComfyUI installation with adjustable strength.
- **Hi-Res Fix** — Optional upscaling pass for higher-resolution output.
- **Preset System** — Save and load generation presets including prompt, checkpoint, resolution, and all mode-specific settings.
- **Quick Render (Sequencer)** — One-click Movie Render Graph rendering of the active Level Sequence directly from the plugin panel.
- **StoryTools Menu** — Unified top-level editor menu integrating ViewGen with companion plugins (SceneBreak, Gaussian Splat Generator).

## UI Layout

ViewGen uses a tabbed interface with the viewport capture and results panel at the top, and three tabs below:

- **Basic** — Artist-friendly controls for prompt, generation mode, checkpoint/LoRA selection, resolution, steps, seed, and the Generate button. Mode-specific settings (Gemini, Kling) appear conditionally. An Advanced section provides access to depth/ControlNet, hi-res fix, defaults, and presets.
- **Graph Editor** — Full visual node-graph editor for building and editing ComfyUI workflows. Includes a node details panel and toolbar for direct graph-to-ComfyUI generation.
- **Connection** — Server configuration (ComfyUI URL, API keys, timeout, model paths) with Test Connection and Refresh Models buttons.

## Requirements

- Unreal Engine 5.7+
- Windows, Linux, or macOS
- A running ComfyUI instance (for ComfyUI generation modes)
- Google Gemini API key (optional, for Gemini generation)
- Kling API key (optional, for Kling generation)
- Meshy API key (optional, for 3D generation)

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   YourProject/
   └── Plugins/
       └── ViewGen/
           ├── Source/
           │   └── ViewGen/
           │       ├── Private/
           │       ├── Public/
           │       └── ThirdParty/
           │           ├── tinygltf/    (GLB/glTF parsing — MIT)
           │           └── xatlas/      (UV atlas generation — MIT)
           ├── Workflows/
           ├── ViewGen.uplugin
           └── ...
   ```
2. Regenerate project files (right-click your `.uproject` → Generate Visual Studio project files).
3. Build and launch the editor.
4. The **StoryTools** menu will appear in the main menu bar. Click **StoryTools → ViewGen** to open the panel.

## Configuration

1. Open the ViewGen panel via **StoryTools → ViewGen**.
2. Switch to the **Connection** tab and enter your ComfyUI server address (default: `http://127.0.0.1:8188`).
3. Click **Test Connection** to verify connectivity and populate model lists.
4. Optionally enter API keys for Gemini, Kling, or Meshy.
5. Switch to the **Basic** tab to select a generation mode, checkpoint, and start generating.

## Included Workflows

The `Workflows/` folder contains 30+ ComfyUI workflow templates covering common generative AI pipelines including img2img, ControlNet depth, vid2vid, seed dance, background removal, image-to-3D, and more. These are JSON files compatible with ComfyUI's API format.

## Companion Plugins

ViewGen is part of the **StoryTools** suite. These companion plugins are optional and have their own repositories:

- **SceneBreak** — AI-powered scene breakdown and asset discovery from reference images.
- **Gaussian Splat Generator** — Generate and import Gaussian splat point clouds from images.

## Third-Party Libraries

ViewGen includes the following MIT-licensed libraries, bundled in `Source/ViewGen/ThirdParty/`:

- **tinygltf** — Header-only glTF 2.0 / GLB loader/writer by Syoyo Fujita. Used for parsing and reconstructing GLB binary files in the Extract/Repack pipeline.
- **xatlas** — UV atlas generation library by Jonathan Young. Used for UV unwrapping and chart packing in the UV Repack feature.

Both libraries are compiled as part of the ViewGen module (no separate build step required). See their respective `LICENSE` files in the ThirdParty directories.

## Contributing

Contributions are welcome! Please open an issue to discuss proposed changes before submitting a pull request.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## Author

Creative Story Lab Inc
