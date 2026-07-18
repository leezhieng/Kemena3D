#include "kopenglesdriver.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <algorithm>

namespace kemena
{
    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    kOpenGLESDriver::~kOpenGLESDriver()
    {
        destroy();
    }

    bool kOpenGLESDriver::init(kWindow *window)
    {
        if (window == nullptr)
            return false;

        // Request an OpenGL ES 3.0 context.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

        // ES does not use forward-compatible flag.
        // SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG is desktop-only.

        glContext = SDL_GL_CreateContext(window->getSdlWindow());
        if (!glContext)
        {
            std::cout << "[kOpenGLESDriver] Failed to create OpenGL ES context: "
                      << SDL_GetError() << std::endl;
            return false;
        }

#if !defined(__ANDROID__) && !defined(KEMENA_GLES)
        // Desktop fallback: initialise GLEW so we can use the GL 3.3+ subset
        // that overlaps with ES 3.0.  On true ES platforms GLEW is not used.
        glewExperimental = GL_TRUE;
        GLenum status = glewInit();
        if (status != GLEW_OK)
        {
            std::cout << "[kOpenGLESDriver] GLEW Error: "
                      << glewGetErrorString(status) << std::endl;
            return false;
        }
#endif

        std::cout << "[kOpenGLESDriver] Version:  " << glGetString(GL_VERSION) << std::endl;
        std::cout << "[kOpenGLESDriver] GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
        std::cout << "[kOpenGLESDriver] Renderer: " << glGetString(GL_RENDERER) << std::endl;

        // ES 3.0 default state: sRGB framebuffer is not a toggleable feature;
        // instead we rely on sRGB internal texture formats where needed.
        // GL_SAMPLE_ALPHA_TO_COVERAGE is also unavailable in ES 3.0.

        glBindVertexArray(0);

        return true;
    }

