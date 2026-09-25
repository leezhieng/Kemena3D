#pragma once
#include "kemena/kemena.h"
#include "kemena/kshadernode.h"
#include "manager.h"
#include <SDL3/SDL_dialog.h>
#include <string>
#include <filesystem>

using namespace kemena;
namespace fs = std::filesystem;

/**
 * @brief Editor panel that hosts the visual shader node-graph editor.
 *
 * Renders a pannable/zoomable canvas of shader nodes, lets the user create,
 * move, connect, and delete nodes, and handles loading, saving, and compiling
 * the graph to a .shader asset. Owns the active kShaderGraph and all canvas
 * interaction state.
 */
class PanelShaderGraph
{
public:
    bool focused = false;       ///< Set each draw() — used by main.cpp's Ctrl+S routing.

    /**
     * @brief Construct the shader editor panel.
     * @param setGui GUI/ImGui manager used for drawing.
     * @param setManager Kemena3D manager providing project state and paths.
     */
    PanelShaderGraph(kGuiManager* setGui, Manager* setManager);

    /**
     * @brief Draw the panel and process its interaction for one frame.
     * @param isOpened In/out flag controlling panel visibility; cleared when closed.
     */
    void draw(bool& isOpened);

    /**
     * @brief Open a .shader file into the editor.
     *
     * Called from the project panel on double-click.
     * @param path Filesystem path of the .shader file to load.
     */
    void openFile(const std::string& path);

    /**
     * @brief Save the current graph (no-op when nothing is loaded).
     *
     * Public so the global Ctrl+S handler can route here when this panel has focus.
     */
    void saveCurrent() { saveGraph(); }

    /** @brief Returns the currently open .shader file path ("" when unsaved). */
    std::string getFilePath() const { return filePath; }

private:
    // -----------------------------------------------------------------------
    // Core state
    // -----------------------------------------------------------------------
    kGuiManager* gui     = nullptr;        ///< GUI/ImGui manager used for drawing.
    Manager*     manager = nullptr;        ///< Kemena3D manager providing project state and paths.

    kShaderGraph graph;                    ///< The shader node graph currently being edited.
    std::string  filePath;                 ///< Current .shader file path (empty = unsaved).
    std::string  lastCompileError;         ///< Error text from the most recent compile attempt.
    bool         compiled = false;         ///< True if the last compile succeeded.

    // -----------------------------------------------------------------------
    // Canvas navigation
    // -----------------------------------------------------------------------
    ImVec2 canvasOffset = { 0.f, 0.f };  ///< Pan offset in canvas space.
    float  canvasZoom   = 1.f;           ///< Current canvas zoom factor.
    bool   isPanning    = false;         ///< True while the user is panning the canvas.
    ImVec2 panStartMouse;                ///< Mouse position when panning began.
    ImVec2 panStartOffset;               ///< Canvas offset when panning began.

    // -----------------------------------------------------------------------
    // Interaction state
    // -----------------------------------------------------------------------
    int  selectedNode  = -1;      ///< Id of the node being moved (-1 = none).
    bool isDraggingNode= false;   ///< True while a node is being dragged.
    ImVec2 dragNodeOffset;        ///< Cursor offset within node at drag start.
    bool isResizingComment = false; ///< True while a comment box is being resized.

    // Connection drag
    bool   isDraggingLink = false;            ///< True while dragging a connection link.
    int    dragFromNode   = -1;               ///< Source node id of the link being dragged.
    int    dragFromPin    = -1;               ///< Source pin id of the link being dragged.
    bool   dragFromOutput = true;             ///< True if the drag started from an output pin.
    kPinType dragPinType  = kPinType::Float;  ///< Pin type of the link being dragged.

    // Context menu
    ImVec2 nodeMenuPos;           ///< Canvas position where the right-click context menu opened.

    // -----------------------------------------------------------------------
    // Node size constants
    // -----------------------------------------------------------------------
    static constexpr float NODE_WIDTH     = 160.f;  ///< Default node body width (canvas units).
    static constexpr float NODE_HEADER_H  = 24.f;   ///< Node header bar height (canvas units).
    static constexpr float PIN_RADIUS     =  5.f;   ///< Pin circle radius (canvas units).
    static constexpr float PIN_ROW_H      = 20.f;   ///< Vertical spacing between pin rows (canvas units).
    static constexpr float PIN_PAD_X      = 10.f;   ///< Horizontal padding of pins from the node edge.

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    /** @brief Draw the top toolbar (new/save/compile buttons and status). */
    void drawToolbar();

