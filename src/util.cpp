#include "util.h"

#include <miniaudio.h>
// Custom decoding backends provided by the Kemena3D SDK
#include "miniaudio_libvorbis.h"
#include "miniaudio_libopus.h"

#include <stb/stb_image.h>
#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>
// Implementation lives in manager.cpp; here we only need the declarations.
#include <stb/stb_image_resize2.h>

#include <fstream> // for std::ofstream
#include <vector>
#include <iostream>
#include <filesystem>
#include <cstring> // for memcpy
#include <sstream>
#include <cctype>
#include <algorithm>       // for std::max
#include <cmath>           // for std::sqrt
#include <assimp/config.h> // for AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/Logger.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include <imgui.h>

uint32_t utf8Next(const char *&it, const char *end)
{
    if (it >= end)
        return 0;

    unsigned char c = static_cast<unsigned char>(*it++);
    if (c < 0x80)
        return c; // ASCII

    // Multibyte
    int extra = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0)
    {
        cp = c & 0x1F;
        extra = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        cp = c & 0x0F;
        extra = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        cp = c & 0x07;
        extra = 3;
    }
    else
        return 0xFFFD;

    for (int i = 0; i < extra; i++)
    {
        if (it >= end)
            return 0xFFFD;
        unsigned char cc = static_cast<unsigned char>(*it);
        if ((cc & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++it;
    }
    return cp;
}

// Encode UTF-8 codepoint
void utf8Encode(uint32_t cp, kString &out)
{
    if (cp < 0x80)
        out.push_back((char)cp);
    else if (cp < 0x800)
    {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

kString fitTextWithEllipsisUtf8(kGuiManager *gui, const kString &text, float maxWidth)
{
    if (text.empty())
        return "";

    if (gui->calcTextSize(text).x <= maxWidth)
        return text;

    const kString ell = "...";

    const char *begin = text.c_str();
    const char *end = text.c_str() + text.size();
    const char *it = begin;

    kString out;
    while (it < end)
    {
        const char *prev = it;
        uint32_t cp = utf8Next(it, end);

        kString tmp = out;
        utf8Encode(cp, tmp);
        tmp += ell;

        if (gui->calcTextSize(tmp).x > maxWidth)
            break;

        out.append(prev, it);
    }

    out += ell;
    return out;
}

// Recursively scale a node's local translation.
static void scaleNodeTranslations(aiNode *n, float s)
{
    if (!n)
        return;
    n->mTransformation.a4 *= s;
    n->mTransformation.b4 *= s;
    n->mTransformation.c4 *= s;
    for (unsigned int i = 0; i < n->mNumChildren; ++i)
        scaleNodeTranslations(n->mChildren[i], s);
}

// Bake a uniform scale into a mutable scene's geometry. The runtime mesh loader
// ignores node transforms, so the scale has to live in the vertices (and, for
// skinned/animated meshes, in the bone bind offsets and animation position
// keys) rather than in a node matrix.
static void scaleSceneUniform(aiScene *s, float scale)
{
    if (!s || scale == 1.0f)
        return;

    for (unsigned int m = 0; m < s->mNumMeshes; ++m)
    {
        aiMesh *mesh = s->mMeshes[m];
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            mesh->mVertices[v] *= scale;
        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            aiMatrix4x4 &o = mesh->mBones[b]->mOffsetMatrix;
            o.a4 *= scale;
            o.b4 *= scale;
            o.c4 *= scale; // translation part
        }
    }

    for (unsigned int a = 0; a < s->mNumAnimations; ++a)
    {
        aiAnimation *anim = s->mAnimations[a];
        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            aiNodeAnim *ch = anim->mChannels[c];
            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
                ch->mPositionKeys[k].mValue *= scale;
        }
    }

    scaleNodeTranslations(s->mRootNode, scale);
}

// Forwards Assimp warning-severity log lines into a std::vector<std::string>.
// Only attached for Warn severity, so anything it receives is a real warning.
//
// IMPORTANT: This stream owns its captured warnings internally rather than
// holding a raw pointer to a caller-provided vector.  The raw-pointer approach
// caused use-after-free crashes (access violation in push_back) when the
// caller's vector was destroyed, moved, or reallocated while the stream was
// still attached to the global Assimp DefaultLogger.  Callers retrieve the
// captured warnings via moveTo() after the import completes.
namespace
{
    class WarnCaptureStream : public Assimp::LogStream
    {
    public:
        WarnCaptureStream() = default;

        void write(const char *message) override
        {
            if (!message)
                return;
            std::string s(message);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            if (!s.empty())
                warnings.push_back(std::move(s));
        }

        /// Append captured warnings to the caller-provided vector (if non-null).
        /// Safe to call multiple times — each call drains the internal buffer.
        void moveTo(std::vector<std::string> *out)
        {
            if (out && !warnings.empty())
            {
                out->insert(out->end(),
                            std::make_move_iterator(warnings.begin()),
                            std::make_move_iterator(warnings.end()));
                warnings.clear();
            }
        }

    private:
        std::vector<std::string> warnings;
    };
}

/**
 * @brief Explains why a legacy FBX file cannot be imported, if that is the case.
 *
 * Assimp's FBX reader implements the FBX 2011+ DOM only (binary 7100 and up, i.e.
 * FBX 2011/2012/2013). Older exports — FBX 6.0/6.1, still produced by legacy
 * exporters and converters — are rejected deep inside the parser, where the only
 * feedback is "FBX-DOM unsupported, old format version, supported are only FBX
 * 2011, FBX 2012 and FBX 2013". Reading the version up front turns that into an
 * actionable message.
 *
 * @param inputPath Candidate model file.
 * @return A user-facing explanation, or an empty string when the file is fine.
 */
static std::string legacyFbxReason(const fs::path &inputPath)
{
    if (inputPath.extension().string() != ".fbx")
        return std::string();

    std::ifstream f(inputPath, std::ios::binary);
    if (!f)
        return std::string();

    char header[64] = {};
    f.read(header, static_cast<std::streamsize>(sizeof(header) - 1));
    const std::streamsize got = f.gcount();

    // Binary FBX: the magic "Kaydara FBX Binary  \0", two unknown bytes, then the
    // version as a little-endian uint32 at offset 23.
    static const char kBinaryMagic[] = "Kaydara FBX Binary  ";
    if (got >= 27 && std::memcmp(header, kBinaryMagic, sizeof(kBinaryMagic) - 1) == 0)
    {
        const unsigned char *b = reinterpret_cast<const unsigned char *>(header);
        const uint32_t version = uint32_t(b[23]) | (uint32_t(b[24]) << 8) |
                                 (uint32_t(b[25]) << 16) | (uint32_t(b[26]) << 24);
        if (version >= 7100)
            return std::string();

        std::ostringstream oss;
        oss << "FBX " << (version / 1000) << "." << ((version / 100) % 10)
            << " (binary version " << version << ") predates FBX 2011 (7100), which is "
               "the oldest FBX the mesh importer can read. Re-export the model as FBX "
               "2011+ (binary 7.4/7.5, e.g. FBX 2014/2015) from its authoring tool, or "
               "convert it to .glb / .gltf / .obj and import that instead.";
        return oss.str();
    }

    // ASCII FBX declares its version next to "FBXVersion".
    const std::string text(header, static_cast<size_t>(got));
    const size_t at = text.find("FBXVersion");
    if (at == std::string::npos)
        return std::string();

    size_t p = text.find_first_of("0123456789", at);
    if (p == std::string::npos)
        return std::string();

    unsigned long version = 0;
    while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])))
        version = version * 10 + static_cast<unsigned long>(text[p++] - '0');
    if (version == 0 || version >= 7100)
        return std::string();

    std::ostringstream oss;
    oss << "FBX " << (version / 1000) << "." << ((version / 100) % 10)
        << " (ASCII version " << version << ") predates FBX 2011 (7100), which is the "
           "oldest FBX the mesh importer can read. Re-export the model as FBX 2011+ "
           "or convert it to .glb / .gltf / .obj and import that instead.";
    return oss.str();
}

