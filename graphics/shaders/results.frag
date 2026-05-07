#version 330 core

out vec4 FragColor;

in vec3 localPos;

uniform sampler2D fieldTex;
uniform sampler2D uColormap;

uniform float L;        // full domain length
uniform float R;  // full domain radius
uniform float vmin;
uniform float vmax;

void main() {
    float r = length(localPos.yz);
    float x = localPos.x;

    float u = clamp(x / L, 0.0, 1.0);
    float v = clamp(r / R, 0.0, 1.0);

    float value = texture(fieldTex, vec2(u, v)).r;

    float denom = max(vmax - vmin, 1e-12);
    float t = clamp((value - vmin) / denom, 0.0, 1.0);

    vec3 color = texture(uColormap, vec2(0.5,t)).rgb;

    FragColor = vec4(color, 1.0);
}