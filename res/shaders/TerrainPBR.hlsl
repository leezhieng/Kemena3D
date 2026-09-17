// =============================================================================
// TerrainPBR.hlsl — DirectX 11 counterpart of TerrainPBR.glsl
// =============================================================================
//
// PBR terrain shader with 4-channel texture splatting (GGX + Smith + Schlick
// Fresnel, CSM shadows, optional skybox ambient).  Compiled as HLSL vs_5_0 /
// ps_5_0 with the entry points VSMain / PSMain.
//
// The engine does not translate shader source: this is the D3D11 variant of
// TerrainPBR.glsl, selected at load time when the active renderer is
// RENDERER_D3D11 (see kAssetManager::loadGlslFromResource()).
//
// -----------------------------------------------------------------------------
// Material parameters (identical to the GLSL @var block — the studio's material
// inspector parses these comments, so keep them in sync with TerrainPBR.glsl)
// -----------------------------------------------------------------------------
// @var sampler2D u_SplatMap       Splat Map
// @var sampler2D u_AlbedoMap[0]   Albedo Layer 0
// @var sampler2D u_AlbedoMap[1]   Albedo Layer 1
// @var sampler2D u_AlbedoMap[2]   Albedo Layer 2
// @var sampler2D u_AlbedoMap[3]   Albedo Layer 3
// @var float     u_BlendSharpness Blend Sharpness
// @var float     u_Tiling[0]      Tiling Layer 0
// @var float     u_Tiling[1]      Tiling Layer 1
// @var float     u_Tiling[2]      Tiling Layer 2
// @var float     u_Tiling[3]      Tiling Layer 3
//
// -----------------------------------------------------------------------------
// Original GLSL inputs, kept as a reference for the editor / authoring tools
// -----------------------------------------------------------------------------
// --- Vertex ---
//   layout(location = 0) in vec3 vertexPosition;
//   layout(location = 1) in vec3 vertexColor;
//   layout(location = 2) in vec2 texCoord;
//   layout(location = 3) in vec3 vertexNormal;
//   uniform mat4 modelMatrix;
//   uniform mat4 viewMatrix;
//   uniform mat4 projectionMatrix;
//   uniform mat4 normalMatrix;
//   out vec3 v_worldPos; out vec3 v_color; out vec2 v_texCoord;
// --- Fragment ---
//   uniform Material material;                 (see struct Material below)
//   uniform samplerCube skyboxMap;
//   uniform bool  skyboxAmbientEnabled;  uniform float skyboxAmbientStrength;
//   uniform vec3  sceneAmbient;
//   uniform int   sunLightNum;    uniform SunLight   sunLights[32];
//   uniform int   pointLightNum;  uniform PointLight pointLights[32];
//   uniform int   spotLightNum;   uniform SpotLight  spotLights[32];
//   uniform mat4  viewMatrix;
//   uniform sampler2DArray shadowMapArray;
//   uniform mat4  lightSpaceMatrices[4]; uniform vec4 cascadeSplits;
//   uniform int   cascadeCount; uniform bool enableShadow; uniform bool receiveShadow;
//   uniform sampler2D u_SplatMap, u_NormalMap[4], u_RoughnessMap[4], u_MetalnessMap[4], u_AOMap[4];
//   uniform float u_Tiling[4], u_BlendSharpness;
//
// Attribute semantics follow kDX11Driver::attribSemantic():
//   location 0 = POSITION, 1 = COLOR, 2 = TEXCOORD, 3 = NORMAL
//
// Texture registers: the D3D11 backend binds material textures to the units the
// renderer assigns, and the shadow array / skybox cube to the engine's fixed
// units 8 / 9 (see kRenderer).  Only the maps the inspector exposes through
// @var are bound there, so:
//   t0      = u_SplatMap        (unit 0)
//   t1..t4  = u_AlbedoMap[0..3] (units 1..4)
//   t8      = shadowMapArray    (unit 8, comparison sampler)
//   t9      = skyboxMap         (unit 9)
// The remaining layer maps (u_NormalMap / u_RoughnessMap / u_MetalnessMap /
// u_AOMap) have no @var entries and therefore no bound unit on either backend.
// The GLSL version samples them from whatever unit a sampler defaults to (unit
// 0 = the splat map); to keep the D3D11 output identical those lookups read the
// splat map as well, and the comment on each use marks it as such.  Add @var
// entries plus free registers to give those layers real textures.
// =============================================================================

// =============================================================================
// VERTEX SHADER
// =============================================================================

#define MAX_CASCADES 4
#define MAX_LIGHTS   32

