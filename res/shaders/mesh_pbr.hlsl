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
    float4x4 normalMatrix;
    float4x4 lightSpaceMatrix;
    float4x4 finalBonesMatrices[MAX_BONES];
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
};

struct VSOutput
{
    float4 position      : SV_Position;
    float3 worldPos      : TEXCOORD0;
    float3 color         : TEXCOORD1;
    float2 texCoord      : TEXCOORD2;
    float4 lightSpacePos : TEXCOORD3;
    float3 T             : TEXCOORD4;
    float3 B             : TEXCOORD5;
    float3 N             : TEXCOORD6;
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
    float4 totalPos       = float4(0.0, 0.0, 0.0, 0.0);
    float3 totalNormal    = float3(0, 0, 0);
    float3 totalTangent   = float3(0, 0, 0);
    float3 totalBitangent = float3(0, 0, 0);
    float  totalWeight    = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = input.boneIDs[i];
        float weight = input.weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPos       = float4(0.0, 0.0, 0.0, 0.0);
            totalNormal    = float3(0, 0, 0);
            totalTangent   = float3(0, 0, 0);
            totalBitangent = float3(0, 0, 0);
            break;
        }
        totalPos       += mul(finalBonesMatrices[boneID], float4(input.position, 1.0)) * weight;
        float3x3 nm     = transpose(inverse3x3((float3x3)finalBonesMatrices[boneID]));
        totalNormal    += mul(nm, input.normal)    * weight;
        totalTangent   += mul(nm, input.tangent)   * weight;
        totalBitangent += mul(nm, input.bitangent) * weight;
        totalWeight    += weight;
    }
    if (totalWeight == 0.0)
    {
        totalPos       = float4(input.position, 1.0);
        totalNormal    = input.normal;
        totalTangent   = input.tangent;
        totalBitangent = input.bitangent;
    }

    bool   anim = totalWeight > 0.0;
    float3 useN = anim ? totalNormal    : input.normal;
    float3 useT = anim ? totalTangent   : input.tangent;
    float3 useB = anim ? totalBitangent : input.bitangent;

    float3x3 nm3      = (float3x3)normalMatrix;
    float3   worldPos = mul(modelMatrix, totalPos).xyz;

    VSOutput o;
    o.worldPos      = worldPos;
    o.color         = input.color;
    o.texCoord      = input.texCoord;
    o.lightSpacePos = mul(lightSpaceMatrix, float4(worldPos, 1.0));
    o.T             = mul(nm3, useT);
    o.B             = mul(nm3, useB);
    o.N             = mul(nm3, useN);
    o.position      = mul(projectionMatrix, mul(viewMatrix, float4(worldPos, 1.0)));
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

struct SunLight
{
    float  power;
    float3 direction;
    float3 diffuse;
    float  _pad0;
    float3 specular;
    float  _pad1;
};

struct PointLight
{
    float  power;
    float3 position;
    float  constant;
    float  linear;
    float  quadratic;
    float  _pad0;
    float3 diffuse;
    float  _pad1;
    float3 specular;
    float  _pad2;
};

struct SpotLight
{
    float  power;
    float3 position;
    float3 direction;
    float  cutOff;
    float  outerCutOff;
    float  constant;
    float  linear;
    float  quadratic;
    float3 diffuse;
    float  _pad0;
    float3 specular;
    float  _pad1;
};

cbuffer PerMaterial : register(b1)
{
    Material material;
    uint     has_albedoMap;
    uint     has_normalMap;
    uint     has_metallicRoughnessMap;
    uint     has_aoMap;
    uint     has_emissiveMap;
    float3   _matFlagPad;
};

cbuffer PerScene : register(b2)
{
    float3 viewPos;
    float  _scenePad0;
    float3 sceneAmbient;
    float  _scenePad1;
    uint   skyboxAmbientEnabled;
    float  skyboxAmbientStrength;
    float2 _scenePad2;
    int    sunLightNum;
    int    pointLightNum;
    int    spotLightNum;
    int    _scenePad3;
    SunLight   sunLights[32];
    PointLight pointLights[32];
    SpotLight  spotLights[32];
};

Texture2D    albedoMap            : register(t0);
Texture2D    normalMap            : register(t1);
Texture2D    metallicRoughnessMap : register(t2);
Texture2D    aoMap                : register(t3);
Texture2D    emissiveMap          : register(t4);
TextureCube  skyboxMap            : register(t5);
SamplerState defaultSampler       : register(s0);

static const float PI = 3.14159265359;

float distGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geoSchlick(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / (ndotv * (1.0 - k) + k);
}

