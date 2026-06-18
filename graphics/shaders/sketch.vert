#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec2 viewMin;
uniform vec2 viewMax;

void main()
{
    vec2 p = (aPos - viewMin) / (viewMax - viewMin);
    p = p * 2.0 - 1.0;

    gl_Position = vec4(p, 0.0, 1.0);
}