#include "panel_animation.h"
#include "panel_game.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ===========================================================================
// Helpers
// ===========================================================================

static ImU32 toImU32(ImVec4 c)
{
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255));
}

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return { a.x - b.x, a.y - b.y }; }
static ImVec2 operator*(ImVec2 a, float s)  { return { a.x * s,   a.y * s   }; }

static const char* easingName(AnimEasing e)
{
    switch (e)
    {
        case AnimEasing::Linear:    return "Linear";
        case AnimEasing::Step:      return "Step";
        case AnimEasing::EaseIn:    return "Ease In";
        case AnimEasing::EaseOut:   return "Ease Out";
        case AnimEasing::EaseInOut: return "Ease In/Out";
        case AnimEasing::CubicIn:   return "Cubic In";
        case AnimEasing::CubicOut:  return "Cubic Out";
        case AnimEasing::CubicInOut:return "Cubic In/Out";
        case AnimEasing::SineIn:    return "Sine In";
        case AnimEasing::SineOut:   return "Sine Out";
        case AnimEasing::SineInOut: return "Sine In/Out";
    }
    return "Linear";
}

static AnimEasing easingFromName(const std::string& n)
{
    if (n == "Step")       return AnimEasing::Step;
    if (n == "Ease In")    return AnimEasing::EaseIn;
    if (n == "Ease Out")   return AnimEasing::EaseOut;
    if (n == "Ease In/Out")return AnimEasing::EaseInOut;
    if (n == "Cubic In")   return AnimEasing::CubicIn;
    if (n == "Cubic Out")  return AnimEasing::CubicOut;
    if (n == "Cubic In/Out")return AnimEasing::CubicInOut;
    if (n == "Sine In")    return AnimEasing::SineIn;
    if (n == "Sine Out")   return AnimEasing::SineOut;
    if (n == "Sine In/Out")return AnimEasing::SineInOut;
    return AnimEasing::Linear;
}

static const char* trackPropName(AnimTrackProperty p)
{
    switch (p)
    {
        case AnimTrackProperty::Position: return "Position";
        case AnimTrackProperty::Rotation: return "Rotation";
        case AnimTrackProperty::Scale:    return "Scale";
        case AnimTrackProperty::Event:    return "Event";
    }
    return "?";
}

static const char* trackPropShortName(AnimTrackProperty p)
{
    switch (p)
    {
        case AnimTrackProperty::Position: return "Pos";
        case AnimTrackProperty::Rotation: return "Rot";
        case AnimTrackProperty::Scale:    return "Scl";
        case AnimTrackProperty::Event:    return "Evt";
    }
    return "?";
}

