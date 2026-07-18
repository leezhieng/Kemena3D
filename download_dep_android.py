#!/usr/bin/env python3
"""
Kemena3D Android Dependency Downloader
======================================
Downloads and cross-compiles third-party dependencies for Android (ARM64/ARMv7)
using the Android NDK CMake toolchain.

Prerequisites:
  - Android NDK (r26+) installed
  - ANDROID_NDK environment variable set, or pass --ndk-path
  - CMake 3.22+, git, curl on PATH

Usage:
  python download_dep_android.py
  python download_dep_android.py --ndk-path C:/android-ndk-r27 --abi arm64-v8a
"""

import os
import stat
import sys
import shutil
import zipfile
import urllib.request
import subprocess
import argparse
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent
DEPS = ROOT / "Dependencies"
TEMP = DEPS / "temp_android"

# -- Dependency versions (mirrors download_dep.py) ---------------------------
ANGELSCRIPT_ZIP = "https://www.angelcode.com/angelscript/sdk/files/angelscript_2.37.0.zip"
SDL_GIT         = "https://github.com/libsdl-org/SDL.git"
SDL_BRANCH      = "release-3.2.16"
JOLT_ZIP        = "https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.3.0.zip"
RECAST_ZIP      = "https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.zip"
GLM_GIT         = "https://github.com/g-truc/glm.git"
GLM_TAG         = "1.0.1"
STB_GIT         = "https://github.com/nothings/stb.git"
STB_REV         = "f58f558c120e9b32c217290b80bad1a0729fbb2c"
NLOHMANN_GIT    = "https://github.com/nlohmann/json.git"
NLOHMANN_TAG    = "v3.12.0"
TINYGLTF_GIT    = "https://github.com/syoyo/tinygltf.git"
TINYGLTF_TAG    = "v3.0.0"
MINIAUDIO_GIT   = "https://github.com/mackron/miniaudio.git"
MINIAUDIO_TAG   = "0.11.25"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def remove_readonly(func, path, _):
    os.chmod(path, stat.S_IWRITE)
    func(path)

def rmtree(p: Path):
    if p.exists():
        shutil.rmtree(p, onerror=remove_readonly)

def die(msg, code=1):
    print(f"[ERROR] {msg}")
    sys.exit(code)

def run(cmd, cwd=None):
    print(f"[RUN] {cmd}")
    r = subprocess.run(cmd, shell=True, cwd=str(cwd) if cwd else None)
    if r.returncode != 0:
        die(f"Command failed: {cmd}")

def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)

def download_zip(url, out):
    print(f"[DOWNLOAD] {url}")
    subprocess.run(["curl", "-L", "-o", str(out), url], check=True)

def extract_zip(zip_path, dest):
    print(f"[EXTRACT] {zip_path.name} -> {dest}")
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(dest)

def download_and_extract_zip(name, url, dest: Path):
    if dest.exists():
        print(f"[SKIP] {name}: '{dest}' already exists.")
        return
    ensure_dir(TEMP)
    zip_path = TEMP / f"{dest.name}.zip"
    print(f"=== Downloading {name} ===")
    download_zip(url, zip_path)
    extract_zip(zip_path, TEMP)
    zip_path.unlink(missing_ok=True)
    ensure_dir(dest)
    # Find first dir in TEMP and move its contents
    top = next((e for e in TEMP.iterdir() if e.is_dir()), None)
    if not top:
        die(f"{name}: no folder inside archive")
    for item in top.iterdir():
        target = dest / item.name
        if item.is_dir():
            shutil.copytree(item, target, dirs_exist_ok=True)
        else:
            shutil.copy2(item, target)
    rmtree(TEMP)
    print(f"[OK] {name} -> {dest}")

def clone_git(name, repo, dest: Path, *, branch=None, revision=None):
    if dest.exists():
        print(f"[SKIP] {name}: '{dest}' already exists.")
        return
    print(f"=== Cloning {name} ===")
    ensure_dir(TEMP)
    args = ["git", "clone"]
    if branch:
        args += ["--branch", branch, "--single-branch"]
    args += [repo, str(TEMP)]
    run(" ".join(args))
    if revision:
        run(f"git checkout {revision}", cwd=TEMP)
    run("git submodule update --init --recursive", cwd=TEMP)
    shutil.copytree(str(TEMP), str(dest))
    rmtree(TEMP)
    print(f"[OK] {name} -> {dest}")

# ---------------------------------------------------------------------------
# Android NDK CMake wrapper
# ---------------------------------------------------------------------------
def cmake_android(srcdir: Path, build_dir: Path, abi: str, ndk: str,
                  extra_args: str = "", build_type: str = "Release"):
    """Configure and build a CMake project for Android."""
    toolchain = Path(ndk) / "build" / "cmake" / "android.toolchain.cmake"
    if not toolchain.exists():
        die(f"Android toolchain not found: {toolchain}")

    ensure_dir(build_dir)
    run(
        f'cmake -S "{srcdir}" -B "{build_dir}" '
        f'-DCMAKE_TOOLCHAIN_FILE="{toolchain}" '
        f'-DANDROID_ABI={abi} '
        f'-DANDROID_PLATFORM=android-21 '
        f'-DANDROID_STL=c++_static '
        f'-DCMAKE_BUILD_TYPE={build_type} '
        f'{extra_args}'
    )
    run(f'cmake --build "{build_dir}" --config {build_type} --parallel')

