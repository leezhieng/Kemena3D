#include "model_preview.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

ModelPreview::ModelPreview(kAssetManager *setAssetManager, int setSize)
    : assetManager(setAssetManager), size(setSize > 0 ? setSize : 256)
{
}

ModelPreview::~ModelPreview()
{
    clearMesh();
    delete renderer;
    delete camera;
    delete world; // owns scene and light
}

void ModelPreview::setAssetManager(kAssetManager *am)
{
    assetManager = am;
    if (renderer)
        renderer->setAssetManager(am);
}

void ModelPreview::resize(int newSize)
{
    if (newSize <= 0)
        return;
    size = newSize;
    if (renderer && (renderer->getWidth() != size || renderer->getHeight() != size))
        renderer->resize(size, size);
}

void ModelPreview::setMesh(kMesh *newMesh, bool takeOwnership)
{
    if (mesh == newMesh && meshOwned == takeOwnership)
        return;

    clearMesh();
    mesh = newMesh;
    meshOwned = takeOwnership;
    if (mesh)
    {
        ensureScene();
        if (scene)
            scene->addMesh(mesh);
    }
}

void ModelPreview::clearMesh()
{
    if (mesh && scene)
        scene->removeMesh(mesh);
    if (mesh && meshOwned)
        delete mesh;
    mesh = nullptr;
    meshOwned = false;
    animator = nullptr;
}

void ModelPreview::addContentObject(kObject *obj)
{
    if (obj)
        contentObjects.push_back(obj);
}

void ModelPreview::removeContentObject(kObject *obj)
{
    contentObjects.erase(std::remove(contentObjects.begin(), contentObjects.end(), obj),
                         contentObjects.end());
    if (scene && obj)
        scene->removeObject(obj);
}

void ModelPreview::clearContentObjects()
{
    if (scene)
        for (kObject *o : contentObjects)
            scene->removeObject(o);
    contentObjects.clear();
}

