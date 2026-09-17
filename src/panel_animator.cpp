#include "panel_animator.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ===========================================================================
// Helpers
// ===========================================================================

static ImU32 toImU32(ImVec4 c)
{
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255));
}

static char asciiToLower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : (char)c;
}

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return { a.x + b.x, a.y + b.y }; }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return { a.x - b.x, a.y - b.y }; }
static ImVec2 operator*(ImVec2 a, float s)  { return { a.x * s,   a.y * s   }; }

static const char* animVarTypeName(AnimVariableType t)
{
    switch (t)
    {
        case AnimVariableType::Bool:    return "Bool";
        case AnimVariableType::Float:   return "Float";
        case AnimVariableType::Int:     return "Int";
    }
    return "Unknown";
}

static AnimVariableType animVarTypeFromName(const std::string& name)
{
    if (name == "Bool")    return AnimVariableType::Bool;
    if (name == "Int")     return AnimVariableType::Int;
    // Older .animator files may store "Trigger". It was a bool that the runtime
    // never auto-reset, so it was functionally identical to Bool; keep those
    // files loading by mapping the stored type to Bool.
    if (name == "Trigger") return AnimVariableType::Bool;
    return AnimVariableType::Float;
}

static const char* conditionCmpName(AnimCondition::Cmp c)
{
    switch (c)
    {
        case AnimCondition::Greater:      return ">";
        case AnimCondition::Less:         return "<";
        case AnimCondition::Equal:        return "==";
        case AnimCondition::NotEqual:     return "!=";
        case AnimCondition::GreaterEqual: return ">=";
        case AnimCondition::LessEqual:    return "<=";
        case AnimCondition::IsTrue:       return "is true";
        case AnimCondition::IsFalse:      return "is false";
    }
    return "?";
}

// ===========================================================================
// AnimCondition::evaluate
// ===========================================================================

bool AnimCondition::evaluate(const std::unordered_map<std::string, float>& vars) const
{
    auto it = vars.find(variableName);
    float val = (it != vars.end()) ? it->second : 0.0f;

    switch (comparison)
    {
        case Greater:      return val > threshold;
        case Less:         return val < threshold;
        case Equal:        return val == threshold;
        case NotEqual:     return val != threshold;
        case GreaterEqual: return val >= threshold;
        case LessEqual:    return val <= threshold;
        case IsTrue:       return val != 0.0f;
        case IsFalse:      return val == 0.0f;
    }
    return false;
}

// ===========================================================================
// AnimatorGraph implementation
// ===========================================================================

AnimState* AnimatorGraph::findState(int id)
{
    for (auto& s : states)
        if (s.id == id) return &s;
    return nullptr;
}

AnimTransition* AnimatorGraph::findTransition(int id)
{
    for (auto& t : transitions)
        if (t.id == id) return &t;
    return nullptr;
}

void AnimatorGraph::removeState(int stateId)
{
    removeTransitionsForState(stateId);
    states.erase(std::remove_if(states.begin(), states.end(),
        [stateId](const AnimState& s) { return s.id == stateId; }),
        states.end());
}

void AnimatorGraph::removeTransition(int transId)
{
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
        [transId](const AnimTransition& t) { return t.id == transId; }),
        transitions.end());
}

void AnimatorGraph::removeTransitionsForState(int stateId)
{
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
        [stateId](const AnimTransition& t) { return t.fromStateId == stateId || t.toStateId == stateId; }),
        transitions.end());
}

nlohmann::json AnimatorGraph::toJson() const
{
    json j;

    j["uuid"] = uuid;
    j["name"] = name;
    j["nextNodeId"] = nextNodeId;
    j["nextLinkId"] = nextLinkId;

    // Clips
    json clipsArr = json::array();
    for (const auto& [clipUuid, clip] : clips)
    {
        json c;
        c["uuid"] = clip.uuid;
        c["name"] = clip.name;
        clipsArr.push_back(c);
    }
    j["clips"] = clipsArr;

    // Variables
    json varsArr = json::array();
    for (const auto& v : variables)
    {
        json vj;
        vj["name"] = v.name;
        vj["type"] = animVarTypeName(v.type);
        vj["defaultValue"] = v.defaultValue;
        varsArr.push_back(vj);
    }
    j["variables"] = varsArr;

    // States
    json statesArr = json::array();
    for (const auto& s : states)
    {
        json sj;
        sj["id"]        = s.id;
        sj["kind"]      = (int)s.kind;
        sj["name"]      = s.name;
        sj["animationUuid"] = s.animationUuid;
        sj["speed"]     = s.speed;
        sj["loop"]      = s.loop;
        sj["isDefault"] = s.isDefault;
        sj["posX"]      = s.posX;
        sj["posY"]      = s.posY;
        sj["sizeX"]     = s.sizeX;
        sj["sizeY"]     = s.sizeY;
        sj["comment"]   = s.comment;
        statesArr.push_back(sj);
    }
    j["states"] = statesArr;

    // Transitions
    json transArr = json::array();
    for (const auto& t : transitions)
    {
        json tj;
        tj["id"]          = t.id;
        tj["fromStateId"] = t.fromStateId;
        tj["toStateId"]   = t.toStateId;
        tj["hasExitTime"] = t.hasExitTime;
        tj["exitTime"]    = t.exitTime;
        tj["blendMode"]     = (int)t.blendMode;
        tj["blendDuration"] = t.blendDuration;

        json condsArr = json::array();
        for (const auto& c : t.conditions)
        {
            json cj;
            cj["variableName"] = c.variableName;
            cj["comparison"]   = (int)c.comparison;
            cj["threshold"]    = c.threshold;
            condsArr.push_back(cj);
        }
        tj["conditions"] = condsArr;
        transArr.push_back(tj);
    }
    j["transitions"] = transArr;

    return j;
}

void AnimatorGraph::fromJson(const nlohmann::json& j)
{
    uuid = j.value("uuid", std::string());
    name = j.value("name", std::string("NewAnimator"));
    nextNodeId = j.value("nextNodeId", 1);
    nextLinkId = j.value("nextLinkId", 1);

    clips.clear();
    if (j.contains("clips"))
    {
        for (const auto& c : j["clips"])
        {
            AnimClipRef ref;
            ref.uuid = c.value("uuid", std::string());
            ref.name = c.value("name", std::string());
            clips[ref.uuid] = ref;
        }
    }

    variables.clear();
    if (j.contains("variables"))
    {
        for (const auto& v : j["variables"])
        {
            AnimVariable var;
            var.name = v.value("name", std::string());
            var.type = animVarTypeFromName(v.value("type", std::string("Float")));
            var.defaultValue = v.value("defaultValue", 0.0f);
            variables.push_back(var);
        }
    }

    states.clear();
    if (j.contains("states"))
    {
        for (const auto& s : j["states"])
        {
            AnimState st;
            st.id            = s.value("id", -1);
            st.kind          = (AnimStateKind)s.value("kind", (int)AnimStateKind::State);
            st.name          = s.value("name", std::string("State"));
            st.animationUuid = s.value("animationUuid", std::string());
            st.speed         = s.value("speed", 1.0f);
            st.loop          = s.value("loop", true);
            st.isDefault     = s.value("isDefault", false);
            st.posX          = s.value("posX", 100.0f);
            st.posY          = s.value("posY", 100.0f);
            st.sizeX         = s.value("sizeX", 320.0f);
            st.sizeY         = s.value("sizeY", 180.0f);
            st.comment       = s.value("comment", std::string("Comment"));
            states.push_back(st);
        }
    }

    transitions.clear();
    if (j.contains("transitions"))
    {
        for (const auto& t : j["transitions"])
        {
            AnimTransition tr;
            tr.id          = t.value("id", -1);
            tr.fromStateId = t.value("fromStateId", -1);
            tr.toStateId   = t.value("toStateId", -1);
            tr.hasExitTime = t.value("hasExitTime", false);
            tr.exitTime    = t.value("exitTime", 0.0f);
            tr.blendMode     = (AnimBlendMode)t.value("blendMode", (int)AnimBlendMode::CrossFade);
            tr.blendDuration = t.value("blendDuration", 0.25f);

            if (t.contains("conditions"))
            {
                for (const auto& c : t["conditions"])
                {
                    AnimCondition cond;
                    cond.variableName = c.value("variableName", std::string());
                    cond.comparison   = (AnimCondition::Cmp)c.value("comparison", (int)AnimCondition::Greater);
                    cond.threshold    = c.value("threshold", 0.0f);
                    tr.conditions.push_back(cond);
                }
            }
            transitions.push_back(tr);
        }
    }

    dirty = false;
}

