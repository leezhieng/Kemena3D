#ifndef PANEL_WORLD_H
#define PANEL_WORLD_H

#include <algorithm>
#include <vector>

#include "kemena/kemena.h"

#include <glm/gtx/matrix_decompose.hpp>

#include "manager.h"
#include "commands.h"
#include <ImGuizmo.h>

using namespace kemena;

class Manager;

/**
 * @brief Editor "World" viewport panel.
 *
 * Hosts the main 3D scene viewport in the studio editor. Draws the rendered
 * scene, the ImGuizmo transform gizmo (with undo support), a pivot-mode and
 * render-mode toolbar, and an offscreen camera-preview overlay when a kCamera
 * node is selected.
 */
class PanelWorld
{
public:
    bool enabled = false; ///< Whether the panel is interactive (true when a project is open).
    bool hovered = false; ///< True when the mouse is hovering over the viewport.
    bool focused = false; ///< True when the viewport has input focus.

    int   width       = 0;          ///< Current viewport width in pixels.
    int   height      = 0;          ///< Current viewport height in pixels.
    float aspectRatio = 0;          ///< Viewport aspect ratio (width / height).
    kVec2 panelPos   = kVec2(0.f, 0.f); ///< Screen-space position of the panel's content region.

    /**
     * @brief Construct the world viewport panel.
     * @param setGuiManager GUI manager used to issue ImGui draw calls.
     * @param setManager    Owning editor manager providing scene/selection state.
     */
    PanelWorld(kGuiManager *setGuiManager, Manager *setManager);

    /**
     * @brief Draw the world viewport for the current frame.
     *
     * Renders the scene, toolbar, transform gizmo and camera-preview overlay,
     * and applies any gizmo-driven transform changes (with undo) to the
     * current selection.
     *
     * @param isOpened      In/out flag controlling panel visibility.
     * @param renderer      Renderer producing the scene image for the viewport.
     * @param editorCamera  Editor camera used to view and navigate the scene.
     */
    void draw(bool &isOpened, kRenderer *renderer, kCamera *editorCamera);

    Manager    *manager; ///< Owning editor manager (scene, selection, pivot mode).
    kGuiManager *gui;    ///< GUI manager used for ImGui rendering.

private:
    bool wasGizmoUsing = false;                  ///< Whether the gizmo was being dragged on the previous frame (for undo detection).
    std::vector<TransformState> gizmoStartStates; ///< Selected-node transforms captured at the start of a gizmo drag, used to build the undo command.

    // Toolbar icon textures (loaded from embedded resources).
    uint32_t iconPivotIndividual = 0;  ///< "Individual origins" pivot-mode icon.
    uint32_t iconPivotCenter     = 0;  ///< "Median point" (centre) pivot-mode icon.
    uint32_t iconPivotLastSelected = 0; ///< "Active element" (last selected) pivot-mode icon.
    uint32_t iconAlignLocal      = 0;  ///< Gizmo "Local" transform-space icon.
    uint32_t iconAlignWorld      = 0;  ///< Gizmo "World" transform-space icon.
    uint32_t iconCamera          = 0;  ///< Editor camera-settings button icon.

    /// Lazy-initialised offscreen renderer used for the camera-preview overlay
    /// shown at the bottom-right when a kCamera is the primary selection.
    kOffscreenRenderer *cameraPreview = nullptr;
};

#endif
