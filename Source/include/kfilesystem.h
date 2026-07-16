/**
 * @file kfilesystem.h
 * @brief Virtual file system that transparently routes I/O to the OS or a .kpak package.
 *
 * In editor/development mode, files are read directly from the project directory.
 * In packaged/runtime mode, files are read from a .kpak archive mounted next to the
 * executable. The API is identical in both modes.
 *
 * Usage:
 * @code
 *   // In main(), before any asset loading:
 *   kFileSystem::init(exeDirectory);
 *
 *   // Then anywhere:
 *   if (kFileSystem::fileExists("scene.world")) { ... }
 *   auto data = kFileSystem::readFile("Library/ImportedAssets/abc.glb");
 *   kString json = kFileSystem::readFileString("game.config");
 * @endcode
 */

#ifndef KFILESYSTEM_H
#define KFILESYSTEM_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>
#include <string>
#include <memory>

namespace kemena
{

class kPackageReader;

/**
 * @brief Global virtual file system singleton.
 *
 * Routes file I/O to either the OS filesystem (editor/development) or a .kpak
 * package (standalone runtime). All paths are relative to the data root.
 */
class KEMENA3D_API kFileSystem
{
public:
    /**
     * @brief Initialises the VFS for the running application.
     *
     * Scans for a .kpak file next to the executable. If found, it is mounted
     * as the data source. Otherwise, the given directory is used as the
     * data root for direct filesystem access.
     *
     * @param exeDir   Directory containing the executable.
     * @param dataDir  Optional override for the data folder name (default: "data").
     */
    static void init(const kString& exeDir, const kString& dataDir = "data");

    /** @brief Shuts down the VFS and releases resources. */
    static void shutdown();

    /** @brief Returns true if running from a .kpak package. */
    static bool isPackaged();

    /**
     * @brief Reads an entire file into a byte buffer.
     * @param relativePath Path relative to the data root (forward slashes).
     * @return File contents, or empty vector if not found.
     */
    static std::vector<uint8_t> readFile(const kString& relativePath);

    /**
     * @brief Reads a text file as a string.
     * @param relativePath Path relative to the data root.
     * @return File contents, or empty string if not found.
     */
    static kString readFileString(const kString& relativePath);

    /**
     * @brief Checks whether a file exists in the VFS.
     * @param relativePath Path relative to the data root.
     * @return true if the file is accessible.
     */
    static bool fileExists(const kString& relativePath);

    /**
     * @brief Returns the uncompressed size of a file, or 0 if not found.
     * @param relativePath Path relative to the data root.
     */
    static size_t getFileSize(const kString& relativePath);

    /**
     * @brief Returns the absolute OS path for files that need direct filesystem
     *        access (e.g. Assimp mesh loading). In packaged mode, this extracts
     *        the file to a temporary location and returns that path.
     *
     * @param relativePath Path relative to the data root.
     * @return Absolute OS path, or empty string if the file cannot be resolved.
     */
    static kString resolveOSPath(const kString& relativePath);

    /** @brief Returns the root data path (package path or data directory). */
    static const kString& getDataRoot();

private:
    static bool s_initialized;
    static bool s_packaged;
    static kString s_dataRoot;
    static std::unique_ptr<kPackageReader> s_package;
};

} // namespace kemena

#endif // KFILESYSTEM_H
