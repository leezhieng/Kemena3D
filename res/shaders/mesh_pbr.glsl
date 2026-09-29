#version 330 core

const int MAX_BONES         = 128;
const int MAX_BONE_INFLUENCE = 4;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 normalMatrix;
uniform mat4 lightSpaceMatrix;
uniform mat4 finalBonesMatrices[MAX_BONES];

layout(location = 0) in vec3  vertexPosition;
layout(location = 1) in vec3  vertexColor;
layout(location = 2) in vec2  vertexTexCoord;
layout(location = 3) in vec3  vertexNormal;
layout(location = 4) in vec3  vertexTangent;
layout(location = 5) in vec3  vertexBitangent;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4  weights;

out vec3 v_worldPos;
out vec3 v_color;
out vec2 v_texCoord;
out vec4 v_lightSpacePos;
out vec3 v_T;
out vec3 v_B;
out vec3 v_N;

void main()
{
    vec4  totalPos       = vec4(0.0);
    vec3  totalNormal    = vec3(0.0);
    vec3  totalTangent   = vec3(0.0);
    vec3  totalBitangent = vec3(0.0);
    float totalWeight    = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPos       = vec4(0.0);
            totalNormal    = vec3(0.0);
            totalTangent   = vec3(0.0);
            totalBitangent = vec3(0.0);
            break;
        }
        totalPos       += finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0) * weight;
        mat3 nm         = transpose(inverse(mat3(finalBonesMatrices[boneID])));
        totalNormal    += nm * vertexNormal    * weight;
        totalTangent   += nm * vertexTangent   * weight;
        totalBitangent += nm * vertexBitangent * weight;
        totalWeight    += weight;
    }
    if (totalWeight == 0.0)
    {
        totalPos       = vec4(vertexPosition, 1.0);
        totalNormal    = vertexNormal;
        totalTangent   = vertexTangent;
        totalBitangent = vertexBitangent;
    }

    bool anim    = totalWeight > 0.0;
    vec3 useN    = anim ? totalNormal    : vertexNormal;
    vec3 useT    = anim ? totalTangent   : vertexTangent;
    vec3 useB    = anim ? totalBitangent : vertexBitangent;

    mat3 nm3     = mat3(normalMatrix);
    vec3 worldPos = vec3(modelMatrix * totalPos);

    v_worldPos      = worldPos;
    v_color         = vertexColor;
    v_texCoord      = vertexTexCoord;
    v_lightSpacePos = lightSpaceMatrix * vec4(worldPos, 1.0);
    v_T             = nm3 * useT;
    v_B             = nm3 * useB;
    v_N             = nm3 * useN;
    gl_Position     = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}

// --- FRAGMENT ---

#version 330 core

struct Material {
    vec2  tiling;
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
    float shininess;
    float metallic;
    float roughness;
};

struct SunLight {
    float power;
    vec3  direction;
    vec3  diffuse;
    vec3  specular;
};

