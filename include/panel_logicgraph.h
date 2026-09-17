#ifndef PANEL_LOGICGRAPH_H
#define PANEL_LOGICGRAPH_H

#include "kemena/kemena.h"
#include "kemena/kscriptgraph.h"
#include "manager.h"

#include "imgui.h"

#include <string>
#include <vector>

using namespace kemena;

/**
 * @brief Visual-scripting node-graph editor panel.
 *
 * Edits a kScriptGraph on a pannable ImGui canvas (custom draw-list rendering,
 * matching the shader editor's style). Saving writes the editable @c .logic
 * JSON to the project's Assets folder and regenerates the AngelScript source
 * to @c Library/GeneratedScripts/<logic-uuid>.as via kScriptGraphCompiler —
 * that generated script lives outside Assets/ (a temp build artifact) and
 * flows through the normal bytecode pipeline (Manager::buildScripts()).
 */
class PanelLogicGraph
{
public:
    bool focused = false; ///< Set each draw() — used by main.cpp's Ctrl+S routing.

    /**
     * @brief Constructs the editor and starts with an empty graph.
     * @param setGui     GUI manager used for ImGui rendering context.
     * @param setManager Studio manager (project paths, script build pipeline).
     */
    PanelLogicGraph(kGuiManager *setGui, Manager *setManager);

    /** @brief Draws the panel; @p isOpened is cleared when the window closes. */
    void draw(bool &isOpened);

    /** @brief Loads a @c .logic file for editing. */
    void openFile(const std::string &path);

    /** @brief Save the current graph (Ctrl+S target). No-op when nothing loaded. */
    void saveCurrent() { saveGraph(); }

    /** @brief Returns the currently loaded .logic file path ("" when untitled). */
    std::string getFilePath() const { return filePath; }

    /**
     * @brief Notifies the editor that an asset was renamed/moved on disk.
     *
     * If the currently-open .logic is the one that moved, its tracked path is
     * updated so a subsequent Save writes to the new location instead of
     * recreating the file at the old path.
     */
    void notifyAssetMoved(const std::string &oldPath, const std::string &newPath);

private:
    /** @brief Resets to a fresh, untitled graph with a new UUID. */
    void newGraph();

    /**
     * @brief Loads and parses a @c .logic file into the current graph.
     * @param path Filesystem path to the @c .logic file.
     * @return @c true on success; @c false if the file cannot be opened/parsed.
     */
    bool loadGraph(const std::string &path);

    /**
     * @brief Writes .logic + regenerates .as; prompts if untitled.
     * @return @c true if the graph was saved.
     */
    bool saveGraph();

    /**
     * @brief Prompts for a destination and saves the graph under a new path.
     * @return @c true if a path was chosen and the save succeeded.
     */
    bool saveGraphAs();

    /** @brief Compiles the graph and writes the generated .as. */
    void regenerateScript();

    /**
     * @brief Reports a save/load status message to the Console panel.
     *
     * Status output intentionally goes to the console instead of the toolbar so
     * the Logic Graph header stays limited to its file name. When no console
     * panel exists (headless / early start-up) the message is kept in
     * @c statusLine as a fallback.
     * @param level Severity used for the console entry's text colour.
     * @param msg   Message text.
     */
    void logStatus(LogLevel level, const std::string &msg);

    // Canvas <-> screen coordinate mapping (pan only; zoom is fixed at 1:1).

    /**
     * @brief Maps a canvas-space point to screen-space (applies pan offset).
     * @param canvasPos Point in canvas coordinates.
     * @param origin    Screen position of the canvas top-left.
     * @return The corresponding screen-space point.
     */
    ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;

    /**
     * @brief Maps a screen-space point back to canvas-space (removes pan offset).
     * @param screenPos Point in screen coordinates.
     * @param origin    Screen position of the canvas top-left.
     * @return The corresponding canvas-space point.
     */
    ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

    /** @brief Draws the top toolbar (New/Open/Save/Save As, centered file name). */
    void drawToolbar();

