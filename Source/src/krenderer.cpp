#include "krenderer.h"
#include "kopengldriver.h"
#include <functional>

namespace kemena
{
    kRenderer::kRenderer()
    {
    }

    bool kRenderer::init(kWindow *window, kRendererType type)
    {
        if (window != nullptr)
            appWindow = window;

        renderType = type;

        if (renderType == kRendererType::RENDERER_GL)
        {
            driver = new kOpenGLDriver();
            if (!driver->init(window))
            {
                delete driver;
                driver = nullptr;
                return false;
            }
            kDriver::setCurrent(driver);

            if (window != nullptr)
            {
                fboWidth  = window->getWindowWidth();
                fboHeight = window->getWindowHeight();
            }
        }

        return true;
    }

    void kRenderer::destroy()
    {
        if (driver == nullptr) return;

        if (enableScreenBuffer)
        {
            if (fboMsaa)         { driver->deleteFramebuffer(fboMsaa);        fboMsaa = 0; }
            if (fboTexColorMsaa) { driver->deleteFBOTexture(fboTexColorMsaa); fboTexColorMsaa = 0; }
            if (rboDepthMsaa)    { driver->deleteRenderbuffer(rboDepthMsaa);  rboDepthMsaa = 0; }

            if (fbo)             { driver->deleteFramebuffer(fbo);            fbo = 0; }
            if (fboTexColor)     { driver->deleteFBOTexture(fboTexColor);     fboTexColor = 0; }
            if (rboDepth)        { driver->deleteRenderbuffer(rboDepth);      rboDepth = 0; }

            if (quadVao)         { driver->deleteVertexArray(quadVao);        quadVao = 0; }
            if (quadVbo)         { driver->deleteBuffer(quadVbo);             quadVbo = 0; }
            if (quadEbo)         { driver->deleteBuffer(quadEbo);             quadEbo = 0; }
        }

        if (enableShadow)
        {
            if (shadowFbo)      { driver->deleteFramebuffer(shadowFbo);   shadowFbo = 0; }
            if (shadowTexArray) { driver->deleteFBOTexture(shadowTexArray); shadowTexArray = 0; }
        }

        if (enablePicking)
        {
            if (pickFbo)      { driver->deleteFramebuffer(pickFbo);    pickFbo = 0; }
            if (pickFboTex)   { driver->deleteFBOTexture(pickFboTex);  pickFboTex = 0; }
            if (pickRboDepth) { driver->deleteRenderbuffer(pickRboDepth); pickRboDepth = 0; }
        }

        if (outlineShader)
        {
            delete outlineShader;
            outlineShader = nullptr;
        }

        if (debugAlbedoShader)  { delete debugAlbedoShader;  debugAlbedoShader  = nullptr; }
        if (debugNormalsShader) { delete debugNormalsShader; debugNormalsShader = nullptr; }
        if (debugWireShader)    { delete debugWireShader;    debugWireShader    = nullptr; }
        if (debugDepthShader)   { delete debugDepthShader;   debugDepthShader   = nullptr; }
        if (debugPickShader)    { delete debugPickShader;    debugPickShader    = nullptr; }

        if (debugLineShader) { delete debugLineShader; debugLineShader = nullptr; }
        if (debugLineVao)    { driver->deleteVertexArray(debugLineVao); debugLineVao = 0; }
        if (debugLineVbo)    { driver->deleteBuffer(debugLineVbo);      debugLineVbo = 0; }

        driver->destroy();
        delete driver;
        driver = nullptr;
    }

    void kRenderer::setEngineInfo(const kString name, uint32_t version)
    {
        engineName = name;
        engineVersion = version;
    }

    kWindow *kRenderer::getWindow()
    {
        return appWindow;
    }

    kDriver *kRenderer::getDriver()
    {
        return driver;
    }

    void kRenderer::clear()
    {
        if (enableScreenBuffer)
            driver->bindFramebuffer(fboMsaa);
        else
            driver->unbindFramebuffer();

        driver->setClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        driver->clear(true, true, true);
        driver->unbindFramebuffer();
    }

    // ---------------------------------------------------------------------------
    // Debug / render-mode visualization
    // ---------------------------------------------------------------------------

    // Shared vertex shader for all debug modes.
    // Outputs: vTexCoord, vNormal (world-space), vFragPos.
    static const char *kDebugVS = R"(
#version 330 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 2) in vec2 vertexTexCoord;
layout(location = 3) in vec3 vertexNormal;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4  weights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

const int MAX_BONES         = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

out vec2 vTexCoord;
out vec3 vNormal;

void main()
{
    vec4  pos = vec4(vertexPosition, 1.0);
    vec3  n   = vertexNormal;
    float tw  = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int  id = boneIDs[i];
        float w = weights[i];
        if (id < 0 || w <= 0.0) continue;
        if (id >= MAX_BONES) { pos = vec4(vertexPosition, 1.0); n = vertexNormal; break; }
        pos += finalBonesMatrices[id] * vec4(vertexPosition, 1.0) * w;
        n   += mat3(transpose(inverse(finalBonesMatrices[id]))) * vertexNormal * w;
        tw  += w;
    }
    if (tw == 0.0) { pos = vec4(vertexPosition, 1.0); n = vertexNormal; }

    vTexCoord = vertexTexCoord;
    vNormal   = normalize(mat3(transpose(inverse(modelMatrix))) * n);
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * pos;
}
)";

    // Albedo mode — sample first texture or fall back to diffuse color.
    static const char *kDebugAlbedoFS = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D debugTex;
uniform bool      hasDebugTex;
uniform vec3      diffuseColor;

void main()
{
    if (hasDebugTex)
        fragColor = texture(debugTex, vTexCoord);
    else
        fragColor = vec4(diffuseColor, 1.0);
}
)";

    // Normals mode — world-space normal as RGB.
    static const char *kDebugNormalsFS = R"(
#version 330 core
in vec3 vNormal;
out vec4 fragColor;

void main()
{
    fragColor = vec4(vNormal * 0.5 + 0.5, 1.0);
}
)";

    // Wireframe mode — flat light-grey fill (used with GL_LINE polygon mode).
    static const char *kDebugWireFS = R"(
#version 330 core
out vec4 fragColor;
void main() { fragColor = vec4(0.85, 0.85, 0.85, 1.0); }
)";

    // Depth mode — linearized depth as greyscale.
    static const char *kDebugDepthFS = R"(
#version 330 core
out vec4 fragColor;
uniform float near;
uniform float far;

void main()
{
    float z = gl_FragCoord.z * 2.0 - 1.0;
    float d = (2.0 * near * far) / (far + near - z * (far - near));
    float lin = clamp(d / far, 0.0, 1.0);
    fragColor = vec4(vec3(lin), 1.0);
}
)";

    // Full-screen quad VS (layout 0=pos, 1=UV) — shared by outline and pick-display passes.
    static const char *kOutlineVS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 texCoord;
