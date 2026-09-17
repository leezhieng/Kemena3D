// =============================================================================
// offscreen_white.hlsl — DirectX 11 counterpart of offscreen_white.glsl
// =============================================================================
//
// Skinned "flat white" pass used by the offscreen renderer (thumbnails / preview
// passes).  Compiled as HLSL vs_5_0 / ps_5_0 with the entry points VSMain / PSMain.
//
// The engine does not translate shader source: this file is the D3D11 variant of
// offscreen_white.glsl and is selected at load time when the active renderer is
// RENDERER_D3D11 (see kAssetManager::loadGlslFromResource()).
//
// Original GLSL inputs, kept here as a reference for the editor / authoring tools
// (the HLSL equivalents below carry the same names and the same semantics):
//   layout(location = 0) in vec3  aPosition;
//   layout(location = 6) in ivec4 boneIDs;
//   layout(location = 7) in vec4  weights;
//   uniform mat4 modelMatrix;
//   uniform mat4 viewMatrix;
//   uniform mat4 projectionMatrix;
//   uniform mat4 finalBoneMatrices[MAX_BONES];
//
// Attribute semantics follow kDX11Driver::attribSemantic():
//   location 0 = POSITION, 6 = BLENDINDICES, 7 = BLENDWEIGHT
//
// NOTE: Assumes row-major matrices (standard HLSL), matching the engine's
//       D3DCOMPILE_PACK_MATRIX_ROW_MAJOR + transposed upload.
// =============================================================================

static const int MAX_BONES          = 128;
static const int MAX_BONE_INFLUENCE = 4;

cbuffer PerObject : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 finalBoneMatrices[MAX_BONES];
};

struct VSInput
{
    float3 position : POSITION;
    int4   boneIDs  : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(VSInput input)
{
    float4 totalPosition = float4(0.0, 0.0, 0.0, 0.0);
    float  totalWeight   = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = input.boneIDs[i];
        float weight = input.weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPosition = float4(0.0, 0.0, 0.0, 0.0);
            break;
        }
        totalPosition += mul(finalBoneMatrices[boneID], float4(input.position, 1.0)) * weight;
        totalWeight   += weight;
    }

    if (totalWeight == 0.0)
        totalPosition = float4(input.position, 1.0);

    VSOutput o;
    o.position = mul(projectionMatrix, mul(viewMatrix, mul(modelMatrix, totalPosition)));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