static ImU32 trackColor(AnimTrackProperty p)
{
    switch (p)
    {
        case AnimTrackProperty::Position: return IM_COL32(100, 180, 255, 220);
        case AnimTrackProperty::Rotation: return IM_COL32(100, 255, 140, 220);
        case AnimTrackProperty::Scale:    return IM_COL32(255, 200, 80,  220);
        case AnimTrackProperty::Event:    return IM_COL32(255, 100, 100, 220);
    }
    return IM_COL32(200, 200, 200, 220);
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static kVec3 lerp(kVec3 a, kVec3 b, float t) { return a + (b - a) * t; }

static float applyEasing(float t, AnimEasing e)
{
    switch (e)
    {
        case AnimEasing::Linear:    return t;
        case AnimEasing::Step:      return t < 1.0f ? 0.0f : 1.0f;
        case AnimEasing::EaseIn:    return t * t;
        case AnimEasing::EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
        case AnimEasing::EaseInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - (float)pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        case AnimEasing::CubicIn:   return t * t * t;
        case AnimEasing::CubicOut:  return 1.0f - (float)pow(1.0f - t, 3.0f);
        case AnimEasing::CubicInOut:return t < 0.5f ? 4.0f * t * t * t : 1.0f - (float)pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
        case AnimEasing::SineIn:    return 1.0f - (float)cos(t * 3.14159f * 0.5f);
        case AnimEasing::SineOut:   return (float)sin(t * 3.14159f * 0.5f);
        case AnimEasing::SineInOut: return (float)(-(cos(3.14159f * t) - 1.0f) * 0.5f);
    }
    return t;
}

// ===========================================================================
// AnimationDoc implementation
// ===========================================================================

nlohmann::json AnimationDoc::toJson() const
{
    json j;
    j["uuid"] = uuid;
    j["name"] = name;
    j["type"] = type;
    j["duration"] = duration;
    j["fps"]      = fps;

    json tracksArr = json::array();
    for (const auto& t : tracks)
    {
        json tj;
        tj["objectUuid"] = t.objectUuid;
        tj["objectName"] = t.objectName;
        tj["property"]   = trackPropName(t.property);

        json kfs = json::array();
        for (const auto& k : t.keyframes)
        {
            json kj;
            kj["time"]  = k.time;
            kj["value"] = { k.value.x, k.value.y, k.value.z };
            kj["easing"] = easingName(k.easing);
            kj["leftTangent"]  = { k.leftTangent.x, k.leftTangent.y, k.leftTangent.z };
            kj["rightTangent"] = { k.rightTangent.x, k.rightTangent.y, k.rightTangent.z };
            kfs.push_back(kj);
        }
        tj["keyframes"] = kfs;
        tracksArr.push_back(tj);
    }
    j["tracks"] = tracksArr;

    json evts = json::array();
    for (const auto& e : events)
    {
        json ej;
        ej["time"]         = e.time;
        ej["functionName"] = e.functionName;
        ej["params"]       = e.params;
        evts.push_back(ej);
    }
    j["events"] = evts;

    return j;
}

void AnimationDoc::fromJson(const nlohmann::json& j)
{
    uuid = j.value("uuid", std::string());
    name = j.value("name", std::string("NewCinematic"));
    type = "scene";

    duration = j.value("duration", 5.0f);
    fps      = j.value("fps", 30.0f);

    tracks.clear();
    if (j.contains("tracks"))
    {
        for (const auto& tj : j["tracks"])
        {
            AnimTrack t;
            t.objectUuid = tj.value("objectUuid", std::string());
            t.objectName = tj.value("objectName", std::string());
            std::string propStr = tj.value("property", std::string("Position"));
            if (propStr == "Rotation")      t.property = AnimTrackProperty::Rotation;
            else if (propStr == "Scale")    t.property = AnimTrackProperty::Scale;
            else if (propStr == "Event")    t.property = AnimTrackProperty::Event;
            else                            t.property = AnimTrackProperty::Position;

            if (tj.contains("keyframes"))
            {
                for (const auto& kj : tj["keyframes"])
                {
                    AnimKeyframe kf;
                    kf.time  = kj.value("time", 0.0f);
                    kf.easing = easingFromName(kj.value("easing", std::string("Linear")));
                    if (kj.contains("value") && kj["value"].is_array() && kj["value"].size() >= 3)
                        kf.value = { kj["value"][0].get<float>(), kj["value"][1].get<float>(), kj["value"][2].get<float>() };
                    if (kj.contains("leftTangent") && kj["leftTangent"].is_array() && kj["leftTangent"].size() >= 3)
                        kf.leftTangent = { kj["leftTangent"][0].get<float>(), kj["leftTangent"][1].get<float>(), kj["leftTangent"][2].get<float>() };
                    if (kj.contains("rightTangent") && kj["rightTangent"].is_array() && kj["rightTangent"].size() >= 3)
                        kf.rightTangent = { kj["rightTangent"][0].get<float>(), kj["rightTangent"][1].get<float>(), kj["rightTangent"][2].get<float>() };
                    t.keyframes.push_back(kf);
                }
            }
            tracks.push_back(t);
        }
    }

    events.clear();
    if (j.contains("events"))
    {
        for (const auto& ej : j["events"])
        {
            AnimEventKeyframe ev;
            ev.time         = ej.value("time", 0.0f);
            ev.functionName = ej.value("functionName", std::string());
            ev.params       = ej.value("params", std::string());
            events.push_back(ev);
        }
    }

    dirty = false;
}

void AnimationDoc::addKeyframe(const std::string& objUuid, const std::string& objName,
                                AnimTrackProperty prop, float time, kVec3 value)
{
    AnimTrack* track = findTrack(objUuid, prop);
    if (!track)
    {
        AnimTrack t;
        t.objectUuid = objUuid;
        t.objectName = objName;
        t.property   = prop;
        tracks.push_back(t);
        track = &tracks.back();
    }

    for (auto& kf : track->keyframes)
    {
        if (std::abs(kf.time - time) < 0.001f)
        {
            kf.value = value;
            dirty = true;
            return;
        }
    }

    AnimKeyframe kf;
    kf.time  = time;
    kf.value = value;
    track->keyframes.push_back(kf);
    sortKeyframes();
    dirty = true;
}

void AnimationDoc::removeKeyframe(const std::string& objUuid, AnimTrackProperty prop, float time)
{
    AnimTrack* track = findTrack(objUuid, prop);
    if (!track) return;

    track->keyframes.erase(
        std::remove_if(track->keyframes.begin(), track->keyframes.end(),
            [time](const AnimKeyframe& k) { return std::abs(k.time - time) < 0.001f; }),
        track->keyframes.end());
    dirty = true;
}

AnimTrack* AnimationDoc::findTrack(const std::string& objUuid, AnimTrackProperty prop)
{
    for (auto& t : tracks)
        if (t.objectUuid == objUuid && t.property == prop) return &t;
    return nullptr;
}

kVec3 AnimationDoc::evaluate(const std::string& objUuid, AnimTrackProperty prop, float time) const
{
    for (const auto& t : tracks)
    {
        if (t.objectUuid != objUuid || t.property != prop) continue;
        if (t.keyframes.empty()) return kVec3(0);

        if (time <= t.keyframes.front().time) return t.keyframes.front().value;
        if (time >= t.keyframes.back().time)  return t.keyframes.back().value;

        for (size_t i = 0; i < t.keyframes.size() - 1; ++i)
        {
            const auto& a = t.keyframes[i];
            const auto& b = t.keyframes[i + 1];
            if (time >= a.time && time <= b.time)
            {
                float dt = (b.time - a.time);
                if (dt < 0.0001f) return a.value;
                float tNorm = applyEasing((time - a.time) / dt, a.easing);
                return lerp(a.value, b.value, tNorm);
            }
        }
    }
    return kVec3(0);
}

std::vector<std::string> AnimationDoc::getTrackObjects() const
{
    std::vector<std::string> result;
    for (const auto& t : tracks)
    {
        if (std::find(result.begin(), result.end(), t.objectUuid) == result.end())
            result.push_back(t.objectUuid);
    }
    return result;
}

void AnimationDoc::sortKeyframes()
{
    for (auto& t : tracks)
    {
        std::sort(t.keyframes.begin(), t.keyframes.end(),
            [](const AnimKeyframe& a, const AnimKeyframe& b) { return a.time < b.time; });
    }
}

// ===========================================================================
// Construction
// ===========================================================================

PanelAnimation::PanelAnimation(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newDoc();
}

// ===========================================================================
// Isolated cinematic editing ("Edit Cinematic")
// ===========================================================================

void PanelAnimation::setCinematicEditing(bool enabled)
{
    // Idempotent: re-enabling while already editing must NOT overwrite the
    // snapshot taken when this edit session began.
    if (enabled == cinematicEditing)
        return;

    if (enabled)
    {
        // Game playback and isolated cinematic editing are mutually exclusive:
        // refuse to enter edit mode while the game is running.
        if (manager && manager->panelGame &&
            manager->panelGame->getPlayState() != GamePlayState::Stopped)
            return;

        // false->true: capture the real scene exactly once, before any isolated
        // cinematic edit happens, so every change can be reverted on exit.
        projectSavedBeforeEdit = manager ? manager->projectSaved : true;
        SceneStateSnapshot::capture(manager, sceneSnapshot);
        cinematicEditing = true;
    }
    else
    {
        // true->false: stop any cinematic preview playback, restore the saved
        // scene (discarding isolated edits) and roll the dirty flag back — the
        // same revert the Game panel performs when playback stops.
        isPlaying = false;
        SceneStateSnapshot::restore(manager, sceneSnapshot); // clears the snapshot
        if (manager)
        {
            manager->projectSaved = projectSavedBeforeEdit;
            manager->refreshWindowTitle();
        }
        cinematicEditing = false;
    }
}

std::string PanelAnimation::generateUuid()
{
    using namespace std::chrono;
    auto seed = (uint64_t)duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;
    auto r1 = dist(rng), r2 = dist(rng);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)r1, (unsigned long long)r2);
    return std::string(buf);
}