// ===========================================================================
// Construction / file helpers
// ===========================================================================

PanelAnimator::PanelAnimator(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newGraph();
}

std::string PanelAnimator::generateUuid()
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

void PanelAnimator::newGraph()
{
    graph       = AnimatorGraph{};
    graph.uuid  = generateUuid();
    graph.name  = "NewAnimator";
    graph.dirty = false;
    filePath.clear();
    selectedState      = -1;
    selectedTransition = -1;
    editingVarIndex    = -1;
    isDraggingLink     = false;
    dragFromState      = -1;
    dragFromOutput     = false;
    isDraggingState    = false;

    // Add a default entry state
    AnimState entry;
    entry.id        = graph.newNodeId();
    entry.name      = "Default State";
    entry.isDefault = true;
    entry.posX      = 300.f;
    entry.posY      = 200.f;
    graph.states.push_back(entry);
}

void PanelAnimator::openFile(const std::string& path)
{
    loadGraph(path);
}

void PanelAnimator::loadGraph(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        graph.fromJson(j);
        filePath = path;
        graph.name = fs::path(path).stem().string();
        graph.dirty = false;
        selectedState      = -1;
        selectedTransition = -1;
        editingVarIndex    = -1;
        isDraggingLink     = false;
        dragFromState      = -1;
        dragFromOutput     = false;
        isDraggingState    = false;
    }
    catch (...) {}
}

void PanelAnimator::saveGraph()
{
    if (filePath.empty()) { saveGraphAs(); return; }

    graph.name = fs::path(filePath).stem().string();

    json j = graph.toJson();
    std::ofstream f(filePath);
    if (!f.is_open()) return;
    f << j.dump(4);
    graph.dirty = false;
}

void SDLCALL PanelAnimator::saveAnimatorCallback(void* userdata,
                                                  const char* const* filelist,
                                                  int /*filter*/)
{
    if (!filelist || !*filelist) return;
    PanelAnimator* self = static_cast<PanelAnimator*>(userdata);

    std::string path = filelist[0];
    if (path.size() < 9 || path.substr(path.size() - 9) != ".animator")
        path += ".animator";

    self->filePath = path;
    self->saveGraph();
}

void PanelAnimator::saveGraphAs()
{
    if (!manager->projectOpened) return;

    fs::path assetsDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
    fs::create_directories(assetsDir);

    std::string defaultName = (graph.name.empty() ? "NewAnimator" : graph.name) + ".animator";

    SDL_DialogFileFilter filters[] = {
        { "Animator files", "animator" },
        { "All files",      "*"        }
    };

    SDL_ShowSaveFileDialog(
        saveAnimatorCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (assetsDir / defaultName).string().c_str()
    );
}

// ===========================================================================
// Coordinate helpers
// ===========================================================================

ImVec2 PanelAnimator::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return origin + (cp + canvasOffset) * canvasZoom;
}

ImVec2 PanelAnimator::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return (sp - origin) * (1.f / canvasZoom) - canvasOffset;
}

ImVec2 PanelAnimator::getInputPinPos(const AnimState& state, ImVec2 origin) const
{
    // Anchor nodes are small pass-through circles with pins on their left/right.
    if (state.isAnchor())
    {
        float zoom = canvasZoom;
        ImVec2 tl  = canvasToScreen({ state.posX, state.posY }, origin);
        return { tl.x, tl.y + 12.f * zoom };
    }

    // Input pin: left side, vertically centered on the node body (below header)
    float zoom = canvasZoom;
    float hdrH = NODE_HEADER_H * zoom;
    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);
    return { tl.x, tl.y + hdrH + bodyH * 0.5f };
}

ImVec2 PanelAnimator::getOutputPinPos(const AnimState& state, ImVec2 origin) const
{
    // Anchor nodes are small pass-through circles with pins on their left/right.
    if (state.isAnchor())
    {
        float zoom = canvasZoom;
        ImVec2 tl  = canvasToScreen({ state.posX, state.posY }, origin);
        return { tl.x + 24.f * zoom, tl.y + 12.f * zoom };
    }

    // Output pin: right side, vertically centered on the node body (below header)
    float zoom = canvasZoom;
    float nw   = NODE_WIDTH * zoom;
    float hdrH = NODE_HEADER_H * zoom;
    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);
    return { tl.x + nw, tl.y + hdrH + bodyH * 0.5f };
}

int PanelAnimator::hitTestInputPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& s : graph.states)
    {
        if (s.isComment()) continue;
        ImVec2 p = getInputPinPos(s, origin);
        float dx = mouse.x - p.x, dy = mouse.y - p.y;
        // Anchors are tiny; keep their pin hit radius tight so the body can be
        // selected instead of the pin grabbing every click near the node.
        float r = PIN_RADIUS * canvasZoom * (s.isAnchor() ? 1.0f : 2.5f);
        if (dx * dx + dy * dy <= r * r)
            return s.id;
    }
    return -1;
}

int PanelAnimator::hitTestOutputPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& s : graph.states)
    {
        if (s.isComment()) continue;
        ImVec2 p = getOutputPinPos(s, origin);
        float dx = mouse.x - p.x, dy = mouse.y - p.y;
        // Anchors are tiny; keep their pin hit radius tight so the body can be
        // selected instead of the pin grabbing every click near the node.
        float r = PIN_RADIUS * canvasZoom * (s.isAnchor() ? 1.0f : 2.5f);
        if (dx * dx + dy * dy <= r * r)
            return s.id;
    }
    return -1;
}

int PanelAnimator::hitTestLinks(ImVec2 mouse, ImVec2 origin) const
{
    const int   segments  = 24;
    const float hitRadius = 9.0f;
    float bestDist2 = hitRadius * hitRadius;
    int   bestId    = -1;

    auto findStateById = [&](int id) -> const AnimState*
    {
        for (const auto& s : graph.states)
            if (s.id == id) return &s;
        return nullptr;
    };

    for (const auto& trans : graph.transitions)
    {
        const AnimState* from = findStateById(trans.fromStateId);
        const AnimState* to   = findStateById(trans.toStateId);
        if (!from || !to) continue;

        ImVec2 p0 = getOutputPinPos(*from, origin);
        ImVec2 p3 = getInputPinPos(*to, origin);
        float cx = (p3.x - p0.x) * 0.5f;
        ImVec2 p1 = { p0.x + cx, p0.y };
        ImVec2 p2 = { p3.x - cx, p3.y };

        ImVec2 prev = p0;
        for (int i = 1; i <= segments; ++i)
        {
            float t = (float)i / (float)segments;
            float u = 1.0f - t;
            float uu = u * u;
            float tt = t * t;
            ImVec2 pt = {
                uu * u * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + tt * t * p3.x,
                uu * u * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + tt * t * p3.y
            };

            float dx = pt.x - prev.x;
            float dy = pt.y - prev.y;
            float len2 = dx * dx + dy * dy;
            float proj = len2 > 0.0001f
                ? ((mouse.x - prev.x) * dx + (mouse.y - prev.y) * dy) / len2
                : 0.0f;
            proj = ImClamp(proj, 0.0f, 1.0f);

            float qx = prev.x + proj * dx;
            float qy = prev.y + proj * dy;
            float dist2 = (mouse.x - qx) * (mouse.x - qx) + (mouse.y - qy) * (mouse.y - qy);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                bestId    = trans.id;
            }
            prev = pt;
        }
    }
    return bestId;
}

