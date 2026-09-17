#include "panel_gui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ===========================================================================
// Small helpers
// ===========================================================================

static ImU32 colorU32(const float c[4])
{
    return IM_COL32((int)(ImClamp(c[0], 0.0f, 1.0f) * 255.0f),
                    (int)(ImClamp(c[1], 0.0f, 1.0f) * 255.0f),
                    (int)(ImClamp(c[2], 0.0f, 1.0f) * 255.0f),
                    (int)(ImClamp(c[3], 0.0f, 1.0f) * 255.0f));
}

static void copyToBuf(char* dst, size_t n, const std::string& src)
{
    if (n == 0) return;
    size_t len = src.size() < (n - 1) ? src.size() : (n - 1);
    if (len > 0) memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

static char asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static std::string toLower(std::string s)
{
    for (char& c : s)
        c = asciiLower(c);
    return s;
}

static bool containsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

/**
 * @brief True when a project image asset was imported with the "GUI" image type.
 *
 * Mirrors the Image Type combo in the Inspector
 * (0 = Texture, 1 = GUI, 2 = Sprite, 3 = Normal Map) and reads the value from
 * Library/Metadata/<uuid>.json. Used to filter the .ui texture picker so only
 * UI-configured images are offered.
 */
static bool isGuiImageAsset(const fs::path& projectPath, const kString& assetUuid)
{
    if (assetUuid.empty())
        return false;
    std::ifstream f(projectPath / "Library" / "Metadata" / (assetUuid + ".json"));
    if (!f.is_open())
        return false;
    try
    {
        json j;
        f >> j;
        return j.value("imageType", 0) == 1; // 1 = "GUI"
    }
    catch (...)
    {
        return false;
    }
}

// ===========================================================================
// Widget type metadata
// ===========================================================================

const char* guiWidgetTypeName(GuiWidgetType t)
{
    switch (t)
    {
        case GuiWidgetType::Text:        return "Text";
        case GuiWidgetType::Button:      return "Button";
        case GuiWidgetType::Panel:       return "Panel";
        case GuiWidgetType::Image:       return "Image";
        case GuiWidgetType::Spacer:      return "Spacer";
        case GuiWidgetType::Line:        return "Line";
        case GuiWidgetType::ScrollView:  return "Scroll View";
        case GuiWidgetType::ProgressBar: return "Progress Bar";
        case GuiWidgetType::Toggle:      return "Toggle";
        case GuiWidgetType::Slider:      return "Slider";
        case GuiWidgetType::InputField:  return "Input Field";
    }
    return "Widget";
}

bool guiWidgetTypeIsContainer(GuiWidgetType t)
{
    return t == GuiWidgetType::Panel || t == GuiWidgetType::ScrollView;
}

const std::vector<GuiWidgetType>& guiWidgetTypes()
{
    static const std::vector<GuiWidgetType> types = {
        GuiWidgetType::Text,
        GuiWidgetType::Button,
        GuiWidgetType::Panel,
        GuiWidgetType::Image,
        GuiWidgetType::Spacer,
        GuiWidgetType::Line,
        GuiWidgetType::ScrollView,
        GuiWidgetType::ProgressBar,
        GuiWidgetType::Toggle,
        GuiWidgetType::Slider,
        GuiWidgetType::InputField
    };
    return types;
}

// Stable identifiers written to .ui files (independent of display names).
static const char* widgetTypeKey(GuiWidgetType t)
{
    switch (t)
    {
        case GuiWidgetType::Text:        return "Text";
        case GuiWidgetType::Button:      return "Button";
        case GuiWidgetType::Panel:       return "Panel";
        case GuiWidgetType::Image:       return "Image";
        case GuiWidgetType::Spacer:      return "Spacer";
        case GuiWidgetType::Line:        return "Line";
        case GuiWidgetType::ScrollView:  return "ScrollView";
        case GuiWidgetType::ProgressBar: return "ProgressBar";
        case GuiWidgetType::Toggle:      return "Toggle";
        case GuiWidgetType::Slider:      return "Slider";
        case GuiWidgetType::InputField:  return "InputField";
    }
    return "Panel";
}

static GuiWidgetType widgetTypeFromKey(const std::string& key)
{
    for (GuiWidgetType t : guiWidgetTypes())
        if (key == widgetTypeKey(t)) return t;
    return GuiWidgetType::Panel;
}

static const char* kAnchorNames[] = {
    "Top Left", "Top", "Top Right",
    "Middle Left", "Center", "Middle Right",
    "Bottom Left", "Bottom", "Bottom Right"
};
static const char* kTextAlignNames[] = { "Left", "Center", "Right" };
static const char* kImageModeNames[] = { "Simple", "Sliced", "Tiled", "Filled" };
static const char* kImageFitNames[]  = { "Cover", "Contain", "Fill", "Scale Down", "None" };
static const char* kScrollDirNames[]  = { "Vertical", "Horizontal", "Both" };

const char* guiImageFitName(GuiImageFit f)
{
    const int i = (int)f;
    if (i < 0 || i >= (int)(sizeof(kImageFitNames) / sizeof(kImageFitNames[0])))
        return "Fill";
    return kImageFitNames[i];
}

/**
 * @brief Destination rect for a texture inside a widget rect, per fit mode.
 *
 * Sizes are in the same space as @p rectMin / @p rectMax (screen pixels);
 * @p texSize is the texture's pixel size. Cover and None may return a rect
 * larger than the widget — callers clip to the widget rect in those two modes.
 */
static void computeGuiImageRect(GuiImageFit fit, ImVec2 rectMin, ImVec2 rectMax,
                                ImVec2 texSize, ImVec2& outMin, ImVec2& outMax)
{
    const float rw = rectMax.x - rectMin.x;
    const float rh = rectMax.y - rectMin.y;
    if (rw <= 0.0f || rh <= 0.0f || texSize.x <= 0.0f || texSize.y <= 0.0f)
    {
        outMin = rectMin;
        outMax = rectMax;
        return;
    }

    if (fit == GuiImageFit::Fill)
    {
        outMin = rectMin;
        outMax = rectMax;
        return;
    }

    float s;
    if (fit == GuiImageFit::None)
    {
        s = 1.0f; // native pixel size, centred
    }
    else
    {
        const float sx = rw / texSize.x;
        const float sy = rh / texSize.y;
        s = (fit == GuiImageFit::Cover) ? ImMax(sx, sy) : ImMin(sx, sy);
        if (fit == GuiImageFit::ScaleDown)
            s = ImMin(s, 1.0f);
    }

    const float dw = texSize.x * s;
    const float dh = texSize.y * s;
    outMin = ImVec2(rectMin.x + (rw - dw) * 0.5f, rectMin.y + (rh - dh) * 0.5f);
    outMax = ImVec2(outMin.x + dw, outMin.y + dh);
}

static GuiWidgetType widgetTypeFromDisplayName(const char* name)
{
    for (GuiWidgetType t : guiWidgetTypes())
        if (strcmp(name, guiWidgetTypeName(t)) == 0) return t;
    return GuiWidgetType::Panel;
}

// ===========================================================================
// GuiLayout
// ===========================================================================

GuiWidget* GuiLayout::findWidget(int id)
{
    for (auto& w : widgets)
        if (w.id == id) return &w;
    return nullptr;
}

const GuiWidget* GuiLayout::findWidget(int id) const
{
    for (const auto& w : widgets)
        if (w.id == id) return &w;
    return nullptr;
}

std::vector<int> GuiLayout::rootIds() const
{
    std::vector<int> roots;
    for (const auto& w : widgets)
        if (w.parentId < 0) roots.push_back(w.id);
    return roots;
}

int GuiLayout::addWidget(GuiWidgetType type, int parentId)
{
    GuiWidget w;
    w.id       = newId();
    w.type     = type;
    w.parentId = parentId;
    w.name     = guiWidgetTypeName(type);

    const bool child = (parentId >= 0);
    w.x = child ? 8.0f : 24.0f;
    w.y = child ? 8.0f : 24.0f;

    switch (type)
    {
        case GuiWidgetType::Text:
            w.width = 220.0f; w.height = 32.0f;
            w.text = "New Text"; w.fontSize = 20.0f;
            w.textAlign = GuiTextAlign::Left;
            w.fillBackground = false;
            break;
        case GuiWidgetType::Button:
            w.width = 200.0f; w.height = 48.0f;
            w.text = "Button"; w.fontSize = 18.0f;
            w.cornerRadius = 4.0f; w.showBorder = true;
            w.color[0] = 0.22f; w.color[1] = 0.40f; w.color[2] = 0.75f;
            break;
        case GuiWidgetType::Panel:
            w.width = 420.0f; w.height = 260.0f;
            w.cornerRadius = 6.0f; w.showBorder = true;
            w.color[0] = 0.16f; w.color[1] = 0.17f; w.color[2] = 0.20f; w.color[3] = 0.90f;
            break;
        case GuiWidgetType::Image:
            w.width = 200.0f; w.height = 200.0f;
            w.color[0] = 0.30f; w.color[1] = 0.31f; w.color[2] = 0.35f; w.color[3] = 1.00f;
            break;
        case GuiWidgetType::Spacer:
            w.width = 80.0f; w.height = 40.0f;
            w.fillBackground = false;
            break;
        case GuiWidgetType::Line:
            w.width = 220.0f; w.height = 2.0f;
            w.color[0] = 0.55f; w.color[1] = 0.56f; w.color[2] = 0.60f; w.color[3] = 1.00f;
            break;
        case GuiWidgetType::ScrollView:
            w.width = 340.0f; w.height = 240.0f;
            w.cornerRadius = 4.0f; w.showBorder = true;
            w.color[0] = 0.12f; w.color[1] = 0.13f; w.color[2] = 0.15f; w.color[3] = 0.92f;
            break;
        case GuiWidgetType::ProgressBar:
            w.width = 240.0f; w.height = 24.0f;
            w.cornerRadius = 4.0f; w.showBorder = true;
            w.value = 0.65f;
            break;
        case GuiWidgetType::Toggle:
            w.width = 44.0f; w.height = 24.0f;
            w.cornerRadius = 12.0f;
            break;
        case GuiWidgetType::Slider:
            w.width = 240.0f; w.height = 24.0f;
            w.value = 0.50f;
            break;
        case GuiWidgetType::InputField:
            w.width = 240.0f; w.height = 34.0f;
            w.text = "Enter text..."; w.fontSize = 18.0f;
            w.textAlign = GuiTextAlign::Left;
            w.showBorder = true; w.cornerRadius = 4.0f;
            break;
    }

    // Link into the parent (or the canvas root list).
    if (parentId >= 0)
    {
        if (GuiWidget* parent = findWidget(parentId))
            parent->children.push_back(w.id);
        else
            w.parentId = -1;
    }

    widgets.push_back(w);
    return w.id;
}

void GuiLayout::removeWidget(int id)
{
    // Collect the subtree (breadth-first) so nested children are removed too.
    std::vector<int> toRemove = { id };
    for (size_t i = 0; i < toRemove.size(); ++i)
    {
        if (const GuiWidget* w = findWidget(toRemove[i]))
            for (int c : w->children)
                toRemove.push_back(c);
    }

    // Detach from the parent's child list.
    if (const GuiWidget* w = findWidget(id))
    {
        if (w->parentId >= 0)
        {
            if (GuiWidget* parent = findWidget(w->parentId))
                parent->children.erase(std::remove(parent->children.begin(),
                                                    parent->children.end(), id),
                                       parent->children.end());
        }
    }

    widgets.erase(std::remove_if(widgets.begin(), widgets.end(),
        [&](const GuiWidget& w)
        {
            return std::find(toRemove.begin(), toRemove.end(), w.id) != toRemove.end();
        }),
        widgets.end());
}

bool GuiLayout::reparentWidget(int id, int newParentId)
{
    if (id == newParentId) return false;

    GuiWidget* w = findWidget(id);
    if (!w) return false;

    // Refuse to nest a widget inside its own descendant (would orphan a cycle).
    for (int c : w->children)
    {
        if (c == newParentId) return false;
        if (const GuiWidget* cw = findWidget(c))
        {
            // Depth-first check of the child subtree.
            std::vector<int> stack = { c };
            while (!stack.empty())
            {
                int cur = stack.back(); stack.pop_back();
                if (cur == newParentId) return false;
                if (const GuiWidget* n = findWidget(cur))
                    for (int cc : n->children) stack.push_back(cc);
            }
        }
    }

    if (w->parentId >= 0)
    {
        if (GuiWidget* oldParent = findWidget(w->parentId))
            oldParent->children.erase(std::remove(oldParent->children.begin(),
                                                  oldParent->children.end(), id),
                                      oldParent->children.end());
    }

    w->parentId = newParentId;
    if (newParentId >= 0)
    {
        if (GuiWidget* parent = findWidget(newParentId))
            parent->children.push_back(id);
    }
    return true;
}

nlohmann::json GuiLayout::toJson() const
{
    json j;
    j["uuid"] = uuid;
    j["name"] = name;
    j["version"] = 1;
    j["canvasWidth"]  = canvasWidth;
    j["canvasHeight"] = canvasHeight;
    j["canvasColor"]  = { canvasColor[0], canvasColor[1], canvasColor[2], canvasColor[3] };
    j["showCanvasGrid"] = showCanvasGrid;
    j["nextId"] = nextId;

    json arr = json::array();
    for (const auto& w : widgets)
    {
        json jw;
        jw["id"]       = w.id;
        jw["parentId"] = w.parentId;
        jw["type"]     = widgetTypeKey(w.type);
        jw["name"]     = w.name;

        jw["x"] = w.x; jw["y"] = w.y;
        jw["width"] = w.width; jw["height"] = w.height;
        jw["anchor"] = (int)w.anchor;
        jw["rotation"] = w.rotation;
        jw["scale"] = w.scale;
        jw["visible"] = w.visible;
        jw["interactable"] = w.interactable;

        jw["color"]       = { w.color[0], w.color[1], w.color[2], w.color[3] };
        jw["textColor"]   = { w.textColor[0], w.textColor[1], w.textColor[2], w.textColor[3] };
        jw["borderColor"] = { w.borderColor[0], w.borderColor[1], w.borderColor[2], w.borderColor[3] };
        jw["borderWidth"]  = w.borderWidth;
        jw["cornerRadius"] = w.cornerRadius;
        jw["fillBackground"] = w.fillBackground;
        jw["showBorder"]     = w.showBorder;

        jw["sliceLeft"]   = w.sliceLeft;
        jw["sliceRight"]  = w.sliceRight;
        jw["sliceTop"]    = w.sliceTop;
        jw["sliceBottom"] = w.sliceBottom;
        jw["showCorners"] = w.showCorners;

        jw["text"]      = w.text;
        jw["fontSize"]  = w.fontSize;
        jw["textAlign"] = (int)w.textAlign;
        jw["wordWrap"]  = w.wordWrap;

        jw["textureUuid"] = w.textureUuid;
        jw["imageMode"]   = (int)w.imageMode;
        jw["imageFit"]    = (int)w.imageFit;
        jw["fillAmount"]  = w.fillAmount;

        jw["scrollDirection"] = (int)w.scrollDirection;
        jw["showScrollbar"]   = w.showScrollbar;
        jw["scrollPosition"]  = w.scrollPosition;

        jw["minValue"] = w.minValue;
        jw["maxValue"] = w.maxValue;
        jw["value"]    = w.value;
        jw["checked"]  = w.checked;
        jw["vertical"] = w.vertical;

        arr.push_back(jw);
    }
    j["widgets"] = arr;
    return j;
}

void GuiLayout::fromJson(const nlohmann::json& j)
{
    *this = GuiLayout{};
    uuid = j.value("uuid", std::string());
    name = j.value("name", std::string());
    canvasWidth  = j.value("canvasWidth", 1920);
    canvasHeight = j.value("canvasHeight", 1080);
    if (canvasWidth  < 1) canvasWidth  = 1920;
    if (canvasHeight < 1) canvasHeight = 1080;
    if (j.contains("canvasColor") && j["canvasColor"].is_array() && j["canvasColor"].size() == 4)
        for (int i = 0; i < 4; ++i) canvasColor[i] = j["canvasColor"][i].get<float>();
    showCanvasGrid = j.value("showCanvasGrid", true);
    nextId = j.value("nextId", 1);

    if (j.contains("widgets") && j["widgets"].is_array())
    {
        for (const auto& jw : j["widgets"])
        {
            GuiWidget w;
            w.id       = jw.value("id", -1);
            w.parentId = jw.value("parentId", -1);
            w.type     = widgetTypeFromKey(jw.value("type", std::string("Panel")));
            w.name     = jw.value("name", std::string("Widget"));

            w.x = jw.value("x", 0.0f); w.y = jw.value("y", 0.0f);
            w.width = jw.value("width", 200.0f); w.height = jw.value("height", 100.0f);
            w.anchor = (GuiAnchor)ImClamp(jw.value("anchor", 0), 0, 8);
            w.rotation = jw.value("rotation", 0.0f);
            w.scale = jw.value("scale", 1.0f);
            w.visible = jw.value("visible", true);
            w.interactable = jw.value("interactable", true);

            auto readCol = [&](const char* key, float out[4])
            {
                if (jw.contains(key) && jw[key].is_array() && jw[key].size() == 4)
                    for (int i = 0; i < 4; ++i) out[i] = jw[key][i].get<float>();
            };
            readCol("color", w.color);
            readCol("textColor", w.textColor);
            readCol("borderColor", w.borderColor);

            w.borderWidth  = jw.value("borderWidth", 0.0f);
            w.cornerRadius = jw.value("cornerRadius", 0.0f);
            w.fillBackground = jw.value("fillBackground", true);
            w.showBorder     = jw.value("showBorder", false);

            w.sliceLeft   = jw.value("sliceLeft", 8.0f);
            w.sliceRight  = jw.value("sliceRight", 8.0f);
            w.sliceTop    = jw.value("sliceTop", 8.0f);
            w.sliceBottom = jw.value("sliceBottom", 8.0f);
            w.showCorners = jw.value("showCorners", true);

            w.text     = jw.value("text", std::string("Text"));
            w.fontSize = jw.value("fontSize", 16.0f);
            w.textAlign = (GuiTextAlign)ImClamp(jw.value("textAlign", 1), 0, 2);
            w.wordWrap = jw.value("wordWrap", false);

            w.textureUuid = jw.value("textureUuid", std::string());
            w.imageMode   = (GuiImageMode)ImClamp(jw.value("imageMode", 0), 0, 3);
            // 2 = "Fill" (the authoring default) for layouts saved before this field existed.
            w.imageFit    = (GuiImageFit)ImClamp(jw.value("imageFit", 2), 0, 4);
            w.fillAmount  = jw.value("fillAmount", 1.0f);

            w.scrollDirection = (GuiScrollDirection)ImClamp(jw.value("scrollDirection", 0), 0, 2);
            w.showScrollbar   = jw.value("showScrollbar", true);
            w.scrollPosition  = jw.value("scrollPosition", 0.0f);

            w.minValue = jw.value("minValue", 0.0f);
            w.maxValue = jw.value("maxValue", 1.0f);
            w.value    = jw.value("value", 0.5f);
            w.checked  = jw.value("checked", true);
            w.vertical = jw.value("vertical", false);

            widgets.push_back(w);
        }
    }

    // Rebuild child lists and make sure the id counter stays ahead.
    for (auto& w : widgets) w.children.clear();
    for (auto& w : widgets)
    {
        if (w.parentId >= 0)
        {
            if (GuiWidget* parent = findWidget(w.parentId))
                parent->children.push_back(w.id);
            else
                w.parentId = -1;
        }
        if (w.id >= nextId) nextId = w.id + 1;
    }

    dirty = false;
}

// ===========================================================================
// PanelGui — construction / file I/O
// ===========================================================================

PanelGui::PanelGui(kGuiManager* setGui, Manager* setManager)
    : gui(setGui), manager(setManager)
{
    newLayout();
}

std::string PanelGui::generateUuid()
{
    using namespace std::chrono;
    auto seed = (uint64_t)duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;
    auto r1 = dist(rng), r2 = dist(rng);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx",
             (unsigned long long)r1, (unsigned long long)r2);
    return std::string(buf);
}

