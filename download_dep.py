#!/usr/bin/env python3
# Kemena3D Dependency Setup (pure standard library only)

import os
import stat
import sys
import shutil
import zipfile
import urllib.request
import subprocess
from pathlib import Path
import platform

system = platform.system()

# ------------------------------------------------------------------------
# Config / Paths
# ------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent / "Dependencies"
TEMP = ROOT / "temp"

# VS and MinGW defaults (edit if your paths differ)
VS2022_VCVARS = Path(os.environ.get("ProgramFiles", "")) / "Microsoft Visual Studio" / "2022" / "Community" / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"

if system == "Windows":
    GCC_PATH = Path(r"C:/msys64/mingw64/bin/gcc.exe")
    GPP_PATH = Path(r"C:/msys64/mingw64/bin/g++.exe")

elif system in ("Linux", "FreeBSD"):
    GCC_PATH = Path("/usr/bin/gcc")
    GPP_PATH = Path("/usr/bin/g++")

elif system == "Darwin":  # macOS
    # Default Clang installed with Xcode Command Line Tools
    GCC_PATH = Path("/usr/bin/clang")
    GPP_PATH = Path("/usr/bin/clang++")

else:
    raise RuntimeError(f"Unsupported platform: {system}")

# Versions / URLs
ANGELSCRIPT_ZIP = "https://www.angelcode.com/angelscript/sdk/files/angelscript_2.37.0.zip"
SDL_GIT = "https://github.com/libsdl-org/SDL.git"
SDL_BRANCH = "release-3.2.16"
IMGUI_GIT = "https://github.com/ocornut/imgui.git"
GLM_GIT = "https://github.com/g-truc/glm.git"
GLM_TAG = "1.0.1"
GLEW_SRC_ZIP = "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip"
ASSIMP_GIT = "https://github.com/assimp/assimp.git"
ASSIMP_TAG = "v6.0.2"
STB_GIT = "https://github.com/nothings/stb.git"
STB_REV = "f58f558c120e9b32c217290b80bad1a0729fbb2c"
NLOHMANN_GIT = "https://github.com/nlohmann/json.git"
NLOHMANN_TAG = "v3.12.0"
# tinygltf v3 — header-only glTF / GLB loader. Drives the slim runtime build
# that ships without Assimp (see KEMENA_USE_ASSIMP in the SDK CMakeLists).
TINYGLTF_GIT = "https://github.com/syoyo/tinygltf.git"
TINYGLTF_TAG = "v3.0.0"
PFD_GIT = "https://github.com/samhocevar/portable-file-dialogs.git"
PFD_TAG = "main"
IMGUIZMO_GIT = "https://github.com/CedricGuillemet/ImGuizmo.git"
IMGUIZMO_TAG = "master"
JOLT_ZIP = "https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.3.0.zip"
RECAST_ZIP = "https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.zip"
MINIAUDIO_GIT = "https://github.com/mackron/miniaudio.git"
MINIAUDIO_TAG = "0.11.25"

# ------------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------------
def remove_readonly(func, path, _):
    """Error handler for shutil.rmtree: clears read-only bit then retries."""
    os.chmod(path, stat.S_IWRITE)
    func(path)

def rmtree(path: Path):
    """shutil.rmtree wrapper that handles read-only files on Windows."""
    shutil.rmtree(path, onerror=remove_readonly)

def die(msg: str, code: int = 1):
    print(f"[ERROR] {msg}")
    sys.exit(code)

def run(cmd: str, cwd: Path | None = None, env: dict | None = None):
    print(f"[RUN] {cmd}")
    result = subprocess.run(cmd, shell=True, cwd=str(cwd) if cwd else None, env=env)
    if result.returncode != 0:
        die(f"Command failed: {cmd}")

def ensure_tools():
    for tool in ("git", "cmake"):
        if shutil.which(tool) is None:
            die(f"'{tool}' not found in PATH. Please install and/or add to PATH.")

def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)

def clean_temp():
    if TEMP.exists():
        rmtree(TEMP)
    ensure_dir(TEMP)

def download_zip(url, out_path):
    print(f"[DOWNLOAD] {url}")
    # Use curl (Windows 10+ has it by default)
    result = subprocess.run(["curl", "-L", "-o", str(out_path), url])
    if result.returncode != 0:
        raise RuntimeError(f"curl failed for {url}")

def extract_zip(zip_path: Path, dest_dir: Path):
    print(f"[EXTRACT] {zip_path.name} -> {dest_dir}")
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(dest_dir)

