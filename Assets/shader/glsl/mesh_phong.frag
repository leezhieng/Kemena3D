#version 330 core

in vec3 vertexPositionFrag;
in vec3 vertexColorFrag;
in vec2 texCoordFrag;
in vec3 vertexNormalFrag;
//in vec3 vertexTangentFrag;
//in vec3 vertexBitangentFrag;

uniform mat4 normalMatrix;
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec3 viewPos;

in vec3 T;
in vec3 B;
in vec3 N;

out vec4 fragColor;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
uniform sampler2D emissiveMap;

uniform bool has_albedoMap;
uniform bool has_normalMap;
uniform bool has_specularMap;
uniform bool has_emissiveMap;

// Cascaded shadow maps (one array layer per cascade)
uniform sampler2DArray shadowMapArray;
uniform mat4  lightSpaceMatrices[4];
uniform vec4  cascadeSplits;     // view-space far distance of each cascade
uniform int   cascadeCount;
uniform float shadowResolution;
uniform bool  shadowDebug;
uniform bool  enableShadow;
uniform bool  receiveShadow;

int gShadowCascade = 0;          // cascade used for the current fragment (debug)

uniform float alphaCutoff = 0.2;

struct Material {
	vec2 tiling;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
}; 
uniform Material material;

uniform vec3 sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool  skyboxAmbientEnabled;
uniform float skyboxAmbientStrength;

struct SunLight
{
	float power;
    vec3 direction;

    vec3 diffuse;
    vec3 specular;
};
uniform int sunLightNum;
uniform SunLight sunLights[32];

struct PointLight
{
	float power;
    vec3 position;

    float constant;
    float linear;
    float quadratic;

    vec3 diffuse;
    vec3 specular;
};
uniform int pointLightNum;
uniform PointLight pointLights[32];

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

    vec3 diffuse;
    vec3 specular;
};
uniform int spotLightNum;
uniform SpotLight spotLights[32];

float csmSplit(int i)
{
    if (i == 0) return cascadeSplits.x;
    if (i == 1) return cascadeSplits.y;
    if (i == 2) return cascadeSplits.z;
    return cascadeSplits.w;
}

float sampleCascade(int layer, vec3 worldPos, float bias)
{
    vec4 lsPos = lightSpaceMatrices[layer] * vec4(worldPos, 1.0);
    vec3 p = lsPos.xyz / lsPos.w;
    p = p * 0.5 + 0.5;

    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
        return 0.0;

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMapArray, 0).xy);
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float d = texture(shadowMapArray, vec3(p.xy + vec2(x, y) * texelSize, float(layer))).r;
            shadow += (p.z - bias > d) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float ShadowCalculation(vec3 worldPos, vec3 normal)
{
    if (!enableShadow || !receiveShadow) return 0.0;

    float fragDepth = abs((viewMatrix * vec4(worldPos, 1.0)).z);

    // Pick the first cascade whose far split still contains the fragment.
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
    {
        if (fragDepth < csmSplit(i)) { layer = i; break; }
    }
    gShadowCascade = layer;

    float bias = max(0.0025 * (1.0 - dot(normalize(normal), vec3(0.0, 1.0, 0.0))), 0.0004);

    float shadow = sampleCascade(layer, worldPos, bias);

    // Smoothly blend into the next cascade across the split boundary.
    float splitFar = csmSplit(layer);
    float band     = splitFar * 0.1;
    if (layer + 1 < cascadeCount && fragDepth > splitFar - band)
    {
        float next = sampleCascade(layer + 1, worldPos, bias);
        float t    = clamp((fragDepth - (splitFar - band)) / band, 0.0, 1.0);
        shadow     = mix(shadow, next, t);
    }
    return shadow;
}

vec3 CalcSunLight(SunLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 specularTexture, float shadow)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.diffuse * material.diffuse * diff * light.power;
    vec3 reflectDir = reflect(-lightDir, normal);
    float shininess = max(material.shininess, 1.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.001), shininess);
    vec3 specular = light.specular * material.specular * spec * specularTexture * light.power;
    return (diffuse + specular) * (1.0 - shadow);
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 specularTexture)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float shininess = max(material.shininess, 1.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.001), shininess);
    float distance = length(light.position - fragPos);
    float attenuation = light.power / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
    vec3 diffuse  = light.diffuse  * material.diffuse  * diff;
    vec3 specular = light.specular * material.specular * spec * specularTexture;
    diffuse  *= attenuation;
    specular *= attenuation;
    return (diffuse + specular);
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 specularTexture)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = light.diffuse * material.diffuse * diff * light.power;
    vec3 reflectDir = reflect(-lightDir, normal);
    float shininess = max(material.shininess, 1.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.001), shininess);
    vec3 specular = light.specular * material.specular * spec * specularTexture * light.power;
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = (light.cutOff - light.outerCutOff);
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    diffuse  *= intensity;
    specular *= intensity;
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
    diffuse  *= attenuation;
    specular *= attenuation;
    return (diffuse + specular);
}

