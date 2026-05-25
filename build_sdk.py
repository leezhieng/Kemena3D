import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

MINGW_SEARCH_PATHS = [
    r"C:\mingw64\bin",
    r"C:\mingw32\bin",
    r"C:\mingw\bin",
    r"C:\msys64\mingw64\bin",
    r"C:\msys64\mingw32\bin",
    r"C:\msys64\usr\bin",
    r"C:\msys2\mingw64\bin",
    r"C:\msys2\mingw32\bin",
]

def find_mingw_make():
    """Return the path to mingw32-make or make, searching PATH then known install dirs."""
    for name in ("mingw32-make", "make"):
        found = shutil.which(name)
        if found:
            return found
    for directory in MINGW_SEARCH_PATHS:
        for name in ("mingw32-make.exe", "make.exe"):
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate):
                return candidate
    raise RuntimeError(
        "Could not find mingw32-make or make. "
        "Ensure MinGW bin directory is on PATH or install MinGW to a standard location."
    )

def run_cmd(cmd, cwd=None):
    print(f"[RUN] {cmd}")
    result = subprocess.run(cmd, shell=True, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(f"[ERROR] Command failed: {cmd}")

def banner():
    print(r"""
  _  __   ___   __  __    ___    _  _     ___     ____    ___
 | |/ /  | __| |  \/  |  | __|  | \| |   /   \   |__ /   |   \
 | ' <   | _|  | |\/| |  | _|   | .` |   | - |    |_ \   | |) |
 |_|\_\  |___| |_|  |_|  |___|  |_|\_|   |_|_|   |___/   |___/
                        www.kemena3d.com
 ------------------------------------------------------------------------
 Automatically compile Kemena3D SDK...
 ------------------------------------------------------------------------
""")

def choose(prompt, options: dict, env=None):
    # Non-interactive override: if the given environment variable holds a valid
    # option key, use it without prompting (used by CI).
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
        raise ValueError(f"Invalid choice: {choice}")
    return choice

def build_with_cmake(generator, build_mode, args, make_program=None):
    build_dir = f"build_{build_mode}"
    install_prefix = os.path.join(os.getcwd(), f"Output/{build_mode}")

    make_arg = f'-DCMAKE_MAKE_PROGRAM="{make_program}" ' if make_program else ""

    # Configure
    run_cmd(
        f'cmake -S . -B {build_dir} -G "{generator}" '
        f'{make_arg}'
        f'-DCMAKE_INSTALL_PREFIX="{install_prefix}" '
        f'-DCMAKE_BUILD_TYPE={build_mode} {args}'
    )

    # Build
    run_cmd(f'cmake --build {build_dir} --config {build_mode} --parallel')

    # Install
    run_cmd(f'cmake --install {build_dir} --config {build_mode}')

    print("[SUCCESS] Kemena3D SDK built and installed successfully.")

def main():
    banner()
    system = platform.system()

    # Compiler selection
    if system == "Windows":
        compiler = choose(
            "\nPlease choose a compiler:",
            {
                "1": "Build with Visual Studio 2022 (Community Edition)",
                "2": "Build with MinGW (GCC 14 or above)"
            },
            env="KEMENA_COMPILER"
        )
    elif system == "Linux":
        print("\nLinux detected → using GCC (default).")
        compiler = "1"
    elif system == "FreeBSD":
        print("\nFreeBSD detected → using GCC (default).")
        compiler = "1"
    elif system == "Darwin":  # macOS
        compiler = choose(
            "\nPlease choose a compiler:",
            {
                "1": "Xcode (Clang/LLVM from Command Line Tools)",
                "2": "GCC (via Homebrew or custom install)"
            },
            env="KEMENA_COMPILER"
        )
    else:
        print(f"Unsupported platform: {system}")
        sys.exit(1)

    # Linking selection
    linking = choose(
        "\nPlease choose static linking or dynamic linking:",
        {"1": "Static linking (library built into executable)",
         "2": "Dynamic linking (DLL / shared library)"},
        env="KEMENA_LINKING"
    )

    # Build configuration selection
    config = choose(
        "\nPlease choose a build configuration:",
        {
            "1": "Debug",
            "2": "Release",
            "3": "Both (Debug and Release)"
        },
        env="KEMENA_CONFIG"
    )

    # CMake generator setup
    make_program = None
    if system == "Windows":
        if compiler == "1":
            generator = "Visual Studio 17 2022"
        elif compiler == "2":
            generator = "MinGW Makefiles"
            make_program = find_mingw_make()
            print(f"[INFO] Using make program: {make_program}")
    elif system in ("Linux", "FreeBSD"):
        generator = "Unix Makefiles"
    elif system == "Darwin":
        generator = "Xcode" if compiler == "1" else "Unix Makefiles"
    else:
        raise RuntimeError(f"No CMake generator for {system}/{compiler}")

    # Build args
    if system == "Windows":
        if compiler == "1":  # Visual Studio
            if linking == "1":
                args = "-DBUILD_SHARED_LIBS=OFF -DUSE_MINGW=OFF"
            else:
                args = "-DBUILD_SHARED_LIBS=ON -DUSE_MINGW=OFF -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON -DKEMENA_SHARED=ON"
        else:  # MinGW
            if linking == "1":
                args = "-DBUILD_SHARED_LIBS=OFF -DUSE_MINGW=ON"
            else:
                args = "-DBUILD_SHARED_LIBS=ON -DUSE_MINGW=ON -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON -DKEMENA_SHARED=ON"

    elif system in ("Linux", "FreeBSD", "Darwin"):
        if linking == "1":
            args = "-DBUILD_SHARED_LIBS=OFF"
        else:
            args = "-DBUILD_SHARED_LIBS=ON -DKEMENA_SHARED=ON"
    else:
        raise RuntimeError(f"No build args defined for {system}")

    configs = []
    if config == "1":
        configs = ["Debug"]
    elif config == "2":
        configs = ["Release"]
    else:
        configs = ["Debug", "Release"]

    for cfg in configs:
        build_with_cmake(generator, cfg, args, make_program)

    print("\n------------------------------------------------------------------------")
    print("Kemena3D SDK has been compiled successfully.")
    print("------------------------------------------------------------------------")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("\n------------------------------------------------------------------------")
        print(f"Failed to compile Kemena3D SDK: {e}")
        print("------------------------------------------------------------------------")
        sys.exit(1)
