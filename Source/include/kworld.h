/**
 * @file kworld.h
 * @brief Top-level container holding scenes and cameras.
 */

#ifndef KWORLD_H
#define KWORLD_H

#include <string>
#include <iostream>
#include <vector>

#include "kdatatype.h"
#include "kassetmanager.h"
#include "kscene.h"
#include "kcamera.h"
#include "kscriptmanager.h"

// Export macro
#ifdef _WIN32
#ifdef KEMENA3D_STATIC
#define KEMENA3D_API
#elif defined(KEMENA3D_EXPORTS)
#define KEMENA3D_API __declspec(dllexport)
#else
#define KEMENA3D_API __declspec(dllimport)
#endif
#else
#define KEMENA3D_API
#endif

namespace kemena
{
    class kScene;
    class kPhysicsManager;

    /**
     * @brief Root container for the entire simulation environment.
     *
     * A kWorld owns one or more kScene instances and a camera list.
     * The renderer iterates over active scenes and draws them through the
     * main camera returned by getMainCamera().
     *
     * Typical setup:
     * @code
     *   kWorld world;
     *   world.setAssetManager(&assetMgr);
     *   kScene *scene = world.createScene("Main");
     *   kCamera *cam  = world.addCamera(kVec3(0,0,5));
     *   world.setMainCamera(cam);
     * @endcode
     */
    class KEMENA3D_API kWorld
    {
    public:
        /** @brief Constructs an empty world and creates its script manager. */
        kWorld();

        /** @brief Destroys the world, releasing owned scenes, cameras, and managers. */
        virtual ~kWorld();

        /**
         * @brief Returns the UUID of this world.
         * @return UUID v4 kString.
         */
        kString getUuid();

        /**
         * @brief Sets the UUID of this world.
         * @param newUuid UUID v4 kString.
         */
        void setUuid(kString newUuid);

        /**
         * @brief Creates a new scene and registers it in this world.
         * @param sceneName Human-readable name for the scene.
         * @param sceneUuid Optional UUID; auto-generated if empty.
         * @return Pointer to the newly created kScene.
         */
        kScene *createScene(kString sceneName, kString sceneUuid = "");

        /**
         * @brief Registers an existing scene in this world.
         * @param scene     Pre-constructed scene to add.
         * @param sceneUuid Optional UUID override.
         */
        void addScene(kScene *scene, kString sceneUuid = "");

        /**
         * @brief Creates and registers a camera in this world.
         * @param position   World-space initial position.
         * @param lookAt     Initial look-at target (for locked cameras).
         * @param type       Camera mode (free or locked).
         * @param objectUuid Optional UUID for the camera node.
         * @return Pointer to the newly created kCamera.
         */
        kCamera *addCamera(kVec3 position = kVec3(0.0f, 0.0f, 0.0f), kVec3 lookAt = kVec3(0.0f, 0.0f, 0.0f), kCameraType type = kCameraType::CAMERA_TYPE_FREE, kString objectUuid = "");

        /**
         * @brief Registers an existing camera in this world.
         * @param camera     Pre-constructed camera to add.
         * @param objectUuid Optional UUID override.
         */
        void addCamera(kCamera *camera, kString objectUuid = "");

        /**
         * @brief Returns the camera used by the renderer for the main view.
         * @return Pointer to the main camera, or nullptr if not set.
         */
        kCamera *getMainCamera();

        /**
         * @brief Sets the main camera used by the renderer.
         * @param camera Pointer to the desired camera.
         */
        void setMainCamera(kCamera *camera);

        /**
         * @brief Assigns the asset manager used by scenes in this world.
         * @param manager Asset manager instance; must outlive the world.
         */
        void setAssetManager(kAssetManager *manager);

        /**
         * @brief Returns the asset manager.
         * @return Pointer to the asset manager, or nullptr if not set.
         */
        kAssetManager *getAssetManager();

        /**
         * @brief Removes a camera from this world's camera list.
         * @param camera Camera to remove.
         */
        void removeCamera(kCamera *camera);

        /**
         * @brief Removes a scene from this world's scene list.
         * @param scene Scene to remove.
         */
        void removeScene(kScene *scene);