// ===========================================================================
// Draw helpers
// ===========================================================================

void PanelAnimator::drawNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    if (state.isAnchor())  { drawAnchorNode(dl, state, origin);  return; }
    if (state.isComment()) { drawCommentNode(dl, state, origin); return; }

    const float zoom     = canvasZoom;
    const float nw       = NODE_WIDTH * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float fontSize = ImGui::GetFontSize();

    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * zoom;
    float totalH = hdrH + bodyH;

    ImVec2 topLeft = canvasToScreen({ state.posX, state.posY }, origin);
    ImVec2 botRight = topLeft + ImVec2(nw, totalH);

    bool isSelected = (state.id == selectedState);

    // Default state indicator: a slightly different header color
    ImVec4 hdrCol = state.isDefault
        ? ImVec4(0.15f, 0.55f, 0.15f, 1.f) // Green for default/entry
        : ImVec4(0.60f, 0.25f, 0.10f, 1.f); // Orange-red for normal states

    // Shadow
    dl->AddRectFilled({ topLeft.x + 3, topLeft.y + 3 }, { botRight.x + 3, botRight.y + 3 },
                      IM_COL32(0, 0, 0, 80), 6.f * zoom);

    // Body
    dl->AddRectFilled(topLeft, botRight, IM_COL32(45, 45, 45, 230), 6.f * zoom);

    // Header
    dl->AddRectFilled(topLeft, { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 6.f * zoom);
    // Flatten header bottom corners
    dl->AddRectFilled({ topLeft.x, topLeft.y + hdrH - 4.f * zoom },
                      { botRight.x, topLeft.y + hdrH },
                      toImU32(hdrCol), 0.f);

    // Outline
    ImU32 outlineCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(100, 100, 100, 180);
    dl->AddRect(topLeft, botRight, outlineCol, 6.f * zoom, 0, isSelected ? 2.f : 1.f);

    // Title
    ImVec2 titlePos = topLeft + ImVec2(6.f * zoom, (hdrH - fontSize) * 0.5f);
    dl->AddText(titlePos, IM_COL32(255, 255, 255, 255), state.name.c_str());

    // Body content: show animation name if assigned
    float bodyY = topLeft.y + hdrH + 4.f * zoom;
    if (!state.animationUuid.empty())
    {
        auto it = graph.clips.find(state.animationUuid);
        std::string clipName = (it != graph.clips.end()) ? it->second.name : state.animationUuid.substr(0, 8) + "...";
        std::string label = "Anim: " + clipName;
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(200, 200, 200, 255), label.c_str());
        bodyY += fontSize + 2.f * zoom;

        // Show speed & loop
        char speedBuf[32];
        snprintf(speedBuf, sizeof(speedBuf), "Speed: %.2f  %s", state.speed, state.loop ? "[Loop]" : "[Once]");
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(160, 160, 160, 255), speedBuf);
    }
    else
    {
        dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(140, 140, 140, 255), "No animation");
    }

    // Input pin (left)
    {
        ImVec2 inPos = getInputPinPos(state, origin);
        dl->AddCircleFilled(inPos, pinR, IM_COL32(100, 180, 255, 255));
        dl->AddCircle(inPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
    }

    // Output pin (right)
    {
        ImVec2 outPos = getOutputPinPos(state, origin);
        dl->AddCircleFilled(outPos, pinR, IM_COL32(255, 180, 80, 255));
        dl->AddCircle(outPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
    }
}

void PanelAnimator::drawAnchorNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    const float zoom = canvasZoom;
    const float r    = 12.f * zoom;
    ImVec2 c         = canvasToScreen({ state.posX, state.posY }, origin) + ImVec2(12.f * zoom, 12.f * zoom);
    bool  isSelected = (state.id == selectedState);

    dl->AddCircleFilled(c, r, IM_COL32(72, 84, 102, 235));
    dl->AddCircle(c, r, isSelected ? IM_COL32(255, 210, 80, 255) : IM_COL32(140, 165, 195, 255),
                  0, isSelected ? 2.5f : 1.5f);

    // Pins are drawn by getInputPinPos/getOutputPinPos geometry.
    ImVec2 inPos  = getInputPinPos(state, origin);
    ImVec2 outPos = getOutputPinPos(state, origin);
    dl->AddCircleFilled(inPos,  PIN_RADIUS * zoom, IM_COL32(100, 180, 255, 255));
    dl->AddCircleFilled(outPos, PIN_RADIUS * zoom, IM_COL32(255, 180, 80, 255));
}

void PanelAnimator::drawCommentNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    const float zoom = canvasZoom;
    ImVec2 tl        = canvasToScreen({ state.posX, state.posY }, origin);
    ImVec2 br        = tl + ImVec2(state.sizeX * zoom, state.sizeY * zoom);
    bool  isSelected = (state.id == selectedState);

    dl->AddRectFilled(tl, br, IM_COL32(60, 78, 105, 72), 8.f * zoom);
    dl->AddRect(tl, br,
                isSelected ? IM_COL32(235, 195, 90, 210) : IM_COL32(110, 138, 170, 150),
                8.f * zoom, 0, isSelected ? 2.f : 1.f);

    if (isSelected)
    {
        ImGui::SetCursorScreenPos({ tl.x + 6.f * zoom, tl.y + 4.f * zoom });
        ImGui::PushID(state.id);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        char buf[1024];
        strncpy_s(buf, state.comment.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(state.sizeX * zoom - 12.f * zoom);
        if (ImGui::InputTextMultiline("##animcomment", buf, sizeof(buf),
                                      ImVec2(state.sizeX * zoom - 12.f * zoom,
                                             state.sizeY * zoom - 8.f * zoom)))
        {
            state.comment = buf;
            graph.dirty   = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
    }
    else
    {
        std::string text = state.comment.empty() ? "Comment" : state.comment;
        ImVec2 p         = tl + ImVec2(8.f * zoom, 6.f * zoom);
        float  lineH     = ImGui::GetFontSize() + 2.f * zoom;
        for (int i = 0; i < 12 && !text.empty(); ++i)
        {
            std::string line = text;
            size_t nl = line.find('\n');
            if (nl != std::string::npos) { line = line.substr(0, nl); text = text.substr(nl + 1); }
            else                          text.clear();
            dl->AddText(p, IM_COL32(235, 235, 235, 255), line.c_str());
            p.y += lineH;
        }
    }

    ImVec2 handle = { br.x - 12.f * zoom, br.y - 12.f * zoom };
    dl->AddTriangleFilled(handle, br, { br.x, handle.y }, IM_COL32(210, 210, 220, 210));
}

void PanelAnimator::drawLinks(ImDrawList* dl, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float fontSize = ImGui::GetFontSize();

    for (const auto& trans : graph.transitions)
    {
        AnimState* from = graph.findState(trans.fromStateId);
        AnimState* to   = graph.findState(trans.toStateId);
        if (!from || !to) continue;

        ImVec2 p0 = getOutputPinPos(*from, origin);
        ImVec2 p3 = getInputPinPos(*to, origin);

        float cx = (p3.x - p0.x) * 0.5f;
        ImVec2 p1 = { p0.x + cx, p0.y };
        ImVec2 p2 = { p3.x - cx, p3.y };

        // Build condition text for tooltip
        std::string condStr;
        for (size_t i = 0; i < trans.conditions.size(); ++i)
        {
            if (i > 0) condStr += " AND ";
            condStr += trans.conditions[i].variableName + " " +
                       conditionCmpName(trans.conditions[i].comparison);
            if (trans.conditions[i].comparison != AnimCondition::IsTrue &&
                trans.conditions[i].comparison != AnimCondition::IsFalse)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), " %.2f", trans.conditions[i].threshold);
                condStr += buf;
            }
        }
        if (condStr.empty())
            condStr = "(no condition)";

        bool isSelected = (trans.id == selectedTransition);
        ImU32 col = isSelected ? IM_COL32(255, 220, 90, 255) : IM_COL32(180, 220, 255, 220);
        dl->AddBezierCubic(p0, p1, p2, p3, col, (isSelected ? 3.5f : 2.f) * zoom);

        // Tooltip on hover (check a point near the center of the bezier)
        ImVec2 mid = { (p0.x + p3.x) * 0.5f, (p0.y + p3.y) * 0.5f };
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float dx = mouse.x - mid.x, dy = mouse.y - mid.y;
        if (dx * dx + dy * dy < 20.f * 20.f)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(condStr.c_str());
            if (trans.hasExitTime)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "Exit time: %.2fs", trans.exitTime);
                ImGui::TextUnformatted(buf);
            }
            ImGui::EndTooltip();
        }

        // Draw condition label near the middle of the link
        if (zoom >= 0.7f && !condStr.empty())
        {
            ImVec2 labelPos = { (p0.x + p3.x) * 0.5f - ImGui::CalcTextSize(condStr.c_str()).x * 0.5f,
                                (p0.y + p3.y) * 0.5f - fontSize * 0.5f - 12.f * zoom };
            dl->AddText(labelPos, IM_COL32(255, 255, 200, 200), condStr.c_str());
        }
    }
}

