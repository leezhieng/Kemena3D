#include "panel_game.h"
#include <algorithm>
#include <cstdio>

PanelGame::PanelGame(kGuiManager *setGui, Manager *setManager)
    : gui(setGui), manager(setManager)
{
    // Load the transport-control icons (white glyphs on transparent PNGs) from
    // the embedded resources. Textures are cached by the asset manager, so the
    // texture IDs stay valid for the lifetime of the app.
    kAssetManager *am = manager->getAssetManager();
    if (am)
    {
        kTexture2D *texPlay = am->loadTexture2DFromResource("ICON_PLAY_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (texPlay)
            iconPlay = texPlay->getTextureID();

        kTexture2D *texPause = am->loadTexture2DFromResource("ICON_PAUSE_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (texPause)
            iconPause = texPause->getTextureID();

        kTexture2D *texStop = am->loadTexture2DFromResource("ICON_STOP_BUTTON", "icon", kTextureFormat::TEX_FORMAT_RGBA);
        if (texStop)
            iconStop = texStop->getTextureID();
    }
}

PanelGame::~PanelGame()
{
    delete gameRenderer;
}

// ---------------------------------------------------------------------------
// Camera helpers
// ---------------------------------------------------------------------------

kCamera *PanelGame::findGameCamera() const
{
    kWorld *world = manager->getWorld();
    if (!world)
        return nullptr;

    const auto &cams = world->getCameras();

    // Prefer the explicitly-set default — but verify it is still registered in
    // the world and is not the editor camera (handles deletion and edge cases).
    if (manager->defaultGameCamera &&
        manager->defaultGameCamera != manager->editorCamera &&
        std::find(cams.begin(), cams.end(), manager->defaultGameCamera) != cams.end())
    {
        return manager->defaultGameCamera;
    }

    // Fall back to the first non-editor camera registered in the world.
    for (kCamera *cam : cams)
    {
        if (cam != manager->editorCamera)
            return cam;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Simulation deltaTime
// ---------------------------------------------------------------------------

float PanelGame::getEffectiveDeltaTime(float dt) const
{
    return (playState == GamePlayState::Paused) ? 0.0f : dt;
}

// ---------------------------------------------------------------------------
// Scene snapshot helpers
// ---------------------------------------------------------------------------

void PanelGame::captureNodeRecursive(kObject *node)
{
    if (!node)
        return;
    ObjectTransformSnapshot snap;
    snap.uuid   = node->getUuid();
    snap.pos    = node->getPosition();
    snap.rot    = node->getRotation();
    snap.scale  = node->getScale();
    snap.active = node->getActive();
    snap.state  = node->serialize();   // Full JSON snapshot for non-transform revert
    sceneSnapshot.push_back(snap);
    for (kObject *child : node->getChildren())
        captureNodeRecursive(child);
}

void PanelGame::captureSnapshot()
{
    sceneSnapshot.clear();
    kScene *scene = manager->getScene();
    if (scene)
        captureNodeRecursive(scene->getRootNode());
}

void PanelGame::restoreSnapshot()
{
    for (const auto &snap : sceneSnapshot)
    {
        kObject *obj = manager->findObjectByUuid(snap.uuid);
        if (!obj)
            continue;

        // Restore transform and active state.
        obj->setPosition(snap.pos);
        obj->setRotation(snap.rot);
        obj->setScale(snap.scale);
        obj->setActive(snap.active);

        // Restore non-transform properties from the full JSON snapshot.
        const auto &j = snap.state;
        if (j.is_null() || !j.is_object())
            continue;

        // --- Common properties ---
        if (j.contains("name") && j["name"].is_string())
            obj->setName(kString(j["name"].get<std::string>()));
        if (j.contains("static") && j["static"].is_boolean())
            obj->setStatic(j["static"].get<bool>());

        // --- Camera-specific ---
        if (j.contains("fov") && j["fov"].is_number())
        {
            kCamera *cam = dynamic_cast<kCamera *>(obj);
            if (cam)
            {
                cam->setFOV(j["fov"].get<float>());
                if (j.contains("near_clip")    && j["near_clip"].is_number())    cam->setNearClip(j["near_clip"].get<float>());
                if (j.contains("far_clip")     && j["far_clip"].is_number())     cam->setFarClip(j["far_clip"].get<float>());
                if (j.contains("scene_uuid")   && j["scene_uuid"].is_string())   cam->setSceneUuid(j["scene_uuid"].get<std::string>());
                if (j.contains("aspect_ratio") && j["aspect_ratio"].is_number()) cam->setAspectRatio(j["aspect_ratio"].get<float>());
            }
        }

        // --- Light-specific ---
        if ((j.contains("power") && j["power"].is_number()) || j.contains("light_type"))
        {
            kLight *light = dynamic_cast<kLight *>(obj);
            if (light)
            {
                if (j.contains("power")    && j["power"].is_number())    light->setPower(j["power"].get<float>());
                if (j.contains("diffuse")  && j["diffuse"].is_object())
                {
                    const auto &d = j["diffuse"];
                    if (d.contains("x") && d["x"].is_number() &&
                        d.contains("y") && d["y"].is_number() &&
                        d.contains("z") && d["z"].is_number())
                        light->setDiffuseColor(kVec3(d["x"].get<float>(), d["y"].get<float>(), d["z"].get<float>()));
                }
                if (j.contains("specular") && j["specular"].is_object())
                {
                    const auto &s = j["specular"];
                    if (s.contains("x") && s["x"].is_number() &&
                        s.contains("y") && s["y"].is_number() &&
                        s.contains("z") && s["z"].is_number())
                        light->setSpecularColor(kVec3(s["x"].get<float>(), s["y"].get<float>(), s["z"].get<float>()));
                }
                if (j.contains("constant")     && j["constant"].is_number())     light->setConstant(j["constant"].get<float>());
                if (j.contains("linear")       && j["linear"].is_number())       light->setLinear(j["linear"].get<float>());
                if (j.contains("quadratic")    && j["quadratic"].is_number())    light->setQuadratic(j["quadratic"].get<float>());
                if (j.contains("cutoff")       && j["cutoff"].is_number())       light->setCutOff(j["cutoff"].get<float>());
                if (j.contains("outer_cutoff") && j["outer_cutoff"].is_number()) light->setOuterCutOff(j["outer_cutoff"].get<float>());
            }
        }

        // --- Mesh-specific ---
        if (j.contains("cast_shadow") && j["cast_shadow"].is_boolean())
        {
            kMesh *mesh = dynamic_cast<kMesh *>(obj);
            if (mesh)
            {
                mesh->setCastShadow(j["cast_shadow"].get<bool>());
                if (j.contains("receive_shadow") && j["receive_shadow"].is_boolean())
                    mesh->setReceiveShadow(j["receive_shadow"].get<bool>());
            }
        }
    }
    sceneSnapshot.clear();
}

// ---------------------------------------------------------------------------
// Play state transitions
// ---------------------------------------------------------------------------

void PanelGame::pressPlay()
{
    // Entering Play mode must return the editor to the GameWorld view when it
    // is in a particle/animator preview. Otherwise main.cpp's game logic gate
    // would skip stepAnimators()/physics/scripts even though the Game panel is
    // rendering the game world — making the animator appear frozen. This also
    // covers resuming from Pause after the user opened a preview asset.
    // PrefabPreview is intentionally left alone: it renders through its own
    // world and already lets the game logic keep running.
    if (manager->activeMode != Manager::EditorMode::GameWorld &&
        manager->activeMode != Manager::EditorMode::PrefabPreview)
        manager->setEditorMode(Manager::EditorMode::GameWorld);

    if (playState == GamePlayState::Stopped)
    {
        kWorld *world = manager->getWorld();

        // Clear defaultGameCamera if it is the editor camera or has been deleted
        // from the world (stale pointer), so the auto-pick below can refresh it.
        if (manager->defaultGameCamera && world)
        {
            const auto &cams = world->getCameras();
            bool isEditorCam = (manager->defaultGameCamera == manager->editorCamera);
            bool isStale = (std::find(cams.begin(), cams.end(),
                                      manager->defaultGameCamera) == cams.end());
            if (isEditorCam || isStale)
                manager->defaultGameCamera = nullptr;
        }

        // Auto-pick the first non-editor camera as default if none is set.
        if (!manager->defaultGameCamera && world)
        {
            for (kCamera *cam : world->getCameras())
            {
                if (cam != manager->editorCamera)
                {
                    manager->defaultGameCamera = cam;
                    break;
                }
            }
        }

        // Remember whether the project was already dirty before entering play.
        // Scene edits made while playing are temporary (restored on Stop) and
        // must not leave the project flagged as unsaved.
        projectSavedBeforePlay = manager->projectSaved;

        captureSnapshot();
        // Spawn physics bodies for every object that opted in. Must happen
        // AFTER captureSnapshot so the snapshot records the editor-authored
        // transforms (not whatever physics moves them to during update).
        manager->startPhysicsSimulation();
        // Start audio and animators before scripts so Awake()/Start() can
        // already drive sound playback and animation state.
        manager->startGameAudio();
        manager->startAnimators();
        // Compile attached scripts to bytecode and dispatch Awake()/Start().
        manager->startScripts();
        playState = GamePlayState::Playing;
    }
    else if (playState == GamePlayState::Paused)
    {
        // Resume all in-game audio clips that were paused by pressPause().
        manager->resumeGameAudio();
        playState = GamePlayState::Playing;
    }
}

void PanelGame::pressPause()
{
    if (playState == GamePlayState::Playing)
    {
        // Freeze in-game audio in sync with the paused simulation.
        manager->pauseGameAudio();
        playState = GamePlayState::Paused;
    }
}

void PanelGame::pressStop()
{
    if (playState != GamePlayState::Stopped)
    {
        // Stop all in-game audio before tearing down the scene state.
        manager->stopGameAudio();
        // Tear down physics BEFORE restoring transforms so the bodies don't
        // overwrite our restored positions on a final sync.
        manager->stopPhysicsSimulation();
        // Dispatch OnDestroy() and release every script instance.
        manager->stopScripts();
        // Tear down animator controllers and restore the bind pose.
        manager->stopAnimators();
        restoreSnapshot();

        // Restore the dirty flag captured before Play. Any scene edits made
        // during play were rolled back by the snapshot above, so they must not
        // keep the project marked as unsaved.
        playState = GamePlayState::Stopped;
        manager->projectSaved = projectSavedBeforePlay;
        manager->refreshWindowTitle();
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void PanelGame::draw(bool &isOpened)
{
    if (!isOpened)
        return;

    bool enabled = manager->projectOpened;
    bool isStopped = (playState == GamePlayState::Stopped);
    bool isPlaying = (playState == GamePlayState::Playing);
    bool isPaused = (playState == GamePlayState::Paused);

    gui->beginDisabled(!enabled);
    // Disable keyboard/gamepad navigation inside the game panel so the arrow
    // keys (and Tab) never move focus between the Play/Pause/Stop buttons.
    gui->windowStart("Game", &isOpened, ImGuiWindowFlags_NoNavInputs);

    // ---- Toolbar: status (right) + transport buttons (centred) -------------
    // While the game is playing, sample a live FPS readout (frames counted over
    // a rolling half-second window) that is shown in the status label.
    if (isPlaying)
    {
        fpsElapsed += gui->getDeltaTime();
        ++fpsFrames;
        if (fpsElapsed >= 0.5f)
        {
            currentFps = (fpsFrames > 0) ? (float)fpsFrames / fpsElapsed : 0.0f;
            fpsElapsed = 0.0f;
            fpsFrames = 0;
        }
    }

    // Status string — "Playing" carries the measured frame rate. Before the
    // first rolling sample completes, fall back to the instantaneous rate so
    // the label never flashes "Playing (0 fps)" at startup.
    char statusText[64];
    if (isPlaying)
    {
        float fpsVal = currentFps;
        if (fpsVal <= 0.0f)
        {
            float dt = gui->getDeltaTime();
            if (dt > 0.0001f)
                fpsVal = 1.0f / dt;
        }
        snprintf(statusText, sizeof(statusText), "Playing (%.0f fps)", fpsVal);
    }
    else if (isPaused)
        snprintf(statusText, sizeof(statusText), "Paused");
    else
        snprintf(statusText, sizeof(statusText), "Stopped");

    // Full toolbar width, measured before any widget is placed on this row.
    const float rowW    = gui->getContentRegionAvail().x;
    const float statusW = gui->calcTextSize(statusText).x;

    // Square icon buttons (uniform frame padding so width == height).
    gui->pushStyleVar(ImGuiStyleVar_ItemSpacing, kVec2(2, 0));
    gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(3, 3));

    // Transport geometry.
    const float iconSize = 24.0f;          // Icon glyph size inside each button.
    const float btnW     = iconSize + 6.0f; // Button width  = icon + 2 * FramePadding.x (3 px).
    const float btnH     = iconSize + 6.0f; // Button height = icon + 2 * FramePadding.y (3 px).
    const float btnGap   = 6.0f;           // Horizontal gap between the buttons.
    const float buttonsW = btnW * 3.0f + btnGap * 2.0f;

    // The three buttons stay centred; they are only pushed left when they would
    // otherwise collide with the right-aligned status label on narrow panels.
    const float statusStartX   = rowW - statusW;        // Right-aligned status.
    const float maxButtonsEndX = statusStartX - 12.0f;  // 12 px clearance from the status.
    float centredX = (rowW - buttonsW) * 0.5f;
    if (centredX + buttonsW > maxButtonsEndX)
        centredX = maxButtonsEndX - buttonsW;
    if (centredX < 0.0f)
        centredX = 0.0f;

    // The status text is shorter than the icon buttons, so nudge it down so
    // both share the same vertical centre line.
    const float rowTopY     = gui->getCursorPosY();
    const float textH       = gui->calcTextSize(statusText).y;
    const float textOffsetY = (btnH > textH) ? (btnH - textH) * 0.5f : 0.0f;

    // Status text, right-aligned & vertically centred on the toolbar.
    gui->setCursorPos(kVec2(statusStartX, rowTopY + textOffsetY));
    if (isPlaying)
        gui->textColored(kVec4(0.35f, 0.90f, 0.35f, 1.0f), statusText);
    else if (isPaused)
        gui->textColored(kVec4(1.00f, 0.80f, 0.20f, 1.0f), statusText);
    else
        gui->textDisabled(statusText);

    // ---- Aspect-ratio preset (left-aligned on the transport row) -----------
    // Height-matched to the transport buttons and vertically centred on the same
    // row, with a small left margin so it doesn't hug the panel edge.
    {
        const float aspectPadY = ImMax(0.0f, (btnH - gui->getFontSize()) * 0.5f);
        gui->pushStyleVar(ImGuiStyleVar_FramePadding, kVec2(8.0f, aspectPadY));
        gui->setCursorPos(kVec2(8.0f, rowTopY)); // 8 px left margin, same row top

        static const char *kAspectNames[] = { "Free Aspect", "3:2", "4:3", "5:4", "16:9", "16:10", "Custom" };
        int aspectIdx = (int)aspectRatio;
        gui->setNextItemWidth(140.0f);
        if (ImGui::Combo("##gameAspect", &aspectIdx, kAspectNames, IM_ARRAYSIZE(kAspectNames)))
            aspectRatio = (GameAspectRatio)aspectIdx;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Game viewport aspect ratio");

        if (aspectRatio == GameAspectRatio::Custom)
        {
            gui->sameLine(0.0f, 4.0f);
            gui->setNextItemWidth(84.0f);
            ImGui::DragFloat("##gameAspectW", &customAspectW, 0.1f, 0.1f, 100.0f, "W %.1f");
            gui->sameLine(0.0f, 4.0f);
            gui->setNextItemWidth(84.0f);
            ImGui::DragFloat("##gameAspectH", &customAspectH, 0.1f, 0.1f, 100.0f, "H %.1f");
        }
        gui->popStyleVar();
    }

    // Place the centred transport buttons at the row top.
    gui->setCursorPos(kVec2(centredX, rowTopY));

    // ---- Play button (icon) ------------------------------------------------
    if (isPlaying)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->imageButton("GamePlay", iconPlay, kVec2(iconSize, iconSize)) && !isPlaying)
        pressPlay();
    if (isPlaying)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isPaused ? "Resume" : "Play");

    gui->sameLine(0.0f, btnGap);

    // ---- Pause button (icon) -----------------------------------------------
    if (isPaused)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.85f, 0.65f, 0.10f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.95f, 0.75f, 0.20f, 1.00f));
    }
    gui->beginDisabled(isStopped);
    if (gui->imageButton("GamePause", iconPause, kVec2(iconSize, iconSize)))
    {
        if (isPlaying)
            pressPause();
        else if (isPaused)
            pressPlay(); // resume
    }
    gui->endDisabled();
    if (isPaused)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isPaused ? "Resume" : "Pause");

    gui->sameLine(0.0f, btnGap);

    // ---- Stop button (icon) ------------------------------------------------
    gui->beginDisabled(isStopped);
    if (!isStopped)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.72f, 0.16f, 0.16f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.88f, 0.26f, 0.26f, 1.00f));
    }
    if (gui->imageButton("GameStop", iconStop, kVec2(iconSize, iconSize)))
        pressStop();
    if (!isStopped)
        gui->popStyleColor(2);
    gui->endDisabled();
    if (gui->isItemHovered())
        gui->setItemTooltip("Stop and reset scene");

    gui->popStyleVar(); // FramePadding
    gui->popStyleVar(); // ItemSpacing

    gui->dummy(kVec2(0.0f, 0.0f)); // Vertical gap between toolbar and viewport

    // Resolve the requested aspect ratio (0 = free / fill the panel).
    float targetAspect = 0.0f;
    switch (aspectRatio)
    {
        case GameAspectRatio::Ratio3_2:   targetAspect = 3.0f / 2.0f;   break;
        case GameAspectRatio::Ratio4_3:   targetAspect = 4.0f / 3.0f;   break;
        case GameAspectRatio::Ratio5_4:   targetAspect = 5.0f / 4.0f;   break;
        case GameAspectRatio::Ratio16_9:  targetAspect = 16.0f / 9.0f;  break;
        case GameAspectRatio::Ratio16_10: targetAspect = 16.0f / 10.0f; break;
        case GameAspectRatio::Custom:
            if (customAspectW > 0.0f && customAspectH > 0.0f)
                targetAspect = customAspectW / customAspectH;
            break;
        case GameAspectRatio::Free:
        default:
            break;
    }

    // ---- Game viewport -----------------------------------------------------
    kVec2 avail = gui->getContentRegionAvail();
    if (avail.x > 0 && avail.y > 0)
    {
        int newW = (int)avail.x;
        int newH = (int)avail.y;

        // Letterbox to the requested aspect ratio when one is selected.
        float imgOffX = 0.0f;
        if (targetAspect > 0.0f)
        {
            if (avail.x / avail.y > targetAspect)
            {
                newH = (int)avail.y;
                newW = (int)((float)newH * targetAspect);
            }
            else
            {
                newW = (int)avail.x;
                newH = (int)((float)newW / targetAspect);
            }
            if (newW < 1) newW = 1;
            if (newH < 1) newH = 1;
            imgOffX = (avail.x - (float)newW) * 0.5f;
        }

        // Create or resize the offscreen renderer to match this panel
        if (!gameRenderer)
        {
            gameRenderer = new kOffscreenRenderer(newW, newH);
            gameRenderer->setAssetManager(manager->getAssetManager());
            gameRenderer->setBackgroundColor(kVec4(0.0f, 0.0f, 0.0f, 1.0f));
            lastRendererW = newW;
            lastRendererH = newH;
        }
        else if (newW != lastRendererW || newH != lastRendererH)
        {
            gameRenderer->resize(newW, newH);
            lastRendererW = newW;
            lastRendererH = newH;
        }

        kCamera *gameCamera = findGameCamera();

        // Find the scene this camera is assigned to, falling back to manager->getScene()
        kScene *gameScene = nullptr;
        if (gameCamera && !gameCamera->getSceneUuid().empty())
        {
            kWorld *world = manager->getWorld();
            if (world)
            {
                for (kScene *s : world->getScenes())
                {
                    if (s->getUuid() == gameCamera->getSceneUuid())
                    {
                        gameScene = s;
                        break;
                    }
                }
            }
        }
        if (!gameScene)
            gameScene = manager->getScene();

        if (gameCamera && gameScene)
        {
            // Keep camera aspect ratio in sync with the panel
            gameCamera->setAspectRatio((float)newW / (float)newH);

            // Update the audio listener position from the game camera every frame
            // while the game is running (no-op when stopped or no spatial audio).
            if (playState != GamePlayState::Stopped)
                manager->updateGameAudio(gameCamera);

            // Render scene only — no editor overlay, no outlines, no debug shapes
            gameRenderer->render(manager->getWorld(), gameScene, gameCamera);

            // Centre the (possibly letterboxed) view inside the panel.
            if (imgOffX > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + imgOffX);

            ImVec2 imgMin = ImGui::GetCursorScreenPos();
            ImTextureRef tex((ImTextureID)(uintptr_t)gameRenderer->getTexture());
            gui->setNextItemAllowOverlap();
            ImGui::Image(tex, ImVec2((float)newW, (float)newH), ImVec2(0, 1), ImVec2(1, 0));

            // ---- Ingame UI (.ui) overlays -----------------------------------
            // Every active scene object created from a .ui asset (dragged in
            // from the Project panel) draws its layout over the game view.
            {
                ImDrawList *fg = ImGui::GetWindowDrawList();
                ImVec2 imgMax(imgMin.x + (float)newW, imgMin.y + (float)newH);
                for (const auto &entry : manager->getObjectUiAssets())
                {
                    kObject *uiObj = manager->findObjectByUuid(entry.first);
                    if (!uiObj || !uiObj->getActive())
                        continue;
                    if (auto layout = manager->getGuiLayoutForAsset(entry.second))
                    {
                        // Resolve .ui image widgets to their project textures so
                        // the overlay draws the real images, fitted per widget.
                        auto resolveTex = [this](const std::string& uuid) -> GuiTextureInfo
                        {
                            GuiTextureInfo info;
                            if (kTexture2D* t = manager->getProjectTexture(uuid, "uiImage"))
                            {
                                if (t->getTextureID() != 0)
                                {
                                    info.glId = t->getTextureID();
                                    info.size = ImVec2((float)t->getWidth(), (float)t->getHeight());
                                }
                            }
                            return info;
                        };
                        renderGuiLayout(fg, *layout, imgMin, imgMax, resolveTex);
                    }
                }
            }
        }
        else
        {
            // No game camera → black screen with centered message
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                pos, ImVec2(pos.x + (float)newW, pos.y + (float)newH),
                IM_COL32(0, 0, 0, 255));

            const char *msg = "No Camera";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(pos.x + ((float)newW - ts.x) * 0.5f,
                       pos.y + ((float)newH - ts.y) * 0.5f),
                IM_COL32(150, 150, 150, 255), msg);

            ImGui::Dummy(ImVec2((float)newW, (float)newH));
        }
    }

    gui->windowEnd();
    gui->endDisabled();
}
