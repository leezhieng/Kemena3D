# How to Build the Dependencies

You can find the tutorial at: https://kemena3d.com/manual

# Why Do This Separately?

Since each library may have different licensing terms, we do **not** include their source files directly in our repository. Instead, we provide a convenient batch script to **automatically download and build all required dependencies** into the project.

# Download Dependencies on Different Development OS

To avoid conflicts, it's best **not to run the batch script with different compilers** in the same project directory without a clean setup.

> For example:  
> If you build using **Visual Studio 2022**, avoid re-running the script with **MinGW**. This may result in **mixed configuration files** and broken builds.

### Recommended:

If you need to build Kemena3D using multiple compilers:

- Clone the entire Kemena3D repository into separate folders for each build environment.
- Alternatively, if switching compilers, **delete all folders inside the `Dependencies` directory** (e.g., `sdl`, `stb`, `glew`, etc.) before re-running the script.

## Windows

### 1. Visual Studio 2022 (Community Edition)

- Run: `download_dep.py`
- Choose **option 1** for compiler

### 2. MinGW (Minimalist GNU for Windows)

- Run: `download_dep.py`
- Choose **option 2** for compiler

## Linux

### 1. GCC

- Run: `download_dep.py`

## macOS

### 1. Xcode (Clang/LLVM from Command Line Tools)

- Run: `download_dep.py`
- Choose **option 1** for compiler

### 2. GCC (via Homebrew or custom install)

- Run: `download_dep.py`
- Choose **option 2** for compiler

# Dependency Information

The automated batch script performs its tasks using the configurations below. If the script fails, you may manually perform the steps as described. Whenever possible, we prioritize using stable official releases. If an official release isn't available, we retrieve the code directly from the GitHub repository. We always targeting a specific version tag or revision instead of pulling the latest code to ensure consistency.

### SDL

- **Website**: [SDL GitHub](https://github.com/libsdl-org/SDL)
- **Version**: `3.2.16`
- **Git Clone**: `https://github.com/libsdl-org/SDL.git`
- **Extract to**: `sdl`
- **Build Required**: ✅ Yes

### GLM

- **Website**: [GLM GitHub](https://github.com/g-truc/glm)
- **Version**: `1.0.1`
- **Git Clone**: `https://github.com/g-truc/glm.git`
- **Extract to**: `glm`
- **Build Required**: ❌ No

### GLEW

- **Website**: [GLEW GitHub](https://github.com/nigels-com/glew)
- **Version**: `2.2.0`
- **Download**: [glew-2.2.0.tgz](https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz)
- **Extract to**: `glew`
- **Build Required**: ✅ Yes

### Dear Imgui

- **Website**: [Dear Imgui GitHub](https://github.com/ocornut/imgui.git)
- **Branch**: `docking`
- **Git Clone**: `https://github.com/ocornut/imgui.git`
- **Extract to**: `imgui`
- **Build Required**: ✅ Yes

### Assimp

- **Website**: [Assimp GitHub](https://github.com/assimp/assimp)
- **Version**: `6.0.2`
- **Git Clone**: `https://github.com/assimp/assimp.git`
- **Extract to**: `assimp`
- **Build Required**: ✅ Yes

### STB

- **Website**: [STB GitHub](https://github.com/nothings/stb)
- **Version**: `Rev. f58f558`
- **Git Clone**: `https://github.com/nothings/stb.git`
- **Extract to**: `stb`
- **Build Required**: ❌ No

### JSON for Modern C++ (nlohmann)

- **Website**: [nlohmann/json GitHub](https://github.com/nlohmann/json)
- **Version**: `3.12.0`
- **Git Clone**: `https://github.com/nlohmann/json.git`
- **Extract to**: `nlohmann`
- **Build Required**: ❌ No

### Jolt Physics

- **Website**: [JoltPhysics GitHub](https://github.com/jrouwe/JoltPhysics)
- **Version**: `5.3.0`
- **Download**: [v5.3.0.zip](https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.3.0.zip)
- **Extract to**: `jolt`
- **Build Required**: ✅ Yes

### Recast Navigation

- **Website**: [Recast GitHub](https://github.com/recastnavigation/recastnavigation)
- **Version**: `1.6.0`
- **Download**: [v1.6.0.zip](https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.zip)
- **Extract to**: `recast`
- **Build Required**: ✅ Yes

### Slang

- **Website**: [Slang GitHub](https://github.com/shader-slang/slang)
- **Version**: `2026.5.2`
- **Git Clone**: `https://github.com/shader-slang/slang.git`
- **Extract to**: `slang`
- **Build Required**: ✅ Yes

### miniaudio

- **Website**: [miniaudio GitHub](https://github.com/mackron/miniaudio)
- **Version**: `0.11.25`
- **Git Clone**: `https://github.com/mackron/miniaudio.git`
- **Extract to**: `miniaudio`
- **Build Required**: ✅ Yes

### Ogg Media Container

- **Website**: [Ogg GitHub](https://github.com/xiph/ogg)
- **Version**: `1.3.5`
- **Git Clone**: `https://github.com/xiph/ogg.git`
- **Extract to**: `ogg`
- **Build Required**: ✅ Yes

### Ogg Vorbis Audio Format

- **Website**: [Vorbis GitHub](https://github.com/xiph/vorbis)
- **Version**: `1.3.7`
- **Git Clone**: `https://github.com/xiph/vorbis.git`
- **Extract to**: `vorbis`
- **Build Required**: ✅ Yes

### Opus Audio Format

- **Website**: [Vorbis GitHub](https://github.com/xiph/opus)
- **Version**: `1.5.2`
- **Git Clone**: `https://github.com/xiph/opus.git`
- **Extract to**: `opus`
- **Build Required**: ✅ Yes

### Decoder for Opus Streams

- **Website**: [Vorbis GitHub](https://github.com/xiph/opusfile)
- **Version**: `0.12`
- **Git Clone**: `https://github.com/xiph/opusfile.git`
- **Extract to**: `opusfile`
- **Build Required**: ✅ Yes