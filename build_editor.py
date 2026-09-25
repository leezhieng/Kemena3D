import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

# Map compiler choice → CMake generator name
COMPILER_GENERATOR = {
    "1": "Visual Studio 18 2026",
    "2": "Visual Studio 17 2022",
    "3": "MinGW Makefiles",
}

SCRIPT_DIR = Path(__file__).resolve().parent

DEFAULT_SDK_DIR = "D:/Projects/Kemena3D/Kemena3D-SDK/Output"

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
 Automatically compile Kemena3D Editor...
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

def _generator_in_cache(cache_file, vs_generator):
    """Return True if the CMake cache was generated with the requested generator."""
    if not cache_file.exists():
        return False
    content = cache_file.read_text(errors="ignore")
    return f"CMAKE_GENERATOR:INTERNAL={vs_generator}" in content

def is_multi_config(generator):
    """Return True for generators that select the config at build time (VS, Xcode)."""
    return generator.startswith("Visual Studio") or generator == "Xcode"

def rebuild_jolt_md(sdk_source_dir, modes, vs_generator="Visual Studio 18 2026"):
    """Rebuild JoltPhysics with /MD (MultiThreadedDLL) to match the Kemena3D SDK CRT."""
    jolt_cmake = Path(sdk_source_dir) / "Dependencies/jolt/Build"
    if not jolt_cmake.exists():
        print(f"[WARN] Jolt source not found at {jolt_cmake}, skipping Jolt rebuild.")
        return
    for mode in modes:
        build_dir = jolt_cmake / f"build_{mode}"
        jolt_lib = build_dir / mode / "Jolt.lib"
        cache_file = build_dir / "CMakeCache.txt"

        # Only remove if the generator changed or the output lib is missing
        if build_dir.exists():
            stale = not _generator_in_cache(cache_file, vs_generator) or not jolt_lib.exists()
            if stale:
                reason = "generator changed" if not _generator_in_cache(cache_file, vs_generator) else "lib missing"
                print(f"[INFO] Removing stale Jolt build directory ({reason}): {build_dir}")
                shutil.rmtree(str(build_dir))
            else:
                print(f"[INFO] Jolt ({mode}) already built with /MD CRT, skipping.")
                continue

        print(f"\n[INFO] Rebuilding Jolt ({mode}) with /MD CRT...")
        # NOTE: vs_generator is always multi-config, so CMAKE_BUILD_TYPE is set at
        # build time via --config and must NOT be passed at configure time.
        run_cmd(
            f'cmake -S "{jolt_cmake}" -B "{build_dir}" '
            f'-G "{vs_generator}" '
            f'-DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF '
            f'-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL '
            f'-DTARGET_UNIT_TESTS=OFF '
            f'-DTARGET_HELLO_WORLD=OFF '
            f'-DTARGET_SAMPLES=OFF '
            f'-DTARGET_VIEWER=OFF '
            f'-DTARGET_PERFORMANCE_TEST=OFF '
            f'-DBUILD_SHARED_LIBS=OFF '
            f'-DENABLE_ALL_WARNINGS=OFF'
        )
        run_cmd(f'cmake --build "{build_dir}" --config {mode} --parallel')

def build_with_cmake(generator, build_mode, extra_args, sdk_dir, make_program=None):
    build_dir = SCRIPT_DIR / f"build_{build_mode.lower()}"

    make_arg = f'-DCMAKE_MAKE_PROGRAM="{make_program}" ' if make_program else ""

    # Only single-config generators (e.g. MinGW Makefiles) need CMAKE_BUILD_TYPE at
    # configure time; multi-config generators (Visual Studio) select it via --config.
    build_type_arg = "" if is_multi_config(generator) else f"-DCMAKE_BUILD_TYPE={build_mode} "

    # Configure
    run_cmd(
        f'cmake -S "{SCRIPT_DIR}" -B "{build_dir}" -G "{generator}" '
        f'{make_arg}'
        f'{build_type_arg}'
        f'-DKEMENA3D_SDK_DIR="{sdk_dir}" '
        f'{extra_args}'
    )

    # Build
    run_cmd(f'cmake --build "{build_dir}" --config {build_mode} --parallel')

    print(f"[SUCCESS] Kemena3D ({build_mode}) built successfully.")

def main():
    banner()
    system = platform.system()

    if system != "Windows":
        print(f"Unsupported platform: {system}")
        sys.exit(1)

    # Compiler selection
    compiler = choose(
        "\nPlease choose a compiler:",
        {
            "1": "Build with Visual Studio 2026 (Community Edition)",
            "2": "Build with Visual Studio 2022 (Community Edition)",
            "3": "Build with MinGW (GCC 14 or above)"
        }
    )

    # Static vs dynamic Kemena3D SDK library
    link_type = choose(
        "\nPlease choose the Kemena3D SDK library type:",
        {
            "1": "Static library",
            "2": "Dynamic library (DLL)"
        }
    )

    # Configuration selection
    config = choose(
        "\nPlease choose a build configuration:",
        {
            "1": "Debug",
            "2": "Release",
            "3": "Debug and Release"
        }
    )

    # SDK directory
    sdk_input = input(f"\nEnter Kemena3D SDK directory (leave blank for default):\n[{DEFAULT_SDK_DIR}]: ").strip()
    sdk_dir = sdk_input if sdk_input else DEFAULT_SDK_DIR

    # CMake generator and args
    link_static = "ON" if link_type == "1" else "OFF"
    make_program = None
    # NOTE: USE_MINGW is consumed only by the SDK's CMakeLists, not the Studio's,
    # so it must NOT be passed here or CMake reports it as an unused CLI variable.
    if compiler in ("1", "2"):
        generator = COMPILER_GENERATOR.get(compiler, "Visual Studio 18 2026")
        extra_args = f"-DKEMENA3D_LINK_STATIC={link_static}"
    else:
        generator = "MinGW Makefiles"
        make_program = find_mingw_make()
        print(f"[INFO] Using make program: {make_program}")
        extra_args = f"-DKEMENA3D_LINK_STATIC={link_static}"

    # Build modes
    if config == "1":
        modes = ["Debug"]
    elif config == "2":
        modes = ["Release"]
    else:
        modes = ["Debug", "Release"]

    # MSVC: ensure Jolt is built with /MD to match the Kemena3D SDK CRT
    if compiler in ("1", "2"):
        sdk_source_dir = str(Path(sdk_dir).parent)
        rebuild_jolt_md(sdk_source_dir, modes, generator)

    for mode in modes:
        build_with_cmake(generator, mode, extra_args, sdk_dir, make_program)

    print("\n------------------------------------------------------------------------")
    print("Kemena3D has been compiled successfully.")
    print("------------------------------------------------------------------------")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("\n------------------------------------------------------------------------")
        print(f"Failed to compile Kemena3D: {e}")
        print("------------------------------------------------------------------------")
        sys.exit(1)