        /**
         * @brief Returns all scenes registered in this world.
         * @return Copy of the internal scene vector.
         */
        std::vector<kScene *> getScenes();

        /**
         * @brief Returns all cameras registered in this world.
         * @return Copy of the internal camera vector.
         */
        std::vector<kCamera *> getCameras();

        // --- Scripting -------------------------------------------------------

        /**
         * @brief Returns the world's AngelScript manager.
         *
         * Created automatically with the world; used to register script assets,
         * compile bytecode, and dispatch lifecycle events.
         */
        kScriptManager *getScriptManager();

        /**
         * @brief Starts script execution across every active scene.
         *
         * Builds a private module instance for each active script component
         * (preferring compiled bytecode), then dispatches Awake() to all
         * instances followed by Start(). Call once when gameplay begins.
         */
        void startScripts();

        /**
         * @brief Stops script execution: dispatches OnDestroy() and releases
         *        every script instance. Call when gameplay ends.
         */
        void stopScripts();

        /**
         * @brief Dispatches Update() then LateUpdate() to all running scripts.
         * @param deltaTime Seconds since the last frame.
         */
        void updateScripts(float deltaTime);

        /**
         * @brief Dispatches FixedUpdate() to all running scripts.
         * @param fixedDeltaTime Seconds of the fixed (physics) step.
         */
        void fixedUpdateScripts(float fixedDeltaTime);

        /** @brief Returns true between startScripts() and stopScripts(). */
        bool getScriptsRunning() const { return scriptsRunning; }

        // --- Physics lifecycle (standalone runtime) -------------------------

        /**
         * @brief Spawns physics bodies and character controllers from every
         *        object's editor-authored descriptor, seeded at its current
         *        world transform. Call once when gameplay begins.
         *
         * The editor drives physics itself; this is for a built game running
         * the world directly.
         */
        void startPhysics();

        /** @brief Steps the simulation and syncs transforms back into nodes. */
        void updatePhysics(float deltaTime);

        /** @brief Destroys all bodies/characters and shuts the simulation down. */
        void stopPhysics();

        /** @brief Returns true between startPhysics() and stopPhysics(). */
        bool getPhysicsRunning() const { return physicsRunning; }

        /**
         * @brief Serialises the world to JSON.
         * @param startScene Index of the first scene to include (default 0).
         * @return JSON object with UUID, scenes, and cameras.
         */
        virtual json serialize(int startScene = 0);

        /**
         * @brief Restores the world from a JSON object.
         * @param data JSON produced by serialize().
         */
        virtual void deserialize(json data);

        /**
         * @brief Loads a world from a serialized @c scene.world file, standalone.
         *
         * Reconstructs every scene and object (meshes, lights, cameras, empties)
         * with their transforms and components (physics, character, navigation,
         * scripts), resolving mesh references and script bytecode relative to the
         * file's folder. Designed for a built game running without the editor.
         *
         * Requires setAssetManager() to have been called first. Picks the first
         * camera found as the main camera.
         *
         * @param path Path to @c scene.world; asset data is resolved relative to
         *             its parent folder (Library/ImportedAssets, Library/Scripts).
         * @return true on success.
         */
        bool loadFromFile(const kString &path);

    protected:
    private:
        /// Collects every object across all active scenes into a flat list.
        std::vector<kObject *> collectAllObjects();
        /// Ensures the component's script asset is registered; returns its UUID.
        kString resolveScriptAsset(kScript &component);

        kAssetManager *assetManager = nullptr; ///< Asset loader reference.

        std::vector<kScene *>  scenes;  ///< Registered scenes.
        std::vector<kCamera *> cameras; ///< Registered cameras.

        kCamera *mainCamera = nullptr; ///< Active render camera.

        kScriptManager *scriptManager  = nullptr; ///< AngelScript manager (world-owned).
        bool            scriptsRunning = false;   ///< True while scripts are executing.

        kPhysicsManager       *physicsManager  = nullptr; ///< Physics world (runtime-owned).
        bool                   physicsRunning  = false;   ///< True while physics is stepping.
        std::vector<kObject *> physicsBodies;             ///< Nodes with a live rigid body.
        std::vector<kObject *> characterBodies;           ///< Nodes with a live character.

        kString uuid; ///< World UUID.
    };
}

#endif // KWORLD_H