void PanelGui::newLayout()
{
    layout = GuiLayout{};
    layout.uuid = generateUuid();
    layout.name = "NewIngameUI";
    layout.dirty = false;
    filePath.clear();

    selectedWidget   = -1;
    dragWidget       = -1;
    isDraggingWidget = false;
    isPanning        = false;
    canvasZoom       = 1.0f;
    canvasOffset     = ImVec2(0.0f, 0.0f);

    // Start with a full-screen panel so there is something to edit.
    int panelId = layout.addWidget(GuiWidgetType::Panel, -1);
    if (GuiWidget* p = layout.findWidget(panelId))
    {
        p->name = "Root Panel";
        p->x = 0.0f; p->y = 0.0f;
        p->width  = (float)layout.canvasWidth;
        p->height = (float)layout.canvasHeight;
        p->anchor = GuiAnchor::TopLeft;
        p->cornerRadius = 0.0f;
        p->color[3] = 0.0f; // fully transparent background by default
    }
}

void PanelGui::openFile(const std::string& path)
{
    loadLayout(path);
}

void PanelGui::loadLayout(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try
    {
        json j; f >> j;
        layout.fromJson(j);
        filePath = path;
        layout.name = fs::path(path).stem().string();
        layout.dirty = false;
        selectedWidget   = -1;
        dragWidget       = -1;
        isDraggingWidget = false;
        isPanning        = false;
        canvasZoom       = 1.0f;
        canvasOffset     = ImVec2(0.0f, 0.0f);
    }
    catch (...) {}
}