void main()
{
    texCoord    = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

    // Object IDs display FS — hashes each object's integer ID to a bright,
    // hue-varied color so every object is visually distinct in the viewport.
    static const char *kPickDisplayFS = R"(
#version 330 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D pickTex;

vec3 idToColor(int id)
{
    if (id == 0) return vec3(0.0);
    // Golden-ratio hash → uniform hue distribution across all IDs
    float h = fract(float(id) * 0.618033988749895);
    // HSV (h, 0.75, 1.0) → RGB
    vec3 p = abs(fract(vec3(h) + vec3(1.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    return mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), 0.75);
}

void main()
{
    vec3 rgb = texture(pickTex, texCoord).rgb;
    int id = int(rgb.r * 255.0 + 0.5)
           + int(rgb.g * 255.0 + 0.5) * 256
           + int(rgb.b * 255.0 + 0.5) * 65536;
    fragColor = vec4(idToColor(id), 1.0);
}
)";

    // ID-boundary outline FS — draws outline where selected pixels border non-selected.
    static const char *kOutlineFS = R"(
#version 330 core
in vec2 texCoord;
out vec4 fragColor;

uniform sampler2D pickTex;
uniform vec2      viewportSize;
uniform int       selectedIds[256];
uniform int       numSelected;
uniform vec4      outlineColor;
uniform int       outlinePixels;

bool isSelected(vec3 rgb)
{
    int id = int(rgb.r * 255.0 + 0.5)
           + int(rgb.g * 255.0 + 0.5) * 256
           + int(rgb.b * 255.0 + 0.5) * 65536;
    if (id == 0) return false;
    for (int i = 0; i < numSelected; i++)
        if (selectedIds[i] == id) return true;
    return false;
}

void main()
{
    vec2 texel     = 1.0 / viewportSize;
    bool centerSel = isSelected(texture(pickTex, texCoord).rgb);

    if (centerSel) { fragColor = vec4(0.0); return; }

    bool nearSelected = false;
    for (int dx = -outlinePixels; dx <= outlinePixels && !nearSelected; dx++)
    {
        for (int dy = -outlinePixels; dy <= outlinePixels && !nearSelected; dy++)
        {
            float dist = length(vec2(float(dx), float(dy)));
            if (dist < 0.5 || dist > float(outlinePixels) + 0.5) continue;
            if (isSelected(texture(pickTex, texCoord + vec2(float(dx), float(dy)) * texel).rgb))
                nearSelected = true;
        }
    }

    fragColor = nearSelected ? outlineColor : vec4(0.0);
}
)";

    void kRenderer::render(kWorld *world, kScene *scene, int x, int y, int width, int height, float deltaTime, bool autoClearSwapWindow)
    {
        if (frameId > 999999999999)
            frameId = 0;
        else
            frameId++;

        // Render shadow scene (cascaded shadow mapping)
        if (shadowShader != nullptr && world->getMainCamera() != nullptr)
        {
            kCamera *shadowCam = world->getMainCamera();
            float camNear = shadowCam->getNearClip();
            float camFar  = shadowCam->getFarClip();
            kMat4 camView = shadowCam->getViewMatrix();

            // Practical split scheme: blend logarithmic and uniform by lambda.
            const int   cascCount = std::max(1, std::min(shadowCascadeCount, kMaxShadowCascades));
            const float lambda    = shadowSplitLambda;
            for (int i = 0; i < cascCount; ++i)
            {
                float ratio = (float)(i + 1) / (float)cascCount;
                float cLog  = camNear * std::pow(camFar / camNear, ratio);
                float cUni  = camNear + (camFar - camNear) * ratio;
                cascadeSplits[i] = lambda * cLog + (1.0f - lambda) * cUni;
            }

            // Find first active sun light
            kVec3 lightDir(0.0f, -1.0f, 0.0f);
            bool hasSun = false;
            for (size_t i = 0; i < scene->getLights().size(); ++i)
            {
                kLight *lt = scene->getLights().at(i);
                if (lt && lt->getActive() && lt->getLightType() == kLightType::LIGHT_TYPE_SUN)
                {
                    lightDir = glm::normalize(lt->getRotation() * kVec3(0.0f, -1.0f, 0.0f));
                    hasSun = true;
                    break;
                }
            }

            if (hasSun)
            {
                kVec3 up = (std::abs(glm::dot(lightDir, kVec3(0, 1, 0))) > 0.99f)
                               ? kVec3(1, 0, 0) : kVec3(0, 1, 0);

                driver->bindFramebuffer(shadowFbo);
                driver->setDepthTest(true);

                for (int cascade = 0; cascade < cascCount; ++cascade)
                {
                    float splitNear = (cascade == 0) ? camNear : cascadeSplits[cascade - 1];
                    float splitFar  = cascadeSplits[cascade];

                    // World-space corners of this cascade's view sub-frustum.
                    kMat4 subProj = glm::perspective(
                        glm::radians(shadowCam->getFOV()),
                        shadowCam->getAspectRatio(),
                        splitNear, splitFar);
                    kMat4 invPV = glm::inverse(subProj * camView);

                    kVec3 corners[8];
                    int idx = 0;
                    for (int ix = 0; ix < 2; ++ix)
                    for (int iy = 0; iy < 2; ++iy)
                    for (int iz = 0; iz < 2; ++iz)
                    {
                        kVec4 pt = invPV * kVec4(ix * 2.0f - 1.0f, iy * 2.0f - 1.0f, iz * 2.0f - 1.0f, 1.0f);
                        corners[idx++] = kVec3(pt / pt.w);
                    }

                    // Bounding sphere of the slice — its radius is independent of
                    // camera orientation, so the shadow box size stays constant
                    // and the map doesn't shimmer as the camera rotates.
                    kVec3 center(0.0f);
                    for (int c = 0; c < 8; ++c) center += corners[c];
                    center /= 8.0f;
                    float radius = 0.0f;
                    for (int c = 0; c < 8; ++c)
                        radius = std::max(radius, glm::length(corners[c] - center));
                    radius = std::ceil(radius * 16.0f) / 16.0f; // quantise to steady it further

                    // Texel-snap the sphere centre in light space to kill the
                    // shimmer that otherwise appears as the camera translates.
                    float texelsPerUnit = (float)shadowResolution / (2.0f * radius);
                    kMat4 snapView    = glm::scale(kMat4(1.0f), kVec3(texelsPerUnit)) *
                                        glm::lookAt(kVec3(0.0f), lightDir, up);
                    kMat4 snapViewInv = glm::inverse(snapView);
                    kVec4 cLS = snapView * kVec4(center, 1.0f);
                    cLS.x = std::floor(cLS.x);
                    cLS.y = std::floor(cLS.y);
                    center = kVec3(snapViewInv * cLS);

                    // Pull the eye well back along the light so casters between the
                    // light and the slice are still captured.
                    const float zExtent = radius * 6.0f;
                    kVec3 eye = center - lightDir * zExtent;
                    kMat4 lightView = glm::lookAt(eye, center, up);
                    kMat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * zExtent);
                    lightSpaceMatrices[cascade] = lightProj * lightView;

                    // Render the scene depth into this cascade's array layer.
                    driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, cascade);
                    driver->setViewport(0, 0, shadowResolution, shadowResolution);
                    driver->clear(false, true, false);
                    shadowShader->use();
                    renderSceneGraphShadow(world, scene, scene->getRootNode(), lightSpaceMatrices[cascade], deltaTime);
                    shadowShader->unuse();
                }
            }
        }

        driver->unbindFramebuffer();

        if (enableScreenBuffer)
        {
            resizeFbo(width, height);
            driver->bindFramebuffer(fboMsaa);
        }

        driver->setDepthTest(true);

        if (autoClearSwapWindow)
        {
            driver->setClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            driver->clear(true, true, true);
        }

        if (world->getMainCamera() != nullptr)
        {
            driver->setViewport(x, y, width, height);
            world->getMainCamera()->setAspectRatio((float)width / (float)height);

            // Render skybox (skip in non-full render modes)
            kMaterial *skyboxMaterial = scene->getSkyboxMaterial();
            kMesh *skyboxMesh = scene->getSkyboxMesh();
            bool skyboxAllowed = (renderMode == kRenderMode::RENDER_MODE_FULL ||
                                  renderMode == kRenderMode::RENDER_MODE_FULL_WIREFRAME);
            if (skyboxAllowed && skyboxMaterial != nullptr && skyboxMesh != nullptr)
            {
                if (skyboxMesh->getLoaded() && skyboxMaterial->getShader() != nullptr)
                {
                    kShader *skyboxShader = skyboxMaterial->getShader();
                    skyboxShader->use();

                    driver->setDepthTest(false);
                    driver->setDepthWrite(false);
                    driver->setCullFace(false);

                    skyboxShader->setValue("viewMatrix", kMat4(kMat3(world->getMainCamera()->getViewMatrix())));
                    skyboxShader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());

                    if (skyboxMaterial->getTextures().size() > 0 &&
                        skyboxMaterial->getTexture(0)->getType() == kTextureType::TEX_TYPE_CUBE)
                    {
                        uint32_t tex = skyboxMaterial->getTexture(0)->getTextureID();
                        driver->bindTextureCube(0, tex);
                        driver->setUniformInt(skyboxShader->getShaderProgram(), skyboxMaterial->getTexture(0)->getTextureName(), 0);
                    }

                    skyboxMesh->calculateModelMatrix();
                    skyboxMesh->draw();

                    for (size_t i = 0; i < skyboxMesh->getChildren().size(); ++i)
                    {
                        kMesh *childMesh = (kMesh *)skyboxMesh->getChildren().at(i);
                        if (childMesh != nullptr)
                        {
                            childMesh->calculateModelMatrix();
                            childMesh->draw();
                        }
                    }

                    skyboxShader->unuse();
                }
            }

            driver->setDepthTest(true);
            driver->setDepthWrite(true);
            driver->setCullFace(true);

            // Build octree and populate visible mesh set for frustum culling
            visibleMeshSet.clear();
            currentFrustumValid = false;
            kCamera *frustumCam = cullingCamera ? cullingCamera : world->getMainCamera();
            if (octreeCullingEnabled && frustumCam != nullptr)
            {
                currentFrustum.extractFromMatrix(frustumCam->getProjectionMatrix() *
                                                 frustumCam->getViewMatrix());
                currentFrustumValid = true;

                if (octreeDirty)
                {
                    sceneOctree->build(scene);
                    octreeDirty = false;
                }
                auto visible = sceneOctree->queryVisible(currentFrustum);
                visibleMeshSet.insert(visible.begin(), visible.end());
            }

            if (renderMode == kRenderMode::RENDER_MODE_FULL ||
                renderMode == kRenderMode::RENDER_MODE_FULL_WIREFRAME)
            {
                renderSceneGraph(world, scene, scene->getRootNode(), false, deltaTime);
            }

            if (renderMode != kRenderMode::RENDER_MODE_FULL)
            {
                kShader *dbgShader = nullptr;
                bool wireframe = false;

                switch (renderMode)
                {
                    case kRenderMode::RENDER_MODE_ALBEDO:
                        if (!debugAlbedoShader) {
                            debugAlbedoShader = new kShader();
                            debugAlbedoShader->loadShadersCode(kDebugVS, kDebugAlbedoFS);
                        }
                        dbgShader = debugAlbedoShader;
                        break;
                    case kRenderMode::RENDER_MODE_NORMALS:
                        if (!debugNormalsShader) {
                            debugNormalsShader = new kShader();
                            debugNormalsShader->loadShadersCode(kDebugVS, kDebugNormalsFS);
                        }
                        dbgShader = debugNormalsShader;
                        break;
                    case kRenderMode::RENDER_MODE_WIREFRAME:
                        if (!debugWireShader) {
                            debugWireShader = new kShader();
                            debugWireShader->loadShadersCode(kDebugVS, kDebugWireFS);
                        }
                        dbgShader = debugWireShader;
                        wireframe = true;
                        break;
                    case kRenderMode::RENDER_MODE_DEPTH:
                        if (!debugDepthShader) {
                            debugDepthShader = new kShader();
                            debugDepthShader->loadShadersCode(kDebugVS, kDebugDepthFS);
                        }
                        dbgShader = debugDepthShader;
                        // Set camera near/far once before the traversal
                        dbgShader->use();
                        dbgShader->setValue("near", world->getMainCamera()->getNearClip());
                        dbgShader->setValue("far",  world->getMainCamera()->getFarClip());
                        dbgShader->unuse();
                        break;
                    case kRenderMode::RENDER_MODE_FULL_WIREFRAME:
                        if (!debugWireShader) {
                            debugWireShader = new kShader();
                            debugWireShader->loadShadersCode(kDebugVS, kDebugWireFS);
                        }
                        dbgShader = debugWireShader;
                        wireframe = true;
                        break;
                    default:
                        break;
                }

                if (dbgShader)
                    renderSceneGraphDebug(world, scene, scene->getRootNode(), dbgShader, wireframe);
            }
        }
        else
        {
            std::cout << "No main camera found" << std::endl;
        }

        driver->unbindFramebuffer();

        if (enableScreenBuffer)
        {
            // Blit MSAA to resolve FBO
            driver->bindReadFramebuffer(fboMsaa);
            driver->bindDrawFramebuffer(fbo);
            driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);

            // Render resolve texture to screen quad
            driver->unbindFramebuffer();
            driver->setDepthTest(false);
            driver->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            driver->clear(true, false, false);

            getScreenShader()->use();
            driver->bindTexture2D(0, fboTexColor);

            if (enableAutoExposure)
            {
                driver->generateMipmaps2D(fboTexColor);
                int mipLevel = (int)std::floor(std::log2(std::max(width, height)));
                driver->readTexture2DRGB(fboTexColor, mipLevel, averageLuminanceColor);
                averageLuminance = 0.2126f * averageLuminanceColor[0]
                                 + 0.7152f * averageLuminanceColor[1]
                                 + 0.0722f * averageLuminanceColor[2];
                float targetExposure = exposureKey / (averageLuminance + 0.001f);
                exposure = glm::mix(exposure, targetExposure, deltaTime * exposureAdaptationRate);
            }

            if (screenShader != nullptr)
            {
                screenShader->setValue("enable_autoExposure", enableAutoExposure);
                screenShader->setValue("exposure", exposure * 3.0f);
                screenShader->setValue("contrast", 1.01f);
                screenShader->setValue("gamma", 2.2f);
            }

            driver->drawIndexed(quadVao, 6);
            getScreenShader()->unuse();
        }

        driver->unbindVertexArray();

        if (autoClearSwapWindow && appWindow != nullptr)
            appWindow->swap();
    }

    void kRenderer::renderSceneGraph(kWorld *world, kScene *scene, kObject *currentNode, bool transparent, float deltaTime)
    {
        if (currentNode == nullptr || !currentNode->getActive()) return;

        currentNode->calculateModelMatrix();

        if (currentNode->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *currentMesh = (kMesh *)currentNode;

            if (currentMesh->getLoaded() && currentMesh->getMaterial() != nullptr)
            {
                // Frustum cull (skip if the scene has culling disabled)
                if (octreeCullingEnabled && currentFrustumValid && scene->getFrustumCullingEnabled())
                {
                    if (currentMesh->getStatic())
                    {
                        // Static: use octree result
                        if (!visibleMeshSet.empty() &&
                            visibleMeshSet.find(currentMesh) == visibleMeshSet.end())
                            goto renderChildren;
                    }
                    else
                    {
                        // Dynamic: direct per-mesh AABB test
                        if (!currentFrustum.intersectsAABB(currentMesh->getWorldAABB()))
                            goto renderChildren;
                    }
                }

                // Blend
                if (currentMesh->getMaterial()->getTransparent() == kTransparentType::TRANSP_TYPE_BLEND)
                {
                    driver->setBlend(true);
                    driver->setBlendFunc(kBlendFactor::SRC_ALPHA, kBlendFactor::ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    driver->setBlend(false);
                }

                // Face culling
                if (currentMesh->getMaterial()->getSingleSided())
                {
                    driver->setCullFace(true);
                    driver->setFrontFace(kFrontFace::CCW);
                    driver->setCullMode(currentMesh->getMaterial()->getCullBack() ? kCullMode::BACK : kCullMode::FRONT);
                }
                else
                {
                    driver->setCullFace(false);
                }

                if (currentMesh->getMaterial()->getShader() != nullptr)
                {
                    kShader *shader = currentMesh->getMaterial()->getShader();
                    shader->use();

                    shader->setValue("normalMatrix", currentMesh->getNormalMatrix());
                    shader->setValue("modelMatrix", currentMesh->getModelMatrixWorld());
                    shader->setValue("viewMatrix", world->getMainCamera()->getViewMatrix());
                    shader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());
                    shader->setValue("viewPos", world->getMainCamera()->getPosition());

                    shader->setValue("material.tiling",    currentMesh->getMaterial()->getUvTiling());
                    shader->setValue("material.ambient",   currentMesh->getMaterial()->getAmbientColor());
                    shader->setValue("material.diffuse",   currentMesh->getMaterial()->getDiffuseColor());
                    shader->setValue("material.specular",  currentMesh->getMaterial()->getSpecularColor());
                    shader->setValue("material.shininess", currentMesh->getMaterial()->getShininess());
                    shader->setValue("material.metallic",  currentMesh->getMaterial()->getMetallic());
                    shader->setValue("material.roughness", currentMesh->getMaterial()->getRoughness());

                    std::vector<kMat4> boneTransforms(128, kMat4(1.0f));
                    if (currentMesh->getSkinned() && currentMesh->getAnimator() != nullptr)
                    {
                        if (!currentMesh->getStatic())
                            currentMesh->getAnimator()->updateAnimation(
                                deltaTime * currentMesh->getAnimator()->getCurrentAnimation()->getSpeed(), frameId);
                        boneTransforms = currentMesh->getAnimator()->getFinalBoneMatrices();
                    }
                    shader->setValue("finalBonesMatrices", boneTransforms);

                    // Lights
                    int countSun = 0, countPoint = 0, countSpot = 0;
                    for (size_t j = 0; j < scene->getLights().size(); ++j)
                    {
                        kLight *light = scene->getLights().at(j);
                        if (light == nullptr || !light->getActive()) continue;

                        if (light->getLightType() == LIGHT_TYPE_SUN)
                        {
                            kString idx = std::to_string(countSun);
                            shader->setValue("sunLights[" + idx + "].power",     light->getPower());
                            shader->setValue("sunLights[" + idx + "].direction", glm::normalize(light->getRotation() * kVec3(0.0f, -1.0f, 0.0f)));
                            shader->setValue("sunLights[" + idx + "].diffuse",   light->getDiffuseColor());
                            shader->setValue("sunLights[" + idx + "].specular",  light->getSpecularColor());
                            countSun++;
                        }
                        else if (light->getLightType() == LIGHT_TYPE_POINT)
                        {
                            kString idx = std::to_string(countPoint);
                            shader->setValue("pointLights[" + idx + "].power",     light->getPower());
                            shader->setValue("pointLights[" + idx + "].position",  light->getPosition());
                            shader->setValue("pointLights[" + idx + "].constant",  light->getConstant());
                            shader->setValue("pointLights[" + idx + "].linear",    light->getLinear());
                            shader->setValue("pointLights[" + idx + "].quadratic", light->getQuadratic());
                            shader->setValue("pointLights[" + idx + "].diffuse",   light->getDiffuseColor());
                            shader->setValue("pointLights[" + idx + "].specular",  light->getSpecularColor());
                            countPoint++;
                        }
                        else if (light->getLightType() == LIGHT_TYPE_SPOT)
                        {
                            kString idx = std::to_string(countSpot);
                            shader->setValue("spotLights[" + idx + "].power",       light->getPower());
                            shader->setValue("spotLights[" + idx + "].position",    light->getPosition());
                            shader->setValue("spotLights[" + idx + "].direction",   glm::normalize(light->getRotation() * kVec3(0.0f, -1.0f, 0.0f)));
                            shader->setValue("spotLights[" + idx + "].cutOff",      light->getCutOff());
                            shader->setValue("spotLights[" + idx + "].outerCutOff", light->getOuterCutOff());
                            shader->setValue("spotLights[" + idx + "].constant",    light->getConstant());
                            shader->setValue("spotLights[" + idx + "].linear",      light->getLinear());
                            shader->setValue("spotLights[" + idx + "].quadratic",   light->getQuadratic());
                            shader->setValue("spotLights[" + idx + "].diffuse",     light->getDiffuseColor());
                            shader->setValue("spotLights[" + idx + "].specular",    light->getSpecularColor());
                            countSpot++;
                        }
                    }
                    shader->setValue("sunLightNum",   countSun);
                    shader->setValue("pointLightNum", countPoint);
                    shader->setValue("spotLightNum",  countSpot);

                    // Scene ambient
                    shader->setValue("sceneAmbient", scene->getAmbientLightColor());
                    shader->setValue("skyboxAmbientEnabled",  scene->getSkyboxAmbientEnabled());
                    shader->setValue("skyboxAmbientStrength", scene->getSkyboxAmbientStrength());

                    // Texture unit layout: [0..N-1] material, [N] shadow array, [N+1] skybox
                    unsigned int shadowUnit = (unsigned int)currentMesh->getMaterial()->getTextures().size();
                    unsigned int skyboxUnit = shadowUnit + 1;

                    // Bind skybox cubemap for IBL ambient
                    kMaterial *skyboxMaterial = scene->getSkyboxMaterial();
                    if (skyboxMaterial != nullptr && skyboxMaterial->getTextures().size() > 0 &&
                        skyboxMaterial->getTexture(0)->getType() == kTextureType::TEX_TYPE_CUBE)
                    {
                        driver->bindTextureCube((int)skyboxUnit, skyboxMaterial->getTexture(0)->getTextureID());
                        shader->setValue("skyboxMap", (int)skyboxUnit);
                    }

                    // Cascaded shadow maps (single depth-texture array).
                    int cascCount = std::max(1, std::min(shadowCascadeCount, kMaxShadowCascades));
                    driver->bindTexture2DArray((int)shadowUnit, shadowTexArray);
                    shader->setValue("shadowMapArray", (int)shadowUnit);
                    std::vector<kMat4> lsm(lightSpaceMatrices, lightSpaceMatrices + cascCount);
                    shader->setValue("lightSpaceMatrices", lsm);
                    // Up to 4 split distances packed into a vec4; cascadeCount says how many are valid.
                    shader->setValue("cascadeSplits",
                        kVec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]));
                    shader->setValue("cascadeCount",    cascCount);
                    shader->setValue("shadowResolution", (float)shadowResolution);
                    shader->setValue("shadowDebug",     shadowDebug);
                    shader->setValue("enableShadow",    enableShadow);
                    shader->setValue("receiveShadow",   currentMesh->getReceiveShadow());

                    // Material textures
                    for (size_t k = 0; k < currentMesh->getMaterial()->getTextures().size(); k++)
                    {
                        kTexture *tex = currentMesh->getMaterial()->getTexture(k);
                        if (tex == nullptr) continue;

                        if (tex->getType() == kTextureType::TEX_TYPE_2D)
                            driver->bindTexture2D((int)k, tex->getTextureID());
                        else if (tex->getType() == kTextureType::TEX_TYPE_CUBE)
                            driver->bindTextureCube((int)k, tex->getTextureID());

                        shader->setValue(tex->getTextureName().c_str(), (unsigned int)k);
                        shader->setValue("has_" + tex->getTextureName(), true);
                    }

                    currentMesh->draw();

                    // Unbind all texture units (material + 1 shadow array + 1 skybox)
                    int matUnits   = (int)currentMesh->getMaterial()->getTextures().size();
                    int totalUnits = matUnits + 2;
                    for (int k = totalUnits - 1; k >= 0; k--)
                    {
                        driver->unbindTexture2D(k);
                        driver->unbindTextureCube(k);
                    }
                    driver->unbindTexture2DArray(matUnits); // shadow array unit

                    shader->unuse();
                }
            }
        }
        else if (currentNode->getType() == kNodeType::NODE_TYPE_LIGHT)
        {
            kLight *currentLight = (kLight *)currentNode;

            if (world->getMainCamera() != nullptr && currentLight->getMaterial() != nullptr)
            {
                kMat4 view = lookAt(world->getMainCamera()->getPosition(),
                                   world->getMainCamera()->getLookAt(),
                                   world->getMainCamera()->calculateUp());
                kMat4 projection = glm::perspective(glm::radians(world->getMainCamera()->getFOV()),
                                                   world->getMainCamera()->getAspectRatio(),
                                                   world->getMainCamera()->getNearClip(),
                                                   world->getMainCamera()->getFarClip());

                if (currentLight->getMaterial()->getTransparent() == kTransparentType::TRANSP_TYPE_BLEND)
                {
                    driver->setBlend(true);
                    driver->setBlendFunc(kBlendFactor::SRC_ALPHA, kBlendFactor::ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    driver->setBlend(false);
                }

                if (currentLight->getMaterial()->getShader() != nullptr)
                {
                    kShader *shader = currentLight->getMaterial()->getShader();
                    shader->use();

                    shader->setValue("viewProjection",           projection * view);
                    shader->setValue("cameraRightWorldSpace",    kVec3(view[0][0], view[1][0], view[2][0]));
                    shader->setValue("cameraUpWorldSpace",       kVec3(view[0][1], view[1][1], view[2][1]));
                    shader->setValue("billboardPosition",        currentLight->getPosition());
                    shader->setValue("billboardSize",            kVec2(0.8f, 0.8f));
                    shader->setValue("color",                    currentLight->getDiffuseColor());

                    for (size_t l = 0; l < currentLight->getMaterial()->getTextures().size(); l++)
                    {
                        kTexture *tex = currentLight->getMaterial()->getTexture(l);
                        if (tex != nullptr && tex->getType() == kTextureType::TEX_TYPE_2D)
                        {
                            driver->bindTexture2D((int)l, tex->getTextureID());
                            driver->setUniformInt(shader->getShaderProgram(), "albedoMap", (int)l);
                        }
                    }

                    currentLight->draw();

                    driver->unbindTexture2D(0);
                    shader->unuse();
                }
            }
        }
        else if (currentNode->getType() == kNodeType::NODE_TYPE_OBJECT)
        {
            kObject *currentObject = currentNode;

            if (currentObject->getMaterial() != nullptr)
            {
                if (currentObject->getMaterial()->getTransparent() == kTransparentType::TRANSP_TYPE_BLEND)
                {
                    driver->setBlend(true);
                    driver->setBlendFunc(kBlendFactor::SRC_ALPHA, kBlendFactor::ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    driver->setBlend(false);
                }

                if (currentObject->getMaterial()->getShader() != nullptr)
                {
                    kShader *shader = currentObject->getMaterial()->getShader();
                    shader->use();

                    shader->setValue("modelMatrix",      currentObject->getModelMatrixWorld());
                    shader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
                    shader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());

                    currentObject->draw();

                    driver->unbindTexture2D(0);
                    shader->unuse();
                }
            }
        }

        renderChildren:
        // Recurse children
        for (size_t i = 0; i < currentNode->getChildren().size(); ++i)
        {
            if (currentNode->getChildren().at(i) != nullptr)
                renderSceneGraph(world, scene, currentNode->getChildren().at(i), transparent, deltaTime);
        }
    }

    void kRenderer::renderSceneGraphShadow(kWorld *world, kScene *scene, kObject *currentNode,
                                            const kMat4 &lightSpaceMatrix, float deltaTime)
    {
        if (currentNode == nullptr || !currentNode->getActive()) return;

        currentNode->calculateModelMatrix();

        if (currentNode->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *currentMesh = (kMesh *)currentNode;

            if (currentMesh->getCastShadow())
            {
                shadowShader->setValue("lightSpaceMatrix", lightSpaceMatrix);
                shadowShader->setValue("modelMatrix",      currentMesh->getModelMatrixWorld());

                std::vector<kMat4> boneTransforms(128, kMat4(1.0f));
                if (currentMesh->getSkinned() && currentMesh->getAnimator() != nullptr)
                {
                    if (!currentMesh->getStatic())
                        currentMesh->getAnimator()->updateAnimation(
                            deltaTime * currentMesh->getAnimator()->getCurrentAnimation()->getSpeed(), frameId);
                    boneTransforms = currentMesh->getAnimator()->getFinalBoneMatrices();
                }
                shadowShader->setValue("finalBonesMatrices", boneTransforms);

                currentMesh->draw();
            }
        }

        for (size_t i = 0; i < currentNode->getChildren().size(); ++i)
        {
            if (currentNode->getChildren().at(i) != nullptr)
                renderSceneGraphShadow(world, scene, currentNode->getChildren().at(i),
                                       lightSpaceMatrix, deltaTime);
        }
    }

    kVec4 kRenderer::getClearColor()
    {
        return clearColor;
    }

    void kRenderer::setClearColor(kVec4 newColor)
    {
        newColor.r = srgbToLinear(newColor.r);
        newColor.g = srgbToLinear(newColor.g);
        newColor.b = srgbToLinear(newColor.b);
        clearColor = newColor;
        if (driver)
            driver->setClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    }

    void kRenderer::setEnableScreenBuffer(bool newEnable, bool useDefaultShader)
    {
        enableScreenBuffer = newEnable;

        if (newEnable)
        {
            driver->setMultisample(true);
            driver->setSRGBEncoding(false);

            // Screen quad
            float quadVerts[] = {
                -1.0f, -1.0f,  0.0f, 0.0f,
                 1.0f, -1.0f,  1.0f, 0.0f,
                 1.0f,  1.0f,  1.0f, 1.0f,
                -1.0f,  1.0f,  0.0f, 1.0f,
            };
            uint32_t quadIndices[] = {0, 1, 2, 2, 3, 0};

            quadVao = driver->createVertexArray();
            quadVbo = driver->createBuffer();
            quadEbo = driver->createBuffer();
            driver->bindVertexArray(quadVao);
            driver->uploadVertexBuffer(quadVbo, quadVerts, sizeof(quadVerts));
            driver->setVertexAttribFloat(0, 2, 4 * sizeof(float), 0);
            driver->setVertexAttribFloat(1, 2, 4 * sizeof(float), 2 * sizeof(float));
            driver->uploadIndexBuffer(quadEbo, quadIndices, sizeof(quadIndices));
            driver->unbindVertexArray();

            // MSAA FBO
            fboMsaa         = driver->createFramebuffer();
            fboTexColorMsaa = driver->createFBOColorTextureMSAA(8, fboWidth, fboHeight);
            driver->attachFBOColorTextureMSAA(fboMsaa, fboTexColorMsaa);
            rboDepthMsaa = driver->createRenderbuffer();
            driver->setupRenderbufferMSAA(rboDepthMsaa, 8, fboWidth, fboHeight);
            driver->attachRenderbufferDepthStencil(fboMsaa, rboDepthMsaa);
            driver->bindFramebuffer(fboMsaa);
            driver->setFramebufferDrawBuffer();
            if (!driver->isFramebufferComplete())
            {
                std::cerr << "MSAA FBO not complete!" << std::endl;
                return;
            }
            driver->unbindFramebuffer();

            // Resolve FBO
            fbo         = driver->createFramebuffer();
            fboTexColor = driver->createFBOColorTexture(fboWidth, fboHeight);
            driver->attachFBOColorTexture(fbo, fboTexColor);
            rboDepth = driver->createRenderbuffer();
            driver->setupRenderbuffer(rboDepth, fboWidth, fboHeight);
            driver->attachRenderbufferDepthStencil(fbo, rboDepth);
            driver->bindFramebuffer(fbo);
            driver->setFramebufferDrawBuffer();
            if (!driver->isFramebufferComplete())
            {
                std::cerr << "Resolve FBO not complete!" << std::endl;
                return;
            }
            driver->unbindFramebuffer();

            if (useDefaultShader)
            {
                kString vertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

                kString fragmentShader = R"(#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform int enable_autoExposure;
uniform float exposure;
uniform float contrast;
uniform float gamma;

void main()
{
    vec3 color = texture(screenTexture, TexCoord).rgb;
    vec3 mapped = color * exposure;
    mapped = (mapped - 0.5) * contrast + 0.5;
    mapped = pow(max(mapped, vec3(0.0)), vec3(1.0 / gamma));
    FragColor = vec4(mapped, 1.0);
})";

                kShader *newScreenShader = new kShader();
                newScreenShader->loadShadersCode(vertexShader.c_str(), fragmentShader.c_str());
                setScreenShader(newScreenShader);
            }
        }
        else
        {
            driver->setSRGBEncoding(true);
        }
    }

    bool kRenderer::getEnableScreenBuffer()
    {
        return enableScreenBuffer;
    }

    void kRenderer::setScreenShader(kShader *newShader)
    {
        screenShader = newShader;
    }

    kShader *kRenderer::getScreenShader()
    {
        return screenShader;
    }

    void kRenderer::setEnableShadow(bool newEnable, bool useDefaultShader)
    {
        enableShadow = newEnable;

        if (newEnable)
        {
            // One depth-texture array (max cascades) + one FBO; the per-cascade
            // layer is attached during the shadow pass.
            shadowTexArray = driver->createFBODepthTextureArray(
                shadowResolution, shadowResolution, kMaxShadowCascades);
            shadowFbo = driver->createFramebuffer();
            driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, 0);

            if (!driver->isFramebufferComplete())
                std::cerr << "Shadow framebuffer is incomplete" << std::endl;
            driver->unbindFramebuffer();

            if (useDefaultShader)
            {
                kString vertexShader = R"(#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 6) in ivec4 boneIDs;
layout (location = 7) in vec4 weights;

uniform mat4 lightSpaceMatrix;
uniform mat4 modelMatrix;

const int MAX_BONES = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalPosition = vec4(vertexPosition, 1.0);
    float totalWeight = 0.0;

    for(int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if(boneID == -1 || weight <= 0.0) continue;
        if(boneID >= MAX_BONES) { totalPosition = vec4(vertexPosition, 1.0); break; }
        totalPosition += (finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0)) * weight;
        totalWeight += weight;
    }

    if (totalWeight == 0.0)
        totalPosition = vec4(vertexPosition, 1.0);

    gl_Position = lightSpaceMatrix * (modelMatrix * totalPosition);
})";

                kString fragmentShader = R"(#version 330 core
