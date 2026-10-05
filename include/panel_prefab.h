#ifndef PANEL_PREFAB_H
#define PANEL_PREFAB_H

#include <vector>

#include "kemena/kemena.h"

#include "manager.h"
#include "commands.h"
#include <ImGuizmo.h>

using namespace kemena;

class Manager;

/**
 * @brief Prefab editor panel.
 *
 * Mirrors PanelWorld but renders an isolated kScene that contains only the
 * prefab subtree being edited. While this panel is open the main world view
 * is hidden so the user focuses on the prefab in isolation.
 *
 * The actual scene + camera live on the Manager (manager->prefabScene /
 * manager->prefabCamera) so that main.cpp can render them through the
 * existing single-FBO render path.
 */
class PanelPrefab
{
public:
    bool enabled = false; ///< Whether prefab editing is active (project open and prefab being edited); controls panel auto-show/hide and disabled state.
    bool hovered = false; ///< True while the mouse is over the panel/viewport.
    bool focused = false; ///< True while the panel/viewport has keyboard focus.

    int   width       = 0;            ///< Current viewport width in pixels.
    int   height      = 0;            ///< Current viewport height in pixels.
    float aspectRatio = 0;            ///< Viewport aspect ratio (width / height), defaulting to 1 when height is zero.
    kVec2 panelPos   = kVec2(0.f, 0.f); ///< Top-left screen position of the viewport image, used to position the ImGuizmo manipulator.

    /**
     * @brief Constructs the prefab editor panel.
     * @param setGuiManager GUI manager used to build the ImGui panel and widgets.
     * @param setManager     Owning Manager that holds the prefab scene, camera, renderer and editing state.
     */
    PanelPrefab(kGuiManager *setGuiManager, Manager *setManager);

    /**
     * @brief Draws the prefab editor panel for the current frame.
     *
     * Auto-shows/hides itself based on the Manager's prefab editing state, renders
     * the prefab's offscreen viewport image, provides Save & Close / Discard toolbar
     * actions, and drives a single-object ImGuizmo manipulator (with undo/redo capture)
     * on the selected object.
     *
     * @param isOpened In/out flag tracking whether the window is open; updated to match the editing state.
     */
    void draw(bool &isOpened);

    Manager     *manager; ///< Owning Manager providing prefab scene, camera, renderer, selection and undo/redo state.
    kGuiManager *gui;     ///< GUI manager used for ImGui window and widget calls.

private:
    bool wasGizmoUsing = false; ///< Tracks whether the gizmo was being manipulated last frame, to detect drag start/end for undo/redo.
    std::vector<TransformState> gizmoStartStates; ///< Snapshot of selected transforms captured at the start of a gizmo drag, used to build the undo command.

    /// Lazy offscreen renderer for the selected camera's preview overlay,
    /// mirroring the World panel. Lives in the prefab driver's GL context.
    kOffscreenRenderer *cameraPreview = nullptr;
    /// Asset manager the preview renderer was built against; when the prefab
    /// editor is reopened the prefab asset manager changes, so the renderer is
    /// recreated to keep its GL resources valid.
    kAssetManager      *cameraPreviewAsset = nullptr;
};

#endif