    void kOpenGLESDriver::destroy()
    {
        if (glContext)
        {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
    }

    void *kOpenGLESDriver::getNativeContext()
    {
        return glContext;
    }

    kString kOpenGLESDriver::getApiVersion()
    {
        return reinterpret_cast<const char *>(glGetString(GL_VERSION));
    }

    kString kOpenGLESDriver::getShaderVersion()
    {
        return reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    }

    bool kOpenGLESDriver::isGLES()
    {
        const char *ver = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        if (!ver) return false;
        // OpenGL ES version strings contain "OpenGL ES".
        return (std::strstr(ver, "OpenGL ES") != nullptr) ||
               (std::strstr(ver, "OpenGL ES") != nullptr);
    }

    // -------------------------------------------------------------------------
    // Frame state
    // -------------------------------------------------------------------------

    void kOpenGLESDriver::setClearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    void kOpenGLESDriver::clear(bool color, bool depth, bool stencil)
    {
        GLbitfield mask = 0;
        if (color)   mask |= GL_COLOR_BUFFER_BIT;
        if (depth)   mask |= GL_DEPTH_BUFFER_BIT;
        if (stencil) mask |= GL_STENCIL_BUFFER_BIT;
        if (mask)    glClear(mask);
    }

    void kOpenGLESDriver::setViewport(int x, int y, int width, int height)
    {
        glViewport(x, y, width, height);
    }

    // -------------------------------------------------------------------------
    // Pipeline state
    // -------------------------------------------------------------------------

    void kOpenGLESDriver::setDepthTest(bool enable)
    {
        enable ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    void kOpenGLESDriver::setDepthWrite(bool enable)
    {
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
    }

    void kOpenGLESDriver::setBlend(bool enable)
    {
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    }

    void kOpenGLESDriver::setBlendFunc(kBlendFactor src, kBlendFactor dst)
    {
        glBlendFunc(toGLBlendFactor(src), toGLBlendFactor(dst));
    }

    void kOpenGLESDriver::setCullFace(bool enable)
    {
        enable ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }

    void kOpenGLESDriver::setCullMode(kCullMode mode)
    {
        switch (mode)
        {
        case kCullMode::BACK:           glCullFace(GL_BACK);           break;
        case kCullMode::FRONT:          glCullFace(GL_FRONT);          break;
        case kCullMode::FRONT_AND_BACK: glCullFace(GL_FRONT_AND_BACK); break;
        }
    }

    void kOpenGLESDriver::setFrontFace(kFrontFace face)
    {
        glFrontFace(face == kFrontFace::CCW ? GL_CCW : GL_CW);
    }

    void kOpenGLESDriver::setMultisample(bool enable)
    {
        enable ? glEnable(GL_MULTISAMPLE) : glDisable(GL_MULTISAMPLE);
    }

    void kOpenGLESDriver::setSRGBEncoding(bool enable)
    {
        // ES 3.0 has no GL_FRAMEBUFFER_SRGB toggle.
        // sRGB behaviour is determined by the internal format of the texture
        // or renderbuffer attached to the FBO.  This is a deliberate no-op.
        (void)enable;
    }

    void kOpenGLESDriver::setSampleAlphaToCoverage(bool enable)
    {
        // GL_SAMPLE_ALPHA_TO_COVERAGE is not available in ES 3.0.
        (void)enable;
    }

    void kOpenGLESDriver::setWireframe(bool enable)
    {
        // glPolygonMode is not available in OpenGL ES 3.0.
        (void)enable;
    }

    // -------------------------------------------------------------------------
    // Shader source adaptation (GLSL 330 → GLSL ES 300)
    // -------------------------------------------------------------------------

    kString kOpenGLESDriver::adaptShaderSource(const char *src, bool isFragment)
    {
        if (!src || !*src)
            return "";

        kString result(src);

        // Replace #version 330 core → #version 300 es
        {
            size_t pos = result.find("#version 330");
            if (pos != kString::npos)
            {
                // Find end of line
                size_t end = result.find('\n', pos);
                if (end == kString::npos)
                    end = result.size();
                result.replace(pos, end - pos, "#version 300 es");
            }
        }

        // If no #version directive at all, prepend one.
        if (result.find("#version") == kString::npos)
        {
            result = "#version 300 es\n" + result;
        }

        // For fragment shaders, inject precision after the #version line.
        if (isFragment)
        {
            size_t nl = result.find('\n');
            if (nl != kString::npos)
            {
                kString after = result.substr(nl + 1);
                // Only inject if not already present.
                if (after.find("precision") == kString::npos ||
                    after.find("precision") > after.find('\n'))
                {
                    result = result.substr(0, nl + 1) +
                             "precision mediump float;\n" + after;
                }
            }
        }

        // Fix GLSL 330 built-in references that don't exist in ES:
        // - gl_FragColor → custom output (handled by the shader using `out`)
        // - texture() already exists in ES 3.0, no change needed.
        // - gl_FragCoord exists in ES 3.0.
        // - layout(binding=X) → not supported; sampler binding via glUniform1i.

        // Remove layout(binding = N) qualifiers — ES 3.0 doesn't support them.
        size_t pos = 0;
        while ((pos = result.find("layout(binding", pos)) != kString::npos)
        {
            size_t end = result.find(')', pos);
            if (end != kString::npos)
            {
                // Also consume trailing whitespace/newline if present
                size_t consumeEnd = end + 1;
                if (consumeEnd < result.size() && result[consumeEnd] == ' ')
                    consumeEnd++;
                result.erase(pos, consumeEnd - pos);
            }
            else
            {
                break;
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Shader programs
    // -------------------------------------------------------------------------

    GLuint kOpenGLESDriver::compileShaderStage(GLenum stage, const char *src)
    {
        if (!src || !*src)
            return 0;

        bool isFrag = (stage == GL_FRAGMENT_SHADER);
        kString adapted = adaptShaderSource(src, isFrag);
        const char *adaptedSrc = adapted.c_str();

        GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &adaptedSrc, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_FALSE)
        {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1)
            {
                std::vector<char> err(logLen);
                glGetShaderInfoLog(shader, logLen, nullptr, err.data());
                std::cout << "[kOpenGLESDriver] Shader compile error ("
                          << (isFrag ? "fragment" : "vertex") << "): "
                          << err.data() << std::endl;
            }
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    uint32_t kOpenGLESDriver::compileShaderProgram(const char *vertSrc, const char *fragSrc)
    {
        GLuint vertShader = compileShaderStage(GL_VERTEX_SHADER, vertSrc);
        GLuint fragShader = compileShaderStage(GL_FRAGMENT_SHADER, fragSrc);

        if (!vertShader && !fragShader)
            return 0;

        GLuint program = glCreateProgram();
        if (vertShader) glAttachShader(program, vertShader);
        if (fragShader) glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint logLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1)
            {
                std::vector<char> err(logLen);
                glGetProgramInfoLog(program, logLen, nullptr, err.data());
                std::cout << "[kOpenGLESDriver] Program link error: "
                          << err.data() << std::endl;
            }
            glDeleteProgram(program);
            program = 0;
        }

        if (vertShader) glDeleteShader(vertShader);
        if (fragShader) glDeleteShader(fragShader);

        return static_cast<uint32_t>(program);
    }

    uint32_t kOpenGLESDriver::compileShaderProgramSpirv(const std::vector<uint8_t> &, const kString &,
                                                         const std::vector<uint8_t> &, const kString &)
    {
        std::cout << "[kOpenGLESDriver] SPIR-V is not available in OpenGL ES 3.0." << std::endl;
        return 0;
    }

    void kOpenGLESDriver::deleteShaderProgram(uint32_t id)
    {
        if (id) glDeleteProgram(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindShaderProgram(uint32_t id)
    {
        glUseProgram(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindShaderProgram()
    {
        glUseProgram(0);
    }

    // --- Uniforms ---

    void kOpenGLESDriver::setUniformBool(uint32_t progId, const kString &name, bool v)
    {
        glUniform1i(glGetUniformLocation(progId, name.c_str()), static_cast<int>(v));
    }

    void kOpenGLESDriver::setUniformInt(uint32_t progId, const kString &name, int v)
    {
        glUniform1i(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformUint(uint32_t progId, const kString &name, uint32_t v)
    {
        glUniform1ui(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformFloat(uint32_t progId, const kString &name, float v)
    {
        glUniform1f(glGetUniformLocation(progId, name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v)
    {
        glUniform2fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v)
    {
        glUniform3fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v)
    {
        glUniform4fv(glGetUniformLocation(progId, name.c_str()), 1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v)
    {
        glUniformMatrix4fv(glGetUniformLocation(progId, name.c_str()), 1, GL_FALSE, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v)
    {
        glUniformMatrix4fv(glGetUniformLocation(progId, name.c_str()),
                           static_cast<GLsizei>(v.size()), GL_FALSE, glm::value_ptr(v[0]));
    }

    // -------------------------------------------------------------------------
    // Vertex arrays
    // -------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createVertexArray()
    {
        GLuint id = 0;
        glGenVertexArrays(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteVertexArray(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId) glDeleteVertexArrays(1, &glId);
    }

    void kOpenGLESDriver::bindVertexArray(uint32_t id)
    {
        glBindVertexArray(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindVertexArray()
    {
        glBindVertexArray(0);
    }

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createBuffer()
    {
        GLuint id = 0;
        glGenBuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteBuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId) glDeleteBuffers(1, &glId);
    }

    void kOpenGLESDriver::uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    void kOpenGLESDriver::uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    void kOpenGLESDriver::updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    }

    void kOpenGLESDriver::setVertexAttribFloat(int location, int components, int stride, size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribPointer(static_cast<GLuint>(location), components, GL_FLOAT, GL_FALSE,
                              stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLESDriver::setVertexAttribInt(int location, int components, int stride, size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribIPointer(static_cast<GLuint>(location), components, GL_INT,
                               stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLESDriver::setVertexAttribDivisor(int location, int divisor)
    {
        glVertexAttribDivisor(static_cast<GLuint>(location), static_cast<GLuint>(divisor));
    }

    // -------------------------------------------------------------------------
    // Draw calls
    // -------------------------------------------------------------------------

    void kOpenGLESDriver::drawIndexed(uint32_t vaoId, int indexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArrays(toGLPrimitiveType(type), 0, vertexCount);
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawArraysInstanced(uint32_t vaoId, kPrimitiveType type, int vertexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArraysInstanced(toGLPrimitiveType(type), 0, vertexCount,
                              static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    // -------------------------------------------------------------------------
    // Texture sampling
    // -------------------------------------------------------------------------

    void kOpenGLESDriver::bindTexture2D(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindTexture2DArray(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindTextureCube(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindTexture2D(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::unbindTexture2DArray(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    void kOpenGLESDriver::unbindTextureCube(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void kOpenGLESDriver::generateMipmaps2D(uint32_t id)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    void kOpenGLESDriver::readTexture2DRGB(uint32_t id, int mipLevel, float *pixels)
    {
        // glGetTexImage is NOT available in ES 3.0.
        // Workaround: attach the texture to a temporary FBO and glReadPixels.
        // The caller (auto-exposure) works on the resolved FBO colour texture,
        // which is already attached to an FBO.  For standalone textures this
        // would require creating a throw-away FBO.

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, static_cast<GLuint>(id), mipLevel);

        GLint width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_HEIGHT, &height);

        if (width > 0 && height > 0 && pixels)
        {
            glReadPixels(0, 0, width, height, GL_RGB, GL_FLOAT, pixels);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    }

    void kOpenGLESDriver::readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
    {
        uint8_t pixel[4] = {0, 0, 0, 0};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
        a = pixel[3];
    }

    // -------------------------------------------------------------------------
    // Texture creation (for asset loading)
    // -------------------------------------------------------------------------

    static GLint toGLESWrap(kTextureWrap w)
    {
        switch (w)
        {
        case kTextureWrap::REPEAT:          return GL_REPEAT;
        case kTextureWrap::CLAMP_TO_EDGE:   return GL_CLAMP_TO_EDGE;
        case kTextureWrap::CLAMP_TO_BORDER: return GL_CLAMP_TO_EDGE;
        case kTextureWrap::MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
        }
    }

    static GLint toGLESMinFilter(kTextureFilter f, bool hasMips)
    {
        if (!hasMips)
        {
            switch (f)
            {
            case kTextureFilter::NEAREST: return GL_NEAREST;
            default:                      return GL_LINEAR;
            }
        }
        switch (f)
        {
        case kTextureFilter::NEAREST:                return GL_NEAREST_MIPMAP_NEAREST;
        case kTextureFilter::LINEAR:                 return GL_LINEAR_MIPMAP_NEAREST;
        case kTextureFilter::NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
        case kTextureFilter::LINEAR_MIPMAP_NEAREST:  return GL_LINEAR_MIPMAP_NEAREST;
        case kTextureFilter::NEAREST_MIPMAP_LINEAR:  return GL_NEAREST_MIPMAP_LINEAR;
        case kTextureFilter::LINEAR_MIPMAP_LINEAR:   return GL_LINEAR_MIPMAP_LINEAR;
        default: return GL_LINEAR_MIPMAP_LINEAR;
        }
    }

    static GLint toGLESMagFilter(kTextureFilter f)
    {
        switch (f)
        {
        case kTextureFilter::NEAREST: return GL_NEAREST;
        default:                      return GL_LINEAR;
        }
    }

    static GLint toGLESInternalFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:   return GL_RGB8;
        case kTextureFormat::TEX_FORMAT_RGBA:  return GL_RGBA8;
        case kTextureFormat::TEX_FORMAT_SRGB:  return GL_SRGB8;
        case kTextureFormat::TEX_FORMAT_SRGBA: return GL_SRGB8_ALPHA8;
        default: return GL_RGBA8;
        }
    }

    static GLenum toGLESBaseFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:
        case kTextureFormat::TEX_FORMAT_SRGB:  return GL_RGB;
        case kTextureFormat::TEX_FORMAT_RGBA:
        case kTextureFormat::TEX_FORMAT_SRGBA: return GL_RGBA;
        default: return GL_RGBA;
        }
    }

    uint32_t kOpenGLESDriver::createTexture2D(int width, int height, kTextureFormat format,
                                               const void *data,
                                               kTextureWrap wrap,
                                               kTextureFilter minFilter,
                                               kTextureFilter magFilter,
                                               bool generateMips)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toGLESWrap(wrap));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toGLESWrap(wrap));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, toGLESMinFilter(minFilter, generateMips));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, toGLESMagFilter(magFilter));

        if (data)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, toGLESInternalFormat(format),
                         width, height, 0, toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
            if (generateMips)
                glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createTextureCube(int width, int height,
                                                 const void *faceData[6],
                                                 bool generateMips)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);

        for (int i = 0; i < 6; ++i)
        {
            if (faceData[i])
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8,
                             width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, faceData[i]);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        if (generateMips)
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::uploadTexture2D(uint32_t id, int level, int width, int height,
                                           kTextureFormat format, const void *data)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, level, toGLESInternalFormat(format),
                     width, height, 0, toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                              int width, int height,
                                              kTextureFormat format, const void *data)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, level, x, y, width, height,
                        toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::uploadCompressedTexture2D(uint32_t id, int level,
                                                     int width, int height,
                                                     kTextureFormat format,
                                                     const void *data, size_t dataSize)
    {
        (void)format;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glCompressedTexImage2D(GL_TEXTURE_2D, level, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                               width, height, 0, (GLsizei)dataSize, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                                 const void *data)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(id));
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_SRGB8,
                     width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void kOpenGLESDriver::deleteTexture(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId)
            glDeleteTextures(1, &glId);
    }

    // -------------------------------------------------------------------------
    // Framebuffers
    // -------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createFramebuffer()
    {
        GLuint id = 0;
        glGenFramebuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteFramebuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId) glDeleteFramebuffers(1, &glId);
    }

    void kOpenGLESDriver::bindFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindReadFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindDrawFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindFramebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool kOpenGLESDriver::isFramebufferComplete()
    {
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void kOpenGLESDriver::blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                                int dstX0, int dstY0, int dstX1, int dstY1)
    {
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    void kOpenGLESDriver::setFramebufferDrawBuffer()
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }

    // -------------------------------------------------------------------------
    // Renderbuffers
    // -------------------------------------------------------------------------

    GLenum kOpenGLESDriver::depthStencilFormat()
    {
        // ES 3.0 standard format for combined depth+stencil.
        // Some implementations may only support GL_DEPTH24_STENCIL8_OES.
        return GL_DEPTH24_STENCIL8;
    }

    GLenum kOpenGLESDriver::depthFormat()
    {
        // ES 3.0 standard depth format (no stencil).
        return GL_DEPTH_COMPONENT24;
    }

    uint32_t kOpenGLESDriver::createRenderbuffer()
    {
        GLuint id = 0;
        glGenRenderbuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteRenderbuffer(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (glId) glDeleteRenderbuffers(1, &glId);
    }

    void kOpenGLESDriver::setupRenderbuffer(uint32_t rboId, int width, int height)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));
        glRenderbufferStorage(GL_RENDERBUFFER, depthStencilFormat(), width, height);
    }

    void kOpenGLESDriver::setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthStencilFormat(), width, height);
    }

    void kOpenGLESDriver::attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, static_cast<GLuint>(rboId));
    }

    // -------------------------------------------------------------------------
    // FBO-managed textures
    // -------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createFBOColorTexture(int width, int height)
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

    uint32_t kOpenGLESDriver::createFBOColorTextureMSAA(int samples, int width, int height)
    {
        // ES 3.0 does not have GL_TEXTURE_2D_MULTISAMPLE.
        // MSAA colour targets must be renderbuffers.
        // Return a multisample RBO instead and attach it directly.
        GLuint id = 0;
        glGenRenderbuffers(1, &id);
        glBindRenderbuffer(GL_RENDERBUFFER, id);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // Store as a negative ID so the renderer can distinguish it
        // from a regular texture.  MSAA "textures" are really RBOs in ES.
        // We return the RBO id directly — the caller uses attachFBOColorTextureMSAA
        // which knows it's an RBO.
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createFBODepthTexture(int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, depthFormat(), width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        // GL_TEXTURE_COMPARE_MODE is NOT available in ES 3.0 for standard
        // depth textures.  Shadow samplers use a different path (see
        // createFBODepthTextureArray below).
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createFBODepthTextureArray(int width, int height, int layers)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D_ARRAY, id);
        // ES 3.0 supports glTexImage3D for 2D arrays.
        // GL_DEPTH_COMPONENT32F is not guaranteed; fall back to GL_DEPTH_COMPONENT24.
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, depthFormat(),
                     width, height, layers, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

        // In ES 3.0, shadow sampling requires GL_TEXTURE_COMPARE_MODE.
        // This is available in ES 3.0 as an enum.
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteFBOTexture(uint32_t id)
    {
        GLuint glId = static_cast<GLuint>(id);
        if (!glId) return;

        // Check if this is actually a renderbuffer (MSAA color).
        // We can't easily tell, so try deleting as both.
        // glDeleteTextures on a renderbuffer name is a no-op (invalid object).
        glDeleteTextures(1, &glId);
        glDeleteRenderbuffers(1, &glId);
    }

    void kOpenGLESDriver::attachFBOColorTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);
    }

    void kOpenGLESDriver::attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId)
    {
        // In ES 3.0, MSAA colour targets are renderbuffers, not textures.
        // attachFBOColorTextureMSAA receives an RBO id.
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, static_cast<GLuint>(texId));
    }

    void kOpenGLESDriver::attachFBODepthTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLESDriver::attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  static_cast<GLuint>(texId), 0, layer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLESDriver::resizeFBOColorTexture(uint32_t texId, int width, int height)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texId));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height)
    {
        // MSAA colour targets are renderbuffers in ES 3.0.
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(texId));
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    GLenum kOpenGLESDriver::toGLBlendFactor(kBlendFactor factor)
    {
        switch (factor)
        {
        case kBlendFactor::ZERO:                  return GL_ZERO;
        case kBlendFactor::ONE:                   return GL_ONE;
        case kBlendFactor::SRC_ALPHA:             return GL_SRC_ALPHA;
        case kBlendFactor::ONE_MINUS_SRC_ALPHA:   return GL_ONE_MINUS_SRC_ALPHA;
        case kBlendFactor::SRC_COLOR:             return GL_SRC_COLOR;
        case kBlendFactor::ONE_MINUS_SRC_COLOR:   return GL_ONE_MINUS_SRC_COLOR;
        case kBlendFactor::DST_ALPHA:             return GL_DST_ALPHA;
        case kBlendFactor::ONE_MINUS_DST_ALPHA:   return GL_ONE_MINUS_DST_ALPHA;
        default:                                   return GL_ONE;
        }
    }

    GLenum kOpenGLESDriver::toGLPrimitiveType(kPrimitiveType type)
    {
        switch (type)
        {
        case kPrimitiveType::TRIANGLES:       return GL_TRIANGLES;
        case kPrimitiveType::TRIANGLE_STRIP:  return GL_TRIANGLE_STRIP;
        case kPrimitiveType::TRIANGLE_FAN:    return GL_TRIANGLE_FAN;
        case kPrimitiveType::LINES:           return GL_LINES;
        case kPrimitiveType::LINE_STRIP:      return GL_LINE_STRIP;
        case kPrimitiveType::POINTS:          return GL_POINTS;
        default:                               return GL_TRIANGLES;
        }
    }

} // namespace kemena
