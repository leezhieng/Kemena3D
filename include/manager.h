#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <random>
#include <iostream>
#include <filesystem>
#include <fstream>

#include <sstream>
#include <iomanip>
#include <memory>

#include <kemena/kwindow.h>
#include <kemena/kworld.h>
#include <kemena/krenderer.h>
#include <kemena/kscene.h>
#include <kemena/kmeshgenerator.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kdecal.h>
#include <kemena/kassetmanager.h>
#include <kemena/koffscreenrenderer.h>
#include <kemena/kprefab.h>
#include <kemena/kterrain.h>
#include <kemena/kpackage.h>
#include <kemena/kanimator.h>
#include <kemena/kskelanimation.h>

#include "commands.h"
#include <portable-file-dialogs.h>
#include <kemena/kguimanager.h>
#include <ImGuizmo.h>

#include "panel_project.h"
#include "panel_hierarchy.h"
#include "panel_console.h"
#include "panel_game.h"
#include "panel_terrain.h"
#include "panel_animator.h"
#include "panel_animation.h"
#include "panel_particle.h"

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <unistd.h>
#include <limits.h>
#endif

using namespace kemena;
namespace fs = std::filesystem;
using json = nlohmann::json;

/**
 * @brief Live shader-preview state shared between PanelShaderGraph and PanelInspector.
 *
 * Holds the most recently compiled shader so the inspector can show a preview
 * that tracks edits made in the shader editor.
 */
struct ShaderPreviewState
{
    bool active = false;
    std::string uuid;
    std::string glslSource;
    std::string shaderType;
    std::string shaderName;
};

/**
 * @brief Describes a single project asset file tracked by the project panel.
 */
struct FileInfo
{
    kString path;
    kString checksum;
    kString type;
};

/**
 * @brief A queued asset conversion/import job (mesh or image).
 */
struct ImportTask
{
    fs::path inputPath;
    fs::path outputPath;
    kString type;
    kString uuid;
    fs::path thumbnailPath;
    bool success = false;
    bool reported = false;
    kString errorMsg;
    std::vector<std::string> warnings;
    bool warningsLogged = false;
};

/**
 * @brief A queued thumbnail render, processed on the main thread after batch import.
 */
struct ThumbnailTask
{
    kString uuid;
    fs::path srcPath;
    fs::path thumbnailPath;
    kString type;
};

/**
 * @brief Lightweight reference to a scene object, used by the hierarchy panel.
 */
struct ObjectInfo
{
    kObject *object;
};

// Defined in panel_animator.h. Kept incomplete here so manager.h can be
// included from panel_animator.h without exposing the mutual-include cycle.
struct AnimatorGraph;

/**
 * @brief Runtime instance of an .animator controller attached to a scene object.
 *
 * Created by Manager::startAnimators() when the Game panel enters Play mode and
 * torn down again by Manager::stopAnimators() on Stop. The engine's kAnimator is
 * driven manually by Manager::stepAnimators() so the graph's state machine
 * (speed, loop, exit-time transitions and conditions) is honoured regardless of
 * which renderer draws the scene.
 */
struct RuntimeAnimator
{
    kString objectUuid;                                            ///< Object this controller drives.
    std::shared_ptr<AnimatorGraph> graph;                          ///< Parsed .animator controller (owned).
    kMesh *rootMesh = nullptr;                                     ///< Topmost mesh of the object subtree (non-owned).
    kAnimator *animator = nullptr;                                 ///< Engine skeletal animator (owned).
    std::vector<kSkeletalAnimation *> ownedClips;                  ///< Loaded clips (owned).
    std::unordered_map<std::string, kSkeletalAnimation *> clipForState; ///< animationUuid → clip.
    std::unordered_map<std::string, std::pair<float, float>> clipFrames; ///< animationUuid → (startFrame, endFrame).
    std::unordered_map<std::string, std::array<bool, 3>> clipRootMotion; ///< animationUuid → {rotation, positionY, positionXZ}.
    int currentStateId = -1;                                       ///< State currently playing.
    float stateTimeSeconds = 0.0f;                                 ///< Seconds since entering the current state.
    std::unordered_map<std::string, float> variables;              ///< Controller variable values.
};

