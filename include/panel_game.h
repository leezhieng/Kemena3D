#ifndef PANEL_GAME_H
#define PANEL_GAME_H

#include "kemena/kemena.h"
#include <kemena/koffscreenrenderer.h>
#include "manager.h"

#include <GL/glew.h>
#include <imgui.h>
#include <vector>

using namespace kemena;

class Manager;

/**
 * @brief Runtime play state of the in-editor game panel.
 */
enum class GamePlayState
{
    Stopped, ///< Game is not running; the editor scene is shown/edited normally.
    Playing, ///< Game is running and being simulated in real time.
    Paused   ///< Game is running but frozen (physics/animations receive dt = 0).
};

/**
 * @brief Viewport aspect-ratio presets for the game view.
 *
 * Non-free presets letterbox the rendered image inside the panel so the game
 * sees the chosen aspect ratio regardless of how the panel is docked/resized.
 */
enum class GameAspectRatio
{
    Free = 0,   ///< Fill the whole panel (free aspect).
    Ratio3_2,   ///< 3:2 letterboxed.
    Ratio4_3,   ///< 4:3 letterboxed.
    Ratio5_4,   ///< 5:4 letterboxed.
    Ratio16_9,  ///< 16:9 letterboxed.
    Ratio16_10, ///< 16:10 letterboxed.
    Custom      ///< User-defined width:height ratio.
};

/**
 * @brief Snapshot of a single object's full state.
 *
 * Captured for every scene node when play begins so the original editor scene
 * can be fully restored when play stops. Stores a complete JSON serialization
 * of the object so non-transform properties (camera FOV, light power, materials,
 * etc.) edited during play are also reverted on stop.
 */
struct ObjectTransformSnapshot
{
    kString        uuid;  ///< UUID of the object this snapshot belongs to.
    kVec3          pos;   ///< Position at capture time.
    kQuat          rot;   ///< Rotation at capture time.
    kVec3          scale; ///< Scale at capture time.
    bool           active; ///< Active/enabled flag at capture time.
    nlohmann::json state; ///< Full serialized state for non-transform properties.
};

/**
 * @brief ImGui panel that previews and runs the game inside the editor.
 *
 * Renders the scene through an offscreen renderer using the game camera and
 * exposes Play/Pause/Stop controls. While playing it owns a snapshot of the
 * scene so edits made during play can be rolled back on stop.
 */
class PanelGame
{
public:
    /**
     * @brief Construct the game panel.
     * @param gui     GUI manager used to issue ImGui draw calls.
     * @param manager Owning editor manager providing world/scene access.
     */
    PanelGame(kGuiManager* gui, Manager* manager);

    /** @brief Destroy the panel. The renderer is owned by Manager::gameRenderer. */
    ~PanelGame();

    /**
     * @brief Draw the game panel for the current frame.
     * @param isOpened In/out flag toggled by the window's close button.
     */
    void draw(bool& isOpened);

    /**
     * @brief Render the game viewport into the dedicated game kRenderer's FBO.
     *
     * Called from the main loop (with the game renderer's driver current) so the
     * result texture is ready before draw() displays it. Resolves the game camera
     * and its scene, swaps it in as the world's main camera for the draw, and
     * restores the previous main camera afterwards. A no-op when the game
     * renderer or a game camera/scene is unavailable.
     *
     * @param viewportW Viewport width in pixels.
     * @param viewportH Viewport height in pixels.
     * @param dt        Delta time used for animation/skinning.
     */
    void renderGame(int viewportW, int viewportH, float dt);

    /** @brief Get the current play state (Stopped, Playing or Paused). */
    GamePlayState getPlayState() const { return playState; }

    /**
     * @brief Compute the delta time the game should advance by.
     *
     * Returns 0 when paused so physics/animations freeze, otherwise the real dt.
     * @param dt Real frame delta time in seconds.
     * @return 0 while paused, @p dt otherwise.
     */
    float getEffectiveDeltaTime(float dt) const;

    /** @brief Start playing: capture the scene snapshot and enter Playing state. */
    void pressPlay();

    /** @brief Toggle between Playing and Paused while the game is running. */
    void pressPause();

    /** @brief Stop playing: restore the scene snapshot and return to Stopped state. */
    void pressStop();

    /** @brief Get the selected game-viewport aspect-ratio preset. */
    GameAspectRatio getAspectRatio() const { return aspectRatio; }

    /** @brief Set the game-viewport aspect-ratio preset. */
    void setAspectRatio(GameAspectRatio value) { aspectRatio = value; }

    /** @brief Get the custom preset width component. */
    float getCustomAspectW() const { return customAspectW; }

    /** @brief Set the custom preset width component. */
    void setCustomAspectW(float value) { customAspectW = value; }

    /** @brief Get the custom preset height component. */
    float getCustomAspectH() const { return customAspectH; }

    /** @brief Set the custom preset height component. */
    void setCustomAspectH(float value) { customAspectH = value; }

    Manager*     manager; ///< Owning editor manager (world, scene, object lookup).
    kGuiManager* gui;     ///< GUI manager used for ImGui rendering.

    int width  = 0; ///< Last viewport width published by draw(); consumed by the main-loop render.
    int height = 0; ///< Last viewport height published by draw(); consumed by the main-loop render.

private:
    GamePlayState playState = GamePlayState::Stopped;        ///< Current play state.
    GameAspectRatio aspectRatio = GameAspectRatio::Free;     ///< Viewport aspect-ratio preset.
    float customAspectW = 16.0f;                             ///< Custom preset width part.
    float customAspectH = 9.0f;                              ///< Custom preset height part.
    std::vector<ObjectTransformSnapshot> sceneSnapshot;      ///< Saved transforms for restore on stop.
    bool projectSavedBeforePlay = true;                      ///< projectSaved value captured when Play was pressed.


    // Transport-control icon textures (loaded from embedded resources).
    uint32_t iconPlay = 0;   ///< Play icon texture handle.
    uint32_t iconPause = 0;  ///< Pause icon texture handle.
    uint32_t iconStop = 0;   ///< Stop icon texture handle.

    // Live FPS readout, sampled only while the game is Playing.
    float fpsElapsed = 0.0f; ///< Seconds accumulated in the current FPS sample window.
    int   fpsFrames = 0;     ///< Frames counted in the current FPS sample window.
    float currentFps = 0.0f; ///< Most recently measured frames-per-second.

    /**
     * @brief Resolve the camera used to render the game view.
     *
     * Prefers the world's explicitly-set default camera (when still registered
     * and not the editor camera).
     * @return The game camera, or nullptr if none is available (black screen).
     */
    kCamera* findGameCamera() const;

    /**
     * @brief Resolve the scene a game camera should render.
     *
     * Uses the camera's assigned scene_uuid when set and present in the world,
     * otherwise falls back to the active scene.
     * @param camera Game camera whose scene is resolved.
     * @return Matching scene, or nullptr when the camera is null.
     */
    kScene* resolveGameScene(kCamera* camera) const;

    /** @brief Capture transform snapshots for the whole scene before play starts. */
    void captureSnapshot();

    /** @brief Restore all captured transform snapshots when play stops. */
    void restoreSnapshot();

    /**
     * @brief Recursively snapshot a node and its descendants.
     * @param node Node to capture; ignored if nullptr.
     */
    void captureNodeRecursive(kObject* node);
};

#endif // PANEL_GAME_H
