#include "panel_inspector.h"
#include "util.h"
#include "panel_project.h"
#include "panel_animator.h"
#include "commands.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <GL/glew.h>
#include <kemena/kanimator.h>
#include <kemena/kskelanimation.h>
#include <kemena/kdecal.h>

using namespace kemena;
namespace fs = std::filesystem;

// Forward declarations for file-local helpers used by drawShaderPreview()
static bool beginPropTable(kGuiManager *gui, const char *id);
static void propLabel(kGuiManager *gui, const char *label);

// Tag / Layer editor modals (defined at the bottom of this file).
static void drawListEditorModal(Manager *manager, const char *title, bool &open,
                                std::vector<std::string> &list, char *newBuf,
                                size_t newBufSize, bool (*addFn)(Manager *, const std::string &),
                                void (*removeFn)(Manager *, const std::string &),
                                bool lockFirst);
static void drawTagLayerEditorModals(Manager *manager);

PanelInspector::PanelInspector(kGuiManager *setGuiManager, Manager *setManager)
{
    gui = setGuiManager;
    manager = setManager;

    kAssetManager *am = manager->getAssetManager();
    if (!am)
        return;

    auto loadIcon = [&](const char *res) -> ImTextureRef
    {
        kTexture2D *t = am->loadTexture2DFromResource(res, "icon", kTextureFormat::TEX_FORMAT_RGBA);
        return t ? (ImTextureRef)(intptr_t)t->getTextureID() : nullptr;
    };

    iconObjMesh = loadIcon("ICON_OBJECT_MESH");
    iconObjLight = loadIcon("ICON_OBJECT_LIGHT");
    iconObjCamera = loadIcon("ICON_OBJECT_CAMERA");
    iconObjAudio = loadIcon("ICON_OBJECT_AUDIO");
    iconObjAudioListener = loadIcon("ICON_OBJECT_AUDIO_LISTENER");
    iconObjScene = loadIcon("ICON_OBJECT_SCENE");
    iconObjTerrain = loadIcon("ICON_OBJECT_TERRAIN");
    iconObjPrefab = loadIcon("ICON_OBJECT_PREFAB");
    iconObjPrefabEdited = loadIcon("ICON_OBJECT_PREFAB_EDITED");

    iconFileModel = loadIcon("ICON_MODEL_FILE");
    iconFileImage = loadIcon("ICON_IMAGE_FILE");
    iconFileFolder = loadIcon("ICON_FOLDER_FILE");
    iconFileMaterial = loadIcon("ICON_MATERIAL_FILE");
    iconFilePrefab = loadIcon("ICON_PREFAB_FILE");
    iconFileAudio = loadIcon("ICON_AUDIO_FILE");
    iconFileVideo = loadIcon("ICON_VIDEO_FILE");
    iconFileScript = loadIcon("ICON_SCRIPT_FILE");
    iconFileText = loadIcon("ICON_TEXT_FILE");
    iconFileWorld = loadIcon("ICON_WORLD_FILE");
    iconFileOther = loadIcon("ICON_OTHER_FILE");
}

PanelInspector::~PanelInspector()
{
    delete previewRenderer;
    for (auto *t : previewDefaultTextures)
        delete t;
    delete previewShader;
    delete previewMat;
    delete previewCamera;
    delete previewWorld; // owns previewScene, previewMesh, previewLight

    delete modelViewRenderer;
    delete modelViewCamera;
    delete modelViewWorld; // owns modelViewScene, modelViewLight (not modelViewMesh — AM-owned)
    for (auto *m : modelViewDefaultMats)
        delete m;

    delete matViewMat;
    delete matViewRenderer;
    delete matViewCamera;
    delete matViewWorld; // owns matViewScene, matViewMesh, matViewLight

    delete animPreviewRenderer;
    delete animPreviewCamera;
    delete animPreviewWorld; // owns animPreviewScene, animPreviewLight (not animPreviewMesh — AM-owned)
    delete animPreviewMat;
    delete animPreviewAnimator;
    // animPreviewClip is owned by the asset manager, not us
}

// ---------------------------------------------------------------------------
// Shader preview helpers
// ---------------------------------------------------------------------------

void PanelInspector::initPreviewScene()
{
    if (previewScene)
        return;

    previewWorld = createWorld(createAssetManager());
    previewScene = previewWorld->createScene("preview");
    previewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview window (see matViewer note).
    previewScene->setShadowsEnabled(false);
    previewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    previewMesh = kMeshGenerator::generateSphere(1.0f, 32, 32);
    previewScene->addMesh(previewMesh);

    previewLight = previewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    previewLight->setPower(1.5f);

    previewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    previewCamera->setFOV(45.0f);
    previewCamera->setAspectRatio(1.0f);
    previewCamera->setNearClip(0.1f);
    previewCamera->setFarClip(100.0f);
    previewCamera->setLookAt(kVec3(0.0f, 0.0f, 0.0f));
    previewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updatePreviewLight()
{
    if (!previewLight || !previewScene)
        return;
    if (!lightEnabled)
    {
        previewLight->setPower(0.0f);
        previewScene->setAmbientLightColor(kVec3(0.5f, 0.5f, 0.5f));
        return;
    }
    previewLight->setPower(1.5f);
    previewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    // Renderer reads sun light direction from the object's rotation, not setDirection()
    previewLight->setRotation(kVec3(lightPitch, lightYaw, 0.0f));
}

void PanelInspector::loadPreviewParams(const std::string &uuid)
{
    // Reset to defaults
    prevDiffuse[0] = prevDiffuse[1] = prevDiffuse[2] = 1.0f;
    prevSpecular[0] = prevSpecular[1] = prevSpecular[2] = 1.0f;
    prevShininess = 32.0f;
    prevMetallic = 0.0f;
    prevRoughness = 0.5f;

    if (uuid.empty())
        return;
    try
    {
        fs::path p = fs::path(manager->baseDir) / "shader_preview_params.json";
        if (!fs::exists(p))
            return;
        std::ifstream f(p);
        if (!f.is_open())
            return;
        nlohmann::json j;
        f >> j;
        if (!j.contains(uuid))
            return;
        auto &s = j[uuid];
        if (s.contains("diffuse") && s["diffuse"].is_array() && s["diffuse"].size() >= 3)
        {
            prevDiffuse[0] = s["diffuse"][0];
            prevDiffuse[1] = s["diffuse"][1];
            prevDiffuse[2] = s["diffuse"][2];
        }
        if (s.contains("specular") && s["specular"].is_array() && s["specular"].size() >= 3)
        {
            prevSpecular[0] = s["specular"][0];
            prevSpecular[1] = s["specular"][1];
            prevSpecular[2] = s["specular"][2];
        }
        if (s.contains("shininess"))
            prevShininess = s["shininess"].get<float>();
        if (s.contains("metallic"))
            prevMetallic = s["metallic"].get<float>();
        if (s.contains("roughness"))
            prevRoughness = s["roughness"].get<float>();
    }
    catch (...)
    {
    }
}

void PanelInspector::savePreviewParams(const std::string &uuid)
{
    if (uuid.empty())
        return;
    try
    {
        fs::path p = fs::path(manager->baseDir) / "shader_preview_params.json";
        nlohmann::json j;
        if (fs::exists(p))
        {
            std::ifstream f(p);
            if (f.is_open())
            {
                try
                {
                    f >> j;
                }
                catch (...)
                {
                }
            }
        }
        j[uuid]["diffuse"] = {prevDiffuse[0], prevDiffuse[1], prevDiffuse[2]};
        j[uuid]["specular"] = {prevSpecular[0], prevSpecular[1], prevSpecular[2]};
        j[uuid]["shininess"] = prevShininess;
        j[uuid]["metallic"] = prevMetallic;
        j[uuid]["roughness"] = prevRoughness;
        std::ofstream f(p);
        if (f.is_open())
            f << j.dump(4);
    }
    catch (...)
    {
    }
}

void PanelInspector::applyPreviewParams()
{
    if (!previewMat)
        return;
    previewMat->setDiffuseColor(kVec3(prevDiffuse[0], prevDiffuse[1], prevDiffuse[2]));
    previewMat->setAmbientColor(kVec3(prevDiffuse[0] * 0.08f, prevDiffuse[1] * 0.08f, prevDiffuse[2] * 0.08f));
    previewMat->setSpecularColor(kVec3(prevSpecular[0], prevSpecular[1], prevSpecular[2]));
    previewMat->setShininess(prevShininess);
    previewMat->setMetallic(prevMetallic);
    previewMat->setRoughness(prevRoughness);
}

void PanelInspector::rebuildPreviewShader()
{
    for (auto *t : previewDefaultTextures)
        delete t;
    previewDefaultTextures.clear();
    delete previewShader;
    delete previewMat;
    previewShader = nullptr;
    previewMat = nullptr;
    prevValid = false;

    if (prevGlsl.empty())
        return;

    initPreviewScene();
    if (!previewMesh)
        return;

    previewShader = new kShader();
    previewShader->loadGlslCode(prevGlsl);
    if (previewShader->getShaderProgram() == 0)
    {
        delete previewShader;
        previewShader = nullptr;
        return;
    }

    previewMat = new kMaterial();
    previewMat->setShader(previewShader);
    applyPreviewParams();

    // Bind a 1×1 white placeholder for every sampler2D in the shader so that
    // stale GL texture units from the main scene render don't bleed through.
    static const uint8_t white[4] = {255, 255, 255, 255};
    std::regex re(R"(uniform\s+sampler2D\s+(\w+)\s*;)");
    std::sregex_iterator it(prevGlsl.begin(), prevGlsl.end(), re);
    for (; it != std::sregex_iterator(); ++it)
    {
        const std::string samplerName = (*it)[1].str();

        uint32_t texId = 0;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        kTexture2D *t = new kTexture2D();
        t->setTextureID(texId);
        t->setTextureName(samplerName);
        t->setType(kTextureType::TEX_TYPE_2D);
        previewDefaultTextures.push_back(t);
        previewMat->addTexture(t);
    }

    previewMesh->setMaterial(previewMat);
    prevValid = true;
}

void PanelInspector::drawShaderPreview()
{
    if (!previewRenderer)
    {
        previewRenderer = new kOffscreenRenderer(256, 256);
        previewRenderer->setAssetManager(manager->getAssetManager());
    }

    initPreviewScene();

    const auto &ps = manager->shaderPreview;

    // UUID changed: save old params, load new, invalidate shader
    if (ps.uuid != prevUuid)
    {
        if (!prevUuid.empty())
            savePreviewParams(prevUuid);
        prevUuid = ps.uuid;
        loadPreviewParams(prevUuid);
        prevGlsl.clear();
        prevValid = false;
    }

    // GLSL changed: rebuild shader/material
    if (!ps.glslSource.empty() && ps.glslSource != prevGlsl)
    {
        prevGlsl = ps.glslSource;
        rebuildPreviewShader();
    }

    // Update light direction / state every frame
    updatePreviewLight();

    // Render offscreen (only when shader is valid)
    if (prevValid && previewScene && previewCamera && previewRenderer)
    {
        applyPreviewParams();
        previewRenderer->render(previewWorld, previewScene, previewCamera);
    }

    // -----------------------------------------------------------------------
    // Header
    // -----------------------------------------------------------------------
    gui->textDisabled("Shader Preview");
    if (!ps.shaderType.empty())
    {
        ImGui::SameLine(0, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.80f, 0.45f, 1.0f));
        ImGui::Text("(%s)", ps.shaderType.c_str());
        ImGui::PopStyleColor();
    }
    gui->spacing();

    // -----------------------------------------------------------------------
    // Preview area
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize;
    float sz = std::min(std::max(avail, 1.0f), 256.0f);
    float ox = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    // Draw rendered image or placeholder via DrawList (doesn't advance cursor)
    if (prevValid && previewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)previewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        dl->AddRect(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                    IM_COL32(60, 60, 75, 200));
        const char *msg = ps.glslSource.empty()
                              ? "Compile shader to preview"
                              : "Shader compile error";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(
            ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                   screenPos.y + (sz - ts.y) * 0.5f),
            IM_COL32(110, 115, 140, 200), msg);
    }

    // InvisibleButton advances cursor and captures mouse so IsItemActive() works for drag
    ImGui::InvisibleButton("##shaderPreview", ImVec2(sz, sz),
                           ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    // clip=false bypasses window clipping rect so hover is reliable regardless of scroll position
    bool pvHovered = ImGui::IsMouseHoveringRect(screenPos, imgMax, false);
    if (pvHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle button (top-right corner of preview)
    // -----------------------------------------------------------------------
    const float btR = 11.0f;
    const float btPad = 7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool btHover = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    // Background shadow + fill
    ImU32 btFg = lightEnabled
                     ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60, 220))
                     : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80, 80, 95, 200));
    dl->AddCircleFilled(btCenter, btR, IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    // Bulb icon: circle (globe) + two horizontal base lines
    float bx = btCenter.x, by = btCenter.y;
    ImU32 bulbInk = IM_COL32(25, 18, 5, lightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        lightEnabled = !lightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", lightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Drag to rotate light (RMB held on preview, outside the bulb button)
    // -----------------------------------------------------------------------
    // IsItemActive() is reliable because InvisibleButton captures the mouse press,
    // preventing the window from stealing the drag for scrolling.
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !btHover && !isDraggingLight)
        isDraggingLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingLight = false;
    if (isDraggingLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        lightYaw += delta.x * 0.015f;
        lightPitch -= delta.y * 0.015f;
        lightPitch = std::clamp(lightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();

    // -----------------------------------------------------------------------
    // Preview parameters
    // -----------------------------------------------------------------------
    const std::string &type = ps.shaderType;
    bool changed = false;

    if (gui->collapsingHeader("Preview Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!beginPropTable(gui, "ShaderPrevTable"))
            return;

        if (type == "Flat")
        {
            propLabel(gui, "Color");
            if (ImGui::ColorEdit3("##PrevFlatColor", prevDiffuse))
                changed = true;
        }
        else
        {
            propLabel(gui, "Albedo");
            if (ImGui::ColorEdit3("##PrevAlbedo", prevDiffuse))
                changed = true;
        }

        if (type == "Phong")
        {
            propLabel(gui, "Specular");
            if (ImGui::ColorEdit3("##PrevSpec", prevSpecular))
                changed = true;
            propLabel(gui, "Shininess");
            if (ImGui::DragFloat("##PrevShine", &prevShininess, 0.5f, 0.0f, 512.0f))
                changed = true;
        }
        else if (type == "PBR" || type.empty())
        {
            propLabel(gui, "Metallic");
            if (ImGui::DragFloat("##PrevMetal", &prevMetallic, 0.01f, 0.0f, 1.0f))
                changed = true;
            propLabel(gui, "Roughness");
            if (ImGui::DragFloat("##PrevRough", &prevRoughness, 0.01f, 0.0f, 1.0f))
                changed = true;
        }

        gui->tableEnd();
    }

    if (changed)
        savePreviewParams(prevUuid);
}

// ---------------------------------------------------------------------------
// Model viewer
// ---------------------------------------------------------------------------

void PanelInspector::initModelViewScene()
{
    if (modelViewScene)
        return;

    modelViewWorld = createWorld(createAssetManager());
    modelViewScene = modelViewWorld->createScene("modelViewer");
    modelViewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview window (see matViewer note).
    modelViewScene->setShadowsEnabled(false);
    modelViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    modelViewLight = modelViewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    modelViewLight->setPower(1.5f);

    modelViewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    modelViewCamera->setFOV(45.0f);
    modelViewCamera->setAspectRatio(1.0f);
    modelViewCamera->setNearClip(0.1f);
    modelViewCamera->setFarClip(500.0f);
    modelViewCamera->setLookAt(kVec3(0.0f));
    modelViewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updateModelViewLight()
{
    if (!modelViewLight || !modelViewScene)
        return;
    if (!modelViewLightEnabled)
    {
        modelViewLight->setPower(0.0f);
        return;
    }
    modelViewLight->setPower(1.5f);
    modelViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
    modelViewLight->setRotation(kVec3(modelViewLightPitch, modelViewLightYaw, 0.0f));
}

void PanelInspector::frameModelViewCamera()
{
    if (!modelViewMesh || !modelViewCamera)
        return;

    // Compute AABB using the mesh's loaded state (no rotation reset — matches thumbnail)
    kAABB combined;
    std::function<void(kMesh *)> expand = [&](kMesh *m)
    {
        m->calculateModelMatrix();
        kAABB b = m->getWorldAABB();
        if (b.isValid())
        {
            combined.expandBy(b.min);
            combined.expandBy(b.max);
        }
        for (kObject *c : m->getChildren())
            if (c->getType() == kNodeType::NODE_TYPE_MESH)
                expand(static_cast<kMesh *>(c));
    };
    expand(modelViewMesh);

    modelViewCenter = combined.isValid() ? combined.center() : kVec3(0.0f);
    kVec3 he = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
    float radius = glm::length(he);
    if (radius < 0.001f)
        radius = 1.0f;

    float fov = 45.0f;
    modelViewCamDist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
    kVec3 dir = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
    float heAlong = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

    modelViewCamera->setFOV(fov);
    modelViewCamera->setNearClip(std::max(0.0001f, modelViewCamDist - heAlong - radius * 0.05f));
    modelViewCamera->setFarClip(modelViewCamDist * 1000.0f);
    // Camera position set per-frame via orbit update in drawModelViewer
}

void PanelInspector::applyDefaultMaterial(kMesh *mesh, kShader *defaultShader)
{
    if (!mesh || !defaultShader)
        return;
    kMaterial *mat = mesh->getMaterial();
    if (!mat || !mat->getShader() || mat->getShader()->getShaderProgram() == 0)
    {
        kMaterial *def = new kMaterial();
        def->setShader(defaultShader);
        def->setDiffuseColor(kVec3(1.0f));
        def->setAmbientColor(kVec3(1.0f));
        def->setSpecularColor(kVec3(1.0f));
        def->setShininess(32.0f);
        mesh->setMaterial(def, false); // false = don't override children
        modelViewDefaultMats.push_back(def);
    }
    for (kObject *child : mesh->getChildren())
        if (child->getType() == kNodeType::NODE_TYPE_MESH)
            applyDefaultMaterial(static_cast<kMesh *>(child), defaultShader);
}

void PanelInspector::drawModelViewer(const PanelProject::SelectedProjectAsset &asset)
{
    if (!modelViewRenderer)
    {
        modelViewRenderer = new kOffscreenRenderer(256, 256);
        modelViewRenderer->setAssetManager(manager->getAssetManager());
    }

    initModelViewScene();

    // Reload mesh when selection changes
    if (asset.uuid != modelViewUuid)
    {
        modelViewUuid = asset.uuid;
        modelViewLightEnabled = false;
        modelViewRotX = 24.09f;
        modelViewRotY = 26.57f;
        modelViewLightYaw = 45.0f;
        modelViewLightPitch = 60.0f;
        isDraggingMVLight = false;
        isDraggingMVModel = false;

        if (modelViewMesh && modelViewScene)
            modelViewScene->removeMesh(modelViewMesh);
        modelViewMesh = nullptr;
        for (auto *m : modelViewDefaultMats)
            delete m;
        modelViewDefaultMats.clear();

        if (!asset.uuid.empty() && manager->getAssetManager())
        {
            fs::path glbPath = manager->projectPath / "Library" / "ImportedAssets" / (asset.uuid + ".glb");
            if (fs::exists(glbPath))
            {
                kAssetManager *am = manager->getAssetManager();
                modelViewMesh = am->loadMesh(glbPath.generic_string());
                if (modelViewMesh)
                {
                    kShader *defShader = am->loadGlslFromResource("SHADER_MESH_PHONG");
                    applyDefaultMaterial(modelViewMesh, defShader);
                    modelViewScene->addMesh(modelViewMesh);
                    frameModelViewCamera();
                }
            }
        }
    }

    // Orbit camera around modelViewCenter — mesh stays fixed at its loaded transform
    if (modelViewCamera)
    {
        float pr = glm::radians(modelViewRotX);
        float yr = glm::radians(modelViewRotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr),
                     std::sin(pr),
                     std::cos(pr) * std::cos(yr));
        modelViewCamera->setPosition(modelViewCenter + camDir * modelViewCamDist);
        modelViewCamera->setLookAt(modelViewCenter);
    }

    updateModelViewLight();

    // Render offscreen
    if (modelViewMesh && modelViewCamera && modelViewRenderer)
    {
        modelViewRenderer->setBackgroundColor(kVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1.0f));
        if (modelViewLightEnabled)
            modelViewRenderer->render(modelViewWorld, modelViewScene, modelViewCamera);
        else
            modelViewRenderer->renderMesh(modelViewMesh, modelViewCamera);
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize;
    float sz = std::min(std::max(avail, 1.0f), 256.0f);
    float ox = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (modelViewMesh && modelViewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)modelViewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                     ImVec2(0, 1), ImVec2(1, 0));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        const char *msg = "No preview available";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                           screenPos.y + (sz - ts.y) * 0.5f),
                    IM_COL32(110, 115, 140, 200), msg);
    }

    // InvisibleButton captures mouse for both LMB and RMB drag
    ImGui::InvisibleButton("##modelViewer", ImVec2(sz, sz),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    bool pvHovered = ImGui::IsItemHovered();
    if (pvHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle (top-right corner)
    // -----------------------------------------------------------------------
    const float btR = 11.0f;
    const float btPad = 7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool btHover = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    ImU32 btFg = modelViewLightEnabled
                     ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60, 220))
                     : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80, 80, 95, 200));
    dl->AddCircleFilled(btCenter, btR, IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float bx = btCenter.x, by = btCenter.y;
    ImU32 bulbInk = IM_COL32(25, 18, 5, modelViewLightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        modelViewLightEnabled = !modelViewLightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", modelViewLightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Scroll wheel = zoom (dolly the orbit camera toward/away from the pivot)
    // -----------------------------------------------------------------------
    float wheel = ImGui::GetIO().MouseWheel;
    if (pvHovered && wheel != 0.0f && modelViewCamera)
    {
        // Scale by distance so zooming feels consistent for both tiny and large meshes.
        modelViewCamDist -= wheel * modelViewCamDist * 0.1f;
        modelViewCamDist = std::clamp(modelViewCamDist, 0.05f, 1000.0f);

        // The frame pass tightly fits the near/far clips to the framed distance.
        // Widen them after zooming so the mesh isn't clipped when dollied out.
        modelViewCamera->setNearClip(std::max(0.0001f, modelViewCamDist * 0.01f));
        modelViewCamera->setFarClip(modelViewCamDist * 1000.0f);
    }

    // -----------------------------------------------------------------------
    // Left-click drag = rotate model
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !btHover && !isDraggingMVModel)
        isDraggingMVModel = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        isDraggingMVModel = false;

    if (isDraggingMVModel)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        modelViewRotY -= delta.x * 0.5f;
        modelViewRotX += delta.y * 0.5f;
        modelViewRotX = std::clamp(modelViewRotX, -89.0f, 89.0f);
    }

    // -----------------------------------------------------------------------
    // Right-click drag = move light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !isDraggingMVLight)
        isDraggingMVLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingMVLight = false;

    if (isDraggingMVLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        modelViewLightYaw += delta.x * 0.015f;
        modelViewLightPitch -= delta.y * 0.015f;
        modelViewLightPitch = std::clamp(modelViewLightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();
}

// ---------------------------------------------------------------------------
// Material viewer
// ---------------------------------------------------------------------------

void PanelInspector::initMatViewScene()
{
    if (matViewScene)
        return;

    matViewWorld = createWorld(createAssetManager());
    matViewScene = matViewWorld->createScene("matViewer");
    matViewScene->setFrustumCullingEnabled(false);
    // No shadows in the preview — the single-mesh thumbnail doesn't benefit
    // from a cast shadow, and skipping the depth pass keeps lit (Phong/PBR)
    // materials rendering correctly in the preview window.
    matViewScene->setShadowsEnabled(false);
    matViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    matViewLight = matViewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    matViewLight->setPower(1.5f);

    matViewMesh = kMeshGenerator::generateSphere(1.0f, 32, 32);
    matViewScene->addMesh(matViewMesh);

    matViewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    matViewCamera->setFOV(45.0f);
    matViewCamera->setAspectRatio(1.0f);
    matViewCamera->setNearClip(0.1f);
    matViewCamera->setFarClip(100.0f);
    matViewCamera->setLookAt(kVec3(0.0f));
    matViewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updateMatViewLight()
{
    if (!matViewLight || !matViewScene)
        return;
    if (!matViewLightEnabled)
    {
        matViewLight->setPower(0.0f);
        matViewScene->setAmbientLightColor(kVec3(0.5f, 0.5f, 0.5f));
        return;
    }
    matViewLight->setPower(1.5f);
    matViewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
    matViewLight->setRotation(kVec3(matViewLightPitch, matViewLightYaw, 0.0f));
}

void PanelInspector::rebuildMatViewMaterial(const nlohmann::json &matJson)
{
    delete matViewMat; // inspector owns the preview material (shader/textures are shared)
    matViewMat = nullptr;
    if (!matViewMesh)
        return;
    if (!matJson.is_object())
        return;

    matViewMat = manager->buildMaterialFromJson(matJson);
    if (matViewMat)
        matViewMesh->setMaterial(matViewMat);
}

void PanelInspector::drawMaterialViewer(const PanelProject::SelectedProjectAsset &asset)
{
    if (!matViewRenderer)
    {
        matViewRenderer = new kOffscreenRenderer(256, 256);
        matViewRenderer->setAssetManager(manager->getAssetManager());
    }

    initMatViewScene();

    // Rebuild when the inspector's live JSON changes (UUID change or any property edit)
    bool uuidChanged = (matInspUuid != matViewUuid);
    if (uuidChanged)
    {
        matViewLightYaw = 45.0f;
        matViewLightPitch = 60.0f;
        matViewLightEnabled = true;
        isDraggingMatLight = false;
        matViewRotX = 6.0f;
        matViewRotY = 0.0f;
        isDraggingMatModel = false;
        matViewUuid = matInspUuid;
    }
    if ((uuidChanged || matInspVersion != matViewVersion) && !matInspUuid.empty())
    {
        matViewVersion = matInspVersion;
        rebuildMatViewMaterial(matInspJson);
    }

    updateMatViewLight();

    // Orbit camera around the preview sphere (LMB drag updates matViewRot*).
    if (matViewCamera)
    {
        float pr = glm::radians(matViewRotX);
        float yr = glm::radians(matViewRotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr),
                     std::sin(pr),
                     std::cos(pr) * std::cos(yr));
        matViewCamera->setPosition(camDir * matViewCamDist);
        matViewCamera->setLookAt(kVec3(0.0f));
    }

    if (matViewMat && matViewScene && matViewCamera && matViewRenderer)
    {
        matViewRenderer->setBackgroundColor(kVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1.0f));
        matViewRenderer->render(matViewWorld, matViewScene, matViewCamera);
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize;
    float sz = std::min(std::max(avail, 1.0f), 256.0f);
    float ox = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (matViewMat && matViewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)matViewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        const char *msg = "No preview available";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                           screenPos.y + (sz - ts.y) * 0.5f),
                    IM_COL32(110, 115, 140, 200), msg);
    }

    ImGui::InvisibleButton("##matViewer", ImVec2(sz, sz),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    bool pvHovered = ImGui::IsItemHovered();
    if (pvHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle (top-right corner)
    // -----------------------------------------------------------------------
    const float btR = 11.0f;
    const float btPad = 7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool btHover = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    ImU32 btFg = matViewLightEnabled
                     ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60, 220))
                     : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80, 80, 95, 200));
    dl->AddCircleFilled(btCenter, btR, IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float bx = btCenter.x, by = btCenter.y;
    ImU32 bulbInk = IM_COL32(25, 18, 5, matViewLightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        matViewLightEnabled = !matViewLightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", matViewLightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Left-click drag = orbit the camera around the sphere
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !btHover && !isDraggingMatModel)
        isDraggingMatModel = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        isDraggingMatModel = false;

    if (isDraggingMatModel)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        matViewRotY -= delta.x * 0.5f;
        matViewRotX -= delta.y * 0.5f; // drag down tilts the view down (was inverted)
        matViewRotX = std::clamp(matViewRotX, -89.0f, 89.0f);
    }

    // -----------------------------------------------------------------------
    // Right-click drag = move light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !btHover && !isDraggingMatLight)
        isDraggingMatLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingMatLight = false;

    if (isDraggingMatLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        matViewLightYaw += delta.x * 0.015f;
        matViewLightPitch -= delta.y * 0.015f;
        matViewLightPitch = std::clamp(matViewLightPitch, -89.0f, 89.0f);
    }

    gui->spacing();
    gui->separator();
    gui->spacing();
}

