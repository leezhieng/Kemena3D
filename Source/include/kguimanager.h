/**
 * @file kguimanager.h
 * @brief Thin wrapper around Dear ImGui for building in-engine UI.
 */

#ifndef KGUIMANAGER_H
#define KGUIMANAGER_H

#include "kexport.h"

#include "kdatatype.h"
#include "krenderer.h"
#include "kwindow.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"
#ifdef KEMENA_D3D11
#include "backends/imgui_impl_dx11.h"
#endif

namespace kemena
{
    /**
     * @brief Wraps Dear ImGui initialisation, event forwarding, and widget calls.
     *
     * Call init() once after the renderer is ready, then each frame:
     * -# Forward input events with processEvent().
     * -# Open a frame with canvasStart().
     * -# Build UI using the widget methods.
     * -# Close the frame with canvasEnd().
     *
     * @code
     *   kGuiManager gui;
     *   gui.init(&renderer);
     *   // per-frame:
     *   gui.canvasStart();
     *   gui.windowStart("My Window");
     *   gui.text("Hello, world!");
     *   gui.windowEnd();
     *   gui.canvasEnd();
     * @endcode
     */
    class KEMENA3D_API kGuiManager
    {
    public:
        /** @brief Constructs an uninitialised GUI manager; call init() before use. */
        kGuiManager();

        /** @brief Destroys the GUI manager, releasing any held ImGui resources. */
        virtual ~kGuiManager();

        /**
         * @brief Initialises ImGui with the given renderer's context and window.
         * @param newRenderer Renderer whose SDL window and driver context are used.
         */
        void init(kRenderer *newRenderer);

        /**
         * @brief Forwards a system event to ImGui for input processing.
         * @param event Engine event struct wrapping the underlying SDL event.
         */
        void processEvent(kSystemEvent event);

        /**
         * @brief Loads and sets the default ImGui font from a Windows resource.
         * @param resourceName Resource identifier for the .ttf font data.
         */
        void loadDefaultFontFromResource(kString resourceName);

        /** @brief Begins a new ImGui frame (call once per render frame). */
        void canvasStart();

        /** @brief Renders all ImGui draw data and ends the frame. */
        void canvasEnd();

        // ---- Window ----

        /**
         * @brief Begins an ImGui window.
         * @param title  Window title bar text.
         * @param open   Optional pointer to a bool controlling visibility; nullptr to ignore.
         * @param flags  ImGuiWindowFlags combination.
         */
        void windowStart(kString title, bool *open = nullptr, ImGuiWindowFlags flags = 0);

        /** @brief Ends the current ImGui window. */
        void windowEnd();

        /** @brief Returns the inner size of the current window in pixels. */
        kVec2 getWindowSize();

        /** @brief Returns the screen-space position of the current window. */
        kVec2 getWindowPos();

        /**
         * @brief Sets the size of the next window before it is created.
         * @param size Desired window size in pixels.
         * @param cond Condition flag (ImGuiCond).
         */
        void setNextWindowSize(kVec2 size, ImGuiCond cond = 0);

        /**
         * @brief Sets the position of the next window before it is created.
         * @param pos   Desired screen-space position.
         * @param cond  Condition flag.
         * @param pivot Normalised pivot point within the window.
         */
        void setNextWindowPos(kVec2 pos, ImGuiCond cond = 0, kVec2 pivot = kVec2(0, 0));

        /**
         * @brief Sets the content size of the next window.
         * @param size Content region size in pixels.
         */
        void setNextWindowContentSize(kVec2 size);

        /**
         * @brief Sets the collapsed state of the next window.
         * @param collapsed true to start collapsed.
         * @param cond      Condition flag.
         */
        void setNextWindowCollapsed(bool collapsed, ImGuiCond cond = 0);

        /** @brief Gives input focus to the next window. */
        void setNextWindowFocus();

        /**
         * @brief Sets the background transparency of the next window.
         * @param alpha Alpha value in [0, 1].
         */
        void setNextWindowBgAlpha(float alpha);

        /**
         * @brief Returns whether the current window has keyboard focus.
         * @param flags ImGuiFocusedFlags.
         * @return true if focused.
         */
        bool isWindowFocused(ImGuiFocusedFlags flags = 0);

        /**
         * @brief Returns whether the mouse is hovering over the current window.
         * @param flags ImGuiHoveredFlags.
         * @return true if hovered.
         */
        bool isWindowHovered(ImGuiHoveredFlags flags = 0);

        /**
         * @brief Returns the content region minimum bound in window-local coordinates.
         * @note Prefer GetCursorScreenPos() + GetContentRegionAvail() for new code.
         */
        kVec2 getWindowContentRegionMin();

        /**
         * @brief Returns the content region maximum bound in window-local coordinates.
         * @note Prefer GetCursorScreenPos() + GetContentRegionAvail() for new code.
         */
        kVec2 getWindowContentRegionMax();

        /** @brief Returns the center of the main viewport in screen coordinates. */
        kVec2 getMainViewportCenter();

        // ---- Child Window ----

