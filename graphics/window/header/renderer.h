#pragma once

#include "buffer_manager.h"

class Shader;

// class for rendering general objects on screen
class Renderer {
public:
	Renderer();

	// Black dotted reference lines along x, y and z, running both ways through
	// the origin. The geometry is built ONCE in unit space -- each axis reaches
	// +-1 -- so the caller sets the length purely by the model matrix it loads
	// on `shaderLine` before the call, the same way the axis gizmo is scaled.
	void renderDottedAxes(Shader& shaderLine);

	// thin, so the dashes read as dots rather than a heavy stripe
	float lineWidth = 2.5f;

private:

	void createDottedAxisBuffer();

	VertexBuffer dottedBuffer;
	int dottedVertexCount = 0;

	// one dash-plus-gap in unit axis space; the dash count per axis is fixed, so
	// scaling the whole thing up keeps the pattern proportional at any size
	static constexpr float dashLength = 0.04f;
	static constexpr float gapLength = 0.035f;
};