    /** @brief Draw the node-graph canvas and handle its pan/zoom and interaction. */
    void drawCanvas();

    /**
     * @brief Draw a single shader node and update its pin screen positions.
     * @param dl ImGui draw list to render into.
     * @param node Node to draw (pin uiX/uiY are updated as a side effect).
     * @param origin Screen-space origin of the canvas.
     */
    void drawNode(ImDrawList* dl, kShaderNode& node, ImVec2 origin);

    /** @brief Draw a pass-through anchor node. */
    void drawAnchorNode(ImDrawList* dl, kShaderNode& node, ImVec2 origin);

    /** @brief Draw a resizable, editable comment box. */
    void drawCommentNode(ImDrawList* dl, kShaderNode& node, ImVec2 origin);

    /**
     * @brief Draw all established connection links between node pins.
     * @param dl ImGui draw list to render into.
     * @param origin Screen-space origin of the canvas.
     */
    void drawLinks(ImDrawList* dl, ImVec2 origin);

    /**
     * @brief Draw the in-progress link that follows the cursor during a connection drag.
     * @param dl ImGui draw list to render into.
     */
    void drawDragLink(ImDrawList* dl);

    /** @brief Draw the right-click context menu for adding nodes. */
    void drawNodeContextMenu();

    /**
     * @brief Convert a canvas-space position to screen space.
     * @param canvasPos Position in canvas coordinates.
     * @param origin Screen-space origin of the canvas.
     * @return Equivalent screen-space position.
     */
    ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;

    /**
     * @brief Convert a screen-space position to canvas space.
     * @param screenPos Position in screen coordinates.
     * @param origin Screen-space origin of the canvas.
     * @return Equivalent canvas-space position.
     */
    ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

    /**
     * @brief Get the screen position of a pin, as computed during drawNode().
     * @param nodeId Owning node id.
     * @param pinId Pin id within the node.
     * @param origin Screen-space origin of the canvas.
     * @return Pin screen position, or {0,0} if the node/pin is not found.
     */
    ImVec2 getPinScreenPos(int nodeId, int pinId, ImVec2 origin) const;

    /** @brief Result of a pin hit-test: the node/pin under the cursor and its kind/type. */
    struct HitPin
    {
        int nodeId = -1;                    ///< Node id under the cursor (-1 = none).
        int pinId = -1;                     ///< Pin id under the cursor (-1 = none).
        bool isOutput = false;              ///< True if the hit pin is an output pin.
        kPinType type = kPinType::Float;    ///< Data type of the hit pin.
    };

    /**
     * @brief Find the node and pin under the cursor.
     * @param mouseScreen Cursor position in screen space.
     * @param origin Screen-space origin of the canvas.
     * @return HitPin describing the pin under the cursor, or default {-1,-1} if none.
     */
    HitPin hitTestPins(ImVec2 mouseScreen, ImVec2 origin) const;

    // File I/O

    /** @brief Reset to a fresh, empty shader graph with a new UUID. */
    void newGraph();

    /** @brief Save the graph to its current file path, prompting if none is set. */
    void saveGraph();

    /** @brief Prompt for a destination and save the graph as a new .shader file. */
    void saveGraphAs();

    /**
     * @brief Load a shader graph from a .shader file.
     * @param path Filesystem path of the file to load.
     */
    void loadGraph(const std::string& path);

    /** @brief Compile the current graph and export the resulting shader. */
    void compileAndExport();

    /**
     * @brief SDL file-dialog callback that completes a "Save As" operation.
     * @param userdata Pointer to the owning PanelShaderGraph instance.
     * @param filelist Null-terminated list of chosen paths (first is used).
     * @param filter Index of the selected file filter (unused).
     */
    static void SDLCALL saveShaderCallback(void* userdata, const char* const* filelist, int filter);

    /**
     * @brief Generate a random 32-character hexadecimal UUID for a new graph.
     * @return Newly generated UUID string.
     */
    static std::string generateUuid();
};