bool convertMeshToGlbEx(const fs::path &inputPath, const fs::path &outputPath,
                        const MeshImportOptions &opt, std::string *errorOut,
                        std::vector<std::string> *warningsOut)
{
    // Fail fast (with a useful message) on FBX files older than the FBX 2011 DOM
    // that Assimp implements — see legacyFbxReason().
    if (const std::string reason = legacyFbxReason(inputPath); !reason.empty())
    {
        if (errorOut)
            *errorOut = reason;
        std::cerr << "Mesh import rejected: " << inputPath << ": " << reason << "\n";
        return false;
    }

    Assimp::Importer importer;

    // Optionally capture Assimp warnings into warningsOut. The DefaultLogger is a
    // global singleton; create one only if the app hasn't, and kill only what we
    // created. (Imports never run concurrently — they're gated behind a modal.)
    // Capture warnings internally — the stream owns its buffer so we never
    // risk a dangling-pointer crash, even if the caller-supplied vector is
    // destroyed or the DefaultLogger outlives this call.
    WarnCaptureStream warnStream;
    bool createdLogger = false;
    if (warningsOut)
    {
        if (Assimp::DefaultLogger::isNullLogger())
        {
            // defStreams = 0 so Assimp doesn't spawn its default file/debugger logs.
            Assimp::DefaultLogger::create("", Assimp::Logger::NORMAL, 0);
            createdLogger = true;
        }
        Assimp::DefaultLogger::get()->attachStream(&warnStream, Assimp::Logger::Warn);
    }

    struct WarnGuard
    {
        WarnCaptureStream *s;
        bool *created;
        bool active;
        ~WarnGuard()
        {
            if (!active)
                return;
            Assimp::DefaultLogger::get()->detachStream(s, Assimp::Logger::Warn);
            if (*created)
                Assimp::DefaultLogger::kill();
        }
    } warnGuard{&warnStream, &createdLogger, warningsOut != nullptr};

    // GenSmoothNormals (not GenNormals) so meshes without normals get smooth
    // shading instead of facets. ValidateDataStructure is intentionally NOT used:
    // it rejects otherwise-loadable glTF and was causing "Failed to convert".
    // Scale is baked into geometry below (not via aiProcess_GlobalScale) because
    // the runtime loader ignores node transforms.
    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_SortByPType |
        aiProcess_ImproveCacheLocality;
    if (opt.tangents == 1)
        flags |= aiProcess_CalcTangentSpace; // needed for normal mapping

    const aiScene *scene = importer.ReadFile(inputPath.string(), flags);

    // Transfer any warnings captured during ReadFile to the caller *before*
    // we check for failure — warnings are useful for diagnosing load issues.
    warnStream.moveTo(warningsOut);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        if (errorOut)
            *errorOut = importer.GetErrorString();
        std::cerr << "Assimp failed to load " << inputPath
                  << ": " << importer.GetErrorString() << "\n";
        return false;
    }

    const bool doScale = (opt.scaleFactor > 0.0f && opt.scaleFactor != 1.0f);
    const bool doStripAnim = (!opt.importAnimation && scene->mNumAnimations > 0);

    // We need a mutable scene to bake scale and/or drop animations.
    aiScene *owned = nullptr;
    const aiScene *toExport = scene;
    if (doScale || doStripAnim)
    {
        owned = importer.GetOrphanedScene(); // ownership transfers to us
        toExport = owned;

        if (doStripAnim)
        {
            for (unsigned int i = 0; i < owned->mNumAnimations; ++i)
                delete owned->mAnimations[i];
            delete[] owned->mAnimations;
            owned->mAnimations = nullptr;
            owned->mNumAnimations = 0;
        }
        if (doScale)
            scaleSceneUniform(owned, opt.scaleFactor);
    }
    else if (scene->mNumAnimations > 0)
    {
        std::cout << inputPath << " contains " << scene->mNumAnimations
                  << " animation(s). They will be exported.\n";
    }

    Assimp::Exporter exporter;
    aiReturn ret = exporter.Export(toExport, "glb2", outputPath.string());
    delete owned;

    // Append any warnings emitted during Export (uncommon but safe).
    warnStream.moveTo(warningsOut);

    if (ret != AI_SUCCESS)
    {
        if (errorOut)
            *errorOut = exporter.GetErrorString();
        std::cerr << "Assimp failed to export " << outputPath
                  << ": " << exporter.GetErrorString() << "\n";
        return false;
    }

    std::cout << "Converted: " << inputPath << " -> " << outputPath << "\n";
    return true;
}

