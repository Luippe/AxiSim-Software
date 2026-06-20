#version 330 core

layout (location = 0) in vec3 aPos;     // revolved 3D position (z, r*cos, r*sin)
layout (location = 1) in float aValue;  // field value at this vertex

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out float vValue;

void main()
{
    vValue = aValue;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
