#version 330 core
layout (location = 0) in vec3 aP0;
layout (location = 1) in vec3 aP1;
layout (location = 2) in vec3 aC0;
layout (location = 3) in vec3 aC1;
layout (location = 4) in vec3 aColor;

out vec3 color;
flat out int remove;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	
	mat4 MVP =  projection * view * model;
	vec4 p0 = MVP * vec4(aP0, 1.0);
	vec4 p1 = MVP * vec4(aP1, 1.0);
	vec4 c0 = MVP * vec4(aC0, 1.0);
	vec4 c1 = MVP * vec4(aC1, 1.0);

	// normalize
	p0 /= p0.w;
	p1 /= p1.w;
	c0 /= c0.w;
	c1 /= c1.w;

	// make origin at p0
	p1 -= p0;
	c0 -= p0;
	c1 -= p0;

	if (sign(p1.x*c0.y - p1.y*c0.x) == sign(p1.x*c1.y - p1.y*c1.x)) {
		remove = 0;
	}
	else {
		remove = 1;
	}

	gl_Position = MVP * vec4(aP0, 1.0);
	color = aColor;
}