void main()
{
	vec4 diffuseTexture = has_albedoMap ? texture(albedoMap, texCoordFrag * material.tiling) : vec4(1.0, 1.0, 1.0, 1.0);
	vec4 normalTexture = has_normalMap ? texture(normalMap, texCoordFrag * material.tiling) : vec4(0.5, 0.5, 1.0, 1.0);
	vec4 specularTexture = has_specularMap ? texture(specularMap, texCoordFrag * material.tiling) : vec4(0.0);
	vec4 emissiveTexture = has_emissiveMap ? texture(emissiveMap, texCoordFrag * material.tiling) : vec4(0.0);
	
	vec3 norm;
	if (has_normalMap)
	{
		// Construct TBN matrix (T, B, N are already world-space from vertex shader)
		vec3 Tn = normalize(T);
		vec3 Bn = normalize(B);
		vec3 Nn = normalize(N);
		mat3 TBN = mat3(Tn, Bn, Nn);

		// Sample tangent-space normal, flip G for OpenGL convention
		vec3 tangentNormal = normalize(normalTexture.rgb * 2.0 - 1.0);
		tangentNormal.g = -tangentNormal.g;

		// Transform to world space
		norm = normalize(TBN * tangentNormal);
	}
	else
	{
		// No normal map — use interpolated vertex normal (already world-space)
		norm = normalize(N);
	}

    vec3 viewDir = normalize(viewPos - vertexPositionFrag);

	//if (diffuseTexture.a < alphaCutoff)
		//discard;

	// Shadow (computed once for first sun light)
	float shadow = ShadowCalculation(vertexPositionFrag, norm);

	// Scene ambient
	vec3 result = sceneAmbient * material.ambient;
	if (skyboxAmbientEnabled)
		result += texture(skyboxMap, norm).rgb * skyboxAmbientStrength * material.ambient;

	// Sun lighting
	if (sunLightNum > 0)
	{
		for(int i = 0; i < sunLightNum; i++)
		{
			result += CalcSunLight(sunLights[i], norm, vertexPositionFrag, viewDir, specularTexture.xyz, shadow);
		}
	}
	
    // Point lights
	if (pointLightNum > 0)
	{
		for(int i = 0; i < pointLightNum; i++)
		{
			result += CalcPointLight(pointLights[i], norm, vertexPositionFrag, viewDir, specularTexture.xyz); 
		}
	}
	
    // Spot light
	if (spotLightNum > 0)
	{
		for(int i = 0; i < spotLightNum; i++)
		{
			result += CalcSpotLight(spotLights[i], norm, vertexPositionFrag, viewDir, specularTexture.xyz);
		}
	}
	
	//result = pow(result, vec3(1.0/2.2));  // Convert to gamma space
	
	// Show lighting
	//fragColor = vec4(result, 1.0);
	//fragColor = vec4(result, 1.0) * vec4(1.0);  // Should be same as above
	
	// Show Combined
    //fragColor = vec4(result, 1.0) * diffuseTexture;
	fragColor = vec4(clamp(result, 0.0, 1.0), 1.0) * diffuseTexture + emissiveTexture;

	// Cascade debug view: tint each cascade a distinct colour.
	if (enableShadow && shadowDebug && receiveShadow)
	{
		vec3 tint = vec3(1.0, 0.4, 0.4);
		if      (gShadowCascade == 1) tint = vec3(0.4, 1.0, 0.4);
		else if (gShadowCascade == 2) tint = vec3(0.4, 0.4, 1.0);
		else if (gShadowCascade >= 3) tint = vec3(1.0, 1.0, 0.4);
		fragColor.rgb *= tint;
	}

	// Flat
	//fragColor = diffuseTexture;
	
	// Show normal
	//fragColor = vec4(norm * 0.5 + 0.5, 1.0);
	//fragColor = normalTexture;
	
	return;
}