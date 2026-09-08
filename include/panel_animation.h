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
// Keyframe & Track data structures
// ===========================================================================

/**
 * @brief Supported easing types for keyframe interpolation.
 */
enum class AnimEasing
{
    Linear,
    Step,
    EaseIn,
    EaseOut,
    EaseInOut,
    CubicIn,
    CubicOut,
    CubicInOut,
    SineIn,
    SineOut,
    SineInOut
};

/**
 * @brief A single keyframe on a property track.
 */
struct AnimKeyframe
{
    float   time  = 0.0f;  ///< Time in seconds on the timeline.
    kVec3   value = {0,0,0}; ///< Value (position/rotation/scale).
    AnimEasing easing = AnimEasing::Linear;

    // Tangent handles for cubic bezier interpolation (in value-space)
    kVec3 leftTangent  = {0,0,0};
    kVec3 rightTangent = {0,0,0};
};

/**
 * @brief Type of property being animated on a track.
 */
enum class AnimTrackProperty
{
    Position,
    Rotation,
    Scale,
    Event   ///< Special track for script-trigger events.
};

/**
 * @brief A single event keyframe that calls a script function.
 */
struct AnimEventKeyframe
{
    float       time     = 0.0f;
    std::string functionName; ///< Name of the script function to call.
    std::string params;       ///< Optional JSON parameter payload.
};

/**
 * @brief A property track belonging to one scene object.
 */
struct AnimTrack
{
    std::string          objectUuid;       ///< Scene object UUID (or "self").
    std::string          objectName;       ///< Display name.
    AnimTrackProperty    property   = AnimTrackProperty::Position;
    std::vector<AnimKeyframe> keyframes;   ///< Sorted by time.
};

// ===========================================================================
// Top-level animation document
// ===========================================================================

/**
 * @brief Full .cinematic document loaded into the editor.
 */
struct AnimationDoc
{
    std::string uuid;
    std::string name;
    std::string type = "scene";  ///< Cinematic documents are always scene animations.

    float       duration = 5.0f;  ///< Total animation length in seconds.
    float       fps      = 30.0f; ///< Timeline frames-per-second display.
    std::vector<AnimTrack> tracks; ///< Per-object per-property tracks.
    std::vector<AnimEventKeyframe> events; ///< Global event keyframes.

    bool dirty = false;

    /** @brief Serialize to JSON. */
    nlohmann::json toJson() const;

    /** @brief Deserialize from JSON. */
    void fromJson(const nlohmann::json& j);

    /** @brief Add a keyframe to a track (creates track if needed). */
    void addKeyframe(const std::string& objUuid, const std::string& objName,
                     AnimTrackProperty prop, float time, kVec3 value);

    /** @brief Remove a keyframe from a track at the given time. */
    void removeKeyframe(const std::string& objUuid, AnimTrackProperty prop, float time);

    /** @brief Get a track by object UUID and property, or nullptr. */
    AnimTrack* findTrack(const std::string& objUuid, AnimTrackProperty prop);

    /** @brief Get interpolated value at a given time (for preview). */
    kVec3 evaluate(const std::string& objUuid, AnimTrackProperty prop, float time) const;

    /** @brief Get sorted unique object UUIDs used in tracks. */
    std::vector<std::string> getTrackObjects() const;

    /** @brief Sort all keyframes by time. */
    void sortKeyframes();
};

// ===========================================================================
// Cinematic Editor Panel
// ===========================================================================

/**
 * @brief Cinematic editing panel with Timeline and Graph (curve) tabs.
 *
 * Works with .cinematic files — editable scene-object animation keyframe tracks
 * for position/rotation/scale plus script events.
 */
class PanelAnimation
{
public:
    bool focused = false;

    PanelAnimation(kGuiManager* setGui, Manager* setManager);

    void draw(bool& isOpened);

    /** @brief Open a .cinematic file into the editor. */
    void openFile(const std::string& path);

