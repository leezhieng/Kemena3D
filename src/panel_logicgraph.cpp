#define IMGUI_DEFINE_MATH_OPERATORS
#include "panel_logicgraph.h"

#include "imgui.h"
#include "panel_console.h"
#include "imgui_internal.h"
#include "portable-file-dialogs.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
namespace
{
    const float NODE_W   = 240.0f;
    const float HEADER_H = 26.0f;
    const float ROW_H    = 22.0f;
    const float PIN_R    = 5.5f;
    const float PIN_HIT  = 9.0f;

    enum class NodeCategory { Event, Flow, Action, Getter, Value, Math };

    NodeCategory categoryOf(kScriptNodeType t)
    {
        switch (t)
        {
            case kScriptNodeType::EventAwake:
            case kScriptNodeType::EventStart:
            case kScriptNodeType::EventUpdate:
            case kScriptNodeType::EventFixedUpdate:
            case kScriptNodeType::EventLateUpdate:
            case kScriptNodeType::EventOnDestroy:
            case kScriptNodeType::EventCollisionEnter:
            case kScriptNodeType::EventCollisionStay:
            case kScriptNodeType::EventCollisionExit:
            case kScriptNodeType::EventTriggerEnter:
            case kScriptNodeType::EventTriggerStay:
            case kScriptNodeType::EventTriggerExit:  return NodeCategory::Event;
            case kScriptNodeType::Branch:
            case kScriptNodeType::Sequence:        return NodeCategory::Flow;
            case kScriptNodeType::Print:
            case kScriptNodeType::SetPosition:
            case kScriptNodeType::SetRotation:
            case kScriptNodeType::SetScale:
            case kScriptNodeType::Translate:
            case kScriptNodeType::Rotate:
            case kScriptNodeType::SetActive:
            case kScriptNodeType::SetVariable:
            case kScriptNodeType::PlaySound:
            case kScriptNodeType::StopAllSounds:
            case kScriptNodeType::SetMasterVolume:
            case kScriptNodeType::SetListenerPosition:
            case kScriptNodeType::SetListenerDirection:
            case kScriptNodeType::PlayAnimation:
            case kScriptNodeType::SetAnimatorSpeed:
            case kScriptNodeType::SetAnimatorTime:
            case kScriptNodeType::SetAnimatorBool:
            case kScriptNodeType::SetAnimatorFloat:
            case kScriptNodeType::SetAnimatorInt:
            case kScriptNodeType::SetAnimatorTrigger:
            case kScriptNodeType::ApplyForce:
            case kScriptNodeType::ApplyImpulse:
            case kScriptNodeType::ApplyTorque:
            case kScriptNodeType::SetLinearVelocity:
            case kScriptNodeType::SetAngularVelocity:
            case kScriptNodeType::SetPhysicsGravity:
            case kScriptNodeType::MoveCharacter:
            case kScriptNodeType::CompareTag:      return NodeCategory::Action;
            case kScriptNodeType::GetSelf:
            case kScriptNodeType::GetPosition:
            case kScriptNodeType::GetRotation:
            case kScriptNodeType::GetScale:
            case kScriptNodeType::GetForward:
            case kScriptNodeType::GetRight:
            case kScriptNodeType::GetUp:
            case kScriptNodeType::GetDeltaTime:
            case kScriptNodeType::GetVariable:
            case kScriptNodeType::GetAction:
            case kScriptNodeType::GetActionPressed:
            case kScriptNodeType::GetActionReleased:
            case kScriptNodeType::GetAxis:
            case kScriptNodeType::GetMasterVolume:
            case kScriptNodeType::GetAnimator:
            case kScriptNodeType::GetAnimatorSpeed:
            case kScriptNodeType::GetAnimatorRootMotionPosition:
            case kScriptNodeType::GetAnimatorRootMotionRotation:
            case kScriptNodeType::GetPhysicsObject:
            case kScriptNodeType::GetPhysicsVelocity:
            case kScriptNodeType::GetPhysicsPosition:
            case kScriptNodeType::GetPhysicsGravity:
            case kScriptNodeType::GetTag:
            case kScriptNodeType::IsPhysicsActive: return NodeCategory::Getter;
            case kScriptNodeType::LiteralFloat:
            case kScriptNodeType::LiteralBool:
            case kScriptNodeType::LiteralString:
            case kScriptNodeType::LiteralInt:
            case kScriptNodeType::LiteralVec3:     return NodeCategory::Value;
            default:                               return NodeCategory::Math;
        }
    }

    ImU32 headerColor(kScriptNodeType t)
    {
        switch (categoryOf(t))
        {
            case NodeCategory::Event:  return IM_COL32(150, 62, 62, 255);
            case NodeCategory::Flow:   return IM_COL32(96, 96, 104, 255);
            case NodeCategory::Action: return IM_COL32(54, 92, 142, 255);
            case NodeCategory::Getter: return IM_COL32(58, 122, 80, 255);
            case NodeCategory::Value:  return IM_COL32(120, 96, 52, 255);
            default:                   return IM_COL32(86, 74, 120, 255);
        }
    }

    ImU32 pinColor(kScriptPinType t)
    {
        switch (t)
        {
            case kScriptPinType::Exec:   return IM_COL32(235, 235, 235, 255);
            case kScriptPinType::Float:  return IM_COL32(126, 206, 126, 255);
            case kScriptPinType::Int:    return IM_COL32(126, 206, 206, 255);
            case kScriptPinType::Bool:   return IM_COL32(206, 96, 96, 255);
            case kScriptPinType::String: return IM_COL32(206, 156, 96, 255);
            case kScriptPinType::Vec3:   return IM_COL32(150, 150, 232, 255);
            case kScriptPinType::Object: return IM_COL32(224, 200, 120, 255);
            default:                     return IM_COL32(200, 200, 200, 255);
        }
    }

    int payloadRows(kScriptNodeType t)
    {
        switch (t)
        {
            case kScriptNodeType::LiteralFloat:
            case kScriptNodeType::LiteralBool:
            case kScriptNodeType::LiteralString:
            case kScriptNodeType::LiteralInt:
            case kScriptNodeType::GetVariable:
            case kScriptNodeType::SetVariable: return 1;
            case kScriptNodeType::GetAction:
            case kScriptNodeType::GetActionPressed:
            case kScriptNodeType::GetActionReleased:
            case kScriptNodeType::GetAxis:
            case kScriptNodeType::CompareTag: return 1;
            case kScriptNodeType::LiteralVec3: return 3;
            case kScriptNodeType::Sequence:    return 1;
            default:                           return 0;
        }
    }

