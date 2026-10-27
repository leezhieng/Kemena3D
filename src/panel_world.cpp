#include "panel_world.h"

#include <algorithm>

PanelWorld::PanelWorld(kGuiManager *setGuiManager, Manager *setManager)
{
    gui = setGuiManager;
    manager = setManager;

    // Disable the hatched/dashed overlay on gizmo axis lines
    ImGuizmo::GetStyle().HatchedAxisLineThickness = 0.0f;

    // Load the viewport toolbar icons (white glyphs on transparent PNGs) from
    // the embedded resources. Textures are cached by the asset manager, so the
    // texture IDs stay valid for the lifetime of the app.
    kAssetManager *am = manager->getAssetManager();
    if (am)
    {
        kTexture2D *tex = am->loadTexture2DFromResource("ICON_PIVOT_INDIVIDUAL_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconPivotIndividual = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_PIVOT_MEDIAN_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconPivotCenter = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_PIVOT_ACTIVE_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconPivotLastSelected = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_ALIGN_LOCAL_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconAlignLocal = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_ALIGN_WORLD_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconAlignWorld = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_CAMERA_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconCamera = tex->getTextureID();

        tex = am->loadTexture2DFromResource("ICON_SCENE_LABEL", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (tex)
            iconPreview = tex->getTextureID();
    }
}

// ---------------------------------------------------------------------------
// Pivot toolbar helpers
// ---------------------------------------------------------------------------

static void pivotButton(kGuiManager *gui, const char *id, uint32_t icon, PivotMode mode, PivotMode &current)
{
    bool active = (current == mode);
    if (active)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->imageButton(id, icon, kVec2(24.0f, 24.0f)))
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
    {
        // When the panel is not on screen it must never be reported as
        // focused/hovered: the main loop's hierarchy-context tracking reads
        // these flags, and a stale `focused` would wrongly force the Hierarchy
        // back to the game world while the Prefab panel is the active context.
        hovered = false;
        focused = false;
        return;
    }

    gui->beginDisabled(!enabled);
    gui->windowStart("World", &isOpened);

    // ----- Viewport toolbar -------------------------------------------------
    // Compact icon buttons: node-gizmo settings (align-local glyph), preview
    // settings (scene glyph) and the editor camera settings. Pivot mode, render
    // mode and the octree toggle now live inside the Gizmo / Preview Settings
    // dropdowns to keep the bar uncluttered.

    // Gizmo Settings dropdown: transform orientation (Local vs World), the
    // multi-object pivot reference, and the node gizmo icon size.
    gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3)); // square icon button
    if (gui->imageButton("GizmoSettingsBtn", iconAlignLocal, kVec2(24.0f, 24.0f)))
        ImGui::OpenPopup("GizmoSettings");
    gui->popStyleVar();
    if (gui->isItemHovered())
        gui->setItemTooltip("Gizmo orientation, pivot and node icon size");

    if (ImGui::BeginPopup("GizmoSettings"))
    {
        ImGui::TextUnformatted("Orientation");
        ImGui::Separator();
        int orient = (manager->manipulatorMode == ImGuizmo::LOCAL) ? 0 : 1;
        if (ImGui::RadioButton("Local", &orient, 0))
            manager->manipulatorMode = ImGuizmo::LOCAL;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gizmo aligned to the object's local orientation");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", &orient, 1))
            manager->manipulatorMode = ImGuizmo::WORLD;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gizmo aligned to world axes");

        ImGui::Separator();
        ImGui::TextUnformatted("Pivot");
        ImGui::Separator();
        gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0));
        gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3));
        pivotButton(gui, "PivotIndividual", iconPivotIndividual, PivotMode::Individual, manager->pivotMode);
        if (gui->isItemHovered())
            gui->setItemTooltip("Individual pivot");
        gui->sameLine();
        pivotButton(gui, "PivotCenter", iconPivotCenter, PivotMode::Center, manager->pivotMode);
        if (gui->isItemHovered())
            gui->setItemTooltip("Center pivot");
        gui->sameLine();
        pivotButton(gui, "PivotLastSelected", iconPivotLastSelected, PivotMode::LastSelected, manager->pivotMode);
        if (gui->isItemHovered())
            gui->setItemTooltip("Last selected pivot");
        gui->popStyleVar(); // FramePadding
        gui->popStyleVar(); // ItemSpacing

        ImGui::Separator();
        ImGui::TextUnformatted("Icon Size");
        ImGui::Separator();
        float iconSize = manager->iconGizmoSize;
        gui->setNextItemWidth(180.0f);
        if (gui->sliderFloat("##GizmoIconSize", &iconSize, 0.05f, 2.0f, "%.2f"))
            manager->setIconGizmoSize(iconSize);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Size of the node icons (light, decal, particle, camera, audio)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
            manager->setIconGizmoSize(0.5f);
        ImGui::EndPopup();
    }

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Preview Settings dropdown: viewport render mode plus the octree debug
    // bounds overlay.
    gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3)); // square icon button
    if (gui->imageButton("PreviewSettingsBtn", iconPreview, kVec2(24.0f, 24.0f)))
        ImGui::OpenPopup("PreviewSettings");
    gui->popStyleVar();
    if (gui->isItemHovered())
        gui->setItemTooltip("Viewport render mode and debug overlays");

    if (ImGui::BeginPopup("PreviewSettings"))
    {
        ImGui::TextUnformatted("Render Mode");
        ImGui::Separator();
        static const char *kRenderModeNames[] = {
            "Full", "Albedo", "Normals", "Wireframe", "Depth", "Object IDs", "Full+Wire"};
        int currentMode = (int)renderer->getRenderMode();
        gui->setNextItemWidth(180.0f);
        if (ImGui::Combo("##RenderMode", &currentMode, kRenderModeNames, 7))
            renderer->setRenderMode((kRenderMode)currentMode);

        ImGui::Separator();
        bool octreeDebug = renderer->getOctreeDebugEnabled();
        if (gui->checkbox("Octree", &octreeDebug))
            renderer->setOctreeDebugEnabled(octreeDebug);
        if (gui->isItemHovered())
            gui->setItemTooltip("Show octree node bounds");

        ImGui::Separator();
        bool showGrid = manager->showEditorGrid;
        if (gui->checkbox("Grid", &showGrid))
        {
            manager->showEditorGrid = showGrid;
            if (manager->editorGridMesh)
            {
                // kRenderer's draw traversal filters on getActive(), not on
                // getVisible(), so toggling visibility alone left the grid on
                // screen. Set both flags so the checkbox actually hides it.
                manager->editorGridMesh->setVisible(showGrid);
                manager->editorGridMesh->setActive(showGrid);
            }
        }
        if (gui->isItemHovered())
            gui->setItemTooltip("Show the editor grid plane");

        bool showGizmo = manager->showEditorGizmo;
        if (gui->checkbox("Gizmo", &showGizmo))
            manager->showEditorGizmo = showGizmo;
        if (gui->isItemHovered())
            gui->setItemTooltip("Show the transform gizmo");

        // Live physics cost. If Step is high while Awake is non-zero, the cost
        // is moving-body contact/solver work; when Awake drops to 0 the pile has
        // gone to sleep and the step should collapse.
        ImGui::Separator();
        ImGui::TextUnformatted("Physics");
        ImGui::Text("Step: %.2f ms", manager->getPhysicsStepMs());
        ImGui::Text("Bodies: %d (awake %d)",
                    manager->getPhysicsBodyCount(), manager->getPhysicsAwakeBodyCount());
        ImGui::Text("Contact events: %d", manager->getPhysicsContactEventCount());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Last physics step timing and activity counters");

        ImGui::EndPopup();
    }

    gui->sameLine();
    gui->dummy(kVec2(8, 0));
    gui->sameLine();

    // Camera settings popup
    gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3)); // square icon button
    if (gui->imageButton("CameraSettingsBtn", iconCamera, kVec2(24.0f, 24.0f)))
        ImGui::OpenPopup("CameraSettings");
    gui->popStyleVar();
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

    // Preview mode indicator (particle only — prefab has its own panel)
    if (manager->activeMode != Manager::EditorMode::GameWorld &&
        manager->activeMode != Manager::EditorMode::PrefabPreview)
    {
        const char *modeLabel = "Preview";
        switch (manager->activeMode)
        {
        case Manager::EditorMode::ParticlePreview: modeLabel = "Particle Preview"; break;
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

                // Terrain tiles own their material (created, persisted and deleted
                // with the tile; edited only from the Terrain panel), so a dropped
                // .mat must not preview or apply to them.
                if (hovObj)
                {
                    if (hovObj->getType() == kNodeType::NODE_TYPE_TERRAIN)
                        hovObj = nullptr;
                    else if (kMesh *hovMesh = dynamic_cast<kMesh *>(hovObj))
                        if (hovMesh->getSerializeType() == "terrain")
                            hovObj = nullptr;
                }

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
    // Terrain brushes are deliberately NOT tied to the editor's preview mode. They
    // operate on the game-world terrain through the terrain manager, so they stay
    // live whenever a tool is armed; the raycast is what decides whether a tile is
    // under the cursor. Gating on activeMode meant that a stray preview mode (left
    // over from opening a prefab or particle asset) silently disabled sculpting and
    // hid the brush ring, with no way back — viewport picking is suppressed while a
    // tool is active, so the scene could not even be re-selected to restore the mode.
    if (manager->panelTerrain)
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

        // Robustness: `hovered` and `focused` can both be false while the mouse is
        // physically over the viewport (another window stealing focus is enough),
        // which silently disabled every brush. Fall back to a rectangle test so a
        // stroke lands whenever the cursor is over the panel.
        if (!hovered && !focused)
        {
            kVec2 m = gui->getMousePos();
            if (m.x >= panelPos.x && m.x <= panelPos.x + panelSize.x &&
                m.y >= panelPos.y && m.y <= panelPos.y + panelSize.y)
                hovered = true;
        }

        // When in sculpt or paint mode, block object picking so the user
        // cannot select 3D objects by clicking in the viewport.
        // (The hierarchy panel bypasses this — it selects by tree click.)
        if (inToolMode && (hovered || focused) && !ImGuizmo::IsUsing() && !modifiersDown)
        {
            kVec2 imMouse = gui->getMousePos();

            bool doSculpt = isSculpting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            // Mask paint: left=add (paint the selected channel), right=erase.
            // The two were swapped here, so a left-click — the action the panel
            // advertises as "paint" — actually erased the channel, and erasing a
            // channel that was already empty changed nothing at all.
            bool doPaintAdd = isPainting && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool doPaintErase = isPainting && ImGui::IsMouseDown(ImGuiMouseButton_Right);
            // Height paint: left=lower, right=raise
            bool doHeightLower = isHeightPaint && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool doHeightRaise = isHeightPaint && ImGui::IsMouseDown(ImGuiMouseButton_Right);

            if (doSculpt || doPaintAdd || doPaintErase || doHeightLower || doHeightRaise)
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
                    // The Position control can move the whole grid away from its
                    // grid-space origin, so march the ray in terrain space and add
                    // the offset back only for the on-screen brush cursor.
                    const kVec3 terrainOffset = manager->panelTerrain->getTerrainGroupOffset();
                    origin -= terrainOffset;

                    float dirXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    // March regardless of orientation: a top-down camera gives
                    // dirXZ ≈ 0, which previously skipped the raycast and made
                    // terrain painting look broken.
                    if (dirXZ > 0.0001f || std::abs(dir.y) > 0.0001f)
                    {
                        // Tile geometry must follow the terrain manager rather than a
                        // literal: a project can have been saved with a different
                        // world size / resolution, and the brush would then sample
                        // the wrong tile entirely.
                        const float tileSize = tmgr->getWorldSize();
                        const int heightRes = tmgr->getHeightRes();
                        const float sampleSpacing = tileSize / static_cast<float>(heightRes - 1);
                        const float stepSize = sampleSpacing * 0.5f;
                        const float maxDist = 10000.0f;

                        float s = stepSize;
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

                            // No tile here means there is no ground to test against.
                            // Skipping the step (instead of treating it as y = 0) is
                            // what stops a phantom hit in empty space from breaking
                            // the march before it ever reaches real terrain.
                            if (!tile)
                            {
                                prevTerrainY = -1e30f;
                                continue;
                            }

                            float terrainY = tile->sampleHeight(p);

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
                            else if (doPaintAdd || doPaintErase)
                                manager->panelTerrain->paintSplatAtWorldPos(hitPos, doPaintErase);
                            else if (doHeightLower || doHeightRaise)
                                manager->panelTerrain->paintAtWorldPos(hitPos, doHeightLower);

                            // Real-time GPU update: fast VBO sub-update during painting
                            {
                                int gx, gz;
                                kTerrainManager::worldToGrid(hitPos, tmgr->getWorldSize(), gx, gz);
                                kTerrain *foundTile = tmgr->getTile(gx, gz);
                                if (foundTile)
                                {
                                    if (doSculpt || doHeightLower || doHeightRaise)
                                        foundTile->updateMesh();
                                    else if (doPaintAdd || doPaintErase)
                                        foundTile->updateSplatTexture();
                                }
                            }
                        }

                        // hitPos is in terrain space (the whole grid may be offset
                        // by the Position control), so add the offset back for the
                        // on-screen brush cursor.
                        manager->panelTerrain->paintCursorPos = hitPos + terrainOffset;
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
                        // See the brush raycast above: march in terrain space so a
                        // grid moved by the Position control still lines up.
                        const kVec3 terrainOffset = manager->panelTerrain->getTerrainGroupOffset();
                        origin -= terrainOffset;

                        float dirXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                        if (dirXZ > 0.0001f || std::abs(dir.y) > 0.0001f)
                        {
                            const float tileSize = tmgr->getWorldSize();
                            const int heightRes = tmgr->getHeightRes();
                            const float sampleSpacing = tileSize / static_cast<float>(heightRes - 1);
                            const float stepSize = sampleSpacing * 0.5f;
                            const float maxDist = 10000.0f;

                            float s = stepSize;
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

                                // See the note in the brush raycast above: a missing
                                // tile must not be mistaken for ground at y = 0.
                                if (!tile)
                                {
                                    prevTerrainY = -1e30f;
                                    continue;
                                }

                                float terrainY = tile->sampleHeight(p);

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

                            manager->panelTerrain->paintCursorPos = hitPos + terrainOffset;
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
        if (isSculpting || isPainting || isHeightPaint)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();

            if (!manager->panelTerrain->paintCursorValid)
            {
                // No terrain under the cursor yet — still draw a brush-size ring at
                // the mouse so the operator can see how big the brush is.
                kVec2 m = gui->getMousePos();
                float r = (std::max)(8.0f, (std::min)(220.0f,
                                                         manager->panelTerrain->sculpt.radius * 3.0f));
                ImU32 col = isHeightPaint ? IM_COL32(112, 171, 100, 170)
                                          : IM_COL32(255, 255, 255, 170);
                dl->AddCircle(ImVec2(m.x, m.y), r, col, 48, 2.0f);
                dl->AddCircleFilled(ImVec2(m.x, m.y), 2.0f, col);
            }
            else
            {

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
            } // end else (paintCursorValid)
        }
    }

    // ------------------------------------------------------------------
    // Multi-object gizmo
    // ------------------------------------------------------------------
    if (manager->showEditorGizmo && !manager->selectedObjects.empty())
    {
        // Terrain tiles are authored exclusively through their grid coordinates
        // and the Terrain panel. Their world transform must never be edited by
        // hand (moving a tile desynchronises it from its neighbours, its
        // heightmap and its collider), so they are excluded from the transform
        // gizmo at all times — including immediately after creation.
        auto isTerrainNode = [](kObject *obj) -> bool
        {
            if (!obj)
                return false;
            if (obj->getType() == NODE_TYPE_TERRAIN)
                return true;
            if (kMesh *mesh = dynamic_cast<kMesh *>(obj))
                return mesh->getSerializeType() == "terrain";
            return false;
        };

        // Gather valid, manually-transformable selected objects.
        std::vector<kObject *> selObjs;
        for (const auto &uuid : manager->selectedObjects)
        {
            kObject *obj = manager->findObjectByUuid(uuid);
            if (obj && obj->getActive() && !isTerrainNode(obj))
                selObjs.push_back(obj);
        }

        if (!selObjs.empty())
        {
            glm::mat4 view = editorCamera->getViewMatrix();
            glm::mat4 proj = editorCamera->getProjectionMatrix();

            // Compute pivot matrix
            glm::mat4 pivotMatrix;
            if (selObjs.size() == 1)
            {
                // A single object's gizmo always sits on its WORLD transform so
                // the manipulated matrix can be converted back into local space
                // correctly, even for nested objects.
                pivotMatrix = selObjs[0]->getModelMatrixWorld();
            }
            else if (manager->pivotMode == PivotMode::Center)
            {
                glm::vec3 center(0.0f);
                for (kObject *obj : selObjs)
                    center += obj->getPosition();
                center /= (float)selObjs.size();
                pivotMatrix = glm::translate(glm::mat4(1.0f), center);
            }
            else
            {
                // LastSelected or Individual → pivot at last selected. If the
                // active selection is a terrain tile (which has no gizmo), fall
                // back to the last transformable object.
                kObject *pivot = manager->selectedObject;
                if (!pivot || isTerrainNode(pivot) ||
                    std::find(selObjs.begin(), selObjs.end(), pivot) == selObjs.end())
                    pivot = selObjs.back();
                pivotMatrix = pivot->getModelMatrixWorld();
            }

            glm::mat4 pivotCopy = pivotMatrix;

            // BeginFrame() is called once per frame by the main loop (see
            // main.cpp). Calling it here as well would overwrite the gizmo-hover
            // snapshot for other panels drawn earlier in the frame.
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
                        // pivotMatrix started as this object's world matrix, so
                        // pivotCopy IS the new WORLD matrix. Convert it back to
                        // LOCAL space using the parent's world matrix before
                        // writing: writing world values as local made nested
                        // objects (e.g. a prefab child) absorb their parent's
                        // transform and jump to the wrong place once saved.
                        glm::mat4 localMatrix = pivotCopy;
                        if (obj->getParent())
                            localMatrix = glm::inverse(obj->getParent()->getModelMatrixWorld()) * pivotCopy;
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(localMatrix, scale, rot, pos, skew, persp);
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
                        // Center / LastSelected: apply the full world-space delta,
                        // then convert the resulting world matrix back into the
                        // object's local space (via its parent) so nested objects
                        // keep a correct relative transform.
                        glm::mat4 newWorld = delta * obj->getModelMatrixWorld();
                        glm::mat4 localMatrix = newWorld;
                        if (obj->getParent())
                            localMatrix = glm::inverse(obj->getParent()->getModelMatrixWorld()) * newWorld;
                        glm::vec3 pos, scale, skew;
                        glm::quat rot;
                        glm::vec4 persp;
                        glm::decompose(localMatrix, scale, rot, pos, skew, persp);
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