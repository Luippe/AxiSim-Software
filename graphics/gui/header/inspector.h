#pragma once
#include "pch.h"
#include "buffer_manager.h"
#include "shader.h"
#include "graphics_struct.h"


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

	// ----------dependencies-----------
	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;
	Colormap& colormap;

	// ----------inspector region-----------
	int nrBase = 0;
	int nzBase = 0;

	std::vector<float> quadVertices;

	// ----------resources-----------
	TextureBuffer textureBuffer;
	FrameBuffer frameBuffer;
	VertexBuffer vertexBuffer;
	Shader inspectorShader;

	// ----------image state-----------
	float aspect;
	float boxWidth, boxHeight;
	int startX, startY;
	int endX, endY;
	int imageWidth, imageHeight;


	// ----------rectangle selection-----------
	bool dragging = false;
	bool isRectReady = false;

	float zoom = 1.0f;

	float u0 = 0.0f;
	float v0 = 0.0f;
	float u1 = 1.0f;
	float v1 = 1.0f;

	ImVec2 zoomCenter = ImVec2(0.5f, 0.5f);
	ImVec2 initMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMouseIndex = ImVec2(0.0f, 0.0f);
	ImVec2 currentMousePos = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos1 = ImVec2(0.0f, 0.0f);
	ImVec2 rectPos2 = ImVec2(0.0f, 0.0f);



	// -----------selected data--------------
	const float circleRadius = 5.0f;
	std::vector<int> selectedIndices;
	std::vector<InspectorPoint> points;
	std::vector<ImVec2> textPos;


	// create a rectangular quad
	void createFullScreenQuad();

	// get and return the loction of the mouse in index notation
	ImVec2 getMouseIndex();

	// render the preview onto fbo
	void renderPreview();

	// create buffer using the scalarImage
	void createBuffer();

	// resize the image so it fits the window region
	void resizeImage();

	// draw rectangle when mouse is dragged
	void displayRect();

	// handle mouse events
	void handleMouse();

	// display text at positions given by textPos
	void displayTextValue();

	// turns i,j coordinates to pixel coordinates
	ImVec2 gridToScreen(float i, float j);



};