    void saveCurrent() { saveDoc(); }

    /** @brief Returns the currently open .cinematic file path ("" when unsaved). */
    std::string getFilePath() const { return filePath; }

private:
    kGuiManager* gui     = nullptr;
    Manager*     manager = nullptr;

    AnimationDoc doc;
    std::string  filePath;

    // Active tab
    int activeTab = 0; ///< 0 = Timeline, 1 = Graph Editor

    // -----------------------------------------------------------------------
    // Timeline state
    // -----------------------------------------------------------------------
    float timelineZoom   = 1.0f;  ///< Horizontal zoom.
    float timelineScroll = 0.0f;  ///< Horizontal scroll offset in seconds.
    float currentTime    = 0.0f;  ///< Current playback cursor position.
    bool  isPlaying      = false;
    bool  isDraggingTime = false;
    bool  loopPlayback   = true;

    // Selected keyframe for editing
    int    selectedTrackIdx   = -1;
    int    selectedKeyframeIdx = -1;

    // Track visibility
    bool showPositionTracks = true;
    bool showRotationTracks = true;
    bool showScaleTracks    = true;
    bool showEventTracks    = true;

    // -----------------------------------------------------------------------
    // Graph (curve) editor state
    // -----------------------------------------------------------------------
    float graphZoomX    = 1.0f;
    float graphZoomY    = 1.0f;
    float graphOffsetX  = 0.0f;
    float graphOffsetY  = 0.0f;

    int    graphSelectedTrack   = -1;
    int    graphSelectedKey     = -1;
    bool   isDraggingGraphKey   = false;
    bool   isPanningGraph       = false;

    // -----------------------------------------------------------------------
    // UI constants
    // -----------------------------------------------------------------------
    static constexpr float TIMELINE_HEADER_H = 24.f;  ///< Time ruler height.
    static constexpr float TRACK_ROW_H       = 28.f;  ///< Height per track row.
    static constexpr float TRACK_LABEL_W     = 160.f; ///< Width of the track label column.
    static constexpr float KEYFRAME_RADIUS   = 4.f;   ///< Keyframe dot radius.
    static constexpr float KEYFRAME_HIT_R    = 7.f;   ///< Keyframe hit-test radius.

    // -----------------------------------------------------------------------
    // Methods
    // -----------------------------------------------------------------------
    void drawToolbar();
    void drawTimelineTab();
    void drawGraphEditorTab();

    // Timeline sub-drawing
    void drawTimeRuler(ImDrawList* dl, ImVec2 origin, float width);
    void drawTrackRow(ImDrawList* dl, int trackIdx, ImVec2 rowOrigin, float width);
    void drawKeyframeDiamond(ImDrawList* dl, ImVec2 center, bool selected, ImU32 color);
    void drawPlaybackCursor(ImDrawList* dl, float cursorX, float topY, float height);

    // Graph editor sub-drawing
    void drawGraphGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawGraphCurve(ImDrawList* dl, const AnimTrack& track, ImVec2 origin, ImVec2 size, int channel);

    // Helpers
    float timeToScreen(float time, float originX, float width) const;
    float screenToTime(float screenX, float originX, float width) const;
    float valueToGraphY(float val, float originY, float height) const;
    float graphYToValue(float screenY, float originY, float height) const;

    void newDoc();
    void saveDoc();
    void saveDocAs();
    void loadDoc(const std::string& path);

    /** @brief Add a scene object track by picking from the scene or entering UUID. */
    void promptAddObjectTrack();

    /** @brief Add a keyframe at the current time on the selected track. */
    void addKeyframeAtCursor();

    /** @brief Delete the selected keyframe. */
    void deleteSelectedKeyframe();

    /** @brief Scan the scene for objects to offer in the add-track dialog. */
    std::vector<kObject*> getSceneObjects() const;

    static std::string generateUuid();
    static void SDLCALL saveAnimCallback(void* userdata, const char* const* filelist, int filter);
};