ImTextureRef PanelInspector::getFileTypeIcon(const kString &fileType) const
{
    if (fileType == "mesh")
        return iconFileModel;
    if (fileType == "image")
        return iconFileImage;
    if (fileType == "material")
        return iconFileMaterial;
    if (fileType == "prefab")
        return iconFilePrefab;
    if (fileType == "audio")
        return iconFileAudio;
    if (fileType == "video")
        return iconFileVideo;
    if (fileType == "script")
        return iconFileScript;
    if (fileType == "text")
        return iconFileText;
    if (fileType == "world")
        return iconFileWorld;
    return iconFileOther;
}

static void drawInlineIcon(ImTextureRef icon, const char *tooltip = nullptr, float size = 16.0f)
{
    if (icon == nullptr)
        return;
    float pad = (ImGui::GetFrameHeight() - size) * 0.5f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
    ImGui::Image(icon, ImVec2(size, size));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - pad);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}

// ---------------------------------------------------------------------------
// Table helpers
// ---------------------------------------------------------------------------

static bool beginPropTable(kGuiManager *gui, const char *id)
{
    if (!gui->tableStart(id, 2, ImGuiTableFlags_SizingStretchProp))
        return false;
    gui->tableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    gui->tableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

static void propLabel(kGuiManager *gui, const char *label)
{
    gui->tableNextRow();
    gui->tableSetColumnIndex(0);
    gui->alignTextToFramePadding();
    gui->text(label);
    if (gui->isItemHovered() && gui->calcTextSize(label).x > gui->getColumnWidth())
    {
        gui->beginTooltip();
        gui->textUnformatted(label);
        gui->endTooltip();
    }
    gui->tableSetColumnIndex(1);
    gui->setNextItemWidth(-FLT_MIN);
}

// Collapsing section header whose title turns orange when the section's
// properties differ from the prefab template (dirty).
static bool sectionHeader(kGuiManager *gui, const char *label, bool dirty)
{
    if (dirty)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.0f, 1.0f)); // yellow/orange
    bool open = gui->collapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    if (dirty)
        ImGui::PopStyleColor();
    return open;
}

// ---------------------------------------------------------------------------
// Transform section
// ---------------------------------------------------------------------------
static void drawTransformSection(kGuiManager *gui, kObject *obj, Manager *mgr)
{
    if (!gui->collapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    bool isStatic = obj->getStatic();

    // Show a hint when the object is static — its transform can still be edited
    // in the editor for authoring, but runtime scripts/physics cannot move it.

    if (!beginPropTable(gui, "TfmTable"))
        return;

    // --- Position ---
    {
        static kVec3 s_posBefore;

        kVec3 pos = obj->getPosition();
        kVec3 posPreEdit = pos;
        float p[3] = {pos.x, pos.y, pos.z};

        propLabel(gui, "Position");
        if (gui->dragFloat3("##Pos", p, 0.1f))
        {
            // Use force-setter so the editor can always author the transform.
            obj->setPositionForced(kVec3(p[0], p[1], p[2]));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_posBefore = posPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getPosition();
            kVec3 before = s_posBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setPositionForced(before); },
                [cap, after]()
                { cap->setPositionForced(after); }));
        }
    }

    // --- Rotation ---
    {
        static kVec3 s_rotBefore;

        kVec3 euler = obj->getRotationEuler();
        kVec3 eulerPreEdit = euler;
        float r[3] = {euler.x, euler.y, euler.z};
        for (int i = 0; i < 3; i++)
            if (r[i] == 0.0f)
                r[i] = 0.0f;

        propLabel(gui, "Rotation");
        if (gui->dragFloat3("##Rot", r, 0.5f))
        {
            // Use force-setter so the editor can always author the transform.
            obj->setRotationForced(kQuat(glm::radians(kVec3(r[0], r[1], r[2]))));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_rotBefore = eulerPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getRotationEuler();
            kVec3 before = s_rotBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setRotationForced(kQuat(glm::radians(before))); },
                [cap, after]()
                { cap->setRotationForced(kQuat(glm::radians(after))); }));
        }
    }

    // --- Scale ---
    {
        static kVec3 s_scaleBefore;

        kVec3 scl = obj->getScale();
        kVec3 sclPreEdit = scl;
        float s[3] = {scl.x, scl.y, scl.z};

        propLabel(gui, "Scale");
        if (gui->dragFloat3("##Scl", s, 0.01f, 0.001f, 10000.0f))
        {
            // Use force-setter so the editor can always author the transform.
            obj->setScaleForced(kVec3(s[0], s[1], s[2]));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
        if (gui->isItemActivated())
            s_scaleBefore = sclPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = obj->getScale();
            kVec3 before = s_scaleBefore;
            kObject *cap = obj;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setScaleForced(before); },
                [cap, after]()
                { cap->setScaleForced(after); }));
        }
    }

    gui->tableEnd();
}

// ---------------------------------------------------------------------------
// Decal section
// ---------------------------------------------------------------------------
static void drawDecalSection(kGuiManager *gui, kDecal *decal, Manager *mgr, bool isDirtyPrefab = false)
{
    if (!sectionHeader(gui, "Decal", isDirtyPrefab))
        return;

    if (!beginPropTable(gui, "DecalTable"))
        return;

    // --- Shader type (built-in Flat / PBR / Phong) ---------------------------
    // Sets the decal's material to a fresh built-in material of the selected
    // type. The choice is persisted on the object so it can be rebuilt on load.
    {
        propLabel(gui, "Shader");

        const char *shaderItems[] = {"Flat", "PBR", "Phong"};
        std::string shaderType = decal->getShaderType();
        int shaderSel = 0;
        if (shaderType == "pbr")       shaderSel = 1;
        else if (shaderType == "phong") shaderSel = 2;

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##DecalShader", &shaderSel, shaderItems, 3))
        {
            const char *newType = (shaderSel == 1) ? "pbr" : ((shaderSel == 2) ? "phong" : "flat");
            std::vector<MaterialSnapshot> before = mgr->captureMaterialSubtree(decal);
            if (mgr->applyDecalShaderType(decal, newType))
            {
                auto cmd = std::make_unique<MaterialCommand>();
                cmd->manager = mgr;
                cmd->before = before;
                cmd->after = mgr->captureMaterialSubtree(decal);
                mgr->undoRedo.push(std::move(cmd));
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }
        }
    }

    // --- Surface offset ------------------------------------------------------
    // How far the flat quad floats above its pivot (along the face normal) so
    // the sticker does not z-fight with the surface it is stamped on.
    {
        propLabel(gui, "Surface Offset");
        float offset = decal->getSurfaceOffset();
        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat("##DecalOffset", &offset, 0.001f, 0.0f, 1.0f, "%.3f"))
        {
            float before = decal->getSurfaceOffset();
            float after = offset;
            decal->setSurfaceOffset(after);
            kDecal *cap = decal;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSurfaceOffset(before); },
                [cap, after]()
                { cap->setSurfaceOffset(after); }));
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }
    }

    // --- Material picker -----------------------------------------------------
    // The decal's appearance comes from its material. Pick a .mat whose albedo
    // map carries the decal artwork; an "Unlit" material with alpha blending is
    // recommended so the sticker ignores scene lighting.
    {
        propLabel(gui, "Material");

        std::vector<std::string> matUuids = {""}; // index 0 = "(None)"
        std::vector<std::string> matNames = {"(None)"};
        for (const auto &kv : mgr->fileMap)
        {
            if (kv.second.type == "material")
            {
                matUuids.push_back(kv.first);
                matNames.push_back(fs::path(kv.second.path).stem().string());
            }
        }
        std::vector<const char *> matNamePtrs;
        matNamePtrs.reserve(matNames.size());
        for (auto &n : matNames)
            matNamePtrs.push_back(n.c_str());

        // Current selection comes from the object's stored material UUID.
        std::string assignedUuid = decal->getMaterialUuid();
        int current = 0;
        for (size_t i = 0; i < matUuids.size(); ++i)
            if (matUuids[i] == assignedUuid)
            {
                current = (int)i;
                break;
            }

        auto applyByIndex = [&](int idx)
        {
            if (idx < 0 || idx >= (int)matUuids.size())
                return;
            std::vector<MaterialSnapshot> before = mgr->captureMaterialSubtree(decal);
            bool ok = false;
            if (idx == 0)
            {
                // "(None)" — drop the .mat and rebuild the built-in material of
                // the currently selected shader type (Flat/PBR/Phong) so the
                // decal stays visible.
                ok = mgr->applyDecalShaderType(decal, decal->getShaderType());
            }
            else
            {
                auto it = mgr->fileMap.find(matUuids[idx]);
                if (it == mgr->fileMap.end())
                    return;
                fs::path matPath = mgr->projectPath / "Assets" / it->second.path;
                ok = mgr->applyMaterialToObject(decal, matPath, matUuids[idx]);
            }
            if (ok)
            {
                auto cmd = std::make_unique<MaterialCommand>();
                cmd->manager = mgr;
                cmd->before = before;
                cmd->after = mgr->captureMaterialSubtree(decal);
                mgr->undoRedo.push(std::move(cmd));
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }
        };

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##DecalMaterial", &current, matNamePtrs.data(), (int)matNamePtrs.size()))
            applyByIndex(current);

        // Accept a material asset dropped directly onto the combo.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
            {
                std::string dropped((const char *)payload->Data);
                {
                    auto nl = dropped.find('\n');
                    if (nl != std::string::npos)
                        dropped = dropped.substr(0, nl);
                }
                auto it = mgr->fileMap.find(dropped);
                if (it != mgr->fileMap.end() && it->second.type == "material")
                {
                    for (size_t i = 0; i < matUuids.size(); ++i)
                        if (matUuids[i] == dropped)
                        {
                            applyByIndex((int)i);
                            break;
                        }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    gui->tableEnd();

    gui->spacing();
    gui->textDisabled("Renders as a flat quad.");
    gui->textDisabled("Rotate to face the surface, scale to size it.");
    gui->spacing();
}

// ---------------------------------------------------------------------------
// Mesh section
// ---------------------------------------------------------------------------
static void drawMeshSection(kGuiManager *gui, kMesh *mesh, Manager *mgr, PanelTerrain *panelTerrain, bool isDirtyPrefab = false)
{
    // Geometry-less mesh nodes — e.g. the empty root of an imported multi-mesh
    // model, whose geometry lives in its sub-meshes — carry no drawable data, so
    // a File / Material / shadow UI on them is meaningless. Skip the section.
    if (mesh->getVertexCount() <= 0)
        return;

    // Determine if this mesh belongs to a terrain tile.
    // kTerrain objects have type NODE_TYPE_TERRAIN.
    // kMesh-based terrain tiles have serializeType == "terrain".
    bool isTerrain = (mesh->getType() == kNodeType::NODE_TYPE_TERRAIN) ||
                     (mesh->getSerializeType() == "terrain");

    const char *header = isTerrain ? "Terrain" : "Mesh";
    if (!sectionHeader(gui, header, isDirtyPrefab))
        return;

    if (!beginPropTable(gui, "MeshTable"))
        return;

    propLabel(gui, "File");
    gui->beginDisabled(true);
    char fileBuf[256];
    strncpy_s(fileBuf, sizeof(fileBuf), mesh->getFileName().c_str(), _TRUNCATE);
    ImGui::InputText("##MeshFile", fileBuf, sizeof(fileBuf));
    gui->endDisabled();

    // --- Material picker -----------------------------------------------------
    // Lists project .mat assets; selecting one applies it to this mesh (same
    // runtime effect as dragging a material onto the object in the viewport).
    // Also accepts a material dragged onto the field. The assignment is stored
    // as the material asset's UUID on the object, so it persists across save/load.
    {
        propLabel(gui, "Material");

        std::vector<std::string> matUuids = {""}; // index 0 = "(None)"
        std::vector<std::string> matNames = {"(None)"};
        for (const auto &kv : mgr->fileMap)
        {
            if (kv.second.type == "material")
            {
                matUuids.push_back(kv.first);
                matNames.push_back(fs::path(kv.second.path).stem().string());
            }
        }
        std::vector<const char *> matNamePtrs;
        matNamePtrs.reserve(matNames.size());
        for (auto &n : matNames)
            matNamePtrs.push_back(n.c_str());

        // Current selection comes from the object's stored material UUID.
        std::string assignedUuid = mesh->getMaterialUuid();
        int current = 0;
        for (size_t i = 0; i < matUuids.size(); ++i)
            if (matUuids[i] == assignedUuid)
            {
                current = (int)i;
                break;
            }

        auto applyByIndex = [&](int idx)
        {
            if (idx < 0 || idx >= (int)matUuids.size())
                return;
            // Snapshot the whole subtree so undo restores per-part materials of
            // multi-mesh models, not just the root.
            std::vector<MaterialSnapshot> before = mgr->captureMaterialSubtree(mesh);
            bool ok = false;
            if (idx == 0)
            {
                // "(None)" — revert the object to a fresh default material so its
                // appearance visibly changes back (and clear the stored UUID).
                mgr->applyDefaultMaterialToObject(mesh);
                ok = true;
            }
            else
            {
                auto it = mgr->fileMap.find(matUuids[idx]);
                if (it == mgr->fileMap.end())
                    return;
                fs::path matPath = mgr->projectPath / "Assets" / it->second.path;
                ok = mgr->applyMaterialToObject(mesh, matPath, matUuids[idx]);
            }
            if (ok)
            {
                auto cmd = std::make_unique<MaterialCommand>();
                cmd->manager = mgr;
                cmd->before = before;
                cmd->after = mgr->captureMaterialSubtree(mesh);
                mgr->undoRedo.push(std::move(cmd));
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }
        };

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##MeshMaterial", &current, matNamePtrs.data(), (int)matNamePtrs.size()))
            applyByIndex(current);

        // Accept a material asset dropped directly onto the combo.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
            {
                std::string dropped((const char *)payload->Data);
                {
                    auto nl = dropped.find('\n');
                    if (nl != std::string::npos)
                        dropped = dropped.substr(0, nl);
                }
                auto it = mgr->fileMap.find(dropped);
                if (it != mgr->fileMap.end() && it->second.type == "material")
                {
                    for (size_t i = 0; i < matUuids.size(); ++i)
                        if (matUuids[i] == dropped)
                        {
                            applyByIndex((int)i);
                            break;
                        }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    propLabel(gui, "Cast Shadow");
    bool castShadow = mesh->getCastShadow();
    bool prevCastShadow = castShadow;
    if (gui->checkbox("##CastShadow", &castShadow))
    {
        mesh->setCastShadow(castShadow);
        kMesh *cap = mesh;
        bool after = castShadow;
        bool before = prevCastShadow;
        mgr->undoRedo.push(std::make_unique<PropertyCommand>(
            [cap, before]()
            { cap->setCastShadow(before); },
            [cap, after]()
            { cap->setCastShadow(after); }));
    }

    propLabel(gui, "Receive Shadow");
    bool receiveShadow = mesh->getReceiveShadow();
    bool prevReceiveShadow = receiveShadow;
    if (gui->checkbox("##ReceiveShadow", &receiveShadow))
    {
        mesh->setReceiveShadow(receiveShadow);
        kMesh *cap = mesh;
        bool after = receiveShadow;
        bool before = prevReceiveShadow;
        mgr->undoRedo.push(std::make_unique<PropertyCommand>(
            [cap, before]()
            { cap->setReceiveShadow(before); },
            [cap, after]()
            { cap->setReceiveShadow(after); }));
    }

    gui->tableEnd();

    // --- Terrain tools (only for terrain meshes) ---
    if (isTerrain && panelTerrain)
    {
        gui->spacing();
        gui->separator();
        gui->spacing();

        auto &sculpt = panelTerrain->sculpt;
        auto &paint = panelTerrain->paint;

        // --- Sculpt toggle button (styled like the Paint Height button) ---
        {
            bool sActive = sculpt.active;
            if (sActive)
            {
                gui->pushStyleColor(ImGuiCol_Button, kVec4(0.44f, 0.67f, 0.39f, 1.0f));
                gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.35f, 0.54f, 0.31f, 1.0f));
            }
            if (gui->button("Sculpt", kIvec2(120, 30)))
            {
                sculpt.active = !sActive;
                if (sculpt.active)
                {
                    paint.active = false;
                    paint.heightActive = false;
                    paint.channel = -1;
                }
            }
            if (sActive)
                gui->popStyleColor(2);

            if (sculpt.active)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Sculpt active");
            }
        }

        // Brush settings (only editable when sculpt is active)
        if (sculpt.active)
        {
            const char *modes[] = {"Raise", "Lower", "Flatten", "Smooth"};
            int currentMode = static_cast<int>(sculpt.mode);
            if (ImGui::Combo("Brush Mode", &currentMode, modes, 4))
            {
                sculpt.mode = static_cast<kTerrain::BrushMode>(currentMode);
                sculpt.affectedTile = nullptr;
            }
            ImGui::SliderFloat("Brush Radius", &sculpt.radius, 0.5f, 50.0f, "%.1f");
            ImGui::SliderFloat("Brush Strength", &sculpt.strength, 0.01f, 1.0f, "%.2f");
            if (sculpt.mode == kTerrain::BrushMode::Flatten)
                ImGui::Text("Flatten target: %.2f (sampled on first click)", sculpt.flattenHeight);
        }

        gui->spacing();
        gui->separator();
        gui->spacing();

        // --- Splat Mask Paint (R/G/B/A) ---
        gui->text("Mask Paint Channel:");
        const char *chLabels[] = {"R", "G", "B", "A"};

        for (int i = 0; i < 4; ++i)
        {
            bool isActive = (paint.active && paint.channel == i);
            if (isActive)
            {
                switch (i)
                {
                case 0:
                    gui->pushStyleColor(ImGuiCol_Button, kVec4(0.86f, 0.24f, 0.24f, 1.0f));
                    gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.69f, 0.19f, 0.19f, 1.0f));
                    break;
                case 1:
                    gui->pushStyleColor(ImGuiCol_Button, kVec4(0.24f, 0.86f, 0.24f, 1.0f));
                    gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.19f, 0.69f, 0.19f, 1.0f));
                    break;
                case 2:
                    gui->pushStyleColor(ImGuiCol_Button, kVec4(0.24f, 0.24f, 0.86f, 1.0f));
                    gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.19f, 0.19f, 0.69f, 1.0f));
                    break;
                case 3:
                    gui->pushStyleColor(ImGuiCol_Button, kVec4(0.71f, 0.71f, 0.71f, 1.0f));
                    gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.56f, 0.56f, 0.56f, 1.0f));
                    break;
                }
            }

            if (gui->button(chLabels[i], kIvec2(40, 30)))
            {
                if (isActive)
                {
                    paint.active = false;
                    paint.channel = -1;
                }
                else
                {
                    paint.active = true;
                    paint.heightActive = false;
                    sculpt.active = false;
                    paint.channel = i;
                }
            }

            if (isActive)
                gui->popStyleColor(2);

            if (i < 3)
                gui->sameLine();
        }

        if (paint.active && paint.channel >= 0)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                               "Painting mask on channel %s", chLabels[paint.channel]);
        }

        gui->sameLine();
        // --- Height Paint toggle button ---
        {
            bool hActive = paint.heightActive;
            if (hActive)
            {
                gui->pushStyleColor(ImGuiCol_Button, kVec4(0.44f, 0.67f, 0.39f, 1.0f));
                gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.35f, 0.54f, 0.31f, 1.0f));
            }
            if (gui->button("Paint Height", kIvec2(120, 30)))
            {
                paint.heightActive = !hActive;
                if (paint.heightActive)
                {
                    paint.active = false;
                    paint.channel = -1;
                    sculpt.active = false;
                }
            }
            if (hActive)
                gui->popStyleColor(2);
        }

        if (paint.heightActive)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Height paint active");
            ImGui::TextDisabled("  Left click = lower (dark)");
            ImGui::TextDisabled("  Right click = raise (light)");
        }

        if (!sculpt.active && !paint.active && !paint.heightActive)
        {
            ImGui::TextDisabled("  Click Sculpt, R/G/B/A mask, or Paint Height");
        }

        gui->spacing();

        // Common paint controls (strength & fade)
        if (gui->dragFloat("Strength", &paint.strength, 0.01f, 0.0f, 5.0f, "%.2f"))
            paint.strength = glm::max(0.0f, paint.strength);

        if (gui->dragFloat("Fade", &paint.fade, 0.01f, 0.0f, 1.0f, "%.2f"))
            paint.fade = glm::clamp(paint.fade, 0.0f, 1.0f);

        // Mask texture selection (optional)
        gui->text("Paint Mask (optional):");
        static char maskBuf[512] = {0};
        if (paint.maskTexturePath.empty() && maskBuf[0] != 0)
            maskBuf[0] = 0;
        if (!paint.maskTexturePath.empty() && maskBuf[0] == 0)
            strncpy_s(maskBuf, sizeof(maskBuf), paint.maskTexturePath.c_str(), _TRUNCATE);

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##PaintMask", maskBuf, sizeof(maskBuf)))
            paint.maskTexturePath = maskBuf;

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload =
                    ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
            {
                std::string dropped((const char *)payload->Data);
                {
                    auto nl = dropped.find('\n');
                    if (nl != std::string::npos)
                        dropped = dropped.substr(0, nl);
                }
                auto it = mgr->fileMap.find(dropped);
                if (it != mgr->fileMap.end() && it->second.type == "image")
                {
                    paint.maskTexturePath = it->second.path;
                    strncpy_s(maskBuf, sizeof(maskBuf), it->second.path.c_str(), _TRUNCATE);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ── Tile Expansion ───────────────────────────────────────────────────
        gui->spacing();
        gui->separator();
        gui->spacing();

        // Find the kTerrain that owns this mesh by scanning the terrain manager.
        kTerrain *ownerTerrain = nullptr;
        if (mgr->terrainManager)
        {
            for (const auto &pair : mgr->terrainManager->getTiles())
            {
                kTerrain *tile = pair.second.get();
                if (tile && tile->getMesh() == mesh)
                {
                    ownerTerrain = tile;
                    break;
                }
            }
        }

        // Helper lambda: expand in a given direction (create + fill + load).
        auto expandInDir = [mgr, ownerTerrain](int gx, int gz, const char *dir)
        {
            if (!mgr->terrainManager || !ownerTerrain)
                return;
            int nx = gx, nz = gz;
            if (strcmp(dir, "north") == 0)   nz += 1;
            else if (strcmp(dir, "south") == 0) nz -= 1;
            else if (strcmp(dir, "east") == 0)  nx += 1;
            else if (strcmp(dir, "west") == 0)  nx -= 1;

            kTerrain *tile = mgr->terrainManager->createTile(nx, nz);
            if (tile)
            {
                // Load immediately so it appears in the viewport
                if (!tile->isLoaded())
                {
                    tile->reload(true);
                    // Also register the mesh with the scene for rendering
                    if (mgr->getRenderer())
                        mgr->getRenderer()->setOctreeDirty();
                }
            }
        };

        if (ownerTerrain)
        {
            int gx = ownerTerrain->getGridX();
            int gz = ownerTerrain->getGridZ();
            ImGui::Text("Tile (%d, %d)", gx, gz);

            gui->spacing();

            // 2×3 layout:     [+Z] [spacer] [+X]
            //                  [-Z] [spacer] [-X]
            float btnW = 60.0f;

            // Row 1
            if (gui->button("+Z##tn", kIvec2(btnW, 28)))
                expandInDir(gx, gz, "north");
            gui->sameLine();
            gui->text("     ");
            gui->sameLine();
            if (gui->button("+X##te", kIvec2(btnW, 28)))
                expandInDir(gx, gz, "east");

            // Row 2
            if (gui->button("-Z##ts", kIvec2(btnW, 28)))
                expandInDir(gx, gz, "south");
            gui->sameLine();
            gui->text("     ");
            gui->sameLine();
            if (gui->button("-X##tw", kIvec2(btnW, 28)))
                expandInDir(gx, gz, "west");

            gui->spacing();
        }
        else
        {
            gui->textDisabled("Expand: (tile not found in manager)");
        }
    }
}