def download_and_extract_zip(name: str, version: str, url: str, destfolder: Path, flatten: bool = True):
    if destfolder.exists():
        print(f"[SKIP] {name}: '{destfolder}' already exists.")
        return
    clean_temp()
    zip_path = TEMP / f"{destfolder.name}.zip"
    print(f"=== Downloading {name} ({version}) ===")
    download_zip(url, zip_path)
    extract_zip(zip_path, TEMP)
    zip_path.unlink(missing_ok=True)
    ensure_dir(destfolder)

    # Flatten: move contents of the first extracted directory into destfolder
    if flatten:
        # Find first top-level dir in TEMP
        top = None
        for entry in TEMP.iterdir():
            if entry.is_dir():
                top = entry
                break
        if top is None:
            die(f"{name}: No folder found inside archive.")
        for item in top.iterdir():
            target = destfolder / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)
    else:
        # Move the first folder as-is
        top = None
        for entry in TEMP.iterdir():
            if entry.is_dir():
                top = entry
                break
        if top is None:
            die(f"{name}: No folder found inside archive.")
        shutil.move(str(top), str(destfolder))

    shutil.rmtree(TEMP)
    print(f"[OK] {name} setup complete at '{destfolder}'.")

def clone_git(name: str, version: str, repo: str, destfolder: Path, revision: str | None = None, branch: str | None = None):
    if destfolder.exists():
        print(f"[SKIP] {name}: '{destfolder}' already exists.")
        return
    print(f"=== Git Clone {name} ({version}) ===")
    clean_temp()
    args = ["git", "clone"]
    if branch:
        args += ["--branch", branch, "--single-branch"]
    args += [repo, str(TEMP)]
    run(" ".join(args))
    if revision:
        run(f"git checkout {revision}", cwd=TEMP)
    run("git submodule update --init --recursive", cwd=TEMP)
    shutil.copytree(str(TEMP), str(destfolder))
    rmtree(TEMP)
    print(f"[OK] {name} setup complete at '{destfolder}'.")

def is_msvc(generator: str) -> bool:
    return generator.startswith("Visual Studio")

def build_with_cmake(name: str, srcdir: Path, build_mode: str, args: str, generator: str):
    cmakelists = srcdir / "CMakeLists.txt"
    if not cmakelists.exists():
        die(f"{name}: CMakeLists.txt not found in '{srcdir}'.")
    print(f"[CMAKE] Building {name} ({build_mode}) with {generator}")
    build_dir = srcdir / f"build_{build_mode}"
    ensure_dir(build_dir)
    # Configure
    run(f'cmake -G "{generator}" -S . -B "{build_dir}" -DCMAKE_BUILD_TYPE={build_mode} {args}', cwd=srcdir)
    # Build
    if is_msvc(generator):
        run(f'cmake --build "{build_dir}" --config {build_mode}', cwd=srcdir)
    else:
        # MinGW (single-config); passing --config is harmless but we omit it
        run(f'cmake --build "{build_dir}"', cwd=srcdir)
    print(f"[OK] {name} built successfully ({build_mode}).")

def write_imgui_cmakelists(imgui_dir: Path, linking: str):
    add_lib = "add_library(imgui STATIC ${IMGUI_SRC})" if linking == "1" else "add_library(imgui SHARED ${IMGUI_SRC})"

    cmake_txt = f"""cmake_minimum_required(VERSION 3.10)
project(ImGuiLib LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
file(GLOB IMGUI_SRC
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
    imgui_tables.cpp
    backends/imgui_impl_sdl3.cpp
    backends/imgui_impl_opengl3.cpp
)
{add_lib}
target_include_directories(imgui PUBLIC ${{CMAKE_CURRENT_SOURCE_DIR}} ${{CMAKE_CURRENT_SOURCE_DIR}}/backends)
if(DEFINED SDL3_INCLUDE_DIR)
    target_include_directories(imgui PUBLIC ${{SDL3_INCLUDE_DIR}})
endif()
# Choose your OpenGL loader macro as needed. Keeping GLEW macro to match batch script.
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW)
"""
    (imgui_dir / "CMakeLists.txt").write_text(cmake_txt, encoding="utf-8")
    print(f"[OK] Generated ImGui CMakeLists.txt at '{imgui_dir / 'CMakeLists.txt'}'")

