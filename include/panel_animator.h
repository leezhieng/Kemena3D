#pragma once
#include "kemena/kemena.h"
#include "manager.h"
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

using namespace kemena;
namespace fs = std::filesystem;

// ===========================================================================
// Data structures for the animation system
// ===========================================================================

/**
 * @brief Variable types supported in the animator controller.
 */
enum class AnimVariableType
{
    Bool,
    Float,
    Int,
    Trigger
};

/**
 * @brief A named variable used to drive animation state transitions.
 */
struct AnimVariable
{
    std::string      name;          ///< Variable name used in conditions.
    AnimVariableType type   = AnimVariableType::Float;
    float            defaultValue = 0.0f; ///< Stored as float; Bool: 0/1, Int: truncated.
};

/**
 * @brief Describes a transition condition, e.g. "Speed > 0.1".
 */
struct AnimCondition
{
    std::string variableName;  ///< Name of the variable to compare.
    enum Cmp { Greater, Less, Equal, NotEqual, GreaterEqual, LessEqual, IsTrue, IsFalse };
    Cmp    comparison = Greater;
    float  threshold  = 0.0f; ///< Used only for float/int comparisons.

    /** @brief Evaluate this condition against the current variable values. */
    bool evaluate(const std::unordered_map<std::string, float>& vars) const;
};

/**
 * @brief How a transition blends between its source and destination states.
 */
enum class AnimBlendMode
{
    Instant   = 0,  ///< Snap to the destination clip immediately.
    CrossFade = 1   ///< Blend from source to destination over blendDuration seconds.
};

/**
 * @brief A transition linking two animation states.
 */
struct AnimTransition
{
    int                 id          = -1;    ///< Unique id within the animator.
    int                 fromStateId = -1;    ///< Source state id.
    int                 toStateId   = -1;    ///< Destination state id.
    std::vector<AnimCondition> conditions;   ///< All conditions (AND logic).
    bool                hasExitTime = false; ///< If true, requires the source anim to play for exitTime seconds before checking conditions.
    float               exitTime    = 0.0f; ///< Seconds before the transition is considered.

    AnimBlendMode       blendMode     = AnimBlendMode::CrossFade; ///< Blending style used for the transition.
    float               blendDuration = 0.25f;                   ///< Cross-fade duration in seconds.
};

/**
 * @brief What a node on the animator canvas represents.
 */
enum class AnimStateKind
{
    State,    ///< A normal animation state that plays a clip.
    Anchor,   ///< Pass-through reroute node used to tidy transition lines.
    Comment   ///< Resizable background comment box with editable text.
};

/**
 * @brief A single animation state node on the graph canvas.
 */
struct AnimState
{
    int           id            = -1;      ///< Unique state id within the animator.
    AnimStateKind kind          = AnimStateKind::State; ///< Node kind on the canvas.
    std::string   name;                    ///< Display name (e.g. "Idle", "Walk").
    std::string   animationUuid;           ///< UUID of the .animation asset to play.
    float         speed         = 1.0f;    ///< Playback speed multiplier.
    bool          loop          = true;    ///< Whether the animation loops.
    bool          isDefault     = false;   ///< True if this is the entry/default state.

    // Canvas position
    float posX = 0.0f;
    float posY = 0.0f;

    // Comment-box payload (used only when kind == AnimStateKind::Comment).
    float       sizeX   = 320.0f;          ///< Comment box width (canvas units).
    float       sizeY   = 180.0f;          ///< Comment box height (canvas units).
    std::string comment = "Comment";       ///< Comment text shown on the box.

    /** @brief True when this is a real, playable animation state. */
    bool isState()   const { return kind == AnimStateKind::State; }
    /** @brief True when this is a pass-through anchor node. */
    bool isAnchor()  const { return kind == AnimStateKind::Anchor; }
    /** @brief True when this is a comment box. */
    bool isComment() const { return kind == AnimStateKind::Comment; }
};

