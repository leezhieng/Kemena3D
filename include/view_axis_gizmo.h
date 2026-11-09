#ifndef VIEW_AXIS_GIZMO_H
#define VIEW_AXIS_GIZMO_H

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <imgui.h>

/**
 * @brief Draws a 3D view-orientation gizmo in the top-right corner of a viewport.
 *
 * The six world axes (+X/-X, +Y/-Y, +Z/-Z) are rotated into view space with the
 * camera's view matrix and projected orthographically onto a small disc. Only
 * the rotation part of the view matrix is used, so the gizmo tracks the camera's
 * orientation and lets the user read the current view direction at a glance.
 *
 * The disc is anchored to the top-right corner of the supplied viewport
 * rectangle, which keeps it clear of the bottom-right camera preview overlay.
 *
 * @param drawList    ImGui draw list to render into (the host panel's window draw list).
 * @param viewMatrix  Camera view matrix (world space -> view space).
 * @param viewportMin Top-left corner of the viewport in screen coordinates.
 * @param viewportMax Bottom-right corner of the viewport in screen coordinates.
 * @param radius      Distance from the gizmo centre to each axis endpoint, in pixels.
 */
inline void drawViewAxisGizmo(ImDrawList *drawList, const glm::mat4 &viewMatrix,
                              const ImVec2 &viewportMin, const ImVec2 &viewportMax,
                              float radius = 42.0f)
{
    if (drawList == nullptr || radius <= 0.0f)
        return;

    const float margin   = 12.0f;
    const float bgRadius = radius + 7.0f;
    const ImVec2 center(viewportMax.x - margin - bgRadius,
                        viewportMin.y + margin + bgRadius);

    // Subtle backdrop so the axes stay readable over bright scene content.
    drawList->AddCircleFilled(center, bgRadius, IM_COL32(18, 20, 24, 140), 48);
    drawList->AddCircle(center, bgRadius, IM_COL32(210, 215, 225, 70), 48, 1.0f);

    struct AxisDraw
    {
        ImVec2 tip;      ///< Projected screen-space endpoint.
        float depth;     ///< View-space Z of the endpoint (larger = nearer the camera).
        float projLen;   ///< Length of the projected direction (0 = pointing at the camera).
        const char *label;
        ImU32 color;
        bool positive;
    };

    static const glm::vec3 kAxisDirs[6] = {
        { 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        { 0.0f, 1.0f, 0.0f}, { 0.0f, -1.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f}, { 0.0f, 0.0f, -1.0f}
    };
    static const char *kAxisLabels[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    static const ImU32 kAxisColors[3] = {
        IM_COL32(235, 82, 82, 255),  // X - red
        IM_COL32(110, 205, 90, 255), // Y - green
        IM_COL32(80, 145, 245, 255)  // Z - blue
    };

    // glm's lookAt-based view matrix is orthonormal, so its upper-left 3x3 is a
    // pure rotation: it maps world directions directly into view space.
    const glm::mat3 rot(viewMatrix);

    AxisDraw axes[6];
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec3 v = rot * kAxisDirs[i];
        axes[i].tip      = ImVec2(center.x + v.x * radius, center.y - v.y * radius);
        axes[i].depth    = v.z; // +Z points toward the viewer in view space
        axes[i].projLen  = std::sqrt(v.x * v.x + v.y * v.y);
        axes[i].label    = kAxisLabels[i];
        axes[i].positive = (i % 2) == 0;

        ImU32 base = kAxisColors[i / 2];
        if (!axes[i].positive)
        {
            // Dim the negative half of each axis so the positive direction reads first.
            const int r = (int)((base >> IM_COL32_R_SHIFT) & 0xFF);
            const int g = (int)((base >> IM_COL32_G_SHIFT) & 0xFF);
            const int b = (int)((base >> IM_COL32_B_SHIFT) & 0xFF);
            base = IM_COL32((int)(r * 0.62f), (int)(g * 0.62f), (int)(b * 0.62f), 255);
        }
        axes[i].color = base;
    }

    // Painter's algorithm: draw the farthest endpoints first so the nearer ones
    // occlude them correctly when the camera looks almost straight down an axis.
    std::sort(axes, axes + 6, [](const AxisDraw &a, const AxisDraw &b) {
        return a.depth < b.depth;
    });

    ImFont *font = ImGui::GetFont();
    const float uiFontSize = ImGui::GetFontSize();
    const float fontSize = uiFontSize * 0.70f;
    // Grow the endpoint balls with the UI font so the labels keep fitting at
    // higher DPI / UI scales.
    const float ballScale = std::clamp(uiFontSize / 13.0f, 0.85f, 1.8f);

    auto addCenteredLabel = [&](const ImVec2 &pos, ImU32 col, const char *text) {
        if (font == nullptr)
            return;
        const ImVec2 ts = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, text);
        drawList->AddText(font, fontSize,
                          ImVec2(pos.x - ts.x * 0.5f, pos.y - ts.y * 0.5f), col, text);
    };

    // Small hub dot marking the gizmo origin.
    drawList->AddCircleFilled(center, 2.5f, IM_COL32(220, 224, 232, 160), 12);

    for (const AxisDraw &a : axes)
    {
        // Fade with depth so the far side of the gizmo reads as farther away.
        float t = (a.depth + 1.0f) * 0.5f; // 0 = away from viewer, 1 = toward viewer
        t = std::clamp(t, 0.0f, 1.0f);
        const int alpha = (int)(120.0f + 135.0f * t);
        const ImU32 axisCol = (a.color & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);

        // Stem from the hub to the endpoint. Skipped when the axis points almost
        // straight at/away from the camera (its projection collapses to a dot).
        if (a.projLen > 0.08f)
            drawList->AddLine(center, a.tip, axisCol, a.positive ? 1.8f : 1.2f);

        if (a.positive)
        {
            drawList->AddCircleFilled(a.tip, 8.0f * ballScale, axisCol, 16);
            addCenteredLabel(a.tip, IM_COL32(15, 17, 20, alpha), a.label);
        }
        else
        {
            // Hollow ball: the ring carries the axis colour, the label stays
            // light so it reads against the dark inset fill.
            const float ballR = 7.0f * ballScale;
            drawList->AddCircleFilled(a.tip, ballR, IM_COL32(18, 20, 24, alpha), 16);
            drawList->AddCircle(a.tip, ballR, axisCol, 16, 1.4f);
            addCenteredLabel(a.tip, IM_COL32(215, 220, 230, alpha), a.label);
        }
    }
}

#endif // VIEW_AXIS_GIZMO_H