void PanelGui::saveLayout()
{
    if (filePath.empty()) { saveLayoutAs(); return; }

    layout.name = fs::path(filePath).stem().string();
    layout.dirty = false;

    json j = layout.toJson();
    std::ofstream f(filePath);
    if (!f.is_open()) return;
    f << j.dump(4);
}

void SDLCALL PanelGui::saveGuiCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    if (!filelist || !*filelist) return;
    PanelGui* self = static_cast<PanelGui*>(userdata);

    std::string path = filelist[0];
    if (path.size() < 3 || path.substr(path.size() - 3) != ".ui")
        path += ".ui";

    self->filePath = path;
    self->saveLayout();
}

void PanelGui::saveLayoutAs()
{
    if (!manager->projectOpened) return;

    fs::path assetsDir = fs::path(manager->projectPath.c_str()) / "Assets" / "UI";
    fs::create_directories(assetsDir);

    std::string defaultName = (layout.name.empty() ? "NewIngameUI" : layout.name) + ".ui";

    SDL_DialogFileFilter filters[] = {
        { "UI files", "ui"  },
        { "All files", "*"  }
    };

    SDL_ShowSaveFileDialog(
        saveGuiCallback,
        this,
        manager->getWindow()->getSdlWindow(),
        filters,
        SDL_arraysize(filters),
        (assetsDir / defaultName).string().c_str()
    );
}

// ===========================================================================
// Geometry
// ===========================================================================

ImVec2 PanelGui::widgetRefPos(const GuiWidget& w, ImVec2 parentRefPos, ImVec2 parentSize) const
{
    float ax = 0.0f, ay = 0.0f;
    switch (w.anchor)
    {
        case GuiAnchor::TopLeft:      ax = 0.0f; ay = 0.0f; break;
        case GuiAnchor::Top:          ax = 0.5f; ay = 0.0f; break;
        case GuiAnchor::TopRight:     ax = 1.0f; ay = 0.0f; break;
        case GuiAnchor::MiddleLeft:   ax = 0.0f; ay = 0.5f; break;
        case GuiAnchor::MiddleCenter: ax = 0.5f; ay = 0.5f; break;
        case GuiAnchor::MiddleRight:  ax = 1.0f; ay = 0.5f; break;
        case GuiAnchor::BottomLeft:   ax = 0.0f; ay = 1.0f; break;
        case GuiAnchor::Bottom:       ax = 0.5f; ay = 1.0f; break;
        case GuiAnchor::BottomRight:  ax = 1.0f; ay = 1.0f; break;
    }
    return ImVec2(parentRefPos.x + ax * parentSize.x + w.x - ax * w.width,
                  parentRefPos.y + ay * parentSize.y + w.y - ay * w.height);
}

// ===========================================================================
// Preview rendering
// ===========================================================================