def write_imguizmo_cmakelists(imguizmo_dir: Path, imgui_dir: Path, linking: str):
    add_lib = "add_library(imguizmo STATIC ${IMGUI_SRC})" if linking == "1" else "add_library(imguizmo SHARED ${IMGUI_SRC})"

    cmake_txt = f"""cmake_minimum_required(VERSION 3.10)
project(ImGuiLib LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
file(GLOB IMGUI_SRC
    GraphEditor.cpp
    ImCurveEdit.cpp
    ImGradient.cpp
    ImGuizmo.cpp
    ImSequencer.cpp
)
{add_lib}
target_include_directories(imguizmo PUBLIC ${{CMAKE_CURRENT_SOURCE_DIR}} {imgui_dir.as_posix()})
"""
    (imguizmo_dir / "CMakeLists.txt").write_text(cmake_txt, encoding="utf-8")
    print(f"[OK] Generated ImGuizmo CMakeLists.txt at '{imguizmo_dir / 'CMakeLists.txt'}'")

# ------------------------------------------------------------------------
# ImGuizmo post-clone source patch
# ------------------------------------------------------------------------
# Applied right after ImGuizmo is fetched and before it is compiled, so the
# built library already renders scale handles as squares instead of circles.
# Idempotent — a sentinel marker line short-circuits re-runs.

# Sentinel comment written into the patched file so re-runs detect prior work
# without re-scanning every OLD/NEW snippet.
IMGUIZMO_PATCH_MARKER = "// KEMENA3D_PATCH: scale handles rendered as rectangles"

# Each tuple is (old_snippet, new_snippet). The script does a literal find,
# so the OLD text must match upstream byte-for-byte (whitespace included).
IMGUIZMO_PATCHES = [
    (
"""               if (gContext.mbUsing && (gContext.GetCurrentID() == gContext.mEditingID))
               {
                  ImU32 scaleLineColor = GetColorU32(SCALE_LINE);
                  drawList->AddLine(baseSSpace, worldDirSSpaceNoScale, scaleLineColor, gContext.mStyle.ScaleLineThickness);
                  drawList->AddCircleFilled(worldDirSSpaceNoScale, gContext.mStyle.ScaleLineCircleSize, scaleLineColor);
               }

               if (!hasTranslateOnAxis || gContext.mbUsing)
               {
                  drawList->AddLine(baseSSpace, worldDirSSpace, colors[i + 1], gContext.mStyle.ScaleLineThickness);
               }
               drawList->AddCircleFilled(worldDirSSpace, gContext.mStyle.ScaleLineCircleSize, colors[i + 1]);""",
"""               if (gContext.mbUsing && (gContext.GetCurrentID() == gContext.mEditingID))
               {
                  ImU32 scaleLineColor = GetColorU32(SCALE_LINE);
                  drawList->AddLine(baseSSpace, worldDirSSpaceNoScale, scaleLineColor, gContext.mStyle.ScaleLineThickness);
                  float hs = gContext.mStyle.ScaleLineCircleSize;
                  drawList->AddRectFilled(ImVec2(worldDirSSpaceNoScale.x - hs, worldDirSSpaceNoScale.y - hs),
                                          ImVec2(worldDirSSpaceNoScale.x + hs, worldDirSSpaceNoScale.y + hs),
                                          scaleLineColor);
               }

               if (!hasTranslateOnAxis || gContext.mbUsing)
               {
                  drawList->AddLine(baseSSpace, worldDirSSpace, colors[i + 1], gContext.mStyle.ScaleLineThickness);
               }
               {
                  float hs = gContext.mStyle.ScaleLineCircleSize;
                  drawList->AddRectFilled(ImVec2(worldDirSSpace.x - hs, worldDirSSpace.y - hs),
                                          ImVec2(worldDirSSpace.x + hs, worldDirSSpace.y + hs),
                                          colors[i + 1]);
               }""",
    ),
    (
"""               drawList->AddCircleFilled(worldDirSSpace, 12.f, colors[i + 1]);
            }
         }
      }

      // draw screen cirle
      drawList->AddCircle(gContext.mScreenSquareCenter, 20.f, colors[0], 32, gContext.mStyle.CenterCircleSize);""",
"""               {
                  const float hs = 12.f;
                  drawList->AddRectFilled(ImVec2(worldDirSSpace.x - hs, worldDirSSpace.y - hs),
                                          ImVec2(worldDirSSpace.x + hs, worldDirSSpace.y + hs),
                                          colors[i + 1]);
               }
            }
         }
      }

      // draw screen cirle
      drawList->AddCircle(gContext.mScreenSquareCenter, 20.f, colors[0], 32, gContext.mStyle.CenterCircleSize);""",
    ),
]