cbuffer PerObject : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 normalMatrix;
    float4x4 lightSpaceMatrices[MAX_CASCADES];
    float4   cascadeSplits;
    int      cascadeCount;
    int      enableShadow;
    int      receiveShadow;
    int      _objPad0;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color    : COLOR;
    float2 texCoord : TEXCOORD0;
    float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 v_worldPos: TEXCOORD0;
    float3 v_color   : TEXCOORD1;
    float2 v_texCoord: TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
    float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));

    VSOutput o;
    o.v_worldPos = worldPos.xyz;
    o.v_color    = input.color;
    o.v_texCoord = input.texCoord;
    o.position   = mul(projectionMatrix, mul(viewMatrix, worldPos));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

struct Material
{
    float3 diffuse;
    float3 ambient;
    float3 specular;
    float  shininess;
    float  metallic;
    float  roughness;
    float  _pad0;
    float  _pad1;
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
    float    u_Tiling[4];
    float    u_BlendSharpness;
    float3   _matPad0;
    Material material;
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
    SunLight   sunLights[MAX_LIGHTS];
    PointLight pointLights[MAX_LIGHTS];
    SpotLight  spotLights[MAX_LIGHTS];
};

Texture2D            u_SplatMap        : register(t0);
Texture2D            u_AlbedoMap[4]    : register(t1);
Texture2DArray       shadowMapArray    : register(t8);
TextureCube          skyboxMap        : register(t9);
SamplerState         u_Sampler         : register(s0);
SamplerComparisonState shadowSampler   : register(s8);
SamplerState         skyboxSampler     : register(s9);

static const float PI = 3.14159265359;

// --- CSM shadow helpers ------------------------------------------------------

float csmSplit(int i)
{
    if (i == 0) return cascadeSplits.x;
    if (i == 1) return cascadeSplits.y;
    if (i == 2) return cascadeSplits.z;
    return cascadeSplits.w;
}

float csmSample(int layer, float3 wp, float bias)
{
    // Same texel size the GLSL derives with textureSize().
    uint sw, sh, se;
    shadowMapArray.GetDimensions(sw, sh, se);
    float2 ts = 1.0 / float2(sw, sh);

    float4 ls = mul(lightSpaceMatrices[layer], float4(wp, 1.0));
    float3 p  = ls.xyz / ls.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
        return 0.0;

    float s = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            // The engine exposes the cascade array through a comparison sampler,
            // so the depth test happens in the sampler: SampleCmp returns 1 for
            // "lit".  The GLSL counts shadowed taps, hence the inversion.
            float lit = shadowMapArray.SampleCmpLevelZero(
                shadowSampler, float3(p.xy + float2(x, y) * ts, (float)layer), p.z - bias);
            s += 1.0 - lit;
        }
    }
    return s / 9.0;
}