bool convertMeshToGlb(const fs::path &inputPath, const fs::path &outputPath)
{
    return convertMeshToGlbEx(inputPath, outputPath, MeshImportOptions{});
}

int getMaxAnimationFrames(const fs::path &inputPath, float fps)
{
    if (fps <= 0.0f)
        fps = 30.0f;

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(inputPath.string(), 0);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mNumAnimations == 0)
        return 30;

    double maxFrames = 0.0;

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation *anim = scene->mAnimations[a];
        if (!anim)
            continue;

        double maxTicks = (anim->mDuration > 0.0) ? (double)anim->mDuration : 0.0;

        // mDuration normally covers the longest channel, but be defensive:
        // some exporters leave it stale, so also scan every keyframe.
        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim *ch = anim->mChannels[c];
            if (!ch)
                continue;

            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
                maxTicks = std::max(maxTicks, (double)ch->mPositionKeys[k].mTime);
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k)
                maxTicks = std::max(maxTicks, (double)ch->mRotationKeys[k].mTime);
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k)
                maxTicks = std::max(maxTicks, (double)ch->mScalingKeys[k].mTime);
        }

        const double ticksPerSecond = (anim->mTicksPerSecond > 0.0)
            ? (double)anim->mTicksPerSecond
            : 1.0;
        maxFrames = std::max(maxFrames, maxTicks / ticksPerSecond * (double)fps);
    }

    return std::max(0, (int)std::lround(maxFrames));
}