def patch_imguizmo(imguizmo_dir: Path):
    """Patch ImGuizmo.cpp to draw scale handles as squares. Idempotent."""
    cpp = imguizmo_dir / "ImGuizmo.cpp"
    if not cpp.exists():
        die(f"ImGuizmo: source not found at '{cpp}' — cannot apply patch.")

    text = cpp.read_text(encoding="utf-8")
    if IMGUIZMO_PATCH_MARKER in text:
        print(f"[PATCH] ImGuizmo: already patched — skipping ({cpp.name})")
        return

    new_text = text
    for idx, (old, new) in enumerate(IMGUIZMO_PATCHES, start=1):
        if old not in new_text:
            die(
                f"ImGuizmo: patch #{idx} snippet not found in source. "
                "Upstream may have changed — update IMGUIZMO_PATCHES in download_dep.py."
            )
        new_text = new_text.replace(old, new, 1)

    # Inject a marker line near the top of the file so future runs short-circuit.
    new_text = f"{IMGUIZMO_PATCH_MARKER}\n" + new_text
    cpp.write_text(new_text, encoding="utf-8")
    print(f"[PATCH] ImGuizmo: scale handles patched to rectangles in {cpp.name}")

def choose(prompt: str, options: dict[str, str], env: str | None = None) -> str:
    # Non-interactive override for CI: if the env var holds a valid key, use it.
    if env:
        val = os.environ.get(env, "").strip()
        if val in options:
            print(f"{prompt}\n  [{env}={val}] (non-interactive)")
            return val
    print(prompt)
    for k, v in options.items():
        print(f"{k}: {v}")
    choice = input("Enter your choice: ").strip()
    if choice not in options:
        die(f"Invalid choice: {choice}")
    return choice

def print_banner():
    print(r"  _  __   ___   __  __    ___    _  _     ___     ____    ___   ")
    print(r" | |/ /  | __| |  \/  |  | __|  | \| |   /   \   |__ /   |   \  ")
    print(r" | ' <   | _|  | |\/| |  | _|   | .` |   | - |    |_ \   | |) | ")
    print(r" |_|\_\  |___| |_|  |_|  |___|  |_|\_|   |_|_|   |___/   |___/  ")
    print(r"                       www.kemena3d.com")
    print("------------------------------------------------------------------------")
    print("Automatically download and compile dependencies for Kemena3D...")
    print("------------------------------------------------------------------------\n")

