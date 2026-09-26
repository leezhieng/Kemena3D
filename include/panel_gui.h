#pragma once
#include "kemena/kemena.h"
#include "manager.h"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <unordered_map>

// Forward-declared so this header can be included from manager.h (which includes
// it before `class Manager` is defined) without tripping over the include cycle.
class Manager;

using namespace kemena;
namespace fs = std::filesystem;

// ===========================================================================
// In-game GUI (PanelGui) data model
//
// A .ui document describes a flat list of widgets (each storing its parent
// id and its ordered child ids). Storing the tree flat keeps serialization and
// recursive removal trivial while still allowing arbitrary nesting via the
// hierarchy panel. Layout values are authored in the document's reference
// resolution and scaled at draw time so the preview always fits the panel.
// ===========================================================================

/**
 * @brief The kind of widget a node represents.
 */
enum class GuiWidgetType
{
    Text,        ///< A plain text label.
    Button,      ///< Image button (background texture + optional text).
    Panel,       ///< Image background with nine-slice corners.
    Image,       ///< A standalone image.
    Spacer,      ///< Invisible layout spacer (occupies space, draws nothing).
    Line,        ///< Horizontal or vertical divider line.
    ScrollView,  ///< Clipping container with scrollbars over its children.
    ProgressBar, ///< Value bar between min and max.
    Toggle,      ///< Boolean checkbox.
    Slider,      ///< Float slider between min and max.
    InputField   ///< Single-line text input field.
};

/** @brief Human-readable name for a widget type (used by menus and inspector). */
const char* guiWidgetTypeName(GuiWidgetType t);

/** @brief True when the type can visually contain child widgets. */
bool guiWidgetTypeIsContainer(GuiWidgetType t);

/** @brief Widget types offered by the "Add Widget" dropdown, in menu order. */
const std::vector<GuiWidgetType>& guiWidgetTypes();

/**
 * @brief Where a widget anchors inside its parent's (or the canvas') rect.
 */
enum class GuiAnchor
{
    TopLeft, Top, TopRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    BottomLeft, Bottom, BottomRight
};

/** @brief Horizontal alignment of a widget's text. */
enum class GuiTextAlign { Left, Center, Right };

/** @brief How an image is stretched into its rect. */
enum class GuiImageMode { Simple, Sliced, Tiled, Filled };

/**
 * @brief How a texture is fitted inside a widget's rectangle.
 *
 * Mirrors the familiar CSS/Unity image fit modes:
 *  - Cover:     scale to fill the rect, preserving aspect; overflow is cropped.
 *  - Contain:   scale to fit inside the rect, preserving aspect (letterboxed).
 *  - Fill:      stretch to the rect (aspect may change).
 *  - ScaleDown: like Contain but never scales above the texture's native size.
 *  - None:      draw at native pixel size, centred (overflow is cropped).
 */
enum class GuiImageFit { Cover, Contain, Fill, ScaleDown, None };

/** @brief Human-readable name for an image fit mode. */
const char* guiImageFitName(GuiImageFit f);

/** @brief GPU texture plus its pixel size, resolved for a .ui image asset. */
struct GuiTextureInfo
{
    uint32_t glId = 0;                           ///< GL texture id (0 = none available).
    ImVec2   size = ImVec2(0.0f, 0.0f);          ///< Texture size in pixels.
};

/**
 * @brief Callback that resolves a .ui image asset UUID to its GPU texture.
 *
 * Lets the runtime renderer (a free function with no Manager access) draw real
 * images without depending on the editor's asset system.
 */
using GuiTextureResolver = std::function<GuiTextureInfo(const std::string& assetUuid)>;

/**
 * @brief 2D affine transform used to rotate/scale a widget's rendered subtree.
 *
 * Stored row-major as `x' = m[0]*x + m[1]*y + m[2]` and
 * `y' = m[3]*x + m[4]*y + m[5]`, which keeps composition and inversion trivial.
 * A widget's own transform is applied about its rectangle centre so rotation and
 * scale never move the widget's authored anchor position.
 */
