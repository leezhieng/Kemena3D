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

    /** @brief Destroy the panel and release its offscreen renderer. */
    ~PanelGame();

    /**
     * @brief Draw the game panel for the current frame.
     * @param isOpened In/out flag toggled by the window's close button.
     */
    void draw(bool& isOpened);

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

    Manager*     manager; ///< Owning editor manager (world, scene, object lookup).
    kGuiManager* gui;     ///< GUI manager used for ImGui rendering.

private:
    GamePlayState playState = GamePlayState::Stopped;        ///< Current play state.
    std::vector<ObjectTransformSnapshot> sceneSnapshot;      ///< Saved transforms for restore on stop.
    bool projectSavedBeforePlay = true;                      ///< projectSaved value captured when Play was pressed.

    kOffscreenRenderer* gameRenderer = nullptr; ///< Offscreen renderer the game view is drawn into.
    int lastRendererW = 0;                      ///< Last offscreen render target width, for resize detection.
    int lastRendererH = 0;                      ///< Last offscreen render target height, for resize detection.

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
