/**
 * @file kdx11driver.cpp
 * @brief DirectX 11 implementation of the kDriver interface.
 */

#include "kdx11driver.h"

#ifdef KEMENA_D3D11

#include <glm/gtc/type_ptr.hpp>

// Link required D3D libraries (MSVC only; MinGW/Clang link via CMake)
#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace kemena
{
    // =========================================================================
    // D3D11ProgramData destructor
    // =========================================================================

    D3D11ProgramData::~D3D11ProgramData()
    {
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        for (auto &kv : constantBuffers)
            if (kv.second) { kv.second->Release(); kv.second = nullptr; }
    }

    // =========================================================================
    // kDX11Driver construction / destruction
    // =========================================================================

    kDX11Driver::kDX11Driver()
    {
        memset(clearColor, 0, sizeof(clearColor));
        memset(boundTextures, 0, sizeof(boundTextures));
        memset(currentRTVs, 0, sizeof(currentRTVs));
    }

    kDX11Driver::~kDX11Driver()
    {
        destroy();
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    bool kDX11Driver::init(kWindow *window)
    {
        if (window == nullptr)
            return false;

        SDL_Window *sdlWin = window->getSdlWindow();
        if (!sdlWin)
            return false;

        // Retrieve native HWND from SDL3
        SDL_PropertiesID props = SDL_GetWindowProperties(sdlWin);
        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!hwnd)
        {
            std::cout << "[kDX11Driver] Failed to get native HWND from SDL window." << std::endl;
            return false;
        }

        int width  = window->getWindowWidth();
        int height = window->getWindowHeight();

        // --- Create DXGI factory & enumerate adapters ------------------------
        IDXGIFactory *dxgiFactory = nullptr;
        HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&dxgiFactory);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateDXGIFactory failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        IDXGIAdapter *adapter = nullptr;
        hr = dxgiFactory->EnumAdapters(0, &adapter);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] EnumAdapters failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            dxgiFactory->Release();
            return false;
        }

        // --- Create D3D11 device & context -----------------------------------
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT createFlags = 0;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        hr = D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            createFlags, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);

        adapter->Release();

        if (FAILED(hr))
        {
            // Try without debug layer
            createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            IDXGIAdapter *adapter2 = nullptr;
            dxgiFactory->EnumAdapters(0, &adapter2);
            hr = D3D11CreateDevice(
                adapter2, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                createFlags, featureLevels, ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
            if (adapter2) adapter2->Release();
        }

        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] D3D11CreateDevice failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        std::cout << "[kDX11Driver] Device created. Feature level: 0x"
                  << std::hex << featureLevel << std::dec << std::endl;

        // --- Create swap chain -----------------------------------------------
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        scDesc.BufferCount       = 2;
        scDesc.BufferDesc.Width  = width;
        scDesc.BufferDesc.Height = height;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferDesc.RefreshRate.Numerator   = 60;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow      = hwnd;
        scDesc.SampleDesc.Count  = 1;
        scDesc.SampleDesc.Quality = 0;
        scDesc.Windowed          = TRUE;
        scDesc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        hr = dxgiFactory->CreateSwapChain(d3dDevice, &scDesc, &swapChain);
        dxgiFactory->Release();

        if (FAILED(hr) || !swapChain)
        {
            std::cout << "[kDX11Driver] CreateSwapChain failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        // --- Create back-buffer views ----------------------------------------
        if (!createBackBufferResources())
            return false;

        // --- Default pipeline state ------------------------------------------
        // Create default states; actual values are applied via applyPipelineState()
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable    = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc      = D3D11_COMPARISON_LESS;
        d3dDevice->CreateDepthStencilState(&dsDesc, &depthStencilState);

        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable           = FALSE;
        blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        d3dDevice->CreateBlendState(&blendDesc, &blendState);

        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode              = D3D11_FILL_SOLID;
        rsDesc.CullMode              = D3D11_CULL_BACK;
        rsDesc.FrontCounterClockwise = FALSE; // CCW = front
        rsDesc.DepthClipEnable       = TRUE;
        d3dDevice->CreateRasterizerState(&rsDesc, &rasterizerState);

        // Apply initial state
        depthStencilDirty = true;
        blendDirty        = true;
        rasterizerDirty   = true;

        // Default viewport
        currentViewport.TopLeftX = 0;
        currentViewport.TopLeftY = 0;
        currentViewport.Width    = (FLOAT)width;
        currentViewport.Height   = (FLOAT)height;
        currentViewport.MinDepth = 0.0f;
        currentViewport.MaxDepth = 1.0f;
        d3dContext->RSSetViewports(1, &currentViewport);

        return true;
    }

    void kDX11Driver::destroy()
    {
        releaseBackBufferResources();

        // Delete all programs
        programs.clear();

        // Delete all VAOs
        for (auto &kv : vertexArrays)
        {
            if (kv.second.inputLayout) kv.second.inputLayout->Release();
            if (kv.second.indexBuffer) kv.second.indexBuffer->Release();
            for (auto &vb : kv.second.vertexBuffers)
                if (vb.buffer) vb.buffer->Release();
        }
        vertexArrays.clear();

        // Delete all buffers
        for (auto &kv : buffers)
            if (kv.second) kv.second->Release();
        buffers.clear();

        // Delete all textures
        for (auto &kv : textures)
        {
            if (kv.second.texture) kv.second.texture->Release();
            if (kv.second.srv)     kv.second.srv->Release();
            if (kv.second.sampler) kv.second.sampler->Release();
        }
        textures.clear();

        // Delete all FBOs
        for (auto &kv : framebuffers)
        {
            for (auto *rtv : kv.second.colorRTVs) if (rtv) rtv->Release();
            if (kv.second.depthDSV) kv.second.depthDSV->Release();
            for (auto *tex : kv.second.colorTextures) if (tex) tex->Release();
            if (kv.second.depthTexture) kv.second.depthTexture->Release();
            for (auto *srv : kv.second.colorSRVs) if (srv) srv->Release();
        }
        framebuffers.clear();

        // Release pipeline states
        if (depthStencilState) { depthStencilState->Release(); depthStencilState = nullptr; }
        if (blendState)        { blendState->Release();        blendState        = nullptr; }
        if (rasterizerState)   { rasterizerState->Release();   rasterizerState   = nullptr; }

        if (swapChain)  { swapChain->Release();  swapChain  = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice)  { d3dDevice->Release();  d3dDevice  = nullptr; }
    }

    void *kDX11Driver::getNativeContext()
    {
        return d3dDevice;
    }

    kString kDX11Driver::getApiVersion()
    {
        if (!d3dDevice) return "DirectX 11 (no device)";

        char buf[128];
        snprintf(buf, sizeof(buf), "Direct3D 11 Feature Level 0x%04x", (unsigned)featureLevel);
        return kString(buf);
    }

    kString kDX11Driver::getShaderVersion()
    {
        // D3D11 uses HLSL Shader Model 5.0
        return "HLSL Shader Model 5.0";
    }

    void *kDX11Driver::getImTextureID(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end() && it->second.srv)
            return static_cast<void *>(it->second.srv);
        return reinterpret_cast<void *>(static_cast<intptr_t>(id));
    }

    void kDX11Driver::swapBuffers()
    {
        if (swapChain)
            swapChain->Present(1, 0); // Present with VSync
    }

    // =========================================================================
    // Frame state
    // =========================================================================

    void kDX11Driver::setClearColor(float r, float g, float b, float a)
    {
        clearColor[0] = r;
        clearColor[1] = g;
        clearColor[2] = b;
        clearColor[3] = a;
    }

    void kDX11Driver::clear(bool color, bool depth, bool stencil)
    {
        // Determine the active RTV and DSV
        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;

        if (currentFBO == 0)
        {
            rtv = backBufferRTV;
            dsv = backBufferDSV;
        }
        else
        {
            auto it = framebuffers.find(currentFBO);
            if (it != framebuffers.end())
            {
                if (!it->second.colorRTVs.empty())
                    rtv = it->second.colorRTVs[0];
                dsv = it->second.depthDSV;
            }
        }

        if (color && rtv)
            d3dContext->ClearRenderTargetView(rtv, clearColor);

        if (dsv)
        {
            UINT clearFlags = 0;
            if (depth)   clearFlags |= D3D11_CLEAR_DEPTH;
            if (stencil) clearFlags |= D3D11_CLEAR_STENCIL;
            if (clearFlags)
                d3dContext->ClearDepthStencilView(dsv, clearFlags, 1.0f, 0);
        }
    }

    void kDX11Driver::setViewport(int x, int y, int width, int height)
    {
        currentViewport.TopLeftX = (FLOAT)x;
        currentViewport.TopLeftY = (FLOAT)y;
        currentViewport.Width    = (FLOAT)width;
        currentViewport.Height   = (FLOAT)height;
        d3dContext->RSSetViewports(1, &currentViewport);
    }

    // =========================================================================
    // Pipeline state
    // =========================================================================

    void kDX11Driver::setDepthTest(bool enable)
    {
        if (depthTestEnabled != enable)
        {
            depthTestEnabled = enable;
            depthStencilDirty = true;
        }
    }

    void kDX11Driver::setDepthWrite(bool enable)
    {
        if (depthWriteEnabled != enable)
        {
            depthWriteEnabled = enable;
            depthStencilDirty = true;
        }
    }

    void kDX11Driver::setBlend(bool enable)
    {
        if (blendEnabled != enable)
        {
            blendEnabled = enable;
            blendDirty = true;
        }
    }

    void kDX11Driver::setBlendFunc(kBlendFactor src, kBlendFactor dst)
    {
        if (blendSrc != src || blendDst != dst)
        {
            blendSrc = src;
            blendDst = dst;
            blendDirty = true;
        }
    }

    void kDX11Driver::setCullFace(bool enable)
    {
        if (cullFaceEnabled != enable)
        {
            cullFaceEnabled = enable;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setCullMode(kCullMode mode)
    {
        if (cullMode != mode)
        {
            cullMode = mode;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setFrontFace(kFrontFace face)
    {
        if (frontFace != face)
        {
            frontFace = face;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setMultisample(bool enable)
    {
        msaaEnabled = enable;
        // D3D11 multisampling is configured per render-target at creation time;
        // this toggle is a hint that may affect subsequent FBO texture creation.
    }

    void kDX11Driver::setSRGBEncoding(bool enable)
    {
        srgbEnabled = enable;
        // For D3D11, sRGB encoding is per-RTV format (e.g. _SRGB variants).
        // The back buffer is typically sRGB; FBO textures can be created with
        // sRGB formats when this flag is on.
    }

    void kDX11Driver::setSampleAlphaToCoverage(bool enable)
    {
        alphaToCoverage = enable;
        // D3D11 handles alpha-to-coverage via BlendState.AlphaToCoverageEnable.
        blendDirty = true;
    }

    void kDX11Driver::setWireframe(bool enable)
    {
        if (wireframeEnabled != enable)
        {
            wireframeEnabled = enable;
            rasterizerDirty = true;
        }
    }

    // =========================================================================
    // Shader programs
    // =========================================================================

    ID3DBlob *kDX11Driver::compileHlslStage(const char *src, const char *entryPoint,
                                             const char *target)
    {
        if (!src || !src[0])
            return nullptr;

        ID3DBlob *codeBlob = nullptr;
        ID3DBlob *errorBlob = nullptr;

        HRESULT hr = D3DCompile(
            src, strlen(src), nullptr, // source name (optional)
            nullptr,                   // defines
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint, target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR,
            0, &codeBlob, &errorBlob);

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cout << "[kDX11Driver] HLSL compile error (" << target << "): "
                          << (const char *)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            else
            {
                std::cout << "[kDX11Driver] HLSL compile error (" << target
                          << "): HRESULT 0x" << std::hex << hr << std::dec << std::endl;
            }
            return nullptr;
        }

        if (errorBlob) errorBlob->Release();
        return codeBlob;
    }

    void kDX11Driver::reflectConstantBuffers(ID3DBlob *vsBlob, ID3DBlob *psBlob,
                                              D3D11ProgramData &prog)
    {
        auto reflectStage = [&](ID3DBlob *blob, ID3D11ShaderReflection *reflector)
        {
            D3D11_SHADER_DESC shaderDesc;
            reflector->GetDesc(&shaderDesc);

            for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
            {
                ID3D11ShaderReflectionConstantBuffer *cb =
                    reflector->GetConstantBufferByIndex(i);

                D3D11_SHADER_BUFFER_DESC cbDesc;
                cb->GetDesc(&cbDesc);

                // Only process bound constant buffers (skip $Globals etc. if not bound)
                if (cbDesc.Type != D3D_CT_CBUFFER)
                    continue;

                // Determine register slot from the bind point
                D3D11_SHADER_INPUT_BIND_DESC bindDesc;
                // Iterate resources to find this CB's bind point
                for (UINT r = 0; r < shaderDesc.BoundResources; ++r)
                {
                    reflector->GetResourceBindingDesc(r, &bindDesc);
                    if (bindDesc.Type == D3D_SIT_CBUFFER &&
                        strcmp(bindDesc.Name, cbDesc.Name) == 0)
                    {
                        uint32_t slot = bindDesc.BindPoint;

                        // Ensure CB exists
                        if (prog.cbSizes.find(slot) == prog.cbSizes.end() ||
                            cbDesc.Size > prog.cbSizes[slot])
                        {
                            prog.cbSizes[slot] = cbDesc.Size;
                            prog.cbShadows[slot].resize(cbDesc.Size, 0);
                            prog.cbDirty[slot] = false;
                        }

                        // Enumerate variables
                        for (UINT v = 0; v < cbDesc.Variables; ++v)
                        {
                            ID3D11ShaderReflectionVariable *var =
                                cb->GetVariableByIndex(v);

                            D3D11_SHADER_VARIABLE_DESC varDesc;
                            var->GetDesc(&varDesc);

                            D3D11UniformInfo info;
                            info.cbSlot = slot;
                            info.offset = varDesc.StartOffset;
                            info.size   = varDesc.Size;

                            prog.uniforms[kString(varDesc.Name)] = info;
                        }
                        break;
                    }
                }
            }
        };

        if (vsBlob)
        {
            ID3D11ShaderReflection *vsRefl = nullptr;
            D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                       IID_ID3D11ShaderReflection, (void **)&vsRefl);
            if (vsRefl)
            {
                reflectStage(vsBlob, vsRefl);
                vsRefl->Release();
            }
        }

        if (psBlob)
        {
            ID3D11ShaderReflection *psRefl = nullptr;
            D3DReflect(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                       IID_ID3D11ShaderReflection, (void **)&psRefl);
            if (psRefl)
            {
                reflectStage(psBlob, psRefl);
                psRefl->Release();
            }
        }
    }

    ID3D11InputLayout *kDX11Driver::createInputLayout(
        ID3DBlob *vsBlob, const std::vector<D3D11AttribDesc> &attribs)
    {
        if (!vsBlob || attribs.empty())
            return nullptr;

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        for (const auto &a : attribs)
        {
            D3D11_INPUT_ELEMENT_DESC elem = {};
            elem.SemanticName         = "TEXCOORD"; // Will be overridden below
            elem.SemanticIndex        = (UINT)a.location;
            elem.Format               = a.isInteger ? DXGI_FORMAT_R32G32B32A32_SINT
                                                    : DXGI_FORMAT_R32G32B32A32_FLOAT;
            elem.InputSlot            = 0;
            elem.AlignedByteOffset    = (UINT)a.offset;
            elem.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
            elem.InstanceDataStepRate = 0;

            // Adjust format based on component count
            if (!a.isInteger)
            {
                switch (a.components)
                {
                case 1: elem.Format = DXGI_FORMAT_R32_FLOAT;          break;
                case 2: elem.Format = DXGI_FORMAT_R32G32_FLOAT;       break;
                case 3: elem.Format = DXGI_FORMAT_R32G32B32_FLOAT;    break;
                case 4: elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
                }
            }
            else
            {
                switch (a.components)
                {
                case 1: elem.Format = DXGI_FORMAT_R32_SINT;          break;
                case 2: elem.Format = DXGI_FORMAT_R32G32_SINT;       break;
                case 3: elem.Format = DXGI_FORMAT_R32G32B32_SINT;    break;
                case 4: elem.Format = DXGI_FORMAT_R32G32B32A32_SINT; break;
                }
            }

            // Use proper semantic names based on typical attribute usage
            if (a.location == 0) { elem.SemanticName = "POSITION";  elem.SemanticIndex = 0; }
            else
            {
                // Map other locations: 1=COLOR, 2=TEXCOORD, 3=NORMAL, 4=TANGENT, 5=BINORMAL, 6=BLENDINDICES, 7=BLENDWEIGHT
                switch (a.location)
                {
                case 1: elem.SemanticName = "COLOR";        elem.SemanticIndex = 0; break;
                case 2: elem.SemanticName = "TEXCOORD";     elem.SemanticIndex = 0; break;
                case 3: elem.SemanticName = "NORMAL";       elem.SemanticIndex = 0; break;
                case 4: elem.SemanticName = "TANGENT";      elem.SemanticIndex = 0; break;
                case 5: elem.SemanticName = "BINORMAL";     elem.SemanticIndex = 0; break;
                case 6: elem.SemanticName = "BLENDINDICES"; elem.SemanticIndex = 0; break;
                case 7: elem.SemanticName = "BLENDWEIGHT";  elem.SemanticIndex = 0; break;
                default: elem.SemanticName = "TEXCOORD";    elem.SemanticIndex = (UINT)(a.location - 2); break;
                }
            }

            elements.push_back(elem);
        }

        ID3D11InputLayout *layout = nullptr;
        HRESULT hr = d3dDevice->CreateInputLayout(
            elements.data(), (UINT)elements.size(),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            &layout);

        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateInputLayout failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        return layout;
    }

    void kDX11Driver::flushConstantBuffers(D3D11ProgramData &prog)
    {
        for (auto &kv : prog.cbDirty)
        {
            if (!kv.second)
                continue;

            uint32_t slot = kv.first;
            auto it = prog.constantBuffers.find(slot);

            // Create CB if not exists
            if (it == prog.constantBuffers.end() || !it->second)
            {
                if (it != prog.constantBuffers.end() && it->second)
                    it->second->Release();

                D3D11_BUFFER_DESC cbDesc = {};
                cbDesc.ByteWidth      = prog.cbSizes[slot];
                cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
                cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
                cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

                ID3D11Buffer *cb = nullptr;
                HRESULT hr = d3dDevice->CreateBuffer(&cbDesc, nullptr, &cb);
                if (FAILED(hr))
                {
                    std::cout << "[kDX11Driver] Failed to create CB slot " << slot << std::endl;
                    kv.second = false;
                    continue;
                }
                prog.constantBuffers[slot] = cb;
                it = prog.constantBuffers.find(slot);
            }

            // Upload shadow data
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = d3dContext->Map(it->second, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (SUCCEEDED(hr))
            {
                memcpy(mapped.pData, prog.cbShadows[slot].data(), prog.cbShadows[slot].size());
                d3dContext->Unmap(it->second, 0);

                // Bind to pipeline
                d3dContext->VSSetConstantBuffers(slot, 1, &it->second);
                d3dContext->PSSetConstantBuffers(slot, 1, &it->second);
            }

            kv.second = false;
        }
    }

    uint32_t kDX11Driver::compileShaderProgram(const char *vertSrc, const char *fragSrc)
    {
        if (!d3dDevice)
            return 0;

        // Compile vertex shader
        ID3DBlob *vsBlob = compileHlslStage(vertSrc, "VSMain", "vs_5_0");
        // If VSMain entry fails, try "main"
        if (!vsBlob && vertSrc && vertSrc[0])
            vsBlob = compileHlslStage(vertSrc, "main", "vs_5_0");

        // Compile pixel shader
        ID3DBlob *psBlob = nullptr;
        if (fragSrc && fragSrc[0])
        {
            psBlob = compileHlslStage(fragSrc, "PSMain", "ps_5_0");
            if (!psBlob)
                psBlob = compileHlslStage(fragSrc, "main", "ps_5_0");
        }

        if (!vsBlob && !psBlob)
            return 0;

        D3D11ProgramData prog;

        // Create vertex shader
        if (vsBlob)
        {
            HRESULT hr = d3dDevice->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                nullptr, &prog.vs);
            if (FAILED(hr))
            {
                std::cout << "[kDX11Driver] CreateVertexShader failed: 0x"
                          << std::hex << hr << std::dec << std::endl;
                vsBlob->Release();
                if (psBlob) psBlob->Release();
                return 0;
            }
        }

        // Create pixel shader
        if (psBlob)
        {
            HRESULT hr = d3dDevice->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                nullptr, &prog.ps);
            if (FAILED(hr))
            {
                std::cout << "[kDX11Driver] CreatePixelShader failed: 0x"
                          << std::hex << hr << std::dec << std::endl;
                if (vsBlob) vsBlob->Release();
                psBlob->Release();
                return 0;
            }
        }

        // Reflect constant buffers
        reflectConstantBuffers(vsBlob, psBlob, prog);

        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();

        uint32_t id = nextProgramId++;
        programs[id] = std::move(prog);
        return id;
    }

    uint32_t kDX11Driver::compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                                     const kString &vertEntry,
                                                     const std::vector<uint8_t> &fragSpirv,
                                                     const kString &fragEntry)
    {
        std::cout << "[kDX11Driver] SPIR-V shaders are not supported on D3D11." << std::endl;
        return 0;
    }

    void kDX11Driver::deleteShaderProgram(uint32_t id)
    {
        programs.erase(id);
    }

    void kDX11Driver::bindShaderProgram(uint32_t id)
    {
        currentProgram = id;

        auto it = programs.find(id);
        if (it == programs.end())
            return;

        D3D11ProgramData &prog = it->second;
        d3dContext->VSSetShader(prog.vs, nullptr, 0);
        d3dContext->PSSetShader(prog.ps, nullptr, 0);
        d3dContext->IASetInputLayout(prog.inputLayout);
    }

    void kDX11Driver::unbindShaderProgram()
    {
        currentProgram = 0;
        d3dContext->VSSetShader(nullptr, nullptr, 0);
        d3dContext->PSSetShader(nullptr, nullptr, 0);
        d3dContext->IASetInputLayout(nullptr);
    }

    // =========================================================================
    // Uniform helpers
    // =========================================================================

    static void writeUniform(D3D11ProgramData &prog, const kString &name,
                             const void *data, size_t size)
    {
        auto it = prog.uniforms.find(name);
        if (it == prog.uniforms.end())
        {
            // Try with a "$Globals." prefix or without CB prefix — the reflection
            // sometimes prepends the constant-buffer name.
            // For now, silently ignore unknown uniforms (they may be in a
            // different CB that we haven't reflected yet).
            return;
        }

        const D3D11UniformInfo &info = it->second;
        auto shadowIt = prog.cbShadows.find(info.cbSlot);
        if (shadowIt == prog.cbShadows.end())
            return;

        size_t copySize = (size < info.size) ? size : info.size;
        if (info.offset + copySize > shadowIt->second.size())
            return;

        memcpy(shadowIt->second.data() + info.offset, data, copySize);
        prog.cbDirty[info.cbSlot] = true;
    }

    void kDX11Driver::setUniformBool(uint32_t progId, const kString &name, bool v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        int iv = v ? 1 : 0;
        writeUniform(it->second, name, &iv, sizeof(int));
    }

    void kDX11Driver::setUniformInt(uint32_t progId, const kString &name, int v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(int));
    }

    void kDX11Driver::setUniformUint(uint32_t progId, const kString &name, uint32_t v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(uint32_t));
    }

    void kDX11Driver::setUniformFloat(uint32_t progId, const kString &name, float v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(float));
    }

    void kDX11Driver::setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec2));
    }

    void kDX11Driver::setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec3));
    }

    void kDX11Driver::setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec4));
    }

    void kDX11Driver::setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        // GLM matrices are column-major; D3D11 HLSL defaults to column-major
        // with D3DCOMPILE_PACK_MATRIX_ROW_MAJOR we told the compiler to use
        // row-major.  However, we still need to match the layout the shader
        // expects.  Our HLSL shaders use row_major matrices.  GLM's memory
        // layout is column-major, so we need to transpose when uploading.
        kMat4 transposed = glm::transpose(v);
        writeUniform(it->second, name, glm::value_ptr(transposed), sizeof(kMat4));
    }

    void kDX11Driver::setUniformMat4Array(uint32_t progId, const kString &name,
                                           const std::vector<kMat4> &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;

        auto ui = it->second.uniforms.find(name);
        if (ui == it->second.uniforms.end()) return;

        const D3D11UniformInfo &info = ui->second;
        auto shadowIt = it->second.cbShadows.find(info.cbSlot);
        if (shadowIt == it->second.cbShadows.end()) return;

        size_t totalSize = v.size() * sizeof(kMat4);
        if (info.offset + totalSize > shadowIt->second.size())
            totalSize = shadowIt->second.size() - info.offset;

        // Transpose each matrix before upload
        std::vector<kMat4> transposed(v.size());
        for (size_t i = 0; i < v.size(); ++i)
            transposed[i] = glm::transpose(v[i]);

        memcpy(shadowIt->second.data() + info.offset, transposed.data(), totalSize);
        it->second.cbDirty[info.cbSlot] = true;
    }

    // =========================================================================
    // Vertex arrays
    // =========================================================================

    uint32_t kDX11Driver::createVertexArray()
    {
        uint32_t id = nextVAOId++;
        vertexArrays[id] = D3D11VertexArrayData();
        return id;
    }

    void kDX11Driver::deleteVertexArray(uint32_t id)
    {
        auto it = vertexArrays.find(id);
        if (it != vertexArrays.end())
        {
            if (it->second.inputLayout) it->second.inputLayout->Release();
            vertexArrays.erase(it);
        }
    }

    void kDX11Driver::bindVertexArray(uint32_t id)
    {
        currentVAO = id;
    }

    void kDX11Driver::unbindVertexArray()
    {
        currentVAO = 0;
    }

    // =========================================================================
    // Buffers
    // =========================================================================

    uint32_t kDX11Driver::createBuffer()
    {
        uint32_t id = nextBufferId++;
        buffers[id] = nullptr;
        return id;
    }

    void kDX11Driver::deleteBuffer(uint32_t id)
    {
        auto it = buffers.find(id);
        if (it != buffers.end())
        {
            if (it->second) it->second->Release();
            buffers.erase(it);
        }
    }

    void kDX11Driver::uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        // Release old buffer if any
        auto it = buffers.find(bufferId);
        if (it != buffers.end() && it->second)
        {
            it->second->Release();
            it->second = nullptr;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth     = (UINT)size;
        desc.Usage         = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags     = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data;

        ID3D11Buffer *buf = nullptr;
        HRESULT hr = d3dDevice->CreateBuffer(&desc, &initData, &buf);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] uploadIndexBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return;
        }

        buffers[bufferId] = buf;

        // If this buffer is associated with a VAO, store it
        auto vaIt = vertexArrays.find(currentVAO);
        if (vaIt != vertexArrays.end())
        {
            if (vaIt->second.indexBuffer) vaIt->second.indexBuffer->Release();
            vaIt->second.indexBuffer = buf;
            buf->AddRef(); // VAO also holds a reference
            vaIt->second.indexCount = (uint32_t)(size / sizeof(uint32_t));
            vaIt->second.indexFormat = DXGI_FORMAT_R32_UINT;
        }
    }

    void kDX11Driver::uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        auto it = buffers.find(bufferId);
        if (it != buffers.end() && it->second)
        {
            it->second->Release();
            it->second = nullptr;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth     = (UINT)size;
        desc.Usage         = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags     = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data;

        ID3D11Buffer *buf = nullptr;
        HRESULT hr = d3dDevice->CreateBuffer(&desc, &initData, &buf);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] uploadVertexBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return;
        }

        buffers[bufferId] = buf;
    }

    void kDX11Driver::updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset)
    {
        auto it = buffers.find(bufferId);
        if (it == buffers.end() || !it->second)
            return;

        D3D11_BUFFER_DESC desc;
        it->second->GetDesc(&desc);

        // If the buffer was created as IMMUTABLE, we can't update it.
        // Create a new DYNAMIC buffer instead.
        if (desc.Usage == D3D11_USAGE_IMMUTABLE)
        {
            // Release old, create new dynamic
            ID3D11Buffer *oldBuf = it->second;

            D3D11_BUFFER_DESC newDesc = {};
            newDesc.ByteWidth      = desc.ByteWidth;
            newDesc.Usage          = D3D11_USAGE_DYNAMIC;
            newDesc.BindFlags      = desc.BindFlags;
            newDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = data;
            // We can't initialize a dynamic buffer with partial data easily using
            // subresource, so we'll create it then map+update.

            ID3D11Buffer *newBuf = nullptr;
            HRESULT hr = d3dDevice->CreateBuffer(&newDesc, nullptr, &newBuf);
            if (SUCCEEDED(hr))
            {
                D3D11_MAPPED_SUBRESOURCE mapped;
                hr = d3dContext->Map(newBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                if (SUCCEEDED(hr))
                {
                    memcpy((uint8_t *)mapped.pData + offset, data, size);
                    d3dContext->Unmap(newBuf, 0);
                }
                it->second = newBuf;
                oldBuf->Release();
            }
            return;
        }

        // Dynamic buffer — map and update
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = d3dContext->Map(it->second, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            // Buffer still in use; use discard
            hr = d3dContext->Map(it->second, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        }
        if (SUCCEEDED(hr))
        {
            memcpy((uint8_t *)mapped.pData + offset, data, size);
            d3dContext->Unmap(it->second, 0);
        }
    }

    void kDX11Driver::setVertexAttribFloat(int location, int components, int stride, size_t offset)
    {
        auto it = vertexArrays.find(currentVAO);
        if (it == vertexArrays.end())
            return;

        D3D11AttribDesc desc;
        desc.location   = location;
        desc.components = components;
        desc.stride     = stride;
        desc.offset     = offset;
        desc.isInteger  = false;
        it->second.attribs.push_back(desc);
    }

    void kDX11Driver::setVertexAttribInt(int location, int components, int stride, size_t offset)
    {
        auto it = vertexArrays.find(currentVAO);
        if (it == vertexArrays.end())
            return;

        D3D11AttribDesc desc;
        desc.location   = location;
        desc.components = components;
        desc.stride     = stride;
        desc.offset     = offset;
        desc.isInteger  = true;
        it->second.attribs.push_back(desc);
    }

    void kDX11Driver::setVertexAttribDivisor(int location, int divisor)
    {
        // In D3D11, instance divisors are set in the input layout element
        // (D3D11_INPUT_ELEMENT_DESC::InstanceDataStepRate).
        // Since we create input layouts lazily, we store this in the attrib
        // descriptor. For now, we note it for future refinement.
        (void)location;
        (void)divisor;
    }

    // =========================================================================
    // Draw calls
    // =========================================================================

    void kDX11Driver::applyPipelineState()
    {
        // Depth-stencil state
        if (depthStencilDirty && depthStencilState)
        {
            depthStencilState->Release();
            depthStencilState = nullptr;

            D3D11_DEPTH_STENCIL_DESC dsDesc = {};
            dsDesc.DepthEnable    = depthTestEnabled ? TRUE : FALSE;
            dsDesc.DepthWriteMask = depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc      = D3D11_COMPARISON_LESS;
            d3dDevice->CreateDepthStencilState(&dsDesc, &depthStencilState);

            if (depthStencilState)
                d3dContext->OMSetDepthStencilState(depthStencilState, 0);
            depthStencilDirty = false;
        }

        // Blend state
        if (blendDirty && blendState)
        {
            blendState->Release();
            blendState = nullptr;

            D3D11_BLEND_DESC bDesc = {};
            bDesc.AlphaToCoverageEnable  = alphaToCoverage ? TRUE : FALSE;
            bDesc.IndependentBlendEnable = FALSE;
            bDesc.RenderTarget[0].BlendEnable           = blendEnabled ? TRUE : FALSE;
            bDesc.RenderTarget[0].SrcBlend              = toD3DBlend(blendSrc);
            bDesc.RenderTarget[0].DestBlend             = toD3DBlend(blendDst);
            bDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
            bDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
            bDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
            bDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            bDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            d3dDevice->CreateBlendState(&bDesc, &blendState);

            if (blendState)
                d3dContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
            blendDirty = false;
        }

        // Rasterizer state
        if (rasterizerDirty && rasterizerState)
        {
            rasterizerState->Release();
            rasterizerState = nullptr;

            D3D11_RASTERIZER_DESC rsDesc = {};
            rsDesc.FillMode = wireframeEnabled ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
            rsDesc.CullMode = cullFaceEnabled ? toD3DCullMode(cullMode) : D3D11_CULL_NONE;
            rsDesc.FrontCounterClockwise = (frontFace == kFrontFace::CCW) ? TRUE : FALSE;
            rsDesc.DepthClipEnable       = TRUE;
            rsDesc.ScissorEnable         = FALSE;
            rsDesc.MultisampleEnable     = msaaEnabled ? TRUE : FALSE;
            // Polygon offset for wireframe to prevent z-fighting
            if (wireframeEnabled)
            {
                rsDesc.DepthBias             = -1;
                rsDesc.SlopeScaledDepthBias  = -1.0f;
            }
            d3dDevice->CreateRasterizerState(&rsDesc, &rasterizerState);

            if (rasterizerState)
                d3dContext->RSSetState(rasterizerState);
            rasterizerDirty = false;
        }
    }

    void kDX11Driver::drawIndexed(uint32_t vaoId, int indexCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        D3D11VertexArrayData &va = vaIt->second;

        // Build input layout if not yet created
        if (!va.inputLayout && !va.attribs.empty() && currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
            {
                // We need the VS blob to create the input layout. Since we
                // didn't store it, we'll create a basic layout from the attribs.
                // In a production engine, you'd cache the VS blob or use a
                // pre-baked input layout.  For now, build from attrib info.
                //
                // Create a simple input layout using the stored attribs:
                std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
                for (const auto &a : va.attribs)
                {
                    D3D11_INPUT_ELEMENT_DESC elem = {};
                    elem.InputSlot            = 0;
                    elem.AlignedByteOffset    = (UINT)a.offset;
                    elem.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
                    elem.InstanceDataStepRate = 0;

                    switch (a.location)
                    {
                    case 0: elem.SemanticName = "POSITION";      elem.SemanticIndex = 0; break;
                    case 1: elem.SemanticName = "COLOR";         elem.SemanticIndex = 0; break;
                    case 2: elem.SemanticName = "TEXCOORD";      elem.SemanticIndex = 0; break;
                    case 3: elem.SemanticName = "NORMAL";        elem.SemanticIndex = 0; break;
                    case 4: elem.SemanticName = "TANGENT";       elem.SemanticIndex = 0; break;
                    case 5: elem.SemanticName = "BINORMAL";      elem.SemanticIndex = 0; break;
                    case 6: elem.SemanticName = "BLENDINDICES";  elem.SemanticIndex = 0; break;
                    case 7: elem.SemanticName = "BLENDWEIGHT";   elem.SemanticIndex = 0; break;
                    default: elem.SemanticName = "TEXCOORD";     elem.SemanticIndex = (UINT)(a.location - 2); break;
                    }

                    if (a.isInteger)
                    {
                        switch (a.components)
                        {
                        case 1: elem.Format = DXGI_FORMAT_R32_SINT;          break;
                        case 2: elem.Format = DXGI_FORMAT_R32G32_SINT;       break;
                        case 3: elem.Format = DXGI_FORMAT_R32G32B32_SINT;    break;
                        case 4: elem.Format = DXGI_FORMAT_R32G32B32A32_SINT; break;
                        }
                    }
                    else
                    {
                        switch (a.components)
                        {
                        case 1: elem.Format = DXGI_FORMAT_R32_FLOAT;          break;
                        case 2: elem.Format = DXGI_FORMAT_R32G32_FLOAT;       break;
                        case 3: elem.Format = DXGI_FORMAT_R32G32B32_FLOAT;    break;
                        case 4: elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
                        }
                    }

                    elements.push_back(elem);
                }

                if (!elements.empty())
                {
                    // We need a VS signature for CreateInputLayout. Use a
                    // minimal dummy VS blob.  In practice, kDX11Driver would
                    // store the VS blob in ProgramData for this purpose.
                    // For now, we'll compile a tiny passthrough VS to get the
                    // signature.
                    //
                    // Actually, the D3D11 runtime allows CreateInputLayout with
                    // just the element descs if you use a compatible shader.
                    // Since we don't have the original VS blob, we rely on the
                    // program's stored input layout (created at compile time).

                    // If the program has an input layout, use it
                    auto progIt2 = programs.find(currentProgram);
                    if (progIt2 != programs.end() && progIt2->second.inputLayout)
                    {
                        va.inputLayout = progIt2->second.inputLayout;
                        va.inputLayout->AddRef();
                    }
                }
            }
        }

        // Bind VA resources
        if (va.indexBuffer)
        {
            d3dContext->IASetIndexBuffer(va.indexBuffer, va.indexFormat, 0);
        }

        if (!va.vertexBuffers.empty())
        {
            std::vector<ID3D11Buffer *> vbs;
            std::vector<UINT> strides;
            std::vector<UINT> offsets;
            for (const auto &vb : va.vertexBuffers)
            {
                vbs.push_back(vb.buffer);
                strides.push_back(vb.stride);
                offsets.push_back(vb.offset);
            }
            d3dContext->IASetVertexBuffers(0, (UINT)vbs.size(),
                                           vbs.data(), strides.data(), offsets.data());
        }
        else
        {
            // If no vertex buffers were explicitly bound to the VAO, look up
            // the buffer by ID from the generic buffer map.  The engine binds
            // vertex data via uploadVertexBuffer and then draws via VAO.
            // We need to set vertex buffers here.  Since the VAO doesn't store
            // them (the engine calls setVertexAttribFloat which stores attribs),
            // we rely on the attrib stride to find the buffer.
            //
            // For simplicity, we'll bind all non-index buffers from the buffers
            // map that match the attrib stride. This is a heuristic.
            if (!va.attribs.empty())
            {
                int stride = va.attribs[0].stride;
                for (auto &bufKv : buffers)
                {
                    if (bufKv.second)
                    {
                        D3D11_BUFFER_DESC bufDesc;
                        bufKv.second->GetDesc(&bufDesc);
                        if (bufDesc.BindFlags & D3D11_BIND_VERTEX_BUFFER)
                        {
                            UINT uStride = (UINT)stride;
                            UINT uOffset = 0;
                            d3dContext->IASetVertexBuffers(0, 1, &bufKv.second,
                                                           &uStride, &uOffset);
                            break;
                        }
                    }
                }
            }
        }

        if (va.inputLayout)
            d3dContext->IASetInputLayout(va.inputLayout);

        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Flush constant buffers before drawing
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind current RTV + DSV
        if (currentFBO == 0)
        {
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        }
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
            {
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
            }
        }

        // Bind textures (set shader resource views)
        for (int i = 0; i < 16; ++i)
        {
            if (boundTextures[i].srv)
            {
                d3dContext->PSSetShaderResources(i, 1, &boundTextures[i].srv);
                if (boundTextures[i].sampler)
                    d3dContext->PSSetSamplers(i, 1, &boundTextures[i].sampler);
            }
        }

        if (indexCount <= 0 && va.vertexBuffers.empty())
        {
            // Try using stored index count
            indexCount = (int)va.indexCount;
        }

        if (indexCount > 0)
            d3dContext->DrawIndexed((UINT)indexCount, 0, 0);
    }

    void kDX11Driver::drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        D3D11VertexArrayData &va = vaIt->second;

        if (va.indexBuffer)
            d3dContext->IASetIndexBuffer(va.indexBuffer, va.indexFormat, 0);

        if (va.inputLayout)
            d3dContext->IASetInputLayout(va.inputLayout);

        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        // Bind textures
        for (int i = 0; i < 16; ++i)
        {
            if (boundTextures[i].srv)
            {
                d3dContext->PSSetShaderResources(i, 1, &boundTextures[i].srv);
                if (boundTextures[i].sampler)
                    d3dContext->PSSetSamplers(i, 1, &boundTextures[i].sampler);
            }
        }

        if (indexCount <= 0)
            indexCount = (int)va.indexCount;

        if (indexCount > 0)
            d3dContext->DrawIndexedInstanced((UINT)indexCount, (UINT)instanceCount, 0, 0, 0);
    }

    void kDX11Driver::drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        d3dContext->IASetPrimitiveTopology(toD3DTopology(type));

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        if (vertexCount > 0)
            d3dContext->Draw((UINT)vertexCount, 0);
    }

    void kDX11Driver::drawArraysInstanced(uint32_t vaoId, kPrimitiveType type,
                                           int vertexCount, int instanceCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        d3dContext->IASetPrimitiveTopology(toD3DTopology(type));

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        if (vertexCount > 0)
            d3dContext->DrawInstanced((UINT)vertexCount, (UINT)instanceCount, 0, 0);
    }

    // =========================================================================
    // Texture sampling
    // =========================================================================

    void kDX11Driver::bindTexture2D(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = false;
            boundTextures[unit].isArray = false;
        }
    }

    void kDX11Driver::bindTexture2DArray(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = false;
            boundTextures[unit].isArray = true;
        }
    }

    void kDX11Driver::bindTextureCube(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = true;
            boundTextures[unit].isArray = false;
        }
    }

    void kDX11Driver::unbindTexture2D(int unit)
    {
        if (unit < 0 || unit >= 16) return;
        boundTextures[unit] = BoundTexture();
        ID3D11ShaderResourceView *nullSrv = nullptr;
        d3dContext->PSSetShaderResources(unit, 1, &nullSrv);
    }

    void kDX11Driver::unbindTexture2DArray(int unit)
    {
        unbindTexture2D(unit);
    }

    void kDX11Driver::unbindTextureCube(int unit)
    {
        unbindTexture2D(unit);
    }

    void kDX11Driver::generateMipmaps2D(uint32_t id)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.srv)
            return;

        // D3D11 can auto-generate mipmaps by calling GenerateMips on the SRV
        d3dContext->GenerateMips(it->second.srv);
    }

    void kDX11Driver::readTexture2DRGB(uint32_t id, int mipLevel, float *pixels)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture)
            return;

        // Create a staging texture to read back
        D3D11_TEXTURE2D_DESC desc;
        it->second.texture->GetDesc(&desc);
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;

        ID3D11Texture2D *staging = nullptr;
        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &staging);
        if (FAILED(hr)) return;

        d3dContext->CopySubresourceRegion(staging, 0, 0, 0, 0,
                                          it->second.texture, mipLevel, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            int mipWidth  = std::max(1, (int)desc.Width  >> mipLevel);
            int mipHeight = std::max(1, (int)desc.Height >> mipLevel);
            for (int y = 0; y < mipHeight; ++y)
            {
                for (int x = 0; x < mipWidth; ++x)
                {
                    uint8_t *src = (uint8_t *)mapped.pData + y * mapped.RowPitch + x * 4;
                    float *dst   = pixels + (y * mipWidth + x) * 3;
                    dst[0] = src[0] / 255.0f;
                    dst[1] = src[1] / 255.0f;
                    dst[2] = src[2] / 255.0f;
                }
            }
            d3dContext->Unmap(staging, 0);
        }
        staging->Release();
    }

    void kDX11Driver::readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
    {
        r = g = b = 0; a = 255;

        // D3D11 doesn't have a simple glReadPixels equivalent.
        // We need to copy the current render target to a staging texture.
        ID3D11Texture2D *backBuffer = nullptr;
        if (currentFBO == 0 && backBufferRTV)
        {
            backBufferRTV->GetResource((ID3D11Resource **)&backBuffer);
        }

        if (!backBuffer)
            return;

        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width              = 1;
        stagingDesc.Height             = 1;
        stagingDesc.MipLevels          = 1;
        stagingDesc.ArraySize          = 1;
        stagingDesc.Format             = bbDesc.Format;
        stagingDesc.SampleDesc.Count   = 1;
        stagingDesc.Usage              = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags          = 0;
        stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D *staging = nullptr;
        HRESULT hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &staging);
        if (FAILED(hr)) { backBuffer->Release(); return; }

        D3D11_BOX srcBox;
        srcBox.left   = (UINT)x;
        srcBox.top    = (UINT)y;
        srcBox.right  = (UINT)x + 1;
        srcBox.bottom = (UINT)y + 1;
        srcBox.front  = 0;
        srcBox.back   = 1;

        d3dContext->CopySubresourceRegion(staging, 0, 0, 0, 0, backBuffer, 0, &srcBox);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            uint8_t *pixel = (uint8_t *)mapped.pData;
            r = pixel[0];
            g = pixel[1];
            b = pixel[2];
            a = pixel[3];
            d3dContext->Unmap(staging, 0);
        }

        staging->Release();
        backBuffer->Release();
    }

    // =========================================================================
    // Framebuffers
    // =========================================================================

    uint32_t kDX11Driver::createFramebuffer()
    {
        uint32_t id = nextFBOId++;
        framebuffers[id] = D3D11FramebufferData();
        return id;
    }

    void kDX11Driver::deleteFramebuffer(uint32_t id)
    {
        auto it = framebuffers.find(id);
        if (it != framebuffers.end())
        {
            for (auto *rtv : it->second.colorRTVs) if (rtv) rtv->Release();
            if (it->second.depthDSV) it->second.depthDSV->Release();
            for (auto *tex : it->second.colorTextures) if (tex) tex->Release();
            if (it->second.depthTexture) it->second.depthTexture->Release();
            for (auto *srv : it->second.colorSRVs) if (srv) srv->Release();
            framebuffers.erase(it);
        }
    }

    void kDX11Driver::bindFramebuffer(uint32_t id)
    {
        currentFBO = id;
    }

    void kDX11Driver::bindReadFramebuffer(uint32_t id)
    {
        // D3D11 doesn't have separate read/draw FBOs; just store for blit
        currentFBO = id;
    }

    void kDX11Driver::bindDrawFramebuffer(uint32_t id)
    {
        currentFBO = id;
    }

    void kDX11Driver::unbindFramebuffer()
    {
        currentFBO = 0;
    }

    bool kDX11Driver::isFramebufferComplete()
    {
        if (currentFBO == 0)
            return backBufferRTV != nullptr;

        auto it = framebuffers.find(currentFBO);
        if (it == framebuffers.end())
            return false;

        return !it->second.colorRTVs.empty() || it->second.depthDSV != nullptr;
    }

    void kDX11Driver::blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                            int dstX0, int dstY0, int dstX1, int dstY1)
    {
        // D3D11 doesn't have a direct blit; we'd use a full-screen quad shader.
        // For the MSAA resolve case, ResolveSubresource is the proper approach.
        // This stub is here for API completeness; MSAA resolve is handled internally
        // by the renderer's screen-buffer path via ResolveSubresource.
        (void)srcX0; (void)srcY0; (void)srcX1; (void)srcY1;
        (void)dstX0; (void)dstY0; (void)dstX1; (void)dstY1;
    }

    void kDX11Driver::setFramebufferDrawBuffer()
    {
        // D3D11 automatically draws to all bound RTVs. No-op for D3D11.
    }

    // =========================================================================
    // Renderbuffers
    // =========================================================================

    uint32_t kDX11Driver::createRenderbuffer()
    {
        // In D3D11, renderbuffers are just depth textures or standard textures.
        // We reuse the texture ID space for RBOs.
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.isDepth = true;
        textures[id] = td;
        return id;
    }

    void kDX11Driver::deleteRenderbuffer(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end())
        {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv)     it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    void kDX11Driver::setupRenderbuffer(uint32_t rboId, int width, int height)
    {
        auto it = textures.find(rboId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] setupRenderbuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        td.width   = width;
        td.height  = height;
        td.samples = 1;
        td.isDepth = true;
    }

    void kDX11Driver::setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height)
    {
        auto it = textures.find(rboId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.SampleDesc.Quality = 0;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] setupRenderbufferMSAA failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        td.width   = width;
        td.height  = height;
        td.samples = samples;
        td.isDepth = true;
    }

    void kDX11Driver::attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId)
    {
        auto texIt = textures.find(rboId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        // Release old DSV
        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        HRESULT hr = d3dDevice->CreateDepthStencilView(
            texIt->second.texture, &dsvDesc, &fb.depthDSV);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] attachRenderbufferDepthStencil failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }
    }

    // =========================================================================
    // Texture creation (for asset loading)
    // =========================================================================

    static DXGI_FORMAT toD3DTextureFormat(kTextureFormat format, bool &isSRGB)
    {
        isSRGB = false;
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case kTextureFormat::TEX_FORMAT_RGBA:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case kTextureFormat::TEX_FORMAT_SRGB:
            isSRGB = true;
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case kTextureFormat::TEX_FORMAT_SRGBA:
            isSRGB = true;
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    static D3D11_TEXTURE_ADDRESS_MODE toD3DWrap(kTextureWrap w)
    {
        switch (w)
        {
        case kTextureWrap::REPEAT:          return D3D11_TEXTURE_ADDRESS_WRAP;
        case kTextureWrap::CLAMP_TO_EDGE:   return D3D11_TEXTURE_ADDRESS_CLAMP;
        case kTextureWrap::CLAMP_TO_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
        case kTextureWrap::MIRRORED_REPEAT: return D3D11_TEXTURE_ADDRESS_MIRROR;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    static D3D11_FILTER toD3DFilter(kTextureFilter minF, kTextureFilter magF, bool hasMips)
    {
        if (!hasMips)
        {
            if (minF == kTextureFilter::NEAREST && magF == kTextureFilter::NEAREST)
                return D3D11_FILTER_MIN_MAG_MIP_POINT;
            if (minF == kTextureFilter::LINEAR && magF == kTextureFilter::LINEAR)
                return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            if (minF == kTextureFilter::NEAREST)
                return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
            return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        }
        bool minNear = (minF == kTextureFilter::NEAREST || minF == kTextureFilter::NEAREST_MIPMAP_NEAREST || minF == kTextureFilter::NEAREST_MIPMAP_LINEAR);
        bool magNear = (magF == kTextureFilter::NEAREST);
        bool mipNear = (minF == kTextureFilter::NEAREST_MIPMAP_NEAREST || minF == kTextureFilter::LINEAR_MIPMAP_NEAREST);
        if (minNear && magNear && mipNear)  return D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (minNear && magNear && !mipNear) return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        if (minNear && !magNear && mipNear) return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if (!minNear && magNear && mipNear) return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        if (!minNear && !magNear && !mipNear) return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }

    uint32_t kDX11Driver::createTexture2D(int width, int height, kTextureFormat format,
                                           const void *data,
                                           kTextureWrap wrap,
                                           kTextureFilter minFilter,
                                           kTextureFilter magFilter,
                                           bool generateMips)
    {
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.width  = width;
        td.height = height;
        td.layers = 1;
        bool isSRGB = false;
        td.format = toD3DTextureFormat(format, isSRGB);
        UINT mipLevels = generateMips ? 0 : 1;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)width;
        desc.Height = (UINT)height;
        desc.MipLevels = mipLevels;
        desc.ArraySize = 1;
        desc.Format = td.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        if (data)
        {
            int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
            std::vector<uint8_t> rgbaData;
            const void *uploadData = data;
            if (channels == 3)
            {
                rgbaData.resize(width * height * 4);
                const uint8_t *src = (const uint8_t *)data;
                uint8_t *dst = rgbaData.data();
                for (int i = 0; i < width * height; ++i) {
                    dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                    src+=3; dst+=4;
                }
                uploadData = rgbaData.data();
            }
            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = uploadData;
            initData.SysMemPitch = (UINT)width * 4;
            d3dDevice->CreateTexture2D(&desc, &initData, &td.texture);
        }
        else
        {
            d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        }
        if (td.texture)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = td.format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = mipLevels;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = toD3DFilter(minFilter, magFilter, generateMips);
            sampDesc.AddressU = toD3DWrap(wrap);
            sampDesc.AddressV = toD3DWrap(wrap);
            sampDesc.AddressW = toD3DWrap(wrap);
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD = 0;
            sampDesc.MaxLOD = generateMips ? D3D11_FLOAT32_MAX : 0;
            d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);
            if (generateMips && td.srv)
                d3dContext->GenerateMips(td.srv);
        }
        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createTextureCube(int width, int height,
                                             const void *faceData[6],
                                             bool generateMips)
    {
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.width = width; td.height = height; td.layers = 6;
        td.isCube = true;
        td.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        UINT mipLevels = generateMips ? 0 : 1;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)width; desc.Height = (UINT)height;
        desc.MipLevels = mipLevels; desc.ArraySize = 6;
        desc.Format = td.format; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | (generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);
        D3D11_SUBRESOURCE_DATA initData[6] = {};
        for (int i = 0; i < 6; ++i) {
            if (faceData[i]) {
                initData[i].pSysMem = faceData[i];
                initData[i].SysMemPitch = (UINT)width * 4;
            }
        }
        d3dDevice->CreateTexture2D(&desc, initData, &td.texture);
        if (td.texture)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = td.format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = mipLevels;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = generateMips ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR;
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD = 0;
            sampDesc.MaxLOD = generateMips ? D3D11_FLOAT32_MAX : 0;
            d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);
            if (generateMips && td.srv) d3dContext->GenerateMips(td.srv);
        }
        textures[id] = td;
        return id;
    }

    void kDX11Driver::uploadTexture2D(uint32_t id, int level, int width, int height,
                                       kTextureFormat format, const void *data)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
        std::vector<uint8_t> rgbaData;
        const void *uploadData = data;
        if (channels == 3 && data) {
            rgbaData.resize(width * height * 4);
            const uint8_t *src = (const uint8_t *)data;
            uint8_t *dst = rgbaData.data();
            for (int i = 0; i < width * height; ++i) {
                dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                src+=3; dst+=4;
            }
            uploadData = rgbaData.data();
        }
        D3D11_BOX box;
        box.left=0; box.top=0; box.right=(UINT)width; box.bottom=(UINT)height;
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, level, &box, uploadData, (UINT)width*4, 0);
    }

    void kDX11Driver::uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                          int width, int height,
                                          kTextureFormat format, const void *data)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
        std::vector<uint8_t> rgbaData;
        const void *uploadData = data;
        if (channels == 3 && data) {
            rgbaData.resize(width * height * 4);
            const uint8_t *src = (const uint8_t *)data;
            uint8_t *dst = rgbaData.data();
            for (int i = 0; i < width * height; ++i) {
                dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                src+=3; dst+=4;
            }
            uploadData = rgbaData.data();
        }
        D3D11_BOX box;
        box.left=(UINT)x; box.top=(UINT)y; box.right=(UINT)(x+width); box.bottom=(UINT)(y+height);
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, level, &box, uploadData, (UINT)width*4, 0);
    }

    void kDX11Driver::uploadCompressedTexture2D(uint32_t id, int level,
                                                 int width, int height,
                                                 kTextureFormat format,
                                                 const void *data, size_t dataSize)
    {
        (void)width; (void)height; (void)format;
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        d3dContext->UpdateSubresource(it->second.texture, level, nullptr, data, (UINT)dataSize, 0);
    }

    void kDX11Driver::uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                             const void *data)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        UINT subresource = D3D11CalcSubresource(0, (UINT)face, 1);
        D3D11_BOX box;
        box.left=0; box.top=0; box.right=(UINT)width; box.bottom=(UINT)height;
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, subresource, &box, data, (UINT)width*4, 0);
    }

    void kDX11Driver::deleteTexture(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end()) {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv) it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    // =========================================================================
    // FBO-managed textures
    // =========================================================================

    uint32_t kDX11Driver::createFBOColorTexture(int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width  = width;
        td.height = height;
        td.format = srgbEnabled ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags          = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }

        // Default sampler
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MaxAnisotropy  = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD         = 0;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBOColorTextureMSAA(int samples, int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.samples = samples;
        td.format  = srgbEnabled ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBODepthTexture(int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.isDepth = true;
        td.format  = DXGI_FORMAT_R32_TYPELESS;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format               = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension        = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels  = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
        }

        // Depth comparison sampler (for shadow mapping)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBODepthTextureArray(int width, int height, int layers)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.layers  = layers;
        td.isDepth = true;
        td.format  = DXGI_FORMAT_R32_TYPELESS;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = (UINT)layers;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                  = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension           = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MipLevels = 1;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize       = (UINT)layers;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    void kDX11Driver::deleteFBOTexture(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end())
        {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv)     it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    void kDX11Driver::attachFBOColorTexture(uint32_t fboId, uint32_t texId)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        // Release old RTV
        for (auto *rtv : fb.colorRTVs) if (rtv) rtv->Release();
        fb.colorRTVs.clear();

        ID3D11RenderTargetView *rtv = nullptr;
        HRESULT hr = d3dDevice->CreateRenderTargetView(texIt->second.texture, nullptr, &rtv);
        if (SUCCEEDED(hr))
        {
            fb.colorRTVs.push_back(rtv);
            fb.colorTextures.push_back(texIt->second.texture);
            texIt->second.texture->AddRef();
            if (texIt->second.srv)
            {
                texIt->second.srv->AddRef();
                fb.colorSRVs.push_back(texIt->second.srv);
            }
        }
        else
        {
            std::cout << "[kDX11Driver] attachFBOColorTexture failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }
    }

    void kDX11Driver::attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId)
    {
        // Same as non-MSAA for D3D11 — the texture itself is multisampled
        attachFBOColorTexture(fboId, texId);
    }

    void kDX11Driver::attachFBODepthTexture(uint32_t fboId, uint32_t texId)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        d3dDevice->CreateDepthStencilView(texIt->second.texture, &dsvDesc, &fb.depthDSV);

        if (fb.depthTexture) fb.depthTexture->Release();
        fb.depthTexture = texIt->second.texture;
        fb.depthTexture->AddRef();
    }

    void kDX11Driver::attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                 = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice       = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = (UINT)layer;
        dsvDesc.Texture2DArray.ArraySize       = 1;

        d3dDevice->CreateDepthStencilView(texIt->second.texture, &dsvDesc, &fb.depthDSV);

        if (fb.depthTexture) fb.depthTexture->Release();
        fb.depthTexture = texIt->second.texture;
        fb.depthTexture->AddRef();
    }

    void kDX11Driver::resizeFBOColorTexture(uint32_t texId, int width, int height)
    {
        auto it = textures.find(texId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        // Release old
        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        td.width  = width;
        td.height = height;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags          = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }

        // Re-attach to any FBOs that reference this texture
        for (auto &fboKv : framebuffers)
        {
            for (size_t i = 0; i < fboKv.second.colorTextures.size(); ++i)
            {
                // We don't track which texture ID an FBO uses, so this is
                // best-effort. The renderer will re-attach after resize.
            }
        }
    }

    void kDX11Driver::resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height)
    {
        auto it = textures.find(texId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        td.width   = width;
        td.height  = height;
        td.samples = samples;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }
    }

    // =========================================================================
    // Back-buffer helpers
    // =========================================================================

    bool kDX11Driver::createBackBufferResources()
    {
        releaseBackBufferResources();

        ID3D11Texture2D *backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] GetBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        hr = d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &backBufferRTV);
        backBuffer->Release();
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateRenderTargetView for back buffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        // Create a depth-stencil buffer for the back buffer
        D3D11_TEXTURE2D_DESC dsDesc = {};
        backBuffer->GetDesc(&dsDesc); // reuse dimensions
        // Actually we need to query the original desc from the swap chain
        DXGI_SWAP_CHAIN_DESC scDesc;
        swapChain->GetDesc(&scDesc);

        dsDesc.Width              = scDesc.BufferDesc.Width;
        dsDesc.Height             = scDesc.BufferDesc.Height;
        dsDesc.MipLevels          = 1;
        dsDesc.ArraySize          = 1;
        dsDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsDesc.SampleDesc.Count   = 1;
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Usage              = D3D11_USAGE_DEFAULT;
        dsDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        ID3D11Texture2D *dsTexture = nullptr;
        hr = d3dDevice->CreateTexture2D(&dsDesc, nullptr, &dsTexture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateDepthStencilView(dsTexture, nullptr, &backBufferDSV);
            dsTexture->Release();
        }

        return true;
    }

    void kDX11Driver::releaseBackBufferResources()
    {
        if (backBufferRTV) { backBufferRTV->Release(); backBufferRTV = nullptr; }
        if (backBufferDSV) { backBufferDSV->Release(); backBufferDSV = nullptr; }
    }

    // =========================================================================
    // Private helpers — conversions
    // =========================================================================

    D3D11_BLEND kDX11Driver::toD3DBlend(kBlendFactor factor)
    {
        switch (factor)
        {
        case kBlendFactor::ZERO:                return D3D11_BLEND_ZERO;
        case kBlendFactor::ONE:                 return D3D11_BLEND_ONE;
        case kBlendFactor::SRC_ALPHA:           return D3D11_BLEND_SRC_ALPHA;
        case kBlendFactor::ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
        case kBlendFactor::SRC_COLOR:           return D3D11_BLEND_SRC_COLOR;
        case kBlendFactor::ONE_MINUS_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
        case kBlendFactor::DST_ALPHA:           return D3D11_BLEND_DEST_ALPHA;
        case kBlendFactor::ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
        default:                                return D3D11_BLEND_ONE;
        }
    }

    D3D11_CULL_MODE kDX11Driver::toD3DCullMode(kCullMode mode)
    {
        switch (mode)
        {
        case kCullMode::BACK:          return D3D11_CULL_BACK;
        case kCullMode::FRONT:         return D3D11_CULL_FRONT;
        case kCullMode::FRONT_AND_BACK: return D3D11_CULL_NONE; // ? Actually cull all
        default:                       return D3D11_CULL_BACK;
        }
    }

    D3D11_PRIMITIVE_TOPOLOGY kDX11Driver::toD3DTopology(kPrimitiveType type)
    {
        switch (type)
        {
        case kPrimitiveType::TRIANGLES:      return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case kPrimitiveType::TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case kPrimitiveType::TRIANGLE_FAN:   return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // No fan in D3D11
        case kPrimitiveType::LINES:          return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case kPrimitiveType::LINE_STRIP:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case kPrimitiveType::POINTS:         return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                             return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    D3D11_PRIMITIVE_TOPOLOGY kDX11Driver::toD3DTopologyTriangles()
    {
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

} // namespace kemena

#endif // KEMENA_D3D11