struct GuiAffine
{
    float m[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    /** @brief Build a scale-then-rotate transform about @p pivot. */
    static GuiAffine fromRotScale(ImVec2 pivot, float scale, float rotationDeg);

    /** @brief Compose transforms; the result applies @p rhs first, then `*this`. */
    GuiAffine compose(const GuiAffine& rhs) const;

    /** @brief Apply the transform to a point. */
    ImVec2 transform(ImVec2 p) const;

    /** @brief Inverse transform (identity when the matrix is singular). */
    GuiAffine inverse() const;
};

/** @brief Which axes a scroll view scrolls along. */
enum class GuiScrollDirection { Vertical, Horizontal, Both };

/**
 * @brief A single in-game GUI widget.
 *
 * Position/size are authored relative to the parent's content rect (or the
 * canvas for root widgets) in reference-resolution units. `anchor` decides
 * which point of the parent the widget's position is measured from, which is
 * what makes HUDs stick to screen edges/corners.
 */
struct GuiWidget
{
    int           id       = -1;             ///< Unique id within the layout.
    int           parentId = -1;             ///< Owning widget id, or -1 for a root widget.
    std::vector<int> children;               ///< Ordered child ids.
    GuiWidgetType type     = GuiWidgetType::Panel;
    std::string   name     = "Widget";

    // --- Layout -----------------------------------------------------------
    float x        = 0.0f;   ///< Position along X (relative to the anchor point).
    float y        = 0.0f;   ///< Position along Y (relative to the anchor point).
    float width    = 200.0f; ///< Width in reference units.
    float height   = 100.0f; ///< Height in reference units.
    GuiAnchor anchor = GuiAnchor::TopLeft; ///< Anchor used to place the widget.
    float rotation = 0.0f;   ///< Rotation in degrees (preview only).
    float scale    = 1.0f;   ///< Uniform content/local scale.
    bool  visible  = true;   ///< Hidden widgets are skipped in the preview.
    bool  interactable = true; ///< Disables hit-testing when false.

    // --- Appearance -------------------------------------------------------
    float color[4]       = { 0.20f, 0.22f, 0.26f, 1.00f }; ///< Background/tint colour.
    float textColor[4]   = { 1.00f, 1.00f, 1.00f, 1.00f }; ///< Text colour.
    float borderColor[4] = { 0.35f, 0.38f, 0.42f, 1.00f }; ///< Border colour.
    float borderWidth    = 0.0f;   ///< Border thickness in reference units.
    float cornerRadius   = 0.0f;   ///< Rounded-corner radius in reference units.
    bool  fillBackground = true;   ///< Draw the background rect.
    bool  showBorder     = false;  ///< Draw the border rect.

    // --- Nine-slice (Panel / Sliced images) -------------------------------
    float sliceLeft   = 8.0f;  ///< Left border slice width.
    float sliceRight  = 8.0f;  ///< Right border slice width.
    float sliceTop    = 8.0f;  ///< Top border slice height.
    float sliceBottom = 8.0f;  ///< Bottom border slice height.
    bool  showCorners = true;  ///< Draw the nine-slice corner guides in the preview.

    // --- Text -------------------------------------------------------------
    std::string  text      = "Text";  ///< Label / placeholder content.
    float        fontSize  = 16.0f;   ///< Font size in reference units.
    GuiTextAlign textAlign = GuiTextAlign::Center; ///< Horizontal text alignment.
    bool         wordWrap  = false;   ///< Wrap text inside the widget rect.

    // --- Image / Button ---------------------------------------------------
    std::string   textureUuid;                          ///< Project image asset UUID ("" = none).
    GuiImageMode  imageMode = GuiImageMode::Simple;     ///< How the texture fills the rect.
    GuiImageFit   imageFit  = GuiImageFit::Fill;        ///< How the texture is fitted into the rect.
    float         fillAmount = 1.0f;                    ///< Filled image mode amount (0..1).

    // --- ScrollView -------------------------------------------------------
    GuiScrollDirection scrollDirection = GuiScrollDirection::Vertical;
    bool  showScrollbar = true; ///< Draw a scrollbar gutter in the preview.
    float scrollPosition = 0.0f; ///< Normalised scroll offset (0..1) shown in the preview.

    // --- ProgressBar / Slider ---------------------------------------------
    float minValue = 0.0f; ///< Range minimum.
    float maxValue = 1.0f; ///< Range maximum.
    float value    = 0.5f; ///< Current value / fill fraction.

    // --- Toggle -----------------------------------------------------------
    bool checked = true;   ///< Toggle state shown in the preview.

    // --- Line -------------------------------------------------------------
    bool vertical = false; ///< Draw the divider vertically instead of horizontally.
};

/**
 * @brief A whole in-game GUI document (.ui).
 */
struct GuiLayout
{
    std::string uuid;             ///< Unique identifier of this layout.
    std::string name;             ///< Display name.
    bool        dirty = false;    ///< True when unsaved changes exist.