void main() {}
)";

                kShader *newShadowShader = new kShader();
                newShadowShader->loadShadersCode(vertexShader.c_str(), fragmentShader.c_str());
                setShadowShader(newShadowShader);
            }
        }
    }

    bool kRenderer::getEnableShadow()
    {
        return enableShadow;
    }

    void kRenderer::setShadowShader(kShader *newShader)
    {
        shadowShader = newShader;
    }

    kShader *kRenderer::getShadowShader()
    {
        return shadowShader;
    }

    void kRenderer::setShadowCascadeCount(int count)
    {
        shadowCascadeCount = std::max(1, std::min(count, kMaxShadowCascades));
    }

    void kRenderer::setShadowResolution(int resolution)
    {
        if (resolution <= 0 || resolution == shadowResolution)
            return;
        shadowResolution = resolution;

        // Rebuild the array texture at the new resolution if shadows are live.
        if (enableShadow && shadowTexArray)
        {
            driver->deleteFBOTexture(shadowTexArray);
            shadowTexArray = driver->createFBODepthTextureArray(
                shadowResolution, shadowResolution, kMaxShadowCascades);
            driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, 0);
            driver->unbindFramebuffer();
        }
    }

    void kRenderer::setEnableAutoExposure(bool newEnable)
    {
        enableAutoExposure = newEnable;
    }

    bool kRenderer::getEnableAutoExposure()
    {
        return enableAutoExposure;
    }

    void kRenderer::resizeFbo(int newWidth, int newHeight)
    {
        if (newWidth == fboWidth && newHeight == fboHeight) return;
        if (newWidth <= 0 || newHeight <= 0) return;

        driver->setMultisample(true);

        // Resize MSAA color texture and depth RBO
        driver->resizeFBOColorTextureMSAA(fboTexColorMsaa, 8, newWidth, newHeight);
        driver->setupRenderbufferMSAA(rboDepthMsaa, 8, newWidth, newHeight);

        // Re-attach to MSAA FBO
        driver->attachFBOColorTextureMSAA(fboMsaa, fboTexColorMsaa);
        driver->attachRenderbufferDepthStencil(fboMsaa, rboDepthMsaa);
        driver->bindFramebuffer(fboMsaa);
        driver->setFramebufferDrawBuffer();
        driver->unbindFramebuffer();

        // Resize resolve color texture and depth RBO
        driver->resizeFBOColorTexture(fboTexColor, newWidth, newHeight);
        driver->setupRenderbuffer(rboDepth, newWidth, newHeight);

        // Re-attach to resolve FBO
        driver->attachFBOColorTexture(fbo, fboTexColor);
        driver->attachRenderbufferDepthStencil(fbo, rboDepth);
        driver->bindFramebuffer(fbo);
        driver->setFramebufferDrawBuffer();
        driver->unbindFramebuffer();

        fboWidth  = newWidth;
        fboHeight = newHeight;
    }

    uint32_t kRenderer::getFboTexture()
    {
        return fboTexColor;
    }

    int kRenderer::getFboWidth()
    {
        return fboWidth;
    }

    int kRenderer::getFboHeight()
    {
        return fboHeight;
    }

    float kRenderer::srgbToLinear(float c)
    {
        return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    kVec3 kRenderer::idToRgb(unsigned int i)
    {
        int r = (i & 0x000000FF) >> 0;
        int g = (i & 0x0000FF00) >> 8;
        int b = (i & 0x00FF0000) >> 16;
        return kVec3(r, g, b);
    }

    unsigned int kRenderer::rgbToId(unsigned int r, unsigned int g, unsigned int b)
    {
        return r + g * 256 + b * 256 * 256;
    }

    // -------------------------------------------------------------------------
    // Color ID object picking
    // -------------------------------------------------------------------------

    void kRenderer::setEnableObjectPicking(bool enable, bool useDefaultShader)
    {
        enablePicking = enable;

        if (enable)
        {
            // Allocate picking FBO at 1×1; it will be resized on first use.
            pickFbo     = driver->createFramebuffer();
            pickFboTex  = driver->createFBOColorTexture(1, 1);
            driver->attachFBOColorTexture(pickFbo, pickFboTex);
            pickRboDepth = driver->createRenderbuffer();
            driver->setupRenderbuffer(pickRboDepth, 1, 1);
            driver->attachRenderbufferDepthStencil(pickFbo, pickRboDepth);
            driver->bindFramebuffer(pickFbo);
            driver->setFramebufferDrawBuffer();
            if (!driver->isFramebufferComplete())
                std::cerr << "Picking FBO not complete!" << std::endl;
            driver->unbindFramebuffer();
            pickFboWidth  = 1;
            pickFboHeight = 1;

            if (useDefaultShader)
            {
                const char *vertSrc = R"(#version 330 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4 weights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

const int MAX_BONES = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalPosition = vec4(vertexPosition, 1.0);
    float totalWeight = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID == -1 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES) { totalPosition = vec4(vertexPosition, 1.0); break; }
        totalPosition += (finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0)) * weight;
        totalWeight += weight;
    }

    if (totalWeight == 0.0)
        totalPosition = vec4(vertexPosition, 1.0);

    gl_Position = projectionMatrix * viewMatrix * modelMatrix * totalPosition;
})";

                const char *fragSrc = R"(#version 330 core
