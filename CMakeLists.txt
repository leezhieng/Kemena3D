cmake_minimum_required(VERSION 3.16)
project(Kemena3DStudio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

# Path to the Kemena3D SDK output directory
# The default path below will be replaced by build_editor.py
set(KEMENA3D_SDK_DIR "C:/Kemena3D/" CACHE PATH "Path to Kemena3D SDK output directory")

# Kemena3D source directory – used to reference transitive deps not copied to the SDK output
get_filename_component(KEMENA3D_SOURCE_DIR "${KEMENA3D_SDK_DIR}" DIRECTORY)

# Whether to link against the static kemena3d library (set by build_editor.py)
option(KEMENA3D_LINK_STATIC "Link against the static kemena3d library" ON)

set(SOURCES
    main.cpp
    src/mainmenu.cpp
    src/commands.cpp
    src/manager.cpp
    src/panel_console.cpp
    src/panel_hierarchy.cpp
    src/panel_inspector.cpp
    src/panel_project.cpp
    src/panel_world.cpp
    src/panel_shadergraph.cpp
    src/panel_logicgraph.cpp
    src/panel_game.cpp
    src/panel_prefab.cpp
    src/panel_terrain.cpp
    src/panel_animator.cpp
    src/panel_animation.cpp
    src/panel_particle.cpp
    src/panel_gui.cpp
    src/splash_screen.cpp
    src/crashhandler.cpp
    src/util.cpp
    resources.rc
)

# Miniaudio custom decoder backends (libvorbis, libopus).
# Compiled directly so their symbols (ma_decoding_backend_libvorbis,
# ma_decoding_backend_libopus) are always available regardless of
# the SDK build state.  The audio codec libs (ogg, vorbis, opus, etc.)
# are already linked below.
set(MINIAUDIO_DECODER_SRC
    "${KEMENA3D_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libvorbis/miniaudio_libvorbis.c"
    "${KEMENA3D_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libopus/miniaudio_libopus.c"
)

add_executable(${PROJECT_NAME} ${SOURCES} ${MINIAUDIO_DECODER_SRC})

# ---------------------------------------------------------------------------
# DirectX 11 backend
#
# Set this to ON when the SDK the studio links was itself configured with
# -DKEMENA_D3D11=ON.  The two must agree: kGuiManager and the engine headers are
# compiled with KEMENA_D3D11, so the studio has to see the same definition to
# drive the matching ImGui backend, and it needs NOMINMAX/WIN32_LEAN_AND_MEAN
# before any Windows or d3d11 header is pulled in.
#
# With KEMENA_D3D11 defined, kemena::createRenderer() creates a D3D11 renderer
# and the studio loads the *_HLSL shader resources registered in resources.rc
# (the GLSL entries stay in use for every other configuration).
#
# Note: DXGI only supports one presented swap chain per HWND, while the studio
# creates several kRenderer instances on a single window; on D3D11 they share
# the HWND, so only the primary renderer's presentation is reliable.  This
# option is therefore opt-in and off by default.
# ---------------------------------------------------------------------------
option(KEMENA3D_USE_D3D11 "Build the studio against a DirectX 11 SDK (must match the SDK's KEMENA_D3D11)" OFF)

if(KEMENA3D_USE_D3D11 AND WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE KEMENA_D3D11 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi d3dcompiler)
    message(STATUS "Kemena3D Studio: DirectX 11 backend enabled")
endif()

# Release builds use the GUI subsystem (no console window); Debug keeps the console
# Use generator expression so this works with both single-config and multi-config generators
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE "$<$<CONFIG:Release>:TRUE>"
)

# Output to bin/Debug or bin/Release under the project root
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}"
)

# Include directories – SDK path varies by configuration
# assimp/config.h is generated into the assimp build tree, not copied to the SDK output
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    dep
    "$<$<CONFIG:Debug>:${KEMENA3D_SDK_DIR}/Debug/include>"
    "$<$<CONFIG:Release>:${KEMENA3D_SDK_DIR}/Release/include>"
    "$<$<CONFIG:Debug>:${KEMENA3D_SDK_DIR}/Debug/include/src>"
    "$<$<CONFIG:Release>:${KEMENA3D_SDK_DIR}/Release/include/src>"
    "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/assimp/build_Debug/include>"
    "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/assimp/build_Release/include>"
    # Miniaudio custom decoder headers (libvorbis, libopus)
    "${KEMENA3D_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libvorbis"
    "${KEMENA3D_SOURCE_DIR}/Dependencies/miniaudio/extras/decoders/libopus"
    # Audio codec headers (ogg, vorbis, opus, opusfile)
    "${KEMENA3D_SOURCE_DIR}/Dependencies/ogg/include"
    "${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/include"
    "${KEMENA3D_SOURCE_DIR}/Dependencies/opus/include"
    "${KEMENA3D_SOURCE_DIR}/Dependencies/opusfile/include"
)

# Preprocessor defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${KEMENA3D_LINK_STATIC}>:KEMENA3D_STATIC>
    $<$<BOOL:${KEMENA3D_LINK_STATIC}>:DKTX_STATIC>
    NOMINMAX
)