void PanelAnimation::newDoc()
{
    doc       = AnimationDoc{};
    doc.uuid  = generateUuid();
    doc.name  = "NewCinematic";
    doc.type  = "scene";
    doc.dirty = false;
    filePath.clear();
    currentTime      = 0.0f;
    isPlaying        = false;
    selectedTrackIdx = -1;
    selectedKeyframeIdx = -1;
    activeTab        = 0;
}

// ===========================================================================
// File I/O
// ===========================================================================

void PanelAnimation::openFile(const std::string& path)
{
    loadDoc(path);
}

void PanelAnimation::loadDoc(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        doc.fromJson(j);
        filePath = path;
        doc.dirty = false;
        currentTime      = 0.0f;
        isPlaying        = false;
        selectedTrackIdx = -1;
        selectedKeyframeIdx = -1;
    }
    catch (...) {}
}

void PanelAnimation::saveDoc()
{
    if (filePath.empty()) { saveDocAs(); return; }

    json j = doc.toJson();
    std::ofstream f(filePath);
    if (!f.is_open()) return;
    f << j.dump(4);
    doc.dirty = false;
}

void SDLCALL PanelAnimation::saveAnimCallback(void* userdata,
                                               const char* const* filelist,
                                               int /*filter*/)
{
    if (!filelist || !*filelist) return;
    PanelAnimation* self = static_cast<PanelAnimation*>(userdata);

    std::string path = filelist[0];
    if (path.size() < 10 || path.substr(path.size() - 10) != ".cinematic")
        path += ".cinematic";

    self->filePath = path;
    self->saveDoc();
}

void PanelAnimation::saveDocAs()
{
    if (!manager->projectOpened) return;

    fs::path assetsDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
    fs::create_directories(assetsDir);

    std::string defaultName = (doc.name.empty() ? "NewCinematic" : doc.name) + ".cinematic";

    SDL_DialogFileFilter filters[] = {
        { "Cinematic files", "cinematic" },
        { "All files",       "*"         }
    };

    SDL_ShowSaveFileDialog(
        saveAnimCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (assetsDir / defaultName).string().c_str()
    );
}

// ===========================================================================
// Scene object helpers
// ===========================================================================

std::vector<kObject*> PanelAnimation::getSceneObjects() const
{
    std::vector<kObject*> result;
    kScene* scene = manager->getScene();
    if (!scene) return result;

    kObject* root = scene->getRootNode();
    if (!root) return result;

    std::function<void(kObject*)> collect = [&](kObject* obj) {
        result.push_back(obj);
        auto children = obj->getChildren();
        for (auto* child : children)
            collect(child);
    };

    auto rootChildren = root->getChildren();
    for (auto* child : rootChildren)
        collect(child);

    return result;
}

void PanelAnimation::promptAddObjectTrack()
{
    if (doc.type != "scene") return;

    auto objects = getSceneObjects();
    if (objects.empty())
    {
        ImGui::OpenPopup("##NoSceneObjects");
        return;
    }

    ImGui::OpenPopup("##AddObjectTrack");
}

// ===========================================================================
// Main draw
// ===========================================================================

