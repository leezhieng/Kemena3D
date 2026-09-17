// =============================================================================
// offscreen_preview.hlsl — DirectX 11 counterpart of offscreen_preview.glsl
// =============================================================================
//
// Skinned "matcap-ish normal preview" used by the offscreen renderer for model
// thumbnails.  Compiled as HLSL vs_5_0 / ps_5_0 with the entry points
// VSMain / PSMain.
//
// Original GLSL inputs, kept here as a reference for the editor / authoring tools
// (the HLSL equivalents below carry the same names and the same semantics):
//   layout(location = 0) in vec3  aPosition;
//   layout(location = 3) in vec3  aNormal;
//   layout(location = 5) in vec3  aBitangent;
//   layout(location = 6) in ivec4 boneIDs;
//   layout(location = 7) in vec4  weights;
//   uniform mat4 modelMatrix;
//   uniform mat4 viewMatrix;
//   uniform mat4 projectionMatrix;
//   uniform vec3 viewPos;
//   uniform mat4 finalBoneMatrices[MAX_BONES];
//
// Attribute semantics follow kDX11Driver::attribSemantic():
//   location 0 = POSITION, 3 = NORMAL, 5 = BINORMAL,
//   6 = BLENDINDICES, 7 = BLENDWEIGHT
// =============================================================================

static const int MAX_BONES          = 128;
static const int MAX_BONE_INFLUENCE = 4;

cbuffer PerObject : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3   viewPos;
    float    _objPad0;
    float4x4 finalBoneMatrices[MAX_BONES];
};

struct VSInput
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float3 bitangent : BINORMAL;
    int4   boneIDs   : BLENDINDICES;
    float4 weights   : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 vNormal   : TEXCOORD0;
    float3 vBitangent: TEXCOORD1;
    float3 vFragPos  : TEXCOORD2;
};

float3x3 inverse3x3(float3x3 m)
{
    float det = m[0][0] * (m[1][1]*m[2][2] - m[1][2]*m[2][1])
              - m[0][1] * (m[1][0]*m[2][2] - m[1][2]*m[2][0])
              + m[0][2] * (m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    float inv = 1.0 / det;
    float3x3 r;
    r[0][0] =  (m[1][1]*m[2][2] - m[1][2]*m[2][1]) * inv;
    r[0][1] = -(m[0][1]*m[2][2] - m[0][2]*m[2][1]) * inv;
    r[0][2] =  (m[0][1]*m[1][2] - m[0][2]*m[1][1]) * inv;
    r[1][0] = -(m[1][0]*m[2][2] - m[1][2]*m[2][0]) * inv;
    r[1][1] =  (m[0][0]*m[2][2] - m[0][2]*m[2][0]) * inv;
    r[1][2] = -(m[0][0]*m[1][2] - m[0][2]*m[1][0]) * inv;
    r[2][0] =  (m[1][0]*m[2][1] - m[1][1]*m[2][0]) * inv;
    r[2][1] = -(m[0][0]*m[2][1] - m[0][1]*m[2][0]) * inv;
    r[2][2] =  (m[0][0]*m[1][1] - m[0][1]*m[1][0]) * inv;
    return r;
}

VSOutput VSMain(VSInput input)
{
    float4 totalPosition  = float4(0.0, 0.0, 0.0, 0.0);
    float3 totalNormal    = float3(0.0, 0.0, 0.0);
    float3 totalBitangent = float3(0.0, 0.0, 0.0);
    float  totalWeight    = 0.0;

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
        totalPosition  += mul(finalBoneMatrices[boneID], float4(input.position, 1.0)) * weight;
        float3x3 nm     = transpose(inverse3x3((float3x3)finalBoneMatrices[boneID]));
        totalNormal    += mul(nm, input.normal)    * weight;
        totalBitangent += mul(nm, input.bitangent) * weight;
        totalWeight    += weight;
    }

    if (totalWeight == 0.0)
    {
        totalPosition  = float4(input.position, 1.0);
        totalNormal    = input.normal;
        totalBitangent = input.bitangent;
    }

    float3x3 normalMatrix = transpose(inverse3x3((float3x3)modelMatrix));
    float4   worldPos     = mul(modelMatrix, totalPosition);

    VSOutput o;
    o.vFragPos   = worldPos.xyz;
    o.vNormal    = normalize(mul(normalMatrix, totalNormal));
    o.vBitangent = normalize(mul(normalMatrix, totalBitangent));
    o.position   = mul(projectionMatrix, mul(viewMatrix, worldPos));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

float4 PSMain(VSOutput input) : SV_Target
{
    float3 n      = normalize(input.vNormal);
    float3 b      = normalize(input.vBitangent);
    float3 V      = normalize(viewPos - input.vFragPos);
    float  dN     = dot(n, V) * 0.5 + 0.5;
    float  dB     = dot(b, V) * 0.5 + 0.5;
    float  top    = n.y * 0.5 + 0.5;
    float  shade  = dN * 0.50 + dB * 0.25 + top * 0.25;
    float3 albedo = float3(0.75, 0.75, 0.75);

    return float4(albedo * (0.10 + shade * 0.90), 1.0);
}