void PanelAnimator::drawDragLink(ImDrawList* dl)
{
    if (!isDraggingLink) return;

    const float zoom = canvasZoom;

    AnimState* from = graph.findState(dragFromState);
    if (!from) return;

    // Use the canvas origin captured during the last drawCanvas call.
    ImVec2 origin = canvasOrigin;

    ImVec2 p0 = dragFromOutput ? getOutputPinPos(*from, origin)
                               : getInputPinPos(*from, origin);
    ImVec2 p3 = ImGui::GetIO().MousePos;
    float  cx = (p3.x - p0.x) * 0.5f;
    ImVec2 p1 = { p0.x + cx, p0.y };
    ImVec2 p2 = { p3.x - cx, p3.y };
    dl->AddBezierCubic(p0, p1, p2, p3, IM_COL32(180, 220, 255, 180), 2.f * zoom);
    dl->AddCircleFilled(p3, PIN_RADIUS * zoom, IM_COL32(255, 255, 255, 180));
}

// ===========================================================================
// Context menu (right-click on canvas)
// ===========================================================================

void PanelAnimator::drawStateContextMenu()
{
    // Right-click on canvas background → add new state
    if (ImGui::BeginPopup("##AnimStateAddMenu"))
    {
        if (ImGui::MenuItem("Add State"))
        {
            AnimState st;
            st.id   = graph.newNodeId();
            st.name = "New State";
            st.posX = contextMenuPos.x;
            st.posY = contextMenuPos.y;
            graph.states.push_back(st);
            graph.dirty = true;
        }
        if (ImGui::MenuItem("Add Anchor"))
        {
            AnimState st;
            st.id   = graph.newNodeId();
            st.kind = AnimStateKind::Anchor;
            st.name = "Anchor";
            st.posX = contextMenuPos.x;
            st.posY = contextMenuPos.y;
            graph.states.push_back(st);
            graph.dirty = true;
        }
        if (ImGui::MenuItem("Add Comment"))
        {
            AnimState st;
            st.id   = graph.newNodeId();
            st.kind = AnimStateKind::Comment;
            st.name = "Comment";
            st.posX = contextMenuPos.x;
            st.posY = contextMenuPos.y;
            graph.states.push_back(st);
            graph.dirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add Animation Clip Reference..."))
        {
            promptAddClip();
        }
        ImGui::EndPopup();
    }
}

// ===========================================================================
// State inspector (shown in the Inspector panel for the selected state)
// ===========================================================================

void PanelAnimator::drawSelectedStateInspector()
{
    if (selectedState < 0) return;

    AnimState* state = graph.findState(selectedState);
    if (!state)
    {
        selectedState = -1;
        return;
    }

    // Anchor and comment nodes have a simplified inspector.
    if (state->isAnchor() || state->isComment())
    {
        ImGui::TextUnformatted(state->isAnchor() ? "Animator Anchor" : "Animator Comment");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "   (id %d)", state->id);
        ImGui::Separator();
        ImGui::Spacing();

        char nameBuf[128];
        strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            state->name = nameBuf;
            graph.dirty = true;
        }

        if (state->isComment())
        {
            char textBuf[1024];
            strncpy_s(textBuf, state->comment.c_str(), sizeof(textBuf));
            textBuf[sizeof(textBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf), ImVec2(-FLT_MIN, 140.f)))
            {
                state->comment = textBuf;
                graph.dirty    = true;
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("Width",  &state->sizeX, 1.f, 80.f, 4000.f)) graph.dirty = true;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("Height", &state->sizeY, 1.f, 60.f, 4000.f)) graph.dirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.24f, 0.24f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.69f, 0.19f, 0.19f, 1.00f));
        if (ImGui::Button("Delete Node", ImVec2(-1, 0)))
        {
            graph.removeState(state->id);
            graph.dirty   = true;
            selectedState = -1;
        }
        ImGui::PopStyleColor(2);
        return;
    }

    // Header
    ImGui::TextUnformatted("Animator State");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "   (id %d)", state->id);
    ImGui::Separator();
    ImGui::Spacing();

    // Edit state name (disabled for the default state)
    bool isDefaultState = state->isDefault;
    char nameBuf[128];
    strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (isDefaultState)
        ImGui::BeginDisabled();
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
    {
        state->name = nameBuf;
        graph.dirty = true;
    }
    if (isDefaultState)
        ImGui::EndDisabled();

    // Assign animation via a picker window (same pattern as "Select Texture").
    ImGui::Spacing();
    {
        std::vector<std::string> animUuids, animNames;
        collectAnimationAssets(animUuids, animNames);

        std::string caption = "(None)";
        for (size_t i = 0; i < animUuids.size(); ++i)
        {
            if (animUuids[i] == state->animationUuid)
            {
                caption = animNames[i];
                break;
            }
        }

        if (ImGui::Button((caption + "##AnimPick").c_str(), ImVec2(-FLT_MIN, 0)))
        {
            animPickerSelected = state->animationUuid;
            animPickerSearch[0] = '\0';
            ImGui::OpenPopup("Select Animation##animpick");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to select an animation asset");

        // Accept a .animation asset dropped directly onto the picker button.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
            {
                std::string dropped((const char*)payload->Data);
                auto nl = dropped.find('\n');
                if (nl != std::string::npos)
                    dropped = dropped.substr(0, nl);

                auto it = manager->fileMap.find(dropped);
                if (it != manager->fileMap.end() && it->second.type == "animation" &&
                    fs::path(it->second.path.c_str()).extension() == ".animation")
                {
                    state->animationUuid = dropped;
                    if (graph.clips.find(dropped) == graph.clips.end())
                    {
                        AnimClipRef ref;
                        ref.uuid = dropped;
                        ref.name = fs::path(it->second.path.c_str()).stem().string();
                        graph.clips[dropped] = ref;
                    }
                    graph.dirty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        drawAnimPickerPopup(state);
    }

    // Speed
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("Speed", &state->speed, 0.05f, 0.0f, 10.0f))
        graph.dirty = true;

    // Loop
    ImGui::Spacing();
    if (ImGui::Checkbox("Loop", &state->loop))
        graph.dirty = true;

    // Default state indicator / set as default
    ImGui::Spacing();
    if (state->isDefault)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "This is the default state");
    }
    else
    {
        if (ImGui::Button("Set as Default", ImVec2(-1, 0)))
        {
            for (auto& s : graph.states) s.isDefault = false;
            state->isDefault = true;
            graph.dirty = true;
        }
    }

    // Delete (never remove the default state)
    if (!state->isDefault)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.24f, 0.24f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.69f, 0.19f, 0.19f, 1.00f));
        if (ImGui::Button("Delete State", ImVec2(-1, 0)))
        {
            graph.removeState(state->id);
            graph.dirty = true;
            selectedState = -1;
        }
        ImGui::PopStyleColor(2);
    }
}