/**
 * @brief Reference to an .animation asset used within the animator.
 */
struct AnimClipRef
{
    std::string uuid;  ///< UUID of the .animation file in the project.
    std::string name;  ///< Display name (derived from the file or user-defined).
};

/**
 * @brief Top-level animator controller data model.
 *
 * Mirrors the pattern used by kShaderGraph / kScriptGraph but dedicated
 * to animation state machines.
 */
struct AnimatorGraph
{
    std::string uuid;            ///< Unique identifier for this animator.
    std::string name;            ///< Display name.
    bool        dirty = false;   ///< True when unsaved changes exist.

    // Referenced .animation clips (keyed by UUID).
    std::unordered_map<std::string, AnimClipRef> clips;

    // Variables exposed to the animation system.
    std::vector<AnimVariable> variables;

    // States (nodes) on the graph.
    std::vector<AnimState> states;

    // Transitions (links) between states.
    std::vector<AnimTransition> transitions;

    // Node / link id counters for unique ids.
    int nextNodeId = 1;
    int nextLinkId = 1;

    /** @brief Generate a new unique node id. */
    int newNodeId() { return nextNodeId++; }

    /** @brief Generate a new unique link id. */
    int newLinkId() { return nextLinkId++; }

    /** @brief Find a state by id, or nullptr. */
    AnimState* findState(int id);

    /** @brief Find a transition by id, or nullptr. */
    AnimTransition* findTransition(int id);

    /** @brief Remove a state and all transitions connected to it. */
    void removeState(int stateId);

    /** @brief Remove a transition. */
    void removeTransition(int transId);

    /** @brief Remove all transitions connected from/to a given state. */
    void removeTransitionsForState(int stateId);

    /** @brief Serialize to JSON. */
    nlohmann::json toJson() const;

    /** @brief Deserialize from JSON. */
    void fromJson(const nlohmann::json& j);
};

// ===========================================================================
// Animator Editor Panel
// ===========================================================================

/**
 * @brief Editor panel for creating and editing .animator state-machine graphs.
 *
 * Provides a pannable/zoomable canvas with animation state nodes, transition
 * links, variable management, and .animation clip assignment. Modelled after
 * the shader-editor panel pattern.
 */
class PanelAnimator
{
public:
    bool focused = false;  ///< Set each draw() — used by main.cpp's Ctrl+S routing.
    bool visible = false;  ///< True while the animator editor window is open.

    PanelAnimator(kGuiManager* setGui, Manager* setManager);

    /** @brief Draw the panel and handle interaction. */
    void draw(bool& isOpened);

    /** @brief Open a .animator file into the editor. */
    void openFile(const std::string& path);

    /** @brief Returns the currently open .animator file path ("" when unsaved). */
    std::string getFilePath() const { return filePath; }

    /** @brief Save the current animator graph. */
    void saveCurrent() { saveGraph(); }

    /** @brief True when a state node is currently selected in the graph. */
    bool hasSelectedState() const { return selectedState >= 0; }

    /** @brief Id of the currently selected state node, or -1 when none. */
    int getSelectedState() const { return selectedState; }

    /** @brief True when a transition link is currently selected in the graph. */
    bool hasSelectedTransition() const { return selectedTransition >= 0; }

    /** @brief Id of the currently selected transition link, or -1 when none. */
    int getSelectedTransition() const { return selectedTransition; }

    /** @brief Draw the editing form for the selected state (called from the Inspector panel). */
    void drawSelectedStateInspector();

    /** @brief Draw the editing form for the selected transition (called from the Inspector panel). */
    void drawSelectedTransitionInspector();

    /** @brief Draw whichever animator element (transition or state) is selected in the Inspector panel. */
    void drawSelectedInspector();

