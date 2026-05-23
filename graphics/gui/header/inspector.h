#pragma once
#include "pch.h"

#include "shader.h"
#include "colorbar.h"

#include "buffer_manager.h"
#include "graphics_struct.h"


class Mesh;
class Results;
class Colormap;
struct GridConfig;
class SceneView;


class Inspector {
public:

	Inspector(SceneView& scene, AppAssets& assets);

	void generate();

	// toolbar is drawn first, then the 2D field is rendered using the remaining space
	void render();

	// upload all uniforms to shader
	void uploadUniforms();

	// reset view of inspector
	void resetView();

	// copy surface to clipboard
	void copyActiveSurfaceToClipboard();

	Colorbar colorbar;

	// copy to clipboard variables
	bool pendingCopy = false;
	bool consoleCopy = false;
	int pendingCopyWidth = 1600;
	int pendingCopyHeight = 420;

private:

	// ----------dependencies-----------
	SceneView& scene;
	Mesh& mesh;
	Results& results;
	GridConfig& g;


	// ----------inspector region-----------
	int nrBase = 0;
	int nzBase = 0;

	std::vector<float> quadVertices;

	// ----------resources-----------
	AppAssets& assets;
	FrameBuffer frameBuffer;	// this is a simple buffer as it is 2D
	FrameBuffer offScreenFBO;
	VertexBuffer vertexBuffer;
	Shader inspectorShader;

	// ----------image state-----------
	int startX, startY;
	int endX, endY;
	int imageWidth, imageHeight;


	// ----------rectangle selection-----------
	bool dragging = false;
	bool isRectReady = false;
	bool toggleSelect = false;


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
	const float circleRadius = 3.0f;
	std::vector<int> selectedIndices;
	std::vector<InspectorPoint> points;

	// popup variables
	bool openPopUp = false;
	ImVec2 clickedMousePos;


	
	// create a rectangular quad
	void createFullScreenQuad();

	// get and return the loction of the mouse in index notation
	ImVec2 getMouseIndex();

	// render the preview onto fbo
	void renderPreview();

	// resize the image so it fits the window region
	void resizeImage();

	// draw rectangle when mouse is dragged
	void displayRect();

	// handle mouse events
	void handleMouse();

	// display text at positions given by textPos
	void displayTextValue();

	// draw toolbar at the top of the inspector, which can be used for variety of functions
	void drawToolBar();

	// draw popup menu when right clicked
	void drawPopup();

	// turns i,j coordinates to pixel coordinates
	ImVec2 gridToScreen(float i, float j);

};