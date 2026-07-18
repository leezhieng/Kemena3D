#include "kshader.h"

#include <sstream>

namespace kemena
{
    kShaderSource kShader::splitSource(const kString& src)
    {
        static const kString MARKER_VERT = "// --- VERTEX ---";
        static const kString MARKER_GEOM = "// --- GEOMETRY ---";
        static const kString MARKER_FRAG = "// --- FRAGMENT ---";

        kShaderSource result;

        // Walk through markers in order of appearance
        auto pos_v = src.find(MARKER_VERT);
        auto pos_g = src.find(MARKER_GEOM);
        auto pos_f = src.find(MARKER_FRAG);

        if (pos_f == kString::npos)
        {
            // No markers — treat whole source as fragment only
            result.fragment = src;
            return result;
        }

        if (pos_v != kString::npos)
            result.vertex = src.substr(pos_v + MARKER_VERT.size(), (pos_g != kString::npos ? pos_g : pos_f) - (pos_v + MARKER_VERT.size()));
        else
            result.vertex = src.substr(0, pos_g != kString::npos ? pos_g : pos_f);

        if (pos_g != kString::npos)
            result.geometry = src.substr(pos_g + MARKER_GEOM.size(), pos_f - (pos_g + MARKER_GEOM.size()));

        result.fragment = src.substr(pos_f + MARKER_FRAG.size());

        return result;
    }


    kShader::kShader()
    {
        shaderProgram = 0;
    }

    kShader::~kShader()
    {
        if (shaderProgram)
        {
            kDriver::getCurrent()->deleteShaderProgram(shaderProgram);
            shaderProgram = 0;
        }
    }

    kString kShader::readFile(const kString filePath)
    {
        kString content;
        std::ifstream fileStream(filePath.c_str(), std::ios::in);

        if (!fileStream.is_open())
        {
            std::cout << "Could not read file " << filePath << ". File does not exist." << std::endl;
            return "";
        }

        kString line = "";
        while (!fileStream.eof())
        {
            std::getline(fileStream, line);
            content.append(line + "\n");
        }

        fileStream.close();
        return content;
    }

    void kShader::loadShadersFile(const kString vertexShaderPath, const kString fragmentShaderPath)
    {
        const char *vertSrc = nullptr;
        const char *fragSrc = nullptr;
        kString vertStr, fragStr;

        if (!vertexShaderPath.empty())
        {
            vertStr = readFile(vertexShaderPath);
            vertSrc = vertStr.c_str();
        }
        if (!fragmentShaderPath.empty())
        {
            fragStr = readFile(fragmentShaderPath);
            fragSrc = fragStr.c_str();
        }

        shaderProgram = kDriver::getCurrent()->compileShaderProgram(vertSrc, fragSrc);
    }

    void kShader::loadShadersCode(const char *vertexShaderCode, const char *fragmentShaderCode)
    {
        shaderProgram = kDriver::getCurrent()->compileShaderProgram(vertexShaderCode, fragmentShaderCode);
    }

    void kShader::loadGlslFile(const kString& path)
    {
        kString src = readFile(path);
        if (src.empty()) { std::cout << "[kShader] loadGlslFile: could not read '" << path << "'." << std::endl; return; }
        loadGlslCode(src);
    }

    void kShader::loadGlslCode(const kString& src)
    {
        kShaderSource s = splitSource(src);
        shaderProgram = kDriver::getCurrent()->compileShaderProgram(
            s.vertex.empty()   ? nullptr : s.vertex.c_str(),
            s.fragment.empty() ? nullptr : s.fragment.c_str());
    }

    void kShader::use()
    {
        kDriver::getCurrent()->bindShaderProgram(shaderProgram);
    }

    void kShader::unuse()
    {
        kDriver::getCurrent()->unbindShaderProgram();
    }

    void kShader::setShaderProgram(uint32_t program)
    {
        shaderProgram = program;
    }

    uint32_t kShader::getShaderProgram()
    {
        return shaderProgram;
    }

    void kShader::setValue(kString name, std::vector<kMat4> value)
    {
        kDriver::getCurrent()->setUniformMat4Array(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, kMat4 value)
    {
        kDriver::getCurrent()->setUniformMat4(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, kVec4 value)
    {
        kDriver::getCurrent()->setUniformVec4(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, kVec3 value)
    {
        kDriver::getCurrent()->setUniformVec3(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, kVec2 value)
    {
        kDriver::getCurrent()->setUniformVec2(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, float value)
    {
        kDriver::getCurrent()->setUniformFloat(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, int value)
    {
        kDriver::getCurrent()->setUniformInt(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, unsigned int value)
    {
        kDriver::getCurrent()->setUniformUint(shaderProgram, name, value);
    }

    void kShader::setValue(kString name, bool value)
    {
        kDriver::getCurrent()->setUniformBool(shaderProgram, name, value);
    }

    // --- DirectX 11 ----------------------------------------------------------

    void kShader::loadHlslFileDX11(const kString& path)
    {
        kString src = readFile(path);
        if (src.empty())
        {
            std::cout << "[kShader] loadHlslFileDX11: could not read '" << path << "'." << std::endl;
            return;
        }
        loadHlslCodeDX11(src);
    }

    void kShader::loadHlslCodeDX11(const kString& src)
    {
        // Split the combined HLSL source using the same markers as GLSL
        kShaderSource s = splitSource(src);

        // For HLSL, the "vertex" section contains the VS entry (VSMain),
        // and "fragment" section contains the PS entry (PSMain).
        // The source is passed directly to the driver's compileShaderProgram,
        // which for D3D11 will compile as HLSL with vs_5_0 / ps_5_0 targets
        // and VSMain / PSMain entry points.
        shaderProgram = kDriver::getCurrent()->compileShaderProgram(
            s.vertex.empty()   ? nullptr : s.vertex.c_str(),
            s.fragment.empty() ? nullptr : s.fragment.c_str());
    }

    // --- DirectX 12 ----------------------------------------------------------

    void kShader::loadHlslFileDX12(const kString& path)
    {
        std::cout << "[kShader] DirectX 12 backend not implemented." << std::endl;
    }

    void kShader::loadHlslCodeDX12(const kString& src)
    {
        std::cout << "[kShader] DirectX 12 backend not implemented." << std::endl;
    }

    // --- Vulkan 1.3 ----------------------------------------------------------

    void kShader::loadSpirvFile(const kString& path)
    {
        std::cout << "[kShader] Vulkan 1.3 backend not implemented." << std::endl;
    }

    void kShader::loadSpirvCode(const kString& src)
    {
        std::cout << "[kShader] Vulkan 1.3 backend not implemented." << std::endl;
    }

    void kShader::loadSpirvBinary(const std::vector<uint8_t>& vertSpv, const std::vector<uint8_t>& fragSpv)
    {
        std::cout << "[kShader] Vulkan 1.3 backend not implemented." << std::endl;
    }

    // --- Metal ---------------------------------------------------------------

    void kShader::loadMetalFile(const kString& path)
    {
        std::cout << "[kShader] Metal backend not implemented." << std::endl;
    }

    void kShader::loadMetalCode(const kString& src)
    {
        std::cout << "[kShader] Metal backend not implemented." << std::endl;
    }
}