// DXT5-compress a single mip level (RGBA8, tightly packed) into a block buffer.
// Partial edge blocks are padded with zeros, matching the DDS block layout.
static std::vector<unsigned char> dxt5CompressLevel(const unsigned char *rgba, int w, int h, int quality)
{
    int blockCountX = (w + 3) / 4;
    int blockCountY = (h + 3) / 4;
    std::vector<unsigned char> out((size_t)blockCountX * blockCountY * 16);

    unsigned char *outPtr = out.data();
    for (int by = 0; by < h; by += 4)
    {
        for (int bx = 0; bx < w; bx += 4)
        {
            unsigned char block[64] = {0}; // 4x4 RGBA pixels
            for (int yy = 0; yy < 4; yy++)
            {
                for (int xx = 0; xx < 4; xx++)
                {
                    int sx = bx + xx;
                    int sy = by + yy;
                    unsigned char *dst = &block[4 * (yy * 4 + xx)];
                    if (sx < w && sy < h)
                        memcpy(dst, rgba + 4 * ((size_t)sy * w + sx), 4);
                    else
                        memset(dst, 0, 4);
                }
            }
            stb_compress_dxt_block(outPtr, block, 1 /*alpha=DXT5*/, quality);
            outPtr += 16;
        }
    }
    return out;
}

// Half a dimension, never below 1 (standard mip-chain reduction).
static inline int mipHalf(int v) { return v > 1 ? v / 2 : 1; }

