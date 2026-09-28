#pragma once
#include "kemena/kemena.h"
#include "manager.h"
#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kscene.h>
#include <kemena/koffscreenrenderer.h>
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
    Int
    // NOTE: a "Trigger" type once existed but behaved identically to Bool —
    // kAnimator::setTrigger() just stores 1.0f and nothing ever auto-resets it,
    // so it could not be consumed. Old .animator files that still store
    // "Trigger" are mapped to Bool on load (see animVarTypeFromName).
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
 *
 * IMPORTANT: the numeric values are part of the on-disk .animator format, so
 * new kinds are appended at the end to keep existing files loadable.
 */
enum class AnimStateKind
{
    State     = 0,  ///< A normal animation state that plays a clip.
    Anchor    = 1,  ///< Pass-through reroute node used to tidy transition lines.
    Comment   = 2,  ///< Resizable background comment box with editable text.
    AnyState  = 3,  ///< Source-only node; its transitions can fire from any state.
    BlendTree = 4   ///< Blends several motions by a 1D or 2D parameter.
};

/**
 * @brief How a blend-tree node interpolates between its child motions.
 */
enum class AnimBlendType
{
    OneD = 0,  ///< A single float parameter places motions along an axis.
    TwoD = 1   ///< Two float parameters place motions on a 2D plane.
};

/**
 * @brief A single motion inside a blend tree.
 *
 * The child drives an existing animation state (referenced by id) so the blend
 * tree never duplicates clip assignments. The threshold / x,y values describe
 * where the child sits on the blend axis or plane.
 */
struct AnimBlendChild
{
    int   stateId   = -1;    ///< Id of the linked animation state that plays this motion.
    float threshold = 0.0f;  ///< 1D position along the blend axis.
    float posX      = 0.0f;  ///< 2D position (x) on the blend plane.
    float posY      = 0.0f;  ///< 2D position (y) on the blend plane.
    float speed     = 1.0f;  ///< Playback speed multiplier for this motion.
};

