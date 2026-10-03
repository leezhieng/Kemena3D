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
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include "blend_weights.h"

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

// Property row helper for the animator inspectors: draws a field caption on the
// left, then continues on the same line so the input widget sits to its right.
// This replaces ImGui's default right-side labels and the stacked
// label-above-widget layout previously used for state / transition / blend-tree
// fields, keeping every caption on the left, on the same line as its input.
static constexpr float PROP_LABEL_W = 120.0f;
static void propLabelLeft(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(PROP_LABEL_W);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

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

        // Blend-tree payload (written for every node so round-trips are exact).
        sj["blendType"]   = (int)s.blendType;
        sj["blendParamX"] = s.blendParamX;
        sj["blendParamY"] = s.blendParamY;
        sj["blendRangeX"] = json::array({ s.blendRangeXMin, s.blendRangeXMax });
        sj["blendRangeY"] = json::array({ s.blendRangeYMin, s.blendRangeYMax });

        // Blend-tree blending / timing (mirrors the transition payload).
        sj["blendMode"]     = (int)s.blendMode;
        sj["blendDuration"] = s.blendDuration;
        sj["hasExitTime"]   = s.hasExitTime;
        sj["exitTime"]      = s.exitTime;

        json blendArr = json::array();
        for (const auto& c : s.blendChildren)
        {
            json cj;
            cj["stateId"]   = c.stateId;
            cj["threshold"] = c.threshold;
            cj["posX"]      = c.posX;
            cj["posY"]      = c.posY;
            cj["speed"]     = c.speed;

            // Per-motion bone-mask group weights (this motion's influence on
            // the base mesh's authored regions).
            json childMaskArr = json::array();
            for (const auto& m : c.maskWeights)
            {
                json mj;
                mj["maskName"] = m.maskName;
                mj["weight"]   = m.weight;
                childMaskArr.push_back(mj);
            }
            cj["maskWeights"] = childMaskArr;

            blendArr.push_back(cj);
        }
        sj["blendChildren"] = blendArr;

        // Bone-mask group weights (partial-animation influence per region).
        json maskArr = json::array();
        for (const auto& m : s.maskWeights)
        {
            json mj;
            mj["maskName"] = m.maskName;
            mj["weight"]   = m.weight;
            maskArr.push_back(mj);
        }
        sj["maskWeights"] = maskArr;

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

            // Blend-tree payload.
            st.blendType   = (AnimBlendType)s.value("blendType", (int)AnimBlendType::OneD);
            st.blendParamX = s.value("blendParamX", std::string());
            st.blendParamY = s.value("blendParamY", std::string());
            if (s.contains("blendRangeX") && s["blendRangeX"].is_array() && s["blendRangeX"].size() >= 2)
            {
                st.blendRangeXMin = s["blendRangeX"][0].get<float>();
                st.blendRangeXMax = s["blendRangeX"][1].get<float>();
            }
            if (s.contains("blendRangeY") && s["blendRangeY"].is_array() && s["blendRangeY"].size() >= 2)
            {
                st.blendRangeYMin = s["blendRangeY"][0].get<float>();
                st.blendRangeYMax = s["blendRangeY"][1].get<float>();
            }

            // Blend-tree blending / timing (absent in older files → defaults).
            st.blendMode     = (AnimBlendMode)s.value("blendMode", (int)AnimBlendMode::CrossFade);
            st.blendDuration = s.value("blendDuration", 0.0f);
            st.hasExitTime   = s.value("hasExitTime", false);
            st.exitTime      = s.value("exitTime", 0.0f);
            if (s.contains("blendChildren") && s["blendChildren"].is_array())
            {
                for (const auto& c : s["blendChildren"])
                {
                    AnimBlendChild bc;
                    bc.stateId   = c.value("stateId", -1);
                    bc.threshold = c.value("threshold", 0.0f);
                    bc.posX      = c.value("posX", 0.0f);
                    bc.posY      = c.value("posY", 0.0f);
                    bc.speed     = c.value("speed", 1.0f);

                    // Per-motion mask weights (absent in older files → empty).
                    if (c.contains("maskWeights") && c["maskWeights"].is_array())
                    {
                        for (const auto& m : c["maskWeights"])
                        {
                            AnimMaskWeight mw;
                            mw.maskName = m.value("maskName", std::string());
                            mw.weight   = m.value("weight", 1.0f);
                            if (!mw.maskName.empty())
                                bc.maskWeights.push_back(mw);
                        }
                    }

                    st.blendChildren.push_back(bc);
                }
            }

            // Bone-mask group weights (absent in older files → empty).
            if (s.contains("maskWeights") && s["maskWeights"].is_array())
            {
                for (const auto& m : s["maskWeights"])
                {
                    AnimMaskWeight mw;
                    mw.maskName = m.value("maskName", std::string());
                    mw.weight   = m.value("weight", 1.0f);
                    if (!mw.maskName.empty())
                        st.maskWeights.push_back(mw);
                }
            }
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
    dragBlendChildIndex = -1;
    blendPreviewValues.clear();
    releasePreviewMesh();
    previewSig.clear();
    previewMeshUuids.clear();
    previewMeshNames.clear();

    // Add the always-present Default State (the entry point).
    AnimState entry;
    entry.id        = graph.newNodeId();
    entry.name      = "Default State";
    entry.isDefault = true;
    entry.posX      = 300.f;
    entry.posY      = 200.f;
    graph.states.push_back(entry);

    // Add the always-present Any State node. It is source-only (no input pin)
    // and, like the Default State, cannot be deleted.
    AnimState any;
    any.id   = graph.newNodeId();
    any.kind = AnimStateKind::AnyState;
    any.name = "Any State";
    any.posX = 40.f;
    any.posY = 40.f;
    graph.states.push_back(any);
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
        // Guarantee the always-present nodes exist even in files written before
        // Any State / Blend Tree support was added.
        bool added = ensureSpecialNodes();
        graph.dirty = added;
        selectedState      = -1;
        selectedTransition = -1;
        editingVarIndex    = -1;
        isDraggingLink     = false;
        dragFromState      = -1;
        dragFromOutput     = false;
        isDraggingState    = false;
        dragBlendChildIndex = -1;
        blendPreviewValues.clear();
        releasePreviewMesh();
        previewSig.clear();
        previewMeshUuids.clear();
        previewMeshNames.clear();
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

// ---------------------------------------------------------------------------
// Node geometry + lookups
// ---------------------------------------------------------------------------

float PanelAnimator::nodeWidth(const AnimState& state) const
{
    switch (state.kind)
    {
        case AnimStateKind::Comment:   return state.sizeX;
        case AnimStateKind::Anchor:    return 24.f;
        case AnimStateKind::AnyState:  return ANY_STATE_WIDTH;
        case AnimStateKind::BlendTree: return BLEND_NODE_WIDTH;
        case AnimStateKind::State:
        default:                       return NODE_WIDTH;
    }
}

float PanelAnimator::nodeHeight(const AnimState& state) const
{
    switch (state.kind)
    {
        case AnimStateKind::Comment:   return state.sizeY;
        case AnimStateKind::Anchor:    return 24.f;
        case AnimStateKind::AnyState:  return NODE_HEADER_H + ANY_STATE_BODY_H;
        case AnimStateKind::BlendTree: return NODE_HEADER_H +
                                              (state.blendType == AnimBlendType::TwoD
                                                   ? BLEND_BODY_H_2D : BLEND_BODY_H_1D);
        case AnimStateKind::State:
        default:
            return NODE_HEADER_H + (60.f > PIN_ROW_H * 2.f ? 60.f : PIN_ROW_H * 2.f);
    }
}

void PanelAnimator::nodeScreenRect(const AnimState& state, ImVec2 origin, ImVec2& tl, ImVec2& br) const
{
    tl = canvasToScreen({ state.posX, state.posY }, origin);
    br = tl + ImVec2(nodeWidth(state) * canvasZoom, nodeHeight(state) * canvasZoom);
}

void PanelAnimator::blendDiagramRect(const AnimState& state, ImVec2 origin, ImVec2& tl, ImVec2& br) const
{
    ImVec2 nTL, nBR;
    nodeScreenRect(state, origin, nTL, nBR);
    float pad = BLEND_PAD * canvasZoom;
    tl = { nTL.x + pad, nTL.y + NODE_HEADER_H * canvasZoom + pad };
    br = { nBR.x - pad, nBR.y - pad };
}

AnimState* PanelAnimator::findAnyState()
{
    for (auto& s : graph.states)
        if (s.isAnyState()) return &s;
    return nullptr;
}

AnimState* PanelAnimator::findDefaultState()
{
    for (auto& s : graph.states)
        if (s.isState() && s.isDefault) return &s;
    return nullptr;
}

bool PanelAnimator::ensureSpecialNodes()
{
    bool added = false;

    // Exactly one Default State must exist.
    if (!findDefaultState())
    {
        AnimState* firstState = nullptr;
        for (auto& s : graph.states)
            if (s.isState()) { firstState = &s; break; }

        if (firstState)
        {
            firstState->isDefault = true;
        }
        else
        {
            AnimState entry;
            entry.id        = graph.newNodeId();
            entry.name      = "Default State";
            entry.isDefault = true;
            entry.posX      = 300.f;
            entry.posY      = 200.f;
            graph.states.push_back(entry);
            added = true;
        }
    }

    // Exactly one Any State node must exist.
    if (!findAnyState())
    {
        AnimState any;
        any.id   = graph.newNodeId();
        any.kind = AnimStateKind::AnyState;
        any.name = "Any State";
        any.posX = 40.f;
        any.posY = 40.f;
        graph.states.push_back(any);
        added = true;
    }

    return added;
}

ImVec2 PanelAnimator::getInputPinPos(const AnimState& state, ImVec2 origin) const
{
    const float zoom = canvasZoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);

    // Anchor nodes are small pass-through circles with pins on their left/right.
    if (state.isAnchor())
        return { tl.x, tl.y + 12.f * zoom };

    // Input pin: left side, vertically centered on the node body (below header).
    float hdrH  = NODE_HEADER_H * zoom;
    float bodyH = (nodeHeight(state) - NODE_HEADER_H) * zoom;
    return { tl.x, tl.y + hdrH + bodyH * 0.5f };
}