// Convert a grayscale height map (RGBA in, height = Rec.601 luma) into a
// tangent-space normal map (RGBA out, +Z up, alpha opaque). `bumpiness` scales
// the slope. When `smooth` is set the height field is pre-blurred (separable
// 5-tap box) before differencing, giving noticeably softer normals; `sharp`
// differences the raw height for crisp, high-frequency detail. Edges clamp.
static std::vector<unsigned char> heightToNormal(const unsigned char *rgba, int w, int h, float bumpiness, bool smooth)
{
    const size_t n = (size_t)w * h;

    // Build a float height field (Rec.601 luma, 0..1).
    std::vector<float> hf(n);
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char *p = rgba + i * 4;
        hf[i] = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
    }

    auto clampX = [&](int x)
    { return std::min(std::max(x, 0), w - 1); };
    auto clampY = [&](int y)
    { return std::min(std::max(y, 0), h - 1); };

    // Smooth: separable 5-tap box blur of the height field. This is what makes
    // the Sharp/Smooth choice clearly visible.
    if (smooth)
    {
        const int R = 2; // radius -> 5 taps
        std::vector<float> tmp(n);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0.0f;
                for (int k = -R; k <= R; ++k)
                    s += hf[(size_t)y * w + clampX(x + k)];
                tmp[(size_t)y * w + x] = s / (2 * R + 1);
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float s = 0.0f;
                for (int k = -R; k <= R; ++k)
                    s += tmp[(size_t)clampY(y + k) * w + x];
                hf[(size_t)y * w + x] = s / (2 * R + 1);
            }
    }

    auto at = [&](int x, int y) -> float
    { return hf[(size_t)clampY(y) * w + clampX(x)]; };

    std::vector<unsigned char> out(n * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float dx = at(x + 1, y) - at(x - 1, y);
            float dy = at(x, y + 1) - at(x, y - 1);
            float nx = -dx * bumpiness;
            float ny = -dy * bumpiness;
            float nz = 1.0f;
            float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv;
            ny *= inv;
            nz *= inv;
            unsigned char *o = out.data() + 4 * ((size_t)y * w + x);
            o[0] = (unsigned char)(std::min(std::max((nx * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[1] = (unsigned char)(std::min(std::max((ny * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[2] = (unsigned char)(std::min(std::max((nz * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f), 255.0f));
            o[3] = 255;
        }
    return out;
}

/**
 * @brief Push opaque colour outward into fully-transparent texels (alpha bleed).
 *
 * Source images routinely store arbitrary RGB in fully-transparent texels —
 * white being by far the most common, since RGB is treated as "don't care"
 * there. Alpha is never filtered in isolation: bilinear sampling, mip
 * generation and DXT5 colour blocks all interpolate RGB across the alpha
 * boundary, so that white is dragged into the visible edge and shows up as a
 * bright one-pixel fringe around otherwise-opaque artwork.
 *
 * Replicating the nearest opaque colour into the transparent texels (leaving
 * their alpha at 0) makes every filter kernel blend edge colour into edge
 * colour instead of into white, which removes the fringe. Nothing visible
 * changes — fully-transparent pixels stay fully transparent — only the RGB the
 * interpolation kernels average in does.
 *
 * Implemented as a multi-source breadth-first flood seeded from the opaque
 * texels that line the alpha boundary, so each transparent texel takes the
 * colour of its nearest opaque neighbour. A wave only reads the previous wave,
 * which stops a spread from smearing a farther colour back toward the edge it
 * came from. The flood has no depth limit, so even the lowest mip levels (which
 * average very large blocks) only ever see edge colour.
 *
 * Runs in O(w * h) time and uses one integer of scratch memory per pixel.
 *
 * @param px Tightly packed RGBA8 pixels (modified in place).
 * @param w  Image width in pixels.
 * @param h  Image height in pixels.
 */
static void bleedTransparentEdges(unsigned char *px, int w, int h)
{
    if (!px || w <= 0 || h <= 0)
        return;

    const size_t n = (size_t)w * (size_t)h;

    // 0 = untouched transparent texel, 1 = originally opaque (flood sources),
    // k > 1 = texel bled by wave k-1. Reading only same-wave sources keeps the
    // colour flowing outward from the true edge.
    std::vector<int> wave(n, 0);
    bool anyTransparent = false;
    for (size_t i = 0; i < n; ++i)
    {
        if (px[i * 4 + 3] > 0)
            wave[i] = 1;
        else
            anyTransparent = true;
    }

    // Fully opaque image: nothing to bleed, and flooding would only copy the
    // image onto itself. This also covers the normal-map and alphaSource = None
    // paths, which force alpha to 255.
    if (!anyTransparent)
        return;

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    // Seed only opaque texels that touch a transparent one. Every reachable
    // transparent texel is adjacent to an opaque texel, so this still floods the
    // whole region while keeping the frontier proportional to the alpha edges
    // rather than to the image area.
    std::vector<size_t> frontier;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t)y * (size_t)w + (size_t)x;
            if (wave[i] != 1)
                continue;
            for (int k = 0; k < 4; ++k)
            {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                if (wave[(size_t)ny * (size_t)w + (size_t)nx] == 0)
                {
                    frontier.push_back(i);
                    break;
                }
            }
        }
    }

    int cur = 1;
    std::vector<size_t> nextFrontier;

    while (!frontier.empty())
    {
        nextFrontier.clear();

        for (size_t idx : frontier)
        {
            const int x = (int)(idx % (size_t)w);
            const int y = (int)(idx / (size_t)w);

            for (int k = 0; k < 4; ++k)
            {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;

                const size_t j = (size_t)ny * (size_t)w + (size_t)nx;
                if (wave[j] != 0)
                    continue; // opaque, or already filled by an earlier wave

                // Average only the current wave's neighbours, never a mix of
                // distances, so the bled colour stays close to the edge colour.
                int r = 0, g = 0, b = 0, count = 0;
                for (int m = 0; m < 4; ++m)
                {
                    const int ax = nx + dx[m];
                    const int ay = ny + dy[m];
                    if (ax < 0 || ay < 0 || ax >= w || ay >= h)
                        continue;
                    const size_t s = (size_t)ay * (size_t)w + (size_t)ax;
                    if (wave[s] != cur)
                        continue;
                    r += px[s * 4 + 0];
                    g += px[s * 4 + 1];
                    b += px[s * 4 + 2];
                    ++count;
                }
                if (count == 0)
                    continue;

                px[j * 4 + 0] = (unsigned char)(r / count);
                px[j * 4 + 1] = (unsigned char)(g / count);
                px[j * 4 + 2] = (unsigned char)(b / count);
                // Alpha deliberately left at 0: the texel stays invisible.
                wave[j] = cur + 1;
                nextFrontier.push_back(j);
            }
        }

        frontier.swap(nextFrontier);
        ++cur;
    }
}

bool convertImageToDDS(const fs::path &inputPath, const fs::path &outputPath, const ImageImportOptions &opt)
{
    // stb's flip flag is a sticky global; set it for this load only and reset
    // it afterwards so other studio image loads aren't affected.
    stbi_set_flip_vertically_on_load(opt.flipVertical ? 1 : 0);
    int srcW, srcH, comp;
    unsigned char *src = stbi_load(inputPath.string().c_str(), &srcW, &srcH, &comp, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!src)
    {
        std::cerr << "Failed to load image: " << inputPath << "\n";
        return false;
    }

    if (opt.isNormalMap)
    {
        // --- Normal-map processing ---------------------------------------
        if (opt.fromGrayscale)
        {
            std::vector<unsigned char> nrm =
                heightToNormal(src, srcW, srcH, opt.bumpiness, opt.normalFilter == 1);
            memcpy(src, nrm.data(), (size_t)srcW * srcH * 4);
        }
        if (opt.flipGreen)
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
                src[i * 4 + 1] = (unsigned char)(255 - src[i * 4 + 1]);
        // Normal maps don't carry alpha — keep it opaque.
        for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            src[i * 4 + 3] = 255;
    }
    else
    {
        // --- Alpha source ------------------------------------------------
        // 0 = None (force opaque), 1 = keep input alpha, 2 = derive from grayscale.
        if (opt.alphaSource == 0)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
                src[i * 4 + 3] = 255;
        }
        else if (opt.alphaSource == 2)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            {
                unsigned char *p = src + i * 4;
                // Rec.601 luma; good enough for an authored grayscale->alpha mask.
                p[3] = (unsigned char)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            }
        }

        // --- Linear Grayscale channel: collapse RGB to luma --------------
        if (opt.grayscale)
        {
            for (size_t i = 0; i < (size_t)srcW * srcH; ++i)
            {
                unsigned char *p = src + i * 4;
                unsigned char y = (unsigned char)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
                p[0] = p[1] = p[2] = y;
            }
        }
    }

    // --- Alpha bleed ------------------------------------------------------
    // Run before resize/mip generation/compression so none of those filters
    // can drag the RGB of transparent texels (usually white) into the visible
    // edge — the source of the bright fringes around opaque artwork.
    bleedTransparentEdges(src, srcW, srcH);

    // --- Resize to clamp the longest edge to Max Size ---------------------
    // Always resize into a caller-owned vector (never the NULL-output allocator)
    // so ownership is unambiguous across translation units.
    int w = srcW, h = srcH;
    std::vector<unsigned char> top;
    int maxSize = opt.maxSize > 0 ? opt.maxSize : 4096;
    int longest = std::max(srcW, srcH);
    if (longest > maxSize)
    {
        double scale = (double)maxSize / (double)longest;
        w = std::max(1, (int)(srcW * scale + 0.5));
        h = std::max(1, (int)(srcH * scale + 0.5));
        top.resize((size_t)w * h * 4);
        unsigned char *ok = opt.sRGB
                                ? stbir_resize_uint8_srgb(src, srcW, srcH, 0, top.data(), w, h, 0, STBIR_RGBA)
                                : stbir_resize_uint8_linear(src, srcW, srcH, 0, top.data(), w, h, 0, STBIR_RGBA);
        if (!ok)
        {
            std::cerr << "Failed to resize image: " << inputPath << "\n";
            stbi_image_free(src);
            return false;
        }
    }
    else
    {
        top.assign(src, src + (size_t)srcW * srcH * 4);
    }
    stbi_image_free(src);
    src = nullptr;

    // --- Build the mip chain ---------------------------------------------
    int levels = 1;
    if (opt.generateMipmap)
    {
        int lw = w, lh = h;
        while (lw > 1 || lh > 1)
        {
            lw = mipHalf(lw);
            lh = mipHalf(lh);
            ++levels;
        }
    }

    const bool compressed = (opt.compression != 0);
    const int quality = (opt.compression >= 3) ? STB_DXT_HIGHQUAL : STB_DXT_NORMAL;

    // Concatenate every level's bytes (top first), generating each mip by
    // downsampling the *previous* level so the chain stays consistent.
    std::vector<unsigned char> payload;
    std::vector<unsigned char> prev = std::move(top);
    int lw = w, lh = h;
    for (int level = 0; level < levels; ++level)
    {
        if (level > 0)
        {
            int nw = mipHalf(lw), nh = mipHalf(lh);
            std::vector<unsigned char> next((size_t)nw * nh * 4);
            unsigned char *ok = opt.sRGB
                                    ? stbir_resize_uint8_srgb(prev.data(), lw, lh, 0, next.data(), nw, nh, 0, STBIR_RGBA)
                                    : stbir_resize_uint8_linear(prev.data(), lw, lh, 0, next.data(), nw, nh, 0, STBIR_RGBA);
            if (!ok)
            {
                std::cerr << "Mip resize failed: " << inputPath << "\n";
                return false;
            }
            prev.swap(next);
            lw = nw;
            lh = nh;
        }

        if (compressed)
        {
            std::vector<unsigned char> blk = dxt5CompressLevel(prev.data(), lw, lh, quality);
            payload.insert(payload.end(), blk.begin(), blk.end());
        }
        else
        {
            payload.insert(payload.end(), prev.begin(), prev.end());
        }
    }

    // --- DDS header -------------------------------------------------------
    DDSHeader header{};
    header.dwMagic = 0x20534444; // "DDS "
    header.dwSize = 124;
    header.dwHeight = h;
    header.dwWidth = w;
    header.dwMipMapCount = (uint32_t)levels;

    uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000; // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    if (levels > 1)
        flags |= 0x20000; // DDSD_MIPMAPCOUNT
    header.ddpf.dwSize = 32;

    if (compressed)
    {
        flags |= 0x80000; // DDSD_LINEARSIZE
        header.dwPitchOrLinearSize = (uint32_t)((((w + 3) / 4) * (size_t)((h + 3) / 4)) * 16);
        header.ddpf.dwFlags = 0x4; // DDPF_FOURCC
        header.ddpf.dwFourCC = ('D') | ('X' << 8) | ('T' << 16) | ('5' << 24);
    }
    else
    {
        flags |= 0x8; // DDSD_PITCH
        header.dwPitchOrLinearSize = (uint32_t)(w * 4);
        header.ddpf.dwFlags = 0x41; // DDPF_RGB | DDPF_ALPHAPIXELS
        header.ddpf.dwRGBBitCount = 32;
        header.ddpf.dwRBitMask = 0x000000FF; // memory order R,G,B,A
        header.ddpf.dwGBitMask = 0x0000FF00;
        header.ddpf.dwBBitMask = 0x00FF0000;
        header.ddpf.dwABitMask = 0xFF000000;
    }

    header.dwFlags = flags;
    header.caps.dwCaps1 = 0x1000; // DDSCAPS_TEXTURE
    if (levels > 1)
        header.caps.dwCaps1 |= 0x8 | 0x400000; // COMPLEX | MIPMAP

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs)
    {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char *>(&header), sizeof(header));
    ofs.write(reinterpret_cast<const char *>(payload.data()), payload.size());
    ofs.close();

    std::cout << "Converted: " << inputPath << " -> " << outputPath
              << " (" << w << "x" << h << ", " << levels << " level(s), "
              << (compressed ? "DXT5" : "RGBA8") << ")\n";
    return true;
}

// Back-compat wrapper: default import settings (used by the initial batch
// import before the user customizes per-texture settings).
bool convertImageToDxt5(const fs::path &inputPath, const fs::path &outputPath)
{
    return convertImageToDDS(inputPath, outputPath, ImageImportOptions{});
}

bool convertAudioToWav(const fs::path &inputPath, const fs::path &outputPath)
{
    // Decode the source file using miniaudio's ma_decoder, then write
    // a 16-bit PCM WAV.  This guarantees the library copy is in a format
    // that every platform backend can play, regardless of the source codec
    // (Vorbis, Opus, MP3, FLAC, …).

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);

    // Register custom decoding backends so the importer can handle Opus and
    // Vorbis files via libopus/libvorbis, not just the built-in stb_vorbis.
    // NOTE: customBackends must live as long as config — do NOT put this in
    // a nested scope, or config.ppCustomBackendVTables becomes a dangling
    // pointer and ma_decoder_init_file will crash on a garbage vtable.
    ma_decoding_backend_vtable* customBackends[] = {
        ma_decoding_backend_libvorbis,
        ma_decoding_backend_libopus,
    };
    config.ppCustomBackendVTables = customBackends;
    config.customBackendCount     = sizeof(customBackends) / sizeof(customBackends[0]);
    config.pCustomBackendUserData = nullptr;

    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(inputPath.string().c_str(), &config, &decoder);
    if (result != MA_SUCCESS)
    {
        // Fall back: copy the original file as-is. The engine can decode
        // most formats (Vorbis, MP3, FLAC) directly from source, and the
        // WAV conversion is a best-effort optimisation.
        std::cerr << "[Audio Import] Decode of '" << inputPath
                  << "' failed (ma_result = " << result
                  << "), copying source as-is." << std::endl;
        std::error_code ec;
        fs::copy_file(inputPath, outputPath, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    // Read all PCM frames into a float buffer
    ma_uint64 totalFrames = 0;
    std::vector<float> floatSamples;
    {
        ma_uint64 framesRead = 0;
        float temp[4096 * 2]; // up to 2 channels * 4096 frames
        do
        {
            framesRead = 0;
            result = ma_decoder_read_pcm_frames(&decoder, temp, 4096, &framesRead);
            if (result != MA_SUCCESS && result != MA_AT_END)
            {
                std::cerr << "[Audio Import] Read error for '" << inputPath
                          << "' (ma_result = " << result << ")." << std::endl;
                ma_decoder_uninit(&decoder);
                return false;
            }
            if (framesRead > 0)
            {
                size_t sampleCount = framesRead * decoder.outputChannels;
                floatSamples.insert(floatSamples.end(), temp, temp + sampleCount);
                totalFrames += framesRead;
            }
        } while (result != MA_AT_END && framesRead > 0);
    }

    ma_uint32 sampleRate    = decoder.outputSampleRate;
    ma_uint32 channels      = decoder.outputChannels;
    ma_uint32 bitsPerSample = 16;
    ma_decoder_uninit(&decoder);

    if (totalFrames == 0 || floatSamples.empty())
    {
        std::cerr << "[Audio Import] No audio data decoded from '" << inputPath << "'." << std::endl;
        return false;
    }

    // Convert float → int16
    const size_t totalSamples = floatSamples.size();
    std::vector<ma_int16> pcmData(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i)
    {
        float s = floatSamples[i];
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pcmData[i] = static_cast<ma_int16>(s * 32767.0f);
    }
    floatSamples.clear(); // free float buffer early
    floatSamples.shrink_to_fit();

    // Write a canonical 16-bit PCM WAV
    std::ofstream out(outputPath, std::ios::binary);
    if (!out)
    {
        std::cerr << "[Audio Import] Cannot open output file '" << outputPath
                  << "' for writing." << std::endl;
        return false;
    }

    ma_uint32 byteRate    = sampleRate * channels * (bitsPerSample / 8);
    ma_uint16 blockAlign  = static_cast<ma_uint16>(channels * (bitsPerSample / 8));
    ma_uint32 dataSize    = static_cast<ma_uint32>(totalSamples * (bitsPerSample / 8));
    ma_uint32 riffSize    = 36 + dataSize;

    auto write32 = [&](ma_uint32 v) { out.write(reinterpret_cast<const char *>(&v), 4); };
    auto write16 = [&](ma_uint16 v) { out.write(reinterpret_cast<const char *>(&v), 2); };

    out.write("RIFF", 4);
    write32(riffSize);
    out.write("WAVE", 4);

    // fmt  sub-chunk
    out.write("fmt ", 4);
    write32(16);                    // sub-chunk size (PCM)
    write16(1);                     // audio format (PCM = 1)
    write16(static_cast<ma_uint16>(channels));
    write32(sampleRate);
    write32(byteRate);
    write16(blockAlign);
    write16(static_cast<ma_uint16>(bitsPerSample));

    // data sub-chunk
    out.write("data", 4);
    write32(dataSize);
    out.write(reinterpret_cast<const char *>(pcmData.data()), dataSize);

    out.close();
    if (!out)
    {
        std::cerr << "[Audio Import] Write error for '" << outputPath << "'." << std::endl;
        return false;
    }

    std::cout << "[Audio Import] " << inputPath.filename()
              << " -> " << outputPath.filename()
              << " (" << sampleRate << " Hz, " << channels << " ch, "
              << totalFrames << " frames)" << std::endl;
    return true;
}
// ---------------------------------------------------------------------------
// Shader reflection
// ---------------------------------------------------------------------------

static kString svTrim(const kString &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a]))
        a++;
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        b--;
    return s.substr(a, b - a);
}