        /**
         * @brief Begins a scrollable child region.
         * @param id          Unique identifier.
         * @param size        Size in pixels (0 = fill remaining; negative = remaining minus value).
         * @param childFlags  ImGuiChildFlags (e.g. ImGuiChildFlags_Borders).
         * @param windowFlags ImGuiWindowFlags.
         * @return true if the child region is visible; always call childEnd() regardless.
         */
        bool childStart(kString id, kVec2 size = kVec2(0, 0), ImGuiChildFlags childFlags = 0, ImGuiWindowFlags windowFlags = 0);

        /** @brief Ends the current child region. Always call this after childStart(). */
        void childEnd();

        // ---- Dockspace ----

        /**
         * @brief Begins a full-window docking area.
         * @param name Unique dockspace identifier.
         */
        void dockSpaceStart(kString name);

        /** @brief Ends the current dockspace (no-op; kept for symmetry). */
        void dockSpaceEnd();

        // ---- Menu ----

        /** @brief Begins the main menu bar. @return true if the bar is visible. */
        bool menuBar();
        /** @brief Ends the main menu bar. */
        void menuBarEnd();

        /**
         * @brief Begins a named menu.
         * @param text Menu label.
         * @return true if the menu is open.
         */
        bool menu(kString text);
        /** @brief Ends the current menu. */
        void menuEnd();

        /**
         * @brief Adds a clickable menu item.
         * @param text     Item label.
         * @param shortcut Shortcut hint displayed on the right (display only).
         * @param selected Whether the item appears checked.
         * @param enabled  Whether the item is interactive.
         * @return true if the item was clicked.
         */
        bool menuItem(kString text, kString shortcut = "", bool selected = false, bool enabled = true);

        /**
         * @brief Adds a toggleable menu item backed by a bool.
         * @param text     Item label.
         * @param shortcut Shortcut hint.
         * @param selected Pointer to the toggle state.
         * @param enabled  Whether the item is interactive.
         * @return true if the item was clicked.
         */
        bool menuItem(kString text, kString shortcut, bool *selected, bool enabled = true);

        // ---- Layout ----

        /** @brief Begins a layout group. */
        void groupStart();
        /** @brief Ends a layout group. */
        void groupEnd();

        /**
         * @brief Disables all user interaction and dims widgets.
         * @param disabled Pass true to enter disabled state (default).
         */
        void beginDisabled(bool disabled = true);

        /** @brief Ends a disabled section opened with beginDisabled(). */
        void endDisabled();

        /**
         * @brief Places the next widget on the same line as the previous one.
         * @param offsetFromStartX Horizontal offset from the left edge (0 = auto).
         * @param spacing          Spacing override (-1 = default).
         */
        void sameLine(float offsetFromStartX = 0.0f, float spacing = -1.0f);

        /** @brief Inserts a small vertical space. */
        void spacing();
        /** @brief Draws a horizontal separator line. */
        void separator();

        /**
         * @brief Draws a horizontal separator with a centred text label.
         * @param text Label to display.
         */
        void separatorText(kString text);

        /** @brief Inserts a blank line in the layout. */
        void newLine();

        /**
         * @brief Moves the cursor right by @p indentW pixels.
         * @param indentW Indent width (0 = use style default).
         */
        void indent(float indentW = 0.0f);

        /**
         * @brief Moves the cursor left by @p indentW pixels.
         * @param indentW Unindent width (0 = use style default).
         */
        void unindent(float indentW = 0.0f);

        /**
         * @brief Inserts an invisible widget of a given size for layout spacing.
         * @param size Size in pixels.
         */
        void dummy(kVec2 size);

        /**
         * @brief Moves the cursor to an absolute position within the window.
         * @param pos Position relative to the window content region.
         */
        void setCursorPos(kVec2 pos);

        /** @brief Sets the cursor X position. @param x X offset in pixels. */
        void setCursorPosX(float x);
        /** @brief Sets the cursor Y position. @param y Y offset in pixels. */
        void setCursorPosY(float y);

        /** @brief Returns the cursor position relative to the window. */
        kVec2 getCursorPos();
        /** @brief Returns the cursor X position. */
        float getCursorPosX();
        /** @brief Returns the cursor Y position. */
        float getCursorPosY();
        /** @brief Returns the cursor position in screen coordinates. */
        kVec2 getCursorScreenPos();

        /**
         * @brief Moves the cursor to an absolute screen-space position.
         * @param pos Absolute screen-space coordinates.
         */
        void setCursorScreenPos(kVec2 pos);

        /** @brief Returns the available content region size in the current window. */
        kVec2 getContentRegionAvail();

        /**
         * @brief Sets the width of the next widget.
         * @param itemWidth Width in pixels (negative values right-align).
         */
        void setNextItemWidth(float itemWidth);

        /**
         * @brief Pushes a width onto the item-width stack.
         * @param itemWidth Width in pixels.
         */
        void pushItemWidth(float itemWidth);
        /** @brief Pops the last item width from the stack. */
        void popItemWidth();
        /** @brief Returns the current item width calculated by ImGui. */
        float calcItemWidth();

        /**
         * @brief Pushes a text wrap position onto the stack.
         * @param wrapLocalPosX X position to wrap at (0 = wrap at window right).
         */
        void pushTextWrapPos(float wrapLocalPosX = 0.0f);
        /** @brief Pops the last text wrap position. */
        void popTextWrapPos();

