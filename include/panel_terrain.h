/**
 * @file panel_terrain.h
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#ifndef PANEL_TERRAIN_H
#define PANEL_TERRAIN_H

#include <string>
#include <filesystem>
#include <unordered_map>

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
    void expandTerrain(int gridX, int gridZ, const kString &direction);
    static kString getDataDirForTerrain(const kString &terrainFilePath);
    static float noiseValue(int x, int y, int seed);
    static float noiseSmooth(float x, float y, int seed);

    kGuiManager *gui = nullptr;
    Manager *manager = nullptr;

    float m_newTileSize = 256.0f;
    int m_newHeightRes = 513;
    float m_defaultHeight = 0.0f;
    int m_loadRadius = 512;
    int m_unloadRadius = 768;
    bool m_autoUpdate = true;

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
};

#endif // PANEL_TERRAIN_H
