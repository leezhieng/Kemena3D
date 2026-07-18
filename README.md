# Kemena3D SDK

Kemena3D is an open-source, cross-platform 3D rendering engine developed in C++. It's designed for more than just game development, it's also well-suited for simulations, interactive applications, architectural visualizations, and various other 3D use cases.

If you're looking for the game engine with complete editor - Kemena3D Studio, please visit the following page instead: https://github.com/leezhieng/kemena3d-studio

## Platforms & Renderers

| Platform | Renderers |
|----------|-----------|
| Windows | OpenGL 3.3/4.5, **DirectX 11** |
| Linux | OpenGL 3.3/4.5 |
| macOS | OpenGL 3.3/4.5 |
| Android | OpenGL ES 3.0 |

## Website

You can find the latest release, tutorials and additional information at: https://kemena3d.com

## Screenshots

![Phone Material on Mesh](Screenshots/phong_mesh.png)
![PBR Material with Shadow](Screenshots/pbr_shadow.jpg)
![Blur Screen Effect](Screenshots/blur.png)
![Edge Detection Screen Effect](Screenshots/edge.png)

## Scripting

Gameplay logic is scripted with [AngelScript](https://www.angelcode.com/angelscript/).
Scripts attach to scene objects, run standard lifecycle functions
(`Awake`, `Start`, `Update`, `FixedUpdate`, `LateUpdate`, `OnDestroy`), and are
compiled to bytecode for fast loading. A host API exposes objects, transforms,
timing and logging to scripts.

See [Documentation/Scripting.md](Documentation/Scripting.md) for the full guide.

## Asset Packaging

The SDK includes a built-in asset packaging system for game distribution. Assets are bundled into a single `.kpak` file with optional per-file compression (LZNT1 on Windows). The Virtual File System (`kFileSystem`) transparently handles both packaged and loose-file modes.

### Package Format (`.kpak`)

- **Binary format** with a header + file index + concatenated file data
- **O(1) random access** — any file can be read without decompressing the entire archive
- **Per-file LZNT1 compression** on Windows via `ntdll.dll` (zero external dependencies)
- **Automatic detection** at runtime — the engine finds `<exeName>.kpak`, `data.kpak`, or a `data/` folder

### Using the VFS

```cpp
#include <kemena/kfilesystem.h>

// Initialize (auto-detects .kpak vs data/ folder)
kFileSystem::init(exeDirectory, "data");

// Transparent file access
if (kFileSystem::fileExists("scene.world")) { ... }
auto data = kFileSystem::readFile("Library/ImportedAssets/asset.glb");
kString json = kFileSystem::readFileString("game.config");

// For APIs needing real OS paths (Assimp, etc.)
kString osPath = kFileSystem::resolveOSPath("Library/ImportedAssets/asset.glb");

// Shutdown
kFileSystem::shutdown();
```

See the [architecture documentation](.architecture.md#asset-packaging-system) for full details.

## Building

### Desktop (Windows/Linux/macOS)

```powershell
# 1. Download dependencies
python download_dep.py

# 2. Build SDK (interactive — select compiler, renderers, etc.)
python build_sdk.py
```

The script asks which renderers to include. On Windows you can choose OpenGL only or OpenGL + DirectX 11.

### Android (from Windows/Linux)

```powershell
# 1. Download & cross-compile dependencies for ARM64
python download_dep_android.py --ndk-path C:/android-ndk-r27 --abi arm64-v8a

# 2. Build SDK
python build_sdk_android.py --ndk-path C:/android-ndk-r27 --abi arm64-v8a
```

See [`.architecture.md`](.architecture.md) for more details on the project structure and build system.
