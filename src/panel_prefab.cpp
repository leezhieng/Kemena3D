#include "panel_prefab.h"

#include <algorithm>

#include <glm/gtx/matrix_decompose.hpp>

PanelPrefab::PanelPrefab(kGuiManager *setGuiManager, Manager *setManager)
{
    gui     = setGuiManager;
    manager = setManager;
}

void PanelPrefab::draw(bool &isOpened)
{
    enabled = manager->projectOpened && manager->prefabEditing;

    // The panel auto-shows when prefab editing starts and auto-hides when it ends.
    if (enabled && !isOpened) isOpened = true;
    if (!enabled && isOpened) isOpened = false;

    if (!isOpened || manager->prefabCamera == nullptr)
    {
        // Keep the flags truthful so the main loop's hierarchy-context tracking
        // never sees an off-screen Prefab panel as focused.
        hovered = false;
        focused = false;
        return;
    }

    gui->beginDisabled(!enabled);
    gui->windowStart("Prefab", &isOpened);

    // If the close button was clicked, save & exit the prefab editor.
    if (!isOpened)
    {
        hovered = false;
        focused = false;
        manager->closePrefabEditor(/*saveChanges*/ true);
        gui->windowEnd();
        gui->endDisabled();
        return;
    }

    gui->text(kString("Editing: ") + manager->editingPrefab.getName());

    // ------------------------------------------------------------------
    // Toolbar — scene-settings drop-downs + Apply / Revert.
    //
    // Lighting / Sky / Sky Ambient / Preview Camera each open a popup that
    // edits manager->prefabSceneSettings, which is immediately pushed onto the
    // prefab scene. Apply/Revert become enabled as soon as the prefab (subtree
    // or settings) diverges from the last saved state.
    // ------------------------------------------------------------------
    Manager::PrefabSceneSettings &ps = manager->prefabSceneSettings;

    if (ImGui::Button("Lighting"))
        ImGui::OpenPopup("PrefabLighting");
    if (ImGui::BeginPopup("PrefabLighting"))
    {
        ImGui::TextUnformatted("Lighting");
        ImGui::Separator();
        gui->checkbox("Sun Light", &ps.sunEnabled);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Power", &ps.sunPower, 0.0f, 10.0f, "%.2f");
        {
            float c[3] = {ps.sunDiffuse.x, ps.sunDiffuse.y, ps.sunDiffuse.z};
            if (ImGui::ColorEdit3("Sun Color", c))
                ps.sunDiffuse = kVec3(c[0], c[1], c[2]);
        }
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Pitch", &ps.sunPitch, -89.0f, 89.0f, "%.0f");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Yaw", &ps.sunYaw, -180.0f, 180.0f, "%.0f");
        ImGui::Separator();
        {
            float a[3] = {ps.ambientColor.x, ps.ambientColor.y, ps.ambientColor.z};
            if (ImGui::ColorEdit3("Ambient", a))
                ps.ambientColor = kVec3(a[0], a[1], a[2]);
        }
        manager->applyPrefabSceneSettings();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Sky"))
        ImGui::OpenPopup("PrefabSky");
    if (ImGui::BeginPopup("PrefabSky"))
    {
        ImGui::TextUnformatted("Sky");
        ImGui::Separator();
        gui->checkbox("Show Skybox", &ps.skyboxEnabled);
        if (ImGui::Button("Reapply Default"))
        {
            ps.skyboxEnabled = true;
            manager->applyDefaultSkyboxToPrefab();
        }
        manager->applyPrefabSceneSettings();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Sky Ambient"))
        ImGui::OpenPopup("PrefabSkyAmbient");
    if (ImGui::BeginPopup("PrefabSkyAmbient"))
    {
        ImGui::TextUnformatted("Sky Ambient");
        ImGui::Separator();
        gui->checkbox("Enabled", &ps.skyAmbientEnabled);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Strength", &ps.skyAmbientStrength, 0.0f, 5.0f, "%.2f");
        manager->applyPrefabSceneSettings();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Preview Camera"))
        ImGui::OpenPopup("PrefabPreviewCamera");
    if (ImGui::BeginPopup("PrefabPreviewCamera"))
    {
        ImGui::TextUnformatted("Preview Camera");
        ImGui::Separator();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("FOV", &ps.camFOV, 20.0f, 160.0f, "%.0f");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Near Clip", &ps.camNearClip, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("Far Clip", &ps.camFarClip, 1.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(180.0f);
        bool orbitChanged = ImGui::SliderFloat("Orbit Distance", &ps.camOrbitDistance, 0.1f, 1000.0f, "%.2f");

        manager->applyPrefabSceneSettings();

        ImGui::Separator();
        bool resetClicked = false;
        if (ImGui::Button("Reset"))
        {
            ps.camFOV = 60.0f;
            ps.camNearClip = 0.1f;
            ps.camFarClip = 10000.0f;
            ps.camOrbitDistance = 9.0f;
            manager->applyPrefabSceneSettings();
            resetClicked = true;
        }

        // Reposition the preview camera immediately when the orbit distance
        // changes so the slider / Reset has a visible effect.
        if ((orbitChanged || resetClicked) && manager->prefabCamera)
        {
            kVec3 fwd = manager->prefabCamera->calculateForward();
            manager->prefabCamera->setPosition(manager->prefabOrbitPivot - fwd * manager->prefabOrbitDistance);
        }
        ImGui::EndPopup();
    }

    // Apply / Revert appear next to the settings buttons. They are enabled only
    // while the prefab has unsaved edits.
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12, 0));
    ImGui::SameLine();

    // Recompute the dirty flag at a throttled rate: serializing the whole
    // subtree every frame would be wasteful for large prefabs.
    static float s_dirtyAccum = 0.0f;
    s_dirtyAccum += ImGui::GetIO().DeltaTime;
    if (s_dirtyAccum >= 0.2f)
    {
        s_dirtyAccum = 0.0f;
        manager->prefabEditorModified = manager->computePrefabEditorModified();
    }

    gui->beginDisabled(!manager->prefabEditorModified);
    if (ImGui::Button("Apply"))
        manager->applyPrefabEditorChanges();
    if (gui->isItemHovered())
        gui->setItemTooltip("Save this prefab and reload its instances in the open world");
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        manager->revertPrefabEditorChanges();
    if (gui->isItemHovered())
        gui->setItemTooltip("Discard changes and reload the prefab from disk");
    gui->endDisabled();

    if (manager->prefabEditorModified)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* Modified");
    }

    gui->separator();

    // Viewport — the prefab editor uses its OWN kRenderer (separate from the
    // World panel's renderer), so it never touches the World panel's FBO.
    kVec2 availSize  = gui->getContentRegionAvail();
    width       = (int)availSize.x;
    height      = (int)availSize.y;
    aspectRatio = (height > 0.0f) ? (availSize.x / availSize.y) : 1.0f;

    panelPos        = gui->getCursorScreenPos();
    kVec2 panelSize = availSize;

    if (manager->prefabRenderer)
    {
        ImTextureRef tex_ref((ImTextureID)(uintptr_t)manager->prefabRenderer->getFboTexture());
        gui->setNextItemAllowOverlap();
        ImGui::Image(tex_ref, ImVec2(availSize.x, availSize.y), ImVec2(0, 1), ImVec2(1, 0));
    }

    hovered = gui->isWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Single-object gizmo on the prefab root (or whatever's selected in prefab panel).
    kObject *target = manager->prefabSelectedObject;
    if (target && target->getActive())
    {
        glm::mat4 view = manager->prefabCamera->getViewMatrix();
        glm::mat4 proj = manager->prefabCamera->getProjectionMatrix();

        glm::mat4 m = target->getModelMatrixWorld();
        glm::mat4 mCopy = m;

        // BeginFrame() is called once per frame by the main loop (see main.cpp);
        // calling it here would clobber other panels' gizmo-hover state.
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

        ImGuizmo::Manipulate(
            glm::value_ptr(view), glm::value_ptr(proj),
            manager->prefabManipulatorType, manager->prefabManipulatorMode,
            glm::value_ptr(mCopy));

        bool isUsingNow = ImGuizmo::IsUsing();
        if (!wasGizmoUsing && isUsingNow)
            gizmoStartStates = manager->captureSelectedTransforms();

        if (isUsingNow)
        {
            // ImGuizmo manipulates the object's WORLD matrix (mCopy). Convert the
            // result back into the object's LOCAL space before decomposing, so a
            // node that lives under a transformed parent (e.g. a child of a
            // prefab root) keeps the correct relative transform instead of
            // inheriting the parent's world offset. Writing world values as
            // local here corrupted the node's saved position/rotation/scale and
            // made prefabs reopen at the wrong place.
            glm::mat4 localMatrix = mCopy;
            if (target->getParent())
                localMatrix = glm::inverse(target->getParent()->getModelMatrixWorld()) * mCopy;

            glm::vec3 pos, scale, skew;
            glm::quat rot;
            glm::vec4 persp;
            glm::decompose(localMatrix, scale, rot, pos, skew, persp);
            // Force-setters so the editor gizmo can always manipulate nodes even
            // when they are marked static (matches the World panel gizmo).
            target->setPositionForced(pos);
            target->setRotationForced(glm::normalize(rot));
            target->setScaleForced(scale);
        }

        if (wasGizmoUsing && !isUsingNow)
        {
            auto after = manager->captureSelectedTransforms();
            manager->undoRedo.push(std::make_unique<TransformCommand>(
                manager, gizmoStartStates, std::move(after)));
        }

        wasGizmoUsing = isUsingNow;
    }

    // ------------------------------------------------------------------
    // Camera preview overlay — when the prefab selection is a kCamera (and
    // not the prefab editor camera itself), render its view through a
    // dedicated offscreen renderer and composite the result at the panel's
    // bottom-right. Mirrors the World panel's preview.
    // ------------------------------------------------------------------
    if (manager->prefabSelectedObject &&
        manager->prefabSelectedObject->getType() == NODE_TYPE_CAMERA &&
        manager->prefabSelectedObject != manager->prefabCamera &&
        manager->prefabSelectedObject->getActive())
    {
        kCamera *previewCam = static_cast<kCamera *>(manager->prefabSelectedObject);

        int targetW = std::max(96, std::min((int)(panelSize.x * 0.30f), 480));
        float aspect = previewCam->getAspectRatio();
        if (aspect <= 0.0f)
            aspect = 4.0f / 3.0f;
        int targetH = (int)((float)targetW / aspect);
        const int maxH = std::max(64, (int)(panelSize.y * 0.40f));
        if (targetH > maxH)
        {
            targetH = maxH;
            targetW = (int)((float)targetH * aspect);
        }

        if (targetW >= 64 && targetH >= 36 &&
            targetW < (int)panelSize.x && targetH < (int)panelSize.y)
        {
            // The prefab world's GPU resources live in the prefab driver's
            // context, so make it current while creating/rendering the preview.
            kDriver *savedDriver = kDriver::getCurrent();
            bool switched = false;
            if (manager->prefabRenderer && manager->prefabRenderer->getDriver())
            {
                manager->prefabRenderer->getDriver()->makeCurrent(manager->getWindow());
                kDriver::setCurrent(manager->prefabRenderer->getDriver());
                switched = true;
            }

            // Recreate the offscreen renderer if the prefab asset manager was
            // swapped (the prefab editor was closed and reopened).
            if (cameraPreview && cameraPreviewAsset != manager->prefabAssetManager)
            {
                delete cameraPreview;
                cameraPreview = nullptr;
                cameraPreviewAsset = nullptr;
            }
            if (!cameraPreview)
            {
                cameraPreview = new kOffscreenRenderer(targetW, targetH);
                cameraPreviewAsset = manager->prefabAssetManager;
            }
            cameraPreview->setAssetManager(manager->prefabAssetManager);
            if (cameraPreview->getWidth() != targetW ||
                cameraPreview->getHeight() != targetH)
                cameraPreview->resize(targetW, targetH);

            // Render at the preview rect's aspect, then restore the camera's.
            float savedAspect = previewCam->getAspectRatio();
            previewCam->setAspectRatio((float)targetW / (float)targetH);
            cameraPreview->render(manager->prefabWorld, manager->prefabScene, previewCam);
            previewCam->setAspectRatio(savedAspect);

            if (switched && savedDriver)
            {
                savedDriver->makeCurrent(manager->getWindow());
                kDriver::setCurrent(savedDriver);
            }

            const float margin = 12.0f;
            ImVec2 a((float)panelPos.x + (float)panelSize.x - (float)targetW - margin,
                     (float)panelPos.y + (float)panelSize.y - (float)targetH - margin);
            ImVec2 b(a.x + (float)targetW, a.y + (float)targetH);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImTextureRef previewTex((ImTextureID)(uintptr_t)cameraPreview->getTexture());
            // Same vertical flip as the main image — kOffscreenRenderer writes
            // top-down GL convention.
            dl->AddImage(previewTex, a, b, ImVec2(0, 1), ImVec2(1, 0));
            dl->AddRect(a, b, IM_COL32(220, 220, 230, 200), 2.0f, 0, 1.5f);

            // Label strip across the top so the user knows which camera.
            std::string label = previewCam->getName();
            if (label.empty())
                label = "Camera";
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            ImVec2 labelMin(a.x, a.y - ts.y - 4.0f);
            ImVec2 labelMax(a.x + ts.x + 8.0f, a.y);
            dl->AddRectFilled(labelMin, labelMax, IM_COL32(0, 0, 0, 160));
            dl->AddText(ImVec2(a.x + 4.0f, a.y - ts.y - 2.0f),
                        IM_COL32(235, 235, 240, 230), label.c_str());
        }
    }

    gui->windowEnd();
    gui->endDisabled();
}
