#version 330 core

out vec4 FragColor;

in vec3 color;
flat in int remove;

void main()
{
	if (remove == 1) {
		discard;
	}
	FragColor = vec4(color, 1.0);
}