// ===========================================================================
// Transition inspector (shown in the Inspector panel for the selected link)
// ===========================================================================

void PanelAnimator::drawSelectedTransitionInspector()
{
    if (selectedTransition < 0) return;

    AnimTransition* trans = graph.findTransition(selectedTransition);
    if (!trans)
    {
        selectedTransition = -1;
        return;
    }

    AnimState* from = graph.findState(trans->fromStateId);
    AnimState* to   = graph.findState(trans->toStateId);

    ImGui::TextUnformatted("Animator Transition");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "   (id %d)", trans->id);
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("From: %s", from ? from->name.c_str() : "(missing)");
    ImGui::Text("To:   %s", to ? to->name.c_str() : "(missing)");
    ImGui::Spacing();

    // -----------------------------------------------------------------------
    // Blending settings
    // -----------------------------------------------------------------------
    ImGui::TextUnformatted("Blending");
    ImGui::Separator();

    const char* blendModes[] = { "Instant", "Cross Fade" };
    int mode = (int)trans->blendMode;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("Blend Mode", &mode, blendModes, IM_ARRAYSIZE(blendModes)))
    {
        trans->blendMode = (AnimBlendMode)mode;
        graph.dirty = true;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("Blend Duration", &trans->blendDuration, 0.01f, 0.0f, 10.0f, "%.2fs"))
        graph.dirty = true;

    ImGui::Spacing();
    ImGui::TextUnformatted("Timing");
    ImGui::Separator();

    if (ImGui::Checkbox("Has Exit Time", &trans->hasExitTime))
        graph.dirty = true;
    if (trans->hasExitTime)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat("Exit Time", &trans->exitTime, 0.01f, 0.0f, 100.0f, "%.2fs"))
            graph.dirty = true;
    }

    // -----------------------------------------------------------------------
    // Conditions
    // -----------------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted("Conditions (AND)");
    ImGui::Separator();

    if (trans->conditions.empty())
        ImGui::TextDisabled("No conditions. This transition always evaluates to true.");

    int removeCond = -1;
    for (int i = 0; i < (int)trans->conditions.size(); ++i)
    {
        AnimCondition& cond = trans->conditions[i];
        ImGui::PushID(i);

        // Resolve the type of the variable this condition references.
        AnimVariableType condVarType = AnimVariableType::Float;

        if (graph.variables.empty())
        {
            ImGui::TextDisabled("(no variables available)");
        }
        else
        {
            std::vector<const char*> varNames;
            int currentVar = -1;
            for (int v = 0; v < (int)graph.variables.size(); ++v)
            {
                varNames.push_back(graph.variables[v].name.c_str());
                if (graph.variables[v].name == cond.variableName)
                {
                    currentVar = v;
                    condVarType = graph.variables[v].type;
                }
            }
            if (currentVar < 0)
            {
                currentVar = 0;
                cond.variableName = graph.variables[currentVar].name;
                condVarType = graph.variables[currentVar].type;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##condVar", &currentVar, varNames.data(), (int)varNames.size()))
            {
                cond.variableName = graph.variables[currentVar].name;
                condVarType       = graph.variables[currentVar].type;
                // Reset to a valid default comparison for the new variable type.
                cond.comparison = (condVarType == AnimVariableType::Bool)
                                      ? AnimCondition::IsTrue
                                      : AnimCondition::Greater;
                graph.dirty = true;
            }
        }

        // Build comparison options based on the referenced variable type.
        std::vector<AnimCondition::Cmp> cmpOptions;
        std::vector<const char*>        cmpLabels;
        switch (condVarType)
        {
            case AnimVariableType::Bool:
                cmpOptions = { AnimCondition::IsTrue, AnimCondition::IsFalse };
                cmpLabels  = { "is true", "is false" };
                break;
            case AnimVariableType::Int:
            case AnimVariableType::Float:
            default:
                cmpOptions = { AnimCondition::Greater, AnimCondition::Less, AnimCondition::Equal,
                               AnimCondition::NotEqual, AnimCondition::GreaterEqual, AnimCondition::LessEqual };
                cmpLabels  = { ">", "<", "==", "!=", ">=", "<=" };
                break;
        }

        // If the stored comparison is not valid for this type, clamp it to the
        // first available option before showing the combo.
        int cmpIdx = 0;
        for (int c = 0; c < (int)cmpOptions.size(); ++c)
            if (cmpOptions[c] == cond.comparison) { cmpIdx = c; break; }
        cond.comparison = cmpOptions[cmpIdx];

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##condCmp", &cmpIdx, cmpLabels.data(), (int)cmpLabels.size()))
        {
            cond.comparison = cmpOptions[cmpIdx];
            graph.dirty = true;
        }

        // The threshold input matches the variable type. Bool conditions have
        // no threshold value to enter.
        if (condVarType == AnimVariableType::Int)
        {
            int ival = (int)cond.threshold;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragInt("##condThr", &ival, 1.0f))
            {
                cond.threshold = (float)ival;
                graph.dirty = true;
            }
        }
        else if (condVarType == AnimVariableType::Float)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##condThr", &cond.threshold, 0.01f))
                graph.dirty = true;
        }

        if (ImGui::Button("Remove##cond"))
            removeCond = i;

        ImGui::PopID();
    }

    if (removeCond >= 0)
    {
        trans->conditions.erase(trans->conditions.begin() + removeCond);
        graph.dirty = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Add Condition", ImVec2(-1, 0)))
    {
        AnimCondition cond;
        if (!graph.variables.empty())
        {
            cond.variableName = graph.variables[0].name;
            AnimVariableType t = graph.variables[0].type;
            cond.comparison = (t == AnimVariableType::Bool)
                                  ? AnimCondition::IsTrue
                                  : AnimCondition::Greater;
        }
        else
        {
            cond.comparison = AnimCondition::Greater;
        }
        trans->conditions.push_back(cond);
        graph.dirty = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.24f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.69f, 0.19f, 0.19f, 1.00f));
    if (ImGui::Button("Delete Transition", ImVec2(-1, 0)))
    {
        graph.removeTransition(trans->id);
        graph.dirty = true;
        selectedTransition = -1;
    }
    ImGui::PopStyleColor(2);
}

void PanelAnimator::drawSelectedInspector()
{
    if (selectedTransition >= 0)
        drawSelectedTransitionInspector();
    else
        drawSelectedStateInspector();
}

// ===========================================================================
// Animation asset picker
// ===========================================================================

bool PanelAnimator::isAnimPickerOpen() const
{
    return ImGui::IsPopupOpen("Select Animation##animpick");
}

void PanelAnimator::collectAnimationAssets(std::vector<std::string>& uuids,
                                           std::vector<std::string>& names) const
{
    uuids.clear();
    names.clear();

    for (const auto& [uuid, info] : manager->fileMap)
    {
        if (info.type != "animation") continue;
        fs::path assetPath(info.path.c_str());
        if (assetPath.extension() != ".animation") continue;

        uuids.push_back(uuid);
        names.push_back(assetPath.stem().string());
    }

    // Keep graph-only clip references selectable too.
    for (const auto& [uuid, clip] : graph.clips)
    {
        if (std::find(uuids.begin(), uuids.end(), uuid) != uuids.end())
            continue;
        uuids.push_back(uuid);
        names.push_back(clip.name);
    }
}