out vec4 fragColor;
uniform vec3 pickColor;

void main()
{
    fragColor = vec4(pickColor, 1.0);
})";

                kShader *newPickShader = new kShader();
                newPickShader->loadShadersCode(vertSrc, fragSrc);
                pickingShader = newPickShader;

                // Billboard picking shader (for lights, cameras, empty objects)
                const char *iconVertSrc = R"(#version 330 core
layout(location = 0) in vec3 vertexPosition;

uniform vec3  cameraRightWorldSpace;
uniform vec3  cameraUpWorldSpace;
uniform mat4  viewProjection;
uniform vec3  billboardPosition;
uniform vec2  billboardSize;

void main()
{
    vec3 worldPos = billboardPosition
        + cameraRightWorldSpace * vertexPosition.x * billboardSize.x
        + cameraUpWorldSpace    * vertexPosition.y * billboardSize.y;
    gl_Position = viewProjection * vec4(worldPos, 1.0);
})";

                kShader *newIconPickShader = new kShader();
                newIconPickShader->loadShadersCode(iconVertSrc, fragSrc);
                pickingIconShader = newIconPickShader;

                // Create a reusable billboard quad VAO
                float iconVerts[12] = {
                    -0.5f, -0.5f, 0.0f,
                     0.5f, -0.5f, 0.0f,
                    -0.5f,  0.5f, 0.0f,
                     0.5f,  0.5f, 0.0f,
                };
                pickingIconVAO = driver->createVertexArray();
                pickingIconVBO = driver->createBuffer();
                driver->bindVertexArray(pickingIconVAO);
                driver->uploadVertexBuffer(pickingIconVBO, iconVerts, sizeof(iconVerts));
                driver->setVertexAttribFloat(0, 3, 3 * sizeof(float), 0);
                driver->unbindVertexArray();
            }
        }
        else
        {
            if (pickFbo)      { driver->deleteFramebuffer(pickFbo);      pickFbo = 0; }
            if (pickFboTex)   { driver->deleteFBOTexture(pickFboTex);    pickFboTex = 0; }
            if (pickRboDepth) { driver->deleteRenderbuffer(pickRboDepth); pickRboDepth = 0; }
            delete pickingShader;     pickingShader     = nullptr;
            delete pickingIconShader; pickingIconShader = nullptr;
            if (pickingIconVAO) { driver->deleteVertexArray(pickingIconVAO); pickingIconVAO = 0; }
            if (pickingIconVBO) { driver->deleteBuffer(pickingIconVBO);      pickingIconVBO = 0; }
            pickFboWidth  = 0;
            pickFboHeight = 0;
        }
    }

    bool kRenderer::getEnableObjectPicking()
    {
        return enablePicking;
    }

    void kRenderer::renderSceneGraphPicking(kWorld *world, kScene *scene, kObject *currentNode)
    {
        if (currentNode == nullptr || !currentNode->getActive()) return;

        currentNode->calculateModelMatrix();

        if (currentNode->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *currentMesh = (kMesh *)currentNode;
            if (currentMesh->getLoaded())
            {
                kVec3 idColor = idToRgb(currentMesh->getId());
                pickingShader->setValue("modelMatrix", currentMesh->getModelMatrixWorld());
                pickingShader->setValue("pickColor",   kVec3(idColor.r / 255.0f,
                                                             idColor.g / 255.0f,
                                                             idColor.b / 255.0f));

                std::vector<kMat4> boneTransforms(128, kMat4(1.0f));
                if (currentMesh->getSkinned() && currentMesh->getAnimator() != nullptr)
                    boneTransforms = currentMesh->getAnimator()->getFinalBoneMatrices();
                pickingShader->setValue("finalBonesMatrices", boneTransforms);

                currentMesh->draw();
            }
        }
        else if (pickingIconShader && pickingIconVAO &&
                 (currentNode->getType() == kNodeType::NODE_TYPE_LIGHT ||
                  currentNode->getType() == kNodeType::NODE_TYPE_CAMERA ||
                  currentNode->getType() == kNodeType::NODE_TYPE_OBJECT) &&
                 currentNode->getMaterial() != nullptr &&
                 world->getMainCamera() != nullptr)
        {
            // Render icon billboard for non-mesh scene objects
            pickingShader->unuse();
            pickingIconShader->use();

            kMat4 view = lookAt(world->getMainCamera()->getPosition(),
                                world->getMainCamera()->getLookAt(),
                                world->getMainCamera()->calculateUp());
            kMat4 proj = glm::perspective(glm::radians(world->getMainCamera()->getFOV()),
                                          world->getMainCamera()->getAspectRatio(),
                                          world->getMainCamera()->getNearClip(),
                                          world->getMainCamera()->getFarClip());

            kVec3 idColor = idToRgb(currentNode->getId());
            pickingIconShader->setValue("viewProjection",        proj * view);
            pickingIconShader->setValue("cameraRightWorldSpace", kVec3(view[0][0], view[1][0], view[2][0]));
            pickingIconShader->setValue("cameraUpWorldSpace",    kVec3(view[0][1], view[1][1], view[2][1]));
            pickingIconShader->setValue("billboardPosition",     currentNode->getPosition());
            pickingIconShader->setValue("billboardSize",         kVec2(0.8f, 0.8f));
            pickingIconShader->setValue("pickColor",             kVec3(idColor.r / 255.0f,
                                                                       idColor.g / 255.0f,
                                                                       idColor.b / 255.0f));

            driver->drawArrays(pickingIconVAO, kPrimitiveType::TRIANGLE_STRIP, 4);

            pickingIconShader->unuse();
            pickingShader->use();
        }

        for (size_t i = 0; i < currentNode->getChildren().size(); ++i)
        {
            if (currentNode->getChildren().at(i) != nullptr)
                renderSceneGraphPicking(world, scene, currentNode->getChildren().at(i));
        }
    }

    static kObject *findObjectById(kObject *node, unsigned int id)
    {
        if (node == nullptr) return nullptr;
        if (node->getId() == id) return node;
        for (kObject *child : node->getChildren())
        {
            kObject *found = findObjectById(child, id);
            if (found) return found;
        }
        return nullptr;
    }

    void kRenderer::renderPickingPass(kWorld *world, kScene *scene, int viewWidth, int viewHeight)
    {
        if (!enablePicking || pickingShader == nullptr) return;
        if (!world || !scene || !world->getMainCamera()) return;
        if (viewWidth <= 0 || viewHeight <= 0) return;

        if (viewWidth != pickFboWidth || viewHeight != pickFboHeight)
        {
            driver->resizeFBOColorTexture(pickFboTex, viewWidth, viewHeight);
            driver->setupRenderbuffer(pickRboDepth, viewWidth, viewHeight);
            driver->attachFBOColorTexture(pickFbo, pickFboTex);
            driver->attachRenderbufferDepthStencil(pickFbo, pickRboDepth);
            pickFboWidth  = viewWidth;
            pickFboHeight = viewHeight;
        }

        driver->setSRGBEncoding(false);
        driver->bindFramebuffer(pickFbo);
        driver->setViewport(0, 0, viewWidth, viewHeight);
        driver->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        driver->clear(true, true, false);
        driver->setDepthTest(true);
        driver->setDepthWrite(true);
        driver->setBlend(false);
        driver->setCullFace(false);

        world->getMainCamera()->setAspectRatio((float)viewWidth / (float)viewHeight);
        pickingShader->use();
        pickingShader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
        pickingShader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());
        renderSceneGraphPicking(world, scene, scene->getRootNode());
        pickingShader->unuse();

        driver->unbindFramebuffer();
        if (!enableScreenBuffer)
            driver->setSRGBEncoding(true);

        // In Object IDs mode, blit the pick texture into the display FBO so the
        // world panel shows the raw color-coded ID buffer instead of the scene.
        if (renderMode == kRenderMode::RENDER_MODE_OBJECT_IDS && enableScreenBuffer && quadVao)
        {
            if (!debugPickShader)
            {
                debugPickShader = new kShader();
                debugPickShader->loadShadersCode(kOutlineVS, kPickDisplayFS);
            }

            driver->bindFramebuffer(fboMsaa);
            driver->setViewport(0, 0, fboWidth, fboHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);

            debugPickShader->use();
            driver->bindTexture2D(0, pickFboTex);
            debugPickShader->setValue("pickTex", 0);
            driver->drawIndexed(quadVao, 6);
            debugPickShader->unuse();
            driver->unbindTexture2D(0);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            driver->unbindFramebuffer();

            driver->bindReadFramebuffer(fboMsaa);
            driver->bindDrawFramebuffer(fbo);
            driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);
            driver->unbindFramebuffer();
        }
    }

    kObject *kRenderer::pickObject(kWorld *world, kScene *scene,
                                   int mouseX, int mouseY,
                                   int viewWidth, int viewHeight)
    {
        if (!enablePicking || pickingShader == nullptr) return nullptr;
        if (!world || !scene || !world->getMainCamera()) return nullptr;
        if (viewWidth <= 0 || viewHeight <= 0) return nullptr;

        // Re-render if FBO size doesn't match — handles first call and panel resize.
        if (viewWidth != pickFboWidth || viewHeight != pickFboHeight)
            renderPickingPass(world, scene, viewWidth, viewHeight);

        // Read back pixel. OpenGL origin is bottom-left; flip Y for screen coords.
        int glX = std::max(0, std::min(mouseX,                     pickFboWidth  - 1));
        int glY = std::max(0, std::min(pickFboHeight - 1 - mouseY, pickFboHeight - 1));

        driver->setSRGBEncoding(false);
        driver->bindFramebuffer(pickFbo);
        uint8_t r = 0, g = 0, b = 0, a = 0;
        driver->readPixelsRGBA(glX, glY, r, g, b, a);
        driver->unbindFramebuffer();
        if (!enableScreenBuffer)
            driver->setSRGBEncoding(true);

        unsigned int pickedId = rgbToId(r, g, b);
        if (pickedId == 0) return nullptr;
        return findObjectById(scene->getRootNode(), pickedId);
    }

    void kRenderer::setRenderMode(kRenderMode mode)
    {
        renderMode = mode;
    }

    kRenderMode kRenderer::getRenderMode()
    {
        return renderMode;
    }

    void kRenderer::renderSceneGraphDebug(kWorld *world, kScene *scene, kObject *currentNode,
                                          kShader *shader, bool wireframe)
    {
        if (!currentNode || !currentNode->getActive()) return;
        currentNode->calculateModelMatrix();

        if (currentNode->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *mesh = static_cast<kMesh *>(currentNode);
            if (mesh->getLoaded())
            {
                shader->use();
                shader->setValue("modelMatrix",      mesh->getModelMatrixWorld());
                shader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
                shader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());

                std::vector<kMat4> bones(128, kMat4(1.0f));
                if (mesh->getSkinned() && mesh->getAnimator())
                    bones = mesh->getAnimator()->getFinalBoneMatrices();
                shader->setValue("finalBonesMatrices", bones);

                // Bind first texture as albedo hint (used by albedo mode).
                bool hasTex = false;
                if (mesh->getMaterial() && !mesh->getMaterial()->getTextures().empty())
                {
                    kTexture *tex = mesh->getMaterial()->getTexture(0);
                    if (tex && tex->getType() == kTextureType::TEX_TYPE_2D)
                    {
                        driver->bindTexture2D(0, tex->getTextureID());
                        shader->setValue("debugTex",    0);
                        shader->setValue("hasDebugTex", true);
                        hasTex = true;
                    }
                }
                if (!hasTex)
                {
                    shader->setValue("hasDebugTex", false);
                    kVec3 diff = mesh->getMaterial()
                                    ? mesh->getMaterial()->getDiffuseColor()
                                    : kVec3(0.7f, 0.7f, 0.7f);
                    shader->setValue("diffuseColor", diff);
                }

                driver->setBlend(false);
                driver->setCullFace(false);

                if (wireframe)
                {
                    glEnable(GL_POLYGON_OFFSET_LINE);
                    glPolygonOffset(-1.0f, -1.0f);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                }

                mesh->draw();

                if (wireframe)
                {
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    glDisable(GL_POLYGON_OFFSET_LINE);
                }

                if (hasTex)
                    driver->unbindTexture2D(0);

                shader->unuse();
            }
        }

        for (size_t i = 0; i < currentNode->getChildren().size(); ++i)
            if (currentNode->getChildren().at(i))
                renderSceneGraphDebug(world, scene, currentNode->getChildren().at(i), shader, wireframe);
    }

    // ---------------------------------------------------------------------------
    // Outline rendering — screen-space ID buffer approach
    // ---------------------------------------------------------------------------
    // Shaders (kOutlineVS, kOutlineFS, kPickDisplayFS) are defined at file scope
    // above kRenderer::render() so they are visible to all methods.

    static const char *kDebugLineVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * viewMatrix * vec4(aPos, 1.0);
}
)";

    static const char *kDebugLineFS = R"(
