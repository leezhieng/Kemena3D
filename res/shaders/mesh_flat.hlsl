// NOTE: Assumes row-major matrices (standard HLSL). If uploading OpenGL column-major
//       matrices, either transpose before upload or declare float4x4 as column_major.

// =============================================================================
// VERTEX SHADER
// =============================================================================

static const int MAX_BONES          = 128;
static const int MAX_BONE_INFLUENCE = 4;

cbuffer PerObject : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 finalBonesMatrices[MAX_BONES];
    int      useInstancing; // 0 = modelMatrix, 1 = per-instance matrix stream
};

struct VSInput
{
    float3 position  : POSITION;
    float3 color     : COLOR;
    float2 texCoord  : TEXCOORD0;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
    int4   boneIDs   : BLENDINDICES;
    float4 weights   : BLENDWEIGHT;
    float4 instanceRow0 : TEXCOORD14;
    float4 instanceRow1 : TEXCOORD15;
    float4 instanceRow2 : TEXCOORD16;
    float4 instanceRow3 : TEXCOORD17;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    float4 totalPos    = float4(0.0, 0.0, 0.0, 0.0);
    float  totalWeight = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = input.boneIDs[i];
        float weight = input.weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES) { totalPos = float4(0.0, 0.0, 0.0, 0.0); break; }
        totalPos    += mul(finalBonesMatrices[boneID], float4(input.position, 1.0)) * weight;
        totalWeight += weight;
    }
    if (totalWeight == 0.0) totalPos = float4(input.position, 1.0);

    float4x4 model = modelMatrix;
    if (useInstancing != 0)
    {
        model = float4x4(input.instanceRow0, input.instanceRow1,
                         input.instanceRow2, input.instanceRow3);
    }

    VSOutput o;
    o.texCoord = input.texCoord;
    o.position = mul(projectionMatrix, mul(viewMatrix, mul(model, totalPos)));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

struct Material
{
    float2 tiling;
    float2 _pad0;
    float3 ambient;
    float  _pad1;
    float3 diffuse;
    float  _pad2;
    float3 specular;
    float  shininess;
    float  metallic;
    float  roughness;
    float2 _pad3;
};

cbuffer PerMaterial : register(b1)
{
    Material material;
    uint     has_albedoMap;
    float3   _matPad;
};

Texture2D    albedoMap      : register(t0);
SamplerState defaultSampler : register(s0);

float4 PSMain(VSOutput input) : SV_Target
{
    float4 albedo = has_albedoMap
        ? albedoMap.Sample(defaultSampler, input.texCoord * material.tiling)
        : float4(1, 1, 1, 1);
    return float4(material.diffuse, 1.0) * albedo;
}
