/**
 * @file kterrain.h
 * @brief Grid-based terrain system with polygon-based sculpting, layered materials,
 *        seamless neighbor stitching, and load/unload based on player position.
 *
 * Sculpting edits vertex Y positions (polygons) directly — no height map texture
 * or vertex shader displacement is used.  The shader only handles material splatting.
 */

#ifndef KTERRAIN_H
#define KTERRAIN_H

#include "kexport.h"
#include "kdatatype.h"
#include "kobject.h"
#include "kmesh.h"
#include "kmaterial.h"
#include "ktexture2d.h"
#include "ktexture.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace kemena
{

    class kScene;
    class kAssetManager;
    class kRenderer;

    // ---------------------------------------------------------------------------
    // Terrain Material Layer
    // ---------------------------------------------------------------------------

    /**
     * @brief Describes one material layer in the terrain's layered material system.
     *
     * Each layer has a base material (albedo/normal/etc.), a tile size for UV
     * scaling, and a height range that controls where the layer is applied.
     */
    struct KEMENA3D_API kTerrainLayer
    {
        kString name;                  ///< Display name (e.g. "Grass", "Rock", "Snow").
        kMaterial *material = nullptr; ///< Material asset for this layer (owned externally).
        float tileSize = 1.0f;         ///< World-space size of one texture repeat.
        float minHeight = 0.0f;        ///< Minimum height (world units) where this layer appears.
        float maxHeight = 1.0f;        ///< Maximum height (world units) where this layer appears.
        float slopeMin = 0.0f;         ///< Minimum slope angle in degrees for this layer.
        float slopeMax = 90.0f;        ///< Maximum slope angle in degrees for this layer.

        void serialize(json &j) const;
        void deserialize(const json &j);
    };

    // ---------------------------------------------------------------------------
    // Terrain Tile
    // ---------------------------------------------------------------------------

    /**
     * @brief A single terrain tile — a heightfield mesh with layered materials.
     *
     * Each tile occupies a fixed-size world-space region and is identified by
     * its integer grid coordinate (gridX, gridZ).  The tile generates a GPU mesh
     * from its height data.
     *
     * Sculpting edits vertex Y positions directly (polygon editing) — no height
     * map texture or vertex shader displacement is used.  The shader only
     * performs material splatting.
     *
     * The mesh is registered with the owning kScene so it participates in
     * frustum culling and rendering like any other mesh.
     *
     * Neighbor references are stored so that adjacent tiles can be stitched
     * together (seamless edges) by adjusting the outer ring of vertices.
     */
    class KEMENA3D_API kTerrain : public kObject
    {
    public:
        /**
         * @brief Constructs an empty terrain tile.
         * @param scene         Owning scene (for mesh registration).
         * @param assetManager  Asset manager for loading textures/materials.
         * @param gridX         Integer X coordinate in the terrain grid.
         * @param gridZ         Integer Z coordinate in the terrain grid.
         * @param worldSize     World-space size of the tile (assumed square; e.g. 256 units).
         * @param heightRes     Number of height samples per side (e.g. 513 produces 512 quads).
         */
        kTerrain(kScene *scene, kAssetManager *assetManager,
                 int gridX, int gridZ, float worldSize = 256.0f, int heightRes = 513);
        ~kTerrain();

        // --- Sculpting brush types -----------------------------------------------

        /** @brief Brush operation types for terrain sculpting. */
        enum class BrushMode
        {
            Raise,   ///< Increase height at brush centre.
            Lower,   ///< Decrease height at brush centre.
            Flatten, ///< Pull heights toward the initial sample height under the brush centre.
            Smooth   ///< Average heights within the brush radius.
        };

        // --- Grid identity -------------------------------------------------------

        /** @brief Returns the X coordinate in the terrain grid. */
        int getGridX() const { return m_gridX; }

        /** @brief Returns the Z coordinate in the terrain grid. */
        int getGridZ() const { return m_gridZ; }

        /** @brief Returns the world-space size of the tile (square). */
        float getWorldSize() const { return m_worldSize; }

        /** @brief Returns the number of height samples per side. */
        int getHeightRes() const { return m_heightRes; }

        /** @brief Returns the world-space position of the tile's corner (min X, min Z). */
        kVec3 getWorldPosition() const;

        // --- Height data ---------------------------------------------------------

        /**
         * @brief Returns a pointer to the raw height data (float array, heightRes*heightRes).
         *
         * The data is stored row-major; index = z * heightRes + x.
         */
        const float *getHeightData() const { return m_heightData.data(); }
        float *getHeightData() { return m_heightData.data(); }

        /** @brief Returns the number of height samples (heightRes * heightRes). */
        size_t getHeightDataSize() const { return m_heightData.size(); }

        /**
         * @brief Samples the height at a world-space position (bilinear interpolation).
         * @param worldPos World-space XZ position (Y is ignored).
         * @return Interpolated height, or 0 if outside the tile.
         */
        float sampleHeight(const kVec3 &worldPos) const;

        /**
         * @brief Sets the height at a given heightmap coordinate.
         * @param x      X index [0, heightRes-1].
         * @param z      Z index [0, heightRes-1].
         * @param value  New height value.
         */
        void setHeight(int x, int z, float value);

        /**
         * @brief Loads height data from a raw float binary file.
         * @param path Path to the .raw height file.
         * @return true on success.
         */
        bool loadHeightData(const kString &path);

        /**
         * @brief Saves height data to a raw float binary file.
         * @param path Path to the .raw height file.
         * @return true on success.
         */
        bool saveHeightData(const kString &path) const;

        /**
         * @brief Applies a sculpting brush stamp at a world-space position.
         *
         * Modifies height values within the brush radius using the specified
         * operation (raise, lower, flatten, smooth).  After modifying height
         * data, call updateMesh() (fast) or rebuildMesh() (full rebuild)
         * to update the GPU vertex buffer.
         *
         * @param worldPos  World-space XZ position of the brush centre.
         * @param radius    World-space brush radius.
         * @param strength  Brush opacity [0, 1]. 1.0 = full effect.
         * @param mode      Brush operation (Raise, Lower, Flatten, Smooth).
         * @param flattenTargetHeight  Heights are pulled toward this value in Flatten mode.
         */
        void applyBrush(const kVec3 &worldPos, float radius, float strength,
                        BrushMode mode, float flattenTargetHeight = 0.0f);

        /**
         * @brief Converts a world-space position to the nearest heightmap coordinates.
         * @param worldPos World-space XZ position.
         * @param outX     Output heightmap X index.
         * @param outZ     Output heightmap Z index.
         * @return true if the position is inside this tile.
         */
        bool worldToHeightmap(const kVec3 &worldPos, int &outX, int &outZ) const;

        /**
         * @brief Converts heightmap coordinates to a world-space XZ position.
         * @param x Heightmap X index.
         * @param z Heightmap Z index.
         * @return World-space position (Y = 0).
         */
        kVec3 heightmapToWorld(int x, int z) const;

        /**
         * @brief Fills the entire heightmap with a flat value and rebuilds the mesh.
         * @param value Height value.
         */
        void fillHeight(float value);

        // --- Splat map (material blending) ---------------------------------------

        /**
         * @brief Returns a pointer to the splat data (RGBA8, heightRes*heightRes*4).
         *
         * Each pixel stores blend weights for up to 4 material layers.
         * The values are normalised (sum to 1.0 per pixel).
         */
        const unsigned char *getSplatData() const { return m_splatData.data(); }
        unsigned char *getSplatData() { return m_splatData.data(); }

        /** @brief Returns the splat data size in bytes. */
        size_t getSplatDataSize() const { return m_splatData.size(); }

        /**
         * @brief Loads a splat map from a PNG file (RGBA8).
         * @param path Path to the PNG splat map.
         * @return true on success.
         */
        bool loadSplatMap(const kString &path);

        /**
         * @brief Saves the splat map to a PNG file (RGBA8).
         * @param path Output path for the PNG splat map.
         * @return true on success.
         */
        bool saveSplatMap(const kString &path) const;

        /**
         * @brief Saves the height map as a normalized grayscale PNG.
         * @param path Output path for the PNG file.
         * @return true on success.
         */
        bool saveHeightMapPng(const kString &path) const;

        // --- Material layers ---------------------------------------------------

        /** @brief Returns the list of terrain material layers. */
        const std::vector<kTerrainLayer> &getLayers() const { return m_layers; }
        std::vector<kTerrainLayer> &getLayers() { return m_layers; }

        /**
         * @brief Sets the material for a specific layer.
         * @param index    Layer index [0, 3].
         * @param material Material to assign.
         */
        void setLayerMaterial(int index, kMaterial *material);

        /**
         * @brief Adds or updates a terrain layer.
         * @param layer Layer descriptor.
         */
        void setLayer(const kTerrainLayer &layer);

        // --- Mesh and rendering -------------------------------------------------

        /** @brief Returns the generated terrain kMesh (registered with the scene). */
        kMesh *getMesh() const { return m_mesh; }

        /**
         * @brief Rebuilds the GPU mesh from the current height data.
         *
         * Generates vertex positions with actual Y heights, computes normals
         * from geometry, and uploads to GPU.  No height map texture or vertex
         * shader displacement is used — the polygon data carries the height.
         *
         * @param stitchNeighbors If true, adjusts edge vertices to match neighbor heights.
         */
        void rebuildMesh(bool stitchNeighbors = true);

        /**
         * @brief Fast update: updates vertex Y positions and normals on the GPU
         *        using glBufferSubData, without full mesh recreation.
         *
         * Call during brush stroke for live feedback; call rebuildMesh() on
         * mouse-up for full quality (tangents, AABB, physics).
         */
        void updateMesh();

        /**
         * @brief Fast update: uploads the current splat data to the GPU splat
         *        texture via glTexSubImage2D (no geometry changes).
         */
        void updateSplatTexture();

        // --- Neighbor stitching --------------------------------------------------

        /**
         * @brief Sets a neighbor tile reference for edge stitching.
         * @param direction "north", "south", "east", or "west".
         * @param neighbor  Pointer to the neighboring kTerrain, or nullptr to clear.
         */
        void setNeighbor(const kString &direction, kTerrain *neighbor);

        /** @brief Returns the neighbor tile in the given direction, or nullptr. */
        kTerrain *getNeighbor(const kString &direction) const;

        // --- Load / unload -------------------------------------------------------

        /** @brief Returns true if the tile is currently loaded (has GPU mesh). */
        bool isLoaded() const { return m_loaded; }

        /**
         * @brief Unloads the tile: destroys GPU mesh and splat texture.
         *
         * Height and splat data are retained in memory so the tile can be reloaded.
         */
        void unload();

        /**
         * @brief Reloads the tile: regenerates GPU mesh.
         * @param stitchNeighbors If true, stitches edges with loaded neighbors.
         */
        void reload(bool stitchNeighbors = true);

        // --- Serialization -------------------------------------------------------

        /**
         * @brief Serializes terrain metadata to JSON.
         *
         * Height and splat data are NOT included — they live in the _data folder.
         */
        void serialize(json &j) const;

        /**
         * @brief Deserializes terrain metadata from JSON.
         *
         * Height and splat data are loaded separately from the _data folder.
         */
        void deserialize(const json &j);

    private:
        /** @brief Computes the world-space X coordinate of a heightmap sample. */
        float sampleToWorldX(int x) const;

        /** @brief Computes the world-space Z coordinate of a heightmap sample. */
        float sampleToWorldZ(int z) const;

        /** @brief Returns the height sample at (x, z) with neighbor stitching applied. */
        float getStitchedHeight(int x, int z) const;

        /** @brief Computes the normal at a heightmap sample using central differences. */
        kVec3 computeNormal(int x, int z) const;

        /** @brief Creates the GPU splat texture (RGBA8) and uploads splat data. */
        void createSplatTexture();

        kScene *m_scene = nullptr;
        kAssetManager *m_assetManager = nullptr;
        kMesh *m_mesh = nullptr;
        kMaterial *m_terrainMaterial = nullptr; ///< Combined terrain shader material.

        int m_gridX = 0;
        int m_gridZ = 0;
        float m_worldSize = 256.0f;
        int m_heightRes = 513;
        float m_sampleSpacing = 0.0f; ///< World-space distance between height samples.

        std::vector<float> m_heightData;        ///< heightRes * heightRes floats.
        std::vector<unsigned char> m_splatData; ///< heightRes * heightRes * 4 bytes (RGBA).
        std::vector<kTerrainLayer> m_layers;    ///< Up to 4 material layers.

        uint32_t m_splatTexture = 0; ///< GPU RGBA8 splat texture

        // Neighbors for stitching (N, S, E, W)
        kTerrain *m_neighborNorth = nullptr;
        kTerrain *m_neighborSouth = nullptr;
        kTerrain *m_neighborEast = nullptr;
        kTerrain *m_neighborWest = nullptr;

        bool m_loaded = false;
    };

    // ---------------------------------------------------------------------------
    // Terrain Manager
    // ---------------------------------------------------------------------------

    /**
     * @brief Manages a grid of kTerrain tiles, handling load/unload based on
     *        player proximity and coordinating seamless neighbor stitching.
     *
     * Typical usage:
     * @code
     *   kTerrainManager terrainMgr;
     *   terrainMgr.init(scene, assetMgr, worldSize, heightRes);
     *
     *   // Each frame:
     *   terrainMgr.update(playerPosition, loadRadius);
     * @endcode
     */
    class KEMENA3D_API kTerrainManager
    {
    public:
        kTerrainManager();
        ~kTerrainManager();

        /**
         * @brief Initializes the terrain manager.
         * @param scene         Owning scene for mesh registration.
         * @param assetManager  Asset manager for loading textures.
         * @param worldSize     World-space size of each tile (square).
         * @param heightRes     Number of height samples per side per tile.
         */
        void init(kScene *scene, kAssetManager *assetManager,
                  float worldSize = 256.0f, int heightRes = 513);

        /**
         * @brief Updates terrain loading based on player position.
         *
         * Tiles within @p loadRadius of @p playerPos are loaded (if not already).
         * Tiles outside @p unloadRadius are unloaded.
         *
         * @param playerPos    World-space player position (XZ used, Y ignored).
         * @param loadRadius   Distance within which tiles are loaded.
         * @param unloadRadius Distance beyond which tiles are unloaded.
         */
        void update(const kVec3 &playerPos, float loadRadius = 512.0f, float unloadRadius = 768.0f);

        /**
         * @brief Creates a terrain tile at the given grid coordinate.
         *
         * If a tile already exists at that coordinate, returns the existing one.
         *
         * @param gridX Integer X coordinate.
         * @param gridZ Integer Z coordinate.
         * @return Pointer to the terrain tile.
         */
        kTerrain *createTile(int gridX, int gridZ);

        /**
         * @brief Removes a terrain tile at the given grid coordinate.
         * @param gridX Integer X coordinate.
         * @param gridZ Integer Z coordinate.
         * @return true if a tile was removed.
         */
        bool removeTile(int gridX, int gridZ);

        /**
         * @brief Returns the terrain tile at the given grid coordinate, or nullptr.
         */
        kTerrain *getTile(int gridX, int gridZ) const;

        /**
         * @brief Returns all currently existing tiles.
         */
        const std::unordered_map<uint64_t, std::unique_ptr<kTerrain>> &getTiles() const { return m_tiles; }

        /**
         * @brief Returns the grid coordinate for a world-space position.
         */
        static void worldToGrid(const kVec3 &worldPos, float worldSize, int &outGridX, int &outGridZ);

        /**
         * @brief Converts grid coordinates to the world-space position of the tile's corner.
         */
        static kVec3 gridToWorld(int gridX, int gridZ, float worldSize);

        /**
         * @brief Packs two grid coordinates into a single 64-bit key for map lookups.
         */
        static uint64_t gridKey(int gridX, int gridZ);

        /**
         * @brief Loads all terrain tiles from .terrain files in a directory.
         * @param directory Path to the folder containing .terrain files.
         */
        void loadFromDirectory(const kString &directory);

        /**
         * @brief Saves all terrain tiles to .terrain files.
         * @param directory Path to the output folder.
         */
        void saveToDirectory(const kString &directory) const;

        /**
         * @brief Unloads all tiles and clears the manager.
         */
        void clear();

        /** @brief Returns the number of currently loaded tiles. */
        int getLoadedCount() const;

        /** @brief Returns the total number of tiles. */
        int getTotalCount() const { return static_cast<int>(m_tiles.size()); }

        /** @brief Returns the world size of each tile. */
        float getWorldSize() const { return m_worldSize; }

        /** @brief Returns the height resolution of each tile. */
        int getHeightRes() const { return m_heightRes; }

    private:
        /**
         * @brief Updates neighbor references for all tiles so stitching works.
         */
        void updateNeighbors();

        /**
         * @brief Loads a tile (creates GPU mesh).
         */
        void loadTile(kTerrain *terrain);

        /**
         * @brief Unloads a tile (destroys GPU mesh).
         */
        void unloadTile(kTerrain *terrain);

        kScene *m_scene = nullptr;
        kAssetManager *m_assetManager = nullptr;
        float m_worldSize = 256.0f;
        int m_heightRes = 513;

        std::unordered_map<uint64_t, std::unique_ptr<kTerrain>> m_tiles;
        std::unordered_set<uint64_t> m_loadedTiles; ///< Set of currently loaded tile keys.
    };

} // namespace kemena

#endif // KTERRAIN_H
