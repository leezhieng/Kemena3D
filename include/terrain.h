/**
 * @file terrain.h
 * @brief Terrain editor for grid-based terrain tiles (drawn inside the Inspector).
 */

#ifndef KEMENA3D_EDITOR_TERRAIN_H
#define KEMENA3D_EDITOR_TERRAIN_H

#include <string>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>
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

    /**
     * @brief One foliage scatter layer: a prefab plus how it is placed.
     *
     * A layer scatters instances of its prefab inside the brush footprint on a
     * jittered grid (cell size → spacing → density), then offsets each instance
     * by the layer's fixed offsets and a per-instance random variance. The
     * brush paints the layer currently selected in the panel, mirroring how the
     * splat brush targets one material layer.
     */
    struct FoliageLayer
    {
        kString name = "Foliage";
        /// Project asset UUID of the .prefab to scatter. Empty = layer can't paint.
        kString prefabUuid;
        bool enabled = true;

        // --- Fixed offsets, applied to every instance ---
        kVec3 positionOffset = kVec3(0.0f);  ///< Added to the sampled terrain position.
        kVec3 rotationOffset = kVec3(0.0f);  ///< Euler degrees added to the instance rotation.
        kVec3 scaleOffset = kVec3(1.0f);     ///< Base instance scale.

        // --- Per-instance random variance (± value) ---
        kVec3 randomPosition = kVec3(0.0f);  ///< Max ± world-unit jitter per axis.
        kVec3 randomRotation = kVec3(0.0f);  ///< Max ± degrees per axis (Y only when yawOnly).
        kVec3 randomScale = kVec3(0.0f);     ///< Max ± scale variance per axis.

        // --- Brush scatter / spacing ---
        float cellSize = 3.0f;   ///< Scatter grid cell size in world units.
        float scatter = 0.75f;   ///< 0..1 fraction of a cell used as random jitter.
        float spacing = 1.5f;    ///< Minimum world distance between two instances.
        float density = 0.6f;    ///< 0..1 probability that a cell produces an instance.
        float maxSlope = 90.0f;  ///< Degrees; cells steeper than this are skipped.

        bool alignToNormal = true; ///< Tilt instances to match the terrain slope.
        bool yawOnly = true;       ///< Apply random rotation about Y only.
        int seed = 1337;           ///< Deterministic scatter seed.
    };
    /// @brief Foliage layers painted by the brush. Always at least one.
    std::vector<FoliageLayer> foliageLayers;

    struct FoliageState
    {
        bool active = false;
        /// Layer the brush scatters (index into foliageLayers).
        int layerIndex = 0;
        float brushRadius = 8.0f;
        /// Set once the current stroke has spawned/erased something, so the
        /// World panel can commit it on mouse-up.
        bool needsCommit = false;
        /// True while a stroke is in progress (set on the first stamp).
        bool strokeActive = false;

        /// Roots spawned by the current stroke (turned into undo entries).
        std::vector<kObject *> strokeSpawned;
        /// Roots erased by the current stroke, with their attach slot.
        struct ErasedEntry
        {
            kObject *root = nullptr;
            kObject *parent = nullptr;
            kObject *nextSibling = nullptr;
        };
        std::vector<ErasedEntry> strokeErased;
        /// Every foliage instance position (new + pre-existing) used for spacing.
        std::vector<kVec3> strokePositions;
        /// Scatter cells already used by this stroke. Each cell maps to one
        /// deterministic candidate, so without this a stationary brush would
        /// spawn a duplicate instance of the same cell every frame.
        std::unordered_set<unsigned long long> strokeCellKeys;
        /// Pre-existing foliage instances considered by erase mode this stroke.
        std::vector<kObject *> sceneFoliage;
    };
    FoliageState foliage;

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

    /// @brief True while the foliage prefab picker is on screen (same reasoning).
    bool isFoliagePickerOpen() const;

    PanelTerrain(kGuiManager *setGuiManager, Manager *setManager);

    /**
     * @brief Draws the whole terrain editor inline, inside the caller's window.
     *
     * The terrain UI has no window of its own any more: the Inspector calls
     * this while a terrain tile is selected, so the tile's settings, brushes,
     * effect layers, materials and collider live next to the object header.
     * Callers must already be inside a window — the method only emits widgets.
     */
    void drawContents();

    /**
     * @brief Draws the layer texture picker (a self-managed top-level window).
     *
     * Must be called *outside* the host window (after its End()) because it
     * creates its own window plus a full-viewport input blocker.
     */
    void drawTexturePickerPopup();

    /**
     * @brief Draws the foliage prefab picker (a self-managed top-level window).
     *
     * Must be called *outside* the Inspector window (after its End()) because it
     * creates its own window plus a full-viewport input blocker, exactly like
     * the layer texture picker.
     */
    void drawFoliagePickerPopup();

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
     * @brief Creates a terrain tile at a grid coordinate.
     *
     * The one and only "add tile" path: it fills/mirrors the height field,
     * blends the shared borders into a band so the new tile stitches seamlessly
     * with its existing neighbours, builds its mesh with seam stitching, writes
     * the assets it owns and refreshes the neighbouring meshes. Both the terrain
     * panel's "+X/-X/+Z/-Z" buttons and the Inspector's Tile Expansion section
     * go through here so a tile is never created half-initialised.
     *
     * @param gridX        Integer X coordinate.
     * @param gridZ        Integer Z coordinate.
     * @param duplicateFrom When non-null, the new tile copies this tile's
     *        height field, splat weights, layer stack and material parameters
     *        instead of starting flat (the "Add neighbour" popup offers this).
     *        The copy is mirrored along the axis facing the source so the
     *        shared border continues instead of butting two unrelated edges
     *        together.
     */
    void addTerrainTile(int gridX, int gridZ, kTerrain *duplicateFrom = nullptr);

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

    /**
     * @brief Reads the foliage layer definitions stored beside the tile payloads.
     *
     * Foliage layers (prefab assignment + scatter settings) belong to the whole
     * terrain rather than to one tile, so they live in
     * `Library/Terrains/foliage_layers.json` and are loaded/saved together with
     * the tiles. Missing or malformed files fall back to a single default layer.
     */
    void loadFoliageLayers(const fs::path &libDir);

    /// @brief Writes the foliage layer definitions beside the tile payloads.
    void saveFoliageLayers(const fs::path &libDir);
    void sculptAtWorldPos(const kVec3 &worldPos);
    void rebuildSculptedTile();
    void paintAtWorldPos(const kVec3 &worldPos, bool lower);
    void rebuildPaintedTile();
    void paintSplatAtWorldPos(const kVec3 &worldPos, bool erase);
    void rebuildPaintedSplatTile();

    /**
     * @brief Scatters (or erases) foliage instances for one brush stamp.
     *
     * Placement samples the terrain under the brush on a jittered grid: the
     * selected layer's cell size/ density decide how many cells spawn, scatter
     * jitters each instance inside its cell, spacing rejects instances that sit
     * too close to one already placed, and the fixed/random offsets shape the
     * instance transform. Erase mode removes foliage instances tagged by this
     * tool within the brush radius.
     *
     * @param worldPos Position in terrain space (the grid offset is added back
     *                 when the instance is actually spawned).
     * @param erase    true → remove foliage instead of scattering it.
     */
    void paintFoliageAtWorldPos(const kVec3 &worldPos, bool erase);

    /**
     * @brief Ends the current foliage stroke: refreshes the scene once and
     *        records the whole stroke as a single undoable command.
     *
     * Called on mouse-up by the World panel; also flushes when nothing was
     * spawned or erased so the stroke state never lingers.
     */
    void commitFoliageStroke();

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
     * @brief Applies a new base elevation (the panel's Position Y) to @p tile.
     *
     * The base elevation is the tile's nominal height. Applying a new value
     * translates the sculpted base field by the difference, so the height map
     * and every effect layer (whose offsets are relative) are preserved.
     * Recorded as one undoable step and written to disk immediately.
     * @param tile         Tile to edit.
     * @param targetHeight New base elevation (world units).
     */
    void applyTileHeight(kTerrain *tile, float targetHeight);

    /**
     * @brief Returns the whole terrain grid's world-space offset.
     *
     * The terrain system addresses tiles by grid coordinate, while rendering
     * uses each tile mesh's world position. The panel's Position control moves
     * every tile together, so the difference between one tile's mesh position and
     * its grid-derived position is the group offset. It is zero for a terrain
     * that was never moved, and uniform across the grid after a move.
     */
    kVec3 getTerrainGroupOffset() const;

    /**
     * @brief Offsets every terrain tile's mesh by @p delta (X/Z).
     *
     * Used by the Position control so the grid keeps its internal layout when
     * the whole terrain is moved. Only the mesh transforms change; height / splat
     * data and the tile grid coordinates stay untouched.
     */
    void applyTerrainGroupOffset(const kVec3 &delta);

    /**
     * @brief Translates every terrain tile's base elevation by @p delta.
     *
     * All tiles shift by the same amount, so their relative relief is preserved.
     * Every tile is snapshotted before the change and committed together, making
     * the group shift one undoable step. Owned height files are written once.
     */
    void applyTerrainGroupHeight(float delta);

    /**
     * @brief Resets a tile's *sculpted* height field.
     *
     * Only the base (sculpt) field is cleared — it returns to the panel's default
     * height. The non-destructive height-modifier (effect) layers keep their
     * generated deltas and are recomposited on top of the cleared base, so the
     * tile reads as the raw generated terrain instead of flat. Recorded as one
     * undoable step for the base field.
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

    /**
     * @brief Drops the cached per-layer edit descriptors for @p tile.
     *
     * Undo/redo can insert or remove layers, which shifts every index above the
     * edited slot. The persistent tree widgets are keyed by index, so a stale
     * descriptor would display and edit the wrong layer (e.g. an erosion layer
     * showing the name of a restored noise layer). Call this after any history
     * operation that changes the layer stack.
     */
    void invalidateLayerEditBuffers(kTerrain *tile) { clearLayerEditBuffers(tile); }

    void applyNoiseToTile(kTerrain *tile, float scale, float amplitude,
                          int octaves, int seed);

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

    /**
     * @brief Rebuilds the meshes of all loaded tiles around a grid coordinate.
     *
     * Covers the eight surrounding tiles: the four edge neighbours (`X-`, `X+`,
     * `Z-`, `Z+`) share a stitched seam, and the four diagonals share a corner
     * sample that kTerrain::stitchSeams() averages across the 2x2 block, so their
     * meshes must be refreshed too or the corner keeps the stale height.
     */
    void rebuildNeighborMeshes(int gridX, int gridZ);

    /**
     * @brief Seeds a new tile so it continues the surrounding terrain.
     *
     * Writes every sample of @p tile's sculpted base from the neighbouring tiles'
     * shared border profiles, weighted by distance to each side (bilinear blend of
     * up to four profiles). A border sample therefore starts *exactly* at the
     * neighbour's height, and the interior fades smoothly toward
     * @p fallbackHeight where no neighbour constrains it.
     *
     * A flat tile can only ever meet a sculpted neighbour with a cliff, however
     * well the seam row itself is stitched — which is what kept reading as an
     * obvious seam when expanding the grid. With no neighbours at all the tile is
     * simply flat at @p fallbackHeight.
     *
     * @param tile           Newly created tile (its neighbour links must be set).
     * @param fallbackHeight Height used where no neighbour influences a sample.
     */
    void seedTileFromNeighborEdges(kTerrain *tile, float fallbackHeight);

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
        float tileHeightBefore = 0.0f;        ///< Nominal elevation at stroke start.
        std::vector<float> heightBefore;      ///< Height copy at stroke start.
        std::vector<unsigned char> weightBefore; ///< Weight copy at stroke start.
    };
    std::vector<EditSnapshot> m_editSnapshots;
    EditSnapshot *findOrCreateEditSnapshot(kTerrain *tile);

    /**
     * @brief True when @p tile is still owned by the terrain manager.
     *
     * Tiles are owned by kTerrainManager, so a pointer that is no longer in its
     * map is dangling — the tile was removed while an edit was pending, or the
     * manager was replaced by loading another world/project.
     */
    bool isLiveTerrainTile(const kTerrain *tile) const;

    /**
     * @brief Drops every cached tile pointer whose tile no longer exists.
     *
     * Clears dead undo snapshots and the sculpt/paint/staged-settings "current
     * tile" pointers. Without this, the stroke after a "remove tile → add tile →
     * sculpt" sequence rebuilt (or diffed) a freed tile, which is the access
     * violation seen in kTerrain::rebuildMesh().
     */
    void purgeStaleTileReferences();

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
        /// Set when the scratch descriptor differs from the stored layer. The
        /// edit is applied on interaction end instead of every frame.
        bool applyPending = false;
        /// True when a generator parameter changed, so the operator must be
        /// re-run (not just recomposited) when the edit is committed.
        bool generatePending = false;
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

    // --- Foliage layer prefab picker (modal) ---
    int m_foliagePickLayer = -1;       ///< Foliage layer whose prefab is being chosen.
    kString m_foliagePickSelected;     ///< Prefab asset UUID chosen in the picker.
    bool m_foliagePickOpen = false;    ///< True while the picker window is shown.
    char m_foliagePickSearch[128] = "";

    // --- Terrain material parameter staging ---
    // Tiling is deliberately NOT here: it is per material layer
    // (kTerrainLayer::tiling) and edited in each layer's tree node.
    struct MaterialSettings
    {
        /// Terrain shading model: 0 = Flat, 1 = Phong, 2 = PBR.
        int shaderType = 2;
        float blendSharpness = 1.0f;
        bool rotateUV = false;
        float uvRotation = 0.0f; ///< Degrees.
        // Stochastic anti-tiling, triplanar projection, cell bombing and the
        // Voronoi warp are all per material layer settings (kTerrainLayer), not
        // material-wide.
        float noiseVariegation = 0.25f;
        float noiseScale = 0.02f;
        bool noiseEnabled = false; ///< Procedural noise variegation on/off.

        // Terrain self-shadow controls. They shape the shader's directional
        // (N·L) lighting, not the CSM cast shadows — CSM follows the scene's
        // shadow settings. Stored as TerrainPBR material params, so they travel
        // with the tile.
        float selfShadowStrength = 0.0f; ///< Extra contrast on slopes facing away from the sun.
        float selfShadowSoftness = 0.0f; ///< Wrap lighting around the terminator (lifts dark slopes).
    };
    kTerrain *m_matSettingsTile = nullptr; ///< Tile the staged settings belong to.
    MaterialSettings m_matSettings;

    /// Desired HeightField collider resolution (samples per side).
    int m_collisionResolution = 129;
    kTerrain *m_collisionResTile = nullptr; ///< Tile the staged resolution belongs to.

    float m_defaultHeight = 0.0f;
    /// Tile whose nominal elevation was last loaded into m_defaultHeight; the
    /// field adopts the selected tile's value when the selection changes.
    kTerrain *m_tileHeightTile = nullptr;

    // --- Terrain size / resolution settings (applied only after confirmation) ---
    float m_settingsWorldSize = 256.0f; ///< Edited value in the Terrain panel.
    int m_settingsHeightRes = 513;      ///< Edited value in the Terrain panel.
    /// Manager values the staged settings above were last synced with. A project
    /// load replaces the terrain manager (possibly with a different world size /
    /// resolution), so the panel refreshes its fields whenever these change
    /// instead of keeping the hard-coded defaults forever.
    float m_syncedWorldSize = -1.0f;
    int m_syncedHeightRes = -1;
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

    // --- Deferred "Add neighbour" choice -------------------------------------
    /// Set when an "Add neighbour" button was clicked; opens a modal asking
    /// whether the new tile should duplicate the source tile or start empty.
    bool m_addNeighbourPending = false;
    /// Tile the new neighbour is spawned from (duplicated on request).
    kTerrain *m_addNeighbourSource = nullptr;
    int m_addNeighbourGridX = 0;      ///< Target grid X of the pending tile.
    int m_addNeighbourGridZ = 0;      ///< Target grid Z of the pending tile.
    kString m_addNeighbourText;       ///< Popup description (direction, coords).

    /// Re-entrancy guard: rebuilding one tile also refreshes its neighbours so the
    /// stitched seams stay closed, and a neighbour rebuild must not bounce back.
    bool m_rebuildingMesh = false;
};

#endif // KEMENA3D_EDITOR_TERRAIN_H