float csmShadow(float3 wp, float3 n)
{
    if (!enableShadow || !receiveShadow) return 0.0;
    float fd = abs(mul(viewMatrix, float4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
        if (fd < csmSplit(i)) { layer = i; break; }
    float bias = max(0.0025 * (1.0 - dot(normalize(n), float3(0.0, 1.0, 0.0))), 0.0004);
    float sh = csmSample(layer, wp, bias);
    float sf = csmSplit(layer);
    float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
        sh = lerp(sh, csmSample(layer + 1, wp, bias), saturate((fd - (sf - band)) / band));
    return sh;
}

// --- PBR lighting (GGX + Smith + Schlick Fresnel) ---------------------------

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

float3 calcPBR(float3 albedo, float metallic, float roughness, float3 F0,
               float3 n, float3 v, float3 l, float3 radiance)
{
    float3 h     = normalize(v + l);
    float NdotH = max(dot(n, h), 0.0);
    float NdotV = max(dot(n, v), 0.0);
    float NdotL = max(dot(n, l), 0.0);
    float NDF   = distGGX(NdotH, roughness);
    float G     = geoSmith(NdotV, NdotL, roughness);
    float3 F    = fresnelSchlick(max(dot(h, v), 0.0), F0);
    float3 kD   = (1.0 - F) * (1.0 - metallic);
    float3 spec = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

// --- Helpers ----------------------------------------------------------------

float heightBlend(float weight, float height, float sharpness)
{
    return clamp(weight * sharpness - (sharpness - 1.0) * 0.5, 0.0, 1.0);
}

// Normal from world position via screen-space derivatives — robust for terrain,
// independent of vertex normal / tangent quality.
float3 computeDisplacedNormal(float3 worldPos)
{
    float3 dx = ddx(worldPos);
    float3 dy = ddy(worldPos);
    return normalize(cross(dx, dy));
}

float4 PSMain(VSOutput input) : SV_Target
{
    // ----- Sample splat map -------------------------------------------------
    float4 splat = u_SplatMap.Sample(u_Sampler, input.v_texCoord);

    // ----- Accumulate blended PBR inputs ------------------------------------
    float3 albedo    = float3(0.0, 0.0, 0.0);
    float3 normal    = float3(0.0, 0.0, 1.0);
    float  roughness = 0.0;
    float  metallic  = 0.0;
    float  ao        = 0.0;
    float  totalWeight = 0.0;

    // ----- Per-layer sampling -----------------------------------------------
    for (int i = 0; i < 4; i++)
    {
        float weight = splat[i];
        if (weight <= 0.001)
            continue;

        float2 uv = input.v_texCoord * u_Tiling[i];

        // Height blending using the albedo luminance as a proxy height.
        float aWeight = weight;
        {
            float4 a = u_AlbedoMap[i].Sample(u_Sampler, uv);
            float  h = dot(a.rgb, float3(0.299, 0.587, 0.114));
            aWeight = heightBlend(weight, h, u_BlendSharpness);
        }

        // Sample albedo
        float4 albedoSample = u_AlbedoMap[i].Sample(u_Sampler, uv);
        albedo += albedoSample.rgb * aWeight;

        // Normal / roughness / metallic / AO maps have no bound unit (see the
        // header note); the GLSL reads unit 0 for them, so sample the splat map
        // to keep the look identical until they get @var entries and registers.
        float3 normSample = u_SplatMap.Sample(u_Sampler, uv).rgb;
        normal    += normSample * aWeight;
        roughness += u_SplatMap.Sample(u_Sampler, uv).r * aWeight;
        metallic  += u_SplatMap.Sample(u_Sampler, uv).r * aWeight;
        ao        += u_SplatMap.Sample(u_Sampler, uv).r * aWeight;

        totalWeight += aWeight;
    }

    // ----- Normalize blended results ----------------------------------------
    if (totalWeight > 0.001)
    {
        albedo    /= totalWeight;
        normal    /= totalWeight;
        roughness /= totalWeight;
        metallic  /= totalWeight;
        ao        /= totalWeight;
    }
    else
    {
        albedo    = float3(0.5, 0.5, 0.5);
        normal    = float3(0.5, 0.5, 1.0);
        roughness = 0.5;
        metallic  = 0.0;
        ao        = 1.0;
    }

    // ----- Geometric normal from displaced world position -------------------
    float3 N = computeDisplacedNormal(input.v_worldPos);

    // ----- PBR lighting -----------------------------------------------------
    float3 v  = normalize(float3(0.0, 0.0, 0.0) - input.v_worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Sun lights (directional)
    float shadow = csmShadow(input.v_worldPos, N);
    for (int si = 0; si < sunLightNum; si++)
    {
        float3 l        = normalize(-sunLights[si].direction);
        float3 radiance = sunLights[si].diffuse * sunLights[si].power;
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, radiance) * (1.0 - shadow);
    }

    // Point lights
    for (int pi = 0; pi < pointLightNum; pi++)
    {
        float3 l    = normalize(pointLights[pi].position - input.v_worldPos);
        float  dist = length(pointLights[pi].position - input.v_worldPos);
        float  att  = 1.0 / (pointLights[pi].constant + pointLights[pi].linear * dist +
                             pointLights[pi].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, pointLights[pi].diffuse * att);
    }

    // Spot lights
    for (int li = 0; li < spotLightNum; li++)
    {
        float3 l      = normalize(spotLights[li].position - input.v_worldPos);
        float  theta  = dot(l, normalize(-spotLights[li].direction));
        float  eps    = spotLights[li].cutOff - spotLights[li].outerCutOff;
        float  intens = clamp((theta - spotLights[li].outerCutOff) / eps, 0.0, 1.0);
        float  dist   = length(spotLights[li].position - input.v_worldPos);
        float  att    = 1.0 / (spotLights[li].constant + spotLights[li].linear * dist +
                               spotLights[li].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l,
                      spotLights[li].diffuse * att * intens);
    }

    // ----- Ambient ----------------------------------------------------------
    float3 ambient = sceneAmbient * albedo * ao;

    // Skybox ambient
    if (skyboxAmbientEnabled)
    {
        float3 skyColor = skyboxMap.Sample(skyboxSampler, reflect(-v, N)).rgb;
        ambient += skyColor * skyboxAmbientStrength * ao;
    }

    // ----- Final color ------------------------------------------------------
    return float4(ambient + Lo, 1.0);
}