        /** @brief Returns the height of a typical framed widget in pixels (FontSize + FramePadding * 2). */
        float getFrameHeight();

        /** @brief Returns the height of a framed widget plus item spacing (distance between two consecutive widget rows). */
        float getFrameHeightWithSpacing();

        // ---- ID Stack ----

        /** @brief Pushes a kString ID onto the ID stack. @param id ID kString. */
        void pushId(kString id);
        /** @brief Pushes an integer ID onto the ID stack. @param id Integer ID. */
        void pushId(int id);
        /** @brief Pushes a pointer ID onto the ID stack. @param ptr Pointer used as ID. */
        void pushId(const void *ptr);
        /** @brief Pops the last ID from the ID stack. */
        void popId();

        // ---- Style ----

        /**
         * @brief Pushes a colour override for an ImGui style element.
         * @param idx   ImGuiCol element to override.
         * @param color RGBA colour.
         */
        void pushStyleColor(ImGuiCol idx, kVec4 color);

        /**
         * @brief Pushes a packed colour override.
         * @param idx   ImGuiCol element.
         * @param color Packed ABGR colour.
         */
        void pushStyleColor(ImGuiCol idx, ImU32 color);

        /**
         * @brief Pops colour overrides from the style stack.
         * @param count Number of colour entries to pop (default 1).
         */
        void popStyleColor(int count = 1);

        /**
         * @brief Pushes a float style variable override.
         * @param idx ImGuiStyleVar to override.
         * @param val New value.
         */
        void pushStyleVar(ImGuiStyleVar idx, float val);

        /**
         * @brief Pushes a kVec2 style variable override.
         * @param idx ImGuiStyleVar to override.
         * @param val New value.
         */
        void pushStyleVar(ImGuiStyleVar idx, kVec2 val);

        /**
         * @brief Pops style variable overrides from the stack.
         * @param count Number of entries to pop (default 1).
         */
        void popStyleVar(int count = 1);

        /**
         * @brief Pushes a font onto the font stack.
         * @param font ImFont pointer loaded via ImGui::GetIO().Fonts.
         */
        void pushFont(ImFont *font);
        /** @brief Pops the last font from the font stack. */
        void popFont();

        /** @brief Returns the current scaled font size in pixels. */
        float getFontSize();

        // ---- Text ----

        /** @brief Renders a plain text kString. @param text Text to display. */
        void text(kString text);

        /**
         * @brief Renders coloured text.
         * @param color RGBA text colour.
         * @param text  Text to display.
         */
        void textColored(kVec4 color, kString text);

        /** @brief Renders greyed-out (disabled) text. @param text Text to display. */
        void textDisabled(kString text);

        /** @brief Vertically aligns the next widget so its baseline lines up with text. */
        void alignTextToFramePadding();
        /** @brief Renders text that wraps at the wrap position. @param text Text. */
        void textWrapped(kString text);

        /**
         * @brief Renders a label/value pair aligned to the value column.
         * @param label Column label.
         * @param text  Value text.
         */
        void labelText(kString label, kString text);

        /** @brief Renders text preceded by a bullet point. @param text Text. */
        void bulletText(kString text);
        /** @brief Renders a bullet point without text. */
        void bullet();

        /**
         * @brief Returns the rendered size of a text kString.
         * @param text                     Text to measure.
         * @param hideTextAfterDoubleHash  If true, text after "##" is not measured.
         * @param wrapWidth                Wrap width (-1 = no wrap).
         * @return Bounding size in pixels.
         */
        kVec2 calcTextSize(kString text, bool hideTextAfterDoubleHash = false, float wrapWidth = -1.0f);

        // ---- Buttons ----

        /**
         * @brief Renders a push-button.
         * @param text Label text.
         * @param size Button size in pixels (0 = auto).
         * @return true if the button was clicked.
         */
        bool button(kString text, kIvec2 size = kIvec2(0, 0));

        /**
         * @brief Renders a small inline push-button.
         * @param text Label text.
         * @return true if clicked.
         */
        bool smallButton(kString text);

        /**
         * @brief Renders an invisible hit-area button.
         * @param id    Unique identifier.
         * @param size  Button size.
         * @param flags ImGuiButtonFlags.
         * @return true if clicked.
         */
        bool invisibleButton(kString id, kVec2 size, ImGuiButtonFlags flags = 0);

        /**
         * @brief Renders an arrow-shaped button.
         * @param id  Unique identifier.
         * @param dir Arrow direction (ImGuiDir).
         * @return true if clicked.
         */
        bool arrowButton(kString id, ImGuiDir dir);

        /**
         * @brief Renders a radio button.
         * @param label  Button label.
         * @param active Whether this button is currently selected.
         * @return true if clicked.
         */
        bool radioButton(kString label, bool active);

        /**
         * @brief Renders a radio button linked to an integer variable.
         * @param label   Button label.
         * @param v       Pointer to the current selection value.
         * @param vButton Value this button represents.
         * @return true if clicked.
         */
        bool radioButton(kString label, int *v, int vButton);