class PanelProject;
class PanelHierarchy;
class PanelConsole;
class PanelLogicGraph;
class PanelShaderGraph;
class PanelGame;
class PanelTerrain;
class PanelAnimator;
class PanelAnimation;
class PanelParticle;

/**
 * @brief Central editor controller for a Kemena3D Studio project.
 *
 * Owns and coordinates the editor's project, world, scene and selection state.
 * Acts as the hub for project lifecycle (new/open/save), asset management and
 * import, object creation and editing, prefab authoring, material assignment,
 * navigation-mesh baking, physics simulation, scripting, and game export. The
 * UI panels delegate their actions here.
 */
class Manager
{
public:
    Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer);
    virtual ~Manager();

    void setScene(kScene *s) { scene = s; }
    kScene *getScene() { return scene; }
    kRenderer *getRenderer() { return renderer; }
    void setGui(kGuiManager *g) { gui = g; }
    kObject *findObjectByUuid(const kString &uuid);
    void deleteSelectedObjects();
    void duplicateSelectedObjects();
    std::vector<TransformState> captureSelectedTransforms();

    // --- Accessors ----------------------------------------------------------
    kWindow *getWindow() { return window; }
    kWorld *getWorld() { return world; }
    kAssetManager *getAssetManager() { return world ? world->getAssetManager() : nullptr; }
    kWorld *getActiveWorld() { return (activeMode == EditorMode::GameWorld) ? world : previewWorld; }
    kScene *getActiveScene() { return (activeMode == EditorMode::GameWorld) ? scene : (previewWorld ? previewWorld->getScenes().empty() ? nullptr : previewWorld->getScenes()[0] : nullptr); }

    /**
     * @brief Returns the world whose scene graph should be displayed in the hierarchy panel.
     *
     * When editing a prefab the hierarchy shows the prefab's isolated world;
     * otherwise it shows the game world.
     */
    kWorld *getHierarchyWorld() { return hierarchyShowsPrefab ? prefabWorld : world; }

    /**
     * @brief Returns the primary scene whose objects should be displayed in the hierarchy panel.
     *
     * When editing a prefab this is the prefab scene; otherwise the active game scene.
     * Editor overlay scenes (grid, etc.) are excluded.
     */
    kScene *getHierarchyScene() { return hierarchyShowsPrefab ? prefabScene : scene; }

    /**
     * @brief Returns the world where new objects should be created.
     *
     * When editing a prefab, new objects are added to the prefab's isolated
     * world; otherwise they go to the game world.
     */
    kWorld *getCreationWorld() { return prefabEditing ? prefabWorld : world; }

    /**
     * @brief Returns the scene where new objects should be created.
     *
     * When editing a prefab, new objects are added to the prefab scene;
     * otherwise they go to the active game scene.
     */
    kScene *getCreationScene() { return prefabEditing ? prefabScene : scene; }

    // --- Edit actions -------------------------------------------------------
    void selectAll();
    void deselectAll();
    void invertSelection();

    // --- Object creation ----------------------------------------------------
    void createSceneObject();
    void createEmpty();
    void createMeshPrimitive(kMesh *mesh, const kString &name);
    void createMeshFromFile();
    void createLight(kLightType type);
    void createCamera();
    void createTerrain();
    void createNavMesh();
    void createAudio();
    void createAudioListener();
    void createParticle();
    void createDecal();

    /**
     * @brief Sets the decal's material to a fresh built-in material of the
     *        given shader type ("flat", "pbr" or "phong").
     *
     * Clears any assigned .mat UUID so the built-in material takes effect.
     * @param decal Target decal object.
     * @param type  Shader type marker.
     * @return true on success.
     */
    bool applyDecalShaderType(kDecal *decal, const kString &type);

    // --- Publish ------------------------------------------------------------

    /** @brief Per-platform publish configuration. */
    struct PlatformPublishSettings
    {
        bool enabled = true;
        std::string gameName = "Game";
        std::string title = "My Game";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        std::string outputDir;
        std::string templateDir;
        std::string iconPath;
        std::string compression = "Default";
    };

    /** @brief Global publish settings for the project. */
    struct PublishSettings
    {
        PlatformPublishSettings platforms[3];  ///< 0=Windows, 1=macOS, 2=Linux
        std::vector<std::string> includeWorlds; ///< World file paths to package
        std::string defaultLevel;               ///< First world to load (relative path)
        std::string templateDir;                ///< Global template dir fallback
    };

    /** @brief Physical device type a named input action can be bound to. */
    enum InputBindingDevice
    {
        INPUT_BINDING_KEYBOARD = 0, ///< Keyboard key (K_KEY_*).
        INPUT_BINDING_MOUSE = 1,    ///< Mouse button (K_MOUSEBUTTON_*).
        INPUT_BINDING_GAMEPAD = 2,  ///< Gamepad button (K_GAMEPAD_BUTTON_*).
    };

    /** @brief A single named input action and the physical input it is bound to. */
    struct InputActionBinding
    {
        std::string name;                          ///< Logical action name (e.g. "Jump").
        int device = INPUT_BINDING_KEYBOARD;       ///< Which device the binding refers to.
        int binding = 0;                           ///< Device-specific binding code.
    };

    /** @brief Global named-input (key binding) settings for the project. */
    struct InputSettings
    {
        std::vector<InputActionBinding> actions;   ///< Ordered list of named actions.
    };

    /** @brief Global object-tag settings for the project (like Unity's tags). */
    struct TagSettings
    {
        std::vector<std::string> tags; ///< Ordered list of user-defined tags.
    };

    /** @brief Global physics-layer settings for the project (like Unity's layers). */
    struct LayerSettings
    {
        std::vector<std::string> layers; ///< Ordered list of user-defined physics layers.
    };

    PublishSettings publishSettings;
    InputSettings inputSettings;
    TagSettings tagSettings;
    LayerSettings layerSettings;
    kInputManager *inputManager = nullptr; ///< Named input manager for Play mode (owned by Manager).
    bool showPublishDialog = false;
    bool showProjectSettings = false;

    // Legacy export settings (kept for backward compat)
    struct ExportSettings
    {
        int platform = 0;
        std::string gameName = "Game";
        std::string title = "My Game";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        std::string outputDir;
        std::string templateDir;
        std::string iconPath;
    };
    ExportSettings exportSettings;
    bool showExportDialog = false;

    /**
     * @brief Legacy export function — builds a distributable game using the
     *        old ExportSettings struct. Kept for backward compatibility with
     *        the Build Settings dialog.
     * @return true on success.
     */
    bool exportGame(const ExportSettings &settings);

    /**
     * @brief Publishes the project for a single platform.
     * @param platformIdx 0=Windows, 1=macOS, 2=Linux.
     * @return true on success.
     */
    bool publishGame(int platformIdx);

    /**
     * @brief Collects asset paths referenced by a world file (JSON).
     * @param worldPath Path to the .world file.
     * @param outAssets Receives relative asset paths (e.g. "Library/ImportedAssets/<uuid>.glb").
     */
    void collectWorldAssetPaths(const fs::path &worldPath, std::set<kString> &outAssets);

    /** @brief Loads publish settings from project config. */
    void loadPublishSettings();

    /** @brief Saves publish settings to project config. */
    void savePublishSettings();

    /** @brief Loads named input bindings from project config. */
    void loadInputSettings();

    /** @brief Saves named input bindings to project config. */
    void saveInputSettings();

    /** @brief Rebuilds the input manager's action bindings from inputSettings. */
    void applyInputBindings();

    /** @brief Advances the input manager (call once per frame while Playing). */
    void stepInput();

    // --- Tags & physics layers --------------------------------------------

    /** @brief Loads the project's object-tag list from config. */
    void loadTagSettings();

    /** @brief Saves the project's object-tag list to config. */
    void saveTagSettings();

    /** @brief Loads the project's physics-layer list from config. */
    void loadLayerSettings();

    /** @brief Saves the project's physics-layer list to config. */
    void saveLayerSettings();

    /**
     * @brief Adds a tag to the project's tag list (deduplicated, non-empty).
     * @param name New tag name.
     * @return true if the tag was added.
     */
    bool addTag(const std::string &name);

    /**
     * @brief Removes a tag from the project's tag list.
     * @param name Tag name to remove.
     */
    void removeTag(const std::string &name);

    /**
     * @brief Adds a layer to the project's physics-layer list (deduplicated,
     *        non-empty, capped at kMaxPhysicsLayers).
     * @param name New layer name.
     * @return true if the layer was added.
     */
    bool addLayer(const std::string &name);

    /**
     * @brief Removes a layer from the project's physics-layer list.
     * The built-in "Default" layer cannot be removed.
     * @param name Layer name to remove.
     */
    void removeLayer(const std::string &name);

    /// True while the Tag editor modal is open.
    bool showTagEditor = false;
    /// True while the physics-layer editor modal is open.
    bool showLayerEditor = false;
    /// Input buffer for the "new tag" field in the tag editor.
    char newTagBuf[64] = "";
    /// Input buffer for the "new layer" field in the layer editor.
    char newLayerBuf[64] = "";

    /// Pending request to show the Cinematic Editor panel.
    bool pendingOpenAnimationEditor = false;

    // --- Navigation baking --------------------------------------------------
    void bakeNavMesh(kObject *navObj);
    void clearNavMesh(kObject *navObj);
    void clearAllNavMeshes();
    bool isNavMeshBaked(kObject *navObj) const;
    kNavMesh *getBakedNavMesh(kObject *navObj) const;

    // --- Asset creation -----------------------------------------------------
    void selectProjectAssetByPath(const fs::path &filePath);
    void createNewMaterial();
    void createNewFolder();
    void createNewShader();
    void createNewRawShader();
    void createNewScript();
    void createNewLogicGraph();
    void createNewAnimator();
    void createNewAnimation();
    void createNewParticle();
    void createNewAnimationFromMesh(const kString &meshUuid, const fs::path &meshPath);
    void deleteAssets(const std::vector<fs::path> &paths);
    void reimportAsset(const kString &uuid);
    bool renameAsset(const fs::path &oldPath, const kString &newName);
    bool duplicateAsset(const fs::path &srcPath);
    bool moveAsset(const fs::path &srcPath, const fs::path &destDir);
    kString getCurrentDirPath();
    void openFolder(kString name);
    void closeFolder();
    bool newProject();
    bool openProject();
    bool newWorld();
    bool openProjectFromPath(const kString &path);

    // --- New Project Dialog --------------------------------------------------
    bool showNewProjectDialog = false;   ///< When true, the New Project dialog is displayed.
    kString newProjectNameInput = "New Project"; ///< Project name text field buffer.
    fs::path newProjectDirInput;         ///< Directory path text field buffer.
    bool newProjectCreateFolder = true;  ///< Create a subfolder named after the project.
    void drawNewProjectDialog();         ///< Renders the New Project modal dialog.
    bool executeNewProject(const kString& name, const fs::path& dir, bool createFolder);

    // Recent projects
    std::vector<kString> recentProjects;
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const kString &path);

    // Per-project config
    void saveProjectConfig();
    fs::path loadLastWorldPath() const;
    void checkAssetChange();
    void refreshWindowTitle();
    void closeEditor();
    void clearWorld(bool forced = false);
    void resetToFreshWorld();
    void deleteObjectRecursive(kObject *node);
    void saveWorld();
    void saveWorldAs(const kString &path);
    void saveWorldAs();
    kString promptWorldSavePath();
    void applyDefaultSkybox(kScene *target);
    void loadWorld(const kString &path);
    void loadDefaultWorldInto(kScene *target);
    void loadDefaultWorkspace();
    void loadProjectWorkspace();

    /**
     * @brief Writes the files currently open in the graph editor panels
     *        (Logic Graph, Animator, Shader Graph, Cinematic) to a workspace ini.
     *
     * Only panels that are currently visible and have a file loaded are written.
     * Paths are stored relative to the project so the workspace stays portable.
     */
    void saveOpenEditorFiles(const fs::path &workspacePath);

    /**
     * @brief Re-opens the graph editor files recorded in a workspace ini.
     *
     * Called right after a project workspace layout is loaded so the editor
     * panels show the same files they had when the project was saved.
     */
    void restoreOpenEditorFiles(const fs::path &workspacePath);

    // --- Prefabs ------------------------------------------------------------
    bool saveSelectedAsPrefab(const kString &prefabName);
    kObject *instantiatePrefabInScene(const fs::path &prefabPath);
    void editPrefab(const fs::path &prefabPath);
    void closePrefabEditor(bool saveChanges);

    bool prefabEditing = false;
    fs::path editingPrefabPath;
    kPrefab editingPrefab;

    // --- Prefab's own isolated world, scene, camera, renderer, and asset manager ---
    // The prefab editor uses a completely separate rendering pipeline from the
    // game world — its own kWorld, kScene, kCamera, kRenderer, and kAssetManager.
    // This keeps prefab editing independent from the main scene graph.
    kWorld        *prefabWorld        = nullptr;  ///< Standalone world for the prefab editor.
    kScene        *prefabScene        = nullptr;  ///< Scene containing the prefab subtree being edited.
    kCamera       *prefabCamera       = nullptr;  ///< Camera for the prefab editor viewport.
    kRenderer     *prefabRenderer     = nullptr;  ///< Dedicated renderer for the prefab panel (own FBO, own driver).
    kAssetManager *prefabAssetManager = nullptr;  ///< Asset manager for the prefab world (loads shaders/textures).
    kObject       *prefabRoot         = nullptr;  ///< Root object of the prefab subtree being edited.
    kScene        *prefabEditorScene  = nullptr;  ///< Editor overlay scene (grid) duplicated for the prefab panel.

    // --- Audio preview -------------------------------------------------------
    kAudioManager *audioPreviewManager = nullptr;
    kAudio        *audioPreviewClip     = nullptr; ///< Currently-previewing clip (nullptr when idle).
    kString        audioPreviewSourceUuid;          ///< UUID of the audio source whose clip is previewing.
    kString        audioPreviewAssetUuid;           ///< UUID of the project audio asset being previewed ("" when idle).
    void startAudioPreview(kAudioSource &src);
    void startAudioPreviewByUuid(const kString &audioUuid);
    void stopAudioPreview();
    bool isAudioPreviewPlaying() const;
    bool isAudioAssetPreviewPlaying() const;

    // --- In-game audio (runtime playback during Game panel play) ------------
    /**
     * @brief Tracks a loaded game-audio clip together with its owning object
     *        so position can be updated every frame for spatialized sources.
     */
    struct GameAudioEntry
    {
        kAudio  *clip   = nullptr;  ///< Loaded audio clip.
        kObject *object = nullptr;  ///< Owning scene object (for position updates).
        bool     spatialize = false; ///< Whether 3D spatialization is enabled.
    };

    kAudioManager          *gameAudioManager = nullptr;
    std::vector<GameAudioEntry> gameAudioEntries;    ///< All audio clips started for the current play session.
    bool                    gameAudioHasSpatial = false; ///< True if any game audio entry uses 3D spatialization.

    /**
     * @brief Iterates the active world/scene, loads and starts every
     *        kAudioSource with isActive && playOnAwake == true.
     */
    void startGameAudio();

    /** @brief Stops and unloads all in-game audio clips started by startGameAudio(). */
    void stopGameAudio();

    /** @brief Pauses every in-game audio clip (keeps the cursor so it can resume). */
    void pauseGameAudio();

    /** @brief Resumes every in-game audio clip paused by pauseGameAudio(). */
    void resumeGameAudio();

    /**
     * @brief Called every frame while the game is playing.
     *
     * Updates the audio listener position from the game camera (if any
     * spatialized source exists) and the world-space position of each
     * spatialized audio source.
     * @param gameCamera  The camera used for the game view (may be nullptr).
     */
    void updateGameAudio(kCamera *gameCamera);

    // --- Animator controllers (runtime, during Game panel play) --------------
    void startAnimators();
    void stopAnimators();
    void stepAnimators(float dt);

    std::vector<RuntimeAnimator> runtimeAnimators; ///< Active animator controllers for the current play session.

    // --- Drag-and-drop helpers ----------------------------------------------
    kObject *instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint = kVec3(0));
    fs::path findAssetPathByUuid(const kString &assetUuid);
    void reparentObject(const kString &uuid, const kString &newParentUuid);
    void reorderBefore(const kString &uuid, const kString &siblingUuid);
    kObject *findPrefabInstanceRoot(kObject *obj);
    bool createPrefabFromSelection();
    bool createPrefabFromObject(const kString& objectUuid, const fs::path& targetDir);
    bool applyPrefabInstance(kObject *instanceRoot);
    void refreshAllPrefabInstances(const kString &prefabUuid);
    void unpackPrefabInstance(kObject *instanceRoot);

    /** @brief Checks if a prefab instance has modifications (excluding transform). */
    bool isPrefabDirty(kObject *instanceRoot);

    /** @brief Reverts a prefab instance to match its source prefab file (preserving transform). */
    void revertPrefabInstance(kObject *instanceRoot);

    /** @brief Clears the cached prefab templates, forcing a re-read on next dirty check. */
    void clearPrefabTemplateCache();

    /**
     * @brief Destroys the current subtree rooted at rootUuid and rebuilds it from a serialized JSON snapshot.
     *
     * Used by the revert undo/redo lambda to swap between dirty and clean states
     * while preserving per-instance properties (transform, parent, selection).
     */
    void replacePrefabSubtree(const kString &rootUuid,
                              const nlohmann::json &stateJson,
                              kScene *targetScene,
                              kObject *parent, kObject *nextSibling,
                              const kVec3 &pos, const kQuat &rot, const kVec3 &scl,
                              const kString &name, bool active,
                              const kString &prefabUuid,
                              bool restoreSelection, bool restoreInList,
                              const kString &selectedDescendantUuid);
    bool applyMaterialToObject(kObject *obj, const fs::path &materialPath,
                               const kString &materialUuid = "");
    kMaterial *buildMaterialFromJson(const nlohmann::json &matJson);
    kTexture2D *getProjectTexture(const kString &textureUuid, const kString &uniformName);
    bool reimportTexture(const kString &textureUuid);
    bool reimportMesh(const kString &meshUuid);
    void processPendingMeshReloads();
    std::vector<kString> pendingMeshReloads;
    kShader *getRawShader(const kString &shaderUuid);
    kString getMaterialShaderSource(const nlohmann::json &matJson);
    void applyDefaultMaterialToObject(kObject *obj);
    void reapplyStoredMaterials();
    std::vector<MaterialSnapshot> captureMaterialSubtree(kObject *root);
    void restoreMaterialSubtree(const std::vector<MaterialSnapshot> &snap);
    void assignImportChildUuids(kObject *root);

    // Material drag-preview state
    kObject *matPreviewObject = nullptr;
    std::vector<MaterialSnapshot> matPreviewSnapshot;
    kString matPreviewSourceUuid;

    std::unordered_map<kString, kTexture2D *> textureCache;
    std::unordered_map<kString, kShader *> shaderCache;

    /** @brief Caches prefab root JSON by prefab UUID for efficient dirty checking. */
    std::unordered_map<kString, nlohmann::json> prefabTemplateCache;

    kString dragHoverObjectUuid;

    // --- Physics simulation -------------------------------------------------
    void startPhysicsSimulation();
    void stopPhysicsSimulation();
    void stepPhysics(float dt);

    kPhysicsManager *physicsManager = nullptr;
    std::vector<kObject *> physicsBodies;
    std::vector<kObject *> characterBodies;

    std::map<kString, kNavMesh *> bakedNavMeshes;

    // --- Scripting ----------------------------------------------------------
    void buildScripts(bool logSummary = false);
    void startScripts();
    void stopScripts();
    void pollScriptChanges(float dt);
    float scriptWatchTimer = 0.0f;

    // Editor path
    fs::path exePath;
    fs::path baseDir;

    /**
     * @brief Identifies the most recently focused editor panel.
     *
     * Persisted across frames by main.cpp and consumed by PanelInspector to
     * decide which selection context to display (project asset, scene object,
     * animator node, shader preview, etc.).
     *
     * @note "Logic" corresponds to the Script Editor panel, which edits .logic
     *       visual-scripting graphs (also the source of generated AngelScript).
     */
    enum class FocusedPanel
    {
        None,        ///< No tracked panel has focus yet.
        Project,     ///< Project asset browser.
        Hierarchy,   ///< Scene-graph tree.
        Scene,       ///< World viewport.
        Logic,       ///< Script Editor (.logic visual scripting).
        Animator,    ///< Animator state-machine editor.
        Particle,    ///< Particle system editor.
        Shader,      ///< Shader node-graph editor.
        Animation,   ///< Cinematic / animation timeline editor.
    };

    // Editor mode — controls what the World panel renders
    enum class EditorMode
    {
        GameWorld,        ///< Normal scene editing
        PrefabPreview,    ///< Previewing/editing a .prefab asset
        ParticlePreview,  ///< Previewing a .particle asset
        AnimatorPreview   ///< Previewing an .animator asset
    };
    EditorMode activeMode = EditorMode::GameWorld;

    // Project info
    kString projectName;
    bool projectOpened = false;
    bool projectSaved = true;

    // Project path
    fs::path projectPath;
    std::vector<kString> currentDir;

    // World info
    kString worldName = "";
    fs::path worldPath;

    PanelProject *panelProject = nullptr;
    PanelHierarchy *panelHierarchy = nullptr;
    PanelLogicGraph *panelLogicGraph = nullptr;
    PanelShaderGraph *panelShaderGraph = nullptr;
    PanelConsole *panelConsole = nullptr;
    PanelGame *panelGame = nullptr;
    PanelTerrain *panelTerrain = nullptr;
    PanelAnimator *panelAnimator = nullptr;
    PanelAnimation *panelAnimation = nullptr;
    PanelParticle  *panelParticle  = nullptr;

    kTerrainManager *terrainManager = nullptr;
    kCamera *editorCamera = nullptr;
    kCamera *defaultGameCamera = nullptr;

    // Editor-camera orbit state
    kVec3 editorCamOrbitPivot = kVec3(0.0f, 3.5f, 0.0f);
    float editorCamOrbitDistance = 14.0f;
    bool editorCamLoadPending = false;

    // --- Preview world (for prefab / particle / animator preview) ---
    kWorld *previewWorld = nullptr;    ///< Standalone world for asset preview, separate from the game world.
    kCamera *previewCamera = nullptr;  ///< Camera for the preview world (orbit-style).
    kString previewAssetPath;          ///< Current asset file being previewed.
    kString previewAssetType;          ///< Extension of the previewed asset (".prefab", ".particle", ".animator").

    // Preview-camera orbit state (reset every time a new asset is opened)
    float previewCamPitch = 24.09f;  ///< Orbit pitch (degrees), matches model viewer default.
    float previewCamYaw   = 26.57f;  ///< Orbit yaw (degrees), matches model viewer default.
    kVec3 previewCamPivot = kVec3(0.0f, 0.0f, 0.0f);  ///< Orbit pivot (AABB centre of previewed asset).
    float previewCamDist  = 5.0f;    ///< Orbit distance, computed via framePreviewCamera().
    bool  previewCamDragging = false; ///< True while the user is dragging to orbit.

    void setEditorMode(EditorMode mode, const kString &assetPath = "", const kString &assetType = "");
    void initPreviewWorld();
    void destroyPreviewWorld();
    void framePreviewCamera();

    std::vector<ImportTask> importQueue;
    std::future<void> importFuture;
    std::atomic<int> filesProcessed{0};
    std::atomic<bool> batchDone{false};
    std::mutex queueMutex;
    bool showingMessageBox = false;

    bool showImportPopup = false;
    std::chrono::steady_clock::time_point importEndTime;
    std::vector<ImportTask> importTasks;
    std::vector<ThumbnailTask> thumbnailQueue;
    kOffscreenRenderer thumbnailRenderer{128, 128};

    void drawImportPopup(PanelConsole *console);
    void processThumbnailQueue(PanelConsole *console);

    std::unordered_map<kString, FileInfo> fileMap;
    std::unordered_map<kString, kString> uuidMap;
    std::unordered_map<kString, ObjectInfo> objectMap;

    std::vector<kString> selectedObjects;
    void selectObject(const kString uuid, bool clearList = false);
    void deselectObject(const kString uuid);

    kObject *selectedObject = nullptr;
    kScene *selectedScene = nullptr;
    bool worldSelected = false;

    // --- Prefab-panel–specific selection (totally separate from world panel) ---
    std::vector<kString> prefabSelectedObjects;
    kObject *prefabSelectedObject = nullptr;

    /** @brief Returns the selection appropriate for the currently-focused panel. */
    std::vector<kString> &getActiveSelectedObjects()
        { return hierarchyShowsPrefab ? prefabSelectedObjects : selectedObjects; }
    kObject *&getActiveSelectedObject()
        { return hierarchyShowsPrefab ? prefabSelectedObject : selectedObject; }

    ImGuizmo::OPERATION manipulatorType = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE manipulatorMode = ImGuizmo::LOCAL;

    // Prefab panel has its own ImGuizmo state so it doesn't interfere with the
    // world panel's gizmo mode.
    ImGuizmo::OPERATION prefabManipulatorType = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE prefabManipulatorMode = ImGuizmo::LOCAL;

    /// Tracks which world the hierarchy panel should display.
    /// true = prefab world, false = game world.
    bool hierarchyShowsPrefab = false;

    UndoRedoManager undoRedo;
    PivotMode pivotMode = PivotMode::LastSelected;
    ShaderPreviewState shaderPreview;

    /// Most recently focused editor panel; drives what PanelInspector displays.
    FocusedPanel lastFocusedPanel = FocusedPanel::None;

private:
    kWindow *window;
    kWorld *world;
    kRenderer *renderer;
    kGuiManager *gui = nullptr;
    kScene *scene = nullptr;
    kString initialWindowTitle;

    kString checkAssetType(const fs::path &p);
    void startBatchImport(const std::vector<ImportTask> &tasks);

    /**
     * @brief Resolves an audio asset UUID to a playable filesystem path.
     *
     * Search order:
     *   1. Converted copy in Library/ImportedAssets (any supported extension).
     *   2. Live asset registry (assets.json) -> Assets/<relative path>.
     *   3. Stale-UUID recovery via Library/Metadata/<uuid>.json, which records
     *      the original source location even after an asset has been re-imported
     *      with a new UUID.  This keeps audio working in freshly cloned projects
     *      where Library/ImportedAssets is never committed to git.
     *
     * @param audioUuid  The kAudioSource::audioFile UUID to resolve.
     * @return           Absolute-ish path (relative to projectPath) if found,
     *                   otherwise an empty path.
     */
    fs::path resolveAudioFileByUuid(const kString &audioUuid);
};

#endif // FILEMANAGER_H
