#pragma once
#include "pch.h"
#include "buffer_manager.h"
#include "shader.h"

class Mesh;
class Results;
class Colormap;
struct GridConfig;
class SceneView;

class Inspector {
public:

	Inspector(SceneView& scene);

	void generate();
	void render();

	// update the texture buffer
	void updateTextureBuffer(const void* data);

	// upload all uniforms to shader
	void uploadUniforms();

	bool keepAspectRatio = false;

private:

	float aspect;
	int startX, startY;
	int endX, endY;
	float boxWidth, boxHeight;
	bool dragging = false;
	int imageWidth, imageHeight;
	glm::vec2 initMousePos;
	glm::vec2 currentMousePos;
	std::vector<int> selectedIndices;

	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;
	Colormap& colormap;

	TextureBuffer textureBuffer;
	FrameBuffer frameBuffer;
	VertexBuffer vertexBuffer;
	Shader inspectorShader;

	// create a rectangular quad
	void createFullScreenQuad();

	// get and return the loction of the mouse in index notation
	glm::vec2 getMouseIndex();

	// handle all mouse events
	void updateSelectedIndices();

	// render the preview onto fbo
	void renderPreview();

	// create buffer using the scalarImage
	void createBuffer();

	// resize the image so it fits the window region
	void resizeImage();

	// draw rectangle when mouse is dragged
	void drawRect();

	int nrBase = 0;
	int nzBase = 0;

	std::vector<float> pixels;
	std::vector<float> quadVertices;

};