float geoSmith(float NdotV, float NdotL, float roughness)
{
    return geoSchlick(NdotV, roughness) * geoSchlick(NdotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with a roughness-aware ceiling. Keeps rough dielectrics from getting
// a mirror-bright grazing response, matching the split-sum IBL model.
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((float3)(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, "Real Shading in
// Unreal Engine 4"). Stands in for a precomputed BRDF LUT so the skybox can be
// integrated with the material's Fresnel response without an extra texture.
float3 envBRDFApprox(float3 specularColor, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572,  0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.04,  -0.04);
    float4 r    = roughness * c0 + c1;
    float  a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB   = float2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

float3 calcPBR(float3 albedo, float metallic, float roughness, float3 F0,
               float3 n, float3 v, float3 l, float3 radiance)
{
    float3 h     = normalize(v + l);
    float  NdotH = max(dot(n, h), 0.0);
    float  NdotV = max(dot(n, v), 0.0);
    float  NdotL = max(dot(n, l), 0.0);
    float  NDF   = distGGX(NdotH, roughness);
    float  G     = geoSmith(NdotV, NdotL, roughness);
    float3 F     = fresnelSchlick(max(dot(h, v), 0.0), F0);
    float3 kD    = (1.0 - F) * (1.0 - metallic);
    float3 spec  = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 uv = input.texCoord * material.tiling;

    float4 albedoSample = has_albedoMap ? albedoMap.Sample(defaultSampler, uv) : float4(1, 1, 1, 1);
    float3 albedo       = material.diffuse * albedoSample.rgb;
    float  metallic     = material.metallic;
    float  roughness    = material.roughness;

    if (has_metallicRoughnessMap)
    {
        float2 mr  = metallicRoughnessMap.Sample(defaultSampler, uv).bg;
        metallic  *= mr.x;
        roughness *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    float3 Nn   = normalize(input.N);
    float3 norm = Nn;
    // Screen-space derivative tangent frame — independent of the mesh's vertex
    // tangents (often missing/degenerate on imported sub-meshes); works on any
    // sub-mesh with UVs.
    if (has_normalMap)
    {
        float3 mapN = normalMap.Sample(defaultSampler, uv).rgb;
        mapN.g = 1.0 - mapN.g;
        mapN   = mapN * 2.0 - 1.0;

        float3 dp1  = ddx(input.worldPos);
        float3 dp2  = ddy(input.worldPos);
        float2 duv1 = ddx(uv);
        float2 duv2 = ddy(uv);
        float3 dp2perp = cross(dp2, Nn);
        float3 dp1perp = cross(Nn, dp1);
        float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
        float invmax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-8));
        norm = normalize(mul(mapN, float3x3(T * invmax, B * invmax, Nn)));
    }

    float3 F0     = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 v      = normalize(viewPos - input.worldPos);
    float3 result = float3(0, 0, 0);

    for (int i = 0; i < sunLightNum; i++)
    {
        float3 l        = normalize(-sunLights[i].direction);
        float3 radiance = sunLights[i].diffuse * sunLights[i].power;
        result += calcPBR(albedo, metallic, roughness, F0, norm, v, l, radiance);
    }
    for (int i = 0; i < pointLightNum; i++)
    {
        float3 l    = normalize(pointLights[i].position - input.worldPos);
        float  dist = length(pointLights[i].position - input.worldPos);
        float  att  = pointLights[i].power /
                      (pointLights[i].constant + pointLights[i].linear * dist
                       + pointLights[i].quadratic * dist * dist);
        result += calcPBR(albedo, metallic, roughness, F0, norm, v, l, pointLights[i].diffuse * att);
    }
    for (int i = 0; i < spotLightNum; i++)
    {
        float3 l      = normalize(spotLights[i].position - input.worldPos);
        float  theta  = dot(l, normalize(-spotLights[i].direction));
        float  eps    = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float  intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float  dist   = length(spotLights[i].position - input.worldPos);
        float  att    = spotLights[i].power * intens /
                        (spotLights[i].constant + spotLights[i].linear * dist
                         + spotLights[i].quadratic * dist * dist);
        result += calcPBR(albedo, metallic, roughness, F0, norm, v, l, spotLights[i].diffuse * att);
    }

    float  ao      = has_aoMap      ? aoMap.Sample(defaultSampler, uv).r        : 1.0;
    float4 emissive = has_emissiveMap ? emissiveMap.Sample(defaultSampler, uv)   : float4(0, 0, 0, 0);

    float3 ambient = sceneAmbient * material.ambient * albedo;
    if (skyboxAmbientEnabled)
    {
        // Physically-based skybox ambient (split-sum IBL approximation).
        //   Diffuse  — irradiance from a heavily blurred sample along N,
        //              energy-weighted by kD so it scales with (1 - metallic)
        //              and the albedo, exactly like direct lighting.
        //   Specular — sampled along the reflection vector at a mip level
        //              driven by roughness; the cubemap's mip chain acts as a
        //              cheap pre-filtered environment map.
        // The material's metallic/roughness/normal-mapped normal therefore
        // shape the result instead of the sky simply being overlaid.
        float  NdotV       = max(dot(norm, v), 0.0);
        float3 R           = reflect(-v, norm);
        float3 irradiance  = skyboxMap.SampleLevel(defaultSampler, norm, 8.0).rgb;
        float3 prefiltered = skyboxMap.SampleLevel(defaultSampler, R, roughness * 6.0).rgb;

        float3 F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD    = (1.0 - F_amb) * (1.0 - metallic);

        ambient += (kD * albedo * irradiance +
                    prefiltered * envBRDFApprox(F0, roughness, NdotV)) *
                   skyboxAmbientStrength * material.ambient;
    }

    result = ambient * ao + result + emissive.rgb;
    return float4(result, albedoSample.a);
}
