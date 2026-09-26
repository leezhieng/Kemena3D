#ifndef UTIL_H
#define UTIL_H

#include "kemena/kemena.h"

using namespace kemena;

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <mutex>

#include <filesystem>

namespace fs = std::filesystem;

/**
 * @brief Binary layout of a DirectDraw Surface (.dds) file header.
 *
 * Mirrors the Microsoft DDS_HEADER structure and is used when writing out
 * compressed (e.g. DXT5) textures. Fields follow the DDS specification and are
 * laid out for direct serialization to/from the start of a .dds file.
 */
struct DDSHeader
{
    uint32_t dwMagic;              ///< Magic value, always the four-character code "DDS ".
    uint32_t dwSize;              ///< Size of this header structure in bytes (excluding dwMagic).
    uint32_t dwFlags;             ///< Flags indicating which header members contain valid data.
    uint32_t dwHeight;            ///< Surface height in pixels.
    uint32_t dwWidth;             ///< Surface width in pixels.
    uint32_t dwPitchOrLinearSize; ///< Pitch (uncompressed) or total byte size (compressed) of the top mip.
    uint32_t dwDepth;             ///< Depth of a volume texture in pixels; otherwise unused.
    uint32_t dwMipMapCount;       ///< Number of mipmap levels stored.
    uint32_t dwReserved1[11];     ///< Reserved; unused.

    /** @brief Pixel format descriptor (DDS_PIXELFORMAT). */
    struct
    {
        uint32_t dwSize;        ///< Size of this pixel-format structure in bytes.
        uint32_t dwFlags;       ///< Flags describing the pixel format (e.g. presence of a FourCC).
        uint32_t dwFourCC;      ///< Four-character compression code (e.g. "DXT5") when applicable.
        uint32_t dwRGBBitCount; ///< Number of bits per pixel for uncompressed formats.
        uint32_t dwRBitMask;    ///< Bit mask for the red channel.
        uint32_t dwGBitMask;    ///< Bit mask for the green channel.
        uint32_t dwBBitMask;    ///< Bit mask for the blue channel.
        uint32_t dwABitMask;    ///< Bit mask for the alpha channel.
    } ddpf;

    /** @brief Surface capability flags (DDS_CAPS). */
    struct
    {
        uint32_t dwCaps1;    ///< Primary capability flags (texture/mipmap/complex).
        uint32_t dwCaps2;    ///< Additional capabilities (cubemap/volume faces).
        uint32_t dwDDSX;     ///< Reserved capability field; unused.
        uint32_t dwReserved; ///< Reserved; unused.
    } caps;

    uint32_t dwReserved2; ///< Reserved; unused.
};

/**
 * @brief Decode the next UTF-8 codepoint starting at @p it and advance the iterator.
 *
 * Reads one full UTF-8 sequence from the byte range [it, end), advancing @p it
 * past the consumed bytes.
 *
 * @param it  Reference to the current read position; advanced past the decoded sequence.
 * @param end One-past-the-end pointer bounding the input range.
 * @return The decoded Unicode codepoint, or 0xFFFD (replacement character) on malformed input.
 */
uint32_t utf8Next(const char*& it, const char* end);

/**
 * @brief Encode a Unicode codepoint as UTF-8 and append it to a string.
 *
 * @param cp  Unicode codepoint to encode.
 * @param out Destination string; the encoded bytes are appended.
 */
void utf8Encode(uint32_t cp, kString& out);

/**
 * @brief Truncate a UTF-8 string with a trailing "..." if it exceeds a pixel width.
 *
 * Measures the rendered width via the GUI manager and, if the text is wider than
 * @p maxWidth, removes whole codepoints from the end until the text plus the
 * ellipsis fits. Returns the original text unchanged when it already fits.
 *
 * @param gui      GUI manager used to measure rendered text size.
 * @param text     Source UTF-8 text to fit.
 * @param maxWidth Maximum allowed rendered width in pixels.
 * @return The original or ellipsized text that fits within @p maxWidth.
 */
kString fitTextWithEllipsisUtf8(kGuiManager *gui, const kString& text, float maxWidth);

/**
 * @brief Per-mesh import settings applied during conversion.
 */
