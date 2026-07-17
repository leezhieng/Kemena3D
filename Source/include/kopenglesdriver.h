/**
 * @file kopenglesdriver.h
 * @brief OpenGL ES 3.0 implementation of the kDriver interface for Android / mobile.
 *
 * Targets OpenGL ES 3.0 (API level 21+ on Android, WebGL 2.0 on web, etc.).
 * Does NOT use GLEW — loads GLES3 symbols natively or via the platform's
 * EGL/GLES3 headers. Built-in shaders are translated to GLSL ES 300 syntax.
 */

#ifndef KOPENGLESDRIVER_H
#define KOPENGLESDRIVER_H

#include "kdriver.h"
#include "kwindow.h"

// On Android / GLES platforms we include the Khronos GLES3 headers.
// Desktop builds that want to test GLES can install the ANGLE or PowerVR SDK.
#if defined(__ANDROID__) || defined(KEMENA_GLES)
  #include <GLES3/gl3.h>
  #include <GLES3/gl3platform.h>
  #include <EGL/egl.h>
#else
  // Fallback for desktop testing: use standard GL headers but restrict to
  // the ES 3.0 subset at the driver level.  Most desktop GL 3.3+ implementations
  // support the ES 3.0 compatibility profile when requested via SDL.
  #include <GL/glew.h>
  #ifdef __APPLE__
    #include <OpenGL/gl.h>
  #else
    #include <GL/gl.h>
  #endif
#endif

#define NO_SDL_GLEXT
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <iostream>
#include <vector>

namespace kemena
{
    /**
     * @brief OpenGL ES 3.0 graphics driver for Android / mobile platforms.
     *
     * Implements the kDriver interface using GLES 3.0 calls.  Unlike
     * kOpenGLDriver, this driver:
     *  - Requests an ES 3.0 context via SDL GL attributes.
     *  - Uses GLES3/gl3.h instead of GLEW.
     *  - Avoids features absent from ES 3.0:
     *      * glPolygonMode        → no-op (wireframe rendered as lines).
     *      * GL_FRAMEBUFFER_SRGB  → no-op (sRGB via internal formats).
     *      * glGetTexImage        → replaced with FBO + glReadPixels.
     *      * GL_TEXTURE_2D_MULTISAMPLE → uses multisample RBO fallback.
     *      * GL_SAMPLE_ALPHA_TO_COVERAGE → no-op.
     *      * GL_DEPTH24_STENCIL8  → GL_DEPTH24_STENCIL8_OES or GL_DEPTH_COMPONENT24.
     *
     * Do not instantiate this class directly — use kRenderer::init() with
     * kRendererType::RENDERER_GLES.
     */
    class KEMENA3D_API kOpenGLESDriver : public kDriver
    {
    public:
        kOpenGLESDriver() = default;
        ~kOpenGLESDriver() override;

        // --- Lifecycle -------------------------------------------------------

        /** @brief Creates an OpenGL ES 3.0 context for the given window. */
        bool init(kWindow *window) override;

        /** @brief Destroys the SDL GL context. */
        void destroy() override;

        /** @brief Returns the raw SDL_GLContext handle. */
        void *getNativeContext() override;

        /** @brief Returns the OpenGL ES version string. */
        kString getApiVersion() override;

        /** @brief Returns the GLSL ES version string. */
        kString getShaderVersion() override;

        // --- Frame state -----------------------------------------------------

        void setClearColor(float r, float g, float b, float a) override;
        void clear(bool color, bool depth, bool stencil) override;
        void setViewport(int x, int y, int width, int height) override;

        // --- Pipeline state --------------------------------------------------

        void setDepthTest(bool enable) override;
        void setDepthWrite(bool enable) override;
        void setBlend(bool enable) override;
        void setBlendFunc(kBlendFactor src, kBlendFactor dst) override;
        void setCullFace(bool enable) override;
        void setCullMode(kCullMode mode) override;
        void setFrontFace(kFrontFace face) override;
        void setMultisample(bool enable) override;
        void setSRGBEncoding(bool enable) override;
        void setSampleAlphaToCoverage(bool enable) override;

        // --- Shader programs -------------------------------------------------

        /**
         * @brief Compiles and links a shader program from GLSL ES source.
         *
         * The source should use GLSL ES 3.00 syntax (`#version 300 es`).
         * If the source starts with `#version 330 core`, the driver will
         * automatically rewrite it to `#version 300 es` and inject the
         * required `precision mediump float;` declaration.
         */
        uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) override;

