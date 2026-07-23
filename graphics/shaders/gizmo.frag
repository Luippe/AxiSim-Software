#version 330 core

in vec3 vNormalView;
in vec3 vColor;
in float vHighlight;

out vec4 FragColor;

// headlight shading: the light sits at the eye, so the triad keeps the same
// look from every camera angle and never goes fully dark on a back face
uniform float ambient;

void main()
{
	vec3 n = normalize(vNormalView);

	// two-sided: cone caps and the inside of a shaft still catch light
	float diff = abs(n.z);

	// soften the terminator so the arms read as rounded rather than faceted
	diff = ambient + (1.0 - ambient) * (0.5 + 0.5 * diff) * diff;

	// hovered arm washes toward white so it is obviously the click target
	vec3 color = mix(vColor, min(vColor * 1.6 + 0.35, vec3(1.0)), vHighlight);

	FragColor = vec4(color * diff, 1.0);
}
