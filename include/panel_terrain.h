/**
 * @file panel_terrain.h
 * @brief Editor panel for managing grid-based terrain tiles.
 */

#ifndef PANEL_TERRAIN_H
#define PANEL_TERRAIN_H

#include <string>
#include <filesystem>
#include <functional>
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
        /// Target material layer (0-based). Starts on the first layer so the brush
        /// is always aimed at a valid layer.
        int channel = 0;
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

    /// @brief When true the selected tile's physics collider is drawn as a
    ///        wireframe in the viewport (Terrain > Terrain Physics).
    bool showCollider = false;

    /// @brief True while the layer texture picker is on screen.
    ///
    /// The editor's raw SDL camera/picking handlers run before ImGui refreshes its
    /// capture state, so they consult this flag to avoid acting through the picker.
    bool isTexturePickerOpen() const { return m_texPickOpen; }

    PanelTerrain(kGuiManager *setGuiManager, Manager *setManager);

    void draw(bool &isOpened);

    /**
     * @brief Draws the Terrain Size / resolution section and its confirm modal.
     *
     * Safe to call when the scene has no terrain yet (or there is no terrain
     * manager): the staged values are then applied once the first tile exists.
     */
    void drawTerrainSizeSettings(kTerrainManager *terrainMgr);

    /**
     * @brief Resolves the terrain tile the editor currently has selected.
     *
     * Consults both the singular active object and the plural selection list that
     * the Hierarchy maintains; returns nullptr when no tile is selected.
     */
    kTerrain *getSelectedTerrainTile(kTerrainManager *terrainMgr);
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
    void saveProjectTerrains();
    void sculptAtWorldPos(const kVec3 &worldPos);
    void rebuildSculptedTile();
    void paintAtWorldPos(const kVec3 &worldPos, bool lower);
    void rebuildPaintedTile();
    void paintSplatAtWorldPos(const kVec3 &worldPos, bool erase);
    void rebuildPaintedSplatTile();

    /**
     * @brief Fills one material layer's whole weight map with @p value.
     *
     * Recorded as a single undoable step and written to the tile's owned payload,
     * so "Fill Layer" (0 = black) and "Clear Layer" (255 = white) behave like one
     * giant brush stroke.
     * @param tile  Tile to edit.
     * @param layer Target material layer (0..kMaxTerrainLayers-1).
     * @param value Weight written to every sample (0..255).
     */
    void fillLayerWeight(kTerrain *tile, int layer, unsigned char value);

    // --- Owned terrain assets ---

    /**
     * @brief Points the terrain manager at the folder that owns tile payloads.
     *
     * Tiles then create/read/delete their own `.height`, `.splat`, `.hlayers`
     * and `.mat` files there — the user never assigns any of them.
     */
    void setTerrainAssetDirectory(const kString &directory);

    /** @brief Writes every tile's owned height / splat / layer / material files. */
    void saveAllOwnedAssets();

    // --- Non-destructive height modifier layers ---

    /**
     * @brief Adds, updates or re-generates one height modifier layer.
     *
     * When @p generator is non-null the layer's offsets are rebuilt from the
     * composite *below* the layer, so re-applying the same settings replaces the
     * previous result instead of accumulating on top of it.
     *
     * @param tile        Target tile.
     * @param layerIndex  Existing layer to update, or -1 to append a new one.
     * @param desc        New layer parameters.
     * @param generator   When non-null, re-runs the operator to rebuild the
     *                    layer's offsets from the composite below it.
     * @param recordUndo  Push one undo step (false for live auto-apply edits).
     * @param persist     Write the tile's owned assets immediately (false while a
     *                    live edit is still in progress).
     * @return The layer index, or -1 on failure.
     */
    int applyHeightLayer(kTerrain *tile, int layerIndex,
                         const kTerrainHeightLayer &desc,
                         const std::function<void(const std::vector<float> &input,
                                                  std::vector<float> &output)> &generator,
                         bool recordUndo = true, bool persist = true);

    /** @brief Removes a height modifier layer as one undoable step. */
    void removeHeightLayerUndoable(kTerrain *tile, int layerIndex);

    /**
     * @brief Applies the panel's "Tile Height" value to @p tile and saves it.
     *
     * Fills the tile's base height field with @ref m_defaultHeight and clears
     * every effect layer's stored delta, so the tile reads flat at exactly that
     * height. Recorded as one undoable step and written to disk immediately.
     */
    void applyTileHeight(kTerrain *tile);

    /**
     * @brief Resets a tile's height map.
     *
     * The sculpted base returns to the panel's default height and every effect
     * layer's contribution (its stored delta) is zeroed, so the tile reads flat
     * again. Recorded as one undoable step for the base field.
     */
    void clearSelectedTileHeight(kTerrain *tile);

    /**
     * @brief Removes one material layer from @p tile.
     *
     * The per-sample weight map is packed by layer index, so deleting a layer
     * shifts every higher layer's weights down into the freed slot (otherwise the
     * painted masks would end up on the wrong materials). Mask-by-layer references
     * are renumbered, the texture arrays/uniforms are repacked, the mesh is
     * rebuilt and the tile's owned payload is written. Like adding a layer, this
     * is not part of the undo history.
     * @param tile  Tile to edit.
     * @param index Layer index to delete.
     */
    void removeTerrainLayer(kTerrain *tile, int index);

    /**
     * @brief Appends the wireframe edges of @p tile's physics collider to @p out.
     *
     * The collider mirrors the tile's height field; the grid is decimated so a
     * full-resolution tile does not flood the debug-line buffer. Each pair of
     * consecutive points is one world-space line segment.
     */
    void appendTileColliderLines(const kTerrain *tile, std::vector<kVec3> &out) const;

    /**
     * @brief Builds (or refreshes) @p tile's HeightField collider at @p resolution.
     *
     * The terrain height grid is bilinearly resampled to @p resolution samples per
     * side, so a higher value produces more triangles and a more accurate collision
     * mesh. The resolution is stored in the shape descriptor and serialised.
     */
    void generateTileCollider(kTerrain *tile, int resolution);

    /**
     * @brief Rebuilds a tile's render mesh, keeping the editor selection valid.
     *
     * kTerrain::rebuildMesh() deletes and re-creates the tile's kMesh, so any raw
     * pointer the editor holds to the old mesh (the active selection) would dangle
     * and crash e.g. the Inspector on the next frame. This re-points the selection
     * at the rebuilt mesh (rebuildMesh preserves the UUID, so the uuid lists stay
     * valid).
     */
    void rebuildTileMesh(kTerrain *tile);

    /**
     * @brief Builds the generator functor for a height layer descriptor.
     *
     * The functor seeds the tile's height field with the layer's input, runs the
     * matching operator (noise / hydraulic / thermal / wind / hydrology) and hands
     * the absolute result back so the layer's delta can be derived from it.
     */
    std::function<void(const std::vector<float> &input, std::vector<float> &output)>
    makeHeightLayerGenerator(kTerrain *tile, const kTerrainHeightLayer &layer);

    /** @brief Re-runs a layer's generator in place, as one undoable step. */
    void regenerateHeightLayer(kTerrain *tile, int layerIndex);

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

    /**
     * @brief Scratch copy of a height-modifier layer while its tree node is open.
     *
     * ImGui widgets bind to the address they are given, so editing a short-lived
     * stack copy made every change snap back to the stored value on the next
     * frame. This buffer keeps the live descriptor alive while the node is open.
     * Every widget change is applied immediately; the buffer also owns the
     * "before" state so a whole drag collapses into one undo step.
     */
    struct LayerEditBuffer
    {
        kTerrain *tile = nullptr;          ///< Tile the layer belongs to.
        int index = -1;                    ///< Layer index within the tile.
        kTerrainHeightLayer desc;          ///< In-progress layer descriptor.
        kTerrainHeightLayer undoBefore;    ///< Stored layer before the current edit.
        bool undoPending = false;          ///< True while live edits await one undo step.
    };
    std::vector<LayerEditBuffer> m_layerEditBuffers;

    /**
     * @brief Records one undo step for an auto-applied layer edit.
     *
     * The live edit has already been applied, so this only pushes the command
     * describing the change and persists the tile's owned payload.
     * @param tile   Edited tile.
     * @param index  Layer index that changed.
     * @param before Layer descriptor captured before the first live change.
     */
    void commitHeightLayerAutoEdit(kTerrain *tile, int index,
                                   const kTerrainHeightLayer &before);

    /**
     * @brief Returns the persistent edit buffer for a layer, seeding it from
     *        @p current the first time the layer is shown.
     */
    LayerEditBuffer &layerEditBufferFor(kTerrain *tile, int index,
                                        const kTerrainHeightLayer &current);

    /** @brief Discards every in-progress layer edit belonging to @p tile. */
    void clearLayerEditBuffers(kTerrain *tile);

    // --- Terrain material-layer texture picker (modal) ---
    kTerrain *m_texPickTile = nullptr; ///< Tile whose layer texture is being chosen.
    int m_texPickLayer = -1;           ///< Layer index within that tile.
    int m_texPickSlot = 0;             ///< 0 = Albedo, 1 = Normal, 2 = ORM.
    kString m_texPickSelected;         ///< Asset UUID chosen in the picker.
    bool m_texPickOpen = false;        ///< True while the picker window is shown.
    char m_texPickSearch[128] = "";

    /**
     * @brief Draws the "Select Terrain Texture" picker window.
     *
     * Deliberately a self-managed top-level window (with its own full-viewport
     * input blocker) instead of an ImGui popup: popups were being dismissed by
     * unrelated clicks and by the raw input the viewport consumes.
     */
    void drawTexturePickerPopup();

    // --- Terrain material parameter staging ---
    // Tiling is deliberately NOT here: it is per material layer
    // (kTerrainLayer::tiling) and edited in each layer's tree node.
    struct MaterialSettings
    {
        /// Terrain shading model: 0 = Flat, 1 = Phong, 2 = PBR.
        int shaderType = 2;
        float blendSharpness = 1.0f;
        bool stochastic = true;
        bool triplanar = true;
        bool rotateUV = false;
        float uvRotation = 0.0f; ///< Degrees.
        float triplanarStrength = 0.75f;
        float triplanarSharpness = 3.0f;
        float noiseVariegation = 0.25f;
        float noiseScale = 0.02f;
        bool noiseEnabled = false; ///< Procedural noise variegation on/off.
    };
    kTerrain *m_matSettingsTile = nullptr; ///< Tile the staged settings belong to.
    MaterialSettings m_matSettings;

    /// Desired HeightField collider resolution (samples per side).
    int m_collisionResolution = 129;
    kTerrain *m_collisionResTile = nullptr; ///< Tile the staged resolution belongs to.

    float m_defaultHeight = 0.0f;

    // --- Terrain size / resolution settings (applied only after confirmation) ---
    float m_settingsWorldSize = 256.0f; ///< Edited value in the Terrain panel.
    int m_settingsHeightRes = 513;      ///< Edited value in the Terrain panel.
    float m_pendingWorldSize = 256.0f;  ///< Value awaiting the confirmation modal.
    int m_pendingHeightRes = 513;       ///< Value awaiting the confirmation modal.
    bool m_resizeConfirmPending = false; ///< Set to open the resize warning modal.
    /// Set when a size / resolution change was confirmed before any tile existed,
    /// so the staged values are applied to the manager on first tile creation.
    bool m_applySizeOnCreate = false;
    /// Set to open the "Clear Height Map" confirmation modal.
    bool m_clearHeightPending = false;

    /// @brief Applies a confirmed world size / resolution change to every tile.
    void applyTerrainResize(float worldSize, int heightRes);

    float m_noiseScale = 50.0f;
    float m_noiseAmplitude = 10.0f;
    int m_noiseOctaves = 4;
    int m_noiseSeed = 42;
    bool m_noiseAdditive = true;
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

    /// Re-entrancy guard: rebuilding one tile also refreshes its neighbours so the
    /// stitched seams stay closed, and a neighbour rebuild must not bounce back.
    bool m_rebuildingMesh = false;
};

#endif // PANEL_TERRAIN_H
