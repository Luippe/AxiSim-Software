#pragma once

#include "buffer_manager.h"

class Shader;

// class for rendering general objects on screen
class Renderer {
public:
	Renderer();
	void renderAxis(Shader& shaderLine);
	int axis_length = 3;
	
private:
	void createAxisBuffer();
	VertexBuffer axisBuffer;
	
};
