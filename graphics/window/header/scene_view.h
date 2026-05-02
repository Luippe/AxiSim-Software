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
	SceneView(Display& disp, Camera& camera, Renderer& renderer, Bounding& bound);

	void render();
	bool hovered = false;
	bool focused = false;

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

private:

	Display& disp;
	FrameBuffer frameBuffer;
	Shader shaderMesh;
	Shader shaderEdge;
	Shader shaderLine;
	Shader shaderResults;
};