    /** @brief Draws the side panel listing the graph's variables. */
    void drawVariablesPanel();

    /** @brief Draggable splitter between the variables column and the canvas. */
    void drawVariablesSplitter();

    /**
     * @brief Keeps Get/Set Variable pin types in sync with their selected
     *        variable's declared type.
     *
     * Called every frame before the canvas is drawn so a variable whose type
     * changed immediately retypes the pins of nodes referencing it.
     */
    void syncVariableNodePins();

    /** @brief Draws the pannable node-graph canvas and handles its interactions. */
    void drawCanvas();

    /**
     * @brief Draws a single node with its header, pins and payload rows.
     * @param dl     Draw list to render into.
     * @param node   Node to draw (mutated for drag/selection state).
     * @param origin Screen position of the canvas top-left.
     */
    void drawNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin);

    /** @brief Draws a pass-through anchor (reroute) node. */
    void drawAnchorNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin);

    /** @brief Draws a resizable, editable comment box. */
    void drawCommentNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin);

    /**
     * @brief Draws the bezier links connecting node pins.
     * @param dl Draw list to render into.
     */
    void drawLinks(ImDrawList *dl);

    /**
     * @brief Draws the right-click "Add Node" context menu.
     * @param canvasSpawnPos Canvas-space position where a new node is placed.
     */
    void drawAddNodeMenu(ImVec2 canvasSpawnPos);

    /**
     * @brief Attempts to connect two pins; silently ignores invalid pairings.
     * @param nodeA Source node id.
     * @param pinA  Source pin index.
     * @param nodeB Destination node id.
     * @param pinB  Destination pin index.
     */
    void tryConnect(int nodeA, int pinA, int nodeB, int pinB);

    /**
     * @brief Collects the unique action names currently bound in Project Settings.
     *
     * Used to populate the Get Action / Get Action Pressed / Get Action Released
     * / Get Axis node picker so node action names can't silently drift from the
     * bindings that getAction()/getActionPressed()/getActionReleased() resolve.
     * @return De-duplicated list of action names (empty when no project is open).
     */
    std::vector<std::string> actionNameList() const;

    /** @brief Copies the currently selected node into the clipboard. */
    void copySelectedNode();

    /** @brief Pastes the clipboard node offset from its original position. */
    void pasteClipboard();

    kGuiManager *gui     = nullptr;
    Manager     *manager = nullptr;

    kScriptGraph graph;
    std::string  filePath; ///< Current .logic path ("" = untitled).

    ImVec2 canvasOffset = ImVec2(0.0f, 0.0f);
    float  canvasZoom   = 1.0f;                ///< Canvas zoom factor (mouse wheel).
    ImVec2 canvasOrigin = ImVec2(0.0f, 0.0f); ///< Canvas top-left, refreshed each frame.

    // Pan state (middle-mouse drag). Persistent so panning keeps working when
    // the cursor moves over a node body (the node's button only owns the left
    // button, so it must not gate the pan).
    bool   isPanning      = false;
    ImVec2 panStartMouse  = ImVec2(0.0f, 0.0f);
    ImVec2 panStartOffset = ImVec2(0.0f, 0.0f);

    int selectedNode = 0;

    // Link-drag state.
    bool linkDragging   = false;
    int  dragNode       = 0;
    int  dragPin        = 0;
    bool dragFromOutput = false;

    // Node-move state.
    int movingNode = 0;

    // Comment resize state.
    int resizingComment = 0;

    // Copy/paste clipboard (single node).
    bool            hasClipboard  = false;
    kScriptGraphNode clipboardNode;

    std::string statusLine; ///< Last status message (fallback when no console exists).

    ///< Throttles re-reading project.json so the input-action picker stays in
    ///< sync with Project Settings without opening the file every frame.
    int inputRefreshCounter = 0;

    float variablesPanelWidth = 227.0f; ///< Current width of the left variables column (matches the animator panel's default).
};

#endif // PANEL_LOGICGRAPH_H