    // Finds a pin by id within a node; sets isOutput when found.
    kScriptGraphPin *getPin(kScriptGraphNode *n, int pinId, bool *isOutput)
    {
        for (auto &p : n->inputs)
            if (p.id == pinId) { if (isOutput) *isOutput = false; return &p; }
        for (auto &p : n->outputs)
            if (p.id == pinId) { if (isOutput) *isOutput = true; return &p; }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Construction / file operations
// ---------------------------------------------------------------------------

PanelLogicGraph::PanelLogicGraph(kGuiManager *setGui, Manager *setManager)
    : gui(setGui), manager(setManager)
{
    if (manager) manager->panelLogicGraph = this;
    newGraph();
}

std::vector<std::string> PanelLogicGraph::actionNameList() const
{
    std::vector<std::string> names;
    if (!manager)
        return names;
    // Same source the runtime uses (Manager::applyInputBindings()) so the
    // picker always offers exactly the actions getAction()/getActionPressed()/
    // getActionReleased() can resolve.
    for (const auto &a : manager->inputSettings.actions)
    {
        if (a.name.empty())
            continue;
        if (std::find(names.begin(), names.end(), a.name) == names.end())
            names.push_back(a.name);
    }
    return names;
}

void PanelLogicGraph::notifyAssetMoved(const std::string &oldPath, const std::string &newPath)
{
    if (filePath.empty()) return;
    std::error_code ec;
    // Compare canonical-ish paths so separators/relativity don't cause misses.
    if (fs::path(filePath).lexically_normal() == fs::path(oldPath).lexically_normal())
    {
        filePath   = newPath;
        graph.name = fs::path(newPath).stem().string();
        logStatus(LogLevel::Info, "Renamed to " + fs::path(newPath).filename().string());
    }
}

void PanelLogicGraph::newGraph()
{
    graph = kScriptGraph{};
    graph.uuid = generateUuid();
    graph.name = "NewScriptGraph";
    filePath.clear();
    canvasOffset = ImVec2(0.0f, 0.0f);
    selectedNode = 0;
    statusLine.clear();

    // Seed with an On Update event so the canvas is not empty.
    graph.nodes.push_back(graph.makeNode(kScriptNodeType::EventUpdate, 120.0f, 120.0f));
}

void PanelLogicGraph::openFile(const std::string &path)
{
    loadGraph(path);
}

bool PanelLogicGraph::loadGraph(const std::string &path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        logStatus(LogLevel::Error, "Failed to open: " + path);
        return false;
    }
    try
    {
        json j;
        in >> j;
        graph.fromJson(j);
    }
    catch (const std::exception &e)
    {
        logStatus(LogLevel::Error, std::string("Parse error: ") + e.what());
        return false;
    }
    filePath     = path;
    canvasOffset = ImVec2(0.0f, 0.0f);
    selectedNode = 0;
    logStatus(LogLevel::Info, "Loaded " + fs::path(path).filename().string());
    return true;
}

bool PanelLogicGraph::saveGraphAs()
{
    std::string defaultDir =
        manager ? (manager->projectPath / "Assets").string() : std::string();

    std::string result = pfd::save_file(
        "Save Logic Graph", defaultDir,
        { "Kemena Logic (*.logic)", "*.logic" }).result();

    if (result.empty())
        return false;

    if (result.size() < 6 || result.substr(result.size() - 6) != ".logic")
        result += ".logic";

    filePath   = result;
    graph.name = fs::path(result).stem().string();
    if (graph.uuid.empty())
        graph.uuid = generateUuid();
    return saveGraph();
}

bool PanelLogicGraph::saveGraph()
{
    if (filePath.empty())
        return saveGraphAs();

    std::ofstream out(filePath);
    if (!out.is_open())
    {
        logStatus(LogLevel::Error, "Cannot write: " + filePath);
        return false;
    }
    out << graph.toJson().dump(2);
    out.close();

    graph.dirty = false;
    regenerateScript();
    return true;
}

void PanelLogicGraph::regenerateScript()
{
    if (!manager || graph.uuid.empty())
    {
        logStatus(LogLevel::Error, "Cannot generate script: missing project or graph UUID");
        return;
    }

    kScriptGraphResult res = kScriptGraphCompiler::compile(graph);
    if (!res.success)
    {
        logStatus(LogLevel::Error, "Compile error: " + res.error);
        return;
    }

    // Generated AngelScript lives outside Assets/: a temp build artifact
    // keyed by the .logic UUID so multiple .logic files can't collide.
    fs::path tempDir = manager->projectPath / "Library" / "GeneratedScripts";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    fs::path asPath = tempDir / (graph.uuid + ".as");

    std::ofstream out(asPath);
    if (!out.is_open())
    {
        logStatus(LogLevel::Error, "Cannot write generated script: " + asPath.string());
        return;
    }
    out << res.code;
    out.close();

    // Refresh bytecode for any object already using this generated script.
    manager->buildScripts();

    // Report the asset the user actually saved (.logic) rather than the
    // internal uuid-keyed generated script, which never appears in Assets/.
    const std::string assetName = filePath.empty()
                                      ? graph.name
                                      : fs::path(filePath).filename().string();
    logStatus(LogLevel::Info, "Saved + compiled -> " + assetName);
}

// ---------------------------------------------------------------------------
// Coordinate mapping
// ---------------------------------------------------------------------------

ImVec2 PanelLogicGraph::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return ImVec2(origin.x + (cp.x + canvasOffset.x) * canvasZoom,
                  origin.y + (cp.y + canvasOffset.y) * canvasZoom);
}

ImVec2 PanelLogicGraph::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return ImVec2((sp.x - origin.x) / canvasZoom - canvasOffset.x,
                  (sp.y - origin.y) / canvasZoom - canvasOffset.y);
}

// ---------------------------------------------------------------------------
// Pin geometry — single source of truth, shared by node + link drawing
// ---------------------------------------------------------------------------

namespace
{
    // Returns the screen position of a pin given the node origin and zoom.
    ImVec2 pinScreenPos(const kScriptGraphNode &n, int pinId, ImVec2 nodeScreen,
                        float zoom)
    {
        if (n.type == kScriptNodeType::Anchor)
        {
            if (!n.inputs.empty() && n.inputs[0].id == pinId)
                return ImVec2(nodeScreen.x, nodeScreen.y + 12.0f * zoom);
            if (!n.outputs.empty() && n.outputs[0].id == pinId)
                return ImVec2(nodeScreen.x + 24.0f * zoom, nodeScreen.y + 12.0f * zoom);
            return nodeScreen;
        }

        for (size_t i = 0; i < n.inputs.size(); ++i)
            if (n.inputs[i].id == pinId)
                return ImVec2(nodeScreen.x,
                              nodeScreen.y + (HEADER_H + i * ROW_H + ROW_H * 0.5f) * zoom);
        for (size_t i = 0; i < n.outputs.size(); ++i)
            if (n.outputs[i].id == pinId)
                return ImVec2(nodeScreen.x + NODE_W * zoom,
                              nodeScreen.y + (HEADER_H + i * ROW_H + ROW_H * 0.5f) * zoom);
        return nodeScreen;
    }