# assimp and Jolt are both built with /MD (MultiThreadedDLL); force /MD for the
# whole studio link so CRT records match across all static dependencies.
if(MSVC AND KEMENA3D_LINK_STATIC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Compiler flags – separated by toolchain
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        # Treat source and execution charsets as UTF-8. Without this MSVC assumes
        # the active code page (e.g. 936/GBK) and emits C4819 for UTF-8 files
        # containing non-ASCII characters (e.g. ©, –, emoji in comments).
        /utf-8
        $<$<CONFIG:Debug>:/Zi>
        # Emit debug info in Release too so the crash reporter can resolve
        # symbols in the no-console GUI build. /Zi does not affect /O2 codegen.
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Zi>
    )
    # WIN32_EXECUTABLE uses /SUBSYSTEM:WINDOWS which expects WinMain; override the
    # entry point so the standard int main() is used instead.
    target_link_options(${PROJECT_NAME} PRIVATE
        # Suppress LNK4099 ("PDB not found with ...") for prebuilt third-party
        # static libraries (assimp, Jolt, Recast, ...) whose .pdb files are not
        # shipped next to the .lib in the SDK output. The warning is benign and
        # only means those libs link without debug info.
        /IGNORE:4099
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
        # Generate a .pdb for Release (paired with /Zi) without bloating the exe.
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    # GCC / MinGW
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Release>:-O2>
    )
endif()

# Library search paths – SDK path varies by configuration
target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${KEMENA3D_SDK_DIR}/Debug/lib>"
    "$<$<CONFIG:Release>:${KEMENA3D_SDK_DIR}/Release/lib>"
)

# Linked libraries — common to both toolchains
target_link_libraries(${PROJECT_NAME} PRIVATE
    kemena3d
    opengl32
    winmm
    ole32
    setupapi
    gdi32
    imm32
    version
    oleaut32
    uuid
    dbghelp   # crash reporter stack walking (crashhandler.cpp)
)

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3-static
        $<$<CONFIG:Debug>:assimp-vc${MSVC_TOOLSET_VERSION}-mtd>
        $<$<CONFIG:Release>:assimp-vc${MSVC_TOOLSET_VERSION}-mt>
        $<$<CONFIG:Debug>:angelscriptd>
        $<$<CONFIG:Release>:angelscript>
        imgui
        imguizmo
        $<$<CONFIG:Debug>:libglew32d>
        $<$<CONFIG:Release>:libglew32>
        $<$<CONFIG:Debug>:zlibstaticd>
        $<$<CONFIG:Release>:zlibstatic>
        # Recast Navigation (MSVC names have -d suffix for Debug)
        $<$<CONFIG:Debug>:Recast-d>
        $<$<CONFIG:Debug>:Detour-d>
        $<$<CONFIG:Debug>:DetourCrowd-d>
        $<$<CONFIG:Debug>:DetourTileCache-d>
        $<$<CONFIG:Release>:Recast>
        $<$<CONFIG:Release>:Detour>
        $<$<CONFIG:Release>:DetourCrowd>
        $<$<CONFIG:Release>:DetourTileCache>
        # Jolt Physics (MSVC build tree — not installed to SDK output)
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/Jolt.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/Jolt.lib>"
        # Audio codec libraries (ogg, vorbis, vorbisfile, opus, opusfile)
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/ogg/build_Debug/Debug/ogg.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/ogg/build_Release/Release/ogg.lib>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Debug/lib/Debug/vorbis.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Release/lib/Release/vorbis.lib>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Debug/lib/Debug/vorbisfile.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Release/lib/Release/vorbisfile.lib>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/opus/build_Release/Release/opus.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/opus/build_Release/Release/opus.lib>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/opusfile/build_Debug/Debug/opusfile.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/opusfile/build_Release/Release/opusfile.lib>"
    )
else()
    # GCC / MinGW
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3
        $<$<CONFIG:Debug>:assimpd>
        $<$<CONFIG:Release>:assimp>
        angelscript
        $<$<CONFIG:Debug>:libglew32d>
        $<$<CONFIG:Release>:libglew32>
        zlibstatic
        # Recast Navigation
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libRecast.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetour.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourCrowd.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourTileCache.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libRecast.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetour.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourCrowd.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourTileCache.a>"
        # Audio codec libraries (MinGW)
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/ogg/build_Debug/libogg.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/ogg/build_Release/libogg.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Debug/lib/libvorbis.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Release/lib/libvorbis.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Debug/lib/libvorbisfile.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/vorbis/build_Release/lib/libvorbisfile.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/opus/build_Debug/libopus.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/opus/build_Release/libopus.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/opusfile/build_Debug/libopusfile.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/opusfile/build_Release/libopusfile.a>"
    )

    # MinGW-only linker flags: static runtime, strip symbols in Release
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libstdc++
        -static-libgcc
        -static
        $<$<CONFIG:Release>:-s>
    )
endif()