// ---------------------------------------------------------------------------
// Camera section
// ---------------------------------------------------------------------------
static void drawCameraSection(kGuiManager *gui, kCamera *cam, Manager *mgr, bool isDirtyPrefab = false)
{
    if (!sectionHeader(gui, "Camera", isDirtyPrefab))
        return;

    if (!beginPropTable(gui, "CamTable"))
        return;

    // FOV
    {
        static float s_fovBefore = 0.0f;
        float fov = cam->getFOV();
        float fovPreEdit = fov;
        propLabel(gui, "FOV");
        if (gui->dragFloat("##FOV", &fov, 0.5f, 1.0f, 179.0f, "%.1f deg"))
            cam->setFOV(fov);
        if (gui->isItemActivated())
            s_fovBefore = fovPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getFOV();
            float before = s_fovBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setFOV(before); },
                [cap, after]()
                { cap->setFOV(after); }));
        }
    }

    // Near Clip
    {
        static float s_nearBefore = 0.0f;
        float nearClip = cam->getNearClip();
        float nearPreEdit = nearClip;
        propLabel(gui, "Near Clip");
        if (gui->dragFloat("##Near", &nearClip, 0.01f, 0.001f, 10000.0f, "%.3f"))
            cam->setNearClip(nearClip);
        if (gui->isItemActivated())
            s_nearBefore = nearPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getNearClip();
            float before = s_nearBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setNearClip(before); },
                [cap, after]()
                { cap->setNearClip(after); }));
        }
    }

    // Far Clip
    {
        static float s_farBefore = 0.0f;
        float farClip = cam->getFarClip();
        float farPreEdit = farClip;
        propLabel(gui, "Far Clip");
        if (gui->dragFloat("##Far", &farClip, 1.0f, 0.1f, 100000.0f, "%.1f"))
            cam->setFarClip(farClip);
        if (gui->isItemActivated())
            s_farBefore = farPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = cam->getFarClip();
            float before = s_farBefore;
            kCamera *cap = cam;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setFarClip(before); },
                [cap, after]()
                { cap->setFarClip(after); }));
        }
    }

    // Scene selector
    {
        kWorld *world = mgr->getWorld();
        const auto &scenes = world ? world->getScenes() : std::vector<kScene *>{};

        std::vector<kScene *> gameScenes;
        for (size_t i = 1; i < scenes.size(); ++i)
            gameScenes.push_back(scenes[i]);

        std::vector<std::string> sceneNames;
        sceneNames.push_back("(Auto)");
        for (kScene *s : gameScenes)
            sceneNames.push_back(s->getName().empty() ? "(unnamed)" : s->getName());

        int curIdx = 0;
        for (int i = 0; i < (int)gameScenes.size(); ++i)
            if (gameScenes[i]->getUuid() == cam->getSceneUuid())
            {
                curIdx = i + 1;
                break;
            }

        std::vector<const char *> scenePtrs;
        for (auto &s : sceneNames)
            scenePtrs.push_back(s.c_str());

        propLabel(gui, "Scene");
        if (ImGui::Combo("##CamScene", &curIdx, scenePtrs.data(), (int)scenePtrs.size()))
            cam->setSceneUuid(curIdx == 0 ? "" : gameScenes[curIdx - 1]->getUuid());
    }

    gui->tableEnd();

    // Set / unset this camera as the default for the Game panel
    gui->spacing();
    bool isDefault = (mgr->defaultGameCamera == cam);
    if (isDefault)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }
    if (gui->button(isDefault ? "Default Camera" : "Set as Default", kIvec2(-1, 0)))
        mgr->defaultGameCamera = isDefault ? nullptr : cam;
    if (isDefault)
        gui->popStyleColor(2);
    if (gui->isItemHovered())
        gui->setItemTooltip(isDefault
                                ? "This is the active Game panel camera.\nClick to unset."
                                : "Use this camera as the default in the Game panel.");
}

// ---------------------------------------------------------------------------
// Light section
// ---------------------------------------------------------------------------
static void drawLightSection(kGuiManager *gui, kLight *light, Manager *mgr, bool isDirtyPrefab = false)
{
    kLightType lt = light->getLightType();

    const char *header = "Light";
    if (lt == LIGHT_TYPE_SUN)
        header = "Light (Sun)";
    else if (lt == LIGHT_TYPE_POINT)
        header = "Light (Point)";
    else if (lt == LIGHT_TYPE_SPOT)
        header = "Light (Spot)";

    if (!sectionHeader(gui, header, isDirtyPrefab))
        return;

    if (!beginPropTable(gui, "LightTable"))
        return;

    // Power
    {
        static float s_pwrBefore = 0.0f;
        float pwr = light->getPower();
        float pwrPreEdit = pwr;
        propLabel(gui, "Power");
        if (gui->dragFloat("##Power", &pwr, 0.1f, 0.0f, 10000.0f))
            light->setPower(pwr);
        if (gui->isItemActivated())
            s_pwrBefore = pwrPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = light->getPower();
            float before = s_pwrBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setPower(before); },
                [cap, after]()
                { cap->setPower(after); }));
        }
    }

    // Diffuse
    {
        static kVec3 s_diffBefore;
        kVec3 diff = light->getDiffuseColor();
        kVec3 diffPreEdit = diff;
        float diffF[3] = {diff.r, diff.g, diff.b};
        propLabel(gui, "Diffuse");
        if (gui->colorEdit3("##Diffuse", diffF))
            light->setDiffuseColor(kVec3(diffF[0], diffF[1], diffF[2]));
        if (gui->isItemActivated())
            s_diffBefore = diffPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = light->getDiffuseColor();
            kVec3 before = s_diffBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setDiffuseColor(before); },
                [cap, after]()
                { cap->setDiffuseColor(after); }));
        }
    }

    // Specular
    {
        static kVec3 s_specBefore;
        kVec3 spec = light->getSpecularColor();
        kVec3 specPreEdit = spec;
        float specF[3] = {spec.r, spec.g, spec.b};
        propLabel(gui, "Specular");
        if (gui->colorEdit3("##Specular", specF))
            light->setSpecularColor(kVec3(specF[0], specF[1], specF[2]));
        if (gui->isItemActivated())
            s_specBefore = specPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = light->getSpecularColor();
            kVec3 before = s_specBefore;
            kLight *cap = light;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSpecularColor(before); },
                [cap, after]()
                { cap->setSpecularColor(after); }));
        }
    }

    if (lt == LIGHT_TYPE_POINT || lt == LIGHT_TYPE_SPOT)
    {
        // Constant
        {
            static float s_constBefore = 0.0f;
            float c = light->getConstant();
            float cPreEdit = c;
            propLabel(gui, "Constant");
            if (gui->dragFloat("##Constant", &c, 0.01f, 0.0f, 10.0f))
                light->setConstant(c);
            if (gui->isItemActivated())
                s_constBefore = cPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getConstant();
                float before = s_constBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setConstant(before); },
                    [cap, after]()
                    { cap->setConstant(after); }));
            }
        }
        // Linear
        {
            static float s_linBefore = 0.0f;
            float l = light->getLinear();
            float lPreEdit = l;
            propLabel(gui, "Linear");
            if (gui->dragFloat("##Linear", &l, 0.01f, 0.0f, 10.0f))
                light->setLinear(l);
            if (gui->isItemActivated())
                s_linBefore = lPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getLinear();
                float before = s_linBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setLinear(before); },
                    [cap, after]()
                    { cap->setLinear(after); }));
            }
        }
        // Quadratic
        {
            static float s_quadBefore = 0.0f;
            float q = light->getQuadratic();
            float qPreEdit = q;
            propLabel(gui, "Quadratic");
            if (gui->dragFloat("##Quadratic", &q, 0.01f, 0.0f, 10.0f))
                light->setQuadratic(q);
            if (gui->isItemActivated())
                s_quadBefore = qPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = light->getQuadratic();
                float before = s_quadBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setQuadratic(before); },
                    [cap, after]()
                    { cap->setQuadratic(after); }));
            }
        }
    }

    if (lt == LIGHT_TYPE_SPOT)
    {
        // Inner cone
        {
            static float s_innerBefore = 0.0f;
            float inner = glm::degrees(glm::acos(light->getCutOff()));
            float innerPreEdit = inner;
            propLabel(gui, "Inner Cone");
            if (gui->dragFloat("##InnerCone", &inner, 0.5f, 0.0f, 89.0f, "%.1f deg"))
                light->setCutOff(glm::cos(glm::radians(inner)));
            if (gui->isItemActivated())
                s_innerBefore = innerPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = glm::degrees(glm::acos(light->getCutOff()));
                float before = s_innerBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setCutOff(glm::cos(glm::radians(before))); },
                    [cap, after]()
                    { cap->setCutOff(glm::cos(glm::radians(after))); }));
            }
        }
        // Outer cone
        {
            static float s_outerBefore = 0.0f;
            float outer = glm::degrees(glm::acos(light->getOuterCutOff()));
            float outerPreEdit = outer;
            propLabel(gui, "Outer Cone");
            if (gui->dragFloat("##OuterCone", &outer, 0.5f, 0.0f, 89.0f, "%.1f deg"))
                light->setOuterCutOff(glm::cos(glm::radians(outer)));
            if (gui->isItemActivated())
                s_outerBefore = outerPreEdit;
            if (gui->isItemDeactivatedAfterEdit())
            {
                float after = glm::degrees(glm::acos(light->getOuterCutOff()));
                float before = s_outerBefore;
                kLight *cap = light;
                mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                    [cap, before]()
                    { cap->setOuterCutOff(glm::cos(glm::radians(before))); },
                    [cap, after]()
                    { cap->setOuterCutOff(glm::cos(glm::radians(after))); }));
            }
        }
    }

    gui->tableEnd();
}

// ---------------------------------------------------------------------------
// Audio section
// ---------------------------------------------------------------------------
static void drawAudioSection(kGuiManager *gui, kObject *obj, Manager *mgr, bool isDirtyPrefab, ImTextureRef audioFileIcon)
{
    if (!sectionHeader(gui, "Audio Source", isDirtyPrefab))
        return;

    if (!beginPropTable(gui, "AudTable"))
        return;

    auto &sources = obj->getAudioSources();

    {
        propLabel(gui, "Audio Clip");

        std::vector<std::string> audUuids = {""};
        std::vector<std::string> audNames = {"(None)"};
        for (const auto &kv : mgr->fileMap)
        {
            if (kv.second.type == "audio")
            {
                audUuids.push_back(kv.first);
                audNames.push_back(fs::path(kv.second.path).stem().string());
            }
        }
        std::vector<const char *> audNamePtrs;
        audNamePtrs.reserve(audNames.size());
        for (auto &n : audNames)
            audNamePtrs.push_back(n.c_str());

        std::string assignedUuid = (sources.size() > 0) ? sources[0].audioFile : std::string("");
        int current = 0;
        for (size_t i = 0; i < audUuids.size(); ++i)
            if (audUuids[i] == assignedUuid)
            {
                current = (int)i;
                break;
            }

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##AudClip", &current, audNamePtrs.data(), (int)audNamePtrs.size()))
        {
            if (sources.empty())
            {
                kAudioSource src;
                src.uuid = generateUuid();
                src.name = current > 0 ? audNames[current] : "Audio Source";
                src.audioFile = audUuids[current];
                src.isActive = true;
                src.playOnAwake = false;
                src.loop = false;
                src.volume = 1.0f;
                src.pitch = 1.0f;
                src.spatialize = true;
                src.attenuationModel = 2;
                obj->addAudioSource(src);
            }
            else
            {
                if (current == 0)
                {
                    obj->removeAudioSource(sources[0].uuid);
                }
                else
                {
                    sources[0].audioFile = audUuids[current];
                    sources[0].name = audNames[current];
                }
            }
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        // Waveform / thumbnail preview row — sits below Audio Clip, above Active.
        if (mgr->panelProject && current > 0)
        {
            gui->tableNextRow();
            gui->tableSetColumnIndex(1);
            ImTextureRef thumbIcon = mgr->panelProject->getThumbnailIcon(audUuids[current], audioFileIcon);
            float imgSize = 64.0f;
            ImGui::Image(thumbIcon, ImVec2(imgSize, imgSize));
        }
    }

    if (!sources.empty())
    {
        auto &src = sources[0];

        propLabel(gui, "Active");
        if (ImGui::Checkbox("##AudAct", &src.isActive))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        propLabel(gui, "Play On Awake");
        if (ImGui::Checkbox("##AudPOA", &src.playOnAwake))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        propLabel(gui, "Loop");
        if (ImGui::Checkbox("##AudLoop", &src.loop))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        propLabel(gui, "Volume");
        if (ImGui::DragFloat("##AudVol", &src.volume, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        propLabel(gui, "Pitch");
        if (ImGui::DragFloat("##AudPitch", &src.pitch, 0.01f, 0.1f, 3.0f, "%.2f"))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        propLabel(gui, "3D Spatial");
        if (ImGui::Checkbox("##AudSpat", &src.spatialize))
        {
            mgr->projectSaved = false;
            mgr->refreshWindowTitle();
        }

        if (src.spatialize)
        {
            propLabel(gui, "Attenuation");
            const char *attModels[] = {"None", "Inverse", "Linear", "Exponential"};
            if (ImGui::Combo("##AudAtt", &src.attenuationModel, attModels, 4))
            {
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }

            propLabel(gui, "Min Distance");
            if (ImGui::DragFloat("##AudMin", &src.minDistance, 0.1f, 0.0f, src.maxDistance))
            {
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }

            propLabel(gui, "Max Distance");
            if (ImGui::DragFloat("##AudMax", &src.maxDistance, 1.0f, src.minDistance, 10000.0f))
            {
                mgr->projectSaved = false;
                mgr->refreshWindowTitle();
            }
        }
    }

    gui->tableEnd();

    gui->spacing();
    if (!sources.empty())
    {
        bool playing = mgr->isAudioPreviewPlaying() &&
                       mgr->audioPreviewSourceUuid == sources[0].uuid;
        const char *label = playing ? "Stop" : "Play";
        if (playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.16f, 0.16f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.26f, 0.26f, 1.00f));
        }
        if (ImGui::Button(label, ImVec2(80, 0)))
            mgr->startAudioPreview(sources[0]);
        if (playing)
            ImGui::PopStyleColor(2);
    }
}

// ---------------------------------------------------------------------------
// Helper: collapsing section header with a small "+" button on the right.
// Returns true if the section is open.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Script picker helpers (used by the Scripts component)
//
// The picker lists every script source under Assets/: raw .as files and .logic
// node graphs. Picking a .as attaches that file directly; picking a .logic
// attaches the generated .as path under Library/GeneratedScripts/<logic-uuid>.as.
// The generated .as is produced when the .logic is saved in the Script Editor,
// so picking a .logic that hasn't been saved yet will only succeed once the
// user opens and saves it (buildScripts() warns "source missing" in the meantime).
// ---------------------------------------------------------------------------
namespace
{

    struct ScriptListEntry
    {
        std::string displayName;
        fs::path asPath;
    };

    std::vector<ScriptListEntry> collectProjectScripts(Manager *mgr)
    {
        std::vector<ScriptListEntry> out;
        if (!mgr || mgr->projectPath.empty())
            return out;

        fs::path assetsDir = mgr->projectPath / "Assets";
        if (!fs::exists(assetsDir))
            return out;

        fs::path genDir = mgr->projectPath / "Library" / "GeneratedScripts";

        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file())
                continue;
            const fs::path &p = it->path();
            std::string ext = p.extension().string();
            std::string rel = fs::relative(p, assetsDir, ec).generic_string();

            if (ext == ".as")
            {
                out.push_back({rel, p});
            }
            else if (ext == ".logic")
            {
                try
                {
                    std::ifstream f(p);
                    if (!f.is_open())
                        continue;
                    nlohmann::json j;
                    f >> j;
                    std::string uuid = j.value("uuid", std::string());
                    if (uuid.empty())
                        continue;
                    out.push_back({rel, genDir / (uuid + ".as")});
                }
                catch (...)
                {
                }
            }
        }

        std::sort(out.begin(), out.end(),
                  [](const ScriptListEntry &a, const ScriptListEntry &b)
                  { return a.displayName < b.displayName; });
        return out;
    }

    // Returns the human-friendly name for an attached script's stored fileName.
    // Generated scripts (under Library/GeneratedScripts/<uuid>.as) are mapped
    // back to the .logic file whose uuid produced them.
    std::string scriptDisplayName(Manager *mgr, const std::string &storedPath)
    {
        fs::path asPath(storedPath);
        if (!mgr || mgr->projectPath.empty())
            return asPath.filename().string();

        fs::path genDir = mgr->projectPath / "Library" / "GeneratedScripts";
        std::error_code ec;
        if (asPath.parent_path() != genDir)
            return asPath.filename().string();

        // Locate the .logic in Assets/ whose uuid matches the .as stem.
        std::string uuid = asPath.stem().string();
        fs::path assetsDir = mgr->projectPath / "Assets";
        if (!fs::exists(assetsDir))
            return asPath.filename().string();

        for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file())
                continue;
            if (it->path().extension() != ".logic")
                continue;
            try
            {
                std::ifstream f(it->path());
                if (!f.is_open())
                    continue;
                nlohmann::json j;
                f >> j;
                if (j.value("uuid", std::string()) == uuid)
                    return it->path().filename().string();
            }
            catch (...)
            {
            }
        }
        return asPath.filename().string();
    }

    struct ScriptVariableDecl
    {
        std::string name;
        std::string typeName; ///< AngelScript type, e.g. "kAnimator@", "float".
    };

    // Extracts simple file-scope globals ("type name = value;") from a script
    // source file. Covers the generated .as from a .logic node graph as well as
    // hand-written scripts whose globals follow the same simple declaration form.
    std::vector<ScriptVariableDecl> collectScriptVariables(const std::string &asPath)
    {
        std::vector<ScriptVariableDecl> out;
        std::ifstream f(asPath);
        if (!f.is_open())
            return out;

        static const std::string kTypes[] = {
            "kAnimator@", "kAudioSource@", "kMaterial@", "kObject@",
            "kVec3", "string", "bool", "float", "int",
        };

        std::string line;
        while (std::getline(f, line))
        {
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos)
                continue;
            // Only file-scope globals (column 0) are script globals; function-local
            // declarations are indented and must not be exposed in the inspector.
            if (first != 0)
                continue;
            std::string stmt = line.substr(first);
            // Skip comments, preprocessor lines and block markers.
            if (stmt[0] == '/' || stmt[0] == '*' || stmt[0] == '#' ||
                stmt[0] == '{' || stmt[0] == '}')
                continue;

            size_t semi = stmt.find(';');
            if (semi == std::string::npos)
                continue;
            std::string decl = stmt.substr(0, semi);
            size_t eq = decl.find('=');
            if (eq == std::string::npos)
                continue;

            std::string lhs = decl.substr(0, eq);
            while (!lhs.empty() && (lhs.back() == ' ' || lhs.back() == '\t'))
                lhs.pop_back();

            for (const std::string &t : kTypes)
            {
                if (lhs.compare(0, t.size(), t) != 0)
                    continue;
                char after = (lhs.size() > t.size()) ? lhs[t.size()] : ' ';
                if (after != ' ' && after != '\t')
                    continue;

                std::string name = lhs.substr(t.size());
                size_t nfirst = name.find_first_not_of(" \t");
                if (nfirst == std::string::npos)
                    break;
                name = name.substr(nfirst);

                ScriptVariableDecl d;
                d.name     = name;
                d.typeName = t;
                out.push_back(d);
                break;
            }
        }
        return out;
    }

    // True when the given object matches a script-global handle-type variable:
    // kObject@ matches any object; the rest require the object to carry the
    // matching component (e.g. an animator for kAnimator@).
    bool scriptHandleTypeMatches(kObject *o, const std::string &typeName)
    {
        if (!o)
            return false;
        if (typeName == "kObject@")
            return true;
        if (typeName == "kAnimator@")
            return !o->getAnimatorRef().empty();
        if (typeName == "kAudioSource@")
            return !o->getAudioSources().empty();
        if (typeName == "kMaterial@")
            return o->getType() == NODE_TYPE_MESH;
        return false;
    }

} // namespace