void PanelAnimator::drawAnimPickerPopup(AnimState* state)
{
    if (!state) return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 440), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Select Animation##animpick", nullptr))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##animsearch", "Search...", animPickerSearch, sizeof(animPickerSearch));

        std::string filter = animPickerSearch;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) { return asciiToLower(c); });

        std::vector<std::string> animUuids, animNames;
        collectAnimationAssets(animUuids, animNames);

        bool doApply = false;

        float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
        ImGui::BeginChild("##animgrid", ImVec2(0, -footer), true);
        {
            // "(None)" clears the animation.
            {
                bool selected = animPickerSelected.empty();
                if (ImGui::Selectable("(None)", selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    animPickerSelected.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        doApply = true;
                }
            }

            for (size_t i = 0; i < animUuids.size(); ++i)
            {
                if (!filter.empty())
                {
                    std::string ln = animNames[i];
                    std::transform(ln.begin(), ln.end(), ln.begin(),
                                   [](unsigned char c) { return asciiToLower(c); });
                    if (ln.find(filter) == std::string::npos)
                        continue;
                }

                bool selected = (animUuids[i] == animPickerSelected);
                if (ImGui::Selectable(animNames[i].c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    animPickerSelected = animUuids[i];
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        doApply = true;
                }
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Select", ImVec2(120, 0)))
            doApply = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        if (doApply)
        {
            state->animationUuid = animPickerSelected;

            if (!animPickerSelected.empty() &&
                graph.clips.find(animPickerSelected) == graph.clips.end())
            {
                AnimClipRef ref;
                ref.uuid = animPickerSelected;
                auto it = manager->fileMap.find(animPickerSelected);
                ref.name = (it != manager->fileMap.end())
                               ? fs::path(it->second.path.c_str()).stem().string()
                               : animPickerSelected;
                graph.clips[animPickerSelected] = ref;
            }

            graph.dirty = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ===========================================================================
// Variable editor panel
// ===========================================================================

void PanelAnimator::drawVariablesPanel()
{
    ImGui::BeginChild("##animvars", ImVec2(variablesPanelWidth, 0.0f), true);

    // Add new variable
    if (ImGui::Button("Add Variable", ImVec2(-1.0f, 0.0f)))
    {
        AnimVariable var;
        var.name = "NewVar";
        // Find a unique name
        int counter = 1;
        bool unique = false;
        while (!unique)
        {
            unique = true;
            for (const auto& v : graph.variables)
            {
                if (v.name == var.name) { unique = false; break; }
            }
            if (!unique) var.name = "NewVar" + std::to_string(counter++);
        }
        graph.variables.push_back(var);
        editingVarIndex = (int)graph.variables.size() - 1;
        graph.dirty = true;
    }

    if (graph.variables.empty())
    {
        // Center the placeholder message in the column.
        const char* emptyMsg = "No variable defined";
        const float emptyTw  = ImGui::CalcTextSize(emptyMsg).x;
        const float emptyAw  = ImGui::GetContentRegionAvail().x;
        if (emptyAw > emptyTw)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (emptyAw - emptyTw) * 0.5f);
        ImGui::TextDisabled("%s", emptyMsg);
    }
    else
    {
        int removeIdx = -1;

        // Square remove button sized to the row height; it sits at the left edge
        // of its cell so the fixed column never crops it (same as logic graph).
        const float xBtn = ImGui::GetFrameHeight();
        const float xCol = xBtn + ImGui::GetStyle().CellPadding.x * 2.0f + 2.0f;

        const char* types[] = { "int", "float", "bool" };

        if (ImGui::BeginTable("##animvarstable", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, xCol);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)graph.variables.size(); ++i)
            {
                auto& var = graph.variables[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                // Name
                ImGui::TableSetColumnIndex(0);
                {
                    char nameBuf[128];
                    strncpy_s(nameBuf, var.name.c_str(), sizeof(nameBuf));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
                    {
                        var.name = nameBuf;
                        graph.dirty = true;
                    }
                }

                // Type
                ImGui::TableSetColumnIndex(1);
                {
                    int typeIdx = (int)var.type;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##type", &typeIdx, types, IM_ARRAYSIZE(types)))
                    {
                        var.type = (AnimVariableType)typeIdx;
                        graph.dirty = true;
                    }
                }

                // Default value
                ImGui::TableSetColumnIndex(2);
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (var.type == AnimVariableType::Bool)
                    {
                        bool bval = (var.defaultValue != 0.0f);
                        if (ImGui::Checkbox("##def", &bval))
                        {
                            var.defaultValue = bval ? 1.0f : 0.0f;
                            graph.dirty = true;
                        }
                    }
                    else if (var.type == AnimVariableType::Int)
                    {
                        int ival = (int)var.defaultValue;
                        if (ImGui::DragInt("##def", &ival, 1.0f))
                        {
                            var.defaultValue = (float)ival;
                            graph.dirty = true;
                        }
                    }
                    else
                    {
                        if (ImGui::DragFloat("##def", &var.defaultValue, 0.1f))
                            graph.dirty = true;
                    }
                }

                // Remove (square, left-aligned; label centered)
                ImGui::TableSetColumnIndex(3);
                // Zero the frame padding so the "x" glyph truly centers inside
                // the square. The theme's wide FramePadding inflates the button's
                // minimum width so it would overflow the cell and clip off-center.
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                if (ImGui::Button("x", ImVec2(xBtn, xBtn)))
                    removeIdx = i;
                ImGui::PopStyleVar(2);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (removeIdx >= 0)
        {
            graph.variables.erase(graph.variables.begin() + removeIdx);
            if (editingVarIndex == removeIdx) editingVarIndex = -1;
            else if (editingVarIndex > removeIdx) editingVarIndex--;
            graph.dirty = true;
        }
    }

    ImGui::EndChild();
}

// ===========================================================================
// Splitter between the variables column and the canvas
// ===========================================================================

void PanelAnimator::drawVariablesSplitter()
{
    const float splitterWidth = 6.f;
    const float minWidth      = 160.f;
    const float maxWidth      = 600.f;

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y < 1.f) avail.y = 1.f;
    ImGui::InvisibleButton("##animvarsplit", ImVec2(splitterWidth, avail.y));

    if (ImGui::IsItemActive())
    {
        variablesPanelWidth = ImClamp(variablesPanelWidth + ImGui::GetIO().MouseDelta.x,
                                      minWidth, maxWidth);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // Draw a subtle vertical grab handle.
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      rect = ImGui::GetItemRectMin();
    ImVec2      max  = ImGui::GetItemRectMax();
    ImU32       col  = ImGui::IsItemHovered() || ImGui::IsItemActive()
                          ? IM_COL32(120, 160, 220, 255)
                          : IM_COL32(70, 70, 70, 255);
    dl->AddRectFilled({ rect.x + 2.f, rect.y },
                      { max.x - 2.f, max.y }, col);

    ImGui::PopStyleVar();
    ImGui::SameLine();
}

// ===========================================================================
// Toolbar
// ===========================================================================

void PanelAnimator::drawToolbar()
{
    bool hasProject = manager->projectOpened;

    if (ImGui::Button("New"))
    {
        // TODO: prompt save if dirty
        newGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open") && hasProject)
    {
        fs::path animDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
        if (!fs::exists(animDir))
            fs::create_directories(animDir);

        SDL_DialogFileFilter filters[] = {
            { "Animator files", "animator" },
            { "All files",      "*"        }
        };

        SDL_ShowOpenFileDialog(
            [](void* userdata, const char* const* filelist, int) {
                if (!filelist || !*filelist) return;
                static_cast<PanelAnimator*>(userdata)->openFile(filelist[0]);
            },
            this,
            manager->getWindow()->getSdlWindow(),
            filters,
            SDL_arraysize(filters),
            animDir.string().c_str(),
            false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && hasProject)
        saveGraph();
    ImGui::SameLine();
    if (ImGui::Button("Save As...") && hasProject)
        saveGraphAs();

    // Opened file name centered across the whole toolbar width.
    std::string title = filePath.empty() ? std::string("untitled")
                                         : fs::path(filePath).filename().string();
    if (graph.dirty)
        title += " *";

    const float titleWidth   = ImGui::CalcTextSize(title.c_str()).x;
    const float contentMinX  = ImGui::GetWindowContentRegionMin().x;
    const float contentMaxX  = ImGui::GetWindowContentRegionMax().x;
    const float contentWidth = contentMaxX - contentMinX;
    ImGui::SameLine();
    if (contentWidth > titleWidth)
    {
        float centeredX = contentMinX + (contentWidth - titleWidth) * 0.5f;
        // Never slide back over the buttons.
        if (centeredX < ImGui::GetCursorPosX())
            centeredX = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(centeredX);
    }
    ImGui::TextUnformatted(title.c_str());

    // Zoom controls pinned to the right edge. The "Zoom" caption is dropped so
    // the slider stays compact beside the Reset View button.
    const float titleEndLocal = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    const float sliderW       = 120.f;
    const float resetW        = ImGui::CalcTextSize("Reset View").x +
                                ImGui::GetStyle().FramePadding.x * 2.0f;
    const float controlsW     = sliderW + ImGui::GetStyle().ItemSpacing.x + resetW;

    float startX = contentMaxX - controlsW;
    const float minX = titleEndLocal + ImGui::GetStyle().ItemSpacing.x;
    if (startX < minX)
        startX = minX;

    ImGui::SameLine(startX);
    ImGui::SetNextItemWidth(sliderW);
    ImGui::SliderFloat("##animzoom", &canvasZoom, 0.25f, 2.f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset View"))
    {
        canvasZoom   = 1.f;
        canvasOffset = { 0.f, 0.f };
    }
}

// ===========================================================================
// Canvas
// ===========================================================================

void PanelAnimator::drawCanvas()
{
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 10 || canvasSize.y < 10) return;

    ImVec2 canvasTL = ImGui::GetCursorScreenPos();
    canvasOrigin     = canvasTL;

    // Invisible button captures input
    ImGui::InvisibleButton("##animCanvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft  |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive  = ImGui::IsItemActive();
    ImVec2 mouse       = ImGui::GetIO().MousePos;
    ImVec2 mouseDelta  = ImGui::GetIO().MouseDelta;
    ImGuiIO& io        = ImGui::GetIO();

    // Accept .animation assets dropped from the Project panel onto a node.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
        {
            std::string dropped((const char*)payload->Data);
            auto nl = dropped.find('\n');
            if (nl != std::string::npos)
                dropped = dropped.substr(0, nl);

            auto it = manager->fileMap.find(dropped);
            if (it != manager->fileMap.end() && it->second.type == "animation" &&
                fs::path(it->second.path.c_str()).extension() == ".animation")
            {
                // Find the node currently under the mouse cursor.
                AnimState* target = nullptr;
                for (int i = (int)graph.states.size() - 1; i >= 0; --i)
                {
                    AnimState& st = graph.states[i];
                    ImVec2 nTL = canvasToScreen({ st.posX, st.posY }, canvasTL);
                    float nw = NODE_WIDTH * canvasZoom;
                    float bodyH = (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f) * canvasZoom;
                    float totalH = NODE_HEADER_H * canvasZoom + bodyH;

                    if (mouse.x >= nTL.x && mouse.x <= nTL.x + nw &&
                        mouse.y >= nTL.y && mouse.y <= nTL.y + totalH)
                    {
                        target = &st;
                        break;
                    }
                }

                if (target)
                {
                    // Dropped onto an existing node: apply the animation to it.
                    target->animationUuid = dropped;
                    if (graph.clips.find(dropped) == graph.clips.end())
                    {
                        AnimClipRef ref;
                        ref.uuid = dropped;
                        ref.name = fs::path(it->second.path.c_str()).stem().string();
                        graph.clips[dropped] = ref;
                    }
                    selectedState      = target->id;
                    selectedTransition = -1;
                    graph.dirty = true;
                }
                else
                {
                    // Dropped onto empty canvas: create a new state with this animation.
                    std::string clipName = fs::path(it->second.path.c_str()).stem().string();

                    AnimState st;
                    st.id            = graph.newNodeId();
                    st.name          = clipName.empty() ? "New State" : clipName;
                    st.animationUuid = dropped;
                    ImVec2 canvasPos  = screenToCanvas(mouse, canvasTL);
                    st.posX = canvasPos.x;
                    st.posY = canvasPos.y;
                    graph.states.push_back(st);

                    if (graph.clips.find(dropped) == graph.clips.end())
                    {
                        AnimClipRef ref;
                        ref.uuid = dropped;
                        ref.name = clipName;
                        graph.clips[dropped] = ref;
                    }

                    selectedState      = st.id;
                    selectedTransition = -1;
                    graph.dirty = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasTL, canvasTL + canvasSize, true);

    // --- Background ---
    dl->AddRectFilled(canvasTL, canvasTL + canvasSize, IM_COL32(28, 28, 28, 255));

    // Grid
    {
        float gridStep = 32.f * canvasZoom;
        ImU32 gridColMinor = IM_COL32(50, 50, 50, 255);
        ImU32 gridColMajor = IM_COL32(65, 65, 65, 255);

        float offX = fmodf(canvasOffset.x * canvasZoom, gridStep);
        float offY = fmodf(canvasOffset.y * canvasZoom, gridStep);

        if (offX < 0) offX += gridStep;
        if (offY < 0) offY += gridStep;

        int mx = (int)(canvasSize.x / gridStep) + 2;
        int my = (int)(canvasSize.y / gridStep) + 2;
        for (int i = 0; i <= mx; ++i)
        {
            float x = canvasTL.x + offX + i * gridStep;
            bool major = (i % 4 == 0);
            dl->AddLine({ x, canvasTL.y }, { x, canvasTL.y + canvasSize.y },
                        major ? gridColMajor : gridColMinor);
        }
        for (int i = 0; i <= my; ++i)
        {
            float y = canvasTL.y + offY + i * gridStep;
            bool major = (i % 4 == 0);
            dl->AddLine({ canvasTL.x, y }, { canvasTL.x + canvasSize.x, y },
                        major ? gridColMajor : gridColMinor);
        }
    }

    // --- Pan (middle mouse or alt + left) ---
    if (canvasHovered || isPanning)
    {
        bool panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                         (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyAlt);

        if (panButton && !isDraggingState && !isDraggingLink)
        {
            if (!isPanning)
            {
                isPanning      = true;
                panStartMouse  = mouse;
                panStartOffset = canvasOffset;
            }
            canvasOffset.x = panStartOffset.x + (mouse.x - panStartMouse.x) / canvasZoom;
            canvasOffset.y = panStartOffset.y + (mouse.y - panStartMouse.y) / canvasZoom;
        }
        else { isPanning = false; }
    }

    // Scroll to zoom
    if (canvasHovered && io.MouseWheel != 0.f)
    {
        float prevZoom = canvasZoom;
        canvasZoom = ImClamp(canvasZoom + io.MouseWheel * 0.1f, 0.25f, 2.f);
        ImVec2 mouseCanvas = screenToCanvas(mouse, canvasTL);
        canvasOffset.x += mouseCanvas.x * (1.f / prevZoom - 1.f / canvasZoom);
        canvasOffset.y += mouseCanvas.y * (1.f / prevZoom - 1.f / canvasZoom);
    }

    // --- Draw comment boxes behind everything else ---
    for (auto& state : graph.states)
        if (state.isComment())
            drawCommentNode(dl, state, canvasTL);

    // --- Draw links ---
    drawLinks(dl, canvasTL);
    drawDragLink(dl);

    // --- Draw nodes ---
    for (auto& state : graph.states)
        if (!state.isComment())
            drawNode(dl, state, canvasTL);

    // --- Interaction ---

    // Detect pin hover and start/finish connection drag
    int hovOutPin = hitTestOutputPins(mouse, canvasTL);
    int hovInPin  = hitTestInputPins(mouse, canvasTL);

    // Click on pin to start link drag
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        if (hovOutPin >= 0)
        {
            // Start dragging a link from this output (right) pin.
            isDraggingLink = true;
            dragFromState  = hovOutPin;
            dragFromOutput = true;
        }
        else if (hovInPin >= 0)
        {
            // Start dragging from an input (left) pin. The connection is
            // completed in reverse: output pin -> this input pin.
            isDraggingLink = true;
            dragFromState  = hovInPin;
            dragFromOutput = false;
        }
        else
        {
            // Comments / anchors are selected/moved by their whole body;
            // states are selected/moved by their header + body.
            bool hitState = false;
            for (int i = (int)graph.states.size() - 1; i >= 0; --i)
            {
                AnimState& state = graph.states[i];
                ImVec2 nTL = canvasToScreen({ state.posX, state.posY }, canvasTL);
                ImVec2 nBR;
                if (state.isComment())
                    nBR = nTL + ImVec2(state.sizeX * canvasZoom, state.sizeY * canvasZoom);
                else if (state.isAnchor())
                    nBR = nTL + ImVec2(24.f * canvasZoom, 24.f * canvasZoom);
                else
                    nBR = nTL + ImVec2(NODE_WIDTH * canvasZoom,
                                       (NODE_HEADER_H + (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f)) * canvasZoom);

                if (mouse.x < nTL.x || mouse.x > nBR.x || mouse.y < nTL.y || mouse.y > nBR.y)
                    continue;

                selectedState      = state.id;
                selectedTransition = -1;
                if (state.isComment())
                {
                    ImVec2 handle = { nBR.x - 12.f * canvasZoom, nBR.y - 12.f * canvasZoom };
                    isResizingComment = (mouse.x >= handle.x && mouse.y >= handle.y);
                    isDraggingState   = !isResizingComment;
                }
                else
                {
                    isDraggingState = true;
                }
                dragStateOffset = screenToCanvas(mouse, canvasTL) - ImVec2(state.posX, state.posY);
                hitState = true;
                break;
            }

            if (!hitState)
            {
                // Clicking empty space may select a transition link.
                int linkHit = hitTestLinks(mouse, canvasTL);
                if (linkHit >= 0)
                {
                    selectedTransition = linkHit;
                    selectedState      = -1;
                }
                else
                {
                    selectedState      = -1;
                    selectedTransition = -1;
                }
            }
        }
    }

    // Move / resize the selected state (or comment box).
    if ((isDraggingState || isResizingComment) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        AnimState* st = graph.findState(selectedState);
        if (st)
        {
            if (isResizingComment && st->isComment())
            {
                ImVec2 cp = screenToCanvas(mouse, canvasTL);
                st->sizeX = ImMax(80.f, cp.x - st->posX);
                st->sizeY = ImMax(60.f, cp.y - st->posY);
            }
            else
            {
                ImVec2 cp = screenToCanvas(mouse, canvasTL) - dragStateOffset;
                st->posX = cp.x;
                st->posY = cp.y;
            }
            graph.dirty = true;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDraggingState   = false;
        isResizingComment = false;

        if (isDraggingLink)
        {
            isDraggingLink = false;

            // Complete the link if we released on a valid pin. The direction
            // was recorded when the drag started, so either output->input or
            // input->output works regardless of which node was dragged from.
            int targetOut = hitTestOutputPins(mouse, canvasTL);
            int targetIn  = hitTestInputPins(mouse, canvasTL);

            int fromState = dragFromState;
            int toState   = -1;

            if (dragFromOutput)
            {
                // Dragged from a right (output) pin: connect it to a left (input) pin.
                if (targetIn >= 0 && targetIn != dragFromState)
                    toState = targetIn;
            }
            else
            {
                // Dragged from a left (input) pin: connect the target right
                // (output) pin into the state this input belongs to.
                if (targetOut >= 0 && targetOut != dragFromState)
                {
                    fromState = targetOut;
                    toState   = dragFromState;
                }
            }

            if (toState >= 0 && fromState != toState)
            {
                // Check for duplicate transition
                bool exists = false;
                for (const auto& t : graph.transitions)
                {
                    if (t.fromStateId == fromState && t.toStateId == toState)
                    { exists = true; break; }
                }

                if (!exists)
                {
                    // Transitions may point to any state, including the default.
                    AnimState* toSt = graph.findState(toState);
                    if (toSt)
                    {
                        AnimTransition trans;
                        trans.id          = graph.newLinkId();
                        trans.fromStateId = fromState;
                        trans.toStateId   = toState;
                        graph.transitions.push_back(trans);
                        graph.dirty = true;
                    }
                }
            }
        }
    }

    // Right-click on canvas → context menu (state settings moved to the Inspector panel)
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !isDraggingLink)
    {
        // Check if we right-clicked on a node → select it so its form appears in the Inspector.
        bool hitState = false;
        for (int i = (int)graph.states.size() - 1; i >= 0; --i)
        {
            AnimState& state = graph.states[i];
            ImVec2 nTL = canvasToScreen({ state.posX, state.posY }, canvasTL);
            ImVec2 nBR;
            if (state.isComment())
                nBR = nTL + ImVec2(state.sizeX * canvasZoom, state.sizeY * canvasZoom);
            else if (state.isAnchor())
                nBR = nTL + ImVec2(24.f * canvasZoom, 24.f * canvasZoom);
            else
                nBR = nTL + ImVec2(NODE_WIDTH * canvasZoom,
                                   (NODE_HEADER_H + (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f)) * canvasZoom);

            if (mouse.x >= nTL.x && mouse.x <= nBR.x &&
                mouse.y >= nTL.y && mouse.y <= nBR.y)
            {
                selectedState      = state.id;
                selectedTransition = -1;
                hitState = true;
                break;
            }
        }
        if (!hitState)
        {
            contextMenuPos = screenToCanvas(mouse, canvasTL);
            ImGui::OpenPopup("##AnimStateAddMenu");
        }
    }

    // Delete key: remove the selected transition, otherwise the selected state.
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (selectedTransition >= 0)
        {
            graph.removeTransition(selectedTransition);
            graph.dirty        = true;
            selectedTransition = -1;
        }
        else if (selectedState >= 0)
        {
            AnimState* st = graph.findState(selectedState);
            if (st && (!st->isDefault || st->isAnchor() || st->isComment()))
            {
                graph.removeState(selectedState);
                graph.dirty    = true;
                selectedState  = -1;
            }
        }
    }

    dl->PopClipRect();
}

// ===========================================================================
// Prompt add clip — opens an SDL file dialog to select an .animation file
// ===========================================================================

void PanelAnimator::promptAddClip()
{
    if (!manager->projectOpened) return;

    fs::path animDir = fs::path(manager->projectPath.c_str()) / "Assets" / "Animations";
    if (!fs::exists(animDir))
        fs::create_directories(animDir);

    // We'll use a simple approach: scan the Animations folder for .animation files
    // and show them in a selectable list
    std::vector<fs::path> animFiles;
    if (fs::exists(animDir))
    {
        for (const auto& entry : fs::directory_iterator(animDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".animation")
                animFiles.push_back(entry.path());
        }
    }

    if (animFiles.empty())
    {
        // No .animation files found — prompt the user to create one
        ImGui::OpenPopup("##NoAnimFiles");
        return;
    }

    // For simplicity, we add all found .animation files as clip references
    for (const auto& p : animFiles)
    {
        // Read the .animation file to get its UUID and name
        try
        {
            std::ifstream f(p);
            if (f.is_open())
            {
                json j; f >> j;
                std::string clipUuid = j.value("uuid", std::string());
                std::string clipName = j.value("name", p.stem().string());
                if (!clipUuid.empty() && graph.clips.find(clipUuid) == graph.clips.end())
                {
                    AnimClipRef ref;
                    ref.uuid = clipUuid;
                    ref.name = clipName;
                    graph.clips[clipUuid] = ref;
                    graph.dirty = true;
                }
            }
        }
        catch (...) {}
    }
}

// ===========================================================================
// Main draw
// ===========================================================================

void PanelAnimator::draw(bool& isOpened)
{
    visible = isOpened;
    if (!isOpened) return;

    ImGui::SetNextWindowSize({ 1000, 700 }, ImGuiCond_FirstUseEver);

    kString title = graph.dirty ? "Animator *" : "Animator";
    title += "###AnimatorEditor";

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(title.c_str(), &isOpened, wflags))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    drawToolbar();
    drawVariablesPanel();
    drawVariablesSplitter();
    drawCanvas();
    drawStateContextMenu();

    ImGui::End();
}
