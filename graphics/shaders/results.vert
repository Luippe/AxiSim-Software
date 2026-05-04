#version 330 core

layout (location = 0) in vec3 aDir;
layout (location = 1) in float aXCoord;
layout (location = 2) in float aRadialCoord;
layout (location = 3) in vec4 instanceData;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 localPos;

void main()
{
    float x0 = instanceData.x;
    float x1 = instanceData.y;
    float innerR = instanceData.z;
    float outerR = instanceData.w;

    float x = mix(x0, x1, aXCoord);
    float r = mix(innerR, outerR, aRadialCoord);

    vec3 pos = vec3(
        x,
        r * aDir.y,
        r * aDir.z
    );

    localPos = pos;

    gl_Position = projection * view * model * vec4(pos, 1.0);
}