#ifndef MODEL_PREVIEW_H
#define MODEL_PREVIEW_H

#include "kemena/kemena.h"
#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kscene.h>
#include <kemena/koffscreenrenderer.h>

#include <functional>
#include <string>

namespace kemena
{
    class kAnimator;
    class kAssetManager;
}

using namespace kemena;

/**
 * @brief Shared, self-contained 3D model preview widget used across the editor.
 *
 * Consolidates the interaction and rendering behaviour that used to be
 * duplicated by the Inspector's model viewer, material viewer and animation
 * preview as well as the Animator's embedded blend-tree preview. Every preview
 * hosted in a panel owns one ModelPreview so they all share the same:
 *
 *   * offscreen resolution and dark background,
 *   * orbit camera (LMB drag), scroll-to-zoom, and camera framing,
 *   * light controls (RMB drag to rotate the key light, corner light-bulb toggle),
 *   * "No preview available" placeholder and mouse cursors.
 *
 * The class owns its renderer, world, scene, camera and light, and holds a
 * borrowed (or owned) content mesh plus an optional animator. Callers can hook
 * onUpdate to advance an animation pose before each frame is rendered.
 */
class ModelPreview
{
public:
    /**
     * @brief Constructs the preview.
     * @param setAssetManager Asset manager used by the offscreen renderer to load built-in shaders.
     * @param setSize Edge length of the square offscreen target, in pixels.
     */
    explicit ModelPreview(kAssetManager *setAssetManager = nullptr, int setSize = 256);

    /** @brief Releases the renderer, preview world/scene/camera/light and any owned content. */
    ~ModelPreview();

    /** @brief Sets the asset manager used by the offscreen renderer. */
    void setAssetManager(kAssetManager *am);

    /** @brief Returns the asset manager currently used by the renderer. */
    kAssetManager *getAssetManager() const { return assetManager; }

    /** @brief Resizes the offscreen render target (recreates the FBO). */
    void resize(int newSize);

    // -----------------------------------------------------------------------
    // Content
    // -----------------------------------------------------------------------
    /**
     * @brief Attaches a mesh to the preview scene, replacing any previous content.
     * @param newMesh Mesh to display (borrowed from the asset manager unless takeOwnership).
     * @param takeOwnership When true the preview deletes the mesh on clear/destruction.
     */
    void setMesh(kMesh *newMesh, bool takeOwnership = false);

    /** @brief Returns the currently displayed mesh, or nullptr. */
    kMesh *getMesh() const { return mesh; }

    /** @brief Detaches and (when owned) destroys the current content mesh. */
    void clearMesh();

    /** @brief Sets the animator used to drive a skeletal pose (not owned). */
    void setAnimator(kAnimator *anim) { animator = anim; }

    /** @brief Returns the animator currently driving the preview, or nullptr. */
    kAnimator *getAnimator() const { return animator; }

    // -----------------------------------------------------------------------
    // Arbitrary scene content (e.g. a spawned prefab hierarchy)
    // -----------------------------------------------------------------------
    /**
     * @brief Registers an object that is already part of the preview scene as
     *        preview content. The preview does NOT own it — callers remain
     *        responsible for deleting it via removeContentObject()/clearContentObjects().
     */
    void addContentObject(kObject *obj);

    /** @brief Unregisters a content object and detaches it from the preview scene. */
    void removeContentObject(kObject *obj);

    /** @brief Detaches all registered content objects from the preview scene. */
    void clearContentObjects();

    /** @brief Registered content objects (e.g. prefab roots); not owned by the preview. */
    const std::vector<kObject *> &getContentObjects() const { return contentObjects; }

    /** @brief True when the preview has anything to render (a mesh or content objects). */
    bool hasContent() const { return mesh != nullptr || !contentObjects.empty(); }

    /**
     * @brief Invoked every render() with the frame delta before the offscreen
     *        pass. Animation previews use it to advance clip time and compute
     *        the blended pose. Pass an empty function to detach.
     */
    std::function<void(float dt)> onUpdate;

    // -----------------------------------------------------------------------
    // Camera / framing
    // -----------------------------------------------------------------------
    /** @brief Fits the orbit camera to the current content's combined bounds. */
    void frameToContent();

    /** @brief Resets orbit angle, distance, pivot and light angles to their defaults. */
    void resetView();

    // -----------------------------------------------------------------------
    // Render + draw
    // -----------------------------------------------------------------------
    /**
     * @brief Updates the orbit camera + light and renders the offscreen image.
     * @param dt Frame delta passed to onUpdate; negative uses ImGui's frame delta.
     */
    void render(float dt = -1.0f);

    /**
     * @brief Draws the rendered image as an ImGui widget and handles mouse
     *        interaction (orbit, zoom, light rotation, light-bulb toggle).
     * @param id Unique ImGui id for the interaction button.
     * @param centered Center the widget horizontally within the content region.
     * @param maxSize Maximum edge length, in pixels.
     */
    void draw(const char *id, bool centered = true, float maxSize = 256.0f);

    // -----------------------------------------------------------------------
    // Access
    // -----------------------------------------------------------------------
    kScene *getScene() const { return scene; }
    kWorld *getWorld() const { return world; }
    kCamera *getCamera() const { return camera; }
    kOffscreenRenderer *getRenderer() const { return renderer; }

    // -----------------------------------------------------------------------
    // Appearance / state
    // -----------------------------------------------------------------------
    /** @brief Turns the preview lighting on/off (the corner light-bulb). */
    void setLit(bool value) { lit = value; }

    /** @brief Whether the preview lighting is currently enabled. */
    bool isLit() const { return lit; }

    /** @brief Overrides the offscreen background colour. */
    void setBackgroundColor(kVec4 color) { backgroundColor = color; }

    /**
     * @brief When true the full scene is rendered even while unlit, so a
     *        material's ambient response stays visible (material viewer).
     */
    bool alwaysRenderScene = false;

    float rotX = 24.09f;      ///< Orbit pitch, in degrees.
    float rotY = 26.57f;      ///< Orbit yaw, in degrees.
    float camDist = 3.0f;     ///< Orbit camera distance from the pivot.
    kVec3 center = kVec3(0.0f); ///< Orbit pivot.
    float lightYaw = 45.0f;   ///< Key light azimuth, in degrees.
    float lightPitch = 60.0f; ///< Key light elevation, in degrees.

private:
    /** @brief Lazily creates the standalone preview world, scene, camera and light. */
    void ensureScene();

    /** @brief Applies the current light enabled/yaw/pitch state to the sun light. */
    void updateLight();

    kAssetManager *assetManager = nullptr;
    kOffscreenRenderer *renderer = nullptr;
    kWorld *world = nullptr;
    kScene *scene = nullptr;
    kCamera *camera = nullptr;
    kLight *light = nullptr;

    kMesh *mesh = nullptr;
    bool meshOwned = false;
    kAnimator *animator = nullptr;
    std::vector<kObject *> contentObjects; ///< Scene objects registered as content (not owned).

    kVec4 backgroundColor = kVec4(42.0f / 255.0f, 42.0f / 255.0f, 42.0f / 255.0f, 1.0f);
    int size = 256;
    bool lit = false;
    bool draggingModel = false;
    bool draggingLight = false;
};

#endif