void PanelGui::drawWidget(ImDrawList* dl, const GuiWidget& w,
                          ImVec2 parentRefPos, ImVec2 parentSize)
{
    if (!w.visible) return;

    const float scale = previewScale;
    const ImVec2 refPos = widgetRefPos(w, parentRefPos, parentSize);
    const ImVec2 p0 = refToScreen(refPos);
    const ImVec2 p1 = refToScreen(ImVec2(refPos.x + w.width, refPos.y + w.height));

    const float cr = w.cornerRadius * scale;
    const float bw = ImMax(1.0f, w.borderWidth * scale);
    const ImU32 bgCol     = colorU32(w.color);
    const ImU32 borderCol = colorU32(w.borderColor);
    const ImU32 textCol   = colorU32(w.textColor);

    const float fontPx = ImMax(1.0f, w.fontSize * scale);
    const float fscale = fontPx / ImGui::GetFontSize();
    const float pad    = ImMax(2.0f, 6.0f * scale);

    auto textSize = [&](const std::string& s) -> ImVec2
    {
        ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        return ImVec2(ts.x * fscale, ts.y * fscale);
    };

    auto drawTextAligned = [&](const std::string& s, ImU32 col)
    {
        if (s.empty()) return;
        ImVec2 ts = textSize(s);
        float tx = p0.x + pad;
        if (w.textAlign == GuiTextAlign::Center) tx = p0.x + ((p1.x - p0.x) - ts.x) * 0.5f;
        else if (w.textAlign == GuiTextAlign::Right) tx = p1.x - pad - ts.x;
        float ty = p0.y + ((p1.y - p0.y) - ts.y) * 0.5f;
        dl->AddText(NULL, fontPx, ImVec2(tx, ty), col, s.c_str());
    };

    auto drawSolid = [&]()
    {
        if (w.fillBackground)
            dl->AddRectFilled(p0, p1, bgCol, cr);
        if (w.showBorder)
            dl->AddRect(p0, p1, borderCol, cr, 0, bw);
    };

    // Draws the widget's texture (when one is assigned) fitted inside p0..p1
    // according to the widget's image fit mode. Returns true when a texture was
    // actually drawn.
    auto drawTextureFitted = [&]() -> bool
    {
        if (w.textureUuid.empty())
            return false;
        GuiTextureInfo tex = resolveWidgetTexture(w.textureUuid);
        if (tex.glId == 0)
            return false;

        ImVec2 dmin, dmax;
        computeGuiImageRect(w.imageFit, p0, p1, tex.size, dmin, dmax);

        // Cover / None can overflow the widget — crop to its rect.
        const bool clip = (w.imageFit == GuiImageFit::Cover || w.imageFit == GuiImageFit::None);
        if (clip) dl->PushClipRect(p0, p1, true);
        dl->AddImage((ImTextureRef)(intptr_t)tex.glId, dmin, dmax);
        if (clip) dl->PopClipRect();
        return true;
    };

    // Draws a placeholder "image" (checkerboard-ish cross + label).
    auto drawImagePlaceholder = [&](const char* label)
    {
        if (w.fillBackground) dl->AddRectFilled(p0, p1, bgCol, cr);
        if (w.showBorder)     dl->AddRect(p0, p1, borderCol, cr, 0, bw);
        dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), IM_COL32(255,255,255,40), ImMax(1.0f, scale));
        dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), IM_COL32(255,255,255,40), ImMax(1.0f, scale));
        if (w.imageMode == GuiImageMode::Sliced)
        {
            float sl = w.sliceLeft * scale, sr = w.sliceRight * scale;
            float st = w.sliceTop  * scale, sb = w.sliceBottom * scale;
            ImU32 guide = IM_COL32(255, 220, 120, 90);
            dl->AddLine(ImVec2(p0.x + sl, p0.y), ImVec2(p0.x + sl, p1.y), guide);
            dl->AddLine(ImVec2(p1.x - sr, p0.y), ImVec2(p1.x - sr, p1.y), guide);
            dl->AddLine(ImVec2(p0.x, p0.y + st), ImVec2(p1.x, p0.y + st), guide);
            dl->AddLine(ImVec2(p0.x, p1.y - sb), ImVec2(p1.x, p1.y - sb), guide);
        }
        std::string lbl = label;
        if (!w.textureUuid.empty()) lbl += " (" + w.textureUuid.substr(0, 8) + ")";
        ImVec2 ts = textSize(lbl);
        dl->AddText(NULL, ImMax(1.0f, 13.0f * scale),
                    ImVec2(p0.x + ((p1.x - p0.x) - ts.x) * 0.5f,
                           p0.y + ((p1.y - p0.y) - ts.y) * 0.5f),
                    IM_COL32(255,255,255,140), lbl.c_str());
    };

    switch (w.type)
    {
        case GuiWidgetType::Text:
        {
            if (w.fillBackground) dl->AddRectFilled(p0, p1, bgCol, cr);
            if (w.showBorder)     dl->AddRect(p0, p1, borderCol, cr, 0, bw);
            drawTextAligned(w.text, textCol);
            break;
        }
        case GuiWidgetType::Button:
        {
            drawSolid();
            drawTextureFitted();
            drawTextAligned(w.text, textCol);
            break;
        }
        case GuiWidgetType::Panel:
        {
            drawSolid();
            if (w.showCorners)
            {
                float sl = w.sliceLeft * scale, sr = w.sliceRight * scale;
                float st = w.sliceTop  * scale, sb = w.sliceBottom * scale;
                ImU32 cc = IM_COL32(255, 200, 80, 170);
                float t = ImMax(1.0f, 1.5f * scale);
                // Top-left
                dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x + sl, p0.y), cc, t);
                dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p0.y + st), cc, t);
                // Top-right
                dl->AddLine(ImVec2(p1.x - sr, p0.y), ImVec2(p1.x, p0.y), cc, t);
                dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p0.y + st), cc, t);
                // Bottom-left
                dl->AddLine(ImVec2(p0.x, p1.y - sb), ImVec2(p0.x, p1.y), cc, t);
                dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p0.x + sl, p1.y), cc, t);
                // Bottom-right
                dl->AddLine(ImVec2(p1.x - sr, p1.y), ImVec2(p1.x, p1.y), cc, t);
                dl->AddLine(ImVec2(p1.x, p1.y - sb), ImVec2(p1.x, p1.y), cc, t);
            }
            break;
        }
        case GuiWidgetType::Image:
        {
            if (!drawTextureFitted())
                drawImagePlaceholder("Image");
            break;
        }
        case GuiWidgetType::Spacer:
        {
            ImU32 col = IM_COL32(140, 140, 150, 70);
            const float dash = ImMax(3.0f, 6.0f * scale);
            auto dashedLine = [&](ImVec2 a, ImVec2 b)
            {
                float len = (b.x - a.x) + (b.y - a.y);
                float step = dash * 2.0f;
                for (float t = 0; t < len; t += step)
                {
                    float e = ImMin(t + dash, len);
                    ImVec2 s = a, u = a;
                    if (a.y == b.y) { s.x = a.x + t; u.x = a.x + e; }
                    else            { s.y = a.y + t; u.y = a.y + e; }
                    dl->AddLine(s, u, col, ImMax(1.0f, scale));
                }
            };
            dashedLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y));
            dashedLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y));
            dashedLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p1.y));
            dashedLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p1.y));
            ImVec2 ts = textSize("Spacer");
            dl->AddText(NULL, ImMax(1.0f, 12.0f * scale),
                        ImVec2(p0.x + ((p1.x - p0.x) - ts.x) * 0.5f,
                               p0.y + ((p1.y - p0.y) - ts.y) * 0.5f),
                        IM_COL32(170,170,180,160), "Spacer");
            break;
        }
        case GuiWidgetType::Line:
        {
            ImU32 c = colorU32(w.color);
            if (w.vertical)
            {
                float cx = (p0.x + p1.x) * 0.5f;
                float t = ImMax(1.0f, w.width * scale);
                dl->AddRectFilled(ImVec2(cx - t * 0.5f, p0.y), ImVec2(cx + t * 0.5f, p1.y), c);
            }
            else
            {
                float cy = (p0.y + p1.y) * 0.5f;
                float t = ImMax(1.0f, w.height * scale);
                dl->AddRectFilled(ImVec2(p0.x, cy - t * 0.5f), ImVec2(p1.x, cy + t * 0.5f), c);
            }
            break;
        }
        case GuiWidgetType::ScrollView:
        {
            drawSolid();
            bool vert = (w.scrollDirection == GuiScrollDirection::Vertical ||
                         w.scrollDirection == GuiScrollDirection::Both);
            bool horz = (w.scrollDirection == GuiScrollDirection::Horizontal ||
                         w.scrollDirection == GuiScrollDirection::Both);
            if (w.showScrollbar && vert)
            {
                float gw = ImMax(4.0f, 7.0f * scale);
                dl->AddRectFilled(ImVec2(p1.x - gw, p0.y), ImVec2(p1.x, p1.y), IM_COL32(0,0,0,90));
                float hs = (p1.y - p0.y) * 0.35f;
                float hy = p0.y + ((p1.y - p0.y) - hs) * ImClamp(w.scrollPosition, 0.0f, 1.0f);
                dl->AddRectFilled(ImVec2(p1.x - gw, hy), ImVec2(p1.x, hy + hs), IM_COL32(160,160,170,200), 3.0f);
            }
            if (w.showScrollbar && horz)
            {
                float gh = ImMax(4.0f, 7.0f * scale);
                dl->AddRectFilled(ImVec2(p0.x, p1.y - gh), ImVec2(p1.x, p1.y), IM_COL32(0,0,0,90));
                float ws = (p1.x - p0.x) * 0.35f;
                float wx = p0.x + ((p1.x - p0.x) - ws) * ImClamp(w.scrollPosition, 0.0f, 1.0f);
                dl->AddRectFilled(ImVec2(wx, p1.y - gh), ImVec2(wx + ws, p1.y), IM_COL32(160,160,170,200), 3.0f);
            }
            break;
        }
        case GuiWidgetType::ProgressBar:
        {
            float frac = (w.maxValue > w.minValue)
                             ? (w.value - w.minValue) / (w.maxValue - w.minValue) : 0.0f;
            frac = ImClamp(frac, 0.0f, 1.0f);
            dl->AddRectFilled(p0, p1, IM_COL32(40,40,48,255), cr);
            ImVec2 fillMax(p0.x + (p1.x - p0.x) * frac, p1.y);
            dl->AddRectFilled(p0, fillMax, colorU32(w.color), cr);
            if (w.showBorder) dl->AddRect(p0, p1, borderCol, cr, 0, bw);
            char buf[32]; snprintf(buf, sizeof(buf), "%d%%", (int)(frac * 100.0f + 0.5f));
            ImVec2 ts = textSize(buf);
            dl->AddText(NULL, fontPx, ImVec2(p0.x + ((p1.x - p0.x) - ts.x) * 0.5f,
                                             p0.y + ((p1.y - p0.y) - ts.y) * 0.5f),
                        textCol, buf);
            break;
        }
        case GuiWidgetType::Toggle:
        {
            float r = (p1.y - p0.y) * 0.5f;
            ImU32 track = w.checked ? colorU32(w.color) : IM_COL32(70,70,78,255);
            dl->AddRectFilled(p0, p1, track, r);
            float kr = ImMax(2.0f, r - 2.0f * scale);
            float kx = w.checked ? (p1.x - r) : (p0.x + r);
            dl->AddCircleFilled(ImVec2(kx, (p0.y + p1.y) * 0.5f), kr, IM_COL32(240,240,240,255));
            break;
        }
        case GuiWidgetType::Slider:
        {
            float frac = (w.maxValue > w.minValue)
                             ? (w.value - w.minValue) / (w.maxValue - w.minValue) : 0.0f;
            frac = ImClamp(frac, 0.0f, 1.0f);
            float cy = (p0.y + p1.y) * 0.5f;
            float th = ImMax(2.0f, 6.0f * scale);
            dl->AddRectFilled(ImVec2(p0.x, cy - th * 0.5f), ImVec2(p1.x, cy + th * 0.5f),
                              IM_COL32(45,45,52,255), th * 0.5f);
            float hx = p0.x + (p1.x - p0.x) * frac;
            dl->AddRectFilled(ImVec2(p0.x, cy - th * 0.5f), ImVec2(hx, cy + th * 0.5f),
                              colorU32(w.color), th * 0.5f);
            float kr = ImMax(4.0f, 8.0f * scale);
            dl->AddCircleFilled(ImVec2(hx, cy), kr, IM_COL32(235,235,240,255));
            break;
        }
        case GuiWidgetType::InputField:
        {
            drawSolid();
            drawTextAligned(w.text, textCol);
            break;
        }
    }

    // Children (clip inside scroll views so overflow stays contained).
    bool clip = (w.type == GuiWidgetType::ScrollView);
    if (clip) dl->PushClipRect(p0, p1, true);
    for (int cid : w.children)
    {
        if (const GuiWidget* child = layout.findWidget(cid))
        {
            ImVec2 childParentPos = refPos;
            if (w.type == GuiWidgetType::ScrollView)
            {
                // Apply the normalised scroll offset to the content origin so the
                // preview shows roughly how far the content has been scrolled.
                if (w.scrollDirection != GuiScrollDirection::Horizontal)
                    childParentPos.y -= w.scrollPosition * w.height * 0.5f;
                if (w.scrollDirection != GuiScrollDirection::Vertical)
                    childParentPos.x -= w.scrollPosition * w.width * 0.5f;
            }
            drawWidget(dl, *child, childParentPos, ImVec2(w.width, w.height));
        }
    }
    if (clip) dl->PopClipRect();

    // Selection highlight.
    if (w.id == selectedWidget)
    {
        dl->AddRect(ImVec2(p0.x - 2.0f, p0.y - 2.0f), ImVec2(p1.x + 2.0f, p1.y + 2.0f),
                    IM_COL32(255, 170, 40, 255), cr, 0, 2.0f);
    }
}

int PanelGui::hitTestWidgets(const GuiWidget& w, ImVec2 parentRefPos, ImVec2 parentSize,
                             ImVec2 mouse) const
{
    if (!w.visible) return -1;

    const ImVec2 refPos = widgetRefPos(w, parentRefPos, parentSize);
    const ImVec2 p0 = refToScreen(refPos);
    const ImVec2 p1 = refToScreen(ImVec2(refPos.x + w.width, refPos.y + w.height));

    // Children are drawn on top of their parent, so test them first (reverse order).
    for (auto it = w.children.rbegin(); it != w.children.rend(); ++it)
    {
        if (const GuiWidget* child = layout.findWidget(*it))
        {
            int hit = hitTestWidgets(*child, refPos, ImVec2(w.width, w.height), mouse);
            if (hit >= 0) return hit;
        }
    }

    if (mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y && mouse.y <= p1.y)
        return w.id;
    return -1;
}

