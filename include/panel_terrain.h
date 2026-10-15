/**
 * @file panel_terrain.h
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#ifndef PANEL_TERRAIN_H
#define PANEL_TERRAIN_H

#include <string>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include <kemena/kemena.h>
#include <kemena/kterrain.h>

#include "manager.h"

namespace fs = std::filesystem;
using namespace kemena;

class Manager;

class PanelTerrain
{
public:
    bool enabled = false;

    struct SculptState
    {
        bool active = false;
        kTerrain::BrushMode mode = kTerrain::BrushMode::Raise;
        float radius = 5.0f;
        float strength = 0.5f;
        float flattenHeight = 0.0f;
        bool needsRebuild = false;
        kTerrain *affectedTile = nullptr;
    };
    SculptState sculpt;

    struct PaintState
    {
        bool active = false;
        int channel = -1;
        bool heightActive = false;
        float strength = 0.5f;
        float fade = 0.3f;
        kString maskTexturePath;
        bool needsRebuild = false;
        kTerrain *affectedTile = nullptr;
    };
    PaintState paint;

    kVec3 paintCursorPos = kVec3(0.0f);
    kVec3 paintCursorNormal = kVec3(0.0f, 1.0f, 0.0f);
    bool paintCursorValid = false;

    PanelTerrain(kGuiManager *setGuiManager, Manager *setManager);

    void draw(bool &isOpened);
    void onTerrainFileRenamed(const kString &oldPath, const kString &newPath);

    /**
     * @brief Captures a tile's height and/or weight buffers before an edit so
     *        the change can be undone.
     *
     * Safe to call repeatedly during a stroke: only the first call for a given
     * tile records its "before" state, so the whole stroke collapses into a
     * single undo step once commitTerrainEdit() runs.
     * @param tile   Tile about to be modified.
     * @param height Capture the height field.
     * @param weight Capture the weight (splat) map.
     */
    void beginTerrainEdit(kTerrain *tile, bool height, bool weight);

    /**
     * @brief Ends the current edit and pushes one undoable command describing
     *        every tile changed since the matching beginTerrainEdit() call.
     *
     * No command is pushed when nothing actually changed.
     */
    void commitTerrainEdit();

    /// @brief Discards the in-progress edit without recording it in the history.
    void cancelTerrainEdit();

    /**
     * @brief Removes the terrain tile at a grid coordinate.
     *
     * The tile's scene mesh is deleted, so if it is currently selected the
     * selection is cleared first to avoid leaving a dangling pointer.
     * @param gridX Integer X coordinate.
     * @param gridZ Integer Z coordinate.
     */
    void removeTerrainTile(int gridX, int gridZ);
    void loadProjectTerrains();
    void saveProjectTerrains() const;
    void sculptAtWorldPos(const kVec3 &worldPos);
    void rebuildSculptedTile();
    void paintAtWorldPos(const kVec3 &worldPos, bool lower);
    void rebuildPaintedTile();
    void paintSplatAtWorldPos(const kVec3 &worldPos, bool erase);
    void rebuildPaintedSplatTile();
    void applyNoiseToTile(kTerrain *tile, float scale, float amplitude,
                          int octaves, int seed, bool additive);

    /**
     * @brief Hydraulic ("droplet") erosion of a tile's heightfield — each droplet
     *        follows the gradient, eroding into its brush footprint and depositing
     *        sediment in pits. Adapted from weigert/SimpleErosion.
     */
    void applyErosionToTile(kTerrain *tile, int droplets, float radius,
                            float inertia, float capacityFactor, float depositFactor,
                            float erodeFactor, float evaporate, float gravity,
                            float minSediment, unsigned int seed);

    /** @brief Thermal/talus erosion: relaxes slopes steeper than @p talus (SoilMachine). */
    void applyThermalErosionToTile(kTerrain *tile, int iterations, float talus, float strength);

    /** @brief Directional wind advection: material is blown along @p dirRadians
     *         (weigert/SimpleWindErosion). */
    void applyWindErosionToTile(kTerrain *tile, float dirRadians, float strength,
                                int iterations, float talus);

    /**
     * @brief Flow-driven river carving using the stream-power law
     *        (erosion ~ k * A^m * S^n) on a D8 flow-accumulation field.
     *        Adapted from weigert/SimpleHydrology, erodr and Terrain-Builder.
     */
    void applyHydrologyToTile(kTerrain *tile, int iterations, float k, float m, float n,
                              float deposition, float minSlope);

private:
    void addTerrainTile(int gridX, int gridZ);

    /// @brief Rebuilds the meshes of the (up to four) loaded tiles adjacent to a coord.
    void rebuildNeighborMeshes(int gridX, int gridZ);

    /// @brief Ensures every terrain tile has a live GPU mesh.
    ///
    /// Runs every frame with no user interface. The editor never unloads tiles
    /// (unloading deletes the tile's scene mesh, which could leave the current
    /// selection dangling), so this only ever loads.
    void ensureTilesLoaded();

    static kString getDataDirForTerrain(const kString &terrainFilePath);
    static float noiseValue(int x, int y, int seed);
    static float noiseSmooth(float x, float y, int seed);

    kGuiManager *gui = nullptr;
    Manager *manager = nullptr;

    /// "Before" buffers captured at the start of an in-progress stroke, one per
    /// affected tile. Diffs against the live buffers are turned into a
    /// TerrainEditCommand by commitTerrainEdit().
    struct EditSnapshot
    {
        kTerrain *tile = nullptr;             ///< Tile being edited.
        bool height = false;                  ///< Height field captured.
        bool weight = false;                  ///< Weight map captured.
        std::vector<float> heightBefore;      ///< Height copy at stroke start.
        std::vector<unsigned char> weightBefore; ///< Weight copy at stroke start.
    };
    std::vector<EditSnapshot> m_editSnapshots;
    EditSnapshot *findOrCreateEditSnapshot(kTerrain *tile);

    float m_defaultHeight = 0.0f;

    float m_noiseScale = 50.0f;
    float m_noiseAmplitude = 10.0f;
    int m_noiseOctaves = 4;
    int m_noiseSeed = 42;
    bool m_noiseAdditive = false;
    int m_noiseType = 0; ///< 0 = fBm, 1 = ridged, 2 = billowed.

    // --- Erosion parameters ---------------------------------------------------
    int   m_erodeDroplets = 50000;
    float m_erodeRadius = 4.0f;
    float m_erodeInertia = 0.05f;
    float m_erodeCapacity = 4.0f;
    float m_erodeDeposit = 0.3f;
    float m_erodeErode = 0.3f;
    float m_erodeEvaporate = 0.01f;
    float m_erodeGravity = 4.0f;

    int   m_thermalIterations = 8;
    float m_thermalTalus = 0.4f;
    float m_thermalStrength = 0.5f;

    float m_windDir = 0.0f; ///< Degrees.
    float m_windStrength = 0.3f;

    int   m_hydroIterations = 6;
    float m_hydroK = 0.02f;
    float m_hydroM = 0.5f;
    float m_hydroN = 1.0f;
    float m_hydroDeposit = 0.1f;

    kString m_terrainDirectory;

    /// Text for the deferred Tile Management message popup.
    kString m_messageBoxText;
    /// Set to open the message popup on the next frame.
    bool m_messageBoxPending = false;
};

#endif // PANEL_TERRAIN_H
