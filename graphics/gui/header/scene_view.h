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

#include <string>
#include <vector>

class Display;
class Project;
class GUI;
class Results;

class SceneView {
public:

	SceneView(Project& project, GUI& gui);

	void render();

	void generate();

	// create buffer for the cylinder and cylinder instances using the vertices and indices from results class
	void createBuffer();

	// force the unstructured 3D surface to rebuild on the next frame
	void markUnstructuredDirty();

	Camera3D camera;
	Renderer renderer;
	Bounding bound{ renderer };
	Colormap colormap;

	Console& console;
	MousePicker picker;	// picker depends on camera, renderer, and bound being initialized first
	ImVec2 rectPos;		// top left corner of window
	ImVec2 rectSize;	// width and height of window

private:

	// the scene is a pane of the Results dockspace (GUI::drawResultsViewport); the
	// class keeps its node free of a tab bar, matching the other viewers
	ImGuiWindowClass windowClass;

	bool hovered = false;
	bool focused = false;
	bool dragging = false;
	bool rotating = false;
	bool leftMouseDown = false;

	float initX = 0.0f;
	float initY = 0.0f;

	unsigned int samples = 4;

	std::vector<CylinderInstance> selectedInstances;

	VertexBuffer cvInstanceBuffer;
	ElementBuffer cvElementBuffer;

	// reduce number of instances by combining control volume for rows
	std::vector<CylinderInstance> createRowMergedCylinderInstances(
		std::vector<float>& field,
		FilterValues& filterValues
	);

	bool compareFloat(float value, FilterValues& filterValues);

	// ---------------- unstructured (revolved) result rendering ----------------

	// raw per-cell values for the currently selected field (nullptr if none)
	const std::vector<double>* currentUnstructuredField() const;

	// true when the cached revolved surface no longer matches the UI state
	bool unstructuredNeedsRebuild();

	// revolve the 2D triangulation into a 3D surface colored by the field
	void buildUnstructuredSurface();

	// draw the cached revolved surface
	void drawUnstructured3D();

	// upload colormap/value-range uniforms for the unstructured shader
	void uploadUnstructuredUniforms();

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

	// unstructured revolved-surface rendering
	Shader shaderResultsUnstructured;
	VertexBuffer usBuffer;
	std::vector<float> usVertexData;	// interleaved: position.xyz, value
	int usVertexCount = 0;
	bool usDirty = true;

	// cached build signature (rebuild when any of these change)
	std::string usFieldName;
	int usShading = -1;
	int usCompare = -1;
	float usValueAt = 0.0f;
	float usValueLower = 0.0f;
	float usValueUpper = 0.0f;

	// revolution sweep angle (270 degrees) so the cut plane stays visible
	const float usSweep = 4.71238898038f;

	Results& results;
	Project& project;
};
