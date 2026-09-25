# Kemena3D

Kemena3D is an open-source, cross-platform 3D game engine built with C++, powered by the underlying Kemena3D SDK rendering engine.

If you're looking for the core 3D rendering engine - Kemena3D SDK, please visit the following page instead: https://github.com/leezhieng/Kemena3D-SDK

## Website

You can find the latest release, tutorials and additional information at: https://www.kemena3d.com

## Screenshots

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/07/kemena_material.png)

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/06/kemena_mesh.png)

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/04/kemena_screenshot.png)

![Editor Screenshot](https://kemena3d.com/site/wp-content/uploads/2026/04/kemena_screenshot_asset.png)

## Scripting

Gameplay is scripted with [AngelScript](https://www.angelcode.com/angelscript/),
authored either as text `.as` files or visually in the **Script Editor** node
graph. Attach a script to any object in the Inspector; pressing **Play**
compiles scripts to bytecode (under the project's `Library/Scripts` folder) and
runs their lifecycle functions. Use **Run → Build Scripts** to compile manually.

See the SDK's [Scripting guide](https://github.com/leezhieng/Kemena3D-SDK/blob/main/Documentation/Scripting.md)
for the host API and node-graph reference.

## Game Export & Asset Packaging

Kemena3D can export your project as a standalone distributable game.
The export process:

1. Compiles all scripts to bytecode
2. Saves the current world as `scene.world`
3. Stages all assets (`Library/ImportedAssets`, `Library/Shaders`, `Library/Scripts`)
4. **Packages everything into a single `<GameName>.kpak` file** with per-file LZNT1 compression
5. Copies the runtime executable and renames it to match your game name
6. Writes a `game.config` with window settings (title, resolution, fullscreen)

The `.kpak` format provides:
- **Single-file distribution** — no loose asset files
- **O(1) random access** — assets are streamed on demand without full-archive decompression
- **Automatic detection** — the runtime auto-finds the `.kpak` next to the executable

If packaging fails for any reason, the editor falls back to copying loose files into a `data/` folder (compatible with the runtime's `data/` folder detection).
