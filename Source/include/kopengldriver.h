/**
 * @file kopengldriver.h
 * @brief OpenGL 3.3 Core Profile implementation of the kDriver interface.
 */

#ifndef KOPENGLDRIVER_H
#define KOPENGLDRIVER_H

#include "kdriver.h"
#include "kwindow.h"

#include <GL/glew.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#define NO_SDL_GLEXT
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <iostream>
#include <vector>

namespace kemena
{
    /**
     * @brief OpenGL 3.3 Core Profile graphics driver.
     *
     * Created by kRenderer when the RENDERER_GL backend is selected. Wraps all
     * raw OpenGL calls so that the rest of the engine remains API-agnostic.
     *
     * Do not instantiate this class directly — use kRenderer::init() instead.
     */
    class KEMENA3D_API kOpenGLDriver : public kDriver
    {
    public:
        kOpenGLDriver() = default;
        ~kOpenGLDriver() override;

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Creates an OpenGL 3.3 Core Profile context for the given window.
         *
         * Sets SDL GL attributes, creates the context, initialises GLEW, and
         * applies default render state.
         * @param window SDL-backed window to create the context for.
         * @return true on success.
         */
        bool init(kWindow *window) override;

        /** @brief Destroys the SDL GL context. */
        void destroy() override;

        /**
         * @brief Returns the raw SDL_GLContext handle.
         * @return Opaque pointer; cast to SDL_GLContext when needed.
         */
        void *getNativeContext() override;

        /** @brief Returns the OpenGL version kString. */
        kString getApiVersion() override;

        /** @brief Returns the GLSL version kString. */
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

        uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) override;
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
        void setVertexAttribFloat(int location, int components, int stride, size_t offset) override;
        void setVertexAttribInt(int location, int components, int stride, size_t offset) override;

        // --- Draw calls ------------------------------------------------------

        void drawIndexed(uint32_t vaoId, int indexCount) override;
        void drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount) override;

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

    private:
        SDL_GLContext glContext = nullptr; ///< The SDL-managed OpenGL context.

        /** @brief Converts a kBlendFactor to the corresponding GL enum. */
        GLenum toGLBlendFactor(kBlendFactor factor);

        /** @brief Converts a kPrimitiveType to the corresponding GL enum. */
        GLenum toGLPrimitiveType(kPrimitiveType type);
    };
}

#endif // KOPENGLDRIVER_H
