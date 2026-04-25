# ViewGen — AI Viewport Generator for Unreal Engine

An Unreal Engine 5.7 editor plugin that captures the viewport and depth buffer as inputs for generative AI workflows. Connect to any REST-based AI backend (ComfyUI, Meshy, etc.) with LoRA support, real-time preview, and integrated rendering.

![ViewGen Screenshot](Docs/screenshot_placeholder.png)

## Features

- **Viewport & Depth Capture** — Capture the editor viewport and engine depth buffer in one click, ready for img2img and ControlNet pipelines.
- **ComfyUI Integration** — Visual node-graph workflow editor that maps directly to ComfyUI API workflows. Ships with 30+ ready-to-use workflow templates.
- **Meshy 3D Generation** — Text-to-3D and image-to-3D model generation via the Meshy API, with in-editor preview.
- **Segmentation Capture** — Object and material segmentation masks for targeted inpainting and compositing.
- **Quick Render (Sequencer)** — One-click Movie Render Graph rendering of the active Level Sequence directly from the plugin panel.
- **StoryTools Menu** — Unified top-level editor menu integrating ViewGen with companion plugins (SceneBreak, Gaussian Splat Generator).

## Requirements

- Unreal Engine 5.7+
- Windows, Linux, or macOS
- A running ComfyUI instance (for AI generation features)
- Meshy API key (optional, for 3D generation)

## Installation

1. Clone or download this repository into your project's `Plugins/` folder:
   ```
   YourProject/
   └── Plugins/
       └── ViewGen/
           ├── Source/
           ├── Workflows/
           ├── ViewGen.uplugin
           └── ...
   ```
2. Regenerate project files (right-click your `.uproject` → Generate Visual Studio project files).
3. Build and launch the editor.
4. The **StoryTools** menu will appear in the main menu bar. Click **StoryTools → ViewGen** to open the panel.

## Configuration

1. Open the ViewGen panel via **StoryTools → ViewGen**.
2. In the **Settings** section, enter your ComfyUI server address (default: `http://127.0.0.1:8188`).
3. Optionally enter your Meshy API key for 3D generation features.
4. Select a workflow template from the Workflows dropdown, or load a custom ComfyUI workflow JSON.

## Included Workflows

The `Workflows/` folder contains 30+ ComfyUI workflow templates covering common generative AI pipelines including img2img, ControlNet depth, vid2vid, seed dance, and more. These are JSON files compatible with ComfyUI's API format.

## Companion Plugins

ViewGen is part of the **StoryTools** suite. These companion plugins are optional and have their own repositories:

- **SceneBreak** — AI-powered scene breakdown and asset discovery from reference images.
- **Gaussian Splat Generator** — Generate and import Gaussian splat point clouds from images.

## Contributing

Contributions are welcome! Please open an issue to discuss proposed changes before submitting a pull request.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

## Author

Creative Story Lab Inc
