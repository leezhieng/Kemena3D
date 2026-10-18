#version 330 core

const int MAX_BONES         = 128;
const int MAX_BONE_INFLUENCE = 4;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 finalBonesMatrices[MAX_BONES];

// Hardware instancing toggle. 0 = use the per-object modelMatrix uniform,
// 1 = use the per-instance matrix stream below.
uniform int  useInstancing;

layout(location = 0) in vec3  vertexPosition;
layout(location = 1) in vec3  vertexColor;
layout(location = 2) in vec2  vertexTexCoord;
layout(location = 3) in vec3  vertexNormal;
layout(location = 4) in vec3  vertexTangent;
layout(location = 5) in vec3  vertexBitangent;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4  weights;
// Per-instance model matrix at location 8 (..11). GL only guarantees 16 attribute
// slots (0..15), so location 16 was invalid on some drivers and hid every
// instanced mesh. See mesh_pbr.glsl.
layout(location = 8) in mat4 instanceModelMatrix;

out vec2 v_texCoord;

void main()
{
    vec4  totalPos    = vec4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES) { totalPos = vec4(0.0); break; }
        totalPos    += finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0) * weight;
        totalWeight += weight;
    }
    if (totalWeight == 0.0) totalPos = vec4(vertexPosition, 1.0);

    mat4 model  = (useInstancing != 0) ? instanceModelMatrix : modelMatrix;
    v_texCoord  = vertexTexCoord;
    gl_Position = projectionMatrix * viewMatrix * model * totalPos;
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
// Material parameters exposed to the editor's material inspector.
// @var vec3      material.diffuse Color
// @var vec2      material.tiling  UV Tiling
// @var sampler2D albedoMap        Albedo
uniform Material  material;
uniform sampler2D albedoMap;
uniform bool      has_albedoMap;

in  vec2 v_texCoord;
out vec4 fragColor;

void main()
{
    vec4 albedo = has_albedoMap
        ? texture(albedoMap, v_texCoord * material.tiling)
        : vec4(1.0);
    fragColor = vec4(material.diffuse, 1.0) * albedo;
}
