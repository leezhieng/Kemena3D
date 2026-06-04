// --- VERTEX ---

#version 450

// Input

layout (location = 0) in vec3 vertexPosition;
layout (location = 2) in vec2 texCoord;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

// Output

out vec3 vertexPositionFrag;
out vec2 texCoordFrag;

void main()
{
	mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;

	texCoordFrag = texCoord;

	gl_Position = mvp * vec4(vertexPosition, 1.0);
}

// --- FRAGMENT ---

#version 450

in vec2 texCoordFrag;

out vec4 fragColor;

uniform sampler2D albedoMap;

void main()
{
	vec4 diffuseTexture = texture(albedoMap, texCoordFrag);

	fragColor = diffuseTexture;
}