    /** @brief True while the animation picker popup is open. */
    bool isAnimPickerOpen() const;

private:
    // -----------------------------------------------------------------------
    // Core state
    // -----------------------------------------------------------------------
    kGuiManager* gui     = nullptr;
    Manager*     manager = nullptr;

    AnimatorGraph graph;        ///< The animator controller being edited.
    std::string   filePath;     ///< Current .animator file path (empty = unsaved).

    // -----------------------------------------------------------------------
    // Canvas navigation
    // -----------------------------------------------------------------------
    ImVec2 canvasOffset = { 0.f, 0.f };
    float  canvasZoom   = 1.f;
    bool   isPanning    = false;
    ImVec2 panStartMouse;
    ImVec2 panStartOffset;
    ImVec2 canvasOrigin  = { 0.f, 0.f }; ///< Cached canvas top-left (screen space) for link preview.

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float  variablesPanelWidth = 340.f; ///< Current width of the left variables column.

    // -----------------------------------------------------------------------
    // Interaction state
    // -----------------------------------------------------------------------
    int  selectedState       = -1;
    int  selectedTransition  = -1;   ///< Id of the selected transition link; -1 = none.
    bool isDraggingState     = false;
    ImVec2 dragStateOffset;
    bool isResizingComment   = false; ///< True while a comment box is being resized.

    // Connection drag
    bool  isDraggingLink  = false;
    int   dragFromState   = -1;
    bool  dragFromOutput  = false;   ///< True when the link drag started from an output (right) pin.

    // Context menu
    ImVec2 contextMenuPos;


    // Variable editor
    int   editingVarIndex   = -1;   ///< Index into graph.variables being edited; -1 = none.

    // Animation picker popup state
    char        animPickerSearch[128] = {0};
    std::string animPickerSelected;

    // -----------------------------------------------------------------------
    // Node size constants
    // -----------------------------------------------------------------------
    static constexpr float NODE_WIDTH    = 180.f;
    static constexpr float NODE_HEADER_H = 26.f;
    static constexpr float PIN_RADIUS    = 6.f;
    static constexpr float PIN_ROW_H     = 22.f;
    static constexpr float PIN_PAD_X     = 10.f;
    // Body height is computed as max(60, PIN_ROW_H * 2) at usage sites.

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------
    void drawToolbar();
    void drawCanvas();
    void drawNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawAnchorNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawCommentNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawLinks(ImDrawList* dl, ImVec2 origin);
    void drawDragLink(ImDrawList* dl);
    void drawStateContextMenu();
    void drawVariablesPanel();
    void drawVariablesSplitter();
    void drawAnimPickerPopup(AnimState* state);
    void collectAnimationAssets(std::vector<std::string>& uuids,
                                std::vector<std::string>& names) const;

    // Coordinate helpers
    ImVec2 canvasToScreen(ImVec2 cp, ImVec2 origin) const;
    ImVec2 screenToCanvas(ImVec2 sp, ImVec2 origin) const;

    /** @brief Get the screen position of a state's input/output pin. */
    ImVec2 getInputPinPos(const AnimState& state, ImVec2 origin) const;
    ImVec2 getOutputPinPos(const AnimState& state, ImVec2 origin) const;

    /** @brief Hit-test: find which state's pin is under the cursor. */
    int hitTestInputPins(ImVec2 mouse, ImVec2 origin) const;
    int hitTestOutputPins(ImVec2 mouse, ImVec2 origin) const;

    /** @brief Hit-test: find which transition link is under the cursor. */
    int hitTestLinks(ImVec2 mouse, ImVec2 origin) const;

    // File I/O
    void newGraph();
    void saveGraph();
    void saveGraphAs();
    void loadGraph(const std::string& path);

    /** @brief Prompt to add a new .animation clip reference. */
    void promptAddClip();

    static std::string generateUuid();

    /** @brief SDL file-dialog callback for Save As. */
    static void SDLCALL saveAnimatorCallback(void* userdata, const char* const* filelist, int filter);
};