        // ---- Checkbox ----

        /**
         * @brief Renders a checkbox.
         * @param text   Checkbox label.
         * @param output Pointer to the bool state.
         * @return true if the state changed.
         */
        bool checkbox(kString text, bool *output);

        /**
         * @brief Renders a checkbox backed by integer flags (signed).
         * @param label      Checkbox label.
         * @param flags      Pointer to the flags integer.
         * @param flagsValue Bit mask this checkbox controls.
         * @return true if the state changed.
         */
        bool checkboxFlags(kString label, int *flags, int flagsValue);

        /**
         * @brief Renders a checkbox backed by integer flags (unsigned).
         * @param label      Checkbox label.
         * @param flags      Pointer to the flags integer.
         * @param flagsValue Bit mask this checkbox controls.
         * @return true if the state changed.
         */
        bool checkboxFlags(kString label, unsigned int *flags, unsigned int flagsValue);

        // ---- Input Text ----

        /**
         * @brief Renders a single-line text input field.
         * @param label     Field label.
         * @param value     Reference to the kString being edited.
         * @param maxLength Maximum character count (default 256).
         * @param flags     ImGuiInputTextFlags.
         * @return true if the value changed.
         */
        bool inputText(kString label, kString &value, size_t maxLength = 256, ImGuiInputTextFlags flags = 0);

        /**
         * @brief Renders a multi-line text input field.
         * @param label     Field label.
         * @param value     Reference to the kString being edited.
         * @param maxLength Maximum character count.
         * @param size      Widget size (0 = auto).
         * @param flags     ImGuiInputTextFlags.
         * @return true if the value changed.
         */
        bool inputTextMultiline(kString label, kString &value, size_t maxLength = 1024, kVec2 size = kVec2(0, 0), ImGuiInputTextFlags flags = 0);

        /**
         * @brief Renders a single-line text input with a placeholder hint.
         * @param label     Field label.
         * @param hint      Greyed placeholder text shown when empty.
         * @param value     Reference to the kString being edited.
         * @param maxLength Maximum character count.
         * @param flags     ImGuiInputTextFlags.
         * @return true if the value changed.
         */
        bool inputTextWithHint(kString label, kString hint, kString &value, size_t maxLength = 256, ImGuiInputTextFlags flags = 0);

        // ---- Input Scalar ----

        /** @brief Single float input. @return true if changed. */
        bool inputFloat(kString label, float *v, float step = 0.0f, float stepFast = 0.0f, kString format = "%.3f", ImGuiInputTextFlags flags = 0);
        /** @brief Two-component float input. @return true if changed. */
        bool inputFloat2(kString label, float v[2], kString format = "%.3f", ImGuiInputTextFlags flags = 0);
        /** @brief Three-component float input. @return true if changed. */
        bool inputFloat3(kString label, float v[3], kString format = "%.3f", ImGuiInputTextFlags flags = 0);
        /** @brief Four-component float input. @return true if changed. */
        bool inputFloat4(kString label, float v[4], kString format = "%.3f", ImGuiInputTextFlags flags = 0);
        /** @brief Single integer input. @return true if changed. */
        bool inputInt(kString label, int *v, int step = 1, int stepFast = 100, ImGuiInputTextFlags flags = 0);
        /** @brief Two-component integer input. @return true if changed. */
        bool inputInt2(kString label, int v[2], ImGuiInputTextFlags flags = 0);
        /** @brief Three-component integer input. @return true if changed. */
        bool inputInt3(kString label, int v[3], ImGuiInputTextFlags flags = 0);
        /** @brief Four-component integer input. @return true if changed. */
        bool inputInt4(kString label, int v[4], ImGuiInputTextFlags flags = 0);
        /** @brief Double-precision float input. @return true if changed. */
        bool inputDouble(kString label, double *v, double step = 0.0, double stepFast = 0.0, kString format = "%.6f", ImGuiInputTextFlags flags = 0);

        // ---- Drag ----