ImVec2 PanelAnimator::getOutputPinPos(const AnimState& state, ImVec2 origin) const
{
    const float zoom = canvasZoom;
    ImVec2 tl = canvasToScreen({ state.posX, state.posY }, origin);

    // Anchor nodes are small pass-through circles with pins on their left/right.
    if (state.isAnchor())
        return { tl.x + 24.f * zoom, tl.y + 12.f * zoom };

    // Output pin: right side, vertically centered on the node body (below header).
    float nw    = nodeWidth(state) * zoom;
    float hdrH  = NODE_HEADER_H * zoom;
    float bodyH = (nodeHeight(state) - NODE_HEADER_H) * zoom;
    return { tl.x + nw, tl.y + hdrH + bodyH * 0.5f };
}

int PanelAnimator::hitTestInputPins(ImVec2 mouse, ImVec2 origin) const
{
    for (const auto& s : graph.states)
    {
        // The Any State node is source-only, so it contributes no input pin.
        if (s.isComment() || s.isAnyState()) continue;
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
    if (state.isAnchor())    { drawAnchorNode(dl, state, origin);    return; }
    if (state.isComment())   { drawCommentNode(dl, state, origin);   return; }
    if (state.isAnyState())  { drawAnyStateNode(dl, state, origin);  return; }
    if (state.isBlendTree()) { drawBlendTreeNode(dl, state, origin); return; }

    const float zoom     = canvasZoom;
    const float nw       = nodeWidth(state) * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float fontSize = ImGui::GetFontSize();

    float totalH = nodeHeight(state) * zoom;

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

void PanelAnimator::drawAnyStateNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float nw       = ANY_STATE_WIDTH * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float fontSize = ImGui::GetFontSize();

    const float totalH = (NODE_HEADER_H + ANY_STATE_BODY_H) * zoom;

    ImVec2 topLeft  = canvasToScreen({ state.posX, state.posY }, origin);
    ImVec2 botRight = topLeft + ImVec2(nw, totalH);

    bool isSelected = (state.id == selectedState);
    ImVec4 hdrCol   = ImVec4(0.10f, 0.20f, 0.55f, 1.f); // deep blue

    dl->AddRectFilled({ topLeft.x + 3, topLeft.y + 3 }, { botRight.x + 3, botRight.y + 3 },
                      IM_COL32(0, 0, 0, 80), 6.f * zoom);
    dl->AddRectFilled(topLeft, botRight, IM_COL32(40, 44, 52, 235), 6.f * zoom);
    dl->AddRectFilled(topLeft, { botRight.x, topLeft.y + hdrH }, toImU32(hdrCol), 6.f * zoom);
    dl->AddRectFilled({ topLeft.x, topLeft.y + hdrH - 4.f * zoom },
                      { botRight.x, topLeft.y + hdrH }, toImU32(hdrCol), 0.f);

    ImU32 outlineCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(100, 100, 100, 180);
    dl->AddRect(topLeft, botRight, outlineCol, 6.f * zoom, 0, isSelected ? 2.f : 1.f);

    ImVec2 titlePos = topLeft + ImVec2(6.f * zoom, (hdrH - fontSize) * 0.5f);
    dl->AddText(titlePos, IM_COL32(255, 255, 255, 255), state.name.c_str());

    float bodyY = topLeft.y + hdrH + 4.f * zoom;
    dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(160, 190, 230, 255), "Source only");
    bodyY += fontSize + 2.f * zoom;
    dl->AddText({ topLeft.x + 6.f * zoom, bodyY }, IM_COL32(150, 150, 150, 255),
                "Fires from any state");

    // Output pin only: the Any State node is never a destination.
    ImVec2 outPos = getOutputPinPos(state, origin);
    dl->AddCircleFilled(outPos, pinR, IM_COL32(255, 180, 80, 255));
    dl->AddCircle(outPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
}

void PanelAnimator::drawBlendTreeNode(ImDrawList* dl, AnimState& state, ImVec2 origin)
{
    const float zoom     = canvasZoom;
    const float nw       = BLEND_NODE_WIDTH * zoom;
    const float hdrH     = NODE_HEADER_H * zoom;
    const float pinR     = PIN_RADIUS * zoom;
    const float fontSize = ImGui::GetFontSize();

    const bool  is2D  = (state.blendType == AnimBlendType::TwoD);
    const float bodyH = (is2D ? BLEND_BODY_H_2D : BLEND_BODY_H_1D) * zoom;

    ImVec2 topLeft  = canvasToScreen({ state.posX, state.posY }, origin);
    ImVec2 botRight = topLeft + ImVec2(nw, hdrH + bodyH);

    bool isSelected = (state.id == selectedState);
    ImVec4 hdrCol   = ImVec4(0.42f, 0.20f, 0.58f, 1.f); // purple

    dl->AddRectFilled({ topLeft.x + 3, topLeft.y + 3 }, { botRight.x + 3, botRight.y + 3 },
                      IM_COL32(0, 0, 0, 80), 6.f * zoom);
    dl->AddRectFilled(topLeft, botRight, IM_COL32(45, 45, 48, 235), 6.f * zoom);
    dl->AddRectFilled(topLeft, { botRight.x, topLeft.y + hdrH }, toImU32(hdrCol), 6.f * zoom);
    dl->AddRectFilled({ topLeft.x, topLeft.y + hdrH - 4.f * zoom },
                      { botRight.x, topLeft.y + hdrH }, toImU32(hdrCol), 0.f);

    ImU32 outlineCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(100, 100, 100, 180);
    dl->AddRect(topLeft, botRight, outlineCol, 6.f * zoom, 0, isSelected ? 2.f : 1.f);

    // Title + 1D/2D tag.
    ImVec2 titlePos = topLeft + ImVec2(6.f * zoom, (hdrH - fontSize) * 0.5f);
    dl->AddText(titlePos, IM_COL32(255, 255, 255, 255), state.name.c_str());
    const char* tag = is2D ? "2D" : "1D";
    float tagW = ImGui::CalcTextSize(tag).x;
    dl->AddText({ topLeft.x + nw - tagW - 6.f * zoom, topLeft.y + (hdrH - fontSize) * 0.5f },
                IM_COL32(235, 225, 255, 220), tag);

    ImVec2 dtl, dbr;
    blendDiagramRect(state, origin, dtl, dbr);

    // Diagram background.
    dl->AddRectFilled(dtl, dbr, IM_COL32(28, 28, 34, 255), 4.f * zoom);
    dl->AddRect(dtl, dbr, IM_COL32(90, 90, 110, 180), 4.f * zoom);

    auto mapX = [&](float v) -> float {
        float r0 = state.blendRangeXMin, r1 = state.blendRangeXMax;
        if (r1 - r0 < 1e-5f) r1 = r0 + 1.f;
        return dtl.x + (v - r0) / (r1 - r0) * (dbr.x - dtl.x);
    };
    auto mapY = [&](float v) -> float {
        float r0 = state.blendRangeYMin, r1 = state.blendRangeYMax;
        if (r1 - r0 < 1e-5f) r1 = r0 + 1.f;
        return dbr.y - (v - r0) / (r1 - r0) * (dbr.y - dtl.y); // y grows upward
    };
    auto evalVar = [&](const std::string& name) -> float {
        // A scrubbed preview value overrides the authored default so the marker
        // tracks the live blend parameter while the user drags the slider.
        auto pv = blendPreviewValues.find(name);
        if (pv != blendPreviewValues.end()) return pv->second;
        for (const auto& v : graph.variables)
            if (v.name == name) return v.defaultValue;
        return 0.f;
    };

    if (is2D)
    {
        float midX = mapX((state.blendRangeXMin + state.blendRangeXMax) * 0.5f);
        float midY = mapY((state.blendRangeYMin + state.blendRangeYMax) * 0.5f);
        dl->AddLine({ midX, dtl.y }, { midX, dbr.y }, IM_COL32(70, 70, 80, 200));
        dl->AddLine({ dtl.x, midY }, { dbr.x, midY }, IM_COL32(70, 70, 80, 200));
    }
    else
    {
        float midY = (dtl.y + dbr.y) * 0.5f;
        dl->AddLine({ dtl.x, midY }, { dbr.x, midY }, IM_COL32(80, 80, 95, 220));
    }

    // Current parameter position marker (uses the variable's default value).
    {
        ImU32 markerCol = IM_COL32(120, 220, 160, 200);
        float px = evalVar(state.blendParamX);
        if (is2D)
        {
            float py = evalVar(state.blendParamY);
            float sx = mapX(px), sy = mapY(py);
            dl->AddLine({ sx, dtl.y }, { sx, dbr.y }, markerCol);
            dl->AddLine({ dtl.x, sy }, { dbr.x, sy }, markerCol);
        }
        else
        {
            float sx = mapX(px);
            dl->AddLine({ sx, dtl.y }, { sx, dbr.y }, markerCol);
        }
    }

    // Each blend child as a labelled dot.
    for (const auto& child : state.blendChildren)
    {
        AnimState* linked = graph.findState(child.stateId);
        float cx = mapX(is2D ? child.posX : child.threshold);
        float cy = is2D ? mapY(child.posY) : (dtl.y + dbr.y) * 0.5f;
        ImU32 dotCol = linked ? IM_COL32(255, 180, 80, 255) : IM_COL32(200, 90, 90, 255);
        dl->AddCircleFilled({ cx, cy }, 5.f * zoom, dotCol);
        dl->AddCircle({ cx, cy }, 5.f * zoom, IM_COL32(20, 20, 20, 220), 0, 1.5f);

        std::string label = linked ? linked->name : std::string("(none)");
        ImVec2 ls = ImGui::CalcTextSize(label.c_str());
        dl->AddText({ cx - ls.x * 0.5f, cy + 7.f * zoom }, IM_COL32(220, 220, 220, 220), label.c_str());
    }

    if (state.blendChildren.empty())
    {
        const char* hint = "Add motions in Inspector";
        ImVec2 hs = ImGui::CalcTextSize(hint);
        dl->AddText({ (dtl.x + dbr.x) * 0.5f - hs.x * 0.5f, (dtl.y + dbr.y) * 0.5f - hs.y * 0.5f },
                    IM_COL32(140, 140, 140, 220), hint);
    }

    // Input (left) and output (right) pins so a blend tree behaves like a state.
    ImVec2 inPos  = getInputPinPos(state, origin);
    ImVec2 outPos = getOutputPinPos(state, origin);
    dl->AddCircleFilled(inPos, pinR, IM_COL32(100, 180, 255, 255));
    dl->AddCircle(inPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
    dl->AddCircleFilled(outPos, pinR, IM_COL32(255, 180, 80, 255));
    dl->AddCircle(outPos, pinR, IM_COL32(200, 200, 200, 180), 0, 1.5f);
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
        if (ImGui::MenuItem("Add Blend Tree"))
        {
            AnimState st;
            st.id        = graph.newNodeId();
            st.kind      = AnimStateKind::BlendTree;
            st.name      = "Blend Tree";
            st.blendType = AnimBlendType::OneD;
            st.posX      = contextMenuPos.x;
            st.posY      = contextMenuPos.y;

            // Preselect the first float variables so the node is usable at once.
            for (const auto& v : graph.variables)
            {
                if (v.type != AnimVariableType::Float) continue;
                if (st.blendParamX.empty()) st.blendParamX = v.name;
                else                        { st.blendParamY = v.name; break; }
            }
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

    // Any State: a fixed, source-only node. It can be neither renamed nor deleted.
    if (state->isAnyState())
    {
        ImGui::TextUnformatted("Animator Any State");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "   (id %d)", state->id);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "This node is always present and cannot be deleted or renamed. "
            "Drag from its output pin to a state (or a blend tree) to create a "
            "transition that can fire from any state while its conditions are met.");
        ImGui::Spacing();
        ImGui::TextDisabled("Any State has an output pin only (no input).");
        return;
    }

    // Blend tree: dedicated editor for the 1D / 2D motion layout.
    if (state->isBlendTree())
    {
        drawBlendTreeInspector(state);
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
    propLabelLeft("Name");
    if (isDefaultState)
        ImGui::BeginDisabled();
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
    {
        state->name = nameBuf;
        graph.dirty = true;
    }
    if (isDefaultState)
        ImGui::EndDisabled();

    // Assign animation via a picker window (same pattern as "Select Texture").
    propLabelLeft("Animation");
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
    propLabelLeft("Speed");
    if (ImGui::DragFloat("##speed", &state->speed, 0.05f, 0.0f, 10.0f))
        graph.dirty = true;

    // Loop
    propLabelLeft("Loop");
    if (ImGui::Checkbox("##loop", &state->loop))
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

    // Per-region influence over the mesh's authored bone-mask groups.
    drawMaskWeightsSection(state);

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
// Blend tree inspector (shown in the Inspector panel for the selected node)
// ===========================================================================

// ===========================================================================
// Embedded blend-tree preview
//
// A self-contained offscreen 3D view (own world/scene/camera/animator) shown
// inside the blend-tree Inspector, so a blend tree can be authored and scrubbed
// without entering the Game panel's Play mode. It re-uses kblend::weights2D so
// the previewed pose matches what the runtime will produce.
// ===========================================================================

PanelAnimator::~PanelAnimator()
{
    releasePreviewMesh();
    delete previewRenderer; previewRenderer = nullptr;
    delete previewCamera;   previewCamera = nullptr;
    delete previewWorld;    previewWorld = nullptr; // owns previewScene
    previewScene = nullptr;
}

void PanelAnimator::ensurePreviewScene()
{
    if (previewRenderer)
        return;

    previewRenderer = new kOffscreenRenderer(512, 512);
    if (manager)
        previewRenderer->setAssetManager(manager->getAssetManager());

    previewWorld = createWorld(createAssetManager());
    previewScene = previewWorld->createScene("blendPreview");
    previewScene->setFrustumCullingEnabled(false);
    previewScene->setShadowsEnabled(false);
    previewScene->setAmbientLightColor(kVec3(0.18f, 0.18f, 0.18f));

    kLight *sun = previewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.4f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    if (sun) sun->setPower(1.4f);

    previewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    previewCamera->setFOV(45.0f);
    previewCamera->setAspectRatio(1.0f);
    previewCamera->setNearClip(0.01f);
    previewCamera->setFarClip(1000.0f);
    previewCamera->setLookAt(kVec3(0.0f));
    previewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelAnimator::releasePreviewMesh()
{
    if (previewScene && previewMesh)
        previewScene->removeMesh(previewMesh);
    previewMesh = nullptr;
    previewMeshUuid.clear();

    // Clips are created by kAssetManager::loadAnimation (it returns a fresh
    // object), so the preview owns and frees them.
    for (auto &kv : previewClips)
        delete kv.second;
    previewClips.clear();

    // Masks are rebuilt for the newly selected base mesh.
    previewMaskGroups.clear();
    previewRestOfBodyMask.reset();
    previewHasMasks = false;

    delete previewAnimator; previewAnimator = nullptr;
    delete previewMat;      previewMat = nullptr;
    previewBlendTime = 0.0f;
}

void PanelAnimator::refreshPreviewMeshes()
{
    previewMeshUuids.clear();
    previewMeshNames.clear();
    if (!manager)
        return;

    // Every mesh asset in the project, sorted by asset path so the list is
    // stable and searchable at a glance.
    std::vector<std::pair<std::string, std::string>> meshes; // (uuid, asset path)
    for (const auto &kv : manager->fileMap)
    {
        if (kv.second.type != "mesh")
            continue;
        meshes.emplace_back(kv.first, kv.second.path.empty() ? kv.first : kv.second.path);
    }
    if (meshes.empty())
    {
        previewMeshChoice = 0;
        return;
    }

    std::sort(meshes.begin(), meshes.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });
    for (const auto &m : meshes)
    {
        previewMeshUuids.push_back(m.first);
        previewMeshNames.push_back(m.second);
    }

    // On first population, prefer the base (driven) skeletal mesh — the asset
    // that authors the bone masks — so the preview shows that model playing
    // back the animator's blend. Fall back to the mesh the clips were authored
    // against (guarantees the clip bones bind to the model's bones).
    if (previewMeshUuid.empty())
    {
        std::string preferred = resolveBaseMeshUuid();
        if (preferred.empty())
        {
            for (const auto &st : graph.states)
            {
                if (st.animationUuid.empty())
                    continue;
                fs::path animPath = manager->findAssetPathByUuid(st.animationUuid);
                if (animPath.empty() || !fs::exists(animPath))
                    continue;
                json j;
                try
                {
                    std::ifstream f(animPath);
                    if (!f.is_open()) continue;
                    f >> j;
                }
                catch (...) { continue; }
                preferred = j.value("meshUuid", std::string());
                if (!preferred.empty())
                    break;
            }
        }
        if (!preferred.empty())
        {
            auto it = std::find(previewMeshUuids.begin(), previewMeshUuids.end(), preferred);
            if (it != previewMeshUuids.end())
                previewMeshChoice = (int)std::distance(previewMeshUuids.begin(), it);
        }
    }

    if (previewMeshChoice >= (int)previewMeshUuids.size())
        previewMeshChoice = 0;
}

void PanelAnimator::ensurePreviewClips()
{
    if (!manager || !previewMesh)
        return;

    kAssetManager *am = manager->getAssetManager();
    if (!am)
        return;

    for (const auto &st : graph.states)
    {
        if (st.animationUuid.empty())
            continue;
        if (previewClips.count(st.animationUuid))
            continue;

        fs::path animPath = manager->findAssetPathByUuid(st.animationUuid);
        if (animPath.empty() || !fs::exists(animPath))
            continue;

        json j;
        try
        {
            std::ifstream f(animPath);
            if (!f.is_open()) continue;
            f >> j;
        }
        catch (...) { continue; }

        std::string meshUuid = j.value("meshUuid", std::string());
        if (meshUuid.empty())
            continue;

        fs::path glbPath = manager->projectPath / "Library" / "ImportedAssets" / (meshUuid + ".glb");
        if (!fs::exists(glbPath))
            continue;

        try
        {
            kSkeletalAnimation *clip = am->loadAnimation(glbPath.generic_string(), previewMesh);
            if (clip)
            {
                // The preview drives clip time itself; keep the engine from
                // advancing it a second time.
                clip->setSpeed(0.0f);

                // Root-motion channels from the .animation asset (parity with
                // the runtime controller, so the previewed pose matches).
                clip->setRootMotionRotation(j.value("rootMotionRotation", false));
                clip->setRootMotionPositionY(j.value("rootMotionPositionY", false));
                clip->setRootMotionPositionXZ(j.value("rootMotionPositionXZ", false));

                // Compensate for unit-scale differences between the base mesh
                // being previewed and the animation's own source mesh. Both are
                // imported independently; without this the base mesh (imported
                // at e.g. scale 0.02) bound to a clip imported at scale 1.0 has
                // wildly wrong translations and the model explodes out of view.
                const float baseScale = meshScaleFactor(previewMeshUuid);
                const float animScale = meshScaleFactor(meshUuid);
                if (baseScale > 0.0f && animScale > 0.0f)
                {
                    const float ratio = baseScale / animScale;
                    if (std::fabs(ratio - 1.0f) > 1e-5f)
                        clip->applyTranslationScale(ratio);
                }

                previewClips[st.animationUuid] = clip;
            }
        }
        catch (const std::exception &)
        {
            // Static skinned mesh with no clips — nothing to preview.
        }
    }
}

void PanelAnimator::framePreviewCamera()
{
    if (!previewMesh || !previewCamera)
        return;

    kAABB combined;
    std::function<void(kMesh *)> expand = [&](kMesh *m)
    {
        m->calculateModelMatrix();
        kAABB b = m->getWorldAABB();
        if (b.isValid()) { combined.expandBy(b.min); combined.expandBy(b.max); }
        for (kObject *c : m->getChildren())
            if (c->getType() == NODE_TYPE_MESH)
                expand(static_cast<kMesh *>(c));
    };
    expand(previewMesh);

    previewCenter = combined.isValid() ? combined.center() : kVec3(0.0f);
    kVec3 he = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
    float radius = glm::length(he);
    if (radius < 0.001f) radius = 1.0f;

    previewCamDist = (radius / glm::tan(glm::radians(22.5f))) * 1.15f;
    previewCamera->setNearClip(std::max(0.0001f, previewCamDist * 0.01f));
    previewCamera->setFarClip(previewCamDist * 100.0f);
}

void PanelAnimator::drawBlendPreview(AnimState* state)
{
    if (!state || !manager)
        return;

    ensurePreviewScene();

    // Refresh the candidate mesh list only when the graph's clip set changes,
    // so this does not re-read the .animation files every frame.
    std::string sig;
    for (const auto &st : graph.states)
        sig += st.animationUuid + "|";
    if (sig != previewSig)
    {
        previewSig = sig;
        refreshPreviewMeshes();
    }

    ImGui::TextUnformatted("Preview Model");
    ImGui::Separator();

    if (previewMeshNames.empty())
    {
        ImGui::TextDisabled("No mesh found for this animator's clips.");
        return;
    }

    std::vector<const char *> names;
    names.reserve(previewMeshNames.size());
    for (const auto &n : previewMeshNames)
        names.push_back(n.c_str());

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##blendpreviewmesh", &previewMeshChoice, names.data(), (int)names.size()))
        releasePreviewMesh(); // reload on the next frame

    const std::string chosen = previewMeshUuids.empty()
        ? std::string()
        : previewMeshUuids[std::min((size_t)previewMeshChoice, previewMeshUuids.size() - 1)];

    if (!chosen.empty() && chosen != previewMeshUuid)
    {
        releasePreviewMesh();

        kAssetManager *am = manager->getAssetManager();
        fs::path glbPath = manager->projectPath / "Library" / "ImportedAssets" / (chosen + ".glb");
        if (am && fs::exists(glbPath))
        {
            previewMesh = am->loadMesh(glbPath.generic_string());
            if (previewMesh)
            {
                previewMeshUuid = chosen;

                // Fallback material for submeshes whose GLB shipped none.
                kShader *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
                if (shader)
                {
                    previewMat = new kMaterial();
                    previewMat->setShader(shader);
                    previewMat->setDiffuseColor(kVec3(0.82f, 0.82f, 0.82f));
                    previewMat->setAmbientColor(kVec3(0.30f, 0.30f, 0.30f));
                    previewMat->setSpecularColor(kVec3(0.20f, 0.20f, 0.20f));
                    previewMat->setShininess(16.0f);

                    std::function<void(kMesh *)> applyMat = [&](kMesh *m)
                    {
                        kMaterial *existing = m->getMaterial();
                        if (!existing || !existing->getShader() ||
                            existing->getShader()->getShaderProgram() == 0)
                            m->setMaterial(previewMat, false); // false = don't auto-apply to children
                        for (kObject *c : m->getChildren())
                            if (c->getType() == NODE_TYPE_MESH)
                                applyMat(static_cast<kMesh *>(c));
                    };
                    applyMat(previewMesh);
                }

                // One animator shared by every bone-bearing submesh.
                previewAnimator = new kAnimator(nullptr);
                std::function<void(kMesh *)> applyAnim = [&](kMesh *m)
                {
                    if (m->getBoneCount() > 0)
                        m->setAnimator(previewAnimator);
                    for (kObject *c : m->getChildren())
                        if (c->getType() == NODE_TYPE_MESH)
                            applyAnim(static_cast<kMesh *>(c));
                };
                applyAnim(previewMesh);

                previewScene->addMesh(previewMesh);
                ensurePreviewClips();
                rebuildPreviewMasks();
                framePreviewCamera();
                previewBlendTime = 0.0f;
            }
        }
    }

    if (!previewMesh || !previewAnimator || previewClips.empty())
    {
        // Render the bare mesh so the user can still frame the model.
        if (previewMesh && previewCamera && previewRenderer)
        {
            float pr = glm::radians(previewRotX);
            float yr = glm::radians(previewRotY);
            kVec3 camDir(std::cos(pr) * std::sin(yr), std::sin(pr), std::cos(pr) * std::cos(yr));
            previewCamera->setPosition(previewCenter + camDir * previewCamDist);
            previewCamera->setLookAt(previewCenter);
            previewRenderer->setBackgroundColor(kVec4(0.16f, 0.16f, 0.16f, 1.0f));
            previewRenderer->renderMesh(previewMesh, previewCamera);

            const float sz = std::max(120.0f, std::min(previewSize, ImGui::GetContentRegionAvail().x));
            ImTextureRef tex((ImTextureID)(uintptr_t)previewRenderer->getTexture());
            ImGui::Image(tex, ImVec2(sz, sz), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            ImGui::TextDisabled("Loading preview…");
        }
        return;
    }

    // ---- Blend parameter (uses the scrubbed preview value when present) ----
    auto varDefault = [&](const std::string &name) -> float
    {
        auto pv = blendPreviewValues.find(name);
        if (pv != blendPreviewValues.end()) return pv->second;
        for (const auto &v : graph.variables)
            if (v.name == name) return v.defaultValue;
        return 0.0f;
    };
    const float paramX = varDefault(state->blendParamX);
    const float paramY = varDefault(state->blendParamY);

    // ---- Gather playable motions ------------------------------------------
    struct PMotion
    {
        kSkeletalAnimation *clip = nullptr;
        float threshold = 0.0f;
        float posX = 0.0f;
        float posY = 0.0f;
        float speed = 1.0f;
        std::vector<AnimMaskWeight> maskWeights;
    };
    std::vector<PMotion> motions;
    for (const auto &c : state->blendChildren)
    {
        AnimState *linked = graph.findState(c.stateId);
        if (!linked) continue;
        auto it = previewClips.find(linked->animationUuid);
        if (it == previewClips.end() || !it->second) continue;

        PMotion m;
        m.clip = it->second;
        m.threshold = c.threshold;
        m.posX = c.posX;
        m.posY = c.posY;
        m.speed = c.speed;
        // Per-motion masks; fall back to the node's own weights when the motion
        // defines none (matches the runtime resolution).
        m.maskWeights = !c.maskWeights.empty() ? c.maskWeights : state->maskWeights;
        motions.push_back(m);
    }

    // ---- Weights (identical math to the runtime) --------------------------
    std::vector<float> weights(motions.size(), 0.0f);
    if (motions.size() == 1)
    {
        weights[0] = 1.0f;
    }
    else if (motions.size() > 1)
    {
        if (state->blendType == AnimBlendType::TwoD)
        {
            std::vector<kblend::Point2> pts;
            pts.reserve(motions.size());
            for (const auto &m : motions)
                pts.emplace_back(m.posX, m.posY);
            weights = kblend::weights2D(pts, paramX, paramY);
        }
        else
        {
            std::vector<size_t> order(motions.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return motions[a].threshold < motions[b].threshold;
            });
            if (paramX <= motions[order.front()].threshold)
                weights[order.front()] = 1.0f;
            else if (paramX >= motions[order.back()].threshold)
                weights[order.back()] = 1.0f;
            else
                for (size_t i = 0; i + 1 < order.size(); ++i)
                {
                    const float t0 = motions[order[i]].threshold;
                    const float t1 = motions[order[i + 1]].threshold;
                    if (paramX >= t0 && paramX <= t1)
                    {
                        const float f = (t1 - t0 > 1e-6f) ? (paramX - t0) / (t1 - t0) : 0.0f;
                        weights[order[i]] = 1.0f - f;
                        weights[order[i + 1]] = f;
                        break;
                    }
                }
        }
    }

    // ---- Advance clip time and build the weighted samples -----------------
    previewBlendTime += ImGui::GetIO().DeltaTime;

    // Emit a motion's weighted samples using the same region-participation
    // semantics as the runtime (see appendMaskedSamples): a group weight of 0
    // contributes nothing for that region — neither its clip nor a rest pose —
    // so the region is driven by the other motions that give it a positive
    // weight instead of collapsing to the bind pose. Regions no group covers
    // stay fully driven.
    // Small per-region coverage floor, mirroring the runtime: a motion with mask
    // weight 0 contributes nothing, but a tiny floor keeps a region whose
    // weighted motions momentarily reach ~0 driven (and hands ownership over
    // continuously) rather than snapping to the bind pose.
    const float kMaskCoverageFloor = 1e-3f;

    auto appendSamples = [&](const PMotion &m, float baseWeight, float ticks,
                             std::vector<kPoseSample> &out)
    {
        const bool useMasks = previewHasMasks && !m.maskWeights.empty();
        if (!useMasks)
        {
            kPoseSample s;
            s.animation = m.clip;
            s.time      = ticks;
            s.weight    = baseWeight;
            out.push_back(s);
            return;
        }

        if (previewRestOfBodyMask)
        {
            kPoseSample s;
            s.animation = m.clip;
            s.time      = ticks;
            s.weight    = baseWeight;
            s.mask      = previewRestOfBodyMask.get();
            out.push_back(s);
        }

        // One sample per authored group; unlisted groups default to full weight.
        for (const auto &gp : previewMaskGroups)
        {
            if (!gp.second)
                continue;
            const kAnimationMask *mask = gp.second.get();

            float gw = 1.0f;
            for (const auto &mw : m.maskWeights)
                if (mw.maskName == gp.first)
                { gw = std::max(0.0f, std::min(1.0f, mw.weight)); break; }

            // 0 means the motion does not animate the region; a positive weight
            // contributes the clip plus the coverage floor.
            if (gw <= 1e-5f)
                continue;

            const float w = baseWeight * gw + kMaskCoverageFloor * gw;
            if (w <= 1e-5f)
                continue;

            kPoseSample s;
            s.animation = m.clip;
            s.time      = ticks;
            s.weight    = w;
            s.mask      = mask;
            out.push_back(s);
        }
    };

    // Every motion contributes its samples (including ones whose blend weight is
    // 0 at this instant) so a region can stay driven by a covering motion through
    // the coverage floor as ownership hands over.
    std::vector<kPoseSample> samples;
    for (size_t i = 0; i < motions.size(); ++i)
    {
        kSkeletalAnimation *clip = motions[i].clip;
        const float tps = clip->getTicksPerSecond();
        const float durSec = (tps > 1e-3f) ? (clip->getDuration() / tps) : 0.0f;

        float sec = previewBlendTime * motions[i].speed;
        if (durSec > 1e-4f)
            sec = std::fmod(sec, durSec);

        appendSamples(motions[i], weights[i], sec * tps, samples);
    }

    if (!samples.empty())
    {
        // Root motion follows the highest-weight motion that has a root-motion
        // channel (not the dominant one), and the source is switched without
        // resetting the tracker so the preview neither stalls nor pops. Derived
        // from the motion blend weights so coverage-floor samples never steer it.
        kSkeletalAnimation *dominant     = nullptr;
        kSkeletalAnimation *rootClip     = nullptr;
        float               dominantTime = 0.0f;
        float               rootTime     = 0.0f;
        float               bestW        = -1.0f;
        float               bestRootW    = -1.0f;
        for (size_t i = 0; i < motions.size(); ++i)
        {
            if (weights[i] <= 1e-5f) continue;
            kSkeletalAnimation *clip = motions[i].clip;
            const float tps = clip->getTicksPerSecond();
            const float durSec = (tps > 1e-3f) ? (clip->getDuration() / tps) : 0.0f;
            float sec = previewBlendTime * motions[i].speed;
            if (durSec > 1e-4f) sec = std::fmod(sec, durSec);
            const float t = sec * tps;

            if (weights[i] > bestW)
            {
                bestW        = weights[i];
                dominant     = clip;
                dominantTime = t;
            }
            const bool hasRoot = clip->getRootMotionRotation() ||
                                 clip->getRootMotionPositionY() ||
                                 clip->getRootMotionPositionXZ();
            if (hasRoot && weights[i] > bestRootW)
            {
                bestRootW = weights[i];
                rootClip  = clip;
                rootTime  = t;
            }
        }
        if (rootClip != nullptr)
            previewAnimator->setBlendRootSource(rootClip, rootTime);
        else if (dominant != nullptr)
            previewAnimator->setBlendRootSource(dominant, dominantTime);

        // Any sample's clip resolves the shared skeleton root; rest-pose
        // samples (weighted masking) carry no animation and are skipped.
        kSkeletalAnimation *poseSource = nullptr;
        for (const kPoseSample &s : samples)
            if (s.animation != nullptr) { poseSource = s.animation; break; }

        try
        {
            if (poseSource != nullptr)
            {
                const kNodeData &root = poseSource->getRootNode();
                previewAnimator->calculateBlendedBoneTransform(samples, &root, kMat4(1.0f));
            }
        }
        catch (const std::exception &)
        {
            // Keep the last successfully computed pose.
        }
    }

    // ---- Camera orbit + offscreen render ----------------------------------
    {
        float pr = glm::radians(previewRotX);
        float yr = glm::radians(previewRotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr), std::sin(pr), std::cos(pr) * std::cos(yr));
        previewCamera->setPosition(previewCenter + camDir * previewCamDist);
        previewCamera->setLookAt(previewCenter);
    }

    previewRenderer->setBackgroundColor(kVec4(0.16f, 0.16f, 0.16f, 1.0f));
    if (previewLightOn)
        previewRenderer->render(previewWorld, previewScene, previewCamera);
    else
        previewRenderer->renderMesh(previewMesh, previewCamera);

    // ---- Image + navigation -----------------------------------------------
    ImGui::Spacing();
    const float sz = std::max(120.0f, std::min(previewSize, ImGui::GetContentRegionAvail().x));
    ImTextureRef tex((ImTextureID)(uintptr_t)previewRenderer->getTexture());
    ImGui::Image(tex, ImVec2(sz, sz), ImVec2(0, 1), ImVec2(1, 0));

    if (ImGui::IsItemHovered())
    {
        ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            previewRotY -= io.MouseDelta.x * 0.4f;
            previewRotX += io.MouseDelta.y * 0.4f;
            previewRotX = std::max(-89.0f, std::min(89.0f, previewRotX));
        }
        if (io.MouseWheel != 0.0f)
        {
            previewCamDist *= (1.0f - io.MouseWheel * 0.1f);
            previewCamDist = std::max(0.05f, previewCamDist);
        }
    }

    propLabelLeft("Lit");
    ImGui::Checkbox("##lit", &previewLightOn);
    ImGui::SameLine();
    if (ImGui::Button("Reset View"))
    {
        previewRotX = 20.0f;
        previewRotY = 30.0f;
        framePreviewCamera();

        // Reset every previewed blend parameter back to 0 — both the sliders
        // (kept in blendPreviewValues) and any running controller.
        for (auto &kv : blendPreviewValues)
        {
            kv.second = 0.0f;
            if (manager != nullptr)
                manager->setRuntimeAnimatorVariable(graph.uuid, kv.first, 0.0f);
        }
        auto zeroParam = [&](const std::string &name)
        {
            if (name.empty()) return;
            blendPreviewValues[name] = 0.0f;
            if (manager != nullptr)
                manager->setRuntimeAnimatorVariable(graph.uuid, name, 0.0f);
        };
        zeroParam(state->blendParamX);
        if (state->blendType == AnimBlendType::TwoD)
            zeroParam(state->blendParamY);
    }
    ImGui::TextDisabled("Drag to orbit, scroll to zoom.");
}

// ===========================================================================
// Bone-mask group weights
//
// A state, blend tree or individual blend-tree motion can weight each named
// bone-mask group authored on the *base* (driven) mesh. The groups themselves
// live on the base mesh asset (Mesh Inspector → Bone Masks); here we only pick
// which of them this node/motion references and how strongly it drives them.
// ===========================================================================

std::string PanelAnimator::resolveBaseMeshUuid() const
{
    if (!manager)
        return std::string();

    // Prefer the mesh chosen for the embedded preview when it carries masks:
    // that selection *is* the base skeletal mesh the user is authoring against.
    if (!previewMeshUuid.empty() && !manager->getMeshMaskGroups(previewMeshUuid).empty())
        return previewMeshUuid;

    // Otherwise: the first mesh asset that authors mask groups. Bone masks live
    // on the driven mesh, never on the animation mesh, so this reliably finds
    // the base skeletal mesh for the rig.
    for (const auto& kv : manager->fileMap)
    {
        if (kv.second.type != "mesh")
            continue;
        if (!manager->getMeshMaskGroups(kv.first).empty())
            return kv.first;
    }

    // Last resort: whatever mesh is loaded in the preview (may be empty).
    return previewMeshUuid;
}

void PanelAnimator::collectBaseMeshMaskGroupNames(std::vector<std::string>& names) const
{
    names.clear();
    if (!manager)
        return;
    const std::string baseUuid = resolveBaseMeshUuid();
    if (baseUuid.empty())
        return;
    for (const auto& g : manager->getMeshMaskGroups(baseUuid))
        names.push_back(g.name);
}

float PanelAnimator::meshScaleFactor(const std::string& uuid) const
{
    if (!manager || uuid.empty())
        return 1.0f;

    fs::path metaPath = manager->projectPath / "Library" / "Metadata" / (uuid + ".json");
    if (!fs::exists(metaPath))
        return 1.0f;

    try
    {
        std::ifstream mf(metaPath);
        json mj;
        mf >> mj;
        return mj.value("scaleFactor", 1.0f);
    }
    catch (...)
    {
    }
    return 1.0f;
}

void PanelAnimator::rebuildPreviewMasks()
{
    previewMaskGroups.clear();
    previewRestOfBodyMask.reset();
    previewHasMasks = false;

    if (!manager)
        return;

    const std::string baseUuid = resolveBaseMeshUuid();
    if (baseUuid.empty())
        return;

    std::vector<BoneMaskGroup> authored = manager->getMeshMaskGroups(baseUuid);
    if (authored.empty())
        return;

    std::vector<std::string> groupedBones;
    for (const auto& g : authored)
    {
        auto mask = std::make_unique<kAnimationMask>(g.name);
        mask->buildFromBoneNames(g.bones, true);
        if (mask->empty())
            continue; // group named no bone actually present on the rig

        for (const auto& b : g.bones)
            groupedBones.push_back(b);
        previewMaskGroups[g.name] = std::move(mask);
    }
    if (previewMaskGroups.empty())
        return;

    // Complement mask so bones no group owns are still driven at full strength.
    if (previewMesh)
    {
        auto rest = std::make_unique<kAnimationMask>("__ungrouped");
        rest->buildFromMesh(previewMesh, true);
        for (const auto& b : groupedBones)
            rest->setBoneEnabled(b, false);
        previewRestOfBodyMask = std::move(rest);
    }

    previewHasMasks = true;
}

void PanelAnimator::drawMaskWeightList(const std::vector<std::string>& groups,
                                       std::vector<AnimMaskWeight>& weights)
{
    int removeIdx = -1;
    for (int i = 0; i < (int)weights.size(); ++i)
    {
        AnimMaskWeight& mw = weights[i];
        ImGui::PushID(i);

        const bool known = std::find(groups.begin(), groups.end(), mw.maskName) != groups.end();
        std::string label = mw.maskName;
        if (!known)
            label += "  (missing)";
        ImGui::TextUnformatted(label.c_str());

        ImGui::SetNextItemWidth(-34.0f);
        if (ImGui::SliderFloat("##weight", &mw.weight, 0.0f, 1.0f, "%.2f"))
            graph.dirty = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            removeIdx = i;

        ImGui::PopID();
    }

    if (removeIdx >= 0)
    {
        weights.erase(weights.begin() + removeIdx);
        graph.dirty = true;
    }

    // Add a group this node / motion does not reference yet.
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##addmask", "Add Mask Group"))
    {
        bool any = false;
        for (const auto& g : groups)
        {
            bool already = false;
            for (const auto& mw : weights)
                if (mw.maskName == g) { already = true; break; }
            if (already)
                continue;

            any = true;
            if (ImGui::Selectable(g.c_str()))
            {
                AnimMaskWeight mw;
                mw.maskName = g;
                mw.weight   = 1.0f;
                weights.push_back(mw);
                graph.dirty = true;
            }
        }
        if (!any)
            ImGui::TextDisabled("All groups added");
        ImGui::EndCombo();
    }
}

void PanelAnimator::drawMaskWeightsSection(AnimState* state)
{
    if (!state)
        return;

    ImGui::Spacing();
    ImGui::TextUnformatted("Mask Weights");
    ImGui::Separator();

    std::vector<std::string> groups;
    collectBaseMeshMaskGroupNames(groups);
    if (groups.empty())
    {
        ImGui::TextDisabled("No bone masks on the base mesh.");
        ImGui::TextWrapped(
            "Author groups in the Mesh Inspector (Import Settings > Bone Masks) "
            "on the base (driven) mesh, then weight them here.");
        return;
    }

    ImGui::PushID("nodemasks");
    drawMaskWeightList(groups, state->maskWeights);
    ImGui::PopID();
}

void PanelAnimator::drawBlendTreeInspector(AnimState* state)
{
    if (!state) return;

    ImGui::TextUnformatted("Animator Blend Tree");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "   (id %d)", state->id);
    ImGui::Separator();
    ImGui::Spacing();

    // Fields use a left-aligned caption with the input on the same line
    // (propLabelLeft): captions stay inside the panel instead of being drawn
    // to the right of, or above, the widget.
    char nameBuf[128];
    strncpy_s(nameBuf, state->name.c_str(), sizeof(nameBuf));
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    propLabelLeft("Name");
    if (ImGui::InputText("##blendname", nameBuf, sizeof(nameBuf)))
    {
        state->name = nameBuf;
        graph.dirty = true;
    }

    // Blend type: a single axis (1D) or a plane (2D).
    const char* blendTypes[] = { "1D", "2D" };
    int bt = (int)state->blendType;
    propLabelLeft("Blend Type");
    if (ImGui::Combo("##blendtype", &bt, blendTypes, IM_ARRAYSIZE(blendTypes)))
    {
        state->blendType = (AnimBlendType)bt;
        graph.dirty = true;
    }

    // Float variables usable as blend parameters.
    std::vector<const char*> floatVars;
    floatVars.push_back("(none)");
    for (const auto& v : graph.variables)
        if (v.type == AnimVariableType::Float) floatVars.push_back(v.name.c_str());

    auto paramIndex = [&](const std::string& name) -> int {
        for (int i = 1; i < (int)floatVars.size(); ++i)
            if (name == floatVars[i]) return i;
        return 0;
    };

    ImGui::Spacing();
    int px = paramIndex(state->blendParamX);
    propLabelLeft("Parameter X");
    if (ImGui::Combo("##paramx", &px, floatVars.data(), (int)floatVars.size()))
    {
        state->blendParamX = (px == 0) ? std::string() : std::string(floatVars[px]);
        graph.dirty = true;
    }

    if (state->blendType == AnimBlendType::TwoD)
    {
        int py = paramIndex(state->blendParamY);
        propLabelLeft("Parameter Y");
        if (ImGui::Combo("##paramy", &py, floatVars.data(), (int)floatVars.size()))
        {
            state->blendParamY = (py == 0) ? std::string() : std::string(floatVars[py]);
            graph.dirty = true;
        }
    }

    // Authoring range for the diagram axes.
    ImGui::Spacing();
    float rangeX[2] = { state->blendRangeXMin, state->blendRangeXMax };
    propLabelLeft("Axis Range X");
    if (ImGui::DragFloat2("##rangex", rangeX, 0.05f))
    {
        state->blendRangeXMin = rangeX[0];
        state->blendRangeXMax = rangeX[1];
        graph.dirty = true;
    }
    if (state->blendType == AnimBlendType::TwoD)
    {
        float rangeY[2] = { state->blendRangeYMin, state->blendRangeYMax };
        propLabelLeft("Axis Range Y");
        if (ImGui::DragFloat2("##rangey", rangeY, 0.05f))
        {
            state->blendRangeYMin = rangeY[0];
            state->blendRangeYMax = rangeY[1];
            graph.dirty = true;
        }
    }

    // -----------------------------------------------------------------------
    // Blending / Timing — the same two groups the transition inspector shows,
    // so a blend tree can declare how its motions are combined and whether it
    // must play for a minimum time before its outgoing transitions may fire.
    // -----------------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted("Blending");
    ImGui::Separator();

    const char* blendModes[] = { "Instant", "Cross Fade" };
    int mode = (int)state->blendMode;
    propLabelLeft("Blend Mode");
    if (ImGui::Combo("##blendtreemode", &mode, blendModes, IM_ARRAYSIZE(blendModes)))
    {
        state->blendMode = (AnimBlendMode)mode;
        graph.dirty = true;
    }

    propLabelLeft("Blend Duration");
    if (ImGui::DragFloat("##blendtreedur", &state->blendDuration, 0.01f, 0.0f, 10.0f, "%.2fs"))
        graph.dirty = true;

    ImGui::Spacing();
    ImGui::TextUnformatted("Timing");
    ImGui::Separator();

    propLabelLeft("Has Exit Time");
    if (ImGui::Checkbox("##hasExitTime", &state->hasExitTime))
        graph.dirty = true;
    if (state->hasExitTime)
    {
        propLabelLeft("Exit Time");
        if (ImGui::DragFloat("##blendtreeexit", &state->exitTime, 0.01f, 0.0f, 100.0f, "%.2fs"))
            graph.dirty = true;
    }

    // Per-region influence over the mesh's authored bone-mask groups.
    drawMaskWeightsSection(state);

    // -----------------------------------------------------------------------
    // Preview — scrub a parameter and watch the blended pose in the embedded
    // 3D view, all without entering the Game panel's Play mode. While playing,
    // the slider also drives any running controller built from this graph; the
    // value is kept locally so previewing never dirties the asset.
    // -----------------------------------------------------------------------
    auto drawParamPreview = [&](const std::string& varName, float lo, float hi)
    {
        if (varName.empty())
        {
            ImGui::TextDisabled("(no parameter)");
            return;
        }
        if (hi - lo < 1e-5f)
            hi = lo + 1.0f;

        ImGui::PushID(varName.c_str());

        float value;
        auto it = blendPreviewValues.find(varName);
        if (it != blendPreviewValues.end())
        {
            value = it->second;
        }
        else
        {
            float def = 0.0f;
            for (const auto& v : graph.variables)
                if (v.name == varName) { def = v.defaultValue; break; }
            value = (manager != nullptr)
                        ? manager->getRuntimeAnimatorVariable(graph.uuid, varName, def)
                        : def;
            blendPreviewValues[varName] = value;
        }

        propLabelLeft(varName.c_str());
        if (ImGui::SliderFloat("##paramval", &value, lo, hi, "%.3f"))
        {
            blendPreviewValues[varName] = value;
            if (manager != nullptr)
                manager->setRuntimeAnimatorVariable(graph.uuid, varName, value);
        }

        ImGui::PopID();
    };

    ImGui::Spacing();
    ImGui::TextUnformatted("Preview");
    ImGui::Separator();
    drawBlendPreview(state);
    ImGui::Spacing();
    drawParamPreview(state->blendParamX, state->blendRangeXMin, state->blendRangeXMax);
    if (state->blendType == AnimBlendType::TwoD)
        drawParamPreview(state->blendParamY, state->blendRangeYMin, state->blendRangeYMax);

    // Motions: each drives an existing animation state.
    ImGui::Spacing();
    ImGui::TextUnformatted(state->blendType == AnimBlendType::TwoD
                               ? "Motions (each has its own X / Y)"
                               : "Motions (ordered by threshold)");
    ImGui::Separator();

    std::vector<int>         candIds;
    std::vector<std::string> candNames;
    for (const auto& s : graph.states)
    {
        if (!s.isState()) continue;
        candIds.push_back(s.id);
        candNames.push_back(s.name.empty() ? ("State " + std::to_string(s.id)) : s.name);
    }

    if (state->blendChildren.empty())
        ImGui::TextDisabled("No motions yet. Add one below.");

    int removeIdx = -1;
    for (int i = 0; i < (int)state->blendChildren.size(); ++i)
    {
        AnimBlendChild& child = state->blendChildren[i];
        ImGui::PushID(i);
        ImGui::Separator();

        // Linked-state combo.
        int cur = 0;
        std::vector<const char*> labels;
        labels.push_back("(none)");
        for (size_t c = 0; c < candNames.size(); ++c)
        {
            labels.push_back(candNames[c].c_str());
            if (candIds[c] == child.stateId) cur = (int)c + 1;
        }

        propLabelLeft("State");
        if (ImGui::Combo("##motionstate", &cur, labels.data(), (int)labels.size()))
        {
            child.stateId = (cur == 0) ? -1 : candIds[cur - 1];
            graph.dirty = true;
        }

        if (state->blendType == AnimBlendType::TwoD)
        {
            float xy[2] = { child.posX, child.posY };
            propLabelLeft("Position (X, Y)");
            if (ImGui::DragFloat2("##motionpos", xy, 0.05f))
            {
                child.posX = xy[0];
                child.posY = xy[1];
                graph.dirty = true;
            }
        }
        else
        {
            propLabelLeft("Threshold");
            if (ImGui::DragFloat("##motionthresh", &child.threshold, 0.05f))
                graph.dirty = true;
        }

        propLabelLeft("Speed");
        if (ImGui::DragFloat("##motionspeed", &child.speed, 0.05f, 0.0f, 10.0f))
            graph.dirty = true;

        // Per-motion Mask Weights. The groups come from the *base* mesh (the
        // driven skeletal mesh), never the animation mesh, so each motion can
        // drive a body region independently of the others at playback time.
        ImGui::Spacing();
        ImGui::TextUnformatted("Mask Weights");
        ImGui::Separator();
        {
            std::vector<std::string> maskGroups;
            collectBaseMeshMaskGroupNames(maskGroups);
            if (maskGroups.empty())
            {
                ImGui::TextDisabled("No bone masks on the base mesh.");
                ImGui::TextWrapped(
                    "Author groups in the Mesh Inspector (Import Settings > "
                    "Bone Masks) on the base (driven) mesh.");
            }
            else
            {
                drawMaskWeightList(maskGroups, child.maskWeights);
            }
        }

        if (ImGui::Button("Remove Motion"))
            removeIdx = i;

        ImGui::PopID();
    }

    if (removeIdx >= 0)
    {
        state->blendChildren.erase(state->blendChildren.begin() + removeIdx);
        graph.dirty = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Add Motion", ImVec2(-1, 0)))
    {
        AnimBlendChild child;
        if (!candIds.empty()) child.stateId = candIds[0];
        child.threshold = (float)state->blendChildren.size();
        state->blendChildren.push_back(child);
        graph.dirty = true;
    }

    // Delete the blend tree node (allowed; it is not one of the fixed nodes).
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.24f, 0.24f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.69f, 0.19f, 0.19f, 1.00f));
    if (ImGui::Button("Delete Blend Tree", ImVec2(-1, 0)))
    {
        graph.removeState(state->id);
        graph.dirty   = true;
        selectedState = -1;
    }
    ImGui::PopStyleColor(2);
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
    propLabelLeft("Blend Mode");
    if (ImGui::Combo("##blendmode", &mode, blendModes, IM_ARRAYSIZE(blendModes)))
    {
        trans->blendMode = (AnimBlendMode)mode;
        graph.dirty = true;
    }

    propLabelLeft("Blend Duration");
    if (ImGui::DragFloat("##blendduration", &trans->blendDuration, 0.01f, 0.0f, 10.0f, "%.2fs"))
        graph.dirty = true;

    ImGui::Spacing();
    ImGui::TextUnformatted("Timing");
    ImGui::Separator();

    propLabelLeft("Has Exit Time");
    if (ImGui::Checkbox("##hasExitTime", &trans->hasExitTime))
        graph.dirty = true;
    if (trans->hasExitTime)
    {
        propLabelLeft("Exit Time");
        if (ImGui::DragFloat("##exittime", &trans->exitTime, 0.01f, 0.0f, 100.0f, "%.2fs"))
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
                // Find the (animation) state node currently under the mouse.
                // Only real states accept a clip directly; blend trees and the
                // fixed nodes derive their animation from elsewhere.
                AnimState* target = nullptr;
                for (int i = (int)graph.states.size() - 1; i >= 0; --i)
                {
                    AnimState& st = graph.states[i];
                    if (!st.isState()) continue;

                    ImVec2 nTL, nBR;
                    nodeScreenRect(st, canvasTL, nTL, nBR);
                    if (mouse.x >= nTL.x && mouse.x <= nBR.x &&
                        mouse.y >= nTL.y && mouse.y <= nBR.y)
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
                ImVec2 nTL, nBR;
                nodeScreenRect(state, canvasTL, nTL, nBR);

                if (mouse.x < nTL.x || mouse.x > nBR.x || mouse.y < nTL.y || mouse.y > nBR.y)
                    continue;

                // Clicking inside a selected blend tree's diagram grabs the
                // nearest motion point so it can be dragged to a new position
                // instead of moving the whole node.
                if (state.isBlendTree() && state.id == selectedState && !state.blendChildren.empty())
                {
                    ImVec2 dtl, dbr;
                    blendDiagramRect(state, canvasTL, dtl, dbr);
                    if (mouse.x >= dtl.x && mouse.x <= dbr.x &&
                        mouse.y >= dtl.y && mouse.y <= dbr.y)
                    {
                        const bool is2D = (state.blendType == AnimBlendType::TwoD);
                        auto mapX = [&](float v) {
                            float r0 = state.blendRangeXMin, r1 = state.blendRangeXMax;
                            if (r1 - r0 < 1e-5f) r1 = r0 + 1.f;
                            return dtl.x + (v - r0) / (r1 - r0) * (dbr.x - dtl.x);
                        };
                        auto mapY = [&](float v) {
                            float r0 = state.blendRangeYMin, r1 = state.blendRangeYMax;
                            if (r1 - r0 < 1e-5f) r1 = r0 + 1.f;
                            return dbr.y - (v - r0) / (r1 - r0) * (dbr.y - dtl.y);
                        };
                        int   best  = -1;
                        float bestD = 1e30f;
                        for (int c = 0; c < (int)state.blendChildren.size(); ++c)
                        {
                            const AnimBlendChild& ch = state.blendChildren[c];
                            float cx = mapX(is2D ? ch.posX : ch.threshold);
                            float cy = is2D ? mapY(ch.posY) : (dtl.y + dbr.y) * 0.5f;
                            float ddx = mouse.x - cx, ddy = mouse.y - cy;
                            float d2 = ddx * ddx + ddy * ddy;
                            if (d2 < bestD) { bestD = d2; best = c; }
                        }
                        // Only grab when the click is on (or very close to) a
                        // motion dot, so clicking empty diagram space still moves
                        // the node rather than teleporting the nearest motion.
                        float pickR = 14.f * canvasZoom;
                        if (best >= 0 && bestD <= pickR * pickR)
                        {
                            dragBlendChildIndex = best;
                            selectedState       = state.id;
                            selectedTransition  = -1;
                            hitState = true;
                            break;
                        }
                    }
                }

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

    // Drag a motion point inside a blend-tree diagram.
    if (dragBlendChildIndex >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        AnimState* bt = graph.findState(selectedState);
        if (bt && bt->isBlendTree() &&
            dragBlendChildIndex < (int)bt->blendChildren.size())
        {
            ImVec2 dtl, dbr;
            blendDiagramRect(*bt, canvasTL, dtl, dbr);
            AnimBlendChild& ch = bt->blendChildren[dragBlendChildIndex];

            float nx = (dbr.x - dtl.x) > 1.f
                ? bt->blendRangeXMin + (mouse.x - dtl.x) / (dbr.x - dtl.x) *
                      (bt->blendRangeXMax - bt->blendRangeXMin)
                : 0.f;
            if (bt->blendType == AnimBlendType::TwoD)
            {
                float ny = (dbr.y - dtl.y) > 1.f
                    ? bt->blendRangeYMin + (dbr.y - mouse.y) / (dbr.y - dtl.y) *
                          (bt->blendRangeYMax - bt->blendRangeYMin)
                    : 0.f;
                ch.posX = nx;
                ch.posY = ny;
            }
            else
            {
                ch.threshold = nx;
            }
            graph.dirty = true;
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
        isDraggingState     = false;
        isResizingComment   = false;
        dragBlendChildIndex = -1;

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
            ImVec2 nTL, nBR;
            nodeScreenRect(state, canvasTL, nTL, nBR);

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
            // The Default State and the Any State node are permanent.
            if (st && !st->isAnyState() &&
                (!st->isDefault || st->isAnchor() || st->isComment()))
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