void PanelAnimation::draw(bool& isOpened)
{
    if (!isOpened)
    {
        // Closing/hiding the panel ends any isolated edit session so its scene
        // snapshot is restored instead of leaking edits into the real scene.
        if (cinematicEditing)
            setCinematicEditing(false);
        wasOpened = false;
        return;
    }

    // Default to a wide, short strip placed along the bottom of the viewport,
    // matching the timeline layout of Unity / Unreal. ImGuiCond_FirstUseEver
    // preserves any saved docking/position, so the explicit position/size only
    // apply the first time the panel opens without a docked layout (e.g. in a
    // project whose workspace predates this panel). The default layout itself
    // (res/layout.ini) docks this window into the bottom strip.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({ vp->Size.x * 0.9f, 320.0f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        { vp->Pos.x + vp->Size.x * 0.05f,
          vp->Pos.y + vp->Size.y - 360.0f },
        ImGuiCond_FirstUseEver);

    // The panel shares the bottom dock node with the Console. Focusing on the
    // first frame it is (re)opened brings its dock tab to the front instead of
    // leaving the user looking at the Console tab that shares the node.
    if (!wasOpened)
        ImGui::SetNextWindowFocus();

    kString title = doc.dirty ? "Cinematic *" : "Cinematic";
    title += "###CinematicEditor";

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(title.c_str(), &isOpened, wflags))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    wasOpened = true;

    drawToolbar();
    ImGui::Separator();

    // Tab bar
    if (ImGui::BeginTabBar("##AnimEditTabs"))
    {
        if (ImGui::BeginTabItem("Dopesheet Editor"))
        {
            activeTab = 0;
            drawTimelineTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graph Editor"))
        {
            activeTab = 1;
            drawGraphEditorTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Popups
    if (ImGui::BeginPopup("##AddObjectTrack"))
    {
        ImGui::Text("Select Object:");
        ImGui::Separator();

        auto objects = getSceneObjects();
        for (auto* obj : objects)
        {
            kString objName = obj->getName().empty() ? obj->getUuid() : obj->getName();
            if (ImGui::Selectable(objName.c_str()))
            {
                doc.addKeyframe(obj->getUuid(), objName, AnimTrackProperty::Position, 0.0f, obj->getPosition());
                kQuat rot = obj->getRotation();
                doc.addKeyframe(obj->getUuid(), objName, AnimTrackProperty::Rotation, 0.0f, kVec3(rot.x, rot.y, rot.z));
                doc.addKeyframe(obj->getUuid(), objName, AnimTrackProperty::Scale, 0.0f, obj->getScale());
                doc.dirty = true;
            }
        }

        ImGui::Separator();
        if (ImGui::Selectable("[Add custom UUID...]"))
        {
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##AddEventKeyframe"))
    {
        static char funcBuf[128] = "";
        static char paramsBuf[256] = "";

        ImGui::Text("Add Event Keyframe at %.2fs", currentTime);
        ImGui::Separator();
        ImGui::InputText("Function", funcBuf, sizeof(funcBuf));
        ImGui::InputText("Params (JSON)", paramsBuf, sizeof(paramsBuf));

        if (ImGui::Button("Add"))
        {
            AnimEventKeyframe ev;
            ev.time         = currentTime;
            ev.functionName = funcBuf;
            ev.params       = paramsBuf;
            doc.events.push_back(ev);
            doc.dirty = true;

            std::sort(doc.events.begin(), doc.events.end(),
                [](const AnimEventKeyframe& a, const AnimEventKeyframe& b) { return a.time < b.time; });

            funcBuf[0] = 0;
            paramsBuf[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            funcBuf[0] = 0;
            paramsBuf[0] = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

// ===========================================================================
// Toolbar
// ===========================================================================

void PanelAnimation::drawToolbar()
{
    bool hasProject = manager->projectOpened;

    ImGui::BeginDisabled(!hasProject);
    if (ImGui::Button("New"))
    {
        newDoc();
    }
    ImGui::SameLine();

    if (ImGui::Button("Open") && hasProject)
    {
        fs::path animDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
        if (!fs::exists(animDir))
            fs::create_directories(animDir);

        SDL_DialogFileFilter filters[] = {
            { "Cinematic files", "cinematic" },
            { "All files",       "*"         }
        };

        SDL_ShowOpenFileDialog(
            [](void* userdata, const char* const* filelist, int) {
                if (!filelist || !*filelist) return;
                auto* self = static_cast<PanelAnimation*>(userdata);
                self->openFile(filelist[0]);
            },
            this,
            manager->getWindow()->getSdlWindow(),
            filters,
            SDL_arraysize(filters),
            animDir.string().c_str(),
            false
        );
    }
    ImGui::SameLine();

    if (ImGui::Button("Save") && hasProject)
        saveDoc();
    ImGui::SameLine();

    if (ImGui::Button("Save As...") && hasProject)
        saveDocAs();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Isolated edit mode. While enabled the cinematic preview is sandboxed:
    // enabling captured the scene, so disabling restores it. Disabled while the
    // Game panel is running (game playback forces this mode off).
    bool gameRunning = manager->panelGame &&
                       manager->panelGame->getPlayState() != GamePlayState::Stopped;
    ImGui::BeginDisabled(gameRunning);
    bool editing = cinematicEditing;
    if (ImGui::Checkbox("Edit Cinematic", &editing))
        setCinematicEditing(editing);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    bool hasDoc = !filePath.empty() || doc.dirty;
    if (!hasDoc)
    {
        ImGui::TextDisabled("No cinematic loaded");
    }
    else
    {
        char nameBuf[128];
        strncpy_s(nameBuf, doc.name.c_str(), sizeof(nameBuf));
        ImGui::SetNextItemWidth(140.f);
        if (ImGui::InputText("##AnimName", nameBuf, sizeof(nameBuf)))
        {
            doc.name = nameBuf;
            doc.dirty = true;
        }
        ImGui::SameLine();

        ImGui::TextColored({0.3f, 1.0f, 0.5f, 1.0f}, "Cinematic");
    }

    ImGui::EndDisabled();
}

// ===========================================================================
// Dopesheet Tab
// ===========================================================================

float PanelAnimation::timeToScreen(float time, float originX, float width) const
{
    float pixelsPerSec = 100.0f * timelineZoom;
    return originX + (time - timelineScroll) * pixelsPerSec;
}

float PanelAnimation::screenToTime(float screenX, float originX, float width) const
{
    float pixelsPerSec = 100.0f * timelineZoom;
    return (screenX - originX) / pixelsPerSec + timelineScroll;
}

void PanelAnimation::drawTimelineTab()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 10 || avail.y < 10) return;

    float headerH = TIMELINE_HEADER_H;
    float labelW  = TRACK_LABEL_W;
    float contentW = avail.x - labelW;
    float contentH = avail.y - headerH;

    bool hasContent = !filePath.empty() || doc.dirty;
    if (!hasContent)
    {
        ImVec2 textSize = ImGui::CalcTextSize("Open or create a .cinematic file to begin editing");
        ImGui::SetCursorPos({ (avail.x - textSize.x) * 0.5f, (avail.y - textSize.y) * 0.5f });
        ImGui::TextDisabled("Open or create a .cinematic file to begin editing");
        return;
    }

    // Cinematic timeline
    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Duration", &doc.duration, 0.1f, 0.5f, 120.0f, "%.1fs");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.f);
    ImGui::DragFloat("FPS", &doc.fps, 1.0f, 1.0f, 120.0f, "%.0f");
    ImGui::SameLine();

    // Playback only runs inside isolated edit mode so a cinematic preview can
    // never leak into the real scene. Pressing Play first ensures the mode is on
    // (capturing the scene snapshot on first entry) and then starts playback.
    ImGui::BeginDisabled(!cinematicEditing);
    if (ImGui::Button(isPlaying ? "|| Pause" : "> Play"))
    {
        if (!cinematicEditing)
            setCinematicEditing(true);
        isPlaying = !isPlaying;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        isPlaying = false;
        currentTime = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loopPlayback);
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "Time: %.2fs", currentTime);
    ImGui::TextUnformatted(timeBuf);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::Checkbox("Pos", &showPositionTracks); ImGui::SameLine();
    ImGui::Checkbox("Rot", &showRotationTracks); ImGui::SameLine();
    ImGui::Checkbox("Scl", &showScaleTracks);    ImGui::SameLine();
    ImGui::Checkbox("Evt", &showEventTracks);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("+ Add Object Track"))
        promptAddObjectTrack();
    ImGui::SameLine();
    if (ImGui::Button("+ Add Event"))
        ImGui::OpenPopup("##AddEventKeyframe");
    ImGui::SameLine();
    if (ImGui::Button("+ Key"))
        addKeyframeAtCursor();

    ImGui::Separator();

    // Timeline canvas area
    ImVec2 canvasTL = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = { contentW, contentH };
    if (canvasSize.x < 10 || canvasSize.y < 10) return;

    std::vector<int> visibleTrackIndices;
    for (int i = 0; i < (int)doc.tracks.size(); ++i)
    {
        const auto& t = doc.tracks[i];
        if (t.property == AnimTrackProperty::Position && !showPositionTracks) continue;
        if (t.property == AnimTrackProperty::Rotation && !showRotationTracks) continue;
        if (t.property == AnimTrackProperty::Scale && !showScaleTracks) continue;
        if (t.property == AnimTrackProperty::Event && !showEventTracks) continue;
        visibleTrackIndices.push_back(i);
    }

    if (showEventTracks && !doc.events.empty())
    {
        if (std::find(visibleTrackIndices.begin(), visibleTrackIndices.end(), -1) == visibleTrackIndices.end())
            visibleTrackIndices.push_back(-1);
    }

    float totalTrackH = visibleTrackIndices.size() * TRACK_ROW_H;
    float totalContentH = totalTrackH > (canvasSize.y - headerH) ? totalTrackH : (canvasSize.y - headerH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasTL, canvasTL + canvasSize, true);

    dl->AddRectFilled(canvasTL, canvasTL + canvasSize, IM_COL32(28, 28, 28, 255));

    // Time ruler
    {
        ImVec2 rulerTL = canvasTL;
        ImVec2 rulerBR = { canvasTL.x + canvasSize.x, canvasTL.y + headerH };
        dl->AddRectFilled(rulerTL, rulerBR, IM_COL32(40, 40, 40, 255));

        float pixelsPerSec = 100.0f * timelineZoom;
        float startTime = (float)floor(timelineScroll / 0.5) * 0.5f;
        float endTime = timelineScroll + canvasSize.x / pixelsPerSec + 0.5f;

        for (float t = startTime; t <= endTime; t += 0.5f)
        {
            float x = timeToScreen(t, rulerTL.x, canvasSize.x);
            if (x < rulerTL.x || x > rulerBR.x) continue;
            bool isSec = (std::abs(t - (float)round(t)) < 0.01f);

            if (isSec)
            {
                dl->AddLine({ x, rulerTL.y }, { x, rulerBR.y }, IM_COL32(80, 80, 80, 255));
                char label[32];
                snprintf(label, sizeof(label), "%.0fs", t);
                ImVec2 textSize = ImGui::CalcTextSize(label);
                dl->AddText({ x - textSize.x * 0.5f, rulerTL.y + 2 }, IM_COL32(180, 180, 180, 255), label);
            }
            else
            {
                dl->AddLine({ x, rulerBR.y - 6 }, { x, rulerBR.y }, IM_COL32(60, 60, 60, 255));
            }
        }

        float fps = doc.fps > 1.0f ? doc.fps : 30.0f;
        float frameStep = 1.0f / fps;
        for (float t = startTime; t <= endTime; t += frameStep)
        {
            float x = timeToScreen(t, rulerTL.x, canvasSize.x);
            if (x < rulerTL.x || x > rulerBR.x) continue;
            dl->AddLine({ x, rulerBR.y - 3 }, { x, rulerBR.y }, IM_COL32(50, 50, 50, 255));
        }
    }

    // Track rows
    float rowY = canvasTL.y + headerH;
    for (size_t vi = 0; vi < visibleTrackIndices.size(); ++vi)
    {
        int ti = visibleTrackIndices[vi];
        ImVec2 rowTL = { canvasTL.x, rowY };
        ImVec2 rowBR = { canvasTL.x + canvasSize.x, rowY + TRACK_ROW_H };

        if (vi % 2 == 0)
            dl->AddRectFilled(rowTL, rowBR, IM_COL32(35, 35, 35, 255));

        if (ti >= 0)
        {
            const auto& track = doc.tracks[ti];
            std::string label = track.objectName + " " + trackPropShortName(track.property);
            dl->AddText({ rowTL.x + 4, rowTL.y + 4 }, IM_COL32(200, 200, 200, 255), label.c_str());

            ImU32 col = trackColor(track.property);
            for (int ki = 0; ki < (int)track.keyframes.size(); ++ki)
            {
                const auto& kf = track.keyframes[ki];
                float kx = timeToScreen(kf.time, rowTL.x, canvasSize.x);
                float ky = rowTL.y + TRACK_ROW_H * 0.5f;

                bool isSelected = (ti == selectedTrackIdx && ki == selectedKeyframeIdx);
                drawKeyframeDiamond(dl, { kx, ky }, isSelected, col);

                if (ki > 0)
                {
                    float prevX = timeToScreen(track.keyframes[ki - 1].time, rowTL.x, canvasSize.x);
                    dl->AddLine({ prevX, ky }, { kx, ky }, IM_COL32(100, 100, 100, 150), 1.0f);
                }
            }
        }
        else
        {
            dl->AddText({ rowTL.x + 4, rowTL.y + 4 }, IM_COL32(255, 150, 150, 255), "Events");

            for (size_t ei = 0; ei < doc.events.size(); ++ei)
            {
                const auto& ev = doc.events[ei];
                float ex = timeToScreen(ev.time, rowTL.x, canvasSize.x);
                float ey = rowTL.y + TRACK_ROW_H * 0.5f;

                dl->AddTriangleFilled({ ex, ey - 5 }, { ex - 5, ey + 5 }, { ex + 5, ey + 5 }, IM_COL32(255, 80, 80, 220));
                dl->AddText({ ex + 7, ey - 6 }, IM_COL32(255, 150, 150, 200), ev.functionName.c_str());
            }
        }

        rowY += TRACK_ROW_H;
    }

    // Playback cursor
    float cursorX = timeToScreen(currentTime, canvasTL.x, canvasSize.x);
    drawPlaybackCursor(dl, cursorX, canvasTL.y, canvasSize.y);

    // Interaction
    ImGui::SetCursorScreenPos(canvasTL);
    ImGui::InvisibleButton("##timelineCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        bool hitKeyframe = false;
        for (size_t vi = 0; vi < visibleTrackIndices.size() && !hitKeyframe; ++vi)
        {
            int ti = visibleTrackIndices[vi];
            if (ti < 0) continue;
            const auto& track = doc.tracks[ti];
            for (int ki = 0; ki < (int)track.keyframes.size(); ++ki)
            {
                const auto& kf = track.keyframes[ki];
                float kx = timeToScreen(kf.time, canvasTL.x, canvasSize.x);
                float ky = canvasTL.y + headerH + vi * TRACK_ROW_H + TRACK_ROW_H * 0.5f;
                float dx = mouse.x - kx, dy = mouse.y - ky;
                if (dx * dx + dy * dy <= KEYFRAME_HIT_R * KEYFRAME_HIT_R)
                {
                    selectedTrackIdx   = ti;
                    selectedKeyframeIdx = ki;
                    currentTime        = kf.time;
                    hitKeyframe = true;
                    break;
                }
            }
        }

        if (!hitKeyframe)
        {
            for (size_t ei = 0; ei < doc.events.size(); ++ei)
            {
                float ex = timeToScreen(doc.events[ei].time, canvasTL.x, canvasSize.x);
                float dx = mouse.x - ex;
                if (std::abs(dx) < 8)
                {
                    currentTime = doc.events[ei].time;
                    hitKeyframe = true;
                    break;
                }
            }
        }

        if (!hitKeyframe)
        {
            currentTime = screenToTime(mouse.x, canvasTL.x, canvasSize.x);
            currentTime = std::max(0.0f, std::min(currentTime, doc.duration));
            isDraggingTime = true;
            selectedTrackIdx = -1;
            selectedKeyframeIdx = -1;
        }
    }

    if (isDraggingTime && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        currentTime = screenToTime(mouse.x, canvasTL.x, canvasSize.x);
        currentTime = std::max(0.0f, std::min(currentTime, doc.duration));
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        isDraggingTime = false;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (selectedTrackIdx >= 0 && selectedKeyframeIdx >= 0)
            ImGui::OpenPopup("##KeyframeCtxMenu");
    }

    if (ImGui::BeginPopup("##KeyframeCtxMenu"))
    {
        if (selectedTrackIdx >= 0 && selectedTrackIdx < (int)doc.tracks.size())
        {
            auto& track = doc.tracks[selectedTrackIdx];
            if (selectedKeyframeIdx >= 0 && selectedKeyframeIdx < (int)track.keyframes.size())
            {
                auto& kf = track.keyframes[selectedKeyframeIdx];

                ImGui::Text("Keyframe at %.2fs", kf.time);
                ImGui::Separator();

                ImGui::DragFloat3("Value", &kf.value.x, 0.05f);
                if (ImGui::IsItemDeactivatedAfterEdit()) doc.dirty = true;

                const char* easingItems[] = {
                    "Linear", "Step", "Ease In", "Ease Out", "Ease In/Out",
                    "Cubic In", "Cubic Out", "Cubic In/Out",
                    "Sine In", "Sine Out", "Sine In/Out"
                };
                int easingIdx = (int)kf.easing;
                if (ImGui::Combo("Easing", &easingIdx, easingItems, IM_ARRAYSIZE(easingItems)))
                {
                    kf.easing = (AnimEasing)easingIdx;
                    doc.dirty = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Delete Keyframe"))
                    deleteSelectedKeyframe();
            }
        }
        ImGui::EndPopup();
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        timelineZoom = std::max(0.1f, std::min(10.0f, timelineZoom + ImGui::GetIO().MouseWheel * 0.1f));
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        timelineScroll -= ImGui::GetIO().MouseDelta.x / (100.0f * timelineZoom);
        timelineScroll = std::max(0.0f, timelineScroll);
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        deleteSelectedKeyframe();

    if (isPlaying)
    {
        float dt = ImGui::GetIO().DeltaTime;
        currentTime += dt;
        if (currentTime >= doc.duration)
        {
            if (loopPlayback)
                currentTime = 0.0f;
            else
            {
                currentTime = doc.duration;
                isPlaying = false;
            }
        }
    }

    dl->PopClipRect();
}

void PanelAnimation::drawKeyframeDiamond(ImDrawList* dl, ImVec2 center, bool selected, ImU32 color)
{
    float r = KEYFRAME_RADIUS;
    dl->AddTriangleFilled(
        { center.x, center.y - r },
        { center.x - r, center.y },
        { center.x + r, center.y },
        color);
    dl->AddTriangleFilled(
        { center.x, center.y + r },
        { center.x - r, center.y },
        { center.x + r, center.y },
        color);

    if (selected)
        dl->AddCircle(center, r + 2, IM_COL32(255, 255, 100, 255), 0, 2.0f);
}

void PanelAnimation::drawPlaybackCursor(ImDrawList* dl, float cursorX, float topY, float height)
{
    if (cursorX < topY - 10) return;
    dl->AddLine({ cursorX, topY }, { cursorX, topY + height },
                IM_COL32(255, 100, 50, 200), 2.0f);
    dl->AddTriangleFilled(
        { cursorX, topY + 8 },
        { cursorX - 6, topY },
        { cursorX + 6, topY },
        IM_COL32(255, 100, 50, 220));
}

void PanelAnimation::addKeyframeAtCursor()
{
    if (selectedTrackIdx < 0 || selectedTrackIdx >= (int)doc.tracks.size()) return;

    auto& track = doc.tracks[selectedTrackIdx];
    if (track.property == AnimTrackProperty::Event) return;

    kVec3 val = doc.evaluate(track.objectUuid, track.property, currentTime);
    doc.addKeyframe(track.objectUuid, track.objectName, track.property, currentTime, val);
}

void PanelAnimation::deleteSelectedKeyframe()
{
    if (selectedTrackIdx < 0 || selectedTrackIdx >= (int)doc.tracks.size()) return;
    auto& track = doc.tracks[selectedTrackIdx];
    if (selectedKeyframeIdx < 0 || selectedKeyframeIdx >= (int)track.keyframes.size()) return;

    track.keyframes.erase(track.keyframes.begin() + selectedKeyframeIdx);
    selectedKeyframeIdx = -1;
    doc.dirty = true;
}

// ===========================================================================
// Graph (Curve) Editor Tab
// ===========================================================================

float PanelAnimation::valueToGraphY(float val, float originY, float height) const
{
    float range = 5.0f / graphZoomY;
    float center = originY + height * 0.5f;
    return center - (val / range) * height * 0.5f;
}

float PanelAnimation::graphYToValue(float screenY, float originY, float height) const
{
    float range = 5.0f / graphZoomY;
    float center = originY + height * 0.5f;
    return (center - screenY) / (height * 0.5f) * range;
}

void PanelAnimation::drawGraphEditorTab()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 10 || avail.y < 10) return;

    bool hasContent = !filePath.empty() || doc.dirty;
    if (!hasContent || doc.type != "scene")
    {
        const char* msg = "Open or create a .cinematic file to edit curves";
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos({ (avail.x - textSize.x) * 0.5f, (avail.y - textSize.y) * 0.5f });
        ImGui::TextDisabled("%s", msg);
        return;
    }

    float selectorW = 180.0f;
    ImGui::BeginChild("##GraphTrackList", { selectorW, avail.y }, true);

    ImGui::Text("Tracks");
    ImGui::Separator();

    for (int i = 0; i < (int)doc.tracks.size(); ++i)
    {
        const auto& t = doc.tracks[i];
        if (t.property == AnimTrackProperty::Event) continue;

        std::string label = t.objectName + " " + trackPropShortName(t.property);
        bool isSelected = (i == graphSelectedTrack);
        if (ImGui::Selectable(label.c_str(), &isSelected))
            graphSelectedTrack = i;
    }

    if (doc.tracks.empty())
        ImGui::TextDisabled("No tracks");

    ImGui::EndChild();

    ImGui::SameLine();

    ImVec2 canvasTL = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = { avail.x - selectorW - ImGui::GetStyle().ItemSpacing.x, avail.y };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasTL, canvasTL + canvasSize, true);

    dl->AddRectFilled(canvasTL, canvasTL + canvasSize, IM_COL32(25, 25, 30, 255));

    drawGraphGrid(dl, canvasTL, canvasSize);

    if (graphSelectedTrack >= 0 && graphSelectedTrack < (int)doc.tracks.size())
    {
        const auto& track = doc.tracks[graphSelectedTrack];
        for (int ch = 0; ch < 3; ++ch)
        {
            ImU32 chCol = (ch == 0) ? IM_COL32(255, 80, 80, 200)
                        : (ch == 1) ? IM_COL32(80, 255, 80, 200)
                        : IM_COL32(80, 80, 255, 200);
            drawGraphCurve(dl, track, canvasTL, canvasSize, ch);
        }

        for (int ki = 0; ki < (int)track.keyframes.size(); ++ki)
        {
            const auto& kf = track.keyframes[ki];
            float kx = timeToScreen(kf.time, canvasTL.x, canvasSize.x);

            for (int ch = 0; ch < 3; ++ch)
            {
                float val = (ch == 0) ? kf.value.x : (ch == 1) ? kf.value.y : kf.value.z;
                float ky = valueToGraphY(val, canvasTL.y, canvasSize.y);

                bool isSelected = (ki == graphSelectedKey);
                ImU32 chCol = (ch == 0) ? IM_COL32(255, 80, 80, 200)
                            : (ch == 1) ? IM_COL32(80, 255, 80, 200)
                            : IM_COL32(80, 80, 255, 200);

                dl->AddCircleFilled({ kx, ky }, isSelected ? 5.0f : 3.5f, chCol);
                if (isSelected)
                    dl->AddCircle({ kx, ky }, 6.0f, IM_COL32(255, 255, 100, 255), 0, 2.0f);
            }
        }
    }

    ImGui::SetCursorScreenPos(canvasTL);
    ImGui::InvisibleButton("##graphCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
    {
        bool hitKey = false;
        if (graphSelectedTrack >= 0 && graphSelectedTrack < (int)doc.tracks.size())
        {
            const auto& track = doc.tracks[graphSelectedTrack];
            for (int ki = 0; ki < (int)track.keyframes.size(); ++ki)
            {
                const auto& kf = track.keyframes[ki];
                float kx = timeToScreen(kf.time, canvasTL.x, canvasSize.x);
                for (int ch = 0; ch < 3; ++ch)
                {
                    float val = (ch == 0) ? kf.value.x : (ch == 1) ? kf.value.y : kf.value.z;
                    float ky = valueToGraphY(val, canvasTL.y, canvasSize.y);
                    float dx = mouse.x - kx, dy = mouse.y - ky;
                    if (dx * dx + dy * dy < 8.0f * 8.0f)
                    {
                        graphSelectedKey = ki;
                        isDraggingGraphKey = true;
                        hitKey = true;
                        break;
                    }
                }
                if (hitKey) break;
            }
        }
        if (!hitKey)
            graphSelectedKey = -1;
    }

    if (isDraggingGraphKey && ImGui::IsMouseDown(ImGuiMouseButton_Left) && graphSelectedTrack >= 0)
    {
        if (graphSelectedKey >= 0 && graphSelectedKey < (int)doc.tracks[graphSelectedTrack].keyframes.size())
        {
            auto& kf = doc.tracks[graphSelectedTrack].keyframes[graphSelectedKey];
            float newTime = screenToTime(mouse.x, canvasTL.x, canvasSize.x);
            newTime = std::max(0.0f, std::min(newTime, doc.duration));
            kf.time = newTime;

            for (int ch = 0; ch < 3; ++ch)
            {
                float ky = valueToGraphY((ch == 0) ? kf.value.x : (ch == 1) ? kf.value.y : kf.value.z,
                                        canvasTL.y, canvasSize.y);
                if (std::abs(mouse.y - ky) < 10.0f)
                {
                    float newVal = graphYToValue(mouse.y, canvasTL.y, canvasSize.y);
                    if (ch == 0) kf.value.x = newVal;
                    else if (ch == 1) kf.value.y = newVal;
                    else kf.value.z = newVal;
                    break;
                }
            }

            doc.dirty = true;
            doc.sortKeyframes();
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        isDraggingGraphKey = false;

    if (hovered && ((ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) ||
        (ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
    {
        graphOffsetX += ImGui::GetIO().MouseDelta.x * 0.01f;
        graphOffsetY -= ImGui::GetIO().MouseDelta.y * 0.01f;
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        if (ImGui::GetIO().KeyCtrl)
            graphZoomY = std::max(0.1f, std::min(10.0f, graphZoomY + ImGui::GetIO().MouseWheel * 0.1f));
        else
            graphZoomX = std::max(0.1f, std::min(10.0f, graphZoomX + ImGui::GetIO().MouseWheel * 0.1f));
    }

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && graphSelectedKey >= 0)
    {
        if (graphSelectedTrack >= 0 && graphSelectedTrack < (int)doc.tracks.size())
        {
            auto& track = doc.tracks[graphSelectedTrack];
            if (graphSelectedKey >= 0 && graphSelectedKey < (int)track.keyframes.size())
            {
                track.keyframes.erase(track.keyframes.begin() + graphSelectedKey);
                graphSelectedKey = -1;
                doc.dirty = true;
            }
        }
    }

    dl->PopClipRect();
}

void PanelAnimation::drawGraphGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size)
{
    float range = 5.0f / graphZoomY;
    float step = 0.5f;
    float centerY = origin.y + size.y * 0.5f;

    for (float val = -range; val <= range; val += step)
    {
        float y = valueToGraphY(val, origin.y, size.y);
        if (y < origin.y || y > origin.y + size.y) continue;

        bool isZero = (std::abs(val) < 0.01f);
        dl->AddLine({ origin.x, y }, { origin.x + size.x, y },
                    isZero ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255));

        if (!isZero && graphZoomY >= 0.5f)
        {
            char label[16];
            snprintf(label, sizeof(label), "%.1f", val);
            dl->AddText({ origin.x + 2, y - 7 }, IM_COL32(120, 120, 120, 200), label);
        }
    }

    float pixelsPerSec = 100.0f * graphZoomX;
    float startTime = 0.0f;
    float endTime = 0.0f + size.x / pixelsPerSec + 0.5f;
    float zeroX = timeToScreen(0.0f, origin.x, size.x);

    for (float t = startTime; t <= endTime; t += 0.5f)
    {
        float x = timeToScreen(t, origin.x, size.x);
        if (x < origin.x || x > origin.x + size.x) continue;
        bool isSec = (std::abs(t - (float)round(t)) < 0.01f);
        dl->AddLine({ x, origin.y }, { x, origin.y + size.y },
                    isSec ? IM_COL32(60, 60, 60, 255) : IM_COL32(40, 40, 40, 255));
    }

    dl->AddLine({ zeroX, origin.y }, { zeroX, origin.y + size.y }, IM_COL32(80, 80, 80, 255));
    dl->AddLine({ origin.x, centerY }, { origin.x + size.x, centerY }, IM_COL32(80, 80, 80, 255));
}

void PanelAnimation::drawGraphCurve(ImDrawList* dl, const AnimTrack& track,
                                     ImVec2 origin, ImVec2 size, int channel)
{
    if (track.keyframes.size() < 2) return;

    ImU32 col = (channel == 0) ? IM_COL32(255, 80, 80, 180)
              : (channel == 1) ? IM_COL32(80, 255, 80, 180)
              : IM_COL32(80, 80, 255, 180);

    const int segments = 100;
    for (int i = 0; i < segments; ++i)
    {
        float t0 = (float)i / segments * doc.duration;
        float t1 = (float)(i + 1) / segments * doc.duration;

        kVec3 v0 = doc.evaluate(track.objectUuid, track.property, t0);
        kVec3 v1 = doc.evaluate(track.objectUuid, track.property, t1);

        float val0 = (channel == 0) ? v0.x : (channel == 1) ? v0.y : v0.z;
        float val1 = (channel == 0) ? v1.x : (channel == 1) ? v1.y : v1.z;

        float x0 = timeToScreen(t0, origin.x, size.x);
        float y0 = valueToGraphY(val0, origin.y, size.y);
        float x1 = timeToScreen(t1, origin.x, size.x);
        float y1 = valueToGraphY(val1, origin.y, size.y);

        dl->AddLine({ x0, y0 }, { x1, y1 }, col, 1.5f);
    }

    if (graphSelectedKey >= 0 && graphSelectedKey < (int)track.keyframes.size())
    {
        const auto& kf = track.keyframes[graphSelectedKey];
        float kx = timeToScreen(kf.time, origin.x, size.x);

        for (int ch = 0; ch < 3; ++ch)
        {
            float val = (ch == 0) ? kf.value.x : (ch == 1) ? kf.value.y : kf.value.z;
            float ky = valueToGraphY(val, origin.y, size.y);

            if (ch == channel)
            {
                float ltx = kx - 20.0f;
                float lty = ky - (channel == 0 ? kf.leftTangent.x : channel == 1 ? kf.leftTangent.y : kf.leftTangent.z) * 10.0f;
                dl->AddLine({ kx, ky }, { ltx, lty }, IM_COL32(200, 200, 200, 100), 1.0f);
                dl->AddCircleFilled({ ltx, lty }, 3.0f, IM_COL32(200, 200, 200, 120));

                float rtx = kx + 20.0f;
                float rty = ky - (channel == 0 ? kf.rightTangent.x : channel == 1 ? kf.rightTangent.y : kf.rightTangent.z) * 10.0f;
                dl->AddLine({ kx, ky }, { rtx, rty }, IM_COL32(200, 200, 200, 100), 1.0f);
                dl->AddCircleFilled({ rtx, rty }, 3.0f, IM_COL32(200, 200, 200, 120));
            }
        }
    }
}
