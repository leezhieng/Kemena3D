#!/usr/bin/env python3
"""
Kemena3D Android SDK Builder
=============================
Cross-compiles the Kemena3D SDK for Android (ARM64/ARMv7) using the NDK.

Prerequisites:
  - Android NDK installed (set ANDROID_NDK or use --ndk-path)
  - Dependencies built via download_dep_android.py
  - CMake 3.22+

Usage:
  python build_sdk_android.py
  python build_sdk_android.py --ndk-path C:/android-ndk-r27 --abi arm64-v8a --config Release
"""

import os
import shutil
import subprocess
import sys
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DEPS = ROOT / "Dependencies"

MINGW_SEARCH_PATHS = [
    r"C:\mingw64\bin",
    r"C:\msys64\mingw64\bin",
    r"C:\msys64\usr\bin",
]

def find_mingw_make():
    for name in ("mingw32-make", "make"):
        found = shutil.which(name)
        if found:
            return found
    for d in MINGW_SEARCH_PATHS:
        for name in ("mingw32-make.exe", "make.exe"):
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return p
    return None  # Will use cmake --build (Ninja or default)

def run_cmd(cmd, cwd=None):
    print(f"[RUN] {cmd}")
    r = subprocess.run(cmd, shell=True, cwd=str(cwd) if cwd else None)
    if r.returncode != 0:
        sys.exit(r.returncode)

def banner():
    print(r"""
  _  __   ___   __  __    ___    _  _     ___     ____    ___
 | |/ /  | __| |  \/  |  | __|  | \| |   /   \   |__ /   |   \
 | ' <   | _|  | |\/| |  | _|   | .` |   | - |    |_ \   | |) |
 |_|\_\  |___| |_|  |_|  |___|  |_|\_|   |_|_|   |___/   |___/
                       www.kemena3d.com
 ------------------------------------------------------------------------
  Kemena3D Android SDK Builder
 ------------------------------------------------------------------------
""")

def main():
    banner()

    parser = argparse.ArgumentParser(description="Build Kemena3D SDK for Android")
    parser.add_argument("--ndk-path", default=os.environ.get("ANDROID_NDK", ""),
                        help="Path to Android NDK (default: $ANDROID_NDK)")
    parser.add_argument("--abi", default="arm64-v8a",
                        choices=["arm64-v8a", "armeabi-v7a", "x86_64", "x86"],
                        help="Android ABI (default: arm64-v8a)")
    parser.add_argument("--config", default="Release",
                        choices=["Debug", "Release"],
                        help="Build configuration (default: Release)")
    parser.add_argument("--api-level", default="21",
                        help="Minimum Android API level (default: 21)")
    args = parser.parse_args()

    ndk = args.ndk_path
    if not ndk or not Path(ndk).exists():
        print("[ERROR] Android NDK not found.")
        print("  Set ANDROID_NDK environment variable or use --ndk-path.")
        print("  Example: --ndk-path C:/Android/android-ndk-r27")
        sys.exit(1)

    toolchain = Path(ndk) / "build" / "cmake" / "android.toolchain.cmake"
    if not toolchain.exists():
        print(f"[ERROR] NDK toolchain not found: {toolchain}")
        sys.exit(1)

    abi        = args.abi
    config     = args.config
    api_level  = args.api_level
    build_dir  = ROOT / f"build_android_{abi}_{config}"
    install_dir = ROOT / f"Output/Android_{abi}/{config}"

    # Build flags
    cmake_args = (
        f'-DCMAKE_TOOLCHAIN_FILE="{toolchain}" '
        f'-DANDROID_ABI={abi} '
        f'-DANDROID_PLATFORM=android-{api_level} '
        f'-DANDROID_STL=c++_static '
        f'-DCMAKE_BUILD_TYPE={config} '
        f'-DBUILD_SHARED_LIBS=OFF '
        f'-DKEMENA_GLES=ON '
        f'-DKEMENA_USE_ASSIMP=OFF '
        f'-DKEMENA_D3D11=OFF '
        f'-DCMAKE_INSTALL_PREFIX="{install_dir}" '
    )

    # If MinGW make is available, use it as the build tool
    make_prog = find_mingw_make()
    if make_prog:
        cmake_args += f'-DCMAKE_MAKE_PROGRAM="{make_prog}" '

    # Locate prebuilt dependency paths
    # These follow the pattern set by download_dep_android.py
    sdl_dir       = DEPS / "sdl"
    angelscript_dir = DEPS / "angelscript"
    jolt_dir      = DEPS / "jolt"
    recast_dir    = DEPS / "recast"

    # SDL3 include
    if sdl_dir.exists():
        cmake_args += f'-DSDL3_DIR="{sdl_dir / f"build_{abi}"}" '

    print(f"\nConfiguration:")
    print(f"  NDK      : {ndk}")
    print(f"  ABI      : {abi}")
    print(f"  API Level: {api_level}")
    print(f"  Config   : {config}")
    print(f"  Build Dir: {build_dir}")
    print(f"  Install  : {install_dir}")

    # Configure
    print("\n--- Configuring ---")
    run_cmd(
        f'cmake -S "{ROOT}" -B "{build_dir}" '
        f'{cmake_args}'
    )

    # Build
    print("\n--- Building ---")
    run_cmd(f'cmake --build "{build_dir}" --config {config} --parallel')

    # Install
    print("\n--- Installing ---")
    run_cmd(f'cmake --install "{build_dir}" --config {config}')

    print(f"\n{'='*70}")
    print(f"  Kemena3D Android SDK built successfully!")
    print(f"  Output: {install_dir}")
    print(f"{'='*70}")

if __name__ == "__main__":
    main()
