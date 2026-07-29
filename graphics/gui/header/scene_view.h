#pragma once
#include "mouse_picker.h"

#include "renderer.h"
#include "axis_gizmo.h"
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

	// Square on to the model and framed on it, turning about its middle. Driven
	// by the Results toolbar's Home button, which resets this and the inspector
	// together so the two views end up looking at the same thing -- and they
	// land on the same plane, since the inspector draws that same z-r section.
	void resetView();

	Camera3D camera;
	Renderer renderer;
	Colormap colormap;
	AxisGizmo axisGizmo;

	// A second, world-anchored copy of the navigation triad, drawn at the scene
	// origin and scaled to the model so the axes read as arrows sitting in the
	// scene rather than a flat cross. Kept separate from axisGizmo (the corner
	// triad) on purpose: sharing it would light this one up and sprout negative
	// arms on it whenever the corner is hovered, since those are member state.
	AxisGizmo originAxis;

	// navigation triad in the corner of the viewport; its pixel size lives on
	// the gizmo itself. No longer exposed in the View menu -- it is how the
	// camera is aimed, not decoration, so there is nothing to turn off.
	bool showAxisGizmo = true;

	// flat colored line cross through world zero. Decoration only -- it is not
	// clickable, the gizmo is what drives the camera.
	bool showOriginAxis = true;

	Console& console;
	MousePicker picker;	// picker depends on camera and renderer being initialized first
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

	// a press that started on the navigation triad snaps the camera on release
	// instead of picking, and must not pan the scene in between
	bool pressedOnGizmo = false;

	// whether the camera has been framed on the model now loaded. Cleared by
	// createBuffer, so a result that has just been generated or loaded frames
	// itself on the next render -- Inspector::pendingFrame for this view.
	bool framedOnModel = false;

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

	// Extent of the revolved model in world space, measured off the geometry
	// being drawn. World x is the axis of revolution and the radial plane is
	// y/z, so the solid is bounded by an axial span and a radius. False when
	// there is nothing loaded. Recomputed every frame rather than cached -- it
	// is three floats, and it then cannot go stale when the mesh is rebuilt or
	// the display length unit changes.
	bool modelBounds(float& axialMin, float& axialMax, float& radius) const;

	// middle of the model in world space, which is what the camera turns about
	static glm::vec3 modelCentre(float axialMin, float axialMax);

	// handle mouse inputs
	void handleMouse();

	// Ctrl + arrow keys orbit without the mouse. Runs through the same drag
	// path, so it honours the selected rotation style and sensitivity.
	void handleKeyboard(ImGuiIO& io);

	// how fast Ctrl + arrows orbit, as the pixels of drag they stand in for
	// per second
	static constexpr float keyRotateSpeed = 260.0f;

	// upload all uniforms onto shader
	void uploadUniforms();

	// update instances for instanced rendering
	void updateSelectedInstances();

	// draw the main 3d space
	void draw3DPreview();

	// Coordinate axes anchored at the scene origin: gizmo-style colored arrows
	// on +x/+y/+z, plus much longer black dotted reference lines through zero.
	// Both are sized off the model so they clear it and scale with it.
	void drawOriginAxes();

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

	// full 360-degree revolution, matching the structured solid-of-revolution view
	const float usSweep = 6.28318530718f;

	Results& results;
	Project& project;
};
