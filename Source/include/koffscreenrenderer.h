/**
 * @file koffscreenrenderer.h
 * @brief Offscreen FBO renderer for thumbnails, previews, and image export.
 */

#ifndef KOFFSCREENRENDERER_H
#define KOFFSCREENRENDERER_H

#include "kexport.h"
#include "kdatatype.h"
#include "kcamera.h"

#include <string>

namespace kemena
{
    class kMesh;
    class kScene;
    class kWorld;
    class kObject;
    class kShader;
    class kDriver;

    /**
     * @brief Renders to an offscreen FBO for thumbnails, previews, and image export.
     *
     * Usage:
     * @code
     *   kOffscreenRenderer offscreen(256, 256);
     *
     *   // Full scene render from a custom camera
     *   offscreen.render(world, scene, &myCamera);
     *
     *   // Single mesh with auto-framed camera
     *   offscreen.renderMesh(mesh);
     *
     *   // Use as ImGui thumbnail
     *   ImGui::Image((ImTextureID)(uintptr_t)offscreen.getTexture(), {256, 256});
     *
     *   // Save to file
     *   offscreen.saveToFile("thumbnail.png");
     * @endcode
     */
    class KEMENA3D_API kOffscreenRenderer
    {
    public:
        /**
         * @brief Construct the renderer and allocate its offscreen FBO.
         *
         * @param width  Output texture width in pixels.
         * @param height Output texture height in pixels.
         */
        kOffscreenRenderer(int width = 256, int height = 256);

        /**
         * @brief Destroy the renderer and release all GPU resources (FBO, textures, shaders).
         */
        ~kOffscreenRenderer();

        /**
         * @brief Resize the offscreen buffer.
         *
         * Destroys and recreates the FBO.  Any previous render result is lost.
         *
         * @param newWidth  New output texture width in pixels.
         * @param newHeight New output texture height in pixels.
         */
        void resize(int newWidth, int newHeight);

        /**
         * @brief Set the background clear color and alpha (default: opaque dark grey).
         *
         * Set alpha to 0 for a fully transparent background — useful when
         * compositing the thumbnail over other UI elements.
         *
         * @param color RGBA clear color in linear/normalized [0,1] components.
         */
        void setBackgroundColor(kVec4 color) { bgColor = color; }

        /**
         * @brief Get the current background clear color and alpha.
         * @return The RGBA background color.
         */
        kVec4 getBackgroundColor() const     { return bgColor; }

        /**
         * @brief Render the full scene to the offscreen buffer.
         *
         * Uses the provided camera for view/projection.  All scene lights,
         * materials, and textures are applied.  Shadows are not included.
         *
         * @param world  World (used only for skinning state, may be nullptr).
         * @param scene  Scene to render.
         * @param camera Camera defining the viewpoint.
         */
        void render(kWorld *world, kScene *scene, kCamera *camera);

        /**
         * @brief Render a single mesh to the offscreen buffer.
         *
         * If @p camera is nullptr, a camera is auto-positioned to frame the
         * mesh's world AABB from a 3/4 view angle.
         *
         * A simple three-point directional light setup is used.  The mesh's
         * own material and shader are applied if present.
         *
         * @param mesh   Mesh to render (must be loaded and have a material).
         * @param camera Override camera, or nullptr for auto-framing.
         */
        void renderMesh(kMesh *mesh, kCamera *camera = nullptr);

        /**
         * @brief Render a mesh using its assigned material shader and uniforms.
         *
         * Unlike renderMesh(), which uses the built-in preview shader, this
         * method binds the mesh's own kMaterial and kShader so that material
         * properties (diffuse, metallic, textures, etc.) are visible in the
         * result.  A simple sun light is injected so the result is shaded.
         *
         * The mesh must have a material with a compiled shader; if not, nothing
         * is drawn.
         *
         * @param mesh   Mesh to render.
         * @param camera Override camera, or nullptr for auto-framing.
         */
        void renderMeshWithMaterial(kMesh *mesh, kCamera *camera = nullptr);

        /**
         * @brief GPU texture ID of the current render result.
         *
         * Pass directly to ImGui::Image as @c (ImTextureID)(uintptr_t)getTexture().
         * Valid until the next resize() call.
         */
        uint32_t getTexture() const { return colorTex; }

        /**
         * @brief Get the output texture width in pixels.
         * @return Buffer width.
         */
        int getWidth()  const { return width;  }

        /**
         * @brief Get the output texture height in pixels.
         * @return Buffer height.
         */
        int getHeight() const { return height; }

        /**
         * @brief Save the current render result to an image file.
         *
         * The format is determined by the file extension:
         *   .png  — lossless, recommended
         *   .jpg  — lossy, smaller
         *   .bmp  — uncompressed
         *   .tga  — uncompressed with alpha
         *
         * @param filePath Destination path including extension.
         * @return true on success.
         */
        bool saveToFile(const kString &filePath) const;

    private:
        kDriver *driver  = nullptr;
        uint32_t fbo      = 0;
        uint32_t colorTex = 0;
        uint32_t depthRbo = 0;

        int width;
        int height;
        int ssaaScale = 2; // render at Nx resolution, box-filter downsample on save
        kVec4 bgColor = kVec4(0.15f, 0.15f, 0.15f, 1.0f);

        kShader *builtinShader = nullptr; ///< Lazy-compiled fallback for meshes with no material.

        // Cascaded-shadow-map resources for the offscreen pass. Lazily allocated
        // on the first render() call so headless thumbnail use cases that never
        // render a scene don't pay the VRAM cost.
        static constexpr int kMaxShadowCascades = 4;
        kShader *shadowShader        = nullptr;
        uint32_t shadowFbo           = 0;
        uint32_t shadowTexArray      = 0;
        int      shadowResolution    = 1024; // smaller than main renderer to save VRAM
        int      shadowCascadeCount  = 3;
        float    shadowSplitLambda   = 0.85f;
        kMat4    lightSpaceMatrices[kMaxShadowCascades];
        float    cascadeSplits[kMaxShadowCascades] = {};

        void createFBO();
        void destroyFBO();
        void ensureBuiltinShader();

        void ensureShadowResources();
        void applyShadowResolution(int resolution);
        void renderShadowPass(kWorld *world, kScene *scene, kCamera *camera);
        void renderShadowNode(kObject *node, const kMat4 &lightSpace, kShader *shader);

        void renderNodeFull(kObject *node, kScene *scene, kCamera *camera);
        void drawMeshWithMaterial(kMesh *mesh, kScene *scene, kCamera *camera,
                                  int sunCount, int pointCount, int spotCount);
        void drawMeshBuiltin(kMesh *mesh, kCamera *camera);
        void drawMeshHierarchy(kMesh *mesh, kCamera *camera);

        void setupLightsFromScene(kShader *shader, kScene *scene,
                                  int &outSun, int &outPoint, int &outSpot);
        void setupSingleSunLight(kShader *shader,
                                 kVec3 direction, kVec3 diffuse, float power);
        void bindMaterialTextures(kMesh *mesh, kShader *shader);
        void unbindMaterialTextures(kMesh *mesh);
    };
}

#endif // KOFFSCREENRENDERER_H