struct MeshImportOptions
{
    float scaleFactor     = 1.0f; ///< Uniform global scale applied on import.
    int   tangents        = 0;    ///< 0 = keep imported tangents, 1 = generate (calc tangent space).
    bool  importAnimation = true; ///< When false, animations are stripped from the .glb.
};

/**
 * @brief Convert a model file to glTF binary (.glb) honouring import settings.
 *
 * Imports @p inputPath with smooth normals (generated only if missing), optional
 * tangent-space generation, and a global scale, then exports a .glb to
 * @p outputPath. Animations are kept unless @p opt.importAnimation is false.
 *
 * @param inputPath  Path to the source model file in any Assimp-supported format.
 * @param outputPath Destination path for the generated .glb file.
 * @param opt        Import settings (scale, tangents, animation).
 * @param errorOut    Optional; receives the Assimp error string on failure.
 * @param warningsOut Optional; receives any Assimp warning messages emitted
 *                    during import (non-fatal). Each entry is one warning line.
 * @return True on success, false on import or export failure.
 */
bool convertMeshToGlbEx(const fs::path& inputPath, const fs::path& outputPath,
                        const MeshImportOptions& opt, std::string* errorOut = nullptr,
                        std::vector<std::string>* warningsOut = nullptr);

/**
 * @brief Convert a model file to .glb using default import settings.
 *
 * Thin wrapper over convertMeshToGlbEx() for the initial batch import.
 *
 * @param inputPath  Path to the source model file.
 * @param outputPath Destination path for the generated .glb file.
 * @return True on successful conversion, false on import or export failure.
 */
bool convertMeshToGlb(const fs::path& inputPath, const fs::path& outputPath);

/**
 * @brief Determine the largest animation frame index present in a model file.
 *
 * Loads @p inputPath with Assimp and inspects every animation clip. For each
 * clip the longest tick span is taken from both the clip's duration and its
 * channel keyframes, converted to seconds using the clip's tick rate, then
 * converted to editor timeline frames using @p fps (30 by default, matching
 * the mesh-animation preview).
 *
 * @param inputPath Path to the source model file (e.g. an FBX with animations).
 * @param fps       Editor timeline frames-per-second used for the conversion.
 * @return The inclusive end-frame index, or 30 when the file has no animation
 *         (or cannot be loaded), preserving the previous default behaviour.
 */
int getMaxAnimationFrames(const fs::path& inputPath, float fps = 30.0f);

/**
 * @brief Per-texture import settings that affect the generated .dds bytes.
 *
 * Colour-space (sRGB), wrap and filter modes are *not* baked into the file —
 * they are applied by the loader at upload time and stored separately in the
 * texture's .meta. `sRGB` appears here only to pick the correct colour space
 * for gamma-aware resizing/mip generation.
 */
struct ImageImportOptions
{
    int  maxSize        = 4096; ///< Clamp the longest edge to this (downscale only).
    int  compression    = 2;    ///< 0=None (uncompressed RGBA8), 1=Low, 2=Normal, 3=High (DXT5).
    int  alphaSource    = 1;    ///< 0=None (opaque), 1=Input Alpha, 2=From Grayscale.
    bool sRGB           = true; ///< Treat colour as sRGB when resizing/generating mips (Channel = sRGB).
    bool grayscale      = false;///< Collapse RGB to luma before encoding (Channel = Linear Grayscale).
    bool generateMipmap = true; ///< Bake a full mip chain into the .dds.
    bool flipVertical   = false;///< Flip the source image vertically on load.

    // --- Normal-map options (only consulted when isNormalMap is true) -------
    bool  isNormalMap   = false;///< Treat this image as a tangent-space normal map.
    bool  flipGreen     = false;///< Invert the green channel (DirectX<->OpenGL convention).
    bool  fromGrayscale = false;///< Generate a normal map from a grayscale height map.
    float bumpiness     = 1.0f; ///< Height-to-normal strength (only when fromGrayscale).
    int   normalFilter  = 0;    ///< Grayscale gradient kernel: 0=Sharp, 1=Smooth.
};

