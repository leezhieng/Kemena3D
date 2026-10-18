// ===========================================================================
// TerrainPBR.glsl — PBR Terrain Shader with 4-channel texture splatting
// ===========================================================================
//
// Uses a RGBA8 control (splat) map to blend up to four terrain materials,
// each with its own albedo, normal, roughness, metallic, and AO texture.
// Lighting: PBR (GGX + Smith + Schlick Fresnel) with CSM shadows.
//
// No height displacement — vertex positions carry the height directly.
//
// ===========================================================================

// --- VERTEX ---
#version 330 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexColor;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 vertexNormal;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 normalMatrix;

out vec3 v_worldPos;
out vec3 v_color;
out vec2 v_texCoord;

void main()
{
    vec4 worldPos = modelMatrix * vec4(vertexPosition, 1.0);
    // No height displacement — vertex Y positions are set directly from terrain height data.
    v_worldPos = worldPos.xyz;
    v_color    = vertexColor;
    v_texCoord = texCoord;

    gl_Position = projectionMatrix * viewMatrix * worldPos;
}

// --- FRAGMENT ---
#version 330 core

// ===========================================================================
// Material
// ===========================================================================
struct Material
{
    vec3  diffuse;
    vec3  ambient;
    vec3  specular;
    float shininess;
    float metallic;
    float roughness;
};
uniform Material material;

// ===========================================================================
// Light structures
// ===========================================================================
struct SunLight
{
    float power;
    vec3  direction;
    vec3  diffuse;
    vec3  specular;
};