struct PointLight {
    float power;
    vec3  position;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

struct SpotLight {
    float power;
    vec3  position;
    vec3  direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

// Material parameters exposed to the editor's material inspector.
// @var vec3      material.diffuse     Base Color
// @var vec3      material.ambient     Ambient
// @var float     material.metallic    Metallic
// @var float     material.roughness   Roughness
// @var vec2      material.tiling      UV Tiling
// @var sampler2D albedoMap            Albedo
// @var sampler2D normalMap            Normal
// @var sampler2D metallicRoughnessMap Metal/Rough
// @var sampler2D aoMap                AO
// @var sampler2D emissiveMap          Emissive
uniform Material  material;
uniform vec3      viewPos;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicRoughnessMap;
uniform sampler2D aoMap;
uniform sampler2D emissiveMap;
uniform bool      has_albedoMap;
uniform bool      has_normalMap;
uniform bool      has_metallicRoughnessMap;
uniform bool      has_aoMap;
uniform bool      has_emissiveMap;

uniform vec3        sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool        skyboxAmbientEnabled;
uniform float       skyboxAmbientStrength;

uniform int       sunLightNum;
uniform SunLight  sunLights[32];
uniform int       pointLightNum;
uniform PointLight pointLights[32];
uniform int       spotLightNum;
uniform SpotLight spotLights[32];

uniform sampler2DArray shadowMapArray;
uniform mat4  lightSpaceMatrices[4];
// The fragment is its own compilation unit (the loader splits on the
// "// --- FRAGMENT ---" marker), so the view matrix must be declared here even
// though the vertex shader also declares it. It is needed to pick the cascade,
// because the splits are camera near/far-plane distances.
uniform mat4  viewMatrix;
uniform vec4  cascadeSplits;
uniform int   cascadeCount;
uniform float shadowResolution;
uniform bool  enableShadow;
uniform bool  receiveShadow;
uniform int   shadowDebug;
uniform float shadowBias;
uniform float shadowNormalBias;
uniform float shadowNormalOffset; // normal-offset distance in shadow-map texels
uniform float shadowSoftness;     // PCF tap spacing in texels (default 1.0)

in vec3 v_worldPos;
in vec3 v_color;
in vec2 v_texCoord;
in vec4 v_lightSpacePos;
in vec3 v_T;
in vec3 v_B;
in vec3 v_N;

out vec4 fragColor;

// World-space size of one shadow-map texel in a cascade. The light matrices are
// ortho projections whose [0][0] == 1/radius, so this needs no extra uniform.
float csmTexelWorld(int cascade)
{
    float m00 = abs(lightSpaceMatrices[cascade][0][0]);
    return (m00 > 0.0) ? 2.0 / (m00 * max(shadowResolution, 1.0)) : 0.0;
}

// Constant + slope-scaled receiver bias. tan(acos(N·L)) grows without bound at
// grazing angles, so it is clamped (Microsoft, "Common Techniques to Improve
// Shadow Depth Maps").
float csmBias(vec3 norm, vec3 sunDir)
{
    float ndl   = max(dot(norm, normalize(-sunDir)), 0.0);
    float slope = tan(acos(max(ndl, 1e-3)));
    return shadowBias + shadowNormalBias * min(slope, 8.0);
}

// Project a world position into a cascade's [0,1] shadow-map space.
bool csmProject(int cascade, vec3 worldPos, out vec3 proj)
{
    vec4 lsp = lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    proj     = lsp.xyz / lsp.w;
    proj     = proj * 0.5 + 0.5;
    return proj.z <= 1.0 && proj.x >= 0.0 && proj.x <= 1.0 &&
           proj.y >= 0.0 && proj.y <= 1.0;
}

// 5x5 PCF whose tap spacing is driven by shadowSoftness texels.
float csmPCF(int cascade, vec3 proj, float bias, vec2 texel)
{
    float shadow = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        float d = texture(shadowMapArray,
                          vec3(proj.xy + vec2(x, y) * texel, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

float calcShadow(vec3 worldPos, vec3 norm, vec3 sunDir)
{
    // cascadeCount<=0 means the caller (e.g. kOffscreenRenderer) never set up
    // the shadow uniforms — bail rather than sample garbage and return 1.
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;

    // Cascade by view-space depth: cascadeSplits are camera near/far-plane
    // distances, so euclidean distance from the eye would pick too coarse a
    // cascade (and waste shadow-map resolution).
    float viewDepth = abs((viewMatrix * vec4(worldPos, 1.0)).z);
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (viewDepth < cascadeSplits[i]) { cascade = i; break; }
    }

    // Normal-offset: push the receiver along its normal by a fraction of a
    // texel, so contact shadows need almost no depth bias and don't peter-pan.
    vec3 samplePos = worldPos + normalize(norm) * (csmTexelWorld(cascade) * shadowNormalOffset);
    float bias = csmBias(norm, sunDir);

    vec3 proj;
    if (!csmProject(cascade, samplePos, proj))
        return 0.0;

    vec2 texel = vec2(1.0 / max(shadowResolution, 1.0)) * max(shadowSoftness, 0.5);
    float shadow = csmPCF(cascade, proj, bias, texel);

    // Smoothly blend into the next cascade near the split to hide the seam
    // ("Cascaded Shadow Maps").
    float split = cascadeSplits[cascade];
    float band  = split * 0.1;
    if (cascade + 1 < cascadeCount && viewDepth > split - band)
    {
        vec3 proj2;
        if (csmProject(cascade + 1, samplePos, proj2))
        {
            float s2 = csmPCF(cascade + 1, proj2, bias, texel);
            float t  = clamp((viewDepth - (split - band)) / max(band, 1e-4), 0.0, 1.0);
            shadow   = mix(shadow, s2, t);
        }
    }
    return shadow;
}

const float PI = 3.14159265359;

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

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with a roughness-aware ceiling. Keeps rough dielectrics from getting
// a mirror-bright grazing response, matching the split-sum IBL model.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, "Real Shading in
// Unreal Engine 4"). Stands in for a precomputed BRDF LUT so the skybox can be
// integrated with the material's Fresnel response without an extra texture.
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

vec3 calcPBR(vec3 albedo, float metallic, float roughness, vec3 F0,
             vec3 n, vec3 v, vec3 l, vec3 radiance)
{
    vec3  h     = normalize(v + l);
    float NdotH = max(dot(n, h),   0.0);
    float NdotV = max(dot(n, v),   0.0);
    float NdotL = max(dot(n, l),   0.0);
    float NDF   = distGGX(NdotH, roughness);
    float G     = geoSmith(NdotV, NdotL, roughness);
    vec3  F     = fresnelSchlick(max(dot(h, v), 0.0), F0);
    vec3  kD    = (1.0 - F) * (1.0 - metallic);
    vec3  spec  = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

void main()
{
    vec2 uv = v_texCoord * material.tiling;

    vec4  albedoSample = has_albedoMap ? texture(albedoMap, uv) : vec4(1.0);
    vec3  albedo       = material.diffuse * albedoSample.rgb;
    float metallic     = material.metallic;
    float roughness    = material.roughness;

    if (has_metallicRoughnessMap)
    {
        vec2 mr  = texture(metallicRoughnessMap, uv).bg;
        metallic  *= mr.x;
        roughness *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    vec3 Nn   = normalize(v_N);
    vec3 norm = Nn;
    // Screen-space derivative tangent frame — independent of the mesh's vertex
    // tangents (often missing/degenerate on imported sub-meshes); works on any
    // sub-mesh with UVs.
    if (has_normalMap)
    {
        vec3 mapN = texture(normalMap, uv).rgb;
        mapN.g = 1.0 - mapN.g;
        mapN   = mapN * 2.0 - 1.0;

        vec3 dp1  = dFdx(v_worldPos);
        vec3 dp2  = dFdy(v_worldPos);
        vec2 duv1 = dFdx(uv);
        vec2 duv2 = dFdy(uv);
        vec3 dp2perp = cross(dp2, Nn);
        vec3 dp1perp = cross(Nn, dp1);
        vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
        float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-8));
        norm = normalize(mat3(T * invmax, B * invmax, Nn) * mapN);
    }

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 v  = normalize(viewPos - v_worldPos);
    vec3 result = vec3(0.0);

    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 l        = normalize(-sunLights[i].direction);
        vec3 radiance = sunLights[i].diffuse * sunLights[i].power;
        vec3 contrib  = calcPBR(albedo, metallic, roughness, F0, norm, v, l, radiance);
        if (i == 0) // Renderer only casts shadow from the first active sun light.
            contrib *= 1.0 - calcShadow(v_worldPos, norm, sunLights[i].direction);
        result += contrib;
    }
    for (int i = 0; i < pointLightNum; i++)
    {
        vec3  l    = normalize(pointLights[i].position - v_worldPos);
        float dist = length(pointLights[i].position - v_worldPos);
        float att  = pointLights[i].power /
                     (pointLights[i].constant + pointLights[i].linear * dist
                      + pointLights[i].quadratic * dist * dist);
        result += calcPBR(albedo, metallic, roughness, F0, norm, v, l, pointLights[i].diffuse * att);
    }
    for (int i = 0; i < spotLightNum; i++)
    {
        vec3  l      = normalize(spotLights[i].position - v_worldPos);
        float theta  = dot(l, normalize(-spotLights[i].direction));
        float eps    = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float dist   = length(spotLights[i].position - v_worldPos);
        float att    = spotLights[i].power * intens /
                       (spotLights[i].constant + spotLights[i].linear * dist
                        + spotLights[i].quadratic * dist * dist);
        result += calcPBR(albedo, metallic, roughness, F0, norm, v, l, spotLights[i].diffuse * att);
    }

    float ao      = has_aoMap      ? texture(aoMap,      uv).r : 1.0;
    vec4  emissive = has_emissiveMap ? texture(emissiveMap, uv) : vec4(0.0);

    vec3 ambient = sceneAmbient * material.ambient * albedo;
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
        float NdotV      = max(dot(norm, v), 0.0);
        vec3  R          = reflect(-v, norm);
        vec3  irradiance = textureLod(skyboxMap, norm, 8.0).rgb;
        vec3  prefiltered = textureLod(skyboxMap, R, roughness * 6.0).rgb;

        vec3  F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3  kD    = (1.0 - F_amb) * (1.0 - metallic);

        ambient += (kD * albedo * irradiance +
                    prefiltered * envBRDFApprox(F0, roughness, NdotV)) *
                   skyboxAmbientStrength * material.ambient;
    }

    result = ambient * ao + result + emissive.rgb;
    fragColor = vec4(result, albedoSample.a);
}
