#version 330 core

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D fieldTex;
uniform sampler2D uColormap;

uniform float vmin;
uniform float vmax;

void main()
{
    float value = texture(fieldTex, TexCoord).r;

    float denom = max(vmax - vmin, 1e-12);
    float t = clamp((value - vmin) / denom, 0.0, 1.0);

    vec3 color = texture(uColormap, vec2(0.5, t)).rgb;

    FragColor = vec4(color, 1.0);
}