#version 330 core
out vec4 outColor;
uniform vec3 lineColor;
void main() { outColor = vec4(lineColor, 1.0); }
)";

    static void appendCircle(std::vector<float> &verts, kVec3 center,
                             kVec3 axisU, kVec3 axisV, float radius, int segments = 32)
    {
        const float pi2 = 2.0f * glm::pi<float>();
        for (int i = 0; i < segments; ++i)
        {
            float a0 = pi2 * i / segments;
            float a1 = pi2 * (i + 1) / segments;
            kVec3 p0 = center + axisU * (radius * cosf(a0)) + axisV * (radius * sinf(a0));
            kVec3 p1 = center + axisU * (radius * cosf(a1)) + axisV * (radius * sinf(a1));
            verts.insert(verts.end(), { p0.x, p0.y, p0.z, p1.x, p1.y, p1.z });
        }
    }

    static void appendLine(std::vector<float> &verts, kVec3 a, kVec3 b)
    {
        verts.insert(verts.end(), { a.x, a.y, a.z, b.x, b.y, b.z });
    }

    static kObject *findNodeByUuid(kObject *node, const kString &uuid)
    {
        if (!node) return nullptr;
        if (node->getUuid() == uuid) return node;
        for (kObject *child : node->getChildren())
        {
            kObject *found = findNodeByUuid(child, uuid);
            if (found) return found;
        }
        return nullptr;
    }

    void kRenderer::renderOutline(kWorld *world, kScene *scene,
                                   const std::vector<kString> &selectedUuids,
                                   kVec4 color, float thickness)
    {
        if (!enableScreenBuffer || selectedUuids.empty() || !world || !scene) return;
        if (!world->getMainCamera()) return;
        if (!enablePicking || pickFboWidth <= 0 || pickFboHeight <= 0) return;

        // Lazy-compile the post-process outline shader.
        if (!outlineShader)
        {
            kShader *s = new kShader();
            s->loadShadersCode(kOutlineVS, kOutlineFS);
            outlineShader = s;
        }

        // Collect integer IDs of selected objects and all their descendants.
        // OBJ-loaded meshes have an empty root; geometry lives in child nodes.
        std::function<void(kObject *, std::vector<int> &)> collectIds;
        collectIds = [&](kObject *node, std::vector<int> &ids)
        {
            if (!node || !node->getActive()) return;
            ids.push_back((int)node->getId());
            for (kObject *child : node->getChildren())
                collectIds(child, ids);
        };

        std::vector<int> selectedIds;
        for (const auto &uuid : selectedUuids)
        {
            kObject *obj = findNodeByUuid(scene->getRootNode(), uuid);
            if (obj) collectIds(obj, selectedIds);
        }
        if (selectedIds.empty()) return;
        if ((int)selectedIds.size() > 256) selectedIds.resize(256);

        // Draw the outline into the MSAA FBO on top of the rendered scene.
        driver->bindFramebuffer(fboMsaa);
        driver->setViewport(0, 0, fboWidth, fboHeight);

        outlineShader->use();
        driver->bindTexture2D(0, pickFboTex);
        outlineShader->setValue("pickTex",       0);
        outlineShader->setValue("viewportSize",  kVec2((float)fboWidth, (float)fboHeight));
        outlineShader->setValue("numSelected",   (int)selectedIds.size());
        outlineShader->setValue("outlineColor",  color);
        outlineShader->setValue("outlinePixels", (int)std::max(1.0f, thickness));

        GLint idLoc = glGetUniformLocation(static_cast<GLuint>(outlineShader->getShaderProgram()), "selectedIds");
        if (idLoc >= 0)
            glUniform1iv(idLoc, (GLsizei)selectedIds.size(), selectedIds.data());

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        driver->drawIndexed(quadVao, 6);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        outlineShader->unuse();
        driver->unbindTexture2D(0);

        // Blit updated MSAA buffer to the resolve FBO (display texture).
        driver->bindReadFramebuffer(fboMsaa);
        driver->bindDrawFramebuffer(fbo);
        driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);
        driver->unbindFramebuffer();
    }

    void kRenderer::renderDebugShapes(kWorld *world, kScene *scene,
                                      const std::vector<kString> &selectedUuids)
    {
        if (!enableScreenBuffer || !world || !scene || selectedUuids.empty()) return;
        if (!world->getMainCamera()) return;

        // Lazy-compile line shader
        if (!debugLineShader)
        {
            debugLineShader = new kShader();
            debugLineShader->loadShadersCode(kDebugLineVS, kDebugLineFS);
        }

        // Create VAO/VBO on first use with dynamic draw hint
        if (!debugLineVao)
        {
            debugLineVao = driver->createVertexArray();
            debugLineVbo = driver->createBuffer();
            glBindVertexArray(static_cast<GLuint>(debugLineVao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
            glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        }

        // Helper: upload vertices and draw as lines
        auto drawLines = [&](const std::vector<float> &verts, kVec3 color)
        {
            if (verts.empty()) return;
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                         verts.data(), GL_DYNAMIC_DRAW);
            debugLineShader->setValue("lineColor", color);
            driver->drawArrays(debugLineVao, kPrimitiveType::LINES,
                               static_cast<int>(verts.size() / 3));
        };

        driver->bindFramebuffer(fboMsaa);
        driver->setViewport(0, 0, fboWidth, fboHeight);

        debugLineShader->use();
        debugLineShader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
        debugLineShader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());

        driver->setDepthTest(true);
        driver->setDepthWrite(false);
        driver->setCullFace(false);
        driver->setBlend(false);

        // Build a fast lookup set for selected UUIDs
        std::set<kString> selectedSet(selectedUuids.begin(), selectedUuids.end());

        // --- Lights ---
        for (kLight *light : scene->getLights())
        {
            if (!light->getActive()) continue;
            if (selectedSet.find(light->getUuid()) == selectedSet.end()) continue;
            light->calculateModelMatrix();

            std::vector<float> verts;
            kVec3 color;
            kVec3 pos = light->getGlobalPosition();
            kLightType lt = light->getLightType();

            if (lt == kLightType::LIGHT_TYPE_POINT)
            {
                color = kVec3(1.0f, 1.0f, 0.0f); // yellow
                float L = light->getLinear();
                float Q = light->getQuadratic();
                float C = light->getConstant();
                float P = light->getPower();
                float radius = 3.0f;
                if (Q > 0.0001f)
                {
                    // Find d where P/(C+L*d+Q*d²) = 5% (reasonable visual range)
                    float target = P / 0.05f - C;
                    float disc   = L * L + 4.0f * Q * target;
                    if (disc > 0.0f)
                    {
                        float r = (-L + sqrtf(disc)) / (2.0f * Q);
                        if (r > 0.0f) radius = r;
                    }
                }
                appendCircle(verts, pos, kVec3(1, 0, 0), kVec3(0, 1, 0), radius);
                appendCircle(verts, pos, kVec3(1, 0, 0), kVec3(0, 0, 1), radius);
                appendCircle(verts, pos, kVec3(0, 1, 0), kVec3(0, 0, 1), radius);
            }
            else if (lt == kLightType::LIGHT_TYPE_SPOT)
            {
                color = kVec3(1.0f, 0.6f, 0.0f); // orange
                float innerAngle = glm::acos(glm::clamp(light->getCutOff(),      -1.0f, 1.0f));
                float outerAngle = glm::acos(glm::clamp(light->getOuterCutOff(), -1.0f, 1.0f));
                float coneLen = 5.0f;
                float innerR  = tanf(innerAngle) * coneLen;
                float outerR  = tanf(outerAngle) * coneLen;

                kVec3 dir = glm::normalize(light->getRotation() * kVec3(0.0f, -1.0f, 0.0f));
                kVec3 crossY = glm::cross(dir, kVec3(0, 1, 0));
                kVec3 right = (glm::length(crossY) > 0.001f)
                    ? glm::normalize(crossY)
                    : glm::normalize(glm::cross(dir, kVec3(1, 0, 0)));
                kVec3 up = glm::normalize(glm::cross(right, dir));

                kVec3 endCenter = pos + dir * coneLen;

                // 8 lines from origin to outer cone circle
                const int coneSegs = 8;
                for (int i = 0; i < coneSegs; ++i)
                {
                    float a    = 2.0f * glm::pi<float>() * i / coneSegs;
                    kVec3 edge = endCenter + right * (outerR * cosf(a)) + up * (outerR * sinf(a));
                    appendLine(verts, pos, edge);
                }
                appendCircle(verts, endCenter, right, up, innerR);
                appendCircle(verts, endCenter, right, up, outerR);
            }
            else if (lt == kLightType::LIGHT_TYPE_SUN)
            {
                color = kVec3(1.0f, 1.0f, 0.3f); // bright yellow
                kVec3 dir      = glm::normalize(light->getRotation() * kVec3(0.0f, -1.0f, 0.0f));
                float arrowLen = 3.0f;
                float headLen  = 0.5f;

                kVec3 perp = glm::cross(dir, kVec3(0, 1, 0));
                if (glm::length(perp) < 0.01f)
                    perp = glm::cross(dir, kVec3(1, 0, 0));
                perp = glm::normalize(perp);

                const float offsets[] = { 0.0f, -1.0f, 1.0f };
                for (float off : offsets)
                {
                    kVec3 start = pos + perp * off;
                    kVec3 end   = start + dir * arrowLen;
                    appendLine(verts, start, end);
                    kVec3 back = -dir;
                    appendLine(verts, end, end + back * headLen + perp  * (headLen * 0.4f));
                    appendLine(verts, end, end + back * headLen - perp  * (headLen * 0.4f));
                }
            }

            drawLines(verts, color);
        }

        // --- Cameras ---
        for (kObject *obj : scene->getObjects())
        {
            if (obj->getType() != kNodeType::NODE_TYPE_CAMERA) continue;
            if (!obj->getActive()) continue;
            if (selectedSet.find(obj->getUuid()) == selectedSet.end()) continue;
            obj->calculateModelMatrix();

            kCamera *cam = static_cast<kCamera *>(obj);
            float fovRad = glm::radians(cam->getFOV());
            float aspect = cam->getAspectRatio();
            float nearD  = cam->getNearClip();
            float farD   = glm::min(cam->getFarClip(), 50.0f);

            float nearH = tanf(fovRad * 0.5f) * nearD;
            float nearW = nearH * aspect;
            float farH  = tanf(fovRad * 0.5f) * farD;
            float farW  = farH * aspect;

            kMat4 invView = glm::inverse(cam->getViewMatrix());
            auto corner = [&](float x, float y, float z) -> kVec3 {
                return kVec3(invView * kVec4(x, y, z, 1.0f));
            };

            kVec3 nBL = corner(-nearW, -nearH, -nearD);
            kVec3 nBR = corner( nearW, -nearH, -nearD);
            kVec3 nTL = corner(-nearW,  nearH, -nearD);
            kVec3 nTR = corner( nearW,  nearH, -nearD);
            kVec3 fBL = corner(-farW, -farH, -farD);
            kVec3 fBR = corner( farW, -farH, -farD);
            kVec3 fTL = corner(-farW,  farH, -farD);
            kVec3 fTR = corner( farW,  farH, -farD);

            std::vector<float> verts;
            // Near plane
            appendLine(verts, nBL, nBR); appendLine(verts, nBR, nTR);
            appendLine(verts, nTR, nTL); appendLine(verts, nTL, nBL);
            // Far plane
            appendLine(verts, fBL, fBR); appendLine(verts, fBR, fTR);
            appendLine(verts, fTR, fTL); appendLine(verts, fTL, fBL);
            // Connecting edges
            appendLine(verts, nBL, fBL); appendLine(verts, nBR, fBR);
            appendLine(verts, nTL, fTL); appendLine(verts, nTR, fTR);

            drawLines(verts, kVec3(0.5f, 0.8f, 1.0f)); // light blue
        }

        // Restore state
        driver->setDepthWrite(true);
        debugLineShader->unuse();

        // Blit updated MSAA buffer to the resolve FBO
        driver->bindReadFramebuffer(fboMsaa);
        driver->bindDrawFramebuffer(fbo);
        driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);
        driver->unbindFramebuffer();
    }

    void kRenderer::renderOctreeDebug(kWorld *world, kScene *scene)
    {
        if (!octreeDebugEnabled || !enableScreenBuffer || !world) return;
        if (!world->getMainCamera()) return;

        // Lazy-compile line shader
        if (!debugLineShader)
        {
            debugLineShader = new kShader();
            debugLineShader->loadShadersCode(kDebugLineVS, kDebugLineFS);
        }
        if (!debugLineVao)
        {
            debugLineVao = driver->createVertexArray();
            debugLineVbo = driver->createBuffer();
            glBindVertexArray(static_cast<GLuint>(debugLineVao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
            glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        }

        auto appendLine = [](std::vector<float> &v, kVec3 a, kVec3 b) {
            v.insert(v.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
        };

        auto drawLines = [&](const std::vector<float> &verts, kVec3 color) {
            if (verts.empty()) return;
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                         verts.data(), GL_DYNAMIC_DRAW);
            debugLineShader->setValue("lineColor", color);
            driver->drawArrays(debugLineVao, kPrimitiveType::LINES,
                               static_cast<int>(verts.size() / 3));
        };

        driver->bindFramebuffer(fboMsaa);
        driver->setViewport(0, 0, fboWidth, fboHeight);

        debugLineShader->use();
        debugLineShader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
        debugLineShader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());

        driver->setDepthTest(false);
        driver->setDepthWrite(false);

        // Leaf nodes: green  |  Internal nodes: grey
        const kVec3 colorLeaf(0.2f, 0.85f, 0.2f);
        const kVec3 colorInternal(0.5f, 0.5f, 0.5f);

        std::vector<float> leafVerts, internalVerts;

        sceneOctree->traverse([&](const kAABB &b, int /*depth*/, bool isLeaf) {
            std::vector<float> &v = isLeaf ? leafVerts : internalVerts;
            kVec3 mn = b.min, mx = b.max;
            // Bottom face
            appendLine(v, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z});
            appendLine(v, {mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z});
            appendLine(v, {mx.x,mn.y,mx.z}, {mn.x,mn.y,mx.z});
            appendLine(v, {mn.x,mn.y,mx.z}, {mn.x,mn.y,mn.z});
            // Top face
            appendLine(v, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mn.z});
            appendLine(v, {mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z});
            appendLine(v, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z});
            appendLine(v, {mn.x,mx.y,mx.z}, {mn.x,mx.y,mn.z});
            // Vertical edges
            appendLine(v, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mn.z});
            appendLine(v, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z});
            appendLine(v, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z});
            appendLine(v, {mn.x,mn.y,mx.z}, {mn.x,mx.y,mx.z});
        });

        drawLines(internalVerts, colorInternal);
        drawLines(leafVerts,     colorLeaf);

        // Draw individual mesh world AABBs: yellow = static, cyan = dynamic
        if (scene)
        {
            const kVec3 colorStatic (1.0f, 0.9f, 0.0f);
            const kVec3 colorDynamic(0.0f, 0.85f, 0.9f);

            std::vector<float> staticVerts, dynamicVerts;

            std::function<void(kObject*)> collectMeshAABBs = [&](kObject *node) {
                if (!node) return;
                if (node->getType() == kNodeType::NODE_TYPE_MESH)
                {
                    kMesh *m = static_cast<kMesh*>(node);
                    if (m->getLoaded())
                    {
                        m->calculateModelMatrix();
                        kAABB b = m->getWorldAABB();
                        if (b.isValid())
                        {
                            std::vector<float> &v = m->getStatic() ? staticVerts : dynamicVerts;
                            kVec3 mn = b.min, mx = b.max;
                            appendLine(v, {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z});
                            appendLine(v, {mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z});
                            appendLine(v, {mx.x,mn.y,mx.z}, {mn.x,mn.y,mx.z});
                            appendLine(v, {mn.x,mn.y,mx.z}, {mn.x,mn.y,mn.z});
                            appendLine(v, {mn.x,mx.y,mn.z}, {mx.x,mx.y,mn.z});
                            appendLine(v, {mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z});
                            appendLine(v, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z});
                            appendLine(v, {mn.x,mx.y,mx.z}, {mn.x,mx.y,mn.z});
                            appendLine(v, {mn.x,mn.y,mn.z}, {mn.x,mx.y,mn.z});
                            appendLine(v, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z});
                            appendLine(v, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z});
                            appendLine(v, {mn.x,mn.y,mx.z}, {mn.x,mx.y,mx.z});
                        }
                    }
                }
                for (kObject *child : node->getChildren())
                    collectMeshAABBs(child);
            };
            collectMeshAABBs(scene->getRootNode());

            drawLines(staticVerts,  colorStatic);
            drawLines(dynamicVerts, colorDynamic);
        }

        driver->setDepthWrite(true);
        debugLineShader->unuse();

        driver->bindReadFramebuffer(fboMsaa);
        driver->bindDrawFramebuffer(fbo);
        driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);
        driver->unbindFramebuffer();
    }

    void kRenderer::renderDebugLines(kWorld *world, const std::vector<kVec3> &segments, kVec3 color)
    {
        if (!enableScreenBuffer || !world || segments.empty()) return;
        if (!world->getMainCamera()) return;

        // Lazy-compile the shared debug-line shader / buffers.
        if (!debugLineShader)
        {
            debugLineShader = new kShader();
            debugLineShader->loadShadersCode(kDebugLineVS, kDebugLineFS);
        }
        if (!debugLineVao)
        {
            debugLineVao = driver->createVertexArray();
            debugLineVbo = driver->createBuffer();
            glBindVertexArray(static_cast<GLuint>(debugLineVao));
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
            glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        }

        std::vector<float> verts;
        verts.reserve(segments.size() * 3);
        for (const kVec3 &p : segments)
        {
            verts.push_back(p.x);
            verts.push_back(p.y);
            verts.push_back(p.z);
        }

        driver->bindFramebuffer(fboMsaa);
        driver->setViewport(0, 0, fboWidth, fboHeight);

        debugLineShader->use();
        debugLineShader->setValue("viewMatrix",       world->getMainCamera()->getViewMatrix());
        debugLineShader->setValue("projectionMatrix", world->getMainCamera()->getProjectionMatrix());
        debugLineShader->setValue("lineColor", color);

        driver->setDepthTest(false);
        driver->setDepthWrite(false);

        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(debugLineVbo));
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);
        driver->drawArrays(debugLineVao, kPrimitiveType::LINES,
                           static_cast<int>(verts.size() / 3));

        driver->setDepthWrite(true);
        debugLineShader->unuse();

        // Resolve MSAA → display texture so the overlay shows in the panel.
        driver->bindReadFramebuffer(fboMsaa);
        driver->bindDrawFramebuffer(fbo);
        driver->blitFramebufferColor(0, 0, fboWidth, fboHeight, 0, 0, fboWidth, fboHeight);
        driver->unbindFramebuffer();
    }
}