# ------------------------------------------------------------------------
# Main
# ------------------------------------------------------------------------
def main():
    ensure_tools()
    print_banner()

    # Select compiler based on OS
    if system == "Windows":
        compiler = choose(
            "Please choose a compiler:",
            {
                "1": "Build with Visual Studio 2022 (Community Edition)",
                "2": "Build with MinGW (GCC 14 or above)"
            },
            env="KEMENA_COMPILER"
        )

    elif system == "Linux":
        print("Linux detected → using GCC (default).")
        compiler = "1"

    elif system == "Darwin":  # macOS
        compiler = choose(
            "Please choose a compiler:",
            {
                "1": "Xcode (Clang/LLVM from Command Line Tools)",
                "2": "GCC (via Homebrew or custom install)"
            },
            env="KEMENA_COMPILER"
        )

    elif system == "FreeBSD":
        print("FreeBSD detected → using GCC (default).")
        compiler = "1"

    else:
        print(f"Unsupported platform: {system}")
        exit(1)
    
    # Select static or dynamic linking
    linking = choose(
        "\nPlease choose static linking or dynamic linking:",
        {"1": "Static linking (library code built into executable)",
         "2": "Dynamic linking (library code built into DLL files)"},
        env="KEMENA_LINKING"
    )

    # Select supported model formats
    modelformat = choose(
        "\nPlease choose Assimp setting:",
        {"1": "All formats (read and write)",
         "2": "GLTF only (read only)"},
        env="KEMENA_ASSIMP"
    )

    # Select generator and common flags
    if system == "Windows":
        if compiler == "1":
            generator = "Visual Studio 17 2022"
        elif compiler == "2":
            generator = "MinGW Makefiles"

    elif system in ("Linux", "FreeBSD"):
        generator = "Unix Makefiles"

    elif system == "Darwin":  # macOS
        if compiler == "1":
            generator = "Xcode"
        elif compiler == "2":
            generator = "Unix Makefiles"

    if not generator:
        raise RuntimeError(f"No CMake generator for {system}/{compiler}")

    print(f"[INFO] Using generator: {generator}")

    # --------------------------------------------------------------------
    # AngelScript
    # --------------------------------------------------------------------
    download_and_extract_zip("AngelScript", "v2.37.0", ANGELSCRIPT_ZIP, ROOT / "angelscript", flatten=True)
    as_src = ROOT / "angelscript" / "angelscript" / "projects" / "cmake"
    # Force /MD for all configs so CRT is consistent across all static dependencies.
    # CMP0091=NEW is required for CMAKE_MSVC_RUNTIME_LIBRARY to take effect on older cmake_minimum_required.
    _md = "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    if compiler == "1":
        # VS
        if linking == "1":
            build_with_cmake("AngelScript", as_src, "Debug",  f"-DBUILD_SHARED_LIBS=OFF {_md}", generator)
            build_with_cmake("AngelScript", as_src, "Release",f"-DBUILD_SHARED_LIBS=OFF {_md}", generator)
        else:
            build_with_cmake("AngelScript", as_src, "Debug",  f"-DBUILD_SHARED_LIBS=ON {_md}", generator)
            build_with_cmake("AngelScript", as_src, "Release",f"-DBUILD_SHARED_LIBS=ON {_md}", generator)
    else:
        # MinGW
        if linking == "1":
            build_with_cmake("AngelScript", as_src, "Debug",  "-DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("AngelScript", as_src, "Release","-DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("AngelScript", as_src, "Debug",  "-DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("AngelScript", as_src, "Release","-DBUILD_SHARED_LIBS=ON", generator)

    # --------------------------------------------------------------------
    # SDL
    # --------------------------------------------------------------------
    clone_git("SDL", "v3.2.16", SDL_GIT, ROOT / "sdl", branch=SDL_BRANCH)
    if compiler == "1":
        if linking == "1":
            build_with_cmake("SDL", ROOT / "sdl", "Debug",  "-DSDL_STATIC=ON -DSDL_SHARED=OFF", generator)
            build_with_cmake("SDL", ROOT / "sdl", "Release","-DSDL_STATIC=ON -DSDL_SHARED=OFF", generator)
        else:
            build_with_cmake("SDL", ROOT / "sdl", "Debug",  "-DSDL_STATIC=OFF -DSDL_SHARED=ON", generator)
            build_with_cmake("SDL", ROOT / "sdl", "Release","-DSDL_STATIC=OFF -DSDL_SHARED=ON", generator)
    else:
        cc = f'-DCMAKE_C_COMPILER="{GCC_PATH}" -DCMAKE_CXX_COMPILER="{GPP_PATH}"'
        if linking == "1":
            build_with_cmake("SDL", ROOT / "sdl", "Debug",  f"{cc} -DSDL_STATIC=ON -DSDL_SHARED=OFF", generator)
            build_with_cmake("SDL", ROOT / "sdl", "Release",f"{cc} -DSDL_STATIC=ON -DSDL_SHARED=OFF", generator)
        else:
            build_with_cmake("SDL", ROOT / "sdl", "Debug",  f"{cc} -DSDL_STATIC=OFF -DSDL_SHARED=ON", generator)
            build_with_cmake("SDL", ROOT / "sdl", "Release",f"{cc} -DSDL_STATIC=OFF -DSDL_SHARED=ON", generator)

    # --------------------------------------------------------------------
    # ImGui (generate CMakeLists.txt and build)
    # --------------------------------------------------------------------
    clone_git("imgui", "docking branch", IMGUI_GIT, ROOT / "imgui", branch="docking")
    write_imgui_cmakelists(ROOT / "imgui", linking)
    sdl_include = ROOT / "sdl" / "include"
    sdl_include_arg = f'-DSDL3_INCLUDE_DIR="{sdl_include}"'
    _md_imgui = f"-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL {sdl_include_arg}"
    if compiler == "1":
        if linking == "1":
            build_with_cmake("imgui", ROOT / "imgui", "Debug",  f"-DBUILD_SHARED_LIBS=OFF {_md_imgui}", generator)
            build_with_cmake("imgui", ROOT / "imgui", "Release",f"-DBUILD_SHARED_LIBS=OFF {_md_imgui}", generator)
        else:
            build_with_cmake("imgui", ROOT / "imgui", "Debug",  f"-DBUILD_SHARED_LIBS=ON {_md_imgui}", generator)
            build_with_cmake("imgui", ROOT / "imgui", "Release",f"-DBUILD_SHARED_LIBS=ON {_md_imgui}", generator)
    else:
        if linking == "1":
            build_with_cmake("imgui", ROOT / "imgui", "Debug",  f"-DBUILD_SHARED_LIBS=OFF {sdl_include_arg}", generator)
            build_with_cmake("imgui", ROOT / "imgui", "Release",f"-DBUILD_SHARED_LIBS=OFF {sdl_include_arg}", generator)
        else:
            build_with_cmake("imgui", ROOT / "imgui", "Debug",  f"-DBUILD_SHARED_LIBS=ON {sdl_include_arg}", generator)
            build_with_cmake("imgui", ROOT / "imgui", "Release",f"-DBUILD_SHARED_LIBS=ON {sdl_include_arg}", generator)

    # --------------------------------------------------------------------
    # GLM (header-only)
    # --------------------------------------------------------------------
    clone_git("GLM", "v1.0.1", GLM_GIT, ROOT / "glm", revision=GLM_TAG)

    # --------------------------------------------------------------------
    # GLEW (built from source)
    # --------------------------------------------------------------------
    download_and_extract_zip("GLEW", "v2.2.0", GLEW_SRC_ZIP, ROOT / "glew", flatten=True)
    glew_src = ROOT / "glew" / "build" / "cmake"
    if compiler == "1":
        if linking == "1":
            build_with_cmake("GLEW", glew_src, "Debug",   "-DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("GLEW", glew_src, "Release", "-DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("GLEW", glew_src, "Debug",   "-DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("GLEW", glew_src, "Release", "-DBUILD_SHARED_LIBS=ON", generator)
    else:
        cc = f'-DCMAKE_C_COMPILER="{GCC_PATH}" -DCMAKE_CXX_COMPILER="{GPP_PATH}"'
        if linking == "1":
            build_with_cmake("GLEW", glew_src, "Debug",   f"{cc} -DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("GLEW", glew_src, "Release", f"{cc} -DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("GLEW", glew_src, "Debug",   f"{cc} -DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("GLEW", glew_src, "Release", f"{cc} -DBUILD_SHARED_LIBS=ON", generator)

    # --------------------------------------------------------------------
    # Assimp
    # --------------------------------------------------------------------
    clone_git("Assimp", "v6.0.2", ASSIMP_GIT, ROOT / "assimp", revision=ASSIMP_TAG)

    # Patch: remove /D_DEBUG from assimp debug flags so objects don't reference
    # debug CRT imports (_CrtDbgReport) when building with /MD (MultiThreadedDLL).
    _assimp_cmake = ROOT / "assimp" / "CMakeLists.txt"
    _assimp_src = _assimp_cmake.read_text(encoding="utf-8")
    _assimp_patched = _assimp_src.replace(
        'SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_DEBUG /Zi /Od")',
        'SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")',
    )
    if _assimp_patched != _assimp_src:
        _assimp_cmake.write_text(_assimp_patched, encoding="utf-8")
        print("[PATCH] Assimp: removed /D_DEBUG from CMAKE_CXX_FLAGS_DEBUG")

    if modelformat == "1":
        model_flags = "-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=ON -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=ON"
    else:
        model_flags = (
            "-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF "
            "-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF "
            "-DASSIMP_BUILD_GLTF_IMPORTER=ON "
            "-DASSIMP_BUILD_GLTF_EXPORTER=OFF "
            "-DASSIMP_NO_EXPORT=ON "
            "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF "
            "-DASSIMP_BUILD_TESTS=OFF "
            "-DASSIMP_BUILD_SAMPLES=OFF "
            "-DASSIMP_BUILD_FBX_IMPORTER=OFF "
            "-DASSIMP_BUILD_OBJ_IMPORTER=OFF "
            "-DASSIMP_BUILD_COLLADA_IMPORTER=OFF"
        )

    if compiler == "1":
        if linking == "1":
            # Force /MD so CRT is consistent across all static dependencies.
            flags = f"-DASSIMP_BUILD_ZLIB=ON -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL {model_flags}"
            build_with_cmake("Assimp", ROOT / "assimp", "Debug",  flags, generator)
            build_with_cmake("Assimp", ROOT / "assimp", "Release",flags, generator)
        else:
            flags = f"-DASSIMP_BUILD_ZLIB=ON -DBUILD_SHARED_LIBS=ON -DASSIMP_BUILD_TESTS=OFF {model_flags}"
            build_with_cmake("Assimp", ROOT / "assimp", "Debug",  flags, generator)
            build_with_cmake("Assimp", ROOT / "assimp", "Release",flags, generator)
    else:
        cc = f'-DCMAKE_C_COMPILER="{GCC_PATH}" -DCMAKE_CXX_COMPILER="{GPP_PATH}"'
        if linking == "1":
            flags = f'{cc} -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF {model_flags}'
            build_with_cmake("Assimp", ROOT / "assimp", "Debug",  flags, generator)
            flags_rel = (f'{cc} -DCMAKE_CXX_FLAGS_RELEASE="-Wno-array-bounds -Wno-alloc-size-larger-than" '
                         f'-Wno-error=array-compare -Wno-error=class-memaccess '
                         f'-DASSIMP_BUILD_ZLIB=ON -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF {model_flags}')
            build_with_cmake("Assimp", ROOT / "assimp", "Release", flags_rel, generator)
        else:
            flags = f'{cc} -DBUILD_SHARED_LIBS=ON -DASSIMP_BUILD_TESTS=OFF {model_flags}'
            build_with_cmake("Assimp", ROOT / "assimp", "Debug",  flags, generator)
            flags_rel = (f'{cc} -DCMAKE_CXX_FLAGS_RELEASE="-Wno-array-bounds -Wno-alloc-size-larger-than" '
                         f'-Wno-error=array-compare -Wno-error=class-memaccess '
                         f'-DASSIMP_BUILD_ZLIB=ON -DBUILD_SHARED_LIBS=ON -DASSIMP_BUILD_TESTS=OFF {model_flags}')
            build_with_cmake("Assimp", ROOT / "assimp", "Release", flags_rel, generator)

    # --------------------------------------------------------------------
    # STB (header-only)
    # --------------------------------------------------------------------
    clone_git("STB", "Rev. f58f558", STB_GIT, ROOT / "stb", revision=STB_REV)

    # --------------------------------------------------------------------
    # nlohmann/json (header-only library with CMake package)
    # --------------------------------------------------------------------
    clone_git("nlohmann JSON", "v3.12.0", NLOHMANN_GIT, ROOT / "nlohmann", revision=NLOHMANN_TAG)

    # --------------------------------------------------------------------
    # tinygltf (header-only) — slim-runtime mesh importer.
    # Only `tiny_gltf.h` is consumed by the SDK; the repo also carries
    # examples / build files we don't touch. Reuses nlohmann + stb that we
    # already vendor, so no extra deps are pulled in.
    # --------------------------------------------------------------------
    clone_git("tinygltf", TINYGLTF_TAG, TINYGLTF_GIT, ROOT / "tinygltf",
              revision=TINYGLTF_TAG)

    # --------------------------------------------------------------------
    # Portable File Dialogs (header-only)
    # --------------------------------------------------------------------
    clone_git("Portable File Dialogs", "main", PFD_GIT, ROOT / "portable-file-dialogs", revision=PFD_TAG)
    
    # --------------------------------------------------------------------
    # ImGuizmo (header-only)
    # --------------------------------------------------------------------
    clone_git("imGuizmo", "master", IMGUIZMO_GIT, ROOT / "imguizmo", revision=IMGUIZMO_TAG)
    # Apply Kemena3D's source patch (square scale handles) right after fetching
    # and before compiling, so the built library already reflects the change.
    patch_imguizmo(ROOT / "imguizmo")
    write_imguizmo_cmakelists(ROOT / "imguizmo", ROOT / "imgui", linking)
    _md_gizmo = "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    if compiler == "1":
        if linking == "1":
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Debug",  f"-DBUILD_SHARED_LIBS=OFF {_md_gizmo}", generator)
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Release",f"-DBUILD_SHARED_LIBS=OFF {_md_gizmo}", generator)
        else:
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Debug",  f"-DBUILD_SHARED_LIBS=ON {_md_gizmo}", generator)
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Release",f"-DBUILD_SHARED_LIBS=ON {_md_gizmo}", generator)
    else:
        if linking == "1":
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Debug",  f"-DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Release",f"-DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Debug",  f"-DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("imGuizmo", ROOT / "imguizmo", "Release",f"-DBUILD_SHARED_LIBS=ON", generator)

    # --------------------------------------------------------------------
    # JoltPhysics (always static — no shared build option)
    # --------------------------------------------------------------------
    download_and_extract_zip("JoltPhysics", "v5.3.0", JOLT_ZIP, ROOT / "jolt", flatten=True)

    # Patch: guard _DEBUG define so Jolt objects don't reference debug CRT imports
    # (_CrtDbgReport) when building with /MD (MultiThreadedDLL).
    _jolt_cmake = ROOT / "jolt" / "Jolt" / "Jolt.cmake"
    _jolt_src = _jolt_cmake.read_text(encoding="utf-8")
    _jolt_old = 'target_compile_definitions(Jolt PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")'
    _jolt_new = (
        'if (NOT MSVC OR USE_STATIC_MSVC_RUNTIME_LIBRARY)\n'
        '\ttarget_compile_definitions(Jolt PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")\n'
        'endif()'
    )
    _jolt_patched = _jolt_src.replace(_jolt_old, _jolt_new, 1)
    if _jolt_patched != _jolt_src:
        _jolt_cmake.write_text(_jolt_patched, encoding="utf-8")
        print("[PATCH] Jolt: guarded _DEBUG define behind USE_STATIC_MSVC_RUNTIME_LIBRARY")

    jolt_flags = (
        "-DTARGET_UNIT_TESTS=OFF "
        "-DTARGET_HELLO_WORLD=OFF "
        "-DTARGET_SAMPLES=OFF "
        "-DTARGET_VIEWER=OFF "
        "-DTARGET_PERFORMANCE_TEST=OFF "
        "-DBUILD_SHARED_LIBS=OFF"
    )
    jolt_src = ROOT / "jolt" / "Build"
    if compiler == "1":
        # Force /MD (MultiThreadedDLL) so CRT is consistent across all static dependencies.
        # USE_STATIC_MSVC_RUNTIME_LIBRARY defaults ON in Jolt's CMakeLists and overrides to /MT;
        # set it OFF so our CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL is respected.
        jolt_flags_msvc = jolt_flags + " -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
        build_with_cmake("JoltPhysics", jolt_src, "Debug",   jolt_flags_msvc, generator)
        build_with_cmake("JoltPhysics", jolt_src, "Release", jolt_flags_msvc, generator)
    else:
        cc = f'-DCMAKE_C_COMPILER="{GCC_PATH}" -DCMAKE_CXX_COMPILER="{GPP_PATH}"'
        build_with_cmake("JoltPhysics", jolt_src, "Debug",   f"{cc} {jolt_flags}", generator)
        build_with_cmake("JoltPhysics", jolt_src, "Release", f"{cc} {jolt_flags}", generator)

    # --------------------------------------------------------------------
    # Recast Navigation
    # --------------------------------------------------------------------
    download_and_extract_zip("Recast Navigation", "v1.6.0", RECAST_ZIP, ROOT / "recast", flatten=True)
    recast_src   = ROOT / "recast"
    recast_flags = (
        "-DRECASTNAVIGATION_DEMO=OFF "
        "-DRECASTNAVIGATION_TESTS=OFF "
        "-DRECASTNAVIGATION_EXAMPLES=OFF "
        "-DBUILD_SHARED_LIBS=OFF "
        # Force /MD for CRT consistency; CMP0091=NEW enables CMAKE_MSVC_RUNTIME_LIBRARY.
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW "
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    )
    for cfg in ("Debug", "Release"):
        install_prefix = f'-DCMAKE_INSTALL_PREFIX="{recast_src / "dist"}"'
        if compiler == "1":
            build_with_cmake("Recast Navigation", recast_src, cfg,
                             f"{recast_flags} {install_prefix}", generator)
            run(f'cmake --install "{recast_src / f"build_{cfg}"}" --config {cfg}',
                cwd=recast_src)
        else:
            cc = f'-DCMAKE_C_COMPILER="{GCC_PATH}" -DCMAKE_CXX_COMPILER="{GPP_PATH}"'
            build_with_cmake("Recast Navigation", recast_src, cfg,
                             f"{cc} {recast_flags} {install_prefix}", generator)
            run(f'cmake --install "{recast_src / f"build_{cfg}"}"', cwd=recast_src)
        # Move installed libs into dist/lib/{cfg} for predictable CMakeLists.txt paths
        installed_lib = recast_src / "dist" / "lib"
        target_lib    = recast_src / "dist" / "lib" / cfg
        if installed_lib.exists() and not target_lib.exists():
            target_lib.mkdir(parents=True, exist_ok=True)
            for f in installed_lib.iterdir():
                if f.is_file():
                    shutil.move(str(f), str(target_lib / f.name))
    print("[OK] Recast Navigation built and installed to 'dist/'.")

    # --------------------------------------------------------------------
    # miniaudio
    # --------------------------------------------------------------------
    clone_git("miniaudio", "0.11.25", MINIAUDIO_GIT, ROOT / "miniaudio", revision=MINIAUDIO_TAG)
    if compiler == "1":
        if linking == "1":
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Debug",  f"-DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Release",f"-DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Debug",  f"-DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Release",f"-DBUILD_SHARED_LIBS=ON", generator)
    else:
        if linking == "1":
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Debug",  f"-DBUILD_SHARED_LIBS=OFF", generator)
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Release",f"-DBUILD_SHARED_LIBS=OFF", generator)
        else:
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Debug",  f"-DBUILD_SHARED_LIBS=ON", generator)
            build_with_cmake("miniaudio", ROOT / "miniaudio", "Release",f"-DBUILD_SHARED_LIBS=ON", generator)

    print("\n------------------------------------------------------------------------")
    print("All dependencies have been downloaded and compiled successfully.")
    print("------------------------------------------------------------------------")

if __name__ == "__main__":
    main()
