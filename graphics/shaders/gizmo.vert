#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in float aId;

out vec3 vNormalView;
out vec3 vColor;
out float vHighlight;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// id of the group to brighten, or anything out of range for none
uniform float uHighlight;

void main()
{
	// the gizmo is only ever uniformly scaled, so the view rotation is enough
	// to carry the normal into eye space
	vNormalView = mat3(view) * mat3(model) * aNormal;
	vColor = aColor;

	// ids are small integers, so an exact-enough match on halves is fine
	vHighlight = abs(aId - uHighlight) < 0.5 ? 1.0 : 0.0;

	gl_Position = projection * view * model * vec4(aPos, 1.0);
}
