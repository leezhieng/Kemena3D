// =============================================================================
// offscreen_shadow.hlsl — DirectX 11 counterpart of offscreen_shadow.glsl
// =============================================================================
//
// Skinned depth-only pass used to render the shadow maps.  Compiled as HLSL
// vs_5_0 / ps_5_0 with the entry points VSMain / PSMain.
//
// Original GLSL inputs, kept here as a reference for the editor / authoring tools
// (the HLSL equivalents below carry the same names and the same semantics):
//   layout(location = 0) in vec3  vertexPosition;
//   layout(location = 6) in ivec4 boneIDs;
//   layout(location = 7) in vec4  weights;
//   uniform mat4 lightSpaceMatrix;
//   uniform mat4 modelMatrix;
//   uniform mat4 finalBoneMatrices[MAX_BONES];
//
// Attribute semantics follow kDX11Driver::attribSemantic():
//   location 0 = POSITION, 6 = BLENDINDICES, 7 = BLENDWEIGHT
// =============================================================================

static const int MAX_BONES          = 128;
static const int MAX_BONE_INFLUENCE = 4;

cbuffer PerObject : register(b0)
{
    float4x4 lightSpaceMatrix;
    float4x4 modelMatrix;
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
    o.position = mul(lightSpaceMatrix, mul(modelMatrix, totalPosition));
    return o;
}

// =============================================================================
// PIXEL SHADER — depth only, no colour output
// =============================================================================

void PSMain(VSOutput input)
{
}
