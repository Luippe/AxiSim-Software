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
	glm::ivec2 initMouseIndex;
	glm::ivec2 currentMouseIndex;
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
	glm::ivec2 getMouseIndex();

	// render the preview onto fbo
	void renderPreview();

	// create buffer using the scalarImage
	void createBuffer();

	// resize the image so it fits the window region
	void resizeImage();

	// draw rectangle when mouse is dragged
	void drawRect();

	// handle mouse events
	void handleMouse();

	// display value when there is mouse event
	void displayValue();

	// turns i,j coordinates to pixel coordinates
	ImVec2 gridToScreen(int i, int j);

	int nrBase = 0;
	int nzBase = 0;

	float zoom = 1.0f;
	glm::vec2 zoomCenter = glm::vec2(0.5f, 0.5f);
	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;

	std::vector<float> pixels;
	std::vector<float> quadVertices;

};