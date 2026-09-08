#include "panel_world.h"

PanelWorld::PanelWorld(kGuiManager *setGuiManager, Manager *setManager)
{
    gui = setGuiManager;
    manager = setManager;

    // Disable the hatched/dashed overlay on gizmo axis lines
    ImGuizmo::GetStyle().HatchedAxisLineThickness = 0.0f;
}

// ---------------------------------------------------------------------------
// Pivot toolbar helpers
// ---------------------------------------------------------------------------

static void pivotButton(kGuiManager *gui, const char *label, PivotMode mode, PivotMode &current)
{
    bool active = (current == mode);
    if (active)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->button(label))
        current = mode;
    if (active)
        gui->popStyleColor(2);
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void PanelWorld::draw(bool &isOpened, kRenderer *renderer, kCamera *editorCamera)
{
    enabled = manager->projectOpened;

    if (!isOpened || renderer == nullptr || editorCamera == nullptr)
        return;

    gui->beginDisabled(!enabled);
    gui->windowStart("World", &isOpened);

    pivotButton(gui, "I", PivotMode::Individual, manager->pivotMode);
    if (gui->isItemHovered())
        gui->setItemTooltip("Individual pivot");
    gui->sameLine();
    pivotButton(gui, "C", PivotMode::Center, manager->pivotMode);
    if (gui->isItemHovered())
        gui->setItemTooltip("Center pivot");
    gui->sameLine();
    pivotButton(gui, "L", PivotMode::LastSelected, manager->pivotMode);
    if (gui->isItemHovered())
        gui->setItemTooltip("Last selected pivot");

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Render mode selector
    static const char *kRenderModeNames[] = {
        "Full", "Albedo", "Normals", "Wireframe", "Depth", "Object IDs", "Full+Wire"};
    int currentMode = (int)renderer->getRenderMode();
    gui->setNextItemWidth(110.0f);
    if (ImGui::Combo("##RenderMode", &currentMode, kRenderModeNames, 7))
        renderer->setRenderMode((kRenderMode)currentMode);

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Octree debug toggle
    {
        bool octreeDebug = renderer->getOctreeDebugEnabled();
        if (gui->checkbox("Octree", &octreeDebug))
            renderer->setOctreeDebugEnabled(octreeDebug);
        if (gui->isItemHovered())
            gui->setItemTooltip("Show octree node bounds");
    }

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Gizmo space: Local (object orientation) vs World (axis-aligned).
    {
        auto modeBtn = [&](const char *label, ImGuizmo::MODE m)
        {
            bool active = (manager->manipulatorMode == m);
            if (active)
            {
                gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
                gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
            }
            if (gui->button(label))
                manager->manipulatorMode = m;
            if (active)
                gui->popStyleColor(2);
        };
        modeBtn("Local", ImGuizmo::LOCAL);
        if (gui->isItemHovered())
            gui->setItemTooltip("Gizmo aligned to the object's local orientation");
        gui->sameLine();
        modeBtn("World", ImGuizmo::WORLD);
        if (gui->isItemHovered())
            gui->setItemTooltip("Gizmo aligned to world axes");
    }

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Camera settings popup
    if (gui->button("Camera..."))
        ImGui::OpenPopup("CameraSettings");
    if (gui->isItemHovered())
        gui->setItemTooltip("Adjust editor camera settings");

    if (ImGui::BeginPopup("CameraSettings"))
    {
        ImGui::Text("Editor Camera Settings");
        ImGui::Separator();

        float fov = editorCamera->getFOV();
        if (ImGui::SliderFloat("FOV", &fov, 20.0f, 160.0f, "%.0f"))
            editorCamera->setFOV(fov);

        float nearClip = editorCamera->getNearClip();
        if (ImGui::SliderFloat("Near Clip", &nearClip, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
            editorCamera->setNearClip(nearClip);

        float farClip = editorCamera->getFarClip();
        if (ImGui::SliderFloat("Far Clip", &farClip, 1.0f, 100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
            editorCamera->setFarClip(farClip);

        float orbitDist = manager->editorCamOrbitDistance;
        if (ImGui::SliderFloat("Orbit Distance", &orbitDist, 1.0f, 1000.0f, "%.1f"))
            manager->editorCamOrbitDistance = orbitDist;

        ImGui::Separator();

        if (ImGui::Button("Reset to Defaults"))
        {
            editorCamera->setFOV(60.0f);
            editorCamera->setNearClip(0.1f);
            editorCamera->setFarClip(10000.0f);
            manager->editorCamOrbitDistance = 10.0f;
        }

        ImGui::EndPopup();
    }

    // Preview mode indicator (particle / animator only — prefab has its own panel)
    if (manager->activeMode != Manager::EditorMode::GameWorld &&
        manager->activeMode != Manager::EditorMode::PrefabPreview)
    {
        const char *modeLabel = "Preview";
        switch (manager->activeMode)
        {
        case Manager::EditorMode::ParticlePreview: modeLabel = "Particle Preview"; break;
        case Manager::EditorMode::AnimatorPreview: modeLabel = "Animator Preview"; break;
        default: break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::TextUnformatted(modeLabel);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("— click a scene object to return to game world");
        gui->separator();
    }

    kVec2 availSize = gui->getContentRegionAvail();
    width = (int)availSize.x;
    height = (int)availSize.y;
    aspectRatio = (height > 0.0f) ? (availSize.x / availSize.y) : 1.0f;

    panelPos = gui->getCursorScreenPos();
    kVec2 panelSize = availSize;

    // Display framebuffer texture
    ImTextureRef tex_ref((ImTextureID)(uintptr_t)renderer->getFboTexture());
    gui->setNextItemAllowOverlap();
    ImGui::Image(tex_ref, ImVec2(availSize.x, availSize.y), ImVec2(0, 1), ImVec2(1, 0));

    // ----- Drop target -------------------------------------------------------
    // Project assets dragged onto the viewport: meshes/prefabs/audio spawn on
    // release; materials live-preview on hover and commit on release.
    // Regardless of payload type, we record the picked object in
    // manager->dragHoverObjectUuid so the main render loop can paint an
    // outline showing the user which object is under their cursor.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
                "PROJECT_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery))
        {
            kString assetUuid((const char *)payload->Data);
            // The project panel may pack several newline-separated UUIDs when
            // multiple files are dragged; this target acts on the first one.
            {
                auto nl = assetUuid.find('\n');
                if (nl != kString::npos)
                    assetUuid = assetUuid.substr(0, nl);
            }
            auto it = manager->fileMap.find(assetUuid);
            if (it != manager->fileMap.end())
            {
                const auto &info = it->second;

                // Pick the object under the mouse for the drag-hover outline.
                // Material drops also use this for live preview targeting.
                kVec2 imMouse = gui->getMousePos();
                int vpMouseX = (int)((imMouse.x - panelPos.x) * 2.0f);
                int vpMouseY = (int)((imMouse.y - panelPos.y) * 2.0f);
                // The exact object (sub-mesh) under the cursor, by Object ID.
                // Material drops apply to THIS node directly — not the whole
                // model — so each sub-mesh of a multi-mesh import can have its
                // own material.
                kObject *hovObj = renderer->pickObject(
                    manager->getWorld(), manager->getScene(),
                    vpMouseX, vpMouseY, width * 2, height * 2);
                manager->dragHoverObjectUuid = hovObj ? hovObj->getUuid() : kString("");

                if (info.type == "material")
                {
                    fs::path matPath = manager->projectPath / "Assets" / info.path;

                    // If the hovered object changed since last frame, restore
                    // the prior preview's whole subtree before applying to the
                    // new one.
                    if (hovObj != manager->matPreviewObject)
                    {
                        if (manager->matPreviewObject)
                            manager->restoreMaterialSubtree(manager->matPreviewSnapshot);

                        manager->matPreviewObject = hovObj;
                        manager->matPreviewSnapshot = hovObj
                                                          ? manager->captureMaterialSubtree(hovObj)
                                                          : std::vector<MaterialSnapshot>();
                        manager->matPreviewSourceUuid = assetUuid;

                        if (hovObj)
                            manager->applyMaterialToObject(hovObj, matPath, assetUuid);
                    }

                    // Commit on release: leave the new material in place, push a
                    // subtree-wide undo, then forget the preview so the post-
                    // target restore below won't revert it.
                    if (payload->IsDelivery() && manager->matPreviewObject)
                    {
                        auto cmd = std::make_unique<MaterialCommand>();
                        cmd->manager = manager;
                        cmd->before = manager->matPreviewSnapshot;
                        cmd->after = manager->captureMaterialSubtree(manager->matPreviewObject);
                        manager->undoRedo.push(std::move(cmd));

                        manager->matPreviewObject = nullptr;
                        manager->matPreviewSnapshot.clear();
                        manager->matPreviewSourceUuid = "";
                    }
                }
                else if (payload->IsDelivery())
                {
                    // Spawn at a point in front of the editor camera so the new
                    // object appears in view rather than at the world origin.
                    kVec3 spawn = editorCamera->getPosition() + editorCamera->calculateForward() * 5.0f;
                    manager->instantiateAssetFromUuid(assetUuid, spawn);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // If a material preview is dangling (drag left the viewport or was
    // cancelled), restore the original material so the scene isn't left with
    // a half-applied preview.
    if (!ImGui::IsDragDropActive() && manager->matPreviewObject)
    {
        manager->restoreMaterialSubtree(manager->matPreviewSnapshot);
        manager->matPreviewObject = nullptr;
        manager->matPreviewSnapshot.clear();
        manager->matPreviewSourceUuid = "";
    }

    // Clear the drag-hover highlight when no drag is in progress.
    if (!ImGui::IsDragDropActive() && !manager->dragHoverObjectUuid.empty())
        manager->dragHoverObjectUuid.clear();

    hovered = gui->isWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // --- Preview camera orbit (LMB drag in preview mode, excl. PrefabPreview) ---
    if (manager->activeMode != Manager::EditorMode::GameWorld &&
        manager->activeMode != Manager::EditorMode::PrefabPreview &&
        manager->previewCamera && hovered)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
        {
            if (!manager->previewCamDragging)
            {
                manager->previewCamDragging = true;
            }
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            manager->previewCamYaw -= delta.x * 0.3f;
            manager->previewCamPitch += delta.y * 0.3f;
            manager->previewCamPitch = std::clamp(manager->previewCamPitch, -89.0f, 89.0f);

            // Apply orbit
            float pr = glm::radians(manager->previewCamPitch);
            float yr = glm::radians(manager->previewCamYaw);
            kVec3 camDir(std::cos(pr) * std::sin(yr),
                         std::sin(pr),
                         std::cos(pr) * std::cos(yr));
            manager->previewCamera->setPosition(
                manager->previewCamPivot + camDir * manager->previewCamDist);
            manager->previewCamera->setLookAt(manager->previewCamPivot);
        }
        else
        {
            manager->previewCamDragging = false;
        }

        // Scroll wheel zoom
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f && hovered)
        {
            manager->previewCamDist -= wheel * 0.5f;
            manager->previewCamDist = std::max(0.5f, std::min(manager->previewCamDist, 500.0f));

            float pr = glm::radians(manager->previewCamPitch);
            float yr = glm::radians(manager->previewCamYaw);
            kVec3 camDir(std::cos(pr) * std::sin(yr),
                         std::sin(pr),
                         std::cos(pr) * std::cos(yr));
            manager->previewCamera->setPosition(
                manager->previewCamPivot + camDir * manager->previewCamDist);
            manager->previewCamera->setLookAt(manager->previewCamPivot);
        }
    }

    // Track whether the cursor is over terrain for any active tool mode
    bool cursorOverTerrain = false;

    // --- Terrain sculpt / paint (GameWorld only) ---
    bool isPreviewMode = (manager->activeMode != Manager::EditorMode::GameWorld);
    if (manager->panelTerrain && !isPreviewMode)
    {
        bool isSculpting = manager->panelTerrain->sculpt.active;
        bool isPainting = manager->panelTerrain->paint.active;
        bool isHeightPaint = manager->panelTerrain->paint.heightActive;
        bool inToolMode = isSculpting || isPainting || isHeightPaint;

        // Reset cursor validity every frame — only set to true below when
        // the raycast actually hits terrain. This prevents the cursor from
        // lingering at its last position when the mouse leaves the viewport
        // or hovers over empty space / a non-terrain object.
        manager->panelTerrain->paintCursorValid = false;

        // Block painting when modifier keys are held (Alt/Ctrl/Shift)
        // so the user can orbit/pan/zoom without painting the terrain.
        bool modifiersDown = ImGui::GetIO().KeyAlt || ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;

        // When in sculpt or paint mode, block object picking so the user
        // cannot select 3D objects by clicking in the viewport.
        // (The hierarchy panel bypasses this — it selects by tree click.)
        // DIAGNOSTIC: print whether conditions are met
        if (isSculpting) {
            std::cerr << "DIAG: inToolMode=" << inToolMode << " hovered=" << hovered
                      << " gizmoUsing=" << ImGuizmo::IsUsing()
                      << " modsDown=" << modifiersDown
                      << " scene=" << (manager->getScene() ? "ok" : "null")
                      << " tmgr=" << (manager->terrainManager ? "ok" : "null")
                      << "\n";
        }

        if (inToolMode && (hovered || focused) && !ImGuizmo::IsUsing() && !modifiersDown)
        {
            kVec2 imMouse = gui->getMousePos();

            bool doSculpt = isSculpting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            // Mask paint: left=add, right=erase
            bool doPaintLower = isPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool doPaintRaise = isPainting && ImGui::IsMouseDown(ImGuiMouseButton_Right);
            // Height paint: left=lower, right=raise
            bool doHeightLower = isHeightPaint && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool doHeightRaise = isHeightPaint && ImGui::IsMouseDown(ImGuiMouseButton_Right);

            if (doSculpt || doPaintLower || doPaintRaise || doHeightLower || doHeightRaise)
            {
                kVec3 origin, dir;
                editorCamera->screenToRay(
                    imMouse.x - panelPos.x,
                    imMouse.y - panelPos.y,
                    panelSize.x, panelSize.y,
                    origin, dir);

                kTerrainManager *tmgr = manager->terrainManager;
                if (tmgr)
                {
                    float dirXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    if (dirXZ > 0.0001f)
                    {
                        const float tileSize = 256.0f;
                        const int heightRes = 513;
                        const float sampleSpacing = tileSize / static_cast<float>(heightRes - 1);
                        const float stepSize = sampleSpacing * 0.5f;
                        const float maxDist = 10000.0f;

                        float s = stepSize / dirXZ;
                        float rayT = 0.0f;
                        bool hitTerrain = false;
                        kVec3 hitPos(0.0f);
                        kVec3 hitNormal(0.0f, 1.0f, 0.0f);

                        float prevTerrainY = -1e30f;
                        {
                            int gx, gz;
                            kTerrainManager::worldToGrid(origin, tileSize, gx, gz);
                            kTerrain *startTile = tmgr->getTile(gx, gz);
                            if (startTile)
                                prevTerrainY = startTile->sampleHeight(origin);
                        }

                        for (int step = 0; step < 20000 && rayT < maxDist; ++step)
                        {
                            rayT += s;
                            kVec3 p = origin + dir * rayT;
                            float rayY = p.y;

                            int gx, gz;
                            kTerrainManager::worldToGrid(p, tileSize, gx, gz);
                            kTerrain *tile = tmgr->getTile(gx, gz);

                            float terrainY = 0.0f;
                            if (tile)
                                terrainY = tile->sampleHeight(p);

                            if (prevTerrainY > -1e29f && rayY <= terrainY)
                            {
                                float prevRayY = rayY - dir.y * s;
                                float tCross = (prevTerrainY - prevRayY) /
                                               ((terrainY - rayY) - (prevTerrainY - prevRayY));
                                tCross = glm::clamp(tCross, 0.0f, 1.0f);
                                hitPos = (origin + dir * (rayT - s)) + dir * (s * tCross);

                                // Compute approximate surface normal at hit
                                if (tile)
                                {
                                    int hx, hz;
                                    if (tile->worldToHeightmap(hitPos, hx, hz))
                                    {
                                        int res = tile->getHeightRes();
                                        float *data = tile->getHeightData();
                                        float sp = tile->getWorldSize() / static_cast<float>(res - 1);
                                        float hL = (hx > 0) ? data[hz * res + (hx - 1)] : data[hz * res + hx];
                                        float hR = (hx < res - 1) ? data[hz * res + (hx + 1)] : data[hz * res + hx];
                                        float hD = (hz > 0) ? data[(hz - 1) * res + hx] : data[hz * res + hx];
                                        float hU = (hz < res - 1) ? data[(hz + 1) * res + hx] : data[hz * res + hx];
                                        hitNormal = glm::normalize(kVec3(
                                            -(hR - hL) / (sp * 2.0f), 1.0f, -(hU - hD) / (sp * 2.0f)));
                                    }
                                }

                                hitTerrain = true;
                                break;
                            }

                            prevTerrainY = terrainY;
                        }

                        if (hitTerrain)
                        {
                            if (doSculpt)
                                manager->panelTerrain->sculptAtWorldPos(hitPos);
                            else if (doPaintLower || doPaintRaise)
                                manager->panelTerrain->paintSplatAtWorldPos(hitPos, doPaintLower);
                            else if (doHeightLower || doHeightRaise)
                                manager->panelTerrain->paintAtWorldPos(hitPos, doHeightLower);

                            // Real-time GPU update: fast VBO sub-update during painting
                            {
                                int gx, gz;
                                kTerrainManager::worldToGrid(hitPos, 256.0f, gx, gz);
                                kTerrain *foundTile = tmgr->getTile(gx, gz);
                                if (foundTile)
                                {
                                    if (doSculpt || doHeightLower || doHeightRaise)
                                        foundTile->updateMesh();
                                    else if (doPaintLower || doPaintRaise)
                                        foundTile->updateSplatTexture();
                                }
                            }
                        }

                        // Remember hit for paint cursor
                        manager->panelTerrain->paintCursorPos = hitPos;
                        manager->panelTerrain->paintCursorNormal = hitNormal;
                        manager->panelTerrain->paintCursorValid = hitTerrain;
                        cursorOverTerrain = hitTerrain;
                    }
                }
            }
            else
            {
                // On mouse-up, rebuild any edited tile
                if (isSculpting && manager->panelTerrain->sculpt.needsRebuild)
                    manager->panelTerrain->rebuildSculptedTile();
                if (isPainting && manager->panelTerrain->paint.needsRebuild)
                    manager->panelTerrain->rebuildPaintedSplatTile();
                else if (isHeightPaint && manager->panelTerrain->paint.needsRebuild)
                    manager->panelTerrain->rebuildPaintedTile();

                // Update cursor position even when not clicking (for paint cursor display)
                if (isSculpting || isPainting || isHeightPaint)
                {
                    kVec3 origin, dir;
                    editorCamera->screenToRay(
                        imMouse.x - panelPos.x,
                        imMouse.y - panelPos.y,
                        panelSize.x, panelSize.y,
                        origin, dir);

                    kTerrainManager *tmgr = manager->terrainManager;
                    if (tmgr)
                    {
                        float dirXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                        if (dirXZ > 0.0001f)
                        {
                            const float tileSize = 256.0f;
                            const int heightRes = 513;
                            const float sampleSpacing = tileSize / static_cast<float>(heightRes - 1);
                            const float stepSize = sampleSpacing * 0.5f;
                            const float maxDist = 10000.0f;

                            float s = stepSize / dirXZ;
                            float rayT = 0.0f;
                            kVec3 hitPos(0.0f);
                            kVec3 hitNormal(0.0f, 1.0f, 0.0f);
                            bool hitTerrain = false;

                            float prevTerrainY = -1e30f;
                            {
                                int gx, gz;
                                kTerrainManager::worldToGrid(origin, tileSize, gx, gz);
                                kTerrain *startTile = tmgr->getTile(gx, gz);
                                if (startTile)
                                    prevTerrainY = startTile->sampleHeight(origin);
                            }

                            for (int step = 0; step < 20000 && rayT < maxDist; ++step)
                            {
                                rayT += s;
                                kVec3 p = origin + dir * rayT;
                                float rayY = p.y;

                                int gx, gz;
                                kTerrainManager::worldToGrid(p, tileSize, gx, gz);
                                kTerrain *tile = tmgr->getTile(gx, gz);

                                float terrainY = 0.0f;
                                if (tile)
                                    terrainY = tile->sampleHeight(p);

                                if (prevTerrainY > -1e29f && rayY <= terrainY)
                                {
                                    float prevRayY = rayY - dir.y * s;
                                    float tCross = (prevTerrainY - prevRayY) /
                                                   ((terrainY - rayY) - (prevTerrainY - prevRayY));
                                    tCross = glm::clamp(tCross, 0.0f, 1.0f);
                                    hitPos = (origin + dir * (rayT - s)) + dir * (s * tCross);

                                    if (tile)
                                    {
                                        int hx, hz;
                                        if (tile->worldToHeightmap(hitPos, hx, hz))
                                        {
                                            int res = tile->getHeightRes();
                                            float *data = tile->getHeightData();
                                            float sp = tile->getWorldSize() / static_cast<float>(res - 1);
                                            float hL = (hx > 0) ? data[hz * res + (hx - 1)] : data[hz * res + hx];
                                            float hR = (hx < res - 1) ? data[hz * res + (hx + 1)] : data[hz * res + hx];
                                            float hD = (hz > 0) ? data[(hz - 1) * res + hx] : data[hz * res + hx];
                                            float hU = (hz < res - 1) ? data[(hz + 1) * res + hx] : data[hz * res + hx];
                                            hitNormal = glm::normalize(kVec3(
                                                -(hR - hL) / (sp * 2.0f), 1.0f, -(hU - hD) / (sp * 2.0f)));
                                        }
                                    }

                                    hitTerrain = true;
                                    break;
                                }

                                prevTerrainY = terrainY;
                            }

                            manager->panelTerrain->paintCursorPos = hitPos;
                            manager->panelTerrain->paintCursorNormal = hitNormal;
                            manager->panelTerrain->paintCursorValid = hitTerrain;
                            cursorOverTerrain = hitTerrain;
                        }
                        else
                        {
                            manager->panelTerrain->paintCursorValid = false;
                            cursorOverTerrain = false;
                        }
                    }
                    else
                    {
                        manager->panelTerrain->paintCursorValid = false;
                        cursorOverTerrain = false;
                    }
                }
            }
        }

        // --- Render paint cursor in the viewport (also shown for sculpt) ---
        // Only show the cursor when the mouse is actually over the terrain
        if ((isSculpting || isPainting || isHeightPaint) && manager->panelTerrain->paintCursorValid)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            // Project the world-space hit position to screen
            glm::mat4 view = editorCamera->getViewMatrix();
            glm::mat4 proj = editorCamera->getProjectionMatrix();
            glm::mat4 vp = proj * view;

            kVec3 wp = manager->panelTerrain->paintCursorPos;
            glm::vec4 clip = vp * glm::vec4(wp.x, wp.y, wp.z, 1.0f);

            if (clip.w > 0.0001f)
            {
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                // Convert NDC [-1,1] to screen pixel coords
                float sx = panelPos.x + (ndc.x * 0.5f + 0.5f) * panelSize.x;
                float sy = panelPos.y + (0.5f - ndc.y * 0.5f) * panelSize.y;

                // Scale circle by brush radius projected to screen
                float brushRadius = manager->panelTerrain->sculpt.radius;
                kVec3 offsetX = wp + kVec3(brushRadius, 0.0f, 0.0f);
                glm::vec4 clipX = vp * glm::vec4(offsetX.x, offsetX.y, offsetX.z, 1.0f);
                float screenRadius = 5.0f;
                if (clipX.w > 0.0001f)
                {
                    glm::vec3 ndcX = glm::vec3(clipX) / clipX.w;
                    float sx2 = panelPos.x + (ndcX.x * 0.5f + 0.5f) * panelSize.x;
                    screenRadius = std::abs(sx2 - sx);
                }
                screenRadius = (std::min)(screenRadius, 200.0f);
                screenRadius = (std::max)(screenRadius, 10.0f);

                // Channel color (green for height paint, R/G/B/A for mask paint)
                ImU32 circleColor = IM_COL32(255, 255, 255, 180);
                if (isHeightPaint)
                {
                    circleColor = IM_COL32(112, 171, 100, 200); // green for height paint
                }
                else
                {
                    switch (manager->panelTerrain->paint.channel)
                    {
                    case 0:
                        circleColor = IM_COL32(220, 60, 60, 180);
                        break; // R
                    case 1:
                        circleColor = IM_COL32(60, 220, 60, 180);
                        break; // G
                    case 2:
                        circleColor = IM_COL32(60, 60, 220, 180);
                        break; // B
                    case 3:
                        circleColor = IM_COL32(180, 180, 180, 180);
                        break; // A
                    }
                }

                // Draw circle projected in 3D, facing the terrain normal
                kVec3 n = manager->panelTerrain->paintCursorNormal;

                // Build two perpendicular tangent vectors to the normal
                kVec3 t1, t2;
                if (std::abs(n.y) < 0.999f)
                    t1 = glm::normalize(glm::cross(n, kVec3(0.0f, 1.0f, 0.0f)));
                else
                    t1 = glm::normalize(glm::cross(n, kVec3(1.0f, 0.0f, 0.0f)));
                t2 = glm::normalize(glm::cross(n, t1));

                const int circleSegs = 48;
                std::vector<ImVec2> circlePts;
                circlePts.reserve(circleSegs);
                float r = brushRadius;

                for (int seg = 0; seg < circleSegs; ++seg)
                {
                    float angle = (float)seg / (float)circleSegs * 6.283185307f;
                    kVec3 pt = wp + t1 * std::cos(angle) * r + t2 * std::sin(angle) * r;
                    glm::vec4 cp = vp * glm::vec4(pt.x, pt.y, pt.z, 1.0f);
                    if (cp.w > 0.0001f)
                    {
                        glm::vec3 nd = glm::vec3(cp) / cp.w;
                        float px = panelPos.x + (nd.x * 0.5f + 0.5f) * panelSize.x;
                        float py = panelPos.y + (0.5f - nd.y * 0.5f) * panelSize.y;
                        circlePts.push_back(ImVec2(px, py));
                    }
                }

                // Draw the circle as a closed polyline
                if (circlePts.size() >= 3)
                {
                    circlePts.push_back(circlePts[0]); // close the loop
                    dl->AddPolyline(circlePts.data(), (int)circlePts.size(), circleColor,
                                    ImDrawFlags_Closed, 2.5f);
                }

                // Draw an up indicator line along the normal
                kVec3 upEnd = wp + n * r * 0.5f;
                glm::vec4 clipUp = vp * glm::vec4(upEnd.x, upEnd.y, upEnd.z, 1.0f);
                if (clipUp.w > 0.0001f)
                {
                    glm::vec3 ndcUp = glm::vec3(clipUp) / clipUp.w;
                    float ux = panelPos.x + (ndcUp.x * 0.5f + 0.5f) * panelSize.x;
                    float uy = panelPos.y + (0.5f - ndcUp.y * 0.5f) * panelSize.y;

                    // Thick end dot
                    dl->AddCircleFilled(ImVec2(sx, sy), 3.5f, circleColor);
                    dl->AddLine(ImVec2(sx, sy), ImVec2(ux, uy), circleColor, 2.5f);
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Multi-object gizmo
    // ------------------------------------------------------------------
    if (!manager->selectedObjects.empty())
    {
        // Gather valid selected object pointers
        std::vector<kObject *> selObjs;
        for (const auto &uuid : manager->selectedObjects)
        {
            kObject *obj = manager->findObjectByUuid(uuid);
            if (obj && obj->getActive())
                selObjs.push_back(obj);
        }

        if (!selObjs.empty())
        {
            glm::mat4 view = editorCamera->getViewMatrix();
            glm::mat4 proj = editorCamera->getProjectionMatrix();

            // Compute pivot matrix
            glm::mat4 pivotMatrix;
            if (manager->pivotMode == PivotMode::Center)
            {
                glm::vec3 center(0.0f);
                for (kObject *obj : selObjs)
                    center += obj->getPosition();
                center /= (float)selObjs.size();
                pivotMatrix = glm::translate(glm::mat4(1.0f), center);
            }
            else
            {
                // LastSelected or Individual → pivot at last selected
                kObject *pivot = manager->selectedObject ? manager->selectedObject : selObjs.back();
                pivotMatrix = pivot->getModelMatrixWorld();
            }

            glm::mat4 pivotCopy = pivotMatrix;

            ImGuizmo::BeginFrame();
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

            ImGuizmo::Manipulate(
                glm::value_ptr(view), glm::value_ptr(proj),
                manager->manipulatorType, manager->manipulatorMode,
                glm::value_ptr(pivotCopy));

            bool isUsingNow = ImGuizmo::IsUsing();

            // Snapshot start state when drag begins
            if (!wasGizmoUsing && isUsingNow)
                gizmoStartStates = manager->captureSelectedTransforms();

            if (isUsingNow)
            {
                glm::mat4 delta = pivotCopy * glm::inverse(pivotMatrix);

                for (kObject *obj : selObjs)
                {
                    // Use force-setters so the editor gizmo can always manipulate
                    // objects, even when marked as static. The static guard only
                    // blocks runtime (gameplay) modifications.
                    if (selObjs.size() == 1)
                    {
                        // pivotMatrix started as this object's world matrix,
                        // so pivotCopy IS the new world matrix — use it directly.
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(pivotCopy, scale, rot, pos, skew, persp);
                        obj->setPositionForced(pos);
                        obj->setRotationForced(glm::normalize(rot));
                        obj->setScaleForced(scale);
                    }
                    else if (manager->pivotMode == PivotMode::Individual)
                    {
                        // Each object around its own centre — extract pure deltas
                        // from the pivot matrices to avoid the T*R*T^-1 drift.
                        glm::vec3 dPos = glm::vec3(pivotCopy[3]) - glm::vec3(pivotMatrix[3]);

                        glm::vec3 sOld(glm::length(glm::vec3(pivotMatrix[0])),
                                       glm::length(glm::vec3(pivotMatrix[1])),
                                       glm::length(glm::vec3(pivotMatrix[2])));
                        glm::vec3 sNew(glm::length(glm::vec3(pivotCopy[0])),
                                       glm::length(glm::vec3(pivotCopy[1])),
                                       glm::length(glm::vec3(pivotCopy[2])));

                        glm::mat3 rOld(glm::vec3(pivotMatrix[0]) / sOld.x,
                                       glm::vec3(pivotMatrix[1]) / sOld.y,
                                       glm::vec3(pivotMatrix[2]) / sOld.z);
                        glm::mat3 rNew(glm::vec3(pivotCopy[0]) / sNew.x,
                                       glm::vec3(pivotCopy[1]) / sNew.y,
                                       glm::vec3(pivotCopy[2]) / sNew.z);
                        glm::quat dRot = glm::normalize(glm::quat_cast(rNew * glm::transpose(rOld)));

                        obj->setPositionForced(obj->getPosition() + dPos);
                        obj->setRotationForced(glm::normalize(dRot * obj->getRotation()));
                        obj->setScaleForced(obj->getScale() * (sNew / sOld));
                    }
                    else
                    {
                        // Center / LastSelected: apply full delta to world matrix
                        glm::mat4 newWorld = delta * obj->getModelMatrixWorld();
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(newWorld, scale, rot, pos, skew, persp);
                        obj->setPositionForced(pos);
                        obj->setRotationForced(glm::normalize(rot));
                        obj->setScaleForced(scale);
                    }
                }
            }

            // Push undo command when drag ends + mark the world dirty so
            // the window title gets its asterisk and Ctrl+S has something
            // to save.
            if (wasGizmoUsing && !isUsingNow)
            {
                auto after = manager->captureSelectedTransforms();
                manager->undoRedo.push(std::make_unique<TransformCommand>(
                    manager, gizmoStartStates, std::move(after)));
                manager->projectSaved = false;
                manager->refreshWindowTitle();
            }

            wasGizmoUsing = isUsingNow;
        }
    }

    // ------------------------------------------------------------------
    // Camera preview overlay — when the primary selection is a kCamera
    // (and not the editor camera), render its view via a dedicated
    // kOffscreenRenderer and composite the result at the bottom-right.
    // ------------------------------------------------------------------
    if (manager->selectedObject &&
        manager->selectedObject->getType() == NODE_TYPE_CAMERA &&
        manager->selectedObject != editorCamera)
    {
        kCamera *previewCam = static_cast<kCamera *>(manager->selectedObject);

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
            if (!cameraPreview)
            {
                cameraPreview = new kOffscreenRenderer(targetW, targetH);
                cameraPreview->setAssetManager(manager->getAssetManager());
            }
            if (cameraPreview->getWidth() != targetW ||
                cameraPreview->getHeight() != targetH)
                cameraPreview->resize(targetW, targetH);

            // Render at the preview rect's aspect, not whatever was left on
            // the camera by the game viewport (avoids stretched output).
            float savedAspect = previewCam->getAspectRatio();
            previewCam->setAspectRatio((float)targetW / (float)targetH);
            cameraPreview->render(manager->getWorld(), manager->getScene(), previewCam);
            previewCam->setAspectRatio(savedAspect);

            const float margin = 12.0f;
            ImVec2 a((float)panelPos.x + (float)panelSize.x - (float)targetW - margin,
                     (float)panelPos.y + (float)panelSize.y - (float)targetH - margin);
            ImVec2 b(a.x + (float)targetW, a.y + (float)targetH);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImTextureRef previewTex((ImTextureID)(uintptr_t)cameraPreview->getTexture());
            // Same vertical flip as the main world image — kOffscreenRenderer
            // writes top-down GL convention.
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