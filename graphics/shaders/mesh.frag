#version 330 core

out vec4 FragColor;
in float val;

void main()
{
	vec3 color = 0.5 * vec3(val);
	FragColor = vec4(color, 1.0);
}