# ---------------------------------------------------------------------------
# Dependency builders
# ---------------------------------------------------------------------------
def build_angelscript(args):
    dest = DEPS / "angelscript"
    if (dest / "angelscript").exists():
        print("[SKIP] AngelScript already present.")
        return
    download_and_extract_zip("AngelScript", ANGELSCRIPT_ZIP, DEPS / "angelscript_temp")
    extracted = next((DEPS / "angelscript_temp").iterdir())
    if (extracted / "angelscript").exists():
        shutil.copytree(str(extracted), str(dest), dirs_exist_ok=True)
    else:
        shutil.copytree(str(extracted), str(dest / "angelscript"), dirs_exist_ok=True)
    rmtree(DEPS / "angelscript_temp")

    # Build AngelScript as static library for Android
    src = dest / "angelscript" / "projects" / "cmake"
    build_dir = dest / f"build_{args.abi}"
    cmake_android(src, build_dir, args.abi, args.ndk_path)

def build_sdl(args):
    dest = DEPS / "sdl"
    clone_git("SDL3", SDL_GIT, dest, branch=SDL_BRANCH)
    build_dir = dest / f"build_{args.abi}"
    cmake_android(dest, build_dir, args.abi, args.ndk_path,
                  extra_args="-DSDL_STATIC=ON -DSDL_SHARED=OFF")

def build_jolt(args):
    dest = DEPS / "jolt"
    download_and_extract_zip("Jolt Physics", JOLT_ZIP, dest)
    build_dir = dest / "Build" / f"build_{args.abi}"
    cmake_android(dest, build_dir, args.abi, args.ndk_path,
                  extra_args="-DCMAKE_BUILD_TYPE=Release")

def build_recast(args):
    dest = DEPS / "recast"
    download_and_extract_zip("RecastNavigation", RECAST_ZIP, dest)
    build_dir = dest / f"build_{args.abi}"
    cmake_android(dest, build_dir, args.abi, args.ndk_path,
                  extra_args="-DRECASTNAVIGATION_DEMO=OFF -DRECASTNAVIGATION_TESTS=OFF -DRECASTNAVIGATION_STATIC=ON")

def fetch_header_only():
    """Download header-only libraries (no compilation needed)."""
    for name, repo, branch, rev in [
        ("glm",       GLM_GIT,       None, GLM_TAG),
        ("stb",       STB_GIT,       None, STB_REV),
        ("tinygltf",  TINYGLTF_GIT,  None, TINYGLTF_TAG),
        ("miniaudio", MINIAUDIO_GIT, None, MINIAUDIO_TAG),
    ]:
        dest = DEPS / name
        clone_git(name, repo, dest, branch=branch, revision=rev)

    # nlohmann/json has a different layout
    dest = DEPS / "nlohmann"
    if not dest.exists():
        clone_git("nlohmann/json", NLOHMANN_GIT, TEMP / "nlohmann", revision=NLOHMANN_TAG)
        json_inc = TEMP / "nlohmann" / "include" / "nlohmann"
        ensure_dir(DEPS / "nlohmann" / "include" / "nlohmann")
        if json_inc.exists():
            for f in json_inc.iterdir():
                shutil.copy2(f, DEPS / "nlohmann" / "include" / "nlohmann" / f.name)
        rmtree(TEMP / "nlohmann")

def ensure_tools():
    for tool in ("git", "cmake", "curl"):
        if shutil.which(tool) is None:
            die(f"'{tool}' not found in PATH.")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Download and cross-compile Kemena3D Android dependencies")
    parser.add_argument("--ndk-path", default=os.environ.get("ANDROID_NDK", ""),
                        help="Path to Android NDK (default: $ANDROID_NDK)")
    parser.add_argument("--abi", default="arm64-v8a",
                        choices=["arm64-v8a", "armeabi-v7a", "x86_64", "x86"],
                        help="Android ABI to build for (default: arm64-v8a)")
    args = parser.parse_args()

    if not args.ndk_path or not Path(args.ndk_path).exists():
        die("Android NDK not found. Set ANDROID_NDK environment variable or use --ndk-path.")

    ensure_tools()
    ensure_dir(DEPS)
    print(f"=== Kemena3D Android Dependency Setup ===")
    print(f"  NDK  : {args.ndk_path}")
    print(f"  ABI  : {args.abi}")
    print(f"  Deps : {DEPS}")

    # 1. Header-only libs (fast, no build)
    print("\n--- Header-only libraries ---")
    fetch_header_only()

    # 2. Build AngelScript
    print("\n--- AngelScript ---")
    build_angelscript(args)

    # 3. Build SDL3
    print("\n--- SDL3 ---")
    build_sdl(args)

    # 4. Build Jolt Physics
    print("\n--- Jolt Physics ---")
    build_jolt(args)

    # 5. Build Recast/Detour
    print("\n--- Recast Navigation ---")
    build_recast(args)

    print("\n=== All Android dependencies ready. Run build_sdk_android.py next. ===")

if __name__ == "__main__":
    main()