/**
 * @brief A single node on the animator canvas.
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

    // Blend-tree payload (used only when kind == AnimStateKind::BlendTree).
    AnimBlendType blendType   = AnimBlendType::OneD; ///< 1D axis or 2D plane.
    std::string   blendParamX;             ///< Float variable driving the x / 1D axis.
    std::string   blendParamY;             ///< Float variable driving the y axis (2D only).
    float         blendRangeXMin = -1.0f;  ///< Display/authoring range for the x axis.
    float         blendRangeXMax =  1.0f;  ///< Display/authoring range for the x axis.
    float         blendRangeYMin = -1.0f;  ///< Display/authoring range for the y axis (2D).
    float         blendRangeYMax =  1.0f;  ///< Display/authoring range for the y axis (2D).
    std::vector<AnimBlendChild> blendChildren; ///< Motions blended by this node.

    /** @brief True when this is a real, playable animation state. */
    bool isState()     const { return kind == AnimStateKind::State; }
    /** @brief True when this is the source-only Any State node. */
    bool isAnyState()  const { return kind == AnimStateKind::AnyState; }
    /** @brief True when this node blends several motions. */
    bool isBlendTree() const { return kind == AnimStateKind::BlendTree; }
    /** @brief True when this is a pass-through anchor node. */
    bool isAnchor()    const { return kind == AnimStateKind::Anchor; }
    /** @brief True when this is a comment box. */
    bool isComment()   const { return kind == AnimStateKind::Comment; }
    /** @brief True for node kinds that can be entered and play animation (State / BlendTree). */
    bool isPlayable()  const { return isState() || isBlendTree(); }
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

    /** @brief Releases the embedded blend-tree preview's render resources. */
    ~PanelAnimator();

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
    float  variablesPanelWidth = 227.f; ///< Current width of the left variables column (2/3 of the former 340 px default).

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

    // Blend-tree diagram drag
    int   dragBlendChildIndex = -1;  ///< Index of the blend child being dragged in a diagram; -1 = none.

    // Context menu
    ImVec2 contextMenuPos;


    // Variable editor
    int   editingVarIndex   = -1;   ///< Index into graph.variables being edited; -1 = none.

    // Live blend-parameter preview (Animator inspector sliders).
    // Keeps the scrubbed value separate from the asset's saved variable
    // defaults so previewing never marks the graph dirty. Keyed by variable
    // name; also drives the blend diagram's parameter marker.
    std::unordered_map<std::string, float> blendPreviewValues;

    // -----------------------------------------------------------------------
    // Embedded blend-tree preview (offscreen 3D view in the Inspector)
    //
    // Self-contained: it renders the selected preview mesh with its own
    // kAnimator, independent of the Game panel's Play mode, so a blend tree
    // can be scrubbed and watched without entering Play.
    // -----------------------------------------------------------------------
    kOffscreenRenderer* previewRenderer = nullptr; ///< Offscreen target, shown via ImGui::Image.
    kWorld*             previewWorld    = nullptr; ///< Standalone world (owns previewScene).
    kScene*             previewScene    = nullptr; ///< Scene holding the preview mesh.
    kCamera*            previewCamera   = nullptr; ///< Orbit camera for the preview.
    kMesh*              previewMesh     = nullptr; ///< Displayed mesh (owned by the asset manager).
    kMaterial*          previewMat      = nullptr; ///< Fallback material applied when the GLB ships none.
    kAnimator*          previewAnimator = nullptr; ///< Drives the blended pose.
    std::unordered_map<std::string, kSkeletalAnimation*> previewClips; ///< animationUuid → loaded clip (owned).
    std::string         previewMeshUuid;              ///< Mesh UUID currently loaded ("" = none).
    std::vector<std::string> previewMeshUuids;        ///< Selectable mesh UUIDs (derived from the graph).
    std::vector<std::string> previewMeshNames;        ///< Display labels aligned with previewMeshUuids.
    std::string         previewSig;                   ///< Signature of the graph's clip set (cache key).
    int                 previewMeshChoice = 0;        ///< Index into previewMeshUuids.
    float               previewRotX      = 20.0f;     ///< Orbit pitch, degrees.
    float               previewRotY      = 30.0f;     ///< Orbit yaw, degrees.
    kVec3               previewCenter    = kVec3(0.0f); ///< Orbit pivot (mesh bounds centre).
    float               previewCamDist   = 3.0f;      ///< Orbit distance from the pivot.
    float               previewBlendTime = 0.0f;      ///< Seconds advanced through the blended clips.
    float               previewSize      = 220.0f;    ///< Preview image side length (px).
    bool                previewDragging  = false;     ///< True while dragging the preview to orbit.
    bool                previewLightOn   = true;      ///< Toggle lit vs. flat preview.

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
    static constexpr float ANY_STATE_WIDTH  = 160.f; ///< Any State node width (canvas units).
    static constexpr float ANY_STATE_BODY_H = 60.f;  ///< Any State node body height.
    static constexpr float BLEND_NODE_WIDTH = 220.f; ///< Blend tree node width.
    static constexpr float BLEND_BODY_H_1D  = 96.f;  ///< Blend tree body height in 1D mode.
    static constexpr float BLEND_BODY_H_2D  = 200.f; ///< Blend tree body height in 2D mode.
    static constexpr float BLEND_PAD        = 8.f;   ///< Inner padding of the blend diagram.

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------
    void drawToolbar();
    void drawCanvas();
    void drawNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawAnchorNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawCommentNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawAnyStateNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawBlendTreeNode(ImDrawList* dl, AnimState& state, ImVec2 origin);
    void drawBlendTreeInspector(AnimState* state);

    // Embedded blend-tree preview helpers.
    void ensurePreviewScene();
    void releasePreviewMesh();
    void refreshPreviewMeshes();
    void ensurePreviewClips();
    void framePreviewCamera();
    void drawBlendPreview(AnimState* state);
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

    /** @brief Canvas-space width of a node (varies by node kind). */
    float nodeWidth(const AnimState& state) const;
    /** @brief Canvas-space height of a node, including its header. */
    float nodeHeight(const AnimState& state) const;
    /** @brief Screen-space top-left / bottom-right corners of a node bounding box. */
    void  nodeScreenRect(const AnimState& state, ImVec2 origin, ImVec2& tl, ImVec2& br) const;
    /** @brief Screen-space rectangle of a blend tree's inner diagram area. */
    void  blendDiagramRect(const AnimState& state, ImVec2 origin, ImVec2& tl, ImVec2& br) const;

    /** @brief Find the (single) Any State node, or nullptr. */
    AnimState* findAnyState();
    /** @brief Find the (single) Default State node, or nullptr. */
    AnimState* findDefaultState();
    /** @brief Guarantee a Default State and an Any State node exist; true if one was added. */
    bool ensureSpecialNodes();

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