    int canvasWidth  = 1920;      ///< Reference canvas width.
    int canvasHeight = 1080;      ///< Reference canvas height;
    float canvasColor[4] = { 0.10f, 0.11f, 0.13f, 1.00f }; ///< Preview background colour.
    bool  showCanvasGrid = true;  ///< Draw a thirds/grid overlay in the preview.

    std::vector<GuiWidget> widgets; ///< Flat list of all widgets.
    int nextId = 1;                 ///< Id counter for new widgets.

    /** @brief Generate a new unique widget id. */
    int newId() { return nextId++; }

    /** @brief Find a widget by id, or nullptr. */
    GuiWidget* findWidget(int id);
    const GuiWidget* findWidget(int id) const;

    /** @brief Return the root widget (parentId == -1) at or below @p index. */
    std::vector<int> rootIds() const;

    /**
     * @brief Create a widget of @p type under @p parentId (or the canvas when -1).
     * @return The id of the newly created widget.
     */
    int addWidget(GuiWidgetType type, int parentId);

    /** @brief Remove a widget and every descendant of it. */
    void removeWidget(int id);

    /** @brief Detach @p id from its current parent and re-parent it, preserving world position. */
    bool reparentWidget(int id, int newParentId);

    /** @brief Serialize to JSON. */
    nlohmann::json toJson() const;

    /** @brief Deserialize from JSON. */
    void fromJson(const nlohmann::json& j);
};

// ===========================================================================
// PanelGui — the in-game GUI editor panel
// ===========================================================================

/**
 * @brief Editor panel for authoring in-game GUI layouts (.ui files).
 *
 * Layout mirrors the Animator panel: a top toolbar, a left hierarchy column
 * with an "Add Widget" dropdown, a resizable splitter, and a right-hand
 * preview canvas that renders the layout and supports selecting/dragging
 * widgets. The Inspector panel shows the selected widget's properties.
 */
class PanelGui
{
public:
    bool focused = false;  ///< Set each draw() — used by main.cpp's Ctrl+S routing.
    bool visible = false;  ///< True while the GUI editor window is open.

    PanelGui(kGuiManager* setGui, Manager* setManager);

    /** @brief Draw the panel and handle interaction. */
    void draw(bool& isOpened);

    /** @brief Open a .ui file into the editor. */
    void openFile(const std::string& path);

    /** @brief Returns the currently open .ui file path ("" when unsaved). */
    std::string getFilePath() const { return filePath; }

    /** @brief Save the current GUI layout. */
    void saveCurrent() { saveLayout(); }

    /** @brief True when a widget is currently selected. */
    bool hasSelectedWidget() const { return selectedWidget >= 0; }

    /** @brief Id of the currently selected widget, or -1 when none. */
    int getSelectedWidget() const { return selectedWidget; }

    /** @brief Draw the property form for the selected widget (called from the Inspector panel). */
    void drawSelectedInspector();

private:
    // -----------------------------------------------------------------------
    // Core state
    // -----------------------------------------------------------------------
    kGuiManager* gui     = nullptr;
    Manager*     manager = nullptr;

    GuiLayout   layout;   ///< The GUI document being edited.
    std::string filePath; ///< Current .ui file path (empty = unsaved).

    // -----------------------------------------------------------------------
    // Layout / interaction state
    // -----------------------------------------------------------------------
    float hierarchyPanelWidth = 260.0f; ///< Width of the left hierarchy column.
    int   selectedWidget      = -1;     ///< Selected widget id, or -1.
    int   dragWidget          = -1;     ///< Widget currently being dragged in the preview.
    bool  isDraggingWidget    = false;
    char  searchBuf[128]      = { 0 };  ///< Hierarchy filter text.
    char  textureSearchBuf[128] = { 0 };///< Filter text for the GUI texture picker.

    /// Resolved GPU textures for .ui image widgets, keyed by asset UUID.
    struct GuiTexEntry
    {
        uint32_t glId     = 0;            ///< GL texture id (0 when unavailable).
        ImVec2   size     = ImVec2(0, 0); ///< Texture size in pixels.
        bool     resolved = false;        ///< True once a load was attempted.
    };
    std::unordered_map<std::string, GuiTexEntry> guiTextureCache;
    std::unordered_map<std::string, uint32_t> guiThumbnailCache; ///< GL ids of picker thumbnails.
    std::string lastProjectPath;            ///< Project the texture caches belong to.

    // Rename popup
    int  renameTarget = -1;                 ///< Widget being renamed; -1 = none.
    char renameBuf[128] = { 0 };            ///< Rename text buffer.
    bool openRenamePopup = false;           ///< Request to open the rename modal.