// Turn a uniform name into a friendlier label: drop a leading "material." and a
// trailing "Map", and Title-case the first letter. e.g. "material.diffuse" ->
// "Diffuse", "albedoMap" -> "Albedo".
static kString svPrettyLabel(const kString &name)
{
    kString n = name;
    size_t dot = n.rfind('.');
    if (dot != kString::npos)
        n = n.substr(dot + 1);
    if (n.size() > 3 && n.compare(n.size() - 3, 3, "Map") == 0)
        n = n.substr(0, n.size() - 3);
    if (!n.empty())
        n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}

std::vector<ShaderVar> parseShaderVars(const kString &source)
{
    std::vector<ShaderVar> vars;
    std::istringstream ss(source);
    std::string line;
    while (std::getline(ss, line))
    {
        size_t cpos = line.find("//");
        if (cpos == std::string::npos)
            continue;
        size_t apos = line.find("@var", cpos);
        if (apos == std::string::npos)
            continue;

        std::istringstream ls(line.substr(apos + 4)); // after "@var"
        ShaderVar v;
        if (!(ls >> v.type))
            continue;
        if (!(ls >> v.name))
            continue;

        std::string rest;
        std::getline(ls, rest);
        rest = svTrim(rest);
        // Allow an optional quoted or bare label.
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);
        v.label = rest.empty() ? svPrettyLabel(v.name) : rest;

        vars.push_back(v);
    }
    return vars;
}

kString getEmbeddedResourceText(const kString &resourceName)
{
#ifdef _WIN32
    HRSRC hRes = FindResourceA(NULL, resourceName.c_str(), RT_RCDATA);
    if (!hRes)
        return "";
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData)
        return "";
    DWORD size = SizeofResource(NULL, hRes);
    const char *data = static_cast<const char *>(LockResource(hData));
    if (!data || size == 0)
        return "";
    return kString(data, size);
#else
    (void)resourceName;
    return "";
#endif
}

kString builtinShaderResource(const kString &shaderName)
{
    if (shaderName == "Unlit")
        return "SHADER_MESH_FLAT";
    if (shaderName == "Phong")
        return "SHADER_MESH_PHONG";
    if (shaderName == "PBR")
        return "SHADER_MESH_PBR";
    if (shaderName == "Terrain PBR")
        return "SHADER_TERRAIN_PBR";
    return "";
}