        /** @brief Drag widget for a single float. @return true if changed. */
        bool dragFloat(kString label, float *v, float speed = 1.0f, float min = 0.0f, float max = 0.0f, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for two floats. @return true if changed. */
        bool dragFloat2(kString label, float v[2], float speed = 1.0f, float min = 0.0f, float max = 0.0f, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for three floats. @return true if changed. */
        bool dragFloat3(kString label, float v[3], float speed = 1.0f, float min = 0.0f, float max = 0.0f, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for four floats. @return true if changed. */
        bool dragFloat4(kString label, float v[4], float speed = 1.0f, float min = 0.0f, float max = 0.0f, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for a single integer. @return true if changed. */
        bool dragInt(kString label, int *v, float speed = 1.0f, int min = 0, int max = 0, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for two integers. @return true if changed. */
        bool dragInt2(kString label, int v[2], float speed = 1.0f, int min = 0, int max = 0, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for three integers. @return true if changed. */
        bool dragInt3(kString label, int v[3], float speed = 1.0f, int min = 0, int max = 0, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Drag widget for four integers. @return true if changed. */
        bool dragInt4(kString label, int v[4], float speed = 1.0f, int min = 0, int max = 0, kString format = "%d", ImGuiSliderFlags flags = 0);

        /**
         * @brief Drag widget for a float range (min + max pair).
         * @return true if either value changed.
         */
        bool dragFloatRange2(kString label, float *vCurrentMin, float *vCurrentMax, float speed = 1.0f, float min = 0.0f, float max = 0.0f, kString format = "%.3f", kString formatMax = "", ImGuiSliderFlags flags = 0);

        /**
         * @brief Drag widget for an integer range (min + max pair).
         * @return true if either value changed.
         */
        bool dragIntRange2(kString label, int *vCurrentMin, int *vCurrentMax, float speed = 1.0f, int min = 0, int max = 0, kString format = "%d", kString formatMax = "", ImGuiSliderFlags flags = 0);

        // ---- Slider ----

        /** @brief Horizontal slider for a single float. @return true if changed. */
        bool sliderFloat(kString label, float *v, float min, float max, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for two floats. @return true if changed. */
        bool sliderFloat2(kString label, float v[2], float min, float max, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for three floats. @return true if changed. */
        bool sliderFloat3(kString label, float v[3], float min, float max, kString format = "%.3f", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for four floats. @return true if changed. */
        bool sliderFloat4(kString label, float v[4], float min, float max, kString format = "%.3f", ImGuiSliderFlags flags = 0);

        /**
         * @brief Horizontal slider that displays and edits an angle in degrees.
         * @return true if changed.
         */
        bool sliderAngle(kString label, float *vRad, float vDegreesMin = -360.0f, float vDegreesMax = 360.0f, kString format = "%.0f deg", ImGuiSliderFlags flags = 0);

        /** @brief Horizontal slider for a single integer. @return true if changed. */
        bool sliderInt(kString label, int *v, int min, int max, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for two integers. @return true if changed. */
        bool sliderInt2(kString label, int v[2], int min, int max, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for three integers. @return true if changed. */
        bool sliderInt3(kString label, int v[3], int min, int max, kString format = "%d", ImGuiSliderFlags flags = 0);
        /** @brief Horizontal slider for four integers. @return true if changed. */
        bool sliderInt4(kString label, int v[4], int min, int max, kString format = "%d", ImGuiSliderFlags flags = 0);

        // ---- Color ----

        /** @brief RGB colour editor widget. @return true if changed. */
        bool colorEdit3(kString label, float col[3], ImGuiColorEditFlags flags = 0);
        /** @brief RGBA colour editor widget. @return true if changed. */
        bool colorEdit4(kString label, float col[4], ImGuiColorEditFlags flags = 0);
        /** @brief RGB colour picker widget. @return true if changed. */
        bool colorPicker3(kString label, float col[3], ImGuiColorEditFlags flags = 0);
        /** @brief RGBA colour picker widget. @return true if changed. */
        bool colorPicker4(kString label, float col[4], ImGuiColorEditFlags flags = 0);

        /**
         * @brief Renders a small coloured square button.
         * @param descId Unique identifier / tooltip.
         * @param col    Button colour.
         * @param flags  ImGuiColorEditFlags.
         * @param size   Button size (0 = default).
         * @return true if clicked.
         */
        bool colorButton(kString descId, kVec4 col, ImGuiColorEditFlags flags = 0, kVec2 size = kVec2(0, 0));

        /**
         * @brief Sets global colour edit display options.
         * @param flags ImGuiColorEditFlags combination.
         */
        void setColorEditOptions(ImGuiColorEditFlags flags);

        // ---- Combo / List / Selectable ----

        /**
         * @brief Drop-down combo box.
         * @param label              Widget label.
         * @param currentItem        Pointer to the selected index.
         * @param items              List of option strings.
         * @param popupMaxHeightInItems Maximum visible items in the popup (-1 = auto).
         * @return true if the selection changed.
         */
        bool combo(kString label, int *currentItem, std::vector<kString> items, int popupMaxHeightInItems = -1);

        /**
         * @brief Scrollable list box.
         * @param label         Widget label.
         * @param currentItem   Pointer to the selected index.
         * @param items         List of option strings.
         * @param heightInItems Visible row count (-1 = auto).
         * @return true if the selection changed.
         */
        bool listBox(kString label, int *currentItem, std::vector<kString> items, int heightInItems = -1);

        /**
         * @brief Selectable row (state passed by value).
         * @param label    Row label.
         * @param selected Whether this row is currently selected.
         * @param flags    ImGuiSelectableFlags.
         * @param size     Row size (0 = fill available).
         * @return true if clicked.
         */
        bool selectable(kString label, bool selected = false, ImGuiSelectableFlags flags = 0, kVec2 size = kVec2(0, 0));

        /**
         * @brief Selectable row (state passed by pointer).
         * @param label     Row label.
         * @param pSelected Pointer to the selection bool.
         * @param flags     ImGuiSelectableFlags.
         * @param size      Row size.
         * @return true if clicked.
         */
        bool selectable(kString label, bool *pSelected, ImGuiSelectableFlags flags = 0, kVec2 size = kVec2(0, 0));

        // ---- Tree / Collapsing ----

        /**
         * @brief Begins a tree node (uses label as ID).
         * @param label Node label.
         * @return true if the node is expanded.
         */
        bool treeStart(kString label);

        /**
         * @brief Begins a tree node with explicit ID and label.
         * @param id    Unique ID kString.
         * @param label Display label (empty = use id).
         * @param flags ImGuiTreeNodeFlags.
         * @return true if the node is expanded.
         */
        bool treeStartEx(kString id, kString label = "", ImGuiTreeNodeFlags flags = 0);

        /** @brief Ends the current tree node (use when treeStart returned true). */
        void treeEnd();
        /** @brief Pops the tree indent regardless of whether the node is open. */
        void treePop();

        /**
         * @brief Collapsible section header.
         * @param label Label text.
         * @param flags ImGuiTreeNodeFlags.
         * @return true if the section is expanded.
         */
        bool collapsingHeader(kString label, ImGuiTreeNodeFlags flags = 0);

        /**
         * @brief Collapsible section header with a close button.
         * @param label   Label text.
         * @param visible Pointer to the visibility bool.
         * @param flags   ImGuiTreeNodeFlags.
         * @return true if the section is expanded.
         */
        bool collapsingHeader(kString label, bool *visible, ImGuiTreeNodeFlags flags = 0);

        /**
         * @brief Sets the open state of the next tree node.
         * @param isOpen Whether the node should be open.
         * @param cond   Condition flag.
         */
        void setNextItemOpen(bool isOpen, ImGuiCond cond = 0);

        // ---- Image ----

        /**
         * @brief Renders a GPU texture as an image widget.
         * @param textureId GPU texture handle.
         * @param size      Display size in pixels.
         * @param uv0       Top-left UV coordinate.
         * @param uv1       Bottom-right UV coordinate.
         */
        void image(uint32_t textureId, kVec2 size, kVec2 uv0 = kVec2(0, 0), kVec2 uv1 = kVec2(1, 1));

        /**
         * @brief Renders a GPU texture as a clickable image button.
         * @param id        Unique identifier.
         * @param textureId GPU texture handle.
         * @param size      Display size in pixels.
         * @param uv0       Top-left UV coordinate.
         * @param uv1       Bottom-right UV coordinate.
         * @param tint      Tint color multiplied over the image (default white = no tint).
         * @return true if the button was clicked.
         */
        bool imageButton(kString id, uint32_t textureId, kVec2 size, kVec2 uv0 = kVec2(0, 0), kVec2 uv1 = kVec2(1, 1), kVec4 tint = kVec4(1, 1, 1, 1));

        // ---- Progress ----

        /**
         * @brief Renders a horizontal progress bar.
         * @param fraction Value in [0, 1] representing progress.
         * @param size     Bar size (-FLT_MIN width = fill available).
         * @param overlay  Optional text drawn over the bar.
         */
        void progressBar(float fraction, kVec2 size = kVec2(-FLT_MIN, 0), kString overlay = "");

        // ---- Tooltip ----

        /**
         * @brief Sets a tooltip shown when the last item is hovered.
         * @param text Tooltip text.
         */
        void setItemTooltip(kString text);

        /** @brief Begins a custom tooltip popup. */
        void beginTooltip();
        /** @brief Ends the current tooltip popup. */
        void endTooltip();

        // ---- Popup ----

        /**
         * @brief Opens a named popup on the next frame.
         * @param id    Popup identifier.
         * @param flags ImGuiPopupFlags.
         */
        void openPopup(kString id, ImGuiPopupFlags flags = 0);

        /**
         * @brief Begins rendering a named popup.
         * @param id    Popup identifier.
         * @param flags ImGuiWindowFlags.
         * @return true if the popup is open.
         */
        bool popupStart(kString id, ImGuiWindowFlags flags = 0);

        /** @brief Ends the current popup. */
        void popupEnd();

        /**
         * @brief Begins a modal popup dialog.
         * @param name  Dialog title.
         * @param open  Optional pointer to the open/close bool.
         * @param flags ImGuiWindowFlags.
         * @return true while the modal is open.
         */
        bool popupModal(kString name, bool *open = nullptr, ImGuiWindowFlags flags = 0);

        /**
         * @brief Begins a context menu popup on right-click of the last item.
         * @param id    Popup identifier.
         * @param flags ImGuiPopupFlags.
         * @return true if the context menu opened.
         */
        bool popupContextItemStart(kString id = "", ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);

        /**
         * @brief Begins a context menu popup on right-click of the current window.
         * @param id    Popup identifier.
         * @param flags ImGuiPopupFlags.
         * @return true if the context menu opened.
         */
        bool popupContextWindowStart(kString id = "", ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);

        /**
         * @brief Returns whether a named popup is currently open.
         * @param id    Popup identifier.
         * @param flags ImGuiPopupFlags.
         * @return true if open.
         */
        bool isPopupOpen(kString id, ImGuiPopupFlags flags = 0);

        /** @brief Closes the currently open popup. */
        void closeCurrentPopup();

        // ---- Tab Bar ----

        /**
         * @brief Begins a tab bar container.
         * @param id    Unique identifier.
         * @param flags ImGuiTabBarFlags.
         * @return true if the tab bar is visible.
         */
        bool tabBarStart(kString id, ImGuiTabBarFlags flags = 0);
        /** @brief Ends the current tab bar. */
        void tabBarEnd();

        /**
         * @brief Begins a tab item inside a tab bar.
         * @param label Item label.
         * @param open  Optional pointer to a bool controlling the close button.
         * @param flags ImGuiTabItemFlags.
         * @return true if this tab is active.
         */
        bool tabItemStart(kString label, bool *open = nullptr, ImGuiTabItemFlags flags = 0);
        /** @brief Ends the current tab item. */
        void tabItemEnd();

        /**
         * @brief Marks a tab (or docked window) as closed programmatically.
         * @param tabOrDockedWindowLabel Label of the tab or window to close.
         */
        void setTabItemClosed(kString tabOrDockedWindowLabel);

        // ---- Tables ----

        /**
         * @brief Begins a table layout.
         * @param id         Unique identifier.
         * @param columns    Number of columns.
         * @param flags      ImGuiTableFlags.
         * @param outerSize  Outer size constraint (0 = auto).
         * @param innerWidth Inner horizontal scrolling width.
         * @return true if the table is visible.
         */
        bool tableStart(kString id, int columns, ImGuiTableFlags flags = 0, kVec2 outerSize = kVec2(0, 0), float innerWidth = 0.0f);
        /** @brief Ends the current table. */
        void tableEnd();

        /**
         * @brief Advances to the next column in the current row.
         * @return true if the column is visible.
         */
        bool tableNextColumn();

        /**
         * @brief Sets the current column index explicitly.
         * @param columnN Zero-based column index.
         * @return true if the column is visible.
         */
        bool tableSetColumnIndex(int columnN);

        /**
         * @brief Begins a new table row.
         * @param rowFlags      ImGuiTableRowFlags.
         * @param minRowHeight  Minimum row height in pixels.
         */
        void tableNextRow(ImGuiTableRowFlags rowFlags = 0, float minRowHeight = 0.0f);

        /**
         * @brief Sets up a table column with optional flags and initial width.
         * @param label              Column header label.
         * @param flags              ImGuiTableColumnFlags.
         * @param initWidthOrWeight  Initial width (fixed) or weight (stretch).
         */
        void tableSetupColumn(kString label, ImGuiTableColumnFlags flags = 0, float initWidthOrWeight = 0.0f);

        /**
         * @brief Freezes leading columns/rows so they remain visible while scrolling.
         * @param cols Number of columns to freeze.
         * @param rows Number of rows to freeze.
         */
        void tableSetupScrollFreeze(int cols, int rows);

        /** @brief Renders the header row using labels set by tableSetupColumn(). */
        void tableHeadersRow();

        /**
         * @brief Renders a single header cell.
         * @param label Header cell label.
         */
        void tableHeader(kString label);

        // ---- Columns ----

        /**
         * @brief Begins a legacy multi-column layout.
         * @param count   Number of columns (1 = resets to single-column).
         * @param id      Optional unique identifier.
         * @param borders Whether to draw column border lines.
         * @note Prefer the Tables API for new code.
         */
        void columnsStart(int count = 1, kString id = "", bool borders = true);

        /** @brief Resets to a single-column layout, ending the columns region. */
        void columnsEnd();

        /** @brief Advances to the next column (wraps to the next row when needed). */
        void nextColumn();

        /**
         * @brief Returns the width of a column in pixels.
         * @param columnIndex Column index (-1 = current column).
         */
        float getColumnWidth(int columnIndex = -1);

        // ---- Scroll ----

        /** @brief Returns the current horizontal scroll position. */
        float getScrollX();
        /** @brief Returns the current vertical scroll position. */
        float getScrollY();
        /** @brief Sets the horizontal scroll position. @param scrollX Target position. */
        void setScrollX(float scrollX);
        /** @brief Sets the vertical scroll position. @param scrollY Target position. */
        void setScrollY(float scrollY);
        /** @brief Returns the maximum horizontal scroll position. */
        float getScrollMaxX();
        /** @brief Returns the maximum vertical scroll position. */
        float getScrollMaxY();
        /**
         * @brief Scrolls so the current item is visible horizontally.
         * @param centerXRatio 0 = left edge, 0.5 = centre, 1 = right edge.
         */
        void setScrollHereX(float centerXRatio = 0.5f);
        /**
         * @brief Scrolls so the current item is visible vertically.
         * @param centerYRatio 0 = top edge, 0.5 = centre, 1 = bottom edge.
         */
        void setScrollHereY(float centerYRatio = 0.5f);

        // ---- Item Queries ----

        /** @brief Returns whether the last widget is hovered. @param flags ImGuiHoveredFlags. */
        bool isItemHovered(ImGuiHoveredFlags flags = 0);
        /** @brief Returns whether the last widget is active (e.g. being dragged). */
        bool isItemActive();
        /** @brief Returns whether the last widget has keyboard focus. */
        bool isItemFocused();
        /** @brief Returns whether the last widget was clicked. @param mouseButton Mouse button index. */
        bool isItemClicked(ImGuiMouseButton mouseButton = 0);
        /** @brief Returns whether the last widget is visible in the clipping rectangle. */
        bool isItemVisible();
        /** @brief Returns whether the last widget's value was edited this frame. */
        bool isItemEdited();
        /** @brief Returns whether the last widget became active this frame. */
        bool isItemActivated();
        /** @brief Returns whether the last widget became inactive this frame. */
        bool isItemDeactivated();
        /** @brief Returns whether the last widget became inactive after being edited. */
        bool isItemDeactivatedAfterEdit();
        /** @brief Returns whether the last tree node was toggled open. */
        bool isItemToggledOpen();
        /** @brief Returns whether any widget is currently hovered. */
        bool isAnyItemHovered();
        /** @brief Returns whether any widget is currently active. */
        bool isAnyItemActive();
        /** @brief Returns whether any widget currently has focus. */
        bool isAnyItemFocused();
        /** @brief Returns the bounding box minimum of the last item (screen space). */
        kVec2 getItemRectMin();
        /** @brief Returns the bounding box maximum of the last item (screen space). */
        kVec2 getItemRectMax();
        /** @brief Returns the bounding box size of the last item. */
        kVec2 getItemRectSize();

        /**
         * @brief Allows the next item to overlap previously drawn items.
         * Call this before the item that should allow overlap.
         */
        void setNextItemAllowOverlap();

        // ---- Mouse ----

        /** @brief Returns whether a mouse button is held down. @param button ImGuiMouseButton. */
        bool isMouseDown(ImGuiMouseButton button);
        /** @brief Returns whether a mouse button was clicked this frame. @param button Button. @param repeat Enable repeat. */
        bool isMouseClicked(ImGuiMouseButton button, bool repeat = false);
        /** @brief Returns whether a mouse button was released this frame. @param button Button. */
        bool isMouseReleased(ImGuiMouseButton button);
        /** @brief Returns whether a mouse button was double-clicked this frame. @param button Button. */
        bool isMouseDoubleClicked(ImGuiMouseButton button);
        /**
         * @brief Returns whether the mouse is inside the given screen-space rectangle.
         * @param rMin Top-left corner.
         * @param rMax Bottom-right corner.
         * @param clip Whether to clip by the current window's rect.
         */
        bool isMouseHoveringRect(kVec2 rMin, kVec2 rMax, bool clip = true);
        /** @brief Returns the current mouse position in screen coordinates. */
        kVec2 getMousePos();
        /** @brief Returns the mouse position delta since the last frame. */
        kVec2 getMouseDelta();
        /** @brief Returns the vertical mouse wheel delta. */
        float getMouseWheel();

        // ---- Keyboard ----

        /** @brief Returns true if the Shift modifier key is currently held. */
        bool isKeyShift();

        /** @brief Returns true if the Ctrl modifier key is currently held. */
        bool isKeyCtrl();

        /** @brief Returns true if any ImGui widget currently wants text input. */
        bool getWantTextInput();

        /** @brief Returns the time in seconds elapsed since the last frame. */
        float getDeltaTime();

        // ---- Draw ----

        /**
         * @brief Draws a textured rectangle directly onto the window draw list.
         * @param textureId GPU texture handle.
         * @param pMin      Top-left corner in screen coordinates.
         * @param pMax      Bottom-right corner in screen coordinates.
         * @param uvMin     Top-left UV coordinate.
         * @param uvMax     Bottom-right UV coordinate.
         * @param tint      Tint color (default white = no tint).
         */
        void drawListAddImage(uint32_t textureId, kVec2 pMin, kVec2 pMax, kVec2 uvMin = kVec2(0, 0), kVec2 uvMax = kVec2(1, 1), kVec4 tint = kVec4(1, 1, 1, 1));

        /** @brief Returns the height of a single text line in pixels. */
        float getTextLineHeight();

        /** @brief Renders a raw string without label formatting. */
        void textUnformatted(kString text);

        // ---- Utility ----

        /**
         * @brief Sets the OS clipboard text.
         * @param text Text to place in the clipboard.
         */
        void setClipboardText(kString text);

        /**
         * @brief Saves the current ImGui layout state to a .ini file.
         * @param filename Path to the .ini file.
         */
        void saveIniSettingsToDisk(kString filename);

        /**
         * @brief Loads ImGui layout state from a .ini file.
         * @param filename Path to the .ini file.
         */
        void loadIniSettingsFromDisk(kString filename);

        /**
         * @brief Loads ImGui layout state from an in-memory .ini blob.
         * @param data Pointer to the ini text data.
         * @param size Number of bytes to read.
         */
        void loadIniSettingsFromMemory(const char *data, size_t size);

        /**
         * @brief Registers a custom .ini settings handler.
         * @param handler Fully configured ImGuiSettingsHandler struct.
         */
        void addSettingsHandler(ImGuiSettingsHandler handler);

        /**
         * @brief Shuts down ImGui and releases backend resources.
         */
        void destroy();

    protected:
    private:
        float      mainScale; ///< UI scale factor applied at initialisation.
        kRenderer *renderer;  ///< Owning renderer (provides window and driver context).
        bool       isD3D11 = false; ///< True when the D3D11 backend is active.
    };
}

#endif // KGUIMANAGER_H
