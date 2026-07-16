#include "kopengldriver.h"

#include <glm/gtc/type_ptr.hpp>

namespace kemena
{
    kDriver *kDriver::s_current = nullptr;

    kOpenGLDriver::~kOpenGLDriver()
    {
        destroy();
    }

    bool kOpenGLDriver::init(kWindow *window)
    {
        if (window == nullptr)
            return false;

#ifdef KEMENA_OPENGL_46
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

        glContext = SDL_GL_CreateContext(window->getSdlWindow());
        if (!glContext)
        {
            std::cout << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
            return false;
        }

        glewExperimental = GL_TRUE;
        GLenum status = glewInit();
        if (status != GLEW_OK)
        {
            std::cout << "GLEW Error: " << glewGetErrorString(status) << std::endl;
            return false;
        }

        std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
        std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

        // Default state
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glBindVertexArray(0);

        return true;
    }

    void kOpenGLDriver::destroy()
    {
        if (glContext)
        {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
    }

    void *kOpenGLDriver::getNativeContext()
    {
        return glContext;
    }

    kString kOpenGLDriver::getApiVersion()
    {
        return reinterpret_cast<const char *>(glGetString(GL_VERSION));
    }

    kString kOpenGLDriver::getShaderVersion()
    {
        return reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    }

    // -------------------------------------------------------------------------
    // Frame state
    // -------------------------------------------------------------------------

    void kOpenGLDriver::setClearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    void kOpenGLDriver::clear(bool color, bool depth, bool stencil)
    {
        GLbitfield mask = 0;
        if (color)
            mask |= GL_COLOR_BUFFER_BIT;
        if (depth)
            mask |= GL_DEPTH_BUFFER_BIT;
        if (stencil)
            mask |= GL_STENCIL_BUFFER_BIT;
        if (mask)
            glClear(mask);
    }

    void kOpenGLDriver::setViewport(int x, int y, int width, int height)
    {
        glViewport(x, y, width, height);
    }

    // -------------------------------------------------------------------------
    // Pipeline state
    // -------------------------------------------------------------------------

    void kOpenGLDriver::setDepthTest(bool enable)
    {
        enable ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    void kOpenGLDriver::setDepthWrite(bool enable)
    {
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
    }

    void kOpenGLDriver::setBlend(bool enable)
    {
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    }

    void kOpenGLDriver::setBlendFunc(kBlendFactor src, kBlendFactor dst)
    {
        glBlendFunc(toGLBlendFactor(src), toGLBlendFactor(dst));
    }

    void kOpenGLDriver::setCullFace(bool enable)
    {
        enable ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }

    void kOpenGLDriver::setCullMode(kCullMode mode)
    {
        switch (mode)
        {
        case kCullMode::BACK:
            glCullFace(GL_BACK);
            break;
        case kCullMode::FRONT:
            glCullFace(GL_FRONT);
            break;
        case kCullMode::FRONT_AND_BACK:
            glCullFace(GL_FRONT_AND_BACK);
            break;
        }
    }

    void kOpenGLDriver::setFrontFace(kFrontFace face)
    {
        glFrontFace(face == kFrontFace::CCW ? GL_CCW : GL_CW);
    }

    void kOpenGLDriver::setMultisample(bool enable)
    {
        enable ? glEnable(GL_MULTISAMPLE) : glDisable(GL_MULTISAMPLE);
    }

    void kOpenGLDriver::setSRGBEncoding(bool enable)
    {
        enable ? glEnable(GL_FRAMEBUFFER_SRGB) : glDisable(GL_FRAMEBUFFER_SRGB);
    }

    void kOpenGLDriver::setSampleAlphaToCoverage(bool enable)
    {
        enable ? glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE) : glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    // -------------------------------------------------------------------------
    // Shader programs
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::compileShaderProgram(const char *vertSrc, const char *fragSrc)
    {
        GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
        GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        GLint result = GL_FALSE;
        int logLength;

        if (vertSrc && strlen(vertSrc) > 0)
        {
            glShaderSource(vertShader, 1, &vertSrc, nullptr);
            glCompileShader(vertShader);
            glGetShaderiv(vertShader, GL_COMPILE_STATUS, &result);
            glGetShaderiv(vertShader, GL_INFO_LOG_LENGTH, &logLength);
            if (result == GL_FALSE && logLength > 1)
            {
                std::vector<char> err(logLength);
                glGetShaderInfoLog(vertShader, logLength, nullptr, err.data());
                std::cout << "Vertex Shader Error: " << err.data() << std::endl;
            }
        }

        if (fragSrc && strlen(fragSrc) > 0)
        {
            glShaderSource(fragShader, 1, &fragSrc, nullptr);
            glCompileShader(fragShader);
            glGetShaderiv(fragShader, GL_COMPILE_STATUS, &result);
            glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &logLength);
            if (result == GL_FALSE && logLength > 1)
            {
                std::vector<char> err(logLength);
                glGetShaderInfoLog(fragShader, logLength, nullptr, err.data());
                std::cout << "Fragment Shader Error: " << err.data() << std::endl;
            }
        }

        GLuint program = glCreateProgram();
        if (vertSrc && strlen(vertSrc) > 0)
            glAttachShader(program, vertShader);
        if (fragSrc && strlen(fragSrc) > 0)
            glAttachShader(program, fragShader);
        glLinkProgram(program);

        glGetProgramiv(program, GL_LINK_STATUS, &result);
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (result == GL_FALSE && logLength > 1)
        {
            std::vector<char> err(logLength);
            glGetProgramInfoLog(program, logLength, nullptr, err.data());
            std::cout << "Link Shader Program Error: " << err.data() << std::endl;
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return static_cast<uint32_t>(program);
    }

    uint32_t kOpenGLDriver::compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                                      const kString &vertEntry,
                                                      const std::vector<uint8_t> &fragSpirv,
                                                      const kString &fragEntry)
    {
        // GL_ARB_gl_spirv / OpenGL 4.6 required
        if (!GLEW_ARB_gl_spirv && !GLEW_VERSION_4_6)
        {
            std::cout << "[kOpenGLDriver] SPIR-V shaders require OpenGL 4.6 or GL_ARB_gl_spirv." << std::endl;
            return 0;
        }

        auto specializeShader = [](GLuint shader, const kString &entry) -> bool
        {
            GLint status = GL_FALSE;
            glSpecializeShaderARB(shader, entry.c_str(), 0, nullptr, nullptr);
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (status == GL_FALSE)
            {
                GLint logLen = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                if (logLen > 1)
                {
                    std::vector<char> log(logLen);
                    glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                    std::cout << "SPIR-V specialization error: " << log.data() << std::endl;
                }
                return false;
            }
            return true;
        };

        GLuint vertShader = 0, fragShader = 0;

        if (!vertSpirv.empty())
        {
            vertShader = glCreateShader(GL_VERTEX_SHADER);
            glShaderBinary(1, &vertShader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB,
                           vertSpirv.data(), static_cast<GLsizei>(vertSpirv.size()));
            if (!specializeShader(vertShader, vertEntry))
            {
                glDeleteShader(vertShader);
                return 0;
            }
        }

        if (!fragSpirv.empty())
        {
            fragShader = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderBinary(1, &fragShader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB,
                           fragSpirv.data(), static_cast<GLsizei>(fragSpirv.size()));
            if (!specializeShader(fragShader, fragEntry))
            {
                glDeleteShader(vertShader);
                glDeleteShader(fragShader);
                return 0;
            }
        }

        GLuint program = glCreateProgram();
        if (vertShader)
            glAttachShader(program, vertShader);
        if (fragShader)
            glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint result = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &result);
        if (result == GL_FALSE)
        {
            GLint logLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1)
            {
                std::vector<char> err(logLen);
                glGetProgramInfoLog(program, logLen, nullptr, err.data());
                std::cout << "SPIR-V link error: " << err.data() << std::endl;
            }
            glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            glDeleteProgram(program);
            return 0;
        }

        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return static_cast<uint32_t>(program);
    }

    void kOpenGLDriver::deleteShaderProgram(uint32_t id)
    {
        if (id)
            glDeleteProgram(static_cast<GLuint>(id));
    }

    void kOpenGLDriver::bindShaderProgram(uint32_t id)
    {
        glUseProgram(static_cast<GLuint>(id));
    }

    void kOpenGLDriver::unbindShaderProgram()
    {
        glUseProgram(0);
    }

    void kOpenGLDriver::setUniformBool(uint32_t progId, const kString &name, bool v)
    {
        glUniform1i(glGetUniformLocation(progId, name.c_str()), static_cast<int>(v));
    }

    void kOpenGLDriver::setUniformInt(uint32_t progId, const kString &name, int v)
    {
        glUniform1i(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLDriver::setUniformUint(uint32_t progId, const kString &name, uint32_t v)
    {
        glUniform1ui(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLDriver::setUniformFloat(uint32_t progId, const kString &name, float v)
    {
        glUniform1f(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLDriver::setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v)
    {
        glUniform2fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLDriver::setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v)
    {
        glUniform3fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLDriver::setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v)
    {
        glUniform4fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLDriver::setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v)
    {
        glUniformMatrix4fv(glGetUniformLocation(progId, name.c_str()), 1, GL_FALSE, glm::value_ptr(v));
    }

    void kOpenGLDriver::setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v)
    {
        glUniformMatrix4fv(glGetUniformLocation(progId, name.c_str()),
                           static_cast<GLsizei>(v.size()), GL_FALSE, glm::value_ptr(v[0]));
    }

    // -------------------------------------------------------------------------
    // Vertex arrays
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::createVertexArray()
    {
        GLuint id = 0;
        glGenVertexArrays(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLDriver::deleteVertexArray(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteVertexArrays(1, &glId);
    }

    void kOpenGLDriver::bindVertexArray(uint32_t id)
    {
        glBindVertexArray(static_cast<GLuint>(id));
    }

    void kOpenGLDriver::unbindVertexArray()
    {
        glBindVertexArray(0);
    }

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::createBuffer()
    {
        GLuint id = 0;
        glGenBuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLDriver::deleteBuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteBuffers(1, &glId);
    }

    void kOpenGLDriver::uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    void kOpenGLDriver::uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    void kOpenGLDriver::updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    }

    void kOpenGLDriver::setVertexAttribFloat(int location, int components, int stride, size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribPointer(static_cast<GLuint>(location), components, GL_FLOAT, GL_FALSE,
                              stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLDriver::setVertexAttribInt(int location, int components, int stride, size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribIPointer(static_cast<GLuint>(location), components, GL_INT,
                               stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLDriver::setVertexAttribDivisor(int location, int divisor)
    {
        glVertexAttribDivisor(static_cast<GLuint>(location), static_cast<GLuint>(divisor));
    }

    // -------------------------------------------------------------------------
    // Draw calls
    // -------------------------------------------------------------------------

    void kOpenGLDriver::drawIndexed(uint32_t vaoId, int indexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void kOpenGLDriver::drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    void kOpenGLDriver::drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArrays(toGLPrimitiveType(type), 0, vertexCount);
        glBindVertexArray(0);
    }

    void kOpenGLDriver::drawArraysInstanced(uint32_t vaoId, kPrimitiveType type, int vertexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArraysInstanced(toGLPrimitiveType(type), 0, vertexCount,
                              static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    // -------------------------------------------------------------------------
    // Texture sampling
    // -------------------------------------------------------------------------

    void kOpenGLDriver::bindTexture2D(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::bindTextureCube(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::unbindTexture2D(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLDriver::bindTexture2DArray(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::unbindTexture2DArray(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    void kOpenGLDriver::unbindTextureCube(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void kOpenGLDriver::generateMipmaps2D(uint32_t id)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    void kOpenGLDriver::readTexture2DRGB(uint32_t id, int mipLevel, float *pixels)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glGetTexImage(GL_TEXTURE_2D, mipLevel, GL_RGB, GL_FLOAT, pixels);
    }

    void kOpenGLDriver::readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
    {
        uint8_t pixel[4] = {0, 0, 0, 0};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
        a = pixel[3];
    }

    // -------------------------------------------------------------------------
    // Framebuffers
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::createFramebuffer()
    {
        GLuint id = 0;
        glGenFramebuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLDriver::deleteFramebuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteFramebuffers(1, &glId);
    }

    void kOpenGLDriver::bindFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::bindReadFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::bindDrawFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLDriver::unbindFramebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool kOpenGLDriver::isFramebufferComplete()
    {
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void kOpenGLDriver::blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                             int dstX0, int dstY0, int dstX1, int dstY1)
    {
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    void kOpenGLDriver::setFramebufferDrawBuffer()
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }

    // -------------------------------------------------------------------------
    // Renderbuffers
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::createRenderbuffer()
    {
        GLuint id = 0;
        glGenRenderbuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLDriver::deleteRenderbuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteRenderbuffers(1, &glId);
    }

    void kOpenGLDriver::setupRenderbuffer(uint32_t rboId, int width, int height)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    }

    void kOpenGLDriver::setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    }

    void kOpenGLDriver::attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, static_cast<GLuint>(rboId));
    }

    // -------------------------------------------------------------------------
    // FBO-managed textures
    // -------------------------------------------------------------------------

    uint32_t kOpenGLDriver::createFBOColorTexture(int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLDriver::createFBOColorTextureMSAA(int samples, int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, id);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA8, width, height, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLDriver::createFBODepthTexture(int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        glBindTexture(GL_TEXTURE_2D, 0);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLDriver::createFBODepthTextureArray(int width, int height, int layers)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D_ARRAY, id);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                     width, height, layers, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLDriver::deleteFBOTexture(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteTextures(1, &glId);
    }

    void kOpenGLDriver::attachFBOColorTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);
    }

    void kOpenGLDriver::attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D_MULTISAMPLE, static_cast<GLuint>(texId), 0);
    }

    void kOpenGLDriver::attachFBODepthTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLDriver::attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  static_cast<GLuint>(texId), 0, layer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLDriver::resizeFBOColorTexture(uint32_t texId, int width, int height)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texId));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLDriver::resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, static_cast<GLuint>(texId));
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA8, width, height, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    GLenum kOpenGLDriver::toGLBlendFactor(kBlendFactor factor)
    {
        switch (factor)
        {
        case kBlendFactor::ZERO:
            return GL_ZERO;
        case kBlendFactor::ONE:
            return GL_ONE;
        case kBlendFactor::SRC_ALPHA:
            return GL_SRC_ALPHA;
        case kBlendFactor::ONE_MINUS_SRC_ALPHA:
            return GL_ONE_MINUS_SRC_ALPHA;
        case kBlendFactor::SRC_COLOR:
            return GL_SRC_COLOR;
        case kBlendFactor::ONE_MINUS_SRC_COLOR:
            return GL_ONE_MINUS_SRC_COLOR;
        case kBlendFactor::DST_ALPHA:
            return GL_DST_ALPHA;
        case kBlendFactor::ONE_MINUS_DST_ALPHA:
            return GL_ONE_MINUS_DST_ALPHA;
        default:
            return GL_ONE;
        }
    }

    GLenum kOpenGLDriver::toGLPrimitiveType(kPrimitiveType type)
    {
        switch (type)
        {
        case kPrimitiveType::TRIANGLES:
            return GL_TRIANGLES;
        case kPrimitiveType::TRIANGLE_STRIP:
            return GL_TRIANGLE_STRIP;
        case kPrimitiveType::TRIANGLE_FAN:
            return GL_TRIANGLE_FAN;
        case kPrimitiveType::LINES:
            return GL_LINES;
        case kPrimitiveType::LINE_STRIP:
            return GL_LINE_STRIP;
        case kPrimitiveType::POINTS:
            return GL_POINTS;
        default:
            return GL_TRIANGLES;
        }
    }
}