struct PointLight
{
    float power;
    vec3  position;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

struct SpotLight
{
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

uniform int        sunLightNum;
uniform SunLight   sunLights[32];
uniform int        pointLightNum;
uniform PointLight pointLights[32];
uniform int        spotLightNum;
uniform SpotLight  spotLights[32];

uniform vec3        sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool        skyboxAmbientEnabled;
uniform float       skyboxAmbientStrength;

// ===========================================================================
// CSM (Cascaded Shadow Maps)
// ===========================================================================
uniform mat4           viewMatrix;
uniform sampler2DArray shadowMapArray;
uniform mat4           lightSpaceMatrices[4];
uniform vec4           cascadeSplits;
uniform int            cascadeCount;
uniform float          shadowResolution;
uniform bool           enableShadow;
uniform bool           receiveShadow;
uniform float          shadowBias;
uniform float          shadowNormalBias;
uniform float          shadowNormalOffset;
uniform float          shadowSoftness;

// ===========================================================================
// Terrain textures
// ===========================================================================
// Material parameters exposed to the editor's material inspector.
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
uniform sampler2D u_SplatMap;          // RGBA8 control map
uniform sampler2D u_AlbedoMap[4];      // 4 albedo/diffuse textures
uniform sampler2D u_NormalMap[4];      // 4 normal maps
uniform sampler2D u_RoughnessMap[4];   // 4 roughness maps
uniform sampler2D u_MetalnessMap[4];   // 4 metallic maps
uniform sampler2D u_AOMap[4];          // 4 AO maps
// u_Tiling is declared as [16] below (covers both the legacy 4-layer path and
// the 16-layer texture-array path).
uniform float     u_BlendSharpness;    // Blend edge sharpness (0.5 - 4.0)

// --- Stochastic / triplanar controls -----------------------------------------
// Learned from the "Procedural Stochastic Terrain Shader" and the
// "SuperSimpleTerrainShader": per-repeat random rotation to kill visible tiling,
// world-space UVs so the pattern stitches across neighbouring tiles, triplanar
// projection on cliffs, and procedural noise variegation.
// @var float     u_TileWorldSize       Tile World Size
// @var float     u_Stochastic          Stochastic Anti-Tiling
// @var float     u_Triplanar           Triplanar (enable)
// @var float     u_TriplanarStrength   Triplanar Cliffs
// @var float     u_TriplanarSharpness  Triplanar Sharpness
// @var float     u_RotateUV            Rotate UV (enable)
// @var float     u_UVRotation          UV Rotation
// @var float     u_NoiseVariegation    Noise Variegation
// @var float     u_NoiseScale          Noise Scale
uniform float u_TileWorldSize;          // World size of one tile (for world-space UVs)
uniform float u_Stochastic;             // 0 = off, 1 = full per-repeat rotation
uniform float u_Triplanar;              // 0 = top-down projection only, 1 = blend side planes on cliffs
uniform float u_TriplanarStrength;      // 0 = flat only, 1 = full triplanar on cliffs
uniform float u_TriplanarSharpness;     // Slope falloff exponent for the triplanar blend
uniform float u_RotateUV;               // 0 = off, 1 = rotate the world-space UVs
uniform float u_UVRotation;             // UV rotation in radians (used when u_RotateUV is 1)
uniform float u_NoiseVariegation;       // Amount of procedural noise tint variation
uniform float u_NoiseScale;             // Noise frequency (1 / world units)
// @var float     u_MaskedBlend         Masked Weight Blending
uniform float u_MaskedBlend;            // 0 = luminance height blend, 1 = albedo-alpha mask blend

// --- Texture-array (16-layer) terrain path -----------------------------------
// When u_UseArrays is 1 the terrain renders from four RGBA weight-map textures
// (16 layers) plus per-layer albedo / normal / ORM / mask sampler2DArrays —
// the Cartographer-style texture-array terrain material.  When 0 the legacy
// 4-layer splat-map path runs instead.
// @var float u_UseArrays
uniform float u_UseArrays;
// @var float u_Tiling[16]
uniform float u_Tiling[16];
// Per-layer mask source: the layer index (0..15) whose painted weight masks this
// layer, or -1 for "auto" (the layer's own weight). Written by the Terrain panel.
uniform float u_MaskChannel[16];
uniform sampler2D u_WeightMap0;
uniform sampler2D u_WeightMap1;
uniform sampler2D u_WeightMap2;
uniform sampler2D u_WeightMap3;
uniform sampler2DArray u_AlbedoArray;
uniform sampler2DArray u_NormalArray;
uniform sampler2DArray u_ORMArray;
uniform sampler2DArray u_MaskArray;

// ===========================================================================
// Varying inputs
// ===========================================================================
in vec3 v_worldPos;
in vec3 v_color;
in vec2 v_texCoord;

out vec4 fragColor;

// ===========================================================================
// CSM shadow map helpers
//
// Follows the Microsoft DX tech articles: normal-offset sampling, constant +
// slope-scaled receiver bias, softness-driven PCF, and a smooth blend across
// the cascade boundary.
// ===========================================================================
float csmSplit(int i)
{
    if (i == 0) return cascadeSplits.x;
    if (i == 1) return cascadeSplits.y;
    if (i == 2) return cascadeSplits.z;
    return cascadeSplits.w;
}

// World-space size of one shadow-map texel for a cascade (light ortho [0][0]==1/radius).
float csmTexelWorld(int layer)
{
    float m = abs(lightSpaceMatrices[layer][0][0]);
    return (m > 0.0) ? 2.0 / (m * max(shadowResolution, 1.0)) : 0.0;
}

// Constant + slope-scaled receiver bias (clamped tan(acos(N·L))).
float csmBias(vec3 n, vec3 l)
{
    float ndl = max(dot(normalize(n), normalize(l)), 0.0);
    return shadowBias + shadowNormalBias * min(tan(acos(max(ndl, 1e-3))), 8.0);
}

bool csmProject(int layer, vec3 wp, out vec3 p)
{
    vec4 ls = lightSpaceMatrices[layer] * vec4(wp, 1.0);
    p = ls.xyz / ls.w;
    p = p * 0.5 + 0.5;
    return p.z <= 1.0 && p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0;
}

float csmPCF(int layer, vec3 p, float bias)
{
    // Screen-space slope bias. On a dense heightfield the shadow depth changes far
    // faster across one pixel than any constant / N·L-scaled bias can cover, which
    // is what produced the triangle-aligned acne all over the terrain. Scaling the
    // bias by the per-pixel depth gradient is the standard cure and only ever adds
    // bias where the surface is steep relative to the light.
    bias += max(abs(dFdx(p.z)), abs(dFdy(p.z))) * 2.0;

    vec2 ts = (1.0 / vec2(textureSize(shadowMapArray, 0).xy)) * max(shadowSoftness, 0.5);
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += (p.z - bias > texture(shadowMapArray, vec3(p.xy + vec2(x, y) * ts, float(layer))).r) ? 1.0 : 0.0;
    return s / 9.0;
}

float csmShadow(vec3 wp, vec3 n, vec3 lightDir)
{
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;
    vec3 l = normalize(-lightDir);
    float fd = abs((viewMatrix * vec4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
        if (fd < csmSplit(i)) { layer = i; break; }
    // Normal-offset, then constant + slope-scaled depth bias.
    vec3 samplePos = wp + normalize(n) * (csmTexelWorld(layer) * shadowNormalOffset);
    float bias = csmBias(n, l);
    vec3 p;
    if (!csmProject(layer, samplePos, p)) return 0.0;
    float sh = csmPCF(layer, p, bias);
    float sf = csmSplit(layer);
    float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
    {
        vec3 p2;
        if (csmProject(layer + 1, samplePos, p2))
        {
            float t = clamp((fd - (sf - band)) / max(band, 1e-4), 0.0, 1.0);
            sh = mix(sh, csmPCF(layer + 1, p2, bias), t);
        }
    }
    return sh;
}

// ===========================================================================
// PBR lighting (GGX + Smith + Schlick Fresnel)
// ===========================================================================
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

// ===========================================================================
// Height blend helper: sharpens transitions between layers
// ===========================================================================
float heightBlend(float weight, float height, float sharpness)
{
    return clamp(weight * sharpness - (sharpness - 1.0) * 0.5, 0.0, 1.0);
}

// ===========================================================================
// Compute normal from world position via screen-space derivatives.
// This is robust for terrain — works regardless of vertex normal/tangent quality.
// ===========================================================================
vec3 computeDisplacedNormal(vec3 worldPos)
{
    vec3 dx = dFdx(worldPos);
    vec3 dy = dFdy(worldPos);
    vec3 n = normalize(cross(dx, dy));
    // Terrain is a heightfield with no overhangs, so its geometric normal always
    // points up. The cross product's handedness depends on the window-space axes,
    // which flipped the relief shading (slopes facing the sun read dark) — force
    // the normal into the +Y hemisphere.
    if (n.y < 0.0)
        n = -n;
    return n;
}

// ===========================================================================
// Stochastic (anti-tiling) + triplanar helpers
//
// Adapted from the reference shaders:
//  * The UV of every texture repeat is rotated by a per-repeat random angle so
//    the same patch of texture never lines up twice ("stochastic").
//  * The rotation is faded back to the plain projection at the repeat border,
//    which hides the seams the rotation would otherwise leave.
//  * UVs are derived from the world position, so the pattern is unique per world
//    cell and stays seamless across neighbouring terrain tiles.
//  * On steep faces the projection is blended toward the side planes so cliff
//    textures stop stretching ("triplanar").
// ===========================================================================
float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

// Smooth value noise in [0,1].
float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// 1 at the centre of a texture repeat, 0 on its border.
float cellEdgeMask(vec2 uv)
{
    vec2 f = abs(fract(uv) - 0.5) * 2.0;
    return 1.0 - max(f.x, f.y);
}

// Rotate the UV around the centre of its repeat cell by a per-cell angle.
vec2 stochasticRotate(vec2 uv, float seed)
{
    vec2 cell = floor(uv);
    float a = hash21(cell + seed) * 6.28318530718;
    float c = cos(a);
    float s = sin(a);
    vec2 d = fract(uv) - 0.5;
    return cell + 0.5 + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
}

// Optional global UV rotation. Rotating the *world* XZ plane keeps neighbouring
// tiles aligned (they all use the same plane), which breaks the axis-aligned
// repeat pattern without reintroducing tile seams.
vec2 rotateWorldUV(vec2 p)
{
    if (u_RotateUV <= 0.5)
        return p;
    float c = cos(u_UVRotation);
    float s = sin(u_UVRotation);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// World-space UV: dividing the absolute world XZ by the tile size makes UVs
// continue across tile boundaries instead of restarting at 0..1 per tile.
vec2 terrainWorldUV()
{
    float tile = (u_TileWorldSize > 0.001) ? u_TileWorldSize : 256.0;
    return rotateWorldUV(v_worldPos.xz) / tile;
}

// Sample one legacy albedo slot. The texture is passed in by value: GLSL 330 does
// NOT allow indexing a sampler array with a variable (`u_AlbedoMap[i]`), and that
// made the whole fragment shader fail to compile — which corrupted the entire
// viewport, not just the terrain. Call sites pass a literal index instead.
vec4 sampleAlbedoSlot(sampler2D tex, int i, vec3 N, float triW)
{
    vec2 uvTop = terrainWorldUV() * u_Tiling[i];

    vec4 top = texture(tex, uvTop);
    if (u_Stochastic > 0.0)
    {
        vec2 ruv = stochasticRotate(uvTop, float(i) * 31.7);
        top = mix(top, texture(tex, ruv),
                  cellEdgeMask(uvTop) * clamp(u_Stochastic, 0.0, 1.0));
    }

    if (triW <= 0.001)
        return top;

    float tile = (u_TileWorldSize > 0.001) ? u_TileWorldSize : 256.0;
    vec2 uvX = (rotateWorldUV(v_worldPos.zy) / tile) * u_Tiling[i];
    vec2 uvZ = (rotateWorldUV(v_worldPos.xy) / tile) * u_Tiling[i];
    vec4 side = texture(tex, uvX) * abs(N.x) +
                texture(tex, uvZ) * abs(N.z);
    side /= max(abs(N.x) + abs(N.z), 1e-4);

    return mix(top, side, clamp(triW, 0.0, 1.0));
}

// Sample one layer's albedo with stochastic anti-tiling on the dominant (top)
// projection and a triplanar blend toward the side planes on steep slopes.
vec4 sampleLayerAlbedo(int i, vec3 N, float triW)
{
    if (i <= 0) return sampleAlbedoSlot(u_AlbedoMap[0], 0, N, triW);
    if (i == 1) return sampleAlbedoSlot(u_AlbedoMap[1], 1, N, triW);
    if (i == 2) return sampleAlbedoSlot(u_AlbedoMap[2], 2, N, triW);
    return sampleAlbedoSlot(u_AlbedoMap[3], 3, N, triW);
}

// Weight of layer l (0..15) from the four packed RGBA weight-map textures.
float getWeightLayer(int l)
{
    if (l < 4)  return texture(u_WeightMap0, v_texCoord)[l];
    if (l < 8)  return texture(u_WeightMap1, v_texCoord)[l - 4];
    if (l < 12) return texture(u_WeightMap2, v_texCoord)[l - 8];
    return texture(u_WeightMap3, v_texCoord)[l - 12];
}

// Albedo of layer i from the albedo sampler2DArray (stochastic + triplanar).
vec4 sampleLayerAlbedoArray(int i, vec3 N, float triW)
{
    vec2 uvTop = terrainWorldUV() * u_Tiling[i];
    vec4 top = texture(u_AlbedoArray, vec3(uvTop, float(i)));
    if (u_Stochastic > 0.0)
    {
        vec2 ruv = stochasticRotate(uvTop, float(i) * 31.7);
        top = mix(top, texture(u_AlbedoArray, vec3(ruv, float(i))),
                  cellEdgeMask(uvTop) * clamp(u_Stochastic, 0.0, 1.0));
    }
    if (triW <= 0.001)
        return top;

    float tile = (u_TileWorldSize > 0.001) ? u_TileWorldSize : 256.0;
    vec2 uvX = (rotateWorldUV(v_worldPos.zy) / tile) * u_Tiling[i];
    vec2 uvZ = (rotateWorldUV(v_worldPos.xy) / tile) * u_Tiling[i];
    vec4 side = texture(u_AlbedoArray, vec3(uvX, float(i))) * abs(N.x) +
                texture(u_AlbedoArray, vec3(uvZ, float(i))) * abs(N.z);
    side /= max(abs(N.x) + abs(N.z), 1e-4);
    return mix(top, side, clamp(triW, 0.0, 1.0));
}

// ===========================================================================
// main()
// ===========================================================================
void main()
{
    // ----- Geometric normal from the displaced world position ---------------
    // Computed up front: the surface slope drives the triplanar cliff blend.
    // Using screen-space derivatives (dFdx/dFdy) — always valid, no TBN needed.
    vec3 N = computeDisplacedNormal(v_worldPos);

    // Triplanar weight — grows as the surface tilts away from horizontal.
    // Opt-in: when disabled the projection stays purely top-down, so the strength /
    // sharpness controls have an unambiguous on/off switch.
    float slope = clamp(1.0 - abs(N.y), 0.0, 1.0);
    float triW  = 0.0;
    if (u_Triplanar > 0.5)
        triW = pow(slope, max(u_TriplanarSharpness, 1.0)) *
               clamp(u_TriplanarStrength, 0.0, 1.0);

    // ----- Sample splat map -------------------------------------------------
    // Only the legacy path uses the RGBA control map; the array path leaves
    // u_SplatMap unbound (its samplers are the packed weight maps), and sampling
    // an unbound sampler would read whatever is bound to unit 0.
    bool arrayPath = (u_UseArrays > 0.5);
    vec4 splat = arrayPath ? vec4(1.0) : texture(u_SplatMap, v_texCoord);

    // ----- Accumulate blended PBR inputs ------------------------------------
    vec3 albedo    = vec3(0.0);
    vec3 normal    = vec3(0.0, 0.0, 1.0); // tangent-space normal accumulation
    float roughness = 0.0;
    float metallic  = 0.0;
    float ao        = 0.0;
    float totalWeight = 0.0;

    // ----- Per-layer sampling -----------------------------------------------
    // Array path (u_UseArrays): 16 layers from four weight maps + texture arrays.
    // Legacy path: 4 layers from the RGBA splat map + per-layer sampler2D maps.
    int layerCount = arrayPath ? 16 : 4;
    for (int i = 0; i < layerCount; i++)
    {
        // Weight of this layer.
        //  * Array path: each layer owns a weight inside the four packed RGBA
        //    weight maps, so layer i reads weight map i/4, component i%4. A
        //    u_MaskChannel[i] >= 0 redirects the layer onto that layer's weight
        //    instead (mask-by-layer); -1 means "auto" (its own weight). Painting a
        //    layer changes exactly this weight — reading only u_SplatMap (= weight
        //    map 0) is why painting layers 4..15 used to have no visible effect.
        //  * Legacy path: all four layers share the u_SplatMap RGBA control map,
        //    so layer i falls back to channel min(i, 3).
        int mc = int(u_MaskChannel[i]);
        float weight;
        if (arrayPath)
            weight = (mc < 0) ? getWeightLayer(i) : getWeightLayer(clamp(mc, 0, 15));
        else
            weight = splat[clamp(mc < 0 ? i : mc, 0, 3)];
        if (weight <= 0.001)
            continue;

        // World-space detail UV.
        vec2 uv = terrainWorldUV() * u_Tiling[i];

        vec4  a;
        vec3  normSample;
        float rSample, mSample, aoSample, maskVal;

        if (arrayPath)
        {
            a          = sampleLayerAlbedoArray(i, N, triW);
            normSample = texture(u_NormalArray, vec3(uv, float(i))).xyz;
            vec3 orm   = texture(u_ORMArray, vec3(uv, float(i))).rgb;
            rSample    = orm.g; mSample = orm.b; aoSample = orm.r;
            maskVal    = texture(u_MaskArray, vec3(uv, float(i))).a;
        }
        else
        {
            // Only the splat control map and the four albedo maps are bound in the
            // legacy path (the weight maps / arrays claim the rest of the engine's
            // eight material texture units), so the remaining PBR inputs come from
            // neutral constants instead of samplers that would read unit 0.
            a          = sampleLayerAlbedo(i, N, triW);
            normSample = vec3(0.5, 0.5, 1.0);
            rSample    = 0.5;
            mSample    = 0.0;
            aoSample   = 1.0;
            maskVal    = a.a;
        }

        // Blend height / mask (Cartographer "masked weight blending").
        float h  = dot(a.rgb, vec3(0.299, 0.587, 0.114)); // luminance as height
        float hm = mix(h, maskVal, clamp(u_MaskedBlend, 0.0, 1.0));
        float aWeight = heightBlend(weight, hm, max(u_BlendSharpness, 0.001));

        albedo    += a.rgb * aWeight;
        normal    += normSample * aWeight;
        roughness += rSample * aWeight;
        metallic  += mSample * aWeight;
        ao        += aoSample * aWeight;
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
        // Fallback: white albedo, flat normal, default roughness/metallic
        albedo    = vec3(0.5);
        normal    = vec3(0.5, 0.5, 1.0);
        roughness = 0.5;
        metallic  = 0.0;
        ao        = 1.0;
    }

    // ----- Procedural noise variegation -------------------------------------
    // Breaks up large uniform expanses (noise-driven variegation, as in the
    // SuperSimpleTerrainShader).
    if (u_NoiseVariegation > 0.0)
    {
        float n = valueNoise(v_worldPos.xz * max(u_NoiseScale, 0.0001));
        albedo *= mix(vec3(1.0), vec3(0.82, 0.86, 0.78), n * u_NoiseVariegation);
    }

    // ----- PBR lighting -----------------------------------------------------
    vec3 v     = normalize(vec3(0.0) - v_worldPos); // view direction (toward camera)
    vec3 F0    = mix(vec3(0.04), albedo, metallic);
    vec3 Lo    = vec3(0.0);

    // Sun lights (directional)
    float shadow = csmShadow(v_worldPos, N,
                             sunLightNum > 0 ? sunLights[0].direction : vec3(0.0, -1.0, 0.0));
    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 l = normalize(-sunLights[i].direction);
        vec3 radiance = sunLights[i].diffuse * sunLights[i].power;
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, radiance) * (1.0 - shadow);
    }

    // Point lights
    for (int i = 0; i < pointLightNum; i++)
    {
        vec3 l = normalize(pointLights[i].position - v_worldPos);
        float dist = length(pointLights[i].position - v_worldPos);
        float att  = 1.0 / (pointLights[i].constant + pointLights[i].linear * dist +
                            pointLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, pointLights[i].diffuse * att);
    }

    // Spot lights
    for (int i = 0; i < spotLightNum; i++)
    {
        vec3 l    = normalize(spotLights[i].position - v_worldPos);
        float theta = dot(l, normalize(-spotLights[i].direction));
        float eps   = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float dist   = length(spotLights[i].position - v_worldPos);
        float att    = 1.0 / (spotLights[i].constant + spotLights[i].linear * dist +
                              spotLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l,
                      spotLights[i].diffuse * att * intens);
    }

    // ----- Ambient ----------------------------------------------------------
    vec3 ambient = sceneAmbient * albedo * ao;

    // Skybox ambient — physically-based (split-sum IBL) so the terrain's
    // metallic / roughness / normal (and per-layer AO) shape the result
    // instead of the sky simply being overlaid.
    if (skyboxAmbientEnabled)
    {
        float NdotV = max(dot(N, v), 0.0);
        vec3  R     = reflect(-v, N);
        vec3  irradiance  = textureLod(skyboxMap, N, 8.0).rgb;
        vec3  prefiltered = textureLod(skyboxMap, R, roughness * 6.0).rgb;

        vec3 F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD    = (1.0 - F_amb) * (1.0 - metallic);

        ambient += (kD * albedo * irradiance +
                    prefiltered * envBRDFApprox(F0, roughness, NdotV)) *
                   skyboxAmbientStrength * ao;
    }

    // ----- Final color ------------------------------------------------------
    fragColor = vec4(ambient + Lo, 1.0);
}