    // Canvas navigation
    ImVec2 canvasOffset = { 0.0f, 0.0f };   ///< Pannable preview offset in screen pixels.
    float  canvasZoom   = 1.0f;             ///< Additional user zoom on top of fit-to-panel.
    bool   isPanning    = false;
    ImVec2 panStartMouse;
    ImVec2 panStartOffset;

    // Cached preview transform for the current frame (set by drawPreview).
    ImVec2 previewOrigin = { 0.0f, 0.0f };  ///< Screen-space top-left of the canvas.
    float  previewScale  = 1.0f;            ///< Reference units -> screen pixels.

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------
    void drawToolbar();
    void drawHierarchyPanel();
    void drawHierarchySplitter();
    void drawPreview();

    /**
     * @brief Recursively draw a widget (and its children) into the preview.
     * @param accWorld Transform accumulated from the widget's ancestors (applied
     *                 after this widget's own rotation/scale).
     */
    void drawWidget(ImDrawList* dl, const GuiWidget& w,
                    ImVec2 parentRefPos, ImVec2 parentSize,
                    const GuiAffine& accWorld = GuiAffine{});

    /**
     * @brief Recursively hit-test widgets, returning the topmost id under @p mouse.
     * @param accWorld Transform accumulated from the widget's ancestors, used to
     *                 map the mouse back into the widget's untransformed rect.
     */
    int hitTestWidgets(const GuiWidget& w, ImVec2 parentRefPos, ImVec2 parentSize,
                       ImVec2 mouse, const GuiAffine& accWorld = GuiAffine{}) const;

    /** @brief Compute a widget's top-left in reference space, honouring its anchor. */
    ImVec2 widgetRefPos(const GuiWidget& w, ImVec2 parentRefPos, ImVec2 parentSize) const;

    /** @brief Convert a reference-space point to screen space using the cached preview transform. */
    ImVec2 refToScreen(ImVec2 ref) const
    {
        return ImVec2(previewOrigin.x + ref.x * previewScale,
                      previewOrigin.y + ref.y * previewScale);
    }

    /** @brief Recursively draw one row of the hierarchy tree. */
    void drawHierarchyNode(int id, int depth);

    /** @brief Add a child widget to the current selection (or the canvas). */
    int addWidget(GuiWidgetType type);

    /** @brief Build a unique default name for a widget type. */
    std::string uniqueName(const std::string& base) const;

    void drawRenamePopup();
    void drawAddWidgetMenu();

    /**
     * @brief Texture picker popup for Image/Button widgets.
     *
     * Lists only project images imported with the "GUI" image type and assigns
     * the chosen asset UUID to @p w->textureUuid.
     */
    void drawTexturePickerPopup(GuiWidget* w);

    /** @brief Resolve a .ui image asset UUID to its GPU texture (cached). */
    GuiTextureInfo resolveWidgetTexture(const std::string& assetUuid);

    /** @brief Resolve an asset UUID to its project thumbnail GL id (cached, 0 = none). */
    uint32_t resolveThumbnail(const std::string& assetUuid);

    // File I/O
    void newLayout();
    void saveLayout();
    void saveLayoutAs();
    void loadLayout(const std::string& path);

    static std::string generateUuid();

    /** @brief SDL file-dialog callback for Save As. */
    static void SDLCALL saveGuiCallback(void* userdata, const char* const* filelist, int filter);
};

// ===========================================================================
// Runtime helpers — used by the Game panel to display .ui layouts placed in
// the scene (see Manager::getObjectUiAssets / getGuiLayoutForAsset).
// Declared after GuiLayout so the type is complete.
// ===========================================================================

/** @brief Load a .ui layout from disk. @return false when the file cannot be parsed. */
bool loadGuiLayoutFromFile(const std::string& path, GuiLayout& outLayout);

/**
 * @brief Render a layout into an ImGui draw list, stretched to fill a screen rect.
 *
 * Used at runtime (Game panel) where the layout must scale to the viewport
 * rather than to the editor's preview transform. The document's reference canvas
 * size is only an authoring guide: the layout is scaled independently on each
 * axis to fill the whole destination rect, so widgets land on the true viewport
 * edges instead of being letterboxed to the saved aspect ratio.
 *
 * @param dl      Target draw list.
 * @param layout  Layout to render.
 * @param rectMin Screen-space top-left of the destination rectangle.
 * @param rectMax Screen-space bottom-right of the destination rectangle.
 */
void renderGuiLayout(ImDrawList* dl, const GuiLayout& layout, ImVec2 rectMin, ImVec2 rectMax,
                     const GuiTextureResolver& resolveTexture = {});