void PanelGui::drawPreview()
{
    ImGui::BeginChild("##guipreview", ImVec2(0.0f, 0.0f), true);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f) avail.x = 1.0f;
    if (avail.y < 1.0f) avail.y = 1.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##guicanvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Backdrop behind the reference canvas.
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + avail.x, canvasPos.y + avail.y),
                      IM_COL32(26, 26, 30, 255));

    // Fit the reference resolution into the available area, then apply user zoom.
    const float pad = 36.0f;
    float fit = ImMin((avail.x - pad * 2.0f) / (float)layout.canvasWidth,
                      (avail.y - pad * 2.0f) / (float)layout.canvasHeight);
    if (fit <= 0.0f) fit = 0.0001f;
    previewScale = fit * canvasZoom;

    ImVec2 canvasSize(layout.canvasWidth * previewScale, layout.canvasHeight * previewScale);
    previewOrigin = ImVec2(canvasPos.x + (avail.x - canvasSize.x) * 0.5f + canvasOffset.x,
                           canvasPos.y + (avail.y - canvasSize.y) * 0.5f + canvasOffset.y);
    ImVec2 canvasEnd(previewOrigin.x + canvasSize.x, previewOrigin.y + canvasSize.y);

    // Canvas background + grid.
    dl->AddRectFilled(previewOrigin, canvasEnd, colorU32(layout.canvasColor));
    if (layout.showCanvasGrid)
    {
        ImU32 gcol = IM_COL32(255, 255, 255, 14);
        for (int i = 1; i < 12; ++i)
        {
            float x = previewOrigin.x + canvasSize.x * (i / 12.0f);
            float y = previewOrigin.y + canvasSize.y * (i / 12.0f);
            dl->AddLine(ImVec2(x, previewOrigin.y), ImVec2(x, canvasEnd.y), gcol);
            dl->AddLine(ImVec2(previewOrigin.x, y), ImVec2(canvasEnd.x, y), gcol);
        }
    }
    dl->AddRect(previewOrigin, canvasEnd, IM_COL32(95, 95, 105, 255), 0, 0, 2.0f);

    // Draw the widget tree.
    const ImVec2 rootParentPos(0.0f, 0.0f);
    const ImVec2 rootParentSize((float)layout.canvasWidth, (float)layout.canvasHeight);
    for (int rid : layout.rootIds())
        if (const GuiWidget* w = layout.findWidget(rid))
            drawWidget(dl, *w, rootParentPos, rootParentSize);

    // ---- Interaction -----------------------------------------------------
    bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImVec2 mdelta = ImGui::GetIO().MouseDelta;
    bool ctrl = ImGui::GetIO().KeyCtrl;

    // Pan with middle or right mouse button.
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
        canvasOffset.x += mdelta.x;
        canvasOffset.y += mdelta.y;
    }

    // Zoom with the wheel while hovering.
    if (hovered)
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            canvasZoom = ImClamp(canvasZoom * (1.0f + wheel * 0.12f), 0.1f, 8.0f);
    }

    // Pick the widget under the cursor.
    int hit = -1;
    if (hovered)
    {
        for (int rid : layout.rootIds())
        {
            if (const GuiWidget* w = layout.findWidget(rid))
            {
                hit = hitTestWidgets(*w, rootParentPos, rootParentSize, mouse);
                if (hit >= 0) break;
            }
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        selectedWidget = hit;
        isDraggingWidget = false;
        if (hit >= 0)
            if (const GuiWidget* hw = layout.findWidget(hit))
                isDraggingWidget = hw->interactable;
    }

    // Drag the selected widget to reposition it.
    if (isDraggingWidget && selectedWidget >= 0 &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (GuiWidget* w = layout.findWidget(selectedWidget))
        {
            float s = (previewScale > 0.0f) ? previewScale : 1.0f;
            w->x += mdelta.x / s;
            w->y += mdelta.y / s;
            layout.dirty = true;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        isDraggingWidget = false;

    // Ctrl+click on empty space deselects; double-click empty space re-fits.
    if (hovered && ctrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hit < 0)
        selectedWidget = -1;

    // Overlay hint (bottom-left corner of the preview).
    char info[128];
    snprintf(info, sizeof(info), "%dx%d  |  zoom %.0f%%", layout.canvasWidth,
             layout.canvasHeight, canvasZoom * fit * 100.0f);
    dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + avail.y - 20.0f),
                IM_COL32(150, 150, 160, 200), info);

    ImGui::EndChild();
}

// ===========================================================================
// Hierarchy panel
// ===========================================================================

static bool subtreeMatches(const GuiLayout& lay, const GuiWidget& w, const std::string& q)
{
    if (containsCI(w.name, q)) return true;
    for (int c : w.children)
        if (const GuiWidget* cw = lay.findWidget(c))
            if (subtreeMatches(lay, *cw, q)) return true;
    return false;
}

