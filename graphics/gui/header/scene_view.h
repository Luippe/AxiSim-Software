#pragma once
#include "mouse_picker.h"

#include "renderer.h"
#include "bounding.h"
#include "colormap.h"
#include "shader.h"
#include "camera.h"

#include "solver_struct.h"
#include "graphics_struct.h"

#include "buffer_manager.h"

class Display;
class Project;
class GUI;
class Results;

class SceneView {
public:

	struct SceneScale{
		uint8_t index = 0;
		float value = 1.0f;
	};


	SceneView(Project& project, GUI& gui);

	void render();

	void generate();

	void updateSceneScale();

	// create buffer for the cylinder and cylinder instances using the vertices and indices from results class
	void createBuffer();

	SceneScale sceneScale;

	Camera camera;
	Renderer renderer;
	Bounding bound{ renderer };
	Colormap colormap;

	Console& console;
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

	std::vector<CylinderInstance> selectedInstances;
	std::vector<Vertex> vertices;

	VertexBuffer cvInstanceBuffer;
	ElementBuffer cvElementBuffer;

	// reduce number of instances by combining control volume for rows
	std::vector<CylinderInstance> createRowMergedCylinderInstances(
		std::vector<float>& field,
		FilterValues& filterValues
	);



	bool compareFloat(float value, FilterValues& filterValues);

	// handle mouse inputs
	void handleMouse();

	// upload all uniforms onto shader
	void uploadUniforms();

	// update instances for instanced rendering
	void updateSelectedInstances();

	// draw the main 3d space
	void draw3DPreview();

	FrameBuffer frameBuffer;
	VertexBuffer cvBuffer;
	Shader shaderLine;
	Shader shaderResults;
	Results& results;
	Project& project;
};