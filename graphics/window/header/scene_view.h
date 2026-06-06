#pragma once
#include "mouse_picker.h"
#include "buffer_manager.h"
#include "colormap.h"
#include "results.h"
#include "shader.h"
#include "mesh.h"
#include "solver.h"
#include "solver_struct.h"

class Display;
class Camera;
class Renderer;
class Bounding;

class SceneView {
public:

	struct SceneScale{
		uint8_t index = 0;
		float value = 1.0f;
	};

	SceneView(Display& disp, Camera& camera, Renderer& renderer, Bounding& bound);

	void render();

	void updateSceneScale();

	SceneScale sceneScale;
	Config config;
	ViewTab currentTab = TAB_MESH;
	Mesh mesh;
	Solver solver;
	Results results;
	Colormap colormap;
	Camera& camera;
	Renderer& renderer;
	Bounding& bound;
	MousePicker picker;	// picker depends on camera, renderer, and bound being initialized first
	ImVec2 rectPos;		// top left corner of window
	ImVec2 rectSize;	// width and height of window

private:

	bool hovered = false;
	bool focused = false;
	bool dragging = false;
	bool rotating = false;
	bool leftMouseDown = false;

	float initX = 0.0f;
	float initY = 0.0f;

	unsigned int samples = 4;

	// handle mouse inputs
	void handleMouse();

	Display& disp;
	FrameBuffer frameBuffer;
	Shader shaderLine;
	Shader shaderResults;
};