/**
 * @brief Version of the image-to-DDS encoder's output.
 *
 * Bumped whenever the encoder changes in a way that makes previously imported
 * `Library/ImportedAssets/*.dds` files stale. The project scan compares this
 * against the `importerVersion` stored in each image's metadata and silently
 * re-imports (and re-stamps) any image written by an older encoder, so an
 * editor upgrade fixes existing projects without a manual re-import.
 *
 * History:
 *  - 1: original encoder (no alpha bleed).
 *  - 2: transparent texels are alpha-bled before resize/mip/DXT5, removing the
 *       bright fringe around opaque artwork.
 */
inline constexpr int kImageImporterVersion = 2;

/**
 * @brief Convert an image file to a DDS texture honouring import settings.
 *
 * Loads @p inputPath as RGBA, applies the alpha-source rule, optionally
 * downscales to @p opt.maxSize, builds a mip chain, and writes either DXT5 or
 * uncompressed RGBA8 (per @p opt.compression) as a .dds at @p outputPath.
 *
 * @param inputPath  Path to the source image file.
 * @param outputPath Destination path for the generated .dds file.
 * @param opt        Import settings controlling size, compression, alpha and mips.
 * @return True on success, false if the image could not be loaded, resized or written.
 */
bool convertImageToDDS(const fs::path& inputPath, const fs::path& outputPath, const ImageImportOptions& opt);

/**
 * @brief Convert an image to a DXT5 .dds using default import settings.
 *
 * Thin wrapper over convertImageToDDS() with an all-defaults
 * @ref ImageImportOptions. Prefer convertImageToDDS() with options derived from
 * a texture's metadata (see the project scan / reimportTexture) so a re-import
 * honours the settings the user chose in the Image Import Settings panel.
 *
 * @param inputPath  Path to the source image file.
 * @param outputPath Destination path for the generated .dds file.
 * @return True on successful conversion, false otherwise.
 */
bool convertImageToDxt5(const fs::path& inputPath, const fs::path& outputPath);

/**
 * @brief Convert an audio file to a PCM WAV using miniaudio.
 *
 * Decodes the source file (any format miniaudio supports: MP3, OGG, WAV, FLAC)
 * and re-encodes it as a 16-bit PCM WAV at @p outputPath. The output format
 * (sample rate, channels, bit depth) matches the decoded source.
 *
 * @param inputPath  Path to the source audio file.
 * @param outputPath Destination path for the generated .wav file.
 * @return True on successful conversion, false if decoding or encoding fails.
 */
bool convertAudioToWav(const fs::path& inputPath, const fs::path& outputPath);

// ---------------------------------------------------------------------------
// Shader reflection — `// @var` material parameter annotations
// ---------------------------------------------------------------------------

/**
 * @brief One material parameter declared by a `// @var <type> <name> [label]`
 *        comment in a shader's source.
 *
 * The material inspector renders one control per ShaderVar, and the runtime
 * pushes its value into the shader uniform named @ref name.
 */
struct ShaderVar
{
    kString type;  ///< GLSL/HLSL type: float, int, bool, vec2, vec3, vec4, sampler2D, samplerCube.
    kString name;  ///< Uniform name the value is bound to (e.g. "albedoMap", "material.diffuse").
    kString label; ///< Display label for the GUI (optional 3rd token; defaults to a prettified name).
};

/**
 * @brief Extracts all `// @var <type> <name> [label]` annotations from shader text.
 *
 * Scans every line for the `@var` marker inside a `//` comment and parses the
 * type, uniform name, and optional display label. Order of appearance is kept.
 *
 * @param source Combined shader source (vertex + fragment, any markers).
 * @return The declared material parameters, in source order.
 */
std::vector<ShaderVar> parseShaderVars(const kString& source);

/**
 * @brief Reads an embedded RT_RCDATA resource as a text string (Windows).
 * @param resourceName Resource identifier (e.g. "SHADER_MESH_PHONG").
 * @return The resource bytes as a string, or empty if not found.
 */
kString getEmbeddedResourceText(const kString& resourceName);

/**
 * @brief Maps a built-in shader display name to its embedded resource name.
 * @param shaderName "Unlit", "Phong", or "PBR".
 * @return The resource name (e.g. "SHADER_MESH_PHONG"), or empty for unknown/custom.
 */
kString builtinShaderResource(const kString& shaderName);

#endif // header guard

