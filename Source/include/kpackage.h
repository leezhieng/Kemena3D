/**
 * @file kpackage.h
 * @brief Binary asset package (.kpak) format for game distribution.
 *
 * A .kpak file is a concatenated archive of game assets with an index for O(1)
 * random-access lookups. Each file entry can optionally be compressed with
 * Deflate (zlib-compatible). The format is designed so the engine can stream
 * individual assets without decompressing the entire archive.
 *
 * Format layout (all integers little-endian):
 *
 *   [Header - 16 bytes]
 *     Magic:       "KPAK"         (4 bytes)
 *     Version:     uint32_t = 1   (4 bytes)
 *     Flags:       uint32_t       (4 bytes)  [bit 0 = has-compressed-entries]
 *     EntryCount:  uint32_t       (4 bytes)
 *
 *   [File Index - repeated EntryCount times]
 *     PathLength:        uint16_t  (2 bytes)
 *     Path:              char[]    (PathLength bytes, UTF-8, NOT null-terminated)
 *     DataOffset:        uint64_t  (8 bytes)  absolute byte offset from file start
 *     CompressedSize:    uint32_t  (4 bytes)  0 = uncompressed
 *     UncompressedSize:  uint32_t  (4 bytes)
 *
 *   [File Data] — concatenated raw or deflate-compressed blobs
 *
 * Paths use forward slashes and are relative to the package root, e.g.:
 *   "scene.world"
 *   "Library/ImportedAssets/abc123.glb"
 *   "Library/Scripts/abc123.kbc"
 */

#ifndef KPACKAGE_H
#define KPACKAGE_H

#include "kexport.h"
#include "kdatatype.h"

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <memory>

namespace kemena
{

/**
 * @brief Metadata for a single file stored inside a .kpak package.
 */
struct KEMENA3D_API kPackageEntry
{
    kString  path;             ///< Virtual path relative to package root (forward slashes).
    uint64_t dataOffset = 0;   ///< Absolute byte offset from the start of the .kpak file.
    uint32_t compressedSize = 0;   ///< Size of the stored blob (0 = uncompressed).
    uint32_t uncompressedSize = 0; ///< Size after decompression.
};

/**
 * @brief Reads assets from a .kpak package file.
 *
 * Open a package once, then call readFile() to decompress individual entries
 * on demand. The index is loaded into memory at construction time; file data
 * is streamed from disk when requested.
 */
class KEMENA3D_API kPackageReader
{
public:
    kPackageReader();
    ~kPackageReader();

    /**
     * @brief Opens a .kpak file and loads its index.
     * @param packagePath Absolute path to the .kpak file.
     * @return true on success.
     */
    bool open(const kString& packagePath);

    /** @brief Closes the package and releases resources. */
    void close();

    /** @brief Returns true if a package is currently open. */
    bool isOpen() const;

    /**
     * @brief Reads and decompresses a single file from the package.
     * @param virtualPath Path as stored in the package (forward slashes, relative).
     * @param outData    Receives the uncompressed file contents.
     * @return true if the file was found and successfully read.
     */
    bool readFile(const kString& virtualPath, std::vector<uint8_t>& outData);

    /**
     * @brief Reads a file as a string.
     * @param virtualPath Path as stored in the package.
     * @param outString Receives the file contents as a string.
     * @return true on success.
     */
    bool readFileString(const kString& virtualPath, kString& outString);

    /**
     * @brief Checks whether a virtual path exists in the package index.
     * @param virtualPath Path to test.
     * @return true if the path is listed in the index.
     */
    bool fileExists(const kString& virtualPath) const;

    /**
     * @brief Returns the uncompressed size of an entry, or 0 if not found.
     * @param virtualPath Path to query.
     */
    uint32_t getFileSize(const kString& virtualPath) const;

    /** @brief Returns the path of the currently open package file. */
    const kString& getPackagePath() const { return m_packagePath; }

private:
    kString m_packagePath;
    std::ifstream m_file;
    bool m_hasCompressed = false;
    std::unordered_map<kString, kPackageEntry> m_index;
};

/**
 * @brief Creates a .kpak package from a directory of assets.
 *
 * Walks a directory tree, optionally compresses each file with Deflate,
 * and writes the concatenated archive with an index header.
 */
class KEMENA3D_API kPackageWriter
{
public:
    /**
     * @brief Compression level for entries added to the package.
     */
    enum class Compression
    {
        None = 0,     ///< Store uncompressed.
        Fast = 1,     ///< Deflate level 1 (fastest).
        Default = 6,  ///< Deflate level 6 (balanced).
        Best = 9      ///< Deflate level 9 (smallest).
    };

    kPackageWriter();
    ~kPackageWriter();

    /**
     * @brief Creates a .kpak package from a directory.
     * @param outputPath   Destination .kpak file path.
     * @param sourceDir    Root directory to package (files retain relative paths).
     * @param compression  Compression level to apply to each file.
     * @param filterExt    If non-empty, only include files with this extension.
     * @return true on success.
     */
    bool create(const kString& outputPath, const kString& sourceDir,
                Compression compression = Compression::Default,
                const kString& filterExt = "");

private:
    /**
     * @brief Compresses @p inData with raw Deflate (no zlib/gzip wrapper).
     * @param inData      Uncompressed input.
     * @param outData     Receives the compressed output.
     * @param level       Compression level 0-9 (0 = store, 1 = fast, 9 = best).
     * @return true on success.
     */
    static bool deflateCompress(const std::vector<uint8_t>& inData,
                                std::vector<uint8_t>& outData,
                                int level);

    /**
     * @brief Decompresses raw Deflate data.
     * @param inData            Compressed input.
     * @param uncompressedSize  Expected uncompressed size.
     * @param outData           Receives the decompressed output.
     * @return true on success.
     */
    static bool deflateDecompress(const std::vector<uint8_t>& inData,
                                  size_t uncompressedSize,
                                  std::vector<uint8_t>& outData);

    void collectFiles(const kString& dir, const kString& baseDir,
                      std::vector<std::pair<kString, kString>>& outFiles,
                      const kString& filterExt);
};

} // namespace kemena

#endif // KPACKAGE_H