void PanelGui::drawHierarchyNode(int id, int depth)
{
    GuiWidget* w = layout.findWidget(id);
    if (!w) return;

    const std::string q = searchBuf;
    if (!q.empty() && !subtreeMatches(layout, *w, q))
        return;

    ImGui::PushID(id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (w->children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selectedWidget == id)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = w->name;
    if (!w->visible) label += "  (hidden)";

    if (!w->visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    if (!w->visible) ImGui::PopStyleColor();

    bool clicked = ImGui::IsItemClicked();
    bool toggled = ImGui::IsItemToggledOpen();

    // Context menu attached to the row.
    if (ImGui::BeginPopupContextItem("##nodectx"))
    {
        if (ImGui::MenuItem("Rename"))
        {
            renameTarget = id;
            copyToBuf(renameBuf, sizeof(renameBuf), w->name);
            openRenamePopup = true;
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            GuiWidget copy = *w;
            copy.id = layout.newId();
            copy.children.clear();
            copy.name = uniqueName(w->name);
            if (copy.parentId >= 0)
                if (GuiWidget* parent = layout.findWidget(copy.parentId))
                    parent->children.push_back(copy.id);
            layout.widgets.push_back(copy);
            selectedWidget = copy.id;
            layout.dirty = true;
        }
        if (ImGui::MenuItem("Delete"))
        {
            int removing = id;
            if (selectedWidget == removing) selectedWidget = -1;
            ImGui::EndPopup();
            layout.removeWidget(removing);
            layout.dirty = true;
            ImGui::PopID();
            return;
        }
        if (guiWidgetTypeIsContainer(w->type) && ImGui::BeginMenu("Add Child"))
        {
            for (GuiWidgetType t : guiWidgetTypes())
                if (ImGui::MenuItem(guiWidgetTypeName(t)))
                {
                    int newId = layout.addWidget(t, id);
                    if (GuiWidget* nw = layout.findWidget(newId))
                        nw->name = uniqueName(guiWidgetTypeName(t));
                    selectedWidget = newId;
                    layout.dirty = true;
                }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (clicked && !toggled)
        selectedWidget = id;

    // Trailing delete button, right-aligned on the same row.
    {
        const float bh = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bh);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        if (ImGui::Button("x", ImVec2(bh, bh)))
        {
            int removing = id;
            if (selectedWidget == removing) selectedWidget = -1;
            ImGui::PopStyleVar(2);
            ImGui::PopID();
            layout.removeWidget(removing);
            layout.dirty = true;
            return;
        }
        ImGui::PopStyleVar(2);
    }

    if (open && !w->children.empty())
    {
        // Copy the child list: recursion may add/remove widgets.
        std::vector<int> kids = w->children;
        for (int c : kids)
            drawHierarchyNode(c, depth + 1);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void PanelGui::drawAddWidgetMenu()
{
    if (!ImGui::BeginPopup("##addwidgetpopup"))
        return;

    ImGui::TextDisabled("Add Widget");
    ImGui::Separator();
    for (GuiWidgetType t : guiWidgetTypes())
    {
        ImGui::PushID((int)t);
        if (ImGui::MenuItem(guiWidgetTypeName(t)))
            addWidget(t);
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void PanelGui::drawTexturePickerPopup(GuiWidget* w)
{
    if (!w || !ImGui::BeginPopup("##guitexturepick"))
        return;

    ImGui::TextDisabled("GUI Images");

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##texsearch", "Search...", textureSearchBuf, sizeof(textureSearchBuf));
    ImGui::Separator();

    if (!manager->projectOpened)
    {
        ImGui::TextDisabled("No project open.");
        ImGui::EndPopup();
        return;
    }

    // Collect every project image whose import settings use the "GUI" image
    // type, optionally filtered by the search box, then sort by name so the
    // list order stays stable between frames.
    struct Entry { kString uuid; std::string name; };
    std::vector<Entry> entries;
    for (const auto& kv : manager->fileMap)
    {
        if (kv.second.type != "image")
            continue;
        if (!isGuiImageAsset(fs::path(manager->projectPath.c_str()), kv.first))
            continue;

        std::string name = fs::path(kv.second.path.c_str()).stem().string();
        if (!containsCI(name, textureSearchBuf))
            continue;
        entries.push_back({ kv.first, name });
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    ImGui::BeginChild("##texlist", ImVec2(320.0f, 320.0f), true);
    if (entries.empty())
    {
        ImGui::TextDisabled("No GUI images found");
        ImGui::TextWrapped("Set an image's Import Settings > Image Type to \"GUI\" to list it here.");
    }
    else
    {
        for (const auto& e : entries)
        {
            const bool selected = (w->textureUuid == e.uuid);
            ImGui::PushID(e.uuid.c_str());

            // Thumbnail (when the project has one) followed by the asset name.
            const uint32_t thumb = resolveThumbnail(e.uuid);
            if (thumb != 0)
            {
                ImGui::Image((ImTextureRef)(intptr_t)thumb, ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
            }

            if (ImGui::Selectable(e.name.c_str(), selected, 0, ImVec2(-FLT_MIN, 26.0f)))
            {
                w->textureUuid = e.uuid;
                layout.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Clear", ImVec2(150.0f, 0.0f)))
    {
        w->textureUuid.clear();
        layout.dirty = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(150.0f, 0.0f)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

GuiTextureInfo PanelGui::resolveWidgetTexture(const std::string& assetUuid)
{
    GuiTextureInfo info;
    if (assetUuid.empty() || !manager)
        return info;

    auto it = guiTextureCache.find(assetUuid);
    if (it != guiTextureCache.end())
        return GuiTextureInfo{ it->second.glId, it->second.size };

    GuiTexEntry entry;
    if (kTexture2D* tex = manager->getProjectTexture(assetUuid, "uiImage"))
    {
        if (tex->getTextureID() != 0)
        {
            entry.glId = tex->getTextureID();
            entry.size = ImVec2((float)tex->getWidth(), (float)tex->getHeight());
        }
    }

    // Only cache successes: a texture imported later in the session should still
    // show up without restarting the editor.
    if (entry.glId != 0)
        guiTextureCache[assetUuid] = entry;

    return GuiTextureInfo{ entry.glId, entry.size };
}

uint32_t PanelGui::resolveThumbnail(const std::string& assetUuid)
{
    if (assetUuid.empty())
        return 0;

    auto it = guiThumbnailCache.find(assetUuid);
    if (it != guiThumbnailCache.end())
        return it->second;

    uint32_t glId = 0;
    fs::path thumbPath = fs::path(manager->projectPath.c_str()) /
                         "Library" / "Thumbnails" / (assetUuid + ".png");
    if (fs::exists(thumbPath))
    {
        if (kAssetManager* am = manager->getAssetManager())
        {
            kTexture2D* tex = am->loadTexture2D(thumbPath.string(),
                                                assetUuid + "_ui_thumb",
                                                kTextureFormat::TEX_FORMAT_RGBA, false);
            if (tex && tex->getTextureID() != 0)
                glId = tex->getTextureID();
        }
    }

    // Only cache hits — thumbnails may be generated later by the project panel.
    if (glId != 0)
        guiThumbnailCache[assetUuid] = glId;
    return glId;
}

std::string PanelGui::uniqueName(const std::string& base) const
{
    auto taken = [&](const std::string& n) -> bool
    {
        for (const auto& w : layout.widgets)
            if (w.name == n) return true;
        return false;
    };

    if (!taken(base)) return base;

    // Strip a trailing " N" so duplicates become "Name 1", "Name 2", ...
    std::string stem = base;
    size_t sp = stem.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < stem.size() &&
        stem.find_first_not_of("0123456789", sp + 1) == std::string::npos)
        stem = stem.substr(0, sp);

    int counter = 1;
    std::string candidate;
    do { candidate = stem + " " + std::to_string(counter++); } while (taken(candidate));
    return candidate;
}

int PanelGui::addWidget(GuiWidgetType type)
{
    // Nested when a container is selected, otherwise added at the root.
    int parentId = -1;
    if (GuiWidget* sel = layout.findWidget(selectedWidget))
        if (guiWidgetTypeIsContainer(sel->type))
            parentId = selectedWidget;

    int id = layout.addWidget(type, parentId);
    if (GuiWidget* w = layout.findWidget(id))
        w->name = uniqueName(guiWidgetTypeName(type));

    selectedWidget = id;
    layout.dirty = true;
    return id;
}

void PanelGui::drawHierarchyPanel()
{
    ImGui::BeginChild("##guihierarchy", ImVec2(hierarchyPanelWidth, 0.0f), true);

    // "Add Widget" dropdown (replaces the Animator's "Add Variable" button).
    if (ImGui::Button("Add Widget", ImVec2(-1.0f, 0.0f)))
        ImGui::OpenPopup("##addwidgetpopup");
    drawAddWidgetMenu();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##guisearch", "Search...", searchBuf, sizeof(searchBuf));

    ImGui::Separator();

    if (layout.widgets.empty())
    {
        const char* msg = "No widget defined";
        float tw = ImGui::CalcTextSize(msg).x;
        float aw = ImGui::GetContentRegionAvail().x;
        if (aw > tw) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (aw - tw) * 0.5f);
        ImGui::TextDisabled("%s", msg);
    }
    else
    {
        std::vector<int> roots = layout.rootIds();
        for (int rid : roots)
            drawHierarchyNode(rid, 0);
    }

    ImGui::EndChild();
}

void PanelGui::drawHierarchySplitter()
{
    const float splitterWidth = 6.0f;
    const float minWidth      = 180.0f;
    const float maxWidth      = 560.0f;

    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y < 1.0f) avail.y = 1.0f;
    ImGui::InvisibleButton("##guihsplit", ImVec2(splitterWidth, avail.y));

    if (ImGui::IsItemActive())
        hierarchyPanelWidth = ImClamp(hierarchyPanelWidth + ImGui::GetIO().MouseDelta.x,
                                      minWidth, maxWidth);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      rect = ImGui::GetItemRectMin();
    ImVec2      mx   = ImGui::GetItemRectMax();
    ImU32       col  = ImGui::IsItemHovered() || ImGui::IsItemActive()
                           ? IM_COL32(120, 160, 220, 255)
                           : IM_COL32(70, 70, 70, 255);
    dl->AddRectFilled(ImVec2(rect.x + 2.0f, rect.y), ImVec2(mx.x - 2.0f, mx.y), col);

    ImGui::PopStyleVar();
    ImGui::SameLine();
}

// ===========================================================================
// Toolbar
// ===========================================================================

void PanelGui::drawToolbar()
{
    bool hasProject = manager->projectOpened;

    if (ImGui::Button("New"))
        newLayout();
    ImGui::SameLine();
    if (ImGui::Button("Open") && hasProject)
    {
        SDL_DialogFileFilter filters[] = {
            { "UI files", "ui"  },
            { "All files", "*"  }
        };
        fs::path dir = fs::path(manager->projectPath.c_str()) / "Assets" / "UI";
        fs::create_directories(dir);
        SDL_ShowOpenFileDialog(
            [](void* userdata, const char* const* filelist, int)
            {
                if (!filelist || !*filelist) return;
                static_cast<PanelGui*>(userdata)->openFile(filelist[0]);
            },
            this,
            manager->getWindow()->getSdlWindow(),
            filters,
            SDL_arraysize(filters),
            dir.string().c_str(),
            false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && hasProject)
        saveLayout();
    ImGui::SameLine();
    if (ImGui::Button("Save As") && hasProject)
        saveLayoutAs();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("Fit View"))
    {
        canvasZoom = 1.0f;
        canvasOffset = ImVec2(0.0f, 0.0f);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragInt("##canvasw", &layout.canvasWidth, 1.0f, 64, 8192, "W %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragInt("##canvash", &layout.canvasHeight, 1.0f, 64, 8192, "H %d");

    ImGui::SameLine();
    if (layout.dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "*");
    else
        ImGui::TextDisabled("saved");
}

void PanelGui::drawRenamePopup()
{
    if (openRenamePopup)
    {
        ImGui::OpenPopup("Rename Widget");
        openRenamePopup = false;
    }

    if (ImGui::BeginPopupModal("Rename Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("New name:");
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("##renamebuf", renameBuf, sizeof(renameBuf));

        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            if (GuiWidget* w = layout.findWidget(renameTarget))
            {
                if (renameBuf[0] != '\0')
                {
                    w->name = renameBuf;
                    layout.dirty = true;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ===========================================================================
// Inspector (drawn inside the Inspector panel)
// ===========================================================================

void PanelGui::drawSelectedInspector()
{
    GuiWidget* w = layout.findWidget(selectedWidget);

    if (!w)
    {
        // No widget selected — expose the canvas/layout properties instead.
        ImGui::SeparatorText("Ingame UI");
        if (ImGui::Button("Add Widget...", ImVec2(-1.0f, 0.0f)))
            addWidget(GuiWidgetType::Panel);

        ImGui::SeparatorText("Canvas");
        if (ImGui::DragInt("Width", &layout.canvasWidth, 1.0f, 64, 8192))
            layout.dirty = true;
        if (ImGui::DragInt("Height", &layout.canvasHeight, 1.0f, 64, 8192))
            layout.dirty = true;
        if (ImGui::ColorEdit4("Background", layout.canvasColor))
            layout.dirty = true;
        if (ImGui::Checkbox("Show Grid", &layout.showCanvasGrid))
            layout.dirty = true;

        ImGui::SeparatorText("Layout");
        char nameBuf[128];
        copyToBuf(nameBuf, sizeof(nameBuf), layout.name);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            layout.name = nameBuf;
            layout.dirty = true;
        }
        ImGui::LabelText("File", "%s", filePath.empty() ? "(unsaved)" : filePath.c_str());
        ImGui::TextDisabled("Select a widget in the hierarchy to edit its properties.");
        return;
    }

    ImGui::SeparatorText(guiWidgetTypeName(w->type));

    // ---- Identity --------------------------------------------------------
    char nameBuf[128];
    copyToBuf(nameBuf, sizeof(nameBuf), w->name);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
    {
        w->name = nameBuf;
        layout.dirty = true;
    }
    ImGui::LabelText("Type", "%s", guiWidgetTypeName(w->type));

    // ---- Transform -------------------------------------------------------
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int anchor = (int)w->anchor;
        if (ImGui::Combo("Anchor", &anchor, kAnchorNames, IM_ARRAYSIZE(kAnchorNames)))
        {
            w->anchor = (GuiAnchor)anchor;
            layout.dirty = true;
        }

        float xy[2] = { w->x, w->y };
        if (ImGui::DragFloat2("Position", xy, 1.0f))
        {
            w->x = xy[0]; w->y = xy[1];
            layout.dirty = true;
        }
        float wh[2] = { w->width, w->height };
        if (ImGui::DragFloat2("Size", wh, 1.0f, 0.0f, 20000.0f))
        {
            w->width = wh[0]; w->height = wh[1];
            layout.dirty = true;
        }
        if (ImGui::DragFloat("Rotation", &w->rotation, 0.5f, -360.0f, 360.0f, "%.1f deg"))
            layout.dirty = true;
        if (ImGui::DragFloat("Scale", &w->scale, 0.01f, 0.01f, 100.0f))
            layout.dirty = true;

        if (ImGui::Checkbox("Visible", &w->visible))      layout.dirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Interactable", &w->interactable)) layout.dirty = true;
    }

    // ---- Appearance ------------------------------------------------------
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Fill Background", &w->fillBackground)) layout.dirty = true;
        if (ImGui::ColorEdit4("Color", w->color)) layout.dirty = true;

        if (ImGui::Checkbox("Border", &w->showBorder)) layout.dirty = true;
        if (w->showBorder || w->borderWidth > 0.0f)
        {
            if (ImGui::ColorEdit4("Border Color", w->borderColor)) layout.dirty = true;
            if (ImGui::DragFloat("Border Width", &w->borderWidth, 0.1f, 0.0f, 64.0f))
                layout.dirty = true;
        }
        if (ImGui::DragFloat("Corner Radius", &w->cornerRadius, 0.1f, 0.0f, 512.0f))
            layout.dirty = true;
    }

    // ---- Nine-slice (Panel or Sliced image) ------------------------------
    bool nineSlice = (w->type == GuiWidgetType::Panel ||
                      (w->type == GuiWidgetType::Image && w->imageMode == GuiImageMode::Sliced));
    if (nineSlice && ImGui::CollapsingHeader("Nine-Slice / Corners", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::DragFloat("Left",   &w->sliceLeft,   0.5f, 0.0f, 1024.0f)) layout.dirty = true;
        if (ImGui::DragFloat("Right",  &w->sliceRight,  0.5f, 0.0f, 1024.0f)) layout.dirty = true;
        if (ImGui::DragFloat("Top",    &w->sliceTop,    0.5f, 0.0f, 1024.0f)) layout.dirty = true;
        if (ImGui::DragFloat("Bottom", &w->sliceBottom, 0.5f, 0.0f, 1024.0f)) layout.dirty = true;
        if (w->type == GuiWidgetType::Panel)
            if (ImGui::Checkbox("Show Corner Guides", &w->showCorners)) layout.dirty = true;
    }

    // ---- Text ------------------------------------------------------------
    bool hasText = (w->type == GuiWidgetType::Text ||
                    w->type == GuiWidgetType::Button ||
                    w->type == GuiWidgetType::InputField);
    if (hasText && ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        char textBuf[512];
        copyToBuf(textBuf, sizeof(textBuf), w->text);
        if (ImGui::InputTextMultiline("Content", textBuf, sizeof(textBuf), ImVec2(-FLT_MIN, 60.0f)))
        {
            w->text = textBuf;
            layout.dirty = true;
        }
        if (ImGui::DragFloat("Font Size", &w->fontSize, 0.5f, 1.0f, 512.0f)) layout.dirty = true;
        int align = (int)w->textAlign;
        if (ImGui::Combo("Alignment", &align, kTextAlignNames, IM_ARRAYSIZE(kTextAlignNames)))
        {
            w->textAlign = (GuiTextAlign)align;
            layout.dirty = true;
        }
        if (ImGui::ColorEdit4("Text Color", w->textColor)) layout.dirty = true;
        if (ImGui::Checkbox("Word Wrap", &w->wordWrap)) layout.dirty = true;
    }

    // ---- Image -----------------------------------------------------------
    bool hasImage = (w->type == GuiWidgetType::Image || w->type == GuiWidgetType::Button);
    if (hasImage && ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Texture selection is a button that opens a picker restricted to
        // project images imported with the "GUI" image type.
        std::string texLabel = "Select Texture...";
        if (!w->textureUuid.empty())
        {
            auto itTex = manager->fileMap.find(w->textureUuid);
            if (itTex != manager->fileMap.end())
                texLabel = "Texture: " + fs::path(itTex->second.path.c_str()).stem().string();
            else
                texLabel = "Texture: " + w->textureUuid.substr(0, 8) + "...";
        }
        if (ImGui::Button(texLabel.c_str(), ImVec2(-FLT_MIN, 0.0f)))
            ImGui::OpenPopup("##guitexturepick");
        drawTexturePickerPopup(w);
        int mode = (int)w->imageMode;
        if (ImGui::Combo("Image Mode", &mode, kImageModeNames, IM_ARRAYSIZE(kImageModeNames)))
        {
            w->imageMode = (GuiImageMode)mode;
            layout.dirty = true;
        }

        // How the texture is fitted inside the widget rect.
        int fit = (int)w->imageFit;
        if (ImGui::Combo("Image Fit", &fit, kImageFitNames, IM_ARRAYSIZE(kImageFitNames)))
        {
            w->imageFit = (GuiImageFit)fit;
            layout.dirty = true;
        }
        if (w->imageMode == GuiImageMode::Filled)
            if (ImGui::SliderFloat("Fill Amount", &w->fillAmount, 0.0f, 1.0f))
                layout.dirty = true;
    }

    // ---- Scroll View -----------------------------------------------------
    if (w->type == GuiWidgetType::ScrollView &&
        ImGui::CollapsingHeader("Scroll View", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int dir = (int)w->scrollDirection;
        if (ImGui::Combo("Direction", &dir, kScrollDirNames, IM_ARRAYSIZE(kScrollDirNames)))
        {
            w->scrollDirection = (GuiScrollDirection)dir;
            layout.dirty = true;
        }
        if (ImGui::Checkbox("Show Scrollbar", &w->showScrollbar)) layout.dirty = true;
        if (ImGui::SliderFloat("Scroll Position", &w->scrollPosition, 0.0f, 1.0f))
            layout.dirty = true;
    }

    // ---- Value (ProgressBar / Slider) ------------------------------------
    if ((w->type == GuiWidgetType::ProgressBar || w->type == GuiWidgetType::Slider) &&
        ImGui::CollapsingHeader("Value", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::DragFloat("Min", &w->minValue, 0.1f)) layout.dirty = true;
        if (ImGui::DragFloat("Max", &w->maxValue, 0.1f)) layout.dirty = true;
        float step = 0.01f * ImMax(1.0f, w->maxValue - w->minValue);
        if (ImGui::SliderFloat("Value", &w->value, w->minValue, w->maxValue, "%.3f"))
            layout.dirty = true;
        (void)step;
    }

    // ---- Toggle ----------------------------------------------------------
    if (w->type == GuiWidgetType::Toggle &&
        ImGui::CollapsingHeader("Toggle", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Checked", &w->checked)) layout.dirty = true;
    }

    // ---- Line ------------------------------------------------------------
    if (w->type == GuiWidgetType::Line &&
        ImGui::CollapsingHeader("Line", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Vertical", &w->vertical)) layout.dirty = true;
        if (ImGui::ColorEdit4("Line Color", w->color)) layout.dirty = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Delete Widget", ImVec2(-1.0f, 0.0f)))
    {
        layout.removeWidget(selectedWidget);
        selectedWidget = -1;
        layout.dirty = true;
    }
}

// ===========================================================================
// Main draw
// ===========================================================================

void PanelGui::draw(bool& isOpened)
{
    visible = isOpened;
    if (!isOpened) return;

    ImGui::SetNextWindowSize({ 1100, 720 }, ImGuiCond_FirstUseEver);

    kString title = layout.dirty ? "Ingame UI *" : "Ingame UI";
    title += "###IngameUI";

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(title.c_str(), &isOpened, wflags))
    {
        focused = false;
        ImGui::End();
        return;
    }

    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Drop cached GL handles when a different project is opened — those
    // textures belonged to the previous project and are no longer valid.
    if (manager->projectOpened)
    {
        const std::string proj = manager->projectPath.string();
        if (proj != lastProjectPath)
        {
            guiTextureCache.clear();
            guiThumbnailCache.clear();
            lastProjectPath = proj;
        }
    }

    drawToolbar();
    drawHierarchyPanel();
    drawHierarchySplitter();
    drawPreview();
    drawRenamePopup();

    ImGui::End();
}

// ===========================================================================
// Runtime rendering (used by the Game panel to show .ui layouts in the scene)
// ===========================================================================

bool loadGuiLayoutFromFile(const std::string& path, GuiLayout& outLayout)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    try
    {
        json j;
        f >> j;
        outLayout.fromJson(j);
        if (outLayout.name.empty())
            outLayout.name = fs::path(path).stem().string();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Anchor fractions of a widget: 0 = left/top, 0.5 = centre, 1 = right/bottom.
static ImVec2 anchorFractions(GuiAnchor anchor)
{
    switch (anchor)
    {
        case GuiAnchor::TopLeft:      return ImVec2(0.0f, 0.0f);
        case GuiAnchor::Top:          return ImVec2(0.5f, 0.0f);
        case GuiAnchor::TopRight:     return ImVec2(1.0f, 0.0f);
        case GuiAnchor::MiddleLeft:   return ImVec2(0.0f, 0.5f);
        case GuiAnchor::MiddleCenter: return ImVec2(0.5f, 0.5f);
        case GuiAnchor::MiddleRight:  return ImVec2(1.0f, 0.5f);
        case GuiAnchor::BottomLeft:   return ImVec2(0.0f, 1.0f);
        case GuiAnchor::Bottom:       return ImVec2(0.5f, 1.0f);
        case GuiAnchor::BottomRight:  return ImVec2(1.0f, 1.0f);
    }
    return ImVec2(0.0f, 0.0f);
}

// Recursive runtime widget painter (no interaction, no selection UI).
//
// Layout rule — stretch-to-fill positions with undistorted widgets:
//   * POSITIONS follow the game view's aspect ratio. Anchor fractions resolve
//     against the parent's on-screen rect, and free offsets scale with the
//     matching axis (scaleX for x, scaleY for y), so a widget anchored to the
//     bottom-right really lands in the bottom-right corner of the view.
//   * SIZES use a single uniform factor (the average of both axes) so every
//     widget keeps its authored aspect ratio — buttons, labels and images are
//     never squashed or stretched.
static void renderGuiWidgetRuntime(const GuiLayout& layout, ImDrawList* dl, const GuiWidget& w,
                                   ImVec2 parentPxMin, ImVec2 parentPxSize,
                                   float scaleX, float scaleY,
                                   const GuiTextureResolver& resolveTexture)
{
    if (!w.visible) return;

    // Uniform size factor: widgets keep their shape at every aspect ratio.
    const float scale = (scaleX + scaleY) * 0.5f;

    const ImVec2 af = anchorFractions(w.anchor);
    const ImVec2 p0(parentPxMin.x + af.x * parentPxSize.x + w.x * scaleX - af.x * w.width * scale,
                    parentPxMin.y + af.y * parentPxSize.y + w.y * scaleY - af.y * w.height * scale);
    const ImVec2 size(w.width * scale, w.height * scale);
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);

    // Radii/borders/fonts follow the same uniform factor as the sizes.
    const float cr        = w.cornerRadius * scale;
    const float bw        = ImMax(1.0f, w.borderWidth * scale);
    const ImU32 bgCol     = colorU32(w.color);
    const ImU32 borderCol = colorU32(w.borderColor);
    const ImU32 textCol   = colorU32(w.textColor);
    const float fontPx    = ImMax(1.0f, w.fontSize * scale);
    const float fscale    = fontPx / ImGui::GetFontSize();
    const float pad       = ImMax(2.0f, 6.0f * scale);

    auto textSize = [&](const std::string& s) -> ImVec2
    {
        ImVec2 ts = ImGui::CalcTextSize(s.c_str());
        return ImVec2(ts.x * fscale, ts.y * fscale);
    };
    auto drawTextAligned = [&](const std::string& s, ImU32 col)
    {
        if (s.empty()) return;
        ImVec2 ts = textSize(s);
        float tx = p0.x + pad;
        if (w.textAlign == GuiTextAlign::Center) tx = p0.x + ((p1.x - p0.x) - ts.x) * 0.5f;
        else if (w.textAlign == GuiTextAlign::Right) tx = p1.x - pad - ts.x;
        float ty = p0.y + ((p1.y - p0.y) - ts.y) * 0.5f;
        dl->AddText(NULL, fontPx, ImVec2(tx, ty), col, s.c_str());
    };
    auto drawSolid = [&]()
    {
        if (w.fillBackground) dl->AddRectFilled(p0, p1, bgCol, cr);
        if (w.showBorder)     dl->AddRect(p0, p1, borderCol, cr, 0, bw);
    };

    switch (w.type)
    {
        case GuiWidgetType::Text:
            if (w.fillBackground) dl->AddRectFilled(p0, p1, bgCol, cr);
            if (w.showBorder)     dl->AddRect(p0, p1, borderCol, cr, 0, bw);
            drawTextAligned(w.text, textCol);
            break;

        case GuiWidgetType::Button:
        {
            drawSolid();
            // Background texture (when assigned), drawn under the label.
            if (resolveTexture && !w.textureUuid.empty())
            {
                GuiTextureInfo tex = resolveTexture(w.textureUuid);
                if (tex.glId != 0)
                {
                    ImVec2 dmin, dmax;
                    computeGuiImageRect(w.imageFit, p0, p1, tex.size, dmin, dmax);
                    const bool clip = (w.imageFit == GuiImageFit::Cover || w.imageFit == GuiImageFit::None);
                    if (clip) dl->PushClipRect(p0, p1, true);
                    dl->AddImage((ImTextureRef)(intptr_t)tex.glId, dmin, dmax);
                    if (clip) dl->PopClipRect();
                }
            }
            drawTextAligned(w.text, textCol);
            break;
        }

        case GuiWidgetType::Panel:
            drawSolid();
            if (w.showCorners)
            {
                float sl = w.sliceLeft * scale, sr = w.sliceRight * scale;
                float st = w.sliceTop * scale,  sb = w.sliceBottom * scale;
                ImU32 cc = IM_COL32(255, 200, 80, 150);
                float t = ImMax(1.0f, 1.5f * scale);
                dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x + sl, p0.y), cc, t);
                dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p0.y + st), cc, t);
                dl->AddLine(ImVec2(p1.x - sr, p0.y), ImVec2(p1.x, p0.y), cc, t);
                dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p0.y + st), cc, t);
                dl->AddLine(ImVec2(p0.x, p1.y - sb), ImVec2(p0.x, p1.y), cc, t);
                dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p0.x + sl, p1.y), cc, t);
                dl->AddLine(ImVec2(p1.x - sr, p1.y), ImVec2(p1.x, p1.y), cc, t);
                dl->AddLine(ImVec2(p1.x, p1.y - sb), ImVec2(p1.x, p1.y), cc, t);
            }
            break;

        case GuiWidgetType::Image:
        {
            bool drawn = false;
            if (resolveTexture && !w.textureUuid.empty())
            {
                GuiTextureInfo tex = resolveTexture(w.textureUuid);
                if (tex.glId != 0)
                {
                    ImVec2 dmin, dmax;
                    computeGuiImageRect(w.imageFit, p0, p1, tex.size, dmin, dmax);
                    const bool clip = (w.imageFit == GuiImageFit::Cover || w.imageFit == GuiImageFit::None);
                    if (clip) dl->PushClipRect(p0, p1, true);
                    dl->AddImage((ImTextureRef)(intptr_t)tex.glId, dmin, dmax);
                    if (clip) dl->PopClipRect();
                    drawn = true;
                }
            }
            if (!drawn)
                drawSolid();
            break;
        }

        case GuiWidgetType::Spacer:
            // Invisible layout spacer — occupies space, draws nothing.
            break;

        case GuiWidgetType::Line:
        {
            ImU32 c = colorU32(w.color);
            if (w.vertical)
            {
                float cx = (p0.x + p1.x) * 0.5f;
                float t = ImMax(1.0f, w.width * scale);
                dl->AddRectFilled(ImVec2(cx - t * 0.5f, p0.y), ImVec2(cx + t * 0.5f, p1.y), c);
            }
            else
            {
                float cy = (p0.y + p1.y) * 0.5f;
                float t = ImMax(1.0f, w.height * scale);
                dl->AddRectFilled(ImVec2(p0.x, cy - t * 0.5f), ImVec2(p1.x, cy + t * 0.5f), c);
            }
            break;
        }

        case GuiWidgetType::ScrollView:
        {
            drawSolid();
            bool vert = (w.scrollDirection == GuiScrollDirection::Vertical ||
                         w.scrollDirection == GuiScrollDirection::Both);
            bool horz = (w.scrollDirection == GuiScrollDirection::Horizontal ||
                         w.scrollDirection == GuiScrollDirection::Both);
            if (w.showScrollbar && vert)
            {
                float gw = ImMax(4.0f, 7.0f * scale);
                dl->AddRectFilled(ImVec2(p1.x - gw, p0.y), ImVec2(p1.x, p1.y), IM_COL32(0, 0, 0, 90));
                float hs = (p1.y - p0.y) * 0.35f;
                float hy = p0.y + ((p1.y - p0.y) - hs) * ImClamp(w.scrollPosition, 0.0f, 1.0f);
                dl->AddRectFilled(ImVec2(p1.x - gw, hy), ImVec2(p1.x, hy + hs),
                                  IM_COL32(160, 160, 170, 200), 3.0f);
            }
            if (w.showScrollbar && horz)
            {
                float gh = ImMax(4.0f, 7.0f * scale);
                dl->AddRectFilled(ImVec2(p0.x, p1.y - gh), ImVec2(p1.x, p1.y), IM_COL32(0, 0, 0, 90));
                float ws = (p1.x - p0.x) * 0.35f;
                float wx = p0.x + ((p1.x - p0.x) - ws) * ImClamp(w.scrollPosition, 0.0f, 1.0f);
                dl->AddRectFilled(ImVec2(wx, p1.y - gh), ImVec2(wx + ws, p1.y),
                                  IM_COL32(160, 160, 170, 200), 3.0f);
            }
            break;
        }

        case GuiWidgetType::ProgressBar:
        {
            float frac = (w.maxValue > w.minValue)
                             ? (w.value - w.minValue) / (w.maxValue - w.minValue) : 0.0f;
            frac = ImClamp(frac, 0.0f, 1.0f);
            dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 48, 255), cr);
            dl->AddRectFilled(p0, ImVec2(p0.x + (p1.x - p0.x) * frac, p1.y),
                              colorU32(w.color), cr);
            if (w.showBorder) dl->AddRect(p0, p1, borderCol, cr, 0, bw);
            break;
        }

        case GuiWidgetType::Toggle:
        {
            float r = (p1.y - p0.y) * 0.5f;
            ImU32 track = w.checked ? colorU32(w.color) : IM_COL32(70, 70, 78, 255);
            dl->AddRectFilled(p0, p1, track, r);
            float kr = ImMax(2.0f, r - 2.0f * scale);
            float kx = w.checked ? (p1.x - r) : (p0.x + r);
            dl->AddCircleFilled(ImVec2(kx, (p0.y + p1.y) * 0.5f), kr, IM_COL32(240, 240, 240, 255));
            break;
        }

        case GuiWidgetType::Slider:
        {
            float frac = (w.maxValue > w.minValue)
                             ? (w.value - w.minValue) / (w.maxValue - w.minValue) : 0.0f;
            frac = ImClamp(frac, 0.0f, 1.0f);
            float cy = (p0.y + p1.y) * 0.5f;
            float th = ImMax(2.0f, 6.0f * scale);
            dl->AddRectFilled(ImVec2(p0.x, cy - th * 0.5f), ImVec2(p1.x, cy + th * 0.5f),
                              IM_COL32(45, 45, 52, 255), th * 0.5f);
            float hx = p0.x + (p1.x - p0.x) * frac;
            dl->AddRectFilled(ImVec2(p0.x, cy - th * 0.5f), ImVec2(hx, cy + th * 0.5f),
                              colorU32(w.color), th * 0.5f);
            dl->AddCircleFilled(ImVec2(hx, cy), ImMax(4.0f, 8.0f * scale),
                                IM_COL32(235, 235, 240, 255));
            break;
        }

        case GuiWidgetType::InputField:
            drawSolid();
            drawTextAligned(w.text, textCol);
            break;
    }

    // Children (clipped inside scroll views).
    bool clip = (w.type == GuiWidgetType::ScrollView);
    if (clip) dl->PushClipRect(p0, p1, true);
    for (int cid : w.children)
    {
        if (const GuiWidget* child = layout.findWidget(cid))
        {
            ImVec2 childParentPos = p0;
            if (w.type == GuiWidgetType::ScrollView)
            {
                if (w.scrollDirection != GuiScrollDirection::Horizontal)
                    childParentPos.y -= w.scrollPosition * size.y * 0.5f;
                if (w.scrollDirection != GuiScrollDirection::Vertical)
                    childParentPos.x -= w.scrollPosition * size.x * 0.5f;
            }
            renderGuiWidgetRuntime(layout, dl, *child, childParentPos, size, scaleX, scaleY,
                                   resolveTexture);
        }
    }
    if (clip) dl->PopClipRect();
}

void renderGuiLayout(ImDrawList* dl, const GuiLayout& layout, ImVec2 rectMin, ImVec2 rectMax,
                     const GuiTextureResolver& resolveTexture)
{
    float rectW = rectMax.x - rectMin.x;
    float rectH = rectMax.y - rectMin.y;
    if (!dl || rectW <= 0.0f || rectH <= 0.0f) return;
    if (layout.canvasWidth <= 0 || layout.canvasHeight <= 0) return;

    // Stretch-to-fill: the reference canvas is mapped onto the whole game view,
    // so layout follows the view's aspect ratio. Positions use the per-axis
    // scales (a widget at 50% width is always at 50% of the view and anchors
    // land on the real screen edges), while widget *sizes* use a single uniform
    // factor so nothing is distorted — see renderGuiWidgetRuntime.
    const float scaleX = rectW / (float)layout.canvasWidth;
    const float scaleY = rectH / (float)layout.canvasHeight;
    if (scaleX <= 0.0f || scaleY <= 0.0f) return;

    for (int rid : layout.rootIds())
        if (const GuiWidget* w = layout.findWidget(rid))
            renderGuiWidgetRuntime(layout, dl, *w, rectMin, ImVec2(rectW, rectH), scaleX, scaleY,
                                   resolveTexture);
}
