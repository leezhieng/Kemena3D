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

    bool  anim      = totalWeight > 0.0;
    vec3  useNormal = anim ? totalNormal    : vertexNormal;
    vec3  useTan    = anim ? totalTangent   : vertexTangent;
    vec3  useBitan  = anim ? totalBitangent : vertexBitangent;

    mat3  nm3       = mat3(normalMatrix);
    vec3  worldPos  = vec3(modelMatrix * totalPos);

    v_worldPos      = worldPos;
    v_color         = vertexColor;
    v_texCoord      = vertexTexCoord;
    v_lightSpacePos = lightSpaceMatrix * vec4(worldPos, 1.0);
    v_T             = nm3 * useTan;
    v_B             = nm3 * useBitan;
    v_N             = nm3 * useNormal;
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
    float glossiness;
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
// @var vec3      material.diffuse   Diffuse
// @var vec3      material.ambient   Ambient
// @var vec3      material.specular   Specular
// @var float     material.shininess  Shininess
// @var float     material.metallic   Metallic
// @var float     material.glossiness Glossiness
// @var vec2      material.tiling     UV Tiling
// @var sampler2D albedoMap           Albedo
// @var sampler2D normalMap           Normal
// @var sampler2D specularMap         Specular Map
// @var sampler2D glossinessMap       Glossiness Map
// @var sampler2D emissiveMap         Emissive
uniform Material  material;
uniform vec3      viewPos;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
uniform sampler2D glossinessMap;
uniform sampler2D emissiveMap;
uniform bool      has_albedoMap;
uniform bool      has_normalMap;
uniform bool      has_specularMap;
uniform bool      has_glossinessMap;
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

vec3 calcSunLight(SunLight light, vec3 norm, vec3 vdir, vec3 specTex, float shininess)
{
    vec3  ldir  = normalize(-light.direction);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power;
}

vec3 calcPointLight(PointLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 specTex, float shininess)
{
    vec3  ldir  = normalize(light.position - fragPos);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float dist  = length(light.position - fragPos);
    float att   = light.power / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * att;
}

vec3 calcSpotLight(SpotLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 specTex, float shininess)
{
    vec3  ldir    = normalize(light.position - fragPos);
    float diff    = max(dot(norm, ldir), 0.0);
    float shine   = max(shininess, 1.0);
    float spec    = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float theta   = dot(ldir, normalize(-light.direction));
    float eps     = light.cutOff - light.outerCutOff;
    float intens  = clamp((theta - light.outerCutOff) / eps, 0.0, 1.0);
    float dist    = length(light.position - fragPos);
    float att     = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power * intens * att;
}

// Fresnel with a roughness-aware ceiling (see mesh_pbr.glsl).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, UE4).
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

void main()
{
    vec2 uv = v_texCoord * material.tiling;

    vec4 diffuseTex  = has_albedoMap   ? texture(albedoMap,   uv) : vec4(1.0);
    vec4 normalTex   = has_normalMap   ? texture(normalMap,   uv) : vec4(0.5, 0.5, 1.0, 1.0);
    vec4 specularTex = has_specularMap ? texture(specularMap, uv) : vec4(1.0);
    vec4 emissiveTex = has_emissiveMap ? texture(emissiveMap, uv) : vec4(0.0);

    // Glossiness scales the specular power (highlight sharpness). The optional
    // map modulates it per-pixel via its red channel.
    float gloss     = material.glossiness * (has_glossinessMap ? texture(glossinessMap, uv).r : 1.0);
    float shininess = material.shininess * gloss;

    vec3 Nn   = normalize(v_N);
    vec3 norm = Nn;
    // Tangent-space normal mapping using a screen-space derivative frame. This
    // does NOT use the mesh's vertex tangents (which are frequently missing or
    // degenerate on imported sub-meshes) — it derives T/B from the world-position
    // and UV gradients, so it works on any sub-mesh that has UVs.
    if (has_normalMap)
    {
        vec3 mapN = normalTex.rgb;
        mapN.g = 1.0 - mapN.g;            // DirectX -> OpenGL green convention
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

    vec3 vdir    = normalize(viewPos - v_worldPos);
    vec3 result  = sceneAmbient * material.ambient;
    vec3 iblSpec = vec3(0.0);

    if (skyboxAmbientEnabled)
    {
        // Material-aware skybox ambient (split-sum IBL). Roughness is derived
        // from the Phong shininess (glossiness-modulated) so matte surfaces get
        // a blurred ambient and glossy surfaces a sharper reflection; the
        // reflection follows the normal-mapped normal. The specular colour sets
        // the dielectric reflectivity, while the metallic factor tints the
        // reflection toward the base colour and suppresses the diffuse term.
        // The specular term is added after the albedo multiply below so it is
        // not tinted by the diffuse map.
        float roughness = clamp(1.0 - shininess / (shininess + 1.0), 0.04, 1.0);
        float NdotV     = max(dot(norm, vdir), 0.0);
        vec3  R         = reflect(-vdir, norm);
        vec3  irradiance  = textureLod(skyboxMap, norm, 8.0).rgb;
        vec3  prefiltered = textureLod(skyboxMap, R, roughness * 6.0).rgb;

        // Fresnel F0: 4% dielectric, blended toward the base colour for metals.
        vec3 F0    = mix(vec3(0.04), material.diffuse, material.metallic);
        vec3 F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD    = (vec3(1.0) - F_amb) * (1.0 - material.metallic);
        // Reflected tint: the specular colour for dielectrics, the base colour
        // for metals (metals have no diffuse response).
        vec3 specTint = mix(material.specular, material.diffuse, material.metallic);

        result  += kD * material.diffuse * irradiance * skyboxAmbientStrength * material.ambient;
        iblSpec  = prefiltered * specTint * envBRDFApprox(vec3(1.0), roughness, NdotV)
                   * skyboxAmbientStrength;
    }

    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 contrib = calcSunLight(sunLights[i], norm, vdir, specularTex.xyz, shininess);
        if (i == 0) // Renderer only casts shadow from the first active sun light.
            contrib *= 1.0 - calcShadow(v_worldPos, norm, sunLights[i].direction);
        result += contrib;
    }
    for (int i = 0; i < pointLightNum; i++) result += calcPointLight(pointLights[i], norm, v_worldPos, vdir, specularTex.xyz, shininess);
    for (int i = 0; i < spotLightNum;  i++) result += calcSpotLight (spotLights[i],  norm, v_worldPos, vdir, specularTex.xyz, shininess);

    fragColor = vec4(clamp(result, 0.0, 1.0), 1.0) * diffuseTex + emissiveTex + vec4(iblSpec, 0.0);
}