    void drawWire(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thick)
    {
        float dist = std::min(std::max(std::fabs(b.x - a.x) * 0.5f, 28.0f), 180.0f);
        dl->AddBezierCubic(a, ImVec2(a.x + dist, a.y),
                           ImVec2(b.x - dist, b.y), b, col, thick);
    }
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void PanelLogicGraph::logStatus(LogLevel level, const std::string &msg)
{
    // The console is the single sink for save/load feedback; statusLine is only
    // kept as a fallback for callers that run before the console exists.
    statusLine = msg;
    if (manager && manager->panelConsole)
        manager->panelConsole->addLog(level, "%s", msg.c_str());
}

void PanelLogicGraph::drawToolbar()
{
    if (ImGui::Button("New"))
        newGraph();
    ImGui::SameLine();
    if (ImGui::Button("Open"))
    {
        std::string dir =
            manager ? (manager->projectPath / "Assets").string() : std::string();
        auto sel = pfd::open_file("Open Logic Graph", dir,
                                  { "Kemena Logic (*.logic)", "*.logic" }).result();
        if (!sel.empty())
            loadGraph(sel[0]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
        saveGraph();
    ImGui::SameLine();
    if (ImGui::Button("Save As..."))
        saveGraphAs();

    // Opened file name centered across the whole toolbar width (not just the
    // space right of the buttons, which would push it too far right).
    std::string title = filePath.empty() ? std::string("untitled")
                                         : fs::path(filePath).filename().string();
    if (graph.dirty)
        title += " *";

    const float titleWidth   = ImGui::CalcTextSize(title.c_str()).x;
    const float contentMinX  = ImGui::GetWindowContentRegionMin().x;
    const float contentWidth = ImGui::GetWindowContentRegionMax().x - contentMinX;
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
}

// ---------------------------------------------------------------------------
// Variables panel
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawVariablesPanel()
{
    static const char *varTypeNames[] = {
        "int", "float", "bool", "vector3", "string",
        "object", "animator", "audio source", "material",
    };

    ImGui::BeginChild("##scriptvars", ImVec2(variablesPanelWidth, 0.0f), true);

    // "Add Variable" button at the very top, exactly like the animator column.
    // New variables default to int; the type is changed in the table below.
    if (ImGui::Button("Add Variable", ImVec2(-1.0f, 0.0f)))
    {
        kScriptGraphVar v;
        // Pick the first free "varN" identifier so removing variables can never
        // leave a duplicate global name behind (duplicates break compilation).
        int counter = 1;
        while (true)
        {
            bool taken = false;
            for (const auto &ov : graph.variables)
            {
                if (ov.name == "var" + std::to_string(counter)) { taken = true; break; }
            }
            if (!taken) break;
            counter++;
        }
        v.name = "var" + std::to_string(counter);
        v.type = kScriptVarType::Int;
        graph.variables.push_back(v);
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
        int removeIndex = -1;

        // Square remove button sized to the row height; it sits at the left edge
        // of its cell so the fixed column never crops it.
        const float xBtn = ImGui::GetFrameHeight();
        const float xCol = xBtn + ImGui::GetStyle().CellPadding.x * 2.0f + 2.0f;

        if (ImGui::BeginTable("##scriptvarstable", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, xCol);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)graph.variables.size(); ++i)
            {
                kScriptGraphVar &v = graph.variables[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                // --- Name ---
                ImGui::TableSetColumnIndex(0);
                {
                    char nameBuf[64];
                    strncpy_s(nameBuf, sizeof(nameBuf), v.name.c_str(), _TRUNCATE);
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
                    {
                        v.name = nameBuf;
                        graph.dirty = true;
                    }
                }

                // --- Type ---
                ImGui::TableSetColumnIndex(1);
                {
                    int vt = (int)v.type;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##type", &vt, varTypeNames, IM_ARRAYSIZE(varTypeNames)))
                    {
                        v.type = (kScriptVarType)vt;
                        graph.dirty = true;
                    }
                }

                // --- Default value ---
                ImGui::TableSetColumnIndex(2);
                switch (v.type)
                {
                    case kScriptVarType::Int:
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::DragInt("##def", &v.defInt, 0.05f))
                            graph.dirty = true;
                        break;
                    case kScriptVarType::Float:
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::DragFloat("##def", &v.defValue, 0.05f))
                            graph.dirty = true;
                        break;
                    case kScriptVarType::Bool:
                        if (ImGui::Checkbox("##def", &v.defBool))
                            graph.dirty = true;
                        break;
                    case kScriptVarType::String:
                    {
                        char buf[128];
                        strncpy_s(buf, sizeof(buf), v.defStr.c_str(), _TRUNCATE);
                        buf[sizeof(buf) - 1] = '\0';
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##def", buf, sizeof(buf)))
                        {
                            v.defStr = buf;
                            graph.dirty = true;
                        }
                        break;
                    }
                    case kScriptVarType::Vec3:
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::DragFloat3("##def", v.defVec, 0.05f))
                            graph.dirty = true;
                        break;
                    default:
                        ImGui::TextDisabled("null");
                        break;
                }

                // --- Remove (square, left-aligned; label centered) ---
                ImGui::TableSetColumnIndex(3);
                // Zero the frame padding so the "x" glyph truly centers inside
                // the square. The theme's wide FramePadding inflates the button's
                // minimum width so it would overflow the cell and clip off-center.
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                if (ImGui::Button("x", ImVec2(xBtn, xBtn)))
                    removeIndex = i;
                ImGui::PopStyleVar(2);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (removeIndex >= 0)
        {
            graph.variables.erase(graph.variables.begin() + removeIndex);
            graph.dirty = true;
        }

        ImGui::Spacing();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Splitter between the variables column and the canvas
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawVariablesSplitter()
{
    const float splitterWidth = 6.0f;
    const float minWidth      = 200.0f;
    const float maxWidth      = 520.0f;

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y < 1.0f) avail.y = 1.0f;
    ImGui::InvisibleButton("##scriptvarsplit", ImVec2(splitterWidth, avail.y));

    if (ImGui::IsItemActive())
    {
        variablesPanelWidth = ImClamp(variablesPanelWidth + ImGui::GetIO().MouseDelta.x,
                                      minWidth, maxWidth);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // Draw a subtle vertical grab handle.
    ImDrawList *dl   = ImGui::GetWindowDrawList();
    ImVec2      rect = ImGui::GetItemRectMin();
    ImVec2      max  = ImGui::GetItemRectMax();
    ImU32       col  = ImGui::IsItemHovered() || ImGui::IsItemActive()
                          ? IM_COL32(120, 160, 220, 255)
                          : IM_COL32(70, 70, 70, 255);
    dl->AddRectFilled(ImVec2(rect.x + 2.0f, rect.y),
                      ImVec2(max.x - 2.0f, max.y), col);

    ImGui::PopStyleVar();
    ImGui::SameLine();
}

// ---------------------------------------------------------------------------
// Node drawing
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin)
{
    if (node.type == kScriptNodeType::Anchor)  { drawAnchorNode(dl, node, origin);  return; }
    if (node.type == kScriptNodeType::Comment) { drawCommentNode(dl, node, origin); return; }

    const float z = canvasZoom;

    int rows = (int)std::max(node.inputs.size(), node.outputs.size());
    if (rows < 1) rows = 1;
    float plRows = (float)payloadRows(node.type);
    float height = (HEADER_H + rows * ROW_H + plRows * ROW_H + 8.0f) * z;

    ImVec2 nodeScreen = canvasToScreen(ImVec2(node.posX, node.posY), origin);
    ImVec2 nodeMax    = ImVec2(nodeScreen.x + NODE_W * z, nodeScreen.y + height);
    bool   selected   = (node.id == selectedNode);

    // Body + header + border.
    dl->AddRectFilled(nodeScreen, nodeMax, IM_COL32(40, 42, 48, 245), 5.0f * z);
    dl->AddRectFilled(nodeScreen, ImVec2(nodeMax.x, nodeScreen.y + HEADER_H * z),
                      headerColor(node.type), 5.0f * z, ImDrawFlags_RoundCornersTop);
    dl->AddRect(nodeScreen, nodeMax,
                selected ? IM_COL32(255, 170, 60, 255) : IM_COL32(20, 20, 24, 255),
                5.0f * z, 0, selected ? 2.5f : 1.2f);
    dl->AddText(ImVec2(nodeScreen.x + 10.0f * z, nodeScreen.y + 5.0f * z),
                IM_COL32(245, 245, 245, 255), node.name.c_str());

    ImGui::PushID(node.id);

    // The whole node body is the select / move handle. Allow overlap so the pin
    // and value widgets submitted afterwards still receive input on top of it.
    ImGui::SetCursorScreenPos(nodeScreen);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##node", ImVec2(NODE_W * z, height));
    if (ImGui::IsItemActivated())
    {
        selectedNode = node.id;
        movingNode   = node.id;
    }
    if (ImGui::IsItemActive() && movingNode == node.id)
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.posX += d.x / z;
        node.posY += d.y / z;
    }
    if (ImGui::IsItemDeactivated() && movingNode == node.id)
        movingNode = 0;

    // Input pins (+ inline default editors for unconnected data pins).
    for (size_t i = 0; i < node.inputs.size(); ++i)
    {
        kScriptGraphPin &p = node.inputs[i];
        ImVec2 pp = pinScreenPos(node, p.id, nodeScreen, z);
        bool connected = graph.incomingLink(node.id, p.id) != nullptr;

        dl->AddCircleFilled(pp, PIN_R * z, pinColor(p.type));
        if (!connected)
            dl->AddCircleFilled(pp, (PIN_R - 2.0f) * z, IM_COL32(40, 42, 48, 255));
        if (!p.name.empty())
            dl->AddText(ImVec2(pp.x + 10.0f * z, pp.y - 7.0f * z),
                        IM_COL32(210, 210, 210, 255), p.name.c_str());

        if (!connected && p.type != kScriptPinType::Exec && p.type != kScriptPinType::Vec3)
        {
            ImGui::PushID((int)p.id);
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 92.0f * z, pp.y - 9.0f * z));
            if (p.type == kScriptPinType::Float)
            {
                ImGui::SetNextItemWidth(64.0f * z);
                if (ImGui::DragFloat("##d", &p.defFloat, 0.05f))
                    graph.dirty = true;
            }
            else if (p.type == kScriptPinType::Int)
            {
                ImGui::SetNextItemWidth(64.0f * z);
                if (ImGui::DragInt("##d", &p.defInt, 0.05f))
                    graph.dirty = true;
            }
            else if (p.type == kScriptPinType::Bool)
            {
                if (ImGui::Checkbox("##d", &p.defBool))
                    graph.dirty = true;
            }
            else if (p.type == kScriptPinType::String)
            {
                char buf[128];
                strncpy_s(buf, sizeof(buf), p.defStr.c_str(), _TRUNCATE);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(78.0f * z);
                if (ImGui::InputText("##d", buf, sizeof(buf)))
                {
                    p.defStr = buf;
                    graph.dirty = true;
                }
            }
            ImGui::PopID();
        }
    }

    // Output pins.
    for (size_t i = 0; i < node.outputs.size(); ++i)
    {
        kScriptGraphPin &p = node.outputs[i];
        ImVec2 pp = pinScreenPos(node, p.id, nodeScreen, z);
        bool connected = graph.outgoingLink(node.id, p.id) != nullptr;

        dl->AddCircleFilled(pp, PIN_R * z, pinColor(p.type));
        if (!connected)
            dl->AddCircleFilled(pp, (PIN_R - 2.0f) * z, IM_COL32(40, 42, 48, 255));
        if (!p.name.empty())
        {
            ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            dl->AddText(ImVec2(pp.x - 10.0f * z - ts.x, pp.y - 7.0f * z),
                        IM_COL32(210, 210, 210, 255), p.name.c_str());
        }
    }

    // Payload widgets below the pin rows.
    float payloadY = nodeScreen.y + (HEADER_H + rows * ROW_H + 3.0f) * z;
    ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f * z, payloadY));
    ImGui::PushItemWidth((NODE_W - 24.0f) * z);

    switch (node.type)
    {
        case kScriptNodeType::LiteralFloat:
            if (ImGui::DragFloat("##lf", &node.valueFloat[0], 0.05f))
                graph.dirty = true;
            break;
        case kScriptNodeType::LiteralInt:
        {
            int iv = (int)node.valueFloat[0];
            if (ImGui::DragInt("##li", &iv, 1.0f))
            {
                node.valueFloat[0] = (float)iv;
                graph.dirty = true;
            }
            break;
        }
        case kScriptNodeType::LiteralBool:
            if (ImGui::Checkbox("Value##lb", &node.valueBool))
                graph.dirty = true;
            break;
        case kScriptNodeType::LiteralString:
        {
            char buf[256];
            strncpy_s(buf, sizeof(buf), node.valueStr.c_str(), _TRUNCATE);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("##ls", buf, sizeof(buf)))
            {
                node.valueStr = buf;
                graph.dirty = true;
            }
            break;
        }
        case kScriptNodeType::LiteralVec3:
            if (ImGui::DragFloat("X##lv", &node.valueFloat[0], 0.05f)) graph.dirty = true;
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f * z, payloadY + ROW_H * z));
            if (ImGui::DragFloat("Y##lv", &node.valueFloat[1], 0.05f)) graph.dirty = true;
            ImGui::SetCursorScreenPos(ImVec2(nodeScreen.x + 12.0f * z, payloadY + ROW_H * 2.0f * z));
            if (ImGui::DragFloat("Z##lv", &node.valueFloat[2], 0.05f)) graph.dirty = true;
            break;
        case kScriptNodeType::GetVariable:
        case kScriptNodeType::SetVariable:
        {
            const char *cur = node.valueStr.empty() ? "(select var)" : node.valueStr.c_str();
            if (ImGui::BeginCombo("##var", cur))
            {
                for (const auto &v : graph.variables)
                {
                    bool sel = (v.name == node.valueStr);
                    if (ImGui::Selectable(v.name.c_str(), sel))
                    {
                        node.valueStr = v.name;
                        graph.dirty = true;
                    }
                }
                if (graph.variables.empty())
                    ImGui::TextDisabled("No variables defined");
                ImGui::EndCombo();
            }
            break;
        }
        case kScriptNodeType::CompareTag:
        {
            // Pick the tag from the project's tag list (Project Settings → Tags)
            // instead of free text, so the value always matches a real tag.
            static const std::vector<std::string> s_emptyTags;
            const std::vector<std::string> &tags = manager ? manager->tagSettings.tags
                                                           : s_emptyTags;
            bool valid = !node.valueStr.empty() &&
                         std::find(tags.begin(), tags.end(), node.valueStr) != tags.end();
            if (!node.valueStr.empty() && !valid)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "!");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Tag \"%s\" is not in Project Settings → Tags.\n"
                        "This node will never match - pick a tag from the list.",
                        node.valueStr.c_str());
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth((NODE_W - 24.0f - (valid ? 0.0f : 18.0f)) * z);
            if (ImGui::BeginCombo("##tag",
                                  node.valueStr.empty() ? "(select tag)" : node.valueStr.c_str()))
            {
                for (const auto &t : tags)
                {
                    bool sel = (t == node.valueStr);
                    if (ImGui::Selectable(t.c_str(), sel))
                    {
                        node.valueStr = t;
                        graph.dirty   = true;
                    }
                }
                if (tags.empty())
                    ImGui::TextDisabled("No tags defined in Project Settings");
                ImGui::EndCombo();
            }
            break;
        }
        case kScriptNodeType::GetAction:
        case kScriptNodeType::GetActionPressed:
        case kScriptNodeType::GetActionReleased:
        case kScriptNodeType::GetAxis:
        {
            // Pick the action from the Project Settings bindings instead of a
            // free-text field. A hand-typed name that doesn't exactly match a
            // bound action silently never fires (especially pressed/released),
            // so the picker keeps the node's name aligned with the bindings.
            std::vector<std::string> actions = actionNameList();
            bool valid = !node.valueStr.empty() &&
                         std::find(actions.begin(), actions.end(), node.valueStr) != actions.end();

            // Flag unbound names (typos or legacy graphs) so the failure is
            // visible instead of the node silently doing nothing.
            if (!node.valueStr.empty() && !valid)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "!");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Action \"%s\" is not bound in Project Settings.\n"
                        "This node will never trigger - pick an action from the list.",
                        node.valueStr.c_str());
                ImGui::SameLine();
            }

            ImGui::SetNextItemWidth((NODE_W - 24.0f - (valid ? 0.0f : 18.0f)) * z);
            if (ImGui::BeginCombo("##action",
                                  node.valueStr.empty() ? "(select action)" : node.valueStr.c_str()))
            {
                if (actions.empty())
                    ImGui::TextDisabled("No actions bound in Project Settings");
                for (const auto &a : actions)
                {
                    bool sel = (a == node.valueStr);
                    if (ImGui::Selectable(a.c_str(), sel))
                    {
                        node.valueStr = a;
                        graph.dirty   = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case kScriptNodeType::Sequence:
        {
            if (ImGui::Button("+"))
            {
                kScriptGraphPin p;
                p.id       = graph.newId();
                p.type     = kScriptPinType::Exec;
                p.isOutput = true;
                node.outputs.push_back(p);
                graph.dirty = true;
            }
            ImGui::SameLine();
            if (node.outputs.size() > 1 && ImGui::Button("-"))
            {
                kScriptGraphPin last = node.outputs.back();
                graph.removeLinksByPin(node.id, last.id);
                node.outputs.pop_back();
                graph.dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("exec outputs");
            break;
        }
        default:
            break;
    }

    ImGui::PopItemWidth();
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Anchor + comment drawing
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawAnchorNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin)
{
    const float z = canvasZoom;
    ImVec2 c        = canvasToScreen(ImVec2(node.posX, node.posY), origin) + ImVec2(12.0f, 12.0f) * z;
    bool   selected = (node.id == selectedNode);
    const float r   = 12.0f * z;

    dl->AddCircleFilled(c, r, IM_COL32(72, 84, 102, 235));
    dl->AddCircle(c, r, selected ? IM_COL32(255, 210, 80, 255) : IM_COL32(140, 165, 195, 255),
                  0, selected ? 2.5f : 1.5f);

    // Visible connection points (left = input, right = output). These match
    // pinScreenPos() so the manual hit-test in drawCanvas() finds them.
    dl->AddCircleFilled(ImVec2(c.x - r, c.y), PIN_R * z, IM_COL32(235, 235, 235, 255));
    dl->AddCircleFilled(ImVec2(c.x + r, c.y), PIN_R * z, IM_COL32(235, 235, 235, 255));

    ImGui::PushID(node.id);
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    ImGui::InvisibleButton("##anchorbody", ImVec2(24.0f, 24.0f) * z);
    if (ImGui::IsItemActivated())
    {
        selectedNode = node.id;
        movingNode   = node.id;
    }
    if (ImGui::IsItemActive() && movingNode == node.id)
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.posX += d.x / z;
        node.posY += d.y / z;
    }
    if (ImGui::IsItemDeactivated() && movingNode == node.id)
        movingNode = 0;
    ImGui::PopID();
}

void PanelLogicGraph::drawCommentNode(ImDrawList *dl, kScriptGraphNode &node, ImVec2 origin)
{
    const float z = canvasZoom;
    ImVec2 tl       = canvasToScreen(ImVec2(node.posX, node.posY), origin);
    ImVec2 br       = tl + ImVec2(node.sizeX, node.sizeY) * z;
    bool   selected = (node.id == selectedNode);

    dl->AddRectFilled(tl, br, IM_COL32(58, 76, 100, 78), 6.0f * z);
    dl->AddRect(tl, br,
                selected ? IM_COL32(230, 190, 90, 220) : IM_COL32(110, 138, 170, 160),
                6.0f * z, 0, selected ? 2.0f : 1.0f);

    ImGui::PushID(node.id);

    // Whole box selects / moves the comment.
    ImGui::SetCursorScreenPos(tl);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##commentbody", ImVec2(node.sizeX, node.sizeY) * z);
    if (ImGui::IsItemActivated())
    {
        selectedNode = node.id;
        movingNode   = node.id;
    }
    if (ImGui::IsItemActive() && movingNode == node.id)
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.posX += d.x / z;
        node.posY += d.y / z;
    }
    if (ImGui::IsItemDeactivated() && movingNode == node.id)
        movingNode = 0;

    // Bottom-right resize handle.
    ImVec2 hTL(br.x - 14.0f * z, br.y - 14.0f * z);
    ImGui::SetCursorScreenPos(hTL);
    ImGui::InvisibleButton("##commentresize", ImVec2(14.0f, 14.0f) * z);
    if (ImGui::IsItemActivated())
    {
        selectedNode    = node.id;
        resizingComment = node.id;
    }
    if (ImGui::IsItemActive() && resizingComment == node.id)
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        node.sizeX = std::max(80.0f, node.sizeX + d.x / z);
        node.sizeY = std::max(60.0f, node.sizeY + d.y / z);
        graph.dirty = true;
    }
    if (ImGui::IsItemDeactivated() && resizingComment == node.id)
        resizingComment = 0;
    dl->AddTriangleFilled(hTL, br, ImVec2(br.x, hTL.y), IM_COL32(210, 210, 220, 210));

    if (selected)
    {
        char buf[1024];
        strncpy_s(buf, sizeof(buf), node.comment.c_str(), _TRUNCATE);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetCursorScreenPos(ImVec2(tl.x + 6.0f * z, tl.y + 4.0f * z));
        ImGui::SetNextItemWidth(node.sizeX * z - 12.0f * z);
        if (ImGui::InputText("##ctext", buf, sizeof(buf)))
        {
            node.comment = buf;
            graph.dirty  = true;
        }
    }
    else
    {
        std::string text = node.comment.empty() ? "Comment" : node.comment;
        ImVec2 p         = tl + ImVec2(8.0f, 6.0f) * z;
        float  lineH     = ImGui::GetFontSize() + 2.0f * z;
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

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Link drawing
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawLinks(ImDrawList *dl)
{
    for (const auto &l : graph.links)
    {
        kScriptGraphNode *from = graph.findNode(l.fromNode);
        kScriptGraphNode *to   = graph.findNode(l.toNode);
        if (!from || !to)
            continue;

        ImVec2 a = pinScreenPos(*from, l.fromPin,
                                canvasToScreen(ImVec2(from->posX, from->posY), canvasOrigin),
                                canvasZoom);
        ImVec2 b = pinScreenPos(*to, l.toPin,
                                canvasToScreen(ImVec2(to->posX, to->posY), canvasOrigin),
                                canvasZoom);

        bool isOut = false;
        kScriptGraphPin *fp = getPin(from, l.fromPin, &isOut);
        ImU32 col = fp ? pinColor(fp->type) : IM_COL32(200, 200, 200, 255);
        drawWire(dl, a, b, col, 2.6f * canvasZoom);
    }
}

// ---------------------------------------------------------------------------
// Add-node context menu
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawAddNodeMenu(ImVec2 spawn)
{
    struct Entry { const char *label; kScriptNodeType type; };
    auto submenu = [&](const char *cat, const Entry *items, int count) {
        if (ImGui::BeginMenu(cat))
        {
            for (int i = 0; i < count; ++i)
                if (ImGui::MenuItem(items[i].label))
                {
                    kScriptGraphNode n = graph.makeNode(items[i].type, spawn.x, spawn.y);
                    graph.nodes.push_back(n);
                    selectedNode = n.id;
                    graph.dirty = true;
                }
            ImGui::EndMenu();
        }
    };

    static const Entry events[] = {
        {"On Awake", kScriptNodeType::EventAwake},
        {"On Start", kScriptNodeType::EventStart},
        {"On Update", kScriptNodeType::EventUpdate},
        {"On Fixed Update", kScriptNodeType::EventFixedUpdate},
        {"On Late Update", kScriptNodeType::EventLateUpdate},
        {"On Destroy", kScriptNodeType::EventOnDestroy},
        {"On Collision Enter", kScriptNodeType::EventCollisionEnter},
        {"On Collision Stay", kScriptNodeType::EventCollisionStay},
        {"On Collision Exit", kScriptNodeType::EventCollisionExit},
        {"On Trigger Enter", kScriptNodeType::EventTriggerEnter},
        {"On Trigger Stay", kScriptNodeType::EventTriggerStay},
        {"On Trigger Exit", kScriptNodeType::EventTriggerExit},
    };
    static const Entry flow[] = {
        {"Branch", kScriptNodeType::Branch},
        {"Sequence", kScriptNodeType::Sequence},
    };
    static const Entry actions[] = {
        {"Print", kScriptNodeType::Print},
        {"Set Position", kScriptNodeType::SetPosition},
        {"Set Rotation", kScriptNodeType::SetRotation},
        {"Set Scale", kScriptNodeType::SetScale},
        {"Translate", kScriptNodeType::Translate},
        {"Rotate", kScriptNodeType::Rotate},
        {"Set Active", kScriptNodeType::SetActive},
        {"Set Variable", kScriptNodeType::SetVariable},
        {"Compare Tag", kScriptNodeType::CompareTag},
    };
    static const Entry getters[] = {
        {"Get Self", kScriptNodeType::GetSelf},
        {"Get Position", kScriptNodeType::GetPosition},
        {"Get Rotation", kScriptNodeType::GetRotation},
        {"Get Scale", kScriptNodeType::GetScale},
        {"Get Forward", kScriptNodeType::GetForward},
        {"Get Right", kScriptNodeType::GetRight},
        {"Get Up", kScriptNodeType::GetUp},
        {"Get Delta Time", kScriptNodeType::GetDeltaTime},
        {"Get Variable", kScriptNodeType::GetVariable},
        {"Get Tag", kScriptNodeType::GetTag},
    };
    static const Entry input[] = {
        {"Get Action", kScriptNodeType::GetAction},
        {"Get Action Pressed", kScriptNodeType::GetActionPressed},
        {"Get Action Released", kScriptNodeType::GetActionReleased},
        {"Get Axis", kScriptNodeType::GetAxis},
    };
    static const Entry audio[] = {
        {"Play Sound", kScriptNodeType::PlaySound},
        {"Stop All Sounds", kScriptNodeType::StopAllSounds},
        {"Set Master Volume", kScriptNodeType::SetMasterVolume},
        {"Get Master Volume", kScriptNodeType::GetMasterVolume},
        {"Set Listener Position", kScriptNodeType::SetListenerPosition},
        {"Set Listener Direction", kScriptNodeType::SetListenerDirection},
    };
    static const Entry animation[] = {
        {"Get Animator", kScriptNodeType::GetAnimator},
        {"Play Animation", kScriptNodeType::PlayAnimation},
        {"Set Animator Speed", kScriptNodeType::SetAnimatorSpeed},
        {"Set Animator Time", kScriptNodeType::SetAnimatorTime},
        {"Get Animator Speed", kScriptNodeType::GetAnimatorSpeed},
        {"Get Root Motion Position", kScriptNodeType::GetAnimatorRootMotionPosition},
        {"Get Root Motion Rotation", kScriptNodeType::GetAnimatorRootMotionRotation},
        {"Set Boolean", kScriptNodeType::SetAnimatorBool},
        {"Set Float", kScriptNodeType::SetAnimatorFloat},
        {"Set Integer", kScriptNodeType::SetAnimatorInt},
    };
    static const Entry physics[] = {
        {"Get Physics Object", kScriptNodeType::GetPhysicsObject},
        {"Apply Force", kScriptNodeType::ApplyForce},
        {"Apply Impulse", kScriptNodeType::ApplyImpulse},
        {"Apply Torque", kScriptNodeType::ApplyTorque},
        {"Set Linear Velocity", kScriptNodeType::SetLinearVelocity},
        {"Set Angular Velocity", kScriptNodeType::SetAngularVelocity},
        {"Get Physics Velocity", kScriptNodeType::GetPhysicsVelocity},
        {"Get Physics Position", kScriptNodeType::GetPhysicsPosition},
        {"Set Physics Gravity", kScriptNodeType::SetPhysicsGravity},
        {"Get Physics Gravity", kScriptNodeType::GetPhysicsGravity},
        {"Is Physics Active", kScriptNodeType::IsPhysicsActive},
        {"Move Character Controller", kScriptNodeType::MoveCharacter},
    };
    static const Entry values[] = {
        {"Float", kScriptNodeType::LiteralFloat},
        {"Int", kScriptNodeType::LiteralInt},
        {"Bool", kScriptNodeType::LiteralBool},
        {"String", kScriptNodeType::LiteralString},
        {"Vector3", kScriptNodeType::LiteralVec3},
        {"Variable", kScriptNodeType::GetVariable},
    };
    static const Entry math[] = {
        {"Add", kScriptNodeType::Add},
        {"Subtract", kScriptNodeType::Subtract},
        {"Multiply", kScriptNodeType::Multiply},
        {"Divide", kScriptNodeType::Divide},
        {"Concat String", kScriptNodeType::ConcatString},
        {"Make Vector3", kScriptNodeType::MakeVec3},
        {"Break Vector3", kScriptNodeType::BreakVec3},
        {"Scale Vector3", kScriptNodeType::ScaleVec3},
        {"Greater", kScriptNodeType::Greater},
        {"Less", kScriptNodeType::Less},
        {"Equal (Float)", kScriptNodeType::EqualFloat},
        {"Equal (Bool)", kScriptNodeType::EqualBool},
        {"Equal (Int)", kScriptNodeType::EqualInt},
        {"Equal (String)", kScriptNodeType::EqualString},
        {"And", kScriptNodeType::And},
        {"Or", kScriptNodeType::Or},
        {"Not", kScriptNodeType::Not},
    };

    if (ImGui::MenuItem("Add Anchor"))
    {
        kScriptGraphNode n = graph.makeNode(kScriptNodeType::Anchor, spawn.x, spawn.y);
        graph.nodes.push_back(n);
        selectedNode = n.id;
        graph.dirty  = true;
    }
    if (ImGui::MenuItem("Add Comment"))
    {
        kScriptGraphNode n = graph.makeNode(kScriptNodeType::Comment, spawn.x, spawn.y);
        graph.nodes.push_back(n);
        selectedNode = n.id;
        graph.dirty  = true;
    }
    ImGui::Separator();

    submenu("Events",   events,  IM_ARRAYSIZE(events));
    submenu("Flow",     flow,    IM_ARRAYSIZE(flow));
    submenu("Actions",  actions, IM_ARRAYSIZE(actions));
    submenu("Get",      getters, IM_ARRAYSIZE(getters));
    submenu("Input",    input,   IM_ARRAYSIZE(input));
    submenu("Audio",    audio,   IM_ARRAYSIZE(audio));
    submenu("Animation", animation, IM_ARRAYSIZE(animation));
    submenu("Physics",  physics, IM_ARRAYSIZE(physics));
    submenu("Values",   values,  IM_ARRAYSIZE(values));
    submenu("Math/Logic", math,  IM_ARRAYSIZE(math));

    if (selectedNode != 0)
    {
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Node"))
            copySelectedNode();
        if (hasClipboard && ImGui::MenuItem("Paste Node"))
            pasteClipboard();
        if (ImGui::MenuItem("Delete Selected Node"))
        {
            graph.removeNode(selectedNode);
            selectedNode = 0;
            graph.dirty = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void PanelLogicGraph::tryConnect(int nodeA, int pinA, int nodeB, int pinB)
{
    if (nodeA == nodeB)
        return;

    kScriptGraphNode *na = graph.findNode(nodeA);
    kScriptGraphNode *nb = graph.findNode(nodeB);
    if (!na || !nb)
        return;

    bool aOut = false, bOut = false;
    kScriptGraphPin *pa = getPin(na, pinA, &aOut);
    kScriptGraphPin *pb = getPin(nb, pinB, &bOut);
    if (!pa || !pb || aOut == bOut)        // need exactly one output + one input
        return;
    bool anchorInvolved = (na->type == kScriptNodeType::Anchor) ||
                          (nb->type == kScriptNodeType::Anchor);
    if (pa->type != pb->type && !anchorInvolved) // strict type match (incl. Exec)
        return;

    int outN, outP, inN, inP;
    kScriptGraphPin *outPin;
    if (aOut) { outN = nodeA; outP = pinA; outPin = pa; inN = nodeB; inP = pinB; }
    else      { outN = nodeB; outP = pinB; outPin = pb; inN = nodeA; inP = pinA; }

    // Input pins accept any number of wires. For data inputs the compiler
    // sums the connected values; for exec inputs the node simply fires from
    // every connected source (each incoming exec wire emits the statement in
    // its own chain). Only an exec output stays single-wire, because the code
    // generator follows the first outgoing exec link when building a chain.
    if (outPin->type == kScriptPinType::Exec)
        graph.removeLinksByPin(outN, outP);

    kScriptGraphLink l;
    l.id       = graph.newId();
    l.fromNode = outN; l.fromPin = outP;
    l.toNode   = inN;  l.toPin   = inP;
    graph.links.push_back(l);
    graph.dirty = true;
}

void PanelLogicGraph::copySelectedNode()
{
    kScriptGraphNode *n = graph.findNode(selectedNode);
    if (!n)
        return;
    clipboardNode = *n;
    hasClipboard   = true;
    logStatus(LogLevel::Info, "Copied " + n->name);
}

void PanelLogicGraph::pasteClipboard()
{
    if (!hasClipboard)
        return;

    kScriptGraphNode n = graph.makeNode(clipboardNode.type,
                                        clipboardNode.posX + 24.0f,
                                        clipboardNode.posY + 24.0f);

    // Carry over the editable payload.
    n.valueStr  = clipboardNode.valueStr;
    n.valueBool = clipboardNode.valueBool;
    for (int i = 0; i < 3; ++i)
        n.valueFloat[i] = clipboardNode.valueFloat[i];
    n.comment = clipboardNode.comment;
    n.sizeX   = clipboardNode.sizeX;
    n.sizeY   = clipboardNode.sizeY;

    // Sequence nodes keep their exec-output count.
    if (n.type == kScriptNodeType::Sequence)
    {
        int srcExec = 0;
        for (const auto &p : clipboardNode.outputs)
            if (p.type == kScriptPinType::Exec)
                ++srcExec;

        int curExec = 0;
        for (const auto &p : n.outputs)
            if (p.type == kScriptPinType::Exec)
                ++curExec;

        while (curExec < srcExec)
        {
            kScriptGraphPin p;
            p.id       = graph.newId();
            p.type     = kScriptPinType::Exec;
            p.isOutput = true;
            n.outputs.push_back(p);
            ++curExec;
        }
        while (curExec > srcExec && curExec > 1)
        {
            n.outputs.pop_back();
            --curExec;
        }
    }

    graph.nodes.push_back(n);
    selectedNode = n.id;
    graph.dirty  = true;
    logStatus(LogLevel::Info, "Pasted " + n.name);
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

void PanelLogicGraph::drawCanvas()
{
    ImGui::BeginChild("##scriptcanvas", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    canvasOrigin    = ImGui::GetCursorScreenPos();
    ImVec2 size     = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y < 1.0f) size.y = 1.0f;
    ImDrawList *dl  = ImGui::GetWindowDrawList();

    // Background + grid.
    dl->AddRectFilled(canvasOrigin,
                      ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + size.y),
                      IM_COL32(28, 29, 33, 255));
    const float grid = 24.0f * canvasZoom;
    for (float x = std::fmod(canvasOffset.x * canvasZoom, grid); x < size.x; x += grid)
        dl->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y),
                    ImVec2(canvasOrigin.x + x, canvasOrigin.y + size.y),
                    IM_COL32(40, 41, 46, 255));
    for (float y = std::fmod(canvasOffset.y * canvasZoom, grid); y < size.y; y += grid)
        dl->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y),
                    ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + y),
                    IM_COL32(40, 41, 46, 255));

    // Canvas-level button: captures panning. Allow overlap so the node
    // header/pin widgets submitted afterwards take input priority over this
    // background button (otherwise the canvas, being submitted first, swallows
    // every click and nodes can't be selected/moved).
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvasbtn", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasActive  = ImGui::IsItemActive();

    // Left-clicking empty canvas space clears the current selection.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selectedNode = 0;

    // Comment boxes behind everything.
    for (auto &n : graph.nodes)
        if (n.type == kScriptNodeType::Comment)
            drawCommentNode(dl, n, canvasOrigin);

    // Links beneath nodes.
    drawLinks(dl);

    // Nodes (also submits their ImGui widgets, drawn on top of the canvas button).
    for (auto &n : graph.nodes)
        if (n.type != kScriptNodeType::Comment)
            drawNode(dl, n, canvasOrigin);

    // ---- Pin hit-testing ---------------------------------------------------
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool overCanvas = (mouse.x >= canvasOrigin.x && mouse.x < canvasOrigin.x + size.x &&
                       mouse.y >= canvasOrigin.y && mouse.y < canvasOrigin.y + size.y);
    int hovNode = 0, hovPin = 0;
    for (auto &n : graph.nodes)
    {
        ImVec2 ns = canvasToScreen(ImVec2(n.posX, n.posY), canvasOrigin);
        auto scan = [&](kScriptGraphPin &p) {
            ImVec2 pp = pinScreenPos(n, p.id, ns, canvasZoom);
            float dx = pp.x - mouse.x, dy = pp.y - mouse.y;
            float hit = PIN_HIT * canvasZoom;
            if (dx * dx + dy * dy <= hit * hit)
            {
                hovNode = n.id;
                hovPin  = p.id;
            }
        };
        for (auto &p : n.inputs)  scan(p);
        for (auto &p : n.outputs) scan(p);
    }

    // Start a link drag from a pin. This takes priority over node movement:
    // the node body button (drawn earlier) may have started a move on the same
    // click, so cancel it when the cursor is actually over a pin. Uses the
    // manual pin hit-test rather than canvas hover, since the node body button
    // now owns ImGui hover over the node.
    if (hovNode && ImGui::IsMouseClicked(0) && !linkDragging)
    {
        kScriptGraphNode *n = graph.findNode(hovNode);
        bool isOut = false;
        if (n && getPin(n, hovPin, &isOut))
        {
            linkDragging   = true;
            dragNode       = hovNode;
            dragPin        = hovPin;
            dragFromOutput = isOut;
            movingNode     = 0; // don't drag the node while wiring a pin
        }
    }

    // Right-click a pin to clear its wires.
    if (hovNode && ImGui::IsMouseClicked(1))
    {
        graph.removeLinksByPin(hovNode, hovPin);
        graph.dirty = true;
    }

    // Finish a link drag.
    if (linkDragging && ImGui::IsMouseReleased(0))
    {
        if (hovNode)
            tryConnect(dragNode, dragPin, hovNode, hovPin);
        linkDragging = false;
    }

    // Live drag wire.
    if (linkDragging)
    {
        kScriptGraphNode *n = graph.findNode(dragNode);
        if (n)
        {
            ImVec2 src = pinScreenPos(*n, dragPin,
                                      canvasToScreen(ImVec2(n->posX, n->posY), canvasOrigin),
                                      canvasZoom);
            if (dragFromOutput)
                drawWire(dl, src, mouse, IM_COL32(255, 220, 120, 255), 2.4f * canvasZoom);
            else
                drawWire(dl, mouse, src, IM_COL32(255, 220, 120, 255), 2.4f * canvasZoom);
        }
        else
        {
            linkDragging = false;
        }
    }

    // Pan with a middle-button drag. Uses a persistent state (same pattern as
    // the shader-graph editor) so the view keeps panning even when the cursor
    // is over a node body — the node's InvisibleButton only owns the left
    // button, so it must not gate the pan.
    if (overCanvas || isPanning)
    {
        bool panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (panButton && !linkDragging && movingNode == 0 && resizingComment == 0)
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
        else
        {
            isPanning = false;
        }
    }

    // Scroll to zoom, anchored at the mouse position.
    ImGuiIO &io = ImGui::GetIO();
    if (overCanvas && io.MouseWheel != 0.0f)
    {
        float prevZoom = canvasZoom;
        canvasZoom = std::max(0.25f, std::min(2.0f, canvasZoom + io.MouseWheel * 0.1f));
        ImVec2 mouseScreen = mouse - canvasOrigin;
        canvasOffset.x += mouseScreen.x * (1.0f / canvasZoom - 1.0f / prevZoom);
        canvasOffset.y += mouseScreen.y * (1.0f / canvasZoom - 1.0f / prevZoom);
    }

    // Delete the selected node with the Delete key.
    if (ImGui::IsWindowFocused() && selectedNode != 0 &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        graph.removeNode(selectedNode);
        selectedNode = 0;
        graph.dirty  = true;
    }

    // Copy / paste the selected node.
    if (ImGui::IsWindowFocused())
    {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && selectedNode != 0)
            copySelectedNode();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && hasClipboard)
            pasteClipboard();
    }

    // Right-click (not on a pin) opens the add-node menu. A manual bounds check
    // is used so comment boxes and node bodies don't block it.
    static ImVec2 ctxSpawn(0.0f, 0.0f);
    if (hovNode == 0 && overCanvas && ImGui::IsMouseClicked(1))
    {
        ctxSpawn = screenToCanvas(mouse, canvasOrigin);
        ImGui::OpenPopup("##addnodemenu");
    }
    if (ImGui::BeginPopup("##addnodemenu"))
    {
        drawAddNodeMenu(ctxSpawn);
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Variable-node pin sync
// ---------------------------------------------------------------------------

void PanelLogicGraph::syncVariableNodePins()
{
    for (auto &n : graph.nodes)
    {
        if (n.type != kScriptNodeType::GetVariable &&
            n.type != kScriptNodeType::SetVariable)
            continue;

        kScriptPinType target = kScriptPinType::Float;
        for (const auto &v : graph.variables)
        {
            if (v.name == n.valueStr)
            {
                target = kScriptVarTypePin(v.type);
                break;
            }
        }

        for (auto &p : n.inputs)
        {
            if (p.type == kScriptPinType::Exec)
                continue;
            if (p.type != target)
            {
                graph.removeLinksByPin(n.id, p.id);
                p.type = target;
                graph.dirty = true;
            }
        }
        for (auto &p : n.outputs)
        {
            if (p.type == kScriptPinType::Exec)
                continue;
            if (p.type != target)
            {
                graph.removeLinksByPin(n.id, p.id);
                p.type = target;
                graph.dirty = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Panel entry point
// ---------------------------------------------------------------------------

void PanelLogicGraph::draw(bool &isOpened)
{
    if (!isOpened)
        return;

    ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Logic Graph", &isOpened, ImGuiWindowFlags_NoScrollbar))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Keep the input-action picker in sync with Project Settings. The re-read is
    // throttled so project.json isn't opened on every frame.
    if (--inputRefreshCounter <= 0)
    {
        inputRefreshCounter = 60;
        if (manager)
            manager->loadInputSettings();
    }

    drawToolbar();
    drawVariablesPanel();
    syncVariableNodePins();
    drawVariablesSplitter();
    drawCanvas();

    ImGui::End();
}