// ---------------------------------------------------------------------------
// Particle section (shown between Transform and Scripts)
// ---------------------------------------------------------------------------
static void drawParticleSection(kGuiManager *gui, kObject *obj, Manager *mgr, bool isDirtyPrefab = false)
{
	auto &particles = obj->getParticles();
	if (particles.empty())
		return;

	for (size_t pi = 0; pi < particles.size(); ++pi)
	{
		auto &p = particles[pi];

		char headerLabel[128];
		snprintf(headerLabel, sizeof(headerLabel), "%s##PartSec%d",
		         p.name.c_str(), (int)pi);

		if (!sectionHeader(gui, headerLabel, isDirtyPrefab))
			continue;

		if (!beginPropTable(gui, ("PartTable" + std::to_string(pi)).c_str()))
			continue;

		// --- Name ---
		{
			char nameBuf[128];
			strncpy_s(nameBuf, sizeof(nameBuf), p.name.c_str(), _TRUNCATE);
			propLabel(gui, "Name");
			if (ImGui::InputText("##PartName", nameBuf, sizeof(nameBuf)))
			{
				p.name = nameBuf;
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Active / Looping ---
		{
			propLabel(gui, "Active");
			if (ImGui::Checkbox("##PartActive", &p.isActive))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}
		{
			propLabel(gui, "Looping");
			if (ImGui::Checkbox("##PartLoop", &p.looping))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Emission ---
		{
			float maxP = (float)p.maxParticles;
			propLabel(gui, "Max Particles");
			if (ImGui::DragFloat("##PartMaxP", &maxP, 1.0f, 1.0f, 100000.0f, "%.0f"))
			{
				p.maxParticles = (int)std::max(1.0f, maxP);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "Emission Rate");
			if (ImGui::DragFloat("##PartRate", &p.emissionRate, 0.5f, 0.0f, 10000.0f, "%.1f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "Lifetime");
			if (ImGui::DragFloat("##PartLife", &p.lifetime, 0.05f, 0.01f, 300.0f, "%.2f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "Gravity Scale");
			if (ImGui::DragFloat("##PartGravity", &p.gravityScale, 0.01f, -10.0f, 10.0f, "%.2f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Velocity ---
		{
			float vel[3] = {p.startVelocity.x, p.startVelocity.y, p.startVelocity.z};
			propLabel(gui, "Start Velocity");
			if (ImGui::DragFloat3("##PartVel", vel, 0.01f))
			{
				p.startVelocity = kVec3(vel[0], vel[1], vel[2]);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "Start Speed");
			if (ImGui::DragFloat("##PartSpeed", &p.startSpeed, 0.05f, 0.0f, 1000.0f, "%.2f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			float var[3] = {p.velocityVariance.x, p.velocityVariance.y, p.velocityVariance.z};
			propLabel(gui, "Vel. Variance");
			if (ImGui::DragFloat3("##PartVar", var, 0.01f))
			{
				p.velocityVariance = kVec3(var[0], var[1], var[2]);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Visual ---
		{
			float cs[4] = {p.colorStart.r, p.colorStart.g, p.colorStart.b, p.colorStart.a};
			propLabel(gui, "Start Color");
			if (ImGui::ColorEdit4("##PartCStart", cs))
			{
				p.colorStart = kVec4(cs[0], cs[1], cs[2], cs[3]);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			float ce[4] = {p.colorEnd.r, p.colorEnd.g, p.colorEnd.b, p.colorEnd.a};
			propLabel(gui, "End Color");
			if (ImGui::ColorEdit4("##PartCEnd", ce))
			{
				p.colorEnd = kVec4(ce[0], ce[1], ce[2], ce[3]);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "Start Size");
			if (ImGui::DragFloat("##PartSzStart", &p.sizeStart, 0.01f, 0.0f, 100.0f, "%.2f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			propLabel(gui, "End Size");
			if (ImGui::DragFloat("##PartSzEnd", &p.sizeEnd, 0.01f, 0.0f, 100.0f, "%.2f"))
			{
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Emission Shape ---
		{
			const char *shapeNames[] = {"Point", "Sphere", "Cone", "Box"};
			int curShape = (int)p.emissionShape;
			propLabel(gui, "Shape");
			if (ImGui::Combo("##PartEmitShape", &curShape, shapeNames, 4))
			{
				p.emissionShape = (kParticle::EmissionShape)curShape;
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}

			float sz[3] = {p.shapeSize.x, p.shapeSize.y, p.shapeSize.z};
			propLabel(gui, "Shape Size");
			if (ImGui::DragFloat3("##PartShapeSz", sz, 0.01f, 0.01f, 1000.0f))
			{
				p.shapeSize = kVec3(sz[0], sz[1], sz[2]);
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		// --- Texture ---
		{
			char texBuf[512];
			strncpy_s(texBuf, sizeof(texBuf), p.texturePath.c_str(), _TRUNCATE);
			propLabel(gui, "Texture Path");
			if (ImGui::InputText("##PartTex", texBuf, sizeof(texBuf)))
			{
				p.texturePath = texBuf;
				mgr->projectSaved = false;
				mgr->refreshWindowTitle();
			}
		}

		gui->tableEnd();

		gui->spacing();
		gui->separator();
		gui->spacing();
	}
}

// ---------------------------------------------------------------------------
// Audio Listener section
// ---------------------------------------------------------------------------
static void drawAudioListenerSection(kGuiManager *gui, kObject *obj, Manager *manager, bool isDirtyPrefab = false)
{
    if (obj->getAudioListeners().empty())
        return;

    if (!sectionHeader(gui, "Audio Listener", isDirtyPrefab))
        return;

    auto &l = obj->getAudioListeners()[0];
    ImGui::PushID(l.uuid.c_str());
    if (ImGui::Checkbox("Active##ListActive", &l.isActive))
        manager->projectSaved = false;
    ImGui::SameLine();
    ImGui::TextUnformatted("Audio Listener");
    ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
    if (ImGui::SmallButton("x##RemList"))
    {
        obj->removeAudioListener(l.uuid);
        manager->projectSaved = false;
    }
    ImGui::PopID();

    gui->spacing();
}

// ---------------------------------------------------------------------------
// Scripts section (Physics / Scripts)
// ---------------------------------------------------------------------------
static void drawScriptsSection(kGuiManager *gui, kObject *obj, Manager *manager, bool isDirtyPrefab = false)
{
    gui->spacing();
    gui->separatorText("Components");
    gui->spacing();

    // Each section renders only when its data is present. To add a
    // script, click the button at the bottom — that opens a popup with
    // every available script type.

    // ── Physics ─────────────────────────────────────────────────────────────
    if (obj->getHasPhysicsDesc())
    {
        if (sectionHeader(gui, "Physics", isDirtyPrefab))
        {
            kPhysicsObjectDesc &desc = obj->getPhysicsDesc();
            const bool dynamic = (desc.type == kPhysicsObjectType::Dynamic);

            if (beginPropTable(gui, "PhysTable"))
            {
                // Body type — Mesh shape disallows Dynamic / Trigger in Jolt,
                // so we grey those out instead of silently coercing on Play.
                const char *bodyNames[] = {"Dynamic", "Static", "Kinematic", "Trigger"};
                int bodyIdx = (int)desc.type;
                const bool needsStaticBody =
                    (desc.shape.type == kPhysicsShapeType::Mesh ||
                     desc.shape.type == kPhysicsShapeType::Plane);
                propLabel(gui, "Body Type");
                if (ImGui::BeginCombo("##PhysBody", bodyNames[bodyIdx]))
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        bool allowed = !needsStaticBody ||
                                       i == (int)kPhysicsObjectType::Static ||
                                       i == (int)kPhysicsObjectType::Kinematic;
                        bool selected = (i == bodyIdx);
                        if (ImGui::Selectable(bodyNames[i], selected,
                                              allowed ? 0 : ImGuiSelectableFlags_Disabled))
                        {
                            desc.type = (kPhysicsObjectType)i;
                            manager->projectSaved = false;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Layer — bodies only interact with other bodies on the same
                // named layer. An "Edit Layers..." item manages the project list.
                {
                    std::string currentLayer = desc.layer;
                    if (currentLayer.empty()) currentLayer = "Default";
                    // Fall back to "Default" if the stored layer no longer exists.
                    bool found = false;
                    for (const auto &l : manager->layerSettings.layers)
                        if (l == currentLayer) { found = true; break; }
                    if (!found)
                    {
                        currentLayer = "Default";
                        desc.layer = "Default";
                    }
                    propLabel(gui, "Layer");
                    if (ImGui::BeginCombo("##PhysLayer", currentLayer.c_str()))
                    {
                        for (const auto &l : manager->layerSettings.layers)
                        {
                            bool selected = (l == currentLayer);
                            if (ImGui::Selectable(l.c_str(), selected))
                            {
                                desc.layer = l;
                                manager->projectSaved = false;
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("Edit Layers..."))
                            manager->showLayerEditor = true;
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Objects only collide with other objects on the same physics layer.");
                }

                // Shape
                const char *shapeNames[] = {
                    "Sphere", "Box", "Capsule", "Cylinder", "Convex Hull", "Mesh", "Plane"};
                int shapeIdx = (int)desc.shape.type;
                propLabel(gui, "Shape");
                if (ImGui::Combo("##PhysShape", &shapeIdx, shapeNames,
                                 IM_ARRAYSIZE(shapeNames)))
                {
                    desc.shape.type = (kPhysicsShapeType)shapeIdx;
                    // Mesh and Plane can only be Static/Kinematic in Jolt;
                    // auto-coerce so the body-type combo doesn't sit on an
                    // illegal value.
                    const bool needsStatic =
                        (desc.shape.type == kPhysicsShapeType::Mesh ||
                         desc.shape.type == kPhysicsShapeType::Plane);
                    if (needsStatic &&
                        desc.type != kPhysicsObjectType::Static &&
                        desc.type != kPhysicsObjectType::Kinematic)
                        desc.type = kPhysicsObjectType::Static;
                    manager->projectSaved = false;
                }

                // Shape params
                switch (desc.shape.type)
                {
                case kPhysicsShapeType::Box:
                {
                    float he[3] = {desc.shape.halfExtents.x, desc.shape.halfExtents.y, desc.shape.halfExtents.z};
                    propLabel(gui, "Half Extents");
                    if (ImGui::DragFloat3("##PhysHE", he, 0.01f, 0.001f, 1000.0f))
                    {
                        desc.shape.halfExtents = kVec3(he[0], he[1], he[2]);
                        manager->projectSaved = false;
                    }
                    break;
                }
                case kPhysicsShapeType::Sphere:
                    propLabel(gui, "Radius");
                    if (ImGui::DragFloat("##PhysR", &desc.shape.radius, 0.01f, 0.001f, 1000.0f))
                        manager->projectSaved = false;
                    break;
                case kPhysicsShapeType::Capsule:
                case kPhysicsShapeType::Cylinder:
                    propLabel(gui, "Radius");
                    if (ImGui::DragFloat("##PhysR", &desc.shape.radius, 0.01f, 0.001f, 1000.0f))
                        manager->projectSaved = false;
                    propLabel(gui, "Height");
                    if (ImGui::DragFloat("##PhysH", &desc.shape.height, 0.01f, 0.001f, 1000.0f))
                        manager->projectSaved = false;
                    break;
                case kPhysicsShapeType::ConvexHull:
                case kPhysicsShapeType::Mesh:
                {
                    propLabel(gui, "Source");
                    ImGui::TextDisabled(desc.shape.type == kPhysicsShapeType::Mesh
                                            ? "Object's mesh (static / kinematic only)"
                                            : "Object's mesh (convex hull)");

                    // Per-axis stretch applied via Jolt's ScaledShape.
                    float sc[3] = {desc.shape.customScale.x,
                                   desc.shape.customScale.y,
                                   desc.shape.customScale.z};
                    propLabel(gui, "Size");
                    if (ImGui::DragFloat3("##PhysScale", sc, 0.01f, 0.01f, 1000.0f, "%.3f"))
                    {
                        desc.shape.customScale = kVec3(sc[0], sc[1], sc[2]);
                        manager->projectSaved = false;
                    }
                    break;
                }
                case kPhysicsShapeType::Plane:
                {
                    propLabel(gui, "Source");
                    ImGui::TextDisabled("Object's +Y axis (static / kinematic only)");

                    // Plane is mathematically infinite; halfExtents.x/z
                    // drive the broadphase rectangle so a finite size is
                    // useful for picking and gameplay queries.
                    float he[3] = {desc.shape.halfExtents.x,
                                   desc.shape.halfExtents.y,
                                   desc.shape.halfExtents.z};
                    propLabel(gui, "Half Size");
                    if (ImGui::DragFloat3("##PlaneHE", he, 0.05f, 0.1f, 10000.0f, "%.2f"))
                    {
                        desc.shape.halfExtents = kVec3(he[0], he[1], he[2]);
                        manager->projectSaved = false;
                    }
                    break;
                }
                }

                // Mass / damping / gravity-factor are Dynamic-only; greyed
                // out otherwise so the user still sees the value.
                gui->beginDisabled(!dynamic);
                propLabel(gui, "Mass (kg)");
                if (ImGui::DragFloat("##PhysMass", &desc.mass, 0.05f, 0.001f, 10000.0f, "%.3f"))
                    manager->projectSaved = false;
                gui->endDisabled();

                propLabel(gui, "Friction");
                if (ImGui::DragFloat("##PhysFr", &desc.friction, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Restitution");
                if (ImGui::DragFloat("##PhysRest", &desc.restitution, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->beginDisabled(!dynamic);
                propLabel(gui, "Linear Damping");
                if (ImGui::DragFloat("##PhysLD", &desc.linearDamping, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Angular Damping");
                if (ImGui::DragFloat("##PhysAD", &desc.angularDamping, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Gravity Factor");
                if (ImGui::DragFloat("##PhysGF", &desc.gravityFactor, 0.01f, -10.0f, 10.0f, "%.2f"))
                    manager->projectSaved = false;
                gui->endDisabled();

                gui->tableEnd();
            }

            gui->spacing();
            if (ImGui::SmallButton("Remove Physics##RemPhys"))
            {
                obj->setHasPhysicsDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Character Controller ────────────────────────────────────────────────
    if (obj->getHasCharacterDesc())
    {
        if (sectionHeader(gui, "Character Controller", isDirtyPrefab))
        {
            kCharacterControllerDesc &cd = obj->getCharacterDesc();

            if (beginPropTable(gui, "CharTable"))
            {
                propLabel(gui, "Radius");
                if (ImGui::DragFloat("##CCRad", &cd.radius, 0.01f, 0.01f, 100.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Height");
                if (ImGui::DragFloat("##CCHt", &cd.height, 0.01f, 0.05f, 100.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Mass (kg)");
                if (ImGui::DragFloat("##CCMass", &cd.mass, 0.5f, 0.1f, 10000.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Friction");
                if (ImGui::DragFloat("##CCFr", &cd.friction, 0.01f, 0.0f, 1.0f, "%.2f"))
                    manager->projectSaved = false;

                propLabel(gui, "Slope Limit");
                if (ImGui::DragFloat("##CCSlope", &cd.slopeLimit, 0.5f, 0.0f, 89.0f, "%.1f"))
                    manager->projectSaved = false;

                propLabel(gui, "Step Height");
                if (ImGui::DragFloat("##CCStep", &cd.stepHeight, 0.01f, 0.0f, 10.0f, "%.3f"))
                    manager->projectSaved = false;

                propLabel(gui, "Gravity Factor");
                if (ImGui::DragFloat("##CCGF", &cd.gravityFactor, 0.01f, -10.0f, 10.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->tableEnd();
            }

            gui->spacing();
            if (ImGui::SmallButton("Remove Character##RemChar"))
            {
                obj->setHasCharacterDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Navigation ───────────────────────────────────────────────────────────
    // Bakes a nav mesh from the scene's static meshes (all, or within an area
    // box centred on this object). Baking is an editor action; the result is
    // held by the Manager, not serialized.
    if (obj->getHasNavMeshDesc())
    {
        if (sectionHeader(gui, "Navigation", isDirtyPrefab))
        {
            kNavMeshDesc &nd = obj->getNavMeshDesc();

            // Geometry source: All vs Area. Radio buttons don't fit the
            // label/widget table layout, so we draw this row inline.
            ImGui::TextUnformatted("Source:");
            ImGui::SameLine();
            if (ImGui::RadioButton("All##NavAll", !nd.useArea))
            {
                nd.useArea = false;
                manager->projectSaved = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Area##NavArea", nd.useArea))
            {
                nd.useArea = true;
                manager->projectSaved = false;
            }

            if (beginPropTable(gui, "NavTable"))
            {
                if (nd.useArea)
                {
                    float sz[3] = {nd.areaSize.x, nd.areaSize.y, nd.areaSize.z};
                    propLabel(gui, "Area Size");
                    if (ImGui::DragFloat3("##NavSz", sz, 0.1f, 0.1f, 100000.0f))
                    {
                        nd.areaSize = kVec3(sz[0], sz[1], sz[2]);
                        manager->projectSaved = false;
                    }
                }

                propLabel(gui, "Agent Radius");
                if (ImGui::DragFloat("##NavAR", &nd.config.agentRadius, 0.01f, 0.01f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Agent Height");
                if (ImGui::DragFloat("##NavAH", &nd.config.agentHeight, 0.01f, 0.01f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Max Climb");
                if (ImGui::DragFloat("##NavMC", &nd.config.agentMaxClimb, 0.01f, 0.0f, 100.0f, "%.2f"))
                    manager->projectSaved = false;
                propLabel(gui, "Max Slope");
                if (ImGui::DragFloat("##NavMS", &nd.config.agentMaxSlope, 0.5f, 0.0f, 89.0f, "%.1f"))
                    manager->projectSaved = false;
                propLabel(gui, "Cell Size");
                if (ImGui::DragFloat("##NavCS", &nd.config.cellSize, 0.01f, 0.01f, 10.0f, "%.2f"))
                    manager->projectSaved = false;

                gui->tableEnd();
            }

            if (nd.useArea)
                ImGui::TextDisabled("  Box is centred on this object's position.");

            gui->spacing();

            if (ImGui::Button("Bake##Nav", ImVec2(80, 0)))
                manager->bakeNavMesh(obj);
            ImGui::SameLine();
            if (ImGui::Button("Clear##Nav", ImVec2(80, 0)))
                manager->clearNavMesh(obj);
            ImGui::SameLine();
            if (manager->isNavMeshBaked(obj))
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Baked");
            else
                ImGui::TextDisabled("Not baked");

            gui->spacing();
            if (ImGui::SmallButton("Remove Navigation##RemNav"))
            {
                manager->clearNavMesh(obj);
                obj->setHasNavMeshDesc(false);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── Animator ────────────────────────────────────────────────────────────
    if (!obj->getAnimatorRef().empty())
    {
        kString assigned = obj->getAnimatorRef();

        if (sectionHeader(gui, "Animator", isDirtyPrefab))
        {
            // Show the assigned animator name
            std::string displayName = assigned;
            auto it = manager->fileMap.find(assigned);
            if (it != manager->fileMap.end())
                displayName = fs::path(it->second.path).stem().string();
            else
            {
                // Truncate UUID for display
                if (displayName.size() > 16)
                    displayName = displayName.substr(0, 8) + "...";
            }

            gui->textUnformatted(displayName.c_str());

            // Remove button
            if (ImGui::SmallButton("x##RemAnim"))
            {
                obj->setAnimatorRef("");
                manager->projectSaved = false;
                manager->refreshWindowTitle();
            }

            gui->spacing();
        }
        gui->spacing();
    }

    // ── Scripts ─────────────────────────────────────────────────────────────
    if (!obj->getScripts().empty())
    {
        if (sectionHeader(gui, "Scripts", isDirtyPrefab))
        {
            auto &scripts = obj->getScripts();
            kString toRemove;
            for (auto &s : scripts)
            {
                ImGui::PushID(s.uuid.c_str());
                if (ImGui::Checkbox("##ScActive", &s.isActive))
                    manager->projectSaved = false;
                ImGui::SameLine();
                ImGui::TextUnformatted(scriptDisplayName(manager, s.fileName).c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f);
                if (ImGui::SmallButton("x##RemSc"))
                    toRemove = s.uuid;

                // Expose script-scope globals (logic-graph variables and .as
                // globals) so the user can assign values or object references.
                std::vector<ScriptVariableDecl> vars = collectScriptVariables(s.fileName);
                if (!vars.empty())
                {
                    for (const auto &decl : vars)
                    {
                        kScriptVarBinding *binding = nullptr;
                        for (auto &b : s.variableBindings)
                        {
                            if (b.name == decl.name)
                            {
                                binding = &b;
                                break;
                            }
                        }
                        if (!binding)
                        {
                            kScriptVarBinding nb;
                            nb.name     = decl.name;
                            nb.typeName = decl.typeName;
                            s.variableBindings.push_back(nb);
                            binding = &s.variableBindings.back();
                        }

                        ImGui::PushID(decl.name.c_str());
                        ImGui::TextUnformatted(decl.name.c_str());
                        ImGui::SameLine(150.0f);
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 20.0f);

                        if (decl.typeName == "int")
                        {
                            int iv = (int)binding->valueFloat[0];
                            if (ImGui::DragInt("##scvar", &iv, 1.0f))
                            {
                                binding->valueFloat[0] = (float)iv;
                                binding->assigned = true;
                                manager->projectSaved = false;
                            }
                        }
                        else if (decl.typeName == "float")
                        {
                            if (ImGui::DragFloat("##scvar", &binding->valueFloat[0], 0.05f))
                            {
                                binding->assigned = true;
                                manager->projectSaved = false;
                            }
                        }
                        else if (decl.typeName == "bool")
                        {
                            if (ImGui::Checkbox("##scvar", &binding->valueBool))
                            {
                                binding->assigned = true;
                                manager->projectSaved = false;
                            }
                        }
                        else if (decl.typeName == "kVec3")
                        {
                            float v[3] = {binding->valueFloat[0],
                                          binding->valueFloat[1],
                                          binding->valueFloat[2]};
                            if (ImGui::DragFloat3("##scvar", v, 0.05f))
                            {
                                binding->valueFloat[0] = v[0];
                                binding->valueFloat[1] = v[1];
                                binding->valueFloat[2] = v[2];
                                binding->assigned = true;
                                manager->projectSaved = false;
                            }
                        }
                        else if (decl.typeName == "string")
                        {
                            char buf[128];
                            strncpy_s(buf, sizeof(buf), binding->valueStr.c_str(), _TRUNCATE);
                            buf[sizeof(buf) - 1] = '\0';
                            if (ImGui::InputText("##scvar", buf, sizeof(buf)))
                            {
                                binding->valueStr = buf;
                                binding->assigned = true;
                                manager->projectSaved = false;
                            }
                        }
                        else
                        {
                            // Handle types: a read-only field the user links by
                            // dragging a scene object from the Hierarchy panel
                            // onto it. The dropped object is validated against the
                            // variable's handle type before linking — no per-frame
                            // full-scene traversal is needed (findObjectByUuid uses
                            // the hierarchy's objectMap fast path).
                            std::string linkedName = "(None)";
                            if (!binding->valueStr.empty())
                            {
                                if (kObject *linked = manager->findObjectByUuid(binding->valueStr))
                                    linkedName = linked->getName();
                            }

                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24.0f);
                            char linkBuf[256];
                            strncpy_s(linkBuf, sizeof(linkBuf), linkedName.c_str(), _TRUNCATE);
                            linkBuf[sizeof(linkBuf) - 1] = '\0';
                            ImGui::InputText("##scobj", linkBuf, sizeof(linkBuf),
                                             ImGuiInputTextFlags_ReadOnly);

                            // Drag an object from the Hierarchy panel here to link it.
                            if (ImGui::BeginDragDropTarget())
                            {
                                const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
                                    "SCENE_OBJECT", ImGuiDragDropFlags_AcceptBeforeDelivery);
                                if (payload && payload->IsDelivery())
                                {
                                    std::string droppedUuid((const char *)payload->Data);
                                    kObject *dropped = manager->findObjectByUuid(droppedUuid);
                                    if (dropped && scriptHandleTypeMatches(dropped, decl.typeName))
                                    {
                                        binding->valueStr = droppedUuid;
                                        binding->assigned = true;
                                        manager->projectSaved = false;
                                    }
                                }
                                else if (payload && ImGui::IsItemHovered())
                                {
                                    std::string droppedUuid((const char *)payload->Data);
                                    kObject *dropped = manager->findObjectByUuid(droppedUuid);
                                    if (!dropped || !scriptHandleTypeMatches(dropped, decl.typeName))
                                        ImGui::SetTooltip("Object does not match this variable type");
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::SameLine();
                            if (ImGui::SmallButton("x##clrscobj"))
                            {
                                binding->valueStr.clear();
                                binding->assigned = false;
                                manager->projectSaved = false;
                            }
                        }
                        ImGui::PopID();
                    }
                }

                ImGui::PopID();
            }
            if (!toRemove.empty())
            {
                obj->removeScript(toRemove);
                manager->projectSaved = false;
            }
        }
        gui->spacing();
    }

    // ── "Add" button ─────────────────────────────────────────────────────────
    // A single button opens a context menu offering "Animator" and "Script" so
    // the user can attach either component type. Each entry cascades into a
    // submenu listing the matching assets (.animator / .as / .logic).
    {
        gui->spacing();
        if (ImGui::Button("Add", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            // ── Animator (submenu listing .animator assets) ──
            std::vector<std::string> animUuids;
            std::vector<std::string> animNames;
            for (const auto &kv : manager->fileMap)
            {
                if (kv.second.type == "animator")
                {
                    animUuids.push_back(kv.first);
                    animNames.push_back(fs::path(kv.second.path).stem().string());
                }
            }

            if (ImGui::BeginMenu("Animator"))
            {
                if (animUuids.empty())
                {
                    ImGui::TextDisabled("No .animator in Assets/");
                }
                else
                {
                    for (size_t i = 0; i < animUuids.size(); ++i)
                    {
                        if (ImGui::MenuItem(animNames[i].c_str()))
                        {
                            obj->setAnimatorRef(animUuids[i]);
                            manager->projectSaved = false;
                            manager->refreshWindowTitle();
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // ── Script (submenu listing .as / .logic assets) ──
            auto entries = collectProjectScripts(manager);
            if (ImGui::BeginMenu("Script"))
            {
                if (entries.empty())
                {
                    ImGui::TextDisabled("No .as or .logic in Assets/");
                }
                else
                {
                    for (const auto &e : entries)
                    {
                        if (ImGui::MenuItem(e.displayName.c_str()))
                        {
                            kScript s;
                            s.uuid = generateUuid();
                            s.fileName = e.asPath.string();
                            s.isActive = true;
                            obj->addScript(s);
                            manager->projectSaved = false;
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // ── Physics Shape (submenu adding a rigid body / character) ──
            // Adds a physics descriptor configured with the chosen collision
            // shape so the "Physics" section appears for fine-tuning. Picking
            // "Character Controller" enables the independent character
            // descriptor instead (its own section in the inspector).
            if (ImGui::BeginMenu("Physics Shape"))
            {
                auto addShape = [&](kPhysicsShapeType st)
                {
                    kPhysicsObjectDesc &desc = obj->getPhysicsDesc();
                    desc.shape.type = st;
                    // Mesh & Plane shapes are static-only in Jolt; force a
                    // static body so the runtime can actually build them.
                    if (st == kPhysicsShapeType::Mesh ||
                        st == kPhysicsShapeType::Plane)
                        desc.type = kPhysicsObjectType::Static;
                    obj->setHasPhysicsDesc(true);
                    manager->projectSaved = false;
                };

                if (ImGui::MenuItem("Sphere"))
                    addShape(kPhysicsShapeType::Sphere);
                if (ImGui::MenuItem("Box"))
                    addShape(kPhysicsShapeType::Box);
                if (ImGui::MenuItem("Capsule"))
                    addShape(kPhysicsShapeType::Capsule);
                if (ImGui::MenuItem("Cylinder"))
                    addShape(kPhysicsShapeType::Cylinder);
                if (ImGui::MenuItem("ConvexHull"))
                    addShape(kPhysicsShapeType::ConvexHull);
                if (ImGui::MenuItem("Mesh"))
                    addShape(kPhysicsShapeType::Mesh);
                if (ImGui::MenuItem("Plane"))
                    addShape(kPhysicsShapeType::Plane);

                ImGui::Separator();

                if (ImGui::MenuItem("Character Controller"))
                {
                    obj->setHasCharacterDesc(true);
                    manager->projectSaved = false;
                }

                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }
}

// ---------------------------------------------------------------------------
// Scene section
// ---------------------------------------------------------------------------
static void drawSceneSection(kGuiManager *gui, kScene *scene, Manager *mgr)
{
    if (!gui->collapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "SceneTable"))
        return;

    // Ambient color
    {
        static kVec3 s_ambBefore;
        kVec3 amb = scene->getAmbientLightColor();
        float ambF[3] = {amb.r, amb.g, amb.b};
        propLabel(gui, "Ambient");
        if (gui->colorEdit3("##SceneAmbient", ambF))
            scene->setAmbientLightColor(kVec3(ambF[0], ambF[1], ambF[2]));
        if (gui->isItemActivated())
            s_ambBefore = amb;
        if (gui->isItemDeactivatedAfterEdit())
        {
            kVec3 after = scene->getAmbientLightColor();
            kVec3 before = s_ambBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setAmbientLightColor(before); },
                [cap, after]()
                { cap->setAmbientLightColor(after); }));
        }
    }

    // Shadows toggle
    {
        bool shadows = scene->getShadowsEnabled();
        propLabel(gui, "Shadows");
        if (gui->checkbox("##SceneShadows", &shadows))
        {
            bool before = !shadows;
            bool after = shadows;
            kScene *cap = scene;
            scene->setShadowsEnabled(shadows);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setShadowsEnabled(before); },
                [cap, after]()
                { cap->setShadowsEnabled(after); }));
        }
    }

    // Shadow bias controls (only when shadows are enabled).
    if (scene->getShadowsEnabled())
    {
        static float s_shadowBiasBefore = 0.0f;
        float bias = scene->getShadowBias();
        float biasPreEdit = bias;
        propLabel(gui, "Shadow Bias");
        if (gui->dragFloat("##ShadowBias", &bias, 0.0001f, 0.0f, 0.05f, "%.5f"))
            scene->setShadowBias(bias);
        if (gui->isItemActivated())
            s_shadowBiasBefore = biasPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowBias();
            float before = s_shadowBiasBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setShadowBias(before); },
                [cap, after]()
                { cap->setShadowBias(after); }));
        }

        static float s_shadowNBiasBefore = 0.0f;
        float nbias = scene->getShadowNormalBias();
        float nbiasPreEdit = nbias;
        propLabel(gui, "Shadow N.Bias");
        if (gui->dragFloat("##ShadowNBias", &nbias, 0.0005f, 0.0f, 0.1f, "%.5f"))
            scene->setShadowNormalBias(nbias);
        if (gui->isItemActivated())
            s_shadowNBiasBefore = nbiasPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowNormalBias();
            float before = s_shadowNBiasBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setShadowNormalBias(before); },
                [cap, after]()
                { cap->setShadowNormalBias(after); }));
        }

        // Shadow map resolution — combo of standard sizes. Bigger = sharper
        // shadow but more VRAM (size² × cascadeCount × depth bpp).
        propLabel(gui, "Shadow Res");
        const char *resItems[] = {"512", "1024", "2048", "4096"};
        const int resValues[] = {512, 1024, 2048, 4096};
        int curRes = scene->getShadowMapResolution();
        int resIdx = 2; // default to 2048
        for (int i = 0; i < 4; ++i)
            if (resValues[i] == curRes)
            {
                resIdx = i;
                break;
            }
        if (ImGui::Combo("##ShadowRes", &resIdx, resItems, IM_ARRAYSIZE(resItems)))
        {
            int before = curRes;
            int after = resValues[resIdx];
            kScene *cap = scene;
            scene->setShadowMapResolution(after);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setShadowMapResolution(before); },
                [cap, after]()
                { cap->setShadowMapResolution(after); }));
        }

        // Shadow softness — PCF tap spacing in texels. 0 = hard, ~3 = very soft.
        static float s_softnessBefore = 0.0f;
        float soft = scene->getShadowSoftness();
        float softPreEdit = soft;
        propLabel(gui, "Shadow Softness");
        if (gui->dragFloat("##ShadowSoftness", &soft, 0.05f, 0.0f, 5.0f, "%.2f"))
            scene->setShadowSoftness(soft);
        if (gui->isItemActivated())
            s_softnessBefore = softPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getShadowSoftness();
            float before = s_softnessBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setShadowSoftness(before); },
                [cap, after]()
                { cap->setShadowSoftness(after); }));
        }
    }

    // Skybox ambient toggle
    {
        bool enabled = scene->getSkyboxAmbientEnabled();
        propLabel(gui, "Sky Ambient");
        if (gui->checkbox("##SkyAmbient", &enabled))
        {
            bool before = !enabled;
            bool after = enabled;
            kScene *cap = scene;
            scene->setSkyboxAmbientEnabled(enabled);
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSkyboxAmbientEnabled(before); },
                [cap, after]()
                { cap->setSkyboxAmbientEnabled(after); }));
        }
    }

    // Skybox ambient strength (only when enabled)
    if (scene->getSkyboxAmbientEnabled())
    {
        static float s_skyStrBefore = 0.0f;
        float str = scene->getSkyboxAmbientStrength();
        float strPreEdit = str;
        propLabel(gui, "Sky Strength");
        if (gui->dragFloat("##SkyStrength", &str, 0.01f, 0.0f, 5.0f))
            scene->setSkyboxAmbientStrength(str);
        if (gui->isItemActivated())
            s_skyStrBefore = strPreEdit;
        if (gui->isItemDeactivatedAfterEdit())
        {
            float after = scene->getSkyboxAmbientStrength();
            float before = s_skyStrBefore;
            kScene *cap = scene;
            mgr->undoRedo.push(std::make_unique<PropertyCommand>(
                [cap, before]()
                { cap->setSkyboxAmbientStrength(before); },
                [cap, after]()
                { cap->setSkyboxAmbientStrength(after); }));
        }
    }

    gui->tableEnd();

    // Skybox controls — for now just a "use the bundled default" button.
    // Future: pick a custom cubemap asset here.
    gui->spacing();
    if (ImGui::Button("Apply Default Skybox"))
        mgr->applyDefaultSkybox(scene);
}

// ---------------------------------------------------------------------------
// Project asset section
// ---------------------------------------------------------------------------

static void drawAssetThumbnail(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset)
{
    if (asset.thumbnail != nullptr)
    {
        float avail = gui->getContentRegionAvail().x;
        float imgSize = std::min(avail, 128.0f);
        gui->setCursorPosX(gui->getCursorPosX() + (avail - imgSize) * 0.5f);
        ImGui::Image(asset.thumbnail, ImVec2(imgSize, imgSize));
        gui->spacing();
    }
}

static void drawAudioAssetPreview(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset, Manager *mgr)
{
    drawAssetThumbnail(gui, asset);

    gui->spacing();
    gui->separator();
    gui->spacing();

    // File info
    if (!gui->collapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!beginPropTable(gui, "AudioTable"))
        return;

    propLabel(gui, "Name");
    gui->beginDisabled(true);
    char nameBuf[256];
    strncpy_s(nameBuf, sizeof(nameBuf), asset.name.c_str(), _TRUNCATE);
    ImGui::InputText("##AudFileName", nameBuf, sizeof(nameBuf));
    gui->endDisabled();

    propLabel(gui, "UUID");
    gui->beginDisabled(true);
    char uuidBuf[64];
    strncpy_s(uuidBuf, sizeof(uuidBuf), asset.uuid.c_str(), _TRUNCATE);
    ImGui::InputText("##AudUuid", uuidBuf, sizeof(uuidBuf));
    gui->endDisabled();

    gui->tableEnd();

    gui->spacing();

    // Play / Stop toggle button
    bool isPlaying = mgr->isAudioPreviewPlaying() &&
                     (mgr->audioPreviewAssetUuid == asset.uuid ||
                      mgr->audioPreviewSourceUuid == asset.uuid);
    const char *label = isPlaying ? "Stop" : "Play";

    if (isPlaying)
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.86f, 0.24f, 0.24f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.69f, 0.19f, 0.19f, 1.00f));
    }
    else
    {
        gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
        gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
    }

    if (ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
    {
        if (isPlaying)
        {
            mgr->stopAudioPreview();
        }
        else
        {
            mgr->startAudioPreviewByUuid(asset.uuid);
        }
    }

    gui->popStyleColor(2);
}

static void saveMetaJson(const fs::path &metaPath, const nlohmann::json &j)
{
    std::ofstream f(metaPath);
    if (f.is_open())
        f << j.dump(4);
}

static nlohmann::json loadMetaJson(const fs::path &metaPath)
{
    if (metaPath.empty())
        return nlohmann::json::object();
    try
    {
        if (!fs::exists(metaPath))
            return nlohmann::json::object();
        std::ifstream f(metaPath);
        if (!f.is_open())
            return nlohmann::json::object();
        return nlohmann::json::parse(f);
    }
    catch (...)
    {
        return nlohmann::json::object();
    }
}

// Mesh compression options
static const char *kMeshCompItems[] = {"None", "Low", "Medium", "High"};
// Tangent options
static const char *kTangentItems[] = {"Import", "Generate"};
// Animation compression options
static const char *kAnimCompItems[] = {"Off", "Keyframe Reduction", "Optimal"};

static void loadMeshSettings(const fs::path &metaPath,
                             float &scaleFactor, int &meshCompression, bool &generateCollider,
                             int &tangents, bool &generateLightmapUV, bool &importAnimation,
                             int &animCompression)
{
    auto j = loadMetaJson(metaPath);
    scaleFactor = j.value("scaleFactor", 1.0f);
    meshCompression = j.value("meshCompression", 0);
    generateCollider = j.value("generateCollider", false);
    tangents = j.value("tangents", 0);
    generateLightmapUV = j.value("generateLightmapUV", false);
    importAnimation = j.value("importAnimation", true);
    animCompression = j.value("animCompression", 0);
}

static void drawMeshImportSettings(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset, Manager *mgr)
{
    static kString lastUuid;
    static float scaleFactor = 1.0f;
    static int meshCompression = 0;
    static bool generateCollider = false;
    static int tangents = 0;
    static bool generateLightmapUV = false;
    static bool importAnimation = true;
    static int animCompression = 0;
    static bool dirty = false;

    if (asset.uuid != lastUuid)
    {
        lastUuid = asset.uuid;
        dirty = false;
        loadMeshSettings(asset.metaPath, scaleFactor, meshCompression, generateCollider, tangents, generateLightmapUV, importAnimation, animCompression);
    }

    if (!gui->collapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "MeshImportTable"))
        return;

    propLabel(gui, "Scale Factor");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("##ScaleFactor", &scaleFactor, 0.001f, 0.0001f, 100.0f, "%.4f"))
        dirty = true;

    propLabel(gui, "Mesh Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##MeshComp", &meshCompression, kMeshCompItems, IM_ARRAYSIZE(kMeshCompItems)))
        dirty = true;

    propLabel(gui, "Generate Collider");
    if (ImGui::Checkbox("##GenCollider", &generateCollider))
        dirty = true;

    propLabel(gui, "Tangents");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Tangents", &tangents, kTangentItems, IM_ARRAYSIZE(kTangentItems)))
        dirty = true;

    propLabel(gui, "Lightmap UVs");
    if (ImGui::Checkbox("##LightmapUV", &generateLightmapUV))
        dirty = true;

    propLabel(gui, "Import Animation");
    if (ImGui::Checkbox("##ImportAnim", &importAnimation))
        dirty = true;

    propLabel(gui, "Anim Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##AnimComp", &animCompression, kAnimCompItems, IM_ARRAYSIZE(kAnimCompItems)))
        dirty = true;

    propLabel(gui, "Material");
    if (ImGui::Button("Extract##Mat", ImVec2(-FLT_MIN, 0)))
    {
        // TODO: extract embedded materials from the mesh asset
    }

    propLabel(gui, "Texture");
    if (ImGui::Button("Extract##Tex", ImVec2(-FLT_MIN, 0)))
    {
        // TODO: extract embedded textures from the mesh asset
    }

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !dirty;
    if (wasDisabled)
        gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Mesh", ImVec2(btnW, 0)) && !asset.metaPath.empty())
    {
        auto j = loadMetaJson(asset.metaPath);
        j["scaleFactor"] = scaleFactor;
        j["meshCompression"] = meshCompression;
        j["generateCollider"] = generateCollider;
        j["tangents"] = tangents;
        j["generateLightmapUV"] = generateLightmapUV;
        j["importAnimation"] = importAnimation;
        j["animCompression"] = animCompression;
        saveMetaJson(asset.metaPath, j);
        dirty = false;

        // Re-convert the source model into Library/ImportedAssets with these
        // settings (scale / tangents / animation) and refresh its thumbnail.
        if (mgr && !asset.uuid.empty())
            mgr->reimportMesh(asset.uuid);
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Mesh", ImVec2(btnW, 0)))
    {
        loadMeshSettings(asset.metaPath, scaleFactor, meshCompression, generateCollider,
                         tangents, generateLightmapUV, importAnimation, animCompression);
        dirty = false;
    }
    if (wasDisabled)
        gui->endDisabled();
}

// Image type options ("Normal Map" = index 3; treated as linear tangent-space data)
static const char *kImgTypeItems[] = {"Texture", "GUI", "Sprite", "Normal Map"};
// Max size options
static const char *kMaxSizeItems[] = {"32", "64", "128", "256", "512", "1024", "2048", "4096"};
// Compression options
static const char *kImgCompItems[] = {"None", "Low Quality", "Normal Quality", "High Quality"};
// Alpha source options
static const char *kAlphaSrcItems[] = {"None", "Input Alpha", "From Grayscale"};
// Wrap mode options
static const char *kWrapModeItems[] = {"Repeat", "Clamp", "Mirror"};
// Filter mode options
static const char *kFilterModeItems[] = {"Point", "Bilinear", "Trilinear"};
// Normal-map gradient filtering (grayscale -> normal)
static const char *kNormalFilterItems[] = {"Sharp", "Smooth"};
// Channel / colour-space options
static const char *kChannelItems[] = {"sRGB", "Linear Color", "Linear Grayscale"};

static constexpr int kImgTypeNormalMap = 3;

// Backing state for the image import-settings panel.
struct ImgImportState
{
    int imageType = 0;
    int maxSizeIndex = 5;
    int compression = 2;
    int alphaSource = 0;
    int channel = 0; ///< 0=sRGB, 1=Linear Color, 2=Linear Grayscale.
    bool generateMipmap = true;
    int wrapMode = 0;
    int filterMode = 1;
    bool flipVertical = false;
    // Normal-map-only
    bool flipGreen = false;
    bool fromGrayscale = false;
    float bumpiness = 1.0f;
    int normalFilter = 0;
};

static void loadImageSettings(const fs::path &metaPath, ImgImportState &s)
{
    auto j = loadMetaJson(metaPath);
    s.imageType = j.value("imageType", 0);
    s.maxSizeIndex = j.value("maxSizeIndex", 5);
    s.compression = j.value("compression", 2);
    s.alphaSource = j.value("alphaSource", 0);
    // Migrate the old boolean sRGB flag: true -> sRGB (0), false -> Linear Color (1).
    s.channel = j.value("channel", j.value("sRGB", true) ? 0 : 1);
    s.generateMipmap = j.value("generateMipmap", true);
    s.wrapMode = j.value("wrapMode", 0);
    s.filterMode = j.value("filterMode", 1);
    s.flipVertical = j.value("flipVertical", false);
    s.flipGreen = j.value("flipGreen", false);
    s.fromGrayscale = j.value("fromGrayscale", false);
    s.bumpiness = j.value("bumpiness", 1.0f);
    s.normalFilter = j.value("normalFilter", 0);
}

static void drawImageImportSettings(kGuiManager *gui, const PanelProject::SelectedProjectAsset &asset, Manager *mgr)
{
    static kString lastUuid;
    static ImgImportState s;
    static bool dirty = false;

    if (asset.uuid != lastUuid)
    {
        lastUuid = asset.uuid;
        dirty = false;
        loadImageSettings(asset.metaPath, s);
    }

    if (!gui->collapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "ImageImportTable"))
        return;

    const bool isNormal = (s.imageType == kImgTypeNormalMap);

    propLabel(gui, "Image Type");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##ImgType", &s.imageType, kImgTypeItems, IM_ARRAYSIZE(kImgTypeItems)))
    {
        dirty = true;
        // Normal maps are linear data — default the Channel to Linear Color when
        // switching to Normal Map (the user can still override it).
        if (s.imageType == kImgTypeNormalMap && s.channel == 0)
            s.channel = 1;
    }

    // Normal-map-specific options.
    if (isNormal)
    {
        propLabel(gui, "Flip Green Channel");
        if (ImGui::Checkbox("##FlipGreen", &s.flipGreen))
            dirty = true;

        propLabel(gui, "Create From Grayscale");
        if (ImGui::Checkbox("##FromGray", &s.fromGrayscale))
            dirty = true;

        propLabel(gui, "Bumpiness");
        gui->setNextItemWidth(-FLT_MIN);
        if (!s.fromGrayscale)
            gui->beginDisabled(true);
        if (ImGui::DragFloat("##Bumpiness", &s.bumpiness, 0.05f, 0.0f, 20.0f))
            dirty = true;
        if (!s.fromGrayscale)
            gui->endDisabled();

        propLabel(gui, "Filtering");
        gui->setNextItemWidth(-FLT_MIN);
        // Filtering only affects the grayscale->normal gradient, so it's inert
        // for an already-authored normal map.
        if (!s.fromGrayscale)
            gui->beginDisabled(true);
        if (ImGui::Combo("##NormalFilter", &s.normalFilter, kNormalFilterItems, IM_ARRAYSIZE(kNormalFilterItems)))
            dirty = true;
        if (!s.fromGrayscale)
            gui->endDisabled();
    }

    propLabel(gui, "Max Size");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##MaxSize", &s.maxSizeIndex, kMaxSizeItems, IM_ARRAYSIZE(kMaxSizeItems)))
        dirty = true;

    propLabel(gui, "Compression");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##ImgComp", &s.compression, kImgCompItems, IM_ARRAYSIZE(kImgCompItems)))
        dirty = true;

    // Alpha source doesn't apply to normal maps (no alpha channel).
    if (!isNormal)
    {
        propLabel(gui, "Alpha Source");
        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##AlphaSrc", &s.alphaSource, kAlphaSrcItems, IM_ARRAYSIZE(kAlphaSrcItems)))
            dirty = true;
    }

    propLabel(gui, "Channel");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##Channel", &s.channel, kChannelItems, IM_ARRAYSIZE(kChannelItems)))
        dirty = true;

    propLabel(gui, "Generate Mipmap");
    if (ImGui::Checkbox("##GenMipmap", &s.generateMipmap))
        dirty = true;

    propLabel(gui, "Flip Vertically");
    if (ImGui::Checkbox("##FlipVert", &s.flipVertical))
        dirty = true;

    propLabel(gui, "Wrap Mode");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##WrapMode", &s.wrapMode, kWrapModeItems, IM_ARRAYSIZE(kWrapModeItems)))
        dirty = true;

    propLabel(gui, "Filter Mode");
    gui->setNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##FilterMode", &s.filterMode, kFilterModeItems, IM_ARRAYSIZE(kFilterModeItems)))
        dirty = true;

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !dirty;
    if (wasDisabled)
        gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Image", ImVec2(btnW, 0)) && !asset.metaPath.empty())
    {
        auto j = loadMetaJson(asset.metaPath);
        j["imageType"] = s.imageType;
        j["maxSizeIndex"] = s.maxSizeIndex;
        j["compression"] = s.compression;
        j["alphaSource"] = s.alphaSource;
        j["channel"] = s.channel;
        j["sRGB"] = (s.channel == 0); // keep legacy flag in sync
        j["generateMipmap"] = s.generateMipmap;
        j["wrapMode"] = s.wrapMode;
        j["filterMode"] = s.filterMode;
        j["flipVertical"] = s.flipVertical;
        j["flipGreen"] = s.flipGreen;
        j["fromGrayscale"] = s.fromGrayscale;
        j["bumpiness"] = s.bumpiness;
        j["normalFilter"] = s.normalFilter;
        saveMetaJson(asset.metaPath, j);
        dirty = false;

        // Re-process the source image into Library/ImportedAssets with these
        // settings, then reload it and refresh every material using it.
        if (mgr && !asset.uuid.empty())
            mgr->reimportTexture(asset.uuid);
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Image", ImVec2(btnW, 0)))
    {
        loadImageSettings(asset.metaPath, s);
        dirty = false;
    }
    if (wasDisabled)
        gui->endDisabled();
}

// ---------------------------------------------------------------------------
// Material inspector
// ---------------------------------------------------------------------------

static void loadMaterialJson(const fs::path &srcPath, nlohmann::json &out)
{
    if (srcPath.empty() || !fs::exists(srcPath))
    {
        out = nlohmann::json::object();
        return;
    }
    try
    {
        std::ifstream f(srcPath);
        if (f.is_open())
            f >> out;
        else
            out = nlohmann::json::object();
    }
    catch (...)
    {
        out = nlohmann::json::object();
    }
}

static void saveMaterialJson(const fs::path &srcPath, const nlohmann::json &j)
{
    std::ofstream f(srcPath);
    if (f.is_open())
        f << j.dump(4);
}

// Bring a pre-@var .mat (hardcoded top-level keys) into the new params format so
// the dynamic inspector shows its existing values. No-op if already migrated.
static void migrateLegacyMaterialJson(nlohmann::json &m)
{
    if (!m.is_object())
        return;
    if (m.contains("params") && m["params"].is_object())
        return;
    nlohmann::json p = nlohmann::json::object();
    auto move = [&](const char *legacy, const char *var)
    {
        if (m.contains(legacy))
            p[var] = m[legacy];
    };
    move("diffuse", "material.diffuse");
    move("ambient", "material.ambient");
    move("specular", "material.specular");
    move("shininess", "material.shininess");
    move("metallic", "material.metallic");
    move("roughness", "material.roughness");
    move("glossiness", "material.glossiness");
    move("uv_tiling", "material.tiling");
    move("texture_albedo", "albedoMap");
    move("texture_normal", "normalMap");
    move("texture_specular", "specularMap");
    move("texture_glossiness", "glossinessMap");
    move("texture_emissive", "emissiveMap");
    m["params"] = p;
}

void PanelInspector::drawMaterialInspector(const PanelProject::SelectedProjectAsset &asset)
{
    // Reload from file when selection changes
    if (asset.uuid != matInspUuid)
    {
        matInspUuid = asset.uuid;
        matInspDirty = false;
        fs::path srcPath;
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
            srcPath = manager->projectPath / "Assets" / it->second.path;
        loadMaterialJson(srcPath, matInspJson);
        migrateLegacyMaterialJson(matInspJson);
        ++matInspVersion;
    }

    if (!gui->collapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!beginPropTable(gui, "MatTable"))
        return;

    auto changed = [&]()
    { matInspDirty = true; ++matInspVersion; };

    // --- Shader (built-ins + hand-written raw shader assets) ---
    {
        propLabel(gui, "Shader");

        // Options: the four built-ins, then every raw .glsl/.hlsl shader asset.
        std::vector<std::string> labels = {"Unlit", "Phong", "PBR", "Terrain PBR"};
        std::vector<std::string> uuids = {"", "", "", ""}; // built-ins have no asset UUID
        for (const auto &kv : manager->fileMap)
        {
            if (kv.second.type != "shader")
                continue;
            labels.push_back(fs::path(kv.second.path).stem().string());
            uuids.push_back(kv.first);
        }

        // Current selection: a referenced raw shader (by UUID) wins; else the
        // built-in name.
        std::string curUuid = matInspJson.value("shader_uuid", std::string(""));
        std::string curName = matInspJson.value("shader", std::string("Phong"));
        int selected = 1; // Phong
        if (!curUuid.empty())
        {
            for (size_t i = 0; i < uuids.size(); ++i)
                if (uuids[i] == curUuid)
                {
                    selected = (int)i;
                    break;
                }
        }
        else
        {
            for (size_t i = 0; i < 4; ++i)
                if (labels[i] == curName)
                {
                    selected = (int)i;
                    break;
                }
        }

        std::vector<const char *> ptrs;
        ptrs.reserve(labels.size());
        for (auto &l : labels)
            ptrs.push_back(l.c_str());

        gui->setNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##MatShader", &selected, ptrs.data(), (int)ptrs.size()))
        {
            if (uuids[selected].empty())
            {
                matInspJson["shader"] = labels[selected]; // built-in
                matInspJson["shader_uuid"] = "";
            }
            else
            {
                matInspJson["shader"] = "Custom";
                matInspJson["shader_uuid"] = uuids[selected]; // raw shader asset
            }
            changed();
        }
    }

    // --- Dynamic parameters, driven by the shader's `// @var` annotations ---
    {
        std::string src = manager->getMaterialShaderSource(matInspJson);
        std::vector<ShaderVar> vars = parseShaderVars(src);

        // Ensure a params object so writes land in the new (@var) format.
        if (!matInspJson.contains("params") || !matInspJson["params"].is_object())
            matInspJson["params"] = nlohmann::json::object();
        nlohmann::json &params = matInspJson["params"];

        // Texture-asset dropdown list — texture-type images only (imageType 0).
        bool hasSampler = false;
        for (const ShaderVar &v : vars)
            if (v.type == "sampler2D" || v.type == "samplerCube")
            {
                hasSampler = true;
                break;
            }

        std::vector<std::string> texUuids = {""};
        std::vector<std::string> texNames = {"(None)"};
        if (hasSampler)
        {
            // Collect first, then sort by display name (case-insensitive) so the
            // dropdown lists textures alphabetically; "(None)" stays at the top.
            std::vector<std::pair<std::string, std::string>> tex; // {name, uuid}
            for (const auto &kv : manager->fileMap)
            {
                if (kv.second.type != "image")
                    continue;
                int imgType = 0;
                fs::path mp = manager->projectPath / "Library" / "Metadata" / (kv.first + ".json");
                try
                {
                    std::ifstream mf(mp);
                    if (mf)
                    {
                        nlohmann::json mj;
                        mf >> mj;
                        imgType = mj.value("imageType", 0);
                    }
                }
                catch (...)
                {
                }
                // 0 = Texture, 3 = Normal Map (both used by materials); skip GUI/Sprite.
                if (imgType != 0 && imgType != 3)
                    continue;
                tex.emplace_back(fs::path(kv.second.path).stem().string(), kv.first);
            }
            std::sort(tex.begin(), tex.end(), [](const auto &a, const auto &b)
                      {
                const std::string &x = a.first, &y = b.first;
                return std::lexicographical_compare(
                    x.begin(), x.end(), y.begin(), y.end(),
                    [](unsigned char c1, unsigned char c2) { return std::tolower(c1) < std::tolower(c2); }); });
            for (auto &t : tex)
            {
                texNames.push_back(t.first);
                texUuids.push_back(t.second);
            }
        }

        if (vars.empty())
            gui->textDisabled("No @var parameters in shader");

        for (const ShaderVar &v : vars)
        {
            propLabel(gui, v.label.c_str());
            kString id = "##mp_" + v.name;

            if (v.type == "vec3" || v.type == "vec4")
            {
                int n = (v.type == "vec4") ? 4 : 3;
                float c[4] = {1, 1, 1, 1};
                if (params.contains(v.name) && params[v.name].is_array())
                    for (int i = 0; i < n && i < (int)params[v.name].size(); ++i)
                        c[i] = params[v.name][i];
                bool ch = (n == 4) ? ImGui::ColorEdit4(id.c_str(), c) : ImGui::ColorEdit3(id.c_str(), c);
                if (ch)
                {
                    if (n == 4)
                        params[v.name] = {c[0], c[1], c[2], c[3]};
                    else
                        params[v.name] = {c[0], c[1], c[2]};
                    changed();
                }
            }
            else if (v.type == "vec2")
            {
                float t[2] = {1, 1};
                if (params.contains(v.name) && params[v.name].is_array())
                    for (int i = 0; i < 2 && i < (int)params[v.name].size(); ++i)
                        t[i] = params[v.name][i];
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat2(id.c_str(), t, 0.01f))
                {
                    params[v.name] = {t[0], t[1]};
                    changed();
                }
            }
            else if (v.type == "float")
            {
                float fdef = (v.name == "material.shininess")    ? 32.0f
                             : (v.name == "material.roughness")  ? 0.5f
                             : (v.name == "material.glossiness") ? 1.0f
                                                                 : 0.0f;
                float f = (params.contains(v.name) && params[v.name].is_number()) ? params[v.name].get<float>() : fdef;
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat(id.c_str(), &f, 0.01f))
                {
                    params[v.name] = f;
                    changed();
                }
            }
            else if (v.type == "int")
            {
                int iv = (params.contains(v.name) && params[v.name].is_number()) ? params[v.name].get<int>() : 0;
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt(id.c_str(), &iv))
                {
                    params[v.name] = iv;
                    changed();
                }
            }
            else if (v.type == "bool")
            {
                bool b = (params.contains(v.name) && params[v.name].is_boolean()) ? params[v.name].get<bool>() : false;
                if (ImGui::Checkbox(id.c_str(), &b))
                {
                    params[v.name] = b;
                    changed();
                }
            }
            else if (v.type == "sampler2D" || v.type == "samplerCube")
            {
                std::string cur = (params.contains(v.name) && params[v.name].is_string()) ? params[v.name].get<std::string>() : "";
                std::string caption = "(None)";
                for (size_t i = 0; i < texUuids.size(); ++i)
                    if (texUuids[i] == cur)
                    {
                        caption = texNames[i];
                        break;
                    }

                // Button shows the current texture name; clicking opens the picker.
                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::Button((caption + id).c_str(), ImVec2(-FLT_MIN, 0)))
                {
                    texPickerParam = v.name;
                    texPickerSelected = cur;
                    texPickerSearch[0] = '\0';
                    ImGui::OpenPopup("Select Texture##texpick");
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("PROJECT_ASSET"))
                    {
                        std::string dropped((const char *)pl->Data);
                        {
                            auto nl = dropped.find('\n');
                            if (nl != std::string::npos)
                                dropped = dropped.substr(0, nl);
                        }
                        auto fit = manager->fileMap.find(dropped);
                        if (fit != manager->fileMap.end() && fit->second.type == "image")
                        {
                            params[v.name] = dropped;
                            changed();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else
            {
                gui->textDisabled(("unsupported: " + v.type).c_str());
            }
        }

        // --- Texture picker modal (shared by every sampler slot) ------------
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(440, 500), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Select Texture##texpick", nullptr))
            {
                // Search field.
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##texsearch", "Search...", texPickerSearch, sizeof(texPickerSearch));
                std::string filter = texPickerSearch;
                std::transform(filter.begin(), filter.end(), filter.begin(),
                               [](unsigned char c)
                               { return (char)std::tolower(c); });

                bool doApply = false;

                // Scrollable thumbnail grid; leave room for the buttons row.
                float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
                ImGui::BeginChild("##texgrid", ImVec2(0, -footer), true);
                {
                    const float thumb = 72.0f;
                    const float cellW = thumb + 12.0f;
                    const float cellH = thumb + ImGui::GetTextLineHeightWithSpacing() + 6.0f;
                    float availw = ImGui::GetContentRegionAvail().x;
                    int cols = std::max(1, (int)(availw / cellW));
                    int col = 0;

                    for (size_t i = 0; i < texUuids.size(); ++i)
                    {
                        // Filter by name (index 0 is "None" — always shown).
                        if (i > 0 && !filter.empty())
                        {
                            std::string ln = texNames[i];
                            std::transform(ln.begin(), ln.end(), ln.begin(),
                                           [](unsigned char c)
                                           { return (char)std::tolower(c); });
                            if (ln.find(filter) == std::string::npos)
                                continue;
                        }

                        ImGui::PushID((int)i);
                        bool selected = (texUuids[i] == texPickerSelected);
                        ImVec2 cur0 = ImGui::GetCursorScreenPos();

                        if (ImGui::Selectable("##cell", selected,
                                              ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellW - 4.0f, cellH)))
                        {
                            texPickerSelected = texUuids[i];
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                doApply = true;
                        }

                        // Thumbnail (None has no preview — a plain framed box).
                        ImDrawList *dl = ImGui::GetWindowDrawList();
                        float ix = cur0.x + (cellW - 4.0f - thumb) * 0.5f;
                        float iy = cur0.y + 3.0f;
                        if (i == 0)
                        {
                            dl->AddRect(ImVec2(ix, iy), ImVec2(ix + thumb, iy + thumb),
                                        IM_COL32(120, 120, 130, 180));
                        }
                        else
                        {
                            ImTextureRef img = getFileTypeIcon("image");
                            if (manager->panelProject)
                                img = manager->panelProject->getThumbnailIcon(texUuids[i], img);
                            dl->AddImage(img, ImVec2(ix, iy), ImVec2(ix + thumb, iy + thumb));
                        }

                        // Centered, ellipsized name under the thumbnail.
                        std::string nm = fitTextWithEllipsisUtf8(gui, texNames[i], cellW - 6.0f);
                        float tw = ImGui::CalcTextSize(nm.c_str()).x;
                        dl->AddText(ImVec2(cur0.x + (cellW - 4.0f - tw) * 0.5f, iy + thumb + 3.0f),
                                    ImGui::GetColorU32(ImGuiCol_Text), nm.c_str());

                        if (texNames[i] != nm && ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", texNames[i].c_str());

                        ImGui::PopID();
                        if (++col < cols)
                            ImGui::SameLine();
                        else
                            col = 0;
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
                    if (!texPickerParam.empty())
                    {
                        params[texPickerParam] = texPickerSelected;
                        changed();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    // --- Single Sided (material render state, not a shader uniform) ---
    {
        propLabel(gui, "Single Sided");
        bool v = matInspJson.value("single_sided", true);
        if (ImGui::Checkbox("##MatSingle", &v))
        {
            matInspJson["single_sided"] = v;
            changed();
        }
    }

    gui->tableEnd();
    gui->spacing();

    bool wasDisabled = !matInspDirty;
    if (wasDisabled)
        gui->beginDisabled(true);
    float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Apply##Mat", ImVec2(btnW, 0)))
    {
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
        {
            fs::path srcPath = manager->projectPath / "Assets" / it->second.path;
            saveMaterialJson(srcPath, matInspJson);
            matInspDirty = false;
            fs::path thumbPath = manager->projectPath / "Library" / "Thumbnails" / (asset.uuid + ".png");
            if (fs::exists(thumbPath))
                fs::remove(thumbPath);
            manager->checkAssetChange();
            // Rebuild runtime materials on scene objects that reference this
            // .mat so the edited settings show up immediately in the scene.
            manager->reapplyStoredMaterials();
            if (manager->panelProject)
            {
                manager->panelProject->pendingSelectUuid = asset.uuid;
                manager->panelProject->triggerRefresh();
            }
        }
    }
    gui->sameLine(0, 4.0f);
    if (ImGui::Button("Revert##Mat", ImVec2(btnW, 0)))
    {
        // Reload from file, broadcast new version so preview reverts too
        fs::path srcPath;
        auto it = manager->fileMap.find(asset.uuid);
        if (it != manager->fileMap.end())
            srcPath = manager->projectPath / "Assets" / it->second.path;
        loadMaterialJson(srcPath, matInspJson);
        migrateLegacyMaterialJson(matInspJson);
        matInspDirty = false;
        ++matInspVersion;
    }
    if (wasDisabled)
        gui->endDisabled();
}

// ===========================================================================
// Animation preview — mirrors the model viewer pattern: standalone offscreen
// renderer, orbit camera, light toggle, LMB orbit / RMB light drag.
// ===========================================================================

void PanelInspector::initAnimPreviewScene()
{
    if (animPreviewScene)
        return;

    animPreviewWorld = createWorld(createAssetManager());
    animPreviewScene = animPreviewWorld->createScene("animPreviewer");
    animPreviewScene->setFrustumCullingEnabled(false);
    animPreviewScene->setShadowsEnabled(false);
    animPreviewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    animPreviewLight = animPreviewScene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    animPreviewLight->setPower(1.5f);

    animPreviewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    animPreviewCamera->setFOV(45.0f);
    animPreviewCamera->setAspectRatio(1.0f);
    animPreviewCamera->setNearClip(0.1f);
    animPreviewCamera->setFarClip(500.0f);
    animPreviewCamera->setLookAt(kVec3(0.0f));
    animPreviewCamera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void PanelInspector::updateAnimPreviewLight()
{
    if (!animPreviewLight || !animPreviewScene)
        return;
    if (!animPreviewLightEnabled)
    {
        animPreviewLight->setPower(0.0f);
        return;
    }
    animPreviewLight->setPower(1.5f);
    animPreviewScene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
    animPreviewLight->setRotation(kVec3(animPreviewLightPitch, animPreviewLightYaw, 0.0f));
}

void PanelInspector::frameAnimPreviewCamera()
{
    if (!animPreviewMesh || !animPreviewCamera)
        return;

    kAABB combined;
    std::function<void(kMesh *)> expand = [&](kMesh *m)
    {
        m->calculateModelMatrix();
        kAABB b = m->getWorldAABB();
        if (b.isValid()) { combined.expandBy(b.min); combined.expandBy(b.max); }
        for (kObject *c : m->getChildren())
            if (c->getType() == kNodeType::NODE_TYPE_MESH)
                expand(static_cast<kMesh *>(c));
    };
    expand(animPreviewMesh);

    animPreviewCenter = combined.isValid() ? combined.center() : kVec3(0.0f);
    kVec3 he = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
    float radius = glm::length(he);
    if (radius < 0.001f) radius = 1.0f;

    float fov = 45.0f;
    animPreviewCamDist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
    kVec3 dir = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
    float heAlong = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

    animPreviewCamera->setFOV(fov);
    animPreviewCamera->setNearClip(std::max(0.0001f, animPreviewCamDist - heAlong - radius * 0.05f));
    animPreviewCamera->setFarClip(animPreviewCamDist * 1000.0f);
}

void PanelInspector::drawAnimationPreview(const PanelProject::SelectedProjectAsset &asset)
{
    if (!animPreviewRenderer)
    {
        animPreviewRenderer = new kOffscreenRenderer(256, 256);
        animPreviewRenderer->setAssetManager(manager->getAssetManager());
    }

    initAnimPreviewScene();

    // Resolve the .animation file path
    fs::path animPath;
    auto it = manager->fileMap.find(asset.uuid);
    if (it != manager->fileMap.end())
        animPath = manager->projectPath / "Assets" / it->second.path;

    if (animPath.empty() || !fs::exists(animPath))
        { drawAssetThumbnail(gui, asset); return; }

    nlohmann::json j;
    try
    {
        std::ifstream f(animPath);
        if (!f.is_open()) { drawAssetThumbnail(gui, asset); return; }
        f >> j;
    }
    catch (...) { drawAssetThumbnail(gui, asset); return; }

    std::string animType = j.value("type", std::string("scene"));

    // Scene-type: simple info card
    if (animType != "mesh")
    {
        drawAssetThumbnail(gui, asset);
        gui->spacing();
        float dur = j.value("duration", 5.0f);
        float fps = j.value("fps", 30.0f);
        int numTracks = (int)j.value("tracks", nlohmann::json::array()).size();
        int numEvents = (int)j.value("events", nlohmann::json::array()).size();
        char infoBuf[256];
        snprintf(infoBuf, sizeof(infoBuf), "Duration: %.1fs\nFPS: %.0f\nTracks: %d\nEvents: %d",
                 dur, fps, numTracks, numEvents);
        ImGui::TextUnformatted(infoBuf);
        if (ImGui::Button("Open in Cinematic Editor", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        {
            if (manager->panelAnimation)
            {
                manager->panelAnimation->openFile(animPath.string());
                manager->pendingOpenAnimationEditor = true;
            }
        }
        return;
    }

    // --- Mesh animation ---
    std::string meshUuid = j.value("meshUuid", "");
    int startF = j.value("startFrame", 0);
    int endF   = j.value("endFrame", 30);
    if (meshUuid.empty()) { drawAssetThumbnail(gui, asset); return; }

    // Reload mesh/preview assets when selection changes
    if (animPreviewUuid != asset.uuid || animPreviewMeshUuid != meshUuid)
    {
        animPreviewUuid     = asset.uuid;
        animPreviewMeshUuid = meshUuid;
        animPreviewStartFrame = startF;
        animPreviewEndFrame   = endF;
        animPreviewStartFrameDraft = startF;
        animPreviewEndFrameDraft   = endF;

        // Load the root-motion options stored in the .animation file.
        animPreviewRootMotionRotation   = j.value("rootMotionRotation", false);
        animPreviewRootMotionPositionY  = j.value("rootMotionPositionY", false);
        animPreviewRootMotionPositionXZ = j.value("rootMotionPositionXZ", false);
        animPreviewRootMotionRotationDraft   = animPreviewRootMotionRotation;
        animPreviewRootMotionPositionYDraft  = animPreviewRootMotionPositionY;
        animPreviewRootMotionPositionXZDraft = animPreviewRootMotionPositionXZ;
        animPreviewLightEnabled = false;
        animPreviewRotX = 24.09f;
        animPreviewRotY = 26.57f;
        animPreviewLightYaw   = 45.0f;
        animPreviewLightPitch = 60.0f;
        isDraggingAPLight = false;
        isDraggingAPModel = false;

        if (animPreviewMesh && animPreviewScene)
            animPreviewScene->removeMesh(animPreviewMesh);
        animPreviewMesh = nullptr;
        delete animPreviewAnimator; animPreviewAnimator = nullptr;
        animPreviewClip = nullptr;
        delete animPreviewMat; animPreviewMat = nullptr;

        fs::path glbPath = manager->projectPath / "Library" / "ImportedAssets" / (meshUuid + ".glb");
        if (!fs::exists(glbPath)) { drawAssetThumbnail(gui, asset); return; }

        kAssetManager *am = manager->getAssetManager();
        if (!am) { drawAssetThumbnail(gui, asset); return; }

        animPreviewMesh = am->loadMesh(glbPath.generic_string());
        if (!animPreviewMesh) { drawAssetThumbnail(gui, asset); return; }

        // Phong material (supports bone skinning) — apply to submeshes that
        // don't already have a valid shader from the GLB file.  Use
        // setChildren=false and manually recurse (same pattern as
        // applyDefaultMaterial in the model viewer).
        kShader *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
        if (shader)
        {
            animPreviewMat = new kMaterial();
            animPreviewMat->setShader(shader);
            animPreviewMat->setDiffuseColor(kVec3(0.8f, 0.8f, 0.8f));
            animPreviewMat->setAmbientColor(kVec3(0.3f, 0.3f, 0.3f));
            animPreviewMat->setSpecularColor(kVec3(0.2f, 0.2f, 0.2f));
            animPreviewMat->setShininess(16.0f);

            std::function<void(kMesh *)> applyMat = [&](kMesh *m)
            {
                kMaterial *existing = m->getMaterial();
                if (!existing || !existing->getShader() ||
                    existing->getShader()->getShaderProgram() == 0)
                    m->setMaterial(animPreviewMat, false); // false = don't auto-apply to children
                for (kObject *c : m->getChildren())
                    if (c->getType() == NODE_TYPE_MESH)
                        applyMat(static_cast<kMesh *>(c));
            };
            applyMat(animPreviewMesh);
        }

        // Skeletal animation — search the entire hierarchy for bones
        // (the root may be just a transform container).  Load the clip
        // bound to the root mesh (so bone-name→id maps resolve correctly),
        // then share one kAnimator across every bone-bearing submesh.
        bool hasBones = false;
        {
            std::function<void(kMesh *)> checkBones = [&](kMesh *m)
            {
                if (hasBones) return;
                if (m->getBoneCount() > 0) { hasBones = true; return; }
                for (kObject *c : m->getChildren())
                    if (c->getType() == NODE_TYPE_MESH)
                        checkBones(static_cast<kMesh *>(c));
            };
            checkBones(animPreviewMesh);
        }
        if (hasBones)
        {
            // loadAnimation throws if the GLB has bone data but no animation
            // clips — that's legitimate (static skinned mesh), so guard it.
            try
            {
                kSkeletalAnimation *skel = am->loadAnimation(glbPath.generic_string(), animPreviewMesh);
                if (skel)
                {
                    animPreviewClip = skel;
                    // Note: the root-motion flags are intentionally NOT applied
                    // to the preview clip — the preview always plays the full
                    // animation (root motion visible). The flags only take
                    // effect at game time, where the root motion is redirected
                    // to scripts as delta position/rotation.
                    animPreviewAnimator = new kAnimator(skel);

                    std::function<void(kMesh *)> applyAnim = [&](kMesh *m)
                    {
                        if (m->getBoneCount() > 0)
                            m->setAnimator(animPreviewAnimator);
                        for (kObject *c : m->getChildren())
                            if (c->getType() == NODE_TYPE_MESH)
                                applyAnim(static_cast<kMesh *>(c));
                    };
                    applyAnim(animPreviewMesh);

                    // Show the start-frame pose immediately instead of the
                    // bind pose while waiting for the user to press Play.
                    float startTicks = (animPreviewStartFrame / 30.0f) *
                        skel->getTicksPerSecond();
                    animPreviewAnimator->setCurrentTime(startTicks);
                    const kNodeData &initRoot = skel->getRootNode();
                    try
                    {
                        animPreviewAnimator->calculateBoneTransform(&initRoot, kMat4(1.0f));
                    }
                    catch (const std::exception &)
                    {
                        // Malformed keyframe channel — leave the bind pose.
                    }
                }
            }
            catch (const std::exception &)
            {
                // static skinned mesh — no animation clips, that's fine
            }
        }

        animPreviewScene->addMesh(animPreviewMesh);
        frameAnimPreviewCamera();

        animPreviewFrame   = (float)startF;
        animPreviewPlaying = false;
    }

    // Orbit camera around animPreviewCenter
    if (animPreviewCamera)
    {
        float pr = glm::radians(animPreviewRotX);
        float yr = glm::radians(animPreviewRotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr),
                     std::sin(pr),
                     std::cos(pr) * std::cos(yr));
        animPreviewCamera->setPosition(animPreviewCenter + camDir * animPreviewCamDist);
        animPreviewCamera->setLookAt(animPreviewCenter);
    }

    updateAnimPreviewLight();

    // Step animation
    float dt = ImGui::GetIO().DeltaTime;
    if (animPreviewPlaying && animPreviewAnimator)
    {
        animPreviewFrame += dt * 30.0f * animPreviewSpeed;
        if (animPreviewFrame > (float)animPreviewEndFrame)
        {
            if (animPreviewLoop)
                animPreviewFrame = (float)animPreviewStartFrame;
            else
            {
                animPreviewFrame  = (float)animPreviewEndFrame;
                animPreviewPlaying = false;
            }
        }
    }

    // Apply the current frame's pose whenever an animator exists. This runs
    // even when paused/stopped so pressing Stop snaps the mesh back to the
    // start frame instead of freezing on the last played pose.
    if (animPreviewAnimator && animPreviewAnimator->getCurrentAnimation())
    {
        // Convert frame count to animation time in ticks.  The animation
        // system stores keyframe timestamps in ticks (from Assimp's mTime);
        // setCurrentTime expects the same unit, otherwise interpolation
        // samples at the wrong scale and the pose appears frozen.
        float timeInSeconds = animPreviewFrame / 30.0f;
        float timeInTicks   = timeInSeconds *
            animPreviewAnimator->getCurrentAnimation()->getTicksPerSecond();
        animPreviewAnimator->setCurrentTime(timeInTicks);
        animPreviewAnimator->setSpeed(animPreviewPlaying ? animPreviewSpeed : 0.0f);

        // setCurrentTime only stores the time value — we must explicitly
        // walk the skeleton to produce the final bone matrices that the
        // shader will read via getFinalBoneMatrices().
        try
        {
            const kNodeData &root = animPreviewAnimator->getCurrentAnimation()->getRootNode();
            animPreviewAnimator->calculateBoneTransform(&root, kMat4(1.0f));
        }
        catch (const std::exception &)
        {
            // Keep the last successfully computed pose.
        }
    }

    // Render offscreen — use the builtin white/preview shader when lighting
    // is off (mirrors drawModelViewer behaviour) so the mesh stays visible
    // instead of going dark.  The builtin shader now supports skeletal
    // animation via finalBoneMatrices so the pose is preserved.
    if (animPreviewMesh && animPreviewCamera && animPreviewRenderer)
    {
        animPreviewRenderer->setBackgroundColor(kVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1.0f));
        if (animPreviewLightEnabled)
            animPreviewRenderer->render(animPreviewWorld, animPreviewScene, animPreviewCamera);
        else
            animPreviewRenderer->renderMesh(animPreviewMesh, animPreviewCamera);
    }

    // -----------------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------------
    float avail = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f - ImGui::GetStyle().ScrollbarSize;
    float sz = std::min(std::max(avail, 1.0f), 256.0f);
    float ox = std::max((avail - sz) * 0.5f, 0.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (animPreviewMesh && animPreviewRenderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)animPreviewRenderer->getTexture(),
                     screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                     ImVec2(0, 1), ImVec2(1, 0));
    }
    else
    {
        dl->AddRectFilled(screenPos, ImVec2(screenPos.x + sz, screenPos.y + sz),
                          IM_COL32(28, 28, 32, 255));
        const char *msg = "No preview available";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(screenPos.x + (sz - ts.x) * 0.5f,
                           screenPos.y + (sz - ts.y) * 0.5f),
                    IM_COL32(110, 115, 140, 200), msg);
    }

    // InvisibleButton captures mouse for both LMB and RMB drag
    ImGui::InvisibleButton("##animPreviewer", ImVec2(sz, sz),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImVec2 imgMax = ImVec2(screenPos.x + sz, screenPos.y + sz);

    bool pvHovered = ImGui::IsItemHovered();
    if (pvHovered || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // -----------------------------------------------------------------------
    // Light-bulb toggle (top-right corner)
    // -----------------------------------------------------------------------
    const float btR = 11.0f;
    const float btPad = 7.0f;
    ImVec2 btCenter = ImVec2(imgMax.x - btR - btPad, screenPos.y + btR + btPad);
    bool btHover = ImGui::IsMouseHoveringRect(
        ImVec2(btCenter.x - btR, btCenter.y - btR),
        ImVec2(btCenter.x + btR, btCenter.y + btR), false);

    ImU32 btFg = animPreviewLightEnabled
                     ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60, 220))
                     : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80, 80, 95, 200));
    dl->AddCircleFilled(btCenter, btR, IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float bx = btCenter.x, by = btCenter.y;
    ImU32 bulbInk = IM_COL32(25, 18, 5, animPreviewLightEnabled ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        animPreviewLightEnabled = !animPreviewLightEnabled;
    if (btHover)
        ImGui::SetTooltip("%s", animPreviewLightEnabled ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Scroll wheel = zoom (dolly the orbit camera toward/away from the pivot)
    // -----------------------------------------------------------------------
    float wheel = ImGui::GetIO().MouseWheel;
    if (pvHovered && wheel != 0.0f && animPreviewCamera)
    {
        // Scale by distance so zooming feels consistent for both tiny and large meshes.
        animPreviewCamDist -= wheel * animPreviewCamDist * 0.1f;
        animPreviewCamDist = std::clamp(animPreviewCamDist, 0.05f, 1000.0f);

        // The frame pass tightly fits the near/far clips to the framed distance.
        // Widen them after zooming so the mesh isn't clipped when dollied out.
        animPreviewCamera->setNearClip(std::max(0.0001f, animPreviewCamDist * 0.01f));
        animPreviewCamera->setFarClip(animPreviewCamDist * 1000.0f);
    }

    // -----------------------------------------------------------------------
    // Left-click drag = rotate model
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !btHover && !isDraggingAPModel)
        isDraggingAPModel = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        isDraggingAPModel = false;

    if (isDraggingAPModel)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        animPreviewRotY -= delta.x * 0.5f;
        animPreviewRotX += delta.y * 0.5f;
        animPreviewRotX = std::clamp(animPreviewRotX, -89.0f, 89.0f);
    }

    // -----------------------------------------------------------------------
    // Right-click drag = move light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !isDraggingAPLight)
        isDraggingAPLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        isDraggingAPLight = false;

    if (isDraggingAPLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        animPreviewLightYaw += delta.x * 0.015f;
        animPreviewLightPitch -= delta.y * 0.015f;
        animPreviewLightPitch = std::clamp(animPreviewLightPitch, -89.0f, 89.0f);
    }

    gui->spacing();

    // Progress bar
    float progress = 0.0f;
    int numFrames = (animPreviewEndFrame - animPreviewStartFrame);
    if (numFrames > 0)
        progress = (animPreviewFrame - (float)animPreviewStartFrame) / (float)numFrames;
    progress = std::clamp(progress, 0.0f, 1.0f);

    char frameLabel[64];
    snprintf(frameLabel, sizeof(frameLabel), "Frame %d / %d  (%.1f%%)",
             (int)animPreviewFrame, animPreviewEndFrame, progress * 100.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImGui::ProgressBar(progress, ImVec2(sz, 0), frameLabel);

    // Playback buttons
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    if (ImGui::Button(animPreviewPlaying ? "|| Pause" : "> Play", ImVec2(sz * 0.32f, 0)))
        animPreviewPlaying = !animPreviewPlaying;
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(sz * 0.32f, 0)))
    {
        animPreviewPlaying = false;
        animPreviewFrame   = (float)animPreviewStartFrame;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &animPreviewLoop);

    // Info row
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    char infoBuf[128];
    snprintf(infoBuf, sizeof(infoBuf), "Mesh: %s  |  Frames: %d - %d",
             meshUuid.substr(0, 12).c_str(), animPreviewStartFrame, animPreviewEndFrame);
    ImGui::TextUnformatted(infoBuf);

    // Start/End frame editing — these values live here in the inspector now
    // that mesh-extracted .animation files no longer open in the editor.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImGui::SetNextItemWidth((sz - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImGui::DragInt("Start Frame", &animPreviewStartFrameDraft, 1, 0, 1000);
    ImGui::SameLine();
    ImGui::SetNextItemWidth((sz - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImGui::DragInt("End Frame", &animPreviewEndFrameDraft, 1, 0, 1000);

    // -----------------------------------------------------------------------
    // Root Motion options — which root-bone channels are redirected to scripts
    // at game time (exposed as the animator's delta position/rotation) instead
    // of being applied to the object. The preview always shows the full
    // animation including root motion.
    // -----------------------------------------------------------------------
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImGui::Separator();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImGui::TextUnformatted("Root Motion");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);

    // The checkboxes take effect immediately — each toggle commits the value
    // and writes it to the .animation file (no Apply needed). The preview
    // always shows the full animation; the options only matter at game time,
    // where the root motion is redirected to scripts as delta position/rotation.
    auto saveRootMotionFlags = [&]()
    {
        animPreviewRootMotionRotation   = animPreviewRootMotionRotationDraft;
        animPreviewRootMotionPositionY  = animPreviewRootMotionPositionYDraft;
        animPreviewRootMotionPositionXZ = animPreviewRootMotionPositionXZDraft;
        try
        {
            nlohmann::json outJ;
            {
                std::ifstream in(animPath);
                if (in.is_open())
                    in >> outJ;
            }
            outJ["rootMotionRotation"]   = animPreviewRootMotionRotation;
            outJ["rootMotionPositionY"]  = animPreviewRootMotionPositionY;
            outJ["rootMotionPositionXZ"] = animPreviewRootMotionPositionXZ;
            std::ofstream out(animPath);
            if (out.is_open())
                out << outJ.dump(4);
        }
        catch (...) {}
    };

    if (ImGui::Checkbox("Root transform rotation", &animPreviewRootMotionRotationDraft))
        saveRootMotionFlags();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    if (ImGui::Checkbox("Root transform position (Y)", &animPreviewRootMotionPositionYDraft))
        saveRootMotionFlags();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    if (ImGui::Checkbox("Root transform position (XZ)", &animPreviewRootMotionPositionXZDraft))
        saveRootMotionFlags();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    if (ImGui::Button("Apply", ImVec2((sz - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 0)))
    {
        if (animPreviewEndFrameDraft < animPreviewStartFrameDraft)
            animPreviewEndFrameDraft = animPreviewStartFrameDraft;

        animPreviewStartFrame = animPreviewStartFrameDraft;
        animPreviewEndFrame   = animPreviewEndFrameDraft;

        // Commit the root-motion options (they take effect at game time; the
        // preview keeps showing the full animation including root motion).
        animPreviewRootMotionRotation   = animPreviewRootMotionRotationDraft;
        animPreviewRootMotionPositionY  = animPreviewRootMotionPositionYDraft;
        animPreviewRootMotionPositionXZ = animPreviewRootMotionPositionXZDraft;

        if (animPreviewFrame < (float)animPreviewStartFrame)
            animPreviewFrame = (float)animPreviewStartFrame;
        if (animPreviewFrame > (float)animPreviewEndFrame)
            animPreviewFrame = (float)animPreviewEndFrame;
        animPreviewPlaying = false;

        try
        {
            nlohmann::json outJ;
            {
                std::ifstream in(animPath);
                if (in.is_open())
                    in >> outJ;
            }
            outJ["startFrame"] = animPreviewStartFrame;
            outJ["endFrame"]   = animPreviewEndFrame;
            outJ["rootMotionRotation"]   = animPreviewRootMotionRotation;
            outJ["rootMotionPositionY"]  = animPreviewRootMotionPositionY;
            outJ["rootMotionPositionXZ"] = animPreviewRootMotionPositionXZ;
            std::ofstream out(animPath);
            if (out.is_open())
                out << outJ.dump(4);
        }
        catch (...) {}
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2((sz - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 0)))
    {
        animPreviewStartFrameDraft = animPreviewStartFrame;
        animPreviewEndFrameDraft   = animPreviewEndFrame;
        animPreviewRootMotionRotationDraft   = animPreviewRootMotionRotation;
        animPreviewRootMotionPositionYDraft  = animPreviewRootMotionPositionY;
        animPreviewRootMotionPositionXZDraft = animPreviewRootMotionPositionXZ;
    }
}

// ---------------------------------------------------------------------------
// Per-section prefab dirty detection
// ---------------------------------------------------------------------------

/** @brief Tracks which inspector sections of a prefab instance differ from the source template. */
struct PrefabDirtySections
{
    bool mesh    = false;
    bool light   = false;
    bool camera  = false;
    bool audio   = false;
    bool particle = false;
    bool scripts = false;
};

/** @brief Maps a top-level JSON key to the inspector section it belongs to. */
static const char *keyToSection(const std::string &key)
{
    // Mesh section keys
    if (key == "file_name" || key == "reference" || key == "material_uuid" ||
        key == "cast_shadow" || key == "receive_shadow")
        return "mesh";

    // Light section keys
    if (key == "power" || key == "diffuse" || key == "specular" ||
        key == "constant" || key == "linear" || key == "quadratic" ||
        key == "cutoff" || key == "outer_cutoff" || key == "light_type")
        return "light";

    // Camera section keys
    if (key == "fov" || key == "near_clip" || key == "far_clip" ||
        key == "aspect_ratio" || key == "scene_uuid" || key == "look_at" ||
        key == "up_axis" || key == "type")
        return "camera";

    // Audio section keys
    if (key == "audio_sources")
        return "audio";

    // Particle section keys
    if (key == "particles" || key == "particle")
        return "particle";

    // Scripts / physics / character / nav / listener keys
    if (key == "scripts" || key == "script" ||
        key == "physics_desc" || key == "physics" ||
        key == "character_desc" || key == "character" ||
        key == "nav_mesh_desc" || key == "navmesh_surface" ||
        key == "audio_listeners")
        return "scripts";

    return nullptr; // unknown or child key — not mapped to a top-level section
}

/**
 * @brief Compares the live instance JSON against the prefab template to determine
 *        which inspector sections have been modified (excluding transform).
 *
 * Skips identity keys (uuid, prefab_ref, template_uuid) and transform keys
 * (position, rotation, scale). Children are recursed into but their dirty status
 * is NOT propagated to parent sections — only top-level key changes matter.
 */
static PrefabDirtySections computePrefabDirtySections(
    const nlohmann::json &templateRoot,
    const nlohmann::json &instanceJson)
{
    PrefabDirtySections dirty;

    // Keys that are identity/transform/linkage or camera-derived and therefore
    // expected to differ between a prefab template and a live instance.
    auto isTransient = [](const std::string &k) -> bool {
        return k == "uuid" || k == "position" || k == "rotation" || k == "scale" ||
               k == "prefab_ref" || k == "template_uuid" ||
               k == "look_at" || k == "up_axis" ||
               k == "aspect_ratio" || k == "scene_uuid";
    };

    // Collect all top-level keys from both objects, skipping transients.
    std::set<std::string> keys;
    auto collectKeys = [&](const nlohmann::json &j, std::set<std::string> &out)
    {
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            const std::string &k = it.key();
            if (isTransient(k))
                continue;
            out.insert(k);
        }
    };
    collectKeys(templateRoot, keys);
    collectKeys(instanceJson, keys);

    for (const std::string &k : keys)
    {
        bool aHas = templateRoot.contains(k);
        bool bHas = instanceJson.contains(k);

        // Key missing in one side → dirty, unless the other side is an empty
        // array (older templates may omit keys that newer serialization emits as []).
        if (aHas != bHas)
        {
            if (!aHas && bHas && instanceJson[k].is_array() && instanceJson[k].empty())
                continue; // template missing key, instance has [] → equivalent
            if (!bHas && aHas && templateRoot[k].is_array() && templateRoot[k].empty())
                continue; // instance missing key, template has [] → equivalent

            const char *sec = keyToSection(k);
            if (sec)
            {
                if      (strcmp(sec, "mesh")     == 0) dirty.mesh     = true;
                else if (strcmp(sec, "light")    == 0) dirty.light    = true;
                else if (strcmp(sec, "camera")   == 0) dirty.camera   = true;
                else if (strcmp(sec, "audio")    == 0) dirty.audio    = true;
                else if (strcmp(sec, "particle") == 0) dirty.particle = true;
                else if (strcmp(sec, "scripts")  == 0) dirty.scripts  = true;
            }
            continue;
        }

        // Both have the key — compare values.
        if (templateRoot[k] != instanceJson[k])
        {
            const char *sec = keyToSection(k);
            if (sec)
            {
                if      (strcmp(sec, "mesh")     == 0) dirty.mesh     = true;
                else if (strcmp(sec, "light")    == 0) dirty.light    = true;
                else if (strcmp(sec, "camera")   == 0) dirty.camera   = true;
                else if (strcmp(sec, "audio")    == 0) dirty.audio    = true;
                else if (strcmp(sec, "particle") == 0) dirty.particle = true;
                else if (strcmp(sec, "scripts")  == 0) dirty.scripts  = true;
            }
        }
    }

    return dirty;
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------
void PanelInspector::draw(bool &opened)
{
    if (!opened)
        return;

    // Shader editor active and last focused: take over the inspector with the
    // live shader preview. Focusing another panel hands the inspector back.
    if (manager->shaderPreview.active && manager->lastFocusedPanel == Manager::FocusedPanel::Shader)
    {
        gui->windowStart("Inspector", &opened);
        drawShaderPreview();
        gui->windowEnd();
        return;
    }

    if (!manager->projectOpened)
        gui->beginDisabled(true);

    gui->windowStart("Inspector", &opened);
    focused = gui->isWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Animator editing takes priority while the animator editor is the last
    // focused panel, it currently owns focus, or its animation picker popup is
    // open. The Inspector's own focus is deliberately excluded so that clicking
    // the Inspector (e.g. after selecting a Project asset) does not yank the
    // Inspector back to the animator form.
    if (manager->panelAnimator != nullptr && manager->panelAnimator->visible &&
        (manager->lastFocusedPanel == Manager::FocusedPanel::Animator ||
         manager->panelAnimator->focused || manager->panelAnimator->isAnimPickerOpen()) &&
        (manager->panelAnimator->hasSelectedState() ||
         manager->panelAnimator->hasSelectedTransition()))
    {
        manager->panelAnimator->drawSelectedInspector();
        gui->windowEnd();
        if (!manager->projectOpened)
            gui->endDisabled();
        return;
    }

    // The Project panel drives the inspector when it was the last focused
    // panel. As a fallback, also show the project selection when no scene
    // object or scene is selected and no other panel owns an explicit context.
    bool projectFocused = (manager->lastFocusedPanel == Manager::FocusedPanel::Project);
    bool fallbackProject = !projectFocused &&
                           manager->getActiveSelectedObjects().empty() &&
                           manager->selectedScene == nullptr;
    if (manager->panelProject != nullptr && (projectFocused || fallbackProject))
    {
        auto asset = manager->panelProject->getProjectSelection();
        if (asset.count > 1)
        {
            gui->spacing();
            kString text = std::to_string(asset.count) + " items selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);
            gui->windowEnd();
            if (!manager->projectOpened)
                gui->endDisabled();
            return;
        }
        if (asset.count == 1)
        {
            if (asset.isFolder)
            {
                drawInlineIcon(iconFileFolder, "Folder");
                gui->sameLine(0, 4.0f);
                gui->textUnformatted(asset.name.c_str());
            }
            else
            {
                drawInlineIcon(getFileTypeIcon(asset.fileType), asset.fileType.c_str());
                gui->sameLine(0, 4.0f);
                gui->textUnformatted(asset.name.c_str());
            }
            gui->spacing();
            gui->separator();
            gui->spacing();
            if (!asset.isFolder && asset.fileType == "mesh")
                drawModelViewer(asset);
            else if (!asset.isFolder && asset.fileType == "material")
                drawMaterialViewer(asset);
            else if (!asset.isFolder && asset.fileType == "animation")
                drawAnimationPreview(asset);
            else if (!asset.isFolder && asset.fileType == "audio")
                drawAudioAssetPreview(gui, asset, manager);
            else
                drawAssetThumbnail(gui, asset);
            if (!asset.isFolder)
            {
                if (asset.fileType == "mesh")
                    drawMeshImportSettings(gui, asset, manager);
                else if (asset.fileType == "image")
                    drawImageImportSettings(gui, asset, manager);
                else if (asset.fileType == "material")
                    drawMaterialInspector(asset);
            }
            gui->windowEnd();
            if (!manager->projectOpened)
                gui->endDisabled();
            return;
        }
        if (projectFocused)
        {
            // The project is the active context but no asset is selected.
            gui->spacing();
            const char *text = "No asset selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);
            gui->windowEnd();
            if (!manager->projectOpened)
                gui->endDisabled();
            return;
        }
    }

    if (manager->worldSelected && !manager->hierarchyShowsPrefab)
    {
        drawInlineIcon(iconObjScene, "World");
        gui->sameLine(0, 4.0f);
        gui->textUnformatted("World");
        gui->spacing();
        gui->separator();
        gui->spacing();

        if (gui->collapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (beginPropTable(gui, "WorldTable"))
            {
                kWorld *world = manager->getWorld();
                propLabel(gui, "Active Camera");

                const auto &cams = world ? world->getCameras() : std::vector<kCamera *>{};
                std::vector<kCamera *> gameCams;
                for (kCamera *c : cams)
                    if (c != manager->editorCamera)
                        gameCams.push_back(c);

                int curIdx = -1;
                for (int i = 0; i < (int)gameCams.size(); ++i)
                    if (gameCams[i] == manager->defaultGameCamera)
                    {
                        curIdx = i;
                        break;
                    }

                std::vector<std::string> camNames;
                camNames.push_back("(None)");
                for (kCamera *c : gameCams)
                    camNames.push_back(c->getName().empty() ? "(unnamed)" : c->getName());

                int comboIdx = (curIdx >= 0) ? curIdx + 1 : 0;
                std::vector<const char *> camNamePtrs;
                for (auto &s : camNames)
                    camNamePtrs.push_back(s.c_str());

                gui->setNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##ActiveCamera", &comboIdx, camNamePtrs.data(), (int)camNamePtrs.size()))
                    manager->defaultGameCamera = (comboIdx == 0) ? nullptr : gameCams[comboIdx - 1];

                gui->tableEnd();
            }
        }
    }
    else if (manager->selectedScene != nullptr)
    {
        kScene *scene = manager->selectedScene;
        drawInlineIcon(iconObjScene, "Scene");
        gui->sameLine(0, 4.0f);
        {
            char nameBuf[256];
            strncpy_s(nameBuf, sizeof(nameBuf), scene->getName().c_str(), _TRUNCATE);
            gui->setNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##SceneName", nameBuf, sizeof(nameBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                kString before = scene->getName();
                kString after = kString(nameBuf);
                if (before != after)
                {
                    scene->setName(after);
                    kScene *cap = scene;
                    manager->undoRedo.push(std::make_unique<PropertyCommand>(
                        [cap, before]()
                        { cap->setName(before); },
                        [cap, after]()
                        { cap->setName(after); }));
                }
            }
        }
        gui->spacing();
        gui->separator();
        gui->spacing();
        drawSceneSection(gui, scene, manager);
    }
    else
    {

        size_t selCount = manager->getActiveSelectedObjects().size();

        if (selCount == 0)
        {
            gui->spacing();
            const char *text = "Nothing is selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);
        }
        else if (selCount > 1)
        {
            gui->spacing();
            kString text = std::to_string(selCount) + " objects selected";
            float tw = gui->calcTextSize(text).x;
            gui->setCursorPosX(gui->getCursorPosX() + (gui->getContentRegionAvail().x - tw) * 0.5f);
            gui->textDisabled(text);

        }
        else
        {
            kObject *obj = manager->getActiveSelectedObject();

            if (obj == nullptr)
            {
                gui->textDisabled("(object not found)");
            }
            else
            {
                kNodeType type = obj->getType();

                // Prefab instance roots use the prefab icon regardless of
                // underlying object type, consistent with the hierarchy panel.
                bool isPrefabRoot = !obj->getPrefabRef().empty();
                bool isPrefabDirty = isPrefabRoot && manager->isPrefabDirty(obj);

                ImTextureRef typeIcon = isPrefabRoot ? (isPrefabDirty ? iconObjPrefabEdited : iconObjPrefab) : iconObjMesh;
                const char *typeLabel = isPrefabRoot ? "Prefab" : "Mesh";

                if (!isPrefabRoot)
                {
                    // For kMesh objects flagged as terrain (via setSerializeType),
                    // show the terrain icon/label so the hierarchy and inspector
                    // remain consistent for terrain tiles.
                    if (type == NODE_TYPE_MESH)
                    {
                        kMesh *asMesh = dynamic_cast<kMesh *>(obj);
                        if (asMesh && asMesh->getSerializeType() == "terrain")
                        {
                            typeIcon = iconObjTerrain;
                            typeLabel = "Terrain";
                        }
                    }
                    if (type == NODE_TYPE_LIGHT)
                    {
                        typeIcon = iconObjLight;
                        typeLabel = "Light";
                    }
                    else if (type == NODE_TYPE_AUDIO)
                    {
                        // Differentiate Audio Source vs Audio Listener based on
                        // which component descriptors are attached to the object.
                        if (!obj->getAudioListeners().empty() && obj->getAudioSources().empty())
                        {
                            typeIcon  = iconObjAudioListener;
                            typeLabel = "Audio Listener";
                        }
                        else if (!obj->getAudioSources().empty() && obj->getAudioListeners().empty())
                        {
                            typeIcon  = iconObjAudio;
                            typeLabel = "Audio Source";
                        }
                        else
                        {
                            typeIcon  = iconObjAudio;
                            typeLabel = "Audio";
                        }
                    }
                    else if (type == NODE_TYPE_CAMERA)
                    {
                        typeIcon = iconObjCamera;
                        typeLabel = "Camera";
                    }
                    else if (type == NODE_TYPE_TERRAIN)
                    {
                        typeIcon = iconObjTerrain;
                        typeLabel = "Terrain";
                    }
                    else if (type == NODE_TYPE_DECAL)
                    {
                        typeIcon = iconObjMesh;
                        typeLabel = "Decal";
                    }
                }

                drawInlineIcon(typeIcon, typeLabel);
                gui->sameLine(0, 4.0f);

                {
                    char nameBuf[256];
                    strncpy_s(nameBuf, sizeof(nameBuf), obj->getName().c_str(), _TRUNCATE);
                    gui->setNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##ObjName", nameBuf, sizeof(nameBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        kString before = obj->getName();
                        kString after = kString(nameBuf);
                        if (before != after)
                        {
                            obj->setName(after);
                            kObject *cap = obj;
                            manager->undoRedo.push(std::make_unique<PropertyCommand>(
                                [cap, before]()
                                { cap->setName(before); },
                                [cap, after]()
                                { cap->setName(after); }));
                        }
                    }
                }

                {
                    bool active = obj->getActive();
                    bool prevActive = active;
                    if (gui->checkbox("Active", &active))
                    {
                        obj->setActive(active);
                        manager->projectSaved = false;
                        manager->refreshWindowTitle();
                        kObject *cap = obj;
                        bool after = active;
                        bool before = prevActive;
                        manager->undoRedo.push(std::make_unique<PropertyCommand>(
                            [cap, before]()
                            { cap->setActive(before); },
                            [cap, after]()
                            { cap->setActive(after); }));
                    }

                    gui->sameLine();
                    {
                        bool isStatic = obj->getStatic();
                        bool prevIsStatic = isStatic;
                        if (gui->checkbox("Static", &isStatic))
                        {
                            obj->setStatic(isStatic);
                            manager->getRenderer()->setOctreeDirty();
                            manager->projectSaved = false;
                            manager->refreshWindowTitle();
                            kObject *cap = obj;
                            bool after = isStatic;
                            bool before = prevIsStatic;
                            manager->undoRedo.push(std::make_unique<PropertyCommand>(
                                [cap, before]()
                                { cap->setStatic(before); },
                                [cap, after]()
                                { cap->setStatic(after); }));
                        }
                    }
                }

                // --- Tag dropdown (like Unity's tag selector) ----------------
                {
                    kString currentTag = obj->getTag();
                    if (currentTag.empty()) currentTag = "(Untagged)";
                    gui->alignTextToFramePadding();
                    gui->text("Tag");
                    gui->sameLine(0, 8.0f);
                    gui->setNextItemWidth(180.0f);
                    if (ImGui::BeginCombo("##ObjTag", currentTag.c_str()))
                    {
                        // Built-in "no tag" option.
                        if (ImGui::Selectable("(Untagged)", currentTag == "(Untagged)"))
                        {
                            obj->setTag("");
                            manager->projectSaved = false;
                        }
                        for (const auto &t : manager->tagSettings.tags)
                        {
                            bool selected = (currentTag == t);
                            if (ImGui::Selectable(t.c_str(), selected))
                            {
                                obj->setTag(t);
                                manager->projectSaved = false;
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("Edit Tags..."))
                            manager->showTagEditor = true;
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Assign a project-defined tag to this object.");
                    gui->spacing();
                }

                gui->spacing();
                gui->separator();
                gui->spacing();

                drawTransformSection(gui, obj, manager);
                gui->spacing();

                // Determine which sections of this prefab instance differ from
                // the source template, so only actually-modified property labels
                // turn yellow/orange (not every label on a dirty prefab).
                PrefabDirtySections dirtySections;
                if (isPrefabRoot)
                {
                    kString prefabUuid = obj->getPrefabRef();
                    nlohmann::json templateRoot;
                    auto it = manager->prefabTemplateCache.find(prefabUuid);
                    if (it != manager->prefabTemplateCache.end())
                    {
                        templateRoot = it->second;
                    }
                    else
                    {
                        // Fallback: load the template on the fly (populates cache too).
                        manager->isPrefabDirty(obj);
                        auto it2 = manager->prefabTemplateCache.find(prefabUuid);
                        if (it2 != manager->prefabTemplateCache.end())
                            templateRoot = it2->second;
                    }
                    if (!templateRoot.is_null())
                    {
                        nlohmann::json instanceJson = obj->serialize();
                        dirtySections = computePrefabDirtySections(templateRoot, instanceJson);
                    }
                }

                if (type == NODE_TYPE_MESH)
                    drawMeshSection(gui, static_cast<kMesh *>(obj), manager, manager->panelTerrain, dirtySections.mesh);
                else if (type == NODE_TYPE_LIGHT)
                    drawLightSection(gui, static_cast<kLight *>(obj), manager, dirtySections.light);
                else if (type == NODE_TYPE_CAMERA)
                    drawCameraSection(gui, static_cast<kCamera *>(obj), manager, dirtySections.camera);
                else if (type == NODE_TYPE_AUDIO && !obj->getAudioSources().empty())
                    drawAudioSection(gui, obj, manager, dirtySections.audio, iconFileAudio);
                else if (type == NODE_TYPE_DECAL)
                    drawDecalSection(gui, static_cast<kDecal *>(obj), manager);

                drawParticleSection(gui, obj, manager, dirtySections.particle);

                drawAudioListenerSection(gui, obj, manager, dirtySections.scripts);

                drawScriptsSection(gui, obj, manager, dirtySections.scripts);

                // --- Prefab Apply/Revert buttons ----------------------------
                // When a prefab instance has non-transform modifications, show
                // "Apply" (overwrite the prefab source) and "Revert" (discard
                // changes and reload from the prefab) at the bottom.
                if (isPrefabRoot)
                {
                    bool dirty = manager->isPrefabDirty(obj);
                    if (dirty)
                    {
                        gui->spacing();
                        gui->separator();
                        gui->spacing();

                        float btnW = (gui->getContentRegionAvail().x - 4.0f) * 0.5f;

                        // --- Apply button ---
                        {
                            gui->pushStyleColor(ImGuiCol_Button, kVec4(0.26f, 0.59f, 0.98f, 1.00f));
                            gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.26f, 0.59f, 0.98f, 0.85f));
                            if (ImGui::Button("Apply##Prefab", ImVec2(btnW, 0)))
                            {
                                // Save instance changes (excluding transform) to prefab source,
                                // then refresh all instances so they pick up the changes.
                                kString prefabRef = obj->getPrefabRef(); // capture before potential deletion
                                manager->applyPrefabInstance(obj);
                                manager->clearPrefabTemplateCache();
                                manager->refreshAllPrefabInstances(prefabRef);
                                if (manager->panelHierarchy)
                                    manager->panelHierarchy->refreshList();
                            }
                            gui->popStyleColor(2);
                        }

                        gui->sameLine(0, 4.0f);

                        // --- Revert button ---
                        {
                            gui->pushStyleColor(ImGuiCol_Button, kVec4(0.86f, 0.24f, 0.24f, 1.00f));
                            gui->pushStyleColor(ImGuiCol_ButtonHovered, kVec4(0.69f, 0.19f, 0.19f, 1.00f));
                            if (ImGui::Button("Revert##Prefab", ImVec2(btnW, 0)))
                            {
                                manager->revertPrefabInstance(obj);
                                manager->clearPrefabTemplateCache();
                                if (manager->panelHierarchy)
                                    manager->panelHierarchy->refreshList();
                            }
                            gui->popStyleColor(2);
                        }

                    }
                }

            }
        }

    } // end selectedScene else

    gui->windowEnd();

    if (!manager->projectOpened)
        gui->endDisabled();

    // --- Tag / Layer editor modals (drawn outside the inspector window) -----
    drawTagLayerEditorModals(manager);
}

// ---------------------------------------------------------------------------
// Tag / Layer editor modals
// ---------------------------------------------------------------------------
static void drawListEditorModal(Manager *manager, const char *title, bool &open,
                                std::vector<std::string> &list, char *newBuf,
                                size_t newBufSize, bool (*addFn)(Manager *, const std::string &),
                                void (*removeFn)(Manager *, const std::string &),
                                bool lockFirst)
{
    if (!open) return;
    if (!ImGui::Begin(title, &open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    // List existing entries with a remove button (first/locked entries cannot
    // be removed). Defer removal until after the loop to avoid iterator
    // invalidation while iterating.
    int removeIndex = -1;
    int idx = 0;
    for (const auto &entry : list)
    {
        bool locked = (lockFirst && idx == 0);
        ImGui::PushID(idx);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(entry.c_str());
        if (!locked)
        {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.0f);
            if (ImGui::SmallButton("X##Remove"))
                removeIndex = idx;
        }
        else
        {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
            ImGui::TextDisabled("(built-in)");
        }
        ImGui::PopID();
        ++idx;
    }

    if (removeIndex >= 0 && removeIndex < (int)list.size())
        removeFn(manager, list[removeIndex]);

    ImGui::Separator();

    // Add a new entry.
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##NewEntry", "New name...", newBuf, newBufSize,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::string name = newBuf;
        addFn(manager, name);
        newBuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        std::string name = newBuf;
        addFn(manager, name);
        newBuf[0] = '\0';
    }

    ImGui::End();
}

static void drawTagLayerEditorModals(Manager *manager)
{
    drawListEditorModal(manager, "Edit Tags", manager->showTagEditor,
                        manager->tagSettings.tags, manager->newTagBuf,
                        sizeof(manager->newTagBuf),
                        [](Manager *m, const std::string &n) { return m->addTag(n); },
                        [](Manager *m, const std::string &n) { m->removeTag(n); },
                        false);

    drawListEditorModal(manager, "Edit Layers", manager->showLayerEditor,
                        manager->layerSettings.layers, manager->newLayerBuf,
                        sizeof(manager->newLayerBuf),
                        [](Manager *m, const std::string &n) { return m->addLayer(n); },
                        [](Manager *m, const std::string &n) { m->removeLayer(n); },
                        true);
}