        /** @brief SPIR-V is unavailable on ES 3.0; returns 0. */
        uint32_t compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                           const kString &vertEntry,
                                           const std::vector<uint8_t> &fragSpirv,
                                           const kString &fragEntry) override;

        void deleteShaderProgram(uint32_t id) override;
        void bindShaderProgram(uint32_t id) override;
        void unbindShaderProgram() override;

        void setUniformBool(uint32_t progId, const kString &name, bool v) override;
        void setUniformInt(uint32_t progId, const kString &name, int v) override;
        void setUniformUint(uint32_t progId, const kString &name, uint32_t v) override;
        void setUniformFloat(uint32_t progId, const kString &name, float v) override;
        void setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v) override;
        void setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v) override;
        void setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v) override;
        void setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v) override;
        void setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v) override;

        // --- Vertex arrays ---------------------------------------------------

        uint32_t createVertexArray() override;
        void deleteVertexArray(uint32_t id) override;
        void bindVertexArray(uint32_t id) override;
        void unbindVertexArray() override;

        // --- Buffers ---------------------------------------------------------

        uint32_t createBuffer() override;
        void deleteBuffer(uint32_t id) override;
        void uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size) override;
        void uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size) override;
        void updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset) override;
        void setVertexAttribFloat(int location, int components, int stride, size_t offset) override;
        void setVertexAttribInt(int location, int components, int stride, size_t offset) override;
        void setVertexAttribDivisor(int location, int divisor) override;

        // --- Draw calls ------------------------------------------------------

        void drawIndexed(uint32_t vaoId, int indexCount) override;
        void drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount) override;
        void drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount) override;
        void drawArraysInstanced(uint32_t vaoId, kPrimitiveType type, int vertexCount, int instanceCount) override;

        // --- Texture sampling ------------------------------------------------

        void bindTexture2D(int unit, uint32_t id) override;
        void bindTexture2DArray(int unit, uint32_t id) override;
        void bindTextureCube(int unit, uint32_t id) override;
        void unbindTexture2D(int unit) override;
        void unbindTexture2DArray(int unit) override;
        void unbindTextureCube(int unit) override;
        void generateMipmaps2D(uint32_t id) override;
        void readTexture2DRGB(uint32_t id, int mipLevel, float *pixels) override;
        void readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) override;

        // --- Framebuffers ----------------------------------------------------

        uint32_t createFramebuffer() override;
        void deleteFramebuffer(uint32_t id) override;
        void bindFramebuffer(uint32_t id) override;
        void bindReadFramebuffer(uint32_t id) override;
        void bindDrawFramebuffer(uint32_t id) override;
        void unbindFramebuffer() override;
        bool isFramebufferComplete() override;
        void blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1) override;
        void setFramebufferDrawBuffer() override;

        // --- Renderbuffers ---------------------------------------------------

        uint32_t createRenderbuffer() override;
        void deleteRenderbuffer(uint32_t id) override;
        void setupRenderbuffer(uint32_t rboId, int width, int height) override;
        void setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height) override;
        void attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId) override;

        // --- FBO-managed textures --------------------------------------------

        uint32_t createFBOColorTexture(int width, int height) override;
        uint32_t createFBOColorTextureMSAA(int samples, int width, int height) override;
        uint32_t createFBODepthTexture(int width, int height) override;
        uint32_t createFBODepthTextureArray(int width, int height, int layers) override;
        void deleteFBOTexture(uint32_t id) override;
        void attachFBOColorTexture(uint32_t fboId, uint32_t texId) override;
        void attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId) override;
        void attachFBODepthTexture(uint32_t fboId, uint32_t texId) override;
        void attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer) override;
        void resizeFBOColorTexture(uint32_t texId, int width, int height) override;
        void resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height) override;

        // --- Helpers exposed for the renderer's GLES adaptation --------------

        /**
         * @brief Rewrites a GLSL 330 core shader source to GLSL ES 300.
         *
         * Performs these transformations:
         *  - `#version 330 core` → `#version 300 es`
         *  - Inserts `precision mediump float;` after the version directive
         *    (for fragment shaders).
         *  - Other version strings are left untouched (assumes already ES).
         *
         * @param src        Original shader source.
         * @param isFragment If true, injects the precision qualifier.
         * @return Rewritten source string.
         */
        static kString adaptShaderSource(const char *src, bool isFragment);

        /** @brief Returns true when the active driver is an ES variant. */
        static bool isGLES();

    private:
        SDL_GLContext glContext = nullptr;

        /** @brief Converts a kBlendFactor to the corresponding GL enum. */
        GLenum toGLBlendFactor(kBlendFactor factor);

        /** @brief Converts a kPrimitiveType to the corresponding GL enum. */
        GLenum toGLPrimitiveType(kPrimitiveType type);

        /** @brief Compiles a single shader stage from GLSL ES source. */
        GLuint compileShaderStage(GLenum stage, const char *src);

        /** @brief Internal depth-stencil format for the current platform. */
        GLenum depthStencilFormat();

        /** @brief Internal depth-only format for the current platform. */
        GLenum depthFormat();
    };

} // namespace kemena

#endif // KOPENGLESDRIVER_H