void ModelPreview::ensureScene()
{
    if (scene)
        return;

    world = createWorld(createAssetManager());
    scene = world->createScene("modelPreview");
    scene->setFrustumCullingEnabled(false);
    // No shadows in the preview window — a cast shadow adds nothing to a single
    // mesh and the depth pass can darken lit Phong/PBR materials.
    scene->setShadowsEnabled(false);
    scene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));

    light = scene->addSunLight(
        kVec3(0.0f, 3.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    light->setPower(1.5f);

    camera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    camera->setFOV(45.0f);
    camera->setAspectRatio(1.0f);
    camera->setNearClip(0.1f);
    camera->setFarClip(500.0f);
    camera->setLookAt(kVec3(0.0f));
    camera->setPosition(kVec3(0.0f, 0.3f, 3.0f));
}

void ModelPreview::updateLight()
{
    if (!light || !scene)
        return;

    if (lit)
    {
        light->setPower(1.5f);
        scene->setAmbientLightColor(kVec3(0.08f, 0.08f, 0.08f));
        light->setRotation(kVec3(lightPitch, lightYaw, 0.0f));
    }
    else
    {
        light->setPower(0.0f);
        // Scene-rendered previews fall back to flat ambient so the material is
        // still readable when the light is switched off.
        if (alwaysRenderScene)
            scene->setAmbientLightColor(kVec3(0.5f, 0.5f, 0.5f));
    }
}

void ModelPreview::frameToContent()
{
    if (!camera)
        return;
    if (!scene)
        ensureScene();
    if (!scene)
        return;

    // Compute the combined AABB of every mesh in the scene graph (this covers
    // both the single content mesh and any registered content objects such as
    // a spawned prefab hierarchy).
    kAABB combined;
    std::function<void(kObject *)> expand = [&](kObject *o)
    {
        if (o->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *m = static_cast<kMesh *>(o);
            m->calculateModelMatrix();
            kAABB b = m->getWorldAABB();
            if (b.isValid())
            {
                combined.expandBy(b.min);
                combined.expandBy(b.max);
            }
        }
        for (kObject *c : o->getChildren())
            expand(c);
    };
    for (kObject *c : scene->getRootNode()->getChildren())
        expand(c);

    center = combined.isValid() ? combined.center() : kVec3(0.0f);
    kVec3 he = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
    float radius = glm::length(he);
    if (radius < 0.001f)
        radius = 1.0f;

    const float fov = 45.0f;
    camDist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
    kVec3 dir = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
    float heAlong = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

    camera->setFOV(fov);
    camera->setNearClip(std::max(0.0001f, camDist - heAlong - radius * 0.05f));
    camera->setFarClip(camDist * 1000.0f);
}

void ModelPreview::resetView()
{
    rotX = 24.09f;
    rotY = 26.57f;
    camDist = 3.0f;
    center = kVec3(0.0f);
    lightYaw = 45.0f;
    lightPitch = 60.0f;
}

void ModelPreview::render(float dt)
{
    if (!renderer)
    {
        renderer = new kOffscreenRenderer(size, size);
        if (assetManager)
            renderer->setAssetManager(assetManager);
    }

    ensureScene();

    if (onUpdate)
        onUpdate(dt >= 0.0f ? dt : ImGui::GetIO().DeltaTime);

    // Apply the orbit position.
    if (camera)
    {
        float pr = glm::radians(rotX);
        float yr = glm::radians(rotY);
        kVec3 camDir(std::cos(pr) * std::sin(yr),
                     std::sin(pr),
                     std::cos(pr) * std::cos(yr));
        camera->setPosition(center + camDir * camDist);
        camera->setLookAt(center);
    }

    updateLight();

    if (!hasContent() || !camera || !renderer)
        return;

    renderer->setBackgroundColor(backgroundColor);
    // Scene render when lit, when the caller forces it, or when the content is
    // a scene graph (multiple objects) that renderMesh() cannot handle.
    if (lit || alwaysRenderScene || !mesh)
        renderer->render(world, scene, camera);
    else
        renderer->renderMesh(mesh, camera);
}

void ModelPreview::draw(const char *id, bool centered, float maxSize)
{
    float avail = ImGui::GetContentRegionAvail().x;
    float sz = std::min(std::max(avail, 1.0f), maxSize);
    float ox = centered ? std::max((avail - sz) * 0.5f, 0.0f) : 0.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
    ImVec2 screenPos = ImGui::GetCursorScreenPos();

    if (hasContent() && renderer)
    {
        dl->AddImage((ImTextureID)(uintptr_t)renderer->getTexture(),
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

    // InvisibleButton captures mouse for both LMB (orbit) and RMB (light) drag.
    ImGui::InvisibleButton(id, ImVec2(sz, sz),
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

    ImU32 btFg = lit
                     ? (btHover ? IM_COL32(255, 235, 100, 255) : IM_COL32(245, 215, 60, 220))
                     : (btHover ? IM_COL32(140, 140, 155, 255) : IM_COL32(80, 80, 95, 200));
    dl->AddCircleFilled(btCenter, btR, IM_COL32(15, 15, 20, 190));
    dl->AddCircleFilled(btCenter, btR - 1.5f, btFg);

    float bx = btCenter.x, by = btCenter.y;
    ImU32 bulbInk = IM_COL32(25, 18, 5, lit ? 230 : 160);
    dl->AddCircle(ImVec2(bx, by - 1.0f), 4.5f, bulbInk, 10, 1.3f);
    dl->AddLine(ImVec2(bx - 3.0f, by + 5.0f), ImVec2(bx + 3.0f, by + 5.0f), bulbInk, 1.3f);
    dl->AddLine(ImVec2(bx - 2.5f, by + 7.0f), ImVec2(bx + 2.5f, by + 7.0f), bulbInk, 1.3f);

    if (btHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        lit = !lit;
    if (btHover)
        ImGui::SetTooltip("%s", lit ? "Disable preview lighting" : "Enable preview lighting");

    // -----------------------------------------------------------------------
    // Scroll wheel = zoom (dolly the orbit camera toward/away from the pivot)
    // -----------------------------------------------------------------------
    float wheel = ImGui::GetIO().MouseWheel;
    if (pvHovered && wheel != 0.0f && camera)
    {
        // Scale by distance so zooming feels consistent for tiny and large meshes.
        camDist -= wheel * camDist * 0.1f;
        camDist = std::clamp(camDist, 0.05f, 1000.0f);

        // Widen the clip planes after zooming so the mesh isn't clipped.
        camera->setNearClip(std::max(0.0001f, camDist * 0.01f));
        camera->setFarClip(camDist * 1000.0f);
    }

    // -----------------------------------------------------------------------
    // Left-click drag = orbit the model
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !btHover && !draggingModel)
        draggingModel = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        draggingModel = false;

    if (draggingModel)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        rotY -= delta.x * 0.5f;
        rotX += delta.y * 0.5f;
        rotX = std::clamp(rotX, -89.0f, 89.0f);
    }

    // -----------------------------------------------------------------------
    // Right-click drag = rotate the key light
    // -----------------------------------------------------------------------
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !btHover && !draggingLight)
        draggingLight = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        draggingLight = false;

    if (draggingLight)
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        lightYaw += delta.x * 0.015f;
        lightPitch -= delta.y * 0.015f;
        lightPitch = std::clamp(lightPitch, -89.0f, 89.0f);
    }
}
