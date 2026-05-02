#version 330 core

out vec4 FragColor;

in vec3 localPos;

uniform sampler2D uColormap;
uniform sampler2D fieldTex;
uniform float R;
uniform float L;
uniform float vmin;
uniform float vmax;


void main()
{
    float r = length(localPos.yz);
    float z = localPos.x;

    float u = clamp(z / L, 0.0, 1.0);
    float v = clamp(r / R, 0.0, 1.0);

    float value = texture(fieldTex, vec2(u, v)).r;
    float t = clamp((value - vmin) / (vmax - vmin), 0.0, 1.0);
    vec3 color = texture(uColormap, vec2(0.5, t)).rgb;


//    FragColor = vec4(color, 1.0);
    FragColor = vec4(vec3(0.8,0.5,0.5),1.0);
}